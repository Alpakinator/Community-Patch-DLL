// c5r::bake::Residency and StandinBaker (issue 3407): product tests in the render suite. A hardware device and the EXE's
// terrain shaders (C5R_EXE, for c5r::terrain::Renderer, the residency's sink) are needed; without the EXE the tests that
// draw skip, which fails the run. Every validation path has a negative test asserting the Err.
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_3.h>
#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "c5r/bake/residency.h"
#include "c5r/bake/standin.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/terrain/terrain.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::bake;

namespace {

const uint32_t TX = 16, TY = 12;

// unbuffered output: a test that dies still shows which one it was
const int kUnbuffered = (std::setvbuf(stdout, nullptr, _IONBF, 0), 0);

// Hills (class 3), tile columns 0..1 deep (class 1: samples of columns 0..2 are 0), columns 14..15 class 2 (2550).
heightfield::HeightGrid field(bool bump = false) {
    heightfield::HeightGrid g;
    g.mapW = TX;
    g.mapH = TY;
    g.samplesX = TX * 64;
    g.samplesY = TY * 64;
    g.wrapX = false;
    g.sx = 1.0f;
    g.sy = 0.866f;
    g.sz = 0.2f;
    g.ox = TX * 32.0f;
    g.oy = TY * 32 * 0.866f;
    g.waterZ = 1.0f;
    g.h.resize(static_cast<size_t>(g.samplesX) * g.samplesY);
    for (uint32_t y = 0; y < g.samplesY; ++y)
        for (uint32_t x = 0; x < g.samplesX; ++x) {
            double v = 14000 + 1000 * std::sin(x / 50.0) * std::cos(y / 37.0);
            if (x < 3 * 64) v = 0;
            if (x >= (TX - 3) * 64) v = 2550;
            g.h[g.index(x, y)] = static_cast<uint16_t>(v);
        }
    if (bump) g.h[g.index(8 * 64 + 32, 6 * 64 + 32)] += 500;
    g.mat.assign(g.h.size(), 7);
    return g;
}
const uint32_t LAND = TX * TY - 2 * TY;   // class 2 + 3

struct Dev {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    Dev() {
        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx)))
            c5r::test::fail(__FILE__, __LINE__, "no D3D11 hardware device");
    }
    ~Dev() {
        if (ctx) ctx->Release();
        if (dev) dev->Release();
    }
};

void lookAt(const float eye[3], const float at[3], float out[16]) {
    float z[3] = {eye[0] - at[0], eye[1] - at[1], eye[2] - at[2]};
    float l = std::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
    for (float& v : z) v /= l;
    const float up[3] = {0, 0, 1};
    float x[3] = {up[1] * z[2] - up[2] * z[1], up[2] * z[0] - up[0] * z[2], up[0] * z[1] - up[1] * z[0]};
    l = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
    for (float& v : x) v /= l;
    const float y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
    const float m[16] = {x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0,
                         -(x[0] * eye[0] + x[1] * eye[1] + x[2] * eye[2]), -(y[0] * eye[0] + y[1] * eye[1] + y[2] * eye[2]),
                         -(z[0] * eye[0] + z[1] * eye[1] + z[2] * eye[2]), 1};
    std::memcpy(out, m, sizeof(m));
}

// A camera 180 above tile (tx, ty), looking at it from the south (as test_residency.py); height and back: another height
// and distance south (issue 0409: a high camera that sees most of the map).
ResidencyView view(uint32_t tx, uint32_t ty, float height = 180.0f, float back = 108.0f) {
    ResidencyView v;
    const float cx = (tx * 64 + 32) - TX * 32.0f, cy = (ty * 64 + 32) * 0.866f - TY * 32 * 0.866f;
    const float eye[3] = {cx, cy - back, height}, at[3] = {cx, cy, 10.0f};
    lookAt(eye, at, v.view);
    const float ys = 1.0f / std::tan(0.3927f), zn = 10.0f, zf = 4000.0f, aspect = 320.0f / 240.0f;
    const float p[16] = {ys / aspect, 0, 0, 0, 0, ys, 0, 0, 0, 0, zf / (zn - zf), -1, 0, 0, zn * zf / (zn - zf), 0};
    std::memcpy(v.proj, p, sizeof(p));
    v.width = 320;
    v.height = 240;
    return v;
}

// A baker that makes t8 the way PipelineBaker does when no listed layer has a spec map: only when the request says
// areaChanged (the EXE's tile+0x300, set on every re-bake). Wraps the stand-in.
class AreaT8Baker : public TileBaker {
public:
    explicit AreaT8Baker(StandinBaker& s) : s_(s) {}
    Status init(ID3D11Device* d, ID3D11DeviceContext* c) override { return s_.init(d, c); }
    Status setMap(const MapInputs& m) override { return s_.setMap(m); }
    Status bake(const BakeRequest& rq, BakedTile& out) override {
        BakeRequest r = rq;
        if (!rq.areaChanged) r.t8Target = BakeTarget();
        const Status st = s_.bake(r, out);
        if (st.ok() && !rq.areaChanged) {
            if (out.t8) out.t8->Release();
            out.t8 = nullptr;
            out.hasT8 = false;
        }
        return st;
    }

private:
    StandinBaker& s_;
};

struct Fixture {
    Dev d;
    terrain::Renderer r;
    heightfield::HeightGrid g = field();
    StandinBaker baker;
    AreaT8Baker areaBaker{baker};
    Residency res;
    std::vector<std::string> lines;
    static void logFn(void* c, const char* l) { static_cast<Fixture*>(c)->lines.push_back(l); }
    // t8OnlyWhenAreaChanged: the residency gets AreaT8Baker (the pipeline's t8 rule) instead of the stand-in itself.
    explicit Fixture(const ResidencyOptions& o, StandinFaults f = StandinFaults(), bool t8OnlyWhenAreaChanged = false)
        : baker(f) {
        const std::string exe = c5r::test::exePath();   // skips when C5R_EXE is not set
        CHECK_OK(r.init(d.dev, std::wstring(exe.begin(), exe.end()), L""));
        CHECK_OK(r.setHeightField(g, 1));
        CHECK_OK(baker.init(d.dev, d.ctx));
        CHECK_OK(res.init(t8OnlyWhenAreaChanged ? static_cast<TileBaker*>(&areaBaker) : &baker, o, &Fixture::logFn, this));
        CHECK_OK(res.onField(d.dev, g, 1, 1));
    }
    bool logged(const char* s) const {
        for (auto& l : lines)
            if (l.find(s) != std::string::npos) return true;
        return false;
    }
    void idleUntilAhead(int maxCalls = 4000) {
        for (int i = 0; i < maxCalls && !res.stats().aheadDone; ++i) {
            Sleep(1);
            res.idle(d.ctx, 50);
        }
    }
};

ResidencyOptions fast() {
    ResidencyOptions o;
    o.rate = 100000;
    o.burst = 64;
    o.arrayChunk = 16;
    return o;
}

}  // namespace

// ---------------------------------------------------------------- validation (no device needed beyond the baker)

C5R_TEST(init_refuses_bad_options) {
    Residency r;
    StandinBaker b{StandinFaults()};
    ResidencyOptions o;
    CHECK_ERR(r.init(nullptr, o), Err::BadLayout);
    o.radius = 129;
    CHECK_ERR(r.init(&b, o), Err::BadLayout);
    o = ResidencyOptions();
    o.radius = -2;
    CHECK_ERR(r.init(&b, o), Err::BadLayout);
    o = ResidencyOptions();
    o.rate = 0;
    CHECK_ERR(r.init(&b, o), Err::BadLayout);
    o = ResidencyOptions();
    o.burst = -1;
    CHECK_ERR(r.init(&b, o), Err::BadLayout);
    o = ResidencyOptions();
    o.gpuMsPerS = -1;
    CHECK_ERR(r.init(&b, o), Err::BadLayout);
    o = ResidencyOptions();
    o.memoryLimitMb = -1;
    CHECK_ERR(r.init(&b, o), Err::BadLayout);
    o = ResidencyOptions();
    o.arrayChunk = 0;
    CHECK_ERR(r.init(&b, o), Err::BadLayout);
    o = ResidencyOptions();
    o.sizeClass = 100;
    CHECK_ERR(r.init(&b, o), Err::BadLayout);
    CHECK_OK(r.init(&b, ResidencyOptions()));
}

C5R_TEST(onfield_refuses_before_init_and_bad_input) {
    Dev d;
    heightfield::HeightGrid g = field();
    Residency r;
    CHECK_ERR(r.onField(d.dev, g, 1, 1), Err::NotFound);
    StandinBaker b{StandinFaults()};
    CHECK_OK(b.init(d.dev, d.ctx));
    CHECK_OK(r.init(&b, ResidencyOptions()));
    CHECK_ERR(r.onField(d.dev, g, 2, 1), Err::Unsupported);
    CHECK_ERR(r.onField(nullptr, g, 1, 1), Err::BadLayout);
    heightfield::HeightGrid bad = g;
    bad.h.pop_back();
    CHECK_ERR(r.onField(d.dev, bad, 1, 1), Err::BadLayout);
    CHECK(!r.hasField());
    CHECK_OK(r.onField(d.dev, g, 1, 1));
    CHECK_EQ(r.tileClass(0, 0), 1);
    CHECK_EQ(r.tileClass(1, 5), 1);
    CHECK_EQ(r.tileClass(2, 5), 3);
    CHECK_EQ(r.tileClass(TX - 1, 5), 2);
    CHECK_EQ(r.tileClass(TX, 0), 0);
    CHECK_EQ(tileHeightClass(g, 2, 5), 3);
    CHECK_EQ(r.stats().land, LAND);
}

C5R_TEST(standin_refuses_bad_requests) {
    Dev d;
    StandinBaker b{StandinFaults()};
    BakeRequest rq;
    BakedTile out;
    CHECK_ERR(b.init(nullptr, d.ctx), Err::BadLayout);
    CHECK_ERR(b.setMap(MapInputs()), Err::NotFound);
    CHECK_OK(b.init(d.dev, d.ctx));
    CHECK_ERR(b.bake(rq, out), Err::NotFound);
    CHECK_ERR(b.setMap(MapInputs()), Err::BadLayout);
    heightfield::HeightGrid g = field();
    MapInputs mi;
    mi.grid = &g;
    CHECK_OK(b.setMap(mi));
    rq.tile.x = TX;
    CHECK_ERR(b.bake(rq, out), Err::NotFound);
    rq.tile.x = 3;
    rq.sizeClass = 100;
    CHECK_ERR(b.bake(rq, out), Err::BadLayout);
    rq.sizeClass = 256;
    CHECK_OK(b.bake(rq, out));
    CHECK(out.t0 && out.t8 && out.hasT8);
    CHECK_EQ(b.liveBytes(), 2 * cacheBytes(256));
    out.t0->Release();
    out.t8->Release();
    CHECK_EQ(b.liveBytes(), 0u);
    CHECK_EQ(cacheBytes(256), 43680u);
    CHECK_EQ(cacheBytes(7), 0u);
}

// ---------------------------------------------------------------- residency with the renderer as the sink

C5R_TEST(whole_map_resident_in_arrays) {
    Fixture f(fast());
    f.idleUntilAhead();
    const ResidencyStats& s = f.res.stats();
    CHECK(s.aheadDone);
    CHECK_EQ(s.resident, LAND);
    CHECK_EQ(s.bakes, static_cast<int64_t>(LAND));
    CHECK_EQ(s.slicesUsed, 2 * LAND);
    CHECK_EQ(s.arrayBytes, static_cast<uint64_t>(s.arrays) * 16 * cacheBytes(256));
    CHECK(s.arrays * 16 >= 2 * LAND && (s.arrays - 1) * 16 < 2 * LAND);
    CHECK_EQ(f.baker.liveBytes(), 0u);   // the stand-in wrote into the slices, made no textures of its own
    CHECK_EQ(s.evicted, 0);
    CHECK(!f.res.tileBaked(0, 0));       // class 1: never baked
    CHECK(f.res.tileBaked(5, 5));
    // the renderer takes every slice view at the next draw
    f.res.beforeDraw(f.d.ctx, f.r, view(8, 6), 50);
    CHECK_EQ(f.res.stats().sinkErrors, 0);
    CHECK_EQ(f.res.stats().grayFrames, 0);
    // draw copies for the tiles within one ring of the view only (t0 + t8 each), not for the whole map
    const uint32_t used = f.res.stats().drawInUse;
    CHECK(used >= 2 * f.res.stats().visibleLand && used < 2 * LAND);
    f.res.beforeDraw(f.d.ctx, f.r, view(3, 3), 50);   // moved: the old copies go back to the pool, new ones are made
    CHECK(f.res.stats().drawTextures <= used + 2 * LAND);
    CHECK_EQ(f.res.stats().grayFrames, 0);
    char line[1024];
    f.res.statsLine(line, sizeof(line));
    CHECK(std::strstr(line, "resident=") != nullptr);
}

