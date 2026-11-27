// test_scene_units_walkopen.inc - ticket 5066 (epic 0042): where a fight's walk home goes when the attacker's walk to its
// attack plot is still open (mode exe), counted through the product's own input path. Written 2026-11-24 for this
// repository (GPL-3.0-or-later). Included by test_scene_units.cpp after test_scene_units_motion.inc (MotionRig, AddCombat,
// AddMove, kFightFlags, Pos, Mark) inside its anonymous namespace; its cases are the walkopen_* rows of kCases.
//
// Every case feeds SrvFeed batches as the bridge sends them (kind 12 with its path record, the combat record with its
// event, the damage, kind 6 positions and the arrival after a frame mark) into UnitsCore in mode exe, and runs the host's
// frame (MotionRig: the feed's close, the controller, the 4775 core, FightPlacement on the module's placement). Measured
// per case: the attacker's drawn figure centroid at its fight's start, at its fight's end (the walk home inside the fight,
// 4742, is over: the home), after everything settled, and the largest centroid step between two drawn frames after the
// fight's end (a snap), each against the plot centres of the plot it left and the plot it stands on after the fight.

struct WalkOpenNumbers
{
	std::string name;
	float startFrom = -1, startTo = -1;   // fight start centroid to the plot left / the plot after the fight
	float homeFrom = -1, homeTo = -1;     // fight end centroid (the home) to the same two plots
	float finalTo = -1;                   // settled centroid to the plot after the fight
	float snap = 0;                       // largest centroid step between two frames after the fight's end
	int fightFrames = 0;
	bool started = false, ended = false;
};

std::pair<float, float> Centroid(const std::map<uint16_t, std::pair<float, float>>& a)
{
	float x = 0, y = 0;
	for(const auto& kv : a)
	{
		x += kv.second.first;
		y += kv.second.second;
	}
	if(!a.empty())
	{
		x /= (float)a.size();
		y /= (float)a.size();
	}
	return std::make_pair(x, y);
}

float Dist(std::pair<float, float> a, float x, float y)
{
	const float dx = a.first - x, dy = a.second - y;
	return std::sqrt(dx * dx + dy * dy);
}

// The scripted run. plotFrom / plotTo: the attacker's plot before its move and after the fight (for the numbers).
// script(frame): the batch the DLL sends before that frame (empty: none). walkFrom: the first frame of the EXE's walk
// positions (-1: none), walkFrames: frames from the start plot's centre to the end plot's centre, then the arrival.
// fightSeq: the seq of the measured fight.
struct WalkOpenRun
{
	MotionRig r;
	uint32_t seq = 3;
	int walkFrom = -1, walkFrames = 45;
	int walkA = -1, walkB = -1;   // plots of the walk (start, end)
	uint32_t fightSeq = 0xFFFFFFFFu;
	bool armed = false;   // the next fight motion of 1001 that starts is the measured fight's (the script sets it)
	std::function<void(int)> script;
	std::function<void(int)> post;   // 5066 (a): after each drawn frame (the defender's figures)
	bool sendPos(int k, float ax, float ay, float bx, float by)
	{
		// one EXE frame's position of 1001 along the straight line, the arrival at k == walkFrames
		const bool arrival = k >= walkFrames;
		const float a = arrival ? 1.0f : (float)k / (float)walkFrames;
		SrvFeed::Batch b = BatchOf({Mark(seq, (int)r.id, 1), Pos(seq + 1, 0, 1001, ax + (bx - ax) * a, ay + (by - ay) * a, 0, arrival)});
		seq += 2;
		r.core.onBatch(b, r.t);
		return arrival;
	}
	bool run(WalkOpenNumbers& out, int plotFrom, int plotTo)
	{
		float fx = 0, fy = 0, tx = 0, ty = 0, wax = 0, way = 0, wbx = 0, wby = 0;
		EXPECT(cu::plotCentre(r.g, plotFrom % 10, plotFrom / 10, fx, fy).ok() && cu::plotCentre(r.g, plotTo % 10, plotTo / 10, tx, ty).ok());
		if(walkFrom >= 0)
			EXPECT(cu::plotCentre(r.g, walkA % 10, walkA / 10, wax, way).ok() && cu::plotCentre(r.g, walkB % 10, walkB / 10, wbx, wby).ok());
		bool walkDone = walkFrom < 0, inFight = false, settledSeen = false;
		std::pair<float, float> prev(0.0f, 0.0f);   // 0783: float literals (MSVC /WX C4244 broke the ASan units build)
		bool havePrev = false;
		int after = -1;
		for(int frame = 0; frame < 30 * 30; ++frame)
		{
			if(script)
				script(frame);
			if(!walkDone && frame >= walkFrom)
				walkDone = sendPos(frame - walkFrom, wax, way, wbx, wby);
			EXPECT(r.frame());
			if(post)
				post(frame);
			const auto a = r.figs(0, 1001);
			const std::pair<float, float> c = Centroid(a);
			bool endedHere = false;
			for(const FightEndRecord& e : r.af.lastFightEnds())
				endedHere = endedHere || e.end.seq == fightSeq;
			double mt = 0;
			bool me = false;
			const bool hasMotion = r.runner.motionOf(0, 1001, mt, me);
			if(armed && !out.started && !endedHere && hasMotion && !me && r.af.hold().named(0, 1001))
			{
				out.started = inFight = true;
				out.startFrom = Dist(c, fx, fy);
				out.startTo = Dist(c, tx, ty);
			}
			if(inFight)
				out.fightFrames++;
			if(endedHere)
			{
				out.ended = true;
				inFight = false;
				out.homeFrom = Dist(c, fx, fy);
				out.homeTo = Dist(c, tx, ty);
				after = 0;
				prev = c;
				havePrev = !a.empty();
				continue;
			}
			if(after >= 0)
			{
				if(getenv("WALKOPEN_TRACE") && after < 80)
					printf("walkopen trace %s frame %d: centroid %.1f from the plot left, %.1f from the plot after; held %d kept %d "
					       "motion %d, figures %zu\n",
					       out.name.c_str(), frame, Dist(c, fx, fy), Dist(c, tx, ty), (int)r.af.hold().named(0, 1001),
					       (int)r.af.keptForReturn(0, 1001), (int)hasMotion, a.size());
				if(havePrev && !a.empty())
					out.snap = (std::max)(out.snap, Dist(c, prev.first, prev.second));
				if(!a.empty())
				{
					prev = c;
					havePrev = true;
				}
				after++;
				const bool quiet = !hasMotion && !r.af.hold().named(0, 1001) && !r.af.keptForReturn(0, 1001) && walkDone;
				if(quiet && !settledSeen)
				{
					settledSeen = true;
					after = 1000;   // 30 frames more after the quiet frame
				}
				if(after >= 1030)
				{
					out.finalTo = Dist(c, tx, ty);
					break;
				}
			}
		}
		printf("walkopen %-26s start: %6.1f from the plot left, %6.1f from the plot after; home: %6.1f / %6.1f; settled %6.1f from "
		       "the plot after; snap %6.1f units in one frame; fight %d frames\n",
		       out.name.c_str(), out.startFrom, out.startTo, out.homeFrom, out.homeTo, out.finalTo, out.snap, out.fightFrames);
		EXPECT(out.started && out.ended && out.finalTo >= 0);
		const MotionCounters& mc = r.runner.motionCounters();
		EXPECT(mc.positionFails == 0 && mc.buildFails == 0);
		return true;
	}
};

