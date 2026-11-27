// c5r::terrain and terrainshot tests (epic 3200, issue 3202). Every check of the API has a negative test asserting the
// error code and that nothing changed after the failure. The draw tests use the EXE's own TerrainShader programs (read
// from C5R_EXE) on a windowless device (hardware, WARP if none) and synthetic height fields whose surface is known:
// the world position read back per pixel must lie on the EXE's bicubic patch surface computed here on the CPU.
// Without C5R_EXE those tests skip, the executable exits 77 and render/build.py --test fails the run (bug 0091 item 2).
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "c5r/bake/heightpage.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/terrain/terrain.h"
#include "c5r/heightfield/heightfield.h"
#include "terrainshot_core.h"
#include "testkit.h"
#include "testutil.h"

using namespace c5r;
using namespace c5r::terrain;

namespace {

std::wstring wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }

// A field of mapW x mapH plots whose sample (c, r) holds f(c, r).
template <class F> heightfield::HeightGrid makeGrid(uint32_t mapW, uint32_t mapH, F f, bool wrap = false) {
    heightfield::HeightGrid g;
    g.mapW = mapW;
    g.mapH = mapH;
    g.samplesX = mapW * 64;
    g.samplesY = mapH * 64;
    g.wrapX = wrap;
    g.sx = 1.0f;
    g.sy = 0.866f;
    g.sz = 0.2f;
    g.ox = static_cast<float>(mapW * 32);
    g.oy = static_cast<float>(mapH) * 32 * 0.866f;
    g.waterZ = 10.117f;
    g.h.resize(static_cast<size_t>(g.samplesX) * g.samplesY);
    for (uint32_t r = 0; r < g.samplesY; ++r)
        for (uint32_t c = 0; c < g.samplesX; ++c) g.h[g.index(c, r)] = f(c, r);
    return g;
}

// A ramp in x whose drawn height (the high byte, issue 0134) is linear: byte = c, so the bicubic surface is the plane
// z = 0.2 c and the tessellated triangles lie on it exactly. The low byte (0xA5 + r) is what HeightAtGrid would add
// (0.13-0.33 world units): a draw from the raw u16 fails every surface check below.
uint16_t ramp(uint32_t c, uint32_t r) { return static_cast<uint16_t>(((c & 255u) << 8) | ((0xA5u + r) & 255u)); }

// A camera at (cx, cy, height) looking straight down (+y up on screen), RH perspective, fov 30 degrees.
Camera downCamera(float cx, float cy, float height, uint32_t w, uint32_t h) {
    Camera c;
    const float v[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -cx, -cy, -height, 1};
    std::memcpy(c.view, v, sizeof v);
    const float n = 1.0f, f = 1000.0f;
    const float ys = 1.0f / std::tan(15.0f * 3.14159265f / 180.0f), xs = ys * static_cast<float>(h) / static_cast<float>(w);
    const float p[16] = {xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, f / (n - f), -1, 0, 0, n * f / (n - f), 0};
    std::memcpy(c.proj, p, sizeof p);
    c.width = w;
    c.height = h;
    return c;
}

// The EXE's patch weights for a domain value t (TerrainFrame.BicubicBasis rows, DS 0x067fa080).
void weights(double t, double w[4]) {
    const double B[4][4] = {{-1.0 / 8, 7.0 / 8, -7.0 / 8, 1.0 / 8}, {3.0 / 8, -1.5, 9.0 / 8, 0}, {-3.0 / 8, 0, 3.0 / 8, 0},
                            {1.0 / 8, 3.0 / 4, 1.0 / 8, 0}};
    const double p[4] = {t * t * t, t * t, t, 1};
    for (int k = 0; k < 4; ++k) w[k] = p[0] * B[0][k] + p[1] * B[1][k] + p[2] * B[2][k] + p[3] * B[3][k];
}
// Height of the EXE's patch surface at sample coordinates (c, r), over the heights the EXE draws (its 8-bit height
// page: drawnHeightAtSample, issue 0134) or, with cpu = true, over HeightAtGrid's u16 / 255 * sz (where the EXE places
// models; the module drew this surface until 2026-11-13).
double surface(const heightfield::HeightGrid& g, double c, double r, bool cpu = false) {
    const double a = std::floor(c), b = std::floor(r);
    double wx[4], wy[4];
    weights(c - a, wx);
    weights(r - b, wy);
    double z = 0;
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i) {
            const int x = static_cast<int>(a) - 1 + i, y = static_cast<int>(b) - 1 + j;
            z += wy[j] * wx[i] * (cpu ? heightfield::heightAtSample(g, x, y) : drawnHeightAtSample(g, x, y));
        }
    return z;
}

struct Fixture {
    terrainshot::Device d;
    Renderer r;
    bool ok = false;
    Fixture() {
        const std::string exe = test::exePath();  // skips without C5R_EXE
        Status st = d.create(false);
        if (!st.ok()) {
            test::fail(__FILE__, __LINE__, "device: " + st.str());
            return;
        }
        st = r.init(d.dev, wide(exe), L"");
        if (!st.ok()) {
            test::fail(__FILE__, __LINE__, "init: " + st.str());
            return;
        }
        ok = true;
    }
};

ID3D11ShaderResourceView* solidTexture(ID3D11Device* dev, uint32_t rgba) {
    std::vector<uint32_t> px(16, rgba);
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = d.Height = 4;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA s = {px.data(), 16, 0};
    ID3D11Texture2D* t = nullptr;
    ID3D11ShaderResourceView* v = nullptr;
    if (FAILED(dev->CreateTexture2D(&d, &s, &t))) return nullptr;
    dev->CreateShaderResourceView(t, nullptr, &v);
    t->Release();
    return v;
}

struct Target {
    ID3D11Texture2D* tex = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    Target(ID3D11Device* dev, uint32_t w, uint32_t h, DXGI_FORMAT f) {
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = w;
        d.Height = h;
        d.MipLevels = d.ArraySize = 1;
        d.Format = f;
        d.SampleDesc.Count = 1;
        d.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (SUCCEEDED(dev->CreateTexture2D(&d, nullptr, &tex))) dev->CreateRenderTargetView(tex, nullptr, &rtv);
    }
    ~Target() {
        if (rtv) rtv->Release();
        if (tex) tex->Release();
    }
};

bool nearly(int a, int b, int tol) { return a - b <= tol && b - a <= tol; }

} // namespace

// ---------------------------------------------------------------- before init (no EXE needed)

C5R_TEST(negative_calls_before_init_are_not_found) {
    Renderer r;
    heightfield::HeightGrid g = makeGrid(2, 2, ramp);
    CHECK_ERR(r.setHeightField(g), Err::NotFound);
    CHECK_ERR(r.setTileCache(0, 0, TileCache()), Err::NotFound);
    Camera cam = downCamera(0, 0, 100, 64, 64);
    CHECK_ERR(r.draw(nullptr, nullptr, nullptr, cam, Options(), nullptr), Err::NotFound);
    double ms = 0;
    CHECK_ERR(r.gpuTime(nullptr, false, &ms), Err::NotFound);
    CHECK_EQ(r.tileGrid().tilesX, 0u);
}

C5R_TEST(negative_init_without_device_or_exe) {
    Renderer r;
    CHECK_ERR(r.init(nullptr, L"Z:/no/such.exe", L""), Err::BadLayout);
    terrainshot::Device d;
    CHECK_OK(d.create(true));  // WARP: no hardware needed for this one
    CHECK_ERR(r.init(d.dev, L"Z:/no/such/CivilizationV_DX11.exe", L""), Err::Io);
    CHECK_ERR(r.setHeightField(makeGrid(2, 2, ramp)), Err::NotFound);  // a failed init leaves nothing behind
}

C5R_TEST(negative_init_exe_without_terrain_blobs_is_not_found) {
    // A PE whose .shader section holds one valid shader, but none at the TerrainShader VAs ("missing blob").
    std::vector<uint8_t> vs = test::stripLikeExe(test::compileHlsl(
        "float4 main(float3 p : POSITION) : SV_Position { return float4(p, 1); }", "main", "vs_5_0"));
    CHECK(!vs.empty());
    std::vector<uint8_t> pe = test::buildPe({{".text", 0x1000, 0x10, std::vector<uint8_t>(0x10, 0xC3)},
                                             {".shader", 0x2000, static_cast<uint32_t>(vs.size()), vs}});
    const std::string path = test::workDir() + "/c5r_terrain_fake_exe.bin";
    FILE* f = std::fopen(path.c_str(), "wb");
    CHECK(f != nullptr);
    if (!f) return;
    std::fwrite(pe.data(), 1, pe.size(), f);
    std::fclose(f);
    terrainshot::Device d;
    CHECK_OK(d.create(true));
    Renderer r;
    CHECK_ERR(r.init(d.dev, wide(path), L""), Err::NotFound);
    // and a PE without a .shader section
    pe = test::buildPe({{".text", 0x1000, 0x10, std::vector<uint8_t>(0x10, 0xC3)}});
    f = std::fopen(path.c_str(), "wb");
    std::fwrite(pe.data(), 1, pe.size(), f);
    std::fclose(f);
    CHECK_ERR(r.init(d.dev, wide(path), L""), Err::NotFound);
    DeleteFileA(path.c_str());
}

// ---------------------------------------------------------------- terrainshot inputs (no EXE needed)

C5R_TEST(negative_json_parser) {
    terrainshot::Json j;
    CHECK_OK(terrainshot::parseJson("{\"a\": [1, -2.5e1, true, null, \"x\\\"y\"], \"b\": {}}", j));
    CHECK(j.get("a") && j.get("a")->arr.size() == 5 && j.get("a")->arr[1].num == -25.0);
    CHECK(j.get("a")->arr[4].str == "x\"y");
    CHECK_ERR(terrainshot::parseJson("", j), Err::Truncated);
    CHECK_ERR(terrainshot::parseJson("{\"a\": [1, 2", j), Err::Truncated);
    CHECK_ERR(terrainshot::parseJson("{\"a\" 1}", j), Err::BadLayout);
    CHECK_ERR(terrainshot::parseJson("{} x", j), Err::BadLayout);
    CHECK_ERR(terrainshot::parseJson("[tru]", j), Err::BadLayout);
    CHECK_ERR(terrainshot::parseJson(std::string(100, '['), j), Err::BadLayout);  // nesting
}

namespace {
void writeText(const std::string& path, const std::string& s) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f) {
        std::fwrite(s.data(), 1, s.size(), f);
        std::fclose(f);
    }
}
const char* kCam =
    "{\"num_views\": 1, \"proj\": [[2,0,0,0],[0,3,0,0],[0,0,-1.01,-1],[0,0,-50.6,0]],"
    " \"views\": [[[1,0,0,0],[0,0.707,-0.707,0],[0,0.707,0.707,0],[-10,20,-500,1]]], \"backbuffer\": [64, 48]}";
} // namespace

C5R_TEST(negative_camera_json) {
    const std::string dir = test::workDir();
    const std::string p = dir + "/c5r_terrain_cam.json";
    Camera cam;
    writeText(p, kCam);
    CHECK_OK(terrainshot::loadCamera(wide(p), 0, cam));
    CHECK(cam.width == 64 && cam.height == 48 && cam.view[12] == -10.0f && cam.proj[11] == -1.0f);
    CHECK_ERR(terrainshot::loadCamera(wide(p), 1, cam), Err::BadLayout);           // view outside num_views
    CHECK_ERR(terrainshot::loadCamera(wide(dir + "/nope.json"), 0, cam), Err::Io);  // missing file
    std::string s = kCam;
    writeText(p, s.substr(0, s.size() - 5));
    CHECK_ERR(terrainshot::loadCamera(wide(p), 0, cam), Err::Truncated);
    s = kCam;
    s.replace(s.find("\"proj\""), 6, "\"prox\"");
    writeText(p, s);
    CHECK_ERR(terrainshot::loadCamera(wide(p), 0, cam), Err::NotFound);             // no proj
    s = kCam;
    s.replace(s.find("-500"), 4, "1e999");
    writeText(p, s);
    CHECK_ERR(terrainshot::loadCamera(wide(p), 0, cam), Err::BadLayout);            // not finite
    s = kCam;
    s.replace(s.find("[64, 48]"), 8, "[0, 48]");
    writeText(p, s);
    CHECK_ERR(terrainshot::loadCamera(wide(p), 0, cam), Err::BadLayout);            // size 0
    DeleteFileA(p.c_str());
}

C5R_TEST(negative_tap_and_ground_truth_inputs) {
    const std::string dir = test::workDir();
    heightfield::HeightGrid g = makeGrid(2, 2, ramp);
    std::vector<uint8_t> tap;
    CHECK_OK(heightfield::writeTap(g, tap));
    const std::string p = dir + "/c5r_terrain_tap.c5hf";
    writeText(p, std::string(tap.begin(), tap.end()));
    heightfield::HeightGrid back;
    CHECK_OK(terrainshot::loadTapFile(wide(p), back));
    CHECK(back.h == g.h && back.samplesX == 128);
    writeText(p, std::string(tap.begin(), tap.begin() + 1000));                    // truncated payload
    CHECK_ERR(terrainshot::loadTapFile(wide(p), back), Err::Truncated);
    std::string bad(tap.begin(), tap.end());
    bad[0] = 'X';
    writeText(p, bad);
    CHECK_ERR(terrainshot::loadTapFile(wide(p), back), Err::BadMagic);             // bad tap
    CHECK_ERR(terrainshot::loadTapFile(wide(dir + "/nope.c5hf"), back), Err::Io);
    DeleteFileA(p.c_str());
    // ground-truth folder: missing files, wrong raw size
    const std::string gt = dir + "/c5r_terrain_gt";
    CreateDirectoryA(gt.c_str(), nullptr);
    CHECK_ERR(terrainshot::loadGroundTruth(wide(gt), back), Err::Io);
    writeText(gt + "/heightmap_info.json", "{\"samples_x\": 128, \"samples_y\": 128}");
    writeText(gt + "/grid_constants.json",
              "{\"wrap_x\": 1, \"grid_sx\": 1.0, \"grid_sy\": 0.866, \"grid_height_scale\": 0.2, \"grid_ox\": 64,"
              " \"grid_oy\": 55.4, \"heightmap_header_hex\": \"0000803f22de2141\"}");
    writeText(gt + "/heightmap_u16le_128x128.raw", std::string(100, '\0'));
    CHECK_ERR(terrainshot::loadGroundTruth(wide(gt), back), Err::BadLayout);       // raw size
    writeText(gt + "/heightmap_u16le_128x128.raw", std::string(128 * 128 * 2, '\0'));
    CHECK_OK(terrainshot::loadGroundTruth(wide(gt), back));
    CHECK(back.wrapX && back.mapW == 2 && back.mapH == 2 && std::fabs(back.waterZ - 10.1167f) < 1e-3f);
    writeText(gt + "/heightmap_info.json", "{\"samples_x\": 100, \"samples_y\": 128}");
    CHECK_ERR(terrainshot::loadGroundTruth(wide(gt), back), Err::BadLayout);       // not plots * 64
    writeText(gt + "/heightmap_info.json", "{\"samples_y\": 128}");
    CHECK_ERR(terrainshot::loadGroundTruth(wide(gt), back), Err::NotFound);        // key missing
    DeleteFileA((gt + "/heightmap_info.json").c_str());
    DeleteFileA((gt + "/grid_constants.json").c_str());
    DeleteFileA((gt + "/heightmap_u16le_128x128.raw").c_str());
    RemoveDirectoryA(gt.c_str());
}

C5R_TEST(negative_png_and_world_writers) {
    const std::string dir = test::workDir();
    std::vector<uint8_t> px(4 * 3 * 4, 200);
    const std::string png = dir + "/c5r_terrain_out.png", wf = dir + "/c5r_terrain_out.f32";
    CHECK_OK(terrainshot::writePngRgba(wide(png), 4, 3, px));
    CHECK(GetFileAttributesA(png.c_str()) != INVALID_FILE_ATTRIBUTES);
    CHECK_ERR(terrainshot::writePngRgba(wide(png), 4, 4, px), Err::BadLayout);
    CHECK_ERR(terrainshot::writePngRgba(wide(png), 0, 3, px), Err::BadLayout);
    CHECK_ERR(terrainshot::writePngRgba(L"Z:/no/such/dir/x.png", 4, 3, px), Err::Io);
    std::vector<float> wv(4 * 3 * 4, 1.0f);
    CHECK_OK(terrainshot::writeWorld(wide(wf), 4, 3, wv));
    CHECK_ERR(terrainshot::writeWorld(wide(wf), 4, 2, wv), Err::BadLayout);
    CHECK_ERR(terrainshot::writeWorld(L"Z:/no/such/dir/x.f32", 4, 3, wv), Err::Io);
    DeleteFileA(png.c_str());
    DeleteFileA(wf.c_str());
    DeleteFileA((wf + ".json").c_str());
}

// ---------------------------------------------------------------- with the EXE's programs

C5R_TEST(negative_set_height_field_checks_and_keeps_the_old_field) {
    Fixture fx;
    if (!fx.ok) return;
    Renderer& r = fx.r;
    CHECK_OK(r.setHeightField(makeGrid(4, 2, ramp)));
    TileGrid tg = r.tileGrid();
    CHECK(tg.tilesX == 2 && tg.tilesY == 1 && tg.tilePlotsX == 2 && tg.tileSamples == 128);
    heightfield::HeightGrid g = makeGrid(4, 4, ramp);
    heightfield::HeightGrid b = g;
    b.h.pop_back();
    CHECK_ERR(r.setHeightField(b), Err::BadLayout);                       // h.size() != samplesX * samplesY
    b = g;
    b.samplesX = 250;
    CHECK_ERR(r.setHeightField(b), Err::BadLayout);                       // samples != plots * 64
    b = g;
    b.mat.assign(10, 0);
    CHECK_ERR(r.setHeightField(b), Err::BadLayout);                       // mat of the wrong size
    b = g;
    b.sz = 0;
    CHECK_ERR(r.setHeightField(b), Err::BadLayout);
    b = g;
    b.ox = std::nanf("");
    CHECK_ERR(r.setHeightField(b), Err::BadLayout);
    CHECK_ERR(r.setHeightField(g, 0), Err::Unsupported);
    CHECK_ERR(r.setHeightField(g, kMaxTilePlots + 1), Err::Unsupported);
    CHECK_ERR(r.setHeightField(makeGrid(3, 4, ramp), 2), Err::Unsupported);  // 3 plots not a multiple of 2
    tg = r.tileGrid();
    CHECK(tg.tilesX == 2 && tg.tilesY == 1);                                 // the old field is still in place
    CHECK_OK(r.setHeightField(makeGrid(3, 3, ramp), 3));
    CHECK(r.tileGrid().tilesX == 1 && r.tileGrid().tileSamples == 192);
}

C5R_TEST(negative_tile_cache_and_draw_checks) {
    Fixture fx;
    if (!fx.ok) return;
    Renderer& r = fx.r;
    Target rt(fx.d.dev, 64, 48, DXGI_FORMAT_R8G8B8A8_UNORM);
    Camera cam = downCamera(0, 0, 100, 64, 48);
    CHECK_ERR(r.draw(fx.d.ctx, rt.rtv, nullptr, cam, Options(), nullptr), Err::NotFound);  // no field set
    CHECK_ERR(r.setTileCache(0, 0, TileCache()), Err::NotFound);
    CHECK_OK(r.setHeightField(makeGrid(4, 4, ramp)));
    CHECK_ERR(r.setTileCache(2, 0, TileCache()), Err::BadLayout);  // tile out of range
    CHECK_ERR(r.setTileCache(0, 2, TileCache()), Err::BadLayout);
    CHECK_OK(r.setTileCache(1, 1, TileCache()));
    FrameStats st;
    st.tilesDrawn = 99;
    CHECK_ERR(r.draw(fx.d.ctx, nullptr, nullptr, cam, Options(), &st), Err::BadLayout);
    CHECK_EQ(st.tilesDrawn, 99u);  // stats untouched on failure
    Camera c = cam;
    c.width = 0;
    CHECK_ERR(r.draw(fx.d.ctx, rt.rtv, nullptr, c, Options(), &st), Err::BadLayout);
    c = cam;
    c.width = 65;
    CHECK_ERR(r.draw(fx.d.ctx, rt.rtv, nullptr, c, Options(), &st), Err::BadLayout);  // not the target's size
    c = cam;
    c.view[5] = std::nanf("");
    CHECK_ERR(r.draw(fx.d.ctx, rt.rtv, nullptr, c, Options(), &st), Err::BadLayout);   // camera not finite
    c = cam;
    c.proj[0] = INFINITY;
    CHECK_ERR(r.draw(fx.d.ctx, rt.rtv, nullptr, c, Options(), &st), Err::BadLayout);
    c = cam;
    std::memset(c.view, 0, sizeof c.view);
    CHECK_ERR(r.draw(fx.d.ctx, rt.rtv, nullptr, c, Options(), &st), Err::BadLayout);   // view not invertible
    Options o;
    o.pass = 3;
    CHECK_ERR(r.draw(fx.d.ctx, rt.rtv, nullptr, cam, o, &st), Err::BadLayout);
    o = Options();
    o.tessFactor = 65;
    CHECK_ERR(r.draw(fx.d.ctx, rt.rtv, nullptr, cam, o, &st), Err::BadLayout);
    o.tessFactor = std::nanf("");
    CHECK_ERR(r.draw(fx.d.ctx, rt.rtv, nullptr, cam, o, &st), Err::BadLayout);
    // issue 0191: the LOD options (lodMip is gone; any level 0..4 is drawn now)
    for (int bad : {-2, kMaxLodLevel + 1, 99}) {
        o = Options();
        o.lodLevel = bad;
        CHECK_ERR(r.draw(fx.d.ctx, rt.rtv, nullptr, cam, o, &st), Err::BadLayout);
    }
    o = Options();
    o.lodTriSize = 0;
    CHECK_ERR(r.draw(fx.d.ctx, rt.rtv, nullptr, cam, o, &st), Err::BadLayout);
    o = Options();
    o.lodSubdiv = 65;
    CHECK_ERR(r.draw(fx.d.ctx, rt.rtv, nullptr, cam, o, &st), Err::BadLayout);
    CHECK_EQ(r.lodState().value, 0.0f);   // failed draws leave the LOD state alone
    CHECK_EQ(r.lodState().lodMip, 0.0f);
    o = Options();
    o.tileD4 = 1025;
    CHECK_ERR(r.draw(fx.d.ctx, rt.rtv, nullptr, cam, o, &st), Err::BadLayout);
    o = Options();
    o.color = ColorSource::WorldPos;
    CHECK_ERR(r.draw(fx.d.ctx, rt.rtv, nullptr, cam, o, &st), Err::BadLayout);         // WorldPos into RGBA8
    CHECK_EQ(st.tilesDrawn, 99u);
    CHECK_OK(r.draw(fx.d.ctx, rt.rtv, nullptr, cam, Options(), &st));
    CHECK(st.tilesDrawn > 0 && st.passUsed == -1 && st.tessFactorUsed == 2.0f);   // -1: per tile (issue 0189)
}

