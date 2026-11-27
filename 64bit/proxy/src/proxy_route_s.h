// proxy_route_s.h - route S (D210) arms as one and falls back as one (ticket 4783, epic 0042; the MC's ruling #8543 on
// queue thread 0042). Hand-written. SPDX-License-Identifier: GPL-3.0-or-later
//
// The shell's three route S parts are the combat hold (proxy_combat_hold.h, 4773), the sound executor
// (proxy_sound_exec.h, 4723) and the effect player (proxy_effect_player.h, 4727). Each has its own checks and its own
// refusal. This module adds the rule over them: the three reach ONE common state before the first fight, decided once
// per EXE process (one proxy load), and the server takes route S only when that state is ARMED.
//
//   - The question: the server asks in its trigger section (vp_trigger.h R9: routeSAsk = its generation). On the first
//     question this module arms the sound executor and the effect player, waits until both readers are attached (at most
//     kAnswerMs of step time, 4730), then arms the hold and clears the EXE's Combat bit at run time (below). The hold comes last, so no hold
//     is ever taken while the OwnerEnd carrier (the sound executor) is not attached.
//   - The answer: ARMED when all three answered armed; NOT_ARMED when any refused or did not answer in time, and then
//     NONE is armed (each part is disarmed again, the Combat bit is left or set back as config.ini has it). The answer is
//     published in the reader block for every generation the server asks (R9) and logged in ONE line that names each
//     part's answer and the build.
//   - Disarmed as one: the hold's watchdog (22 s from the server's start of a fight, or its outer bound from the take), a part
//     that stops answering (no longer attached or active: a fault), or the server's NOT TAKEN (no answer at its first
//     fight) disarms all three for the rest of the EXE process: every held fight is released at once, the Combat bit is
//     set back, the answer turns DISARMED (the server then sends no record). One LOUD line names the cause and the
//     fights' event ids. A player never waits 22 s twice.
//   - D118: a refusal, a missed answer or a disarm on a supported build (VpExeBuildProductReady) is a fault: the D118
//     report (stage "route-s", non-fatal, the helper's popup) is written ONCE per EXE process, never per fight. The
//     parts' own reports are routed here (their reasons go into it). On a build the patch does not support the line is
//     LOUD and no report is written (the patch's own unknown-build path reports that build).
//   - The Combat bit (the session keys of 4783): config.ini keeps Combat and Projectile ON (0368, D147: no product
//     default changes), so a session that is not armed plays every fight the stock way (the EXE's simulation, its own
//     timing, sounds and particles). ARMED clears the Combat bit 0x800 of the app's DisableSystems mask
//     ([cam_app] + 0x940, the build table's cam_app); the EXE's simulation tests that bit at the start of each fight
//     (research/05-graphics-composition/combat-choreography.md 4.1 [C]), so from then on every visualised fight runs as
//     with Combat off (no simulation, no EXE combat sound or particle; the hold keeps its records until the server's
//     OwnerEnd). Disarm sets the bit back only when this module cleared it. When config.ini already has Combat off the
//     bit is left as it is (and NOT_ARMED then means fights as with Combat off, named in the line). Projectile 0x2000
//     is not written: the projectiles of a fight are launched by the simulation (effects-server.md 1.1).
//
// The product switch: [Renderer] ServerCombat (options id 28, default 1; MC #8627). With 0 the proxy writes no
// render_units_triggers=1 (proxy_shell.cpp UnitLines), the server never asks and this module stays idle: stock fights,
// one start-log line, no report. That is also the ruling's live forced refusal.
// TEST ONLY, compiled out of every product build (MC #8639): with /DVP64_ROUTE_S_TEST_REFUSE_BUILD (no builder of the
// repository defines it) VP64_TEST_ROUTE_S_REFUSE=hold|sound|effect makes that ONE part's arm answer "refused". The
// offline test makes each part refuse through its stand-ins instead.
//
// Parts: the core (Configure, Ask, Step, ServerDecision, HoldWatchdog, PartReport) works on a Parts of plain function
// pointers and is tested offline with the three real cores (proxy/tests/route_s/); the glue (Init, FrameHook, the
// section, the Combat bit through the build table, the report) is the only part that needs the proxy runtime.
//
// Toolchain: clang-cl -m32 with VC9 headers (the proxy's), C++03, no STL in the core.
#pragma once
#include <windows.h>
#include <stddef.h>
#include <stdint.h>

