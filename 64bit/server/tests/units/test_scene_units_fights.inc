// test_scene_units_fights.inc - issue 4774 (epic 0042): the per-unit hold and the fight player inside the unit scene's
// own code (AnimFeed, UnitsCore, AnimRunner). Written 2026-11-20 for this repository (GPL-3.0-or-later). Included by
// test_scene_units.cpp inside its anonymous namespace (its helpers: FakePort, Rec, Player, Ev, BatchOf, TestLookups,
// Kinds, Host, TestDb, Target, Grid, LookAt, EXPECT); its cases are the fight_* rows of kCases.
//
// The fights' sequences are the EXE queue's of combat-choreography.md 3.4 (0321): a combat, its damage, death, removal
// and advance in one drain; an uninvolved unit's move in the same drain. A test double stands in for the choreography
// (a fight of N frames) except in fight_controller, which plays the 4775 core against the controller on the install.

class FakeFightPlayer : public AnimFightPlayer
{
public:
	std::map<uint32_t, int> length;   // advance calls until the fight ends by itself (default 2)
	std::set<uint32_t> refuse;
	std::vector<uint32_t> started, ended;
	FightPlan start(const Fight& f) override
	{
		started.push_back(f.seq);
		FightPlan p;
		p.ok = !refuse.count(f.seq);
		if(!p.ok)
		{
			p.why = "AirCombat: an air member (4777) [test]";
			return p;
		}
		left_[f.seq] = length.count(f.seq) ? length[f.seq] : 2;
		ca::TimelineEvent e;   // one event of the fight in its start frame (the attacker's strike)
		e.kind = ca::EventKind::Action;
		e.action = ca::ActionKind::Attack;
		e.player = f.attacker.owner;
		e.id = f.attacker.unit;
		pending_.push_back(e);
		return p;
	}
	bool advance(const Fight& f, double) override { return --left_[f.seq] <= 0; }
	void end(const Fight& f, FightEnd) override { ended.push_back(f.seq); }
	void takeEvents(std::vector<ca::TimelineEvent>& out) override
	{
		out.insert(out.end(), pending_.begin(), pending_.end());
		pending_.clear();
	}
	void figures(const std::set<std::pair<int32_t, int32_t>>&, std::vector<FigureInstance>& out) override { out.clear(); }

private:
	std::map<uint32_t, int> left_;
	std::vector<ca::TimelineEvent> pending_;
};

const uint16_t kFightFlags = SrvFeed::kCombatVisualize | SrvFeed::kCombatGameRead | SrvFeed::kCombatActiveTurn |
                             SrvFeed::kCombatReturnedKnown | SrvFeed::kCombatAttackerAdvances;

// Adds a combat event (seq) of attacker (ao, ai) on defender (dob, di) to b with its record.
void AddCombat(SrvFeed::Batch& b, uint32_t seq, int ao, int ai, int dob, int di, uint16_t flags, uint32_t returned, int finalA = 0,
               int finalD = 0)
{
	SrvFeed::CombatRecord r;
	memset(&r, 0, sizeof(r));
	r.seq = seq;
	r.kind = SrvFeed::kUnitCombat;
	r.flags = flags;
	r.plot = -1;
	r.attackerOwner = ao;
	r.attackerUnit = ai;
	r.attackerCity = -1;
	r.defenderOwner = dob;
	r.defenderUnit = di;
	r.defenderCity = -1;
	r.finalDamage[0] = finalA;
	r.finalDamage[1] = finalD;
	r.returned = returned;
	SrvFeed::Event e = Ev(seq, SrvFeed::kUnitCombat, ao, ai, (int)b.combats.size(), 0);
	b.combats.push_back(r);
	b.events.push_back(e);
}

void AddMove(SrvFeed::Batch& b, uint32_t seq, int owner, int id, std::vector<int32_t> plots)
{
	SrvFeed::PathEntry pe;
	pe.seq = seq;
	pe.first = (uint32_t)b.pathPlots.size();
	pe.length = (uint32_t)plots.size();
	b.pathPlots.insert(b.pathPlots.end(), plots.begin(), plots.end());
	b.events.push_back(Ev(seq, SrvFeed::kUnitMoved, owner, id, (int)b.paths.size(), 0));
	b.paths.push_back(pe);
}

// The scene's frame as the host makes it, without a controller: build, the feed's close, the view on each copy.
struct FightRig
{
	FakePort port;
	UnitsCore core;
	AnimFeed af;
	FakeFightPlayer fp;
	BuiltFrame bf;
	ca::TimelineFrame f;
	std::vector<cu::PlayerInput> pl;
	double t = 1000;
	int64_t id = 41;
	explicit FightRig(bool player = true) : core(port, Log)
	{
		port.players = {Player(0, 3, 5, 0), Player(1, 3, 5, 0)};
		core.setDatabase(TestLookups(), nullptr);
		if(player)
			af.setFightPlayer(&fp);
		core.setAnimFeed(&af);
	}
	void frame()
	{
		core.build(nullptr, nullptr, t, bf, id);
		af.close(core, t, id, f, pl);
		for(cu::UnitScene& sc : bf.copies)
		{
			const int c = af.view().nextCopy();
			af.view().applyTo(sc, c);
			af.view().remember(sc, c);
		}
		t += 1000.0 / 30.0;
		id++;
	}
	const cu::UnitInput* listed(int owner, int unit) const
	{
		for(const cu::UnitInput& u : bf.copies[1].units)
			if(u.player == owner && u.id == unit)
				return &u;
		return nullptr;
	}
	// three warriors: (0, 1001) on plot 23, (1, 2001) on 24, (0, 1003) on 30; created and taken at frame 41
	void start()
	{
		port.units = {Rec(0, 1001, 7, 23, 2, cu::kFlagVisible, 0, 100, 1), Rec(0, 1003, 7, 30, 2, cu::kFlagVisible, 0, 100, 3),
		              Rec(1, 2001, 7, 24, 5, cu::kFlagVisible, 0, 100, 2)};
		af.start();
		core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 23), Ev(2, SrvFeed::kUnitCreated, 1, 2001, 7, 0, 24),
		                      Ev(3, SrvFeed::kUnitCreated, 0, 1003, 7, 0, 30)}),
		             t);
		frame();
	}
	// one drain: 1001 attacks 2001 (seq 4), kills it and advances onto 24; 1003 walks elsewhere; the table is final
	void killAndAdvance(uint16_t flags)
	{
		port.units = {Rec(0, 1001, 7, 24, 1, cu::kFlagVisible, 20, 100, 9), Rec(0, 1003, 7, 31, 1, cu::kFlagVisible, 0, 100, 10)};
		SrvFeed::Batch b = BatchOf({});
		AddCombat(b, 4, 0, 1001, 1, 2001, flags, 777, 20, 100);
		b.events.push_back(Ev(5, SrvFeed::kUnitSetDamage, 1, 2001, 100, 0));
		b.events.push_back(Ev(6, SrvFeed::kUnitDestroyedInCombat, 1, 2001));
		b.events.push_back(Ev(7, SrvFeed::kUnitDestroyed, 1, 2001));
		b.events.push_back(Ev(8, SrvFeed::kUnitSetDamage, 0, 1001, 20, 0));
		AddMove(b, 9, 0, 1001, {24});
		AddMove(b, 10, 0, 1003, {31});
		b.events.push_back(Ev(11, SrvFeed::kUnitFortify, 0, 1001, 1));
		core.onBatch(b, t);
	}
};

