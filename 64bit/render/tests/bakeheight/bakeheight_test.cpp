// render/tests/bakeheight/bakeheight_test.cpp - c5r::bake height pages and bake-time constants (issue 3406).
//
// Synthetic tests need nothing. The game tests read C5R_HEIGHT_REF (default D:/c5-64bit-work/3400/w6/ref; game data,
// never committed, SHA-256 in its manifest.txt):
//   tiny_tap.c5hf       height tap of the Tiny save (3402, C5H-3402-TINY of c5h-9028-tiny-t0008)
//   tiny_page_0_2.c5rt  the height page bound for tile P in 3023's capture frame380 (3403's ref380/height.c5rt)
//   tiny_P_frame.bin    TerrainFrame of tile P's bake (3403's ref380/P_frame.bin)
//   huge_tap.c5hf       height tap of the Huge save (3401 armA, C5H-3401-HUGE_0350)
//   slope_tap.c5hf      height tap of 3402's edited Tiny copy C5H-3402-SLOPE (features placed by Lua change heights)
//   pages.txt + *_cap_*.c5rt   every distinct height page in the RenderDoc exports of 3023 and 3402 (pages_from_capture.py)
//   tiles_tiny.txt, tiles_huge.txt   every tile's fields read by PID from the EXE (3402's dump; issue 3406's probe)
// A missing input fails the test with its path. research/05-graphics-composition/terrain-bake-passes.md 5.0 and 5.3.
#include <windows.h>
#include <d3d11.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "c5r/bake/heightpage.h"
#include "c5r/heightfield/heightfield.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::bake;

namespace {

std::string refDir() {
    const char* e = std::getenv("C5R_HEIGHT_REF");
    return e && *e ? e : "D:/c5-64bit-work/3400/w6/ref";
}

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(static_cast<size_t>(n));
    size_t got = n ? std::fread(out.data(), 1, out.size(), f) : 0;
    std::fclose(f);
    return got == out.size();
}

bool need(const std::string& name, std::vector<uint8_t>& out) {
    const std::string path = refDir() + "/" + name;
    if (!readFile(path, out)) {
        test::fail(__FILE__, __LINE__, "missing expected data " + path + " (set C5R_HEIGHT_REF)");
        return false;
    }
    return true;
}

bool loadTap(const std::string& name, heightfield::HeightGrid& g) {
    std::vector<uint8_t> b;
    if (!need(name, b)) return false;
    Status s = heightfield::readTap(b.data(), b.size(), g);
    if (!s.ok()) {
        test::fail(__FILE__, __LINE__, name + ": " + s.str());
        return false;
    }
    return true;
}

// A C5RT texture (render/tests/bakepost/mkref.py's format), levels tightly packed.
bool loadC5rt(const std::string& name, uint32_t& format, std::vector<std::vector<uint8_t>>& levels) {
    std::vector<uint8_t> b;
    if (!need(name, b)) return false;
    if (b.size() < 40 || std::memcmp(b.data(), "C5RT", 4) != 0) {
        test::fail(__FILE__, __LINE__, name + " is not a C5RT file");
        return false;
    }
    uint32_t h[9];
    std::memcpy(h, b.data() + 4, sizeof h);
    format = h[1];
    const uint32_t w = h[2], nsub = h[8];
    size_t o = 40;
    levels.clear();
    for (uint32_t i = 0; i < nsub; ++i) {
        if (o + 12 > b.size()) break;
        uint32_t rp, dp, n;
        std::memcpy(&rp, &b[o], 4); std::memcpy(&dp, &b[o + 4], 4); std::memcpy(&n, &b[o + 8], 4);
        o += 12;
        if (o + n > b.size() || rp == 0) break;
        const uint32_t mw = w >> i ? w >> i : 1, rows = n / rp;
        std::vector<uint8_t> lv;
        for (uint32_t r = 0; r < rows; ++r) lv.insert(lv.end(), b.begin() + o + r * rp, b.begin() + o + r * rp + mw);
        levels.push_back(lv);
        o += n;
    }
    if (levels.size() != nsub) {
        test::fail(__FILE__, __LINE__, name + " is truncated");
        return false;
    }
    return true;
}

// A small synthetic grid: mapW x mapH plots, h(x, y) = f(x, y).
heightfield::HeightGrid synthGrid(uint32_t mapW, uint32_t mapH, uint16_t (*f)(uint32_t, uint32_t)) {
    heightfield::HeightGrid g;
    g.mapW = mapW; g.mapH = mapH;
    g.samplesX = mapW * 64; g.samplesY = mapH * 64;
    g.h.resize(static_cast<size_t>(g.samplesX) * g.samplesY);
    for (uint32_t y = 0; y < g.samplesY; ++y)
        for (uint32_t x = 0; x < g.samplesX; ++x) g.h[g.index(x, y)] = f(x, y);
    return g;
}

