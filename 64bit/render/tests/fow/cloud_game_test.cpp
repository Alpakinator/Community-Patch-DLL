// c5r::fow::CloudRenderer with the EXE's own programs (C5R_EXE) and the install's cloud textures (C5R_GAME_DIR), task 3955.
// Without them the executable exits 77 (SKIP) and render/build.py --test fails the run. Checks: init loads the three
// cloud files (1024 x 1024; t5 = _02); the planes draw where the mask's G is 1 and leave the target untouched where G is 0
// or below 0.01 (G = 2 / 255); FOW CULL writes depth 0 over the culled plots and the planes fail there; the counts;
// negative tests: the cloud textures missing (an empty install), the programs missing (an EXE without a .shader section),
// wrong views, a missing mask or depth, non-finite inputs. Own code; the game's bytes are read at run time only.
#include <c5r/fow/fow.h>
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "cloud_testutil.h"
#include "testutil.h"   // render/tests/shaders: buildPe

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

using namespace c5r;
using namespace c5r::fow;

namespace {

std::wstring envW(const char* name)
{
    const char* v = std::getenv(name);
    if (!v || !*v) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, v, -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, v, -1, &w[0], n);
    return w;
}

// A 16 x 12 map (samples 1024 x 1024: 4 margin rows), nominal M_h2w, centred world box; an orthographic top-down
// view-projection over world [-512, 512]^2 (clip = (x / 512, y / 512, 0.5 - z / 1000, 1)) and the overlay window equal to
// it, so that mask texel (u, v) lies under world (1024 u - 512, 512 - 1024 v).
const uint32_t kW = 16, kH = 12;
const int kPx = 256;

CloudWorld smallWorld()
{
    CloudWorld w;
    const float m[16] = {64, 0, 0, 0, 32, 64, 0, 0, 0, 0, 0, 0, 32, 128, 0, 1};
    std::memcpy(w.hexToWorld, m, sizeof m);
    w.sx = 1.0f;
    w.sy = 0.866f;
    w.samplesX = kW * 64;
    w.samplesY = (kH + 4) * 64;
    w.ox = kW * 64 / 2.0f;
    w.oy = 0.866f * 64 * (kH + 4) / 2.0f;
    w.maxX = (w.samplesX - 1) / 2.0f;
    w.minX = -w.maxX;
    w.maxY = (w.samplesY - 1) * 0.866f / 2.0f;
    w.minY = -w.maxY;
    return w;
}

View orthoView()
{
    View v;
    const float vp[16] = {1 / 512.0f, 0, 0, 0, 0, 1 / 512.0f, 0, 0, 0, 0, -1 / 1000.0f, 0, 0, 0, 0.5f, 1};
    std::memcpy(v.viewProj, vp, sizeof vp);
    const float ov[16] = {1 / 512.0f, 0, 0, 0, 0, 1 / 512.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    std::memcpy(v.overlayProj, ov, sizeof ov);
    v.x0 = 0; v.x1 = kW - 1; v.y0 = 0; v.y1 = kH - 2;   // the cull loop runs to x1 + 1, y1 + 2: the whole map
    return v;
}

// pixel of world (x, y) under orthoView in a kPx target
int pxOf(float x) { return static_cast<int>((x / 512.0f * 0.5f + 0.5f) * kPx); }
int pyOf(float y) { return static_cast<int>((0.5f - y / 512.0f * 0.5f) * kPx); }

bool same(const std::vector<uint8_t>& img, int x, int y, const uint8_t ref[4])
{
    const uint8_t* p = &img[(static_cast<size_t>(y) * kPx + x) * 4];
    return p[0] == ref[0] && p[1] == ref[1] && p[2] == ref[2];
}

} // namespace

