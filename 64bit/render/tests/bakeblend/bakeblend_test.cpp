// c5r::bake blend tests (issue 3402).
//
// Synthetic tests need nothing. The reference tests read:
//   C5R_BAKE_REF  a folder made by tools/bake_inputs.py + make_ref.py + ref_txt.py from a live run and RenderDoc
//                 captures (game data, on D; default D:/c5-64bit-work/3400/w2/ref-tiny): tap.c5hf, blendref.txt, one
//                 folder per captured blend draw (vb.bin, ib.bin, cb_vs0.bin, cb_vs2.bin) and textures/
//   C5R_EXE       the EXE (its .shader section), C5R_GAME_DIR the install (TerrainDef.csv and the layer textures).
// A missing input fails the test with the path it looked for (never a skip).
#include <d3d11.h>
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "c5r/bake/blend.h"
#include "c5r/bake/layers.h"
#include "c5r/gamefs/dds.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/shaders/shadersection.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::bake;

namespace {

std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

float hexFloat(const std::string& h) {
    uint32_t u = static_cast<uint32_t>(std::strtoul(h.c_str(), nullptr, 16));
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

uint32_t floatBits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

std::string sha256(const uint8_t* p, size_t n) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    uint8_t d[32] = {};
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return "";
    BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0);
    BCryptHashData(h, const_cast<PUCHAR>(p), static_cast<ULONG>(n), 0);
    BCryptFinishHash(h, d, 32, 0);
    BCryptDestroyHash(h);
    BCryptCloseAlgorithmProvider(alg, 0);
    std::string s;
    char b[3];
    for (int i = 0; i < 32; ++i) {
        std::snprintf(b, sizeof b, "%02x", d[i]);
        s += b;
    }
    return s;
}

// ---------------------------------------------------------------- synthetic fixtures

struct Synth {
    heightfield::HeightGrid g;
    LayerTables t;
    BlendInputs in;
    Synth(uint32_t plotsX = 4, uint32_t plotsY = 4) {
        g.mapW = plotsX;
        g.mapH = plotsY;
        g.samplesX = plotsX * 64;
        g.samplesY = plotsY * 64;
        g.h.assign(size_t(g.samplesX) * g.samplesY, 100);
        g.mat.assign(g.h.size(), 0);
        t.layerCount = 6;
        for (int m = 0; m < 4; ++m) t.firstLayerOfMaterial[m] = static_cast<uint8_t>(m);
        t.firstLayerOfMaterial[3] = 3;  // material 3: slope-projected, layers 3, 4, 5
        t.slopeProjected[3] = 1;
        t.setOfLayer = {0, 1, 2, 3, 3, 3};
        t.projectionOfLayer = {0, 0, 0, 0, 1, 2};
        for (int k = 0; k < 4; ++k) {
            TextureSet s;
            s.diffuse = "d" + std::to_string(k) + ".dds";
            s.heightmap = "h" + std::to_string(k) + ".dds";
            s.textureScale = 0.014f;
            t.sets.push_back(s);
        }
        in.grid = &g;
        in.tables = &t;
    }
};

LayerList list1(uint32_t a) {
    LayerList l;
    l.count = 1;
    l.ids[0] = a;
    return l;
}

// ---------------------------------------------------------------- the reference folder

struct DrawRef {
    std::string dir;
    TileGeom geom;
    int32_t e4 = 0, e8 = 0;
    LayerList list;
    struct Tex { uint32_t slot, rid, w, h, mips, fmt; };
    std::vector<Tex> tex;
    std::string vsSha, psSha;
};

struct Ref {
    std::string root;
    uint32_t sx = 0, sy = 0;
    float step[3] = {};
    LayerTables t;
    std::vector<KernelTap> kernel;
    std::vector<std::string> kernelHex;
    uint32_t largest = 0, mapPlotsX = 0;
    std::vector<float> liveScale;  // TR8+0xE10, 64 entries
    std::vector<DrawRef> draws;
    heightfield::HeightGrid grid;
};

// C5R_BAKE_REF: one or more reference folders separated by ';' (each from one live session and its captures).
std::vector<std::string> refRoots() {
    const char* e = std::getenv("C5R_BAKE_REF");
    std::string v = e && *e ? e : "D:/c5-64bit-work/3400/w2/ref-tiny;D:/c5-64bit-work/3400/w2/ref-slope";
    std::vector<std::string> out;
    size_t a = 0;
    while (a <= v.size()) {
        size_t b = v.find(';', a);
        if (b == std::string::npos) b = v.size();
        if (b > a) out.push_back(v.substr(a, b - a));
        a = b + 1;
    }
    return out;
}

