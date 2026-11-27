// test_draw.cpp - c5r::pathmodels layer 3 (ticket 0300): the recorded preview drawn on WARP with the EXE's UIShader
// (C5R_EXE) and the install's meshes and textures (C5R_GAME_DIR) at 3801's camera (the capture's cb1), and every
// validation path of the renderer.
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "c5r/gamefs/gamefiles.h"
#include "pathmodels_testkit.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::pathmodels;

namespace {

ID3D11Device* warp(ID3D11DeviceContext** ctx) {
    ID3D11Device* dev = nullptr;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev,
                                   nullptr, ctx);
    return SUCCEEDED(hr) ? dev : nullptr;
}

struct Fx {
    gamefs::GameFiles files;
    PathModels models;
    PathRenderer r;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    Fx() {
        if (c5r::test::gameDir().empty()) c5r::test::skip("C5R_GAME_DIR not set");
        if (c5r::test::exePath().empty()) c5r::test::skip("C5R_EXE not set");
        dev = warp(&ctx);
        if (!dev) c5r::test::skip("no WARP device");
    }
    ~Fx() {
        r.release();
        if (ctx) ctx->Release();
        if (dev) dev->Release();
    }
    Status open() {
        C5R_TRY(files.open(gamefs::fromUtf8(c5r::test::gameDir())));
        C5R_TRY(models.load(files));
        C5R_TRY(r.openPrograms(gamefs::fromUtf8(c5r::test::exePath())));
        return r.create(dev, models, gameFilesReader(files));
    }
};

struct Target {
    ID3D11Texture2D* rt = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11Texture2D* ds = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    ID3D11Texture2D* staging = nullptr;
    UINT w, h;
    Target(ID3D11Device* dev, UINT w_, UINT h_) : w(w_), h(h_) {
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = w;
        d.Height = h;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_RENDER_TARGET;
        dev->CreateTexture2D(&d, nullptr, &rt);
        dev->CreateRenderTargetView(rt, nullptr, &rtv);
        d.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        dev->CreateTexture2D(&d, nullptr, &ds);
        dev->CreateDepthStencilView(ds, nullptr, &dsv);
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.BindFlags = 0;
        d.Usage = D3D11_USAGE_STAGING;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        dev->CreateTexture2D(&d, nullptr, &staging);
    }
    ~Target() {
        if (rtv) rtv->Release();
        if (rt) rt->Release();
        if (dsv) dsv->Release();
        if (ds) ds->Release();
        if (staging) staging->Release();
    }
    void bind(ID3D11DeviceContext* ctx) {
        const float clear[4] = {0, 0, 0, 0};
        ctx->ClearRenderTargetView(rtv, clear);
        ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        ctx->OMSetRenderTargets(1, &rtv, dsv);
        D3D11_VIEWPORT vp = {0, 0, static_cast<float>(w), static_cast<float>(h), 0, 1};
        ctx->RSSetViewports(1, &vp);
    }
    std::vector<uint8_t> read(ID3D11DeviceContext* ctx) {
        ctx->CopyResource(staging, rt);
        D3D11_MAPPED_SUBRESOURCE m;
        std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
        if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
            for (UINT y = 0; y < h; ++y)
                std::memcpy(&px[static_cast<size_t>(y) * w * 4], static_cast<uint8_t*>(m.pData) + y * m.RowPitch, w * 4);
            ctx->Unmap(staging, 0);
        }
        return px;
    }
};

// Pixel of a world point under the capture's view-projection (row vectors, cb1 rows 4-7).
bool project(const float cb1[32], float x, float y, float z, UINT w, UINT h, int& px, int& py) {
    const float* m = cb1 + 16;
    const float cx = x * m[0] + y * m[4] + z * m[8] + m[12];
    const float cy = x * m[1] + y * m[5] + z * m[9] + m[13];
    const float cw = x * m[3] + y * m[7] + z * m[11] + m[15];
    if (!(cw > 0)) return false;
    px = static_cast<int>((cx / cw * 0.5f + 0.5f) * w);
    py = static_cast<int>((0.5f - cy / cw * 0.5f) * h);
    return px >= 0 && py >= 0 && px < static_cast<int>(w) && py < static_cast<int>(h);
}

}  // namespace

