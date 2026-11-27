// Helpers of the c5r::fow cloud tests (task 3955): a windowless D3D11 device (hardware, WARP if none), a colour target and
// a depth buffer with CPU readback, a mask texture filled by the test, and the Tiny map's placement of 3951's captures.
// Header-only so that the folder's *_util.cpp glob (shared with 3952's tests) is not touched. Own code; no game bytes.
#pragma once
#include <d3d11.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <c5r/fow/fow.h>

#pragma comment(lib, "d3d11.lib")

namespace c5r { namespace cloudtest {

struct Device {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    ~Device()
    {
        if (ctx) ctx->Release();
        if (dev) dev->Release();
    }
    bool create(bool warp = false)
    {
        const D3D_FEATURE_LEVEL fl[] = {D3D_FEATURE_LEVEL_11_0};
        HRESULT h = E_FAIL;
        if (!warp)
            h = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
        if (FAILED(h))
            h = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
        return SUCCEEDED(h);
    }
};

// RGBA8 colour target plus D32 depth, both readable.
struct Target {
    ID3D11Texture2D* tex = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11Texture2D* depth = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    uint32_t w = 0, h = 0;
    ~Target()
    {
        if (dsv) dsv->Release();
        if (depth) depth->Release();
        if (rtv) rtv->Release();
        if (tex) tex->Release();
    }
    bool create(ID3D11Device* dev, uint32_t W, uint32_t H)
    {
        w = W;
        h = H;
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = W;
        d.Height = H;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(dev->CreateTexture2D(&d, nullptr, &tex)) || FAILED(dev->CreateRenderTargetView(tex, nullptr, &rtv))) return false;
        d.Format = DXGI_FORMAT_D32_FLOAT;
        d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        return SUCCEEDED(dev->CreateTexture2D(&d, nullptr, &depth)) && SUCCEEDED(dev->CreateDepthStencilView(depth, nullptr, &dsv));
    }
    void clear(ID3D11DeviceContext* ctx, const float rgba[4])
    {
        ctx->ClearRenderTargetView(rtv, rgba);
        ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
    }
    // bytes per pixel 4 (colour) or 4 (depth as float)
    static bool readTex(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* t, uint32_t W, uint32_t H,
                        std::vector<uint8_t>& out)
    {
        D3D11_TEXTURE2D_DESC d = {};
        t->GetDesc(&d);
        d.Usage = D3D11_USAGE_STAGING;
        d.BindFlags = 0;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D* s = nullptr;
        if (FAILED(dev->CreateTexture2D(&d, nullptr, &s))) return false;
        ctx->CopyResource(s, t);
        D3D11_MAPPED_SUBRESOURCE m = {};
        if (FAILED(ctx->Map(s, 0, D3D11_MAP_READ, 0, &m))) {
            s->Release();
            return false;
        }
        out.resize(static_cast<size_t>(W) * H * 4);
        for (uint32_t y = 0; y < H; ++y)
            std::memcpy(out.data() + static_cast<size_t>(y) * W * 4, static_cast<const uint8_t*>(m.pData) + y * m.RowPitch, W * 4);
        ctx->Unmap(s, 0);
        s->Release();
        return true;
    }
    bool readColour(ID3D11Device* dev, ID3D11DeviceContext* ctx, std::vector<uint8_t>& out)
    {
        return readTex(dev, ctx, tex, w, h, out);
    }
    bool readDepth(ID3D11Device* dev, ID3D11DeviceContext* ctx, std::vector<float>& out)
    {
        std::vector<uint8_t> b;
        if (!readTex(dev, ctx, depth, w, h, b)) return false;
        out.resize(static_cast<size_t>(w) * h);
        std::memcpy(out.data(), b.data(), b.size());
        return true;
    }
};

// A size x size RGBA8 mask; fill(u, v) gives the texel (R, G, B, A) at texel centre (u, v) in [0, 1).
template <class F> ID3D11ShaderResourceView* makeMask(ID3D11Device* dev, uint32_t size, F fill)
{
    std::vector<uint32_t> px(static_cast<size_t>(size) * size);
    for (uint32_t y = 0; y < size; ++y)
        for (uint32_t x = 0; x < size; ++x) {
            uint8_t c[4];
            fill((x + 0.5f) / size, (y + 0.5f) / size, c);
            px[static_cast<size_t>(y) * size + x] = c[0] | (c[1] << 8) | (c[2] << 16) | (static_cast<uint32_t>(c[3]) << 24);
        }
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = d.Height = size;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA s = {px.data(), size * 4, 0};
    ID3D11Texture2D* t = nullptr;
    ID3D11ShaderResourceView* v = nullptr;
    if (FAILED(dev->CreateTexture2D(&d, &s, &t))) return nullptr;
    dev->CreateShaderResourceView(t, nullptr, &v);
    t->Release();
    return v;
}

// The map placement of the Tiny map of 3951's captures (56 x 36 plots, 3584 x 2560 samples): the world box of
// water-system.md 10.6 and M_h2w, sx, sy, ox, oy of camera-and-picking.md 9 [L] (M_h2w rounded there to 4-5 digits).
inline fow::CloudWorld tinyWorld()
{
    fow::CloudWorld w;
    const float m[16] = {63.9986f, 0.00094f, 0, 0, 32.0f, 64.0019f, 0, 0, 0, 0, 0, 0, 32.0007f, 128.0033f, 0, 1};
    std::memcpy(w.hexToWorld, m, sizeof m);
    w.sx = 1.0f;
    w.sy = 0.866f;
    w.ox = 1792.0f;
    w.oy = 1108.48f;
    w.minX = -1791.5f;
    w.maxX = 1791.5f;
    w.minY = -1108.047f;
    w.maxY = 1108.047f;
    w.samplesX = 3584;
    w.samplesY = 2560;
    return w;
}

// The view-projections of 3951's t21 captures, solved from the cloud draws' WVP = World x VP (render/evidence/0168/
// 3955/cloud_cb.py; camera matrices, no game bytes) [L].
inline void cam1VP(float out[16])
{
    const float m[16] = {2.135113f, 0, 0, 0, 0, 2.73274936f, 0.716057581f, 0.707106835f, 0, 2.73274922f, -0.716057539f,
                         -0.707106829f, 2461.37555f, -790.28332f, 349.885524f, 395.511957f};
    std::memcpy(out, m, sizeof m);
}
inline void cam2VP(float out[16])
{
    const float m[16] = {2.135113f, 0, 0, 0, 0, 2.73274936f, 0.716057581f, 0.707106835f, 0, 2.73274922f, -0.716057539f,
                         -0.707106829f, 2596.28334f, -1093.3892f, 36.5978618f, 86.1403787f};
    std::memcpy(out, m, sizeof m);
}

}} // namespace c5r::cloudtest
