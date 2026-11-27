// c5r/decal/decal.h - terrain decals in the server's tile bake: roads, railroads, farm fields, improvement and city
// grounds, the river bed (epic 0166; research/05-graphics-composition/terrain-decals.md sections 0.2, 4 and 6).
// Interface committed by lead-0166 before wave 2 (E2) with a stub; owners: 4103 (DecalSet, decalsForTile, the source)
// and 4104 (DecalPass). Each may extend its own part; the shapes below are what the pipeline (4105) takes.
//
// Behaviour (D83: from the knowledge base's description, no decompiler output): the EXE draws, between the blend and
// NormalCalculate, every decal whose rectangle overlaps the tile, in ascending priority, with TerrainDecalShader pass 1
// (the EXE's own VS/PS blobs read at run time) straight into the blend's three 512 RGBA16F targets (pass bit 0) or into
// the macro-shadow target (pass bit 1, tree shadows; none under D33). One draw per decal; its constant buffer holds the
// tile's DecalViewProj (registers 0-3), (1, 1, 0, 0) (4), the decal's rectangle (5) and its priority (6.x).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <d3d11.h>
#include "c5r/status.h"

namespace c5r { namespace shaders { class ShaderSection; } }
namespace c5r { namespace gamefs { class GameFiles; } }