C5R_TEST(f6496_drawn_on_warp) {
    Fx fx;
    CHECK_OK(fx.open());
    if (!fx.r.ready()) return;
    std::vector<Instance> inst;
    CHECK_OK(placePath(pmt::f6496Input(), inst));
    std::vector<DrawItem> draws;
    CHECK_OK(buildDraws(fx.models, inst, 0.0f, draws));
    CHECK_EQ(draws.size(), 54u);
    Target t(fx.dev, 1277, 705);   // half of 3801's 2554 x 1411
    t.bind(fx.ctx);
    CHECK_OK(fx.r.draw(fx.ctx, draws, f6496::kCb1));
    const std::vector<uint8_t> px = t.read(fx.ctx);
    size_t covered = 0;
    for (size_t i = 0; i < px.size(); i += 4) covered += (px[i] | px[i + 1] | px[i + 2] | px[i + 3]) != 0;
    std::printf("WARP: %zu of %u pixels covered by the path models\n", covered, t.w * t.h);
    CHECK(covered > 2000u);
    CHECK(covered < static_cast<size_t>(t.w) * t.h / 4);
    // each node's disc covers the pixel of its centre (2 units above the ground: the disc's top)
    const PathInput in = pmt::f6496Input();
    for (size_t i = 0; i < in.pos.size(); ++i) {
        int x = 0, y = 0;
        CHECK(project(f6496::kCb1, in.pos[i].x, in.pos[i].y, in.pos[i].z + 2.0f, t.w, t.h, x, y));
        const size_t o = (static_cast<size_t>(y) * t.w + x) * 4;
        if (!(px[o] | px[o + 1] | px[o + 2]))
            c5r::test::fail(__FILE__, __LINE__, "node " + std::to_string(i) + "'s disc not drawn at its centre pixel");
    }
    // a corner far from the path stays clear
    CHECK_EQ(px[0] | px[1] | px[2] | px[3], 0);
}

C5R_TEST(frame_constants) {
    Mat4 cam, vp;
    for (int k = 0; k < 16; ++k) {
        cam.m[k] = f6496::kCb1[k];
        vp.m[k] = f6496::kCb1[16 + k];
    }
    float cb1[32];
    frameConstants(cam, vp, cb1);
    CHECK_EQ(pmt::maxDiff(cb1, f6496::kCb1, 32), 0.0f);
}

C5R_TEST(program_digests) {
    CHECK(std::string(uiShaderVs().sha256) == f6496::kVsSha);
    CHECK(std::string(uiShaderPs().sha256) == f6496::kPsSha);
}

// ---- validation paths

C5R_TEST(neg_programs) {
    PathRenderer r;
    CHECK_ERR(r.openProgramsFromBlobs({}), Err::NotFound);
    CHECK_ERR(r.openProgramsFromBlobs({std::vector<uint8_t>(64, 1)}), Err::NotFound);
    CHECK_ERR(r.openPrograms(L"C:/no/such/CivilizationV_DX11.exe"), Err::Io);
}

C5R_TEST(neg_create_and_draw) {
    Fx fx;
    PathRenderer bare;
    PathModels empty;
    CHECK_ERR(bare.create(nullptr, empty, nullptr), Err::Device);
    CHECK_ERR(bare.create(fx.dev, empty, nullptr), Err::NotFound);   // no programs
    CHECK(!bare.ready());
    std::vector<DrawItem> one(1);
    CHECK_ERR(bare.draw(fx.ctx, one, f6496::kCb1), Err::Device);
    CHECK_OK(fx.files.open(gamefs::fromUtf8(c5r::test::gameDir())));
    CHECK_OK(fx.models.load(fx.files));
    CHECK_OK(bare.openPrograms(gamefs::fromUtf8(c5r::test::exePath())));
    CHECK_ERR(bare.create(fx.dev, empty, gameFilesReader(fx.files)), Err::NotFound);   // models not loaded
    // a texture missing
    FileReader inner = gameFilesReader(fx.files);
    FileReader noSpec = [inner](const std::string& n, std::vector<uint8_t>& out) -> Status {
        if (n == "centeroneturnspec.dds") return Status::fail(Err::NotFound, "withheld");
        return inner(n, out);
    };
    CHECK_ERR(bare.create(fx.dev, fx.models, noSpec), Err::NotFound);
    CHECK(!bare.ready());
    // t2 not a cube map: a 2D texture served under the cube's name
    FileReader flatCube = [inner](const std::string& n, std::vector<uint8_t>& out) -> Status {
        return inner(n == "cubelight_generic_32.dds" ? "specgray.dds" : n, out);
    };
    CHECK_ERR(bare.create(fx.dev, fx.models, flatCube), Err::Unsupported);
    // not a DDS
    FileReader junk = [inner](const std::string& n, std::vector<uint8_t>& out) -> Status {
        if (n == "colorred.dds") {
            out.assign(16, 7);
            return Status::Ok();
        }
        return inner(n, out);
    };
    Status js = bare.create(fx.dev, fx.models, junk);
    CHECK(!js.ok());
    CHECK(!bare.ready());
    // ready, then an item out of range draws nothing
    CHECK_OK(bare.create(fx.dev, fx.models, inner));
    CHECK(bare.ready());
    DrawItem bad;
    bad.asset = Asset::CenterOneTurn;
    bad.mesh = 2;
    CHECK_ERR(bare.draw(fx.ctx, {bad}, f6496::kCb1), Err::BadLayout);
    bad.asset = static_cast<Asset>(40);
    bad.mesh = 0;
    CHECK_ERR(bare.draw(fx.ctx, {bad}, f6496::kCb1), Err::BadLayout);
    std::vector<DrawItem> d;
    std::vector<Instance> inst(1);
    inst[0].asset = static_cast<Asset>(40);
    CHECK_ERR(buildDraws(fx.models, inst, 0.0f, d), Err::BadLayout);
    inst[0].asset = Asset::CenterOneTurn;
    CHECK_ERR(buildDraws(fx.models, inst, std::nanf(""), d), Err::BadLayout);
}

C5R_TEST_MAIN