// Loads the reference; on a missing input it fails the test with the path and returns false.
bool loadRef(Ref& r, const std::string& root) {
    r.root = root;
    const std::string txt = r.root + "/blendref.txt";
    std::ifstream f(txt);
    if (!f) {
        c5r::test::fail(__FILE__, __LINE__, "C5R_BAKE_REF: " + txt + " missing (make it with render/tests/bakeblend/tools)");
        return false;
    }
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream is(line);
        std::string k;
        is >> k;
        if (k == "samples") is >> r.sx >> r.sy;
        else if (k == "steps") {
            std::string a;
            for (int i = 0; i < 3; ++i) { is >> a; r.step[i] = hexFloat(a); }
        } else if (k == "scale_of_set") {
            std::string a;
            while (is >> a) r.liveScale.push_back(hexFloat(a));
        } else if (k == "layer_count") is >> r.t.layerCount;
        else if (k == "first_layer_of_material" || k == "slope_projected") {
            for (int i = 0; i < 64; ++i) {
                uint32_t v = 0;
                is >> v;
                (k[0] == 'f' ? r.t.firstLayerOfMaterial : r.t.slopeProjected)[i] = static_cast<uint8_t>(v);
            }
        } else if (k == "set_of_layer" || k == "projection_of_layer") {
            std::vector<uint32_t>& v = k[0] == 's' ? r.t.setOfLayer : r.t.projectionOfLayer;
            uint32_t x;
            while (is >> x) v.push_back(x);
        } else if (k == "kernel") {
            uint32_t n = 0;
            is >> n;
            for (uint32_t i = 0; i < n; ++i) {
                KernelTap tp;
                std::string h;
                is >> tp.dx >> tp.dy >> h;
                tp.w = hexFloat(h);
                r.kernel.push_back(tp);
                r.kernelHex.push_back(h);
            }
        } else if (k == "largest_cache") is >> r.largest;
        else if (k == "map_plots_x") is >> r.mapPlotsX;
        else if (k == "draw") {
            DrawRef d;
            is >> d.dir >> d.geom.originX >> d.geom.originY >> d.geom.extentX >> d.geom.extentY >> d.e4 >> d.e8 >> d.list.count;
            for (uint32_t i = 0; i < d.list.count && i < 8; ++i) is >> d.list.ids[i];
            r.draws.push_back(d);
        } else if (k == "tex" && !r.draws.empty()) {
            DrawRef::Tex t;
            is >> t.slot >> t.rid >> t.w >> t.h >> t.mips >> t.fmt;
            r.draws.back().tex.push_back(t);
        } else if (k == "shaders" && !r.draws.empty()) {
            is >> r.draws.back().vsSha >> r.draws.back().psSha;
        }
    }
    std::vector<uint8_t> tap;
    const std::string tp = r.root + "/tap.c5hf";
    if (!readFile(tp, tap)) {
        c5r::test::fail(__FILE__, __LINE__, "C5R_BAKE_REF: " + tp + " missing");
        return false;
    }
    Status st = heightfield::readTap(tap.data(), tap.size(), r.grid);
    if (!st.ok()) {
        c5r::test::fail(__FILE__, __LINE__, tp + ": " + st.str());
        return false;
    }
    if (r.draws.empty()) {
        c5r::test::fail(__FILE__, __LINE__, txt + " lists no captured draw");
        return false;
    }
    return true;
}

// TerrainDef.csv from the install (C5R_GAME_DIR) into r.t.sets.
bool loadSets(Ref& r, gamefs::GameFiles& fs) {
    const std::string gd = c5r::test::gameDir();
    Status st = fs.open(widen(gd));
    if (!st.ok()) {
        c5r::test::fail(__FILE__, __LINE__, "C5R_GAME_DIR " + gd + ": " + st.str());
        return false;
    }
    // The EXE's tables match the loose DLC copy Assets/DLC/Shared/Terrain/TerrainDef.csv; since issue 3404 gamefs's
    // name lookup returns it (package folders mounted as the EXE's "Common" kind), and 3401's parser (the one parser,
    // with the appended Checkerboard set) reads it.
    LayerTables csv;
    std::string origin;
    st = loadTerrainDef(fs, csv, &origin);
    if (!st.ok()) {
        c5r::test::fail(__FILE__, __LINE__, "TerrainDef.csv: " + st.str());
        return false;
    }
    CHECK(origin == kSharedTerrainDef);
    r.t.sets = csv.sets;
    // The CSV's scale column (and the appended set's 0.01) equals the EXE's per-set scale for every set.
    CHECK(r.t.sets.size() <= r.liveScale.size());
    for (size_t k = 0; k < r.t.sets.size() && k < r.liveScale.size(); ++k) {
        if (floatBits(r.t.sets[k].textureScale) != floatBits(r.liveScale[k]))
            std::printf("set %zu (%s): CSV scale %.9g, live %.9g\n", k, r.t.sets[k].diffuse.c_str(), r.t.sets[k].textureScale,
                        r.liveScale[k]);
        CHECK_EQ(floatBits(r.t.sets[k].textureScale), floatBits(r.liveScale[k]));
    }
    // The tables computed from the CSV equal the live ones the reference carries.
    CHECK_EQ(csv.layerCount, r.t.layerCount);
    for (uint32_t l = 0; l < r.t.layerCount && l < csv.setOfLayer.size() && l < r.t.setOfLayer.size(); ++l)
        CHECK_EQ(csv.setOfLayer[l], r.t.setOfLayer[l]);
    return true;
}

BlendInputs inputsOf(Ref& r) {
    BlendInputs in;
    in.grid = &r.grid;
    in.tables = &r.t;
    in.stepX = r.step[0];
    in.stepY = r.step[1];
    in.stepZ = r.step[2];
    return in;
}

struct Device {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    Device() {
        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
    }
    ~Device() {
        if (ctx) ctx->Release();
        if (dev) dev->Release();
    }
};