C5R_TEST(visible_first_then_rings_and_gray_counter) {
    ResidencyOptions o = fast();
    o.burst = 2;           // too small for the view: the first frames are gray
    o.rate = 1;
    o.trace = 50;
    o.catchUpVisible = false;   // issue 0409: the burst alone, as before (the catch-up has its own tests below)
    Fixture f(o);
    f.res.beforeDraw(f.d.ctx, f.r, view(8, 6), 50);
    const ResidencyStats& s = f.res.stats();
    CHECK(s.visibleLand > 2);
    CHECK_EQ(s.bakes, 2);
    CHECK_EQ(s.grayFrames, 1);
    CHECK(f.logged("ring 0 visible 1"));
    for (int i = 0; i < 40; ++i) f.res.beforeDraw(f.d.ctx, f.r, view(8, 6), 50);
    CHECK_EQ(f.res.stats().staleFrames, static_cast<int64_t>(0));
    CHECK(f.res.stats().grayFrames < 40);   // burst 2 per frame catches up with the view
}

C5R_TEST(radius_zero_evicts_after_a_move) {
    ResidencyOptions o = fast();
    o.radius = 0;
    Fixture f(o);
    f.res.beforeDraw(f.d.ctx, f.r, view(4, 4), 50);
    f.idleUntilAhead();
    const uint32_t first = f.res.stats().resident;
    CHECK_EQ(first, f.res.stats().visibleLand);
    // issue 0409: 5 s of budget, not 50 ms: the view's 24 new tiles are within the burst (64), so no catch-up, and under
    // ASan on a loaded machine 24 bakes took more than 50 ms (2026-11-16, twice; 0393's rerun 1 too): the test is about
    // eviction, not about the budget
    f.res.beforeDraw(f.d.ctx, f.r, view(12, 8), 5000);
    const ResidencyStats& s = f.res.stats();
    CHECK(s.evicted > 0);
    CHECK_EQ(s.resident, s.visibleLand);
    CHECK_EQ(s.grayFrames, 0);
}

C5R_TEST(raise_and_plots_changed_rebake) {
    Fixture f(fast());
    f.idleUntilAhead();
    CHECK(!f.res.raiseTile(TX, 0));
    CHECK(f.res.raiseTile(5, 5));
    CHECK(!f.res.tileBaked(5, 5));
    f.res.idle(f.d.ctx, 50);
    CHECK(f.res.tileBaked(5, 5));
    CHECK_EQ(f.res.stats().rebakes, 1);
    CHECK_EQ(f.res.stats().latencyN, 1);
    // plot (8, 4) -> tile (8, 6) and the rectangle 5 x 7 around it; an index past the map is ignored
    const uint32_t plots[2] = {4 * TX + 8, 9999999};
    f.res.onPlotsChanged(plots, 2);
    CHECK_EQ(f.res.stats().plotsChanged, 1);
    CHECK_EQ(f.res.stats().raises, 1 + 35);
    for (int i = 0; i < 20; ++i) f.res.idle(f.d.ctx, 50);
    CHECK_EQ(f.res.stats().rebakes, 1 + 35);
}

// issue 0365: a tile baked earlier (around view A) and raised while tiles near the new view B have no bake yet: with
// staleFirst (the default) idle() re-bakes it before it bakes any tile without a bake; without it (before 0365) idle()
// goes on in ring order from B and leaves it stale. Each arm: 4 visible tiles of A per frame, then B once, then one idle
// call of 4 bakes.
C5R_TEST(stale_first_rebakes_before_the_bake_ahead_goes_on) {
    for (int arm = 0; arm < 2; ++arm) {
        ResidencyOptions o = fast();
        o.burst = 4;
        o.staleFirst = arm == 0;
        Fixture f(o);
        for (int i = 0; i < 12; ++i) f.res.beforeDraw(f.d.ctx, f.r, view(4, 3), 50);
        CHECK(f.res.tileBaked(4, 3));
        f.res.beforeDraw(f.d.ctx, f.r, view(12, 9), 50);   // the rings start at B now; most of B's tiles have no bake
        CHECK(f.res.stats().visibleLand > 4);              // B's visible tiles beyond the frame's 4 wait for idle()
        const int64_t before = f.res.stats().bakes;
        CHECK(f.res.raiseTile(4, 3));
        CHECK(f.res.tileHasBake(4, 3) && !f.res.tileBaked(4, 3));
        f.res.idle(f.d.ctx, 50);   // the first call only starts the token clock
        Sleep(5);
        f.res.idle(f.d.ctx, 50);   // tokens: min(5 ms x rate, burst 4) = 4 bakes
        const ResidencyStats& s = f.res.stats();
        CHECK_EQ(s.bakes, before + 4);
        CHECK(s.pending > 0);   // the bake-ahead is not done: unbaked tiles were waiting
        if (arm == 0) {
            CHECK(f.res.tileBaked(4, 3));
            CHECK_EQ(s.staleFirstBakes, static_cast<int64_t>(1));
            CHECK_EQ(s.rebakes, static_cast<int64_t>(1));
        } else {
            CHECK(!f.res.tileBaked(4, 3));   // the negative: without staleFirst it waits behind B's rings
            CHECK_EQ(s.staleFirstBakes, static_cast<int64_t>(0));
            CHECK_EQ(s.rebakes, static_cast<int64_t>(0));
        }
        CHECK(!f.res.tileHasBake(TX, 0));
        f.idleUntilAhead();
        CHECK(f.res.tileBaked(4, 3));
        CHECK_EQ(f.res.stats().rebakes, static_cast<int64_t>(1));   // baked twice: once before the raise, once after
    }
}

C5R_TEST(field_change_rebakes_only_the_changed_tile) {
    Fixture f(fast());
    f.idleUntilAhead();
    heightfield::HeightGrid g2 = field(true);
    CHECK_OK(f.r.setHeightField(g2, 1));
    CHECK_OK(f.res.onField(f.d.dev, g2, 1, 2));
    CHECK_EQ(f.res.stats().fieldChangedTiles, 1);
    CHECK(!f.res.tileBaked(8, 6));
    for (int i = 0; i < 100 && !f.res.tileBaked(8, 6); ++i) {   // the token bucket starts empty after a field
        Sleep(2);
        f.res.idle(f.d.ctx, 50);
    }
    CHECK(f.res.tileBaked(8, 6));
    CHECK_EQ(f.res.stats().rebakes, 1);
    CHECK_EQ(f.res.stats().resident, LAND);
    f.res.beforeDraw(f.d.ctx, f.r, view(8, 6), 50);   // every resident tile handed to the renderer again
    CHECK_EQ(f.res.stats().sinkErrors, 0);
    CHECK_EQ(f.res.stats().grayFrames, 0);
}

C5R_TEST(failed_tile_stays_plain_until_raised) {
    StandinFaults fl;
    fl.failX = 8;
    fl.failY = 6;
    Fixture f(fast(), fl);
    f.idleUntilAhead();
    CHECK_EQ(f.res.stats().failures, 1);
    CHECK_EQ(f.res.stats().resident, LAND - 1);
    CHECK(f.logged("bake of tile (8, 6) generation 1 failed: Unsupported"));
    f.res.beforeDraw(f.d.ctx, f.r, view(8, 6), 50);
    CHECK_EQ(f.res.stats().grayFramesAfterAhead, 1);
    f.res.idle(f.d.ctx, 50);
    CHECK_EQ(f.res.stats().failures, 1);   // not retried
    f.res.raiseTile(8, 6);
    f.res.idle(f.d.ctx, 50);
    CHECK_EQ(f.res.stats().failures, 2);
}

C5R_TEST(out_of_memory_lowers_the_radius_then_stops) {
    ResidencyOptions o = fast();
    o.arrayChunk = 4;
    o.memoryLimitMb = 60 * 2 * cacheBytes(256) / 1048576.0;   // 60 tiles
    Fixture f(o);
    f.res.beforeDraw(f.d.ctx, f.r, view(8, 6), 50);
    f.idleUntilAhead();
    for (int i = 0; i < 50; ++i) f.res.idle(f.d.ctx, 50);
    const ResidencyStats& s = f.res.stats();
    CHECK(s.oomEvents >= 1);
    CHECK(f.logged("radius -1 -> 5"));
    CHECK(s.arrayBytes <= static_cast<uint64_t>(o.memoryLimitMb * 1048576.0));
    CHECK(!s.stopped);
    // a limit below the visible tiles: radius down to 0, then baking stops
    ResidencyOptions o2 = fast();
    o2.arrayChunk = 2;
    o2.radius = 1;
    o2.memoryLimitMb = 4 * 2 * cacheBytes(256) / 1048576.0;
    Fixture g(o2);
    for (int i = 0; i < 5; ++i) g.res.beforeDraw(g.d.ctx, g.r, view(8, 6), 50);
    CHECK(g.logged("radius 1 -> 0"));
    CHECK(g.logged("baking stopped"));
    CHECK(g.res.stats().stopped);
    CHECK_EQ(g.res.stats().grayFrames, g.res.stats().framesCounted);
}

C5R_TEST(baker_out_of_memory_without_arrays) {
    ResidencyOptions o = fast();
    o.arrays = false;
    StandinFaults fl;
    fl.vramBudgetMb = 30 * 2 * cacheBytes(256) / 1048576.0;
    Fixture f(o, fl);
    f.res.beforeDraw(f.d.ctx, f.r, view(8, 6), 50);
    f.idleUntilAhead();
    for (int i = 0; i < 50; ++i) f.res.idle(f.d.ctx, 50);
    CHECK(f.res.stats().oomEvents >= 1);
    CHECK(f.logged("OutOfMemory: stand-in: CreateTexture2D failed 0x8007000E"));
    CHECK(f.baker.liveBytes() <= static_cast<uint64_t>(fl.vramBudgetMb * 1048576.0));
    CHECK_EQ(f.res.stats().arrays, 0u);
}

C5R_TEST(gpu_budget_limits_the_background) {
    ResidencyOptions o = fast();
    o.rate = 400;
    o.gpuMsPerS = 20;
    StandinFaults fl;
    fl.cpuMs = 2;   // reported as gpuMs
    Fixture f(o, fl);
    const DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < 1000) {
        Sleep(2);
        f.res.idle(f.d.ctx, 50);
    }
    const ResidencyStats& s = f.res.stats();
    CHECK(s.bakes >= 5 && s.bakes <= 20);   // 20 ms/s at 2 ms per bake: ~10 in one second (+ the quarter-second start)
    CHECK(std::fabs(s.bakeGpuMsSum - 2.0 * s.bakes) < 1e-3);
}

// Issue 0137: server.exe grew +3.11 MB per AI turn with the renderer against +1.20 without (3311), because every re-baked
// tile gained a t8 slice (re-bakes set areaChanged, the EXE's tile+0x300) and each new 256-slice chunk adds its size to
// private bytes (11.0 MB per chunk at size class 256, live). Re-baking the same tiles must not grow the arrays or the
// process: every land tile's t8 slice is held from its first bake, and a re-bake writes into the tile's own slices.
// Size class 512 here (174,752 B per slice, 44.7 MB per chunk) puts the old code's growth far above the reading's spread.
//
// Issue 0173: the readings. The driver keeps memory of its own in private bytes: the upload and command memory of work in
// flight, and pools it grows to that size and keeps after the GPU has caught up. With the GPU shared (another process
// drawing), the test's 10,000 uploads ran ahead of the GPU and the driver's share grew by up to +243 MB in one run (+17.3
// MB from round 3 in the failure that opened 0173), all of it given back by IDXGIDevice3::Trim; the residency's counters
// stayed exact. So (1) every round waits for the GPU to finish it, as the product's frame does, which bounds the work in
// flight; and (2) the three readings (after the bake-ahead, after round 3, at the end) are taken settled: GPU idle,
// ClearState, Trim, 200 ms. What a settled reading still holds is the process's own memory: heap, the arrays, the caches,
// the draw copies. Measured spreads and the bounds: research/05-graphics-composition/renderer-in-server-design.md 4.15.1.
namespace {

double privateMb() {
    PROCESS_MEMORY_COUNTERS_EX pm = {};
    pm.cb = sizeof(pm);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pm), sizeof(pm));
    return pm.PrivateUsage / 1048576.0;
}

// Waits until the GPU has finished everything submitted (at most about 30 s; false if it did not).
bool waitGpu(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    D3D11_QUERY_DESC qd = {D3D11_QUERY_EVENT, 0};
    ID3D11Query* q = nullptr;
    if (FAILED(dev->CreateQuery(&qd, &q))) return false;
    ctx->End(q);
    BOOL done = FALSE;
    bool ok = false;
    const DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < 30000) {
        if (ctx->GetData(q, &done, sizeof(done), 0) == S_OK) {
            ok = true;
            break;
        }
        Sleep(0);
    }
    q->Release();
    return ok;
}

// A settled reading: the GPU idle, the context's bindings cleared (Trim's precondition), the driver's pools trimmed, and
// time for the driver's own thread, which frees some memory only a while after the work it was given. A Trim's memory
// comes back one pass late (2026-11-14: the first pass frees 0-2 MB, the second 35-70 MB), so passes repeat until two in a
// row free less than 0.25 MB each (at most 12 passes of 100 ms); the lowest reading.
double settledPrivateMb(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    const bool trace = std::getenv("C5R_0137_TRACE") != nullptr;
    IDXGIDevice3* dx = nullptr;
    CHECK(SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice3), reinterpret_cast<void**>(&dx))));
    double best = privateMb();
    if (trace) std::printf("  settle: %.2f", best);
    int quiet = 0;
    for (int pass = 0; pass < 12 && quiet < 2; ++pass) {
        CHECK(waitGpu(dev, ctx));
        Sleep(100);
        ctx->ClearState();
        ctx->Flush();
        if (dx) dx->Trim();
        const double p = privateMb();
        if (trace) std::printf(" %.2f", p);
        quiet = p < best - 0.25 ? 0 : quiet + 1;
        best = std::min(best, p);
    }
    if (trace) std::printf("\n");
    if (dx) dx->Release();
    return best;
}

