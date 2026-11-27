// test_scene_units_afterwalk.inc - ticket 5065 (epic 0042, W118): the hold behind an open walk (AnimFeed::holdBehindWalks,
// setOrdersAfterWalk; research/05-graphics-composition/unit-controller.md 20). Included by test_scene_units.cpp after its
// helpers. The feed case drives UnitsCore + AnimFeed with the records as the EXE and the DLL send them (kind 12 and the
// kind 18 of an automated worker in one batch, the position records, the arrival); the host case runs the product's
// whole path (SceneUnits, AnimRunner, the install's worker machine) and reads the clip each figure plays.

// UnitsCore + AnimFeed in mode exe (or own), one unit 1001 created at plot 23; step() feeds a batch and closes one frame.
struct AfterWalkRig
{
	FakePort port;
	UnitsCore core;
	AnimFeed af;
	BuiltFrame bf;
	ca::TimelineFrame f;
	std::vector<cu::PlayerInput> pl;
	int64_t fr = 20;
	double t = 1000;
	AfterWalkRig(bool exe, bool on) : core(port, Log)
	{
		port.units = {Rec(0, 1001, 7, 23, 4, cu::kFlagVisible, 0, 100, 1)};
		port.players = {Player(0, 3, 5, 0)};
		core.setDatabase(TestLookups(), nullptr);
		core.setPositionsExe(exe);
		core.setAnimFeed(&af);
		af.setOrdersAfterWalk(on);
		core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 23)}), t);
		core.build(nullptr, nullptr, t, bf, fr);
		af.start();
		af.close(core, t, fr, f, pl);
	}
	std::string step(const SrvFeed::Batch* b = nullptr)
	{
		t += 33;
		fr++;
		if(b)
			core.onBatch(*b, t);
		core.build(nullptr, nullptr, t, bf, fr);
		af.close(core, t, fr, f, pl);
		return Kinds(f);
	}
};

