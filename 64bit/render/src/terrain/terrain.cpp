// terrain.cpp - c5r::terrain: the terrain surface drawn with the EXE's TerrainShader programs (epic 3200, issue 3202).
// Own code (team-3200 W1). The EXE's shaders are read from the EXE's file at run time; nothing of the EXE is copied
// here. Facts: research/05-graphics-composition/asset-pipeline.md 3.3, 3.4, 3.6; terrain-surface.md (this module).
//
// How the EXE's tessellated pass reads its inputs (from the SM5 disassembly of VS 0x067d6490, HS 0x067f98f0, DS
// 0x067fa080, PS 0x067e8d68; terrain-surface.md 2) and what this module feeds it:
//   VS: id -> (i, j) = (id & 255, id >> 8); local = (i - 3, j - 3) * LodStep; height texel = local + TileOffset.xy +
//       TileOffset.zw + 0.5, scaled by TerrainFrame.HeightTexelScale and sampled (sampler 0, linear wrap) at LodMip;
//       z = texel.w (the ALPHA channel) * HexScale.z * 255.
//       -> one R16G16B16A16_UNORM texture per tile, S = P + 2*kTileBorder texels a side, texel (u, v) = local (u-3,
//          v-3), stored with rows flipped (HeightTexelScale.y = -1/S under wrap addressing reads row S-1-v), alpha =
//          (u16 >> 8) * 257 and HexScale.z = sz, so that z = (u16 >> 8) * sz: the EXE's own drawn height, from its 8-bit
//          height page (issue 0134; until 2026-11-13 alpha = the raw u16 and HexScale.z = sz * 65535/65025, i.e.
//          HeightAtGrid 0x6D8B60's u16/255*sz, which stood 0.05-0.25 world units above the EXE's surface and hid the
//          bases of the EXE's models); TileOffset.xy = the tile's sample origin, .zw = 3 - origin.
//   HS: 16 control points, quad domain, factor TerrainFrame.TessFactor. DS: bicubic over the 4x4 control heights with
//       TerrainFrame.BicubicBasis; xy = (domain * LodStep + control point 5's local + TileOffset.xy) * HexScale.xy;
//       world = (x, y, z, 1) * TileWorld; clip = world * ViewProj; cache uv = (local + 2) * InvGridSize.
//       -> patch (a, b) of a tile = control points i = a + c + 2, j = b + r + 2 (c, r = 0..3), so control point 5 sits
//          at local (a, b): one patch per height sample, P x P patches per tile.
// Issue 0190: Arg6, t3 and ShadowMatrix0 from Options::shadow (the EXE's ShadowBlur copy), t7 the install's TerrainShadow
// LUT while a shadow is bound (terrain-surface.md 9); without one the text below holds. Issue 4305: t7 is the volume of the
// tile's style (0xD90E74 + 4 * style), as t6 (terrain-surface.md 12.6).
//   PS (pass 1): colour = t0 (when FlagC), shadow lerp (Arg6 * t3) through LUT t7, overlay t1 (premultiplied), FOW
//       (t4.x, t4.z, t5, LUT t6; corrected 2026-11-14 by issue 3953: the second mask channel is .z, not .y,
//       fog-of-war.md 3.3-3.4), distance fog (FogParams, ramp t2). -> FlagC 1, Arg6 0, overlay (0,0,0,0), FOW mask
//       (1,1,1,1): x 1, weight x * (1 - z) 0, no LUT (issue 3953: Options::fow binds the real inputs); distance fog: the
//       EXE's DistanceFog block from the camera and the install's fogramp.dds as t2 (issue 0188; until then density 0).
// Issue 0191: the height texture is the EXE's own page (1024 x 1024 A8_UNORM, 11 mips, built by c5r::bake's page rule),
// HeightTexelScale (1/1024, -1/1024), TileOffset.zw = minus the page window's first sample (64 - px * 896 for page px):
// the VS then reads exactly the texels the EXE's reads, at every mip. The per-tile R16G16B16A16 textures of the text above
// are gone. LodMip / LodStep / the tile's patch count / TessFactor come from the EXE's LOD rule (terrain.h, kLodExe).
#include "c5r/terrain/terrain.h"

#include <windows.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include "c5r/bake/heightpage.h"
#include "c5r/bake/post.h"
#include "c5r/gamefs/dds.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/shaders/shadersection.h"
#include "c5r/terrainsets/terrainsets.h"

namespace c5r { namespace terrain {

namespace {

template <class T> void rel(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

Status hrOk(HRESULT h, const char* what) {
    if (SUCCEEDED(h)) return Status::Ok();
    char b[32];
    std::snprintf(b, sizeof b, " (hr 0x%08lx)", static_cast<unsigned long>(h));
    return Status::fail(Err::Device, std::string(what) + b);
}

bool finite16(const float* m) {
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(m[i])) return false;
    return true;
}

void mul44(const float* a, const float* b, float* out) {  // out = a * b, row-major
    float r[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            double s = 0;
            for (int k = 0; k < 4; ++k) s += static_cast<double>(a[i * 4 + k]) * b[k * 4 + j];
            r[i * 4 + j] = static_cast<float>(s);
        }
    std::memcpy(out, r, sizeof r);
}

bool inverse44(const float* m, float* out) {
    double a[4][8];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 8; ++j) a[i][j] = j < 4 ? m[i * 4 + j] : (j - 4 == i ? 1.0 : 0.0);
    for (int c = 0; c < 4; ++c) {
        int piv = c;
        for (int r = c + 1; r < 4; ++r)
            if (std::fabs(a[r][c]) > std::fabs(a[piv][c])) piv = r;
        if (std::fabs(a[piv][c]) < 1e-12) return false;
        if (piv != c)
            for (int j = 0; j < 8; ++j) std::swap(a[c][j], a[piv][j]);
        const double d = a[c][c];
        for (int j = 0; j < 8; ++j) a[c][j] /= d;
        for (int r = 0; r < 4; ++r) {
            if (r == c) continue;
            const double f = a[r][c];
            for (int j = 0; j < 8; ++j) a[r][j] -= f * a[c][j];
        }
    }
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) out[i * 4 + j] = static_cast<float>(a[i][j + 4]);
    return true;
}

void rgba(uint32_t c, float* out) {
    for (int i = 0; i < 4; ++i) out[i] = static_cast<float>((c >> (8 * i)) & 0xFF) / 255.0f;
}

// Writes floats into a CPU copy of a constant buffer at a member of the layout, by name.
Status put(const terrainsets::CbLayout& l, const char* member, const float* v, uint32_t n, std::vector<uint8_t>& cb) {
    for (const terrainsets::CbMember& m : l.members) {
        if (std::strcmp(m.name, member) != 0) continue;
        const uint32_t size = terrainsets::memberTypeSize(m.type) * m.count;
        if (n * 4 > size || m.offset + n * 4 > cb.size())
            return Status::fail(Err::BadLayout, std::string(l.name) + "." + member + ": " + std::to_string(n) +
                                                    " floats do not fit");
        std::memcpy(cb.data() + m.offset, v, n * 4);
        return Status::Ok();
    }
    return Status::fail(Err::NotFound, std::string(l.name) + " has no member " + member);
}

// Ticket 0707: a member of a layout found once per draw for put's per-tile writes (put's checks and messages, the same
// failure at the same write: the status is returned where put would have returned it).
struct MemberAt {
    uint32_t offset = 0;
    Status st = Status::Ok();
};
MemberAt findMember(const terrainsets::CbLayout& l, const char* member, uint32_t n, size_t cbSize) {
    MemberAt r;
    for (const terrainsets::CbMember& m : l.members) {
        if (std::strcmp(m.name, member) != 0) continue;
        const uint32_t size = terrainsets::memberTypeSize(m.type) * m.count;
        if (n * 4 > size || m.offset + n * 4 > cbSize)
            r.st = Status::fail(Err::BadLayout, std::string(l.name) + "." + member + ": " + std::to_string(n) + " floats do not fit");
        r.offset = m.offset;
        return r;
    }
    r.st = Status::fail(Err::NotFound, std::string(l.name) + " has no member " + member);
    return r;
}

// Ticket 0707: the coarse cull. Tiles are grouped in blocks of kCullBlock x kCullBlock; a block whose box lies outside one
// clip plane by a margin far above float rounding holds only tiles that the per-tile test culls (the plane tests are
// linear, every tile box lies in the block box), so they are skipped without their 8-corner tests. Within the margin the
// block falls back to the per-tile test: the tiles drawn, their order and the counters are those of the per-tile cull.
constexpr uint32_t kCullBlock = 8;
// True when every corner of the box is outside the same clip plane (the per-tile test's six planes) by more than
// kCullMargin times the scale of the clip values over the box; false for any NaN or infinity.
constexpr float kCullMargin = 1e-4f;
bool boxOutsideByMargin(const float* vp, float x0, float x1, float y0, float y1, float z0, float z1) {
    const float ax = std::max(std::fabs(x0), std::fabs(x1)), ay = std::max(std::fabs(y0), std::fabs(y1)),
                az = std::max(std::fabs(z0), std::fabs(z1));
    float sc[4];
    for (int j = 0; j < 4; ++j)
        sc[j] = ax * std::fabs(vp[j]) + ay * std::fabs(vp[4 + j]) + az * std::fabs(vp[8 + j]) + std::fabs(vp[12 + j]);
    const float m03 = kCullMargin * (sc[0] + sc[3]), m13 = kCullMargin * (sc[1] + sc[3]), m2 = kCullMargin * sc[2],
                m23 = kCullMargin * (sc[2] + sc[3]);
    int out[6] = {0};
    for (int c = 0; c < 8; ++c) {
        const float w[4] = {(c & 1) ? x1 : x0, (c & 2) ? y1 : y0, (c & 4) ? z1 : z0, 1};
        float cl[4];
        for (int j = 0; j < 4; ++j) cl[j] = w[0] * vp[j] + w[1] * vp[4 + j] + w[2] * vp[8 + j] + w[3] * vp[12 + j];
        out[0] += cl[0] + cl[3] < -m03;   // the tile test's cl0 < -cl3
        out[1] += cl[0] - cl[3] > m03;    // cl0 > cl3
        out[2] += cl[1] + cl[3] < -m13;   // cl1 < -cl3
        out[3] += cl[1] - cl[3] > m13;    // cl1 > cl3
        out[4] += cl[2] < -m2;            // cl2 < 0
        out[5] += cl[2] - cl[3] > m23;    // cl2 > cl3
    }
    bool culled = false;
    for (int j = 0; j < 6; ++j) culled |= out[j] == 8;
    return culled;
}

const char* kOwnHlsl = R"(
// Own shaders of c5r::terrain (decision D58 1c, debug views, the water placeholder). Own code.
cbuffer OwnPs : register(b3) {
    float4 cColor;     // constant colour
    float4 cTile;      // x, y: world position of the tile's local (0, 0); z: material texture present; w: mode
    float4 cField;     // sx, sy, ramp top (world z), unused
    float4 cLight;     // light direction (xyz), ambient (w)
};
Texture2D<float> gMat : register(t10);
struct DsOut {
    float4 pos : SV_POSITION;
    float3 n : NORMAL;
    float4 t0 : TEXCOORD0;
    float4 t1 : TEXCOORD1;
    float3 wp : TEXCOORD2;
    float2 t3 : TEXCOORD3;
};
float4 ownPs(DsOut i) : SV_Target {
    int mode = (int)cTile.w;
    if (mode == 5) return float4(i.wp, 1.0);
    if (mode == 3) {
        float t = saturate(i.wp.z / cField.z);
        return float4(0.25 + 0.75 * t, 0.55 + 0.45 * t * (1.0 - t), 0.85 - 0.75 * t, 1.0);
    }
    if (mode == 4) {
        float3 n = normalize(cross(ddx(i.wp), ddy(i.wp)));
        if (n.z < 0) n = -n;
        float l = saturate(dot(n, normalize(cLight.xyz)));
        float v = cLight.w + (1.0 - cLight.w) * l;
        return float4(v, v, v, 1.0);
    }
    if (mode == 1 && cTile.z > 0.5) {
        int2 s = int2(floor((i.wp.x - cTile.x) / cField.x + 3.5), floor((i.wp.y - cTile.y) / cField.y + 3.5));
        uint m = (uint)round(gMat.Load(int3(s, 0)) * 255.0);
        uint h = m * 2654435761u;
        return float4(((h >> 8) & 255) / 255.0, ((h >> 16) & 255) / 255.0, ((h >> 24) & 255) / 255.0, 1.0);
    }
    return cColor;
}

cbuffer Water : register(b0) {
    row_major float4x4 wViewProj;
    float4 wRect;      // xmin, ymin, xmax, ymax (world)
    float4 wColor;
    float4 wParams;    // x: z, y: world-position mode
};
struct WOut { float4 pos : SV_Position; float3 wp : TEXCOORD0; };
WOut waterVs(uint id : SV_VertexID) {
    WOut o;
    float2 c = float2((id & 1) ? wRect.z : wRect.x, (id & 2) ? wRect.w : wRect.y);
    o.wp = float3(c, wParams.x);
    o.pos = mul(float4(o.wp, 1.0), wViewProj);
    return o;
}
float4 waterPs(WOut i) : SV_Target {
    if (wParams.y > 0.5) return float4(i.wp, 2.0);
    return wColor;
}
)";

Status compile(const char* entry, const char* target, ID3DBlob** out) {
    ID3DBlob* err = nullptr;
    const HRESULT h = D3DCompile(kOwnHlsl, std::strlen(kOwnHlsl), "c5r_terrain_own.hlsl", nullptr, nullptr, entry, target,
                                 D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &err);
    std::string msg = err ? std::string(static_cast<const char*>(err->GetBufferPointer()), err->GetBufferSize()) : "";
    rel(err);
    if (FAILED(h)) return Status::fail(Err::Device, std::string("D3DCompile ") + entry + ": " + msg);
    return Status::Ok();
}

const int kModeConstant = 0, kModeMaterial = 1, kModeHeight = 3, kModeNormal = 4, kModeWorld = 5;

int ownMode(ColorSource c) {
    switch (c) {
    case ColorSource::Material: return kModeMaterial;
    case ColorSource::Height: return kModeHeight;
    case ColorSource::Normal: return kModeNormal;
    case ColorSource::WorldPos: return kModeWorld;
    default: return kModeConstant;
    }
}

struct Tile {
    uint32_t page = 0;       // index into Impl::pages (issue 0191)
    ID3D11ShaderResourceView* mat = nullptr;
    float zmin = 0, zmax = 0;
    TileCache cache;
    // Land by the EXE's height class (0x7212B0, the residency's rule): the highest raw sample of the tile and 4 samples
    // around it, in u16 / 255 units, is >= 2. Water tiles are never baked: a tile without a cache is gray only on land.
    bool land = false;
    uint64_t grayMark = 0;   // the draw that counted this tile last (a tile drawn in two wrap copies counts once)
    uint8_t style = 0;       // issue 3953: the tile style (tile+0x138): t6 with Options::fow on, t7 with a shadow
                             // (issue 4305), 0 Generic
    uint8_t cacheKind = 0;   // issue 0371: CacheKind of cache.color (setTileCache)
};

// A height page (issue 0191): the EXE's page (px, py) with window start (px * 896 - 64, py * 896 - 64), or for a tile that
// does not fit its EXE page (3-plot tiles only) a page of the same rule whose window starts 64 samples before the tile.
struct Page {
    int32_t wx = 0, wy = 0;  // first sample of the window (mip 0 column 0; row 1023 holds sample row wy)
    ID3D11ShaderResourceView* srv = nullptr;
    bool exe = false;        // ticket 4909: an EXE page (bake::buildHeightPage (px, py)), else a window page
    uint32_t px = 0, py = 0;
};

