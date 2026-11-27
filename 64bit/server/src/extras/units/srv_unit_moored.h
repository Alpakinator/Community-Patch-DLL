// srv_unit_moored.h - ShipsMoored, the unit extra that draws the ships in a city moored on the city plot's water side
// (epic 0455, issue 4984; option [Extras] ShipsMoored, id 25, visual addition, default 0 by D207). Written 2026-11-20 for
// this repository. SPDX-License-Identifier: GPL-3.0-or-later
//
// Stock draws no ship standing in a city (the DLL hides a unit that enters a city and a garrison at a load; extras-units.md
// 4981.2, 4980's c4 mask [L]). This extra asks the units seam to show each ship in a city and puts it on the plot's water
// side: a side of the plot whose footprint is water, the hull facing the look's way; several ships in one city stand in a
// row along the shore. Design, the looks and the results: research/05-graphics-composition/extras-units.md section "4984".
//
// Which units (IsPortShip): the sea domain (DOMAIN_SEA 0), not dead, not delayed death, on a plot whose centre is land in
// the height field (above its water level). A ship can stand on a land plot only in a city or, in VP (GLOBAL_PASSABLE_FORTS
// = 1), on a fort, a citadel or a feitoria used as a canal; the feed carries no city flag on a plot (PlotRecord flags bit 4
// is never set), so such a canal plot is treated as a port too (extras-units.md 4984.1). An embarked land unit is land
// domain and never taken. A foreign ship: the seam's fog filter keeps another team's unit only on a plot the active team
// sees now and never shows another team's invisible unit (a submarine), as the game's unit flags do (4983.2).
// Never: a unit held in a fight (lead-0042's contract), a root or heading for a walking ship (its scene plot is not its
// record's plot, or its scene root is more than 0.02 inner radii from the plot centre: lead-0042 #7810): shown only.
// Display only: reads the scene's tables and height field, never calls the DLL.
#pragma once
#include <stdint.h>
#include <memory>
#include <string>
#include <vector>

#include "srv_units_extras.h"