const uint16_t kWalkOpenFlags = (uint16_t)((kFightFlags & ~SrvFeed::kCombatAttackerAdvances) | SrvFeed::kCombatDefenderRetaliates);

// (0, 1001) on plot 21, (1, 2001) on plot 24 (and (1, 3001) on plot 35 when `third`), created, three frames drawn.
bool WalkOpenOpen(WalkOpenRun& w, bool rush, bool third = false)
{
	g_log.clear();
	w.r.rush = rush;
	w.r.posExe = true;
	if(!w.r.open())
	{
		printf("walkopen: open: %s\n", w.r.why.c_str());
		return false;
	}
	if(third)
		w.r.port.players = {Player(0, 3, 5, 0), Player(1, 3, 5, 0)};
	w.r.port.units = {Rec(0, 1001, 7, 21, 1, cu::kFlagVisible, 0, 100, 1), Rec(1, 2001, 7, 24, 4, cu::kFlagVisible, 0, 100, 2)};
	std::vector<SrvFeed::Event> ev = {Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 21), Ev(2, SrvFeed::kUnitCreated, 1, 2001, 7, 0, 24)};
	if(third)
	{
		w.r.port.units.push_back(Rec(1, 3001, 7, 35, 4, cu::kFlagVisible, 0, 100, 3));
		ev.push_back(Ev(3, SrvFeed::kUnitCreated, 1, 3001, 7, 0, 35));
	}
	w.r.core.onBatch(BatchOf(ev), w.r.t);
	w.seq = third ? 4 : 3;
	for(int k = 0; k < 3; ++k)
		EXPECT(w.r.frame());
	return true;
}

