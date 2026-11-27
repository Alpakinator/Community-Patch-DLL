// The coast strips' tests (ticket 4911; live-terrain.md 8.15). Synthetic lines, styles and TerrainDef rows (no value
// here is the stock file's); every input check has a negative test. game_terrain_def reads the install's TerrainDef.csv.
#include <chrono>
#include <cmath>
#include <cstring>
#include "c5r/bake/bake_types.h"
#include "c5r/bake/layers.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/liveterrain/coaststrips.h"
#include "../../common/testkit.h"

using namespace c5r;
using namespace c5r::liveterrain;

namespace {

// rows: 0 generic code 0x7 (continent 0), 1 continent 1 code 0x7, 2 generic code 0x9, 3 continent 1 code 0x35,
// 4 continent 1 code 0x10, 5 continent 2 code 0x10 (no 0x35 there), 6 no id
StripMaterials synMaterials() {
    StripMaterials m;
    m.rowId = {0x100007u, 0x110007u, 0x100009u, 0x110035u, 0x110010u, 0x120010u, 0xFFFFFFFFu};
    return m;
}

// One style (kind 1): line heights 0.30 0.31 0.32 0.33 0.34, weights 0 1 1 1 0, band 1 code 0x9 (row 2), band 2 code
// 0x7 (row 1 by continent 1 first).
StripStyles synStyles() {
    StripStyles s;
    s.present[1] = true;
    StripStyle& k = s.kind[1];
    const float h[5] = {0.30f, 0.31f, 0.32f, 0.33f, 0.34f}, w[5] = {0, 1, 1, 1, 0};
    for (int i = 0; i < 5; ++i) { k.height[i] = h[i]; k.weight[i] = w[i]; }
    k.bandTerrain[0] = -1; k.bandTerrain[1] = 0x9; k.bandTerrain[2] = 0x7; k.bandTerrain[3] = -1;
    return s;
}

// A straight coast along x at rows y = base + offset[i], L points from x = x0 in steps of dx.
StripLines straightLines(uint32_t L, float x0, float dx, float base, uint8_t kind = 1) {
    const float off[5] = {-20.0f, -1.5f, 0.0f, 1.5f, 20.0f};
    StripLines s;
    for (int i = 0; i < 5; ++i)
        for (uint32_t j = 0; j < L; ++j) {
            StripPoint p;
            p.x = x0 + dx * float(j); p.y = base + off[i]; p.t = 0.0f; p.styleA = kind; p.styleB = kind;
            s.line[i].push_back(p);
        }
    return s;
}

struct Field {
    uint32_t W, H;
    std::vector<uint16_t> h;
    std::vector<uint8_t> m;
    Field(uint32_t w, uint32_t hh, uint16_t hv, uint8_t mv) : W(w), H(hh), h(size_t(w) * hh, hv), m(size_t(w) * hh, mv) {}
    StripField whole(bool wrap) {
        StripField f; f.samplesX = W; f.samplesY = H; f.wrapX = wrap; f.w = W; f.h = H;
        f.heights = h.data(); f.materials = m.data();
        return f;
    }
};

} // namespace

// ---------------------------------------------------------------- materials
C5R_TEST(materials_resolve) {
    const StripMaterials m = synMaterials();
    CHECK_EQ(m.find(0x110007u), 1);
    CHECK_EQ(m.find(0x130000u), -1);
    CHECK_EQ(m.resolveBand(0x7), uint8_t(1));      // continent 1 first
    CHECK_EQ(m.resolveBand(0x9), uint8_t(2));      // then the generic row
    CHECK_EQ(m.resolveBand(0x5), kNoMaterial);     // neither
    CHECK_EQ(m.resolveBand(-1), kNoMaterial);
    uint8_t from[4], to[4];
    m.swapPairs(from, to);
    CHECK_EQ(from[0], uint8_t(3)); CHECK_EQ(to[0], uint8_t(4));
    CHECK_EQ(from[1], kNoMaterial); CHECK_EQ(to[1], uint8_t(5));     // the target row alone: from stays none
    CHECK_EQ(from[2], kNoMaterial); CHECK_EQ(to[2], kNoMaterial);
}

