// srv_unit_intercept.h - AirInterceptPatrol, the unit extra that flies the active team's aircraft on intercept round a
// holding ring inside their intercept range (epic 0455, issue 4986; option [Extras] AirInterceptPatrol, id 27, a visual
// addition, default 0, MC #8114). Written 2026-11-20 for this repository. SPDX-License-Identifier: GPL-3.0-or-later
//
// Design and results: research/05-graphics-composition/extras-units.md section "4986". Premise [L, 4980 c7]: stock and the
// server draw a fighter on intercept (feed kind 1000, activity 5) as its parked formation at its base; no patrol.
//
// Which units (InterceptChoice): an aircraft (domain air), alive, of the active team (D207, E7: the owner's PlayerRecord
// team against the first row's activeTeam, FixAirBases' AirTeamOf), whose activity is 5 by the feed's kind 1000 (or that is
// landing after it left 5), with an intercept range > 0 (Units.AirInterceptRange of its unit type: the DLL's
// GetAirInterceptRange is that row plus promotions, and VP has no promotion that changes it, 4986.0). Never a unit held in
// a fight (lead-0042's contract) and never another team's aircraft.
//
// The motion (PatrolPose): a ring round the base plot's centre of radius share x range x d (d = the distance of two
// neighbouring plot centres), flown counter-clockwise at a constant ground speed, each aircraft at its own phase (a hash of
// owner and id); a take-off arc from the base to the ring when the order is given (kind 1000: new 5, old not 5) and a
// landing arc from the ring back to the base when it ends (old 5, new not 5); after a load (old == new == 5) the aircraft is
// on the ring at once. The arcs are quadratic Bezier curves whose ring end has the ring's tangent and speed. The seam takes
// the root (x, y) and the heading (yaw = the direction of travel + pi/2, the placement's yaw); the height above the ground
// (lift) and the flying clip are the seam's hunks H2 / H3 (4986.0), passed through when the override has them.
// Display only: reads the feed and the scene's tables, never calls the DLL.
#pragma once
#include <stdint.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "srv_unit_airbases.h"
#include "srv_units_extras.h"

