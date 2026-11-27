// unitfeed_fixture.cpp - synthetic scene bundles (format version 5) with the unit feed's records, for unitfeed's tests
// (epic 0375, issue 4457). Written 2026-11-16 for this repository (GPL-3.0-or-later). Every byte this file writes is
// synthetic (D83); the units_db member is the database file named on the command line, copied at run time.
//
//   unitfeed_fixture.exe VARIANT DIR DB   writes the bundle DIR (absent or empty); exit 0 written, 1 failed, 2 usage
//
// Format version 5 (issue 0440; issue 0448 brought the fixture to it): render_units is 1, so the bundle carries the
// units_db member units/gen0000000001.db (DB copied whole, as the recorder copies the database the unit scene opened) and
// the recorder's note kUnitsNoHistoryNote in place of units_history.bin: the controller starts at the span's first frame
// (unitfeed's default --anim-from), so no frame was drawn from the controller before the span ("none recorded", the
// recorder's own words for an empty history).
//
// Variants (the type indices are rows of the catalogue database F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db: Units 83
// UNIT_WARRIOR, 78 UNIT_SPEARMAN; Civilizations 14 ROME, 8 GREECE; PlayerColors 77 ROME, 71 GREECE; Eras 0 ANCIENT):
//   good          loop 1: two creations and a visibility with blend 0.5 s (drainMs 1000), PLOTS, PLAYERS copy, UNITS
//                 first copy; frame 100 (loop 1, nowMs 1200: the fade at 0.4); loop 2: a move with its path and a combat
//                 (PATHS, COMBATS, PLAYERS batch row), UNITS diff; frame 101 (nowMs 1400); loop 3: a plot-state event
//                 only (no unit kind: the scene takes no copy); loop 4: the removal of (1, 1002), UNITS diff; frame 102
//                 (nowMs 1600)
//   nounits       good without any UNITS record (a recording without the scene's copies)
//   extracopy     good plus a UNITS copy after loop 3's drain, which the scene does not take (out of step)
//   missingcopy   good without loop 2's UNITS copy (the scene takes a copy the recording does not hold: out of step)
//   noclock       good with loop 1's drainMs -1
//   toomany       one frame with 4,097 units of player 0 (the .c5units writer's limit is 4,096)
//   aiturn        issue 4774: 0477's recorded AI turn's barbarian fights (two attacks of player 63 on the human's unit
//                 (0, 1046), event ids 54 and 55, barriers), the defender's two SetDamage behind them, then 600 frames
//                 (20 s at 30 per second) for the fights to play
//   aiturnforest  ticket 5052 (T): aiturn with one fog table read after loop 1's PLOTS (caller 3, every plot plains,
//                 revealed and visible), the defender's plot 3 a forest (feature 5, LEAVES)
//   aiturnplains  the same read with no forest (every plot plains, DIRT)
//   nukelong      issue 4729: special's nuke alone (missile 2005, infantry member 3006, a city member) with the frame
//                 clock of c5h-b r1 at its opening (start frame 4.44 s, first advance 6.08 s) and 30 per second after
//                 it: the missile's run, combat ready and attack fall in one frame
//   nuker1        the same with r1's whole recorded frame clock of that fight (frames 142-196 of
//                 F:/c5-64bit-work/0042/w20/anim-r1, 54 frames in 21.07 s), then 30 per second
//   cityshot      ticket 5062 (W107): player 0's city 5 (plot 0) strikes the barbarian Warrior (63, 1045) on plot 3 (a
//                 forest in one fog table read), kind 28 with a[3] = 1 (the attacking city's plot + 1), then 600 frames
//   cityshotnoplot  the same with a[3] = 0 (a recording made before 5062: the attacking city's plot not known)
#include "srv_scene_bundle.h"

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "vp64_options.h"

// server.exe's SrvOptions() for the render units this program links (every option at its default)
static Vp64Opt::Options g_fixtureOptions;
const Vp64Opt::Options& SrvOptions() { return g_fixtureOptions; }

namespace sb = srvrender::scenebundle;