// C1: a move of two plots (21 -> 22 -> 23) and its attack on 2001 (on 24) in one drain, as CvUnit::UnitAttackWithMove
// publishes the queued visual moves right before CvUnitCombat::Attack; the EXE walks the unit to 23 in 1.5 s and arrives.
// knob: AnimFeed::setFightAfterWalk (5066 (a): the combat waits behind the open walk; on, the fight starts and homes on the
// plot after the move, no snap; off, the before numbers of 18.42). dies: the defender is killed in the same drain (its
// damage, death and removal behind the combat): it must stay drawn from the drain to its fight's end.
bool WalkOpenMoveAttack(bool rush, int combatDelay, const char* name, bool knob = true, bool dies = false)
{
	WalkOpenRun w;
	if(!WalkOpenOpen(w, rush))
		return false;
	w.r.af.setFightAfterWalk(knob);
	w.walkA = 21;
	w.walkB = 23;
	w.walkFrom = 1;
	int defenderMissing = 0, waitFrames = 0;
	bool fightOver = false;
	w.post = [&](int frame) {
		if(!w.armed || fightOver)
			return;
		for(const FightEndRecord& e : w.r.af.lastFightEnds())
			fightOver = fightOver || e.end.seq == w.fightSeq;
		if(fightOver)
			return;
		if(w.r.figs(1, 2001).empty())
			defenderMissing++;
		if(!w.r.af.hold().named(0, 1001))
			waitFrames++;
		(void)frame;
	};
	w.script = [&](int frame) {
		if(frame == 0)
		{
			w.r.port.units[0] = Rec(0, 1001, 7, 23, 1, cu::kFlagVisible, 0, 100, w.seq);
			SrvFeed::Batch b = BatchOf({});
			AddMove(b, w.seq++, 0, 1001, {22, 23});
			if(combatDelay == 0 && dies)
			{
				w.fightSeq = w.seq;
				w.armed = true;
				w.r.port.units = {Rec(0, 1001, 7, 23, 1, cu::kFlagVisible, 10, 100, w.seq + 4)};   // 2001 is gone
				AddCombat(b, w.seq, 0, 1001, 1, 2001, kWalkOpenFlags, 4246, 10, 100);
				b.events.push_back(Ev(w.seq + 1, SrvFeed::kUnitSetDamage, 1, 2001, 100, 0));
				b.events.push_back(Ev(w.seq + 2, SrvFeed::kUnitDestroyedInCombat, 1, 2001));
				b.events.push_back(Ev(w.seq + 3, SrvFeed::kUnitDestroyed, 1, 2001));
				b.events.push_back(Ev(w.seq + 4, SrvFeed::kUnitSetDamage, 0, 1001, 10, 0));
				w.seq += 5;
			}
			else if(combatDelay == 0)
			{
				w.fightSeq = w.seq;
				w.armed = true;
				w.r.port.units[0] = Rec(0, 1001, 7, 23, 1, cu::kFlagVisible, 10, 100, w.seq + 2);
				w.r.port.units[1] = Rec(1, 2001, 7, 24, 4, cu::kFlagVisible, 40, 100, w.seq + 1);
				AddCombat(b, w.seq, 0, 1001, 1, 2001, kWalkOpenFlags, 4242, 10, 40);
				b.events.push_back(Ev(w.seq + 1, SrvFeed::kUnitSetDamage, 1, 2001, 40, 0));
				b.events.push_back(Ev(w.seq + 2, SrvFeed::kUnitSetDamage, 0, 1001, 10, 0));
				w.seq += 3;
			}
			w.r.core.onBatch(b, w.r.t);
		}
		else if(combatDelay > 0 && frame == combatDelay)
		{
			w.fightSeq = w.seq;
			w.armed = true;
			w.r.port.units[0] = Rec(0, 1001, 7, 23, 1, cu::kFlagVisible, 10, 100, w.seq + 2);
			w.r.port.units[1] = Rec(1, 2001, 7, 24, 4, cu::kFlagVisible, 40, 100, w.seq + 1);
			SrvFeed::Batch b = BatchOf({});
			AddCombat(b, w.seq, 0, 1001, 1, 2001, kWalkOpenFlags, 4242, 10, 40);
			b.events.push_back(Ev(w.seq + 1, SrvFeed::kUnitSetDamage, 1, 2001, 40, 0));
			b.events.push_back(Ev(w.seq + 2, SrvFeed::kUnitSetDamage, 0, 1001, 10, 0));
			w.seq += 3;
			w.r.core.onBatch(b, w.r.t);
		}
	};
	WalkOpenNumbers n;
	n.name = name;
	if(!w.run(n, 21, 23))
		return false;
	printf("walkopen %-26s defender undrawn %d frames before its fight's end; attacker not yet in its fight %d frames after the "
	       "combat came; fight_walk %llu/%llu/%llu\n",
	       name, defenderMissing, waitFrames, (unsigned long long)w.r.af.counters().fightWalkHeld,
	       (unsigned long long)w.r.af.counters().fightWalkBehind, (unsigned long long)w.r.af.counters().fightWalkDropped);
	const bool openAtCombat = combatDelay < 45;   // the walk (45 frames from frame 1) is still open when the combat comes
	if(knob || !openAtCombat)
	{
		// the after (5066 (a)) and the control: the fight starts and homes on the plot after the move, nothing snaps (a kill
		// has no walk home: the victory figures stand forward, 18.18, so its home is not measured)
		EXPECT(n.startTo < 16.0f && (dies || n.homeTo < 16.0f) && n.finalTo < 8.0f && n.snap < 16.0f);
		EXPECT(defenderMissing == 0);
		EXPECT(w.r.af.counters().fightWalkHeld == (knob && openAtCombat ? 1u : 0u));
	}
	else
	{
		EXPECT(n.startFrom < 16.0f && n.snap > 96.0f);   // the before (18.42): the fight on the plot left, two plots snapped
		EXPECT(w.r.af.counters().fightWalkHeld == 0);
	}
	return true;
}

