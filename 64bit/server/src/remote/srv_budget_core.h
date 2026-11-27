// srv_budget_core.h - the turn budget's accounting in server.exe's remote mode (ticket 4652, epic 0437 W4a). Header-only
// and free of DLL types so that server/tests/run_budget_test.py can drive it with a fake clock; srv_budget.cpp wraps it
// with QueryPerformanceCounter, the files and the shared turn block.
//
// One 64-bit state word for the whole process: bits 0-47 the clock (ticks, modulo 2^48) of its last change, bit 48 the
// turn parity, then 4 bits per service thread (ui 49-52, core 53-56, aux 57-60) holding that thread's current class
// (vp_budget.h: 0 = waiting for work, kInUpdate = inside Update on its own work, kIn* = serving another engine call,
// kOut* = waiting for an engine call it made, kOutSave = anything inside a save). Every change of a thread's class, and
// each turn boundary, replaces the word with one compare-and-swap and charges the interval since the word's last change
// to ONE row: the class of the highest-priority busy thread in the old word (core, then ui, then aux), or row 0 (the EXE
// alone) when none is busy. So the rows of a turn partition the turn's wall time exactly, in integer ticks.
// The accumulators are per channel (written only by that channel's thread) and per parity; a boundary flips the parity,
// waits until no other thread is between its compare-and-swap and its accumulator update (`adding`), then reads and
// zeroes the closed parity. No lock, allocation or I/O per call: one clock read, one compare-and-swap, a few adds.
#pragma once
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "vp_budget.h"

namespace SrvBudgetCore
{
const int kChannels = 3;                 // vpipc kChannelUi 0, kChannelCore 1, kChannelAux 2
const int kSlots = kChannels + 1;        // slot 3: boundaries and the final flush from a thread that serves no channel
const int kMaxDepth = 64;                // frames tracked per thread; deeper ones are counted and left to their parent
const int kRows = vpbudget::kClassCount;
const int kTsBits = 48;
const uint64_t kTsMask = (1ull << kTsBits) - 1;
const int kParityBit = 48;
const int kPriority[kChannels] = {1, 0, 2};  // core first, then ui, then aux

inline uint64_t Ts(uint64_t w) { return w & kTsMask; }
inline int Parity(uint64_t w) { return (int)((w >> kParityBit) & 1u); }
inline int Cls(uint64_t w, int ch) { return (int)((w >> (49 + 4 * ch)) & 0xFu); }
inline uint64_t WithCls(uint64_t w, int ch, int c)
{
	const int s = 49 + 4 * ch;
	return (w & ~(0xFull << s)) | ((uint64_t)(c & 0xF) << s);
}
inline int TopChannel(uint64_t w)
{
	for (int i = 0; i < kChannels; ++i)
		if (Cls(w, kPriority[i])) return kPriority[i];
	return -1;
}
inline int BusyCount(uint64_t w)
{
	int n = 0;
	for (int ch = 0; ch < kChannels; ++ch)
		if (Cls(w, ch)) ++n;
	return n;
}
inline int RowOf(uint64_t w)
{
	const int top = TopChannel(w);
	return top < 0 ? 0 : Cls(w, top);
}

// One turn's sums. rows: exclusive ticks per row (they sum to the turn's wall time). msgs: messages (one CALL or one
// RETURN) whose transit lies inside the row's time on the thread the row belongs to; hidden: messages of a thread whose
// time was charged to a higher-priority thread's row. calls/ticks: per class, calls and inclusive ticks (served: handler
// time; made: round trip), by the turn in which the call ended. overlap: ticks with more than one thread busy.
struct Bucket
{
	int64_t rows[kRows];
	int64_t msgs[kRows];
	int64_t calls[kRows];
	int64_t ticks[kRows];
	int64_t hidden;
	int64_t overlap;
	int64_t updates;
	int64_t anomalies;  // a clock delta that ran backwards (never expected; charged as 0)
	int64_t deep;       // frames beyond kMaxDepth (not tracked)
	void Add(const Bucket& o)
	{
		for (int i = 0; i < kRows; ++i)
		{
			rows[i] += o.rows[i];
			msgs[i] += o.msgs[i];
			calls[i] += o.calls[i];
			ticks[i] += o.ticks[i];
		}
		hidden += o.hidden;
		overlap += o.overlap;
		updates += o.updates;
		anomalies += o.anomalies;
		deep += o.deep;
	}
	int64_t RowSum() const
	{
		int64_t s = 0;
		for (int i = 0; i < kRows; ++i) s += rows[i];
		return s;
	}
};

struct __declspec(align(64)) Slot
{
	int depth;      // tracked frames
	int skipped;    // frames beyond kMaxDepth
	int saveDepth;  // tracked frames of class kOutSave
	uint8_t eff[kMaxDepth];   // the class the frame shows in the word (kOutSave inside a save)
	uint8_t orig[kMaxDepth];  // the call's own class (inclusive counters)
	int64_t t0[kMaxDepth];
	volatile LONG adding;     // 1 from before the compare-and-swap until the accumulators are updated
	volatile LONG addSeq;     // completed updates
	Bucket b[2];
};

class Core
{
public:
	typedef int64_t (*ClockFn)();

