// c5r::water: the FOW mask t10 reaches the ocean and the river programs (epic 0168, issue 3953;
// research/05-graphics-composition/fog-of-war.md 3.4 and 7.4). The scene (task 3954) binds the server's mask through
// Renderer::setTexture(Slot::FowMask); the EXE's ocean and river PSs read t10 at the overlay coordinate (.x and .z):
// discard where x == 0, lerp to a flat 0.4 grey by x, then to the TerrainFOW LUT t11 by x * (1 - z). Checked with
// solid masks, each system alone, on a windowless device with the EXE's programs and the install's art (C5R_EXE,
// C5R_GAME_DIR; without them the test skips, the executable exits 77 and render/build.py --test fails the run):
//   - the module's own t10 (white) and a "visible" mask (1, 0, 1, 1) give the same picture, byte for byte;
//   - "unexplored" (0, 1, 0, 1): nothing of the system is drawn (the discard);
//   - "seen" (1, 0, 0, 1): drawn, and different from visible (the TerrainFOW Generic LUT);
//   - setTexture(FowMask, nullptr) restores the module's own: the first picture again.
// Own code; synthetic inputs only.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "c5r/water/water.h"
#include "testkit.h"
#include "water_util.h"

using namespace c5r;
using namespace c5r::water;

namespace {

std::wstring wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }

// A river quad x -50..50, y -10..10 at z (counter-clockwise seen from above), the vertex layout of river_test.cpp.
Mesh riverQuad(float z) {
    Mesh m;
    m.stride = 32;
    const float pos[4][2] = {{-50, -10}, {50, -10}, {50, 10}, {-50, 10}};
    for (int v = 0; v < 4; ++v) {
        float vtx[8] = {pos[v][0], pos[v][1], z, pos[v][0] / 64.0f, pos[v][1] / 64.0f, v * 0.25f, 0.5f, 0};
        const uint8_t* b = reinterpret_cast<const uint8_t*>(vtx);
        m.vb.insert(m.vb.end(), b, b + 32);
    }
    const uint32_t ix[6] = {0, 1, 2, 0, 2, 3};
    m.indexFormat = DXGI_FORMAT_R32_UINT;
    for (int i = 0; i < 6; ++i) {
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&ix[i]);
        m.ib.insert(m.ib.end(), b, b + 4);
    }
    Mesh::Draw d;
    d.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    d.indexCount = 6;
    d.pass = 4;
    m.draws.push_back(d);
    return m;
}

ID3D11ShaderResourceView* solid(ID3D11Device* dev, uint32_t rgba) {
    std::vector<uint32_t> px(16, rgba);
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = d.Height = 4;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA s = {px.data(), 16, 0};
    ID3D11Texture2D* t = nullptr;
    ID3D11ShaderResourceView* v = nullptr;
    if (FAILED(dev->CreateTexture2D(&d, &s, &t))) return nullptr;
    dev->CreateShaderResourceView(t, nullptr, &v);
    t->Release();
    return v;
}

size_t drawn(const std::vector<uint8_t>& px) {
    size_t n = 0;
    for (size_t i = 0; i + 3 < px.size(); i += 4) n += !(px[i] == 255 && px[i + 1] == 0 && px[i + 2] == 255);
    return n;
}
size_t differ(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    size_t n = 0;
    for (size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4)
        n += a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2];
    return n;
}

} // namespace

C5R_TEST(game_fow_mask_reaches_the_ocean_and_the_river) {
    const std::string exe = test::exePath();
    const std::string game = test::gameDir();
    wtest::Device d;
    CHECK_OK(d.create(false));
    Renderer r;
    CHECK_OK(r.init(d.dev, wide(exe), wide(game)));
    CHECK_OK(r.setHeightField(wtest::flatGrid(28, 16, 0)));   // sea floor 0: deep water everywhere
    CHECK_OK(r.setMesh(System::River, riverQuad(10.117f)));
    const uint32_t W = 160, H = 120;
    wtest::Target t;
    CHECK_OK(t.create(d.dev, W, H));
    FrameInputs in;
    in.cam = wtest::downCamera(0, 0, 600, W, H);
    in.fields = r.stockFields();
    ID3D11ShaderResourceView* vis = solid(d.dev, 0xFFFF00FFu);
    ID3D11ShaderResourceView* seen = solid(d.dev, 0xFF0000FFu);
    ID3D11ShaderResourceView* unex = solid(d.dev, 0xFF00FF00u);
    CHECK(vis && seen && unex);
    if (!vis || !seen || !unex) return;
    ID3D11ShaderResourceView* own = r.inputTexture(Slot::FowMask);
    CHECK(own != nullptr);
    auto shot = [&](const Options& o, std::vector<uint8_t>& px, FrameStats& st) {
        const float magenta[4] = {1, 0, 1, 1};
        d.ctx->ClearRenderTargetView(t.rtv, magenta);
        CHECK_OK(r.draw(d.ctx, t.rtv, nullptr, in, o, &st));
        CHECK_OK(t.read(d.ctx, px));
    };
    Options ocean;
    ocean.oceanBlend = false;   // the PS's own output
    ocean.rivers = false;
    ocean.waves = false;
    Options river;
    river.ocean = false;
    river.waves = false;
    struct Case { const char* name; Options o; bool isRiver; };
    const Case cases[2] = {{"ocean", ocean, false}, {"river", river, true}};
    for (const Case& c : cases) {
        std::vector<uint8_t> base, pv, ps, pu, back;
        FrameStats st;
        CHECK_OK(r.setTexture(Slot::FowMask, nullptr));
        shot(c.o, base, st);
        CHECK(c.isRiver ? st.riverDraws == 1 : st.oceanDraws >= 1);
        CHECK_OK(r.setTexture(Slot::FowMask, vis));
        CHECK(r.inputTexture(Slot::FowMask) == vis);
        shot(c.o, pv, st);
        CHECK_OK(r.setTexture(Slot::FowMask, seen));
        shot(c.o, ps, st);
        CHECK_OK(r.setTexture(Slot::FowMask, unex));
        shot(c.o, pu, st);
        CHECK(c.isRiver ? st.riverDraws == 1 : st.oceanDraws >= 1);   // drawn, and discarded by the PS
        CHECK_OK(r.setTexture(Slot::FowMask, nullptr));
        CHECK(r.inputTexture(Slot::FowMask) == own);
        shot(c.o, back, st);
        std::printf("  %s: drawn px own t10 %zu, visible %zu (differ from own %zu), seen %zu (differ from visible %zu), "
                    "unexplored %zu; own again differs %zu\n", c.name, drawn(base), drawn(pv), differ(base, pv), drawn(ps),
                    differ(pv, ps), drawn(pu), differ(base, back));
        CHECK(drawn(base) > (c.isRiver ? 200u : W * H - 1));
        CHECK_EQ(differ(base, pv), size_t(0));
        CHECK_EQ(drawn(ps), drawn(base));
        CHECK(differ(pv, ps) > drawn(base) / 2);
        CHECK_EQ(drawn(pu), size_t(0));
        CHECK_EQ(differ(base, back), size_t(0));
    }
    vis->Release();
    seen->Release();
    unex->Release();
}

C5R_TEST_MAIN
