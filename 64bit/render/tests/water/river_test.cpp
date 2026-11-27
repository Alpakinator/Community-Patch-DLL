// c5r::water rivers (epic 3600, issue 3605): the river art from the install, RiverRenderer's checks with a negative test
// each, and the EXE's river passes over a synthetic river quad (cull, depth write, pass choice) on a windowless device.
// Without C5R_EXE / C5R_GAME_DIR those tests skip, the executable exits 77 and render/build.py --test fails the run.
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "c5r/bake/post.h"
#include "c5r/water/water.h"
#include "testkit.h"
#include "water_util.h"

using namespace c5r;
using namespace c5r::water;

namespace {

std::wstring wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }

// A river quad x -50..50, y -10..10 at z (counter-clockwise seen from above: front with FrontCounterClockwise).
Mesh riverQuad(float z, bool reversed = false, DXGI_FORMAT indexFormat = DXGI_FORMAT_R32_UINT) {
    Mesh m;
    m.stride = 32;
    const float pos[4][2] = {{-50, -10}, {50, -10}, {50, 10}, {-50, 10}};
    for (int v = 0; v < 4; ++v) {
        float vtx[8] = {pos[v][0], pos[v][1], z, pos[v][0] / 64.0f, pos[v][1] / 64.0f, v * 0.25f, 0.5f, 0};
        const uint8_t* b = reinterpret_cast<const uint8_t*>(vtx);
        m.vb.insert(m.vb.end(), b, b + 32);
    }
    const uint32_t fwd[6] = {0, 1, 2, 0, 2, 3}, rev[6] = {0, 2, 1, 0, 3, 2};
    const uint32_t* ix = reversed ? rev : fwd;
    m.indexFormat = indexFormat;
    for (int i = 0; i < 6; ++i) {
        if (indexFormat == DXGI_FORMAT_R32_UINT) {
            const uint8_t* b = reinterpret_cast<const uint8_t*>(&ix[i]);
            m.ib.insert(m.ib.end(), b, b + 4);
        } else {
            const uint16_t s = static_cast<uint16_t>(ix[i]);
            const uint8_t* b = reinterpret_cast<const uint8_t*>(&s);
            m.ib.insert(m.ib.end(), b, b + 2);
        }
    }
    Mesh::Draw d;
    d.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    d.indexCount = 6;
    d.pass = 4;
    m.draws.push_back(d);
    return m;
}

struct DepthTarget {
    ID3D11Texture2D* tex = nullptr;
    ID3D11Texture2D* staging = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    ~DepthTarget() {
        if (dsv) dsv->Release();
        if (staging) staging->Release();
        if (tex) tex->Release();
    }
    bool create(ID3D11Device* dev, uint32_t w, uint32_t h) {
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = w;
        d.Height = h;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R32_TYPELESS;
        d.SampleDesc.Count = 1;
        d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        if (FAILED(dev->CreateTexture2D(&d, nullptr, &tex))) return false;
        D3D11_DEPTH_STENCIL_VIEW_DESC v = {};
        v.Format = DXGI_FORMAT_D32_FLOAT;
        v.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        if (FAILED(dev->CreateDepthStencilView(tex, &v, &dsv))) return false;
        d.BindFlags = 0;
        d.Usage = D3D11_USAGE_STAGING;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        return SUCCEEDED(dev->CreateTexture2D(&d, nullptr, &staging));
    }
    float at(ID3D11DeviceContext* ctx, uint32_t x, uint32_t y) {
        ctx->CopyResource(staging, tex);
        D3D11_MAPPED_SUBRESOURCE m;
        if (FAILED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) return -1;
        float v;
        std::memcpy(&v, static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch + x * 4, 4);
        ctx->Unmap(staging, 0);
        return v;
    }
};

// A down camera's WaterFrame over the 28 x 16-plot flat field.
std::vector<uint8_t> frameFor(const terrain::Camera& cam) {
    FrameInputs in;
    in.cam = cam;
    in.fields = stockWaterFields(nullptr);
    in.world = worldFromGrid(wtest::flatGrid(28, 16, 0));
    std::vector<uint8_t> f(kWaterFrameBytes);
    computeWaterFrame(in, f.data());
    return f;
}

