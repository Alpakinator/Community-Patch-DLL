// c5r::terrain: the fog of war's inputs (epic 0168, issue 3953; research/05-graphics-composition/fog-of-war.md 7.4).
// Options::fow binds the FOW mask (t4), the cloud base (t5), the tile style's TerrainFOW LUT (t6) and FOWAnim
// (TerrainFrame 0x090) into the EXE's own TerrainShader PSs. Checked here on a windowless device with the EXE's programs
// (C5R_EXE; without it the draw tests skip, the executable exits 77 and render/build.py --test fails the run):
//   - off (the default) is bit-equal to the picture before, and so is the fog on with an all-visible mask;
//   - unexplored = the cloud base; seen = the tile style's LUT; visible = untouched;
//   - FOWAnim moves the cloud uv as (world.xy + zw) * xy;
//   - every check has a negative test (null mask or cloud base with the fog on, a mask view of the wrong format, a
//     cloud base that is not 2D, FOWAnim not finite, bad LUT files, styles and tiles out of range) asserting the error
//     and that nothing changed;
//   - with C5R_GAME_DIR: the five TerrainFOW volumes from the install; with C5R_FOW_CAPTURE_LUT (a captured t6 volume,
//     32^3 RGBA8, e.g. 3951's 2473_49245115.bin) the volume of style C5R_FOW_CAPTURE_STYLE (default 0) must equal it.
// Issue 4305 (epic 0169) adds t7 per tile style at the end: the TerrainShadow volumes per style (the TGA rule, the
// install's bytes, a crafted file per style bound to its style's tiles, a bad style file failing init).
// Own code; synthetic inputs only, no game bytes.
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "c5r/bake/heightpage.h"
#include "c5r/bake/post.h"
#include "c5r/fow/fow.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/terrain/terrain.h"
#include "terrainshot_core.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::terrain;

namespace {

std::wstring wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }

// A flat field of mapW x mapH plots at drawn height 40 * 0.2 = 8 (no water plane: the tests turn it off).
heightfield::HeightGrid flatGrid(uint32_t mapW, uint32_t mapH, bool wrap) {
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
    g.waterZ = 1.0f;
    g.h.assign(static_cast<size_t>(g.samplesX) * g.samplesY, static_cast<uint16_t>(40u << 8));
    return g;
}

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
ID3D11ShaderResourceView* solid(ID3D11Device* dev, uint32_t rgba) { return pixelTexture(dev, 4, 4, std::vector<uint32_t>(16, rgba)); }

void cacheEveryTile(Renderer& r, ID3D11ShaderResourceView* c) {
    const TileGrid tg = r.tileGrid();
    TileCache tc;
    tc.color = c;
    for (uint32_t y = 0; y < tg.tilesY; ++y)
        for (uint32_t x = 0; x < tg.tilesX; ++x) r.setTileCache(x, y, tc);
}

// A 32-bit uncompressed TGA (type 2) of w x h pixels, BGRA in the file, bottom-up unless topDown; pixel (x, y) in
// reading order (y = 0 the top row) = f(x, y) as RGBA.
template <class F> std::vector<uint8_t> makeTga(uint32_t w, uint32_t h, bool topDown, F f, uint8_t type = 2, uint8_t bpp = 32) {
    std::vector<uint8_t> t(18 + static_cast<size_t>(w) * h * 4, 0);
    t[2] = type;
    t[12] = static_cast<uint8_t>(w);
    t[13] = static_cast<uint8_t>(w >> 8);
    t[14] = static_cast<uint8_t>(h);
    t[15] = static_cast<uint8_t>(h >> 8);
    t[16] = bpp;
    t[17] = topDown ? 0x28 : 0x08;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t c = f(x, y);
            const uint32_t fileRow = topDown ? y : h - 1 - y;
            uint8_t* p = &t[18 + (static_cast<size_t>(fileRow) * w + x) * 4];
            p[0] = static_cast<uint8_t>(c >> 16);  // B
            p[1] = static_cast<uint8_t>(c >> 8);   // G
            p[2] = static_cast<uint8_t>(c);        // R
            p[3] = static_cast<uint8_t>(c >> 24);  // A
        }
    return t;
}

// The bytes of a 3D view's texture (RGBA8, x fastest, then y, then z).
std::vector<uint8_t> readVolume(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* v) {
    std::vector<uint8_t> out;
    if (!v) return out;
    ID3D11Resource* res = nullptr;
    v->GetResource(&res);
    ID3D11Texture3D* t = nullptr;
    if (!res || FAILED(res->QueryInterface(__uuidof(ID3D11Texture3D), reinterpret_cast<void**>(&t))) || !t) {
        if (res) res->Release();
        return out;
    }
    D3D11_TEXTURE3D_DESC d;
    t->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    ID3D11Texture3D* s = nullptr;
    if (SUCCEEDED(dev->CreateTexture3D(&d, nullptr, &s))) {
        ctx->CopyResource(s, t);
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx->Map(s, 0, D3D11_MAP_READ, 0, &m))) {
            out.resize(static_cast<size_t>(d.Width) * d.Height * d.Depth * 4);
            for (uint32_t z = 0; z < d.Depth; ++z)
                for (uint32_t y = 0; y < d.Height; ++y)
                    std::memcpy(&out[(static_cast<size_t>(z) * d.Height + y) * d.Width * 4],
                                static_cast<const uint8_t*>(m.pData) + z * m.DepthPitch + y * m.RowPitch, d.Width * 4);
            ctx->Unmap(s, 0);
        }
        s->Release();
    }
    t->Release();
    res->Release();
    return out;
}

uint64_t fnv64(const std::vector<uint8_t>& v) {
    uint64_t h = 1469598103934665603ull;
    for (uint8_t b : v) {
        h ^= b;
        h *= 1099511628211ull;
    }
    return h;
}

struct Fixture {
    terrainshot::Device d;
    Renderer r;
    bool ok = false;
    explicit Fixture(bool game = false) {
        const std::string exe = test::exePath();   // skips without C5R_EXE
        const std::string gd = game ? test::gameDir() : std::string();
        Status st = d.create(false);
        if (!st.ok()) {
            test::fail(__FILE__, __LINE__, "device: " + st.str());
            return;
        }
        st = r.init(d.dev, wide(exe), wide(gd));
        if (!st.ok()) {
            test::fail(__FILE__, __LINE__, "init: " + st.str());
            return;
        }
        ok = true;
    }
};

size_t countNear(const std::vector<uint8_t>& rgba, int r, int g, int b, int tol) {
    size_t n = 0;
    for (size_t i = 0; i + 3 < rgba.size(); i += 4)
        if (std::abs(rgba[i] - r) <= tol && std::abs(rgba[i + 1] - g) <= tol && std::abs(rgba[i + 2] - b) <= tol) ++n;
    return n;
}

