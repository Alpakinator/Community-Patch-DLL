// c5r/bake/residency.h - residency, bake-ahead and invalidation of baked terrain tiles (issue 3407, epic 3400 scope 5).
//
// One library for shell_server.exe --scene terrain and for server.exe's terrain scene (epic 3300): its calls are those of
// epic 3300's seam srvrender::TileCacheSource (server/src/render/srv_tile_source.h): onField, onPlotsChanged, beforeDraw,
// idle, release, statsLine. A host adapter forwards them; this header includes nothing of server/.
// Facts, figures and the rules' reasons: research/05-graphics-composition/terrain-bake-residency.md. In short:
//   - Land = the EXE's height class 2 or 3 (0x7212B0: highest sample of the tile + 4 samples, in u16/255 units, >= 2).
//   - Order: visible land tiles first (nearest first, before the draw, up to `burst` per frame; all of them when more
//     than `burst` have no bake at all, issue 0409's catchUpVisible), then breadth-first rings
//     from the visible tiles (from the map's centre before the first camera) in idle(), budgeted by `rate` tiles per
//     second, optionally by `gpuMsPerS` of the baker's gpuMs, and by the call's budgetMs of CPU.
//   - Radius (BakedTileCameraRadius): -1 keeps every land tile; N keeps and bakes only tiles within N rings of the
//     visible tiles and evicts the rest at once.
//   - Storage: t0 and t8 live in BC1 texture arrays (chunks of `arrayChunk` slices) handed to the baker as BakeTarget;
//     a baker that returns its own textures instead is copied into the slice. A resident tile holds two slices from its
//     first bake to its eviction, the t8 one also when its bake made no t8 (a re-bake sets areaChanged and makes one),
//     and a re-bake writes into the tile's own slices: the arrays do not grow with re-bakes (issue 0137). The EXE's pixel shaders declare t0 / t8
//     as Texture2D, and reading a Texture2DArray view through them is undefined by the D3D11 specification (it reads the
//     slice on the test machine's RTX 4080 [L, render/tests/bakeresidency]; other drivers may differ), so the renderer
//     gets a Texture2D copy (a pool) of each tile within one ring of the view, refreshed when re-baked.
//   - Out of memory (Err::OutOfMemory from the baker or from an array allocation, or beyond `memoryLimitMb`): the radius
//     in force drops to 5, then by one ring; at radius 0 baking stops; unbaked tiles stay plain.
//   - Invalidation: a content generation per tile; onField of the same size raises it for tiles whose heights or
//     material bytes changed (tile + 3 samples); onPlotsChanged and raiseTile raise it from outside. Stale tiles keep
//     their old bake until re-baked; re-bakes set BakeRequest::areaChanged.
//   - Counters: a frame (beforeDraw) with a visible land tile without any bake is a gray frame (acceptance C).
// Every call on one thread (the render thread), with that thread's device and immediate context.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "c5r/bake/baker.h"
#include "c5r/status.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct ID3D11Query;

