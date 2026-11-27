// combat_hold_test.cpp - offline tests of proxy/src/proxy_combat_hold.cpp (ticket 4773), linked with it (built with
// VP_COMBAT_HOLD_OFFLINE: no proxy runtime). Built and run by test_combat_hold.py with the proxy's toolchain (clang-cl
// -m32, VC9 headers) and once under MSVC /fsanitize=address (x86). SPDX-License-Identifier: GPL-3.0-or-later
//
// A fake queue B stands in for the EXE's (combat-choreography.md 3.2-3.5, written from that description; nothing of the
// EXE is read): an index word and three control blocks of 0x4C bytes in a page of their own, each with the flip word at
// +0x20, the two halves {+4 begin, +8 end} at +0 and +0x10 and an initialised critical section at +0x34; records with
// the size, type, the 13 header words (keys 0-9, counter 11, flags 12) and the payload (event id at +0x44, the visualise
// byte at +0x544). The fake pump does what 3.5.2 says the EXE's does: the collection erases the dispatch half's records
// whose counter is 0 and compacts it (no lock), then the move appends the whole write half (under the critical section).
// Ordering (who waits behind whom) is not modelled: that is the EXE's, live in 3.5.6.
//
// Cases: not armed (nothing read); arm; the take (write half, counter 1, only when visualised); a hold survives the pump
// and is dispatched once; OwnerEnd then the drop at the next frame (dispatch half; and the write half when the record has
// not moved yet); the watchdog of a hold never ended (22 s, and 14 s with SetLimitMs(12000)) - MC #7790 condition 3; an
// OwnerEnd for an unknown id and a second OwnerEnd; the server could not play the fight; a load during a fight (the
// queue emptied; the same id again with other keys); a counter taken away by someone else (never below 0, the hold
// refused); every take-side layout check (size, visualise byte, counter not 0, a bad walk, the index, the critical
// section's shape) and every arm refusal (unknown build, a build without a verified layout - nothing read, a symbol not
// carried, unreadable memory) with one report per load; the queue's lock respected; 64 holds and the 65th; Disarm; the
// chained frame hook; three threads (posts and takes, OwnerEnds, frames and pumps) racing over 600 fights.
// With VP_COMBAT_HOLD_BINDING also the binding against a fake image header and the build table (old build only).
// Exit code 0 when every check passes, 1 otherwise; "--expect-fail" adds one check that must fail.
#include "proxy_combat_hold.h"
#ifdef VP_COMBAT_HOLD_BINDING
#include "proxy_exe.h"
#endif
#include <windows.h>
#include <process.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

namespace CH = ProxyCombatHold;

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
CRITICAL_SECTION g_logLock;
std::string g_log;
int g_loud = 0;
int g_reports = 0;
std::string g_reportCheck;
void LogSink(const char* line, bool loud)
{
	EnterCriticalSection(&g_logLock);
	g_log += line;
	g_log += "\n";
	if (loud) ++g_loud;
	if (g_verbose) printf("    log%s: %s\n", loud ? " LOUD" : "", line);
	LeaveCriticalSection(&g_logLock);
}
void ReportSink(const char* check, const char* line)
{
	++g_reports;
	g_reportCheck = check;
	(void)line;
}
bool LogHas(const char* s) { return g_log.find(s) != std::string::npos; }

volatile LONG g_now = 1000;
DWORD Clock() { return (DWORD)g_now; }

// ---- the fake queue ------------------------------------------------------------------------------------------------
const DWORD kHalfBytes = 4u << 20;
BYTE* g_page = NULL;   // the index at +0, the control blocks at +0x100
BYTE* g_halves[2] = {NULL, NULL};
LONG g_index = 1;

LONG* IndexWord() { return reinterpret_cast<LONG*>(g_page); }
BYTE* Buffers() { return g_page + 0x100; }
BYTE* Q() { return Buffers() + CH::kQbStride * g_index; }
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
	// A new page each time: a critical section is never re-initialised or zeroed in place (some cases spoil it on
	// purpose, and the old page is left as it is; a few KB per case).
	g_page = static_cast<BYTE*>(VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	g_index = 1;
	*IndexWord() = g_index;
	InitializeCriticalSection(QCs());
	Flip() = 1;  // the write half at +0x10, the dispatch half at +0
	BYTE* w = Q() + 0x10;
	BYTE* d = Q();
	*reinterpret_cast<DWORD*>(w + 4) = *reinterpret_cast<DWORD*>(w + 8) = (DWORD)(ULONG_PTR)g_halves[1];
	*reinterpret_cast<DWORD*>(d + 4) = *reinterpret_cast<DWORD*>(d + 8) = (DWORD)(ULONG_PTR)g_halves[0];
}

CH::Target GoodTarget()
{
	CH::Target t;
	memset(&t, 0, sizeof(t));
	t.build = kVpExeBuild4390913;
	t.qbIndex = (ULONG_PTR)IndexWord();
	t.qbBuf = (ULONG_PTR)Buffers();
	return t;
}

struct Keys
{
	DWORD w[10];
};
Keys MakeKeys(DWORD unit)
{
	Keys k;
	const DWORD v[10] = {100 + unit, 200 + unit, 0, unit, 1, unit + 1, 0xFFFFFFFFu, 0, 0xFFFFFFFFu, 0};
	memcpy(k.w, v, sizeof(v));
	return k;
}