// The fight holds its units' damage, death, removal and move until its end; another unit's move and a fortify pass; the
// held units are drawn from their views (the attacker on its old plot, the killed defender still listed).
bool CaseFightHold()
{
	FightRig r;
	r.start();
	EXPECT(r.af.held() == 3);
	r.killAndAdvance(kFightFlags);
	r.frame();   // 42: the combat is added; its units are held from it on
	EXPECT(Kinds(r.f) == "move:1003,arrive:1003,fortify:1001");
	EXPECT(r.af.hold().held(0, 1001) && r.af.hold().held(1, 2001) && !r.af.hold().held(0, 1003) && r.af.hold().queued() == 5);
	EXPECT(r.af.counters().combats == 1 && r.fp.started.empty());
	const cu::UnitInput* a = r.listed(0, 1001);
	const cu::UnitInput* d = r.listed(1, 2001);
	const cu::UnitInput* o = r.listed(0, 1003);
	EXPECT(a && a->plot_x == 3 && a->plot_y == 2 && a->damage == 0 && a->facing == 2);   // frozen: plot 23, no damage yet
	EXPECT(d && d->plot_x == 4 && d->plot_y == 2 && d->damage == 0 && d->facing == 5);   // its removal waits: listed again
	EXPECT(o && o->plot_x == 1 && o->plot_y == 3);                                       // the table's (plot 31)
	r.frame();   // 43: the fight starts
	EXPECT(Kinds(r.f) == "action:1001" && r.fp.started.size() == 1 && r.fp.started[0] == 4 && r.af.counters().fightEvents == 1);
	r.frame();   // 44
	EXPECT(Kinds(r.f).empty() && r.af.hold().queued() == 5 && r.listed(1, 2001));
	r.frame();   // 45: the fight ends by itself; its ops come back in the DLL's order, the table drawn again
	EXPECT(Kinds(r.f) == "damage:2001,destroyed:2001,remove:2001,damage:1001,move:1001,arrive:1001");
	EXPECT(r.af.lastFightEnds().size() == 1 && r.af.lastFightEnds()[0].end.why == FightEnd::Natural &&
	       r.af.lastFightEnds()[0].end.owner == 777 && r.af.lastFightEnds()[0].released == 5 && r.af.lastFightEnds()[0].exeFrame == 45);
	EXPECT(!r.af.hold().held(0, 1001) && r.af.hold().queued() == 0 && r.af.view().views() == 0);
	a = r.listed(0, 1001);
	EXPECT(a && a->plot_x == 4 && a->damage == 20 && !r.listed(1, 2001));
	EXPECT(r.af.hold().counters().held == 5 && r.af.hold().counters().released == 5 && r.af.counters().ownerEnds == 1);
	r.frame();
	EXPECT(Kinds(r.f).empty());
	return true;
}

// Quick Combat on: no fight, nothing held: every event passes at once in the DLL's order.
bool CaseFightQuickCombat()
{
	FightRig r;
	r.start();
	r.killAndAdvance(SrvFeed::kCombatVisualize | SrvFeed::kCombatGameRead | SrvFeed::kCombatQuickOption | SrvFeed::kCombatReturnedKnown);
	r.frame();
	EXPECT(Kinds(r.f) == "damage:2001,destroyed:2001,remove:2001,damage:1001,move:1001,arrive:1001,move:1003,arrive:1003,fortify:1001");
	EXPECT(r.fp.started.empty() && r.af.hold().counters().held == 0 && r.af.fights()->counters().noFight == 1);
	return true;
}

// Without a player (no runner): the combat records are counted and nothing is held (the scene as before 4774). And a
// combat event whose record is not in its batch is counted, not a fight.
bool CaseFightNoPlayer()
{
	FightRig r(false);
	r.start();
	r.killAndAdvance(kFightFlags);
	r.frame();
	EXPECT(Kinds(r.f) == "damage:2001,destroyed:2001,remove:2001,damage:1001,move:1001,arrive:1001,move:1003,arrive:1003,fortify:1001");
	EXPECT(r.af.counters().combats == 1 && !r.af.fights() && r.af.hold().counters().held == 0);
	SrvFeed::Batch b = BatchOf({Ev(12, SrvFeed::kUnitCombat, 0, 1001, 3, 0)});
	r.af.take(b.events[0], b);
	EXPECT(r.af.counters().combatsBad == 1 && r.af.counters().combats == 1);
	return true;
}

// One unit fights twice (a blitz): what came between fight A and fight B comes back at A's end; B starts the frame after
// it and holds the rest (0321 3.4: several combats on one unit play in order).
bool CaseFightTwoOnOneUnit()
{
	FightRig r;
	r.start();
	r.port.units = {Rec(0, 1001, 7, 23, 2, cu::kFlagVisible, 15, 100, 8), Rec(0, 1003, 7, 30, 2, cu::kFlagVisible, 30, 100, 7),
	                Rec(1, 2001, 7, 24, 5, cu::kFlagVisible, 40, 100, 5)};
	SrvFeed::Batch b = BatchOf({});
	AddCombat(b, 4, 0, 1001, 1, 2001, kFightFlags, 50, 10, 40);
	b.events.push_back(Ev(5, SrvFeed::kUnitSetDamage, 1, 2001, 40, 0));
	AddCombat(b, 6, 0, 1001, 0, 1003, kFightFlags, 51, 15, 30);   // a test pairing: the second target is 1003
	b.events.push_back(Ev(7, SrvFeed::kUnitSetDamage, 0, 1003, 30, 0));
	b.events.push_back(Ev(8, SrvFeed::kUnitSetDamage, 0, 1001, 15, 0));
	r.core.onBatch(b, r.t);
	r.fp.length[4] = 1;
	r.fp.length[6] = 1;
	r.frame();   // 42
	EXPECT(Kinds(r.f).empty() && r.af.hold().queued() == 3);
	r.frame();   // 43: A starts, B waits (it names 1001)
	EXPECT(Kinds(r.f) == "action:1001" && r.fp.started.size() == 1);
	r.frame();   // 44: A ends: 2001's damage (owned by A) comes back; 1001's (owned by B) waits
	EXPECT(Kinds(r.f) == "damage:2001" && r.af.hold().held(0, 1001) && !r.af.hold().held(1, 2001));
	r.frame();   // 45: B starts
	EXPECT(Kinds(r.f) == "action:1001" && r.fp.started.size() == 2 && r.fp.started[1] == 6);
	r.frame();   // 46: B ends
	EXPECT(Kinds(r.f) == "damage:1003,damage:1001" && r.af.hold().queued() == 0 && r.af.counters().ownerEnds == 2);
	return true;
}

