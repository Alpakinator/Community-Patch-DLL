// c5r/units/placement_check.h - the placement oracle check (epic 0041, U3, issue 4354, DoD D): every unit of 3907's
// catalogue placed with placeUnit() and compared with 0327's exact figure positions (unit-formations.md 12.2-12.4).
// Callable from a test (render/tests/units/test_placement_catalogue.cpp) and from 4355's oracle tool.
//
// The catalogue text comes from models/tools/formations/catalogue_tsv.py (numbers only, committed as
// models/evidence/0041/4354/catalogue_figures.tsv). Results: unit-renderer.md section 12.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "c5r/status.h"
#include "c5r/units/placement.h"

namespace c5r { namespace units {

struct CatalogueMember {
    std::string type;       // ArtDefine_UnitInfoMemberInfos.UnitMemberInfoType, in row order
    int count = 0;          // NumMembers
    std::string domain;     // ArtDefine_UnitMemberInfos.Domain ("" for land)
};
struct CatalogueFigure {
    std::vector<std::string> members;   // the member type(s) whose meshes formed this measured figure
    float x = 0, y = 0, z = 0;          // exact T (world)
    float yawDeg = 0;                   // fitted yaw
    float rotMaxDeg = 0;                // the palette fit's rotation error (>= 0.05: a cross-fade or poor fit)
    float partSpread = 0;               // how far the T of the figure's meshes lie apart (> 0.002: a mesh cross-fading)
};
struct CatalogueUnit {
    std::string frame, artRow;
    int object = 0;
    int32_t plotX = 0, plotY = 0;
    int facing = 2;
    uint32_t seed = 0;
    std::vector<CatalogueMember> members;
    std::vector<CatalogueFigure> figures;
    int line = 0;
};
// Parses catalogue_tsv.py's text (header "c5placement-catalogue\t1"). Checks: a missing header [BadVersion]; an
// unknown record, a wrong field count or a number that does not parse completely [BadLayout]; a member or fig line
// before any unit [BadLayout]. On failure `out` is empty; the message names the line.
Status parseCatalogue(const std::string& text, std::vector<CatalogueUnit>& out);

struct CheckTolerances {
    float xy = 0.002f;      // world units, figure position in the ground plane
    float yawDeg = 0.1146f; // degrees (0.002 rad: the fits' own yaw noise reaches 0.08 deg, unit-renderer.md 12)
    float z = 0.001f;       // world units
};

struct FigureCheck {
    int figure = -1;            // index in member order (-1: not placed, heights only)
    int measured = -1;          // index into CatalogueUnit::figures
    std::string member, domain;
    float dxy = 0, dyawDeg = 0;
    float dz = 0;               // placed z - measured z (at the placed slot)
    float dzAtMeasured = 0;     // the height rule at the measured x, y - measured z
    float rotMaxDeg = 0;
    float partSpread = 0;
    std::string heightClass;    // "" within tolerance; else air, sea, fit, grouping, other
};
struct UnitCheck {
    const CatalogueUnit* unit = nullptr;
    std::string kind;           // multi, single, no-figures, figures!=members, failed
    std::string detail;         // the failed Status text, or the formation used
    float maxDxy = 0, maxDyawDeg = 0, maxDz = 0;
    bool within = false;        // positions and facings within tolerance (multi and single)
    std::string outsideClass;   // when not within: air (an Air-domain figure), fit (a figure whose palette fit is
                                // a cross-fade: rot >= 0.05 deg or part spread > 0.002), sea, else other
    std::vector<FigureCheck> figures;
};
struct CatalogueReport {
    std::vector<UnitCheck> units;
    int multi = 0, multiWithin = 0, single = 0, singleWithin = 0, notCompared = 0, failed = 0;
    int heightFigures = 0, heightWithin = 0;
    int multiOutsideAir = 0, multiOutsideFit = 0, multiOutsideOther = 0;
};

// Places every unit (all figures alive, the unit's seed and facing, position source own) and pairs placed and
// measured figures by minimum total distance among figures of the same member type (the measured order is the draw
// order). waterLevel: the scene's floor (the height field's waterZ). Checks: none of its own beyond its inputs':
// a unit whose placement fails is counted in `failed` with the Status text, never skipped silently.
Status checkCatalogue(const FormationTable& table, const heightfield::HeightGrid& g, const PlacementColumns& cols,
                      const std::vector<CatalogueUnit>& units, float waterLevel, const CheckTolerances& tol,
                      CatalogueReport& out);

// The report as text: one line per figure (tab-separated, with a header) and a summary block at the end.
std::string catalogueReportText(const CatalogueReport& r, const CheckTolerances& tol);

// Minimum-cost assignment for a square cost matrix (n x n, row-major): col[i] = the column of row i.
void minCostAssignment(const std::vector<double>& cost, size_t n, std::vector<size_t>& col);

}}  // namespace c5r::units