// Appends a record to the write half under the queue's critical section (as the EXE's posters).
BYTE* Post(DWORD type, uint32_t id, const Keys& k, BYTE vis, DWORD size = 0, LONG counter = 0)
{
	if (!size) size = (type == 0x59 || type == 0x5A) ? CH::kRecSizeRun : 0x80;
	for (;;)
	{
		EnterCriticalSection(QCs());
		const DWORD end = End(true);
		if (end + size <= Begin(true) + kHalfBytes)
		{
			BYTE* r = reinterpret_cast<BYTE*>((ULONG_PTR)end);
			memset(r, 0, size);
			*reinterpret_cast<DWORD*>(r) = size;
			*reinterpret_cast<DWORD*>(r + 4) = type;
			memcpy(r + CH::kRecOffKeys, k.w, sizeof(k.w));
			*reinterpret_cast<LONG*>(r + CH::kRecOffHold) = counter;
			*reinterpret_cast<DWORD*>(r + 0x40) = 0x11;
			*reinterpret_cast<DWORD*>(r + CH::kRecOffId) = id;
			if (size > CH::kRecOffVisualise) r[CH::kRecOffVisualise] = vis;
			End(true) = end + size;
			LeaveCriticalSection(QCs());
			return r;
		}
		LeaveCriticalSection(QCs());
		Sleep(0);
	}
}

// The fake pump (main thread): collection, then the move. Returns how many records were dispatched (moved).
int Pump(std::vector<uint32_t>* dispatched = NULL)
{
	// collection: keep records whose counter is not 0
	BYTE* d = reinterpret_cast<BYTE*>((ULONG_PTR)Begin(false));
	BYTE* end = reinterpret_cast<BYTE*>((ULONG_PTR)End(false));
	BYTE* out = d;
	for (BYTE* r = d; r < end;)
	{
		const DWORD size = *reinterpret_cast<DWORD*>(r);
		if (*reinterpret_cast<volatile LONG*>(r + CH::kRecOffHold) != 0)
		{
			if (out != r) memmove(out, r, size);
			out += size;
		}
		r += size;
	}
	End(false) = (DWORD)(ULONG_PTR)out;
	// the move, under the critical section
	EnterCriticalSection(QCs());
	BYTE* w = reinterpret_cast<BYTE*>((ULONG_PTR)Begin(true));
	const DWORD n = End(true) - Begin(true);
	int moved = 0;
	for (BYTE* r = w; r < w + n; r += *reinterpret_cast<DWORD*>(r))
	{
		++moved;
		if (dispatched) dispatched->push_back(*reinterpret_cast<uint32_t*>(r + CH::kRecOffId));
	}
	memcpy(out, w, n);
	End(false) = (DWORD)(ULONG_PTR)(out + n);
	End(true) = Begin(true);
	LeaveCriticalSection(QCs());
	return moved;
}

// The record of `id` (type 0x59) in either half, NULL when none.
BYTE* Find(uint32_t id)
{
	for (int h = 0; h < 2; ++h)
	{
		const bool write = h == 1;
		// bounded to the half's own memory (a case spoils End on purpose; stale bytes past the records are not walked)
		BYTE* const lo = reinterpret_cast<BYTE*>((ULONG_PTR)Begin(write));
		BYTE* hi = reinterpret_cast<BYTE*>((ULONG_PTR)End(write));
		if (hi > lo + kHalfBytes) hi = lo + kHalfBytes;
		for (BYTE* r = lo; r + 8 <= hi;)
		{
			const DWORD size = *reinterpret_cast<DWORD*>(r);
			if (size < CH::kRecMin || size > (DWORD)(hi - r)) break;
			if (*reinterpret_cast<DWORD*>(r + 4) == 0x59 && *reinterpret_cast<uint32_t*>(r + CH::kRecOffId) == id) return r;
			r += size;
		}
	}
	return NULL;
}
LONG CounterOf(uint32_t id)
{
	BYTE* r = Find(id);
	return r ? *reinterpret_cast<volatile LONG*>(r + CH::kRecOffHold) : -999;
}
bool InWriteHalf(uint32_t id)
{
	BYTE* r = Find(id);
	return r && (ULONG_PTR)r >= Begin(true) && (ULONG_PTR)r < End(true);
}

void Fresh()
{
	CH::Test::Reset();
	CH::SetSinks(LogSink, ReportSink);
	CH::Test::SetClock(Clock);
	g_log.clear();
	g_loud = 0;
	g_reports = 0;
	g_reportCheck.clear();
	g_now = 1000;
	SetUpQueue();
}

// A visualised fight posted as the EXE's handler does (0x59, then 0x5A with the next id) and the take.
void Fight(uint32_t id, DWORD unit, BYTE vis = 1)
{
	Post(0x59, id, MakeKeys(unit), vis);
	Post(0x5A, id + 1, MakeKeys(unit), vis);
	CH::AfterCombatCall(id);
}

// ---- the cases -------------------------------------------------------------------------------------------------------
void CaseNotArmed()
{
	Fresh();
	Post(0x59, 11, MakeKeys(1), 1);
	CH::AfterCombatCall(11);
	CHECK(CH::CurrentState() == CH::kNotArmed);
	CHECK(CounterOf(11) == 0);
	CHECK(CH::Test::Takes() == 0 && CH::Test::TakeMisses() == 0);
	CHECK(g_log.empty());
	CH::OwnerEnd(11, true);  // nothing held: counted only
	CH::OnFrame();
	CHECK(CounterOf(11) == 0);
	CHECK(CH::Test::UnknownEnd() == 1);
}

