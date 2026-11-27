// c5r::fow::MaskRenderer (task 3952): the FOW mask with the EXE's FOWDrawMask programs against fog-of-war.md 2.2, 3.3
// and 7.1. Needs C5R_EXE (the programs are read from the player's EXE, D85); without it every GPU test SKIPs and
// render/build.py --test fails the run. Device: hardware, WARP when there is none (no window).
#include <c5r/fow/fow.h>

#include <windows.h>
#include <d3d11.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "testkit.h"
#include "testutil.h"

using namespace c5r;
using namespace c5r::fow;

namespace {

struct Gpu {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    Gpu()
    {
        const D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev,
                                     nullptr, &ctx)))
            D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
    }
    ~Gpu()
    {
        if (ctx) ctx->Release();
        if (dev) dev->Release();
    }
};

std::wstring wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }

std::wstring exeOrSkip()
{
    const std::string e = test::exePath();   // skips when C5R_EXE is unset or missing
    return wide(e);
}

// OverlayProj of a world window [x0, x1] x [y0, y1] (overlay-system.md 4: diag(sx, sy, -0.5, 1), row 3 (tx, ty, 0.5, 1))
void windowProj(float x0, float x1, float y0, float y1, float op[16])
{
    std::memset(op, 0, 16 * sizeof(float));
    op[0] = 2.0f / (x1 - x0);
    op[5] = 2.0f / (y1 - y0);
    op[10] = -0.5f;
    op[12] = -(x0 + x1) / (x1 - x0);
    op[13] = -(y0 + y1) / (y1 - y0);
    op[14] = 0.5f;
    op[15] = 1.0f;
}

// The plot centre in world with nominalPlacement (fog-of-war.md 7.1).
void centre(const MapInput& m, int gx, int gy, float& x, float& y)
{
    const float hx = static_cast<float>(gx - (gy >> 1)), hy = static_cast<float>(gy);
    x = (hx * m.hexToWorld[0] + hy * m.hexToWorld[4] + m.hexToWorld[12]) * m.gridScale[0] - m.gridOrigin[0];
    y = (hx * m.hexToWorld[1] + hy * m.hexToWorld[5] + m.hexToWorld[13]) * m.gridScale[1] - m.gridOrigin[1];
}

struct Image {
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> px;   // RGBA8 rows
    const uint8_t* at(int x, int y) const
    {
        static const uint8_t none[4] = {77, 77, 77, 77};
        if (x < 0 || y < 0 || x >= static_cast<int>(w) || y >= static_cast<int>(h)) return none;
        return &px[(static_cast<size_t>(y) * w + static_cast<size_t>(x)) * 4];
    }
};

bool readMip(Gpu& g, ID3D11ShaderResourceView* srv, uint32_t mip, Image& out)
{
    ID3D11Resource* r = nullptr;
    srv->GetResource(&r);
    ID3D11Texture2D* t = static_cast<ID3D11Texture2D*>(r);
    D3D11_TEXTURE2D_DESC d;
    t->GetDesc(&d);
    D3D11_TEXTURE2D_DESC s = d;
    s.Width = std::max(1u, d.Width >> mip);
    s.Height = std::max(1u, d.Height >> mip);
    s.MipLevels = 1;
    s.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    s.Usage = D3D11_USAGE_STAGING;
    s.BindFlags = 0;
    s.MiscFlags = 0;
    s.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* st = nullptr;
    bool ok = SUCCEEDED(g.dev->CreateTexture2D(&s, nullptr, &st));
    if (ok) {
        g.ctx->CopySubresourceRegion(st, 0, 0, 0, 0, t, mip, nullptr);
        D3D11_MAPPED_SUBRESOURCE ms;
        ok = SUCCEEDED(g.ctx->Map(st, 0, D3D11_MAP_READ, 0, &ms));
        if (ok) {
            out.w = s.Width;
            out.h = s.Height;
            out.px.resize(static_cast<size_t>(s.Width) * s.Height * 4);
            for (uint32_t y = 0; y < s.Height; ++y)
                std::memcpy(&out.px[static_cast<size_t>(y) * s.Width * 4],
                            static_cast<const uint8_t*>(ms.pData) + static_cast<size_t>(y) * ms.RowPitch, s.Width * 4);
            g.ctx->Unmap(st, 0);
        }
        st->Release();
    }
    r->Release();
    return ok;
}

// Texel of a world point under op (fog-of-war.md 2.1: u = 0.5 NDC.x + 0.5, v = 0.5 - 0.5 NDC.y).
const uint8_t* texel(const Image& im, const float op[16], float x, float y)
{
    const float nx = x * op[0] + y * op[4] + op[12], ny = x * op[1] + y * op[5] + op[13];
    const float u = 0.5f * nx + 0.5f, v = 0.5f - 0.5f * ny;
    return im.at(static_cast<int>(std::floor(u * im.w)), static_cast<int>(std::floor(v * im.h)));
}

bool eq4(const uint8_t* p, int r, int g, int b, int a) { return p[0] == r && p[1] == g && p[2] == b && p[3] == a; }

std::string px(const uint8_t* p)
{
    char b[48];
    std::snprintf(b, sizeof b, "(%d, %d, %d, %d)", p[0], p[1], p[2], p[3]);
    return b;
}

#define CHECK_PX(p, r, g, b, a) do { const uint8_t* p_ = (p); if (!eq4(p_, r, g, b, a)) \
    test::fail(__FILE__, __LINE__, "texel " + px(p_) + " want (" #r ", " #g ", " #b ", " #a ")"); } while (0)

// A W x H map, every plot `fill`, nominal placement; one view whose window is the map's world box.
struct Scene {
    std::vector<PlotState> s;
    MapInput map;
    View view;
    MaskInput in;
    Scene(uint32_t w, uint32_t h, PlotState fill, bool wrap = false)
    {
        s.assign(static_cast<size_t>(w) * h, fill);
        map.states = s.data();
        map.width = w;
        map.height = h;
        map.stateCount = s.size();
        map.wrapX = wrap;
        nominalPlacement(map);
        float ax, ay, bx, by;
        centre(map, 0, 0, ax, ay);
        centre(map, static_cast<int>(w) - 1, static_cast<int>(h) - 1, bx, by);
        windowProj(ax - 64, bx + 64, ay - 64, by + 64, view.overlayProj);
        view.x0 = 0;
        view.x1 = static_cast<int32_t>(w);
        view.y0 = 0;
        view.y1 = static_cast<int32_t>(h);
        in.map = map;
        in.views = &view;
        in.numViews = 1;
    }
    void set(int x, int y, PlotState st) { s[static_cast<size_t>(y) * map.width + static_cast<size_t>(x)] = st; }
    void sync() { in.map = map; in.views = &view; }
};

struct Ready {
    Gpu g;
    MaskRenderer r;
    bool ok = false;
    explicit Ready(uint32_t size = 1024)
    {
        const std::wstring exe = exeOrSkip();
        CHECK(g.dev != nullptr);
        if (!g.dev) return;
        Status s = r.init(g.dev, exe, size);
        CHECK_OK(s);
        ok = s.ok();
    }
};

} // namespace

C5R_TEST(init_target_as_stock)
{
    Ready a(1024);
    if (!a.ok) return;
    CHECK_EQ(a.r.size(), 1024u);
    ID3D11Resource* res = nullptr;
    a.r.srv()->GetResource(&res);
    D3D11_TEXTURE2D_DESC d;
    static_cast<ID3D11Texture2D*>(res)->GetDesc(&d);
    res->Release();
    CHECK_EQ(d.Width, 1024u);
    CHECK_EQ(d.Height, 1024u);
    CHECK_EQ(d.MipLevels, 11u);
    CHECK_EQ(static_cast<int>(d.Format), static_cast<int>(DXGI_FORMAT_R8G8B8A8_TYPELESS));
    CHECK((d.BindFlags & (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET)) ==
          (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET));
    CHECK((d.MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS) != 0);
    MaskRenderer m512;
    CHECK_OK(m512.init(a.g.dev, exeOrSkip(), 512));
    CHECK_EQ(m512.size(), 512u);
    m512.srv()->GetResource(&res);
    static_cast<ID3D11Texture2D*>(res)->GetDesc(&d);
    res->Release();
    CHECK_EQ(d.MipLevels, 10u);
}

C5R_TEST(one_plot_per_class_centres)
{
    Ready a;
    if (!a.ok) return;
    Scene sc(8, 8, PlotState::Unexplored);
    sc.set(2, 3, PlotState::Visible);
    sc.set(5, 4, PlotState::Seen);
    sc.sync();
    MaskStats st;
    CHECK_OK(a.r.build(a.g.ctx, sc.in, &st));
    CHECK_EQ(st.hexagons[0], 1u);    // seen
    CHECK_EQ(st.hexagons[1], 1u);    // visible
    CHECK_EQ(st.hexagons[2], 62u);   // unexplored
    CHECK_EQ(st.dropped, 0u);
    CHECK_EQ(st.draws, 1u + 1u + 3u);
    Image im;
    CHECK(readMip(a.g, a.r.srv(), 0, im));
    float x, y;
    centre(sc.map, 2, 3, x, y);
    CHECK_PX(texel(im, sc.view.overlayProj, x, y), 255, 0, 255, 255);   // visible (1, 0, 1, 1)
    centre(sc.map, 5, 4, x, y);
    CHECK_PX(texel(im, sc.view.overlayProj, x, y), 255, 0, 0, 255);     // seen (1, 0, 0, 1)
    centre(sc.map, 6, 6, x, y);
    CHECK_PX(texel(im, sc.view.overlayProj, x, y), 0, 255, 0, 255);     // unexplored (0, 1, 0, 1)
    // far outside every hexagon: the clear
    CHECK_PX(im.at(1, 1), 0, 0, 0, 0);
}

