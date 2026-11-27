// render/tests/forest/shadows_test.cpp - c5r::forest Tree_Shadow decals (epic 0431, ticket 4552).
//
// Unit tests: the record of one plot (corners, uv, order, the rectangle and its clamp, the binary16 positions, the hash,
// hills), a plot through buildPlot, the merge into a tap set (indices, names, list order, the walk and its ranks, the area
// block and the marks, the pass-0 lists untouched, the tap set untouched), the change (rectangles, bake tiles, area
// tiles); a negative for every refusal with its outputs and status.
//
// Proof modes (not run by ctest; render/evidence/0431/4552/):
//   c5r_forest_shadows.exe --proof IN OUT
//     IN: 4550's text form (grid, params, layout, record lines; proof_4550.py) plus optional lines "world X0 Y0 X1 Y1" (the
//     clamp rectangle; else the grid's), "dtap PATH" (a decal set to merge into) and "mark X0 Y0 X1 Y1" (rectangles of
//     forest decals dropped before this moment: their area tiles). OUT: per record its decal (quads, half, rectangle,
//     hash); with a dtap the merge's figures: area bits and tiles with decals before and after, pass-0 lists compared on
//     every tile, the merge's time. Exit 0 written, 2 usage, 3 input refused.
//   c5r_forest_shadows.exe --dtapcheck DTAP [DTAP ...]
//     Against a recorded (stock) set: (a) every small decal's walk point and cell recomputed by mergeShadows' rule (the
//     rectangle's centre in single precision, trunc((p - origin) x inverse)) against the EXE's; (b) every Tree_Shadow
//     decal's vertices against shadowDecal's form: the corner and uv pattern exact, and per quad a single-precision centre
//     that gives the EXE's binary16 positions with the module's arithmetic ((p - min) x (1 / extent), float, round to
//     nearest even), and, for contrast, with a division, a truncation and double precision.
//     Exit 0 all equal, 1 a difference, 3 input refused.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "c5r/bake/grid.h"
#include "c5r/decal/decal.h"
#include "c5r/forest/cut.h"
#include "c5r/forest/placement.h"
#include "c5r/forest/shadows.h"
#include "c5r/heightfield/heightfield.h"
#include "shadows_util.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::forest;

namespace {

const float kWorld[4] = {0.0f, 0.0f, 511.0f, 511.0f};   // 8 x 8 tiles

void nominalMatrix(float m[16])
{
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = 64.0f; m[5] = 64.0f; m[4] = 32.0f; m[12] = 32.0f; m[13] = 128.0f; m[15] = 1.0f;
}

heightfield::HeightGrid flatGrid(uint32_t mapW, uint32_t mapH, uint16_t h)
{
    heightfield::HeightGrid g;
    g.mapW = mapW;
    g.mapH = mapH;
    g.samplesX = mapW * 64;
    g.samplesY = mapH * 64;
    g.wrapX = true;
    g.sx = 1.0f;
    g.sy = 0.866f;
    g.sz = 0.2f;
    g.ox = mapW * 32.0f;
    g.oy = mapH * 32.0f * 0.866f;
    nominalMatrix(g.hexToWorld);
    g.h.assign(static_cast<size_t>(g.samplesX) * g.samplesY, h);
    return g;
}

uint16_t u16For(float z) { return static_cast<uint16_t>(z / 0.2f * 255.0f + 0.5f); }

ForestParams paramsN(uint32_t n, float space)
{
    ForestParams p;
    p.numTiles = 4;
    p.randomTrees = n;
    p.rawRandom = n;
    p.spaceBetween = space;
    p.overlayAlpha = 0.6f;
    return p;
}

Layout layoutOf(const ForestParams& p, uint32_t seed)
{
    float m[16];
    nominalMatrix(m);
    TileMask mask;
    mask.texels.assign(kMaskEdge * kMaskEdge, 255);
    mask.nonZero = kMaskEdge * kMaskEdge;
    CrtRand r(seed);
    Layout l;
    Status st = buildLayout(p, mask, m, r, l);
    if (!st.ok()) std::printf("layoutOf: %s\n", st.str().c_str());
    return l;
}

// Blocks with the given drawn-tree centres (sample frame) and half-size.
PlotBlocks blocksWith(const std::vector<std::pair<float, float>>& centres, float half)
{
    PlotBlocks b;
    b.shadowHalf = half;
    for (size_t i = 0; i < centres.size(); ++i) {
        DrawnTree t;
        t.index = static_cast<uint32_t>(i);
        t.sampleX = centres[i].first;
        t.sampleY = centres[i].second;
        b.drawn.push_back(t);
    }
    return b;
}

float hf(uint16_t h) { return bake::halfToFloat(h); }

bool emptyDecal(const ShadowDecal& d) { return d.quads == 0 && d.vertices.empty() && d.hash == 0; }

bool emptySet(const decal::DecalSet& s)
{
    return s.decals.empty() && s.vertices.empty() && s.textures.empty() && s.areaChanged.empty() && s.walk.cols == 0 &&
           s.grid.cols == 0;
}

// A tap set of 8 x 8 tiles with three pass-0 decals (one triangle each), names roads / blank spec, list orders 5, 9, 7.
decal::DecalSet tapSet()
{
    decal::DecalSet s;
    s.generation = 7;
    s.loadSeq = 3;
    std::memcpy(s.worldRect, kWorld, sizeof kWorld);
    s.textures = {"roadsandrails_new_d.dds", "roadsandrails_h.dds", "BLANK_TEXTURE_SPEC_DECAL.dds"};
    const float rects[3][4] = {{10, 10, 40, 40}, {100, 100, 140, 120}, {300, 20, 330, 60}};
    const uint32_t orders[3] = {5, 9, 7};
    for (int i = 0; i < 3; ++i) {
        decal::Decal d;
        std::memcpy(d.rect, rects[i], sizeof d.rect);
        d.priority = 0.7f;
        d.pass = 0;
        d.texture[0] = 0;
        d.texture[1] = 1;
        d.texture[2] = 2;
        d.firstVertex = static_cast<uint32_t>(s.vertices.size());
        d.vertexCount = 3;
        d.listOrder = orders[i];
        d.hash = 1000 + i;
        for (int k = 0; k < 3; ++k) s.vertices.push_back(decal::DecalVertex{static_cast<uint16_t>(k), 0, 0, 0});
        s.decals.push_back(d);
    }
    decal::buildIndex(s);
    return s;
}

// The tap set with a walk: 2 x 2 cells of 256 samples from the origin; each cell's list newest (highest list order) first.
decal::DecalSet tapSetWithWalk()
{
    decal::DecalSet s = tapSet();
    decal::DecalWalk& w = s.walk;
    w.origin[0] = w.origin[1] = 0.0f;
    w.inverse[0] = w.inverse[1] = 1.0f / 256.0f;
    w.cols = w.rows = 2;
    const size_t n = s.decals.size();
    w.cell.resize(n);
    w.rank.assign(n, 0);
    w.point.resize(2 * n);
    for (size_t k = 0; k < n; ++k) {
        const decal::Decal& d = s.decals[k];
        const float px = (d.rect[0] + d.rect[2]) * 0.5f, py = (d.rect[1] + d.rect[3]) * 0.5f;
        w.point[2 * k] = px;
        w.point[2 * k + 1] = py;
        w.cell[k] = static_cast<uint32_t>(py / 256.0f) * 2 + static_cast<uint32_t>(px / 256.0f);
    }
    for (size_t k = 0; k < n; ++k)
        for (size_t j = 0; j < n; ++j)
            if (j != k && w.cell[j] == w.cell[k] && s.decals[j].listOrder > s.decals[k].listOrder) w.rank[k]++;
    decal::buildIndex(s);
    return s;
}

// The area block of the tap set: 8 x 8 tiles, tile (0, 0) set.
decal::DecalSet withArea(decal::DecalSet s)
{
    s.areaTilesX = s.areaTilesY = 8;
    s.areaChanged.assign(64, 0);
    s.areaChanged[0] = 1;
    return s;
}

std::vector<uint32_t> passList(const decal::DecalSet& s, int ox, int oy, uint32_t pass)
{
    std::vector<uint32_t> out;
    const decal::TileDecalList l = decal::decalsForTile(s, ox, oy);
    for (uint32_t i : l.indices)
        if (s.decals[i].pass == pass) out.push_back(i);
    return out;
}

bool sameTap(const decal::DecalSet& a, const decal::DecalSet& b)
{
    if (a.decals.size() != b.decals.size() || a.vertices.size() != b.vertices.size() || a.textures != b.textures ||
        a.areaChanged != b.areaChanged || a.walk.cell != b.walk.cell || a.walk.rank != b.walk.rank)
        return false;
    for (size_t i = 0; i < a.decals.size(); ++i)
        if (std::memcmp(&a.decals[i], &b.decals[i], sizeof(decal::Decal)) != 0) return false;
    return std::memcmp(a.vertices.data(), b.vertices.data(), a.vertices.size() * sizeof(decal::DecalVertex)) == 0;
}

} // namespace

