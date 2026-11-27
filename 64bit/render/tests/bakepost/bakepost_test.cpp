// render/tests/bakepost/bakepost_test.cpp - c5r::bake::BakePost (issue 3403): the passes after the blend, compared with
// the EXE's own results for one tile.
//
// Expected data: C5R_BAKE_REF = a folder made by mkref.py from the RenderDoc capture of 3023 run 3 (game data, kept on D,
// never committed; default D:/c5-64bit-work/3400/w3/ref380). The capture's initial contents hold the EXE's whole bake of
// the tile baked just before the frame ("P"): blend targets, normal, macro shadow, RGBA8 caches and the BC1 blocks. The
// EXE (C5R_EXE) gives the shaders, the install (C5R_GAME_DIR) the style cubes and the ColorKey LUT for the
// game-file variant. A missing input fails the test with its path. Difference images go to
// C5R_WORK/bakepost/. research/05-graphics-composition/terrain-bake-passes.md section 7 has the figures.
#include <windows.h>
#include <d3d11.h>
#include <DirectXPackedVector.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "c5r/bake/layers.h"     // tileWindow (issue 0371)
#include "c5r/bake/pipeline.h"   // makeExeGridNormals (issue 0371)
#include "c5r/bake/post.h"
#include "c5r/decal/decal.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/gamefs/dds.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/shaders/shadersection.h"
#include "testkit.h"
#include "testutil.h"

using Microsoft::WRL::ComPtr;
using namespace c5r;
using namespace c5r::bake;

namespace {

std::wstring wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }

// Issue 0371: the up-res capture's folder (C5R_BAKE_REF_UPRES, terrain-bake-passes.md 3.5.4) is read by the tests that
// switch to it (RefScope); every other test reads 3403's tile P.
const char* g_refEnv = "C5R_BAKE_REF";
const char* g_refDefault = "D:/c5-64bit-work/3400/w3/ref380";
bool g_needSecond = true;   // tile P's tile+0x2FC (3403's P has t8; the up-res tile's is in its folder's flags)
bool g_shadowGiven = false; // issue 0371: bakeTileP hands the EXE's macro shadow (P_shadow) to the bake (macroShadowGiven)

std::string refDir() {
    const char* e = std::getenv(g_refEnv);
    return e && *e ? e : g_refDefault;
}

struct RefScope {
    const char* env;
    const char* def;
    bool second;
    RefScope(const char* e, const char* d, bool needSecond) : env(g_refEnv), def(g_refDefault), second(g_needSecond) {
        g_refEnv = e;
        g_refDefault = d;
        g_needSecond = needSecond;
    }
    ~RefScope() {
        g_refEnv = env;
        g_refDefault = def;
        g_needSecond = second;
    }
};

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

// A required input: fails the test (not a skip) with the path when it is missing.
bool need(const std::string& name, std::vector<uint8_t>& out) {
    const std::string path = refDir() + "/" + name;
    if (!readFile(path, out)) {
        test::fail(__FILE__, __LINE__, "missing expected data " + path + " (set C5R_BAKE_REF; make it with mkref.py)");
        return false;
    }
    return true;
}

struct Sub { uint32_t rowPitch, depthPitch; std::vector<uint8_t> data; };
struct RefTex {
    uint32_t format = 0, w = 0, h = 0, depth = 0, mips = 0, array = 0, flags = 0;
    std::vector<Sub> subs;
};

bool loadRef(const std::string& name, RefTex& t) {
    std::vector<uint8_t> b;
    if (!need(name + ".c5rt", b)) return false;
    if (b.size() < 40 || std::memcmp(b.data(), "C5RT", 4) != 0) {
        test::fail(__FILE__, __LINE__, name + ".c5rt is not a C5RT file");
        return false;
    }
    uint32_t h[9];
    std::memcpy(h, b.data() + 4, sizeof h);
    t.format = h[1]; t.w = h[2]; t.h = h[3]; t.depth = h[4]; t.mips = h[5]; t.array = h[6]; t.flags = h[7];
    size_t at = 40;
    for (uint32_t i = 0; i < h[8]; ++i) {
        uint32_t s[3];
        if (at + 12 > b.size()) return false;
        std::memcpy(s, b.data() + at, 12);
        at += 12;
        if (at + s[2] > b.size()) return false;
        t.subs.push_back(Sub{s[0], s[1], std::vector<uint8_t>(b.begin() + at, b.begin() + at + s[2])});
        at += s[2];
    }
    return true;
}

struct Dev {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    bool create(bool allowWarp) {
        const D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        if (SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx)))
            return true;
        return allowWarp && SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION,
                                                        &dev, nullptr, &ctx));
    }
};

// Bytes per row (of blocks for BC) and rows for a mip of a 2D format.
void rowInfo(DXGI_FORMAT f, uint32_t w, uint32_t h, uint32_t& rowBytes, uint32_t& rows) {
    switch (f) {
    case DXGI_FORMAT_BC1_TYPELESS: case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB:
        rowBytes = std::max(1u, (w + 3) / 4) * 8; rows = std::max(1u, (h + 3) / 4); return;
    case DXGI_FORMAT_BC3_TYPELESS: case DXGI_FORMAT_BC3_UNORM:
        rowBytes = std::max(1u, (w + 3) / 4) * 16; rows = std::max(1u, (h + 3) / 4); return;
    case DXGI_FORMAT_A8_UNORM: rowBytes = w; rows = h; return;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R32G32_UINT:
        rowBytes = w * 8; rows = h; return;
    default: rowBytes = w * 4; rows = h; return;
    }
}

// Creates a texture from a reference file; srvFmt / dim pick the view the EXE used.
bool makeTex(Dev& d, const RefTex& r, UINT bind, UINT misc, DXGI_FORMAT srvFmt, D3D11_SRV_DIMENSION dim,
             ComPtr<ID3D11Resource>& tex, ComPtr<ID3D11ShaderResourceView>& srv, bool uploadAllMips = true) {
    std::vector<D3D11_SUBRESOURCE_DATA> init;
    for (const Sub& s : r.subs) init.push_back(D3D11_SUBRESOURCE_DATA{s.data.data(), s.rowPitch, s.depthPitch});
    HRESULT hr;
    if (r.depth > 1) {
        D3D11_TEXTURE3D_DESC t = {r.w, r.h, r.depth, r.mips, static_cast<DXGI_FORMAT>(r.format), D3D11_USAGE_DEFAULT, bind, 0, misc};
        ComPtr<ID3D11Texture3D> t3;
        hr = d.dev->CreateTexture3D(&t, init.data(), &t3);
        tex = t3;
    } else {
        D3D11_TEXTURE2D_DESC t = {};
        t.Width = r.w; t.Height = r.h; t.MipLevels = r.mips; t.ArraySize = r.array;
        t.Format = static_cast<DXGI_FORMAT>(r.format); t.SampleDesc.Count = 1; t.Usage = D3D11_USAGE_DEFAULT;
        t.BindFlags = bind; t.MiscFlags = misc;
        ComPtr<ID3D11Texture2D> t2;
        hr = d.dev->CreateTexture2D(&t, nullptr, &t2);
        if (SUCCEEDED(hr))
            for (uint32_t i = 0; i < r.subs.size(); ++i)
                if (uploadAllMips || i % r.mips == 0)
                    d.ctx->UpdateSubresource(t2.Get(), i, nullptr, r.subs[i].data.data(), r.subs[i].rowPitch, r.subs[i].depthPitch);
        tex = t2;
    }
    if (FAILED(hr)) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC v = {};
    v.Format = srvFmt;
    v.ViewDimension = dim;
    if (dim == D3D11_SRV_DIMENSION_TEXTURE3D) v.Texture3D.MipLevels = r.mips;
    else if (dim == D3D11_SRV_DIMENSION_TEXTURECUBE) v.TextureCube.MipLevels = r.mips;
    else v.Texture2D.MipLevels = r.mips;
    return SUCCEEDED(d.dev->CreateShaderResourceView(tex.Get(), &v, &srv));
}

// Tight bytes of mip m of a 2D texture.
std::vector<uint8_t> readback(Dev& d, ID3D11Texture2D* t, uint32_t m) {
    D3D11_TEXTURE2D_DESC desc;
    t->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> st;
    if (FAILED(d.dev->CreateTexture2D(&desc, nullptr, &st))) return {};
    d.ctx->CopyResource(st.Get(), t);
    const uint32_t w = std::max(1u, desc.Width >> m), h = std::max(1u, desc.Height >> m);
    uint32_t rowBytes, rows;
    rowInfo(desc.Format, w, h, rowBytes, rows);
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(d.ctx->Map(st.Get(), m, D3D11_MAP_READ, 0, &ms))) return {};
    std::vector<uint8_t> out(static_cast<size_t>(rowBytes) * rows);
    for (uint32_t y = 0; y < rows; ++y)
        std::memcpy(out.data() + static_cast<size_t>(y) * rowBytes, static_cast<const uint8_t*>(ms.pData) + static_cast<size_t>(y) * ms.RowPitch, rowBytes);
    d.ctx->Unmap(st.Get(), m);
    return out;
}

std::vector<uint8_t> tight(const RefTex& r, uint32_t m) {
    uint32_t rowBytes, rows;
    rowInfo(static_cast<DXGI_FORMAT>(r.format), std::max(1u, r.w >> m), std::max(1u, r.h >> m), rowBytes, rows);
    std::vector<uint8_t> out(static_cast<size_t>(rowBytes) * rows);
    for (uint32_t y = 0; y < rows; ++y)
        std::memcpy(out.data() + static_cast<size_t>(y) * rowBytes, r.subs[m].data.data() + static_cast<size_t>(y) * r.subs[m].rowPitch, rowBytes);
    return out;
}