// The surface: the world position read back at every pixel lies on the EXE's bicubic surface of the field (across the
// tile boundaries x = 0 and y = 0 of this field), and every pixel is covered.
C5R_TEST(draw_ramp_matches_the_patch_surface) {
    Fixture fx;
    if (!fx.ok) return;
    heightfield::HeightGrid g = makeGrid(4, 4, ramp);
    CHECK_OK(fx.r.setHeightField(g));
    Camera cam = downCamera(10, 5, 120, 160, 120);
    Options o;
    o.color = ColorSource::Constant;
    o.water = false;
    o.lodLevel = 0;   // issue 0191: the level-0 surface (the EXE's rule would pick L = 3 for this small window)
    terrainshot::ShotResult res;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, true, res));
    CHECK_EQ(res.stats.tilesDrawn, 4u);
    size_t covered = 0, bad = 0;
    double worst = 0;
    for (size_t i = 0; i < static_cast<size_t>(res.width) * res.height; ++i) {
        const float* w = &res.world[i * 4];
        if (w[3] != 1.0f) continue;
        ++covered;
        const double c = (w[0] + g.ox) / g.sx, r = (w[1] + g.oy) / g.sy;
        // tolerance 0.01: the ramp rises 0.2 per sample since 0134 (whole bytes), and the read-back x carries about
        // 0.03 samples of interpolation error (worst 0.0011 at the old 0.031 per sample, 0.0048 at 0.2, 2026-11-13)
        const double d = std::fabs(w[2] - surface(g, c, r));
        worst = std::max(worst, d);
        if (d > 0.01 && ++bad <= 5)
            std::printf("  pixel %zu: world (%.3f, %.3f, %.4f), surface %.4f at sample (%.2f, %.2f)\n", i, w[0], w[1], w[2],
                        surface(g, c, r), c, r);
    }
    std::printf("  ramp: %zu of %u px covered, worst |z - surface| %.5f\n", covered, res.width * res.height, worst);
    CHECK_EQ(covered, static_cast<size_t>(res.width) * res.height);
    CHECK_EQ(bad, size_t(0));
    // the colour pass drew the constant colour everywhere
    CHECK(nearly(res.rgba[0], 0x80, 1) && nearly(res.rgba[1], 0x80, 1) && nearly(res.rgba[2], 0x80, 1));
    // and the heights are the CPU formula at the samples: z at a pixel close to a sample centre
    double ms = -1;
    CHECK_OK(fx.r.gpuTime(fx.d.ctx, true, &ms));
    CHECK(ms >= 0);
}

C5R_TEST(draw_wraps_in_x) {
    Fixture fx;
    if (!fx.ok) return;
    // periodic in x over the 256 samples, so the surface is smooth across the seam (a ramp would put a cliff there,
    // where the tessellated triangles depart from the cubic surface by more than the tolerance)
    heightfield::HeightGrid g = makeGrid(4, 4, [](uint32_t c, uint32_t r) {
        return static_cast<uint16_t>(6000.0 + 3000.0 * std::sin(6.283185307179586 * c / 256.0) + 20.0 * r);
    }, true);
    CHECK_OK(fx.r.setHeightField(g));
    Camera cam = downCamera(128, 0, 120, 160, 120);  // on the east edge x = +128: half the view is the wrap copy
    Options o;
    o.color = ColorSource::Constant;
    o.water = false;
    o.tessFactor = 8;  // the drawn heights are whole bytes (0.2 steps): fine triangles follow the cubic within 0.005
    o.lodLevel = 0;    // issue 0191: the level-0 surface
    terrainshot::ShotResult res;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, true, res));
    // issue 4314: copy +1 lies at the EXE's wrap period, (samplesX - 1) * sx = 255 here, so its sample c sits at world
    // x = c * sx - ox + 255; the column [255, 256) is drawn by both copies (copy 0's last interval, copy +1's first), so a
    // pixel there lies on one of the two surfaces. At the old shift 256 every copy +1 pixel is one sample off (about 0.06
    // world units of height on this sine, over the 0.005 allowed).
    const double P = wrapPeriod(g.samplesX, g.sx) / g.sx;
    CHECK(P == 255.0);
    size_t covered = 0, beyond = 0, bad = 0;
    for (size_t i = 0; i < static_cast<size_t>(res.width) * res.height; ++i) {
        const float* w = &res.world[i * 4];
        if (w[3] != 1.0f) continue;
        ++covered;
        double c = (w[0] + g.ox) / g.sx;
        const double r = (w[1] + g.oy) / g.sy;
        double s = surface(g, c, r);
        if (c >= P) {
            ++beyond;
            const double s1 = surface(g, c - P, r);
            if (c >= g.samplesX || std::fabs(w[2] - s1) < std::fabs(w[2] - s)) {
                s = s1;
                c -= P;
            }
        }
        if (std::fabs(w[2] - s) > 0.005 && ++bad <= 5)
            std::printf("  pixel %zu: world (%.3f, %.3f, %.4f), surface %.4f at sample (%.2f, %.2f)\n", i, w[0], w[1], w[2],
                        s, c, r);
    }
    std::printf("  wrap: %zu covered, %zu beyond x = +128, %zu off the surface\n", covered, beyond, bad);
    CHECK_EQ(covered, static_cast<size_t>(res.width) * res.height);
    CHECK(beyond > covered / 4);
    CHECK_EQ(bad, size_t(0));
}

C5R_TEST(draw_water_plane_and_culling) {
    Fixture fx;
    if (!fx.ok) return;
    CHECK_OK(fx.r.setHeightField(makeGrid(4, 4, [](uint32_t, uint32_t) { return uint16_t(0); })));
    Camera cam = downCamera(0, 0, 120, 64, 48);
    Options o;
    o.color = ColorSource::Constant;
    terrainshot::ShotResult res;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, true, res));
    size_t water = 0;
    for (size_t i = 0; i < static_cast<size_t>(res.width) * res.height; ++i)
        if (res.world[i * 4 + 3] == 2.0f && std::fabs(res.world[i * 4 + 2] - 10.117f) < 1e-4f) ++water;
    CHECK_EQ(water, static_cast<size_t>(res.width) * res.height);  // water (z 10.117) covers the field at z 0
    // the water colour: R in the low byte of 0xFF6E4A2A
    CHECK(nearly(res.rgba[0], 0x2A, 1) && nearly(res.rgba[1], 0x4A, 1) && nearly(res.rgba[2], 0x6E, 1));
    o.waterZ = 5.5f;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, true, res));
    CHECK(std::fabs(res.world[2] - 5.5f) < 1e-4f);
    // a camera far off the map draws no tile
    Camera away = downCamera(5000, 0, 120, 64, 48);
    o.water = false;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, away, o, false, res));
    CHECK(res.stats.tilesDrawn == 0 && res.stats.tilesCulled == 4);
}

// Ticket 0707: the coarse cull (blocks of 8 x 8 tiles before the per-tile test) draws the same tiles in the same order,
// with the same counters and the same picture, as the per-tile test alone, over random cameras (tilted and straight down,
// near the wrap seam and off the map) on a wrapped field whose tile rows end in a partial block.
C5R_TEST(draw_coarse_cull_equals_the_tile_cull) {
    Fixture fx;
    if (!fx.ok) return;
    heightfield::HeightGrid g = makeGrid(40, 26, [](uint32_t c, uint32_t r) {
        return static_cast<uint16_t>(9000.0 + 6000.0 * std::sin(6.283185307179586 * c / 2560.0 * 3.0) *
                                                  std::cos(6.283185307179586 * r / 1664.0 * 2.0) + 7.0 * (r % 97));
    }, true);
    CHECK_OK(fx.r.setHeightField(g));
    uint32_t seed = 0x0707u;
    auto rnd = [&seed](float lo, float hi) {
        seed = seed * 1664525u + 1013904223u;
        return lo + (hi - lo) * static_cast<float>(seed >> 8) / 16777216.0f;
    };
    const float halfW = 40 * 32.0f, halfH = 26 * 32 * 0.866f;
    size_t compared = 0, withSkips = 0, mixed = 0;
    uint64_t skipped = 0;
    for (int i = 0; i < 48; ++i) {
        // eye over the map, at its east / west seam (a third of the cameras) or off it; tilted 0 .. 1.1 rad toward -y
        float ex = rnd(-halfW - 300, halfW + 300);
        if (i % 3 == 1) ex = (i % 2 ? halfW : -halfW) + rnd(-80, 80);
        const float ey = rnd(-halfH - 200, halfH + 400), ez = rnd(40, 900), th = (i % 4 == 0) ? 0.0f : rnd(0.05f, 1.1f);
        Camera cam = downCamera(0, 0, 1, 160, 120);
        const float c = std::cos(th), s = std::sin(th);
        const float v[16] = {1, 0, 0, 0, 0, c, s, 0, 0, -s, c, 0, -ex, -ey * c + ez * s, -ey * s - ez * c, 1};
        std::memcpy(cam.view, v, sizeof v);
        Options o;
        o.color = ColorSource::Height;
        o.water = false;
        o.lodLevel = 0;   // no LOD state carried from one draw to the next
        o.clearRgba = 0xFF102030u;
        terrainshot::ShotResult on, off;
        o.coarseCull = true;
        CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, true, on));
        o.coarseCull = false;
        CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, true, off));
        CHECK_EQ(off.stats.coarseSkipped, 0u);
        CHECK_EQ(on.stats.tilesDrawn, off.stats.tilesDrawn);
        CHECK_EQ(on.stats.tilesCulled, off.stats.tilesCulled);
        CHECK_EQ(on.stats.drawnOrderHash, off.stats.drawnOrderHash);
        CHECK_EQ(on.stats.tilesWithoutCache, off.stats.tilesWithoutCache);
        CHECK(on.rgba == off.rgba);
        CHECK(on.world.size() == off.world.size() &&
              std::memcmp(on.world.data(), off.world.data(), on.world.size() * sizeof(float)) == 0);
        ++compared;
        skipped += on.stats.coarseSkipped;
        if (on.stats.coarseSkipped > 0) ++withSkips;
        if (on.stats.coarseSkipped > 0 && on.stats.tilesDrawn > 0) ++mixed;
    }
    std::printf("  coarse cull: %zu cameras, %zu with blocks culled (%zu also drawing), %llu tile visits skipped\n", compared,
                withSkips, mixed, static_cast<unsigned long long>(skipped));
    CHECK(withSkips > compared / 2);   // the coarse path ran in most frames ...
    CHECK(mixed > compared / 4);       // ... and beside drawn tiles, not only for cameras off the map
}

// The EXE's pixel shader (pass 1) with a tile cache: with the module's constant inputs (overlay transparent, FOW
// visible, shadow off, fog off) the picture is the cache colour; tiles without a cache take the fallback.
C5R_TEST(draw_caches_through_the_exe_pixel_shader) {
    Fixture fx;
    if (!fx.ok) return;
    CHECK_OK(fx.r.setHeightField(makeGrid(4, 4, ramp)));
    ID3D11ShaderResourceView* red = solidTexture(fx.d.dev, 0xFF0000FFu);
    CHECK(red != nullptr);
    if (!red) return;
    TileCache tc;
    tc.color = red;
    CHECK_OK(fx.r.setTileCache(0, 0, tc));
    red->Release();  // the renderer holds its own reference
    Camera cam = downCamera(10, 5, 120, 160, 120);
    Options o;
    o.color = ColorSource::Caches;
    o.fallback = ColorSource::Constant;
    o.constantRgba = 0xFF00FF00u;
    o.water = false;
    terrainshot::ShotResult res;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, true, res));
    CHECK_EQ(res.stats.tilesWithoutCache, 3u);
    CHECK_EQ(res.stats.landTilesWithoutCache, 3u);   // the ramp is land in every tile (issue 0139)
    size_t redPx = 0, greenPx = 0, other = 0;
    for (size_t i = 0; i < static_cast<size_t>(res.width) * res.height; ++i) {
        const uint8_t* p = &res.rgba[i * 4];
        const bool inTile00 = res.world[i * 4] < 0 && res.world[i * 4 + 1] < 0;  // tile (0, 0): x < 0, y < 0
        if (nearly(p[0], 255, 1) && nearly(p[1], 0, 1) && nearly(p[2], 0, 1) && inTile00) ++redPx;
        else if (nearly(p[0], 0, 1) && nearly(p[1], 255, 1) && nearly(p[2], 0, 1) && !inTile00) ++greenPx;
        else ++other;
    }
    std::printf("  caches: red %zu, green %zu, other %zu\n", redPx, greenPx, other);
    CHECK(redPx > 1000 && greenPx > 1000);
    CHECK(other < 400);  // at most the boundary row and column
    CHECK_OK(fx.r.setTileCache(0, 0, TileCache()));  // clear
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, res));
    CHECK_EQ(res.stats.tilesWithoutCache, 4u);
    CHECK_EQ(res.stats.landTilesWithoutCache, 4u);
}

// Issue 0139: the gray-tile count of a draw counts LAND tiles without a cache only (the EXE's class rule: highest raw
// sample of the tile + 4 around it, u16 / 255 >= 2), each once also when the tile is drawn in two wrap copies; outside
// Caches it is 0.
C5R_TEST(draw_counts_land_tiles_without_a_cache) {
    Fixture fx;
    if (!fx.ok) return;
    // land only in the west half (samples c < 128): tiles x 0 and 1 are land, tiles x 2 and 3 water. Raw 509 = 1.996
    // (class 1); the margin rule: tile x 2 starts at c 128, its 4-sample margin reaches c 124 (land) -> make the land
    // stop at c 120 so tile 2 is water, and tile 1's own samples (64..127) hold land.
    heightfield::HeightGrid g = makeGrid(4, 4, [](uint32_t c, uint32_t) { return uint16_t(c < 120 ? 600 : 509); });
    CHECK_OK(fx.r.setHeightField(g, 1));   // 1-plot tiles (64 samples): 4 x 4 tiles, as the residency
    Camera cam = downCamera(0, 0, 500, 160, 120);   // the whole 4 x 4 field in view
    Options o;
    o.color = ColorSource::Caches;
    o.fallback = ColorSource::Constant;
    o.water = false;
    terrainshot::ShotResult res;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, res));
    std::printf("  land count: drawn %u, without cache %u, land without cache %u\n", res.stats.tilesDrawn,
                res.stats.tilesWithoutCache, res.stats.landTilesWithoutCache);
    CHECK_EQ(res.stats.tilesDrawn, 16u);
    CHECK_EQ(res.stats.tilesWithoutCache, 16u);
    CHECK_EQ(res.stats.landTilesWithoutCache, 8u);
    // a cache on one land tile and on one water tile: one land tile fewer
    ID3D11ShaderResourceView* red = solidTexture(fx.d.dev, 0xFF0000FFu);
    CHECK(red != nullptr);
    if (!red) return;
    TileCache tc;
    tc.color = red;
    CHECK_OK(fx.r.setTileCache(0, 0, tc));
    CHECK_OK(fx.r.setTileCache(3, 0, tc));
    red->Release();
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, res));
    CHECK_EQ(res.stats.landTilesWithoutCache, 7u);
    CHECK_EQ(res.stats.tilesWithoutCache, 14u);
    // not Caches: no gray tile, whatever the caches
    o.color = ColorSource::Constant;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, res));
    CHECK_EQ(res.stats.landTilesWithoutCache, 0u);
    // wrap: a land tile on the seam drawn in two copies counts once; all-land 4 x 4 wrapped field seen whole
    heightfield::HeightGrid w = makeGrid(4, 4, [](uint32_t, uint32_t) { return uint16_t(600); }, true);
    CHECK_OK(fx.r.setHeightField(w, 1));
    o.color = ColorSource::Caches;
    Camera seam = downCamera(128, 0, 600, 160, 120);   // the east edge: copies k = 0 and k = +1 both in view
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, seam, o, false, res));
    std::printf("  wrap: drawn %u (every copy), land without cache %u\n",
                res.stats.tilesDrawn, res.stats.landTilesWithoutCache);
    CHECK(res.stats.tilesDrawn > 16u);                   // some tiles drawn twice
    CHECK_EQ(res.stats.landTilesWithoutCache, 16u);      // each counted once
}

// Ticket 0665: with the caches as colour source and the material fallback (the product's default) an open-sea tile
// (not land, the EXE's height class 1) without a cache is not drawn: the EXE draws none, and its material colour (byte
// 37 = 150, 4, 222) showed through in the first frame after a far camera jump (fog-after-camera-jump.md). Land tiles
// without a cache and every tile under the diagnostic colour sources and fallbacks are still drawn.
C5R_TEST(draw_hides_open_sea_tiles_without_a_cache) {
    Fixture fx;
    if (!fx.ok) return;
    // as draw_counts_land_tiles_without_a_cache: tiles x 0 and 1 land, tiles x 2 and 3 open sea; material 37 everywhere
    heightfield::HeightGrid g = makeGrid(4, 4, [](uint32_t c, uint32_t) { return uint16_t(c < 120 ? 600 : 509); });
    g.mat.assign(g.h.size(), 37);
    CHECK_OK(fx.r.setHeightField(g, 1));
    const uint32_t h = uint32_t(g.mat[0]) * 2654435761u;   // the stored material (37), a runtime value (no C4127)
    CHECK(((h >> 8) & 255) == 150 && ((h >> 16) & 255) == 4 && ((h >> 24) & 255) == 222);   // the purple of 0663
    Camera cam = downCamera(0, 0, 500, 160, 120);   // the whole 4 x 4 field in view
    Options o;   // ColorSource::Caches, fallback Material: the product's defaults
    CHECK(o.color == ColorSource::Caches && o.fallback == ColorSource::Material);
    o.water = false;
    o.clearRgba = 0xFF545454u;   // the D33 clear colour (84, 84, 84)
    terrainshot::ShotResult res;
    const auto count = [&res](uint8_t r, uint8_t gr, uint8_t b) {
        size_t n = 0;
        for (size_t i = 0; i < static_cast<size_t>(res.width) * res.height; ++i) {
            const uint8_t* p = &res.rgba[i * 4];
            if (nearly(p[0], r, 1) && nearly(p[1], gr, 1) && nearly(p[2], b, 1)) ++n;
        }
        return n;
    };
    // the detector: Material as the colour source draws every tile purple
    o.color = ColorSource::Material;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, res));
    const size_t purpleAll = count(150, 4, 222);
    std::printf("  material source: drawn %u, purple %zu of %u px\n", res.stats.tilesDrawn, purpleAll,
                res.width * res.height);
    CHECK_EQ(res.stats.tilesDrawn, 16u);
    CHECK_EQ(res.stats.seaTilesHidden, 0u);
    CHECK(purpleAll > static_cast<size_t>(res.width) * res.height / 2);
    // the caches source, no cache anywhere: the 8 land tiles drawn (purple), the 8 sea tiles hidden (the clear colour)
    o.color = ColorSource::Caches;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, res));
    const size_t purpleLand = count(150, 4, 222), clearPx = count(84, 84, 84);
    std::printf("  caches + material: drawn %u, hidden %u, without cache %u, land without cache %u; purple %zu, clear %zu\n",
                res.stats.tilesDrawn, res.stats.seaTilesHidden, res.stats.tilesWithoutCache,
                res.stats.landTilesWithoutCache, purpleLand, clearPx);
    CHECK_EQ(res.stats.tilesDrawn, 8u);
    CHECK_EQ(res.stats.seaTilesHidden, 8u);
    CHECK_EQ(res.stats.tilesWithoutCache, 16u);      // the hidden tiles still count as tiles without a cache
    CHECK_EQ(res.stats.landTilesWithoutCache, 8u);
    CHECK(purpleLand > 1000 && purpleLand < purpleAll * 3 / 4);   // about half the picture (the land half)
    CHECK(clearPx > 1000 && purpleLand + clearPx + 400 >= static_cast<size_t>(res.width) * res.height);
    // a cache on a sea tile: drawn with it, one tile fewer hidden
    ID3D11ShaderResourceView* red = solidTexture(fx.d.dev, 0xFF0000FFu);
    CHECK(red != nullptr);
    if (!red) return;
    TileCache tc;
    tc.color = red;
    CHECK_OK(fx.r.setTileCache(3, 0, tc));
    red->Release();
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, res));
    CHECK_EQ(res.stats.tilesDrawn, 9u);
    CHECK_EQ(res.stats.seaTilesHidden, 7u);
    CHECK(count(255, 0, 0) > 100);
    CHECK_OK(fx.r.setTileCache(3, 0, TileCache()));
    // the no-sea case: 0 purple pixels in a sea-only view (camera over tiles x 2 and 3)
    Camera sea = downCamera(64, 0, 60, 160, 120);
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, sea, o, false, res));
    std::printf("  sea view: drawn %u, hidden %u, purple %zu\n", res.stats.tilesDrawn, res.stats.seaTilesHidden,
                count(150, 4, 222));
    CHECK(res.stats.seaTilesHidden > 0);
    CHECK_EQ(count(150, 4, 222), size_t(0));
    // the diagnostic fallbacks and sources keep drawing the sea tiles
    for (ColorSource f : {ColorSource::Constant, ColorSource::Height, ColorSource::Normal}) {
        o.fallback = f;
        CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, res));
        CHECK_EQ(res.stats.tilesDrawn, 16u);
        CHECK_EQ(res.stats.seaTilesHidden, 0u);
    }
    o.fallback = ColorSource::Material;
    o.color = ColorSource::Constant;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, res));
    CHECK_EQ(res.stats.tilesDrawn, 16u);
    CHECK_EQ(res.stats.seaTilesHidden, 0u);
}