	void Init(ClockFn clock)
	{
		memset(m_slot, 0, sizeof(m_slot));
		m_clock = clock;
		m_start = clock();
		m_lastBoundary = m_start;
		m_word = (uint64_t)m_start & kTsMask;
		m_flushing = 0;
	}

	// ---- frames (each called only by the thread that serves channel ch) ----
	// The engine called the DLL: one CALL message arrived in the interval that ends now.
	void ServedEnter(int ch, int cls)
	{
		Slot& s = m_slot[ch];
		if (s.depth >= kMaxDepth)
		{
			++s.skipped;
			return;
		}
		const int eff = s.saveDepth ? (int)vpbudget::kOutSave : cls;
		s.eff[s.depth] = (uint8_t)eff;
		s.orig[s.depth] = (uint8_t)cls;
		++s.depth;
		s.t0[s.depth - 1] = Change(ch, eff, false, 1, 0, NULL);
	}
	// The served call ends: its RETURN message goes out in the interval that begins now.
	void ServedLeave(int ch)
	{
		Slot& s = m_slot[ch];
		if (s.skipped)
		{
			--s.skipped;
			++s.b[Parity(m_word)].deep;
			return;
		}
		if (s.depth <= 0) return;
		const int i = --s.depth;
		const int parent = s.depth ? s.eff[s.depth - 1] : 0;
		int p = 0;
		const int64_t now = Change(ch, parent, false, 0, 1, &p);
		s.b[p].calls[s.orig[i]] += 1;
		s.b[p].ticks[s.orig[i]] += now - s.t0[i];
	}
	// The DLL calls the engine and waits: the CALL message goes out in the interval that begins now.
	void OutEnter(int ch, int cls)
	{
		Slot& s = m_slot[ch];
		if (s.depth >= kMaxDepth)
		{
			++s.skipped;
			return;
		}
		if (cls == vpbudget::kOutSave) ++s.saveDepth;
		const int eff = s.saveDepth ? (int)vpbudget::kOutSave : cls;
		s.eff[s.depth] = (uint8_t)eff;
		s.orig[s.depth] = (uint8_t)cls;
		++s.depth;
		s.t0[s.depth - 1] = Change(ch, eff, false, 0, 1, NULL);
	}
	// The engine call returned: its RETURN message arrived in the interval that ends now.
	void OutLeave(int ch)
	{
		Slot& s = m_slot[ch];
		if (s.skipped)
		{
			--s.skipped;
			++s.b[Parity(m_word)].deep;
			return;
		}
		if (s.depth <= 0) return;
		const int i = --s.depth;
		if (s.orig[i] == vpbudget::kOutSave && s.saveDepth > 0) --s.saveDepth;
		const int parent = s.depth ? s.eff[s.depth - 1] : 0;
		int p = 0;
		const int64_t now = Change(ch, parent, false, 1, 0, &p);
		s.b[p].calls[s.orig[i]] += 1;
		s.b[p].ticks[s.orig[i]] += now - s.t0[i];
	}
	// One more Update call in the current turn (the generated dispatcher's EnterUpdate, outermost level).
	void CountUpdate(int ch) { ++m_slot[ch].b[Parity(m_word)].updates; }

