// test_rt_ring.cpp - issue 0119, own code, 2026-11-13. Built and run by server/tests/run_rt_ring_test.py.
//
// Unit test of the server's RETURN payload ring: KeepReturn() of server/src/remote/srv_rt.cpp, compiled out of that file
// (the runner writes the function into keep_return.inc beside this file). The style of proxy/tools/rt_tests unit.cpp
// TestReturnRing (issue 9041), with one difference: KeepReturn keeps its slots in function-local thread_local storage
// the test cannot name, so the bytes it holds are measured with a counting global operator new/delete, each scenario on
// a thread of its own (a fresh ring), and the counter is read again after that thread has ended.
//
// Exit 0 when every check passes, 1 when one fails.
#include <vpipc/marshal.h>

#include <atomic>
#include <new>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <utility>
#include <vector>

// ---- counting allocator: live bytes requested through operator new ------------------------------------------------
static std::atomic<long long> g_live(0);

static void* CountedAlloc(size_t n)
{
	// 16-byte header holds the size (keeps the 16-byte alignment of the heap)
	unsigned char* p = static_cast<unsigned char*>(malloc(n + 16));
	if (!p) return NULL;
	*reinterpret_cast<size_t*>(p) = n;
	g_live += (long long)n;
	return p + 16;
}
static void CountedFree(void* q)
{
	if (!q) return;
	unsigned char* p = static_cast<unsigned char*>(q) - 16;
	g_live -= (long long)*reinterpret_cast<size_t*>(p);
	free(p);
}
void* operator new(size_t n)
{
	void* p = CountedAlloc(n);
	if (!p) throw std::bad_alloc();
	return p;
}
void* operator new[](size_t n) { return operator new(n); }
void* operator new(size_t n, const std::nothrow_t&) noexcept { return CountedAlloc(n); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return CountedAlloc(n); }
void operator delete(void* p) noexcept { CountedFree(p); }
void operator delete[](void* p) noexcept { CountedFree(p); }
void operator delete(void* p, size_t) noexcept { CountedFree(p); }
void operator delete[](void* p, size_t) noexcept { CountedFree(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { CountedFree(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { CountedFree(p); }

// ---- the function under test (server/src/remote/srv_rt.cpp, extracted by the runner) ------------------------------
#include "keep_return.inc"

// ---- checks ----------------------------------------------------------------------------------------------------------
static int g_fails = 0, g_passes = 0;
static void Check(const char* what, bool ok, const char* detail = NULL)
{
	printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", what, detail ? ": " : "", detail ? detail : "");
	if (ok) ++g_passes; else ++g_fails;
}

static const long long kBound = 64LL * 64 * 1024;  // 64 slots of at most 64 KB each (the proxy's bound, 9041)

namespace
{
// Drives KeepReturn the way End() does: a reader on a payload in a (reused) source buffer, KeepReturn, the reader on the
// copy. Keeps the copies of the last 64 calls (this one included) and checks they are intact: the contract the
// decoders rely on (a nested call may come before the payload is read to its end).
struct Sim
{
	std::vector<uint8_t> src;  // stands for the channel's data area
	uint32_t lcg, calls;
	long long base, peak;
	bool copiesOk, readerOk;
	std::vector<std::pair<const uint8_t*, uint32_t> > recent;  // (copy, call number)
	Sim() : lcg(12345u), calls(0), base(0), peak(0), copiesOk(true), readerOk(true)
	{
		src.resize(14u << 20);  // larger than any payload: no allocation while measuring
		recent.reserve(256);
		base = g_live;
	}
	long long Held() const { return g_live - base; }
	uint32_t Rand() { lcg = lcg * 1664525u + 1013904223u; return lcg >> 8; }
	void Call(uint32_t n)
	{
		const uint32_t tag = ++calls;
		if (n >= 8) { memcpy(&src[0], &tag, 4); memcpy(&src[n - 4], &tag, 4); }
		vpipc::Reader r(n ? &src[0] : NULL, n);
		KeepReturn(r);
		if (r.remaining() != n || (n == 0) != (r.cursor() == NULL) || (n && r.cursor() == &src[0])) readerOk = false;
		while (!recent.empty() && calls - recent.front().second >= 64u) recent.erase(recent.begin());
		if (n >= 8) recent.push_back(std::make_pair(r.cursor(), tag));
		if ((calls & 1023u) == 0 || n > (1u << 20))
			for (size_t i = 0; i < recent.size(); ++i)
			{
				uint32_t a = 0;
				memcpy(&a, recent[i].first, 4);
				if (a != recent[i].second) copiesOk = false;
			}
		if (Held() > peak) peak = Held();
	}
	void Small(uint32_t count)
	{
		for (uint32_t i = 0; i < count; ++i) Call(4 + 8 * (Rand() % 3));
	}
	// The huge save's RETURN pattern (proxy/tools/rt_tests unit.cpp RingSim, from 9036's proxy-calls.tsv): PreGame 14 KB,
	// Game 800 KB, 64 players about 358 KB, 64 teams about 20 KB, the map 12.66 MB growing 4 KB per turn, 51 KB.
	void Save(uint32_t turn)
	{
		Small(3 + Rand() % 20); Call(14 * 1024);
		Small(3 + Rand() % 20); Call(800 * 1024);
		for (int p = 0; p < 64; ++p) { Small(Rand() % 4); Call(358 * 1024 + Rand() % 4096); }
		for (int t = 0; t < 64; ++t) { Small(Rand() % 4); Call(20 * 1024 + Rand() % 1024); }
		Small(3 + Rand() % 20); Call(12662545u + turn * 4096u);
		Small(3 + Rand() % 20); Call(51 * 1024);
	}
	void Turn(uint32_t turn)
	{
		Small(1000 + Rand() % 2000);
		for (int k = 0; k < 6; ++k) { Small(Rand() % 400); Call((1u << 20) + Rand() % (1u << 20)); }
		Save(turn);
		Small(500 + Rand() % 2000);
		Save(turn);
		Small(500 + Rand() % 1000);
	}
};

struct Result
{
	long long held, peak, step1, step2;
	bool copiesOk, readerOk;
	uint32_t calls;
};

// Runs fn on a new thread (a fresh ring); returns what fn measured and, in leftAfterExit, the bytes still allocated
// after the thread has ended (the ring of an ended thread must be freed).
template <class F> Result OnThread(F fn, long long& leftAfterExit)
{
	Result res;
	memset(&res, 0, sizeof(res));
	Result* pres = new Result(res);  // allocated before the baseline, freed after the reading
	const long long before = g_live;
	std::thread t([&fn, pres]() { *pres = fn(); });
	t.join();
	leftAfterExit = g_live - before;
	res = *pres;
	delete pres;
	return res;
}
}  // namespace

int main()
{
	char m[256];
	// 1. The acceptance case: one large RETURN (the huge save's map body) and the small ones after it.
	{
		long long left = 0;
		const Result r = OnThread([]() {
			Sim s;
			s.Small(10);
			s.Call(12662545u);
			Result x;
			memset(&x, 0, sizeof(x));
			x.step1 = s.Held();
			s.Small(64);
			x.held = s.Held();
			x.peak = s.peak;
			x.copiesOk = s.copiesOk;
			x.readerOk = s.readerOk;
			x.calls = s.calls;
			return x;
		}, left);
		_snprintf_s(m, sizeof(m), _TRUNCATE, "%.2f MiB held after the 12662545-byte RETURN, %.3f MiB after 64 small ones (bound %.0f KiB)",
		            r.step1 / 1048576.0, r.held / 1048576.0, kBound / 1024.0);
		Check("ring: after one large RETURN and 64 small ones the held bytes fall back under 64 x 64 KB", r.held <= kBound, m);
		Check("ring: the reader is set on a copy of the payload, its length unchanged", r.readerOk && r.copiesOk);
		_snprintf_s(m, sizeof(m), _TRUNCATE, "%lld bytes left allocated after the thread ended", left);
		Check("ring: the ring of an ended thread is freed", left == 0, m);
	}
	// 2. Twenty AI turns of the huge save's pattern (two saves a turn, thousands of small RETURNs between).
	{
		long long left = 0;
		const Result r = OnThread([]() {
			Sim s;
			for (uint32_t t = 0; t < 20; ++t) s.Turn(t);
			Result x;
			memset(&x, 0, sizeof(x));
			x.step1 = s.Held();
			s.Small(64);
			x.held = s.Held();
			x.peak = s.peak;
			x.copiesOk = s.copiesOk;
			x.readerOk = s.readerOk;
			x.calls = s.calls;
			return x;
		}, left);
		_snprintf_s(m, sizeof(m), _TRUNCATE, "%.1f MiB held after 20 AI turns, %.3f MiB after 64 small calls, peak %.1f MiB, %u calls",
		            r.step1 / 1048576.0, r.held / 1048576.0, r.peak / 1048576.0, r.calls);
		Check("ring: after 20 turns of saves and 64 small calls the ring holds at most 64 x 64 KB", r.held <= kBound, m);
		Check("ring: the peak stays near one save's bodies (below 64 MB)", r.peak < (64LL << 20), m);
		Check("ring: every copy is intact for the next 63 calls", r.copiesOk && r.readerOk);
		_snprintf_s(m, sizeof(m), _TRUNCATE, "%lld bytes left allocated after the thread ended", left);
		Check("ring: the ring of an ended thread is freed (20 turns)", left == 0, m);
	}
	// 3. A large payload gets a buffer of exactly its size: a slot that held 4 MB takes a 5 MB payload (assign would grow
	// the 4 MB buffer 1.5x to 6 MB), and the 5 MB buffer is released when a small payload reuses the slot.
	{
		long long left = 0;
		const Result r = OnThread([]() {
			Sim s;
			Result x;
			memset(&x, 0, sizeof(x));
			s.Call(4u << 20);
			s.Small(63);
			const long long h0 = s.Held();
			s.Call(5u << 20);  // the slot of the 4 MB payload
			x.step1 = s.Held() - h0 + (4LL << 20);  // the slot's buffer now (its 4 MB were counted in h0)
			s.Small(63);
			s.Call(100);  // that slot again
			x.held = s.Held();
			x.copiesOk = s.copiesOk;
			x.readerOk = s.readerOk;
			return x;
		}, left);
		_snprintf_s(m, sizeof(m), _TRUNCATE, "the slot holds %lld bytes for a %u-byte payload; %lld bytes held after the slot was reused",
		            r.step1, 5u << 20, r.held);
		Check("ring: a large payload is sized exactly (no 1.5x growth)", r.step1 == (5LL << 20), m);
		Check("ring: the large buffer is released when its slot is reused", r.held <= kBound, m);
	}
	// 4. An empty payload: the reader gets NULL and length 0.
	{
		long long left = 0;
		const Result r = OnThread([]() {
			Sim s;
			s.Call(0);
			Result x;
			memset(&x, 0, sizeof(x));
			x.readerOk = s.readerOk;
			return x;
		}, left);
		Check("ring: an empty payload gives a NULL reader of length 0", r.readerOk);
	}
	printf("test_rt_ring: %d passed, %d failed\n", g_passes, g_fails);
	return g_fails ? 1 : 0;
}