C5R_TEST(draw_material_and_debug_colours) {
    Fixture fx;
    if (!fx.ok) return;
    heightfield::HeightGrid g = makeGrid(4, 4, ramp);
    g.mat.assign(g.h.size(), 7);
    CHECK_OK(fx.r.setHeightField(g));
    Camera cam = downCamera(10, 5, 120, 64, 48);
    Options o;
    o.color = ColorSource::Material;
    o.water = false;
    terrainshot::ShotResult res;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, res));
    const uint32_t h = 7u * 2654435761u;
    CHECK(nearly(res.rgba[0], (h >> 8) & 255, 1) && nearly(res.rgba[1], (h >> 16) & 255, 1) &&
          nearly(res.rgba[2], (h >> 24) & 255, 1));
    for (ColorSource c : {ColorSource::Height, ColorSource::Normal}) {
        o.color = c;
        CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, res));
        CHECK(res.rgba[3] == 255);
    }
}

// The offline draw on real data: epic 3000's Tiny ground truth (C5R_GT, default D:/c5-64bit-work/i2-ground-truth) at
// the v1 camera, full back-buffer size. Missing ground truth is a FAILURE with the path, not a skip (brief of 3202):
// the definition of done needs this test to run. Checks: most of the picture is terrain or water, both appear, and at
// 2,000 terrain pixels spread over the image the world z read back equals the EXE's patch surface of the real field.
C5R_TEST(game_offline_draw_tiny_ground_truth) {
    Fixture fx;
    if (!fx.ok) return;
    const char* e = std::getenv("C5R_GT");
    const std::string root = e && *e ? e : "D:/c5-64bit-work/i2-ground-truth";
    const std::string dir = root + "/tiny";
    if (GetFileAttributesA((dir + "/heightmap_info.json").c_str()) == INVALID_FILE_ATTRIBUTES) {
        test::fail(__FILE__, __LINE__, "ground truth missing: " + dir + " (set C5R_GT to the folder of epic 3000's "
                                                                       "i2-ground-truth)");
        return;
    }
    heightfield::HeightGrid g;
    CHECK_OK(terrainshot::loadGroundTruth(wide(dir), g));
    CHECK(g.mapW == 56 && g.mapH == 40 && g.wrapX);
    Camera cam;
    CHECK_OK(terrainshot::loadCamera(wide(dir + "/camera_v1_city_default_after.json"), 0, cam));
    CHECK(cam.width == 2554 && cam.height == 1411);
    CHECK_OK(fx.r.setHeightField(g));
    Options o;
    o.color = ColorSource::Normal;
    o.clearRgba = 0xFF545454u;
    o.tessFactor = 8;  // fine triangles: the read-back z is linear between tessellated vertices
    o.lodLevel = 0;    // issue 0191: the level-0 surface of surface() (the EXE's rule draws L >= 1 here)
    terrainshot::ShotResult res;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, true, res));
    if (res.world.empty()) return;
    size_t terrain = 0, water = 0, bad = 0, checked = 0;
    const size_t n = static_cast<size_t>(res.width) * res.height;
    for (size_t i = 0; i < n; ++i) {
        const float* w = &res.world[i * 4];
        if (w[3] == 2.0f) ++water;
        if (w[3] != 1.0f) continue;
        ++terrain;
        if (i % 1777 != 0) continue;
        ++checked;
        double c = (w[0] + g.ox) / g.sx;
        if (c < 0) c += g.samplesX;
        if (c >= g.samplesX) c -= g.samplesX;
        if (std::fabs(w[2] - surface(g, c, (w[1] + g.oy) / g.sy)) > 0.01) ++bad;
    }
    std::printf("  tiny v1: terrain %zu, water %zu of %zu px; %zu checked, %zu off the surface; tiles drawn %u culled %u;"
                " gpu %.3f ms\n", terrain, water, n, checked, bad, res.stats.tilesDrawn, res.stats.tilesCulled, res.gpuMs);
    CHECK(terrain + water > n * 9 / 10);
    CHECK(terrain > n / 10 && water > 0);
    CHECK(checked > 200);
    CHECK_EQ(bad, size_t(0));
}

// ---------------------------------------------------------------- issue 0134: the height the EXE draws

// The rule itself, no device: the high byte times sz; HeightAtGrid's wrap and outside-is-0.
C5R_TEST(drawn_height_is_the_high_byte) {
    heightfield::HeightGrid g = makeGrid(2, 2, [](uint32_t c, uint32_t) { return static_cast<uint16_t>(c == 5 ? 15325 : 255); },
                                         true);
    CHECK(std::fabs(drawnHeightAtSample(g, 5, 7) - 59 * 0.2f) < 1e-5f);     // 15325 = 59 * 256 + 221
    CHECK(std::fabs(heightfield::heightAtSample(g, 5, 7) - 12.0196f) < 1e-3f);  // where the EXE places models
    CHECK(drawnHeightAtSample(g, 6, 7) == 0.0f);                              // 255: high byte 0
    CHECK(drawnHeightAtSample(g, 5 - 128, 7) == drawnHeightAtSample(g, 5, 7));  // wrap in x
    CHECK(drawnHeightAtSample(g, 5, -1) == 0.0f);                             // outside in y: 0
    CHECK(drawnHeightAtSample(g, 5, 128) == 0.0f);
    heightfield::HeightGrid empty;
    CHECK(drawnHeightAtSample(empty, 0, 0) == 0.0f);
}

// A city plot on flat land as on the huge save (Thebes and Memphis: u16 about 15,325, HeightAtGrid z 12.02): the drawn
// surface is the EXE's 11.8 (byte 59 * 0.2), not 12.02. Before issue 0134 the module drew 12.02 here, 0.22 world units
// above the EXE's terrain, and cut the bases of the EXE's city models (bug 0134).
C5R_TEST(draw_city_plot_at_the_exe_drawn_height) {
    Fixture fx;
    if (!fx.ok) return;
    heightfield::HeightGrid g = makeGrid(4, 4, [](uint32_t, uint32_t) { return uint16_t(15325); });
    CHECK_OK(fx.r.setHeightField(g));
    Camera cam = downCamera(0, 0, 60, 96, 72);
    Options o;
    o.color = ColorSource::Constant;
    o.water = false;
    terrainshot::ShotResult res;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, true, res));
    size_t covered = 0, off = 0;
    double zmin = 1e9, zmax = -1e9;
    for (size_t i = 0; i < static_cast<size_t>(res.width) * res.height; ++i) {
        const float* w = &res.world[i * 4];
        if (w[3] != 1.0f) continue;
        ++covered;
        zmin = std::min(zmin, static_cast<double>(w[2]));
        zmax = std::max(zmax, static_cast<double>(w[2]));
        if (std::fabs(w[2] - 11.8) > 0.001) ++off;
    }
    std::printf("  city plot: %zu px, z %.5f .. %.5f (EXE 11.8; HeightAtGrid %.5f)\n", covered, zmin, zmax,
                heightfield::heightAtSample(g, 128, 128));
    CHECK_EQ(covered, static_cast<size_t>(res.width) * res.height);
    CHECK_EQ(off, size_t(0));
    CHECK(heightfield::heightAtSample(g, 128, 128) - zmax > 0.2);  // the old draw's height is not what is drawn now
}

// The surface under a real city plot: epic 3000's huge ground truth (C5R_GT; the stock dump of C5H-VP8P-HUGE_0350, turn
// 350, 2026-11-11) at its v2 camera (closest zoom at Memphis (100, 30)). Over the terrain pixels within 24 samples of
// Memphis' plot centre (sample (6432, 2048), from gt_reader.hex_to_world by shell/evidence/0134/height_offset_0134.py),
// the world z read back lies on the cubic surface of the drawn heights within 0.01, and on average 0.15-0.25 world
// units below the cubic surface of HeightAtGrid's heights (the script's figure for this plot: 0.194).
C5R_TEST(game_surface_under_a_city_plot_huge) {
    Fixture fx;
    if (!fx.ok) return;
    const char* e = std::getenv("C5R_GT");
    const std::string root = e && *e ? e : "D:/c5-64bit-work/i2-ground-truth";
    const std::string dir = root + "/huge";
    if (GetFileAttributesA((dir + "/heightmap_info.json").c_str()) == INVALID_FILE_ATTRIBUTES) {
        test::fail(__FILE__, __LINE__, "ground truth missing: " + dir + " (set C5R_GT to the folder of epic 3000's "
                                                                       "i2-ground-truth)");
        return;
    }
    heightfield::HeightGrid g;
    CHECK_OK(terrainshot::loadGroundTruth(wide(dir), g));
    CHECK(g.mapW == 128 && g.mapH == 84 && g.wrapX);
    Camera cam;
    CHECK_OK(terrainshot::loadCamera(wide(dir + "/camera_v2_city_zoomin_after.json"), 0, cam));
    CHECK_OK(fx.r.setHeightField(g));
    Options o;
    o.color = ColorSource::Constant;
    o.water = false;
    o.tessFactor = 8;
    o.lodLevel = 0;   // issue 0191: the level-0 surface of surface()
    terrainshot::ShotResult res;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, true, res));
    if (res.world.empty()) return;
    const double cx = 6432, cy = 2048, rad = 24;
    size_t inPlot = 0, bad = 0;
    double sumBelow = 0, minBelow = 1e9;
    for (size_t i = 0; i < static_cast<size_t>(res.width) * res.height; i += 7) {
        const float* w = &res.world[i * 4];
        if (w[3] != 1.0f) continue;
        const double c = (w[0] + g.ox) / g.sx, r = (w[1] + g.oy) / g.sy;
        if ((c - cx) * (c - cx) + (r - cy) * (r - cy) > rad * rad) continue;
        ++inPlot;
        if (std::fabs(w[2] - surface(g, c, r)) > 0.01) ++bad;
        const double below = surface(g, c, r, true) - w[2];
        sumBelow += below;
        minBelow = std::min(minBelow, below);
    }
    const double meanBelow = inPlot ? sumBelow / static_cast<double>(inPlot) : 0;
    std::printf("  Memphis plot: %zu px sampled, %zu off the drawn surface; below HeightAtGrid's surface by %.4f on average"
                " (min %.4f)\n", inPlot, bad, meanBelow, minBelow);
    CHECK(inPlot > 1000);
    CHECK_EQ(bad, size_t(0));
    CHECK(meanBelow > 0.15 && meanBelow < 0.25);
    CHECK(minBelow > 0.0);
}

// ---------------------------------------------------------------- the overlay input (epic 0167, issue 3704)

namespace {
// A w x h texture of 32-bit texels (R in the low byte), one mip, of format f (R8G8B8A8_UNORM unless a test wants another).
ID3D11ShaderResourceView* pixelTexture(ID3D11Device* dev, uint32_t w, uint32_t h, const std::vector<uint32_t>& px,
                                       DXGI_FORMAT f = DXGI_FORMAT_R8G8B8A8_UNORM) {
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = w;
    d.Height = h;
    d.MipLevels = d.ArraySize = 1;
    d.Format = f;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA s = {px.data(), w * 4, 0};
    ID3D11Texture2D* t = nullptr;
    ID3D11ShaderResourceView* v = nullptr;
    if (FAILED(dev->CreateTexture2D(&d, &s, &t))) return nullptr;
    dev->CreateShaderResourceView(t, nullptr, &v);
    t->Release();
    return v;
}
// Every tile of the current field gets the same constant cache (the EXE's pixel shader runs only on tiles with one).
void cacheEveryTile(Renderer& r, ID3D11ShaderResourceView* c) {
    const TileGrid tg = r.tileGrid();
    TileCache tc;
    tc.color = c;
    for (uint32_t y = 0; y < tg.tilesY; ++y)
        for (uint32_t x = 0; x < tg.tilesX; ++x) r.setTileCache(x, y, tc);
}
// An OverlayProj whose NDC x is x * sx + tx (y, z, w as identity).
void projX(float m[16], float sx, float tx) {
    const float v[16] = {sx, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, tx, 0, 0, 1};
    std::memcpy(m, v, sizeof v);
}
}  // namespace

// Done G (render): a missing overlay is the transparent default; a bad OverlayProj and a bad overlay view are named
// errors that leave the stats untouched; a NaN camera stays an error with an overlay bound.
C5R_TEST(negative_overlay_checks) {
    Fixture fx;
    if (!fx.ok) return;
    Renderer& r = fx.r;
    CHECK_OK(r.setHeightField(makeGrid(4, 4, ramp)));
    Target rt(fx.d.dev, 64, 48, DXGI_FORMAT_R8G8B8A8_UNORM);
    Camera cam = downCamera(0, 0, 100, 64, 48);
    FrameStats st;
    st.tilesDrawn = 99;
    for (int k = 0; k < 3; ++k) {
        Options o;
        o.overlay.proj[k][13] = std::nanf("");   // without a view too: the matrices are always checked
        CHECK_ERR(r.draw(fx.d.ctx, rt.rtv, nullptr, cam, o, &st), Err::BadLayout);
        o.overlay.proj[k][13] = INFINITY;
        CHECK_ERR(r.draw(fx.d.ctx, rt.rtv, nullptr, cam, o, &st), Err::BadLayout);
    }
    ID3D11ShaderResourceView* flt = pixelTexture(fx.d.dev, 2, 2, std::vector<uint32_t>(4, 0), DXGI_FORMAT_R32_FLOAT);
    ID3D11ShaderResourceView* bgra = pixelTexture(fx.d.dev, 2, 2, std::vector<uint32_t>(4, 0), DXGI_FORMAT_B8G8R8A8_UNORM);
    ID3D11ShaderResourceView* good = pixelTexture(fx.d.dev, 2, 2, std::vector<uint32_t>(4, 0));
    CHECK(flt && bgra && good);
    if (!flt || !bgra || !good) return;
    Options o;
    o.overlay.srv = flt;
    CHECK_ERR(r.draw(fx.d.ctx, rt.rtv, nullptr, cam, o, &st), Err::BadLayout);   // not R8G8B8A8_UNORM
    o.overlay.srv = bgra;
    CHECK_ERR(r.draw(fx.d.ctx, rt.rtv, nullptr, cam, o, &st), Err::BadLayout);
    o.overlay.srv = good;
    Camera c = cam;
    c.view[0] = std::nanf("");
    CHECK_ERR(r.draw(fx.d.ctx, rt.rtv, nullptr, c, o, &st), Err::BadLayout);   // NaN camera with an overlay bound
    CHECK_EQ(st.tilesDrawn, 99u);
    CHECK_OK(r.draw(fx.d.ctx, rt.rtv, nullptr, cam, o, &st));
    CHECK(st.overlayBound && st.tilesDrawn > 0);
    CHECK_OK(r.draw(fx.d.ctx, rt.rtv, nullptr, cam, Options(), &st));            // missing overlay: the default
    CHECK(!st.overlayBound);
    flt->Release();
    bgra->Release();
    good->Release();
}

// The default is the picture before 3704, bit for bit: no overlay against an explicit transparent overlay under any
// matrices, on a wrapping field with caches (the EXE's pixel shader path); overlayProjForCopy's identity.
C5R_TEST(draw_overlay_default_is_bit_equal) {
    Fixture fx;
    if (!fx.ok) return;
    CHECK_OK(fx.r.setHeightField(makeGrid(4, 4, ramp, true)));
    ID3D11ShaderResourceView* cache = solidTexture(fx.d.dev, 0xFF3080C0u);
    ID3D11ShaderResourceView* clear = pixelTexture(fx.d.dev, 4, 4, std::vector<uint32_t>(16, 0));
    CHECK(cache && clear);
    if (!cache || !clear) return;
    cacheEveryTile(fx.r, cache);
    Camera cam = downCamera(120, 0, 120, 160, 120);   // with the wrap copy in view
    Options o;
    o.water = false;
    terrainshot::ShotResult a, b;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, a));
    o.overlay.srv = clear;
    projX(o.overlay.proj[0], 0.013f, 0.5f);
    projX(o.overlay.proj[1], 0.007f, -0.25f);
    projX(o.overlay.proj[2], -0.02f, 3.0f);
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, b));
    CHECK(b.stats.overlayBound && !a.stats.overlayBound);
    CHECK(a.rgba.size() == b.rgba.size() && a.rgba == b.rgba);
    size_t diff = 0;
    for (size_t i = 0; i < a.rgba.size() && i < b.rgba.size(); ++i) diff += a.rgba[i] != b.rgba[i];
    std::printf("  default against a transparent overlay: %zu bytes differ\n", diff);
    float m[16];
    Overlay none;
    overlayProjForCopy(none, 1, 256.0f, m);
    CHECK(m[0] == 1 && m[5] == 1 && m[10] == 1 && m[15] == 1 && m[12] == 0 && m[13] == 0);
    cache->Release();
    clear->Release();
}

// The composition, premultiplied as the EXE's PS does it (terrain-surface.md 1): colour * (1 - a) + rgb.
C5R_TEST(draw_overlay_composes_premultiplied) {
    Fixture fx;
    if (!fx.ok) return;
    CHECK_OK(fx.r.setHeightField(makeGrid(4, 4, ramp)));
    ID3D11ShaderResourceView* cache = solidTexture(fx.d.dev, 0xFF3264C8u);                                 // (200, 100, 50)
    ID3D11ShaderResourceView* ovl = pixelTexture(fx.d.dev, 4, 4, std::vector<uint32_t>(16, 0x80002040u));  // (64, 32, 0, 128)
    CHECK(cache && ovl);
    if (!cache || !ovl) return;
    cacheEveryTile(fx.r, cache);
    Camera cam = downCamera(10, 5, 120, 64, 48);
    Options o;
    o.water = false;
    o.overlay.srv = ovl;
    terrainshot::ShotResult res;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, res));
    const double a = 128.0 / 255.0;
    const int want[3] = {static_cast<int>(std::lround(200 * (1 - a) + 64)), static_cast<int>(std::lround(100 * (1 - a) + 32)),
                         static_cast<int>(std::lround(50 * (1 - a)))};
    size_t good = 0;
    const size_t n = static_cast<size_t>(res.width) * res.height;
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* p = &res.rgba[i * 4];
        if (nearly(p[0], want[0], 2) && nearly(p[1], want[1], 2) && nearly(p[2], want[2], 2)) ++good;
    }
    std::printf("  premultiplied: %zu of %zu px = (%d, %d, %d) +-2; pixel 0 (%d, %d, %d)\n", good, n, want[0], want[1], want[2],
                res.rgba[0], res.rgba[1], res.rgba[2]);
    CHECK_EQ(good, n);
    cache->Release();
    ovl->Release();
}

