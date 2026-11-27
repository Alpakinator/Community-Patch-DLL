// c5r/forest/placement.h - where the trees of a forest or jungle plot stand, as the EXE's Forest system places them
// (epic 0431, ticket 4550: route A, package T1).
//
// Facts it rests on: research/05-graphics-composition/trees.md 2.2 (life cycle), 2.3 (the placement rule), 8.1 (the
// layout and record forms), 10 (this module as built, with the static readings and the offline proof). Own code written
// from those descriptions of behaviour (D83); forests.xml and the four L8 masks are read from the player's install at run
// time through c5r::gamefs, nothing of the game's files is in the repository. Pure CPU: no D3D.
//
// The pieces, in the order the EXE uses them:
//   1. ForestParams from forests.xml (the copy the EXE resolves: Vibrant Textures' loose Assets/DLC/VT/forests.xml in
//      every install we test, D94), tree counts rounded to the nearest multiple of 90.
//   2. TileMask: the four L8 128 x 128 masks forest_tile1..4.dds (the hex shape).
//   3. Layouts: 20 per session, layout k from mask k mod num_tiles, each N trees placed by the rule of trees.md 2.3 with a
//      CRT-compatible rand() (CrtRand, seed given by the caller), sorted by square y, descending. Or layouts given
//      directly (the forest tap's arrays, for the offline proof).
//   4. PlotRecord: a forest or jungle plot (kind 1 forest, 2 jungle, 3 / 4 the same on hills), its layout index
//      (rand() % 20 when the plot gets its trees) and its cube coordinates.
//   5. buildPlot: per plot its N / 90 blocks of 90 float4 (x, y, z, scale) = the ForestShader's cb7, with the hide rules
//      (cut bits 0-2, z below the water height 10.1167, slope normal z < 0.9; scale 0) and 0.75 x size on kinds 3 / 4;
//      plus the drawn trees with their shadow quads (half-size 6.0, 3.6 on kinds 3 / 4) for the Tree_Shadow decal (4552).
// Placement holds 3 and 4 for a scene (4554), which feeds it plain plot inputs (no server header is needed here).
//
// Frames: "sample frame" = the output of (hx, hy, hz, 1) x M_h2w (the EXE's 0xD91030), where the shadow decal's quads
// and rectangle live [L]; "world" = x * sx - ox, y * sy - oy (HexToWorld 0x6851C0, the cb7 x / y) [C].
// Errors: every call returns a c5r::Status; nothing throws. A failed call leaves its output empty or the object as it was.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "c5r/forest/crtrand.h"
#include "c5r/forest/cut.h"
#include "c5r/status.h"

namespace c5r { namespace gamefs { class GameFiles; } }
namespace c5r { namespace heightfield { struct HeightGrid; } }

