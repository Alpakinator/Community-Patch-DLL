// render/tools/fowmask/fowmask.cpp - the FOW mask of c5r::fow::MaskRenderer from a text description of the map and the
// views (epic 0168, task 3952). Own code.
//
//   fowmask --input IN.txt --out OUT [--exe PATH] [--size 1024|512] [--warp]
//
// IN.txt (written by fowmask_input.py from a capture's census, export and the map's grid constants; '#' lines ignored):
//   c5r-fowmask-input 1
//   map W H WRAP                   (WRAP 0 or 1)
//   h2w F x 16                     (M_h2w row-major)
//   scale SX SY
//   origin OX OY
//   world WW WH                    (optional: world width = wrap period, world height; for the camera range rule)
//   states S...                    (W x H digits, row-major y * W + x: 0 visible, 1 seen, 2 unexplored)
//   views N
//   view F x 16 X0 X1 Y0 Y1        (OverlayProj row-major, then the plot range; X1 <= X0: derived by the module)
//   camera F x 16                  (optional, after its view: ViewProj row-major, for the camera range rule)
// Writes OUT.rgba (mip 0, R8G8B8A8 rows, size x size), OUT.png and prints the pass statistics. --exe defaults to C5R_EXE.
// The EXE is opened read-only; its programs are never written anywhere (D83).
// Exit codes: 0 done, 1 an input or device error (the message names the check), 2 usage. Never 3.
#include <windows.h>
#include <d3d11.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <c5r/fow/fow.h>
#include "terrainshot_core.h"

using namespace c5r;

