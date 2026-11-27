// test_units_queue.cpp - tests of the per-unit hold and the held view (server/src/units/srv_units_queue.*; epic 0042,
// issue 4774). Written 2026-11-20 for this repository (GPL-3.0-or-later). Built and run by run_units_queue_test.py.
//
//   test_units_queue.exe        every check; "test_units_queue: <n> passed, <m> failed", exit 0 only when m == 0
//
// The sequences follow the EXE's queue as combat-choreography.md 3.4 describes it (0321): one unit fighting twice in a row
// (fight A, its damage, death and advance, then fight B), a uninvolved unit passing, and the negative cases: an end for an
// unknown fight, a load during a fight, a unit queue and the total over their limits.
#include "units/srv_units_queue.h"

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

using namespace srvunits;
namespace cu = c5r::units;

static int g_pass = 0, g_fail = 0;
#define CHECK(c)                                                                                                         \
	do                                                                                                                   \
	{                                                                                                                    \
		if(c)                                                                                                            \
			++g_pass;                                                                                                    \
		else                                                                                                             \
		{                                                                                                                \
			++g_fail;                                                                                                    \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                          \
		}                                                                                                                \
	} while(0)

static HeldOp Op(uint32_t seq, uint16_t kind, int owner, int id, int a2 = 0, int a3 = 0, int dest = -1)
{
	HeldOp o;
	o.seq = seq;
	o.kind = kind;
	o.owner = owner;
	o.id = id;
	o.a2 = a2;
	o.a3 = a3;
	o.dest = dest;
	return o;
}

static std::string Seqs(const std::vector<HeldOp>& v)
{
	std::string s;
	for(const HeldOp& o : v)
		s += (s.empty() ? "" : ",") + std::to_string(o.seq);
	return s;
}

static UnitHold::Result Take(UnitHold& h, const HeldOp& o)
{
	std::vector<HeldOp> fl;
	std::string why;
	return h.take(o, fl, why);
}

static void TestWaitsKinds()
{
	const uint16_t yes[] = {12, 13, 14, 15, 16, 30, kHoldArrive};
	for(uint16_t k : yes)
		CHECK(UnitHold::waits(k));
	const uint16_t no[] = {11, 17, 18, 19, 20, 21, 22, 23, 24, 25, 28, 29, 31, 32, 33, 101, 102};
	for(uint16_t k : no)
		CHECK(!UnitHold::waits(k));
}

// One unit U (0, 1) fights A (seq 10) against V (1, 7), then B (seq 20) against W (1, 8); X (2, 9) is not involved.
static void TestTwoFightsOneUnit()
{
	UnitHold h;
	CHECK(Take(h, Op(5, 12, 0, 1)) == UnitHold::Pass);   // before any fight: passes
	h.begin(0, 1, 10);
	h.begin(1, 7, 10);
	CHECK(h.held(0, 1) && h.held(1, 7) && !h.held(1, 8) && !h.held(2, 9));
	CHECK(Take(h, Op(11, 16, 1, 7, 100, 60)) == UnitHold::Held);   // V's damage
	CHECK(Take(h, Op(12, 15, 1, 7)) == UnitHold::Held);            // V died in combat
	CHECK(Take(h, Op(13, 14, 1, 7)) == UnitHold::Held);            // V removed
	CHECK(Take(h, Op(14, 16, 0, 1, 20, 10)) == UnitHold::Held);    // U's damage
	CHECK(Take(h, Op(15, 12, 0, 1, 0, 0, 33)) == UnitHold::Held);  // U advances
	CHECK(Take(h, Op(16, 17, 0, 1, 1)) == UnitHold::Pass);         // a fortify does not wait
	CHECK(Take(h, Op(17, 12, 2, 9)) == UnitHold::Pass);            // another unit passes
	h.begin(0, 1, 20);
	h.begin(1, 8, 20);
	CHECK(Take(h, Op(21, 16, 0, 1, 40, 20)) == UnitHold::Held);    // U's damage of B: owned by B
	CHECK(Take(h, Op(22, 16, 1, 8, 30, 0)) == UnitHold::Held);
	HeldOp arrive = Op(0, kHoldArrive, 0, 1);
	CHECK(Take(h, arrive) == UnitHold::Held);
	CHECK(h.queued() == 8 && h.queued(0, 1) == 4 && h.queued(1, 7) == 3);
	// A ends: V's three and U's two of A come back, in order; U's B part stays; V is free, U still held by B
	std::vector<HeldOp> out;
	CHECK(h.end(10, out));
	CHECK(Seqs(out) == "14,15,11,12,13");   // units in key order: (0, 1) before (1, 7); each unit in its own order
	CHECK(h.held(0, 1) && !h.named(1, 7) && h.held(1, 8));
	CHECK(h.queued() == 3 && h.queued(0, 1) == 2);
	out.clear();
	CHECK(h.end(20, out));
	CHECK(Seqs(out) == "21,0,22" && h.queued() == 0 && h.units() == 0 && !h.held(0, 1));
	CHECK(h.counters().held == 8 && h.counters().released == 8 && h.counters().passedHeldUnit == 1);
	CHECK(h.counters().maxUnit == 4 && h.counters().maxTotal == 8);
	// after the end everything passes
	CHECK(Take(h, Op(30, 14, 0, 1)) == UnitHold::Pass);
}