RiverShared sharedConstants(ID3D11Device* dev, std::vector<ID3D11SamplerState*>& samps, ID3D11ShaderResourceView** white,
                            ID3D11ShaderResourceView** lut) {
    RiverShared sh;
    D3D11_SAMPLER_DESC d = {};
    d.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    d.AddressU = d.AddressV = d.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    d.MaxAnisotropy = 1;
    d.MaxLOD = D3D11_FLOAT32_MAX;
    for (int i = 0; i < 13; ++i) {
        ID3D11SamplerState* s = nullptr;
        dev->CreateSamplerState(&d, &s);
        samps.push_back(s);
        sh.samplers[i] = s;
    }
    const uint32_t w = 0xFFFFFFFFu;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = td.Height = 1;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA s = {&w, 4, 0};
    ID3D11Texture2D* t = nullptr;
    dev->CreateTexture2D(&td, &s, &t);
    dev->CreateShaderResourceView(t, nullptr, white);
    t->Release();
    D3D11_TEXTURE3D_DESC vd = {};
    vd.Width = vd.Height = vd.Depth = 1;
    vd.MipLevels = 1;
    vd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    vd.Usage = D3D11_USAGE_IMMUTABLE;
    vd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sv = {&w, 4, 4};
    ID3D11Texture3D* t3 = nullptr;
    dev->CreateTexture3D(&vd, &sv, &t3);
    dev->CreateShaderResourceView(t3, nullptr, lut);
    t3->Release();
    for (uint32_t i : {4u, 8u, 9u, 10u, 13u}) sh.srv[i] = *white;
    sh.srv[11] = *lut;
    return sh;
}

} // namespace

C5R_TEST(river_colorkey_file_names) {
    CHECK_EQ(riverColorKeyFile(0), std::string("Rivers_Generic_ColorKey.tga"));
    CHECK_EQ(riverColorKeyFile(4), std::string("Rivers_Europe_ColorKey.tga"));
    CHECK(riverColorKeyFile(5).empty());
}

C5R_TEST(river_calls_before_init_fail) {
    RiverRenderer r;
    CHECK(!r.hasMesh());
    CHECK_ERR(r.setMesh(riverQuad(10)), Err::NotFound);
    CHECK_ERR(r.setTexture(RiverSlot::Alpha, nullptr), Err::NotFound);
    RiverShared sh;
    uint8_t f[kWaterFrameBytes] = {};
    CHECK_ERR(r.draw(nullptr, nullptr, nullptr, 1, 1, f, sh, -1, 0, nullptr, nullptr), Err::NotFound);
    CHECK_ERR(r.init(nullptr, L"x.exe", L""), Err::BadLayout);
    RiverArt art;
    CHECK(!loadRiverArt(L"C:/no/such/install", art).ok());
}

C5R_TEST(game_river_art_from_the_install) {
    const std::string game = test::gameDir();
    if (game.empty()) test::skip("C5R_GAME_DIR not set");
    RiverArt art;
    CHECK_OK(loadRiverArt(wide(game), art));
    CHECK(art.leanBytes[0].size() == 218506);  // riversmooth16.lean (water-system.md 8.3)
    CHECK(art.leanBytes[1].size() == 218506);  // riverrough_new16.lean
    CHECK(!art.alphaDds.empty() && !art.diffuseDds.empty());
    CHECK(art.colorKeyFromFile[0]);             // Rivers_Generic_ColorKey.tga
    for (int s = 0; s < 5; ++s) CHECK_EQ(art.colorKey[s].size(), bake::kColorKeyTexels * 4);
    // the other styles have no file in this install: the identity volume
    for (int s = 1; s < 5; ++s) {
        CHECK(!art.colorKeyFromFile[s]);
        CHECK_EQ(art.colorKey[s][0], 0);
        CHECK_EQ(art.colorKey[s][((31 * 32 + 31) * 32 + 31) * 4], 255);
    }
}