C5R_TEST(edge_ramp_profile)
{
    Ready a;
    if (!a.ok) return;
    // visible (3, 4) beside unexplored (4, 4) on one row; window 256 x 256 world units: 4 texels a world unit
    Scene sc(8, 8, PlotState::Seen);
    sc.set(4, 4, PlotState::Unexplored);
    sc.set(3, 4, PlotState::Visible);
    float cx, cy;
    centre(sc.map, 3, 4, cx, cy);
    windowProj(cx - 128, cx + 128, cy - 128, cy + 128, sc.view.overlayProj);
    sc.sync();
    CHECK_OK(a.r.build(a.g.ctx, sc.in, nullptr));
    Image im;
    CHECK(readMip(a.g, a.r.srv(), 0, im));
    const float* op = sc.view.overlayProj;
    // along +x from the visible centre: B = 255 to the shared edge at 32, linear to 0 at 1.33 x 32 = 42.56
    CHECK_EQ(static_cast<int>(texel(im, op, cx + 30.0f, cy)[2]), 255);
    const int bMid = texel(im, op, cx + 37.28f, cy)[2];
    CHECK(bMid > 112 && bMid < 144);
    CHECK_EQ(static_cast<int>(texel(im, op, cx + 43.5f, cy)[2]), 0);
    // the unexplored neighbour's G: 255 from its centre to its edge (x = cx + 32), 0 at 1.40 x 32 = 44.8 from its centre
    CHECK_EQ(static_cast<int>(texel(im, op, cx + 34.0f, cy)[1]), 255);
    const int gMid = texel(im, op, cx + 64.0f - 38.4f, cy)[1];   // half way down its ramp
    CHECK(gMid > 112 && gMid < 144);
    CHECK_EQ(static_cast<int>(texel(im, op, cx + 64.0f - 46.0f, cy)[1]), 0);
    // both ramps overlap at the edge: R and B high and G high together
    const uint8_t* e = texel(im, op, cx + 32.2f, cy);
    CHECK(e[0] > 240 && e[1] > 240 && e[2] > 240 && e[3] == 255);
    // the mips are generated (2 x 2 box of mip 0, rounding)
    Image m1;
    CHECK(readMip(a.g, a.r.srv(), 1, m1));
    int worst = 0;
    for (uint32_t y = 0; y < m1.h; y += 7)
        for (uint32_t x = 0; x < m1.w; x += 5)
            for (int c = 0; c < 4; ++c) {
                const int s = im.at(2 * x, 2 * y)[c] + im.at(2 * x + 1, 2 * y)[c] + im.at(2 * x, 2 * y + 1)[c] +
                              im.at(2 * x + 1, 2 * y + 1)[c];
                worst = std::max(worst, std::abs(4 * m1.at(x, y)[c] - s));
            }
    CHECK(worst <= 8);   // within 2 levels of the 2 x 2 mean
}

C5R_TEST(wrap_views_two_windows)
{
    Ready a;
    if (!a.ok) return;
    Scene sc(8, 6, PlotState::Unexplored, true);
    sc.set(1, 2, PlotState::Visible);
    float x1, y1;
    centre(sc.map, 1, 2, x1, y1);
    // As stock's wrap copy (overlay-system.md 4): view 1's window is view 0's moved by the map's width in world, so a
    // plot and its copy land on the same texels. View 0 draws plots 4..7 only; view 1 the copies 8..11, which read the
    // states of x mod 8: the visible plot (1, 2) reaches the mask only through view 1's copy (9, 2).
    View v[2];
    windowProj(x1 - 200, x1 + 200, y1 - 200, y1 + 200, v[0].overlayProj);
    v[0].x0 = 4; v[0].x1 = 8; v[0].y0 = 0; v[0].y1 = 6;
    const float shift = 8 * 64.0f;
    windowProj(x1 + shift - 200, x1 + shift + 200, y1 - 200, y1 + 200, v[1].overlayProj);
    v[1].x0 = 8; v[1].x1 = 12; v[1].y0 = 0; v[1].y1 = 6;
    sc.in.views = v;
    sc.in.numViews = 2;
    MaskStats st;
    CHECK_OK(a.r.build(a.g.ctx, sc.in, &st));
    CHECK_EQ(st.hexagons[1], 1u);
    CHECK_EQ(st.hexagons[2], 47u);
    Image im;
    CHECK(readMip(a.g, a.r.srv(), 0, im));
    float x9, y9;
    centre(sc.map, 9, 2, x9, y9);
    CHECK(std::fabs(x9 - (x1 + shift)) < 0.01f);
    CHECK_PX(texel(im, v[1].overlayProj, x9, y9), 255, 0, 255, 255);
    CHECK_PX(texel(im, v[0].overlayProj, x1, y1), 255, 0, 255, 255);   // the same texel
    // the same map without wrap: plots past the width are outside the map and drawn unexplored
    sc.map.wrapX = false;
    sc.sync();
    sc.in.views = v;
    sc.in.numViews = 2;
    CHECK_OK(a.r.build(a.g.ctx, sc.in, &st));
    CHECK_EQ(st.hexagons[1], 0u);
    CHECK_EQ(st.hexagons[2], 48u);
    CHECK(readMip(a.g, a.r.srv(), 0, im));
    CHECK_PX(texel(im, v[1].overlayProj, x9, y9), 0, 255, 0, 255);
}

C5R_TEST(empty_and_all_visible_maps)
{
    Ready a;
    if (!a.ok) return;
    {
        Scene sc(10, 8, PlotState::Unexplored);
        MaskStats st;
        CHECK_OK(a.r.build(a.g.ctx, sc.in, &st));
        CHECK_EQ(st.hexagons[0] + st.hexagons[1], 0u);
        CHECK_EQ(st.hexagons[2], 80u);
        CHECK_EQ(st.draws, 4u);
        Image im;
        CHECK(readMip(a.g, a.r.srv(), 0, im));
        size_t rb = 0, g = 0;
        for (size_t i = 0; i < im.px.size(); i += 4) {
            rb += im.px[i] + im.px[i + 2];
            g += im.px[i + 1] ? 1 : 0;
        }
        CHECK_EQ(rb, static_cast<size_t>(0));
        CHECK(g > im.px.size() / 4 / 4);
    }
    {
        Scene sc(10, 8, PlotState::Visible);
        MaskStats st;
        CHECK_OK(a.r.build(a.g.ctx, sc.in, &st));
        CHECK_EQ(st.hexagons[1], 80u);
        CHECK_EQ(st.hexagons[0] + st.hexagons[2], 0u);
        Image im;
        CHECK(readMip(a.g, a.r.srv(), 0, im));
        size_t g = 0;
        for (size_t i = 0; i < im.px.size(); i += 4) g += im.px[i + 1];
        CHECK_EQ(g, static_cast<size_t>(0));
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 10; ++x) {
                float cx, cy;
                centre(sc.map, x, y, cx, cy);
                CHECK_PX(texel(im, sc.view.overlayProj, cx, cy), 255, 0, 255, 255);
            }
    }
}

C5R_TEST(cap_2048_per_pass)
{
    Ready a;
    if (!a.ok) return;
    Scene sc(60, 40, PlotState::Unexplored);   // 2,400 unexplored plots in range
    sc.set(0, 0, PlotState::Seen);
    sc.sync();
    MaskStats st;
    CHECK_OK(a.r.build(a.g.ctx, sc.in, &st));
    CHECK_EQ(st.hexagons[2], 2048u);
    CHECK_EQ(st.dropped, 2399u - 2048u);
    CHECK_EQ(st.hexagons[0], 1u);
    CHECK_EQ(st.draws, 1u + 2048u / 24u + 1u);   // 85 full draws and one of 8
}

C5R_TEST(range_from_window)
{
    // the Tiny map of 3951's cam1 (window x -1600..-768, y -64..640): the rule's range covers the window's plots
    MapInput m;
    m.width = 56;
    m.height = 36;
    nominalPlacement(m);
    View v;
    windowProj(-1600, -768, -64, 640, v.overlayProj);
    int32_t r[4];
    CHECK_OK(plotRangeFromWindow(m, v, r));
    std::printf("  cam1 window rule: X [%d, %d) Y [%d, %d) (stock drew X [1, 17) Y [18, 30))\n", r[0], r[1], r[2], r[3]);
    CHECK(r[0] <= 2 && r[1] >= 17 && r[2] <= 18 && r[3] >= 30);
    CHECK(r[0] >= 0 && r[1] <= 18 && r[2] >= 15 && r[3] <= 32);
    // negative: NaN, zero scale, no placement, empty map
    View bad = v;
    bad.overlayProj[13] = std::nanf("");
    CHECK_ERR(plotRangeFromWindow(m, bad, r), Err::BadLayout);
    bad = v;
    bad.overlayProj[0] = 0.0f;
    CHECK_ERR(plotRangeFromWindow(m, bad, r), Err::BadLayout);
    MapInput none;
    none.width = 56;
    none.height = 36;
    CHECK_ERR(plotRangeFromWindow(none, v, r), Err::BadLayout);
    MapInput empty;
    nominalPlacement(empty);
    CHECK_ERR(plotRangeFromWindow(empty, v, r), Err::BadLayout);
}