// BC1 decode to RGB8 (4-colour mode with the usual 2/3-1/3 rounding; 3-colour mode when c0 <= c1).
std::vector<uint8_t> decodeBc1(const std::vector<uint8_t>& blocks, uint32_t w, uint32_t h) {
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
    const uint32_t bw = std::max(1u, w / 4), bh = std::max(1u, h / 4);
    for (uint32_t by = 0; by < bh; ++by)
        for (uint32_t bx = 0; bx < bw; ++bx) {
            const uint8_t* b = blocks.data() + (static_cast<size_t>(by) * bw + bx) * 8;
            const uint16_t c[2] = {static_cast<uint16_t>(b[0] | b[1] << 8), static_cast<uint16_t>(b[2] | b[3] << 8)};
            int pal[4][3];
            for (int k = 0; k < 2; ++k) {
                const int r = (c[k] >> 11) & 31, g = (c[k] >> 5) & 63, bl = c[k] & 31;
                pal[k][0] = (r << 3) | (r >> 2); pal[k][1] = (g << 2) | (g >> 4); pal[k][2] = (bl << 3) | (bl >> 2);
            }
            for (int ch = 0; ch < 3; ++ch) {
                if (c[0] > c[1]) {
                    pal[2][ch] = (2 * pal[0][ch] + pal[1][ch] + 1) / 3;
                    pal[3][ch] = (pal[0][ch] + 2 * pal[1][ch] + 1) / 3;
                } else {
                    pal[2][ch] = (pal[0][ch] + pal[1][ch]) / 2;
                    pal[3][ch] = 0;
                }
            }
            const uint32_t idx = b[4] | b[5] << 8 | b[6] << 16 | static_cast<uint32_t>(b[7]) << 24;
            for (uint32_t p = 0; p < 16; ++p) {
                const uint32_t x = bx * 4 + p % 4, y = by * 4 + p / 4;
                if (x >= w || y >= h) continue;
                for (int ch = 0; ch < 3; ++ch) rgb[(static_cast<size_t>(y) * w + x) * 3 + ch] = static_cast<uint8_t>(pal[(idx >> (2 * p)) & 3][ch]);
            }
        }
    return rgb;
}

struct Diff { double equalShare = 0, median = 0, p95 = 0, maxAbs = 0; size_t n = 0; };

// Per-element comparison of two byte images (each byte one channel value).
Diff compareBytes(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, size_t stride = 1, size_t channels = 1) {
    Diff d;
    if (a.size() != b.size() || a.empty()) { d.maxAbs = 1e9; return d; }
    std::vector<int> v;
    size_t eq = 0;
    for (size_t i = 0; i < a.size(); i += stride)
        for (size_t c = 0; c < channels && i + c < a.size(); ++c) {
            const int x = std::abs(static_cast<int>(a[i + c]) - static_cast<int>(b[i + c]));
            v.push_back(x);
            if (!x) ++eq;
        }
    d.n = v.size();
    d.equalShare = static_cast<double>(eq) / static_cast<double>(v.size());
    std::sort(v.begin(), v.end());
    d.median = v[v.size() / 2];
    d.p95 = v[std::min(v.size() - 1, static_cast<size_t>(v.size() * 0.95))];
    d.maxAbs = v.back();
    return d;
}

double blockEqualShare(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if (a.size() != b.size() || a.empty()) return 0;
    size_t eq = 0;
    for (size_t i = 0; i < a.size(); i += 8) eq += std::memcmp(a.data() + i, b.data() + i, 8) == 0;
    return static_cast<double>(eq) / static_cast<double>(a.size() / 8);
}

void writeDiffBmp(const std::string& path, const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, uint32_t w, uint32_t h) {
    // 24-bit BMP of |a - b| per channel, times 16 (clamped); a and b are RGB8 of w x h.
    const uint32_t row = (w * 3 + 3) & ~3u;
    std::vector<uint8_t> f(54 + static_cast<size_t>(row) * h, 0);
    auto put = [&](size_t at, uint32_t v) { std::memcpy(f.data() + at, &v, 4); };
    f[0] = 'B'; f[1] = 'M';
    put(2, static_cast<uint32_t>(f.size())); put(10, 54); put(14, 40); put(18, w); put(22, h);
    f[26] = 1; f[28] = 24; put(34, static_cast<uint32_t>(row) * h);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
            for (int c = 0; c < 3; ++c) {
                const size_t i = (static_cast<size_t>(y) * w + x) * 3 + c;
                const int dv = std::min(255, 16 * std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])));
                f[54 + static_cast<size_t>(h - 1 - y) * row + x * 3 + (2 - c)] = static_cast<uint8_t>(dv);
            }
    CreateDirectoryA((test::workDir() + "/bakepost").c_str(), nullptr);
    FILE* o = std::fopen(path.c_str(), "wb");
    if (o) { std::fwrite(f.data(), 1, f.size(), o); std::fclose(o); }
}

float f16(uint16_t h) { return DirectX::PackedVector::XMConvertHalfToFloat(h); }

void reg(const std::vector<uint8_t>& cb, uint32_t r, float out[4]) { std::memcpy(out, cb.data() + r * 16, 16); }

// Tile P's inputs from the reference folder: normals, origin and constants read from the EXE's own buffers.
struct TileP {
    std::vector<float> normals;
    int originX = 0, originY = 0;
    PostConstants k;
};

bool loadTileP(TileP& t) {
    std::vector<uint8_t> g24, g48, frame, own, ncb;
    if (!need("P_grid24.bin", g24) || !need("P_grid48.bin", g48) || !need("P_frame.bin", frame) || !need("P_own.bin", own) ||
        !need("P_normcb.bin", ncb))
        return false;
    t.normals.resize(67 * 67 * 3);
    for (size_t i = 0; i < 67 * 67; ++i) {
        CacheVertex v;
        std::memcpy(&v, g24.data() + i * 24, 24);
        for (int c = 0; c < 3; ++c) t.normals[i * 3 + c] = f16(v.normal[c]);
    }
    BlendVertex v0;
    std::memcpy(&v0, g48.data(), sizeof v0);
    t.originX = static_cast<int>(v0.sample[0]) + 1;
    t.originY = static_cast<int>(v0.sample[1]) + 1;
    float r[4];
    reg(frame, 21, r); std::memcpy(t.k.halfVector, r, 12);
    reg(frame, 17, r); std::memcpy(t.k.shadowDir, r, 12);
    reg(frame, 16, r); std::memcpy(t.k.hexScale, r, 12);
    reg(frame, 22, r); std::memcpy(t.k.heightTexelScale, r, 8);
    reg(own, 73, r); std::memcpy(t.k.heightOffset, r, 8);
    reg(own, 75, r); t.k.shadowDraws = static_cast<uint32_t>(std::lround(1.0 / r[2]));
    reg(ncb, 0, r); std::memcpy(t.k.normalStep, r, 8);
    reg(ncb, 1, r); t.k.normalStrength = r[0];
    return true;
}

// Inputs of tile P on the device. blendMip0Only: upload only mip 0 of the blend targets and let BakePost make the mips.
struct Inputs {
    ComPtr<ID3D11Resource> blend[3], height, sun, shadow, lut;
    ComPtr<ID3D11ShaderResourceView> blendSrv[3], heightSrv, sunSrv, shadowSrv, lutSrv;
    TileP tile;
    bool load(Dev& d, bool blendMip0Only) {
        const char* names[3] = {"P_albedo", "P_normal", "P_spec"};
        for (int i = 0; i < 3; ++i) {
            RefTex r;
            if (!loadRef(names[i], r)) return false;
            r.format = DXGI_FORMAT_R16G16B16A16_TYPELESS;
            if (!makeTex(d, r, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_GENERATE_MIPS,
                         DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_SRV_DIMENSION_TEXTURE2D, blend[i], blendSrv[i], !blendMip0Only))
                return false;
        }
        RefTex h, s, sh, l;
        if (!loadRef("height", h) || !loadRef("sun", s) || !loadRef("shadowlight", sh) || !loadRef("lut_terrain", l)) return false;
        return makeTex(d, h, D3D11_BIND_SHADER_RESOURCE, 0, DXGI_FORMAT_A8_UNORM, D3D11_SRV_DIMENSION_TEXTURE2D, height, heightSrv) &&
               makeTex(d, s, D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_TEXTURECUBE, DXGI_FORMAT_BC1_UNORM_SRGB,
                       D3D11_SRV_DIMENSION_TEXTURECUBE, sun, sunSrv) &&
               makeTex(d, sh, D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_TEXTURECUBE, DXGI_FORMAT_BC1_UNORM_SRGB,
                       D3D11_SRV_DIMENSION_TEXTURECUBE, shadow, shadowSrv) &&
               makeTex(d, l, D3D11_BIND_SHADER_RESOURCE, 0, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_SRV_DIMENSION_TEXTURE3D, lut, lutSrv) &&
               loadTileP(tile);
    }
    PostInputs post(bool mipsReady) const {
        PostInputs in;
        for (int i = 0; i < 3; ++i) in.blend[i] = static_cast<ID3D11Texture2D*>(blend[i].Get());
        in.blendMipsReady = mipsReady;
        in.heightPage = heightSrv.Get();
        in.originX = tile.originX;
        in.originY = tile.originY;
        in.gridNormals = tile.normals.data();
        in.gridNormalCount = 67 * 67;
        in.sunCube = sunSrv.Get();
        in.shadowCube = shadowSrv.Get();
        in.colorKey = lutSrv.Get();
        in.sizeClass = 256;
        in.needSecond = g_needSecond;
        in.k = tile.k;
        return in;
    }
};

bool openExe(shaders::ShaderSection& sec) {
    const std::string exe = test::exePath();
    if (exe.empty()) {
        test::fail(__FILE__, __LINE__, "C5R_EXE is not set (the EXE's shaders are needed)");
        return false;
    }
    Status st = sec.open(wide(exe));
    if (!st.ok()) {
        test::fail(__FILE__, __LINE__, "cannot open the EXE " + exe + ": " + st.str());
        return false;
    }
    return true;
}

void report(const char* what, const Diff& d) {
    std::printf("  %-34s equal %.4f%%  median %.0f  p95 %.0f  max %.0f  (n %zu)\n", what, 100.0 * d.equalShare, d.median, d.p95,
                d.maxAbs, d.n);
}

}  // namespace

// ---------------------------------------------------------------- no input needed

C5R_TEST(cache_mips_by_size_class) {
    CHECK_EQ(cacheMips(64), 4u);
    CHECK_EQ(cacheMips(128), 5u);
    CHECK_EQ(cacheMips(256), 6u);
    CHECK_EQ(cacheMips(512), 7u);
    CHECK_EQ(cacheMips(0), 0u);
    CHECK_EQ(cacheMips(32), 0u);
    CHECK_EQ(cacheMips(1024), 0u);
    CHECK_EQ(cacheMips(255), 0u);
}