uint16_t codeXY(uint32_t x, uint32_t y) {  // high byte encodes x mod 16 and y mod 16
    return static_cast<uint16_t>((((x & 15) << 4) | (y & 15)) << 8 | 0xFF);
}

uint32_t bitsOf(float f) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

bool sameBits(const float* a, const float* b, int n) {
    for (int i = 0; i < n; ++i)
        if (bitsOf(a[i]) != bitsOf(b[i])) return false;
    return true;
}

} // namespace

// ---- Synthetic ----

C5R_TEST(page_count_and_tile_rule) {
    heightfield::HeightGrid tiny = synthGrid(56, 40, [](uint32_t, uint32_t) -> uint16_t { return 0; });
    uint32_t px = 0, py = 0;
    CHECK_OK(heightPageCount(tiny, px, py));
    CHECK_EQ(px, 5u);
    CHECK_EQ(py, 3u);
    HeightPageIndex p;
    int32_t off[2];
    CHECK_OK(heightPageForTile(tiny, TileId{10, 33}, p, off));  // tile P of 3023's capture, origin (640, 2112)
    CHECK_EQ(p.px, 0u);
    CHECK_EQ(p.py, 2u);
    CHECK_EQ(off[0], 64);
    CHECK_EQ(off[1], -1728);
    CHECK_OK(heightPageForTile(tiny, TileId{13, 14}, p, off));  // origin 832 -> page 0, 896 -> page 1
    CHECK_EQ(p.px, 0u);
    CHECK_EQ(p.py, 1u);
    CHECK_OK(heightPageForTile(tiny, TileId{14, 13}, p, off));
    CHECK_EQ(p.px, 1u);
    CHECK_EQ(p.py, 0u);
    CHECK_EQ(off[0], 64 - 896);
    CHECK_EQ(off[1], 64);
    heightfield::HeightGrid huge;
    huge.mapW = 128; huge.mapH = 84; huge.samplesX = 8192; huge.samplesY = 5376;
    CHECK_OK(heightPageCount(huge, px, py));
    CHECK_EQ(px, 10u);
    CHECK_EQ(py, 7u);
    // Every tile lies inside its page: origin - (page origin - 64) + 64 samples <= 1023.
    for (uint32_t ty = 0; ty < huge.mapH; ++ty)
        for (uint32_t tx = 0; tx < huge.mapW; ++tx) {
            CHECK_OK(heightPageForTile(huge, TileId{tx, ty}, p, off));
            const int32_t lx = static_cast<int32_t>(tx * 64) + off[0], ly = static_cast<int32_t>(ty * 64) + off[1];
            if (lx < 64 || lx + 64 > 1023 || ly < 64 || ly + 64 > 1023) {
                test::fail(__FILE__, __LINE__, "tile outside its page");
                return;
            }
        }
}

C5R_TEST(page_texels_address_wrap_and_high_byte) {
    heightfield::HeightGrid g = synthGrid(56, 40, codeXY);
    HeightPage page;
    CHECK_OK(buildHeightPage(g, HeightPageIndex{0, 0}, 1, page));
    CHECK_EQ(page.levels.size(), size_t(1));
    const std::vector<uint8_t>& t = page.levels[0];
    auto expect = [&](uint32_t px, uint32_t py, uint32_t u, uint32_t v) {
        const int64_t sx = ((int64_t(px) * 896 - 64 + u) % g.samplesX + g.samplesX) % g.samplesX;
        const int64_t sy = ((int64_t(py) * 896 - 64 + 1023 - v) % g.samplesY + g.samplesY) % g.samplesY;
        return static_cast<uint8_t>(codeXY(static_cast<uint32_t>(sx), static_cast<uint32_t>(sy)) >> 8);
    };
    // u = 0 is sample x -64 -> wrapped to samplesX - 64; v = 1023 is sample row -64 -> samplesY - 64 (y wraps too).
    CHECK_EQ(t[1023 * 1024 + 0], static_cast<uint8_t>(codeXY(g.samplesX - 64, g.samplesY - 64) >> 8));
    CHECK_EQ(t[0], static_cast<uint8_t>(codeXY(g.samplesX - 64, 959) >> 8));
    CHECK_EQ(t[(1023 - 64) * 1024 + 64], static_cast<uint8_t>(codeXY(0, 0) >> 8));
    size_t bad = 0;
    for (uint32_t v = 0; v < 1024; v += 7)
        for (uint32_t u = 0; u < 1024; u += 5)
            if (t[v * 1024 + u] != expect(0, 0, u, v)) ++bad;
    CHECK_EQ(bad, size_t(0));
    HeightPage last;  // page x 4 of 5 starts at 3584 = samplesX: a wrapped copy of page 0
    CHECK_OK(buildHeightPage(g, HeightPageIndex{4, 2}, 1, last));
    HeightPage first;
    CHECK_OK(buildHeightPage(g, HeightPageIndex{0, 2}, 1, first));
    CHECK(last.levels[0] == first.levels[0]);
    // The texel is the high byte, not a rounded height: 0x12FF -> 0x12.
    heightfield::HeightGrid c = synthGrid(14, 14, [](uint32_t, uint32_t) -> uint16_t { return 0x12FF; });
    HeightPage cp;
    CHECK_OK(buildHeightPage(c, HeightPageIndex{0, 0}, kHeightPageMips, cp));
    CHECK_EQ(cp.levels.size(), size_t(kHeightPageMips));
    for (uint32_t m = 0; m < kHeightPageMips; ++m) {
        CHECK_EQ(cp.levels[m].size(), size_t((1024 >> m) * (1024 >> m)));
        CHECK_EQ(cp.levels[m][0], uint8_t(0x12));  // a constant page stays constant through box + sharpen
    }
}

