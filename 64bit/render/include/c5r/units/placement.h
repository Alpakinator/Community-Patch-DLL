// c5r/units/placement.h - L3 of the unit module: where each figure of a unit stands (epic 0041, U3, issue 4354).
//
// Design: research/05-graphics-composition/unit-renderer.md 6.4 (results in its section 12). The rule is the EXE's
// formation code as research/05-graphics-composition/unit-formations.md describes it (sections 2, 3, 7, 11.2, 12),
// written in our own words from that description (D83): UnitFormations.xml read from the install at run time, the
// slot rule (rows, centre-out order, bend, the random part seeded by the unit's art record index, centring), the
// facing's yaw, the root, dead members left out, heights from the EXE's height field (C5HF) with no tilt.
//
// Failure behaviour (unit-renderer.md 7): every function returns a c5r::Status; the module's prepare step (4352)
// turns any failed placement Status into the unit status PlacementFailed with the Status text, and does not draw the
// unit. Codes: NotFound = a formation name not in the table, or no UnitFormations.xml in the install; BadLayout = an
// input out of range (plot outside the height field, figure count, alive count, sizes, non-finite numbers); Io for a
// file that cannot be read.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "c5r/status.h"

namespace c5m { class TableSource; }

namespace c5r {
namespace gamefs { class GameFiles; }
namespace heightfield { struct HeightGrid; }
namespace units {

// ---- The formation table (UnitFormations.xml) ----

// One <Formation> record (unit-formations.md 1, 4). Defaults are the EXE's built-in "Default" record; a field the
// file does not give keeps its default.
struct FormationRecord {
    std::string name;
    int32_t spacingX = 6, spacingY = 7;
    float spacingMultiplier = 1.0f;
    float unitRandomness = 0.2f;
    float blockRandomness = 0.2f;     // read, not used by the slot rule (unit-formations.md 3.4)
    int32_t perRowInc = 0;
    float yOffsetInc = 1.2f;
    int32_t yOffsetCurved = 1;
    int32_t stagger = 0;
    int32_t totalWidth = 4;
    int32_t xBlockSize = -1, xBlockSpacingCount = 0;
    int32_t yBlockSize = -1, yBlockSpacingCount = 0;
    int32_t centerWidth = 0;
};

class FormationTable {
public:
    // Parses a UnitFormations.xml text. The table always starts with the built-in "Default" record; each <Formation>
    // adds one record (a later one with the same name replaces the earlier one [I]). Unknown child elements are
    // ignored and listed in warnings() [I: the EXE reads the known names only]. Checks: not XML or root not
    // <Formations> [BadLayout]; a <Formation> without <Name> [BadLayout]; a known field that is not a number
    // [BadLayout]; no <Formation> at all [BadLayout]. On failure the table is left empty (size() 0, find() nullptr).
    Status parse(const std::string& xml);
    // Reads the formation file the EXE uses: the loose <installRoot>/Assets/Units/UnitFormations.xml when it exists
    // (the EXE opens that path; the stock install also holds an older 21-entry copy in UnitModels.fpk, which the
    // file system's name lookup returns instead, and the catalogue's units use formations only the loose 24-entry
    // file has [L, unit-renderer.md 12]); else unitformations.xml through the file system [I]. The same for
    // UnitVisConfig.ini's [Debugging] GlobalUnitScale (percent, default 100). NotFound if neither has the XML.
    Status load(const gamefs::GameFiles& files, const std::wstring& installRoot);
    const std::string& origin() const { return origin_; }   // where the XML came from
    // "" -> the built-in Default (the EXE's rule for an empty or NULL ArtDefine_UnitInfos.Formation); a non-empty
    // name not in the table -> nullptr (the caller reports PlacementFailed; the EXE itself would fall back to Default,
    // unit-formations.md 2, but no stock or VP art row names an unknown formation).
    const FormationRecord* find(const std::string& name) const;
    size_t size() const { return records_.empty() ? 0 : records_.size() - 1; }   // records from the file
    float unitScale() const { return unitScale_; }
    void setUnitScale(float s) { unitScale_ = s; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    const std::vector<FormationRecord>& records() const { return records_; }    // [0] = Default once loaded

private:
    std::vector<FormationRecord> records_;
    std::vector<std::string> warnings_;
    std::string origin_;
    float unitScale_ = 1.0f;
};

// The two database columns placement needs that c5m::ArtTables does not load: ArtDefine_UnitInfos.Formation by art
// row Type (NULL -> "") and ArtDefine_UnitMemberInfos.ZOffset by member Type (NULL -> 0). Checks: a table or column
// missing [NotFound]; a ZOffset that is not a finite number [BadLayout].
struct PlacementColumns {
    std::map<std::string, std::string> formation;   // ArtDefine_UnitInfos.Type -> Formation
    std::map<std::string, float> zOffset;           // ArtDefine_UnitMemberInfos.Type -> ZOffset
};
Status loadPlacementColumns(c5m::TableSource& db, PlacementColumns& out);

// ---- The slot rule and its pieces (unit-formations.md 3) ----

struct Slot {
    float x = 0, y = 0;   // slot frame: x across the rows, y along the files (front row y = 0 before centring)
};

// The generator of the slot rule's random part (unit-formations.md 3.4).
struct FormationRng {
    uint32_t state = 0;
    float next();         // the next u in [0, 1)
};

// The n slots of a record for n members (all members, living or dead: unit-formations.md 7), centred on their
// bounding box taken together with the origin (3.5). seed: the generator is reset to it; withRandom false gives the
// deterministic grid (comparison only). unitScale multiplies the spacings (the table's GlobalUnitScale).
// Checks: n 0 or above kMaxFigures [BadLayout].
const uint32_t kMaxFigures = 64;   // unit-renderer.md 3's limit per unit
Status formationSlots(const FormationRecord& rec, uint32_t n, uint32_t seed, bool withRandom, float unitScale,
                      std::vector<Slot>& out);

// The unit's yaw in radians for a DLL DirectionTypes value: 150 - 60 d degrees (mod 360) for d 0..5; any other value
// gives 1.0 rad, as the EXE's switch does (unit-formations.md 11.2).
float facingYaw(int facing);

// Number of living figures out of n (unit-formations.md 7): alive >= 0 overrides; else the dead count is
// round(n x percent / 100) with percent = damage x 100 / maxHp (integer, [I]), at most n - 1 below 100 percent.
// Members die from the last backwards, so figures 0 .. living-1 live. Checks: n above kMaxFigures, alive above n,
// maxHp <= 0 with alive -1, damage negative [BadLayout].
Status livingFigures(uint32_t n, int16_t damage, int16_t maxHp, int16_t alive, uint32_t& living);

// The plot centre as the EXE's HexToWorld computes it (0x6851C0: (hx, hy, 0, 1) x M_h2w, then x sx - ox,
// y sy - oy; hx = x - (y >> 1), hy = y): the tap's hexToWorld matrix, not the nominal 64 / 32 lattice. This is the
// 0.05-0.12-unit "common shift" of unit-formations.md 12.3 (premise P6; unit-renderer.md 12).
// Checks: the grid has no samples or an all-zero hexToWorld [BadLayout]; the plot outside 0 <= x < mapW,
// 0 <= y < mapH - 4 (the DLL's map; mapH counts 4 margin rows) [BadLayout].
Status plotCentre(const heightfield::HeightGrid& g, int32_t plotX, int32_t plotY, float& x, float& y);

// HeightAtWorld 0x530A10 as unit-formations.md 12.4 describes it: column (x + ox) / sx and row (y + oy) / sy, each
// truncated toward zero, then the sample's height (wraps x on a wrapping map, 0 outside the grid).
float heightAtWorld(const heightfield::HeightGrid& g, float x, float y);

// ---- One unit ----

struct PlacementInput {
    std::string formation;            // ArtDefine_UnitInfos.Formation of the unit's art row ("" = Default)
    std::vector<float> zOffset;       // per figure in member order: ArtDefine_UnitMemberInfos.ZOffset (NULL = 0)
    int32_t plotX = 0, plotY = 0;
    bool hasRoot = false;             // true: rootX/Y (position source exe); false: the plot centre (source own)
    float rootX = 0, rootY = 0;
    int facing = 2;                   // DirectionTypes
    uint32_t seed = 0;                // UnitInput::formation_seed (the EXE's art record index, unit-formations.md 12.1)
    int16_t damage = 0, maxHp = 100, alive = -1;
    float waterLevel = 0;             // UnitScene::water_level: the floor of every figure's height
    float footRadius = 0;             // issue 4982 (FixUnitPlacement, option 23): > 0 and the figure's own sample above the
                                      // water: the figure stands on the highest ground within this radius of its origin
                                      // (footBase); 0 = the EXE's rule, byte-equal. Checks: not finite, < 0 or > 16 [BadLayout]
};

struct FigurePlacement {
    uint16_t figure = 0;              // index in member order
    uint16_t slot = 0;                // slot taken (= figure for survivors: the dead are the last ones)
    float x = 0, y = 0, z = 0;        // the figure's model origin in world units (T of unit-playback.md 1.1)
    float yaw = 0;                    // radians about +z; no tilt
};

struct UnitPlacement {
    float rootX = 0, rootY = 0;       // the unit's position the slots were moved to
    float yaw = 0;
    uint32_t figures = 0;             // all members
    uint32_t living = 0;
    std::vector<FigurePlacement> placed;   // living figures only, in member order; the dead are not drawn
};

// L3 for one unit: the record by name, the slots of all members with the unit's seed, rotated by the facing's yaw,
// moved to the root; each living figure takes the next slot; z = max(heightAtWorld at the slot, waterLevel) +
// zOffset (no tilt; with footRadius > 0 on land, the highest ground within footRadius instead, issue 4982). Checks: those of find (NotFound), plotCentre, livingFigures and formationSlots; zOffset empty
// or above kMaxFigures [BadLayout]; a non-finite root, zOffset or waterLevel [BadLayout]. The plot is checked in both
// position sources. On failure `out` is cleared.
Status placeUnit(const FormationTable& table, const heightfield::HeightGrid& g, const PlacementInput& in,
                 UnitPlacement& out);

// ---- Figure offsets (epic 0042, issue 4776: a fight's figure motion; unit-combat-server.md "Figure motion (4776)") ----

// One figure moved away from its slot: dx across the rows, dy along the files, in the unit's own frame (the slot frame
// of formationSlots, before the facing's yaw); yaw the figure's own heading in radians about +z when hasYaw.
struct FigureOffset {
    uint16_t figure = 0;              // index in member order (FigurePlacement::figure)
    float dx = 0, dy = 0;
    bool hasYaw = false;
    float yaw = 0;
};

// Moves each listed figure of `p` (placed by placeUnit) by its offset rotated by the unit's yaw (p.yaw); its height is
// sampled again at the new spot, keeping the figure's ZOffset part (z - max(h at the old spot, waterLevel) + max(h at
// the new spot, waterLevel)); its yaw becomes the offset's when hasYaw. Figures not listed do not change, so an empty
// list changes nothing. Checks: an offset naming a figure that is not placed, or a value that is not finite
// [BadLayout]; on failure `p` is left unchanged.
Status applyFigureOffsets(const heightfield::HeightGrid& g, float waterLevel, const std::vector<FigureOffset>& offs,
                          UnitPlacement& p);

}}  // namespace c5r::units
