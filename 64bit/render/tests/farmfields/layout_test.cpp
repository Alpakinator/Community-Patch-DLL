// render/tests/farmfields/layout_test.cpp - the farm layout's stages (epic 0528, ticket 4852) against the numbers of
// research/05-graphics-composition/farm-fields-layout.md: the noise and the direction field (angles A +- (pi/8 + pi/16)),
// the plot geometry, the seed tile, ear clipping, one block's graph (faces, areas near 186 L^2, vertices 4-16), the field
// look (uv scale 1/25.6, the turned share near 1/3, categories), the tracks (0.5 wide, v step 1.3), fields per farm,
// determinism, a farm added and removed (only its neighbourhood changes), and the negatives of every error path.
// Inputs are made by the tests (D83). Prints the measured figures (the hand-back's table).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "c5r/bake/grid.h"
#include "c5r/decal/decal.h"
#include "c5r/farmfields/farmfields.h"
#include "c5r/farmfields/layout.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::farmfields;

namespace {

const double kPi = 3.14159265358979323846;
const double kSpan = kPi / 8.0 + kPi / 16.0;

FarmTypes vpTypes()
{   // VP's farm_types.xml shape on Tiny SETUP (terrain-decals.md 8.3): 8, 8, 1, 1, 8, 1, 1, 1 children
    FarmTypes t;
    const uint32_t n[kCategoryCount] = {8, 8, 1, 1, 8, 1, 1, 1};
    for (uint32_t c = 0; c < kCategoryCount; ++c) t.hasSpec[c].assign(n[c], 0);
    return t;
}

// A W x H map, every plot (or every plot of a pattern) a farm of art type `art`.
LayoutInput farmland(uint32_t w, uint32_t h, uint8_t art, uint32_t seed = 777, bool all = true)
{
    LayoutInput in;
    in.gameSeed = seed;
    in.mapW = w;
    in.mapH = h;
    in.worldRect[2] = static_cast<float>(64 * w - 1);
    in.worldRect[3] = static_cast<float>(64 * (h + 4) - 1);
    in.types = vpTypes();
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            if (!all && ((x + 2 * y) % 3 != 0)) continue;
            FarmPlot p;
            p.x = static_cast<int32_t>(x);
            p.y = static_cast<int32_t>(y);
            p.artType = art;
            in.plots.push_back(p);
        }
    return in;
}

double quant(std::vector<double> v, double q)
{
    if (v.empty()) return std::numeric_limits<double>::quiet_NaN();
    std::sort(v.begin(), v.end());
    return v[static_cast<size_t>(q * (v.size() - 1) + 0.5)];
}

double polyArea(const std::vector<double>& p)
{
    double a = 0;
    const size_t n = p.size() / 2;
    for (size_t i = 0; i < n; ++i) {
        const size_t j = (i + 1) % n;
        a += p[2 * i] * p[2 * j + 1] - p[2 * j] * p[2 * i + 1];
    }
    return a * 0.5;
}

// A field polygon's corners in L.
std::vector<double> layoutCorners(const FieldPolygon& f)
{
    std::vector<double> r;
    for (size_t k = 0; k < f.corners.size() / 2; ++k) {
        r.push_back(f.corners[2 * k]);
        r.push_back(f.corners[2 * k + 1] / kLayoutYScale);
    }
    return r;
}

// uv scale and angle (radians, -pi..pi) of a field from its first two corners.
void uvFit(const FieldPolygon& f, double& scale, double& angle)
{
    const std::vector<double> c = layoutCorners(f);
    const double dx = c[2] - c[0], dy = c[3] - c[1];
    const double du = f.uv[2] - f.uv[0], dv = f.uv[3] - f.uv[1];
    scale = std::sqrt(du * du + dv * dv) / std::sqrt(dx * dx + dy * dy);
    angle = std::atan2(dv, du) - std::atan2(dy, dx);
    while (angle > kPi) angle -= 2 * kPi;
    while (angle < -kPi) angle += 2 * kPi;
}

} // namespace

// ---- the direction field ----

C5R_TEST(perlin_permutation_and_simplex_noise)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // the figures below reach the log even if a later case fails
    const uint8_t* p = perlinPermutation();
    const uint8_t first[16] = {151, 160, 137, 91, 90, 15, 131, 13, 201, 95, 96, 53, 194, 233, 7, 225};   // the text's 7
    CHECK(std::memcmp(p, first, 16) == 0);
    std::vector<int> seen(256, 0);
    for (int i = 0; i < 256; ++i) seen[p[i]]++;
    CHECK(std::count(seen.begin(), seen.end(), 1) == 256);   // a permutation of 0..255
    CHECK_EQ(simplex2(0.0, 0.0), 0.0);                       // zero at a lattice point
    double lo = 1e9, hi = -1e9;
    for (int j = 0; j < 200; ++j)
        for (int i = 0; i < 200; ++i) {
            const double v = simplex2(i * 0.0731, j * 0.0617);
            lo = (std::min)(lo, v);
            hi = (std::max)(hi, v);
        }
    CHECK(lo >= -1.0 && hi <= 1.0);
    CHECK(lo < -0.7 && hi > 0.7);   // the range is used
    // smooth: a small step changes the value a little
    CHECK(std::fabs(simplex2(3.3, 4.4) - simplex2(3.3001, 4.4)) < 0.01);
}