namespace c5r { namespace forest {

const uint32_t kTreesPerBlock = 90;         // one cb7 block = one DrawIndexedInstanced(2160, 1) [C, L]
const uint32_t kLayoutCount = 20;           // layouts made at game start [C]
const uint32_t kMaxTreesPerPlot = 4096;     // design 9.5: a rounded count outside 90..4096 is refused
const uint32_t kMaxTiles = 64;              // num_tiles accepted (the tap's own bound on layouts)
const uint32_t kMaskEdge = 128;             // L8 128 x 128 [C, L]
const float kSquare = 96.0f;                // the placement square's edge (sample units) [C]
const uint32_t kZeroSizeAfter = 350;        // trees with index > 350 get size 0 [C 0x5A3BB0]
const uint32_t kTriesPerTree = 20;          // tries for the distance rule [C]
const float kSlopeMinNormalZ = 0.9f;        // 0x9FCEC8 [C]
const float kHillScale = 0.75f;             // 0xA0A4AC, kinds 3 / 4 [C]
const float kShadowHalf = 6.0f;             // 0xA04FC4 [C]
const float kShadowHillFactor = 0.6f;       // 0x9D9DB0 [C]
// The water height the rule compares with: 0.2f * 50.583656f as the EXE multiplies it (0xB9A6DC x 0xA00638) [C].
float waterHeight();

// ---- 1. forests.xml ----
struct ForestParams {
    uint32_t numTiles = 0;
    uint32_t randomTrees = 0;     // rounded to the nearest multiple of 90 (VT 200 -> 180, base 360 -> 360)
    uint32_t griddedTrees = 0;    // rounded the same way; only 0 is supported (its placement was not read)
    uint32_t rawRandom = 0, rawGridded = 0;   // as written in the file
    float overlayAlpha = 0.0f;
    float spaceBetween = 0.0f;
    uint32_t treesPerPlot() const { return randomTrees + griddedTrees; }
    uint32_t blocksPerPlot() const { return treesPerPlot() / kTreesPerBlock; }
};
// The EXE's rounding (0x5DC3C0): float(n) x float(1/90), half away from zero, x 90.
uint32_t roundTo90(uint32_t n);
// Parses the <Forest .../> element. Errors: BadLayout (no Forest element; an attribute missing, not a number or
// negative; num_tiles 0 or above kMaxTiles; the rounded random count outside 90..kMaxTreesPerPlot; overlay_alpha or
// space_between not finite, space_between negative), Unsupported (a gridded count that rounds above 0).
Status parseForestsXml(const char* text, size_t size, ForestParams& out);

// ---- 2. the masks ----
struct TileMask {
    std::vector<uint8_t> texels;   // kMaskEdge * kMaskEdge, row 0 = the file's first row, x fastest
    uint32_t nonZero = 0;
};
// Errors: the DDS parser's (Truncated, BadMagic, BadLayout, Unsupported); Unsupported when the file is not L8 ("Incorrect
// tile texture format (L8 required)", as the EXE stops); BadLayout when not 128 x 128 or no texel is set (the EXE's loop
// would never end).
Status parseTileMask(const uint8_t* dds, size_t size, TileMask& out);
// The texel rule of 0x5A3BB0 [C]: u = trunc(x x (1/96) x 127), v = trunc((1 - y x (1/96)) x 127); accepted when u and v
// are below 128 and the file's texel at index v x 128 + u - 1 is not 0 (the EXE's copy is one byte later than the file:
// trees.md 10.3, [L] on 7,200 tapped trees; u = v = 0 is refused).
bool maskAccepts(const TileMask& m, float x, float y);

// Reads forests.xml and forest_tile1..num_tiles.dds through the game's file system. origin (optional) receives where
// forests.xml came from (GameFiles::origin). Errors: NotFound (a file, named), the parsers' errors.
Status loadInstall(const gamefs::GameFiles& fs, ForestParams& params, std::vector<TileMask>& masks,
                   std::string* xmlOrigin = nullptr);

// ---- 3. layouts ----
// CrtRand (MSVC's rand(), the caller seeds it): c5r/forest/crtrand.h, shared with the draw (ticket 4554).

// One tree of a layout: the EXE's 28 bytes (trees.md 8.1). offset: its place relative to the plot in the units
// HexToWorld takes (added to the record's first two cube coordinates; offset[2] = -(offset[0] + offset[1])); one: 1.0;
// square: its point in the 96 x 96 placement square; size: 0.5-1.0, 0 = not drawn.
struct Tree {
    float offset[3] = {0, 0, 0};
    float one = 1.0f;
    float square[2] = {0, 0};
    float size = 0.0f;
};
using Layout = std::vector<Tree>;

// The square point's offset as the EXE stores it: the sample-frame point (M_h2w's origin + (x - 48, y - 48)) taken back
// through the inverse of M_h2w (the EXE uses its own M_w2h 0xD90FF0: equal to 6e-7 on the tapped layouts [L]).
// Errors: BadLayout (the matrix not finite or not invertible).
Status squareToOffset(const float hexToWorld[16], float x, float y, float offset[3]);
// One layout by the rule (trees.md 2.3, 10): N = params.treesPerPlot() trees, then sorted. Errors: BadLayout (params
// with no trees, a mask of the wrong size or empty, the matrix as squareToOffset; more than 100,000 x N draws without
// filling the layout).
Status buildLayout(const ForestParams& params, const TileMask& mask, const float hexToWorld[16], CrtRand& rng,
                   Layout& out);
// The 20 layouts of a session: layout k from masks[k % numTiles]. Errors: BadLayout (masks.size() != numTiles), and
// buildLayout's.
Status buildLayouts(const ForestParams& params, const std::vector<TileMask>& masks, const float hexToWorld[16],
                    CrtRand& rng, std::vector<Layout>& out);
// The sort of 0x6021D0: square y descending; ties keep their order (the EXE's introsort is not stable: the order of two
// trees with the same y may differ from the EXE's [I]).
void sortLayout(Layout& layout);
// Checks layouts given from outside (a tap): 1..kMaxTiles layouts, each exactly treesPerPlot trees, every value finite,
// size 0 or in [0.5, 1]. Errors: BadLayout, naming the layout and tree.
Status checkLayouts(const std::vector<Layout>& layouts, uint32_t treesPerPlot);

// ---- 4. plot records ----
enum class Kind : uint8_t { None = 0, Forest = 1, Jungle = 2, ForestHills = 3, JungleHills = 4 };
// The kind of a plot from the feed's facts (P1, trees.md 10.1: kind 3 / 4 = plotType hills, exact on both saves).
Kind kindOf(bool forest, bool jungle, bool hills);
inline bool isHills(Kind k) { return k == Kind::ForestHills || k == Kind::JungleHills; }
inline bool isJungle(Kind k) { return k == Kind::Jungle || k == Kind::JungleHills; }

struct PlotRecord {
    int32_t plotX = 0, plotY = 0;
    int32_t cube[3] = {0, 0, 0};   // hx = x - (y >> 1), hy = y, hz = -(hx + hy) [C ToHexFromGrid, L P1]
    Kind kind = Kind::None;
    uint32_t layout = 0;
};
// From a grid plot. Errors: BadLayout (kind None).
Status makeRecord(int32_t plotX, int32_t plotY, Kind kind, uint32_t layout, PlotRecord& out);
// From the tap's cube coordinates. Errors: BadLayout (kind outside 1..4, cube coordinates that do not sum to 0).
Status recordFromCube(const int32_t cube[3], uint32_t kind, uint32_t layout, PlotRecord& out);

// ---- 5. per plot ----
struct Instance {   // one float4 of cb7
    float x = 0, y = 0, z = 0, scale = 0;
};
struct HideCounts {
    uint32_t sizeZero = 0, cut = 0, water = 0, slope = 0, drawn = 0;   // a tree is counted under its first reason
};
struct DrawnTree {
    uint32_t index = 0;                 // in the layout
    float sampleX = 0, sampleY = 0;     // centre in the sample frame (the shadow quad's frame)
    float worldX = 0, worldY = 0, z = 0;
    float scale = 0;                    // the cb7 scale (size, x 0.75 on kinds 3 / 4)
};
struct PlotBlocks {
    uint32_t blocks = 0;
    std::vector<Instance> instances;    // blocks * 90, block b = instances [90 b, 90 b + 90)
    HideCounts counts;
    std::vector<DrawnTree> drawn;       // the trees drawn, in layout order (one shadow quad each)
    float shadowHalf = 0;               // 6.0, or 3.6 on kinds 3 / 4
    float shadowRect[4] = {0, 0, 0, 0}; // min x, min y, max x, max y of the quads (sample frame); 0 when none drawn
    uint64_t cutGeneration = 0;         // the cut source's generation the blocks were built at
};
struct BuildOptions {
    bool slopeRule = true;              // false: no slope rule (comparison arms only)
};
// The normal z of the slope rule at sample (c, r): the EXE's 5 x 5 edge-preserving gradient (0x5E1300's behaviour,
// trees.md 10.2); x wraps on a wrapping map, samples outside the grid take the centre's height [I]. Returns 1 for a grid
// without samples.
float slopeNormalZ(const heightfield::HeightGrid& g, int32_t c, int32_t r);
// Builds one plot (trees.md 2.3, 10.4). Errors: BadLayout (the grid without samples, hexToWorld not finite, the layout
// shorter than params' treesPerPlot or empty, a record of kind None, treesPerPlot not a multiple of 90, a value not
// finite in the output).
Status buildPlot(const heightfield::HeightGrid& g, const ForestParams& params, const Layout& layout,
                 const PlotRecord& rec, const CutSource& cut, const BuildOptions& opt, PlotBlocks& out);

// ---- the session's state, for a scene ----
// What a scene knows of one plot from its plot table (SrvFeed::PlotRecord-derived; the scene maps the feature ids).
struct PlotInput {
    int32_t x = 0, y = 0;
    bool forest = false, jungle = false, hills = false;
};

class Placement {
public:
    // Stock's start: params and masks from the install, 20 layouts with CrtRand(seed); later layout indices continue the
    // same random stream. Errors: loadInstall's and buildLayouts'. On failure the placement stays empty.
    Status init(const gamefs::GameFiles& fs, const float hexToWorld[16], uint32_t seed, std::string* xmlOrigin = nullptr);
    Status initWith(const ForestParams& params, const std::vector<TileMask>& masks, const float hexToWorld[16],
                    uint32_t seed);
    // Layouts given directly (the forest tap): checked by checkLayouts. layoutSeed seeds the stream for later setPlot.
    Status initWithLayouts(const ForestParams& params, std::vector<Layout> layouts, uint32_t layoutSeed = 1);
    bool ready() const { return !layouts_.empty(); }
    const ForestParams& params() const { return params_; }
    const std::vector<Layout>& layouts() const { return layouts_; }