C5R_TEST(river_mesh_checks) {
    const std::string exe = test::exePath();
    if (exe.empty()) test::skip("C5R_EXE not set");
    wtest::Device d;
    CHECK_OK(d.create(false));
    RiverRenderer r;
    CHECK_OK(r.init(d.dev, wide(exe), L""));
    Mesh m = riverQuad(10);
    m.stride = 16;
    CHECK_ERR(r.setMesh(m), Err::BadLayout);
    m = riverQuad(10);
    m.vb.pop_back();
    CHECK_ERR(r.setMesh(m), Err::BadLayout);  // vertex bytes not whole vertices
    m = riverQuad(10);
    m.indexFormat = 99;
    CHECK_ERR(r.setMesh(m), Err::BadLayout);
    m = riverQuad(10);
    m.ib.pop_back();
    CHECK_ERR(r.setMesh(m), Err::BadLayout);  // index bytes not whole indices
    m = riverQuad(10);
    m.draws.clear();
    CHECK_ERR(r.setMesh(m), Err::BadLayout);
    m = riverQuad(10);
    m.draws[0].topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    CHECK_ERR(r.setMesh(m), Err::BadLayout);
    m = riverQuad(10);
    m.draws[0].pass = 2;
    CHECK_ERR(r.setMesh(m), Err::BadLayout);
    m = riverQuad(10);
    m.draws[0].indexCount = 9;
    CHECK_ERR(r.setMesh(m), Err::BadLayout);  // past the indices
    m = riverQuad(10);
    m.draws[0].indexCount = 4;
    CHECK_ERR(r.setMesh(m), Err::BadLayout);  // not whole triangles
    m = riverQuad(10);
    m.draws[0].baseVertex = 4;
    CHECK_ERR(r.setMesh(m), Err::BadLayout);
    m = riverQuad(10);
    m.ib[4 * 2] = 9;                           // index 2 -> 9: past the 4 vertices
    CHECK_ERR(r.setMesh(m), Err::BadLayout);
    m = riverQuad(10);
    m.ownConstants.assign(10, 0);
    CHECK_ERR(r.setMesh(m), Err::BadLayout);
    CHECK(!r.hasMesh());                       // nothing was taken
    CHECK_OK(r.setMesh(riverQuad(10, false, DXGI_FORMAT_R16_UINT)));
    CHECK(r.hasMesh());
    CHECK_OK(r.setMesh(Mesh()));
    CHECK(!r.hasMesh());
    CHECK_ERR(r.setTexture(static_cast<RiverSlot>(4), nullptr), Err::BadLayout);   // t4 is shared, set on the Renderer
    CHECK_ERR(r.setTexture(static_cast<RiverSlot>(13), nullptr), Err::BadLayout);
    CHECK_OK(r.setTexture(RiverSlot::ColorKey, nullptr));
}