C5R_TEST(materials_from_layer_tables) {
    bake::LayerTables t;
    bake::TextureSet a; a.continent = "1"; a.id = 0x35; t.sets.push_back(a);
    bake::TextureSet b; b.continent = "0"; b.id = 0x5; t.sets.push_back(b);
    bake::TextureSet c; c.continent = "-1"; c.id = -1; t.sets.push_back(c);
    StripMaterials m;
    CHECK_OK(StripMaterials::fromLayerTables(t, m));
    CHECK_EQ(m.rowId.size(), size_t(3));
    CHECK_EQ(m.rowId[0], 0x110035u);
    CHECK_EQ(m.rowId[1], 0x100005u);
    CHECK_EQ(m.rowId[2], 0xFFFFFFFFu);
}

C5R_TEST(materials_bad) {
    StripMaterials m; m.rowId = {1u};
    bake::LayerTables t;
    CHECK_ERR(StripMaterials::fromLayerTables(t, m), Err::BadLayout);          // no rows
    CHECK_EQ(m.rowId.size(), size_t(1));                                      // untouched
    bake::TextureSet a; a.continent = "x1"; a.id = 1; t.sets.push_back(a);
    CHECK_ERR(StripMaterials::fromLayerTables(t, m), Err::BadLayout);          // continent not a number
    t.sets[0].continent = "300";
    CHECK_ERR(StripMaterials::fromLayerTables(t, m), Err::BadLayout);          // out of range
    CHECK_EQ(m.rowId.size(), size_t(1));
}

// ---------------------------------------------------------------- the mesh
C5R_TEST(mesh_shape) {
    StripMesh m;
    CHECK_OK(buildStripMesh(straightLines(10, 100.0f, 2.0f, 50.0f), synStyles(), synMaterials(), m));
    CHECK_EQ(m.vertices.size(), size_t(9 * 8));
    CHECK_EQ(m.triangles(), 8u * 8u);
    CHECK_EQ(m.segments(), 1u);
    CHECK_EQ(m.segmentEnd[0], 64u);
    // column 0: midpoints of points 0 and 1 (x 101), band 1 inner on line 1, outer on line 2
    const StripVertex& in1 = m.vertices[2], &out1 = m.vertices[3];
    CHECK_EQ(in1.x, 101.0f); CHECK_EQ(in1.y, 48.5f); CHECK_EQ(out1.y, 50.0f);
    CHECK_EQ(in1.h, 0.31f); CHECK_EQ(out1.h, 0.32f); CHECK_EQ(in1.w, 1.0f);
    CHECK_EQ(in1.material, uint8_t(2)); CHECK_EQ(out1.material, uint8_t(2));
    CHECK_EQ(m.vertices[0].material, kNoMaterial);     // band 0
    CHECK_EQ(m.vertices[0].w, 0.0f);
    CHECK_EQ(m.vertices[4].material, uint8_t(1));      // band 2
    // the last column (j = 8) is the midpoint of points 8 and 9
    CHECK_EQ(m.vertices[8 * 8].x, 117.0f);
    // the triangles of quad 0, band 0
    CHECK_EQ(m.tris[0], 0u); CHECK_EQ(m.tris[1], 1u); CHECK_EQ(m.tris[2], 8u);
    CHECK_EQ(m.tris[3], 1u); CHECK_EQ(m.tris[4], 9u); CHECK_EQ(m.tris[5], 8u);
    CHECK_EQ(m.minX, 101.0f); CHECK_EQ(m.maxX, 117.0f); CHECK_EQ(m.minY, 30.0f); CHECK_EQ(m.maxY, 70.0f);
}

C5R_TEST(mesh_segment_rule) {
    for (uint32_t L = 3; L < 400; ++L) {
        StripMesh m;
        CHECK_OK(buildStripMesh(straightLines(L, 0.0f, 1.0f, 50.0f), synStyles(), synMaterials(), m));
        const uint32_t T = m.triangles(), c = m.segmentTris, n = m.segments();
        CHECK_EQ(T, 8 * (L - 2));
        CHECK(c >= 64 && c % 8 == 0);
        CHECK(n == 1 || n % 2 == 0);
        CHECK(!(T % c >= 1 && T % c <= 7));
        CHECK_EQ(n, (T - 1) / c + 1);
        CHECK_EQ(m.segmentEnd.back(), T);
    }
}

