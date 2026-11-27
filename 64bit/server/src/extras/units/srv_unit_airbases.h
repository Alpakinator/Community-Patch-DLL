// srv_unit_airbases.h - FixAirBases, the unit extra that draws based and carried aircraft after a live change of their plot
// as stock draws them after a load (epic 0455, issue 4985; option [Extras] FixAirBases, id 26, bugfix, default 1 since D235
// (the owner, 2026-11-22, after its live smoke; was 0 until the owner accepted it, MC #7901). Written 2026-11-20 for this repository. SPDX-License-Identifier: GPL-3.0-or-later
//
// Stock is not consistent with itself (research/05-graphics-composition/extras-units.md "4985 re-scope" [L] and "4985
// FixAirBases"): the DLL's CvPlot::updateCenterUnit gives every unit of a plot except its centre unit Visibility(false)
// whenever that plot's units change (a unit enters or leaves, a kill), and an aircraft is never the centre unless selected;
// CvUnit::setXY also hides every unit that enters a city. During a load neither runs (both return early while the graphics
// are not initialised; CvUnit::setupGraphical hides only a garrisoned unit), so after a load every aircraft is drawn, and
// after a live rebase, a carrier move or a kill on its plot the same aircraft is not. The server draws exactly what stock
// draws (it follows the DLL's Visibility messages). With this extra on, the server takes stock's after-load behaviour.
//
// Which units (AirBaseChoice): an aircraft (domain air: fighters, bombers, missiles; always based in a city or carried by a
// carrier, a submarine or a cruiser) that the scene hides (the visible flag clear), alive (no dead or delayed-death flag),
// of the active team (E7: the after-load state shows another team's aircraft only on plots the active team sees; this
// extra shows none of them, the conservative reading), not walking (its scene plot is the record's plot, and a scene root,
// if any, is at that plot's centre). It never sets a root, a heading or a scale: the after-load placement is the scene's
// own for a listed aircraft with no position record (4980's bundles: no position record for any aircraft after the load;
// a rebase or a teleport clears the record, srv_units_core.cpp), so a shown aircraft is placed as after a load.
// Never: a unit held in a fight (lead-0042's contract; the seam ignores it anyway).
// Inputs: the scene's unit table and player table (UnitExtrasFrame; missing: E6, off for the session with one line) and
// its height field (none this frame: nothing to do, the scene draws no unit either). Feed kind 1001 (carrier, the DLL's
// render events) only splits the counts into carried and based; kind 30 (rebase) is not needed: its effect, the hide,
// reaches the extra through the visible flag, and the scene clears the rebased unit's root itself.
// Display only: reads the scene's tables and the feed, never calls the DLL.
#pragma once
#include <stdint.h>
#include <memory>
#include <string>
#include <vector>

#include "srv_units_extras.h"

namespace srvextras {

const int8_t kDomainAir = 1;   // DomainTypes DOMAIN_AIR (DOMAIN_SEA 0, DOMAIN_AIR 1, DOMAIN_LAND 2)

// A scene root farther than this share of the hex's inner radius from its plot centre is a walking unit (not shown). The
// same share as GarrisonVisible's (4983).
const float kAirBaseWalkShare = 0.02f;

// An aircraft alive by the record's flags: domain air, no dead or delayed-death flag.
bool IsLiveAircraft(const SrvFeed::UnitRecord& r);

// The owner's team against the active team (from the scene's player table).
enum class AirTeam
{
	Active = 0,   // the owner's team is the active team
	Other = 1,    // another team (E7: not shown)
	NoRecord = 2  // the owner has no player row: not shown (the scene does not draw it either)
};
// The team of rec's owner in players (indexed by player; a row whose player field differs is no record).
AirTeam AirTeamOf(const SrvFeed::UnitRecord& r, const std::vector<SrvFeed::PlayerRecord>& players);

// What the extra does for one unit (pure: the unit tests check it without a scene).
struct AirBaseChoice
{
	enum Kind
	{
		NotAircraft = 0,   // not domain air: untouched
		Listed,            // stock lists it: nothing to show
		Dead,              // dead or delayed death (fading out by a kill): untouched
		Held,              // held in a fight: untouched
		NoPlayer,          // its owner has no player row: untouched
		Foreign,           // another team's aircraft: untouched (E7)
		NoPlot,            // its plot is outside the grid: untouched
		Walking,           // its scene plot or root is not its plot's: untouched
		Shown              // shown (the scene's own placement)
	};
	Kind kind = NotAircraft;
	bool show = false;
};
// walking: the unit's scene plot (u.plotX, u.plotY) is not its record's plot, or its scene root is farther than
// kAirBaseWalkShare inner radii from that plot's centre (the caller decides; AirBaseWalking below). plotOk: the record's
// plot is inside the grid.
AirBaseChoice ChooseAirBase(const srvunits::UnitExtraUnit& u, AirTeam team, bool plotOk, bool walking);

// The walking test: (recX, recY) the record's plot; centreOk/cx/cy its centre (c5r::units::plotCentre); inner the hex's
// inner radius. A root with no centre to compare is walking (cannot tell: not shown).
bool AirBaseWalking(const srvunits::UnitExtraUnit& u, int32_t recX, int32_t recY, bool centreOk, float cx, float cy, float inner);

// The extra. id: its option row (Vp64Opt::kFixAirBases once the row exists; the maker in UnitExtraMakers() passes it).
std::unique_ptr<UnitExtra> MakeFixAirBases(Vp64Opt::Id id);

// Counts of a FixAirBases made here (tests, the research numbers); false for another extra. Unit-frames, except carrierEvents
// (kind 1001 events taken) and cargo (units cargo now by those events).
struct AirBaseCounts
{
	uint64_t frames = 0, aircraft = 0, shown = 0, shownCarried = 0, shownBased = 0, dead = 0, held = 0, foreign = 0,
	         noPlayer = 0, noPlot = 0, walking = 0, carrierEvents = 0;
	size_t cargo = 0;
};
bool AirBaseCountsOf(const UnitExtra& x, AirBaseCounts& out);

}  // namespace srvextras
