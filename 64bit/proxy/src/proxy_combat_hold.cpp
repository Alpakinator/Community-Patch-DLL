// proxy_combat_hold.cpp - route S (D210): the proxy's hold on the EXE's RunCombatSim record (ticket 4773). See
// proxy_combat_hold.h. SPDX-License-Identifier: GPL-3.0-or-later
//
// Written from the knowledge base's description of the EXE's behaviour (D83): research/05-graphics-composition/
// combat-choreography.md 3.2-3.5 (ticket 4796's reading and live check), 3.5.7 (this file as built). No EXE function is
// called and no EXE code byte is read, compared or written (CEG, D4). Read: queue B's index and its control block (.data,
// through the build table), the two halves' records (heap), the queue's critical section's fields. Written: one record's
// hold counter, by interlocked increment and compare-exchange only. The EXE's own critical section is entered around
// every walk of the write half, as the EXE's posters and its move do.
// Toolchain: clang-cl -m32, VC9 headers (the proxy's); offline test proxy/tests/combat_hold/ (the same and MSVC
// /fsanitize=address, x86) with VP_COMBAT_HOLD_OFFLINE defined (no proxy runtime glue).
#ifndef VP_COMBAT_HOLD_OFFLINE
#include "proxy_rt_internal.h"  // first: the fork's headers set up <windows.h> their way
#include "proxy_rt_logic.h"     // ProxyLogic::Settings
#include "proxy_report.h"
#include "proxy_session.h"
#include "vp64_options.h"
#include <string>
#include <vector>
#endif
#include "proxy_combat_hold.h"
#if !defined(VP_COMBAT_HOLD_OFFLINE) || defined(VP_COMBAT_HOLD_BINDING)
#include "proxy_exe.h"  // epic 0590: the build table (pac_qb_index, pac_qb_buf); the binding only
#endif
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ProxyCombatHold
{
namespace
{
// ---- the verified layouts (MC #7790 condition 2) -------------------------------------------------------------------
// The builds on which the layout of proxy_combat_hold.h (stride, flip, the critical section, the record's size, type,
// keys, counter, event id and visualise byte) was verified. On a build not listed here the hold is off with one LOUD
// line and the report, and Arm answers false (D118). A build is added only with its own verification written
// into combat-choreography.md 3.5 (the reading on that build and a live take and drop), never by analogy.
struct LayoutRow
{
	int build;
	const char* evidence;
};
const LayoutRow kLayoutVerified[] = {
	{kVpExeBuild4390913, "ticket 4796, combat-choreography.md 3.5: read 2026-11-20 [C], live takes and drops 4/4 [L]"},
	{kVpExeBuild26577625, "ticket 0618, new-exe-build-26577625.md C.6 and combat-choreography.md 3.5.9: the 16 functions of 3.5 unchanged 2026-11-20 [C], live takes and drops 4/4 [L]"},
};

// True if [addr, addr + n) lies in committed, readable memory (VirtualQuery; no fault on any address).
bool Readable(ULONG_PTR addr, size_t n)
{
	if (addr < 0x10000 || addr + n < addr) return false;
	ULONG_PTR at = addr;
	while (at < addr + n)
	{
		MEMORY_BASIC_INFORMATION mi;
		if (VirtualQuery(reinterpret_cast<const void*>(at), &mi, sizeof(mi)) != sizeof(mi)) return false;
		if (mi.State != MEM_COMMIT || (mi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
		const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
		                       PAGE_EXECUTE_WRITECOPY;
		if ((mi.Protect & readable) == 0) return false;
		const ULONG_PTR end = reinterpret_cast<ULONG_PTR>(mi.BaseAddress) + mi.RegionSize;
		if (end <= at) return false;
		at = end;
	}
	return true;
}

const char* BuildLabel(int build)
{
	const VpExeBuildInfo* b = VpExeBuildInfoOf(build);
	return b ? b->label : "an unknown build";
}

// ---- state -----------------------------------------------------------------------------------------------------
enum Phase
{
	kFree = 0,
	kReserved = 1,  // a take is looking for its record
	kHeld = 2       // the counter is raised by us
};

struct Hold
{
	LONG phase;
	uint32_t id;
	DWORD keys[kRecKeyWords];  // header words 0-9 as the take saw them
	DWORD taken;               // the clock at the take
	DWORD due;                 // the watchdog (4783): before the start, the outer bound taken + (limit + grace) x
	                           // (position + 1); from the server's start of the fight, start + limit + grace
	DWORD testEndAt;           // TEST seam: an OwnerEnd at this time (valid when testEnd)
	bool testEnd;
	bool ended;                // the server's OwnerEnd came
	bool played;               // ... and said it played the fight
	bool released;             // ReleaseAll (4783: route S disarmed as one)
	bool started;              // 4783: the server started the fight (its first record in the trigger stream)
	DWORD startedAt;           // ... at this clock
	LONG position;             // 4783: the holds still held and not ended at its take (its place in the server's queue)
	LONG frames;               // frames seen while held
};

CRITICAL_SECTION g_cs;  // the table (never held while the EXE's critical section is entered, nor the other way round)
volatile LONG g_csInit = 0;
volatile LONG g_state = kNotArmed;
volatile LONG g_reported = 0;
LogFn g_log = NULL;
ReportFn g_report = NULL;
WatchdogFn g_watchdog = NULL;   // 4783
DWORD TickClock() { return GetTickCount(); }
ClockFn g_clock = TickClock;
ULONG_PTR g_qbIndex = 0;  // the addresses of queue B's index and buffers in this process (the build table's)
ULONG_PTR g_qbBuf = 0;
int g_build = kVpExeBuildUnknown;
DWORD g_testEndMs = 0;
// The watchdog's limit: kLimitAirMs (20 s) for every hold, so the watchdog fires at 22 s. The EXE takes 20 s when an air
// figure takes part, which it reads from the figures' weapon records (combat-choreography.md 4.2, 13.2), data the proxy
// does not read; a 12 s watchdog on a fight that the EXE (and the server) gives 20 s would drop the hold while the
// server still plays it. The server's OwnerEnd ends every hold it plays; the watchdog is only for a server or a stream
// that failed, where 22 s instead of 14 s costs the wait of one fight. SetLimitMs takes the fight's own limit once the
// server tells it (ticket 4782's fight record).
DWORD g_limitMs = kLimitAirMs;
Hold g_holds[kMaxHolds];
Hold g_drop[kMaxHolds];   // the main thread's copies of the holds it drops this frame (OnFrame only)
volatile LONG g_held = 0;
volatile LONG g_takes = 0, g_notVis = 0, g_misses = 0, g_noSlot = 0, g_dropsEnd = 0, g_dropsWatchdog = 0, g_notPlayed = 0,
              g_gone = 0, g_unknownEnd = 0, g_counterRefused = 0, g_reports = 0, g_frames = 0, g_refusedTakes = 0,
              g_released = 0, g_starts = 0, g_unknownStart = 0, g_dropsOuter = 0;
void (*g_chained)() = NULL;

void EnsureInit()
{
	if (g_csInit == 2) return;
	if (InterlockedCompareExchange(&g_csInit, 1, 0) == 0)
	{
		InitializeCriticalSection(&g_cs);
		InterlockedExchange(&g_csInit, 2);
		return;
	}
	while (g_csInit != 2) Sleep(0);
}

void Log(bool loud, const char* fmt, ...)
{
	char line[1200];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
	va_end(ap);
	if (g_log) g_log(line, loud);
}

// A failed check: no new hold in this proxy load; one LOUD line; the report once. Always false.
bool Refuse(const char* check, const char* fmt, ...)
{
	char why[700];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(why, sizeof(why), _TRUNCATE, fmt, ap);
	va_end(ap);
	InterlockedExchange(&g_state, kRefused);
	char counters[600];
	CountersLine(counters, sizeof(counters));
	char line[1400];
	_snprintf_s(line, sizeof(line), _TRUNCATE,
	            "LOUD: combat hold: OFF, check %s failed: %s; no hold is taken in this proxy load (holds already taken are "
	            "still dropped), route S's server fights must not start (D118: the error report); %s",
	            check, why, counters);
	if (g_log) g_log(line, true);
	if (InterlockedCompareExchange(&g_reported, 1, 0) == 0)
	{
		InterlockedIncrement(&g_reports);
		if (g_report) g_report(check, line);
	}
	return false;
}

// ---- guarded reads (leaf functions without stack objects: SEH and MSVC ASan, rt_tests' 4855 note) -------------------
bool ReadDword(ULONG_PTR at, DWORD* out)
{
	__try
	{
		*out = *reinterpret_cast<const volatile DWORD*>(at);
		return true;
	}
	__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		return false;
	}
}

// The critical section at `at` has the shape of an initialised one: its debug info is -1 (Windows 8 and later) or a
// readable debug record that points back at it; the recursion count is small and not negative. NULL when it has, else
// the reason. Read before every EnterCriticalSection on it (lead-0590 #7786: a run-time check of +0x34 per build).
const char* CsShape(ULONG_PTR at)
{
	if (!Readable(at, sizeof(RTL_CRITICAL_SECTION))) return "the critical section cannot be read";
	DWORD debugInfo = 0, recursion = 0;
	if (!ReadDword(at + offsetof(RTL_CRITICAL_SECTION, DebugInfo), &debugInfo) ||
	    !ReadDword(at + offsetof(RTL_CRITICAL_SECTION, RecursionCount), &recursion))
		return "the critical section cannot be read";
	if ((LONG)recursion < 0 || recursion > 0xFFFF) return "the critical section's recursion count is not plausible";
	if (debugInfo == 0xFFFFFFFFu) return NULL;
	if (debugInfo < 0x10000) return "the critical section's debug info is neither -1 nor a pointer";
	const ULONG_PTR back = debugInfo + offsetof(RTL_CRITICAL_SECTION_DEBUG, CriticalSection);
	DWORD seen = 0;
	if (!Readable(back, 4) || !ReadDword(back, &seen) || seen != at)
		return "the critical section's debug info does not point back at it";
	return NULL;
}

// The queue: q, or 0 with the reason.
ULONG_PTR Queue(const char** why)
{
	DWORD idx = 0;
	if (!Readable(g_qbIndex, 4) || !ReadDword(g_qbIndex, &idx))
	{
		*why = "queue B's index cannot be read";
		return 0;
	}
	if ((LONG)idx < 0 || (LONG)idx >= kQbIndexMax)
	{
		*why = "queue B's index is not 0..2";
		return 0;
	}
	const ULONG_PTR q = g_qbBuf + kQbStride * idx;
	if (!Readable(q, kQbStride))
	{
		*why = "queue B's control block cannot be read";
		return 0;
	}
	return q;
}

enum Found
{
	kFoundNo = 0,    // the walk ended exactly at the half's end without it
	kFoundYes = 1,
	kFoundBad = -1,  // the half or a record size is out of bounds: the layout is not what it should be
	kFoundFault = -2
};

// The walk itself (no stack objects; ASan note above).
int WalkRaw(ULONG_PTR begin, ULONG_PTR end, uint32_t id, ULONG_PTR* rec)
{
	__try
	{
		ULONG_PTR at = begin;
		while (at < end)
		{
			const DWORD size = *reinterpret_cast<const volatile DWORD*>(at + kRecOffSize);
			if (size < kRecMin || size > kRecMax || size > end - at) return kFoundBad;
			if (*reinterpret_cast<const volatile DWORD*>(at + kRecOffType) == kRecTypeRun &&
			    *reinterpret_cast<const volatile DWORD*>(at + kRecOffId) == id)
			{
				*rec = at;
				return kFoundYes;
			}
			at += size;
		}
		return at == end ? kFoundNo : kFoundBad;
	}
	__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		return kFoundFault;
	}
}

// The RunCombatSim record of `id` in one half of q (the write half: the caller holds the queue's critical section).
int FindIn(ULONG_PTR q, bool writeHalf, uint32_t id, ULONG_PTR* rec, const char** why)
{
	*rec = 0;
	DWORD flip = 0, begin = 0, end = 0;
	if (!ReadDword(q + kQbOffFlip, &flip))
	{
		*why = "queue B's flip word cannot be read";
		return kFoundFault;
	}
	const ULONG_PTR half = q + (((writeHalf ? flip : flip - 1) & 1) * 0x10);
	if (!ReadDword(half + kHalfOffBegin, &begin) || !ReadDword(half + kHalfOffEnd, &end))
	{
		*why = "a half of queue B cannot be read";
		return kFoundFault;
	}
	if (begin == end) return kFoundNo;
	if (end < begin || end - begin > kHalfMax || begin < 0x10000)
	{
		*why = "a half of queue B has begin and end out of bounds";
		return kFoundBad;
	}
	if (!Readable(begin, end - begin))
	{
		*why = "a half of queue B is not readable memory";
		return kFoundFault;
	}
	const int r = WalkRaw(begin, end, id, rec);
	if (r == kFoundBad) *why = "a record size in queue B is out of bounds (the walk does not end at the half's end)";
	if (r == kFoundFault) *why = "an access fault walking queue B";
	return r;
}

// Reads the record's checked fields. False on a fault.
bool ReadRecord(ULONG_PTR rec, DWORD* size, LONG* counter, DWORD* vis, DWORD* keys)
{
	__try
	{
		*size = *reinterpret_cast<const volatile DWORD*>(rec + kRecOffSize);
		*counter = *reinterpret_cast<const volatile LONG*>(rec + kRecOffHold);
		*vis = *reinterpret_cast<const volatile BYTE*>(rec + kRecOffVisualise);
		for (int i = 0; i < kRecKeyWords; ++i) keys[i] = reinterpret_cast<const volatile DWORD*>(rec + kRecOffKeys)[i];
		return true;
	}
	__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		return false;
	}
}

// ---- the take ------------------------------------------------------------------------------------------------------
enum TakeResult
{
	kTaken,
	kNotVisualised,
	kMiss,
	kBad  // the layout is not what it should be: refuse (why, check)
};

// Under the queue's critical section: find, check, raise. keys gets the record's key words.
TakeResult TakeInQueue(uint32_t id, DWORD* keys, const char** check, const char** why, char* detail, size_t cap)
{
	const ULONG_PTR q = Queue(why);
	if (!q)
	{
		*check = "combat-hold-queue";
		return kBad;
	}
	const char* cs = CsShape(q + kQbOffCs);
	if (cs)
	{
		*check = "combat-hold-cs";
		*why = cs;
		return kBad;
	}
	TakeResult res = kMiss;
	EnterCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(q + kQbOffCs));
	ULONG_PTR rec = 0;
	const int f = FindIn(q, true, id, &rec, why);
	if (f < 0)
	{
		*check = "combat-hold-walk";
		res = kBad;
	}
	else if (f == kFoundYes)
	{
		DWORD size = 0, vis = 0;
		LONG counter = 0;
		if (!ReadRecord(rec, &size, &counter, &vis, keys))
		{
			*check = "combat-hold-record";
			*why = "an access fault reading the record";
			res = kBad;
		}
		else if (size != kRecSizeRun)
		{
			*check = "combat-hold-record";
			*why = "the record's size is not RunCombatSim's";
			_snprintf_s(detail, cap, _TRUNCATE, " (0x%lx, not 0x%lx)", (unsigned long)size, (unsigned long)kRecSizeRun);
			res = kBad;
		}
		else if (vis > 1)
		{
			*check = "combat-hold-record";
			*why = "the record's visualise byte is neither 0 nor 1";
			_snprintf_s(detail, cap, _TRUNCATE, " (%lu)", (unsigned long)vis);
			res = kBad;
		}
		else if (counter != 0)
		{
			// A record still in the write half was posted with the counter 0 and nobody has reached it yet (3.5.1).
			*check = "combat-hold-counter";
			*why = "the record's hold counter is not 0 at the take";
			_snprintf_s(detail, cap, _TRUNCATE, " (%ld)", (long)counter);
			res = kBad;
		}
		else if (!vis)
			res = kNotVisualised;
		else
		{
			const LONG now = InterlockedIncrement(reinterpret_cast<volatile LONG*>(rec + kRecOffHold));
			res = kTaken;
			if (now != 1) _snprintf_s(detail, cap, _TRUNCATE, " (the counter went to %ld)", (long)now);
		}
	}
	LeaveCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(q + kQbOffCs));
	return res;
}

