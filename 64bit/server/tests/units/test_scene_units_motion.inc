// test_scene_units_motion.inc - ticket 4724 (epic 0042): the fights' figure motion (4776) in the server's unit scene: the
// runner's placement source, start positions, FightMotion per frame, the walk back with the view kept. Written 2026-11-20
// for this repository (GPL-3.0-or-later). Included by test_scene_units.cpp after test_scene_units_fights.inc
// (TestEnvCombat, AddCombat, kFightFlags) inside its anonymous namespace; its cases are the motion_* rows of kCases.
//
// The host's frame as UnitsHost::beginFrame runs it (build, setGrid, the feed's close, the controller, fillFigures on the
// three copies, the drawing renderer's prepare) but on a CPU-only renderer whose plan the test reads: every placed figure.

struct MotionRig
{
	FakePort port;
	UnitsCore core;
	Env env;
	cu::GameFilesProvider* prov = nullptr;
	cu::ArtResolver res;
	AnimRunner runner;
	AnimFeed af;
	cu::UnitRenderer draw;
	c5r::heightfield::HeightGrid g = Grid();
	BuiltFrame bf;
	cu::FramePlan plan;
	ca::TimelineFrame f;
	std::vector<cu::PlayerInput> pl;
	double t = 1000;
	int64_t id = 11;
	bool withGrid = true;
	bool rush = false;     // 5066: TestEnvCombat's mounted (rush) combat row
	bool posExe = false;   // 5066: mode exe (render_units_positions=exe, the product's), set before the first batch
	bool ok = false;
	std::string why;
	MotionRig() : core(port, Log) {}
	~MotionRig() { delete prov; }
	bool open()
	{
		if(posExe)
			core.setPositionsExe(true);
		if(!TestEnvCombat(false, false, false, rush)(L"", env, why))
			return false;
		prov = new cu::GameFilesProvider(env.gf);
		const cu::UnitStatus rs = res.open(*env.db, *prov);
		if(!rs.ok())
		{
			why = "art: " + rs.str();
			return false;
		}
		Lookups l;
		if(!l.load(*env.db, why))
			return false;
		core.setDatabase(l, &res);
		if(!runner.open(env.gf, *env.db, res, why))
			return false;
		af.setBuilds(runner.builds());
		af.setRunner(&runner);
		core.setAnimFeed(&af);
		const c5r::Status ds = draw.open(nullptr, env.gf, *env.db, env.exe, env.install);
		if(!ds.ok())
		{
			why = "draw: " + ds.str();
			return false;
		}
		draw.setPoseSource(runner.pose());
		cu::PlacementSource* ps = runner.fightPlacement(env.gf, env.install);   // H3's call
		if(!ps)
		{
			why = "fightPlacement: " + runner.motionWhy();
			return false;
		}
		draw.setPlacementSource(ps);
		port.players = {Player(0, 3, 5, 0), Player(1, 3, 5, 0)};
		ok = true;
		return true;
	}
	bool frame()
	{
		core.build(nullptr, &g, t, bf, id);
		if(!af.started())
			af.start();
		runner.setGrid(withGrid ? &g : nullptr);   // H4's call
		af.close(core, t, id, f, pl);
		std::set<ca::Controller::UnitKey> drawn;
		for(const cu::UnitScene& sc : bf.copies)
			for(const cu::UnitInput& u : sc.units)
				drawn.insert(std::make_pair(u.player, u.id));
		std::string w;
		if(!runner.apply(f, pl, core, &drawn, w))
		{
			why = "apply: " + w;
			return false;
		}
		for(cu::UnitScene& sc : bf.copies)
			runner.fillFigures(sc);
		plan.clear();
		const c5r::Status s = draw.prepare(bf.copies[1], &g, plan);
		t += 1000.0 / 30.0;
		id++;
		if(!s.ok())
		{
			why = "prepare: " + s.str();
			return false;
		}
		return true;
	}
	// the placed figures of a unit in this frame's plan: figure -> (x, y)
	std::map<uint16_t, std::pair<float, float>> figs(int player, int unit) const
	{
		std::map<uint16_t, std::pair<float, float>> out;
		for(const cu::UnitPlan& u : plan.units)
			if(u.player == player && u.id == unit && u.status.ok())
				for(const cu::FigurePlacement& p : u.placement.placed)
					out[p.figure] = std::make_pair(p.x, p.y);
		return out;
	}
};

