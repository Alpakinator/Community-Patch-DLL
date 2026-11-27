// c5r::bake pipeline and the bakecompare tool (issue 3404).
//
// Synthetic tests need nothing. The game tests need C5R_GAME_DIR (TerrainDef.csv, layer textures, style cubes, LUT) and
// C5R_EXE (the EXE's shaders); without them they skip, which fails the run. The reference test reads 3401's reference set
// (C5R_BAKE_REFSET, default D:/c5-64bit-work/3400/ref/C5H-3401-TINY_0008; game-derived, on D) and fails with the path
// when it is missing. bakecompare is run as a product: every exit code with its own input.
#include <d3d11.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <DirectXPackedVector.h>

#include "c5r/bake/pipeline.h"
#include "c5r/bake/heightpage.h"   // ticket 0714: heightPageCount
#include "c5r/decal/decal.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/shaders/shadersection.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::bake;

namespace {

// ---------------------------------------------------------------- fixtures

heightfield::HeightGrid synthGrid(uint32_t plotsX = 4, uint32_t plotsY = 4, uint8_t material = 0) {
    heightfield::HeightGrid g;
    g.mapW = plotsX;
    g.mapH = plotsY;
    g.samplesX = plotsX * 64;
    g.samplesY = plotsY * 64;
    g.wrapX = true;
    g.h.resize(static_cast<size_t>(g.samplesX) * g.samplesY);
    for (uint32_t y = 0; y < g.samplesY; ++y)
        for (uint32_t x = 0; x < g.samplesX; ++x) {
            // gentle hills, all land
            const double v = 12000 + 6000 * std::sin(x * 0.07) * std::cos(y * 0.05);
            g.h[g.index(x, y)] = static_cast<uint16_t>(v);
        }
    g.mat.assign(g.h.size(), material);
    return g;
}

TileWindow windowOf(const heightfield::HeightGrid& g, uint32_t x, uint32_t y) {
    TileWindow w;
    TileId t;
    t.x = x;
    t.y = y;
    Status s = tileWindow(g, t, w);
    if (!s.ok()) c5r::test::fail(__FILE__, __LINE__, "tileWindow: " + s.str());
    return w;
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

// The install and the EXE, opened once per test that needs them.
struct Game {
    gamefs::GameFiles fs;
    shaders::ShaderSection exe;
    LayerTables tables;
    bool ok = false;
    Game() {
        const std::string gd = c5r::test::gameDir(), ep = c5r::test::exePath();
        if (gd.empty() || ep.empty()) c5r::test::skip("C5R_GAME_DIR / C5R_EXE not set");
        Status s = fs.open(gamefs::fromUtf8(gd));
        if (s.ok()) s = exe.open(gamefs::fromUtf8(ep));
        if (s.ok()) s = loadTerrainDef(fs, tables);
        if (!s.ok()) {
            c5r::test::fail(__FILE__, __LINE__, "game inputs: " + s.str());
            return;
        }
        ok = true;
    }
};

bool readBc1(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* tex, std::vector<uint8_t>& out) {
    out.clear();
    if (!tex) return false;
    D3D11_TEXTURE2D_DESC d;
    tex->GetDesc(&d);
    D3D11_TEXTURE2D_DESC s = d;
    s.Usage = D3D11_USAGE_STAGING;
    s.BindFlags = 0;
    s.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    s.MiscFlags = 0;
    ID3D11Texture2D* st = nullptr;
    if (FAILED(dev->CreateTexture2D(&s, nullptr, &st))) return false;
    ctx->CopyResource(st, tex);
    for (uint32_t m = 0; m < d.MipLevels; ++m) {
        D3D11_MAPPED_SUBRESOURCE map;
        if (FAILED(ctx->Map(st, m, D3D11_MAP_READ, 0, &map))) {
            st->Release();
            return false;
        }
        const uint32_t blocks = std::max(1u, (d.Width >> m) / 4);
        for (uint32_t r = 0; r < blocks; ++r) {
            const uint8_t* row = static_cast<const uint8_t*>(map.pData) + static_cast<size_t>(r) * map.RowPitch;
            out.insert(out.end(), row, row + blocks * 8);
        }
        ctx->Unmap(st, m);
    }
    st->Release();
    return true;
}

// ---------------------------------------------------------------- the tool as a product

std::string toolPath() {
#ifdef C5R_BAKECOMPARE
    return C5R_BAKECOMPARE;
#else
    return std::string();
#endif
}

std::string scratch(const std::string& name) {
    std::string d = c5r::test::workDir() + "/bake";
    CreateDirectoryA(c5r::test::workDir().c_str(), nullptr);
    CreateDirectoryA(d.c_str(), nullptr);
    return d + "/" + name;
}

void mkdirs(const std::string& p) {
    std::string cur;
    for (char c : p) {
        cur += c;
        if ((c == '/' || c == '\\') && cur.size() > 3) CreateDirectoryA(cur.c_str(), nullptr);
    }
    CreateDirectoryA(p.c_str(), nullptr);
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
    WaitForSingleObject(pi.hProcess, 600000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    output = readText(out);
    return static_cast<int>(code);
}

std::string q(const std::string& s) { return "\"" + s + "\""; }

// One raw_index.json record (the fields bakecompare reads).
std::string record(const std::string& file, uint32_t cache, uint32_t x, uint32_t y, uint32_t tilesY, uint32_t f2fc,
                   const std::vector<uint32_t>& layers, uint32_t bytes = 43680, bool withSize = true) {
    std::string L;
    for (size_t i = 0; i < layers.size(); ++i) L += (i ? "," : "") + std::to_string(layers[i]);
    return "{\"file\":\"" + file + "\",\"cache\":" + std::to_string(cache) + ",\"tileIndex\":" +
           std::to_string(x * tilesY + y) + ",\"tileX\":" + std::to_string(x) + ",\"tileY\":" + std::to_string(y) +
           ",\"w\":256,\"h\":256,\"mips\":6,\"fmt\":70,\"bytes\":" + std::to_string(bytes) + ",\"tile\":{" +
           (withSize ? "\"size_17c\":256," : "") + "\"f2fc\":" + std::to_string(f2fc) + ",\"s310\":1,\"layers\":[" + L + "]}}";
}

// A reference folder in the shape of 3401's: manifest.json, armB/caches/raw_index.json, armB/heightfield.c5hf.
struct SynthRef {
    std::string dir;
    heightfield::HeightGrid g;
    explicit SynthRef(const std::string& name) : dir(scratch(name)), g(synthGrid()) {
        mkdirs(dir + "/armB/caches");
        // start from an empty folder: files of an earlier run must not answer for missing ones
        for (const char* f : {"/equal_between_sessions.json", "/decoded_s.json", "/armB/caches/raw_index.json"})
            DeleteFileA((dir + f).c_str());
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((dir + "/armB/caches/*.bin").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do DeleteFileA((dir + "/armB/caches/" + fd.cFileName).c_str());
            while (FindNextFileA(h, &fd));
            FindClose(h);
        }
        writeText(dir + "/manifest.json", "{\"save\":\"SYNTH\",\"arms\":{\"armB\":{\"dll\":\"none\",\"config\":\"synthetic\"}}}");
        std::vector<uint8_t> tap;
        Status s = heightfield::writeTap(g, tap);
        if (!s.ok()) c5r::test::fail(__FILE__, __LINE__, "writeTap: " + s.str());
        writeBytes(dir + "/armB/heightfield.c5hf", tap);
    }
    void index(const std::vector<std::string>& recs) {
        std::string s = "{\"textures\":[";
        for (size_t i = 0; i < recs.size(); ++i) s += (i ? "," : "") + recs[i];
        writeText(dir + "/armB/caches/raw_index.json", s + "]}");
    }
};

std::string gameArgs() { return " --game-dir " + q(c5r::test::gameDir()) + " --exe " + q(c5r::test::exePath()); }

}  // namespace

// ---------------------------------------------------------------- CPU helpers (stand-ins)

C5R_TEST(vertex_heights_wrap_like_the_blend_fill) {
    heightfield::HeightGrid g = synthGrid();
    std::vector<float> z;
    CHECK_OK(gridVertexHeights(g, windowOf(g, 0, 0), z));
    CHECK_EQ(z.size(), size_t(67 * 67));
    // vertex (0, 0) of tile (0, 0) stands on sample (-1, -1): unsigned wrap to (samplesX - 1, samplesY - 1)
    const float want = (static_cast<float>(g.h[g.index(g.samplesX - 1, g.samplesY - 1)]) * (1.0f / 255.0f)) * g.sz;
    CHECK_EQ(z[0], want);
    const float want2 = (static_cast<float>(g.h[g.index(5, 7)]) * (1.0f / 255.0f)) * g.sz;
    CHECK_EQ(z[8 * 67 + 6], want2);  // vertex (6, 8) = sample (5, 7)
}

C5R_TEST(approx_normals_flat_and_ramp) {
    heightfield::HeightGrid g = synthGrid();
    std::fill(g.h.begin(), g.h.end(), static_cast<uint16_t>(5000));
    std::vector<float> n;
    CHECK_OK(approxGridNormals(g, windowOf(g, 1, 1), n));
    CHECK_EQ(n.size(), size_t(67 * 67 * 3));
    CHECK(n[0] == 0.0f && n[1] == 0.0f && n[2] == 1.0f);
    // a ramp in x: h16 = 255 * x, so z = 0.2 per sample, dz/dx = 0.2: n = (-0.2, 0, 1) / |.|
    for (uint32_t y = 0; y < g.samplesY; ++y)
        for (uint32_t x = 0; x < g.samplesX; ++x) g.h[g.index(x, y)] = static_cast<uint16_t>(255 * x);
    CHECK_OK(approxGridNormals(g, windowOf(g, 1, 1), n));
    const size_t c = (30 * 67 + 30) * 3;
    const double len = std::sqrt(0.04 + 1.0);
    CHECK(std::fabs(n[c] - (-0.2 / len)) < 1e-5 && std::fabs(n[c + 1]) < 1e-6 && std::fabs(n[c + 2] - 1.0 / len) < 1e-5);
}

C5R_TEST(fitted_height_page_follows_the_rule) {
    heightfield::HeightGrid g = synthGrid();
    std::vector<uint8_t> page;
    int32_t ox = 0, oy = 0;
    const TileWindow w = windowOf(g, 2, 1);
    CHECK_OK(fittedHeightPage(g, w, page, ox, oy));
    CHECK_EQ(page.size(), size_t(1024 * 1024));
    CHECK_EQ(ox, 512 - 128 - 32);
    CHECK_EQ(oy, -(64 + 544));
    // every sample of the tile: texel (x + ox, (-oy - y) mod 1024) = round(h16 / 255)
    size_t bad = 0;
    for (uint32_t y = w.oy; y < w.oy + w.ey; ++y)
        for (uint32_t x = w.ox; x < w.ox + w.ex; ++x) {
            const int32_t col = static_cast<int32_t>(x) + ox, row = ((-oy - static_cast<int32_t>(y)) % 1024 + 1024) % 1024;
            const uint32_t want = std::min(255u, (g.h[g.index(x, y)] + 127u) / 255u);
            if (page[static_cast<size_t>(row) * 1024 + col] != want) ++bad;
        }
    CHECK_EQ(bad, size_t(0));
}

C5R_TEST(negative_cpu_helpers) {
    heightfield::HeightGrid g = synthGrid();
    TileWindow w = windowOf(g, 1, 1);
    heightfield::HeightGrid shortG = g;
    shortG.h.resize(10);
    std::vector<float> z(5, 1.0f);
    CHECK_ERR(gridVertexHeights(shortG, w, z), Err::BadLayout);
    CHECK(z.empty());
    std::vector<float> n(5, 1.0f);
    CHECK_ERR(approxGridNormals(shortG, w, n), Err::BadLayout);
    CHECK(n.empty());
    TileWindow zero = w;
    zero.ex = 0;
    CHECK_ERR(gridVertexHeights(g, zero, z), Err::BadLayout);
    TileWindow big = w;
    big.ey = 70;
    CHECK_ERR(approxGridNormals(g, big, n), Err::BadLayout);
    std::vector<uint8_t> page(3, 1);
    int32_t ox = 0, oy = 0;
    CHECK_ERR(fittedHeightPage(shortG, w, page, ox, oy), Err::BadLayout);
    CHECK(page.empty());
    TileWindow outside = w;
    outside.ox = g.samplesX;
    CHECK_ERR(fittedHeightPage(g, outside, page, ox, oy), Err::BadLayout);
    CHECK(page.empty());
}

C5R_TEST(stand_in_sources_name_themselves) {
    CHECK(std::string(makeApproxGridNormals()->name()).find("stand-in") == 0);
    CHECK(std::string(makeFlatSlopeClasses()->name()).find("stand-in") == 0);
    CHECK(std::string(makeFittedHeightPages()->name()).find("stand-in") == 0);
    CHECK(std::string(makeCaptureConstants()->name()).find("stand-in") == 0);
    heightfield::HeightGrid g = synthGrid();
    SlopeClasses cls(5, 1);
    CHECK_OK(makeFlatSlopeClasses()->classes(g, windowOf(g, 1, 1), cls));
    CHECK(cls.empty());
    PostConstants k;
    k.halfVector[0] = 9;
    CHECK_OK(makeCaptureConstants()->constants(TileId(), k));
    CHECK_EQ(k.halfVector[0], 0.0f);
    HeightPageView hp;
    CHECK_ERR(makeFittedHeightPages()->page(nullptr, nullptr, g, windowOf(g, 1, 1), hp), Err::BadLayout);
    CHECK(hp.srv == nullptr);
}

// ---------------------------------------------------------------- the baker, order and input checks

C5R_TEST(negative_baker_without_init_or_inputs) {
    PipelineConfig none;
    PipelineBaker b(none);
    BakedTile out;
    BakeRequest req;
    CHECK_ERR(b.bake(req, out), Err::NotFound);  // before init
    CHECK(out.t0 == nullptr && out.t8 == nullptr);
    PipelineBaker::Overrides bad;
    bad.needSecond = 2;
    CHECK_ERR(b.bakeWith(req, bad, out), Err::BadLayout);
    bad.needSecond = -1;
    bad.style = 5;
    CHECK_ERR(b.bakeWith(req, bad, out), Err::BadLayout);
    bad.style = -2;
    CHECK_ERR(b.bakeWith(req, bad, out), Err::BadLayout);
    MapInputs mi;
    CHECK_ERR(b.setMap(mi), Err::NotFound);      // before init
    Device d;
    CHECK(d.dev != nullptr);
    CHECK_ERR(b.init(nullptr, d.ctx), Err::BadLayout);
    CHECK_ERR(b.init(d.dev, nullptr), Err::BadLayout);
    CHECK_ERR(b.init(d.dev, d.ctx), Err::BadLayout);  // no install view
    gamefs::GameFiles fs;
    PipelineConfig onlyFs;
    onlyFs.fs = &fs;
    PipelineBaker b2(onlyFs);
    CHECK_ERR(b2.init(d.dev, d.ctx), Err::BadLayout);  // no shader section
    shaders::ShaderSection empty;
    PipelineConfig both = onlyFs;
    both.exe = &empty;
    both.largestCacheSize = 100;
    PipelineBaker b3(both);
    CHECK_ERR(b3.init(d.dev, d.ctx), Err::BadLayout);  // largest cache size
    both.largestCacheSize = 512;
    PipelineBaker b4(both);
    CHECK_ERR(b4.init(d.dev, d.ctx), Err::NotFound);   // an empty section has no blend shaders
    CHECK_ERR(b4.bake(req, out), Err::NotFound);
}

C5R_TEST(game_negative_map_and_request_checks) {
    Game game;
    if (!game.ok) return;
    Device d;
    PipelineConfig cfg;
    cfg.fs = &game.fs;
    cfg.exe = &game.exe;
    PipelineBaker b(cfg);
    CHECK_OK(b.init(d.dev, d.ctx));
    BakedTile out;
    BakeRequest req;
    CHECK_ERR(b.bake(req, out), Err::NotFound);        // before setMap
    heightfield::HeightGrid g = synthGrid();
    MapInputs mi;
    CHECK_ERR(b.setMap(mi), Err::BadLayout);           // no grid, no tables
    mi.grid = &g;
    CHECK_ERR(b.setMap(mi), Err::BadLayout);           // no tables
    mi.tables = &game.tables;
    heightfield::HeightGrid noMat = g;
    noMat.mat.clear();
    mi.grid = &noMat;
    CHECK_ERR(b.setMap(mi), Err::NotFound);            // material bytes missing
    mi.grid = &g;
    mi.style = 5;
    CHECK_ERR(b.setMap(mi), Err::BadLayout);           // style
    CHECK_ERR(b.bake(req, out), Err::NotFound);        // the failed setMap dropped the map
    LayerTables noLayers;
    mi.style = 0;
    mi.tables = &noLayers;
    CHECK_ERR(b.setMap(mi), Err::BadLayout);           // layer count 0
    mi.tables = &game.tables;
    CHECK_OK(b.setMap(mi));
    req.sizeClass = 100;
    CHECK_ERR(b.bake(req, out), Err::BadLayout);       // size class
    req.sizeClass = 256;
    req.tile.x = 4;
    CHECK_ERR(b.bake(req, out), Err::NotFound);        // outside the map
    req.tile.x = 0;
    req.tile.y = 9;
    CHECK_ERR(b.bake(req, out), Err::NotFound);
    CHECK(out.t0 == nullptr && out.t8 == nullptr);
    heightfield::HeightGrid orphan = g;
    orphan.mat[orphan.index(70, 70)] = 200;           // a material byte without a texture set
    mi.grid = &orphan;
    CHECK_OK(b.setMap(mi));
    req.tile.x = 1;
    req.tile.y = 1;
    CHECK_ERR(b.bake(req, out), Err::NotFound);
    CHECK(out.t0 == nullptr);
}

namespace {
class BadNormals : public GridNormalSource {
public:
    const char* name() const override { return "test: wrong count"; }
    Status normals(const heightfield::HeightGrid&, const TileWindow&, std::vector<float>& out) override {
        out.assign(30, 0.0f);
        return Status::Ok();
    }
};
class FailingSlopes : public SlopeClassSource {
public:
    const char* name() const override { return "test: fails"; }
    Status classes(const heightfield::HeightGrid&, const TileWindow&, SlopeClasses&) override {
        return Status::fail(Err::Unsupported, "test slope source");
    }
};
class BadClasses : public SlopeClassSource {
public:
    const char* name() const override { return "test: class 3"; }
    Status classes(const heightfield::HeightGrid&, const TileWindow& w, SlopeClasses& out) override {
        out.assign(static_cast<size_t>(w.ex + 2) * (w.ey + 2), 3);
        return Status::Ok();
    }
};
class Style7 : public StyleSource {
public:
    const char* name() const override { return "test: style 7"; }
    uint32_t style(TileId) override { return 7; }
};
}  // namespace

C5R_TEST(game_negative_sources) {
    Game game;
    if (!game.ok) return;
    Device d;
    heightfield::HeightGrid g = synthGrid();
    MapInputs mi;
    mi.grid = &g;
    mi.tables = &game.tables;
    BakeRequest req;
    req.tile.x = 1;
    req.tile.y = 1;
    BakedTile out;
    BadNormals bn;
    FailingSlopes fsl;
    BadClasses bc;
    Style7 s7;
    struct Case { GridNormalSource* n; SlopeClassSource* s; StyleSource* st; Err want; };
    const Case cases[] = {{&bn, nullptr, nullptr, Err::BadLayout}, {nullptr, &fsl, nullptr, Err::Unsupported},
                          {nullptr, &bc, nullptr, Err::BadLayout}, {nullptr, nullptr, &s7, Err::BadLayout}};
    for (const Case& c : cases) {
        PipelineConfig cfg;
        cfg.fs = &game.fs;
        cfg.exe = &game.exe;
        cfg.normals = c.n;
        cfg.slopes = c.s;
        cfg.styles = c.st;
        PipelineBaker b(cfg);
        CHECK_OK(b.init(d.dev, d.ctx));
        CHECK_OK(b.setMap(mi));
        CHECK_ERR(b.bake(req, out), c.want);
        CHECK(out.t0 == nullptr && out.t8 == nullptr);
    }
}

// ---------------------------------------------------------------- the baked tile (game files and the EXE's shaders)

C5R_TEST(game_bake_every_size_class_and_edge_tiles_deterministic) {
    Game game;
    if (!game.ok) return;
    Device d;
    PipelineConfig cfg;
    cfg.fs = &game.fs;
    cfg.exe = &game.exe;
    PipelineBaker b(cfg);
    CHECK_OK(b.init(d.dev, d.ctx));
    heightfield::HeightGrid g = synthGrid();
    // material 0 on the left half, 1 on the right: a two-layer tile at x = 1
    for (uint32_t y = 0; y < g.samplesY; ++y)
        for (uint32_t x = 100; x < g.samplesX; ++x) g.mat[g.index(x, y)] = 1;
    MapInputs mi;
    mi.grid = &g;
    mi.tables = &game.tables;
    CHECK_OK(b.setMap(mi));
    const uint32_t classes[4] = {64, 128, 256, 512};
    for (uint32_t sc : classes) {
        BakeRequest req;
        req.tile.x = 1;
        req.tile.y = 1;
        req.sizeClass = sc;
        req.contentGeneration = 42;
        BakedTile out;
        CHECK_OK(b.bake(req, out));
        CHECK(out.t0 != nullptr);
        CHECK_EQ(out.sizeClass, sc);
        CHECK_EQ(out.contentGeneration, uint64_t(42));
        const PipelineTileInfo& info = b.lastTile();
        CHECK_EQ(info.list.count, 2u);
        CHECK_EQ(out.t8 != nullptr, info.needSecond);
        CHECK_EQ(out.hasT8, info.needSecond);
        CHECK(out.gpuMs > 0.0f);
        // the defaults are the EXE's rules of 3405 / 3406
        CHECK(info.normals.find("3405") == 0 && info.slopes.find("3405") == 0);
        CHECK(info.heightPage.find("3406") == 0 && info.constants.find("3406") == 0);
        CHECK_EQ(info.styleSource, std::string("MapInputs::style"));
        if (out.t0) {
            D3D11_TEXTURE2D_DESC dd;
            out.t0->GetDesc(&dd);
            CHECK(dd.Width == sc && dd.Height == sc && dd.MipLevels == cacheMips(sc) && dd.Format == DXGI_FORMAT_BC1_TYPELESS);
        }
        std::vector<uint8_t> a, a8;
        CHECK(readBc1(d.dev, d.ctx, out.t0, a));
        if (out.t8) CHECK(readBc1(d.dev, d.ctx, out.t8, a8));
        if (out.t0) out.t0->Release();
        if (out.t8) out.t8->Release();
        // the same bake again: byte-identical (deterministic on one device)
        BakedTile again;
        CHECK_OK(b.bake(req, again));
        std::vector<uint8_t> c, c8;
        CHECK(readBc1(d.dev, d.ctx, again.t0, c));
        if (again.t8) CHECK(readBc1(d.dev, d.ctx, again.t8, c8));
        CHECK(a == c && a8 == c8);
        if (again.t0) again.t0->Release();
        if (again.t8) again.t8->Release();
    }
    // overrides: t8 forced on and off (the EXE's tile+0x300 case), a style other than the map's
    for (int ns = 0; ns < 2; ++ns) {
        BakeRequest req;
        req.tile.x = 1;
        req.tile.y = 2;
        PipelineBaker::Overrides ov;
        ov.needSecond = ns;
        ov.style = 3;
        BakedTile o2;
        CHECK_OK(b.bakeWith(req, ov, o2));
        CHECK_EQ(o2.t8 != nullptr, ns == 1);
        CHECK_EQ(b.lastTile().style, 3u);
        CHECK_EQ(b.lastTile().needSecond, ns == 1);
        if (o2.t0) o2.t0->Release();
        if (o2.t8) o2.t8->Release();
    }
    // the last column and row (extent 64: a 66 x 66 blend grid) bake too
    BakeRequest edge;
    edge.tile.x = 3;
    edge.tile.y = 3;
    BakedTile eo;
    CHECK_OK(b.bake(edge, eo));
    CHECK_EQ(b.lastTile().window.ex, 64u);
    CHECK(eo.t0 != nullptr);
    if (eo.t0) eo.t0->Release();
    if (eo.t8) eo.t8->Release();
    CHECK(b.texturesLoaded() > 0);
}

// The contract of wave 3 (baker.h): areaChanged, array targets, hasT8, per-tile style from the plots' art types.
C5R_TEST(game_contract_targets_area_changed_and_plot_style) {
    Game game;
    if (!game.ok) return;
    Device d;
    PipelineConfig cfg;
    cfg.fs = &game.fs;
    cfg.exe = &game.exe;
    PipelineBaker b(cfg);
    CHECK_OK(b.init(d.dev, d.ctx));
    heightfield::HeightGrid g = synthGrid();
    MapInputs mi;
    mi.grid = &g;
    mi.tables = &game.tables;
    // plot art types: plotsX must be the grid's tiles in x, plotsY 1..mapH
    std::vector<uint8_t> art(4 * 4, 3);  // every plot Africa
    mi.plotArtTypes = art.data();
    mi.plotsX = 3;
    mi.plotsY = 4;
    CHECK_ERR(b.setMap(mi), Err::BadLayout);
    mi.plotsX = 4;
    mi.plotsY = 5;
    CHECK_ERR(b.setMap(mi), Err::BadLayout);
    mi.plotsY = 0;
    CHECK_ERR(b.setMap(mi), Err::BadLayout);
    mi.plotsY = 4;
    CHECK_OK(b.setMap(mi));
    BakeRequest req;
    req.tile.x = 1;
    req.tile.y = 3;
    BakedTile plain;
    CHECK_OK(b.bake(req, plain));
    CHECK_EQ(b.lastTile().style, 3u);  // tileStyle over plot rows 1 and 2
    CHECK_EQ(b.lastTile().styleSource, std::string("MapInputs::plotArtTypes (tileStyle)"));
    const bool byList = b.lastTile().needSecondByList;
    std::vector<uint8_t> p0, p8;
    CHECK(readBc1(d.dev, d.ctx, plain.t0, p0));
    if (plain.t8) CHECK(readBc1(d.dev, d.ctx, plain.t8, p8));
    if (plain.t0) plain.t0->Release();
    if (plain.t8) plain.t8->Release();
    // areaChanged (tile+0x300): t8 always
    BakeRequest area = req;
    area.areaChanged = true;
    BakedTile ao;
    CHECK_OK(b.bake(area, ao));
    CHECK(ao.hasT8 && ao.t8 != nullptr && b.lastTile().needSecond);
    if (!byList) CHECK(!plain.hasT8);
    if (ao.t0) ao.t0->Release();
    if (ao.t8) ao.t8->Release();
    // into slices of a BC1 array: no texture returned, the slice holds what the plain bake returned
    D3D11_TEXTURE2D_DESC ad = {};
    ad.Width = ad.Height = 256;
    ad.MipLevels = 6;
    ad.ArraySize = 3;
    ad.Format = DXGI_FORMAT_BC1_TYPELESS;
    ad.SampleDesc.Count = 1;
    ad.Usage = D3D11_USAGE_DEFAULT;
    ad.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* arr = nullptr;
    CHECK(SUCCEEDED(d.dev->CreateTexture2D(&ad, nullptr, &arr)));
    if (!arr) return;
    BakeRequest sl = req;
    sl.areaChanged = !p8.empty();
    sl.t0Target.array = arr;
    sl.t0Target.slice = 1;
    sl.t8Target.array = arr;
    sl.t8Target.slice = 2;
    BakedTile so;
    CHECK_OK(b.bake(sl, so));
    CHECK(so.t0 == nullptr && so.t8 == nullptr);
    CHECK_EQ(so.hasT8, !p8.empty());
    // read the slices back through a staging copy of the array
    D3D11_TEXTURE2D_DESC sd = ad;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* st = nullptr;
    CHECK(SUCCEEDED(d.dev->CreateTexture2D(&sd, nullptr, &st)));
    if (st) {
        d.ctx->CopyResource(st, arr);
        for (uint32_t slice = 1; slice <= 2; ++slice) {
            std::vector<uint8_t> got;
            for (uint32_t m = 0; m < 6; ++m) {
                D3D11_MAPPED_SUBRESOURCE map;
                if (FAILED(d.ctx->Map(st, D3D11CalcSubresource(m, slice, 6), D3D11_MAP_READ, 0, &map))) break;
                const uint32_t blocks = std::max(1u, (256u >> m) / 4);
                for (uint32_t r = 0; r < blocks; ++r) {
                    const uint8_t* row = static_cast<const uint8_t*>(map.pData) + static_cast<size_t>(r) * map.RowPitch;
                    got.insert(got.end(), row, row + blocks * 8);
                }
                d.ctx->Unmap(st, D3D11CalcSubresource(m, slice, 6));
            }
            if (slice == 1) CHECK(got == p0);
            if (slice == 2 && !p8.empty()) CHECK(got == p8);
        }
        st->Release();
    }
    // bad targets: not BC1, wrong mips, slice past the array
    D3D11_TEXTURE2D_DESC bd = ad;
    bd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    ID3D11Texture2D* rgba = nullptr;
    CHECK(SUCCEEDED(d.dev->CreateTexture2D(&bd, nullptr, &rgba)));
    bd = ad;
    bd.MipLevels = 5;
    ID3D11Texture2D* fiveMips = nullptr;
    CHECK(SUCCEEDED(d.dev->CreateTexture2D(&bd, nullptr, &fiveMips)));
    BakeRequest bad = req;
    BakedTile bo;
    bad.t0Target.array = rgba;
    CHECK_ERR(b.bake(bad, bo), Err::BadLayout);
    bad.t0Target.array = fiveMips;
    CHECK_ERR(b.bake(bad, bo), Err::BadLayout);
    bad.t0Target.array = arr;
    bad.t0Target.slice = 3;
    CHECK_ERR(b.bake(bad, bo), Err::BadLayout);
    bad.t0Target.slice = 0;
    bad.sizeClass = 128;  // the array is 256
    CHECK_ERR(b.bake(bad, bo), Err::BadLayout);
    CHECK(bo.t0 == nullptr && bo.t8 == nullptr && !bo.hasT8);
    if (rgba) rgba->Release();
    if (fiveMips) fiveMips->Release();
    arr->Release();
}

// A texture name the install lacks follows the EXE's rule (handle 0, bound like the dummy), not a failure: every layer
// of every set of the install's TerrainDef.csv binds, and the names recorded as missing are exactly those gamefs lacks.
C5R_TEST(game_every_layer_of_every_set_binds) {
    Game game;
    if (!game.ok) return;
    Device d;
    PipelineConfig cfg;
    cfg.fs = &game.fs;
    cfg.exe = &game.exe;
    PipelineBaker b(cfg);
    CHECK_OK(b.init(d.dev, d.ctx));
    BlendTextureCache cache;
    std::vector<std::string> expect;
    auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    for (uint32_t id = 0; id < game.tables.layerCount; ++id) {
        LayerList l;
        l.count = 1;
        l.ids[0] = id;
        BlendTextures tex;
        Status st = cache.bind(d.dev, game.fs, game.tables, l, nullptr, tex);
        if (!st.ok()) c5r::test::fail(__FILE__, __LINE__, "layer " + std::to_string(id) + ": " + st.str());
        const TextureSet& s = game.tables.sets[game.tables.setOfLayer[id]];
        for (const std::string& n : {s.diffuse, s.heightmap, s.specmap})
            if (!n.empty() && n != "NULL" && !game.fs.exists(n) &&
                std::find(expect.begin(), expect.end(), lower(n)) == expect.end())
                expect.push_back(lower(n));
    }
    std::vector<std::string> got = cache.missing();
    std::sort(got.begin(), got.end());
    std::sort(expect.begin(), expect.end());
    for (const std::string& n : got) std::printf("  missing in the install, bound as handle 0: %s\n", n.c_str());
    CHECK(got == expect);
    std::printf("  %u layers of %zu sets bind; %zu name(s) missing\n", game.tables.layerCount, game.tables.sets.size(),
                got.size());
    // the old rule still fails on them
    if (!expect.empty()) {
        BlendTextureCache strict;
        strict.setMissingRule(MissingTexture::Fail);
        bool failed = false;
        for (uint32_t id = 0; id < game.tables.layerCount && !failed; ++id) {
            LayerList l;
            l.count = 1;
            l.ids[0] = id;
            BlendTextures tex;
            failed = strict.bind(d.dev, game.fs, game.tables, l, nullptr, tex).code == Err::NotFound;
        }
        CHECK(failed);
    }
}

// The four huge tiles that failed in 3407 / 3408 (Gibraltar, set 8, height map missing): a tile of that material bakes.
C5R_TEST(game_gibraltar_tile_bakes) {
    Game game;
    if (!game.ok) return;
    Device d;
    PipelineConfig cfg;
    cfg.fs = &game.fs;
    cfg.exe = &game.exe;
    PipelineBaker b(cfg);
    CHECK_OK(b.init(d.dev, d.ctx));
    heightfield::HeightGrid g = synthGrid();
    for (uint32_t y = 60; y < 140; ++y)
        for (uint32_t x = 60; x < 140; ++x) g.mat[g.index(x, y)] = 8;  // Rock of Gibraltar's set (slope-projected)
    MapInputs mi;
    mi.grid = &g;
    mi.tables = &game.tables;
    CHECK_OK(b.setMap(mi));
    BakeRequest req;
    req.tile.x = 1;
    req.tile.y = 1;
    BakedTile o;
    CHECK_OK(b.bake(req, o));
    CHECK(o.t0 != nullptr);
    bool listed = false;
    for (uint32_t i = 0; i < b.lastTile().list.count; ++i) listed |= game.tables.setOfLayer[b.lastTile().list.ids[i]] == 8;
    CHECK(listed);
    if (!game.fs.exists(game.tables.sets[8].heightmap)) CHECK(!b.missingTextures().empty());
    if (o.t0) o.t0->Release();
    if (o.t8) o.t8->Release();
}

// gpuMs: Blocking gives this bake's time; Deferred never waits and reports finished earlier bakes.
C5R_TEST(game_gpu_timing_modes) {
    Game game;
    if (!game.ok) return;
    Device d;
    heightfield::HeightGrid g = synthGrid();
    MapInputs mi;
    mi.grid = &g;
    mi.tables = &game.tables;
    for (GpuTiming mode : {GpuTiming::Blocking, GpuTiming::Deferred}) {
        PipelineConfig cfg;
        cfg.fs = &game.fs;
        cfg.exe = &game.exe;
        cfg.gpuTiming = mode;
        PipelineBaker b(cfg);
        CHECK_OK(b.init(d.dev, d.ctx));
        CHECK_OK(b.setMap(mi));
        float last = 0;
        int nonZero = 0;
        for (int k = 0; k < 24; ++k) {
            BakeRequest req;
            req.tile.x = static_cast<uint32_t>(k % 3);
            req.tile.y = static_cast<uint32_t>((k / 3) % 3);
            BakedTile o;
            CHECK_OK(b.bake(req, o));
            if (mode == GpuTiming::Blocking) CHECK(o.gpuMs > 0.0f);
            nonZero += o.gpuMs > 0.0f;
            last = o.gpuMs;
            if (o.t0) o.t0->Release();
            if (o.t8) o.t8->Release();
        }
        if (mode == GpuTiming::Deferred) d.ctx->Flush();
        std::printf("  %s: %d of 24 bakes reported a GPU time, last %.3f ms\n",
                    mode == GpuTiming::Blocking ? "blocking" : "deferred", nonZero, last);
        CHECK(nonZero > 0);
    }
}

// The stand-ins stay selectable and bake every tile, the edge tiles included.
C5R_TEST(game_stand_in_sources_bake) {
    Game game;
    if (!game.ok) return;
    Device d;
    std::unique_ptr<GridNormalSource> n = makeApproxGridNormals();
    std::unique_ptr<SlopeClassSource> s = makeFlatSlopeClasses();
    std::unique_ptr<HeightPageSource> hp = makeFittedHeightPages();
    std::unique_ptr<BakeConstantsSource> k = makeCaptureConstants();
    PipelineConfig cfg;
    cfg.fs = &game.fs;
    cfg.exe = &game.exe;
    cfg.normals = n.get();
    cfg.slopes = s.get();
    cfg.heightPages = hp.get();
    cfg.constants = k.get();
    cfg.gpuTiming = GpuTiming::Off;
    PipelineBaker b(cfg);
    CHECK_OK(b.init(d.dev, d.ctx));
    heightfield::HeightGrid g = synthGrid();
    MapInputs mi;
    mi.grid = &g;
    mi.tables = &game.tables;
    CHECK_OK(b.setMap(mi));
    for (uint32_t t = 2; t < 4; ++t) {
        BakeRequest req;
        req.tile.x = t;
        req.tile.y = t;
        BakedTile o;
        CHECK_OK(b.bake(req, o));
        CHECK(o.t0 != nullptr);
        CHECK_EQ(o.gpuMs, 0.0f);  // timing off
        CHECK(b.lastTile().normals.find("stand-in") == 0);
        if (o.t0) o.t0->Release();
        if (o.t8) o.t8->Release();
    }
}

// ---------------------------------------------------------------- bakecompare as a product

C5R_TEST(bakecompare_usage_and_missing_inputs) {
    if (toolPath().empty()) c5r::test::skip("C5R_BAKECOMPARE not set");
    std::string out;
    CHECK_EQ(runTool("", out), 2);
    CHECK(out.find("usage") != std::string::npos);
    CHECK_EQ(runTool("--ref x --game-dir y --exe z --out w --bogus", out), 2);
    CHECK_EQ(runTool("--ref x --game-dir y --exe z --out w --limit -3", out), 2);
    CHECK_EQ(runTool("--ref x --game-dir y --exe z --out w --tiles 1;2", out), 2);
    CHECK_EQ(runTool("--ref x --game-dir y --exe z --out w --t8-rule maybe", out), 2);
    CHECK_EQ(runTool("--ref x --game-dir y --exe z --out w --sources maybe", out), 2);
    CHECK_EQ(runTool("--ref x --game-dir y --exe z --out w --plot-art", out), 2);
    CHECK_EQ(runTool("--ref x --game-dir y --exe z", out), 2);  // --out missing
    const std::string o = " --out " + q(scratch("out-missing"));
    CHECK_EQ(runTool("--ref " + q(scratch("no-such-ref")) + " --game-dir y --exe z" + o, out), 3);
    SynthRef r("ref-missing");
    DeleteFileA((r.dir + "/manifest.json").c_str());
    CHECK_EQ(runTool("--ref " + q(r.dir) + " --game-dir y --exe z" + o, out), 3);         // manifest.json missing
    CHECK(out.find("manifest.json") != std::string::npos);
    writeText(r.dir + "/manifest.json", "{\"save\":\"S\"}");
    CHECK_EQ(runTool("--ref " + q(r.dir) + " --game-dir y --exe z" + o, out), 3);         // raw_index.json missing
    CHECK(out.find("raw_index.json") != std::string::npos);
    r.index({record("t0_00005.bin", 0, 1, 1, 4, 0, {0})});
    DeleteFileA((r.dir + "/armB/heightfield.c5hf").c_str());
    CHECK_EQ(runTool("--ref " + q(r.dir) + " --game-dir y --exe z" + o, out), 3);         // height field missing
    SynthRef r2("ref-missing2");
    r2.index({record("t0_00005.bin", 0, 1, 1, 4, 0, {0})});
    CHECK_EQ(runTool("--ref " + q(r2.dir) + " --game-dir " + q(scratch("no-install")) + " --exe z" + o, out), 3);  // install
    if (!c5r::test::gameDir().empty())
        CHECK_EQ(runTool("--ref " + q(r2.dir) + " --game-dir " + q(c5r::test::gameDir()) + " --exe " +
                         q(scratch("no-exe.exe")) + o, out), 3);                          // EXE missing
}

C5R_TEST(bakecompare_unparsable_and_inconsistent_inputs) {
    if (toolPath().empty()) c5r::test::skip("C5R_BAKECOMPARE not set");
    std::string out;
    const std::string o = " --out " + q(scratch("out-bad")) + " --game-dir y --exe z";
    SynthRef r("ref-bad");
    r.index({record("t0_00005.bin", 0, 1, 1, 4, 0, {0})});
    writeText(r.dir + "/manifest.json", "{\"save\": ");
    CHECK_EQ(runTool("--ref " + q(r.dir) + o, out), 4);                                   // manifest not JSON
    writeText(r.dir + "/manifest.json", "{\"arms\":{}}");
    CHECK_EQ(runTool("--ref " + q(r.dir) + o, out), 4);                                   // manifest without "save"
    writeText(r.dir + "/manifest.json", "{\"save\":\"S\"}");
    writeText(r.dir + "/armB/caches/raw_index.json", "{\"textures\":[1,2,]}");
    CHECK_EQ(runTool("--ref " + q(r.dir) + o, out), 4);                                   // raw index not JSON
    writeText(r.dir + "/armB/caches/raw_index.json", "{\"files\":[]}");
    CHECK_EQ(runTool("--ref " + q(r.dir) + o, out), 4);                                   // no "textures"
    r.index({});
    CHECK_EQ(runTool("--ref " + q(r.dir) + o, out), 5);                                   // no texture listed
    r.index({record("t0_00005.bin", 0, 1, 1, 4, 0, {0})});
    writeText(r.dir + "/equal_between_sessions.json", "[");
    CHECK_EQ(runTool("--ref " + q(r.dir) + o, out), 4);                                   // ceiling file not JSON
    writeText(r.dir + "/equal_between_sessions.json", "{\"caches\":[{\"tile\":1}]}");
    CHECK_EQ(runTool("--ref " + q(r.dir) + o, out), 4);                                   // bad ceiling entry
    DeleteFileA((r.dir + "/equal_between_sessions.json").c_str());
    writeText(r.dir + "/decoded_s.json", "{\"per_texture\":5}");
    CHECK_EQ(runTool("--ref " + q(r.dir) + o, out), 4);                                   // bad decoded file
    DeleteFileA((r.dir + "/decoded_s.json").c_str());
    writeText(r.dir + "/armB/heightfield.c5hf", "not a tap");
    CHECK_EQ(runTool("--ref " + q(r.dir) + o, out), 4);                                   // height field not a tap
    heightfield::HeightGrid noMat = r.g;
    noMat.mat.clear();
    std::vector<uint8_t> tap;
    CHECK_OK(heightfield::writeTap(noMat, tap));
    writeBytes(r.dir + "/armB/heightfield.c5hf", tap);
    CHECK_EQ(runTool("--ref " + q(r.dir) + o, out), 5);                                   // no material bytes
    CHECK_OK(heightfield::writeTap(r.g, tap));
    writeBytes(r.dir + "/armB/heightfield.c5hf", tap);
    r.index({record("t0_00045.bin", 0, 11, 1, 4, 0, {0})});
    CHECK_EQ(runTool("--ref " + q(r.dir) + o, out), 5);                                   // tile beyond the map
    // the plots' continent art types (--plot-art): missing 3, unparsable 4, not fitting the grid 5
    r.index({record("t0_00005.bin", 0, 1, 1, 4, 0, {0})});
    const std::string art = scratch("plot_art.txt");
    DeleteFileA(art.c_str());
    CHECK_EQ(runTool("--ref " + q(r.dir) + o + " --plot-art " + q(art), out), 3);
    writeText(art, "4 2 0,1,2,x,0,0,0,0");
    CHECK_EQ(runTool("--ref " + q(r.dir) + o + " --plot-art " + q(art), out), 4);   // not a number
    writeText(art, "4 2 0,1,2,3");
    CHECK_EQ(runTool("--ref " + q(r.dir) + o + " --plot-art " + q(art), out), 4);   // too few values
    writeText(art, "4 2 0,1,2,300,0,0,0,0");
    CHECK_EQ(runTool("--ref " + q(r.dir) + o + " --plot-art " + q(art), out), 4);   // above 255
    writeText(art, "3 2 0,1,2,3,0,0");
    CHECK_EQ(runTool("--ref " + q(r.dir) + o + " --plot-art " + q(art), out), 5);   // 3 plots wide, the grid 4 tiles
    writeText(art, "4 5 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0");
    CHECK_EQ(runTool("--ref " + q(r.dir) + o + " --plot-art " + q(art), out), 5);   // 5 rows, the grid 4
}

C5R_TEST(game_bakecompare_end_to_end_self_and_skips) {
    if (toolPath().empty()) c5r::test::skip("C5R_BAKECOMPARE not set");
    Game game;
    if (!game.ok) return;
    // The "EXE" caches of tile (1, 1) are the server's own bake: the comparator must find them equal. Tile (2, 1)'s t0 is
    // all zero (differs), its t8 missing on disk, tile (2, 2) has a short file, tile (0, 2) a record without fields.
    SynthRef r("ref-e2e");
    Device d;
    PipelineConfig cfg;
    cfg.fs = &game.fs;
    cfg.exe = &game.exe;
    PipelineBaker b(cfg);
    CHECK_OK(b.init(d.dev, d.ctx));
    MapInputs mi;
    mi.grid = &r.g;
    mi.tables = &game.tables;
    CHECK_OK(b.setMap(mi));
    BakeRequest req;
    req.tile.x = 1;
    req.tile.y = 1;
    BakedTile bt;
    CHECK_OK(b.bake(req, bt));
    std::vector<uint8_t> t0, t8;
    CHECK(readBc1(d.dev, d.ctx, bt.t0, t0));
    const bool second = bt.t8 != nullptr;
    if (second) CHECK(readBc1(d.dev, d.ctx, bt.t8, t8));
    if (bt.t0) bt.t0->Release();
    if (bt.t8) bt.t8->Release();
    const std::vector<uint32_t> list(b.lastTile().list.ids, b.lastTile().list.ids + b.lastTile().list.count);
    CHECK_EQ(t0.size(), size_t(43680));
    writeBytes(r.dir + "/armB/caches/t0_00005.bin", t0);
    std::vector<std::string> recs = {record("t0_00005.bin", 0, 1, 1, 4, second ? 1 : 0, list)};
    if (second) {
        writeBytes(r.dir + "/armB/caches/t8_00005.bin", t8);
        recs.push_back(record("t8_00005.bin", 8, 1, 1, 4, 1, list));
    }
    writeBytes(r.dir + "/armB/caches/t0_00009.bin", std::vector<uint8_t>(43680, 0));
    recs.push_back(record("t0_00009.bin", 0, 2, 1, 4, second ? 1 : 0, list));
    recs.push_back(record("t8_00009.bin", 8, 2, 1, 4, 1, list));                         // file missing
    writeBytes(r.dir + "/armB/caches/t0_00010.bin", std::vector<uint8_t>(100, 0));
    recs.push_back(record("t0_00010.bin", 0, 2, 2, 4, 0, list));                         // short file
    recs.push_back(record("t0_00002.bin", 0, 0, 2, 4, 0, list, 43680, false));           // no size_17c
    r.index(recs);
    const std::string outDir = scratch("out-e2e");
    std::string out;
    const int code = runTool("--ref " + q(r.dir) + gameArgs() + " --out " + q(outDir) + " --diff-images 2 --quiet", out);
    CHECK_EQ(code, 0);
    if (code != 0) std::printf("%s\n", out.c_str());
    const std::string rep = readText(outDir + "/bakecompare_SYNTH.json");
    CHECK(!rep.empty());
    CHECK(rep.find("\"raw file missing or unreadable\":1") != std::string::npos);
    CHECK(rep.find("\"raw file size differs from the record\":1") != std::string::npos);
    CHECK(rep.find("\"record fields missing\":1") != std::string::npos);
    CHECK(rep.find("\"equal\":{\"t0\":1") != std::string::npos);  // tile (1, 1) t0 equal to itself
    CHECK(rep.find("\"ceiling\":null") != std::string::npos);     // no ceiling files in this folder
    const std::string csv = readText(outDir + "/bakecompare_SYNTH.csv");
    CHECK(csv.find("5,1,1,0,\"equal\",1,0,4096,4096,5460,5460,1.0000,0,0,1.0000") != std::string::npos);
    CHECK(csv.find("9,2,1,0,") != std::string::npos);
    // the differing texture got a difference image
    CHECK(GetFileAttributesA((outDir + "/diff/SYNTH_2_1_t0.bmp").c_str()) != INVALID_FILE_ATTRIBUTES);
    // the same with the wave-1 stand-ins and with plot art types (all Generic: the same caches)
    const std::string art = scratch("plot_art_e2e.txt");
    writeText(art, "4 4 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0");
    const std::string outDir2 = scratch("out-e2e-standin");
    CHECK_EQ(runTool("--ref " + q(r.dir) + gameArgs() + " --out " + q(outDir2) + " --sources standin --plot-art " + q(art) +
                         " --quiet", out), 0);
    const std::string rep2 = readText(outDir2 + "/bakecompare_SYNTH.json");
    CHECK(rep2.find("stand-in: 3403") != std::string::npos);
    CHECK(rep2.find("plot continent art types") != std::string::npos);
}

// ---------------------------------------------------------------- the reference set (3401, game-derived, on D)

C5R_TEST(game_bakecompare_reference_sample) {
    if (toolPath().empty()) c5r::test::skip("C5R_BAKECOMPARE not set");
    if (c5r::test::gameDir().empty() || c5r::test::exePath().empty()) c5r::test::skip("C5R_GAME_DIR / C5R_EXE not set");
    const char* e = std::getenv("C5R_BAKE_REFSET");
    const std::string ref = e && *e ? e : "D:/c5-64bit-work/3400/ref/C5H-3401-TINY_0008";
    if (GetFileAttributesA((ref + "/manifest.json").c_str()) == INVALID_FILE_ATTRIBUTES) {
        c5r::test::fail(__FILE__, __LINE__, "C5R_BAKE_REFSET: " + ref + "/manifest.json missing (3401's reference set)");
        return;
    }
    const std::string outDir = scratch("out-ref");
    std::string out;
    const int code = runTool("--ref " + q(ref) + gameArgs() + " --out " + q(outDir) + " --limit 24 --quiet", out);
    std::printf("%s", out.c_str());
    CHECK_EQ(code, 0);
    CHECK(out.find("24 tiles") != std::string::npos);
}

// ---------------------------------------------------------------- decals in the pipeline (epic 0166, issue 4105)

namespace {
// One quad decal (two triangles, uv 0..8) over its rectangle; texture kNoTexture binds the EXE's default texture.
void quadDecal(decal::DecalSet& s, float x0, float y0, float x1, float y1, uint32_t pass) {
    using DirectX::PackedVector::XMConvertFloatToHalf;
    const float p[6][2] = {{0, 0}, {1, 0}, {0, 1}, {0, 1}, {1, 0}, {1, 1}};
    decal::Decal d;
    d.rect[0] = x0; d.rect[1] = y0; d.rect[2] = x1; d.rect[3] = y1;
    d.priority = 0.5f;
    d.pass = pass;
    d.firstVertex = static_cast<uint32_t>(s.vertices.size());
    d.vertexCount = 6;
    d.listOrder = static_cast<uint32_t>(s.decals.size());
    for (const auto& v : p)
        s.vertices.push_back(decal::DecalVertex{XMConvertFloatToHalf(v[0]), XMConvertFloatToHalf(v[1]),
                                                XMConvertFloatToHalf(v[0] * 8), XMConvertFloatToHalf(v[1] * 8)});
    s.decals.push_back(d);
}

struct Caches {
    std::vector<uint8_t> t0, t8;
    bool hasT8 = false;
};

// Bakes and reads back (releases the textures).
Caches bakeRead(Device& d, PipelineBaker& b, const BakeRequest& req) {
    Caches c;
    BakedTile o;
    Status s = b.bake(req, o);
    if (!s.ok()) c5r::test::fail(__FILE__, __LINE__, "bake: " + s.str());
    if (o.t0) readBc1(d.dev, d.ctx, o.t0, c.t0);
    if (o.t8) readBc1(d.dev, d.ctx, o.t8, c.t8);
    c.hasT8 = o.hasT8;
    if (o.t0) o.t0->Release();
    if (o.t8) o.t8->Release();
    return c;
}
}  // namespace

// No decals (null), an empty list, a list whose set is not empty but none of whose decals is the tile's, and decals off:
// the caches are those of the pipeline before 4105, bit for bit. A decal over the tile changes t0.
C5R_TEST(game_decals_null_or_empty_keep_the_bake_bit_for_bit) {
    Game game;
    if (!game.ok) return;
    Device d;
    heightfield::HeightGrid g = synthGrid();
    for (uint32_t y = 0; y < g.samplesY; ++y)
        for (uint32_t x = 100; x < g.samplesX; ++x) g.mat[g.index(x, y)] = 1;
    MapInputs mi;
    mi.grid = &g;
    mi.tables = &game.tables;
    PipelineConfig cfg;
    cfg.fs = &game.fs;
    cfg.exe = &game.exe;
    PipelineBaker b(cfg);
    CHECK_OK(b.init(d.dev, d.ctx));
    CHECK(b.decalPass() != nullptr);
    CHECK_OK(b.decalStatus());
    CHECK_OK(b.setMap(mi));
    PipelineConfig offCfg = cfg;
    offCfg.decals = false;
    PipelineBaker off(offCfg);
    CHECK_OK(off.init(d.dev, d.ctx));
    CHECK_OK(off.setMap(mi));

    decal::DecalSet set;
    set.worldRect[2] = static_cast<float>(g.samplesX);
    set.worldRect[3] = static_cast<float>(g.samplesY);
    quadDecal(set, 70, 70, 120, 110, 0);  // tile (1, 1) only
    decal::buildIndex(set);
    const decal::TileDecalList none = decal::decalsForTile(set, 128, 128);  // tile (2, 2): none of the set's decals
    CHECK(none.set == &set && none.indices.empty());
    const decal::TileDecalList mine = decal::decalsForTile(set, 64, 64);
    CHECK_EQ(mine.indices.size(), size_t(1));
    decal::TileDecalList empty;

    for (uint32_t sc : {64u, 256u, 512u}) {
        for (int area = 0; area < 2; ++area) {
            for (uint32_t t : {1u, 2u}) {
                BakeRequest req;
                req.tile.x = t;
                req.tile.y = t;
                req.sizeClass = sc;
                req.areaChanged = area == 1;
                const Caches base = bakeRead(d, b, req);
                CHECK(!base.t0.empty());
                BakeRequest r2 = req;
                r2.decals = &empty;
                const Caches e = bakeRead(d, b, r2);
                CHECK(e.t0 == base.t0 && e.t8 == base.t8 && e.hasT8 == base.hasT8);
                r2.decals = &none;
                const Caches n = bakeRead(d, b, r2);
                CHECK(n.t0 == base.t0 && n.t8 == base.t8);
                const Caches o = bakeRead(d, off, req);
                CHECK(o.t0 == base.t0 && o.t8 == base.t8 && o.hasT8 == base.hasT8);
            }
        }
    }
    CHECK_EQ(b.decalCounters().draws, uint64_t(0));
    // the tile's decal is drawn: t0 changes, one draw
    BakeRequest req;
    req.tile.x = 1;
    req.tile.y = 1;
    const Caches base = bakeRead(d, b, req);
    BakeRequest withDecal = req;
    withDecal.decals = &mine;
    const Caches dc = bakeRead(d, b, withDecal);
    CHECK(!dc.t0.empty() && dc.t0 != base.t0);
    CHECK_EQ(b.decalCounters().draws, uint64_t(1));
    // and again: deterministic
    const Caches dc2 = bakeRead(d, b, withDecal);
    CHECK(dc2.t0 == dc.t0);
}

// Negative: a decal pass that cannot init (blobs not at the given VAs) fails init when decals are on and leaves no
// pass when they are off; a request with decals then fails BadLayout with no output, one without still bakes. A list
// index past the set fails the bake (the pass's BadLayout, counted).
C5R_TEST(game_negative_decal_pass_init_and_request) {
    Game game;
    if (!game.ok) return;
    Device d;
    decal::DecalShaderVas bogus;
    bogus.vs = 0x10;
    bogus.ps3 = 0x20;
    PipelineConfig cfg;
    cfg.fs = &game.fs;
    cfg.exe = &game.exe;
    cfg.decalVas = &bogus;
    PipelineBaker on(cfg);
    const Status s = on.init(d.dev, d.ctx);
    CHECK_ERR(s, Err::NotFound);
    CHECK(s.msg.find("decal pass") != std::string::npos);
    CHECK(on.decalPass() == nullptr);
    cfg.decals = false;
    PipelineBaker off(cfg);
    CHECK_OK(off.init(d.dev, d.ctx));
    CHECK(off.decalPass() == nullptr);
    CHECK_ERR(off.decalStatus(), Err::NotFound);
    CHECK_EQ(off.decalCounters().draws, uint64_t(0));
    heightfield::HeightGrid g = synthGrid();
    MapInputs mi;
    mi.grid = &g;
    mi.tables = &game.tables;
    CHECK_OK(off.setMap(mi));
    decal::DecalSet set;
    quadDecal(set, 70, 70, 120, 110, 0);
    decal::TileDecalList list = decal::decalsForTile(set, 64, 64);
    CHECK_EQ(list.indices.size(), size_t(1));
    BakeRequest req;
    req.tile.x = 1;
    req.tile.y = 1;
    req.decals = &list;
    BakedTile o;
    CHECK_ERR(off.bake(req, o), Err::BadLayout);
    CHECK(o.t0 == nullptr && o.t8 == nullptr);
    req.decals = nullptr;
    CHECK_OK(off.bake(req, o));
    CHECK(o.t0 != nullptr);
    if (o.t0) o.t0->Release();
    if (o.t8) o.t8->Release();
    PipelineConfig good;
    good.fs = &game.fs;
    good.exe = &game.exe;
    PipelineBaker b(good);
    CHECK_OK(b.init(d.dev, d.ctx));
    CHECK_OK(b.setMap(mi));
    decal::TileDecalList bad = list;
    bad.indices.push_back(7);
    req.decals = &bad;
    BakedTile bo;
    CHECK_ERR(b.bake(req, bo), Err::BadLayout);
    CHECK(bo.t0 == nullptr && bo.t8 == nullptr);
    CHECK(b.decalCounters().badDecals > 0);
}

// ---------------------------------------------------------------- issue 0417: step times, first uses, the warm-up

namespace {
struct LogSink {
    std::vector<std::string> lines;
    static void fn(void* u, const char* l) { static_cast<LogSink*>(u)->lines.push_back(l); }
    size_t count(const std::string& what) const {
        size_t n = 0;
        for (const std::string& l : lines)
            if (l.find(what) != std::string::npos) ++n;
        return n;
    }
};

double stepSum(const PipelineStepTimes& s) {
    return s.prepMs + s.pageMs + s.ownMs + s.texturesMs + s.blendMs + s.normalsMs + s.styleMs + s.constantsMs + s.postMs +
           s.copyMs + s.gpuTimerMs;
}

heightfield::HeightGrid twoMaterialGrid() {
    heightfield::HeightGrid g = synthGrid();
    for (uint32_t y = 0; y < g.samplesY; ++y)
        for (uint32_t x = 100; x < g.samplesX; ++x) g.mat[g.index(x, y)] = 1;
    return g;
}

// The distinct texture names the layers of t use (what warm() binds): diffuse, height map, spec map unless empty / NULL.
size_t textureNames(const LayerTables& t) {
    std::vector<std::string> names;
    for (uint32_t id = 0; id < t.layerCount; ++id) {
        const TextureSet& s = t.sets[t.setOfLayer[id]];
        for (const std::string& n : {s.diffuse, s.heightmap, s.specmap})
            if (!n.empty() && n != "NULL" && std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n);
    }
    return names.size();
}
}  // namespace

// Without the warm-up: the first bake of a tile makes its first uses (layer textures, the height page, the style) and is
// logged with its steps; the same bake again makes none and is not logged. The steps add up to the bake (only the
// inputs' assembly between them is not counted). A slow bake (cfg.slowBakeMs) is logged and counted, at most 32 lines;
// firstUseLogs caps the first-use lines but not the counter; a failed bake is logged with its status.
C5R_TEST(game_step_times_and_first_use_lines) {
    Game game;
    if (!game.ok) return;
    Device d;
    LogSink sink;
    PipelineConfig cfg;
    cfg.fs = &game.fs;
    cfg.exe = &game.exe;
    cfg.log = &LogSink::fn;
    cfg.logUser = &sink;
    cfg.slowBakeMs = 1e9;
    PipelineBaker b(cfg);
    CHECK_OK(b.init(d.dev, d.ctx));
    CHECK(sink.lines.empty());  // no warm-up line: WarmUp::Off is the default
    CHECK(!b.warmReport().ran);
    heightfield::HeightGrid g = twoMaterialGrid();
    MapInputs mi;
    mi.grid = &g;
    mi.tables = &game.tables;
    CHECK_OK(b.setMap(mi));
    BakeRequest req;
    req.tile.x = 1;
    req.tile.y = 1;
    req.sizeClass = 256;
    const Caches c1 = bakeRead(d, b, req);
    CHECK(!c1.t0.empty());
    const PipelineStepTimes s1 = b.lastSteps();
    CHECK(s1.firstUse());
    CHECK(s1.texturesLoaded >= 2);  // two layers: at least their diffuse and height maps
    CHECK(s1.pageBuilt && s1.styleLoaded);
    CHECK(s1.totalMs > 0 && s1.texturesMs > 0 && s1.pageMs > 0 && s1.postMs > 0 && s1.blendMs > 0);
    CHECK(stepSum(s1) <= s1.totalMs + 0.01);
    CHECK(stepSum(s1) >= 0.5 * s1.totalMs);
    std::printf("  first bake: %s\n", s1.str().c_str());
    CHECK_EQ(sink.lines.size(), size_t(1));
    if (!sink.lines.empty()) {
        const std::string& l = sink.lines[0];
        std::printf("  %s\n", l.c_str());
        CHECK(l.find("pipeline: first use: tile (1, 1) size 256, style 0, 2 layers") == 0);
        CHECK(l.find("page ") != std::string::npos && l.find("(built)") != std::string::npos);
        CHECK(l.find("(loaded)") != std::string::npos && l.find("textures ") != std::string::npos);
        // the files it read, each with its size and read + create time (issue 0417: name the files of a slow bake)
        CHECK(l.find("; files ") != std::string::npos && l.find(".dds ") != std::string::npos && l.find(" KB ") != std::string::npos);
    }
    CHECK_EQ(b.firstUseBakes(), uint64_t(1));
    CHECK_EQ(b.slowBakes(), uint64_t(0));
    // the same tile again, and a tile of the same page and layers: nothing new
    for (uint32_t t : {1u, 2u}) {
        req.tile.x = req.tile.y = t;
        bakeRead(d, b, req);
        const PipelineStepTimes& s = b.lastSteps();
        CHECK(!s.firstUse());
        CHECK(s.texturesLoaded == 0 && !s.pageBuilt && !s.styleLoaded && s.styleMs == 0);
    }
    CHECK_EQ(sink.lines.size(), size_t(1));
    CHECK_EQ(b.firstUseBakes(), uint64_t(1));

    // slowBakeMs 0: every bake is slow; 32 lines at most, the counter goes on; firstUseLogs 0: no first-use line
    LogSink slowSink;
    PipelineConfig sc = cfg;
    sc.logUser = &slowSink;
    sc.slowBakeMs = 0;
    sc.firstUseLogs = 0;
    PipelineBaker sb(sc);
    CHECK_OK(sb.init(d.dev, d.ctx));
    CHECK_OK(sb.setMap(mi));
    req.tile.x = req.tile.y = 1;
    for (int i = 0; i < 33; ++i) bakeRead(d, sb, req);
    CHECK_EQ(sb.slowBakes(), uint64_t(33));
    CHECK_EQ(sb.firstUseBakes(), uint64_t(1));
    CHECK_EQ(slowSink.count("pipeline: slow bake: tile (1, 1) size 256, style 0, 2 layers, t8 "), size_t(32));
    CHECK_EQ(slowSink.count("first use"), size_t(0));
    CHECK_EQ(slowSink.lines.size(), size_t(32));
    // negative: a failed bake (a tile outside the map) records its steps up to the failure and, past the cap, no line
    BakeRequest out = req;
    out.tile.x = 9;
    BakedTile o;
    CHECK_ERR(sb.bake(out, o), Err::NotFound);
    CHECK(sb.lastSteps().totalMs > 0 && sb.lastSteps().blendMs == 0 && !sb.lastSteps().firstUse());
    CHECK_EQ(sb.slowBakes(), uint64_t(34));
    CHECK_EQ(slowSink.lines.size(), size_t(32));
    // under the cap a failed bake's line names its status
    LogSink failSink;
    PipelineConfig fc = sc;
    fc.logUser = &failSink;
    PipelineBaker fb(fc);
    CHECK_OK(fb.init(d.dev, d.ctx));
    CHECK_OK(fb.setMap(mi));
    CHECK_ERR(fb.bake(out, o), Err::NotFound);
    CHECK_EQ(failSink.lines.size(), size_t(1));
    if (!failSink.lines.empty()) CHECK(failSink.lines[0].find("pipeline: slow bake: tile (9, 1) size 256, failed: NotFound") == 0);
}

// The warm-up at init: every layer texture of the tables, the five styles and the throwaway bakes (every style with and
// without t8, one with a decal) before the first map; init logs its report. The map's bakes then make no first use but
// their height pages (map-dependent), and bake the same bytes as a baker without the warm-up.
C5R_TEST(game_warm_at_init_takes_the_first_uses_out_of_the_bakes) {
    Game game;
    if (!game.ok) return;
    Device d;
    LogSink sink;
    PipelineConfig cfg;
    cfg.fs = &game.fs;
    cfg.exe = &game.exe;
    cfg.log = &LogSink::fn;
    cfg.logUser = &sink;
    cfg.warm = WarmUp::AtInit;
    cfg.warmTables = &game.tables;
    cfg.warmSizeClass = 256;
    PipelineBaker b(cfg);
    CHECK_OK(b.init(d.dev, d.ctx));
    const WarmReport& r = b.warmReport();
    std::printf("  warm-up: %s\n", r.str().c_str());
    CHECK(r.ran);
    CHECK_OK(r.status);
    CHECK_EQ(size_t(r.textures), textureNames(game.tables));
    CHECK_EQ(b.texturesLoaded() + b.missingTextures().size(), textureNames(game.tables));
    CHECK_EQ(r.styles, 5u);
    CHECK_EQ(r.bakes, 11u);  // 5 styles x t8 0 / 1, and one with a decal
    CHECK(r.ms > 0 && r.ms >= r.texturesMs + r.stylesMs + r.bakesMs);
    CHECK(b.decalCounters().draws >= 1);
    CHECK_EQ(sink.count("pipeline: warm-up at init done in "), size_t(1));
    CHECK_EQ(sink.lines.size(), size_t(1));
    CHECK_EQ(b.firstUseBakes(), uint64_t(0));  // the warm-up's own bakes are not counted
    CHECK_EQ(b.slowBakes(), uint64_t(0));
    // the warm-up's map is gone: a bake before setMap fails as before
    BakeRequest req;
    req.tile.x = 1;
    req.tile.y = 1;
    req.sizeClass = 256;
    BakedTile o;
    CHECK_ERR(b.bake(req, o), Err::NotFound);
    CHECK(o.t0 == nullptr);

    heightfield::HeightGrid g = twoMaterialGrid();
    MapInputs mi;
    mi.grid = &g;
    mi.tables = &game.tables;
    CHECK_OK(b.setMap(mi));
    PipelineConfig plainCfg;
    plainCfg.fs = &game.fs;
    plainCfg.exe = &game.exe;
    PipelineBaker plain(plainCfg);
    CHECK_OK(plain.init(d.dev, d.ctx));
    CHECK_OK(plain.setMap(mi));
    int pages = 0;
    for (uint32_t t : {1u, 2u, 3u})
        for (int s = 0; s < 5; ++s)
            for (int ns = 0; ns < 2; ++ns) {
                BakeRequest q = req;
                q.tile.x = q.tile.y = t;
                PipelineBaker::Overrides ov;
                ov.style = s;
                ov.needSecond = ns;
                BakedTile wo, po;
                CHECK_OK(b.bakeWith(q, ov, wo));
                const PipelineStepTimes st = b.lastSteps();
                CHECK(st.texturesLoaded == 0 && !st.styleLoaded && st.decalTexturesLoaded == 0);
                if (st.pageBuilt) ++pages;
                CHECK_OK(plain.bakeWith(q, ov, po));
                std::vector<uint8_t> a, a8, c, c8;
                CHECK(readBc1(d.dev, d.ctx, wo.t0, a));
                CHECK(readBc1(d.dev, d.ctx, po.t0, c));
                if (wo.t8) CHECK(readBc1(d.dev, d.ctx, wo.t8, a8));
                if (po.t8) CHECK(readBc1(d.dev, d.ctx, po.t8, c8));
                CHECK(!a.empty() && a == c && a8 == c8);
                CHECK_EQ(wo.t8 != nullptr, ns == 1);
                for (ID3D11Texture2D* x : {wo.t0, wo.t8, po.t0, po.t8})
                    if (x) x->Release();
            }
    CHECK_EQ(pages, 1);  // the 4 x 4 map is one height page
    CHECK_EQ(b.firstUseBakes(), uint64_t(1));
    CHECK_EQ(sink.count("pipeline: first use: "), size_t(1));
    CHECK(plain.firstUseBakes() >= 5);  // without the warm-up every style's first bake is a first use
}

// Negative: warm() before init (NotFound) or at a bad size class (BadLayout) does nothing; without tables only the
// styles load; a texture name the install lacks is recorded as missing, as a bake would, and is no failure; a table whose
// layer has no set fails the texture step, the styles still load, the throwaway bakes cannot (setMap refuses the table:
// none made, the warm-up's map dropped) and the first failure is returned; a
// failing warm-up at init (bad size class) is logged and does not fail init.
C5R_TEST(game_negative_warm_up) {
    Game game;
    if (!game.ok) return;
    Device d;
    PipelineConfig cfg;
    cfg.fs = &game.fs;
    cfg.exe = &game.exe;
    {
        PipelineBaker b(cfg);
        CHECK_ERR(b.warm(&game.tables, 256), Err::NotFound);
        CHECK(!b.warmReport().ran);
        CHECK_ERR(b.warmReport().status, Err::NotFound);
        CHECK_EQ(b.texturesLoaded(), size_t(0));
        CHECK_OK(b.init(d.dev, d.ctx));
        CHECK_ERR(b.warm(&game.tables, 100), Err::BadLayout);
        CHECK(!b.warmReport().ran);
        CHECK_EQ(b.texturesLoaded(), size_t(0));
        CHECK_OK(b.warm(nullptr, 256));
        CHECK(b.warmReport().ran);
        CHECK_EQ(b.warmReport().textures, 0u);
        CHECK_EQ(b.warmReport().styles, 5u);
        CHECK_EQ(b.warmReport().bakes, 0u);
        CHECK_EQ(b.texturesLoaded(), size_t(0));
        // again: the styles are loaded already
        CHECK_OK(b.warm(nullptr, 256));
        CHECK_EQ(b.warmReport().styles, 0u);
    }
    {
        LayerTables t = game.tables;
        t.sets[t.setOfLayer[0]].diffuse = "no_such_texture_0417.dds";
        PipelineBaker b(cfg);
        CHECK_OK(b.init(d.dev, d.ctx));
        CHECK_OK(b.warm(&t, 256));
        CHECK_EQ(b.warmReport().bakes, 11u);
        const std::vector<std::string>& m = b.missingTextures();
        CHECK(std::find(m.begin(), m.end(), std::string("no_such_texture_0417.dds")) != m.end());
    }
    {
        LayerTables t = game.tables;
        t.setOfLayer[3] = 999;  // a layer without a set: bind fails BadLayout
        PipelineBaker b(cfg);
        CHECK_OK(b.init(d.dev, d.ctx));
        CHECK_ERR(b.warm(&t, 256), Err::BadLayout);
        CHECK(b.warmReport().ran);
        CHECK_ERR(b.warmReport().status, Err::BadLayout);
        CHECK_EQ(b.warmReport().styles, 5u);
        CHECK_EQ(b.warmReport().bakes, 0u);
        BakeRequest req;
        BakedTile o;
        CHECK_ERR(b.bake(req, o), Err::NotFound);  // no map after the warm-up
        CHECK(b.warmReport().str().find("first failure: BadLayout") != std::string::npos);
    }
    {
        LogSink sink;
        PipelineConfig bad = cfg;
        bad.warm = WarmUp::AtInit;
        bad.warmTables = &game.tables;
        bad.warmSizeClass = 100;
        bad.log = &LogSink::fn;
        bad.logUser = &sink;
        PipelineBaker b(bad);
        CHECK_OK(b.init(d.dev, d.ctx));
        CHECK_ERR(b.warmReport().status, Err::BadLayout);
        CHECK_EQ(sink.lines.size(), size_t(1));
        if (!sink.lines.empty())
            CHECK(sink.lines[0].find("pipeline: warm-up at init not run in 0.0 ms") == 0 &&
                  sink.lines[0].find("first failure: BadLayout") != std::string::npos);
        // the baker works as without the warm-up
        heightfield::HeightGrid g = synthGrid();
        MapInputs mi;
        mi.grid = &g;
        mi.tables = &game.tables;
        CHECK_OK(b.setMap(mi));
        BakeRequest req;
        req.tile.x = 1;
        req.tile.y = 1;
        CHECK(!bakeRead(d, b, req).t0.empty());
    }
}

// ---------------------------------------------------------------- ticket 0698: the layer textures' and styles' loader

namespace {
struct SingleThreadedDevice {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    SingleThreadedDevice() {
        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_SINGLETHREADED, &fl, 1,
                          D3D11_SDK_VERSION, &dev, nullptr, &ctx);
    }
    ~SingleThreadedDevice() {
        if (ctx) ctx->Release();
        if (dev) dev->Release();
    }
};

// 3 tiles x 5 styles x t8 0 / 1 of a baker, read back (t0 then t8 per bake, in that order).
std::vector<std::vector<uint8_t>> bake30(ID3D11Device* dev, ID3D11DeviceContext* ctx, PipelineBaker& b, uint32_t& reads,
                                         uint32_t& styleLoads, uint32_t& waits) {
    std::vector<std::vector<uint8_t>> out;
    reads = styleLoads = waits = 0;
    for (uint32_t t : {1u, 2u, 3u})
        for (int s = 0; s < 5; ++s)
            for (int ns = 0; ns < 2; ++ns) {
                BakeRequest q;
                q.tile.x = q.tile.y = t;
                q.sizeClass = 256;
                PipelineBaker::Overrides ov;
                ov.style = s;
                ov.needSecond = ns;
                BakedTile o;
                const Status st = b.bakeWith(q, ov, o);
                if (!st.ok()) c5r::test::fail(__FILE__, __LINE__, "bake: " + st.str());
                const PipelineStepTimes& sp = b.lastSteps();
                reads += sp.texturesLoaded;
                styleLoads += sp.styleLoaded ? 1 : 0;
                waits += sp.loaderWaits;
                std::vector<uint8_t> a, a8;
                if (o.t0) readBc1(dev, ctx, o.t0, a);
                if (o.t8) readBc1(dev, ctx, o.t8, a8);
                out.push_back(a);
                out.push_back(a8);
                if (o.t0) o.t0->Release();
                if (o.t8) o.t8->Release();
            }
    return out;
}
}  // namespace

// LoadAhead::Thread bakes the same bytes as the bakes' own reads (LoadAhead::Off), in four arms: no loader; the loader
// given time to finish before the bakes (no read and no wait in any bake); the bakes started at once after setMap (they
// wait for what is in flight, never read); the loader on a D3D11_CREATE_DEVICE_SINGLETHREADED device (read on the loader,
// made at adoption). Every layer texture name of the tables and the five styles come from the loader, and its lines are
// logged.
C5R_TEST(game_loader_bakes_bit_for_bit_what_the_bakes_own_reads_bake) {
    Game game;
    if (!game.ok) return;
    Device d;
    heightfield::HeightGrid g = twoMaterialGrid();
    MapInputs mi;
    mi.grid = &g;
    mi.tables = &game.tables;
    PipelineConfig cfg;
    cfg.fs = &game.fs;
    cfg.exe = &game.exe;
    uint32_t reads = 0, styleLoads = 0, waits = 0;
    std::vector<std::vector<uint8_t>> ref;
    {
        PipelineBaker plain(cfg);
        CHECK_OK(plain.init(d.dev, d.ctx));
        CHECK(!plain.loaderOn());
        CHECK_OK(plain.setMap(mi));
        ref = bake30(d.dev, d.ctx, plain, reads, styleLoads, waits);
        CHECK(reads >= 2 && styleLoads == 5 && waits == 0);   // the bakes read
        CHECK_EQ(plain.loaderCounters().requested, uint64_t(0));
        std::printf("  no loader: %u layer texture reads and %u style loads in the bakes\n", reads, styleLoads);
    }
    CHECK(ref.size() == 60 && !ref[0].empty());
    const size_t names = textureNames(game.tables);
    for (int arm = 0; arm < 3; ++arm) {
        SingleThreadedDevice sd;
        ID3D11Device* dev = arm == 2 ? sd.dev : d.dev;
        ID3D11DeviceContext* ctx = arm == 2 ? sd.ctx : d.ctx;
        CHECK(dev != nullptr);
        if (!dev) continue;
        LogSink sink;
        PipelineConfig lc = cfg;
        lc.loadAhead = LoadAhead::Thread;
        lc.log = &LogSink::fn;
        lc.logUser = &sink;
        lc.slowBakeMs = 1e9;
        PipelineBaker b(lc);
        CHECK_OK(b.init(dev, ctx));
        CHECK(b.loaderOn());
        CHECK_EQ(sink.count("pipeline: loader: started ("), size_t(1));
        CHECK_EQ(sink.count(arm == 2 ? "single-threaded device" : "free-threaded device"), size_t(1));
        CHECK_EQ(sink.count("; 5 styles queued"), size_t(1));
        CHECK_OK(b.setMap(mi));
        CHECK_EQ(sink.count("pipeline: loader: map set: "), size_t(1));
        CHECK_EQ(b.loaderCounters().requested, uint64_t(5 + names));
        if (arm != 1) CHECK(b.waitLoader(60000));
        const std::vector<std::vector<uint8_t>> got = bake30(dev, ctx, b, reads, styleLoads, waits);
        CHECK(b.waitLoader(60000));
        const PipelineLoaderCounters& c = b.loaderCounters();
        std::printf("  arm %d (%s): reads %u, style loads %u, waits %u; loader: %s\n", arm,
                    arm == 0 ? "loader finished first" : arm == 1 ? "bakes at once" : "single-threaded device", reads,
                    styleLoads, waits, c.str().c_str());
        CHECK_EQ(reads, 0u);        // no layer texture read by a bake
        CHECK_EQ(styleLoads, 0u);   // no style loaded by a bake
        if (arm != 1) CHECK_EQ(waits, 0u);
        CHECK_EQ(c.layerTextures + c.missing, uint64_t(names));
        CHECK_EQ(c.styles, uint64_t(5));
        CHECK_EQ(c.failed, uint64_t(0));
        CHECK_EQ(c.dropped, uint64_t(0));
        CHECK_EQ(c.fallbackBinds, uint64_t(0));
        CHECK_EQ(c.createdAtAdopt, arm == 2 ? uint64_t(c.layerTextures + 5) : uint64_t(0));
        CHECK(c.bytes > 0 && c.readMs > 0);
        CHECK_EQ(b.loaderLoads().size(), size_t(names + 5));
        CHECK_EQ(b.texturesLoaded() + b.missingTextures().size(), names);
        CHECK_EQ(b.loaderPending(), size_t(0));
        CHECK_EQ(sink.count("pipeline: loader: queue empty "), size_t(1));
        if (arm != 1) CHECK_EQ(b.firstUseBakes(), uint64_t(1));   // the height page only (a wait also counts in arm 1)
        CHECK_EQ(got.size(), ref.size());
        for (size_t k = 0; k < got.size() && k < ref.size(); ++k)
            if (got[k] != ref[k]) {
                c5r::test::fail(__FILE__, __LINE__, "arm " + std::to_string(arm) + ": bake output " + std::to_string(k) + " differs");
                break;
            }
        // a second field with the same tables queues nothing
        CHECK_OK(b.setMap(mi));
        CHECK_EQ(b.loaderCounters().requested, uint64_t(5 + names));
        CHECK_EQ(sink.count("pipeline: loader: map set: "), size_t(1));
    }
}

// The loader's fallbacks and negatives: a loader stopped before the map leaves the reads to the bakes (as before 0698,
// the same bytes); a texture name the install lacks is recorded by the loader as missing (bound as handle 0, no read in
// a bake); a layer without a set fails the bake BadLayout as without the loader; a bake before setMap fails NotFound; a
// second init restarts the loader without requeueing what is in; the destructor ends a loader with a full queue; init
// without game files fails without a thread. The style inputs' two halves report loadStyleInputs' failures in its order.
C5R_TEST(game_loader_fallbacks_and_negatives) {
    Game game;
    if (!game.ok) return;
    Device d;
    heightfield::HeightGrid g = twoMaterialGrid();
    MapInputs mi;
    mi.grid = &g;
    mi.tables = &game.tables;
    PipelineConfig cfg;
    cfg.fs = &game.fs;
    cfg.exe = &game.exe;
    BakeRequest req;
    req.tile.x = 1;
    req.tile.y = 1;
    req.sizeClass = 256;
    Caches ref;
    {
        PipelineBaker plain(cfg);
        CHECK_OK(plain.init(d.dev, d.ctx));
        CHECK_OK(plain.setMap(mi));
        ref = bakeRead(d, plain, req);
    }
    PipelineConfig lc = cfg;
    lc.loadAhead = LoadAhead::Thread;
    {
        // stopped before the map: nothing queued, the bake reads, the same bytes
        PipelineBaker b(lc);
        CHECK_OK(b.init(d.dev, d.ctx));
        b.stopLoader();
        CHECK(!b.loaderOn());
        CHECK_EQ(b.loaderPending(), size_t(0));
        CHECK_OK(b.setMap(mi));
        const Caches c = bakeRead(d, b, req);
        CHECK(b.lastSteps().texturesLoaded >= 2);
        CHECK(b.lastSteps().loaderWaits == 0);
        CHECK(c.t0 == ref.t0 && c.t8 == ref.t8);
        b.stopLoader();   // a second stop does nothing
    }
    {
        // a bake before setMap; then a missing name: recorded by the loader, no read, no failure, the same bytes otherwise
        LayerTables t = game.tables;
        const uint32_t layer = t.firstLayerOfMaterial[0];
        const std::string old = t.sets[t.setOfLayer[layer]].diffuse;
        t.sets[t.setOfLayer[layer]].diffuse = "no_such_texture_0698.dds";
        PipelineBaker b(lc);
        CHECK_OK(b.init(d.dev, d.ctx));
        BakedTile o;
        CHECK_ERR(b.bake(req, o), Err::NotFound);
        CHECK(o.t0 == nullptr);
        MapInputs m2 = mi;
        m2.tables = &t;
        CHECK_OK(b.setMap(m2));
        CHECK(b.waitLoader(60000));
        CHECK(b.loaderCounters().missing >= 1);
        const std::vector<std::string>& m = b.missingTextures();
        CHECK(std::find(m.begin(), m.end(), std::string("no_such_texture_0698.dds")) != m.end());
        const Caches c = bakeRead(d, b, req);
        CHECK_EQ(b.lastSteps().texturesLoaded, 0u);
        CHECK(!c.t0.empty());
        // the plain baker over the same table binds the same handle 0
        PipelineBaker plain(cfg);
        CHECK_OK(plain.init(d.dev, d.ctx));
        CHECK_OK(plain.setMap(m2));
        const Caches pc = bakeRead(d, plain, req);
        CHECK(pc.t0 == c.t0 && pc.t8 == c.t8);
        CHECK(std::find(plain.missingTextures().begin(), plain.missingTextures().end(),
                        std::string("no_such_texture_0698.dds")) != plain.missingTextures().end());
        (void)old;
    }
    {
        // a layer without a set: setMap refuses the table BadLayout as without the loader, and queues nothing
        LayerTables t = game.tables;
        t.setOfLayer[t.firstLayerOfMaterial[0]] = 999;
        MapInputs m2 = mi;
        m2.tables = &t;
        PipelineBaker b(lc), plain(cfg);
        CHECK_OK(b.init(d.dev, d.ctx));
        CHECK_OK(plain.init(d.dev, d.ctx));
        CHECK_ERR(plain.setMap(m2), Err::BadLayout);
        CHECK_ERR(b.setMap(m2), Err::BadLayout);
        CHECK_EQ(b.loaderCounters().requested, uint64_t(5));   // the styles only
        BakedTile o;
        CHECK_ERR(plain.bake(req, o), Err::NotFound);
        CHECK_ERR(b.bake(req, o), Err::NotFound);
        CHECK(o.t0 == nullptr);
    }
    {
        // a second init: the loader restarts; the styles already in are not queued again
        PipelineBaker b(lc);
        CHECK_OK(b.init(d.dev, d.ctx));
        CHECK(b.waitLoader(60000));
        CHECK_EQ(b.loaderCounters().styles, uint64_t(5));
        CHECK_OK(b.init(d.dev, d.ctx));
        CHECK(b.loaderOn());
        CHECK_EQ(b.loaderCounters().requested, uint64_t(5));
        CHECK_EQ(b.loaderPending(), size_t(0));
    }
    {
        // the destructor with a full queue (no wait): returns, nothing leaks into a crash
        PipelineBaker* b = new PipelineBaker(lc);
        CHECK_OK(b->init(d.dev, d.ctx));
        CHECK_OK(b->setMap(mi));
        CHECK(b->loaderOn());
        delete b;
    }
    {
        // init without game files: BadLayout, no loader
        PipelineConfig bad = lc;
        bad.fs = nullptr;
        PipelineBaker b(bad);
        CHECK_ERR(b.init(d.dev, d.ctx), Err::BadLayout);
        CHECK(!b.loaderOn());
        CHECK(b.waitLoader(0));   // nothing pending without a loader
    }
    {
        // the style inputs in two halves: style 7 BadLayout; no device BadLayout; a missing file NotFound naming it; a
        // bad Sunlight file reported before a missing Shadowlight file (loadStyleInputs' order)
        StyleFiles f;
        CHECK_ERR(readStyleFiles(game.fs, 7, f), Err::BadLayout);
        StyleInputs si;
        CHECK_ERR(createStyleInputs(d.dev, f, si), Err::BadLayout);
        CHECK_OK(readStyleFiles(game.fs, 2, f));
        CHECK(f.bytes() > 0);
        CHECK_ERR(createStyleInputs(nullptr, f, si), Err::BadLayout);
        CHECK_OK(createStyleInputs(d.dev, f, si));
        CHECK(si.sunCube && si.shadowCube && si.colorKey);
        si.release();
        StyleFiles miss = f;
        miss.shadowRead = Status::fail(Err::NotFound, shadowlightFile(2) + ": not found");
        const Status ms = createStyleInputs(d.dev, miss, si);
        CHECK_ERR(ms, Err::NotFound);
        CHECK(ms.msg.find("Shadowlight_Asia_Terrain_2.dds") != std::string::npos);
        CHECK(si.sunCube == nullptr && si.shadowCube == nullptr && si.colorKey == nullptr);
        StyleFiles badSun = miss;
        badSun.sun.resize(16);   // a truncated DDS: its parse fails before the Shadowlight read is looked at
        const Status bs = createStyleInputs(d.dev, badSun, si);
        CHECK(!bs.ok() && bs.code != Err::NotFound);
        CHECK(si.sunCube == nullptr);
    }
}

// ---------------------------------------------------------------- ticket 0712: prep ahead

namespace {
// Four layers of three materials (0: flat and slope-projected, 1, 4 with a spec map), as bakelayers' small table.
LayerTables prepTables() {
    LayerTables t;
    const Status s = parseTerrainDef("h\n0,Flat_D.dds,Flat_H.dds,NULL,0,0.014,0,0\n0,Slope_D.dds,Slope_H.dds,Slope_S.dds,1,0.05,1,54\n"
                                     "1,Next_D.dds,Next_H.dds,NULL,0,0.1,0,2\n4,Spec_D.dds,Spec_H.dds,Spec_S.dds,0,0.2,0,1F\n",
                                     t);
    if (!s.ok()) c5r::test::fail(__FILE__, __LINE__, "prepTables: " + s.str());
    return t;
}

// Steep hills (slope classes 1 and 2 somewhere in every tile) and materials in stripes of three ids, wrapping in x.
heightfield::HeightGrid prepGrid(uint32_t px = 6, uint32_t py = 5, const uint8_t ids[3] = nullptr) {
    static const uint8_t kIds[3] = {0, 1, 4};
    const uint8_t* m = ids ? ids : kIds;
    heightfield::HeightGrid g = synthGrid(px, py);
    for (uint32_t y = 0; y < g.samplesY; ++y)
        for (uint32_t x = 0; x < g.samplesX; ++x) {
            const double v = 20000 + 15000 * std::sin(x * 0.19) * std::cos(y * 0.13) + 4000 * std::sin(x * 0.011 + y * 0.017);
            g.h[g.index(x, y)] = static_cast<uint16_t>(std::max(0.0, std::min(65535.0, v)));
            g.mat[g.index(x, y)] = m[((x / 23) + (y / 17)) % 3];
        }
    return g;
}

// The CPU steps of bakeBody before ticket 0712, one by one (the reference prepCpu must equal).
Status prepPieces(const heightfield::HeightGrid& g, const LayerTables& t, TileId tile, GridNormalSource& normals,
                  SlopeClassSource& slopes, PrepProduct& out) {
    out = PrepProduct();
    out.tile = tile;
    C5R_TRY(buildLayerList(t, g, tile, out.list));
    C5R_TRY(tileWindow(g, tile, out.window));
    BlendInputs bin;
    bin.grid = &g;
    bin.tables = &t;
    bin.stepX = g.sx;
    bin.stepY = g.sy;
    bin.stepZ = g.sz;
    TileGeom geom;
    geom.originX = static_cast<int32_t>(out.window.ox);
    geom.originY = static_cast<int32_t>(out.window.oy);
    geom.extentX = out.window.ex;
    geom.extentY = out.window.ey;
    C5R_TRY(slopes.classes(g, out.window, out.cls));
    C5R_TRY(fillBlendVertices(bin, geom, out.list, out.cls, out.verts));
    C5R_TRY(buildBlendIndices(out.window.ex + 2, out.window.ey + 2, out.idx));
    return normals.normals(g, out.window, out.normals);
}

TileId tileAt(uint32_t x, uint32_t y) {
    TileId t;
    t.x = x;
    t.y = y;
    return t;
}

// Waits until the worker has ended `jobs` jobs in all (made, failed or discarded), at most ms.
bool waitPrepJobs(const PipelineBaker& b, uint64_t jobs, double ms = 10000) {
    for (int i = 0; i < static_cast<int>(ms); ++i) {
        const PrepCounters c = b.prepCounters();
        if (c.made + c.failed + c.discarded >= jobs) return true;
        Sleep(1);
    }
    return false;
}

Caches bakeReadOv(Device& d, PipelineBaker& b, const BakeRequest& req, const PipelineBaker::Overrides& ov) {
    Caches c;
    BakedTile o;
    const Status s = b.bakeWith(req, ov, o);
    if (!s.ok()) c5r::test::fail(__FILE__, __LINE__, "bake: " + s.str());
    if (o.t0) readBc1(d.dev, d.ctx, o.t0, c.t0);
    if (o.t8) readBc1(d.dev, d.ctx, o.t8, c.t8);
    c.hasT8 = o.hasT8;
    if (o.t0) o.t0->Release();
    if (o.t8) o.t8->Release();
    return c;
}

bool sameCaches(const Caches& a, const Caches& b) { return a.t0 == b.t0 && a.t8 == b.t8 && a.hasT8 == b.hasT8 && !a.t0.empty(); }
}  // namespace

// prepCpu is the bake's CPU steps in their order (layer list, window, slope classes, blend vertices, indices, normals):
// every tile of a wrapping 6 x 5 field (the seam columns 0 and 5, the last row's short window) equals the steps made one by
// one, with the EXE's sources and with the stand-ins. samePrepProduct sees one changed byte and another tile. Negatives:
// a tile outside the field (NotFound), a field without material bytes (the layer list's check), a source that fails.
C5R_TEST(prep_cpu_is_the_bakes_cpu_steps) {
    const LayerTables t = prepTables();
    const heightfield::HeightGrid g = prepGrid();
    std::unique_ptr<GridNormalSource> en = makeExeGridNormals(), an = makeApproxGridNormals();
    std::unique_ptr<SlopeClassSource> es = makeExeSlopeClasses(), fs = makeFlatSlopeClasses();
    size_t n = 0, withSlopes = 0, lists = 0;
    for (uint32_t y = 0; y < g.mapH; ++y)
        for (uint32_t x = 0; x < g.mapW; ++x) {
            PrepProduct a, b;
            CHECK_OK(prepCpu(g, t, tileAt(x, y), *en, *es, a));
            CHECK_OK(prepPieces(g, t, tileAt(x, y), *en, *es, b));
            CHECK(samePrepProduct(a, b));
            CHECK_EQ(a.normals.size(), size_t(a.window.ex + 2) * (a.window.ey + 2) * 3);
            CHECK(!a.verts.empty() && !a.idx.empty() && a.bytes() > 0);
            for (uint8_t c : a.cls) withSlopes += c != 0;
            lists += a.list.count > 1;
            PrepProduct s1, s2;
            CHECK_OK(prepCpu(g, t, tileAt(x, y), *an, *fs, s1));
            CHECK_OK(prepPieces(g, t, tileAt(x, y), *an, *fs, s2));
            CHECK(samePrepProduct(s1, s2));
            ++n;
        }
    CHECK_EQ(n, size_t(30));
    CHECK(withSlopes > 0);   // the slope classes are exercised
    CHECK(lists > 0);        // tiles with more than one layer
    std::printf("  %zu tiles: prepCpu equals the steps one by one (%zu vertices with a slope class, %zu tiles with 2+ layers)\n",
                n, withSlopes, lists);
    PrepProduct a, b;
    CHECK_OK(prepCpu(g, t, tileAt(2, 2), *en, *es, a));
    b = a;
    CHECK(samePrepProduct(a, b));
    b.normals[7] = std::nextafter(b.normals[7], 2.0f);
    CHECK(!samePrepProduct(a, b));
    b = a;
    b.verts.back().weight[0] ^= 1;   // one weight byte
    CHECK(!samePrepProduct(a, b));
    CHECK_OK(prepCpu(g, t, tileAt(3, 2), *en, *es, b));
    CHECK(!samePrepProduct(a, b));
    CHECK_ERR(prepCpu(g, t, tileAt(6, 0), *en, *es, a), Err::NotFound);
    CHECK_ERR(prepCpu(g, t, tileAt(0, 5), *en, *es, a), Err::NotFound);
    heightfield::HeightGrid noMat = g;
    noMat.mat.clear();
    PrepProduct p1, p2;
    const Status s1 = prepCpu(noMat, t, tileAt(1, 1), *en, *es, p1), s2 = prepPieces(noMat, t, tileAt(1, 1), *en, *es, p2);
    CHECK(!s1.ok());
    CHECK_EQ(s1.code, s2.code);
    struct Failing : GridNormalSource {
        const char* name() const override { return "failing"; }
        Status normals(const heightfield::HeightGrid&, const TileWindow&, std::vector<float>&) override {
            return Status::fail(Err::Io, "test");
        }
    } failing;
    CHECK_ERR(prepCpu(g, t, tileAt(1, 1), failing, *es, a), Err::Io);
    struct Short : GridNormalSource {
        const char* name() const override { return "short"; }
        Status normals(const heightfield::HeightGrid&, const TileWindow&, std::vector<float>& out) override {
            out.assign(9, 0.0f);
            return Status::Ok();
        }
    } shortN;
    CHECK_ERR(prepCpu(g, t, tileAt(1, 1), shortN, *es, a), Err::BadLayout);
}

// prepCpu on four threads at once (the shared EXE sources, every tile in another order on each) gives the serial bytes:
// the computation keeps no state between calls and does not depend on the thread.
C5R_TEST(prep_cpu_on_threads_equals_serial) {
    const LayerTables t = prepTables();
    const heightfield::HeightGrid g = prepGrid(8, 6);
    std::unique_ptr<GridNormalSource> en = makeExeGridNormals();
    std::unique_ptr<SlopeClassSource> es = makeExeSlopeClasses();
    const uint32_t N = g.mapW * g.mapH;
    std::vector<PrepProduct> serial(N);
    for (uint32_t i = 0; i < N; ++i) CHECK_OK(prepCpu(g, t, tileAt(i % g.mapW, i / g.mapW), *en, *es, serial[i]));
    std::vector<std::vector<PrepProduct>> got(4, std::vector<PrepProduct>(N));
    std::vector<int> bad(4, 0);
    std::vector<std::thread> th;
    for (int k = 0; k < 4; ++k)
        th.emplace_back([&, k]() {
            for (uint32_t j = 0; j < N; ++j) {
                static const uint32_t kMul[4] = {1, 5, 7, 11};   // prime to N = 48: every tile once per thread
                const uint32_t i = (j * kMul[k] + 7 * k) % N;
                if (!prepCpu(g, t, tileAt(i % g.mapW, i / g.mapW), *en, *es, got[k][i]).ok()) bad[k]++;
            }
        });
    for (std::thread& x : th) x.join();
    for (int k = 0; k < 4; ++k) {
        CHECK_EQ(bad[k], 0);
        for (uint32_t i = 0; i < N; ++i)
            if (!samePrepProduct(serial[i], got[k][i])) {
                c5r::test::fail(__FILE__, __LINE__, "thread " + std::to_string(k) + " tile " + std::to_string(i) + " differs");
                break;
            }
    }
}

// PrepAhead::Thread bakes the bytes of an inline baker (prep ahead off) on the same field: random tiles, the seam
// columns, generations 1 and 3, t8 made and not (needSecond 0 / 1, areaChanged), each requested and taken from the cache
// (a hit); a tile never requested (a miss), a generation other than the requested one (a miss), a batch baked at once
// (hits, waits or claims, all the same bytes). An edit of the field between the request and the bake, with the writer's
// join (prepInvalidate) and heightsChanged: a miss, the edited field's bytes. A writer that forgets the join is caught by
// the TEST ONLY check (prepVerify): mismatch counted, logged, the inline products used. setMap empties the cache.
C5R_TEST(game_prep_ahead_bakes_bit_for_bit_what_inline_bakes) {
    Game game;
    if (!game.ok) return;
    Device d;
    heightfield::HeightGrid g = synthGrid(8, 6);
    for (uint32_t y = 0; y < g.samplesY; ++y)
        for (uint32_t x = 0; x < g.samplesX; ++x) {
            g.h[g.index(x, y)] = static_cast<uint16_t>(16000 + 12000 * std::sin(x * 0.17) * std::cos(y * 0.11));
            g.mat[g.index(x, y)] = ((x / 37) + (y / 29)) % 2;
        }
    MapInputs mi;
    mi.grid = &g;
    mi.tables = &game.tables;
    PipelineConfig cfg;
    cfg.fs = &game.fs;
    cfg.exe = &game.exe;
    cfg.slowBakeMs = 1e9;
    cfg.firstUseLogs = 0;
    PipelineBaker plain(cfg);
    CHECK_OK(plain.init(d.dev, d.ctx));
    CHECK_OK(plain.setMap(mi));
    CHECK(!plain.prepOn());
    LogSink sink;
    PipelineConfig pc = cfg;
    pc.prepAhead = PrepAhead::Thread;
    pc.log = &LogSink::fn;
    pc.logUser = &sink;
    PipelineBaker ahead(pc);
    CHECK(!ahead.prepOn());   // before init
    CHECK_OK(ahead.init(d.dev, d.ctx));
    CHECK(!ahead.prepOn());   // before setMap
    CHECK_OK(ahead.setMap(mi));
    CHECK(ahead.prepOn());
    struct Case {
        uint32_t x, y;
        uint64_t gen;
        int needSecond;
        bool area;
    };
    std::vector<Case> cases;
    uint32_t r = 712;
    for (int k = 0; k < 12; ++k) {
        r = r * 1103515245u + 12345u;
        cases.push_back(Case{(r >> 8) % 8, (r >> 16) % 6, k % 3 == 2 ? 3u : 1u, k % 3 - 1, (k & 1) != 0});
    }
    for (uint32_t y : {0u, 3u, 5u}) {
        cases.push_back(Case{0, y, 1, -1, false});
        cases.push_back(Case{7, y, 2, -1, true});
    }
    uint64_t jobs = 0;
    int t8Made = 0, t8None = 0;
    for (size_t k = 0; k < cases.size(); ++k) {
        const Case& c = cases[k];
        BakeRequest q;
        q.tile = tileAt(c.x, c.y);
        q.sizeClass = 256;
        q.contentGeneration = c.gen;
        q.areaChanged = c.area;
        PipelineBaker::Overrides ov;
        ov.needSecond = c.needSecond;
        PrepHint h;
        h.tile = q.tile;
        h.contentGeneration = c.gen;
        const PrepCounters before = ahead.prepCounters();
        const bool cached = ahead.prepRequest(static_cast<int>(k % 2), &h, 1) == 0;   // a repeated tile is in the cache
        if (!cached) CHECK(waitPrepJobs(ahead, ++jobs));
        const Caches a = bakeReadOv(d, plain, q, ov), b = bakeReadOv(d, ahead, q, ov);
        CHECK(sameCaches(a, b));
        CHECK_EQ(int(ahead.lastSteps().prepAhead), 1);
        CHECK_EQ(int(plain.lastSteps().prepAhead), 0);
        CHECK_EQ(ahead.lastSteps().normalsMs, 0.0);
        CHECK_EQ(ahead.prepCounters().hits, before.hits + 1);
        (b.hasT8 ? t8Made : t8None)++;
    }
    CHECK(t8Made > 0 && t8None > 0);   // both second-cache cases
    std::printf("  %zu cases bit for bit from the cache (t8 %d made, %d not); %s\n", cases.size(), t8Made, t8None,
                ahead.prepCounters().str().c_str());
    {   // never requested: a miss, inline, the same bytes
        BakeRequest q;
        q.tile = tileAt(4, 4);
        q.contentGeneration = 9;
        const Caches a = bakeReadOv(d, plain, q, PipelineBaker::Overrides()), b = bakeReadOv(d, ahead, q, PipelineBaker::Overrides());
        CHECK(sameCaches(a, b));
        CHECK_EQ(int(ahead.lastSteps().prepAhead), 3);
        CHECK(ahead.lastSteps().normalsMs > 0);
    }
    {   // requested at generation 2, baked at 3: a miss
        PrepHint h;
        h.tile = tileAt(5, 1);
        h.contentGeneration = 2;
        CHECK_EQ(ahead.prepRequest(0, &h, 1), size_t(1));
        CHECK(waitPrepJobs(ahead, ++jobs));
        BakeRequest q;
        q.tile = h.tile;
        q.contentGeneration = 3;
        const Caches a = bakeReadOv(d, plain, q, PipelineBaker::Overrides()), b = bakeReadOv(d, ahead, q, PipelineBaker::Overrides());
        CHECK(sameCaches(a, b));
        CHECK_EQ(int(ahead.lastSteps().prepAhead), 3);
    }
    {   // a batch baked at once: whatever each bake found (hit, wait, claim), the same bytes
        std::vector<PrepHint> hs;
        for (uint32_t i = 0; i < 8; ++i) {
            PrepHint h;
            h.tile = tileAt(i, (i * 5) % 6);
            h.contentGeneration = 40;
            hs.push_back(h);
        }
        const PrepCounters c0 = ahead.prepCounters();
        CHECK_EQ(ahead.prepRequest(1, hs.data(), hs.size()), size_t(8));
        int how[4] = {};
        for (const PrepHint& h : hs) {
            BakeRequest q;
            q.tile = h.tile;
            q.contentGeneration = 40;
            const Caches a = bakeReadOv(d, plain, q, PipelineBaker::Overrides()), b = bakeReadOv(d, ahead, q, PipelineBaker::Overrides());
            CHECK(sameCaches(a, b));
            how[ahead.lastSteps().prepAhead & 3]++;
        }
        const PrepCounters c1 = ahead.prepCounters();
        CHECK_EQ(how[0], 0);
        CHECK_EQ(uint64_t(how[1]), c1.hits - c0.hits);
        CHECK_EQ(uint64_t(how[2]), c1.waits - c0.waits);
        CHECK_EQ(uint64_t(how[3]), c1.misses - c0.misses);
        std::printf("  batch of 8 at once: %d hits, %d waits, %d inline (%llu claimed, %llu timeouts)\n", how[1], how[2], how[3],
                    static_cast<unsigned long long>(c1.claimed - c0.claimed),
                    static_cast<unsigned long long>(c1.waitTimeouts - c0.waitTimeouts));
        ahead.prepInvalidate();   // the batch's jobs still running end here (their products discarded)
        const PrepCounters c2 = ahead.prepCounters();
        jobs = c2.made + c2.failed + c2.discarded;
        CHECK_EQ(c2.products, uint64_t(0));
    }
    {   // an edit between the request and the bake, with the writer's join: a miss, the edited field's bytes
        PrepHint h;
        h.tile = tileAt(2, 3);
        h.contentGeneration = 1;
        CHECK_EQ(ahead.prepRequest(0, &h, 1), size_t(1));
        CHECK(waitPrepJobs(ahead, ++jobs));
        BakeRequest q;
        q.tile = h.tile;
        q.contentGeneration = 1;
        const Caches before = bakeReadOv(d, plain, q, PipelineBaker::Overrides());
        ahead.prepInvalidate();   // the writer's join
        for (uint32_t y = 3 * 64 + 10; y < 3 * 64 + 40; ++y)
            for (uint32_t x = 2 * 64 + 5; x < 2 * 64 + 50; ++x) {
                g.h[g.index(x, y)] = static_cast<uint16_t>(g.h[g.index(x, y)] + 3000);
                g.mat[g.index(x, y)] = 1 - g.mat[g.index(x, y)];
            }
        plain.heightsChanged(g, nullptr, 0);
        ahead.heightsChanged(g, nullptr, 0);
        const Caches a = bakeReadOv(d, plain, q, PipelineBaker::Overrides()), b = bakeReadOv(d, ahead, q, PipelineBaker::Overrides());
        CHECK(sameCaches(a, b));
        CHECK(a.t0 != before.t0);   // the edit changes the bake
        CHECK_EQ(int(ahead.lastSteps().prepAhead), 3);
    }
    {   // setMap empties the cache
        PrepHint h;
        h.tile = tileAt(6, 2);
        h.contentGeneration = 1;
        CHECK_EQ(ahead.prepRequest(0, &h, 1), size_t(1));
        CHECK(waitPrepJobs(ahead, ++jobs));
        CHECK_EQ(ahead.prepCounters().products, uint64_t(1));
        const uint64_t inv = ahead.prepCounters().invalidations;
        CHECK_OK(ahead.setMap(mi));
        CHECK_EQ(ahead.prepCounters().products, uint64_t(0));
        CHECK_EQ(ahead.prepCounters().invalidations, inv + 1);
    }
    // a writer that forgets the join (material bytes only, so the height pages stay right): the TEST ONLY check
    LogSink vsink;
    PipelineConfig vc = pc;
    vc.prepVerify = true;
    vc.log = &LogSink::fn;
    vc.logUser = &vsink;
    PipelineBaker verify(vc);
    CHECK_OK(verify.init(d.dev, d.ctx));
    CHECK_OK(verify.setMap(mi));
    {
        PrepHint h;
        h.tile = tileAt(1, 1);
        h.contentGeneration = 1;
        CHECK_EQ(verify.prepRequest(0, &h, 1), size_t(1));
        CHECK(waitPrepJobs(verify, 1));
        BakeRequest q;
        q.tile = h.tile;
        q.contentGeneration = 1;
        const Caches b0 = bakeReadOv(d, verify, q, PipelineBaker::Overrides());   // checked: equal
        CHECK_EQ(int(verify.lastSteps().prepAhead), 1);
        CHECK_EQ(verify.prepCounters().verified, uint64_t(1));
        CHECK_EQ(verify.prepCounters().verifyMismatches, uint64_t(0));
        CHECK(sameCaches(b0, bakeReadOv(d, plain, q, PipelineBaker::Overrides())));
        CHECK_EQ(verify.prepRequest(0, &h, 1), size_t(0));   // still cached
        for (uint32_t y = 64 + 3; y < 64 + 60; ++y)
            for (uint32_t x = 64 + 3; x < 64 + 60; ++x) g.mat[g.index(x, y)] = 1 - g.mat[g.index(x, y)];
        const Caches a = bakeReadOv(d, plain, q, PipelineBaker::Overrides()), b = bakeReadOv(d, verify, q, PipelineBaker::Overrides());
        CHECK(sameCaches(a, b));   // the inline products were used
        CHECK(a.t0 != b0.t0);
        CHECK_EQ(verify.prepCounters().verifyMismatches, uint64_t(1));
        CHECK_EQ(vsink.count("LOUD: pipeline: prep ahead: the product of tile (1, 1) gen 1 differs"), size_t(1));
        std::printf("  verify: %s\n", verify.prepCounters().str().c_str());
    }
    CHECK_EQ(sink.count("LOUD"), size_t(0));
}

// The negatives and the bounds: no prep ahead before setMap, with a caller's normal or slope source, on a bad lane or
// null hints; hints outside the field, listed twice, cached or queued in the other lane are skipped; a lane's request
// replaces its queue; the cache's bound evicts the oldest (unused ones counted) and reports its bytes; prepStop and a
// later request start the worker again; init stops it; the destructor ends a worker with a full queue. Prep ahead off:
// the counters stay 0 and every bake says 0.
C5R_TEST(game_prep_ahead_negatives_lanes_and_bounds) {
    Game game;
    if (!game.ok) return;
    Device d;
    heightfield::HeightGrid g = twoMaterialGrid();   // 4 x 4 tiles
    MapInputs mi;
    mi.grid = &g;
    mi.tables = &game.tables;
    PipelineConfig cfg;
    cfg.fs = &game.fs;
    cfg.exe = &game.exe;
    cfg.slowBakeMs = 1e9;
    cfg.firstUseLogs = 0;
    PrepHint one;
    one.tile = tileAt(1, 1);
    one.contentGeneration = 1;
    {   // off
        PipelineBaker b(cfg);
        CHECK_OK(b.init(d.dev, d.ctx));
        CHECK_OK(b.setMap(mi));
        CHECK(!b.prepOn());
        CHECK_EQ(b.prepRequest(0, &one, 1), size_t(0));
        BakeRequest q;
        q.tile = one.tile;
        CHECK(!bakeRead(d, b, q).t0.empty());
        CHECK_EQ(int(b.lastSteps().prepAhead), 0);
        b.prepInvalidate();   // nothing runs: a no-op
        const PrepCounters c = b.prepCounters();
        CHECK(c.requested == 0 && c.made == 0 && c.hits == 0 && c.misses == 0 && c.invalidations == 0);
    }
    PipelineConfig pc = cfg;
    pc.prepAhead = PrepAhead::Thread;
    {   // a caller's source: inline only
        std::unique_ptr<GridNormalSource> an = makeApproxGridNormals();
        PipelineConfig sc = pc;
        sc.normals = an.get();
        PipelineBaker b(sc);
        CHECK_OK(b.init(d.dev, d.ctx));
        CHECK_OK(b.setMap(mi));
        CHECK(!b.prepOn());
        CHECK_EQ(b.prepRequest(0, &one, 1), size_t(0));
        BakeRequest q;
        q.tile = one.tile;
        CHECK(!bakeRead(d, b, q).t0.empty());
        CHECK_EQ(int(b.lastSteps().prepAhead), 0);
        std::unique_ptr<SlopeClassSource> fs = makeFlatSlopeClasses();
        PipelineConfig sc2 = pc;
        sc2.slopes = fs.get();
        PipelineBaker b2(sc2);
        CHECK_OK(b2.init(d.dev, d.ctx));
        CHECK_OK(b2.setMap(mi));
        CHECK(!b2.prepOn());
    }
    {
        PipelineConfig bc = pc;
        bc.prepCacheProducts = 4;
        PipelineBaker b(bc);
        CHECK_EQ(b.prepRequest(0, &one, 1), size_t(0));   // before init
        CHECK_OK(b.init(d.dev, d.ctx));
        CHECK_EQ(b.prepRequest(0, &one, 1), size_t(0));   // before setMap
        CHECK_OK(b.setMap(mi));
        CHECK_EQ(b.prepRequest(-1, &one, 1), size_t(0));
        CHECK_EQ(b.prepRequest(2, &one, 1), size_t(0));
        CHECK_EQ(b.prepRequest(0, nullptr, 1), size_t(0));
        CHECK_EQ(b.prepRequest(0, nullptr, 0), size_t(0));
        CHECK_EQ(b.prepCounters().requested, uint64_t(0));
        // outside the field and listed twice: skipped
        PrepHint hs[4] = {one, one, one, one};
        hs[1].tile = tileAt(4, 0);
        hs[2].tile = tileAt(0, 4);
        CHECK_EQ(b.prepRequest(0, hs, 4), size_t(1));
        CHECK(waitPrepJobs(b, 1));
        CHECK_EQ(b.prepRequest(1, &one, 1), size_t(0));   // cached
        // the bound: 10 distinct tiles one by one, 4 kept, the 6 oldest evicted unused
        uint64_t jobs = 1;
        for (uint32_t i = 0; i < 10; ++i) {
            PrepHint h;
            h.tile = tileAt(i % 4, i / 4 + 1);
            h.contentGeneration = 5;
            CHECK_EQ(b.prepRequest(i % 2, &h, 1), size_t(1));
            CHECK(waitPrepJobs(b, ++jobs));
        }
        PrepCounters c = b.prepCounters();
        CHECK_EQ(c.products, uint64_t(4));
        CHECK_EQ(c.evictedUnused, uint64_t(7));   // the first product and 6 of the 10
        CHECK(c.bytes > 0 && c.bytesMax >= c.bytes);
        std::printf("  cache of 4: %.0f KB (%.0f KB a product)\n", c.bytes / 1024.0, c.bytes / 4096.0);
        // the newest is a hit; it was made in lane 1
        BakeRequest q;
        q.tile = tileAt(9 % 4, 9 / 4 + 1);
        q.contentGeneration = 5;
        CHECK(!bakeRead(d, b, q).t0.empty());
        CHECK_EQ(int(b.lastSteps().prepAhead), 1);
        // stop, then a request starts the worker again
        b.prepStop();
        CHECK_EQ(b.prepCounters().products, uint64_t(0));
        PrepHint h2 = one;
        h2.contentGeneration = 77;
        CHECK_EQ(b.prepRequest(0, &h2, 1), size_t(1));
        CHECK(waitPrepJobs(b, ++jobs));
        q.tile = h2.tile;
        q.contentGeneration = 77;
        CHECK(!bakeRead(d, b, q).t0.empty());
        CHECK_EQ(int(b.lastSteps().prepAhead), 1);
        // a lane's request replaces its queue: a long queue, then one hint in the same lane
        std::vector<PrepHint> many;
        for (uint32_t i = 0; i < 16; ++i) {
            PrepHint h;
            h.tile = tileAt(i % 4, i / 4);
            h.contentGeneration = 100 + i;
            many.push_back(h);
        }
        const PrepCounters r0 = b.prepCounters();
        CHECK_EQ(b.prepRequest(1, many.data(), many.size()), size_t(4));   // the cap: prepCacheProducts per lane
        PrepHint last = one;
        last.contentGeneration = 200;
        CHECK_EQ(b.prepRequest(1, &last, 1), size_t(1));
        b.prepInvalidate();
        const PrepCounters r1 = b.prepCounters();
        CHECK_EQ(r1.requested - r0.requested, uint64_t(5));
        CHECK(r1.replaced - r0.replaced <= 4);
        CHECK(r1.made + r1.discarded + r1.failed - (r0.made + r0.discarded + r0.failed) <= 5);
        CHECK_EQ(r1.products, uint64_t(0));
        // init stops the worker and empties the cache
        CHECK_EQ(b.prepRequest(0, &h2, 1), size_t(1));
        CHECK_OK(b.init(d.dev, d.ctx));
        CHECK_EQ(b.prepCounters().products, uint64_t(0));
    }
    {   // the destructor ends a worker with a full queue
        PipelineConfig bc = pc;
        bc.prepCacheProducts = 64;
        PipelineBaker* b = new PipelineBaker(bc);
        CHECK_OK(b->init(d.dev, d.ctx));
        CHECK_OK(b->setMap(mi));
        std::vector<PrepHint> all;
        for (uint32_t i = 0; i < 16; ++i) {
            PrepHint h;
            h.tile = tileAt(i % 4, i / 4);
            h.contentGeneration = 3;
            all.push_back(h);
        }
        CHECK_EQ(b->prepRequest(0, all.data(), all.size()), size_t(16));
        const DWORD t0 = GetTickCount();
        delete b;
        CHECK(GetTickCount() - t0 < 5000);
    }
}

// ---------------------------------------------------------------- ticket 0714: the height pages at the field

namespace {
// A 30 x 20 plot map: 1920 x 1280 samples, (1920 + 896) / 896 = 3 by (1280 + 896) / 896 = 2 pages.
heightfield::HeightGrid pageGrid() {
    heightfield::HeightGrid g = synthGrid(30, 20);
    for (uint32_t y = 0; y < g.samplesY; ++y)
        for (uint32_t x = 900; x < g.samplesX; ++x) g.mat[g.index(x, y)] = 1;
    return g;
}
std::vector<TileId> allTiles(const heightfield::HeightGrid& g) {
    std::vector<TileId> v;
    for (uint32_t y = 0; y < g.mapH; ++y)
        for (uint32_t x = 0; x < g.mapW; ++x) {
            TileId t;
            t.x = x;
            t.y = y;
            v.push_back(t);
        }
    return v;
}
// Tiles on every page, the wrap seam's two columns and the last row.
std::vector<TileId> sampleTiles(const heightfield::HeightGrid& g) {
    std::vector<TileId> v;
    for (uint32_t y : {0u, 1u, 7u, 13u, 14u, 15u, g.mapH - 1})
        for (uint32_t x : {0u, 6u, 13u, 14u, 20u, 27u, 28u, g.mapW - 1}) {
            TileId t;
            t.x = x;
            t.y = y;
            v.push_back(t);
        }
    return v;
}
}  // namespace

// warmPages before the bakes: the bakes build no page (no `(built)` in any first-use line) and bake the same bytes as a
// baker that builds its pages in the bakes (tiles on every page, both sides of the wrap seam, the first and last rows);
// a second call builds nothing; tiles outside the map are skipped and counted; after heightsChanged (all, then a box over
// an edited region between the warm-up and the bake) the next bake builds the dropped page again from the edited grid
// and still equals the plain baker's.
C5R_TEST(game_warm_pages_takes_the_page_builds_out_of_the_bakes) {
    Game game;
    if (!game.ok) return;
    Device d;
    LogSink sink;
    PipelineConfig cfg;
    cfg.fs = &game.fs;
    cfg.exe = &game.exe;
    cfg.log = &LogSink::fn;
    cfg.logUser = &sink;
    cfg.slowBakeMs = 1e9;
    cfg.firstUseLogs = 1000;
    PipelineBaker b(cfg);
    CHECK_OK(b.init(d.dev, d.ctx));
    PipelineConfig plainCfg = cfg;
    plainCfg.log = nullptr;
    PipelineBaker plain(plainCfg);
    CHECK_OK(plain.init(d.dev, d.ctx));
    heightfield::HeightGrid g = pageGrid();
    uint32_t px = 0, py = 0;
    CHECK_OK(heightPageCount(g, px, py));
    CHECK_EQ(px * py, 6u);
    MapInputs mi;
    mi.grid = &g;
    mi.tables = &game.tables;
    CHECK_OK(b.setMap(mi));
    CHECK_OK(plain.setMap(mi));

    std::vector<TileId> tiles = allTiles(g);
    TileId outX, outY;
    outX.x = g.mapW;
    outX.y = 0;
    outY.x = 0;
    outY.y = g.mapH + 3;
    tiles.push_back(outX);
    tiles.push_back(outY);
    PageWarmReport r;
    CHECK_OK(b.warmPages(tiles.data(), tiles.size(), &r));
    std::printf("  warmPages: %u tiles, %u skipped, %u pages, %u built, %.1f ms (thread CPU %.1f ms)\n", r.tiles, r.skipped,
                r.pages, r.built, r.ms, r.threadMs);
    CHECK_EQ(r.tiles, uint32_t(tiles.size()));
    CHECK_EQ(r.skipped, 2u);
    CHECK_EQ(r.pages, 6u);
    CHECK_EQ(r.built, 6u);
    CHECK(r.ms > 0);
    CHECK_EQ(b.pagesBuilt(), uint64_t(6));
    CHECK(sink.lines.empty());   // warmPages logs nothing: the caller says it
    // again: nothing new
    PageWarmReport r2;
    CHECK_OK(b.warmPages(tiles.data(), tiles.size(), &r2));
    CHECK_EQ(r2.pages, 6u);
    CHECK_EQ(r2.built, 0u);
    CHECK_EQ(b.pagesBuilt(), uint64_t(6));

    int bakes = 0;
    for (const TileId& t : sampleTiles(g)) {
        BakeRequest q;
        q.tile = t;
        q.sizeClass = 256;
        const Caches a = bakeRead(d, b, q);
        CHECK(!b.lastSteps().pageBuilt);
        const Caches c = bakeRead(d, plain, q);
        CHECK(!a.t0.empty() && a.t0 == c.t0 && a.t8 == c.t8 && a.hasT8 == c.hasT8);
        ++bakes;
    }
    CHECK_EQ(bakes, 56);
    CHECK_EQ(b.pagesBuilt(), uint64_t(6));    // the bakes built none
    CHECK_EQ(plain.pagesBuilt(), uint64_t(6));  // the plain baker built every page in its bakes
    CHECK_EQ(sink.count("(built)"), size_t(0));
    CHECK(plain.firstUseBakes() >= 6);

    // heightsChanged (all): the lazy build of a dropped page is unchanged
    b.heightsChanged(g, nullptr, 0);
    BakeRequest q;
    q.tile.x = 3;
    q.tile.y = 3;
    q.sizeClass = 256;
    bakeRead(d, b, q);
    CHECK(b.lastSteps().pageBuilt);
    CHECK_EQ(b.pagesBuilt(), uint64_t(7));
    PageWarmReport r3;
    CHECK_OK(b.warmPages(tiles.data(), tiles.size(), &r3));
    CHECK_EQ(r3.built, 5u);   // the other five again

    // an in-place edit between the warm-up and the bake: the box's pages dropped, the bake builds them from the edited grid
    for (uint32_t y = 300; y < 340; ++y)
        for (uint32_t x = 300; x < 340; ++x) g.h[g.index(x, y)] = static_cast<uint16_t>(g.h[g.index(x, y)] + 9000);
    const uint32_t box[4] = {300, 300, 340, 340};
    b.heightsChanged(g, box, 1);
    plain.heightsChanged(g, box, 1);
    const uint64_t before = b.pagesBuilt();
    q.tile.x = 5;
    q.tile.y = 5;
    const Caches ea = bakeRead(d, b, q);
    CHECK(b.lastSteps().pageBuilt);
    CHECK_EQ(b.pagesBuilt(), before + 1);
    const Caches ec = bakeRead(d, plain, q);
    CHECK(!ea.t0.empty() && ea.t0 == ec.t0 && ea.t8 == ec.t8);
}

// Negative: warmPages before init and before setMap (NotFound), null tiles with a count (BadLayout), nothing built; no
// tiles: Ok, nothing built; only tiles outside the map: Ok, all skipped; a baker given its own page source
// (PipelineConfig::heightPages) counts the pages but pagesBuilt stays 0; a failed setMap drops the map (NotFound again).
C5R_TEST(game_negative_warm_pages) {
    Game game;
    if (!game.ok) return;
    Device d;
    PipelineConfig cfg;
    cfg.fs = &game.fs;
    cfg.exe = &game.exe;
    PipelineBaker b(cfg);
    TileId t;
    PageWarmReport r;
    r.built = 99;
    CHECK_ERR(b.warmPages(&t, 1, &r), Err::NotFound);
    CHECK_EQ(r.built, 0u);
    CHECK_EQ(r.tiles, 1u);
    CHECK_OK(b.init(d.dev, d.ctx));
    CHECK_ERR(b.warmPages(&t, 1, &r), Err::NotFound);
    heightfield::HeightGrid g = pageGrid();
    MapInputs mi;
    mi.grid = &g;
    mi.tables = &game.tables;
    CHECK_OK(b.setMap(mi));
    CHECK_ERR(b.warmPages(nullptr, 3, &r), Err::BadLayout);
    CHECK_EQ(b.pagesBuilt(), uint64_t(0));
    CHECK_OK(b.warmPages(nullptr, 0, &r));
    CHECK_EQ(r.pages, 0u);
    CHECK_EQ(r.built, 0u);
    TileId out[2];
    out[0].x = 500;
    out[1].y = 500;
    CHECK_OK(b.warmPages(out, 2, &r));
    CHECK_EQ(r.skipped, 2u);
    CHECK_EQ(r.pages, 0u);
    CHECK_OK(b.warmPages(&t, 1, nullptr));   // no report asked
    CHECK_EQ(b.pagesBuilt(), uint64_t(1));
    // a failed setMap (no tables) drops the map
    MapInputs bad = mi;
    bad.tables = nullptr;
    CHECK_ERR(b.setMap(bad), Err::BadLayout);
    CHECK_ERR(b.warmPages(&t, 1, &r), Err::NotFound);
    // the caller's own page source
    std::unique_ptr<HeightPageSource> fitted = makeFittedHeightPages();
    PipelineConfig fc = cfg;
    fc.heightPages = fitted.get();
    PipelineBaker fb(fc);
    CHECK_OK(fb.init(d.dev, d.ctx));
    CHECK_OK(fb.setMap(mi));
    std::vector<TileId> all = allTiles(g);
    CHECK_OK(fb.warmPages(all.data(), all.size(), &r));
    CHECK_EQ(r.pages, 6u);
    CHECK_EQ(r.built, 0u);
    CHECK_EQ(fb.pagesBuilt(), uint64_t(0));
}

C5R_TEST_MAIN