namespace c5r {
namespace terrain { class Renderer; struct SampleRect; }
namespace heightfield { struct HeightGrid; }
namespace bake {

// Asked by idle() before each background bake and once at its end (issue 0140): true = a camera frame waits for the
// render thread, so the slice ends now. Called on the render thread; must be cheap (a shared-memory read).
typedef bool (*ResidencyYield)(void* ctx);

// ================================================================ the near-camera pair (issue 0332)
//
// The EXE draws a tile in state 5 (or 2) from an uncompressed R8G8B8A8 pair (tile+0x190 / +0x194, 256 or 512 texels, the
// full chain) instead of its BC1 pair. Which tiles hold one depends on the camera's history and on fixed pools, so the
// server reproduces the EXE's state machine (research/05-graphics-composition/terrain-bake-passes.md 3.5.1 and 3.5.3; D83:
// written from that description):
//   - the cull 0x7BA820, per visible tile in its list: wanted size = 0x4B2B40 (sizeClassForProjectedSize of the tile's
//     projected size, at least minClass); state 1 -> request 3; 4 -> request 5; 5 -> request 2 when the content
//     generation is ahead of the bake's, and request 2 when the held near size differs from the wanted one and the wanted
//     size's pool has more than one free texture;
//   - then the list sorted by 0x5E7010: requested state ascending, current state ascending, tile+0x1BC descending
//     (unsigned; not traced, 0 for every tile here), tile+0x1A4 descending (the tile's distance from the near plane:
//     the farthest first);
//   - the up-res pass: the first `cap` tiles of the sorted list whose state is not 0, not 5 with request 5 and not 4 with
//     request 5 (cap = upresSettled (2) on a settled frame, perFrameMoving (1) while the camera moves); each, while the
//     frame's bake budget lasts (budgetSettled (16) settled, perFrameMoving (1) moving; counted per attempt, also a
//     failed one), takes a new pair of its wanted size (2 textures when the tile has t8, else 1) from that size's pool:
//     in array mode (stock) an empty pool gives nothing (no fallback; the tile keeps drawing BC1); without array mode the
//     next smaller pool is tried. A new pair is taken before the old one is given back. On success: state 5, the old pair
//     returned, near size = the new size; the host bakes it (state-5 bake: bakepost's nearCache) the same frame.
//   - the pair goes back to its pool when the tile is evicted (onEvict) or its bake leaves state 5 / 2 (onBcOnlyBake: the
//     EXE's wrapper 0x627810 in states 1 and 3).
// The visible tiles' BC1 bakes (the residency's own) stay the host's; onBake tells the machine about them.
//
// Pools at stock high (TS+0x629F4..+0x62A00, live [L]): 16 textures of 64, 0 of 128, 256 of 256, 64 of 512; at most
// 256 x 349,524 + 64 x 1,398,100 + 16 x 21,844 B = 179,306,048 B (171.0 MiB) of video memory beside the BC1 caches.
struct NearPoolCounts {
    uint32_t n64 = 16, n128 = 0, n256 = 256, n512 = 64;   // textures per size
    uint32_t count(uint32_t size) const;                  // 0 for another size
};
// The pools' video memory with every texture made: sum of count(size) * nearCacheBytes(size).
uint64_t nearPoolBytes(const NearPoolCounts& c);

struct NearCacheOptions {
    NearPoolCounts pools;
    uint32_t minClass = 256;       // [0xD90F30] at TerrainDetailLevel 3
    uint32_t upresSettled = 2;     // [0xD90F64]: up-res candidates per settled frame
    uint32_t perFrameMoving = 1;   // [0xD90F60]: up-res candidates and bakes per frame while the camera moves
    uint32_t budgetSettled = 16;   // bakes per settled frame (0x7BA820)
    bool arrayMode = true;         // [0xD90F4C] = 1: no fallback to a smaller pool
};

// The EXE's tile states (tile+0x310 now, +0x314 requested), as far as the machine keeps them.
enum NearState : uint8_t { kNearNone = 0, kNearAhead = 1, kNearRebake = 2, kNearView = 3, kNearPending = 4, kNearHeld = 5 };

// One tile of the frame's visible list (the cull's list for the main view).
struct NearVisibleTile {
    uint32_t tile = 0;            // the host's tile index (< tileCount)
    float projectedSize = 0;      // 0x744AE0 in pixels (c5r::terrain::exeTileProjectedSize)
    float nearDistance = 0;       // tile+0x1A4: the box centre's distance from the view's near plane (0x4AD810)
    uint32_t key1BC = 0;          // tile+0x1BC (not traced; 0)
};

// A pair taken this frame: the host bakes the tile into these pool entries (state-5 bake), or calls upresFailed.
struct NearUpres {
    uint32_t tile = 0;
    uint32_t size = 0;            // 64 / 128 / 256 / 512
    int32_t entry[2] = {-1, -1};  // pool entries of `size` (entry[1] -1 without t8)
};

struct NearFrameResult {
    std::vector<NearUpres> upres;  // in the order taken
    uint32_t candidates = 0;       // tiles collected by the up-res pass (<= cap)
    uint32_t attempts = 0;         // allocations tried (each counted against the frame's bake budget)
    uint32_t failed = 0;           // attempts whose pool was short
    uint32_t budgetUsed = 0;       // = attempts; the frame's other bakes get budget - budgetUsed
    uint32_t requests2 = 0;        // tiles that asked for a new pair this frame (size change or content)
    // ticket 0667: held pairs whose content changed and whose new pair the pool could not give: baked again into their
    // own textures (inPlace; also in upres, with their own entries) or given back to draw their BC1 pair (dropped; also
    // counted in failed)
    uint32_t inPlace = 0;
    uint32_t dropped = 0;
    // Ticket 0712 (prep ahead): the sorted list's next tiles beyond this frame's candidates that the up-res pass would have
    // taken (the same rule as a candidate), at most kNearNext, in the list's order: the next frames' up-res if the camera
    // goes on. Nothing of the machine depends on it.
    static const uint32_t kNearNext = 4;
    std::vector<uint32_t> next;
};

struct NearCacheCounts {
    uint32_t held[4] = {};         // tiles holding a pair of 64 / 128 / 256 / 512
    uint32_t freeTex[4] = {};      // free textures per size
    uint32_t state[6] = {};        // tiles per current state
    int64_t upres = 0, upresFailed = 0, released = 0, frames = 0;
};

class NearCaches {
public:
    // BadLayout: tileCount 0 or above 1 << 22; a pool above 4096 textures; minClass not 64/128/256/512; upresSettled or
    // perFrameMoving 0 or above 64; budgetSettled 0 or above 256. Resets every tile to state 0.
    Status init(uint32_t tileCount, const NearCacheOptions& o);
    // The host baked the tile's BC1 pair (forView: a bake for the view, state 3; else the bake-ahead, state 1). needSecond:
    // the tile has t8 (+0x2FC): its pairs take two textures. A tile that holds a pair keeps it (state 5): its BC1 pair is
    // the state-5 bake's. Out of range: ignored.
    void onBake(uint32_t tile, bool forView, bool needSecond);
    // A BC1-only bake of a tile in state 5 / 2 (the EXE's wrapper for request 3 or 1): its pair goes back; state 3 / 1.
    void onBcOnlyBake(uint32_t tile, bool forView);
    // The tile's content changed (tile+0x1CC raised): a tile in state 5 asks for a new pair at its next visible frame.
    void onContentChanged(uint32_t tile);
    // The tile lost its caches (eviction 0x485670): its pair goes back; state 0.
    void onEvict(uint32_t tile);
    // One frame of the cull: the visible list (any order; duplicates and tiles out of range ignored) and whether the camera
    // is settled (TS+0x75FE4 = 0). Returns the pairs taken; their tiles are in state 5 from now on.
    NearFrameResult frame(const NearVisibleTile* vis, size_t n, bool settled);
    // The host could not bake a pair it was given: the pair goes back, the tile returns to its state before the up-res.
    void upresFailed(uint32_t tile);

