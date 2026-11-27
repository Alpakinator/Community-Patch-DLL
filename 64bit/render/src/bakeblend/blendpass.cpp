// c5r::bake blend, GPU side: the blend draw of the terrain bake with the EXE's TerrainShaderScreen pass-2 shaders, the
// states and targets as captured (research/05-graphics-composition/terrain-bake-blend.md 2.5 and 4), and the layer
// textures by name through c5r::gamefs (issue 3402).
#include "c5r/bake/blend.h"

#include <d3d11.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include "c5r/gamefs/dds.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/shaders/reflect.h"
#include "c5r/shaders/shadersection.h"

namespace c5r {
namespace bake {

namespace {

Status hr(HRESULT h, const char* what) {
    if (SUCCEEDED(h)) return Status::Ok();
    char b[32];
    std::snprintf(b, sizeof b, " (hr 0x%08lx)", static_cast<unsigned long>(h));
    return Status::fail(Err::Device, std::string(what) + b);
}

template <class T>
void rel(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x", v);
    return b;
}

// What the blend shaders must look like (reflection of the EXE's own blobs, research file 2.5).
Status checkVs(const shaders::ShaderReflection& r) {
    struct In { const char* sem; uint32_t idx; uint32_t type; };
    const In want[7] = {{"POSITION", 0, 3}, {"TEXCOORD", 0, 3}, {"TEXCOORD", 1, 3}, {"TEXCOORD", 2, 3},
                        {"TEXCOORD", 3, 3}, {"BLENDINDICES", 0, 1}, {"BLENDINDICES", 1, 1}};
    if (r.input.elems.size() != 7) return Status::fail(Err::BadLayout, "blend VS: " + std::to_string(r.input.elems.size()) + " inputs, want 7");
    for (int i = 0; i < 7; ++i) {
        const shaders::SigElement& e = r.input.elems[i];
        if (e.semantic != want[i].sem || e.semanticIndex != want[i].idx || e.componentType != want[i].type)
            return Status::fail(Err::BadLayout, "blend VS: input " + std::to_string(i) + " is " + e.semantic +
                                                    std::to_string(e.semanticIndex));
    }
    const shaders::CbDecl* c0 = r.findCb(0);
    const shaders::CbDecl* c2 = r.findCb(2);
    if (!c0 || c0->vec4Count < 1 || !c2 || c2->vec4Count != kTerrainLayersFloat4)
        return Status::fail(Err::BadLayout, "blend VS: constant buffers differ (want cb0 >= 1, cb2 = 96 registers)");
    return Status::Ok();
}

Status checkPs(const shaders::ShaderReflection& r) {
    if (!r.cbuffers.empty()) return Status::fail(Err::BadLayout, "blend PS: declares a constant buffer");
    if (r.samplers.size() != 3) return Status::fail(Err::BadLayout, "blend PS: " + std::to_string(r.samplers.size()) + " samplers, want 3");
    uint32_t tex = 0;
    for (const shaders::ResourceDecl& d : r.resources)
        if (d.dimension == 3 && d.slot < 24) ++tex;
    if (tex != 24 || r.resources.size() != 24) return Status::fail(Err::BadLayout, "blend PS: textures differ (want t0..t23, 2D)");
    if (r.output.elems.size() != 3) return Status::fail(Err::BadLayout, "blend PS: " + std::to_string(r.output.elems.size()) + " outputs, want 3");
    return Status::Ok();
}

}  // namespace

struct BlendPass::Impl {
    ID3D11Device* dev = nullptr;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* il = nullptr;
    ID3D11BlendState* blend = nullptr;
    ID3D11DepthStencilState* depth = nullptr;
    ID3D11RasterizerState* raster = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11Texture2D* rt[3] = {};
    ID3D11RenderTargetView* rtv[3] = {};
    ID3D11ShaderResourceView* rtSrv[3] = {};
    ID3D11Texture2D* dummy = nullptr;
    ID3D11ShaderResourceView* dummySrv = nullptr;
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    ID3D11Buffer* cb0 = nullptr;
    ID3D11Buffer* cb1 = nullptr;
    ID3D11Buffer* cb2 = nullptr;
    ~Impl() {
        rel(vs); rel(ps); rel(il); rel(blend); rel(depth); rel(raster); rel(sampler);
        for (int i = 0; i < 3; ++i) { rel(rt[i]); rel(rtv[i]); rel(rtSrv[i]); }
        rel(dummy); rel(dummySrv); rel(vb); rel(ib); rel(cb0); rel(cb1); rel(cb2); rel(dev);
    }
};

BlendPass::BlendPass() {}
BlendPass::~BlendPass() { delete p_; }

ID3D11Texture2D* BlendPass::target(int i) const { return p_ && i >= 0 && i < 3 ? p_->rt[i] : nullptr; }
ID3D11ShaderResourceView* BlendPass::targetSrv(int i) const { return p_ && i >= 0 && i < 3 ? p_->rtSrv[i] : nullptr; }
ID3D11ShaderResourceView* BlendPass::dummySrv() const { return p_ ? p_->dummySrv : nullptr; }

Status BlendPass::init(ID3D11Device* dev, const shaders::ShaderSection& exe) { return init(dev, exe, kBlendVsVa, kBlendPsVa); }

Status BlendPass::init(ID3D11Device* dev, const shaders::ShaderSection& exe, uint32_t vsVa, uint32_t psVa) {
    delete p_;
    p_ = nullptr;
    if (!dev) return Status::fail(Err::BadLayout, "blend pass: no device");
    const shaders::ShaderBlob* bv = exe.findByVa(vsVa);
    const shaders::ShaderBlob* bp = exe.findByVa(psVa);
    if (!bv) return Status::fail(Err::NotFound, "blend VS " + hex(vsVa) + " not in the EXE's .shader section");
    if (!bp) return Status::fail(Err::NotFound, "blend PS " + hex(psVa) + " not in the EXE's .shader section");
    if (bv->stage != shaders::Stage::Vertex) return Status::fail(Err::BadLayout, "blend VS " + hex(vsVa) + " is not a vertex shader");
    if (bp->stage != shaders::Stage::Pixel) return Status::fail(Err::BadLayout, "blend PS " + hex(psVa) + " is not a pixel shader");
    shaders::ShaderReflection rv, rp;
    C5R_TRY(shaders::reflect(bv->bytes, bv->size, rv));
    C5R_TRY(shaders::reflect(bp->bytes, bp->size, rp));
    C5R_TRY(checkVs(rv));
    C5R_TRY(checkPs(rp));

    std::unique_ptr<Impl> p(new Impl());
    p->dev = dev;
    dev->AddRef();
    C5R_TRY(hr(dev->CreateVertexShader(bv->bytes, bv->size, nullptr, &p->vs), "CreateVertexShader (EXE blend VS)"));
    C5R_TRY(hr(dev->CreatePixelShader(bp->bytes, bp->size, nullptr, &p->ps), "CreatePixelShader (EXE blend PS)"));
    const D3D11_INPUT_ELEMENT_DESC el[7] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 2, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 3, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 1, DXGI_FORMAT_R8G8B8A8_UINT, 0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    C5R_TRY(hr(dev->CreateInputLayout(el, 7, bv->bytes, bv->size, &p->il), "CreateInputLayout (blend)"));

    D3D11_BLEND_DESC bd = {};
    bd.AlphaToCoverageEnable = FALSE;
    bd.IndependentBlendEnable = FALSE;  // RenderTarget[0] applies to all three targets
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    C5R_TRY(hr(dev->CreateBlendState(&bd, &p->blend), "CreateBlendState"));
    D3D11_DEPTH_STENCIL_DESC dd = {};
    dd.DepthEnable = FALSE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    dd.FrontFace.StencilFailOp = dd.FrontFace.StencilDepthFailOp = dd.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    dd.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    dd.BackFace = dd.FrontFace;
    C5R_TRY(hr(dev->CreateDepthStencilState(&dd, &p->depth), "CreateDepthStencilState"));
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.FrontCounterClockwise = TRUE;
    rd.DepthClipEnable = TRUE;
    C5R_TRY(hr(dev->CreateRasterizerState(&rd, &p->raster), "CreateRasterizerState"));
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxAnisotropy = 0;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MinLOD = 0;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    C5R_TRY(hr(dev->CreateSamplerState(&sd, &p->sampler), "CreateSamplerState"));

    for (int i = 0; i < 3; ++i) {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = td.Height = kBlendTargetSize;
        td.MipLevels = 10;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        C5R_TRY(hr(dev->CreateTexture2D(&td, nullptr, &p->rt[i]), "CreateTexture2D (blend target)"));
        C5R_TRY(hr(dev->CreateRenderTargetView(p->rt[i], nullptr, &p->rtv[i]), "CreateRenderTargetView (blend target)"));
        C5R_TRY(hr(dev->CreateShaderResourceView(p->rt[i], nullptr, &p->rtSrv[i]), "CreateShaderResourceView (blend target)"));
    }
    {
        std::vector<uint8_t> zero(32 * 32 * 4, 0);
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = td.Height = 32;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sr = {zero.data(), 128, 0};
        C5R_TRY(hr(dev->CreateTexture2D(&td, &sr, &p->dummy), "CreateTexture2D (dummy)"));
        C5R_TRY(hr(dev->CreateShaderResourceView(p->dummy, nullptr, &p->dummySrv), "CreateShaderResourceView (dummy)"));
    }
    auto dyn = [&](UINT bytes, UINT bind, ID3D11Buffer** out, const char* what) -> Status {
        D3D11_BUFFER_DESC b = {};
        b.ByteWidth = bytes;
        b.Usage = D3D11_USAGE_DYNAMIC;
        b.BindFlags = bind;
        b.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        return hr(dev->CreateBuffer(&b, nullptr, out), what);
    };
    const UINT nv = (kBlendGrid) * (kBlendGrid);
    C5R_TRY(dyn(nv * sizeof(BlendVertex), D3D11_BIND_VERTEX_BUFFER, &p->vb, "CreateBuffer (blend VB)"));
    C5R_TRY(dyn((kBlendGrid - 1) * (kBlendGrid - 1) * 6 * 2, D3D11_BIND_INDEX_BUFFER, &p->ib, "CreateBuffer (blend IB)"));
    C5R_TRY(dyn(kOwnBlockBytes, D3D11_BIND_CONSTANT_BUFFER, &p->cb0, "CreateBuffer (cb0)"));
    C5R_TRY(dyn(0x250, D3D11_BIND_CONSTANT_BUFFER, &p->cb1, "CreateBuffer (cb1, TerrainFrame, not read)"));
    C5R_TRY(dyn(kTerrainLayersFloat4 * 16, D3D11_BIND_CONSTANT_BUFFER, &p->cb2, "CreateBuffer (cb2, TerrainLayers)"));
    p_ = p.release();
    return Status::Ok();
}

static Status upload(ID3D11DeviceContext* ctx, ID3D11Buffer* b, const void* data, size_t n) {
    D3D11_MAPPED_SUBRESOURCE m = {};
    C5R_TRY(hr(ctx->Map(b, 0, D3D11_MAP_WRITE_DISCARD, 0, &m), "Map"));
    std::memcpy(m.pData, data, n);
    ctx->Unmap(b, 0);
    return Status::Ok();
}

Status BlendPass::run(ID3D11DeviceContext* ctx, const std::vector<BlendVertex>& vertices,
                      const std::vector<uint16_t>& indices, const std::vector<uint8_t>& ownBlock,
                      const std::vector<float>& terrainLayers, const BlendTextures& tex) {
    if (!p_ || !ctx) return Status::fail(Err::BadLayout, "blend pass: not initialised");
    if (vertices.empty() || vertices.size() > size_t(kBlendGrid) * kBlendGrid)
        return Status::fail(Err::BadLayout, "blend pass: " + std::to_string(vertices.size()) + " vertices");
    if (indices.empty() || indices.size() % 3 || indices.size() > size_t(kBlendGrid - 1) * (kBlendGrid - 1) * 6)
        return Status::fail(Err::BadLayout, "blend pass: " + std::to_string(indices.size()) + " indices");
    for (uint16_t i : indices)
        if (i >= vertices.size()) return Status::fail(Err::BadLayout, "blend pass: index " + std::to_string(i) + " past the vertices");
    if (ownBlock.size() != kOwnBlockBytes) return Status::fail(Err::BadLayout, "blend pass: own block of " + std::to_string(ownBlock.size()) + " bytes");
    if (terrainLayers.size() != kTerrainLayersFloat4 * 4)
        return Status::fail(Err::BadLayout, "blend pass: TerrainLayers of " + std::to_string(terrainLayers.size()) + " floats");
    C5R_TRY(upload(ctx, p_->vb, vertices.data(), vertices.size() * sizeof(BlendVertex)));
    C5R_TRY(upload(ctx, p_->ib, indices.data(), indices.size() * 2));
    C5R_TRY(upload(ctx, p_->cb0, ownBlock.data(), ownBlock.size()));
    C5R_TRY(upload(ctx, p_->cb2, terrainLayers.data(), terrainLayers.size() * 4));
    std::vector<uint8_t> zero(0x250, 0);
    C5R_TRY(upload(ctx, p_->cb1, zero.data(), zero.size()));

    ID3D11ShaderResourceView* none[24] = {};
    ctx->PSSetShaderResources(0, 24, none);
    ctx->OMSetRenderTargets(3, p_->rtv, nullptr);
    const float clear[4] = {0, 0, 0, 0};
    for (int i = 0; i < 3; ++i) ctx->ClearRenderTargetView(p_->rtv[i], clear);
    D3D11_VIEWPORT vp = {0, 0, float(kBlendTargetSize), float(kBlendTargetSize), 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->OMSetDepthStencilState(p_->depth, 0);
    ctx->RSSetState(p_->raster);
    const float bf[4] = {0, 0, 0, 0};
    ctx->OMSetBlendState(p_->blend, bf, 0xffffffffu);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11ShaderResourceView* srv[24];
    for (int i = 0; i < 24; ++i) srv[i] = tex.srv[i] ? tex.srv[i] : p_->dummySrv;
    ctx->PSSetShaderResources(0, 24, srv);
    ID3D11SamplerState* s[3] = {p_->sampler, p_->sampler, p_->sampler};
    ctx->PSSetSamplers(0, 3, s);
    ctx->IASetIndexBuffer(p_->ib, DXGI_FORMAT_R16_UINT, 0);
    ID3D11Buffer* cbs[3] = {p_->cb0, p_->cb1, p_->cb2};
    ctx->VSSetConstantBuffers(0, 3, cbs);
    ctx->PSSetConstantBuffers(0, 3, cbs);
    ctx->VSSetShader(p_->vs, nullptr, 0);
    ctx->PSSetShader(p_->ps, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->IASetInputLayout(p_->il);
    const UINT stride = sizeof(BlendVertex), off = 0;
    ctx->IASetVertexBuffers(0, 1, &p_->vb, &stride, &off);
    ctx->DrawIndexed(static_cast<UINT>(indices.size()), 0, 0);
    ID3D11RenderTargetView* nrt[3] = {};
    ctx->OMSetRenderTargets(3, nrt, nullptr);
    ctx->PSSetShaderResources(0, 24, none);
    for (int i = 0; i < 3; ++i) ctx->GenerateMips(p_->rtSrv[i]);
    return Status::Ok();
}

Status BlendPass::readTargets(ID3D11DeviceContext* ctx, BlendTargets& out) {
    if (!p_ || !ctx) return Status::fail(Err::BadLayout, "blend pass: not initialised");
    out.size = kBlendTargetSize;
    for (int i = 0; i < 3; ++i) {
        D3D11_TEXTURE2D_DESC td = {};
        p_->rt[i]->GetDesc(&td);
        td.MipLevels = 1;
        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.MiscFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D* st = nullptr;
        C5R_TRY(hr(p_->dev->CreateTexture2D(&td, nullptr, &st), "CreateTexture2D (staging)"));
        ctx->CopySubresourceRegion(st, 0, 0, 0, 0, p_->rt[i], 0, nullptr);
        D3D11_MAPPED_SUBRESOURCE m = {};
        HRESULT h = ctx->Map(st, 0, D3D11_MAP_READ, 0, &m);
        if (FAILED(h)) {
            st->Release();
            return hr(h, "Map (staging)");
        }
        out.texels[i].resize(size_t(kBlendTargetSize) * kBlendTargetSize * 4);
        for (uint32_t y = 0; y < kBlendTargetSize; ++y)
            std::memcpy(&out.texels[i][size_t(y) * kBlendTargetSize * 4], static_cast<const uint8_t*>(m.pData) + size_t(y) * m.RowPitch,
                        kBlendTargetSize * 8);
        ctx->Unmap(st, 0);
        st->Release();
    }
    return Status::Ok();
}

// ---------------------------------------------------------------- textures by name

struct BlendTextureCache::Impl {
    std::map<std::string, ID3D11ShaderResourceView*> srv;
    MissingTexture rule = MissingTexture::AsHandle0;
    std::vector<std::string> missing;
    std::vector<BlendTextureCache::Load> loads;  // issue 0417
    ~Impl() {
        for (auto& kv : srv)
            if (kv.second) kv.second->Release();
    }
};

BlendTextureCache::BlendTextureCache() : p_(new Impl()) {}
BlendTextureCache::~BlendTextureCache() { delete p_; }
size_t BlendTextureCache::loaded() const { return p_->srv.size(); }
void BlendTextureCache::setMissingRule(MissingTexture rule) { p_->rule = rule; }
const std::vector<std::string>& BlendTextureCache::missing() const { return p_->missing; }
const std::vector<BlendTextureCache::Load>& BlendTextureCache::loads() const { return p_->loads; }

namespace {
std::string lowerName(const std::string& s) {
    std::string k = s;
    for (char& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return k;
}
}  // namespace

// ticket 0698: textures made on the pipeline's loader thread
bool BlendTextureCache::has(const std::string& name) const {
    if (p_->srv.count(name)) return true;
    if (p_->rule != MissingTexture::AsHandle0) return false;
    const std::string key = lowerName(name);
    return std::find(p_->missing.begin(), p_->missing.end(), key) != p_->missing.end();
}

bool BlendTextureCache::provide(const std::string& name, ID3D11ShaderResourceView* srv) {
    if (!srv || p_->srv.count(name)) return false;
    srv->AddRef();
    p_->srv[name] = srv;
    return true;
}

bool BlendTextureCache::provideMissing(const std::string& name) {
    if (p_->rule != MissingTexture::AsHandle0) return false;
    const std::string key = lowerName(name);
    if (std::find(p_->missing.begin(), p_->missing.end(), key) == p_->missing.end()) p_->missing.push_back(key);
    return true;
}

Status BlendTextureCache::bind(ID3D11Device* dev, const gamefs::GameFiles& fs, const LayerTables& t,
                               const LayerList& list, ID3D11ShaderResourceView* dummy, BlendTextures& out) {
    out = BlendTextures();
    if (!dev) return Status::fail(Err::BadLayout, "texture cache: no device");
    if (list.count > kMaxLayers) return Status::fail(Err::BadLayout, "layer list: count " + std::to_string(list.count) + " > 8");
    auto get = [&](const std::string& name, ID3D11ShaderResourceView** srv) -> Status {
        auto it = p_->srv.find(name);
        if (it != p_->srv.end()) {
            *srv = it->second;
            return Status::Ok();
        }
        // issue 0417: each load timed and recorded (a slow bake names the files it read)
        using Clock = std::chrono::steady_clock;
        auto ms = [](Clock::time_point a, Clock::time_point b) {
            return std::chrono::duration<double, std::milli>(b - a).count();
        };
        const Clock::time_point r0 = Clock::now();
        if (!fs.exists(name) && p_->rule == MissingTexture::AsHandle0) {
            // The EXE's loader leaves handle 0 for a name it cannot find; the binder gives that slot no view, which
            // samples as 0 like the dummy (blend.h). Recorded once per name.
            std::string key = name;
            for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (std::find(p_->missing.begin(), p_->missing.end(), key) == p_->missing.end()) {
                p_->missing.push_back(key);
                Load l;
                l.name = name;
                l.readMs = ms(r0, Clock::now());
                l.missing = true;
                p_->loads.push_back(l);
            }
            *srv = dummy;
            return Status::Ok();
        }
        std::vector<uint8_t> bytes;
        Status st = fs.read(name, bytes);
        if (!st.ok()) return Status::fail(Err::NotFound, "texture " + name + " not in the install (" + st.str() + ")");
        const Clock::time_point r1 = Clock::now();
        gamefs::DdsImage img;
        C5R_TRY(gamefs::parseDds(bytes.data(), bytes.size(), img));
        ID3D11Resource* res = nullptr;
        ID3D11ShaderResourceView* v = nullptr;
        C5R_TRY(gamefs::createTexture(dev, img, &res, &v));
        res->Release();
        Load l;
        l.name = name;
        l.bytes = bytes.size();
        l.readMs = ms(r0, r1);
        l.createMs = ms(r1, Clock::now());
        p_->loads.push_back(l);
        p_->srv[name] = v;
        *srv = v;
        return Status::Ok();
    };
    for (uint32_t i = 0; i < kMaxLayers; ++i) {
        out.srv[i] = out.srv[8 + i] = out.srv[16 + i] = dummy;
        if (i >= list.count) continue;
        const uint32_t id = list.ids[i];
        if (id >= t.layerCount || id >= t.setOfLayer.size() || t.setOfLayer[id] >= t.sets.size())
            return Status::fail(Err::BadLayout, "layer list: id " + std::to_string(id) + " has no texture set");
        const TextureSet& s = t.sets[t.setOfLayer[id]];
        C5R_TRY(get(s.diffuse, &out.srv[i]));
        C5R_TRY(get(s.heightmap, &out.srv[8 + i]));
        if (!s.specmap.empty() && s.specmap != "NULL") C5R_TRY(get(s.specmap, &out.srv[16 + i]));
    }
    return Status::Ok();
}

}  // namespace bake
}  // namespace c5r