bool CaseAfterWalkFeed()
{
	// on (the default), mode exe: the work an automated worker's MOVE_TO + BUILD post with the move waits for the arrival
	{
		AfterWalkRig r(true, true);
		EXPECT(Kinds(r.f) == "create:1001" && r.af.ordersAfterWalk() && r.af.holdsAfterWalk(SrvFeed::kUnitWork) &&
		       r.af.holdsAfterWalk(SrvFeed::kUnitFortify) && !r.af.holdsAfterWalk(SrvFeed::kUnitSetDamage));
		r.port.units[0] = Rec(0, 1001, 7, 25, 1, cu::kFlagVisible, 0, 100, 3);
		SrvFeed::Batch mw = MoveBatch(2, 0, 1001, 25, {Ev(3, SrvFeed::kUnitWork, 0, 1001, 0)});
		EXPECT(r.step(&mw) == "move:1001");
		EXPECT(r.af.walkOpen(0, 1001) && r.af.afterWalkWaiting() == 1 && r.af.counters().afterWalkHeld == 1);
		SrvFeed::Batch p1 = BatchOf({Mark(4, 1, 1), Pos(5, 0, 1001, 150, 140, 2)});
		EXPECT(r.step(&p1).empty() && r.af.afterWalkWaiting() == 1);
		SrvFeed::Batch p2 = BatchOf({Mark(6, 2, 1), Pos(7, 0, 1001, 160, 140, 2, true)});   // the arrival
		EXPECT(r.step(&p2) == "arrive:1001" && !r.af.walkOpen(0, 1001) && r.af.afterWalkWaiting() == 0);
		EXPECT(r.step() == "action:1001" && r.f.events[0].action == ca::ActionKind::Work);   // first at the next close
		EXPECT(r.af.counters().afterWalkReleased == 1 && r.af.counters().afterWalkNoWalk == 0);
		// a work in a later batch while the walk plays waits too; a second kind 12 continues the walk (it still waits); a
		// fortify behind it keeps its feed order; a damage is not in the table and passes at once
		r.port.units[0] = Rec(0, 1001, 7, 27, 1, cu::kFlagVisible, 0, 100, 8);
		SrvFeed::Batch m2 = MoveBatch(8, 0, 1001, 26);
		EXPECT(r.step(&m2) == "move:1001");
		SrvFeed::Batch w2 = BatchOf({Ev(9, SrvFeed::kUnitWork, 0, 1001, 0), Ev(10, SrvFeed::kUnitSetDamage, 0, 1001, 10, 0)});
		EXPECT(r.step(&w2) == "damage:1001" && r.af.afterWalkWaiting() == 1);
		SrvFeed::Batch m3 = MoveBatch(11, 0, 1001, 27, {Ev(12, SrvFeed::kUnitFortify, 0, 1001, 1)});
		EXPECT(r.step(&m3) == "move:1001" && r.af.afterWalkWaiting() == 2 && r.af.walkOpen(0, 1001));
		SrvFeed::Batch a3 = BatchOf({Mark(13, 3, 1), Pos(14, 0, 1001, 170, 140, 2, true)});
		EXPECT(r.step(&a3) == "arrive:1001");
		EXPECT(r.step() == "action:1001,fortify:1001");
		// a walk that ends by a teleport (place): the work comes back after the place
		r.port.units[0] = Rec(0, 1001, 7, 28, 1, cu::kFlagVisible, 0, 100, 15);
		SrvFeed::Batch m4 = MoveBatch(15, 0, 1001, 28, {Ev(16, SrvFeed::kUnitWork, 0, 1001, 0)});
		EXPECT(r.step(&m4) == "move:1001");
		SrvFeed::Batch tp = BatchOf({Ev(17, SrvFeed::kUnitTeleported, 0, 1001, 29)});
		EXPECT(r.step(&tp) == "place:1001");
		EXPECT(r.step() == "action:1001");
		// F9's rule ends a walk with no position (an arrive): the work comes back after it
		SrvFeed::Batch m5 = MoveBatch(18, 0, 1001, 29, {Ev(19, SrvFeed::kUnitWork, 0, 1001, 0)});
		EXPECT(r.step(&m5) == "move:1001");
		std::string seen;
		for(int k = 0; k < kWalkStartQuietFrames + 3 && seen.find("action") == std::string::npos; ++k)
			seen += r.step() + ";";
		EXPECT(seen.find("arrive:1001;action:1001;") != std::string::npos);
		// the unit removed while it walks: its waiting work is given back and dropped (the unit is gone)
		SrvFeed::Batch m6 = MoveBatch(20, 0, 1001, 30, {Ev(21, SrvFeed::kUnitWork, 0, 1001, 0)});
		EXPECT(r.step(&m6) == "move:1001");
		const uint64_t dropped = r.af.counters().droppedNotHeld;
		r.port.units.clear();
		SrvFeed::Batch rm = BatchOf({Ev(22, SrvFeed::kUnitDestroyed, 0, 1001)});
		EXPECT(r.step(&rm) == "remove:1001");
		EXPECT(r.step().empty() && r.af.counters().droppedNotHeld == dropped + 1 && r.af.afterWalkWaiting() == 0);
		EXPECT(r.af.counters().afterWalkHeld == 6 && r.af.counters().afterWalkReleased == 6);
	}
	// a kind 12 that opens no walk (the record has no facing): the work does not wait for ever, it comes at the next close
	{
		AfterWalkRig r(true, true);
		r.port.units[0] = Rec(0, 1001, 7, 25, -1, cu::kFlagVisible, 0, 100, 3);
		SrvFeed::Batch mw = MoveBatch(2, 0, 1001, 25, {Ev(3, SrvFeed::kUnitWork, 0, 1001, 0)});
		EXPECT(r.step(&mw).empty() && !r.af.walkOpen(0, 1001));
		EXPECT(r.step() == "action:1001" && r.af.counters().afterWalkNoWalk == 1);
	}
	// off: the rule before 5065 (the work at once, beside the move)
	{
		AfterWalkRig r(true, false);
		// (ticket 5066 (a) keeps its combat kinds in the table under its own knob; with that knob off too the table is empty)
		EXPECT(!r.af.ordersAfterWalk() && !r.af.holdsAfterWalk(SrvFeed::kUnitWork) && !r.af.holdsAfterWalk(SrvFeed::kUnitFortify));
		r.af.setFightAfterWalk(false);
		EXPECT(!r.af.holdsAnyAfterWalk());
		r.af.setFightAfterWalk(true);
		r.port.units[0] = Rec(0, 1001, 7, 25, 1, cu::kFlagVisible, 0, 100, 3);
		SrvFeed::Batch mw = MoveBatch(2, 0, 1001, 25, {Ev(3, SrvFeed::kUnitWork, 0, 1001, 0), Ev(4, SrvFeed::kUnitFortify, 0, 1001, 1)});
		EXPECT(r.step(&mw) == "move:1001,action:1001,fortify:1001" && r.af.afterWalkWaiting() == 0);
		EXPECT(r.af.counters().afterWalkHeld == 0);
	}
	// mode own: no walk is open, nothing waits (0375's mapping: move, arrive, then the work in one frame)
	{
		AfterWalkRig r(false, true);
		r.port.units[0] = Rec(0, 1001, 7, 25, 1, cu::kFlagVisible, 0, 100, 3);
		SrvFeed::Batch mw = MoveBatch(2, 0, 1001, 25, {Ev(3, SrvFeed::kUnitWork, 0, 1001, 0)});
		EXPECT(r.step(&mw) == "move:1001,arrive:1001,action:1001" && !r.af.walkOpen(0, 1001));
		EXPECT(r.af.counters().afterWalkHeld == 0);
	}
	return true;
}