// A load during a fight: the clock ends it (OwnerEnd Reset, so the proxy's hold is dropped), the held ops of the old game
// are dropped, the load removes every unit.
bool CaseFightLoad()
{
	FightRig r;
	r.start();
	r.killAndAdvance(kFightFlags);
	r.frame();   // 42
	EXPECT(r.af.hold().queued() == 5);
	r.port.units.clear();
	r.core.onBatch(BatchOf({Ev(12, SrvFeed::kLoadBegin, 1, 0)}), r.t);
	r.frame();   // 43: the fight starts (its strike) and the load ends it
	EXPECT(Kinds(r.f) == "action:1001,remove:1001,remove:1003,remove:2001");
	EXPECT(r.af.lastFightEnds().size() == 1 && r.af.lastFightEnds()[0].end.why == FightEnd::Reset && r.af.lastFightEnds()[0].released == 0);
	EXPECT(r.af.hold().queued() == 0 && r.af.hold().counters().droppedLoad == 5 && r.af.view().views() == 0 && r.fp.ended.size() == 1);
	EXPECT(r.af.fights()->queued() == 0 && r.af.fights()->playing() == 0);
	return true;
}

// The choreography refuses the fight (4777's cases): it ends in its start frame, the ops come back at once, LOUD text.
bool CaseFightRefused()
{
	FightRig r;
	r.start();
	r.killAndAdvance(kFightFlags);
	r.fp.refuse.insert(4);
	r.frame();   // 42
	r.frame();   // 43
	EXPECT(Kinds(r.f) == "damage:2001,destroyed:2001,remove:2001,damage:1001,move:1001,arrive:1001");
	EXPECT(r.af.lastFightEnds().size() == 1 && r.af.lastFightEnds()[0].end.why == FightEnd::Refused);
	EXPECT(r.af.lastLoud().size() == 1 && r.af.lastLoud()[0].find("fight seq 4 (event id 777) refused by the choreography: AirCombat") == 0);
	return true;
}

// Overflow: a held unit's queue beyond kHoldUnitMax comes back at once, in order, LOUD; the unit then passes.
bool CaseFightOverflow()
{
	FightRig r;
	r.start();
	SrvFeed::Batch b = BatchOf({});
	AddCombat(b, 4, 0, 1001, 1, 2001, kFightFlags, 9);
	for(uint32_t i = 0; i < 300; ++i)
		b.events.push_back(Ev(5 + i, SrvFeed::kUnitSetDamage, 0, 1001, (int)(i % 90), 0));
	r.port.units[0].seq = 304;
	r.core.onBatch(b, r.t);
	r.fp.length[4] = 1000;
	r.frame();
	size_t dmg = 0;
	bool inOrder = true;
	for(const ca::TimelineEvent& e : r.f.events)
		if(e.kind == ca::EventKind::Damage)
		{
			inOrder = inOrder && e.a == (int)(dmg % 90);
			dmg++;
		}
	EXPECT(dmg == 300 && inOrder && r.af.hold().queued() == 0 && r.af.hold().counters().overflowUnit == 1);
	EXPECT(r.af.lastLoud().size() == 1 && r.af.lastLoud()[0].find("its held ops reached 256") != std::string::npos);
	EXPECT(r.af.hold().named(0, 1001) && !r.af.hold().held(0, 1001));
	return true;
}

// Issue 4783 (W33 #8801): the combat's record was drained before the forwarded call returned (kCombatReturnedKnown clear);
// the EXE's event id comes in a later batch's lateResults. (a) before the next close: the record not yet added takes it;
// (b) while the fight plays: the clock's fight takes it; either way the fight's OwnerEnd carries it. (c) never (the
// negative, the feed before 4783): the OwnerEnd is owner 0, counted and LOUD.
bool CaseFightLateResult()
{
	const uint16_t lateFlags = (uint16_t)(kFightFlags & ~SrvFeed::kCombatReturnedKnown);
	SrvFeed::Batch late = BatchOf({});
	late.lateResults.push_back(SrvFeed::LateCombatResult{4, 777});
	late.lateResults.push_back(SrvFeed::LateCombatResult{99, 5});   // a seq no fight has (Quick Combat, not visualised)
	{
		FightRig r;   // (a)
		r.af.keepFightEnds(true);
		r.start();
		r.killAndAdvance(lateFlags);
		r.core.onBatch(late, r.t);
		r.frame();   // 42: the record completed, then added
		EXPECT(r.af.counters().lateResults == 2 && r.af.counters().lateToRecord == 1 && r.af.counters().lateNoFight == 1);
		EXPECT(r.af.fights()->counters().ownerUnknown == 0);
		for(int k = 0; k < 3; ++k)
			r.frame();   // 43 start, 44, 45 end
		EXPECT(r.af.fightEnds().size() == 1 && r.af.fightEnds()[0].end.owner == 777);
		EXPECT(r.af.counters().ownerEnds == 1 && r.af.counters().ownerZeroEnds == 0);
	}
	{
		FightRig r;   // (b)
		r.af.keepFightEnds(true);
		r.start();
		r.killAndAdvance(lateFlags);
		r.frame();   // 42: added without an id
		r.frame();   // 43: started without an id
		EXPECT(r.fp.started.size() == 1 && r.af.fights()->counters().ownerUnknown == 1);
		r.core.onBatch(late, r.t);
		r.frame();   // 44: the id reaches the playing fight
		EXPECT(r.af.counters().lateToFight == 1 && r.af.fights()->counters().ownerLate == 1);
		r.frame();   // 45: the end
		EXPECT(r.af.fightEnds().size() == 1 && r.af.fightEnds()[0].end.owner == 777 && r.af.counters().ownerZeroEnds == 0);
	}
	{
		FightRig r;   // (c)
		r.af.keepFightEnds(true);
		r.start();
		r.killAndAdvance(lateFlags);
		bool loud = false;
		for(int k = 0; k < 4; ++k)
		{
			r.frame();
			for(const std::string& l : r.af.lastLoud())
				loud = loud || l.find("fight seq 4 ends (OwnerEnd Natural) with no EXE event id") == 0;
		}
		EXPECT(r.af.fightEnds().size() == 1 && r.af.fightEnds()[0].end.owner == 0);
		EXPECT(r.af.counters().ownerZeroEnds == 1 && r.af.fights()->counters().ownerZeroEnds == 1 && loud);
	}
	return true;
}