struct RebakeMemory {
    ResidencyStats s0, s;
    double p0 = 0, p3 = 0, p1 = 0;   // settled readings: after the bake-ahead, after round 3, at the end (MB)
    double pMax = 0;                  // highest unsettled reading after a round (GPU idle, pools not trimmed)
    int rounds = 0;
};

// The bake-ahead, then rounds of raising every land tile, idling until all are re-baked and one beforeDraw alternating
// between two views, until 10,000 re-bakes. leakPerRebake > 0 adds a deliberate leak of that many bytes of heap per re-bake
// (the negative test, and C5R_0173_LEAK_BYTES to try the bound's reach).
RebakeMemory rebakeSameTiles(Fixture& f, size_t leakPerRebake) {
    const bool trace = std::getenv("C5R_0137_TRACE") != nullptr;
    RebakeMemory m;
    std::vector<void*> leaked;
    f.res.beforeDraw(f.d.ctx, f.r, view(8, 6), 50);
    f.idleUntilAhead();
    f.res.beforeDraw(f.d.ctx, f.r, view(3, 3), 50);
    f.res.beforeDraw(f.d.ctx, f.r, view(8, 6), 50);
    m.s0 = f.res.stats();
    m.p0 = settledPrivateMb(f.d.dev, f.d.ctx);
    m.pMax = m.p0;
    const int64_t want = 10000;
    while (f.res.stats().rebakes < want && m.rounds < 200) {
        const int64_t before = f.res.stats().rebakes;
        for (uint32_t y = 0; y < TY; ++y)
            for (uint32_t x = 0; x < TX; ++x)
                if (f.res.tileClass(x, y) >= 2) f.res.raiseTile(x, y);
        for (int i = 0; i < 400 && f.res.stats().pending; ++i) f.res.idle(f.d.ctx, 50);
        f.res.beforeDraw(f.d.ctx, f.r, (m.rounds & 1) ? view(3, 3) : view(8, 6), 50);   // draw copies refreshed and moved
        f.d.ctx->Flush();
        CHECK(waitGpu(f.d.dev, f.d.ctx));   // the round finished on the GPU, as a frame would be before the next
        for (int64_t i = before; leakPerRebake && i < f.res.stats().rebakes; ++i) {
            void* p = std::malloc(leakPerRebake);
            if (p) std::memset(p, 1, leakPerRebake);
            leaked.push_back(p);
        }
        m.rounds++;
        double pr;
        if (m.rounds == 3) {
            m.p3 = settledPrivateMb(f.d.dev, f.d.ctx);
            pr = m.p3;
        } else {
            pr = privateMb();
        }
        m.pMax = std::max(m.pMax, pr);
        if (trace)
            std::printf("round %d: rebakes %lld private %.2f MB%s draw_tex %u slices %u\n", m.rounds,
                        static_cast<long long>(f.res.stats().rebakes), pr, m.rounds == 3 ? " (settled)" : "",
                        f.res.stats().drawTextures, f.res.stats().slicesUsed);
    }
    m.s = f.res.stats();
    m.p1 = settledPrivateMb(f.d.dev, f.d.ctx);
    for (void* p : leaked) std::free(p);
    return m;
}

const double kFromRound3Mb = 6.0;   // settled growth from round 3 to the end (9,576 re-bakes): 0.64 KB per re-bake

}  // namespace

// Stated bound: the slices and arrays stay exactly as after the bake-ahead; the settled private bytes after 10,000 re-bakes
// are at most 8 MB plus the draw-copy textures the pool added above the reading after the bake-ahead (a tile within one ring
// of the view that gains its t8 needs a second draw copy once; the pool is bounded by 2 per tile within one ring); and from
// round 3 (pool and caches at their size) to the end at most 6 MB, about 0.64 KB per re-bake. Measured 2026-11-14 on the
// RTX 4080 (4.15.1), 22 runs, 10 alone and 12 beside three other copies of this suite: whole run -1.1 to +2.9 MB over the
// draw copies, from round 3 -1.3 to +2.4 MB. The code before 0137 fails the slice checks (slices 168 -> 336, a second
// chunk), which 0173 did not change. [L, the GPU the suite runs on]
C5R_TEST(rebake_same_tiles_10000_times_memory_flat) {
    const uint32_t SC = 512;
    const double drawTexMb = ((cacheBytes(SC) + 65535) / 65536) * 65536 / 1048576.0;   // one Texture2D, 64 KB pages
    const char* leakEnv = std::getenv("C5R_0173_LEAK_BYTES");
    const size_t leak = leakEnv ? static_cast<size_t>(std::strtoul(leakEnv, nullptr, 10)) : 0;
    ResidencyOptions o = fast();
    o.arrayChunk = 256;   // the product's chunk
    o.sizeClass = SC;
    Fixture f(o, StandinFaults(), true);
    const RebakeMemory m = rebakeSameTiles(f, leak);
    const ResidencyStats& s0 = m.s0;
    const ResidencyStats& s = m.s;
    const double bound = 8.0 + drawTexMb * (s.drawTextures - s0.drawTextures);
    std::printf("0137: %lld re-bakes in %d rounds at size class %u%s: slices %u -> %u, arrays %.1f -> %.1f MB, caches %.1f -> "
                "%.1f MB, draw textures %u -> %u; settled private %.1f -> %.1f MB (%+.1f, bound %+.1f; highest unsettled "
                "%+.1f), from round 3 %+.1f (bound %+.1f)\n",
                static_cast<long long>(s.rebakes), m.rounds, SC, leak ? " WITH A DELIBERATE LEAK" : "", s0.slicesUsed,
                s.slicesUsed, s0.arrayBytes / 1048576.0, s.arrayBytes / 1048576.0, s0.cacheBytes / 1048576.0,
                s.cacheBytes / 1048576.0, s0.drawTextures, s.drawTextures, m.p0, m.p1, m.p1 - m.p0, bound, m.pMax - m.p0,
                m.p1 - m.p3, kFromRound3Mb);
    CHECK(s0.aheadDone);
    CHECK_EQ(s0.resident, LAND);
    CHECK_EQ(s0.cacheBytes, static_cast<uint64_t>(LAND) * cacheBytes(SC));   // no t8 before an area change
    CHECK(s.rebakes >= 10000);
    CHECK_EQ(s.failures, 0);
    CHECK_EQ(s.resident, LAND);
    CHECK_EQ(s.cacheBytes, static_cast<uint64_t>(2 * LAND) * cacheBytes(SC));   // every re-baked tile has its t8 now
    CHECK_EQ(s0.slicesUsed, 2 * LAND);
    CHECK_EQ(s.slicesUsed, s0.slicesUsed);
    CHECK_EQ(s0.t8Held, LAND);
    CHECK_EQ(s.t8Held, 0u);
    CHECK_EQ(s.arrayBytes, s0.arrayBytes);
    CHECK(s.drawTextures <= s0.drawTextures + 2 * LAND);
    CHECK(m.p1 - m.p0 <= bound);
    CHECK(m.p3 > 0 && m.p1 - m.p3 <= kFromRound3Mb);
}

// The negative test of the memory check (0173): the same run with a deliberate leak of 2 KB of heap per re-bake (18.7 MB
// from round 3 to the end) must exceed the bound from round 3, i.e. the check the test above makes would fail it.
C5R_TEST(rebake_memory_check_catches_a_leak) {
    ResidencyOptions o = fast();
    o.arrayChunk = 256;
    o.sizeClass = 512;
    Fixture f(o, StandinFaults(), true);
    const RebakeMemory m = rebakeSameTiles(f, 2048);
    std::printf("0173: with a leak of 2048 B per re-bake: %lld re-bakes, settled private from round 3 %+.1f MB (bound %+.1f)\n",
                static_cast<long long>(m.s.rebakes), m.p1 - m.p3, kFromRound3Mb);
    CHECK(m.s.rebakes >= 10000);
    CHECK(m.p3 > 0 && m.p1 - m.p3 > kFromRound3Mb);
    CHECK_EQ(m.s.slicesUsed, m.s0.slicesUsed);   // the leak is the test's own: the residency is unchanged
}


// The arrays' slice views are Texture2DArray views; the EXE's terrain pixel shaders declare t0 / t8 as Texture2D. A view
// of another dimension than the shader declares is undefined by the D3D11 specification, which is why the residency hands
// the renderer Texture2D draw copies. The test pins what this machine's driver does (it reads the slice) beside a control
// read through a Texture2DArray declaration and the copy. [L, the GPU the suite runs on]
C5R_TEST(array_slice_view_reads_as_texture2d) {
    Dev d;
    StandinBaker b{StandinFaults()};
    CHECK_OK(b.init(d.dev, d.ctx));
    heightfield::HeightGrid g = field();
    MapInputs mi;
    mi.grid = &g;
    CHECK_OK(b.setMap(mi));
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = td.Height = 256;
    td.MipLevels = 6;
    td.ArraySize = 4;
    td.Format = DXGI_FORMAT_BC1_TYPELESS;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* arr = nullptr;
    CHECK(SUCCEEDED(d.dev->CreateTexture2D(&td, nullptr, &arr)));
    BakeRequest rq;
    rq.tile.x = 5;
    rq.tile.y = 5;
    rq.t0Target.array = arr;
    rq.t0Target.slice = 3;
    rq.t8Target.array = arr;
    rq.t8Target.slice = 2;
    BakedTile out;
    CHECK_OK(b.bake(rq, out));
    CHECK(!out.t0 && !out.t8 && out.hasT8);
    rq.t0Target.slice = 4;
    CHECK_ERR(b.bake(rq, out), Err::BadLayout);
    D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
    vd.Format = DXGI_FORMAT_BC1_UNORM;
    vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    vd.Texture2DArray.MipLevels = 6;
    vd.Texture2DArray.FirstArraySlice = 3;
    vd.Texture2DArray.ArraySize = 1;
    ID3D11ShaderResourceView* srv = nullptr;
    CHECK(SUCCEEDED(d.dev->CreateShaderResourceView(arr, &vd, &srv)));
    // a Texture2D copy of slice 3, as the residency's draw copies are made
    td.ArraySize = 1;
    ID3D11Texture2D* copy = nullptr;
    CHECK(SUCCEEDED(d.dev->CreateTexture2D(&td, nullptr, &copy)));
    for (uint32_t mm = 0; mm < 6; ++mm)
        d.ctx->CopySubresourceRegion(copy, D3D11CalcSubresource(mm, 0, 6), 0, 0, 0, arr, D3D11CalcSubresource(mm, 3, 6), nullptr);
    D3D11_SHADER_RESOURCE_VIEW_DESC cd = {};
    cd.Format = DXGI_FORMAT_BC1_UNORM;
    cd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    cd.Texture2D.MipLevels = 6;
    ID3D11ShaderResourceView* copySrv = nullptr;
    CHECK(SUCCEEDED(d.dev->CreateShaderResourceView(copy, &cd, &copySrv)));
    const char* vsSrc =
        // clockwise on screen (the default rasterizer state culls counter-clockwise triangles)
        "float4 vs(uint i : SV_VertexID) : SV_Position { float2 p = float2(i & 2, (i << 1) & 2); return float4(p * 2 - 1, 0, 1); }\n";
    const char* ps2d = "Texture2D t0 : register(t0);\nfloat4 ps() : SV_Target { return t0.Load(int3(5, 5, 0)); }\n";
    const char* psArr = "Texture2DArray t0 : register(t0);\nfloat4 ps() : SV_Target { return t0.Load(int4(5, 5, 0, 0)); }\n";
    ID3DBlob *vb = nullptr, *err = nullptr;
    CHECK(SUCCEEDED(D3DCompile(vsSrc, std::strlen(vsSrc), "t", nullptr, nullptr, "vs", "vs_4_0", 0, 0, &vb, &err)));
    ID3D11VertexShader* vs = nullptr;
    d.dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &vs);
    D3D11_TEXTURE2D_DESC rd = {};
    rd.Width = rd.Height = 4;
    rd.MipLevels = rd.ArraySize = 1;
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.SampleDesc.Count = 1;
    rd.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D* rt = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    d.dev->CreateTexture2D(&rd, nullptr, &rt);
    d.dev->CreateRenderTargetView(rt, nullptr, &rtv);
    rd.BindFlags = 0;
    rd.Usage = D3D11_USAGE_STAGING;
    rd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* st = nullptr;
    d.dev->CreateTexture2D(&rd, nullptr, &st);
    auto read = [&](const char* psSrc, ID3D11ShaderResourceView* view, uint8_t px[4]) {
        ID3DBlob* pb = nullptr;
        ID3DBlob* e2 = nullptr;
        CHECK(SUCCEEDED(D3DCompile(psSrc, std::strlen(psSrc), "t", nullptr, nullptr, "ps", "ps_4_0", 0, 0, &pb, &e2)));
        ID3D11PixelShader* ps = nullptr;
        d.dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &ps);
        const float black[4] = {0, 0, 0, 0};
        d.ctx->ClearRenderTargetView(rtv, black);
        D3D11_VIEWPORT vp = {0, 0, 4, 4, 0, 1};
        d.ctx->OMSetRenderTargets(1, &rtv, nullptr);
        d.ctx->RSSetViewports(1, &vp);
        d.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        d.ctx->IASetInputLayout(nullptr);
        d.ctx->VSSetShader(vs, nullptr, 0);
        d.ctx->PSSetShader(ps, nullptr, 0);
        d.ctx->PSSetShaderResources(0, 1, &view);
        d.ctx->Draw(3, 0);
        d.ctx->CopyResource(st, rt);
        D3D11_MAPPED_SUBRESOURCE m;
        CHECK(SUCCEEDED(d.ctx->Map(st, 0, D3D11_MAP_READ, 0, &m)));
        std::memcpy(px, m.pData, 4);
        d.ctx->Unmap(st, 0);
        ps->Release();
        pb->Release();
    };
    // the stand-in's t0 colour of tile (5, 5), generation 0, through RGB565
    const uint32_t h = 5u * 73856093u ^ 5u * 19349663u;
    const int r = 60 + (h & 0x7F), gg = 70 + ((h >> 8) & 0x7F), bb = 30 + ((h >> 16) & 0x3F);
    const int er = ((r >> 3) * 255 + 15) / 31, eg = ((gg >> 2) * 255 + 31) / 63, eb = ((bb >> 3) * 255 + 15) / 31;
    auto matches = [&](const uint8_t px[4]) { return std::abs(px[0] - er) <= 2 && std::abs(px[1] - eg) <= 2 && std::abs(px[2] - eb) <= 2; };
    uint8_t a[4], b2[4], c[4];
    read(psArr, srv, a);      // control: the slice view read as an array
    read(ps2d, copySrv, c);   // the residency's draw copy read as Texture2D
    read(ps2d, srv, b2);      // the slice view read through a Texture2D declaration (the EXE's shaders)
    std::printf("slice 3: array view as Texture2DArray %u %u %u; Texture2D copy %u %u %u; array view as Texture2D %u %u %u "
                "(expected about %d %d %d)\n", a[0], a[1], a[2], c[0], c[1], c[2], b2[0], b2[1], b2[2], er, eg, eb);
    CHECK(matches(a));
    CHECK(matches(c));
    CHECK(matches(b2));   // [L] on the RTX 4080 the mismatch reads the slice; undefined by the specification
    st->Release();
    rtv->Release();
    rt->Release();
    vs->Release();
    vb->Release();
    copySrv->Release();
    copy->Release();
    srv->Release();
    arr->Release();
}

