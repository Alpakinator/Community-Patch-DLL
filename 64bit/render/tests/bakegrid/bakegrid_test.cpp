// render/tests/bakegrid - issue 3405: the per-sample normals, the CacheBake grid normals and the slope classes.
//
// Synthetic tests need nothing. The reference tests read C5R_GRID_REF (default D:/c5-64bit-work/3400/w5/ref; game
// data, never committed): one folder per live session with the session's height-field tap (tap.c5hf) and one folder per
// captured bake (grid48.bin + grid24.bin, made by tools/grid_ref.py from RenderDoc exports), and ref380/ (tiles P and
// C of 3403's capture, P_/C_grid48.bin + P_/C_grid24.bin, no tap: heights recovered from the blend vertices). A missing
// input fails the test with its path.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "c5r/bake/grid.h"
#include "c5r/bake/layers.h"
#include "c5r/bake/post.h"
#include "c5r/gamefs/gamefiles.h"
#include "testkit.h"

#include <windows.h>

using namespace c5r;
using namespace c5r::bake;

namespace {

// C++14 (the render build): a minimal path type and directory listing over Win32.
struct P {
    std::string s;
    P(const std::string& v = std::string()) : s(v) {}
    P(const char* v) : s(v) {}
    P operator/(const std::string& name) const { return P(s + "/" + name); }
    const std::string& string() const { return s; }
    std::string filename() const {
        const size_t k = s.find_last_of("/\\");
        return k == std::string::npos ? s : s.substr(k + 1);
    }
};
bool isDir(const P& p) {
    const DWORD a = GetFileAttributesA(p.s.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
bool exists(const P& p) { return GetFileAttributesA(p.s.c_str()) != INVALID_FILE_ATTRIBUTES; }
std::vector<P> subdirs(const P& p) {
    std::vector<P> out;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((p.s + "/*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        const std::string n = fd.cFileName;
        if (n != "." && n != ".." && (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(p / n);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end(), [](const P& a, const P& b) { return a.s < b.s; });
    return out;
}

heightfield::HeightGrid makeGrid(uint32_t mapW, uint32_t mapH, bool wrap) {
    heightfield::HeightGrid g;
    g.mapW = mapW;
    g.mapH = mapH;
    g.samplesX = mapW * 64;
    g.samplesY = mapH * 64;
    g.wrapX = wrap;
    g.h.assign(static_cast<size_t>(g.samplesX) * g.samplesY, 0);
    return g;
}

// A deterministic rough field (hills plus noise) so that every tap matters.
void roughen(heightfield::HeightGrid& g, uint32_t seed) {
    uint32_t s = seed;
    for (uint32_t y = 0; y < g.samplesY; ++y)
        for (uint32_t x = 0; x < g.samplesX; ++x) {
            s = s * 1664525u + 1013904223u;
            const double hill = 9000.0 + 6000.0 * std::sin(x * 0.07) * std::cos(y * 0.05);
            g.h[g.index(x, y)] = static_cast<uint16_t>(hill + (s >> 20));
        }
}

bool same(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * 4) == 0;
}

std::vector<uint8_t> readFile(const P& p) {
    std::ifstream f(p.s, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string refRoot() {
    const char* e = std::getenv("C5R_GRID_REF");
    return e && *e ? e : "D:/c5-64bit-work/3400/w5/ref";
}

// Reads a file of the reference; a missing or short file fails the test with its path.
bool need(const P& p, std::vector<uint8_t>& out, size_t minSize) {
    out = readFile(p);
    if (out.size() < minSize || out.empty()) {
        c5r::test::fail(__FILE__, __LINE__, "C5R_GRID_REF: " + p.string() + " missing or short (make it with "
                                            "render/tests/bakegrid/tools/grid_ref.py)");
        return false;
    }
    return true;
}

// The grid's width and height from the blend vertices' sample coordinates (a tile in the last column or row has 66
// vertices on that side, 67 otherwise). False when the count does not factor that way.
bool gridShape(const std::vector<uint8_t>& g48, uint32_t& gw, uint32_t& gh) {
    const size_t nv = g48.size() / 48;
    if (nv < 4) return false;
    float y0, y;
    std::memcpy(&y0, g48.data() + 16, 4);
    gw = 0;
    while (gw < nv) {
        std::memcpy(&y, g48.data() + static_cast<size_t>(gw) * 48 + 16, 4);
        if (y != y0) break;
        ++gw;
    }
    gh = gw ? static_cast<uint32_t>(nv / gw) : 0;
    return gw >= 2 && static_cast<size_t>(gw) * gh == nv;
}

struct GridCompare {
    size_t vertices = 0, equal = 0, edge = 0, edgeEqual = 0;
    size_t tiles = 0, lastTiles = 0, lastTilesEqual = 0, wrapTiles = 0, wrapTilesEqual = 0;  // whole tiles
};

// Compares the normals (bytes 12..19 of the 24-byte vertex) the server makes for the tile of grid48 with grid24.
// mask (optional): only vertices whose flag is set are compared.
void compareGrid(const heightfield::HeightGrid& g, const std::vector<uint8_t>& g48, const std::vector<uint8_t>& g24,
                 GridCompare& c, const std::vector<uint8_t>* mask, const std::string& name) {
    const size_t nv = g48.size() / 48;
    uint32_t gw = 0, gh = 0;
    CHECK(gridShape(g48, gw, gh));
    CHECK_EQ(g24.size(), nv * 24);
    if (static_cast<size_t>(gw) * gh != nv || g24.size() != nv * 24) return;
    BlendVertex v0;
    std::memcpy(&v0, g48.data(), 48);
    TileGeom t;
    t.originX = static_cast<int32_t>(v0.sample[0]) + 1;
    t.originY = static_cast<int32_t>(v0.sample[1]) + 1;
    t.extentX = gw - 2;
    t.extentY = gh - 2;
    std::vector<float> n;
    CHECK_OK(gridNormals(g, t, n));
    std::vector<uint16_t> half;
    CHECK_OK(packGridNormals(n, half));
    if (half.size() != nv * 4) return;
    size_t bad = 0;
    const size_t before = c.equal, beforeN = c.vertices;
    for (size_t k = 0; k < nv; ++k) {
        if (mask && !(*mask)[k]) continue;
        const uint32_t i = static_cast<uint32_t>(k % gw), j = static_cast<uint32_t>(k / gw);
        const bool edge = i == 0 || j == 0 || i == gw - 1 || j == gh - 1;
        const bool eq = std::memcmp(g24.data() + k * 24 + 12, &half[k * 4], 8) == 0;
        ++c.vertices;
        c.equal += eq;
        c.edge += edge;
        c.edgeEqual += edge && eq;
        if (!eq && bad++ < 3) {
            uint16_t e[4];
            std::memcpy(e, g24.data() + k * 24 + 12, 8);
            std::printf("  %s vertex (%u,%u): EXE %04x %04x %04x %04x, server %04x %04x %04x %04x\n", name.c_str(), i, j,
                        e[0], e[1], e[2], e[3], half[k * 4], half[k * 4 + 1], half[k * 4 + 2], half[k * 4 + 3]);
        }
    }
    // whole-tile counts: a tile in the last column or row (extent 64), a tile whose margin crosses the map's x edge
    const bool allEq = c.equal - before == c.vertices - beforeN;
    const bool last = gw == 66 || gh == 66;
    const bool wrap = t.originX == 0 || t.originX + static_cast<int32_t>(t.extentX) >= static_cast<int32_t>(g.samplesX);
    ++c.tiles;
    c.lastTiles += last;
    c.lastTilesEqual += last && allEq;
    c.wrapTiles += wrap;
    c.wrapTilesEqual += wrap && allEq;
}

}  // namespace

// ---------------------------------------------------------------- the rule, synthetic

C5R_TEST(flat_field_at_height_0_gives_up_normals_and_the_quirk_tilts_higher_flat_ground) {
    auto g = makeGrid(4, 4, true);
    std::vector<float> n;
    CHECK_OK(sampleNormals(g, -5, -5, 20, 20, n));  // all heights 0
    CHECK_EQ(n.size(), static_cast<size_t>(20 * 20 * 3));
    for (size_t i = 0; i < n.size(); i += 3) CHECK(n[i] == 0.0f && n[i + 1] == 0.0f && n[i + 2] > 0.9999f);
    SlopeClasses cls;
    CHECK_OK(slopeClassesFromNormals(n, cls));
    for (uint8_t c : cls) CHECK_EQ(c, static_cast<uint8_t>(0));
    std::vector<uint16_t> h;
    CHECK_OK(packGridNormals(n, h));
    CHECK_EQ(h[0], static_cast<uint16_t>(0));
    CHECK_EQ(h[1], static_cast<uint16_t>(0));
    CHECK_EQ(h[2], static_cast<uint16_t>(0x3C00));
    CHECK_EQ(h[3], static_cast<uint16_t>(0x3C00));
    // Flat ground above 0 is not level: the prediction steps by 1.0 per column (the EXE's quirk), so the weights of
    // the low and high side differ and the normal leans by about 0.009 at height 12345 (terrain-bake-passes.md 5.1).
    std::fill(g.h.begin(), g.h.end(), static_cast<uint16_t>(12345));
    CHECK_OK(sampleNormals(g, 7, 7, 1, 1, n));
    CHECK(n[0] > 0.009f && n[0] < 0.0093f && n[1] > 0.0104f && n[1] < 0.0107f && n[2] > 0.9998f);
    CHECK_EQ(slopeClass(n[0], n[1], n[2]), static_cast<uint8_t>(0));
}

C5R_TEST(slopes_in_x_and_y_give_classes_2_and_1_with_upward_normals) {
    auto gx = makeGrid(2, 2, false), gy = makeGrid(2, 2, false);
    for (uint32_t y = 0; y < gx.samplesY; ++y)
        for (uint32_t x = 0; x < gx.samplesX; ++x) {
            // a steep rise in +x / +y around sample (40, 40): 1500 units (5.9 height units) per sample
            gx.h[gx.index(x, y)] = static_cast<uint16_t>(20000 + 1500 * (std::min(std::max(static_cast<int>(x), 30), 50) - 40));
            gy.h[gy.index(x, y)] = static_cast<uint16_t>(20000 + 1500 * (std::min(std::max(static_cast<int>(y), 30), 50) - 40));
        }
    std::vector<float> nx, ny;
    CHECK_OK(sampleNormals(gx, 40, 40, 1, 1, nx));
    CHECK_OK(sampleNormals(gy, 40, 40, 1, 1, ny));
    CHECK(nx[0] < 0.0f && nx[2] > 0.0f && nx[2] <= 0.9f);  // the normal leans against the rise
    CHECK(ny[1] < 0.0f && ny[2] > 0.0f && ny[2] <= 0.9f);
    CHECK_EQ(slopeClass(nx[0], nx[1], nx[2]), static_cast<uint8_t>(2));
    CHECK_EQ(slopeClass(ny[0], ny[1], ny[2]), static_cast<uint8_t>(1));
    // a gentle rise stays top-down
    auto gg = makeGrid(2, 2, false);
    for (uint32_t y = 0; y < gg.samplesY; ++y)
        for (uint32_t x = 0; x < gg.samplesX; ++x) gg.h[gg.index(x, y)] = static_cast<uint16_t>(1000 + x * 4);
    std::vector<float> ng;
    CHECK_OK(sampleNormals(gg, 40, 40, 1, 1, ng));
    CHECK(ng[2] > 0.9f);
    CHECK_EQ(slopeClass(ng[0], ng[1], ng[2]), static_cast<uint8_t>(0));
}

C5R_TEST(a_normal_is_a_function_of_its_sample_only_any_block_split_gives_the_same) {
    auto g = makeGrid(3, 3, true);
    roughen(g, 7);
    std::vector<float> big;
    CHECK_OK(sampleNormals(g, -1, -1, 67, 67, big));
    // the EXE's job split: blocks of at most 12 x 12 vertices starting at -1
    std::vector<float> joined(big.size());
    for (int y0 = -1; y0 < 66; y0 += 12)
        for (int x0 = -1; x0 < 66; x0 += 12) {
            const uint32_t w = static_cast<uint32_t>(std::min(x0 + 12, 66) - x0), h = static_cast<uint32_t>(std::min(y0 + 12, 66) - y0);
            std::vector<float> b;
            CHECK_OK(sampleNormals(g, x0, y0, w, h, b));
            for (uint32_t r = 0; r < h; ++r)
                std::memcpy(&joined[((y0 + 1 + r) * 67 + (x0 + 1)) * 3], &b[r * w * 3], w * 12);
        }
    CHECK(same(big, joined));
    // single samples too
    std::vector<float> one;
    CHECK_OK(sampleNormals(g, 30, 17, 1, 1, one));
    CHECK(std::memcmp(one.data(), &big[(18 * 67 + 31) * 3], 12) == 0);
}

C5R_TEST(x_wraps_on_a_wrapping_map_and_clamps_otherwise_y_always_clamps) {
    auto g = makeGrid(2, 2, true);
    roughen(g, 11);
    const int32_t W = static_cast<int32_t>(g.samplesX), H = static_cast<int32_t>(g.samplesY);
    std::vector<float> a, b;
    // wrap: sample -1 is sample W-1, sample W is sample 0 (any multiple too)
    CHECK_OK(sampleNormals(g, -1, 50, 1, 1, a));
    CHECK_OK(sampleNormals(g, W - 1, 50, 1, 1, b));
    CHECK(same(a, b));
    CHECK_OK(sampleNormals(g, W, 50, 1, 1, a));
    CHECK_OK(sampleNormals(g, 0, 50, 1, 1, b));
    CHECK(same(a, b));
    CHECK_OK(sampleNormals(g, 3 * W + 5, 50, 1, 1, a));
    CHECK_OK(sampleNormals(g, 5, 50, 1, 1, b));
    CHECK(same(a, b));
    // the column W-1 influences sample 0 when wrapping ...
    auto g2 = g;
    for (int32_t y = 0; y < H; ++y) g2.h[g2.index(W - 1, y)] = 60000;
    CHECK_OK(sampleNormals(g, 0, 50, 1, 1, a));
    CHECK_OK(sampleNormals(g2, 0, 50, 1, 1, b));
    CHECK(!same(a, b));
    // ... but not without the wrap (clamped: x = -1, -2 read column 0)
    auto n1 = g, n2 = g2;
    n1.wrapX = n2.wrapX = false;
    CHECK_OK(sampleNormals(n1, 0, 50, 1, 1, a));
    CHECK_OK(sampleNormals(n2, 0, 50, 1, 1, b));
    CHECK(same(a, b));
    auto n3 = n1;
    for (int32_t y = 0; y < H; ++y) n3.h[n3.index(0, y)] = 60000;
    for (int32_t y = 0; y < H; ++y) n3.h[n3.index(1, y)] = n1.h[n1.index(1, y)];
    std::vector<float> c, d;
    CHECK_OK(sampleNormals(n3, -1, 50, 1, 1, c));  // all of x = -3 .. 1 read columns 0, 0, 0, 0, 1
    auto n4 = n3;
    CHECK_OK(sampleNormals(n4, 0, 50, 1, 1, d));
    CHECK(!same(c, d));
    // y: the last row does not influence row 0 or row -1 (no wrap), with or without the x wrap
    auto g3 = g;
    for (int32_t x = 0; x < W; ++x) g3.h[g3.index(x, H - 1)] = 60000;
    for (int32_t y0 : {-1, 0}) {
        CHECK_OK(sampleNormals(g, 20, y0, 1, 1, a));
        CHECK_OK(sampleNormals(g3, 20, y0, 1, 1, b));
        CHECK(same(a, b));
    }
    // far below or above the field every tap reads row 0 / row H-1, so the normals there do not depend on y
    CHECK_OK(sampleNormals(g, 20, -10, 1, 1, a));
    CHECK_OK(sampleNormals(g, 20, -5, 1, 1, b));
    CHECK(same(a, b) && std::isfinite(a[0]));
    CHECK_OK(sampleNormals(g, 20, H + 10, 1, 1, a));
    CHECK_OK(sampleNormals(g, 20, H + 3, 1, 1, b));
    CHECK(same(a, b));
}

C5R_TEST(equals_the_list_builders_normal_grid_within_the_normalisation_rounding) {
    // 3401's normalGrid (bakelayers) ports the same filter for the 69 x 69 window; it divides by the length where
    // the EXE (and this module) use D3DX's estimate plus one Newton step (terrain-bake-passes.md 5.1), so the two
    // agree to a few ulps.
    auto g = makeGrid(3, 2, true);
    roughen(g, 3);
    for (auto o : {std::pair<uint32_t, uint32_t>{0, 0}, {64, 64}, {128, 64}, {150, 70}}) {
        std::vector<float> theirs, mine;
        CHECK_OK(normalGrid(g, o.first, o.second, theirs));
        CHECK_OK(sampleNormals(g, static_cast<int32_t>(o.first), static_cast<int32_t>(o.second), 69, 69, mine));
        CHECK_EQ(theirs.size(), mine.size());
        size_t apart = 0, cls = 0;
        for (size_t i = 0; i < mine.size(); ++i) apart += std::fabs(theirs[i] - mine[i]) > 1e-6f;
        for (size_t i = 0; i < mine.size(); i += 3)
            cls += slopeClass(theirs[i], theirs[i + 1], theirs[i + 2]) != slopeClass(mine[i], mine[i + 1], mine[i + 2]);
        CHECK_EQ(apart, static_cast<size_t>(0));
        CHECK_EQ(cls, static_cast<size_t>(0));
    }
}

C5R_TEST(grid_normals_place_vertex_i_j_on_sample_origin_plus_i_minus_1) {
    auto g = makeGrid(3, 3, true);
    roughen(g, 5);
    for (uint32_t ext : {65u, 64u}) {
        TileGeom t;
        t.originX = 64;
        t.originY = 128;
        t.extentX = t.extentY = ext;
        std::vector<float> n, s;
        CHECK_OK(gridNormals(g, t, n));
        CHECK_EQ(n.size(), static_cast<size_t>((ext + 2) * (ext + 2) * 3));
        CHECK_OK(sampleNormals(g, 63, 127, ext + 2, ext + 2, s));
        CHECK(same(n, s));
        SlopeClasses cls;
        std::vector<float> n2;
        CHECK_OK(tileSlopeClasses(g, t, cls, &n2));
        CHECK(same(n, n2));
        CHECK_EQ(cls.size(), static_cast<size_t>((ext + 2) * (ext + 2)));
        CHECK_OK(tileSlopeClasses(g, t, cls));
    }
    // the last column of a wrapping map: its right margin is column 0
    TileGeom last;
    last.originX = 128;
    last.originY = 0;
    last.extentX = 64;
    last.extentY = 65;
    std::vector<float> n, s;
    CHECK_OK(gridNormals(g, last, n));
    CHECK_OK(sampleNormals(g, 0, -1, 1, 1, s));
    CHECK(std::memcmp(&n[(0 * 66 + 65) * 3], s.data(), 12) == 0);  // vertex (65, 0) = sample (192, -1) = (0, -1)
}

C5R_TEST(slope_classes_fit_fillBlendVertices) {
    // the classes have the grid's size; fillBlendVertices takes them (its own checks reject other sizes)
    auto g = makeGrid(2, 2, true);
    roughen(g, 9);
    g.mat.assign(g.h.size(), 0);
    LayerTables tables;
    CHECK_OK(parseTerrainDef("h\n0,a.dds,b.dds,NULL,0,0.1,0,0\n0,c.dds,d.dds,NULL,0,0.1,1,1\n", tables));
    for (uint32_t y = 0; y < g.samplesY; ++y)
        for (uint32_t x = 0; x < g.samplesX; ++x) g.mat[g.index(x, y)] = (x / 8) % 2 ? 1 : 0;
    BlendInputs in;
    in.grid = &g;
    in.tables = &tables;
    TileGeom t;
    t.originX = 0;
    t.originY = 0;
    SlopeClasses cls;
    CHECK_OK(tileSlopeClasses(g, t, cls));
    LayerList list;
    list.count = tables.layerCount;
    for (uint32_t i = 0; i < list.count; ++i) list.ids[i] = i;
    std::vector<BlendVertex> v;
    CHECK_OK(fillBlendVertices(in, t, list, cls, v));
    CHECK_EQ(v.size(), cls.size());
    size_t sloped = 0;
    for (uint8_t c : cls) sloped += c != 0;
    CHECK(sloped > 0);  // the rough field has steep places, so the slope layers are used
}

// ---------------------------------------------------------------- half floats

C5R_TEST(float_to_half_rounds_to_nearest_even_with_infinity_nan_and_subnormals) {
    CHECK_EQ(floatToHalf(0.0f), static_cast<uint16_t>(0x0000));
    CHECK_EQ(floatToHalf(-0.0f), static_cast<uint16_t>(0x8000));
    CHECK_EQ(floatToHalf(1.0f), static_cast<uint16_t>(0x3C00));
    CHECK_EQ(floatToHalf(-2.0f), static_cast<uint16_t>(0xC000));
    CHECK_EQ(floatToHalf(65504.0f), static_cast<uint16_t>(0x7BFF));
    CHECK_EQ(floatToHalf(65519.0f), static_cast<uint16_t>(0x7BFF));
    CHECK_EQ(floatToHalf(65520.0f), static_cast<uint16_t>(0x7C00));
    CHECK_EQ(floatToHalf(1e30f), static_cast<uint16_t>(0x7C00));
    CHECK_EQ(floatToHalf(-INFINITY), static_cast<uint16_t>(0xFC00));
    CHECK((floatToHalf(NAN) & 0x7C00) == 0x7C00 && (floatToHalf(NAN) & 0x3FF) != 0);
    // ties: 1 + 2^-11 lies halfway between 0x3C00 and 0x3C01 -> even (0x3C00); 1 + 3 * 2^-11 -> 0x3C02
    CHECK_EQ(floatToHalf(1.0f + 0.00048828125f), static_cast<uint16_t>(0x3C00));
    CHECK_EQ(floatToHalf(1.0f + 3 * 0.00048828125f), static_cast<uint16_t>(0x3C02));
    CHECK_EQ(floatToHalf(1.0f + 0.00048828125f + 1e-7f), static_cast<uint16_t>(0x3C01));
    // subnormals: 2^-24 is 0x0001; 2^-25 ties to 0; 1.5 * 2^-24 ties to 2 * 2^-24
    CHECK_EQ(floatToHalf(5.9604644775390625e-08f), static_cast<uint16_t>(0x0001));
    CHECK_EQ(floatToHalf(2.98023223876953125e-08f), static_cast<uint16_t>(0x0000));
    CHECK_EQ(floatToHalf(8.94069671630859375e-08f), static_cast<uint16_t>(0x0002));
    CHECK_EQ(floatToHalf(6.0975551605224609375e-05f), static_cast<uint16_t>(0x03FF));  // largest subnormal
    CHECK_EQ(floatToHalf(6.103515625e-05f), static_cast<uint16_t>(0x0400));            // smallest normal
    // every finite half survives the round trip
    size_t bad = 0;
    for (uint32_t h = 0; h < 0x10000; ++h) {
        if ((h & 0x7C00) == 0x7C00 && (h & 0x3FF)) continue;  // NaN payloads
        bad += floatToHalf(halfToFloat(static_cast<uint16_t>(h))) != h;
    }
    CHECK_EQ(bad, static_cast<size_t>(0));
    CHECK(std::isnan(halfToFloat(0x7E00)));
    CHECK_EQ(halfToFloat(0xFC00), -INFINITY);
}

C5R_TEST(pack_matches_bakepost_cache_grid_conversion) {
    // bakepost's buildCacheGrid converts the same floats with DirectXMath; both must give the same halves.
    auto g = makeGrid(2, 2, true);
    roughen(g, 13);
    TileGeom t;
    t.originX = 64;
    t.originY = 0;
    std::vector<float> n;
    CHECK_OK(gridNormals(g, t, n));
    std::vector<uint16_t> h;
    CHECK_OK(packGridNormals(n, h));
    std::vector<CacheVertex> cg(67 * 67);
    buildCacheGrid(n.data(), 256, cg.data());
    size_t bad = 0;
    for (size_t k = 0; k < cg.size(); ++k) bad += std::memcmp(cg[k].normal, &h[k * 4], 8) != 0;
    CHECK_EQ(bad, static_cast<size_t>(0));
}

// ---------------------------------------------------------------- negative tests

C5R_TEST(rejects_bad_inputs) {
    std::vector<float> n;
    heightfield::HeightGrid empty;
    CHECK_ERR(sampleNormals(empty, 0, 0, 1, 1, n), Err::BadLayout);
    auto g = makeGrid(1, 1, false);
    auto shortg = g;
    shortg.h.pop_back();
    CHECK_ERR(sampleNormals(shortg, 0, 0, 1, 1, n), Err::BadLayout);
    auto nan = g;
    nan.sz = NAN;
    CHECK_ERR(sampleNormals(nan, 0, 0, 1, 1, n), Err::BadLayout);
    nan = g;
    nan.sx = INFINITY;
    CHECK_ERR(sampleNormals(nan, 0, 0, 1, 1, n), Err::BadLayout);
    CHECK_ERR(sampleNormals(g, 0, 0, 0, 1, n), Err::BadLayout);
    CHECK_ERR(sampleNormals(g, 0, 0, 1, 0, n), Err::BadLayout);
    CHECK_ERR(sampleNormals(g, 0, 0, kMaxNormalWindow + 1, 1, n), Err::BadLayout);
    CHECK_ERR(sampleNormals(g, 0, 0, 1, kMaxNormalWindow + 1, n), Err::BadLayout);
    CHECK_OK(sampleNormals(g, 0, 0, 1, 1, n));
    TileGeom t;
    t.extentX = 0;
    CHECK_ERR(gridNormals(g, t, n), Err::BadLayout);
    t.extentX = 66;
    CHECK_ERR(gridNormals(g, t, n), Err::BadLayout);
    t.extentX = 65;
    t.extentY = 0;
    CHECK_ERR(gridNormals(g, t, n), Err::BadLayout);
    SlopeClasses cls;
    CHECK_ERR(tileSlopeClasses(g, t, cls), Err::BadLayout);
    CHECK_ERR(tileSlopeClasses(empty, TileGeom(), cls), Err::BadLayout);
    std::vector<uint16_t> h;
    CHECK_ERR(packGridNormals(std::vector<float>(4), h), Err::BadLayout);
    CHECK_ERR(slopeClassesFromNormals(std::vector<float>(5), cls), Err::BadLayout);
    CHECK_OK(packGridNormals(std::vector<float>(), h));
    CHECK(h.empty());
}

// ---------------------------------------------------------------- against the EXE (C5R_GRID_REF)

// Every captured bake of every session folder: the server's NORMAL halves equal the EXE's CacheBake vertices.
C5R_TEST(game_grid_normals_equal_the_exe_captures) {
    const P root = refRoot();
    if (!isDir(root)) {
        c5r::test::fail(__FILE__, __LINE__, "C5R_GRID_REF: " + root.string() + " missing");
        return;
    }
    size_t sessions = 0, bakes = 0;
    GridCompare all;
    for (const P& s : subdirs(root)) {
        if (!exists(s / "tap.c5hf")) continue;
        std::vector<uint8_t> tap;
        if (!need(s / "tap.c5hf", tap, 128)) return;
        heightfield::HeightGrid g;
        CHECK_OK(heightfield::readTap(tap.data(), tap.size(), g));
        ++sessions;
        GridCompare c;
        size_t n = 0;
        for (const P& d : subdirs(s)) {
            if (!exists(d / "grid48.bin")) continue;
            std::vector<uint8_t> g48, g24;
            if (!need(d / "grid48.bin", g48, 48) || !need(d / "grid24.bin", g24, 24)) return;
            compareGrid(g, g48, g24, c, nullptr, s.filename() + "/" + d.filename());
            ++n;
        }
        std::printf("  %s: %zu bakes, %zu / %zu vertices equal (grid-edge vertices %zu / %zu; last-column/row tiles %zu"
                    " / %zu, x-wrap tiles %zu / %zu equal)\n", s.filename().c_str(), n, c.equal, c.vertices,
                    c.edgeEqual, c.edge, c.lastTilesEqual, c.lastTiles, c.wrapTilesEqual, c.wrapTiles);
        bakes += n;
        all.vertices += c.vertices;
        all.equal += c.equal;
        all.edge += c.edge;
        all.edgeEqual += c.edgeEqual;
    }
    CHECK(sessions >= 1);
    CHECK(bakes >= 1);
    CHECK(all.vertices > 0);
    CHECK_EQ(all.equal, all.vertices);
}

// The blend's 48-byte vertices of every reference tile, made with the slope classes of this module (and the layer list
// of 3401's builder, the kernel and rule of 3402), against the EXE's: all bytes. Also counts what classes all 0 would
// change, which shows whether a tile exercises the slope path.
C5R_TEST(game_blend_vertices_with_these_slope_classes_equal_the_exe) {
    const std::string dir = c5r::test::gameDir();
    if (dir.empty()) c5r::test::skip("C5R_GAME_DIR not set");
    gamefs::GameFiles gfs;
    CHECK_OK(gfs.open(gamefs::fromUtf8(dir)));
    LayerTables tables;
    CHECK_OK(loadTerrainDef(gfs, gamefs::fromUtf8(dir), tables));
    const P root = refRoot();
    if (!isDir(root)) {
        c5r::test::fail(__FILE__, __LINE__, "C5R_GRID_REF: " + root.string() + " missing");
        return;
    }
    size_t tiles = 0, vertices = 0, equal = 0, slopeTiles = 0, zeroWouldDiffer = 0, sloped = 0;
    for (const P& s : subdirs(root)) {
        if (!exists(s / "tap.c5hf")) continue;
        std::vector<uint8_t> tap;
        if (!need(s / "tap.c5hf", tap, 128)) return;
        heightfield::HeightGrid g;
        CHECK_OK(heightfield::readTap(tap.data(), tap.size(), g));
        if (g.mat.empty()) {
            c5r::test::fail(__FILE__, __LINE__, (s / "tap.c5hf").string() + " has no byte layer");
            return;
        }
        BlendInputs in;
        in.grid = &g;
        in.tables = &tables;
        in.stepX = g.sx;
        in.stepY = g.sy;
        in.stepZ = g.sz;
        for (const P& d : subdirs(s)) {
            // a live catch from one read (layer_probe --grid-arrays, marker single_read): its 24-byte vertices
            // are complete, its 48-byte weights may have been half written, so it is not used here
            if (!exists(d / "grid48.bin") || exists(d / "single_read")) continue;
            std::vector<uint8_t> g48;
            if (!need(d / "grid48.bin", g48, 48)) return;
            const size_t nv = g48.size() / 48;
            uint32_t gw = 0, gh = 0;
            CHECK(gridShape(g48, gw, gh));
            if (static_cast<size_t>(gw) * gh != nv) continue;
            BlendVertex v0;
            std::memcpy(&v0, g48.data(), 48);
            TileGeom t;
            t.originX = static_cast<int32_t>(v0.sample[0]) + 1;
            t.originY = static_cast<int32_t>(v0.sample[1]) + 1;
            t.extentX = gw - 2;
            t.extentY = gh - 2;
            TileId id;
            id.x = static_cast<uint32_t>(t.originX) / 64;
            id.y = static_cast<uint32_t>(t.originY) / 64;
            LayerList list;
            CHECK_OK(buildLayerList(tables, g, id, list));
            SlopeClasses cls;
            CHECK_OK(tileSlopeClasses(g, t, cls));
            std::vector<BlendVertex> mine, flat;
            CHECK_OK(fillBlendVertices(in, t, list, cls, mine));
            CHECK_OK(fillBlendVertices(in, t, list, SlopeClasses(), flat));
            if (mine.size() != nv) continue;
            size_t eq = 0, zd = 0;
            for (size_t k = 0; k < nv; ++k) {
                eq += std::memcmp(&mine[k], g48.data() + k * 48, 48) == 0;
                zd += std::memcmp(&flat[k], g48.data() + k * 48, 48) != 0;
            }
            bool any = false;
            for (uint8_t c : cls) any = any || c != 0;
            if (eq != nv)
                std::printf("  %s/%s: %zu / %zu blend vertices equal\n", s.filename().c_str(),
                            d.filename().c_str(), eq, nv);
            ++tiles;
            vertices += nv;
            equal += eq;
            zeroWouldDiffer += zd;
            slopeTiles += zd > 0;
            sloped += any;
        }
    }
    std::printf("  %zu tiles, %zu / %zu blend vertices equal; %zu tiles have class 1/2 vertices; classes all 0 would "
                "change %zu vertices on %zu tiles\n", tiles, equal, vertices, sloped, zeroWouldDiffer, slopeTiles);
    CHECK(tiles >= 1);
    CHECK_EQ(equal, vertices);
}

// 3403's tiles P and C (frame380): no tap of that session, so the heights come from the blend vertices themselves
// (height = h16 / 255 * 0.2 inverts exactly) and only vertices whose 5 x 5 neighbourhood is known are compared.
C5R_TEST(game_ref380_tiles_p_and_c_equal_the_exe) {
    const P root = P(refRoot()) / "ref380";
    std::vector<uint8_t> g48[2], g24[2];
    const char* tags[2] = {"P", "C"};
    for (int t = 0; t < 2; ++t)
        if (!need(root / (std::string(tags[t]) + "_grid48.bin"), g48[t], 67 * 67 * 48) ||
            !need(root / (std::string(tags[t]) + "_grid24.bin"), g24[t], 67 * 67 * 24))
            return;
    auto g = makeGrid(56, 40, true);  // Tiny with its margin rows: 3584 x 2560 samples
    std::vector<uint8_t> known(g.h.size(), 0);
    for (int t = 0; t < 2; ++t) {
        g48[t].resize(67 * 67 * 48);
        g24[t].resize(67 * 67 * 24);
        for (size_t k = 0; k < 67 * 67; ++k) {
            BlendVertex v;
            std::memcpy(&v, g48[t].data() + k * 48, 48);
            const long hh = std::lround(v.world[2] / 0.2 * 255.0);
            int found = -1;
            for (long c = hh - 1; c <= hh + 1; ++c)
                if (c >= 0 && c <= 65535 && (static_cast<float>(c) * (1.0f / 255.0f)) * 0.2f == v.world[2]) found = static_cast<int>(c);
            CHECK(found >= 0);
            const uint32_t x = static_cast<uint32_t>(v.sample[0]), y = static_cast<uint32_t>(v.sample[1]);
            g.h[g.index(x, y)] = static_cast<uint16_t>(found);
            known[g.index(x, y)] = 1;
        }
    }
    GridCompare c;
    for (int t = 0; t < 2; ++t) {
        std::vector<uint8_t> mask(67 * 67, 0);
        for (size_t k = 0; k < 67 * 67; ++k) {
            BlendVertex v;
            std::memcpy(&v, g48[t].data() + k * 48, 48);
            const int x = static_cast<int>(v.sample[0]), y = static_cast<int>(v.sample[1]);
            bool all = true;
            for (int dy = -2; dy <= 2 && all; ++dy)
                for (int dx = -2; dx <= 2 && all; ++dx) all = known[g.index(static_cast<uint32_t>(x + dx), static_cast<uint32_t>(y + dy))] != 0;
            mask[k] = all;
        }
        compareGrid(g, g48[t], g24[t], c, &mask, std::string("ref380/") + tags[t]);
    }
    std::printf("  ref380 P and C: %zu / %zu vertices with known neighbourhoods equal\n", c.equal, c.vertices);
    CHECK(c.vertices >= 8000);
    CHECK_EQ(c.equal, c.vertices);
}

// The slope save (3402's C5H-3402-SLOPE): the tiles that hold slope-projected materials (bytes 4, 8, 17) get slope
// classes 1 and 2 on their steep vertices; the counts are printed for the research.
C5R_TEST(game_slope_save_tiles_have_slope_classes) {
    const P tapPath = P(refRoot()) / "slope" / "tap.c5hf";
    std::vector<uint8_t> tap;
    if (!need(tapPath, tap, 128)) return;
    heightfield::HeightGrid g;
    CHECK_OK(heightfield::readTap(tap.data(), tap.size(), g));
    if (g.mat.empty()) {
        c5r::test::fail(__FILE__, __LINE__, tapPath.string() + " has no byte layer");
        return;
    }
    const uint32_t tilesX = g.samplesX / 64, tilesY = g.samplesY / 64;
    size_t tiles = 0, onSlope = 0, moved[3] = {0, 0, 0};
    for (uint32_t ty = 0; ty < tilesY; ++ty)
        for (uint32_t tx = 0; tx < tilesX; ++tx) {
            TileGeom t;
            t.originX = static_cast<int32_t>(tx * 64);
            t.originY = static_cast<int32_t>(ty * 64);
            t.extentX = tx + 1 == tilesX ? 64 : 65;
            t.extentY = ty + 1 == tilesY ? 64 : 65;
            bool has = false;
            for (uint32_t j = 0; j < t.extentY + 2 && !has; ++j)
                for (uint32_t i = 0; i < t.extentX + 2 && !has; ++i) {
                    const uint32_t x = (g.samplesX + t.originX + i - 1) % g.samplesX, y = (g.samplesY + t.originY + j - 1) % g.samplesY;
                    const uint8_t b = g.mat[g.index(x, y)];
                    has = b == 4 || b == 8 || b == 17;
                }
            if (!has) continue;
            ++tiles;
            SlopeClasses cls;
            CHECK_OK(tileSlopeClasses(g, t, cls));
            for (uint32_t j = 0; j < t.extentY + 2; ++j)
                for (uint32_t i = 0; i < t.extentX + 2; ++i) {
                    const uint32_t x = (g.samplesX + t.originX + i - 1) % g.samplesX, y = (g.samplesY + t.originY + j - 1) % g.samplesY;
                    const uint8_t b = g.mat[g.index(x, y)];
                    if (b != 4 && b != 8 && b != 17) continue;
                    ++onSlope;
                    ++moved[cls[j * (t.extentX + 2) + i]];
                }
        }
    std::printf("  slope save: %zu tiles with slope-projected samples, %zu such vertices: class 0 %zu, 1 %zu, 2 %zu\n",
                tiles, onSlope, moved[0], moved[1], moved[2]);
    CHECK(tiles >= 1);
    CHECK(moved[1] + moved[2] > 0);
}

C5R_TEST_MAIN