// Half-float bit patterns to an ordered integer, for distances in float16 units (ULPs).
int32_t halfOrder(uint16_t h) { return (h & 0x8000) ? -static_cast<int32_t>(h & 0x7fff) : static_cast<int32_t>(h); }

struct ChannelStats {
    double equalShare = 0;
    uint32_t maxD = 0, medianD = 0, p95D = 0;
};

void compareTargets(const BlendTargets& a, const BlendTargets& b, ChannelStats out[3][4]) {
    for (int t = 0; t < 3; ++t)
        for (int c = 0; c < 4; ++c) {
            std::vector<uint32_t> d;
            const size_t n = size_t(a.size) * a.size;
            d.reserve(n);
            size_t eq = 0;
            for (size_t i = 0; i < n; ++i) {
                const int32_t x = halfOrder(a.texels[t][i * 4 + c]), y = halfOrder(b.texels[t][i * 4 + c]);
                const uint32_t dd = static_cast<uint32_t>(std::abs(x - y));
                if (!dd) ++eq;
                d.push_back(dd);
            }
            std::sort(d.begin(), d.end());
            out[t][c].equalShare = double(eq) / double(n);
            out[t][c].maxD = d.back();
            out[t][c].medianD = d[n / 2];
            out[t][c].p95D = d[(n * 95) / 100];
        }
}

}  // namespace

// ================================================================ synthetic tests

C5R_TEST(kernel_default_is_separable_and_normalised) {
    std::vector<KernelTap> k = defaultKernel();
    CHECK_EQ(k.size(), size_t(25));
    double s = 0;
    for (const KernelTap& t : k) s += t.w;
    CHECK(std::fabs(s - 1.0) < 1e-5);
    CHECK_EQ(k[0].dx, -2);
    CHECK_EQ(k[0].dy, -2);
    CHECK_EQ(k[5].dy, -1);
    CHECK_EQ(floatBits(k[0].w), floatBits(k[24].w));
    CHECK_EQ(floatBits(k[1].w), floatBits(k[5].w));
    CHECK(k[12].w > k[7].w && k[7].w > k[2].w);
}

C5R_TEST(slope_class_rule) {
    CHECK_EQ(slopeClass(0, 0, 1), 0);
    CHECK_EQ(slopeClass(0.3f, 0.5f, 0.8f), 1);
    CHECK_EQ(slopeClass(0.5f, 0.3f, 0.8f), 2);
    CHECK_EQ(slopeClass(0.4f, 0.4f, 0.9f), 2);  // nz == 0.9 is a slope; |ny| == |nx| gives 2
    CHECK_EQ(slopeClass(0, 0, std::nanf("")), 2);
}

C5R_TEST(single_material_gives_full_weight) {
    Synth s;
    std::vector<BlendVertex> v;
    TileGeom g;
    g.originX = 64;
    g.originY = 64;
    CHECK_OK(fillBlendVertices(s.in, g, list1(0), SlopeClasses(), v));
    CHECK_EQ(v.size(), size_t(67 * 67));
    for (const BlendVertex& x : v) {
        CHECK_EQ(x.weight[0], 255);
        CHECK_EQ(x.layer[0], 0);
        CHECK_EQ(x.weight[1], 0);
    }
    CHECK_EQ(floatBits(v[0].pos[0]), floatBits(-1.0f));
    CHECK_EQ(floatBits(v[0].pos[1]), floatBits(1.0f));
    CHECK_EQ(floatBits(v[66].pos[0]), floatBits(1.0f));
    CHECK_EQ(floatBits(v[66 * 67].pos[1]), floatBits(-1.0f));
    CHECK_EQ(v[0].sample[0], 63.0f);
    CHECK_EQ(v[0].sample[1], 63.0f);
    CHECK(std::fabs(v[0].world[2] - 100.0f / 255.0f * 0.2f) < 1e-6f);
}

C5R_TEST(two_materials_split_and_drop_unlisted) {
    Synth s;
    // material 1 on the right half of the map: x >= 128
    for (uint32_t y = 0; y < s.g.samplesY; ++y)
        for (uint32_t x = 128; x < s.g.samplesX; ++x) s.g.mat[s.g.index(x, y)] = 1;
    std::vector<float> w;
    CHECK_OK(layerWeights(s.in, 128, 100, 0, w));
    CHECK(w[1] > w[0] && w[0] > 0.0f);
    CHECK(std::fabs(w[0] + w[1] - 1.0f) < 1e-6f);
    LayerList l;
    l.count = 2;
    l.ids[0] = 0;
    l.ids[1] = 1;
    TileGeom g;
    g.originX = 64 + 32;
    g.originY = 64;
    std::vector<BlendVertex> v;
    CHECK_OK(fillBlendVertices(s.in, g, l, SlopeClasses(), v));
    const BlendVertex& c = v[size_t(30) * 67 + 33];  // sample x = 128
    CHECK(c.weight[0] > 0 && c.weight[1] > 0 && c.weight[0] + c.weight[1] <= 255 && c.weight[0] + c.weight[1] >= 253);
    // listed only layer 0: the right half's weight is dropped, not renormalised
    std::vector<BlendVertex> v1;
    CHECK_OK(fillBlendVertices(s.in, g, list1(0), SlopeClasses(), v1));
    CHECK(v1[size_t(30) * 67 + 33].weight[0] < 128);
    // a vertex with no listed weight at all gets weight 1 in slot 0
    CHECK_EQ(v1[size_t(30) * 67 + 66].weight[0], 255);
}

