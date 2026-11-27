// route_s_test.cpp - offline tests of proxy/src/proxy_route_s.cpp (ticket 4783, MC ruling #8543 point 4), linked with
// the three REAL route S cores it governs: proxy_combat_hold.cpp (VP_COMBAT_HOLD_OFFLINE), proxy_sound_exec.cpp
// (VP_SOUND_EXEC_OFFLINE) and proxy_effect_player.cpp (VP_EFFECT_PLAYER_OFFLINE). Built and run by test_route_s.py with the
// proxy's toolchain (clang-cl -m32, VC9 headers) and once under MSVC /fsanitize=address (x86).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stand-ins (nothing of the EXE is read): a fake queue B for the hold (combat_hold_test.cpp's, combat-choreography.md
// 3.2-3.5), a trigger section of VPTRIG_SECTION_BYTES on which the test plays the server's part of vp_trigger.h R9 (the
// question, the decision, OwnerEnd records), a mock audio for the sound executor, a mock poster for the effect player, and
// one DWORD as the app's DisableSystems mask. The frame function calls the parts in the bridge's chain order (the hold's
// drop, the sound executor, the effect player, then route S: proxy_ui_bridge_rt.cpp Init) and the glue's section steps.
//
// Cases: armed (one line, the answer, the Combat bit cleared, a fight held and dropped by the server's OwnerEnd, the bit
// back at the unload as found); for each part a refusal (hold: the hold's own unknown-build refusal; sound executor and
// effect player: their bind refusal): NOT ARMED, no hold taken, the Combat bit untouched, one report; a reader that never
// attaches (5 s); two parts refusing (still one report); the executor that stops answering mid-session (one watchdog at
// 22 s, the fight's id in the line, released, disarmed as one, the next fight not held); the server's NOT TAKEN; a part
// fault after the arming (a held fight released at once); Combat already off in config.ini; a mask value not recognised
// (no write, no arming); a second generation (answered, not armed again); an unsupported build (no report); the unload.
// Exit code 0 when every check passes, 1 otherwise; "--expect-fail" adds one check that must fail.
#include "proxy_route_s.h"
#include "proxy_combat_hold.h"
#include "proxy_sound_exec.h"
#include "proxy_effect_player.h"
#include "vp_trigger.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

namespace RS = ProxyRouteS;
namespace CH = ProxyCombatHold;
namespace SE = ProxySoundExec;
namespace EP = ProxyEffectPlayer;