C5R_TEST(field_angle_in_the_texts_range)
{
    const FieldParams f = fieldParamsFor(12345);
    CHECK(f.ox >= 0 && f.ox < 256 && f.oy >= 0 && f.oy < 256);
    const FieldParams g = fieldParamsFor(12346);
    CHECK(f.ox != g.ox || f.oy != g.oy);
    double lo = 1e9, hi = -1e9;
    for (int j = 0; j < 300; ++j)
        for (int i = 0; i < 300; ++i) {
            const double a = fieldAngle(f, i * 23.0, j * 19.0);
            lo = (std::min)(lo, a);
            hi = (std::max)(hi, a);
        }
    CHECK(lo >= kBaseAngle - kSpan - 1e-9 && hi <= kBaseAngle + kSpan + 1e-9);
    CHECK(lo < kBaseAngle - 0.3 && hi > kBaseAngle + 0.3);
    std::printf("field angle over 90,000 points: %.1f .. %.1f degrees (text: 37.57 +- 33.75)\n", lo * 180 / kPi, hi * 180 / kPi);
}

C5R_TEST(plot_geometry_round_trips)
{
    for (int32_t y = 0; y < 12; ++y)
        for (int32_t x = 0; x < 16; ++x) {
            double c[2];
            plotCentreLayout(x, y, c);
            int32_t px = -1, py = -1;
            CHECK(plotAt(c[0], c[1], 16, 12, px, py));
            CHECK(px == x && py == y);
            CHECK(plotAt(c[0] + 25.0, c[1] - 20.0, 16, 12, px, py));   // inside the hex (inradius 32)
            CHECK(px == x && py == y);
        }
    double c[2];
    plotCentreLayout(1, 1, c);
    double d[2];
    plotCentreLayout(1, 2, d);
    CHECK(std::fabs((d[1] - c[1]) - 55.425) < 0.01);   // rows 55.43 apart in L
    int32_t px = 0, py = 0;
    CHECK(!plotAt(-500.0, 100.0, 16, 12, px, py));
    CHECK(!plotAt(std::numeric_limits<double>::quiet_NaN(), 100.0, 16, 12, px, py));
    CHECK(!plotAt(10.0, 10.0, 0, 12, px, py));
}

C5R_TEST(seed_tile_fifty_points)
{
    const std::vector<double> t = seedTile(99);
    CHECK_EQ(t.size(), static_cast<size_t>(100));
    for (double v : t) {
        CHECK(v >= 0 && v <= 63.936 + 1e-9);
        const double k = v / 0.064;
        CHECK(std::fabs(k - std::floor(k + 0.5)) < 1e-6);
    }
    CHECK(seedTile(99) == t);
    CHECK(seedTile(100) != t);
}

// ---- ear clipping ----

C5R_TEST(ear_clipping_as_the_farm_manager)
{
    std::vector<uint32_t> tris;
    const double square[8] = {0, 0, 10, 0, 10, 10, 0, 10};
    CHECK(earClip(square, 4, tris));
    CHECK(tris == std::vector<uint32_t>({0, 1, 2, 0, 2, 3}));   // the fan from vertex 0
    const double pent[10] = {0, 0, 4, -1, 6, 3, 3, 6, -1, 3};
    CHECK(earClip(pent, 5, tris));
    CHECK(tris == std::vector<uint32_t>({0, 1, 2, 0, 2, 3, 0, 3, 4}));
    // a concave L: triangles cover its area exactly
    const double ell[12] = {0, 0, 4, 0, 4, 1, 1, 1, 1, 4, 0, 4};
    CHECK(earClip(ell, 6, tris));
    CHECK_EQ(tris.size(), static_cast<size_t>(12));
    double area = 0;
    for (size_t k = 0; k < tris.size(); k += 3) {
        const double* a = &ell[2 * tris[k]];
        const double* b = &ell[2 * tris[k + 1]];
        const double* c = &ell[2 * tris[k + 2]];
        area += 0.5 * ((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]));
    }
    CHECK(std::fabs(area - 7.0) < 1e-9);
    // clockwise works too (the orientation is the polygon's)
    const double cw[8] = {0, 0, 0, 10, 10, 10, 10, 0};
    CHECK(earClip(cw, 4, tris));
    // refusals: fewer than 3 points, all collinear (no ear), the float form agrees
    CHECK(!earClip(square, 2, tris));
    CHECK(tris.empty());
    const double line[8] = {0, 0, 1, 0, 2, 0, 3, 0};
    CHECK(!earClip(line, 4, tris));
    CHECK(tris.empty());
    const float sq[8] = {0, 0, 10, 0, 10, 10, 0, 10};
    CHECK(earClip(sq, 4, tris));
    CHECK_EQ(tris.size(), static_cast<size_t>(6));
}

// ---- one block's graph ----