bool CaseWalkOpenMoveAttackRush() { return WalkOpenMoveAttack(true, 0, "move_attack_rush"); }
bool CaseWalkOpenMoveAttackFoot() { return WalkOpenMoveAttack(false, 0, "move_attack_foot"); }
bool CaseWalkOpenMoveAttackLater() { return WalkOpenMoveAttack(true, 2, "move_attack_rush_2frames"); }
bool CaseWalkOpenArrivedFirst() { return WalkOpenMoveAttack(true, 60, "arrived_first_rush"); }
bool CaseWalkOpenMoveAttackRushOff() { return WalkOpenMoveAttack(true, 0, "move_attack_rush_off", false); }
bool CaseWalkOpenMoveAttackFootOff() { return WalkOpenMoveAttack(false, 0, "move_attack_foot_off", false); }
bool CaseWalkOpenMoveAttackLaterOff() { return WalkOpenMoveAttack(true, 2, "move_attack_rush_2frames_off", false); }
bool CaseWalkOpenDefenderDies() { return WalkOpenMoveAttack(true, 0, "move_attack_kill_rush", true, true); }
bool CaseWalkOpenDefenderDiesOff() { return WalkOpenMoveAttack(true, 0, "move_attack_kill_rush_off", false, true); }

// C3 (kill-advance): 1001 already on 23 (walked there and arrived) kills 2001 on 24 and advances onto it in one drain; the
// EXE walks the advance only after the fight's OwnerEnd (its move records wait behind the held RunCombatSim).
// knob: AnimFeed::setFightAfterWalk (5066 (b): the advance given back at the end frees the kept walk-back view). On: the
// winner walks from where it stands onto 24 (no step above a frame's walk); off: the rule before 5066 (the glide back to 23
// under the kept view, then a 64-unit snap onto 24: the before numbers of 18.42).
bool WalkOpenKillAdvance(bool knob)
{
	WalkOpenRun w;
	if(!WalkOpenOpen(w, true))
		return false;
	w.r.af.setFightAfterWalk(knob);
	// 5059 (unit-combat-server.md 18.36.3): the off arm pins the before of 5066 (b), measured with route S's victory rule of
	// then, the machine's 1800 alone, under which 1001 takes victory after the kill. With 5059's default (victory only with
	// the enable word Victory, as stock; none in VP) the winner takes no victory, and the off arm snaps 3.2 units instead of
	// the before's 64 (W135's relink, 2026-11-25). The off arm keeps the rule of the before: the word off.
	if(!knob)
		w.r.runner.setVictoryNeedsWord(false);
	// first the walk 21 -> 23 with its arrival, no fight
	w.walkA = 21;
	w.walkB = 23;
	w.walkFrom = 1;
	w.walkFrames = 30;
	bool advanceWalk = false;
	int endFrame = -1;
	w.script = [&](int frame) {
		if(frame == 0)
		{
			w.r.port.units[0] = Rec(0, 1001, 7, 23, 1, cu::kFlagVisible, 0, 100, w.seq);
			SrvFeed::Batch b = BatchOf({});
			AddMove(b, w.seq++, 0, 1001, {22, 23});
			w.r.core.onBatch(b, w.r.t);
		}
		else if(frame == 60)
		{
			w.fightSeq = w.seq;
			w.armed = true;
			w.r.port.units = {Rec(0, 1001, 7, 24, 1, cu::kFlagVisible, 20, 100, w.seq + 5)};
			SrvFeed::Batch b = BatchOf({});
			AddCombat(b, w.seq, 0, 1001, 1, 2001, (uint16_t)(kFightFlags | SrvFeed::kCombatDefenderRetaliates), 4243, 20, 100);
			b.events.push_back(Ev(w.seq + 1, SrvFeed::kUnitSetDamage, 1, 2001, 100, 0));
			b.events.push_back(Ev(w.seq + 2, SrvFeed::kUnitDestroyedInCombat, 1, 2001));
			b.events.push_back(Ev(w.seq + 3, SrvFeed::kUnitDestroyed, 1, 2001));
			b.events.push_back(Ev(w.seq + 4, SrvFeed::kUnitSetDamage, 0, 1001, 20, 0));
			AddMove(b, w.seq + 5, 0, 1001, {24});
			w.seq += 6;
			w.r.core.onBatch(b, w.r.t);
		}
		else if(frame > 60 && !advanceWalk)
		{
			for(const FightEndRecord& e : w.r.af.lastFightEnds())
				if(e.end.seq == w.fightSeq)
					endFrame = frame;
			if(endFrame >= 0)
			{
				advanceWalk = true;   // the EXE walks 23 -> 24 now
				w.walkA = 23;
				w.walkB = 24;
				w.walkFrom = frame;
				w.walkFrames = 20;
			}
		}
	};
	// the run's walk state: restart it for the advance (the first walk is over by then)
	WalkOpenNumbers n;
	n.name = knob ? "kill_advance_rush" : "kill_advance_rush_off";
	// run() keeps one walk; the advance's walk is fed here through the script's frame hook instead
	int k = 0;
	auto first = w.script;
	w.script = [&](int frame) {
		first(frame);
		if(advanceWalk && frame >= w.walkFrom && k <= w.walkFrames)
		{
			float ax = 0, ay = 0, bx = 0, by = 0;
			cu::plotCentre(w.r.g, 3, 2, ax, ay);
			cu::plotCentre(w.r.g, 4, 2, bx, by);
			const int kk = k++;
			const int saved = w.walkFrames;
			w.walkFrames = 20;
			w.sendPos(kk, ax, ay, bx, by);
			w.walkFrames = saved;
		}
	};
	if(!w.run(n, 23, 24))
		return false;
	if(knob)
		EXPECT(n.snap < 8.0f && n.finalTo < 4.0f && w.r.af.counters().returnsFreed == 1);
	else
		EXPECT(n.snap > 48.0f && w.r.af.counters().returnsFreed == 0);   // the before: one plot in one frame
	return true;
}
bool CaseWalkOpenKillAdvance() { return WalkOpenKillAdvance(true); }
bool CaseWalkOpenKillAdvanceOff() { return WalkOpenKillAdvance(false); }

