// test_scene_units_merge.inc - where 4772 (the walk's facing, mode exe) and 4774 (the per-unit hold and the fights) meet
// (epic 0042, ticket 4724 item 0; unit-combat-server.md 6.4). Written 2026-11-20 for this repository (GPL-3.0-or-later).
// Included by test_scene_units.cpp after test_scene_units_fights.inc (FakeFightPlayer, AddCombat, AddMove, kFightFlags)
// inside its anonymous namespace; its cases are the merge_* rows of kCases.

// The host's frame without a controller, in mode exe: build, the feed's close, the walk facings followed, then per copy
// what AnimRunner::fillFigures does (the walk facings, the held units' views over them, the view's memory).
struct MergeRig
{
	FakePort port;
	UnitsCore core;
	AnimFeed af;
	FakeFightPlayer fp;
	WalkFacings wf;
	BuiltFrame bf;
	ca::TimelineFrame f;
	std::vector<cu::PlayerInput> pl;
	double t = 1000;
	int64_t id = 41;
	uint32_t seq = 1;
	MergeRig() : core(port, Log)
	{
		port.players = {Player(0, 3, 5, 0), Player(1, 3, 5, 0)};
		core.setDatabase(TestLookups(), nullptr);
		af.setFightPlayer(&fp);
		core.setAnimFeed(&af);
		core.setPositionsExe(true);
		// (0, 1001) on plot 23 facing east (1); (1, 2001) on plot 24 facing west (4)
		port.units = {Rec(0, 1001, 7, 23, 1, cu::kFlagVisible, 0, 100, 1), Rec(1, 2001, 7, 24, 4, cu::kFlagVisible, 0, 100, 2)};
		core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 23), Ev(2, SrvFeed::kUnitCreated, 1, 2001, 7, 0, 24)}), t);
		af.start();   // after the batch, as the host starts the controller (WalkRig): one create per unit of the table
		seq = 3;
		step();
	}
	void step(const SrvFeed::Batch* b = nullptr)
	{
		if(b)
			core.onBatch(*b, t);
		core.build(nullptr, nullptr, t, bf, id);
		af.close(core, t, id, f, pl);
		wf.follow(f);
		for(cu::UnitScene& sc : bf.copies)
		{
			wf.apply(sc);
			const int c = af.view().nextCopy();
			af.view().applyTo(sc, c);
			af.view().remember(sc, c);
		}
		t += 1000.0 / 30.0;
		id++;
	}
	// one EXE frame's position record of 1001 (a frame mark first, as the bridge sends them)
	void pos(float x, float y, bool arrival = false)
	{
		const uint32_t s = seq;
		seq += 2;
		SrvFeed::Batch b = BatchOf({Mark(s, (int)id, 1), Pos(s + 1, 0, 1001, x, y, 2, arrival)});
		step(&b);
	}
	int drawn(int unit) const
	{
		const cu::UnitInput* u = UnitOf(bf, unit);
		return u ? u->facing : -9;
	}
	size_t turns() const
	{
		size_t n = 0;
		for(const ca::TimelineEvent& e : f.events)
			n += e.kind == ca::EventKind::Turn ? 1 : 0;
		return n;
	}
};