// The face statistics against the offset seeds' separation radius (the reading of 3.4 that matches stock's areas).
C5R_TEST(separation_sweep)
{
    const LayoutInput in = farmland(16, 12, 4);
    const FieldParams f = fieldParamsFor(in.gameSeed);
    const double bounds[4] = {0, 0, in.worldRect[2], in.worldRect[3] / kLayoutYScale};
    const double radii[5] = {kSepOffset, kSepOffset, kSepOffset, kSepOffset, kSepOffset};
    const double arcs[5] = {48, 64, 96, 128, 1e9};
    for (int k = 0; k < 5; ++k) {
        LayoutParams p;
        p.sepOffset = radii[k];
        p.contArc = arcs[k];
        BlockGraph g;
        CHECK_OK(layoutBlock(in.gameSeed, f, 1, 1, bounds, g, p));
        std::vector<double> areas, verts;
        for (const GraphFace& face : g.faces) {
            areas.push_back(face.area);
            verts.push_back(static_cast<double>(face.pts.size() / 2));
        }
        std::printf("sweep: arc %.0f point %.1f offset %.1f: %u lines, %zu faces, area p5/p50/p95 %.0f / %.0f / %.0f, vertices p50/p95 %.0f / %.0f\n",
                    p.contArc, p.sepPoint, p.sepOffset, g.lines, g.faces.size(), quant(areas, 0.05), quant(areas, 0.5), quant(areas, 0.95),
                    quant(verts, 0.5), quant(verts, 0.95));
    }
}

C5R_TEST(block_graph_faces_and_statistics)
{
    const LayoutInput in = farmland(16, 12, 4);
    const FieldParams f = fieldParamsFor(in.gameSeed);
    const double bounds[4] = {0, 0, in.worldRect[2], in.worldRect[3] / kLayoutYScale};
    BlockGraph g;
    const auto t0 = std::chrono::steady_clock::now();
    CHECK_OK(layoutBlock(in.gameSeed, f, 1, 1, bounds, g));
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    CHECK(g.lines > 0 && g.seedsAccepted > 0 && g.seedsTried >= g.seedsAccepted);
    CHECK(!g.faces.empty());
    std::vector<double> areas, verts;
    for (const GraphFace& face : g.faces) {
        CHECK(face.area > 0);
        CHECK(std::fabs(polyArea(face.pts) - face.area) < 1e-6 * (1 + face.area));   // counter-clockwise
        CHECK(face.pts.size() >= 6);
        CHECK(face.cx >= g.window[0] && face.cx <= g.window[2] && face.cy >= g.window[1] && face.cy <= g.window[3]);
        areas.push_back(face.area);
        verts.push_back(static_cast<double>(face.pts.size() / 2));
    }
    // every live edge lies on a cycle (no dead end after pruning): every vertex of a live edge has degree >= 2
    std::vector<int> deg(g.vertices.size() / 2, 0);
    for (const GraphEdge& e : g.edges)
        if (e.live) {
            deg[e.v0]++;
            deg[e.v1]++;
            CHECK(e.family == 1 || e.family == 2);
            CHECK(e.pts.size() >= 4);
        }
    for (const GraphEdge& e : g.edges)
        if (e.live) CHECK(deg[e.v0] >= 2 && deg[e.v1] >= 2);
    std::printf("block (1, 1): window %.0f x %.0f L, %u lines, seeds %u tried / %u accepted, %zu faces, area p5/p50/p95 %.0f / %.0f / "
                "%.0f L^2 (stock 27 / 186 / 398), vertices p5/p50/p95/max %.0f / %.0f / %.0f / %.0f (stock 4 / 5 / 8 / 14), %.1f ms\n",
                g.window[2] - g.window[0], g.window[3] - g.window[1], g.lines, g.seedsTried, g.seedsAccepted, g.faces.size(),
                quant(areas, 0.05), quant(areas, 0.5), quant(areas, 0.95), quant(verts, 0.05), quant(verts, 0.5), quant(verts, 0.95),
                quant(verts, 1.0), ms);
    CHECK(quant(areas, 0.5) > 90 && quant(areas, 0.5) < 400);
    // the same block twice: the same graph
    BlockGraph h;
    CHECK_OK(layoutBlock(in.gameSeed, f, 1, 1, bounds, h));
    CHECK_EQ(h.faces.size(), g.faces.size());
    for (size_t i = 0; i < g.faces.size() && i < h.faces.size(); ++i) CHECK(g.faces[i].pts == h.faces[i].pts);
    // negatives: bounds not finite or empty, a negative index; the output emptied
    const double nanb[4] = {0, 0, std::numeric_limits<double>::quiet_NaN(), 10};
    CHECK_ERR(layoutBlock(1, f, 0, 0, nanb, h), Err::BadLayout);
    CHECK(h.faces.empty() && h.edges.empty());
    const double emptyb[4] = {5, 5, 5, 10};
    CHECK_ERR(layoutBlock(1, f, 0, 0, emptyb, h), Err::BadLayout);
    CHECK_ERR(layoutBlock(1, f, -1, 0, bounds, h), Err::BadLayout);
    CHECK_ERR(layoutBlock(1, f, 0, 0, nullptr, h), Err::BadLayout);
    // a block outside the world: an empty graph, Ok
    CHECK_OK(layoutBlock(1, f, 50, 50, bounds, h));
    CHECK(h.faces.empty());
}