// The levels of a page of the rule with an arbitrary window start (both axes wrap, texel = u16 >> 8, rows bottom-up),
// mips by c5r::bake::heightPageNextMip. For EXE windows c5r::bake::buildHeightPage is used instead.
Status buildWindowPage(const heightfield::HeightGrid& g, int32_t wx, int32_t wy, bake::HeightPage& out) {
    out.levels.clear();
    const uint32_t N = bake::kHeightPageSize;
    std::vector<uint8_t> l0(static_cast<size_t>(N) * N);
    const int64_t X = g.samplesX, Y = g.samplesY;
    for (uint32_t v = 0; v < N; ++v) {
        const int64_t sy = ((static_cast<int64_t>(wy) + (N - 1 - v)) % Y + Y) % Y;
        for (uint32_t u = 0; u < N; ++u) {
            const int64_t sx = ((static_cast<int64_t>(wx) + u) % X + X) % X;
            l0[static_cast<size_t>(v) * N + u] =
                static_cast<uint8_t>(g.h[g.index(static_cast<uint32_t>(sx), static_cast<uint32_t>(sy))] >> 8);
        }
    }
    out.levels.push_back(std::move(l0));
    for (uint32_t m = 1, n = N; m < bake::kHeightPageMips; ++m, n >>= 1) {
        std::vector<uint8_t> next;
        C5R_TRY(bake::heightPageNextMip(out.levels.back(), n, next));
        out.levels.push_back(std::move(next));
    }
    return Status::Ok();
}

struct GpuQuery {
    ID3D11Query* disjoint = nullptr;
    ID3D11Query* t0 = nullptr;
    ID3D11Query* t1 = nullptr;
    bool issued = false;
    uint64_t serial = 0;
};

} // namespace

struct Renderer::Impl {
    ID3D11Device* dev = nullptr;
    ID3D11VertexShader* vs = nullptr;
    ID3D11HullShader* hs[3] = {};
    ID3D11DomainShader* ds = nullptr;
    ID3D11PixelShader* ps[3] = {};
    ID3D11PixelShader* ownPs = nullptr;
    ID3D11VertexShader* waterVs = nullptr;
    ID3D11PixelShader* waterPs = nullptr;
    ID3D11Buffer* cbTile = nullptr;
    ID3D11Buffer* cbFrame = nullptr;
    ID3D11Buffer* cbFog = nullptr;
    ID3D11Buffer* cbOwn = nullptr;
    ID3D11Buffer* cbWater = nullptr;
    ID3D11SamplerState* samp[13] = {};
    ID3D11ShaderResourceView* overlay = nullptr;  // (0,0,0,0)
    ID3D11ShaderResourceView* white = nullptr;    // (1,1,1,1): fog ramp, shadow, FOW mask (visible), spec map, caches
    ID3D11ShaderResourceView* black = nullptr;    // FOW base layer; t2 without a fog ramp (red 0: no distance fog)
    ID3D11ShaderResourceView* fogRamp = nullptr;  // issue 0188: the install's fogramp.dds (t2), null without gameDir
    ID3D11ShaderResourceView* specMap = nullptr;  // issue 0189: the install's specscreenmap.dds (t9), null without gameDir
    // issue 0190, per style since 4305: TerrainShadow ColorKey volume (t7 with a shadow); null = identity (lut)
    ID3D11ShaderResourceView* shadowLut[5] = {};
    uint32_t shadowLutFile = 0;                      // bit s = shadowLut[s] from a file
    ID3D11ShaderResourceView* lut = nullptr;      // 32^3 identity
    ID3D11ShaderResourceView* fowLut[5] = {};     // issue 3953: TerrainFOW volume per style; null = identity (lut)
    uint32_t fowLutFile = 0;                      // issue 3953: bit s = fowLut[s] from a file
    ID3D11RasterizerState* rs = nullptr;
    ID3D11RasterizerState* rsWire = nullptr;
    ID3D11DepthStencilState* dss = nullptr;
    ID3D11DepthStencilState* dssOff = nullptr;   // ticket 4551: the Screen Overlay pass (no depth)
    ID3D11BlendState* bs = nullptr;
    ID3D11Buffer* ib[kMaxLodLevel + 1] = {};    // issue 0191: one patch list per level
    uint32_t ibPatches[kMaxLodLevel + 1] = {};
    LodState lod;                                // issue 0191: TR+0x2424..+0x2430 of the kLodExe draws
    ID3D11Texture2D* depthTex = nullptr;
    ID3D11DepthStencilView* depthDsv = nullptr;
    uint32_t depthW = 0, depthH = 0;
    GpuQuery q[4];
    GpuQuery* listQ = nullptr;   // ticket 0753: the set gpuTimingBegin opened (gpuTimingEnd closes it)
    uint64_t serial = 0;
    uint64_t drawMark = 0;   // +1 per draw (Tile::grayMark)
    double lastGpuMs = -1;
    const terrainsets::ShaderSetDesc* set = nullptr;

    // field
    bool haveField = false;
    TileGrid grid;
    uint32_t P = 0, S = 0;
    uint32_t samplesX = 0, samplesY = 0;
    float sx = 1, sy = 0.866f, sz = 0.2f, ox = 0, oy = 0, waterZ = 0, zmaxAll = 1;
    bool wrapX = false;
    std::vector<Tile> tiles;
    std::vector<Page> pages;
    std::vector<uint8_t> exeListed;   // issue 0191: per EXE (64-sample) tile, 1 = height class 3 (the LOD's list)
    // ticket 0707: the coarse cull's scratch (per draw): each block's z range and, per wrap copy, whether it is culled
    std::vector<float> blockZ0, blockZ1;
    std::vector<uint8_t> blockCulled;

    void dropField() {
        for (Tile& t : tiles) {
            rel(t.mat);
            rel(t.cache.color);
            rel(t.cache.second);
        }
        tiles.clear();
        for (Page& pg : pages) rel(pg.srv);
        pages.clear();
        exeListed.clear();
        haveField = false;
        grid = TileGrid();
        for (int l = 0; l <= kMaxLodLevel; ++l) {
            rel(ib[l]);
            ibPatches[l] = 0;
        }
        lod = LodState();
    }
    ~Impl() {
        dropField();
        rel(vs);
        for (auto& h : hs) rel(h);
        rel(ds);
        for (auto& x : ps) rel(x);
        rel(ownPs);
        rel(waterVs);
        rel(waterPs);
        rel(cbTile);
        rel(cbFrame);
        rel(cbFog);
        rel(cbOwn);
        rel(cbWater);
        for (auto& s : samp) rel(s);
        rel(overlay);
        rel(white);
        rel(black);
        rel(fogRamp);
        rel(specMap);
        for (auto& l : shadowLut) rel(l);
        shadowLutFile = 0;
        rel(lut);
        for (auto& l : fowLut) rel(l);
        rel(rs);
        rel(rsWire);
        rel(dss);
        rel(dssOff);
        rel(bs);
        rel(depthDsv);
        rel(depthTex);
        for (GpuQuery& g : q) {
            rel(g.disjoint);
            rel(g.t0);
            rel(g.t1);
        }
        rel(dev);
    }

    Status makeBuffer(uint32_t size, ID3D11Buffer** out) {
        D3D11_BUFFER_DESC d = {};
        d.ByteWidth = size;
        d.Usage = D3D11_USAGE_DYNAMIC;
        d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        return hrOk(dev->CreateBuffer(&d, nullptr, out), "CreateBuffer (constant)");
    }
    Status makeTex2D(uint32_t w, uint32_t h, DXGI_FORMAT f, const void* data, uint32_t pitch,
                     ID3D11ShaderResourceView** srv) {
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = w;
        d.Height = h;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = f;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA s = {data, pitch, 0};
        ID3D11Texture2D* t = nullptr;
        C5R_TRY(hrOk(dev->CreateTexture2D(&d, &s, &t), "CreateTexture2D"));
        Status st = hrOk(dev->CreateShaderResourceView(t, nullptr, srv), "CreateShaderResourceView");
        t->Release();
        return st;
    }
    // The per-tile parts of setHeightField, shared with updateHeightRegion (ticket 4909) so both give the same bits.
    // z bounds over the levels 0..kMaxLodLevel of the texels the tile (origin c0, r0) can read in its page hp.
    void tileBounds(Tile& t, int c0, int r0, const bake::HeightPage& hp, float zScale) const {
        float zmin = 1e30f, zmax = -1e30f;
        const Page& pg = pages[t.page];
        const int32_t ux0 = c0 - pg.wx, ux1 = c0 + static_cast<int32_t>(P) - pg.wx;   // window columns
        const int32_t uy0 = r0 - pg.wy, uy1 = r0 + static_cast<int32_t>(P) - pg.wy;   // window sample rows
        for (int l = 0; l <= kMaxLodLevel; ++l) {
            const int32_t n = static_cast<int32_t>(bake::kHeightPageSize >> l);
            const int32_t e = 3;   // texels: two control points of margin, the bilinear neighbour
            const int32_t cx0 = std::max(0, (ux0 >> l) - e), cx1 = std::min(n - 1, (ux1 >> l) + e);
            // row v holds window sample row 1023 - v at mip 0, so rows (1023 - uy1 .. 1023 - uy0) >> l
            const int32_t ry0 = std::max(0, ((static_cast<int32_t>(bake::kHeightPageSize) - 1 - uy1) >> l) - e);
            const int32_t ry1 = std::min(n - 1, ((static_cast<int32_t>(bake::kHeightPageSize) - 1 - uy0) >> l) + e);
            const std::vector<uint8_t>& lv = hp.levels[static_cast<size_t>(l)];
            uint8_t bmin = 255, bmax = 0;
            for (int32_t v = ry0; v <= ry1; ++v)
                for (int32_t u = cx0; u <= cx1; ++u) {
                    const uint8_t b = lv[static_cast<size_t>(v) * n + u];
                    bmin = std::min(bmin, b);
                    bmax = std::max(bmax, b);
                }
            zmin = std::min(zmin, bmin * zScale);
            zmax = std::max(zmax, bmax * zScale);
        }
        t.zmin = zmin;
        t.zmax = zmax;
    }
    // the material bytes of the module's own PS (D58 1c): S x S around the tile, clamped; m is S x S scratch
    Status tileMaterial(Tile& t, const heightfield::HeightGrid& g, int c0, int r0, std::vector<uint8_t>& m) {
        for (uint32_t v = 0; v < S; ++v)
            for (uint32_t u = 0; u < S; ++u) {
                const int c = c0 + static_cast<int>(u) - static_cast<int>(kTileBorder);
                const int r = r0 + static_cast<int>(v) - static_cast<int>(kTileBorder);
                int cc = c, rr = std::min(std::max(r, 0), static_cast<int>(g.samplesY) - 1);
                if (g.wrapX) cc = ((cc % static_cast<int>(g.samplesX)) + static_cast<int>(g.samplesX)) % static_cast<int>(g.samplesX);
                else cc = std::min(std::max(cc, 0), static_cast<int>(g.samplesX) - 1);
                m[static_cast<size_t>(v) * S + u] = g.mat[g.index(static_cast<uint32_t>(cc), static_cast<uint32_t>(rr))];
            }
        return makeTex2D(S, S, DXGI_FORMAT_R8_UNORM, m.data(), S, &t.mat);
    }
    // the land class (Tile::land): rows outside the grid are skipped, columns wrap or are skipped
    void tileLand(Tile& t, const heightfield::HeightGrid& g, int c0, int r0) const {
        uint16_t hmax = 0;
        const int X = static_cast<int>(g.samplesX), Y = static_cast<int>(g.samplesY), Pn = static_cast<int>(P);
        for (int r = r0 - 4; r < r0 + Pn + 4; ++r) {
            if (r < 0 || r >= Y) continue;
            for (int c = c0 - 4; c < c0 + Pn + 4; ++c) {
                int cw = c;
                if (cw < 0 || cw >= X) {
                    if (!g.wrapX) continue;
                    cw = (cw % X + X) % X;
                }
                hmax = std::max(hmax, g.h[g.index(static_cast<uint32_t>(cw), static_cast<uint32_t>(r))]);
            }
        }
        t.land = hmax / 255.0f >= 2.0f;
        t.grayMark = 0;
    }
    // Issue 3953: a 32^3 R8G8B8A8_UNORM volume from 32^3 RGBA texels, x fastest (the identity LUT's and the ColorKey
    // volumes' layout).
    Status makeLut(const void* rgba, const char* what, ID3D11ShaderResourceView** out) {
        D3D11_TEXTURE3D_DESC d = {};
        d.Width = d.Height = d.Depth = 32;
        d.MipLevels = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA s = {rgba, 32 * 4, 32 * 32 * 4};
        ID3D11Texture3D* t = nullptr;
        C5R_TRY(hrOk(dev->CreateTexture3D(&d, &s, &t), (std::string("CreateTexture3D (") + what + ")").c_str()));
        Status st = hrOk(dev->CreateShaderResourceView(t, nullptr, out), (std::string("CreateShaderResourceView (") + what + ")").c_str());
        t->Release();
        return st;
    }
    // Issue 3953: style's TerrainFOW volume from TGA bytes (null / 0: identity); unchanged on error.
    Status setFowLut(uint32_t style, const uint8_t* tga, size_t size) {
        if (style >= 5) return Status::fail(Err::BadLayout, "setFowLut: style " + std::to_string(style) + " is not 0..4");
        if (!tga || size == 0) {
            rel(fowLut[style]);
            fowLutFile &= ~(1u << style);
            return Status::Ok();
        }
        std::vector<uint8_t> v;
        Status st = bake::buildColorKeyLut(tga, size, v);
        if (!st.ok()) return Status::fail(st.code, "TerrainFOW_" + std::string(bake::styleName(style)) + "_ColorKey: " + st.msg);
        ID3D11ShaderResourceView* nv = nullptr;
        C5R_TRY(makeLut(v.data(), "TerrainFOW LUT", &nv));
        rel(fowLut[style]);
        fowLut[style] = nv;
        fowLutFile |= 1u << style;
        return Status::Ok();
    }
    // Issue 4305: style's TerrainShadow volume from TGA bytes (null / 0: identity); unchanged on error. As setFowLut.
    Status setShadowLut(uint32_t style, const uint8_t* tga, size_t size) {
        if (style >= 5) return Status::fail(Err::BadLayout, "setShadowLut: style " + std::to_string(style) + " is not 0..4");
        if (!tga || size == 0) {
            rel(shadowLut[style]);
            shadowLutFile &= ~(1u << style);
            return Status::Ok();
        }
        std::vector<uint8_t> v;
        Status st = bake::buildColorKeyLut(tga, size, v);
        if (!st.ok())
            return Status::fail(st.code, "TerrainShadow_" + std::string(bake::styleName(style)) + "_ColorKey.tga: " + st.msg);
        ID3D11ShaderResourceView* nv = nullptr;
        C5R_TRY(makeLut(v.data(), "TerrainShadow LUT", &nv));
        rel(shadowLut[style]);
        shadowLut[style] = nv;
        shadowLutFile |= 1u << style;
        return Status::Ok();
    }
    Status upload(ID3D11DeviceContext* ctx, ID3D11Buffer* b, const void* data, size_t n) {
        D3D11_MAPPED_SUBRESOURCE m;
        C5R_TRY(hrOk(ctx->Map(b, 0, D3D11_MAP_WRITE_DISCARD, 0, &m), "Map (constant buffer)"));
        std::memcpy(m.pData, data, n);
        ctx->Unmap(b, 0);
        return Status::Ok();
    }
    // Retires issued timestamp queries oldest first; lastGpuMs = the newest one read. Without wait it stops at the first
    // one not ready (GetData with DONOTFLUSH); with wait it flushes and waits up to 2 s for each.
    void pollQueries(ID3D11DeviceContext* ctx, bool wait) {
        for (;;) {
            GpuQuery* g = nullptr;
            for (GpuQuery& x : q)
                if (x.issued && (!g || x.serial < g->serial)) g = &x;
            if (!g) return;
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj = {};
            UINT64 a = 0, b = 0;
            const UINT flags = wait ? 0 : D3D11_ASYNC_GETDATA_DONOTFLUSH;
            const auto t0 = std::chrono::steady_clock::now();
            for (;;) {
                if (ctx->GetData(g->disjoint, &dj, sizeof dj, flags) == S_OK &&
                    ctx->GetData(g->t0, &a, sizeof a, flags) == S_OK && ctx->GetData(g->t1, &b, sizeof b, flags) == S_OK)
                    break;
                if (!wait || std::chrono::steady_clock::now() - t0 > std::chrono::seconds(2)) return;
                Sleep(0);
            }
            g->issued = false;
            if (!dj.Disjoint && dj.Frequency && b >= a)
                lastGpuMs = static_cast<double>(b - a) * 1000.0 / static_cast<double>(dj.Frequency);
        }
    }
};

