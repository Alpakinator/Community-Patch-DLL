// c5r/bake/pipeline.h - the bake pipeline (issue 3404, epic 3400): one tile of a HeightGrid in, its BC1 colour caches
// t0 / t8 out, on the server's GPU with the EXE's own shaders. It joins issue 3401's layer lists (bakelayers), 3402's
// vertex fill and blend pass (bakeblend), 3403's passes after the blend (bakepost), 3405's grid normals and slope classes
// (bakegrid) and 3406's height pages and bake-time constants (bakeheight) behind the TileBaker contract of
// c5r/bake/baker.h.
//
// The per-tile inputs sit behind small interfaces. The defaults are the EXE's rules (wave 2); the stand-ins of wave 1
// stay selectable for tests and comparisons (research: 05-graphics-composition/terrain-bake-comparison.md):
//   GridNormalSource    the CacheBake grid normals, tile+0x308: default 3405's gridNormals; stand-in 3403's
//                       approximation (central differences of the vertex heights, terrain-bake-passes.md 5.1).
//   SlopeClassSource    the blend vertices' slope classes: default 3405's tileSlopeClasses; stand-in all 0.
//   HeightPageSource    the macro shadow's height page tile+0xEC and its offset tile+0xE4/+0xE8: default 3406's
//                       buildHeightPage (pages shared by their tiles); stand-in 3403's fitted rule, one page per tile.
//   BakeConstantsSource HalfVector and AuxDir at bake time: default 3406's stockBakeLight; stand-in the load-screen
//                       values of 3023 run 3 (PostConstants' defaults).
//   StyleSource         the tile's terrain style (tile+0x138): default MapInputs::plotArtTypes through 3406's tileStyle,
//                       else MapInputs::style.
//
// D83: the algorithms are written from the knowledge base's description; shaders and textures are read from the
// player's install at run time. Decals (roads, farms: bake stage 2; epic 0166) are drawn by a c5r::decal::DecalPass
// the pipeline makes in init, from BakeRequest::decals (issue 4105; terrain-decals.md 11 and 12).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "c5r/bake/baker.h"
#include "c5r/bake/blend.h"
#include "c5r/bake/layers.h"
#include "c5r/bake/post.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/status.h"

struct ID3D11ShaderResourceView;