const uint32_t kCache = 0xFF3080C0u;   // (192, 128, 48)
const uint32_t kCloud = 0xFFA05A28u;   // (40, 90, 160)
const uint32_t kSeen = 0xFF0000FFu, kVisible = 0xFFFF00FFu, kUnexplored = 0xFF00FF00u, kAllOnes = 0xFFFFFFFFu;

} // namespace

// ---------------------------------------------------------------- before init / field (no EXE needed)

C5R_TEST(fow_calls_before_init_and_field) {
    Renderer r;
    const std::vector<uint8_t> tga = makeTga(1024, 32, false, [](uint32_t, uint32_t) { return 0xFF000000u; });
    CHECK_ERR(r.setFowLut(0, tga.data(), tga.size()), Err::NotFound);
    CHECK_ERR(r.setTileStyle(0, 0, 1), Err::NotFound);
    const uint8_t art[4] = {0, 1, 2, 3};
    CHECK_ERR(r.setTileStyles(art, 2, 2, false), Err::NotFound);
    CHECK(r.fowLut(0) == nullptr);
    CHECK_EQ(r.fowLutsFromFile(), 0u);
    CHECK_EQ(r.tileStyleAt(0, 0), 0u);
    Options o;
    CHECK(!o.fow.enabled && !o.fow.mask && !o.fow.cloudBase);
    for (float a : o.fow.anim) CHECK_EQ(a, 0.0f);
}

// ---------------------------------------------------------------- draws with the EXE's programs

// Off (the default) is the picture before 3953; the fog on with every plot visible ((1, 0, 1, 1) at the centre of a
// visible plot, and the old constant (1, 1, 1, 1)) and FOWAnim 0 changes no byte, whatever the tiles' styles; the wrap
// copy is in view.
C5R_TEST(fow_default_is_bit_equal) {
    Fixture fx;
    if (!fx.ok) return;
    CHECK_OK(fx.r.setHeightField(flatGrid(4, 6, true), 1));
    ID3D11ShaderResourceView* cache = solid(fx.d.dev, kCache);
    ID3D11ShaderResourceView* vis = solid(fx.d.dev, kVisible);
    ID3D11ShaderResourceView* ones = solid(fx.d.dev, kAllOnes);
    ID3D11ShaderResourceView* cloud = solid(fx.d.dev, kCloud);
    CHECK(cache && vis && ones && cloud);
    if (!cache || !vis || !ones || !cloud) return;
    cacheEveryTile(fx.r, cache);
    const Camera cam = downCamera(120, 0, 160, 160, 120);
    Options o;
    o.water = false;
    terrainshot::ShotResult a, b, c, d;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, a));
    CHECK(!a.stats.fowOn);
    CHECK_EQ(a.stats.fowStyleTiles[0], 0u);
    o.fow.enabled = true;
    o.fow.mask = vis;
    o.fow.cloudBase = cloud;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, b));
    o.fow.mask = ones;
    CHECK_OK(fx.r.setTileStyle(1, 2, 3));
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, c));
    CHECK(b.stats.fowOn && c.stats.fowOn);
    CHECK(b.stats.fowStyleTiles[0] > 0);
    CHECK_EQ(b.stats.fowStyleTiles[0] + b.stats.fowStyleTiles[3], b.stats.tilesPass0 + b.stats.tilesPass1);
    o.fow.enabled = false;   // the inputs given but the fog off: the default again
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, d));
    size_t db = 0, dc = 0, dd = 0;
    for (size_t i = 0; i < a.rgba.size(); ++i) {
        db += i < b.rgba.size() && a.rgba[i] != b.rgba[i];
        dc += i < c.rgba.size() && a.rgba[i] != c.rgba[i];
        dd += i < d.rgba.size() && a.rgba[i] != d.rgba[i];
    }
    std::printf("  default vs fow on, visible mask: %zu bytes differ; (1,1,1,1) mask with a style-3 tile: %zu; fow off "
                "with inputs given: %zu (of %zu)\n", db, dc, dd, a.rgba.size());
    CHECK(a.rgba.size() == b.rgba.size() && a.rgba == b.rgba);
    CHECK(a.rgba == c.rgba);
    CHECK(a.rgba == d.rgba);
    CHECK(countNear(a.rgba, 192, 128, 48, 0) > a.rgba.size() / 8);   // the cache colour is on screen
    for (auto* v : {cache, vis, ones, cloud}) v->Release();
}

// Unexplored ((0, 1, 0, 1)): every terrain pixel is the cloud base, whatever the cache.
C5R_TEST(fow_unexplored_takes_the_cloud_base) {
    Fixture fx;
    if (!fx.ok) return;
    CHECK_OK(fx.r.setHeightField(flatGrid(4, 6, false), 1));
    ID3D11ShaderResourceView* cache = solid(fx.d.dev, kCache);
    ID3D11ShaderResourceView* unex = solid(fx.d.dev, kUnexplored);
    ID3D11ShaderResourceView* cloud = solid(fx.d.dev, kCloud);
    CHECK(cache && unex && cloud);
    if (!cache || !unex || !cloud) return;
    cacheEveryTile(fx.r, cache);
    const Camera cam = downCamera(0, 0, 120, 96, 64);
    Options o;
    o.water = false;
    o.fow.enabled = true;
    o.fow.mask = unex;
    o.fow.cloudBase = cloud;
    o.fow.anim[0] = o.fow.anim[1] = 0.002f;
    terrainshot::ShotResult res;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, res));
    const size_t n = res.rgba.size() / 4, cl = countNear(res.rgba, 40, 90, 160, 1);
    std::printf("  unexplored: %zu of %zu px = the cloud base (40, 90, 160) +-1; pixel 0 (%d, %d, %d)\n", cl, n, res.rgba[0],
                res.rgba[1], res.rgba[2]);
    CHECK_EQ(cl, n);
    for (auto* v : {cache, unex, cloud}) v->Release();
}

