// Terrain-side shader sets against the real EXE (task 2903): C5R_EXE = CivilizationV_DX11.exe (read-only, build 4390913);
// SKIP without it. Every set validates at every shader level, and the create-site handles agree with the material table.
#include <cstdio>
#include <string>

#include "c5r/shaders/shadersection.h"
#include "c5r/terrainsets/terrainsets.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::shaders;
using namespace c5r::terrainsets;

namespace {
std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }
} // namespace

C5R_TEST(game_terrainsets_validate_against_exe) {
    const std::string exe = c5r::test::exePath();
    ShaderSection sec;
    CHECK_OK(sec.open(widen(exe)));
    CHECK(sec.createSitesApplied()); // so validateSet also compares every blob's handle with the material table
    size_t blobs = 0;
    for (const ShaderSetDesc& s : allSets()) {
        Status st = validateSet(s, sec);
        if (!st.ok()) std::fprintf(stderr, "%s: %s\n", s.name, st.str().c_str());
        CHECK_OK(st);
        for (const PassDesc& p : s.passes)
            for (uint32_t va : {p.sm50[0], p.sm50[1], p.sm50[2], p.sm50[3], p.sm41[0], p.sm41[1], p.sm40[0], p.sm40[1]})
                if (va) {
                    ++blobs;
                    const ShaderBlob* b = sec.findByVa(va);
                    CHECK(b != nullptr);
                    if (b) CHECK(b->set == 50 || b->set == 41 || b->set == 40);
                }
    }
    std::printf("terrain-side sets: %zu, blob references validated: %zu\n", allSets().size(), blobs);
}

C5R_TEST(game_terrainsets_terrain_tessellation_set) {
    const std::string exe = c5r::test::exePath();
    ShaderSection sec;
    CHECK_OK(sec.open(widen(exe)));
    const ShaderSetDesc* t = findSet("TerrainShader");
    CHECK(t != nullptr);
    if (!t) return;
    CHECK_EQ(t->material, 73u);
    const PassDesc& p = t->passes[1]; // the normal tessellated pass
    const ShaderBlob* vs = sec.findByVa(p.sm50[0]);
    const ShaderBlob* hs = sec.findByVa(p.sm50[2]);
    const ShaderBlob* ds = sec.findByVa(p.sm50[3]);
    CHECK(vs && hs && ds);
    if (vs) { CHECK(vs->stage == Stage::Vertex); CHECK_EQ(vs->handleGlobal, 0x2de1c8cu); CHECK_EQ(vs->set, 50); }
    if (hs) CHECK(hs->stage == Stage::Hull);
    if (ds) CHECK(ds->stage == Stage::Domain);
    // the CacheBake PS pair GATE2 took for water
    const ShaderSetDesc* cb = findSet("CacheBake");
    CHECK(cb && cb->passes[0].sm50[1] == 0x067ebe78u && cb->passes[1].sm50[1] == 0x067ed018u);
}

