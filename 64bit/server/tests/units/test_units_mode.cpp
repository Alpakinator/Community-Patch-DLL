// test_units_mode.cpp - tests of the mode word's unit bit (epic 0040, issue 4753; server/src/units/srv_units_mode.h, the
// wiring in srv_render_scene.*, the two host keys in srv_render_host.cpp). Written 2026-11-17 for this repository
// (GPL-3.0-or-later). Build: server/evidence/0040/3/build_mode_test.py (links build_units_tests.py's objects).
//
//   test_units_mode.exe [case ...]     every case (or the named ones); "CASE <name> PASS|FAIL <why>" per case
// Exit: 0 every case passed, 1 a case failed, 2 no case matched. No device, no install, no game.
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "srv_render_common.h"
#include "srv_render_host.h"
#include "srv_render_scene.h"
#include "units/srv_units_mode.h"
#include "units/srv_units_scene.h"

#include "srv_options.h"

// server.exe's SrvOptions() (server/src/srv_options.cpp) for the render units: every option at its default
static Vp64Opt::Options g_options;
const Vp64Opt::Options& SrvOptions() { return g_options; }

using namespace srvunits;

namespace {

std::vector<std::string> g_log;
std::string g_fail;

void Log(void*, const char* line) { g_log.push_back(line); }
void HostLog(const char* line) { g_log.push_back(line); }

#define EXPECT(c)                                                                                                          \
	do                                                                                                                     \
	{                                                                                                                      \
		if(!(c))                                                                                                           \
		{                                                                                                                  \
			char b_[512];                                                                                                  \
			snprintf(b_, sizeof(b_), "line %d: %s", __LINE__, #c);                                                       \
			g_fail = b_;                                                                                                   \
			return false;                                                                                                  \
		}                                                                                                                  \
	} while(0)

size_t LogCount(const std::string& needle)
{
	size_t n = 0;
	for(const std::string& l : g_log)
		n += l.find(needle) != std::string::npos;
	return n;
}

// A test section with the shell_shared.h layout: the header and the mode block (everything up to the camera block).
struct TestSection
{
	std::vector<uint8_t> bytes;
	TestSection() : bytes(VPSHELL_CAMERA_OFFSET, 0) {}
	VPShellHeader* hdr() { return reinterpret_cast<VPShellHeader*>(bytes.data()); }
	VPShellMode* mode() { return VPSHELL_MODE(hdr()); }
	bool allZero() const
	{
		for(size_t i = VPSHELL_MODE_OFFSET; i < VPSHELL_MODE_OFFSET + sizeof(VPShellMode); ++i)
			if(bytes[i])
				return false;
		return true;
	}
};

const uint32_t kPid = 4753;
const int64_t kFreq = 1000;   // 1 tick = 1 ms

// A writer on, attached to sec, with the given options; clock ticks are ms.
void Make(ModeWriter& w, TestSection& sec, bool exe = true, double holdMs = 2000)
{
	w.setOptions(holdMs, exe);
	w.configure(true, kPid, kFreq, &Log, nullptr);
	w.attach(sec.mode());
}

// ------------------------------------------------------------------------------------------------------------- cases
bool CaseLayout()
{
	// the block where 4751 put it: in the zero bytes between the header and the camera block
	EXPECT(VPSHELL_MODE_OFFSET == 512 && sizeof(VPShellMode) == 72);   // ticket 4785: was 64
	TestSection sec;
	EXPECT((uint8_t*)sec.mode() == sec.bytes.data() + 512);
	EXPECT((uint8_t*)&sec.mode()->writerPid == sec.bytes.data() + 516);
	EXPECT((uint8_t*)&sec.mode()->changes == sec.bytes.data() + 520);
	EXPECT((uint8_t*)&sec.mode()->readyLossTotal == sec.bytes.data() + 524);
	EXPECT((uint8_t*)&sec.mode()->changedQpc == sec.bytes.data() + 528);
	// issue 4761: the consumer word filterState took reserved[0]; the rest stays where it was
	EXPECT((uint8_t*)&sec.mode()->latchedWord == sec.bytes.data() + 536);
	EXPECT((uint8_t*)&sec.mode()->filterState == sec.bytes.data() + 552);
	EXPECT((uint8_t*)&sec.mode()->reserved[0] == sec.bytes.data() + 556 && sizeof(sec.mode()->reserved) == 20);
	EXPECT(VPFILTER_WORD(VPFILTER_CANNOT_DROP, VPFILTER_R_PATH2_REFUSED) == 0x102u && VPFILTER_R_COUNT == 9u);
	// ticket 4785: the mask counters after reserved[5], bit 4, and the old block's bytes unchanged
	EXPECT((uint8_t*)&sec.mode()->droppedUnitMask == sec.bytes.data() + 576 && (uint8_t*)&sec.mode()->passedSetUnitMask == sec.bytes.data() + 580);
	EXPECT(VPMODE_UNIT_SHADOWS == 0x10u && VPMODE_KNOWN == 0x1Fu && VPSHELL_MODE_OFFSET + sizeof(VPShellMode) <= VPSHELL_CAMERA_OFFSET);
	return true;
}

// ticket 4785: bit 4 is written only with the unit bit and while wanted; a change of wanted rewrites the word at once
bool CaseUnitShadowBit()
{
	TestSection sec;
	VPShellMode* m = sec.mode();
	ModeWriter w;
	Make(w, sec);
	w.setUnitShadow(true, 1);   // wanted before the unit bit: nothing written
	EXPECT(sec.allZero() && !w.unitShadowBit());
	w.onFrame(true, 10);        // the unit bit: both
	EXPECT(m->word == (VPMODE_UNITS | VPMODE_UNIT_SHADOWS) && m->changes == 1 && w.unitShadowBit() && w.counters().shadowSets == 1);
	w.setUnitShadow(false, 20);   // not wanted any more: bit 4 cleared at once, the unit bit kept
	EXPECT(m->word == VPMODE_UNITS && m->changes == 2 && m->changedQpc == 20 && w.counters().shadowClears == 1);
	w.setUnitShadow(false, 21);   // no change: no store
	EXPECT(m->changes == 2);
	w.setUnitShadow(true, 30);
	EXPECT(m->word == (VPMODE_UNITS | VPMODE_UNIT_SHADOWS) && m->changes == 3);
	w.onPoll(false, 40);          // the readiness lost: both bits 0
	EXPECT(m->word == 0 && !w.unitShadowBit());
	w.onFrame(true, 50);          // back: both again
	EXPECT(m->word == (VPMODE_UNITS | VPMODE_UNIT_SHADOWS));
	w.onLoadBegin(60);            // a load: both 0
	EXPECT(m->word == 0);
	// another producer's bits 1-3 are kept; bits 5-31 written 0
	m->word = VPMODE_CITIES | 0x100u;
	w.onFrame(true, 70);
	EXPECT(m->word == (VPMODE_CITIES | VPMODE_UNITS | VPMODE_UNIT_SHADOWS));
	// source own: the unit bit stays 0, so bit 4 too
	TestSection s2;
	ModeWriter w2;
	Make(w2, s2, false);
	w2.setUnitShadow(true, 1);
	w2.onFrame(true, 2);
	EXPECT(s2.allZero() && !w2.unitShadowBit());
	EXPECT(LogCount("the server's unit shadows are ready") == 3);
	return true;
}

// ticket 4785: the compositor passes mask draws while it latched both bits: bit 4 withdrawn (LOUD once, not fatal); counts
// from before both bits were latched are not ours
bool CaseUnitShadowDoubled()
{
	TestSection sec;
	VPShellMode* m = sec.mode();
	ModeWriter w;
	Make(w, sec);
	w.setUnitShadow(true, 1);
	w.onFrame(true, 10);
	EXPECT(m->word == (VPMODE_UNITS | VPMODE_UNIT_SHADOWS));
	m->passedSetUnitMask = 7;     // counted before the latch had bit 4
	m->latchedWord = VPMODE_UNITS;
	w.onPoll(true, 11);
	EXPECT(w.unitShadowBit() && w.counters().shadowDoubled == 0);
	m->latchedWord = VPMODE_UNITS | VPMODE_UNIT_SHADOWS;
	w.onPoll(true, 12);           // the base is read (7)
	m->droppedUnitMask = 40;      // dropped draws are fine
	w.onFrame(true, 13);
	EXPECT(w.unitShadowBit() && w.counters().shadowDoubled == 0);
	m->passedSetUnitMask = 9;     // two passed under both bits
	w.onPoll(true, 14);
	EXPECT(!w.unitShadowBit() && m->word == VPMODE_UNITS && w.counters().shadowDoubled == 1 && w.bit() == 1);
	std::string why;
	EXPECT(!w.failed(why));
	EXPECT(LogCount("LOUD: units: mode: the compositor passed 2 unit shadow-mask draws") == 1);
	w.setUnitShadow(false, 15);
	w.setUnitShadow(true, 16);    // withdrawn for the session: wanted again does not set it
	w.onFrame(true, 17);
	EXPECT(!w.unitShadowBit() && m->word == VPMODE_UNITS);
	char line[2048] = "";
	w.appendLine(line, sizeof(line));
	EXPECT(strstr(line, "shadow_bit=0 shadow_wanted=1 shadow_sets=1 shadow_clears=1 shadow_doubled=1 passed_set_mask=9") != nullptr);
	return true;
}

// (a) the bit is 1 only while on, ready and the source exe
bool CaseBitRule()
{
	TestSection sec;
	ModeWriter w;
	Make(w, sec);
	EXPECT(w.phase() == ModeWriter::kStarting && sec.allZero());
	w.onFrame(false, 10);   // not ready: 0, nothing written
	EXPECT(w.bit() == 0 && sec.allZero());
	w.onPoll(true, 11);     // a poll never sets the bit
	EXPECT(w.bit() == 0 && sec.allZero());
	w.onFrame(true, 20);    // ready + exe: 1
	EXPECT(w.bit() == 1 && sec.mode()->word == VPMODE_UNITS && sec.mode()->writerPid == kPid);
	EXPECT(sec.mode()->changes == 1 && sec.mode()->changedQpc == 20 && sec.mode()->readyLossTotal == 0);
	EXPECT(w.phase() == ModeWriter::kLive);
	w.onFrame(true, 30);    // no change: no store
	EXPECT(sec.mode()->changes == 1 && sec.mode()->changedQpc == 20);
	// source own (the product default until 4755): ready never sets the bit and nothing is written
	TestSection s2;
	ModeWriter w2;
	Make(w2, s2, false);
	w2.onFrame(true, 10);
	w2.onFrame(true, 20);
	EXPECT(w2.bit() == 0 && s2.allZero() && w2.counters().notExeFrames == 2);
	EXPECT(LogCount("position source is own") == 1);   // said once
	// off (render_units=0): every call a no-op
	TestSection s3;
	ModeWriter w3;
	w3.setOptions(2000, true);
	w3.configure(false, kPid, kFreq, &Log, nullptr);
	w3.attach(s3.mode());
	w3.onFrame(true, 1);
	w3.onLoadBegin(2);
	w3.onPoll(false, 3);
	w3.onFrame(true, 4);
	EXPECT(w3.bit() == 0 && s3.allZero() && w3.phase() == ModeWriter::kOff && w3.counters().changes == 0);
	// detached: the state runs, nothing is written
	ModeWriter w4;
	w4.setOptions(2000, true);
	w4.configure(true, kPid, kFreq, &Log, nullptr);
	w4.onFrame(true, 5);
	EXPECT(w4.bit() == 1 && w4.counters().sets == 1);
	return true;
}

// the producer part only: bits 1-3 kept, bits 4-31 written 0, the consumer part and the reserved words untouched
bool CaseProducerPartOnly()
{
	TestSection sec;
	VPShellMode* m = sec.mode();
	m->word = VPMODE_CITIES | 0x100u;   // bit 1 (another producer's) and a bit that must be 0
	m->latchedWord = 0xAAAA0001u;
	m->latchedPresent = 77;
	m->droppedTotal = 5;
	m->passedSetTotal = 9;
	m->filterState = 0x11111111u;   // issue 4761 (was reserved[0]): read, never written; state 0x11 is no known state
	m->reserved[4] = 0x55555555u;
	ModeWriter w;
	Make(w, sec);
	w.onFrame(true, 1);
	EXPECT(m->word == (VPMODE_CITIES | VPMODE_UNITS));
	w.onPoll(false, 2);
	EXPECT(m->word == VPMODE_CITIES);
	EXPECT(m->latchedWord == 0xAAAA0001u && m->latchedPresent == 77 && m->droppedTotal == 5 && m->passedSetTotal == 9);
	EXPECT(m->filterState == 0x11111111u && m->reserved[4] == 0x55555555u && m->reserved[0] == 0);
	std::string why;
	EXPECT(!w.failed(why) && w.counters().lastFilterState == 0x11111111u);
	return true;
}

// a word a dead server left (another pid): untouched until this writer's first change, which writes its own pid
bool CaseStaleWriter()
{
	TestSection sec;
	sec.mode()->word = VPMODE_UNITS;
	sec.mode()->writerPid = 1234;
	sec.mode()->changes = 40;
	ModeWriter w;
	Make(w, sec);
	w.onFrame(false, 1);
	EXPECT(sec.mode()->writerPid == 1234 && sec.mode()->changes == 40);
	w.onFrame(true, 2);
	EXPECT(sec.mode()->writerPid == kPid && sec.mode()->word == VPMODE_UNITS && sec.mode()->changes == 41);
	return true;
}

// (b) a new load: 0 at kLoadBegin, 1 at the first ready frame of the load, the transition logged with its length
bool CaseLoad()
{
	TestSection sec;
	ModeWriter w;
	Make(w, sec);
	// the first game's start without a kLoadBegin: the transition runs from the first drawn frame
	w.onFrame(false, 100);
	w.onFrame(false, 150);
	w.onFrame(true, 400);
	EXPECT(w.bit() == 1 && w.counters().transitions == 1 && w.counters().lastTransitionMs == 300);
	EXPECT(w.counters().lastTransitionFrames == 2 && LogCount("start-up transition 1 ended: unit bit 1 after 300 ms since the first drawn frame") == 1);
	// a load
	w.onLoadBegin(1000);
	EXPECT(w.bit() == 0 && sec.mode()->word == 0 && w.phase() == ModeWriter::kLoading && sec.mode()->readyLossTotal == 0);
	EXPECT(w.counters().loads == 1 && w.counters().losses == 0 && LogCount("load 1 begins") == 1);
	w.onPoll(false, 1100);   // not ready during the load: no loss, no bound
	w.onPoll(false, 9000);
	EXPECT(!w.counters().losses && w.phase() == ModeWriter::kLoading);
	std::string why;
	EXPECT(!w.failed(why));
	w.onFrame(false, 9500);   // the load's first drawn frame
	w.onFrame(false, 9600);
	w.onFrame(false, 9700);
	EXPECT(w.bit() == 0);
	w.onFrame(true, 10000);
	EXPECT(w.bit() == 1 && sec.mode()->word == VPMODE_UNITS && w.counters().transitions == 2);
	EXPECT(w.counters().lastTransitionMs == 9000 && w.counters().lastTransitionFrameMs == 500 && w.counters().lastTransitionFrames == 3);
	EXPECT(w.counters().maxTransitionMs == 9000);
	EXPECT(LogCount("after 9000 ms since the load's kLoadBegin (500 ms and 3 not-ready frames") == 1);
	EXPECT(sec.mode()->changes == 3);   // set, cleared at the load, set
	// a load whose scene is ready at once: set at its first drawn frame
	w.onLoadBegin(20000);
	w.onFrame(true, 20016);
	EXPECT(w.bit() == 1 && w.counters().lastTransitionMs == 16 && w.counters().lastTransitionFrames == 0);
	return true;
}

// (c) a loss that recovers within the bound: cleared at once, counted, LOUD, not fatal
bool CaseLossRecovers()
{
	TestSection sec;
	ModeWriter w;
	Make(w, sec);
	w.onFrame(true, 0);
	w.onPoll(false, 250);   // lost between frames (ticket 0641: steps of at most 250 ms here, the step clock = wall time)
	EXPECT(w.bit() == 0 && sec.mode()->word == 0 && sec.mode()->readyLossTotal == 1 && w.phase() == ModeWriter::kLost);
	EXPECT(LogCount("LOUD: units: mode: the unit readiness was lost while the unit bit was set (between frames)") == 1);
	for(int64_t t = 500; t <= 2000; t += 250)
		w.onPoll(false, t);
	w.onFrame(false, 2125);   // still lost at a frame: no second count
	EXPECT(sec.mode()->readyLossTotal == 1 && w.counters().losses == 1);
	w.onFrame(true, 2250);    // back after 2000 ms = the bound: within
	std::string why;
	EXPECT(!w.failed(why) && w.bit() == 1 && sec.mode()->word == VPMODE_UNITS && w.counters().recovered == 1);
	EXPECT(w.counters().lastLossMs == 2000 && w.counters().lastLossWallMs == 2000 && w.counters().fatal == 0);
	EXPECT(LogCount("LOUD: units: mode: the unit readiness is back after 2000 ms of step time (2000 ms wall; bound 2000 ms of "
	                "step time): unit bit 1") == 1);
	// a loss seen at a drawn frame
	w.onFrame(false, 4000);
	EXPECT(w.bit() == 0 && sec.mode()->readyLossTotal == 2 && LogCount("(at a drawn frame)") == 1);
	w.onFrame(true, 4100);
	EXPECT(w.bit() == 1 && w.counters().recovered == 2 && w.counters().maxLossMs == 2000 && w.counters().lastLossMs == 100);
	return true;
}

// (c) a loss beyond the bound: failed() with the fatal text; the bit stays 0 after
bool CaseLossFatal()
{
	TestSection sec;
	ModeWriter w;
	Make(w, sec);
	w.onFrame(true, 0);
	w.onPoll(false, 250);
	for(int64_t t = 500; t <= 2250; t += 250)
		w.onPoll(false, t);   // 2000 ms: not beyond
	std::string why;
	EXPECT(!w.failed(why));
	w.onPoll(false, 2251);
	EXPECT(w.failed(why) && w.phase() == ModeWriter::kFailed && w.counters().fatal == 1);
	EXPECT(why.compare(0, 7, "units: ") == 0 && why.find("lost in play for 2001 ms of frames that ran (2001 ms wall") != std::string::npos &&
	       why.find("render_units_ready_hold_ms=2000") != std::string::npos);
	EXPECT(LogCount("LOUD: units: the unit readiness was lost in play") == 1);
	w.onFrame(true, 2600);   // too late: no set, no second line
	w.onLoadBegin(2700);
	w.onPoll(false, 9000);
	EXPECT(w.bit() == 0 && sec.mode()->word == 0 && w.counters().fatal == 1 && sec.mode()->readyLossTotal == 1);
	EXPECT(LogCount("lost in play") == 1);
	// the bound at a frame: a ready frame after the bound is fatal, not a recovery
	TestSection s2;
	ModeWriter w2;
	Make(w2, s2, true, 100);
	w2.onFrame(true, 0);
	w2.onFrame(false, 10);
	w2.onFrame(true, 111);
	EXPECT(w2.failed(why) && w2.bit() == 0 && w2.counters().recovered == 0);
	return true;
}

// a load that begins during a loss ends the loss: counted, LOUD, never fatal
bool CaseLoadDuringLoss()
{
	TestSection sec;
	ModeWriter w;
	Make(w, sec);
	w.onFrame(true, 0);
	w.onPoll(false, 100);
	w.onLoadBegin(1500);
	w.onPoll(false, 60000);
	std::string why;
	EXPECT(!w.failed(why) && w.counters().lossesEndedByLoad == 1 && w.phase() == ModeWriter::kLoading);
	EXPECT(LogCount("a new load began 0 ms of step time (1400 ms wall) into a readiness loss") == 1);   // the load is no step
	w.onFrame(true, 61000);
	EXPECT(w.bit() == 1);
	return true;
}

// Ticket 0641: the hold in step time at the product's default (30000 ms; a step adds at most 250 ms). (a) A 4848 ms stall of
// the server's loop with the readiness kept: no loss; (b) a readiness lost across a 10 s stall and 2.4 s of slow iterations,
// back after it: recovered, not fatal (the old 2000 ms of wall time ended it); (c) a lasting loss at 30 frames a second ends
// at 30 s of step time with the reason; (d) a lasting loss while every step is a stall still ends, after 120-121 steps.
bool CaseStallSteps()
{
	std::string why;
	{
		TestSection sec;
		ModeWriter w;
		Make(w, sec, true, 0);   // 0: the default
		EXPECT(w.holdMs() == 30000);
		int64_t t = 0;
		for(; t < 330; t += 33)
			w.onFrame(true, t), w.onPoll(true, t + 16);
		t += 4848;
		w.onPoll(true, t);
		w.onFrame(true, t + 16);
		EXPECT(w.bit() == 1 && w.counters().losses == 0 && w.counters().stalls == 1 && w.counters().maxStepGapMs > 4800);
	}
	{
		TestSection sec;
		ModeWriter w;
		Make(w, sec, true, 0);
		w.onFrame(true, 0);
		w.onPoll(false, 16);   // lost
		w.onPoll(false, 10016);   // a 10 s stall: 250 ms
		const int64_t gaps[] = {540, 677, 229, 779, 146};
		int64_t t = 10016;
		for(int64_t g : gaps)
			t += g, w.onFrame(false, t);
		w.onFrame(true, t + 33);
		EXPECT(!w.failed(why) && w.bit() == 1 && w.counters().recovered == 1 && w.counters().lastLossMs < 2000 &&
		       w.counters().lastLossWallMs > 12000);
		EXPECT(LogCount("LOUD: units: mode: the unit readiness was lost while the unit bit was set") == 1 &&
		       LogCount("LOUD: units: mode: the unit readiness is back after") == 1);
	}
	{
		TestSection sec;
		ModeWriter w;
		Make(w, sec, true, 0);
		w.onFrame(true, 0);
		w.onPoll(false, 16);
		int k = 0;
		for(int64_t t = 49; k < 2000 && !w.failed(why); ++k, t += 33)
			w.onFrame(false, t);
		EXPECT(w.failed(why) && k >= 909 && k <= 911 && why.find("of frames that ran") != std::string::npos &&
		       why.find("render_units_ready_hold_ms=30000") != std::string::npos && why.find("(D118)") != std::string::npos);
	}
	{
		TestSection sec;
		ModeWriter w;
		Make(w, sec, true, 0);
		w.onFrame(true, 0);
		w.onPoll(false, 16);
		int k = 0;
		for(int64_t t = 1016; k < 1000 && !w.failed(why); ++k, t += 1000)
			w.onPoll(false, t);
		EXPECT(w.failed(why) && (k == 120 || k == 121) && w.counters().stalls >= 120);
	}
	return true;
}

// options and bad inputs: a hold <= 0 is the default; a frequency <= 0 does not divide by zero; setOptions keeps the state
bool CaseOptions()
{
	ModeWriter w;
	w.setOptions(0, true);
	EXPECT(w.holdMs() == kUnitsReadyHoldMsDefault);
	w.setOptions(-5, true);
	EXPECT(w.holdMs() == kUnitsReadyHoldMsDefault && kUnitsReadyHoldMsDefault == 30000.0 && kUnitsStepGapMs == 250.0);
	TestSection sec;
	w.configure(true, kPid, 0, &Log, nullptr);
	w.attach(sec.mode());
	w.onFrame(true, 1);
	w.setOptions(300, true);   // a new device: state kept
	EXPECT(w.bit() == 1 && w.phase() == ModeWriter::kLive && w.holdMs() == 300);
	w.onPoll(false, 2);
	w.onPoll(false, 303);      // freq 1: 1 tick = 1 s; every step is a stall counted 250 ms (ticket 0641): 250 ms, within
	std::string why;
	EXPECT(!w.failed(why) && w.counters().stalls == 2);
	w.onPoll(false, 304);      // 500 ms of step time > 300 ms
	EXPECT(w.failed(why));
	// the source leaving exe while live (setOptions): bit 0, not a loss
	TestSection s2;
	ModeWriter w2;
	Make(w2, s2);
	w2.onFrame(true, 1);
	w2.setOptions(2000, false);
	w2.onFrame(true, 2);
	EXPECT(w2.bit() == 0 && w2.counters().losses == 0 && s2.mode()->readyLossTotal == 0 && w2.phase() == ModeWriter::kStarting);
	// configure again resets the state and the counters
	w2.configure(true, kPid, kFreq, &Log, nullptr);
	EXPECT(w2.counters().changes == 0 && w2.phase() == ModeWriter::kStarting && !w2.failed(why));
	return true;
}

// the line: bit= and the loss counters
bool CaseLine()
{
	TestSection sec;
	ModeWriter w;
	Make(w, sec);
	w.onFrame(true, 0);
	w.onPoll(false, 10);
	w.onFrame(true, 20);
	char line[2048] = "units 1s: ready=1";
	w.appendLine(line, sizeof(line));
	const std::string s(line);
	EXPECT(s.find("units 1s: ready=1 | mode bit=1 phase=live src=exe hold_ms=2000 losses=1 recovered=1 ") == 0);
	EXPECT(s.find(" fatal=0 ") != std::string::npos && s.find(" last_loss_ms=10 ") != std::string::npos);
	char tiny[24] = "units 1s: ready=1 xxxxx";   // 23 characters: the buffer is full, nothing appended, no overrun
	w.appendLine(tiny, sizeof(tiny));
	EXPECT(std::string(tiny) == "units 1s: ready=1 xxxxx");
	char cut[40] = "units 1s: ready=1";          // room for part of it: cut, terminated inside the buffer
	w.appendLine(cut, sizeof(cut));
	EXPECT(strlen(cut) == 39 && std::string(cut) == "units 1s: ready=1 | mode bit=1 phase=li");
	return true;
}

// --------------------------------------------------------------------------- issue 4761: the filter cannot drop (D118)
// A writer live with the bit set at t=0 and the compositor's latch of it.
void MakeLive(ModeWriter& w, TestSection& sec, double holdMs = 2000)
{
	Make(w, sec, true, holdMs);
	w.onFrame(true, 0);
	sec.mode()->latchedWord = VPMODE_UNITS;
}

// 0 (not reported) and 1 (can drop) never act; neither does a state that is not 2, nor a reason without the state
bool CaseFilterNoFatal()
{
	const uint32_t states[] = {VPFILTER_NOT_REPORTED,
	                           VPFILTER_CAN_DROP,
	                           VPFILTER_WORD(VPFILTER_CAN_DROP, VPFILTER_R_PATH2_REFUSED),   // a reason without the state
	                           VPFILTER_WORD(0, VPFILTER_R_EXCEPTION),
	                           3u,
	                           0xFFu,
	                           0x203u};
	for(uint32_t st : states)
	{
		TestSection sec;
		ModeWriter w;
		MakeLive(w, sec);
		sec.mode()->filterState = st;
		w.onPoll(true, 10);
		w.onFrame(true, 20);
		w.onPoll(true, 100000);
		std::string why;
		EXPECT(!w.failed(why) && w.bit() == 1 && w.phase() == ModeWriter::kLive && sec.mode()->word == VPMODE_UNITS);
		EXPECT(w.counters().filterFatal == 0 && w.counters().fatal == 0 && w.counters().lastFilterState == st);
		// the writer reads the consumer part and never writes it
		EXPECT(sec.mode()->filterState == st && sec.mode()->latchedWord == VPMODE_UNITS);
	}
	EXPECT(LogCount("units drawn twice") == 0);
	return true;
}

// 2 with the bit held and latched: fatal at once (no wait for the hold bound), the reason named, LOUD once, the bit cleared,
// every later call a no-op; at a poll and at a drawn frame alike
bool CaseFilterFatal()
{
	for(int atFrame = 0; atFrame < 2; ++atFrame)
	{
		g_log.clear();
		TestSection sec;
		ModeWriter w;
		MakeLive(w, sec, 600000);   // the longest hold: the step must not wait for it
		const uint32_t changes = sec.mode()->changes;
		sec.mode()->filterState = VPFILTER_WORD(VPFILTER_CANNOT_DROP, VPFILTER_R_PATH2_REFUSED);
		if(atFrame)
			w.onFrame(true, 0);
		else
			w.onPoll(true, 0);   // the same QPC as the set: 0 ms into the state
		std::string why;
		EXPECT(w.failed(why) && w.phase() == ModeWriter::kFailed && w.counters().filterFatal == 1 && w.counters().fatal == 1);
		EXPECT(why ==
		       "units: the compositor's unit filter cannot drop the EXE's pass-0 unit draws (path 2 refused the EXE's renderer; "
		       "filterState 0x102) while it has latched the unit bit: units drawn twice (D118)");
		EXPECT(LogCount("LOUD: " + why) == 1);
		EXPECT(w.bit() == 0 && sec.mode()->word == 0 && sec.mode()->changes == changes + 1 && sec.mode()->readyLossTotal == 0);
		EXPECT(w.counters().losses == 0);   // not a readiness loss
		// failed: every later call is a no-op (no second step, no set)
		w.onFrame(true, 10);
		w.onPoll(true, 20);
		w.onLoadBegin(30);
		w.onFrame(true, 40);
		EXPECT(w.bit() == 0 && sec.mode()->word == 0 && w.counters().filterFatal == 1 && LogCount("units drawn twice") == 1);
		char line[2048] = "";
		w.appendLine(line, sizeof(line));
		EXPECT(std::string(line).find(" phase=failed ") != std::string::npos);
		EXPECT(std::string(line).find(" fatal=1 ") != std::string::npos);
		EXPECT(std::string(line).find(" filter=0x102 filter_fatal=1") != std::string::npos);
	}
	return true;
}

// every reason is named; an unknown one too
bool CaseFilterReasons()
{
	const char* want[] = {"no reason given",
	                      "path 2 refused the EXE's renderer",
	                      "path 2 not run: the compositor refused the EXE image",
	                      "an exception in a detour turned the filter off",
	                      "no DROP program recognised",
	                      "unit_filter=off",
	                      "the test table was refused",
	                      "the table of marked objects is full",
	                      "no draw reached the filter's hooks",
	                      "an unknown reason"};
	for(uint32_t r = 0; r <= VPFILTER_R_COUNT; ++r)
	{
		TestSection sec;
		ModeWriter w;
		MakeLive(w, sec);
		sec.mode()->filterState = VPFILTER_WORD(VPFILTER_CANNOT_DROP, r);
		w.onPoll(true, 5);
		std::string why;
		EXPECT(w.failed(why));
		char tail[64];
		snprintf(tail, sizeof(tail), "; filterState 0x%X)", VPFILTER_WORD(VPFILTER_CANNOT_DROP, r));
		EXPECT(why.find(std::string("(") + want[r] + tail) != std::string::npos);
		EXPECT(why.find("units drawn twice (D118)") != std::string::npos && why.compare(0, 7, "units: ") == 0);
	}
	return true;
}

// 2 while the bit is not held (starting, loading, lost, source own, off, detached) or not latched: no step
bool CaseFilterNotHeld()
{
	const uint32_t cannot = VPFILTER_WORD(VPFILTER_CANNOT_DROP, VPFILTER_R_EXCEPTION);
	std::string why;
	// held but not latched (the compositor sees the server stale, or has not latched yet): nothing; latched later: the step
	{
		TestSection sec;
		ModeWriter w;
		Make(w, sec);
		w.onFrame(true, 0);
		sec.mode()->filterState = cannot;
		sec.mode()->latchedWord = VPMODE_CITIES;   // another bit latched, not the unit bit
		w.onPoll(true, 10);
		w.onFrame(true, 20);
		EXPECT(!w.failed(why) && w.bit() == 1);
		sec.mode()->latchedWord = VPMODE_UNITS | VPMODE_CITIES;
		w.onPoll(true, 30);
		EXPECT(w.failed(why) && w.counters().filterFatal == 1);
		EXPECT(sec.mode()->word == 0);   // the writer's own bit cleared; VPMODE_CITIES was never in the word
	}
	// not held: starting (no ready frame yet), with a latch a dead server left
	{
		TestSection sec;
		ModeWriter w;
		Make(w, sec);
		sec.mode()->latchedWord = VPMODE_UNITS;
		sec.mode()->filterState = cannot;
		w.onFrame(false, 0);
		w.onPoll(false, 10);
		EXPECT(!w.failed(why) && w.phase() == ModeWriter::kStarting);
		// loading
		w.onLoadBegin(20);
		w.onPoll(false, 30);
		w.onFrame(false, 40);
		EXPECT(!w.failed(why) && w.phase() == ModeWriter::kLoading);
		// the first ready frame sets the bit; the step comes at the next call that sees it held (the compositor's latch is
		// of a word it will read again at its next Present)
		w.onFrame(true, 50);
		EXPECT(!w.failed(why) && w.bit() == 1);
		w.onPoll(true, 51);
		EXPECT(w.failed(why) && w.counters().filterFatal == 1);
	}
	// lost: the bit is 0 during a loss; the step is not taken (the loss's own bound applies)
	{
		TestSection sec;
		ModeWriter w;
		MakeLive(w, sec);
		w.onPoll(false, 10);   // lost
		sec.mode()->filterState = cannot;
		w.onPoll(false, 20);
		w.onFrame(false, 30);
		EXPECT(!w.failed(why) && w.phase() == ModeWriter::kLost && w.counters().filterFatal == 0);
	}
	// source own: the bit never set
	{
		TestSection sec;
		ModeWriter w;
		Make(w, sec, false);
		sec.mode()->latchedWord = VPMODE_UNITS;
		sec.mode()->filterState = cannot;
		w.onFrame(true, 0);
		w.onPoll(true, 10);
		EXPECT(!w.failed(why) && w.bit() == 0);
	}
	// render_units=0 (the writer off): nothing read, nothing acted on
	{
		TestSection sec;
		ModeWriter w;
		w.setOptions(2000, true);
		w.configure(false, kPid, kFreq, &Log, nullptr);
		w.attach(sec.mode());
		sec.mode()->latchedWord = VPMODE_UNITS;
		sec.mode()->filterState = cannot;
		w.onFrame(true, 0);
		w.onPoll(true, 10);
		EXPECT(!w.failed(why) && w.counters().lastFilterState == 0);
	}
	// detached (no block: a server without a section): the writer's state runs, no step
	{
		ModeWriter w;
		w.setOptions(2000, true);
		w.configure(true, kPid, kFreq, &Log, nullptr);
		w.onFrame(true, 0);
		w.onPoll(true, 10);
		EXPECT(!w.failed(why) && w.bit() == 1 && w.counters().lastFilterState == 0);
	}
	return true;
}

// the headless server and an older compositor: the consumer part stays 0 (filterState 0, latchedWord 0); the writer
// behaves as 4753 built it, whatever happens
bool CaseFilterHeadless()
{
	TestSection sec;
	ModeWriter w;
	Make(w, sec, true, 50);
	w.onFrame(true, 0);
	w.onPoll(true, 10);
	w.onFrame(false, 20);   // a loss, back within 50 ms
	w.onFrame(true, 60);
	w.onPoll(true, 100000);
	std::string why;
	EXPECT(!w.failed(why) && w.bit() == 1 && w.counters().recovered == 1 && w.counters().filterFatal == 0);
	EXPECT(sec.mode()->filterState == 0 && sec.mode()->latchedWord == 0);
	// latched by an older compositor that has no filterState (0): no step
	sec.mode()->latchedWord = VPMODE_UNITS;
	w.onPoll(true, 100010);
	w.onFrame(true, 100020);
	EXPECT(!w.failed(why) && w.bit() == 1);
	return true;
}

// ---------------------------------------------------------------------------------------------- the scene's wiring
struct FakeUnits : SceneUnits
{
	bool* ready_;
	explicit FakeUnits(bool* r) : ready_(r) {}
	void setDevice(ID3D11Device*) override {}
	bool feed(const SrvFeed::Batch&, double) override { return false; }
	void useFeedSource(srvrender::FeedSource*) override {}
	void poll() override {}
	void setPlotStates(const uint8_t*, size_t, uint32_t, uint32_t, uint32_t) override {}
	void beginFrame(const UnitsCamera&, const c5r::heightfield::HeightGrid*) override {}
	void drawReflection(ID3D11DeviceContext*, ID3D11RenderTargetView*, ID3D11DepthStencilView*, uint32_t, const float*) override {}
	void drawScene(ID3D11DeviceContext*, ID3D11RenderTargetView*, ID3D11DepthStencilView*, uint32_t, uint32_t, const UnitsCamera&,
	               const c5r::heightfield::HeightGrid*) override
	{
	}
	bool ready() const override { return *ready_; }
	bool failed(std::string&) const override { return false; }
	bool animating() const override { return false; }
	void secondLine(char* out, size_t n) override { snprintf(out, n, "units 1s: ready=%d", *ready_ ? 1 : 0); }
	void totalsLine(char* out, size_t n) override { snprintf(out, n, "units totals: x"); }
};

int g_fatalCalls = 0, g_fatalCode = 0;
std::string g_fatalWhy;
void FakeFatal(int code, const char* why)
{
	g_fatalCalls++;
	g_fatalCode = code;
	g_fatalWhy = why ? why : "";
}

SrvFeed::Batch LoadBatch()
{
	SrvFeed::Batch b;
	SrvFeed::Event e;
	memset(&e, 0, sizeof(e));
	e.seq = 1;
	e.kind = SrvFeed::kLoadBegin;
	e.plot = -1;
	e.turn = -1;
	b.events.push_back(e);
	return b;
}

// the product's defaults through the scene: render_units_positions own, so the bit stays 0 and nothing is written
bool CaseSceneDefaults()
{
	bool ready = true;
	srvrender::Scene s;
	TestSection sec;
	s.setUnits(std::unique_ptr<SceneUnits>(new FakeUnits(&ready)));
	s.unitsMode()->attach(sec.mode());
	EXPECT(s.unitsMode()->on() && s.unitsMode()->holdMs() == 30000 && !s.unitsMode()->sourceExe());   // ticket 0641
	s.unitsFrameDone();
	s.poll();
	EXPECT(s.unitsMode()->bit() == 0 && sec.allZero() && s.unitsMode()->counters().notExeFrames == 1);
	return true;
}

// (d) render_units=0: no unit scene, the writer off, the word stays 0 whatever happens
bool CaseSceneUnitsOff()
{
	srvrender::Scene s;
	TestSection sec;
	std::string why;
	EXPECT(s.configureUnits(false, UnitsOptions(), why));
	EXPECT(!s.unitsMode());   // no unit scene: no writer at all
	s.unitsFrameDone();
	s.unitFeed(LoadBatch());
	s.poll();
	s.unitsFrameDone();
	EXPECT(!s.unitsMode() && !s.unitsFailed(why));
	// a unit scene removed again (setUnits(null), configureUnits(false)) turns the writer off: it writes nothing more
	bool ready = true;
	s.setUnits(std::unique_ptr<SceneUnits>(new FakeUnits(&ready)));
	EXPECT(s.unitsMode() && s.unitsMode()->on());
	s.unitsMode()->attach(sec.mode());
	s.unitsMode()->setOptions(2000, true);
	s.setUnits(nullptr);
	EXPECT(!s.unitsMode()->on());
	s.unitsMode()->onFrame(true, 1);   // even a direct call: off
	s.unitFeed(LoadBatch());
	s.poll();
	EXPECT(sec.allZero() && s.unitsMode()->bit() == 0);
	s.setUnits(std::unique_ptr<SceneUnits>(new FakeUnits(&ready)));
	EXPECT(s.unitsMode()->on());
	EXPECT(s.configureUnits(false, UnitsOptions(), why) && !s.units() && !s.unitsMode()->on());
	s.unitsFrameDone();
	EXPECT(sec.allZero());
	return true;
}

// the scene's steps: frame -> set, kLoadBegin in a batch -> clear, poll -> loss; within the bound no fatal; beyond it
// UnitsFatalStep calls the (fake) HostConfig::fatal once with code 5 and "units: ..."
bool CaseSceneFatal()
{
	srvrender::LogOpen(nullptr, &HostLog);
	bool ready = true;
	srvrender::Scene s;
	TestSection sec;
	s.setUnits(std::unique_ptr<SceneUnits>(new FakeUnits(&ready)));
	s.unitsMode()->attach(sec.mode());
	s.unitsMode()->setOptions(5000, true);
	s.unitsFrameDone();
	EXPECT(sec.mode()->word == VPMODE_UNITS && sec.mode()->writerPid == GetCurrentProcessId());
	s.unitFeed(LoadBatch());
	EXPECT(sec.mode()->word == 0 && s.unitsMode()->phase() == srvunits::ModeWriter::kLoading);
	s.unitsFrameDone();
	EXPECT(sec.mode()->word == VPMODE_UNITS);
	// a loss within the bound: no fatal
	ready = false;
	s.poll();
	EXPECT(sec.mode()->word == 0 && sec.mode()->readyLossTotal == 1);
	g_fatalCalls = 0;
	bool done = false;
	srvrender::UnitsFatalStep(s, &FakeFatal, done);
	EXPECT(!done && g_fatalCalls == 0);
	ready = true;
	s.unitsFrameDone();
	EXPECT(sec.mode()->word == VPMODE_UNITS && s.unitsMode()->counters().recovered == 1);
	// a loss beyond a 50 ms bound
	s.unitsMode()->setOptions(50, true);
	ready = false;
	s.poll();
	Sleep(120);
	s.poll();
	std::string why;
	EXPECT(s.unitsFailed(why) && why.compare(0, 7, "units: ") == 0);
	srvrender::UnitsFatalStep(s, &FakeFatal, done);
	srvrender::UnitsFatalStep(s, &FakeFatal, done);
	EXPECT(done && g_fatalCalls == 1 && g_fatalCode == 5 && g_fatalWhy == why);
	EXPECT(LogCount("units: the 64-bit path cannot go on: units: the unit readiness was lost in play") == 1);
	// no fatal hook (tests, tools): LOUD only
	bool done2 = false;
	srvrender::UnitsFatalStep(s, nullptr, done2);
	EXPECT(done2 && g_fatalCalls == 1 && LogCount("no fatal hook (HostConfig::fatal)") == 1);
	srvrender::LogOpen(nullptr, nullptr);
	return true;
}

// issue 4761 through the scene: the compositor's filterState 2 with the unit bit latched reaches HostConfig::fatal(5, ...)
// by the host's UnitsFatalStep, once, within a poll; filterState 1 does not
bool CaseSceneFilterFatal()
{
	srvrender::LogOpen(nullptr, &HostLog);
	bool ready = true;
	srvrender::Scene s;
	TestSection sec;
	s.setUnits(std::unique_ptr<SceneUnits>(new FakeUnits(&ready)));
	s.unitsMode()->attach(sec.mode());
	s.unitsMode()->setOptions(2000, true);
	s.unitsFrameDone();
	EXPECT(sec.mode()->word == VPMODE_UNITS);
	sec.mode()->latchedWord = VPMODE_UNITS;
	sec.mode()->filterState = VPFILTER_CAN_DROP;
	s.poll();
	s.unitsFrameDone();
	g_fatalCalls = 0;
	bool done = false;
	srvrender::UnitsFatalStep(s, &FakeFatal, done);
	std::string why;
	EXPECT(!done && g_fatalCalls == 0 && !s.unitsFailed(why));
	sec.mode()->filterState = VPFILTER_WORD(VPFILTER_CANNOT_DROP, VPFILTER_R_NO_HOOKS);
	s.poll();
	EXPECT(s.unitsFailed(why) && why.find("(no draw reached the filter's hooks; filterState 0x802)") != std::string::npos);
	srvrender::UnitsFatalStep(s, &FakeFatal, done);
	srvrender::UnitsFatalStep(s, &FakeFatal, done);
	EXPECT(done && g_fatalCalls == 1 && g_fatalCode == 5 && g_fatalWhy == why);
	EXPECT(sec.mode()->word == 0);
	EXPECT(LogCount("units: the 64-bit path cannot go on: units: the compositor's unit filter cannot drop") == 1);
	srvrender::LogOpen(nullptr, nullptr);
	return true;
}

// the scene's "units 1s" line (render.log) carries bit= and the loss counters; the totals a "units mode totals" line
bool CaseSceneLine()
{
	wchar_t dir[MAX_PATH];
	EXPECT(GetTempPathW(MAX_PATH, dir) > 0);
	wchar_t path[MAX_PATH];
	swprintf_s(path, L"%stest_units_mode_%lu.log", dir, GetCurrentProcessId());
	DeleteFileW(path);
	bool ready = true;
	srvrender::Scene s;
	TestSection sec;
	s.setUnits(std::unique_ptr<SceneUnits>(new FakeUnits(&ready)));
	s.unitsMode()->attach(sec.mode());
	s.unitsMode()->setOptions(2000, true);
	s.unitsFrameDone();
	ready = false;
	s.poll();
	srvrender::LogOpen(path, &HostLog);
	char out[8192];
	s.secondLine(out, sizeof(out));
	s.totalsLine(out, sizeof(out));
	srvrender::LogClose();
	srvrender::LogOpen(nullptr, nullptr);
	std::string text;
	FILE* f = nullptr;
	EXPECT(_wfopen_s(&f, path, L"rb") == 0 && f);
	char buf[4096];
	size_t n;
	while((n = fread(buf, 1, sizeof(buf), f)) > 0)
		text.append(buf, n);
	fclose(f);
	DeleteFileW(path);
	EXPECT(text.find("units 1s: ready=0 | mode bit=0 phase=lost src=exe hold_ms=2000 losses=1 recovered=0 ") != std::string::npos);
	EXPECT(text.find("units mode totals: | mode bit=0 phase=lost") != std::string::npos);
	EXPECT(LogCount("units mode totals: | mode bit=0") == 1);   // a note: server.log too
	return true;
}

// a program without srv_units_mode.cpp (no factory): the scene runs its unit scene without a writer, LOUD once, no crash
std::unique_ptr<ModeWriter> TestWriterFactory() { return std::unique_ptr<ModeWriter>(new ModeWriter()); }
bool CaseNoWriterFactory()
{
	srvrender::LogOpen(nullptr, &HostLog);
	RegisterModeWriterFactory(nullptr);
	bool ready = true;
	srvrender::Scene s;
	s.setUnits(std::unique_ptr<SceneUnits>(new FakeUnits(&ready)));
	const bool none = s.unitsMode() == nullptr;
	s.unitsFrameDone();
	s.unitFeed(LoadBatch());
	s.poll();
	std::string why;
	const bool notFailed = !s.unitsFailed(why);
	RegisterModeWriterFactory(&TestWriterFactory);   // a writer again for any later case
	srvrender::LogOpen(nullptr, nullptr);
	EXPECT(none && notFailed);
	EXPECT(LogCount("LOUD: units: mode: this program does not link server/src/units/srv_units_mode.cpp") == 1);
	s.setUnits(std::unique_ptr<SceneUnits>(new FakeUnits(&ready)));   // the next unit scene gets one
	EXPECT(s.unitsMode() && s.unitsMode()->on());
	return true;
}

// the two host keys: defaults, both values, refused values (ParseHostConfig's path for bad values)
bool CaseHostKeys()
{
	auto parse = [](std::map<std::string, std::string> kv, std::string& why) {
		srvrender::HostConfig c;
		return srvrender::ParseHostConfig(
		    [&](const char* k) {
			    auto it = kv.find(k);
			    return it == kv.end() ? std::string() : it->second;
		    },
		    "C:\\game", "C:\\run", c, why);
	};
	std::string why;
	EXPECT(parse({{"render_units_positions", "exe"}, {"render_units_ready_hold_ms", "1500"}}, why));
	EXPECT(srvrender::RenderUnitsPositionsOption() == srvrender::SceneOptions::kUnitsPositionsExe &&
	       srvrender::RenderUnitsReadyHoldMsOption() == 1500);
	EXPECT(parse({}, why));   // the defaults again (last parse wins)
	EXPECT(srvrender::RenderUnitsPositionsOption() == srvrender::SceneOptions::kUnitsPositionsOwn &&
	       srvrender::RenderUnitsReadyHoldMsOption() == 30000);   // ticket 0641: the default (was 2000)
	EXPECT(parse({{"render_units_positions", "own"}}, why) && srvrender::RenderUnitsPositionsOption() == srvrender::SceneOptions::kUnitsPositionsOwn);
	EXPECT(parse({{"render_units_positions", "exe"}}, why) && srvrender::RenderUnitsPositionsOption() == srvrender::SceneOptions::kUnitsPositionsExe);
	EXPECT(!parse({{"render_units_positions", "EXE"}}, why) && why == "render_units_positions=EXE: own or exe");
	EXPECT(!parse({{"render_units_positions", "1"}}, why) && why == "render_units_positions=1: own or exe");
	EXPECT(!parse({{"render_units_ready_hold_ms", "0"}}, why) && why == "render_units_ready_hold_ms=0: 1..600000");
	EXPECT(!parse({{"render_units_ready_hold_ms", "600001"}}, why) && why == "render_units_ready_hold_ms=600001: 1..600000");
	EXPECT(!parse({{"render_units_ready_hold_ms", "2s"}}, why) && why == "render_units_ready_hold_ms=2s: 1..600000");
	EXPECT(parse({{"render_units_ready_hold_ms", "1"}}, why) && srvrender::RenderUnitsReadyHoldMsOption() == 1);
	EXPECT(parse({{"render_units_ready_hold_ms", "600000"}}, why) && srvrender::RenderUnitsReadyHoldMsOption() == 600000);
	EXPECT(parse({}, why) && srvrender::RenderUnitsShadowOption() == 1);   // ticket 4785: default 1
	EXPECT(parse({{"render_units_shadow", "0"}}, why) && srvrender::RenderUnitsShadowOption() == 0);
	EXPECT(parse({{"render_units_shadow", "1"}}, why) && srvrender::RenderUnitsShadowOption() == 1);
	EXPECT(!parse({{"render_units_shadow", "on"}}, why) && why == "render_units_shadow=on: 0 or 1");
	EXPECT(parse({{"render_test_units_offset", "40,-2.5"}}, why));   // ticket 4785, TEST ONLY
	EXPECT(!parse({{"render_test_units_offset", "40"}}, why) && why == "render_test_units_offset=40: dx,dy within +-500 world units");
	EXPECT(!parse({{"render_test_units_offset", "600,0"}}, why));
	EXPECT(!parse({{"render_test_units_offset", "1,2x"}}, why));
	EXPECT(parse({}, why));
	return true;
}

struct Case
{
	const char* name;
	bool (*fn)();
};
const Case kCases[] = {
    {"layout", CaseLayout},
    {"unit_shadow_bit", CaseUnitShadowBit},         // ticket 4785
    {"unit_shadow_doubled", CaseUnitShadowDoubled},  // ticket 4785
    {"bit_rule", CaseBitRule},
    {"producer_part_only", CaseProducerPartOnly},
    {"stale_writer", CaseStaleWriter},
    {"load", CaseLoad},
    {"loss_recovers", CaseLossRecovers},
    {"loss_fatal", CaseLossFatal},
    {"load_during_loss", CaseLoadDuringLoss},
    {"stall_steps", CaseStallSteps},   // ticket 0641
    {"options", CaseOptions},
    {"line", CaseLine},
    {"filter_no_fatal", CaseFilterNoFatal},
    {"filter_fatal", CaseFilterFatal},
    {"filter_reasons", CaseFilterReasons},
    {"filter_not_held", CaseFilterNotHeld},
    {"filter_headless", CaseFilterHeadless},
    {"scene_defaults", CaseSceneDefaults},
    {"scene_units_off", CaseSceneUnitsOff},
    {"scene_fatal", CaseSceneFatal},
    {"scene_filter_fatal", CaseSceneFilterFatal},
    {"scene_line", CaseSceneLine},
    {"host_keys", CaseHostKeys},
    {"no_writer_factory", CaseNoWriterFactory},
};

}  // namespace

int main(int argc, char** argv)
{
	srvrender::LogOpen(nullptr, nullptr);
	int fails = 0, run = 0;
	for(const Case& c : kCases)
	{
		bool want = argc < 2;
		for(int i = 1; i < argc; ++i)
			want |= !strcmp(argv[i], c.name);
		if(!want)
			continue;
		g_log.clear();
		g_fail.clear();
		const bool ok = c.fn();
		run++;
		fails += ok ? 0 : 1;
		printf("CASE %s %s%s%s\n", c.name, ok ? "PASS" : "FAIL", ok ? "" : " ", g_fail.c_str());
		if(!ok)
			for(const std::string& l : g_log)
				printf("    log: %s\n", l.c_str());
		fflush(stdout);
	}
	if(!run)
	{
		printf("test_units_mode: no case matches\n");
		return 2;
	}
	printf("test_units_mode: %d of %d cases passed\n", run - fails, run);
	return fails ? 1 : 0;
}