// The Tiny map's live placement (epic 3000 ground truth, 2026-11-11: M_h2w, GridHeightmap sx sy ox oy, world box) and
// the two cameras of 3951's captures (TerrainFrame ViewProj, measured 2026-11-14); stock drew X [1, 17) x Y [18, 30)
// at cam1 and X [3, 14) x Y [21, 29) at cam2 (fog-of-war.md 7.1, the captures' FOWDrawMask cb0).
MapInput tinyLive()
{
    MapInput m;
    m.width = 56;
    m.height = 36;
    const float h[16] = {63.9986000f, 0.000938415644f, 0, 0, 32.0f, 64.0018845f, 0, 0, 0, 0, 0, 0, 32.0007057f, 128.003296f, 0, 1};
    std::memcpy(m.hexToWorld, h, sizeof h);
    m.gridScale[0] = 1.0f;
    m.gridScale[1] = 0.866f;
    m.gridOrigin[0] = 1792.0f;
    m.gridOrigin[1] = 1108.47998f;
    m.worldWidth = 3583.0f;
    m.worldHeight = 2216.09399f;
    return m;
}

C5R_TEST(range_from_camera_stock_cameras)
{
    MapInput m = tinyLive();
    View v;
    const float cam1[16] = {2.135113f, 0, 0, 0, 0, 2.73274922f, 0.716057539f, 0.707106829f,
                            0, 2.73274922f, -0.716057539f, -0.707106829f, 2458.02808f, -792.057678f, 349.420624f, 395.052856f};
    const float cam2[16] = {2.135113f, 0, 0, 0, 0, 2.73274922f, 0.716057539f, 0.707106829f,
                            0, 2.73274922f, -0.716057539f, -0.707106829f, 2596.28394f, -1093.38892f, 36.5974731f, 86.140007f};
    int32_t r[4];
    std::memcpy(v.viewProj, cam1, sizeof cam1);
    CHECK_OK(plotRangeFromCamera(m, v, r));
    CHECK(r[0] == 1 && r[1] == 17 && r[2] == 18 && r[3] == 30);
    std::printf("  cam1 camera rule: X [%d, %d) Y [%d, %d)\n", r[0], r[1], r[2], r[3]);
    std::memcpy(v.viewProj, cam2, sizeof cam2);
    CHECK_OK(plotRangeFromCamera(m, v, r));
    CHECK(r[0] == 3 && r[1] == 14 && r[2] == 21 && r[3] == 29);
    std::printf("  cam2 camera rule: X [%d, %d) Y [%d, %d)\n", r[0], r[1], r[2], r[3]);
    // the same with the textbook placement
    MapInput n = m;
    nominalPlacement(n);
    CHECK_OK(plotRangeFromCamera(n, v, r));
    CHECK(r[0] == 3 && r[1] == 14 && r[2] == 21 && r[3] == 29);
    // X clamps to [0, width]: the camera moved 40 plots west
    float west[16];
    std::memcpy(west, cam1, sizeof west);
    west[12] += 2.135113f * 40 * 64;
    std::memcpy(v.viewProj, west, sizeof west);
    CHECK_OK(plotRangeFromCamera(m, v, r));
    CHECK_EQ(r[0], 0);
    CHECK(r[1] >= 0 && r[1] <= 56);
    // negative: NaN, singular, a ray parallel to the ground, no world box, no placement, empty map
    std::memcpy(v.viewProj, cam1, sizeof cam1);
    v.viewProj[5] = std::nanf("");
    CHECK_ERR(plotRangeFromCamera(m, v, r), Err::BadLayout);
    std::memset(v.viewProj, 0, sizeof v.viewProj);
    CHECK_ERR(plotRangeFromCamera(m, v, r), Err::BadLayout);
    const float side[16] = {1, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 1};   // view direction along +y: rays parallel to z = 0
    std::memcpy(v.viewProj, side, sizeof side);
    CHECK_ERR(plotRangeFromCamera(m, v, r), Err::BadLayout);
    std::memcpy(v.viewProj, cam1, sizeof cam1);
    MapInput nw = m;
    nw.worldHeight = 0.0f;
    CHECK_ERR(plotRangeFromCamera(nw, v, r), Err::BadLayout);
    nw = m;
    nw.gridScale[1] = 0.0f;
    CHECK_ERR(plotRangeFromCamera(nw, v, r), Err::BadLayout);
    nw = m;
    nw.height = 0;
    CHECK_ERR(plotRangeFromCamera(nw, v, r), Err::BadLayout);
}

C5R_TEST(build_takes_the_camera_rule)
{
    Ready a;
    if (!a.ok) return;
    MapInput m = tinyLive();
    std::vector<PlotState> s(56 * 36, PlotState::Unexplored);
    m.states = s.data();
    m.stateCount = s.size();
    View v;
    const float cam1[16] = {2.135113f, 0, 0, 0, 0, 2.73274922f, 0.716057539f, 0.707106829f,
                            0, 2.73274922f, -0.716057539f, -0.707106829f, 2458.02808f, -792.057678f, 349.420624f, 395.052856f};
    std::memcpy(v.viewProj, cam1, sizeof cam1);
    windowProj(-1600, -768, -64, 640, v.overlayProj);
    MaskInput in;
    in.map = m;
    in.views = &v;
    in.numViews = 1;
    MaskStats st;
    CHECK_OK(a.r.build(a.g.ctx, in, &st));
    CHECK_EQ(st.hexagons[2], 16u * 12u);   // stock's 192 hexagons at cam1
    in.map.worldHeight = 0.0f;               // no world box: the window rule (wider)
    CHECK_OK(a.r.build(a.g.ctx, in, &st));
    CHECK(st.hexagons[2] > 192u);
}

// ---- ticket 0674: the window from the camera, the uv map between two windows, the overlay resample ----

namespace {

const float kCam1[16] = {2.135113f, 0, 0, 0, 0, 2.73274922f, 0.716057539f, 0.707106829f,
                         0, 2.73274922f, -0.716057539f, -0.707106829f, 2458.02808f, -792.057678f, 349.420624f, 395.052856f};
const float kCam2[16] = {2.135113f, 0, 0, 0, 0, 2.73274922f, 0.716057539f, 0.707106829f,
                         0, 2.73274922f, -0.716057539f, -0.707106829f, 2596.28394f, -1093.38892f, 36.5974731f, 86.140007f};

// the window box of an OverlayProj (x0, x1, y0, y1)
void boxOf(const float op[16], double b[4])
{
    b[0] = (-1.0 - op[12]) / op[0];
    b[1] = (1.0 - op[12]) / op[0];
    b[2] = (-1.0 - op[13]) / op[5];
    b[3] = (1.0 - op[13]) / op[5];
}

bool sameBox(const float op[16], double x0, double x1, double y0, double y1)
{
    double b[4];
    boxOf(op, b);
    return std::fabs(b[0] - x0) < 1e-3 && std::fabs(b[1] - x1) < 1e-3 && std::fabs(b[2] - y0) < 1e-3 && std::fabs(b[3] - y1) < 1e-3;
}

// a camera zoomed out by `k` (< 1): clip x and y scaled about the view's centre (a wider field of view, the same eye)
void zoomed(const float cam[16], float k, float out[16])
{
    std::memcpy(out, cam, 16 * sizeof(float));
    for (int r = 0; r < 4; ++r) {
        out[r * 4 + 0] *= k;
        out[r * 4 + 1] *= k;
    }
}

void uvOf(const float op[16], double x, double y, double& u, double& v)
{
    u = 0.5 * (x * op[0] + op[12]) + 0.5;
    v = 0.5 - 0.5 * (y * op[5] + op[13]);
}

// A source overlay of n x n texels with its full mip chain: 8 x 8 blocks of one colour each (r 30 bx + 20, g 30 by + 20,
// b 128, a 255), its mips made by GenerateMips (a block's interior keeps its colour down to the block's last mip).
struct BlockSource {
    ID3D11Texture2D* tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    uint32_t n = 0;
    ~BlockSource()
    {
        if (srv) srv->Release();
        if (tex) tex->Release();
    }
    bool make(Gpu& g, uint32_t size)
    {
        n = size;
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = d.Height = size;
        d.MipLevels = 0;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        d.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        if (FAILED(g.dev->CreateTexture2D(&d, nullptr, &tex)) || FAILED(g.dev->CreateShaderResourceView(tex, nullptr, &srv)))
            return false;
        std::vector<uint8_t> px(static_cast<size_t>(size) * size * 4);
        const uint32_t blk = size / 8;
        for (uint32_t y = 0; y < size; ++y)
            for (uint32_t x = 0; x < size; ++x) {
                uint8_t* p = &px[(static_cast<size_t>(y) * size + x) * 4];
                p[0] = static_cast<uint8_t>(30 * (x / blk) + 20);
                p[1] = static_cast<uint8_t>(30 * (y / blk) + 20);
                p[2] = 128;
                p[3] = 255;
            }
        g.ctx->UpdateSubresource(tex, 0, nullptr, px.data(), size * 4, 0);
        g.ctx->GenerateMips(srv);
        return true;
    }
};

} // namespace