float MaxShift(const std::map<uint16_t, std::pair<float, float>>& a, const std::map<uint16_t, std::pair<float, float>>& b)
{
	float m = 0;
	for(const auto& kv : a)
	{
		auto it = b.find(kv.first);
		if(it == b.end())
			continue;
		const float dx = kv.second.first - it->second.first, dy = kv.second.second - it->second.second;
		m = (std::max)(m, std::sqrt(dx * dx + dy * dy));
	}
	return m;
}

float MinDistTo(const std::map<uint16_t, std::pair<float, float>>& a, float x, float y)
{
	float m = 1e30f;
	for(const auto& kv : a)
	{
		const float dx = kv.second.first - x, dy = kv.second.second - y;
		m = (std::min)(m, std::sqrt(dx * dx + dy * dy));
	}
	return m;
}

// One melee fight (fight_controller's record: two warriors, 1001 on plot 22 attacks 2001 on plot 23, 40 damage on the
// defender, 10 on the attacker that retaliates). withGrid: the frame's grid handed over (H4); without it the fight runs
// with no positions and every figure stays where the module places it.
bool MotionMelee(bool withGrid)
{
	g_log.clear();
	MotionRig r;
	r.withGrid = withGrid;
	if(!r.open())
	{
		printf("motion: open: %s\n", r.why.c_str());
		return false;
	}
	r.port.units = {Rec(0, 1001, 7, 22, 1, cu::kFlagVisible, 0, 100, 1), Rec(1, 2001, 7, 23, 4, cu::kFlagVisible, 0, 100, 2)};
	r.core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 22), Ev(2, SrvFeed::kUnitCreated, 1, 2001, 7, 0, 23)}), r.t);
	for(int k = 0; k < 3; ++k)
		EXPECT(r.frame());
	const auto att0 = r.figs(0, 1001), def0 = r.figs(1, 2001);
	EXPECT(att0.size() >= 2 && def0.size() >= 2);
	float dcx = 0, dcy = 0;
	for(const auto& kv : def0)
	{
		dcx += kv.second.first / def0.size();
		dcy += kv.second.second / def0.size();
	}
	const float approach0 = MinDistTo(att0, dcx, dcy);
	r.port.units = {Rec(0, 1001, 7, 22, 1, cu::kFlagVisible, 10, 100, 5), Rec(1, 2001, 7, 23, 4, cu::kFlagVisible, 40, 100, 4)};
	SrvFeed::Batch b = BatchOf({});
	AddCombat(b, 3, 0, 1001, 1, 2001, (uint16_t)((kFightFlags & ~SrvFeed::kCombatAttackerAdvances) | SrvFeed::kCombatDefenderRetaliates),
	          4242, 10, 40);
	b.events.push_back(Ev(4, SrvFeed::kUnitSetDamage, 1, 2001, 40, 0));
	b.events.push_back(Ev(5, SrvFeed::kUnitSetDamage, 0, 1001, 10, 0));
	r.core.onBatch(b, r.t);
	// the fight: until its end (the motion of 1001 ended), at most 14 s
	float maxShift = 0, minApproach = approach0;
	int frames = 0, fightFrames = 0;
	bool ended = false, fightOver = false;
	for(; frames < 30 * 14 && !fightOver; ++frames)
	{
		EXPECT(r.frame());
		double mt = 0;
		bool me = false;
		if(r.runner.motionOf(0, 1001, mt, me) && me)
			ended = true;   // the final motion: the walk back
		if(r.af.hold().named(0, 1001))
			fightFrames++;
		else if(fightFrames)
		{
			fightOver = true;
			break;
		}
		const auto a = r.figs(0, 1001);
		maxShift = (std::max)(maxShift, MaxShift(a, att0));
		minApproach = (std::min)(minApproach, MinDistTo(a, dcx, dcy));
	}
	const MotionCounters& mc = r.runner.motionCounters();
	printf("motion(%s): fight %d frames, attacker figures moved up to %.1f units, nearest to the defender's centre %.1f -> %.1f; "
	       "positioned %llu, no positions %llu, builds %llu\n",
	       withGrid ? "grid" : "no grid", fightFrames, maxShift, approach0, minApproach, (unsigned long long)mc.positioned,
	       (unsigned long long)mc.noPositions, (unsigned long long)mc.builds);
	if(!withGrid)
	{
		// no positions: the core's fixed approach, no motion: every figure where the module places it
		EXPECT(fightOver && !ended && fightFrames > 30 && mc.positioned == 0 && mc.noPositions == 1 && mc.builds == 0 && maxShift == 0.0f);
		EXPECT(LogCount("issue 4724") == 0);
		return true;
	}
	EXPECT(ended && mc.positioned == 1 && mc.noPositions == 0 && mc.positionFails == 0 && mc.buildFails == 0 && mc.builds >= 2);
	EXPECT(maxShift > 10.0f && minApproach < approach0 - 10.0f);   // the attackers ran at the defenders
	// the end: both units walk back; their views are kept while they do
	EXPECT(mc.returns == 2 && r.af.keptForReturn(0, 1001) && !r.af.hold().named(0, 1001));
	auto prev = r.figs(0, 1001);
	int back = 0;
	double mt = 0;
	bool me = false;
	for(; back < 30 * 11 && r.runner.motionOf(0, 1001, mt, me); ++back)
		EXPECT(r.frame());
	const auto att1 = r.figs(0, 1001);
	printf("motion(grid): walked back in %d frames, settled %llu, limit %llu, not drawn %llu; shift from the start %.2f\n", back,
	       (unsigned long long)mc.settled, (unsigned long long)mc.returnLimit, (unsigned long long)mc.notDrawn, MaxShift(att1, att0));
	EXPECT(back > 0 && back < 30 * 11 && mc.settled >= 1 && mc.returnLimit == 0);
	EXPECT(!r.af.keptForReturn(0, 1001));
	EXPECT(r.frame());   // one more frame: the views go
	EXPECT(r.af.view().views() == 0 && MaxShift(r.figs(0, 1001), att1) < 1e-3f);
	// back in the module's own slots: the same frame placed again with no motion gives the same positions
	for(const auto& kv : att1)
		EXPECT(att0.count(kv.first) && MaxShift({kv}, att0) < 1e-3f);
	EXPECT(LogCount("issue 4724") == 0 && r.core.counters().byStatus[(int)Status::kAnimFailed] == 0);
	return true;
}