void overlayProjForCopy(const Overlay& o, int k, float shift, float out[16]) {
    static const float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    if (!o.srv || k < -1 || k > 1) {
        std::memcpy(out, ident, sizeof ident);
        return;
    }
    const float* p = o.proj[k + 1];
    std::memcpy(out, p, 16 * sizeof(float));
    if (k == 0) return;  // the camera's view: the EXE's bytes unchanged
    // Translation(-k * shift) * P, row vectors: rows 0-2 stay, row 3 = -k * shift * row 0 + row 3
    const double s = -static_cast<double>(k) * shift;
    for (int j = 0; j < 4; ++j) out[12 + j] = static_cast<float>(s * p[j] + static_cast<double>(p[12 + j]));
}

CacheKind cacheKindOf(ID3D11ShaderResourceView* v) {
    // issue 0371: what a tile's t0 is, from the view's own description and its texture's size (no GPU work)
    if (!v) return CacheKind::None;
    D3D11_SHADER_RESOURCE_VIEW_DESC d = {};
    v->GetDesc(&d);
    if (d.Format == DXGI_FORMAT_BC1_UNORM || d.Format == DXGI_FORMAT_BC1_UNORM_SRGB || d.Format == DXGI_FORMAT_BC1_TYPELESS)
        return CacheKind::Bc1;
    if (d.Format != DXGI_FORMAT_R8G8B8A8_UNORM && d.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) return CacheKind::Other;
    ID3D11Resource* r = nullptr;
    v->GetResource(&r);
    ID3D11Texture2D* t = nullptr;
    if (r) r->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&t));
    CacheKind k = CacheKind::Other;
    if (t) {
        D3D11_TEXTURE2D_DESC td = {};
        t->GetDesc(&td);
        k = td.Width == 256 ? CacheKind::Rgba256 : td.Width == 512 ? CacheKind::Rgba512 : CacheKind::Other;
        t->Release();
    }
    if (r) r->Release();
    return k;
}

float wrapPeriod(uint32_t samplesX, float sx) {
    // issue 4314: the EXE's float(64 * mapW - 1) at 0xD90FE4 (MapSetup 0x535BF0), not the sample span samplesX * sx
    return samplesX ? static_cast<float>(samplesX - 1) * sx : 0.0f;
}

void shadowMatrixForCopy(const Shadow& s, int k, float shift, float out[16]) {
    // issue 0190: the same rule as the overlay's (the PS maps the drawn world position through it, terrain-surface.md 9)
    Overlay o;
    o.srv = s.srv;
    std::memcpy(o.proj, s.matrix, sizeof o.proj);
    overlayProjForCopy(o, k, shift, out);
}

float fogZoomFraction(float cameraHeight, float zoomMin, float zoomMax) {
    if (!(cameraHeight > 0.0f) || !(zoomMax > zoomMin)) return 0.0f;
    const float zoom = std::log(cameraHeight / 50.0f) * 6.6667f;
    const float f = (std::min(std::max(zoom, zoomMin), zoomMax) - zoomMin) / (zoomMax - zoomMin);
    return std::min(std::max(f, 0.0f), 1.0f);
}

void distanceFogBlock(const DistanceFog& f, const float camPos[3], float out[8]) {
    if (!f.enabled) {
        const float off[8] = {0.5f, 0.5f, 0.5f, 1.0f, 0, 0, 0, 0};
        std::memcpy(out, off, sizeof off);
        return;
    }
    std::memcpy(out, f.color, 4 * sizeof(float));
    out[4] = -f.density;
    out[5] = 0.0f;
    if (f.density != 0.0f) {
        // exp(-(density * camZ)) * (-1 / density) * scale with every step in double, as the EXE's x87: bit-equal at
        // 3601's cameras. The product rounded to float first (as water.cpp's 0x144) is 4 and 16 ulp off there.
        const double d = static_cast<double>(f.density);
        const double e = std::exp(-(d * static_cast<double>(camPos[2])));
        out[5] = static_cast<float>(e * (-1.0 / d) * static_cast<double>(f.scale));
    }
    out[6] = 0.0f;
    out[7] = f.w < 0.0f ? fogZoomFraction(camPos[2]) : f.w;
}

float drawnHeightAtSample(const heightfield::HeightGrid& g, int x, int y) {
    if (g.samplesX == 0 || g.samplesY == 0) return 0.0f;
    if (g.wrapX) {
        const int w = static_cast<int>(g.samplesX);
        x = ((x % w) + w) % w;
    }
    if (x < 0 || y < 0 || x >= static_cast<int>(g.samplesX) || y >= static_cast<int>(g.samplesY)) return 0.0f;
    return static_cast<float>(g.h[g.index(static_cast<uint32_t>(x), static_cast<uint32_t>(y))] >> 8) * g.sz;
}

// ---- issue 0191: the EXE's LOD rule (terrain-surface.md 7; written from that description, D83) ----

void exeTileBox(uint32_t originX, uint32_t originY, uint32_t d4, uint32_t d8, float sx, float sy, float sz, float ox,
                float oy, float box[6]) {
    box[0] = static_cast<float>(static_cast<int32_t>(originX)) * sx - ox;
    box[1] = static_cast<float>(static_cast<int32_t>(originY)) * sy - oy;
    box[2] = -0.0f;   // minus the field's z offset (TR8+0x1AB8), 0 for every field the module draws [I]
    box[3] = static_cast<float>(static_cast<int32_t>(d4)) * sx + box[0];
    box[4] = static_cast<float>(static_cast<int32_t>(d8)) * sy + box[1];
    box[5] = sz * 255.0f + box[2];
}

uint32_t exeTileVerts(uint32_t origin, uint32_t tileSamples, uint32_t samples) {
    if (origin >= samples) return 0;
    return std::min(tileSamples + 1, samples - origin);
}

namespace {

// 0x744AE0 in double from its float steps; the EXE returns the product unrounded (x87).
double projectedSizeD(const float box[6], const float m[16], uint32_t width) {
    const float lo[3] = {box[0], box[1], box[2]}, hi[3] = {box[3], box[4], box[5]};
    // the four space diagonals, A_k -> B_k
    const float A[4][3] = {{lo[0], lo[1], lo[2]}, {lo[0], lo[1], hi[2]}, {lo[0], hi[1], hi[2]}, {lo[0], hi[1], lo[2]}};
    const float B[4][3] = {{hi[0], hi[1], hi[2]}, {hi[0], hi[1], lo[2]}, {hi[0], lo[1], lo[2]}, {hi[0], lo[1], hi[2]}};
    auto ndc = [&](const float* p, float* out) {
        float c[4];
        for (int j = 0; j < 4; ++j) c[j] = p[0] * m[j] + p[1] * m[4 + j] + p[2] * m[8 + j] + m[12 + j];
        const float inv = 1.0f / c[3];
        out[0] = inv * c[0];
        out[1] = inv * c[1];
    };
    float best = 0.0f;
    for (int k = 0; k < 4; ++k) {
        float a[2], b[2];
        ndc(A[k], a);
        ndc(B[k], b);
        const float dy = -b[1] + a[1], dx = -b[0] + a[0];
        const float d = static_cast<float>(std::sqrt(static_cast<double>(dy) * dy + static_cast<double>(dx) * dx) * 0.5);
        if (d > best) best = d;
    }
    return static_cast<double>(width) * static_cast<double>(best) * 0.5;
}

float tileLodD(double p, uint32_t maxVerts, int triSize, int subdiv) {
    double q = p * 0.5 / static_cast<double>(triSize) / static_cast<double>(subdiv);
    if (1.0 > q) q = 1.0;
    const double n = static_cast<double>(static_cast<int32_t>(maxVerts) - 1);
    const double r = std::log(n / q) * 1.4426950408889634 + 0.5;
    if (!(4.0 > r)) return 4.0f;
    const float rf = static_cast<float>(r);
    if (rf > -2.0f) return rf;
    return -2.0f;
}

} // namespace

float exeTileProjectedSize(const float box[6], const float viewProj[16], uint32_t backBufferWidth) {
    return static_cast<float>(projectedSizeD(box, viewProj, backBufferWidth));
}

float exeTileLod(float projectedSize, uint32_t maxVerts, int triSize, int subdiv) {
    return tileLodD(projectedSize, maxVerts, triSize, subdiv);
}

bool exeTileSphereVisible(const float box[6], const float m[16]) {
    float c[3], e[3];
    for (int i = 0; i < 3; ++i) {
        c[i] = (box[i] + box[3 + i]) * 0.5f;
        e[i] = -box[i] + box[3 + i];
    }
    const float r = static_cast<float>(std::sqrt(static_cast<double>(e[2]) * e[2] + static_cast<double>(e[1]) * e[1] +
                                                 static_cast<double>(e[0]) * e[0]) * 0.5);
    // the planes of 0x8CEEE0 from the clip matrix's columns: far c3 - c2, near c2, c3 + c0, c3 - c0, c3 + c1, c3 - c1
    auto col = [&](int j, int i) { return m[i * 4 + j]; };
    float P[6][4];
    for (int i = 0; i < 4; ++i) {
        P[0][i] = col(3, i) - col(2, i);
        P[1][i] = col(2, i);
        P[2][i] = col(3, i) + col(0, i);
        P[3][i] = col(3, i) - col(0, i);
        P[4][i] = col(3, i) + col(1, i);
        P[5][i] = col(3, i) - col(1, i);
    }
    bool vis = true;
    for (auto& p : P) {
        const float s = 1.0f / std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        for (float& x : p) x *= s;
        const float dist = p[2] * c[2] + p[1] * c[1] + p[0] * c[0] + p[3];
        if (-r > dist) vis = false;
    }
    return vis;
}

bool exeLodUpdate(LodState& s, float M, int subdiv) {
    const float S = s.value, H = s.hyst;
    const float d = M - S;
    bool take = false;
    if (std::fabs(d) > std::fabs(H)) take = true;
    else if (!(H * d < 0.0f) && std::fabs(d) > 0.0f) take = true;   // NaN products count as < 0 (comiss: unordered)
    if (!take) return false;
    if (M >= 0.0f) {
        s.lodMip = M;
        s.tessFactor = subdiv;
    } else {
        s.lodMip = 0.0f;
        s.tessFactor = (1 << static_cast<int>(M * -0.75f)) * subdiv;
    }
    s.lodMip = static_cast<float>(std::floor(static_cast<double>(s.lodMip)));
    const float fM = static_cast<float>(std::floor(static_cast<double>(M)));
    const double fS = std::floor(static_cast<double>(S));
    if (std::fabs(static_cast<double>(fM) - fS) > 0.0) s.hyst = static_cast<float>((static_cast<double>(fM) - fS) * 0.25);
    s.value = M;
    return true;
}

void exeLodGrid(uint32_t d, int level, uint32_t* patches, float* lodStep) {
    int32_t n = static_cast<int32_t>(d);
    for (int l = 0; l < level; ++l) n = (n >> 1) + 1;
    if (patches) *patches = n > 1 ? static_cast<uint32_t>(n - 1) : 0;
    if (lodStep) *lodStep = n > 1 ? static_cast<float>(static_cast<int32_t>(d) - 1) / static_cast<float>(n - 1) : 0.0f;
}

bool exeTileClass3(const heightfield::HeightGrid& g, uint32_t tx, uint32_t ty) {
    if (g.samplesX == 0 || g.samplesY == 0 || g.h.size() != static_cast<size_t>(g.samplesX) * g.samplesY) return false;
    uint16_t hmax = 0;
    for (uint32_t r = ty * 64 + 4; r < ty * 64 + 67; ++r) {
        const uint32_t rr = r % g.samplesY;
        for (uint32_t c = tx * 64 + 4; c < tx * 64 + 67; ++c) hmax = std::max(hmax, g.h[g.index(c % g.samplesX, rr)]);
    }
    return !(static_cast<float>(hmax) / 255.0f < 30.0f);
}

Status exeFrameLod(uint32_t samplesX, uint32_t samplesY, float sx, float sy, float sz, float ox, float oy,
                   const float viewProj[16], uint32_t backBufferWidth, int triSize, int subdiv, LodFrame& out,
                   const uint8_t* listed, std::vector<uint32_t>* visibleTiles) {
    if (samplesX == 0 || samplesY == 0 || samplesX % 64 || samplesY % 64)
        return Status::fail(Err::BadLayout, "exeFrameLod: samples not a positive multiple of 64");
    if (!finite16(viewProj) || !(std::isfinite(sx) && std::isfinite(sy) && std::isfinite(sz) && std::isfinite(ox) &&
                                 std::isfinite(oy)))
        return Status::fail(Err::BadLayout, "exeFrameLod: a value is not finite");
    if (backBufferWidth == 0 || triSize < 1 || triSize > 64 || subdiv < 1 || subdiv > 64)
        return Status::fail(Err::BadLayout, "exeFrameLod: width 0 or TriSize / Subdiv outside 1..64");
    LodFrame f;
    if (visibleTiles) visibleTiles->clear();
    const uint32_t tx = samplesX / 64, ty = samplesY / 64;
    for (uint32_t y = 0; y < ty; ++y)
        for (uint32_t x = 0; x < tx; ++x) {
            if (listed && !listed[y * tx + x]) continue;
            const uint32_t d4 = exeTileVerts(x * 64, 64, samplesX), d8 = exeTileVerts(y * 64, 64, samplesY);
            float box[6];
            exeTileBox(x * 64, y * 64, d4, d8, sx, sy, sz, ox, oy, box);
            if (!exeTileSphereVisible(box, viewProj)) continue;
            ++f.visible;
            if (visibleTiles) visibleTiles->push_back(y * tx + x);
            const float l = tileLodD(projectedSizeD(box, viewProj, backBufferWidth), std::max(d4, d8), triSize, subdiv);
            if (f.minLod > l) {
                f.minLod = l;
                f.minTile = y * tx + x;
            }
        }
    out = f;
    return Status::Ok();
}

Renderer::Renderer() {}
Renderer::~Renderer() { release(); }

LodState Renderer::lodState() const { return p_ ? p_->lod : LodState(); }

void Renderer::resetLod() {
    if (p_) p_->lod = LodState();
}

// ---- issue 3953: the fog of war's LUTs and tile styles ----
ID3D11ShaderResourceView* Renderer::fowLut(uint32_t style) const {
    if (!p_ || style >= 5) return nullptr;
    return p_->fowLut[style] ? p_->fowLut[style] : p_->lut;
}

uint32_t Renderer::fowLutsFromFile() const { return p_ ? p_->fowLutFile : 0; }

Status Renderer::setFowLut(uint32_t style, const uint8_t* tga, size_t size) {
    if (!p_) return Status::fail(Err::NotFound, "setFowLut: init not done");
    return p_->setFowLut(style, tga, size);
}

Status Renderer::setTileStyle(uint32_t tileX, uint32_t tileY, uint32_t style) {
    if (!p_ || !p_->haveField) return Status::fail(Err::NotFound, "setTileStyle: no field set");
    if (tileX >= p_->grid.tilesX || tileY >= p_->grid.tilesY)
        return Status::fail(Err::BadLayout, "setTileStyle: tile (" + std::to_string(tileX) + ", " + std::to_string(tileY) +
                                                ") outside the tile grid");
    if (style >= 5) return Status::fail(Err::BadLayout, "setTileStyle: style " + std::to_string(style) + " is not 0..4");
    p_->tiles[static_cast<size_t>(tileY) * p_->grid.tilesX + tileX].style = static_cast<uint8_t>(style);
    return Status::Ok();
}

Status Renderer::setTileStyles(const uint8_t* artTypes, uint32_t mapW, uint32_t mapH, bool wrapX) {
    if (!p_ || !p_->haveField) return Status::fail(Err::NotFound, "setTileStyles: no field set");
    const uint32_t plotsX = p_->samplesX / 64, plotsY = p_->samplesY / 64;
    if (!artTypes || mapW == 0 || mapH == 0 || mapW != plotsX || mapH + 4 != plotsY)
        return Status::fail(Err::BadLayout, "setTileStyles: art types of " + std::to_string(mapW) + " x " + std::to_string(mapH) +
                                                " plots for a field of " + std::to_string(plotsX) + " x " +
                                                std::to_string(plotsY) + " (the DLL's map is the field's rows - 4)");
    const uint32_t plots = p_->P / 64;
    for (uint32_t ty = 0; ty < p_->grid.tilesY; ++ty)
        for (uint32_t tx = 0; tx < p_->grid.tilesX; ++tx) {
            bake::TileId t;
            t.x = tx * plots;
            t.y = ty * plots;
            p_->tiles[static_cast<size_t>(ty) * p_->grid.tilesX + tx].style =
                static_cast<uint8_t>(bake::tileStyle(artTypes, mapW, mapH, wrapX, t));
        }
    return Status::Ok();
}

