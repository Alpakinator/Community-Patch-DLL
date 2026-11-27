// initbench (issue 0198): the install-reading part of the server's render start, timed in one arm per process, so a
// driver (render/evidence/0198/initbench_series.py) can interleave the arms. Written 2026-11-15 for this repository
// (GPL-3.0-or-later).
//
// What it times is what server/src/render/srv_render_scene.cpp's Scene::init does with the install, with the real
// modules: the terrain renderer's init and the water module's init (the ocean and rivers, which open their own index of
// the install in every arm, then the coast waves). The arms differ only in who builds an index of the install
// (gamefs::GameFiles::open, 1.3-1.5 s on an install, terrain-surface.md 6):
//   pre0188  the terrain without an install (no fog ramp), the waves with their own index      (before issue 0188)
//   0188     the terrain with its own index (gameDir), the waves with their own index          (0188 until 0198)
//   0198     one index opened first, read by the terrain and by the waves                       (issue 0198)
// The residency's own index (srv_residency_source.cpp) is the same in every arm and not part of this.
//
//   initbench.exe --arm pre0188|0188|0198 --game DIR --exe EXE [--warp]
// Output: one line `ARM <arm> total_ms <t> index_ms <i> terrain_ms <a> water_ms <b> fog <0|1> waves <0|1>`; exit 0, 1
// on an init error (the error printed), 2 on usage.
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include "c5r/gamefs/gamefiles.h"
#include "c5r/shaders/shadersection.h"
#include "c5r/terrain/terrain.h"
#include "c5r/water/water.h"
#include "c5r/water/waves.h"
#include "terrainshot_core.h"

using namespace c5r;
using Clock = std::chrono::steady_clock;

static double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

int wmain(int argc, wchar_t** argv) {
    std::wstring arm, game, exe;
    bool warp = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--arm" && i + 1 < argc) arm = argv[++i];
        else if (a == L"--game" && i + 1 < argc) game = argv[++i];
        else if (a == L"--exe" && i + 1 < argc) exe = argv[++i];
        else if (a == L"--warp") warp = true;
        else {
            std::fprintf(stderr, "initbench: unknown argument %ls\n", a.c_str());
            return 2;
        }
    }
    if ((arm != L"pre0188" && arm != L"0188" && arm != L"0198") || game.empty() || exe.empty()) {
        std::fprintf(stderr, "usage: initbench --arm pre0188|0188|0198 --game DIR --exe EXE [--warp]\n");
        return 2;
    }
    terrainshot::Device d;
    Status st = d.create(warp);
    if (!st.ok()) {
        std::printf("FAIL device: %s\n", st.str().c_str());
        return 1;
    }
    const Clock::time_point t0 = Clock::now();
    double indexMs = 0, terrainMs = 0, waterMs = 0;
    gamefs::GameFiles shared;
    terrain::Renderer t;
    water::Renderer w;
    water::WaveRenderer wr;
    bool wavesOk = false;
    if (arm == L"0198") {
        const Clock::time_point ti = Clock::now();
        st = shared.open(game);
        indexMs = msSince(ti);
        if (!st.ok()) {
            std::printf("FAIL index: %s\n", st.str().c_str());
            return 1;
        }
    }
    {
        const Clock::time_point ta = Clock::now();
        if (arm == L"pre0188") st = t.init(d.dev, exe, std::wstring());
        else if (arm == L"0188") st = t.init(d.dev, exe, game);
        else st = t.init(d.dev, exe, shared);
        terrainMs = msSince(ta);
        if (!st.ok()) {
            std::printf("FAIL terrain: %s\n", st.str().c_str());
            return 1;
        }
    }
    {   // as C5rWaterModule::init (server/src/render/srv_water_scene.cpp)
        const Clock::time_point tb = Clock::now();
        st = w.init(d.dev, exe, game);
        if (!st.ok()) {
            std::printf("FAIL water: %s\n", st.str().c_str());
            return 1;
        }
        shaders::ShaderSection sec;
        gamefs::GameFiles own;
        Status ws = sec.open(exe);
        if (ws.ok() && arm != L"0198") ws = own.open(game);
        if (ws.ok()) ws = wr.init(d.dev, sec, arm == L"0198" ? shared : own);
        wavesOk = ws.ok();
        waterMs = msSince(tb);
        if (!wavesOk) std::printf("NOTE waves: %s\n", ws.str().c_str());
    }
    const double total = msSince(t0);
    std::printf("ARM %ls total_ms %.3f index_ms %.3f terrain_ms %.3f water_ms %.3f fog %d waves %d\n", arm.c_str(), total, indexMs,
                terrainMs, waterMs, t.fogRamp() ? 1 : 0, wavesOk ? 1 : 0);
    wr.release();
    w.release();
    t.release();
    return 0;
}