// The install-backed case: two warriors, the 4775 core played on the controller (ControllerFeedback): the fighters carry
// the scene's 'held in a fight' field while the fight plays, the defender's damage waits for its end, and the fight ends
// by itself before the 12 s limit.
// 5066 (W117): rush = the warriors as a VP mounted unit's combat row (RushAttackFormation 1, ShortMoveRadius 24, RunCharge
// without AttackCharge: the knight's shape of 5057), so the core plays its rush and ride-through path on the warrior models.
EnvOpener TestEnvCombat(bool visKill = false, bool eras = false, bool geometry = false, bool rush = false)
{
	return [=](const std::wstring&, Env& out, std::string& why) {
		const c5r::Status s = out.gf.open(c5r::gamefs::fromUtf8(g_game));
		if(!s.ok())
		{
			why = s.str();
			return false;
		}
		std::unique_ptr<c5m::TsvTables> db = TestDb(false);
		c5m::Error e;
		const char* enable = "Idle Attack RunCharge AttackCity Death BombardDefend Run Fortify CombatReady";
		// 4776's motion columns NULL: the EXE's defaults (CombatMotion)
		const char* n = "\\N";
		const char* rf = rush ? "1" : n;    // 5066: RushAttackFormation
		const char* sr = rush ? "24" : n;   // 5066: ShortMoveRadius
		db->add("ArtDefine_UnitMemberCombats",
		        Tsv({{"UnitMemberType", "EnableActions", "RushAttackFormation", "LastToDie", "HasShortRangedAttack", "HasLongRangedAttack",
		              "MoveRadius", "ShortMoveRadius", "MoveRate", "ShortMoveRate", "TurnRateMin", "TurnRateMax", "ReformBeforeCombat",
		              "HasCollisionAttack", "AttackAltitude"},   // 4777's columns (4724 and 4777 together)
		             {"ART_DEF_UNIT_MEMBER_WARRIOR_V2", enable, rf, "0", "0", "0", n, sr, n, n, n, n, n, "0", n},
		             {"ART_DEF_UNIT_MEMBER_WARRIOR", enable, rf, "0", "0", "0", n, sr, n, n, n, n, n, "0", n},
		             {"ART_DEF_UNIT_MEMBER_WARRIOR_V4", enable, rf, "0", "0", "0", n, sr, n, n, n, n, n, "0", n},
		             {"ART_DEF_UNIT_MEMBER_TRIREME", "Idle Attack AttackCity Death BombardDefend Run Fortify CombatReady", n, "0", "0", "0", n, n,
		              n, n, n, n, n, "0", n}}),
		        &e);
		db->add("ArtDefine_UnitMemberCombatWeapons",
		        Tsv({{"UnitMemberType", "Index", "WeaponTypeTag", "IsDropped", "TargetGround", "ProjectileSpeed", "AreaDamageDelay",
		              "WaitForEffectCompletion", "HitEffect", "HitEffectScale", "WeaponTypeSoundOverrideTag"},   // 4777's columns
		             {"ART_DEF_UNIT_MEMBER_WARRIOR_V2", "0", "BLUNT", "0", "0", n, n, "0", n, n, n},
		             {"ART_DEF_UNIT_MEMBER_WARRIOR", "0", "BLUNT", "0", "0", n, n, "0", n, n, n},
		             {"ART_DEF_UNIT_MEMBER_WARRIOR_V4", "0", "BLUNT", "0", "0", n, n, "0", n, n, n}}),
		        &e);
		if(visKill)   // 4783 (W33, C1): the weapon table with the VisKillStrength columns (NULL: one figure per landing)
			db->add("ArtDefine_UnitMemberCombatWeapons",
			        Tsv({{"UnitMemberType", "Index", "WeaponTypeTag", "IsDropped", "TargetGround", "ProjectileSpeed", "AreaDamageDelay",
			              "WaitForEffectCompletion", "HitEffect", "HitEffectScale", "WeaponTypeSoundOverrideTag", "VisKillStrengthMin",
			              "VisKillStrengthMax"},
			             {"ART_DEF_UNIT_MEMBER_WARRIOR_V2", "0", "BLUNT", "0", "0", n, n, "0", n, n, n, n, n},
			             {"ART_DEF_UNIT_MEMBER_WARRIOR", "0", "BLUNT", "0", "0", n, n, "0", n, n, n, n, n},
			             {"ART_DEF_UNIT_MEMBER_WARRIOR_V4", "0", "BLUNT", "0", "0", n, n, "0", n, n, n, n, n}}),
			        &e);
		if(eras)   // ticket 5062: VP's Eras.CityBombardEffectTag (eras 0-1 EARLY, 2-4 MID, 5-7 LATE; research 18.37) beside
		           // TestDb's columns (this table replaces TestDb's: every era keeps ArtPrefix ANCIENT, so the art stays the same)
			db->add("Eras", Tsv({{"ID", "Type", "ArtPrefix", "CityBombardEffectTag"}, {"0", "ERA_ANCIENT", "ANCIENT", "CITY_BOMBARD_EARLY"},
			                     {"1", "ERA_CLASSICAL", "ANCIENT", "CITY_BOMBARD_EARLY"}, {"2", "ERA_MEDIEVAL", "ANCIENT", "CITY_BOMBARD_MID"},
			                     {"3", "ERA_RENAISSANCE", "ANCIENT", "CITY_BOMBARD_MID"}, {"4", "ERA_INDUSTRIAL", "ANCIENT", "CITY_BOMBARD_MID"},
			                     {"5", "ERA_MODERN", "ANCIENT", "CITY_BOMBARD_LATE"}, {"6", "ERA_POSTMODERN", "ANCIENT", "CITY_BOMBARD_LATE"},
			                     {"7", "ERA_FUTURE", "ANCIENT", "CITY_BOMBARD_LATE"}}),
			        &e);
		if(geometry)   // ticket 5062 (W107): the columns the trigger stream's fight geometry reads (FightGeometry::open:
		               // loadProjectileWeapons, ArtDefine_UnitMemberCombats TargetRadius / TargetHeight), so that its routes open
		{
			db->add("ArtDefine_UnitMemberCombats",
			        Tsv({{"UnitMemberType", "EnableActions", "RushAttackFormation", "LastToDie", "HasShortRangedAttack", "HasLongRangedAttack",
			              "MoveRadius", "ShortMoveRadius", "MoveRate", "ShortMoveRate", "TurnRateMin", "TurnRateMax", "ReformBeforeCombat",
			              "HasCollisionAttack", "AttackAltitude", "TargetRadius", "TargetHeight"},
			             {"ART_DEF_UNIT_MEMBER_WARRIOR_V2", enable, rf, "0", "0", "0", n, sr, n, n, n, n, n, "0", n, n, n},
			             {"ART_DEF_UNIT_MEMBER_WARRIOR", enable, rf, "0", "0", "0", n, sr, n, n, n, n, n, "0", n, n, n},
			             {"ART_DEF_UNIT_MEMBER_WARRIOR_V4", enable, rf, "0", "0", "0", n, sr, n, n, n, n, n, "0", n, n, n},
			             {"ART_DEF_UNIT_MEMBER_TRIREME", "Idle Attack AttackCity Death BombardDefend Run Fortify CombatReady", n, "0", "0", "0",
			              n, n, n, n, n, n, n, "0", n, n, n}}),
			        &e);
			db->add("ArtDefine_UnitMemberCombatWeapons",
			        Tsv({{"UnitMemberType", "Index", "SubIndex", "ID", "ProjectileSpeed", "ProjectileTurnRateMin", "ProjectileTurnRateMax",
			              "HitEffect", "HitEffectScale", "HitRadius", "ProjectileChildEffectScale", "AreaDamageDelay", "ContinuousFire",
			              "WaitForEffectCompletion", "TargetGround", "IsDropped", "WeaponTypeTag", "MissTargetSlopRadius",
			              "WeaponTypeSoundOverrideTag"},
			             {"ART_DEF_UNIT_MEMBER_WARRIOR_V2", "0", "0", n, n, n, n, n, n, n, n, n, "0", "0", "0", "0", "BLUNT", n, n},
			             {"ART_DEF_UNIT_MEMBER_WARRIOR", "0", "0", n, n, n, n, n, n, n, n, n, "0", "0", "0", "0", "BLUNT", n, n},
			             {"ART_DEF_UNIT_MEMBER_WARRIOR_V4", "0", "0", n, n, n, n, n, n, n, n, n, "0", "0", "0", "0", "BLUNT", n, n}}),
			        &e);
		}
		out.db = std::move(db);
		out.exe = c5r::gamefs::fromUtf8(g_exe);
		out.install = c5r::gamefs::fromUtf8(g_game);
		return true;
	};
}