	// ---- turn boundary ----
	// Flips the parity on slot `slot` (a channel's thread, or kChannels for any other thread), waits for the other
	// slots' updates in flight, then sums the closed parity of every slot into `out` and zeroes it. Returns the
	// boundary's clock value; *pWall = ticks since the previous boundary (= out.RowSum() unless a slot did not finish
	// within `waitCapMs`: then *pSynced = false). Boundaries are serialised among themselves (m_flushing).
	int64_t Boundary(int slot, Bucket& out, int64_t* pWall, bool* pSynced, DWORD waitCapMs = 200)
	{
		while (InterlockedCompareExchange(&m_flushing, 1, 0) != 0) SwitchToThread();
		int p = 0;
		const int64_t now = Change(slot, -1, true, 0, 0, &p);
		bool synced = true;
		for (int j = 0; j < kSlots; ++j)
		{
			if (j == slot) continue;
			Slot& s = m_slot[j];
			const LONG seq = s.addSeq;
			const DWORD t0 = GetTickCount();
			for (unsigned spin = 0; s.adding && s.addSeq == seq; ++spin)
			{
				if (spin > 64) SwitchToThread();
				if (GetTickCount() - t0 > waitCapMs)
				{
					synced = false;
					break;
				}
			}
		}
		memset(&out, 0, sizeof(out));
		for (int j = 0; j < kSlots; ++j)
		{
			out.Add(m_slot[j].b[p]);
			memset(&m_slot[j].b[p], 0, sizeof(Bucket));
		}
		*pWall = now - m_lastBoundary;
		m_lastBoundary = now;
		if (pSynced) *pSynced = synced;
		InterlockedExchange(&m_flushing, 0);
		return now;
	}

	int64_t Start() const { return m_start; }
	int64_t LastBoundary() const { return m_lastBoundary; }
	uint64_t Word() const { return m_word; }
	const Slot& SlotOf(int i) const { return m_slot[i]; }

private:
	// Replaces this slot's class in the word (newCls -1: unchanged; slot kChannels has none) and, with `flip`, the
	// parity; charges the interval since the word's last change to the row of the old word's top thread. msgsBefore:
	// messages in the interval that ends now; msgsAfter: in the one that begins now. Returns now; *pParity = the
	// parity of the interval that ended now (the bucket of everything that ends at this change).
	int64_t Change(int slot, int newCls, bool flip, int msgsBefore, int msgsAfter, int* pParity)
	{
		Slot& s = m_slot[slot];
		s.adding = 1;  // the compare-and-swap below is a full barrier: no other slot sees the new word before this
		uint64_t oldw, neww;
		int64_t now;
		for (;;)
		{
			oldw = (uint64_t)InterlockedCompareExchange64((volatile LONG64*)&m_word, 0, 0);
			now = m_clock();  // read after the word: never earlier than the word's last change
			neww = (newCls >= 0 && slot < kChannels) ? WithCls(oldw, slot, newCls) : oldw;
			neww = (neww & ~kTsMask) | ((uint64_t)now & kTsMask);
			if (flip) neww ^= (1ull << kParityBit);
			if ((uint64_t)InterlockedCompareExchange64((volatile LONG64*)&m_word, (LONG64)neww, (LONG64)oldw) == oldw)
				break;
		}
		const int p = Parity(oldw);
		Bucket& b = s.b[p];
		uint64_t d = ((uint64_t)now - Ts(oldw)) & kTsMask;
		if (d >> (kTsBits - 1))  // backwards: never expected (the clock is read after the word)
		{
			++b.anomalies;
			d = 0;
		}
		const int top = TopChannel(oldw);
		const int row = top < 0 ? 0 : Cls(oldw, top);
		b.rows[row] += (int64_t)d;
		if (BusyCount(oldw) > 1) b.overlap += (int64_t)d;
		if (msgsBefore)
		{
			if (top < 0 || top == slot)
				b.msgs[row] += msgsBefore;
			else
				b.hidden += msgsBefore;
		}
		if (msgsAfter)
		{
			Bucket& b2 = s.b[Parity(neww)];
			const int top2 = TopChannel(neww);
			if (top2 < 0 || top2 == slot)
				b2.msgs[top2 < 0 ? 0 : Cls(neww, top2)] += msgsAfter;
			else
				b2.hidden += msgsAfter;
		}
		s.addSeq = s.addSeq + 1;
		InterlockedExchange(&s.adding, 0);  // a full barrier: the updates above are visible before `adding` reads 0
		if (pParity) *pParity = p;
		return now;
	}

	volatile uint64_t m_word;
	ClockFn m_clock;
	int64_t m_start;
	int64_t m_lastBoundary;
	volatile LONG m_flushing;
	Slot m_slot[kSlots];
};
}  // namespace SrvBudgetCore