C5R_TEST(slope_projected_material_moves_layer_by_class) {
    Synth s;
    std::fill(s.g.mat.begin(), s.g.mat.end(), uint8_t(3));
    std::vector<float> w;
    CHECK_OK(layerWeights(s.in, 100, 100, 0, w));
    CHECK_EQ(w[3], 1.0f);
    CHECK_OK(layerWeights(s.in, 100, 100, 2, w));
    CHECK_EQ(w[5], 1.0f);
    CHECK_EQ(w[3], 0.0f);
}

C5R_TEST(kernel_wraps_unsigned_without_adding_the_width) {
    Synth s(3, 3);  // samplesX 192, not a power of two
    // material 1 only at x = 189 (= -3 mod 192) and at x = 4294967293 mod 192 = 61
    for (uint32_t y = 0; y < s.g.samplesY; ++y) s.g.mat[s.g.index(61, y)] = 1;
    std::vector<float> w;
    CHECK_OK(layerWeights(s.in, -1, 50, 0, w));  // taps reach x = -3 -> 61 in the EXE's arithmetic
    CHECK(w[1] > 0.0f);
    CHECK_EQ(4294967293u % 192u, 61u);
}

C5R_TEST(indices_order_and_count) {
    std::vector<uint16_t> ix;
    CHECK_OK(buildBlendIndices(67, 67, ix));
    CHECK_EQ(ix.size(), size_t(26136));
    uint16_t mx = 0;
    for (uint16_t i : ix) mx = std::max(mx, i);
    CHECK_EQ(mx, uint16_t(4488));
    // first quad: x = 1 (odd), y = 1: (1,1) (1,0) (0,1) / (0,1) (1,0) (0,0)
    const uint16_t want[6] = {68, 1, 67, 67, 1, 0};
    for (int i = 0; i < 6; ++i) CHECK_EQ(ix[i], want[i]);
    CHECK_OK(buildBlendIndices(66, 66, ix));
    CHECK_EQ(ix.size(), size_t(65 * 65 * 6));
}

C5R_TEST(terrain_layers_quantise_scale) {
    Synth s;
    std::vector<float> L;
    CHECK_OK(fillTerrainLayers(s.t, 56, L));
    CHECK_EQ(L.size(), size_t(384));
    const float X = 3583.0f;
    CHECK_EQ(floatBits(L[0]), floatBits(static_cast<float>(static_cast<int>(0.014f * 0.33f * X)) * (1.0f / X) * 3.030303f));
    CHECK_EQ(L[4 * 4 + 1], 1.0f);
    CHECK_EQ(L[4 * 6], 0.0f);
    std::vector<uint8_t> own;
    CHECK_OK(fillOwnBlock(s.t, 56, list1(4), 512, -832, 64, own));
    const float* f = reinterpret_cast<const float*>(own.data());
    CHECK_EQ(f[0], -1.0f / 1024.0f);
    CHECK_EQ(f[1], 1.0f / 1024.0f);
    CHECK_EQ(floatBits(f[4]), floatBits(L[16]));
    CHECK_EQ(f[5], 1.0f);
    CHECK_EQ(f[4 * 73], -832.0f);
}

// ---------------------------------------------------------------- negative tests (every validation path)

C5R_TEST(negative_vertex_fill_inputs) {
    Synth s;
    std::vector<BlendVertex> v(5);
    TileGeom g;
    LayerList l9;
    l9.count = 9;
    CHECK_ERR(fillBlendVertices(s.in, g, l9, SlopeClasses(), v), Err::BadLayout);
    CHECK(v.empty());
    CHECK_ERR(fillBlendVertices(s.in, g, list1(6), SlopeClasses(), v), Err::BadLayout);  // id >= layerCount
    TileGeom big = g;
    big.extentX = 66;
    CHECK_ERR(fillBlendVertices(s.in, big, list1(0), SlopeClasses(), v), Err::BadLayout);
    TileGeom zero = g;
    zero.extentY = 0;
    CHECK_ERR(fillBlendVertices(s.in, zero, list1(0), SlopeClasses(), v), Err::BadLayout);
    CHECK_ERR(fillBlendVertices(s.in, g, list1(0), SlopeClasses(10, 0), v), Err::BadLayout);  // classes wrong size
    CHECK_ERR(fillBlendVertices(s.in, g, list1(0), SlopeClasses(67 * 67, 3), v), Err::BadLayout);  // class 3
    BlendInputs noGrid = s.in;
    noGrid.grid = nullptr;
    CHECK_ERR(fillBlendVertices(noGrid, g, list1(0), SlopeClasses(), v), Err::BadLayout);
    Synth s2;
    s2.g.mat.clear();
    CHECK_ERR(fillBlendVertices(s2.in, g, list1(0), SlopeClasses(), v), Err::BadLayout);  // grid too small / no bytes
    Synth s3;
    s3.g.h.resize(10);
    CHECK_ERR(fillBlendVertices(s3.in, g, list1(0), SlopeClasses(), v), Err::BadLayout);
    Synth s4;
    s4.in.kernel.clear();
    CHECK_ERR(fillBlendVertices(s4.in, g, list1(0), SlopeClasses(), v), Err::BadLayout);
    Synth s5;
    s5.t.layerCount = 0;
    CHECK_ERR(fillBlendVertices(s5.in, g, list1(0), SlopeClasses(), v), Err::BadLayout);
    Synth s6;
    s6.t.firstLayerOfMaterial[0] = 200;  // material maps past layerCount
    CHECK_ERR(fillBlendVertices(s6.in, g, list1(0), SlopeClasses(), v), Err::BadLayout);
    CHECK(v.empty());
    std::vector<float> w;
    CHECK_ERR(layerWeights(s.in, 0, 0, 3, w), Err::BadLayout);
}

