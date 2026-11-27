// c5r/farmfields/buildings.h - the farm buildings (barns, silos) as records: one per farm plot, placed on the edge of the
// server's own fields (ticket 0653, decision D230; part B, ticket 0654, draws them). Pure CPU, no D3D. Written 2026-11-21
// for this repository (GPL-3.0-or-later).
//
// Why: with [Renderer] ServerFarmFields = 1 the EXE's farm builder never runs, and the EXE then makes no farm building
// (research/02-exe-internals/farm-field-double-free.md 4.4, 4.7). The server makes them itself (route S-F there).
//
// What the EXE does, as the knowledge base describes it (D83: own code from the description, nothing from the EXE;
// research/05-graphics-composition/cities-and-landmarks.md 2.5, 2.6, farm-fields-layout.md 4.2):
//   * one farm record per farm plot; its building row = a choice among the FARM rows of ArtDefine_Landmarks for the
//     farm's era level (improvement ART_DEF_IMPROVEMENT_FARM, no resource, state Any, at most 3 per level);
//   * the era level from the owner's era: 0-2 -> 1, 3 -> 2, 4 -> 3, more -> 4; a level without rows falls back to the next
//     lower level that has some;
//   * the placer (ticket 0657, cities-and-landmarks.md 2.6.2 (1), corrected 2026-11-23; was "move to the nearest point on
//     them"): start at the plot centre; among the record's field curves whose value F = 0.5 x sum x_prev (y - y_prev) has
//     |F| >= 100, the one whose area-weighted centroid is strictly nearest the plot centre gives the position: that centroid
//     (none: the plot centre); the facing = the direction field at that point clamped into the map, plus a random quarter
//     turn; z = the terrain height (the server reads it at the clamped point); the position itself is not clamped.
// What the server does differently (each a choice of ticket 0653, research farm-fields-layout.md 16):
//   * the row and the quarter turn are chosen by a hash of the game's seed and the plot (plotSeed), not by the process's
//     rand(): the same on every load of a save, different between plots (D194: a plausible variant is enough);
//   * the curves are the server's own field outlines of that plot (the producer's FarmLayout), not the EXE's; F and the
//     centroid are computed in double in L (the EXE: float in its world frame, which is L shifted when sx = 1, sy = 0.866);
//   * ticket 0657: with ServerFarmFields = 1 the EXE's own placer puts the building at the point chosen here (the patch
//     hands it as a transient curve, farm_guard.h), so the EXE's instance and this record agree;
//   * the height comes from a height field when one is given (heightfield::heightAtSample at the point), else the
//     record says it has none (hasHeight false) and the drawer samples it.
//
// Frames: positions are in the decals' sample frame (the height field's samples: plotCentre()); distances are measured in
// layout units (L, y / kLayoutYScale), where the plot grid is isotropic, as the farm manager measures in world units
// [I: L = world up to the field's sx = 1, sy = 0.866]. The facing is an angle about +z, counter-clockwise from +x in L
// (= world) [I: the model's own forward axis is the drawer's business].
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "c5r/farmfields/farmfields.h"
#include "c5r/status.h"