    uint32_t tileCount() const;
    NearState state(uint32_t tile) const;
    NearState requested(uint32_t tile) const;
    uint32_t nearSize(uint32_t tile) const;      // tile+0x178: the held pair's size, 0 without one
    uint32_t wanted(uint32_t tile) const;        // tile+0x180 of its last visible frame (0 before)
    int32_t entry(uint32_t tile, int k) const;   // the held pair's pool entries (-1 without)
    bool drawsNear(uint32_t tile) const;         // the EXE's draw: states 2 and 5 with a pair
    uint32_t freeCount(uint32_t size) const;     // free textures of a size
    NearCacheCounts counts() const;
    const NearCacheOptions& options() const;

    // Ticket 0495 (the offline scene harness's near.bin): the machine's whole state as bytes, byte-exact (magic "C5NC",
    // version 1, little-endian): the tile count, the options, the counters, the four free lists in their order, the last
    // frame's up-res tiles, then every tile that is not in its initial state (index and every field). loadState takes such
    // bytes into a machine already init()ed with the same tile count and options. Errors (the machine unchanged):
    // Truncated (short or trailing bytes), BadLayout (magic, version, another tile count or options, a tile index out of
    // range or not ascending, a state above 5, a size not 0/64/128/256/512, a pool entry out of its pool or held twice,
    // a pool entry neither free nor held).
    void saveState(std::vector<uint8_t>& out) const;
    Status loadState(const uint8_t* data, size_t size);
    // Ticket 0495: the tile holds a pair (states 2 and 5) baked at a content generation older than its current one (a
    // rebake asked for and not yet taken, e.g. its pool has no free texture): the pair shows inputs a replay cannot bake.
    bool contentBehind(uint32_t tile) const;

private:
    struct T {
        uint8_t state = 0, req = 0, prevState = 0, prevReq = 0;
        bool second = false;
        uint32_t nearSize = 0, wanted = 0, prevSize = 0;
        int32_t entry[2] = {-1, -1}, prevEntry[2] = {-1, -1};
        uint64_t gen = 1, bakedGen = 1;
    };
    static int sizeIndex(uint32_t size);
    bool take(uint32_t size, int n, int32_t out[2], uint32_t& gotSize);  // gotSize: the pool's size (smaller only without array mode)
    void give(uint32_t size, const int32_t e[2]);
    void release(T& t);
    NearCacheOptions o_;
    std::vector<T> t_;
    std::vector<int32_t> free_[4];
    int64_t upres_ = 0, upresFailed_ = 0, released_ = 0, frames_ = 0;
    std::vector<uint32_t> lastUpres_;
};

// The state-5 bake of one tile into its near pair (issue 0332): the host's bake (the pipeline's bakepost with
// PostInputs::nearCache, terrain-bake-passes.md 3.5.1), into textures of the residency's pools (createNearCache's kind).
// t8 is null when the tile has no t8. A failure gives the pair back; the tile draws its BC1 pair.
class NearBaker {
public:
    virtual ~NearBaker() {}
    virtual Status bakeNear(ID3D11DeviceContext* ctx, TileId tile, uint64_t contentGeneration, ID3D11Texture2D* t0,
                            ID3D11Texture2D* t8) = 0;
};

struct ResidencyOptions {
    int radius = -1;              // BakedTileCameraRadius: -1 whole map, 0..128 rings
    double rate = 400;            // background bakes per second (token bucket)
    int burst = 16;               // visible tiles baked per beforeDraw (the EXE's settled budget, 0x7BA820)
    double gpuMsPerS = 0;         // > 0: background bakes use at most this many ms of the baker's gpuMs per second
    uint32_t sizeClass = 256;     // colour-cache size (0xD90F30 at the owner's quality)
    bool arrays = true;           // t0 / t8 in BC1 texture arrays (false: one texture per cache, as the baker returns it)
    uint32_t arrayChunk = 256;    // slices per array texture (one chunk = 11 MB at 256)
    double memoryLimitMb = 0;     // > 0: arrays beyond this many MB count as out of memory (a cap, and a test seam)
    int trace = 0;                // log the first N bakes (tile, ring, visible, class)
    // issue 0140: idle() asks yield(yieldCtx) before each bake and ends its slice when it says a camera waits (null: never)
    ResidencyYield yield = nullptr;
    void* yieldCtx = nullptr;
    // issue 0140: true (the EXE's rule, 0x7BA820) re-bakes a visible tile whose bake is stale before the draw, within burst
    // and budgetMs; false bakes before the draw only visible tiles without any bake (gray) and leaves the stale ones to
    // idle(), which takes the visible tiles first (their old bake is drawn until then; no gray frame comes of it)
    bool staleBeforeDraw = true;
    // issue 0365: true: idle() re-bakes stale tiles (a bake older than the tile's content) before it bakes tiles that have
    // none, each pass in ring order; so a change raised during the bake-ahead (a decal generation published after the
    // field) re-bakes the tiles already baked without it before the bake-ahead goes on. false (the default): one pass in
    // ring order (as before 0365: a raise restarts the pass from the first ring, stale and unbaked tiles alike; the
    // visible tiles are ring 0, so their re-bakes come first either way). Off by default after 0365's oldpc A/B
    // (terrain-decals.md 19.4): on huge it showed no measured gain (the load generations' re-baked tiles were done 0.2 s
    // after their publication with it off too) and no measured cost (set 1's extra bakes were run-to-run spread, set 2).
    bool staleFirst = false;
    // issue 0409: true (the default): when more visible land tiles have no bake at all than the frame's burst (the first
    // draw after a load, a jump to an unbaked part of the map), beforeDraw bakes every one of them before the draw,
    // without burst or budgetMs, then the land tiles without a bake at the view's edge that c5r::terrain::Renderer's
    // wider cull may draw too (its z bounds span the tile's whole height page; the residency takes z over the whole
    // byte range there), so that frame shows no tile in the fallback colour; stale tiles (an older bake is drawn)
    // keep the burst. With as many such tiles as the burst or fewer (an ordinary pan) the burst applies as before. The
    // first draw after each onField is logged with its bake count and time either way. false: burst and budgetMs always
    // (before 0409: the huge map's first frames showed 58-74 unbaked tiles for 5-8 frames, ticket 0404).
    bool catchUpVisible = true;
    // issue 0332: the near-camera pair (NearCaches above). Off by default in the library; the server turns it on by its
    // host key render_bake_near (1 by default since 0371; research terrain-bake-passes.md 3.5.4). On: init needs
    // nearBaker; beforeDraw runs NearCaches::frame over the visible land tiles (the projected size of 0x744AE0 for
    // ResidencyView::width, the distance from the view's near plane; settled = !ResidencyView::moving) before its own
    // visible bakes, which then get burst minus the frame's up-res attempts; each pair taken is baked by nearBaker into
    // pool textures made on first use. Eviction and dropped tiles give their pairs back.
    bool nearCaches = false;
    NearCacheOptions nearOptions;
    NearBaker* nearBaker = nullptr;   // not owned
};

// Ticket 4909: what Residency::onFieldRegion did.
struct RegionFieldStats {
    uint32_t tilesScanned = 0, tilesChanged = 0;
    bool landChanged = false;
    double ms = 0;
};

struct ResidencyStats {
    uint32_t tilesX = 0, tilesY = 0, class1 = 0, class2 = 0, class3 = 0, land = 0;
    uint32_t resident = 0, visible = 0, visibleLand = 0, pending = 0;
    uint64_t cacheBytes = 0;       // BC1 bytes of the resident caches
    uint64_t arrayBytes = 0;       // bytes of the allocated array textures (the video memory the arrays take)
    uint32_t arrays = 0, slicesUsed = 0, slicesTotal = 0;
    uint32_t t8Held = 0;           // arrays on: t8 slices held for tiles whose bake made no t8 (counted in slicesUsed, 0137)
    uint32_t drawTextures = 0, drawInUse = 0;   // arrays on: the pool of Texture2D draw copies (tiles within one ring of the view)
    int64_t drawCopies = 0;
    int64_t bakes = 0, rebakes = 0, failures = 0, oomEvents = 0, evicted = 0, sinkErrors = 0, raises = 0, copies = 0;
    int64_t fieldChangedTiles = 0, fields = 0, plotsChanged = 0;
    int64_t framesCounted = 0, grayFrames = 0, grayFramesAfterAhead = 0, grayTileFrames = 0, staleFrames = 0;
    int64_t latencyN = 0;
    double latencyMaxMs = 0, latencySumMs = 0;
    double bakeCpuMsSum = 0, bakeCpuMsMax = 0, bakeGpuMsSum = 0, bakeGpuMsMax = 0;
    // issue 0140. idle slices (calls that baked at least one tile) and their longest CPU time; yields = slices ended by
    // the yield check. yieldWait*: per yield, the time from the check before it that found no camera to the check that
    // found one: the camera arrived inside that span, so it waited at most that long in the slice (an upper bound).
    // yieldWaitHist: those bounds in 0.25 ms bins (the last bin: 8 ms and more). Pre-draw: bakes in beforeDraw, the stale
    // ones among them (re-bakes), and the longest beforeDraw bake time of one frame.
    int64_t idleSlices = 0, yields = 0, predrawBakes = 0, predrawRebakes = 0, staleDeferred = 0;
    int64_t staleFirstBakes = 0;   // issue 0365: idle() bakes taken by the stale pass (ResidencyOptions::staleFirst)
    // issue 0409 (ResidencyOptions::catchUpVisible): frames whose visible land tiles without a bake exceeded the burst and
    // were all baked before the draw, their bakes (of them at the view's edge: land tiles outside the visible set that
    // the renderer's wider z bounds may draw), the largest such set and the longest such frame's bake time.
    int64_t catchUpDraws = 0, catchUpBakes = 0, catchUpEdgeBakes = 0;
    uint32_t catchUpMaxTiles = 0;
    double catchUpMaxMs = 0;
    // issue 0409: the first beforeDraw after the last onField: visible land tiles without a bake before it, bakes made in
    // it and their time (-1 / 0 before that draw).
    int32_t firstDrawUnbaked = -1, firstDrawBakes = -1;
    double firstDrawMs = -1;
    // issue 0409: bakes that took 500 ms of CPU or more (the first 16 are logged with their tile and time)
    int64_t slowBakes = 0;
    double idleSliceMaxMs = 0, yieldWaitMaxMs = 0, yieldWaitSumMs = 0, predrawMaxMs = 0;
    enum { kYieldBins = 33 };
    int64_t yieldWaitHist[kYieldBins] = {};
    double yieldWaitP99Ms() const;   // the upper edge of the bin that holds the 99th percentile; 0 without yields
    // issue 0332, the near pairs (zero while ResidencyOptions::nearCaches is off): tiles holding a pair of 256 / 512, free
    // textures of 256 / 512, pool textures made and their bytes, pairs baked, attempts that found the pool short, bakes
    // that failed, and the frame's up-res attempts at the last beforeDraw.
    uint32_t nearHeld256 = 0, nearHeld512 = 0, nearFree256 = 0, nearFree512 = 0, nearTextures = 0;
    uint64_t nearBytes = 0;
    int64_t nearUpres = 0, nearShort = 0, nearBakeFailures = 0;
    uint32_t nearLastAttempts = 0;
    // issue 0371: visible tiles the last beforeDraw handed to the renderer with their near pair of 256 / 512 (the EXE's
    // draw of states 2 and 5); the other visible land tiles with a bake draw their BC1 pair
    uint32_t nearDrawn256 = 0, nearDrawn512 = 0;
    bool aheadDone = false;
    double aheadMs = -1;           // onField -> every land tile in the radius baked (first time per field)
    uint32_t aheadTiles = 0;
    int radiusInForce = -1;
    bool stopped = false;
};

// The frame's camera, as srvrender::ViewInfo (row-vector matrices, D3DX layout).
struct ResidencyView {
    int64_t frameId = 0;
    float camWorld[16] = {};
    float view[16] = {}, proj[16] = {};
    uint32_t width = 0, height = 0;
    bool moving = false;          // issue 0332: the camera moves (the EXE's TS+0x75FE4 = 1): one up-res per frame
};

typedef void (*ResidencyLog)(void* ctx, const char* line);

class Residency {
public:
    Residency();
    ~Residency();
    Residency(const Residency&) = delete;
    Residency& operator=(const Residency&) = delete;