// C2 (B behind A): 1001 on 23 (arrived) attacks 2001 (fight A); while A plays, the DLL sends its move 23 -> 34 and its
// attack on 3001 (on 35) in one drain (fight B, queued behind A). The EXE walks 23 -> 34 only after A's OwnerEnd.
// knob on (5066 (a)): B waits behind the walk A's held move opens at A's end; it starts and homes on 34. Off: the before.
bool WalkOpenBehindA(bool knob)
{
	WalkOpenRun w;
	if(!WalkOpenOpen(w, true, true))
		return false;
	w.r.af.setFightAfterWalk(knob);
	w.walkA = 21;
	w.walkB = 23;
	w.walkFrom = 1;
	w.walkFrames = 30;
	uint32_t aSeq = 0;
	bool second = false;
	int k = 0;
	w.script = [&](int frame) {
		if(frame == 0)
		{
			w.r.port.units[0] = Rec(0, 1001, 7, 23, 1, cu::kFlagVisible, 0, 100, w.seq);
			SrvFeed::Batch b = BatchOf({});
			AddMove(b, w.seq++, 0, 1001, {22, 23});
			w.r.core.onBatch(b, w.r.t);
		}
		else if(frame == 60)
		{
			aSeq = w.seq;
			w.r.port.units[0] = Rec(0, 1001, 7, 23, 1, cu::kFlagVisible, 10, 100, w.seq + 2);
			w.r.port.units[1] = Rec(1, 2001, 7, 24, 4, cu::kFlagVisible, 30, 100, w.seq + 1);
			SrvFeed::Batch b = BatchOf({});
			AddCombat(b, w.seq, 0, 1001, 1, 2001, kWalkOpenFlags, 4244, 10, 30);
			b.events.push_back(Ev(w.seq + 1, SrvFeed::kUnitSetDamage, 1, 2001, 30, 0));
			b.events.push_back(Ev(w.seq + 2, SrvFeed::kUnitSetDamage, 0, 1001, 10, 0));
			w.seq += 3;
			w.r.core.onBatch(b, w.r.t);
		}
		else if(frame == 66)
		{
			w.fightSeq = w.seq + 1;
			w.r.port.units[0] = Rec(0, 1001, 7, 34, 1, cu::kFlagVisible, 20, 100, w.seq + 3);
			w.r.port.units[2] = Rec(1, 3001, 7, 35, 4, cu::kFlagVisible, 30, 100, w.seq + 2);
			SrvFeed::Batch b = BatchOf({});
			AddMove(b, w.seq, 0, 1001, {34});
			AddCombat(b, w.seq + 1, 0, 1001, 1, 3001, kWalkOpenFlags, 4245, 20, 30);
			b.events.push_back(Ev(w.seq + 2, SrvFeed::kUnitSetDamage, 1, 3001, 30, 0));
			b.events.push_back(Ev(w.seq + 3, SrvFeed::kUnitSetDamage, 0, 1001, 20, 0));
			w.seq += 4;
			w.r.core.onBatch(b, w.r.t);
		}
		else if(frame > 66 && !second)
		{
			for(const FightEndRecord& e : w.r.af.lastFightEnds())
				second = second || e.end.seq == aSeq;   // the EXE walks 23 -> 34 from A's end
			w.armed = second;                            // B's motion is the next one that starts
		}
		if(second && k <= 30)
		{
			float ax = 0, ay = 0, bx = 0, by = 0;
			cu::plotCentre(w.r.g, 3, 2, ax, ay);
			cu::plotCentre(w.r.g, 4, 3, bx, by);
			w.sendPos(k++, ax, ay, bx, by);
		}
	};
	WalkOpenNumbers n;
	n.name = knob ? "b_behind_a_rush" : "b_behind_a_rush_off";
	if(!w.run(n, 23, 34))
		return false;
	if(knob)
		EXPECT(n.startTo < 16.0f && n.homeTo < 16.0f && n.finalTo < 8.0f && n.snap < 16.0f &&
		       w.r.af.counters().fightWalkHeld == 1);
	else
		EXPECT(n.startFrom < 16.0f && n.snap > 96.0f && w.r.af.counters().fightWalkHeld == 0);   // the before (18.42)
	return true;
}
bool CaseWalkOpenBehindA() { return WalkOpenBehindA(true); }
bool CaseWalkOpenBehindAOff() { return WalkOpenBehindA(false); }

