// srv_unit_placement_fix.h - FixUnitPlacement, the unit extra that keeps unit figures out of mountain and hill rock (epic
// 0455, issue 4982; option [Extras] FixUnitPlacement, id 23, bugfix, default 0 until the owner accepts it, MC #8099). Written
// 2026-11-20 for this repository. SPDX-License-Identifier: GPL-3.0-or-later
//
// The defect (research/05-graphics-composition/extras-units.md 4982.2 [C]): every figure stands upright on the one height
// sample under its origin, so on a mountain or hill slope part of each figure is inside the rock (a horse's hooves up to 41 %
// of its height on the c1 mountain) while its downhill side floats; stock and the server alike. The rule is our own (D83),
// scored offline before any code (4982.4):
//
// Part A (this extra, the seam's root override, extras-units.md 4981.1): a land unit at rest on its plot has its formation
// moved to the flattest ground near the plot centre. Candidates every kFlatStep units within kFlatSearch units of the centre
// (0.31 of the hex's inner radius); each one's cost is the mean slope of the terrain (bilinear between the samples, central
// differences of +-0.5 units) over the points of a kFlatStep grid within kFlatRadius of it; the cheapest wins, ties to the
// nearer candidate. The unit moves only when that saves at least kFlatMinGain of slope and kFlatMinShare of the centre's
// (flat plots stay), and never onto ground with more points under the water than the centre has. The move eases in over
// kSettleMs when a unit arrives (a unit already at rest when first seen takes it at once); a unit that starts to walk snaps
// back to the scene's root (it merges into the move; lead-0455 #8158). Part B (each figure on the highest ground within 3
// units of its origin) is placement.cpp's footBase (agreed by lead-0042 #8163/#8184); this extra asks for it with the seam's
// UnitOverride::footRadius (FootRadiusFor: units it moves and units on a rough plot), eased in like the move.
//
// Which units (PlacementChoice): domain land, not embarked, alive (no dead or delayed-death flag), drawn this frame (listed
// by stock or shown by an earlier extra), not held in a fight (lead-0042's contract; the seam ignores it anyway), not
// walking (its scene plot is the record's and its scene root, if any, is within kPlacementWalkShare inner radii of that
// plot's centre), and no earlier extra gave it a root.
// Known limit: a moved unit that 0042's fight queue holds snaps back to the scene's root for the fight (the seam ignores a
// held unit's override; asked of lead-0042, 2026-11-20 #8158).
// Inputs: the scene's unit table and height field (UnitExtrasFrame). No unit table: E6, off with one line. No height field
// this frame: nothing to do (the scene draws no unit either).
// Cost: the pick depends only on the plot and the heights around it, so it is cached per plot and recomputed only when a
// sparse checksum of the samples around that plot changes (a live terrain change).
// Display only: reads the scene's tables, never calls the DLL.
#pragma once
#include <stdint.h>
#include <memory>
#include <string>

#include "srv_units_extras.h"

namespace c5r { namespace heightfield { struct HeightGrid; } }

namespace srvextras {

const int8_t kPlacementDomainLand = 2;   // DomainTypes DOMAIN_LAND
const float kFlatSearch = 10.0f;         // the farthest candidate root from the plot centre (units; inner radius 32)
const float kFlatStep = 2.0f;            // candidate and cost grid step (units)
const float kFlatRadius = 10.0f;         // the ground a formation stands on, around a candidate (units)
const float kFlatMinGain = 0.05f;        // the least saving of mean slope (dz per unit run) for a move
const float kFlatMinShare = 0.10f;       // ... and the least share of the centre's mean slope
const float kPlacementWalkShare = 0.02f; // a root farther from its plot centre than this share of the inner radius walks
const double kSettleMs = 400.0;          // the move eases in over this when a unit arrives
const float kFootRadius = 3.0f;          // part B: each figure on the highest ground within this of its origin (4982.4 max_r3)
const float kFootRoughSlope = 0.25f;     // part B also on a plot that does not move when its centre's mean slope is at least this

// The terrain between the samples: bilinear on the samples heightAtWorld reads (wraps and edges as heightAtSample).
float SurfaceAt(const c5r::heightfield::HeightGrid& g, float x, float y);
// The mean slope |grad| of SurfaceAt over the points of a step grid within radius of (x, y); underWater counts the points
// whose surface is below the water (g.waterZ).
float MeanSlope(const c5r::heightfield::HeightGrid& g, float x, float y, float radius, float step, int* underWater);

// The pick for one plot centre (pure: the unit tests check it on synthetic fields).
struct FlatGround
{
	bool move = false;            // a candidate saves enough slope
	float dx = 0, dy = 0;         // the formation's offset from the plot centre (0, 0 when not moving)
	float slopeCentre = 0;        // the centre's mean slope
	float slopePick = 0;          // the pick's (the centre's when not moving)
	int candidates = 0;           // candidates scored
};
FlatGround FindFlatGround(const c5r::heightfield::HeightGrid& g, float cx, float cy);

// What the extra does for one unit (pure).
struct PlacementChoice
{
	enum Kind
	{
		NotLand = 0,   // domain not land: untouched
		Embarked,      // embarked (drawn as a boat): untouched
		Dead,          // dead or delayed death: untouched
		NotDrawn,      // neither listed by stock nor shown by an earlier extra: untouched
		Held,          // held in a fight: untouched
		Rooted,        // an earlier extra gave it a root: untouched
		NoPlot,        // its plot is outside the grid: untouched
		Walking,       // its scene plot or root is not its plot's: untouched
		Candidate      // at rest on its plot: FindFlatGround decides
	};
	Kind kind = NotLand;
};
PlacementChoice ChoosePlacement(const srvunits::UnitExtraUnit& u, const srvunits::UnitOverride& io, bool plotOk, bool walking);

// The walking test (as FixAirBases's): (recX, recY) the record's plot; centreOk/cx/cy its centre; inner the inner radius.
bool PlacementWalking(const srvunits::UnitExtraUnit& u, int32_t recX, int32_t recY, bool centreOk, float cx, float cy,
                      float inner);

// The ease-in share after rest began at restSinceMs (negative: at rest when first seen, 1 at once).
float SettleShare(double nowMs, double restSinceMs);
// Part B's field (UnitOverride::footRadius) for a unit at rest on its plot: kFootRadius when its formation moves or its
// plot centre is rough (mean slope >= kFootRoughSlope), else 0 (the EXE's rule). It eases in with the move (x SettleShare).
float FootRadiusFor(const FlatGround& fg);

// The extra. id: its option row (Vp64Opt::kFixUnitPlacement once the row exists; the maker in UnitExtraMakers() passes it).
std::unique_ptr<UnitExtra> MakeFixUnitPlacement(Vp64Opt::Id id);

// Counts of a FixUnitPlacement made here (tests, the research numbers): unit-frames, except picks (FindFlatGround runs) and
// cachedPlots.
struct PlacementCounts
{
	uint64_t frames = 0, land = 0, moved = 0, settling = 0, flat = 0, embarked = 0, dead = 0, notDrawn = 0, held = 0,
	         rooted = 0, noPlot = 0, walking = 0, picks = 0, feet = 0;   // feet: unit-frames given a foot radius (part B)
	size_t cachedPlots = 0;
};
bool PlacementCountsOf(const UnitExtra& x, PlacementCounts& out);

}  // namespace srvextras
