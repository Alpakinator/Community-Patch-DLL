// cloud.cpp - c5r::fow::CloudRenderer: FOW CULL and the three cloud planes of the fog of war, drawn with the EXE's own
// FOWDrawMask pass 3 and FOWBaseLayer programs (task 3955, epic 0168). Own code, written from the behaviour described in
// research/05-graphics-composition/fog-of-war.md sections 2.3, 3.2, 3.5 and 7.2 (D83: no decompiler output here). The
// programs are read from the player's EXE and the cloud textures from the player's install at run time; nothing of
// either is in this file.
#include <c5r/fow/fow.h>

#include <windows.h>
#include <d3d11.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>

#include "c5r/gamefs/dds.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/shaders/shadersection.h"
#include "c5r/terrainsets/terrainsets.h"
#include "c5r/water/water.h"

namespace c5r {
namespace fow {

namespace {

template <class T> void rel(T*& p)
{
    if (p) p->Release();
    p = nullptr;
}

Status hrOk(HRESULT h, const char* what)
{
    if (SUCCEEDED(h)) return Status::Ok();
    char b[32];
    std::snprintf(b, sizeof b, " (hr 0x%08lx)", static_cast<unsigned long>(h));
    return Status::fail(h == E_OUTOFMEMORY ? Err::OutOfMemory : Err::Device, std::string(what) + b);
}

bool finiteN(const float* m, int n)
{
    for (int i = 0; i < n; ++i)
        if (!std::isfinite(m[i])) return false;
    return true;
}

// out = a * b (row-major, row vectors), each element summed left to right in float (D3DXMatrixMultiply's scalar form).
void mul44(const float* a, const float* b, float* out)
{
    float r[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = a[i * 4 + 0] * b[0 * 4 + j];
            s += a[i * 4 + 1] * b[1 * 4 + j];
            s += a[i * 4 + 2] * b[2 * 4 + j];
            s += a[i * 4 + 3] * b[3 * 4 + j];
            r[i * 4 + j] = s;
        }
    std::memcpy(out, r, sizeof r);
}

// Scale(sx, sy, 1) * Translate(cx, cy, cz), row-major row vectors.
void quadWorld(float sx, float sy, float cx, float cy, float cz, float* out)
{
    const float m[16] = {sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, 1, 0, cx, cy, cz, 1};
    std::memcpy(out, m, sizeof m);
}

bool worldOk(const CloudWorld& w)
{
    return finiteN(w.hexToWorld, 16) && std::isfinite(w.sx) && std::isfinite(w.sy) && std::isfinite(w.ox) &&
           std::isfinite(w.oy) && std::isfinite(w.minX) && std::isfinite(w.minY) && std::isfinite(w.maxX) &&
           std::isfinite(w.maxY) && w.sx != 0.0f && w.sy != 0.0f && w.maxX > w.minX && w.maxY > w.minY &&
           w.samplesX != 0 && w.samplesY != 0;
}

water::World toWaterWorld(const CloudWorld& c)
{
    water::World w;
    w.minX = c.minX;
    w.minY = c.minY;
    w.maxX = c.maxX;
    w.maxY = c.maxY;
    w.width = c.maxX - c.minX;
    w.height = c.maxY - c.minY;
    w.sx = c.sx;
    w.sy = c.sy;
    w.samplesX = c.samplesX;
    w.samplesY = c.samplesY;
    w.valid = true;
    return w;
}

// The water writer's rounding of a sample coordinate: half away from zero, in double, then truncated through a float
// (water-system.md 10.2 step 2; the same as water.cpp's roundSample).
int roundSample(float v)
{
    const double d = v >= 0.0f ? std::floor(static_cast<double>(v) + 0.5) : std::ceil(static_cast<double>(v) - 0.5);
    return static_cast<int>(static_cast<float>(d));
}

// Stock's constants (fog-of-war.md 7.2) [L: 3951's captures, read by 3955].
const float kPlaneHeight[3] = {11.2f, 38.25f, 51.0f};   // 0.2 x 56, 0.2 x 191.25, 0.2 x 255
const float kPlaneAnimScale[3] = {1.0f, 1.5f, 2.0f};    // FOWAnim.zw x s
const char* const kPlaneFile[3] = {"fogcloudtile_dif_02.dds", "fogcloudtile_dif_03.dds", "fogcloudtile_dif_01.dds"};
const float kCullHeight = 102.0f;                       // the FOW CULL quads' z (depth is forced to 0 by the VS)
const float kCullShiftY = 128.0f;                       // the quads' centre: HexToWorld + (32 sx, 32 sy - 128)
const float kHalf = 32.0f;                              // half a plot in samples
const uint32_t kSlots = 24;                             // quads per draw (the programs' constant-buffer arrays)
const uint32_t kCullCap = 2048;                         // FOW CULL quads per view; more are dropped
const uint32_t kPlaneCap = 2048;                        // plane quads per view and plane, bx outer; more are dropped [C]
const uint32_t kBaseCbBytes = 3328;                     // FOWBaseLayer's own buffer as the EXE creates it [L]
const uint32_t kCullCbBytes = 1536;                     // FOWDrawMask's own buffer [L]
// FOWBaseLayer's own buffer (fog-of-war.md 3.5) [C, L]: WVP[24] at 0x000, World[24] at 0x600, OverlayProj at 0xC00, the
// plane's FOWAnim at 0xC40. FOWDrawMask pass 3 reads WVP[24] at 0x000.
const uint32_t kOffWorld = 0x600, kOffOverlay = 0xC00, kOffAnim = 0xC40;

} // namespace

// ---- free functions ----

Status cullFlags(const MapInput& map, std::vector<uint8_t>& out)
{
    out.clear();
    if (!map.states || map.width == 0 || map.height == 0)
        return Status::fail(Err::BadLayout, "cullFlags: no plot states or an empty map");
    const int W = static_cast<int>(map.width), H = static_cast<int>(map.height);
    out.assign(static_cast<size_t>(W) * H, 0);
    for (int y = 0; y < H; ++y) {
        if (y < 2 || y > H - 2) continue;   // rows 0, 1 and H - 1 stay 0 [C]
        for (int x = 0; x < W; ++x) {
            bool clear = true;
            for (int dy = -2; dy <= 2 && clear; ++dy)
                for (int dx = -2; dx <= 2; ++dx) {
                    if ((dx == 2 || dx == -2) && (dy == 2 || dy == -2)) continue;
                    const int xx = ((x + dx) % W + W) % W, yy = ((y + dy) % H + H) % H;
                    if (map.states[static_cast<size_t>(yy) * W + xx] == PlotState::Unexplored) {
                        clear = false;
                        break;
                    }
                }
            out[static_cast<size_t>(y) * W + x] = clear ? 1 : 0;
        }
    }
    return Status::Ok();
}

void hexToWorldXY(const CloudWorld& w, int32_t gx, int32_t gy, float out[2])
{
    const float hx = static_cast<float>(gx - (gy >> 1)), hy = static_cast<float>(gy);
    const float* m = w.hexToWorld;
    const float x = hx * m[0] + hy * m[4] + m[12];
    const float y = hx * m[1] + hy * m[5] + m[13];
    out[0] = x * w.sx - w.ox;
    out[1] = y * w.sy - w.oy;
}

Status planeBlocks(const float viewProj[16], const CloudWorld& w, int32_t out[4])
{
    out[0] = out[1] = out[2] = out[3] = 0;
    if (!viewProj || !finiteN(viewProj, 16)) return Status::fail(Err::BadLayout, "planeBlocks: matrix not finite");
    if (!worldOk(w)) return Status::fail(Err::BadLayout, "planeBlocks: world not valid");
    // Stock's plane draw (fog-of-war.md 7.2, "the plane rectangle", ticket 0572 [C]) takes x from the view's footprint
    // CLAMPED to the world box (0xDA1B00 / 0xDA1B30) and y from the UNCLAMPED footprint (0xDA1B60 / 0xDA1B90), and clamps
    // no block index: at the south and north map edges the planes go on past the world box (a clamp there cut them at
    // the box, the three planes' ends showing as two seams and a cloud-to-black line, 0572).
    water::Footprint fp;
    C5R_TRY(water::viewFootprint(viewProj, toWaterWorld(w), fp));
    water::World open = toWaterWorld(w);   // the same footprint without the clamp in y
    open.minY = -std::numeric_limits<float>::max();
    open.maxY = std::numeric_limits<float>::max();
    water::Footprint fpY;
    C5R_TRY(water::viewFootprint(viewProj, open, fpY));
    if (!(std::isfinite(fp.minX) && std::isfinite(fp.maxX) && std::isfinite(fpY.minY) && std::isfinite(fpY.maxY)))
        return Status::fail(Err::BadLayout, "planeBlocks: footprint not finite");
    // A near-horizontal view puts the unclamped y far away; keep the sample coordinate inside int (stock's x87 store
    // would give 0x80000000 there; such a view draws its first 2,048 quads either way).
    const auto sample = [](float v) { return roundSample(std::max(-1.0e9f, std::min(1.0e9f, v))); };
    const int rx0 = sample((fp.minX - w.minX) / w.sx);
    const int ry0 = sample((fpY.minY - w.minY) / w.sy);
    const int rx1 = sample((fp.maxX - w.minX) / w.sx);
    const int ry1 = sample((fpY.maxY - w.minY) / w.sy);
    out[0] = rx0 / 64;   // C division: toward zero, as the EXE's
    out[1] = rx1 / 64 + 1;
    out[2] = ry0 / 64;
    // One block row more at the far (high y) end than the ocean's [L: 3951's t21 cameras; C: 0572's read].
    out[3] = ry1 / 64 + 2;
    return Status::Ok();
}

// ---- CloudRenderer ----

struct CloudRenderer::Impl {
    ID3D11Device* dev = nullptr;
    ID3D11VertexShader* vsBase = nullptr;
    ID3D11PixelShader* psBase = nullptr;
    ID3D11VertexShader* vsCull = nullptr;
    ID3D11PixelShader* psCull = nullptr;
    ID3D11InputLayout* layoutBase = nullptr;
    ID3D11InputLayout* layoutCull = nullptr;
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    ID3D11Buffer* cbBase = nullptr;
    ID3D11Buffer* cbCull = nullptr;
    ID3D11BlendState* blendCull = nullptr;
    ID3D11BlendState* blendPlane = nullptr;
    ID3D11DepthStencilState* depthCull = nullptr;
    ID3D11DepthStencilState* depthPlane = nullptr;
    ID3D11RasterizerState* raster = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11ShaderResourceView* cloud[3] = {};   // plane order: _02, _03, _01
    std::vector<uint8_t> flags;                // cull flags of the last draw
    std::vector<float> cullWorld;              // per view: the culled quads' world matrices (16 floats each)

