// c5r/liveterrain/producer.h - the server's region producer of heights and materials for live terrain
// (ticket 4903, epic 0465; way 3 of research/07-architecture-decisions/architect-2026-11-18-live-terrain-route.md 3.3).
//
// Our own code, written from research/05-graphics-composition/live-terrain.md section 7 (the EXE's hex pattern
// matcher, the piece library TerrainPiecesLibrary.fxsb and the block blend, described in words; D83). The piece
// library is read at run time from the player's install. Interface: live-terrain.md 8.1; design as built: 8.2-8.7.
//
// Coordinates: plot (x, y) as the DLL numbers them; sample (c, r) of the dense field of 64 W x 64 (H + 4) samples
// (asset-pipeline.md 4.2 point 5); the pattern grid is W x (H + 4) cells, cell (x, y + 2) for plot (x, y) (7.5).
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "c5r/heightfield/heightfield.h"
#include "c5r/status.h"

namespace c5r { namespace liveterrain {

const int kMaxPlotsSide = 256;     // larger maps are Unsupported
const int kRingSamples = 64;       // width of the ring over which w falls from 1 to 0 (8.1) [I: first choice]
const int kMaxWindowPlots = 256;   // a larger window waits for the reload (TooLarge)
const int kRiverKeepSamples = 60;  // (Options::riverKeepDisc only, the 4908 rule) kept radius around a river-edge plot's
                                   // centre: its hex plus about 25 samples beyond (8.9) [I]
const int kFeatherSamples = 32;    // w falls to 0 over this many samples towards kept (water, marsh) samples (8.9) [I]
// The river band (ticket 0717, research/05-graphics-composition/live-terrain-mountain-partial.md 3): the EXE's river carve
// changes the field only within about 24 world units of a river edge [C, offline on 4902's Tiny and huge taps]. Within
// kRiverCoreWorld of the edge line the current ground is kept; from there to kRiverReachWorld the new ground rises in
// linearly (a raise only: lowered ground is drawn as the change gives it, the carve only lowers). The core 2 and the
// linear rise to 24 follow the reload's own profile of a mountain beside a river [L, the live check of 2026-11-23,
// live-terrain-mountain-partial.md 7]; was 6 (from the flat-land lowering medians alone).
const float kRiverCoreWorld = 2.0f;
const float kRiverReachWorld = 24.0f;
const float kRowWorld = 0.866f;    // world units per sample row (sample columns are 1.0; the grid's sy of every map seen)
const int kCoastNearSamples = 128; // a land/water change redraws a changed coast line within this distance of a changed
                                   // plot's centre (4913, live-terrain.md 8.17)
// (no seam-plot exclusion: the 4902 seam residual was closed by the per-level list reset, 8.3; a window that crosses
// the x wrap seam still waits, reason Seam)

// ---------------------------------------------------------------- piece library (live-terrain.md 7.3)
struct Piece {
    uint32_t index = 0;
    int level = 0;
    uint8_t flags = 0;             // bit 0: 128-sample textures, else 512; bit 2: becomes a matcher pattern
    uint32_t tex[4] = {0, 0, 0, 0}; // row-table starts: height (u16 runs), material, height weight, material weight
    uint16_t region = 0;           // 0 land, 1 coast, 2 ocean
    std::string name;
    struct Cell { uint8_t c, r; uint32_t flags; uint8_t continent; uint8_t claimed; };
    std::vector<Cell> used;        // used cells in the order the matcher tests them (c outer, r inner)
    int size() const { return (flags & 1) ? 128 : 512; }
};

class PieceLibrary {
public:
    // Reads the whole file. Errors: Io, BadVersion (first u32 not 20), BadLayout / Truncated (sizes, row tables).
    static Status open(const std::wstring& path, std::shared_ptr<const PieceLibrary>& out);
    static Status fromBytes(std::vector<uint8_t> bytes, std::shared_ptr<const PieceLibrary>& out);
    const std::vector<Piece>& pieces() const { return pieces_; }
    int maxLevel() const { return maxLevel_; }
    // Decoded textures of a piece (size x size, row-major), decoded on first use and cached. t: 0 height, 1 material,
    // 2 height weight, 3 material weight. Thread-safe.
    const uint16_t* heights(uint32_t piece) const;
    const uint8_t* bytes(uint32_t piece, int t) const;
    // true when some piece's material texture holds value m (materials no piece makes come from later writers)
    bool makesMaterial(uint8_t m) const { return makes_[m]; }
private:
    Status parse();
    std::vector<uint8_t> file_;
    const uint32_t* rows_ = nullptr; uint32_t nRows_ = 0;
    const uint8_t* runs8_ = nullptr; uint32_t nRuns8_ = 0;
    const uint16_t* runs16_ = nullptr; uint32_t nRuns16_ = 0;
    std::vector<Piece> pieces_;
    int maxLevel_ = 0;
    bool makes_[256] = {};
    struct Cache;
    std::shared_ptr<Cache> cache_;
};

// ---------------------------------------------------------------- map frame (the EXE's grid constants, 7.5)
struct MapFrame {
    int plotsW = 0, plotsH = 0;    // the DLL's map size
    bool wrapX = false;
    float h2w[16] = {};            // hex (q, r, s, 1) -> sample position, row vectors (0xD91030)
    float corner[4][4] = {};       // the corner hexes of the 128 and 512 rectangles (0xD910F0..0xD91120)
    int32_t G[16] = {}, Ginv[16] = {};   // cube -> pattern-grid frame and back (0xD911D0, 0xD91210)
    int gridW() const { return plotsW; }
    int gridH() const { return plotsH + 4; }
    int samplesX() const { return plotsW * 64; }
    int samplesY() const { return (plotsH + 4) * 64; }
    // Parses the EXE's grid constants block 0xD90FBC..0xD9132C (880 bytes) and the map size in it.
    // Errors: Truncated (n < 880), BadLayout (sizes 0 or the grid size not W x (H + 4)), Unsupported (> 256 plots).
    static Status fromConstantsBlock(const uint8_t* p, size_t n, MapFrame& out);
};

// ---------------------------------------------------------------- plots
struct PlotInfo {
    heightfield::PlotInfo base;    // terrain, hills, mountain, feature, natural wonder
    int continentArt = 0;
    uint8_t riverEdges = 0;        // bit 0 W of river, bit 1 NW of river, bit 2 NE of river: a river on the plot's east,
                                   // south-east, south-west edge (CvPlot::IsRiverSide). With Options::riverEdgesFromAnyFlag
                                   // any non-zero value means only "a river on some edge" (the feed's flag)
    bool lake = false;
    bool isMarsh() const;          // feature FEATURE_MARSH (by type name; id 2 when the name is empty)
};
struct PlotTable {
    int w = 0, h = 0;
    std::vector<PlotInfo> plots;   // index y * w + x
};

// ---------------------------------------------------------------- tap
struct TapView {
    const uint16_t* heights = nullptr;   // samplesX * samplesY, x fastest
    const uint8_t* materials = nullptr;
    int samplesX = 0, samplesY = 0;
};

uint64_t deriveSeed(const MapFrame& f, const PlotTable& atLoad);

// ---------------------------------------------------------------- unknown natural wonders (ticket 0613, live-terrain.md 8.13.2)
// A natural wonder unknown to the EXE: a land plot's natural wonder whose Type the EXE's pattern-flag table does not name
// and which is not FEATURE_FOUNTAIN_YOUTH or FEATURE_EL_DORADO (those keep the terrain's value). The EXE gives such a plot
// Mount Fuji's pattern flag 0x40000 (heightfield::patternCellFlags, P7). An unnamed wonder (featureType empty: the server
// cannot tell) and a wonder on a coast or ocean plot are never counted: the stock rule stays for them.
bool unknownToExe(const heightfield::PlotInfo& p);
// The pattern cell value of a plot. neutralUnknownWonders false: heightfield::patternCellFlags (the EXE's rule, Fuji for an
// unknown wonder). true: an unknown wonder (unknownToExe) gets the plot's own terrain value instead (the neutral base).
uint32_t cellFlags(const heightfield::PlotInfo& p, bool neutralUnknownWonders);

// ---------------------------------------------------------------- result
enum class WaitReason { None = 0, NoChange, Water, Lake, RiverEdge, Marsh, Seam, TooLarge };
const char* waitReasonName(WaitReason r);

struct WindowRect {
    int x0 = 0, y0 = 0, w = 0, h = 0;    // samples, x already wrapped into 0..samplesX-1
    std::vector<uint16_t> heights;       // w * h
    std::vector<uint8_t> materials;
};

struct ChangeResult {
    bool live = false;
    WaitReason reason = WaitReason::None;
    std::string reasonText;
    std::vector<std::pair<int, int>> changedPlots;
    std::vector<WindowRect> window;
    uint64_t samplesD = 0, samplesFull = 0, samplesRing = 0, samplesOutside = 0;
    uint32_t removedPlacements = 0, addedPlacements = 0;
    double ms = 0;
    // land/water changes (4913, live-terrain.md 8.17; only with enableLandWater): the change moved a coast
    bool landWater = false;
    bool landRegion = false;              // (0717) a land change drawn through the region fill and the strips of the
                                          // current coast lines (Options::landRegion); no coast moved: landWater stays false
    uint64_t samplesRiverCore = 0, samplesRiverBand = 0;   // (0717) window samples in the river band's core / its rise
    uint32_t coastLinesRedrawn = 0;      // lines whose pre-jitter shape changed (drawn inside the window)
    uint32_t coastLinesBeyondWindow = 0;  // of those, lines that also move farther than the window: a reload moves
                                          // them elsewhere too (named, not drawn)
    uint32_t coastLinesReloadMoves = 0;   // unchanged lines whose jitter a reload would shift (named, not drawn)
    uint64_t samplesWonderKept = 0;       // window samples kept on natural wonders' own hexes (Options::protectWonders)
    // MC #9538 item 1 (live-terrain.md 8.10.3): zero-length coast segments or tangents the curves of this change stepped
    // over (CoastCurves::degenerate of the live set, with the new build's step-4 cases), and strip triangles refused for
    // a vertex that is not finite. Both 0 on every recorded input; the server logs one LOUD line per change with any.
    uint32_t coastDegenerate = 0;
    uint64_t stripTrianglesRefused = 0;
};

struct CoastStyles;
struct StripMaterials;

struct Placement {
    uint32_t piece = 0;
    uint8_t transform = 0;
    int32_t ax = 0, ay = 0, az = 0;      // anchor cube coordinates
};

class Producer {
public:
    Producer();
    ~Producer();
    Producer(Producer&&) noexcept;
    Producer& operator=(Producer&&) noexcept;
    static Status create(std::shared_ptr<const PieceLibrary> lib, const MapFrame& frame, const PlotTable& atLoad,
                         const TapView& tap, uint64_t seed, Producer& out);
    Status change(const PlotTable& after, ChangeResult& out);
    // Offline diagnostics only (the pictures of live-terrain.md 8.5): draws a window that the clean-window rule would
    // hold back (reasons Water, Lake, RiverEdge, Marsh), with the reason kept in reasonText. Never for the product.
    // protectWater / protectMarsh (4908, live-terrain.md 8.9; both on by default): keep the samples of coast, ocean and
    // margin hexes and of material 37 (water), and of marsh plots and of materials no piece makes (marsh), from the
    // current field, with w feathered to 0 next to them, in place of waiting for the reload. A change of a plot's
    // land/water class still waits (Water).
    // protectRivers (on by default): for a change that keeps every plot's land/water class, the river band (ticket 0717):
    // the samples within kRiverCoreWorld of a river edge keep the current ground and a raise rises in to
    // kRiverReachWorld; not on hexes that are water. A land/water change keeps the 4908 disc: every sample within
    // kRiverKeepSamples of a river-edge plot's centre, with the kFeatherSamples feather, not on the changed plots, not
    // where the coast moved, and not on hexes that are water after the change (w7-ridge, live-terrain.md 8.17).
    // riverKeepDisc (off by default): the disc for every change (the rule before 0717; with landRegion off as well, the
    // producer before 0717 bit for bit).
    // riverEdgesFromAnyFlag (off by default): PlotInfo::riverEdges is the feed's "a river on some edge" flag (server.exe
    // today gives every such plot 0x7); an edge counts as a river edge when both plots beside it carry the flag.
    // landRegion (on by default; only with enableLandWater, ticket 0717): a change that keeps every plot's land/water
    // class is drawn as a land/water change is (the region fill of the current coast lines, the blend, the coast
    // strips), so the coast beside it is drawn as a reload draws it; off: the 4908 water keep and its feather.
    // protectWonders (on by default; land/water changes only, 4913 item (b), live-terrain.md 8.17): the samples of a
    // natural wonder's own hexes keep the current field (the load's ground) during a land/water change, unless the
    // wonder's own plot is one of the changed plots; the coast around them is redrawn and feathered towards them.
    // Offline diagnostics only (w7-ridge, live-terrain.md 8.17 "Finding: a ridge of the old shore"): per sample of the
    // (unsplit) window, row-major from (x0, y0), the weight before and after the feather and why a sample is kept.
    struct SampleDiagnostics {
        int x0 = 0, y0 = 0, w = 0, h = 0;
        std::vector<float> weightFull, weight;   // wt before the feather (1 on the full set, the ring), wt as written
        std::vector<uint8_t> why;                // 0 not kept, 1 natural wonder, 2 river keep, 3 marsh, 4 water (not class mode),
                                                 // 5 not kept: a hex that is water after a land/water change (no river keep)
        std::vector<uint8_t> regionBefore, regionAfter;   // class mode only: the region bytes of the window
        std::vector<uint8_t> moved;              // class mode only: 1 where the step treats the sample as moved coast
        std::vector<std::pair<float, float>> riverCentres;   // the river-edge plot centres the keep used (samples; riverKeepDisc),
                                                             // else the midpoints of the river edges the band used
        std::vector<float> riverFactor;          // the band's factor per sample (1 outside it, 0 in its core)
    };
    struct Options { bool diagnosticIgnoreClean = false; bool protectWater = true; bool protectMarsh = true; bool protectRivers = true;
                     bool protectWonders = true;
                     bool riverKeepDisc = false;          // ticket 0717: the 4908 river keep in place of the band
                     bool riverEdgesFromAnyFlag = false;  // ticket 0717: riverEdges is the feed's any-edge flag
                     bool landRegion = true;              // ticket 0717: land changes through the land/water step's region
                     SampleDiagnostics* diagnostics = nullptr;   // offline tools only; nullptr in the product
                     int diagnosticRingSamples = 0;              // offline tools only: > 0 replaces kRingSamples (a test run)
                     // offline tools and the negative test only: true restores the river keep on hexes that are water
                     // after a land/water change (before w7-ridge; the old shore then stays as a ridge, 8.17)
                     bool diagnosticRiverKeepOnWater = false;
                     // ticket 0613 ([Extras] option id 14, live-terrain.md 8.13.2 (b)): the new table's cells are built with
                     // the neutral base for unknown wonders (cellFlags(.., true)). create always builds the EXE's rule (the
                     // tap is the EXE's bake), so the first change with this on, given the load's own table, re-draws every
                     // unknown wonder of the load (the load-time pass); later changes keep it on. Off: today's producer bit
                     // for bit.
                     bool neutralUnknownWonders = false;
    };
    Status change(const PlotTable& after, ChangeResult& out, const Options& opt);
    // Land/water changes (ticket 4913, live-terrain.md 8.17). Call once after create: builds the load's coastline
    // curves (4910, from the load's plot table and mapRandomSeed) and the coast strips' meshes (4911). From then on a
    // change that turns a plot's land/water class (land to water, water to land, a lake added) goes live instead of
    // waiting with Water: the window holds the new region fill, the blend with the coast and ocean pieces and the
    // strips. Without this call such a change waits, as before. Errors: from buildCoastCurves / buildStripMesh;
    // BadLayout when called before create.
    Status enableLandWater(const CoastStyles& styles, const StripMaterials& materials, uint32_t mapRandomSeed);
    bool landWaterEnabled() const;
    // MC #9538 item 1: CoastCurves::degenerate of the load's curves (enableLandWater); 0 before it
    uint32_t loadCoastDegenerate() const;
    // Inspection for tests and tools.
    const std::vector<Placement>& placements() const;
    // The own blend of the current placement set over the whole map (heights, materials), as 7.4 describes it.
    Status ownField(std::vector<uint16_t>& heights, std::vector<uint8_t>& materials) const;
    const std::vector<uint16_t>& currentHeights() const;
    const std::vector<uint8_t>& currentMaterials() const;
    uint64_t matcherDraws() const;
private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}} // namespace c5r::liveterrain