// ---- the field's look ----

C5R_TEST(field_uv_and_category)
{
    float u0, v0, u1, v1;
    fieldUv(10.0, 20.0, 10.0, 20.0, 0.0, u0, v0);
    CHECK(std::fabs(u0 - 0.5f) < 1e-6 && std::fabs(v0 - 0.5f) < 1e-6);   // the centroid at (0.5, 0.5) before the turn
    fieldUv(10.0 + 25.6, 20.0, 10.0, 20.0, 0.0, u1, v1);
    CHECK(std::fabs(u1 - u0 - 1.0f) < 1e-6 && std::fabs(v1 - v0) < 1e-6);   // 25.6 L = one texture repeat
    fieldUv(10.0 + 25.6, 20.0, 10.0, 20.0, kPi / 2, u1, v1);
    float uc, vc;
    fieldUv(10.0, 20.0, 10.0, 20.0, kPi / 2, uc, vc);
    CHECK(std::fabs(u1 - uc) < 1e-6 && std::fabs(v1 - vc - 1.0f) < 1e-6);   // row-vector rotation: x axis -> v
    FarmPlot p;
    uint32_t c = 99;
    p.artType = 0;
    CHECK(!fieldCategory(p, c));
    p.artType = 5;
    CHECK(!fieldCategory(p, c));
    const uint32_t byArt[5] = {0, kAmerica, kAsia, kAfrica, kEurope};
    for (uint8_t a = 1; a <= 4; ++a) {
        p.artType = a;
        CHECK(fieldCategory(p, c) && c == byArt[a]);
    }
    p.state = FarmState::HalfBuilt;
    CHECK(fieldCategory(p, c) && c == kHalfBuilt);
    p.wheat = true;
    CHECK(fieldCategory(p, c) && c == kWheatHalfBuilt);
    p.state = FarmState::Built;
    CHECK(fieldCategory(p, c) && c == kWheat);
    p.state = FarmState::Pillaged;
    CHECK(fieldCategory(p, c) && c == kWheat);   // pillaged: the normal category (the damage copy is added after it)
}

// ---- the whole layout ----

