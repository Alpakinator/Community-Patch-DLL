// Helpers of the c5r::arc tests (ticket 0199): a windowless D3D11 device, a colour target and depth with initial contents
// and CPU readback, and the reference set of mkref_arc.py. Header-only. Own code; no game bytes.
#pragma once
#include <d3d11.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <c5r/arc/arc.h>

#pragma comment(lib, "d3d11.lib")

namespace c5r { namespace arctest {

inline std::string env(const char* name, const char* dflt)
{
    const char* e = std::getenv(name);
    return (e && *e) ? std::string(e) : std::string(dflt);
}

inline std::string refRoot() { return env("C5R_ARC_REF", "F:/c5-64bit-work/0199/ref"); }

inline bool readFile(const std::string& path, std::vector<uint8_t>& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

inline bool writeFile(const std::string& path, const void* data, size_t n)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(static_cast<const char*>(data), static_cast<std::streamsize>(n));
    return static_cast<bool>(f);
}

// arc.txt of mkref_arc.py: records by their first word (the rest as numbers / words).
struct Ref {
    std::string dir;
    std::map<std::string, std::vector<std::string>> rec;   // "pass 0" and "pass 1" keep their number in the key
    std::vector<uint8_t> vb, ib, cb;
    bool load(const std::string& root, std::string& why)
    {
        dir = root;
        std::vector<uint8_t> txt;
        if (!readFile(root + "/arc.txt", txt)) {
            why = "no " + root + "/arc.txt (make it with render/tests/arc/mkref_arc.py; set C5R_ARC_REF)";
            return false;
        }
        std::istringstream is(std::string(txt.begin(), txt.end()));
        std::string line;
        while (std::getline(is, line)) {
            std::istringstream ls(line);
            std::string k, w;
            ls >> k;
            if (k.empty()) continue;
            if (k == "pass") {
                ls >> w;
                k += " " + w;
            }
            std::vector<std::string> v;
            while (ls >> w) v.push_back(w);
            if (k == "cam" && !v.empty()) {
                const std::string name = v[0];
                v.erase(v.begin());
                cams[name] = v;
                continue;
            }
            rec[k] = v;
        }
        if (!readFile(root + "/vb.bin", vb) || !readFile(root + "/ib.bin", ib) || !readFile(root + "/cb.bin", cb)) {
            why = "vb.bin, ib.bin or cb.bin missing in " + root;
            return false;
        }
        return true;
    }
    double num(const char* k, size_t i) const
    {
        auto it = rec.find(k);
        if (it == rec.end() || i >= it->second.size()) return 0.0;
        return std::atof(it->second[i].c_str());
    }
    bool has(const char* k) const { return rec.count(k) != 0; }
    // The height field of 3804's Tiny stock arm (records grid and h2w, file heights.u16).
    bool grid(heightfield::HeightGrid& g, std::string& why) const
    {
        auto gi = rec.find("grid");
        auto hi = rec.find("h2w");
        if (gi == rec.end() || gi->second.size() != 10 || hi == rec.end() || hi->second.size() != 16) {
            why = "no grid / h2w records in " + dir + "/arc.txt (mkref_arc.py --stock3804)";
            return false;
        }
        const std::vector<std::string>& v = gi->second;
        g.samplesX = static_cast<uint32_t>(std::atoi(v[0].c_str()));
        g.samplesY = static_cast<uint32_t>(std::atoi(v[1].c_str()));
        g.mapW = static_cast<uint32_t>(std::atoi(v[2].c_str()));
        g.mapH = static_cast<uint32_t>(std::atoi(v[3].c_str()));
        g.wrapX = std::atoi(v[4].c_str()) != 0;
        g.sx = static_cast<float>(std::atof(v[5].c_str()));
        g.sy = static_cast<float>(std::atof(v[6].c_str()));
        g.sz = static_cast<float>(std::atof(v[7].c_str()));
        g.ox = static_cast<float>(std::atof(v[8].c_str()));
        g.oy = static_cast<float>(std::atof(v[9].c_str()));
        for (int i = 0; i < 16; ++i) g.hexToWorld[i] = static_cast<float>(std::atof(hi->second[static_cast<size_t>(i)].c_str()));
        std::vector<uint8_t> raw;
        if (!readFile(dir + "/heights.u16", raw) || raw.size() != static_cast<size_t>(g.samplesX) * g.samplesY * 2u) {
            why = "heights.u16 missing or of another size in " + dir;
            return false;
        }
        g.h.resize(static_cast<size_t>(g.samplesX) * g.samplesY);
        std::memcpy(g.h.data(), raw.data(), raw.size());
        return true;
    }
    // A camera record: plots and the on shot's view-projection.
    bool cam(const std::string& name, int32_t plots[4], float vp[16], double& offDiff) const
    {
        auto it = cams.find(name);
        if (it == cams.end() || it->second.size() != 21) return false;
        const std::vector<std::string>& v = it->second;
        for (int i = 0; i < 4; ++i) plots[i] = std::atoi(v[static_cast<size_t>(i)].c_str());
        offDiff = std::atof(v[4].c_str());
        for (int i = 0; i < 16; ++i) vp[i] = static_cast<float>(std::atof(v[static_cast<size_t>(5 + i)].c_str()));
        return true;
    }
    std::map<std::string, std::vector<std::string>> cams;   // "cam NAME ..." records by NAME
};

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

// RGBA8 colour + D24S8 depth, with optional initial contents (raw texels, tightly packed rows).
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
    bool create(ID3D11Device* dev, uint32_t W, uint32_t H, const void* colour = nullptr, const void* depthBits = nullptr)
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
        D3D11_SUBRESOURCE_DATA sc = {colour, W * 4u, 0};
        if (FAILED(dev->CreateTexture2D(&d, colour ? &sc : nullptr, &tex)) || FAILED(dev->CreateRenderTargetView(tex, nullptr, &rtv)))
            return false;
        d.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        D3D11_SUBRESOURCE_DATA sd = {depthBits, W * 4u, 0};
        return SUCCEEDED(dev->CreateTexture2D(&d, depthBits ? &sd : nullptr, &depth)) &&
               SUCCEEDED(dev->CreateDepthStencilView(depth, nullptr, &dsv));
    }
    void clear(ID3D11DeviceContext* ctx, const float rgba[4])
    {
        ctx->ClearRenderTargetView(rtv, rgba);
        ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    }
    static bool readTex(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* t, std::vector<uint8_t>& out)
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
        out.resize(static_cast<size_t>(d.Width) * d.Height * 4u);
        for (uint32_t y = 0; y < d.Height; ++y)
            std::memcpy(&out[static_cast<size_t>(y) * d.Width * 4u], static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch,
                        d.Width * 4u);
        ctx->Unmap(s, 0);
        s->Release();
        return true;
    }
    bool readColour(ID3D11Device* dev, ID3D11DeviceContext* ctx, std::vector<uint8_t>& out) { return readTex(dev, ctx, tex, out); }
    bool readDepth(ID3D11Device* dev, ID3D11DeviceContext* ctx, std::vector<uint8_t>& out) { return readTex(dev, ctx, depth, out); }
};

}} // namespace c5r::arctest