// ---- (1) the record of one plot ----

C5R_TEST(shadow_decal_form_corners_uv_and_rectangle)
{
    const PlotBlocks b = blocksWith({{100.0f, 200.0f}, {130.5f, 190.25f}}, 6.0f);
    ShadowDecal d;
    CHECK_OK(shadowDecal(b, 3, 4, kWorld, d));
    CHECK_EQ(d.plotX, 3);
    CHECK_EQ(d.plotY, 4);
    CHECK_EQ(d.quads, 2u);
    CHECK_EQ(d.half, 6.0f);
    CHECK_EQ(d.vertices.size(), 12u);
    CHECK_EQ(d.rect[0], 94.0f);
    CHECK_EQ(d.rect[1], 184.25f);
    CHECK_EQ(d.rect[2], 136.5f);
    CHECK_EQ(d.rect[3], 206.0f);
    // the EXE's corner and uv pattern; positions decode to the corners (within binary16 at extent ~40: 1/2048 x 42)
    const float ux[6] = {0, 1, 1, 0, 1, 0}, uy[6] = {1, 1, 0, 1, 0, 0};
    const int cx[6] = {-1, 1, 1, -1, 1, -1}, cy[6] = {-1, -1, 1, -1, 1, 1};
    for (int q = 0; q < 2; ++q)
        for (int k = 0; k < 6; ++k) {
            const decal::DecalVertex& v = d.vertices[6 * q + k];
            CHECK_EQ(hf(v.u), ux[k]);
            CHECK_EQ(hf(v.v), uy[k]);
            const float x = d.rect[0] + hf(v.px) * (d.rect[2] - d.rect[0]);
            const float y = d.rect[1] + hf(v.py) * (d.rect[3] - d.rect[1]);
            CHECK(std::fabs(x - (b.drawn[q].sampleX + cx[k] * 6.0f)) < 0.03f);
            CHECK(std::fabs(y - (b.drawn[q].sampleY + cy[k] * 6.0f)) < 0.03f);
            CHECK_EQ(v.px, bake::floatToHalf(((b.drawn[q].sampleX + cx[k] * 6.0f) - d.rect[0]) * (1.0f / (d.rect[2] - d.rect[0]))));
        }
    // the box's corners are exactly 0 and 1
    CHECK_EQ(hf(d.vertices[0].px), 0.0f);
    CHECK_EQ(hf(d.vertices[2].py), 1.0f);
    CHECK(d.hash != 0);
    ShadowDecal again;
    CHECK_OK(shadowDecal(b, 3, 4, kWorld, again));
    CHECK_EQ(again.hash, d.hash);
    PlotBlocks moved = b;
    moved.drawn[1].sampleX += 0.25f;
    CHECK_OK(shadowDecal(moved, 3, 4, kWorld, again));
    CHECK(again.hash != d.hash);
}

C5R_TEST(shadow_decal_hills_half_and_clamp_to_the_world)
{
    // kinds 3 / 4: 3.6 (0.6 x 6.0 in single precision, as buildPlot sets it)
    ShadowDecal d;
    CHECK_OK(shadowDecal(blocksWith({{50.0f, 50.0f}}, kShadowHillFactor * kShadowHalf), 0, 0, kWorld, d));
    CHECK_EQ(d.rect[0], 50.0f - 0.6f * 6.0f);
    CHECK_EQ(d.rect[3], 50.0f + 0.6f * 6.0f);
    // a plot at the world's east edge: the rectangle is clamped, positions are normalised to the clamped rectangle and
    // run past 1 (stock: 5 Tiny decals with positions up to 1.97, trees.md 12.1)
    CHECK_OK(shadowDecal(blocksWith({{507.0f, 100.0f}, {500.0f, 104.0f}}, 6.0f), 7, 1, kWorld, d));
    CHECK_EQ(d.rect[0], 494.0f);
    CHECK_EQ(d.rect[2], 511.0f);   // 513 clamped
    CHECK_EQ(d.rect[1], 94.0f);
    CHECK_EQ(d.rect[3], 110.0f);
    float maxPos = 0;
    for (const decal::DecalVertex& v : d.vertices) maxPos = std::max(maxPos, hf(v.px));
    CHECK(maxPos > 1.0f);
    CHECK_EQ(d.vertices[1].px, bake::floatToHalf((513.0f - 494.0f) * (1.0f / 17.0f)));   // quad 0's max x past the clamp
    CHECK_EQ(d.vertices[7].px, bake::floatToHalf((506.0f - 494.0f) * (1.0f / 17.0f)));   // quad 1's max x inside
    // the west edge: a negative corner clamped to 0, positions below 0 kept
    CHECK_OK(shadowDecal(blocksWith({{2.0f, 100.0f}}, 6.0f), 0, 1, kWorld, d));
    CHECK_EQ(d.rect[0], 0.0f);
    CHECK(hf(d.vertices[0].px) < 0.0f);
}

C5R_TEST(shadow_decal_no_drawn_tree_is_no_decal)
{
    ShadowDecal d;
    d.quads = 9;
    CHECK_OK(shadowDecal(blocksWith({}, 6.0f), 2, 2, kWorld, d));
    CHECK_EQ(d.quads, 0u);
    CHECK(d.vertices.empty());
    CHECK_EQ(d.plotX, 2);
}