C5R_TEST(window_from_camera_stock_cameras)
{
    View v;
    float w[16];
    Footprint fp;
    std::memcpy(v.viewProj, kCam1, sizeof kCam1);
    CHECK_OK(windowFromCamera(v, w, &fp));
    std::printf("  cam1 footprint x [%.2f, %.2f] y [%.2f, %.2f]\n", fp.x0, fp.x1, fp.y0, fp.y1);
    CHECK(sameBox(w, -1600, -768, -64, 640));   // stock's window at cam1 (fog-of-war.md 7.1; build_takes_the_camera_rule)
    CHECK(footprintInWindow(fp, w));
    CHECK(w[10] == -0.5f && w[14] == 0.5f && w[15] == 1.0f && w[1] == 0.0f && w[4] == 0.0f);   // the EXE's form
    float stock1[16];
    windowProj(-1600, -768, -64, 640, stock1);
    CHECK(footprintInWindow(fp, stock1));
    std::memcpy(v.viewProj, kCam2, sizeof kCam2);
    Footprint fp2;
    CHECK_OK(windowFromCamera(v, w, &fp2));
    std::printf("  cam2 footprint x [%.2f, %.2f] y [%.2f, %.2f]\n", fp2.x0, fp2.x1, fp2.y0, fp2.y1);
    CHECK(sameBox(w, -1536, -960, 128, 640));
    CHECK(footprintInWindow(fp2, w));
    // footprintFromCamera is the box plotRangeFromCamera takes (stock's X [3, 14) Y [21, 29) at cam2 unchanged)
    int32_t r[4];
    CHECK_OK(plotRangeFromCamera(tinyLive(), v, r));
    CHECK(r[0] == 3 && r[1] == 14 && r[2] == 21 && r[3] == 29);
    // cam1 zoomed out (clip x, y x 0.8: the footprint 25 % wider about the view's centre) leaves cam1's window; its own
    // window holds it
    float z[16];
    zoomed(kCam1, 0.8f, z);
    std::memcpy(v.viewProj, z, sizeof z);
    Footprint fz;
    CHECK_OK(windowFromCamera(v, w, &fz));
    std::printf("  cam1 x 0.8 footprint x [%.2f, %.2f] y [%.2f, %.2f]\n", fz.x0, fz.x1, fz.y0, fz.y1);
    CHECK(!footprintInWindow(fz, stock1));
    CHECK(footprintInWindow(fz, w));
    double b[4];
    boxOf(w, b);
    for (int i = 0; i < 4; ++i) CHECK(std::fabs(b[i] / 64.0 - std::round(b[i] / 64.0)) < 1e-6);   // 64-unit multiples
    // the tolerance: a footprint 0.005 units past the edge is inside, 0.5 units past it is not
    Footprint e = fp;
    e.x1 = -768.0 + 0.005;
    CHECK(footprintInWindow(e, stock1));
    e.x1 = -768.0 + 0.5;
    CHECK(!footprintInWindow(e, stock1));
    // negative: a singular camera, a window with a zero scale
    std::memset(v.viewProj, 0, sizeof v.viewProj);
    CHECK_ERR(windowFromCamera(v, w), Err::BadLayout);
    float zero[16] = {};
    CHECK(!footprintInWindow(fp, zero));
}

C5R_TEST(uv_map_between_windows)
{
    float from[16], to[16];
    windowProj(-1600, -768, -64, 640, from);
    windowProj(-1792, -640, -128, 768, to);   // a zoom-out step: wider in both axes
    UvMap m;
    CHECK_OK(uvMapBetweenWindows(from, to, m));
    const double pts[4][2] = {{-1530.0, 40.0}, {-772.0, 586.0}, {-1700.0, 700.0}, {-1000.0, 300.0}};
    for (const auto& p : pts) {
        double uf, vf, ut, vt;
        uvOf(from, p[0], p[1], uf, vf);
        uvOf(to, p[0], p[1], ut, vt);
        CHECK(std::fabs(m.a[0] * ut + m.b[0] - uf) < 1e-5);
        CHECK(std::fabs(m.a[1] * vt + m.b[1] - vf) < 1e-5);
    }
    CHECK(std::fabs(m.a[0] - 1152.0 / 832.0) < 1e-5 && std::fabs(m.a[1] - 896.0 / 704.0) < 1e-5);
    CHECK_OK(uvMapBetweenWindows(from, from, m));   // the same window: the identity
    CHECK(std::fabs(m.a[0] - 1) < 1e-6 && std::fabs(m.a[1] - 1) < 1e-6 && std::fabs(m.b[0]) < 1e-6 && std::fabs(m.b[1]) < 1e-6);
    float bad[16];
    std::memcpy(bad, to, sizeof bad);
    bad[4] = 0.001f;
    CHECK_ERR(uvMapBetweenWindows(from, bad, m), Err::BadLayout);
    bad[4] = 0.0f;
    bad[5] = 0.0f;
    CHECK_ERR(uvMapBetweenWindows(bad, to, m), Err::BadLayout);
    bad[5] = std::nanf("");
    CHECK_ERR(uvMapBetweenWindows(from, bad, m), Err::BadLayout);
}

// The resampled overlay holds the source's texel at the same world point, and (0, 0, 0, 0) where the source's window does
// not reach (the strip that came into view). Two moves: a zoom-out step with a shift, and a pure shift of a third of a
// block. Device: hardware, WARP without one; no EXE needed.
C5R_TEST(overlay_resample_world_points)
{
    Gpu g;
    CHECK(g.dev != nullptr);
    if (!g.dev) return;
    BlockSource s;
    CHECK(s.make(g, 256));
    if (!s.srv) return;
    struct Move { double from[4], to[4]; };
    const Move moves[2] = {{{-1600, -768, -64, 640}, {-1792, -544, -192, 864}},    // x 1.5, y 1.5 (a zoom-out step)
                           {{-1600, -768, -64, 640}, {-1536, -704, -128, 576}}};   // shifted 64 units (a pan step)
    OverlayResampler rs;
    for (const Move& mv : moves) {
        float from[16], to[16];
        windowProj(static_cast<float>(mv.from[0]), static_cast<float>(mv.from[1]), static_cast<float>(mv.from[2]),
                   static_cast<float>(mv.from[3]), from);
        windowProj(static_cast<float>(mv.to[0]), static_cast<float>(mv.to[1]), static_cast<float>(mv.to[2]),
                   static_cast<float>(mv.to[3]), to);
        CHECK_OK(rs.resample(g.ctx, s.srv, from, to));
        CHECK(rs.srv() != nullptr);
        if (!rs.srv()) return;
        Image out;
        CHECK(readMip(g, rs.srv(), 0, out));
        CHECK_EQ(out.w, 256u);
        // block centres of the source: the same colour at the same world point
        int checked = 0, bad = 0;
        for (int by = 0; by < 8; ++by)
            for (int bx = 0; bx < 8; ++bx) {
                const double uo = (32.0 * bx + 16.0) / 256.0, vo = (32.0 * by + 16.0) / 256.0;
                const double x = ((2.0 * uo - 1.0) - from[12]) / from[0], y = ((1.0 - 2.0 * vo) - from[13]) / from[5];
                double un, vn;
                uvOf(to, x, y, un, vn);
                if (un < 0 || un >= 1 || vn < 0 || vn >= 1) continue;
                const uint8_t* p = out.at(static_cast<int>(std::floor(un * 256)), static_cast<int>(std::floor(vn * 256)));
                ++checked;
                if (std::abs(p[0] - (30 * bx + 20)) > 3 || std::abs(p[1] - (30 * by + 20)) > 3 || std::abs(p[2] - 128) > 3 || p[3] < 252) {
                    if (bad++ < 4) std::printf("  block (%d, %d): got %s\n", bx, by, px(p).c_str());
                }
            }
        CHECK(checked >= 30);
        CHECK_EQ(bad, 0);
        // every texel whose source point lies a texel or more outside [0, 1] is (0, 0, 0, 0); a texel two or more
        // texels inside is opaque
        UvMap m;
        CHECK_OK(uvMapBetweenWindows(from, to, m));
        int outside = 0, outsideBad = 0, inside = 0, insideBad = 0;
        for (uint32_t ty = 0; ty < out.h; ++ty)
            for (uint32_t tx = 0; tx < out.w; ++tx) {
                const double u = m.a[0] * (tx + 0.5) / out.w + m.b[0], v = m.a[1] * (ty + 0.5) / out.h + m.b[1];
                const double e = 1.0 / 256.0;
                const uint8_t* p = out.at(static_cast<int>(tx), static_cast<int>(ty));
                if (u < -e || u > 1 + e || v < -e || v > 1 + e) {
                    ++outside;
                    outsideBad += eq4(p, 0, 0, 0, 0) ? 0 : 1;
                } else if (u > 2 * e && u < 1 - 2 * e && v > 2 * e && v < 1 - 2 * e) {
                    ++inside;
                    insideBad += p[3] >= 252 ? 0 : 1;
                }
            }
        std::printf("  move to [%.0f, %.0f] x [%.0f, %.0f]: %d block centres, %d strip texels, %d inside texels\n", mv.to[0], mv.to[1],
                    mv.to[2], mv.to[3], checked, outside, inside);
        CHECK(outside > 0 && inside > 0);
        CHECK_EQ(outsideBad, 0);
        CHECK_EQ(insideBad, 0);
        // the mips were made: the last level is the strip and the blocks averaged (not the untouched clear)
        Image last;
        CHECK(readMip(g, rs.srv(), 8, last));
        CHECK(last.w == 1 && last.at(0, 0)[3] > 0 && last.at(0, 0)[3] < 255);
    }
    // negative: no source, a source of another format, a bad window; a failed call leaves no result
    float from[16], to[16];
    windowProj(-1600, -768, -64, 640, from);
    windowProj(-1792, -544, -192, 864, to);
    CHECK_ERR(rs.resample(g.ctx, nullptr, from, to), Err::BadLayout);
    CHECK(rs.srv() == nullptr);
    CHECK_OK(rs.resample(g.ctx, s.srv, from, to));
    float bad[16];
    std::memcpy(bad, to, sizeof bad);
    bad[0] = 0.0f;
    CHECK_ERR(rs.resample(g.ctx, s.srv, from, bad), Err::BadLayout);
    CHECK(rs.srv() == nullptr);
    ID3D11Texture2D* t = nullptr;
    ID3D11ShaderResourceView* f = nullptr;
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = d.Height = 16;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    d.SampleDesc.Count = 1;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (SUCCEEDED(g.dev->CreateTexture2D(&d, nullptr, &t)) && SUCCEEDED(g.dev->CreateShaderResourceView(t, nullptr, &f)))
        CHECK_ERR(rs.resample(g.ctx, f, from, to), Err::BadLayout);
    if (f) f->Release();
    if (t) t->Release();
    rs.release();
    CHECK(rs.srv() == nullptr);
    CHECK_OK(rs.resample(g.ctx, s.srv, from, to));   // made again after release()
}

