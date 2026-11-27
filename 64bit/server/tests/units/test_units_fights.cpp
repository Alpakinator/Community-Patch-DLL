// test_units_fights.cpp - tests of the server's fight clock (server/src/units/srv_units_fights.*; epic 0042, issue 4782).
// Written 2026-11-20 for this repository (GPL-3.0-or-later). Built and run by run_units_fights_test.py.
//
//   test_units_fights.exe        every check; "test_units_fights: <n> passed, <m> failed", exit 0 only when m == 0
//
// The recorded AI turn is 0477's stock run (server/evidence/0477/logs/aiturn-stock-ai1.log: two AI Heavy Bombers on one
// human submarine, then two barbarian attacks on one human unit, all while the human watched the AI turn) and the same
// turn with 0477's DLL fix (aiturn-fixv2-ai1.log); the fights' lengths stand in for the choreography (a test double),
// so the clock's order, barrier, limit and OwnerEnd rules are what is tested.
#include "units/srv_units_fights.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

using namespace srvunits;
using SrvFeed::CombatRecord;
using SrvFeed::CombatMember;

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

static const double kDt = 1.0 / 30.0;

// The choreography's double: per seq a length (seconds; < 0 = never ends by itself), an air flag, a refusal.
struct Plan
{
	double length = 3.0;
	bool air = false;
	bool refuse = false;
};
class FakePlayer : public FightPlayer
{
public:
	std::map<uint32_t, Plan> plans;
	std::vector<uint32_t> startedSeq, endedSeq;
	std::vector<FightEnd> endedWhy;
	std::map<uint32_t, uint32_t> ownerAtStart;                // issue 4783: f.owner as start() saw it (0: not known)
	std::vector<std::pair<uint32_t, uint32_t>> ownerKnownCalls;   // issue 4783: (seq, owner) of ownerKnown()
	void ownerKnown(const Fight& f) override { ownerKnownCalls.push_back(std::make_pair(f.seq, f.owner)); }
	FightPlan start(const Fight& f) override
	{
		startedSeq.push_back(f.seq);
		ownerAtStart[f.seq] = f.ownerKnown ? f.owner : 0;
		const Plan p = plans.count(f.seq) ? plans[f.seq] : Plan();
		FightPlan r;
		r.ok = !p.refuse;
		r.air = p.air;
		if(p.refuse)
			r.why = "AirCombat: an air member (4777) [test]";
		return r;
	}
	bool advance(const Fight& f, double dt) override
	{
		const Plan p = plans.count(f.seq) ? plans[f.seq] : Plan();
		advanced[f.seq] += dt;   // ticket 0796: the player's own time (the sum of the steps it was given)
		return p.length >= 0 && f.elapsed + 1e-9 >= p.length;
	}
	std::map<uint32_t, double> rates;      // ticket 0796: a fight's playback rate (absent: the default, 1)
	std::map<uint32_t, double> advanced;   // ticket 0796
	double rate(const Fight& f) override { return rates.count(f.seq) ? rates[f.seq] : FightPlayer::rate(f); }
	void end(const Fight& f, FightEnd why) override
	{
		endedSeq.push_back(f.seq);
		endedWhy.push_back(why);
	}
};

static CombatRecord R(uint32_t seq, int aOwner, int aUnit, int dOwner, int dUnit, bool aiTurn, uint32_t eventId = 0)
{
	CombatRecord r;
	memset(&r, 0, sizeof(r));
	r.seq = seq;
	r.kind = SrvFeed::kUnitCombat;
	r.flags = (uint16_t)(SrvFeed::kCombatVisualize | SrvFeed::kCombatGameRead | (aiTurn ? 0 : SrvFeed::kCombatActiveTurn));
	if(eventId)
	{
		r.flags = (uint16_t)(r.flags | SrvFeed::kCombatReturnedKnown);
		r.returned = eventId;
	}
	r.plot = 100;
	r.attackerOwner = aOwner;
	r.attackerUnit = aUnit;
	r.attackerCity = -1;
	r.defenderOwner = dOwner;
	r.defenderUnit = dUnit;
	r.defenderCity = -1;
	return r;
}