// Issue 0332: the near pair's sizes, and createNearCache's checks.
C5R_TEST(near_cache_mips_bytes_and_create) {
    CHECK_EQ(nearCacheMips(64), 7u);
    CHECK_EQ(nearCacheMips(128), 8u);
    CHECK_EQ(nearCacheMips(256), 9u);
    CHECK_EQ(nearCacheMips(512), 10u);
    CHECK_EQ(nearCacheMips(0), 0u);
    CHECK_EQ(nearCacheMips(1024), 0u);
    CHECK_EQ(nearCacheMips(300), 0u);
    CHECK_EQ(nearCacheBytes(256), uint64_t(349524));    // terrain-bake-records.md 3.1: a 256 pair 699,048 B
    CHECK_EQ(nearCacheBytes(512), uint64_t(1398100));   // a 512 pair 2,796,200 B
    CHECK_EQ(nearCacheBytes(64), uint64_t(21844));
    CHECK_EQ(nearCacheBytes(100), uint64_t(0));
    ID3D11Texture2D* t = reinterpret_cast<ID3D11Texture2D*>(1);
    CHECK_ERR(createNearCache(nullptr, 256, &t), Err::BadLayout);
    CHECK(t == nullptr);
    CHECK_ERR(createNearCache(nullptr, 256, nullptr), Err::BadLayout);
    Dev d;
    CHECK(d.create(true));
    t = reinterpret_cast<ID3D11Texture2D*>(1);
    CHECK_ERR(createNearCache(d.dev.Get(), 1024, &t), Err::BadLayout);
    CHECK(t == nullptr);
    CHECK_ERR(createNearCache(d.dev.Get(), 0, &t), Err::BadLayout);
    for (uint32_t s : {64u, 128u, 256u, 512u}) {
        CHECK_OK(createNearCache(d.dev.Get(), s, &t));
        CHECK(t != nullptr);
        if (!t) continue;
        D3D11_TEXTURE2D_DESC td;
        t->GetDesc(&td);
        CHECK_EQ(td.Width, s);
        CHECK_EQ(td.Height, s);
        CHECK_EQ(td.MipLevels, nearCacheMips(s));
        CHECK_EQ(static_cast<int>(td.Format), static_cast<int>(DXGI_FORMAT_R8G8B8A8_UNORM));
        CHECK(td.MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS);
        t->Release();
        t = nullptr;
    }
}

C5R_TEST(negative_init_without_device) {
    BakePost b;
    shaders::ShaderSection sec;
    CHECK_ERR(b.init(nullptr, sec), Err::BadLayout);
    CHECK(b.target(PostTarget::Normal) == nullptr);
}

C5R_TEST(negative_run_before_init) {
    BakePost b;
    PostInputs in;
    PostOutputs out;
    out.t0 = reinterpret_cast<ID3D11Texture2D*>(1);
    CHECK_ERR(b.run(nullptr, in, out), Err::NotFound);
    CHECK(out.t0 == nullptr && out.t8 == nullptr);
}

// Synthetic EXEs: a PE whose .shader section holds shaders compiled here (no game bytes).
namespace {
struct FakeExe {
    shaders::ShaderSection sec;
    uint32_t vaVs = 0, vaPs = 0;
    bool build(const char* vsSrc, const char* psSrc) {
        std::vector<uint8_t> vs = test::stripLikeExe(test::compileHlsl(vsSrc, "main", "vs_5_0"));
        std::vector<uint8_t> ps = test::stripLikeExe(test::compileHlsl(psSrc, "main", "ps_5_0"));
        if (vs.empty() || ps.empty()) return false;
        std::vector<uint8_t> data = vs;
        if (data.size() % 8) data.resize(data.size() + 4, 0);  // the EXE's 4-byte zero gap before an 8-aligned blob
        const uint32_t psOff = static_cast<uint32_t>(data.size());
        data.insert(data.end(), ps.begin(), ps.end());
        std::vector<uint8_t> pe = test::buildPe({{".text", 0x1000, 0x10, std::vector<uint8_t>(0x10, 0xC3)},
                                                 {".shader", 0x2000, static_cast<uint32_t>(data.size()), data}});
        if (!sec.parse(pe).ok()) return false;
        vaVs = 0x400000 + 0x2000;
        vaPs = vaVs + psOff;
        return sec.findByVa(vaVs) && sec.findByVa(vaPs);
    }
    PostShaderVas vas() const {
        PostShaderVas v;
        v.normalVs = v.shadowVs = v.cacheVs = v.compressVs = vaVs;
        v.normalPs = v.shadowPs = v.cachePs1 = v.cachePs2 = v.compressPs = vaPs;
        return v;
    }
};
const char* kVs = "float4 main(float3 p : POSITION, float3 n : NORMAL, float2 t : TEXCOORD0) : SV_Position { return float4(p + n * t.x, 1); }";
}  // namespace

C5R_TEST(negative_init_missing_shader_is_not_found) {
    Dev d;
    CHECK(d.create(true));
    FakeExe fx;
    CHECK(fx.build(kVs, "float4 main() : SV_Target { return 1; }"));
    BakePost b;
    CHECK_ERR(b.init(d.dev.Get(), fx.sec), Err::NotFound);   // the default VAs are not in the fake section
    CHECK(b.target(PostTarget::Normal) == nullptr);
    PostShaderVas v = fx.vas();
    v.normalVs = 0x00500000;                                  // one VA missing (the first one looked up)
    CHECK_ERR(b.init(d.dev.Get(), fx.sec, v), Err::NotFound);
    CHECK(b.target(PostTarget::Normal) == nullptr);
}

C5R_TEST(negative_init_wrong_stage_or_reflection_is_bad_layout) {
    Dev d;
    CHECK(d.create(true));
    FakeExe fx;
    // A PS with the NormalCalculate interface except that t0 is a cube: the reflection check refuses it.
    CHECK(fx.build(kVs, "cbuffer B : register(b1) { float4 r[2]; }; TextureCube t0 : register(t0); SamplerState s0 : register(s0);"
                        "float4 main(float4 p : SV_Position) : SV_Target { return t0.Sample(s0, r[0].xyz) * r[1].x; }"));
    BakePost b;
    PostShaderVas v = fx.vas();
    CHECK_ERR(b.init(d.dev.Get(), fx.sec, v), Err::BadLayout);
    v.normalVs = fx.vaPs;                                      // a PS where the VS belongs
    CHECK_ERR(b.init(d.dev.Get(), fx.sec, v), Err::BadLayout);
    FakeExe fx2;                                               // right texture, but a constant buffer slot nobody binds
    CHECK(fx2.build(kVs, "cbuffer B : register(b3) { float4 r; }; Texture2D t0 : register(t0); SamplerState s0 : register(s0);"
                         "float4 main(float4 p : SV_Position) : SV_Target { return t0.Sample(s0, r.xy); }"));
    CHECK_ERR(b.init(d.dev.Get(), fx2.sec, fx2.vas()), Err::BadLayout);
    FakeExe fx3;                                               // a VS input the pass does not provide
    CHECK(fx3.build("float4 main(float3 p : POSITION, float4 c : COLOR0) : SV_Position { return float4(p, 1) + c; }",
                    "cbuffer B : register(b1) { float4 r[2]; }; Texture2D t0 : register(t0); SamplerState s0 : register(s0);"
                    "float4 main(float4 p : SV_Position) : SV_Target { return t0.Sample(s0, r[0].xy) * r[1].x; }"));
    CHECK_ERR(b.init(d.dev.Get(), fx3.sec, fx3.vas()), Err::BadLayout);
    CHECK(b.target(PostTarget::Shadow) == nullptr);
}

// ---------------------------------------------------------------- the EXE's shaders (C5R_EXE)

