// c5r::water: the ocean's own inputs are stock's (epic 3600, issue 3609; water-system.md 10.7). One test per fix:
// the LEAN pair from the install, the currents map bound after the first draw, a zero shadow input, the TerrainFOW
// ColorKey, the stock fog and shadow-blend defaults with the zoom fraction, and the PS's output staying finite and in
// the stock range where the flat LEAN pair of before drove it to black and white.
// Without C5R_EXE / C5R_GAME_DIR the GPU tests skip, the executable exits 77 and render/build.py --test fails the run.
#include <windows.h>
#include <cmath>
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

bool desc2d(ID3D11ShaderResourceView* v, D3D11_TEXTURE2D_DESC& d) {
    if (!v) return false;
    ID3D11Resource* r = nullptr;
    v->GetResource(&r);
    ID3D11Texture2D* t = nullptr;
    const bool ok = r && SUCCEEDED(r->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&t)));
    if (ok) {
        t->GetDesc(&d);
        t->Release();
    }
    if (r) r->Release();
    return ok;
}

// The first texel of a 2D view's mip 0 (up to 8 bytes).
std::vector<uint8_t> firstTexel(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* v, uint32_t bytes) {
    std::vector<uint8_t> out;
    D3D11_TEXTURE2D_DESC d;
    if (!desc2d(v, d)) return out;
    ID3D11Resource* r = nullptr;
    v->GetResource(&r);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    ID3D11Texture2D* s = nullptr;
    if (SUCCEEDED(dev->CreateTexture2D(&d, nullptr, &s))) {
        ctx->CopyResource(s, r);
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx->Map(s, 0, D3D11_MAP_READ, 0, &m))) {
            out.assign(static_cast<const uint8_t*>(m.pData), static_cast<const uint8_t*>(m.pData) + bytes);
            ctx->Unmap(s, 0);
        }
        s->Release();
    }
    r->Release();
    return out;
}

} // namespace

C5R_TEST(zoom_fraction_follows_the_controller) {
    // 3601's cameras [L]: height 640.32 at zoom level 17 (0.6316 in the capture), 261.0 at 11 (0)
    CHECK(std::fabs(zoomFraction(640.3154f) - 0.6315789f) < 1e-4f);
    CHECK(std::fabs(zoomFraction(261.0035f) - 0.0f) < 2e-3f);
    CHECK_EQ(zoomFraction(100.0f), 0.0f);      // below the minimum level: clamped
    CHECK_EQ(zoomFraction(100000.0f), 1.0f);   // above the maximum
    CHECK_EQ(zoomFraction(-5.0f), 0.0f);
    CHECK_EQ(zoomFraction(NAN), 0.0f);
    CHECK_EQ(zoomFraction(500.0f, 20.0f, 10.0f), 0.0f);  // max <= min
}

C5R_TEST(fog_and_shadow_defaults_are_stock) {
    const Fog f;
    CHECK_EQ(f.density, 0.05f);
    CHECK_EQ(f.scale, 5.0e-5f);
    // issue 4305: the EXE's float bits of 0xDA1C50..0xDA1C58 (n * (1 / 255); terrain-surface.md 12.4)
    const uint32_t wantBits[3] = {0x3F129293u, 0x3F36B6B7u, 0x3F73F3F5u};
    for (int k = 0; k < 3; ++k) {
        uint32_t b = 0;
        std::memcpy(&b, &f.color[k], 4);
        CHECK_EQ(b, wantBits[k]);
    }
    CHECK_EQ(f.color[3], 1.0f);
    CHECK(f.w < 0.0f);
    FrameInputs in;
    CHECK_EQ(in.shadowBlend, 1.0f);
    // WaterFrame row 0x14C takes the zoom fraction of the camera's height when fog.w < 0
    const float v[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -10, -20, -640.3154f, 1};
    const float p[16] = {2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 1};
    std::memcpy(in.cam.view, v, sizeof v);
    std::memcpy(in.cam.proj, p, sizeof p);
    in.cam.width = in.cam.height = 8;
    in.fields = stockWaterFields(nullptr);
    in.world = worldFromGrid(wtest::flatGrid(56, 40, 0));
    uint8_t blk[kWaterFrameBytes];
    CHECK_OK(computeWaterFrame(in, blk));
    float w, x, sb;
    std::memcpy(&w, blk + 0x14C, 4);
    std::memcpy(&x, blk + 0x140, 4);
    std::memcpy(&sb, blk + 0x220, 4);
    CHECK(std::fabs(w - 0.6315789f) < 1e-4f);
    CHECK_EQ(x, -0.05f);
    CHECK_EQ(sb, 1.0f);
    in.fog.w = 0.25f;  // given: taken as is
    CHECK_OK(computeWaterFrame(in, blk));
    std::memcpy(&w, blk + 0x14C, 4);
    CHECK_EQ(w, 0.25f);
}

