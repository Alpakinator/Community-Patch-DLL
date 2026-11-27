// pathshot.cpp - the stand-alone DIAGNOSTIC picture of ticket 0300: 3801's recorded preview placed and drawn by
// c5r::pathmodels at the capture's camera (frame6496's cb1), over stock's picture of the same camera with the path off,
// compared with stock's picture with the path on. Not an acceptance picture (D112): no scene depth (the path is not cut
// by the ground), the clip time is not the screenshot's, the UI's turn labels are in both stock pictures only where
// stock drew them. Writes ours.png, diff.png (|ours - stock| x 4, path mask only) and metrics.json into OUTDIR.
//
//   c5r_pathshot GAME_DIR EXE PATH_OFF.png PATH_ON.png OUTDIR [ANIM_TIME]
//
// Exit codes: 0 written; 2 usage; 3 an input missing or unreadable; 4 a module step failed; 5 sizes differ.
#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "c5r/gamefs/gamefiles.h"
#include "c5r/pathmodels/pathmodels.h"
#include "f6496_reference.h"
#include "pathmodels_testkit.h"

using namespace c5r;
using namespace c5r::pathmodels;

namespace {

struct Image {
    UINT w = 0, h = 0;
    std::vector<uint8_t> rgba;
};

bool readPng(IWICImagingFactory* f, const std::wstring& path, Image& out) {
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* fr = nullptr;
    IWICFormatConverter* conv = nullptr;
    bool ok = SUCCEEDED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                     WICDecodeMetadataCacheOnDemand, &dec)) &&
              SUCCEEDED(dec->GetFrame(0, &fr)) && SUCCEEDED(f->CreateFormatConverter(&conv)) &&
              SUCCEEDED(conv->Initialize(fr, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                         WICBitmapPaletteTypeCustom)) &&
              SUCCEEDED(conv->GetSize(&out.w, &out.h));
    if (ok) {
        out.rgba.resize(static_cast<size_t>(out.w) * out.h * 4);
        ok = SUCCEEDED(conv->CopyPixels(nullptr, out.w * 4, static_cast<UINT>(out.rgba.size()), out.rgba.data()));
    }
    if (conv) conv->Release();
    if (fr) fr->Release();
    if (dec) dec->Release();
    return ok;
}