// Seen ((1, 0, 0, 1)): the colour goes through the TerrainFOW LUT of the tile's style, per tile; visible leaves it.
C5R_TEST(fow_seen_takes_the_tile_styles_lut) {
    Fixture fx;
    if (!fx.ok) return;
    CHECK_OK(fx.r.setHeightField(flatGrid(4, 6, false), 1));
    ID3D11ShaderResourceView* cache = solid(fx.d.dev, kCache);
    ID3D11ShaderResourceView* seen = solid(fx.d.dev, kSeen);
    ID3D11ShaderResourceView* vis = solid(fx.d.dev, kVisible);
    ID3D11ShaderResourceView* cloud = solid(fx.d.dev, kCloud);
    CHECK(cache && seen && vis && cloud);
    if (!cache || !seen || !vis || !cloud) return;
    cacheEveryTile(fx.r, cache);
    // style 2: every texel (200, 30, 60); style 4: every texel (10, 220, 90)
    const std::vector<uint8_t> t2 = makeTga(1024, 32, false, [](uint32_t, uint32_t) { return 0xFF3C1EC8u; });
    const std::vector<uint8_t> t4 = makeTga(32, 1024, true, [](uint32_t, uint32_t) { return 0xFF5ADC0Au; });
    CHECK_OK(fx.r.setFowLut(2, t2.data(), t2.size()));
    CHECK_OK(fx.r.setFowLut(4, t4.data(), t4.size()));
    CHECK_EQ(fx.r.fowLutsFromFile(), (1u << 2) | (1u << 4));
    const TileGrid tg = fx.r.tileGrid();
    for (uint32_t y = 0; y < tg.tilesY; ++y) {
        CHECK_OK(fx.r.setTileStyle(0, y, 2));
        CHECK_OK(fx.r.setTileStyle(1, y, 2));
        CHECK_OK(fx.r.setTileStyle(3, y, 4));
    }
    CHECK_EQ(fx.r.tileStyleAt(0, 1), 2u);
    CHECK_EQ(fx.r.tileStyleAt(2, 1), 0u);
    CHECK_EQ(fx.r.tileStyleAt(3, 1), 4u);
    const Camera cam = downCamera(0, 0, 700, 160, 120);   // the whole 4 x 6 field
    Options o;
    o.water = false;
    o.fow.enabled = true;
    o.fow.mask = seen;
    o.fow.cloudBase = cloud;
    terrainshot::ShotResult s, v;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, s));
    o.fow.mask = vis;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, v));
    const size_t k2 = countNear(s.rgba, 200, 30, 60, 1), k4 = countNear(s.rgba, 10, 220, 90, 1);
    // the identity volume (texel i = i * 255 / 31, linear filtering) returns the colour within a few levels
    const size_t keep = countNear(s.rgba, 192, 128, 48, 6), visKeep = countNear(v.rgba, 192, 128, 48, 0);
    std::printf("  seen: style-2 LUT colour %zu px, style-4 %zu px, identity (Generic) %zu px; visible: cache colour %zu px;"
                " tiles by style %u/%u/%u/%u/%u\n", k2, k4, keep, visKeep, s.stats.fowStyleTiles[0], s.stats.fowStyleTiles[1],
                s.stats.fowStyleTiles[2], s.stats.fowStyleTiles[3], s.stats.fowStyleTiles[4]);
    CHECK(k2 > 0 && k4 > 0 && keep > 0);
    CHECK(k2 > k4);   // two columns against one
    CHECK_EQ(countNear(v.rgba, 200, 30, 60, 1), size_t(0));
    CHECK_EQ(countNear(v.rgba, 10, 220, 90, 1), size_t(0));
    CHECK_EQ(s.stats.fowStyleTiles[2], 2 * tg.tilesY);
    CHECK_EQ(s.stats.fowStyleTiles[4], tg.tilesY);
    CHECK_EQ(s.stats.fowStyleTiles[0], tg.tilesY);
    // the art-type route: the field's rows - 4 are the DLL's map (4 x 2 plots); tile (x, y) lists plot rows y - 2, y - 1
    std::vector<uint8_t> art(4 * 2, 0);
    art[1 * 4 + 2] = 3;   // plot (2, 1): Africa
    CHECK_OK(fx.r.setTileStyles(art.data(), 4, 2, false));
    uint32_t africa = 0;
    for (uint32_t y = 0; y < tg.tilesY; ++y)
        for (uint32_t x = 0; x < tg.tilesX; ++x) {
            bake::TileId t;
            t.x = x;
            t.y = y;
            CHECK_EQ(fx.r.tileStyleAt(x, y), bake::tileStyle(art.data(), 4, 2, false, t));
            africa += fx.r.tileStyleAt(x, y) == 3;
        }
    CHECK(africa > 0);
    for (auto* x : {cache, seen, vis, cloud}) x->Release();
}

// FOWAnim: the DS's cloud uv is (world.xy + FOWAnim.zw) * FOWAnim.xy. With xy = 1/256 and a 256-texel ramp (R = texel)
// a pixel's red follows world x; z = -64 shifts it by 64 texels.
C5R_TEST(fow_anim_moves_the_cloud) {
    Fixture fx;
    if (!fx.ok) return;
    CHECK_OK(fx.r.setHeightField(flatGrid(4, 6, false), 1));
    std::vector<uint32_t> ramp(256);
    for (uint32_t i = 0; i < 256; ++i) ramp[i] = 0xFF000000u | i;
    ID3D11ShaderResourceView* cache = solid(fx.d.dev, kCache);
    ID3D11ShaderResourceView* unex = solid(fx.d.dev, kUnexplored);
    ID3D11ShaderResourceView* cloud = pixelTexture(fx.d.dev, 256, 1, ramp);
    CHECK(cache && unex && cloud);
    if (!cache || !unex || !cloud) return;
    cacheEveryTile(fx.r, cache);
    const Camera cam = downCamera(0, 0, 120, 160, 120);
    Options o;
    o.water = false;
    o.fow.enabled = true;
    o.fow.mask = unex;
    o.fow.cloudBase = cloud;
    terrainshot::ShotResult a, b, z;
    o.fow.anim[0] = o.fow.anim[1] = 1.0f / 256.0f;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, true, a));
    o.fow.anim[2] = -64.0f;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, b));
    o.fow.anim[0] = o.fow.anim[1] = o.fow.anim[2] = 0.0f;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, z));
    // per pixel: the expected texel from the world x read back; linear filtering between neighbours, wrap at 256
    size_t n = 0, okA = 0, okB = 0, shifted = 0;
    for (uint32_t y = 4; y < a.height; y += 7)
        for (uint32_t x = 4; x < a.width; x += 7) {
            const size_t i = static_cast<size_t>(y) * a.width + x;
            if (a.world[i * 4 + 3] != 1.0f) continue;
            const double wx = a.world[i * 4 + 0];
            auto closeTo = [](double want, int got) {
                double d = std::fmod(std::fabs(want - got), 256.0);
                return std::min(d, 256.0 - d) <= 1.5;
            };
            const double ua = std::fmod(std::fmod(wx - 0.5, 256.0) + 256.0, 256.0);         // texel centre at i + 0.5
            const double ub = std::fmod(std::fmod(wx - 64.0 - 0.5, 256.0) + 256.0, 256.0);
            // within a texel of the wrap seam the filter blends texel 255 with texel 0: skipped
            if (ua > 254.0 || ub > 254.0) continue;
            ++n;
            okA += closeTo(ua, a.rgba[i * 4]);
            okB += closeTo(ub, b.rgba[i * 4]);
            shifted += a.rgba[i * 4] != b.rgba[i * 4];
        }
    std::printf("  FOWAnim: %zu sampled terrain px; red = texel of world x: %zu; of world x - 64 with z = -64: %zu; "
                "changed by z: %zu; anim 0 gives texel 0 everywhere: pixel 0 red %d\n", n, okA, okB, shifted, z.rgba[0]);
    CHECK(n > 100);
    CHECK_EQ(okA, n);
    CHECK_EQ(okB, n);
    CHECK(shifted > n / 2);
    CHECK_EQ(countNear(z.rgba, z.rgba[0], z.rgba[1], z.rgba[2], 0), z.rgba.size() / 4);   // uv 0: one colour
    for (auto* v : {cache, unex, cloud}) v->Release();
}