C5R_TEST(mip_rule_box_then_sharpen) {
    // 4 x 4 -> 2 x 2. Box: (10+20+30+41)>>2 = 25 (truncating), (200+200+201+202)>>2 = 200, 0, 255.
    const std::vector<uint8_t> src = {10, 20, 200, 200,
                                      30, 41, 201, 202,
                                      0, 0, 255, 255,
                                      0, 3, 255, 255};
    std::vector<uint8_t> dst;
    CHECK_OK(heightPageNextMip(src, 4, dst));
    CHECK_EQ(dst.size(), size_t(4));
    // b = [25 200; 0 255]; neighbours clamped (the window of (0, 0) is rows {0, 0, 1} x cols {0, 0, 1}).
    auto ref = [](const int b[4], int x, int y) {
        int s = 0;
        for (int j = -1; j <= 1; ++j)
            for (int i = -1; i <= 1; ++i) {
                int yy = y + j < 0 ? 0 : (y + j > 1 ? 1 : y + j), xx = x + i < 0 ? 0 : (x + i > 1 ? 1 : x + i);
                s += (i == 0 && j == 0 ? 9 : -1) * b[yy * 2 + xx];
            }
        const int c = b[y * 2 + x];
        int d = s - c, q = d >= 0 ? d / 8 : -((-d + 7) / 8);  // floor division
        int v = c + q;
        return v < 0 ? 0 : (v > 255 ? 255 : v);
    };
    const int b[4] = {25, 200, 0, 255};
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) CHECK_EQ(int(dst[y * 2 + x]), ref(b, x, y));
    CHECK_EQ(int(dst[0]), 0);    // S = 225 - 730 = -505; 25 + floor(-530 / 8) = -42 -> 0
    CHECK_EQ(int(dst[3]), 255);  // S = 2295 - 1190 = 1105; 255 + 850 / 8 = 361 -> 255
    // 1 x 1 after 2 x 2: identity on the box.
    CHECK_OK(heightPageNextMip(std::vector<uint8_t>{1, 2, 3, 6}, 2, dst));
    CHECK_EQ(dst.size(), size_t(1));
    CHECK_EQ(int(dst[0]), 3);
}

C5R_TEST(constants_stock_values_and_rules) {
    const BakeLight l = stockBakeLight();
    const uint32_t aux[3] = {0x3EDDB3D7u, 0xBE800000u, 0x3F5DB3D7u};
    const uint32_t half[3] = {0xA37477F2u, 0xBE5DA258u, 0x3F79EE89u};  // tile P's TerrainFrame reg 21 (capture)
    for (int i = 0; i < 3; ++i) {
        CHECK_EQ(bitsOf(l.auxDir[i]), aux[i]);
        CHECK_EQ(bitsOf(l.halfVector[i]), half[i]);
    }
    float d[3];
    lightDirection(30.0, 150.0, d);
    CHECK(sameBits(d, l.auxDir, 3));            // AuxDir: the formula gives the stored bits
    lightDirection(25.0, 90.0, d);
    CHECK(sameBits(d + 1, l.lightDir + 1, 2));  // LightDir y, z equal; x is the x87 residue (5.3)
    CHECK(std::fabs(d[0] - l.lightDir[0]) < 1e-20f);
    CHECK_EQ(styleFromBits(0), 0u);
    CHECK_EQ(styleFromBits(1), 0u);   // bit 0 alone: Generic
    CHECK_EQ(styleFromBits(9), 3u);   // bits 0 and 3 (3204's tiles): Africa
    CHECK_EQ(styleFromBits(0x14), 2u);
    CHECK_EQ(styleFromBits(0x20), 0u);  // bits above 4 do not choose
    const uint8_t types[] = {0, 4, 2, 40};
    CHECK_EQ(styleFromArtTypes(types, 4), 2u);
    CHECK_EQ(styleFromArtTypes(nullptr, 3), 0u);
    CHECK_EQ(sizeClassForProjectedSize(300.0f, 64), 512u);
    CHECK_EQ(sizeClassForProjectedSize(256.0f, 64), 256u);
    CHECK_EQ(sizeClassForProjectedSize(129.0f, 64), 256u);
    CHECK_EQ(sizeClassForProjectedSize(128.0f, 64), 128u);
    CHECK_EQ(sizeClassForProjectedSize(64.0f, 64), 64u);
    CHECK_EQ(sizeClassForProjectedSize(10.0f, 128), 128u);
    heightfield::HeightGrid tiny = synthGrid(56, 40, [](uint32_t, uint32_t) -> uint16_t { return 0; });
    BakeTimeConstants k;
    const uint8_t pt[] = {3, 0};
    CHECK_OK(bakeTimeConstants(tiny, TileId{10, 33}, pt, 2, k));
    CHECK_EQ(k.page.px, 0u);
    CHECK_EQ(k.page.py, 2u);
    CHECK_EQ(k.k.heightOffset[0], 64.0f);
    CHECK_EQ(k.k.heightOffset[1], -1728.0f);
    CHECK(sameBits(k.k.halfVector, l.halfVector, 3));
    CHECK(sameBits(k.k.shadowDir, l.auxDir, 3));
    CHECK_EQ(k.style, 3u);
    CHECK_EQ(k.sizeClass, kCacheSizeDefault);
}

