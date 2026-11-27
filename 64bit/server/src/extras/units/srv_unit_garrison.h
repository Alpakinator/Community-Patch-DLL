// srv_unit_garrison.h - GarrisonVisible, the unit extra that draws a city's military garrison at the city (epic 0455,
// issue 4983; option [Extras] GarrisonVisible, id 24, visual addition, default 0). Written 2026-11-20 for this repository.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stock hides a garrisoned unit (the DLL's Visibility(false) at a load and when a unit enters a city; extras-units.md
// 4981.2), so a city shows no figure of its garrison. This extra asks the units seam to show it and, in the chosen look,
// puts the formation at the plot's south edge, turned to face south (out of the city, towards the default camera).
// Design, the looks and the results: research/05-graphics-composition/extras-units.md section "4983".
//
// Which units (GarrisonChoice): the unit table's garrisoned flag (kind 22 and every unit event's IsGarrisoned getter),
// land domain only (a ship that counts as a garrison is ShipsMoored's, 4984; an aircraft is never garrisoned), not
// embarked, not dead. Civilians are never garrisoned, and stock already draws those standing in a city after a load (4980).
// A foreign garrison: the seam's fog filter keeps another team's unit only on a plot the active team sees now and the seam
// never shows another team's invisible unit; that is the rule by which the game's own unit flags show a foreign garrison
// (the flag hidden when the plot is not visible or the unit is invisible to the active team) [C, UnitFlagManager.lua of the
// base game and of the VP EUI files, read 2026-11-20].
// Never: a unit held in a fight (lead-0042's contract; the seam ignores it anyway), a root or heading for a walking unit
// (its scene root is not its plot's centre: lead-0042 #7810, our #7812).
// Display only: reads the scene's tables and height field, never calls the DLL.
#pragma once
#include <stdint.h>
#include <memory>
#include <string>

#include "srv_units_extras.h"

namespace srvextras {

// How a shown garrison stands (the candidates for the owner, extras-units.md 4983.3)
enum class GarrisonLook
{
	SouthEdge = 0,          // (a) the whole formation at the plot's edge, the south side if it is on land (else the next
	                        // side on land, kGarrisonSideDeg), facing out of the city; the default
	Centre = 1,             // (b) the whole formation at the plot centre among the buildings, with the unit's own facing
	SouthEdgeCompact = 2,   // (c) as (a), farther out and smaller: clear of a unit stock draws at the city centre (4983.3)
	Perimeter = 3           // (d) the living figures spread in a ring along the plot's land edges, facing out (stage d, the
	                        // owner 2026-11-23: render/src/extras/units/garrison_perimeter.h; extras-units.md 4983.14)
};

// The edge looks' distance from the plot centre, as a share of the hex's inner radius (half the distance between two
// neighbouring plot centres in a row): the formation's root this far south of the centre; and the compact look's scale.
const float kGarrisonEdgeShare = 0.6f;
const float kGarrisonCompactShare = 0.85f;
const float kGarrisonCompactScale = 0.8f;
// A scene root farther than this share of the inner radius from the plot centre is a walking unit (no root or heading).
const float kGarrisonWalkShare = 0.02f;
// The yaw that turns a unit to face south (the module's placement: yaw = 150 - 60 facing degrees, the front along local
// -y; unit-formations.md 11.2): 0. A unit facing the world angle a (radians from +x, y north) has yaw a + pi/2.
const float kGarrisonSouthYaw = 0.0f;
// The edge looks try these sides of the plot in this order (world angle from +x in degrees; y north): south first (the
// default camera looks north, so the south side is in front of the city's buildings), then south-east, south-west, east,
// west, north-east, north-west. A side is taken when the formation's footprint there is on land: the height field above
// the water level at the root and at four points kGarrisonFootShare x inner radius (x the look's scale) around it. No
// side on land (a one-plot island city): the centre look's placement for that unit.
const int kGarrisonSides = 7;
const float kGarrisonSideDeg[kGarrisonSides] = {-90.0f, -60.0f, -120.0f, 0.0f, 180.0f, 60.0f, 120.0f};
const float kGarrisonFootShare = 0.5f;

// A unit this extra treats as a city's military garrison: garrisoned, land domain (DOMAIN_LAND 2), not embarked, not dead.
bool IsGarrisonUnit(const SrvFeed::UnitRecord& r);
const int8_t kDomainLand = 2;

// The hex's inner radius from the height field (half the distance of plot centres (0, 0) and (1, 0)); false with why
// when the field gives no plot centres (E6: the extra switches itself off).
bool GarrisonInnerRadius(const c5r::heightfield::HeightGrid& g, float& inner, std::string& why);

// What the extra does for one unit (pure: the unit tests check it without a scene).
struct GarrisonChoice
{
	enum Kind
	{
		NotGarrison = 0,   // not a garrison by IsGarrisonUnit: untouched
		Held,              // held in a fight: untouched
		NoPlot,            // no plot centre for its plot (outside the field): untouched (the scene does not draw it either)
		Walking,           // shown if hidden, no root or heading
		Placed             // shown if hidden, placed by the look
	};
	Kind kind = NotGarrison;
	bool show = false;
	bool root = false;
	float rootX = 0, rootY = 0;
	bool heading = false;
	float yaw = 0;
	float scale = 1;
	int side = -1;            // the edge looks: the index into kGarrisonSideDeg taken; -1 none (centre)
	bool noLandSide = false;  // an edge look found no side on land: placed as the centre look
	uint8_t perimeter = 0;    // look (d): the plot's land edges (bits 0..5, garrison_perimeter.h); 0 with noLandSide
};
// centreOk/cx/cy: the unit's plot centre (c5r::units::plotCentre); inner: GarrisonInnerRadius; grid: the height field for
// the edge looks' land test (null: no test, the first side is taken).
GarrisonChoice ChooseGarrison(const srvunits::UnitExtraUnit& u, bool centreOk, float cx, float cy, float inner, GarrisonLook look,
                              const c5r::heightfield::HeightGrid* grid = nullptr);
// The land test of one side: true when the height field is above its water level at (x, y) and at the four points r
// away from it along x and y.
bool GarrisonFootOnLand(const c5r::heightfield::HeightGrid& g, float x, float y, float r);

// The extra itself. id: its option row (Vp64Opt::kGarrisonVisible once the row exists; the maker in UnitExtraMakers()
// passes it). A test value since D243 (the row's Def::testOnly): while PerimeterGarrisons is in effect it steps aside
// (configure notes it, every unit untouched, one note line "units extra GarrisonVisible: PerimeterGarrisons = 1 ...").
std::unique_ptr<UnitExtra> MakeGarrisonVisible(Vp64Opt::Id id, GarrisonLook look = GarrisonLook::SouthEdge);
// PerimeterGarrisons (ticket 4983, D243: the owner's "Perimeter Garrisons", option [Extras] PerimeterGarrisons, id 32,
// visual addition, default 0): the same extra with look (d), named after its own row. id: Vp64Opt::kPerimeterGarrisons.
std::unique_ptr<UnitExtra> MakePerimeterGarrisons(Vp64Opt::Id id);

// Counts of a GarrisonVisible or PerimeterGarrisons made here (tests, and the extra's research numbers); false for another
// extra.
struct GarrisonCounts
{
	uint64_t frames = 0, garrisons = 0, shown = 0, placed = 0, walking = 0, held = 0, noPlot = 0, noLandSide = 0;
};
bool GarrisonCountsOf(const UnitExtra& x, GarrisonCounts& out);

}  // namespace srvextras