// The mask is sampled at the overlay's coordinate: with the fog on and no overlay texture, Options::overlay.proj still
// places the mask (here a mask of two halves, NDC x < 0 unexplored, x >= 0 visible, with the matrix NDC.x = world.x / 64).
C5R_TEST(fow_mask_uses_the_overlay_matrix) {
    Fixture fx;
    if (!fx.ok) return;
    CHECK_OK(fx.r.setHeightField(flatGrid(4, 6, false), 1));
    ID3D11ShaderResourceView* cache = solid(fx.d.dev, kCache);
    ID3D11ShaderResourceView* cloud = solid(fx.d.dev, kCloud);
    std::vector<uint32_t> halves(64 * 4);
    for (uint32_t y = 0; y < 4; ++y)
        for (uint32_t x = 0; x < 64; ++x) halves[y * 64 + x] = x < 32 ? kUnexplored : kVisible;
    ID3D11ShaderResourceView* mask = pixelTexture(fx.d.dev, 64, 4, halves);
    CHECK(cache && cloud && mask);
    if (!cache || !cloud || !mask) return;
    cacheEveryTile(fx.r, cache);
    const Camera cam = downCamera(0, 0, 120, 160, 120);
    Options o;
    o.water = false;
    o.fow.enabled = true;
    o.fow.mask = mask;
    o.fow.cloudBase = cloud;
    const float m[16] = {1.0f / 64, 0, 0, 0, 0, 1.0f / 64, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(o.overlay.proj[1], m, sizeof m);
    terrainshot::ShotResult res;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, true, res));
    CHECK(!res.stats.overlayBound);
    size_t left = 0, leftCloud = 0, right = 0, rightCache = 0;
    for (size_t i = 0; i < res.rgba.size() / 4; ++i) {
        if (res.world[i * 4 + 3] != 1.0f) continue;
        const float wx = res.world[i * 4];
        const uint8_t* p = &res.rgba[i * 4];
        if (wx < -4) {
            ++left;
            leftCloud += std::abs(p[0] - 40) <= 1 && std::abs(p[1] - 90) <= 1 && std::abs(p[2] - 160) <= 1;
        } else if (wx > 4 && wx < 60) {
            ++right;
            rightCache += p[0] == 192 && p[1] == 128 && p[2] == 48;
        }
    }
    std::printf("  mask by OverlayProj: world x < -4: %zu of %zu px cloud; 4 < x < 60: %zu of %zu px the cache\n", leftCloud,
                left, rightCache, right);
    CHECK(left > 100 && right > 100);
    CHECK_EQ(leftCloud, left);
    CHECK_EQ(rightCache, right);
    for (auto* v : {cache, cloud, mask}) v->Release();
}

// The module binds exactly buildColorKeyLut's bytes; null bytes restore the identity volume.
C5R_TEST(fow_lut_volume_is_the_tga_rule) {
    Fixture fx;
    if (!fx.ok) return;
    const std::vector<uint8_t> tga = makeTga(1024, 32, false, [](uint32_t x, uint32_t y) {
        return 0x80000000u | ((x * 7 + y) & 255u) | (((x >> 3) & 255u) << 8) | (((y * 5) & 255u) << 16);
    });
    std::vector<uint8_t> want;
    CHECK_OK(bake::buildColorKeyLut(tga.data(), tga.size(), want));
    ID3D11ShaderResourceView* idBefore = fx.r.fowLut(1);
    const std::vector<uint8_t> ident = readVolume(fx.d.dev, fx.d.ctx, idBefore);
    CHECK_EQ(ident.size(), size_t(32 * 32 * 32 * 4));
    CHECK_OK(fx.r.setFowLut(1, tga.data(), tga.size()));
    CHECK(fx.r.fowLut(1) != idBefore);
    const std::vector<uint8_t> got = readVolume(fx.d.dev, fx.d.ctx, fx.r.fowLut(1));
    CHECK(got == want);
    CHECK(fx.r.fowLut(0) == idBefore);   // the others keep the identity
    CHECK_OK(fx.r.setFowLut(1, nullptr, 0));
    CHECK(fx.r.fowLut(1) == idBefore);
    CHECK_EQ(fx.r.fowLutsFromFile(), 0u);
    // the identity volume: texel (x, y, z) = (x, y, z) * 255 / 31 (integer), alpha 255
    CHECK(ident[(31 * 32 * 32 + 0 * 32 + 1) * 4 + 0] == 8 && ident[(31 * 32 * 32) * 4 + 2] == 255 && ident[3] == 255);
}