int Reserve()
{
	EnterCriticalSection(&g_cs);
	int slot = -1;
	for (int i = 0; i < kMaxHolds && slot < 0; ++i)
		if (g_holds[i].phase == kFree)
		{
			memset(&g_holds[i], 0, sizeof(g_holds[i]));
			g_holds[i].phase = kReserved;
			slot = i;
		}
	LeaveCriticalSection(&g_cs);
	return slot;
}

void Release(int slot)
{
	EnterCriticalSection(&g_cs);
	g_holds[slot].phase = kFree;
	LeaveCriticalSection(&g_cs);
}

// ---- the drop ------------------------------------------------------------------------------------------------------
enum DropResult
{
	kDropped,
	kDropGone,     // no record of this id with these keys (dropped by a load, the queue rebuilt): nothing written
	kDropCounter   // the counter was 0 or below: nothing written
};

// Lowers the counter by one, never below 0. The previous value in *before.
bool Lower(ULONG_PTR rec, LONG* before)
{
	volatile LONG* c = reinterpret_cast<volatile LONG*>(rec + kRecOffHold);
	for (int tries = 0; tries < 1000; ++tries)
	{
		const LONG v = *c;
		*before = v;
		if (v <= 0) return false;
		if (InterlockedCompareExchange(c, v - 1, v) == v) return true;
	}
	return false;
}

