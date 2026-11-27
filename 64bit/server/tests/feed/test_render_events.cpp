// test_render_events.cpp - unit tests of the server's side of the DLL's render messages (ticket 4932, epic 0454 X2:
// server/src/feed/srv_render_events.cpp with the feed, srv_feed.cpp). Built and run by run_render_events_test.py
// (clang-cl /W4 /WX, and once with MSVC /fsanitize=address). This program is the host EXE: it exports VP64RenderEvent
// (srv_render_events.cpp is linked into it), so a stand-in DLL built from the fork's real VP64RenderEvents.h/.inl finds
// the export through GetModuleHandle(NULL) exactly as the game DLL does in server.exe.
//
//   test_render_events [<standin-on.dll> <standin-none.dll> <standin-v2.dll>] [--bench]
//
// Exit 0 when every check passed, 1 otherwise. -DRE_TEST_INJECT_FAIL adds one failing check (the runner's negative).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include "../../src/feed/srv_feed.h"
#include "../../src/feed/srv_render_events.h"

static int g_pass = 0, g_fail = 0;
static const char* g_test = "";

#define CHECK(cond)                                                                                                   \
	do                                                                                                                \
	{                                                                                                                 \
		if (cond) ++g_pass;                                                                                           \
		else { ++g_fail; printf("FAIL %s: %s (line %d)\n", g_test, #cond, __LINE__); }                                \
	} while (0)

static bool Has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

// The feed as a test needs it: off or on, empty, counters zero.
static void Fresh(bool feedOn)
{
	SrvFeed::SetEnabled(true);
	SrvFeed::Batch b;
	SrvFeed::Drain(b);   // empty what an earlier test left
	SrvFeed::SetEnabled(feedOn);
	SrvRenderEvents::ResetCounters();
	SrvRenderEvents::SetTurn(-1);
}

static std::vector<SrvFeed::Event> Drained()
{
	SrvFeed::Batch b;
	SrvFeed::Drain(b);
	std::vector<SrvFeed::Event> v;
	for (const SrvFeed::Event& e : b.events)
		if (e.kind >= SrvFeed::kExtrasFirst && e.kind <= SrvFeed::kExtrasLast)
			v.push_back(e);
	return v;
}

static void TestKinds()
{
	g_test = "kinds";
	CHECK(SrvFeed::kExtrasFirst == 1000 && SrvFeed::kExtrasLast == 1999);
	CHECK(SrvFeed::kUnitActivityChanged == 1000 && SrvFeed::kUnitCarrierChanged == 1001);
	CHECK(SrvFeed::kExtrasFirst > SrvFeed::kUiMirror);   // after every stock kind
	CHECK(SrvRenderEvents::kVersionExpected == 1);
	printf("source has VP64RenderEvents.h: %d\n", SrvRenderEvents::SourceHasHeader() ? 1 : 0);
}

static void TestDecide()
{
	g_test = "decide";
	SrvRenderEvents::Decision d = SrvRenderEvents::Decide(false, 0);
	CHECK(!d.sends && Has(d.line, "has no DllGetRenderEventsVersion export") && Has(d.line, "VP64_RENDER_EVENTS"));
	d = SrvRenderEvents::Decide(true, 1);
	CHECK(d.sends && Has(d.line, "sends render messages, version 1") && !Has(d.line, "LOUD"));
	d = SrvRenderEvents::Decide(true, 2);
	CHECK(!d.sends && Has(d.line, "LOUD") && Has(d.line, "version 2, this server knows version 1"));
	d = SrvRenderEvents::Decide(true, 0);
	CHECK(!d.sends && Has(d.line, "version 0"));
	// Apply on no module: as a DLL without the export
	const std::string line = SrvRenderEvents::Apply(NULL);
	CHECK(Has(line, "has no DllGetRenderEventsVersion export") && !SrvRenderEvents::DllSends());
}

static void TestFeedOff()
{
	g_test = "feed off";
	Fresh(false);
	VP64RenderEvent(1000, 5, 0, 7, 5, 0);
	VP64RenderEvent(1001, 5, 0, 8, 3, 0);
	const SrvRenderEvents::Counters c = SrvRenderEvents::GetCounters();
	CHECK(c.received == 2 && c.activity == 1 && c.carrier == 1 && c.badKind == 0 && c.setup == 0);
	SrvFeed::SetEnabled(true);
	CHECK(Drained().empty());   // nothing was queued while the feed was off
}

static void TestFeedOn()
{
	g_test = "feed on";
	Fresh(true);
	SrvRenderEvents::SetTurn(42);
	VP64RenderEvent(1000, 77, 3, 12, 5, 0);       // intercept (5) from awake (0)
	VP64RenderEvent(1000, 78, 3, 13, 2, 2);       // at setup: sleep == sleep
	VP64RenderEvent(1001, -1, 3, 14, 99, 3);      // loaded onto unit 99 of player 3, no plot
	VP64RenderEvent(1500, 1, 2, 3, 4, 5);         // a kind of the range this server does not know: pushed, counted
	VP64RenderEvent(999, 1, 0, 0, 0, 0);          // outside the range: refused
	VP64RenderEvent(2000, 1, 0, 0, 0, 0);
	VP64RenderEvent(-1, 1, 0, 0, 0, 0);
	VP64RenderEvent(65536 + 1000, 1, 0, 0, 0, 0); // would be 1000 as a uint16_t: refused before the cast
	VP64RenderEvent(0, 1, 0, 0, 0, 0);
	VP64RenderEvent(SrvFeed::kUnitCreated, 1, 0, 0, 0, 0);   // a stock kind is never accepted from the DLL
	const SrvRenderEvents::Counters c = SrvRenderEvents::GetCounters();
	CHECK(c.received == 10);
	CHECK(c.activity == 2 && c.setup == 1 && c.carrier == 1 && c.unknownKind == 1 && c.badKind == 6);
	const std::vector<SrvFeed::Event> v = Drained();
	CHECK(v.size() == 4);
	if (v.size() == 4)
	{
		CHECK(v[0].kind == 1000 && v[0].plot == 77 && v[0].turn == 42 && v[0].flags == 0 && v[0].a[0] == 3 &&
		      v[0].a[1] == 12 && v[0].a[2] == 5 && v[0].a[3] == 0);
		CHECK(v[1].kind == 1000 && v[1].plot == 78 && v[1].a[2] == 2 && v[1].a[3] == 2);
		CHECK(v[2].kind == 1001 && v[2].plot == -1 && v[2].a[1] == 14 && v[2].a[2] == 99 && v[2].a[3] == 3);
		CHECK(v[3].kind == 1500 && v[3].a[3] == 5);
		CHECK(v[0].seq < v[1].seq && v[1].seq < v[2].seq && v[2].seq < v[3].seq);
	}
	const std::string s = SrvRenderEvents::Summary();
	CHECK(Has(s, "LOUD") && Has(s, "10 calls") && Has(s, "activity 2 (at setup 1)") && Has(s, "carrier 1") &&
	      Has(s, "unknown kinds 1") && Has(s, "outside 1000-1999 6"));
	// ordered with the stock events by seq
	SrvFeed::Event st = {};
	st.kind = SrvFeed::kFeatureChanged;
	st.plot = 9;
	SrvFeed::Push(st);
	VP64RenderEvent(1001, 9, 1, 2, -1, -1);   // unloaded
	SrvFeed::Batch b;
	SrvFeed::Drain(b);
	CHECK(b.events.size() == 2 && b.events[0].kind == SrvFeed::kFeatureChanged && b.events[1].kind == 1001 &&
	      b.events[0].seq < b.events[1].seq && b.events[1].a[2] == -1 && b.events[1].a[3] == -1);
}

static void TestSummaryQuiet()
{
	g_test = "summary";
	Fresh(true);
	SrvRenderEvents::Apply(NULL);   // the DLL does not send
	CHECK(SrvRenderEvents::Summary().empty());
	VP64RenderEvent(1000, 1, 0, 1, 2, 0);
	const std::string s = SrvRenderEvents::Summary();
	CHECK(!s.empty() && !Has(s, "LOUD") && Has(s, "1 calls"));
}

static const int kThreads = 4, kEach = 100000;   // namespace scope: MSVC refuses an implicit lambda capture of locals

static void TestThreads()
{
	g_test = "threads";
	Fresh(true);
	SrvFeed::SetQueueCap(1u << 22);
	std::atomic<bool> stop(false);
	std::atomic<uint64_t> drained(0);
	bool ordered = true;
	std::thread reader([&] {
		uint32_t last = 0;
		while (!stop.load())
		{
			SrvFeed::Batch b;
			if (SrvFeed::Drain(b))
				for (const SrvFeed::Event& e : b.events)
				{
					if (e.kind == 1000)
						drained++;
					if (e.seq <= last)
						ordered = false;
					last = e.seq;
				}
			Sleep(0);
		}
		SrvFeed::Batch b;
		SrvFeed::Drain(b);
		for (const SrvFeed::Event& e : b.events)
		{
			if (e.kind == 1000)
				drained++;
			if (e.seq <= last)
				ordered = false;
			last = e.seq;
		}
	});
	std::vector<std::thread> ws;
	for (int t = 0; t < kThreads; ++t)
		ws.push_back(std::thread([t] {
			for (int i = 0; i < kEach; ++i)
				VP64RenderEvent(1000, i, t, i, 1, 0);
		}));
	for (std::thread& w : ws)
		w.join();
	stop = true;
	reader.join();
	const SrvRenderEvents::Counters c = SrvRenderEvents::GetCounters();
	const uint64_t dropped = SrvFeed::GetCounters().dropped;
	CHECK(c.received == (uint64_t)kThreads * kEach && c.activity == (uint64_t)kThreads * kEach);
	CHECK(drained.load() + dropped == (uint64_t)kThreads * kEach);
	CHECK(ordered);
	printf("threads: %d x %d sends, drained %llu, dropped %llu\n", kThreads, kEach,
	       (unsigned long long)drained.load(), (unsigned long long)dropped);
	SrvFeed::SetQueueCap(65536);
}

typedef int (*ActiveFn)();
typedef void (*SendFn)(int, int, int, int, int, int);
typedef unsigned (*VersionFn)();

static void TestStandins(const char* on, const char* none, const char* v2, bool bench)
{
	g_test = "standin on";
	Fresh(true);
	HMODULE h = LoadLibraryA(on);
	CHECK(h != NULL);
	if (!h)
		return;
	ActiveFn active = (ActiveFn)(void*)GetProcAddress(h, "StandinActive");
	SendFn send = (SendFn)(void*)GetProcAddress(h, "StandinSend");
	VersionFn ver = (VersionFn)(void*)GetProcAddress(h, "DllGetRenderEventsVersion");
	CHECK(active && send && ver);
	if (!active || !send || !ver)
		return;
	CHECK(active() == 1);   // the real .inl found this EXE's export at the DLL's load
	CHECK(ver() == 1);
	const std::string line = SrvRenderEvents::Apply(h);
	CHECK(SrvRenderEvents::DllSends() && Has(line, "sends render messages, version 1"));
	send(1001, 321, 2, 55, 17, 2);
	send(1000, 321, 2, 55, 5, 0);
	const std::vector<SrvFeed::Event> v = Drained();
	CHECK(v.size() == 2 && v[0].kind == 1001 && v[0].plot == 321 && v[0].a[1] == 55 && v[0].a[2] == 17 && v[1].kind == 1000 &&
	      v[1].a[2] == 5);
	const SrvRenderEvents::Counters c = SrvRenderEvents::GetCounters();
	CHECK(c.received == 2 && c.carrier == 1 && c.activity == 1);

	if (bench)
	{
		// R5: the cost of one send through the DLL's macro (an indirect call into this EXE, the counters, Push), with the
		// feed on (a reader draining every millisecond, as the render loop does) and off.
		for (int pass = 0; pass < 2; ++pass)
		{
			const bool feedOn = pass == 0;
			Fresh(feedOn);
			SrvFeed::SetQueueCap(1u << 22);   // no drop between two drains: the measured path is the queued one
			const uint64_t dropped0 = SrvFeed::GetCounters().dropped;
			std::atomic<bool> stop(false);
			std::thread reader([&] {
				while (!stop.load())
				{
					SrvFeed::Batch b;
					SrvFeed::Drain(b);
					Sleep(1);
				}
			});
			const int n = 2000000;
			LARGE_INTEGER f, t0, t1;
			QueryPerformanceFrequency(&f);
			QueryPerformanceCounter(&t0);
			for (int i = 0; i < n; ++i)
				send(1000, i & 0xFFFF, 1, i, 2, 0);
			QueryPerformanceCounter(&t1);
			stop = true;
			reader.join();
			const double ns = (double)(t1.QuadPart - t0.QuadPart) * 1e9 / (double)f.QuadPart / n;
			const uint64_t dropped = SrvFeed::GetCounters().dropped - dropped0;
			printf("BENCH feed=%s sends=%d ns_per_send=%.1f dropped=%llu\n", feedOn ? "on" : "off", n, ns,
			       (unsigned long long)dropped);
			if (feedOn)
				CHECK(dropped == 0);
		}
		SrvFeed::SetQueueCap(65536);
	}

	g_test = "standin none";
	HMODULE hn = LoadLibraryA(none);
	CHECK(hn != NULL);
	if (hn)
	{
		CHECK(GetProcAddress(hn, "DllGetRenderEventsVersion") == NULL);
		const std::string ln = SrvRenderEvents::Apply(hn);
		CHECK(!SrvRenderEvents::DllSends() && Has(ln, "has no DllGetRenderEventsVersion export"));
	}
	g_test = "standin v2";
	HMODULE h2 = LoadLibraryA(v2);
	CHECK(h2 != NULL);
	if (h2)
	{
		const std::string l2 = SrvRenderEvents::Apply(h2);
		CHECK(!SrvRenderEvents::DllSends() && Has(l2, "LOUD") && Has(l2, "version 2"));
	}
}

int main(int argc, char** argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	TestKinds();
	TestDecide();
	TestFeedOff();
	TestFeedOn();
	TestSummaryQuiet();
	TestThreads();
	bool bench = false;
	std::vector<const char*> dlls;
	for (int i = 1; i < argc; ++i)
	{
		if (strcmp(argv[i], "--bench") == 0)
			bench = true;
		else
			dlls.push_back(argv[i]);
	}
	if (dlls.size() == 3)
		TestStandins(dlls[0], dlls[1], dlls[2], bench);
	else
		printf("stand-ins: not given (%zu paths)\n", dlls.size());
#ifdef RE_TEST_INJECT_FAIL
	g_test = "injected";
	CHECK(false);
#endif
	printf("render events tests: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