// ---------------------------------------------------------------- issue 0140: the idle slice yields to a waiting camera

namespace {
// A yield check that says "a camera waits" from its (after + 1)-th call on; each call is counted.
struct YieldAfter {
    int after = 0, calls = 0;
    static bool fn(void* c) {
        YieldAfter& y = *static_cast<YieldAfter*>(c);
        return y.calls++ >= y.after;
    }
};
int64_t histSum(const ResidencyStats& s) {
    int64_t n = 0;
    for (int b = 0; b < ResidencyStats::kYieldBins; ++b) n += s.yieldWaitHist[b];
    return n;
}
}  // namespace

C5R_TEST(idle_yields_before_the_next_bake_once_a_camera_waits) {
    YieldAfter y;
    y.after = 3;
    ResidencyOptions o = fast();
    o.yield = &YieldAfter::fn;
    o.yieldCtx = &y;
    Fixture f(o);
    f.res.idle(f.d.ctx, 1000);   // starts the token clock
    Sleep(5);
    f.res.idle(f.d.ctx, 1000);   // 1000 ms of budget and tokens for 64: only the yield ends it
    const ResidencyStats& s = f.res.stats();
    CHECK_EQ(s.bakes, static_cast<int64_t>(3));   // three checks said no, the fourth yes: no bake after it
    CHECK_EQ(y.calls, 4);
    CHECK_EQ(s.yields, static_cast<int64_t>(1));
    CHECK_EQ(s.idleSlices, static_cast<int64_t>(1));
    CHECK_EQ(histSum(s), static_cast<int64_t>(1));
    // the bound is the span of the last bake (check 3 -> check 4): above 0, below the whole slice
    CHECK(s.yieldWaitMaxMs > 0 && s.yieldWaitMaxMs <= s.idleSliceMaxMs + 1e-9);
    CHECK(s.yieldWaitP99Ms() >= s.yieldWaitMaxMs - 1e-9 && s.yieldWaitP99Ms() <= s.yieldWaitMaxMs + 0.25);
    CHECK(!s.aheadDone);
    // the camera still waits at the next call: nothing is baked, the yield is counted, no slice
    Sleep(2);
    f.res.idle(f.d.ctx, 1000);
    CHECK_EQ(f.res.stats().bakes, static_cast<int64_t>(3));
    CHECK_EQ(f.res.stats().yields, static_cast<int64_t>(2));
    CHECK_EQ(f.res.stats().idleSlices, static_cast<int64_t>(1));
    // drawn (the host's check says no again): the bake-ahead goes on and completes
    y.after = 1000000;
    y.calls = 0;
    f.idleUntilAhead();
    CHECK(f.res.stats().aheadDone);
    CHECK_EQ(f.res.stats().resident, LAND);
    CHECK_EQ(f.res.stats().yields, static_cast<int64_t>(2));
    char line[2048];
    f.res.statsLine(line, sizeof(line));
    CHECK(std::strstr(line, " yields=2 ") != nullptr);
    CHECK(std::strstr(line, " yield=1") != nullptr);
    CHECK(std::strstr(line, " stale_before_draw=1 ") != nullptr);
}

C5R_TEST(idle_end_check_counts_a_camera_that_came_during_the_last_bake) {
    YieldAfter y;
    y.after = 1;   // the check before the first bake says no, the end check yes
    ResidencyOptions o = fast();
    o.yield = &YieldAfter::fn;
    o.yieldCtx = &y;
    Fixture f(o);
    f.res.idle(f.d.ctx, 1000);
    Sleep(5);
    f.res.idle(f.d.ctx, 1e-6);   // the budget ends the slice after one bake
    const ResidencyStats& s = f.res.stats();
    CHECK_EQ(s.bakes, static_cast<int64_t>(1));
    CHECK_EQ(y.calls, 2);
    CHECK_EQ(s.yields, static_cast<int64_t>(1));
    CHECK(s.yieldWaitMaxMs > 0);
}

C5R_TEST(idle_without_a_yield_check_is_unchanged) {
    Fixture f(fast());
    f.res.idle(f.d.ctx, 1000);
    Sleep(5);
    f.res.idle(f.d.ctx, 1000);   // tokens for 64 bakes (burst 64), all taken
    const ResidencyStats& s = f.res.stats();
    CHECK_EQ(s.bakes, static_cast<int64_t>(64));
    CHECK_EQ(s.yields, static_cast<int64_t>(0));
    CHECK_EQ(s.idleSlices, static_cast<int64_t>(1));
    CHECK_EQ(s.yieldWaitP99Ms(), 0.0);
    char line[2048];
    f.res.statsLine(line, sizeof(line));
    CHECK(std::strstr(line, " yield=0") != nullptr);
}

C5R_TEST(stale_visible_tile_waits_for_idle_when_stale_before_draw_is_off) {
    ResidencyOptions o = fast();
    o.staleBeforeDraw = false;
    Fixture f(o);
    // gray tiles are still baked before the draw: the first frame bakes the visible land, none of it a re-bake
    f.res.beforeDraw(f.d.ctx, f.r, view(8, 6), 50);
    const int64_t first = f.res.stats().bakes;
    CHECK(first > 0);
    CHECK_EQ(f.res.stats().predrawBakes, first);
    CHECK_EQ(f.res.stats().predrawRebakes, static_cast<int64_t>(0));
    CHECK_EQ(f.res.stats().grayFrames, static_cast<int64_t>(0));
    f.idleUntilAhead();
    const int64_t ahead = f.res.stats().bakes;
    CHECK(f.res.raiseTile(8, 6));   // the tile under the camera
    f.res.beforeDraw(f.d.ctx, f.r, view(8, 6), 50);
    CHECK_EQ(f.res.stats().bakes, ahead);                 // not re-baked before the draw ...
    CHECK(f.res.stats().staleDeferred >= 1);
    CHECK(!f.res.tileBaked(8, 6));
    CHECK_EQ(f.res.stats().grayFrames, static_cast<int64_t>(0));   // ... drawn with its old bake: stale, not gray
    CHECK(f.res.stats().staleFrames >= 1);
    Sleep(2);
    f.res.idle(f.d.ctx, 50);                              // the idle slice takes it first (ring 0)
    CHECK(f.res.tileBaked(8, 6));
    CHECK_EQ(f.res.stats().rebakes, static_cast<int64_t>(1));
    CHECK_EQ(f.res.stats().predrawRebakes, static_cast<int64_t>(0));
}

C5R_TEST(stale_visible_tile_rebaked_before_the_draw_by_default) {
    Fixture f(fast());
    CHECK(ResidencyOptions().staleBeforeDraw);
    f.idleUntilAhead();
    f.res.beforeDraw(f.d.ctx, f.r, view(8, 6), 50);
    CHECK(f.res.raiseTile(8, 6));
    f.res.beforeDraw(f.d.ctx, f.r, view(8, 6), 50);
    CHECK(f.res.tileBaked(8, 6));
    CHECK_EQ(f.res.stats().predrawRebakes, static_cast<int64_t>(1));
    CHECK_EQ(f.res.stats().staleDeferred, static_cast<int64_t>(0));
    CHECK(f.res.stats().predrawMaxMs > 0);
}

// ================================================================ issue 0409: the catch-up at a load's first draw

namespace {
// The renderer's own count of the frame's land tiles drawn without a cache (FrameStats::landTilesWithoutCache, issue
// 0139: what the server publishes as the frame's unbaked tiles), for the residency's view; -1 when the draw failed.
int64_t drawnUnbaked(Fixture& f, const ResidencyView& v) {
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = v.width;
    td.Height = v.height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D* tex = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    if (FAILED(f.d.dev->CreateTexture2D(&td, nullptr, &tex))) return -1;
    if (FAILED(f.d.dev->CreateRenderTargetView(tex, nullptr, &rtv))) {
        tex->Release();
        return -1;
    }
    terrain::Camera cam;
    std::memcpy(cam.view, v.view, sizeof(cam.view));
    std::memcpy(cam.proj, v.proj, sizeof(cam.proj));
    cam.width = v.width;
    cam.height = v.height;
    terrain::Options op;
    op.color = terrain::ColorSource::Caches;
    op.fallback = terrain::ColorSource::Material;
    op.water = false;
    terrain::FrameStats fs;
    const Status st = f.r.draw(f.d.ctx, rtv, nullptr, cam, op, &fs);
    rtv->Release();
    tex->Release();
    if (!st.ok()) {
        std::printf("  draw: %s\n", st.str().c_str());
        return -1;
    }
    return fs.landTilesWithoutCache;
}

// A high camera over the middle of the map: well over 16 visible land tiles (the huge map's first draw saw 58-74).
ResidencyView wideView() { return view(8, 6, 700.0f, 500.0f); }
}  // namespace

C5R_TEST(catch_up_first_draw_bakes_every_visible_tile) {
    CHECK(ResidencyOptions().catchUpVisible);   // on by default
    ResidencyOptions o = fast();
    o.burst = 16;
    o.rate = 1;                                 // no bake-ahead worth the name: what the first draw shows is its own
    Fixture f(o);
    f.res.beforeDraw(f.d.ctx, f.r, wideView(), 4);   // the server's budget of 4 ms is not a limit for the catch-up either
    const ResidencyStats& s = f.res.stats();
    std::printf("  first draw: %u visible land, %d without a bake, %d baked in %.1f ms\n", s.visibleLand, s.firstDrawUnbaked,
                s.firstDrawBakes, s.firstDrawMs);
    CHECK(s.visibleLand >= 74);
    CHECK_EQ(s.firstDrawUnbaked, static_cast<int32_t>(s.visibleLand));
    const int64_t all = static_cast<int64_t>(s.visibleLand) + s.catchUpEdgeBakes;   // the view's edge (none or few here)
    CHECK_EQ(static_cast<int64_t>(s.firstDrawBakes), all);
    CHECK_EQ(s.bakes, all);
    CHECK_EQ(s.predrawBakes, all);
    CHECK(s.firstDrawMs > 0);
    CHECK_EQ(s.grayFrames, static_cast<int64_t>(0));
    CHECK_EQ(s.catchUpDraws, static_cast<int64_t>(1));
    CHECK_EQ(s.catchUpBakes, all);
    CHECK_EQ(s.catchUpMaxTiles, s.visibleLand);
    CHECK(f.logged("residency: first draw of load 1 (frame 0): "));
    CHECK(f.logged("catch-up: every one); 0 still without a bake"));
    // issue 0417: the same draw split into the baker's calls (wall, thread CPU, page faults) and the rest
    CHECK(f.logged("residency: first draw of load 1 split: baker calls "));
    CHECK(f.logged("ms, the rest of the draw "));
    // the renderer itself draws no land tile without its cache: the frame the server publishes has 0 unbaked
    CHECK_EQ(drawnUnbaked(f, wideView()), static_cast<int64_t>(0));
    char line[2048];
    f.res.statsLine(line, sizeof(line));
    CHECK(std::strstr(line, "catch_up=1 catch_up_draws=1 ") != nullptr);
    // the next frame of the same view bakes nothing before the draw and is not a catch-up
    f.res.beforeDraw(f.d.ctx, f.r, wideView(), 4);
    CHECK_EQ(f.res.stats().catchUpDraws, static_cast<int64_t>(1));
    CHECK_EQ(f.res.stats().bakes, all);
}