uint32_t Renderer::tileStyleAt(uint32_t tileX, uint32_t tileY) const {
    if (!p_ || !p_->haveField || tileX >= p_->grid.tilesX || tileY >= p_->grid.tilesY) return 0;
    return p_->tiles[static_cast<size_t>(tileY) * p_->grid.tilesX + tileX].style;
}

void Renderer::release() {
    delete p_;
    p_ = nullptr;
}

Status Renderer::init(ID3D11Device* device, const std::wstring& exePath, const std::wstring& gameDir) {
    return initFrom(device, exePath, nullptr, gameDir);
}

// issue 0198: the caller's index (the server's scene opens one for the terrain and the waves)
Status Renderer::init(ID3D11Device* device, const std::wstring& exePath, const gamefs::GameFiles& files) {
    return initFrom(device, exePath, &files, std::wstring());
}

Status Renderer::initFrom(ID3D11Device* device, const std::wstring& exePath, const gamefs::GameFiles* files,
                          const std::wstring& gameDir) {
    release();
    if (!device) return Status::fail(Err::BadLayout, "init: no device");
    shaders::ShaderSection sec;
    C5R_TRY(sec.open(exePath));
    const terrainsets::ShaderSetDesc* set = terrainsets::findSet("TerrainShader");
    if (!set) return Status::fail(Err::NotFound, "terrainsets has no TerrainShader");
    std::unique_ptr<Impl> p(new Impl());
    p->dev = device;
    device->AddRef();
    p->set = set;
    auto blob = [&](uint32_t va, shaders::Stage st, const shaders::ShaderBlob** out) -> Status {
        const shaders::ShaderBlob* b = sec.findByVa(va);
        char v[16];
        std::snprintf(v, sizeof v, "0x%08x", va);
        if (!b) return Status::fail(Err::NotFound, std::string("TerrainShader blob ") + v + " not in the .shader section");
        if (b->stage != st) return Status::fail(Err::BadLayout, std::string("TerrainShader blob ") + v + " has another stage");
        *out = b;
        return Status::Ok();
    };
    for (uint32_t pass = 0; pass < 3; ++pass) {
        const terrainsets::PassDesc* pd = nullptr;
        for (const terrainsets::PassDesc& d : set->passes)
            if (d.index == pass) pd = &d;
        if (!pd || !pd->sm50[0] || !pd->sm50[1] || !pd->sm50[2] || !pd->sm50[3])
            return Status::fail(Err::NotFound, "TerrainShader pass " + std::to_string(pass) + " has no SM5 VS/PS/HS/DS");
        const shaders::ShaderBlob *bv = nullptr, *bp = nullptr, *bh = nullptr, *bd = nullptr;
        C5R_TRY(blob(pd->sm50[0], shaders::Stage::Vertex, &bv));
        C5R_TRY(blob(pd->sm50[1], shaders::Stage::Pixel, &bp));
        C5R_TRY(blob(pd->sm50[2], shaders::Stage::Hull, &bh));
        C5R_TRY(blob(pd->sm50[3], shaders::Stage::Domain, &bd));
        if (!p->vs) C5R_TRY(hrOk(device->CreateVertexShader(bv->bytes, bv->size, nullptr, &p->vs), "CreateVertexShader (EXE)"));
        if (!p->ds) C5R_TRY(hrOk(device->CreateDomainShader(bd->bytes, bd->size, nullptr, &p->ds), "CreateDomainShader (EXE)"));
        C5R_TRY(hrOk(device->CreateHullShader(bh->bytes, bh->size, nullptr, &p->hs[pass]), "CreateHullShader (EXE)"));
        C5R_TRY(hrOk(device->CreatePixelShader(bp->bytes, bp->size, nullptr, &p->ps[pass]), "CreatePixelShader (EXE)"));
    }
    // own shaders
    ID3DBlob* b = nullptr;
    C5R_TRY(compile("ownPs", "ps_5_0", &b));
    Status st = hrOk(device->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &p->ownPs), "CreatePixelShader (own)");
    rel(b);
    C5R_TRY(st);
    C5R_TRY(compile("waterVs", "vs_5_0", &b));
    st = hrOk(device->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &p->waterVs), "CreateVertexShader (water)");
    rel(b);
    C5R_TRY(st);
    C5R_TRY(compile("waterPs", "ps_5_0", &b));
    st = hrOk(device->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &p->waterPs), "CreatePixelShader (water)");
    rel(b);
    C5R_TRY(st);
    // constant buffers: sizes from the layouts the EXE creates them with
    uint32_t tileSize = 0, frameSize = 0, fogSize = 0;
    for (const terrainsets::CbBinding& cb : set->cbuffers) {
        if (cb.slot == 0) tileSize = cb.layout->sizeBytes;
        if (cb.slot == 1) frameSize = cb.layout->sizeBytes;
        if (cb.slot == 2) fogSize = cb.layout->sizeBytes;
    }
    if (!tileSize || !frameSize || !fogSize) return Status::fail(Err::NotFound, "TerrainShader constant-buffer layouts missing");
    C5R_TRY(p->makeBuffer(tileSize, &p->cbTile));
    C5R_TRY(p->makeBuffer(frameSize, &p->cbFrame));
    C5R_TRY(p->makeBuffer(fogSize, &p->cbFog));
    C5R_TRY(p->makeBuffer(64, &p->cbOwn));
    C5R_TRY(p->makeBuffer(112, &p->cbWater));
    // the EXE's 13 sampler states (asset-pipeline.md 3.1)
    for (uint32_t i = 0; i < 13; ++i) {
        const terrainsets::SamplerStateDesc* s = terrainsets::samplerState(i);
        if (!s) return Status::fail(Err::NotFound, "sampler state " + std::to_string(i) + " not described");
        D3D11_SAMPLER_DESC d = {};
        d.Filter = static_cast<D3D11_FILTER>(s->filter);
        d.AddressU = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(s->address[0]);
        d.AddressV = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(s->address[1]);
        d.AddressW = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(s->address[2]);
        d.MaxAnisotropy = s->maxAnisotropy ? s->maxAnisotropy : 1;
        d.ComparisonFunc = static_cast<D3D11_COMPARISON_FUNC>(s->comparison);
        d.MinLOD = 0;
        d.MaxLOD = D3D11_FLOAT32_MAX;
        C5R_TRY(hrOk(device->CreateSamplerState(&d, &p->samp[i]), "CreateSamplerState"));
    }
    // constant textures
    const uint32_t zero = 0, one = 0xFFFFFFFFu, blk = 0xFF000000u;
    C5R_TRY(p->makeTex2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &zero, 4, &p->overlay));
    C5R_TRY(p->makeTex2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &one, 4, &p->white));
    C5R_TRY(p->makeTex2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &blk, 4, &p->black));
    // issue 0188: the distance-fog ramp t2, `assets/DistanceFog/FogRamp.dds` (MiscTextures.fpk, 128 x 8, 8 mips, A8R8G8B8)
    // as the EXE binds it at 0xDA12B4 (asset-pipeline.md 3.4) [L: its texels equal the EXE's t2 in 3601's captures]
    // issue 0189: the spec map t9 of pass 0, `assets/terrain/lighting/specscreenmap.dds` (BC3 1024 x 1024, 11 mips) as the
    // EXE binds it from 0xDA12C8 (asset-pipeline.md 3.4) [L: equal to the EXE's t9 in 3601's captures,
    // terrain-surface.md 8]
    // issue 0198: the caller's index when given; else one of gameDir, opened here as before (1.3-1.5 s on an install)
    gamefs::GameFiles own;
    if (!files && !gameDir.empty()) {
        C5R_TRY(own.open(gameDir));
        files = &own;
    }
    if (files) {
        const gamefs::GameFiles& fs = *files;
        auto load2d = [&](const char* name, ID3D11ShaderResourceView** out) -> Status {
            std::vector<uint8_t> bytes;
            Status rs = fs.read(name, bytes);
            if (!rs.ok()) return Status::fail(Err::NotFound, std::string(name) + ": " + rs.str());
            gamefs::DdsImage img;
            rs = gamefs::parseDds(bytes.data(), bytes.size(), img);
            if (!rs.ok()) return Status::fail(rs.code, std::string(name) + ": " + rs.msg);
            if (img.dim != gamefs::DdsDim::Tex2D || img.cube || img.arraySize != 1)
                return Status::fail(Err::Unsupported, std::string(name) + ": not a plain 2D texture");
            ID3D11Resource* tex = nullptr;
            rs = gamefs::createTexture(device, img, &tex, out);
            if (tex) tex->Release();
            return rs;
        };
        C5R_TRY(load2d("fogramp.dds", &p->fogRamp));
        C5R_TRY(load2d("specscreenmap.dds", &p->specMap));
        // issue 3953: the TerrainFOW LUT of each style (t6 with Options::fow), `TerrainFOW_<Style>_ColorKey.tga`
        // (asset-pipeline.md 3.5; one loader 0x681A00 for all 25 ColorKey volumes, fog-of-war.md 3.10); a missing file
        // keeps the identity volume, as the EXE's does; a file that does not parse fails init
        for (uint32_t s = 0; s < 5; ++s) {
            std::vector<uint8_t> tga;
            if (!fs.read("TerrainFOW_" + std::string(bake::styleName(s)) + "_ColorKey.tga", tga).ok()) continue;
            C5R_TRY(p->setFowLut(s, tga.data(), tga.size()));
        }
        // issue 0190: t7, the TerrainShadow ColorKey volume (ColorKeySystem 0x543BB0: 0xD90E74 + 4 * style), from the
        // loose `Assets/ColorKeys/TerrainShadow_<Style>_ColorKey.tga` by the EXE's TGA rule (c5r::bake::buildColorKeyLut).
        // Issue 4305: one volume per style, bound by the tile's style (as t6). A missing file keeps the identity volume, as
        // the EXE's loader does; a file that is there but does not read or parse is an init error naming it.
        for (uint32_t s = 0; s < 5; ++s) {
            const std::string shadowName = "TerrainShadow_" + std::string(bake::styleName(s)) + "_ColorKey.tga";
            if (!fs.exists(shadowName)) continue;
            std::vector<uint8_t> tga;
            Status rs = fs.read(shadowName, tga);
            if (!rs.ok()) return Status::fail(rs.code, shadowName + ": " + rs.msg);
            C5R_TRY(p->setShadowLut(s, tga.data(), tga.size()));
        }
    }
    {
        std::vector<uint32_t> v(32 * 32 * 32);
        for (uint32_t z = 0; z < 32; ++z)
            for (uint32_t y = 0; y < 32; ++y)
                for (uint32_t x = 0; x < 32; ++x)
                    v[(z * 32 + y) * 32 + x] = 0xFF000000u | ((z * 255 / 31) << 16) | ((y * 255 / 31) << 8) | (x * 255 / 31);
        D3D11_TEXTURE3D_DESC d = {};
        d.Width = d.Height = d.Depth = 32;
        d.MipLevels = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA s = {v.data(), 32 * 4, 32 * 32 * 4};
        ID3D11Texture3D* t = nullptr;
        C5R_TRY(hrOk(device->CreateTexture3D(&d, &s, &t), "CreateTexture3D (identity LUT)"));
        st = hrOk(device->CreateShaderResourceView(t, nullptr, &p->lut), "CreateShaderResourceView (LUT)");
        t->Release();
        C5R_TRY(st);
    }
    // states: blend/depth/raster of the terrain passes are not recovered from the EXE (asset-pipeline.md 3.7): opaque,
    // depth LESS_EQUAL with write, no culling.
    D3D11_RASTERIZER_DESC r = {};
    r.FillMode = D3D11_FILL_SOLID;
    r.CullMode = D3D11_CULL_NONE;
    r.DepthClipEnable = TRUE;
    C5R_TRY(hrOk(device->CreateRasterizerState(&r, &p->rs), "CreateRasterizerState"));
    r.FillMode = D3D11_FILL_WIREFRAME;
    C5R_TRY(hrOk(device->CreateRasterizerState(&r, &p->rsWire), "CreateRasterizerState (wire)"));
    D3D11_DEPTH_STENCIL_DESC ds = {};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    C5R_TRY(hrOk(device->CreateDepthStencilState(&ds, &p->dss), "CreateDepthStencilState"));
    // ticket 4551: the Screen Overlay pass binds no depth target and tests nothing (trees.md 11.1)
    ds.DepthEnable = FALSE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    C5R_TRY(hrOk(device->CreateDepthStencilState(&ds, &p->dssOff), "CreateDepthStencilState (screen overlay)"));
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    C5R_TRY(hrOk(device->CreateBlendState(&bd, &p->bs), "CreateBlendState"));
    for (GpuQuery& g : p->q) {
        D3D11_QUERY_DESC qd = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        C5R_TRY(hrOk(device->CreateQuery(&qd, &g.disjoint), "CreateQuery (disjoint)"));
        qd.Query = D3D11_QUERY_TIMESTAMP;
        C5R_TRY(hrOk(device->CreateQuery(&qd, &g.t0), "CreateQuery (timestamp)"));
        C5R_TRY(hrOk(device->CreateQuery(&qd, &g.t1), "CreateQuery (timestamp)"));
    }
    p_ = p.release();
    return Status::Ok();
}