bool CaseFightController()
{
	Host h(TestEnvCombat());
	h.port.players = {Player(0, 3, 5, 0), Player(1, 3, 5, 0)};
	h.u->poll();
	h.probe->waitPrecheck();
	EXPECT(h.probe->precheckPassed());
	h.port.units = {Rec(0, 1001, 7, 22, 1, cu::kFlagVisible, 0, 100, 1), Rec(1, 2001, 7, 23, 4, cu::kFlagVisible, 0, 100, 2)};
	h.u->feed(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 22), Ev(2, SrvFeed::kUnitCreated, 1, 2001, 7, 0, 23)}));
	h.u->setDevice(g_dev);
	Target t;
	const c5r::heightfield::HeightGrid g = Grid();
	float cx = 0, cy = 0;
	EXPECT(cu::plotCentre(g, 2, 2, cx, cy).ok());
	UnitsCamera cam = LookAt(cx, cy);
	auto draw = [&]() {
		t.clear();
		h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
		h.t += 1000.0 / 30.0;
		cam.frameId++;
	};
	for(int k = 0; k < 3; ++k)
		draw();
	EXPECT(h.probe->lastBuilt().copies[1].units.size() == 2);
	// 1001 attacks 2001: 2001 takes 40 damage, 1001 10; no kill, no advance
	h.port.units = {Rec(0, 1001, 7, 22, 1, cu::kFlagVisible, 10, 100, 5), Rec(1, 2001, 7, 23, 4, cu::kFlagVisible, 40, 100, 4)};
	SrvFeed::Batch b = BatchOf({});
	// the defender retaliates (its 10 damage on the attacker): without kCombatDefenderRetaliates the attacker's planned death
	// has no striker and the core holds the fight to its 12 s limit (found here, unit-combat-server.md 6.2)
	AddCombat(b, 3, 0, 1001, 1, 2001,
	          (uint16_t)((kFightFlags & ~SrvFeed::kCombatAttackerAdvances) | SrvFeed::kCombatDefenderRetaliates), 4242, 10, 40);
	b.events.push_back(Ev(4, SrvFeed::kUnitSetDamage, 1, 2001, 40, 0));
	b.events.push_back(Ev(5, SrvFeed::kUnitSetDamage, 0, 1001, 10, 0));
	h.u->feed(b);
	int heldFrames = 0, frames = 0;
	bool sawHeldDamage0 = false, ended = false;
	for(; frames < 30 * 14 && !ended; ++frames)
	{
		draw();
		const bool held = h.probe->core().heldInFight(0, 1001) && h.probe->core().heldInFight(1, 2001);
		if(held)
		{
			heldFrames++;
			for(const cu::UnitInput& u : h.probe->lastBuilt().copies[1].units)
				if(u.id == 2001 && u.damage == 0)
					sawHeldDamage0 = true;
		}
		else if(heldFrames)
			ended = true;
	}
	printf("fight_controller: held %d frames (%.2f s), ended %d, the defender drawn undamaged while held %d\n", heldFrames,
	       heldFrames / 30.0, ended ? 1 : 0, sawHeldDamage0 ? 1 : 0);
	for(const std::string& l : g_log)
		if(l.find("LOUD") != std::string::npos)
			printf("fight_controller: log: %s\n", l.c_str());
	EXPECT(ended && sawHeldDamage0);
	EXPECT(heldFrames > 30 && heldFrames < 12 * 30);   // the core's own end, not the 12 s limit (melee about 4-5 s, 4775)
	bool d40 = false;
	for(const cu::UnitInput& u : h.probe->lastBuilt().copies[1].units)
		d40 = d40 || (u.id == 2001 && u.damage == 40);
	EXPECT(d40);
	EXPECT(LogCount("refused by the choreography") == 0 && h.probe->core().counters().byStatus[(int)Status::kAnimFailed] == 0);
	// 4783 (W33, C1; MC #8906): the hand-made database has no VisKillStrength columns: the fallback rule, named once
	EXPECT(LogCount("units: combat tables: one figure per landing (database has no VisKillStrength columns)") == 1 &&
	       LogCount("units: combat tables: VisKillStrength per landing") == 0);
	return true;
}