DropResult DropAt(ULONG_PTR rec, const Hold& h, LONG* before, const char** why)
{
	DWORD size = 0, vis = 0, keys[kRecKeyWords];
	LONG counter = 0;
	if (!ReadRecord(rec, &size, &counter, &vis, keys))
	{
		*why = "an access fault reading the record";
		return kDropGone;
	}
	if (size != kRecSizeRun || memcmp(keys, h.keys, sizeof(keys)) != 0)
	{
		*why = "a record of this event id with other keys (not the one held)";
		return kDropGone;
	}
	if (!Lower(rec, before))
	{
		*why = "the hold counter is 0 or below";
		return kDropCounter;
	}
	return kDropped;
}

// The main thread: the dispatch half first (only the main thread's pump changes it, so no lock), then the write half
// under the queue's critical section (a drop before the record's first move).
DropResult Drop(const Hold& h, LONG* before, const char** where, const char** why)
{
	*where = "-";
	*why = "";
	const ULONG_PTR q = Queue(why);
	if (!q) return kDropGone;
	ULONG_PTR rec = 0;
	int f = FindIn(q, false, h.id, &rec, why);
	if (f == kFoundYes)
	{
		*where = "dispatch half";
		return DropAt(rec, h, before, why);
	}
	const char* cs = CsShape(q + kQbOffCs);
	if (cs)
	{
		*why = cs;
		return kDropGone;
	}
	DropResult r = kDropGone;
	EnterCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(q + kQbOffCs));
	f = FindIn(q, true, h.id, &rec, why);
	if (f == kFoundYes)
	{
		*where = "write half";
		r = DropAt(rec, h, before, why);
	}
	LeaveCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(q + kQbOffCs));
	if (f == kFoundNo && !**why) *why = "no RunCombatSim record of this event id in either half";
	return r;
}