C5R_TEST(layout_farmland_against_the_texts_numbers)
{
    const LayoutInput in = farmland(16, 12, 4);
    FarmLayout l;
    FarmDecals d;
    const auto t0 = std::chrono::steady_clock::now();
    Invariant which = Invariant::kHash;
    CHECK_OK(produce(in, l, d, &which));
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    CHECK(which == Invariant::kNone);
    CHECK_EQ(l.groups.size(), static_cast<size_t>(4 * 3));   // 16 x 12 plots in blocks of 4 x 4
    std::vector<double> perFarm(in.plots.size(), 0), areas, scales, rw, rh;
    size_t turned = 0, outside = 0;
    for (const FieldPolygon& f : l.fields) {
        perFarm[f.plot] += 1;
        CHECK_EQ(f.category, static_cast<uint32_t>(kEurope));
        const std::vector<double> c = layoutCorners(f);
        areas.push_back(polyArea(c));
        double s, a;
        uvFit(f, s, a);
        scales.push_back(s);
        const double deg = a * 180 / kPi;
        // unturned in -(A +- span), turned that + 90 degrees (farm-fields-layout.md 7)
        const double lo = -(kBaseAngle + kSpan) * 180 / kPi, hi = -(kBaseAngle - kSpan) * 180 / kPi;
        if (deg >= lo - 0.5 && deg <= hi + 0.5) {
        } else if (deg >= lo + 89.5 && deg <= hi + 90.5) {
            ++turned;
        } else {
            ++outside;
        }
        double bx0 = 1e9, by0 = 1e9, bx1 = -1e9, by1 = -1e9;
        for (size_t k = 0; k < c.size() / 2; ++k) {
            bx0 = (std::min)(bx0, c[2 * k]);
            bx1 = (std::max)(bx1, c[2 * k]);
            by0 = (std::min)(by0, c[2 * k + 1]);
            by1 = (std::max)(by1, c[2 * k + 1]);
        }
        rw.push_back(bx1 - bx0);
        rh.push_back(by1 - by0);
    }
    CHECK_EQ(outside, static_cast<size_t>(0));
    const double share = static_cast<double>(turned) / l.fields.size();
    CHECK(share > 0.25 && share < 0.42);
    CHECK(std::fabs(quant(scales, 0.5) - 0.0390625) < 0.0002);
    size_t without = 0;
    for (double v : perFarm) without += v == 0;
    // tracks: the strip width 0.5 (pairs at one point), u 0 / 1, v steps of 1.3
    std::vector<double> widths;
    bool uv01 = true, vstep = true;
    for (const TrackStrip& t : l.tracks) {
        for (size_t k = 0; k + 23 < t.xyuv.size(); k += 24) {   // per segment: 6 vertices of 4 floats
            const float* q = &t.xyuv[k];
            const double dx = q[0] - q[4], dy = (q[1] - q[5]) / kLayoutYScale;   // vertex 0 (left, k) and 1 (right, k)
            widths.push_back(std::sqrt(dx * dx + dy * dy));
            uv01 = uv01 && q[2] == 0.0f && q[6] == 1.0f;
            vstep = vstep && std::fabs((q[11] - q[3]) - 1.3f) < 1e-5f;   // vertex 2 is at point k + 1
        }
    }
    CHECK(!l.tracks.empty());
    CHECK(uv01 && vstep);
    CHECK(std::fabs(quant(widths, 0.5) - 0.5) < 0.02);
    std::printf("farmland 16 x 12 (192 farms, Europe): %zu fields, %zu tracks, %zu decals, %.0f ms; fields per farm min/p50/max %.0f / %.0f / "
                "%.0f, %zu farms without (stock huge: 1-33, median 14, 44 of 425 without); area p5/p50/p95 %.0f / %.0f / %.0f L^2 (stock "
                "27 / 186 / 398); rectangle p50 %.1f x %.1f L (stock 20.7 x 20.8); uv scale p50 %.7f (0.0390625); turned %.1f %% (33.0); "
                "track width p5/p50/p95 %.3f / %.3f / %.3f (0.493 / 0.503 / 0.505)\n",
                l.fields.size(), l.tracks.size(), d.decals.size(), ms, quant(perFarm, 0.0), quant(perFarm, 0.5), quant(perFarm, 1.0),
                without, quant(areas, 0.05), quant(areas, 0.5), quant(areas, 0.95), quant(rw, 0.5), quant(rh, 0.5), quant(scales, 0.5),
                100 * share, quant(widths, 0.05), quant(widths, 0.5), quant(widths, 0.95));
    // every plot a farm: a farm keeps every face centred on it (plot 3,547 L^2 / ~186), so more than stock's 1-33 on a
    // real map, where faces centred on other plots are dropped
    CHECK(quant(perFarm, 0.5) >= 8 && quant(perFarm, 0.5) <= 40 && quant(perFarm, 1.0) <= 90);
    CHECK(quant(areas, 0.5) > 90 && quant(areas, 0.5) < 400);
    // determinism: byte-equal on a second run
    FarmLayout l2;
    FarmDecals d2;
    CHECK_OK(produce(in, l2, d2));
    CHECK(d2.decals.size() == d.decals.size() && d2.vertices.size() == d.vertices.size());
    CHECK(std::memcmp(d.decals.data(), d2.decals.data(), d.decals.size() * sizeof(decal::Decal)) == 0);
    CHECK(std::memcmp(d.vertices.data(), d2.vertices.data(), d.vertices.size() * sizeof(decal::DecalVertex)) == 0);
    CHECK(d.textures == d2.textures);
}

C5R_TEST(a_farm_added_and_removed_changes_only_its_neighbourhood)
{
    LayoutInput in = farmland(16, 12, 3, 4242, false);
    FarmDecals a, b;
    FarmLayout la, lb;
    CHECK_OK(produce(in, la, a));
    // remove one farm in the middle of a block (plot (5, 5) is in the pattern: (5 + 10) % 3 == 0)
    LayoutInput less = in;
    size_t at = less.plots.size();
    for (size_t i = 0; i < less.plots.size(); ++i)
        if (less.plots[i].x == 5 && less.plots[i].y == 5) at = i;
    CHECK(at < less.plots.size());
    less.plots.erase(less.plots.begin() + static_cast<std::ptrdiff_t>(at));
    CHECK_OK(produce(less, lb, b));
    FarmChange ch;
    CHECK_OK(farmChange(a, b, in.worldRect, ch));
    CHECK(ch.removed > 0);
    float c[2];
    plotCentre(5, 5, c);
    size_t distant = 0;   // (not `far`: a Windows macro)
    for (const decal::DecalRect& r : ch.rects) {
        const double mx = (r.v[0] + r.v[2]) * 0.5, my = (r.v[1] + r.v[3]) * 0.5;
        if (std::fabs(mx - c[0]) > 128 || std::fabs(my - c[1]) > 128) ++distant;
    }
    CHECK_EQ(distant, static_cast<size_t>(0));
    // every field of every other farm is byte-equal (fields depend on the game, the plot and its state only)
    size_t fa = 0, fb = 0;
    for (size_t i = 0; i < a.decals.size(); ++i) fa += !a.track[i];
    for (size_t i = 0; i < b.decals.size(); ++i) fb += !b.track[i];
    size_t removedFields = 0;
    for (const FieldPolygon& f : la.fields) removedFields += in.plots[f.plot].x == 5 && in.plots[f.plot].y == 5;
    CHECK_EQ(fa - fb, removedFields);
    // and added back: the same set as before
    FarmDecals a2;
    FarmLayout la2;
    CHECK_OK(produce(in, la2, a2));
    CHECK_OK(farmChange(a, a2, in.worldRect, ch));
    CHECK_EQ(ch.added + ch.removed, 0u);
    // pillaged: a damage copy after each of its fields, the fields themselves unchanged
    LayoutInput pil = in;
    pil.plots[at].state = FarmState::Pillaged;
    FarmLayout lp;
    FarmDecals dp;
    CHECK_OK(produce(pil, lp, dp));
    size_t dmg = 0, own = 0;
    for (size_t i = 0; i < lp.fields.size(); ++i) {
        if (lp.fields[i].category == kDamage) {
            ++dmg;
            CHECK(i > 0 && lp.fields[i - 1].corners == lp.fields[i].corners && lp.fields[i - 1].plot == lp.fields[i].plot);
        } else if (lp.fields[i].plot == at) {
            ++own;
        }
    }
    CHECK(dmg > 0 && dmg == own);
    CHECK_OK(farmChange(a, dp, in.worldRect, ch));
    CHECK_EQ(ch.removed, 0u);
    CHECK_EQ(ch.added, static_cast<uint32_t>(dmg));
}

