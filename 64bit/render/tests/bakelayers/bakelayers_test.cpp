// render/tests/bakelayers/bakelayers_test.cpp - issue 3401. The texture-set tables, the normal grid and the per-tile
// layer lists on synthetic CSVs and height fields (every check with a negative test asserting the exact Err), the
// checker tool run as a product (exit codes, outputs), and the install's TerrainDef.csv through c5r::gamefs.
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "c5r/bake/layers.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/heightfield/heightfield.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::bake;
using heightfield::HeightGrid;

namespace {

const char* kHeader = "Continent ID,Diffuse,Heightmap,Specmap,BaseDominance,Texture Scale,Projection,ID\n";

// Sets: 0 Flat (1 layer: 0), 1 Slope (projection 1: 1,2,3), 2 Next (4), 3 Spec (5), then Checkerboard (6).
std::string smallCsv() {
    return std::string(kHeader) +
           "0,Flat_D.dds,Flat_H.dds,NULL,0,0.014,0,0\r\n"
           "0,Slope_D.dds,Slope_H.dds,Slope_S.dds,1,0.05,1,54\n"
           "\n"
           "1, Next_D.dds ,Next_H.dds,NULL,0,0.1,0,2\n"
           "4,Spec_D.dds,Spec_H.dds,Spec_S.dds,0,0.2,0,1F\n";
}

LayerTables smallTables() {
    LayerTables t;
    Status s = parseTerrainDef(smallCsv(), t);
    if (!s) c5r::test::fail(__FILE__, __LINE__, "smallTables: " + s.str());
    return t;
}

// A w x h plot field, flat at height base, material m everywhere.
HeightGrid field(uint32_t w, uint32_t h, uint16_t base, uint8_t m) {
    HeightGrid g;
    g.mapW = w;
    g.mapH = h;
    g.samplesX = w * 64;
    g.samplesY = h * 64;
    g.wrapX = true;
    g.h.assign(static_cast<size_t>(g.samplesX) * g.samplesY, base);
    g.mat.assign(g.h.size(), m);
    return g;
}

LayerList list(const LayerTables& t, const HeightGrid& g, uint32_t x, uint32_t y, LayerListDetail* d = nullptr) {
    TileId id;
    id.x = x;
    id.y = y;
    LayerList l;
    Status s = buildLayerList(t, g, id, l, d);
    if (!s) c5r::test::fail(__FILE__, __LINE__, "buildLayerList: " + s.str());
    return l;
}

std::vector<uint32_t> ids(const LayerList& l) { return std::vector<uint32_t>(l.ids, l.ids + l.count); }

// 800 height units per sample from 60 on (tile (1,1)'s normal window is 62..137): n.z about 0.81 (research 2.2a).
uint16_t ramp(uint32_t v) { return static_cast<uint16_t>(v < 60 ? 0 : std::min(65535u, (v - 60) * 800)); }

const float* normalAt(const std::vector<float>& n, uint32_t i, uint32_t k) { return &n[(k * kNormalGrid + i) * 3]; }

// ---- running the checker tool ----
std::string toolPath() {
#ifdef C5R_BAKELAYERS_CHECK
    return C5R_BAKELAYERS_CHECK;
#else
    return std::string();
#endif
}

std::string scratch(const std::string& name) {
    std::string d = c5r::test::workDir() + "/bakelayers";
    CreateDirectoryA(c5r::test::workDir().c_str(), nullptr);
    CreateDirectoryA(d.c_str(), nullptr);
    return d + "/" + name;
}

void writeText(const std::string& path, const std::string& text) {
    std::ofstream o(path, std::ios::binary);
    o << text;
}

void writeBytes(const std::string& path, const std::vector<uint8_t>& b) {
    std::ofstream o(path, std::ios::binary);
    o.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

std::string readText(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

// Runs the tool with args, stdout+stderr to a file; returns the exit code (-1 if it could not run).
int runTool(const std::string& args, std::string& output) {
    std::string out = scratch("tool-out.txt");
    SECURITY_ATTRIBUTES sa = {sizeof sa, nullptr, TRUE};
    HANDLE f = CreateFileA(out.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return -1;
    std::string cmd = "\"" + toolPath() + "\" " + args;
    STARTUPINFOA si = {};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = f;
    si.hStdError = f;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi = {};
    std::vector<char> buf(cmd.begin(), cmd.end());
    buf.push_back('\0');
    BOOL ok = CreateProcessA(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(f);
    if (!ok) return -1;
    WaitForSingleObject(pi.hProcess, 120000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    output = readText(out);
    return static_cast<int>(code);
}

// The lists file layer_probe.py --lists-txt writes, from given lists.
std::string listsFile(const HeightGrid& g, const LayerTables& t, const std::vector<LayerList>& l) {
    std::string s = "# test\ngrid " + std::to_string(g.mapW) + " " + std::to_string(g.mapH) + "\nsamples " +
                    std::to_string(g.samplesX) + " " + std::to_string(g.samplesY) + "\nlayers " +
                    std::to_string(t.layerCount) + "\nc10";
    for (size_t k = 0; k < t.sets.size(); ++k) s += " " + std::to_string(t.firstLayerOfMaterial[k]);
    s += "\nb10";
    for (size_t k = 0; k < t.sets.size(); ++k) s += " " + std::to_string(t.slopeProjected[k]);
    s += "\nf10";
    for (uint32_t v : t.setOfLayer) s += " " + std::to_string(v);
    s += "\nk1010";
    for (uint32_t v : t.projectionOfLayer) s += " " + std::to_string(v);
    s += "\n";
    for (uint32_t x = 0; x < g.mapW; ++x)
        for (uint32_t y = 0; y < g.mapH; ++y) {
            const LayerList& e = l[static_cast<size_t>(x) * g.mapH + y];
            s += "tile " + std::to_string(x * g.mapH + y) + " " + std::to_string(x) + " " + std::to_string(y) + " " +
                 std::to_string(e.count);
            for (uint32_t i = 0; i < e.count; ++i) s += " " + std::to_string(e.ids[i]);
            s += "\n";
        }
    return s;
}

}  // namespace

// ---- parseTerrainDef ----

C5R_TEST(csv_parses_rows_and_appends_checkerboard) {
    LayerTables t;
    CHECK_OK(parseTerrainDef(smallCsv(), t));
    CHECK_EQ(t.sets.size(), static_cast<size_t>(5));
    CHECK_EQ(t.layerCount, 7u);
    CHECK_EQ(t.sets[0].diffuse, std::string("Flat_D.dds"));
    CHECK_EQ(t.sets[0].specmap, std::string(""));
    CHECK_EQ(t.sets[1].specmap, std::string("Slope_S.dds"));
    CHECK_EQ(t.sets[1].projection, 1);
    CHECK_EQ(t.sets[1].id, 0x54);  // the id column is hexadecimal
    CHECK_EQ(t.sets[3].id, 0x1F);
    CHECK_EQ(t.sets[2].diffuse, std::string("Next_D.dds"));  // trimmed
    CHECK(std::fabs(t.sets[1].textureScale - 0.05f) < 1e-7f);
    CHECK(std::fabs(t.sets[1].baseDominance - 1.0f) < 1e-7f);
    CHECK_EQ(t.sets[4].diffuse, std::string(kAppendedSetTexture));
    CHECK_EQ(t.sets[4].heightmap, std::string(kAppendedSetTexture));
    CHECK_EQ(t.sets[4].textureScale, kAppendedSetScale);
    const uint8_t first[5] = {0, 1, 4, 5, 6};
    for (int k = 0; k < 5; ++k) CHECK_EQ(t.firstLayerOfMaterial[k], first[k]);
    CHECK_EQ(t.slopeProjected[1], static_cast<uint8_t>(1));
    CHECK_EQ(t.slopeProjected[0] + t.slopeProjected[2] + t.slopeProjected[3] + t.slopeProjected[4], 0);
    const uint32_t setOf[7] = {0, 1, 1, 1, 2, 3, 4}, projOf[7] = {0, 0, 1, 2, 0, 0, 0};
    CHECK_EQ(t.setOfLayer.size(), static_cast<size_t>(7));
    for (int i = 0; i < 7; ++i) {
        CHECK_EQ(t.setOfLayer[i], setOf[i]);
        CHECK_EQ(t.projectionOfLayer[i], projOf[i]);
    }
}

C5R_TEST(csv_no_data_row_fails) {
    LayerTables t;
    CHECK_ERR(parseTerrainDef(kHeader, t), Err::BadLayout);
    CHECK_ERR(parseTerrainDef("", t), Err::BadLayout);
    CHECK_EQ(t.layerCount, 0u);  // out untouched on failure
}

C5R_TEST(csv_missing_columns_fail) {
    LayerTables t;
    CHECK_ERR(parseTerrainDef(std::string(kHeader) + "0,A_D.dds,A_H.dds,NULL,0,0.014,0\n", t), Err::BadLayout);
}

C5R_TEST(csv_bad_numbers_fail) {
    LayerTables t;
    CHECK_ERR(parseTerrainDef(std::string(kHeader) + "x,A_D.dds,A_H.dds,NULL,0,0.014,0,0\n", t), Err::BadLayout);
    CHECK_ERR(parseTerrainDef(std::string(kHeader) + "0,A_D.dds,A_H.dds,NULL,zero,0.014,0,0\n", t), Err::BadLayout);
    CHECK_ERR(parseTerrainDef(std::string(kHeader) + "0,A_D.dds,A_H.dds,NULL,0,0.01x,0,0\n", t), Err::BadLayout);
    CHECK_ERR(parseTerrainDef(std::string(kHeader) + "0,A_D.dds,A_H.dds,NULL,0,0.014,,0\n", t), Err::BadLayout);
    CHECK_ERR(parseTerrainDef(std::string(kHeader) + "0,A_D.dds,A_H.dds,NULL,0,0.014,0,G1\n", t), Err::BadLayout);
    CHECK_ERR(parseTerrainDef(std::string(kHeader) + "0,A_D.dds,A_H.dds,NULL,0,inf,0,0\n", t), Err::BadLayout);
}

C5R_TEST(csv_projection_other_than_0_1_unsupported) {
    LayerTables t;
    CHECK_ERR(parseTerrainDef(std::string(kHeader) + "0,A_D.dds,A_H.dds,NULL,0,0.014,2,0\n", t), Err::Unsupported);
}

C5R_TEST(csv_empty_texture_name_fails) {
    LayerTables t;
    CHECK_ERR(parseTerrainDef(std::string(kHeader) + "0,,A_H.dds,NULL,0,0.014,0,0\n", t), Err::BadLayout);
    CHECK_ERR(parseTerrainDef(std::string(kHeader) + "0,A_D.dds,,NULL,0,0.014,0,0\n", t), Err::BadLayout);
}

C5R_TEST(csv_too_many_rows_fail) {
    std::string ok = kHeader, bad;
    for (uint32_t i = 0; i < kMaxTextureSets - 1; ++i) ok += "0,A_D.dds,A_H.dds,NULL,0,0.014,0,0\n";
    LayerTables t;
    CHECK_OK(parseTerrainDef(ok, t));  // 63 rows + Checkerboard = 64 sets, 64 layers
    CHECK_EQ(t.layerCount, kMaxLayerIds);
    bad = ok + "0,A_D.dds,A_H.dds,NULL,0,0.014,0,0\n";
    CHECK_ERR(parseTerrainDef(bad, t), Err::BadLayout);
    // too many layer ids with few rows: 21 slope rows = 63 ids, + Checkerboard = 64; one more slope row = 67
    std::string slope = kHeader;
    for (int i = 0; i < 21; ++i) slope += "0,S_D.dds,S_H.dds,NULL,0,0.05,1,0\n";
    CHECK_OK(parseTerrainDef(slope, t));
    CHECK_EQ(t.layerCount, 64u);
    CHECK_ERR(parseTerrainDef(slope + "0,S_D.dds,S_H.dds,NULL,0,0.05,1,0\n", t), Err::BadLayout);
}

// ---- checkGrid / tileWindow ----

C5R_TEST(grid_checks) {
    HeightGrid g = field(2, 2, 1000, 0);
    CHECK_OK(checkGrid(g));
    HeightGrid e = g;
    e.mat.clear();
    CHECK_ERR(checkGrid(e), Err::NotFound);  // empty mat
    e = g;
    e.mat.resize(e.mat.size() - 1);
    CHECK_ERR(checkGrid(e), Err::BadLayout);  // mat size mismatch
    e = g;
    e.h.resize(e.h.size() + 64);
    CHECK_ERR(checkGrid(e), Err::BadLayout);  // grid size mismatch
    e = g;
    e.samplesX = 100;
    CHECK_ERR(checkGrid(e), Err::BadLayout);  // samples not plots * 64
    e = g;
    e.mapW = 0;
    CHECK_ERR(checkGrid(e), Err::BadLayout);
    e = g;
    e.sz = NAN;
    CHECK_ERR(checkGrid(e), Err::BadLayout);
}

C5R_TEST(tile_window_extents) {
    HeightGrid g = field(3, 2, 0, 0);
    TileWindow w;
    TileId t;
    t.x = 1;
    t.y = 0;
    CHECK_OK(tileWindow(g, t, w));
    CHECK_EQ(w.ox, 64u);
    CHECK_EQ(w.oy, 0u);
    CHECK_EQ(w.ex, 65u);
    CHECK_EQ(w.ey, 65u);
    t.x = 2;
    t.y = 1;
    CHECK_OK(tileWindow(g, t, w));
    CHECK_EQ(w.ex, 64u);  // last column / row
    CHECK_EQ(w.ey, 64u);
    t.x = 3;
    CHECK_ERR(tileWindow(g, t, w), Err::BadLayout);
    t.x = 0;
    t.y = 2;
    CHECK_ERR(tileWindow(g, t, w), Err::BadLayout);
}

// ---- the normal grid ----

C5R_TEST(normals_flat_field_at_height_0_points_up) {
    HeightGrid g = field(2, 2, 0, 0);
    std::vector<float> n;
    CHECK_OK(normalGrid(g, 64, 0, n));
    CHECK_EQ(n.size(), static_cast<size_t>(kNormalGrid * kNormalGrid * 3));
    for (size_t i = 0; i < n.size(); i += 3) {
        CHECK_EQ(n[i], 0.0f);
        CHECK_EQ(n[i + 1], 0.0f);
        CHECK_EQ(n[i + 2], 1.0f);
    }
}

C5R_TEST(normals_flat_field_above_0_leans_to_plus_x_plus_y) {
    // The prediction steps by 1.0 instead of the slope (2.2a): on flat ground the taps after the first column / row
    // get smaller weights, so the normal leans to +x and +y, more the higher the ground.
    HeightGrid lo = field(2, 2, 10000, 0), hi = field(2, 2, 60000, 0);
    std::vector<float> a, b;
    CHECK_OK(normalGrid(lo, 0, 0, a));
    CHECK_OK(normalGrid(hi, 0, 0, b));
    const float* p = normalAt(a, 20, 20);
    const float* q = normalAt(b, 20, 20);
    CHECK(p[0] > 0.0f && p[1] > 0.0f && p[2] < 1.0f && p[2] > 0.99f);
    CHECK(p[0] < p[1]);  // the same lean in x and y, n.x scaled by sy = 0.866 (2.2a step 5)
    CHECK(q[0] > p[0] && q[2] < p[2]);
}

C5R_TEST(normals_follow_the_slope_and_entry_i_is_sample_ox_plus_i) {
    // Flat for x < 100, a steep ramp up in x from x = 100 (tile (1,0) has ox = 64: entry i = sample 64 + i).
    HeightGrid g = field(2, 2, 0, 0);
    for (uint32_t y = 0; y < g.samplesY; ++y)
        for (uint32_t x = 100; x < g.samplesX; ++x) g.h[g.index(x, y)] = static_cast<uint16_t>(std::min(65535u, (x - 99) * 3000));
    std::vector<float> n;
    CHECK_OK(normalGrid(g, 64, 0, n));
    const float* flat = normalAt(n, 30, 30);   // sample 94: all taps (92..96) flat
    CHECK_EQ(flat[2], 1.0f);
    const float* n97 = normalAt(n, 33, 30);    // sample 97: taps 95..99 and gradient 96..98 flat
    CHECK_EQ(n97[2], 1.0f);
    const float* steep = normalAt(n, 40, 30);  // sample 104: on the ramp
    CHECK(steep[2] < kSlopeThreshold);
    CHECK(steep[0] < 0.0f);                    // uphill to +x: the upward normal leans to -x
    CHECK(std::fabs(steep[1]) < std::fabs(steep[0]));
    CHECK(std::fabs(steep[0] * steep[0] + steep[1] * steep[1] + steep[2] * steep[2] - 1.0f) < 1e-5f);
    const float* edge = normalAt(n, 35, 30);   // sample 99: gradient (h100 - h98) >> 1 is not zero
    CHECK(edge[2] < 1.0f);
}

C5R_TEST(normals_slope_in_y_tilts_y) {
    HeightGrid g = field(2, 2, 0, 0);
    for (uint32_t y = 0; y < g.samplesY; ++y)
        for (uint32_t x = 0; x < g.samplesX; ++x) g.h[g.index(x, y)] = ramp(y);
    std::vector<float> n;
    CHECK_OK(normalGrid(g, 0, 64, n));
    const float* v = normalAt(n, 10, 10);
    CHECK(std::fabs(v[0]) < std::fabs(v[1]));
    CHECK(v[1] < 0.0f);
    CHECK(v[2] < kSlopeThreshold);
}

C5R_TEST(normals_negative_paths) {
    HeightGrid g = field(1, 1, 0, 0);
    std::vector<float> n;
    CHECK_ERR(normalGrid(g, 64, 0, n), Err::BadLayout);  // origin outside
    g.h.clear();
    CHECK_ERR(normalGrid(g, 0, 0, n), Err::BadLayout);   // grid size mismatch
}

// ---- the layer lists ----

C5R_TEST(list_single_material) {
    LayerTables t = smallTables();
    HeightGrid g = field(3, 3, 500, 2);
    CHECK(ids(list(t, g, 1, 1)) == std::vector<uint32_t>({4}));
    HeightGrid c = field(3, 3, 500, 4);  // the appended set's material byte
    CHECK(ids(list(t, c, 0, 0)) == std::vector<uint32_t>({6}));
}

C5R_TEST(list_window_is_two_before_and_extent_after) {
    LayerTables t = smallTables();
    // tile (1,1): x and y from 62 to 64 + 65 = 129 inclusive
    struct Case { uint32_t x, y; bool in; };
    const Case cases[] = {{62, 100, true}, {61, 100, false}, {129, 100, true}, {130, 100, false},
                          {100, 62, true}, {100, 61, false}, {100, 129, true}, {100, 130, false}};
    for (const Case& c : cases) {
        HeightGrid g = field(3, 3, 0, 0);
        g.mat[g.index(c.x, c.y)] = 3;
        std::vector<uint32_t> got = ids(list(t, g, 1, 1));
        CHECK(got == (c.in ? std::vector<uint32_t>({0, 5}) : std::vector<uint32_t>({0})));
    }
    // last column (extent 64): x to 128 + 64 = 192, which wraps to 0
    HeightGrid g = field(3, 3, 0, 0);
    g.mat[g.index(0, 100)] = 3;
    CHECK(ids(list(t, g, 2, 1)) == std::vector<uint32_t>({0, 5}));
}

C5R_TEST(list_wraps_x_and_y) {
    LayerTables t = smallTables();
    HeightGrid g = field(3, 3, 0, 0);
    g.mat[g.index(g.samplesX - 1, 10)] = 2;  // left margin of column 0
    CHECK(ids(list(t, g, 0, 0)) == std::vector<uint32_t>({0, 4}));
    HeightGrid h = field(3, 3, 0, 0);
    h.mat[h.index(10, h.samplesY - 2)] = 2;  // top margin of row 0 reads the last rows: y wraps (2.2 step 1)
    CHECK(ids(list(t, h, 0, 0)) == std::vector<uint32_t>({0, 4}));
}

C5R_TEST(list_slope_material_on_flat_ground_takes_its_three_ids) {
    LayerTables t = smallTables();
    HeightGrid g = field(3, 3, 9000, 1);
    LayerListDetail d;
    CHECK(ids(list(t, g, 1, 1, &d)) == std::vector<uint32_t>({1, 2, 3}));
    CHECK_EQ(d.slopeSamples, 68u * 68u);
    CHECK_EQ(d.moved1 + d.moved2, 0u);
    CHECK_EQ(d.counts[1], 2u * 68u * 68u);  // counted twice
    CHECK_EQ(d.counts[2], 68u * 68u);
    CHECK_EQ(d.counts[3], 68u * 68u);
}

C5R_TEST(list_steep_x_moves_by_two_into_the_next_sets) {
    LayerTables t = smallTables();
    HeightGrid g = field(3, 3, 0, 1);
    for (uint32_t y = 0; y < g.samplesY; ++y)
        for (uint32_t x = 0; x < g.samplesX; ++x) g.h[g.index(x, y)] = ramp(x);
    LayerListDetail d;
    LayerList l = list(t, g, 1, 1, &d);
    // every sample moved by +2 (|ny| = 0 <= |nx|): ids 3, 4, 5 = the set's last projection and the next two sets
    CHECK(ids(l) == std::vector<uint32_t>({3, 4, 5}));
    CHECK_EQ(d.moved2, d.slopeSamples);
}

C5R_TEST(list_steep_y_moves_by_one) {
    LayerTables t = smallTables();
    HeightGrid g = field(3, 3, 0, 1);
    for (uint32_t y = 0; y < g.samplesY; ++y)
        for (uint32_t x = 0; x < g.samplesX; ++x) g.h[g.index(x, y)] = ramp(y);
    LayerListDetail d;
    CHECK(ids(list(t, g, 1, 1, &d)) == std::vector<uint32_t>({2, 3, 4}));
    CHECK_EQ(d.moved1, d.slopeSamples);
}

C5R_TEST(list_uses_the_normal_two_samples_further) {
    // One slope sample at (97, 100) in tile (1,1). The ramp starts at x = 100: the normal of sample 97 is flat, that of
    // sample 99 is not (normals_follow_the_slope...). The EXE tests sample x with the normal of x + 2, so it moves.
    LayerTables t = smallTables();
    HeightGrid g = field(3, 3, 0, 0);
    for (uint32_t y = 0; y < g.samplesY; ++y)
        for (uint32_t x = 100; x < g.samplesX; ++x) g.h[g.index(x, y)] = static_cast<uint16_t>(std::min(65535u, (x - 99) * 3000));
    g.mat[g.index(97, 100)] = 1;
    std::vector<float> n;
    CHECK_OK(normalGrid(g, 64, 64, n));
    CHECK_EQ(normalAt(n, 33, 36)[2], 1.0f);             // sample (97, 100)
    const float* n99 = normalAt(n, 35, 38);              // sample (99, 102)
    LayerListDetail d;
    LayerList l = list(t, g, 1, 1, &d);
    CHECK_EQ(d.slopeSamples, 1u);
    if (n99[2] <= kSlopeThreshold) {
        CHECK_EQ(d.moved2 + d.moved1, 1u);
        CHECK(ids(l) != std::vector<uint32_t>({0, 1, 2, 3}));
    } else {
        c5r::test::fail(__FILE__, __LINE__, "the ramp is not steep enough at sample 99 for this test");
    }
}

C5R_TEST(list_more_than_8_sorted_by_count_and_cut) {
    // 10 materials with layers 0..9 (all flat sets): counts by area; equal counts keep id order.
    std::string csv = kHeader;
    for (int i = 0; i < 10; ++i) csv += "0,M_D.dds,M_H.dds,NULL,0,0.014,0,0\n";
    LayerTables t;
    CHECK_OK(parseTerrainDef(csv, t));
    HeightGrid g = field(3, 3, 0, 0);
    // tile (1,1) window x, y = 62..129 (68 x 68). Rows 62..: material m covers rows by band height.
    const uint32_t band[10] = {2, 2, 30, 3, 5, 5, 8, 6, 4, 3};  // rows per material: sum 68
    uint32_t y = 62;
    for (uint8_t m = 0; m < 10; ++m)
        for (uint32_t r = 0; r < band[m]; ++r, ++y)
            for (uint32_t x = 0; x < g.samplesX; ++x) g.mat[g.index(x, y)] = m;
    LayerListDetail d;
    LayerList l = list(t, g, 1, 1, &d);
    CHECK(d.sorted);
    // counts: 2:30, 6:8, 7:6, 4:5, 5:5, 8:4, 3:3, 9:3, 0:2, 1:2 -> first 8
    CHECK(ids(l) == std::vector<uint32_t>({2, 6, 7, 4, 5, 8, 3, 9}));
}

C5R_TEST(list_exactly_8_stays_ascending) {
    std::string csv = kHeader;
    for (int i = 0; i < 8; ++i) csv += "0,M_D.dds,M_H.dds,NULL,0,0.014,0,0\n";
    LayerTables t;
    CHECK_OK(parseTerrainDef(csv, t));
    HeightGrid g = field(3, 3, 0, 7);
    for (uint8_t m = 0; m < 7; ++m) g.mat[g.index(70 + m, 70)] = m;
    LayerListDetail d;
    CHECK(ids(list(t, g, 1, 1, &d)) == std::vector<uint32_t>({0, 1, 2, 3, 4, 5, 6, 7}));
    CHECK(!d.sorted);
}

C5R_TEST(list_negative_paths) {
    LayerTables t = smallTables();
    HeightGrid g = field(2, 2, 0, 0);
    TileId id;
    LayerList l;
    g.mat[g.index(10, 10)] = 5;  // 5 sets (0..4): byte 5 has no row
    CHECK_ERR(buildLayerList(t, g, id, l), Err::NotFound);
    g.mat.clear();
    CHECK_ERR(buildLayerList(t, g, id, l), Err::NotFound);  // empty mat
    HeightGrid s = field(2, 2, 0, 0);
    s.h.pop_back();
    CHECK_ERR(buildLayerList(t, s, id, l), Err::BadLayout);  // grid size mismatch
    HeightGrid o = field(2, 2, 0, 0);
    id.x = 2;
    CHECK_ERR(buildLayerList(t, o, id, l), Err::BadLayout);  // tile outside
    id.x = 0;
    LayerTables empty;
    CHECK_ERR(buildLayerList(empty, o, id, l), Err::BadLayout);  // no layers
    std::vector<LayerList> all;
    CHECK_ERR(buildAllLayerLists(t, s, all), Err::BadLayout);
    CHECK(all.empty());
}

C5R_TEST(all_lists_are_column_major) {
    LayerTables t = smallTables();
    HeightGrid g = field(3, 2, 0, 0);
    for (uint32_t y = 64; y < 128; ++y)
        for (uint32_t x = 140; x < 150; ++x) g.mat[g.index(x, y)] = 2;  // inside tile (2,1) only
    std::vector<LayerList> all;
    CHECK_OK(buildAllLayerLists(t, g, all));
    CHECK_EQ(all.size(), static_cast<size_t>(6));
    for (size_t i = 0; i < all.size(); ++i)
        // tile (2,1) = index 5 holds them; tile (2,0) = index 4 too: its window reaches y 65 and wraps to y 126/127
        CHECK(ids(all[i]) == ((i == 4 || i == 5) ? std::vector<uint32_t>({0, 4}) : std::vector<uint32_t>({0})));
}

C5R_TEST(second_cache_flag) {
    LayerTables t = smallTables();
    LayerList l;
    l.count = 1;
    l.ids[0] = 0;
    CHECK(!needsSecondCache(t, l, true, false));
    CHECK(needsSecondCache(t, l, true, true));
    l.ids[0] = 5;  // set 3 has a spec map
    CHECK(needsSecondCache(t, l, true, false));
    CHECK(!needsSecondCache(t, l, false, true));
    l.ids[0] = 40;  // past the table: ignored
    CHECK(!needsSecondCache(t, l, true, false));
}

// ---- the checker tool as a product ----

C5R_TEST(checker_equal_lists_exit_0_and_report) {
    if (toolPath().empty()) c5r::test::skip("C5R_BAKELAYERS_CHECK not set");
    LayerTables t = smallTables();
    HeightGrid g = field(3, 2, 0, 0);
    for (uint32_t x = 140; x < 150; ++x) g.mat[g.index(x, 90)] = 1;
    std::vector<LayerList> all;
    CHECK_OK(buildAllLayerLists(t, g, all));
    std::vector<uint8_t> tap;
    CHECK_OK(heightfield::writeTap(g, tap));
    writeBytes(scratch("t.c5hf"), tap);
    writeText(scratch("t.csv"), smallCsv());
    writeText(scratch("exe.txt"), listsFile(g, t, all));
    std::string out;
    int code = runTool("--tap \"" + scratch("t.c5hf") + "\" --csv \"" + scratch("t.csv") + "\" --exe-lists \"" +
                           scratch("exe.txt") + "\" --report \"" + scratch("r.json") + "\" --server-lists \"" +
                           scratch("srv.txt") + "\"",
                       out);
    CHECK_EQ(code, 0);
    CHECK(out.find("6 tiles, 6 equal, 0 differ") != std::string::npos);
    CHECK(out.find("tables equal") != std::string::npos);
    std::string r = readText(scratch("r.json"));
    CHECK(r.find("\"equal\":6") != std::string::npos);
    CHECK(r.find("\"differences\":[]") != std::string::npos);
    std::string srv = readText(scratch("srv.txt"));
    CHECK(srv.find("tile 5 2 1 4 0 1 2 3") != std::string::npos);
}

C5R_TEST(checker_difference_exit_1_names_the_tile) {
    if (toolPath().empty()) c5r::test::skip("C5R_BAKELAYERS_CHECK not set");
    LayerTables t = smallTables();
    HeightGrid g = field(2, 2, 0, 0);
    std::vector<LayerList> all;
    CHECK_OK(buildAllLayerLists(t, g, all));
    all[3].ids[0] = 4;  // tile (1,1)
    LayerTables bad = t;
    bad.setOfLayer[2] = 9;  // a table difference too
    std::vector<uint8_t> tap;
    CHECK_OK(heightfield::writeTap(g, tap));
    writeBytes(scratch("d.c5hf"), tap);
    writeText(scratch("d.csv"), smallCsv());
    writeText(scratch("d.txt"), listsFile(g, bad, all));
    std::string out;
    int code = runTool("--tap \"" + scratch("d.c5hf") + "\" --csv \"" + scratch("d.csv") + "\" --exe-lists \"" +
                           scratch("d.txt") + "\" --report \"" + scratch("d.json") + "\"",
                       out);
    CHECK_EQ(code, 1);
    CHECK(out.find("4 tiles, 3 equal, 1 differ") != std::string::npos);
    CHECK(out.find("DIFF tile 3 (1,1): EXE [4] server [0]") != std::string::npos);
    CHECK(out.find("TABLE f10[2]: EXE 9, server 1") != std::string::npos);
    CHECK(readText(scratch("d.json")).find("\"differ\":1") != std::string::npos);
}

C5R_TEST(checker_negative_paths_exit_codes) {
    if (toolPath().empty()) c5r::test::skip("C5R_BAKELAYERS_CHECK not set");
    LayerTables t = smallTables();
    HeightGrid g = field(2, 2, 0, 0);
    std::vector<LayerList> all;
    CHECK_OK(buildAllLayerLists(t, g, all));
    std::vector<uint8_t> tap;
    CHECK_OK(heightfield::writeTap(g, tap));
    writeBytes(scratch("n.c5hf"), tap);
    writeText(scratch("n.csv"), smallCsv());
    writeText(scratch("n.txt"), listsFile(g, t, all));
    const std::string T = " --tap \"" + scratch("n.c5hf") + "\"", C = " --csv \"" + scratch("n.csv") + "\"",
                      L = " --exe-lists \"" + scratch("n.txt") + "\"";
    std::string out;
    // usage
    CHECK_EQ(runTool(T + L, out), 2);                                   // neither --csv nor --game-dir
    CHECK_EQ(runTool(T + C + L + " --game-dir x", out), 2);             // both
    CHECK_EQ(runTool(T + C + L + " --bogus 1", out), 2);
    CHECK_EQ(runTool(T + C + " --exe-lists", out), 2);                  // missing value
    // unreadable inputs
    CHECK_EQ(runTool(" --tap \"" + scratch("missing.c5hf") + "\"" + C + L, out), 3);
    CHECK_EQ(runTool(T + " --csv \"" + scratch("missing.csv") + "\"" + L, out), 3);
    CHECK_EQ(runTool(T + C + " --exe-lists \"" + scratch("missing.txt") + "\"", out), 3);
    CHECK(out.find("cannot read") != std::string::npos);
    // bad tap, bad CSV, bad lists
    writeText(scratch("bad.c5hf"), "C5HX this is not a tap");
    CHECK_EQ(runTool(" --tap \"" + scratch("bad.c5hf") + "\"" + C + L, out), 4);
    writeText(scratch("bad.csv"), std::string(kHeader) + "0,A,B\n");
    CHECK_EQ(runTool(T + " --csv \"" + scratch("bad.csv") + "\"" + L, out), 4);
    CHECK(out.find("TerrainDef.csv") != std::string::npos);
    const char* badLists[] = {"grid 2 2\nlayers 7\ntile 0 0 0 1 x\n",          // not a number
                              "layers 7\ntile 0 0 0 1 0\n",                    // tile before grid
                              "grid 2 2\nlayers 7\ntile 0 0 0 9 0 0 0 0 0 0 0 0 0\n",  // N > 8
                              "grid 2 2\nlayers 7\ntile 1 0 0 1 0\n",          // index not x * ty + y
                              "grid 2 2\nlayers 7\nfoo 1\n",                   // unknown key
                              "grid 2 2\ntile 0 0 0 1 0\n",                    // no layers line
                              "grid 2 2\nlayers 7\ntile 0 0 0 1 0\n"};         // tiles missing
    for (const char* b : badLists) {
        writeText(scratch("bad.txt"), b);
        CHECK_EQ(runTool(T + C + " --exe-lists \"" + scratch("bad.txt") + "\"", out), 4);
    }
    // shape: a tap without material bytes, a lists file for another grid, a material without a row
    HeightGrid nomat = g;
    nomat.mat.clear();
    std::vector<uint8_t> tap2;
    CHECK_OK(heightfield::writeTap(nomat, tap2));
    writeBytes(scratch("nomat.c5hf"), tap2);
    CHECK_EQ(runTool(" --tap \"" + scratch("nomat.c5hf") + "\"" + C + L, out), 5);
    CHECK(out.find("material") != std::string::npos);
    HeightGrid big = field(3, 2, 0, 0);
    std::vector<LayerList> all3;
    CHECK_OK(buildAllLayerLists(t, big, all3));
    writeText(scratch("other.txt"), listsFile(big, t, all3));
    CHECK_EQ(runTool(T + C + " --exe-lists \"" + scratch("other.txt") + "\"", out), 5);
    HeightGrid orphan = g;
    orphan.mat[orphan.index(5, 5)] = 77;
    std::vector<uint8_t> tap3;
    CHECK_OK(heightfield::writeTap(orphan, tap3));
    writeBytes(scratch("orphan.c5hf"), tap3);
    CHECK_EQ(runTool(" --tap \"" + scratch("orphan.c5hf") + "\"" + C + L, out), 5);
    CHECK(out.find("no texture set") != std::string::npos);
}

// ---- the install's TerrainDef.csv through gamefs ----

C5R_TEST(game_terraindef_matches_the_live_tables) {
    std::string dir = c5r::test::gameDir();
    if (dir.empty()) c5r::test::skip("C5R_GAME_DIR not set");
    gamefs::GameFiles fs;
    CHECK_OK(fs.open(gamefs::fromUtf8(dir)));
    LayerTables t;
    std::string origin;
    CHECK_OK(loadTerrainDef(fs, t, &origin));
    CHECK(origin == kSharedTerrainDef);  // through gamefs since issue 3404: the Shared DLC's loose file
    // The VP install of 2026-11-12 (terrain-bake-records.md 2.3, live): 41 rows + Checkerboard, 48 layers,
    // slope-projected sets 4, 8, 17; first ids 0,1,2,3,4,7,8,9,10,13,14.
    CHECK_EQ(t.sets.size(), static_cast<size_t>(42));
    CHECK_EQ(t.layerCount, 48u);
    for (size_t k = 0; k < t.sets.size(); ++k) CHECK_EQ(t.slopeProjected[k], static_cast<uint8_t>(k == 4 || k == 8 || k == 17));
    const uint8_t first[11] = {0, 1, 2, 3, 4, 7, 8, 9, 10, 13, 14};
    for (int k = 0; k < 11; ++k) CHECK_EQ(t.firstLayerOfMaterial[k], first[k]);
    CHECK_EQ(t.firstLayerOfMaterial[41], static_cast<uint8_t>(47));
    // The old signature reads through gamefs too (installRoot ignored).
    LayerTables same;
    CHECK_OK(loadTerrainDef(fs, gamefs::fromUtf8(dir), same, nullptr));
    CHECK_EQ(same.layerCount, t.layerCount);
    // gamefs without the package folders (the model before 3404) gives the base pack's older table (other slope
    // flags): the bug this issue fixed.
    gamefs::GameFiles old;
    gamefs::GameFiles::Options oo;
    oo.mountPackageRoots = false;
    CHECK_OK(old.open(gamefs::fromUtf8(dir), oo));
    LayerTables pack;
    std::string packOrigin;
    CHECK_OK(loadTerrainDef(old, pack, &packOrigin));
    CHECK(packOrigin.find(".fpk") != std::string::npos);
    CHECK(pack.layerCount != t.layerCount || pack.slopeProjected[9] != t.slopeProjected[9]);
    // the missing-file path: an empty file system view
    gamefs::GameFiles none;
    std::string o2 = "stale";
    CHECK_ERR(loadTerrainDef(none, t, &o2), Err::NotFound);
    CHECK(o2.empty());
    CHECK_ERR(loadTerrainDef(none, gamefs::fromUtf8(c5r::test::workDir()), t, nullptr), Err::NotFound);
    CHECK_EQ(t.layerCount, 48u);  // untouched by the failures
}

C5R_TEST_MAIN