bool ArmChecked(const Target& t)
{
	if (t.build == kVpExeBuildUnknown)
		return Refuse("combat-hold-build", "not an EXE build the patch knows (PE timestamp 0x%08lX, SizeOfImage 0x%08lX)",
		              (unsigned long)t.stamp, (unsigned long)t.sizeOfImage);
	const char* evidence = LayoutVerified(t.build);
	if (!evidence)
		return Refuse("combat-hold-layout", "queue B's and the combat record's layout is not verified on %s "
		                                    "(MC #7790 condition 2)", BuildLabel(t.build));
	if (t.notCarried && *t.notCarried) return Refuse("combat-hold-symbol", "%s", t.notCarried);
	if (!t.qbIndex || !t.qbBuf)
		return Refuse("combat-hold-symbol", "queue B's index (pac_qb_index) or buffers (pac_qb_buf) are not carried to %s",
		              BuildLabel(t.build));
	// No address goes into the log (D202: a log may be copied into evidence; the names are enough).
	if (!Readable(t.qbIndex, 4) || !Readable(t.qbBuf, kQbStride * kQbIndexMax))
		return Refuse("combat-hold-queue", "queue B's index (pac_qb_index) or buffers (pac_qb_buf) are not readable memory");
	g_qbIndex = t.qbIndex;
	g_qbBuf = t.qbBuf;
	g_build = t.build;
	InterlockedExchange(&g_state, kActive);
	Log(false, "combat hold: on (%s; layout verified: %s): a visualised RunCombatSim record is held from the carried combat "
	           "call until the server's OwnerEnd, or the watchdog at %lu ms + %lu ms%s",
	    BuildLabel(t.build), evidence, (unsigned long)g_limitMs, (unsigned long)kGraceMs,
	    g_testEndMs ? "; TEST: an OwnerEnd after each take (VP64_TEST_COMBAT_HOLD)" : "");
	return true;
}
}  // namespace

// ---- the interface ---------------------------------------------------------------------------------------------------
void SetSinks(LogFn log, ReportFn report)
{
	g_log = log;
	g_report = report;
}

void SetReportSink(ReportFn report) { g_report = report; }

void SetWatchdogSink(WatchdogFn watchdog) { g_watchdog = watchdog; }