// The fights on one unit end out of order (a reset / unit-gone end of B first): what A owns still waits for A.
static void TestOutOfOrderEnd()
{
	UnitHold h;
	h.begin(0, 1, 10);
	CHECK(Take(h, Op(11, 16, 0, 1, 10)) == UnitHold::Held);
	h.begin(0, 1, 20);
	CHECK(Take(h, Op(21, 16, 0, 1, 20)) == UnitHold::Held);
	std::vector<HeldOp> out;
	CHECK(h.end(20, out));
	CHECK(out.empty() && h.held(0, 1) && h.queued(0, 1) == 2);   // B's op is behind A's: it waits with it
	CHECK(h.end(10, out));
	CHECK(Seqs(out) == "11,21" && !h.named(0, 1));
}

static void TestUnknownEnd()
{
	UnitHold h;
	std::vector<HeldOp> out;
	CHECK(!h.end(99, out) && out.empty() && h.counters().endsUnknown == 1);
	h.begin(0, 1, 10);
	CHECK(Take(h, Op(11, 16, 0, 1)) == UnitHold::Held);
	CHECK(!h.end(98, out) && out.empty() && h.queued() == 1 && h.counters().endsUnknown == 2);
	CHECK(h.end(10, out) && out.size() == 1);
	CHECK(!h.end(10, out) && h.counters().endsUnknown == 3);   // an end taken twice: the second is unknown
	h.begin(5, -1, 30);   // a city side is no unit
	CHECK(h.units() == 0);
}

static void TestLoadDuringFight()
{
	UnitHold h;
	h.begin(0, 1, 10);
	h.begin(1, 2, 10);
	CHECK(Take(h, Op(11, 16, 0, 1)) == UnitHold::Held);
	CHECK(Take(h, Op(12, 14, 1, 2)) == UnitHold::Held);
	h.clear();
	CHECK(h.queued() == 0 && h.units() == 0 && !h.held(0, 1) && h.counters().droppedLoad == 2 && h.counters().clears == 1);
	CHECK(Take(h, Op(13, 16, 0, 1)) == UnitHold::Pass);
	std::vector<HeldOp> out;
	CHECK(!h.end(10, out) && out.empty());   // the clock's Reset end after the load: nothing left
}

