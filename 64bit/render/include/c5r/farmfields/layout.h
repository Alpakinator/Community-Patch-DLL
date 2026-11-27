// c5r/farmfields/layout.h - the stages of the farm layout (epic 0528, ticket 4852): the EXE's streamline farm layout as
// research/05-graphics-composition/farm-fields-layout.md sections 1-8 describe it (D83: own code from that text; nothing
// from the EXE). layoutFarms (farmfields.h) runs them; this header exposes each stage for the tests.
//
// Frames: "layout units" (L) = the decal sample frame with y divided by kLayoutYScale (farm-fields-layout.md, head):
// plots 64 apart in x, 55.43 between rows. Every length of the text (step 0.65, spacings 13.6 / 4.0, uv scale 25.6, the
// margin 5.0, the track half-width 0.25) is in L.
//
// The one deliberate departure from stock (D186, ticket 4852; farm-fields-layout.md 13): stock grows one shared graph
// farm by farm, so its picture depends on the order farms arrive in and is random per session. Here the graph is laid out
// per fixed BLOCK of kBlockPlots x kBlockPlots plots (with a margin of kBlockMargin L around it), from the game's seed and
// the block's index only, independent of which plots are farms. A farm's fields are the faces of its block's graph whose
// centroid lies on the farm plot, so the same save gives the same fields on every load, whatever the order, and a farm
// built, pillaged or removed changes only its own fields and the tracks of the cells around it. Seams: faces of two
// blocks meet without shared lines along the block borders (every kBlockPlots plots) [I: a narrow strip of mismatched
// edges there].
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>
#include "c5r/farmfields/farmfields.h"
#include "c5r/status.h"