namespace c5r {
namespace gamefs { class GameFiles; }
namespace shaders { class ShaderSection; }
namespace decal { class DecalPass; struct DecalCounters; struct DecalShaderVas; }
namespace bake {

// Vertices per side of a tile's grids at most (67; (ex + 2) x (ey + 2) in general, 66 on the short side of the tiles of
// the last column / row).
constexpr uint32_t kPipelineGrid = kBlendGrid;

// ---------------------------------------------------------------- the per-tile inputs

class GridNormalSource {
public:
    virtual ~GridNormalSource() {}
    virtual const char* name() const = 0;
    // (ex + 2) x (ey + 2) unit normals (x, y, z; row-major, j outer) for vertex (i, j) at height sample (ox + i - 1,
    // oy + j - 1).
    virtual Status normals(const heightfield::HeightGrid& g, const TileWindow& w, std::vector<float>& out) = 0;
};

class SlopeClassSource {
public:
    virtual ~SlopeClassSource() {}
    virtual const char* name() const = 0;
    // (ex + 2) x (ey + 2) classes 0..2, row-major, or empty for "all 0" (bakeblend's SlopeClasses).
    virtual Status classes(const heightfield::HeightGrid& g, const TileWindow& w, SlopeClasses& out) = 0;
};

struct HeightPageView {
    ID3D11ShaderResourceView* srv = nullptr;  // 2D, height in the alpha channel; owned by the source, valid until its next call
    int32_t offsetX = 0, offsetY = 0;         // tile+0xE4 / +0xE8: added to the sample coordinate before * (1/1024, -1/1024)
};

class HeightPageSource {
public:
    virtual ~HeightPageSource() {}
    virtual const char* name() const = 0;
    virtual Status page(ID3D11Device* dev, ID3D11DeviceContext* ctx, const heightfield::HeightGrid& g, const TileWindow& w,
                        HeightPageView& out) = 0;
    // Ticket 4909: g's samples changed in place inside boxes (boxCount quads x0, y0, x1, y1, exclusive ends; null: anywhere).
    // A source that keeps pages drops the ones built from those samples. The default keeps nothing, so does nothing.
    virtual void heightsChanged(const heightfield::HeightGrid& g, const uint32_t* boxes, size_t boxCount) {
        (void)g;
        (void)boxes;
        (void)boxCount;
    }
};

class BakeConstantsSource {
public:
    virtual ~BakeConstantsSource() {}
    virtual const char* name() const = 0;
    // Fills k.halfVector and k.shadowDir (and may change the other light values); heightOffset is set by the pipeline.
    virtual Status constants(TileId t, PostConstants& k) = 0;
};

class StyleSource {
public:
    virtual ~StyleSource() {}
    virtual const char* name() const = 0;
    virtual uint32_t style(TileId t) = 0;  // 0 Generic, 1 America, 2 Asia, 3 Africa, 4 Europe
};

// ---------------------------------------------------------------- the stand-ins (CPU parts exposed for the tests)

// The vertex heights of the tile's (ex + 2) x (ey + 2) grid: (float(h16) * (1/255)) * g.sz at the sample (ox + i - 1,
// oy + j - 1), x and y wrapped unsigned modulo samplesX / samplesY as the blend vertex fill does (terrain-bake-blend.md
// 2). Checks: grid sizes inconsistent, extents 0 or above 65 [BadLayout].
Status gridVertexHeights(const heightfield::HeightGrid& g, const TileWindow& w, std::vector<float>& z);
// 3403's approximation (terrain-bake-passes.md 5.1): n = normalize(-dz/dx, -dz/dy, 1) from central differences of the
// vertex heights, x step g.sx, y step g.sy, one-sided at the grid's edges. [STAND-IN]
Status approxGridNormals(const heightfield::HeightGrid& g, const TileWindow& w, std::vector<float>& out);
// 3403's fitted page rule (terrain-bake-passes.md 5.0 before 3406 decoded it): page texel (column, row) = (x + offsetX,
// (-offsetY - y) mod 1024) holds round(h16 / 255) clamped to 255, x wrapped when the map wraps, else clamped, y clamped.
// The stand-in centres the tile in its own page: offsetX = 512 - ox - 32, offsetY = -(oy + 544). Fills mip 0 (1024 x
// 1024 bytes, row-major). [STAND-IN]
constexpr uint32_t kStandInPageSize = 1024;
Status fittedHeightPage(const heightfield::HeightGrid& g, const TileWindow& w, std::vector<uint8_t>& page,
                        int32_t& offsetX, int32_t& offsetY);

std::unique_ptr<GridNormalSource> makeApproxGridNormals();
std::unique_ptr<SlopeClassSource> makeFlatSlopeClasses();
std::unique_ptr<HeightPageSource> makeFittedHeightPages();   // A8_UNORM 1024 x 1024, 11 mips (box-filtered on the CPU)
std::unique_ptr<BakeConstantsSource> makeCaptureConstants(); // PostConstants' defaults (3023 run 3, load screen)

// ---------------------------------------------------------------- the EXE's rules (wave 2): the baker's defaults

// 3405 (src/bakegrid, c5r/bake/grid.h): gridNormals and tileSlopeClasses of the tile's (ex + 2) x (ey + 2) grid.
std::unique_ptr<GridNormalSource> makeExeGridNormals();
std::unique_ptr<SlopeClassSource> makeExeSlopeClasses();
// 3406 (src/bakeheight, c5r/bake/heightpage.h): buildHeightPage + createHeightPageTexture, one texture per page, kept
// while the grid stays the same (pointer and sizes), and stockBakeLight (HalfVector, AuxDir as the EXE's float bits).
std::unique_ptr<HeightPageSource> makeExeHeightPages();
std::unique_ptr<BakeConstantsSource> makeStockBakeConstants();

// ---------------------------------------------------------------- the baker

enum class GpuTiming { Off, Blocking, Deferred };

// Ticket 0698 (research: 05-graphics-composition/terrain-bake-slow-first-use.md section 8): where the layer textures and
// the style inputs are read. Off: by the bake that first needs them, on the caller's thread (the behaviour before 0698).
// Thread: a loader thread of the baker's own reads them ahead: the five styles' inputs from init, every layer texture of
// the map's TerrainDef tables from setMap (0642's route for the decal textures). A bake takes what the loader finished,
// waits for a name the loader has queued or in flight (moved to the front of its queue), and reads a name itself only
// when it was never requested or failed on the loader, so its output is the same bit for bit.
enum class LoadAhead { Off, Thread };

// Ticket 0698: the loader's counters (written on the caller's thread: at adoption and in the bakes).
struct PipelineLoaderCounters {
    uint64_t requested = 0;          // layer texture names and styles queued
    uint64_t layerTextures = 0;      // layer textures the loader made and the baker took
    uint64_t missing = 0;            // layer texture names the install lacks (bound as the EXE's handle 0, no read)
    uint64_t styles = 0;             // styles the loader made and the baker took
    uint64_t failed = 0;             // names or styles that failed on the loader (a bake then reads them as before)
    uint64_t createdAtAdopt = 0;     // made on the caller's thread (a D3D11_CREATE_DEVICE_SINGLETHREADED device)
    uint64_t dropped = 0;            // made by the loader after a bake had read the name itself (released)
    uint64_t bytes = 0;              // file bytes the loader read
    double readMs = 0, readMsMax = 0;  // the loader's reads (sum, longest file)
    double createMs = 0;             // the loader's parse + creation (sum)
    uint64_t waits = 0;              // bakes that waited for a name or style queued or in flight
    double waitMs = 0, waitMsMax = 0;
    uint64_t promoted = 0;           // queued names a bake moved to the front of the queue
    uint64_t fallbackBinds = 0;      // bakes whose layer textures were not all from the loader (BlendTextureCache::bind)
    // "N layer textures (M missing), S styles, X MB, reads R ms in all, longest L ms, create C ms, F failed; bakes that
    // waited W (T ms, longest U ms), fallback binds B"
    std::string str() const;
};

// ---------------------------------------------------------------- ticket 0712: prep ahead
//
// Research: 05-graphics-composition/terrain-bake-residency.md section 9; the Architect's frame-jobs note section 6 (B1).
// A bake's CPU products that read only the map's inputs (the height grid's samples and material bytes, the layer tables)
// and the tile: the layer list, the tile's window, the slope classes, the blend vertices and indices ("prep") and the
// grid normals. PrepAhead::Thread: a worker thread of the baker computes them ahead for the tiles the caller names
// (prepRequest) into a bounded cache keyed by tile, content generation and the map's epoch; a bake takes a product of its
// tile and generation (hit), waits for one in flight (at most about the time of computing it), or computes inline as
// before (miss). The texels are made in the bake that needs them by the unchanged GPU steps. A product is the inline
// computation's value for the same inputs (prepCpu is the one code path of both), so the bake's bytes do not change.
// Every writer of a prep input joins the worker and clears the cache first (prepInvalidate): the grid is read by the
// worker in place, so a write without that join races with it. setMap and heightsChanged invalidate too.
// Only with the pipeline's own normal and slope sources (PipelineConfig::normals and ::slopes null): a caller's source
// may not be safe to call from another thread, so with one the bakes compute inline.
enum class PrepAhead { Off, Thread };

// The products (prepCpu's output). Immutable once made; shared by the cache and the bakes that take it.
struct PrepProduct {
    TileId tile;
    LayerList list;
    TileWindow window;
    SlopeClasses cls;
    std::vector<BlendVertex> verts;
    std::vector<uint16_t> idx;
    std::vector<float> normals;   // (ex + 2) x (ey + 2) x 3
    double prepMs = 0, normalsMs = 0;   // the steps' time where it was made (the worker or the bake's thread)
    size_t bytes() const;               // the vectors' heap bytes
};

// The pure computation (thread-safe for the EXE's rules: makeExeGridNormals / makeExeSlopeClasses keep no state):
// buildLayerList, tileWindow, slopes.classes, fillBlendVertices, buildBlendIndices, normals.normals, in that order, as a
// bake makes them. Checks: tile outside the grid [NotFound]; those of each step; the normals' size [BadLayout].
Status prepCpu(const heightfield::HeightGrid& g, const LayerTables& t, TileId tile, GridNormalSource& normals,
               SlopeClassSource& slopes, PrepProduct& out);
// True when a and b hold the same products, byte for byte (the vectors by memcmp, the list and window by field).
bool samePrepProduct(const PrepProduct& a, const PrepProduct& b);

// A tile the caller expects to bake soon, with the content generation its bake request will carry.
struct PrepHint {
    TileId tile;
    uint64_t contentGeneration = 0;
};

// Ticket 0712: the counters (written under the worker's lock; read on the caller's thread).
struct PrepCounters {
    uint64_t requested = 0;     // jobs queued (prepRequest; a hint already made, in flight or queued is not counted)
    uint64_t replaced = 0;      // queued jobs dropped by a newer request of their lane (never started)
    uint64_t made = 0;          // products the worker made and cached
    uint64_t failed = 0;        // jobs whose prepCpu failed (nothing cached: the bake then fails inline as before)
    uint64_t discarded = 0;     // products made under an epoch that an invalidation ended (not cached)
    uint64_t hits = 0;          // bakes that took a cached product
    uint64_t waits = 0;         // bakes that waited for their product in flight and took it
    uint64_t waitTimeouts = 0;  // bakes whose wait passed its bound (computed inline; the product is cached later)
    uint64_t claimed = 0;       // bakes whose tile was queued, not started: taken off the queue, computed inline
    uint64_t misses = 0;        // bakes that computed inline with prep ahead on (claimed and timeouts included)
    uint64_t evictedUnused = 0; // products evicted from the cache before a bake took them
    uint64_t invalidations = 0; // prepInvalidate calls (the writers' joins, setMap, heightsChanged)
    uint64_t joinWaits = 0;     // invalidations that waited for a job in flight
    uint64_t verified = 0, verifyMismatches = 0;   // PipelineConfig::prepVerify: hits checked against inline, mismatches
    double waitMs = 0, waitMsMax = 0;    // the bakes' waits (waits and timeouts)
    double joinMs = 0, joinMsMax = 0;    // the invalidations' waits
    double workerMs = 0;                 // the worker's prepCpu time (sum)
    uint64_t products = 0;               // in the cache now
    uint64_t bytes = 0, bytesMax = 0;    // the cache's heap bytes now and at most
    // "requested R (replaced P), made M (failed F, discarded D); bakes: hits H, waits W (T ms, longest U ms), timeouts O,
    // claimed C, misses X; evicted unused E; invalidations I (J waited, K ms); worker S ms; cache N products, B KB (max)"
    std::string str() const;
};

// Issue 0417: what the pipeline does once, before the first bake that needs it (research: 05-graphics-composition/
// terrain-bake-slow-first-use.md). Off: everything is made on first use inside a bake (the behaviour before 0417).
// AtInit: init ends with warm() (below) over PipelineConfig::warmTables, so that a load's first bakes find every layer
// texture, every style's cubes and LUT and every pass already used once on the GPU.
enum class WarmUp { Off, AtInit };

// Issue 0417: the CPU time of one bake's steps (steady clock), and the first uses it made. A step's time covers
// recording its D3D11 calls; with GpuTiming::Deferred nothing waits for the GPU, so a GPU stall shows as a slow D3D11 call
// (a Map, a Create, a Flush) inside the step that made it.
struct PipelineStepTimes {
    double totalMs = 0;      // the whole bakeWith call
    double prepMs = 0;       // checks, layer list, window, style, slope classes, blend vertices and indices
    double pageMs = 0;       // the height page (HeightPageSource::page; built and uploaded on the page's first use)
    double ownMs = 0;        // the blend's own constant block
    double texturesMs = 0;   // the layer textures (BlendTextureCache::bind: read, parse and create on first use)
    double blendMs = 0;      // BlendPass::run (upload, draws, mips)
    double normalsMs = 0;    // the grid normals
    double styleMs = 0;      // the style's cubes and LUT (read and created on the style's first use)
    double constantsMs = 0;  // the bake constants
    double postMs = 0;       // BakePost::run (decals, normal, shadow, cache bake, compression, the output textures)
    double copyMs = 0;       // the copies into the caller's array slices
    double gpuTimerMs = 0;   // the GPU timer's end (Deferred: flush and collect; Blocking: the wait for the GPU)
    double threadMs = 0;     // this thread's own CPU time (user + kernel) in the bake: far below totalMs = it waited
    double logMs = 0;        // the bake's own log line (after totalMs; set when one was written)
    uint32_t texturesLoaded = 0;        // layer textures this bake loaded (each name once per baker)
    uint32_t decalTexturesLoaded = 0;   // decal textures this bake loaded (each name once per decal pass)
    bool pageBuilt = false;             // the default height pages: this bake built its page
    bool styleLoaded = false;           // this bake loaded its style's inputs
    // Ticket 0698 (LoadAhead::Thread): this bake waited for the loader (names or a style queued or in flight); the wait
    // is inside the textures and style steps. A wait counts as a first use (the bake is logged with its steps).
    uint32_t loaderWaits = 0;
    double loaderWaitMs = 0;
    // Ticket 0712 (PrepAhead::Thread): where this bake's prep and normals came from: 0 computed inline with prep ahead off
    // or not applicable, 1 a cached product (hit), 2 a product it waited for, 3 computed inline with prep ahead on (miss).
    // prepMs and normalsMs are then the bake's own time (a hit: the look-up; a wait: the look-up and the wait in prepMs).
    uint8_t prepAhead = 0;
    double prepWaitMs = 0;
    bool firstUse() const { return texturesLoaded || decalTexturesLoaded || pageBuilt || styleLoaded || loaderWaits; }
    // "total T ms: prep a, page b (built), own c, textures d (k loaded), blend e, normals f, style g (loaded), constants
    // h, post i (k decal textures loaded), copy j, gpu timer k; thread CPU t[; loader waits n (w ms)]"
    std::string str() const;
};

// Ticket 0714 (frame pacing B2, terrain-bake-slow-first-use.md 9): what warmPages() did.
struct PageWarmReport {
    uint32_t tiles = 0;      // tiles given
    uint32_t skipped = 0;    // tiles outside the map (not counted as a failure)
    uint32_t pages = 0;      // distinct pages those tiles use
    uint32_t built = 0;      // pages built by this call (the others were built already)
    double ms = 0;           // the whole call (steady clock)
    double threadMs = 0;     // this thread's CPU in it
};

// Issue 0417: what warm() did.
struct WarmReport {
    bool ran = false;
    Status status;           // the first failure (warm() goes on after one), else Ok
    double ms = 0;           // the whole warm-up
    double texturesMs = 0, stylesMs = 0, bakesMs = 0, gpuWaitMs = 0;
    uint32_t textures = 0;   // layer texture names bound (loaded or recorded as missing)
    uint32_t styles = 0;     // styles loaded
    uint32_t bakes = 0;      // throwaway bakes made
    std::string str() const; // one line for a log
};

struct PipelineConfig {
    const gamefs::GameFiles* fs = nullptr;           // the player's install (layer textures, style cubes, LUT); not owned
    const shaders::ShaderSection* exe = nullptr;     // the EXE's .shader section; not owned
    // Sources; null = the EXE's rule (makeExe*, makeStockBakeConstants). Not owned; they must outlive the baker.
    GridNormalSource* normals = nullptr;
    SlopeClassSource* slopes = nullptr;
    HeightPageSource* heightPages = nullptr;
    BakeConstantsSource* constants = nullptr;
    StyleSource* styles = nullptr;                   // null: MapInputs::plotArtTypes (tileStyle), else MapInputs::style
    bool specMaps = true;                            // the EXE's 0xD90F28 (TerrainSpecMaps; 1 in every reference run)
    uint32_t largestCacheSize = 512;                 // 0xD90F2C (cb0 [0].xy of the blend pass)
    // BakedTile::gpuMs from timestamp queries (issue 3404, wave 3). Blocking: the time of this bake; the CPU waits for
    // the GPU at the end of every bake. Deferred: never waits; gpuMs is the newest finished earlier bake's time (0 until
    // one has finished), read with DONOTFLUSH from a ring of 16 query sets. Off: 0.
    GpuTiming gpuTiming = GpuTiming::Blocking;
    // Decals (epic 0166, 4105). On: init makes a c5r::decal::DecalPass beside the post passes (the EXE's
    // TerrainDecalShader blobs from cfg.exe, textures through cfg.fs) and its failure fails init. Off: init still tries
    // (decalStatus() says how it went) but never fails for it; a bake whose request carries decals then fails BadLayout
    // when there is no decal pass. Either way a request without decals bakes as before, bit for bit.
    bool decals = true;
    const decal::DecalShaderVas* decalVas = nullptr;  // null: the build's VAs (c5r/decal/decal.h); tests give others
    // Where the decal pass reads its textures by name; null = fs. The server gives a view with the mods' folders above
    // the install (VP's farm fields, terrain-decals.md 8.3 and 12), so that the layer textures keep fs's view.
    const gamefs::GameFiles* decalFs = nullptr;
    // Issue 0417. The warm-up (WarmUp above) and the tables whose texture sets it loads (the server gives its
    // TerrainDef.csv tables: every map's layers come from them); AtInit without tables loads the styles only.
    // warmSizeClass: the size class of the throwaway bakes (the residency's, 256 in the server). A warm-up that
    // fails is logged and recorded (warmReport()); it never fails init.
    WarmUp warm = WarmUp::Off;
    const LayerTables* warmTables = nullptr;
    uint32_t warmSizeClass = 256;
    // Issue 0417: a log sink (one line per call, no newline; null = none). A bake of slowBakeMs or more of CPU is logged
    // with its steps ("pipeline: slow bake: ..."; the first 32), and the first firstUseLogs bakes that made a first use
    // (a texture, a style, a height page) are logged with theirs ("pipeline: first use: ..."). The warm-up's report too.
    void (*log)(void* user, const char* line) = nullptr;
    void* logUser = nullptr;
    double slowBakeMs = 500.0;
    int firstUseLogs = 64;
    // Ticket 0698: LoadAhead above. Thread: init starts the loader (after the warm-up, when that runs) and queues the five
    // styles; setMap queues every layer texture name of the map's tables (each layer id's set: diffuse, height map, spec
    // map). On a D3D11_CREATE_DEVICE_SINGLETHREADED device the loader reads and the textures are made at adoption, on the
    // caller's thread. A loader that cannot start is logged and the bakes read as with Off.
    LoadAhead loadAhead = LoadAhead::Off;
    // Ticket 0712: PrepAhead above. Thread: the worker starts at the first prepRequest after init. prepCacheProducts: the
    // cache's bound (least recently made or used evicted first; about 0.33 MB a product at 67 x 67 vertices).
    // prepVerify (TEST ONLY): every hit is computed inline too and compared; a mismatch is logged (LOUD) and the inline
    // products are used.
    PrepAhead prepAhead = PrepAhead::Off;
    uint32_t prepCacheProducts = 32;
    bool prepVerify = false;
};

// What the last bake used and saw (for the comparator's explanations).
struct PipelineTileInfo {
    LayerList list;
    TileWindow window;
    bool needSecond = false;        // what the bake used
    bool needSecondByList = false;  // the list rule alone (a listed layer's set has a spec map)
    uint32_t style = 0;
    int32_t pageOffsetX = 0, pageOffsetY = 0;
    std::string normals, slopes, heightPage, constants, styleSource;  // names of the sources used
};

class PipelineBaker : public TileBaker {
public:
    explicit PipelineBaker(const PipelineConfig& cfg);
    ~PipelineBaker() override;
    PipelineBaker(const PipelineBaker&) = delete;
    PipelineBaker& operator=(const PipelineBaker&) = delete;