// The pass's GPU cost at the overlay's live size (2048 x 2048, 12 mips: overlay-system.md, the tap reader's check) with
// timestamp queries around resample() (the draw and GenerateMips); printed, not asserted (an indication under
// docs/briefs/PERFORMANCE.md, no speed claim). C5R_RESAMPLE_TIMING_N sets the sample count (default 200).
C5R_TEST(overlay_resample_gpu_time)
{
    Gpu g;
    CHECK(g.dev != nullptr);
    if (!g.dev) return;
    BlockSource s;
    CHECK(s.make(g, 2048));
    if (!s.srv) return;
    int n = 200;
    if (const char* e = std::getenv("C5R_RESAMPLE_TIMING_N")) n = std::max(10, std::atoi(e));
    float from[16], zoomTo[16], panTo[16];
    windowProj(-1600, -768, -64, 640, from);
    windowProj(-1792, -544, -192, 864, zoomTo);
    windowProj(-1536, -704, -128, 576, panTo);
    D3D11_QUERY_DESC qd = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    ID3D11Query *dis = nullptr, *t0 = nullptr, *t1 = nullptr;
    CHECK(SUCCEEDED(g.dev->CreateQuery(&qd, &dis)));
    qd.Query = D3D11_QUERY_TIMESTAMP;
    CHECK(SUCCEEDED(g.dev->CreateQuery(&qd, &t0)) && SUCCEEDED(g.dev->CreateQuery(&qd, &t1)));
    if (!dis || !t0 || !t1) return;
    std::wstring adapter = L"?";
    {
        IDXGIDevice* xd = nullptr;
        IDXGIAdapter* ad = nullptr;
        DXGI_ADAPTER_DESC desc;
        if (SUCCEEDED(g.dev->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&xd))) && SUCCEEDED(xd->GetAdapter(&ad)) &&
            SUCCEEDED(ad->GetDesc(&desc)))
            adapter = desc.Description;
        if (ad) ad->Release();
        if (xd) xd->Release();
    }
    OverlayResampler rs;
    for (int arm = 0; arm < 2; ++arm) {
        const float* to = arm == 0 ? zoomTo : panTo;
        std::vector<double> ms;
        for (int i = 0; i < n + 5; ++i) {
            g.ctx->Begin(dis);
            g.ctx->End(t0);
            const Status st = rs.resample(g.ctx, s.srv, from, to);
            g.ctx->End(t1);
            g.ctx->End(dis);
            CHECK_OK(st);
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj = {};
            UINT64 a = 0, b = 0;
            while (g.ctx->GetData(dis, &dj, sizeof dj, 0) == S_FALSE) {}
            while (g.ctx->GetData(t0, &a, sizeof a, 0) == S_FALSE) {}
            while (g.ctx->GetData(t1, &b, sizeof b, 0) == S_FALSE) {}
            if (i >= 5 && !dj.Disjoint && dj.Frequency && b >= a) ms.push_back(static_cast<double>(b - a) * 1000.0 / dj.Frequency);
        }
        CHECK(!ms.empty());
        if (ms.empty()) break;
        std::sort(ms.begin(), ms.end());
        auto q = [&](double f) { return ms[std::min(ms.size() - 1, static_cast<size_t>(f * (ms.size() - 1) + 0.5))]; };
        std::printf("  resample 2048 x 2048 x 12 mips (%s): gpu ms median %.4f p10 %.4f p90 %.4f p99 %.4f max %.4f, n %zu, adapter '%ls'\n",
                    arm == 0 ? "zoom-out step x 1.5" : "pan step 64 units", q(0.5), q(0.1), q(0.9), q(0.99), ms.back(), ms.size(),
                    adapter.c_str());
    }
    dis->Release();
    t0->Release();
    t1->Release();
}

// ---- Ticket 0793: the overlay's history (research/05-graphics-composition/overlay-history.md) ----
namespace {

// Tiny's world box (the camera block's, fog-of-war.md: width 3583 = the wrap period, height 2216.094) as the history keys it.
OverlayWorld tinyWorld(uint32_t key = 1, bool wrap = true)
{
    OverlayWorld w;
    w.x0 = -1791.5;
    w.x1 = 1791.5;
    w.y0 = -1108.047;
    w.y1 = 1108.047;
    w.wrapPeriod = wrap ? 3583.0 : 0.0;
    w.key = key;
    return w;
}

// The colour of a world point: 64-unit blocks anchored to the world (x taken modulo the wrap period from x0), so every
// overlay of the same ground shows the same colour wherever its window lies. salt changes the colours (a newer overlay).
void worldColour(double x, double y, int salt, uint8_t c[4])
{
    const double P = 3583.0;
    const double xw = x - P * std::floor((x + 1791.5) / P);
    const int bx = static_cast<int>(std::floor((xw + 1791.5) / 64.0)), by = static_cast<int>(std::floor((y + 1108.047) / 64.0));
    c[0] = static_cast<uint8_t>(20 + 30 * ((bx + salt) % 8));
    c[1] = static_cast<uint8_t>(20 + 30 * ((by + 2 * salt) % 8));
    c[2] = static_cast<uint8_t>(salt ? 200 : 128);
    c[3] = 255;
}

// An "EXE overlay" of window [x0, x1] x [y0, y1]: n x n texels with mips, each texel the world colour of its centre.
struct WorldSource {
    ID3D11Texture2D* tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    float proj[16] = {};
    ~WorldSource()
    {
        if (srv) srv->Release();
        if (tex) tex->Release();
    }
    bool make(Gpu& g, uint32_t n, double x0, double x1, double y0, double y1, int salt = 0)
    {
        windowProjFromBox(x0, x1, y0, y1, proj);
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = d.Height = n;
        d.MipLevels = 0;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        d.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        if (FAILED(g.dev->CreateTexture2D(&d, nullptr, &tex)) || FAILED(g.dev->CreateShaderResourceView(tex, nullptr, &srv)))
            return false;
        std::vector<uint8_t> px(static_cast<size_t>(n) * n * 4);
        for (uint32_t ty = 0; ty < n; ++ty)
            for (uint32_t tx = 0; tx < n; ++tx) {
                const double x = x0 + (x1 - x0) * (tx + 0.5) / n, y = y1 - (y1 - y0) * (ty + 0.5) / n;
                worldColour(x, y, salt, &px[(static_cast<size_t>(ty) * n + tx) * 4]);
            }
        g.ctx->UpdateSubresource(tex, 0, nullptr, px.data(), n * 4, 0);
        g.ctx->GenerateMips(srv);
        return true;
    }
};

// Over the block centres of the result (window `to`) that `region` accepts: counts of centres checked, centres whose
// colour is the world colour (salt), and centres that are (0, 0, 0, 0).
struct Tally {
    int n = 0, match = 0, clear = 0;
};
template <class F> Tally tallyBlocks(const Image& out, const float to[16], int salt, F region)
{
    Tally t;
    double tx0 = ((-1.0) - to[12]) / to[0], tx1 = (1.0 - to[12]) / to[0], ty0 = (-1.0 - to[13]) / to[5], ty1 = (1.0 - to[13]) / to[5];
    // the block centres: x = -1791.5 + 32 + 64 i, y = -1108.047 + 32 + 64 j (worldColour's blocks)
    const double yS = -1108.047 + 32.0 + 64.0 * std::floor((ty0 + 1108.047) / 64.0);
    const double xS = -1791.5 + 32.0 + 64.0 * std::floor((tx0 + 1791.5) / 64.0);
    for (double y = yS; y < ty1; y += 64.0)
        for (double x = xS; x < tx1; x += 64.0) {
            if (x - 20 < tx0 || x + 20 > tx1 || y - 20 < ty0 || y + 20 > ty1) continue;   // whole inside the target
            if (!region(x, y)) continue;
            const uint8_t* p = texel(out, to, static_cast<float>(x), static_cast<float>(y));
            uint8_t c[4];
            worldColour(x, y, salt, c);
            ++t.n;
            if (std::abs(p[0] - c[0]) <= 3 && std::abs(p[1] - c[1]) <= 3 && std::abs(p[2] - c[2]) <= 3 && p[3] >= 250) ++t.match;
            if (eq4(p, 0, 0, 0, 0)) ++t.clear;
        }
    return t;
}

bool inBox(double x, double y, double x0, double x1, double y0, double y1, double m = 24.0)
{
    return x > x0 + m && x < x1 - m && y > y0 + m && y < y1 - m;
}
bool outBox(double x, double y, double x0, double x1, double y0, double y1, double m = 24.0)
{
    return x < x0 - m || x > x1 + m || y < y0 - m || y > y1 + m;
}

} // namespace