void CaseTakeAndOwnerEnd()
{
	Fresh();
	CHECK(CH::Arm(GoodTarget()));
	CHECK(CH::CurrentState() == CH::kActive);
	CHECK(LogHas("combat hold: on (") && LogHas("layout verified"));
	CHECK(g_loud == 0 && g_reports == 0);
	CHECK(CH::Arm(GoodTarget()));  // again: nothing new
	Fight(101, 1);
	CHECK(CounterOf(101) == 1);
	CHECK(InWriteHalf(101));
	CHECK(CH::Test::Takes() == 1 && CH::Test::Held() == 1);
	// the 0x5A record of the same fight is not held
	BYTE* end = Find(101) + CH::kRecSizeRun;
	CHECK(*reinterpret_cast<DWORD*>(end + 4) == 0x5A && *reinterpret_cast<LONG*>(end + CH::kRecOffHold) == 0);
	// the pump moves and dispatches it once; the collection keeps it
	std::vector<uint32_t> disp;
	CHECK(Pump(&disp) == 2);
	CHECK(disp.size() == 2 && disp[0] == 101 && disp[1] == 102);
	CHECK(!InWriteHalf(101) && CounterOf(101) == 1);
	disp.clear();
	CHECK(Pump(&disp) == 0);
	CHECK(CounterOf(101) == 1);  // kept, not dispatched again
	// frames before the OwnerEnd: nothing
	for (int i = 0; i < 5; ++i) CH::OnFrame();
	CHECK(CounterOf(101) == 1 && CH::Test::Held() == 1);
	CH::OwnerEnd(101, true);
	CHECK(CounterOf(101) == 1);  // dropped only at the main thread's next frame
	CH::OnFrame();
	CHECK(CounterOf(101) == 0);
	CHECK(CH::Test::DropsEnd() == 1 && CH::Test::DropsWatchdog() == 0 && CH::Test::Held() == 0);
	CHECK(LogHas("drop id 101 by OwnerEnd") && LogHas("dispatch half, counter 1 -> 0"));
	Pump();
	CHECK(Find(101) == NULL);  // the collection erased it
	CHECK(g_loud == 0 && g_reports == 0);
	// a second OwnerEnd for the same fight: counted, nothing written
	CH::OwnerEnd(101, true);
	CH::OnFrame();
	CHECK(CH::Test::UnknownEnd() == 1);
	char line[600];
	CH::CountersLine(line, sizeof(line));
	CHECK(strstr(line, "state active, takes 1") != NULL && strstr(line, "drops 1 (OwnerEnd 1") != NULL);
}

void CaseDropInWriteHalf()
{
	Fresh();
	CHECK(CH::Arm(GoodTarget()));
	Fight(201, 2);
	CH::OwnerEnd(201, true);
	CH::OnFrame();  // no pump in between: the record never moved
	CHECK(CounterOf(201) == 0 && InWriteHalf(201));
	CHECK(LogHas("write half, counter 1 -> 0"));
	CHECK(CH::Test::DropsEnd() == 1 && g_loud == 0);
}

void CaseNotVisualisedAndMiss()
{
	Fresh();
	CHECK(CH::Arm(GoodTarget()));
	Fight(301, 3, 0);
	CHECK(CounterOf(301) == 0 && CH::Test::NotVisualised() == 1 && CH::Test::Takes() == 0 && CH::Test::Held() == 0);
	CH::AfterCombatCall(399);  // no record of that id
	CHECK(CH::Test::TakeMisses() == 1 && g_loud == 1 && LogHas("LOUD: combat hold: no RunCombatSim record of event id 399"));
	CHECK(CH::CurrentState() == CH::kActive && g_reports == 0);
	CH::OwnerEnd(301, true);  // a fight without a hold
	CHECK(CH::Test::UnknownEnd() == 1);
}

void CaseWatchdog()
{
	// MC #7790 condition 3: a hold that is never dropped by an OwnerEnd
	Fresh();
	CHECK(CH::Arm(GoodTarget()));
	Fight(401, 4);
	Pump();
	g_now += CH::kLimitAirMs + CH::kGraceMs - 1;
	CH::OnFrame();
	CHECK(CounterOf(401) == 1 && CH::Test::Held() == 1);
	g_now += 1;
	CH::OnFrame();
	CHECK(CounterOf(401) == 0 && CH::Test::Held() == 0);
	CHECK(CH::Test::DropsWatchdog() == 1 && CH::Test::DropsEnd() == 0);
	CHECK(LogHas("LOUD: combat hold: drop id 401 by the outer bound (never started by the server) after 22000 ms (queue position 0)") && g_loud == 1);   // 4783: the outer bound at position 0 = the old 22 s
	CH::OwnerEnd(401, true);  // the server's end after the watchdog: nothing more
	CH::OnFrame();
	CHECK(CounterOf(401) == 0 && CH::Test::UnknownEnd() == 1);
	// the 12 s limit when it is given; the clock wraps
	Fresh();
	CH::SetLimitMs(CH::kLimitMs);
	CHECK(CH::Arm(GoodTarget()));
	g_now = (LONG)0xFFFFF000u;
	Fight(402, 5);
	g_now = (LONG)(0xFFFFF000u + CH::kLimitMs + CH::kGraceMs - 1);
	CH::OnFrame();
	CHECK(CounterOf(402) == 1);
	g_now = (LONG)(0xFFFFF000u + CH::kLimitMs + CH::kGraceMs);
	CH::OnFrame();
	CHECK(CounterOf(402) == 0 && CH::Test::DropsWatchdog() == 1);
	// the TEST seam: an OwnerEnd endMs after the take
	Fresh();
	CH::SetTestEndMs(5000);
	CHECK(CH::Arm(GoodTarget()));
	CHECK(LogHas("TEST: an OwnerEnd after each take"));
	Fight(403, 6);
	g_now += 4999;
	CH::OnFrame();
	CHECK(CounterOf(403) == 1);
	g_now += 1;
	CH::OnFrame();
	CHECK(CounterOf(403) == 0 && CH::Test::DropsEnd() == 1 && LogHas("by OwnerEnd (TEST) after 5000 ms"));
	// lead-0042 #7862: the server's owner 0 ("owner unknown", 4782) never drops by id; the watchdog does. A call that
	// returned the id 0 takes no hold.
	Fresh();
	CHECK(CH::Arm(GoodTarget()));
	Fight(405, 7);
	CH::OwnerEnd(0, true);
	CH::OnFrame();
	CHECK(CounterOf(405) == 1 && CH::Test::UnknownEnd() == 1 && CH::Test::DropsEnd() == 0);
	g_now += CH::kLimitAirMs + CH::kGraceMs;
	CH::OnFrame();
	CHECK(CounterOf(405) == 0 && CH::Test::DropsWatchdog() == 1);
	Post(0x59, 0, MakeKeys(8), 1);
	CH::AfterCombatCall(0);
	CHECK(CounterOf(0) == 0 && CH::Test::Takes() == 1 && CH::Test::TakeMisses() == 1 &&
	      LogHas("returned the event id 0: no hold") && CH::CurrentState() == CH::kActive);
	// a Reset on the server comes as one OwnerEnd per fight (played = false): every hold dropped, no drop-all call
	Fresh();
	CHECK(CH::Arm(GoodTarget()));
	for (int i = 0; i < 5; ++i) Fight(420 + 2 * i, 20 + i);
	Pump();
	for (int i = 0; i < 5; ++i) CH::OwnerEnd(420 + 2 * i, false);
	CH::OnFrame();
	int left = 0;
	for (int i = 0; i < 5; ++i) left += CounterOf(420 + 2 * i);
	CHECK(left == 0 && CH::Test::NotPlayed() == 5 && CH::Test::Held() == 0);
}