// Negative tests: each check's error, and the draw's stats and the module's state unchanged after it.
C5R_TEST(negative_fow_checks) {
    Fixture fx;
    if (!fx.ok) return;
    Renderer& r = fx.r;
    CHECK_OK(r.setHeightField(flatGrid(4, 6, false), 1));
    ID3D11ShaderResourceView* cache = solid(fx.d.dev, kCache);
    ID3D11ShaderResourceView* mask = solid(fx.d.dev, kSeen);
    ID3D11ShaderResourceView* cloud = solid(fx.d.dev, kCloud);
    ID3D11ShaderResourceView* flt = pixelTexture(fx.d.dev, 2, 2, std::vector<uint32_t>(4, 0), DXGI_FORMAT_R32_FLOAT);
    ID3D11ShaderResourceView* bgra = pixelTexture(fx.d.dev, 2, 2, std::vector<uint32_t>(4, 0), DXGI_FORMAT_B8G8R8A8_UNORM);
    CHECK(cache && mask && cloud && flt && bgra);
    if (!cache || !mask || !cloud || !flt || !bgra) return;
    cacheEveryTile(r, cache);
    const Camera cam = downCamera(0, 0, 120, 64, 48);
    terrainshot::ShotResult ref;
    Options o;
    o.water = false;
    CHECK_OK(terrainshot::renderShot(fx.d, r, cam, o, false, ref));
    FrameStats st;
    st.tilesDrawn = 12345;
    auto failsWith = [&](const Options& x, const char* what) {
        FrameStats s2 = st;
        terrainshot::ShotResult res;
        Status s = terrainshot::renderShot(fx.d, r, cam, x, false, res);
        if (s.code != Err::BadLayout) test::fail(__FILE__, __LINE__, std::string(what) + ": want BadLayout, got " + s.str());
        Status s3 = r.draw(fx.d.ctx, nullptr, nullptr, cam, x, &s2);   // no target: fails before the fow checks too
        CHECK(!s3.ok());
        CHECK_EQ(s2.tilesDrawn, 12345u);
        std::printf("  %s: %s\n", what, s.str().c_str());
    };
    Options f = o;
    f.fow.enabled = true;
    f.fow.cloudBase = cloud;
    failsWith(f, "fow on, mask null");
    f.fow.mask = mask;
    f.fow.cloudBase = nullptr;
    failsWith(f, "fow on, cloud base null");
    f.fow.cloudBase = cloud;
    f.fow.anim[2] = std::nanf("");
    failsWith(f, "FOWAnim NaN");
    f.fow.anim[2] = INFINITY;
    failsWith(f, "FOWAnim infinite");
    f.fow.anim[2] = 0;
    f.fow.mask = flt;
    failsWith(f, "mask R32_FLOAT");
    f.fow.mask = bgra;
    failsWith(f, "mask B8G8R8A8");
    f.fow.mask = mask;
    f.fow.cloudBase = r.fowLut(0);   // a 3D view
    failsWith(f, "cloud base a Texture3D view");
    // the module still draws as before after the failures
    terrainshot::ShotResult after;
    CHECK_OK(terrainshot::renderShot(fx.d, r, cam, o, false, after));
    CHECK(after.rgba == ref.rgba);
    // LUT files: bad TGAs leave the volume as it was
    const std::vector<uint8_t> good = makeTga(1024, 32, false, [](uint32_t, uint32_t) { return 0xFF102030u; });
    CHECK_OK(r.setFowLut(3, good.data(), good.size()));
    ID3D11ShaderResourceView* before = r.fowLut(3);
    const std::vector<uint8_t> volBefore = readVolume(fx.d.dev, fx.d.ctx, before);
    std::vector<uint8_t> trunc(good.begin(), good.begin() + 1000);
    CHECK_ERR(r.setFowLut(3, trunc.data(), trunc.size()), Err::Truncated);
    CHECK_ERR(r.setFowLut(3, good.data(), 10), Err::Truncated);   // no header
    const std::vector<uint8_t> rle = makeTga(1024, 32, false, [](uint32_t, uint32_t) { return 0u; }, 10);
    CHECK_ERR(r.setFowLut(3, rle.data(), rle.size()), Err::Unsupported);
    const std::vector<uint8_t> b24 = makeTga(1024, 32, false, [](uint32_t, uint32_t) { return 0u; }, 2, 24);
    CHECK_ERR(r.setFowLut(3, b24.data(), b24.size()), Err::Unsupported);
    const std::vector<uint8_t> tiny = makeTga(32, 32, false, [](uint32_t, uint32_t) { return 0u; });
    CHECK_ERR(r.setFowLut(3, tiny.data(), tiny.size()), Err::BadLayout);
    CHECK_ERR(r.setFowLut(5, good.data(), good.size()), Err::BadLayout);
    CHECK(r.fowLut(3) == before);
    CHECK(readVolume(fx.d.dev, fx.d.ctx, r.fowLut(3)) == volBefore);
    CHECK_EQ(r.fowLutsFromFile(), 1u << 3);
    CHECK(r.fowLut(5) == nullptr);
    // tile styles
    const TileGrid tg = r.tileGrid();
    CHECK_OK(r.setTileStyle(1, 1, 4));
    CHECK_ERR(r.setTileStyle(tg.tilesX, 0, 1), Err::BadLayout);
    CHECK_ERR(r.setTileStyle(0, tg.tilesY, 1), Err::BadLayout);
    CHECK_ERR(r.setTileStyle(1, 1, 5), Err::BadLayout);
    CHECK_EQ(r.tileStyleAt(1, 1), 4u);
    CHECK_EQ(r.tileStyleAt(tg.tilesX, 0), 0u);
    std::vector<uint8_t> art(4 * 2, 1);
    CHECK_ERR(r.setTileStyles(nullptr, 4, 2, false), Err::BadLayout);
    CHECK_ERR(r.setTileStyles(art.data(), 4, 6, false), Err::BadLayout);   // the field's rows, not the DLL's
    CHECK_ERR(r.setTileStyles(art.data(), 3, 2, false), Err::BadLayout);
    CHECK_ERR(r.setTileStyles(art.data(), 0, 2, false), Err::BadLayout);
    CHECK_EQ(r.tileStyleAt(1, 1), 4u);   // unchanged after the failures
    CHECK_EQ(r.tileStyleAt(0, 0), 0u);
    // a new field resets every style to Generic
    CHECK_OK(r.setHeightField(flatGrid(4, 6, false), 1));
    CHECK_EQ(r.tileStyleAt(1, 1), 0u);
    for (auto* v : {cache, mask, cloud, flt, bgra}) v->Release();
}

// The install's five TerrainFOW volumes (C5R_GAME_DIR). With C5R_FOW_CAPTURE_LUT (a captured t6, 32^3 RGBA8 bytes) the
// volume of style C5R_FOW_CAPTURE_STYLE (default 0, Generic) must equal it bit for bit; every style's equality is printed.
C5R_TEST(game_fow_luts_from_the_install) {
    Fixture fx(true);
    if (!fx.ok) return;
    const uint32_t files = fx.r.fowLutsFromFile();
    std::vector<uint8_t> cap;
    const char* capPath = std::getenv("C5R_FOW_CAPTURE_LUT");
    if (capPath && *capPath) {
        std::ifstream f(capPath, std::ios::binary);
        cap.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        CHECK_EQ(cap.size(), size_t(32 * 32 * 32 * 4));
    }
    // C5R_FOW_LUT_DUMP: a folder (game data, D83: under C:/c5-64bit-work/, never committed) that receives each style's
    // volume as terrainfow_<style>.bin, for the replay of render/evidence/0168/3953/replay_3953.py
    const char* dump = std::getenv("C5R_FOW_LUT_DUMP");
    std::vector<uint8_t> vols[5];
    for (uint32_t s = 0; s < 5; ++s) {
        vols[s] = readVolume(fx.d.dev, fx.d.ctx, fx.r.fowLut(s));
        CHECK_EQ(vols[s].size(), size_t(32 * 32 * 32 * 4));
        if (dump && *dump) {
            std::ofstream o(std::string(dump) + "/terrainfow_" + std::to_string(s) + ".bin", std::ios::binary);
            o.write(reinterpret_cast<const char*>(vols[s].data()), static_cast<std::streamsize>(vols[s].size()));
            CHECK(o.good());
        }
        std::printf("  style %u %-8s from file %d, fnv64 %016llx%s\n", s, bake::styleName(s), (files >> s) & 1,
                    static_cast<unsigned long long>(fnv64(vols[s])),
                    cap.empty() ? "" : (vols[s] == cap ? ", EQUAL to the capture's t6" : ", differs from the capture's t6"));
    }
    CHECK(files & 1u);   // the Generic file exists in every install (water.cpp binds it as t11)
    if (!cap.empty()) {
        const char* cs = std::getenv("C5R_FOW_CAPTURE_STYLE");
        const uint32_t want = cs && *cs ? static_cast<uint32_t>(std::atoi(cs)) : 0u;
        CHECK(want < 5);
        if (want < 5) CHECK(vols[want] == cap);
    }
}

