// render/tools/quadtest/quadtest.cpp - task 2906: a game texture drawn by a game shader, no window, into PNG files.
//
//   quadtest [--game DIR] [--exe PATH] [--texture NAME] [--set NAME] [--pass N] [--out DIR] [--evidence FILE.png]
//
// Draws the quad twice with the game pass (identity transform, and a rotated and scaled transform written into the
// constant buffer by member name) and twice with a passthrough reference HLSL pair, compares each pair, and checks
// that the transform moved the image. PNGs go to --out (default C:/c5-64bit-work/2900/2906, D133); --evidence writes a
// 128x128 downscaled copy of the rotated game image. Exit 0 when every check passes.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <windows.h>
#include "quadtest_core.h"

using namespace c5r;
using namespace c5r::quadtest;

static std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

int main(int argc, char** argv) {
    const char* profile = std::getenv("USERPROFILE");  // the defaults are under the user folder (%USERPROFILE%)
    const std::string home = profile ? profile : "";
    std::string game = home + "/Desktop/pure_vp/Sid Meier's Civilization V";
    std::string exe = home + "/tools/ghidra-projects/bin/CivilizationV_DX11.exe";
    std::string tex = "euro_grassland_d.dds", set = "CompositeShader", out = "C:/c5-64bit-work/2900/2906", evidence;
    uint32_t pass = 0;
    if (const char* e = std::getenv("C5R_GAME_DIR")) game = e;
    if (const char* e = std::getenv("C5R_EXE")) exe = e;
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string k = argv[i], v = argv[i + 1];
        if (k == "--game") game = v; else if (k == "--exe") exe = v; else if (k == "--texture") tex = v;
        else if (k == "--set") set = v; else if (k == "--pass") pass = static_cast<uint32_t>(std::atoi(v.c_str()));
        else if (k == "--out") out = v; else if (k == "--evidence") evidence = v;
        else { std::fprintf(stderr, "unknown option %s\n", k.c_str()); return 2; }
    }
    auto failed = [](const char* what, const Status& s) { std::printf("quadtest: FAILED: %s: %s\n", what, s.str().c_str()); return 1; };

    Device d;
    Status s = d.create();
    if (!s) return failed("device", s);
    std::printf("device: %s (no window)\n", d.adapter.c_str());

    shaders::ShaderSection sec;
    if (!(s = sec.open(widen(exe)))) return failed("EXE .shader section", s);
    GamePass gp;
    if (!(s = findPass(sec, set.c_str(), pass, gp))) return failed("shader set", s);
    std::printf("set %s pass %u: VS 0x%08x (%s, %u B), PS 0x%08x (%s, %u B), cb0 %s %u B\n", set.c_str(), pass, gp.vs->va,
                gp.vs->model().c_str(), gp.vs->size, gp.ps->va, gp.ps->model().c_str(), gp.ps->size, gp.cb0->name,
                gp.cb0->sizeBytes);

    ID3D11ShaderResourceView* srv = nullptr;
    std::string origin, format;
    if (!(s = loadGameTexture(d, widen(game), tex, &srv, &origin, &format))) return failed("texture", s);
    std::printf("texture %s: %s, from %s\n", tex.c_str(), format.c_str(), origin.c_str());

    DrawParams id;
    DrawParams rot;
    const float c = std::cos(0.5f) * 0.8f, sn = std::sin(0.5f) * 0.8f; // rotate 0.5 rad, scale 0.8, shift right 0.1
    const float m[16] = {c, sn, 0, 0, -sn, c, 0, 0, 0, 0, 1, 0, 0.1f, 0, 0, 1};
    std::memcpy(rot.transform, m, sizeof m);

    Image gi, ri, gr, rr;
    if (!(s = drawGame(d, gp, srv, id, gi))) return failed("game draw (identity)", s);
    if (!(s = drawReference(d, srv, id, ri))) return failed("reference draw (identity)", s);
    if (!(s = drawGame(d, gp, srv, rot, gr))) return failed("game draw (rotated)", s);
    if (!(s = drawReference(d, srv, rot, rr))) return failed("reference draw (rotated)", s);
    srv->Release();

    CreateDirectoryA(out.c_str(), nullptr);
    writePng(widen(out + "/game_identity.png"), gi);
    writePng(widen(out + "/ref_identity.png"), ri);
    writePng(widen(out + "/game_rotated.png"), gr);
    writePng(widen(out + "/ref_rotated.png"), rr);
    if (!evidence.empty() && !(s = writePng(widen(evidence), downscale(gr, 128, 128)))) return failed("evidence png", s);

    Diff a, b, moved;
    compare(gi, ri, a);
    compare(gr, rr, b);
    compare(gi, gr, moved);
    std::printf("game vs reference, identity: %u px covered, mean |d| %.4f, max %u, >2: %.5f\n", a.covered, a.meanAbs, a.maxAbs, a.fracOver2);
    std::printf("game vs reference, rotated:  %u px covered, mean |d| %.4f, max %u, >2: %.5f\n", b.covered, b.meanAbs, b.maxAbs, b.fracOver2);
    std::printf("identity vs rotated (game):  mean |d| %.4f (must be large: the cbuffer moved the quad)\n", moved.meanAbs);
    const double coverage = double(a.covered) / (double(id.width) * id.height); // quad covers 0.75^2 of the target
    bool ok = a.maxAbs <= 2 && b.maxAbs <= 2 && moved.meanAbs > 5.0 && coverage > 0.5 && coverage < 0.6;
    std::printf("coverage %.4f (expected 0.5625); output in %s\nquadtest: %s\n", coverage, out.c_str(), ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