C5R_TEST(catch_up_off_keeps_the_burst_at_the_first_draw) {
    // the control arm (and the switch's negative test): the behaviour 0404 measured, 16 a frame and gray frames
    ResidencyOptions o = fast();
    o.burst = 16;
    o.rate = 1;
    o.catchUpVisible = false;
    Fixture f(o);
    f.res.beforeDraw(f.d.ctx, f.r, wideView(), 50);
    const ResidencyStats& s = f.res.stats();
    CHECK(s.visibleLand >= 74);
    CHECK_EQ(s.bakes, static_cast<int64_t>(16));
    CHECK_EQ(s.firstDrawBakes, 16);
    CHECK_EQ(s.firstDrawUnbaked, static_cast<int32_t>(s.visibleLand));
    CHECK_EQ(s.grayFrames, static_cast<int64_t>(1));
    CHECK_EQ(s.catchUpDraws, static_cast<int64_t>(0));
    CHECK(f.logged("(burst, catch-up off); "));
    CHECK_EQ(drawnUnbaked(f, wideView()), static_cast<int64_t>(s.visibleLand - 16));
    char line[2048];
    f.res.statsLine(line, sizeof(line));
    CHECK(std::strstr(line, "catch_up=0 catch_up_draws=0 ") != nullptr);
}

C5R_TEST(catch_up_after_a_jump_and_after_a_new_field) {
    ResidencyOptions o = fast();
    o.burst = 3;
    o.rate = 1;
    Fixture f(o);
    f.res.beforeDraw(f.d.ctx, f.r, view(4, 3), 50);
    const int64_t first = f.res.stats().bakes;
    CHECK(first > 3);
    CHECK_EQ(f.res.stats().catchUpDraws, static_cast<int64_t>(1));
    // a jump to the other side of the map: every visible tile there is unbaked, more than the burst
    f.res.beforeDraw(f.d.ctx, f.r, view(12, 8), 50);
    const ResidencyStats& s = f.res.stats();
    CHECK_EQ(s.catchUpDraws, static_cast<int64_t>(2));
    CHECK(s.bakes - first > 3);
    CHECK_EQ(s.grayFrames, static_cast<int64_t>(0));
    CHECK(f.logged("residency: catch-up draw (frame 0): "));
    {
        const int64_t du = drawnUnbaked(f, view(12, 8));
        std::printf("  jump: residency visible land %u, the renderer's land tiles without a cache %lld\n", s.visibleLand,
                    static_cast<long long>(du));
        for (uint32_t y = 0; y < TY; ++y) {
            std::printf("   ");
            for (uint32_t x = 0; x < TX; ++x)
                std::printf("%c", f.res.tileClass(x, y) < 2 ? '~' : f.res.tileHasBake(x, y) ? '#' : '.');
            std::printf("\n");
        }
        // the renderer's cull draws tiles beyond the residency's tight visible set (its z bounds span the height page):
        // the catch-up baked those at the view's edge too, or this frame would publish them unbaked
        CHECK_EQ(du, static_cast<int64_t>(0));
        CHECK(s.catchUpEdgeBakes > 0);
        CHECK(f.logged("at the view's edge) in "));
    }
    // a new field of another size starts over: its first draw is logged as load 2 and caught up again
    heightfield::HeightGrid g2 = field();
    CHECK_OK(f.r.setHeightField(g2, 1));
    CHECK_OK(f.res.onField(f.d.dev, g2, 1, 2));   // same size: compared, nothing changed, the bakes stay
    f.res.beforeDraw(f.d.ctx, f.r, view(12, 8), 50);
    CHECK(f.logged("residency: first draw of load 2 (frame 0): "));
    CHECK_EQ(f.res.stats().firstDrawUnbaked, 0);
    CHECK_EQ(f.res.stats().firstDrawBakes, 0);
    CHECK(f.logged("0 without a bake; 0 baked before the draw"));
}

C5R_TEST(slow_bake_is_logged_and_counted) {
    ResidencyOptions o = fast();
    o.burst = 1;
    o.rate = 1;
    o.catchUpVisible = false;   // one bake before the draw
    StandinFaults slow;
    slow.cpuMs = 520;           // above the 500 ms of a slow bake
    Fixture f(o, slow);
    f.res.beforeDraw(f.d.ctx, f.r, view(8, 6), 50);
    CHECK_EQ(f.res.stats().bakes, static_cast<int64_t>(1));
    CHECK_EQ(f.res.stats().slowBakes, static_cast<int64_t>(1));
    CHECK(f.logged("residency: slow bake: tile ("));
    CHECK(f.logged("(a visible tile; ok, bake 1 since the start; thread CPU "));
    // issue 0417: the stand-in's 520 ms are a busy wait, so the thread's own CPU time is most of it (scheduler
    // granularity about 15.6 ms; a loaded machine may take some of it away)
    double threadMs = -1;
    for (const std::string& l : f.lines) {
        const char* k = std::strstr(l.c_str(), "; thread CPU ");
        if (k) threadMs = std::atof(k + std::strlen("; thread CPU "));
    }
    std::printf("  slow stand-in bake: thread CPU %.1f ms of 520\n", threadMs);
    CHECK(threadMs >= 100.0);
    CHECK(f.logged(" page faults)"));
    char line[2048];
    f.res.statsLine(line, sizeof(line));
    CHECK(std::strstr(line, " slow_bakes=1") != nullptr);
    // a fast bake is not one
    Fixture g(o);
    g.res.beforeDraw(g.d.ctx, g.r, view(8, 6), 50);
    CHECK_EQ(g.res.stats().bakes, static_cast<int64_t>(1));
    CHECK_EQ(g.res.stats().slowBakes, static_cast<int64_t>(0));
    CHECK(!g.logged("residency: slow bake"));
}

C5R_TEST(catch_up_keeps_the_burst_for_a_pan) {
    // the bake-ahead is done; a pan of one tile with some stale tiles in view: no catch-up, the burst as before
    ResidencyOptions o = fast();
    o.burst = 8;
    o.radius = 0;            // only the view is kept: a pan shows new tiles without a bake (5 for this pan)
    Fixture f(o);
    f.res.beforeDraw(f.d.ctx, f.r, view(8, 6), 50);
    f.idleUntilAhead();
    f.res.beforeDraw(f.d.ctx, f.r, view(8, 6), 50);
    const int64_t draws0 = f.res.stats().catchUpDraws;   // the first draw's
    CHECK_EQ(draws0, static_cast<int64_t>(1));
    for (uint32_t y = 6; y <= 7; ++y)
        for (uint32_t x = 6; x <= 10; ++x) CHECK(f.res.raiseTile(x, y));   // ten stale tiles under the camera
    const int64_t b0 = f.res.stats().bakes;
    f.res.beforeDraw(f.d.ctx, f.r, view(8, 7), 50);   // one tile north: one new row comes into the view
    const ResidencyStats& s = f.res.stats();
    std::printf("  pan: %lld bakes before the draw, %lld gray tile frames, %lld stale frames\n",
                static_cast<long long>(s.bakes - b0), static_cast<long long>(s.grayTileFrames),
                static_cast<long long>(s.staleFrames));
    CHECK_EQ(s.catchUpDraws, draws0);                 // new tiles within the burst: no catch-up
    CHECK_EQ(s.bakes - b0, static_cast<int64_t>(8));  // the burst, not every stale and new tile
    CHECK(s.staleFrames >= 1);
}

// ================================================================ the near-camera pair (issue 0332)
// The state machine of terrain-bake-passes.md 3.5.1 / 3.5.3 without a device: pools, the cull's switch, the sort, the
// up-res pass, release.

namespace {
// The visible list: tiles [first, first + n) with projected size p and near-plane distance = base + index * step.
std::vector<NearVisibleTile> visRange(uint32_t first, uint32_t n, float p, float dist0 = 100, float step = 1) {
    std::vector<NearVisibleTile> v;
    for (uint32_t i = 0; i < n; ++i) {
        NearVisibleTile t;
        t.tile = first + i;
        t.projectedSize = p;
        t.nearDistance = dist0 + step * static_cast<float>(i);
        v.push_back(t);
    }
    return v;
}
uint32_t heldCount(const NearCaches& m, uint32_t first, uint32_t n, uint32_t size = 0) {
    uint32_t c = 0;
    for (uint32_t i = first; i < first + n; ++i) c += m.drawsNear(i) && (!size || m.nearSize(i) == size);
    return c;
}
}  // namespace

C5R_TEST(near_pool_sizes_and_memory) {
    NearPoolCounts c;
    CHECK_EQ(c.count(64), 16u);
    CHECK_EQ(c.count(128), 0u);
    CHECK_EQ(c.count(256), 256u);
    CHECK_EQ(c.count(512), 64u);
    CHECK_EQ(c.count(100), 0u);
    // 256 x 349,524 + 64 x 1,398,100 + 16 x 21,844: the stock pools' video memory, beside the BC1 caches
    CHECK_EQ(nearPoolBytes(c), uint64_t(179306048));
    std::printf("  stock near pools: %llu B = %.1f MiB = %.1f MB\n", static_cast<unsigned long long>(nearPoolBytes(c)),
                nearPoolBytes(c) / 1048576.0, nearPoolBytes(c) / 1e6);
    NearPoolCounts none;
    none.n64 = none.n256 = none.n512 = 0;
    CHECK_EQ(nearPoolBytes(none), uint64_t(0));
}

C5R_TEST(near_init_refuses_bad_options) {
    NearCaches m;
    NearCacheOptions o;
    CHECK_ERR(m.init(0, o), Err::BadLayout);
    CHECK_ERR(m.init((1u << 22) + 1, o), Err::BadLayout);
    NearCacheOptions b = o; b.pools.n256 = 4097; CHECK_ERR(m.init(10, b), Err::BadLayout);
    b = o; b.minClass = 100; CHECK_ERR(m.init(10, b), Err::BadLayout);
    b = o; b.minClass = 1024; CHECK_ERR(m.init(10, b), Err::BadLayout);
    b = o; b.upresSettled = 0; CHECK_ERR(m.init(10, b), Err::BadLayout);
    b = o; b.perFrameMoving = 65; CHECK_ERR(m.init(10, b), Err::BadLayout);
    b = o; b.budgetSettled = 0; CHECK_ERR(m.init(10, b), Err::BadLayout);
    CHECK_OK(m.init(10, o));
    CHECK_EQ(m.freeCount(256), 256u);
    CHECK_EQ(m.freeCount(512), 64u);
    CHECK_EQ(m.freeCount(128), 0u);
    CHECK_EQ(m.freeCount(300), 0u);
    // calls out of range are ignored; queries answer "nothing"
    m.onBake(10, true, true);
    m.onEvict(99);
    m.onContentChanged(99);
    m.upresFailed(99);
    CHECK_EQ(static_cast<int>(m.state(99)), 0);
    CHECK(!m.drawsNear(99));
    CHECK_EQ(m.entry(99, 0), -1);
    CHECK_EQ(m.entry(0, 2), -1);
    NearVisibleTile bad;
    bad.tile = 50;
    NearFrameResult r = m.frame(&bad, 1, true);
    CHECK(r.upres.empty() && r.candidates == 0);
    r = m.frame(nullptr, 3, true);
    CHECK(r.upres.empty());
}

