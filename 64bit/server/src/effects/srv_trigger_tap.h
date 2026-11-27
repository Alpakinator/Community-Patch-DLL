// srv_trigger_tap.h - the trigger emitter on the server's unit scene (epic 0042, ticket 4722): what the hand-off in
// srv_units_anim.cpp calls, so that the hunk there stays two lines (unit-combat-server.md 11.5).
// Hand-written for this repository. SPDX-License-Identifier: GPL-3.0-or-later
//
//   - ControllerFightPlayer::start(f)   -> tap.fightStarted(f)        (the fight's units are the emitter's from now on)
//   - AnimRunner::apply, after the controller's frame ->
//       tap.frame(feed.lastFrameId(), f.dt, controller, feed.lastFightEnds(), feed.lastClipEnds(), why)
//     which writes frame F's records (the crossed triggers of every fighting figure, the fights' and clip instances'
//     OwnerEnds) before the scene marks F's slot READY (R1).
//   - 4793 (additive): with setRoutes, a fight's effects go through srv_effect_routes.h in front of the emitter, and
//     ControllerFightPlayer::advance hands the core's hits over: tap.fightHits(f.seq, run.result().hits). Null routes:
//     the tap as 4722 built it.
//   - 4728 (additive): ControllerFightPlayer::start, before combatFigures -> tap.fightArt(f, ctl, art, tables) (the
//     fight's units' art for the bones and the projectile speeds, applyEffectSpeeds); ControllerFightPlayer::advance ->
//     tap.fightRun(f.seq, run.result(), run.motion, f.elapsed) (fightHits, then the core's targets and the figures'
//     poses for the flight planner and the bones). The hooks are the trigger stream's (srv_trigger_stream.cpp); unset: no
//     call.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "c5r/unitanim/combat.h"
#include "c5r/unitanim/controller.h"
#include "c5r/unitanim/motion.h"
#include "srv_effect_routes.h"
#include "srv_trigger_emitter.h"
#include "units/srv_units_anim.h"

namespace srveffects {

class AnimTriggerTap
{
public:
	explicit AnimTriggerTap(TriggerEmitter& em) : em_(em) {}
	void fightStarted(const srvunits::Fight& f);
	// 4783: a started fight's event id came late (FightClock::setOwner -> ControllerFightPlayer::ownerKnown).
	void fightOwner(const srvunits::Fight& f) { em_.fightOwner(f.seq, f.owner); }
	// 4793: the effect routes (not owned; null = none, the tap as 4722 built it).
	void setRoutes(EffectRoutes* r) { routes_ = r; }
	EffectRoutes* routes() const { return routes_; }
	// 4793: the fight's hits as the core holds them (CombatResult::hits, growing); the new ones go to the routes.
	void fightHits(uint32_t seq, const std::vector<c5r::unitanim::CombatHit>& hits);
	// 0742: the same for the core's city shots at their launch (CombatResult::launches, growing) -> routes.launches.
	void fightLaunches(uint32_t seq, const std::vector<c5r::unitanim::CombatHit>& launches);
	// One frame: false with why when the emitter refused the frame (a negative id, a bad dt; 4728: an id not above the last is a redraw, accepted); nothing written.
	// clipEnds: the frame's ClipEnded (AnimFeed::lastClipEnds), applied before the fights' ends.
	bool frame(int64_t frameId, double dt, const c5r::unitanim::Controller& c, const std::vector<srvunits::FightEndRecord>& ends,
	           const std::vector<srvunits::OwnerEnd>& clipEnds, std::string& why);
	TriggerEmitter& emitter() { return em_; }
	// Called with the controller at the start of each frame (TriggerStream: the members behind $(WEAPON)).
	std::function<void(const c5r::unitanim::Controller&)> beforeFrame;

	// 4728: a fight's start, before the controller's combatFigures copies the members' weapons (the stream resolves the
	// units' art and applies the projectile effects' speeds to `tables`).
	void fightArt(const srvunits::Fight& f, c5r::unitanim::Controller& c, c5r::unitanim::ArtSource& art,
	              c5r::unitanim::CombatArtTables& tables)
	{
		if(onFightArt)
			onFightArt(f, c, art, tables);
	}
	// 4728: a fight's step (after the core's advance and the motion's rebuild): its hits (fightHits) and its state.
	void fightRun(uint32_t seq, const c5r::unitanim::CombatResult& r, const std::shared_ptr<const c5r::unitanim::FightMotion>& motion,
	              double elapsed)
	{
		fightLaunches(seq, r.launches);   // 0742: a city volley's shots at their launch, before the landings
		fightHits(seq, r.hits);
		if(onFightRun)
			onFightRun(seq, r, motion, elapsed);
	}
	std::function<void(const srvunits::Fight&, c5r::unitanim::Controller&, c5r::unitanim::ArtSource&, c5r::unitanim::CombatArtTables&)>
		onFightArt;
	std::function<void(uint32_t, const c5r::unitanim::CombatResult&, const std::shared_ptr<const c5r::unitanim::FightMotion>&, double)>
		onFightRun;
	// 4728: after the frame's records, with the frame's ends (clip instances' and fights'), in the order applied.
	std::function<void(const std::vector<srvunits::OwnerEnd>&)> afterFrame;
	// Ticket 0796: a fight played at a rate (quick_combat.h) has its core's times in its own, scaled time. After this
	// call its new hits and city shots go to the routes with each time T mapped to the real clock as wall - (elapsed - T)
	// / rate (Fight::wall, Fight::elapsed, Fight::rate of this step: exact for a time inside the step, which a hit taken
	// in the frame it lands is), so they are neither scheduled late nor dropped as late. Unit projectiles (the routes'
	// flights from a Transfer) keep their planned paths in real time (0796 measured none launched in the 3x phase; a
	// follow-up for 0798/0799). Called before fightRun; never called: the times as they are, the rule before 0796.
	void fightClock(uint32_t seq, double wall, double elapsed, double rate)
	{
		auto it = hits_.find(seq);
		if(it != hits_.end())
		{
			it->second.scaled = true;
			it->second.wall = wall;
			it->second.elapsed = elapsed;
			it->second.rate = rate > 0 ? rate : 1.0;
		}
	}

private:
	TriggerEmitter& em_;
	EffectRoutes* routes_ = nullptr;                          // 4793
	// 0742: launchesTaken; 0796: the fight's clock of its last step (fightClock)
	struct FightHits
	{
		double start = 0;
		size_t taken = 0, launchesTaken = 0;
		bool scaled = false;
		double wall = 0, elapsed = 0, rate = 1;
		double real(double t) const { return scaled ? wall - (elapsed - t) / rate : t; }
	};
	std::map<uint32_t, FightHits> hits_;                      // 4793: per fight seq
	std::vector<TriggerIn> trig_;
	std::vector<srvunits::OwnerEnd> ends_;
};

}  // namespace srveffects