C5R_TEST(shadow_decal_refusals)
{
    const PlotBlocks good = blocksWith({{100.0f, 100.0f}}, 6.0f);
    ShadowDecal d;
    auto refused = [&](Status st) {
        CHECK_ERR(st, Err::BadLayout);
        CHECK(emptyDecal(d));
    };
    const float nanWorld[4] = {0, std::nanf(""), 511, 511};
    const float emptyWorld[4] = {10, 0, 10, 511};
    const float flipped[4] = {511, 0, 0, 511};
    refused(shadowDecal(good, 0, 0, nanWorld, d));
    refused(shadowDecal(good, 0, 0, emptyWorld, d));
    refused(shadowDecal(good, 0, 0, flipped, d));
    refused(shadowDecal(good, 0, 0, nullptr, d));
    refused(shadowDecal(blocksWith({{100.0f, 100.0f}}, 0.0f), 0, 0, kWorld, d));
    refused(shadowDecal(blocksWith({{100.0f, 100.0f}}, -6.0f), 0, 0, kWorld, d));
    refused(shadowDecal(blocksWith({{100.0f, 100.0f}}, std::nanf("")), 0, 0, kWorld, d));
    refused(shadowDecal(blocksWith({{100.0f, 100.0f}}, INFINITY), 0, 0, kWorld, d));
    refused(shadowDecal(blocksWith({{100.0f, 100.0f}, {std::nanf(""), 3.0f}}, 6.0f), 0, 0, kWorld, d));
    refused(shadowDecal(blocksWith({{100.0f, INFINITY}}, 6.0f), 0, 0, kWorld, d));
    refused(shadowDecal(blocksWith({{900.0f, 100.0f}}, 6.0f), 0, 0, kWorld, d));    // outside the world (x)
    refused(shadowDecal(blocksWith({{100.0f, -50.0f}}, 6.0f), 0, 0, kWorld, d));    // outside the world (y)
    std::vector<std::pair<float, float>> many(kMaxShadowQuads + 1, {100.0f, 100.0f});
    refused(shadowDecal(blocksWith(many, 6.0f), 0, 0, kWorld, d));
    many.pop_back();
    CHECK_OK(shadowDecal(blocksWith(many, 6.0f), 0, 0, kWorld, d));                 // exactly AddDecal's limit
    CHECK_EQ(d.vertices.size(), 6u * kMaxShadowQuads);
    CHECK(d.vertices.size() <= 0xFFFEu);
}

C5R_TEST(world_rect_of_a_grid)
{
    heightfield::HeightGrid g = flatGrid(56, 40, 0);
    float w[4];
    worldRectOf(g, w);
    CHECK_EQ(w[0], 0.0f);
    CHECK_EQ(w[1], 0.0f);
    CHECK_EQ(w[2], 3583.0f);   // the tap's TDS+0x6C on Tiny
    CHECK_EQ(w[3], 2559.0f);
}

C5R_TEST(shadow_decal_of_a_built_plot)
{
    heightfield::HeightGrid g = flatGrid(8, 8, u16For(20.0f));
    const ForestParams p = paramsN(180, 2.0f);
    const Layout l = layoutOf(p, 11);
    float w[4];
    worldRectOf(g, w);
    for (Kind k : {Kind::Forest, Kind::JungleHills}) {
        PlotRecord r;
        CHECK_OK(makeRecord(3, 4, k, 0, r));
        PlotBlocks b;
        CHECK_OK(buildPlot(g, p, l, r, EmptyCut(), BuildOptions(), b));
        CHECK_EQ(b.counts.drawn, 180u);
        ShadowDecal d;
        CHECK_OK(shadowDecal(b, 3, 4, w, d));
        CHECK_EQ(d.quads, b.counts.drawn);
        CHECK_EQ(d.half, isHills(k) ? kShadowHillFactor * kShadowHalf : kShadowHalf);
        for (int i = 0; i < 4; ++i) CHECK_EQ(d.rect[i], b.shadowRect[i]);   // inside the world: the unclamped box
        // quads in layout order (the layout is sorted by square y descending, so sample y descending)
        for (uint32_t q = 1; q < d.quads; ++q) CHECK(b.drawn[q].index > b.drawn[q - 1].index);
    }
}

// ---- (2) the merge ----

C5R_TEST(merge_appends_after_the_tap_and_keeps_the_tap)
{
    const decal::DecalSet tap = tapSet();
    const decal::DecalSet before = tap;
    ShadowDecal a, b;
    CHECK_OK(shadowDecal(blocksWith({{100.0f, 100.0f}, {110.0f, 104.0f}}, 6.0f), 1, 1, kWorld, a));
    CHECK_OK(shadowDecal(blocksWith({{120.0f, 108.0f}}, 3.6f), 2, 1, kWorld, b));
    ShadowDecal none;
    CHECK_OK(shadowDecal(blocksWith({}, 6.0f), 5, 5, kWorld, none));
    const ShadowDecal* list[3] = {&a, &none, &b};
    decal::DecalSet m;
    MergeStats st;
    CHECK_OK(mergeShadows(tap, list, 3, nullptr, m, &st));
    CHECK(sameTap(tap, before));                                  // the tap's set is not modified
    CHECK_EQ(st.tapDecals, 3u);
    CHECK_EQ(st.shadowDecals, 2u);                                // the plot without trees adds nothing
    CHECK_EQ(st.shadowVertices, 18u);
    CHECK_EQ(st.namesAdded, 1u);                                  // the spec name was there (other case)
    CHECK(!st.walk);
    CHECK(!st.area);
    CHECK_EQ(m.generation, 7u);
    CHECK_EQ(m.loadSeq, 3u);
    CHECK_EQ(m.decals.size(), 5u);
    CHECK_EQ(m.textures.size(), 4u);
    CHECK_EQ(m.textures[3], std::string(kShadowAlbedo));
    for (size_t i = 0; i < 3; ++i) CHECK(std::memcmp(&m.decals[i], &tap.decals[i], sizeof(decal::Decal)) == 0);
    for (size_t i = 3; i < 5; ++i) {
        const decal::Decal& d = m.decals[i];
        const ShadowDecal& s = i == 3 ? a : b;
        CHECK_EQ(d.priority, 0.1f);
        CHECK_EQ(d.pass, 1u);
        CHECK_EQ(d.texture[0], 3u);
        CHECK_EQ(d.texture[1], 3u);
        CHECK_EQ(d.texture[2], 2u);
        CHECK_EQ(d.vertexCount, 6u * s.quads);
        CHECK_EQ(d.hash, s.hash);
        CHECK(std::memcmp(d.rect, s.rect, sizeof d.rect) == 0);
        CHECK(std::memcmp(&m.vertices[d.firstVertex], s.vertices.data(), s.vertices.size() * sizeof(decal::DecalVertex)) == 0);
    }
    CHECK_EQ(m.decals[3].firstVertex, 9u);
    CHECK_EQ(m.decals[4].firstVertex, 21u);
    CHECK_EQ(m.decals[3].listOrder, 10u);                         // after the tap's newest (9)
    CHECK_EQ(m.decals[4].listOrder, 11u);
    CHECK(m.grid.cols > 0);                                       // indexed
    // tile (1, 1) draws its road and both shadows; pass 1: the newer shadow first (4111's rule, no walk)
    CHECK(passList(m, 64, 64, 0) == passList(tap, 64, 64, 0));
    const std::vector<uint32_t> p1 = passList(m, 64, 64, 1);
    CHECK_EQ(p1.size(), 2u);
    CHECK_EQ(p1[0], 4u);
    CHECK_EQ(p1[1], 3u);
    // every tile's pass-0 list is the tap's
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) CHECK(passList(m, 64 * x, 64 * y, 0) == passList(tap, 64 * x, 64 * y, 0));
    CHECK_EQ(treeShadowCount(m), 2u);
    CHECK_EQ(treeShadowCount(tap), 0u);
}