C5R_TEST(negative_inputs) {
    heightfield::HeightGrid empty;
    uint32_t px, py;
    CHECK_ERR(heightPageCount(empty, px, py), Err::BadLayout);
    heightfield::HeightGrid bad = synthGrid(2, 2, [](uint32_t, uint32_t) -> uint16_t { return 0; });
    bad.samplesX = 100;  // not plots * 64
    CHECK_ERR(heightPageCount(bad, px, py), Err::BadLayout);
    HeightPage page;
    CHECK_ERR(buildHeightPage(bad, HeightPageIndex{0, 0}, 1, page), Err::BadLayout);
    heightfield::HeightGrid shortH = synthGrid(2, 2, [](uint32_t, uint32_t) -> uint16_t { return 0; });
    shortH.h.pop_back();
    CHECK_ERR(buildHeightPage(shortH, HeightPageIndex{0, 0}, 1, page), Err::BadLayout);
    CHECK(page.levels.empty());
    heightfield::HeightGrid g = synthGrid(2, 2, [](uint32_t, uint32_t) -> uint16_t { return 0; });
    CHECK_ERR(buildHeightPage(g, HeightPageIndex{1, 0}, 1, page), Err::NotFound);
    CHECK_ERR(buildHeightPage(g, HeightPageIndex{0, 1}, 1, page), Err::NotFound);
    CHECK_ERR(buildHeightPage(g, HeightPageIndex{0, 0}, 5, page), Err::Unsupported);
    CHECK_ERR(buildHeightPage(g, HeightPageIndex{0, 0}, 0, page), Err::Unsupported);
    CHECK(page.levels.empty());
    HeightPageIndex p;
    int32_t off[2] = {7, 7};
    CHECK_ERR(heightPageForTile(g, TileId{2, 0}, p, off), Err::NotFound);
    CHECK_ERR(heightPageForTile(g, TileId{0, 2}, p, off), Err::NotFound);
    CHECK_ERR(heightPageForTile(empty, TileId{0, 0}, p, off), Err::BadLayout);
    CHECK_EQ(off[0], 0);
    std::vector<uint8_t> dst;
    CHECK_ERR(heightPageNextMip(std::vector<uint8_t>(9, 0), 3, dst), Err::BadLayout);
    CHECK_ERR(heightPageNextMip(std::vector<uint8_t>(4, 0), 0, dst), Err::BadLayout);
    CHECK_ERR(heightPageNextMip(std::vector<uint8_t>(5, 0), 2, dst), Err::BadLayout);
    BakeTimeConstants k;
    CHECK_ERR(bakeTimeConstants(g, TileId{9, 9}, nullptr, 0, k), Err::NotFound);
    ID3D11Texture2D* tex = reinterpret_cast<ID3D11Texture2D*>(1);
    ID3D11ShaderResourceView* srv = reinterpret_cast<ID3D11ShaderResourceView*>(1);
    HeightPage ok;
    CHECK_OK(buildHeightPage(g, HeightPageIndex{0, 0}, 1, ok));
    CHECK_ERR(createHeightPageTexture(nullptr, ok, &tex, &srv), Err::BadLayout);
    CHECK(tex == nullptr && srv == nullptr);
}