    // Checks: no device or context, no install view (cfg.fs) or shader section (cfg.exe), largest cache size not
    // 64/128/256/512 [BadLayout]; the blend and post passes' own checks (shaders missing NotFound, reflection BadLayout,
    // D3D11 Device, E_OUTOFMEMORY OutOfMemory).
    Status init(ID3D11Device* dev, ID3D11DeviceContext* ctx) override;
    // Checks: before init [NotFound]; grid or tables null [BadLayout]; checkGrid's checks (material bytes missing
    // NotFound, sizes BadLayout); tables.layerCount 0 [BadLayout]; style >= 5 [BadLayout]; plotArtTypes given with
    // plotsX != grid.mapW or plotsY 0 or above grid.mapH [BadLayout]; fillTerrainLayers' checks. On failure the previous
    // map is dropped (bake then fails with NotFound). MapInputs' pointers must outlive the map.
    Status setMap(const MapInputs& in) override;
    // Epic 0465, ticket 4909 (live-terrain.md 8.14.2): the map's grid had its samples changed in place inside boxes
    // (boxCount quads x0, y0, x1, y1, exclusive ends; null or 0: anywhere, e.g. a field re-applied whole or a new load
    // whose grid has the old one's address). The height pages built from those samples are dropped, so the next bake
    // builds them from the edited grid (HeightPageSource::heightsChanged). Call it before the next bake.
    void heightsChanged(const heightfield::HeightGrid& g, const uint32_t* boxes, size_t boxCount);
    // Ticket 0714 (frame pacing B2, terrain-bake-slow-first-use.md 9): builds now the height pages of `tiles` (count tiles
    // of the map set by setMap), as the first bake of each page would, so that the bakes after it build none (a page build
    // was 3.7-12.8 ms of a bake on the laptop, 2026-11-23, the only first use left in a load's bakes after 0642 and 0698).
    // A page is a function of the grid alone, so the bakes' outputs are the same bytes with or without this call. Pages
    // already built are kept; a tile outside the map is skipped and counted. The pages stay until heightsChanged or a
    // grid at another address. Checks: before init or setMap [NotFound]; tiles null with count > 0 [BadLayout]; the page
    // source's errors (the first is returned; the other tiles' pages are still built). Not logged: the caller says it.
    Status warmPages(const TileId* tiles, size_t count, PageWarmReport* report = nullptr);
    // Ticket 0714: height pages built so far (by bakes and by warmPages); 0 with a page source of the caller's
    // (PipelineConfig::heightPages).
    uint64_t pagesBuilt() const;
    // Checks: before init or setMap [NotFound]; size class not 64/128/256/512 [BadLayout]; tile outside the map
    // [NotFound]; a target array that is not BC1 (TYPELESS / UNORM), not sizeClass^2 or not cacheMips(sizeClass) mips,
    // or a slice past its size [BadLayout]; then the checks of the list builder, the vertex fill, the sources, the
    // texture loads (a name the install lacks NotFound), the passes (E_OUTOFMEMORY OutOfMemory). t8 is made when a
    // listed layer's set has a spec map or req.areaChanged (tile+0x300). Outputs stay null / false on failure.
    Status bake(const BakeRequest& req, BakedTile& out) override;

