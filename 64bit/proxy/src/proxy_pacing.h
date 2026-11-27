// proxy_pacing.h - D165 option 3: a shorter update tick in AI turns (ticket 4662, epic 0437). Hand-written.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The EXE's main thread asks the game-core thread for the next ICvGame1::Update only on a tick: GameViewState::Update
// adds each frame's time to an accumulator and compares it with ICvEngineUtility1::getSecsPerTurn() (slot 102 of the
// engine utility object, 0.1 s). Between two AI players the frame accepts the next player and then waits for that tick
// (T_tick, research/02-exe-internals/game-core-thread.md section 6, research/08-performance/turn-time-study.md "What
// paces an AI turn in the client (D165)"). With VP64Options.ini [Performance] AiUpdateTickMs = 0..100 the proxy points
// the engine utility object's vptr (heap data) at a copy of the EXE's vftable whose slot 102 answers the option's tick
// only when all of these hold, and the EXE's own function otherwise:
//   - the return address is the frame's call site (VA 0x7258C1, after its "call edx" at 0x7258BF): every other
//     caller (the DLL's mission timer through the server, the proxy, any other EXE code) gets the EXE's 0.1;
//   - the calling thread is the EXE's main thread (the process's first thread);
//   - the frame's AI-turn byte (VA 0x2DE0447) is 1: from the frame's acceptance of an AI player to the end of its
//     Update; 0 in the human's turn, so the human's units' mission timers (which count Updates) are unchanged;
//   - the update hold (VA 0x2DE044C, an AI leader screen) is 0 (an addition to the design's three conditions: no
//     Update runs under the hold, so a faster tick there would only wake the core thread more often).
// Design: research/08-performance/d165-pacing-fix-design.md sections 3-7. No EXE instruction byte is read, compared or
// written (CEG, D4); only the EXE's PE header, data (the app global, the AI-turn byte, the hold), the vftable's entries
// (.rdata) and the heap object's vptr are read, and only the vptr is written (D165, D83: written from the knowledge
// base's description of the EXE's behaviour).
//
// On by default since ticket 0566 (D191, 2026-11-19): an absent key gives AiUpdateTickMs = 0 (options/src/vp64_options.cpp;
// it was -1 before). Off only with an explicit AiUpdateTickMs = -1: then nothing is read or written (one log line), the
// player's own file decides. With the default on, the checks below run for every player who has not set -1, so on an
// EXE other than the one this was written for they fail loud by default (wanted: MC #7259, D118/D125). Fail loud (MC #3503):
// a check that fails at the first install of a proxy load writes the D118 error report and starts the helper's popup;
// a check that fails later (the vptr no longer ours, an access fault, the app's pointer changed) writes the report and
// a loud log line, without a popup; in both cases the game goes on with the EXE's own pacing (the identical game).
// Every change of state is one line in vpproxy.log starting "pacing:"; counters every 60 s while active and at unload.
//
// D165 option 2 (ticket 4665), [Performance] AiSkipVisualDrain = 1: after each outermost ICvGame1::Update of an AI turn
// the proxy writes the player the frame's gate will look for into the heap field GameViewState+0x8CC (the player the
// frame accepted last), so the frame finds no player change and does not wait for queue B to drain before it asks for
// the next Update (game-core-thread.md 6.2 skip 2). Conditions, all of them: the AI-turn byte was 1 at the Update's
// start; the hold is 0; the frame's own scan, done by the proxy (the first player in slots 0-63 with IsEverAlive and
// IsTurnActive, through the same DLL interface the EXE uses), finds a player; that player is not the active human. One
// aligned interlocked write; nothing restored (the frame writes the field itself). Skip 3 (the AI-turn byte against the
// frame's per-frame flag) then does not fire either: in stock it fires only because skip 2 clears the flag, and the
// frame never scans while an Update is pending, so the write is in place before its first scan (perf/evidence/0437/
// 4665/notes.md [C, L]). Checks: at the install (the first Update with the option on) the EXE build (VerifyExe),
// g_Civ5App readable, [g_Civ5App+0xC0] readable with the GameViewState vptr (VA 0xA20908); again before every AI
// Update's write (the per-Update re-check). Fail loud exactly as option 3 (boot: report and popup; play: report and
// loud line, no popup); the two options are independent (one failing leaves the other as it is). Off (0 or absent):
// nothing read, nothing written, the scan never runs (one log line).
//
// D165 option 4 (ticket 4675), [Performance] AiDrainUnordered = 1, only with option 2 on: at the AI turn's end-of-phase
// drain (option 2's scan after an AI Update finds no turn-active player, so the frame waits for queue B to drain before
// the turn-change request) the proxy clears queue B's ordering byte (q+0x30, q = 0x2A10E08 + 0x4C * [0x2A10200], plain
// .data). With the byte 0 the pump's move (0x4AC910) skips its blocking check and moves the whole write half, in posting
// order, in one pass: the backlog is dispatched in one frame instead of one keyed record per unit per frame
// (research/08-performance/turn-time-study.md "What the drain is made of", d165-pacing-fix-design.md section 10). The
// byte is written in the EXE only by LoadGameState::Update (0 at its first state, 1 at a later one) [C, ticket 4675], so
// nothing in play sets it back by itself. Guards, all of them, read after the Update while it is still pending (the main
// thread pumps queue B only while no Update is pending, game-core-thread.md 6.3): the hold 0x2DE044C is 0 (option 2's
// test), Quick Combat and Quick Movement on (the DLL's own answers, ICvPreGame1), no record in queue B's dispatch half held
// (hold counter, header word 11) and no record in either half with flag bit 1 (header word 12), the byte 1. The byte is set
// back to 1 at the top of the next Update (the turn-change Update: its request set the pending count, so no pump has run
// since [C]), at a failure of option 2 or 4 and at unload. Install checks (with option 2's): the queue index in 0..2, the
// queue's control block readable, the byte 1. Fail loud as options 2 and 3 (checks named drain-*). With option 2 off (or
// host=headless) option 4 is off with one warning line.
//
// D191 option 2 value 2 (ticket 0571; 0568's variant F1 with the EXE's own gate, d165-pacing-fix-design.md section 11),
// [Performance] AiSkipVisualDrain = 2: option 2 as with 1, except at an AI gap (option 2's scan found the next
// turn-active AI player) where queue B holds more than kHoldFloorRecords undispatched records (the write half's and the
// dispatch half's from the dispatch offset q+0x2C on; one stable walk, as option 4's): there option 2 does NOT write
// +0x8CC, so the frame's own gate sees the player change, drops the AI-turn byte and waits until queue B has drained, as
// the EXE does; and with option 4 active and its guards passing there the proxy clears the ordering byte (set back at the
// next Update's top, as at the end of the phase), so that drain is one pump. Why not a hold of the next request (0568's
// design, built first): the frame's pump moves no record of queue B while the AI-turn byte is 1, even with no Update
// pending (ticket 0571, poller [L]), and option 2's write keeps the byte at 1 through the AI turn; a hold only waited.
// Gaps with at most the floor are written as with value 1. Value 2 without AiDrainUnordered = 1 drains each such gap in
// the EXE's order (stock pacing for big gaps; one warning line). A failed read of queue B at a gap is a failure of
// option 2 during play (checks skip-gap-*): no more writes or clears in this load, the D118 report. Counters:
// HoldCountersLine.
//
// D246 (the owner, 2026-11-23; ticket 0718): options 2 and 4 act only while Quick Combat AND Quick Movement are on. With
// animations on (either off) they let the AI players' animated moves and fights of several players play at once, so
// then the stock wait and order run. One read of the DLL's own settings (CvPreGame::quickCombat()/quickMovement(),
// ICvPreGame1; game-wide, what the DLL itself reads to animate or not, and what the in-game options menu switches) per AI
// Update that reaches a decision: after the Update, while it is still pending (so the main thread has not started to
// pump queue B: never mid-drain), i.e. at the next AI player's turn start, or at the end-of-phase drain's start. That
// one read decides option 2's write (value 1 and 2) and option 4's clear (end of phase and gated gaps). Both on: as
// built. Either off, or the settings not answered: +0x8CC is not written (the frame's own gate sees the player change
// and drains queue B in the EXE's order, as stock) and the ordering byte is not cleared. Toggling the quick modes in
// game therefore switches the options from the next AI player on. Each change of the read state is one log line
// ("pacing: D246: ..."); counters in SkipCountersLine ("animations on N (...)"). The TEST variable of option 4
// (VP64_PACING_TEST_DRAIN_ANY_MOVEMENT=1) waives the Quick Movement half of this rule for both options.
#pragma once
#include <windows.h>
#include <stddef.h>