// The wrap copies: the camera's copy samples with proj[1], the copy at +W with Translation(-W) * proj[2], i.e. at the
// texels the EXE's wrap view gives the original ground (overlay-system.md 4). A 256 x 1 ramp (R = texel index, opaque)
// makes the sampled u readable per pixel; the world x is read back. u = 0.5 * NDC.x + 0.5 is the DS's mapping, checked
// here (a flipped or an unshifted mapping fails every pixel).
C5R_TEST(draw_overlay_wrap_copies_take_their_view_matrix) {
    Fixture fx;
    if (!fx.ok) return;
    heightfield::HeightGrid g = makeGrid(4, 4, [](uint32_t, uint32_t) { return uint16_t(0); }, true);
    CHECK_OK(fx.r.setHeightField(g));
    std::vector<uint32_t> ramp256(256);
    for (uint32_t i = 0; i < 256; ++i) ramp256[i] = 0xFF000000u | i;
    ID3D11ShaderResourceView* cache = solidTexture(fx.d.dev, 0xFF808080u);
    ID3D11ShaderResourceView* ovl = pixelTexture(fx.d.dev, 256, 1, ramp256);
    CHECK(cache && ovl);
    if (!cache || !ovl) return;
    cacheEveryTile(fx.r, cache);
    const float W = wrapPeriod(g.samplesX, g.sx);            // 255: the module's wrap shift (issue 4314; 256 before)
    Camera cam = downCamera(128, 0, 120, 160, 120);          // the east edge: x in about [85, 171]
    Options o;
    o.water = false;
    o.overlay.srv = ovl;
    projX(o.overlay.proj[0], 0.0f, 100.0f);                  // copy -1: not in view
    projX(o.overlay.proj[1], 1.0f / 64, -1.0f);              // copy 0: u = x / 128
    projX(o.overlay.proj[2], 1.0f / 64, 1.0f);               // copy +1, applied to the original x - W: u = (x - W + 128) / 128
    terrainshot::ShotResult res;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, true, res));
    size_t n0 = 0, bad0 = 0, n1 = 0, bad1 = 0;
    for (size_t i = 0; i < static_cast<size_t>(res.width) * res.height; ++i) {
        const float x = res.world[i * 4];
        if (res.world[i * 4 + 3] != 1.0f) continue;
        double u;
        bool copy1;
        if (x > 90 && x < 125) {
            u = x / 128.0;
            copy1 = false;
        } else if (x > 131 && x < 166) {
            u = (x - W + 128) / 128.0;
            copy1 = true;
        } else {
            continue;
        }
        const double want = u * 256 - 0.5;   // linear filtering between texel centres
        const bool ok = std::fabs(res.rgba[i * 4] - want) <= 2.0;
        (copy1 ? n1 : n0)++;
        if (!ok) {
            (copy1 ? bad1 : bad0)++;
            if (bad0 + bad1 <= 5)
                std::printf("  pixel %zu: x %.2f R %d want %.1f (copy %d)\n", i, x, res.rgba[i * 4], want, copy1 ? 1 : 0);
        }
    }
    std::printf("  wrap overlay: copy 0 %zu px (%zu off), copy +1 %zu px (%zu off)\n", n0, bad0, n1, bad1);
    CHECK(n0 > 1000 && n1 > 1000);
    CHECK_EQ(bad0, size_t(0));
    CHECK_EQ(bad1, size_t(0));
    float m[16];
    overlayProjForCopy(o.overlay, 1, W, m);
    CHECK(std::fabs(m[12] - (1.0f - W / 64)) < 1e-5f && m[0] == o.overlay.proj[2][0]);
    overlayProjForCopy(o.overlay, 0, W, m);
    CHECK(std::memcmp(m, o.overlay.proj[1], sizeof m) == 0);   // the camera's view: the bytes as given
    cache->Release();
    ovl->Release();
}

// ---- issue 4314: the wrap period ---------------------------------------------------------------------------------

namespace {
float hexFloat(uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
// A row-major matrix from 16 IEEE bit patterns.
void hexMatrix(const uint32_t (&u)[16], float out[16]) {
    for (int i = 0; i < 16; ++i) out[i] = hexFloat(u[i]);
}
// max |a - b| over the 16 elements, relative to the largest |b|.
double relDiff(const float a[16], const float b[16]) {
    double d = 0, m = 0;
    for (int i = 0; i < 16; ++i) {
        d = std::max(d, std::fabs(static_cast<double>(a[i]) - b[i]));
        m = std::max(m, std::fabs(static_cast<double>(b[i])));
    }
    return m > 0 ? d / m : d;
}
} // namespace

// The period is the EXE's: MapSetup 0x535BF0 stores float(64 * mapW - 1) at 0xD90FE4 [C], 3583 on Tiny (56 plots) and
// 8191 on Huge (128) as read live (camera-and-picking.md 9; task 4302's probe: grid wrap_period 3583.0, world box
// -+1791.5), bit for bit; one unit less than the sample span samplesX * sx.
C5R_TEST(wrap_period_is_the_exes) {
    for (uint32_t mapW : {40u, 56u, 66u, 80u, 104u, 128u}) {
        const float want = static_cast<float>(static_cast<int>(64 * mapW) - 1);   // the EXE's shl 6, dec, cvtsi2ss
        const float got = wrapPeriod(mapW * 64, 1.0f);
        CHECK(std::memcmp(&got, &want, 4) == 0);
        CHECK(got != static_cast<float>(mapW * 64));
    }
    CHECK(wrapPeriod(3584, 1.0f) == 3583.0f);
    CHECK(wrapPeriod(8192, 1.0f) == 8191.0f);
    CHECK(-0.5f * wrapPeriod(3584, 1.0f) == -1791.5f && 0.5f * wrapPeriod(3584, 1.0f) == 1791.5f);   // 0xD90FCC / D8
    CHECK(wrapPeriod(256, 1.0f) == 255.0f);
    CHECK(wrapPeriod(0, 1.0f) == 0.0f);
}

// The module's copy matrix against an EXE view pair at the seam (task 4302, terrain-surface.md 11 S3; probe reads
// 03_seam and 05_seam_east, 2026-11-15, Tiny `C5H-0169-4302-TINY_0009V`, zoom 14.5, two views each): view 1 shows the
// EXE's own, unshifted ground moved by k W (k = -1 with the camera west of the centre, +1 east), so the module, which
// draws copy k at TileWorld + k * wrapPeriod through view 0 and binds Translation(-k * wrapPeriod) * matrix[k + 1], must
// get view 0's matrix back for that ground: within 1e-5 relative of the largest element (seen: 1.2e-7 shadow, 2.5e-6 and
// 3.7e-6 overlay, whose view 1 row 3 z is 0.50001). The module's period before 4314 (3584 = samplesX * sx) misses by one
// unit times row 0: 5.6e-4 to 5.8e-4, which this test also shows. ShadowMatrix0 0xDA1790 + 0x40 v and OverlayProj 0xDA1690 +
// 0x40 v, row-major as stored (numbers only, D83).
C5R_TEST(wrap_copy_matrix_equals_the_exe_view_pair) {
    struct Pair {
        const char* what;
        int k;
        uint32_t v0[16], v1[16];
    };
    static const Pair pairs[] = {
        {"shadow, seam west (03_seam)", -1,
         {0x3b150cca, 0x39d02006, 0x36f22883, 0, 0, 0x3b614d51, 0xb68bcf65, 0, 0xba950cca, 0x3a502006, 0x37722883, 0,
          0x40802320, 0x3f15ff9d, 0x3edbe8bd, 0x3f800000},
         {0x3b150cc9, 0x39d02005, 0x36f22882, 0, 0, 0x3b614d50, 0xb68bcf64, 0, 0xba950cc9, 0x3a502005, 0x37722882, 0,
          0xc084a09f, 0xbf561e66, 0x3eceab77, 0x3f7fffff}},
        {"shadow, seam east (05_seam_east)", 1,
         {0x3b150c2a, 0x39d01f40, 0x36f22883, 0, 0, 0x3b614c7a, 0xb68bcf65, 0, 0xba950c2a, 0x3a501f40, 0x37722883, 0,
          0xc080240e, 0xbf540daf, 0x3ecee85c, 0x3f800000},
         {0x3b150c29, 0x39d01f3f, 0x36f22882, 0, 0, 0x3b614c79, 0xb68bcf64, 0, 0xba950c29, 0x3a501f3f, 0x37722882, 0,
          0x40849e99, 0x3f180efe, 0x3edc25a0, 0x3f7fffff}},
        {"overlay, seam west (03_seam)", -1,
         {0x3b124925, 0, 0, 0, 0, 0x3b4ccccd, 0, 0, 0, 0, 0xbf000000, 0, 0x40800000, 0x80000000, 0x3f000000, 0x3f800000},
         {0x3b124924, 0, 0, 0, 0, 0x3b4ccccc, 0, 0, 0, 0, 0xbeffffff, 0, 0xc07fdb6d, 0x340a6667, 0x3f0000a6, 0x3f7fffff}},
        {"overlay, seam east (05_seam_east)", 1,
         {0x3b124925, 0, 0, 0, 0, 0x3b4ccccd, 0, 0, 0, 0, 0xbf000000, 0, 0xc076db6e, 0x80000000, 0x3f000000, 0x3f800000},
         {0x3b124924, 0, 0, 0, 0, 0x3b4ccccc, 0, 0, 0, 0, 0xbeffffff, 0, 0x40848000, 0x33d9999a, 0x3f0000f0, 0x3f7fffff}},
    };
    const float period = wrapPeriod(56 * 64, 1.0f);   // Tiny: 3583
    // the module only tests the view for null before it composes; this pointer is never used as a view
    ID3D11ShaderResourceView* const bound = reinterpret_cast<ID3D11ShaderResourceView*>(static_cast<uintptr_t>(16));
    for (const Pair& p : pairs) {
        float v0[16], v1[16], got[16], old[16];
        hexMatrix(p.v0, v0);
        hexMatrix(p.v1, v1);
        if (std::strncmp(p.what, "shadow", 6) == 0) {
            Shadow s;
            s.srv = bound;
            std::memcpy(s.matrix[1], v0, sizeof v0);
            std::memcpy(s.matrix[1 + p.k], v1, sizeof v1);
            shadowMatrixForCopy(s, p.k, period, got);
            shadowMatrixForCopy(s, p.k, 3584.0f, old);
            float zero[16];
            shadowMatrixForCopy(s, 0, period, zero);
            CHECK(std::memcmp(zero, v0, sizeof v0) == 0);   // the camera's copy: view 0's bytes
        } else {
            Overlay o;
            o.srv = bound;
            std::memcpy(o.proj[1], v0, sizeof v0);
            std::memcpy(o.proj[1 + p.k], v1, sizeof v1);
            overlayProjForCopy(o, p.k, period, got);
            overlayProjForCopy(o, p.k, 3584.0f, old);
        }
        const double rel = relDiff(got, v0), relOld = relDiff(old, v0);
        std::printf("  %s, k %+d: copy matrix vs view 0 %.2e relative at %.0f (%.2e at 3584)\n", p.what, p.k, rel, period,
                    relOld);
        CHECK(rel <= 1e-5);
        CHECK(relOld > 1e-4);   // the defect of 13.6 is visible here
    }
}

// ---- issue 0188: the EXE's distance fog --------------------------------------------------------------------------

namespace {

float bitsToFloat(uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
uint32_t floatBits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// TerrainShader cb2 (DistanceFog, 32 bytes) of every main-scene terrain draw and TerrainFrame.CameraPos in 3601's two
// stock RenderDoc captures (Tiny C5H-3600-TINY_0009V, c5h-c, 32-bit pin i0ship-26234371-ia, VT present), read on
// 2026-11-14 with render/evidence/0188/fog_block.py [L]: one distinct block per frame (150 and 58 draws).
struct FogCapture {
    const char* name;
    float camPos[3];
    uint32_t cb2[8];
};
const FogCapture kFogCaptures[2] = {
    {"cam1 (zoom 17)", {352.85369873046875f, -854.3394775390625f, 640.3153686523438f},
     {0x3f129293u, 0x3f36b6b7u, 0x3f73f3f5u, 0x3f800000u, 0xbd4ccccdu, 0xa365f52bu, 0x00000000u, 0x3f21af28u}},
    {"cam2 (zoom 11)", {1471.935791015625f, -518.599609375f, 261.0035400390625f},
     {0x3f129293u, 0x3f36b6b7u, 0x3f73f3f5u, 0x3f800000u, 0xbd4ccccdu, 0xb113ba1du, 0x00000000u, 0x00000000u}},
};

// Red channel of mip 0 of a 2D view (a 4-byte RGBA or BGRA format), row-major; d gets the texture's description.
std::vector<uint8_t> redMip0(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* v, D3D11_TEXTURE2D_DESC& d) {
    std::vector<uint8_t> out;
    ID3D11Resource* r = nullptr;
    v->GetResource(&r);
    ID3D11Texture2D* t = nullptr;
    if (!r || FAILED(r->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&t)))) {
        if (r) r->Release();
        return out;
    }
    t->GetDesc(&d);
    D3D11_TEXTURE2D_DESC s = d;
    s.Usage = D3D11_USAGE_STAGING;
    s.BindFlags = 0;
    s.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    s.MiscFlags = 0;
    ID3D11Texture2D* st = nullptr;
    if (SUCCEEDED(dev->CreateTexture2D(&s, nullptr, &st))) {
        ctx->CopyResource(st, t);
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
            const int red = d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS ? 2 : 0;
            for (uint32_t y = 0; y < d.Height; ++y)
                for (uint32_t x = 0; x < d.Width; ++x)
                    out.push_back(static_cast<const uint8_t*>(m.pData)[y * m.RowPitch + x * 4 + red]);
            ctx->Unmap(st, 0);
        }
        st->Release();
    }
    t->Release();
    r->Release();
    return out;
}

} // namespace

// The module's block equals the EXE's bytes at both captured cameras: members 0-6 bit for bit with the defaults; the ramp
// row w from the camera height is the zoom fraction to 4e-5 (zoom 17) and 2e-3 (zoom 11: the EXE's 0 is its target
// level, the camera at rest is 11.017) [I: the EXE writes its controller's target, 0xD90FAC]; with w given as the EXE's,
// all eight members are bit-equal.
C5R_TEST(fog_block_equals_the_exe_capture) {
    for (const FogCapture& c : kFogCaptures) {
        DistanceFog f;
        float b[8];
        distanceFogBlock(f, c.camPos, b);
        int equal = 0;
        for (int i = 0; i < 7; ++i) {
            equal += floatBits(b[i]) == c.cb2[i];
            if (floatBits(b[i]) != c.cb2[i])
                std::printf("  %s member %d: 0x%08x, the EXE 0x%08x\n", c.name, i, floatBits(b[i]), c.cb2[i]);
        }
        CHECK_EQ(equal, 7);
        const float wExe = bitsToFloat(c.cb2[7]);
        std::printf("  %s: w %.9g, the EXE %.9g; module block (bytes, for render/evidence/0188/replay_0188.py): ", c.name,
                    b[7], wExe);
        for (int i = 0; i < 32; ++i) std::printf("%02x", reinterpret_cast<const uint8_t*>(b)[i]);
        std::printf("\n");
        CHECK(std::fabs(b[7] - wExe) < (wExe > 0 ? 4e-5f : 2e-3f));
        f.w = wExe;
        distanceFogBlock(f, c.camPos, b);
        for (int i = 0; i < 8; ++i) CHECK_EQ(floatBits(b[i]), c.cb2[i]);
    }
    // the colour as the EXE rounds it: c * (1 / 255) in float; c / 255 is one ulp lower in blue
    CHECK(floatBits(243.0f / 255.0f) == 0x3f73f3f4u);
    CHECK(floatBits(DistanceFog().color[2]) == 0x3f73f3f5u);
}

C5R_TEST(fog_block_off_and_edges) {
    const float cam[3] = {0, 0, 640.0f};
    DistanceFog f;
    f.enabled = false;
    float b[8];
    distanceFogBlock(f, cam, b);
    const float off[8] = {0.5f, 0.5f, 0.5f, 1.0f, 0, 0, 0, 0};   // the block before 0188
    CHECK(std::memcmp(b, off, sizeof off) == 0);
    f.enabled = true;
    f.density = 0.0f;   // no fog: FogParams.y 0, not -inf
    distanceFogBlock(f, cam, b);
    CHECK(b[4] == 0.0f && b[5] == 0.0f);
    CHECK_EQ(fogZoomFraction(100.0f), 0.0f);
    CHECK_EQ(fogZoomFraction(100000.0f), 1.0f);
    CHECK_EQ(fogZoomFraction(-1.0f), 0.0f);
    CHECK_EQ(fogZoomFraction(NAN), 0.0f);
    CHECK_EQ(fogZoomFraction(500.0f, 20.0f, 10.0f), 0.0f);
    CHECK(std::fabs(fogZoomFraction(640.3153686523438f) - 0.6315789f) < 4e-5f);
}

// Negative: a fog value that is not finite refuses the draw (BadLayout); the module draws as before afterwards.
C5R_TEST(negative_fog_not_finite) {
    Fixture fx;
    if (!fx.ok) return;
    CHECK_OK(fx.r.setHeightField(makeGrid(4, 4, ramp)));
    Camera cam = downCamera(10, 5, 120, 64, 48);
    Options o;
    o.water = false;
    o.color = ColorSource::Constant;
    o.constantRgba = 0xFF102030u;
    terrainshot::ShotResult a, b;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, a));
    for (int k = 0; k < 4; ++k) {
        Options x = o;
        if (k == 0) x.fog.density = NAN;
        if (k == 1) x.fog.scale = INFINITY;
        if (k == 2) x.fog.w = -INFINITY;
        if (k == 3) x.fog.color[1] = NAN;
        Target t(fx.d.dev, 64, 48, DXGI_FORMAT_R8G8B8A8_UNORM);
        CHECK(t.rtv != nullptr);
        if (!t.rtv) return;
        FrameStats st;
        CHECK_ERR(fx.r.draw(fx.d.ctx, t.rtv, nullptr, cam, x, &st), Err::BadLayout);
    }
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, b));
    CHECK(a.rgba == b.rgba);
}

// Without an install (gameDir empty) there is no ramp: t2 black, the fog term 0, and the picture is the one before 0188
// bit for bit (fog on against fog off, through the EXE's pixel shader, at a camera where the fog term is large).
C5R_TEST(draw_without_a_ramp_is_bit_equal) {
    Fixture fx;
    if (!fx.ok) return;
    CHECK(fx.r.fogRamp() == nullptr);
    CHECK_OK(fx.r.setHeightField(makeGrid(4, 4, ramp)));
    ID3D11ShaderResourceView* cache = solidTexture(fx.d.dev, 0xFF3080C0u);
    CHECK(cache != nullptr);
    if (!cache) return;
    cacheEveryTile(fx.r, cache);
    Camera cam = downCamera(0, 0, 640, 160, 120);
    Options o;
    o.water = false;
    terrainshot::ShotResult a, b;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, a));
    o.fog.enabled = false;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, b));
    CHECK(!a.stats.fogOn && !b.stats.fogOn);
    CHECK(a.rgba.size() == b.rgba.size() && a.rgba == b.rgba);
    cache->Release();
}