namespace ProxyRouteS
{
enum Part
{
	kHold = 0,
	kSound = 1,
	kEffect = 2,
	kPartCount = 3
};

enum State
{
	kIdle = 0,      // no question yet (the server has no trigger stream, or has not asked)
	kArming = 1,    // the sound executor and the effect player armed; waiting for both to attach
	kArmed = 2,     // all three armed and attached, the Combat bit cleared (or already off)
	kNotArmed = 3,  // a part refused or did not answer in time: none armed for this EXE process
	kDisarmed = 4   // was armed; disarmed as one for the rest of this EXE process
};

// The answer window (ticket 4730): the two readers attach at the frame after their arm, so the window counts STEP time,
// not wall time: each gap between two arming steps counts at most kStepGapMs. A load stall (the main thread blocked for
// seconds, no frame; W24's huge turn-350 save: 7.7 s between the question and the next frame) then uses up at most
// kStepGapMs of the window, and a part that never answers while frames run falls back after kAnswerMs of frames as
// before (at least kAnswerMs / kStepGapMs = 20 steps). The ARMED line and every NO ANSWER line name each part's answer
// time (wall and step time after the question) and the longest gap between steps.
const DWORD kAnswerMs = 5000;
const DWORD kStepGapMs = 250;   // a gap above this is a stall, not a frame: it counts as kStepGapMs
const int kMaxIds = 8;          // event ids named in the watchdog's line

// What the module does to the parts and the EXE. Every function runs on the EXE's main thread (the frame hook).
struct Parts
{
	void* ctx;
	bool (*arm)(void* ctx, int part);         // the part's SetServerFights(true); false = refused
	bool (*answering)(void* ctx, int part);   // the hold: active; the sound executor, the effect player: attached
	void (*disarm)(void* ctx, int part);      // the part's SetServerFights(false)
	void (*releaseHolds)(void* ctx);          // every hold dropped at the hold's next frame (ProxyCombatHold::ReleaseAll)
	// The EXE's Combat bit: off = true clears it (when set), false sets it back (only when this module cleared it).
	// Returns false when the bit cannot be read or written; text = what was done ("cleared", "already off in
	// config.ini", "set back", "left off").
	bool (*combat)(void* ctx, bool off, char* text, size_t textCap);
};

typedef void (*LogFn)(const char* line, bool loud);
typedef void (*ReportFn)(const char* check, const char* line);   // the D118 report: once per EXE process
typedef DWORD (*ClockFn)();                                      // ms, wraps: GetTickCount in the product

void SetSinks(LogFn log, ReportFn report, ClockFn clock);
// The parts and the EXE's build: steamBuild = 4390913 / 26577625 (0 = not a known build), label for the line, supported
// = VpExeBuildProductReady (a refusal is then a fault: the report).
void Configure(const Parts& parts, uint32_t steamBuild, const char* buildLabel, bool supported);

// The server's question for generation gen (!= 0): the first one starts the arming; later ones are answered with the
// state already decided.
void Ask(uint32_t gen);
// Once per frame on the main thread, after the parts' own frame hooks.
void Step();
// The server's decision (vp_trigger.h VPTrigRouteSTaken) for the generation asked: NO_ANSWER or REFUSED while armed or
// arming disarms as one.
void ServerDecision(uint32_t taken);
// The hold's watchdog dropped the hold of eventId after afterMs since its take (ProxyCombatHold's watchdog sink; the
// main thread); started: by the fight's own bound after the server started it, else by the outer bound.
void HoldWatchdog(uint32_t eventId, DWORD afterMs, bool started);
// A part's own D118 report (its check and line), routed here: kept as that part's reason; the one report names it.
void PartReport(int part, const char* check, const char* line);

// ---- the Combat bit's rule (the glue resolves the field: the app's DisableSystems mask; a test's word offline) --------
// config.ini's [DisableSystems] as the EXE's config mask, read once at the proxy's start (ProxySession::ReadConfigIniMask:
// read = 1 read, 0 no config.ini = every bit set, -1 unreadable).
void SetIniMask(int read, uint32_t mask);
// off = true: read before write (the value found must equal config.ini's mask in the 24 known bits, else no write and
// false); the bit cleared when set; already clear = nothing written, true. off = false: set back only when this module
// cleared it (the value found at the arming). text = what was done. Main thread.
bool CombatBit(ULONG_PTR field, bool off, char* text, size_t cap);
// While armed: the bit cleared again when something set it (true when it did). Main thread.
bool CombatKeepOff(ULONG_PTR field);
bool CombatClearedByUs();

State CurrentState();
// The answer for the reader block: VPTRIG_ROUTES_* (vp_trigger.h) and the parts' bits (VPTRIG_PART_*, answered << 8).
uint32_t AnswerState();
uint32_t AnswerParts();
uint32_t SteamBuild();
// "route S: state armed, asks N, generations N, watchdogs N, part faults N, server not taken N, reports N, combat bit cleared"
void CountersLine(char* out, size_t cap);

#ifndef VP_ROUTE_S_OFFLINE
// ---- the glue (the proxy runtime) ------------------------------------------------------------------------------------
// proxy_ui_bridge_rt.cpp Init, after the three parts' Init: the sinks, the parts' report and watchdog sinks, the build,
// the TEST ONLY switch, the chain. Once per proxy load.
void Init(void (*existingFrameHook)());
// The frame hook the bridge calls: the chained hook first (the parts'), then the question, the step and the answer.
void FrameHook();
// proxy_rt.cpp Shutdown and DetachOnUnload (MC #8582 condition 3): the Combat bit back as it was found at the arming,
// when this module cleared it. True with one line for the log when it did something.
bool OnUnload(char* line, size_t cap);
#endif

namespace Test
{
void Reset();   // forgets everything (state, counters; sinks and parts stay)
long Reports();
long Watchdogs();
long Disarms();
const char* PartWhy(int part);   // the part's reason as reported ("" = none)
}  // namespace Test
}  // namespace ProxyRouteS
