// reflbench - the GPU cost of the reflected ice (issue 0382; research/05-graphics-composition/polar-ice.md 13): what the
// server's water scene adds per drawn frame, timed with D3D11 timestamp queries on one device.
// Own code (GPL-3.0-or-later). The EXE's IceShader blobs and the ice's textures are read at run time (D83); the mesh is the
// ice tap's file form (shell/common/shell_ice_tap.h, VPShellIceImage) of a reference set, never committed.
//
//   reflbench --exe EXE --game INSTALL --mesh ICE.c5ice --vp VP.txt --width W [--reps N] [--warmup N] [--json OUT]
//
// VP.txt: the scene's g_View[0] x g_Proj, 16 floats (row-major, D3DX). W: the world width (the map wraps: three copies).
// Per rep, in one disjoint query: clear the 1024 x 1024 RGBA8 target and its D24S8 depth (stock's 0x5A0F80) [t0..t1], the
// ice drawn with the reflection camera of the view (c5r::water::reflectionCamera) [t1..t2]; the context is flushed and the
// rep's queries read before the next (one rep in flight: the numbers are the passes' own, not overlapped work). The
// shared inputs t7-t10 are constant textures of stock's sizes (fog ramp 128 x 8, FOW mask 1024^2 visible, cloud base 1024^2,
// LUT 32^3): the cost of the draw, not of their contents. Prints and writes median / p95 / mean / min of each part in ms,
// the adapter, the draws and indices of one rep, and the CPU time of the ice module's draw call. Exit 0 ok, 1 a step failed,
// 2 usage.
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "c5r/gamefs/gamefiles.h"
#include "c5r/ice/ice.h"
#include "c5r/shaders/shadersection.h"
#include "c5r/water/water.h"

using namespace c5r;