// The worker's art rows on top of TestDb: unit type 10 UNIT_WORKER (ART_DEF_UNIT__WORKER, 8 x WORKER_EURO from the
// install's worker files; the worker machine), the warrior and trireme rows kept.
EnvOpener WorkerEnv()
{
	return [](const std::wstring&, Env& out, std::string& why) {
		const c5r::Status s = out.gf.open(c5r::gamefs::fromUtf8(g_game));
		if(!s.ok())
		{
			why = s.str();
			return false;
		}
		std::unique_ptr<c5m::TsvTables> db = TestDb(false);
		c5m::Error e;
		db->add("Units", Tsv({{"ID", "Type", "UnitArtInfo", "UnitArtInfoCulturalVariation", "UnitArtInfoEraVariation"},
		                      {"7", "UNIT_WARRIOR", "ART_DEF_UNIT__WARRIOR", "0", "0"},
		                      {"8", "UNIT_TRIREME", "ART_DEF_UNIT_TRIREME", "0", "0"},
		                      {"10", "UNIT_WORKER", "ART_DEF_UNIT__WORKER", "0", "0"}}),
		        &e);
		db->add("ArtDefine_UnitInfos", Tsv({{"Type", "Formation", "DamageStates"},
		                                    {"ART_DEF_UNIT__WARRIOR", "Warrior", "1"},
		                                    {"ART_DEF_UNIT_TRIREME", "\\N", "3"},
		                                    {"ART_DEF_UNIT__WORKER", "LooseCivilian", "1"}}),
		        &e);
		db->add("ArtDefine_UnitInfoMemberInfos", Tsv({{"UnitInfoType", "UnitMemberInfoType", "NumMembers"},
		                                              {"ART_DEF_UNIT__WARRIOR", "ART_DEF_UNIT_MEMBER_WARRIOR_V2", "3"},
		                                              {"ART_DEF_UNIT__WARRIOR", "ART_DEF_UNIT_MEMBER_WARRIOR", "6"},
		                                              {"ART_DEF_UNIT__WARRIOR", "ART_DEF_UNIT_MEMBER_WARRIOR_V4", "3"},
		                                              {"ART_DEF_UNIT_TRIREME", "ART_DEF_UNIT_MEMBER_TRIREME", "1"},
		                                              {"ART_DEF_UNIT__WORKER", "ART_DEF_UNIT_MEMBER_WORKER_EURO", "8"}}),
		        &e);
		db->add("ArtDefine_UnitMemberInfos",
		        Tsv({{"Type", "Scale", "ZOffset", "Domain", "Model", "MaterialTypeTag"},
		             {"ART_DEF_UNIT_MEMBER_WARRIOR_V2", "0.14000000059604645", "\\N", "", "Assets/Units/Warrior/Warrior_V2.fxsxml", "CLOTH"},
		             {"ART_DEF_UNIT_MEMBER_WARRIOR", "0.14000000059604645", "\\N", "", "Assets/Units/Warrior/Warrior.fxsxml", "CLOTH"},
		             {"ART_DEF_UNIT_MEMBER_WARRIOR_V4", "0.14000000059604645", "\\N", "", "Assets/Units/Warrior/Warrior_V4.fxsxml", "CLOTH"},
		             {"ART_DEF_UNIT_MEMBER_TRIREME", "0.07000000029802322", "\\N", "Sea", "Assets/Units/Trireme/Trireme.fxsxml", "WOOD"},
		             {"ART_DEF_UNIT_MEMBER_WORKER_EURO", "0.14000000059604645", "\\N", "", "Assets/Units/Worker/Worker_EURO/Worker_EURO.fxsxml", "CLOTH"}}),
		        &e);
		db->add("ArtDefine_UnitMemberCombats",
		        Tsv({{"UnitMemberType", "EnableActions"},
		             {"ART_DEF_UNIT_MEMBER_WARRIOR_V2", "Idle Attack RunCharge AttackCity Death BombardDefend Run Fortify CombatReady"},
		             {"ART_DEF_UNIT_MEMBER_WARRIOR", "Idle Attack RunCharge AttackCity Death BombardDefend Run Fortify CombatReady"},
		             {"ART_DEF_UNIT_MEMBER_WARRIOR_V4", "Idle Attack RunCharge AttackCity Death BombardDefend Run Fortify CombatReady"},
		             {"ART_DEF_UNIT_MEMBER_TRIREME", "Idle Attack AttackCity Death BombardDefend Run Fortify CombatReady"},
		             {"ART_DEF_UNIT_MEMBER_U_ASSYRIAN_SIEGE_TOWER", "Idle AttackCity AttackCharge Death BombardDefend Run Fortify CombatReady"},
		             {"ART_DEF_UNIT_MEMBER_WORKER_EURO", "Idle Death BombardDefend Run"}}),
		        &e);
		out.db = std::move(db);
		out.exe = c5r::gamefs::fromUtf8(g_exe);
		out.install = c5r::gamefs::fromUtf8(g_game);
		return true;
	};
}