    // The same with a style forced (0..4; -1 keeps the rule), for the comparator's style probe. needSecond 0 / 1
    // replaces the t8 rule (-1 keeps it). Checks: those of bake; needSecond not -1..1 or style not -1..4 [BadLayout].
    struct Overrides {
        int needSecond = -1;
        int style = -1;
    };
    Status bakeWith(const BakeRequest& req, const Overrides& ov, BakedTile& out);

    const PipelineTileInfo& lastTile() const;
    // Issue 0417: the steps of the last bake (also of a failed one, up to the step that failed), and counters over every
    // bake since init: slow bakes (>= cfg.slowBakeMs) and bakes with a first use.
    const PipelineStepTimes& lastSteps() const;
    uint64_t slowBakes() const;
    uint64_t firstUseBakes() const;

    // Issue 0417: the warm-up. Loads every layer texture of `tables` (each set's diffuse, height and spec map; a name the
    // install lacks is recorded as missing, as a bake would), the five styles' inputs, and makes throwaway bakes on a
    // small map of its own at `sizeClass`: every style with and without t8 and one with a decal, so that every shader
    // and state is used on the device once; then waits (at most 10 s) for the GPU to finish them. The map set by setMap
    // is dropped (a map set before warm() must be set again). Checks: before init [NotFound]; size class not
    // 64/128/256/512 [BadLayout]. A failing step is recorded in the report and the next one runs; the first failure is
    // returned. tables may be null: then only the styles are loaded (a bake needs a table's textures).
    Status warm(const LayerTables* tables, uint32_t sizeClass);
    const WarmReport& warmReport() const;