const char* LayoutVerified(int build)
{
	for (size_t i = 0; i < sizeof(kLayoutVerified) / sizeof(kLayoutVerified[0]); ++i)
		if (kLayoutVerified[i].build == build) return kLayoutVerified[i].evidence;
	return NULL;
}

bool Arm(const Target& t)
{
	EnsureInit();
	if (g_state == kRefused) return false;
	if (g_state == kActive) return true;
	return ArmChecked(t);
}

void Disarm()
{
	EnsureInit();
	if (InterlockedCompareExchange(&g_state, kNotArmed, kActive) == kActive)
		Log(false, "combat hold: off (route S's server fights off): no new hold; %ld held now", (long)g_held);
}

#if !defined(VP_COMBAT_HOLD_OFFLINE) || defined(VP_COMBAT_HOLD_BINDING)
// ---- the binding: the only reader of the build table ----------------------------------------------------------------
Target TargetOf(ULONG_PTR base)
{
	static char s_line[300];  // the not-carried line (one binding call at a time: arming is rare)
	Target t;
	memset(&t, 0, sizeof(t));
	t.build = ProxyExe::BuildOfImage(base, &t.stamp, &t.sizeOfImage);
	if (t.build == kVpExeBuildUnknown) return t;
	t.qbIndex = ProxyExe::Addr(t.build, VPX_pac_qb_index, base);
	t.qbBuf = ProxyExe::Addr(t.build, VPX_pac_qb_buf, base);
	if (!t.qbIndex)
		t.notCarried = ProxyExe::NotCarried(s_line, sizeof(s_line), "", t.build, VPX_pac_qb_index);
	else if (!t.qbBuf)
		t.notCarried = ProxyExe::NotCarried(s_line, sizeof(s_line), "", t.build, VPX_pac_qb_buf);
	return t;
}

bool SetServerFights(bool on)
{
	if (!on)
	{
		Disarm();
		return true;
	}
	return Arm(TargetOf(reinterpret_cast<ULONG_PTR>(GetModuleHandleA(NULL))));
}
#endif

void SetTestEndMs(DWORD endMs) { g_testEndMs = endMs; }

void SetLimitMs(DWORD limitMs) { g_limitMs = limitMs; }

State CurrentState() { return static_cast<State>(g_state); }

void AfterCombatCall(uint32_t eventId)
{
	if (g_state != kActive)
	{
		if (g_state == kRefused) InterlockedIncrement(&g_refusedTakes);
		return;
	}
	EnsureInit();
	if (eventId == 0)
	{
		// 0 is no event id (the server's "owner unknown", 4782): an OwnerEnd could never name it. No hold.
		const LONG n = InterlockedIncrement(&g_misses);
		if (n <= 10 || (n % 100) == 0)
			Log(true, "LOUD: combat hold: a combat call returned the event id 0: no hold (%ld times)", (long)n);
		return;
	}
	const int slot = Reserve();
	if (slot < 0)
	{
		const LONG n = InterlockedIncrement(&g_noSlot);
		if (n <= 3 || (n % 100) == 0)
			Log(true, "LOUD: combat hold: no slot for event id %lu (%d holds at once): no hold, the fight shows as with "
			          "Combat off (%ld times)", (unsigned long)eventId, kMaxHolds, (long)n);
		return;
	}
	Hold& h = g_holds[slot];
	const char* check = "";
	const char* why = "";
	char detail[80] = "";
	const TakeResult r = TakeInQueue(eventId, h.keys, &check, &why, detail, sizeof(detail));
	if (r == kTaken)
	{
		const DWORD now = g_clock();
		EnterCriticalSection(&g_cs);
		// 4783 (W24 #8726, MC #8731): the server plays a burst of fights one after another, so a hold waits in the
		// server's queue before its fight starts. Until the start (FightStarted) its bound is the outer one, its queue
		// position (the holds still held and not ended) times one fight's bound; position 0 is the old 22 s.
		LONG position = 0;
		for (int i = 0; i < kMaxHolds; ++i)
			if (i != slot && g_holds[i].phase == kHeld && !g_holds[i].ended) ++position;
		h.id = eventId;
		h.taken = now;
		h.position = position;
		h.due = now + (g_limitMs + kGraceMs) * (DWORD)(position + 1);
		h.testEnd = g_testEndMs != 0;
		h.testEndAt = now + g_testEndMs;
		h.phase = kHeld;
		InterlockedIncrement(&g_held);
		LeaveCriticalSection(&g_cs);
		const LONG n = InterlockedIncrement(&g_takes);
		Log(false, "combat hold: take id %lu (thread %lu): held%s; %ld held now, %ld takes, queue position %ld (bound %lu ms "
		           "until the server starts it)", (unsigned long)eventId, (unsigned long)GetCurrentThreadId(), detail,
		    (long)g_held, (long)n, (long)position, (unsigned long)((g_limitMs + kGraceMs) * (DWORD)(position + 1)));
		return;
	}
	Release(slot);
	if (r == kNotVisualised)
	{
		const LONG n = InterlockedIncrement(&g_notVis);
		if (n <= 3 || (n % 100) == 0)
			Log(false, "combat hold: id %lu not visualised: no hold (%ld times)", (unsigned long)eventId, (long)n);
	}
	else if (r == kMiss)
	{
		const LONG n = InterlockedIncrement(&g_misses);
		if (n <= 10 || (n % 100) == 0)
			Log(true, "LOUD: combat hold: no RunCombatSim record of event id %lu in queue B's write half after the call: no "
			          "hold, the EXE's records of this fight pass at once (%ld times)", (unsigned long)eventId, (long)n);
	}
	else
		Refuse(check, "%s%s (at the take of event id %lu)", why, detail, (unsigned long)eventId);
}

