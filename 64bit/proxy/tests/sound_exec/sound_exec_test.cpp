// sound_exec_test.cpp - offline test of the shell's sound executor (ticket 4723): proxy/src/proxy_sound_exec.cpp's core
// against a stand-in trigger section (shell/common/vp_trigger.h, written here with the server's own helpers) and a mock
// of the EXE's audio. Nothing of the EXE, no game, no file of the install. Optionally replays a recorded trigger log
// (4721's text form, `R ...` and `N ...` lines) through the section and checks the executor's invariants on it.
//
//     sound_exec_test.exe [--log FILE] [--expect-fail] [-v]
//
// --expect-fail: one deliberately wrong expectation (the program's own failure path: exit 1, one "FAIL line").
// Hand-written. SPDX-License-Identifier: GPL-3.0-or-later
#define VP_SOUND_EXEC_OFFLINE 1
#include "proxy_sound_exec.h"
#include "vp_trigger.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace ProxySoundExec;

namespace
{
int g_checks = 0, g_failed = 0;
bool g_verbose = false;

void Check(bool ok, const char* what, long a = 0, long b = 0)
{
	++g_checks;
	if (!ok)
	{
		++g_failed;
		printf("FAIL line: %s (%ld, %ld)\n", what, a, b);
	}
	else if (g_verbose)
		printf("ok   %s\n", what);
}
#define EQ(a, b, what) Check((long)(a) == (long)(b), what, (long)(a), (long)(b))

// ---- the stand-in section ----------------------------------------------------------------------------------------
unsigned char* g_sec = NULL;
uint32_t g_gen = 0;
uint32_t g_handle = 0;

void NewSection()
{
	if (!g_sec) g_sec = (unsigned char*)VirtualAlloc(NULL, VPTRIG_SECTION_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	memset(g_sec, 0, VPTRIG_SECTION_BYTES);
	g_gen = 0;
	g_handle = 0;
}
void ServerInit()
{
	VPTrigInit(g_sec, ++g_gen, 4242);
}

VPTrigRecord Rec(uint8_t kind, int64_t frame, uint32_t owner, uint32_t fseq)
{
	VPTrigRecord r;
	memset(&r, 0, sizeof(r));
	r.version = VPTRIG_VERSION;
	r.kind = kind;
	r.frameId = frame;
	r.owner = owner;
	r.fightSeq = fseq;
	r.player = 1;
	r.unit = 8193;
	r.figure = -1;
	r.trigger = -1;
	return r;
}
uint32_t Start(int64_t frame, uint32_t owner, uint32_t name, bool loop, int figure = 2, uint64_t inst = 55,
               uint32_t fseq = 7)
{
	VPTrigRecord r = Rec(VPTRIG_SOUND_START, frame, owner, fseq);
	r.flags = loop ? VPTRIG_FLAG_LOOP : 0;
	r.figure = figure;
	r.instance = inst;
	r.handle = ++g_handle;
	r.name = name;
	r.pos[0] = 1.5f;
	r.pos[1] = -2.25f;
	r.pos[2] = 0.1f;
	VPTrigWrite(g_sec, r);
	return r.handle;
}
void FightEnd(int64_t frame, uint32_t owner, uint8_t reason, uint32_t fseq = 7)
{
	VPTrigRecord r = Rec(VPTRIG_OWNER_END, frame, owner, fseq);
	r.reason = reason;
	r.flags = VPTrigPlayedFor(reason) ? VPTRIG_FLAG_PLAYED : 0;
	VPTrigWrite(g_sec, r);
}
void ClipEnd(int64_t frame, uint32_t owner, int figure, uint64_t inst, uint32_t fseq = 7)
{
	VPTrigRecord r = Rec(VPTRIG_OWNER_END, frame, owner, fseq);
	r.reason = VPTRIG_END_CLIP_ENDED;
	r.flags = VPTRIG_FLAG_PLAYED;
	r.figure = figure;
	r.instance = inst;
	VPTrigWrite(g_sec, r);
}
void StopRec(int64_t frame, uint32_t owner, uint32_t handle)
{
	VPTrigRecord r = Rec(VPTRIG_SOUND_STOP, frame, owner, 7);
	r.handle = handle;
	VPTrigWrite(g_sec, r);
}
void Effect(int64_t frame, uint32_t owner)
{
	VPTrigRecord r = Rec(VPTRIG_EFFECT_START, frame, owner, 7);
	r.handle = ++g_handle;
	r.scale = 1.0f;
	VPTrigWrite(g_sec, r);
}

// ---- the mock audio ----------------------------------------------------------------------------------------------
struct MockSound
{
	bool playing;
	int script;
	DWORD startMs;
	DWORD endMs;      // a one-shot ends by itself here (0 = never: a loop)
	DWORD stoppedMs;  // 0 = not stopped
	int playedFrame;
	float pos[3];
};
const int kMock = 4096;
MockSound g_mock[kMock];
int g_nMock = 0;
DWORD g_now = 1000;
int g_frame = 0;
int g_voices = 1000;          // voices left (a refusal when 0)
long g_stopsOfDead = 0;       // a stop called on a sound that is not playing (must stay 0)
const int kLoopScript = 100;  // scripts 100..199 loop (as the script's own flag); 200.. are one-shots of 1 s
DWORD g_oneShotMs = 1000;

int MScript(void*, const char* name)
{
	if (strncmp(name, "AS3D_", 5) != 0) return -1;
	return atoi(name + 5);
}
void* MPlay(void*, int script, const float pos[3])
{
	if (g_voices <= 0 || g_nMock >= kMock) return NULL;
	--g_voices;
	MockSound& m = g_mock[g_nMock++];
	m.playing = true;
	m.script = script;
	m.startMs = g_now;
	m.endMs = script >= 250 ? g_now + 10000 : script >= 200 ? g_now + g_oneShotMs : 0;   // 250..: 10-s one-shots
	m.stoppedMs = 0;
	m.playedFrame = g_frame;
	m.pos[0] = pos[0];
	m.pos[1] = pos[1];
	m.pos[2] = pos[2];
	return &m;
}
void Tick(DWORD ms)
{
	g_now += ms;
	for (int i = 0; i < g_nMock; ++i)
		if (g_mock[i].playing && g_mock[i].endMs && (int32_t)(g_now - g_mock[i].endMs) >= 0)
		{
			g_mock[i].playing = false;
			++g_voices;
		}
}
bool MLive(void*, void* s, int script)
{
	MockSound* m = (MockSound*)s;
	return m && m->playing && m->script == script;
}
void MStop(void*, void* s)
{
	MockSound* m = (MockSound*)s;
	if (!m->playing) ++g_stopsOfDead;
	m->playing = false;
	m->stoppedMs = g_now;
	++g_voices;
}
// 4740: the product's clock is GetTickCount, which moves in 15.6 ms ticks at any moment, also inside one Step. With
// g_tickInStep set, every read after the first of a Step returns g_now + g_tickInStep (the tick that came mid-step).
DWORD g_tickInStep = 0;
int g_readsInStep = 0;
DWORD MClock() { return g_readsInStep++ == 0 ? g_now : g_now + g_tickInStep; }

struct HoldEnd
{
	uint32_t id;
	bool played;
};
HoldEnd g_holdEnds[256];
int g_nHold = 0;
void MOwnerEnd(uint32_t id, bool played)
{
	if (g_nHold < 256)
	{
		g_holdEnds[g_nHold].id = id;
		g_holdEnds[g_nHold].played = played;
	}
	++g_nHold;
}
int g_loud = 0;
void MLog(const char* line, bool loud)
{
	if (loud) ++g_loud;
	if (g_verbose) printf("  log%s: %s\n", loud ? " LOUD" : "", line);
}

Audio MockAudio()
{
	Audio a;
	a.ctx = NULL;
	a.scriptIndex = MScript;
	a.play = MPlay;
	a.live = MLive;
	a.stop = MStop;
	return a;
}

uint32_t g_names[400];
void Fresh()
{
	Test::Reset();
	NewSection();
	memset(g_mock, 0, sizeof(g_mock));
	g_nMock = 0;
	g_now = 1000;
	g_frame = 0;
	g_voices = 1000;
	g_stopsOfDead = 0;
	g_nHold = 0;
	g_loud = 0;
	g_oneShotMs = 1000;
	g_tickInStep = 0;
	g_readsInStep = 0;
	SetSinks(MLog, MOwnerEnd, MClock);
	ServerInit();
	char n[32];
	for (int s = 100; s < 300; ++s)
	{
		_snprintf(n, sizeof(n), "AS3D_%d", s);
		g_names[s] = VPTrigAddName(g_sec, VPTRIG_NAME_SOUND3D, n);
	}
	g_names[1] = VPTrigAddName(g_sec, VPTRIG_NAME_SOUND3D, "NOT_A_SCRIPT");
	g_names[2] = VPTrigAddName(g_sec, VPTRIG_NAME_EFFECT, "AS3D_150");   // an effect name, never a sound
	Attach(g_sec, 77, MockAudio());
}
// One frame: the clock, the mock's natural ends, the executor's step.
void Frame(int64_t g, DWORD ms = 33)
{
	Tick(ms);
	g_frame = (int)g;
	g_readsInStep = 0;
	Step(g);
}
int Playing()
{
	int n = 0;
	for (int i = 0; i < g_nMock; ++i) n += g_mock[i].playing ? 1 : 0;
	return n;
}

// ---- cases -----------------------------------------------------------------------------------------------------------
void CaseFrameRule()
{
	Fresh();
	Start(10, 500, g_names[200], false);
	Start(12, 500, g_names[201], false);
	Frame(9);
	EQ(Test::Played(), 0, "R2: nothing before its frame");
	Frame(10);
	EQ(Test::Played(), 1, "R2: frame 10's record at frame 10");
	EQ(g_mock[0].playedFrame, 10, "R2: played in its own frame");
	Frame(11);
	EQ(Test::Played(), 1, "R2: frame 12's record waits");
	Frame(12);
	EQ(Test::Played(), 2, "R2: frame 12 at 12");
	EQ(g_mock[1].pos[0] * 4, 6, "the record's world position reaches the play");
	EQ(VPTrigLoad64(&VPTRIG_READER(g_sec)->executedTotal), 2, "reader block: executedTotal");
	EQ(VPTrigLoad64(&VPTRIG_READER(g_sec)->readSeq), 2, "reader block: readSeq");
	EQ(VPTRIG_READER(g_sec)->readerPid, 77, "reader block: readerPid");
}

void CaseStale()
{
	Fresh();
	Start(10, 500, g_names[200], false);
	Start(14, 500, g_names[201], false);
	Start(16, 500, g_names[202], false);
	Frame(17);   // 10 is 7 frames late, 14 is 3 late (kept), 16 is 1 late
	EQ(Test::Stale(), 1, "never queued for later: the 7-frame-late start dropped");
	EQ(Test::Played(), 2, "starts within kStaleFrames play");
	EQ(VPTrigLoad64(&VPTRIG_READER(g_sec)->droppedStale), 1, "reader block: droppedStale");
	// a refused play is not retried
	g_voices = 0;
	Start(18, 500, g_names[203], false);
	Frame(18);
	Frame(19);
	Frame(20);
	EQ(Test::Refused(), 1, "a play the EXE refuses is counted once");
	EQ(Test::Played(), 2, "and never retried");
}

void CaseFightEnd()
{
	Fresh();
	Start(10, 500, g_names[100], true);    // a loop
	Start(10, 500, g_names[200], false);   // a one-shot, 1 s
	Start(10, 500, g_names[250], false);   // a one-shot, 10 s
	Start(10, 600, g_names[101], true, 2, 55, 8);   // another fight's loop
	Frame(10);
	EQ(Test::Played(), 4, "four starts");
	EQ(Playing(), 4, "four playing");
	FightEnd(11, 500, VPTRIG_END_NATURAL);
	Frame(11);
	EQ(g_mock[0].playing, 0, "OwnerEnd: the fight's loop stops at once");
	EQ(g_mock[2].playing, 1, "OwnerEnd: a one-shot rings out");
	EQ(g_mock[3].playing, 1, "OwnerEnd: another fight's loop goes on");
	EQ(g_nHold, 1, "R8: one OwnerEnd for the hold");
	EQ(g_holdEnds[0].id, 500, "R8: the fight's event id");
	EQ(g_holdEnds[0].played, 1, "R8: Natural is played");
	// a start of the ended fight is refused
	Start(12, 500, g_names[102], true);
	Frame(12);
	EQ(Test::AfterEnd(), 1, "no sound of an ended fight starts later");
	// 4734: the long one-shot is not cut at 3 s; it ends by itself at 10 s (stock lets every one-shot end so); the 1-s
	// one ended by itself too
	for (int f = 13; f < 13 + 100; ++f) Frame(f);   // ~3.3 s
	EQ(g_mock[2].playing, 1, "4734: the 10-s one-shot still plays 3.3 s after OwnerEnd (no 3-s cut)");
	for (int f = 113; f < 113 + 230; ++f) Frame(f);   // ~7.6 s more: past its 10-s end
	EQ(g_mock[2].playing, 0, "4734: the 10-s one-shot ended");
	EQ(g_mock[2].stoppedMs, 0, "4734: it ended by itself (no stop)");
	EQ(g_mock[1].stoppedMs, 0, "the 1-s one-shot ended by itself (no stop)");
	EQ(Test::StoppedRingOut(), 0, "counted: ring-out 0");
	EQ(Test::NaturalEnd(), 2, "counted: both one-shots natural ends");
	EQ(Test::LiveSounds(), 1, "only the other fight's loop is left in the table");
	EQ(Test::StoppedOwnerEnd(), 1, "counted: owner end 1 (the loop, at once)");
	EQ(g_stopsOfDead, 0, "never a stop on a sound that has ended");
	// a second OwnerEnd of the same fight does not reach the hold twice
	FightEnd(344, 500, VPTRIG_END_LIMIT);
	Frame(344);
	EQ(g_nHold, 1, "R8: the hold hears a fight's end once");
}

// 4734 (lead-0042, 2026-11-21): a one-shot outlives its fight's OwnerEnd and the former 3-s ring-out and ends by itself;
// a loop is still cut at once at the OwnerEnd (the guard against a melee sound that repeats after its fight, 0599); a
// looping script sent as a one-shot (a wrong loop flag) is stopped by the guard kRingOutMs after the OwnerEnd.
void CaseOneShotOutlivesEnd()
{
	Fresh();
	g_oneShotMs = 6000;
	Start(10, 500, g_names[100], true);    // a loop
	Start(10, 500, g_names[200], false);   // a 6-s one-shot
	Start(10, 500, g_names[150], false);   // a looping script (never ends by itself) sent as a one-shot
	Frame(10);
	EQ(Test::Played(), 3, "three played");
	FightEnd(11, 500, VPTRIG_END_NATURAL);
	Frame(11);
	EQ(g_mock[0].playing, 0, "loop still cut at once at the OwnerEnd");
	EQ(g_mock[0].stoppedMs, g_now, "loop stopped in the OwnerEnd's frame");
	int f = 12;
	for (; f < 12 + 120; ++f) Frame(f);   // ~4 s: past the former 3-s ring-out
	EQ(g_mock[1].playing, 1, "the 6-s one-shot outlives OwnerEnd + 3 s");
	for (int k = 0; k < 90; ++k, ++f) Frame(f);   // ~3 s more: past its end
	EQ(g_mock[1].playing, 0, "the 6-s one-shot ended");
	EQ(g_mock[1].stoppedMs, 0, "by itself (EOS), never stopped");
	EQ(g_mock[2].playing, 1, "the mis-flagged loop plays on before the guard");
	const DWORD endAt = g_mock[0].stoppedMs;
	while (g_mock[2].playing && (DWORD)(g_now - endAt) < kRingOutMs + 1000) Frame(f++);
	EQ(g_mock[2].playing, 0, "the guard stops a never-ending one-shot");
	Check(g_mock[2].stoppedMs - endAt >= kRingOutMs && g_mock[2].stoppedMs - endAt < kRingOutMs + 100,
	      "the guard: kRingOutMs (+ one frame) after the OwnerEnd", (long)(g_mock[2].stoppedMs - endAt), (long)kRingOutMs);
	EQ(Test::StoppedRingOut(), 1, "counted: ring-out 1 (the guard)");
	EQ(Test::NaturalEnd(), 1, "counted: natural end 1");
	EQ(Test::LiveSounds(), 0, "the table is empty");
	EQ(g_stopsOfDead, 0, "never a stop on a sound that has ended");
}

void CaseReasonsAndOwnerZero()
{
	Fresh();
	Start(10, 500, g_names[100], true);
	Start(10, 0, g_names[101], true, 2, 55, 9);   // owner unknown: the fight is named by its seq
	Start(10, 0, g_names[102], true, 2, 55, 10);
	Frame(10);
	FightEnd(11, 500, VPTRIG_END_REFUSED);
	FightEnd(11, 0, VPTRIG_END_GONE, 9);
	Frame(11);
	EQ(g_nHold, 1, "owner 0 never reaches the hold");
	EQ(g_holdEnds[0].played, 0, "R8: Refused is not played");
	EQ(Test::HoldOwnerZero(), 1, "owner 0 counted");
	EQ(g_mock[1].playing, 0, "owner 0: the fight named by seq 9 stopped");
	EQ(g_mock[2].playing, 1, "owner 0: seq 10 goes on");
}

void CaseClipEnded()
{
	Fresh();
	Start(10, 500, g_names[100], true, 2, 55);
	Start(10, 500, g_names[101], true, 3, 56);
	Start(10, 500, g_names[200], false, 2, 55);
	Frame(10);
	ClipEnd(11, 500, 2, 55);
	Frame(11);
	EQ(g_mock[0].playing, 0, "ClipEnded: that instance's loop stops");
	EQ(g_mock[1].playing, 1, "ClipEnded: another figure's loop goes on");
	EQ(g_nHold, 0, "ClipEnded never reaches the hold");
	EQ(Test::StoppedClip(), 1, "counted: clip 1");
	Start(12, 500, g_names[103], true, 2, 57);   // the figure's next clip: the fight is not ended
	Frame(12);
	EQ(Test::Played(), 4, "after a ClipEnded the fight's next clip still sounds");
}

void CaseStopRecordAndEffects()
{
	Fresh();
	const uint32_t h = Start(10, 500, g_names[100], true);
	Effect(10, 500);
	Frame(10);
	EQ(Test::EffectsIgnored(), 1, "effects are the effect routes' (counted, ignored)");
	StopRec(11, 500, h);
	Frame(11);
	EQ(g_mock[0].playing, 0, "SoundStop stops its handle");
	EQ(Test::StoppedRecord(), 1, "counted: stop record 1");
	StopRec(12, 500, h);
	Frame(12);
	EQ(g_stopsOfDead, 0, "a second stop of a gone handle does nothing");
}

void CaseUnknownNames()
{
	Fresh();
	Start(10, 500, g_names[1], true);    // a name the game has no script for
	Start(10, 500, g_names[2], true);    // an effect name in a sound record
	Start(10, 500, 3999, true);          // an index never published
	Frame(10);
	EQ(Test::UnknownScript(), 3, "unknown scripts are not played");
	EQ(Test::Played(), 0, "nothing played");
	Check(g_loud >= 2, "unknown script: LOUD", g_loud, 2);
}

void CaseWatchdog()
{
	Fresh();
	Start(10, 500, g_names[100], true);
	Start(10, 600, g_names[101], true, 2, 55, 8);
	Frame(10);
	// fight 600 keeps sending (clip ends of a figure), fight 500 falls silent
	for (int f = 11; f < 11 + 700; ++f)   // 700 frames x 33 ms = 23.1 s
	{
		if (f % 30 == 0) ClipEnd(f, 600, 5, 1000 + f, 8);
		Frame(f);
	}
	EQ(g_mock[0].playing, 0, "watchdog: the silent fight's loop is stopped after 22 s");
	EQ(g_mock[1].playing, 1, "watchdog: a fight that keeps sending is left alone");
	EQ(Test::StoppedWatchdog(), 1, "counted: watchdog 1");
	Start(800, 500, g_names[102], true);
	Frame(800);
	EQ(Test::AfterEnd(), 1, "a watchdog-ended fight starts nothing more");
	// 4740: the executor's watchdog governs its own sounds only. The server's OwnerEnd of that fight, when it comes, still
	// reaches the hold (before 4740 the ended entry swallowed it: the hold waited for its own watchdog and route S
	// disarmed), once
	EQ(g_nHold, 0, "4740: nothing passed to the hold by the executor's watchdog itself");
	FightEnd(801, 500, VPTRIG_END_LIMIT);
	Frame(801);
	EQ(g_nHold, 1, "4740: the server's OwnerEnd after the executor's watchdog reaches the hold");
	EQ(g_holdEnds[0].id, 500, "4740: with the fight's event id");
	EQ(g_holdEnds[0].played, 1, "4740: and its played flag (Limit is played)");
	FightEnd(802, 500, VPTRIG_END_LIMIT);
	Frame(802);
	EQ(g_nHold, 1, "4740: R8 still once per fight");
}

// 4740 (W38's S4, 2026-11-21: fight event 122793): GetTickCount moved by one tick inside a Step, between the Step's own
// clock read and DoStop's. Before 4740 DoStop stamped the fight with that later read, Housekeep's unsigned (now - lastMs)
// wrapped to ~4.29e9 >= 22000 and the watchdog ended a fight 5.5 s old ("sent nothing for 22000 ms", 8 sounds stopped);
// the server's OwnerEnd 0.3 s later found the entry ended and never reached the hold, whose own watchdog then disarmed
// route S.
void CaseTickInsideStep()
{
	Fresh();
	Start(10, 500, g_names[100], true);                 // the fight's loop
	const uint32_t h = Start(10, 500, g_names[250], false);   // a 10-s one-shot
	Frame(10);
	EQ(Test::Played(), 2, "4740: two played");
	for (int f = 11; f < 11 + 150; ++f) Frame(f);       // ~5 s into the fight
	g_tickInStep = 16;                                    // the tick comes mid-step
	StopRec(161, 500, h);
	Frame(161);
	g_tickInStep = 0;
	EQ(Test::StoppedWatchdog(), 0, "4740: no executor watchdog 5 s into a fight that keeps sending");
	EQ(g_mock[0].playing, 1, "4740: the fight's loop plays on after a mid-step tick");
	EQ(g_loud, 0, "4740: no LOUD 'sent nothing for 22000 ms' line");
	EQ(Test::StoppedRecord(), 1, "4740: the stop record stopped its one-shot");
	for (int f = 162; f < 162 + 10; ++f) Frame(f);
	FightEnd(172, 500, VPTRIG_END_NATURAL);
	Frame(172);
	EQ(g_nHold, 1, "4740: the fight's OwnerEnd reaches the hold");
	EQ(g_holdEnds[0].id, 500, "4740: with the fight's event id");
	EQ(g_mock[0].playing, 0, "4740: the loop stopped by the OwnerEnd");
	EQ(Test::StoppedOwnerEnd(), 1, "4740: counted: owner end 1");
	// the same with the tick in a step that carries only a ClipEnded and in one with a start (both stamp the step's now)
	Fresh();
	Start(10, 600, g_names[101], true, 2, 55, 8);
	Frame(10);
	g_tickInStep = 16;
	ClipEnd(11, 600, 3, 99, 8);
	Frame(11);
	Start(12, 600, g_names[200], false, 2, 56, 8);
	Frame(12);
	g_tickInStep = 0;
	EQ(Test::StoppedWatchdog(), 0, "4740: no watchdog after a mid-step tick with a ClipEnded or a start");
	EQ(g_loud, 0, "4740: no LOUD line");
}

// 4732 (W35's probe, 4731): R7's unit-owned form (owner 0, fightSeq 0: the server's victory tail) is no fight. Before
// the fix its shared (0, 0) entry was ended by the fight watchdog 22 s after the last tail record (a LOUD "fight 0
// (seq 0) sent nothing" line) and every tail sound in the next 22 s was dropped as "after end".
void CaseVictoryTail()
{
	Fresh();
	Start(10, 0, g_names[200], false, 2, 55, 0);   // a 1-s tail one-shot
	Frame(10);
	EQ(Test::Played(), 1, "tail: the one-shot plays");
	EQ(Test::Fights(), 0, "tail: no fight entry");
	int f = 11;
	for (; f < 11 + 700; ++f) Frame(f);   // 23.1 s without a record
	EQ(g_loud, 0, "tail: no watchdog line after 23 s of silence");
	EQ(Test::StoppedWatchdog(), 0, "tail: nothing stopped by the watchdog");
	EQ(Test::LiveSounds(), 0, "tail: the one-shot freed at its natural end");
	EQ(Test::NaturalEnd(), 1, "tail: counted a natural end");
	for (int k = 0; k < 150; ++k, ++f) Frame(f);   // 5 s more: where the former watchdog end dropped the next tail
	Start(f, 0, g_names[201], false, 3, 56, 0);
	Frame(f++);
	EQ(Test::Played(), 2, "tail: a one-shot 5 s after the former watchdog end plays");
	EQ(Test::AfterEnd(), 0, "tail: nothing dropped as after end");
	// tails of two units at once, and a ClipEnded of one clip instance (rings out only that one)
	g_oneShotMs = 10000;
	Start(f, 0, g_names[202], false, 2, 70, 0);
	Start(f, 0, g_names[203], false, 4, 71, 0);
	Frame(f++);
	ClipEnd(f, 0, 2, 70, 0);
	Frame(f++);
	for (int k = 0; k < 100; ++k, ++f) Frame(f);   // 3.3 s: past the former 3-s ring-out
	EQ(g_mock[2].playing, 1, "tail: the clip instance's one-shot outlives its ClipEnded (4734: no 3-s cut)");
	EQ(g_mock[3].playing, 1, "tail: the other unit's one-shot goes on");
	for (int k = 0; k < 220; ++k, ++f) Frame(f);   // 7.3 s more: both 10-s one-shots past their end
	EQ(g_mock[2].playing, 0, "tail: the clip instance's one-shot ended by itself");
	EQ(g_mock[2].stoppedMs, 0, "tail: never stopped (4734)");
	// the forms the server never sends: a loop and a fight OwnerEnd, both bad records; neither leaves an ended entry
	Start(f, 0, g_names[100], true, 2, 72, 0);
	FightEnd(f, 0, VPTRIG_END_NATURAL, 0);
	Frame(f++);
	EQ(Test::BadRecords(), 2, "tail: a loop and a fight end in the unit-owned form are bad records");
	EQ(g_nHold, 0, "tail: nothing reaches the hold");
	Start(f, 0, g_names[204], false, 5, 73, 0);
	Frame(f++);
	EQ(Test::Played(), 5, "tail: still played after the bad records");
	EQ(Test::TailStarts(), 6, "tail: starts counted");
	EQ(Test::TailPlayed(), 5, "tail: played counted");
	EQ(Test::Fights(), 0, "tail: still no fight entry");
	EQ(g_loud, 0, "tail: no LOUD line");
}

void CaseNewGenerationAndStop()
{
	Fresh();
	Start(10, 500, g_names[100], true);
	Start(10, 600, g_names[101], true, 2, 55, 8);
	Frame(10);
	ServerInit();   // a server restart: generation 2
	for (int s = 100; s < 300; ++s)
	{
		char n[32];
		_snprintf(n, sizeof(n), "AS3D_%d", s);
		g_names[s] = VPTrigAddName(g_sec, VPTRIG_NAME_SOUND3D, n);
	}
	Start(11, 700, g_names[103], true);   // frame ids are the EXE's (vp_frame.h): they go on across a server restart
	Frame(11);
	EQ(Test::NewGenerations(), 1, "R6: a new generation seen");
	EQ(g_mock[0].playing, 0, "R6: old loops stopped");
	EQ(g_mock[1].playing, 0, "R6: every old loop stopped");
	EQ(g_nHold, 2, "R6: every old fight ended for the hold");
	Check(!g_holdEnds[0].played && !g_holdEnds[1].played, "R6: Reset is not played");
	EQ(Test::Played(), 3, "the new generation's record plays");
	VPTrigStop(g_sec);   // an orderly stop (magic 0)
	Frame(12);
	EQ(Test::NewGenerations(), 2, "R6: a stopped server reads as a new generation");
	EQ(Playing(), 0, "everything stopped at the server's stop");
	Detach();
	EQ(VPTRIG_READER(g_sec)->readerPid, 0, "Detach clears readerPid");
	Step(13);   // detached: nothing
}

void CaseLost()
{
	Fresh();
	for (uint32_t i = 0; i < VPTRIG_CAPACITY + 100; ++i)
	{
		VPTrigRecord r = Rec(VPTRIG_EFFECT_START, 10, 500, 7);
		r.handle = ++g_handle;
		r.scale = 1;
		// no reader attached yet as the server sees it: write directly (R3 drops only with a reader)
		VPTRIG_READER(g_sec)->readerPid = 0;
		VPTrigWrite(g_sec, r);
	}
	Frame(10);
	Frame(11);
	Frame(12);
	Frame(13);
	Check(Test::Lost() >= 1, "R3: overwritten records counted as lost", Test::Lost(), 1);
	EQ(Test::EffectsIgnored(), (long)VPTRIG_CAPACITY, "the rest of the ring read once, 1024 per frame");
}

void CaseFullAndBounded()
{
	Fresh();
	for (int i = 0; i < kMaxSounds + 10; ++i) Start(10, 500, g_names[100 + (i % 100)], true, i, (uint64_t)i);
	Frame(10);
	EQ(Test::Played(), kMaxSounds, "256 live sounds at most");
	EQ(Test::Full(), 10, "the rest counted as full, not started");
	EQ(Test::LiveSounds(), kMaxSounds, "table full");
	FightEnd(11, 500, VPTRIG_END_NATURAL);
	Frame(11);
	EQ(Test::LiveSounds(), 0, "the fight's end frees every slot");
	EQ(Playing(), 0, "and stops every loop");
	// 3000 records in one frame: at most kMaxRecordsPerStep per step
	Fresh();
	for (int i = 0; i < 3000; ++i) Effect(10, 500);
	Frame(10);
	EQ(Test::EffectsIgnored(), kMaxRecordsPerStep, "bounded: 1024 records per frame");
	Frame(11);
	Frame(12);
	EQ(Test::EffectsIgnored(), 3000, "the rest in the next frames");
}

void CaseNaturalEndFrees()
{
	Fresh();
	for (int i = 0; i < 40; ++i) Start(10, 500, g_names[200], false, i, (uint64_t)i);
	Frame(10);
	for (int f = 11; f < 11 + 60; ++f) Frame(f);   // 2 s: every 1-s one-shot ended by itself
	EQ(Test::LiveSounds(), 0, "one-shots that ended by themselves free their slots (sweep)");
	EQ(Test::NaturalEnd(), 40, "counted: natural end 40");
	EQ(g_stopsOfDead, 0, "no stop for them");
}

void CaseAttachRefusals()
{
	Test::Reset();
	SetSinks(MLog, MOwnerEnd, MClock);
	Audio a = MockAudio();
	EQ(Attach(NULL, 1, a), 0, "Attach refuses a missing section");
	a.stop = NULL;
	NewSection();
	EQ(Attach(g_sec, 1, a), 0, "Attach refuses an incomplete audio");
	EQ(Attached(), 0, "not attached");
	Step(5);   // not attached: no fault
}

// ---- a recorded trigger log (4721's text form) -------------------------------------------------------------------
bool Key(const char* line, const char* key, char* out, size_t cap)
{
	char k[32];
	_snprintf(k, sizeof(k), " %s=", key);
	const char* p = strstr(line, k);
	if (!p) return false;
	p += strlen(k);
	size_t n = 0;
	while (p[n] && p[n] != ' ' && p[n] != '\r' && p[n] != '\n' && n + 1 < cap) out[n] = p[n], ++n;
	out[n] = 0;
	return true;
}
int KindOf(const char* s)
{
	const char* k[] = {"None", "SoundStart", "SoundStop", "EffectStart", "EffectMove", "EffectStop", "OwnerEnd"};
	for (int i = 0; i < 7; ++i)
		if (!strcmp(s, k[i])) return i;
	return -1;
}
int ReasonOf(const char* s)
{
	const char* k[] = {"Natural", "Limit", "Refused", "Gone", "Reset", "ClipEnded"};
	for (int i = 0; i < 6; ++i)
		if (!strcmp(s, k[i])) return i;
	return 0;
}

// Replays the log: names into the table in order, records into the ring, one frame step per frame id. Checks: no sound
// of a fight starts after the fight's OwnerEnd was executed; 3 s (+ a frame) after every fight's end none of its
// sounds is playing; at the end only sounds of fights without an end may play.
void ReplayLog(const char* path)
{
	FILE* f = fopen(path, "rb");
	Check(f != NULL, "--log: the file opens");
	if (!f) return;
	Fresh();
	VPTrigInit(g_sec, ++g_gen, 4242);   // the log's own names: a fresh generation
	Frame(0);
	static char line[2048];
	char v[256];
	int names = 0, recs = 0, starts = 0, ends = 0;
	int64_t lastFrame = 0;
	while (fgets(line, sizeof(line), f))
	{
		if (line[0] == 'N' && line[1] == ' ')
		{
			unsigned idx = 0;
			char kind[32], text[200];
			if (sscanf(line, "N %u %31s %199[^\r\n]", &idx, kind, text) == 3)
			{
				const uint32_t k = !strcmp(kind, "sound3d") ? 1 : !strcmp(kind, "effect") ? 2 : 3;
				const uint32_t got = VPTrigAddName(g_sec, k, text);
				Check(got == idx, "--log: name index as in the log", (long)got, (long)idx);
				++names;
			}
			continue;
		}
		if (line[0] != 'R' || line[1] != ' ') continue;
		VPTrigRecord r;
		memset(&r, 0, sizeof(r));
		r.version = VPTRIG_VERSION;
		if (Key(line, "F", v, sizeof(v))) r.frameId = _atoi64(v);
		if (Key(line, "kind", v, sizeof(v))) r.kind = (uint8_t)KindOf(v);
		if (Key(line, "reason", v, sizeof(v)) && strcmp(v, "-")) r.reason = (uint8_t)ReasonOf(v);
		if (Key(line, "flags", v, sizeof(v)))
			r.flags = (strstr(v, "loop") ? VPTRIG_FLAG_LOOP : 0) | (strstr(v, "follows") ? VPTRIG_FLAG_FOLLOWS_OWNER : 0) |
			          (strstr(v, "played") ? VPTRIG_FLAG_PLAYED : 0);
		if (Key(line, "owner", v, sizeof(v))) r.owner = (uint32_t)strtoul(v, NULL, 10);
		if (Key(line, "fseq", v, sizeof(v))) r.fightSeq = (uint32_t)strtoul(v, NULL, 10);
		if (Key(line, "player", v, sizeof(v))) r.player = atoi(v);
		if (Key(line, "unit", v, sizeof(v))) r.unit = atoi(v);
		if (Key(line, "figure", v, sizeof(v))) r.figure = atoi(v);
		if (Key(line, "handle", v, sizeof(v))) r.handle = (uint32_t)strtoul(v, NULL, 10);
		if (Key(line, "inst", v, sizeof(v))) r.instance = _strtoui64(v, NULL, 10);
		if (Key(line, "name", v, sizeof(v))) r.name = (uint32_t)strtoul(v, NULL, 10);
		if (Key(line, "pos", v, sizeof(v))) sscanf(v, "%f,%f,%f", &r.pos[0], &r.pos[1], &r.pos[2]);
		// step the frames up to this record's frame first (the stream is written ahead of the picture)
		while (lastFrame < r.frameId - 1) Frame(++lastFrame);
		VPTrigWrite(g_sec, r);
		++recs;
		starts += r.kind == VPTRIG_SOUND_START;
		ends += r.kind == VPTRIG_OWNER_END && r.figure == -1 && r.instance == 0;
	}
	fclose(f);
	for (int k = 0; k < 120; ++k) Frame(++lastFrame);   // 4 s after the last record
	printf("--log %s: %d names, %d records (%d sound starts, %d fight ends); played %ld, unknown script %ld, stale %ld, "
	       "after end %ld, hold OwnerEnd %ld\n", path, names, recs, starts, ends, Test::Played(), Test::UnknownScript(),
	       Test::Stale(), Test::AfterEnd(), Test::HoldEnds());
	Check(recs > 0, "--log: records read");
	// every fight that ended: none of its sounds plays 3 s after (the log's last fight end is 4 s before the end here)
	EQ(g_stopsOfDead, 0, "--log: never a stop on an ended sound");
	if (ends > 0) Check(Test::HoldEnds() + Test::HoldOwnerZero() >= 1, "--log: fight ends reached the hold or were owner 0");
}
}  // namespace

int main(int argc, char** argv)
{
	bool expectFail = false;
	const char* log = NULL;
	for (int i = 1; i < argc; ++i)
	{
		if (!strcmp(argv[i], "--expect-fail")) expectFail = true;
		else if (!strcmp(argv[i], "-v")) g_verbose = true;
		else if (!strcmp(argv[i], "--log") && i + 1 < argc) log = argv[++i];
	}
	CaseFrameRule();
	CaseStale();
	CaseFightEnd();
	CaseOneShotOutlivesEnd();
	CaseReasonsAndOwnerZero();
	CaseClipEnded();
	CaseStopRecordAndEffects();
	CaseUnknownNames();
	CaseWatchdog();
	CaseTickInsideStep();
	CaseVictoryTail();
	CaseNewGenerationAndStop();
	CaseLost();
	CaseFullAndBounded();
	CaseNaturalEndFrees();
	CaseAttachRefusals();
	if (log) ReplayLog(log);
	if (expectFail) EQ(1, 2, "--expect-fail: a deliberately wrong expectation");
	char c[600];
	CountersLine(c, sizeof(c));
	if (g_verbose) printf("%s\n", c);
	printf("sound_exec_test: %d/%d checks passed\n", g_checks - g_failed, g_checks);
	return g_failed ? 1 : 0;
}
