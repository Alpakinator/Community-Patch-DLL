// srv_effect_routes.h - a fight's effect starts into the trigger stream (epic 0042, ticket 4793, route S stage A; D210,
// D211). Hand-written for this repository. SPDX-License-Identifier: GPL-3.0-or-later
//
// Stage A (the Architect's route S note 2.1 and 2.3): every effect start that belongs to a fight leaves the server as
// an EffectStart / EffectMove / EffectStop record of shell/common/vp_trigger.h, written by 4722's emitter
// (srv_trigger_emitter.h), and the EXE's Particle system plays it (the shell's player, 4798's records). This class
// is the routes in front of the emitter (effects-formats.md 6, the routes that belong to a fight):
//   1. Clip Effect triggers (the controller's, through the emitter): the name is resolved through 4786's library
//      (c5r/effects/effect_routes.h EffectNames: $(TERRAIN) / $(BASETERRAIN) / $(WEAPON) with the figure's values; a
//      surface without a variant takes DIRT, counted; a name that resolves to nothing is not sent, counted by name).
//   2. Held projectiles: an Effect trigger that a Transfer row names by refid (the arrow in the hand) is started here
//      through the emitter's API, so that the projectile's flight takes over that handle (the EXE's own hand-over,
//      record 0x2B: one instance from the hand to the target). Not taken within its trigger's duration, it is stopped.
//   3. Combat hits (4777's CombatHit): a weapon without a flight starts its HitEffect as a one-shot where the hit lands;
//      a city's volley (the era's CITY_BOMBARD_* VisEffect, a projectile effect) is flown by 4792's ProjectileSet; a
//      unit's projectile hit is flown through addFlight (the launch is 4792's planLaunch, the caller's).
//   4. The flights (4792's ProjectileSet) step on this class's clock and their records go to the emitter 1:1
//      (W16 #8010, W15's open item): EffectStart -> startEffect (follows), EffectMove -> moveEffect, EffectStop ->
//      stopEffect, ChildStart / HitStart -> one-shot startEffect. A fight's end removes its flights still in the air
//      with their trails and drops their impacts (note 1.5 item 4).
//   Not a fight's (stay the EXE's, note 2.3 item 4): city damage smoke, pillage smoke, landmark states, paradrop. The
//   DLL's GameplayDoFX has no call in a fight (only CvNotifications and CvMinorCivAI call it; effects-formats.md E8).
//
// Order in one server frame (after the emitter's beginFrame and fightStarted, before its triggers and endFrame):
//   routes.beginFrame(dt); routes.fightStarted(f) for each; routes.intake(triggers) then emitter.triggers(triggers);
//   routes.hits(new hits); routes.addFlight(...) for each unit projectile launched; routes.ownerEnds(ends) then
//   emitter.ownerEnds(ends); routes.endFrame(); emitter.endFrame().
//
// One thread (the server's render thread). Nothing throws. No EXE address, no name from the game's files (D83).
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "effects/srv_trigger_emitter.h"
#include "c5r/effects/effect_routes.h"
#include "c5r/effects/projectile.h"