namespace ProxyPacing
{
// ---- the EXE layout: the addresses per build from the build table (ticket 4963, exebuild/symbols; RVAs from the image
// base, VA = 0x400000 + RVA). Rvas() is the layout of the build the last image check found (the build from the image's
// PE stamp and SizeOfImage): every install path checks the image first. The symbols, by field:
//   appGlobal    cam_app             g_Civ5App, .data
//   utilVftable  pac_util_vftable    the engine utility vftable, .rdata
//   secsPerTurn  pac_secs_per_turn   slot 102, getSecsPerTurn, .text
//   frameSite    pac_frame_site      the return address of its call in GameViewState::Update (after the 2-byte
//                                    "call edx": 4662 [C, L] corrects the KB), .text
//   aiTurnByte   pac_ai_turn_byte    the frame's AI-turn byte, .data
//   hold         pac_hold            the update hold counter, .data
//   gvsVftable   ses_gvs_vtbl        GameViewState's vftable, .rdata: compared, never read
//   qbIndex      pac_qb_index        queue B's index, .data
//   qbBuf        pac_qb_buf          queue B's buffers, .data: q = buf + kQbStride * index
// The structure offsets and lengths below are the same in both builds (0582 B.2).
struct ExeRvas
{
	DWORD appGlobal, utilVftable, secsPerTurn, frameSite, aiTurnByte, hold, gvsVftable, qbIndex, qbBuf;
};
const ExeRvas& Rvas();
// Fills the layout of a build from its table: 0, or 2 (an unknown build) or 7 (a symbol the table does not carry: its
// name in *missing). Install, SkInstall and DrInstall call it through the image check.
int SelectLayout(int build, const char** missing);
const DWORD kAppOffEngineUtility = 0x1460;  // [[g_Civ5App]+0x1460] = the engine utility object
const int kSlotSecsPerTurn = 102;
// The vftable's length: 260 interface slots (to VA 0x9EAB24 in build 4390913) and 4 more entries, all in .text; the entry
// after them (VA 0x9EAB38 in build 4390913) is not a code pointer, and the entry before slot 0 (the RTTI slot) is 0. [C
// 2026-11-17, read in the Steam EXE file SHA-256 b2ce96ae..., ticket 4662; checked again on the live object at every
// install. The same 264 entries and slot -1 = 0 in build 26577625: 4963, 2026-11-19.]
const int kEntries = 264;
// Option 2 (ticket 4665): the frame's accepted player. [C 2026-11-17, game-core-thread.md 6.2, 4665 notes.md]
const DWORD kAppOffGvs = 0xC0;           // [[g_Civ5App]+0xC0] = the GameViewState (heap)
const DWORD kGvsOffSeen = 0x8CC;         // GameViewState+0x8CC: the player the frame accepted last (64 = none)
const int kSlots = 64;                   // the frame scans player slots 0-63; 64 = no turn-active player
// Option 4 (ticket 4675): queue B. [C 2026-11-17, game-core-thread.md 6.2/6.3, turn-time-study.md "What the drain is made
// of"; the byte's writers: ticket 4675]
const DWORD kQbStride = 0x4C;
const int kQbIndexMax = 3;               // 0..2: the fourth buffer would overlap the flag set (VA 0x2A10F00 in 4390913)
const DWORD kQbOffFlip = 0x20;           // p: the write half at q + (p & 1) * 0x10, the dispatch half at q + ((p-1) & 1) * 0x10
const DWORD kQbOffOrder = 0x30;          // the ordering byte (1 in play)
const DWORD kQbCtl = 0x34;               // the control block read: both halves (+4 begin, +8 end, +0xC capacity), +0x20..+0x33
const DWORD kRecMin = 0x48;              // a record: +0 size, +4 type, header words from +0x10, the event id at +0x44
const DWORD kRecMax = 0x40000;
const DWORD kRecOffHold = 0x3C;          // header word 11: the hold counter
const DWORD kRecOffFlags = 0x40;         // header word 12: the flags (bit 1 blocks every later record)
const DWORD kHalfMax = 64u << 20;
const DWORD kQbOffDispatched = 0x2C;     // the dispatch offset: the dispatch half's records before it are dispatched
// Option 2 value 2 (ticket 0571, D191; 0568's variant F1's floor, a model choice, not a measured optimum; docs/OPTIONS.md).
const LONG kHoldFloorRecords = 100;      // a gap goes to the EXE's own gate only when queue B has more undispatched records

enum State
{
	kOff = 0,      // AiUpdateTickMs = -1: nothing installed, nothing read
	kPending = 1,  // on, not installed yet (no engine utility yet)
	kActive = 2,   // the vptr is ours
	kStopped = 3   // a check failed: the EXE's own pacing until the next proxy load
};

struct Config
{
	ULONG_PTR base;    // the EXE's image base (GetModuleHandle(NULL))
	void* object;      // the engine utility the EXE gave the DLL (ProxyRtInt::EngineUtility); NULL = none yet
	DWORD mainThread;  // the EXE's main thread id (0 = unknown: an install refuses)
	int tickMs;        // [Performance] AiUpdateTickMs: -1 = off, 0..100
};

// Sinks. log: one complete line (no newline). report: a failed check; boot = the first install of this proxy load
// (the report and the popup) or later (the report only); check = the short name of the check; line = the whole reason.
typedef void (*LogFn)(const char* line, bool loud);
typedef void (*ReportFn)(bool boot, const char* check, const char* line);

// Option 2's configuration and what the scan after an Update found.
struct SkipConfig
{
	ULONG_PTR base;  // the EXE's image base
	int on;          // [Performance] AiSkipVisualDrain: 0 = off, 1 = on, 2 = on, big AI gaps to the EXE's gate (ticket 0571)
	bool headless;   // vpproxy.ini host=headless: no EXE frame to pace (off with one line, never a failure)
	int drain;       // [Performance] AiDrainUnordered (option 4, ticket 4675): 0 = off, 1 = on (only with on = 1)
	bool drainAnyMovement;  // TEST only (the environment variable VP64_PACING_TEST_DRAIN_ANY_MOVEMENT=1): option 4's
	                        // Quick Movement guard is not applied, for the DIAGNOSTIC pictures of MC #4203 (a)
};

// The game settings Quick Combat and Quick Movement (D246, ticket 0718: options 2 and 4 act only with both on), asked of
// the DLL once per AI Update that reaches option 2's decision (ticket 4675 asked them only at an end-of-phase drain).
// False = they could not be asked (then neither option acts for that Update).
typedef bool (*QuickFn)(bool* quickCombat, bool* quickMovement);

struct ScanResult
{
	int first;         // the first player in slots 0-63 with IsEverAlive and IsTurnActive; kSlots when none
	int active;        // ICvGame1::GetActivePlayer
	bool activeHuman;  // that player's IsHuman (asked only when first == active)
	long calls;        // DLL interface calls the scan made
};
// The scan (the glue: through the proxy's DLL interface; the tests: their own). False = it could not run (no game).
typedef bool (*ScanFn)(ScanResult* out);

// ---- the product (proxy_pacing.cpp's engine glue) ---------------------------------------------------------------
// From ProxyRtInt::BeforeUpdate (the game-core thread, top of each ICvGame1::Update): the lazy installs and option 3's
// re-check; option 2's sample of the AI-turn byte.
void OnUpdate();
// From ProxyRtInt::AfterUpdate (ProxyRt::End of the outermost ICvGame1::Update, game-core thread, after the call and its
// epoch have ended): option 2's write. Nothing when option 2 is off.
void OnAfterUpdate();
// From DetachOnUnload (FreeLibrary, under the loader lock): the vptr back to the EXE's vftable when it is still ours,
// and the copy's slot 102 back to the EXE's function (the copy's page is left mapped, so a thread that read the old
// vptr just before still calls EXE code). Writes one line into `line` (empty when nothing was installed). Option 2 has
// nothing to restore; its counters line is logged here directly when it was on (the caller logs from the same place).
// True when something was restored.
bool OnUnload(char* line, size_t cap);

// ---- the core (no engine; proxy/tools/rt_tests/pacing_unit.cpp) ------------------------------------------------
void SetSinks(LogFn log, ReportFn report);
// One step: off = nothing at all; pending = the install (its failure is a boot failure); active = the re-check (the
// app's pointer, the object's vptr), a new object (restore the old one, install on the new one), a vptr that is the
// EXE's own again (installed again); stopped = nothing.
void Update(const Config& c);
// The restore of OnUnload (also used by the tests). why names the occasion in the line.
bool Restore(const char* why, char* line, size_t cap);
// NULL when the image at base is the EXE build the layout above was read from, else the reason (guarded reads).
const char* VerifyExe(ULONG_PTR base);
State CurrentState();
// "state active, site calls N (shortened N, stock N, other thread N), other callers N, checks N, installs N"
void CountersLine(char* out, size_t cap);

// ---- option 2 (ticket 4665) -------------------------------------------------------------------------------------
// Top of each Update: off = nothing at all (decided at the first call of the load); pending = the install (its failure
// is a boot failure); active = the AI-turn byte's sample for this Update; stopped = nothing.
void SkipBefore(const SkipConfig& c);
// After each outermost Update: active and the byte was 1 at the start = the hold, the re-check (a play failure), the
// scan, the conditions, the D246 read of quick (Quick Combat and Quick Movement both on, else no write), the write.
// Anything else: nothing (no read). No turn-active player and option 4 active = option 4's guards (the same D246 read)
// and, when they hold, the ordering byte cleared. quick NULL counts as settings not answered (no write, no clear).
void SkipAfter(ScanFn scan, QuickFn quick = NULL);
State SkipState();
// "option 2 counters: state active, Updates N (AI turn N), scans N (DLL calls N, N us, max N us), writes N (already
// set N), not written: human turn N, hold N, no player N, the active human N, scan failed N, animations on N (Quick
// Combat off N, Quick Movement off N, settings unknown N); checks N; quick reads N (changes N, N us, max N us)"
void SkipCountersLine(char* out, size_t cap);

// ---- option 4 (ticket 4675) -------------------------------------------------------------------------------------
State DrainState();
// True while the proxy holds queue B's ordering byte at 0 (from the end-of-phase clear to the next Update's top).
bool DrainArmed();
// The byte set back to 1 when the proxy holds it at 0 (the unload; also used by the tests). One line into `line` (empty
// when nothing was held). True when it was written back.
bool DrainRestore(const char* why, char* line, size_t cap);
// "option 4 counters: state active, end-of-phase drains N, cleared N, set back N, not cleared: Quick Combat off N, Quick
// Movement off N, settings unknown N, held N, flag bit 1 N, queue changing N; records walked N (max N), N us (max N us)"
void DrainCountersLine(char* out, size_t cap);

// ---- option 2 value 2 (ticket 0571) -------------------------------------------------------------------------------
// "option 2 value 2 counters: gaps N (to the EXE's gate N; written: at most 100 records N, queue changing N);
// undispatched records at a gap max N (all gaps N); walks N us (max N us); option 4 at gated gaps: cleared N, not
// cleared: settings N, held N, flag bit 1 N"
void HoldCountersLine(char* out, size_t cap);

namespace Test
{
// Forgets everything as a new proxy load would (nothing is restored; the copy's page stays as it is).
void Reset();
// The frame's call site as the replacement compares it (the tests call from their own site).
void SetSite(ULONG_PTR site);
// The return address the replacement saw last (any caller).
ULONG_PTR LastReturnAddress();
// The copy's vptr value (the address of its slot 0), 0 when none.
ULONG_PTR CopyVptr();
long SiteCalls();
long Shortened();
long SiteStock();
long SiteOtherThread();
long OtherCallers();
long Checks();
long Installs();
// Option 2's counters.
long SkipWrites();
long SkipScans();
long SkipChecks();
long SkipNotAiTurn();
long SkipHeld();
long SkipNoPlayer();
long SkipHuman();
long SkipAnimations();    // D246 (ticket 0718): not written because Quick Combat or Quick Movement was off or unknown
long QuickReads();        // D246: the reads of the two settings (one per AI Update that reached a decision)
long QuickChanges();      // D246: the logged changes of the read state (the first read counts)
// Option 4's counters.
long DrainEnds();
long DrainCleared();
long DrainSetBack();
long DrainNotQuick();     // Quick Combat or Quick Movement off, or the settings could not be asked
long DrainHeld();         // a held record in the dispatch half
long DrainBit1();         // a record with flag bit 1
long DrainChanging();     // the queue changed or was being written during every walk
// The ordering byte's address the proxy cleared last (0 when none).
ULONG_PTR DrainByteAddress();
// Option 2 value 2's counters (ticket 0571).
long HoldGaps();        // gaps seen (option 2's scan found the next AI player)
long HoldGated();       // gaps left to the EXE's gate (+0x8CC not written)
long HoldSmall();       // written: at most kHoldFloorRecords undispatched records
long HoldChanging();    // written: the queue changed during every walk
LONGLONG HoldLastRecords();  // the undispatched records counted at the last gap
long DrainGapCleared();      // option 4's clears at gated gaps
long DrainGapRefused();      // option 4's guards refusing at gated gaps (settings, held, flag bit 1)
}  // namespace Test
}  // namespace ProxyPacing