C5R_TEST(texture_upload_round_trip) {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev,
                                   nullptr, &ctx);
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr,
                               &ctx);
    if (FAILED(hr)) {
        test::fail(__FILE__, __LINE__, "no D3D11 device");
        return;
    }
    heightfield::HeightGrid g = synthGrid(15, 15, codeXY);
    HeightPage page;
    CHECK_OK(buildHeightPage(g, HeightPageIndex{0, 0}, kHeightPageMips, page));
    ID3D11Texture2D* tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    CHECK_OK(createHeightPageTexture(dev, page, &tex, &srv));
    HeightPage broken = page;
    broken.levels[3].pop_back();
    ID3D11Texture2D* t2 = nullptr;
    ID3D11ShaderResourceView* s2 = nullptr;
    CHECK_ERR(createHeightPageTexture(dev, broken, &t2, &s2), Err::BadLayout);
    broken.levels.resize(4);
    CHECK_ERR(createHeightPageTexture(dev, broken, &t2, &s2), Err::BadLayout);
    if (tex && srv) {
        D3D11_TEXTURE2D_DESC d;
        tex->GetDesc(&d);
        CHECK_EQ(d.Format, DXGI_FORMAT_A8_UNORM);
        CHECK_EQ(d.MipLevels, kHeightPageMips);
        d.Usage = D3D11_USAGE_STAGING;
        d.BindFlags = 0;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D* st = nullptr;
        CHECK(SUCCEEDED(dev->CreateTexture2D(&d, nullptr, &st)));
        if (st) {
            ctx->CopyResource(st, tex);
            size_t bad = 0;
            for (uint32_t m = 0; m < kHeightPageMips; ++m) {
                D3D11_MAPPED_SUBRESOURCE ms;
                if (FAILED(ctx->Map(st, m, D3D11_MAP_READ, 0, &ms))) { ++bad; continue; }
                const uint32_t n = 1024 >> m;
                for (uint32_t y = 0; y < n; ++y)
                    if (std::memcmp(static_cast<const uint8_t*>(ms.pData) + y * ms.RowPitch,
                                    page.levels[m].data() + y * n, n) != 0)
                        ++bad;
                ctx->Unmap(st, m);
            }
            CHECK_EQ(bad, size_t(0));
            st->Release();
        }
        srv->Release();
        tex->Release();
    }
    ctx->Release();
    dev->Release();
}

// ---- Against the EXE ----

C5R_TEST(game_tiny_page_equals_capture_all_mips) {
    heightfield::HeightGrid g;
    if (!loadTap("tiny_tap.c5hf", g)) return;
    uint32_t fmt = 0;
    std::vector<std::vector<uint8_t>> ref;
    if (!loadC5rt("tiny_page_0_2.c5rt", fmt, ref)) return;
    CHECK_EQ(fmt, uint32_t(DXGI_FORMAT_A8_UNORM));
    CHECK_EQ(ref.size(), size_t(kHeightPageMips));
    HeightPageIndex p;
    int32_t off[2];
    CHECK_OK(heightPageForTile(g, TileId{10, 33}, p, off));  // tile P, the capture's bake
    HeightPage page;
    CHECK_OK(buildHeightPage(g, p, kHeightPageMips, page));
    size_t equal = 0, total = 0;
    for (size_t m = 0; m < ref.size() && m < page.levels.size(); ++m) {
        CHECK_EQ(ref[m].size(), page.levels[m].size());
        for (size_t i = 0; i < ref[m].size() && i < page.levels[m].size(); ++i, ++total)
            if (ref[m][i] == page.levels[m][i]) ++equal;
    }
    std::printf("  tiny page (%u, %u): %zu of %zu bytes equal over %zu mips\n", p.px, p.py, equal, total, ref.size());
    CHECK_EQ(total, size_t(kHeightPageBytes));
    CHECK_EQ(equal, total);
    // The wrapped last page column equals page 0's.
    HeightPage wrapped;
    CHECK_OK(buildHeightPage(g, HeightPageIndex{4, 2}, kHeightPageMips, wrapped));
    CHECK(wrapped.levels == page.levels);
    // A neighbouring page differs (the comparison can fail).
    HeightPage other;
    CHECK_OK(buildHeightPage(g, HeightPageIndex{1, 2}, kHeightPageMips, other));
    CHECK(other.levels[0] != ref[0]);
}

C5R_TEST(game_tiny_constants_equal_capture_frame) {
    heightfield::HeightGrid g;
    if (!loadTap("tiny_tap.c5hf", g)) return;
    std::vector<uint8_t> fr;
    if (!need("tiny_P_frame.bin", fr)) return;
    if (fr.size() < 0x250) {
        test::fail(__FILE__, __LINE__, "tiny_P_frame.bin shorter than TerrainFrame");
        return;
    }
    auto reg = [&](int r, float out[4]) { std::memcpy(out, fr.data() + r * 16, 16); };
    float hex[4], aux[4], light[4], cam[4], axis[4], half[4], hts[4], msz[4];
    reg(16, hex); reg(17, aux); reg(18, light); reg(19, cam); reg(20, axis); reg(21, half); reg(22, hts); reg(24, msz);
    BakeTimeConstants k;
    CHECK_OK(bakeTimeConstants(g, TileId{10, 33}, nullptr, 0, k));
    const BakeLight l = stockBakeLight();
    CHECK(sameBits(k.k.halfVector, half, 3));
    CHECK(sameBits(k.k.shadowDir, aux, 3));
    CHECK(sameBits(l.lightDir, light, 3));
    CHECK(sameBits(k.k.hexScale, hex, 3));
    CHECK(sameBits(k.k.heightTexelScale, hts, 2));
    const float zero3[3] = {0, 0, 0}, z1[3] = {0, 0, 1};
    CHECK(sameBits(cam, zero3, 3));   // identity bake view
    CHECK(sameBits(axis, z1, 3));
    CHECK_EQ(msz[0], float(g.samplesX));  // MapSize holds samples
    CHECK_EQ(msz[1], float(g.samplesY));
    CHECK_EQ(k.k.heightOffset[0], 64.0f);
    CHECK_EQ(k.k.heightOffset[1], -1728.0f);
}

