// c5r/bake/layers.h - bake stage 1, issue 3401: the texture-set tables from TerrainDef.csv and the per-tile layer
// lists, computed in the server from the tapped height field (heights + material bytes).
//
// Behaviour follows research/05-graphics-composition/terrain-bake-records.md section 2.2 (list builder 0x568430,
// normal grid 0x747580, CSV loader 0x61BAE0), written from a static reading in words and tables (D83: no decompiler
// output in this repository). The EXE quirks are kept on purpose so that the lists equal the EXE's bit for bit:
// y wraps in the list window, the normal of sample (x+2, y+2) is used for sample (x, y), the slope-projected count
// reaches into the next set's ids, the normal grid's plane prediction steps by 1.0.
//
// Every function returns a Status and never throws; each check names the Err a negative test asserts.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "c5r/bake/bake_types.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/status.h"

namespace c5r {
namespace gamefs { class GameFiles; }
namespace bake {

// Limits of the EXE's tables at TR8 (2.3): 64 texture sets (+0x708.. +0x908 u32 each), 64 layer ids (+0xF10, +0x1010).
constexpr uint32_t kMaxTextureSets = 64;
constexpr uint32_t kMaxLayerIds = 64;
// The CSV's columns (2.2b): continent, diffuse, heightmap, specmap, base dominance, texture scale, projection, id.
constexpr uint32_t kTerrainDefColumns = 8;
// The set the EXE appends after the last CSV row (2.2b) [C].
extern const char* const kAppendedSetTexture;  // "Checkerboard.dds"
constexpr float kAppendedSetScale = 0.01f;
// The slope threshold of the list builder (2.2 step 4): n.z <= 0.9 moves a slope-projected layer.
constexpr float kSlopeThreshold = 0.9f;
// The tile's normal grid (2.2a): 69 x 69 entries, entry (i, k) = normal of sample (ox+i, oy+k).
constexpr uint32_t kNormalGrid = 69;

// Parses the text of TerrainDef.csv into out.sets (one per data row, then the appended Checkerboard set) and
// computes the TR8 tables (layerCount, firstLayerOfMaterial, slopeProjected, setOfLayer, projectionOfLayer).
// The first line is the header and is skipped; empty lines (also "\r") are skipped; fields are comma-separated and
// trimmed; NULL in the specmap column becomes "". The id column is hexadecimal as in the EXE.
// Checks: no data row [BadLayout]; a row with fewer than kTerrainDefColumns fields [BadLayout]; a number that does not
// parse completely (continent, base dominance, scale, projection, id) [BadLayout]; projection not 0 or 1
// [Unsupported]; an empty diffuse or heightmap name [BadLayout]; more than kMaxTextureSets sets including the
// appended one, or more than kMaxLayerIds layer ids [BadLayout] ("too many rows").
Status parseTerrainDef(const std::string& text, LayerTables& out);

// Reads the TerrainDef.csv the EXE uses through c5r::gamefs and parses it. The EXE's live tables equal the loose file
// <install>/Assets/DLC/Shared/Terrain/TerrainDef.csv [L, terrain-bake-records.md 2.4]; since issue 3404 gamefs mounts
// the package folders as the EXE's "Common" kind does (asset-pipeline.md 1.2), so the bare name resolves to that file
// and not to the older copy in Resource/Common/TerrainModels.fpk.
// Checks: not found [NotFound]; the read fails [Io]; then those of parseTerrainDef. origin receives the file's origin
// (loose path relative to the install, or pack:name); cleared on NotFound.
extern const char* const kSharedTerrainDef;  // "Assets/DLC/Shared/Terrain/TerrainDef.csv", the origin expected
Status loadTerrainDef(const gamefs::GameFiles& fs, LayerTables& out, std::string* origin = nullptr);
// The signature of 3401, kept so that callers compile: installRoot is ignored since issue 3404 (the read goes through
// fs). New code calls the overload above.
Status loadTerrainDef(const gamefs::GameFiles& fs, const std::wstring& installRoot, LayerTables& out,
                      std::string* origin = nullptr);

// Checks a HeightGrid for the list builder: samplesX/Y 0 or not mapW/mapH * 64 [BadLayout]; h.size() !=
// samplesX * samplesY [BadLayout] ("grid size mismatch"); mat empty [NotFound] (the byte layer was not tapped);
// mat.size() != h.size() [BadLayout]; sx, sy, sz not finite [BadLayout].
Status checkGrid(const heightfield::HeightGrid& g);

// The tile's window as the EXE's tile object holds it (2.1): origin = (x, y) * 64, extent 65, or 64 in the last
// column / row. tilesX = mapW, tilesY = mapH. Checks: tile outside the grid [BadLayout].
struct TileWindow {
    uint32_t ox = 0, oy = 0, ex = 0, ey = 0;
};
Status tileWindow(const heightfield::HeightGrid& g, TileId t, TileWindow& out);

// The tile's 69 x 69 normal grid (2.2a): out gets kNormalGrid * kNormalGrid float3, row-major (k * 69 + i).
// Checks: those of checkGrid except that mat is not needed (only h); the origin outside the grid [BadLayout].
Status normalGrid(const heightfield::HeightGrid& g, uint32_t ox, uint32_t oy, std::vector<float>& out);

// What the list builder saw, for the checker's explanations.
struct LayerListDetail {
    uint32_t counts[128] = {};     // the EXE's counters
    uint32_t samples = 0;          // samples in the window
    uint32_t slopeSamples = 0;     // samples of slope-projected materials
    uint32_t moved1 = 0, moved2 = 0;  // of those, moved by +1 / +2
    float closestNz = 2.0f;        // the slope sample's n.z closest to the threshold (2 = none)
    uint32_t materials[256] = {};  // material byte histogram over the window
    bool sorted = false;           // more than 8 ids: sorted by count and cut
};

// The tile's layer list (2.2 steps 1-6). Checks: those of checkGrid and tileWindow; tables.layerCount 0 or above
// kMaxLayerIds [BadLayout]; a material byte b in the window with b >= tables.sets.size() (no texture set) [NotFound]
// (the EXE would read layer 0: see 2.2 step 2); a layer id + 2 past the 128 counters [BadLayout].
Status buildLayerList(const LayerTables& tables, const heightfield::HeightGrid& g, TileId t, LayerList& out,
                      LayerListDetail* detail = nullptr);

// Every tile's list in the EXE's grid order (column-major: index x * tilesY + y). Checks: those of buildLayerList,
// for the first tile that fails.
Status buildAllLayerLists(const LayerTables& tables, const heightfield::HeightGrid& g, std::vector<LayerList>& out);

// tile+0x2FC (2.2 step 7): true when the switch is on and (the tile flag +0x300 is set or a listed layer's set has a
// spec texture). Layer ids past setOfLayer are ignored.
bool needsSecondCache(const LayerTables& tables, const LayerList& list, bool globalSwitch, bool tileFlag300);

}  // namespace bake
}  // namespace c5r