namespace srvextras {

const int32_t kActivityIntercept = 5;   // ActivityTypes ACTIVITY_INTERCEPT (srv_feed.h kUnitActivityChanged)

// The look (candidates for the owner, 4986.0; the maker's one argument until the owner picks).
struct PatrolLook
{
	float radiusShare = 0.5f;   // the ring's radius as a share of the intercept range (C1: half the range)
	float radiusPlots = 0.0f;   // > 0: a fixed ring radius in plots instead (the range still gates who flies); look (d)
	// The flight height. absolute: the world z the ring is flown at (90: the bombers' figures in a stock strike, [L] 0477,
	// combat-choreography.md 13.3; a fighter's own patrol or rebase height is unchecked); else lift world units above the
	// base plot's ground.
	bool absolute = true;
	float altitude = 90.0f;
	float lift = 40.0f;
	float speed = 24.0f;        // ground speed along the ring, world units per second [I, a look]
	float arcSeconds = 6.0f;    // take-off and landing arc duration
};
PatrolLook PatrolLookA();   // (a) half range at z 90 (C1's recommendation: half range at the stock flight height; the default)
PatrolLook PatrolLookB();   // (b) the full range at z 90
PatrolLook PatrolLookC();   // (c) half range, 40 above the base's ground (lower, closer to the city)
PatrolLook PatrolLookD();   // (d) a fixed ring of 1.5 plots (96 units) at z 90: stays near its city on a close camera
                            // (lead-0455 #8298, after the r1 pictures: VP's fighter range 8 puts half range at 4 plots)

// Where a patrolling aircraft is.
struct PatrolPose
{
	float x = 0, y = 0;   // world units (the root)
	float yaw = 0;        // radians about +z, the placement's yaw (the direction of travel + pi/2)
	float lift = 0;       // world units above the ground
};

// The phase of an aircraft on its ring (radians in [0, 2 pi)), from owner and id only (stable over frames and sessions).
float PatrolPhase(int32_t owner, int32_t id);
// The ring's angle at time tS (seconds of the frame clock): phase + speed / radius x tS.
float RingAngle(float phase, float radius, float speed, double tS);
// The pose on the ring of centre (cx, cy) and radius r at angle a (counter-clockwise: travel towards a + pi/2).
PatrolPose RingPose(float cx, float cy, float r, float a, float lift);
// The take-off arc at share s in [0, 1]: from the base (bx, by) to the ring point `end` (with the ring's tangent there),
// lift 0 -> end.lift (smoothstep). The middle control point is end - tangent x (arcLen / 2) with arcLen = speed x seconds,
// so the arc ends at the ring's speed.
PatrolPose TakeOffPose(float bx, float by, const PatrolPose& end, float arcLen, float s);
// The landing arc at share s: from the ring point `start` (its tangent) to the base, lift start.lift -> 0.
PatrolPose LandingPose(const PatrolPose& start, float bx, float by, float arcLen, float s);

// The choice for one unit (pure: the unit tests check it without a scene).
struct InterceptChoice
{
	enum Kind
	{
		NotAircraft = 0,   // not domain air: untouched
		NotPatrolling,     // no activity 5 and no landing under way: untouched (stock's parked formation)
		Dead,              // dead or delayed death: untouched
		Held,              // held in a fight: untouched
		NoPlayer,          // its owner has no player row: untouched
		Foreign,           // another team's aircraft: untouched (E7)
		NoPlot,            // its plot is outside the grid: untouched
		NoRange,           // its unit type has no intercept range (> 0): untouched
		Flying             // on the ring or an arc: root, heading, shown
	};
	Kind kind = NotAircraft;
};
// patrolling: the extra's activity map says 5 or a landing is under way; range: the unit type's AirInterceptRange (< 0:
// unknown); team: AirTeamOf.
InterceptChoice ChooseIntercept(const srvunits::UnitExtraUnit& u, bool patrolling, int32_t range, AirTeam team, bool plotOk);

// The extra. id: its option row (Vp64Opt::kAirInterceptPatrol once row 27 exists; the maker in UnitExtraMakers() passes it).
// ranges: Units ID -> AirInterceptRange; until the seam passes the database's (hunk H1) the caller gives it here (tests,
// the scratch player). An empty table: every aircraft NoRange (E6: one line at the first aircraft on intercept).
std::unique_ptr<UnitExtra> MakeAirInterceptPatrol(Vp64Opt::Id id, const PatrolLook& look = PatrolLookA(),
                                                  const std::map<int32_t, int32_t>& ranges = std::map<int32_t, int32_t>());

// Counts (unit-frames, except activityEvents, takeOffs, landings, loads: events taken).
struct InterceptCounts
{
	uint64_t frames = 0, aircraft = 0, flying = 0, onRing = 0, takingOff = 0, landing = 0, dead = 0, held = 0, foreign = 0,
	         noPlayer = 0, noPlot = 0, noRange = 0, activityEvents = 0, takeOffs = 0, landings = 0, atLoad = 0;
	uint64_t promotionNotes = 0;   // MC #11389: remarks taken by the module (a mod's promotion changes the intercept range)
	uint64_t noActivityNotes = 0;  // MC #11423: the no-render-messages line queued (at most 1 per session)
	size_t patrols = 0;  // aircraft with activity 5 now
	float lastLift = 0;   // the lift above the ground at the root of the last flying unit (until the seam's hunk H2 takes it)
};
bool InterceptCountsOf(const UnitExtra& x, InterceptCounts& out);

// MC #11389: the one log line for promotions that change the intercept range (types: UnitPromotions.Type); "" for none.
std::string PromotionNote(const std::vector<std::string>& types);
// 2026-11-23 (MC #11423): the one line per session when no feed kind 1000 arrived in 5 s of frames with a unit table after
// a load: the DLL sends no render messages (the 0.1b pins), so no aircraft is known to be on intercept.
extern const char* const kNoActivityNote;

}  // namespace srvextras