namespace srveffects {

struct RouteParams
{
	c5r::effects::HitPlanParams hit;
	double lateDrop = 0.1;        // note 1.5 item 1 for one-shot hit effects (s)
	double heldGrace = 0.5;       // a held effect whose trigger has no duration waits this long for its flight (s) [I]
	bool heldHandOver = true;     // false: held triggers go to the emitter as any Effect trigger (two instances)
	// Ticket 5064 (rule A; unit-combat-server.md 18.39): the held effect (the arrow in the hand, which its flight takes
	// over) starts as the emitter starts any trigger effect: at the bone's world transform (position and orientation,
	// FigurePositions::boneTransform) and at the figure's member scale (figureScale), as the EXE's 0x28 start carries
	// them and its flight keeps them (effects-server.md 1.1, 2.4 [L]). False: the rule before 5064 (the bone's point, the
	// identity orientation, scale 1, which drew every unit projectile about seven times stock's size).
	// TriggerStreamConfig::routes; host key render_units_projectile_pose.
	bool projectilePose = true;
	// Ticket 0742 (rule A; unit-combat-server.md 18.43): a city volley flies from each shot's LAUNCH (the core's
	// CombatResult::launches, handed over by launches()), so the projectile travels its flight and lands with the core's
	// landing; the landing's hit then adds no second flight. The projectile and its ProjectileEnd children fly at
	// cityVolleyScale / cityVolleyChildScale. False: the rule before 0742 (the flight planned at the landing with its
	// start 0.6 s in the past, so it never travels, at scale 1). Host keys render_units_city_volley (and
	// render_units_city_volley_scale, render_units_city_volley_child_scale).
	bool cityVolley = true;
	float cityVolleyScale = 0.14f;
	float cityVolleyChildScale = 0.14f;
	std::string citySurface = "CITY";   // $(TERRAIN) of a hit on a city (effects-formats.md 6: CITY follows the target)
};

// One combat hit of a running fight (the core's CombatHit; its times are seconds on the fight's clock).
struct HitIn
{
	c5r::unitanim::CombatHit hit;
	uint32_t fightSeq = 0;        // the fight it belongs to (srvunits::Fight::seq)
	bool targetIsCity = false;    // the target is a city (its surface is RouteParams::citySurface)
};

struct RouteCounters
{
	// 1. clip Effect triggers
	uint64_t effectTriggers = 0, exact = 0, fallback = 0, weaponEmpty = 0, refused = 0;
	std::map<std::string, uint64_t> refusedNames;   // name as written -> triggers not sent
	// 2. held projectiles
	uint64_t held = 0, heldTaken = 0, heldExpired = 0, heldRefused = 0, heldNoPosition = 0;
	// 5064 (projectilePose): held starts at the bone's transform / without it (a point or the root, identity), and at a
	// member scale other than 1 / at 1 (no member known: FigurePositions answers 1; or a member authored at 1)
	uint64_t heldBoneTransform = 0, heldNoBoneTransform = 0, heldScaleMember = 0, heldScaleUnknown = 0;
	uint64_t transfers = 0, transfersPlanned = 0, transfersNoHeld = 0, plannerRefused = 0;
	// hit sounds (CombatHit::sound): sent when it names a known 3D script; a weapon tag is not a script
	uint64_t hitSounds = 0, hitSoundNotScript = 0;
	// 3. hits
	uint64_t hits = 0, hitOneShot = 0, hitFlight = 0, hitCityVolley = 0, hitCityDefence = 0, hitNone = 0, hitRefused = 0;
	uint64_t hitLate = 0, hitNoFight = 0, hitEmitterRefused = 0;
	uint64_t hitScaleMember = 0, hitScaleUnknown = 0;   // an unset HitEffectScale: the member's scale taken / 1.0 taken
	// 0742 (cityVolley): city shots handed over at their launch / planned as a flight there / not planned (no centre,
	// no name: the landing's hit then routes as before); landing hits whose flight left at the launch (no second flight)
	uint64_t cityLaunches = 0, cityLaunchFlights = 0, cityLaunchNotFlown = 0, hitCityVolleyFlown = 0;
	// 4. flights
	uint64_t flightsAdded = 0, flightsRefused = 0, flightsForced = 0;
	uint64_t recStart = 0, recMove = 0, recStop = 0, recChild = 0, recHit = 0, recRefused = 0;
	std::map<std::string, uint64_t> why;            // refusal texts of the emitter's API, counted (first 64 kinds)
};

class EffectRoutes
{
public:
	EffectRoutes(TriggerEmitter& emitter, c5r::effects::EffectNames& names, const RouteParams& p = RouteParams())
		: em_(emitter), names_(names), p_(p) {}
	void setSubstitutions(Substitutions* s) { subs_ = s; }                 // not owned; the emitter's own object
	void setPositions(FigurePositions* p) { pos_ = p; }                    // not owned; for held starts
	void setHeld(const c5r::effects::HeldEffects* h) { held_ = h; }        // not owned; null = no hand-over
	// Instead of setHeld: the trigger files by name (the controller's LogicCache::triggers), read into this class's own
	// HeldEffects the first time a trigger of the file is seen. Null result: the file has no held rows.
	void setTriggerFiles(std::function<const c5m::TriggerSystem*(const std::string& file)> fn) { files_ = fn; }
	// The launch of a unit's projectile at its Transfer trigger (4792's planLaunch with the core's target, or a scene
	// tool's stand-in): `effect` and `from` are the held effect's name and start point. False: no flight (counted).
	using FlightPlanner = std::function<bool(const TriggerIn& transfer, const std::string& effect, const float from[3],
	                                         c5r::effects::ProjectileSpec& out, std::string& why)>;
	void setFlightPlanner(FlightPlanner p) { planner_ = p; }
	void setScripts(const ScriptTable* t) { scripts_ = t; }               // not owned; hit sounds need a known script
	c5r::effects::ProjectileSet& flights() { return set_; }