// A pan right and back: the leading edge of the pan back is ground the first overlay held: the result shows it from the
// history at the same world point; ground no overlay held stays (0, 0, 0, 0); the counters say which. Then a zoom-out
// step over ground seen in two windows: no transparent ground at all. Device: hardware, WARP without one; no EXE needed.
C5R_TEST(overlay_history_fills_leading_edge)
{
    Gpu g;
    CHECK(g.dev != nullptr);
    if (!g.dev) return;
    const OverlayWorld world = tinyWorld();
    const double A[4] = {-1600, -768, -64, 640}, B[4] = {-1344, -512, -64, 640};   // B: A panned 256 units right
    WorldSource sa, sb;
    CHECK(sa.make(g, 256, A[0], A[1], A[2], A[3]));
    CHECK(sb.make(g, 256, B[0], B[1], B[2], B[3]));
    if (!sa.srv || !sb.srv) return;
    OverlayResampler rs;
    // at rest on A: the first window after a load is drawn after kRefreshCalls generations; then the pan to B at once
    const uint64_t rest = OverlayResampler::kRefreshCalls;
    for (uint64_t k = 1; k <= rest; ++k) CHECK_OK(rs.remember(g.ctx, sa.srv, sa.proj, world, k));
    CHECK_EQ(rs.historyStats().absorbed, 0u);
    CHECK_OK(rs.remember(g.ctx, sa.srv, sa.proj, world, rest + 1));
    CHECK_EQ(rs.historyStats().absorbed, 1u);
    CHECK_OK(rs.remember(g.ctx, sb.srv, sb.proj, world, rest + 2));
    CHECK_OK(rs.remember(g.ctx, sb.srv, sb.proj, world, rest + 2));   // the same key: nothing drawn
    OverlayHistoryStats st = rs.historyStats();
    CHECK_EQ(st.absorbed, 2u);
    CHECK_EQ(st.skipped, rest + 1);
    CHECK(st.width == 3583u && st.height == 2473u);   // 1 texel per world unit (8.9 M texels < 16 M), the 128-unit margins in y
    std::printf("  history %ux%u, %.3f texels per world unit\n", st.width, st.height, st.texelsPerUnit);
    // the pan back: camera window C = B moved 576 units left and 64 down; the pair is B's
    const double C[4] = {-1920, -1088, -128, 576};
    float to[16];
    windowProjFromBox(C[0], C[1], C[2], C[3], to);
    CHECK_OK(rs.resample(g.ctx, sb.srv, sb.proj, to));
    Image out;
    CHECK(rs.srv() && readMip(g, rs.srv(), 0, out));
    if (out.px.empty()) return;
    const Tally inB = tallyBlocks(out, to, 0, [&](double x, double y) { return inBox(x, y, B[0], B[1], B[2], B[3]); });
    const Tally fromA = tallyBlocks(out, to, 0, [&](double x, double y) {
        return inBox(x, y, A[0], A[1], A[2], A[3]) && outBox(x, y, B[0], B[1], B[2], B[3]);
    });
    const Tally never = tallyBlocks(out, to, 0, [&](double x, double y) {
        return outBox(x, y, A[0], A[1], A[2], A[3]) && outBox(x, y, B[0], B[1], B[2], B[3]);
    });
    std::printf("  pan back: source blocks %d/%d, history blocks %d/%d, never-seen blocks clear %d/%d\n", inB.match, inB.n, fromA.match,
                fromA.n, never.clear, never.n);
    CHECK(inB.n >= 4 && inB.match == inB.n);
    CHECK(fromA.n >= 8 && fromA.match == fromA.n);   // the leading edge kept from history (before 0793: clear)
    CHECK(never.n >= 8 && never.clear == never.n);   // ground no overlay held stays transparent (no invention)
    st = rs.historyStats();
    CHECK_EQ(st.strips, 1u);
    CHECK_EQ(st.stripHoles, 1u);
    CHECK(st.stripCells > st.stripCellsHeld && st.stripCellsHeld > 0);
    // a zoom-out step inside A and B's union (x -1600..-512, y -64..640), the pair B's: every block shown
    const double Z[4] = {-1536, -576, -64, 640};
    windowProjFromBox(Z[0], Z[1], Z[2], Z[3], to);
    CHECK_OK(rs.resample(g.ctx, sb.srv, sb.proj, to));
    CHECK(rs.srv() && readMip(g, rs.srv(), 0, out));
    const Tally all = tallyBlocks(out, to, 0, [](double, double) { return true; });
    std::printf("  zoom-out step over seen ground: %d/%d blocks, %d clear\n", all.match, all.n, all.clear);
    CHECK(all.n >= 100 && all.match == all.n && all.clear == 0);
    st = rs.historyStats();
    CHECK_EQ(st.strips, 2u);
    CHECK_EQ(st.stripFilled, 1u);
    // R3: a newer overlay of the same ground replaces the old one in the history; only it does
    WorldSource sa2;
    CHECK(sa2.make(g, 256, A[0], A[1], A[2], A[3], 1));
    CHECK_OK(rs.remember(g.ctx, sa2.srv, sa2.proj, world, 100));
    CHECK_OK(rs.remember(g.ctx, sb.srv, sb.proj, world, 101));   // B again, A's ground left alone outside B
    windowProjFromBox(C[0], C[1], C[2], C[3], to);
    CHECK_OK(rs.resample(g.ctx, sb.srv, sb.proj, to));
    CHECK(rs.srv() && readMip(g, rs.srv(), 0, out));
    const Tally newer = tallyBlocks(out, to, 1, [&](double x, double y) {
        return inBox(x, y, A[0], A[1], A[2], A[3]) && outBox(x, y, B[0], B[1], B[2], B[3]);
    });
    CHECK(newer.n >= 8 && newer.match == newer.n);
    // forget(): no history until the next remember(), then an empty one
    rs.forget();
    CHECK_OK(rs.resample(g.ctx, sb.srv, sb.proj, to));
    CHECK(rs.srv() && readMip(g, rs.srv(), 0, out));
    const Tally gone = tallyBlocks(out, to, 1, [&](double x, double y) { return outBox(x, y, B[0], B[1], B[2], B[3]); });
    CHECK(gone.n >= 8 && gone.clear == gone.n);
    CHECK_OK(rs.remember(g.ctx, sb.srv, sb.proj, world, 102));   // the first window after forget(): waits
    CHECK_OK(rs.remember(g.ctx, sa.srv, sa.proj, world, 103));   // a move: drawn into the cleared history
    CHECK_OK(rs.resample(g.ctx, sb.srv, sb.proj, to));
    CHECK(rs.srv() && readMip(g, rs.srv(), 0, out));
    const Tally gone2 = tallyBlocks(out, to, 1, [&](double x, double y) {
        return outBox(x, y, A[0], A[1], A[2], A[3]) && outBox(x, y, B[0], B[1], B[2], B[3]);
    });
    CHECK(gone2.n >= 8 && gone2.clear == gone2.n);
    const Tally back = tallyBlocks(out, to, 0, [&](double x, double y) {
        return inBox(x, y, A[0], A[1], A[2], A[3]) && outBox(x, y, B[0], B[1], B[2], B[3]);
    });
    CHECK(back.n >= 8 && back.match == back.n);
    // a new map key: no history until that map's first window is drawn
    CHECK_OK(rs.remember(g.ctx, sb.srv, sb.proj, tinyWorld(2), 104));
    CHECK_OK(rs.resample(g.ctx, sb.srv, sb.proj, to));
    CHECK(rs.srv() && readMip(g, rs.srv(), 0, out));
    const Tally newMap = tallyBlocks(out, to, 0, [&](double x, double y) { return outBox(x, y, B[0], B[1], B[2], B[3]); });
    CHECK(newMap.n >= 8 && newMap.clear == newMap.n);
    CHECK_OK(rs.remember(g.ctx, sa.srv, sa.proj, tinyWorld(2), 105));   // drawn: the old map's history cleared first
    CHECK(rs.historyStats().clears >= 2u);
}