C5R_TEST(wheat_half_built_and_no_continent)
{
    LayoutInput in = farmland(8, 8, 2, 31);
    for (FarmPlot& p : in.plots) {
        if (p.y == 1) p.wheat = true;
        if (p.y == 2) p.state = FarmState::HalfBuilt;
        if (p.y == 3) {
            p.wheat = true;
            p.state = FarmState::HalfBuilt;
        }
        if (p.y == 4) p.artType = 0;
    }
    FarmLayout l;
    FarmDecals d;
    CHECK_OK(produce(in, l, d));
    size_t byRow[8][kCategoryCount] = {};
    for (const FieldPolygon& f : l.fields) byRow[in.plots[f.plot].y][f.category]++;
    CHECK(byRow[1][kWheat] > 0 && byRow[1][kAsia] == 0);
    CHECK(byRow[2][kHalfBuilt] > 0 && byRow[2][kAsia] == 0);
    CHECK(byRow[3][kWheatHalfBuilt] > 0 && byRow[3][kWheat] == 0);
    size_t row4 = 0;
    for (uint32_t c = 0; c < kCategoryCount; ++c) row4 += byRow[4][c];
    CHECK_EQ(row4, static_cast<size_t>(0));   // art type 0: no field decal
    CHECK(byRow[5][kAsia] > 0);
    // a category without variants: no field
    LayoutInput none = in;
    none.types.hasSpec[kAsia].clear();
    FarmLayout ln;
    FarmDecals dn;
    CHECK_OK(produce(none, ln, dn));
    for (const FieldPolygon& f : ln.fields) CHECK(f.category != kAsia);
}

C5R_TEST(parallel_blocks_and_the_cache_give_the_same_bytes)
{
    const LayoutInput in = farmland(16, 12, 4, 99);
    FarmLayout l0, l1, l8, lc;
    FarmDecals d0, d1, d8, dc;
    CHECK_OK(produce(in, l0, d0));   // no cache: the blocks in parallel on the default threads
    BlockCache one;
    one.threads = 1;
    CHECK_OK(produce(in, l1, d1, nullptr, &one));
    BlockCache eight;
    eight.threads = 8;
    CHECK_OK(produce(in, l8, d8, nullptr, &eight));
    CHECK_EQ(eight.misses, static_cast<uint64_t>(12));
    CHECK_EQ(eight.hits, static_cast<uint64_t>(0));
    CHECK_OK(produce(in, lc, dc, nullptr, &eight));   // every block from the cache
    CHECK_EQ(eight.misses, static_cast<uint64_t>(12));
    CHECK_EQ(eight.hits, static_cast<uint64_t>(12));
    const FarmDecals* all[3] = {&d1, &d8, &dc};
    for (const FarmDecals* d : all) {
        CHECK(d->decals.size() == d0.decals.size() && d->vertices.size() == d0.vertices.size() && d->textures == d0.textures);
        CHECK(std::memcmp(d->decals.data(), d0.decals.data(), d0.decals.size() * sizeof(decal::Decal)) == 0);
        CHECK(std::memcmp(d->vertices.data(), d0.vertices.data(), d0.vertices.size() * sizeof(decal::DecalVertex)) == 0);
    }
    // another seed or world empties the cache
    LayoutInput other = in;
    other.gameSeed = 100;
    CHECK_OK(produce(other, lc, dc, nullptr, &eight));
    CHECK_EQ(eight.misses, static_cast<uint64_t>(24));
}

C5R_TEST(layout_refuses_a_bad_input)
{
    LayoutInput in = farmland(8, 8, 2);
    in.worldRect[2] = std::numeric_limits<float>::quiet_NaN();
    FarmLayout l;
    l.groups.resize(2);
    CHECK_ERR(layoutFarms(in, l), Err::BadLayout);
    CHECK(l.groups.empty() && l.fields.empty() && l.tracks.empty());
    in = farmland(8, 8, 2);
    in.plots[3].x = 99;
    CHECK_ERR(layoutFarms(in, l), Err::BadLayout);
    CHECK(l.groups.empty());
}