// 4733 (W36): a fight at the 12 s limit names both units' types and each side's figure states in its LOUD line. The
// setup is fight_controller's without kCombatDefenderRetaliates: the attacker's planned death has no striker, the
// defender's figures bombard-defend throughout, and the core holds the fight to its limit.
bool CaseFightLimitLine()
{
	Host h(TestEnvCombat());
	h.port.players = {Player(0, 3, 5, 0), Player(1, 3, 5, 0)};
	h.u->poll();
	h.probe->waitPrecheck();
	EXPECT(h.probe->precheckPassed());
	h.port.units = {Rec(0, 1001, 7, 22, 1, cu::kFlagVisible, 0, 100, 1), Rec(1, 2001, 7, 23, 4, cu::kFlagVisible, 0, 100, 2)};
	h.u->feed(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 22), Ev(2, SrvFeed::kUnitCreated, 1, 2001, 7, 0, 23)}));
	h.u->setDevice(g_dev);
	Target t;
	const c5r::heightfield::HeightGrid g = Grid();
	float cx = 0, cy = 0;
	EXPECT(cu::plotCentre(g, 2, 2, cx, cy).ok());
	UnitsCamera cam = LookAt(cx, cy);
	auto draw = [&]() {
		t.clear();
		h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
		h.t += 1000.0 / 30.0;
		cam.frameId++;
	};
	for(int k = 0; k < 3; ++k)
		draw();
	h.port.units = {Rec(0, 1001, 7, 22, 1, cu::kFlagVisible, 10, 100, 5), Rec(1, 2001, 7, 23, 4, cu::kFlagVisible, 40, 100, 4)};
	SrvFeed::Batch b = BatchOf({});
	AddCombat(b, 3, 0, 1001, 1, 2001, (uint16_t)(kFightFlags & ~(SrvFeed::kCombatAttackerAdvances | SrvFeed::kCombatDefenderRetaliates)),
	          4243, 10, 40);
	b.events.push_back(Ev(4, SrvFeed::kUnitSetDamage, 1, 2001, 40, 0));
	b.events.push_back(Ev(5, SrvFeed::kUnitSetDamage, 0, 1001, 10, 0));
	h.u->feed(b);
	for(int frames = 0; frames < 30 * 14 && LogCount("ran to its limit") == 0; ++frames)
		draw();
	std::string line;
	for(const std::string& l : g_log)
		if(l.find("ran to its limit") != std::string::npos)
			line = l;
	printf("fight_limit_line: %s\n", line.c_str());
	EXPECT(!line.empty());
	EXPECT(line.find("figure steps; attacker 0/1001 UNIT_") != std::string::npos);
	EXPECT(line.find("; defender 1/2001 UNIT_") != std::string::npos);
	EXPECT(line.find(" defend>defend f") != std::string::npos);   // the defender's figures never answered
	EXPECT(line.size() < 1100);
	return true;
}