namespace {

template <class T> void rel(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

bool readFile(const std::wstring& p, std::vector<uint8_t>& out) {
    FILE* f = _wfopen(p.c_str(), L"rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? static_cast<size_t>(n) : 0);
    const bool ok = out.empty() || std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

uint32_t rd32(const std::vector<uint8_t>& b, size_t off) {
    uint32_t v = 0;
    if (off + 4 <= b.size()) std::memcpy(&v, b.data() + off, 4);
    return v;
}

// The tap's file form (VPShellIceImage 112 B: +0 magic 'C5IC', +4 version 1, +28 chunks, +36 fields bytes, +48 VB bytes,
// +52 IB bytes, +56 fields offset, +60 chunk offset, +64 VB offset, +68 IB offset, +72 image bytes; chunks 32 B each).
bool loadMesh(const std::wstring& path, ice::Mesh& m, std::string& why) {
    std::vector<uint8_t> img;
    if (!readFile(path, img) || img.size() < 112) { why = "mesh file missing or short"; return false; }
    if (rd32(img, 0) != 0x43493543u || rd32(img, 4) != 1 || rd32(img, 36) != ice::kFieldsSize || rd32(img, 72) != img.size()) {
        why = "not an ice tap image of version 1";
        return false;
    }
    const uint32_t chunks = rd32(img, 28), vbB = rd32(img, 48), ibB = rd32(img, 52);
    const uint32_t fo = rd32(img, 56), co = rd32(img, 60), vo = rd32(img, 64), io = rd32(img, 68);
    if (!chunks || chunks > 4096 || static_cast<uint64_t>(fo) + ice::kFieldsSize > img.size() ||
        static_cast<uint64_t>(co) + 32ull * chunks > img.size() || static_cast<uint64_t>(vo) + vbB > img.size() ||
        static_cast<uint64_t>(io) + ibB > img.size()) {
        why = "image parts outside the file";
        return false;
    }
    m = ice::Mesh();
    m.vb.assign(img.begin() + vo, img.begin() + vo + vbB);
    m.ib.assign(img.begin() + io, img.begin() + io + ibB);
    std::memcpy(m.fields.raw, img.data() + fo, ice::kFieldsSize);
    m.fields.valid = true;
    for (uint32_t i = 0; i < chunks; ++i) {
        const size_t o = co + 32ull * i;
        ice::Chunk c;
        std::memcpy(c.box, img.data() + o, 16);
        c.triangles = rd32(img, o + 16);
        c.startIndex = rd32(img, o + 20);
        std::memcpy(&c.baseVertex, img.data() + o + 24, 4);
        m.chunks.push_back(c);
    }
    return true;
}

ID3D11ShaderResourceView* tex2D(ID3D11Device* dev, UINT w, UINT h, uint32_t rgba) {
    std::vector<uint32_t> px(static_cast<size_t>(w) * h, rgba);
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = w;
    d.Height = h;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA s = {px.data(), w * 4, 0};
    ID3D11Texture2D* t = nullptr;
    ID3D11ShaderResourceView* v = nullptr;
    if (SUCCEEDED(dev->CreateTexture2D(&d, &s, &t))) dev->CreateShaderResourceView(t, nullptr, &v);
    rel(t);
    return v;
}

ID3D11ShaderResourceView* tex3D(ID3D11Device* dev, UINT n) {
    std::vector<uint32_t> px(static_cast<size_t>(n) * n * n, 0xFF808080u);
    D3D11_TEXTURE3D_DESC d = {};
    d.Width = d.Height = d.Depth = n;
    d.MipLevels = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA s = {px.data(), n * 4, n * n * 4};
    ID3D11Texture3D* t = nullptr;
    ID3D11ShaderResourceView* v = nullptr;
    if (SUCCEEDED(dev->CreateTexture3D(&d, &s, &t))) dev->CreateShaderResourceView(t, nullptr, &v);
    rel(t);
    return v;
}

struct Stat {
    double median = 0, p95 = 0, mean = 0, min = 0;
};
Stat stat(std::vector<double> v) {
    Stat s;
    if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    s.median = v[v.size() / 2];
    s.p95 = v[std::min(v.size() - 1, static_cast<size_t>(v.size() * 0.95))];
    s.min = v.front();
    for (double x : v) s.mean += x;
    s.mean /= static_cast<double>(v.size());
    return s;
}

int usage() {
    std::fprintf(stderr, "usage: reflbench --exe EXE --game INSTALL --mesh ICE.c5ice --vp VP.txt --width W [--reps N] [--warmup N] [--json OUT]\n");
    return 2;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    std::wstring exe, game, mesh, vpFile, json;
    float width = 0;
    int reps = 300, warmup = 30;
    for (int i = 1; i < argc; ++i) {
        const std::wstring k = argv[i];
        if (i + 1 >= argc) return usage();
        const std::wstring v = argv[++i];
        if (k == L"--exe") exe = v;
        else if (k == L"--game") game = v;
        else if (k == L"--mesh") mesh = v;
        else if (k == L"--vp") vpFile = v;
        else if (k == L"--json") json = v;
        else if (k == L"--width") width = static_cast<float>(_wtof(v.c_str()));
        else if (k == L"--reps") reps = _wtoi(v.c_str());
        else if (k == L"--warmup") warmup = _wtoi(v.c_str());
        else return usage();
    }
    if (exe.empty() || game.empty() || mesh.empty() || vpFile.empty() || !(width > 0) || reps < 10 || warmup < 0) return usage();
    float vp[16];
    {
        FILE* f = _wfopen(vpFile.c_str(), L"r");
        int n = 0;
        if (f) {
            while (n < 16 && std::fscanf(f, "%f", &vp[n]) == 1) ++n;
            std::fclose(f);
        }
        if (n != 16) { std::fprintf(stderr, "reflbench: %ls: want 16 floats\n", vpFile.c_str()); return 1; }
    }
    water::ReflectionCamera rc;
    Status st = water::reflectionCamera(vp, rc);
    if (!st.ok()) { std::fprintf(stderr, "reflbench: reflection camera: %s\n", st.str().c_str()); return 1; }
    ice::Mesh m;
    std::string why;
    if (!loadMesh(mesh, m, why)) { std::fprintf(stderr, "reflbench: %ls: %s\n", mesh.c_str(), why.c_str()); return 1; }

    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    const D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx))) {
        std::fprintf(stderr, "reflbench: no hardware device\n");
        return 1;
    }
    std::string adapter = "?";
    {
        IDXGIDevice* dd = nullptr;
        IDXGIAdapter* ad = nullptr;
        DXGI_ADAPTER_DESC desc;
        if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dd))) && SUCCEEDED(dd->GetAdapter(&ad)) &&
            SUCCEEDED(ad->GetDesc(&desc))) {
            char b[256];
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, b, sizeof b, nullptr, nullptr);
            adapter = b;
        }
        rel(ad);
        rel(dd);
    }
    shaders::ShaderSection sec;
    gamefs::GameFiles fs;
    ice::Renderer r;
    st = sec.open(exe);
    if (st.ok()) st = fs.open(game);
    if (st.ok()) st = r.init(dev, sec, fs);
    if (st.ok()) st = r.setMesh(m);
    if (!st.ok()) { std::fprintf(stderr, "reflbench: ice module: %s\n", st.str().c_str()); return 1; }

    // the reflection target and depth as srv_water_scene.cpp makes them
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = d.Height = water::kReflectionSize;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D *ct = nullptr, *dt = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    dev->CreateTexture2D(&d, nullptr, &ct);
    d.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    dev->CreateTexture2D(&d, nullptr, &dt);
    if (ct) dev->CreateRenderTargetView(ct, nullptr, &rtv);
    if (dt) dev->CreateDepthStencilView(dt, nullptr, &dsv);
    ice::FrameInputs in;
    std::memcpy(in.cam.view, rc.view, 64);
    std::memcpy(in.cam.proj, rc.proj, 64);
    in.cam.width = in.cam.height = water::kReflectionSize;
    in.worldWidth = width;
    in.wrapX = true;
    in.fogRamp = tex2D(dev, 128, 8, 0xFFFFFFFFu);
    in.fowMask = tex2D(dev, 1024, 1024, 0xFFFFFFFFu);
    in.cloudBase = tex2D(dev, 1024, 1024, 0xFF000000u);
    in.fowLut = tex3D(dev, 32);
    if (!rtv || !dsv || !in.fogRamp || !in.fowMask || !in.cloudBase || !in.fowLut) {
        std::fprintf(stderr, "reflbench: target or inputs not made\n");
        return 1;
    }
    D3D11_QUERY_DESC qd = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    ID3D11Query *dq = nullptr, *q[3] = {};
    dev->CreateQuery(&qd, &dq);
    qd.Query = D3D11_QUERY_TIMESTAMP;
    for (auto& x : q) dev->CreateQuery(&qd, &x);
    std::vector<double> clearMs, iceMs, cpuMs;
    ice::FrameStats fsOne;
    const float zero[4] = {0, 0, 0, 0};
    for (int i = 0; i < warmup + reps; ++i) {
        ctx->Begin(dq);
        ctx->End(q[0]);
        ctx->ClearRenderTargetView(rtv, zero);
        ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        ctx->End(q[1]);
        ice::FrameStats s;
        st = r.draw(ctx, rtv, dsv, in, &s);
        ctx->End(q[2]);
        ctx->End(dq);
        ctx->Flush();
        if (!st.ok()) { std::fprintf(stderr, "reflbench: draw: %s\n", st.str().c_str()); return 1; }
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj;
        UINT64 t[3];
        while (ctx->GetData(dq, &dj, sizeof dj, 0) == S_FALSE) {}
        for (int k = 0; k < 3; ++k)
            while (ctx->GetData(q[k], &t[k], sizeof t[k], 0) == S_FALSE) {}
        if (i < warmup || dj.Disjoint) continue;
        const double f = static_cast<double>(dj.Frequency) / 1000.0;
        clearMs.push_back(static_cast<double>(t[1] - t[0]) / f);
        iceMs.push_back(static_cast<double>(t[2] - t[1]) / f);
        cpuMs.push_back(s.cpuMs);
        fsOne = s;
    }
    const Stat a = stat(clearMs), b = stat(iceMs), c = stat(cpuMs);
    char out[2048];
    std::snprintf(out, sizeof out,
                  "{\"adapter\": \"%s\", \"reps\": %zu, \"warmup\": %d, \"chunks\": %zu, \"copies\": %u, \"draws\": %u, \"indices\": %llu, "
                  "\"half\": [%.3f, %.3f], \"clear_ms\": {\"median\": %.5f, \"p95\": %.5f, \"mean\": %.5f, \"min\": %.5f}, "
                  "\"ice_ms\": {\"median\": %.5f, \"p95\": %.5f, \"mean\": %.5f, \"min\": %.5f}, "
                  "\"ice_cpu_ms\": {\"median\": %.5f, \"p95\": %.5f, \"mean\": %.5f, \"min\": %.5f}}",
                  adapter.c_str(), iceMs.size(), warmup, m.chunks.size(), fsOne.copies, fsOne.draws,
                  static_cast<unsigned long long>(fsOne.indices), rc.halfX, rc.halfY, a.median, a.p95, a.mean, a.min, b.median, b.p95,
                  b.mean, b.min, c.median, c.p95, c.mean, c.min);
    std::printf("%s\n", out);
    if (!json.empty()) {
        FILE* f = _wfopen(json.c_str(), L"w");
        if (!f) { std::fprintf(stderr, "reflbench: cannot write %ls\n", json.c_str()); return 1; }
        std::fprintf(f, "%s\n", out);
        std::fclose(f);
    }
    rel(dq);
    for (auto& x : q) rel(x);
    rel(rtv);
    rel(dsv);
    rel(ct);
    rel(dt);
    r.release();
    rel(ctx);
    rel(dev);
    return 0;
}
