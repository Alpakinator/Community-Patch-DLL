// resample.cpp - c5r::fow::OverlayResampler: the EXE's Terrain Overlay target of frame N-1 moved into the window the
// server made from camera N (ticket 0674, epic 0673; research/05-graphics-composition/pan-coherence.md 3), and since ticket
// 0793 the overlay's ground retained (research/05-graphics-composition/overlay-history.md). Own code and own programs (an
// HLSL string compiled at the first call, as c5r::terrain's own programs): nothing of the EXE.
//
// The move: one full-screen triangle into mip 0 of a texture of the source's size and mip count: each texel at uv takes
// the source at uv' = a uv + b (uvMapBetweenWindows), trilinear over the source's mips (the level from the derivatives of
// uv', i.e. log2 of the map's scale: a zoom-out step reads a smaller mip); where uv' lies outside [0, 1] (the plots that
// came into the window: the leading edge) the history's texel of the same world point where the history holds one, else
// (0, 0, 0, 0); then GenerateMips, as the EXE's own target has its mips made (overlay-system.md) and as the server builds
// the mask's.
//
// The history (ticket 0793, the retained-world proposal's R1-R3): a texture of the whole map's ground in world space,
// u = (x - x0) / W (one wrap period on a wrapping map, the sampler wraps u), v = (y1 - y) / H, filled by remember() with
// every received overlay at its window (one quad per wrap copy the window reaches), with an R8 "held" mask written 1 where
// an overlay was drawn. Texels never drawn stay (0, 0, 0, 0) and held 0, so the move reads history / held: the mean of
// the held texels under the bilinear footprint. A CPU grid of 32-unit cells mirrors "held" for the counters (which edge
// ground the history held). Removal only by a newer overlay over the same ground or a new map.
#include <c5r/fow/fow.h>

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

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

