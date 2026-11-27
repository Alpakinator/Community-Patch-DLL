// c5r/liveterrain/coastlines.h - the EXE's coastline curves and its region fill, ported (ticket 4910, epic 0615).
//
// Our own code, written from research/05-graphics-composition/live-terrain.md 8.10 and 8.10.1 (D83). Nothing here is
// taken from the EXE: the description is the source. The coast styles and character splines are read at run time
// from the player's install (TerrainModels.fpk: coaststyles.xml and the CoastCharacterSpline_*.txt files).
//
// What it makes, per load (8.10.1 steps 1-8): the coast lines of the whole map from the plot table, the map frame and
// mapRandomSeed, as the EXE's CreateTerrain makes them: one point array, the boundary ranges (the waterlines, value
// 0 land/coast or 1 coast/ocean) and the fill ranges (the offset lines, values type and type + 1). From those, per
// 64 x 64-sample block, the region byte the block blend tests (8.10 steps 1-5, 8.10.1 step 9): 0 land, 1 coast,
// 2 ocean.
//
// Frames as producer.h: plot (x, y) as the DLL numbers them, the pattern grid W x (H + 4), samples 64 W x 64 (H + 4).
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "c5r/liveterrain/producer.h"
#include "c5r/status.h"

namespace c5r { namespace liveterrain {

struct CoastPoint { float x = 0, y = 0; };

// ---------------------------------------------------------------- coast styles (8.10.1 step 7)
// One CoastStyle element as the EXE keeps it (25 values), lines 0..4 = high side, high beach, waterline, low beach,
// low side: offset along the normal (-HighSide, -HighBeach.Offset, Waterline.Offset, LowBeach.Offset, LowSide),
// flag (0, 1, 1, 1, 0), height (HighBeach, HighBeach, Waterline, LowBeach, LowBeach .Height) and character spline
// index (-1, HighBeach, Waterline, LowBeach .Character, -1; 65535 in the file = -1).
struct CoastStyle {
    float offset[5] = {}, flag[5] = {}, height[5] = {};
    int character[5] = {};
    int terrainHigh = -1, terrainLow = -1;   // HighBeach / LowBeach TerrainType (65535 = -1), as read (not remapped)
};

struct CoastStyles {
    static const int kKinds = 13;            // the styles' order: Default, Grass, ..., Mountain, DefaultShallows
    bool present[kKinds] = {};
    CoastStyle style[kKinds];                // a kind without an element reads as all zeros (the EXE's lookup)
    std::vector<std::vector<CoastPoint>> characters;   // 98 entries each, in SplineCharacter order (step 7)
    const CoastStyle& lookup(int kind) const;          // all zeros for an absent or out-of-range kind
    // Parses coaststyles.xml; readFile(name) returns a character spline file's text (false when missing).
    // Errors: BadLayout (no style list, a style without its elements, a spline with fewer than 98 points, a
    // character index past the list), NotFound (a spline file missing).
    static Status parse(const std::string& xml, const std::function<bool(const std::string&, std::string&)>& readFile,
                        CoastStyles& out);
    // The character table of one spline file's text (8.10.1 step 7, the loader).
    static Status characterTable(const std::string& text, std::vector<CoastPoint>& out);
};

// A point of the coast mesh's lines as 0x73C170 leaves them (8.10.1 step 7): samples x, y; the blend fraction t
// between style A and style B; the two style kinds (A: this point's, B: the previous one's).
struct CoastMeshPoint { float x = 0, y = 0, t = 0; uint8_t styleA = 0, styleB = 0; };

// ---------------------------------------------------------------- the curves (8.10.1 steps 1-8)
struct CoastRange { int32_t start = 0, end = 0, value = 0; };   // points [start, end) of CoastCurves::points

struct CoastLine {                 // one extracted polyline (step 5), in rand order
    int type = 0;                  // 0 land/coast, 1 coast/ocean
    bool closed = false;
    int startX = 0, startY = 0, startSlot = 0;   // the start edge: pattern-grid cell and slot
    uint32_t points = 0;           // its point count after extraction (the count its draws depend on)
    uint32_t draws = 0;            // rand() calls of its jitter (step 6)
    uint64_t shape = 0;            // FNV-1a over type, closed and the extracted points before the jitter (4913: a line
                                   // whose shape is unchanged by a plot change is the same coastline)
    std::vector<CoastMeshPoint> mesh[5];   // the five lines of the coast mesh, 8 points - 14 each (step 7); line 2 is
                                           // the waterline that the region fill uses
    // Zero-length tangents met in this line's jitter (step 6) and coast mesh (step 7): two equal neighbouring points
    // give 1/0 there (the EXE's arithmetic would make NaN points). The port leaves such a point where it is (no move
    // along a normal it does not have) and counts it here (MC #9538 item 1, live-terrain.md 8.10.3). 0 on every map
    // checked against the taps (8.10.2), so the exact-port numbers do not depend on it.
    uint32_t degenerate = 0;
};

struct CoastCurves {
    std::vector<CoastPoint> points;          // CreateTerrain's array: waterlines, then offset lines (step 8)
    std::vector<CoastRange> boundary, fill;  // in the EXE's order
    std::vector<CoastLine> lines;
    uint64_t randDraws = 0;                  // all jitter draws
    uint32_t randStateAfter = 0;             // the CRT state after the jitter (the decal set-up draws next, 7.6)
    // MC #9538 item 1 (8.10.3): the zero-length cases the port stepped over instead of making NaN points: the lines'
    // own (CoastLine::degenerate), the offset points of step 8 not made for a zero-length waterline segment, and (only
    // from buildCoastCurves) the mountain pushes of step 4 left out for a zero tangent (degenerateSmooth, also in the sum)
    uint32_t degenerate = 0;
    uint32_t degenerateSmooth = 0;
};

// Builds the whole map's curves. seed = CvPreGame::mapRandomSeed (srand's argument). The plot table must be W x H of
// the frame. Errors: BadLayout (sizes), Unsupported (larger than kMaxPlotsSide).
Status buildCoastCurves(const MapFrame& frame, const PlotTable& plots, const CoastStyles& styles, uint32_t seed,
                        CoastCurves& out);

// Step 8 alone: the point array and the boundary and fill ranges of a given list of lines (in that order), as
// buildCoastCurves makes them. randDraws becomes the lines' sum; randStateAfter is left 0. (4913: a live curve set
// made of load-time lines and changed lines.) Errors: BadLayout (a line whose waterline has fewer than 2 points).
Status assembleCurves(std::vector<CoastLine> lines, CoastCurves& out);

// ---------------------------------------------------------------- the region bytes (8.10 steps 1-5, 8.10.1 step 9)
// Region bytes of the blocks [bx0, bx1) x [by0, by1) (block = 64 x 64 samples; bx in 0 .. W - 1, by in 0 .. H + 3),
// written row-major over the rectangle: out[(r - 64 by0) * 64 (bx1 - bx0) + (c - 64 bx0)] for sample (c, r).
// The plot table is the one the curves were built from. Errors: BadLayout (rectangle outside the map, sizes; a point
// of a range that is not finite or lies more than kRegionPointReach field sizes from the field: refused, so that the
// fill never walks a line from INT_MIN, MC #9538 item 1).
const int kRegionPointReach = 16;
Status regionBlocks(const MapFrame& frame, const PlotTable& plots, const CoastCurves& curves, int bx0, int by0,
                    int bx1, int by1, std::vector<uint8_t>& out);

// Convenience: the whole field's region bytes (64 W x 64 (H + 4), x fastest).
Status regionField(const MapFrame& frame, const PlotTable& plots, const CoastCurves& curves, std::vector<uint8_t>& out);

}} // namespace c5r::liveterrain