static SrvFeed::Event Ev(uint32_t seq, uint16_t kind, int plot, int a0, int a1, int a2 = 0, int a3 = 0)
{
	SrvFeed::Event e = {};
	e.seq = seq;
	e.kind = kind;
	e.plot = plot;
	e.turn = 3;
	e.a[0] = a0;
	e.a[1] = a1;
	e.a[2] = a2;
	e.a[3] = a3;
	return e;
}
static SrvFeed::UnitRecord Unit(int owner, int id, int type, int plot, uint32_t seq, uint8_t flags = SrvFeed::kUnitFlagVisible)
{
	SrvFeed::UnitRecord u;
	memset(&u, 0, sizeof(u));
	u.owner = (int8_t)owner;
	u.domain = 2;   // DOMAIN_LAND
	u.facing = 1;
	u.flags = flags;
	u.id = id;
	u.unitType = type;
	u.plot = plot;
	u.damage = 0;
	u.maxHP = 100;
	u.artStyle = -1;
	u.era = 0;
	u.seq = seq;
	return u;
}
static SrvFeed::PlayerRecord Player(int player, int colour, int civ)
{
	SrvFeed::PlayerRecord p;
	memset(&p, 0, sizeof(p));
	p.player = (int8_t)player;
	p.team = (int8_t)player;
	p.alive = 1;
	p.colour = colour;
	p.civilization = civ;
	p.era = 0;
	p.artStyle = 3;
	p.changeGen = 1;
	p.activeTeam = 0;
	return p;
}
static float Bits(int32_t v)
{
	float f;
	memcpy(&f, &v, 4);
	return f;
}
static int32_t FloatBits(float f)
{
	int32_t v;
	memcpy(&v, &f, 4);
	return v;
}

struct Builder
{
	std::vector<uint8_t> feed;
	sb::FeedEncoder enc;
	std::vector<sb::FrameRecord> frames;
	bool ok = true;
	void put(const std::vector<uint8_t>& r) { feed.insert(feed.end(), r.begin(), r.end()); }
	void drain(uint64_t loop, SrvFeed::Batch b, double drainMs)
	{
		std::vector<uint8_t> r;
		b.width = 2;
		b.height = 3;
		b.snapshotGen = 1;
		const c5r::Status st = enc.addDrain(loop, 100 + (int64_t)frames.size(), b, &r, drainMs);
		if(!st.ok())
		{
			printf("fixture: %s\n", st.str().c_str());
			ok = false;
		}
		put(r);
	}
	void units(uint64_t loop, uint32_t caller, const std::vector<SrvFeed::UnitRecord>& t)
	{
		std::vector<uint8_t> r;
		const c5r::Status st = enc.addUnits(loop, caller, 1, t, &r);
		if(!st.ok())
		{
			printf("fixture: %s\n", st.str().c_str());
			ok = false;
		}
		put(r);
	}
	void players(uint64_t loop, const std::vector<SrvFeed::PlayerRecord>& rows)
	{
		std::vector<uint8_t> r;
		if(!enc.addPlayers(loop, rows, &r).ok())
			ok = false;
		put(r);
	}
	void plots(uint64_t loop)
	{
		std::vector<SrvFeed::PlotRecord> t(6);
		for(SrvFeed::PlotRecord& p : t)
		{
			memset(&p, 0, sizeof(p));
			p.terrain = 1;
			p.plotType = 2;
			p.feature = -1;
			p.route = -1;
			p.resource = -1;
			p.improvement = -1;
			p.owner = -1;
			p.improvementState = -1;
			p.routeState = -1;
		}
		std::vector<uint8_t> r;
		enc.addPlots(loop, 1, 1, 2, 3, t, &r);
		put(r);
	}
	// ticket 5052 (T): a fog scene's table read (PLOTS of caller 3, kPlotsFog: FowScene::readTable's copy), every plot
	// plains (terrain 1, EffectTypeTag DIRT), revealed and visible (fow 3); plot `forest` (if >= 0) with feature 5
	// (FEATURE_FOREST, EffectTypeTag LEAVES, in the catalogue database)
	void fogPlots(uint64_t loop, int forest)
	{
		std::vector<SrvFeed::PlotRecord> t(6);
		for(size_t i = 0; i < t.size(); ++i)
		{
			SrvFeed::PlotRecord& p = t[i];
			memset(&p, 0, sizeof(p));
			p.terrain = 1;
			p.plotType = 2;
			p.feature = (int8_t)((int)i == forest ? 5 : -1);
			p.route = -1;
			p.resource = -1;
			p.improvement = -1;
			p.owner = -1;
			p.fow = 3;
			p.improvementState = -1;
			p.routeState = -1;
		}
		std::vector<uint8_t> r;
		enc.addPlots(loop, 3, 1, 2, 3, t, &r);
		put(r);
	}
	void frame(uint64_t loop, double nowMs)
	{
		sb::FrameRecord f;
		memset(&f, 0, sizeof(f));
		f.frameId = 100 + (int64_t)frames.size();
		f.loopIter = loop;
		f.nowMs = nowMs;
		f.width = 8;
		f.height = 4;
		frames.push_back(f);
	}
};