C5R_TEST(river_draw_with_the_exe_programs) {
    const std::string exe = test::exePath();
    if (exe.empty()) test::skip("C5R_EXE not set");
    wtest::Device d;
    CHECK_OK(d.create(false));
    RiverRenderer r;
    CHECK_OK(r.init(d.dev, wide(exe), L""));
    const uint32_t W = 160, H = 120;
    wtest::Target t;
    CHECK_OK(t.create(d.dev, W, H));
    DepthTarget dt;
    CHECK(dt.create(d.dev, W, H));
    std::vector<ID3D11SamplerState*> samps;
    ID3D11ShaderResourceView* white = nullptr;
    ID3D11ShaderResourceView* lut = nullptr;
    const RiverShared sh = sharedConstants(d.dev, samps, &white, &lut);
    const terrain::Camera cam = wtest::downCamera(0, 0, 600, W, H);
    const std::vector<uint8_t> f = frameFor(cam);
    uint32_t draws = 0;
    int used = -1;
    CHECK_ERR(r.draw(d.ctx, t.rtv, dt.dsv, W, H, f.data(), sh, -1, 0, &draws, &used), Err::NotFound);  // no mesh
    CHECK_OK(r.setMesh(riverQuad(10.117f)));
    CHECK_ERR(r.draw(d.ctx, t.rtv, dt.dsv, W, H, f.data(), sh, 2, 0, &draws, &used), Err::BadLayout);   // pass 2
    CHECK_ERR(r.draw(d.ctx, t.rtv, dt.dsv, W, H, f.data(), sh, -1, 5, &draws, &used), Err::BadLayout);  // style 5
    CHECK_ERR(r.draw(d.ctx, t.rtv, dt.dsv, W, H, nullptr, sh, -1, 0, &draws, &used), Err::BadLayout);   // no frame
    CHECK_ERR(r.draw(d.ctx, t.rtv, dt.dsv, 0, H, f.data(), sh, -1, 0, &draws, &used), Err::BadLayout);
    RiverShared noSamp = sh;
    noSamp.samplers[5] = nullptr;
    CHECK_ERR(r.draw(d.ctx, t.rtv, dt.dsv, W, H, f.data(), noSamp, -1, 0, &draws, &used), Err::BadLayout);
    for (int pass : {-1, 1, 4}) {
        const float magenta[4] = {1, 0, 1, 1};
        d.ctx->ClearRenderTargetView(t.rtv, magenta);
        d.ctx->ClearDepthStencilView(dt.dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
        CHECK_OK(r.draw(d.ctx, t.rtv, dt.dsv, W, H, f.data(), sh, pass, 0, &draws, &used));
        CHECK_EQ(draws, 1u);
        CHECK_EQ(used, pass < 0 ? 4 : pass);
        std::vector<uint8_t> px;
        CHECK_OK(t.read(d.ctx, px));
        auto changed = [&](uint32_t x, uint32_t y) {
            const uint8_t* q = px.data() + (static_cast<size_t>(y) * W + x) * 4;
            return !(q[0] == 255 && q[1] == 0 && q[2] == 255);
        };
        // the quad covers the middle (x +-50, y +-10 at 590 units: about +-38 x +-8 pixels); the corners stay
        CHECK(changed(W / 2, H / 2));
        CHECK(!changed(5, 5));
        CHECK(!changed(W - 5, H - 5));
        // the river writes depth (stock: depth write ALL)
        const float z = dt.at(d.ctx, W / 2, H / 2);
        CHECK(z > 0.0f && z < 1.0f);
        CHECK_EQ(dt.at(d.ctx, 5, 5), 1.0f);
    }
    // the stock rasterizer culls back faces (FrontCounterClockwise): the reversed quad is not drawn
    CHECK_OK(r.setMesh(riverQuad(10.117f, true)));
    const float magenta[4] = {1, 0, 1, 1};
    d.ctx->ClearRenderTargetView(t.rtv, magenta);
    CHECK_OK(r.draw(d.ctx, t.rtv, nullptr, W, H, f.data(), sh, -1, 0, &draws, &used));
    std::vector<uint8_t> px;
    CHECK_OK(t.read(d.ctx, px));
    const uint8_t* c = px.data() + (static_cast<size_t>(H / 2) * W + W / 2) * 4;
    CHECK(c[0] == 255 && c[1] == 0 && c[2] == 255);
    for (auto* s : samps) s->Release();
    white->Release();
    lut->Release();
}

C5R_TEST(renderer_draws_rivers_after_the_ocean) {
    const std::string exe = test::exePath();
    const std::string game = test::gameDir();
    if (exe.empty() || game.empty()) test::skip("C5R_EXE / C5R_GAME_DIR not set");
    wtest::Device d;
    CHECK_OK(d.create(false));
    Renderer r;
    CHECK_OK(r.init(d.dev, wide(exe), wide(game)));
    CHECK_OK(r.setHeightField(wtest::flatGrid(28, 16, 0)));
    wtest::Target t;
    CHECK_OK(t.create(d.dev, 160, 120));
    FrameInputs in;
    in.cam = wtest::downCamera(0, 0, 600, 160, 120);
    in.fields = r.stockFields();
    FrameStats st;
    Options o;
    CHECK_OK(r.draw(d.ctx, t.rtv, nullptr, in, o, &st));
    CHECK_EQ(st.riverDraws, 0u);
    CHECK(st.systemsMissing & (1u << static_cast<uint32_t>(System::River)));
    CHECK_OK(r.setMesh(System::River, riverQuad(10.117f)));
    CHECK_OK(r.draw(d.ctx, t.rtv, nullptr, in, o, &st));
    CHECK_EQ(st.riverDraws, 1u);
    CHECK_EQ(st.riverPassUsed, 4);
    CHECK(!(st.systemsMissing & (1u << static_cast<uint32_t>(System::River))));
    o.riverPass = 1;
    CHECK_OK(r.draw(d.ctx, t.rtv, nullptr, in, o, &st));
    CHECK_EQ(st.riverPassUsed, 1);
    o.riverPass = 2;
    CHECK_ERR(r.draw(d.ctx, t.rtv, nullptr, in, o, &st), Err::BadLayout);
    o.riverPass = -1;
    o.riverStyle = 5;
    CHECK_ERR(r.draw(d.ctx, t.rtv, nullptr, in, o, &st), Err::BadLayout);
    o.riverStyle = 0;
    o.rivers = false;
    CHECK_OK(r.draw(d.ctx, t.rtv, nullptr, in, o, &st));
    CHECK_EQ(st.riverDraws, 0u);
    CHECK_ERR(r.setRiverTexture(static_cast<RiverSlot>(9), nullptr), Err::BadLayout);
    CHECK_OK(r.setRiverTexture(RiverSlot::Height, nullptr));
    Mesh bad = riverQuad(10);
    bad.stride = 24;
    CHECK_ERR(r.setMesh(System::River, bad), Err::BadLayout);
}

C5R_TEST_MAIN