namespace
{
int g_pass = 0, g_fail = 0;
bool g_verbose = false;
void Check(bool ok, const char* name, int line)
{
	if (ok)
		++g_pass;
	else
	{
		++g_fail;
		printf("FAIL line %d: %s\n", line, name);
	}
}
#define CHECK(x) Check((x), #x, __LINE__)

// ---- sinks -----------------------------------------------------------------------------------------------------------
std::string g_log;
std::vector<std::string> g_lines;
int g_loud = 0;
int g_reports = 0;          // route S's report sink (the one D118 report)
int g_partReportsDirect = 0;  // a part's report that did NOT go through route S (must stay 0)
std::string g_reportCheck, g_reportLine;
void LogSink(const char* line, bool loud)
{
	g_log += line;
	g_log += "\n";
	g_lines.push_back(line);
	if (loud) ++g_loud;
	if (g_verbose) printf("    log%s: %s\n", loud ? " LOUD" : "", line);
}
void ReportSink(const char* check, const char* line)
{
	++g_reports;
	g_reportCheck = check;
	g_reportLine = line;
}
bool LogHas(const char* s) { return g_log.find(s) != std::string::npos; }
int LinesWith(const char* s)
{
	int n = 0;
	for (size_t i = 0; i < g_lines.size(); ++i) n += g_lines[i].find(s) != std::string::npos ? 1 : 0;
	return n;
}
void HoldReport(const char* check, const char* line) { RS::PartReport(RS::kHold, check, line); }

volatile LONG g_now = 1000;
DWORD Clock() { return (DWORD)g_now; }
// 4740: the sound executor's clock with a GetTickCount tick that comes inside its Step: every read after the first of a
// Step returns g_now + g_seTickInStep (0 = the shared clock).
DWORD g_seTickInStep = 0;
int g_seReads = 0;
DWORD SeClock() { return g_seReads++ == 0 ? (DWORD)g_now : (DWORD)g_now + g_seTickInStep; }

// ---- the fake queue B (combat_hold_test.cpp's, reduced) -------------------------------------------------------------
const DWORD kHalfBytes = 1u << 20;
BYTE* g_page = NULL;
BYTE* g_halves[2] = {NULL, NULL};
LONG* IndexWord() { return reinterpret_cast<LONG*>(g_page); }
BYTE* Buffers() { return g_page + 0x100; }
BYTE* Q() { return Buffers() + CH::kQbStride * 1; }
CRITICAL_SECTION* QCs() { return reinterpret_cast<CRITICAL_SECTION*>(Q() + CH::kQbOffCs); }
LONG& Flip() { return *reinterpret_cast<LONG*>(Q() + CH::kQbOffFlip); }
BYTE* Half(bool write) { return Q() + (((write ? Flip() : Flip() - 1) & 1) * 0x10); }
DWORD& Begin(bool write) { return *reinterpret_cast<DWORD*>(Half(write) + CH::kHalfOffBegin); }
DWORD& End(bool write) { return *reinterpret_cast<DWORD*>(Half(write) + CH::kHalfOffEnd); }

void SetUpQueue()
{
	if (!g_halves[0])
	{
		g_halves[0] = static_cast<BYTE*>(VirtualAlloc(NULL, kHalfBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
		g_halves[1] = static_cast<BYTE*>(VirtualAlloc(NULL, kHalfBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	}
	g_page = static_cast<BYTE*>(VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	*IndexWord() = 1;
	InitializeCriticalSection(QCs());
	Flip() = 1;
	BYTE* w = Q() + 0x10;
	BYTE* d = Q();
	*reinterpret_cast<DWORD*>(w + 4) = *reinterpret_cast<DWORD*>(w + 8) = (DWORD)(ULONG_PTR)g_halves[1];
	*reinterpret_cast<DWORD*>(d + 4) = *reinterpret_cast<DWORD*>(d + 8) = (DWORD)(ULONG_PTR)g_halves[0];
}

CH::Target HoldTarget(bool good)
{
	CH::Target t;
	memset(&t, 0, sizeof(t));
	t.build = good ? kVpExeBuild4390913 : kVpExeBuildUnknown;
	t.stamp = good ? 0 : 0x12345678u;
	t.qbIndex = (ULONG_PTR)IndexWord();
	t.qbBuf = (ULONG_PTR)Buffers();
	return t;
}

// A visualised RunCombatSim record posted as the EXE's handler does, then the carried call's take.
void Fight(uint32_t id, DWORD unit)
{
	const DWORD size = CH::kRecSizeRun;
	EnterCriticalSection(QCs());
	BYTE* r = reinterpret_cast<BYTE*>((ULONG_PTR)End(true));
	memset(r, 0, size);
	*reinterpret_cast<DWORD*>(r) = size;
	*reinterpret_cast<DWORD*>(r + 4) = CH::kRecTypeRun;
	const DWORD keys[10] = {100 + unit, 200 + unit, 0, unit, 1, unit + 1, 0xFFFFFFFFu, 0, 0xFFFFFFFFu, 0};
	memcpy(r + CH::kRecOffKeys, keys, sizeof(keys));
	*reinterpret_cast<DWORD*>(r + 0x40) = 0x11;
	*reinterpret_cast<DWORD*>(r + CH::kRecOffId) = id;
	r[CH::kRecOffVisualise] = 1;
	End(true) = End(true) + size;
	LeaveCriticalSection(QCs());
	CH::AfterCombatCall(id);
}

// ---- the trigger section and the server's part of R9 ------------------------------------------------------------------
std::vector<uint8_t> g_sec;
void* Sec() { return &g_sec[0]; }
int64_t g_frame = 100;     // the frame shown
uint32_t g_gen = 0;

void ServerInit(bool ask)
{
	g_gen = VPTRIG_SERVER(Sec())->generation + 1;
	VPTrigInit(Sec(), g_gen, 4242);
	if (ask) VPTrigAskRouteS(Sec());
}
// The server writes an OwnerEnd only when its generation's answer is ARMED (R9); returns whether it wrote.
bool ServerOwnerEnd(uint32_t owner)
{
	if (VPTrigRouteSAnswer(Sec(), NULL, NULL) != VPTRIG_ROUTES_ARMED) return false;
	VPTrigRecord r;
	memset(&r, 0, sizeof(r));
	r.frameId = g_frame + 1;
	r.version = (uint16_t)VPTRIG_VERSION;
	r.kind = VPTRIG_OWNER_END;
	r.reason = VPTRIG_END_NATURAL;
	r.flags = VPTRIG_FLAG_PLAYED;
	r.owner = owner;
	r.fightSeq = owner;
	r.player = 0;
	r.unit = 1;
	r.figure = -1;
	r.instance = 0;
	return VPTrigWrite(Sec(), r);
}

// The server starts a fight: its first record (a sound start; the script name is not known to the mock: counted
// unknown script, the start is taken all the same).
bool ServerStart(uint32_t owner)
{
	if (VPTrigRouteSAnswer(Sec(), NULL, NULL) != VPTRIG_ROUTES_ARMED) return false;
	VPTrigRecord r;
	memset(&r, 0, sizeof(r));
	r.frameId = g_frame + 1;
	r.version = (uint16_t)VPTRIG_VERSION;
	r.kind = VPTRIG_SOUND_START;
	r.owner = owner;
	r.fightSeq = owner;
	r.unit = 1;
	r.figure = 0;
	r.handle = owner;
	r.instance = 1;
	return VPTrigWrite(Sec(), r);
}

// The server stops one of a fight's sounds (a SoundStop record, as the emitter sends for a clip's loop).
bool ServerSoundStop(uint32_t owner, uint32_t handle)
{
	if (VPTrigRouteSAnswer(Sec(), NULL, NULL) != VPTRIG_ROUTES_ARMED) return false;
	VPTrigRecord r;
	memset(&r, 0, sizeof(r));
	r.frameId = g_frame + 1;
	r.version = (uint16_t)VPTRIG_VERSION;
	r.kind = VPTRIG_SOUND_STOP;
	r.owner = owner;
	r.fightSeq = owner;
	r.unit = 1;
	r.figure = 0;
	r.handle = handle;
	r.instance = 1;
	return VPTrigWrite(Sec(), r);
}

// ---- the parts' stand-ins -----------------------------------------------------------------------------------------------
int g_soundA = 0;
int MockScript(void*, const char*) { return 1; }
void* MockPlay(void*, int, const float*) { return &g_soundA; }
bool MockLive(void*, void*, int) { return true; }
void MockStop(void*, void*) {}
bool MockPost(void*, EP::PostKind, const void*) { return true; }

int g_refuse = -1;            // the part whose arm refuses
int g_refuse2 = -1;           // a second one
bool g_soundNoAttach = false;   // the sound executor's section cannot be mapped: it never attaches
bool g_soundStalled = false;    // attached, but its frame step stops running (the executor "stops answering")
bool g_wantSound = false, g_wantEffect = false;
volatile LONG* g_field = NULL;  // the app's DisableSystems mask

bool Arm(void*, int part)
{
	if (part == RS::kHold) return CH::Arm(HoldTarget(g_refuse != RS::kHold));
	if (part == g_refuse || part == g_refuse2)
	{
		RS::PartReport(part, part == RS::kSound ? "sound-exec-bind" : "effect-player-bind",
		               "the stand-in's binding refused (as AudioOfExe / PosterOfExe on a build without its rows)");
		return false;
	}
	if (part == RS::kSound) g_wantSound = true;
	if (part == RS::kEffect) g_wantEffect = true;
	return true;
}
bool Answering(void*, int part)
{
	if (part == RS::kHold) return CH::CurrentState() == CH::kActive;
	if (part == RS::kSound) return SE::Attached();
	return EP::Attached();
}
void Disarm(void*, int part)
{
	if (part == RS::kHold) CH::Disarm();
	if (part == RS::kSound) g_wantSound = false;
	if (part == RS::kEffect) g_wantEffect = false;
}
void Release(void*) { CH::ReleaseAll(); }
bool Combat(void*, bool off, char* text, size_t cap) { return RS::CombatBit((ULONG_PTR)g_field, off, text, cap); }

uint32_t g_pubGen = 0, g_pubState = 0, g_pubParts = 0;
// One EXE frame: the bridge's chain (the hold's drop, the sound executor, the effect player, route S) and the glue's
// section steps (proxy_route_s.cpp FrameHook).
void Frame()
{
	++g_frame;
	CH::OnFrame();
	SE::Audio a = {NULL, MockScript, MockPlay, MockLive, MockStop};
	if (g_wantSound && !SE::Attached() && !g_soundNoAttach) SE::Attach(Sec(), 77, a);
	if (!g_wantSound && SE::Attached()) SE::Detach();
	g_seReads = 0;
	if (SE::Attached() && !g_soundStalled) SE::Step(g_frame);
	EP::Poster p = {NULL, MockPost};
	if (g_wantEffect && !EP::Attached()) EP::Attach(Sec(), p);
	if (!g_wantEffect && EP::Attached()) EP::Detach();
	if (EP::Attached()) EP::Step(g_frame);
	const VPTrigServer* s = VPTRIG_SERVER(Sec());
	if (s->magic == VPTRIG_MAGIC && s->routeSAsk != 0 && s->routeSAsk == s->generation)
	{
		RS::Ask(s->generation);
		RS::ServerDecision(s->routeSTaken);
		RS::Step();
		const uint32_t st = RS::AnswerState();
		if (st != 0 && (s->generation != g_pubGen || st != g_pubState || RS::AnswerParts() != g_pubParts))
		{
			VPTrigPublishRouteS(Sec(), s->generation, st, RS::AnswerParts(), RS::SteamBuild());
			g_pubGen = s->generation;
			g_pubState = st;
			g_pubParts = RS::AnswerParts();
		}
	}
	else
		RS::Step();
	if (RS::CurrentState() == RS::kArmed) RS::CombatKeepOff((ULONG_PTR)g_field);
	InterlockedExchangeAdd(&g_now, 33);
}
void Frames(int n)
{
	for (int i = 0; i < n; ++i) Frame();
}

// config.ini masks: the product's keys off (0368's list: Combat, Particle, Projectile on = bits 0x3800 set), and the same
// with the player's Combat key off
const DWORD kIniOn = 0xFFEC3F67u;
const DWORD kIniOff = kIniOn & ~0x800u;

void Fresh(DWORD ini = kIniOn, DWORD field = kIniOn, bool supported = true)
{
	CH::Test::Reset();
	SE::Test::Reset();
	EP::Test::Reset();
	RS::Test::Reset();
	g_log.clear();
	g_lines.clear();
	g_loud = 0;
	g_reports = 0;
	g_partReportsDirect = 0;
	g_reportCheck.clear();
	g_reportLine.clear();
	g_now = 1000;
	g_frame = 100;
	g_refuse = g_refuse2 = -1;
	g_soundNoAttach = g_soundStalled = false;
	g_wantSound = g_wantEffect = false;
	g_pubGen = g_pubState = g_pubParts = 0;
	SetUpQueue();
	CH::SetSinks(LogSink, HoldReport);
	CH::Test::SetClock(Clock);
	CH::SetWatchdogSink(RS::HoldWatchdog);
	g_seTickInStep = 0;
	g_seReads = 0;
	SE::SetSinks(LogSink, CH::OwnerEnd, SeClock);
	SE::SetFightStartSink(CH::FightStarted);   // 4783: the server's start of each fight (glue: ProxySoundExec::Init)
	EP::SetSinks(LogSink, Clock);
	RS::SetSinks(LogSink, ReportSink, Clock);
	RS::Parts parts = {NULL, Arm, Answering, Disarm, Release, Combat};
	RS::Configure(parts, supported ? 4390913u : 0u, supported ? "Steam build 4390913 (test)" : "an unknown build (test)",
	              supported);
	RS::SetIniMask(1, ini);
	*g_field = (LONG)field;
	g_sec.assign(VPTRIG_SECTION_BYTES, 0);
	g_gen = 0;
}

uint32_t Answer(uint32_t* parts = NULL, uint32_t* build = NULL) { return VPTrigRouteSAnswer(Sec(), parts, build); }

// ---- the cases -------------------------------------------------------------------------------------------------------
void CaseIdleWithoutQuestion()
{
	Fresh();
	ServerInit(false);   // a server without the question (no route S): nothing is armed, nothing is written
	Frames(10);
	Fight(11, 1);
	CHECK(RS::CurrentState() == RS::kIdle);
	CHECK(CH::Test::Takes() == 0 && !SE::Attached() && !EP::Attached());
	CHECK(*g_field == (LONG)kIniOn);
	CHECK(Answer() == VPTRIG_ROUTES_NONE && g_reports == 0);
}

void CaseArmedAndUnload()
{
	Fresh();
	ServerInit(true);
	Frame();   // the question: the two readers armed
	CHECK(RS::CurrentState() == RS::kArming);
	CHECK(CH::CurrentState() == CH::kNotArmed);   // the hold comes last
	Frame();   // both attached: the hold, the Combat bit, ARMED
	CHECK(RS::CurrentState() == RS::kArmed);
	CHECK(CH::CurrentState() == CH::kActive && SE::Attached() && EP::Attached());
	CHECK(LinesWith("route S: ARMED on Steam build 4390913") == 1);
	// 4730: each part's answer time (wall and step time after the question) and the window's step time in the line
	CHECK(LogHas("(decided once for this EXE process, 33 ms after the question, 33 ms of step time of the 5000 ms window, "
	             "longest gap between steps 33 ms): combat hold armed (answered at 33 ms, 33 ms of step time), sound "
	             "executor armed (answered at 33 ms, 33 ms of step time), effect player armed (answered at 33 ms, 33 ms of "
	             "step time); Combat bit cleared"));
	CHECK(((DWORD)*g_field & 0x800u) == 0 && ((DWORD)*g_field | 0x800u) == kIniOn);   // only the Combat bit
	uint32_t parts = 0, build = 0;
	CHECK(Answer(&parts, &build) == VPTRIG_ROUTES_ARMED);
	CHECK(parts == 0x707u && build == 4390913u);
	CHECK(g_reports == 0 && g_loud == 0);
	// a fight: held, and dropped by the server's OwnerEnd through the sound executor (R8)
	Fight(21, 1);
	CHECK(CH::Test::Takes() == 1 && CH::Test::Held() == 1);
	CHECK(ServerOwnerEnd(21));
	Frames(3);
	CHECK(CH::Test::DropsEnd() == 1 && CH::Test::DropsWatchdog() == 0 && CH::Test::Held() == 0);
	CHECK(RS::CurrentState() == RS::kArmed);
	// the EXE sets the bit again (a load?): cleared again while armed
	*g_field = (LONG)((DWORD)*g_field | 0x800u);
	Frames(31);
	CHECK(((DWORD)*g_field & 0x800u) == 0);
	// the unload (proxy_rt.cpp Shutdown / DetachOnUnload call OnUnload -> CombatBit(field, false)): back as found
	char text[200];
	CHECK(RS::CombatBit((ULONG_PTR)g_field, false, text, sizeof(text)));
	CHECK(*g_field == (LONG)kIniOn && !RS::CombatClearedByUs());
	CHECK(strstr(text, "set back on as found") != NULL);
	// a second restore writes nothing
	CHECK(RS::CombatBit((ULONG_PTR)g_field, false, text, sizeof(text)) && *g_field == (LONG)kIniOn);
}

// One part refuses at the arming: NOT ARMED, no hold, the Combat bit untouched, one report, one decision line.
void CheckNotArmed(const char* partText, uint32_t wantParts)
{
	Frames(3);
	CHECK(RS::CurrentState() == RS::kNotArmed);
	CHECK(CH::CurrentState() != CH::kActive);
	CHECK(!SE::Attached() && !EP::Attached());
	CHECK(*g_field == (LONG)kIniOn);
	Fight(31, 2);
	CHECK(CH::Test::Takes() == 0 && CH::Test::Held() == 0);   // no hold is taken
	CHECK(g_reports == 1);
	CHECK(LinesWith("LOUD: route S: NOT ARMED on Steam build 4390913") == 1);
	CHECK(LogHas(partText));
	CHECK(LogHas("Combat bit: as config.ini has it: nothing written, stock fights"));
	uint32_t parts = 0;
	CHECK(Answer(&parts) == VPTRIG_ROUTES_NOT_ARMED);
	CHECK(parts == wantParts);
	CHECK(!ServerOwnerEnd(31));   // the server sends no record
	// decided once: further frames and a new generation's question change nothing
	ServerInit(true);
	Frames(5);
	CHECK(RS::CurrentState() == RS::kNotArmed && Answer() == VPTRIG_ROUTES_NOT_ARMED && g_reports == 1);
	CHECK(CH::Test::Takes() == 0);
}

void CaseRefuseHold()
{
	Fresh();
	g_refuse = RS::kHold;
	ServerInit(true);
	CheckNotArmed("combat hold REFUSED (combat-hold-build", 0x706u);
	CHECK(strstr(RS::Test::PartWhy(RS::kHold), "combat-hold-build") != NULL);
	CHECK(CH::Test::Reports() == 1);   // the hold's own report went into route S's one report, not a popup of its own
}

void CaseRefuseSound()
{
	Fresh();
	g_refuse = RS::kSound;
	ServerInit(true);
	CheckNotArmed("sound executor REFUSED (sound-exec-bind", 0x604u);
	CHECK(LogHas("combat hold not asked"));
	CHECK(g_reportCheck == "route-s-sound-exec");
}

void CaseRefuseEffect()
{
	Fresh();
	g_refuse = RS::kEffect;
	ServerInit(true);
	CheckNotArmed("effect player REFUSED (effect-player-bind", 0x602u);
	CHECK(g_reportCheck == "route-s-effect-player");
}

void CaseTwoRefusals()
{
	Fresh();
	g_refuse = RS::kSound;
	g_refuse2 = RS::kEffect;
	ServerInit(true);
	CheckNotArmed("effect player REFUSED", 0x600u);
}

void CaseNoAttach()
{
	Fresh();
	g_soundNoAttach = true;
	ServerInit(true);
	Frames(100);   // 3.3 s
	CHECK(RS::CurrentState() == RS::kArming && CH::CurrentState() == CH::kNotArmed);
	// 4730 (b): while frames run, the window is 5 s as before: the step at 5016 ms (the first with 5000 ms or more) falls back
	const DWORD t0 = 1000;   // the question's frame (Fresh: g_now = 1000)
	while (RS::CurrentState() == RS::kArming && (DWORD)g_now - t0 < 10000) Frame();
	CHECK(RS::CurrentState() == RS::kNotArmed && (DWORD)g_now - t0 == 5016 + 33);   // fell back in the frame at 5016 ms
	CHECK(LogHas("a part did not answer in time"));
	// 4730 (c): the NO ANSWER line names the times; the effect player's answer at the first frame
	CHECK(LogHas("sound executor NO ANSWER in the 5000 ms window (5016 ms of step time, 5016 ms after the question, longest "
	             "gap between steps 33 ms)"));
	CHECK(LogHas("effect player armed (answered at 33 ms, 33 ms of step time)") && LogHas("combat hold not asked"));
	Frames(2);   // the effect player detaches at its next frame
	CHECK(!EP::Attached() && g_reports == 1 && *g_field == (LONG)kIniOn);
	Fight(41, 3);
	CHECK(CH::Test::Takes() == 0);
}

// 4730 (a): a load stall between the question and the next frames (W24's huge turn-350 save: 7718 ms) does not use up the
// window. First W24's shape: both readers attach at the first frame after the stall (armed before the fix too). Then the
// case the fix is for: after the stall the sound executor needs one frame more; before the fix that frame's step saw
// 7 s of wall time and fell back (a false NOT ARMED and a report on a game with nothing wrong).
void CaseStallThenAnswered()
{
	Fresh();
	ServerInit(true);
	Frame();   // the question (g_now 1000): the two readers armed
	CHECK(RS::CurrentState() == RS::kArming);
	g_now += 7685;   // no frame for 7.7 s
	Frame();         // both attach: ARMED at 7718 ms
	CHECK(RS::CurrentState() == RS::kArmed && g_reports == 0 && g_loud == 0);
	CHECK(LogHas("7718 ms after the question, 250 ms of step time of the 5000 ms window, longest gap between steps 7718 ms"));
	CHECK(LogHas("sound executor armed (answered at 7718 ms, 250 ms of step time)"));

	Fresh();
	g_soundNoAttach = true;   // the executor's section is not mappable yet: it attaches a frame later
	ServerInit(true);
	Frame();
	g_now += 7000;   // the stall
	Frame();         // the effect player attaches; the executor not yet: 7033 ms of wall time, 250 ms of step time
	CHECK(RS::CurrentState() == RS::kArming && g_reports == 0);
	g_soundNoAttach = false;
	Frame();         // the executor attaches: ARMED
	CHECK(RS::CurrentState() == RS::kArmed && CH::CurrentState() == CH::kActive && g_reports == 0 && g_loud == 0);
	CHECK(LinesWith("route S: ARMED on Steam build 4390913") == 1 && LinesWith("NOT ARMED") == 0);
	CHECK(LogHas("7066 ms after the question, 283 ms of step time of the 5000 ms window, longest gap between steps 7033 ms"));
	CHECK(LogHas("combat hold armed (answered at 7066 ms, 283 ms of step time), sound executor armed (answered at 7066 ms, "
	             "283 ms of step time), effect player armed (answered at 7033 ms, 250 ms of step time)"));
	CHECK(Answer() == VPTRIG_ROUTES_ARMED && ((DWORD)*g_field & 0x800u) == 0);
}

// 4730 (b) with stalls: a part that never answers still falls back. One stall: after 5 s of frames beside it. Every frame
// a stall (1 s apart, a loading screen's pace): after 20 steps (5000 / 250 ms), not never.
void CaseNoAnswerWithStalls()
{
	Fresh();
	g_soundNoAttach = true;
	ServerInit(true);
	Frame();
	g_now += 7000;
	Frame();
	CHECK(RS::CurrentState() == RS::kArming);
	const DWORD t0 = 1000;
	while (RS::CurrentState() == RS::kArming && (DWORD)g_now - t0 < 30000) Frame();
	// steps at 0, 7033 (250 counted), then every 33 ms: 5000 ms of step time at the 144th more (250 + 144 x 33 = 5002 ms),
	// 7033 + 144 x 33 = 11785 ms after the question
	CHECK(RS::CurrentState() == RS::kNotArmed && (DWORD)g_now - t0 == 11785 + 33);
	CHECK(LogHas("sound executor NO ANSWER in the 5000 ms window (5002 ms of step time, 11785 ms after the question, "
	             "longest gap between steps 7033 ms)"));
	CHECK(g_reports == 1 && g_reportCheck == "route-s-sound-exec-no-answer" && *g_field == (LONG)kIniOn);

	Fresh();
	g_soundNoAttach = true;
	ServerInit(true);
	Frame();
	int steps = 0;
	while (RS::CurrentState() == RS::kArming && steps < 100)
	{
		g_now += 1000 - 33;   // with the frame's own 33 ms: one step a second
		Frame();
		++steps;
	}
	CHECK(RS::CurrentState() == RS::kNotArmed && steps == 20);
	CHECK(LogHas("sound executor NO ANSWER in the 5000 ms window (5000 ms of step time, 20000 ms after the question, longest "
	             "gap between steps 1000 ms)"));
	CHECK(g_reports == 1);
}

// The executor stops answering mid-session: one watchdog, the fight's id in the line, released, disarmed as one.
void CaseExecutorStops()
{
	Fresh();
	ServerInit(true);
	Frames(2);
	CHECK(RS::CurrentState() == RS::kArmed);
	g_soundStalled = true;   // attached, but no OwnerEnd reaches the hold any more
	Fight(201, 5);
	CHECK(CH::Test::Takes() == 1);
	CHECK(ServerOwnerEnd(201));   // written, never executed
	const DWORD t0 = (DWORD)g_now;
	while (RS::CurrentState() == RS::kArmed && (DWORD)g_now - t0 < 30000) Frame();
	const DWORD waited = (DWORD)g_now - t0;
	CHECK(RS::CurrentState() == RS::kDisarmed);
	CHECK(waited >= 22000 && waited < 22200);
	CHECK(CH::Test::DropsWatchdog() == 1 && RS::Test::Watchdogs() == 1);
	CHECK(LinesWith("LOUD: route S: DISARMED as one on Steam build 4390913") == 1);
	CHECK(LogHas("the hold's watchdog dropped fight event id 201 after 22"));
	CHECK(*g_field == (LONG)kIniOn);   // the Combat bit back as found: the next fights play stock
	CHECK(CH::CurrentState() == CH::kNotArmed);
	Frames(2);
	CHECK(!SE::Attached() && !EP::Attached());
	CHECK(Answer() == VPTRIG_ROUTES_DISARMED);
	CHECK(g_reports == 1 && g_reportCheck == "route-s-watchdog");
	// the next fight is not held: a player never waits 22 s twice
	Fight(202, 6);
	CHECK(CH::Test::Takes() == 1 && CH::Test::Held() == 0);
	CHECK(!ServerOwnerEnd(202));
	Frames(800);
	CHECK(RS::Test::Watchdogs() == 1 && g_reports == 1 && RS::CurrentState() == RS::kDisarmed);
}

void CaseServerNotTaken()
{
	Fresh();
	ServerInit(true);
	Frames(2);
	CHECK(RS::CurrentState() == RS::kArmed);
	VPTRIG_SERVER(Sec())->routeSTaken = VPTRIG_TAKEN_NO_ANSWER;
	Frame();
	CHECK(RS::CurrentState() == RS::kDisarmed);
	CHECK(LogHas("the server did not take route S (no answer at its first fight record)"));
	CHECK(*g_field == (LONG)kIniOn && CH::CurrentState() == CH::kNotArmed && g_reports == 1);
	// while arming: a not-armed decision instead
	Fresh();
	ServerInit(true);
	Frame();
	CHECK(RS::CurrentState() == RS::kArming);
	VPTRIG_SERVER(Sec())->routeSTaken = VPTRIG_TAKEN_NO_ANSWER;
	Frame();
	CHECK(RS::CurrentState() == RS::kNotArmed && CH::CurrentState() == CH::kNotArmed && *g_field == (LONG)kIniOn);
	// 4730 (c): the readers attached at this frame are named with their time; the hold, never asked, did not answer
	CHECK(LogHas("combat hold NO ANSWER before the server's decision (0 ms of step time, 33 ms after the question, longest "
	             "gap between steps 0 ms), sound executor armed (answered at 33 ms, 0 ms of step time)"));
}

// A part stops after the arming (the effect player's fault): a held fight released at once, disarmed as one.
void CasePartFault()
{
	Fresh();
	ServerInit(true);
	Frames(2);
	Fight(301, 7);
	CHECK(CH::Test::Held() == 1);
	EP::Detach();            // the player turned itself off (an access fault: proxy_effect_player.cpp FrameHook)
	g_wantEffect = false;
	RS::PartReport(RS::kEffect, "effect-player-fault", "an access fault (stand-in)");
	Frame();                 // route S sees it, releases, disarms
	CHECK(RS::CurrentState() == RS::kDisarmed);
	Frame();                 // the hold's next frame drops the released hold
	CHECK(CH::Test::Released() == 1 && CH::Test::Held() == 0 && CH::Test::DropsWatchdog() == 0);
	CHECK(LogHas("the effect player stopped answering (effect-player-fault"));
	CHECK(g_reports == 1 && *g_field == (LONG)kIniOn);
}

void CaseCombatAlreadyOff()
{
	Fresh(kIniOff, kIniOff);
	ServerInit(true);
	Frames(2);
	CHECK(RS::CurrentState() == RS::kArmed);
	CHECK(LogHas("Combat bit already off (config.ini has Combat off): nothing written"));
	CHECK(*g_field == (LONG)kIniOff && !RS::CombatClearedByUs());
	VPTRIG_SERVER(Sec())->routeSTaken = VPTRIG_TAKEN_NO_ANSWER;
	Frame();
	CHECK(RS::CurrentState() == RS::kDisarmed && *g_field == (LONG)kIniOff);   // never set on: the system was never started
	CHECK(LogHas("off as config.ini has it: nothing written, fights as with Combat off"));
}

void CaseValueNotRecognised()
{
	Fresh(kIniOn, kIniOn & ~0x4u);   // another bit differs from config.ini's
	ServerInit(true);
	Frames(2);
	CHECK(RS::CurrentState() == RS::kNotArmed);
	CHECK(LogHas("the EXE's Combat bit cannot be cleared (the app's DisableSystems mask differs from config.ini's in bits "
	             "0x00000004: not recognised, not written)"));
	CHECK(*g_field == (LONG)(kIniOn & ~0x4u) && CH::CurrentState() == CH::kNotArmed && g_reports == 1);
	Fight(51, 8);
	CHECK(CH::Test::Takes() == 0);
	// the Combat bit found already cleared while config.ini has it on: not recognised either
	Fresh(kIniOn, kIniOff);
	ServerInit(true);
	Frames(2);
	CHECK(RS::CurrentState() == RS::kNotArmed && *g_field == (LONG)kIniOff);
	// config.ini not readable: no write, no arming
	Fresh();
	RS::SetIniMask(-1, 0);
	ServerInit(true);
	Frames(2);
	CHECK(RS::CurrentState() == RS::kNotArmed && *g_field == (LONG)kIniOn);
}

void CaseSecondGeneration()
{
	Fresh();
	ServerInit(true);
	Frames(2);
	CHECK(RS::CurrentState() == RS::kArmed);
	ServerInit(true);   // a load: the server's next generation asks again
	CHECK(Answer() == VPTRIG_ROUTES_NONE);
	Frame();
	CHECK(Answer() == VPTRIG_ROUTES_ARMED && RS::CurrentState() == RS::kArmed);
	CHECK(LinesWith("route S: ARMED") == 1 && LogHas("generation 2 asks"));
}

void CaseUnsupportedBuild()
{
	Fresh(kIniOn, kIniOn, false);
	g_refuse = RS::kSound;
	ServerInit(true);
	Frames(2);
	CHECK(RS::CurrentState() == RS::kNotArmed && g_reports == 0);
	CHECK(LogHas("(not a supported build: no report)"));
}

// ---- bursts (4783: W24 #8726, MC #8731 / #8737): the DLL takes an AI unit's attacks in one go, the server plays them
// one after another; each fight's watchdog runs from the server's start (its first record through the executor), a
// fight never started is bounded by its outer bound from the take.
const int kFightFrames = 136;   // 4.5 s of 33 ms frames: one fight as the server plays it
void PlaySerial(const uint32_t* ids, int from, int to, int skip)
{
	for (int k = from; k < to; ++k)
	{
		if (k == skip) continue;
		CHECK(ServerStart(ids[k]));
		Frames(kFightFrames);
		CHECK(ServerOwnerEnd(ids[k]));
		Frames(2);
	}
}

void CaseBurstNine()
{
	Fresh();
	ServerInit(true);
	Frames(2);
	CHECK(RS::CurrentState() == RS::kArmed);
	uint32_t ids[9];
	for (int k = 0; k < 9; ++k)
	{
		ids[k] = 501 + 2 * k;
		Fight(ids[k], 10 + k);   // nine takes in one frame
	}
	CHECK(CH::Test::Takes() == 9 && CH::Test::Held() == 9);
	const DWORD t0 = (DWORD)g_now;
	PlaySerial(ids, 0, 9, -1);
	CHECK((DWORD)g_now - t0 > 40000);   // the 9th OwnerEnd came about 41 s after its take: the old 22 s rule dropped 5-9
	CHECK(CH::Test::DropsEnd() == 9 && CH::Test::DropsWatchdog() == 0 && CH::Test::Starts() == 9 && CH::Test::Held() == 0);
	CHECK(RS::CurrentState() == RS::kArmed && RS::Test::Watchdogs() == 0 && g_reports == 0);
}

void CaseBurstMiddleNeverStarted()
{
	Fresh();
	ServerInit(true);
	Frames(2);
	uint32_t ids[9];
	for (int k = 0; k < 9; ++k)
	{
		ids[k] = 601 + 2 * k;
		Fight(ids[k], 20 + k);
	}
	const DWORD t0 = (DWORD)g_now;
	PlaySerial(ids, 0, 9, 4);   // the 5th (609) is lost by the server: never started, never ended
	CHECK(CH::Test::DropsEnd() == 8 && CH::Test::DropsWatchdog() == 0 && CH::Test::Held() == 1);
	CHECK(RS::CurrentState() == RS::kArmed);
	while (RS::CurrentState() == RS::kArmed && (DWORD)g_now - t0 < 200000) Frame();
	const DWORD waited = (DWORD)g_now - t0;
	CHECK(RS::CurrentState() == RS::kDisarmed && waited >= 110000 && waited < 110200);   // 5 x 22 s: its queue position 4
	CHECK(CH::Test::DropsOuter() == 1 && CH::Test::DropsWatchdog() == 1 && RS::Test::Watchdogs() == 1);
	CHECK(LogHas("fight event id 609 after 110") && LogHas("(never started, the outer bound)"));
	CHECK(LinesWith("LOUD: route S: DISARMED as one") == 1 && g_reports == 1 && *g_field == (LONG)kIniOn);
}

void CaseBurstAcrossTurnEnd()
{
	Fresh();
	ServerInit(true);
	Frames(2);
	uint32_t ids[9];
	for (int k = 0; k < 5; ++k)
	{
		ids[k] = 701 + 2 * k;
		Fight(ids[k], 30 + k);   // the end of turn T: five attacks
	}
	PlaySerial(ids, 0, 2, -1);
	for (int k = 5; k < 9; ++k)
	{
		ids[k] = 701 + 2 * k;
		Fight(ids[k], 30 + k);   // turn T + 1 begins with four more while three of T's still wait
	}
	PlaySerial(ids, 2, 9, -1);
	CHECK(CH::Test::DropsEnd() == 9 && CH::Test::DropsWatchdog() == 0 && CH::Test::Held() == 0);
	CHECK(RS::CurrentState() == RS::kArmed && g_reports == 0);
}

void CaseStartedOwnerEndLost()
{
	Fresh();
	ServerInit(true);
	Frames(2);
	Fight(801, 40);
	Fight(803, 41);
	Fight(805, 42);
	CHECK(ServerStart(801));   // started; its OwnerEnd never comes
	Frame();
	const DWORD t0 = (DWORD)g_now;
	while (RS::CurrentState() == RS::kArmed && (DWORD)g_now - t0 < 60000) Frame();
	const DWORD waited = (DWORD)g_now - t0;
	CHECK(RS::CurrentState() == RS::kDisarmed && waited >= 21900 && waited < 22200);   // 22 s from the start
	CHECK(LogHas("fight event id 801 after 22") && LogHas("(started by the server, its 22 s bound)"));
	Frame();
	CHECK(CH::Test::DropsWatchdog() == 1 && CH::Test::DropsOuter() == 0 && CH::Test::Released() == 2 && CH::Test::Held() == 0);
	CHECK(g_reports == 1 && *g_field == (LONG)kIniOn);
}
// 4740 (W38's S4, 2026-11-21): a burst of three; the 2nd (S4's 122793) is started after the 1st's end; 5.5 s into it the
// server stops one of its sounds in a frame where GetTickCount ticks inside the executor's step. Before 4740: the
// executor's own watchdog ended the fight at once (an unsigned wrap), its OwnerEnd 0.3 s later never reached the hold, the
// hold's watchdog dropped it 22 s after its start and route S disarmed (one report). After: every hold ends by OwnerEnd.
void CaseTickInsideExecutorStep()
{
	Fresh();
	ServerInit(true);
	Frames(2);
	CHECK(RS::CurrentState() == RS::kArmed);
	const uint32_t ids[3] = {122775, 122793, 122805};
	for (int k = 0; k < 3; ++k) Fight(ids[k], 50 + k);   // three takes in one frame
	CHECK(CH::Test::Takes() == 3 && CH::Test::Held() == 3);
	PlaySerial(ids, 0, 1, -1);                             // 122775 plays and ends
	CHECK(ServerStart(ids[1]));                            // 122793 starts (its sound start, handle = its id)
	Frames(166);                                           // 5.5 s
	g_seTickInStep = 16;                                   // the tick comes inside the executor's step
	CHECK(ServerSoundStop(ids[1], ids[1]));
	Frame();
	g_seTickInStep = 0;
	CHECK(!LogHas("sent nothing for"));                    // before 4740: the executor's watchdog line in this frame
	Frames(9);                                             // 0.3 s
	CHECK(ServerOwnerEnd(ids[1]));
	Frames(2);
	CHECK(CH::Test::DropsEnd() == 2 && CH::Test::Held() == 1);   // before 4740: 1 and 2 (122793 still held)
	PlaySerial(ids, 2, 3, -1);                             // 122805 plays and ends
	const DWORD t0 = (DWORD)g_now;
	while ((DWORD)g_now - t0 < 30000) Frame();             // past the 22 s bound of every hold
	CHECK(CH::Test::DropsEnd() == 3 && CH::Test::DropsWatchdog() == 0 && CH::Test::Held() == 0);
	CHECK(RS::CurrentState() == RS::kArmed && RS::Test::Watchdogs() == 0 && g_reports == 0);
	CHECK(LinesWith("DISARMED") == 0);
}
}  // namespace

int main(int argc, char** argv)
{
	bool expectFail = false;
	for (int i = 1; i < argc; ++i)
	{
		if (strcmp(argv[i], "-v") == 0) g_verbose = true;
		if (strcmp(argv[i], "--expect-fail") == 0) expectFail = true;
	}
	setvbuf(stdout, NULL, _IONBF, 0);
	g_field = static_cast<volatile LONG*>(VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	CaseIdleWithoutQuestion();
	CaseArmedAndUnload();
	CaseRefuseHold();
	CaseRefuseSound();
	CaseRefuseEffect();
	CaseTwoRefusals();
	CaseNoAttach();
	CaseStallThenAnswered();
	CaseNoAnswerWithStalls();
	CaseExecutorStops();
	CaseServerNotTaken();
	CasePartFault();
	CaseCombatAlreadyOff();
	CaseValueNotRecognised();
	CaseSecondGeneration();
	CaseUnsupportedBuild();
	CaseBurstNine();
	CaseBurstMiddleNeverStarted();
	CaseBurstAcrossTurnEnd();
	CaseStartedOwnerEndLost();
	CaseTickInsideExecutorStep();
	if (expectFail) CHECK(!"the runner's negative test: this check must fail");
	printf("route_s_test: %d/%d checks passed\n", g_pass, g_pass + g_fail);
	return g_fail ? 1 : 0;
}