C5R_TEST(near_upres_two_per_settled_frame_one_while_moving) {
    NearCaches m;
    CHECK_OK(m.init(64, NearCacheOptions()));
    for (uint32_t i = 0; i < 10; ++i) m.onBake(i, true, true);
    std::vector<NearVisibleTile> v = visRange(0, 10, 200.0f);   // p 200: wanted 256
    v.push_back(v[0]);                                          // a duplicate is ignored
    NearVisibleTile unbaked;                                    // state 0: never a candidate
    unbaked.tile = 20;
    unbaked.projectedSize = 200;
    unbaked.nearDistance = 1e6f;
    v.push_back(unbaked);
    NearFrameResult r = m.frame(v.data(), v.size(), true);
    CHECK_EQ(r.upres.size(), size_t(2));
    CHECK_EQ(r.candidates, 2u);
    CHECK_EQ(r.budgetUsed, 2u);
    CHECK_EQ(m.state(20), kNearNone);
    // the farthest first (tile+0x1A4 descending): tiles 9 and 8
    CHECK(r.upres.size() == 2 && r.upres[0].tile == 9 && r.upres[1].tile == 8);
    CHECK(r.upres.size() == 2 && r.upres[0].size == 256 && r.upres[0].entry[0] >= 0 && r.upres[0].entry[1] >= 0);
    CHECK_EQ(m.state(9), kNearHeld);
    CHECK_EQ(m.nearSize(9), 256u);
    CHECK_EQ(m.wanted(9), 256u);
    CHECK(m.drawsNear(9));
    CHECK_EQ(m.freeCount(256), 252u);   // two pairs of two textures
    r = m.frame(v.data(), v.size(), false);   // moving: one
    CHECK_EQ(r.upres.size(), size_t(1));
    CHECK(r.upres.size() == 1 && r.upres[0].tile == 7);
    for (int f = 0; f < 10; ++f) m.frame(v.data(), v.size(), true);
    CHECK_EQ(heldCount(m, 0, 10, 256), 10u);
    CHECK_EQ(m.freeCount(256), 236u);
    r = m.frame(v.data(), v.size(), true);    // nothing left: no candidate
    CHECK(r.upres.empty() && r.candidates == 0 && r.budgetUsed == 0);
    NearCacheCounts c = m.counts();
    CHECK_EQ(c.held[2], 10u);
    CHECK_EQ(c.state[kNearHeld], 10u);
    CHECK_EQ(c.upres, int64_t(10));
    // a tile without t8 takes one texture
    m.onBake(30, true, false);
    NearVisibleTile one;
    one.tile = 30;
    one.projectedSize = 200;
    r = m.frame(&one, 1, true);
    CHECK(r.upres.size() == 1 && r.upres[0].entry[1] == -1);
    CHECK_EQ(m.freeCount(256), 235u);
}

// ticket 0495: the state as bytes, byte-exact, and a loaded machine goes on as the saved one did
C5R_TEST(near_state_save_load_round_trip_and_same_next_frames) {
    NearCaches a;
    CHECK_OK(a.init(64, NearCacheOptions()));
    for (uint32_t i = 0; i < 12; ++i) a.onBake(i, i % 3 != 0, i % 2 == 0);
    std::vector<NearVisibleTile> v = visRange(0, 12, 200.0f);
    for (int f = 0; f < 3; ++f) a.frame(v.data(), v.size(), f != 1);
    a.onContentChanged(11);
    std::vector<uint8_t> s;
    a.saveState(s);
    CHECK(s.size() > 100);
    NearCaches b;
    CHECK_OK(b.init(64, NearCacheOptions()));
    CHECK_OK(b.loadState(s.data(), s.size()));
    std::vector<uint8_t> s2;
    b.saveState(s2);
    CHECK(s2 == s);
    for (int f = 0; f < 6; ++f) {
        const NearFrameResult ra = a.frame(v.data(), v.size(), true), rb = b.frame(v.data(), v.size(), true);
        CHECK_EQ(ra.upres.size(), rb.upres.size());
        for (size_t k = 0; k < ra.upres.size() && k < rb.upres.size(); ++k)
            CHECK(ra.upres[k].tile == rb.upres[k].tile && ra.upres[k].size == rb.upres[k].size &&
                  ra.upres[k].entry[0] == rb.upres[k].entry[0] && ra.upres[k].entry[1] == rb.upres[k].entry[1]);
    }
    a.saveState(s);
    b.saveState(s2);
    CHECK(s2 == s);
    // contentBehind: a held pair whose tile's content moved on, until its rebake is taken
    NearCaches c;
    CHECK_OK(c.init(8, NearCacheOptions()));
    c.onBake(0, true, false);
    NearVisibleTile one;
    one.tile = 0;
    one.projectedSize = 200;
    c.frame(&one, 1, true);
    CHECK(c.drawsNear(0) && !c.contentBehind(0) && !c.contentBehind(1) && !c.contentBehind(99));
    c.onContentChanged(0);
    CHECK(c.contentBehind(0));
    c.frame(&one, 1, true);   // the rebake (request 2) takes a new pair at the current generation
    CHECK(c.drawsNear(0) && !c.contentBehind(0));
}

// ticket 0495: the negatives of loadState; each leaves the machine as it was
C5R_TEST(near_state_load_refuses_bad_bytes_and_keeps_the_machine) {
    NearCaches a;
    CHECK_OK(a.init(64, NearCacheOptions()));
    for (uint32_t i = 0; i < 4; ++i) a.onBake(i, true, true);
    std::vector<NearVisibleTile> v = visRange(0, 4, 200.0f);
    a.frame(v.data(), v.size(), true);
    std::vector<uint8_t> good;
    a.saveState(good);
    NearCaches b;
    CHECK_OK(b.init(64, NearCacheOptions()));
    std::vector<uint8_t> before;
    b.saveState(before);
    auto keeps = [&]() {
        std::vector<uint8_t> now;
        b.saveState(now);
        return now == before;
    };
    std::vector<uint8_t> x = good;
    x[0] ^= 1;   // magic
    CHECK_ERR(b.loadState(x.data(), x.size()), Err::BadLayout);
    CHECK(keeps());
    x = good;
    x[4] = 2;    // version
    CHECK_ERR(b.loadState(x.data(), x.size()), Err::BadLayout);
    CHECK(keeps());
    CHECK_ERR(b.loadState(good.data(), good.size() - 1), Err::Truncated);   // short
    CHECK(keeps());
    x = good;
    x.push_back(0);   // trailing
    CHECK_ERR(b.loadState(x.data(), x.size()), Err::Truncated);
    CHECK(keeps());
    NearCaches c;   // another tile count
    CHECK_OK(c.init(65, NearCacheOptions()));
    CHECK_ERR(c.loadState(good.data(), good.size()), Err::BadLayout);
    NearCacheOptions o2;   // other options
    o2.upresSettled = 3;
    NearCaches d;
    CHECK_OK(d.init(64, o2));
    CHECK_ERR(d.loadState(good.data(), good.size()), Err::BadLayout);
    // a pool entry held twice: the 256 free list's first entry repeated as the second (offset: header 12, options 36,
    // counters 32, then the 64 list's count and entries, the 128 list's count, the 256 list's count and entries)
    x = good;
    const size_t n64 = 16, at256 = 12 + 36 + 32 + 4 + 4 * n64 + 4 + 4;
    std::memcpy(&x[at256 + 8], &x[at256 + 4], 4);
    CHECK_ERR(b.loadState(x.data(), x.size()), Err::BadLayout);
    CHECK(keeps());
    CHECK_ERR(b.loadState(nullptr, 4), Err::BadLayout);
    CHECK(keeps());
    CHECK_OK(b.loadState(good.data(), good.size()));
    CHECK(!keeps());
}

C5R_TEST(near_sort_requested_state_then_state_then_keys) {
    NearCaches m;
    NearCacheOptions o;
    o.upresSettled = 1;
    CHECK_OK(m.init(16, o));
    // tile 0: bake-ahead (state 1), tile 1: baked for view (state 3), both visible; the state-1 tile sorts first
    m.onBake(0, false, true);
    m.onBake(1, true, true);
    NearVisibleTile v[2];
    v[0].tile = 1; v[0].projectedSize = 200; v[0].nearDistance = 500;   // farther, but state 3
    v[1].tile = 0; v[1].projectedSize = 200; v[1].nearDistance = 10;
    NearFrameResult r = m.frame(v, 2, true);
    CHECK(r.upres.size() == 1 && r.upres[0].tile == 0);
    CHECK_EQ(m.state(1), kNearView);
    r = m.frame(v, 2, true);
    CHECK(r.upres.size() == 1 && r.upres[0].tile == 1);
    // tile+0x1BC descending before the distance
    m.onBake(2, true, true);
    m.onBake(3, true, true);
    NearVisibleTile w[2];
    w[0].tile = 2; w[0].projectedSize = 200; w[0].nearDistance = 900; w[0].key1BC = 0;
    w[1].tile = 3; w[1].projectedSize = 200; w[1].nearDistance = 1; w[1].key1BC = 7;
    r = m.frame(w, 2, true);
    CHECK(r.upres.size() == 1 && r.upres[0].tile == 3);
    // a request 2 (state 5, content changed) sorts before every request 3
    m.onBake(4, true, true);
    m.onContentChanged(0);
    NearVisibleTile x[2];
    x[0].tile = 4; x[0].projectedSize = 200; x[0].nearDistance = 1000;
    x[1].tile = 0; x[1].projectedSize = 200; x[1].nearDistance = 1;
    r = m.frame(x, 2, true);
    CHECK_EQ(r.requests2, 1u);
    CHECK(r.upres.size() == 1 && r.upres[0].tile == 0);   // re-baked into a new pair; tile 4 waits
    CHECK_EQ(m.state(0), kNearHeld);
    CHECK_EQ(m.requested(0), kNearHeld);
    CHECK_EQ(m.state(4), kNearView);
}

// Array mode (stock): no fallback; the first candidates that fail block the ones behind them in that frame.
C5R_TEST(near_no_fallback_in_array_mode_and_fallback_without) {
    NearCaches m;
    NearCacheOptions o;
    o.pools.n512 = 4;   // two pairs
    CHECK_OK(m.init(32, o));
    for (uint32_t i = 0; i < 6; ++i) m.onBake(i, true, true);
    // tiles 0-3 want 512 and are the farthest; tiles 4, 5 want 256 and are closer
    std::vector<NearVisibleTile> v = visRange(0, 4, 300.0f, 1000, 10);
    std::vector<NearVisibleTile> v2 = visRange(4, 2, 200.0f, 10, 1);
    v.insert(v.end(), v2.begin(), v2.end());
    for (int f = 0; f < 20; ++f) m.frame(v.data(), v.size(), true);
    CHECK_EQ(heldCount(m, 0, 4, 512), 2u);
    CHECK_EQ(m.freeCount(512), 0u);
    CHECK_EQ(m.freeCount(256), 256u);       // free 256 textures, yet no 512-wanting tile takes one
    CHECK_EQ(heldCount(m, 4, 2), 0u);       // and the 256 tiles behind the two failing candidates never get theirs
    NearFrameResult r = m.frame(v.data(), v.size(), true);
    CHECK_EQ(r.candidates, 2u);
    CHECK_EQ(r.failed, 2u);
    CHECK_EQ(r.budgetUsed, 2u);             // failed attempts count against the frame's bake budget
    CHECK(m.counts().upresFailed >= 2);
    // without array mode the pool of 256 is taken instead
    NearCaches n;
    o.arrayMode = false;
    CHECK_OK(n.init(32, o));
    for (uint32_t i = 0; i < 6; ++i) n.onBake(i, true, true);
    for (int f = 0; f < 20; ++f) n.frame(v.data(), v.size(), true);
    CHECK_EQ(heldCount(n, 0, 4, 512), 2u);
    CHECK_EQ(heldCount(n, 0, 4, 256), 2u);
    CHECK_EQ(heldCount(n, 4, 2, 256), 2u);
}

C5R_TEST(near_size_change_requests_a_new_pair_only_when_the_pool_has_two) {
    NearCaches m;
    NearCacheOptions o;
    o.pools.n256 = 3;
    CHECK_OK(m.init(8, o));
    m.onBake(0, true, true);
    NearVisibleTile v;
    v.tile = 0;
    v.projectedSize = 300;   // wants 512
    m.frame(&v, 1, true);
    CHECK_EQ(m.nearSize(0), 512u);
    CHECK_EQ(m.freeCount(512), 62u);
    v.projectedSize = 200;   // now wants 256; the 256 pool has 3 free (> 1): request 2, then a 256 pair, the 512 back
    NearFrameResult r = m.frame(&v, 1, true);
    CHECK_EQ(r.requests2, 1u);
    CHECK(r.upres.size() == 1 && r.upres[0].size == 256);
    CHECK_EQ(m.nearSize(0), 256u);
    CHECK_EQ(m.freeCount(512), 64u);
    CHECK_EQ(m.freeCount(256), 1u);
    // back to 512: the 512 pool has 64 free: request 2 and a 512 pair
    v.projectedSize = 300;
    r = m.frame(&v, 1, true);
    CHECK_EQ(m.nearSize(0), 512u);
    CHECK_EQ(m.freeCount(256), 3u);
    // a second tile takes two 256 textures; the first now wants 256 with one free: no request, it keeps 512
    m.onBake(1, true, true);
    NearVisibleTile w;
    w.tile = 1;
    w.projectedSize = 200;
    m.frame(&w, 1, true);
    CHECK_EQ(m.freeCount(256), 1u);
    v.projectedSize = 200;
    r = m.frame(&v, 1, true);
    CHECK_EQ(r.requests2, 0u);
    CHECK_EQ(m.requested(0), kNearHeld);
    CHECK_EQ(m.nearSize(0), 512u);
    CHECK(m.drawsNear(0));
    // min class: a small projected size still wants 256 (TerrainDetailLevel 3)
    v.projectedSize = 10;
    m.frame(&v, 1, true);
    CHECK_EQ(m.wanted(0), 256u);
}

