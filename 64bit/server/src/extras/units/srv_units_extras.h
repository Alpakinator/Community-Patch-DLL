// srv_units_extras.h - the unit extras module behind the units seam (epic 0455, issue 4981). Written 2026-11-20 for this
// repository. SPDX-License-Identifier: GPL-3.0-or-later
//
// The seam (server/src/units/srv_units_scene.h, srvunits::UnitsExtras) is what the unit scene calls; this module implements
// it as an ordered list of extras, one per [Extras] option (ids 23-27: FixUnitPlacement, GarrisonVisible, ShipsMoored,
// FixAirBases, AirInterceptPatrol; 32: PerimeterGarrisons; each added by its own ticket to UnitExtraMakers() in
// srv_units_extras.cpp). Design and results:
// research/05-graphics-composition/extras-units.md section "4981".
//
// Rules the module keeps for every extra (the epic's E5 and E6):
// - configure: an extra runs when its effective state (Vp64Opt::Extra, or the extra's own state() for a stand-in) is on and
//   the units are ours (ServerUnits = 1). With ServerUnits = 0 each extra that would be on says "off: ServerUnits = 0", one
//   line; an extra whose state is off says why (Vp64Opt::DescribeExtra's reason), one line.
// - an extra that fails (unit() or beginFrame() returns false, any call throws, or it gives an override outside the seam's
//   ranges) is off for the session with one line; the unit keeps what the extras before it gave; the others go on.
// - counts per extra: ov (root, heading or foot radius set), shown (show asked for a unit stock hides), scaled, failures.
// Display only: an extra reads the feed and the scene's tables; it never calls the DLL and draws nothing of its own.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <memory>
#include <string>
#include <vector>

#include "units/srv_units_scene.h"
#include "vp64_options.h"

namespace srvextras {

// One unit extra (a feature). Every call on the render thread; a false return names why in `why`.
class UnitExtra
{
public:
	virtual ~UnitExtra() {}
	virtual const char* name() const = 0;                                 // the [Extras] key ("GarrisonVisible")
	virtual Vp64Opt::Id id() const { return Vp64Opt::kCount; }            // its option id (kCount: a stand-in with no row)
	// Its effective state from the options; a feature returns Vp64Opt::Extra(o, id()).
	virtual Vp64Opt::ExtraState state(const Vp64Opt::Options& o) const = 0;
	// Once, when it will run (state on, units ours). False: it does not run (one line with why).
	virtual bool configure(const Vp64Opt::Options& o, std::string& why) { (void)o; (void)why; return true; }
	virtual void onBatch(const SrvFeed::Batch& b) { (void)b; }
	virtual bool beginFrame(const srvunits::UnitExtrasFrame& f, std::string& why) { (void)f; (void)why; return true; }
	// One unit of the table per built frame; io holds what the scene and the extras before this one gave.
	virtual bool unit(const srvunits::UnitExtraUnit& u, srvunits::UnitOverride& io, std::string& why) = 0;
	virtual bool animating() const { return false; }
	// Issue 4983 (d): the extra gives UnitOverride::perimeterEdges, so the module's placement decorator must be in the chain
	virtual bool wantsPlacement() const { return false; }
	// Issue 4986: after a successful beginFrame the module asks for a remark to log once ("units extra <name>: <line>"); an
	// extra returns true at most once per remark (AirInterceptPatrol: a mod's promotion changes the intercept range).
	virtual bool takeNote(std::string& line) { (void)line; return false; }
};

typedef std::unique_ptr<UnitExtra> (*UnitExtraMaker)();
// The features, in the order the scene applies them (since the 0455 landing: GarrisonVisible, ShipsMoored, FixAirBases;
// 4982 FixUnitPlacement first; 4986 AirInterceptPatrol and 4983 PerimeterGarrisons (id 32, D243) last).
const std::vector<UnitExtraMaker>& UnitExtraMakers();

struct UnitExtraCounts
{
	uint64_t overrides = 0, shown = 0, scaled = 0, failures = 0;
};

// The module over a given list (tests: stand-in extras) and with the features' list (the registrar's factory).
std::unique_ptr<srvunits::UnitsExtras> MakeUnitsExtrasWith(std::vector<std::unique_ptr<UnitExtra>> extras);
std::unique_ptr<srvunits::UnitsExtras> MakeUnitsExtrasImpl();
// The counts of extra i of a module made here (tests); false for another object or i out of range.
bool UnitExtraCountsOf(const srvunits::UnitsExtras& m, size_t i, UnitExtraCounts& out, bool& running, bool& failed);

}  // namespace srvextras