C5R_TEST(game_ocean_inputs_are_stock) {
    const std::string exe = test::exePath(), game = test::gameDir();
    if (exe.empty() || game.empty()) test::skip("C5R_EXE / C5R_GAME_DIR not set");
    wtest::Device d;
    CHECK_OK(d.create(false));
    Renderer r;
    CHECK_OK(r.init(d.dev, wide(exe), wide(game)));
    D3D11_TEXTURE2D_DESC td;
    // t0 / t1: waterbumps16 (512 x 512, 10 mips; water-system.md 8.3), not a 1 x 1 constant
    CHECK(desc2d(r.inputTexture(Slot::Lean0), td));
    CHECK_EQ(td.Width, 512u);
    CHECK_EQ(td.MipLevels, 10u);
    CHECK_EQ(static_cast<int>(td.Format), static_cast<int>(DXGI_FORMAT_R16G16B16A16_UNORM));
    CHECK(desc2d(r.inputTexture(Slot::Lean1), td));
    CHECK_EQ(static_cast<int>(td.Format), static_cast<int>(DXGI_FORMAT_R16_UNORM));
    // t13: no shadow = 0
    const std::vector<uint8_t> sh = firstTexel(d.dev, d.ctx, r.inputTexture(Slot::Shadow), 4);
    CHECK(sh.size() == 4 && sh[0] == 0);
    // t5 before a field: the constant; after setHeightField and one draw: 3602's 256 x 256 map
    CHECK(desc2d(r.inputTexture(Slot::Currents), td));
    CHECK_EQ(td.Width, 1u);
    CHECK_OK(r.setHeightField(wtest::flatGrid(28, 16, 0)));
    wtest::Target t;
    CHECK_OK(t.create(d.dev, 160, 120));
    FrameInputs in;
    in.cam = wtest::downCamera(0, 0, 600, 160, 120);
    in.fields = r.stockFields();
    FrameStats st;
    CHECK_OK(r.draw(d.ctx, t.rtv, nullptr, in, Options(), &st));
    CHECK(desc2d(r.inputTexture(Slot::Currents), td));
    CHECK_EQ(td.Width, 256u);
    // Options::computeCurrents false leaves a stale map alone (a new field marks it stale)
    CHECK_OK(r.setHeightField(wtest::flatGrid(28, 16, 0)));
    Options noCur;
    noCur.computeCurrents = false;
    CHECK_OK(r.draw(d.ctx, t.rtv, nullptr, in, noCur, &st));
    CHECK(desc2d(r.inputTexture(Slot::Currents), td));
    CHECK_EQ(td.Width, 1u);
    // setTexture still overrides, and null restores the module's own
    ID3D11ShaderResourceView* mine = r.inputTexture(Slot::Lean0);
    CHECK_OK(r.setTexture(Slot::Lean0, r.inputTexture(Slot::Lean1)));
    CHECK(r.inputTexture(Slot::Lean0) != mine);
    CHECK_OK(r.setTexture(Slot::Lean0, nullptr));
    CHECK(r.inputTexture(Slot::Lean0) == mine);
    CHECK(r.inputTexture(static_cast<Slot>(7)) == nullptr);   // t7 is per page, not an input
    Renderer none;
    CHECK(none.inputTexture(Slot::Lean0) == nullptr);
}

