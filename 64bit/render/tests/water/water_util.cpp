// Shared helpers of the c5r::water tests (issue 3604). Own code.
#include "water_util.h"

#include <cmath>
#include <cstring>

namespace c5r { namespace wtest {

Device::~Device() {
    if (ctx) ctx->Release();
    if (dev) dev->Release();
}

Status Device::create(bool warp) {
    const D3D_FEATURE_LEVEL want = D3D_FEATURE_LEVEL_11_0;
    HRESULT h = E_FAIL;
    if (!warp) {
        h = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &want, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
        if (SUCCEEDED(h)) adapter = "hardware";
    }
    if (FAILED(h)) {
        h = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &want, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
        if (SUCCEEDED(h)) adapter = "WARP";
    }
    if (FAILED(h)) return Status::fail(Err::Device, "D3D11CreateDevice (feature level 11_0) failed");
    return Status::Ok();
}

Target::~Target() {
    if (rtv) rtv->Release();
    if (staging) staging->Release();
    if (tex) tex->Release();
}

Status Target::create(ID3D11Device* dev, uint32_t width, uint32_t height, DXGI_FORMAT f) {
    w = width;
    h = height;
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = w;
    d.Height = h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = f;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(dev->CreateTexture2D(&d, nullptr, &tex))) return Status::fail(Err::Device, "CreateTexture2D (target)");
    if (FAILED(dev->CreateRenderTargetView(tex, nullptr, &rtv))) return Status::fail(Err::Device, "CreateRenderTargetView");
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(dev->CreateTexture2D(&d, nullptr, &staging))) return Status::fail(Err::Device, "CreateTexture2D (staging)");
    return Status::Ok();
}

Status Target::read(ID3D11DeviceContext* ctx, std::vector<uint8_t>& rgba) {
    ctx->CopyResource(staging, tex);
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) return Status::fail(Err::Device, "Map (staging)");
    rgba.resize(static_cast<size_t>(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        std::memcpy(rgba.data() + static_cast<size_t>(y) * w * 4, static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch, w * 4);
    ctx->Unmap(staging, 0);
    return Status::Ok();
}

heightfield::HeightGrid flatGrid(uint32_t mapW, uint32_t mapH, uint16_t hv, bool wrap) {
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
    g.waterZ = 10.117f;
    g.h.assign(static_cast<size_t>(g.samplesX) * g.samplesY, hv);
    return g;
}

terrain::Camera downCamera(float cx, float cy, float height, uint32_t w, uint32_t h) {
    terrain::Camera c;
    const float v[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -cx, -cy, -height, 1};
    std::memcpy(c.view, v, sizeof v);
    const float n = 1.0f, f = 5000.0f;
    const float ys = 1.0f / std::tan(15.0f * 3.14159265f / 180.0f), xs = ys * static_cast<float>(h) / static_cast<float>(w);
    const float p[16] = {xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, f / (n - f), -1, 0, 0, n * f / (n - f), 0};
    std::memcpy(c.proj, p, sizeof p);
    c.width = w;
    c.height = h;
    return c;
}

}} // namespace c5r::wtest