C5R_TEST(game_init_and_input_validation) {
    shaders::ShaderSection sec;
    if (!openExe(sec)) return;
    Dev d;
    CHECK(d.create(true));
    BakePost b;
    CHECK_OK(b.init(d.dev.Get(), sec));
    CHECK(b.target(PostTarget::Normal) != nullptr);
    CHECK(b.target(PostTarget::Cache0) == nullptr);   // made on the first run, per size class
    // Valid-looking inputs made here (no game data), then one fault at a time.
    auto tex2d = [&](uint32_t size, uint32_t mips, DXGI_FORMAT f, UINT bind, UINT misc) {
        D3D11_TEXTURE2D_DESC t = {};
        t.Width = t.Height = size; t.MipLevels = mips; t.ArraySize = 1; t.Format = f; t.SampleDesc.Count = 1;
        t.Usage = D3D11_USAGE_DEFAULT; t.BindFlags = bind; t.MiscFlags = misc;
        ComPtr<ID3D11Texture2D> x;
        CHECK(SUCCEEDED(d.dev->CreateTexture2D(&t, nullptr, &x)));
        return x;
    };
    const UINT rtsr = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> good = tex2d(512, 10, DXGI_FORMAT_R16G16B16A16_FLOAT, rtsr, D3D11_RESOURCE_MISC_GENERATE_MIPS);
    ComPtr<ID3D11Texture2D> wrongFmt = tex2d(512, 10, DXGI_FORMAT_R8G8B8A8_UNORM, rtsr, D3D11_RESOURCE_MISC_GENERATE_MIPS);
    ComPtr<ID3D11Texture2D> wrongSize = tex2d(256, 9, DXGI_FORMAT_R16G16B16A16_FLOAT, rtsr, D3D11_RESOURCE_MISC_GENERATE_MIPS);
    ComPtr<ID3D11Texture2D> oneMip = tex2d(512, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, rtsr, 0);
    ComPtr<ID3D11Texture2D> noRt = tex2d(512, 10, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE, 0);
    ComPtr<ID3D11Texture2D> h = tex2d(64, 1, DXGI_FORMAT_A8_UNORM, D3D11_BIND_SHADER_RESOURCE, 0);
    ComPtr<ID3D11ShaderResourceView> hv, cubeV, lutV;
    CHECK(SUCCEEDED(d.dev->CreateShaderResourceView(h.Get(), nullptr, &hv)));
    D3D11_TEXTURE2D_DESC cd = {};
    cd.Width = cd.Height = 8; cd.MipLevels = 1; cd.ArraySize = 6; cd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; cd.SampleDesc.Count = 1;
    cd.BindFlags = D3D11_BIND_SHADER_RESOURCE; cd.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
    ComPtr<ID3D11Texture2D> cube;
    CHECK(SUCCEEDED(d.dev->CreateTexture2D(&cd, nullptr, &cube)));
    CHECK(SUCCEEDED(d.dev->CreateShaderResourceView(cube.Get(), nullptr, &cubeV)));
    D3D11_TEXTURE3D_DESC ld = {4, 4, 4, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
    ComPtr<ID3D11Texture3D> lut;
    CHECK(SUCCEEDED(d.dev->CreateTexture3D(&ld, nullptr, &lut)));
    CHECK(SUCCEEDED(d.dev->CreateShaderResourceView(lut.Get(), nullptr, &lutV)));
    std::vector<float> normals(67 * 67 * 3, 0.0f);
    for (size_t i = 0; i < 67 * 67; ++i) normals[i * 3 + 2] = 1.0f;
    PostInputs base;
    for (int i = 0; i < 3; ++i) base.blend[i] = good.Get();
    base.heightPage = hv.Get();
    base.sunCube = base.shadowCube = cubeV.Get();
    base.colorKey = lutV.Get();
    base.gridNormals = normals.data();
    base.gridNormalCount = 67 * 67;
    auto expectFail = [&](PostInputs in, Err want, const char* what) {
        PostOutputs out;
        Status st = b.run(d.ctx.Get(), in, out);
        if (st.code != want) test::fail(__FILE__, __LINE__, std::string(what) + ": want " + errName(want) + ", got " + st.str());
        CHECK(out.t0 == nullptr && out.t8 == nullptr);
    };
    PostInputs in = base; in.blend[1] = wrongFmt.Get(); expectFail(in, Err::BadLayout, "blend format");
    in = base; in.blend[0] = wrongSize.Get(); expectFail(in, Err::BadLayout, "blend size");
    in = base; in.blend[2] = oneMip.Get(); expectFail(in, Err::BadLayout, "blend mips");
    in = base; in.blend[2] = noRt.Get(); expectFail(in, Err::BadLayout, "blend bind flags");
    in = base; in.blend[0] = nullptr; expectFail(in, Err::BadLayout, "blend missing");
    in = base; in.heightPage = nullptr; expectFail(in, Err::BadLayout, "height page missing");
    in = base; in.heightPage = cubeV.Get(); expectFail(in, Err::BadLayout, "height page not 2D");
    in = base; in.sunCube = hv.Get(); expectFail(in, Err::BadLayout, "sun not a cube");
    in = base; in.colorKey = nullptr; expectFail(in, Err::BadLayout, "LUT missing");
    in = base; in.colorKey = hv.Get(); expectFail(in, Err::BadLayout, "LUT not 3D");
    in = base; in.gridNormalCount = 66 * 66; expectFail(in, Err::BadLayout, "normal count");
    in = base; in.gridW = 66; expectFail(in, Err::BadLayout, "normal count for a 66 x 67 grid");  // 3404
    in = base; in.gridW = 68; expectFail(in, Err::BadLayout, "grid wider than 67");
    in = base; in.gridH = 1; expectFail(in, Err::BadLayout, "grid of one row");
    in = base; in.gridNormals = nullptr; expectFail(in, Err::BadLayout, "normals missing");
    in = base; in.sizeClass = 100; expectFail(in, Err::BadLayout, "size class 100");
    in = base; in.sizeClass = 1024; expectFail(in, Err::BadLayout, "size class 1024");
    in = base; in.k.shadowDraws = 0; expectFail(in, Err::BadLayout, "shadow draws 0");
    // decals (epic 0166, 4104): a non-empty list without a decal pass is refused
    decal::DecalSet dset;
    dset.decals.resize(1);
    decal::TileDecalList dlist;
    dlist.set = &dset;
    dlist.indices.push_back(0);
    in = base; in.decals = &dlist; expectFail(in, Err::BadLayout, "decals without a decal pass");
    // the near pair (issue 0332): each fault refused before anything is drawn
    ComPtr<ID3D11Texture2D> n256a, n256b, n512, nShort, nFmt, nNoMips;
    CHECK_OK(createNearCache(d.dev.Get(), 256, &n256a));
    CHECK_OK(createNearCache(d.dev.Get(), 256, &n256b));
    CHECK_OK(createNearCache(d.dev.Get(), 512, &n512));
    nShort = tex2d(256, 6, DXGI_FORMAT_R8G8B8A8_UNORM, rtsr, D3D11_RESOURCE_MISC_GENERATE_MIPS);   // the BC1 path's 6 mips
    nFmt = tex2d(256, 9, DXGI_FORMAT_B8G8R8A8_UNORM, rtsr, D3D11_RESOURCE_MISC_GENERATE_MIPS);
    nNoMips = tex2d(256, 9, DXGI_FORMAT_R8G8B8A8_UNORM, rtsr, 0);
    in = base; in.nearCache[1] = n256b.Get(); expectFail(in, Err::BadLayout, "near t8 without t0");
    in = base; in.nearCache[0] = n256a.Get(); expectFail(in, Err::BadLayout, "near t8 missing with needSecond");
    in = base; in.needSecond = false; in.nearCache[0] = n256a.Get(); in.nearCache[1] = n256b.Get();
    expectFail(in, Err::BadLayout, "near t8 for a tile without t8");
    in = base; in.nearCache[0] = n256a.Get(); in.nearCache[1] = n512.Get(); expectFail(in, Err::BadLayout, "near sizes differ");
    in = base; in.nearCache[0] = n256a.Get(); in.nearCache[1] = n256a.Get(); expectFail(in, Err::BadLayout, "near t0 = t8");
    in = base; in.nearCache[0] = nShort.Get(); in.nearCache[1] = n256b.Get(); expectFail(in, Err::BadLayout, "near not the full chain");
    in = base; in.nearCache[0] = nFmt.Get(); in.nearCache[1] = n256b.Get(); expectFail(in, Err::BadLayout, "near format");
    in = base; in.nearCache[0] = n256a.Get(); in.nearCache[1] = nNoMips.Get(); expectFail(in, Err::BadLayout, "near without GENERATE_MIPS");
    in = base; in.nearCache[0] = wrongSize.Get(); in.needSecond = false; expectFail(in, Err::BadLayout, "near float format");
    // the given macro shadow (issue 0371, tests and replays): 512 R8G8B8A8 only
    {
        ComPtr<ID3D11Texture2D> s256 = tex2d(256, 1, DXGI_FORMAT_R8G8B8A8_UNORM, rtsr, 0);
        ComPtr<ID3D11Texture2D> sFmt = tex2d(512, 1, DXGI_FORMAT_B8G8R8A8_UNORM, rtsr, 0);
        ComPtr<ID3D11Texture2D> sOk = tex2d(512, 1, DXGI_FORMAT_R8G8B8A8_UNORM, rtsr, 0);
        in = base; in.macroShadowGiven = s256.Get(); expectFail(in, Err::BadLayout, "given macro shadow of 256");
        in = base; in.macroShadowGiven = sFmt.Get(); expectFail(in, Err::BadLayout, "given macro shadow BGRA");
        in = base; in.macroShadowGiven = sOk.Get();
        PostOutputs o;
        CHECK_OK(b.run(d.ctx.Get(), in, o));
        CHECK(o.t0 != nullptr);
        if (o.t0) o.t0->Release();
        if (o.t8) o.t8->Release();
    }
    // valid near pairs: 256 and 512 with t8, 512 alone without
    for (int c = 0; c < 3; ++c) {
        in = base;
        in.nearCache[0] = c == 0 ? n256a.Get() : n512.Get();
        in.nearCache[1] = c == 0 ? n256b.Get() : nullptr;
        in.needSecond = c == 0;
        ComPtr<ID3D11Texture2D> n512b;
        if (c == 1) {
            CHECK_OK(createNearCache(d.dev.Get(), 512, &n512b));
            in.nearCache[1] = n512b.Get();
            in.needSecond = true;
        }
        PostOutputs o;
        CHECK_OK(b.run(d.ctx.Get(), in, o));
        CHECK(o.t0 != nullptr);
        CHECK((o.t8 != nullptr) == in.needSecond);
        if (o.t0) o.t0->Release();
        if (o.t8) o.t8->Release();
    }
    PostOutputs out;
    CHECK_ERR(b.run(nullptr, base, out), Err::BadLayout);
    // and the valid inputs run, at every size class
    for (uint32_t sc : {64u, 128u, 256u, 512u}) {
        in = base;
        in.sizeClass = sc;
        in.needSecond = sc != 128;
        PostOutputs o;
        CHECK_OK(b.run(d.ctx.Get(), in, o));
        CHECK(o.t0 != nullptr);
        CHECK((o.t8 != nullptr) == in.needSecond);
        if (o.t0) {
            D3D11_TEXTURE2D_DESC t;
            o.t0->GetDesc(&t);
            CHECK_EQ(t.Width, sc);
            CHECK_EQ(t.MipLevels, cacheMips(sc));
            CHECK_EQ(static_cast<int>(t.Format), static_cast<int>(DXGI_FORMAT_BC1_TYPELESS));
            o.t0->Release();
        }
        if (o.t8) o.t8->Release();
    }
    // an edge tile (issue 3404): the last column and row have extent 64, so a 66 x 66 grid (25,350 indices)
    in = base;
    in.gridW = in.gridH = 66;
    in.gridNormalCount = 66 * 66;
    PostOutputs eo;
    CHECK_OK(b.run(d.ctx.Get(), in, eo));
    CHECK(eo.t0 != nullptr);
    if (eo.t0) eo.t0->Release();
    if (eo.t8) eo.t8->Release();
}

// The generalised grids (issue 3404): (ex + 2) x (ey + 2) vertices, the 67 grid unchanged.
C5R_TEST(edge_tile_grids) {
    std::vector<uint16_t> g67, g66, g6667;
    buildGridIndices(67, 67, g67);
    buildGridIndices(66, 66, g66);
    buildGridIndices(66, 67, g6667);
    CHECK_EQ(g67.size(), size_t(26136));
    CHECK_EQ(g66.size(), size_t(25350));
    CHECK_EQ(g6667.size(), size_t(65 * 66 * 6));
    std::vector<uint16_t> fixed(66 * 66 * 6);
    buildGridIndices(fixed.data());
    CHECK(fixed == g67);  // the 67 form is the same as before
    uint16_t mx = 0;
    for (uint16_t v : g66) mx = std::max(mx, v);
    CHECK_EQ(mx, uint16_t(66 * 66 - 1));
    std::vector<BlendVertex> s;
    buildShadowGrid(64, 128, 66, 66, s);
    CHECK_EQ(s.size(), size_t(66 * 66));
    CHECK(s.front().pos[0] == -1.0f && s.back().pos[0] == 1.0f && s.front().pos[1] == 1.0f && s.back().pos[1] == -1.0f);
    CHECK(s.back().sample[0] == 64.0f - 1 + 65 && s.back().sample[1] == 128.0f - 1 + 65);
    std::vector<float> n(66 * 66 * 3, 0.0f);
    std::vector<CacheVertex> c;
    buildCacheGrid(n.data(), 256, 66, 66, c);
    CHECK_EQ(c.size(), size_t(66 * 66));
    CHECK_EQ(c.back().uv[0], uint16_t(0x3C00));  // 1.0 as a half: the last vertex at u = 65 / 65
    CHECK_EQ(c[1].uv[0], DirectX::PackedVector::XMConvertFloatToHalf(1.0f / 65.0f));
    // the 67 builders give what the fixed-size ones give
    std::vector<BlendVertex> s67;
    buildShadowGrid(10, 20, 67, 67, s67);
    std::vector<BlendVertex> f67(67 * 67);
    buildShadowGrid(10, 20, f67.data());
    CHECK(std::memcmp(s67.data(), f67.data(), sizeof(BlendVertex) * f67.size()) == 0);
}

// ---------------------------------------------------------------- against the capture (C5R_BAKE_REF)

C5R_TEST(game_grids_and_constants_match_the_exe) {
    std::vector<uint8_t> ib, g48, g24, cg48, cib, cframe, cown0, cncb;
    if (!need("P_ib.bin", ib) || !need("P_grid48.bin", g48) || !need("P_grid24.bin", g24) || !need("C_grid48.bin", cg48) ||
        !need("C_ib.bin", cib) || !need("C_frame.bin", cframe) || !need("C_shadow0.bin", cown0) || !need("C_normcb.bin", cncb))
        return;
    std::vector<uint16_t> mine(66 * 66 * 6);
    buildGridIndices(mine.data());
    CHECK(std::memcmp(mine.data(), ib.data(), mine.size() * 2) == 0);
    CHECK(std::memcmp(mine.data(), cib.data(), mine.size() * 2) == 0);
    for (const std::vector<uint8_t>* g : {&g48, &cg48}) {
        BlendVertex first;
        std::memcpy(&first, g->data(), sizeof first);
        std::vector<BlendVertex> sg(67 * 67);
        buildShadowGrid(static_cast<int>(first.sample[0]) + 1, static_cast<int>(first.sample[1]) + 1, sg.data());
        size_t bad = 0;
        for (size_t i = 0; i < sg.size(); ++i) {
            BlendVertex e;
            std::memcpy(&e, g->data() + i * sizeof e, sizeof e);
            const bool diff = std::memcmp(e.pos, sg[i].pos, 12) != 0 || std::memcmp(e.sample, sg[i].sample, 8) != 0;
            if (diff && !bad)
                std::printf("  first grid difference at vertex %zu: EXE (%.9g %.9g %.9g | %.9g %.9g), built (%.9g %.9g %.9g | %.9g %.9g)\n",
                            i, e.pos[0], e.pos[1], e.pos[2], e.sample[0], e.sample[1], sg[i].pos[0], sg[i].pos[1], sg[i].pos[2],
                            sg[i].sample[0], sg[i].sample[1]);
            bad += diff;
        }
        CHECK_EQ(bad, static_cast<size_t>(0));
    }
    TileP t;
    if (!loadTileP(t)) return;
    std::vector<CacheVertex> cg(67 * 67);
    buildCacheGrid(t.normals.data(), 256, cg.data());
    size_t badUv = 0, badN = 0;
    double posErr = 0;
    for (size_t i = 0; i < cg.size(); ++i) {
        CacheVertex e;
        std::memcpy(&e, g24.data() + i * 24, 24);
        badUv += std::memcmp(e.uv, cg[i].uv, 4) != 0;
        badN += std::memcmp(e.normal, cg[i].normal, 8) != 0;
        for (int c = 0; c < 3; ++c) posErr = std::max(posErr, static_cast<double>(std::fabs(e.pos[c] - cg[i].pos[c])));
    }
    CHECK_EQ(badUv, static_cast<size_t>(0));
    CHECK_EQ(badN, static_cast<size_t>(0));
    CHECK(posErr < 1e-6);
    // The defaults of PostConstants are the EXE's values at the load screen (both tiles).
    PostConstants def;
    CHECK(std::memcmp(def.halfVector, t.k.halfVector, 12) == 0 || std::fabs(def.halfVector[1] - t.k.halfVector[1]) < 1e-5f);
    float r[4];
    reg(cframe, 17, r);
    CHECK(std::fabs(r[0] - def.shadowDir[0]) < 1e-5f && std::fabs(r[1] - def.shadowDir[1]) < 1e-5f);
    reg(cown0, 73, r);
    CHECK_EQ(r[0], t.k.heightOffset[0]);
    CHECK_EQ(r[1], t.k.heightOffset[1]);
    reg(cncb, 1, r);
    CHECK_EQ(r[0], def.normalStrength);
    CHECK_EQ(t.k.normalStrength, def.normalStrength);
    CHECK_EQ(t.k.shadowDraws, def.shadowDraws);
    CHECK_EQ(t.k.normalStep[0], def.normalStep[0]);
}

namespace {
// Runs tile P and compares every stage with the EXE's own targets; returns false on a setup failure.
// gameStyle >= 0: the cubes and the ColorKey LUT come from the install (C5R_GAME_DIR) for that style, not the capture.
// decals (epic 0166, 4104): 0 none (stage 1), 1 an empty decal list with a decal pass, 2 one pass-0 and one pass-1 decal
// far off the tile (drawn, clipped away; the blend mips are made again from mip 0).
// nearSize (issue 0332): 0 none; 256 / 512 the EXE's state-5 bake with a near pair of that size, compared after the run:
// figures near<k>_vs_exe_mips0_5 (the near cache's mips 0-5 against the EXE's CacheBake target of the same tile, which is
// CacheBake at 256 and its GenerateMips; only for 256) and near<k>_vs_chain (every mip against the EXE's mip 0 uploaded
// into a full-chain texture and GenerateMips'd on this device; only for 256), near<k>_own_chain (every mip against the
// near cache's own mip 0 GenerateMips'd again: the chain is GenerateMips').
bool bakeTileP(bool blendMipsFromExe, std::map<std::string, double>& figures, std::vector<uint8_t>* blocksOut,
               int gameStyle = -1, const std::vector<float>* normalsOverride = nullptr, int decals = 0, uint32_t nearSize = 0) {
    shaders::ShaderSection sec;
    if (!openExe(sec)) return false;
    Dev d;
    if (!d.create(false)) {
        test::fail(__FILE__, __LINE__, "no hardware D3D11 device (the comparison needs the GPU the capture was taken on)");
        return false;
    }
    Inputs ins;
    if (!ins.load(d, !blendMipsFromExe)) {
        test::fail(__FILE__, __LINE__, "could not load tile P's inputs from " + refDir());
        return false;
    }
    BakePost b;
    Status st = b.init(d.dev.Get(), sec);
    if (!st.ok()) { test::fail(__FILE__, __LINE__, "init: " + st.str()); return false; }
    PostInputs pin = ins.post(blendMipsFromExe);
    if (normalsOverride) pin.gridNormals = normalsOverride->data();
    StyleInputs style;
    if (gameStyle >= 0) {
        gamefs::GameFiles fs;
        st = fs.open(wide(test::gameDir()));
        if (st.ok()) st = loadStyleInputs(d.dev.Get(), fs, static_cast<uint32_t>(gameStyle), style);
        if (!st.ok()) { test::fail(__FILE__, __LINE__, "style inputs from " + test::gameDir() + ": " + st.str()); return false; }
        pin.sunCube = style.sunCube;
        pin.shadowCube = style.shadowCube;
        pin.colorKey = style.colorKey;
        std::printf("style inputs from the install: %s\n", styleName(static_cast<uint32_t>(gameStyle)));
    }
    decal::DecalPass dpass;
    decal::DecalSet dset;
    decal::TileDecalList dlist;
    if (decals) {
        st = dpass.init(d.dev.Get(), sec, nullptr);
        if (!st.ok()) { test::fail(__FILE__, __LINE__, "decal pass init: " + st.str()); return false; }
        dlist.set = &dset;
        if (decals == 2) {
            using DirectX::PackedVector::XMConvertFloatToHalf;
            const uint16_t z = XMConvertFloatToHalf(0.0f), o = XMConvertFloatToHalf(1.0f);
            for (uint32_t pass = 0; pass < 2; ++pass) {
                decal::Decal dc;
                dc.rect[0] = dc.rect[1] = 10000.0f;
                dc.rect[2] = dc.rect[3] = 10064.0f;
                dc.pass = pass;
                dc.firstVertex = static_cast<uint32_t>(dset.vertices.size());
                dc.vertexCount = 6;
                const decal::DecalVertex q[6] = {{z, z, z, z}, {o, z, o, z}, {z, o, z, o}, {z, o, z, o}, {o, z, o, z}, {o, o, o, o}};
                dset.vertices.insert(dset.vertices.end(), q, q + 6);
                dset.decals.push_back(dc);
                dlist.indices.push_back(pass);
            }
        }
        pin.decals = &dlist;
        pin.decalPass = &dpass;
    }
    // issue 0371: the EXE's own macro shadow of the tile (its decals are not in the capture), PostInputs::macroShadowGiven
    ComPtr<ID3D11Resource> givenShadow;
    ComPtr<ID3D11ShaderResourceView> givenShadowSrv;
    if (g_shadowGiven) {
        RefTex r;
        if (!loadRef("P_shadow", r)) return false;
        if (!makeTex(d, r, D3D11_BIND_SHADER_RESOURCE, 0, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_SRV_DIMENSION_TEXTURE2D, givenShadow,
                     givenShadowSrv))
            return false;
        pin.macroShadowGiven = static_cast<ID3D11Texture2D*>(givenShadow.Get());
    }
    ComPtr<ID3D11Texture2D> nearTex[2];
    if (nearSize) {
        for (int k = 0; k < (g_needSecond ? 2 : 1); ++k) {
            st = createNearCache(d.dev.Get(), nearSize, &nearTex[k]);
            if (!st.ok()) { test::fail(__FILE__, __LINE__, "createNearCache: " + st.str()); return false; }
            pin.nearCache[k] = nearTex[k].Get();
        }
    }
    PostOutputs out;
    st = b.run(d.ctx.Get(), pin, out);
    style.release();
    if (decals == 2 && st.ok() && dpass.counters().draws != 2) test::fail(__FILE__, __LINE__, "the off-tile decals were not drawn");
    if (!st.ok()) { test::fail(__FILE__, __LINE__, "run: " + st.str()); return false; }
    ComPtr<ID3D11Texture2D> t0, t8;
    t0.Attach(out.t0);
    t8.Attach(out.t8);
    std::printf("tile P (origin %d, %d), blend mips %s:\n", ins.tile.originX, ins.tile.originY,
                blendMipsFromExe ? "from the EXE" : "generated here");
    if (!blendMipsFromExe) {
        const char* names[3] = {"P_albedo", "P_normal", "P_spec"};
        for (int i = 0; i < 3; ++i) {
            RefTex r;
            if (!loadRef(names[i], r)) return false;
            size_t eq = 0, n = 0;
            for (uint32_t m = 1; m < r.mips; ++m) {
                std::vector<uint8_t> a = readback(d, static_cast<ID3D11Texture2D*>(ins.blend[i].Get()), m), e = tight(r, m);
                n += a.size();
                for (size_t k = 0; k < a.size() && k < e.size(); ++k) eq += a[k] == e[k];
            }
            figures[std::string("blendmips_equal_") + names[i]] = static_cast<double>(eq) / static_cast<double>(n);
            std::printf("  GenerateMips of %-9s mips 1-9 bytes equal %.4f%%\n", names[i], 100.0 * eq / n);
        }
    }
    struct Stage { const char* ref; PostTarget t; uint32_t mips; };
    const Stage stages[] = {{"P_nrm", PostTarget::Normal, 10}, {"P_shadow", PostTarget::Shadow, 10},
                            {"P_cache0", PostTarget::Cache0, 6}, {"P_cache1", PostTarget::Cache1, 6}};
    for (const Stage& s : stages) {
        if (s.t == PostTarget::Cache1 && !g_needSecond) continue;   // 0371: a tile without t8
        RefTex r;
        if (!loadRef(s.ref, r)) return false;
        std::vector<uint8_t> a0 = readback(d, b.target(s.t), 0), e0 = tight(r, 0);
        Diff dm = compareBytes(a0, e0, 4, s.t == PostTarget::Shadow ? 1 : 4);
        report((std::string(s.ref) + " mip 0").c_str(), dm);
        figures[std::string(s.ref) + "_mip0_equal"] = dm.equalShare;
        figures[std::string(s.ref) + "_mip0_max"] = dm.maxAbs;
        std::vector<uint8_t> all, allE;
        for (uint32_t m = 0; m < s.mips; ++m) {
            std::vector<uint8_t> a = readback(d, b.target(s.t), m), e = tight(r, m);
            all.insert(all.end(), a.begin(), a.end());
            allE.insert(allE.end(), e.begin(), e.end());
        }
        Diff da = compareBytes(all, allE, 4, s.t == PostTarget::Shadow ? 1 : 4);
        report((std::string(s.ref) + " all mips").c_str(), da);
        figures[std::string(s.ref) + "_all_equal"] = da.equalShare;
    }
    const char* blockRefs[2] = {"P_blocks0", "P_blocks1"};
    ID3D11Texture2D* outs[2] = {t0.Get(), t8.Get()};
    for (int k = 0; k < (g_needSecond ? 2 : 1); ++k) {
        RefTex r;
        if (!loadRef(blockRefs[k], r)) return false;
        std::vector<uint8_t> mineAll, refAll, decM, decR;
        for (uint32_t m = 0; m < 6; ++m) {
            std::vector<uint8_t> a = readback(d, outs[k], m), e = tight(r, m);
            const double share = blockEqualShare(a, e);
            std::printf("  %s mip %u: BC1 blocks equal %.4f%%\n", k ? "t8" : "t0", m, 100.0 * share);
            mineAll.insert(mineAll.end(), a.begin(), a.end());
            refAll.insert(refAll.end(), e.begin(), e.end());
            const uint32_t w = 256 >> m;
            std::vector<uint8_t> da = decodeBc1(a, w, w), de = decodeBc1(e, w, w);
            decM.insert(decM.end(), da.begin(), da.end());
            decR.insert(decR.end(), de.begin(), de.end());
            if (m == 0)
                writeDiffBmp(test::workDir() + "/bakepost/" + (k ? "t8" : "t0") + (normalsOverride ? "_approxnormals" : "") +
                                 "_mip0_diff_x16.bmp", da, de, w, w);
        }
        const double share = blockEqualShare(mineAll, refAll);
        Diff dd = compareBytes(decM, decR, 1, 1);
        std::printf("  %s all mips: BC1 blocks equal %.4f%%\n", k ? "t8" : "t0", 100.0 * share);
        report(k ? "t8 decoded RGB, all mips" : "t0 decoded RGB, all mips", dd);
        figures[std::string(k ? "t8" : "t0") + "_blocks_equal"] = share;
        figures[std::string(k ? "t8" : "t0") + "_decoded_equal"] = dd.equalShare;
        figures[std::string(k ? "t8" : "t0") + "_decoded_p95"] = dd.p95;
        figures[std::string(k ? "t8" : "t0") + "_decoded_max"] = dd.maxAbs;
        if (blocksOut) blocksOut->insert(blocksOut->end(), mineAll.begin(), mineAll.end());
    }
    // The near pair (issue 0332).
    for (int k = 0; k < (g_needSecond ? 2 : 1) && nearSize; ++k) {
        const std::string tag = std::string("near") + (k ? "1" : "0");
        const uint32_t mips = nearCacheMips(nearSize);
        D3D11_TEXTURE2D_DESC nd;
        nearTex[k]->GetDesc(&nd);
        if (nd.Width != nearSize || nd.MipLevels != mips) { test::fail(__FILE__, __LINE__, tag + ": wrong shape"); return false; }
        // A full-chain texture with the given mip 0, chain by GenerateMips on this device.
        auto chainOf = [&](const std::vector<uint8_t>& mip0, uint32_t size, ComPtr<ID3D11Texture2D>& tex) {
            if (!createNearCache(d.dev.Get(), size, &tex).ok()) return false;
            d.ctx->UpdateSubresource(tex.Get(), 0, nullptr, mip0.data(), size * 4, 0);
            ComPtr<ID3D11ShaderResourceView> v;
            if (FAILED(d.dev->CreateShaderResourceView(tex.Get(), nullptr, &v))) return false;
            d.ctx->GenerateMips(v.Get());
            return true;
        };
        ComPtr<ID3D11Texture2D> own;
        if (!chainOf(readback(d, nearTex[k].Get(), 0), nearSize, own)) { test::fail(__FILE__, __LINE__, "own chain"); return false; }
        size_t eqOwn = 0, nOwn = 0;
        for (uint32_t m = 0; m < mips; ++m) {
            std::vector<uint8_t> a = readback(d, nearTex[k].Get(), m), e = readback(d, own.Get(), m);
            nOwn += a.size();
            for (size_t i = 0; i < a.size() && i < e.size(); ++i) eqOwn += a[i] == e[i];
        }
        figures[tag + "_own_chain"] = static_cast<double>(eqOwn) / static_cast<double>(nOwn);
        std::printf("  %s %u (%u mips): equal to GenerateMips of its own mip 0 %.4f%%\n", tag.c_str(), nearSize, mips,
                    100.0 * eqOwn / nOwn);
        // Issue 0371: a folder made from a capture of an up-res frame holds the EXE's own near pair of the tile (P_near0 /
        // P_near1, its initial contents: the state-5 bake of the frame before); every mip against it, mip 0 by itself
        std::vector<uint8_t> probe;
        if (readFile(refDir() + "/P_near" + (k ? "1" : "0") + ".c5rt", probe)) {
            RefTex r;
            if (!loadRef(std::string("P_near") + (k ? "1" : "0"), r)) return false;
            if (r.w != nearSize || r.mips != mips) {
                test::fail(__FILE__, __LINE__, tag + ": the EXE's near cache is " + std::to_string(r.w) + " with " +
                                                   std::to_string(r.mips) + " mips");
                return false;
            }
            std::vector<uint8_t> a0 = readback(d, nearTex[k].Get(), 0), e0 = tight(r, 0);
            Diff d0 = compareBytes(a0, e0, 4, 4);
            report((tag + " mip 0 vs the EXE's near cache").c_str(), d0);
            figures[tag + "_vs_exe_mip0"] = d0.equalShare;
            figures[tag + "_vs_exe_mip0_max"] = d0.maxAbs;
            if (a0.size() == e0.size() && a0.size() == static_cast<size_t>(nearSize) * nearSize * 4) {
                std::vector<uint8_t> a3, e3;
                for (size_t i = 0; i < a0.size(); i += 4) {
                    a3.insert(a3.end(), a0.begin() + i, a0.begin() + i + 3);
                    e3.insert(e3.end(), e0.begin() + i, e0.begin() + i + 3);
                }
                writeDiffBmp(test::workDir() + "/bakepost/upres_" + tag + "_mip0_diff_x16.bmp", a3, e3, nearSize, nearSize);
            }
            std::vector<uint8_t> all, allE;
            for (uint32_t m = 0; m < mips; ++m) {
                std::vector<uint8_t> a = readback(d, nearTex[k].Get(), m), e = tight(r, m);
                all.insert(all.end(), a.begin(), a.end());
                allE.insert(allE.end(), e.begin(), e.end());
            }
            Diff da = compareBytes(all, allE, 4, 4);
            report((tag + " all mips vs the EXE's near cache").c_str(), da);
            figures[tag + "_vs_exe_all"] = da.equalShare;
        }
        if (nearSize != 256) continue;
        RefTex r;
        if (!loadRef(k ? "P_cache1" : "P_cache0", r)) return false;
        std::vector<uint8_t> all, allE;
        for (uint32_t m = 0; m < 6; ++m) {
            std::vector<uint8_t> a = readback(d, nearTex[k].Get(), m), e = tight(r, m);
            all.insert(all.end(), a.begin(), a.end());
            allE.insert(allE.end(), e.begin(), e.end());
        }
        Diff dv = compareBytes(all, allE, 4, 4);
        report((tag + " 256 mips 0-5 vs the EXE's CacheBake").c_str(), dv);
        figures[tag + "_vs_exe_mips0_5"] = dv.equalShare;
        ComPtr<ID3D11Texture2D> exeChain;
        if (!chainOf(tight(r, 0), 256, exeChain)) { test::fail(__FILE__, __LINE__, "EXE chain"); return false; }
        size_t eq = 0, n = 0;
        for (uint32_t m = 0; m < mips; ++m) {
            std::vector<uint8_t> a = readback(d, nearTex[k].Get(), m), e = readback(d, exeChain.Get(), m);
            n += a.size();
            size_t eqm = 0;
            for (size_t i = 0; i < a.size() && i < e.size(); ++i) eqm += a[i] == e[i];
            eq += eqm;
            if (m >= 6) std::printf("  %s mip %u (%u x %u, past the EXE's 6): equal %.4f%%\n", tag.c_str(), m, 256 >> m, 256 >> m,
                                    100.0 * eqm / a.size());
        }
        figures[tag + "_vs_chain"] = static_cast<double>(eq) / static_cast<double>(n);
        std::printf("  %s all 9 mips vs the EXE's mip 0 + GenerateMips: equal %.4f%%\n", tag.c_str(), 100.0 * eq / n);
    }
    return true;
}
}  // namespace

C5R_TEST(game_tile_p_against_the_exe_blend_mips_from_exe) {
    std::map<std::string, double> f;
    std::vector<uint8_t> first, second;
    if (!bakeTileP(true, f, &first)) return;
    // Reproducible: a second bake on a new device gives the same blocks.
    std::map<std::string, double> f2;
    if (!bakeTileP(true, f2, &second)) return;
    CHECK(first == second);
    std::printf("  second run: blocks %s\n", first == second ? "identical" : "DIFFERENT");
    CHECK(f["t0_blocks_equal"] >= C5R_BAKEPOST_MIN_EQUAL);
    CHECK(f["t8_blocks_equal"] >= C5R_BAKEPOST_MIN_EQUAL);
}

// Epic 0166 (4104): with the decal hook given but no decal on the tile, the bake is stage 1's bit for bit.
C5R_TEST(game_tile_p_with_the_decal_hook_and_no_decals_is_stage_1) {
    std::map<std::string, double> f0, f1, f2;
    std::vector<uint8_t> b0, b1, b2;
    if (!bakeTileP(true, f0, &b0)) return;
    if (!bakeTileP(true, f1, &b1, -1, nullptr, 1)) return;
    if (!bakeTileP(true, f2, &b2, -1, nullptr, 2)) return;
    CHECK(b0 == b1);
    CHECK(b0 == b2);
    std::printf("  empty decal list: blocks %s; two off-tile decals: blocks %s\n", b0 == b1 ? "identical" : "DIFFERENT",
                b0 == b2 ? "identical" : "DIFFERENT");
    CHECK(f1["t0_blocks_equal"] >= C5R_BAKEPOST_MIN_EQUAL && f2["t0_blocks_equal"] >= C5R_BAKEPOST_MIN_EQUAL);
    CHECK(f1["t8_blocks_equal"] >= C5R_BAKEPOST_MIN_EQUAL && f2["t8_blocks_equal"] >= C5R_BAKEPOST_MIN_EQUAL);
    CHECK(f2["P_nrm_all_equal"] >= 1.0 && f2["P_shadow_all_equal"] >= 1.0 && f2["P_cache0_all_equal"] >= 1.0);
}

// Issue 0332: the EXE's state-5 bake. With a 256 near pair the near caches' mips 0-5 equal the EXE's own CacheBake
// targets of tile P (CacheBake at 256 + GenerateMips) bit for bit, mips 6-8 continue that chain, and the BC1 path is
// unchanged (t0 / t8 blocks equal the EXE's, and equal the blocks of a bake without the near pair). A 512 pair is made
// with its own chain; it has no EXE reference here (terrain-bake-passes.md 3.5.3: not verified).
C5R_TEST(game_tile_p_near_pair_is_the_state_5_bake) {
    std::map<std::string, double> f0, f256, f512;
    std::vector<uint8_t> b0, b256, b512;
    if (!bakeTileP(true, f0, &b0)) return;
    if (!bakeTileP(true, f256, &b256, -1, nullptr, 0, 256)) return;
    if (!bakeTileP(true, f512, &b512, -1, nullptr, 0, 512)) return;
    CHECK(b0 == b256);
    CHECK(b0 == b512);
    CHECK(f256["t0_blocks_equal"] >= C5R_BAKEPOST_MIN_EQUAL && f256["t8_blocks_equal"] >= C5R_BAKEPOST_MIN_EQUAL);
    CHECK(f256["near0_vs_exe_mips0_5"] >= 1.0);
    CHECK(f256["near1_vs_exe_mips0_5"] >= 1.0);
    CHECK(f256["near0_vs_chain"] >= 1.0);
    CHECK(f256["near1_vs_chain"] >= 1.0);
    CHECK(f256["near0_own_chain"] >= 1.0 && f256["near1_own_chain"] >= 1.0);
    CHECK(f512["near0_own_chain"] >= 1.0 && f512["near1_own_chain"] >= 1.0);
    std::printf("  BC1 blocks with a 256 / 512 near pair: %s / %s to the bake without\n", b0 == b256 ? "identical" : "DIFFERENT",
                b0 == b512 ? "identical" : "DIFFERENT");
}

// Issue 0371 (terrain-bake-passes.md 3.5.4): the EXE's state-5 bake at the near size, from a stock capture of a frame
// that follows an up-res: C5R_BAKE_REF_UPRES = the folder mkref.py --ids made from it (game data on F:, never committed).
// The tile baked just before the frame ("P" there too) left its blend, normal, macro-shadow, CacheBake and block targets
// and its near pair; the server's state-5 bake of those inputs at the pair's size is compared in every mip, mip 0 alone
// as well (the part 0332 could not check), and its BC1 path against the blocks as above.
C5R_TEST(game_upres_tile_near_pair_is_the_exes_in_every_mip) {
    const char* def = "F:/c5-64bit-work/0371/refupres";
    const char* e = std::getenv("C5R_BAKE_REF_UPRES");
    const std::string dir = e && *e ? e : def;
    std::vector<uint8_t> probe;
    const bool second = readFile(dir + "/P_near1.c5rt", probe);
    RefScope scope("C5R_BAKE_REF_UPRES", def, second);
    RefTex n0;
    if (!loadRef("P_near0", n0)) return;
    std::printf("up-res reference %s: near pair %u x %u, %u mips, t8 %s\n", dir.c_str(), n0.w, n0.h, n0.mips, second ? "yes" : "no");
    CHECK(n0.w == 512 || n0.w == 256);
    // The tile's CacheBake grid normals are not in the capture (its dynamic vertex buffer is written again by the frame's
    // own bake): the EXE's rule (5.1) over the save's height field, C5R_UPRES_HF (a .c5hf tap of the same save).
    const char* he = std::getenv("C5R_UPRES_HF");
    const std::string hf = he && *he ? he : "F:/c5-64bit-work/0371/refupres/heightfield.c5hf";
    std::vector<uint8_t> tap, g48;
    if (!readFile(hf, tap)) {
        test::fail(__FILE__, __LINE__, "missing height field " + hf + " (set C5R_UPRES_HF)");
        return;
    }
    if (!need("P_grid48.bin", g48)) return;
    heightfield::HeightGrid grid;
    Status st = heightfield::readTap(tap.data(), tap.size(), grid);
    if (!st.ok()) { test::fail(__FILE__, __LINE__, "height field " + hf + ": " + st.str()); return; }
    BlendVertex v0;
    std::memcpy(&v0, g48.data(), sizeof v0);
    TileId tile;
    tile.x = static_cast<uint32_t>(std::lround(v0.sample[0] + 1)) / 64;
    tile.y = static_cast<uint32_t>(std::lround(v0.sample[1] + 1)) / 64;
    TileWindow w;
    st = tileWindow(grid, tile, w);
    std::vector<float> normals;
    if (st.ok()) st = makeExeGridNormals()->normals(grid, w, normals);
    if (!st.ok() || normals.size() != 67 * 67 * 3) {
        test::fail(__FILE__, __LINE__, "grid normals of tile (" + std::to_string(tile.x) + ", " + std::to_string(tile.y) + "): " +
                                           st.str() + ", " + std::to_string(normals.size()) + " floats");
        return;
    }
    std::printf("tile (%u, %u): grid normals by the EXE rule from %s (%ux%u samples)\n", tile.x, tile.y, hf.c_str(),
                grid.samplesX, grid.samplesY);
    // First without its decals (none in the capture): the macro shadow lacks the tile's tree shadows (pass 3a); a figure,
    // not a check. Then with the EXE's own macro shadow given (PostInputs::macroShadowGiven): every other input is the
    // tile's, checked by the EXE's targets of the same bake (normal target, CacheBake at 256, both BC1 blocks).
    std::map<std::string, double> f0, f;
    if (!bakeTileP(true, f0, nullptr, -1, &normals, 0, n0.w)) return;
    std::printf("  without the tile's decals: macro shadow equal %.4f%%, near0 mip 0 equal %.4f%%\n",
                100 * f0["P_shadow_all_equal"], 100 * f0["near0_vs_exe_mip0"]);
    g_shadowGiven = true;
    const bool ran = bakeTileP(true, f, nullptr, -1, &normals, 0, n0.w);
    g_shadowGiven = false;
    if (!ran) return;
    CHECK(f["P_shadow_all_equal"] >= 1.0);
    CHECK(f["P_nrm_all_equal"] >= 1.0);
    CHECK(f["P_cache0_all_equal"] >= 1.0);
    CHECK(f["near0_vs_exe_mip0"] >= 1.0);
    CHECK(f["near0_vs_exe_all"] >= 1.0);
    if (second) {
        CHECK(f["near1_vs_exe_mip0"] >= 1.0);
        CHECK(f["near1_vs_exe_all"] >= 1.0);
    }
    CHECK(f["t0_blocks_equal"] >= C5R_BAKEPOST_MIN_EQUAL);
    if (second) CHECK(f["t8_blocks_equal"] >= C5R_BAKEPOST_MIN_EQUAL);
}

C5R_TEST(game_tile_p_against_the_exe_blend_mips_generated) {
    std::map<std::string, double> f;
    if (!bakeTileP(false, f, nullptr)) return;
    CHECK(f["t0_blocks_equal"] >= C5R_BAKEPOST_MIN_EQUAL);
    CHECK(f["t8_blocks_equal"] >= C5R_BAKEPOST_MIN_EQUAL);
}

// What the open normal rule costs (terrain-bake-passes.md 5.1): tile P baked with normals from central differences of
// the blend grid's vertex heights instead of the EXE's own normals. A figure for the epic, bounded so that a regression
// in the passes shows.
C5R_TEST(game_tile_p_with_normals_from_vertex_heights) {
    std::vector<uint8_t> g48;
    if (!need("P_grid48.bin", g48)) return;
    std::vector<float> z(67 * 67), n(67 * 67 * 3);
    for (size_t i = 0; i < z.size(); ++i) {
        BlendVertex v;
        std::memcpy(&v, g48.data() + i * sizeof v, sizeof v);
        z[i] = v.world[2];
    }
    for (int j = 0; j < 67; ++j)
        for (int i = 0; i < 67; ++i) {
            const int i0 = std::max(0, i - 1), i1 = std::min(66, i + 1), j0 = std::max(0, j - 1), j1 = std::min(66, j + 1);
            const double gx = (z[j * 67 + i1] - z[j * 67 + i0]) / (i1 - i0);
            const double gy = (z[j1 * 67 + i] - z[j0 * 67 + i]) / ((j1 - j0) * 0.866);
            const double len = std::sqrt(gx * gx + gy * gy + 1.0);
            n[(j * 67 + i) * 3 + 0] = static_cast<float>(-gx / len);
            n[(j * 67 + i) * 3 + 1] = static_cast<float>(-gy / len);
            n[(j * 67 + i) * 3 + 2] = static_cast<float>(1.0 / len);
        }
    std::map<std::string, double> f;
    if (!bakeTileP(true, f, nullptr, -1, &n)) return;
    std::printf("  with approximate normals: t0 blocks equal %.2f%%, decoded p95 %.0f max %.0f; t8 blocks equal %.2f%%, decoded p95 %.0f max %.0f\n",
                100 * f["t0_blocks_equal"], f["t0_decoded_p95"], f["t0_decoded_max"], 100 * f["t8_blocks_equal"],
                f["t8_decoded_p95"], f["t8_decoded_max"]);
    CHECK(f["t0_decoded_p95"] <= C5R_BAKEPOST_APPROX_P95);
    CHECK(f["t8_decoded_p95"] <= C5R_BAKEPOST_APPROX_P95);
}

// ---------------------------------------------------------------- style inputs (game files)

C5R_TEST(style_file_names) {
    CHECK_EQ(sunlightFile(0), std::string("Sunlight_Generic_Terrain.dds"));
    CHECK_EQ(shadowlightFile(0), std::string("Shadowlight_Generic_Terrain.dds"));
    CHECK_EQ(shadowlightFile(1), std::string("Shadowlight_America_Terrain_2.dds"));
    CHECK_EQ(shadowlightFile(2), std::string("Shadowlight_Asia_Terrain_2.dds"));
    CHECK_EQ(shadowlightFile(3), std::string("Shadowlight_Africa_Terrain.dds"));
    CHECK_EQ(shadowlightFile(4), std::string("Shadowlight_Europe_Terrain_2.dds"));
    CHECK_EQ(colorKeyFile(4), std::string("Terrain_Europe_ColorKey.tga"));
    CHECK(styleName(5) == nullptr);
    CHECK(sunlightFile(5).empty() && shadowlightFile(5).empty() && colorKeyFile(5).empty());
}

namespace {
std::vector<uint8_t> synthTga(uint32_t w, uint32_t h, uint8_t type, uint8_t bpp, uint8_t desc, size_t cut = 0) {
    std::vector<uint8_t> t(18, 0);
    t[2] = type; t[12] = w & 255; t[13] = static_cast<uint8_t>(w >> 8); t[14] = h & 255; t[15] = static_cast<uint8_t>(h >> 8);
    t[16] = bpp; t[17] = desc;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            // B, G, R, A = (x & 255, y, x >> 8, 200)
            t.push_back(static_cast<uint8_t>(x & 255)); t.push_back(static_cast<uint8_t>(y));
            t.push_back(static_cast<uint8_t>(x >> 8)); t.push_back(200);
        }
    t.resize(t.size() - cut);
    return t;
}
}  // namespace

