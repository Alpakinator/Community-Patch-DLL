// test_draw.cpp - ticket 4873: the draw layer (BuildingPrograms, ModelRenderer): every refusal, and one draw of a
// library part with the EXE's programs on the hardware device (the install, the database and the EXE read at run time;
// skipped without them, which build.py counts as a failure). The harness (render/tools/cityscene) compares whole
// frames with the captures; this test keeps the code paths covered.
#include <d3d11.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <vector>

#include "c5r/gamefs/gamefiles.h"
#include "c5r/landmarks/draw.h"
#include "landmarks_testkit.h"
#include "shell_model_scene.h"

using namespace c5r;
using namespace c5r::landmarks;

namespace {

struct Device {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    Device() {
        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0, got;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev,
                                     &got, &ctx)))
            D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, &got, &ctx);
    }
    ~Device() {
        if (ctx) ctx->Release();
        if (dev) dev->Release();
    }
};

}  // namespace

C5R_TEST(programs_negative) {
    BuildingPrograms p;
    CHECK_ERR(p.openFromBlobs({}), Err::NotFound);
    CHECK(!p.opened());
    CHECK_ERR(p.openFromBlobs({std::vector<uint8_t>(64, 0x44)}), Err::NotFound);
    CHECK_ERR(p.create(nullptr), Err::Device);
    Device d;
    if (d.dev) CHECK_ERR(p.create(d.dev), Err::NotFound);   // not opened
    CHECK(p.vs(Material::Blocks) == nullptr);
    CHECK(p.layout(Material::Skinned) == nullptr);
}

C5R_TEST(renderer_negative) {
    ModelRenderer r;
    ModelScene s;
    ViewInput v;
    EnvInput e;
    CHECK_ERR(r.draw(nullptr, s, v, e, nullptr), Err::Device);
    CHECK_ERR(r.prepare(0), Err::Device);
    CHECK(!r.prepared(0));
    BuildingPrograms p;
    ModelLibrary lib;
    gamefs::GameFiles files;
    CHECK_ERR(r.init(nullptr, p, lib, files), Err::Device);
    Device d;
    if (d.dev) CHECK_ERR(r.init(d.dev, p, lib, files), Err::NotFound);   // programs not opened
}

C5R_TEST(tables) {
    // the input layouts: slot 0 the three vertex forms, slot 1 the instance rows or the id
    CHECK_EQ(inputElements(Material::Blocks).size(), 7u);
    CHECK_EQ(inputElements(Material::Instanced).size(), 7u);
    CHECK_EQ(inputElements(Material::Single).size(), 3u);
    CHECK_EQ(inputElements(Material::Skinned).size(), 6u);
    CHECK_EQ(inputElements(Material::Instanced)[6].AlignedByteOffset, 48u);
    CHECK_EQ(inputElements(Material::Skinned)[5].Format, DXGI_FORMAT_R32_UINT);
    // the captures' states (landmark-renderer.md 7.3)
    CHECK(!captureStates(Material::Blocks, DrawPass::Scene).blend.RenderTarget[0].BlendEnable);
    CHECK(captureStates(Material::Instanced, DrawPass::Scene).blend.RenderTarget[0].BlendEnable);
    CHECK_EQ(captureStates(Material::Single, DrawPass::Scene).raster.CullMode, D3D11_CULL_BACK);
    CHECK_EQ(captureStates(Material::Single, DrawPass::Reflection).raster.CullMode, D3D11_CULL_FRONT);
    CHECK_EQ(captureStates(Material::Skinned, DrawPass::Scene).depth.DepthFunc, D3D11_COMPARISON_LESS_EQUAL);
    CHECK_EQ(captureSampler(6).AddressU, D3D11_TEXTURE_ADDRESS_CLAMP);
    CHECK_EQ(captureSampler(0).AddressU, D3D11_TEXTURE_ADDRESS_WRAP);
}