// Frames 1..n of kDt; returns every OwnerEnd.
struct Run
{
	FakePlayer player;
	FightClock clock{player};
	std::vector<OwnerEnd> out;
	uint64_t frame = 0;
	void add(const CombatRecord& r)
	{
		std::string why;
		const bool ok = clock.add(r, nullptr, 0, why);
		CHECK(ok);
		if(!ok)
			printf("  add: %s\n", why.c_str());
	}
	void frames(int n, const std::vector<FigureInstance>* figs = nullptr)
	{
		for(int k = 0; k < n; ++k)
		{
			std::string why;
			const bool ok = clock.frame(++frame, kDt, figs, out, why);
			CHECK(ok);
		}
	}
	const OwnerEnd* endOf(uint32_t seq) const
	{
		for(auto& e : out)
			if(e.seq == seq && e.instance == 0)
				return &e;
		return nullptr;
	}
	uint64_t startOf(uint32_t seq) const
	{
		const OwnerEnd* e = endOf(seq);
		return e ? e->frame - (uint64_t)llround(e->elapsed / kDt) : 0;
	}
};

static void testRecordedAiTurnStock()
{
	// 0477 stock, the human watching the AI turn (flags read, active turn clear): every fight is a barrier
	Run r;
	r.player.plans[1] = Plan{-1, true, false};   // the bomber on the submarine: never ends by itself (0477 13.4)
	r.player.plans[2] = Plan{-1, true, false};
	r.player.plans[3] = Plan{3.20, false, false};
	r.player.plans[4] = Plan{3.67, false, false};
	r.add(R(1, 1, 1052, 0, 1054, true, 501));
	r.add(R(2, 1, 1053, 0, 1054, true, 502));
	r.add(R(3, 63, 1045, 0, 1046, true, 503));
	r.add(R(4, 63, 1049, 0, 1046, true, 504));
	r.frames(2000);
	CHECK(r.out.size() == 4);
	CHECK(r.player.startedSeq == std::vector<uint32_t>({1, 2, 3, 4}));   // the DLL's order
	const OwnerEnd* e1 = r.endOf(1);
	const OwnerEnd* e2 = r.endOf(2);
	const OwnerEnd* e3 = r.endOf(3);
	const OwnerEnd* e4 = r.endOf(4);
	CHECK(e1 && e2 && e3 && e4);
	if(!(e1 && e2 && e3 && e4))
		return;
	// the 20 s limit with an aircraft (0321 13.2; recorded 20.05 and 20.07 s), then the barbarians' own ends
	CHECK(e1->why == FightEnd::Limit && fabs(e1->elapsed - 20.0) < kDt + 1e-6);
	CHECK(e2->why == FightEnd::Limit && fabs(e2->elapsed - 20.0) < kDt + 1e-6);
	CHECK(e3->why == FightEnd::Natural && fabs(e3->elapsed - 3.20) < kDt + 1e-6);
	CHECK(e4->why == FightEnd::Natural && fabs(e4->elapsed - 3.67) < kDt + 1e-6);
	// barriers play alone: each starts in the frame after the previous one's end (recorded: Run 0.03 s after End)
	CHECK(r.startOf(2) == e1->frame + 1);
	CHECK(r.startOf(3) == e2->frame + 1);
	CHECK(r.startOf(4) == e3->frame + 1);
	CHECK(e1->owner == 501 && e4->owner == 504);   // OwnerEnd names the EXE's event id
	CHECK(r.clock.counters().barriers == 4 && r.clock.counters().limit == 2 && r.clock.counters().natural == 2);
	CHECK(r.clock.counters().maxPlaying == 1);
	CHECK(r.clock.queued() == 0 && r.clock.playing() == 0);
}