// The wrap: an overlay whose window lies past the seam (x > 1791.5, the EXE's wrap copy) is the same ground as the window
// moved by one period; a later camera window on the other side reads it.
C5R_TEST(overlay_history_wrap_seam)
{
    Gpu g;
    CHECK(g.dev != nullptr);
    if (!g.dev) return;
    const OverlayWorld world = tinyWorld();
    const double A[4] = {1408, 2240, -64, 640};   // straddles the seam at 1791.5
    WorldSource sa, sb;
    CHECK(sa.make(g, 256, A[0], A[1], A[2], A[3]));
    const double B[4] = {-1856 + 256, -1024 + 256, -64, 640};   // elsewhere: the pair of the camera-window frame
    CHECK(sb.make(g, 256, B[0], B[1], B[2], B[3]));
    if (!sa.srv || !sb.srv) return;
    OverlayResampler rs;
    for (uint64_t k = 1; k <= OverlayResampler::kRefreshCalls + 1; ++k) CHECK_OK(rs.remember(g.ctx, sa.srv, sa.proj, world, k));
    CHECK_OK(rs.remember(g.ctx, sb.srv, sb.proj, world, 100));
    CHECK_EQ(rs.historyStats().absorbed, 2u);
    // camera window C on the left copy: A moved by -3583 covers x -2175..-1343
    const double C[4] = {-2112, -1280, -64, 640};
    float to[16];
    windowProjFromBox(C[0], C[1], C[2], C[3], to);
    CHECK_OK(rs.resample(g.ctx, sb.srv, sb.proj, to));
    Image out;
    CHECK(rs.srv() && readMip(g, rs.srv(), 0, out));
    if (out.px.empty()) return;
    const Tally wrapped = tallyBlocks(out, to, 0, [&](double x, double y) {
        return inBox(x + 3583.0, y, A[0], A[1], A[2], A[3]) && outBox(x, y, B[0], B[1], B[2], B[3]);
    });
    std::printf("  wrap seam: %d/%d blocks from the history's other copy\n", wrapped.match, wrapped.n);
    CHECK(wrapped.n >= 20 && wrapped.match == wrapped.n);
    // the same overlays on a map that does not wrap: the ground past x1 + 128 is not the map's; nothing wraps
    OverlayResampler flat;
    for (uint64_t k = 1; k <= OverlayResampler::kRefreshCalls + 1; ++k)
        CHECK_OK(flat.remember(g.ctx, sa.srv, sa.proj, tinyWorld(1, false), k));
    CHECK_OK(flat.remember(g.ctx, sb.srv, sb.proj, tinyWorld(1, false), 100));
    CHECK_OK(flat.resample(g.ctx, sb.srv, sb.proj, to));
    CHECK(flat.srv() && readMip(g, flat.srv(), 0, out));
    const Tally none = tallyBlocks(out, to, 0, [&](double x, double y) {
        return x < -1791.5 - 160 && outBox(x, y, B[0], B[1], B[2], B[3]);
    });
    CHECK(none.n >= 4 && none.clear == none.n);
}

// remember()'s refusals leave the history as it was; the refresh rule (the same window drawn again only every
// kRefreshCalls new keys).
C5R_TEST(overlay_history_refusals_and_refresh)
{
    Gpu g;
    CHECK(g.dev != nullptr);
    if (!g.dev) return;
    WorldSource sa;
    CHECK(sa.make(g, 64, -1600, -768, -64, 640));
    if (!sa.srv) return;
    OverlayResampler rs;
    OverlayWorld bad = tinyWorld();
    bad.x1 = bad.x0;
    CHECK_ERR(rs.remember(g.ctx, sa.srv, sa.proj, bad, 1), Err::BadLayout);
    bad = tinyWorld();
    bad.y0 = std::nan("");
    CHECK_ERR(rs.remember(g.ctx, sa.srv, sa.proj, bad, 1), Err::BadLayout);
    bad = tinyWorld();
    bad.wrapPeriod = -1;
    CHECK_ERR(rs.remember(g.ctx, sa.srv, sa.proj, bad, 1), Err::BadLayout);
    CHECK_ERR(rs.remember(g.ctx, nullptr, sa.proj, tinyWorld(), 1), Err::BadLayout);
    float skew[16];
    std::memcpy(skew, sa.proj, sizeof skew);
    skew[4] = 0.01f;
    CHECK_ERR(rs.remember(g.ctx, sa.srv, skew, tinyWorld(), 1), Err::BadLayout);
    CHECK_EQ(rs.historyStats().rememberFails, 5u);
    CHECK_EQ(rs.historyStats().absorbed, 0u);
    // one window: the first drawn at call kRefreshCalls + 1 (the first window after a load waits), then every kRefreshCalls;
    // nothing drawn before (no texture made: width 0)
    for (uint64_t k = 1; k <= OverlayResampler::kRefreshCalls; ++k) CHECK_OK(rs.remember(g.ctx, sa.srv, sa.proj, tinyWorld(), k));
    CHECK(rs.historyStats().absorbed == 0 && rs.historyStats().width == 0u);
    for (uint64_t k = OverlayResampler::kRefreshCalls + 1; k <= 2 * OverlayResampler::kRefreshCalls + 1; ++k)
        CHECK_OK(rs.remember(g.ctx, sa.srv, sa.proj, tinyWorld(), k));
    const OverlayHistoryStats st = rs.historyStats();
    std::printf("  %u calls with one window: %llu drawn, %llu skipped\n", 2 * OverlayResampler::kRefreshCalls + 1,
                static_cast<unsigned long long>(st.absorbed), static_cast<unsigned long long>(st.skipped));
    CHECK_EQ(st.absorbed, 2u);
    CHECK_EQ(st.absorbed + st.skipped, 2u * OverlayResampler::kRefreshCalls + 1);
    // a huge map (128 x 80: 8191 x 5541 world units) stays within 16 M texels
    OverlayWorld huge;
    huge.x0 = -4095.5;
    huge.x1 = 4095.5;
    huge.y0 = -2770.5;
    huge.y1 = 2770.5;
    huge.wrapPeriod = 8191;
    huge.key = 9;
    CHECK_OK(rs.remember(g.ctx, sa.srv, sa.proj, huge, 999));   // the first window: waits
    float moved[16];
    windowProjFromBox(-1536, -704, -64, 640, moved);
    CHECK_OK(rs.remember(g.ctx, sa.srv, moved, huge, 1000));   // a move: drawn, the history made for the huge map
    const OverlayHistoryStats sh = rs.historyStats();
    CHECK(sh.width > 3583u);
    std::printf("  huge map history %ux%u (%.3f texels per world unit, %.1f MB with the held mask)\n", sh.width, sh.height, sh.texelsPerUnit,
                sh.width * static_cast<double>(sh.height) * 5.0 / 1048576.0);
    CHECK(static_cast<double>(sh.width) * sh.height <= 16.0 * 1048576 + 16384);
    rs.release();
    CHECK_EQ(rs.historyStats().width, 0u);
}

// The cost of remember() (a window's quad into the history) and of resample() with a history, at the overlay's live
// size; printed, not asserted (an indication under docs/briefs/PERFORMANCE.md, no speed claim).
C5R_TEST(overlay_history_gpu_time)
{
    Gpu g;
    CHECK(g.dev != nullptr);
    if (!g.dev) return;
    WorldSource sa, sb;
    CHECK(sa.make(g, 2048, -1600, -768, -64, 640));
    CHECK(sb.make(g, 2048, -1536, -704, -64, 640));
    if (!sa.srv || !sb.srv) return;
    int n = 200;
    if (const char* e = std::getenv("C5R_RESAMPLE_TIMING_N")) n = std::max(10, std::atoi(e));
    D3D11_QUERY_DESC qd = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    ID3D11Query *dis = nullptr, *t0 = nullptr, *t1 = nullptr;
    CHECK(SUCCEEDED(g.dev->CreateQuery(&qd, &dis)));
    qd.Query = D3D11_QUERY_TIMESTAMP;
    CHECK(SUCCEEDED(g.dev->CreateQuery(&qd, &t0)) && SUCCEEDED(g.dev->CreateQuery(&qd, &t1)));
    if (!dis || !t0 || !t1) return;
    OverlayResampler rs;
    float to[16];
    windowProjFromBox(-1664, -768, -128, 640, to);
    for (int arm = 0; arm < 2; ++arm) {
        std::vector<double> ms;
        for (int i = 0; i < n + 5; ++i) {
            const WorldSource& s = (i & 1) ? sb : sa;   // alternating windows: every remember() draws
            g.ctx->Begin(dis);
            g.ctx->End(t0);
            const Status st = arm == 0 ? rs.remember(g.ctx, s.srv, s.proj, tinyWorld(), static_cast<uint64_t>(i + 1))
                                       : rs.resample(g.ctx, sb.srv, sb.proj, to);
            g.ctx->End(t1);
            g.ctx->End(dis);
            CHECK_OK(st);
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj = {};
            UINT64 a = 0, b = 0;
            while (g.ctx->GetData(dis, &dj, sizeof dj, 0) == S_FALSE) {}
            while (g.ctx->GetData(t0, &a, sizeof a, 0) == S_FALSE) {}
            while (g.ctx->GetData(t1, &b, sizeof b, 0) == S_FALSE) {}
            if (i >= 5 && !dj.Disjoint && dj.Frequency && b >= a) ms.push_back(static_cast<double>(b - a) * 1000.0 / dj.Frequency);
        }
        CHECK(!ms.empty());
        if (ms.empty()) break;
        std::sort(ms.begin(), ms.end());
        auto q = [&](double f) { return ms[std::min(ms.size() - 1, static_cast<size_t>(f * (ms.size() - 1) + 0.5))]; };
        std::printf("  %s: gpu ms median %.4f p10 %.4f p90 %.4f p99 %.4f max %.4f, n %zu\n",
                    arm == 0 ? "remember 2048^2 window into the Tiny history" : "resample 2048^2 with the history", q(0.5), q(0.1),
                    q(0.9), q(0.99), ms.back(), ms.size());
    }
    dis->Release();
    t0->Release();
    t1->Release();
}