namespace c5r {
namespace heightfield { struct HeightGrid; }
namespace farmfields {

// ---- era levels ----
const uint32_t kFarmEraLevels = 5;        // 0 (a row of Era "Any") .. 4
const uint32_t kFarmRowsPerLevel = 3;     // the farm manager's table holds 3 slots per level
// The level of a game era (EraTypes index: 0 Ancient .. 7 Future): 0-2 -> 1, 3 -> 2, 4 -> 3, >= 5 -> 4; < 0 (no owner,
// owner's era unknown) -> 1 (MC, ticket 0653: a farm without an owner takes level 1).
uint8_t farmEraLevel(int32_t gameEra);
// The game era index of an ArtDefine_Landmarks Era text: Ancient 0, Classical 1, Medieval 2, Renaissance 3, Industrial 4,
// Modern 5, Postmodern / Atomic 6, Future / Information 7 (compared without case); "Any" or empty -> -1; anything else -2.
int32_t landmarkEraIndex(const std::string& era);
// The level of a FARM row from its Era text: Any / empty -> 0, else farmEraLevel(landmarkEraIndex); -1 for an unknown
// text [I: that the EXE maps a row's era through the same table as the farm's; Ancient -> 1 and Industrial -> 3 put the
// Medieval_Farm rows at level 1 and the Modern_Farm rows at level 3, so levels 2 and 4 fall back to them].
int32_t farmRowLevel(const std::string& era);

// ---- the rows the buildings choose from ----
// Filled from the model library (c5r::landmarks::ModelLibrary::farmRows) by the server; tests by hand. A row id is an
// index into the library's landmarkRows(); only rows the library could lay out are listed.
struct FarmRowRefusal {
    int32_t row = -1;          // the library's row index
    std::string text;          // names the row and why ("row 412 (ART_DEF_IMPROVEMENT_FARM / ... / Modern_Farm2.fxsxml):
                               // its model file was refused: ...")
};
struct FarmRowChoice {
    std::vector<int32_t> level[kFarmEraLevels];   // per level, at most kFarmRowsPerLevel row ids, in the table's order
    std::vector<FarmRowRefusal> refused;          // farm-building rows that cannot be drawn (D118: one LOUD line each)
    uint32_t farmHandlerRows = 0;                  // rows with LayoutHandler FARM (the 18 resource rows included)
    bool any() const;
    bool operator==(const FarmRowChoice& o) const;
    bool operator!=(const FarmRowChoice& o) const { return !(*this == o); }
};

// ---- the record ----
struct FarmBuilding {
    uint32_t plot = 0;               // index into the LayoutInput::plots it was made from
    int32_t x = 0, y = 0;            // the farm plot (the DLL's coordinates)
    int8_t owner = -1;               // the plot's owner (PlayerTypes), -1 none
    int32_t era = -1;                // the owner's era (EraTypes), -1 unknown or no owner
    uint8_t eraLevel = 1;            // farmEraLevel(era): the level asked for
    uint8_t rowLevel = 0xFF;         // the level the row came from (<= eraLevel, the fallback), 0xFF: no row
    int32_t row = -1;                // the ArtDefine_Landmarks row (the library's landmarkRows() index), -1: none at this
                                     // level or below (the drawer draws nothing for it and counts it, D118)
    uint32_t pick = 0;               // the seeded choice: row = rows[rowLevel][pick % count]
    FarmState state = FarmState::Built;   // the plot's state (FarmPlot::state)
    bool pillaged = false;           // state == Pillaged
    int32_t field = -1;              // the field whose centroid is the position (index into FarmLayout::fields), -1: no field
                                     // of the plot has |F| >= kStockFieldMinF (the building stands at the plot centre, as
                                     // the EXE's placer does for a record without such a curve)
    float pos[2] = {0, 0};           // x, y in the sample frame (not clamped, as the EXE's placer; ticket 0657)
    float distance = 0;              // L from the plot centre to pos (0 without a field)
    float z = 0;                     // the terrain height at pos (world z), valid when hasHeight
    bool hasHeight = false;
    uint8_t quarter = 0;             // the seeded quarter turn, 0..3
    float facing = 0;                // radians in [0, 2 pi): fieldAngle at pos + quarter x pi / 2
    bool operator==(const FarmBuilding& o) const;
    bool operator!=(const FarmBuilding& o) const { return !(*this == o); }
};

// The per-plot facts the layout input does not carry (the server's plot table and player rows).
struct BuildingFacts {
    std::vector<int8_t> owner;       // per LayoutInput::plots (same order); empty: every owner -1
    int32_t eraOfPlayer[64];         // EraTypes per player, -1 unknown
    BuildingFacts();
};

// The seeds of one plot's building (no state: same input, same values). pick: the row choice; quarter: 0..3.
uint32_t buildingPick(uint32_t gameSeed, int32_t x, int32_t y);
uint8_t buildingQuarter(uint32_t gameSeed, int32_t x, int32_t y);

// Ticket 0657: the stock placer's rule (cities-and-landmarks.md 2.6.2 (1)). F of a closed corner loop in L:
// 0.5 x sum over the corners of x_prev x (y - y_prev) (the previous corner of the first is the last); for an axis-aligned
// rectangle 0.5 x its area, in general not the area. A field takes part when |F| >= kStockFieldMinF.
const double kStockFieldMinF = 100.0;
double stockFieldF(const FieldPolygon& f);
// The area-weighted centroid of the corner loop in L; false for a loop of zero area (or fewer than 3 corners).
bool stockFieldCentroid(const FieldPolygon& f, double out[2]);
// Among the given fields with |F| >= kStockFieldMinF, the centroid strictly nearest (px, py) (L; the first wins a tie), in
// the sample frame; false when no field takes part (the EXE's placer then keeps the plot centre). `field` gets the field's
// index in `fields`, `dist` the distance in L.
bool stockPlacement(const std::vector<FieldPolygon>& fields, const std::vector<uint32_t>& which, double px, double py,
                    float out[2], int32_t& field, double& dist);

// The nearest point to (px, py) (L) on the outlines of the given fields (corners in the sample frame), in the sample
// frame; false when no field has an outline (fewer than 2 corners). `field` gets the field's index in `fields`, `dist`
// the distance in L. Ticket 0653's rule, NOT the EXE's (0657): placeBuildings uses stockPlacement.
bool nearestOnOutlines(const std::vector<FieldPolygon>& fields, const std::vector<uint32_t>& which, double px, double py,
                       float out[2], int32_t& field, double& dist);

// One record per farm plot of `in` (in its order: ascending by y then x), from the layout made from that same input
// (Producer::layout() with Producer::input()). height: optional (null: hasHeight false). Checks: checkInput's; a layout
// field whose plot is outside the input; facts.owner neither empty nor one per plot; a row id below -1 in `rows`
// [BadLayout]. On failure `out` is empty.
Status placeBuildings(const LayoutInput& in, const FarmLayout& layout, const BuildingFacts& facts, const FarmRowChoice& rows,
                      const heightfield::HeightGrid* height, std::vector<FarmBuilding>& out);

// The record as one line (tests, logs): "x,y o=<owner> e=<era> L<eraLevel>/<rowLevel> row=<row> st=<state> f=<field>
// pos=<x>,<y> z=<z|-> q=<quarter> a=<facing>".
std::string buildingText(const FarmBuilding& b);

}}  // namespace c5r::farmfields