bool CaseMotionMelee() { return MotionMelee(true); }
bool CaseMotionNoGrid() { return MotionMelee(false); }

// Issue 4726, found live (c5h-b, unit-combat-server.md 12.7): the DLL's final damage of a killed unit is not clamped (the
// damage dealt plus the damage before: 152 for a frigate a destroyer sank). The adapter clamps it to the unit's maximum, so
// the fight plays (before: BadInput "final damage 152 outside 0..100", the fight refused, nothing shown).
bool CaseFightOverkill()
{
	g_log.clear();
	MotionRig r;
	if(!r.open())
	{
		printf("fight_overkill: open: %s\n", r.why.c_str());
		return false;
	}
	r.port.units = {Rec(0, 1001, 7, 22, 1, cu::kFlagVisible, 0, 100, 1), Rec(1, 2001, 7, 23, 4, cu::kFlagVisible, 60, 100, 2)};
	r.core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 22), Ev(2, SrvFeed::kUnitCreated, 1, 2001, 7, 0, 23)}), r.t);
	for(int k = 0; k < 3; ++k)
		EXPECT(r.frame());
	r.port.units = {Rec(0, 1001, 7, 22, 1, cu::kFlagVisible, 10, 100, 5), Rec(1, 2001, 7, 23, 4, cu::kFlagVisible, 100, 100, 4)};
	SrvFeed::Batch b = BatchOf({});
	AddCombat(b, 3, 0, 1001, 1, 2001,
	          (uint16_t)((kFightFlags & ~SrvFeed::kCombatAttackerAdvances) | SrvFeed::kCombatDefenderRetaliates), 4244, 10, 152);
	b.events.push_back(Ev(4, SrvFeed::kUnitSetDamage, 1, 2001, 100, 60));
	b.events.push_back(Ev(5, SrvFeed::kUnitSetDamage, 0, 1001, 10, 0));
	r.core.onBatch(b, r.t);
	int fightFrames = 0;
	bool fightOver = false;
	for(int frames = 0; frames < 30 * 14 && !fightOver; ++frames)
	{
		EXPECT(r.frame());
		if(r.af.hold().named(0, 1001))
			fightFrames++;
		else if(fightFrames)
			fightOver = true;
	}
	const MotionCounters& mc = r.runner.motionCounters();
	printf("fight_overkill: fight %d frames, positioned %llu, refusals in the log %d\n", fightFrames,
	       (unsigned long long)mc.positioned, (int)LogCount("refused by the choreography"));
	EXPECT(fightOver && fightFrames > 30 && fightFrames < 30 * 12);
	EXPECT(mc.positioned == 1 && LogCount("refused by the choreography") == 0);
	return true;
}