static void TestOverflowUnit()
{
	UnitHold h;
	h.begin(0, 1, 10);
	for(uint32_t i = 0; i < kHoldUnitMax; ++i)
		CHECK(Take(h, Op(100 + i, 16, 0, 1, (int)i)) == UnitHold::Held);
	std::vector<HeldOp> fl;
	std::string why;
	CHECK(h.take(Op(1000, 16, 0, 1), fl, why) == UnitHold::Overflow);
	CHECK(fl.size() == kHoldUnitMax && fl.front().seq == 100 && fl.back().seq == 100 + kHoldUnitMax - 1);
	CHECK(why.find("its held ops reached 256") != std::string::npos && why.find("unit (0, 1)") != std::string::npos);
	CHECK(h.queued() == 0 && h.named(0, 1) && !h.held(0, 1) && h.counters().overflowUnit == 1 && h.counters().flushed == kHoldUnitMax);
	CHECK(Take(h, Op(1001, 16, 0, 1)) == UnitHold::Pass && h.counters().passedBroken == 1);   // broken: passes
	std::vector<HeldOp> out;
	CHECK(h.end(10, out) && out.empty() && !h.named(0, 1));
	h.begin(0, 1, 20);   // a new fight holds it again
	CHECK(Take(h, Op(1002, 16, 0, 1)) == UnitHold::Held);
}

static void TestOverflowTotal()
{
	UnitHold h;
	const uint32_t units = (uint32_t)(kHoldTotalMax / 128);
	for(uint32_t u = 0; u < units; ++u)
	{
		h.begin(0, (int)u, 10);
		for(uint32_t i = 0; i < 128; ++i)
			CHECK(Take(h, Op(1 + u * 128 + i, 16, 0, (int)u)) == UnitHold::Held);
	}
	CHECK(h.queued() == kHoldTotalMax);
	h.begin(0, 999, 10);
	std::vector<HeldOp> fl;
	std::string why;
	CHECK(h.take(Op(900000, 16, 0, 999), fl, why) == UnitHold::Overflow);
	CHECK(fl.empty() && why.find("all held ops reached 8192") != std::string::npos && h.counters().overflowTotal == 1);
	CHECK(h.take(Op(900001, 16, 0, 5), fl, why) == UnitHold::Overflow && fl.size() == 128);   // the next one's unit too
	std::vector<HeldOp> out;
	CHECK(h.end(10, out) && out.size() == kHoldTotalMax - 128 && h.queued() == 0 && h.units() == 0);
}

static cu::UnitScene Scene(std::vector<cu::UnitInput> units)
{
	cu::UnitScene s;
	s.units = std::move(units);
	cu::PlayerInput p;
	p.player = 1;
	p.colour = "C1";
	s.players.push_back(p);
	cu::PlayerInput q;
	q.player = 0;
	q.colour = "C0";
	s.players.push_back(q);
	return s;
}

static cu::UnitInput Unit(int player, int id, int x, int y, int damage, int facing = 2)
{
	cu::UnitInput u;
	u.player = player;
	u.id = id;
	u.unit_type = "UNIT_WARRIOR";
	u.plot_x = x;
	u.plot_y = y;
	u.damage = (int16_t)damage;
	u.facing = (int8_t)facing;
	u.formation_seed = 7;
	u.flags = cu::kFlagVisible;
	return u;
}