// One constant buffer for both programs: p0, p1, p2 (meaning per pass, below).
const char kHlsl[] = R"(
Texture2D src : register(t0);
Texture2D hist : register(t1);
Texture2D held : register(t2);
SamplerState smp : register(s0);
SamplerState smpH : register(s1);
cbuffer Map : register(b0) { float4 p0; float4 p1; float4 p2; };
struct VO { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
// the move: p0 = (a.x, a.y, b.x, b.y) target uv -> source uv; p1 = (c.x, c.y, d.x, d.y) target uv -> history uv;
// p2.x = 1 with a history, p2.y = 1 when the history wraps in u
VO vs(uint id : SV_VertexID)
{
    VO o;
    const float2 t = float2((id << 1) & 2, id & 2);
    o.pos = float4(t * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.uv = t;
    return o;
}
float4 ps(VO i) : SV_Target
{
    const float2 s = p0.xy * i.uv + p0.zw;
    const float4 c = src.Sample(smp, s);   // outside the branch: the level comes from the derivatives of s
    const float inside = (s.x >= 0.0 && s.x <= 1.0 && s.y >= 0.0 && s.y <= 1.0) ? 1.0 : 0.0;
    const float2 h = p1.xy * i.uv + p1.zw;
    const float4 hc = hist.SampleLevel(smpH, h, 0.0);
    const float hm = held.SampleLevel(smpH, h, 0.0).r;
    const float hin = (h.y >= 0.0 && h.y <= 1.0 && (p2.y > 0.5 || (h.x >= 0.0 && h.x <= 1.0))) ? 1.0 : 0.0;
    const float use = p2.x * hin * (hm > 0.25 ? 1.0 : 0.0);
    const float4 kept = use > 0.5 ? saturate(hc / max(hm, 0.25)) : float4(0.0, 0.0, 0.0, 0.0);
    return inside > 0.5 ? c : kept;
}
// the history's write: p0 = the quad's rectangle in history uv (u0, v0, u1, v1); p1 = (A.x, A.y, B.x, B.y) history uv ->
// source uv
VO vsSplat(uint id : SV_VertexID)
{
    VO o;
    const float2 t = float2(id & 1, id >> 1);
    const float2 uv = lerp(p0.xy, p0.zw, t);
    o.pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    o.uv = uv;
    return o;
}
struct SO { float4 c : SV_Target0; float h : SV_Target1; };
SO psSplat(VO i)
{
    const float2 s = p1.xy * i.uv + p1.zw;
    const float4 c = src.Sample(smp, s);
    if (s.x < 0.0 || s.x > 1.0 || s.y < 0.0 || s.y > 1.0) discard;
    SO o;
    o.c = c;
    o.h = 1.0;
    return o;
}
)";

// The programs' bytecode, compiled once per process (ticket 0793: a test or an offline tool that makes many devices or
// scenes paid D3DCompile at each one). The caller releases *out.
Status compile(const char* entry, const char* target, ID3DBlob** out)
{
    static std::mutex mu;
    static std::map<std::string, ID3DBlob*> cache;   // kept for the process's life
    std::lock_guard<std::mutex> lock(mu);
    auto it = cache.find(entry);
    if (it != cache.end()) {
        it->second->AddRef();
        *out = it->second;
        return Status::Ok();
    }
    ID3DBlob* err = nullptr;
    const HRESULT h = D3DCompile(kHlsl, sizeof(kHlsl) - 1, "c5r_fow_overlay_resample.hlsl", nullptr, nullptr, entry, target,
                                 D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &err);
    std::string msg = err ? std::string(static_cast<const char*>(err->GetBufferPointer()), err->GetBufferSize()) : "";
    rel(err);
    if (FAILED(h)) return Status::fail(Err::Device, std::string("overlay resample: D3DCompile ") + entry + ": " + msg);
    (*out)->AddRef();
    cache[entry] = *out;
    return Status::Ok();
}

// The source view's checks (both passes): a Texture2D R8G8B8A8_UNORM view; its size at the most detailed mip and its mips.
Status sourceOf(ID3D11ShaderResourceView* src, UINT& w, UINT& h, UINT& mips)
{
    D3D11_SHADER_RESOURCE_VIEW_DESC sv;
    src->GetDesc(&sv);
    if (sv.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || sv.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
        return Status::fail(Err::BadLayout, "overlay resample: the source is not a Texture2D R8G8B8A8_UNORM view (dimension " +
                                                std::to_string(static_cast<int>(sv.ViewDimension)) + ", format " +
                                                std::to_string(static_cast<int>(sv.Format)) + ")");
    ID3D11Resource* res = nullptr;
    src->GetResource(&res);
    ID3D11Texture2D* st = nullptr;
    if (res) res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&st));
    rel(res);
    if (!st) return Status::fail(Err::BadLayout, "overlay resample: the source's resource is not a Texture2D");
    D3D11_TEXTURE2D_DESC sd;
    st->GetDesc(&sd);
    st->Release();
    // the view's mips (the reader's view covers every mip of the slot texture)
    mips = sv.Texture2D.MipLevels == static_cast<UINT>(-1) ? sd.MipLevels - sv.Texture2D.MostDetailedMip : sv.Texture2D.MipLevels;
    w = sd.Width >> sv.Texture2D.MostDetailedMip;
    h = sd.Height >> sv.Texture2D.MostDetailedMip;
    if (w == 0 || h == 0 || mips == 0) return Status::fail(Err::BadLayout, "overlay resample: the source view is empty");
    return Status::Ok();
}

// A window matrix's world box (overlay-system.md 4: x = (NDC - tx) / sx for NDC -1 and 1).
bool windowBox(const float m[16], double& x0, double& x1, double& y0, double& y1)
{
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(m[i])) return false;
    if (m[0] == 0.0f || m[5] == 0.0f || m[1] != 0.0f || m[4] != 0.0f) return false;
    const double sx = m[0], tx = m[12], sy = m[5], ty = m[13];
    x0 = (-1.0 - tx) / sx;
    x1 = (1.0 - tx) / sx;
    y0 = (-1.0 - ty) / sy;
    y1 = (1.0 - ty) / sy;
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    return true;
}

constexpr double kCell = 32.0;                 // the CPU mirror's cell (world units)
constexpr double kMargin = 128.0;              // the history's margin past the world box on each open side
constexpr double kMaxTexels = 16.0 * 1048576;  // 16 M texels: 64 MB colour + 16 MB held
constexpr double kMaxSide = 8192.0;

} // namespace