// The epic's own modules end to end (C5R_EXE, C5R_GAME_DIR): c5r::fow::MaskRenderer's mask (3952) and
// CloudRenderer::cloudBase() (3955) bound through Options::fow, with fowAnim. An 8 x 8-plot map (field 8 x 12 with the
// margin rows), every plot in one state, the window = the whole field; the middle of the picture lies inside the map's
// hexagons, so there the mask is exactly the state's colour: visible = the default picture byte for byte, unexplored =
// the cloud base (not the cache), seen = the TerrainFOW LUT of the tiles' style (different from visible).
C5R_TEST(game_fow_with_the_epics_mask_and_cloud_base) {
    Fixture fx(true);
    if (!fx.ok) return;
    const std::string exe = test::exePath(), game = test::gameDir();
    const uint32_t W = 8, H = 8;
    CHECK_OK(fx.r.setHeightField(flatGrid(W, H + 4, false), 1));
    ID3D11ShaderResourceView* cache = solid(fx.d.dev, kCache);
    CHECK(cache != nullptr);
    if (!cache) return;
    cacheEveryTile(fx.r, cache);
    fow::MaskRenderer mask;
    fow::CloudRenderer cloud;
    CHECK_OK(mask.init(fx.d.dev, wide(exe), 512));
    CHECK_OK(cloud.init(fx.d.dev, wide(exe), wide(game)));
    CHECK(mask.srv() && cloud.cloudBase());
    if (!mask.srv() || !cloud.cloudBase()) return;
    const float halfW = W * 32.0f, halfH = (H + 4) * 32.0f * 0.866f;
    fow::View v;
    const float op[16] = {1.0f / halfW, 0, 0, 0, 0, 1.0f / halfH, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(v.overlayProj, op, sizeof op);   // the field's world box -> the whole mask (plot range from the window)
    std::vector<fow::PlotState> states(W * H);
    fow::MaskInput in;
    in.map.states = states.data();
    in.map.stateCount = states.size();
    in.map.width = W;
    in.map.height = H;
    in.map.wrapX = false;
    in.map.worldWidth = W * 64.0f - 1.0f;
    fow::nominalPlacement(in.map);
    in.views = &v;
    in.numViews = 1;
    const Camera cam = downCamera(0, 0, 120, 160, 120);
    Options o;
    o.water = false;
    std::memcpy(o.overlay.proj[1], op, sizeof op);
    terrainshot::ShotResult def, vis, unex, seen;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, def));
    o.fow.enabled = true;
    o.fow.mask = mask.srv();
    o.fow.cloudBase = cloud.cloudBase();
    fow::fowAnim(in.map.worldWidth, 12.5, o.fow.anim);
    auto shotWith = [&](fow::PlotState s, terrainshot::ShotResult& r) {
        for (auto& x : states) x = s;
        fow::MaskStats ms;
        CHECK_OK(mask.build(fx.d.ctx, in, &ms));
        CHECK(ms.hexagons[0] + ms.hexagons[1] + ms.hexagons[2] >= W * H);
        CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, r));
        CHECK(r.stats.fowOn);
    };
    shotWith(fow::PlotState::Visible, vis);
    shotWith(fow::PlotState::Unexplored, unex);
    shotWith(fow::PlotState::Seen, seen);
    // the middle half of the picture
    size_t n = 0, visEq = 0, unexCache = 0, unexDiff = 0, seenDiff = 0;
    for (uint32_t y = def.height / 4; y < def.height * 3 / 4; ++y)
        for (uint32_t x = def.width / 4; x < def.width * 3 / 4; ++x) {
            const size_t i = (static_cast<size_t>(y) * def.width + x) * 4;
            ++n;
            visEq += std::memcmp(&def.rgba[i], &vis.rgba[i], 4) == 0;
            unexCache += unex.rgba[i] == 192 && unex.rgba[i + 1] == 128 && unex.rgba[i + 2] == 48;
            unexDiff += std::memcmp(&def.rgba[i], &unex.rgba[i], 3) != 0;
            seenDiff += std::memcmp(&vis.rgba[i], &seen.rgba[i], 3) != 0;
        }
    std::printf("  the epic's mask and cloud base, middle %zu px: visible = default %zu; unexplored differs %zu (cache "
                "colour left %zu); seen differs from visible %zu\n", n, visEq, unexDiff, unexCache, seenDiff);
    CHECK_EQ(visEq, n);
    CHECK_EQ(unexCache, size_t(0));
    CHECK_EQ(unexDiff, n);
    CHECK_EQ(seenDiff, n);
    cache->Release();
}

// ================================================================ issue 4305 (epic 0169): t7 per tile style
// The TerrainShadow ColorKey volume of the tile's style (0xD90E74 + 4 * style), as t6 above: init reads the five
// `TerrainShadow_<Style>_ColorKey.tga` (missing = identity, unparsable = init error naming it), setShadowLut replaces
// one, and the draw binds the tile style's volume as t7 while a shadow is bound (research/05-graphics-composition/
// terrain-surface.md 12.6).

namespace {

ID3D11ShaderResourceView* r16Solid(ID3D11Device* dev, uint16_t v) {
    std::vector<uint16_t> px(16, v);
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = d.Height = 4;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R16_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA s = {px.data(), 4 * 2, 0};
    ID3D11Texture2D* t = nullptr;
    ID3D11ShaderResourceView* sv = nullptr;
    if (FAILED(dev->CreateTexture2D(&d, &s, &t))) return nullptr;
    dev->CreateShaderResourceView(t, nullptr, &sv);
    t->Release();
    return sv;
}

// A ColorKey TGA whose every texel is one colour (RGBA as 0xAABBGGRR).
std::vector<uint8_t> solidLutTga(uint32_t rgba) {
    return makeTga(1024, 32, false, [rgba](uint32_t, uint32_t) { return rgba; });
}

// A ColorKey TGA with a pattern that depends on the style (every style's volume different).
std::vector<uint8_t> patternLutTga(uint32_t style) {
    return makeTga(1024, 32, false, [style](uint32_t x, uint32_t y) {
        return 0xFF000000u | ((x * (style + 3) + y) & 255u) | (((x >> 2) * (style + 1) & 255u) << 8) |
               (((y * 7 + style * 40) & 255u) << 16);
    });
}

bool writeBytes(const std::string& path, const std::vector<uint8_t>& b) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return f.good();
}

} // namespace

