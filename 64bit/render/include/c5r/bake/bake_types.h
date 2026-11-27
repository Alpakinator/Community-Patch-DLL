// c5r::bake - shared types of bake stage 1 (epic 3400): the GPU colour stage of the terrain bake, run in the server.
//
// Contract written by the team-3400 lead before wave 1 (2026-11-13) so that the three wave-1 workers can build against
// one interface. Behaviour is described in research/05-graphics-composition/terrain-bake-records.md (D83: no decompiler
// output in this repository; algorithms are re-implemented from the knowledge base's description).
//
// Ownership of the modules that implement this contract (each module has its own folder and CMakeLists.txt):
//   src/bakelayers/  + tests/bakelayers/  issue 3401: TerrainDef.csv -> LayerTables, HeightGrid -> LayerList per tile
//   src/bakeblend/   + tests/bakeblend/   issue 3402: per-vertex weights (BlendVertex) and the blend pass
//   src/bakepost/    + tests/bakepost/    issue 3403: the passes after the blend, as far as t0 / t8 (BC1)
// Changes to this header go through the lead (one writer).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace c5r {
namespace bake {

// One tile = one plot = 64 x 64 height samples (terrain-rendering-inventory.md 5.1).
constexpr uint32_t kTileSamples = 64;
// The list builder reads the tile plus a margin of 2 samples on every side (terrain-bake-records.md 2.2).
constexpr uint32_t kListMargin = 2;
// At most 8 layers per tile, after the sort by count (2.2 step 6).
constexpr uint32_t kMaxLayers = 8;
// The blend pass's grid: 67 x 67 vertices, 66 x 66 quads (section 4).
constexpr uint32_t kBlendGrid = 67;
// Size of the blend targets (three R16G16B16A16_FLOAT) and of the colour caches t0 / t8 (BC1, 256 x 256, 6 mips at the
// quality of the reference runs; the size class can be 64/128/256/512, tile+0x17C).
constexpr uint32_t kBlendTargetSize = 512;
constexpr uint32_t kCacheSizeDefault = 256;

// One row of TerrainDef.csv = texture set k (terrain-bake-records.md 2.3). Read from the player's install at run time.
struct TextureSet {
    std::string continent;    // continent id column
    std::string diffuse;      // file names as written in the CSV
    std::string heightmap;
    std::string specmap;      // empty for NULL
    float baseDominance = 0;  // "base dominance" column
    float textureScale = 0;   // equals TR8+0xE10[k]
    int projection = 0;       // 1 = slope-projected (three layers: top-down and two side projections)
    int id = 0;               // id column
};

// The global tables the EXE keeps at TR8, computed from the TextureSets (2.3).
struct LayerTables {
    uint32_t layerCount = 0;               // TR8+0x1110 (48 with the VP install of 2026-11-12)
    uint8_t firstLayerOfMaterial[256] = {}; // TR8+0xC10: material byte -> first layer id
    uint8_t slopeProjected[256] = {};       // TR8+0xB10: 1 when the material's set is slope-projected
    std::vector<uint32_t> setOfLayer;       // TR8+0xF10: layer -> texture set k
    std::vector<uint32_t> projectionOfLayer;// TR8+0x1010: 0 top-down, 1 and 2 the side projections
    // Rows of the loose Assets/DLC/Shared/Terrain/TerrainDef.csv (not the older copy in TerrainModels.fpk), index k,
    // plus the set the EXE's loader appends after the last row (Checkerboard.dds, scale 0.01, no CSV row): 42 sets and
    // 48 layers with the VP install of 2026-11-13 (terrain-bake-records.md 2.2b, issues 3401/3402). The id column is
    // hexadecimal.
    std::vector<TextureSet> sets;
};

// A tile's layer list as the EXE holds it at tile+0x1F8 (count) and tile+0x1FC (ids).
struct LayerList {
    uint32_t count = 0;
    uint32_t ids[kMaxLayers] = {};
};

// Tile coordinates: x in [0, tilesX), y in [0, tilesY); the EXE's grid index is column-major x * tilesY + y.
struct TileId {
    uint32_t x = 0, y = 0;
};

// One vertex of the blend pass's grid, 48 bytes, the layout of the EXE's vertex buffer (section 4 table).
#pragma pack(push, 1)
struct BlendVertex {
    float pos[3];        // POSITION: clip space, -1..1 in steps of 2/66
    float sample[2];     // TEXCOORD0: the sample coordinate
    float world[3];      // TEXCOORD1: world x, world y (= sample y * 0.866), height
    uint8_t weight[8];   // TEXCOORD2/3: 8 layer weights, R8G8B8A8_UNORM x 2
    uint8_t layer[8];    // BLENDINDICES0/1: 8 layer ids, R8G8B8A8_UINT x 2
};
#pragma pack(pop)
static_assert(sizeof(BlendVertex) == 48, "BlendVertex must match the EXE's 48-byte vertex");

}  // namespace bake
}  // namespace c5r