// Issue 3602: the water sets traced against their blobs, and one negative case per check on each traced layout (the
// blobs are the EXE's; the descriptions are mutated copies).
C5R_TEST(game_terrainsets_water_sets) {
    const std::string exe = c5r::test::exePath();
    ShaderSection sec;
    CHECK_OK(sec.open(widen(exe)));
    const ShaderSetDesc* water = findSet("WaterShader");
    const ShaderSetDesc* wave = findSet("WaveShader");
    const ShaderSetDesc* cur = findSet("WorldCurrents");
    CHECK(water && wave && cur);
    if (!water || !wave || !cur) return;
    for (const ShaderSetDesc* s : {water, wave, cur}) {
        CHECK_OK(validateSet(*s, sec));
        CHECK(s->identification == Conf::Confirmed);
        for (const CbBinding& b : s->cbuffers)   // every member of the layouts 3602 traced (WaterFrame is 3604's)
            if (std::string(b.layout->name) != "WaterFrame")
                for (const CbMember& m : b.layout->members) CHECK(m.conf == Conf::Confirmed);
    }
    // sizes (question 10): WaveShader own 0xf80 + shared 27 (0x80); WorldCurrents shared 43 (0x40), no own buffer
    auto layout = [](const ShaderSetDesc* s, uint32_t slot) -> const CbLayout* {
        for (const CbBinding& b : s->cbuffers)
            if (b.slot == slot) return b.layout;
        return nullptr;
    };
    CHECK(layout(wave, 0) && layout(wave, 0)->sizeBytes == 0xf80u && layout(wave, 0)->sharedId == -1);
    CHECK(layout(wave, 1) && layout(wave, 1)->sizeBytes == 0x80u && layout(wave, 1)->sharedId == 27);
    CHECK(layout(cur, 0) == nullptr);
    CHECK(layout(cur, 1) && layout(cur, 1)->sizeBytes == 0x40u && layout(cur, 1)->sharedId == 43);
    CHECK(layout(water, 0) && layout(water, 0)->sizeBytes == 0x80u);

    // a buffer smaller than the blob declares: WaveFrame at 0x60 (the VS declares cb1[7])
    CbLayout small = *layout(wave, 1);
    small.sizeBytes = 0x60;
    small.members.resize(small.members.size() - 2);   // drop AtlasScale and Tint (they lie past 0x60)
    ShaderSetDesc w2 = *wave;
    w2.cbuffers = {{0, layout(wave, 0)}, {1, &small}};
    CHECK(validateSet(w2, sec).code == Err::BadLayout);
    // a read component no member covers: CurrentsFrame without GradientScale (pass 3 reads cb1[3].x)
    CbLayout noGrad = *layout(cur, 1);
    noGrad.members.pop_back();
    ShaderSetDesc c2 = *cur;
    c2.cbuffers = {{1, &noGrad}};
    CHECK(validateSet(c2, sec).code == Err::BadLayout);
    // the river World matrix missing (the river VS reads cb0[0..3].xyz)
    CbLayout noWorld = *layout(water, 0);
    noWorld.members.clear();
    ShaderSetDesc r2 = *water;
    r2.cbuffers = {{0, &noWorld}, {1, layout(water, 1)}};
    CHECK(validateSet(r2, sec).code == Err::BadLayout);
    // the dynamically indexed wave array replaced by a single register
    CbLayout noArray = *layout(wave, 0);
    noArray.members = {{"Wave0", 0, MemberType::Float4, 1, "", Conf::Inferred}};
    ShaderSetDesc w3 = *wave;
    w3.cbuffers = {{0, &noArray}, {1, layout(wave, 1)}};
    CHECK(validateSet(w3, sec).code == Err::BadLayout);
    // a slot without a layout: WorldCurrents without slot 1
    ShaderSetDesc c3 = *cur;
    c3.cbuffers.clear();
    CHECK(validateSet(c3, sec).code == Err::BadLayout);
    // a texture of the wrong dimension, and a texture slot missing
    ShaderSetDesc w4 = *wave;
    w4.psTextures = {{0, TexDim::Tex3D, "g_WaveTexture", "", 3, Conf::Inferred}};
    CHECK(validateSet(w4, sec).code == Err::BadLayout);
    ShaderSetDesc c4 = *cur;
    c4.psTextures.clear();
    CHECK(validateSet(c4, sec).code == Err::BadLayout);
    // a blob address not in the section, and a VS named as the PS
    ShaderSetDesc c5 = *cur;
    c5.passes[3].sm50[1] = 0x0682d731;
    CHECK(validateSet(c5, sec).code == Err::NotFound);
    ShaderSetDesc c6 = *cur;
    c6.passes[3].sm50[1] = c6.passes[3].sm50[0];
    CHECK(validateSet(c6, sec).code == Err::BadLayout);
    // a wrong handle global
    ShaderSetDesc c7 = *cur;
    c7.passes[3].handle[1] = 0x2de1dec;
    CHECK(validateSet(c7, sec).code == Err::BadLayout);
    // overlapping members (validateLayout)
    CbLayout ov = *layout(wave, 1);
    ov.members.push_back({"Overlap", 0x44, MemberType::Float, 1, "", Conf::Inferred});
    ShaderSetDesc w5 = *wave;
    w5.cbuffers = {{0, layout(wave, 0)}, {1, &ov}};
    CHECK(validateSet(w5, sec).code == Err::BadLayout);
}