C5R_TEST(merge_with_nothing_is_the_tap_with_the_names)
{
    const decal::DecalSet tap = tapSet();
    decal::DecalSet m;
    MergeStats st;
    CHECK_OK(mergeShadows(tap, nullptr, 0, nullptr, m, &st));
    CHECK_EQ(m.decals.size(), 3u);
    CHECK_EQ(st.shadowDecals, 0u);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            CHECK(decal::decalsForTile(m, 64 * x, 64 * y).indices == decal::decalsForTile(tap, 64 * x, 64 * y).indices);
    // a set that already names both textures adds none
    decal::DecalSet named = tapSet();
    named.textures.push_back("assets/Decals/Trees/tree_shadow_2.DDS");
    CHECK_OK(mergeShadows(named, nullptr, 0, nullptr, m, &st));
    CHECK_EQ(st.namesAdded, 0u);
    CHECK_EQ(m.textures.size(), 4u);
}

C5R_TEST(merge_extends_the_walk_newest_first)
{
    const decal::DecalSet tap = tapSetWithWalk();
    CHECK(decal::hasWalk(tap));
    ShadowDecal a, b, c;
    // a and b in cell 0 (points below 256), c in cell 3
    CHECK_OK(shadowDecal(blocksWith({{100.0f, 100.0f}}, 6.0f), 1, 1, kWorld, a));
    CHECK_OK(shadowDecal(blocksWith({{104.0f, 102.0f}}, 6.0f), 1, 2, kWorld, b));
    CHECK_OK(shadowDecal(blocksWith({{400.0f, 400.0f}}, 6.0f), 6, 6, kWorld, c));
    const ShadowDecal* list[3] = {&a, &b, &c};
    decal::DecalSet m;
    MergeStats st;
    CHECK_OK(mergeShadows(tap, list, 3, nullptr, m, &st));
    CHECK(st.walk);
    CHECK(decal::hasWalk(m));
    CHECK_EQ(st.walkLarge, 0u);
    // AddDecal's point and cell; at the head of the cell's list, the last added first
    CHECK_EQ(m.walk.point[2 * 3], (a.rect[0] + a.rect[2]) * 0.5f);
    CHECK_EQ(m.walk.cell[3], 0u);
    CHECK_EQ(m.walk.cell[4], 0u);
    CHECK_EQ(m.walk.cell[5], 3u);
    CHECK_EQ(m.walk.rank[4], 0u);   // b: newest of cell 0
    CHECK_EQ(m.walk.rank[3], 1u);   // a
    CHECK_EQ(m.walk.rank[5], 0u);   // c: alone in cell 3
    // the tap's decals of cell 0 moved down by the two forest decals, in their own order
    for (size_t k = 0; k < 3; ++k)
        CHECK_EQ(m.walk.rank[k], tap.walk.rank[k] + (tap.walk.cell[k] == 0 ? 2u : 0u));
    // the EXE's selection: pass 1 of tile (1, 1) holds b then a; pass 0 of every tile is the tap's, in its order
    const std::vector<decal::ExeDrawEntry> s1 = decal::exeSelection(m, 64, 64, 1);
    CHECK_EQ(s1.size(), 2u);
    CHECK_EQ(s1[0].decal, 4u);
    CHECK_EQ(s1[1].decal, 3u);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            const std::vector<decal::ExeDrawEntry> e0 = decal::exeSelection(m, 64 * x, 64 * y, 0);
            const std::vector<decal::ExeDrawEntry> t0 = decal::exeSelection(tap, 64 * x, 64 * y, 0);
            CHECK_EQ(e0.size(), t0.size());
            for (size_t i = 0; i < std::min(e0.size(), t0.size()); ++i) CHECK_EQ(e0[i].decal, t0[i].decal);
            CHECK(passList(m, 64 * x, 64 * y, 0) == passList(tap, 64 * x, 64 * y, 0));
        }
}

C5R_TEST(merge_walk_large_and_clamped_cells)
{
    decal::DecalSet tap = tapSetWithWalk();
    // a grid whose cells are smaller than the shadow (inverse 1/8): a point past the last cell is clamped to it
    tap.walk.inverse[0] = tap.walk.inverse[1] = 1.0f / 8.0f;
    for (size_t k = 0; k < tap.decals.size(); ++k) {
        const uint32_t cx = std::min(1u, static_cast<uint32_t>(tap.walk.point[2 * k] / 8.0f));
        const uint32_t cy = std::min(1u, static_cast<uint32_t>(tap.walk.point[2 * k + 1] / 8.0f));
        tap.walk.cell[k] = cy * 2 + cx;
    }
    tap.walk.rank = {0, 1, 2};   // all three in cell 3, newest = index 0 here
    decal::buildIndex(tap);
    CHECK(decal::hasWalk(tap));
    // a forest decal wider than 256 samples (a synthetic plot of two far trees) goes to the large list
    ShadowDecal wide, narrow;
    CHECK_OK(shadowDecal(blocksWith({{20.0f, 20.0f}, {300.0f, 30.0f}}, 6.0f), 0, 0, kWorld, wide));
    CHECK_OK(shadowDecal(blocksWith({{4.0f, 4.0f}}, 6.0f), 0, 0, kWorld, narrow));
    const ShadowDecal* list[2] = {&wide, &narrow};
    decal::DecalSet m;
    MergeStats st;
    CHECK_OK(mergeShadows(tap, list, 2, nullptr, m, &st));
    CHECK_EQ(st.walkLarge, 1u);
    CHECK_EQ(m.walk.cell[3], decal::kWalkLarge);
    CHECK_EQ(m.walk.rank[3], 0u);                      // the tap had no large decal
    CHECK_EQ(m.walk.cell[4], 0u);                      // rect clamped at 0 (0..10): point (5, 5) -> cell 0
    CHECK_EQ(m.walk.point[2 * 4], (0.0f + 10.0f) * 0.5f);
    CHECK(decal::hasWalk(m));
    CHECK_EQ(m.walk.large.size(), 1u);
    // the large one is selected after the small ones on the tiles it covers
    const std::vector<decal::ExeDrawEntry> s1 = decal::exeSelection(m, 0, 0, 1);
    CHECK_EQ(s1.size(), 2u);
    CHECK_EQ(s1[0].decal, 4u);
    CHECK_EQ(s1[1].decal, 3u);
}

C5R_TEST(merge_sets_the_area_bits_and_the_marks)
{
    const decal::DecalSet tap = withArea(tapSet());
    ShadowDecal a, edge;
    CHECK_OK(shadowDecal(blocksWith({{100.0f, 100.0f}, {130.0f, 100.0f}}, 6.0f), 1, 1, kWorld, a));   // 94..136: tiles 1..2, 1
    CHECK_OK(shadowDecal(blocksWith({{2.0f, 60.0f}}, 6.0f), 0, 0, kWorld, edge));                      // 0..8, 54..66: tiles 0, 0..1
    const ShadowDecal* list[2] = {&a, &edge};
    const std::vector<decal::TileOrigin> marks = {{5, 6}, {0, 0}, {-1, 2}, {8, 0}, {3, 99}};
    decal::DecalSet m;
    MergeStats st;
    CHECK_OK(mergeShadows(tap, list, 2, &marks, m, &st));
    CHECK(st.area);
    CHECK_EQ(m.areaTilesX, 8u);
    auto bit = [&](int x, int y) { return m.areaChanged[static_cast<size_t>(y) * 8 + x]; };
    CHECK_EQ(bit(1, 1), 1);
    CHECK_EQ(bit(2, 1), 1);
    CHECK_EQ(bit(0, 0), 1);   // the tap's own
    CHECK_EQ(bit(0, 1), 1);
    CHECK_EQ(bit(5, 6), 1);   // a mark
    CHECK_EQ(bit(3, 1), 0);
    CHECK_EQ(st.areaBitsSet, 4u);   // (1,1) (2,1) (0,1) (5,6); (0,0) was set; marks outside the grid ignored
    size_t ones = 0;
    for (uint8_t v : m.areaChanged) ones += v;
    CHECK_EQ(ones, 5u);
    bool known = false;
    CHECK(decal::areaChangedAt(m, 2, 1, known));
    CHECK(known);
    // without an area block: none is made, the marks do nothing
    CHECK_OK(mergeShadows(tapSet(), list, 2, &marks, m, &st));
    CHECK(!st.area);
    CHECK(m.areaChanged.empty());
    CHECK_EQ(st.areaBitsSet, 0u);
    // a block of the wrong size counts as none
    decal::DecalSet bad = withArea(tapSet());
    bad.areaChanged.pop_back();
    CHECK_OK(mergeShadows(bad, list, 2, &marks, m, &st));
    CHECK(!st.area);
}