static void testRecordedAiTurnFixed()
{
	// 0477's fix (aiturn-fixv2-ai1: both strikes 2.00 s): the clock adds nothing: no limit, two natural ends
	Run r;
	r.player.plans[1] = Plan{2.0, true, false};
	r.player.plans[2] = Plan{2.0, true, false};
	r.add(R(1, 1, 1048, 0, 1050, true, 601));
	r.add(R(2, 1, 1049, 0, 1050, true, 602));
	r.frames(400);
	const OwnerEnd* e1 = r.endOf(1);
	const OwnerEnd* e2 = r.endOf(2);
	CHECK(e1 && e2);
	if(!(e1 && e2))
		return;
	CHECK(e1->why == FightEnd::Natural && e2->why == FightEnd::Natural);
	CHECK(fabs(e1->elapsed - 2.0) < kDt + 1e-6 && fabs(e2->elapsed - 2.0) < kDt + 1e-6);
	CHECK(e2->frame - e1->frame <= (uint64_t)(2.0 / kDt) + 2);   // about 2 s apart, not 20
	CHECK(r.clock.counters().limit == 0);
}

static void testPerUnitWaitInTheHumansTurn()
{
	Run r;
	r.add(R(1, 0, 1, 63, 2, false, 701));   // A
	r.add(R(2, 0, 3, 63, 4, false, 702));   // B: other units, plays beside A
	r.add(R(3, 0, 1, 63, 5, false, 703));   // C: names A's attacker, waits for A
	r.player.plans[1] = Plan{2.0, false, false};
	r.player.plans[2] = Plan{1.0, false, false};
	r.player.plans[3] = Plan{1.5, false, false};
	r.frames(1);
	CHECK(r.clock.playing() == 2 && r.clock.queued() == 1);
	CHECK(r.clock.inFight(0, 1) && r.clock.inFight(63, 5) && !r.clock.inFight(0, 9));
	r.frames(300);
	CHECK(r.player.startedSeq == std::vector<uint32_t>({1, 2, 3}));
	CHECK(r.startOf(1) == 1 && r.startOf(2) == 1);
	CHECK(r.endOf(1) && r.startOf(3) == r.endOf(1)->frame + 1);
	CHECK(r.clock.counters().barriers == 0 && r.clock.counters().maxPlaying == 2);
	// the ends come out in the DLL's order within a frame and B ends before A
	CHECK(r.endOf(2) && r.endOf(1) && r.endOf(2)->frame < r.endOf(1)->frame);
}

static void testBarrierPlaysAlone()
{
	Run r;
	CombatRecord a = R(1, 0, 1, 63, 2, false, 801);
	CombatRecord b = R(2, 0, 3, 63, 4, false, 802);
	b.nuclearLevel = 1;   // a nuclear attack is a barrier in the human's turn too
	CombatRecord c = R(3, 0, 5, 63, 6, false, 803);
	r.add(a);
	r.add(b);
	r.add(c);
	r.player.plans[1] = Plan{1.0, false, false};
	r.player.plans[2] = Plan{1.0, false, false};
	r.player.plans[3] = Plan{1.0, false, false};
	r.frames(200);
	CHECK(r.endOf(1) && r.endOf(2) && r.endOf(3));
	if(!(r.endOf(1) && r.endOf(2) && r.endOf(3)))
		return;
	CHECK(r.startOf(2) == r.endOf(1)->frame + 1);   // waits for every earlier fight
	CHECK(r.startOf(3) == r.endOf(2)->frame + 1);   // and every later one waits for it
	CHECK(r.clock.counters().barriers == 1 && r.clock.counters().maxPlaying == 1);
}

