// Tests on the real EXE (task 2902): C5R_EXE = CivilizationV_DX11.exe (read-only; build 4390913 or, since ticket 4966, 26577625); SKIP without it.
// The expected numbers are the measurements of 2026-11-10 recorded in research/05-graphics-composition/asset-pipeline.md
// section 2.
#include <cstdio>
#include <map>

#include "c5r/shaders/d3dcheck.h"
#include "c5r/shaders/reflect.h"
#include "c5r/shaders/shadersection.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::shaders;

namespace {
std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }
} // namespace

C5R_TEST(game_exe_shader_section_index) {
    const std::string exe = c5r::test::exePath();
    ShaderSection sec;
    CHECK_OK(sec.open(widen(exe)));
    // ticket 4966: C5R_EXE may be either known build (the .shader section is byte-identical, moved by -0x170000 in build
    // 26577625; its place in the file differs); the build-4390913 numbers below hold on both through findByVa
    CHECK(sec.build() >= 0);
    if (sec.build() == 0) {
        CHECK_EQ(sec.sectionVa(), 0x0619B000u);
        CHECK_EQ(sec.sectionFileOffset(), 0x8B6A00u);
    }
    CHECK(!sec.blobs().empty() && sec.blobs()[0].exeVa == sec.sectionVa() && sec.blobs()[0].va == 0x0619B000u);
    std::printf("game_exe_shader_section_index: EXE build id %d\n", sec.build());
    CHECK_EQ(sec.blobs().size(), size_t(1417));
    uint64_t bytes = 0;
    for (const ShaderBlob& b : sec.blobs()) bytes += b.size;
    CHECK_EQ(bytes, uint64_t(6993892));
    CHECK_EQ(sec.gaps().size(), size_t(712));
    for (const SectionGap& g : sec.gaps()) CHECK(g.allZero && g.size == 4);
    CHECK(sec.createSitesApplied());
    std::map<std::string, int> n;
    for (const ShaderBlob& b : sec.blobs()) {
        n[std::to_string(b.set) + stageName(b.stage)]++;
        CHECK(b.createSite != 0 && b.handleGlobal != 0);
        CHECK(b.exeCreateSite != 0 && b.exeHandleGlobal != 0 && b.exeCreateFunction != 0);
        CHECK(sec.findByVa(b.va) == &b && sec.findByExeVa(b.exeVa) == &b);
        if (sec.build() == 0) CHECK(b.exeVa == b.va && b.exeCreateSite == b.createSite);
        CHECK_EQ(b.set, b.major * 10 + b.minor); // every blob's set is its own shader model
    }
    CHECK_EQ(n["40vs"], 126); CHECK_EQ(n["40ps"], 320);
    CHECK_EQ(n["41vs"], 126); CHECK_EQ(n["41ps"], 352);
    CHECK_EQ(n["50vs"], 128); CHECK_EQ(n["50ps"], 362); CHECK_EQ(n["50hs"], 2); CHECK_EQ(n["50ds"], 1);
    // the terrain VS GATE2 named: vertex-ID only, created at 0x4ee47f into the handle global 0x2de1c8c
    const ShaderBlob* t = sec.findByVa(0x067D6490);
    CHECK(t && t->size == 772 && t->stage == Stage::Vertex && t->set == 50);
    CHECK(t && t->createSite == 0x4EE47F && t->handleGlobal == 0x2DE1C8C && t->createFunction == 0x4E5C20);
}

C5R_TEST(game_exe_every_blob_reflects_and_agrees_with_d3d_disassemble) {
    const std::string exe = c5r::test::exePath();
    ShaderSection sec;
    CHECK_OK(sec.open(widen(exe)));
    size_t agree = 0, d3dZero = 0;
    for (const ShaderBlob& b : sec.blobs()) {
        ShaderReflection r;
        CHECK_OK(reflect(b.bytes, b.size, r));
        std::string text;
        CHECK_OK(d3dDisassemble(b.bytes, b.size, text));
        std::vector<std::string> diffs;
        if (compareWithDisassembly(r, text, diffs) == 0) ++agree;
        D3DReflectInfo d;
        CHECK_OK(d3dReflectInfo(b.bytes, b.size, d));
        if (d.constantBuffers == 0 && d.boundResources == 0) ++d3dZero;
    }
    CHECK_EQ(agree, size_t(1417));
    CHECK_EQ(d3dZero, size_t(1417)); // stripped blobs: D3DReflect sees no constant buffers or bindings
}