C5R_TEST(mesh_style_switch_and_parity) {
    // kind 1 then kind 2 from column 3 on: the profile is re-read once, the parity flips, f = 1 - t
    StripStyles st = synStyles();
    st.present[2] = true;
    for (int i = 0; i < 5; ++i) { st.kind[2].height[i] = 0.5f; st.kind[2].weight[i] = 1.0f; }
    st.kind[2].bandTerrain[0] = st.kind[2].bandTerrain[1] = st.kind[2].bandTerrain[2] = st.kind[2].bandTerrain[3] = 0x9;
    StripLines s = straightLines(8, 0.0f, 1.0f, 50.0f);
    for (int i = 0; i < 5; ++i)
        for (uint32_t j = 3; j < 8; ++j) { s.line[i][j].styleA = 2; s.line[i][j].styleB = 1; s.line[i][j].t = 0.25f; }
    StripMesh m;
    CHECK_OK(buildStripMesh(s, st, synMaterials(), m));
    // column 3 = midpoint of points 3 and 4: style A 2, t 0.25, odd parity -> f = 0.75: h = B(kind 1) * 0.25 + A * 0.75
    const StripVertex& v = m.vertices[3 * 8 + 2];
    CHECK_EQ(v.h, 0.31f * 0.25f + 0.5f * 0.75f);
    CHECK_EQ(v.material, uint8_t(2));                  // f > 0.5: A's band 1 (code 0x9 -> row 2)
    CHECK_EQ(m.vertices[3 * 8 + 0].material, uint8_t(2));   // A's band 0 (0x9) where kind 1 had none
    // column 2 = midpoint of points 2 and 3: bytes from point 2 (kind 1), t = 0.125, even parity -> f = 0.125
    CHECK_EQ(m.vertices[2 * 8 + 2].h, 0.31f * 0.875f + 0.31f * 0.125f);
}

C5R_TEST(mesh_absent_kind_is_zero) {
    StripMaterials mats = synMaterials();
    mats.rowId.push_back(0x110000u);                   // a code-0 row (continent 1): what an absent kind's bands become
    StripMesh m;
    CHECK_OK(buildStripMesh(straightLines(5, 0.0f, 1.0f, 50.0f, 7), synStyles(), mats, m));
    CHECK_EQ(m.vertices[2].h, 0.0f);
    CHECK_EQ(m.vertices[2].w, 0.0f);
    CHECK_EQ(m.vertices[0].material, uint8_t(7));
    StripMesh m2;
    CHECK_OK(buildStripMesh(straightLines(5, 0.0f, 1.0f, 50.0f, 200), synStyles(), mats, m2));   // kind >= 13
    CHECK_EQ(m2.vertices[2].h, 0.0f);
}

C5R_TEST(mesh_bad_and_short) {
    StripLines s = straightLines(10, 0.0f, 1.0f, 50.0f);
    s.line[3].pop_back();
    StripMesh m;
    m.segmentTris = 99;
    CHECK_ERR(buildStripMesh(s, synStyles(), synMaterials(), m), Err::BadLayout);
    CHECK_EQ(m.segmentTris, 99u);                      // untouched
    CHECK_OK(buildStripMesh(straightLines(2, 0.0f, 1.0f, 50.0f), synStyles(), synMaterials(), m));
    CHECK_EQ(m.triangles(), 0u);
    CHECK_EQ(m.segments(), 0u);
}

// ---------------------------------------------------------------- the raster
C5R_TEST(raster_flat_strip) {
    StripMesh m;
    CHECK_OK(buildStripMesh(straightLines(40, 10.0f, 4.0f, 60.0f), synStyles(), synMaterials(), m));
    Field fd(200, 130, 9000, 11);
    StripField f = fd.whole(false);
    StripStats st;
    CHECK_OK(applyStrips({m}, synMaterials(), f, &st));
    CHECK(st.trianglesDrawn > 0);
    CHECK_EQ(st.meshesTouching, 1u);
    // band 1 (rows 58.5..60) and band 2 (60..61.5) have w = 1: the height is the interpolated profile, +-1 for f32
    const auto H = [&](uint32_t x, uint32_t y) { return int(fd.h[size_t(y) * 200 + x]); };
    const auto M = [&](uint32_t x, uint32_t y) { return int(fd.m[size_t(y) * 200 + x]); };
    CHECK(std::abs(H(80, 60) - int(0.32f * 65535.0f)) <= 1);
    CHECK(std::abs(H(80, 59) - int((0.31f * 2.0f + 0.32f) / 3.0f * 65535.0f)) <= 1);   // a third of the way to line 2
    CHECK_EQ(M(80, 59), 2);                            // band 1's material (generic row of code 0x9)
    CHECK_EQ(M(80, 61), 1);                            // band 2's
    // band 0: between the old height and the profile, not written beyond line 0 (row 40) or line 4 (row 80)
    CHECK(H(80, 50) > 9000 && H(80, 50) < int(0.31f * 65535.0f));
    CHECK_EQ(M(80, 50), 11);                           // no band-0 material
    CHECK_EQ(H(80, 39), 9000);
    CHECK_EQ(H(80, 81), 9000);
    CHECK_EQ(H(5, 60), 9000);                          // before the first column (x 12)
    // the ramp is monotone toward the beach in band 0
    CHECK(H(80, 45) <= H(80, 50) && H(80, 50) <= H(80, 55));
}