struct OverlayResampler::Impl {
    ID3D11Device* dev = nullptr;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11VertexShader* vsSplat = nullptr;
    ID3D11PixelShader* psSplat = nullptr;
    ID3D11Buffer* cb = nullptr;
    ID3D11SamplerState* smp = nullptr;
    ID3D11SamplerState* smpWrap = nullptr;    // the history on a wrapping map: u wraps, v clamps
    ID3D11SamplerState* smpClamp = nullptr;   // the history on a map that does not wrap
    ID3D11RasterizerState* raster = nullptr;
    ID3D11BlendState* blend = nullptr;
    ID3D11DepthStencilState* depth = nullptr;
    ID3D11Texture2D* tex = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    UINT width = 0, height = 0, mips = 0;
    bool valid = false;

    // ---- the history (ticket 0793) ----
    ID3D11Texture2D* hTex = nullptr;
    ID3D11RenderTargetView* hRtv = nullptr;
    ID3D11ShaderResourceView* hSrv = nullptr;
    ID3D11Texture2D* mTex = nullptr;   // held
    ID3D11RenderTargetView* mRtv = nullptr;
    ID3D11ShaderResourceView* mSrv = nullptr;
    UINT hw = 0, hh = 0;               // the history's texels
    bool hHave = false;                // the history's textures exist and are keyed to hWorld
    OverlayWorld hWorld;               // the world as given
    double hx0 = 0, hy1 = 0, hW = 0, hH = 0;   // the history's box: u = (x - hx0) / hW, v = (hy1 - y) / hH
    bool hWrap = false;
    uint32_t cx = 0, cy = 0;           // the CPU mirror's cells
    std::vector<uint8_t> cells;
    bool anyHeld = false;
    bool forgotten = false;            // forget() was called: the textures are cleared at the next history()
    OverlayWorld want;                 // the map of the newest remember() call (the history is keyed to it at its next draw)
    bool haveWant = false;
    static bool sameWorld(const OverlayWorld& a, const OverlayWorld& b)
    {
        return a.key == b.key && (a.wrapPeriod > 0.0) == (b.wrapPeriod > 0.0) && std::fabs(a.x0 - b.x0) <= 0.01 &&
               std::fabs(a.x1 - b.x1) <= 0.01 && std::fabs(a.y0 - b.y0) <= 0.01 && std::fabs(a.y1 - b.y1) <= 0.01 &&
               std::fabs(a.wrapPeriod - b.wrapPeriod) <= 0.01;
    }
    bool haveKey = false;
    uint64_t lastKey = 0;
    float lastProj[16] = {};
    bool haveLastProj = false;
    uint32_t sinceAbsorb = 0;
    OverlayHistoryStats st;

