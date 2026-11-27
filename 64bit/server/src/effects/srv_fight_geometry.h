// srv_fight_geometry.h - where a fight's figures and their bones are, and where a projectile flies, for the trigger stream
// (epic 0042, ticket 4728; route S stage A, D210 / D211; effects-server.md section 3).
// Hand-written for this repository. SPDX-License-Identifier: GPL-3.0-or-later
//
// The three open items of 4793 (effects-formats.md 15.6) on the server's own data:
//   - positions: a fighting figure stands where its fight's FightMotion (4776) puts it at the fight's clock (x, y, yaw),
//     z = max(the height field at (x, y), the water level) + its member's ZOffset (placement.h's rule); a figure of a fight
//     without motion has no position here (the emitter then takes the unit's root, as before);
//   - bones: the trigger's bone on the figure's pose program of this frame (c5r/effects/bone_pose.h, the unit module's
//     pose step), placed at the figure's frame with its member's Scale; figureScale = that Scale;
//   - the flight planner (EffectRoutes::FlightPlanner): 4792's planLaunch with the core's target, i.e. the target the
//     striking figure's last FigureStep names (CombatResult::steps), at its pose now; a city target at the fight plot's
//     centre (Participant); the weapon row by pickWeapon (the firing member, the held effect's name); the Transfer's
//     tangent from its trigger row; the target member's TargetRadius / TargetHeight / MoveRate
//     (ArtDefine_UnitMemberCombats); hit or miss is not known at the launch (the core decides it at the landing): a hit
//     [I], counted.
// One thread (the server's render thread). Nothing throws. No EXE address (D83).
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "c5models/art.h"
#include "c5r/effects/bone_pose.h"
#include "c5r/effects/effect_routes.h"
#include "c5r/effects/projectile.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/unitanim/combat.h"
#include "c5r/unitanim/controller.h"
#include "c5r/unitanim/motion.h"
#include "c5r/units/art.h"
#include "srv_trigger_emitter.h"
#include "units/srv_units_fights.h"

namespace srveffects {

struct GeometryCounters
{
	uint64_t figureAsks = 0, figurePosed = 0, figureNoMotion = 0;
	uint64_t boneAsks = 0, boneOk = 0, boneNoFigure = 0, boneNoArt = 0, boneNoProgram = 0, boneRefused = 0;
	uint64_t plans = 0, planned = 0, planCity = 0, planStepTarget = 0, planOpponent = 0, planNoTarget = 0, planNoWeapon = 0,
	         planNoArt = 0, planRefused = 0, planNoTangent = 0;
	// 5064 (C3): figures posed whose motion gives no heading (FigurePose::yaw_known false: the yaw used is 0, while the
	// figure is drawn at its placement's heading); launches from such a figure, and those turned to face their target
	// (projectilePose on)
	uint64_t figureYawUnknown = 0, planYawUnknown = 0, planYawToTarget = 0;
	uint64_t arts = 0, artFails = 0;
	uint64_t speedMembers = 0, speedWeapons = 0;
	double boneDistSum = 0, boneDistMax = 0;   // |bone point - figure origin| over the bones given (a plausibility check)
	double boneHeightMin = 1e30, boneHeightMax = -1e30;   // bone point z - figure origin z
};

class FightGeometry : public FigurePositions
{
public:
	// The weapon rows (loadProjectileWeapons) and the members' combat rows (TargetRadius, TargetHeight, MoveRate). False
	// with why when a table cannot be read.
	bool open(c5m::TableSource& db, std::string& why);
	// The bones (null files: no bone is known, the emitter takes roots as before). strings: the .gr2 string databases.
	void setBoneSource(c5m::FileProvider* files, const c5m::StringDatabase* strings);
	void setNames(c5r::effects::EffectNames* n) { names_ = n; }   // the planner's children, hit effect and speeds
	void setGrid(const c5r::heightfield::HeightGrid* g) { grid_ = g; }
	void setWidth(uint32_t w) { width_ = w; }                     // the map's width (a plot index -> x, y)
	void setUnitRoot(std::function<bool(int32_t, int32_t, float[3])> f) { root_ = std::move(f); }
	void setSubstitutions(Substitutions* s) { subs_ = s; }        // $(TERRAIN) of a flight's landing
	void setTriggerFiles(std::function<const c5m::TriggerSystem*(const std::string& file)> f) { files_ = std::move(f); }
	// 5064 (RouteParams::projectilePose): a launching figure whose motion gives no heading launches along the direction
	// to its target, not along the tangent turned by yaw 0. False: the rule before 5064.
	void setProjectilePose(bool on) { projectilePose_ = on; }