static void testNoFightAndRefusal()
{
	Run r;
	CombatRecord q = R(1, 0, 1, 63, 2, false, 901);
	q.flags = (uint16_t)(q.flags | SrvFeed::kCombatQuickOption);   // Quick Combat (read): no fight
	CombatRecord v = R(2, 0, 1, 63, 2, false, 902);
	v.flags = (uint16_t)(v.flags & ~SrvFeed::kCombatVisualize);   // not visualised: no fight
	CombatRecord quickUnread = R(3, 0, 1, 63, 2, false, 903);
	quickUnread.flags = (uint16_t)(SrvFeed::kCombatVisualize | SrvFeed::kCombatQuickOption);   // bit 8 unread: a fight
	r.add(q);
	r.add(v);
	r.add(quickUnread);
	r.add(R(4, 0, 7, 63, 8, false, 904));
	r.player.plans[4] = Plan{1.0, false, true};   // refused (4777's case): ends in its start frame
	r.frames(100);
	CHECK(r.clock.counters().noFight == 2 && r.clock.counters().fights == 2);
	CHECK(!r.endOf(1) && !r.endOf(2));
	const OwnerEnd* e4 = r.endOf(4);
	CHECK(e4 && e4->why == FightEnd::Refused && e4->elapsed == 0 && e4->frame == 1 && e4->text.find("AirCombat") == 0);
	CHECK(r.clock.counters().refused == 1);
	// a record without the event id: OwnerEnd names the seq, owner 0, counted
	Run u;
	u.add(R(1, 0, 1, 63, 2, false, 0));
	u.player.plans[1] = Plan{0.5, false, false};
	u.frames(40);
	CHECK(u.endOf(1) && u.endOf(1)->owner == 0 && u.endOf(1)->seq == 1 && u.clock.counters().ownerUnknown == 1);
}

static void testLimitWithoutAircraft()
{
	Run r;
	r.add(R(1, 0, 1, 63, 2, false, 1001));
	r.player.plans[1] = Plan{30.0, false, false};
	r.frames(600);
	CHECK(r.endOf(1) && r.endOf(1)->why == FightEnd::Limit && fabs(r.endOf(1)->elapsed - 12.0) < kDt + 1e-6);
}

static void testClipInstances()
{
	Run r;
	r.add(R(1, 0, 1, 63, 2, false, 1101));
	r.player.plans[1] = Plan{1.0, false, false};
	std::vector<FigureInstance> figs = {{0, 1, 0, 10}, {0, 1, 1, 11}, {63, 2, 0, 20}, {5, 9, 0, 99}};
	r.frames(1, &figs);   // start; the figures' instances open
	r.frames(3, &figs);   // unchanged: nothing
	CHECK(r.out.empty());
	figs[1].instance = 12;   // figure 1 of 0/1 entered a new state: its instance 11 ended
	figs.erase(figs.begin() + 2);   // the defender's figure 0 vanished (removed): its instance 20 was cut
	r.frames(1, &figs);
	CHECK(r.out.size() == 2);
	bool f1 = false, f2 = false;
	for(auto& e : r.out)
	{
		if(e.why == FightEnd::ClipEnded && e.player == 0 && e.unit == 1 && e.figure == 1 && e.instance == 11 &&
		   e.owner == 1101)
			f1 = true;
		if(e.why == FightEnd::ClipEnded && e.player == 63 && e.unit == 2 && e.figure == 0 && e.instance == 20)
			f2 = true;
	}
	CHECK(f1 && f2);
	// a unit outside every fight gets no records; at the fight's end its open instances are covered by its OwnerEnd
	r.frames(60, &figs);
	size_t clipRecords = 0;
	for(auto& e : r.out)
		clipRecords += e.why == FightEnd::ClipEnded;
	CHECK(clipRecords == 2);
	CHECK(r.endOf(1) && r.endOf(1)->why == FightEnd::Natural);
	CHECK(r.clock.counters().clipEnds == 2);
}

