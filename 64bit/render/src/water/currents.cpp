// render/src/water/currents.cpp - the "Water Currents" map with the EXE's WorldCurrents pass 3 (issue 3602).
// Behaviour and facts: c5r/water/currents.h and research/05-graphics-composition/water-system.md section 8.
// The CPU model below is written from that description, as the independent check of the GPU path.
#include "c5r/water/currents.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "c5r/shaders/shadersection.h"
#include "c5r/terrainsets/terrainsets.h"

namespace c5r { namespace water {

namespace {

template <class T> void rel(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

Status hr(HRESULT h, const char* what) {
    if (SUCCEEDED(h)) return Status::Ok();
    char b[64];
    std::snprintf(b, sizeof b, " failed (0x%08lx)", static_cast<unsigned long>(h));
    return Status::fail(h == E_OUTOFMEMORY ? Err::OutOfMemory : Err::Device, std::string("currents: ") + what + b);
}

bool finite(float v) { return std::isfinite(v); }

float frac(float v) { return v - std::floor(v); }

float sgn(float v) { return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f); }

// The GPU arithmetic the pass follows (issue 3602 experiment, 2026-11-14 on the workstation's GPU, section 8.6 of
// water-system.md): a division is a multiplication by the reciprocal, a `mad` is fused. The noise hash amplifies one
// ulp into a different sign, so the model reproduces both.
float dv(float a, float b) { return a * (1.0f / b); }
float md(float a, float b, float c) { return std::fma(a, b, c); }

// The lattice hash of the pass: cell (cx, cy) on an nx x ny lattice -> two signs (gradient x, gradient y).
void cornerSigns(float cx, float cy, float nx, float ny, float& gx, float& gy) {
    const float hy = frac(dv(17.0f * cy, ny));
    const float a = md(hy * hy, 3721.0f, cx);
    const float h0 = frac(dv(13.0f * a, nx));
    const float h = h0 * h0;
    gx = sgn(frac(h * 1860.5f) - 0.5f);
    gy = sgn(frac(h * 930.25f) - 0.5f);
}

// Bilinear, wrap, red channel of a w x h float image at uv (texel centres at (i + 0.5) / w).
float sampleWrap(const float* img, uint32_t w, uint32_t h, float u, float v) {
    const float x = u * static_cast<float>(w) - 0.5f, y = v * static_cast<float>(h) - 0.5f;
    const float fx = std::floor(x), fy = std::floor(y);
    const float tx = x - fx, ty = y - fy;
    auto at = [&](long i, long j) {
        const long W = static_cast<long>(w), H = static_cast<long>(h);
        i %= W;
        if (i < 0) i += W;
        j %= H;
        if (j < 0) j += H;
        return img[static_cast<size_t>(j) * w + static_cast<size_t>(i)];
    };
    const long i0 = static_cast<long>(fx), j0 = static_cast<long>(fy);
    const float a = at(i0, j0) * (1 - tx) + at(i0 + 1, j0) * tx;
    const float b = at(i0, j0 + 1) * (1 - tx) + at(i0 + 1, j0 + 1) * tx;
    return a * (1 - ty) + b * ty;
}

struct QuadVertex {
    float x, y, z, u, v;
};

} // namespace

Status currentsParamsFromGrid(const heightfield::HeightGrid& g, CurrentsParams& out) {
    out = CurrentsParams();
    if (g.mapW == 0 || g.mapH == 0) return Status::fail(Err::BadLayout, "currents: height grid without plots");
    out.sizeX = static_cast<float>(g.mapW);
    out.sizeY = static_cast<float>(g.mapH);
    return Status::Ok();
}

Status computeCurrentsFrame(const CurrentsParams& p, uint8_t out[kCurrentsFrameBytes]) {
    if (!out) return Status::fail(Err::BadLayout, "currents: out null");
    if (!finite(p.sizeX) || !finite(p.sizeY) || !finite(p.noiseAmplitude) || !finite(p.noiseCell) || !finite(p.gradientScale))
        return Status::fail(Err::BadLayout, "currents: a parameter is not finite");
    if (!(p.sizeX > 0) || !(p.sizeY > 0) || !(p.noiseCell > 0))
        return Status::fail(Err::BadLayout, "currents: size and noise cell must be > 0");
    if (std::floor(p.sizeX / p.noiseCell) < 1.0f || std::floor(p.sizeY / p.noiseCell) < 1.0f)
        return Status::fail(Err::BadLayout, "currents: the noise lattice has no cell (size / noise cell < 1)");
    float f[kCurrentsFrameBytes / 4] = {0};
    f[0] = p.sizeX;
    f[1] = p.sizeY;
    f[4] = p.noiseAmplitude;
    f[8] = p.noiseCell;
    f[12] = p.gradientScale;
    std::memcpy(out, f, kCurrentsFrameBytes);
    return Status::Ok();
}

Status currentsModel(const CurrentsParams& p, const float* source, uint32_t srcW, uint32_t srcH, std::vector<float>& out) {
    out.clear();
    uint8_t cb[kCurrentsFrameBytes];
    C5R_TRY(computeCurrentsFrame(p, cb));
    if (source && (srcW == 0 || srcH == 0)) return Status::fail(Err::BadLayout, "currents: source of size 0");
    if (source)
        for (size_t i = 0; i < static_cast<size_t>(srcW) * srcH; ++i)
            if (!finite(source[i])) return Status::fail(Err::BadLayout, "currents: source texel " + std::to_string(i) + " not finite");
    const uint32_t N = kCurrentsMapSize;
    out.assign(static_cast<size_t>(N) * N * 4, 0.0f);
    const float nx = std::floor(p.sizeX / p.noiseCell), ny = std::floor(p.sizeY / p.noiseCell);
    const float tx = 1.0f / p.sizeX, ty = 1.0f / p.sizeY;
    for (uint32_t y = 0; y < N; ++y) {
        for (uint32_t x = 0; x < N; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(N);
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(N);
            // gradient noise: cell (cx, cy), local (fu, fv); corner signs; dot products; smoothstep blend of the
            // value and its derivative terms as the pass combines them.
            const float pu = nx * u, pv = ny * v;
            const float fu = frac(pu), fv = frac(pv);
            const float cx = md(u, nx, -fu), cy = md(v, ny, -fv);
            float sx00, sy00, sx01, sy01, sx10, sy10, sx11, sy11;   // (x corner, y corner)
            cornerSigns(cx, cy, nx, ny, sx00, sy00);
            cornerSigns(cx, cy + 1, nx, ny, sx01, sy01);
            cornerSigns(cx + 1, cy, nx, ny, sx10, sy10);
            cornerSigns(cx + 1, cy + 1, nx, ny, sx11, sy11);
            const float d00 = md(fu, sx00, sy00 * fv);
            const float d01 = md(fu, sx01, sy01 * (fv - 1));
            const float d10 = md(fu - 1, sx10, sy10 * fv);
            const float d11 = md(fu - 1, sx11, sy11 * (fv - 1));
            const float su = md(-fu, 2.0f, 3.0f) * (fu * fu), sv = md(-fv, 2.0f, 3.0f) * (fv * fv);
            const float du = fu * md(fu, -6.0f, 6.0f), dv2 = fv * md(fv, -6.0f, 6.0f);
            const float X = md(d10 - d00, du, md(su, sx10 - sx00, sx00));
            const float Y = md(d11 - d01, du, md(su, sx11 - sx01, sx01));
            const float Z = md(d01 - d00, dv2, md(sv, sy01 - sy00, sy00));
            const float W = md(d11 - d10, dv2, md(sv, sy11 - sy10, sy10));
            const float nX = md(su, W - Z, Z);
            const float nY = md(sv, Y - X, X);
            const float aX = nX * p.noiseAmplitude * 0.5f, aY = nY * p.noiseAmplitude * 0.5f;
            const float noise[4] = {md(aX, -1.0f, 0.5f), md(aY, 1.0f, 0.5f), 0.0f, 1.0f};
            // Sobel of the source's red channel, step 1 / size
            float gx = 0, gy = 0;
            if (source) {
                auto h = [&](float du2, float dv2) { return sampleWrap(source, srcW, srcH, u + du2 * tx, v + dv2 * ty); };
                gx = (h(-1, -1) + 2 * h(-1, 0) + h(-1, 1)) - (h(1, -1) + 2 * h(1, 0) + h(1, 1));
                gy = (h(-1, -1) + 2 * h(0, -1) + h(1, -1)) - (h(-1, 1) + 2 * h(0, 1) + h(1, 1));
            }
            gx *= p.gradientScale;
            gy *= p.gradientScale;
            const float len = std::sqrt(md(gx, gx, gy * gy));
            const float flow[4] = {md(gy * 0.5f, -1.0f, 0.5f), md(gx * 0.5f, 1.0f, 0.5f), 0.0f, len};
            const float t = len < 1.0f ? len : 1.0f;
            float* o = &out[(static_cast<size_t>(y) * N + x) * 4];
            for (int c = 0; c < 4; ++c) o[c] = md(t, flow[c] - noise[c], noise[c]);
        }
    }
    return Status::Ok();
}

void quantiseUnorm8(const std::vector<float>& in, std::vector<uint8_t>& out) {
    out.resize(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        float v = in[i];
        if (!(v > 0.0f)) v = 0.0f;   // NaN -> 0, as D3D's float -> UNORM conversion
        if (v > 1.0f) v = 1.0f;
        out[i] = static_cast<uint8_t>(std::ceil(v * 255.0f - 0.5f));   // nearest, a tie rounds down (as the GPU, 8.6)
    }
}

struct Currents::Impl {
    ID3D11Device* dev = nullptr;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* layout = nullptr;
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* cb = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11Texture2D* tex = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    uint64_t generation = 0;
    ~Impl() {
        rel(srv); rel(rtv); rel(tex); rel(sampler); rel(cb); rel(vb); rel(layout); rel(ps); rel(vs); rel(dev);
    }
};

Currents::Currents() {}
Currents::~Currents() { release(); }

void Currents::release() {
    delete p_;
    p_ = nullptr;
}

Status Currents::init(ID3D11Device* dev, const shaders::ShaderSection& sec) {
    release();
    if (!dev) return Status::fail(Err::BadLayout, "currents: no device");
    const terrainsets::ShaderSetDesc* set = terrainsets::findSet("WorldCurrents");
    if (!set) return Status::fail(Err::NotFound, "currents: terrainsets has no WorldCurrents");
    C5R_TRY(terrainsets::validateSet(*set, sec));
    const terrainsets::PassDesc* pd = nullptr;
    for (const terrainsets::PassDesc& d : set->passes)
        if (d.index == kCurrentsPass) pd = &d;
    if (!pd || !pd->sm50[0] || !pd->sm50[1]) return Status::fail(Err::NotFound, "currents: WorldCurrents pass 3 has no SM5 VS/PS");
    const shaders::ShaderBlob* bv = sec.findByVa(pd->sm50[0]);
    const shaders::ShaderBlob* bp = sec.findByVa(pd->sm50[1]);
    if (!bv || !bp) return Status::fail(Err::NotFound, "currents: a WorldCurrents pass-3 blob is not in the .shader section");
    if (bv->stage != shaders::Stage::Vertex || bp->stage != shaders::Stage::Pixel)
        return Status::fail(Err::BadLayout, "currents: a WorldCurrents pass-3 blob has another stage");
    std::unique_ptr<Impl> p(new Impl());
    p->dev = dev;
    dev->AddRef();
    C5R_TRY(hr(dev->CreateVertexShader(bv->bytes, bv->size, nullptr, &p->vs), "CreateVertexShader (EXE)"));
    C5R_TRY(hr(dev->CreatePixelShader(bp->bytes, bp->size, nullptr, &p->ps), "CreatePixelShader (EXE)"));
    // the VS's input signature: POSITION0.xyz, TEXCOORD0.xy
    const D3D11_INPUT_ELEMENT_DESC il[2] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    C5R_TRY(hr(dev->CreateInputLayout(il, 2, bv->bytes, bv->size, &p->layout), "CreateInputLayout"));
    // full-screen quad: clip (-1, 1) at uv (0, 0), clip (1, -1) at uv (1, 1) [I: the EXE's quad orientation]
    const QuadVertex q[4] = {{-1, 1, 0, 0, 0}, {1, 1, 0, 1, 0}, {-1, -1, 0, 0, 1}, {1, -1, 0, 1, 1}};
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof q;
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sd = {q, 0, 0};
    C5R_TRY(hr(dev->CreateBuffer(&bd, &sd, &p->vb), "CreateBuffer (quad)"));
    uint32_t cbSize = 0;
    for (const terrainsets::CbBinding& b : set->cbuffers)
        if (b.slot == 1) cbSize = b.layout->sizeBytes;
    if (cbSize != kCurrentsFrameBytes)
        return Status::fail(Err::BadLayout, "currents: WorldCurrents slot 1 is not the 0x40-byte CurrentsFrame");
    D3D11_BUFFER_DESC cd = {};
    cd.ByteWidth = cbSize;
    cd.Usage = D3D11_USAGE_DEFAULT;
    cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    C5R_TRY(hr(dev->CreateBuffer(&cd, nullptr, &p->cb), "CreateBuffer (CurrentsFrame)"));
    const terrainsets::SamplerStateDesc* s0 = terrainsets::samplerState(0);
    if (!s0) return Status::fail(Err::NotFound, "currents: sampler state 0 not described");
    D3D11_SAMPLER_DESC sm = {};
    sm.Filter = static_cast<D3D11_FILTER>(s0->filter);
    sm.AddressU = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(s0->address[0]);
    sm.AddressV = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(s0->address[1]);
    sm.AddressW = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(s0->address[2]);
    sm.MaxAnisotropy = s0->maxAnisotropy;
    sm.ComparisonFunc = static_cast<D3D11_COMPARISON_FUNC>(s0->comparison);
    sm.MinLOD = 0;
    sm.MaxLOD = D3D11_FLOAT32_MAX;
    C5R_TRY(hr(dev->CreateSamplerState(&sm, &p->sampler), "CreateSamplerState"));
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = td.Height = kCurrentsMapSize;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    C5R_TRY(hr(dev->CreateTexture2D(&td, nullptr, &p->tex), "CreateTexture2D (Water Currents)"));
    D3D11_RENDER_TARGET_VIEW_DESC rd = {};
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    C5R_TRY(hr(dev->CreateRenderTargetView(p->tex, &rd, &p->rtv), "CreateRenderTargetView"));
    D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
    vd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    vd.Texture2D.MipLevels = 1;
    C5R_TRY(hr(dev->CreateShaderResourceView(p->tex, &vd, &p->srv), "CreateShaderResourceView"));
    p_ = p.release();
    return Status::Ok();
}

Status Currents::compute(ID3D11DeviceContext* ctx, const CurrentsParams& prm, ID3D11ShaderResourceView* source) {
    if (!p_) return Status::fail(Err::NotFound, "currents: init not done");
    if (!ctx) return Status::fail(Err::BadLayout, "currents: no context");
    uint8_t block[kCurrentsFrameBytes];
    C5R_TRY(computeCurrentsFrame(prm, block));
    ctx->UpdateSubresource(p_->cb, 0, nullptr, block, 0, 0);
    ID3D11ShaderResourceView* nullSrv = nullptr;
    ctx->PSSetShaderResources(5, 1, &nullSrv);   // the map may be bound at t5 of a water pass
    ctx->OMSetRenderTargets(1, &p_->rtv, nullptr);
    D3D11_VIEWPORT vp = {0, 0, static_cast<float>(kCurrentsMapSize), static_cast<float>(kCurrentsMapSize), 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(nullptr);
    ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    ctx->OMSetDepthStencilState(nullptr, 0);
    const UINT stride = sizeof(QuadVertex), offset = 0;
    ctx->IASetInputLayout(p_->layout);
    ctx->IASetVertexBuffers(0, 1, &p_->vb, &stride, &offset);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ctx->VSSetShader(p_->vs, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->PSSetShader(p_->ps, nullptr, 0);
    ctx->VSSetConstantBuffers(1, 1, &p_->cb);
    ctx->PSSetConstantBuffers(1, 1, &p_->cb);
    ctx->PSSetShaderResources(0, 1, &source);
    ctx->PSSetSamplers(0, 1, &p_->sampler);
    ctx->Draw(4, 0);
    ID3D11RenderTargetView* nullRtv = nullptr;
    ctx->OMSetRenderTargets(1, &nullRtv, nullptr);
    ctx->PSSetShaderResources(0, 1, &nullSrv);
    ++p_->generation;
    return Status::Ok();
}

ID3D11ShaderResourceView* Currents::srv() const { return p_ ? p_->srv : nullptr; }
ID3D11Texture2D* Currents::texture() const { return p_ ? p_->tex : nullptr; }
uint64_t Currents::generation() const { return p_ ? p_->generation : 0; }

Status Currents::readback(ID3D11DeviceContext* ctx, std::vector<uint8_t>& rgba8) {
    rgba8.clear();
    if (!p_ || p_->generation == 0) return Status::fail(Err::NotFound, "currents: nothing computed");
    if (!ctx) return Status::fail(Err::BadLayout, "currents: no context");
    D3D11_TEXTURE2D_DESC td = {};
    p_->tex->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* st = nullptr;
    C5R_TRY(hr(p_->dev->CreateTexture2D(&td, nullptr, &st), "CreateTexture2D (staging)"));
    ctx->CopyResource(st, p_->tex);
    D3D11_MAPPED_SUBRESOURCE m = {};
    Status s = hr(ctx->Map(st, 0, D3D11_MAP_READ, 0, &m), "Map (staging)");
    if (s.ok()) {
        rgba8.resize(static_cast<size_t>(kCurrentsMapSize) * kCurrentsMapSize * 4);
        for (uint32_t y = 0; y < kCurrentsMapSize; ++y)
            std::memcpy(&rgba8[static_cast<size_t>(y) * kCurrentsMapSize * 4], static_cast<const uint8_t*>(m.pData) + y * m.RowPitch,
                        kCurrentsMapSize * 4);
        ctx->Unmap(st, 0);
    }
    rel(st);
    return s;
}

}} // namespace c5r::water