// With the install (C5R_GAME_DIR): t2 is fogramp.dds, 128 x 8 with 8 mips, and its red channel (the one the PS reads)
// equals the EXE's t2 at the texels below [L, fog_block.py: the capture's t2 equals the file's pixels with R and B
// swapped, all 5,500 bytes]. A draw through the EXE's pixel shader then moves the top rows of the picture (the ramp's
// far end, u = 0.5 + 0.5 v5.y) toward the fog colour and leaves the bottom half unchanged (ramp 0 for u < 0.5).
C5R_TEST(game_fog_ramp_is_the_exes_and_fogs_the_far_terrain) {
    const std::string exe = test::exePath();
    const std::string game = test::gameDir();
    terrainshot::Device d;
    CHECK_OK(d.create(false));
    double ms[2];
    for (int k = 0; k < 2; ++k) {   // the init's cost of the install (k 1) against none (k 0), for the record
        Renderer t;
        const auto t0 = std::chrono::steady_clock::now();
        CHECK_OK(t.init(d.dev, wide(exe), k ? wide(game) : std::wstring()));
        ms[k] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
    std::printf("  init without an install %.1f ms, with it (fog ramp) %.1f ms\n", ms[0], ms[1]);
    Renderer r;
    CHECK_OK(r.init(d.dev, wide(exe), wide(game)));
    CHECK(r.fogRamp() != nullptr);
    if (!r.fogRamp()) return;
    D3D11_TEXTURE2D_DESC td = {};
    const std::vector<uint8_t> red = redMip0(d.dev, d.ctx, r.fogRamp(), td);
    CHECK(td.Width == 128 && td.Height == 8 && td.MipLevels == 8);
    CHECK_EQ(red.size(), size_t(128 * 8));
    if (red.size() != 128 * 8) return;
    // the capture's red channel at u = 0, 32, 64, 96, 127 of rows 0..7
    const uint8_t want[8][5] = {{0, 0, 0, 36, 189}, {0, 0, 0, 41, 196}, {0, 0, 0, 47, 202}, {0, 0, 0, 55, 208},
                                {0, 0, 0, 66, 215}, {0, 0, 0, 77, 215}, {0, 0, 0, 90, 215}, {0, 0, 1, 101, 215}};
    const uint32_t us[5] = {0, 32, 64, 96, 127};
    for (int row = 0; row < 8; ++row)
        for (int k = 0; k < 5; ++k) CHECK_EQ(static_cast<int>(red[row * 128 + us[k]]), static_cast<int>(want[row][k]));
    CHECK_OK(r.setHeightField(makeGrid(4, 4, ramp)));
    ID3D11ShaderResourceView* cache = solidTexture(d.dev, 0xFF3080C0u);
    CHECK(cache != nullptr);
    if (!cache) return;
    cacheEveryTile(r, cache);
    Camera cam = downCamera(0, 0, 300, 160, 120);   // zoom 11.9: w 0.099, the field fills the view
    Options o;
    o.water = false;
    terrainshot::ShotResult on, off;
    CHECK_OK(terrainshot::renderShot(d, r, cam, o, true, on));
    o.fog.enabled = false;
    CHECK_OK(terrainshot::renderShot(d, r, cam, o, true, off));
    CHECK(on.stats.fogOn && !off.stats.fogOn);
    double topOn = 0, topOff = 0;
    size_t nTop = 0, bottomDiff = 0, nBottom = 0;
    for (uint32_t y = 0; y < on.height; ++y)
        for (uint32_t x = 0; x < on.width; ++x) {
            const size_t i = static_cast<size_t>(y) * on.width + x;
            if (off.world[i * 4 + 3] != 1.0f) continue;   // terrain pixels only
            if (y < on.height / 8) {
                topOn += on.rgba[i * 4 + 2];
                topOff += off.rgba[i * 4 + 2];
                ++nTop;
            } else if (y >= on.height / 2) {
                ++nBottom;
                for (int c = 0; c < 3; ++c) bottomDiff += on.rgba[i * 4 + c] != off.rgba[i * 4 + c];
            }
        }
    std::printf("  fog: top rows %zu px, blue %.1f with fog, %.1f without; bottom half %zu px, %zu channels differ\n", nTop,
                nTop ? topOn / nTop : 0.0, nTop ? topOff / nTop : 0.0, nBottom, bottomDiff);
    CHECK(nTop > 100 && nBottom > 100);
    if (nTop) CHECK(topOn / nTop > topOff / nTop + 5.0);   // toward the fog colour's blue (243) from the cache's (0x30)
    CHECK_EQ(bottomDiff, size_t(0));
    cache->Release();
}

// Negative: an install folder without the ramp refuses the init; nothing half-made (setHeightField: init not done).
C5R_TEST(negative_init_install_without_a_fog_ramp) {
    const std::string exe = test::exePath();
    terrainshot::Device d;
    CHECK_OK(d.create(false));
    Renderer r;
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    const std::string empty = std::string(tmp) + "c5r_terrain_no_install_0188";
    CreateDirectoryA(empty.c_str(), nullptr);
    const Status st = r.init(d.dev, wide(exe), wide(empty));
    CHECK(!st.ok());
    std::printf("  init with an empty folder: %s\n", st.str().c_str());
    CHECK(r.fogRamp() == nullptr);
    CHECK_ERR(r.setHeightField(makeGrid(4, 4, ramp)), Err::NotFound);
    RemoveDirectoryA(empty.c_str());
}

// ---------------------------------------------------------------- issue 0191: the EXE's LOD

namespace {

// The EXE's drawn surface at level L on the CPU (terrain-surface.md 7, P11): the tile's page (c5r::bake::buildHeightPage)
// sampled at mip L with the VS's addressing (bilinear at mip-L texel coordinate (s - wx + 0.5) / 2^L - 0.5 in x and
// (1024 - (s - wy) - 0.5) / 2^L - 0.5 in y, wrapping), control points at (a - 1 .. a + 2) * 2^L from the tile origin,
// the BicubicBasis cubic. P = the module's tile samples (the page is the tile origin's).
struct PageCache {
    const heightfield::HeightGrid& g;
    std::map<uint32_t, bake::HeightPage> pages;
    explicit PageCache(const heightfield::HeightGrid& grid) : g(grid) {}
    const bake::HeightPage& page(uint32_t px, uint32_t py) {
        const uint32_t key = px * 1000 + py;
        auto it = pages.find(key);
        if (it != pages.end()) return it->second;
        bake::HeightPage hp;
        bake::HeightPageIndex pi;
        pi.px = px;
        pi.py = py;
        bake::buildHeightPage(g, pi, bake::kHeightPageMips, hp);
        return pages.emplace(key, std::move(hp)).first->second;
    }
    // byte * sz at sample (x, y) of tile origin (tx0, ty0)'s page, mip L, bilinear
    double height(int tx0, int ty0, int L, double x, double y) {
        const uint32_t px = static_cast<uint32_t>(tx0) / 896, py = static_cast<uint32_t>(ty0) / 896;
        const bake::HeightPage& hp = page(px, py);
        if (hp.levels.size() != bake::kHeightPageMips) return -1e9;
        const double wx = static_cast<double>(px) * 896 - 64, wy = static_cast<double>(py) * 896 - 64;
        const int n = 1024 >> L;
        const double s = static_cast<double>(1 << L);
        const double u = (x - wx + 0.5) / s - 0.5, v = (1024.0 - (y - wy) - 0.5) / s - 0.5;
        const double fu = std::floor(u), fv = std::floor(v);
        const double au = u - fu, av = v - fv;
        auto tex = [&](int iu, int iv) {
            iu = ((iu % n) + n) % n;
            iv = ((iv % n) + n) % n;
            return static_cast<double>(hp.levels[static_cast<size_t>(L)][static_cast<size_t>(iv) * n + iu]);
        };
        const int iu = static_cast<int>(fu), iv = static_cast<int>(fv);
        const double b = (1 - av) * ((1 - au) * tex(iu, iv) + au * tex(iu + 1, iv)) +
                         av * ((1 - au) * tex(iu, iv + 1) + au * tex(iu + 1, iv + 1));
        return b * g.sz;
    }
    double surface(int L, uint32_t P, double c, double r) {
        const double step = static_cast<double>(1 << L);
        const int tx0 = static_cast<int>(std::floor(c / P)) * static_cast<int>(P);
        const int ty0 = static_cast<int>(std::floor(r / P)) * static_cast<int>(P);
        const double lc = c - tx0, lr = r - ty0;
        const double a = std::floor(lc / step), b = std::floor(lr / step);
        double wx[4], wy[4];
        weights(lc / step - a, wx);
        weights(lr / step - b, wy);
        double z = 0;
        for (int j = 0; j < 4; ++j)
            for (int i = 0; i < 4; ++i)
                z += wy[j] * wx[i] *
                     height(tx0, ty0, L, tx0 + (a - 1 + i) * step, ty0 + (b - 1 + j) * step);
        return z;
    }
};

// A top-down orthographic camera whose pixel (i, j) centre is at sample (c0 + i, r0 + h - 1 - j) (one pixel per sample).
Camera orthoSamples(const heightfield::HeightGrid& g, uint32_t c0, uint32_t r0, uint32_t w, uint32_t h, float height) {
    Camera c;
    const float cx = (static_cast<float>(c0) + w / 2.0f - 0.5f) * g.sx - g.ox;
    const float cy = (static_cast<float>(r0) + h / 2.0f - 0.5f) * g.sy - g.oy;
    const float v[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -cx, -cy, -height, 1};
    std::memcpy(c.view, v, sizeof v);
    const float zn = 1.0f, zf = 1000.0f;
    const float p[16] = {2.0f / (w * g.sx), 0, 0, 0, 0, 2.0f / (h * g.sy), 0, 0, 0, 0, 1.0f / (zn - zf), 0, 0, 0,
                         zn / (zn - zf), 1};
    std::memcpy(c.proj, p, sizeof p);
    c.width = w;
    c.height = h;
    return c;
}

// A field with slopes and fine detail, so that the mips differ from mip 0 and from each other.
uint16_t bumpy(uint32_t c, uint32_t r) {
    const double v = 9000.0 + 4000.0 * std::sin(6.283185307179586 * c / 96.0) * std::cos(6.283185307179586 * r / 80.0) +
                     150.0 * std::sin(0.7 * c) + 120.0 * std::cos(0.9 * r);
    return static_cast<uint16_t>(v);
}

void mul44t(const float* a, const float* b, float* out) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            double s = 0;
            for (int k = 0; k < 4; ++k) s += static_cast<double>(a[i * 4 + k]) * b[k * 4 + j];
            out[i * 4 + j] = static_cast<float>(s);
        }
}

std::string repoFile(const std::string& rel) {
    std::string f = __FILE__;
    for (char& ch : f)
        if (ch == '\\') ch = '/';
    const size_t k = f.rfind("/render/tests/terrain/");
    return (k == std::string::npos ? std::string(".") : f.substr(0, k)) + "/" + rel;
}

} // namespace

// The tile rule's pieces (0x6497C0, the level grid, the tile box, the vertex counts) at values worked by hand.
C5R_TEST(lod_rule_tile_values_and_grid) {
    // LOD = clamp(0.5 + log2(64 / max(1, p * 0.5 / 4 / 2)), -2, 4)
    CHECK(std::fabs(exeTileLod(256.0f, 65) - 2.5f) < 1e-6f);   // q 16
    CHECK(std::fabs(exeTileLod(1024.0f, 65) - 0.5f) < 1e-6f);  // q 64
    CHECK(std::fabs(exeTileLod(4096.0f, 65) + 1.5f) < 1e-6f);  // q 256
    CHECK_EQ(exeTileLod(64.0f, 65), 4.0f);                      // 4.5 -> 4
    CHECK_EQ(exeTileLod(1.0f, 65), 4.0f);                       // q < 1 -> 1 -> 6.5 -> 4
    CHECK_EQ(exeTileLod(1.0e6f, 65), -2.0f);                    // clamp below
    CHECK(std::fabs(exeTileLod(256.0f, 64) - static_cast<float>(0.5 + std::log2(63.0 / 16.0))) < 1e-6f);
    CHECK(std::fabs(exeTileLod(256.0f, 65, 2, 2) - 1.5f) < 1e-6f);   // TriSize 2: q 32
    uint32_t n = 0;
    float st = 0;
    const uint32_t wantN[5] = {64, 32, 16, 8, 4};
    for (int l = 0; l <= kMaxLodLevel; ++l) {
        exeLodGrid(65, l, &n, &st);
        CHECK_EQ(n, wantN[l]);
        CHECK_EQ(st, static_cast<float>(1 << l));
        exeLodGrid(129, l, &n, &st);   // a 2-plot tile of the module
        CHECK_EQ(n, wantN[l] * 2);
        CHECK_EQ(st, static_cast<float>(1 << l));
    }
    exeLodGrid(64, 1, &n, &st);   // the EXE's last column (d4 64): 33 vertices, LodStep 63 / 32
    CHECK_EQ(n, 32u);
    CHECK_EQ(st, 63.0f / 32.0f);
    CHECK_EQ(exeTileVerts(0, 64, 8192), 65u);
    CHECK_EQ(exeTileVerts(8128, 64, 8192), 64u);
    CHECK_EQ(exeTileVerts(8192, 64, 8192), 0u);
    float b[6];
    exeTileBox(6464, 1728, 65, 65, 1.0f, 0.866f, 0.2f, 4096.0f, 2327.808f, b);
    CHECK(b[0] == 2368.0f && std::fabs(b[1] - (1728 * 0.866f - 2327.808f)) < 1e-3f && b[2] == 0.0f);
    CHECK(b[3] == 2433.0f && std::fabs(b[4] - b[1] - 65 * 0.866f) < 1e-3f && std::fabs(b[5] - 51.0f) < 1e-4f);
    // a box seen head-on by an orthographic view of NDC = world / 100: its largest diagonal spans |(0.65, 0.5629)| / 1
    float m[16] = {0.01f, 0, 0, 0, 0, 0.01f, 0, 0, 0, 0, 0.001f, 0, 0, 0, 0.5f, 1};
    float bx[6] = {0, 0, 0, 65, 56.29f, 51};
    const double diag = std::sqrt(0.65 * 0.65 + 0.5629 * 0.5629);
    CHECK(std::fabs(exeTileProjectedSize(bx, m, 2000) - 0.25 * 2000 * diag) < 0.01);
    CHECK(exeTileSphereVisible(bx, m));
    // the right plane is x = 100 (NDC 1); the sphere's radius is half the diagonal, 49.99 world units
    float faraway[6] = {500, 0, 0, 565, 56.29f, 51};   // centre x 532.5: far out
    CHECK(!exeTileSphereVisible(faraway, m));
    float out[6] = {130, 0, 0, 195, 56.29f, 51};   // centre 162.5: 62.5 beyond the plane, more than the radius: out
    CHECK(!exeTileSphereVisible(out, m));
    float edge[6] = {115, 0, 0, 180, 56.29f, 51};  // centre 147.5: 47.5 beyond, less than the radius: in (box wholly outside)
    CHECK(exeTileSphereVisible(edge, m));
}

// 0x4241E0's update and hysteresis on a sequence of frame minima (worked by hand from terrain-surface.md 7).
C5R_TEST(lod_hysteresis_follows_the_exe) {
    LodState s;
    CHECK(!exeLodUpdate(s, 0.0f));                   // d = 0: nothing
    CHECK(exeLodUpdate(s, 1.57f));                   // |d| 1.57 > |0|
    CHECK(s.lodMip == 1.0f && s.tessFactor == 2 && s.value == 1.57f && s.hyst == 0.25f);
    CHECK(exeLodUpdate(s, 1.80f));                   // same direction as the last level change: taken
    CHECK(s.lodMip == 1.0f && s.value == 1.80f && s.hyst == 0.25f);
    CHECK(!exeLodUpdate(s, 1.60f));                  // back by 0.2 <= 0.25: refused
    CHECK(s.value == 1.80f && s.lodMip == 1.0f);
    CHECK(exeLodUpdate(s, 1.50f));                   // back by 0.3: taken, same level, hyst unchanged
    CHECK(s.lodMip == 1.0f && s.value == 1.50f && s.hyst == 0.25f);
    CHECK(exeLodUpdate(s, 0.95f));                   // down a level: hyst -0.25
    CHECK(s.lodMip == 0.0f && s.hyst == -0.25f);
    CHECK(!exeLodUpdate(s, 1.10f));                  // up by 0.15 against hyst -0.25: refused (level stays 0)
    CHECK(s.lodMip == 0.0f && s.value == 0.95f);
    CHECK(exeLodUpdate(s, 1.21f));                   // up by 0.26: taken, level 1, hyst +0.25
    CHECK(s.lodMip == 1.0f && s.hyst == 0.25f);
    CHECK(exeLodUpdate(s, 3.0f));
    CHECK(s.lodMip == 3.0f && s.hyst == 0.5f);       // two levels at once: 0.25 * 2
    CHECK(exeLodUpdate(s, -1.0f));                   // LodMip 0, TessFactor (1 << int(0.75)) * 2
    CHECK(s.lodMip == 0.0f && s.tessFactor == 2 && s.hyst == -1.0f);
    CHECK(exeLodUpdate(s, -1.5f, 3));                // (1 << int(1.125)) * Subdiv 3
    CHECK(s.lodMip == 0.0f && s.tessFactor == 6 && s.hyst == -0.25f);
    CHECK(exeLodUpdate(s, 0.2f));                    // back to M >= 0: TessFactor = Subdiv; floor 0 - floor(-1.5) = 2
    CHECK(s.lodMip == 0.0f && s.tessFactor == 2 && s.hyst == 0.5f);
}

// Negative: the frame rule refuses bad geometry and leaves its output alone.
C5R_TEST(negative_exe_frame_lod_inputs) {
    const float vp[16] = {0.01f, 0, 0, 0, 0, 0.01f, 0, 0, 0, 0, 0.001f, 0, 0, 0, 0.5f, 1};
    LodFrame f;
    f.visible = 77;
    CHECK_ERR(exeFrameLod(100, 64, 1, 0.866f, 0.2f, 0, 0, vp, 100, 4, 2, f), Err::BadLayout);   // not 64 * n
    CHECK_ERR(exeFrameLod(0, 64, 1, 0.866f, 0.2f, 0, 0, vp, 100, 4, 2, f), Err::BadLayout);
    float bad[16];
    std::memcpy(bad, vp, sizeof bad);
    bad[5] = std::nanf("");
    CHECK_ERR(exeFrameLod(128, 64, 1, 0.866f, 0.2f, 0, 0, bad, 100, 4, 2, f), Err::BadLayout);
    CHECK_ERR(exeFrameLod(128, 64, 1, 0.866f, 0.2f, INFINITY, 0, vp, 100, 4, 2, f), Err::BadLayout);
    CHECK_ERR(exeFrameLod(128, 64, 1, 0.866f, 0.2f, 0, 0, vp, 0, 4, 2, f), Err::BadLayout);      // width 0
    CHECK_ERR(exeFrameLod(128, 64, 1, 0.866f, 0.2f, 0, 0, vp, 100, 0, 2, f), Err::BadLayout);
    CHECK_ERR(exeFrameLod(128, 64, 1, 0.866f, 0.2f, 0, 0, vp, 100, 4, 65, f), Err::BadLayout);
    CHECK_EQ(f.visible, 77u);
    // and an empty list (listed all 0) keeps the start value 10
    const uint8_t none[2] = {0, 0};
    CHECK_OK(exeFrameLod(128, 64, 1, 0.866f, 0.2f, 0, 0, vp, 100, 4, 2, f, none));
    CHECK(f.visible == 0 && f.minLod == 10.0f);
    // the class rule on a field below 30 * 255 and above it
    heightfield::HeightGrid lo = makeGrid(2, 2, [](uint32_t, uint32_t) { return uint16_t(7649); });
    heightfield::HeightGrid hi = makeGrid(2, 2, [](uint32_t c, uint32_t r) { return uint16_t(c == 70 && r == 70 ? 7650 : 0); });
    CHECK(!exeTileClass3(lo, 0, 0));
    CHECK(exeTileClass3(hi, 1, 1));    // (70, 70) is in tile (1, 1)'s window [68, 131)
    CHECK(!exeTileClass3(hi, 0, 0));   // tile (0, 0)'s window [4, 67) misses it
    CHECK(exeTileClass3(makeGrid(2, 2, [](uint32_t c, uint32_t) { return uint16_t(c == 2 ? 9000 : 0); }), 1, 0));   // wraps: 130 = 128 + 2
}

// The five stock captures of issue 0154 (render/evidence/0191/results/capture_lod_inputs.json: ViewProj, width and the
// drawn tiles of each frame's main view, numbers only): the rule gives the captured LodMip, LodStep and TessFactor from
// the camera and the width alone. For the three captures of 3601's Tiny saves (C5H-3600-TINY_0008 and _0009V, one height
// field: 3609's stock tap, C5R_TINY9V_RAW) the visible list (class 3, sphere test) equals the tiles the EXE drew, tile
// for tile. 0167's two captures (another instance's save, heights not kept) are checked with every tile listed.
C5R_TEST(game_lod_reproduces_the_stock_captures) {
    terrainshot::Json j;
    const std::string path = repoFile("render/evidence/0191/results/capture_lod_inputs.json");
    CHECK_OK(terrainshot::readJsonFile(wide(path), j));
    if (j.obj.empty()) return;
    const char* e = std::getenv("C5R_TINY9V_RAW");
    const std::string raw = e && *e ? e : "D:/c5-64bit-work/3600/3609/stock/heights/heightmap_u16le_3584x2560.raw";
    heightfield::HeightGrid tiny = makeGrid(56, 40, [](uint32_t, uint32_t) { return uint16_t(0); }, true);
    tiny.ox = 1792.0f;
    tiny.oy = 2560 * 0.866f / 2;
    {
        FILE* f = std::fopen(raw.c_str(), "rb");
        if (!f) {
            test::fail(__FILE__, __LINE__, "Tiny height tap missing: " + raw + " (set C5R_TINY9V_RAW)");
            return;
        }
        const size_t got = std::fread(tiny.h.data(), 2, tiny.h.size(), f);
        std::fclose(f);
        CHECK_EQ(got, tiny.h.size());
    }
    std::vector<uint8_t> listed(56 * 40);
    for (uint32_t y = 0; y < 40; ++y)
        for (uint32_t x = 0; x < 56; ++x) listed[y * 56 + x] = exeTileClass3(tiny, x, y) ? 1 : 0;
    int checked = 0, setsEqual = 0;
    for (const auto& kv : j.obj) {
        const terrainshot::Json& c = kv.second;
        const terrainshot::Json* vpj = c.get("ViewProj");
        if (!vpj || vpj->arr.size() != 1 || vpj->arr[0].arr.size() != 16) continue;
        float vp[16];
        for (int i = 0; i < 16; ++i) vp[i] = static_cast<float>(vpj->arr[0].arr[i].num);
        const uint32_t width = static_cast<uint32_t>(c.get("ScreenSize")->arr[0].num);
        const float wantMip = static_cast<float>(c.get("LodMip")->arr[0].num);
        const float wantStep = static_cast<float>(c.get("LodStep")->arr[0].arr[0].num);
        const float wantTess = static_cast<float>(c.get("TessFactor")->arr[0].num);
        const bool tinySave = kv.first.find("c5h-c_") == 0;
        LodFrame f;
        std::vector<uint32_t> vis;
        CHECK_OK(exeFrameLod(3584, 2560, 1.0f, 0.866f, 0.2f, tiny.ox, tiny.oy, vp, width, 4, 2, f,
                             tinySave ? listed.data() : nullptr, &vis));
        LodState s;
        CHECK(exeLodUpdate(s, f.minLod));
        const int L = static_cast<int>(s.lodMip);
        float step = 0;
        exeLodGrid(65, L, nullptr, &step);
        std::printf("  %s: width %u, min LOD %.4f over %u tiles -> LodMip %.0f LodStep %.0f tess %d (captured %.0f %.0f %.0f)\n",
                    kv.first.c_str(), width, f.minLod, f.visible, s.lodMip, step, s.tessFactor, wantMip, wantStep, wantTess);
        CHECK_EQ(s.lodMip, wantMip);
        CHECK_EQ(step, wantStep);
        CHECK_EQ(static_cast<float>(s.tessFactor), wantTess);
        ++checked;
        if (tinySave) {
            std::vector<uint32_t> drawn;
            for (const terrainshot::Json& t : c.get("tiles")->arr)
                drawn.push_back(static_cast<uint32_t>(t.get("origin")->arr[1].num / 64) * 56 +
                                static_cast<uint32_t>(t.get("origin")->arr[0].num / 64));
            std::sort(drawn.begin(), drawn.end());
            std::sort(vis.begin(), vis.end());
            std::printf("    visible list %zu tiles, drawn %zu, equal %d\n", vis.size(), drawn.size(), vis == drawn ? 1 : 0);
            CHECK(vis == drawn);
            setsEqual += vis == drawn;
        }
    }
    CHECK_EQ(checked, 5);
    CHECK_EQ(setsEqual, 3);
}