// ---- the calibration of the offset seeds' separation (not run by ctest; render/evidence/0528/calibration.txt) ----
// c5r_farmfields_layout.exe --calibrate: bisects LayoutParams::sepOffset in [9.6, 13.6] so that the median area of the
// fields of three all-farm 16 x 12 maps (game seeds 777, 4242, 31337, pooled) is stock's 186 L^2 (farm-fields-layout.md
// 11: 186 / 188 on two huge recordings); prints every evaluation with p5 / p50 / p95 (stock 27 / 186 / 398 and 23 / 188 /
// 384), the fields per farm and the time. Exit 0.
namespace {

struct Eval {
    double r, p5, p50, p95, perFarm, ms;
    size_t fields;
};

double g_arc = 1e30;   // the piece length evaluate() uses (the default: no cut)

Eval evaluate(double r)
{
    Eval e{r, 0, 0, 0, 0, 0, 0};
    std::vector<double> areas, perFarm;
    const auto t0 = std::chrono::steady_clock::now();
    const uint32_t seeds[3] = {777, 4242, 31337};
    for (uint32_t s : seeds) {
        const LayoutInput in = farmland(16, 12, 4, s);
        const FieldParams fp = fieldParamsFor(s);
        const double bounds[4] = {0, 0, in.worldRect[2], in.worldRect[3] / kLayoutYScale};
        LayoutParams p;
        p.sepOffset = r;
        p.contArc = g_arc;
        std::vector<double> farm(in.plots.size(), 0);
        for (int32_t by = 0; by < 3; ++by)
            for (int32_t bx = 0; bx < 4; ++bx) {
                BlockGraph g;
                layoutBlock(s, fp, bx, by, bounds, g, p);
                for (const GraphFace& face : g.faces) {
                    int32_t px = 0, py = 0;
                    if (!plotAt(face.cx, face.cy, in.mapW, in.mapH, px, py)) continue;
                    if (px / kBlockPlots != bx || py / kBlockPlots != by) continue;   // the block's own plots, as layoutFarms
                    std::vector<uint32_t> tris;
                    if (!earClip(face.pts.data(), face.pts.size() / 2, tris)) continue;
                    areas.push_back(face.area);
                    farm[static_cast<size_t>(py) * in.mapW + px] += 1;
                }
            }
        perFarm.insert(perFarm.end(), farm.begin(), farm.end());
    }
    e.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    e.fields = areas.size();
    e.p5 = quant(areas, 0.05);
    e.p50 = quant(areas, 0.5);
    e.p95 = quant(areas, 0.95);
    e.perFarm = quant(perFarm, 0.5);
    return e;
}

void printEval(const Eval& e)
{
    std::printf("| %.4f | %zu | %.0f | %.0f | %.0f | %.0f | %.0f |\n", e.r, e.fields, e.p5, e.p50, e.p95, e.perFarm, e.ms);
}

// --textvalues: the text's parameters (3.2 as corrected) with the piece length contArc varied, the same statistics.
int textValues()
{
    std::printf("the text's parameters (farm-fields-layout.md 3.2-3.4 as corrected 2026-11-18 02:35): point / offset / "
                "continuation radii %.1f / %.1f / %.1f, offset spacing %.1f, stop %.1f, near %.1f; all-farm 16 x 12 maps, seeds "
                "777 / 4242 / 31337 pooled; exact face areas (stock exact triangle areas on the huge recordings: p5 / p50 / p95 "
                "81 / 218 / 427 and 72 / 217 / 417, compare_huge.txt)\n\n",
                kSepPoint, kSepOffset, kSepContinuation, kOffsetSeedSpacing, kStopDistance, kNearZone);
    std::printf("| contArc | fields | area p5 | p50 | p95 | fields per farm p50 | ms |\n|---|---|---|---|---|---|---|\n");
    const double arcs[6] = {48, 64, 96, 128, 192, 1e9};
    for (double arc : arcs) {
        g_arc = arc;
        Eval e = evaluate(kSepOffset);
        e.r = arc;
        std::printf("| %.0f | %zu | %.0f | %.0f | %.0f | %.0f | %.0f |\n", arc >= 1e8 ? -1.0 : arc, e.fields, e.p5, e.p50, e.p95, e.perFarm,
                    e.ms);
    }
    g_arc = LayoutParams().contArc;
    std::printf("\n(contArc -1 = no cut: every half line traced to the window's edge)\n");
    return 0;
}

int calibrate()
{
    std::printf("calibration of the offset seeds' separation (4852): all-farm 16 x 12 maps, seeds 777 / 4242 / 31337 pooled, "
                "contArc %.0f, point seeds %.1f; target median field area 186 L^2 (stock p5 / p50 / p95 27 / 186 / 398)\n\n",
                LayoutParams().contArc, kSepPoint);
    std::printf("| sepOffset | fields | area p5 | p50 | p95 | fields per farm p50 | ms |\n|---|---|---|---|---|---|---|\n");
    const double fixedR[3] = {4.0, 9.6, 13.6};
    for (double r : fixedR) printEval(evaluate(r));
    double lo = 9.6, hi = 13.6;
    Eval best = evaluate(lo);
    for (int it = 0; it < 10; ++it) {
        const double mid = 0.5 * (lo + hi);
        const Eval e = evaluate(mid);
        printEval(e);
        if (std::fabs(e.p50 - 186.0) < std::fabs(best.p50 - 186.0)) best = e;
        if (e.p50 < 186.0) lo = mid;
        else hi = mid;
    }
    std::printf("\nresult: sepOffset %.4f gives area p5 / p50 / p95 %.0f / %.0f / %.0f L^2\n", best.r, best.p5, best.p50, best.p95);
    return 0;
}

} // namespace