Status Renderer::setHeightField(const heightfield::HeightGrid& g, uint32_t tilePlots) {
    if (!p_) return Status::fail(Err::NotFound, "setHeightField: init not done");
    if (g.mapW == 0 || g.mapH == 0 || g.mapW > heightfield::kMaxPlots || g.mapH > heightfield::kMaxPlots ||
        g.samplesX != g.mapW * heightfield::kSamplesPerPlot || g.samplesY != g.mapH * heightfield::kSamplesPerPlot)
        return Status::fail(Err::BadLayout, "setHeightField: samples are not plots * 64 (or plots 0 / above 256)");
    if (g.h.size() != static_cast<size_t>(g.samplesX) * g.samplesY)
        return Status::fail(Err::BadLayout, "setHeightField: h.size() " + std::to_string(g.h.size()) + " != samplesX*samplesY");
    if (!g.mat.empty() && g.mat.size() != g.h.size())
        return Status::fail(Err::BadLayout, "setHeightField: mat neither empty nor samplesX*samplesY");
    if (!(std::isfinite(g.sx) && std::isfinite(g.sy) && std::isfinite(g.sz) && g.sx > 0 && g.sy > 0 && g.sz > 0 &&
          std::isfinite(g.ox) && std::isfinite(g.oy) && std::isfinite(g.waterZ)))
        return Status::fail(Err::BadLayout, "setHeightField: scales or offsets not finite (or <= 0)");
    if (tilePlots == 0 || tilePlots > kMaxTilePlots)
        return Status::fail(Err::Unsupported, "setHeightField: tilePlots " + std::to_string(tilePlots) + " outside 1.." +
                                                  std::to_string(kMaxTilePlots));
    if (g.mapW % tilePlots || g.mapH % tilePlots)
        return Status::fail(Err::Unsupported, "setHeightField: map " + std::to_string(g.mapW) + " x " +
                                                  std::to_string(g.mapH) + " plots is not a multiple of " +
                                                  std::to_string(tilePlots));
    Impl& p = *p_;
    p.dropField();
    p.P = tilePlots * heightfield::kSamplesPerPlot;
    p.S = p.P + 2 * kTileBorder;
    p.grid.tilePlotsX = p.grid.tilePlotsY = tilePlots;
    p.grid.tilesX = g.mapW / tilePlots;
    p.grid.tilesY = g.mapH / tilePlots;
    p.grid.tileSamples = p.P;
    p.samplesX = g.samplesX;
    p.samplesY = g.samplesY;
    p.sx = g.sx;
    p.sy = g.sy;
    p.sz = g.sz;
    p.ox = g.ox;
    p.oy = g.oy;
    p.waterZ = g.waterZ;
    p.wrapX = g.wrapX;
    // issue 0191: one patch list per level: patch (a, b) -> control points i = a + c + 2, j = b + r + 2 (c, r = 0..3),
    // (n - 1)^2 patches of the level's vertex count n (exeLodGrid with d = P + 1: P, P/2, ... patches per edge)
    for (int l = 0; l <= kMaxLodLevel; ++l) {
        uint32_t n = 0;
        exeLodGrid(p.P + 1, l, &n, nullptr);
        std::vector<uint32_t> idx;
        idx.reserve(static_cast<size_t>(n) * n * 16);
        for (uint32_t b = 0; b < n; ++b)
            for (uint32_t a = 0; a < n; ++a)
                for (uint32_t r = 0; r < 4; ++r)
                    for (uint32_t c = 0; c < 4; ++c) idx.push_back((a + c + 2) + 256u * (b + r + 2));
        D3D11_BUFFER_DESC d = {};
        d.ByteWidth = static_cast<UINT>(idx.size() * 4);
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA s = {idx.data(), 0, 0};
        C5R_TRY(hrOk(p.dev->CreateBuffer(&d, &s, &p.ib[l]), "CreateBuffer (patch indices)"));
        p.ibPatches[l] = n * n;
    }
    const uint32_t S = p.S;
    // Issue 0191: the height texture of a tile is its EXE page (terrain-bake-passes.md 5.0): page (originX / 896,
    // originY / 896), window from sample px * 896 - 64, A8_UNORM texel = u16 >> 8 (the VS reads the ALPHA: z = byte / 255
    // * HexScale.z * 255 = byte * sz, the EXE's drawn height, drawnHeightAtSample; issue 0134), 11 mips by the EXE's box +
    // sharpen rule. A tile reads, at level L <= 4, samples from origin - 2 * 2^L to origin + P + 2 * 2^L: inside the page
    // for 1- and 2-plot tiles (64 and 128 divide 896); a 3-plot tile that does not fit gets its own window at origin - 64.
    // The CPU levels are kept until every tile's z bounds over the levels it can draw are known (the sharpen can leave
    // the range of mip 0), then dropped.
    const int32_t margin = 2 << kMaxLodLevel;   // 32 samples
    std::vector<bake::HeightPage> cpuPages;
    auto pageFor = [&](int32_t wx, int32_t wy, bool exe, uint32_t px, uint32_t py, uint32_t* index) -> Status {
        for (size_t i = 0; i < p.pages.size(); ++i)
            if (p.pages[i].wx == wx && p.pages[i].wy == wy) {
                *index = static_cast<uint32_t>(i);
                return Status::Ok();
            }
        bake::HeightPage hp;
        if (exe) {
            bake::HeightPageIndex pi;
            pi.px = px;
            pi.py = py;
            C5R_TRY(bake::buildHeightPage(g, pi, bake::kHeightPageMips, hp));
        } else {
            C5R_TRY(buildWindowPage(g, wx, wy, hp));
        }
        Page pg;
        pg.wx = wx;
        pg.wy = wy;
        pg.exe = exe;
        pg.px = px;
        pg.py = py;
        ID3D11Texture2D* tex = nullptr;
        C5R_TRY(bake::createHeightPageTexture(p.dev, hp, &tex, &pg.srv));
        if (tex) tex->Release();
        p.pages.push_back(pg);
        cpuPages.push_back(std::move(hp));
        *index = static_cast<uint32_t>(p.pages.size() - 1);
        return Status::Ok();
    };
    std::vector<uint8_t> m(g.mat.empty() ? 0 : static_cast<size_t>(S) * S);
    p.tiles.resize(static_cast<size_t>(p.grid.tilesX) * p.grid.tilesY);
    float zmaxAll = 0;
    for (uint32_t ty = 0; ty < p.grid.tilesY; ++ty)
        for (uint32_t tx = 0; tx < p.grid.tilesX; ++tx) {
            Tile& t = p.tiles[static_cast<size_t>(ty) * p.grid.tilesX + tx];
            const int c0 = static_cast<int>(tx * p.P), r0 = static_cast<int>(ty * p.P);
            {
                const uint32_t px = static_cast<uint32_t>(c0) / bake::kHeightPageStep, py = static_cast<uint32_t>(r0) / bake::kHeightPageStep;
                int32_t wx = static_cast<int32_t>(px * bake::kHeightPageStep) - static_cast<int32_t>(bake::kHeightPageMargin);
                int32_t wy = static_cast<int32_t>(py * bake::kHeightPageStep) - static_cast<int32_t>(bake::kHeightPageMargin);
                const int32_t N = static_cast<int32_t>(bake::kHeightPageSize), Pn = static_cast<int32_t>(p.P);
                const bool fits = c0 - margin - wx >= 0 && c0 + Pn + margin - wx < N && r0 - margin - wy >= 0 &&
                                  r0 + Pn + margin - wy < N;
                if (!fits) {
                    wx = c0 - static_cast<int32_t>(bake::kHeightPageMargin);
                    wy = r0 - static_cast<int32_t>(bake::kHeightPageMargin);
                }
                C5R_TRY(pageFor(wx, wy, fits, px, py, &t.page));
            }
            // z bounds over the levels 0..kMaxLodLevel of the texels the tile can read
            p.tileBounds(t, c0, r0, cpuPages[t.page], g.sz);
            if (!m.empty()) C5R_TRY(p.tileMaterial(t, g, c0, r0, m));
            zmaxAll = std::max(zmaxAll, t.zmax);
            p.tileLand(t, g, c0, r0);
        }
    p.zmaxAll = zmaxAll > 0 ? zmaxAll : 1.0f;
    // issue 0191: the EXE tiles its cull lists (height class 3), for the LOD rule
    {
        const uint32_t etx = g.samplesX / 64, ety = g.samplesY / 64;
        p.exeListed.assign(static_cast<size_t>(etx) * ety, 0);
        for (uint32_t y = 0; y < ety; ++y)
            for (uint32_t x = 0; x < etx; ++x) p.exeListed[static_cast<size_t>(y) * etx + x] = exeTileClass3(g, x, y) ? 1 : 0;
    }
    p.haveField = true;
    return Status::Ok();
}

// ---- ticket 4909: the region update (live-terrain.md 8.14) ----------------------------------------------------------

bool spanMeetsWrappedWindow(int64_t lo, int64_t hi, int64_t w0, int64_t n, int64_t period) {
    if (hi <= lo || period <= 0) return false;
    if (n >= period || hi - lo >= period) return true;
    // the window's samples are (w0 + i) mod period, i in [0, n): a sample s of [lo, hi) is in it when (s - w0) mod period
    // < n. The span's offsets are d0 .. d0 + len - 1 (mod period): they meet [0, n) when d0 < n or when they run past
    // period (and so through 0).
    int64_t d0 = (lo - w0) % period;
    if (d0 < 0) d0 += period;
    return d0 < n || d0 + (hi - lo) > period;
}

namespace {
Status checkRects(const heightfield::HeightGrid& g, const std::vector<SampleRect>& rects) {
    for (const SampleRect& r : rects)
        if (r.x0 >= r.x1 || r.y0 >= r.y1 || r.x1 > g.samplesX || r.y1 > g.samplesY)
            return Status::fail(Err::BadLayout, "region: rect (" + std::to_string(r.x0) + ", " + std::to_string(r.y0) + ") - (" +
                                                    std::to_string(r.x1) + ", " + std::to_string(r.y1) + ") empty or outside " +
                                                    std::to_string(g.samplesX) + " x " + std::to_string(g.samplesY));
    return Status::Ok();
}
// a window [w0, w0 + n) in x (wrapping by samplesX when wrapX, else clamped: a clamped window's edge sample stands for
// every column beyond it) and [v0, v0 + m) in y (clamped) meets a rect
bool windowMeetsRects(const heightfield::HeightGrid& g, const std::vector<SampleRect>& rects, int64_t w0, int64_t n, int64_t v0,
                      int64_t m, bool wrapX) {
    const int64_t X = g.samplesX, Y = g.samplesY;
    int64_t ya = std::max<int64_t>(0, v0), yb = std::min<int64_t>(Y, v0 + m);
    if (v0 + m <= 0) ya = 0, yb = 1;          // clamped to row 0
    if (v0 >= Y) ya = Y - 1, yb = Y;          // clamped to the last row
    for (const SampleRect& r : rects) {
        if (static_cast<int64_t>(r.y1) <= ya || static_cast<int64_t>(r.y0) >= yb) continue;
        if (wrapX) {
            if (spanMeetsWrappedWindow(r.x0, r.x1, w0, n, X)) return true;
        } else {
            int64_t xa = std::max<int64_t>(0, w0), xb = std::min<int64_t>(X, w0 + n);
            if (w0 + n <= 0) xa = 0, xb = 1;
            if (w0 >= X) xa = X - 1, xb = X;
            if (static_cast<int64_t>(r.x1) > xa && static_cast<int64_t>(r.x0) < xb) return true;
        }
    }
    return false;
}
} // namespace

Status pagesForRects(const heightfield::HeightGrid& g, const std::vector<SampleRect>& rects,
                     std::vector<bake::HeightPageIndex>& out) {
    out.clear();
    uint32_t pagesX = 0, pagesY = 0;
    C5R_TRY(bake::heightPageCount(g, pagesX, pagesY));
    C5R_TRY(checkRects(g, rects));
    const int64_t N = bake::kHeightPageSize;
    for (uint32_t py = 0; py < pagesY; ++py)
        for (uint32_t px = 0; px < pagesX; ++px) {
            const int64_t sx0 = static_cast<int64_t>(px) * bake::kHeightPageStep - bake::kHeightPageMargin;
            const int64_t sy0 = static_cast<int64_t>(py) * bake::kHeightPageStep - bake::kHeightPageMargin;
            bool hit = false;
            for (const SampleRect& r : rects)
                if (spanMeetsWrappedWindow(r.x0, r.x1, sx0, N, g.samplesX) && spanMeetsWrappedWindow(r.y0, r.y1, sy0, N, g.samplesY)) {
                    hit = true;
                    break;
                }
            if (hit) {
                bake::HeightPageIndex i;
                i.px = px;
                i.py = py;
                out.push_back(i);
            }
        }
    return Status::Ok();
}

Status Renderer::updateHeightRegion(const heightfield::HeightGrid& g, const std::vector<SampleRect>& rects,
                                    const std::vector<bake::HeightPage>* built, RegionUpdateStats* stats) {
    const auto t0 = std::chrono::steady_clock::now();
    auto ms = [](std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    RegionUpdateStats st;
    if (!p_) return Status::fail(Err::NotFound, "updateHeightRegion: init not done");
    Impl& p = *p_;
    if (!p.haveField) return Status::fail(Err::NotFound, "updateHeightRegion: no field");
    const uint32_t tp = p.grid.tilePlotsX;
    if (g.samplesX != p.samplesX || g.samplesY != p.samplesY || !tp || g.mapW != p.grid.tilesX * tp ||
        g.mapH != p.grid.tilesY * tp || g.h.size() != static_cast<size_t>(g.samplesX) * g.samplesY ||
        (!g.mat.empty() && g.mat.size() != g.h.size()) || (g.mat.empty() != (p.tiles.empty() || !p.tiles[0].mat)) ||
        g.sx != p.sx || g.sy != p.sy || g.sz != p.sz || g.ox != p.ox || g.oy != p.oy || g.waterZ != p.waterZ || g.wrapX != p.wrapX)
        return Status::fail(Err::BadLayout, "updateHeightRegion: not the current field's layout");
    C5R_TRY(checkRects(g, rects));
    if (built)
        for (const bake::HeightPage& hp : *built)
            if (hp.levels.size() != bake::kHeightPageMips || hp.levels[0].size() != static_cast<size_t>(bake::kHeightPageSize) * bake::kHeightPageSize)
                return Status::fail(Err::BadLayout, "updateHeightRegion: a built page is not 1024 x 1024 with 11 levels");
    // 1. the pages whose window meets a rect: built (or taken from the worker), then made; nothing replaced before all are
    const int64_t N = bake::kHeightPageSize;
    std::vector<uint32_t> which;
    for (uint32_t i = 0; i < p.pages.size(); ++i) {
        const Page& pg = p.pages[i];
        bool hit = false;
        for (const SampleRect& r : rects)
            if (spanMeetsWrappedWindow(r.x0, r.x1, pg.wx, N, g.samplesX) && spanMeetsWrappedWindow(r.y0, r.y1, pg.wy, N, g.samplesY)) {
                hit = true;
                break;
            }
        if (hit) which.push_back(i);
    }
    std::vector<bake::HeightPage> cpu(which.size());
    std::vector<ID3D11ShaderResourceView*> srvs(which.size(), nullptr);
    auto dropNew = [&]() {
        for (auto& s : srvs) rel(s);
    };
    const auto tb = std::chrono::steady_clock::now();
    for (size_t k = 0; k < which.size(); ++k) {
        const Page& pg = p.pages[which[k]];
        bool have = false;
        if (built && pg.exe)
            for (const bake::HeightPage& hp : *built)
                if (hp.index.px == pg.px && hp.index.py == pg.py) {
                    cpu[k] = hp;
                    have = true;
                    st.pagesFromWorker++;
                    break;
                }
        Status bs = Status::Ok();
        if (!have) {
            if (pg.exe) {
                bake::HeightPageIndex pi;
                pi.px = pg.px;
                pi.py = pg.py;
                bs = bake::buildHeightPage(g, pi, bake::kHeightPageMips, cpu[k]);
            } else {
                bs = buildWindowPage(g, pg.wx, pg.wy, cpu[k]);
            }
        }
        if (!bs.ok()) {
            dropNew();
            return bs;
        }
    }
    const auto tu = std::chrono::steady_clock::now();
    st.buildMs = ms(tb, tu);
    for (size_t k = 0; k < which.size(); ++k) {
        ID3D11Texture2D* tex = nullptr;
        const Status cs = bake::createHeightPageTexture(p.dev, cpu[k], &tex, &srvs[k]);
        if (tex) tex->Release();
        if (!cs.ok()) {
            dropNew();
            return cs;
        }
    }
    std::vector<int32_t> slot(p.pages.size(), -1);
    for (size_t k = 0; k < which.size(); ++k) {
        rel(p.pages[which[k]].srv);
        p.pages[which[k]].srv = srvs[k];
        slot[which[k]] = static_cast<int32_t>(k);
    }
    st.pagesRebuilt = static_cast<uint32_t>(which.size());
    const auto tt = std::chrono::steady_clock::now();
    st.uploadMs = ms(tu, tt);
    // 2. the tiles: bounds (tiles on a rebuilt page), material and land (tiles whose window meets a rect)
    const uint32_t S = p.S;
    const int Pn = static_cast<int>(p.P);
    std::vector<uint8_t> m(g.mat.empty() ? 0 : static_cast<size_t>(S) * S);
    Status ts = Status::Ok();
    for (uint32_t ty = 0; ty < p.grid.tilesY && ts.ok(); ++ty)
        for (uint32_t tx = 0; tx < p.grid.tilesX; ++tx) {
            Tile& t = p.tiles[static_cast<size_t>(ty) * p.grid.tilesX + tx];
            const int c0 = static_cast<int>(tx * p.P), r0 = static_cast<int>(ty * p.P);
            t.grayMark = 0;
            if (slot[t.page] >= 0) {
                p.tileBounds(t, c0, r0, cpu[static_cast<size_t>(slot[t.page])], g.sz);
                st.tilesBounds++;
            }
            if (!m.empty() && windowMeetsRects(g, rects, c0 - static_cast<int>(kTileBorder), S, r0 - static_cast<int>(kTileBorder), S, g.wrapX)) {
                rel(t.mat);
                ts = p.tileMaterial(t, g, c0, r0, m);
                if (!ts.ok()) break;
                st.tilesMat++;
            }
            if (windowMeetsRects(g, rects, c0 - 4, Pn + 8, r0 - 4, Pn + 8, g.wrapX)) {
                p.tileLand(t, g, c0, r0);
                st.tilesLand++;
            }
        }
    if (!ts.ok()) {
        p.dropField();   // a tile without its material texture: no half-updated field (the caller re-applies g whole)
        return ts;
    }
    float zmaxAll = 0;
    for (const Tile& t : p.tiles) zmaxAll = std::max(zmaxAll, t.zmax);
    p.zmaxAll = zmaxAll > 0 ? zmaxAll : 1.0f;
    {   // the EXE list: rows ty * 64 + 4 .. + 66 mod samplesY, columns mod samplesX (exeTileClass3 wraps both)
        const uint32_t etx = g.samplesX / 64, ety = g.samplesY / 64;
        for (uint32_t y = 0; y < ety; ++y)
            for (uint32_t x = 0; x < etx; ++x) {
                bool hit = false;
                for (const SampleRect& r : rects)
                    if (spanMeetsWrappedWindow(r.x0, r.x1, static_cast<int64_t>(x) * 64 + 4, 63, g.samplesX) &&
                        spanMeetsWrappedWindow(r.y0, r.y1, static_cast<int64_t>(y) * 64 + 4, 63, g.samplesY)) {
                        hit = true;
                        break;
                    }
                if (!hit) continue;
                p.exeListed[static_cast<size_t>(y) * etx + x] = exeTileClass3(g, x, y) ? 1 : 0;
                st.exeTiles++;
            }
    }
    p.lod = LodState();   // as setHeightField's dropField
    const auto te = std::chrono::steady_clock::now();
    st.tilesMs = ms(tt, te);
    st.totalMs = ms(t0, te);
    if (stats) *stats = st;
    return Status::Ok();
}

namespace {
uint64_t fnv(uint64_t h, const void* d, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(d);
    for (size_t i = 0; i < n; ++i) {
        h ^= b[i];
        h *= 0x100000001b3ull;
    }
    return h;
}
// every level of a 2D texture's view, read back through a staging copy (tests and the bench only)
Status readBackSrv(ID3D11Device* dev, ID3D11ShaderResourceView* srv, uint64_t& h) {
    if (!srv) {
        h = fnv(h, "null", 4);
        return Status::Ok();
    }
    ID3D11Resource* res = nullptr;
    srv->GetResource(&res);
    ID3D11Texture2D* tex = nullptr;
    const HRESULT qi = res ? res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex)) : E_FAIL;
    rel(res);
    if (FAILED(qi) || !tex) return Status::fail(Err::Device, "fieldDigest: not a 2D texture");
    D3D11_TEXTURE2D_DESC d = {};
    tex->GetDesc(&d);
    D3D11_TEXTURE2D_DESC s = d;
    s.Usage = D3D11_USAGE_STAGING;
    s.BindFlags = 0;
    s.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    s.MiscFlags = 0;
    ID3D11Texture2D* stg = nullptr;
    if (FAILED(dev->CreateTexture2D(&s, nullptr, &stg))) {
        tex->Release();
        return Status::fail(Err::Device, "fieldDigest: staging texture");
    }
    ID3D11DeviceContext* ctx = nullptr;
    dev->GetImmediateContext(&ctx);
    ctx->CopyResource(stg, tex);
    Status out = Status::Ok();
    const uint32_t bpp = d.Format == DXGI_FORMAT_A8_UNORM || d.Format == DXGI_FORMAT_R8_UNORM ? 1 : 4;
    for (uint32_t mip = 0; mip < d.MipLevels && out.ok(); ++mip) {
        D3D11_MAPPED_SUBRESOURCE map = {};
        if (FAILED(ctx->Map(stg, mip, D3D11_MAP_READ, 0, &map))) {
            out = Status::fail(Err::Device, "fieldDigest: map");
            break;
        }
        const uint32_t w = std::max(1u, d.Width >> mip), hh = std::max(1u, d.Height >> mip);
        for (uint32_t y = 0; y < hh; ++y) h = fnv(h, static_cast<const uint8_t*>(map.pData) + static_cast<size_t>(y) * map.RowPitch, w * bpp);
        ctx->Unmap(stg, mip);
    }
    ctx->Release();
    stg->Release();
    tex->Release();
    return out;
}
} // namespace

