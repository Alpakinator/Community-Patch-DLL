// proxy_budget_core.h - the proxy's side of the turn budget (ticket 4652, epic 0437 W4a): header-only, C++03, no
// proxy state, so that proxy/tests/budget/ can drive it with a fake clock and a fake turn block. proxy_budget.cpp
// wraps it with QueryPerformanceCounter, the section and the file.
//
// What it measures, per turn (a turn = the interval between two boundaries that server.exe publishes in the shared
// turn block, vp_budget.h; the same QueryPerformanceCounter clock in both processes):
//   exe_alone  the time in which no EXE thread is inside a call to the server (from just before the CALL is sent until
//              its RETURN is decoded): the EXE (and the proxy's local answers) alone. One 64-bit word holds the
//              number of threads with a call in flight (low 8 bits) and the clock of its last change; a thread's first
//              call in flight and its last one change it by compare-and-swap, and the 0 -> 1 change charges the idle
//              interval since the 1 -> 0 change. The server's boundary lies inside ICvGame1::Update, i.e. inside a
//              call in flight, so an idle interval never spans a boundary.
//   out.<c>    calls the EXE made into the DLL (EXE -> server), per class: count and round trip seen by the proxy
//   in.<c>     calls the DLL made into the EXE (server -> proxy), per class: count and handler time in the EXE
// Each event goes to the turn in which it ended. Accumulators: 64-bit compare-and-swap adds into a ring of buckets;
// bucket b holds the events of [boundary b-1, boundary b) (bucket 0: from the proxy's start to the first boundary). A
// bucket is written once its end boundary is published and `settle` ticks old (proxy_budget.cpp: 50 ms; an event that
// ends inside the bucket is added within microseconds of its end), or at the end. (Changed 2026-11-16 19:30: the first
// rule waited for the next boundary too, so a game stopped by the harness lost its last full turn.)
#pragma once
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "vp_budget.h"

namespace ProxyBudgetCore
{
const int kBuckets = 16;
const int kClasses = vpbudget::kClassCount;

inline void Add64(volatile LONG64* p, int64_t v)
{
	LONG64 cur = *p;
	for (;;)
	{
		const LONG64 prev = InterlockedCompareExchange64(p, cur + v, cur);
		if (prev == cur) return;
		cur = prev;
	}
}
inline int64_t Read64(volatile LONG64* p) { return (int64_t)InterlockedCompareExchange64(p, 0, 0); }

struct Bucket
{
	volatile LONG64 exeAlone;
	volatile LONG64 idleIntervals;
	volatile LONG64 calls[2][kClasses];  // [0] out: EXE -> DLL round trips; [1] in: DLL -> EXE handler times
	volatile LONG64 ticks[2][kClasses];
};

// A closed bucket, ready to be written.
struct Closed
{
	int64_t index;  // b
	int turn;       // the game turn of the bucket (-1 for bucket 0, the load)
	int64_t wall;   // ticks between its boundaries (bucket 0: from the proxy's start)
	int flag;       // 0, 1 load, 2 end (partial), 3 lost (its boundaries left the ring before it was written)
	int64_t exeAlone, idleIntervals;
	int64_t calls[2][kClasses], ticks[2][kClasses];
};

class Core
{
public:
	typedef int64_t (*ClockFn)();

	void Init(ClockFn clock, const vpbudget::TurnBlock* block)
	{
		memset(m_b, 0, sizeof(m_b));
		m_clock = clock;
		m_block = block;
		m_start = clock();
		m_word = (LONG64)(((uint64_t)m_start) << 8);
		m_nextFlush = 0;
		m_flushing = 0;
		m_late = 0;
		m_settle = 0;
	}
	// Ticks a closed bucket waits after its end boundary before it is written (default 0).
	void SetSettle(int64_t ticks) { m_settle = ticks; }

	// The first call in flight on a thread begins (the caller keeps the per-thread count).
	void BusyBegin(int64_t now) { Busy(+1, now); }
	// The last call in flight on a thread ended.
	void BusyEnd(int64_t now) { Busy(-1, now); }
	// A call ended: dir 0 = made by the EXE (round trip), 1 = served for the DLL (handler time).
	void Call(int dir, int cls, int64_t t0, int64_t t1)
	{
		Bucket& b = m_b[Index(BucketOf(t1))];
		Add64(&b.calls[dir][cls], 1);
		Add64(&b.ticks[dir][cls], t1 - t0);
	}

