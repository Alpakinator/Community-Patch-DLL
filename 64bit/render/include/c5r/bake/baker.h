// c5r::bake::TileBaker - one tile in, its colour caches t0 / t8 out: the contract between the bake pipeline (issue 3404,
// src/bake/) and its users (residency and bake-ahead in the server, issue 3407; the comparator, 3404).
//
// Written by the team-3400 lead before wave 2 (2026-11-13). Changes go through the lead. Behaviour: research/
// 05-graphics-composition/terrain-bake-records.md, terrain-bake-blend.md, terrain-bake-passes.md.
#pragma once

#include <cstdint>

#include "c5r/bake/bake_types.h"
#include "c5r/status.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;

namespace c5r {
namespace heightfield {
struct HeightGrid;
}
namespace decal {
struct TileDecalList;
}
namespace bake {

// What the server knows about one tile when it asks for a bake.
// Where the caller wants the result (2026-11-13, wave 3, from 3407: one texture per cache costs about 64 KB of video
// memory each, 627 MB for the huge map against 413 MB of BC1). When an array is given, the baker copies its result into
// that slice (CopySubresourceRegion, all mips) and returns no texture of its own for that cache.
struct BakeTarget {
    ID3D11Texture2D* array = nullptr;  // BC1_TYPELESS or BC1_UNORM array of sizeClass^2, cacheMips(sizeClass) mips; not owned
    uint32_t slice = 0;
};

struct BakeRequest {
    TileId tile;
    uint32_t sizeClass = kCacheSizeDefault;  // 64 / 128 / 256 / 512 (mips 4 / 5 / 6 / 7)
    uint64_t contentGeneration = 0;          // the server's counter for this tile's inputs; echoed into BakedTile
    // The EXE's tile+0x300 ("area changed since load", set by the rectangle invalidation 0x4387D0): the EXE then makes
    // t8 even when no listed layer has a spec texture (3404, terrain-bake-comparison.md 6). A server that re-bakes a tile
    // because its content changed sets this.
    bool areaChanged = false;
    BakeTarget t0Target, t8Target;           // optional; see BakeTarget
    // The tile's decals in draw order (c5r::decal::decalsForTile; epic 0166, issue 4105), or null. Null or an empty list:
    // the bake is the same as without decals, bit for bit. Not owned; it (and its set) must outlive the bake call.
    const decal::TileDecalList* decals = nullptr;
    // Issue 0371: the EXE's state-5 bake. When nearCache[0] is given, the bake also draws CacheBake into the near pair
    // (c5r/bake/post.h PostInputs::nearCache: createNearCache's kind, 64 / 128 / 256 / 512, the full chain) before its BC1
    // path; nearCache[1] must be given exactly when the bake makes t8 (else BadLayout before anything is drawn). Null
    // (the default): the bake is the same as without this field, bit for bit. Not owned.
    ID3D11Texture2D* nearCache[2] = {nullptr, nullptr};
};

// The result. Textures are owned by the baker's caller once returned (AddRef'd, the caller Releases them).
struct BakedTile {
    TileId tile;
    uint32_t sizeClass = 0;
    uint64_t contentGeneration = 0;
    ID3D11Texture2D* t0 = nullptr;  // BC1 (UNORM view), sizeClass^2, cacheMips(sizeClass) mips; null when t0Target was used
    ID3D11Texture2D* t8 = nullptr;  // same format; null when no t8 is made (tile+0x2FC = 0 and not areaChanged) or t8Target was used
    bool hasT8 = false;             // a t8 was made (returned or copied into t8Target)
    float gpuMs = 0;                // GPU time of this bake (timestamp queries); must be filled: residency budgets by it
};

// Inputs shared by every tile of a map; set once after load and again when they change.
struct MapInputs {
    const heightfield::HeightGrid* grid = nullptr;  // heights + material bytes (the tap), not owned
    const LayerTables* tables = nullptr;            // from TerrainDef.csv, not owned
    // The style is per tile: the lowest set bit 1..4 of tile+0x138, the OR of the tile's plots' continent art types
    // (3406, terrain-bake-passes.md 5.3; c5r::bake::tileStyle). When plotArtTypes is given the baker uses it; else every
    // tile gets `style`.
    const uint8_t* plotArtTypes = nullptr;          // plotsX * plotsY continent art types, row-major, not owned
    uint32_t plotsX = 0, plotsY = 0;
    bool wrapX = true;
    uint32_t style = 0;                             // fallback for every tile (Sunlight / Shadowlight / ColorKey set)
};

class TileBaker {
public:
    virtual ~TileBaker() {}
    // Device and immediate context are the caller's; the baker creates its own resources on init.
    virtual Status init(ID3D11Device* dev, ID3D11DeviceContext* ctx) = 0;
    virtual Status setMap(const MapInputs& in) = 0;
    // Records the bake on ctx. NotFound for a tile outside the map, BadLayout for a bad size class. Decals (roads,
    // farms: bake stage 2) are drawn from req.decals (epic 0166); outputs stay null on failure.
    virtual Status bake(const BakeRequest& req, BakedTile& out) = 0;
};

}  // namespace bake
}  // namespace c5r