// Ticket 5062 (W98): a city's own ranged strike names CombatParams::city_effect by its owner's era (Eras.CityBombardEffectTag
// read at open, the era from the player record): era 1 EARLY, era 3 MID, era 6 LATE; an era not known leaves it unnamed
// (counted); render_units_trigger_city_shot 0 names nothing and counts nothing (the rule before 5062); a unit attacker is
// never named. Through UnitsHost, AnimFeed and the AnimRunner on the install (as fight_controller).
bool CaseFightCityShot()
{
	struct Out
	{
		bool open = false;
		size_t tags = 0;
		uint64_t named = 0, unnamed = 0;
		std::string last;
		size_t sayLines = 0;
	};
	auto run = [](int era, bool on, bool cityAttacks) {
		Out o;
		g_log.clear();
		UnitsOptions uo;
		uo.triggerCityShot = on;
		Host h(TestEnvCombat(false, true), uo);
		h.port.players = {Player(0, 3, 5, era), Player(1, 3, 5, 0)};
		h.u->poll();
		h.probe->waitPrecheck();
		if(!h.probe->precheckPassed())
		{
			printf("fight_city_shot: era %d: the pre-check did not pass\n", era);
			for(const std::string& l : g_log)
				printf("fight_city_shot: log: %s\n", l.c_str());
			return o;
		}
		h.port.units = {Rec(0, 1001, 7, 22, 1, cu::kFlagVisible, 0, 100, 1), Rec(1, 2001, 7, 23, 4, cu::kFlagVisible, 0, 100, 2)};
		h.u->feed(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 22), Ev(2, SrvFeed::kUnitCreated, 1, 2001, 7, 0, 23)}));
		h.u->setDevice(g_dev);
		Target t;
		const c5r::heightfield::HeightGrid g = Grid();
		float cx = 0, cy = 0;
		cu::plotCentre(g, 2, 2, cx, cy);
		UnitsCamera cam = LookAt(cx, cy);
		auto draw = [&]() {
			t.clear();
			h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
			h.t += 1000.0 / 30.0;
			cam.frameId++;
		};
		for(int k = 0; k < 3; ++k)
			draw();
		const AnimRunner* r = h.probe->animRunner();
		o.open = r != nullptr;
		if(!r)
			return o;
		o.sayLines = LogCount("units: city shot (5062): 8 era tags");
		h.port.units = {Rec(0, 1001, 7, 22, 1, cu::kFlagVisible, 0, 100, 1), Rec(1, 2001, 7, 23, 4, cu::kFlagVisible, 30, 100, 4)};
		SrvFeed::Batch b = BatchOf({});
		if(cityAttacks)
		{
			// the city (owner 0, city id 5) strikes 2001 (kind 28: a[0], a[1] = the city's owner and id)
			SrvFeed::CombatRecord c;
			memset(&c, 0, sizeof(c));
			c.seq = 3;
			c.kind = SrvFeed::kCityCombat;
			c.flags = (uint16_t)(kFightFlags & ~SrvFeed::kCombatAttackerAdvances);
			c.plot = -1;
			c.attackerOwner = 0;
			c.attackerUnit = -1;
			c.attackerCity = 5;
			c.defenderOwner = 1;
			c.defenderUnit = 2001;
			c.defenderCity = -1;
			c.finalDamage[1] = 30;
			c.returned = 4244;
			b.events.push_back(Ev(3, SrvFeed::kCityCombat, 0, 5, (int)b.combats.size(), 0));
			b.combats.push_back(c);
		}
		else
			AddCombat(b, 3, 0, 1001, 1, 2001,
			          (uint16_t)((kFightFlags & ~SrvFeed::kCombatAttackerAdvances) | SrvFeed::kCombatDefenderRetaliates), 4244, 0, 30);
		b.events.push_back(Ev(4, SrvFeed::kUnitSetDamage, 1, 2001, 30, 0));
		h.u->feed(b);
		for(int k = 0; k < 10 && r->cityShotsNamed() + r->cityShotsUnnamed() == 0; ++k)
			draw();
		o.tags = r->cityBombardTags();
		o.named = r->cityShotsNamed();
		o.unnamed = r->cityShotsUnnamed();
		o.last = r->lastCityShot();
		return o;
	};
	const Out e1 = run(1, true, true), e3 = run(3, true, true), e6 = run(6, true, true), eu = run(-1, true, true),
	          off = run(1, false, true), unit = run(1, true, false);
	printf("fight_city_shot: era1 %llu/%llu %s, era3 %llu/%llu %s, era6 %llu/%llu %s, unknown %llu/%llu, off %llu/%llu (say %zu), "
	       "unit attacker %llu/%llu; tags %zu\n",
	       (unsigned long long)e1.named, (unsigned long long)e1.unnamed, e1.last.c_str(), (unsigned long long)e3.named,
	       (unsigned long long)e3.unnamed, e3.last.c_str(), (unsigned long long)e6.named, (unsigned long long)e6.unnamed,
	       e6.last.c_str(), (unsigned long long)eu.named, (unsigned long long)eu.unnamed, (unsigned long long)off.named,
	       (unsigned long long)off.unnamed, off.sayLines, (unsigned long long)unit.named, (unsigned long long)unit.unnamed, e1.tags);
	EXPECT(e1.open && e3.open && e6.open && eu.open && off.open && unit.open);
	EXPECT(e1.tags == 8 && e1.sayLines == 1);
	EXPECT(e1.named == 1 && e1.unnamed == 0 && e1.last == "CITY_BOMBARD_EARLY");
	EXPECT(e3.named == 1 && e3.unnamed == 0 && e3.last == "CITY_BOMBARD_MID");
	EXPECT(e6.named == 1 && e6.unnamed == 0 && e6.last == "CITY_BOMBARD_LATE");
	EXPECT(eu.named == 0 && eu.unnamed == 1 && eu.last.empty());
	EXPECT(off.named == 0 && off.unnamed == 0 && off.last.empty() && off.sayLines == 0);
	EXPECT(unit.named == 0 && unit.unnamed == 0);
	return true;
}