    // BadLayout: baker null, radius outside -1..128, rate <= 0 or not finite, burst < 0, gpuMsPerS < 0, memoryLimitMb < 0,
    // arrayChunk 0 or above 2048, an unknown size class. The baker is not owned; init() of the baker is the host's.
    Status init(TileBaker* baker, const ResidencyOptions& o, ResidencyLog log = nullptr, void* logCtx = nullptr);
    // A new height field (after Renderer::setHeightField, which dropped the renderer's caches) or the same one again. Same
    // tile counts: tiles whose inputs changed get a new generation, every resident tile is handed to the renderer again
    // at the next beforeDraw. Else everything is released and every land tile starts at generation 1. map: the baker's
    // other inputs (tables, art types); its grid is set to &grid. The grid must stay alive. Errors: NotFound (init not
    // done), Unsupported (tilePlots != 1: the bake's tile is one 64-sample plot), BadLayout (sizes), the baker's setMap.
    Status onField(ID3D11Device* dev, const heightfield::HeightGrid& grid, uint32_t tilePlots, uint32_t loadSeq,
                   const MapInputs* map = nullptr);
    // Epic 0465, ticket 4909 (live-terrain.md 8.14): the same field (same device, sizes, scales, wrap) with its samples
    // changed inside rects only (a live terrain window, after terrain::Renderer::updateHeightRegion, which keeps the
    // renderer's caches). Only the tiles whose read window (64 + 2 x 4 samples) meets a rect are compared, by onField's
    // own rule, so every tile ends as onField's comparison leaves it (class, land, bounds, hash, generation; the EXE list
    // with near caches; the class counts; the bake order); no tile is marked to be handed to the renderer again (its
    // caches were kept) and the bake scheduling (tokens, bake-ahead) runs on. map: as onField's. Errors: NotFound (no
    // field yet), BadLayout (another device or layout, a rect outside), the baker's setMap.
    Status onFieldRegion(ID3D11Device* dev, const heightfield::HeightGrid& grid, const std::vector<terrain::SampleRect>& rects,
                         const MapInputs* map = nullptr, RegionFieldStats* stats = nullptr);
    // Ticket 4909 (tests, the bench): [0] the tiles' field state, [1] the EXE list and class counts, [2] the bake order.
    Status fieldDigest(std::vector<uint64_t>& out) const;
    // Plots whose content changed: index = y * plotsX + x of the game's map (plotsX = the grid's plots in x, rows without
    // the grid's 2 + 2 margin rows). Each raises the tiles within 2 columns and 3 rows of the plot's tile (the EXE's
    // rectangle invalidation reaches that far for a road, terrain-bake-residency.md 3.3). Indices outside: counted, ignored.
    void onPlotsChanged(const uint32_t* plotIndices, size_t count, double signalMs = -1);
    // One tile raised from outside (the tile generation block). False outside the map or before a field.
    bool raiseTile(uint32_t x, uint32_t y, double signalMs = -1);
    // Before each draw: the visible set, rings and eviction; visible tiles without a current bake baked (burst, budgetMs;
    // with catchUpVisible every visible land tile without any bake when there are more of them than the burst);
    // every changed tile handed to the renderer (setTileCache); the frame counted.
    void beforeDraw(ID3D11DeviceContext* ctx, terrain::Renderer& r, const ResidencyView& v, double budgetMs);
    // Background bake-ahead and re-bakes, at most budgetMs of CPU (0: nothing). The caches reach the renderer at the next
    // beforeDraw. With ResidencyOptions::yield the slice also ends as soon as it says a camera waits (checked before each
    // bake: a waiting camera is held up by at most the bake in progress).
    void idle(ID3D11DeviceContext* ctx, double budgetMs);
    // Ticket 0712 (prep ahead; research terrain-bake-residency.md 9): the tiles this residency expects to bake next, read
    // without changing anything. near: the last beforeDraw's NearFrameResult::next (the next frames' up-res if the camera
    // goes on; near pairs on and not held). ahead: the visible tiles that still need a bake after the last beforeDraw (the
    // next frames' burst), then idle()'s order from its cursor (the stale pass first with staleFirst), at most maxAhead in
    // all. Each with the tile's content generation now (the one its bake request will carry if nothing changes first).
    struct Hint {
        TileId tile;
        uint64_t contentGeneration = 0;
    };
    void nextBakes(std::vector<Hint>& nearHints, std::vector<Hint>& aheadHints, size_t maxAhead) const;   // `near`: a Windows macro
    // True once after a visible tile's cache changed outside beforeDraw (the host may draw again with the last camera).
    bool takeVisibleChanged();
    // Releases every D3D object (device lost, shutdown). onField follows before the next use.
    void release();
    // One line: tiles resident, pending, gray frames, memory.
    void statsLine(char* out, size_t n);
    const ResidencyStats& stats();
    bool hasField() const;
    uint32_t tilesX() const;
    uint32_t tilesY() const;
    int tileClass(uint32_t x, uint32_t y) const;   // 0 outside / no field
    bool tileBaked(uint32_t x, uint32_t y) const;  // a current bake
    bool tileHasBake(uint32_t x, uint32_t y) const;   // issue 0365: any bake, current or stale
    // issue 0332: the tile's near pair when it holds one that the EXE's draw would bind (states 2 and 5): its textures
    // (t8 null without one; owned by the residency, valid until the next call that changes the tile) and size. False
    // otherwise or with nearCaches off.
    bool nearPair(uint32_t x, uint32_t y, ID3D11Texture2D** t0, ID3D11Texture2D** t8, uint32_t* size) const;
    const NearCaches* nearCaches() const;          // null while nearCaches is off or before a field
    // Ticket 0495 (the offline scene harness): the near machine's state (NearCaches::saveState); false while nearCaches is
    // off or before a field. restoreNear loads such a state (NearCaches::loadState) and bakes every pair the state holds
    // in states 2 and 5 into its pool textures with the tile's current inputs (nearBaker, as an up-res would), so that the
    // next beforeDraw draws them; *pairs = the pairs baked. Errors: NotFound (near off, no field, no device context),
    // loadState's, the first failed bake (the machine then holds the loaded state, the failed tile draws its BC1 pair).
    // setNearHold(true): beforeDraw skips the up-res pass (the offline player's settle calls beforeDraw many times per
    // frame, the live server once); BC1 bakes still tell the machine.
    bool nearState(std::vector<uint8_t>& out) const;
    // The texels of every pair whose content is behind its tile (NearCaches::contentBehind), read back on ctx: each
    // texture's whole chain, RGBA8, mip 0 first, rows tight (nearCacheBytes(size) bytes). restoreNear uploads such an
    // image into the pair's pool texture instead of baking it; an image for a tile that holds no pair in the state, of
    // another size or of a wrong length is refused (BadLayout).
    struct NearPairImage {
        uint32_t tile = 0, size = 0, second = 0;   // second: 0 the t0 texture, 1 the t8 one
        std::vector<uint8_t> texels;
    };
    Status nearStaleImages(ID3D11DeviceContext* ctx, std::vector<NearPairImage>& out) const;
    Status restoreNear(ID3D11DeviceContext* ctx, const uint8_t* data, size_t size, uint32_t* pairs = nullptr,
                       const std::vector<NearPairImage>* images = nullptr);
    void setNearHold(bool hold);

    struct Impl;

private:
    Impl* p_ = nullptr;
};

// The highest-sample class of 0x7212B0 for one tile of g (1, 2 or 3), with the server's window (tile + 4 samples).
int tileHeightClass(const heightfield::HeightGrid& g, uint32_t tileX, uint32_t tileY);

}  // namespace bake
}  // namespace c5r