    void dropTarget()
    {
        rel(srv); rel(rtv); rel(tex);
        width = height = mips = 0;
        valid = false;
    }
    void dropHistory()
    {
        rel(mSrv); rel(mRtv); rel(mTex);
        rel(hSrv); rel(hRtv); rel(hTex);
        if (hHave) st.clears++;
        hw = hh = 0;
        hHave = false;
        cells.clear();
        anyHeld = false;
        st.width = st.height = 0;
        st.texelsPerUnit = 0;
    }
    ~Impl()
    {
        dropTarget();
        dropHistory();
        rel(depth); rel(blend); rel(raster); rel(smpClamp); rel(smpWrap); rel(smp); rel(cb);
        rel(psSplat); rel(vsSplat); rel(ps); rel(vs);
        rel(dev);
    }
    Status programs(ID3D11Device* d)
    {
        if (dev == d && vs) return Status::Ok();
        dropTarget();
        dropHistory();
        rel(depth); rel(blend); rel(raster); rel(smpClamp); rel(smpWrap); rel(smp); rel(cb);
        rel(psSplat); rel(vsSplat); rel(ps); rel(vs);
        rel(dev);
        dev = d;
        dev->AddRef();
        ID3DBlob* b = nullptr;
        C5R_TRY(compile("vs", "vs_4_0", &b));
        HRESULT h = dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vs);
        rel(b);
        C5R_TRY(hrOk(h, "overlay resample: CreateVertexShader"));
        C5R_TRY(compile("ps", "ps_4_0", &b));
        h = dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &ps);
        rel(b);
        C5R_TRY(hrOk(h, "overlay resample: CreatePixelShader"));
        C5R_TRY(compile("vsSplat", "vs_4_0", &b));
        h = dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vsSplat);
        rel(b);
        C5R_TRY(hrOk(h, "overlay history: CreateVertexShader"));
        C5R_TRY(compile("psSplat", "ps_4_0", &b));
        h = dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psSplat);
        rel(b);
        C5R_TRY(hrOk(h, "overlay history: CreatePixelShader"));
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = 48;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        C5R_TRY(hrOk(dev->CreateBuffer(&bd, nullptr, &cb), "overlay resample: CreateBuffer"));
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        C5R_TRY(hrOk(dev->CreateSamplerState(&sd, &smp), "overlay resample: CreateSamplerState"));
        C5R_TRY(hrOk(dev->CreateSamplerState(&sd, &smpClamp), "overlay history: CreateSamplerState"));
        sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        C5R_TRY(hrOk(dev->CreateSamplerState(&sd, &smpWrap), "overlay history: CreateSamplerState (wrap)"));
        D3D11_RASTERIZER_DESC rd = {};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        C5R_TRY(hrOk(dev->CreateRasterizerState(&rd, &raster), "overlay resample: CreateRasterizerState"));
        D3D11_BLEND_DESC bl = {};
        bl.RenderTarget[0].BlendEnable = FALSE;
        bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        C5R_TRY(hrOk(dev->CreateBlendState(&bl, &blend), "overlay resample: CreateBlendState"));
        D3D11_DEPTH_STENCIL_DESC dd = {};
        dd.DepthEnable = FALSE;
        dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
        C5R_TRY(hrOk(dev->CreateDepthStencilState(&dd, &depth), "overlay resample: CreateDepthStencilState"));
        return Status::Ok();
    }
    Status target(UINT w, UINT h, UINT m)
    {
        if (tex && w == width && h == height && m == mips) return Status::Ok();
        dropTarget();
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = w;
        d.Height = h;
        d.MipLevels = m;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        d.MiscFlags = m > 1 ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
        C5R_TRY(hrOk(dev->CreateTexture2D(&d, nullptr, &tex), "overlay resample: the target (CreateTexture2D)"));
        D3D11_RENDER_TARGET_VIEW_DESC rv = {};
        rv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        rv.Texture2D.MipSlice = 0;
        HRESULT hr = dev->CreateRenderTargetView(tex, &rv, &rtv);
        if (SUCCEEDED(hr)) hr = dev->CreateShaderResourceView(tex, nullptr, &srv);
        if (FAILED(hr)) {
            dropTarget();
            return hrOk(hr, "overlay resample: the target's views");
        }
        width = w;
        height = h;
        mips = m;
        return Status::Ok();
    }
    static Status plainTarget(ID3D11Device* d, UINT w, UINT h, DXGI_FORMAT f, ID3D11Texture2D** t, ID3D11RenderTargetView** r,
                              ID3D11ShaderResourceView** s, const char* what)
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = f;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        C5R_TRY(hrOk(d->CreateTexture2D(&td, nullptr, t), what));
        HRESULT hr = d->CreateRenderTargetView(*t, nullptr, r);
        if (SUCCEEDED(hr)) hr = d->CreateShaderResourceView(*t, nullptr, s);
        return hrOk(hr, what);
    }
    // The history keyed to `w` (made, or cleared when the world changed). ctx clears the new or changed textures.
    Status history(ID3D11DeviceContext* ctx, const OverlayWorld& w)
    {
        const bool wrap = w.wrapPeriod > 0.0;
        if (hHave && sameWorld(w, hWorld) && !forgotten) return Status::Ok();
        const bool countClear = !forgotten;   // forget() counted its own clear
        const double x0 = wrap ? w.x0 : w.x0 - kMargin;
        const double W = wrap ? w.wrapPeriod : (w.x1 - w.x0) + 2.0 * kMargin;
        const double H = (w.y1 - w.y0) + 2.0 * kMargin;
        double d = 1.0;
        d = std::min(d, std::sqrt(kMaxTexels / (W * H)));
        d = std::min(d, kMaxSide / W);
        d = std::min(d, kMaxSide / H);
        const UINT tw = std::max<UINT>(1, static_cast<UINT>(std::ceil(W * d - 1e-6)));
        const UINT th = std::max<UINT>(1, static_cast<UINT>(std::ceil(H * d - 1e-6)));
        if (!hHave || tw != hw || th != hh) {
            if (hHave && !countClear) st.clears--;   // dropHistory() counts it; forget() already did
            dropHistory();
            Status s = plainTarget(dev, tw, th, DXGI_FORMAT_R8G8B8A8_UNORM, &hTex, &hRtv, &hSrv, "overlay history: the colour texture");
            if (s.ok()) s = plainTarget(dev, tw, th, DXGI_FORMAT_R8_UNORM, &mTex, &mRtv, &mSrv, "overlay history: the held texture");
            if (!s.ok()) {
                dropHistory();
                return s;
            }
            hw = tw;
            hh = th;
        } else if (countClear) {
            st.clears++;   // the same size, another map: cleared below
        }
        forgotten = false;
        const float zero[4] = {0, 0, 0, 0};
        ctx->ClearRenderTargetView(hRtv, zero);
        ctx->ClearRenderTargetView(mRtv, zero);
        hWorld = w;
        hWrap = wrap;
        hx0 = x0;
        hW = W;
        hH = H;
        hy1 = w.y1 + kMargin;
        cx = static_cast<uint32_t>(std::ceil(W / kCell));
        cy = static_cast<uint32_t>(std::ceil(H / kCell));
        cells.assign(static_cast<size_t>(cx) * cy, 0);
        anyHeld = false;
        hHave = true;
        st.width = hw;
        st.height = hh;
        st.texelsPerUnit = hw / W;
        return Status::Ok();
    }
    // The CPU mirror's cell of a world point; false outside the history's box (v, or u on a map that does not wrap).
    bool cellOf(double x, double y, size_t& idx) const
    {
        double u = (x - hx0) / hW;
        const double v = (hy1 - y) / hH;
        if (v < 0.0 || v >= 1.0) return false;
        if (hWrap)
            u -= std::floor(u);
        else if (u < 0.0 || u >= 1.0)
            return false;
        const uint32_t ix = std::min(cx - 1, static_cast<uint32_t>(u * hW / kCell));
        const uint32_t iy = std::min(cy - 1, static_cast<uint32_t>(v * hH / kCell));
        idx = static_cast<size_t>(iy) * cx + ix;
        return true;
    }
    void bindCommon(ID3D11DeviceContext* ctx, UINT w, UINT h)
    {
        D3D11_VIEWPORT vp = {0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f};
        ctx->RSSetViewports(1, &vp);
        ctx->RSSetState(raster);
        const float bf[4] = {0, 0, 0, 0};
        ctx->OMSetBlendState(blend, bf, 0xFFFFFFFFu);
        ctx->OMSetDepthStencilState(depth, 0);
        ctx->IASetInputLayout(nullptr);
        ctx->HSSetShader(nullptr, nullptr, 0);
        ctx->DSSetShader(nullptr, nullptr, 0);
        ctx->GSSetShader(nullptr, nullptr, 0);
        ctx->PSSetConstantBuffers(0, 1, &cb);
        ID3D11SamplerState* const ss[2] = {smp, hWrap ? smpWrap : smpClamp};
        ctx->PSSetSamplers(0, 2, ss);
        ctx->VSSetConstantBuffers(0, 1, &cb);
    }
    void unbindCommon(ID3D11DeviceContext* ctx)
    {
        ID3D11RenderTargetView* const noRt[2] = {nullptr, nullptr};
        ctx->OMSetRenderTargets(2, noRt, nullptr);
        ID3D11ShaderResourceView* const none[3] = {nullptr, nullptr, nullptr};
        ctx->PSSetShaderResources(0, 3, none);
        ID3D11Buffer* const noCb = nullptr;
        ctx->PSSetConstantBuffers(0, 1, &noCb);
        ctx->VSSetConstantBuffers(0, 1, &noCb);
        ID3D11SamplerState* const noSmp[2] = {nullptr, nullptr};
        ctx->PSSetSamplers(0, 2, noSmp);
    }
    Status writeCb(ID3D11DeviceContext* ctx, const float v[12])
    {
        D3D11_MAPPED_SUBRESOURCE ms;
        C5R_TRY(hrOk(ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms), "overlay resample: Map"));
        std::memcpy(ms.pData, v, 12 * sizeof(float));
        ctx->Unmap(cb, 0);
        return Status::Ok();
    }
};