// Issue 4729 (found live, c5h-b r1's bomber; unit-combat-server.md 12.9): a unit the DLL hides during its fight (a
// Visibility 0: r1's bomber back over its city) is not listed by the build after the fight, but its view is kept for the
// walk back (4724) and HoldView::applyTo relists it after the build. The drawn set given to the controller now holds the
// walking-back units too, so the relisted unit has its programs (before: UnitRefused "no pose program ... figure 0" in
// every frame of its walk back).
bool CaseMotionHiddenReturn()
{
	g_log.clear();
	MotionRig r;
	if(!r.open())
	{
		printf("motion_hidden_return: open: %s\n", r.why.c_str());
		return false;
	}
	r.port.units = {Rec(0, 1001, 7, 22, 1, cu::kFlagVisible, 0, 100, 1), Rec(1, 2001, 7, 23, 4, cu::kFlagVisible, 0, 100, 2)};
	r.core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 22), Ev(2, SrvFeed::kUnitCreated, 1, 2001, 7, 0, 23)}), r.t);
	for(int k = 0; k < 3; ++k)
		EXPECT(r.frame());
	r.port.units = {Rec(0, 1001, 7, 22, 1, cu::kFlagVisible, 10, 100, 5), Rec(1, 2001, 7, 23, 4, cu::kFlagVisible, 40, 100, 4)};
	SrvFeed::Batch b = BatchOf({});
	AddCombat(b, 3, 0, 1001, 1, 2001, (uint16_t)((kFightFlags & ~SrvFeed::kCombatAttackerAdvances) | SrvFeed::kCombatDefenderRetaliates),
	          4245, 10, 40);
	b.events.push_back(Ev(4, SrvFeed::kUnitSetDamage, 1, 2001, 40, 0));
	b.events.push_back(Ev(5, SrvFeed::kUnitSetDamage, 0, 1001, 10, 0));
	r.core.onBatch(b, r.t);
	// counts the frames that placed unit (0, 1001) and those whose plan refused it for want of a pose program
	int placed = 0, noProgram = 0, backFrames = 0, fightFrames = 0;
	auto look = [&]() {
		for(const cu::UnitPlan& u : r.plan.units)
			if(u.player == 0 && u.id == 1001)
			{
				if(u.status.ok())
					placed++;
				else if(u.status.text.find("no pose program") != std::string::npos)
					noProgram++;
			}
	};
	bool hidden = false, fightOver = false;
	for(int frames = 0; frames < 30 * 14 && !fightOver; ++frames)
	{
		EXPECT(r.frame());
		if(r.af.hold().named(0, 1001))
		{
			if(++fightFrames == 10)
			{
				// the DLL hides the attacker during the fight (blend 0: at once); the table's record follows
				r.port.units[0].flags = 0;
				r.core.onBatch(BatchOf({Ev(9, SrvFeed::kUnitVisibility, 0, 1001, 0, 0)}), r.t);
				hidden = true;
			}
		}
		else if(fightFrames)
			fightOver = true;
	}
	EXPECT(fightOver && hidden && r.af.keptForReturn(0, 1001));
	look();   // the fight's end frame: the walk back has begun
	double mt = 0;
	bool me = false;
	for(; backFrames < 30 * 11 && r.af.keptForReturn(0, 1001) && r.runner.motionOf(0, 1001, mt, me); ++backFrames)
	{
		EXPECT(r.frame());
		look();
	}
	printf("motion_hidden_return: fight %d frames, walk back %d frames: placed %d, refused for no pose program %d; "
	       "UnitRefused in the log %d\n",
	       fightFrames, backFrames, placed, noProgram, (int)LogCount("no pose program"));
	EXPECT(backFrames > 0 && placed > 0 && noProgram == 0);
	return true;
}