// Feed level (MergeRig: mode exe, the fake fight player) ticket 5066 (a)'s order and bounds. Each arm: 1001 (on 23) moves
// 23 -> 33 -> 34 and attacks 2001 (on 24) in one drain; then (a) the defender moves before the arrival: its move waits
// behind the combat and comes after the fight; (b) a teleport, (c) the attacker's removal, (d) F9's rule (no position for
// kWalkStartQuietFrames frames) each end the walk and give the combat back; (e) a load drops the waiting combat.
std::string WalkOpenStart(MergeRig& r, uint32_t& cs)
{
	r.port.units[0] = Rec(0, 1001, 7, 34, 0, cu::kFlagVisible, 0, 100, r.seq);
	r.port.units[1] = Rec(1, 2001, 7, 24, 4, cu::kFlagVisible, 30, 100, r.seq + 2);
	SrvFeed::Batch b = BatchOf({});
	AddMove(b, r.seq, 0, 1001, {33, 34});
	cs = r.seq + 1;
	r.fp.length[cs] = 2;
	AddCombat(b, cs, 0, 1001, 1, 2001, kFightFlags & ~SrvFeed::kCombatAttackerAdvances, 950 + cs, 0, 30);
	b.events.push_back(Ev(r.seq + 2, SrvFeed::kUnitSetDamage, 1, 2001, 30, 0));
	r.seq += 3;
	r.step(&b);
	return Kinds(r.f);
}

// Steps until the fight cs has ended (at most 30 closes); every close's kinds, ';'-separated.
std::string WalkOpenToEnd(MergeRig& r, uint32_t cs)
{
	std::string seen;
	for(int k = 0; k < 30; ++k)
	{
		r.step();
		seen += Kinds(r.f) + ";";
		bool ended = false;
		for(const FightEndRecord& e : r.af.lastFightEnds())
			ended = ended || e.end.seq == cs;
		if(ended)
			break;
	}
	return seen;
}

bool CaseWalkOpenFeed()
{
	uint32_t cs = 0;
	// (a) the defender moves while the combat waits: behind it in feed order
	{
		g_log.clear();
		MergeRig r;
		EXPECT(WalkOpenStart(r, cs) == "move:1001" && r.af.afterWalkWaiting() == 2 && r.af.view().frozen(1, 2001));
		r.pos(100, 100);
		r.port.units[1] = Rec(1, 2001, 7, 25, 4, cu::kFlagVisible, 30, 100, r.seq);
		SrvFeed::Batch m = BatchOf({});
		AddMove(m, r.seq++, 1, 2001, {25});
		r.step(&m);
		EXPECT(Kinds(r.f).empty() && r.af.afterWalkWaiting() == 3 && r.af.counters().fightWalkBehind == 2);
		const cu::UnitInput* d = UnitOf(r.bf, 2001);
		EXPECT(d && d->plot_x == 4 && d->plot_y == 2);   // drawn on 24 from its view
		r.pos(110, 117, true);
		EXPECT(Kinds(r.f).find("arrive:1001") != std::string::npos && r.af.afterWalkWaiting() == 0);
		const std::string seen = WalkOpenToEnd(r, cs);
		// the fight first (its strike), then at its end the damage and the defender's move, in feed order
		EXPECT(seen.find("action:1001") != std::string::npos && seen.find("damage:2001,move:2001") != std::string::npos &&
		       seen.find("action:1001") < seen.find("move:2001"));
		EXPECT(r.fp.started.size() == 1 && r.fp.started[0] == cs && !r.af.view().frozen(1, 2001));
	}
	// (b) a teleport ends the walk: the combat comes back after the place
	{
		g_log.clear();
		MergeRig r;
		EXPECT(WalkOpenStart(r, cs) == "move:1001");
		r.port.units[0] = Rec(0, 1001, 7, 35, 0, cu::kFlagVisible, 0, 100, r.seq);
		SrvFeed::Batch tp = BatchOf({Ev(r.seq++, SrvFeed::kUnitTeleported, 0, 1001, 35)});
		r.step(&tp);
		EXPECT(Kinds(r.f) == "place:1001" && r.af.afterWalkWaiting() == 0);
		const std::string seen = WalkOpenToEnd(r, cs);
		EXPECT(r.fp.started.size() == 1 && seen.find("damage:2001") != std::string::npos);
	}
	// (c) the attacker's removal ends the walk: the combat comes back after it (bounded; the fight runs without its unit)
	{
		g_log.clear();
		MergeRig r;
		EXPECT(WalkOpenStart(r, cs) == "move:1001");
		r.port.units.erase(r.port.units.begin());
		SrvFeed::Batch rm = BatchOf({Ev(r.seq++, SrvFeed::kUnitDestroyed, 0, 1001)});
		r.step(&rm);
		EXPECT(Kinds(r.f) == "remove:1001" && r.af.afterWalkWaiting() == 0);
		WalkOpenToEnd(r, cs);
		EXPECT(r.af.afterWalkWaiting() == 0 && !r.af.hold().named(1, 2001));
	}
	// (d) F9's rule: no position record for kWalkStartQuietFrames frames ends the walk by an arrive
	{
		g_log.clear();
		MergeRig r;
		EXPECT(WalkOpenStart(r, cs) == "move:1001");
		std::string seen;
		for(int k = 0; k < kWalkStartQuietFrames + 10 && r.fp.started.empty(); ++k)
		{
			r.step();
			seen += Kinds(r.f) + ";";
		}
		EXPECT(seen.find("arrive:1001") != std::string::npos && r.fp.started.size() == 1);
	}
	// (e) a load drops the waiting combat and the ops behind it (the fights' hold drops its own the same way)
	{
		g_log.clear();
		MergeRig r;
		EXPECT(WalkOpenStart(r, cs) == "move:1001" && r.af.afterWalkWaiting() == 2);
		SrvFeed::Batch ld = BatchOf({Ev(r.seq++, SrvFeed::kLoadBegin, 1, 0)});
		r.step(&ld);
		EXPECT(r.af.afterWalkWaiting() == 0 && r.af.counters().fightWalkDropped == 2 && r.fp.started.empty());
		r.step();
		EXPECT(r.fp.started.empty());
	}
	// off: the rule before 5066 (the combat is added at once, beside the move)
	{
		g_log.clear();
		MergeRig r;
		r.af.setFightAfterWalk(false);
		EXPECT(WalkOpenStart(r, cs) == "move:1001" && r.af.afterWalkWaiting() == 0 && r.af.hold().named(0, 1001));
	}
	return true;
}

