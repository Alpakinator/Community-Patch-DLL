// c5r::fow::ParticleRenderer with the EXE's FOWParticleShader (C5R_EXE) and the install's fx_fog_dif.dds, flowfield.dds and
// FluidFOWSettings.xml (C5R_GAME_DIR), ticket 0340. Without them the executable exits 77 (SKIP) and render/build.py --test
// fails the run. Checks: init reads the three files (every setting read, the atlas 8 x 8, the flow field 1024 x 1024);
// the sprites draw where the mask's G is 1 and leave the target untouched where G is 0 or below 0.01, and where the depth
// is 0 (FOW CULL's zeros); two renderers with the same seed and camera draw byte-equal pictures over 30 frames (a run is
// repeatable); negative tests: no reset, no mask or depth, wrong views, a matrix not finite, an install without the files.
// Own code; the game's bytes are read at run time only.
#include <c5r/fow/fow.h>
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "cloud_testutil.h"

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

const int kPx = 256;

// a 16 x 12 map's world (cloud_game_test's), a top-down orthographic view over world [-512, 512]^2, the overlay window
// equal to it
CloudWorld smallWorld()
{
    CloudWorld w;
    const float m[16] = {64, 0, 0, 0, 32, 64, 0, 0, 0, 0, 0, 0, 32, 128, 0, 1};
    std::memcpy(w.hexToWorld, m, sizeof m);
    w.samplesX = 16 * 64;
    w.samplesY = 16 * 64;
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
    return v;
}

// a perspective view (row vectors, D3D's left-handed clip space, 60 degrees, aspect 1) from eye to target, z up
void perspectiveView(float ex, float ey, float ez, float tx, float ty, float tz, float out[16])
{
    float f[3] = {tx - ex, ty - ey, tz - ez};
    const float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (float& c : f) c /= fl;
    const float up[3] = {0, 0, 1};
    float r[3] = {up[1] * f[2] - up[2] * f[1], up[2] * f[0] - up[0] * f[2], up[0] * f[1] - up[1] * f[0]};
    const float rl = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    for (float& c : r) c /= rl;
    const float u[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]};
    const float e[3] = {ex, ey, ez};
    float view[16] = {r[0], u[0], f[0], 0, r[1], u[1], f[1], 0, r[2], u[2], f[2], 0, 0, 0, 0, 1};
    view[12] = -(e[0] * r[0] + e[1] * r[1] + e[2] * r[2]);
    view[13] = -(e[0] * u[0] + e[1] * u[1] + e[2] * u[2]);
    view[14] = -(e[0] * f[0] + e[1] * f[1] + e[2] * f[2]);
    const float ys = 1.0f / std::tan(0.5236f), zn = 10.0f, zf = 5000.0f;
    const float proj[16] = {ys, 0, 0, 0, 0, ys, 0, 0, 0, 0, zf / (zf - zn), 1, 0, 0, -zn * zf / (zf - zn), 0};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float acc = 0;
            for (int k = 0; k < 4; ++k) acc += view[i * 4 + k] * proj[k * 4 + j];
            out[i * 4 + j] = acc;
        }
}

uint32_t changed(const std::vector<uint8_t>& img, int x0, int x1, int y0, int y1, uint32_t* total)
{
    uint32_t n = 0, t = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const uint8_t* p = &img[(static_cast<size_t>(y) * kPx + x) * 4];
            ++t;
            n += !(p[0] == 64 && p[1] == 64 && p[2] == 64);
        }
    if (total) *total = t;
    return n;
}

} // namespace