C5R_TEST(colorkey_lut_rule_and_negatives) {
    std::vector<uint8_t> lut;
    // bottom-left origin, two rows of 32768: the volume comes from the TOP row = file row 1
    std::vector<uint8_t> t = synthTga(32768, 2, 2, 32, 0x08);
    CHECK_OK(buildColorKeyLut(t.data(), t.size(), lut));
    CHECK_EQ(lut.size(), kColorKeyTexels * 4);
    CHECK(lut[0] == 0 && lut[1] == 1 && lut[2] == 0 && lut[3] == 200);           // texel 0: R = x>>8, G = file row 1, B = x
    CHECK(lut[300 * 4 + 0] == 1 && lut[300 * 4 + 2] == 44 && lut[300 * 4 + 1] == 1);
    t = synthTga(32768, 2, 2, 32, 0x28);                                         // top-left origin: file row 0 first
    CHECK_OK(buildColorKeyLut(t.data(), t.size(), lut));
    CHECK(lut[1] == 0);
    CHECK_ERR(buildColorKeyLut(t.data(), 10, lut), Err::Truncated);
    CHECK(lut.empty());
    t = synthTga(32768, 2, 10, 32, 0x08);
    CHECK_ERR(buildColorKeyLut(t.data(), t.size(), lut), Err::Unsupported);       // RLE
    t = synthTga(32768, 2, 2, 24, 0x08);
    CHECK_ERR(buildColorKeyLut(t.data(), t.size(), lut), Err::Unsupported);       // 24 bpp
    t = synthTga(32768, 2, 2, 32, 0x18);
    CHECK_ERR(buildColorKeyLut(t.data(), t.size(), lut), Err::Unsupported);       // right-to-left
    t = synthTga(100, 100, 2, 32, 0x08);
    CHECK_ERR(buildColorKeyLut(t.data(), t.size(), lut), Err::BadLayout);         // 10,000 < 32^3 pixels
    t = synthTga(32768, 2, 2, 32, 0x08, 4);
    CHECK_ERR(buildColorKeyLut(t.data(), t.size(), lut), Err::Truncated);         // pixel data cut
    CHECK(lut.empty());
    CHECK_ERR(buildColorKeyLut(nullptr, 0, lut), Err::Truncated);
}