static void testGoneAndReset()
{
	Run r;
	r.add(R(1, 0, 1, 63, 2, false, 1201));
	r.add(R(2, 0, 1, 63, 3, false, 1202));   // queued behind 1
	r.add(R(3, 0, 5, 63, 6, false, 1203));
	r.player.plans[1] = Plan{5.0, false, false};
	r.player.plans[3] = Plan{5.0, false, false};
	r.frames(3);
	r.clock.unitGone(0, 1, r.out);   // fights 1 (playing) and 2 (queued) end
	CHECK(r.endOf(1) && r.endOf(1)->why == FightEnd::Gone && r.endOf(2) && r.endOf(2)->why == FightEnd::Gone);
	CHECK(r.endOf(1)->frame == 3);
	r.clock.unitGone(4, 44, r.out);   // a unit in no fight: nothing
	CHECK(r.out.size() == 2);
	r.clock.reset(r.out);
	CHECK(r.endOf(3) && r.endOf(3)->why == FightEnd::Reset);
	CHECK(r.clock.queued() == 0 && r.clock.playing() == 0);
	CHECK(r.player.endedSeq.size() == 2);   // the player was told about the two that had started (1 and 3)
}

static void testRefusals()
{
	FakePlayer p;
	FightClock c(p);
	std::string why;
	CombatRecord r = R(5, 0, 1, 63, 2, false, 1);
	CHECK(c.add(r, nullptr, 0, why));
	CHECK(!c.add(R(5, 0, 1, 63, 2, false, 1), nullptr, 0, why) && why.find("not above") != std::string::npos);
	CHECK(!c.add(R(4, 0, 1, 63, 2, false, 1), nullptr, 0, why));
	CombatRecord k = R(9, 0, 1, 63, 2, false, 1);
	k.kind = 26;
	CHECK(!c.add(k, nullptr, 0, why) && why.find("kind 26") != std::string::npos);
	CHECK(!c.add(R(10, 0, 1, 63, 2, false, 1), nullptr, 3, why) && why.find("no member array") != std::string::npos);
	CHECK(c.counters().refusedRecords == 4);
	CombatMember m[2];
	memset(m, 0, sizeof(m));
	CHECK(c.add(R(11, 0, 7, 63, 8, false, 1), m, 2, why));
	// ticket 0742: a record back from its wait behind a walk (resumed) is taken below the last seq, once; not resumed, or
	// equal to the last seq, or with a fight of its seq still held, it is refused as before
	CHECK(c.add(R(7, 0, 9, 63, 10, false, 1), nullptr, 0, why, -1, true) && c.counters().resumedBelow == 1);
	CHECK(c.queued() == 3);
	CHECK(!c.add(R(7, 0, 9, 63, 10, false, 1), nullptr, 0, why, -1, true) && why.find("is held") != std::string::npos);
	CHECK(!c.add(R(11, 0, 7, 63, 8, false, 1), m, 2, why, -1, true) && why.find("is held") != std::string::npos);
	CHECK(!c.add(R(6, 0, 9, 63, 10, false, 1), nullptr, 0, why) && why.find("DLL's call order") != std::string::npos);
	CHECK(c.add(R(12, 0, 9, 63, 10, false, 1), nullptr, 0, why));   // the last seq stays 11 after the resumed 7
	CHECK(c.counters().refusedRecords == 7 && c.counters().resumedBelow == 1);
	std::vector<OwnerEnd> out;
	CHECK(!c.frame(1, -0.1, nullptr, out, why) && why.find("dt") != std::string::npos);
	CHECK(!c.frame(1, NAN, nullptr, out, why));
	CHECK(c.frame(5, kDt, nullptr, out, why));
	CHECK(!c.frame(5, kDt, nullptr, out, why) && why.find("not above") != std::string::npos);
	CHECK(c.counters().badFrames == 3);
	CHECK(std::string(fightEndName(FightEnd::ClipEnded)) == "ClipEnded");
}