Status digestTexture(ID3D11Device* dev, ID3D11ShaderResourceView* srv, uint64_t& h) {
    if (!dev) return Status::fail(Err::BadLayout, "digestTexture: no device");
    return readBackSrv(dev, srv, h);
}

Status Renderer::fieldDigest(std::vector<uint64_t>& out, bool withCaches) const {
    out.clear();
    if (!p_ || !p_->haveField) return Status::fail(Err::NotFound, "fieldDigest: no field");
    const Impl& p = *p_;
    const uint64_t seed = 0xcbf29ce484222325ull;
    uint64_t h = seed;
    for (const Page& pg : p.pages) {
        h = fnv(h, &pg.wx, sizeof pg.wx);
        h = fnv(h, &pg.wy, sizeof pg.wy);
        C5R_TRY(readBackSrv(p.dev, pg.srv, h));
    }
    out.push_back(h);   // [0] pages
    h = seed;
    uint64_t hm = seed;
    for (const Tile& t : p.tiles) {
        h = fnv(h, &t.page, sizeof t.page);
        h = fnv(h, &t.zmin, sizeof t.zmin);
        h = fnv(h, &t.zmax, sizeof t.zmax);
        h = fnv(h, &t.land, sizeof t.land);
        h = fnv(h, &t.style, sizeof t.style);
        h = fnv(h, &t.grayMark, sizeof t.grayMark);
        C5R_TRY(readBackSrv(p.dev, t.mat, hm));
    }
    out.push_back(h);    // [1] tiles
    out.push_back(hm);   // [2] material textures
    out.push_back(fnv(seed, p.exeListed.data(), p.exeListed.size()));   // [3]
    h = fnv(seed, &p.zmaxAll, sizeof p.zmaxAll);
    h = fnv(h, &p.lod, sizeof p.lod);
    h = fnv(h, &p.grid, sizeof p.grid);
    for (int l = 0; l <= kMaxLodLevel; ++l) h = fnv(h, &p.ibPatches[l], sizeof p.ibPatches[l]);
    out.push_back(h);   // [4] scalars
    if (withCaches) {
        h = seed;
        for (const Tile& t : p.tiles) {
            const uintptr_t a = reinterpret_cast<uintptr_t>(t.cache.color), b = reinterpret_cast<uintptr_t>(t.cache.second);
            h = fnv(h, &a, sizeof a);
            h = fnv(h, &b, sizeof b);
            h = fnv(h, &t.cacheKind, sizeof t.cacheKind);
        }
        out.push_back(h);   // [5] cache pointers
    }
    return Status::Ok();
}

ID3D11ShaderResourceView* Renderer::fogRamp() const { return p_ ? p_->fogRamp : nullptr; }
ID3D11ShaderResourceView* Renderer::specMap() const { return p_ ? p_->specMap : nullptr; }
ID3D11ShaderResourceView* Renderer::shadowLut(uint32_t style) const {
    if (!p_ || style >= 5) return nullptr;
    return p_->shadowLut[style] ? p_->shadowLut[style] : p_->lut;
}
bool Renderer::shadowLutFromFile() const { return p_ && (p_->shadowLutFile & 1u); }
uint32_t Renderer::shadowLutsFromFile() const { return p_ ? p_->shadowLutFile : 0; }
Status Renderer::setShadowLut(uint32_t style, const uint8_t* tga, size_t size) {
    if (!p_) return Status::fail(Err::NotFound, "setShadowLut: init not done");
    return p_->setShadowLut(style, tga, size);
}

TileGrid Renderer::tileGrid() const { return p_ && p_->haveField ? p_->grid : TileGrid(); }

Status Renderer::setTileCache(uint32_t tileX, uint32_t tileY, const TileCache& c) {
    if (!p_ || !p_->haveField) return Status::fail(Err::NotFound, "setTileCache: no field set");
    if (tileX >= p_->grid.tilesX || tileY >= p_->grid.tilesY)
        return Status::fail(Err::BadLayout, "setTileCache: tile (" + std::to_string(tileX) + ", " + std::to_string(tileY) +
                                                ") outside " + std::to_string(p_->grid.tilesX) + " x " +
                                                std::to_string(p_->grid.tilesY));
    Tile& t = p_->tiles[static_cast<size_t>(tileY) * p_->grid.tilesX + tileX];
    if (c.color) c.color->AddRef();
    if (c.second) c.second->AddRef();
    rel(t.cache.color);
    rel(t.cache.second);
    t.cache = c;
    t.cacheKind = static_cast<uint8_t>(cacheKindOf(c.color));
    return Status::Ok();
}

Status Renderer::gpuTime(ID3D11DeviceContext* ctx, bool wait, double* ms) {
    if (!p_ || !ctx) return Status::fail(Err::NotFound, "gpuTime: init not done");
    p_->pollQueries(ctx, wait);
    if (p_->lastGpuMs < 0) return Status::fail(Err::NotFound, "gpuTime: no timestamps available");
    if (ms) *ms = p_->lastGpuMs;
    return Status::Ok();
}

Status Renderer::draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv,
                      const Camera& cam, const Options& o, FrameStats* stats) {
    return drawPass(ctx, rtv, dsv, cam, o, stats, false);
}

void Renderer::gpuTimingBegin(ID3D11DeviceContext* ctx) {
    if (!p_ || !ctx) return;
    p_->listQ = nullptr;   // an unclosed begin is dropped (its set stays not issued)
    p_->pollQueries(ctx, false);
    for (GpuQuery& g : p_->q)
        if (!g.issued && g.disjoint) { p_->listQ = &g; break; }
    if (p_->listQ) {
        ctx->Begin(p_->listQ->disjoint);
        ctx->End(p_->listQ->t0);
    }
}

void Renderer::gpuTimingEnd(ID3D11DeviceContext* ctx) {
    GpuQuery* gq = p_ ? p_->listQ : nullptr;
    if (!gq || !ctx) return;
    ctx->End(gq->t1);
    ctx->End(gq->disjoint);
    gq->issued = true;
    gq->serial = ++p_->serial;
    p_->listQ = nullptr;
}

Status Renderer::drawScreenOverlay(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, const Camera& cam,
                                   const Options& o, FrameStats* stats) {
    return drawPass(ctx, rtv, nullptr, cam, o, stats, true);
}