    // A plot gains, keeps or loses its trees. A plot that is neither forest nor jungle loses its record; a plot whose kind
    // is unchanged keeps its layout and blocks; otherwise it gets a record with layout rand() % layout count (stock's
    // listener). Errors: NotFound (not ready), BadLayout (x or y negative or above 65535, forest and jungle both set).
    Status setPlot(const PlotInput& p);
    // The same with a given layout (the tap's records). Errors: as setPlot, BadLayout (layout past the count).
    Status setPlotWithLayout(const PlotInput& p, uint32_t layout);
    void removePlot(int32_t x, int32_t y);
    void clearPlots();
    // Ticket 0495 (the offline scene harness's forest.bin): the layout stream's state (the next setPlot's rand()), read and
    // set back as it was.
    uint32_t streamState() const { return rng_.state(); }
    void setStreamState(uint32_t s) { rng_ = CrtRand(s); }
    const PlotRecord* record(int32_t x, int32_t y) const;
    size_t recordCount() const { return records_.size(); }
    std::vector<PlotRecord> records() const;

    // The plot's blocks, built on first use (stock builds a plot the first time it is in a view) and again when the cut
    // source's generation moved or after invalidate. *out points into the placement until the plot changes. Errors:
    // NotFound (no record at x, y; not ready), buildPlot's.
    Status blocks(const heightfield::HeightGrid& g, const CutSource& cut, int32_t x, int32_t y, const PlotBlocks** out,
                  const BuildOptions& opt = BuildOptions());
    // Drop built blocks (a new height field: all; a plot's own change: that plot).
    void invalidateAll();
    void invalidate(int32_t x, int32_t y);
    size_t builtCount() const;

private:
    struct Entry {
        PlotRecord rec;
        bool built = false;
        PlotBlocks blocks;
    };
    static uint64_t key(int32_t x, int32_t y) { return (static_cast<uint64_t>(static_cast<uint32_t>(y)) << 32) |
                                                      static_cast<uint32_t>(x); }
    Status put(const PlotInput& p, bool haveLayout, uint32_t layout);

    ForestParams params_;
    std::vector<Layout> layouts_;
    CrtRand rng_;
    std::map<uint64_t, Entry> records_;
};

}} // namespace c5r::forest