// Ticket 0667: a held pair whose content changed while the wanted pool is empty (array mode; the 512 pool full at a close
// camera) is baked again into its own textures, not kept with its old content (the EXE's rule left it drawing the
// content from before a live land/water change: 4916's murky film and sandy band, live-terrain.md 11.10 / 11.11).
C5R_TEST(near_content_change_with_an_empty_pool_rebakes_in_place) {
    NearCaches m;
    NearCacheOptions o;
    o.pools.n512 = 4;   // two pairs of two textures
    CHECK_OK(m.init(16, o));
    m.onBake(0, true, true);
    m.onBake(1, true, true);
    std::vector<NearVisibleTile> v = visRange(0, 2, 300.0f);   // both want 512
    for (int f = 0; f < 4; ++f) m.frame(v.data(), v.size(), true);
    CHECK_EQ(heldCount(m, 0, 2, 512), 2u);
    CHECK_EQ(m.freeCount(512), 0u);
    const int32_t e0 = m.entry(0, 0), e1 = m.entry(0, 1);
    CHECK(e0 >= 0 && e1 >= 0);
    // the content of tile 0 changes (a live terrain window or a road raised its tile)
    m.onContentChanged(0);
    CHECK(m.contentBehind(0));
    NearFrameResult r = m.frame(v.data(), v.size(), true);
    CHECK_EQ(r.requests2, 1u);
    CHECK_EQ(r.inPlace, 1u);
    CHECK_EQ(r.failed, 0u);
    CHECK(r.upres.size() == 1 && r.upres[0].tile == 0 && r.upres[0].size == 512 && r.upres[0].entry[0] == e0 &&
          r.upres[0].entry[1] == e1);
    CHECK(!m.contentBehind(0));
    CHECK(m.drawsNear(0));
    CHECK_EQ(m.entry(0, 0), e0);
    CHECK_EQ(m.freeCount(512), 0u);
    CHECK_EQ(m.requested(0), kNearHeld);
    // the next frame asks nothing more of it
    r = m.frame(v.data(), v.size(), true);
    CHECK_EQ(r.requests2, 0u);
    CHECK(r.upres.empty());
    // a failed bake of the in-place pair gives it back: the tile draws its BC1 pair (never the old content)
    m.onContentChanged(1);
    r = m.frame(v.data(), v.size(), true);
    CHECK(r.upres.size() == 1 && r.upres[0].tile == 1);
    m.upresFailed(1);
    CHECK(!m.drawsNear(1));
    CHECK_EQ(m.freeCount(512), 2u);

    // a pair of one texture whose tile now needs a second (t8) cannot be baked in place: it is given back
    NearCaches n;
    o.pools.n512 = 2;
    CHECK_OK(n.init(16, o));
    n.onBake(0, true, false);   // no t8: one texture
    n.onBake(1, true, false);
    for (int f = 0; f < 4; ++f) n.frame(v.data(), v.size(), true);
    CHECK_EQ(heldCount(n, 0, 2, 512), 2u);
    CHECK_EQ(n.freeCount(512), 0u);
    n.onBake(0, true, true);    // the tile's bake now has a t8
    n.onContentChanged(0);
    r = n.frame(v.data(), v.size(), true);
    CHECK_EQ(r.dropped, 1u);
    CHECK_EQ(r.inPlace, 0u);
    CHECK(!n.drawsNear(0));
    CHECK_EQ(n.state(0), kNearView);
    CHECK(!n.contentBehind(0));
}

C5R_TEST(near_release_on_evict_and_bc1_only_bake_and_failed_upres) {
    NearCaches m;
    CHECK_OK(m.init(8, NearCacheOptions()));
    for (uint32_t i = 0; i < 3; ++i) m.onBake(i, true, true);
    std::vector<NearVisibleTile> v = visRange(0, 3, 200.0f);
    for (int f = 0; f < 3; ++f) m.frame(v.data(), v.size(), true);
    CHECK_EQ(heldCount(m, 0, 3), 3u);
    CHECK_EQ(m.freeCount(256), 250u);
    m.onEvict(0);
    CHECK_EQ(m.state(0), kNearNone);
    CHECK(!m.drawsNear(0));
    CHECK_EQ(m.freeCount(256), 252u);
    m.onBcOnlyBake(1, true);
    CHECK_EQ(m.state(1), kNearView);
    CHECK_EQ(m.nearSize(1), 0u);
    CHECK_EQ(m.freeCount(256), 254u);
    // a BC1 bake of a tile that holds a pair keeps the pair (it is the state-5 bake's BC1 part)
    m.onBake(2, true, true);
    CHECK(m.drawsNear(2));
    CHECK_EQ(m.counts().released, int64_t(2));
    // the host could not bake a pair it was given: the pair goes back, the tile draws BC1
    NearVisibleTile t;
    t.tile = 1;
    t.projectedSize = 200;
    NearFrameResult r = m.frame(&t, 1, true);
    CHECK(r.upres.size() == 1);
    CHECK_EQ(m.freeCount(256), 252u);
    m.upresFailed(1);
    CHECK_EQ(m.state(1), kNearView);
    CHECK(!m.drawsNear(1));
    CHECK_EQ(m.freeCount(256), 254u);
    m.upresFailed(2);   // not up-ressed this frame: ignored
    CHECK(m.drawsNear(2));
}

// The camera history of 0306's camera 2 (terrain-bake-passes.md 3.5.2: load view, then the zoom into Honolulu): the
// 512 pool (64 textures) is filled by the tiles visible before the zoom; after it the 25 closest tiles want 512, find the
// pool empty and draw BC1, while the 32 pairs stay on the far rows. "Every visible tile" would give 57 near tiles; the
// EXE's (and this) machine gives 32, as measured live.
C5R_TEST(near_camera_history_of_0306_camera_2) {
    NearCaches m;
    CHECK_OK(m.init(256, NearCacheOptions()));
    // far rows: tiles 100..131 (32), visible at the load view wanting 512, plus tiles 132..141 wanting 256
    for (uint32_t i = 100; i < 142; ++i) m.onBake(i, true, true);
    std::vector<NearVisibleTile> load = visRange(100, 32, 300.0f, 400, 1);
    std::vector<NearVisibleTile> far256 = visRange(132, 10, 200.0f, 800, 1);
    load.insert(load.end(), far256.begin(), far256.end());
    for (int f = 0; f < 60; ++f) m.frame(load.data(), load.size(), true);
    CHECK_EQ(heldCount(m, 100, 32, 512), 32u);
    CHECK_EQ(heldCount(m, 132, 10, 256), 10u);
    CHECK_EQ(m.freeCount(512), 0u);
    // camera 2: the 32 far tiles still visible and wanting 512 (zoomed in), 25 close tiles (0..24) wanting 512
    for (uint32_t i = 0; i < 25; ++i) m.onBake(i, true, true);
    std::vector<NearVisibleTile> cam2 = visRange(100, 32, 400.0f, 300, 1);
    std::vector<NearVisibleTile> close = visRange(0, 25, 700.0f, 20, 1);
    cam2.insert(cam2.end(), close.begin(), close.end());
    for (int f = 0; f < 120; ++f) m.frame(cam2.data(), cam2.size(), true);
    uint32_t nearTiles = 0, bc1 = 0;
    for (const NearVisibleTile& t : cam2) (m.drawsNear(t.tile) ? nearTiles : bc1)++;
    std::printf("  camera 2 after the load view: %u of %zu visible tiles draw a near pair, %u BC1 (0306 live: 32 / 25)\n",
                nearTiles, cam2.size(), bc1);
    CHECK_EQ(nearTiles, 32u);
    CHECK_EQ(bc1, 25u);
    CHECK_EQ(heldCount(m, 0, 25), 0u);   // the closest tiles draw BC1
    for (uint32_t i = 0; i < 25; ++i) CHECK_EQ(m.state(i), kNearView);
    // Which tiles hold a pair depends on the history, not only on the view: the same final view (tiles 100..131 now the
    // closest, 200..231 farther, all wanting 512) after a history where 100..131 took the 512 pool first keeps the pairs
    // on 100..131; from an empty start the farthest-first order gives them to 200..231.
    std::vector<NearVisibleTile> fin = visRange(100, 32, 400.0f, 10, 1);
    std::vector<NearVisibleTile> farther = visRange(200, 32, 400.0f, 500, 1);
    fin.insert(fin.end(), farther.begin(), farther.end());
    for (uint32_t i = 200; i < 232; ++i) m.onBake(i, true, true);
    for (int f = 0; f < 120; ++f) m.frame(fin.data(), fin.size(), true);
    CHECK_EQ(heldCount(m, 100, 32, 512), 32u);
    CHECK_EQ(heldCount(m, 200, 32), 0u);
    NearCaches fresh;
    CHECK_OK(fresh.init(256, NearCacheOptions()));
    for (uint32_t i = 100; i < 132; ++i) fresh.onBake(i, true, true);
    for (uint32_t i = 200; i < 232; ++i) fresh.onBake(i, true, true);
    for (int f = 0; f < 120; ++f) fresh.frame(fin.data(), fin.size(), true);
    CHECK_EQ(heldCount(fresh, 200, 32, 512), 32u);
    CHECK_EQ(heldCount(fresh, 100, 32), 0u);
}

// ---------------------------------------------------------------- the Residency's switch (issue 0332)

namespace {
// Clears the pair to a colour per tile; fails when told to. Counts calls and checks the textures it gets.
class FakeNearBaker : public NearBaker {
public:
    ID3D11Device* dev = nullptr;
    int calls = 0, badTextures = 0;
    bool fail = false;
    Status bakeNear(ID3D11DeviceContext* ctx, TileId tile, uint64_t, ID3D11Texture2D* t0, ID3D11Texture2D* t8) override {
        calls++;
        if (fail) return Status::fail(Err::Device, "fake near bake failure");
        for (ID3D11Texture2D* t : {t0, t8}) {
            if (!t) continue;
            D3D11_TEXTURE2D_DESC d;
            t->GetDesc(&d);
            if (d.Format != DXGI_FORMAT_R8G8B8A8_UNORM || d.MipLevels != nearCacheMips(d.Width)) badTextures++;
            ID3D11RenderTargetView* rtv = nullptr;
            if (FAILED(dev->CreateRenderTargetView(t, nullptr, &rtv))) { badTextures++; continue; }
            const float c[4] = {tile.x / 16.0f, tile.y / 16.0f, 0.5f, 1};
            ctx->ClearRenderTargetView(rtv, c);
            rtv->Release();
        }
        return Status::Ok();
    }
};
}  // namespace

C5R_TEST(near_switch_off_by_default_and_needs_a_baker) {
    CHECK(!ResidencyOptions().nearCaches);
    Residency r;
    StandinBaker b{StandinFaults()};
    ResidencyOptions o;
    o.nearCaches = true;
    CHECK_ERR(r.init(&b, o), Err::BadLayout);   // no near baker
    FakeNearBaker nb;
    o.nearBaker = &nb;
    o.nearOptions.minClass = 100;
    CHECK_ERR(r.init(&b, o), Err::BadLayout);   // the machine's own checks
    o.nearOptions.minClass = 256;
    CHECK_OK(r.init(&b, o));
    CHECK(r.nearCaches() == nullptr);           // before a field
    // off: nothing of the near pairs
    Fixture f(fast());
    f.idleUntilAhead();
    for (int i = 0; i < 5; ++i) f.res.beforeDraw(f.d.ctx, f.r, view(8, 6), 50);
    CHECK(f.res.nearCaches() == nullptr);
    CHECK(!f.res.nearPair(8, 6, nullptr, nullptr, nullptr));
    CHECK_EQ(f.res.stats().nearTextures, 0u);
    CHECK_EQ(f.res.stats().nearUpres, int64_t(0));
}

C5R_TEST(near_switch_on_upres_two_per_settled_frame_and_release_on_eviction) {
    ResidencyOptions o = fast();
    o.radius = 0;                 // a move evicts everything outside the view
    FakeNearBaker nb;
    o.nearCaches = true;
    o.nearBaker = &nb;
    Fixture f(o);
    nb.dev = f.d.dev;
    f.res.beforeDraw(f.d.ctx, f.r, view(4, 4), 50);   // bakes the visible tiles (BC1), none held yet
    f.idleUntilAhead();
    const uint32_t vis = f.res.stats().visibleLand;
    CHECK(vis > 4);
    ResidencyView v = view(4, 4);
    f.res.beforeDraw(f.d.ctx, f.r, v, 50);
    CHECK_EQ(nb.calls, 2);
    CHECK_EQ(f.res.stats().nearLastAttempts, 2u);
    v.moving = true;
    f.res.beforeDraw(f.d.ctx, f.r, v, 50);
    CHECK_EQ(nb.calls, 3);
    v.moving = false;
    for (uint32_t i = 0; i < vis; ++i) f.res.beforeDraw(f.d.ctx, f.r, v, 50);
    const ResidencyStats& s = f.res.stats();
    CHECK_EQ(s.nearHeld256 + s.nearHeld512, vis);   // every visible land tile, 256 at this small view (min class)
    CHECK_EQ(s.nearHeld256, vis);
    CHECK_EQ(static_cast<uint32_t>(nb.calls), vis);
    CHECK_EQ(nb.badTextures, 0);
    CHECK_EQ(s.nearFree256 + s.nearTextures, 256u);   // pool textures made on first use, the rest free and unmade
    CHECK_EQ(s.nearBytes, static_cast<uint64_t>(s.nearTextures) * nearCacheBytes(256));
    // nearPair: the textures of a held tile
    const NearCaches* m = f.res.nearCaches();
    CHECK(m != nullptr);
    uint32_t found = 0;
    for (uint32_t i = 0; m && i < TX * TY; ++i) {
        if (!m->drawsNear(i)) continue;
        ID3D11Texture2D *a = nullptr, *b = nullptr;
        uint32_t size = 0;
        CHECK(f.res.nearPair(i % TX, i / TX, &a, &b, &size));
        CHECK(a != nullptr && size == 256);
        found++;
    }
    CHECK_EQ(found, vis);
    // a move with radius 0 evicts the old view's tiles: their pairs go back
    f.res.beforeDraw(f.d.ctx, f.r, view(12, 8), 50);
    CHECK(f.res.stats().evicted > 0);
    CHECK(f.res.stats().nearHeld256 < vis);
    uint32_t heldTex = 0;
    for (uint32_t i = 0; m && i < TX * TY; ++i) heldTex += (m->entry(i, 0) >= 0) + (m->entry(i, 1) >= 0);
    CHECK_EQ(f.res.stats().nearFree256 + heldTex, 256u);   // the pool's textures: free + held
    // a content change of a held tile asks for a new pair: baked again
    for (int i = 0; i < 40; ++i) f.res.beforeDraw(f.d.ctx, f.r, view(12, 8), 50);
    const int before = nb.calls;
    CHECK(f.res.raiseTile(12, 8));
    for (int i = 0; i < 4; ++i) f.res.beforeDraw(f.d.ctx, f.r, view(12, 8), 50);
    CHECK(nb.calls > before);
}