// A fight begins while 1001's walk is open: the unit is drawn from its frozen view (facing 1, not a walk facing), and its
// walk gives no turn while the fight names it; at the fight's end the held damage comes back first, then the one turn to
// the newest heading (measured from the sample before the hold); the arrival ends the walk with the record's facing.
// Ticket 5066: this is the rule before 5066 (the fight starts at once, the attacker frozen where it was last drawn): it is
// kept as the knob's off arm (AnimFeed::setFightAfterWalk(false)); the on arm (the default) is merge_walk_held_fight_on.
bool CaseMergeWalkHeldFight()
{
	g_log.clear();
	MergeRig r;
	r.af.setFightAfterWalk(false);   // ticket 5066 off: the rule before 5066, byte for byte
	EXPECT(Kinds(r.f) == "create:1001,create:2001" && r.drawn(1001) == 1);
	r.port.units[0] = Rec(0, 1001, 7, 34, 0, cu::kFlagVisible, 0, 100, r.seq);   // the walk ends on plot 34, facing 0
	{
		SrvFeed::Batch b = BatchOf({});
		AddMove(b, r.seq++, 0, 1001, {33, 34});
		r.step(&b);
	}
	EXPECT(Kinds(r.f) == "move:1001" && r.wf.walking() == 1 && r.drawn(1001) == 1);
	r.pos(100, 100);   // the first sample
	r.pos(110, 100);   // east: no turn
	EXPECT(Kinds(r.f).empty() && r.af.counters().walkSamples == 1 && r.drawn(1001) == 1);
	// 1001 attacks 2001 (40 damage on it) while its walk is open; the fight plays 3 frames
	r.fp.length[r.seq] = 3;
	r.port.units[1] = Rec(1, 2001, 7, 24, 4, cu::kFlagVisible, 40, 100, r.seq + 1);
	{
		SrvFeed::Batch b = BatchOf({});
		AddCombat(b, r.seq, 0, 1001, 1, 2001, kFightFlags & ~SrvFeed::kCombatAttackerAdvances, 900, 0, 40);
		b.events.push_back(Ev(r.seq + 1, SrvFeed::kUnitSetDamage, 1, 2001, 40, 0));
		r.seq += 2;
		r.step(&b);
	}
	EXPECT(r.af.hold().named(0, 1001) && r.af.hold().queued() == 1 && r.af.view().frozen(0, 1001));
	// north-east steps while the fight names 1001: no sample, no turn; drawn from its view
	float x = 110, y = 100;
	int frames = 0;
	size_t heldTurns = 0;
	bool drawnFrozen = true;
	while(r.af.hold().named(0, 1001) && frames < 20)
	{
		x += 5;
		y += 8.660254f;
		r.pos(x, y);
		frames++;
		if(r.af.hold().named(0, 1001))
		{
			heldTurns += r.turns();
			drawnFrozen = drawnFrozen && r.drawn(1001) == 1;
		}
	}
	EXPECT(frames >= 3 && frames < 20 && heldTurns == 0 && drawnFrozen);
	EXPECT(r.af.walksOpen() == 1);   // the arrive waited in the hold: the walk is still open
	// the end frame (the loop's last): no sample was taken while held (1 before the hold, 1 now): the held damage first, then the turn to north-east (0, left) from the sample before the hold
	EXPECT(Kinds(r.f) == "damage:2001,turn:1001" && r.f.events[1].a == 0 && r.f.events[1].b == 0);
	EXPECT(r.af.lastFightEnds().size() == 1 && r.af.lastFightEnds()[0].released == 1 && r.drawn(1001) == 0);
	EXPECT(r.af.counters().walkSamples == 2 && r.af.counters().turns == 1);
	r.pos(x + 5, y + 8.660254f);   // straight on
	EXPECT(Kinds(r.f).empty() && r.drawn(1001) == 0);
	r.pos(x + 5, y + 8.660254f, true);   // the arrival: arrive with the record's facing 0
	EXPECT(Kinds(r.f) == "arrive:1001" && r.f.events[0].a == 0 && r.wf.walking() == 0 && r.af.walksOpen() == 0);
	return true;
}