void OwnerEnd(uint32_t eventId, bool played)
{
	EnsureInit();
	bool found = false;
	EnterCriticalSection(&g_cs);
	// Id 0 is the server's "owner unknown" (4782): it never names a hold (none is taken for id 0); the watchdog drops it.
	for (int i = 0; i < kMaxHolds && !found && eventId != 0; ++i)
		if (g_holds[i].phase == kHeld && g_holds[i].id == eventId && !g_holds[i].ended)
		{
			g_holds[i].ended = true;
			g_holds[i].played = played;
			found = true;
		}
	LeaveCriticalSection(&g_cs);
	if (!found)
	{
		const LONG n = InterlockedIncrement(&g_unknownEnd);
		if (n <= 10 || (n % 100) == 0)
			Log(false, "combat hold: OwnerEnd for event id %lu, which has no hold: nothing done (%ld times)",
			    (unsigned long)eventId, (long)n);
	}
}

void FightStarted(uint32_t eventId)
{
	EnsureInit();
	if (eventId == 0) return;
	const DWORD now = g_clock();
	bool found = false, already = false;
	LONG position = 0;
	EnterCriticalSection(&g_cs);
	for (int i = 0; i < kMaxHolds && !found; ++i)
		if (g_holds[i].phase == kHeld && g_holds[i].id == eventId && !g_holds[i].ended)
		{
			found = true;
			already = g_holds[i].started;
			if (!already)
			{
				g_holds[i].started = true;
				g_holds[i].startedAt = now;
				g_holds[i].due = now + g_limitMs + kGraceMs;
				position = g_holds[i].position;
			}
		}
	LeaveCriticalSection(&g_cs);
	if (!found)
	{
		const LONG n = InterlockedIncrement(&g_unknownStart);
		if (n <= 3 || (n % 100) == 0)
			Log(false, "combat hold: the server started fight %lu, which has no hold (not visualised, or taken before route "
			           "S armed): nothing done (%ld times)", (unsigned long)eventId, (long)n);
		return;
	}
	if (already) return;
	InterlockedIncrement(&g_starts);
	Log(false, "combat hold: the server started fight %lu (queue position %ld at its take): watchdog %lu ms from now",
	    (unsigned long)eventId, (long)position, (unsigned long)(g_limitMs + kGraceMs));
}

void ReleaseAll()
{
	EnsureInit();
	int n = 0;
	EnterCriticalSection(&g_cs);
	for (int i = 0; i < kMaxHolds; ++i)
		if (g_holds[i].phase == kHeld && !g_holds[i].ended)
		{
			g_holds[i].ended = true;
			g_holds[i].played = false;
			g_holds[i].released = true;
			++n;
		}
	LeaveCriticalSection(&g_cs);
	if (n) Log(true, "LOUD: combat hold: route S disarmed as one: %d held fight(s) released at the next frame", n);
}

void OnFrame()
{
	const LONG frame = InterlockedIncrement(&g_frames);
	if (g_csInit != 2) return;
	if (g_held == 0)
	{
		if ((frame % 36000) == 0 && g_takes)
		{
			char c[600];
			CountersLine(c, sizeof(c));
			Log(false, "%s", c);
		}
		return;
	}
	const DWORD now = g_clock();
	int n = 0;
	EnterCriticalSection(&g_cs);
	for (int i = 0; i < kMaxHolds; ++i)
	{
		Hold& h = g_holds[i];
		if (h.phase != kHeld) continue;
		++h.frames;
		if (h.testEnd && (LONG)(now - h.testEndAt) >= 0 && !h.ended)
		{
			h.ended = true;
			h.played = true;
		}
		if (h.ended || (LONG)(now - h.due) >= 0)
		{
			g_drop[n++] = h;
			h.phase = kFree;
			InterlockedDecrement(&g_held);
		}
	}
	LeaveCriticalSection(&g_cs);
	for (int i = 0; i < n; ++i)
	{
		const Hold& h = g_drop[i];
		LONG before = 0;
		const char* where = "-";
		const char* why = "";
		const DropResult r = Drop(h, &before, &where, &why);
		const char* cause = h.released ? "route S's disarm (released)"
		                    : h.ended  ? (h.testEnd ? "OwnerEnd (TEST)" : "OwnerEnd")
		                    : h.started ? "the watchdog (started by the server, no OwnerEnd within the fight's bound)"
		                                : "the outer bound (never started by the server)";
		const DWORD after = now - h.taken;
		if (h.released)
			InterlockedIncrement(&g_released);
		else if (h.ended)
			InterlockedIncrement(&g_dropsEnd);
		else
		{
			InterlockedIncrement(&g_dropsWatchdog);
			if (!h.started) InterlockedIncrement(&g_dropsOuter);
		}
		if (h.ended && !h.played && !h.released) InterlockedIncrement(&g_notPlayed);
		if (r == kDropped)
		{
			const bool loud = !h.ended || !h.played;
			char st[96] = "";
			if (h.started)
				_snprintf_s(st, sizeof(st), _TRUNCATE, " (the server started it %lu ms after the take; queue position %ld)",
				            (unsigned long)(h.startedAt - h.taken), (long)h.position);
			else if (!h.ended)
				_snprintf_s(st, sizeof(st), _TRUNCATE, " (queue position %ld)", (long)h.position);
			Log(loud, "%scombat hold: drop id %lu by %s%s after %lu ms%s, %ld frames: %s, counter %ld -> %ld",
			    loud ? "LOUD: " : "", (unsigned long)h.id, cause,
			    h.ended && !h.played && !h.released ? " (the server could not play this fight)" : "", (unsigned long)after,
			    st, (long)h.frames, where, (long)before, (long)(before - 1));
		}
		else if (r == kDropGone)
		{
			InterlockedIncrement(&g_gone);
			Log(true, "LOUD: combat hold: drop id %lu by %s after %lu ms: the record is gone (%s): nothing written (a load "
			          "or a game exit during the fight)", (unsigned long)h.id, cause, (unsigned long)after, why);
		}
		else
		{
			InterlockedIncrement(&g_counterRefused);
			char d[120];
			_snprintf_s(d, sizeof(d), _TRUNCATE, " (%ld in the %s, event id %lu, by %s)", (long)before, where,
			            (unsigned long)h.id, cause);
			Refuse("combat-hold-counter", "%s at the drop%s: nothing written", why, d);
		}
		// 4783: the watchdog is route S's last safety net; the common state disarms as one (proxy_route_s.h)
		if (!h.ended && g_watchdog) g_watchdog(h.id, after, h.started);
	}
}