C5R_TEST(negative_indices_and_constants) {
    std::vector<uint16_t> ix(3);
    CHECK_ERR(buildBlendIndices(1, 67, ix), Err::BadLayout);
    CHECK(ix.empty());
    CHECK_ERR(buildBlendIndices(300, 300, ix), Err::BadLayout);
    Synth s;
    std::vector<float> L;
    CHECK_ERR(fillTerrainLayers(s.t, 0, L), Err::BadLayout);
    Synth big;
    big.t.layerCount = 97;
    CHECK_ERR(fillTerrainLayers(big.t, 56, L), Err::BadLayout);
    Synth shortT;
    shortT.t.setOfLayer.resize(2);
    CHECK_ERR(fillTerrainLayers(shortT.t, 56, L), Err::BadLayout);
    Synth badSet;
    badSet.t.setOfLayer[0] = 9;
    CHECK_ERR(fillTerrainLayers(badSet.t, 56, L), Err::BadLayout);
    std::vector<uint8_t> own;
    CHECK_ERR(fillOwnBlock(s.t, 56, list1(0), 0, 0, 0, own), Err::BadLayout);
    CHECK_ERR(fillOwnBlock(s.t, 56, list1(7), 512, 0, 0, own), Err::BadLayout);
    // (The TerrainDef.csv parser's negative tests are 3401's, render/tests/bakelayers: one parser since issue 3404.)
}

C5R_TEST(negative_pass_without_init) {
    BlendPass p;
    shaders::ShaderSection empty;
    CHECK_ERR(p.init(nullptr, empty), Err::BadLayout);
    Device d;
    CHECK(d.dev != nullptr);
    std::vector<BlendVertex> v(4489);
    std::vector<uint16_t> ix(6, 0);
    std::vector<uint8_t> own(kOwnBlockBytes);
    std::vector<float> L(384);
    CHECK_ERR(p.run(d.ctx, v, ix, own, L, BlendTextures()), Err::BadLayout);
    BlendTargets t;
    CHECK_ERR(p.readTargets(d.ctx, t), Err::BadLayout);
    if (d.dev) CHECK_ERR(p.init(d.dev, empty), Err::NotFound);  // an empty section has no blend shaders
}

// ================================================================ against the EXE and the captures

C5R_TEST(game_shader_init_and_negative_blobs) {
    const std::string exe = c5r::test::exePath();
    shaders::ShaderSection sec;
    CHECK_OK(sec.open(widen(exe)));
    Device d;
    CHECK(d.dev != nullptr);
    if (!d.dev) return;
    BlendPass p;
    CHECK_OK(p.init(d.dev, sec));
    BlendPass n1;
    CHECK_ERR(n1.init(d.dev, sec, 0x12345678, kBlendPsVa), Err::NotFound);   // shader not found
    CHECK_ERR(n1.init(d.dev, sec, kBlendVsVa, 0x12345678), Err::NotFound);
    CHECK_ERR(n1.init(d.dev, sec, kBlendPsVa, kBlendPsVa), Err::BadLayout);  // a PS where the VS should be
    CHECK_ERR(n1.init(d.dev, sec, 0x067D6490, kBlendPsVa), Err::BadLayout);  // the terrain VS: wrong reflection
    CHECK_ERR(n1.init(d.dev, sec, kBlendVsVa, 0x067E6E58), Err::BadLayout);  // the terrain PS: wrong reflection
    // the VS/PS of the captures are the EXE's blobs
    const shaders::ShaderBlob* bv = sec.findByVa(kBlendVsVa);
    const shaders::ShaderBlob* bp = sec.findByVa(kBlendPsVa);
    CHECK(bv && bp);
    if (!bv || !bp) return;
    for (const std::string& root : refRoots()) {
        Ref r;
        if (!loadRef(r, root)) continue;
        for (const DrawRef& dr : r.draws) {
            CHECK_EQ(sha256(bv->bytes, bv->size), dr.vsSha);
            CHECK_EQ(sha256(bp->bytes, bp->size), dr.psSha);
        }
        std::printf("blend shaders: VS 0x%08x (%u B) and PS 0x%08x (%u B) equal the captured ones in %zu draws (%s)\n",
                    kBlendVsVa, bv->size, kBlendPsVa, bp->size, r.draws.size(), root.c_str());
    }
}