C5R_TEST(shadow_lut_calls_before_init) {
    Renderer r;
    const std::vector<uint8_t> tga = solidLutTga(0xFF102030u);
    CHECK_ERR(r.setShadowLut(0, tga.data(), tga.size()), Err::NotFound);
    for (uint32_t s = 0; s < 6; ++s) CHECK(r.shadowLut(s) == nullptr);
    CHECK_EQ(r.shadowLutsFromFile(), 0u);
    CHECK(!r.shadowLutFromFile());
    FrameStats st;
    for (uint32_t c : st.shadowStyleTiles) CHECK_EQ(c, 0u);
}

// Every style's volume is buildColorKeyLut's bytes of its own file; the others keep theirs; null restores identity;
// negatives (style 5, a file that does not parse) leave every volume unchanged.
C5R_TEST(shadow_lut_per_style_is_the_tga_rule) {
    Fixture fx;
    if (!fx.ok) return;
    Renderer& r = fx.r;
    ID3D11ShaderResourceView* ident = r.shadowLut(0);
    CHECK(ident != nullptr);
    for (uint32_t s = 1; s < 5; ++s) CHECK(r.shadowLut(s) == ident);   // no install: identity for every style
    CHECK_EQ(r.shadowLutsFromFile(), 0u);
    std::vector<uint8_t> want[5];
    for (uint32_t s = 0; s < 5; ++s) {
        const std::vector<uint8_t> tga = patternLutTga(s);
        CHECK_OK(bake::buildColorKeyLut(tga.data(), tga.size(), want[s]));
        CHECK_OK(r.setShadowLut(s, tga.data(), tga.size()));
    }
    CHECK_EQ(r.shadowLutsFromFile(), 0x1Fu);
    CHECK(r.shadowLutFromFile());
    std::vector<uint8_t> got[5];
    for (uint32_t s = 0; s < 5; ++s) {
        got[s] = readVolume(fx.d.dev, fx.d.ctx, r.shadowLut(s));
        CHECK(got[s] == want[s]);
        for (uint32_t t = 0; t < s; ++t) CHECK(got[s] != got[t]);   // five different volumes
    }
    CHECK(r.fowLut(2) == r.fowLut(0));   // t6's volumes are untouched (identity)
    // negatives: style 5, a file that does not parse (fewer than 32^3 pixels; RLE): the volumes stay
    CHECK_ERR(r.setShadowLut(5, want[0].data(), want[0].size()), Err::BadLayout);
    const std::vector<uint8_t> fewPx = makeTga(16, 16, false, [](uint32_t, uint32_t) { return 0xFFFFFFFFu; });
    const Status bad = r.setShadowLut(3, fewPx.data(), fewPx.size());
    std::printf("  a TerrainShadow LUT that does not parse (16 x 16): %s\n", bad.str().c_str());
    CHECK(!bad.ok());
    CHECK(bad.msg.find("TerrainShadow_Africa_ColorKey.tga") != std::string::npos);
    const std::vector<uint8_t> rle = makeTga(1024, 32, false, [](uint32_t, uint32_t) { return 0xFFFFFFFFu; }, 10);
    CHECK(!r.setShadowLut(3, rle.data(), rle.size()).ok());
    CHECK(readVolume(fx.d.dev, fx.d.ctx, r.shadowLut(3)) == want[3]);
    CHECK_EQ(r.shadowLutsFromFile(), 0x1Fu);
    // null restores the identity volume for that style only
    CHECK_OK(r.setShadowLut(4, nullptr, 0));
    CHECK(r.shadowLut(4) == ident);
    CHECK_EQ(r.shadowLutsFromFile(), 0x0Fu);
    CHECK(r.shadowLut(5) == nullptr);
}

// The draw binds the tile style's volume as t7 while a shadow is bound: with a full shadow (a map of ones, strength 1)
// a tile of style 2 shows style 2's LUT colour, style 4's its own, a Generic tile (identity) about the cache colour; the
// counts per style are the tiles drawn. Without a shadow no style colour appears (t7 is not read: Arg6 0), and the fog of
// war's t6 is not changed by the t7 volumes (fow off: no LUT colour). A crafted different file per style is bound to its
// style's tiles only.
C5R_TEST(shadow_t7_is_bound_by_the_tile_style) {
    Fixture fx;
    if (!fx.ok) return;
    CHECK_OK(fx.r.setHeightField(flatGrid(4, 6, false), 1));
    ID3D11ShaderResourceView* cache = solid(fx.d.dev, kCache);
    ID3D11ShaderResourceView* ones = r16Solid(fx.d.dev, 0xFFFF);
    CHECK(cache && ones);
    if (!cache || !ones) return;
    cacheEveryTile(fx.r, cache);
    const std::vector<uint8_t> t2 = solidLutTga(0xFF3C1EC8u);   // (200, 30, 60)
    const std::vector<uint8_t> t4 = solidLutTga(0xFF5ADC0Au);   // (10, 220, 90)
    CHECK_OK(fx.r.setShadowLut(2, t2.data(), t2.size()));
    CHECK_OK(fx.r.setShadowLut(4, t4.data(), t4.size()));
    const TileGrid tg = fx.r.tileGrid();
    for (uint32_t y = 0; y < tg.tilesY; ++y) {
        CHECK_OK(fx.r.setTileStyle(0, y, 2));
        CHECK_OK(fx.r.setTileStyle(1, y, 2));
        CHECK_OK(fx.r.setTileStyle(3, y, 4));
    }
    const Camera cam = downCamera(0, 0, 700, 160, 120);   // the whole 4 x 6 field
    Options o;
    o.water = false;
    o.fog.enabled = false;
    o.spec = false;
    terrainshot::ShotResult off, on;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, off));
    o.shadow.srv = ones;
    o.shadow.strength = 1.0f;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, on));
    const size_t k2 = countNear(on.rgba, 200, 30, 60, 1), k4 = countNear(on.rgba, 10, 220, 90, 1);
    const size_t keep = countNear(on.rgba, 192, 128, 48, 6);
    std::printf("  full shadow: style-2 LUT colour %zu px, style-4 %zu px, Generic (identity) about the cache %zu px; "
                "tiles by t7 style %u/%u/%u/%u/%u; no shadow: style colours %zu / %zu px, t7 tiles %u\n", k2, k4, keep,
                on.stats.shadowStyleTiles[0], on.stats.shadowStyleTiles[1], on.stats.shadowStyleTiles[2],
                on.stats.shadowStyleTiles[3], on.stats.shadowStyleTiles[4], countNear(off.rgba, 200, 30, 60, 1),
                countNear(off.rgba, 10, 220, 90, 1), off.stats.shadowStyleTiles[0]);
    CHECK(on.stats.shadowOn && !on.stats.shadowLutFromFile);   // Generic is the identity here
    CHECK(k2 > 0 && k4 > 0 && keep > 0);
    CHECK(k2 > k4);   // two columns against one
    CHECK_EQ(on.stats.shadowStyleTiles[2], 2 * tg.tilesY);
    CHECK_EQ(on.stats.shadowStyleTiles[4], tg.tilesY);
    CHECK_EQ(on.stats.shadowStyleTiles[0], tg.tilesY);
    CHECK_EQ(on.stats.shadowStyleTiles[1] + on.stats.shadowStyleTiles[3], 0u);
    CHECK_EQ(on.stats.shadowStyleTiles[0] + on.stats.shadowStyleTiles[2] + on.stats.shadowStyleTiles[4],
             on.stats.tilesPass0 + on.stats.tilesPass1);
    CHECK(!off.stats.shadowOn);
    CHECK_EQ(countNear(off.rgba, 200, 30, 60, 1), size_t(0));
    CHECK_EQ(countNear(off.rgba, 10, 220, 90, 1), size_t(0));
    for (uint32_t c : off.stats.shadowStyleTiles) CHECK_EQ(c, 0u);
    // every tile Generic again: no style colour with the shadow on (the style decides the volume, not the column)
    for (uint32_t y = 0; y < tg.tilesY; ++y)
        for (uint32_t x = 0; x < tg.tilesX; ++x) CHECK_OK(fx.r.setTileStyle(x, y, 0));
    terrainshot::ShotResult gen;
    CHECK_OK(terrainshot::renderShot(fx.d, fx.r, cam, o, false, gen));
    CHECK_EQ(countNear(gen.rgba, 200, 30, 60, 1) + countNear(gen.rgba, 10, 220, 90, 1), size_t(0));
    CHECK_EQ(gen.stats.shadowStyleTiles[0], on.stats.tilesPass0 + on.stats.tilesPass1);
    cache->Release();
    ones->Release();
}