int main()
{
    const std::wstring exe = envW("C5R_EXE"), game = envW("C5R_GAME_DIR");
    if (exe.empty() || game.empty()) {
        std::printf("SKIP particles_game: C5R_EXE / C5R_GAME_DIR not set (the program and files come from the player's)\n");
        return 77;
    }
    cloudtest::Device d;
    if (!d.create()) {
        std::printf("FAIL no D3D11 device\n");
        return 1;
    }
    ParticleRenderer r;
    Status s = r.init(d.dev, exe, game);
    std::printf("init: %s\n", s.str().c_str());
    CHECK(s.ok());
    if (!s.ok()) return 1;
    const ParticleSettings& st = r.settings();
    std::printf("settings: spawn %d size %.3g [%.3g, %.3g] sd %.3g mass %.3g [%.3g, %.3g] sd %.3g corr %.3g fog %.3g var %.3g "
                "density %.3g speed %.3g drag %.3g master %.3g force %.3g rotate %.3g\n",
                st.spawnFrequency, st.sizeMean, st.sizeMin, st.sizeMax, st.sizeDeviation, st.massMean, st.massMin, st.massMax,
                st.massDeviation, st.massSizeCorrelation, st.fogHeight, st.heightVariation, st.fluidDensity, st.speed, st.drag,
                st.masterSize, st.forceStrength, st.maxRotate);
    // the install's file differs from the EXE's defaults in every value of both groups [L, fog-of-war.md 10.1]: a group
    // that stopped early would leave its last values at the defaults
    const ParticleSettings def;
    CHECK(st.heightVariation != def.heightVariation && st.fogHeight != def.fogHeight && st.maxRotate != def.maxRotate);
    CHECK(r.reset(1000.0f, 900.0f, 1).ok());
    std::printf("reset: %u particles\n", r.sim().stats().particles);
    CHECK(r.sim().stats().particles > 1000);

    cloudtest::Target t;
    CHECK(t.create(d.dev, kPx, kPx));
    const float baseF[4] = {64 / 255.0f, 64 / 255.0f, 64 / 255.0f, 1.0f};
    View view = orthoView();
    ParticleInput in;
    in.colour = t.rtv;
    in.depth = t.dsv;
    in.width = in.height = kPx;
    in.views = &view;
    in.numViews = 1;
    in.world = smallWorld();

    // 1. the gate: G = 1 on the left half, 0 on the right, 2/255 in a band
    {
        ID3D11ShaderResourceView* mask = cloudtest::makeMask(d.dev, 64, [](float u, float, uint8_t c[4]) {
            c[0] = 0; c[2] = 0; c[3] = 255;
            c[1] = u < 0.25f ? 255 : (u < 0.375f ? 2 : 0);
        });
        in.mask = mask;
        t.clear(d.ctx, baseF);
        ParticleStats ps;
        s = r.frame(d.ctx, in, &ps);
        std::printf("frame (gate): %s; %u sprites, %u moved\n", s.str().c_str(), ps.sprites, ps.moved);
        CHECK(s.ok() && ps.sprites > 0);
        std::vector<uint8_t> img;
        CHECK(t.readColour(d.dev, d.ctx, img));
        uint32_t tG1, tLow, tG0;
        const uint32_t cG1 = changed(img, 0, 58, 40, 216, &tG1), cLow = changed(img, 70, 90, 40, 216, &tLow),
                       cG0 = changed(img, 102, 248, 40, 216, &tG0);
        std::printf("G = 1: %u of %u changed; G = 2/255: %u of %u; G = 0: %u of %u\n", cG1, tG1, cLow, tLow, cG0, tG0);
        CHECK(cG1 > tG1 / 10);   // the sprites cover a part (the atlas alpha is not 1 everywhere)
        CHECK(cLow == 0 && cG0 == 0);
        // 2. depth 0 (FOW CULL's zeros): nothing drawn
        t.clear(d.ctx, baseF);
        d.ctx->ClearDepthStencilView(t.dsv, D3D11_CLEAR_DEPTH, 0.0f, 0);
        CHECK(r.frame(d.ctx, in, &ps).ok());
        CHECK(t.readColour(d.dev, d.ctx, img));
        uint32_t tAll;
        const uint32_t cAll = changed(img, 0, kPx, 0, kPx, &tAll);
        std::printf("depth 0: %u of %u changed\n", cAll, tAll);
        CHECK(cAll == 0);
        // 3. repeatable: two renderers, the same seed and camera, 30 frames, byte-equal pictures and states
        ParticleRenderer r2;
        CHECK(r2.init(d.dev, exe, game).ok());
        CHECK(r.reset(1000.0f, 900.0f, 11).ok() && r2.reset(1000.0f, 900.0f, 11).ok());
        bool equal = true;
        for (int f = 0; f < 30 && equal; ++f) {
            std::vector<uint8_t> a, b;
            t.clear(d.ctx, baseF);
            CHECK(r.frame(d.ctx, in, nullptr).ok());
            CHECK(t.readColour(d.dev, d.ctx, a));
            t.clear(d.ctx, baseF);
            CHECK(r2.frame(d.ctx, in, nullptr).ok());
            CHECK(t.readColour(d.dev, d.ctx, b));
            equal = a == b;
        }
        CHECK(equal);
        CHECK(r.sim().saveState() == r2.sim().saveState());
        std::printf("repeatable over 30 frames: %s\n", equal ? "yes" : "no");
        // 0692: two renderers that differ only in a diagnostic switch draw byte-equal pictures and keep byte-equal states
        // over 40 frames (the first frames of the same seed and camera)
        auto sameRun = [&](const char* var, const char* value, const char* label, const ParticleInput& pin) {
            ParticleRenderer x, y;
            _putenv_s(var, value);
            CHECK(x.init(d.dev, exe, game).ok());
            _putenv_s(var, "");
            CHECK(y.init(d.dev, exe, game).ok());
            CHECK(x.reset(1000.0f, 900.0f, 23).ok() && y.reset(1000.0f, 900.0f, 23).ok());
            bool same = true;
            uint32_t sprites = 0;
            for (int f = 0; f < 40 && same; ++f) {
                std::vector<uint8_t> a, b;
                ParticleStats sa, sb;
                t.clear(d.ctx, baseF);
                CHECK(x.frame(d.ctx, pin, &sa).ok());
                CHECK(t.readColour(d.dev, d.ctx, a));
                t.clear(d.ctx, baseF);
                CHECK(y.frame(d.ctx, pin, &sb).ok());
                CHECK(t.readColour(d.dev, d.ctx, b));
                same = a == b && std::memcmp(&sa, &sb, sizeof sa) == 0;
                sprites = sa.sprites;
            }
            same = same && x.sim().saveState() == y.sim().saveState();
            std::printf("%s: %s over 40 frames (%u sprites, step threads %u and %u)\n", label, same ? "equal" : "DIFFERENT",
                        sprites, x.sim().stepThreads(), y.sim().stepThreads());
            CHECK(same);
        };
        // cut A: the step on one thread against the default count
        sameRun("C5_RENDER_PARTICLE_THREADS", "1", "step on 1 thread vs the default", in);
        sameRun("C5_RENDER_PARTICLE_THREADS", "7", "step on 7 threads vs the default", in);
        // cut D: every emitted sprite drawn (the cull off) against the default (sprites no view shows left out), with the
        // mask G = 1 everywhere: the view above, two views (a wrap-like copy shifted by 700 world units), a perspective view
        // seen from the south-west, low over the map, and the same with a second view behind its left edge
        {
            ID3D11ShaderResourceView* open = cloudtest::makeMask(d.dev, 64, [](float, float, uint8_t c[4]) {
                c[0] = 0; c[1] = 255; c[2] = 0; c[3] = 255;
            });
            ParticleInput o = in;
            o.mask = open;
            sameRun("C5_RENDER_PARTICLE_CULL", "0", "cull off vs on, one view", o);
            View two[2] = {view, view};
            two[1].viewProj[12] += 700.0f / 512.0f;
            o.views = two;
            o.numViews = 2;
            sameRun("C5_RENDER_PARTICLE_CULL", "0", "cull off vs on, two views", o);
            View persp[2];
            perspectiveView(-420.0f, -620.0f, 260.0f, 120.0f, 80.0f, 0.0f, persp[0].viewProj);
            perspectiveView(380.0f, -300.0f, 180.0f, -150.0f, 200.0f, 0.0f, persp[1].viewProj);
            std::memcpy(persp[0].overlayProj, view.overlayProj, 64);
            std::memcpy(persp[1].overlayProj, view.overlayProj, 64);
            o.views = persp;
            o.numViews = 1;
            sameRun("C5_RENDER_PARTICLE_CULL", "0", "cull off vs on, perspective", o);
            o.numViews = 2;
            sameRun("C5_RENDER_PARTICLE_CULL", "0", "cull off vs on, two perspective views", o);
            open->Release();
        }
        // negatives
        ParticleInput bad = in;
        bad.mask = nullptr;
        CHECK(r.frame(d.ctx, bad, nullptr).code == Err::BadLayout);
        bad = in;
        bad.depth = nullptr;
        CHECK(r.frame(d.ctx, bad, nullptr).code == Err::BadLayout);
        bad = in;
        bad.numViews = 0;
        CHECK(r.frame(d.ctx, bad, nullptr).code == Err::BadLayout);
        View nanView = view;
        nanView.viewProj[0] = NAN;
        bad = in;
        bad.views = &nanView;
        CHECK(r.frame(d.ctx, bad, nullptr).code == Err::BadLayout);
        mask->Release();
    }
    {
        ParticleRenderer none;
        CHECK(none.frame(d.ctx, in, nullptr).code == Err::NotFound);
        CHECK(none.reset(100, 100, 1).code == Err::NotFound);
        // an install without the files: an empty folder with Assets/ and Resource/
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        const std::wstring root = std::wstring(tmp) + L"c5r_particles_empty_" + std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(root.c_str(), nullptr);
        CreateDirectoryW((root + L"\\Assets").c_str(), nullptr);
        CreateDirectoryW((root + L"\\Resource").c_str(), nullptr);
        Status e = none.init(d.dev, exe, root);
        std::printf("empty install: %s\n", e.str().c_str());
        CHECK(e.code == Err::NotFound);
        RemoveDirectoryW((root + L"\\Assets").c_str());
        RemoveDirectoryW((root + L"\\Resource").c_str());
        RemoveDirectoryW(root.c_str());
        // an EXE path that does not exist
        CHECK(!none.init(d.dev, L"C:\\no\\such\\exe.exe", game).ok());
    }
    std::printf("%s (%d failures)\n", g_fail ? "FAILED" : "PASSED", g_fail);
    return g_fail ? 1 : 0;
}