C5R_TEST(raster_window_equals_whole) {
    StripMesh m;
    CHECK_OK(buildStripMesh(straightLines(60, 5.0f, 3.0f, 64.0f), synStyles(), synMaterials(), m));
    Field whole(256, 160, 12000, 9);
    for (size_t i = 0; i < whole.h.size(); ++i) whole.h[i] = uint16_t(8000 + (i * 37) % 9000);
    Field win = whole;
    StripField f = whole.whole(false);
    CHECK_OK(applyStrips({m}, synMaterials(), f));
    // the window 70..140 x 50..90, as its own arrays
    const uint32_t X0 = 70, Y0 = 50, WW = 70, HH = 40;
    std::vector<uint16_t> wh(size_t(WW) * HH); std::vector<uint8_t> wm(size_t(WW) * HH);
    for (uint32_t y = 0; y < HH; ++y)
        for (uint32_t x = 0; x < WW; ++x) {
            wh[size_t(y) * WW + x] = win.h[size_t(Y0 + y) * 256 + X0 + x];
            wm[size_t(y) * WW + x] = win.m[size_t(Y0 + y) * 256 + X0 + x];
        }
    StripField g; g.samplesX = 256; g.samplesY = 160; g.x0 = X0; g.y0 = Y0; g.w = WW; g.h = HH;
    g.heights = wh.data(); g.materials = wm.data();
    CHECK_OK(applyStrips({m}, synMaterials(), g));
    int diff = 0, changed = 0;
    for (uint32_t y = 0; y < HH; ++y)
        for (uint32_t x = 0; x < WW; ++x) {
            const size_t a = size_t(Y0 + y) * 256 + X0 + x, b = size_t(y) * WW + x;
            diff += (whole.h[a] != wh[b] || whole.m[a] != wm[b]) ? 1 : 0;
            changed += (whole.h[a] != win.h[a]) ? 1 : 0;
        }
    CHECK_EQ(diff, 0);
    CHECK(changed > 1000);
}

C5R_TEST(raster_deterministic_and_wave_order) {
    // a long coast with several segments crossing itself: the result is the same twice, and the wave order (even
    // segments first) differs from plain triangle order somewhere where segments overlap
    // (the second pass uses a style of other heights, so the last writer of a w = 1 sample shows)
    StripStyles st = synStyles();
    st.present[2] = true;
    st.kind[2] = st.kind[1];
    for (int i = 0; i < 5; ++i) st.kind[2].height[i] = 0.5f;
    StripLines s = straightLines(120, 0.0f, 2.0f, 80.0f);
    for (int i = 0; i < 5; ++i)
        for (uint32_t j = 0; j < 120; ++j) {
            s.line[i][j].x = float(j % 60) * 4.0f + (j >= 60 ? 2.0f : 0.0f);
            if (j >= 60) { s.line[i][j].styleA = 2; s.line[i][j].styleB = 2; }
        }
    StripMesh m;
    CHECK_OK(buildStripMesh(s, st, synMaterials(), m));
    CHECK(m.segments() >= 2);
    Field a(300, 160, 10000, 9), b = a;
    StripField fa = a.whole(false), fb = b.whole(false);
    CHECK_OK(applyStrips({m}, synMaterials(), fa));
    CHECK_OK(applyStrips({m}, synMaterials(), fb));
    CHECK(a.h == b.h && a.m == b.m);
    // plain order: one segment holding every triangle
    StripMesh one = m;
    one.segmentEnd = {m.triangles()};
    one.segmentTris = m.triangles();
    Field c(300, 160, 10000, 9);
    StripField fc = c.whole(false);
    CHECK_OK(applyStrips({one}, synMaterials(), fc));
    CHECK(c.h != a.h);
}