C5R_TEST(draw_one_block) {
    const std::string exe = test::exePath();
    if (exe.empty()) test::skip("C5R_EXE not set");
    lmt::Install& in = lmt::install(true);
    CHECK_OK(in.libStatus);
    Device d;
    if (!d.dev) test::skip("no D3D11 device");
    BuildingPrograms prog;
    CHECK_OK(prog.open(gamefs::fromUtf8(exe)));
    ModelRenderer r;
    CHECK_OK(r.init(d.dev, prog, in.lib, in.files));
    CHECK_ERR(r.prepare(static_cast<uint32_t>(in.lib.files().size())), Err::BadLayout);
    const int32_t fi = in.lib.fileIndex("euro_anc_city.gr2", 1.0f);
    CHECK(fi >= 0);
    if (fi < 0) return;
    CHECK_OK(r.prepare(static_cast<uint32_t>(fi)));
    CHECK(r.prepared(static_cast<uint32_t>(fi)));
    CHECK_OK(r.prepare(static_cast<uint32_t>(fi)));   // idempotent
    const ModelFile& mf = in.lib.files()[fi];
    const ModelLibrary::Buffers* b = nullptr;
    CHECK_OK(in.lib.resident(static_cast<uint32_t>(fi), &b));
    // part 0's box from its float vertices, the world rows put it at the origin, z from 0 up; an orthographic
    // view-projection that is diagonal (the same under either matrix convention)
    const Part& p = mf.layout.parts[0];
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    const uint16_t* ix = reinterpret_cast<const uint16_t*>(b->indices.data());
    for (uint32_t k = 0; k < p.key.indexCount; ++k) {
        const uint32_t v = ix[p.key.firstIndex + k] + static_cast<uint32_t>(p.key.baseVertex);
        float pos[3];
        std::memcpy(pos, b->vertices.data() + static_cast<size_t>(v) * 32, 12);
        for (int c = 0; c < 3; ++c) {
            lo[c] = std::min(lo[c], pos[c]);
            hi[c] = std::max(hi[c], pos[c]);
        }
    }
    const float e = std::max(hi[0] - lo[0], hi[1] - lo[1]) * 0.6f + 1e-3f, ez = (hi[2] - lo[2]) * 2.0f + 1e-3f;
    ModelScene s;
    s.targets.push_back(TapTarget{64, 64, 1, 28, 64, 64, 1});
    s.rows.resize(4 + 22);
    s.rows[0] = {1, 0, 0, 0};
    s.rows[1] = {0, 1, 0, 0};
    s.rows[2] = {0, 0, 1, 0};
    s.rows[3] = {-(lo[0] + hi[0]) / 2, -(lo[1] + hi[1]) / 2, -lo[2] + 1e-3f, 1};
    s.rows[4] = {1 / e, 0, 0, 0};       // cb1 rows 0-3: the view-projection
    s.rows[5] = {0, 1 / e, 0, 0};
    s.rows[6] = {0, 0, 1 / ez, 0};
    s.rows[7] = {0, 0, 0, 1};
    s.consts.push_back(TapConst{4, 22, 1, 1});
    DrawItem it;
    it.file = static_cast<uint32_t>(fi);
    it.part = 0;
    it.material = Material::Blocks;
    it.instanceCount = 1;
    it.instRow = 0;
    it.vsCb1 = it.psCb1 = 0;
    s.items.push_back(it);
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = td.Height = 64;
    td.MipLevels = td.ArraySize = 1;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    ID3D11Texture2D *rt = nullptr, *idt = nullptr, *dt = nullptr;
    ID3D11RenderTargetView *rtv = nullptr, *idv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    CHECK(SUCCEEDED(d.dev->CreateTexture2D(&td, nullptr, &rt)) && SUCCEEDED(d.dev->CreateRenderTargetView(rt, nullptr, &rtv)));
    td.Format = DXGI_FORMAT_R32_UINT;
    CHECK(SUCCEEDED(d.dev->CreateTexture2D(&td, nullptr, &idt)) && SUCCEEDED(d.dev->CreateRenderTargetView(idt, nullptr, &idv)));
    td.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    CHECK(SUCCEEDED(d.dev->CreateTexture2D(&td, nullptr, &dt)) && SUCCEEDED(d.dev->CreateDepthStencilView(dt, nullptr, &dsv)));
    const float clear[4] = {0, 0, 0, 0};
    d.ctx->ClearRenderTargetView(rtv, clear);
    d.ctx->ClearRenderTargetView(idv, clear);
    d.ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
    ViewInput v;
    v.rtv = rtv;
    v.dsv = dsv;
    v.idRtv = idv;
    v.viewport = {0, 0, 64, 64, 0, 1};
    EnvInput env;
    DrawReport rep;
    CHECK_OK(r.draw(d.ctx, s, v, env, &rep));
    CHECK_EQ(rep.items, 1u);
    CHECK_EQ(rep.drawn, 1u);
    CHECK_EQ(rep.skipped, 0u);
    // the id target holds 1 (item 0) where the block was drawn
    td.Format = DXGI_FORMAT_R32_UINT;
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* st = nullptr;
    CHECK(SUCCEEDED(d.dev->CreateTexture2D(&td, nullptr, &st)));
    uint32_t covered = 0, other = 0;
    if (st) {
        d.ctx->CopyResource(st, idt);
        D3D11_MAPPED_SUBRESOURCE ms;
        if (SUCCEEDED(d.ctx->Map(st, 0, D3D11_MAP_READ, 0, &ms))) {
            for (uint32_t y = 0; y < 64; ++y)
                for (uint32_t x = 0; x < 64; ++x) {
                    const uint32_t id = static_cast<const uint32_t*>(ms.pData)[y * ms.RowPitch / 4 + x];
                    covered += id == 1;
                    other += id != 0 && id != 1;
                }
            d.ctx->Unmap(st, 0);
        }
        st->Release();
    }
    CHECK(covered > 100);
    CHECK_EQ(other, 0u);
    // an item of another target is not drawn; a scene item out of range is skipped and named
    s.items[0].target = 1;
    CHECK_OK(r.draw(d.ctx, s, v, env, &rep));
    CHECK_EQ(rep.items, 0u);
    s.items[0].target = 0;
    s.items[0].part = 100000;
    CHECK_OK(r.draw(d.ctx, s, v, env, &rep));
    CHECK_EQ(rep.skipped, 1u);
    ViewInput none;
    CHECK_ERR(r.draw(d.ctx, s, none, env, &rep), Err::BadLayout);   // no render target
    for (IUnknown* u : std::initializer_list<IUnknown*>{rtv, idv, dsv, rt, idt, dt})
        if (u) u->Release();
}

C5R_TEST_MAIN