namespace {
// tiles_<save>.txt: header lines start with '#'; then "tile x y originX originY offX offY handle bits138" per tile
// (handle and bits "-" when the dump did not read them) and "page px py handle" per non-zero entry of TR+0x12AC.
void checkTilesFile(const std::string& tapName, const std::string& tilesName) {
    heightfield::HeightGrid g;
    if (!loadTap(tapName, g)) return;
    std::vector<uint8_t> b;
    if (!need(tilesName, b)) return;
    std::istringstream in(std::string(b.begin(), b.end()));
    std::string line;
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> pageHandle;
    std::map<std::pair<uint32_t, uint32_t>, std::set<uint32_t>> tileHandles;
    std::set<std::pair<uint32_t, uint32_t>> used;
    size_t tiles = 0, ruleOk = 0, originOk = 0;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string kind;
        ls >> kind;
        if (kind == "page") {
            uint32_t px, py, h;
            ls >> px >> py >> h;
            pageHandle[{px, py}] = h;
        } else if (kind == "tile") {
            uint32_t x, y, ox, oy;
            int32_t ex, ey;
            std::string hs;
            ls >> x >> y >> ox >> oy >> ex >> ey >> hs;
            ++tiles;
            if (ox == x * 64 && oy == y * 64) ++originOk;
            HeightPageIndex p;
            int32_t off[2];
            if (heightPageForTile(g, TileId{x, y}, p, off).ok() && off[0] == ex && off[1] == ey) ++ruleOk;
            used.insert({p.px, p.py});
            if (hs != "-") tileHandles[{p.px, p.py}].insert(static_cast<uint32_t>(std::stoul(hs)));
        }
    }
    uint32_t pagesX = 0, pagesY = 0;
    CHECK_OK(heightPageCount(g, pagesX, pagesY));
    size_t handleOk = 0;
    for (const auto& e : tileHandles)
        if (e.second.size() == 1 && pageHandle.count(e.first) && pageHandle[e.first] == *e.second.begin()) ++handleOk;
    std::printf("  %s: %zu tiles, origin rule %zu, page/offset rule %zu; %zu pages used, %zu with one handle equal to "
                "TR+0x12AC; table holds %zu handles for %u x %u pages\n",
                tilesName.c_str(), tiles, originOk, ruleOk, used.size(), handleOk, pageHandle.size(), pagesX,
                pagesY);
    CHECK_EQ(tiles, size_t(g.mapW) * g.mapH);
    CHECK_EQ(originOk, tiles);
    CHECK_EQ(ruleOk, tiles);
    CHECK_EQ(handleOk, tileHandles.size());
    // Files whose handles were read carry the page table too (the EXE's TR+0x12AC, one handle per page).
    if (!tileHandles.empty()) CHECK_EQ(pageHandle.size(), size_t(pagesX) * pagesY);
}
} // namespace

// Every distinct height page found in the RenderDoc exports of 3023 (Tiny) and 3402 (Tiny and the edited "slope" copy):
// pages.txt lines "<tap> <c5rt> px py"; each must equal the page built from its save's tap, all mips.
C5R_TEST(game_captured_pages_equal_rule_all_mips) {
    std::vector<uint8_t> list;
    if (!need("pages.txt", list)) return;
    std::istringstream in(std::string(list.begin(), list.end()));
    std::map<std::string, heightfield::HeightGrid> taps;
    std::string tap, file;
    uint32_t px, py;
    size_t pages = 0, equalPages = 0;
    while (in >> tap >> file >> px >> py) {
        if (!taps.count(tap) && !loadTap(tap, taps[tap])) return;
        uint32_t fmt = 0;
        std::vector<std::vector<uint8_t>> ref;
        if (!loadC5rt(file, fmt, ref)) return;
        HeightPage page;
        CHECK_OK(buildHeightPage(taps[tap], HeightPageIndex{px, py}, kHeightPageMips, page));
        ++pages;
        if (fmt == uint32_t(DXGI_FORMAT_A8_UNORM) && page.levels == ref) ++equalPages;
        else std::printf("  %s: page (%u, %u) differs\n", file.c_str(), px, py);
        // Negative: the same capture against the next page index must differ.
        HeightPage wrong;
        CHECK_OK(buildHeightPage(taps[tap], HeightPageIndex{(px + 1) % 4, py}, kHeightPageMips, wrong));
        CHECK(wrong.levels != ref);
    }
    std::printf("  captured pages: %zu of %zu equal in all 11 mips (%zu saves)\n", equalPages, pages, taps.size());
    CHECK(pages >= 15);
    CHECK_EQ(equalPages, pages);
}

