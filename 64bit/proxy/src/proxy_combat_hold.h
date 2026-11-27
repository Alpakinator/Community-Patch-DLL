// proxy_combat_hold.h - route S (D210): the proxy holds the EXE's RunCombatSim record of a visualised fight until the
// server's OwnerEnd for that fight (ticket 4773, epic 0042). Hand-written. SPDX-License-Identifier: GPL-3.0-or-later
//
// What it does (written from the knowledge base's description of the EXE's behaviour, D83:
// research/05-graphics-composition/combat-choreography.md 3.2-3.5, "the hold as built" in 3.5.7):
//   - the take: right after the EXE served a carried ICvEngineUtility{1..4}::GameplayUnitCombat / GameplayCityCombat
//     (proxy_rt.cpp OnCall, on the thread that served it: the main thread for the human's orders, the game-core thread
//     in the AI's Update), the record of the returned event id (type 0x59) is looked for in queue B's write half under
//     the queue's own critical section and, when its visualise byte is set, its hold counter (header word 11) is raised
//     by one with an interlocked increment. The EXE's move and dispatch go on as usual; the collection keeps the record
//     while the counter is not 0, so everything that shares its keys (EndCombatSim, the unit's damage, death, advance and
//     later fights; every record in the AI turns, where the record is a barrier) waits behind it, as behind the EXE's own
//     simulation;
//   - the drop: on the EXE's main thread, from the Lua bridge's frame hook (proxy_ui_bridge_rt.cpp Init), at the first
//     frame after the server's OwnerEnd for that event id (OwnerEnd below; until ticket 4795's stream delivers it, a test
//     seam), or by the watchdog at the fight limit plus 2 s: the record is found again (the dispatch half, else the write
//     half under the critical section), checked against what the take saw, and its counter lowered by one, never below 0;
//   - nothing else: no EXE function is called, no code byte is read or written; queue B's two .data names come from the
//     build table (exebuild/symbols/proxy.tsv: pac_qb_index, pac_qb_buf; ProxyExe::Addr), the offsets below are layout.
//
// When it is on: only when route S's server fights are on (SetServerFights(true): ticket 4782/4795 calls it when the
// server plays fights; or the TEST ONLY environment variable VP64_TEST_COMBAT_HOLD of the EXE's process, read in Init).
// Off (the product today, Combat on, D147): nothing is read or written, AfterServed returns at its first test.
//
// Parts: the core (the take, the table of holds, the drop, the watchdog, the checks) works on a Target of plain
// addresses and is tested on its own; the binding (TargetOf) is the only part that reads the build table (proxy_exe.h);
// the glue (AfterServed, Init, the report) is the only part that needs the proxy runtime.
//
// Fail loud (D118, MC #7790 condition 2, lead-0590 #7786): the layout below is verified on builds 4390913 and
// 26577625 (ticket 0618) (kLayoutVerified in the .cpp, named per build). On an unknown build, a build without a verified layout, a symbol the
// build table does not carry, or a failed sanity check of the queue or a record (each offset written through is checked
// at run time: the critical section's shape before it is entered, the counter's value before each change, the record's
// type, size, visualise byte and keys), the hold is off for the rest of this proxy load with one LOUD line, the D118
// report is written once, and Arm / SetServerFights answer false so that the server starts no fight of its own: the
// EXE then shows every fight as with Combat off (RunCombatSim and EndCombatSim one frame apart), the identical game.
//
// Toolchain: clang-cl -m32 with VC9 headers (the proxy's), C++03; offline test proxy/tests/combat_hold/ (the same and
// MSVC /fsanitize=address) with VP_COMBAT_HOLD_OFFLINE defined (no proxy runtime: the sinks are the test's; with
// VP_COMBAT_HOLD_BINDING also the binding, against the build table).
#pragma once
#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include "vp_exe_build.h"  // the builds' identities (no address)