void FrameHook()
{
	void (*chained)() = g_chained;
	if (chained) chained();
	OnFrame();
}

void Chain(void (*existing)()) { g_chained = existing; }

void CountersLine(char* out, size_t cap)
{
	static const char* const kNames[] = {"not armed", "active", "refused"};
	const LONG st = g_state;
	_snprintf_s(out, cap, _TRUNCATE,
	            "combat hold: state %s, takes %ld (not visualised %ld, misses %ld, no slot %ld, refused %ld), drops %ld "
	            "(OwnerEnd %ld, watchdog %ld, released %ld, not played %ld), gone %ld, counter refused %ld, unknown OwnerEnd "
	            "%ld, held now %ld, frames %ld, started by the server %ld (without a hold %ld), outer-bound drops %ld",
	            st >= 0 && st <= 2 ? kNames[st] : "?", (long)g_takes, (long)g_notVis, (long)g_misses, (long)g_noSlot,
	            (long)g_refusedTakes, (long)(g_dropsEnd + g_dropsWatchdog + g_released), (long)g_dropsEnd,
	            (long)g_dropsWatchdog, (long)g_released, (long)g_notPlayed, (long)g_gone, (long)g_counterRefused,
	            (long)g_unknownEnd, (long)g_held, (long)g_frames, (long)g_starts, (long)g_unknownStart, (long)g_dropsOuter);
}

namespace Test
{
void Reset()
{
	EnsureInit();
	EnterCriticalSection(&g_cs);
	memset(g_holds, 0, sizeof(g_holds));
	LeaveCriticalSection(&g_cs);
	g_state = kNotArmed;
	g_reported = 0;
	g_qbIndex = g_qbBuf = 0;
	g_build = kVpExeBuildUnknown;
	g_testEndMs = 0;
	g_limitMs = kLimitAirMs;
	g_held = 0;
	g_takes = g_notVis = g_misses = g_noSlot = g_dropsEnd = g_dropsWatchdog = g_notPlayed = g_gone = g_unknownEnd =
	    g_counterRefused = g_reports = g_frames = g_refusedTakes = g_released = g_starts = g_unknownStart = g_dropsOuter = 0;
	g_chained = NULL;
}
void SetClock(ClockFn clock) { g_clock = clock ? clock : TickClock; }
long Takes() { return g_takes; }
long NotVisualised() { return g_notVis; }
long TakeMisses() { return g_misses; }
long NoSlot() { return g_noSlot; }
long DropsEnd() { return g_dropsEnd; }
long DropsWatchdog() { return g_dropsWatchdog; }
long NotPlayed() { return g_notPlayed; }
long Gone() { return g_gone; }
long UnknownEnd() { return g_unknownEnd; }
long CounterRefused() { return g_counterRefused; }
long Reports() { return g_reports; }
long Released() { return g_released; }
long Starts() { return g_starts; }
long UnknownStarts() { return g_unknownStart; }
long DropsOuter() { return g_dropsOuter; }
int Held() { return (int)g_held; }
}  // namespace Test

// ---- the product glue --------------------------------------------------------------------------------------------------
#ifndef VP_COMBAT_HOLD_OFFLINE
}  // namespace ProxyCombatHold

const ProxyLogic::Settings& ProxyIniSettings();                  // proxy_update.cpp
void ProxyBuildIdText(HMODULE self, char* out, size_t outSize);  // proxy_buildid.cpp