C5R_TEST(tile_style_from_plots_synthetic) {
    // 4 x 4 plots, all 0 except a few; tile rows t.y - 2 and t.y - 1.
    uint8_t a[16] = {0};
    a[0 * 4 + 1] = 4;  // plot (1, 0)
    a[1 * 4 + 0] = 3;  // plot (0, 1)
    CHECK_EQ(tileStyle(a, 4, 4, false, TileId{1, 2}), 3u);  // even: (0,1) (1,0) (1,1) (2,0) -> bits 3, 4 -> 3
    CHECK_EQ(tileStyle(a, 4, 4, false, TileId{2, 2}), 0u);  // even: (1,1) (2,0) (2,1) (3,0) -> none
    CHECK_EQ(tileStyle(a, 4, 4, false, TileId{2, 3}), 0u);  // odd: (1,1) (2,1) (2,2) (3,2) + fifth (1,2)
    CHECK_EQ(tileStyle(a, 4, 4, false, TileId{0, 3}), 3u);  // odd: (-1,1) skipped, (0,1) = 3
    a[1 * 4 + 3] = 2;  // plot (3, 1): reached from tile (0, 3) only through an x wrap, which the EXE does not do (0383)
    CHECK_EQ(tileStyle(a, 4, 4, true, TileId{0, 3}), 3u);
    CHECK_EQ(tileStyle(a, 4, 4, false, TileId{0, 3}), 3u);
    CHECK_EQ(tileStyle(a, 4, 4, false, TileId{0, 0}), 0u);  // rows -2, -1: outside
    CHECK_EQ(tileStyle(nullptr, 4, 4, false, TileId{1, 2}), 0u);
    CHECK_EQ(tileStyle(a, 0, 4, false, TileId{1, 2}), 0u);
    CHECK_EQ(tileStyle(a, 4, 0, false, TileId{1, 2}), 0u);
}

// The fifth plot (ticket 0383): tiles with 1 <= x <= (y + 1) / 2 also list (x - 1, y - 2) (even y) or (x - 1, y - 1)
// (odd y); no other tile does, and x never wraps.
C5R_TEST(tile_style_fifth_plot_synthetic) {
    CHECK(!tileListsFifthPlot(TileId{0, 0}));
    CHECK(!tileListsFifthPlot(TileId{1, 0}));
    CHECK(tileListsFifthPlot(TileId{1, 1}));  // (1 + 1) / 2 = 1
    CHECK(tileListsFifthPlot(TileId{1, 2}));
    CHECK(!tileListsFifthPlot(TileId{2, 2}));
    CHECK(tileListsFifthPlot(TileId{2, 3}));
    CHECK(!tileListsFifthPlot(TileId{3, 4}));
    CHECK(tileListsFifthPlot(TileId{3, 5}));
    CHECK(!tileListsFifthPlot(TileId{0, 39}));  // column 0 never: its column x - 1 is outside and not wrapped
    CHECK(tileListsFifthPlot(TileId{20, 39}));
    CHECK(!tileListsFifthPlot(TileId{21, 39}));
    CHECK(tileListsFifthPlot(TileId{0xFFFFFFFFu >> 1, 0xFFFFFFFFu}));  // no overflow in (y + 1) / 2
    // The triangle's size on the probed maps: Tiny 56 x 40 tiles 400, huge 128 x 84 tiles 1,764: 3406's probes have
    // 399 and 1,763 lists of five plots, plus tile (1, 1), whose list holds the fifth plot (0, 0) but only 3 plots.
    const uint32_t dims[2][3] = {{56, 40, 400}, {128, 84, 1764}};
    for (const auto& d : dims) {
        uint32_t n = 0;
        for (uint32_t y = 0; y < d[1]; ++y)
            for (uint32_t x = 0; x < d[0]; ++x) n += tileListsFifthPlot(TileId{x, y});
        CHECK_EQ(n, d[2]);
    }
    // 6 x 6 plots; each case sets one plot that only the fifth plot (or a wrap) reaches.
    uint8_t e[36] = {0};
    e[0 * 6 + 0] = 4;  // plot (0, 0)
    CHECK_EQ(tileStyle(e, 6, 6, false, TileId{1, 2}), 4u);   // even (1, 2): four (0,1) (1,0) (1,1) (2,0), fifth (0,0)
    CHECK_EQ(tileStyle(e, 6, 6, false, TileId{1, 1}), 4u);   // odd (1, 1): in the map (1,0) (2,0), fifth (0,0)
    CHECK_EQ(tileStyle(e, 6, 6, false, TileId{2, 2}), 0u);   // even (2, 2): 2 > 1, no fifth plot
    std::fill(e, e + 36, uint8_t(0));
    e[2 * 6 + 1] = 2;  // plot (1, 2)
    CHECK_EQ(tileStyle(e, 6, 6, false, TileId{2, 3}), 2u);   // odd (2, 3): four (1,1) (2,1) (2,2) (3,2), fifth (1,2)
    CHECK_EQ(tileStyle(e, 6, 6, false, TileId{2, 4}), 2u);   // even (2, 4): four (1,3) (2,2) (2,3) (3,2), fifth (1,2)
    std::fill(e, e + 36, uint8_t(0));
    e[2 * 6 + 2] = 2;  // plot (2, 2): where even (3, 4)'s fifth plot would be
    CHECK_EQ(tileStyle(e, 6, 6, false, TileId{3, 4}), 0u);   // even (3, 4): 3 > 2, no fifth plot
    std::fill(e, e + 36, uint8_t(0));
    e[4 * 6 + 2] = 3;  // plot (2, 4)
    CHECK_EQ(tileStyle(e, 6, 6, false, TileId{3, 5}), 3u);   // odd (3, 5): four (2,3) (3,3) (3,4) (4,4), fifth (2,4)
    std::fill(e, e + 36, uint8_t(0));
    e[4 * 6 + 3] = 3;  // plot (3, 4): where odd (4, 5)'s fifth plot would be
    CHECK_EQ(tileStyle(e, 6, 6, false, TileId{4, 5}), 0u);   // odd (4, 5): 4 > 3, no fifth plot
    std::fill(e, e + 36, uint8_t(0));
    e[3 * 6 + 5] = 1;  // plot (5, 3): tile (0, 4)'s (x - 1, y - 1) only through a wrap
    CHECK_EQ(tileStyle(e, 6, 6, true, TileId{0, 4}), 0u);
    e[3 * 6 + 5] = 0;
    e[2 * 6 + 0] = 1;  // plot (0, 2): tile (5, 4)'s (x + 1, y - 2) only through a wrap
    CHECK_EQ(tileStyle(e, 6, 6, true, TileId{5, 4}), 0u);
    // An art type >= 32 sets no bit and the lower one decides.
    std::fill(e, e + 36, uint8_t(0));
    e[0] = 200;
    e[1 * 6 + 0] = 3;
    CHECK_EQ(tileStyle(e, 6, 6, false, TileId{1, 2}), 3u);
}