static void TestView()
{
	HoldView v;
	// frame 1: U (0, 1) at (2, 2) and V (1, 7) at (3, 2), both in copy 1; V also in copy 2 (the wrap copy +1)
	v.beginFrame();
	cu::UnitScene c0 = Scene({}), c1 = Scene({Unit(0, 1, 2, 2, 0), Unit(1, 7, 3, 2, 60)}), c2 = Scene({Unit(1, 7, 3, 2, 60)});
	CHECK(v.nextCopy() == 0);
	v.applyTo(c0, 0);
	v.remember(c0, 0);
	CHECK(v.nextCopy() == 1);
	v.applyTo(c1, 1);
	v.remember(c1, 1);
	CHECK(v.nextCopy() == 2);
	v.applyTo(c2, 2);
	v.remember(c2, 2);
	CHECK(v.nextCopy() == 2);   // a fourth call stays at the last copy
	// frame 2: the batch had the fight; the table is final: V removed, U advanced to (3, 2) with damage 20
	v.beginFrame();
	CHECK(v.freeze(0, 1) && v.freeze(1, 7) && v.freeze(0, 1));
	CHECK(!v.freeze(4, 4) && v.counters().missing == 1);   // not listed last frame: drawn from the table
	CHECK(v.views() == 2 && v.frozen(0, 1) && !v.frozen(4, 4));
	cu::UnitScene d1 = Scene({Unit(0, 1, 3, 2, 20, 1)}), d2 = Scene({});
	d2.players.clear();
	cu::UnitScene d0 = Scene({});
	v.applyTo(d0, 0);
	v.applyTo(d1, 1);
	v.applyTo(d2, 2);
	CHECK(d0.units.empty());
	CHECK(d1.units.size() == 2 && d1.units[0].plot_x == 2 && d1.units[0].damage == 0 && d1.units[0].facing == 2);   // U frozen
	CHECK(d1.units[1].player == 1 && d1.units[1].id == 7 && d1.units[1].plot_x == 3 && d1.units[1].damage == 60);   // V relisted
	CHECK(d2.units.size() == 1 && d2.units[0].id == 7 && d2.players.size() == 1 && d2.players[0].colour == "C1");   // its row
	CHECK(v.counters().substituted == 1 && v.counters().relisted == 2);
	// the fight ends: its ops come back and change the views; V's removal ends its listing
	v.apply(Op(11, 16, 1, 7, 100, 60), 10, -1);
	v.apply(Op(12, 15, 1, 7), 10, -1);
	v.apply(Op(14, 16, 0, 1, 20, 0), 10, -1);
	v.apply(Op(15, 12, 0, 1, 0, 0, 23), 10, 1);   // the path's last plot 23 = (3, 2) on a 10-wide map; facing 1
	v.apply(Op(99, 17, 0, 1, 1), 10, -1);          // a kind the view does not take: unchanged
	cu::UnitScene e1 = Scene({Unit(0, 1, 3, 2, 20, 1)});
	v.applyTo(e1, 1);
	CHECK(e1.units.size() == 2 && e1.units[0].plot_x == 3 && e1.units[0].plot_y == 2 && e1.units[0].damage == 20 && e1.units[0].facing == 1);
	CHECK(e1.units[1].damage == 100 && (e1.units[1].flags & cu::kFlagDead));
	v.apply(Op(13, 14, 1, 7), 10, -1);
	cu::UnitScene f1 = Scene({Unit(0, 1, 3, 2, 20, 1)});
	v.applyTo(f1, 1);
	CHECK(f1.units.size() == 1 && v.counters().applied == 5);
	v.apply(Op(16, 13, 0, 1, 45), 10, 4);   // teleport to plot 45 = (5, 4)
	v.apply(Op(17, 30, 0, 1, 45, 51), 10, 3);   // rebase to plot 51 = (1, 5)
	cu::UnitScene g1 = Scene({Unit(0, 1, 3, 2, 20, 1)});
	v.applyTo(g1, 1);
	CHECK(g1.units[0].plot_x == 1 && g1.units[0].plot_y == 5 && g1.units[0].facing == 3);
	v.release(0, 1);
	v.release(1, 7);
	v.release(1, 7);
	CHECK(v.views() == 0 && v.counters().released == 2);
	cu::UnitScene h1 = Scene({Unit(0, 1, 3, 2, 20, 1)});
	v.applyTo(h1, 1);
	CHECK(h1.units.size() == 1 && h1.units[0].plot_x == 3);   // the table again
	v.clear();
	CHECK(!v.freeze(0, 1));   // the load cleared the remembered frame too
}

int main()
{
	TestWaitsKinds();
	TestTwoFightsOneUnit();
	TestOutOfOrderEnd();
	TestUnknownEnd();
	TestLoadDuringFight();
	TestOverflowUnit();
	TestOverflowTotal();
	TestView();
#ifdef QUEUE_TEST_INJECT_FAIL
	CHECK(!"injected");
	printf("FAIL injected\n");
#endif
	printf("test_units_queue: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