C5R_TEST(negative_style_inputs) {
    StyleInputs s;
    gamefs::GameFiles fs;
    CHECK_ERR(loadStyleInputs(nullptr, fs, 0, s), Err::BadLayout);
    Dev d;
    CHECK(d.create(true));
    CHECK_ERR(loadStyleInputs(d.dev.Get(), fs, 5, s), Err::BadLayout);
    // an install without the files (empty Assets/ and Resource/): NotFound, and nothing is left in `s`
    const std::string root = test::workDir() + "/bakepost_empty_install";
    CreateDirectoryA(root.c_str(), nullptr);
    CreateDirectoryA((root + "/Assets").c_str(), nullptr);
    CreateDirectoryA((root + "/Resource").c_str(), nullptr);
    CHECK_OK(fs.open(wide(root)));
    CHECK_ERR(loadStyleInputs(d.dev.Get(), fs, 0, s), Err::NotFound);
    CHECK(s.sunCube == nullptr && s.shadowCube == nullptr && s.colorKey == nullptr);
}

C5R_TEST(game_style_inputs_match_the_capture) {
    const std::string gd = test::gameDir();
    if (gd.empty()) { test::fail(__FILE__, __LINE__, "C5R_GAME_DIR is not set (the style files are read from the install)"); return; }
    gamefs::GameFiles fs;
    CHECK_OK(fs.open(wide(gd)));
    RefTex lut, sun, shadow;
    if (!loadRef("lut_terrain", lut) || !loadRef("sun", sun) || !loadRef("shadowlight", shadow)) return;
    int matchSun = -1, matchShadow = -1, lutMatches = 0;
    for (uint32_t st = 0; st < 5; ++st) {
        std::vector<uint8_t> tga, l;
        CHECK_OK(fs.read(colorKeyFile(st), tga));
        CHECK_OK(buildColorKeyLut(tga.data(), tga.size(), l));
        if (l == lut.subs[0].data) ++lutMatches;
        for (int which = 0; which < 2; ++which) {
            std::vector<uint8_t> bytes;
            CHECK_OK(fs.read(which ? shadowlightFile(st) : sunlightFile(st), bytes));
            gamefs::DdsImage img;
            CHECK_OK(gamefs::parseDds(bytes.data(), bytes.size(), img));
            const RefTex& r = which ? shadow : sun;
            bool same = img.subresources.size() == r.subs.size();
            for (size_t i = 0; same && i < r.subs.size(); ++i)
                same = img.subresources[i].size == r.subs[i].data.size() &&
                       std::memcmp(img.pixels() + img.subresources[i].offset, r.subs[i].data.data(), r.subs[i].data.size()) == 0;
            if (same) {
                std::printf("  capture's %s cube = the install's %s\n", which ? "Shadowlight" : "Sunlight",
                            (which ? shadowlightFile(st) : sunlightFile(st)).c_str());
                if (!which) matchSun = static_cast<int>(st);
                if (which && (matchShadow < 0 || static_cast<int>(st) == matchSun)) matchShadow = static_cast<int>(st);
            }
        }
    }
    // The style whose two files both equal the capture's cubes (several styles can share a file's content).
    int style = -1;
    for (uint32_t st = 0; st < 5 && style < 0; ++st) {
        bool sunOk = false, shadowOk = false;
        for (int which = 0; which < 2; ++which) {
            std::vector<uint8_t> bytes;
            if (!fs.read(which ? shadowlightFile(st) : sunlightFile(st), bytes).ok()) continue;
            gamefs::DdsImage img;
            if (!gamefs::parseDds(bytes.data(), bytes.size(), img).ok()) continue;
            const RefTex& r = which ? shadow : sun;
            bool same = img.subresources.size() == r.subs.size();
            for (size_t i = 0; same && i < r.subs.size(); ++i)
                same = img.subresources[i].size == r.subs[i].data.size() &&
                       std::memcmp(img.pixels() + img.subresources[i].offset, r.subs[i].data.data(), r.subs[i].data.size()) == 0;
            (which ? shadowOk : sunOk) = same;
        }
        if (sunOk && shadowOk) style = static_cast<int>(st);
    }
    std::printf("  style whose Sunlight and Shadowlight both equal the capture's: %s; Terrain ColorKey equal for %d of 5 styles\n",
                style >= 0 ? styleName(static_cast<uint32_t>(style)) : "none", lutMatches);
    CHECK(matchSun >= 0 && matchShadow >= 0);
    CHECK(lutMatches >= 1);
    CHECK(style >= 0);
    if (style < 0) return;
    std::map<std::string, double> f;
    if (!bakeTileP(true, f, nullptr, style)) return;
    CHECK(f["t0_blocks_equal"] >= C5R_BAKEPOST_MIN_EQUAL);
    CHECK(f["t8_blocks_equal"] >= C5R_BAKEPOST_MIN_EQUAL);
}

C5R_TEST_MAIN