// The style rule against the EXE: every tile's tile+0x138 (issue 3406's probe of the Huge save) and every plot's
// continent art type read through the Lua channel at the same time (huge_art.txt: "W H a,a,a,...").
namespace {
void checkStyle(const char* save, const std::string& artName, const std::string& tilesName, size_t wantTiles,
                size_t minStyled) {
    std::vector<uint8_t> artFile, tilesFile;
    if (!need(artName, artFile) || !need(tilesName, tilesFile)) return;
    std::istringstream as(std::string(artFile.begin(), artFile.end()));
    uint32_t W = 0, H = 0;
    std::string list;
    as >> W >> H >> list;
    std::vector<uint8_t> art;
    std::stringstream ls(list);
    std::string v;
    while (std::getline(ls, v, ',')) art.push_back(static_cast<uint8_t>(std::stoul(v)));
    CHECK_EQ(art.size(), size_t(W) * H);
    if (art.size() != size_t(W) * H) return;
    std::istringstream in(std::string(tilesFile.begin(), tilesFile.end()));
    std::string line;
    size_t tiles = 0, equal = 0, styled = 0;
    while (std::getline(in, line)) {
        std::istringstream l(line);
        std::string kind;
        uint32_t x, y, ox, oy, h, bits;
        int32_t ex, ey;
        if (!(l >> kind) || kind != "tile") continue;
        l >> x >> y >> ox >> oy >> ex >> ey >> h >> bits;
        ++tiles;
        const uint32_t want = styleFromBits(bits);
        if (want) ++styled;
        if (tileStyle(art.data(), W, H, true, TileId{x, y}) == want) ++equal;
    }
    std::printf("  %s: tile style from art types equals tile+0x138's for %zu of %zu tiles (%zu not Generic)\n", save,
                equal, tiles, styled);
    CHECK_EQ(tiles, wantTiles);
    CHECK_EQ(equal, tiles);      // with the fifth plot and no wrap (0383): every tile (was 4-plot: Tiny 2,237, huge 10,748)
    CHECK(styled >= minStyled);  // the comparison is not trivially all Generic
}
} // namespace

C5R_TEST(game_huge_tile_style_from_art_types) { checkStyle("huge", "huge_art.txt", "tiles_huge.txt", 10752, 1000); }
C5R_TEST(game_tiny_tile_style_from_art_types) { checkStyle("tiny", "tiny_art.txt", "tiles_tiny.txt", 2240, 100); }

C5R_TEST(game_tiny_every_tile_page_and_offset) { checkTilesFile("tiny_tap.c5hf", "tiles_tiny.txt"); }
C5R_TEST(game_huge_every_tile_page_and_offset) { checkTilesFile("huge_tap.c5hf", "tiles_huge.txt"); }

C5R_TEST_MAIN