// The live reads of issue 0191 on oldpc (huge TB-HUGE_0350 t350, stock, 1914 x 1051, testbed job 20261114-203537-79a7,
// 2026-11-14 20:39 EST; terrain-surface.md 7): at 9055's three cameras g_View / g_Proj and TR+0x242C (1.98247933 at the load
// view; 2.01592898 at Thebes, 2.02267861 at Akhetaten, held there by the hysteresis after the LookAt) and the visible
// list (84 / 121 / 113 tiles; the rule's list equals it tile for tile, checked by render/evidence/0191's live check).
// The load view's minimum is the held value; at the other two the held value lies above the frame's minimum by less
// than 0.25 (a fall smaller than the hysteresis is refused).
C5R_TEST(game_lod_at_the_oldpc_huge_reads) {
    const char* e = std::getenv("C5R_GT");
    const std::string dir = std::string(e && *e ? e : "D:/c5-64bit-work/i2-ground-truth") + "/huge";
    heightfield::HeightGrid g;
    if (GetFileAttributesA((dir + "/heightmap_info.json").c_str()) == INVALID_FILE_ATTRIBUTES) {
        test::fail(__FILE__, __LINE__, "ground truth missing: " + dir);
        return;
    }
    CHECK_OK(terrainshot::loadGroundTruth(wide(dir), g));
    std::vector<uint8_t> listed(128 * 84);
    for (uint32_t y = 0; y < 84; ++y)
        for (uint32_t x = 0; x < 128; ++x) listed[y * 128 + x] = exeTileClass3(g, x, y) ? 1 : 0;
    // g_View row 3 as read (rows 0-2 are the 45-degree tilt of every map camera) and g_Proj as read
    const float t[3][3] = {{-2975.864990234375f, 536.5439453125f, -1136.5440673828125f},
                           {-2432.855224609375f, 579.202880859375f, -1179.2030029296875f},
                           {-1952.5399169921875f, 926.5091552734375f, -1526.5091552734375f}};
    const float held[3] = {1.9824793338775635f, 2.0159289836883545f, 2.0226786136627197f};
    const uint32_t readVis[3] = {84, 121, 113};   // TR+0x2334 as read
    const char* name[3] = {"load view", "Thebes", "Akhetaten"};
    terrainshot::Json live;
    for (int k = 0; k < 3; ++k) {
        const float view[16] = {1, 0, 0, 0, 0, 0.7071068286895752f, -0.7071068286895752f, 0, 0, 0.7071068286895752f,
                                0.7071068286895752f, 0, t[k][0], t[k][1], t[k][2], 1};
        const float proj[16] = {2.1325252056121826f, 0, 0, 0, 0, 3.88358998298645f, 0, 0, 0, 0, -1.0126582384109497f, -1,
                                0, 0, -50.632911682128906f, 0};
        float vp[16];
        mul44t(view, proj, vp);
        LodFrame f;
        std::vector<uint32_t> vis;
        CHECK_OK(exeFrameLod(g.samplesX, g.samplesY, g.sx, g.sy, g.sz, g.ox, g.oy, vp, 1914, 4, 2, f, listed.data(), &vis));
        uint32_t x0 = 999, x1 = 0, y0 = 999, y1 = 0;
        for (uint32_t i : vis) {
            x0 = std::min(x0, i % 128);
            x1 = std::max(x1, i % 128);
            y0 = std::min(y0, i / 128);
            y1 = std::max(y1, i / 128);
        }
        std::printf("  %s: %u tiles (x %u..%u, y %u..%u), min LOD %.4f (TR+0x242C %.4f)\n", name[k], f.visible, x0, x1, y0, y1,
                    f.minLod, held[k]);
        CHECK_EQ(f.visible, readVis[k]);
        // the EXE's list as read (render/evidence/0191/results/live_stock_oldpc.json, cameras[k].exe.tiles: tx, ty, L)
        if (k == 0) CHECK_OK(terrainshot::readJsonFile(wide(repoFile("render/evidence/0191/results/live_stock_oldpc.json")), live));
        const terrainshot::Json* cams = live.get("cameras");
        if (cams && cams->arr.size() == 3) {
            std::vector<uint32_t> exe;
            for (const terrainshot::Json& tl : cams->arr[k].get("exe")->get("tiles")->arr)
                exe.push_back(static_cast<uint32_t>(tl.arr[1].num) * 128 + static_cast<uint32_t>(tl.arr[0].num));
            std::sort(exe.begin(), exe.end());
            std::vector<uint32_t> mine = vis;
            std::sort(mine.begin(), mine.end());
            std::printf("    the EXE's list %zu tiles, equal to the rule's: %d\n", exe.size(), exe == mine ? 1 : 0);
            CHECK(exe == mine);
        } else {
            CHECK(cams && cams->arr.size() == 3);
        }
        if (k == 0) {
            CHECK(std::fabs(f.minLod - held[k]) < 2e-6f);                     // 1.98247969 against 1.98247933
            CHECK(x0 == 104 && x1 == 113 && y0 == 24 && y1 == 34);            // the read's tile range
        } else {
            CHECK(held[k] > f.minLod && held[k] - f.minLod < 0.25f);         // held by the hysteresis
            LodState s;                                                       // from the load view's state:
            exeLodUpdate(s, held[0]);                                         // level 1, H +0.25
            exeLodUpdate(s, held[k]);                                         // the rise to the held value is taken
            CHECK(!exeLodUpdate(s, f.minLod) && s.lodMip == 2.0f);           // the fall to this frame's is not
        }
    }
}

// The drawn surface at every fixed level 0..4 lies on the CPU rule over the EXE's page mips (tess 64: the triangles
// follow the cubic), and the levels differ from each other (the mips are used, not mip 0).
C5R_TEST(draw_lod_levels_follow_the_page_mips) {
    Fixture fx;
    if (!fx.ok) return;
    heightfield::HeightGrid g = makeGrid(4, 4, bumpy);
    CHECK_OK(fx.r.setHeightField(g, 1));
    PageCache pc(g);
    Camera cam = downCamera(-20, 10, 150, 160, 120);
    for (int L = 0; L <= kMaxLodLevel; ++L) {
        Options o;
        o.color = ColorSource::Constant;
        o.water = false;
        o.lodLevel = L;
        o.tessFactor = static_cast<float>(std::min(64, 8 << L));   // 4-8 segments per sample: the triangles follow the cubic
        terrainshot::ShotResult res;
        CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, true, res));
        CHECK(res.stats.lodLevel == L && res.stats.lodMip == static_cast<float>(L) &&
              res.stats.lodStep == static_cast<float>(1 << L));
        size_t covered = 0, bad = 0, differs = 0;
        double worst = 0;
        for (size_t i = 0; i < static_cast<size_t>(res.width) * res.height; i += 3) {
            const float* w = &res.world[i * 4];
            if (w[3] != 1.0f) continue;
            ++covered;
            const double c = (w[0] + g.ox) / g.sx, r = (w[1] + g.oy) / g.sy;
            const double s = pc.surface(L, 64, c, r);
            const double d = std::fabs(w[2] - s);
            worst = std::max(worst, d);
            if (d > 0.01) ++bad;
            if (L > 0 && std::fabs(pc.surface(L - 1, 64, c, r) - s) > 0.05) ++differs;
        }
        std::printf("  L %d: %zu px checked, worst |z - rule| %.5f, %zu off; %zu px differ from L - 1 by > 0.05\n", L, covered,
                    worst, bad, differs);
        CHECK(covered > 5000);
        CHECK_EQ(bad, size_t(0));
        if (L > 0) CHECK(differs > covered / 20);
    }
}

// kLodExe: the draw's level is the rule's for the frame (the class-3 list of the field, the camera's width or lodWidth),
// the state advances once per draw, the hysteresis refuses a small change back, and a failed draw leaves the state alone.
C5R_TEST(draw_exe_lod_picks_the_level_and_keeps_state) {
    Fixture fx;
    if (!fx.ok) return;
    heightfield::HeightGrid g = makeGrid(4, 4, bumpy);
    CHECK_OK(fx.r.setHeightField(g, 1));
    std::vector<uint8_t> listed(16);
    for (uint32_t i = 0; i < 16; ++i) listed[i] = exeTileClass3(g, i % 4, i / 4) ? 1 : 0;
    const uint32_t W = 1280, H = 960;   // wide enough that the minimum lies inside the rule's clamp at these heights
    Target rt(fx.d.dev, W, H, DXGI_FORMAT_R8G8B8A8_UNORM);
    Options o;
    o.color = ColorSource::Constant;
    o.water = false;
    auto expect = [&](const Camera& c, uint32_t width) {
        float vp[16];
        mul44t(c.view, c.proj, vp);
        LodFrame f;
        exeFrameLod(g.samplesX, g.samplesY, g.sx, g.sy, g.sz, g.ox, g.oy, vp, width, 4, 2, f, listed.data());
        return f;
    };
    CHECK_EQ(fx.r.lodState().value, 0.0f);
    Camera a = downCamera(0, 0, 400, W, H);
    const LodFrame fa = expect(a, W);
    FrameStats st;
    CHECK_OK(fx.r.draw(fx.d.ctx, rt.rtv, nullptr, a, o, &st));
    LodState want;
    exeLodUpdate(want, fa.minLod);
    std::printf("  height 400: min %.4f over %u, level %d (rule %.0f), tess %.0f\n", st.lodMin, st.lodVisible, st.lodLevel,
                want.lodMip, st.tessFactorUsed);
    CHECK(fa.visible > 0 && st.lodVisible == fa.visible && st.lodMin == fa.minLod && st.lodAccepted);
    CHECK(st.lodLevel == static_cast<int>(want.lodMip) && st.lodStep == static_cast<float>(1 << st.lodLevel));
    CHECK(fx.r.lodState().value == fa.minLod && fx.r.lodState().lodMip == want.lodMip);
    // the same camera again: d = 0, nothing changes
    CHECK_OK(fx.r.draw(fx.d.ctx, rt.rtv, nullptr, a, o, &st));
    CHECK(!st.lodAccepted && fx.r.lodState().value == fa.minLod);
    // lodWidth doubles the projected size: the minimum falls by 1
    Options ow = o;
    ow.lodWidth = 2 * W;
    const LodFrame fw = expect(a, 2 * W);
    CHECK(std::fabs((fa.minLod - fw.minLod) - 1.0f) < 1e-4f);
    CHECK(fa.minLod > 1.0f && fa.minLod < 4.0f);
    fx.r.resetLod();
    CHECK_OK(fx.r.draw(fx.d.ctx, rt.rtv, nullptr, a, ow, &st));
    CHECK(st.lodMin == fw.minLod);
    fx.r.resetLod();
    CHECK_OK(fx.r.draw(fx.d.ctx, rt.rtv, nullptr, a, o, &st));
    // a little closer: the minimum falls by less than 0.25 against the last level change (up): refused
    Camera b = downCamera(0, 0, 380, W, H);
    const LodFrame fb = expect(b, W);
    std::printf("  height 380: min %.4f (held %.4f, hyst %.2f)\n", fb.minLod, fx.r.lodState().value, fx.r.lodState().hyst);
    CHECK(fb.minLod < fa.minLod && fa.minLod - fb.minLod < 0.25f && fx.r.lodState().hyst > 0);
    CHECK_OK(fx.r.draw(fx.d.ctx, rt.rtv, nullptr, b, o, &st));
    CHECK(!st.lodAccepted && st.lodMin == fb.minLod && fx.r.lodState().value == fa.minLod);
    // a failed draw (bad TriSize) leaves the state as it was
    Options bad = o;
    bad.lodTriSize = 0;
    Camera c = downCamera(0, 0, 100, W, H);
    CHECK_ERR(fx.r.draw(fx.d.ctx, rt.rtv, nullptr, c, bad, &st), Err::BadLayout);
    CHECK(fx.r.lodState().value == fa.minLod);
    // far closer: taken, a lower level
    CHECK_OK(fx.r.draw(fx.d.ctx, rt.rtv, nullptr, c, o, &st));
    std::printf("  height 100: min %.4f, level %d\n", st.lodMin, st.lodLevel);
    CHECK(st.lodAccepted && st.lodLevel < static_cast<int>(want.lodMip));
    // a new field resets the state (the EXE's at terrain creation)
    CHECK_OK(fx.r.setHeightField(g, 1));
    CHECK(fx.r.lodState().value == 0.0f && fx.r.lodState().lodMip == 0.0f && fx.r.lodState().hyst == 0.0f);
}

// The huge ground truth at Thebes (plot (101, 25), sample (6496, 1728)) at L = 1 (the EXE's level at the default zoom on
// the workstation), TessFactor 2: an orthographic top-down target with one pixel per sample puts every pixel centre on a
// tessellated vertex, so the read-back z is the vertex height. It equals the CPU rule over the EXE's mip-1 page at every
// vertex, and differs from the level-0 surface the module drew before 0191. The vertex grid goes to C5R_WORK
// (l1_thebes_vertices.f32 + .json) for render/evidence/0191/compare_l1_0191.py, which checks it against 0154's
// lod_surface_0154.py (an independent implementation of the same rule).
C5R_TEST(game_l1_surface_at_vertices_huge) {
    Fixture fx;
    if (!fx.ok) return;
    const char* e = std::getenv("C5R_GT");
    const std::string dir = std::string(e && *e ? e : "D:/c5-64bit-work/i2-ground-truth") + "/huge";
    if (GetFileAttributesA((dir + "/heightmap_info.json").c_str()) == INVALID_FILE_ATTRIBUTES) {
        test::fail(__FILE__, __LINE__, "ground truth missing: " + dir);
        return;
    }
    heightfield::HeightGrid g;
    CHECK_OK(terrainshot::loadGroundTruth(wide(dir), g));
    CHECK_OK(fx.r.setHeightField(g, 1));
    const uint32_t W = 256, H = 192, c0 = 6496 - 128, r0 = 1728 - 96;
    Camera cam = orthoSamples(g, c0, r0, W, H, 300.0f);
    Options o;
    o.color = ColorSource::Constant;
    o.water = false;
    o.lodLevel = 1;
    terrainshot::ShotResult res;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, true, res));
    if (res.world.empty()) return;
    CHECK(res.stats.lodStep == 2.0f && res.stats.tessFactorUsed == 2.0f);
    PageCache pc(g);
    std::vector<float> z(static_cast<size_t>(W) * H, 0);
    size_t n = 0, bad = 0, posBad = 0, differs = 0;
    double worst = 0, sumD = 0;
    for (uint32_t j = 0; j < H; ++j)
        for (uint32_t i = 0; i < W; ++i) {
            const float* w = &res.world[(static_cast<size_t>(j) * W + i) * 4];
            const uint32_t c = c0 + i, r = r0 + H - 1 - j;
            z[static_cast<size_t>(H - 1 - j) * W + i] = w[2];   // row 0 = sample row r0
            if (w[3] != 1.0f) continue;
            ++n;
            if (std::fabs(w[0] - (c * g.sx - g.ox)) > 1e-3 || std::fabs(w[1] - (r * g.sy - g.oy)) > 1e-3) ++posBad;
            const double s = pc.surface(1, 64, c, r), s0 = pc.surface(0, 64, c, r);
            const double d = std::fabs(w[2] - s);
            worst = std::max(worst, d);
            if (d > 0.002) ++bad;
            sumD += w[2] - s0;
            if (std::fabs(s - s0) > 0.05) ++differs;
        }
    std::printf("  Thebes L1 vertices: %zu of %u, %zu off the rule by > 0.002 (worst %.6f), %zu not on a vertex; mean z - L0 "
                "%.4f, %zu differ from L0 by > 0.05\n", n, W * H, bad, worst, posBad, n ? sumD / n : 0.0, differs);
    CHECK_EQ(n, static_cast<size_t>(W) * H);
    CHECK_EQ(posBad, size_t(0));
    CHECK_EQ(bad, size_t(0));
    CHECK(differs > n / 50);
    // the dump for the cross-check against 0154's script
    const std::string out = test::workDir() + "/l1_thebes_vertices.f32";
    FILE* f = std::fopen(out.c_str(), "wb");
    if (f) {
        std::fwrite(z.data(), 4, z.size(), f);
        std::fclose(f);
        writeText(out + ".json", "{\"x0\": " + std::to_string(c0) + ", \"y0\": " + std::to_string(r0) + ", \"w\": " +
                                     std::to_string(W) + ", \"h\": " + std::to_string(H) +
                                     ", \"rows\": \"row 0 = sample row y0\", \"lod\": 1}");
        std::printf("  wrote %s\n", out.c_str());
    }
}

// ---------------------------------------------------------------- issue 0189: the EXE's pass per tile and t9

namespace {

// FNV-1a 64 over every mip of a block-compressed 2D view, each mip's rows of 4 x 4 blocks packed tight (the DDS payload's
// order, which is also the capture export's); bytes = the total. 0 when the view cannot be read.
uint64_t blockFnv(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* v, uint32_t blockBytes,
                  D3D11_TEXTURE2D_DESC& d, size_t* bytes) {
    *bytes = 0;
    ID3D11Resource* r = nullptr;
    v->GetResource(&r);
    ID3D11Texture2D* t = nullptr;
    if (!r || FAILED(r->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&t)))) {
        if (r) r->Release();
        return 0;
    }
    t->GetDesc(&d);
    D3D11_TEXTURE2D_DESC s = d;
    s.Usage = D3D11_USAGE_STAGING;
    s.BindFlags = 0;
    s.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    s.MiscFlags = 0;
    uint64_t h = 0xcbf29ce484222325ull;
    ID3D11Texture2D* st = nullptr;
    if (SUCCEEDED(dev->CreateTexture2D(&s, nullptr, &st))) {
        ctx->CopyResource(st, t);
        for (uint32_t m = 0; m < d.MipLevels; ++m) {
            const uint32_t bw = std::max(1u, ((d.Width >> m) + 3) / 4), bh = std::max(1u, ((d.Height >> m) + 3) / 4);
            D3D11_MAPPED_SUBRESOURCE map;
            if (FAILED(ctx->Map(st, m, D3D11_MAP_READ, 0, &map))) break;
            for (uint32_t y = 0; y < bh; ++y) {
                const uint8_t* row = static_cast<const uint8_t*>(map.pData) + static_cast<size_t>(y) * map.RowPitch;
                for (uint32_t i = 0; i < bw * blockBytes; ++i) {
                    h ^= row[i];
                    h *= 0x100000001b3ull;
                }
                *bytes += bw * blockBytes;
            }
            ctx->Unmap(st, m);
        }
        st->Release();
    }
    t->Release();
    r->Release();
    return h;
}

bool writeFile(const std::string& path, const std::vector<uint8_t>& b) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(b.data(), 1, b.size(), f) == b.size();
    return std::fclose(f) == 0 && ok;
}

} // namespace

// The pass per tile (Options::pass -1, 0x5C37E0): a tile whose cache has a t8 is drawn with pass 0, the others with pass
// 1; 0..2 force one pass for every tile. Without an install there is no spec map: FlagB 0, specOn false, and the pass-0
// PS gives exactly its pass-1 picture (its colour is t0 then, as pass 1's), so the per-tile picture is bit-equal to the
// pass-1 one.
C5R_TEST(draw_pass_per_tile_follows_t8) {
    Fixture fx;
    if (!fx.ok) return;
    CHECK(fx.r.specMap() == nullptr);
    CHECK_OK(fx.r.setHeightField(makeGrid(4, 4, ramp)));
    ID3D11ShaderResourceView* cache = solidTexture(fx.d.dev, 0xFF3080C0u);
    ID3D11ShaderResourceView* spec = solidTexture(fx.d.dev, 0xFFFFFFFFu);
    CHECK(cache != nullptr && spec != nullptr);
    if (!cache || !spec) return;
    const TileGrid tg = fx.r.tileGrid();
    CHECK(tg.tilesX == 2 && tg.tilesY == 2);
    for (uint32_t y = 0; y < tg.tilesY; ++y)
        for (uint32_t x = 0; x < tg.tilesX; ++x) {
            TileCache tc;
            tc.color = cache;
            tc.second = x == y ? spec : nullptr;   // t8 on tiles (0, 0) and (1, 1)
            CHECK_OK(fx.r.setTileCache(x, y, tc));
        }
    Camera cam = downCamera(0, 0, 640, 160, 120);
    Options o;
    o.water = false;
    terrainshot::ShotResult per, p1, p0, nospec;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, per));
    std::printf("  per tile: drawn %u, pass 0 %u, pass 1 %u, passUsed %d, specOn %d\n", per.stats.tilesDrawn, per.stats.tilesPass0,
                per.stats.tilesPass1, per.stats.passUsed, per.stats.specOn ? 1 : 0);
    CHECK_EQ(per.stats.tilesDrawn, 4u);
    CHECK_EQ(per.stats.tilesPass0, 2u);
    CHECK_EQ(per.stats.tilesPass1, 2u);
    CHECK_EQ(per.stats.passUsed, -1);
    CHECK(!per.stats.specOn);
    CHECK(!per.stats.listCounted);   // 2-plot tiles: a module tile is not an EXE tile
    o.pass = 1;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, p1));
    CHECK(p1.stats.tilesPass0 == 0 && p1.stats.tilesPass1 == 4 && p1.stats.passUsed == 1);
    o.pass = 0;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, p0));
    CHECK(p0.stats.tilesPass0 == 4 && p0.stats.tilesPass1 == 0 && p0.stats.passUsed == 0);
    o.pass = -1;
    o.spec = false;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, nospec));
    CHECK(!nospec.stats.specOn && nospec.stats.tilesPass0 == 2);
    size_t diff = 0;
    for (size_t i = 0; i < per.rgba.size() && i < p1.rgba.size(); ++i) diff += per.rgba[i] != p1.rgba[i];
    std::printf("  without a spec map: per-tile vs pass 1: %zu bytes differ; pass 0 vs pass 1 equal %d\n", diff,
                p0.rgba == p1.rgba ? 1 : 0);
    CHECK(per.rgba.size() == p1.rgba.size() && per.rgba == p1.rgba);
    CHECK(p0.rgba == p1.rgba && nospec.rgba == p1.rgba);
    // a tile drawn with the module's own PS (no cache) counts for neither pass
    CHECK_OK(fx.r.setTileCache(1, 1, TileCache()));
    Options o2;
    o2.water = false;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o2, false, per));
    CHECK(per.stats.tilesPass0 == 1 && per.stats.tilesPass1 == 2 && per.stats.tilesWithoutCache == 1);
    cache->Release();
    spec->Release();
}

