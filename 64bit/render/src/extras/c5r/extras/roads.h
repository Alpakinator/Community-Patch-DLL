// c5r/extras/roads.h - FixRoadLayout's road and railroad generator (epic 0454, package X3, ticket 4933; design
// docs/extras/roads.md). Written 2026-11-19 for this repository (GPL-3.0-or-later). Our own design from the DLL's per-plot
// route state and the plot grid: nothing of the EXE's route builder is decoded or ported.
//
// The pipeline, each step a pure function of its input (so the same save gives the same layout on every load):
//   MapIn (one PlotIn per plot: the route band the active team sees, the optional city mark)
//     -> buildNetwork()  which neighbouring route plots the drawn road joins: every pair of adjacent route plots, less the
//                        edges that would close a ring around no plot without a route (the rule, roads.md 2), decided per
//                        triangle cluster in a fixed rank order
//     -> layOut()        per route plot the pieces of track through it (a through curve, spurs, a dead end, a stub), as
//                        centre lines in one of three styles, then strips of three vertex rows in the texture bands of the
//                        recorded route curves (RoadsandRails_new_*, priority 0.7: the server's existing decal pass draws
//                        them, so the engine's own drawing is reused)
//     -> composeRoadSet() the decal tap's set with its route curves dropped and ours added (the walk and area blocks
//                        carried, as the farm fields do)
//   roadChange() names the plots and rectangles that differ between two productions (the tiles to re-bake).
//
// Frames: "sample" = the height field's sample frame (64 per plot; plot (x, y) centred at (64 x + 32 + 32 (y & 1),
// 64 y + 128)); "iso" = the same with y scaled by kIsoY (0.866, the field's sy / sx), where hexes are regular. All
// geometry is made in iso and written out in sample.
//
// Errors: every call returns a c5r::Status; nothing throws; a failed call leaves its output empty.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "c5r/decal/decal.h"
#include "c5r/forest/cut.h"
#include "c5r/status.h"