// Ticket 5066 (a), the two-walks edge (W120's code read, W125): one drain in which A (0, 1001) walks and attacks D (1, 2001)
// (c1, behind A's walk), B (1, 2002) walks and attacks E (0, 1002) (c2, behind B's walk), then D attacks E (c3: D waits behind
// A, E behind B). A arrives first while B still walks: c3 must not begin before c2 (stock: E's records keep their order in
// the EXE's queue). Before the fix a unit's wait named one walk only (the last), so A's give-back let c3 pass, and it began
// at c1's end, ahead of c2.
bool CaseWalkOpenTwoWalks()
{
	g_log.clear();
	MergeRig r;
	r.port.units.push_back(Rec(0, 1002, 7, 26, 1, cu::kFlagVisible, 0, 100, r.seq));
	r.port.units.push_back(Rec(1, 2002, 7, 37, 4, cu::kFlagVisible, 0, 100, r.seq + 1));
	SrvFeed::Batch cr = BatchOf({Ev(r.seq, SrvFeed::kUnitCreated, 0, 1002, 7, 0, 26), Ev(r.seq + 1, SrvFeed::kUnitCreated, 1, 2002, 7, 0, 37)});
	r.seq += 2;
	r.step(&cr);
	const uint16_t fl = kFightFlags & ~SrvFeed::kCombatAttackerAdvances;
	r.port.units[0] = Rec(0, 1001, 7, 34, 0, cu::kFlagVisible, 0, 100, r.seq);
	r.port.units[3] = Rec(1, 2002, 7, 36, 4, cu::kFlagVisible, 0, 100, r.seq + 2);
	SrvFeed::Batch b = BatchOf({});
	AddMove(b, r.seq, 0, 1001, {33, 34});
	const uint32_t c1 = r.seq + 1, c2 = r.seq + 3, c3 = r.seq + 4;
	AddCombat(b, c1, 0, 1001, 1, 2001, fl, 950 + c1, 0, 10);
	AddMove(b, r.seq + 2, 1, 2002, {36});
	AddCombat(b, c2, 1, 2002, 0, 1002, fl, 950 + c2, 0, 10);
	AddCombat(b, c3, 1, 2001, 0, 1002, fl, 950 + c3, 0, 10);
	r.seq += 5;
	r.step(&b);
	EXPECT(r.af.afterWalkWaiting() == 3 && r.fp.started.empty());
	// A arrives at once; B sends a position every frame (its walk stays open) for 20 frames
	auto frame = [&](bool aArrives, bool bArrives) {
		const uint32_t s = r.seq;
		r.seq += 3;
		std::vector<SrvFeed::Event> ev{Mark(s, (int)r.id, aArrives ? 2 : 1)};
		if(aArrives)
			ev.push_back(Pos(s + 1, 0, 1001, 110, 117, 2, true));
		ev.push_back(Pos(s + 2, 1, 2002, 200, 100 + (float)r.id, 2, bArrives));
		SrvFeed::Batch p = BatchOf(ev);
		r.step(&p);
	};
	frame(true, false);
	for(int k = 0; k < 20; ++k)
		frame(false, false);
	const auto at = [&](uint32_t s) {
		return (size_t)(std::find(r.fp.started.begin(), r.fp.started.end(), s) - r.fp.started.begin());
	};
	std::string order;
	for(uint32_t s : r.fp.started)
		order += std::string(order.empty() ? "" : ",") + (s == c1 ? "c1" : s == c2 ? "c2" : s == c3 ? "c3" : "?");
	printf("walkopen_two_walks: begun while B walks: [%s]\n", order.c_str());
	EXPECT(at(c1) < r.fp.started.size() && at(c2) == r.fp.started.size() && at(c3) == r.fp.started.size());
	frame(false, true);
	for(int k = 0; k < 30 && r.fp.started.size() < 3; ++k)
		r.step();
	EXPECT(r.fp.started.size() == 3 && at(c1) < at(c2) && at(c2) < at(c3));
	return true;
}