// Issue 4726: the adapter's special inputs on the controller (install-backed, warriors only: the test database has no
// aircraft; the aircraft's height is render/tests/unitanim/test_fight_motion_special.cpp's). 1001 on plot 22 attacks 2001 on
// plot 23; the record carries the per-combat damage (kCombatDamageKnown: the attacker takes 10, the defender 40), a unit
// damage member 2002 (plot 24, 30 damage) and the interceptor's flagged member 2003 (plot 21, deals 10). The interceptor
// and the member take part with their figures (othersWith 2), are held in the fight with the two sides, carry the fight's
// motion, and the member's SetDamage waits for the fight's end.
bool CaseMotionOthers()
{
	g_log.clear();
	MotionRig r;
	if(!r.open())
	{
		printf("motion_others: open: %s\n", r.why.c_str());
		return false;
	}
	r.port.units = {Rec(0, 1001, 7, 22, 1, cu::kFlagVisible, 0, 100, 1), Rec(1, 2001, 7, 23, 4, cu::kFlagVisible, 0, 100, 2),
	                Rec(1, 2002, 7, 24, 4, cu::kFlagVisible, 0, 100, 3), Rec(1, 2003, 7, 21, 1, cu::kFlagVisible, 0, 100, 4)};
	r.core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 22), Ev(2, SrvFeed::kUnitCreated, 1, 2001, 7, 0, 23),
	                        Ev(3, SrvFeed::kUnitCreated, 1, 2002, 7, 0, 24), Ev(4, SrvFeed::kUnitCreated, 1, 2003, 7, 0, 21)}),
	               r.t);
	for(int k = 0; k < 3; ++k)
		EXPECT(r.frame());
	r.port.units = {Rec(0, 1001, 7, 22, 1, cu::kFlagVisible, 10, 100, 6), Rec(1, 2001, 7, 23, 4, cu::kFlagVisible, 40, 100, 6),
	                Rec(1, 2002, 7, 24, 4, cu::kFlagVisible, 30, 100, 7), Rec(1, 2003, 7, 21, 1, cu::kFlagVisible, 0, 100, 4)};
	SrvFeed::Batch b = BatchOf({});
	AddCombat(b, 5, 0, 1001, 1, 2001,
	          (uint16_t)((kFightFlags & ~SrvFeed::kCombatAttackerAdvances) | SrvFeed::kCombatDefenderRetaliates |
	                     SrvFeed::kCombatDamageKnown | SrvFeed::kCombatInterceptorMember),
	          4243, 10, 40);
	b.combats.back().reserved = SrvFeed::CombatDamageWords(10, 40);
	b.combats.back().firstMember = (uint32_t)b.combatMembers.size();
	SrvFeed::CombatMember m;
	memset(&m, 0, sizeof(m));
	m.player = 1;
	m.id = 2002;
	m.damage = 30;
	m.finalDamage = 30;
	m.maxHP = 100;
	b.combatMembers.push_back(m);
	m.id = 2003;
	m.flags = SrvFeed::kCombatMemberInterceptor;
	m.damage = 10;
	m.finalDamage = 0;
	b.combatMembers.push_back(m);
	b.combats.back().nMembers = 2;
	b.events.push_back(Ev(6, SrvFeed::kUnitSetDamage, 1, 2001, 40, 0));
	b.events.push_back(Ev(7, SrvFeed::kUnitSetDamage, 0, 1001, 10, 0));
	b.events.push_back(Ev(8, SrvFeed::kUnitSetDamage, 1, 2002, 30, 0));
	r.core.onBatch(b, r.t);
	int fightFrames = 0;
	bool fightOver = false, memberHeld = false, interceptorHeld = false, memberMotion = false, interceptorMotion = false;
	for(int frames = 0; frames < 30 * 14 && !fightOver; ++frames)
	{
		EXPECT(r.frame());
		if(r.af.hold().named(0, 1001))
		{
			fightFrames++;
			memberHeld = memberHeld || r.af.hold().held(1, 2002);
			interceptorHeld = interceptorHeld || r.af.hold().held(1, 2003);
			double mt = 0;
			bool me = false;
			memberMotion = memberMotion || r.runner.motionOf(1, 2002, mt, me);
			interceptorMotion = interceptorMotion || r.runner.motionOf(1, 2003, mt, me);
		}
		else if(fightFrames)
			fightOver = true;
	}
	const MotionCounters& mc = r.runner.motionCounters();
	printf("motion_others: fight %d frames; others with figures %llu, without %llu, unplaced %llu; positioned %llu, builds %llu, "
	       "build fails %llu; member held %d, interceptor held %d, member motion %d, interceptor motion %d\n",
	       fightFrames, (unsigned long long)mc.othersWith, (unsigned long long)mc.othersWithout,
	       (unsigned long long)mc.othersUnplaced, (unsigned long long)mc.positioned, (unsigned long long)mc.builds,
	       (unsigned long long)mc.buildFails, memberHeld, interceptorHeld, memberMotion, interceptorMotion);
	EXPECT(fightOver && fightFrames > 30 && fightFrames < 30 * 12);
	EXPECT(mc.othersWith == 2 && mc.othersWithout == 0 && mc.othersUnplaced == 0);
	EXPECT(mc.positioned == 1 && mc.buildFails == 0 && mc.builds >= 2);
	EXPECT(memberHeld && interceptorHeld && memberMotion && interceptorMotion);
	EXPECT(!r.af.hold().named(1, 2002) && !r.af.hold().named(1, 2003));
	EXPECT(LogCount("issue 4724") == 0 && r.core.counters().byStatus[(int)Status::kAnimFailed] == 0);
	return true;
}