int main()
{
    const std::wstring exe = envW("C5R_EXE"), game = envW("C5R_GAME_DIR"), work = envW("C5R_WORK");
    if (exe.empty() || game.empty()) {
        std::printf("SKIP cloud_game: C5R_EXE / C5R_GAME_DIR not set (the programs and textures come from the player's files)\n");
        return 77;
    }
    cloudtest::Device d;
    if (!d.create()) {
        std::printf("FAIL no D3D11 device\n");
        return 1;
    }
    CloudRenderer r;
    Status s = r.init(d.dev, exe, game);
    std::printf("init: %s\n", s.str().c_str());
    CHECK(s.ok());
    if (!s.ok()) return 1;
    CHECK(r.cloudBase() != nullptr && r.cloudBase() == r.planeTexture(0) && r.planeTexture(3) == nullptr);
    for (uint32_t i = 0; i < 3; ++i) {
        ID3D11Resource* res = nullptr;
        r.planeTexture(i)->GetResource(&res);
        D3D11_TEXTURE2D_DESC td = {};
        static_cast<ID3D11Texture2D*>(res)->GetDesc(&td);
        res->Release();
        std::printf("plane %u texture %u x %u, %u mips, format %d\n", i, td.Width, td.Height, td.MipLevels, td.Format);
        CHECK(td.Width == 1024 && td.Height == 1024);
        // [L 3951] plane 1 = _02 BC1, plane 2 = _03 BC3, plane 3 = _01 BC3
        CHECK(td.Format == (i == 0 ? DXGI_FORMAT_BC1_UNORM : DXGI_FORMAT_BC3_UNORM));
    }

    cloudtest::Target t;
    CHECK(t.create(d.dev, kPx, kPx));
    const float baseF[4] = {64 / 255.0f, 64 / 255.0f, 64 / 255.0f, 1.0f};
    const uint8_t base[4] = {64, 64, 64, 255};
    const CloudWorld world = smallWorld();
    View view = orthoView();
    std::vector<PlotState> states(static_cast<size_t>(kW) * kH, PlotState::Unexplored);
    float anim[4];
    fowAnim(world.maxX - world.minX, 3.0, anim);

    // 1. the mask gate: G = 1 on the left half of the window, 0 on the right, 2/255 in the band u in [0.25, 0.375)
    //    (all plots unexplored: no plot is culled)
    {
        ID3D11ShaderResourceView* mask = cloudtest::makeMask(d.dev, 64, [](float u, float, uint8_t c[4]) {
            c[0] = 0; c[2] = 0; c[3] = 255;
            c[1] = u < 0.25f ? 255 : (u < 0.375f ? 2 : 0);
        });
        CHECK(mask != nullptr);
        CloudInput in;
        in.colour = t.rtv;
        in.depth = t.dsv;
        in.width = in.height = kPx;
        in.mask = mask;
        in.geometry.map.states = states.data();
        in.geometry.map.width = kW;
        in.geometry.map.height = kH;
        in.geometry.views = &view;
        in.geometry.numViews = 1;
        std::memcpy(in.fowAnim, anim, sizeof anim);
        in.world = world;
        t.clear(d.ctx, baseF);
        CloudStats st;
        s = r.draw(d.ctx, in, &st);
        std::printf("draw (gate): %s; cull %u, planes %u / %u / %u quads, %u draws\n", s.str().c_str(), st.cullQuads,
                    st.planeQuads[0], st.planeQuads[1], st.planeQuads[2], st.draws);
        CHECK(s.ok());
        CHECK(st.cullQuads == 0);
        CHECK(st.planeQuads[0] > 0 && st.planeQuads[0] == st.planeQuads[1] && st.planeQuads[1] == st.planeQuads[2]);
        CHECK(st.draws == 3 * ((st.planeQuads[0] + 23) / 24));
        std::vector<uint8_t> img;
        CHECK(t.readColour(d.dev, d.ctx, img));
        int changedG1 = 0, totalG1 = 0, changedG0 = 0, totalG0 = 0, changedLow = 0, totalLow = 0;
        for (int y = 24; y < 232; ++y)   // inside the world box's y (+-443 world units)
            for (int x = 0; x < kPx; ++x) {
                const float u = (x + 0.5f) / kPx;
                const bool ch = !same(img, x, y, base);
                if (u < 0.23f) { ++totalG1; changedG1 += ch; }
                else if (u > 0.27f && u < 0.355f) { ++totalLow; changedLow += ch; }
                else if (u > 0.395f && u < 0.97f) { ++totalG0; changedG0 += ch; }   // u near 1 wraps onto G = 1
            }
        std::printf("G = 1: %d of %d changed; G = 2/255: %d of %d; G = 0: %d of %d\n", changedG1, totalG1, changedLow,
                    totalLow, changedG0, totalG0);
        CHECK(changedG1 > totalG1 * 99 / 100);
        CHECK(changedLow == 0);   // discard below 0.01
        CHECK(changedG0 == 0);
        mask->Release();
    }

    // 2. FOW CULL: plots 0..11 explored, 12..15 unexplored: flags 1 for x in [2, 9] (x - 2 wraps into 14, 15 for x < 2),
    //    rows 2..10; the mask's G is 1 everywhere, so the planes cover everything except the culled quads.
    {
        for (uint32_t y = 0; y < kH; ++y)
            for (uint32_t x = 0; x < kW; ++x) states[y * kW + x] = x < 12 ? PlotState::Visible : PlotState::Unexplored;
        ID3D11ShaderResourceView* mask = cloudtest::makeMask(d.dev, 16, [](float, float, uint8_t c[4]) {
            c[0] = 0; c[1] = 255; c[2] = 0; c[3] = 255;
        });
        CloudInput in;
        in.colour = t.rtv;
        in.depth = t.dsv;
        in.width = in.height = kPx;
        in.mask = mask;
        in.geometry.map.states = states.data();
        in.geometry.map.width = kW;
        in.geometry.map.height = kH;
        in.geometry.views = &view;
        in.geometry.numViews = 1;
        std::memcpy(in.fowAnim, anim, sizeof anim);
        in.world = world;
        t.clear(d.ctx, baseF);
        CloudStats st;
        s = r.draw(d.ctx, in, &st);
        std::printf("draw (cull): %s; cull %u quads\n", s.str().c_str(), st.cullQuads);
        CHECK(s.ok());
        CHECK(st.cullQuads == 8 * 9);   // x 2..9, y 2..10
        std::vector<uint8_t> img;
        std::vector<float> depth;
        CHECK(t.readColour(d.dev, d.ctx, img));
        CHECK(t.readDepth(d.dev, d.ctx, depth));
        // a culled plot's quad centre: kept colour, depth 0; an unexplored plot's: covered
        float hw[2];
        hexToWorldXY(world, 5, 6, hw);
        const float cx = hw[0] + 32.0f, cy = hw[1] + 0.866f * 32.0f - 128.0f;
        const int px = pxOf(cx), py = pyOf(cy);
        std::printf("culled quad (5, 6) at pixel (%d, %d): depth %.3f\n", px, py, depth[static_cast<size_t>(py) * kPx + px]);
        CHECK(px > 0 && px < static_cast<int>(kPx) && py > 0 && py < static_cast<int>(kPx));
        CHECK(depth[static_cast<size_t>(py) * kPx + px] == 0.0f);
        CHECK(same(img, px, py, base));
        hexToWorldXY(world, 14, 6, hw);
        const int ux = pxOf(hw[0] + 32.0f), uy = pyOf(hw[1] + 0.866f * 32.0f - 128.0f);
        if (ux >= 0 && ux < static_cast<int>(kPx) && uy >= 0 && uy < static_cast<int>(kPx)) {
            CHECK(!same(img, ux, uy, base));
            CHECK(depth[static_cast<size_t>(uy) * kPx + ux] == 1.0f);
        }

        // negatives with otherwise valid inputs
        CloudInput b = in;
        b.geometry.numViews = 0;
        CHECK(r.draw(d.ctx, b).code == Err::BadLayout);
        b.geometry.numViews = kMaxViews + 1;
        CHECK(r.draw(d.ctx, b).code == Err::BadLayout);
        b = in;
        b.geometry.views = nullptr;
        CHECK(r.draw(d.ctx, b).code == Err::BadLayout);
        View nanView = view;
        nanView.viewProj[0] = std::nanf("");
        b = in;
        b.geometry.views = &nanView;
        CHECK(r.draw(d.ctx, b).code == Err::BadLayout);
        nanView = view;
        nanView.overlayProj[12] = std::nanf("");
        CHECK(r.draw(d.ctx, b).code == Err::BadLayout);
        b = in;
        b.mask = nullptr;
        CHECK(r.draw(d.ctx, b).code == Err::BadLayout);
        b = in;
        b.depth = nullptr;
        CHECK(r.draw(d.ctx, b).code == Err::BadLayout);
        b = in;
        b.width = 0;
        CHECK(r.draw(d.ctx, b).code == Err::BadLayout);
        b = in;
        b.fowAnim[2] = std::nanf("");
        CHECK(r.draw(d.ctx, b).code == Err::BadLayout);
        b = in;
        b.geometry.map.states = nullptr;
        CHECK(r.draw(d.ctx, b).code == Err::BadLayout);
        b = in;
        b.world.samplesY = 0;
        CHECK(r.draw(d.ctx, b).code == Err::BadLayout);
        CHECK(r.draw(nullptr, in).code == Err::BadLayout);
        mask->Release();
    }

    // 3. negative init: the cloud textures missing (an install with empty Assets/ and Resource/), the programs missing
    //    (this test's own executable: a PE without a .shader section)
    {
        std::wstring root = (work.empty() ? std::wstring(L"C:/c5-64bit-work/0168/3955") : work) + L"/cloud_empty_install";
        CreateDirectoryW(root.c_str(), nullptr);
        CreateDirectoryW((root + L"/Assets").c_str(), nullptr);
        CreateDirectoryW((root + L"/Resource").c_str(), nullptr);
        CloudRenderer e;
        s = e.init(d.dev, exe, root);
        std::printf("init without the cloud files: %s\n", s.str().c_str());
        CHECK(s.code == Err::NotFound);
        CHECK(e.cloudBase() == nullptr);
        // a synthetic i386 PE with a .text section only (testutil's buildPe), written to the work folder
        const std::vector<uint8_t> pe = c5r::test::buildPe({{".text", 0x1000, 0x10, std::vector<uint8_t>(0x10, 0xC3)}});
        const std::wstring pePath = root + L"/no_shader_section.exe";
        HANDLE f = CreateFileW(pePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD wr = 0;
        CHECK(f != INVALID_HANDLE_VALUE && WriteFile(f, pe.data(), static_cast<DWORD>(pe.size()), &wr, nullptr));
        if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
        s = e.init(d.dev, pePath, game);
        std::printf("init with an EXE without programs: %s\n", s.str().c_str());
        CHECK(s.code == Err::NotFound);
        DeleteFileW(pePath.c_str());
        CHECK(e.draw(d.ctx, CloudInput()).code == Err::NotFound);
    }

    std::printf("%s cloud_game (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