// The pass over the EXE's visible list (FrameStats::list*, for the live comparison with the EXE's tile+0x2FC): with 1-plot
// tiles and the EXE's LOD every listed tile is classed by its cache (t8: pass 0, cache only: pass 1, none), whether the
// module's own cull draws it or not, and listPass0Hash is FNV-1a 32 over the pass-0 indices. A fixed LOD level or a
// colour source other than Caches leaves it uncounted.
C5R_TEST(draw_pass_counts_over_the_exe_list) {
    Fixture fx;
    if (!fx.ok) return;
    CHECK_OK(fx.r.setHeightField(makeGrid(4, 4, ramp), 1));
    ID3D11ShaderResourceView* cache = solidTexture(fx.d.dev, 0xFF3080C0u);
    ID3D11ShaderResourceView* spec = solidTexture(fx.d.dev, 0xFFFFFFFFu);
    CHECK(cache != nullptr && spec != nullptr);
    if (!cache || !spec) return;
    const TileGrid tg = fx.r.tileGrid();
    CHECK(tg.tilesX == 4 && tg.tilesY == 4 && tg.tileSamples == 64);
    uint32_t want0 = 0, want1 = 0, wantNone = 0, h = 2166136261u;
    for (uint32_t y = 0; y < tg.tilesY; ++y)
        for (uint32_t x = 0; x < tg.tilesX; ++x) {
            const uint32_t i = y * tg.tilesX + x;
            TileCache tc;
            if (i % 5 == 4) {   // no cache at all: 4, 9, 14
                ++wantNone;
                continue;
            }
            tc.color = cache;
            tc.second = (i % 3 == 0) ? spec : nullptr;
            CHECK_OK(fx.r.setTileCache(x, y, tc));
            if (tc.second) {
                ++want0;
                for (int b = 0; b < 4; ++b) {
                    h ^= (i >> (8 * b)) & 0xFFu;
                    h *= 16777619u;
                }
            } else ++want1;
        }
    Camera cam = downCamera(0, 0, 640, 160, 120);
    Options o;
    o.water = false;
    terrainshot::ShotResult res;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, res));
    std::printf("  list: %u visible, pass 0 %u (want %u), pass 1 %u (want %u), no cache %u (want %u), hash %08x (want %08x)\n",
                res.stats.lodVisible, res.stats.listPass0, want0, res.stats.listPass1, want1, res.stats.listNoCache, wantNone,
                res.stats.listPass0Hash, h);
    CHECK(res.stats.listCounted);
    CHECK_EQ(res.stats.lodVisible, 16u);   // every tile is of class 3 (the ramp reaches 30.0) and in view
    CHECK_EQ(res.stats.listPass0, want0);
    CHECK_EQ(res.stats.listPass1, want1);
    CHECK_EQ(res.stats.listNoCache, wantNone);
    CHECK_EQ(res.stats.listPass0Hash, h);
    CHECK_EQ(res.stats.tilesPass0, want0);   // all of them are drawn here too
    o.lodLevel = 1;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, res));
    CHECK(!res.stats.listCounted && res.stats.listPass0 == 0);
    o.lodLevel = kLodExe;
    o.color = ColorSource::Material;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, res));
    CHECK(!res.stats.listCounted);
    cache->Release();
    spec->Release();
}

// With the install (C5R_GAME_DIR): t9 is specscreenmap.dds (Resource/DX9/TerrainTextures.fpk), BC3 1024 x 1024 with 11
// mips, and its bytes equal the EXE's t9 (resource 43161) of 3601's two stock captures: 1,398,128 bytes, FNV-1a 64
// 0xfff35e0f81f61405 over all mips [L, pass_inputs_0189.py: the capture's t9 equals the file's payload byte for byte].
// A draw with spec on (FlagB 1) changes only the pass-0 tile (its colour is t0 + t8 * t9 in linear light, so never
// darker than with spec off beyond the round trip's rounding), and leaves the pass-1 tiles bit-equal.
C5R_TEST(game_spec_map_is_the_exes_and_lights_pass0) {
    const std::string exe = test::exePath();
    const std::string game = test::gameDir();
    terrainshot::Device d;
    CHECK_OK(d.create(false));
    Renderer r;
    CHECK_OK(r.init(d.dev, wide(exe), wide(game)));
    CHECK(r.specMap() != nullptr);
    if (!r.specMap()) return;
    D3D11_TEXTURE2D_DESC td = {};
    size_t bytes = 0;
    const uint64_t h = blockFnv(d.dev, d.ctx, r.specMap(), 16, td, &bytes);
    std::printf("  t9: %ux%u, %u mips, format %d, %zu bytes, FNV-1a 0x%016llx\n", td.Width, td.Height, td.MipLevels,
                static_cast<int>(td.Format), bytes, static_cast<unsigned long long>(h));
    CHECK(td.Width == 1024 && td.Height == 1024 && td.MipLevels == 11);
    CHECK(td.Format == DXGI_FORMAT_BC3_UNORM || td.Format == DXGI_FORMAT_BC3_TYPELESS);
    CHECK_EQ(bytes, size_t(1398128));
    CHECK(h == 0xfff35e0f81f61405ull);
    CHECK_OK(r.setHeightField(makeGrid(4, 4, ramp)));
    ID3D11ShaderResourceView* cache = solidTexture(d.dev, 0xFF404040u);
    ID3D11ShaderResourceView* spec = solidTexture(d.dev, 0xFFFFFFFFu);
    CHECK(cache != nullptr && spec != nullptr);
    if (!cache || !spec) return;
    cacheEveryTile(r, cache);
    TileCache tc;
    tc.color = cache;
    tc.second = spec;
    CHECK_OK(r.setTileCache(0, 0, tc));   // tile (0, 0): x < 0, y < 0
    Camera cam = downCamera(0, 0, 640, 320, 240);
    Options o;
    o.water = false;
    terrainshot::ShotResult on, off;
    CHECK_OK(terrainshot::renderShot(d, r, cam, o, true, on));
    o.spec = false;
    CHECK_OK(terrainshot::renderShot(d, r, cam, o, true, off));
    CHECK(on.stats.specOn && !off.stats.specOn);
    CHECK(on.stats.tilesPass0 == 1 && on.stats.tilesPass1 == 3);
    size_t brighter = 0, darker = 0, inTile = 0, outsideDiff = 0, outside = 0;
    int maxUp = 0;
    for (size_t i = 0; i < static_cast<size_t>(on.width) * on.height; ++i) {
        if (off.world[i * 4 + 3] != 1.0f) continue;   // terrain pixels only
        const bool t00 = off.world[i * 4] < -1.0f && off.world[i * 4 + 1] < -1.0f;
        const bool other = off.world[i * 4] > 1.0f || off.world[i * 4 + 1] > 1.0f;
        if (t00) {
            ++inTile;
            int up = 0;
            for (int c = 0; c < 3; ++c) up = std::max(up, static_cast<int>(on.rgba[i * 4 + c]) - off.rgba[i * 4 + c]);
            for (int c = 0; c < 3; ++c) darker += static_cast<int>(on.rgba[i * 4 + c]) < static_cast<int>(off.rgba[i * 4 + c]) - 1;
            brighter += up > 4;
            maxUp = std::max(maxUp, up);
        } else if (other) {
            ++outside;
            for (int c = 0; c < 3; ++c) outsideDiff += on.rgba[i * 4 + c] != off.rgba[i * 4 + c];
        }
    }
    std::printf("  spec: tile (0, 0) %zu px, %zu brighter by > 4 (max +%d), %zu channels darker by > 1; other tiles %zu px, "
                "%zu channels differ\n", inTile, brighter, maxUp, darker, outside, outsideDiff);
    CHECK(inTile > 1000 && outside > 1000);
    CHECK(brighter > 100);
    CHECK_EQ(darker, size_t(0));
    CHECK_EQ(outsideDiff, size_t(0));
    cache->Release();
    spec->Release();
}

// Negative: an install whose fog ramp is there but whose spec map is missing, or does not parse, refuses the init with
// the file's name; nothing half-made (no ramp, no spec map, setHeightField: init not done). The fake install is a
// temporary folder with Assets/fogramp.dds (the install's bytes) and an empty Resource/.
C5R_TEST(negative_init_install_without_a_spec_map) {
    const std::string exe = test::exePath();
    const std::string game = test::gameDir();
    std::vector<uint8_t> rampDds;
    {
        gamefs::GameFiles fs;
        CHECK_OK(fs.open(wide(game)));
        CHECK_OK(fs.read("fogramp.dds", rampDds));
    }
    if (rampDds.empty()) return;
    terrainshot::Device d;
    CHECK_OK(d.create(false));
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    const std::string root = std::string(tmp) + "c5r_terrain_no_spec_0189";
    CreateDirectoryA(root.c_str(), nullptr);
    CreateDirectoryA((root + "\\Assets").c_str(), nullptr);
    CreateDirectoryA((root + "\\Resource").c_str(), nullptr);
    DeleteFileA((root + "\\Assets\\specscreenmap.dds").c_str());   // a leftover of an interrupted run
    CHECK(writeFile(root + "\\Assets\\fogramp.dds", rampDds));
    {
        Renderer r;
        const Status st = r.init(d.dev, wide(exe), wide(root));
        std::printf("  no spec map: %s\n", st.str().c_str());
        CHECK_ERR(st, Err::NotFound);
        CHECK(st.msg.find("specscreenmap.dds") != std::string::npos);
        CHECK(r.fogRamp() == nullptr && r.specMap() == nullptr);
        CHECK_ERR(r.setHeightField(makeGrid(4, 4, ramp)), Err::NotFound);
    }
    const std::vector<uint8_t> junk = {'D', 'D', 'S', ' ', 1, 2, 3};
    CHECK(writeFile(root + "\\Assets\\specscreenmap.dds", junk));
    {
        Renderer r;
        const Status st = r.init(d.dev, wide(exe), wide(root));
        std::printf("  a spec map that does not parse: %s\n", st.str().c_str());
        CHECK(!st.ok());
        CHECK(st.msg.find("specscreenmap.dds") != std::string::npos);
        CHECK(r.fogRamp() == nullptr && r.specMap() == nullptr);
        CHECK_ERR(r.setHeightField(makeGrid(4, 4, ramp)), Err::NotFound);
    }
    DeleteFileA((root + "\\Assets\\specscreenmap.dds").c_str());
    DeleteFileA((root + "\\Assets\\fogramp.dds").c_str());
    RemoveDirectoryA((root + "\\Assets").c_str());
    RemoveDirectoryA((root + "\\Resource").c_str());
    RemoveDirectoryA(root.c_str());
}

// ---------------------------------------------------------------- issue 0198: the install's files from a shared index

// With the install (C5R_GAME_DIR): init with an index the caller opened gives the same t2 (red channel of every texel),
// t9 (FNV-1a over all mips) and t7 source as init with the install folder, and costs only the reads (both timed, for
// the record: the folder's init builds the index, 1.3-1.5 s in 0188's measurement).
C5R_TEST(shared_index_init_equals_the_install_folder_init) {
    const std::string exe = test::exePath();
    const std::string game = test::gameDir();
    terrainshot::Device d;
    CHECK_OK(d.create(false));
    gamefs::GameFiles fs;
    const auto t0 = std::chrono::steady_clock::now();
    CHECK_OK(fs.open(wide(game)));
    const auto t1 = std::chrono::steady_clock::now();
    Renderer a, b;
    CHECK_OK(a.init(d.dev, wide(exe), fs));
    const auto t2 = std::chrono::steady_clock::now();
    CHECK_OK(b.init(d.dev, wide(exe), wide(game)));
    const auto t3 = std::chrono::steady_clock::now();
    auto ms = [](std::chrono::steady_clock::time_point x, std::chrono::steady_clock::time_point y) {
        return std::chrono::duration<double, std::milli>(y - x).count();
    };
    std::printf("  index open %.1f ms; init with the index %.1f ms; init with the folder %.1f ms\n", ms(t0, t1), ms(t1, t2),
                ms(t2, t3));
    CHECK(a.fogRamp() != nullptr && b.fogRamp() != nullptr && a.specMap() != nullptr && b.specMap() != nullptr);
    if (!a.fogRamp() || !b.fogRamp() || !a.specMap() || !b.specMap()) return;
    D3D11_TEXTURE2D_DESC da = {}, db = {};
    const std::vector<uint8_t> ra = redMip0(d.dev, d.ctx, a.fogRamp(), da), rb = redMip0(d.dev, d.ctx, b.fogRamp(), db);
    CHECK(da.Width == 128 && da.Height == 8 && da.MipLevels == 8);
    CHECK_EQ(ra.size(), size_t(128 * 8));
    CHECK(ra == rb);
    size_t na = 0, nb = 0;
    const uint64_t ha = blockFnv(d.dev, d.ctx, a.specMap(), 16, da, &na), hb = blockFnv(d.dev, d.ctx, b.specMap(), 16, db, &nb);
    CHECK_EQ(na, size_t(1398128));
    CHECK(ha == hb && na == nb && ha == 0xfff35e0f81f61405ull);
    CHECK(a.shadowLutFromFile() == b.shadowLutFromFile());
    CHECK(a.shadowLutFromFile() == fs.exists("TerrainShadow_Generic_ColorKey.tga"));
}

// The init reads the caller's index and opens none of its own: a fake install (Assets/fogramp.dds and
// Assets/specscreenmap.dds with the install's bytes, an empty Resource/) is indexed, then its Resource/ is removed. The
// index's init still succeeds (the loose files are read through it), while the folder's init now fails with the gamefs
// Io of an install without Resource/ (it would build a new index). The index outlives nothing: the renderer works after
// the index is destroyed (setHeightField Ok).
C5R_TEST(shared_index_is_read_not_reopened) {
    const std::string exe = test::exePath();
    const std::string game = test::gameDir();
    std::vector<uint8_t> rampDds, specDds;
    {
        gamefs::GameFiles fs;
        CHECK_OK(fs.open(wide(game)));
        CHECK_OK(fs.read("fogramp.dds", rampDds));
        CHECK_OK(fs.read("specscreenmap.dds", specDds));
    }
    if (rampDds.empty() || specDds.empty()) return;
    terrainshot::Device d;
    CHECK_OK(d.create(false));
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    const std::string root = std::string(tmp) + "c5r_terrain_shared_index_0198";
    CreateDirectoryA(root.c_str(), nullptr);
    CreateDirectoryA((root + "\\Assets").c_str(), nullptr);
    CreateDirectoryA((root + "\\Resource").c_str(), nullptr);
    CHECK(writeFile(root + "\\Assets\\fogramp.dds", rampDds));
    CHECK(writeFile(root + "\\Assets\\specscreenmap.dds", specDds));
    Renderer r;
    {
        gamefs::GameFiles fs;
        CHECK_OK(fs.open(wide(root)));
        CHECK(RemoveDirectoryA((root + "\\Resource").c_str()) != 0);
        CHECK_OK(r.init(d.dev, wide(exe), fs));
        CHECK(r.fogRamp() != nullptr && r.specMap() != nullptr && !r.shadowLutFromFile());
        Renderer folder;
        const Status st = folder.init(d.dev, wide(exe), wide(root));
        std::printf("  the folder's init without Resource/: %s\n", st.str().c_str());
        CHECK_ERR(st, Err::Io);
        CHECK(folder.fogRamp() == nullptr);
    }
    CHECK_OK(r.setHeightField(makeGrid(4, 4, ramp)));
    DeleteFileA((root + "\\Assets\\specscreenmap.dds").c_str());
    DeleteFileA((root + "\\Assets\\fogramp.dds").c_str());
    RemoveDirectoryA((root + "\\Assets").c_str());
    RemoveDirectoryA((root + "\\Resource").c_str());
    RemoveDirectoryA(root.c_str());
}

// Negative: an index without the ramp, or whose spec map does not parse, refuses the init naming the file, and nothing
// is half-made (no ramp, no spec map, setHeightField: init not done); a null device is BadLayout before any read; the
// EXE's errors come first, as with the folder (a missing EXE is Io with a good index).
C5R_TEST(negative_shared_index_without_a_ramp_or_with_a_bad_spec_map) {
    const std::string exe = test::exePath();
    const std::string game = test::gameDir();
    std::vector<uint8_t> rampDds;
    {
        gamefs::GameFiles fs;
        CHECK_OK(fs.open(wide(game)));
        CHECK_OK(fs.read("fogramp.dds", rampDds));
    }
    if (rampDds.empty()) return;
    terrainshot::Device d;
    CHECK_OK(d.create(false));
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    const std::string root = std::string(tmp) + "c5r_terrain_shared_bad_0198";
    CreateDirectoryA(root.c_str(), nullptr);
    CreateDirectoryA((root + "\\Assets").c_str(), nullptr);
    CreateDirectoryA((root + "\\Resource").c_str(), nullptr);
    DeleteFileA((root + "\\Assets\\fogramp.dds").c_str());         // leftovers of an interrupted run
    DeleteFileA((root + "\\Assets\\specscreenmap.dds").c_str());
    {
        gamefs::GameFiles fs;
        CHECK_OK(fs.open(wide(root)));
        Renderer r;
        const Status st = r.init(d.dev, wide(exe), fs);
        std::printf("  an index without the ramp: %s\n", st.str().c_str());
        CHECK_ERR(st, Err::NotFound);
        CHECK(st.msg.find("fogramp.dds") != std::string::npos);
        CHECK(r.fogRamp() == nullptr && r.specMap() == nullptr);
        CHECK_ERR(r.setHeightField(makeGrid(4, 4, ramp)), Err::NotFound);
        CHECK_ERR(r.init(nullptr, wide(exe), fs), Err::BadLayout);
        CHECK_ERR(r.init(d.dev, L"Z:/no/such/CivilizationV_DX11.exe", fs), Err::Io);
    }
    CHECK(writeFile(root + "\\Assets\\fogramp.dds", rampDds));
    const std::vector<uint8_t> junk = {'D', 'D', 'S', ' ', 1, 2, 3};
    CHECK(writeFile(root + "\\Assets\\specscreenmap.dds", junk));
    {
        gamefs::GameFiles fs;
        CHECK_OK(fs.open(wide(root)));
        Renderer r;
        const Status st = r.init(d.dev, wide(exe), fs);
        std::printf("  an index whose spec map does not parse: %s\n", st.str().c_str());
        CHECK(!st.ok());
        CHECK(st.msg.find("specscreenmap.dds") != std::string::npos);
        CHECK(r.fogRamp() == nullptr && r.specMap() == nullptr);
        CHECK_ERR(r.setHeightField(makeGrid(4, 4, ramp)), Err::NotFound);
    }
    DeleteFileA((root + "\\Assets\\specscreenmap.dds").c_str());
    DeleteFileA((root + "\\Assets\\fogramp.dds").c_str());
    RemoveDirectoryA((root + "\\Assets").c_str());
    RemoveDirectoryA((root + "\\Resource").c_str());
    RemoveDirectoryA(root.c_str());
}

// ---------------------------------------------------------------- issue 0190: the models' shadows (t3, ShadowMatrix0, Arg6, t7)

namespace {
// A w x h R16_UNORM texture (the EXE's ShadowBlur format), one mip.
ID3D11ShaderResourceView* r16Texture(ID3D11Device* dev, uint32_t w, uint32_t h, const std::vector<uint16_t>& px) {
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = w;
    d.Height = h;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R16_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA s = {px.data(), w * 2, 0};
    ID3D11Texture2D* t = nullptr;
    ID3D11ShaderResourceView* v = nullptr;
    if (FAILED(dev->CreateTexture2D(&d, &s, &t))) return nullptr;
    dev->CreateShaderResourceView(t, nullptr, &v);
    t->Release();
    return v;
}
// A ShadowMatrix0 whose p.x is x * sx + tx (y, z, w as identity: p.w = 1).
void shadowX(float m[16], float sx, float tx) {
    const float v[16] = {sx, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, tx, 0, 0, 1};
    std::memcpy(m, v, sizeof v);
}
// FNV-1a 64 over a 32^3 RGBA8 volume's texels (rows of 128 bytes, in x, y, z order).
uint64_t volumeFnv(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* v, D3D11_TEXTURE3D_DESC& d) {
    ID3D11Resource* r = nullptr;
    v->GetResource(&r);
    ID3D11Texture3D* t = nullptr;
    if (!r || FAILED(r->QueryInterface(__uuidof(ID3D11Texture3D), reinterpret_cast<void**>(&t)))) {
        if (r) r->Release();
        return 0;
    }
    t->GetDesc(&d);
    D3D11_TEXTURE3D_DESC s = d;
    s.Usage = D3D11_USAGE_STAGING;
    s.BindFlags = 0;
    s.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    s.MiscFlags = 0;
    uint64_t h = 0xcbf29ce484222325ull;
    ID3D11Texture3D* st = nullptr;
    if (SUCCEEDED(dev->CreateTexture3D(&s, nullptr, &st))) {
        ctx->CopyResource(st, t);
        D3D11_MAPPED_SUBRESOURCE map;
        if (SUCCEEDED(ctx->Map(st, 0, D3D11_MAP_READ, 0, &map))) {
            for (uint32_t z = 0; z < d.Depth; ++z)
                for (uint32_t y = 0; y < d.Height; ++y) {
                    const uint8_t* row = static_cast<const uint8_t*>(map.pData) + static_cast<size_t>(z) * map.DepthPitch +
                                         static_cast<size_t>(y) * map.RowPitch;
                    for (uint32_t i = 0; i < d.Width * 4; ++i) {
                        h ^= row[i];
                        h *= 0x100000001b3ull;
                    }
                }
            ctx->Unmap(st, 0);
        }
        st->Release();
    }
    t->Release();
    r->Release();
    return h;
}
size_t bytesDiffer(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    size_t n = a.size() == b.size() ? 0 : std::max(a.size(), b.size());
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) n += a[i] != b[i];
    return n;
}
} // namespace