// Issue 4783 (W33 #8801): the EXE's event id that came after the combat record was drained (SrvFeed::Batch::lateResults)
// reaches the fight through setOwner: before its start, while it waits, while it plays; never (the end stays owner 0,
// counted); after its end and for a fight whose id was known (nothing changes).
static void testLateOwner()
{
	// before the start: the record was added, no frame yet
	{
		Run r;
		r.add(R(10, 0, 100, 1, 200, false));
		CHECK(r.clock.counters().ownerUnknown == 1);
		CHECK(r.clock.setOwner(10, 610) == FightClock::OwnerSet::Applied);
		r.frames(200);
		CHECK(r.player.ownerAtStart[10] == 610);          // the fight starts with its event id
		CHECK(r.player.ownerKnownCalls.empty());          // not started when it came: nothing to tell the player
		const OwnerEnd* e = r.endOf(10);
		CHECK(e && e->owner == 610 && e->why == FightEnd::Natural);
		CHECK(r.clock.counters().ownerLate == 1 && r.clock.counters().ownerZeroEnds == 0);
	}
	// while queued: the human's turn, the second fight of unit (0, 100) waits for the first
	{
		Run r;
		r.add(R(20, 0, 100, 1, 200, false, 620));
		r.add(R(21, 0, 100, 1, 201, false));
		r.frames(3);
		CHECK(r.clock.playing() == 1 && r.clock.queued() == 1);
		CHECK(r.clock.setOwner(21, 621) == FightClock::OwnerSet::Applied);
		CHECK(r.player.ownerKnownCalls.empty());          // it waits: the player has not seen it
		r.frames(400);
		CHECK(r.player.ownerAtStart[21] == 621);
		const OwnerEnd* e = r.endOf(21);
		CHECK(e && e->owner == 621);
		CHECK(r.endOf(20) && r.endOf(20)->owner == 620);
		CHECK(r.clock.counters().ownerZeroEnds == 0);
	}
	// while playing: the player is told once, the clip instances' and the fight's OwnerEnds carry the id
	{
		Run r;
		r.add(R(30, 0, 100, 1, 200, false));
		std::vector<FigureInstance> figs(1);
		figs[0].player = 0; figs[0].unit = 100; figs[0].figure = 0; figs[0].instance = 7;
		r.frames(5, &figs);
		CHECK(r.clock.playing() == 1 && r.player.ownerAtStart[30] == 0);
		CHECK(r.clock.setOwner(30, 630) == FightClock::OwnerSet::Applied);
		CHECK(r.player.ownerKnownCalls.size() == 1 && r.player.ownerKnownCalls[0] == std::make_pair(30u, 630u));
		figs[0].instance = 8;                             // the figure's clip ends while the fight plays
		r.frames(1, &figs);
		bool clipOwner = false;
		for(const OwnerEnd& e : r.out)
			if(e.seq == 30 && e.why == FightEnd::ClipEnded)
				clipOwner = e.owner == 630;
		CHECK(clipOwner);
		r.frames(200, &figs);
		const OwnerEnd* e = r.endOf(30);
		CHECK(e && e->owner == 630);
		CHECK(r.clock.counters().ownerLate == 1 && r.clock.counters().ownerZeroEnds == 0);
		// a second late result for it changes nothing (it has ended)
		CHECK(r.clock.setOwner(30, 631) == FightClock::OwnerSet::NoFight);
	}
	// never (the negative: the pre-4783 behaviour): the end is owner 0 and counted
	{
		Run r;
		r.add(R(40, 0, 100, 1, 200, false));
		r.frames(200);
		const OwnerEnd* e = r.endOf(40);
		CHECK(e && e->owner == 0);
		CHECK(r.clock.counters().ownerZeroEnds == 1 && r.clock.counters().ownerLate == 0);
		CHECK(r.clock.setOwner(40, 640) == FightClock::OwnerSet::NoFight);   // too late: an end already sent stays owner 0
		CHECK(r.clock.counters().ownerLateNoFight == 1);
	}
	// an id already known is kept; a seq the clock never held (Quick Combat: no fight) is NoFight
	{
		Run r;
		r.add(R(50, 0, 100, 1, 200, false, 650));
		CombatRecord q = R(51, 0, 101, 1, 201, false);
		q.flags = (uint16_t)(q.flags | SrvFeed::kCombatQuickOption);
		r.add(q);
		CHECK(r.clock.setOwner(50, 999) == FightClock::OwnerSet::Known);
		CHECK(r.clock.setOwner(51, 651) == FightClock::OwnerSet::NoFight);
		r.frames(200);
		CHECK(r.endOf(50) && r.endOf(50)->owner == 650 && !r.endOf(51));
	}
}