// The body of draw() and, with screenOverlay (ticket 4551, trees.md 11.2), of drawScreenOverlay(): the same tiles, cull,
// order and wrap copies; the Screen Overlay pass differs only where `screen` is tested below.
Status Renderer::drawPass(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv,
                          const Camera& cam, const Options& o, FrameStats* stats, bool screenOverlay) {
    const auto tStart = std::chrono::steady_clock::now();
    if (!p_) return Status::fail(Err::NotFound, "draw: init not done");
    if (!p_->haveField) return Status::fail(Err::NotFound, "draw: no field set");
    if (!ctx || !rtv) return Status::fail(Err::BadLayout, "draw: no context or render target");
    if (cam.width == 0 || cam.height == 0) return Status::fail(Err::BadLayout, "draw: camera width or height 0");
    if (!finite16(cam.view) || !finite16(cam.proj) || !finite16(cam.camWorld))
        return Status::fail(Err::BadLayout, "draw: a camera matrix is not finite");
    if (o.pass < -1 || o.pass > 2) return Status::fail(Err::BadLayout, "draw: pass " + std::to_string(o.pass) + " outside 0..2");
    if (!std::isfinite(o.tessFactor) || o.tessFactor < 0 || o.tessFactor > 64 || o.tileD4 > 1024)
        return Status::fail(Err::BadLayout, "draw: tessFactor outside [0, 64] or tileD4 above 1024");
    if ((o.lodLevel != kLodExe && (o.lodLevel < 0 || o.lodLevel > kMaxLodLevel)) || o.lodTriSize < 1 || o.lodTriSize > 64 ||
        o.lodSubdiv < 1 || o.lodSubdiv > 64)
        return Status::fail(Err::BadLayout, "draw: lodLevel " + std::to_string(o.lodLevel) + " neither kLodExe nor 0.." +
                                                std::to_string(kMaxLodLevel) + ", or lodTriSize / lodSubdiv outside 1..64");
    if (!std::isfinite(o.waterZ)) return Status::fail(Err::BadLayout, "draw: waterZ not finite");
    // issue 0188: the fog inputs
    {
        bool fin = std::isfinite(o.fog.density) && std::isfinite(o.fog.scale) && std::isfinite(o.fog.w);
        for (float c : o.fog.color) fin = fin && std::isfinite(c);
        if (!fin) return Status::fail(Err::BadLayout, "draw: a distance-fog value is not finite");
    }
    // issue 3704: the overlay input (overlay-system.md 4-5)
    for (int k = 0; k < 3; ++k)
        if (!finite16(o.overlay.proj[k]))
            return Status::fail(Err::BadLayout, "draw: overlay matrix of wrap copy " + std::to_string(k - 1) + " not finite");
    float overlaySize = 2048.0f;   // the EXE's target size; the bound texture's width when one is given
    if (o.overlay.srv) {
        D3D11_SHADER_RESOURCE_VIEW_DESC sd;
        o.overlay.srv->GetDesc(&sd);
        if (sd.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || sd.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
            return Status::fail(Err::BadLayout, "draw: overlay view is not a Texture2D R8G8B8A8_UNORM view (dimension " +
                                                    std::to_string(static_cast<int>(sd.ViewDimension)) + ", format " +
                                                    std::to_string(static_cast<int>(sd.Format)) + ")");
        ID3D11Resource* res = nullptr;
        o.overlay.srv->GetResource(&res);
        ID3D11Texture2D* tex = nullptr;
        if (res && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex))) && tex) {
            D3D11_TEXTURE2D_DESC td;
            tex->GetDesc(&td);
            overlaySize = static_cast<float>(td.Width);
            tex->Release();
        }
        rel(res);
    }
    // issue 3953: the fog of war's inputs (fog-of-war.md 7.4); checked before any state changes
    if (o.fow.enabled) {
        if (!o.fow.mask) return Status::fail(Err::BadLayout, "draw: fow on without a mask (t4)");
        if (!o.fow.cloudBase) return Status::fail(Err::BadLayout, "draw: fow on without a cloud base (t5)");
        for (float a : o.fow.anim)
            if (!std::isfinite(a)) return Status::fail(Err::BadLayout, "draw: FOWAnim not finite");
        D3D11_SHADER_RESOURCE_VIEW_DESC sd;
        o.fow.mask->GetDesc(&sd);
        if (sd.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || sd.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
            return Status::fail(Err::BadLayout, "draw: fow mask view is not a Texture2D R8G8B8A8_UNORM view (dimension " +
                                                    std::to_string(static_cast<int>(sd.ViewDimension)) + ", format " +
                                                    std::to_string(static_cast<int>(sd.Format)) + ")");
        o.fow.cloudBase->GetDesc(&sd);
        if (sd.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D)
            return Status::fail(Err::BadLayout, "draw: fow cloud base view is not a Texture2D view (dimension " +
                                                    std::to_string(static_cast<int>(sd.ViewDimension)) + ")");
    }
    // issue 0190: the shadow input (terrain-surface.md 9)
    for (int k = 0; k < 3; ++k)
        if (!finite16(o.shadow.matrix[k]))
            return Status::fail(Err::BadLayout, "draw: shadow matrix of wrap copy " + std::to_string(k - 1) + " not finite");
    if (!std::isfinite(o.shadow.strength) || o.shadow.strength < 0.0f || o.shadow.strength > 1.0f)
        return Status::fail(Err::BadLayout, "draw: shadow strength not finite or outside [0, 1]");
    if (o.shadow.srv) {
        D3D11_SHADER_RESOURCE_VIEW_DESC sd;
        o.shadow.srv->GetDesc(&sd);
        // issue 4308: R16G16B16A16_FLOAT too (the EXE's quality 1, where ShadowBlur is ShadowRaw: the PS reads .r)
        if (sd.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
            (sd.Format != DXGI_FORMAT_R16_UNORM && sd.Format != DXGI_FORMAT_R16G16B16A16_FLOAT))
            return Status::fail(Err::BadLayout, "draw: shadow view is not a Texture2D R16_UNORM or R16G16B16A16_FLOAT view (dimension " +
                                                    std::to_string(static_cast<int>(sd.ViewDimension)) + ", format " +
                                                    std::to_string(static_cast<int>(sd.Format)) + ")");
    }
    float inv[16];
    if (!inverse44(cam.view, inv)) return Status::fail(Err::BadLayout, "draw: view matrix not invertible");
    Impl& p = *p_;
    // issue 3953: the mask is sampled at the overlay's coordinate, so with the fog on the overlay's matrices are bound
    // also when no overlay texture is given (the overlay itself stays the transparent 1x1 then)
    Overlay ov = o.overlay;
    if (o.fow.enabled && !ov.srv) ov.srv = p.overlay;
    // target size and format
    {
        ID3D11Resource* res = nullptr;
        rtv->GetResource(&res);
        ID3D11Texture2D* tex = nullptr;
        HRESULT h = res ? res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex)) : E_FAIL;
        rel(res);
        if (FAILED(h) || !tex) return Status::fail(Err::BadLayout, "draw: render target is not a 2D texture");
        D3D11_TEXTURE2D_DESC d;
        tex->GetDesc(&d);
        tex->Release();
        if (d.Width != cam.width || d.Height != cam.height)
            return Status::fail(Err::BadLayout, "draw: camera " + std::to_string(cam.width) + " x " + std::to_string(cam.height) +
                                                    " but target " + std::to_string(d.Width) + " x " + std::to_string(d.Height));
        D3D11_RENDER_TARGET_VIEW_DESC rd;
        rtv->GetDesc(&rd);
        const bool worldMode = o.color == ColorSource::WorldPos ||
                               (o.color == ColorSource::Caches && o.fallback == ColorSource::WorldPos);
        if (worldMode && rd.Format != DXGI_FORMAT_R32G32B32A32_FLOAT && !screenOverlay)
            return Status::fail(Err::BadLayout, "draw: WorldPos needs an R32G32B32A32_FLOAT target");
        if (screenOverlay && rd.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
            return Status::fail(Err::BadLayout, "drawScreenOverlay: the target view is not R8G8B8A8_UNORM (format " +
                                                    std::to_string(static_cast<int>(rd.Format)) + ")");
    }
    // depth (ticket 4551: none for the Screen Overlay pass, which clears its target to 0 as stock)
    if (screenOverlay) {
        dsv = nullptr;
        const float zero4[4] = {0, 0, 0, 0};
        ctx->ClearRenderTargetView(rtv, zero4);
    } else if (!dsv) {
        if (!p.depthDsv || p.depthW != cam.width || p.depthH != cam.height) {
            rel(p.depthDsv);
            rel(p.depthTex);
            D3D11_TEXTURE2D_DESC d = {};
            d.Width = cam.width;
            d.Height = cam.height;
            d.MipLevels = 1;
            d.ArraySize = 1;
            d.Format = DXGI_FORMAT_D32_FLOAT;
            d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
            C5R_TRY(hrOk(p.dev->CreateTexture2D(&d, nullptr, &p.depthTex), "CreateTexture2D (depth)"));
            C5R_TRY(hrOk(p.dev->CreateDepthStencilView(p.depthTex, nullptr, &p.depthDsv), "CreateDepthStencilView"));
            p.depthW = cam.width;
            p.depthH = cam.height;
        }
        dsv = p.depthDsv;
        ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    } else if (o.clearDepth) {
        ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    }
    if (o.clearRgba && !screenOverlay) {
        float c[4];
        rgba(o.clearRgba, c);
        ctx->ClearRenderTargetView(rtv, c);
    }

    // GPU timing: read what is ready, then open this frame's queries (not for the Screen Overlay pass: gpuTime stays the
    // scene draw's)
    GpuQuery* gq = nullptr;
    if (!screenOverlay && o.gpuTiming) {   // ticket 0753: not on a deferred context (gpuTimingBegin / End around its execute)
        p.pollQueries(ctx, false);
        for (GpuQuery& g : p.q)
            if (!g.issued) { gq = &g; break; }
    }
    if (gq) {
        ctx->Begin(gq->disjoint);
        ctx->End(gq->t0);
    }

    // issue 0189: o.pass -1 = the EXE's choice per tile (0x5C37E0): pass 0 where the tile has a t8, else pass 1 (the HS of
    // both is 0x067f98f0, the PS differs); 0..2 forces that pass for every tile
    const int forcedPass = o.pass;
    const int pass = o.pass < 0 ? 1 : o.pass;   // the pass bound before the first tile (and every tile without a t8)
    const bool specOn = o.spec && p.specMap != nullptr;
    float vp[16];
    mul44(cam.view, cam.proj, vp);
    // issue 0191: the LOD of this frame (terrain.h, kLodExe; terrain-surface.md 7). The state advances only when the
    // draw succeeds (committed at the end).
    LodState lodNext = p.lod;
    LodFrame lf;
    std::vector<uint32_t> listTiles;   // issue 0189: the EXE's visible list (64-sample tiles), for the pass counts
    bool lodAccepted = false;
    int level = 0;
    float lodMip = 0.0f, tess = 0.0f;
    if (o.lodLevel == kLodExe) {
        // ticket 4551: the Screen Overlay pass takes the state draw() left (no list, no update)
        if (!screenOverlay) {
            C5R_TRY(exeFrameLod(p.samplesX, p.samplesY, p.sx, p.sy, p.sz, p.ox, p.oy, vp, o.lodWidth ? o.lodWidth : cam.width,
                                o.lodTriSize, o.lodSubdiv, lf, p.exeListed.data(), &listTiles));
            if (lf.visible > 0) lodAccepted = exeLodUpdate(lodNext, lf.minLod, o.lodSubdiv);
        }
        lodMip = lodNext.lodMip;
        level = std::min(std::max(static_cast<int>(std::floor(lodMip)), 0), kMaxLodLevel);
        tess = o.tessFactor > 0 ? o.tessFactor : static_cast<float>(lodNext.tessFactor);
    } else {
        level = o.lodLevel;
        lodMip = static_cast<float>(level);
        tess = o.tessFactor > 0 ? o.tessFactor : static_cast<float>(o.lodSubdiv);
    }
    if (tess > 64.0f) tess = 64.0f;   // the hull shader's maximum (TessFactor (1 << 1) * Subdiv 64 at L < 0)
    // ticket 4551 (trees.md 11.1): the Screen Overlay pass draws the patch list of level L + 1 with that level's LodStep
    // (twice L's) at L's LodMip; at the clamp (L = kMaxLodLevel) level 4 itself [I: stock has no level 5]
    const int drawLevel = screenOverlay ? std::min(level + 1, kMaxLodLevel) : level;
    float lodStepV = 1.0f;
    exeLodGrid(p.P + 1, drawLevel, nullptr, &lodStepV);

    // TerrainFrame (asset-pipeline.md 3.3)
    const terrainsets::CbLayout* lTile = nullptr;
    const terrainsets::CbLayout* lFrame = nullptr;
    const terrainsets::CbLayout* lFog = nullptr;
    for (const terrainsets::CbBinding& b : p.set->cbuffers) {
        if (b.slot == 0) lTile = b.layout;
        if (b.slot == 1) lFrame = b.layout;
        if (b.slot == 2) lFog = b.layout;
    }
    std::vector<uint8_t> frame(lFrame->sizeBytes, 0), fog(lFog->sizeBytes, 0), tile(lTile->sizeBytes, 0);
    const float basis[16] = {-1.0f / 8, 7.0f / 8, -7.0f / 8, 1.0f / 8, 3.0f / 8, -1.5f, 9.0f / 8, 0,
                             -3.0f / 8, 0, 3.0f / 8, 0, 1.0f / 8, 3.0f / 4, 1.0f / 8, 0};
    const float one = 1.0f, zero = 0.0f;
    const float hexScale[3] = {p.sx, p.sy, p.sz};  // the EXE's (1.0, 0.866, 0.2); see the height texture in setHeightField
    const float aux[3] = {0.707f, 0, 0.707f};
    float light[3] = {-0.4f, -0.5f, 0.75f};
    {
        const float n = std::sqrt(light[0] * light[0] + light[1] * light[1] + light[2] * light[2]);
        for (float& x : light) x /= n;
    }
    const float camPos[3] = {inv[12], inv[13], inv[14]};
    const float camZ[3] = {inv[2], inv[6], inv[10]};
    float half[3] = {light[0] + camZ[0], light[1] + camZ[1], light[2] + camZ[2]};
    {
        const float n = std::sqrt(half[0] * half[0] + half[1] * half[1] + half[2] * half[2]);
        if (n > 0)
            for (float& x : half) x /= n;
    }
    // the EXE's page addressing (issue 0191): texel units over 1024, rows bottom-up through the negative y scale
    const float heightTexel[2] = {1.0f / static_cast<float>(bake::kHeightPageSize), -1.0f / static_cast<float>(bake::kHeightPageSize)};
    const float bakeTexel[2] = {1.0f / 512, 1.0f / 512};
    const float mapSize[2] = {static_cast<float>(p.samplesX / 64), static_cast<float>(p.samplesY / 64)};  // plots; unread by pass 1
    const float screen[2] = {static_cast<float>(cam.width), static_cast<float>(cam.height)};
    C5R_TRY(put(*lFrame, "ViewProj", vp, 16, frame));
    float ovl[16];
    overlayProjForCopy(ov, 0, 0.0f, ovl);   // identity without an overlay and the fog off (the picture before 3704)
    C5R_TRY(put(*lFrame, "OverlayProj", ovl, 16, frame));
    // issue 3953: FOWAnim (TerrainFrame 0x090): the DS's cloud uv (world.xy + FOWAnim.zw) * FOWAnim.xy; 0 with the fog
    // off (the picture before)
    if (o.fow.enabled) C5R_TRY(put(*lFrame, "FOWAnim", o.fow.anim, 4, frame));
    C5R_TRY(put(*lFrame, "BicubicBasis", basis, 16, frame));
    C5R_TRY(put(*lFrame, "TessFactor", &tess, 1, frame));
    C5R_TRY(put(*lFrame, "TessFactorOne", &one, 1, frame));
    C5R_TRY(put(*lFrame, "HexScale", hexScale, 3, frame));
    C5R_TRY(put(*lFrame, "AuxDir", aux, 3, frame));
    C5R_TRY(put(*lFrame, "LightDir", light, 3, frame));
    C5R_TRY(put(*lFrame, "CameraPos", camPos, 3, frame));
    C5R_TRY(put(*lFrame, "CameraAxisZ", camZ, 3, frame));
    C5R_TRY(put(*lFrame, "HalfVector", half, 3, frame));
    C5R_TRY(put(*lFrame, "HeightTexelScale", heightTexel, 2, frame));
    C5R_TRY(put(*lFrame, "BakeTexelScale", bakeTexel, 2, frame));
    C5R_TRY(put(*lFrame, "MapSize", mapSize, 2, frame));
    // issue 0190: Arg6 = the shadow strength while the EXE's shadow map is bound as t3, else 0 (no shadow)
    const bool shadowOn = o.shadow.srv != nullptr;
    const float arg6 = shadowOn ? o.shadow.strength : 0.0f;
    C5R_TRY(put(*lFrame, "Arg6", &arg6, 1, frame));
    C5R_TRY(put(*lFrame, "FlagMode2", &zero, 1, frame));
    // FlagB (TR+0x241C != 0): pass 0's spec switch; 1 in the EXE's pass-0 draws of 3601's captures (issue 0189), 0 here
    // without the install's spec map (then a pass-0 tile is its pass-1 picture)
    const float flagB = specOn ? 1.0f : 0.0f;
    C5R_TRY(put(*lFrame, "FlagB", &flagB, 1, frame));
    C5R_TRY(put(*lFrame, "FlagC", &one, 1, frame));      // the PS takes t0 only when FlagC != 0
    C5R_TRY(put(*lFrame, "One", &one, 1, frame));
    C5R_TRY(put(*lFrame, "FlagD", &zero, 1, frame));
    float shm[16];
    shadowMatrixForCopy(o.shadow, 0, 0.0f, shm);   // identity without a shadow (the picture before 0190)
    C5R_TRY(put(*lFrame, "ShadowMatrix0", shm, 16, frame));
    C5R_TRY(put(*lFrame, "ScreenSize", screen, 2, frame));
    C5R_TRY(put(*lFrame, "OverlaySize", &overlaySize, 1, frame));
    // DistanceFog (issue 0188): the EXE's block for this camera; visible only with the install's ramp as t2
    float fogBlock[8];
    distanceFogBlock(o.fog, camPos, fogBlock);
    C5R_TRY(put(*lFog, "FogColor", fogBlock, 4, fog));
    C5R_TRY(put(*lFog, "FogParams", fogBlock + 4, 4, fog));
    C5R_TRY(p.upload(ctx, p.cbFrame, frame.data(), frame.size()));
    C5R_TRY(p.upload(ctx, p.cbFog, fog.data(), fog.size()));

    // pipeline
    D3D11_VIEWPORT vpt = {0, 0, static_cast<float>(cam.width), static_cast<float>(cam.height), 0, 1};
    ctx->RSSetViewports(1, &vpt);
    ctx->RSSetState(o.wireframe ? p.rsWire : p.rs);
    ctx->OMSetRenderTargets(1, &rtv, dsv);
    ctx->OMSetDepthStencilState(screenOverlay ? p.dssOff : p.dss, 0);
    ctx->OMSetBlendState(p.bs, nullptr, 0xFFFFFFFFu);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetIndexBuffer(p.ib[drawLevel], DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_16_CONTROL_POINT_PATCHLIST);
    ctx->VSSetShader(p.vs, nullptr, 0);
    ctx->HSSetShader(p.hs[screenOverlay ? 2 : pass], nullptr, 0);
    ctx->DSSetShader(p.ds, nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ID3D11Buffer* cbs[4] = {p.cbTile, p.cbFrame, p.cbFog, p.cbOwn};
    ctx->VSSetConstantBuffers(0, 3, cbs);
    ctx->HSSetConstantBuffers(0, 3, cbs);
    ctx->DSSetConstantBuffers(0, 3, cbs);
    ctx->PSSetConstantBuffers(0, 4, cbs);
    ctx->VSSetSamplers(0, 1, &p.samp[0]);
    ID3D11SamplerState* ps[10];
    for (auto& s : ps) s = p.samp[0];
    for (const terrainsets::TexSlot& t : p.set->psTextures)
        if (t.slot < 10) ps[t.slot] = p.samp[t.samplerState >= 0 && t.samplerState < 13 ? t.samplerState : 0];
    ctx->PSSetSamplers(0, 10, ps);
    // PS inputs from other systems: constants (overlay transparent, shadow white, FOW mask (1, 1) = visible and no
    // TransFOW LUT, FOW base black, identity LUTs, spec map white); the fog ramp from the install (issue 0188), black
    // (no fog) without one or with the fog off
    ID3D11ShaderResourceView* const overlaySrv = o.overlay.srv ? o.overlay.srv : p.overlay;   // issue 3704
    const bool fogOn = o.fog.enabled && p.fogRamp != nullptr;
    ID3D11ShaderResourceView* const rampSrv = fogOn ? p.fogRamp : p.black;
    // t9: the install's spec map (issue 0189), black without one (FlagB 0 then: not used)
    ID3D11ShaderResourceView* const specSrv = specOn ? p.specMap : p.black;
    // t3 / t7 (issue 0190): the EXE's shadow map and the TerrainShadow LUT while a shadow is bound; white and identity
    // otherwise (Arg6 0 then: the lerp factor is 0). Issue 4305: t7 per tile below (the style's volume); Generic here.
    ID3D11ShaderResourceView* const shadowSrv = shadowOn ? o.shadow.srv : p.white;
    ID3D11ShaderResourceView* const shadowLutSrv = shadowOn && p.shadowLut[0] ? p.shadowLut[0] : p.lut;
    ID3D11ShaderResourceView* srv[11] = {p.white, overlaySrv, rampSrv, shadowSrv, p.white, p.black, p.lut, shadowLutSrv, p.white, specSrv, nullptr};
    // issue 3953: the fog of war: t4 the mask, t5 the cloud base, t6 per tile below (the style's TerrainFOW volume)
    if (o.fow.enabled) {
        srv[4] = o.fow.mask;
        srv[5] = o.fow.cloudBase;
    }

    const ColorSource fallback = o.color == ColorSource::Caches ? o.fallback : o.color;
    const int mode = ownMode(fallback);
    float ownCb[16] = {0};
    rgba(o.constantRgba, ownCb);
    ownCb[8] = p.sx;
    ownCb[9] = p.sy;
    ownCb[10] = p.zmaxAll;
    ownCb[12] = light[0];
    ownCb[13] = light[1];
    ownCb[14] = light[2];
    ownCb[15] = 0.25f;

    FrameStats st;
    const uint64_t mark = ++p.drawMark;
    st.passUsed = forcedPass;
    st.specOn = specOn;
    // issue 0189: the pass the module gives each tile of the EXE's visible list (the tiles the EXE draws, 0191), whether
    // or not the module's own box cull draws it; only when a module tile is an EXE tile (64 samples)
    if (o.lodLevel == kLodExe && p.P == 64 && o.color == ColorSource::Caches) {
        st.listCounted = true;
        uint32_t h = 2166136261u;
        for (uint32_t i : listTiles) {
            if (i >= p.tiles.size()) continue;
            const Tile& t = p.tiles[i];
            const int tp = !t.cache.color ? -1 : forcedPass >= 0 ? forcedPass : (t.cache.second ? 0 : 1);
            if (t.cache.color) ++st.listKind[t.cacheKind < 5 ? t.cacheKind : 0];   // issue 0371
            if (tp == 0) {
                ++st.listPass0;
                for (int b = 0; b < 4; ++b) {
                    h ^= (i >> (8 * b)) & 0xFFu;
                    h *= 16777619u;
                }
            } else if (tp == 1) ++st.listPass1;
            else if (tp < 0) ++st.listNoCache;
        }
        st.listPass0Hash = h;
    }
    int hsPass = screenOverlay ? 2 : pass;   // the hull shader bound
    if (screenOverlay) {
        st.passUsed = 2;
        st.listCounted = false;
    }
    st.tessFactorUsed = tess;
    st.lodLevel = level;
    st.lodMip = lodMip;
    st.lodStep = lodStepV;
    st.lodMin = lf.minLod;
    st.lodValue = lodNext.value;
    st.lodVisible = lf.visible;
    st.lodAccepted = lodAccepted;
    st.overlayBound = o.overlay.srv != nullptr;
    st.fogOn = fogOn;
    st.fowOn = o.fow.enabled;
    st.shadowOn = shadowOn;
    st.shadowLutFromFile = shadowOn && p.shadowLut[0] != nullptr;
    // issue 4314: the copies at the EXE's wrap period (3583 on Tiny), as the EXE's wrap views; samplesX * sx (3584) put
    // the ground across the seam, its overlay and its shadow one world unit east of stock (terrain-surface.md 13.6, 13.8)
    const float wrapW = wrapPeriod(p.samplesX, p.sx);
    const int copies = p.wrapX ? 1 : 0;
    // ticket 0707: the coarse cull's blocks (their z ranges from the tiles' as the tile test forms them, every draw)
    const uint32_t bW = (p.grid.tilesX + kCullBlock - 1) / kCullBlock, bH = (p.grid.tilesY + kCullBlock - 1) / kCullBlock;
    const bool coarse =
        o.coarseCull && p.tiles.size() == static_cast<size_t>(p.grid.tilesX) * p.grid.tilesY && bW > 0 && bH > 0;
    if (coarse) {
        p.blockZ0.assign(static_cast<size_t>(bW) * bH, 0.0f);
        p.blockZ1.assign(static_cast<size_t>(bW) * bH, 0.0f);
        p.blockCulled.assign(static_cast<size_t>(bW) * bH, 0);
        for (uint32_t by = 0; by < bH; ++by)
            for (uint32_t bx = 0; bx < bW; ++bx) {
                float z0 = 0, z1 = 0;
                bool first = true;
                for (uint32_t ty = by * kCullBlock; ty < std::min(p.grid.tilesY, (by + 1) * kCullBlock); ++ty)
                    for (uint32_t tx = bx * kCullBlock; tx < std::min(p.grid.tilesX, (bx + 1) * kCullBlock); ++tx) {
                        const Tile& t = p.tiles[static_cast<size_t>(ty) * p.grid.tilesX + tx];
                        const float a = t.zmin - 1.0f, b = t.zmax + 1.0f;
                        z0 = first ? a : std::min(z0, a);
                        z1 = first ? b : std::max(z1, b);
                        first = false;
                    }
                p.blockZ0[static_cast<size_t>(by) * bW + bx] = z0;
                p.blockZ1[static_cast<size_t>(by) * bW + bx] = z1;
            }
    }
    // ticket 0707: TerrainTile's members found once (put's per-tile by-name walk); each status returned at put's place
    const MemberAt mTw = findMember(*lTile, "TileWorld", 16, tile.size());
    const MemberAt mOff = findMember(*lTile, "TileOffset", 4, tile.size());
    const MemberAt mInv = findMember(*lTile, "InvGridSize", 2, tile.size());
    const MemberAt mLs = findMember(*lTile, "LodStep", 2, tile.size());
    const MemberAt mMip = findMember(*lTile, "LodMip", 1, tile.size());
    // ticket 0707: the tile's pixel shader bound only when it differs from the one the loop bound last (the first tile
    // always binds); nothing else in the loop binds a pixel shader, so every draw sees the shader it saw before
    ID3D11PixelShader* psBound = nullptr;
    bool psKnown = false;
    auto setPs = [&](ID3D11PixelShader* s) {
        if (psKnown && s == psBound) return;
        ctx->PSSetShader(s, nullptr, 0);
        psBound = s;
        psKnown = true;
    };
    for (int k = -copies; k <= copies; ++k) {
        const float shiftX = static_cast<float>(k) * wrapW;
        if ((ov.srv || shadowOn) && copies) {
            // issue 3704: each wrap copy samples the overlay with the matrix of the EXE view that shows it (issue 3953:
            // and the FOW mask, which shares the overlay's window); issue 0190: the shadow map the same way
            overlayProjForCopy(ov, k, wrapW, ovl);
            C5R_TRY(put(*lFrame, "OverlayProj", ovl, 16, frame));
            shadowMatrixForCopy(o.shadow, k, wrapW, shm);
            C5R_TRY(put(*lFrame, "ShadowMatrix0", shm, 16, frame));
            C5R_TRY(p.upload(ctx, p.cbFrame, frame.data(), frame.size()));
        }
        if (coarse) {
            // the block's box: the first tile's low corner to the last tile's high corner, by the tile test's formulas
            for (uint32_t by = 0; by < bH; ++by)
                for (uint32_t bx = 0; bx < bW; ++bx) {
                    const uint32_t txA = bx * kCullBlock, txB = std::min(p.grid.tilesX, (bx + 1) * kCullBlock);
                    const uint32_t tyA = by * kCullBlock, tyB = std::min(p.grid.tilesY, (by + 1) * kCullBlock);
                    const float x0 = (static_cast<float>(txA * p.P) - 1) * p.sx - p.ox + shiftX;
                    const float x1 = (static_cast<float>(txB * p.P) + 1) * p.sx - p.ox + shiftX;
                    const float y0 = (static_cast<float>(tyA * p.P) - 1) * p.sy - p.oy;
                    const float y1 = (static_cast<float>(tyB * p.P) + 1) * p.sy - p.oy;
                    const size_t bi = static_cast<size_t>(by) * bW + bx;
                    p.blockCulled[bi] = boxOutsideByMargin(vp, x0, x1, y0, y1, p.blockZ0[bi], p.blockZ1[bi]) ? 1 : 0;
                }
        }
        for (uint32_t ty = 0; ty < p.grid.tilesY; ++ty)
            for (uint32_t tx = 0; tx < p.grid.tilesX; ++tx) {
                if (coarse && p.blockCulled[static_cast<size_t>(ty / kCullBlock) * bW + tx / kCullBlock]) {
                    // as the tile test: every tile of the block's row run is outside one plane; on to the next block
                    const uint32_t end = std::min(p.grid.tilesX, (tx / kCullBlock + 1) * kCullBlock);
                    if (k == 0) st.tilesCulled += end - tx;
                    st.coarseSkipped += end - tx;
                    tx = end - 1;
                    continue;
                }
                Tile& t = p.tiles[static_cast<size_t>(ty) * p.grid.tilesX + tx];
                // cull: the tile's box (one sample of margin for the bicubic surface) against the clip volume
                const float x0 = (static_cast<float>(tx * p.P) - 1) * p.sx - p.ox + shiftX;
                const float x1 = (static_cast<float>((tx + 1) * p.P) + 1) * p.sx - p.ox + shiftX;
                const float y0 = (static_cast<float>(ty * p.P) - 1) * p.sy - p.oy;
                const float y1 = (static_cast<float>((ty + 1) * p.P) + 1) * p.sy - p.oy;
                const float z0 = t.zmin - 1.0f, z1 = t.zmax + 1.0f;
                int out[6] = {0};
                for (int c = 0; c < 8; ++c) {
                    const float w[4] = {(c & 1) ? x1 : x0, (c & 2) ? y1 : y0, (c & 4) ? z1 : z0, 1};
                    float cl[4];
                    for (int j = 0; j < 4; ++j) cl[j] = w[0] * vp[j] + w[1] * vp[4 + j] + w[2] * vp[8 + j] + w[3] * vp[12 + j];
                    out[0] += cl[0] < -cl[3];
                    out[1] += cl[0] > cl[3];
                    out[2] += cl[1] < -cl[3];
                    out[3] += cl[1] > cl[3];
                    out[4] += cl[2] < 0;
                    out[5] += cl[2] > cl[3];
                }
                bool culled = false;
                for (int j = 0; j < 6; ++j) culled |= out[j] == 8;
                if (culled) {
                    if (k == 0) ++st.tilesCulled;
                    continue;
                }
                // ticket 0665: an open-sea tile (height class 1, not land) is never baked and the EXE never draws it (its
                // in-play list holds class 3 only); with the caches as colour source and the material fallback (the
                // product's default) it is not drawn either, so what lies under the ocean is the scene clear, as in stock.
                // Its material colour (byte 37 = 150, 4, 222) showed through in the frame after a far camera jump
                // (research/05-graphics-composition/fog-after-camera-jump.md). The diagnostic fallbacks keep drawing it.
                if (o.color == ColorSource::Caches && o.fallback == ColorSource::Material && !t.cache.color && !t.land &&
                    !screenOverlay) {
                    if (k == 0) {
                        ++st.tilesWithoutCache;
                        ++st.seaTilesHidden;
                    }
                    continue;
                }
                // TerrainTile (asset-pipeline.md 3.3): TileWorld translation, TileOffset, InvGridSize, LodStep, LodMip
                const float tw[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -p.ox + shiftX, -p.oy, 0, 1};
                // TileOffset.zw = minus the page window's first sample (tile+0xE4 / +0xE8: 64 - px * 896), issue 0191
                const Page& pg = p.pages[t.page];
                const float off[4] = {static_cast<float>(tx * p.P), static_cast<float>(ty * p.P), static_cast<float>(-pg.wx),
                                      static_cast<float>(-pg.wy)};
                // tile+0xd4/+0xd8 are taken as the tile's vertex count P + 1 [I, P16]: InvGridSize = 1/(P + 2)
                const float d4 = static_cast<float>(o.tileD4 ? o.tileD4 : p.P + 1);
                // ticket 4551: 0 in the Screen Overlay pass (stock's pass-2 draws, trees.md 11.1)
                const float invGrid[2] = {screenOverlay ? 0.0f : 1.0f / (d4 + 1.0f), screenOverlay ? 0.0f : 1.0f / (d4 + 1.0f)};
                const float lodStep[2] = {lodStepV, lodStepV};
                C5R_TRY(mTw.st);   // ticket 0707: put's writes at the members found once
                std::memcpy(tile.data() + mTw.offset, tw, 16 * 4);
                C5R_TRY(mOff.st);
                std::memcpy(tile.data() + mOff.offset, off, 4 * 4);
                C5R_TRY(mInv.st);
                std::memcpy(tile.data() + mInv.offset, invGrid, 2 * 4);
                C5R_TRY(mLs.st);
                std::memcpy(tile.data() + mLs.offset, lodStep, 2 * 4);
                C5R_TRY(mMip.st);
                std::memcpy(tile.data() + mMip.offset, &lodMip, 1 * 4);
                C5R_TRY(p.upload(ctx, p.cbTile, tile.data(), tile.size()));
                const bool useCache = o.color == ColorSource::Caches && t.cache.color;
                if (o.color == ColorSource::Caches && !t.cache.color && k == 0 && !screenOverlay) ++st.tilesWithoutCache;   // 0665: hidden sea tiles counted above
                if (o.color == ColorSource::Caches && !t.cache.color && t.land && t.grayMark != mark && !screenOverlay) {
                    t.grayMark = mark;
                    ++st.landTilesWithoutCache;
                }
                ctx->VSSetShaderResources(0, 1, &pg.srv);
                if (screenOverlay) {
                    // ticket 4551: every tile with the pass-2 PS 0x067ea680 (reads t1 only), cached or not
                    srv[0] = p.white;
                    srv[8] = p.white;
                    srv[10] = nullptr;
                    setPs(p.ps[2]);
                } else if (useCache) {
                    if (k == 0) ++st.drawnKind[t.cacheKind < 5 ? t.cacheKind : 0];   // issue 0371
                    srv[0] = t.cache.color;
                    srv[8] = t.cache.second ? t.cache.second : p.white;
                    srv[10] = nullptr;
                    if (o.fow.enabled) {   // issue 3953: the tile style's TerrainFOW volume (0xD90E60 + 4 * style)
                        srv[6] = p.fowLut[t.style] ? p.fowLut[t.style] : p.lut;
                        ++st.fowStyleTiles[t.style];
                    }
                    if (shadowOn) {   // issue 4305: the tile style's TerrainShadow volume (0xD90E74 + 4 * style)
                        srv[7] = p.shadowLut[t.style] ? p.shadowLut[t.style] : p.lut;
                        ++st.shadowStyleTiles[t.style];
                    }
                    const int tp = forcedPass >= 0 ? forcedPass : (t.cache.second ? 0 : 1);
                    if (tp != hsPass) {
                        ctx->HSSetShader(p.hs[tp], nullptr, 0);
                        hsPass = tp;
                    }
                    setPs(p.ps[tp]);
                    if (tp == 0) ++st.tilesPass0;
                    else if (tp == 1) ++st.tilesPass1;
                } else {
                    ownCb[4] = static_cast<float>(tx * p.P) * p.sx - p.ox + shiftX;
                    ownCb[5] = static_cast<float>(ty * p.P) * p.sy - p.oy;
                    ownCb[6] = t.mat ? 1.0f : 0.0f;
                    ownCb[7] = static_cast<float>(mode);
                    C5R_TRY(p.upload(ctx, p.cbOwn, ownCb, sizeof ownCb));
                    srv[0] = p.white;
                    srv[8] = p.white;
                    srv[10] = t.mat;
                    setPs(p.ownPs);
                }
                ctx->PSSetShaderResources(0, 11, srv);
                ctx->DrawIndexed(p.ibPatches[drawLevel] * 16, 0, 0);
                ++st.tilesDrawn;
                {   // ticket 0707: the draw order's hash
                    const uint32_t id[2] = {static_cast<uint32_t>(k + 1), ty * p.grid.tilesX + tx};
                    const uint8_t* b = reinterpret_cast<const uint8_t*>(id);
                    for (size_t i = 0; i < sizeof id; ++i) {
                        st.drawnOrderHash ^= b[i];
                        st.drawnOrderHash *= 16777619u;
                    }
                }
            }
    }
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ID3D11ShaderResourceView* none[11] = {};
    ctx->VSSetShaderResources(0, 1, none);
    ctx->PSSetShaderResources(0, 11, none);

    // water placeholder: one flat quad over the whole grid (and its wrap copies) at waterZ (D58 point 7); not in the
    // Screen Overlay pass (ticket 4551)
    if (o.water && !screenOverlay) {
        float wcb[28] = {0};
        std::memcpy(wcb, vp, sizeof vp);
        const float extra = p.wrapX ? static_cast<float>(p.samplesX) * p.sx : 0.0f;   // covers both copies, as before 4314
        wcb[16] = -p.ox - extra;
        wcb[17] = -p.oy;
        wcb[18] = static_cast<float>(p.samplesX) * p.sx - p.ox + extra;
        wcb[19] = static_cast<float>(p.samplesY) * p.sy - p.oy;
        rgba(o.waterRgba, wcb + 20);
        wcb[24] = o.waterZ >= 0 ? o.waterZ : p.waterZ;
        wcb[25] = (o.color == ColorSource::WorldPos) ? 1.0f : 0.0f;
        C5R_TRY(p.upload(ctx, p.cbWater, wcb, sizeof wcb));
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ctx->VSSetShader(p.waterVs, nullptr, 0);
        ctx->PSSetShader(p.waterPs, nullptr, 0);
        ctx->VSSetConstantBuffers(0, 1, &p.cbWater);
        ctx->PSSetConstantBuffers(0, 1, &p.cbWater);
        ctx->Draw(4, 0);
    }
    if (gq) {
        ctx->End(gq->t1);
        ctx->End(gq->disjoint);
        gq->issued = true;
        gq->serial = ++p.serial;
    }
    st.gpuMs = p.lastGpuMs;
    st.cpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tStart).count();
    if (o.lodLevel == kLodExe && !screenOverlay) p.lod = lodNext;   // issue 0191: the frame's LOD state, once the draw went through
    if (stats) *stats = st;
    return Status::Ok();
}

}} // namespace c5r::terrain