static void ref_kernel_equals_the_live_table_one(const std::string& root) {
    Ref r;
    if (!loadRef(r, root)) return;
    std::vector<KernelTap> k = defaultKernel();
    CHECK_EQ(k.size(), r.kernel.size());
    for (size_t i = 0; i < k.size() && i < r.kernel.size(); ++i) {
        CHECK_EQ(k[i].dx, r.kernel[i].dx);
        CHECK_EQ(k[i].dy, r.kernel[i].dy);
        CHECK_EQ(floatBits(k[i].w), floatBits(r.kernel[i].w));
    }
}

static void ref_vertex_buffers_byte_equal_one(const std::string& root) {
    Ref r;
    if (!loadRef(r, root)) return;
    BlendInputs in = inputsOf(r);
    size_t tiles = 0, multi = 0;
    for (const DrawRef& d : r.draws) {
        std::vector<uint8_t> vb;
        const std::string p = r.root + "/" + d.dir + "/vb.bin";
        if (!readFile(p, vb)) {
            c5r::test::fail(__FILE__, __LINE__, "missing " + p);
            continue;
        }
        std::vector<BlendVertex> v;
        CHECK_OK(fillBlendVertices(in, d.geom, d.list, SlopeClasses(), v));
        CHECK_EQ(vb.size(), v.size() * sizeof(BlendVertex));
        size_t diff = 0;
        for (size_t i = 0; i < v.size() && (i + 1) * 48 <= vb.size(); ++i)
            if (std::memcmp(&v[i], &vb[i * 48], 48) != 0) ++diff;
        std::printf("vertex buffer %s: tile origin (%d, %d), %u layer(s), %zu vertices, %zu differ\n", d.dir.c_str(),
                    d.geom.originX, d.geom.originY, d.list.count, v.size(), diff);
        CHECK_EQ(diff, size_t(0));
        ++tiles;
        if (d.list.count > 1) ++multi;
    }
    CHECK(multi >= 1);
    std::printf("vertex buffers byte-equal: %zu tiles, %zu with several layers\n", tiles, multi);
}

static void ref_index_buffers_equal_one(const std::string& root) {
    Ref r;
    if (!loadRef(r, root)) return;
    for (const DrawRef& d : r.draws) {
        std::vector<uint8_t> ib;
        const std::string p = r.root + "/" + d.dir + "/ib.bin";
        if (!readFile(p, ib)) {
            c5r::test::fail(__FILE__, __LINE__, "missing " + p);
            continue;
        }
        std::vector<uint16_t> ix;
        CHECK_OK(buildBlendIndices(d.geom.extentX + 2, d.geom.extentY + 2, ix));
        CHECK_EQ(ib.size(), ix.size() * 2);
        CHECK(ib.size() == ix.size() * 2 && std::memcmp(ib.data(), ix.data(), ib.size()) == 0);
    }
}

static void ref_constant_blocks_equal_where_read_one(const std::string& root) {
    Ref r;
    gamefs::GameFiles fs;
    if (!loadRef(r, root) || !loadSets(r, fs)) return;
    std::vector<float> L;
    CHECK_OK(fillTerrainLayers(r.t, r.mapPlotsX, L));
    for (const DrawRef& d : r.draws) {
        std::vector<uint8_t> c2, c0;
        const std::string p2 = r.root + "/" + d.dir + "/cb_vs2.bin", p0 = r.root + "/" + d.dir + "/cb_vs0.bin";
        if (!readFile(p2, c2) || !readFile(p0, c0)) {
            c5r::test::fail(__FILE__, __LINE__, "missing " + p2 + " or " + p0);
            continue;
        }
        CHECK_EQ(c2.size(), size_t(0x600));
        size_t bad = 0;
        for (uint32_t l = 0; l < r.t.layerCount && (l + 1) * 16 <= c2.size(); ++l)
            if (std::memcmp(&L[4 * l], &c2[16 * l], 8) != 0) ++bad;
        CHECK_EQ(bad, size_t(0));
        std::vector<uint8_t> own;
        CHECK_OK(fillOwnBlock(r.t, r.mapPlotsX, d.list, r.largest, d.e4, d.e8, own));
        CHECK_EQ(c0.size(), size_t(kOwnBlockBytes));
        // the parts written for this tile: [0].xy, the slots' (scale, projection), [73].xy
        CHECK(std::memcmp(own.data(), c0.data(), 8) == 0);
        for (uint32_t k = 0; k < d.list.count; ++k) CHECK(std::memcmp(&own[16 * (1 + k)], &c0[16 * (1 + k)], 8) == 0);
        CHECK(std::memcmp(&own[16 * 73], &c0[16 * 73], 8) == 0);
    }
}