// Ticket 5062 (W107; unit-combat-server.md 18.37.2-18.37.3): a city's strike is FLOWN only from the city's centre, and the
// server takes that centre from kind 28's a[3] (the attacking city's plot + 1, srv_remote_feed.cpp OnCombat on the DLL
// thread). The product's own path, as server.exe runs it: UnitsHost -> AnimFeed (a3) -> FightClock -> AnimRunner (the
// start positions) -> the core's volley -> the trigger stream's effect routes (planHitEffect), counting FLIGHTS and the
// routes' CityVolley hits, not only the named city_effect. Arms: a3 = the city's plot + 1 (placed, every city hit a
// CityVolley, at least one flight); a3 = 0 (a recording before 5062: no centre, one plain line, the hits route None,
// no flight: the code before W107 for every arm); render_units_trigger_city_shot 0 (nothing named, no centre, no flight).
bool CaseFightCityFlight()
{
	struct Out
	{
		bool open = false, routes = false;
		uint64_t named = 0, centres = 0, noCentre = 0, volleys = 0, none = 0, flights = 0, hits = 0;
		uint64_t launches = 0, launchFlights = 0, flown = 0, starts = 0, moves = 0, children = 0;   // 0742
		size_t lines = 0;
	};
	char tmp[MAX_PATH];
	GetTempPathA(MAX_PATH, tmp);
	auto run = [&tmp](int a3, bool on, const char* tag, bool volley = true) {
		Out o;
		g_log.clear();
		UnitsOptions uo;
		uo.triggerCityShot = on;
		uo.triggerCityVolley = volley;   // 0742
		uo.triggerLog = std::string(tmp) + "c5_5062_w107_" + tag + "_" + std::to_string(GetCurrentProcessId()) + ".log";
		uo.install = c5r::gamefs::fromUtf8(g_game);
		{
			Host h(TestEnvCombat(false, true, true), uo);
			h.port.players = {Player(0, 3, 5, 1), Player(1, 3, 5, 0)};
			h.u->poll();
			h.probe->waitPrecheck();
			if(!h.probe->precheckPassed())
			{
				printf("fight_city_flight: %s: the pre-check did not pass\n", tag);
				return o;
			}
			h.port.units = {Rec(1, 2001, 7, 23, 4, cu::kFlagVisible, 0, 100, 2)};
			h.u->feed(BatchOf({Ev(1, SrvFeed::kUnitCreated, 1, 2001, 7, 0, 23)}));
			h.u->setDevice(g_dev);
			Target t;
			const c5r::heightfield::HeightGrid g = Grid();
			float cx = 0, cy = 0;
			cu::plotCentre(g, 2, 2, cx, cy);
			UnitsCamera cam = LookAt(cx, cy);
			auto draw = [&]() {
				t.clear();
				h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
				h.t += 1000.0 / 30.0;
				cam.frameId++;
			};
			for(int k = 0; k < 3; ++k)
				draw();
			const AnimRunner* r = h.probe->animRunner();
			srveffects::TriggerStream* ts = h.probe->triggerStream();
			o.open = r != nullptr;
			o.routes = ts && ts->routes();
			if(!r || !o.routes)
			{
				printf("fight_city_flight: %s: runner %d, routes %d (%s)\n", tag, r ? 1 : 0, o.routes ? 1 : 0,
				       ts ? ts->effectsWhy().c_str() : "no trigger stream");
				return o;
			}
			// the city (owner 0, id 5, on plot 22: x 2, y 2 of the 10-wide batch) strikes 2001 on plot 23, as the DLL's
			// GenerateRangedCombatInfo(CvCity&) makes it: ranged, no retaliation, no member, the record's plot the target's
			h.port.units = {Rec(1, 2001, 7, 23, 4, cu::kFlagVisible, 30, 100, 4)};
			SrvFeed::Batch b = BatchOf({});
			SrvFeed::CombatRecord c;
			memset(&c, 0, sizeof(c));
			c.seq = 3;
			c.kind = SrvFeed::kCityCombat;
			c.flags = (uint16_t)((kFightFlags & ~(SrvFeed::kCombatAttackerAdvances | SrvFeed::kCombatDefenderRetaliates)) |
			                     SrvFeed::kCombatRanged);
			c.plot = 23;
			c.attackerOwner = 0;
			c.attackerUnit = -1;
			c.attackerCity = 5;
			c.defenderOwner = 1;
			c.defenderUnit = 2001;
			c.defenderCity = -1;
			c.finalDamage[1] = 30;
			c.returned = 4245;
			b.events.push_back(Ev(3, SrvFeed::kCityCombat, 0, 5, (int)b.combats.size(), a3, 23));
			b.combats.push_back(c);
			b.events.push_back(Ev(4, SrvFeed::kUnitSetDamage, 1, 2001, 30, 0, 23));
			h.u->feed(b);
			for(int k = 0; k < 30 * 8; ++k)   // 8 s: the volley flies and lands
				draw();
			const srveffects::RouteCounters& rc = ts->routes()->counters();
			o.named = r->cityShotsNamed();
			o.centres = r->cityCentres();
			o.noCentre = r->cityNoCentre();
			o.volleys = rc.hitCityVolley;
			o.none = rc.hitNone;
			o.flights = rc.flightsAdded;
			o.hits = rc.hits;
			o.launches = rc.cityLaunches;
			o.launchFlights = rc.cityLaunchFlights;
			o.flown = rc.hitCityVolleyFlown;
			o.starts = rc.recStart;
			o.moves = rc.recMove;
			o.children = rc.recChild;
			o.lines = LogCount("units: city shot (5062): fight seq 3: city 0/5 (plot -1) has no centre");
		}
		DeleteFileA(uo.triggerLog.c_str());
		return o;
	};
	const Out on = run(22 + 1, true, "on"), noPlot = run(0, true, "noplot"), off = run(22 + 1, false, "off");
	// 0742 (unit-combat-server.md 18.43): render_units_city_volley 0, the rule before 0742: the flight is planned at the
	// landing with its start city_flight in the past, so it starts and lands in one frame (no move)
	const Out pre = run(22 + 1, true, "pre0742", false);
	auto say = [](const char* tag, const Out& o) {
		printf("fight_city_flight: %s: named %llu, centres %llu, no centre %llu (lines %zu); routes: hits %llu, city volleys %llu, "
		       "none %llu, flights %llu; launches %llu flown %llu, landings flown %llu; records start %llu move %llu child %llu\n",
		       tag, (unsigned long long)o.named, (unsigned long long)o.centres,
		       (unsigned long long)o.noCentre, o.lines, (unsigned long long)o.hits, (unsigned long long)o.volleys,
		       (unsigned long long)o.none, (unsigned long long)o.flights, (unsigned long long)o.launches,
		       (unsigned long long)o.launchFlights, (unsigned long long)o.flown, (unsigned long long)o.starts,
		       (unsigned long long)o.moves, (unsigned long long)o.children);
	};
	say("plot", on);
	say("no plot", noPlot);
	say("off", off);
	say("pre 0742", pre);
	// 0742: with the key on every shot leaves at its launch and travels (0.6 s at 30 frames a second: about 18 moves each);
	// the landing adds no second flight. Before 0742: the same flights, each started and landed at once
	EXPECT(on.launches >= 1 && on.launchFlights == on.launches && on.flights == on.launchFlights && on.flown == on.volleys);
	EXPECT(on.starts == on.flights && on.moves >= 10 * on.flights);
	EXPECT(pre.flights >= 1 && pre.flights == pre.volleys && pre.launchFlights == 0 && pre.flown == 0);
	EXPECT(pre.starts == pre.flights && pre.moves <= pre.flights);
	EXPECT(on.open && on.routes && noPlot.open && noPlot.routes && off.open && off.routes);
	EXPECT(on.named == 1 && on.centres == 1 && on.noCentre == 0 && on.lines == 0);
	EXPECT(on.volleys >= 1 && on.none == 0 && on.flights >= 1 && on.flights == on.volleys);
	EXPECT(noPlot.named == 1 && noPlot.centres == 0 && noPlot.noCentre == 1 && noPlot.lines == 1);
	EXPECT(noPlot.volleys == 0 && noPlot.none >= 1 && noPlot.flights == 0);
	EXPECT(off.named == 0 && off.centres == 0 && off.noCentre == 0 && off.volleys == 0 && off.flights == 0);
	return true;
}

// 4783 (W33, C1; MC #8906): a database with the VisKillStrength columns: the controller's open names C1's rule, once, in
// the server's plain log (not LOUD)
bool CaseFightStrikeRule()
{
	Host h(TestEnvCombat(true));
	h.port.players = {Player(0, 3, 5, 0), Player(1, 3, 5, 0)};
	h.u->poll();
	h.probe->waitPrecheck();
	EXPECT(h.probe->precheckPassed());
	h.port.units = {Rec(0, 1001, 7, 22, 1, cu::kFlagVisible, 0, 100, 1), Rec(1, 2001, 7, 23, 4, cu::kFlagVisible, 0, 100, 2)};
	h.u->feed(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 22), Ev(2, SrvFeed::kUnitCreated, 1, 2001, 7, 0, 23)}));
	h.u->setDevice(g_dev);
	Target t;
	const c5r::heightfield::HeightGrid g = Grid();
	float cx = 0, cy = 0;
	EXPECT(cu::plotCentre(g, 2, 2, cx, cy).ok());
	UnitsCamera cam = LookAt(cx, cy);
	for(int k = 0; k < 3; ++k)
	{
		t.clear();
		h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
		h.t += 1000.0 / 30.0;
		cam.frameId++;
	}
	EXPECT(h.probe->lastBuilt().copies[1].units.size() == 2);
	EXPECT(LogCount("units: combat tables: VisKillStrength per landing") == 1 &&
	       LogCount("units: combat tables: one figure per landing") == 0 && LogCount("LOUD: units: combat tables") == 0);
	return true;
}