namespace {

int usage()
{
    std::fprintf(stderr, "usage: fowmask --input IN.txt --out OUT [--exe PATH] [--size 1024|512] [--warp]\n");
    return 2;
}

int fail(const Status& s)
{
    std::fprintf(stderr, "fowmask: %s\n", s.str().c_str());
    return 1;
}

std::string narrow(const std::wstring& w)
{
    std::string s;
    for (wchar_t c : w) s.push_back(c < 128 ? static_cast<char>(c) : '?');
    return s;
}

struct Input {
    fow::MapInput map;
    std::vector<fow::PlotState> states;
    std::vector<fow::View> views;
};

Status readInput(const std::wstring& path, Input& in)
{
    std::ifstream f(path.c_str());
    if (!f) return Status::fail(Err::Io, "cannot open " + narrow(path));
    std::string line, key;
    bool magic = false, haveMap = false;
    uint32_t wantViews = 0;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        ls >> key;
        if (key == "c5r-fowmask-input") {
            int v = 0;
            ls >> v;
            if (v != 1) return Status::fail(Err::BadVersion, "input version " + std::to_string(v));
            magic = true;
        } else if (!magic) {
            return Status::fail(Err::BadMagic, "first line is not c5r-fowmask-input 1");
        } else if (key == "map") {
            int wrap = 0;
            ls >> in.map.width >> in.map.height >> wrap;
            in.map.wrapX = wrap != 0;
            haveMap = !ls.fail();
        } else if (key == "h2w") {
            for (float& x : in.map.hexToWorld) ls >> x;
        } else if (key == "scale") {
            ls >> in.map.gridScale[0] >> in.map.gridScale[1];
        } else if (key == "origin") {
            ls >> in.map.gridOrigin[0] >> in.map.gridOrigin[1];
        } else if (key == "states") {
            std::string s;
            ls >> s;
            in.states.clear();
            for (char c : s) {
                if (c < '0' || c > '2') return Status::fail(Err::BadLayout, std::string("state character '") + c + "'");
                in.states.push_back(static_cast<fow::PlotState>(c - '0'));
            }
        } else if (key == "world") {
            ls >> in.map.worldWidth >> in.map.worldHeight;
        } else if (key == "camera") {
            if (in.views.empty()) return Status::fail(Err::BadLayout, "camera line before a view line");
            for (float& x : in.views.back().viewProj) ls >> x;
        } else if (key == "views") {
            ls >> wantViews;
        } else if (key == "view") {
            fow::View v;
            for (float& x : v.overlayProj) ls >> x;
            ls >> v.x0 >> v.x1 >> v.y0 >> v.y1;
            in.views.push_back(v);
        } else {
            return Status::fail(Err::BadLayout, "unknown key " + key);
        }
        if (ls.fail()) return Status::fail(Err::Truncated, "line '" + key + "' incomplete");
    }
    if (!magic) return Status::fail(Err::BadMagic, "empty input");
    if (!haveMap) return Status::fail(Err::NotFound, "no map line");
    if (wantViews != in.views.size())
        return Status::fail(Err::BadLayout, "views " + std::to_string(wantViews) + " announced, " +
                                                std::to_string(in.views.size()) + " given");
    in.map.states = in.states.data();
    in.map.stateCount = in.states.size();
    return Status::Ok();
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    std::wstring input, out, exe;
    uint32_t size = 1024;
    bool warp = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        auto next = [&]() -> const wchar_t* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (a == L"--input") { const wchar_t* v = next(); if (!v) return usage(); input = v; }
        else if (a == L"--out") { const wchar_t* v = next(); if (!v) return usage(); out = v; }
        else if (a == L"--exe") { const wchar_t* v = next(); if (!v) return usage(); exe = v; }
        else if (a == L"--size") { const wchar_t* v = next(); if (!v) return usage(); size = static_cast<uint32_t>(std::wcstoul(v, nullptr, 10)); }
        else if (a == L"--warp") warp = true;
        else return usage();
    }
    if (input.empty() || out.empty()) return usage();
    if (exe.empty()) {
        const char* e = std::getenv("C5R_EXE");
        if (!e) { std::fprintf(stderr, "fowmask: no --exe and no C5R_EXE\n"); return 2; }
        exe = std::wstring(e, e + std::strlen(e));
    }
    Input in;
    Status s = readInput(input, in);
    if (!s.ok()) return fail(s);
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    const D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = E_FAIL;
    if (!warp)
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
    if (FAILED(hr)) return fail(Status::fail(Err::Device, "D3D11CreateDevice"));
    int rc = 0;
    {
        fow::MaskRenderer r;
        s = r.init(dev, exe, size);
        fow::MaskInput mi;
        mi.map = in.map;
        mi.views = in.views.data();
        mi.numViews = static_cast<uint32_t>(in.views.size());
        fow::MaskStats st;
        if (s.ok()) s = r.build(ctx, mi, &st);
        std::vector<uint8_t> px;
        if (s.ok()) {
            ID3D11Resource* res = nullptr;
            r.srv()->GetResource(&res);
            D3D11_TEXTURE2D_DESC d = {};
            d.Width = d.Height = size;
            d.MipLevels = 1;
            d.ArraySize = 1;
            d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_STAGING;
            d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ID3D11Texture2D* stg = nullptr;
            hr = dev->CreateTexture2D(&d, nullptr, &stg);
            if (SUCCEEDED(hr)) {
                ctx->CopySubresourceRegion(stg, 0, 0, 0, 0, res, 0, nullptr);
                D3D11_MAPPED_SUBRESOURCE ms;
                hr = ctx->Map(stg, 0, D3D11_MAP_READ, 0, &ms);
                if (SUCCEEDED(hr)) {
                    px.resize(static_cast<size_t>(size) * size * 4);
                    for (uint32_t y = 0; y < size; ++y)
                        std::memcpy(&px[static_cast<size_t>(y) * size * 4],
                                    static_cast<const uint8_t*>(ms.pData) + static_cast<size_t>(y) * ms.RowPitch, size * 4);
                    ctx->Unmap(stg, 0);
                }
                stg->Release();
            }
            res->Release();
            if (FAILED(hr)) s = Status::fail(Err::Device, "readback");
        }
        if (s.ok()) {
            const std::wstring raw = out + L".rgba";
            FILE* f = _wfopen(raw.c_str(), L"wb");
            if (!f || std::fwrite(px.data(), 1, px.size(), f) != px.size()) s = Status::fail(Err::Io, "write " + narrow(raw));
            if (f) std::fclose(f);
        }
        if (s.ok()) s = terrainshot::writePngRgba(out + L".png", size, size, px);
        if (s.ok())
            std::printf("fowmask: %ux%u, views %u, hexagons seen %u visible %u unexplored %u, dropped %u, draws %u -> %s.rgba/.png\n",
                        size, size, mi.numViews, st.hexagons[0], st.hexagons[1], st.hexagons[2], st.dropped, st.draws,
                        narrow(out).c_str());
        else
            rc = fail(s);
    }
    ctx->Release();
    dev->Release();
    return rc;
}