	// Boundaries published so far.
	int32_t Published() const { return m_block ? m_block->count : 0; }
	// Takes the next bucket that is ready (its end boundary published and `settle` ticks old), or, with `all`, every remaining one (the
	// last as partial up to `now`). Returns false when there is none. Single consumer: TryLock/Unlock around it.
	bool TakeClosed(bool all, int64_t now, Closed& out)
	{
		const int32_t n = Published();
		const int64_t b = m_nextFlush;
		if (!all ? !(b <= (int64_t)n - 1) : !(b <= (int64_t)n)) return false;
		if (!all)
		{
			int64_t tb = 0;
			if (Entry((int32_t)b, &tb, NULL) && now - tb < m_settle) return false;  // not settled yet
		}
		memset(&out, 0, sizeof(out));
		out.index = b;
		int64_t tStart = m_start, tEnd = now;
		int32_t turn = -1;
		bool lost = false;
		if (b >= 1 && !Entry((int32_t)(b - 1), &tStart, &turn)) lost = true;
		if (b < (int64_t)n)
		{
			if (!Entry((int32_t)b, &tEnd, NULL)) lost = true;
		}
		out.turn = (b == 0) ? -1 : turn;
		out.wall = lost ? 0 : tEnd - tStart;
		out.flag = lost ? 3 : (b >= (int64_t)n ? 2 : (b == 0 ? 1 : 0));
		Bucket& s = m_b[Index(b)];
		out.exeAlone = Read64(&s.exeAlone);
		out.idleIntervals = Read64(&s.idleIntervals);
		for (int d = 0; d < 2; ++d)
			for (int c = 0; c < kClasses; ++c)
			{
				out.calls[d][c] = Read64(&s.calls[d][c]);
				out.ticks[d][c] = Read64(&s.ticks[d][c]);
			}
		memset((void*)&s, 0, sizeof(Bucket));
		m_nextFlush = b + 1;
		return true;
	}
	bool TryLock() { return InterlockedCompareExchange(&m_flushing, 1, 0) == 0; }
	void Unlock() { InterlockedExchange(&m_flushing, 0); }
	int64_t Late() const { return m_late; }
	int64_t Start() const { return m_start; }

	// The bucket of an event that ended at t: the number of published boundaries at or before t.
	int64_t BucketOf(int64_t t)
	{
		const int32_t n = Published();
		const int32_t lo = n > (int32_t)vpbudget::kRing ? n - (int32_t)vpbudget::kRing : 0;
		for (int32_t k = n - 1; k >= lo; --k)
		{
			int64_t tb = 0;
			if (!Entry(k, &tb, NULL)) continue;  // overwritten or being written: an older one decides
			if (tb <= t) return (int64_t)k + 1;
		}
		if (lo > 0) InterlockedIncrement((volatile LONG*)&m_late);
		return lo;
	}

private:
	static int Index(int64_t b) { return (int)(b % kBuckets); }

	// Boundary k from the ring, checked by its sequence number (false: overwritten or being written).
	bool Entry(int32_t k, int64_t* pTb, int32_t* pTurn) const
	{
		if (!m_block) return false;
		const vpbudget::BoundaryEntry& e = m_block->e[(uint32_t)k % vpbudget::kRing];
		const int32_t want = 2 * k + 2;
		if (e.seq != want) return false;
		MemoryBarrier();
		const int64_t tb = e.tb;
		const int32_t turn = e.turn;
		MemoryBarrier();
		if (e.seq != want) return false;
		*pTb = tb;
		if (pTurn) *pTurn = turn;
		return true;
	}

	void Busy(int delta, int64_t now)
	{
		LONG64 oldw, neww;
		int64_t t = now;
		for (;;)
		{
			oldw = Read64(&m_word);
			t = m_clock();  // read after the word: never earlier than its last change
			const int64_t count = (int64_t)(oldw & 0xFF) + delta;
			neww = (LONG64)((((uint64_t)t) << 8) | (uint64_t)(count < 0 ? 0 : (count > 255 ? 255 : count)));
			if (InterlockedCompareExchange64(&m_word, neww, oldw) == oldw) break;
		}
		(void)now;
		if (delta > 0 && (oldw & 0xFF) == 0)
		{
			const int64_t since = (int64_t)(((uint64_t)oldw) >> 8);
			const int64_t d = t - since;
			Bucket& b = m_b[Index(BucketOf(t))];
			Add64(&b.exeAlone, d > 0 ? d : 0);
			Add64(&b.idleIntervals, 1);
		}
	}

	ClockFn m_clock;
	const vpbudget::TurnBlock* m_block;
	int64_t m_start;
	volatile LONG64 m_word;  // (clock << 8) | threads with a call in flight
	int64_t m_nextFlush;
	volatile LONG m_flushing;
	volatile LONG m_late;
	int64_t m_settle;
	Bucket m_b[kBuckets];
};
}  // namespace ProxyBudgetCore