// The product's path: a worker given a move and its build (BUILD_ROAD: the shovel, 1910) in one batch, then the EXE's
// positions along one plot and its arrival. Per frame the clips its figures play (lower case, joined).
struct WorkerWalk
{
	std::vector<std::string> walk, after;   // per frame: the clips of the worker's figures
	uint64_t held = 0, animFailed = 0;
	std::string awText;                     // the totals line's after_walk field
};

std::string LowerClips(const cu::UnitInput& u)
{
	std::string s;
	for(const cu::FigureInput& fi : u.figures)
	{
		std::string c = fi.clip;
		for(char& ch : c)
			ch = (char)tolower((unsigned char)ch);
		if(s.find(c) == std::string::npos)
			s += c + " ";
	}
	return s;
}

bool RunWorkerWalk(bool on, WorkerWalk& w)
{
	UnitsOptions o;
	o.positionsExe = true;
	o.ordersAfterWalk = on;
	Host h(WorkerEnv(), o);
	h.u->poll();
	h.probe->waitPrecheck();
	EXPECT(h.probe->precheckPassed());
	h.port.units = {Rec(0, 1001, 10, 2 * 10 + 2, 1, cu::kFlagVisible, 0, 100, 1)};
	h.u->feed(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 10, 0, 22)}));
	h.u->setDevice(g_dev);
	Target t;
	const c5r::heightfield::HeightGrid g = Grid();
	float cx = 0, cy = 0, ex = 0, ey = 0;
	EXPECT(cu::plotCentre(g, 2, 2, cx, cy).ok() && cu::plotCentre(g, 3, 2, ex, ey).ok());
	UnitsCamera cam = LookAt(cx, cy);
	auto frame = [&](const SrvFeed::Batch* b) {
		if(b)
			h.u->feed(*b);
		h.t += 33;
		cam.frameId++;
		t.clear();
		h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
		const BuiltFrame& bf = h.probe->lastBuilt();
		return bf.copies[1].units.size() == 1 ? LowerClips(bf.copies[1].units[0]) : std::string("-");
	};
	for(int k = 0; k < 3; ++k)
		frame(nullptr);
	h.port.units = {Rec(0, 1001, 10, 2 * 10 + 3, 0, cu::kFlagVisible, 0, 100, 2)};   // moved to (3, 2)
	SrvFeed::Batch mw = MoveBatch(2, 0, 1001, 23, {Ev(3, SrvFeed::kUnitWork, 0, 1001, 0)});
	frame(&mw);
	uint32_t seq = 4;
	const int steps = 30;   // about 1 s of walk, as the EXE's one-plot walk
	for(int k = 1; k <= steps; ++k)
	{
		const float a = (float)k / steps;
		SrvFeed::Batch p = BatchOf({Mark(seq, k, 1), Pos(seq + 1, 0, 1001, cx + (ex - cx) * a, cy + (ey - cy) * a, 0, k == steps)});
		seq += 2;
		w.walk.push_back(frame(&p));
	}
	for(int k = 0; k < 75; ++k)   // 2.5 s after the arrival
		w.after.push_back(frame(nullptr));
	char line[8192];
	h.u->totalsLine(line, sizeof(line));
	const char* aw = strstr(line, " after_walk=");   // the hold's counters (only while its table holds a kind)
	w.held = aw ? strtoull(aw + 12, nullptr, 10) : 0;
	w.awText = aw ? std::string(aw + 1, strcspn(aw + 1, " |")) : std::string("(none)");
	w.animFailed = h.probe->core().counters().byStatus[(int)Status::kAnimFailed];
	return true;
}