// The install's five TerrainShadow volumes (C5R_GAME_DIR): each equals buildColorKeyLut of the file the game's lookup
// gives (gamefs), or the identity volume when the install lacks it. With C5R_SHADOW_CAPTURE_LUT (a captured t7, 32^3
// RGBA8, e.g. 3601's 2449 / 2455) every style's equality to it is printed and the Generic volume must equal it.
C5R_TEST(game_shadow_luts_from_the_install) {
    Fixture fx(true);
    if (!fx.ok) return;
    gamefs::GameFiles fs;
    CHECK_OK(fs.open(wide(test::gameDir())));
    std::vector<uint8_t> cap;
    const char* capPath = std::getenv("C5R_SHADOW_CAPTURE_LUT");
    if (capPath && *capPath) {
        std::ifstream f(capPath, std::ios::binary);
        cap.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        CHECK_EQ(cap.size(), size_t(32 * 32 * 32 * 4));
    }
    uint32_t files = 0;
    for (uint32_t s = 0; s < 5; ++s) {
        const std::string name = "TerrainShadow_" + std::string(bake::styleName(s)) + "_ColorKey.tga";
        const std::vector<uint8_t> got = readVolume(fx.d.dev, fx.d.ctx, fx.r.shadowLut(s));
        CHECK_EQ(got.size(), size_t(32 * 32 * 32 * 4));
        std::vector<uint8_t> tga, want;
        const bool have = fs.exists(name) && fs.read(name, tga).ok();
        if (have) {
            CHECK_OK(bake::buildColorKeyLut(tga.data(), tga.size(), want));
            CHECK(got == want);
            files |= 1u << s;
        } else {
            CHECK(((fx.r.shadowLutsFromFile() >> s) & 1u) == 0u);   // the identity volume
        }
        std::printf("  t7 style %u %-8s file %d, fnv64 %016llx%s\n", s, bake::styleName(s), have ? 1 : 0,
                    static_cast<unsigned long long>(fnv64(got)),
                    cap.empty() ? "" : (got == cap ? ", EQUAL to the capture's t7" : ", differs from the capture's t7"));
        if (!cap.empty() && s == 0) CHECK(got == cap);
    }
    CHECK_EQ(fx.r.shadowLutsFromFile(), files);
    CHECK(files & 1u);   // every install has the Generic file (0190)
}

// Negative with a crafted install (C5R_GAME_DIR for the ramp and spec map only): a style file that does not parse fails
// init naming it, nothing half-made; a crafted Asia file alone is Asia's volume and every other style the identity.
C5R_TEST(negative_style_shadow_file_fails_init) {
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
    const std::string root = std::string(tmp) + "c5r_terrain_shadow_styles_4305";
    const std::string asia = root + "\\Assets\\TerrainShadow_Asia_ColorKey.tga";
    CreateDirectoryA(root.c_str(), nullptr);
    CreateDirectoryA((root + "\\Assets").c_str(), nullptr);
    CreateDirectoryA((root + "\\Resource").c_str(), nullptr);
    CHECK(writeBytes(root + "\\Assets\\fogramp.dds", rampDds));
    CHECK(writeBytes(root + "\\Assets\\specscreenmap.dds", specDds));
    const std::vector<uint8_t> good = patternLutTga(2);
    CHECK(writeBytes(asia, good));
    {
        Renderer r;
        CHECK_OK(r.init(d.dev, wide(exe), wide(root)));
        std::vector<uint8_t> want;
        CHECK_OK(bake::buildColorKeyLut(good.data(), good.size(), want));
        CHECK_EQ(r.shadowLutsFromFile(), 1u << 2);
        CHECK(readVolume(d.dev, d.ctx, r.shadowLut(2)) == want);
        for (uint32_t s : {0u, 1u, 3u, 4u}) CHECK(r.shadowLut(s) == r.shadowLut(0) && r.shadowLut(0) != r.shadowLut(2));
        std::printf("  a crafted Asia TerrainShadow file alone: bits 0x%x, Asia's volume = its bytes\n", r.shadowLutsFromFile());
    }
    CHECK(writeBytes(asia, makeTga(16, 16, false, [](uint32_t, uint32_t) { return 0xFFFFFFFFu; })));
    {
        Renderer r;
        const Status st = r.init(d.dev, wide(exe), wide(root));
        std::printf("  an Asia TerrainShadow file that does not parse: %s\n", st.str().c_str());
        CHECK(!st.ok());
        CHECK(st.msg.find("TerrainShadow_Asia_ColorKey.tga") != std::string::npos);
        CHECK(r.shadowLut(2) == nullptr && r.shadowLutsFromFile() == 0u && r.fogRamp() == nullptr);
    }
    DeleteFileA(asia.c_str());
    DeleteFileA((root + "\\Assets\\specscreenmap.dds").c_str());
    DeleteFileA((root + "\\Assets\\fogramp.dds").c_str());
    RemoveDirectoryA((root + "\\Assets").c_str());
    RemoveDirectoryA((root + "\\Resource").c_str());
    RemoveDirectoryA(root.c_str());
}

C5R_TEST_MAIN