namespace c5r { namespace decal {

constexpr uint32_t kNoTexture = 0xFFFFFFFFu;   // the EXE's handle 0: the engine's default texture (open question 7.3)

struct DecalVertex { uint16_t px, py, u, v; };  // the EXE's 8 bytes: half2 position in the decal's rect, half2 uv
static_assert(sizeof(DecalVertex) == 8, "vertex");

struct Decal {
    float rect[4] = {0, 0, 0, 0};   // min x, min y, max x, max y in height-field samples
    float priority = 0;             // the draw order key (ascending) and register 6.x
    uint32_t pass = 0;              // 0: the blend's three targets; 1: the macro-shadow target
    uint32_t texture[3] = {kNoTexture, kNoTexture, kNoTexture};   // albedo, height, spec: index into DecalSet::textures
    uint32_t firstVertex = 0;       // into DecalSet::vertices
    uint32_t vertexCount = 0;       // 3 x triangles
    uint32_t listOrder = 0;         // tie order of equal priorities
    uint64_t hash = 0;              // content hash (identity across generations: slots and offsets move in the EXE)
};

// ---- 4103: the tile query's square and the per-tile index (render/src/decal/decal_set.cpp) ----
// A tile is 64 x 64 height-field samples, its origin a multiple of 64. A decal belongs to tile (ox, oy) when its
// rectangle overlaps [ox + kTileQueryLo, ox + kTileQueryHi] x [oy + kTileQueryLo, oy + kTileQueryHi], inclusive on both
// ends (min <= hi and max >= lo). Why -2 and 66 (terrain-decals.md 10.2): the decal VS draws at rect + (1, 1) and the
// tile's DecalViewProj maps origin - 0.94 .. origin + 65.06 onto the 512 viewport (4.2), so in rectangle coordinates
// the tile sees origin - 1.94 .. origin + 64.06; the square is a superset of that and of the tile's 64-sample square,
// so no decal that can touch a texel is left out, and a decal it adds beyond that draws nothing (clipped).
constexpr int kTileSamples = 64;
constexpr int kTileQueryLo = -2;
constexpr int kTileQueryHi = 66;

// A uniform grid with one cell per tile: cell (cx, cy) lists, in draw order, every decal of the tile whose origin is
// (64 cx, 64 cy). Built by buildIndex from the decals (the reader builds it for every set it makes); empty = not built.
struct DecalGrid {
    int cellX0 = 0, cellY0 = 0;             // the first cell (tile origin / 64)
    int cols = 0, rows = 0;                 // 0: no grid, decalsForTile scans the whole set
    std::vector<uint32_t> start;            // cols * rows + 1 offsets into items (row-major, y then x)
    std::vector<uint32_t> items;            // decal indices
};

// 0366: the EXE's own draw order (terrain-decals.md 18). The small decals sit in the EXE's spatial index (a grid of
// cols x rows cells over the world from `origin`, `inverse` cells per sample; each cell a list, newest add first), the
// large ones (a side over kLargeDecal) in the large list (add order). From the tap's walk block, per decal: `cell` (cy *
// cols + cx, or kWalkLarge), `rank` (its place in the cell's list, or in the large list) and `point` (the index point,
// x then y). buildIndex fills cellStart / cellItems (each cell's decals by rank) and `large` (by rank). Empty (cols 0):
// the image carried no walk block; decalsForTile keeps the rule of 4111.
constexpr uint32_t kWalkLarge = 0xFFFFFFFFu;
struct DecalWalk {
    float origin[2] = {0, 0};
    float inverse[2] = {0, 0};
    uint32_t cols = 0, rows = 0;
    std::vector<uint32_t> cell;             // per decal
    std::vector<uint32_t> rank;             // per decal
    std::vector<float> point;               // per decal: x, y
    std::vector<uint32_t> cellStart;        // buildIndex: cols * rows + 1 offsets into cellItems
    std::vector<uint32_t> cellItems;        // buildIndex: decal indices, each cell's in rank order
    std::vector<uint32_t> large;            // buildIndex: the large decals in rank order
};

struct DecalRect { float v[4] = {0, 0, 0, 0}; };   // min x, min y, max x, max y (height-field samples)
struct TileOrigin { int x = 0, y = 0; };           // a tile's origin in height-field samples (multiples of 64)

// One generation of the EXE's decals, as the tap (or a .dtap file) delivered it; immutable once built.
struct DecalSet {
    uint32_t generation = 0;
    uint32_t loadSeq = 0;
    float worldRect[4] = {0, 0, 0, 0};
    std::vector<std::string> textures;      // file names, resolved through c5r::gamefs (packs, DLC, VT, mods)
    std::vector<Decal> decals;
    std::vector<DecalVertex> vertices;
    DecalGrid grid;                         // 4103: buildIndex(set); a set built by hand may leave it empty
    // 4111: the EXE's tile+0x300 ("area changed since load", terrain-decals.md 14) of every tile of its grid, from the
    // tap's area block: areaChanged[y * areaTilesX + x] is 0 or 1 for the tile with origin (64 x, 64 y). Empty (both 0):
    // the image carried none; the server keeps its own rule (a tile with decals counts as changed).
    uint32_t areaTilesX = 0, areaTilesY = 0;
    std::vector<uint8_t> areaChanged;
    DecalWalk walk;                         // 0366: the EXE's index and large list (empty: no walk block)
};

// 0366: the set carries a usable walk (cols, rows set, a cell / rank / point per decal and buildIndex's lists).
bool hasWalk(const DecalSet& set);

// The decals a tile draws, in draw order (with a walk, 0366: the EXE's order per pass; else ascending priority, equal
// priorities small decals newest first, then large decals in list order, 4111; see decalsForTile). `set` outlives the list.
struct TileDecalList {
    const DecalSet* set = nullptr;
    std::vector<uint32_t> indices;          // into set->decals
};

// The tile whose height-field origin is (originX, originY): every decal whose rectangle overlaps the tile's query square
// (kTileQueryLo..kTileQueryHi above, inclusive comparisons, as the EXE's draw tests), in draw order: ascending priority;
// equal priorities: the small decals (both sides at most kLargeDecal samples) first, newest (highest listOrder) first,
// then the large ones in listOrder (4111, from 4106's comparison: terrain-decals.md 13.4 and 14.2); then by index.
// With a grid and a 64-aligned origin: one cell's list, O(decals of the tile); else a scan of the whole set,
// O(n log n). A decal with a NaN rectangle is in no tile.
// 0366: when the set has a walk (hasWalk) and the origin is 64-aligned, the list is the EXE's own (terrain-decals.md
// 18.3) instead: per pass (0, 1) the decals the EXE's tile bake selects, in the order its draw sorts them, both passes
// merged by priority (pass 0 first on equal priority; each pass's own order kept). The selection: the tile rectangle
// [o - 1, o + e] per axis (e = 65, 64 in the last column of the world), the index's cells of that rectangle grown by
// 256, rows outer, columns inner, each cell's list by rank, a point inside [min - 256, max + 256) taken; a decal
// kept when its rectangle overlaps the tile rectangle (inclusive); then the large list by rank, the same test; then
// sorted by priority as the EXE's introsort does (exeSortByPriority). Every decal of this list lies inside the query
// square of the 4111 rule (the tile rectangle is inside it).
constexpr float kLargeDecal = 256.0f;       // the EXE's large-decal size (terrain-decals.md 1.4, 6.1 point 4)
TileDecalList decalsForTile(const DecalSet& set, int originX, int originY);

// 0366: one entry of the EXE's draw list and its sort. The EXE's sort (0x648890, terrain-decals.md 18.3) compares only
// the priority (float <) and is not stable above 32 entries; this reproduces it entry for entry (the 2008-era library
// introsort: insertion sort at 32 or fewer, the median-of-three or of-nine fat partition, the depth limit and heap sort).
struct ExeDrawEntry { uint32_t decal = 0; float priority = 0; };
void exeSortByPriority(ExeDrawEntry* first, ExeDrawEntry* last);
// 0366: the decals of one pass the EXE's draw selects for the tile at (originX, originY), in its walk's order, before the
// sort (empty without a walk). For tests and the comparator.
std::vector<ExeDrawEntry> exeSelection(const DecalSet& set, int originX, int originY, uint32_t pass);

// 4111: the EXE's tile+0x300 of tile (tileX, tileY) from the set's area block: `known` false when the set carries no
// block or the tile is outside it (the caller keeps its own rule), else the bit.
bool areaChangedAt(const DecalSet& set, int tileX, int tileY, bool& known);

// Builds set.grid over every decal (cost O(n log n + cell entries)). A set with a non-finite rectangle or one beyond
// +-2^20 samples gets no grid (decalsForTile scans).
void buildIndex(DecalSet& set);

// 0365 (terrain-decals.md 19): set's index from the previous generation's, when only some decals changed. newOf[j] is
// the index in `set` of prev's decal j when that decal is kept (its rectangle bit for bit the same), kDeltaGone when it
// was removed; `added` lists the set's decals that are no kept decal's image. The walk's lists are built as buildIndex
// does (O(n), from the per-decal fields); the grid keeps prev's cells: every cell's list is prev's with the kept indices
// renumbered and the removed ones dropped, plus the added decals of that cell, then sorted in draw order only when an
// added decal joined it or the renumbering left it out of order (equal priorities keep their tie by listOrder and index,
// which can move). The result equals buildIndex(set) exactly (same cells, same lists). It falls back to buildIndex's
// whole grid (returns false, `why` says why) when it cannot know that: prev has no grid, the mapping is not a one-to-one
// cover of set's decals, a kept decal's rectangle differs, a removed decal's cells reach the grid's edge (the span could
// shrink), or an added decal's reach past it or is not finite. Cost: O(cell entries + n) plus the sort of the changed cells.
constexpr uint32_t kDeltaGone = 0xFFFFFFFFu;
struct IndexDeltaStats {
    uint32_t cellsSorted = 0;               // cells sorted again (an added decal, or the renumbering moved a tie)
    uint32_t entriesAdded = 0;              // cell entries of the added decals
};
bool buildIndexFrom(DecalSet& set, const DecalSet& prev, const std::vector<uint32_t>& newOf,
                    const std::vector<uint32_t>& added, std::string* why = nullptr, IndexDeltaStats* stats = nullptr);

// The tiles whose query square overlaps any of the rectangles, limited to the tiles of worldRect (origins 64 x
// floor(worldRect min / 64) .. 64 x floor(worldRect max / 64)); sorted by y then x, unique. A rectangle with a NaN is
// skipped. These are the tiles a decal generation's changed rectangles re-bake (4105).
std::vector<TileOrigin> tilesForRects(const DecalRect* rects, size_t count, const float worldRect[4]);

// 0393 (terrain-decals.md 20): the tiles to re-bake when the tie rule changes between two applied sets, that is when
// the walk block appears or goes (hasWalk(prev) != hasWalk(next)): a tile baked under prev keeps prev's order of equal
// priorities (and prev's selection) until it is baked again, and a changed rectangle names only the decals that changed.
// Returns every tile of next's world (origins as tilesForRects gives them, sorted by y then x) whose draw list
// decalsForTile(prev) differs from decalsForTile(next) in pass 0 or in pass 1, each pass's entries taken in list order
// (the decal pass draws each pass into its own targets, so how the two interleave draws nothing different): another
// length, or at some place a decal of another content (hash, rectangle, priority, pass, vertex count). Only tiles with a decal in either set's grid are compared (each grid
// holds the 4111 square's decals, a superset of the walk's selection); a set without a grid makes every tile of the
// world a candidate. Empty when the rule is the same on both sides (the changed rectangles cover those changes).
// Cost: two decalsForTile per candidate tile (with a walk: two selections and two sorts); counted in `stats`.
struct TieRuleStats {
    uint32_t candidates = 0;                // tiles compared
    uint32_t differ = 0;                    // tiles returned
    uint32_t decalsCompared = 0;            // list entries of both sides
};
std::vector<TileOrigin> tieRuleTiles(const DecalSet& prev, const DecalSet& next, TieRuleStats* stats = nullptr);

// The EXE's TerrainDecalShader blobs by VA in its .shader section (build 4390913; asset-pipeline.md 3).
struct DecalShaderVas {
    uint32_t vs = 0x067e6360;
    uint32_t ps3 = 0x067f8f30;              // pass 1, three targets: the one stock draws
};

struct DecalCounters {
    uint64_t draws = 0, tilesWithDecals = 0, missingTextures = 0, badDecals = 0;
    uint64_t texturesLoaded = 0;            // files read through gamefs by the draw itself (each name once per DecalPass)
};

// Ticket 0642: the loader thread's counters (DecalPass::startLoader). Written on the caller's thread only (adopt, ready,
// the draws); the loader's own times arrive with each texture it hands over.
struct DecalLoaderCounters {
    uint64_t requested = 0;                 // names queued by request()
    uint64_t adopted = 0;                   // textures the loader made and the caller took into the cache
    uint64_t failed = 0;                    // names the loader could not load (a draw then takes its own path and fails as before)
    uint64_t createdAtAdopt = 0;            // of the adopted: created on the caller's thread (a SINGLETHREADED device)
    uint64_t bytes = 0;                     // file bytes the loader read
    double readMs = 0, readMsMax = 0;       // the loader's gamefs reads (sum, longest)
    double createMs = 0;                    // the loader's parse + creation (sum)
    uint64_t waits = 0;                     // draws that waited for a name in flight on the loader
    double waitMs = 0, waitMsMax = 0;       // those waits (sum, longest)
    uint64_t syncLoads = 0;                 // names a draw read itself while the loader ran (not requested, or failed there)
};

// ---- The decal pass (4104; terrain-decals.md sections 4 and 11). D83: behaviour from the knowledge base.
//
// One draw per decal, in the list's order, with the EXE's TerrainDecalShader pass 1 (VS + the three-target PS read from
// the EXE at run time): identity R16 index buffer (index i = i), the decal's vertex run as a triangle list (R16G16_FLOAT
// POSITION at 0, R16G16_FLOAT TEXCOORD0 at 4, stride 8), viewport 512 x 512, no depth, rasterizer solid / CULL_NONE /
// FrontCounterClockwise / DepthClip, one sampler MIN_MAG_MIP_LINEAR WRAP in s0-s2. Textures t0 albedo (the _SRGB view),
// t1 height and t2 spec (UNORM views) of one typeless resource per file. Pass 0 into the blend's three 512 RGBA16F
// targets (mip 0, not cleared) with SRC_ALPHA / INV_SRC_ALPHA for colour and ONE / ONE for alpha; pass 1 into the
// macro-shadow target (512 RGBA8, mip 0) with ONE / ONE for both. kNoTexture binds the EXE's 8 x 8 default texture
// (R8G8B8A8, texel (0, 0) = (255, 0, 0, 255), all others 0).

constexpr uint32_t kDecalCbFloats = 28;    // 7 registers = 0x70 bytes, what the VS reads

// The tile's DecalViewProj (registers 0-3, row-major as the EXE stores them): (s, 0, 0, 0), (0, -s, 0, 0),
// (0, 0, -0.5, 0), (tx, ty, 0.5, 1) with s = float(1/33), tx = -(originX + 32) s - 1/512, ty = (originY + 32) s + 1/512
// (the tile's 66-sample square onto clip -1..1, y flipped, shifted by half a texel of the 512 target). Bit-equal to the
// EXE's on every captured tile (section 11).
void decalViewProj(int originX, int originY, float out[16]);
// The whole constant block of one decal draw: DecalViewProj, (1, 1, 0, 0), the decal's rectangle, (priority, 0, 0, 0).
void decalConstants(int originX, int originY, const Decal& d, float out[kDecalCbFloats]);
// The EXE's default texture (handle 0) as 8 x 8 R8G8B8A8 bytes.
void defaultDecalTexels(uint8_t out[8 * 8 * 4]);

// The decal pass. One per device; it caches every texture it loads (by lower-case name) until it is destroyed.
class DecalPass {
public:
    DecalPass();
    ~DecalPass();
    DecalPass(const DecalPass&) = delete;
    DecalPass& operator=(const DecalPass&) = delete;