bool writePng(IWICImagingFactory* f, const std::wstring& path, const Image& img) {
    IWICStream* st = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* fr = nullptr;
    // The PNG encoder's native 32-bit format is BGRA: swap red and blue into a copy.
    std::vector<uint8_t> bgra(img.rgba);
    for (size_t i = 0; i + 3 < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    bool ok = SUCCEEDED(f->CreateStream(&st)) && SUCCEEDED(st->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
              SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
              SUCCEEDED(enc->Initialize(st, WICBitmapEncoderNoCache)) && SUCCEEDED(enc->CreateNewFrame(&fr, nullptr)) &&
              SUCCEEDED(fr->Initialize(nullptr)) && SUCCEEDED(fr->SetSize(img.w, img.h)) &&
              SUCCEEDED(fr->SetPixelFormat(&fmt)) && IsEqualGUID(fmt, GUID_WICPixelFormat32bppBGRA) &&
              SUCCEEDED(fr->WritePixels(img.h, img.w * 4, static_cast<UINT>(bgra.size()), bgra.data())) &&
              SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit());
    if (fr) fr->Release();
    if (enc) enc->Release();
    if (st) st->Release();
    return ok;
}

int fail(int code, const std::string& what) {
    std::fprintf(stderr, "c5r_pathshot: %s\n", what.c_str());
    return code;
}

double pct(std::vector<int>& v, double q) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const double last = static_cast<double>(v.size() - 1);
    const size_t i = static_cast<size_t>(std::min(last, std::floor(q * last + 0.5)));
    return v[i];
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 6 || argc > 8)
        return fail(2, "usage: c5r_pathshot GAME_DIR EXE PATH_OFF.png PATH_ON.png OUTDIR [ANIM_TIME [f6496|p1]]");
    // The path: f6496 = 3801's capture (nodes and ground positions of its discs); p1 = 3804's stock camera p1 on the
    // same Tiny save a turn later in play, route (50,13) (51,14) (52,14) (53,14) (53,13): the shared plots take the
    // capture's positions, (53,14) is (52,14) + 64 in x at the height field's 12.0 (3804's dump, 15300 x 0.2 / 255).
    const std::string pathName = argc == 8 ? argv[7] : "f6496";
    PathInput pathIn = pmt::f6496Input();
    if (pathName == "p1") {
        pathIn.nodes[3].x = 53;
        pathIn.nodes[3].y = 14;
        pathIn.pos[3] = pathIn.pos[2];
        pathIn.pos[3].x += 64.0f;
        pathIn.pos[3].z = 12.0f;
    } else if (pathName != "f6496") {
        return fail(2, "unknown path " + pathName);
    }
    const std::wstring game = gamefs::fromUtf8(argv[1]), exe = gamefs::fromUtf8(argv[2]);
    const std::wstring offPath = gamefs::fromUtf8(argv[3]), onPath = gamefs::fromUtf8(argv[4]);
    const std::string outDir = argv[5];
    const float animTime = argc >= 7 ? static_cast<float>(std::atof(argv[6])) : 0.0f;
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return fail(4, "CoInitializeEx");
    IWICImagingFactory* wic = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
        return fail(4, "WIC factory");
    Image off, on;
    if (!readPng(wic, offPath, off)) return fail(3, "cannot read " + std::string(argv[3]));
    if (!readPng(wic, onPath, on)) return fail(3, "cannot read " + std::string(argv[4]));
    if (off.w != on.w || off.h != on.h) return fail(5, "the two stock pictures differ in size");

    gamefs::GameFiles files;
    PathModels models;
    PathRenderer r;
    Status s = files.open(game);
    if (s.ok()) s = models.load(files);
    if (s.ok()) s = r.openPrograms(exe);
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    if (s.ok() && FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                           &dev, nullptr, &ctx)))
        s = Status::fail(Err::Device, "WARP device");
    if (s.ok()) s = r.create(dev, models, gameFilesReader(files));
    std::vector<Instance> inst;
    std::vector<DrawItem> draws;
    if (s.ok()) s = placePath(pathIn, inst);
    if (s.ok()) s = buildDraws(models, inst, animTime, draws);
    if (!s.ok()) return fail(4, s.str());

    // The target starts as stock's path-off picture; the scene's depth is not available (cleared to 1).
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = off.w;
    d.Height = off.h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_RENDER_TARGET;
    D3D11_SUBRESOURCE_DATA init = {off.rgba.data(), off.w * 4, 0};
    ID3D11Texture2D *rt = nullptr, *ds = nullptr, *staging = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    bool ok = SUCCEEDED(dev->CreateTexture2D(&d, &init, &rt)) && SUCCEEDED(dev->CreateRenderTargetView(rt, nullptr, &rtv));
    d.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ok = ok && SUCCEEDED(dev->CreateTexture2D(&d, nullptr, &ds)) && SUCCEEDED(dev->CreateDepthStencilView(ds, nullptr, &dsv));
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.BindFlags = 0;
    d.Usage = D3D11_USAGE_STAGING;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ok = ok && SUCCEEDED(dev->CreateTexture2D(&d, nullptr, &staging));
    if (!ok) return fail(4, "targets");
    ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    ctx->OMSetRenderTargets(1, &rtv, dsv);
    D3D11_VIEWPORT vp = {0, 0, static_cast<float>(off.w), static_cast<float>(off.h), 0, 1};
    ctx->RSSetViewports(1, &vp);
    s = r.draw(ctx, draws, f6496::kCb1);
    if (!s.ok()) return fail(4, s.str());
    ctx->CopyResource(staging, rt);
    Image ours;
    ours.w = off.w;
    ours.h = off.h;
    ours.rgba.resize(off.rgba.size());
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) return fail(4, "map");
    for (UINT y = 0; y < ours.h; ++y)
        std::memcpy(&ours.rgba[static_cast<size_t>(y) * ours.w * 4], static_cast<uint8_t*>(m.pData) + y * m.RowPitch,
                    ours.w * 4);
    ctx->Unmap(staging, 0);

    // Mask: where stock's path changes the picture (max channel |on - off| > 48) or ours does.
    Image diff = ours;
    std::vector<int> all;
    size_t maskStock = 0, maskOurs = 0, both = 0;
    for (size_t i = 0; i < ours.rgba.size(); i += 4) {
        int dStock = 0, dOurs = 0, dv = 0;
        for (int c = 0; c < 3; ++c) {
            dStock = std::max(dStock, std::abs(on.rgba[i + c] - off.rgba[i + c]));
            dOurs = std::max(dOurs, std::abs(ours.rgba[i + c] - off.rgba[i + c]));
            dv = std::max(dv, std::abs(ours.rgba[i + c] - on.rgba[i + c]));
        }
        const bool inS = dStock > 48, inO = dOurs > 48;
        maskStock += inS;
        maskOurs += inO;
        both += inS && inO;
        if (inS || inO) all.push_back(dv);
        for (int c = 0; c < 3; ++c)
            diff.rgba[i + c] = (inS || inO) ? static_cast<uint8_t>(std::min(255, dv * 4)) : 0;
        diff.rgba[i + 3] = 255;
        ours.rgba[i + 3] = 255;
    }
    std::vector<int> sorted = all;
    const double med = pct(sorted, 0.5), p95 = pct(sorted, 0.95), mx = sorted.empty() ? 0 : sorted.back();
    CreateDirectoryA(outDir.c_str(), nullptr);
    if (!writePng(wic, gamefs::fromUtf8(outDir + "/ours.png"), ours) ||
        !writePng(wic, gamefs::fromUtf8(outDir + "/diff.png"), diff))
        return fail(3, "cannot write the pictures into " + outDir);
    char js[640];
    std::snprintf(js, sizeof js,
                  "{\"label\": \"DIAGNOSTIC (no scene depth, clip time %.4f s, path %s)\",\n"
                  " \"size\": [%u, %u], \"draws\": %zu, \"mask_px\": %zu, \"stock_path_px\": %zu, \"ours_path_px\": %zu,\n"
                  " \"both_px\": %zu, \"median\": %.0f, \"p95\": %.0f, \"max\": %.0f}\n",
                  animTime, pathName.c_str(), ours.w, ours.h, draws.size(), all.size(), maskStock, maskOurs, both, med, p95, mx);
    FILE* f = std::fopen((outDir + "/metrics.json").c_str(), "wb");
    if (!f) return fail(3, "cannot write metrics.json");
    std::fputs(js, f);
    std::fclose(f);
    std::fputs(js, stdout);
    staging->Release();
    dsv->Release();
    ds->Release();
    rtv->Release();
    rt->Release();
    r.release();
    ctx->Release();
    dev->Release();
    wic->Release();
    return 0;
}