static void ref_blend_targets_server_vs_capture_replay_one(const std::string& root) {
    Ref r;
    gamefs::GameFiles fs;
    if (!loadRef(r, root) || !loadSets(r, fs)) return;
    const std::string exe = c5r::test::exePath();
    shaders::ShaderSection sec;
    CHECK_OK(sec.open(widen(exe)));
    Device d;
    CHECK(d.dev != nullptr);
    if (!d.dev) return;
    BlendPass pass;
    CHECK_OK(pass.init(d.dev, sec));
    BlendTextureCache cache;
    BlendInputs in = inputsOf(r);
    std::vector<float> L;
    CHECK_OK(fillTerrainLayers(r.t, r.mapPlotsX, L));
    std::map<uint32_t, ID3D11ShaderResourceView*> captured;
    const std::string csvPath = c5r::test::workDir() + "/bakeblend_targets.csv";
    FILE* csv = std::fopen(csvPath.c_str(), "w");
    if (csv) std::fprintf(csv, "draw,layers,textures_equal,target,channel,equal_share,max_d,median_d,p95_d\n");
    for (const DrawRef& dr : r.draws) {
        // the server's inputs
        std::vector<BlendVertex> v;
        std::vector<uint16_t> ix;
        std::vector<uint8_t> own;
        CHECK_OK(fillBlendVertices(in, dr.geom, dr.list, SlopeClasses(), v));
        CHECK_OK(buildBlendIndices(dr.geom.extentX + 2, dr.geom.extentY + 2, ix));
        CHECK_OK(fillOwnBlock(r.t, r.mapPlotsX, dr.list, r.largest, dr.e4, dr.e8, own));
        BlendTextures tex;
        Status st = cache.bind(d.dev, fs, r.t, dr.list, pass.dummySrv(), tex);
        CHECK_OK(st);
        if (!st.ok()) continue;
        // texture identity: the install's DDS data against the captured subresources
        bool texEqual = true;
        for (const DrawRef::Tex& t : dr.tex) {
            if (t.slot >= 24 || t.w == 32) continue;  // the dummy
            const uint32_t i = t.slot % 8;
            const TextureSet& s = r.t.sets[r.t.setOfLayer[dr.list.ids[i]]];
            const std::string& name = t.slot < 8 ? s.diffuse : (t.slot < 16 ? s.heightmap : s.specmap);
            std::vector<uint8_t> bytes;
            CHECK_OK(fs.read(name, bytes));
            gamefs::DdsImage img;
            CHECK_OK(gamefs::parseDds(bytes.data(), bytes.size(), img));
            bool eq = img.width == t.w && img.mipLevels == t.mips;
            for (uint32_t m = 0; eq && m < t.mips; ++m) {
                std::vector<uint8_t> cap;
                const std::string p = r.root + "/textures/" + std::to_string(t.rid) + "_" + std::to_string(m) + ".bin";
                if (!readFile(p, cap)) {
                    c5r::test::fail(__FILE__, __LINE__, "missing " + p);
                    eq = false;
                    break;
                }
                const gamefs::DdsSubresource& sr = img.subresources[m];
                eq = cap.size() == sr.size && std::memcmp(cap.data(), img.pixels() + sr.offset, sr.size) == 0;
            }
            if (!eq) {
                texEqual = false;
                std::printf("  texture %s (slot %u): install %ux%u %u mips, captured %ux%u %u mips, data %s\n", name.c_str(),
                            t.slot, img.width, img.height, img.mipLevels, t.w, t.h, t.mips,
                            img.width == t.w ? "differs" : "not compared");
            }
        }
        Status rs = pass.run(d.ctx, v, ix, own, L, tex);
        CHECK_OK(rs);
        if (!rs.ok()) continue;
        BlendTargets server;
        CHECK_OK(pass.readTargets(d.ctx, server));

        // the replay: the captured vertex, index and constant data and the captured textures
        std::vector<uint8_t> vb, ib, c0, c2;
        const std::string base = r.root + "/" + dr.dir;
        if (!readFile(base + "/vb.bin", vb) || !readFile(base + "/ib.bin", ib) || !readFile(base + "/cb_vs0.bin", c0) ||
            !readFile(base + "/cb_vs2.bin", c2)) {
            c5r::test::fail(__FILE__, __LINE__, "missing capture data in " + base);
            continue;
        }
        std::vector<BlendVertex> cv(vb.size() / 48);
        std::memcpy(cv.data(), vb.data(), cv.size() * 48);
        std::vector<uint16_t> cix(ib.size() / 2);
        std::memcpy(cix.data(), ib.data(), cix.size() * 2);
        std::vector<float> cl(384);
        std::memcpy(cl.data(), c2.data(), std::min(c2.size(), size_t(1536)));
        c0.resize(kOwnBlockBytes);
        BlendTextures ctex;
        for (const DrawRef::Tex& t : dr.tex) {
            if (t.slot >= 24) continue;
            if (!captured.count(t.rid)) {
                std::vector<std::vector<uint8_t>> subs(t.mips);
                std::vector<D3D11_SUBRESOURCE_DATA> sd(t.mips);
                bool ok = true;
                for (uint32_t m = 0; m < t.mips; ++m) {
                    const std::string p = r.root + "/textures/" + std::to_string(t.rid) + "_" + std::to_string(m) + ".bin";
                    if (!readFile(p, subs[m])) {
                        c5r::test::fail(__FILE__, __LINE__, "missing " + p);
                        ok = false;
                        break;
                    }
                    const uint32_t w = std::max(1u, t.w >> m);
                    sd[m].pSysMem = subs[m].data();
                    sd[m].SysMemPitch = t.fmt == 28 ? w * 4 : std::max(1u, (w + 3) / 4) * (t.fmt == 71 ? 8 : 16);
                }
                if (!ok) continue;
                D3D11_TEXTURE2D_DESC td = {};
                td.Width = t.w;
                td.Height = t.h;
                td.MipLevels = t.mips;
                td.ArraySize = 1;
                td.Format = static_cast<DXGI_FORMAT>(t.fmt);
                td.SampleDesc.Count = 1;
                td.Usage = D3D11_USAGE_IMMUTABLE;
                td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                ID3D11Texture2D* tx = nullptr;
                ID3D11ShaderResourceView* sv = nullptr;
                if (FAILED(d.dev->CreateTexture2D(&td, sd.data(), &tx)) || FAILED(d.dev->CreateShaderResourceView(tx, nullptr, &sv))) {
                    c5r::test::fail(__FILE__, __LINE__, "replay texture " + std::to_string(t.rid) + " not created");
                    if (tx) tx->Release();
                    continue;
                }
                tx->Release();
                captured[t.rid] = sv;
            }
            ctex.srv[t.slot] = captured[t.rid];
        }
        rs = pass.run(d.ctx, cv, cix, c0, cl, ctex);
        CHECK_OK(rs);
        if (!rs.ok()) continue;
        BlendTargets replay;
        CHECK_OK(pass.readTargets(d.ctx, replay));
        ChannelStats cs[3][4];
        compareTargets(server, replay, cs);
        std::printf("blend targets %s (%u layer(s), textures %s):\n", dr.dir.c_str(), dr.list.count,
                    texEqual ? "equal" : "DIFFER");
        uint32_t worst = 0;
        for (int t = 0; t < 3; ++t) {
            std::printf("  target %d:", t);
            for (int c = 0; c < 4; ++c) {
                std::printf(" [%c equal %.4f max %u med %u p95 %u]", "RGBA"[c], cs[t][c].equalShare, cs[t][c].maxD,
                            cs[t][c].medianD, cs[t][c].p95D);
                worst = std::max(worst, cs[t][c].maxD);
                if (csv)
                    std::fprintf(csv, "%s,%u,%d,%d,%c,%.6f,%u,%u,%u\n", dr.dir.c_str(), dr.list.count, texEqual ? 1 : 0, t,
                                 "RGBA"[c], cs[t][c].equalShare, cs[t][c].maxD, cs[t][c].medianD, cs[t][c].p95D);
            }
            std::printf("\n");
        }
        if (texEqual) CHECK_EQ(worst, 0u);  // equal inputs must give equal targets
    }
    if (csv) std::fclose(csv);
    for (auto& kv : captured) kv.second->Release();
    std::printf("per-channel figures written to %s\n", csvPath.c_str());
}