    // Creates the shaders from the EXE's blobs (checked by reflection: stage, shader model 5, the VS's inputs POSITION
    // and TEXCOORD and its cb0 within 7 registers, the PS's t0-t2 as 2D textures, no constant buffer beyond cb0's 7
    // registers, three targets), the input layout, states, sampler, identity index buffer and the default texture.
    // `files` may be null (then every texture must be given with provideTexture). Errors: no device BadLayout; a blob
    // not in the section NotFound; a blob of another stage or other declarations BadLayout; D3D11 failures Device.
    Status init(ID3D11Device* device, const shaders::ShaderSection& shaders, gamefs::GameFiles* files,
                const DecalShaderVas& vas = {});
    // Gives the texture for a name (tests replay captured textures; a caller with its own loader). The resource must be
    // a 2D texture with SHADER_RESOURCE; the pass keeps a reference and makes its own views (_SRGB and UNORM of a
    // typeless format, else the format itself). Errors: before init NotFound; no texture or not 2D BadLayout.
    Status provideTexture(const std::string& name, ID3D11Texture2D* tex);

    // The list's pass-0 decals into mip 0 of blend[0..2] (512 x 512, R16G16B16A16_FLOAT or _TYPELESS, RENDER_TARGET).
    // The caller generates the mips afterwards. A list with no pass-0 decal draws nothing and touches nothing.
    Status drawBlend(ID3D11DeviceContext* ctx, int originX, int originY, const TileDecalList& list,
                     ID3D11Texture2D* const blend[3]);
    // The list's pass-1 decals (tree shadows) into mip 0 of the macro-shadow target (512 x 512, R8G8B8A8_UNORM or
    // _TYPELESS, RENDER_TARGET), ONE / ONE. No pass-1 decal: nothing happens (macroShadow may then be null).
    Status drawMacroShadow(ID3D11DeviceContext* ctx, int originX, int originY, const TileDecalList& list,
                           ID3D11Texture2D* macroShadow);
    // Errors of both draws, checked for every decal of the pass before anything is drawn (so a failed call draws
    // nothing): before init NotFound; no context BadLayout; list without a set BadLayout; a list index outside the
    // set's decals, a texture index outside the set's names, a vertex run outside the set's vertices, empty, not a
    // multiple of 3 or longer than 65,535 BadLayout (counted in badDecals); a target missing or of the wrong format,
    // size or bind flags BadLayout; a texture name gamefs cannot resolve, or no gamefs, NotFound (missingTextures);
    // a file that does not parse: gamefs's codes; D3D11 failures Device.