void CaseNotPlayed()
{
	Fresh();
	CHECK(CH::Arm(GoodTarget()));
	Fight(501, 7);
	Pump();
	CH::OwnerEnd(501, false);
	CH::OnFrame();
	CHECK(CounterOf(501) == 0 && CH::Test::NotPlayed() == 1 && CH::Test::DropsEnd() == 1);
	CHECK(LogHas("LOUD: combat hold: drop id 501 by OwnerEnd (the server could not play this fight)") && g_loud == 1);
	CHECK(CH::CurrentState() == CH::kActive);
}

// Ticket 4783 (W24 #8726, MC #8731): a burst of 9 fights taken in one frame and played by the server one after another
// (4.5 s each, 40.5 s in all) loses none to the watchdog: each one's 22 s runs from FightStarted; a fight never
// started is bounded by its outer bound (22 s x (position + 1)); a started fight whose OwnerEnd is lost drops at 22 s
// after its start.
void CaseBurst()
{
	Fresh();
	CHECK(CH::Arm(GoodTarget()));
	for (uint32_t k = 0; k < 9; ++k) Fight(1001 + 2 * k, 60 + k);
	Pump();
	CHECK(CH::Test::Held() == 9 && LogHas("queue position 8 (bound 198000 ms until the server starts it)"));
	for (uint32_t k = 0; k < 9; ++k)
	{
		CH::FightStarted(1001 + 2 * k);
		for (int f = 0; f < 45; ++f)
		{
			g_now += 100;
			CH::OnFrame();
		}
		CH::OwnerEnd(1001 + 2 * k, true);
		CH::OnFrame();
	}
	CHECK(CH::Test::DropsEnd() == 9 && CH::Test::DropsWatchdog() == 0 && CH::Test::Starts() == 9 && CH::Test::Held() == 0);
	CH::FightStarted(1001);   // a start for an id that has no hold any more: counted, nothing else
	CHECK(CH::Test::UnknownStarts() == 1);
	// the 5th never started (lost by the server); the others play: its outer bound (5 x 22 s from the take) drops it
	Fresh();
	CHECK(CH::Arm(GoodTarget()));
	for (uint32_t k = 0; k < 9; ++k) Fight(2001 + 2 * k, 70 + k);
	Pump();
	const LONG t0 = g_now;
	for (uint32_t k = 0; k < 9; ++k)
	{
		if (k == 4) continue;
		CH::FightStarted(2001 + 2 * k);
		g_now += 4500;
		CH::OnFrame();
		CH::OwnerEnd(2001 + 2 * k, true);
		CH::OnFrame();
	}
	CHECK(CH::Test::DropsEnd() == 8 && CH::Test::DropsWatchdog() == 0 && CH::Test::Held() == 1);
	g_now = t0 + 5 * (CH::kLimitAirMs + CH::kGraceMs) - 1;
	CH::OnFrame();
	CHECK(CH::Test::Held() == 1);
	g_now += 1;
	CH::OnFrame();
	CHECK(CH::Test::Held() == 0 && CH::Test::DropsWatchdog() == 1 && CH::Test::DropsOuter() == 1);
	CHECK(LogHas("LOUD: combat hold: drop id 2009 by the outer bound (never started by the server) after 110000 ms (queue "
	             "position 4)"));
	// a started fight whose OwnerEnd is lost: 22 s after its start, not after its take
	Fresh();
	CHECK(CH::Arm(GoodTarget()));
	Fight(3001, 80);
	Fight(3003, 81);
	Pump();
	g_now += 10000;
	CH::FightStarted(3003);
	g_now += CH::kLimitAirMs + CH::kGraceMs - 1;
	CH::OnFrame();
	CHECK(CH::Test::DropsWatchdog() == 1 && CH::Test::Held() == 1);   // 3001 never started: its outer bound (22 s) fired
	g_now += 1;
	CH::OnFrame();
	CHECK(CH::Test::DropsWatchdog() == 2 && CH::Test::DropsOuter() == 1 && CH::Test::Held() == 0);
	CHECK(LogHas("LOUD: combat hold: drop id 3003 by the watchdog (started by the server, no OwnerEnd within the fight's "
	             "bound) after 32000 ms (the server started it 10000 ms after the take; queue position 1)"));
}

