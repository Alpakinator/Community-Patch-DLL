// render/src/bakepost/post.cpp - the terrain bake's passes after the blend (issue 3403). Behaviour as described in
// research/05-graphics-composition/terrain-bake-passes.md (D83: no decompiler output; the EXE's shaders are read from
// the player's EXE at run time through c5r::shaders). Interface and errors: c5r/bake/post.h.
#include "c5r/bake/post.h"

#include <windows.h>
#include <DirectXPackedVector.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "c5r/decal/decal.h"
#include "c5r/shaders/reflect.h"
#include "c5r/shaders/shadersection.h"

using Microsoft::WRL::ComPtr;

namespace c5r { namespace bake {

namespace {

constexpr uint32_t kGrid = kBlendGrid;           // 67 vertices per side
constexpr uint32_t kQuads = kGrid - 1;           // 66
constexpr uint32_t kGridVerts = kGrid * kGrid;   // 4,489
constexpr uint32_t kGridIndices = kQuads * kQuads * 6;  // 26,136
constexpr uint32_t kBlendMips = 10;              // 512 x 512 full chain
constexpr uint32_t kOwnBytes = 1280;             // TerrainShaderScreen slot-0 buffer (0x4C0 block, 80 registers)
constexpr uint32_t kFrameBytes = 0x250;          // TerrainFrame (shared id 11)

Status hrOk(HRESULT h, const char* what) {
    if (SUCCEEDED(h)) return Status::Ok();
    char b[32];
    std::snprintf(b, sizeof b, " (hr 0x%08lx)", static_cast<unsigned long>(h));
    return Status::fail(h == E_OUTOFMEMORY ? Err::OutOfMemory : Err::Device, std::string(what) + b);
}

std::string hexVa(uint32_t va) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x", va);
    return b;
}

// What a pass binds for a blob, checked against the blob's own declarations (c5r::shaders reflection).
struct Expect {
    const char* name;
    shaders::Stage stage;
    std::vector<std::pair<uint32_t, uint32_t>> resources;   // (slot, D3D10_SB_RESOURCE_DIMENSION): exactly these
    std::vector<std::pair<uint32_t, uint32_t>> cbMax;       // (slot, registers the bound buffer holds): declared within
    std::vector<const char*> inputs;                         // VS: semantic names the input layout provides
    uint32_t outputs;                                        // PS: number of render-target outputs
};

Status checkBlob(const shaders::ShaderBlob& b, const Expect& e) {
    const std::string who = std::string(e.name) + " blob " + hexVa(b.va);
    if (b.stage != e.stage) return Status::fail(Err::BadLayout, who + " has another stage");
    shaders::ShaderReflection r;
    Status st = shaders::reflect(b.bytes, b.size, r);
    if (!st.ok()) return Status::fail(Err::BadLayout, who + ": reflection failed: " + st.str());
    if (r.major != 5) return Status::fail(Err::BadLayout, who + " is not shader model 5");
    std::vector<std::pair<uint32_t, uint32_t>> res;
    for (const shaders::ResourceDecl& d : r.resources) res.push_back(std::make_pair(d.slot, d.dimension));
    if (res != e.resources) return Status::fail(Err::BadLayout, who + ": declared textures differ from what the pass binds");
    for (const shaders::CbDecl& c : r.cbuffers) {
        bool ok = false;
        for (const auto& m : e.cbMax)
            if (m.first == c.slot && c.vec4Count <= m.second) ok = true;
        if (!ok)
            return Status::fail(Err::BadLayout, who + ": constant buffer slot " + std::to_string(c.slot) + " (" +
                                                    std::to_string(c.vec4Count) + " registers) is not provided");
    }
    if (e.stage == shaders::Stage::Vertex) {
        for (const shaders::SigElement& s : r.input.elems) {
            bool ok = false;
            for (const char* n : e.inputs)
                if (s.semantic == n) ok = true;
            if (!ok) return Status::fail(Err::BadLayout, who + ": input " + s.semantic + " is not in the vertex layout");
        }
    } else {
        uint32_t n = 0;
        for (const shaders::SigElement& s : r.output.elems)
            if (s.semantic == "SV_Target" || s.semantic == "SV_TARGET") ++n;
        if (n != e.outputs) return Status::fail(Err::BadLayout, who + ": " + std::to_string(n) + " targets, the pass has " +
                                                                    std::to_string(e.outputs));
    }
    return Status::Ok();
}

struct Target {
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11ShaderResourceView> srv;
    std::vector<ComPtr<ID3D11RenderTargetView>> rtv;  // one per mip (only mip 0 for the colour targets)
};

Status makeTarget(ID3D11Device* dev, uint32_t size, uint32_t mips, DXGI_FORMAT texFmt, DXGI_FORMAT viewFmt,
                  bool rtvPerMip, Target& t) {
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = d.Height = size;
    d.MipLevels = mips;
    d.ArraySize = 1;
    d.Format = texFmt;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    d.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    C5R_TRY(hrOk(dev->CreateTexture2D(&d, nullptr, &t.tex), "CreateTexture2D (bake target)"));
    D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = viewFmt;
    sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sv.Texture2D.MipLevels = mips;
    C5R_TRY(hrOk(dev->CreateShaderResourceView(t.tex.Get(), &sv, &t.srv), "CreateShaderResourceView (bake target)"));
    t.rtv.clear();
    for (uint32_t m = 0; m < (rtvPerMip ? mips : 1u); ++m) {
        D3D11_RENDER_TARGET_VIEW_DESC rv = {};
        rv.Format = viewFmt;
        rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        rv.Texture2D.MipSlice = m;
        ComPtr<ID3D11RenderTargetView> v;
        C5R_TRY(hrOk(dev->CreateRenderTargetView(t.tex.Get(), &rv, &v), "CreateRenderTargetView (bake target)"));
        t.rtv.push_back(v);
    }
    return Status::Ok();
}

void setReg(std::vector<float>& cb, uint32_t reg, float x, float y = 0, float z = 0, float w = 0) {
    cb[reg * 4 + 0] = x;
    cb[reg * 4 + 1] = y;
    cb[reg * 4 + 2] = z;
    cb[reg * 4 + 3] = w;
}

}  // namespace

uint32_t cacheMips(uint32_t sizeClass) {
    switch (sizeClass) {
    case 64: return 4;
    case 128: return 5;
    case 256: return 6;
    case 512: return 7;
    default: return 0;
    }
}

uint32_t nearCacheMips(uint32_t size) {
    switch (size) {
    case 64: return 7;
    case 128: return 8;
    case 256: return 9;
    case 512: return 10;
    default: return 0;
    }
}

uint64_t nearCacheBytes(uint32_t size) {
    const uint32_t mips = nearCacheMips(size);
    uint64_t b = 0;
    for (uint32_t m = 0; m < mips; ++m) {
        const uint64_t s = size >> m;
        b += s * s * 4;
    }
    return b;
}

Status createNearCache(ID3D11Device* dev, uint32_t size, ID3D11Texture2D** out) {
    if (!out) return Status::fail(Err::BadLayout, "createNearCache: no output");
    *out = nullptr;
    if (!dev) return Status::fail(Err::BadLayout, "createNearCache: no device");
    const uint32_t mips = nearCacheMips(size);
    if (!mips) return Status::fail(Err::BadLayout, "createNearCache: size " + std::to_string(size) + " is not 64/128/256/512");
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = d.Height = size;
    d.MipLevels = mips;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    d.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    return hrOk(dev->CreateTexture2D(&d, nullptr, out), "CreateTexture2D (near cache)");
}

void buildGridIndices(uint32_t gw, uint32_t gh, std::vector<uint16_t>& out) {
    out.clear();
    if (gw < 2 || gh < 2) return;
    const uint32_t qx = gw - 1, qy = gh - 1;
    out.reserve(static_cast<size_t>(qx) * qy * 6);
    for (uint32_t strip = 0; strip < qx; strip += 6)
        for (uint32_t j = 0; j < qy; ++j)
            for (uint32_t i = strip; i < strip + 6 && i < qx; ++i) {
                const uint16_t a = static_cast<uint16_t>(j * gw + i), b = static_cast<uint16_t>(a + 1),
                               c = static_cast<uint16_t>(a + gw), d = static_cast<uint16_t>(a + gw + 1);
                const uint16_t even[6] = {d, b, c, c, b, a}, odd[6] = {d, b, a, c, d, a};
                out.insert(out.end(), (i & 1) ? odd : even, ((i & 1) ? odd : even) + 6);
            }
}

void buildGridIndices(uint16_t out[66 * 66 * 6]) {
    std::vector<uint16_t> v;
    buildGridIndices(kGrid, kGrid, v);
    std::memcpy(out, v.data(), v.size() * sizeof(uint16_t));
}

void buildShadowGrid(int originX, int originY, uint32_t gw, uint32_t gh, std::vector<BlendVertex>& out) {
    out.assign(static_cast<size_t>(gw) * gh, BlendVertex());
    std::memset(out.data(), 0, sizeof(BlendVertex) * out.size());
    const double nx = gw > 1 ? static_cast<double>(gw - 1) : 1.0;
    const float ny = gh > 1 ? static_cast<float>(gh - 1) : 1.0f;
    for (uint32_t j = 0; j < gh; ++j)
        for (uint32_t i = 0; i < gw; ++i) {
            BlendVertex& v = out[j * gw + i];
            v.pos[0] = static_cast<float>((2.0 * i - nx) / nx);
            v.pos[1] = 1.0f - (static_cast<float>(j) / ny) * 2.0f;  // +0 at j = 33 of 67, as the EXE
            v.pos[2] = 0.5f;
            v.sample[0] = static_cast<float>(originX - 1 + static_cast<int>(i));
            v.sample[1] = static_cast<float>(originY - 1 + static_cast<int>(j));
        }
}

void buildShadowGrid(int originX, int originY, BlendVertex out[67 * 67]) {
    std::vector<BlendVertex> v;
    buildShadowGrid(originX, originY, kGrid, kGrid, v);
    std::memcpy(out, v.data(), v.size() * sizeof(BlendVertex));
}

void buildCacheGrid(const float* normals, uint32_t sizeClass, uint32_t gw, uint32_t gh, std::vector<CacheVertex>& out) {
    using DirectX::PackedVector::XMConvertFloatToHalf;
    out.assign(static_cast<size_t>(gw) * gh, CacheVertex());
    const float half = sizeClass ? 1.0f / static_cast<float>(sizeClass) : 0.0f;
    const double nx = gw > 1 ? static_cast<double>(gw - 1) : 1.0;
    const float fx = static_cast<float>(nx), ny = gh > 1 ? static_cast<float>(gh - 1) : 1.0f;
    for (uint32_t j = 0; j < gh; ++j)
        for (uint32_t i = 0; i < gw; ++i) {
            const uint32_t k = j * gw + i;
            CacheVertex& v = out[k];
            v.pos[0] = static_cast<float>((2.0 * i - nx) / nx) - half;
            v.pos[1] = 1.0f - (static_cast<float>(j) / ny) * 2.0f + half;
            v.pos[2] = 0.5f;
            for (int c = 0; c < 3; ++c) v.normal[c] = XMConvertFloatToHalf(normals[k * 3 + c]);
            v.normal[3] = XMConvertFloatToHalf(1.0f);
            v.uv[0] = XMConvertFloatToHalf(static_cast<float>(i) / fx);
            v.uv[1] = XMConvertFloatToHalf(static_cast<float>(j) / ny);
        }
}

void buildCacheGrid(const float* normals, uint32_t sizeClass, CacheVertex out[67 * 67]) {
    std::vector<CacheVertex> v;
    buildCacheGrid(normals, sizeClass, kGrid, kGrid, v);
    std::memcpy(out, v.data(), v.size() * sizeof(CacheVertex));
}

struct BakePost::Impl {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11VertexShader> normalVs, shadowVs, cacheVs, compressVs;
    ComPtr<ID3D11PixelShader> normalPs, shadowPs, cachePs1, cachePs2, compressPs;
    ComPtr<ID3D11InputLayout> quadLayout, shadowLayout, cacheLayout, compressLayout;
    ComPtr<ID3D11BlendState> blendOff, blendAdd;
    ComPtr<ID3D11DepthStencilState> depthOff;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11SamplerState> sampA, sampB;  // linear wrap (id 0), linear clamp (id 5)
    ComPtr<ID3D11Buffer> quadVb, quadIb, gridIb, shadowVb, cacheVb, normalCb, ownCb, frameCb, compressCb;
    Target normal, shadow;                    // 512 RGBA8, 10 mips
    uint32_t sizeClass = 0;
    Target cache[2], blocks[2];               // per size class
};

BakePost::BakePost() {}
BakePost::~BakePost() { release(); }
void BakePost::release() {
    delete p_;
    p_ = nullptr;
}

ID3D11Texture2D* BakePost::target(PostTarget which) const {
    if (!p_) return nullptr;
    switch (which) {
    case PostTarget::Normal: return p_->normal.tex.Get();
    case PostTarget::Shadow: return p_->shadow.tex.Get();
    case PostTarget::Cache0: return p_->cache[0].tex.Get();
    case PostTarget::Cache1: return p_->cache[1].tex.Get();
    case PostTarget::Blocks0: return p_->blocks[0].tex.Get();
    case PostTarget::Blocks1: return p_->blocks[1].tex.Get();
    }
    return nullptr;
}

Status BakePost::init(ID3D11Device* device, const shaders::ShaderSection& exe, const PostShaderVas& vas) {
    release();
    if (!device) return Status::fail(Err::BadLayout, "bakepost init: no device");
    std::unique_ptr<Impl> p(new Impl());
    p->dev = device;
    using shaders::Stage;
    const uint32_t D2 = 3, D3 = 5, CUBE = 6;  // D3D10_SB_RESOURCE_DIMENSION
    const std::vector<const char*> quadIn = {"POSITION", "NORMAL", "TEXCOORD"};
    const std::vector<const char*> blendIn = {"POSITION", "TEXCOORD", "BLENDINDICES"};
    struct Want { uint32_t va; Expect e; const shaders::ShaderBlob* blob; };
    std::vector<Want> want = {
        {vas.normalVs, {"NormalCalculate VS", Stage::Vertex, {}, {}, quadIn, 0}, nullptr},
        {vas.normalPs, {"NormalCalculate PS", Stage::Pixel, {{0, D2}}, {{1, 2}}, {}, 1}, nullptr},
        {vas.shadowVs, {"Macro Shadow VS", Stage::Vertex, {}, {{0, kOwnBytes / 16}}, blendIn, 0}, nullptr},
        {vas.shadowPs, {"Macro Shadow PS", Stage::Pixel, {{0, D2}}, {{0, kOwnBytes / 16}, {1, kFrameBytes / 16}}, {}, 1}, nullptr},
        {vas.cacheVs, {"CacheBake VS", Stage::Vertex, {}, {}, quadIn, 0}, nullptr},
        {vas.cachePs1, {"CacheBake PS (one target)", Stage::Pixel, {{0, D2}, {1, D2}, {2, D2}, {4, CUBE}, {5, CUBE}, {8, D3}},
                        {{1, kFrameBytes / 16}}, {}, 1}, nullptr},
        {vas.cachePs2, {"CacheBake PS (two targets)", Stage::Pixel,
                        {{0, D2}, {1, D2}, {2, D2}, {3, D2}, {4, CUBE}, {5, CUBE}, {8, D3}}, {{1, kFrameBytes / 16}}, {}, 2}, nullptr},
        {vas.compressVs, {"CompressBC VS", Stage::Vertex, {}, {}, {"POSITION"}, 0}, nullptr},
        {vas.compressPs, {"CompressBC PS", Stage::Pixel, {{0, D2}}, {{0, 1}}, {}, 1}, nullptr},
    };
    for (Want& w : want) {
        w.blob = exe.findByVa(w.va);
        if (!w.blob) return Status::fail(Err::NotFound, std::string(w.e.name) + " blob " + hexVa(w.va) + " not in the .shader section");
        C5R_TRY(checkBlob(*w.blob, w.e));
    }
    auto vs = [&](int i, ComPtr<ID3D11VertexShader>& out) {
        return hrOk(device->CreateVertexShader(want[i].blob->bytes, want[i].blob->size, nullptr, &out), "CreateVertexShader (EXE)");
    };
    auto ps = [&](int i, ComPtr<ID3D11PixelShader>& out) {
        return hrOk(device->CreatePixelShader(want[i].blob->bytes, want[i].blob->size, nullptr, &out), "CreatePixelShader (EXE)");
    };
    C5R_TRY(vs(0, p->normalVs));
    C5R_TRY(ps(1, p->normalPs));
    C5R_TRY(vs(2, p->shadowVs));
    C5R_TRY(ps(3, p->shadowPs));
    C5R_TRY(vs(4, p->cacheVs));
    C5R_TRY(ps(5, p->cachePs1));
    C5R_TRY(ps(6, p->cachePs2));
    C5R_TRY(vs(7, p->compressVs));
    C5R_TRY(ps(8, p->compressPs));

    // Input layouts: the EXE's own vertex formats (terrain-bake-passes.md 3.1-3.4).
    const D3D11_INPUT_ELEMENT_DESC quad[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    const D3D11_INPUT_ELEMENT_DESC blend[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 2, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 3, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 1, DXGI_FORMAT_R8G8B8A8_UINT, 0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    const D3D11_INPUT_ELEMENT_DESC cache[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    C5R_TRY(hrOk(device->CreateInputLayout(quad, 3, want[0].blob->bytes, want[0].blob->size, &p->quadLayout), "CreateInputLayout (quad)"));
    C5R_TRY(hrOk(device->CreateInputLayout(blend, 7, want[2].blob->bytes, want[2].blob->size, &p->shadowLayout), "CreateInputLayout (blend grid)"));
    C5R_TRY(hrOk(device->CreateInputLayout(cache, 3, want[4].blob->bytes, want[4].blob->size, &p->cacheLayout), "CreateInputLayout (cache grid)"));
    C5R_TRY(hrOk(device->CreateInputLayout(quad, 3, want[7].blob->bytes, want[7].blob->size, &p->compressLayout), "CreateInputLayout (compress quad)"));

    // States (the capture's objects, terrain-bake-passes.md 2).
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = FALSE;
    bd.RenderTarget[0].SrcBlend = bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOp = bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    C5R_TRY(hrOk(device->CreateBlendState(&bd, &p->blendOff), "CreateBlendState (off)"));
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].DestBlend = bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    C5R_TRY(hrOk(device->CreateBlendState(&bd, &p->blendAdd), "CreateBlendState (add)"));
    D3D11_DEPTH_STENCIL_DESC dd = {};
    dd.DepthEnable = FALSE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    dd.FrontFace.StencilFailOp = dd.FrontFace.StencilDepthFailOp = dd.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    dd.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    dd.BackFace = dd.FrontFace;
    C5R_TRY(hrOk(device->CreateDepthStencilState(&dd, &p->depthOff), "CreateDepthStencilState"));
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.FrontCounterClockwise = TRUE;
    rd.DepthClipEnable = TRUE;
    C5R_TRY(hrOk(device->CreateRasterizerState(&rd, &p->raster), "CreateRasterizerState"));
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    C5R_TRY(hrOk(device->CreateSamplerState(&sd, &p->sampA), "CreateSamplerState (linear wrap)"));
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    C5R_TRY(hrOk(device->CreateSamplerState(&sd, &p->sampB), "CreateSamplerState (linear clamp)"));

    // Buffers: the full-target quad (3.1), the grid indices, dynamic grids and constant buffers.
    const float quadV[32] = {1, 1, 0, 1, 1, 0, 1, 0, -1, 1, 0, -1, 1, 0, 0, 0,
                             1, -1, 0, 1, -1, 0, 1, 1, -1, -1, 0, -1, -1, 0, 0, 1};
    const uint32_t quadI[6] = {2, 1, 0, 3, 1, 2};
    auto immutable = [&](const void* data, UINT bytes, UINT bind, ComPtr<ID3D11Buffer>& out, const char* what) {
        D3D11_BUFFER_DESC d = {};
        d.ByteWidth = bytes;
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = bind;
        D3D11_SUBRESOURCE_DATA s = {data, 0, 0};
        return hrOk(device->CreateBuffer(&d, &s, &out), what);
    };
    auto dynamic = [&](UINT bytes, UINT bind, ComPtr<ID3D11Buffer>& out, const char* what) {
        D3D11_BUFFER_DESC d = {};
        d.ByteWidth = bytes;
        d.Usage = D3D11_USAGE_DYNAMIC;
        d.BindFlags = bind;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        return hrOk(device->CreateBuffer(&d, nullptr, &out), what);
    };
    C5R_TRY(immutable(quadV, sizeof quadV, D3D11_BIND_VERTEX_BUFFER, p->quadVb, "CreateBuffer (quad)"));
    C5R_TRY(immutable(quadI, sizeof quadI, D3D11_BIND_INDEX_BUFFER, p->quadIb, "CreateBuffer (quad indices)"));
    // The grid's indices depend on the tile's extent (67 x 67, or 66 on the short side at the map's last column / row):
    // a dynamic buffer of the largest size, written per run.
    C5R_TRY(dynamic(kGridIndices * 2, D3D11_BIND_INDEX_BUFFER, p->gridIb, "CreateBuffer (grid indices)"));
    C5R_TRY(dynamic(kGridVerts * sizeof(BlendVertex), D3D11_BIND_VERTEX_BUFFER, p->shadowVb, "CreateBuffer (shadow grid)"));
    C5R_TRY(dynamic(kGridVerts * sizeof(CacheVertex), D3D11_BIND_VERTEX_BUFFER, p->cacheVb, "CreateBuffer (cache grid)"));
    C5R_TRY(dynamic(32, D3D11_BIND_CONSTANT_BUFFER, p->normalCb, "CreateBuffer (normal constants)"));
    C5R_TRY(dynamic(kOwnBytes, D3D11_BIND_CONSTANT_BUFFER, p->ownCb, "CreateBuffer (own constants)"));
    C5R_TRY(dynamic(kFrameBytes, D3D11_BIND_CONSTANT_BUFFER, p->frameCb, "CreateBuffer (frame constants)"));
    C5R_TRY(dynamic(16, D3D11_BIND_CONSTANT_BUFFER, p->compressCb, "CreateBuffer (compress constants)"));

    C5R_TRY(makeTarget(device, kBlendTargetSize, kBlendMips, DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM, false, p->normal));
    C5R_TRY(makeTarget(device, kBlendTargetSize, kBlendMips, DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM, false, p->shadow));
    p_ = p.release();
    return Status::Ok();
}

namespace {

Status upload(ID3D11DeviceContext* ctx, ID3D11Buffer* b, const void* data, size_t bytes) {
    D3D11_MAPPED_SUBRESOURCE m;
    C5R_TRY(hrOk(ctx->Map(b, 0, D3D11_MAP_WRITE_DISCARD, 0, &m), "Map (bakepost)"));
    std::memcpy(m.pData, data, bytes);
    ctx->Unmap(b, 0);
    return Status::Ok();
}

Status checkBlendTarget(ID3D11Texture2D* t, int i) {
    static const char* names[3] = {"albedo", "normal", "spec"};
    const std::string who = std::string("blend target ") + names[i];
    if (!t) return Status::fail(Err::BadLayout, who + " missing");
    D3D11_TEXTURE2D_DESC d;
    t->GetDesc(&d);
    if (d.Format != DXGI_FORMAT_R16G16B16A16_FLOAT && d.Format != DXGI_FORMAT_R16G16B16A16_TYPELESS)
        return Status::fail(Err::BadLayout, who + ": format " + std::to_string(d.Format) + ", want R16G16B16A16_FLOAT");
    if (d.Width != kBlendTargetSize || d.Height != kBlendTargetSize)
        return Status::fail(Err::BadLayout, who + ": " + std::to_string(d.Width) + " x " + std::to_string(d.Height) + ", want 512 x 512");
    if (d.MipLevels != kBlendMips || d.ArraySize != 1 || d.SampleDesc.Count != 1)
        return Status::fail(Err::BadLayout, who + ": want 10 mips, one slice, no MSAA");
    if ((d.BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE)) != (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE) ||
        !(d.MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS))
        return Status::fail(Err::BadLayout, who + ": want RENDER_TARGET | SHADER_RESOURCE and GENERATE_MIPS");
    return Status::Ok();
}

// A near cache given to run() (issue 0332): createNearCache's kind; size returns its width.
Status checkNearTarget(ID3D11Texture2D* t, int k, uint32_t& size) {
    const std::string who = std::string("near cache ") + (k ? "t8" : "t0");
    D3D11_TEXTURE2D_DESC d;
    t->GetDesc(&d);
    if (d.Format != DXGI_FORMAT_R8G8B8A8_UNORM && d.Format != DXGI_FORMAT_R8G8B8A8_TYPELESS)
        return Status::fail(Err::BadLayout, who + ": format " + std::to_string(d.Format) + ", want R8G8B8A8_UNORM");
    if (d.Width != d.Height || !nearCacheMips(d.Width))
        return Status::fail(Err::BadLayout, who + ": " + std::to_string(d.Width) + " x " + std::to_string(d.Height) +
                                                ", want a square of 64 / 128 / 256 / 512");
    if (d.MipLevels != nearCacheMips(d.Width) || d.ArraySize != 1 || d.SampleDesc.Count != 1)
        return Status::fail(Err::BadLayout, who + ": want the full chain (" + std::to_string(nearCacheMips(d.Width)) +
                                                " mips), one slice, no MSAA");
    if ((d.BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE)) != (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE) ||
        !(d.MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS))
        return Status::fail(Err::BadLayout, who + ": want RENDER_TARGET | SHADER_RESOURCE and GENERATE_MIPS");
    size = d.Width;
    return Status::Ok();
}

Status checkSrv(ID3D11ShaderResourceView* v, D3D11_SRV_DIMENSION dim, const char* who) {
    if (!v) return Status::fail(Err::BadLayout, std::string(who) + " missing");
    D3D11_SHADER_RESOURCE_VIEW_DESC d;
    v->GetDesc(&d);
    if (d.ViewDimension != dim) return Status::fail(Err::BadLayout, std::string(who) + ": wrong view dimension");
    return Status::Ok();
}

}  // namespace

Status BakePost::run(ID3D11DeviceContext* ctx, const PostInputs& in, PostOutputs& out) {
    out.t0 = out.t8 = nullptr;
    if (!p_) return Status::fail(Err::NotFound, "bakepost run before init");
    if (!ctx) return Status::fail(Err::BadLayout, "bakepost run: no context");
    const uint32_t mips = cacheMips(in.sizeClass);
    if (!mips) return Status::fail(Err::BadLayout, "size class " + std::to_string(in.sizeClass) + " is not 64/128/256/512");
    const bool anyDecals = in.decals && !in.decals->indices.empty();
    if (anyDecals && !in.decalPass) return Status::fail(Err::BadLayout, "decals given without a decal pass");
    const bool blendDecals = anyDecals && decal::DecalPass::count(*in.decals, 0) > 0;
    const bool shadowDecals = anyDecals && decal::DecalPass::count(*in.decals, 1) > 0;
    for (int i = 0; i < 3; ++i) C5R_TRY(checkBlendTarget(in.blend[i], i));
    C5R_TRY(checkSrv(in.heightPage, D3D11_SRV_DIMENSION_TEXTURE2D, "height page"));
    C5R_TRY(checkSrv(in.sunCube, D3D11_SRV_DIMENSION_TEXTURECUBE, "Sunlight cube"));
    C5R_TRY(checkSrv(in.shadowCube, D3D11_SRV_DIMENSION_TEXTURECUBE, "Shadowlight cube"));
    C5R_TRY(checkSrv(in.colorKey, D3D11_SRV_DIMENSION_TEXTURE3D, "ColorKey LUT"));
    if (in.gridW < 2 || in.gridH < 2 || in.gridW > kGrid || in.gridH > kGrid)
        return Status::fail(Err::BadLayout, "grid " + std::to_string(in.gridW) + " x " + std::to_string(in.gridH) +
                                                ": want 2..67 per side");
    const uint32_t gridVerts = in.gridW * in.gridH, gridIndices = (in.gridW - 1) * (in.gridH - 1) * 6;
    if (!in.gridNormals || in.gridNormalCount != gridVerts)
        return Status::fail(Err::BadLayout, "grid normals: want " + std::to_string(in.gridW) + " x " +
                                                std::to_string(in.gridH) + " = " + std::to_string(gridVerts) + ", got " +
                                                std::to_string(in.gridNormalCount));
    if (in.k.shadowDraws == 0 || in.k.shadowDraws > 16)
        return Status::fail(Err::BadLayout, "shadow draws must be 1..16");
    if (in.macroShadowGiven) {   // issue 0371: 512 x 512, R8G8B8A8 (UNORM or TYPELESS), at least one mip
        D3D11_TEXTURE2D_DESC d;
        in.macroShadowGiven->GetDesc(&d);
        if (d.Width != 512 || d.Height != 512 || d.ArraySize != 1 || d.SampleDesc.Count != 1 ||
            (d.Format != DXGI_FORMAT_R8G8B8A8_UNORM && d.Format != DXGI_FORMAT_R8G8B8A8_TYPELESS))
            return Status::fail(Err::BadLayout, "given macro shadow: want 512 x 512 R8G8B8A8, one slice");
    }
    // The near pair (issue 0332): checked before anything is drawn.
    uint32_t nearSize = 0;
    if (in.nearCache[0] || in.nearCache[1]) {
        if (!in.nearCache[0]) return Status::fail(Err::BadLayout, "near cache t8 given without t0");
        if (in.needSecond != (in.nearCache[1] != nullptr))
            return Status::fail(Err::BadLayout, in.needSecond ? "near cache t8 missing (the tile makes t8)"
                                                              : "near cache t8 given for a tile without t8");
        C5R_TRY(checkNearTarget(in.nearCache[0], 0, nearSize));
        if (in.nearCache[1]) {
            uint32_t s1 = 0;
            C5R_TRY(checkNearTarget(in.nearCache[1], 1, s1));
            if (s1 != nearSize) return Status::fail(Err::BadLayout, "near caches of different sizes");
            if (in.nearCache[1] == in.nearCache[0]) return Status::fail(Err::BadLayout, "near caches t0 and t8 are one texture");
        }
    }
    Impl& p = *p_;
    ID3D11Device* dev = p.dev.Get();
    // Views of the near pair (made per run: the pool's textures change from tile to tile).
    ComPtr<ID3D11RenderTargetView> nearRtv[2];
    ComPtr<ID3D11ShaderResourceView> nearSrv[2];
    for (int k = 0; k < 2 && nearSize; ++k) {
        if (!in.nearCache[k]) continue;
        D3D11_RENDER_TARGET_VIEW_DESC rv = {};
        rv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        C5R_TRY(hrOk(dev->CreateRenderTargetView(in.nearCache[k], &rv, &nearRtv[k]), "CreateRenderTargetView (near cache)"));
        D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MipLevels = nearCacheMips(nearSize);
        C5R_TRY(hrOk(dev->CreateShaderResourceView(in.nearCache[k], &sv, &nearSrv[k]), "CreateShaderResourceView (near cache)"));
    }

    // Per size-class targets.
    if (p.sizeClass != in.sizeClass) {
        for (int k = 0; k < 2; ++k) {
            C5R_TRY(makeTarget(dev, in.sizeClass, mips, DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM, false, p.cache[k]));
            C5R_TRY(makeTarget(dev, in.sizeClass / 4, mips, DXGI_FORMAT_R32G32_UINT, DXGI_FORMAT_R32G32_UINT, true, p.blocks[k]));
        }
        p.sizeClass = in.sizeClass;
    }
    ComPtr<ID3D11ShaderResourceView> blendSrv[3];
    for (int i = 0; i < 3; ++i) {
        D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MipLevels = kBlendMips;
        C5R_TRY(hrOk(dev->CreateShaderResourceView(in.blend[i], &sv, &blendSrv[i]), "CreateShaderResourceView (blend target)"));
    }
    // Constant data.
    std::vector<float> normalCb(8, 0.0f), own(kOwnBytes / 4, 0.0f), frame(kFrameBytes / 4, 0.0f);
    setReg(normalCb, 0, in.k.normalStep[0], in.k.normalStep[1]);
    setReg(normalCb, 1, in.k.normalStrength);
    setReg(own, 73, in.k.heightOffset[0], in.k.heightOffset[1]);
    setReg(own, 75, 0, 0, 1.0f / static_cast<float>(in.k.shadowDraws));
    setReg(frame, 16, in.k.hexScale[0], in.k.hexScale[1], in.k.hexScale[2]);
    setReg(frame, 17, in.k.shadowDir[0], in.k.shadowDir[1], in.k.shadowDir[2]);
    setReg(frame, 21, in.k.halfVector[0], in.k.halfVector[1], in.k.halfVector[2]);
    setReg(frame, 22, in.k.heightTexelScale[0], in.k.heightTexelScale[1]);
    std::vector<BlendVertex> sg;
    buildShadowGrid(in.originX, in.originY, in.gridW, in.gridH, sg);
    std::vector<CacheVertex> cg;
    buildCacheGrid(in.gridNormals, in.sizeClass, in.gridW, in.gridH, cg);
    std::vector<uint16_t> gi;
    buildGridIndices(in.gridW, in.gridH, gi);
    C5R_TRY(upload(ctx, p.gridIb.Get(), gi.data(), gi.size() * sizeof(uint16_t)));
    C5R_TRY(upload(ctx, p.normalCb.Get(), normalCb.data(), 32));
    C5R_TRY(upload(ctx, p.ownCb.Get(), own.data(), kOwnBytes));
    C5R_TRY(upload(ctx, p.frameCb.Get(), frame.data(), kFrameBytes));
    C5R_TRY(upload(ctx, p.shadowVb.Get(), sg.data(), sg.size() * sizeof(BlendVertex)));
    C5R_TRY(upload(ctx, p.cacheVb.Get(), cg.data(), cg.size() * sizeof(CacheVertex)));

    ID3D11ShaderResourceView* const none[16] = {};
    ID3D11RenderTargetView* const noRt[8] = {};
    auto common = [&](float size) {
        ctx->OMSetRenderTargets(8, noRt, nullptr);
        ctx->PSSetShaderResources(0, 16, none);
        D3D11_VIEWPORT vp = {0, 0, size, size, 0, 1};
        ctx->RSSetViewports(1, &vp);
        ctx->OMSetDepthStencilState(p.depthOff.Get(), 0);
        ctx->RSSetState(p.raster.Get());
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->HSSetShader(nullptr, nullptr, 0);
        ctx->DSSetShader(nullptr, nullptr, 0);
        ctx->GSSetShader(nullptr, nullptr, 0);
    };
    const float zero4[4] = {0, 0, 0, 0};
    const UINT quadStride = 32, off = 0;

    // 1a. The decals into mip 0 of the blend targets (epic 0166): between the blend and NormalCalculate, as the EXE.
    if (blendDecals) C5R_TRY(in.decalPass->drawBlend(ctx, in.originX, in.originY, *in.decals, in.blend));
    // 1. Mips of the blend targets (again when decals changed mip 0).
    if (!in.blendMipsReady || blendDecals)
        for (int i = 0; i < 3; ++i) ctx->GenerateMips(blendSrv[i].Get());

    // 2. Normal Calculation.
    common(512);
    ctx->OMSetRenderTargets(1, p.normal.rtv[0].GetAddressOf(), nullptr);
    ctx->OMSetBlendState(p.blendOff.Get(), zero4, 0xffffffff);
    {
        ID3D11ShaderResourceView* t[2] = {blendSrv[1].Get(), blendSrv[1].Get()};
        ID3D11SamplerState* s[2] = {p.sampB.Get(), p.sampB.Get()};
        ID3D11Buffer* cb[2] = {nullptr, p.normalCb.Get()};
        ctx->PSSetShaderResources(0, 2, t);
        ctx->PSSetSamplers(0, 2, s);
        ctx->VSSetConstantBuffers(0, 2, cb);
        ctx->PSSetConstantBuffers(0, 2, cb);
    }
    ctx->IASetIndexBuffer(p.quadIb.Get(), DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetVertexBuffers(0, 1, p.quadVb.GetAddressOf(), &quadStride, &off);
    ctx->IASetInputLayout(p.quadLayout.Get());
    ctx->VSSetShader(p.normalVs.Get(), nullptr, 0);
    ctx->PSSetShader(p.normalPs.Get(), nullptr, 0);
    ctx->DrawIndexed(6, 0, 0);
    ctx->OMSetRenderTargets(8, noRt, nullptr);
    ctx->GenerateMips(p.normal.srv.Get());

    // 3. Macro Shadow.
    common(512);
    ctx->OMSetRenderTargets(1, p.shadow.rtv[0].GetAddressOf(), nullptr);
    ctx->ClearRenderTargetView(p.shadow.rtv[0].Get(), zero4);
    ctx->OMSetBlendState(p.blendAdd.Get(), zero4, 0xffffffff);
    {
        ID3D11ShaderResourceView* t[1] = {in.heightPage};
        ID3D11SamplerState* s[1] = {p.sampA.Get()};
        ID3D11Buffer* cb[2] = {p.ownCb.Get(), p.frameCb.Get()};
        ctx->PSSetShaderResources(0, 1, t);
        ctx->PSSetSamplers(0, 1, s);
        ctx->VSSetConstantBuffers(0, 2, cb);
        ctx->PSSetConstantBuffers(0, 2, cb);
    }
    const UINT blendStride = sizeof(BlendVertex);
    ctx->IASetIndexBuffer(p.gridIb.Get(), DXGI_FORMAT_R16_UINT, 0);
    ctx->IASetVertexBuffers(0, 1, p.shadowVb.GetAddressOf(), &blendStride, &off);
    ctx->IASetInputLayout(p.shadowLayout.Get());
    ctx->VSSetShader(p.shadowVs.Get(), nullptr, 0);
    ctx->PSSetShader(p.shadowPs.Get(), nullptr, 0);
    for (uint32_t d = 0; d < in.k.shadowDraws; ++d) ctx->DrawIndexed(gridIndices, 0, 0);
    ctx->OMSetRenderTargets(8, noRt, nullptr);
    // 3a. "Decal Macro Shadow": the pass-1 decals (tree shadows) into the macro-shadow target, ONE / ONE.
    if (shadowDecals) C5R_TRY(in.decalPass->drawMacroShadow(ctx, in.originX, in.originY, *in.decals, p.shadow.tex.Get()));
    // issue 0371, tests and replays only: the EXE's own macro-shadow result replaces the target's mip 0 (checked above)
    if (in.macroShadowGiven) ctx->CopySubresourceRegion(p.shadow.tex.Get(), 0, 0, 0, 0, in.macroShadowGiven, 0, nullptr);
    ctx->GenerateMips(p.shadow.srv.Get());

    // 4. Terrain Cache (CacheBake): in state 5 first straight into the near pair at its size (issue 0332,
    // terrain-bake-passes.md 3.5.1), then into the scratch targets of the size class for the BC1 path. Same program, grid,
    // inputs and states; only the targets and the viewport differ (the VS does not read POSITION, whose half-texel margin
    // is the size class's).
    const bool two = in.needSecond;
    const UINT cacheStride = sizeof(CacheVertex);
    auto cacheBake = [&](ID3D11RenderTargetView* rt0, ID3D11RenderTargetView* rt1, uint32_t size) {
        common(static_cast<float>(size));
        ID3D11RenderTargetView* rt[2] = {rt0, rt1};
        ctx->OMSetRenderTargets(two ? 2 : 1, rt, nullptr);
        ctx->OMSetBlendState(p.blendOff.Get(), zero4, 0xffffffff);
        ID3D11ShaderResourceView* t[9] = {p.normal.srv.Get(), blendSrv[0].Get(), p.shadow.srv.Get(), blendSrv[2].Get(),
                                          in.sunCube, in.shadowCube, nullptr, nullptr, in.colorKey};
        ID3D11SamplerState* b = p.sampB.Get();
        ID3D11SamplerState* a = p.sampA.Get();
        ID3D11SamplerState* s[9] = {b, b, b, b, a, a, a, a, b};
        ID3D11Buffer* cb[2] = {nullptr, p.frameCb.Get()};
        ctx->PSSetShaderResources(0, 9, t);
        ctx->PSSetSamplers(0, 9, s);
        ctx->VSSetConstantBuffers(0, 2, cb);
        ctx->PSSetConstantBuffers(0, 2, cb);
        ctx->IASetIndexBuffer(p.gridIb.Get(), DXGI_FORMAT_R16_UINT, 0);
        ctx->IASetVertexBuffers(0, 1, p.cacheVb.GetAddressOf(), &cacheStride, &off);
        ctx->IASetInputLayout(p.cacheLayout.Get());
        ctx->VSSetShader(p.cacheVs.Get(), nullptr, 0);
        ctx->PSSetShader(two ? p.cachePs2.Get() : p.cachePs1.Get(), nullptr, 0);
        ctx->DrawIndexed(gridIndices, 0, 0);
        ctx->OMSetRenderTargets(8, noRt, nullptr);
    };
    if (nearSize) {
        cacheBake(nearRtv[0].Get(), nearRtv[1].Get(), nearSize);
        ctx->GenerateMips(nearSrv[0].Get());
        if (two) ctx->GenerateMips(nearSrv[1].Get());
    }
    cacheBake(p.cache[0].rtv[0].Get(), p.cache[1].rtv[0].Get(), in.sizeClass);
    ctx->GenerateMips(p.cache[0].srv.Get());
    if (two) ctx->GenerateMips(p.cache[1].srv.Get());

    // 5. DXT Compress, then the copies into the BC1 caches.
    ComPtr<ID3D11Texture2D> outTex[2];
    for (int k = 0; k < (two ? 2 : 1); ++k) {
        for (uint32_t m = 0; m < mips; ++m) {
            common(static_cast<float>((in.sizeClass / 4) >> m));
            ctx->OMSetRenderTargets(1, p.blocks[k].rtv[m].GetAddressOf(), nullptr);
            ctx->OMSetBlendState(p.blendOff.Get(), zero4, 0xffffffff);
            const float mipArg[4] = {static_cast<float>(m), 0, 0, 0};
            C5R_TRY(upload(ctx, p.compressCb.Get(), mipArg, sizeof mipArg));
            ID3D11ShaderResourceView* t[1] = {p.cache[k].srv.Get()};
            ID3D11SamplerState* s[1] = {p.sampA.Get()};
            ctx->PSSetShaderResources(0, 1, t);
            ctx->PSSetSamplers(0, 1, s);
            ctx->VSSetConstantBuffers(0, 1, p.compressCb.GetAddressOf());
            ctx->PSSetConstantBuffers(0, 1, p.compressCb.GetAddressOf());
            ctx->IASetIndexBuffer(p.quadIb.Get(), DXGI_FORMAT_R32_UINT, 0);
            ctx->IASetVertexBuffers(0, 1, p.quadVb.GetAddressOf(), &quadStride, &off);
            ctx->IASetInputLayout(p.compressLayout.Get());
            ctx->VSSetShader(p.compressVs.Get(), nullptr, 0);
            ctx->PSSetShader(p.compressPs.Get(), nullptr, 0);
            ctx->DrawIndexed(6, 0, 0);
        }
        ctx->OMSetRenderTargets(8, noRt, nullptr);
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = d.Height = in.sizeClass;
        d.MipLevels = mips;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_BC1_TYPELESS;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        C5R_TRY(hrOk(dev->CreateTexture2D(&d, nullptr, &outTex[k]), "CreateTexture2D (BC1 cache)"));
        ctx->CopyResource(outTex[k].Get(), p.blocks[k].tex.Get());
    }
    ctx->PSSetShaderResources(0, 16, none);
    out.t0 = outTex[0].Detach();
    out.t8 = two ? outTex[1].Detach() : nullptr;
    return Status::Ok();
}

}} // namespace c5r::bake