// Ticket 5066 (a), the knob on (the default): the same records. The combat of 1001 waits behind its open walk (the EXE's
// queue runs RunCombatSim behind the move records), with the damage on 2001 behind it; 1001 walks on drawn by the EXE's
// positions (not frozen), 2001 keeps the view of the frame before; at the arrival the combat comes back, the fight is added
// at the next close, starts at the one after, and gives the damage back at its end.
bool CaseMergeWalkHeldFightOn()
{
	g_log.clear();
	MergeRig r;
	EXPECT(r.af.fightAfterWalk() && r.af.holdsAfterWalk(SrvFeed::kUnitCombat) && r.af.holdsAfterWalk(SrvFeed::kCityCombat));
	r.port.units[0] = Rec(0, 1001, 7, 34, 0, cu::kFlagVisible, 0, 100, r.seq);
	{
		SrvFeed::Batch b = BatchOf({});
		AddMove(b, r.seq++, 0, 1001, {33, 34});
		r.step(&b);
	}
	r.pos(100, 100);
	r.pos(110, 100);
	const uint32_t cs = r.seq;
	r.fp.length[cs] = 3;
	r.port.units[1] = Rec(1, 2001, 7, 24, 4, cu::kFlagVisible, 40, 100, r.seq + 1);
	{
		SrvFeed::Batch b = BatchOf({});
		AddCombat(b, cs, 0, 1001, 1, 2001, kFightFlags & ~SrvFeed::kCombatAttackerAdvances, 900, 0, 40);
		b.events.push_back(Ev(r.seq + 1, SrvFeed::kUnitSetDamage, 1, 2001, 40, 0));
		r.seq += 2;
		r.step(&b);
	}
	EXPECT(Kinds(r.f).empty() && !r.af.hold().named(0, 1001) && r.af.afterWalkWaiting() == 2);
	EXPECT(r.af.counters().fightWalkHeld == 1 && r.af.counters().fightWalkBehind == 1);
	EXPECT(r.af.view().frozen(1, 2001) && !r.af.view().frozen(0, 1001) && r.af.walksOpen() == 1);
	const cu::UnitInput* d = UnitOf(r.bf, 2001);
	EXPECT(d && d->damage == 0);   // the view of the frame before (the table has 40)
	float x = 110, y = 100;
	for(int k = 0; k < 5; ++k)
	{
		x += 5;
		y += 8.660254f;
		r.pos(x, y);
		EXPECT(r.fp.started.empty() && !r.af.hold().named(0, 1001) && r.af.afterWalkWaiting() == 2);
	}
	EXPECT(r.af.counters().walkSamples >= 3);   // the walk goes on sampled (it was frozen before 5066)
	r.pos(x + 5, y + 8.660254f, true);   // the arrival
	EXPECT(Kinds(r.f).find("arrive:1001") != std::string::npos && r.af.walksOpen() == 0 && r.af.afterWalkWaiting() == 0);
	r.step();   // the combat comes back first: the fight is added, the damage waits in the fights' hold
	EXPECT(r.af.hold().named(0, 1001) && r.af.hold().named(1, 2001) && r.af.hold().queued() == 1 && r.fp.started.empty());
	r.step();   // the fight starts on the plot the walk reached
	EXPECT(Kinds(r.f) == "action:1001" && r.fp.started.size() == 1 && r.fp.started[0] == cs);
	int frames = 0;
	while(r.af.lastFightEnds().empty() && frames < 20)
	{
		r.step();
		frames++;
	}
	EXPECT(frames < 20 && Kinds(r.f) == "damage:2001" && r.af.lastFightEnds()[0].released == 1);
	EXPECT(!r.af.view().frozen(1, 2001) && !r.af.view().frozen(0, 1001));
	return true;
}