void CaseLoadDuringFight()
{
	Fresh();
	CHECK(CH::Arm(GoodTarget()));
	Fight(601, 8);
	Fight(603, 9);
	Pump();
	// a load: the EXE's queue starts again empty; a new game's fight gets the same id 601 for other units
	End(false) = Begin(false);
	End(true) = Begin(true);
	Post(0x59, 601, MakeKeys(50), 1);
	CH::OwnerEnd(601, true);
	CH::OnFrame();
	CHECK(CounterOf(601) == 0);  // not ours: untouched
	CHECK(CH::Test::Gone() == 1 && LogHas("the record is gone (a record of this event id with other keys"));
	// the other one's record is gone altogether: the watchdog finds nothing (603 was taken second: queue position 1, its
	// outer bound two fights long, 4783)
	g_now += 2 * (CH::kLimitAirMs + CH::kGraceMs);
	CH::OnFrame();
	CHECK(CH::Test::Gone() == 2 && CH::Test::Held() == 0 && LogHas("no RunCombatSim record of this event id in either half"));
	CHECK(CH::CurrentState() == CH::kActive && g_reports == 0);
}

void CaseCounterTakenAway()
{
	Fresh();
	CHECK(CH::Arm(GoodTarget()));
	Fight(701, 10);
	Fight(703, 11);
	Pump();
	*reinterpret_cast<volatile LONG*>(Find(701) + CH::kRecOffHold) = 0;  // someone else dropped our hold
	CH::OwnerEnd(701, true);
	CH::OnFrame();
	CHECK(CounterOf(701) == 0);  // never below 0
	CHECK(CH::Test::CounterRefused() == 1 && CH::CurrentState() == CH::kRefused);
	CHECK(g_reports == 1 && g_reportCheck == "combat-hold-counter");
	CHECK(LogHas("LOUD: combat hold: OFF, check combat-hold-counter failed: the hold counter is 0 or below at the drop"));
	// refused: no new hold; the hold already taken is still dropped
	Fight(705, 12);
	CHECK(CounterOf(705) == 0 && CH::Test::Takes() == 2);
	CH::OwnerEnd(703, true);
	CH::OnFrame();
	CHECK(CounterOf(703) == 0 && CH::Test::DropsEnd() == 2);
	CHECK(!CH::Arm(GoodTarget()));  // refused for the rest of the load
	CHECK(g_reports == 1);
}

// One take-side check: the take refuses, nothing is written, one LOUD line, one report.
void TakeRefused(const char* name, const char* check, void (*spoil)(), uint32_t id, int line)
{
	Fresh();
	CHECK(CH::Arm(GoodTarget()));
	spoil();
	const LONG before = CounterOf(id);
	CH::AfterCombatCall(id);
	const bool ok = CH::CurrentState() == CH::kRefused && g_reports == 1 && g_reportCheck == check && g_loud == 1 &&
	                CounterOf(id) == before && CH::Test::Takes() == 0 && CH::Test::Held() == 0;
	if (!ok)
		printf("  (%s: state %d, reports %d '%s', loud %d, counter %ld -> %ld)\n", name, (int)CH::CurrentState(), g_reports,
		       g_reportCheck.c_str(), g_loud, (long)before, (long)CounterOf(id));
	Check(ok, name, line);
	// later fights are not held, and nothing more is reported (on a sound queue again)
	if (g_verbose) printf("    step: %s: a sound queue again\n", name);
	SetUpQueue();
	if (g_verbose) printf("    step: %s: post\n", name);
	Post(0x59, 999, MakeKeys(99), 1);
	if (g_verbose) printf("    step: %s: take\n", name);
	CH::AfterCombatCall(999);
	Check(CounterOf(999) == 0 && g_reports == 1, name, line);
	if (g_verbose) printf("    step: %s: done\n", name);
}
void SpoilSize() { Post(0x59, 801, MakeKeys(1), 1, CH::kRecSizeRun + 0x10); }
void SpoilVis() { Post(0x59, 802, MakeKeys(1), 2); }
void SpoilCounter() { Post(0x59, 803, MakeKeys(1), 1, 0, 1); }
void SpoilWalk()
{
	Post(0x59, 804, MakeKeys(1), 1);
	*reinterpret_cast<DWORD*>((ULONG_PTR)Begin(true)) = 0x10;  // a size below any record's
}
void SpoilWalkEnd()
{
	Post(0x59, 809, MakeKeys(2), 1);
	End(true) -= 4;  // the walk does not end at the half's end
}
void SpoilIndex()
{
	Post(0x59, 808, MakeKeys(1), 1);
	*IndexWord() = 3;
}
void SpoilCsZero()
{
	Post(0x59, 805, MakeKeys(1), 1);
	memset(QCs(), 0, sizeof(CRITICAL_SECTION));
}
RTL_CRITICAL_SECTION_DEBUG g_fakeDebug;
void SpoilCsDebug()
{
	Post(0x59, 806, MakeKeys(1), 1);
	memset(&g_fakeDebug, 0, sizeof(g_fakeDebug));
	g_fakeDebug.CriticalSection = reinterpret_cast<RTL_CRITICAL_SECTION*>(&g_fakeDebug);  // points elsewhere
	reinterpret_cast<RTL_CRITICAL_SECTION*>(QCs())->DebugInfo = &g_fakeDebug;
}
void SpoilHalf()
{
	Post(0x48, 807, MakeKeys(1), 0);  // no RunCombatSim of that id: the walk goes on to the half's end
	End(true) = Begin(true) + kHalfBytes + 0x1000;  // past the half's memory (unreadable, or zeros: a size out of bounds)
}