// ---- the offline comparison's producer (render/evidence/0528/compare_huge.py runs it) ----
// c5r_farmfields_layout.exe --produce IN OUT: IN is text: "map W H", "world X0 Y0 X1 Y1", "seed S", "types N0 .. N7"
// (children per category, no spec), then "farm X Y STATE ART WHEAT" lines (ascending by y, then x). OUT: "time_ms T",
// "groups G", per field "F plot category child n x0 y0 u0 v0 ..." (the sample frame) and per track "T plot n x y u v ...".
// Exit 0 written, 2 usage, 3 the input or the producer refused.
namespace {

int produceFile(const char* inPath, const char* outPath)
{
    FILE* f = std::fopen(inPath, "r");
    if (!f) return 2;
    LayoutInput in;
    char key[32];
    while (std::fscanf(f, "%31s", key) == 1) {
        const std::string k = key;
        if (k == "map") {
            if (std::fscanf(f, "%u %u", &in.mapW, &in.mapH) != 2) break;
        } else if (k == "world") {
            if (std::fscanf(f, "%f %f %f %f", &in.worldRect[0], &in.worldRect[1], &in.worldRect[2], &in.worldRect[3]) != 4) break;
        } else if (k == "seed") {
            if (std::fscanf(f, "%u", &in.gameSeed) != 1) break;
        } else if (k == "types") {
            for (uint32_t c = 0; c < kCategoryCount; ++c) {
                unsigned n = 0;
                if (std::fscanf(f, "%u", &n) != 1) break;
                in.types.hasSpec[c].assign(n, 0);
            }
        } else if (k == "farm") {
            int x, y, st, art, wh;
            if (std::fscanf(f, "%d %d %d %d %d", &x, &y, &st, &art, &wh) != 5) break;
            FarmPlot p;
            p.x = x;
            p.y = y;
            p.state = static_cast<FarmState>(st);
            p.artType = static_cast<uint8_t>(art);
            p.wheat = wh != 0;
            in.plots.push_back(p);
        }
    }
    std::fclose(f);
    FarmLayout l;
    FarmDecals d;
    BlockCache cache;
    const auto t0 = std::chrono::steady_clock::now();
    const Status s = produce(in, l, d, nullptr, &cache);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!s.ok()) {
        std::fprintf(stderr, "produce: %s\n", s.str().c_str());
        return 3;
    }
    // a change in play: the first farm pillaged, with the load's block graphs cached
    if (!in.plots.empty()) {
        LayoutInput again = in;
        again.plots[0].state = again.plots[0].state == FarmState::Pillaged ? FarmState::Built : FarmState::Pillaged;
        FarmLayout l2;
        FarmDecals d2;
        const auto t1 = std::chrono::steady_clock::now();
        const Status s2 = produce(again, l2, d2, nullptr, &cache);
        const double ms2 = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        std::printf("produce again with one farm changed, block graphs cached: %s, %.1f ms (cache hits %llu)\n", s2.ok() ? "ok" : "refused",
                    ms2, static_cast<unsigned long long>(cache.hits));
    }
    FILE* o = std::fopen(outPath, "w");
    if (!o) return 2;
    std::fprintf(o, "time_ms %.1f\ngroups %zu\n", ms, l.groups.size());
    for (const FieldPolygon& p : l.fields) {
        std::fprintf(o, "F %u %u %u %zu", p.plot, p.category, p.child, p.corners.size() / 2);
        for (size_t k = 0; k < p.corners.size() / 2; ++k)
            std::fprintf(o, " %.4f %.4f %.6f %.6f", p.corners[2 * k], p.corners[2 * k + 1], p.uv[2 * k], p.uv[2 * k + 1]);
        std::fprintf(o, "\n");
    }
    for (const TrackStrip& t : l.tracks) {
        std::fprintf(o, "T %u %zu", t.plot, t.xyuv.size() / 4);
        for (float v : t.xyuv) std::fprintf(o, " %.4f", v);
        std::fprintf(o, "\n");
    }
    std::fclose(o);
    std::printf("produce: %zu farms -> %zu groups, %zu fields, %zu tracks, %.1f ms; the span text (render_farmfields_state) %zu bytes\n",
                in.plots.size(), l.groups.size(), l.fields.size(), l.tracks.size(), ms, spanText(in, 3, -1).size());
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--calibrate") == 0) return calibrate();
    if (argc > 1 && std::strcmp(argv[1], "--textvalues") == 0) return textValues();
    if (argc > 1 && std::strcmp(argv[1], "--produce") == 0) return argc == 4 ? produceFile(argv[2], argv[3]) : 2;
    return ::c5r::test::runAll();
}