// Negative: a shadow matrix that is not finite (each wrap copy, also without a view), a strength not finite or outside
// [0, 1], a shadow view of another format or dimension: BadLayout, the stats untouched. A good R16 view draws with
// shadowOn; the default draws without.
C5R_TEST(negative_shadow_checks) {
    Fixture fx;
    if (!fx.ok) return;
    Renderer& r = fx.r;
    CHECK_OK(r.setHeightField(makeGrid(4, 4, ramp)));
    Target rt(fx.d.dev, 64, 48, DXGI_FORMAT_R8G8B8A8_UNORM);
    Camera cam = downCamera(0, 0, 100, 64, 48);
    FrameStats st;
    st.tilesDrawn = 99;
    for (int k = 0; k < 3; ++k) {
        Options o;
        o.shadow.matrix[k][5] = std::nanf("");
        CHECK_ERR(r.draw(fx.d.ctx, rt.rtv, nullptr, cam, o, &st), Err::BadLayout);
        o.shadow.matrix[k][5] = -INFINITY;
        CHECK_ERR(r.draw(fx.d.ctx, rt.rtv, nullptr, cam, o, &st), Err::BadLayout);
    }
    const float badStrength[4] = {std::nanf(""), INFINITY, -0.01f, 1.01f};
    for (float s : badStrength) {
        Options o;
        o.shadow.strength = s;
        CHECK_ERR(r.draw(fx.d.ctx, rt.rtv, nullptr, cam, o, &st), Err::BadLayout);
    }
    ID3D11ShaderResourceView* rgba = pixelTexture(fx.d.dev, 2, 2, std::vector<uint32_t>(4, 0));
    ID3D11ShaderResourceView* flt = pixelTexture(fx.d.dev, 2, 2, std::vector<uint32_t>(4, 0), DXGI_FORMAT_R32_FLOAT);
    ID3D11ShaderResourceView* good = r16Texture(fx.d.dev, 2, 2, std::vector<uint16_t>(4, 0));
    CHECK(rgba && flt && good);
    if (!rgba || !flt || !good) return;
    Options o;
    o.shadow.srv = rgba;
    CHECK_ERR(r.draw(fx.d.ctx, rt.rtv, nullptr, cam, o, &st), Err::BadLayout);
    o.shadow.srv = flt;
    CHECK_ERR(r.draw(fx.d.ctx, rt.rtv, nullptr, cam, o, &st), Err::BadLayout);
    CHECK_EQ(st.tilesDrawn, 99u);
    CHECK(!st.shadowOn);
    o.shadow.srv = good;
    CHECK_OK(r.draw(fx.d.ctx, rt.rtv, nullptr, cam, o, &st));
    CHECK(st.shadowOn && !st.shadowLutFromFile && st.tilesDrawn > 0);   // no install: the identity t7
    CHECK_OK(r.draw(fx.d.ctx, rt.rtv, nullptr, cam, Options(), &st));
    CHECK(!st.shadowOn);
    float m[16];
    Shadow none;
    shadowMatrixForCopy(none, 1, 256.0f, m);   // no view: identity
    CHECK(m[0] == 1 && m[5] == 1 && m[10] == 1 && m[15] == 1 && m[12] == 0 && m[13] == 0);
    rgba->Release();
    flt->Release();
    good->Release();
}

// Issue 4308: the EXE's quality 1 binds ShadowRaw (R16G16B16A16_FLOAT, the back buffer's size) as the shadow map; the PS
// reads .r. A float map whose r is 1 (g, b, a 0) gives the picture of an R16 map of ones, and one whose r is 0 (g, b, a
// 1) the picture of an R16 map of zeros, bit for bit; R32_FLOAT stays refused (negative_shadow_checks).
C5R_TEST(shadow_q1_float_map_reads_red) {
    Fixture fx;
    if (!fx.ok) return;
    CHECK_OK(fx.r.setHeightField(makeGrid(4, 4, ramp, true)));
    ID3D11ShaderResourceView* cache = solidTexture(fx.d.dev, 0xFF3080C0u);
    ID3D11ShaderResourceView* ones = r16Texture(fx.d.dev, 4, 4, std::vector<uint16_t>(16, 0xFFFF));
    ID3D11ShaderResourceView* zeros = r16Texture(fx.d.dev, 4, 4, std::vector<uint16_t>(16, 0));
    auto f16 = [&](uint16_t r, uint16_t gba) -> ID3D11ShaderResourceView* {
        std::vector<uint16_t> px;
        for (int i = 0; i < 16; ++i) { px.push_back(r); px.push_back(gba); px.push_back(gba); px.push_back(gba); }
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = d.Height = 4;
        d.MipLevels = d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA s = {px.data(), 4 * 8, 0};
        ID3D11Texture2D* t = nullptr;
        ID3D11ShaderResourceView* v = nullptr;
        if (FAILED(fx.d.dev->CreateTexture2D(&d, &s, &t))) return nullptr;
        fx.d.dev->CreateShaderResourceView(t, nullptr, &v);
        t->Release();
        return v;
    };
    ID3D11ShaderResourceView* fOne = f16(0x3C00, 0);        // half 1.0 in r
    ID3D11ShaderResourceView* fZero = f16(0, 0x3C00);       // r 0, the rest 1.0
    CHECK(cache && ones && zeros && fOne && fZero);
    if (!cache || !ones || !zeros || !fOne || !fZero) return;
    cacheEveryTile(fx.r, cache);
    Camera cam = downCamera(120, 0, 120, 160, 120);
    Options o;
    o.water = false;
    shadowX(o.shadow.matrix[1], 0.01f, 0.3f);
    terrainshot::ShotResult a, b, c, e;
    o.shadow.srv = ones;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, a));
    o.shadow.srv = fOne;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, b));
    o.shadow.srv = zeros;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, c));
    o.shadow.srv = fZero;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, e));
    CHECK(b.stats.shadowOn && e.stats.shadowOn);
    const size_t d1 = bytesDiffer(a.rgba, b.rgba), d0 = bytesDiffer(c.rgba, e.rgba), dOn = bytesDiffer(a.rgba, c.rgba);
    std::printf("  q1 float map: r = 1 against R16 ones %zu bytes differ, r = 0 against R16 zeros %zu; ones against zeros %zu\n", d1,
                d0, dOn);
    CHECK_EQ(d1, size_t(0));
    CHECK_EQ(d0, size_t(0));
    CHECK(dOn > 0);   // the shadow shows at all
    cache->Release();
    ones->Release();
    zeros->Release();
    fOne->Release();
    fZero->Release();
}

// The default is the picture before 0190, bit for bit, on a wrapping field with caches: no shadow against a bound shadow
// map of zeros at strength 1 (saturate(t3) = 0), and against a map of ones at strength 0 (Arg6 0), under any matrices.
C5R_TEST(draw_shadow_off_is_bit_equal) {
    Fixture fx;
    if (!fx.ok) return;
    CHECK_OK(fx.r.setHeightField(makeGrid(4, 4, ramp, true)));
    ID3D11ShaderResourceView* cache = solidTexture(fx.d.dev, 0xFF3080C0u);
    ID3D11ShaderResourceView* zeros = r16Texture(fx.d.dev, 4, 4, std::vector<uint16_t>(16, 0));
    ID3D11ShaderResourceView* ones = r16Texture(fx.d.dev, 4, 4, std::vector<uint16_t>(16, 0xFFFF));
    CHECK(cache && zeros && ones);
    if (!cache || !zeros || !ones) return;
    cacheEveryTile(fx.r, cache);
    Camera cam = downCamera(120, 0, 120, 160, 120);   // with the wrap copy in view
    Options o;
    o.water = false;
    terrainshot::ShotResult a, b, c;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, a));
    o.shadow.srv = zeros;
    shadowX(o.shadow.matrix[0], 0.01f, 0.3f);
    shadowX(o.shadow.matrix[1], 0.003f, -0.2f);
    shadowX(o.shadow.matrix[2], -0.02f, 2.0f);
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, b));
    o.shadow.srv = ones;
    o.shadow.strength = 0.0f;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, c));
    CHECK(!a.stats.shadowOn && b.stats.shadowOn && c.stats.shadowOn);
    const size_t db = bytesDiffer(a.rgba, b.rgba), dc = bytesDiffer(a.rgba, c.rgba);
    std::printf("  shadow off against zeros at strength 1: %zu bytes differ; ones at strength 0: %zu\n", db, dc);
    CHECK_EQ(db, size_t(0));
    CHECK_EQ(dc, size_t(0));
    cache->Release();
    zeros->Release();
    ones->Release();
}

// t3 through ShadowMatrix0, per wrap copy, with the install's TerrainShadow LUT as t7: a 256 x 1 map (texels 0..127
// shadow 1, 128..255 shadow 0; sampler state 0 wraps), u = 0.5 p.x / p.w - 0.5 with p = (world, 1) * ShadowMatrix0 (the
// PS's mapping, terrain-surface.md 9; v = -0.5 p.y / p.w - 0.5, any row of a 1-high map). Copy 0: u = x / 256, so its
// ground at x in (90, 120) is shadowed. Copy +1: Translation(-W) * matrix[2] with matrix[2] u = x / 512 + 0.8, so its
// ground (original x in (-120, -90)) has u in (0.56, 0.63): unshadowed; a module that bound matrix[2] unshifted would
// give u in (0.07, 0.12): shadowed. Shadowed ground equals the full-shadow picture (a map of ones), unshadowed ground is
// bit-equal to the shadow-off picture, strength 0.5 lies between off and full.
// The LUT's bytes are the EXE's t7 in 3601's captures (FNV-1a 64 0xdf1432ef669171e5 over the 131,072 bytes [L]).
C5R_TEST(game_shadow_map_matrix_and_lut_are_the_exes) {
    const std::string exe = test::exePath();
    const std::string game = test::gameDir();
    terrainshot::Device d;
    CHECK_OK(d.create(false));
    Renderer r;
    CHECK_OK(r.init(d.dev, wide(exe), wide(game)));
    CHECK(r.shadowLutFromFile() && r.shadowLut() != nullptr);
    if (!r.shadowLut()) return;
    D3D11_TEXTURE3D_DESC td = {};
    const uint64_t h = volumeFnv(d.dev, d.ctx, r.shadowLut(), td);
    std::printf("  t7: %ux%ux%u, format %d, FNV-1a 0x%016llx\n", td.Width, td.Height, td.Depth, static_cast<int>(td.Format),
                static_cast<unsigned long long>(h));
    CHECK(td.Width == 32 && td.Height == 32 && td.Depth == 32 && td.Format == DXGI_FORMAT_R8G8B8A8_UNORM);
    CHECK(h == 0xdf1432ef669171e5ull);
    heightfield::HeightGrid g = makeGrid(4, 4, [](uint32_t, uint32_t) { return uint16_t(0); }, true);
    CHECK_OK(r.setHeightField(g));
    ID3D11ShaderResourceView* cache = solidTexture(d.dev, 0xFF60A0C0u);
    std::vector<uint16_t> halfPx(256, 0);
    for (int i = 0; i < 128; ++i) halfPx[i] = 0xFFFF;
    ID3D11ShaderResourceView* half = r16Texture(d.dev, 256, 1, halfPx);
    ID3D11ShaderResourceView* ones = r16Texture(d.dev, 256, 1, std::vector<uint16_t>(256, 0xFFFF));
    CHECK(cache && half && ones);
    if (!cache || !half || !ones) return;
    cacheEveryTile(r, cache);
    Camera cam = downCamera(128, 0, 120, 160, 120);          // x in about [85, 171]: copy 0 left of 128, copy +1 right
                                                             // (W = wrapPeriod = (samplesX - 1) * sx = 255, 4314)
    Options o;
    o.water = false;
    o.fog.enabled = false;                                   // the colour change is the shadow's alone
    o.spec = false;
    terrainshot::ShotResult off, on, full, mid;
    CHECK_OK(terrainshot::renderShot(d, r, cam, o, true, off));
    o.shadow.srv = half;
    shadowX(o.shadow.matrix[1], 1.0f / 128, 1.0f);   // u = 0.5 (x / 128 + 1) - 0.5 = x / 256
    shadowX(o.shadow.matrix[2], 1.0f / 256, 2.6f);   // u = 0.5 (x / 256 + 2.6) - 0.5 = x / 512 + 0.8
    CHECK_OK(terrainshot::renderShot(d, r, cam, o, false, on));
    CHECK(on.stats.shadowOn && on.stats.shadowLutFromFile);
    o.shadow.strength = 0.5f;
    CHECK_OK(terrainshot::renderShot(d, r, cam, o, false, mid));
    o.shadow.strength = 1.0f;
    o.shadow.srv = ones;
    CHECK_OK(terrainshot::renderShot(d, r, cam, o, false, full));
    size_t inShadow = 0, shadowBad = 0, copy1 = 0, copy1Bad = 0, midBad = 0, changed = 0;
    for (size_t i = 0; i < static_cast<size_t>(off.width) * off.height; ++i) {
        if (off.world[i * 4 + 3] != 1.0f) continue;
        const float x = off.world[i * 4];
        const uint8_t* a = &off.rgba[i * 4];
        const uint8_t* b = &on.rgba[i * 4];
        const uint8_t* f = &full.rgba[i * 4];
        const uint8_t* m = &mid.rgba[i * 4];
        const bool same = a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
        const bool asFull = f[0] == b[0] && f[1] == b[1] && f[2] == b[2];
        if (x > 90 && x < 120) {            // copy 0, u = x / 256 < 0.47: the map's shadow texel
            ++inShadow;
            shadowBad += !asFull;
            changed += !same;
            for (int c = 0; c < 3; ++c)
                midBad += m[c] < std::min(a[c], f[c]) - 1 || m[c] > std::max(a[c], f[c]) + 1;
        } else if (x > 136 && x < 166) {    // copy +1: u = (x - W) / 512 + 0.8 in (0.56, 0.63): no shadow
            ++copy1;
            copy1Bad += !same;
        }
    }
    std::printf("  shadow: copy 0 shadowed %zu px (%zu not the full-shadow colour, %zu changed), copy +1 %zu px (%zu "
                "changed); strength 0.5 outside [off, full] on %zu channels; off %d,%d,%d full %d,%d,%d\n", inShadow,
                shadowBad, changed, copy1, copy1Bad, midBad, off.rgba[0], off.rgba[1], off.rgba[2], full.rgba[0],
                full.rgba[1], full.rgba[2]);
    CHECK(inShadow > 1000 && copy1 > 1000);
    CHECK_EQ(shadowBad, size_t(0));
    CHECK(changed > inShadow * 9 / 10);   // the TerrainShadow LUT moves the colour
    CHECK_EQ(copy1Bad, size_t(0));
    CHECK_EQ(midBad, size_t(0));
    cache->Release();
    half->Release();
    ones->Release();
}

// Negative: an install whose TerrainShadow LUT is missing keeps the identity t7 (as the EXE's loader); one that does not
// parse refuses the init with the file's name, nothing half-made. The fake install holds the real fog ramp and spec map.
C5R_TEST(negative_init_install_shadow_lut) {
    const std::string exe = test::exePath();
    const std::string game = test::gameDir();
    std::vector<uint8_t> rampDds, specDds;
    {
        gamefs::GameFiles fs;
        CHECK_OK(fs.open(wide(game)));
        CHECK_OK(fs.read("fogramp.dds", rampDds));
        CHECK_OK(fs.read("specscreenmap.dds", specDds));
    }
    if (rampDds.empty() || specDds.empty()) return;
    terrainshot::Device d;
    CHECK_OK(d.create(false));
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    const std::string root = std::string(tmp) + "c5r_terrain_shadow_lut_0190";
    const std::string lutPath = root + "\\Assets\\TerrainShadow_Generic_ColorKey.tga";
    CreateDirectoryA(root.c_str(), nullptr);
    CreateDirectoryA((root + "\\Assets").c_str(), nullptr);
    CreateDirectoryA((root + "\\Resource").c_str(), nullptr);
    DeleteFileA(lutPath.c_str());   // a leftover of an interrupted run
    CHECK(writeFile(root + "\\Assets\\fogramp.dds", rampDds));
    CHECK(writeFile(root + "\\Assets\\specscreenmap.dds", specDds));
    {
        Renderer r;
        CHECK_OK(r.init(d.dev, wide(exe), wide(root)));
        CHECK(!r.shadowLutFromFile() && r.shadowLut() != nullptr);   // the identity volume
        std::printf("  no TerrainShadow LUT: init ok, identity t7\n");
    }
    std::vector<uint8_t> junk(18, 0);
    junk[2] = 2;          // true colour
    junk[12] = 4;         // 4 x 4 pixels: fewer than 32^3
    junk[14] = 4;
    junk[16] = 32;
    junk.resize(18 + 64, 0);
    CHECK(writeFile(lutPath, junk));
    {
        Renderer r;
        const Status st = r.init(d.dev, wide(exe), wide(root));
        std::printf("  a TerrainShadow LUT that does not parse: %s\n", st.str().c_str());
        CHECK_ERR(st, Err::BadLayout);
        CHECK(st.msg.find("TerrainShadow_Generic_ColorKey.tga") != std::string::npos);
        CHECK(r.shadowLut() == nullptr && !r.shadowLutFromFile() && r.fogRamp() == nullptr);
        CHECK_ERR(r.setHeightField(makeGrid(4, 4, ramp)), Err::NotFound);
    }
    DeleteFileA(lutPath.c_str());
    DeleteFileA((root + "\\Assets\\specscreenmap.dds").c_str());
    DeleteFileA((root + "\\Assets\\fogramp.dds").c_str());
    RemoveDirectoryA((root + "\\Assets").c_str());
    RemoveDirectoryA((root + "\\Resource").c_str());
    RemoveDirectoryA(root.c_str());
}

// A map that does not wrap draws only copy 0: its picture does not depend on the wrap copies' matrices (proj[0], [2],
// matrix[0], [2]), bit for bit; with no copy drawn the period changes nothing of it (issue 4314's "a non-wrapping map
// bit for bit as before": the draw's only use of the period is the shift k * period of copies k = -1, +1).
C5R_TEST(draw_without_wrap_ignores_the_copies) {
    Fixture fx;
    if (!fx.ok) return;
    CHECK_OK(fx.r.setHeightField(makeGrid(4, 4, ramp, false)));
    ID3D11ShaderResourceView* cache = solidTexture(fx.d.dev, 0xFF3080C0u);
    std::vector<uint32_t> ramp256(256);
    for (uint32_t i = 0; i < 256; ++i) ramp256[i] = 0x80000000u | (i << 8) | i;
    ID3D11ShaderResourceView* ovl = pixelTexture(fx.d.dev, 256, 1, ramp256);
    std::vector<uint16_t> halfPx(256, 0);
    for (int i = 0; i < 128; ++i) halfPx[i] = 0xFFFF;
    ID3D11ShaderResourceView* sh = r16Texture(fx.d.dev, 256, 1, halfPx);
    CHECK(cache && ovl && sh);
    if (!cache || !ovl || !sh) return;
    cacheEveryTile(fx.r, cache);
    Camera cam = downCamera(120, 0, 120, 160, 120);   // the map's east edge: a wrapping map would show copy +1 here
    Options o;
    o.water = false;
    o.overlay.srv = ovl;
    o.shadow.srv = sh;
    projX(o.overlay.proj[1], 1.0f / 64, -1.0f);
    shadowX(o.shadow.matrix[1], 1.0f / 128, 1.0f);
    terrainshot::ShotResult a, b;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, true, a));
    projX(o.overlay.proj[0], 0.3f, 7.0f);
    projX(o.overlay.proj[2], -0.02f, -3.0f);
    shadowX(o.shadow.matrix[0], 0.05f, -1.0f);
    shadowX(o.shadow.matrix[2], -0.01f, 2.0f);
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, true, b));
    size_t beyond = 0;
    const float east = static_cast<float>(4 * 64) * 1.0f - 128.0f;   // samplesX * sx - ox: no ground east of it
    for (size_t i = 0; i < static_cast<size_t>(a.width) * a.height; ++i)
        beyond += a.world[i * 4 + 3] == 1.0f && a.world[i * 4] > east + 1.0f;
    const size_t d = bytesDiffer(a.rgba, b.rgba);
    std::printf("  no wrap: %zu bytes differ under other copy matrices; %zu px beyond the east edge; tiles drawn %u\n", d,
                beyond, a.stats.tilesDrawn);
    CHECK_EQ(d, size_t(0));
    CHECK(a.world == b.world);
    CHECK_EQ(beyond, size_t(0));
    CHECK(a.stats.tilesDrawn == b.stats.tilesDrawn && a.stats.tilesDrawn > 0);
    cache->Release();
    ovl->Release();
    sh->Release();
}

C5R_TEST_MAIN