OverlayResampler::~OverlayResampler() { delete m_impl; }

void OverlayResampler::release()
{
    if (m_impl) {
        const OverlayHistoryStats keep = m_impl->st;   // the counters outlive the device objects
        delete m_impl;
        m_impl = new Impl();
        m_impl->st = keep;
        if (keep.width) m_impl->st.clears++;
        m_impl->st.width = m_impl->st.height = 0;
        m_impl->st.texelsPerUnit = 0;
    }
}

ID3D11ShaderResourceView* OverlayResampler::srv() const { return m_impl && m_impl->valid ? m_impl->srv : nullptr; }

OverlayHistoryStats OverlayResampler::historyStats() const { return m_impl ? m_impl->st : OverlayHistoryStats(); }

void OverlayResampler::forget()
{
    if (!m_impl) return;
    Impl& p = *m_impl;
    p.haveKey = false;
    p.haveLastProj = false;
    p.sinceAbsorb = 0;
    if (!p.hHave || p.forgotten) return;
    // no device context here: the textures are cleared at the next drawing remember() (history() takes its clear route);
    // until then resample() reads no history
    p.forgotten = true;
    p.anyHeld = false;
    std::fill(p.cells.begin(), p.cells.end(), static_cast<uint8_t>(0));
    p.st.clears++;
}