C5R_TEST(raster_mountain_swap) {
    StripMesh m;
    CHECK_OK(buildStripMesh(straightLines(30, 0.0f, 4.0f, 60.0f), synStyles(), synMaterials(), m));
    // old material row 3 (continent 1, 0x35): band 0 samples (no vertex material) below 35000 become row 4
    Field fd(150, 130, 9000, 3);
    StripField f = fd.whole(false);
    CHECK_OK(applyStrips({m}, synMaterials(), f));
    CHECK_EQ(int(fd.m[size_t(50) * 150 + 60]), 4);
    CHECK_EQ(int(fd.m[size_t(59) * 150 + 60]), 4);     // the swap wins over band 1's material
    CHECK_EQ(int(fd.m[size_t(100) * 150 + 60]), 3);    // untouched
    // above 35000 no swap: a profile at 0.9
    StripStyles hi = synStyles();
    for (int i = 0; i < 5; ++i) hi.kind[1].height[i] = 0.9f;
    StripMesh mh;
    CHECK_OK(buildStripMesh(straightLines(30, 0.0f, 4.0f, 60.0f), hi, synMaterials(), mh));
    Field fh(150, 130, 60000, 3);
    StripField g = fh.whole(false);
    CHECK_OK(applyStrips({mh}, synMaterials(), g));
    CHECK_EQ(int(fh.m[size_t(50) * 150 + 60]), 3);
    CHECK_EQ(int(fh.m[size_t(59) * 150 + 60]), 2);     // band 1's material, no swap
}

C5R_TEST(raster_wrap_and_clip) {
    // a coast from x = -40 to x = 40 on a 200-wide field
    StripMesh m;
    CHECK_OK(buildStripMesh(straightLines(21, -40.0f, 4.0f, 60.0f), synStyles(), synMaterials(), m));
    Field w(200, 130, 9000, 11), n = w;
    StripField fw = w.whole(true), fn = n.whole(false);
    CHECK_OK(applyStrips({m}, synMaterials(), fw));
    CHECK_OK(applyStrips({m}, synMaterials(), fn));
    CHECK(w.h[size_t(60) * 200 + 180] != 9000);        // wrapped to the far edge
    CHECK_EQ(int(n.h[size_t(60) * 200 + 180]), 9000);  // clipped on a map that does not wrap
    CHECK_EQ(w.h[size_t(60) * 200 + 10], n.h[size_t(60) * 200 + 10]);
    // a window at the far edge sees the wrapped part
    StripField e = w.whole(true);
    e.x0 = 150; e.w = 50;
    CHECK(stripTouches(m, e));
    StripField far = n.whole(false);
    far.x0 = 150; far.w = 50;
    CHECK(!stripTouches(m, far));
}

C5R_TEST(raster_bad_inputs) {
    StripMesh m;
    CHECK_OK(buildStripMesh(straightLines(10, 0.0f, 4.0f, 60.0f), synStyles(), synMaterials(), m));
    Field fd(100, 100, 9000, 11);
    StripField f = fd.whole(false);
    f.w = 101;
    CHECK_ERR(applyStrips({m}, synMaterials(), f), Err::BadLayout);
    f = fd.whole(false); f.y0 = 1;
    CHECK_ERR(applyStrips({m}, synMaterials(), f), Err::BadLayout);
    f = fd.whole(false); f.heights = nullptr;
    CHECK_ERR(applyStrips({m}, synMaterials(), f), Err::BadLayout);
    f = fd.whole(false);
    StripMesh bad = m; bad.tris[5] = 100000;
    CHECK_ERR(applyStrips({bad}, synMaterials(), f), Err::BadLayout);
    for (uint16_t v : fd.h) CHECK_EQ(int(v), 9000);   // nothing written by a refused call
}