C5R_TEST(near_switch_failed_near_bake_leaves_bc1) {
    ResidencyOptions o = fast();
    FakeNearBaker nb;
    nb.fail = true;
    o.nearCaches = true;
    o.nearBaker = &nb;
    Fixture f(o);
    nb.dev = f.d.dev;
    f.idleUntilAhead();
    for (int i = 0; i < 3; ++i) f.res.beforeDraw(f.d.ctx, f.r, view(8, 6), 50);
    CHECK_EQ(nb.calls, 6);
    CHECK_EQ(f.res.stats().nearBakeFailures, int64_t(6));
    CHECK_EQ(f.res.stats().nearUpres, int64_t(0));
    CHECK_EQ(f.res.stats().nearHeld256, 0u);
    CHECK_EQ(f.res.stats().nearFree256, 256u);
    CHECK(f.logged("near pair of tile"));
    CHECK(f.res.tileBaked(8, 6));   // the BC1 pair stays
}

// ---------------------------------------------------------------- issue 0371: the draw takes the near pair

namespace {
// One draw of the renderer at the residency's view; the frame's counts by what each drawn tile's t0 was.
bool drawKinds(Fixture& f, const ResidencyView& v, terrain::FrameStats& fs) {
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = v.width;
    td.Height = v.height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D* tex = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    if (FAILED(f.d.dev->CreateTexture2D(&td, nullptr, &tex))) return false;
    if (FAILED(f.d.dev->CreateRenderTargetView(tex, nullptr, &rtv))) {
        tex->Release();
        return false;
    }
    terrain::Camera cam;
    std::memcpy(cam.view, v.view, sizeof(cam.view));
    std::memcpy(cam.proj, v.proj, sizeof(cam.proj));
    cam.width = v.width;
    cam.height = v.height;
    terrain::Options op;
    op.color = terrain::ColorSource::Caches;
    op.fallback = terrain::ColorSource::Material;
    op.water = false;
    fs = terrain::FrameStats();
    const Status st = f.r.draw(f.d.ctx, rtv, nullptr, cam, op, &fs);
    rtv->Release();
    tex->Release();
    if (!st.ok()) std::printf("  draw: %s\n", st.str().c_str());
    return st.ok();
}
}  // namespace

C5R_TEST(near_draw_binds_the_pair_in_state_5_and_the_bc1_pair_otherwise) {
    for (int arrays = 0; arrays < 2; ++arrays) {
        ResidencyOptions o = fast();
        o.arrays = arrays != 0;
        o.radius = 0;
        FakeNearBaker nb;
        o.nearCaches = true;
        o.nearBaker = &nb;
        Fixture f(o);
        nb.dev = f.d.dev;
        const ResidencyView v = view(4, 4);
        // the visible tiles' BC1 bakes, no pair yet; no time budget (an ASan build bakes fewer tiles in 50 ms, and the
        // check below needs every visible tile baked in this first frame)
        f.res.beforeDraw(f.d.ctx, f.r, v, 1e6);
        terrain::FrameStats fs;
        CHECK(drawKinds(f, v, fs));
        CHECK_EQ(fs.drawnKind[2] + fs.drawnKind[3], 0u);
        CHECK(fs.drawnKind[1] > 0);
        const uint32_t bc1Before = fs.drawnKind[1];
        f.res.beforeDraw(f.d.ctx, f.r, v, 1e6);  // two up-res: two tiles drawn from their pair from now on
        CHECK(drawKinds(f, v, fs));
        CHECK_EQ(f.res.stats().nearDrawn256, 2u);
        CHECK_EQ(fs.drawnKind[2], 2u);
        CHECK_EQ(fs.drawnKind[1], bc1Before - 2);
        const uint32_t vis = f.res.stats().visibleLand;
        for (uint32_t i = 0; i < vis; ++i) f.res.beforeDraw(f.d.ctx, f.r, v, 50);
        CHECK(drawKinds(f, v, fs));
        CHECK_EQ(f.res.stats().nearDrawn256, vis);
        CHECK_EQ(fs.drawnKind[2], f.res.stats().nearDrawn256);   // the renderer drew what the residency handed it
        CHECK_EQ(fs.drawnKind[3], 0u);
        // every drawn near tile's view is a pool texture of its pair
        uint32_t pairs = 0;
        for (uint32_t i = 0; i < TX * TY; ++i) {
            ID3D11Texture2D *a = nullptr, *b = nullptr;
            uint32_t size = 0;
            if (f.res.nearPair(i % TX, i / TX, &a, &b, &size)) pairs++;
        }
        CHECK_EQ(pairs, vis);
        // radius 0: a move evicts the old tiles and their pairs; the new view's tiles draw BC1 until their up-res
        const ResidencyView w = view(12, 8);
        f.res.beforeDraw(f.d.ctx, f.r, w, 50);
        CHECK(drawKinds(f, w, fs));
        CHECK(fs.drawnKind[2] <= 2u);
        CHECK(fs.drawnKind[1] > 0);
        CHECK_EQ(fs.drawnKind[4], 0u);
        // a failed near bake leaves its tile on BC1 (no stale view handed over)
        nb.fail = true;
        const int64_t failed0 = f.res.stats().nearBakeFailures;
        f.res.beforeDraw(f.d.ctx, f.r, w, 50);
        CHECK(f.res.stats().nearBakeFailures > failed0);
        const uint32_t drawnNear = f.res.stats().nearDrawn256;
        CHECK(drawKinds(f, w, fs));
        CHECK_EQ(fs.drawnKind[2], drawnNear);
    }
}

C5R_TEST(near_off_draws_bc1_only_and_near_stats_stay_off_the_stats_line) {
    ResidencyOptions o = fast();
    Fixture f(o);
    const ResidencyView v = view(4, 4);
    for (int i = 0; i < 5; ++i) f.res.beforeDraw(f.d.ctx, f.r, v, 50);
    terrain::FrameStats fs;
    CHECK(drawKinds(f, v, fs));
    CHECK_EQ(fs.drawnKind[2] + fs.drawnKind[3], 0u);
    CHECK(fs.drawnKind[1] > 0);
    char line[4096];
    f.res.statsLine(line, sizeof line);
    CHECK(std::strstr(line, "near=1") == nullptr);
    FakeNearBaker nb;
    o.nearCaches = true;
    o.nearBaker = &nb;
    Fixture g(o);
    nb.dev = g.d.dev;
    g.res.beforeDraw(g.d.ctx, g.r, v, 50);
    g.res.beforeDraw(g.d.ctx, g.r, v, 50);
    // the near pairs' figures are in ResidencyStats (the server writes them on a line of its own), never in statsLine,
    // whose length the server's terrain line depends on (0371: 22 more fields cut that line's end)
    char line2[4096];
    g.res.statsLine(line2, sizeof line2);
    CHECK(std::strstr(line2, "near") == nullptr);
    CHECK_EQ(g.res.stats().nearHeld256, 2u);
    CHECK_EQ(g.res.stats().nearDrawn256, 2u);
}

// ---------------------------------------------------------------- ticket 0712: the next bakes (prep ahead's hints)

// NearFrameResult::next: the sorted list's tiles after this frame's candidates that the pass would take, at most
// kNearNext, in the list's order; the next moving frame's up-res is the first of them. The machine is the same with or
// without reading it (next is a by-product).
C5R_TEST(near_next_names_the_next_frames_upres) {
    NearCaches m;
    CHECK_OK(m.init(64, NearCacheOptions()));
    for (uint32_t i = 0; i < 10; ++i) m.onBake(i, true, true);
    const std::vector<NearVisibleTile> v = visRange(0, 10, 200.0f);   // the farthest first: 9, 8, ...
    NearFrameResult r = m.frame(v.data(), v.size(), false);          // moving: one candidate
    CHECK(r.upres.size() == 1 && r.upres[0].tile == 9);
    CHECK_EQ(r.next.size(), size_t(NearFrameResult::kNearNext));
    CHECK(r.next.size() == 4 && r.next[0] == 8 && r.next[1] == 7 && r.next[2] == 6 && r.next[3] == 5);
    for (int f = 0; f < 3; ++f) {
        const uint32_t want = r.next.empty() ? 999 : r.next[0];
        r = m.frame(v.data(), v.size(), false);
        CHECK(r.upres.size() == 1 && r.upres[0].tile == want);
    }
    // settled: two candidates, the next ones after them
    r = m.frame(v.data(), v.size(), true);
    CHECK(r.upres.size() == 2 && r.upres[0].tile == 5 && r.upres[1].tile == 4);
    CHECK(r.next.size() == 4 && r.next[0] == 3 && r.next[1] == 2 && r.next[2] == 1 && r.next[3] == 0);
    r = m.frame(v.data(), v.size(), true);
    CHECK(r.upres.size() == 2 && r.upres[0].tile == 3 && r.next.size() == 2 && r.next[0] == 1 && r.next[1] == 0);
    // the last two: no next
    r = m.frame(v.data(), v.size(), true);
    CHECK(r.upres.size() == 2 && r.next.empty());
    // a state-0 tile and a held one are never in next
    NearCaches z;
    CHECK_OK(z.init(8, NearCacheOptions()));
    NearVisibleTile none;
    none.tile = 3;
    none.projectedSize = 200;
    r = z.frame(&none, 1, false);
    CHECK(r.upres.empty() && r.next.empty());
}

// Residency::nextBakes: the idle order from its cursor before a camera and after; the visible tiles that still need a bake
// after a beforeDraw whose burst was short; a raised tile with its new generation; nothing once everything is baked or
// with maxAhead 0; no near hints with near pairs off. Reading changes nothing (the same bakes follow).
C5R_TEST(next_bakes_follow_the_idle_order_and_the_visible_set) {
    ResidencyOptions o = fast();
    o.burst = 1;
    o.catchUpVisible = false;
    Fixture f(o);
    std::vector<Residency::Hint> nearH, ahead;   // `near` is a Windows macro
    f.res.nextBakes(nearH,ahead, 5);
    CHECK(nearH.empty());
    CHECK_EQ(ahead.size(), size_t(5));
    for (const Residency::Hint& h : ahead) {
        CHECK(h.tile.x < TX && h.tile.y < TY && f.res.tileClass(h.tile.x, h.tile.y) >= 2);
        CHECK_EQ(h.contentGeneration, uint64_t(1));
        CHECK(!f.res.tileBaked(h.tile.x, h.tile.y));
    }
    // the first idle bake takes the first hint (the first call fills the token bucket from 0, the second bakes once: its
    // first check passes, the next finds the budget spent)
    f.res.idle(f.d.ctx, 0.0001);
    Sleep(2);
    f.res.idle(f.d.ctx, 0.0001);
    if (!ahead.empty()) CHECK(f.res.tileBaked(ahead[0].tile.x, ahead[0].tile.y));
    std::vector<Residency::Hint> none;
    f.res.nextBakes(nearH,none, 0);
    CHECK(none.empty());
    // a view: one visible bake (burst 1); the hints start with the visible tiles still without one
    const ResidencyView v = view(8, 6);
    f.res.beforeDraw(f.d.ctx, f.r, v, 50);
    f.res.nextBakes(nearH,ahead, 3);
    CHECK_EQ(ahead.size(), size_t(3));
    for (const Residency::Hint& h : ahead) CHECK(!f.res.tileBaked(h.tile.x, h.tile.y));
    // everything baked: no hint
    f.idleUntilAhead();
    f.res.beforeDraw(f.d.ctx, f.r, v, 50);
    for (int i = 0; i < 64; ++i) f.res.beforeDraw(f.d.ctx, f.r, v, 50);
    f.res.nextBakes(nearH,ahead, 6);
    CHECK(ahead.empty());
    // a raised tile: hinted with its new generation
    CHECK(f.res.raiseTile(5, 5));
    f.res.nextBakes(nearH,ahead, 6);
    CHECK(ahead.size() == 1 && ahead[0].tile.x == 5 && ahead[0].tile.y == 5 && ahead[0].contentGeneration == 2);
    CHECK(nearH.empty());   // near pairs off
}

C5R_TEST_MAIN
