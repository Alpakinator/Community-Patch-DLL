// c5r/liveterrain/coaststrips.h - the EXE's coast strips (CreateTerrain step 6) for live terrain (ticket 4911, epic
// 0615).
//
// Our own code, written from research/05-graphics-composition/live-terrain.md 8.15 (the strip mesh, the two-wave raster,
// the CoastStyles.xml profiles and the TerrainDef material rows, described in words; D83). Nothing here comes from the
// EXE's or the game's files: the coast styles and the TerrainDef table are read from the player's install at run time
// by the caller (W1's CoastStyles, c5r::bake::loadTerrainDef).
//
// Input: per coast, the five offset lines that step 3 leaves (0x73C170; W1's CoastLine::mesh[0..4], ticket 4910). Output:
// the strips' heights and material bytes written into a window of a field that already holds the block blend.
//
// Not modelled, by design (8.15): the EXE's "special segment" (it has no effect on the field). specialPick() reproduces
// its two rand() draws for the stream count only. Lakes have no step of their own: their shores are coasts like any
// other (8.15 "Lakes").
#pragma once
#include <cstdint>
#include <vector>
#include "c5r/status.h"

namespace c5r {
namespace bake { struct LayerTables; }
namespace liveterrain {

const int kStripStyleKinds = 13;     // the EXE's coast-style names (0xB9A064): Default .. DefaultShallows
const uint8_t kNoMaterial = 0xFF;    // a vertex or band without a material
const uint16_t kMountainSwapMax = 35000;  // the mountain-top swap applies at new heights <= this (0x88B8)

// One point of an offset line, as 0x73C170 leaves it (16 bytes in the EXE): sample coordinates, the blend fraction
// and the two coast-style kinds. Same shape as W1's CoastMeshPoint.
struct StripPoint {
    float x = 0, y = 0, t = 0;
    uint8_t styleA = 0, styleB = 0;
};

// The five offset lines of one coast (lines 0..4: high side, high beach, waterline, low beach, low side). All five
// have the same point count L (8 n - 14 for a source spline of n points).
struct StripLines {
    std::vector<StripPoint> line[5];
};

// One coast style's profile (8.15 table): per line its weight and height; per band its terrain code (-1: none).
struct StripStyle {
    float weight[5] = {0, 0, 0, 0, 0};
    float height[5] = {0, 0, 0, 0, 0};
    int bandTerrain[4] = {0, 0, 0, 0};
};

// The 13 kinds. A kind that is not present behaves as the EXE's all-zero profile (weights and heights 0, band codes 0).
struct StripStyles {
    bool present[kStripStyleKinds] = {};
    StripStyle kind[kStripStyleKinds];
};

// The TerrainDef table as the EXE holds it: row k (= material byte k) has id (0x10 + continent) << 16 | id, the CSV's
// id column read as hexadecimal. A row without an id (the appended Checkerboard set) holds 0xFFFFFFFF.
struct StripMaterials {
    std::vector<uint32_t> rowId;
    // The row of id, or -1.
    int find(uint32_t id) const;
    // A band terrain code as the strip builder resolves it: 0x110000 | code, else 0x100000 | code; kNoMaterial when
    // neither exists or code is -1.
    uint8_t resolveBand(int code) const;
    // The mountain-top swap pairs for continents 1..4 (rows of id | 0x35 -> id | 0x10); from[c] = kNoMaterial when the
    // continent lacks the target row (the EXE's rule: then it takes no part).
    void swapPairs(uint8_t from[4], uint8_t to[4]) const;
    // From c5r::bake's TerrainDef tables (sets in row order; the appended set without a row gets 0xFFFFFFFF).
    static Status fromLayerTables(const bake::LayerTables& t, StripMaterials& out);
};

// One vertex of the strip mesh (20 bytes in the EXE).
struct StripVertex {
    float x = 0, y = 0;
    uint8_t material = kNoMaterial;
    float h = 0, w = 0;
};

// The strip mesh of one coast: 8 vertices per column, 8 triangles per quad (two per band), segments of c triangles.
struct StripMesh {
    std::vector<StripVertex> vertices;
    std::vector<uint32_t> tris;          // 3 vertex indices per triangle, in the EXE's order
    std::vector<uint32_t> segmentEnd;    // exclusive end triangle of each segment
    uint32_t segmentTris = 0;            // c
    float minX = 0, minY = 0, maxX = 0, maxY = 0;   // the vertices' bounding box (empty mesh: all 0)
    uint32_t triangles() const { return uint32_t(tris.size() / 3); }
    uint32_t segments() const { return uint32_t(segmentEnd.size()); }
};

// Builds the mesh of one coast (0x4B2100 > 0x55F9F0). Errors: BadLayout (the five lines differ in length). Lines of
// fewer than 3 points give an empty mesh (no triangles). A style kind without a profile (not present, or 13 and above)
// takes the EXE's all-zero profile.
Status buildStripMesh(const StripLines& lines, const StripStyles& styles, const StripMaterials& mats, StripMesh& out);

// The edge weights (e01, e12, e02) of triangle i of a strip (the same for every quad).
void stripEdgeWeights(uint32_t triangle, float out[3]);

// A window of a dense field (row-major, x fastest) that the strips write into. x0 + w <= samplesX, y0 + h <= samplesY
// (a window crossing the x seam is two calls). Samples outside the window are neither read nor written; a sample's
// result depends only on its own value and the triangles covering it, so a window gives the same samples as the whole
// field.
struct StripField {
    uint32_t samplesX = 0, samplesY = 0;
    bool wrapX = false;
    uint32_t x0 = 0, y0 = 0, w = 0, h = 0;
    uint16_t* heights = nullptr;   // w * h
    uint8_t* materials = nullptr;  // w * h
};

struct StripStats {
    uint64_t trianglesDrawn = 0;   // triangles that passed the area test and reached the window's rows
    uint64_t samplesWritten = 0;   // sample writes inside the window (a sample covered twice counts twice)
    uint32_t meshesTouching = 0;   // meshes whose box reaches the window
    uint64_t trianglesRefused = 0; // triangles with a vertex that is not finite or a box larger than the field: not drawn
                                   // (MC #9538 item 1, live-terrain.md 8.10.3; 0 on the recorded inputs)
};

// The raster (0x639D40 > 0x6990D0) of the given meshes, in the EXE's order: every even segment of every mesh (mesh
// order), then every odd segment. meshes must be in the EXE's coast order (W1's CoastCurves::lines order). Errors:
// BadLayout (window outside the field, null arrays).
Status applyStrips(const std::vector<StripMesh>& meshes, const StripMaterials& mats, StripField& field,
                   StripStats* stats = nullptr);

// Whether a mesh's box reaches a window (x wrapped on a wrapping map). Meshes that do not can be left out of
// applyStrips without changing a sample.
bool stripTouches(const StripMesh& m, const StripField& field);

// The EXE's two rand() draws of step 6 (MSVCR90's generator, state advanced in place): rand() % meshes picks a coast,
// then (rand() % (segments / 2)) * 2 an even segment of it. Returns false with no draws for no meshes; returns false
// after the first draw when the picked coast has fewer than 2 segments (the EXE divides by zero there). The pick has
// no effect on the field (8.15); a reload draws again from its own stream position.
struct SpecialPick { uint32_t coast = 0, segment = 0; };
bool specialPick(const std::vector<StripMesh>& meshes, uint32_t& randState, SpecialPick& out);

}} // namespace c5r::liveterrain