static std::vector<uint8_t> Heightfield()
{
	c5r::heightfield::HeightGrid g;
	g.mapW = 2;
	g.mapH = 3 + 4;          // the map's rows plus the 4 margin rows of a tap (placement refuses mapH <= 4)
	g.samplesX = 128;
	g.samplesY = 7 * 64;
	g.wrapX = false;
	g.ox = 0;
	g.oy = 0;
	g.waterZ = 10.0f;
	g.mapSeed = 7;
	g.hexToWorld[0] = 1;
	g.hexToWorld[5] = 1;
	g.h.assign((size_t)g.samplesX * g.samplesY, 1000);
	std::vector<uint8_t> out;
	c5r::heightfield::writeTap(g, out);
	return out;
}

int main(int argc, char** argv)
{
	if(argc != 4)
	{
		printf("usage: unitfeed_fixture.exe good|nounits|extracopy|missingcopy|noclock|toomany|aiturn|aiturnforest|aiturnplains|special|nukelong|nuker1|cityshot|cityshotnoplot DIR DB\n");
		return 2;
	}
	const std::string v = argv[1];
	if(v != "good" && v != "nounits" && v != "extracopy" && v != "missingcopy" && v != "noclock" && v != "toomany" && v != "aiturn" &&
	   v != "aiturnforest" && v != "aiturnplains" && v != "special" && v != "nukelong" && v != "nuker1" && v != "cityshot" &&
	   v != "cityshotnoplot")
	{
		printf("usage: unknown variant %s\n", v.c_str());
		return 2;
	}
	const bool copies = v != "nounits";
	Builder B;
	B.feed.clear();
	sb::EncodeStreamHead(sb::MemberKind::Feed, &B.feed);
	const int kWarrior = 83, kSpearman = 78;
	if(v == "toomany")
	{
		SrvFeed::Batch b;
		b.unitTableGen = 1;
		std::vector<SrvFeed::UnitRecord> t;
		for(uint32_t k = 0; k < 4097; ++k)
		{
			b.events.push_back(Ev(k + 1, SrvFeed::kUnitCreated, (int)(k % 6), 0, 1001 + (int)k, kWarrior));
			t.push_back(Unit(0, 1001 + (int)k, kWarrior, (int)(k % 6), k + 1));
		}
		B.drain(1, b, 1000);
		B.plots(1);
		B.players(1, {Player(0, 77, 14)});
		B.units(1, srvunits::rec::kUnitsFirstCopy, t);
		B.frame(1, 1200);
	}
	else if(v == "aiturn" || v == "aiturnforest" || v == "aiturnplains")
	{
		// Issue 4774: the shape of 0477's recorded AI turn (server/evidence/0477/logs/aiturn-stock-ai1.log, events 123-128:
		// two barbarian attacks on one human unit while the human watched, event ids 54 and 55, the defender's damage 27
		// then 43, the attackers' 0): every fight a barrier (kCombatGameRead set, kCombatActiveTurn clear). The unit types
		// and the owners' art are this fixture's (a spearman defends, warriors attack; player 63 drawn with Greece's row).
		SrvFeed::Batch b1;
		b1.unitTableGen = 1;
		b1.events.push_back(Ev(1, SrvFeed::kUnitCreated, 3, 0, 1046, kSpearman));
		b1.events.push_back(Ev(2, SrvFeed::kUnitCreated, 2, 63, 1045, kWarrior));
		b1.events.push_back(Ev(3, SrvFeed::kUnitCreated, 5, 63, 1049, kWarrior));
		B.drain(1, b1, 1000.0);
		B.plots(1);
		if(v != "aiturn")   // ticket 5052 (T)
			B.fogPlots(1, v == "aiturnforest" ? 3 : -1);
		std::vector<SrvFeed::UnitRecord> t1 = {Unit(0, 1046, kSpearman, 3, 1), Unit(63, 1045, kWarrior, 2, 2), Unit(63, 1049, kWarrior, 5, 3)};
		B.players(1, {Player(0, 77, 14), Player(63, 71, 8)});
		B.units(1, srvunits::rec::kUnitsFirstCopy, t1);
		B.frame(1, 1200);
		SrvFeed::Batch b2;
		b2.unitTableGen = 1;
		const uint16_t flags = SrvFeed::kCombatVisualize | SrvFeed::kCombatGameRead | SrvFeed::kCombatReturnedKnown |
		                       SrvFeed::kCombatDefenderRetaliates;
		const struct { uint32_t seq; int attacker; uint32_t eventId; int finalD, prevD; } fights[2] = {{4, 1045, 54, 27, 0}, {6, 1049, 55, 43, 27}};
		for(const auto& fi : fights)
		{
			b2.events.push_back(Ev(fi.seq, SrvFeed::kUnitCombat, 3, 63, fi.attacker, (int)b2.combats.size(), 0));
			SrvFeed::CombatRecord c;
			memset(&c, 0, sizeof(c));
			c.seq = fi.seq;
			c.kind = SrvFeed::kUnitCombat;
			c.flags = flags;
			c.plot = 3;
			c.attackerOwner = 63;
			c.attackerUnit = fi.attacker;
			c.attackerCity = -1;
			c.defenderOwner = 0;
			c.defenderUnit = 1046;
			c.defenderCity = -1;
			c.finalDamage[0] = 0;
			c.finalDamage[1] = fi.finalD;
			c.returned = fi.eventId;
			b2.combats.push_back(c);
			b2.events.push_back(Ev(fi.seq + 1, SrvFeed::kUnitSetDamage, 3, 0, 1046, fi.finalD, fi.prevD));
		}
		B.drain(2, b2, 1300);
		std::vector<SrvFeed::UnitRecord> t2 = t1;
		t2[0].damage = 43;
		t2[0].seq = 7;
		B.units(2, srvunits::rec::kUnitsLaterCopy, t2);
		for(int k = 0; k < 600; ++k)   // 20 s of frames at 30 per second
			B.frame(2, 1400 + k * (1000.0 / 30.0));
	}
	else if(v == "cityshot" || v == "cityshotnoplot")
	{
		// Ticket 5062 (W107): a city's own ranged strike in the shape the feed writes (srv_remote_feed.cpp OnCombat for
		// GameplayCityCombat, the DLL's GenerateRangedCombatInfo(CvCity&): the city is the attacker, no damage member, the
		// record's plot the target's, retaliate clear, ranged): player 0's city id 5 on plot 0 strikes the barbarian Warrior
		// (63, 1045) on plot 3 (a forest in the fog table read), 30 damage, no kill. cityshot: kind 28's a[3] = plot 0 + 1
		// (the attacking city's plot, 5062); cityshotnoplot: a[3] = 0 (a recording before 5062). Then 600 frames.
		SrvFeed::Batch b1;
		b1.unitTableGen = 1;
		b1.events.push_back(Ev(1, SrvFeed::kUnitCreated, 3, 63, 1045, kWarrior));
		B.drain(1, b1, 1000.0);
		B.plots(1);
		B.fogPlots(1, 3);
		std::vector<SrvFeed::UnitRecord> t1 = {Unit(63, 1045, kWarrior, 3, 1)};
		B.players(1, {Player(0, 77, 14), Player(63, 71, 8)});
		B.units(1, srvunits::rec::kUnitsFirstCopy, t1);
		B.frame(1, 1200);
		SrvFeed::Batch b2;
		b2.unitTableGen = 1;
		const uint16_t flags = SrvFeed::kCombatVisualize | SrvFeed::kCombatGameRead | SrvFeed::kCombatReturnedKnown |
		                       SrvFeed::kCombatRanged;
		b2.events.push_back(Ev(2, SrvFeed::kCityCombat, 3, 0, 5, 0, v == "cityshot" ? 0 + 1 : 0));
		SrvFeed::CombatRecord c;
		memset(&c, 0, sizeof(c));
		c.seq = 2;
		c.kind = SrvFeed::kCityCombat;
		c.flags = flags;
		c.plot = 3;
		c.attackerOwner = 0;
		c.attackerUnit = -1;
		c.attackerCity = 5;
		c.defenderOwner = 63;
		c.defenderUnit = 1045;
		c.defenderCity = -1;
		c.finalDamage[0] = 0;
		c.finalDamage[1] = 30;
		c.returned = 56;
		b2.combats.push_back(c);
		b2.events.push_back(Ev(3, SrvFeed::kUnitSetDamage, 3, 63, 1045, 30, 0));
		B.drain(2, b2, 1300);
		std::vector<SrvFeed::UnitRecord> t2 = t1;
		t2[0].damage = 30;
		t2[0].seq = 3;
		B.units(2, srvunits::rec::kUnitsLaterCopy, t2);
		for(int k = 0; k < 600; ++k)   // 20 s of frames at 30 per second
			B.frame(2, 1400 + k * (1000.0 / 30.0));
	}
	else if(v == "special")
	{
		// Issue 4726: 4777's special fights through the server's adapter, the records in the shape the feed writes since
		// 4726 (catalogue rows: Units 34 UNIT_BOMBER, 16 UNIT_SUBMARINE, 36 UNIT_FIGHTER, 44 UNIT_INFANTRY, 24
		// UNIT_NUCLEAR_MISSILE). Each fight has its own units (none waits for another but the nuke, a barrier):
		//   seq 10 (event id 70) stock bomber on a submarine (0477's series-stock-sub: retaliate set, the damage not known)
		//   seq 12 (event id 71) the same with 0477's fix (retaliate clear, kCombatDamageKnown: the attacker's damage 0)
		//   seq 14 (event id 72) a bomber on infantry intercepted by a fighter (the interceptor's member, 40 dealt; the
		//                        strike aborted: both finals 0)
		//   seq 16 (event id 73) a nuclear missile on plot 4: no defender, one unit damage member (infantry, killed) and a
		//                        city member
		// then 1500 frames (50 s at 30 per second) for the fights to play.
		const int kBomber = 34, kSub = 16, kFighter = 36, kInfantry = 44, kMissile = 24;
		struct U { int owner, id, type, plot, domain; };
		const U us[] = {{0, 2001, kBomber, 0, 1}, {1, 3001, kSub, 1, 0}, {0, 2002, kBomber, 0, 1}, {1, 3002, kSub, 1, 0},
		                {0, 2003, kBomber, 2, 1}, {1, 3003, kInfantry, 3, 2}, {1, 3004, kFighter, 3, 1},
		                {0, 2005, kMissile, 2, 1}, {1, 3006, kInfantry, 4, 2}};
		SrvFeed::Batch b1;
		b1.unitTableGen = 1;
		std::vector<SrvFeed::UnitRecord> t1;
		uint32_t seq = 1;
		for(const U& u : us)
		{
			b1.events.push_back(Ev(seq, SrvFeed::kUnitCreated, u.plot, u.owner, u.id, u.type));
			SrvFeed::UnitRecord r = Unit(u.owner, u.id, u.type, u.plot, seq);
			r.domain = (int8_t)u.domain;
			t1.push_back(r);
			++seq;
		}
		B.drain(1, b1, 1000.0);
		B.plots(1);
		B.players(1, {Player(0, 77, 14), Player(1, 71, 8)});
		B.units(1, srvunits::rec::kUnitsFirstCopy, t1);
		B.frame(1, 1200);
		SrvFeed::Batch b2;
		b2.unitTableGen = 1;
		const uint16_t base = SrvFeed::kCombatVisualize | SrvFeed::kCombatReturnedKnown | SrvFeed::kCombatBombing |
		                      SrvFeed::kCombatRanged;
		auto combat = [&](uint32_t s, int ai, int dob, int di, int plot, uint16_t flags, uint32_t id, int fa, int fd) {
			b2.events.push_back(Ev(s, SrvFeed::kUnitCombat, plot, 0, ai, (int)b2.combats.size(), 0));
			SrvFeed::CombatRecord c;
			memset(&c, 0, sizeof(c));
			c.seq = s;
			c.kind = SrvFeed::kUnitCombat;
			c.flags = flags;
			c.plot = plot;
			c.attackerOwner = 0;
			c.attackerUnit = ai;
			c.attackerCity = -1;
			c.defenderOwner = dob;
			c.defenderUnit = di;
			c.defenderCity = -1;
			c.finalDamage[0] = fa;
			c.finalDamage[1] = fd;
			c.returned = id;
			c.firstMember = (uint32_t)b2.combatMembers.size();
			b2.combats.push_back(c);
			return &b2.combats.back();
		};
		auto member = [&](SrvFeed::CombatRecord* c, int player, int id, uint16_t fl, int dmg, int fin, int maxHp) {
			SrvFeed::CombatMember m;
			memset(&m, 0, sizeof(m));
			m.player = player;
			m.id = id;
			m.flags = fl;
			m.damage = (int16_t)dmg;
			m.finalDamage = (int16_t)fin;
			m.maxHP = (int16_t)maxHp;
			m.x = m.y = -1;
			b2.combatMembers.push_back(m);
			c->nMembers++;
		};
		combat(10, 2001, 1, 3001, 1, base | SrvFeed::kCombatDefenderRetaliates, 70, 0, 30);
		b2.events.push_back(Ev(11, SrvFeed::kUnitSetDamage, 1, 1, 3001, 30, 0));
		SrvFeed::CombatRecord* fix = combat(12, 2002, 1, 3002, 1, base | SrvFeed::kCombatDamageKnown, 71, 0, 30);
		fix->reserved = SrvFeed::CombatDamageWords(0, 30);
		b2.events.push_back(Ev(13, SrvFeed::kUnitSetDamage, 1, 1, 3002, 30, 0));
		SrvFeed::CombatRecord* ic =
		    combat(14, 2003, 1, 3003, 3, base | SrvFeed::kCombatDamageKnown | SrvFeed::kCombatInterceptorMember, 72, 0, 0);
		ic->reserved = SrvFeed::CombatDamageWords(0, 0);
		member(ic, 1, 3004, SrvFeed::kCombatMemberInterceptor, 40, 0, 100);
		SrvFeed::CombatRecord* nk = combat(16, 2005, -1, -1, 4, base, 73, 0, 0);
		nk->nuclearLevel = 1;
		member(nk, 1, 3006, 0, 100, 100, 100);
		member(nk, 1, 4, 1, 80, 80, 300);
		b2.events.push_back(Ev(17, SrvFeed::kUnitSetDamage, 4, 1, 3006, 100, 0));
		B.drain(2, b2, 1300);
		std::vector<SrvFeed::UnitRecord> t2 = t1;
		t2[1].damage = 30;
		t2[1].seq = 11;
		t2[3].damage = 30;
		t2[3].seq = 13;
		t2[8].damage = 100;
		t2[8].seq = 17;
		B.units(2, srvunits::rec::kUnitsLaterCopy, t2);
		for(int k = 0; k < 1500; ++k)
			B.frame(2, 1400 + k * (1000.0 / 30.0));
	}
	else if(v == "nukelong" || v == "nuker1")
	{
		// Issue 4729: the nuke of special (seq 16, event id 73) alone, on r1's frame clock (header)
		const int kInfantry = 44, kMissile = 24;
		SrvFeed::Batch b1;
		b1.unitTableGen = 1;
		std::vector<SrvFeed::UnitRecord> t1;
		b1.events.push_back(Ev(1, SrvFeed::kUnitCreated, 2, 0, 2005, kMissile));
		t1.push_back(Unit(0, 2005, kMissile, 2, 1));
		t1.back().domain = 1;
		b1.events.push_back(Ev(2, SrvFeed::kUnitCreated, 4, 1, 3006, kInfantry));
		t1.push_back(Unit(1, 3006, kInfantry, 4, 2));
		t1.back().domain = 2;
		B.drain(1, b1, 1000.0);
		B.plots(1);
		B.players(1, {Player(0, 77, 14), Player(1, 71, 8)});
		B.units(1, srvunits::rec::kUnitsFirstCopy, t1);
		B.frame(1, 1200);
		SrvFeed::Batch b2;
		b2.unitTableGen = 1;
		b2.events.push_back(Ev(16, SrvFeed::kUnitCombat, 4, 0, 2005, 0, 0));
		SrvFeed::CombatRecord c;
		memset(&c, 0, sizeof(c));
		c.seq = 16;
		c.kind = SrvFeed::kUnitCombat;
		c.flags = SrvFeed::kCombatVisualize | SrvFeed::kCombatReturnedKnown | SrvFeed::kCombatBombing | SrvFeed::kCombatRanged;
		c.plot = 4;
		c.attackerOwner = 0;
		c.attackerUnit = 2005;
		c.attackerCity = -1;
		c.defenderOwner = -1;
		c.defenderUnit = -1;
		c.defenderCity = -1;
		c.returned = 73;
		c.nuclearLevel = 1;
		c.firstMember = 0;
		c.nMembers = 2;
		b2.combats.push_back(c);
		SrvFeed::CombatMember m;
		memset(&m, 0, sizeof(m));
		m.player = 1;
		m.id = 3006;
		m.damage = 100;
		m.finalDamage = 100;
		m.maxHP = 100;
		m.x = m.y = -1;
		b2.combatMembers.push_back(m);
		m.id = 4;
		m.flags = 1;
		m.damage = 80;
		m.finalDamage = 80;
		m.maxHP = 300;
		b2.combatMembers.push_back(m);
		b2.events.push_back(Ev(17, SrvFeed::kUnitSetDamage, 4, 1, 3006, 100, 0));
		B.drain(2, b2, 1300);
		std::vector<SrvFeed::UnitRecord> t2 = t1;
		t2[1].damage = 100;
		t2[1].seq = 17;
		B.units(2, srvunits::rec::kUnitsLaterCopy, t2);
		// r1's server frames (the record's dt, ms): 142 the fight's start, 143 its first advance, ... 196 the limit
		static const double kR1[] = {4443.0, 6080.2, 5243.0, 2805.7, 955.7, 10.4, 6.9,  9.7,  9.4,  11.8, 3.7,  120.3, 11.6, 3.6,
		                             9.3,    7.3,    6.3,    7.7,    7.9,   6.9,  6.4,  6.7,  6.5,  10.8, 10.9, 10.7,  8.1,  9.6,
		                             16.6,   5.5,    27.4,   31.2,   33.8,  31.5, 2.6,  32.0, 32.7, 32.8, 33.4, 33.7,  33.1, 33.5,
		                             33.3,   33.6,   32.8,   33.6,   33.8,  33.9, 32.4, 33.9, 32.6, 33.5, 32.9, 33.0,  4899.8};
		const size_t n = v == "nuker1" ? sizeof(kR1) / sizeof(kR1[0]) : 2;   // nukelong: the start and the 6.08 s advance
		double t = 1400;
		B.frame(2, t);   // the combat is added
		for(size_t k = 0; k < n; ++k)
			B.frame(2, t += kR1[k]);
		for(int k = 0; k < 1200; ++k)   // 40 s at 30 per second
			B.frame(2, t += 1000.0 / 30.0);
	}
	else
	{
		// loop 1
		SrvFeed::Batch b1;
		b1.unitTableGen = 1;
		b1.events.push_back(Ev(1, SrvFeed::kUnitCreated, 0, 0, 1001, kWarrior));
		b1.events.push_back(Ev(2, SrvFeed::kUnitCreated, 5, 1, 1002, kSpearman));
		b1.events.push_back(Ev(3, SrvFeed::kUnitVisibility, 5, 1, 1002, 1, FloatBits(0.5f)));
		B.drain(1, b1, v == "noclock" ? -1.0 : 1000.0);
		B.plots(1);
		std::vector<SrvFeed::UnitRecord> t1 = {Unit(0, 1001, kWarrior, 0, 1), Unit(1, 1002, kSpearman, 5, 3)};
		if(copies)
		{
			B.players(1, {Player(0, 77, 14), Player(1, 71, 8)});
			B.units(1, srvunits::rec::kUnitsFirstCopy, t1);
		}
		B.frame(1, 1200);
		// loop 2: a move (path 0 -> 1 -> 3) and a combat
		SrvFeed::Batch b2;
		b2.unitTableGen = 1;
		b2.events.push_back(Ev(4, SrvFeed::kUnitMoved, 3, 0, 1001, 0, 3));
		b2.pathPlots = {0, 1, 3};
		b2.paths = {{4, 0, 3}};
		b2.events.push_back(Ev(5, SrvFeed::kUnitCombat, 5, 0, 1001, 0, 0));
		SrvFeed::CombatRecord c;
		memset(&c, 0, sizeof(c));
		c.seq = 5;
		c.kind = SrvFeed::kUnitCombat;
		c.flags = SrvFeed::kCombatVisualize | SrvFeed::kCombatGameRead;
		c.plot = 5;
		c.attackerOwner = 0;
		c.attackerUnit = 1001;
		c.attackerCity = -1;
		c.defenderOwner = 1;
		c.defenderUnit = 1002;
		c.defenderCity = -1;
		c.finalDamage[1] = 40;
		c.nMembers = 1;
		b2.combats = {c};
		SrvFeed::CombatMember m;
		memset(&m, 0, sizeof(m));
		m.player = 1;
		m.id = 1002;
		m.damage = 40;
		m.finalDamage = 40;
		m.maxHP = 100;
		m.x = 1;
		m.y = 2;
		b2.combatMembers = {m};
		b2.players = {Player(1, 71, 8)};
		B.drain(2, b2, 1300);
		std::vector<SrvFeed::UnitRecord> t2 = {Unit(0, 1001, kWarrior, 3, 5), Unit(1, 1002, kSpearman, 5, 5)};
		t2[1].damage = 40;
		if(copies && v != "missingcopy")
			B.units(2, srvunits::rec::kUnitsLaterCopy, t2);
		B.frame(2, 1400);
		// loop 3: no unit kind (the scene takes no copy)
		SrvFeed::Batch b3;
		b3.unitTableGen = 1;
		b3.events.push_back(Ev(6, SrvFeed::kPlotState, 2, -1, -1, -1, -1));
		b3.changedPlots = {2};
		B.drain(3, b3, 1450);
		if(v == "extracopy")
			B.units(3, srvunits::rec::kUnitsLaterCopy, t2);
		// loop 4: the removal of (1, 1002)
		SrvFeed::Batch b4;
		b4.unitTableGen = 1;
		b4.events.push_back(Ev(7, SrvFeed::kUnitDestroyed, 5, 1, 1002));
		B.drain(4, b4, 1500);
		if(copies)
			B.units(4, srvunits::rec::kUnitsLaterCopy, {t2[0]});
		B.frame(4, 1600);
	}
	if(!B.ok)
		return 1;
	// the units_db member (format version 5): the database file, whole, under the generation 1 the scene opened
	std::vector<uint8_t> db;
	if(FILE* f = _wfopen(sb::Widen(argv[3]).c_str(), L"rb"))
	{
		uint8_t buf[65536];
		size_t n;
		while((n = fread(buf, 1, sizeof(buf), f)) > 0)
			db.insert(db.end(), buf, buf + n);
		fclose(f);
	}
	else
	{
		printf("fixture: cannot read the database %s\n", argv[3]);
		return 1;
	}
	sb::Writer w;
	c5r::Status st = w.open(sb::Widen(argv[2]));
	std::vector<uint8_t> hf = Heightfield();
	if(st.ok())
		st = w.addMember("heightfield/load0001.c5hf", sb::MemberKind::Heightfield, hf.data(), hf.size());
	if(st.ok())
		st = w.addMember(sb::UnitsDbPath(1), sb::MemberKind::UnitsDb, db.data(), db.size());
	if(st.ok())
		st = w.appendStart("feed.bin", sb::MemberKind::Feed);
	if(st.ok())
		st = w.append("feed.bin", B.feed.data() + 16, B.feed.size() - 16);
	if(st.ok())
		st = w.appendStart("frames.bin", sb::MemberKind::Frames);
	if(st.ok())
		st = w.appendStart("cameras.bin", sb::MemberKind::CameraBlocks);
	for(size_t k = 0; st.ok() && k < B.frames.size(); ++k)
	{
		VPShellCamera cam;
		memset(&cam, 0, sizeof(cam));
		cam.status = 1;
		cam.frameStamp = B.frames[k].frameId;
		cam.numViews = 1;
		for(int i = 0; i < 16; ++i)
		{
			cam.view[0][i] = i % 5 == 0 ? 1.0f : 0.0f;
			cam.proj[i] = i % 5 == 0 ? 1.0f : 0.0f;
		}
		cam.proj[0] = Bits(0x7FC00000);   // not finite: the cull is off, every listed unit is in wrap copy 0
		cam.vpW = cam.bbW = 8;
		cam.vpH = cam.bbH = 4;
		st = w.append("frames.bin", &B.frames[k], sizeof(B.frames[k]));
		if(st.ok())
			st = w.append("cameras.bin", &cam, sizeof(cam));
	}
	sb::Manifest m;
	m.bundleId = "SYNTH-4457-" + v + "_20261116-000000_test";
	m.created = "2026-11-16T00:00:00-05:00";
	m.recorderKeys["render_test_record"] = "C:/c5-64bit-work/0375/4457/synthetic";
	m.serverExeSha256 = std::string(64, 'a');
	m.repoCommit = "synthetic";
	m.buildConfig = "release";
	m.saveName = "SYNTH-4457";
	m.saveSha256 = std::string(64, 'd');
	m.mapW = 2;
	m.mapH = 3;
	m.turn = 3;
	m.loadSeq = 1;
	m.vt = "absent";
	m.install = "C:/nowhere";
	m.machine = "test";
	m.renderConfig["render_units"] = "1";
	m.firstFrameId = 100;
	m.frameCount = (uint32_t)B.frames.size();
	m.versionNotes.push_back("synthetic: issue 4457's unitfeed fixture (" + v + ")");
	m.versionNotes.push_back(std::string(sb::kUnitsNoHistoryNote) + ": none recorded");   // the controller starts at frame 100
	for(const char* n : sb::kCounterNames)
		m.counters[n] = 0;
	if(st.ok())
		st = w.finish(m);
	if(!st.ok())
	{
		printf("fixture: %s\n", st.str().c_str());
		return 1;
	}
	printf("fixture: %s written to %s (%zu frames, feed %zu bytes)\n", v.c_str(), argv[2], B.frames.size(), B.feed.size());
	return 0;
}