C5R_TEST(merge_refusals)
{
    decal::DecalSet m;
    MergeStats st;
    ShadowDecal good;
    CHECK_OK(shadowDecal(blocksWith({{100.0f, 100.0f}}, 6.0f), 1, 1, kWorld, good));
    auto refused = [&](Status s, Err want) {
        CHECK_ERR(s, want);
        CHECK(emptySet(m));
        CHECK_EQ(st.shadowDecals, 0u);
        CHECK_EQ(st.tapDecals, 0u);
    };
    const ShadowDecal* one[1] = {&good};
    // the EXE's own Tree_Shadow decals in the tap's set (Forest on)
    decal::DecalSet withTrees = tapSet();
    withTrees.textures.push_back("Tree_Shadow_2.dds");
    withTrees.decals[1].texture[0] = 3;
    withTrees.decals[1].pass = 1;
    m.decals.resize(2);   // a stale output is cleared
    refused(mergeShadows(withTrees, one, 1, nullptr, m, &st), Err::Unsupported);
    // Tree_Shadow_2.dds on a pass-0 decal is not the Forest's
    withTrees.decals[1].pass = 0;
    CHECK_OK(mergeShadows(withTrees, one, 1, nullptr, m, &st));
    CHECK_EQ(st.namesAdded, 0u);
    // the tap's world rectangle
    decal::DecalSet badWorld = tapSet();
    badWorld.worldRect[2] = std::nanf("");
    refused(mergeShadows(badWorld, one, 1, nullptr, m, &st), Err::BadLayout);
    badWorld.worldRect[2] = -5.0f;
    refused(mergeShadows(badWorld, one, 1, nullptr, m, &st), Err::BadLayout);
    const decal::DecalSet tap = tapSet();
    // no list, a null entry
    refused(mergeShadows(tap, nullptr, 1, nullptr, m, &st), Err::BadLayout);
    const ShadowDecal* withNull[2] = {&good, nullptr};
    refused(mergeShadows(tap, withNull, 2, nullptr, m, &st), Err::BadLayout);
    // vertices not 6 x quads
    ShadowDecal shortV = good;
    shortV.vertices.pop_back();
    const ShadowDecal* l1[1] = {&shortV};
    refused(mergeShadows(tap, l1, 1, nullptr, m, &st), Err::BadLayout);
    ShadowDecal zeroQ = good;
    zeroQ.quads = 0;   // quads 0 with vertices
    const ShadowDecal* l2[1] = {&zeroQ};
    refused(mergeShadows(tap, l2, 1, nullptr, m, &st), Err::BadLayout);
    // a rectangle not finite, inverted, or outside the tap's world
    ShadowDecal r = good;
    r.rect[1] = std::nanf("");
    const ShadowDecal* l3[1] = {&r};
    refused(mergeShadows(tap, l3, 1, nullptr, m, &st), Err::BadLayout);
    r = good;
    std::swap(r.rect[0], r.rect[2]);
    refused(mergeShadows(tap, l3, 1, nullptr, m, &st), Err::BadLayout);
    r = good;
    r.rect[2] = 600.0f;   // made against another world
    refused(mergeShadows(tap, l3, 1, nullptr, m, &st), Err::BadLayout);
    r = good;
    r.rect[1] = -1.0f;
    refused(mergeShadows(tap, l3, 1, nullptr, m, &st), Err::BadLayout);
    // the list order has no room after the tap's newest
    decal::DecalSet full = tapSet();
    full.decals[0].listOrder = 0xFFFFFFFFu;
    refused(mergeShadows(full, one, 1, nullptr, m, &st), Err::BadLayout);
    full.decals[0].listOrder = 0xFFFFFFFEu;
    refused(mergeShadows(full, one, 1, nullptr, m, &st), Err::BadLayout);
    full.decals[0].listOrder = 0xFFFFFFFDu;   // exactly room for one
    CHECK_OK(mergeShadows(full, one, 1, nullptr, m, &st));
    CHECK_EQ(m.decals.back().listOrder, 0xFFFFFFFEu);
    // a tap whose walk lists are present but whose per-decal ranks no longer fit once extended cannot happen through
    // hasWalk; a tap without a usable walk merges without one
    decal::DecalSet brokenWalk = tapSetWithWalk();
    brokenWalk.walk.rank[0] = 7;   // hasWalk still true (lists built before), the ranks inconsistent
    refused(mergeShadows(brokenWalk, one, 1, nullptr, m, &st), Err::BadLayout);
}

// ---- (3) the change ----

C5R_TEST(shadow_change_rects_bake_tiles_and_area_tiles)
{
    ShadowDecal a, b, none;
    CHECK_OK(shadowDecal(blocksWith({{100.0f, 100.0f}, {130.0f, 100.0f}}, 6.0f), 1, 1, kWorld, a));   // 94..136 x 94..106
    CHECK_OK(shadowDecal(blocksWith({{126.0f, 60.0f}}, 6.0f), 2, 0, kWorld, b));                      // 120..132 x 54..66
    CHECK_OK(shadowDecal(blocksWith({}, 6.0f), 0, 0, kWorld, none));
    const ShadowDecal* list[3] = {&a, &none, &b};
    ShadowChange ch;
    CHECK_OK(shadowChange(list, 3, kWorld, ch));
    CHECK_EQ(ch.rects.size(), 2u);
    CHECK(std::memcmp(ch.rects[0].v, a.rect, sizeof a.rect) == 0);
    // stock's plain squares: a 94..136 x 94..106 -> x 1..2, y 1; b 120..132 x 54..66 -> x 1..2, y 0..1
    const std::vector<std::pair<int, int>> want = {{1, 0}, {2, 0}, {1, 1}, {2, 1}};
    CHECK_EQ(ch.areaTiles.size(), want.size());
    for (size_t i = 0; i < std::min(want.size(), ch.areaTiles.size()); ++i) {
        CHECK_EQ(ch.areaTiles[i].x, want[i].first);
        CHECK_EQ(ch.areaTiles[i].y, want[i].second);
    }
    const std::vector<decal::TileOrigin> t = decal::tilesForRects(ch.rects.data(), ch.rects.size(), kWorld);
    CHECK_EQ(ch.bakeTiles.size(), t.size());
    for (size_t i = 0; i < std::min(t.size(), ch.bakeTiles.size()); ++i) {
        CHECK_EQ(ch.bakeTiles[i].x, t[i].x);
        CHECK_EQ(ch.bakeTiles[i].y, t[i].y);
    }
    // every area tile is a bake tile (the plain square lies inside the query square)
    for (const decal::TileOrigin& at : ch.areaTiles) {
        bool found = false;
        for (const decal::TileOrigin& bt : ch.bakeTiles) found = found || (bt.x == 64 * at.x && bt.y == 64 * at.y);
        CHECK(found);
    }
    // the merged set: exactly the bake tiles' draw lists can differ from the tap's
    const decal::DecalSet tap = tapSet();
    decal::DecalSet m;
    const ShadowDecal* both[2] = {&a, &b};
    CHECK_OK(mergeShadows(tap, both, 2, nullptr, m));
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            const bool differs = decal::decalsForTile(m, 64 * x, 64 * y).indices != decal::decalsForTile(tap, 64 * x, 64 * y).indices;
            bool listed = false;
            for (const decal::TileOrigin& bt : ch.bakeTiles) listed = listed || (bt.x == 64 * x && bt.y == 64 * y);
            if (differs) CHECK(listed);
        }
    // nothing changed: empty
    CHECK_OK(shadowChange(nullptr, 0, kWorld, ch));
    CHECK(ch.rects.empty() && ch.bakeTiles.empty() && ch.areaTiles.empty());
}