Status OverlayResampler::remember(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* src, const float proj[16], const OverlayWorld& world,
                                  uint64_t frameKey)
{
    if (!m_impl) m_impl = new Impl();
    Impl& p = *m_impl;
    auto fail = [&](Status s) {
        p.st.rememberFails++;
        return s;
    };
    if (!ctx || !src) return fail(Status::fail(Err::BadLayout, "overlay history: ctx or source null"));
    double wx0, wx1, wy0, wy1;
    if (!windowBox(proj, wx0, wx1, wy0, wy1))
        return fail(Status::fail(Err::BadLayout, "overlay history: the source's window is not an axis-aligned window"));
    if (!(std::isfinite(world.x0) && std::isfinite(world.x1) && std::isfinite(world.y0) && std::isfinite(world.y1) && world.x1 > world.x0 &&
          world.y1 > world.y0))
        return fail(Status::fail(Err::BadLayout, "overlay history: the world box is not finite or empty"));
    if (!std::isfinite(world.wrapPeriod) || world.wrapPeriod < 0.0)
        return fail(Status::fail(Err::BadLayout, "overlay history: the wrap period is not finite or negative"));
    UINT sw, sh, sm;
    {
        const Status s = sourceOf(src, sw, sh, sm);
        if (!s.ok()) return fail(s);
    }
    // CPU bookkeeping first; the device work (programs, textures, the draw) only when this call draws, so that a short
    // replay at rest (an offline bundle of a few frames, a stand-in rig) costs nothing (0793: sceneshot_test 1,136 s ->
    // 3,224 s when every new Scene compiled the programs and made the textures at its first frame)
    if (!p.haveWant || !p.sameWorld(world, p.want)) {
        p.want = world;   // a new map: a new history, cleared when it is next drawn (history())
        p.haveWant = true;
        p.haveKey = false;
        p.haveLastProj = false;
        p.sinceAbsorb = 0;
        p.anyHeld = false;
    }
    if (p.haveKey && frameKey == p.lastKey) {
        p.st.skipped++;
        return Status::Ok();
    }
    p.haveKey = true;
    p.lastKey = frameKey;
    const bool sameWindow = p.haveLastProj && std::memcmp(p.lastProj, proj, sizeof p.lastProj) == 0;
    if (sameWindow && p.sinceAbsorb + 1 < kRefreshCalls) {
        p.sinceAbsorb++;
        p.st.skipped++;
        return Status::Ok();
    }
    if (!p.haveLastProj) {
        // the first window after a load (or forget()): drawn after kRefreshCalls generations at rest, or at once by the
        // next call with another window (any camera move)
        std::memcpy(p.lastProj, proj, sizeof p.lastProj);
        p.haveLastProj = true;
        p.sinceAbsorb = 0;
        p.st.skipped++;
        return Status::Ok();
    }
    ID3D11Device* dev = nullptr;
    ctx->GetDevice(&dev);
    if (!dev) return fail(Status::fail(Err::Device, "overlay history: the context has no device"));
    Status s = p.programs(dev);
    dev->Release();
    if (!s.ok()) return fail(s);
    s = p.history(ctx, p.want);
    if (!s.ok()) return fail(s);
    // the quads: the window in history uv, one per wrap copy it reaches (u + m on a wrapping map)
    const double u0 = (wx0 - p.hx0) / p.hW, u1 = (wx1 - p.hx0) / p.hW;
    const double v0 = (p.hy1 - wy1) / p.hH, v1 = (p.hy1 - wy0) / p.hH;
    const double sx = proj[0], tx = proj[12], sy = proj[5], ty = proj[13];
    int mLo = 0, mHi = 0;
    if (p.hWrap) {
        mLo = static_cast<int>(std::floor(u0));
        mHi = static_cast<int>(std::floor(u1));
        if (mHi - mLo > 4) mHi = mLo + 4;   // a window wider than four periods: refused below
    }
    if (p.hWrap && std::floor(u1) - std::floor(u0) > 4)
        return fail(Status::fail(Err::BadLayout, "overlay history: the window spans more than four wrap periods"));
    ID3D11ShaderResourceView* const noSrv[3] = {nullptr, nullptr, nullptr};
    ctx->PSSetShaderResources(0, 3, noSrv);
    ID3D11RenderTargetView* const rts[2] = {p.hRtv, p.mRtv};
    ctx->OMSetRenderTargets(2, rts, nullptr);
    p.bindCommon(ctx, p.hw, p.hh);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ctx->VSSetShader(p.vsSplat, nullptr, 0);
    ctx->PSSetShader(p.psSplat, nullptr, 0);
    ctx->PSSetShaderResources(0, 1, &src);
    int drawn = 0;
    for (int m = mLo; m <= mHi; ++m) {
        const double qu0 = u0 - m, qu1 = u1 - m;
        if (qu1 <= 0.0 || qu0 >= 1.0 || v1 <= 0.0 || v0 >= 1.0) continue;
        // history uv (uh, vh) -> world x = hx0 + (uh + m) W, y = hy1 - vh H -> source uv
        const double Au = 0.5 * sx * p.hW, Bu = 0.5 * (sx * (p.hx0 + m * p.hW) + tx) + 0.5;
        const double Av = 0.5 * sy * p.hH, Bv = 0.5 - 0.5 * (sy * p.hy1 + ty);
        const float c[12] = {static_cast<float>(qu0), static_cast<float>(v0), static_cast<float>(qu1), static_cast<float>(v1),
                             static_cast<float>(Au), static_cast<float>(Av), static_cast<float>(Bu), static_cast<float>(Bv),
                             0, 0, 0, 0};
        s = p.writeCb(ctx, c);
        if (!s.ok()) {
            p.unbindCommon(ctx);
            return fail(s);
        }
        ctx->Draw(4, 0);
        ++drawn;
    }
    p.unbindCommon(ctx);
    // the CPU mirror: every cell whose centre the window holds
    for (double y = std::floor(wy0 / kCell) * kCell + 0.5 * kCell; y < wy1; y += kCell) {
        if (y < wy0) continue;
        for (double x = std::floor(wx0 / kCell) * kCell + 0.5 * kCell; x < wx1; x += kCell) {
            if (x < wx0) continue;
            size_t idx;
            if (p.cellOf(x, y, idx)) {
                p.cells[idx] = 1;
                p.anyHeld = true;
            }
        }
    }
    std::memcpy(p.lastProj, proj, sizeof p.lastProj);
    p.haveLastProj = true;
    p.sinceAbsorb = 0;
    p.st.absorbed++;
    (void)drawn;
    return Status::Ok();
}