    // How many of the list's decals belong to the pass (0 or 1); 0 for a list without a set.
    static size_t count(const TileDecalList& list, uint32_t pass);
    const DecalCounters& counters() const { return counters_; }
    // Issue 0417: every texture file the pass loaded, in order (a slow bake names the files it read).
    struct TextureLoad {
        std::string name;
        uint64_t bytes = 0;
        double readMs = 0;     // gamefs exists + read
        double createMs = 0;   // parse + texture creation
    };
    const std::vector<TextureLoad>& loads() const { return loads_; }

    // ---- Ticket 0642: the loader thread (research/05-graphics-composition/terrain-bake-slow-first-use.md section 7).
    // Without it every first use of a name is read, parsed and created inside the draw, on the caller's (the render)
    // thread: 21 names took 4,835 ms on a loaded disk (0641). With it a caller requests a set's names ahead of their use;
    // one thread of the pass reads each through gamefs (const reads: GameFiles may be read from two threads), parses it
    // and creates the texture and its views exactly as the draw does (the device is free-threaded; on a device created
    // D3D11_CREATE_DEVICE_SINGLETHREADED the parsed file is kept and the texture created at adopt, on the caller's
    // thread). Every other call below is the caller's thread, the one that draws.
    // startLoader: before init NotFound; without game files NotFound; a second call is Ok and starts nothing. The loader
    // ends with stopLoader, init and the destructor (queued names are dropped; finished ones stay for adopt).
    Status startLoader();
    void stopLoader();
    bool loaderOn() const;
    // Queues every name (a set's DecalSet::textures, any case, with or without a folder; looked up by its bare lower-case
    // file name as the draw does) that is neither in the cache nor queued, in flight, finished or failed. Returns how
    // many it queued; 0 without a loader.
    size_t request(const std::vector<std::string>& names);
    // Moves every texture the loader finished into the cache; returns how many. Cheap when there are none.
    size_t adopt();
    // adopt(), then true when every name is in the cache or failed on the loader (a draw of it then fails as it did
    // before 0642). Without a loader: true only when every name is in the cache.
    bool ready(const std::vector<std::string>& names);
    // Names queued, in flight or finished and not adopted.
    size_t loaderPending() const;
    const DecalLoaderCounters& loaderCounters() const { return loaderCounters_; }
    // The loader's files in the order adopted (bytes, read and parse + create time on the loader thread).
    const std::vector<TextureLoad>& loaderLoads() const { return loaderLoads_; }
    // The draws, with a loader: finished names are adopted first; a name in flight is waited for (counted in
    // DecalLoaderCounters::waits); a name never requested, or failed on the loader, is read by the draw as before
    // (counted in syncLoads, and as before in DecalCounters::texturesLoaded and loads()).

private:
    Status draw(ID3D11DeviceContext* ctx, int originX, int originY, const TileDecalList& list, uint32_t pass,
                ID3D11Texture2D* const* targets, uint32_t targetCount);
    struct Impl;
    Impl* impl_ = nullptr;
    DecalCounters counters_;
    std::vector<TextureLoad> loads_;
    DecalLoaderCounters loaderCounters_;
    std::vector<TextureLoad> loaderLoads_;
};

}} // namespace c5r::decal
