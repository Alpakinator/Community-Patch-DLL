// render/tools/terrainshot/terrainshot.cpp - the offline terrain picture (epic 3200, issue 3202). Own code.
//
//   terrainshot (--gt DIR | --c5hf FILE) --camera CAMERA.json --out OUT.png [--world OUT.f32] [--exe PATH]
//               [--color constant|material|height|normal] [--rgba 0xAABBGGRR] [--no-water] [--water-z Z]
//               [--tile-plots N] [--pass N] [--tess F] [--view I] [--warp] [--wire] [--clear 0xAABBGGRR]
//
// Draws the height field of a ground-truth folder (D:/c5-64bit-work/i2-ground-truth/<map>) or of a C5HF tap file at
// the camera of a gt_reader.py camera JSON, at the camera's back-buffer size, with the EXE's TerrainShader programs
// (c5r::terrain), on a windowless hardware D3D11 device (WARP with --warp or when no hardware device exists). Writes
// the colour PNG and, with --world, the world position per pixel (float32 x, y, z, w; w 1 terrain, 2 water, 0 none)
// plus a .json with its size. --exe defaults to C5R_EXE. Nothing is ever written next to the inputs unless asked.
// Exit codes: 0 done, 1 an input or device error (the message names the check), 2 usage. Never 3 (the launch gates'
// "lock held").
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>
#include "terrainshot_core.h"

using namespace c5r;

namespace {
int usage() {
    std::fprintf(stderr,
                 "usage: terrainshot (--gt DIR | --c5hf FILE) --camera CAMERA.json --out OUT.png [--world OUT.f32]\n"
                 "       [--exe PATH] [--color constant|material|height|normal] [--rgba 0xAABBGGRR] [--no-water]\n"
                 "       [--water-z Z] [--tile-plots N] [--pass N] [--tess F] [--view I] [--warp] [--wire] [--clear C]\n");
    return 2;
}
std::string u8(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s += c < 128 ? static_cast<char>(c) : '?';
    return s;
}
int fail(const char* step, const Status& st) {
    std::fprintf(stderr, "terrainshot: %s: %s\n", step, st.str().c_str());
    return 1;
}
bool parseU32(const std::wstring& s, uint32_t& out) {
    wchar_t* e = nullptr;
    const unsigned long v = std::wcstoul(s.c_str(), &e, 0);
    if (!e || *e || s.empty()) return false;
    out = static_cast<uint32_t>(v);
    return true;
}
bool parseF(const std::wstring& s, float& out) {
    wchar_t* e = nullptr;
    out = static_cast<float>(std::wcstod(s.c_str(), &e));
    return e && !*e && !s.empty();
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    std::wstring gt, c5hf, camPath, out, world, exe, color = L"normal";
    terrain::Options o;
    o.color = terrain::ColorSource::Normal;
    o.clearRgba = 0xFF545454u;  // the D33 clear colour (84, 84, 84)
    uint32_t tilePlots = terrain::kDefaultTilePlots, view = 0;
    bool warp = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        auto next = [&](std::wstring& v) -> bool {
            if (i + 1 >= argc) return false;
            v = argv[++i];
            return true;
        };
        std::wstring v;
        if (a == L"--gt") { if (!next(gt)) return usage(); }
        else if (a == L"--c5hf") { if (!next(c5hf)) return usage(); }
        else if (a == L"--camera") { if (!next(camPath)) return usage(); }
        else if (a == L"--out") { if (!next(out)) return usage(); }
        else if (a == L"--world") { if (!next(world)) return usage(); }
        else if (a == L"--exe") { if (!next(exe)) return usage(); }
        else if (a == L"--color") { if (!next(color)) return usage(); }
        else if (a == L"--rgba") { if (!next(v) || !parseU32(v, o.constantRgba)) return usage(); }
        else if (a == L"--clear") { if (!next(v) || !parseU32(v, o.clearRgba)) return usage(); }
        else if (a == L"--no-water") o.water = false;
        else if (a == L"--water-z") { if (!next(v) || !parseF(v, o.waterZ)) return usage(); }
        else if (a == L"--tile-plots") { if (!next(v) || !parseU32(v, tilePlots)) return usage(); }
        else if (a == L"--pass") { uint32_t p = 0; if (!next(v) || !parseU32(v, p)) return usage(); o.pass = static_cast<int>(p); }
        else if (a == L"--tess") { if (!next(v) || !parseF(v, o.tessFactor)) return usage(); }
        else if (a == L"--view") { if (!next(v) || !parseU32(v, view)) return usage(); }
        else if (a == L"--warp") warp = true;
        else if (a == L"--wire") o.wireframe = true;
        else return usage();
    }
    if ((gt.empty() == c5hf.empty()) || camPath.empty() || out.empty()) return usage();
    if (color == L"constant") o.color = terrain::ColorSource::Constant;
    else if (color == L"material") o.color = terrain::ColorSource::Material;
    else if (color == L"height") o.color = terrain::ColorSource::Height;
    else if (color == L"normal") o.color = terrain::ColorSource::Normal;
    else return usage();
    if (exe.empty()) {
        const wchar_t* e = _wgetenv(L"C5R_EXE");
        if (e) exe = e;
    }
    if (exe.empty()) {
        std::fprintf(stderr, "terrainshot: no EXE: give --exe or set C5R_EXE (CivilizationV_DX11.exe, read-only)\n");
        return 1;
    }

    heightfield::HeightGrid g;
    Status st = gt.empty() ? terrainshot::loadTapFile(c5hf, g) : terrainshot::loadGroundTruth(gt, g);
    if (!st.ok()) return fail("height field", st);
    terrain::Camera cam;
    st = terrainshot::loadCamera(camPath, static_cast<int>(view), cam);
    if (!st.ok()) return fail("camera", st);
    terrainshot::Device d;
    st = d.create(warp);
    if (!st.ok()) return fail("device", st);
    terrain::Renderer r;
    st = r.init(d.dev, exe, L"");
    if (!st.ok()) return fail("init", st);
    const auto t0 = std::chrono::steady_clock::now();
    st = r.setHeightField(g, tilePlots);
    const double fieldMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!st.ok()) return fail("setHeightField", st);
    std::printf("terrainshot: setHeightField %.1f ms (%u x %u samples)\n", fieldMs, g.samplesX, g.samplesY);
    terrainshot::ShotResult res;
    st = terrainshot::renderShot(d, r, cam, o, !world.empty(), res);
    if (!st.ok()) return fail("draw", st);
    st = terrainshot::writePngRgba(out, res.width, res.height, res.rgba);
    if (!st.ok()) return fail("png", st);
    if (!world.empty()) {
        st = terrainshot::writeWorld(world, res.width, res.height, res.world);
        if (!st.ok()) return fail("world", st);
    }
    const terrain::TileGrid tg = r.tileGrid();
    std::printf("terrainshot: %ux%u, device %s, map %ux%u plots (%ux%u samples), wrap %d, waterZ %.4f, tiles %ux%u of %u "
                "plots, drawn %u, culled %u, pass %d, tess %.1f, cpu %.2f ms, gpu %.3f ms -> %s\n",
                res.width, res.height, d.adapter.c_str(), g.mapW, g.mapH, g.samplesX, g.samplesY, g.wrapX ? 1 : 0,
                static_cast<double>(g.waterZ), tg.tilesX, tg.tilesY, tg.tilePlotsX, res.stats.tilesDrawn,
                res.stats.tilesCulled, res.stats.passUsed, static_cast<double>(res.stats.tessFactorUsed), res.stats.cpuMs,
                res.gpuMs, u8(out).c_str());
    return 0;
}