void CaseTakeChecks()
{
	TakeRefused("take: the record's size", "combat-hold-record", SpoilSize, 801, __LINE__);
	CHECK(LogHas("the record's size is not RunCombatSim's (0x570, not 0x560)"));
	TakeRefused("take: the visualise byte", "combat-hold-record", SpoilVis, 802, __LINE__);
	TakeRefused("take: the counter at the take", "combat-hold-counter", SpoilCounter, 803, __LINE__);
	TakeRefused("take: a record size out of bounds", "combat-hold-walk", SpoilWalk, 804, __LINE__);
	TakeRefused("take: the walk misses the half's end", "combat-hold-walk", SpoilWalkEnd, 809, __LINE__);
	TakeRefused("take: the index", "combat-hold-queue", SpoilIndex, 808, __LINE__);
	TakeRefused("take: a critical section of zeros", "combat-hold-cs", SpoilCsZero, 805, __LINE__);
	TakeRefused("take: a critical section's debug info", "combat-hold-cs", SpoilCsDebug, 806, __LINE__);
	TakeRefused("take: a half past its memory", "combat-hold-walk", SpoilHalf, 807, __LINE__);
}

void CaseArmRefusals()
{
	// a build without a verified layout: nothing of the queue is read (its page made unreadable proves it). Both known
	// builds are verified since ticket 0618, so the stand-in is the next build id (a build added to vp_exe_build.h
	// whose layout nobody has read yet); BuildLabel names it "an unknown build".
	Fresh();
	CH::Target t = GoodTarget();
	t.build = kVpExeBuildCount;
	DWORD old = 0;
	CHECK(VirtualProtect(g_page, 0x1000, PAGE_NOACCESS, &old) != 0);
	CHECK(!CH::Arm(t));
	CHECK(VirtualProtect(g_page, 0x1000, PAGE_READWRITE, &old) != 0);
	CHECK(CH::CurrentState() == CH::kRefused && g_reports == 1 && g_reportCheck == "combat-hold-layout" && g_loud == 1);
	CHECK(LogHas("layout is not verified on an unknown build"));
	CHECK(LogHas("route S's server fights must not start (D118"));
	Post(0x59, 901, MakeKeys(1), 1);
	CH::AfterCombatCall(901);
	CHECK(CounterOf(901) == 0 && g_reports == 1 && g_loud == 1);
	CHECK(!CH::Arm(GoodTarget()));  // a refusal holds for the load
	CHECK(CH::LayoutVerified(kVpExeBuild4390913) != NULL && CH::LayoutVerified(kVpExeBuild26577625) != NULL &&
	      CH::LayoutVerified(kVpExeBuildCount) == NULL && CH::LayoutVerified(kVpExeBuildUnknown) == NULL);
	// build 26577625 (ticket 0618): the same layout arms there, and the line names its evidence
	Fresh();
	t = GoodTarget();
	t.build = kVpExeBuild26577625;
	CHECK(CH::Arm(t) && CH::CurrentState() == CH::kActive && g_reports == 0 && LogHas("ticket 0618"));
	Fresh();
	// an unknown build
	Fresh();
	t = GoodTarget();
	t.build = kVpExeBuildUnknown;
	t.stamp = 0x12345678;
	t.sizeOfImage = 0x1000;
	CHECK(!CH::Arm(t));
	CHECK(g_reportCheck == "combat-hold-build" && LogHas("not an EXE build the patch knows (PE timestamp 0x12345678"));
	// a symbol not carried (the binding's line), and no address
	Fresh();
	t = GoodTarget();
	t.qbIndex = 0;
	t.notCarried = "symbol pac_qb_index is not carried to (test)";
	CHECK(!CH::Arm(t) && g_reportCheck == "combat-hold-symbol" && LogHas("symbol pac_qb_index is not carried"));
	Fresh();
	t = GoodTarget();
	t.qbBuf = 0;
	CHECK(!CH::Arm(t) && g_reportCheck == "combat-hold-symbol" && LogHas("not carried to Steam build 4390913"));
	// unreadable memory
	Fresh();
	CHECK(VirtualProtect(g_page, 0x1000, PAGE_NOACCESS, &old) != 0);
	CHECK(!CH::Arm(GoodTarget()) && g_reportCheck == "combat-hold-queue");
	CHECK(VirtualProtect(g_page, 0x1000, PAGE_READWRITE, &old) != 0);
	CHECK(g_reports == 1);
	// Disarm before any arm, and SetSinks without a report sink: no crash
	Fresh();
	CH::Disarm();
	CHECK(CH::CurrentState() == CH::kNotArmed);
	CH::SetSinks(LogSink, NULL);
	t.build = kVpExeBuildUnknown;
	CHECK(!CH::Arm(t) && g_reports == 0);
}

unsigned __stdcall HoldQueueLock(void* arg)
{
	EnterCriticalSection(QCs());
	SetEvent(static_cast<HANDLE>(arg));
	Sleep(300);
	LeaveCriticalSection(QCs());
	return 0;
}

void CaseLockRespected()
{
	Fresh();
	CHECK(CH::Arm(GoodTarget()));
	Post(0x59, 1001, MakeKeys(1), 1);
	HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
	HANDLE th = (HANDLE)_beginthreadex(NULL, 0, HoldQueueLock, ev, 0, NULL);
	WaitForSingleObject(ev, 5000);
	const DWORD t0 = GetTickCount();
	CH::AfterCombatCall(1001);
	const DWORD waited = GetTickCount() - t0;
	WaitForSingleObject(th, 5000);
	CloseHandle(th);
	CloseHandle(ev);
	CHECK(waited >= 200);  // the take waited for the queue's critical section
	CHECK(CounterOf(1001) == 1);
}