size_t FramesWith(const std::vector<std::string>& v, const char* needle)
{
	size_t n = 0;
	for(const std::string& s : v)
		n += s.find(needle) != std::string::npos ? 1 : 0;
	return n;
}

bool CaseAfterWalkWorkerHost()
{
	WorkerWalk on, off;
	EXPECT(RunWorkerWalk(true, on));
	EXPECT(RunWorkerWalk(false, off));
	const size_t onRun = FramesWith(on.walk, "_run"), onTool = FramesWith(on.walk, "shovel");
	const size_t offRun = FramesWith(off.walk, "_run"), offTool = FramesWith(off.walk, "shovel");
	printf("afterwalk_worker_host: on: walk frames %zu run %zu shovel %zu, after: shovel %zu, held %llu | off: run %zu shovel %zu, "
	       "after: shovel %zu\n", on.walk.size(), onRun, onTool, FramesWith(on.after, "shovel"), (unsigned long long)on.held,
	       offRun, offTool, FramesWith(off.after, "shovel"));
	printf("afterwalk_worker_host: on walk[5] '%s' after[last] '%s' %s | off walk[5] '%s' after[last] '%s' %s\n",
	       on.walk[5].c_str(), on.after.back().c_str(), on.awText.c_str(), off.walk[5].c_str(), off.after.back().c_str(),
	       off.awText.c_str());
	// on: the worker runs through its walk (the run start lasts 0.1 s) and digs once it has stopped
	EXPECT(on.held == 1 && on.animFailed == 0 && onTool == 0 && onRun + 4 >= on.walk.size());
	EXPECT(on.after.back().find("shovel") != std::string::npos);
	// off (the rule before 5065): the shovel through the walk, no run; idle after the arrive (the work ended)
	EXPECT(off.held == 0 && off.animFailed == 0 && offRun == 0 && offTool + 4 >= off.walk.size());
	EXPECT(off.after.back().find("shovel") == std::string::npos);
	return true;
}