C5R_TEST(shadow_change_refusals)
{
    ShadowDecal a;
    CHECK_OK(shadowDecal(blocksWith({{100.0f, 100.0f}}, 6.0f), 1, 1, kWorld, a));
    const ShadowDecal* one[1] = {&a};
    ShadowChange ch;
    auto refused = [&](Status st) {
        CHECK_ERR(st, Err::BadLayout);
        CHECK(ch.rects.empty() && ch.bakeTiles.empty() && ch.areaTiles.empty());
    };
    const float nanWorld[4] = {0, 0, std::nanf(""), 511};
    const float emptyWorld[4] = {0, 0, 0, 511};
    refused(shadowChange(one, 1, nanWorld, ch));
    refused(shadowChange(one, 1, emptyWorld, ch));
    refused(shadowChange(one, 1, nullptr, ch));
    refused(shadowChange(nullptr, 1, kWorld, ch));
    const ShadowDecal* withNull[2] = {&a, nullptr};
    refused(shadowChange(withNull, 2, kWorld, ch));
    ShadowDecal bad = a;
    bad.rect[3] = INFINITY;
    const ShadowDecal* l[1] = {&bad};
    refused(shadowChange(l, 1, kWorld, ch));
}

C5R_TEST(dtap_reader_refusals)
{
    decal::DecalSet s;
    std::vector<uint8_t> b(112, 0);
    CHECK_ERR(test4552::readDtap(std::vector<uint8_t>(50, 0), s), Err::Truncated);
    CHECK_ERR(test4552::readDtap(b, s), Err::BadMagic);
    const uint32_t magic = 0x54443543u, two = 2, one = 1, hdr = 112, rec = 48;
    std::memcpy(&b[0], &magic, 4);
    std::memcpy(&b[4], &two, 4);
    CHECK_ERR(test4552::readDtap(b, s), Err::BadVersion);
    std::memcpy(&b[4], &one, 4);
    CHECK_ERR(test4552::readDtap(b, s), Err::BadLayout);
    std::memcpy(&b[8], &hdr, 4);
    std::memcpy(&b[12], &rec, 4);
    CHECK_OK(test4552::readDtap(b, s));   // an empty image
    CHECK(s.decals.empty());
    const uint32_t n = 3, off = 112;
    std::memcpy(&b[24], &n, 4);
    std::memcpy(&b[40], &off, 4);
    CHECK_ERR(test4552::readDtap(b, s), Err::Truncated);
    CHECK(s.decals.empty());
}

// ---- proof modes ----

namespace {

// binary16 by truncation (round toward zero): the contrast rule of --dtapcheck
uint16_t halfTrunc(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    const uint32_t sign = (u >> 16) & 0x8000u;
    const int32_t e = static_cast<int32_t>((u >> 23) & 0xFFu) - 127 + 15;
    const uint32_t mant = u & 0x7FFFFFu;
    if (((u >> 23) & 0xFFu) == 0xFFu) return static_cast<uint16_t>(sign | 0x7C00u | (mant ? 0x200u : 0u));
    if (e >= 31) return static_cast<uint16_t>(sign | 0x7BFFu);
    if (e <= 0) {
        if (e < -10) return static_cast<uint16_t>(sign);
        return static_cast<uint16_t>(sign | ((mant | 0x800000u) >> (14 - e)));
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(e) << 10) | (mant >> 13));
}

// A single-precision centre c within +-0.08 of `est` whose corners give (lo, hi) by pos(c, sign) -> binary16 (--dtapcheck).
template <typename Pos> bool feasibleBy(float est, uint16_t lo, uint16_t hi, Pos pos)
{
    float c = est - 0.08f;
    const float end = est + 0.08f;
    for (int guard = 0; c <= end && guard < 100000; ++guard, c = std::nextafter(c, INFINITY))
        if (pos(c, -1.0f) == lo && pos(c, 1.0f) == hi) return true;
    return false;
}