C5R_TEST(derived_range_when_none_given)
{
    Ready a;
    if (!a.ok) return;
    Scene sc(8, 8, PlotState::Visible);
    sc.view.x0 = sc.view.x1 = 0;   // derive from the window (the map's box plus one plot)
    sc.sync();
    MaskStats st;
    CHECK_OK(a.r.build(a.g.ctx, sc.in, &st));
    CHECK_EQ(st.hexagons[1], 64u);
    CHECK(st.hexagons[2] > 0u);   // the rim outside the map, unexplored
}

C5R_TEST(fow_bits_conversion)
{
    const uint8_t bits[6] = {0, 1, 2, 3, 0x81, 0xF0};
    PlotState out[6];
    CHECK_EQ(convertFowBits(bits, 6, out), 2u);   // 0x81 and 0xF0 carry bits above 1
    CHECK(out[0] == PlotState::Unexplored);
    CHECK(out[1] == PlotState::Seen);
    CHECK(out[2] == PlotState::Visible);
    CHECK(out[3] == PlotState::Visible);
    CHECK(out[4] == PlotState::Seen);
    CHECK(out[5] == PlotState::Unexplored);
    CHECK_EQ(convertFowBits(nullptr, 6, out), 0u);
}

C5R_TEST(negative_init)
{
    Gpu g;
    MaskRenderer r;
    const std::wstring exe = exeOrSkip();
    CHECK_ERR(r.init(nullptr, exe, 1024), Err::BadLayout);
    CHECK(g.dev != nullptr);
    if (!g.dev) return;
    CHECK_ERR(r.init(g.dev, exe, 300), Err::BadLayout);   // wrong target size
    CHECK_ERR(r.init(g.dev, exe, 2048), Err::BadLayout);
    CHECK(r.srv() == nullptr);
    CHECK_EQ(r.size(), 0u);
    CHECK_ERR(r.init(g.dev, L"C:/c5-64bit-work/no-such-dir/CivilizationV_DX11.exe", 1024), Err::Io);
    // an EXE file whose .shader section holds one valid program, but none of FOWDrawMask's
    const std::vector<uint8_t> vs = test::stripLikeExe(test::compileHlsl(
        "float4 main(float4 p : POSITION) : SV_Position { return p; }", "main", "vs_5_0"));
    CHECK(!vs.empty());
    std::vector<test::SynthSection> secs(2);
    secs[0].name = ".text";
    secs[0].rva = 0x1000;
    secs[0].virtualSize = 0x200;
    secs[0].data.assign(0x200, 0xCC);
    secs[1].name = ".shader";
    secs[1].rva = 0x2000;
    secs[1].virtualSize = static_cast<uint32_t>(vs.size());
    secs[1].data = vs;
    const std::vector<uint8_t> pe = test::buildPe(secs);
    const std::string path = test::workDir() + "/fow_mask_noprograms.exe";
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(pe.data()), static_cast<std::streamsize>(pe.size()));
    }
    const Status s = r.init(g.dev, wide(path), 1024);
    CHECK_ERR(s, Err::NotFound);
    CHECK(s.msg.find("FOWDrawMask blob 0x0677a9f8") != std::string::npos);
    std::printf("  no-programs EXE: %s\n", s.str().c_str());
    CHECK(r.srv() == nullptr);
    DeleteFileA(path.c_str());
    // build before init
    Scene sc(4, 4, PlotState::Visible);
    CHECK_ERR(r.build(g.ctx, sc.in, nullptr), Err::NotFound);
}

C5R_TEST(negative_build_inputs_keep_the_target)
{
    Ready a;
    if (!a.ok) return;
    Scene sc(8, 8, PlotState::Visible);
    CHECK_OK(a.r.build(a.g.ctx, sc.in, nullptr));
    float cx, cy;
    centre(sc.map, 3, 3, cx, cy);
    const float good[16] = {sc.view.overlayProj[0], sc.view.overlayProj[1], sc.view.overlayProj[2], sc.view.overlayProj[3],
                            sc.view.overlayProj[4], sc.view.overlayProj[5], sc.view.overlayProj[6], sc.view.overlayProj[7],
                            sc.view.overlayProj[8], sc.view.overlayProj[9], sc.view.overlayProj[10], sc.view.overlayProj[11],
                            sc.view.overlayProj[12], sc.view.overlayProj[13], sc.view.overlayProj[14], sc.view.overlayProj[15]};
    auto still = [&](int line) {
        Image im;
        if (!readMip(a.g, a.r.srv(), 0, im)) { test::fail(__FILE__, line, "readback"); return; }
        if (!eq4(texel(im, good, cx, cy), 255, 0, 255, 255)) test::fail(__FILE__, line, "the target changed after a refused build");
    };
    MaskStats st;
    // NaN and zero-scale OverlayProj
    sc.view.overlayProj[12] = std::nanf("");
    CHECK_ERR(a.r.build(a.g.ctx, sc.in, &st), Err::BadLayout);
    CHECK_EQ(st.draws, 0u);
    still(__LINE__);
    std::memcpy(sc.view.overlayProj, good, sizeof good);
    sc.view.overlayProj[5] = 0.0f;
    CHECK_ERR(a.r.build(a.g.ctx, sc.in, &st), Err::BadLayout);
    still(__LINE__);
    std::memcpy(sc.view.overlayProj, good, sizeof good);
    // a plot table of the wrong size
    sc.in.map.stateCount = 63;
    CHECK_ERR(a.r.build(a.g.ctx, sc.in, &st), Err::BadLayout);
    sc.in.map.stateCount = 65;
    CHECK_ERR(a.r.build(a.g.ctx, sc.in, &st), Err::BadLayout);
    sc.in.map.stateCount = 64;
    // out-of-range state
    sc.s[17] = static_cast<PlotState>(7);
    CHECK_ERR(a.r.build(a.g.ctx, sc.in, &st), Err::BadLayout);
    still(__LINE__);
    sc.s[17] = PlotState::Visible;
    // null states, zero size, too big
    sc.in.map.states = nullptr;
    CHECK_ERR(a.r.build(a.g.ctx, sc.in, &st), Err::BadLayout);
    sc.in.map.states = sc.s.data();
    sc.in.map.width = 0;
    CHECK_ERR(a.r.build(a.g.ctx, sc.in, &st), Err::BadLayout);
    sc.in.map.width = 8;
    // placement missing or not finite
    sc.in.map.hexToWorld[15] = 0.0f;
    CHECK_ERR(a.r.build(a.g.ctx, sc.in, &st), Err::BadLayout);
    sc.in.map.hexToWorld[15] = 1.0f;
    sc.in.map.gridOrigin[1] = INFINITY;
    CHECK_ERR(a.r.build(a.g.ctx, sc.in, &st), Err::BadLayout);
    sc.in.map.gridOrigin[1] = sc.map.gridOrigin[1];
    // views
    sc.in.numViews = 0;
    CHECK_ERR(a.r.build(a.g.ctx, sc.in, &st), Err::BadLayout);
    sc.in.numViews = kMaxViews + 1;
    CHECK_ERR(a.r.build(a.g.ctx, sc.in, &st), Err::BadLayout);
    sc.in.numViews = 1;
    sc.in.views = nullptr;
    CHECK_ERR(a.r.build(a.g.ctx, sc.in, &st), Err::BadLayout);
    sc.in.views = &sc.view;
    sc.view.x0 = -10000;
    CHECK_ERR(a.r.build(a.g.ctx, sc.in, &st), Err::BadLayout);
    sc.view.x0 = 0;
    CHECK_ERR(a.r.build(nullptr, sc.in, &st), Err::BadLayout);
    still(__LINE__);
    // and the same input is accepted again
    CHECK_OK(a.r.build(a.g.ctx, sc.in, &st));
    CHECK_EQ(st.hexagons[1], 64u);
}

C5R_TEST_MAIN