namespace c5r {
namespace extras {
namespace roads {

// The texture band a plot's route is drawn with: what the recorded route curves use per route state (roads.md 1).
enum Band : uint8_t {
    kBandNone = 0,
    kBandRoad = 1,
    kBandRail = 2,
    kBandRoadTrade = 3,      // a road carrying a trade route (route state 6)
    kBandRailTrade = 4,      // a railroad carrying a trade route (route state 7)
    kBandRoadPillaged = 5,   // drawn with kBandRoad's band until the pillaged look is made (roads.md 5)
    kBandRailPillaged = 6,
    kBandCount = 7
};
bool isRail(Band b);

// A band from the feed's PlotRecord (server/src/feed/srv_feed.h): route (RouteTypes, -1 none) and routeState (the DLL's
// eRoadTypes: 0 road, 1 railroad, 2/3 pillaged, 4/5 under construction, 6/7 trade route, 8 none, -1 never seen) and the
// route-pillaged bit (flags bit 2). Under construction and none give kBandNone (stock draws no curve there either,
// extras.md X0.1); routeState -1 falls back to route (0 road, otherwise railroad).
Band bandFromFeed(int route, int routeState, unsigned flags);

struct PlotIn {
    uint8_t band = kBandNone;   // Band
    uint8_t city = 0;           // 1: a city stands on the plot (a hub: its edges rank first). The feed has no city mark yet
                                // (PlotRecord flags bit 4 is never set): 0 everywhere is valid and loses only that rule.
};

struct MapIn {
    uint32_t width = 0, height = 0;   // plots; at most kMaxPlotsPerAxis each
    bool wrapX = false;               // x wraps (neighbours across the seam)
    std::vector<PlotIn> plots;        // width * height, index y * width + x
};
const uint32_t kMaxPlotsPerAxis = 1024;

// ---- the grid ----
const float kIsoY = 0.866f;        // the field's sy / sx (terrain.cpp default; the hex frame's aspect)
const int kDirs = 6;               // 0 E, 1 NE, 2 NW, 3 W, 4 SW, 5 SE (counter-clockwise in iso; "N" = y + 1)
// Plot centre in the sample frame.
void plotCentre(int x, int y, float& sx, float& sy);
// The neighbour of (x, y) in direction d, without wrapping x (may be -1 or width); false when its row is off the map.
bool neighbourRaw(int x, int y, int d, int height, int& nx, int& ny);
// The same, x wrapped when wrapX; false when off the map (row, or column without wrap).
bool neighbour(const MapIn& m, int x, int y, int d, int& nx, int& ny);

// ---- the network ----
struct Network {
    uint32_t width = 0, height = 0;
    bool wrapX = false;
    std::vector<uint8_t> edges;   // per plot: bit d set = the drawn road joins the neighbour in direction d (symmetric)
    // statistics of the last build
    uint32_t routePlots = 0, candidateEdges = 0, edges0 = 0;   // edges in no triangle (always kept)
    uint32_t clusters = 0, maxClusterEdges = 0, rejected = 0;  // triangle clusters, the largest one's edges, edges left out
};
// Which of the candidate edges (every pair of adjacent route plots) the network keeps.
enum class Connect : uint8_t {
    Tidy = 0,   // the ring rule (roads.md 2): no ring around nothing; variant (e)'s network (FixRoadLayout 1)
    All = 1,    // all connections (ticket 4940, Civilization III style): every candidate kept, nothing left to infer
};
const char* connectName(Connect c);
bool connectFromName(const std::string& name, Connect& out);
Status buildNetwork(const MapIn& m, Network& out, Connect connect = Connect::Tidy);

// Every bounded face of the network encloses at least one plot without a route (the ring rule, roads.md 2). For tests:
// returns the number of faces that break it (0 = ring free) and the first offending face's plots in `where`.
uint32_t countEmptyFaces(const MapIn& m, const Network& n, std::vector<uint32_t>* where = nullptr);

// ---- the layout ----
enum class Style : uint8_t {
    Straight = 0,      // (a) straight from plot centre to plot centre, turns rounded with a fillet
    Spline = 1,        // (b) a smooth curve (cubic Hermite) through the plot centres
    SplineJitter = 2,  // (c) (b) through centres moved by a small fixed per-plot offset
    Line = 3,          // straight first (second candidate round): straight centre to centre, every turn a short fillet,
                       // a branch merging into the through-line with a short curve (or straight into it, mergeLength 0),
                       // a run cut short inside a cluster ending in a short curve, never a straight stop
};
const char* styleName(Style s);
bool styleFromName(const std::string& name, Style& out);

// The centre of a plot (not a city) with junctionMin or more drawn edges (ticket 4940: four to six under all connections,
// the design question for the maintainer's pick). Every treatment keeps the straight legs from the edges' middles, so a
// neighbour's half of each edge is the same whichever treatment its own centre uses.
enum class Junction : uint8_t {
    Through = 0,   // the style's own rule (roads.md 3, 4a): one through piece, the other edges spurs or merging branches
    Cross = 1,     // crossing straights: every opposite pair of edges one straight piece through the centre, any edge
                   // without its opposite a straight spoke to the centre (lines crossing in a point, as Civilization III)
    Ring = 2,      // a small ring (ringRadius) around the centre, every edge a straight spoke ending on it (a roundabout)
    Rosette = 3,   // every two angularly neighbouring edges joined by a short curve that bends round the centre (the
                   // style's fillet, its legs at most 60 % of the half edge); nothing passes through the centre itself
};
const char* junctionName(Junction j);
bool junctionFromName(const std::string& name, Junction& out);

struct LayoutOptions {
    Style style = Style::Spline;
    Connect connect = Connect::Tidy;    // the network produce() builds (layOut takes the network it is given)
    Junction junction = Junction::Through;
    uint8_t junctionMin = 4;            // the edges a plot needs for `junction` to apply (3 .. 6)
    float ringRadius = 9.0f;            // Junction::Ring: the ring's radius (iso samples, 4 .. 24)
    float filletRadius = 20.0f;     // (a): the radius of a turn's arc (iso samples), shrunk where the legs are short
    float tension = 0.85f;          // (b, c): the tangent's length at a centre as a share of the plot spacing (64)
    float jitter = 4.0f;            // (c): the largest offset of a plot's pass point (iso samples)
    float lineJitter = 0.0f;        // style Line (ticket 4940 part 1b): the same fixed per-plot offset of the pass point, so
                                    // runs wobble from plot to plot (0: none, the output byte for byte as before; at most 7: at
                                    // 8 a merging branch can cross its bent line, roads.md 4f)
    float subStep = 2.5f;           // the strip's vertex spacing along the centre line (iso samples)
    float stubLength = 16.0f;       // a route plot with no route neighbour: a short stub through its centre
    float cityStop = 0.0f;          // > 0: pieces into a city plot end this far from its centre (a ring of that radius)
    float mergeLength = 16.0f;      // style Line: the legs of a branch's curve into the through-line (0: straight into it)
    float leafCurve = 14.0f;        // style Line: the legs of the end curve of a run cut short inside a cluster (0: none)
    float priority = 0.7f;          // the route curves' priority (Main_Road's, terrain-decals.md 2.2)
};

// One texture band: the three v rows (edge, centre, edge), the u span one texture repeat uses, the repeat's length along
// the road and the half width of the strip (iso samples). Measured from the recorded route curves (roads.md 1).
struct BandLook {
    float v[3];
    float u0, u1;          // the strip's u runs from u1 at a repeat's start to u0 at its end, as recorded
    float repeat;          // samples per texture repeat
    float halfWidth;
};
const BandLook& bandLook(Band b);

// The route curves' textures (albedo, height, spec), resolved by c5r::gamefs as the tap's names are.
extern const char* const kRouteTextures[3];

struct RoadDecals {
    std::vector<std::string> textures;          // kRouteTextures
    std::vector<decal::Decal> decals;           // one per piece of a route plot (list order 0 .. n - 1)
    std::vector<decal::DecalVertex> vertices;
    std::vector<uint32_t> owner;                // per decal: its plot (y * width + x)
    // the centre lines, for tests and the diagrams: per decal its piece's points (iso), ended by a NaN pair
    std::vector<float> lines;
    std::vector<uint32_t> lineStart;            // per decal: index into `lines` (pairs), plus one past the end
    bool empty() const { return decals.empty(); }
};

// The pieces of every route plot as strip decals. `n` must be buildNetwork(m)'s. Plots whose band is kBandNone get none.
Status layOut(const MapIn& m, const Network& n, const LayoutOptions& o, RoadDecals& out);

// buildNetwork(m, net, o.connect) then layOut.
Status produce(const MapIn& m, const LayoutOptions& o, Network& net, RoadDecals& out);

// ---- the trees' cut mask (prepared for X3b, ticket 4934) ----
// The cells of the forest's cut grid our pieces cover, as a c5r::forest::CutSource (the farm footprint's pattern,
// c5r/farmfields/footprint.h): a cell (col, row) covers samples [2 col, 2 col + 2) x [2 row, 2 row + 2) (the cut rule of
// c5r/forest/cut.h); it is set when its sample point (2 col, 2 row) lies within the strip's half width + `margin` (iso
// samples) of a piece's centre line. The bit is kRoadFootprintBit [I: the bit the EXE's route stamps use is not known
// yet; any of kCutTreeBits hides a tree]. Independent of the style: it reads the pieces layOut produced.
const uint8_t kRoadFootprintBit = 0x02;
class RoadFootprint : public forest::CutSource {
public:
    // Errors: BadLayout (the world rectangle not finite or empty, more than 16384 cells on an axis, a table of `r` of the
    // wrong length, an owner outside the map). On an error the mask is empty.
    Status build(const MapIn& m, const RoadDecals& r, const float worldRect[4], float margin = 3.0f);
    void setFrame(float ox, float oy) { ox_ = ox; oy_ = oy; }
    uint8_t bitsAt(float worldX, float worldY) const override;
    bool cell(int32_t col, int32_t row) const;
    uint64_t generation() const override { return generation_; }
    void setGeneration(uint64_t g) { generation_ = g; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    uint64_t cellsSet() const { return cellsSet_; }
    uint64_t hash() const { return hash_; }   // FNV-1a 64 over the size and the bits (publish only when it moved)

private:
    uint32_t width_ = 0, height_ = 0, wordsPerRow_ = 0;
    float ox_ = 0, oy_ = 0;
    std::vector<uint64_t> bits_;
    uint64_t cellsSet_ = 0, hash_ = 0, generation_ = 0;
};

// ---- joining the tap's set ----
// A record of the tap's set is a route curve when its priority is 0.7 (+-1e-4) and its albedo's name contains
// "roadsandrails" (any case): the only key at 0.7 (terrain-decals.md 2.2).
bool isTapRouteCurve(const decal::DecalSet& set, const decal::Decal& d);

struct ComposeStats {
    uint32_t tapDecals = 0, tapRemoved = 0, roadDecals = 0, roadVertices = 0, namesAdded = 0;
    bool walk = false;
    uint32_t walkLarge = 0;
    bool area = false;
    uint32_t areaBitsSet = 0;
};
// A new set: the tap's decals in their order (its route curves left out when removeTapRoutes), then ours, list orders
// after the tap's, the walk (the kept decals' ranks closed up; ours as AddDecal would index them: small when neither side
// exceeds 256 samples, in the cell of the rectangle's centre, newest first) and the area block (ours mark their tiles)
// carried. Empty `roads` with removeTapRoutes: the tap's set without route curves. Errors: BadLayout (a decal of ours with
// a bad vertex run, texture index or rectangle outside the tap's world; an inconsistent walk).
Status composeRoadSet(const decal::DecalSet& tap, bool removeTapRoutes, const RoadDecals& roads, decal::DecalSet& out,
                      ComposeStats* stats = nullptr);

// ---- changes ----
// FNV-1a 64 of a decal's content (the tap reader's DecalContentHash rule: rectangle, priority, pass, the three names,
// the vertex bytes).
uint64_t contentHash(const decal::Decal& d, const std::vector<std::string>& names, const std::vector<decal::DecalVertex>& v);
struct RoadChange {
    std::vector<decal::DecalRect> rects;   // rectangles of decals in one production and not the other (by content)
    std::vector<uint32_t> plots;           // their owner plots, sorted, unique
};
Status roadChange(const RoadDecals& before, const RoadDecals& after, RoadChange& out);

// A whole-map check of the drawn centre lines (tests, the report): pieces of different plots that cross away from a
// shared point, the largest turn between consecutive centre-line steps (degrees) and dead ends farther than `tol` from
// their plot's (pass) centre.
struct LineCheck {
    uint32_t pieces = 0, crossings = 0, overshoots = 0;
    float maxTurnDeg = 0;
    float firstOvershoot[2] = {0, 0};   // iso point of the first overshoot (diagnosis)
    uint32_t firstOvershootPlot = 0;
    // the straight-first rule (second round), with the network given:
    uint32_t abruptEnds = 0;    // a run that stops straight (its last 12 iso samples turn less than 10 degrees) at a plot
                                // with one edge and a route neighbour the ring rule left out
    uint32_t longCurves = 0;    // curved stretches (every step turning more than 0.75 degree per iso sample) longer than
                                // curveMax
    float maxCurve = 0;         // the longest curved stretch (iso samples)
    float straightShare = 0;    // share of the centre lines' length that is straight
};
LineCheck checkLines(const MapIn& m, const RoadDecals& r, float tol = 1.0f, const Network* net = nullptr,
                     float curveMax = 40.0f);

// ---- bridges (ticket 0808, docs/extras/roads.md 4h) ----
// With the layout on, the EXE still draws its own CPU-built road and rail bridge at each river crossing of ITS route
// curves (Bridges.gr2's pieces bent along the crossing, the heading and the deck height in the vertices, the instance rows
// a translation; landmark-renderer.md 8, city-renderer.md 13.11). These calls re-place such a bridge onto our road's
// crossing of the same hex edge: the same mesh turned about z and moved in x / y, its height kept.

// Where the drawn road crosses the edge between plot (x, y) and its neighbour in direction d: the point on the edge (iso,
// around the unwrapped centre (cx, cy) of (x, y), the halfEdge rule both plots share) and the unit heading of the centre
// line there, from (x, y) towards the neighbour. False when the network does not join the two plots (or d is off the map).
bool edgeCrossing(const MapIn& m, const Network& n, const LayoutOptions& o, int x, int y, float cx, float cy, int d,
                  float& isoX, float& isoY, float& dirX, float& dirY);

// A bridge mesh's footprint in its model space: the centre of its extent along and across its long axis (x, y; z the mean)
// and the unit long axis (the principal axis of the vertices' x / y; the span of the bridge). From the vertex buffer the
// model layer holds: stride 20 (POSITION R16G16B16A16_FLOAT first) or 32 (POSITION R32G32B32_FLOAT first).
struct BridgeFit {
    float cx = 0, cy = 0, cz = 0;
    float ax = 1, ay = 0;         // unit long axis (x >= 0, or y > 0 when x is 0)
    float length = 0, width = 0;  // extent along and across the axis
    uint32_t vertices = 0;
};
// Errors: BadLayout (no data, a stride not 20 or 32, bytes not a multiple of it, a non-finite position, a mesh without a
// long axis: length not above width).
Status fitBridgeMesh(const uint8_t* vb, size_t bytes, uint32_t stride, BridgeFit& out);

enum class BridgeAction : uint8_t {
    Keep = 0,   // not at a hex edge of the map (off the map, or farther than kBridgeEdgeReach from every edge middle): drawn as is
    Move = 1,   // our road crosses this edge: rowsOut places the mesh on our crossing, along our heading
    Drop = 2,   // our layout does not join the two plots of this edge: no road at either end, so no bridge
};
const float kBridgeEdgeReach = 20.0f;   // iso samples from an edge's middle (a hex edge is 36.9 long; adjacent middles 32 apart)
struct BridgePlacement {
    BridgeAction action = BridgeAction::Keep;
    uint32_t plot = 0;              // the edge's plot (the lower of the two plot*6+dir keys) and direction
    uint8_t dir = 0;
    uint64_t edgeKey = 0;           // plot * 6 + dir + 1 (0: no edge, Keep)
    float edgeDist = 0;             // the bridge's centre from the edge's middle (iso samples)
    float shift = 0;                // Move: how far its centre moved (iso samples)
    float turnDeg = 0;              // Move: the turn about z (degrees, -90 .. 90)
    float rows[16] = {};            // Move: the new instance rows (row vectors: world = (x, y, z, 1) rows), else rowsIn
};
// rowsIn: the bridge's four instance rows as the tap holds them (row vectors; row 3 the origin); fit: fitBridgeMesh of its
// vertex buffer; ox, oy: the field's frame (iso = world + (ox, oy), the cut grid's rule, c5r/forest/cut.h). The mesh's turn
// is the smaller of the two that lay its axis on our heading (a bridge reads the same from either end).
BridgePlacement placeBridge(const MapIn& m, const Network& n, const LayoutOptions& o, const float rowsIn[16],
                            const BridgeFit& fit, float ox, float oy);

}  // namespace roads
}  // namespace extras
}  // namespace c5r