namespace ProxyCombatHold
{
// ---- the layout (combat-choreography.md 3.1-3.5 [C], 3.5.6 [L]; verified on 4390913 and 26577625, 3.5.9) ---------
const DWORD kQbStride = 0x4C;         // queue B: q = buffers + kQbStride * index
const LONG kQbIndexMax = 3;           // the index is 0..2 (proxy_pacing.h)
const DWORD kQbOffFlip = 0x20;        // p: the write half at q + (p & 1) * 0x10, the dispatch half at q + ((p - 1) & 1) * 0x10
const DWORD kHalfOffBegin = 0x4;      // a half: +4 begin, +8 end (pointers)
const DWORD kHalfOffEnd = 0x8;
const DWORD kQbOffCs = 0x34;          // the queue's critical section (the posters and the move hold it); 0x34 + 0x18 = 0x4C
const DWORD kRecOffSize = 0x0;        // a record: +0 size, +4 type, header words from +0x10, payload from +0x44
const DWORD kRecOffType = 0x4;
const DWORD kRecOffKeys = 0x10;       // header words 0-9: the plots and the three unit keys (3.3)
const int kRecKeyWords = 10;
const DWORD kRecOffHold = 0x3C;       // header word 11: the hold counter
const DWORD kRecOffId = 0x44;         // the payload's first word: the event id
const DWORD kRecOffVisualise = 0x44 + 0x500;  // the payload's visualise byte (GetVisualizeCombat)
const DWORD kRecTypeRun = 0x59;       // RunCombatSim
const DWORD kRecSizeRun = 0x560;      // its record size
const DWORD kRecMin = 0x48;           // the walk's bounds for any record (proxy_pacing.h)
const DWORD kRecMax = 0x40000;
const DWORD kHalfMax = 64u << 20;
// ---- the watchdog (note 3.1, 1.5 rule 3) -----------------------------------------------------------------------
const DWORD kLimitMs = 12000;     // the EXE's fight limit
const DWORD kLimitAirMs = 20000;  // ... when an air figure takes part
const DWORD kGraceMs = 2000;      // the watchdog drops a hold at the limit plus this
const int kMaxHolds = 64;         // holds at once (one per visualised fight still playing; stock plays one per unit)

enum State
{
	kNotArmed = 0,  // route S's fights are off: nothing is read or written (the product today)
	kActive = 1,    // armed, the build and its layout checked: holds are taken
	kRefused = 2    // a check failed: no new hold in this proxy load (holds already taken are still dropped)
};

// Sinks. log: one complete line without a newline; loud = a LOUD line. report: the D118 report of a refusal, called
// at most once per proxy load; check = the short name of the failed check, line = the whole reason.
typedef void (*LogFn)(const char* line, bool loud);
typedef void (*ReportFn)(const char* check, const char* line);
// The clock (milliseconds, wraps): GetTickCount in the product, the test's own in the tests.
typedef DWORD (*ClockFn)();

void SetSinks(LogFn log, ReportFn report);
// Ticket 4783 (route S arms as one, proxy_route_s.h): the report alone (the common state takes the hold's reason into
// its one report), and the watchdog's sink: called on the main thread for each hold the watchdog drops.
void SetReportSink(ReportFn report);
// afterMs: since the take; started: the server had started the fight (its per-fight bound fired), else the outer bound.
typedef void (*WatchdogFn)(uint32_t eventId, DWORD afterMs, bool started);
void SetWatchdogSink(WatchdogFn watchdog);

// ---- arming ---------------------------------------------------------------------------------------------------------
// What the hold needs of the running EXE, resolved by the binding (TargetOf, through the build table) or given by a
// test. The core never includes the build table: lead-0042 #7806 (2).
struct Target
{
	int build;               // kVpExeBuild* of vp_exe_build.h, or kVpExeBuildUnknown
	uint32_t stamp;          // the image header's TimeDateStamp and SizeOfImage (named in the unknown build's line)
	uint32_t sizeOfImage;
	ULONG_PTR qbIndex;       // queue B's index in this process (build table: pac_qb_index); 0 = not carried
	ULONG_PTR qbBuf;         // queue B's buffers (pac_qb_buf); 0 = not carried
	const char* notCarried;  // the binding's line when a symbol is not carried (NULL or "" = none)
};
// Route S's server fights on (ticket 4782/4795: the server tells the proxy that it plays fights): the target is checked
// (a known build, its layout verified, both symbols carried, the queue's index and buffers readable). False when the
// hold cannot work here (one LOUD line, the report): the server must then play no fight (D118). Any thread.
bool Arm(const Target& t);
// Route S's server fights off: no new hold (holds already taken are still dropped). Any thread.
void Disarm();
// The builds whose layout is verified (kLayoutVerified in the .cpp): the evidence, or NULL.
const char* LayoutVerified(int build);
#if !defined(VP_COMBAT_HOLD_OFFLINE) || defined(VP_COMBAT_HOLD_BINDING)
// ---- the binding (needs the build table: proxy_exe.h, epic 0590 stage 2) ------------------------------------------------
// The target of the image at base: its build from the PE header, queue B's two names through ProxyExe::Addr.
Target TargetOf(ULONG_PTR base);
// The product's switch: on = Arm(TargetOf(the EXE's image)), off = Disarm(). The answer of Arm (true when off).
bool SetServerFights(bool on);
#endif
// TEST ONLY seam until ticket 4795 delivers OwnerEnd: each hold taken gets an OwnerEnd endMs after its take (0 = none:
// only the watchdog drops it). Set before the first take.
void SetTestEndMs(DWORD endMs);
// The watchdog's limit for every hold, in ms before the grace (kLimitAirMs: see the .cpp, "the watchdog's limit").
void SetLimitMs(DWORD limitMs);
State CurrentState();

// ---- the take, the end, the drop -------------------------------------------------------------------------------------
// After the EXE served a carried combat call (the thread that served it): eventId = the event id the EXE returned.
void AfterCombatCall(uint32_t eventId);
// The server's end of the fight eventId (any thread): its hold is dropped at the main thread's next frame. played =
// false: the server could not play this fight (a LOUD count; the hold is dropped all the same). An id without a hold
// is counted and logged, nothing else.
void OwnerEnd(uint32_t eventId, bool played);
// Ticket 4783: route S disarmed as one: every hold held now is dropped at the main thread's next frame (logged as
// released, counted), whatever its OwnerEnd. Any thread.
void ReleaseAll();
// Ticket 4783 (W24 #8726, MC #8731): the server started the fight eventId (the sound executor saw its first record in
// the trigger stream). Its watchdog then runs from now: the fight's limit plus the grace (22 s). Before that, a hold's
// bound is the OUTER one from its take: (limit + grace) x (1 + its queue position, the holds still held and not ended
// at its take), so a burst of fights the server plays one after another is not cut by the watchdog while it waits, and
// a fight the server never starts is still bounded. Both bounds end the same way (LOUD, the watchdog sink). Any thread.
void FightStarted(uint32_t eventId);
// The EXE's main thread, once per frame: the drops whose OwnerEnd came or whose watchdog is due.
void OnFrame();
// The frame hook the bridge calls: the chained hook first (ProxyUiDraw's), then OnFrame.
void FrameHook();
void Chain(void (*existing)());

// "combat hold: state active, takes N (not visualised N, misses N, no slot N), drops N (OwnerEnd N, watchdog N, not
// played N), gone N, unknown OwnerEnd N, held now N, frames N"
void CountersLine(char* out, size_t cap);

// ---- the product glue (proxy_combat_hold.cpp, not in the offline test) ----------------------------------------------
#ifndef VP_COMBAT_HOLD_OFFLINE
// proxy_rt.cpp OnCall, after EngDispatchGenerated served `method` (handled, not failed); ret = the RETURN payload.
void AfterServed(uint32_t method, const uint8_t* ret, uint32_t retSize);
// proxy_ui_bridge_rt.cpp Init: the sinks, the TEST ONLY switch, the chain. Once per proxy load.
void Init(void (*existingFrameHook)());
#endif

namespace Test
{
void Reset();                   // forgets everything (state, holds, counters, sinks stay)
void SetClock(ClockFn clock);   // NULL = GetTickCount
long Takes();
long NotVisualised();
long TakeMisses();
long NoSlot();
long DropsEnd();
long DropsWatchdog();
long NotPlayed();
long Gone();
long UnknownEnd();
long CounterRefused();          // a drop found the counter at 0 or below: nothing written
long Reports();
long Released();                // holds dropped by ReleaseAll (4783)
long Starts();                  // FightStarted that started a hold (4783)
long UnknownStarts();           // FightStarted for an id without a hold
long DropsOuter();              // watchdog drops by the outer bound (never started); counted in DropsWatchdog too
int Held();                     // holds in the table now
}  // namespace Test
}  // namespace ProxyCombatHold