// Ticket 0796: a fight played at a rate. Its own time (elapsed, the player's steps, its end, its limit) runs at dt x rate,
// its real time (Fight::wall) at dt; unitRate names its units at that rate in every frame it steps, its end frame too,
// and every other unit at 1. A rate of 1, a bad rate (0, negative, not finite) or no rate: real time.
static void testPlaybackRate()
{
	{
		Run r;
		r.add(R(1, 0, 1, 63, 2, false, 1201));
		r.player.plans[1] = Plan{3.0, false, false};
		r.player.rates[1] = 3.0;
		r.frames(1);   // the start frame: no step
		CHECK(r.clock.unitRate(0, 1) == 1.0);
		r.frames(1);
		CHECK(r.clock.unitRate(0, 1) == 3.0 && r.clock.unitRate(63, 2) == 3.0 && r.clock.unitRate(0, 9) == 1.0);
		r.frames(28);   // 29 steps x 0.1 s = 2.9 s of fight time: still playing
		CHECK(!r.endOf(1));
		r.frames(1);   // the 30th step reaches 3.0 s: the end frame, the rate still named for it
		CHECK(r.endOf(1) && r.endOf(1)->why == FightEnd::Natural && fabs(r.endOf(1)->elapsed - 3.0) < 1e-6);
		CHECK(r.clock.unitRate(0, 1) == 3.0);
		CHECK(fabs(r.player.advanced[1] - 3.0) < 1e-6);
		r.frames(1);
		CHECK(r.clock.unitRate(0, 1) == 1.0);
	}
	{
		Run r;   // the limit in fight time: 12 s at 3x after 4 s of real time
		r.add(R(1, 0, 1, 63, 2, false, 1202));
		r.player.plans[1] = Plan{30.0, false, false};
		r.player.rates[1] = 3.0;
		r.frames(200);
		CHECK(r.endOf(1) && r.endOf(1)->why == FightEnd::Limit && fabs(r.endOf(1)->elapsed - 12.0) < 3 * kDt + 1e-6);
		CHECK(r.endOf(1) && r.endOf(1)->frame <= 1 + 121);
	}
	for(double bad : {0.0, -2.0, nan(""), 1.0})
	{
		Run r;   // real time: the end after 3 s as without a rate
		r.add(R(1, 0, 1, 63, 2, false, 1203));
		r.player.plans[1] = Plan{3.0, false, false};
		r.player.rates[1] = bad;
		r.frames(120);
		CHECK(r.endOf(1) && r.endOf(1)->frame == 1 + 90 && fabs(r.endOf(1)->elapsed - 90 * kDt) < 1e-9);
		CHECK(r.clock.unitRate(0, 1) == 1.0);
	}
}

int main()
{
	testPlaybackRate();   // ticket 0796
	testLateOwner();   // issue 4783
	testRecordedAiTurnStock();
	testRecordedAiTurnFixed();
	testPerUnitWaitInTheHumansTurn();
	testBarrierPlaysAlone();
	testNoFightAndRefusal();
	testLimitWithoutAircraft();
	testClipInstances();
	testGoneAndReset();
	testRefusals();
#ifdef FIGHTS_TEST_INJECT_FAIL
	CHECK(!"FAIL injected");
	printf("FAIL injected\n");
#endif
	printf("test_units_fights: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