// Fight A holds 1001's walk (its move and the EXE's arrival); fight B on 1001 comes while A plays. At A's end the move and
// its arrive come back in one frame (the arrival already happened); the arrive is not deferred into B's hold, so the walk
// is closed before B starts and B gives back only its own ops.
// Ticket 5066 (a): with the knob on (the default) fight B waits behind the walk that A's held move opens at A's end (the
// EXE's queue: A, then 1001's move, then B); here the arrive comes back with the move, so B is added at the next close and
// starts one close later than before. Off: the rule before 5066 (B names 1001 at A's end, starts at the next close).
bool MergeReleasedArrive(bool knob)
{
	g_log.clear();
	MergeRig r;
	r.af.setFightAfterWalk(knob);
	const uint32_t a = r.seq;
	r.fp.length[a] = 4;
	{
		SrvFeed::Batch b = BatchOf({});
		AddCombat(b, a, 0, 1001, 1, 2001, kFightFlags & ~SrvFeed::kCombatAttackerAdvances, 901, 0, 0);
		r.seq++;
		r.step(&b);
	}
	EXPECT(r.af.hold().named(0, 1001));
	// 1001's move and its whole walk in the EXE's records, arrival included, while A names it
	r.port.units[0] = Rec(0, 1001, 7, 34, 0, cu::kFlagVisible, 0, 100, r.seq);
	{
		SrvFeed::Batch b = BatchOf({});
		AddMove(b, r.seq++, 0, 1001, {33, 34});
		b.events.push_back(Mark(r.seq, (int)r.id, 1));
		b.events.push_back(Pos(r.seq + 1, 0, 1001, 100, 100, 2));
		b.events.push_back(Mark(r.seq + 2, (int)r.id + 1, 1));
		b.events.push_back(Pos(r.seq + 3, 0, 1001, 110, 117, 2, true));
		r.seq += 4;
		r.step(&b);
	}
	EXPECT(Kinds(r.f).find("1001") == std::string::npos || Kinds(r.f) == "action:1001");
	EXPECT(r.af.hold().queued(0, 1001) == 2);   // the move and the arrive, owned by A
	// fight B on 1001 while A plays: it waits for A (the per-unit wait)
	const uint32_t bseq = r.seq;
	r.fp.length[bseq] = 2;
	r.port.units[1] = Rec(1, 2001, 7, 24, 4, cu::kFlagVisible, 30, 100, r.seq + 1);
	{
		SrvFeed::Batch b = BatchOf({});
		AddCombat(b, bseq, 0, 1001, 1, 2001, kFightFlags & ~SrvFeed::kCombatAttackerAdvances, 902, 0, 30);
		b.events.push_back(Ev(r.seq + 1, SrvFeed::kUnitSetDamage, 1, 2001, 30, 0));
		r.seq += 2;
		r.step(&b);
	}
	// to A's end
	int frames = 0;
	while(r.af.lastFightEnds().empty() && frames < 20)
	{
		r.step();
		frames++;
	}
	EXPECT(frames < 20 && r.af.lastFightEnds().size() == 1 && r.af.lastFightEnds()[0].end.seq == a);
	EXPECT(r.af.lastFightEnds()[0].released == 2);
	// A's end frame: the move and its arrive together; the walk is closed at once
	EXPECT(Kinds(r.f) == "move:1001,arrive:1001" && r.f.events[1].a == 0);
	EXPECT(r.af.walksOpen() == 0 && r.wf.walking() == 0 && r.af.counters().arrivesDeferred == 0);
	if(knob)
	{
		// B and the damage behind it waited behind the walk; the arrive gave them back: B is added at the next close
		EXPECT(!r.af.hold().named(0, 1001) && r.af.counters().fightWalkHeld == 1 && r.af.counters().fightWalkBehind == 1);
		r.step();
		EXPECT(Kinds(r.f).empty() && r.fp.started.size() == 1);
	}
	EXPECT(r.af.hold().named(0, 1001) && r.af.hold().queued(0, 1001) == 0);   // B names it, nothing of A's walk waits
	r.step();   // B starts
	EXPECT(Kinds(r.f) == "action:1001" && r.fp.started.size() == 2 && r.fp.started[1] == bseq);
	frames = 0;
	while(r.af.lastFightEnds().empty() && frames < 20)
	{
		r.step();
		frames++;
	}
	EXPECT(frames < 20 && Kinds(r.f) == "damage:2001" && r.af.lastFightEnds()[0].released == 1);
	return true;
}
bool CaseMergeReleasedArrive() { return MergeReleasedArrive(false); }
bool CaseMergeReleasedArriveOn() { return MergeReleasedArrive(true); }