void CaseSlotsAndDisarm()
{
	Fresh();
	CHECK(CH::Arm(GoodTarget()));
	for (int i = 0; i < CH::kMaxHolds; ++i) Fight(2000 + 2 * i, 100 + i);
	CHECK(CH::Test::Held() == CH::kMaxHolds && CH::Test::NoSlot() == 0);
	Fight(3001, 999);
	CHECK(CH::Test::NoSlot() == 1 && CounterOf(3001) == 0 && LogHas("LOUD: combat hold: no slot for event id 3001"));
	Pump();
	for (int i = 0; i < CH::kMaxHolds; ++i) CH::OwnerEnd(2000 + 2 * i, true);
	CH::OnFrame();
	int still = 0;
	for (int i = 0; i < CH::kMaxHolds; ++i) still += CounterOf(2000 + 2 * i) != 0;
	CHECK(still == 0 && CH::Test::Held() == 0 && CH::Test::DropsEnd() == CH::kMaxHolds);
	// Disarm: no new hold; a held one is still dropped
	Fight(3101, 1);
	CH::Disarm();
	CHECK(CH::CurrentState() == CH::kNotArmed && LogHas("combat hold: off (route S's server fights off)"));
	Fight(3103, 2);
	CHECK(CounterOf(3101) == 1 && CounterOf(3103) == 0);
	CH::OwnerEnd(3101, true);
	CH::OnFrame();
	CHECK(CounterOf(3101) == 0);
	CHECK(CH::Arm(GoodTarget()) && CH::CurrentState() == CH::kActive);  // on again
}

int g_chainedCalls = 0;
void Chained() { ++g_chainedCalls; }
void CaseChain()
{
	Fresh();
	CHECK(CH::Arm(GoodTarget()));
	CH::Chain(Chained);
	Fight(4001, 1);
	CH::OwnerEnd(4001, true);
	CH::FrameHook();
	CHECK(g_chainedCalls == 1 && CounterOf(4001) == 0);
	CH::Chain(NULL);
	CH::FrameHook();
	CHECK(g_chainedCalls == 1);
}

// ---- three threads racing ----------------------------------------------------------------------------------------
const int kRaceFights = 600;
volatile LONG g_raceTaken = 0;
volatile LONG g_raceEnded = 0;
volatile LONG g_raceStop = 0;
volatile LONG g_raceIds[kRaceFights];
// The EXE's main thread pumps queue B only while no Update is pending (combat-choreography.md 3.5.4, game-core-thread.md
// 6.3): the core thread's carried call (post and take) and the pump never overlap. Modelled by this lock.
CRITICAL_SECTION g_update;

unsigned __stdcall RaceCore(void*)
{
	for (int i = 0; i < kRaceFights; ++i)
	{
		const uint32_t id = 10000 + 2 * i;
		while (CH::Test::Held() >= CH::kMaxHolds - 4) Sleep(0);  // the server ends fights as they play
		EnterCriticalSection(&g_update);
		Fight(id, (DWORD)(i % 40));
		LeaveCriticalSection(&g_update);
		InterlockedExchange(&g_raceIds[i], (LONG)id);
		InterlockedIncrement(&g_raceTaken);
	}
	return 0;
}

unsigned __stdcall RaceEnds(void*)
{
	int next = 0;
	while (next < kRaceFights)
	{
		if (g_raceIds[next])
		{
			CH::OwnerEnd((uint32_t)g_raceIds[next], (next % 7) != 0);
			++next;
			InterlockedIncrement(&g_raceEnded);
		}
		else
			Sleep(0);
	}
	return 0;
}

void CaseRace()
{
	Fresh();
	CHECK(CH::Arm(GoodTarget()));
	memset(const_cast<LONG*>(g_raceIds), 0, sizeof(g_raceIds));
	g_raceTaken = g_raceEnded = 0;
	HANDLE a = (HANDLE)_beginthreadex(NULL, 0, RaceCore, NULL, 0, NULL);
	HANDLE b = (HANDLE)_beginthreadex(NULL, 0, RaceEnds, NULL, 0, NULL);
	const DWORD t0 = GetTickCount();
	while ((g_raceEnded < kRaceFights || CH::Test::Held() > 0) && GetTickCount() - t0 < 60000)
	{
		if (TryEnterCriticalSection(&g_update))
		{
			Pump();
			LeaveCriticalSection(&g_update);
		}
		CH::OnFrame();  // the drop runs whether or not an Update is pending (the frame hook)
	}
	WaitForSingleObject(a, 10000);
	WaitForSingleObject(b, 10000);
	CloseHandle(a);
	CloseHandle(b);
	Pump();
	Pump();
	CHECK(CH::Test::Takes() == kRaceFights);
	CHECK(CH::Test::DropsEnd() == kRaceFights && CH::Test::DropsWatchdog() == 0);
	CHECK(CH::Test::NotPlayed() == (kRaceFights + 6) / 7);
	CHECK(CH::Test::Held() == 0 && CH::Test::Gone() == 0 && CH::Test::CounterRefused() == 0);
	CHECK(End(false) == Begin(false) && End(true) == Begin(true));  // every record collected
	CHECK(CH::CurrentState() == CH::kActive && g_reports == 0);
	if (g_verbose) printf("    race: %d fights in %lu ms\n", kRaceFights, (unsigned long)(GetTickCount() - t0));
}