int dtapCheckMain(int argc, char** argv)
{
    int rc = 0;
    for (int a = 2; a < argc; ++a) {
        std::vector<uint8_t> bytes;
        decal::DecalSet s;
        Status st = test4552::readFile(argv[a], bytes);
        if (st.ok()) st = test4552::readDtap(bytes, s);
        if (!st.ok()) { std::fprintf(stderr, "dtapcheck: %s: %s\n", argv[a], st.str().c_str()); return 3; }
        std::printf("== %s: %zu decals, %zu Tree_Shadow, walk %s\n", argv[a], s.decals.size(), treeShadowCount(s),
                    decal::hasWalk(s) ? "yes" : "no");
        // (a) the walk's point and cell by mergeShadows' rule
        if (decal::hasWalk(s)) {
            const decal::DecalWalk& w = s.walk;
            uint64_t narrow = 0, pointEq = 0, cellEq = 0, tsSmall = 0, tsEq = 0;
            for (size_t k = 0; k < s.decals.size(); ++k) {
                if (w.cell[k] == decal::kWalkLarge) continue;
                ++narrow;
                const decal::Decal& d = s.decals[k];
                const float px = (d.rect[0] + d.rect[2]) * 0.5f, py = (d.rect[1] + d.rect[3]) * 0.5f;
                const bool pe = px == w.point[2 * k] && py == w.point[2 * k + 1];
                const float tx = (px - w.origin[0]) * w.inverse[0], ty = (py - w.origin[1]) * w.inverse[1];
                const uint32_t cx = std::min(w.cols - 1, static_cast<uint32_t>(std::max(0, static_cast<int32_t>(tx))));
                const uint32_t cy = std::min(w.rows - 1, static_cast<uint32_t>(std::max(0, static_cast<int32_t>(ty))));
                const bool ce = cy * w.cols + cx == w.cell[k];
                pointEq += pe;
                cellEq += ce;
                const bool ts = d.pass == 1 && d.texture[0] < s.textures.size() &&
                                (s.textures[d.texture[0]] == kShadowAlbedo);
                tsSmall += ts;
                tsEq += ts && pe && ce;
            }
            std::printf("walk: small decals %llu, point equal %llu, cell equal %llu; Tree_Shadow %llu, both equal %llu\n",
                        (unsigned long long)narrow, (unsigned long long)pointEq, (unsigned long long)cellEq,
                        (unsigned long long)tsSmall, (unsigned long long)tsEq);
            if (pointEq != narrow || cellEq != narrow) rc = 1;
        }
        // (b) the Tree_Shadow vertices against shadowDecal's form
        uint64_t decals = 0, quads = 0, formEq = 0, h6 = 0, h36 = 0, misses = 0;
        uint64_t variants[4] = {0, 0, 0, 0};
        const uint16_t z = bake::floatToHalf(0.0f), o = bake::floatToHalf(1.0f);
        for (const decal::Decal& d : s.decals) {
            if (!(d.pass == 1 && d.texture[0] < s.textures.size() && s.textures[d.texture[0]] == kShadowAlbedo)) continue;
            ++decals;
            const decal::DecalVertex* v = &s.vertices[d.firstVertex];
            const float w = d.rect[2] - d.rect[0], hgt = d.rect[3] - d.rect[1];
            for (uint32_t q = 0; q < d.vertexCount / 6; ++q) {
                const decal::DecalVertex* p = v + 6 * q;
                ++quads;
                const uint16_t x0 = p[0].px, y0 = p[0].py, x1 = p[2].px, y1 = p[2].py;
                const bool form = p[1].px == x1 && p[1].py == y0 && p[3].px == x0 && p[3].py == y0 && p[4].px == x1 &&
                                  p[4].py == y1 && p[5].px == x0 && p[5].py == y1 && p[0].u == z && p[0].v == o &&
                                  p[1].u == o && p[1].v == o && p[2].u == o && p[2].v == z && p[3].u == z && p[3].v == o &&
                                  p[4].u == o && p[4].v == z && p[5].u == z && p[5].v == z;
                formEq += form;
                const float ext = (hf(x1) - hf(x0)) * w;
                const float h = ext > 9.6f ? kShadowHalf : kShadowHillFactor * kShadowHalf;
                (h == kShadowHalf ? h6 : h36)++;
                const float ex = d.rect[0] + (hf(x0) + hf(x1)) * 0.5f * w, ey = d.rect[1] + (hf(y0) + hf(y1)) * 0.5f * hgt;
                // per arithmetic: a single-precision centre within +-0.08 of the decoded one whose two corners give the
                // EXE's binary16 on both axes. 0: the module's ((p - min) x (1 / extent), float, RNE); 1: (p - min) /
                // extent, float, RNE; 2: the same with truncation; 3: everything in double, then float, RNE
                bool fit[4] = {false, false, false, false};
                for (int var = 0; var < 4; ++var) {
                    auto posFor = [&](float r0, float extent) {
                        return [=](float c, float sg) -> uint16_t {
                            const float x = c + sg * h;
                            if (var == 0) return bake::floatToHalf((x - r0) * (1.0f / extent));
                            if (var == 1) return bake::floatToHalf((x - r0) / extent);
                            if (var == 2) return halfTrunc((x - r0) / extent);
                            return bake::floatToHalf(static_cast<float>(
                                (static_cast<double>(c) + sg * static_cast<double>(h) - r0) / static_cast<double>(extent)));
                        };
                    };
                    fit[var] = feasibleBy(ex, x0, x1, posFor(d.rect[0], w)) && feasibleBy(ey, y0, y1, posFor(d.rect[1], hgt));
                    variants[var] += fit[var];
                }
                if (!fit[0] && misses++ < 10)
                    std::printf("  no centre: listOrder %u quad %u rect (%.9g %.9g %.9g %.9g) half %.9g corners x %.9g %.9g "
                                "y %.9g %.9g\n",
                                d.listOrder, q, d.rect[0], d.rect[1], d.rect[2], d.rect[3], h, hf(x0), hf(x1), hf(y0), hf(y1));
            }
        }
        std::printf("Tree_Shadow: %llu decals, %llu quads (half 6.0: %llu, 3.6: %llu); corner/uv form equal %llu; a centre "
                    "reproducing both axes: the module's (p - min) x (1 / extent) %llu; for contrast (p - min) / extent %llu, "
                    "the same truncated %llu, double then float %llu\n",
                    (unsigned long long)decals, (unsigned long long)quads, (unsigned long long)h6, (unsigned long long)h36,
                    (unsigned long long)formEq, (unsigned long long)variants[0], (unsigned long long)variants[1],
                    (unsigned long long)variants[2], (unsigned long long)variants[3]);
        if (formEq != quads || variants[0] != quads) rc = 1;
    }
    std::printf("dtapcheck exit %d\n", rc);
    return rc;
}

// Tiles of the world (origins 64 x, 64 y) whose decal list is not empty: the residency's areaChanged under 4105's rule.
uint32_t tilesWithDecals(const decal::DecalSet& s, uint32_t tx, uint32_t ty)
{
    uint32_t n = 0;
    for (uint32_t y = 0; y < ty; ++y)
        for (uint32_t x = 0; x < tx; ++x) n += !decal::decalsForTile(s, 64 * static_cast<int>(x), 64 * static_cast<int>(y)).indices.empty();
    return n;
}

uint32_t areaBits(const decal::DecalSet& s)
{
    uint32_t n = 0;
    for (uint8_t v : s.areaChanged) n += v;
    return n;
}