C5R_TEST(game_negative_missing_texture_name) {
    gamefs::GameFiles fs;
    const std::string gd = c5r::test::gameDir();
    CHECK_OK(fs.open(widen(gd)));
    Device d;
    CHECK(d.dev != nullptr);
    if (!d.dev) return;
    Synth s;
    s.t.sets[0].diffuse = "no_such_terrain_texture_3402.dds";
    BlendTextureCache cache;
    BlendTextures tex;
    // The EXE's rule (3404): a name the install lacks is handle 0, bound like the dummy, and recorded.
    ID3D11ShaderResourceView* const marker = reinterpret_cast<ID3D11ShaderResourceView*>(&s);  // never dereferenced
    CHECK_OK(cache.bind(d.dev, fs, s.t, list1(0), marker, tex));
    CHECK(tex.srv[0] == marker);
    // the synthetic set's height map "h0.dds" is not in the install either
    CHECK(cache.missing().size() == 2 && cache.missing()[0] == "no_such_terrain_texture_3402.dds" &&
          cache.missing()[1] == "h0.dds");
    // the old rule on request
    cache.setMissingRule(MissingTexture::Fail);
    CHECK_ERR(cache.bind(d.dev, fs, s.t, list1(0), nullptr, tex), Err::NotFound);
    cache.setMissingRule(MissingTexture::AsHandle0);
    // a file that exists but is no DDS stays an error under the EXE's rule
    Synth s2;
    s2.t.sets[0].diffuse = "terraindef.csv";
    BlendTextureCache c2;
    Status bad = c2.bind(d.dev, fs, s2.t, list1(0), nullptr, tex);
    CHECK(!bad.ok() && bad.code != Err::NotFound);
    CHECK(c2.missing().empty());
    LayerList l9;
    l9.count = 9;
    CHECK_ERR(cache.bind(d.dev, fs, s.t, l9, nullptr, tex), Err::BadLayout);
    CHECK_ERR(cache.bind(nullptr, fs, s.t, list1(0), nullptr, tex), Err::BadLayout);
}

C5R_TEST(ref_kernel_equals_the_live_table) {
    for (const std::string& root : refRoots()) ref_kernel_equals_the_live_table_one(root);
}

C5R_TEST(ref_vertex_buffers_byte_equal) {
    for (const std::string& root : refRoots()) ref_vertex_buffers_byte_equal_one(root);
}

C5R_TEST(ref_index_buffers_equal) {
    for (const std::string& root : refRoots()) ref_index_buffers_equal_one(root);
}

C5R_TEST(ref_constant_blocks_equal_where_read) {
    for (const std::string& root : refRoots()) ref_constant_blocks_equal_where_read_one(root);
}

C5R_TEST(ref_blend_targets_server_vs_capture_replay) {
    for (const std::string& root : refRoots()) ref_blend_targets_server_vs_capture_replay_one(root);
}

C5R_TEST_MAIN