Status OverlayResampler::resample(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* src, const float from[16], const float to[16])
{
    if (m_impl) m_impl->valid = false;
    if (!ctx || !src) return Status::fail(Err::BadLayout, "overlay resample: ctx or source null");
    UvMap map;
    C5R_TRY(uvMapBetweenWindows(from, to, map));
    UINT w, h, viewMips;
    C5R_TRY(sourceOf(src, w, h, viewMips));
    ID3D11Device* dev = nullptr;
    ctx->GetDevice(&dev);
    if (!dev) return Status::fail(Err::Device, "overlay resample: the context has no device");
    if (!m_impl) m_impl = new Impl();
    Status s = m_impl->programs(dev);
    dev->Release();
    C5R_TRY(s);
    Impl& p = *m_impl;
    C5R_TRY(p.target(w, h, viewMips));
    // the history's map: target uv -> world -> history uv (uvMapBetweenWindows checked `to`)
    const bool useHistory = p.hHave && p.anyHeld && !p.forgotten;
    float c[12] = {map.a[0], map.a[1], map.b[0], map.b[1], 0, 0, 0, 0, 0, 0, 0, 0};
    if (useHistory) {
        const double sxT = to[0], txT = to[12], syT = to[5], tyT = to[13];
        c[4] = static_cast<float>(2.0 / (sxT * p.hW));
        c[6] = static_cast<float>(((-1.0 - txT) / sxT - p.hx0) / p.hW);
        c[5] = static_cast<float>(2.0 / (syT * p.hH));
        c[7] = static_cast<float>((p.hy1 - (1.0 - tyT) / syT) / p.hH);
        c[8] = 1.0f;
        c[9] = p.hWrap ? 1.0f : 0.0f;
    }
    // the counters: the edge (cells of `to` whose centre `from` does not hold) and how much of it the history held
    {
        double fx0, fx1, fy0, fy1, tx0, tx1, ty0, ty1;
        if (windowBox(from, fx0, fx1, fy0, fy1) && windowBox(to, tx0, tx1, ty0, ty1)) {
            const double e = 0.01;
            if (tx0 < fx0 - e || tx1 > fx1 + e || ty0 < fy0 - e || ty1 > fy1 + e) {
                p.st.strips++;
                uint64_t n = 0, held = 0;
                if (useHistory) {
                    for (double y = std::floor(ty0 / kCell) * kCell + 0.5 * kCell; y < ty1; y += kCell) {
                        if (y < ty0) continue;
                        for (double x = std::floor(tx0 / kCell) * kCell + 0.5 * kCell; x < tx1; x += kCell) {
                            if (x < tx0) continue;
                            if (x >= fx0 && x <= fx1 && y >= fy0 && y <= fy1) continue;
                            size_t idx;
                            if (!p.cellOf(x, y, idx)) continue;   // off the map's box: no ground
                            ++n;
                            held += p.cells[idx] ? 1 : 0;
                        }
                    }
                }
                p.st.stripCells += n;
                p.st.stripCellsHeld += held;
                if (useHistory && held == n)
                    p.st.stripFilled++;
                else
                    p.st.stripHoles++;
            }
        }
    }
    C5R_TRY(p.writeCb(ctx, c));
    ID3D11ShaderResourceView* const none[3] = {nullptr, nullptr, nullptr};
    ctx->PSSetShaderResources(0, 3, none);
    ctx->OMSetRenderTargets(1, &p.rtv, nullptr);
    p.bindCommon(ctx, w, h);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(p.vs, nullptr, 0);
    ctx->PSSetShader(p.ps, nullptr, 0);
    ID3D11ShaderResourceView* const srvs[3] = {src, useHistory ? p.hSrv : nullptr, useHistory ? p.mSrv : nullptr};
    ctx->PSSetShaderResources(0, 3, srvs);
    ctx->Draw(3, 0);
    p.unbindCommon(ctx);
    if (p.mips > 1) ctx->GenerateMips(p.srv);
    p.valid = true;
    return Status::Ok();
}

} // namespace fow
} // namespace c5r