namespace c5r { namespace farmfields {

// ---- constants of the text ----
const double kLayoutYScale = 1.1547344;      // sample y = layout y x this (head of the file)
const double kBaseAngle = 0.65572;           // A = atan(1 / 1.3) (section 7)
const double kNoiseScale1 = 1.0 / 150.0;     // 0.0066667, amplitude pi / 8
const double kNoiseScale2 = 1.0 / 40.0;      // 0.025, amplitude pi / 16
const double kTraceStep = 0.65;              // 0.5 x h, h = 1.3 (3.2)
// The parameter block as corrected by 4850's re-read (farm-fields-layout.md 3.2-3.4, 2026-11-18 02:35; the first reading
// was one slot off: 13.6 spacing, radii 13.6 / 4.0 / 9.6, 2.4 and 14.4 unused).
const double kOffsetSeedSpacing = 14.4;      // arc length between offset seeds (3.3)
const double kSepPoint = 13.6;               // separation radius, point seeds (kind 0), against every line
const double kSepOffset = 13.6;              // offset seeds (kind 1), against the own family
const double kSepContinuation = 4.0;         // continuation seeds (kind 2), against the own family's other tags
const double kStopDistance = 2.4;            // a step ending this close to a same-family line (other tag) ends the trace
const double kNearZone = 9.6;                // within this, the line stops at its next crossing of any edge
const double kOffsetStart = 0.65;            // offset seed start beside the line
const double kSeedPeriod = 64.0;             // the seed tile's period
const int kSeedTilePoints = 50;
const double kMergeTolSq = 0.004;            // collinear merge (squared distance)
const int kMaxFaceEdges = 11;                // a walk abandoned at 12 edges (3.6)
const double kUvScale = 25.6;                // uv = R(a)((p - c) / 25.6 + 0.5) (4.2)
const double kMarginPush = 5.0;              // field-margin push (mask bit 0x10)
const double kTrackHalfWidth = 0.25;
const double kTrackVStep = 1.3;
// ours
const int kBlockPlots = 4;                   // a block: 4 x 4 plots
const double kBlockMargin = 48.0;            // traced beyond the block's plots, so its faces close
const int kMaxTracePoints = 4000;            // per half line, a safety bound (a window's diagonal is ~700 steps)

// ---- the direction field (section 7) ----
// Standard 2D simplex noise (skew (sqrt 3 - 1) / 2, unskew (3 - sqrt 3) / 6, 12 gradients, Ken Perlin's reference
// permutation, final scale 70), in about -1..1.
double simplex2(double x, double y);
// Ken Perlin's reference permutation (256 entries), for the tests.
const uint8_t* perlinPermutation();
// The per-map noise offset o: stock takes it from the terrain system (writer not found, 12); ours from the game's seed.
struct FieldParams { double ox = 0, oy = 0; };
FieldParams fieldParamsFor(uint32_t gameSeed);
// angle(p) = A + (pi / 8) N(p / 150 + o) + (pi / 16) N(p / 40 + o), p in L.
double fieldAngle(const FieldParams& f, double lx, double ly);

// ---- plot geometry in L ----
// The plot's centre in L (plotCentre's sample frame with y / kLayoutYScale).
void plotCentreLayout(int32_t x, int32_t y, double out[2]);
// The plot whose centre is nearest (the hex under the point), false when it is off the map.
bool plotAt(double lx, double ly, uint32_t mapW, uint32_t mapH, int32_t& x, int32_t& y);

// ---- the seed tile (3.3): 50 points in a 64 x 64 square, x then y each rand() % 1000 x 0.064, from the game's seed ----
std::vector<double> seedTile(uint32_t gameSeed);

// ---- one block's graph ----
struct GraphEdge {
    uint32_t v0 = 0, v1 = 0;      // junctions
    uint8_t family = 1;           // 1 the field's direction, 2 its perpendicular
    std::vector<double> pts;      // x, y in L, from v0 to v1 (collinear points merged)
    bool live = true;
};
struct GraphFace {
    std::vector<double> pts;      // x, y in L: the closed polygon, counter-clockwise, collinear points merged
    std::vector<uint32_t> edges;  // the edges it walks
    double area = 0;              // > 0
    double cx = 0, cy = 0;        // area-weighted centroid
};
struct BlockGraph {
    int32_t bx = 0, by = 0;
    double window[4] = {0, 0, 0, 0};     // L: the traced rectangle (min x, min y, max x, max y)
    std::vector<double> vertices;        // x, y per junction
    std::vector<GraphEdge> edges;        // after pruning (3.5): every live edge lies on a cycle
    std::vector<GraphFace> faces;        // the bounded faces of at most kMaxFaceEdges edges (3.6)
    uint32_t lines = 0, seedsTried = 0, seedsAccepted = 0;   // statistics
};
// The block's rectangle in L: the box of its plots' hexes (+-32 in x, +-37 in y around each centre) grown by
// kBlockMargin, clipped to `bounds` (the world in L). False when nothing is left.
bool blockWindow(int32_t bx, int32_t by, const double bounds[4], double out[4]);
// The parameters of the layout (3.2-3.4). Defaults: the text's values; the tests and render/evidence/0528 vary them.
// contArc: stock traces a line only inside one farm's 128 x 110.85 rectangle and goes on in a later farm's call from
// its dangling end through a continuation seed (kind 2). A half line can be cut into pieces of contArc L of arc length,
// each end a continuation seed taken first, to stand for those rectangles; with the text's distance stops the cut
// changes nothing measurable (render/evidence/0528/text_values.txt: median area 246-252 for 48 .. no cut), so the
// default is no cut (every half line traced to the window's edge, the text's rule inside one rectangle).
// History: before 4850's re-read a calibrated sepOffset of 11.1 stood in for the misread 4.0
// (render/evidence/0528/calibration.txt, superseded).
struct LayoutParams {
    double sepPoint = kSepPoint;
    double sepOffset = kSepOffset;
    double sepContinuation = kSepContinuation;
    double offsetSpacing = kOffsetSeedSpacing;
    double stopDistance = kStopDistance;
    double nearZone = kNearZone;
    double contArc = 1e30;
};
// Lays out block (bx, by): point seeds from the seed tile inside the window, queued in a random order (rand() % k);
// offset seeds every 14.4 along each new line, on one side; separation tests; traces (forward and backward) with the
// distance stops (2.4 ends the trace and drops the piece since the last junction, 9.6 makes the line stop at its next
// crossing), stops at a line of the own family, crossings of the other; pruning of dead ends; the faces. Deterministic
// in (gameSeed, bx, by, bounds).
Status layoutBlock(uint32_t gameSeed, const FieldParams& f, int32_t bx, int32_t by, const double bounds[4], BlockGraph& out,
                   const LayoutParams& params = LayoutParams());

// The block graphs layoutFarms made, by (by, bx), for one game seed and world (cleared when either changes).
struct BlockCache {
    uint32_t gameSeed = 0;
    double bounds[4] = {0, 0, 0, 0};
    bool valid = false;
    std::map<std::pair<int32_t, int32_t>, BlockGraph> graphs;
    uint64_t hits = 0, misses = 0;
    unsigned threads = 0;   // threads that lay out the missing blocks (0: the hardware's less one, 1 to 8)
};

// ---- triangulation (4.2 step 2): ear clipping from index 0 as the farm manager does; a convex polygon gives the fan
// from vertex 0. xy: n points (x, y); false when a pass finds no ear (no decal is made). tris: 3 x (n - 2) indices.
bool earClip(const double* xy, size_t n, std::vector<uint32_t>& tris);
bool earClip(const float* xy, size_t n, std::vector<uint32_t>& tris);

// ---- the field's look (4.2 steps 3-5) ----
// The texture category of a farm by state, wheat and art type; false when it draws no field (art type 0, stock: a
// continent other than 1-4). Half-built: Crops_Half_Built (Crops_Wheat_HB when wheat); wheat: Crops_Wheat; else the
// continent's crops.
bool fieldCategory(const FarmPlot& p, uint32_t& category);
// uv of a point p (L) of a field with centroid c and angle a: R(a) ((p - c) / 25.6 + 0.5), R the rotation about the uv
// origin (row-vector form: u = x cos a - y sin a, v = x sin a + y cos a).
void fieldUv(double px, double py, double cx, double cy, double a, float& u, float& v);

}} // namespace c5r::farmfields