// MC #9538 item 1 (live-terrain.md 8.10.3): a vertex that is not finite (a NaN from a zero-length coast segment, or an
// infinity) is refused triangle by triangle, before any truncation to INT_MIN: the call returns at once, the triangles
// that do not use it are drawn as before, and the refusals are counted.
C5R_TEST(raster_non_finite_vertex) {
    StripMesh m;
    CHECK_OK(buildStripMesh(straightLines(20, 0.0f, 4.0f, 60.0f), synStyles(), synMaterials(), m));
    Field ref(120, 120, 9000, 11);
    StripField fr = ref.whole(true);
    StripStats sr;
    CHECK_OK(applyStrips({m}, synMaterials(), fr, &sr));
    CHECK_EQ(sr.trianglesRefused, 0u);
    const float bads[3] = {std::nanf(""), INFINITY, -INFINITY};
    for (float bad : bads) {
        StripMesh b = m;
        b.vertices[0].x = bad;                         // column 0's inner vertex of band 0 (two triangles use it)
        b.vertices[8 * 5 + 3].y = bad;                 // column 5, band 1's outer vertex
        Field fd(120, 120, 9000, 11);
        StripField f = fd.whole(true);
        StripStats st;
        const auto t0 = std::chrono::steady_clock::now();
        CHECK_OK(applyStrips({b}, synMaterials(), f, &st));
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(ms < 2000.0);                            // never a loop of 2^31 rows
        CHECK(st.trianglesRefused >= 3u);
        CHECK(st.trianglesDrawn > 0u);
        CHECK(st.trianglesDrawn + st.trianglesRefused <= sr.trianglesDrawn + 8u);
        // far from the refused vertices the field is the reference's
        int same = 0, total = 0;
        for (uint32_t y = 40; y < 80; ++y)
            for (uint32_t x = 60; x < 80; ++x) { ++total; same += fd.h[size_t(y) * 120 + x] == ref.h[size_t(y) * 120 + x] ? 1 : 0; }
        CHECK_EQ(same, total);
    }
    // a finite vertex far outside (a box larger than the field) is refused too
    StripMesh far = m;
    far.vertices[0].x = 3.0e8f;
    Field ff(120, 120, 9000, 11);
    StripField f2 = ff.whole(true);
    StripStats s2;
    CHECK_OK(applyStrips({far}, synMaterials(), f2, &s2));
    CHECK(s2.trianglesRefused >= 1u);
}

// ---------------------------------------------------------------- the special pick
C5R_TEST(special_pick_draws) {
    std::vector<StripMesh> ms(3);
    for (StripMesh& m : ms) CHECK_OK(buildStripMesh(straightLines(200, 0.0f, 1.0f, 50.0f), synStyles(), synMaterials(), m));
    CHECK(ms[0].segments() >= 2);
    uint32_t st = 1;                                   // srand(1): rand() gives 41, 18467, ...
    SpecialPick p;
    CHECK(specialPick(ms, st, p));
    CHECK_EQ(p.coast, 41u % 3u);
    CHECK_EQ(p.segment, (18467u % (ms[p.coast].segments() / 2)) * 2);
    uint32_t s2 = 1;
    std::vector<StripMesh> small(1);
    CHECK_OK(buildStripMesh(straightLines(5, 0.0f, 1.0f, 50.0f), synStyles(), synMaterials(), small[0]));
    CHECK_EQ(small[0].segments(), 1u);
    CHECK(!specialPick(small, s2, p));                 // the EXE would divide by zero here
    CHECK(s2 != 1u);                                   // one draw made
    uint32_t s3 = 1;
    CHECK(!specialPick(std::vector<StripMesh>(), s3, p));
    CHECK_EQ(s3, 1u);                                  // no draw
}

// ---------------------------------------------------------------- the install's TerrainDef.csv (C5R_GAME_DIR)
C5R_TEST(game_terrain_def) {
    const std::string dir = c5r::test::gameDir();
    gamefs::GameFiles fs;
    CHECK_OK(fs.open(std::wstring(dir.begin(), dir.end())));
    bake::LayerTables t;
    CHECK_OK(bake::loadTerrainDef(fs, t));
    StripMaterials m;
    CHECK_OK(StripMaterials::fromLayerTables(t, m));
    // 8.15: code 5 resolves to the generic Euro_Coast row (material 3); the American mountain top swaps to its base
    CHECK_EQ(m.resolveBand(5), uint8_t(3));
    uint8_t from[4], to[4];
    m.swapPairs(from, to);
    CHECK_EQ(from[0], uint8_t(10));
    CHECK_EQ(to[0], uint8_t(9));
    CHECK_EQ(from[1], kNoMaterial);
    CHECK_EQ(from[3], kNoMaterial);
}

C5R_TEST_MAIN