    // Ticket 0698: the loader (LoadAhead::Thread). loaderOn: it runs. loaderPending: names and styles queued, in flight,
    // or finished and not taken yet. adoptLoaded: takes what the loader finished (every bake does it first; a caller may
    // do it at idle); returns how many. waitLoader: adopts until nothing is pending or timeoutMs passed (tests, tools);
    // true when nothing is pending. stopLoader: ends the thread (queued names are dropped: a later bake reads them
    // itself); init and the destructor stop it. loaderLoads: the loader's files in the order taken (a style: one entry,
    // its three files together, named "style <n>").
    bool loaderOn() const;
    size_t loaderPending() const;
    size_t adoptLoaded();
    bool waitLoader(double timeoutMs);
    void stopLoader();
    const PipelineLoaderCounters& loaderCounters() const;
    const std::vector<BlendTextureCache::Load>& loaderLoads() const;

    // Ticket 0712: prep ahead (PrepAhead above). prepOn: PrepAhead::Thread with the pipeline's own normal and slope
    // sources, after init. prepRequest: the tiles of `lane` (0 or 1; the worker takes lane 0 first) the caller expects to
    // bake next, in that order; the call replaces the lane's queued jobs (not those in flight or made); hints already
    // cached, in flight or listed twice are skipped; at most prepCacheProducts per lane are kept. Before setMap, or with
    // prepOn false: nothing (0). Returns the jobs queued. prepInvalidate: the join and clear of every writer of a prep input
    // (the grid's samples or material bytes, the tables): the queues are dropped, a job in flight is waited for (its
    // product discarded) and the cache is emptied; the worker reads nothing until the next prepRequest, so the caller may
    // write after it returns. Called on the caller's thread; setMap and heightsChanged call it themselves. prepStop ends
    // the worker (init, warm and the destructor call it); a later prepRequest starts it again.
    bool prepOn() const;
    size_t prepRequest(int lane, const PrepHint* hints, size_t n);
    void prepInvalidate();
    void prepStop();
    PrepCounters prepCounters() const;

    // The style inputs (cubes, LUT) are loaded per style on first use and kept.
    size_t texturesLoaded() const;
    // Layer texture names the install lacks, bound as the EXE's handle 0 (BlendTextureCache, blend.h; e.g.
    // generic_gibraltar_h.dds on the VP install of 2026-11-13).
    const std::vector<std::string>& missingTextures() const;
    // The decal pass made in init (null when its init failed or before init), its init's status and its counters (draws,
    // missing textures, bad decals; zero without a pass).
    decal::DecalPass* decalPass() const;
    const Status& decalStatus() const;
    const decal::DecalCounters& decalCounters() const;

private:
    struct Impl;
    Impl* p_ = nullptr;
};

}  // namespace bake
}  // namespace c5r