namespace ProxyCombatHold
{
namespace
{
char g_reportCheck[64];
char g_reportLine[1400];

void GlueLog(const char* line, bool loud)
{
	if (loud)
		ProxyRtInt::LogErrPublic("%s", line);
	else
		ProxyRtInt::Log("%s", line);
}

HMODULE SelfModule()
{
	HMODULE self = NULL;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                   reinterpret_cast<LPCSTR>(&SelfModule), &self);
	return self;
}

// The D118 report of the non-fatal stage "combat-hold" and the helper's popup. On its own thread: a refusal can come on
// the main thread inside a Lua listener (the frame hook), where no report is written (ticket 0487's rule).
DWORD WINAPI ReportThread(LPVOID)
{
	const ProxyLogic::Settings& s = ProxyIniSettings();
	ProxyReport::Input in;
	in.stage = "combat-hold";
	in.code = g_reportCheck;
	in.details.push_back(g_reportLine);
	in.details.push_back("non-fatal (ticket 4773, D118): the proxy cannot hold the game's combat records on this EXE, so "
	                     "the server must not play its own fights; the game goes on and shows each fight's result at once");
	in.d33 = false;
	in.nonFatal = true;
	in.summary = std::string("The patch cannot time the game's combat records to the server's fights (combat-hold: ") +
	             in.code + "); the game goes on without combat animations.";
	{
		char id[1024];
		ProxyBuildIdText(SelfModule(), id, sizeof(id));
		in.patch = id;
	}
	ProxyReport::Hashed h;
	h.label = "proxy";
	{
		char p[MAX_PATH * 2] = {};
		GetModuleFileNameA(SelfModule(), p, sizeof(p));
		h.path = p;
	}
	in.hashes.push_back(h);
	if (!s.logPath.empty()) in.tails.push_back(s.logPath);
	if (s.iniFound) in.copies.push_back(s.iniPath);
	{
		wchar_t opt[1024];
		if (Vp64Opt::DefaultPath(opt, sizeof(opt) / sizeof(opt[0])) != Vp64Opt::kPathNone) in.copiesW.push_back(opt);
	}
	{
		char c[600];
		CountersLine(c, sizeof(c));
		in.session.push_back(c);
	}
	std::wstring root, dir;
	std::vector<std::string> lines;
	if (!ProxyReport::Root(&root))
		lines.push_back("report: FAILED: the Documents folder could not be found; no report");
	else
		ProxyReport::Write(in, root, &dir, &lines);
	for (size_t i = 0; i < lines.size(); ++i) ProxyRtInt::Log("combat hold: %s", lines[i].c_str());
	if (dir.empty()) dir = root;
	std::string helperLine;
	const bool started = ProxySession::StartHelper(ProxySession::Widen(s.proxyDir), L"popup", dir,
	                                               s.iniFound ? ProxySession::Widen(s.iniPath) : std::wstring(),
	                                               &helperLine);
	ProxyRtInt::Log("combat hold: %s%s; the game goes on (non-fatal)", started ? "" : "the popup FAILED to start: ",
	                ProxyReport::Scrub(helperLine).c_str());
	ProxyRtInt::LogFlush();
	return 0;
}

void GlueReport(const char* check, const char* line)
{
	strcpy_s(g_reportCheck, sizeof(g_reportCheck), check ? check : "?");
	strcpy_s(g_reportLine, sizeof(g_reportLine), line ? line : "");
	ProxyRtInt::LogFlush();
	HANDLE t = CreateThread(NULL, 0, ReportThread, NULL, 0, NULL);
	if (t)
		CloseHandle(t);
	else
		ProxyRtInt::Log("combat hold: the report's thread could not start (error %lu): no report",
		                (unsigned long)GetLastError());
}
}  // namespace

void AfterServed(uint32_t method, const uint8_t* ret, uint32_t retSize)
{
	if (g_state != kActive && g_state != kRefused) return;  // route S's fights off: nothing at all
	if (method != kM_ICvEngineUtility1_GameplayUnitCombat && method != kM_ICvEngineUtility1_GameplayCityCombat &&
	    method != kM_ICvEngineUtility2_GameplayUnitCombat && method != kM_ICvEngineUtility2_GameplayCityCombat &&
	    method != kM_ICvEngineUtility3_GameplayUnitCombat && method != kM_ICvEngineUtility3_GameplayCityCombat &&
	    method != kM_ICvEngineUtility4_GameplayUnitCombat && method != kM_ICvEngineUtility4_GameplayCityCombat)
		return;
	if (!ret || retSize < 4)
	{
		Log(true, "LOUD: combat hold: a combat call (0x%08lX) returned %lu bytes, no event id: no hold",
		    (unsigned long)method, (unsigned long)retSize);
		return;
	}
	uint32_t id = 0;
	memcpy(&id, ret, 4);
	AfterCombatCall(id);
}

void Init(void (*existingFrameHook)())
{
	EnsureInit();
	SetSinks(GlueLog, GlueReport);
	Chain(existingFrameHook);
	// TEST ONLY (ticket 4773, until 4782/4795 call SetServerFights and deliver OwnerEnd): VP64_TEST_COMBAT_HOLD=<ms> in the
	// EXE's process arms the hold at the boot as route S would, with an OwnerEnd <ms> after each take (0 = none, only the
	// watchdog). Never set by the product or the installer.
	char v[32] = "";
	const DWORD n = GetEnvironmentVariableA("VP64_TEST_COMBAT_HOLD", v, sizeof(v));
	if (n > 0 && n < sizeof(v))
	{
		char* end = NULL;
		const unsigned long ms = strtoul(v, &end, 10);
		if (!end || *end || ms > 600000)
		{
			ProxyRtInt::Log("LOUD: TEST ONLY: VP64_TEST_COMBAT_HOLD=%s is not a number of ms (0..600000): ignored", v);
			return;
		}
		ProxyRtInt::Log("LOUD: TEST ONLY: VP64_TEST_COMBAT_HOLD=%lu: the combat hold is armed as route S would arm it, %s "
		                "(ticket 4773; never set by the product)",
		                ms, ms ? "with an OwnerEnd that many ms after each take" : "with no OwnerEnd (the watchdog drops)");
		SetTestEndMs((DWORD)ms);
		SetServerFights(true);
	}
}
#endif  // VP_COMBAT_HOLD_OFFLINE
}  // namespace ProxyCombatHold