// Ticket 0742 (the owner's beta 4 game, 2026-11-25 01:21:38; unit-combat-server.md 18.43): A (0, 1001) walks and attacks D
// (1, 2001) (c1, behind A's walk), and in the same drain C (0, 1002) attacks E (1, 2002) (c2, no walk: added at once). When A
// arrives, c1 comes back below the clock's last seq (c2). Before the fix the clock refused it ("not above the last one
// added"): no fight, no OwnerEnd, so the shell's hold waited for its 22 s outer bound and route S disarmed for the game.
// Off (fight after walk 0): nothing waits, both are added in feed order (the rule before 5066).
static bool WalkOpenLaterCombatFirst(bool on)
{
	g_log.clear();
	MergeRig r;
	r.af.setFightAfterWalk(on);
	r.port.units.push_back(Rec(0, 1002, 7, 26, 1, cu::kFlagVisible, 0, 100, r.seq));
	r.port.units.push_back(Rec(1, 2002, 7, 27, 4, cu::kFlagVisible, 0, 100, r.seq + 1));
	SrvFeed::Batch cr = BatchOf({Ev(r.seq, SrvFeed::kUnitCreated, 0, 1002, 7, 0, 26), Ev(r.seq + 1, SrvFeed::kUnitCreated, 1, 2002, 7, 0, 27)});
	r.seq += 2;
	r.step(&cr);
	const uint16_t fl = kFightFlags & ~SrvFeed::kCombatAttackerAdvances;
	r.port.units[0] = Rec(0, 1001, 7, 34, 0, cu::kFlagVisible, 0, 100, r.seq);
	SrvFeed::Batch b = BatchOf({});
	AddMove(b, r.seq, 0, 1001, {33, 34});
	const uint32_t c1 = r.seq + 1, c2 = r.seq + 2;
	AddCombat(b, c1, 0, 1001, 1, 2001, fl, 950 + c1, 0, 10);
	AddCombat(b, c2, 0, 1002, 1, 2002, fl, 950 + c2, 0, 10);
	r.seq += 3;
	r.step(&b);
	if(on)
		EXPECT(r.af.afterWalkWaiting() >= 1 && r.af.counters().fightWalkHeld == 1);
	// A arrives in the next frame
	{
		const uint32_t s = r.seq;
		r.seq += 2;
		SrvFeed::Batch p = BatchOf({Mark(s, (int)r.id, 2), Pos(s + 1, 0, 1001, 110, 117, 2, true)});
		r.step(&p);
	}
	for(int k = 0; k < 60 && r.fp.started.size() < 2; ++k)
		r.step();
	const auto at = [&](uint32_t s) {
		return (size_t)(std::find(r.fp.started.begin(), r.fp.started.end(), s) - r.fp.started.begin());
	};
	printf("walkopen_later_combat_first (%s): started %zu (c1 %s, c2 %s), refused %llu, resumed below %llu\n", on ? "on" : "off",
	       r.fp.started.size(), at(c1) < r.fp.started.size() ? "yes" : "no", at(c2) < r.fp.started.size() ? "yes" : "no",
	       (unsigned long long)r.af.counters().combatsRefused, (unsigned long long)r.af.fights()->counters().resumedBelow);
	EXPECT(r.fp.started.size() == 2 && at(c1) < 2 && at(c2) < 2 && r.af.counters().combatsRefused == 0);
	if(on)
		EXPECT(at(c2) < at(c1) && r.af.fights()->counters().resumedBelow == 1);
	else
		EXPECT(r.af.fights()->counters().resumedBelow == 0);
	return true;
}
bool CaseWalkOpenLaterCombatFirst() { return WalkOpenLaterCombatFirst(true); }
bool CaseWalkOpenLaterCombatFirstOff() { return WalkOpenLaterCombatFirst(false); }
