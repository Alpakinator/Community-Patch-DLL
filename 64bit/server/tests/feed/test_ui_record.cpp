// test_ui_record.cpp - the server's side of the bridge's ui records (issue 4754, epic 0040; issue 0178 for the record):
// SrvRemote::UiRec of server/src/remote/srv_remote.h (decode, the feed event's layout, the counters and the report line)
// and the event's way through SrvFeed::Push / Drain (server/src/feed/srv_feed.cpp). Written 2026-11-17 for this
// repository (GPL-3.0-or-later).
// Built and run by server/tests/feed/run_ui_record_test.py (clang-cl x64 /W4 /WX, and MSVC /fsanitize=address).
// Prints "test_ui_record: N passed, M failed"; exit 0 only when every check passed. -DFEED_TEST_INJECT_FAIL adds one
// failing check (the runner's negative test).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define SRV_REMOTE_UI_RECORD_ONLY
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "../../src/remote/srv_remote.h"

namespace
{
int g_pass = 0, g_fail = 0;
const char* g_test = "";
#define CHECK(cond)                                                                                                   \
	do                                                                                                                \
	{                                                                                                                 \
		if (cond) ++g_pass;                                                                                           \
		else { ++g_fail; printf("FAIL %s: %s (line %d)\n", g_test, #cond, __LINE__); }                                \
	} while (0)

using SrvRemote::UiRec::Fields;
namespace U = SrvRemote::UiRec;

// The bytes of one record as the proxy's UiBridge::Record lays them out.
std::vector<uint8_t> Bytes(uint16_t version, uint16_t kind, uint32_t flags, int32_t a0, int32_t a1, int32_t a2, int32_t a3,
                           uint32_t seq, uint32_t gen)
{
	std::vector<uint8_t> b(32, 0);
	const int32_t a[4] = { a0, a1, a2, a3 };
	memcpy(&b[0], &version, 2);
	memcpy(&b[2], &kind, 2);
	memcpy(&b[4], &flags, 4);
	memcpy(&b[8], a, 16);
	memcpy(&b[24], &seq, 4);
	memcpy(&b[28], &gen, 4);
	return b;
}

int32_t FB(float f)
{
	int32_t b;
	memcpy(&b, &f, sizeof(b));
	return b;
}

Fields Rec(uint16_t kind, uint32_t flags, int32_t a0, int32_t a1, int32_t a2, int32_t a3, uint32_t seq)
{
	Fields r;
	memset(&r, 0, sizeof(r));
	r.version = 1;
	r.kind = kind;
	r.flags = flags;
	r.a[0] = a0;
	r.a[1] = a1;
	r.a[2] = a2;
	r.a[3] = a3;
	r.seq = seq;
	r.gen = 1;
	return r;
}

uint32_t PosFlags(uint32_t player, bool arrival)
{
	return (player << U::kPosPlayerShift) | (arrival ? (uint32_t)U::kPosArrival : 0u);
}

bool Has(const std::string& s, const char* needle) { return s.find(needle) != std::string::npos; }

void TestDecode()
{
	g_test = "decode";
	Fields r;
	std::vector<uint8_t> b = Bytes(1, 6, PosFlags(5, true), 17542, FB(2303.9f), FB(-831.2f), FB(12.04f), 41, 3);
	CHECK(U::Decode(b.data(), 32, r));
	CHECK(r.version == 1 && r.kind == 6 && r.flags == PosFlags(5, true) && r.a[0] == 17542 && r.a[1] == FB(2303.9f) &&
	      r.a[2] == FB(-831.2f) && r.a[3] == FB(12.04f) && r.seq == 41 && r.gen == 3);
	// negative: a short or long body, another version, nothing
	CHECK(!U::Decode(b.data(), 31, r) && r.version == 1);
	std::vector<uint8_t> longer = b;
	longer.push_back(0);
	CHECK(!U::Decode(longer.data(), 33, r));
	std::vector<uint8_t> v2 = Bytes(2, 6, 0, 0, 0, 0, 0, 1, 1);
	CHECK(!U::Decode(v2.data(), 32, r) && r.version == 2);
	CHECK(!U::Decode(NULL, 32, r) && r.version == 0);
	CHECK(!U::Decode(b.data(), 1, r) && r.version == 0 && r.kind == 0);
	CHECK(!U::Decode(b.data(), 0, r));
}

void TestToEvent()
{
	g_test = "event";
	// kind 6, a position of player 5
	SrvFeed::Event e = U::ToEvent(Rec(6, PosFlags(5, false), 17542, FB(1.5f), FB(2.5f), FB(-3.25f), 1));
	CHECK(e.kind == SrvFeed::kUiMirror && e.flags == 6 && e.plot == 5 && e.turn == 0 && e.a[0] == 17542 &&
	      e.a[1] == FB(1.5f) && e.a[2] == FB(2.5f) && e.a[3] == FB(-3.25f));
	// kind 6, an arrival of player 63 (the barbarians) and of player 255 (the top of the 8 bits)
	e = U::ToEvent(Rec(6, PosFlags(63, true), 9, 1, 2, 3, 2));
	CHECK(e.plot == 63 && e.turn == 1 && e.flags == 6 && e.a[0] == 9);
	e = U::ToEvent(Rec(6, PosFlags(255, true), 9, 1, 2, 3, 2));
	CHECK(e.plot == 255 && e.turn == 1);
	e = U::ToEvent(Rec(6, PosFlags(0, false), 9, 1, 2, 3, 2));
	CHECK(e.plot == 0 && e.turn == 0);
	// kind 6 with stray bits outside the player and arrival bits: only bits 16-23 and 24 are read
	e = U::ToEvent(Rec(6, PosFlags(7, false) | 0x3FFu | 0xFE000000u, 9, 1, 2, 3, 2));
	CHECK(e.plot == 7 && e.turn == 0);
	// kind 7, a frame mark: plot and turn stay -1
	e = U::ToEvent(Rec(7, 0, 812, 3, 0, 0, 3));
	CHECK(e.kind == SrvFeed::kUiMirror && e.flags == 7 && e.plot == -1 && e.turn == -1 && e.a[0] == 812 && e.a[1] == 3);
	// kinds 1-5 as before (0178): the record's argument flags are not carried, plot and turn -1
	e = U::ToEvent(Rec(1, (1u << 9) | (1u << 1), 10, 11, 12, 13, 4));
	CHECK(e.flags == 1 && e.plot == -1 && e.turn == -1 && e.a[0] == 10 && e.a[3] == 13);
	e = U::ToEvent(Rec(4, 0, 1, 0, 0, 0, 5));
	CHECK(e.flags == 4 && e.plot == -1 && e.turn == -1 && e.a[0] == 1);
	// a kind 6 bit pattern on another kind is not read
	e = U::ToEvent(Rec(5, PosFlags(5, true), 1, 2, 1, 0, 6));
	CHECK(e.plot == -1 && e.turn == -1);
}

void TestFeed()
{
	g_test = "feed";
	SrvFeed::Batch b;
	SrvFeed::SetEnabled(false);
	SrvFeed::Push(U::ToEvent(Rec(7, 0, 1, 1, 0, 0, 1)));
	CHECK(!SrvFeed::Drain(b) || b.events.empty());   // disabled: a no-op, as with render=0
	SrvFeed::SetEnabled(true);
	SrvFeed::Drain(b);
	SrvFeed::Push(U::ToEvent(Rec(7, 0, 812, 2, 0, 0, 10)));
	SrvFeed::Push(U::ToEvent(Rec(6, PosFlags(5, false), 17542, FB(1.0f), FB(2.0f), FB(3.0f), 11)));
	SrvFeed::Push(U::ToEvent(Rec(6, PosFlags(5, true), 17542, FB(1.0f), FB(2.0f), FB(3.0f), 12)));
	CHECK(SrvFeed::Drain(b));
	CHECK(b.events.size() == 3);
	if (b.events.size() == 3)
	{
		const SrvFeed::Event* v = b.events.data();
		CHECK(v[1].seq == v[0].seq + 1 && v[2].seq == v[1].seq + 1);
		CHECK(v[0].kind == SrvFeed::kUiMirror && v[0].flags == 7 && v[0].plot == -1 && v[0].a[1] == 2);
		CHECK(v[1].flags == 6 && v[1].plot == 5 && v[1].turn == 0 && v[1].a[0] == 17542 && v[1].a[3] == FB(3.0f));
		CHECK(v[2].flags == 6 && v[2].plot == 5 && v[2].turn == 1);
	}
	SrvFeed::SetEnabled(false);
}

void TestTally()
{
	g_test = "tally";
	U::Tally t = {};
	CHECK(t.Add(Rec(7, 0, 1, 3, 0, 0, 1)) == 1);
	CHECK(t.Add(Rec(6, PosFlags(5, false), 1, 0, 0, 0, 2)) == 1);
	CHECK(t.Add(Rec(6, PosFlags(5, false), 1, 0, 0, 0, 3)) == 2);
	CHECK(t.Add(Rec(6, PosFlags(5, true), 1, 0, 0, 0, 4)) == 3);
	t.Add(Rec(1, 0, 0, 0, 0, 0, 5));
	t.Add(Rec(9, 0, 0, 0, 0, 0, 6));      // a kind above the known ones
	t.Add(Rec(0, 0, 0, 0, 0, 0, 7));
	CHECK(t.Bad() == 1);
	std::string s = t.Summary();
	CHECK(t.total == 7 && t.kinds[6] == 3 && t.arrivals == 1 && t.kinds[7] == 1 && t.announced == 3 && t.seqGaps == 0);
	CHECK(Has(s, "ui records (issue 0178): 7 received (arrow 1, remove-arrows 0, minimap 0, movement 0, unit-moving 0, "
	             "unit-position 3 (positions 2, arrivals 1), frame-mark 1 (announcing 3), other 2), bad 1, sequence gaps 0, "
	             "last seq 7 gen 1"));
	// a gap in the proxy's sequence is counted
	t.Add(Rec(6, PosFlags(5, false), 1, 0, 0, 0, 9));
	CHECK(t.seqGaps == 1 && Has(t.Summary(), "sequence gaps 1, last seq 9"));
	// a mark with a negative count announces nothing
	t.Add(Rec(7, 0, 2, -4, 0, 0, 10));
	CHECK(t.announced == 3 && t.kinds[7] == 2);
	// an empty tally
	U::Tally z = {};
	CHECK(Has(z.Summary(), "0 received (arrow 0, remove-arrows 0, minimap 0, movement 0, unit-moving 0, unit-position 0 "
	                       "(positions 0, arrivals 0), frame-mark 0 (announcing 0), other 0), bad 0"));
}
}  // namespace

int main()
{
	TestDecode();
	TestToEvent();
	TestFeed();
	TestTally();
#ifdef FEED_TEST_INJECT_FAIL
	g_test = "inject";
	CHECK(!"injected");
	printf("FAIL injected (FEED_TEST_INJECT_FAIL)\n");
#endif
	printf("test_ui_record: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail == 0 ? 0 : 1;
}