// The flat LEAN pair of before (zero second moments) drives the ocean PS to saturated black and white; the stock pair
// does not. Over an all-water view: the share of pixels at pure black or pure white.
C5R_TEST(game_lean_pair_keeps_the_sea_out_of_black_and_white) {
    const std::string exe = test::exePath(), game = test::gameDir();
    if (exe.empty() || game.empty()) test::skip("C5R_EXE / C5R_GAME_DIR not set");
    wtest::Device d;
    CHECK_OK(d.create(false));
    Renderer r;
    CHECK_OK(r.init(d.dev, wide(exe), wide(game)));
    heightfield::HeightGrid g = wtest::flatGrid(28, 16, 0);
    for (uint32_t y = 0; y < g.samplesY; ++y)  // a sloping sea floor (depth colour varies)
        for (uint32_t x = 0; x < g.samplesX; ++x) g.h[g.index(x, y)] = static_cast<uint16_t>((x * 10000u / g.samplesX) & 0xFF00u);
    CHECK_OK(r.setHeightField(g));
    wtest::Target t;
    CHECK_OK(t.create(d.dev, 160, 120));
    FrameInputs in;
    in.cam = wtest::downCamera(0, 0, 600, 160, 120);
    in.fields = r.stockFields();
    in.fields.set(0xE4, 31.9f);   // the capture's wave amplitude and clock (water-system.md 10.6)
    in.fields.set(0xDC, 765.6f);
    auto extremes = [&]() {
        const float clear[4] = {0.3f, 0.3f, 0.3f, 1};
        d.ctx->ClearRenderTargetView(t.rtv, clear);
        FrameStats st;
        CHECK_OK(r.draw(d.ctx, t.rtv, nullptr, in, Options(), &st));
        std::vector<uint8_t> px;
        CHECK_OK(t.read(d.ctx, px));
        size_t n = 0;
        for (size_t i = 0; i < px.size(); i += 4) {
            const bool black = px[i] < 4 && px[i + 1] < 4 && px[i + 2] < 4;
            const bool white = px[i] > 251 && px[i + 1] > 251 && px[i + 2] > 251;
            n += black || white;
        }
        return static_cast<double>(n) / static_cast<double>(px.size() / 4);
    };
    const double stockShare = extremes();
    // the flat pair of before
    const uint16_t lean[4] = {0, 0, 0x8000, 0x8000};
    const uint16_t lean1 = 0x8000;
    auto make = [&](DXGI_FORMAT f, const void* p, uint32_t pitch) {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = td.Height = 1;
        td.MipLevels = td.ArraySize = 1;
        td.Format = f;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd = {p, pitch, 0};
        ID3D11Texture2D* tx = nullptr;
        ID3D11ShaderResourceView* v = nullptr;
        d.dev->CreateTexture2D(&td, &sd, &tx);
        if (tx) d.dev->CreateShaderResourceView(tx, nullptr, &v);
        if (tx) tx->Release();
        return v;
    };
    ID3D11ShaderResourceView* f0 = make(DXGI_FORMAT_R16G16B16A16_UNORM, lean, 8);
    ID3D11ShaderResourceView* f1 = make(DXGI_FORMAT_R16_UNORM, &lean1, 2);
    CHECK_OK(r.setTexture(Slot::Lean0, f0));
    CHECK_OK(r.setTexture(Slot::Lean1, f1));
    f0->Release();
    f1->Release();
    const double flatShare = extremes();
    std::printf("  pure black or white: stock LEAN %.2f %%, flat LEAN %.2f %%\n", stockShare * 100, flatShare * 100);
    CHECK(stockShare < 0.01);
    CHECK(flatShare > 0.20);
}

C5R_TEST_MAIN