namespace srvextras {

// How a moored ship stands (the candidates, extras-units.md 4984.3; the owner's pick D250: (d) for value 1, (b) for
// value 2). The enum's numbers are not the option's values (MooredLookOfValue maps them).
enum class MooredLook
{
	BowToLand = 0,        // (a) the bow to the city, the stern to the sea; several ships side by side along the shore
	                      // (C1's recommendation; not offered, D250)
	AlongShore = 1,       // (b) alongside the shore (the hull parallel to the plot's edge); several ships end to end
	                      // (ShipsMoored = 2)
	OutToSea = 2,         // (c) the bow to the sea; several ships side by side (not offered, D250)
	BowToLandSmall = 3    // (d) as (a) at kMooredSmallScale (ShipsMoored = 1)
};

const int8_t kDomainSea = 0;
// The scale of look (d).
const float kMooredSmallScale = 0.8f;
// A scene root farther than this share of the inner radius from the plot centre is a walking ship (no root or heading).
const float kMooredWalkShare = 0.02f;
// The water side: the extra tries these directions from the plot centre (world angle from +x in degrees, y north; the
// garrison's order: south first, the default camera looks north) and in each the distances kMooredNearShare,
// kMooredNearShare + kMooredStepShare, ... up to kMooredFarShare (shares of the inner radius; the plot's edge is at 1, its
// south corner at 1.155). It takes the nearest distance at which the whole row's footprint is water, the earlier
// direction on a tie.
const int kMooredSides = 7;
const float kMooredSideDeg[kMooredSides] = {-90.0f, -60.0f, -120.0f, 0.0f, 180.0f, 60.0f, 120.0f};
const float kMooredNearShare = 0.6f;
const float kMooredFarShare = 1.6f;
const float kMooredStepShare = 0.1f;
// A ship's footprint (shares of the inner radius, x the look's scale): the root and the points kMooredHalfLength along the
// hull and kMooredHalfBeam across it on either side; each must be water (the height field at or below its water level).
// Set from round r1 (extras-units.md 4984.6): a trireme at stock scale is about one inner radius long; with 0.35 / 0.15
// the along-the-shore look stood with its stern on the beach.
const float kMooredHalfLength = 0.5f;
const float kMooredHalfBeam = 0.2f;
// The distance between neighbouring ships' roots in a row (shares of the inner radius, x the look's scale): side by side
// (looks a, c, d) and end to end (look b).
const float kMooredBeamGap = 0.5f;
const float kMooredLengthGap = 1.1f;

// A ship this extra moors: sea domain, alive (no dead or delayed-death flag).
bool IsLiveShip(const SrvFeed::UnitRecord& r);

// One ship's mooring: its root and the yaw that turns it (the seam's heading: yaw = facing angle + 90 degrees, the module's
// yaw 150 - 60 d degrees with the front along local -y; unit-formations.md 11.2), its scale.
struct MooredSlot
{
	float x = 0, y = 0, yaw = 0, scale = 1;
	int side = -1;       // the index into kMooredSideDeg; -1: no water spot for this ship (the fallback below)
	float share = 0;     // the distance from the plot centre, in inner radii
};
// The row of n ships of one port plot (centre cx, cy; inner radius inner): out gets n slots, member j at offset
// (j - (n - 1) / 2) x gap along the shore. When no direction and distance holds the whole row on water and n > 1, the
// fallback places each ship on its own, in order, at the nearest water spot along the shore (offsets 0, +gap, -gap,
// +2 gap, ... at each direction and distance) at least one gap from every ship placed before it; a ship with no such spot
// gets side -1. False (out empty) when no ship is placed.
bool PlanMooredRow(const c5r::heightfield::HeightGrid& g, float cx, float cy, float inner, int n, MooredLook look,
                   std::vector<MooredSlot>& out);
// The footprint test of one ship at (x, y) whose hull points along the world angle hullRad, at scale s.
bool MooredFootOnWater(const c5r::heightfield::HeightGrid& g, float x, float y, float hullRad, float inner, float s);
// The facing (world angle, radians) and the hull's direction of a look on the side at world angle a (radians); the row's
// gap for the look (share of the inner radius, before the scale).
float MooredFacing(MooredLook look, float a);
float MooredGapShare(MooredLook look);
float MooredScale(MooredLook look);

// What the extra does for one unit (pure: the unit tests check it without a scene).
struct MooredChoice
{
	enum Kind
	{
		NotShip = 0,     // not a live ship on a port plot (IsLiveShip, its plot's centre on land): untouched
		Held,            // held in a fight: untouched
		Walking,         // shown if hidden, no root or heading
		NoWaterSide,     // a port ship with no water spot: left as stock has it (never shown: it would stand on land)
		Placed           // shown if hidden, placed at its slot
	};
	Kind kind = NotShip;
	bool show = false;
	bool root = false;
	MooredSlot slot;
};
// port: the unit is a live ship whose record plot's centre is land (the extra's frame plan); walking: AirBaseWalking's
// rule for this unit; slot: its slot in its port's row (null: no water spot for it).
MooredChoice ChooseMoored(const srvunits::UnitExtraUnit& u, bool port, bool walking, const MooredSlot* slot);

// The option's values (D250, the owner 2026-11-24, after the live sheets of 2026-11-23): 0 = off (the default, D207),
// 1 = look (d) BowToLandSmall, 2 = look (b) AlongShore. Looks (a) and (c) are not offered to players (no value names them;
// MakeShipsMoored's fixed-look form keeps them for the tests). False (look untouched) for a value that names no look.
bool MooredLookOfValue(int value, MooredLook& look);
// The look's short name for the log ("look (d) BowToLandSmall").
const char* MooredLookName(MooredLook look);

// The extra itself. id: its option row (Vp64Opt::kShipsMoored; the maker in UnitExtraMakers() passes it). The look is the
// option's value in force (MooredLookOfValue of Vp64Opt::Extra(o, id).value), read once in configure; a value that names no
// look fails configure (one line). The extra logs its look once, after its first frame ("units extra ShipsMoored: look
// (d) BowToLandSmall (ShipsMoored = 1)").
std::unique_ptr<UnitExtra> MakeShipsMoored(Vp64Opt::Id id);
// The same with a fixed look, whatever the value (the unit tests and the scratch look switch of the live step; never the
// product's maker).
std::unique_ptr<UnitExtra> MakeShipsMoored(Vp64Opt::Id id, MooredLook look);

// Counts of a ShipsMoored made here (tests, and the extra's research numbers); false for another extra. Unit-frames, except
// frames, ports (port plots with a ship, per frame) and rows (port plots with more than one ship, per frame).
struct MooredCounts
{
	uint64_t frames = 0, ports = 0, rows = 0, ships = 0, shown = 0, placed = 0, walking = 0, held = 0, noWaterSide = 0;
};
bool MooredCountsOf(const UnitExtra& x, MooredCounts& out);
// The look a ShipsMoored made here uses (after configure for the value-driven form); false for another extra.
bool MooredLookOf(const UnitExtra& x, MooredLook& out);

}  // namespace srvextras