#ifdef VP_COMBAT_HOLD_BINDING
// ---- the binding: a fake image header, the build table ---------------------------------------------------------------
void WriteHeader(BYTE* base, uint32_t stamp, uint32_t soi)
{
	memset(base, 0, 0x400);
	base[0] = 'M';
	base[1] = 'Z';
	*reinterpret_cast<DWORD*>(base + 0x3C) = 0x80;
	memcpy(base + 0x80, "PE\0\0", 4);
	*reinterpret_cast<WORD*>(base + 0x84) = 0x14C;
	*reinterpret_cast<DWORD*>(base + 0x80 + 8) = stamp;
	*reinterpret_cast<WORD*>(base + 0x80 + 24) = 0x10B;
	*reinterpret_cast<DWORD*>(base + 0x80 + 24 + 56) = soi;
}

void CaseBinding()
{
	const VpExeBuildInfo* old = VpExeBuildInfoOf(kVpExeBuild4390913);
	const VpExeBuildInfo* nb = VpExeBuildInfoOf(kVpExeBuild26577625);
	BYTE* base = static_cast<BYTE*>(VirtualAlloc(NULL, old->sizeOfImage, MEM_RESERVE, PAGE_NOACCESS));
	CHECK(base != NULL);
	if (!base) return;
	CHECK(VirtualAlloc(base, 0x1000, MEM_COMMIT, PAGE_READWRITE) != NULL);
	WriteHeader(base, old->timeDateStamp, old->sizeOfImage);
	CH::Target t = CH::TargetOf((ULONG_PTR)base);
	CHECK(t.build == kVpExeBuild4390913);
	CHECK(t.qbIndex == (ULONG_PTR)base + VpExeRva(kVpExeBuild4390913, VPX_pac_qb_index) && t.qbIndex > (ULONG_PTR)base);
	CHECK(t.qbBuf == (ULONG_PTR)base + VpExeRva(kVpExeBuild4390913, VPX_pac_qb_buf) && t.qbBuf > (ULONG_PTR)base);
	CHECK(!t.notCarried || !*t.notCarried);
	// arm on it: the queue's memory is not committed in the fake image, so the readable check refuses
	Fresh();
	CHECK(!CH::Arm(t) && g_reportCheck == "combat-hold-queue");
	// with the queue's pages committed (index 0, zeroed control blocks): armed
	CHECK(VirtualAlloc(reinterpret_cast<void*>(t.qbIndex & ~0xFFFu), 0x1000, MEM_COMMIT, PAGE_READWRITE) != NULL);
	CHECK(VirtualAlloc(reinterpret_cast<void*>(t.qbBuf & ~0xFFFu), 0x2000, MEM_COMMIT, PAGE_READWRITE) != NULL);
	Fresh();
	CHECK(CH::Arm(t) && CH::CurrentState() == CH::kActive);
	// the take on zeroed control blocks: a critical section of zeros, refused (the binding's queue, the core's check)
	CH::AfterCombatCall(1);
	CHECK(CH::CurrentState() == CH::kRefused && g_reportCheck == "combat-hold-cs");
	// a symbol not carried
	ProxyExe::Test::DropSymbol(VPX_pac_qb_index);
	t = CH::TargetOf((ULONG_PTR)base);
	CHECK(t.qbIndex == 0 && t.notCarried && strstr(t.notCarried, "pac_qb_index") != NULL);
	Fresh();
	CHECK(!CH::Arm(t) && g_reportCheck == "combat-hold-symbol");
	ProxyExe::Test::DropSymbol(VPX_pac_qb_buf);
	t = CH::TargetOf((ULONG_PTR)base);
	CHECK(t.qbIndex != 0 && t.qbBuf == 0 && t.notCarried && strstr(t.notCarried, "pac_qb_buf") != NULL);
	ProxyExe::Test::DropSymbol(-1);
	// the new build's header: known, its layout verified (ticket 0618), both names carried; the layout check passes
	// (whatever the fake image holds at the new build's queue addresses decides the rest, never the layout check)
	WriteHeader(base, nb->timeDateStamp, nb->sizeOfImage);
	t = CH::TargetOf((ULONG_PTR)base);
	CHECK(t.build == kVpExeBuild26577625 && t.qbIndex > (ULONG_PTR)base && t.qbBuf > (ULONG_PTR)base);
	Fresh();
	{
		const bool armed = CH::Arm(t);
		CHECK(armed || (g_reportCheck != "combat-hold-layout" && g_reportCheck != "combat-hold-symbol"));
	}
	// an unknown header, and no image at all
	WriteHeader(base, 0x11111111u, 0x2000);
	t = CH::TargetOf((ULONG_PTR)base);
	CHECK(t.build == kVpExeBuildUnknown && t.stamp == 0x11111111u && t.qbIndex == 0 && t.qbBuf == 0);
	Fresh();
	CHECK(!CH::Arm(t) && g_reportCheck == "combat-hold-build");
	t = CH::TargetOf(0);
	CHECK(t.build == kVpExeBuildUnknown);
	VirtualFree(base, 0, MEM_RELEASE);
}
#endif
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
	InitializeCriticalSection(&g_logLock);
	InitializeCriticalSection(&g_update);
	CaseNotArmed();
	CaseTakeAndOwnerEnd();
	CaseDropInWriteHalf();
	CaseNotVisualisedAndMiss();
	CaseWatchdog();
	CaseNotPlayed();
	CaseBurst();
	CaseLoadDuringFight();
	CaseCounterTakenAway();
	CaseTakeChecks();
	CaseArmRefusals();
	CaseLockRespected();
	CaseSlotsAndDisarm();
	CaseChain();
	CaseRace();
#ifdef VP_COMBAT_HOLD_BINDING
	CaseBinding();
	printf("combat_hold_test: binding cases included\n");
#endif
	if (expectFail) CHECK(!"the runner's negative test: this check must fail");
	printf("combat_hold_test: %d/%d checks passed\n", g_pass, g_pass + g_fail);
	return g_fail ? 1 : 0;
}