int proofMain(const std::string& inPath, const std::string& outPath)
{
    std::ifstream in(inPath);
    if (!in) { std::fprintf(stderr, "proof: cannot read %s\n", inPath.c_str()); return 3; }
    std::string word, gridPath, dtapPath;
    ForestParams params;
    std::vector<Layout> layouts;
    struct Rec { PlotRecord rec; uint32_t index, built; };
    std::vector<Rec> recs;
    std::vector<ShadowDecal> markDecals;
    float world[4] = {0, 0, 0, 0};
    bool haveWorld = false;
    auto bitsToFloat = [](uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; };
    while (in >> word) {
        if (word == "grid") {
            in >> std::ws;
            std::getline(in, gridPath);
        } else if (word == "dtap") {
            in >> std::ws;
            std::getline(in, dtapPath);
        } else if (word == "world") {
            in >> world[0] >> world[1] >> world[2] >> world[3];
            haveWorld = true;
        } else if (word == "mark") {
            ShadowDecal d;
            in >> d.rect[0] >> d.rect[1] >> d.rect[2] >> d.rect[3];
            d.quads = 1;   // shadowChange only reads the rectangle
            markDecals.push_back(d);
        } else if (word == "params") {
            in >> params.randomTrees >> params.griddedTrees;
            params.numTiles = 4;
            params.spaceBetween = 2.0f;
        } else if (word == "layout") {
            size_t k, n;
            in >> k >> n;
            if (k != layouts.size() || n > kMaxTreesPerPlot) { std::fprintf(stderr, "proof: bad layout header\n"); return 3; }
            Layout l(n);
            for (size_t i = 0; i < n; ++i) {
                uint32_t u[7];
                for (uint32_t& x : u) in >> std::hex >> x >> std::dec;
                l[i].offset[0] = bitsToFloat(u[0]); l[i].offset[1] = bitsToFloat(u[1]); l[i].offset[2] = bitsToFloat(u[2]);
                l[i].one = bitsToFloat(u[3]); l[i].square[0] = bitsToFloat(u[4]); l[i].square[1] = bitsToFloat(u[5]);
                l[i].size = bitsToFloat(u[6]);
            }
            layouts.push_back(std::move(l));
        } else if (word == "record") {
            Rec r;
            int32_t cube[3];
            uint32_t kind, lay;
            in >> r.index >> cube[0] >> cube[1] >> cube[2] >> kind >> lay >> r.built;
            Status st = recordFromCube(cube, kind, lay, r.rec);
            if (!st.ok() || lay >= layouts.size()) { std::fprintf(stderr, "proof: record %u refused\n", r.index); return 3; }
            recs.push_back(r);
        } else {
            std::fprintf(stderr, "proof: unknown word %s\n", word.c_str());
            return 3;
        }
    }
    if (!in.eof()) { std::fprintf(stderr, "proof: input not read to the end\n"); return 3; }
    Status st = checkLayouts(layouts, params.treesPerPlot());
    if (!st.ok()) { std::fprintf(stderr, "proof: %s\n", st.str().c_str()); return 3; }
    std::vector<uint8_t> gb;
    st = test4552::readFile(gridPath, gb);
    heightfield::HeightGrid g;
    if (st.ok()) st = heightfield::readTap(gb.data(), gb.size(), g);
    if (!st.ok()) { std::fprintf(stderr, "proof: grid %s: %s\n", gridPath.c_str(), st.str().c_str()); return 3; }
    gb.clear();
    gb.shrink_to_fit();
    if (!haveWorld) worldRectOf(g, world);
    FILE* out = std::fopen(outPath.c_str(), "w");
    if (!out) { std::fprintf(stderr, "proof: cannot write %s\n", outPath.c_str()); return 3; }
    std::fprintf(out, "# world %.1f %.1f %.1f %.1f\n# index plotX plotY kind quads half minX minY maxX maxY hash\n", world[0],
                 world[1], world[2], world[3]);
    std::vector<ShadowDecal> decals;
    decals.reserve(recs.size());
    for (const Rec& r : recs) {
        PlotBlocks b;
        st = buildPlot(g, params, layouts[r.rec.layout], r.rec, EmptyCut(), BuildOptions(), b);
        ShadowDecal d;
        if (st.ok()) st = shadowDecal(b, r.rec.plotX, r.rec.plotY, world, d);
        if (!st.ok()) { std::fprintf(stderr, "proof: record %u: %s\n", r.index, st.str().c_str()); std::fclose(out); return 3; }
        std::fprintf(out, "rec %u %d %d %u %u %.4f %.4f %.4f %.4f %.4f %016llx\n", r.index, r.rec.plotX, r.rec.plotY,
                     static_cast<unsigned>(r.rec.kind), d.quads, d.half, d.rect[0], d.rect[1], d.rect[2], d.rect[3],
                     static_cast<unsigned long long>(d.hash));
        decals.push_back(std::move(d));
    }
    if (!dtapPath.empty()) {
        std::vector<uint8_t> db;
        decal::DecalSet tap;
        st = test4552::readFile(dtapPath, db);
        if (st.ok()) st = test4552::readDtap(db, tap);
        if (!st.ok()) { std::fprintf(stderr, "proof: dtap %s: %s\n", dtapPath.c_str(), st.str().c_str()); std::fclose(out); return 3; }
        db.clear();
        db.shrink_to_fit();
        std::vector<const ShadowDecal*> ptrs;
        for (const ShadowDecal& d : decals) ptrs.push_back(&d);
        std::vector<const ShadowDecal*> mptrs;
        for (const ShadowDecal& d : markDecals) mptrs.push_back(&d);
        ShadowChange marks;
        st = shadowChange(mptrs.data(), mptrs.size(), tap.worldRect, marks);
        decal::DecalSet merged;
        MergeStats ms;
        const auto t0 = std::chrono::steady_clock::now();
        if (st.ok()) st = mergeShadows(tap, ptrs.data(), ptrs.size(), &marks.areaTiles, merged, &ms);
        const double mergeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!st.ok()) { std::fprintf(stderr, "proof: merge: %s\n", st.str().c_str()); std::fclose(out); return 3; }
        ShadowDecal const* const* cp = ptrs.data();
        ShadowChange ch;
        st = shadowChange(cp, ptrs.size(), tap.worldRect, ch);
        if (!st.ok()) { std::fprintf(stderr, "proof: change: %s\n", st.str().c_str()); std::fclose(out); return 3; }
        const uint32_t tx = static_cast<uint32_t>(std::floor(tap.worldRect[2] / 64.0f)) + 1;
        const uint32_t ty = static_cast<uint32_t>(std::floor(tap.worldRect[3] / 64.0f)) + 1;
        // pass-0 lists of every tile: equal between the tap's set and the merged one
        uint32_t equal0 = 0;
        for (uint32_t y = 0; y < ty; ++y)
            for (uint32_t x = 0; x < tx; ++x) {
                const int ox = 64 * static_cast<int>(x), oy = 64 * static_cast<int>(y);
                equal0 += passList(merged, ox, oy, 0) == passList(tap, ox, oy, 0);
            }
        std::fprintf(out, "dtap %s generation %u decals %zu walk %d area %ux%u\n", dtapPath.c_str(), tap.generation,
                     tap.decals.size(), decal::hasWalk(tap) ? 1 : 0, tap.areaTilesX, tap.areaTilesY);
        std::fprintf(out, "merge shadows %u vertices %u names_added %u walk %d walk_large %u area %d area_bits_set %u ms %.2f\n",
                     ms.shadowDecals, ms.shadowVertices, ms.namesAdded, ms.walk ? 1 : 0, ms.walkLarge, ms.area ? 1 : 0,
                     ms.areaBitsSet, mergeMs);
        std::fprintf(out, "marks rects %zu area_tiles %zu\n", markDecals.size(), marks.areaTiles.size());
        std::fprintf(out, "change rects %zu bake_tiles %zu area_tiles %zu\n", ch.rects.size(), ch.bakeTiles.size(),
                     ch.areaTiles.size());
        std::fprintf(out, "area tap_bits %u merged_bits %u tap_tiles_with_decals %u merged_tiles_with_decals %u tiles %u\n",
                     areaBits(tap), areaBits(merged), tilesWithDecals(tap, tx, ty), tilesWithDecals(merged, tx, ty), tx * ty);
        std::fprintf(out, "pass0_lists_equal %u of %u\n", equal0, tx * ty);
        // the t8 marks: every area tile of the forest decals has decals under the merged set (the residency's 4105 rule
        // bakes it with areaChanged) and its bit set (the rule with decalAreaFromExe); every bake tile of the change
        // whose pass-1 list is not empty
        uint32_t withList = 0, withBit = 0, withPass1 = 0;
        for (const decal::TileOrigin& t : ch.areaTiles) {
            withList += !decal::decalsForTile(merged, 64 * t.x, 64 * t.y).indices.empty();
            bool known = false;
            withBit += decal::areaChangedAt(merged, t.x, t.y, known) && known;
        }
        for (const decal::TileOrigin& t : ch.bakeTiles) withPass1 += !passList(merged, t.x, t.y, 1).empty();
        std::fprintf(out, "forest_area_tiles %zu with_decals %u with_bit %u; bake_tiles %zu with_pass1 %u\n",
                     ch.areaTiles.size(), withList, withBit, ch.bakeTiles.size(), withPass1);
    }
    std::fclose(out);
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc >= 2 && std::string(argv[1]) == "--proof") {
        if (argc != 4) {
            std::fprintf(stderr, "usage: %s --proof IN OUT\n", argv[0]);
            return 2;
        }
        return proofMain(argv[2], argv[3]);
    }
    if (argc >= 2 && std::string(argv[1]) == "--dtapcheck") {
        if (argc < 3) {
            std::fprintf(stderr, "usage: %s --dtapcheck DTAP [DTAP ...]\n", argv[0]);
            return 2;
        }
        return dtapCheckMain(argc, argv);
    }
    return test::runAll();
}