    ~Impl()
    {
        rel(vsBase); rel(psBase); rel(vsCull); rel(psCull); rel(layoutBase); rel(layoutCull);
        rel(vb); rel(ib); rel(cbBase); rel(cbCull); rel(blendCull); rel(blendPlane); rel(depthCull); rel(depthPlane);
        rel(raster); rel(sampler);
        for (auto*& c : cloud) rel(c);
        rel(dev);
    }

    Status makeDynamicCb(uint32_t bytes, ID3D11Buffer** out)
    {
        D3D11_BUFFER_DESC d = {};
        d.ByteWidth = bytes;
        d.Usage = D3D11_USAGE_DYNAMIC;
        d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        return hrOk(dev->CreateBuffer(&d, nullptr, out), "CreateBuffer (constant)");
    }

    Status upload(ID3D11DeviceContext* ctx, ID3D11Buffer* b, const uint8_t* bytes, uint32_t n)
    {
        D3D11_MAPPED_SUBRESOURCE m = {};
        C5R_TRY(hrOk(ctx->Map(b, 0, D3D11_MAP_WRITE_DISCARD, 0, &m), "Map (constant buffer)"));
        std::memcpy(m.pData, bytes, n);
        ctx->Unmap(b, 0);
        return Status::Ok();
    }
};

CloudRenderer::~CloudRenderer() { delete m_impl; }

Status CloudRenderer::init(ID3D11Device* dev, const std::wstring& exePath, const std::wstring& gameDir)
{
    delete m_impl;
    m_impl = nullptr;
    if (!dev) return Status::fail(Err::BadLayout, "CloudRenderer::init: no device");
    const terrainsets::ShaderSetDesc* base = terrainsets::findSet("FOWBaseLayer");
    const terrainsets::ShaderSetDesc* maskSet = terrainsets::findSet("FOWDrawMask");
    if (!base || !maskSet) return Status::fail(Err::NotFound, "terrainsets has no FOWBaseLayer or FOWDrawMask set");
    const terrainsets::PassDesc* pBase = nullptr;
    const terrainsets::PassDesc* pCull = nullptr;
    for (const auto& pd : base->passes)
        if (pd.index == 0) pBase = &pd;
    for (const auto& pd : maskSet->passes)
        if (pd.index == 3) pCull = &pd;
    if (!pBase || !pCull) return Status::fail(Err::NotFound, "FOWBaseLayer pass 0 or FOWDrawMask pass 3 not described");

    shaders::ShaderSection sec;
    C5R_TRY(sec.open(exePath));
    std::unique_ptr<Impl> p(new Impl());
    p->dev = dev;
    dev->AddRef();
    auto blob = [&](uint32_t va, shaders::Stage st, const char* what, const shaders::ShaderBlob** out) -> Status {
        char v[16];
        std::snprintf(v, sizeof v, "0x%08x", va);
        const shaders::ShaderBlob* b = va ? sec.findByVa(va) : nullptr;
        if (!b) return Status::fail(Err::NotFound, std::string(what) + " blob " + v + " not in the .shader section");
        if (b->stage != st) return Status::fail(Err::BadLayout, std::string(what) + " blob " + v + " has another stage");
        *out = b;
        return Status::Ok();
    };
    const shaders::ShaderBlob *bvB = nullptr, *bpB = nullptr, *bvC = nullptr, *bpC = nullptr;
    C5R_TRY(blob(pBase->sm50[0], shaders::Stage::Vertex, "FOWBaseLayer VS", &bvB));
    C5R_TRY(blob(pBase->sm50[1], shaders::Stage::Pixel, "FOWBaseLayer PS", &bpB));
    C5R_TRY(blob(pCull->sm50[0], shaders::Stage::Vertex, "FOWDrawMask pass 3 VS", &bvC));
    C5R_TRY(blob(pCull->sm50[1], shaders::Stage::Pixel, "FOWDrawMask pass 3 PS", &bpC));
    C5R_TRY(hrOk(dev->CreateVertexShader(bvB->bytes, bvB->size, nullptr, &p->vsBase), "CreateVertexShader (FOWBaseLayer)"));
    C5R_TRY(hrOk(dev->CreatePixelShader(bpB->bytes, bpB->size, nullptr, &p->psBase), "CreatePixelShader (FOWBaseLayer)"));
    C5R_TRY(hrOk(dev->CreateVertexShader(bvC->bytes, bvC->size, nullptr, &p->vsCull), "CreateVertexShader (FOW CULL)"));
    C5R_TRY(hrOk(dev->CreatePixelShader(bpC->bytes, bpC->size, nullptr, &p->psCull), "CreatePixelShader (FOW CULL)"));

    // The quad vertex of both programs [L, the EXE's buffer]: POSITION float3 (corner x, y in {-1, 1}, 0), NORMAL float3
    // (x, y, slot 0..23), TEXCOORD float2; stride 32.
    const D3D11_INPUT_ELEMENT_DESC el[3] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    C5R_TRY(hrOk(dev->CreateInputLayout(el, 3, bvB->bytes, bvB->size, &p->layoutBase), "CreateInputLayout (FOWBaseLayer)"));
    C5R_TRY(hrOk(dev->CreateInputLayout(el, 3, bvC->bytes, bvC->size, &p->layoutCull), "CreateInputLayout (FOW CULL)"));
    {
        // 24 quads; corners in the order (1, 1), (-1, 1), (1, -1), (-1, -1); triangles (2, 1, 0), (3, 1, 2) [L]
        std::vector<float> v;
        std::vector<uint32_t> ix;
        const float cx[4] = {1, -1, 1, -1}, cy[4] = {1, 1, -1, -1};
        for (uint32_t q = 0; q < kSlots; ++q) {
            for (int k = 0; k < 4; ++k) {
                const float c[8] = {cx[k], cy[k], 0.0f, cx[k], cy[k], static_cast<float>(q), (cx[k] + 1.0f) * 0.5f,
                                    (1.0f - cy[k]) * 0.5f};
                v.insert(v.end(), c, c + 8);
            }
            const uint32_t t[6] = {2, 1, 0, 3, 1, 2};
            for (uint32_t k : t) ix.push_back(4 * q + k);
        }
        D3D11_BUFFER_DESC d = {};
        d.ByteWidth = static_cast<UINT>(v.size() * sizeof(float));
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA s = {v.data(), 0, 0};
        C5R_TRY(hrOk(dev->CreateBuffer(&d, &s, &p->vb), "CreateBuffer (quad vertices)"));
        d.ByteWidth = static_cast<UINT>(ix.size() * sizeof(uint32_t));
        d.BindFlags = D3D11_BIND_INDEX_BUFFER;
        s.pSysMem = ix.data();
        C5R_TRY(hrOk(dev->CreateBuffer(&d, &s, &p->ib), "CreateBuffer (quad indices)"));
    }
    C5R_TRY(p->makeDynamicCb(kBaseCbBytes, &p->cbBase));
    C5R_TRY(p->makeDynamicCb(kCullCbBytes, &p->cbCull));
    {
        D3D11_BLEND_DESC b = {};
        b.RenderTarget[0].BlendEnable = FALSE;
        b.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
        b.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
        b.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        b.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
        b.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        b.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        b.RenderTarget[0].RenderTargetWriteMask = 0;
        C5R_TRY(hrOk(dev->CreateBlendState(&b, &p->blendCull), "CreateBlendState (FOW CULL)"));
        b.RenderTarget[0].BlendEnable = TRUE;
        b.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        b.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        b.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        b.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        b.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        C5R_TRY(hrOk(dev->CreateBlendState(&b, &p->blendPlane), "CreateBlendState (planes)"));
    }
    {
        D3D11_DEPTH_STENCIL_DESC d = {};
        d.DepthEnable = TRUE;
        d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        d.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        d.StencilEnable = FALSE;
        d.FrontFace.StencilFailOp = d.FrontFace.StencilDepthFailOp = d.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
        d.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
        d.BackFace = d.FrontFace;
        C5R_TRY(hrOk(dev->CreateDepthStencilState(&d, &p->depthCull), "CreateDepthStencilState (FOW CULL)"));
        d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        C5R_TRY(hrOk(dev->CreateDepthStencilState(&d, &p->depthPlane), "CreateDepthStencilState (planes)"));
    }
    {
        D3D11_RASTERIZER_DESC r = {};
        r.FillMode = D3D11_FILL_SOLID;
        r.CullMode = D3D11_CULL_FRONT;
        r.FrontCounterClockwise = TRUE;
        r.DepthClipEnable = TRUE;
        C5R_TRY(hrOk(dev->CreateRasterizerState(&r, &p->raster), "CreateRasterizerState"));
    }
    {
        D3D11_SAMPLER_DESC s = {};
        s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        s.MaxAnisotropy = 1;   // the EXE's 0 (unused by a linear filter; D3D11 refuses 0)
        s.ComparisonFunc = D3D11_COMPARISON_NEVER;
        s.MinLOD = 0.0f;
        s.MaxLOD = D3D11_FLOAT32_MAX;
        C5R_TRY(hrOk(dev->CreateSamplerState(&s, &p->sampler), "CreateSamplerState"));
    }
    {
        gamefs::GameFiles fs;
        C5R_TRY(fs.open(gameDir));
        for (int i = 0; i < 3; ++i) {
            std::vector<uint8_t> bytes;
            Status r = fs.read(kPlaneFile[i], bytes);
            if (!r.ok()) return Status::fail(Err::NotFound, std::string(kPlaneFile[i]) + " not readable from the install: " + r.str());
            gamefs::DdsImage img;
            Status pr = gamefs::parseDds(bytes.data(), bytes.size(), img);
            if (!pr.ok()) return Status::fail(pr.code, std::string(kPlaneFile[i]) + ": " + pr.msg);
            ID3D11Resource* tex = nullptr;
            Status cr = gamefs::createTexture(dev, img, &tex, &p->cloud[i]);
            rel(tex);
            if (!cr.ok()) return Status::fail(cr.code, std::string(kPlaneFile[i]) + ": " + cr.msg);
        }
    }
    m_impl = p.release();
    return Status::Ok();
}

Status CloudRenderer::draw(ID3D11DeviceContext* ctx, const CloudInput& in, CloudStats* stats)
{
    if (stats) *stats = CloudStats();
    Impl* p = m_impl;
    if (!p) return Status::fail(Err::NotFound, "CloudRenderer::draw: not initialised");
    if (!ctx || !in.colour || !in.depth || !in.mask)
        return Status::fail(Err::BadLayout, "CloudRenderer::draw: no context, colour target, depth or mask");
    if (in.width == 0 || in.height == 0) return Status::fail(Err::BadLayout, "CloudRenderer::draw: viewport 0");
    const MaskInput& g = in.geometry;
    if (!g.views || g.numViews == 0 || g.numViews > kMaxViews)
        return Status::fail(Err::BadLayout, "CloudRenderer::draw: views 0, above " + std::to_string(kMaxViews) + " or null");
    if (!g.map.states || g.map.width == 0 || g.map.height == 0)
        return Status::fail(Err::BadLayout, "CloudRenderer::draw: no plot states or an empty map");
    if (!finiteN(in.fowAnim, 4)) return Status::fail(Err::BadLayout, "CloudRenderer::draw: FOWAnim not finite");
    if (!worldOk(in.world)) return Status::fail(Err::BadLayout, "CloudRenderer::draw: world not valid");
    for (uint32_t v = 0; v < g.numViews; ++v)
        if (!finiteN(g.views[v].viewProj, 16) || !finiteN(g.views[v].overlayProj, 16))
            return Status::fail(Err::BadLayout, "CloudRenderer::draw: a matrix of view " + std::to_string(v) + " not finite");

    C5R_TRY(cullFlags(g.map, p->flags));
    const CloudWorld& w = in.world;
    const int W = static_cast<int>(g.map.width), H = static_cast<int>(g.map.height);

    // per view: the planes' blocks and the culled quads
    struct ViewWork { int32_t blocks[4]; uint32_t cullFirst, cullCount; };
    std::vector<ViewWork> work(g.numViews);
    p->cullWorld.clear();
    uint32_t cullDropped = 0;
    for (uint32_t v = 0; v < g.numViews; ++v) {
        const View& vw = g.views[v];
        C5R_TRY(planeBlocks(vw.viewProj, w, work[v].blocks));
        work[v].cullFirst = static_cast<uint32_t>(p->cullWorld.size() / 16);
        int x0, x1, y0, y1;   // inclusive-exclusive loop bounds
        bool fromFootprint = vw.x1 <= vw.x0;
        water::Footprint fp;
        if (fromFootprint) {
            C5R_TRY(water::viewFootprint(vw.viewProj, toWaterWorld(w), fp));
            x0 = 0; x1 = W; y0 = 0; y1 = H;
        } else {
            x0 = vw.x0; x1 = vw.x1 + 1; y0 = vw.y0; y1 = vw.y1 + 2;   // stock's loop over the same globals
        }
        uint32_t n = 0;
        for (int gx = x0; gx < x1; ++gx)
            for (int gy = y0; gy < y1; ++gy) {
                if (gx < 0 || gx >= W || gy < 0 || gy >= H) continue;
                if (!p->flags[static_cast<size_t>(gy) * W + gx]) continue;
                float hw[2];
                hexToWorldXY(w, gx, gy, hw);
                const float cx = w.sx * kHalf + hw[0];
                const float cy = (w.sy * kHalf + hw[1]) - kCullShiftY;
                if (fromFootprint && (cx < fp.minX - 64.0f || cx > fp.maxX + 64.0f || cy < fp.minY - 128.0f || cy > fp.maxY + 128.0f))
                    continue;
                if (n >= kCullCap) {
                    ++cullDropped;
                    continue;
                }
                float m[16];
                quadWorld(w.sx * kHalf, w.sy * kHalf, cx, cy, kCullHeight, m);
                p->cullWorld.insert(p->cullWorld.end(), m, m + 16);
                ++n;
            }
        work[v].cullCount = n;
    }

    // pipeline
    const D3D11_VIEWPORT vp = {0.0f, 0.0f, static_cast<float>(in.width), static_cast<float>(in.height), 0.0f, 1.0f};
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(p->raster);
    ctx->OMSetRenderTargets(1, &in.colour, in.depth);
    const UINT stride = 32, offset = 0;
    ctx->IASetVertexBuffers(0, 1, &p->vb, &stride, &offset);
    ctx->IASetIndexBuffer(p->ib, DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    uint32_t draws = 0;

    // 1. FOW CULL
    {
        ctx->IASetInputLayout(p->layoutCull);
        ctx->VSSetShader(p->vsCull, nullptr, 0);
        ctx->PSSetShader(p->psCull, nullptr, 0);
        ctx->VSSetConstantBuffers(0, 1, &p->cbCull);
        ctx->PSSetConstantBuffers(0, 1, &p->cbCull);
        const float blendFactor[4] = {0, 0, 0, 0};
        ctx->OMSetBlendState(p->blendCull, blendFactor, 0xFFFFFFFFu);
        ctx->OMSetDepthStencilState(p->depthCull, 0);
        uint8_t cb[kCullCbBytes];
        for (uint32_t v = 0; v < g.numViews; ++v) {
            const ViewWork& vw = work[v];
            for (uint32_t first = 0; first < vw.cullCount; first += kSlots) {
                const uint32_t n = std::min(kSlots, vw.cullCount - first);
                std::memset(cb, 0, sizeof cb);
                for (uint32_t s = 0; s < n; ++s) {
                    float wvp[16];
                    mul44(&p->cullWorld[static_cast<size_t>(vw.cullFirst + first + s) * 16], g.views[v].viewProj, wvp);
                    std::memcpy(cb + 64 * s, wvp, 64);
                }
                C5R_TRY(p->upload(ctx, p->cbCull, cb, sizeof cb));
                ctx->DrawIndexed(6 * n, 0, 0);
                ++draws;
            }
        }
        if (stats) {
            for (const ViewWork& vw : work) stats->cullQuads += vw.cullCount;
            stats->cullDropped = cullDropped;
        }
    }

    // 2. the three planes
    {
        ctx->IASetInputLayout(p->layoutBase);
        ctx->VSSetShader(p->vsBase, nullptr, 0);
        ctx->PSSetShader(p->psBase, nullptr, 0);
        ctx->VSSetConstantBuffers(0, 1, &p->cbBase);
        ctx->PSSetConstantBuffers(0, 1, &p->cbBase);
        const float blendFactor[4] = {0, 0, 0, 0};
        ctx->OMSetBlendState(p->blendPlane, blendFactor, 0xFFFFFFFFu);
        ctx->OMSetDepthStencilState(p->depthPlane, 0);
        ID3D11SamplerState* samp[2] = {p->sampler, p->sampler};
        ctx->PSSetSamplers(0, 2, samp);
        std::vector<uint8_t> cb(kBaseCbBytes, 0);
        for (int plane = 0; plane < 3; ++plane) {
            ID3D11ShaderResourceView* srv[2] = {p->cloud[plane], in.mask};
            ctx->PSSetShaderResources(0, 2, srv);
            const float s = kPlaneAnimScale[plane];
            const float anim[4] = {in.fowAnim[0], in.fowAnim[1], in.fowAnim[2] * s, in.fowAnim[3] * s};
            for (uint32_t v = 0; v < g.numViews; ++v) {
                const ViewWork& vw = work[v];
                const float* vpm = g.views[v].viewProj;
                std::vector<float> quads;   // world matrices in block order (bx outer, by inner), at most kPlaneCap
                for (int bx = vw.blocks[0]; bx < vw.blocks[1] && quads.size() < kPlaneCap * 16; ++bx)
                    for (int by = vw.blocks[2]; by < vw.blocks[3] && quads.size() < kPlaneCap * 16; ++by) {
                        const float hx = w.sx * kHalf, hy = w.sy * kHalf;
                        const float cx = (static_cast<float>(bx * 64) * w.sx + hx) + w.minX;
                        const float cy = (static_cast<float>(by * 64) * w.sy + hy) + w.minY;
                        float m[16];
                        quadWorld(hx, hy, cx, cy, kPlaneHeight[plane], m);
                        quads.insert(quads.end(), m, m + 16);
                    }
                const uint32_t total = static_cast<uint32_t>(quads.size() / 16);
                if (stats) stats->planeQuads[plane] += total;
                for (uint32_t first = 0; first < total; first += kSlots) {
                    const uint32_t n = std::min(kSlots, total - first);
                    std::fill(cb.begin(), cb.end(), static_cast<uint8_t>(0));
                    for (uint32_t q = 0; q < n; ++q) {
                        const float* world = &quads[static_cast<size_t>(first + q) * 16];
                        float wvp[16];
                        mul44(world, vpm, wvp);
                        std::memcpy(cb.data() + 64 * q, wvp, 64);
                        std::memcpy(cb.data() + kOffWorld + 64 * q, world, 64);
                    }
                    std::memcpy(cb.data() + kOffOverlay, g.views[v].overlayProj, 64);
                    std::memcpy(cb.data() + kOffAnim, anim, 16);
                    C5R_TRY(p->upload(ctx, p->cbBase, cb.data(), kBaseCbBytes));
                    ctx->DrawIndexed(6 * n, 0, 0);
                    ++draws;
                }
            }
        }
        ID3D11ShaderResourceView* none[2] = {nullptr, nullptr};
        ctx->PSSetShaderResources(0, 2, none);
    }
    if (stats) stats->draws = draws;
    return Status::Ok();
}

ID3D11ShaderResourceView* CloudRenderer::cloudBase() const { return m_impl ? m_impl->cloud[0] : nullptr; }

ID3D11ShaderResourceView* CloudRenderer::planeTexture(uint32_t i) const
{
    return m_impl && i < 3 ? m_impl->cloud[i] : nullptr;
}

} // namespace fow
} // namespace c5r