	// Each frame, with the controller after its apply (its pose programs of this frame).
	void beginFrame(const c5r::unitanim::Controller& c);
	void fightStarted(const srvunits::Fight& f);
	// The art a unit of a fight is drawn with (by the host's art source; members in member order).
	void unitArt(int32_t player, int32_t unit, std::shared_ptr<const c5r::units::UnitArt> art);
	const c5r::units::UnitArt* artOf(int32_t player, int32_t unit) const;
	void fightRun(uint32_t seq, const c5r::unitanim::CombatResult& r, const std::shared_ptr<const c5r::unitanim::FightMotion>& m,
	              double elapsed);
	// Fight ends (not clip ends) drop the fight's state.
	void ownerEnds(const std::vector<srvunits::OwnerEnd>& ends);

	// FigurePositions
	bool bone(int32_t player, int32_t unit, int32_t figure, const std::string& bone, float out[3]) override;
	bool boneTransform(int32_t player, int32_t unit, int32_t figure, const std::string& bone, float pos[3], float orient[4]) override;
	float figureScale(int32_t player, int32_t unit, int32_t figure) override;
	bool figure(int32_t player, int32_t unit, int32_t figure, float out[3]) override;
	bool unit(int32_t player, int32_t unit, float out[3]) override;

	// EffectRoutes::FlightPlanner with `now` = the routes' clock.
	bool plan(const TriggerIn& transfer, const std::string& effect, const float from[3], double now,
	          c5r::effects::ProjectileSpec& out, std::string& why);

	const GeometryCounters& counters() const { return c_; }
	const c5r::effects::BonePoseCounts* boneCounts() const { return poser_ ? &poser_->counts() : nullptr; }
	GeometryCounters& counters() { return c_; }

private:
	using FigKey = std::tuple<int32_t, int32_t, int32_t>;
	struct Target
	{
		int32_t player = -1, unit = -1, figure = -1;
	};
	struct FightState
	{
		srvunits::FightSide att, def;
		int32_t plot = -1;
		std::shared_ptr<const c5r::unitanim::FightMotion> motion;
		double elapsed = 0;
		size_t steps = 0;                 // CombatResult::steps already read
		std::map<FigKey, Target> targets; // a striking figure's last target
	};
	struct MemberRow
	{
		float targetRadius = c5r::effects::kDefaultTargetRadius, targetHeight = 0, moveRate = 0.78125f;
	};
	const FightState* fightOf(int32_t player, int32_t unit) const;
	const c5r::units::MemberArt* memberOf(int32_t player, int32_t unit, int32_t figure) const;
	bool frameOf(int32_t player, int32_t unit, int32_t figure, c5r::effects::FigureFrame& out);
	float ground(float x, float y) const;
	bool cityCentre(const FightState& f, float out[3]) const;

	c5r::effects::ProjectileWeaponTable weapons_;
	std::map<std::string, MemberRow> rows_;
	std::unique_ptr<c5r::effects::BonePoser> poser_;
	c5r::effects::EffectNames* names_ = nullptr;
	const c5r::heightfield::HeightGrid* grid_ = nullptr;
	uint32_t width_ = 0;
	std::function<bool(int32_t, int32_t, float[3])> root_;
	Substitutions* subs_ = nullptr;
	std::function<const c5m::TriggerSystem*(const std::string&)> files_;
	std::map<uint32_t, FightState> fights_;
	std::map<std::pair<int32_t, int32_t>, uint32_t> unitFight_;    // a unit's newest running fight
	std::map<std::pair<int32_t, int32_t>, std::shared_ptr<const c5r::units::UnitArt>> arts_;
	std::map<FigKey, const c5r::unitanim::FigureProgram*> programs_;   // this frame's (pointers into the controller's)
	c5r::effects::FlightRandom rng_;
	GeometryCounters c_;
	bool projectilePose_ = true;   // 5064
	bool lastYawKnown_ = true;     // 5064: the last frameOf's FigurePose::yaw_known
};

}  // namespace srveffects