	// The frame's seconds (the same dt as the emitter's beginFrame). False with why for a dt not finite or negative.
	bool beginFrame(double dt, std::string& why);
	double now() const { return now_; }
	void fightStarted(const srvunits::Fight& f);
	// 1 and 2: resolves the Effect triggers' names in place (TriggerIn::event becomes the name in force), removes the
	// ones that resolve to nothing and the held ones (started here). Other trigger types are left as they are.
	void intake(std::vector<TriggerIn>& in);
	// 3: hits of running fights; each is applied when its moment comes (a one-shot at hit.time, a city volley at its
	// launch), in this frame or a later one. `fightStart` = this class's clock at the fight's time 0.
	void hits(const std::vector<HitIn>& in, double fightStart);
	// 0742: a city volley's shots at their launch (the core's CombatResult::launches); with RouteParams::cityVolley each
	// is flown from its launch (applied at the launch, this frame or later). Off: ignored (counted only).
	void launches(const std::vector<HitIn>& in, double fightStart);
	// 4: a unit's projectile launched now (4792's planLaunch gave spec.path; spec.t0 on this class's clock). The flight
	// takes the held effect of (player, unit, figure) when one waits (spec.start_effect is then cleared). The set's
	// handle (0 = refused, why).
	uint32_t addFlight(c5r::effects::ProjectileSpec spec, std::string& why);
	// Fight ends (not clip ends): its flights in the air are removed with their trails, their impacts dropped; its
	// pending hits dropped.
	void ownerEnds(const std::vector<srvunits::OwnerEnd>& ends);
	// Applies the hits that are due, steps the flights to now and hands their records to the emitter; stops held
	// effects that waited too long.
	void endFrame();

	const RouteCounters& counters() const { return c_; }
	size_t pendingHits() const { return pending_.size(); }
	size_t heldWaiting() const { return heldWait_.size(); }

private:
	struct Pending
	{
		HitIn in;
		double at = 0;            // this class's clock: the moment it applies
		c5r::effects::HitEffectPlan plan;
	};
	struct Held
	{
		uint32_t handle = 0;      // the emitter's
		uint32_t fseq = 0;
		double until = 0;         // this class's clock
		int32_t trigger = -1;
		std::string name, file;   // the resolved effect, its trigger file
		float pos[3] = {0, 0, 0};
	};
	struct FlightRef
	{
		uint32_t emitterHandle = 0;
		int32_t player = -1, unit = -1, figure = -1;
		uint32_t fseq = 0;
	};
	using FigKey = std::tuple<int32_t, int32_t, int32_t>;

	void note(const std::string& why);
	c5r::effects::NameValues valuesFor(int32_t player, int32_t unit, int32_t figure, TrigType type, int32_t ec);
	bool startHeld(const TriggerIn& t);
	const c5r::effects::HeldEffects* heldFor(const std::string& file);
	void transfer(const TriggerIn& t);
	void applyHit(const Pending& p);
	float hitMemberScale(const c5r::unitanim::CombatHit& h);   // the striker's member scale, else 1 (counted)
	void pass(const std::vector<c5r::effects::ProjectileRecord>& recs);

	TriggerEmitter& em_;
	c5r::effects::EffectNames& names_;
	RouteParams p_;
	Substitutions* subs_ = nullptr;
	FigurePositions* pos_ = nullptr;
	const c5r::effects::HeldEffects* held_ = nullptr;
	std::function<const c5m::TriggerSystem*(const std::string&)> files_;
	c5r::effects::HeldEffects ownHeld_;
	std::set<std::string> filesSeen_;
	FlightPlanner planner_;
	const ScriptTable* scripts_ = nullptr;
	c5r::effects::ProjectileSet set_;
	double now_ = 0, dt_ = 0;
	bool inFrame_ = false;
	std::map<std::pair<int32_t, int32_t>, uint32_t> fightOf_;   // unit -> fight seq (running fights)
	std::vector<Pending> pending_;
	std::map<FigKey, Held> heldWait_;
	std::map<uint32_t, FlightRef> flights_;                    // the set's handle -> the emitter's
	std::map<uint32_t, std::set<int32_t>> cityFlown_;          // 0742: fight seq -> city shots flown at their launch
	c5r::effects::HitPlanParams cityPlan() const;              // 0742: p_.hit with the city volley's scales
	RouteCounters c_;
};

}  // namespace srveffects
