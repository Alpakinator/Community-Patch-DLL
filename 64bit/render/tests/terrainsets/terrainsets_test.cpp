// Terrain-side shader set tests (task 2903): every check of validateSet / validateBlob / validateLayout against
// synthetic shaders compiled here (no game bytes), and the built-in descriptions' own consistency.
#include <cstring>
#include <set>
#include <string>

#include "c5r/shaders/shadersection.h"
#include "c5r/terrainsets/terrainsets.h"
#include "testkit.h"
#include "testutil.h"

using namespace c5r;
using namespace c5r::shaders;
using namespace c5r::terrainsets;
using namespace c5r::test;

namespace {

// VS: cb0 = {float4x4 M; float4 P} (5 registers, all read), cb1 = float4 Arr[8] indexed dynamically, t0 2D + s0.
const char* kVs =
    "cbuffer A : register(b0) { float4x4 M; float4 P; }\n"
    "cbuffer B : register(b1) { float4 Arr[8]; }\n"
    "Texture2D T0 : register(t0); SamplerState S0 : register(s0);\n"
    "float4 main(float3 p : POSITION, uint i : SV_VertexID) : SV_Position {\n"
    "  float4 x = mul(float4(p, 1), M) + P;\n"
    "  x += Arr[i % 8];\n"
    "  x += T0.SampleLevel(S0, p.xy, 0);\n"
    "  return x; }\n";
// PS: reads cb0[4] (P) only, samples a 3D texture at t1 with s1.
const char* kPs =
    "cbuffer A : register(b0) { float4x4 M; float4 P; }\n"
    "Texture3D V : register(t1); SamplerState S1 : register(s1);\n"
    "float4 main(float4 p : SV_Position) : SV_Target { return P * V.Sample(S1, p.xyz); }\n";
// PS2: samples t1 with sampler s2 (a sampler slot without a texture of its own index).
const char* kPs2 =
    "Texture3D V : register(t1); SamplerState S2 : register(s2);\n"
    "float4 main(float4 p : SV_Position) : SV_Target { return V.Sample(S2, p.xyz); }\n";

struct Fixture {
    ShaderSection sec;
    uint32_t vs = 0, ps = 0, ps2 = 0;
    Fixture() {
        const std::vector<uint8_t> a = stripLikeExe(compileHlsl(kVs, "main", "vs_5_0"));
        const std::vector<uint8_t> b = stripLikeExe(compileHlsl(kPs, "main", "ps_5_0"));
        const std::vector<uint8_t> c = stripLikeExe(compileHlsl(kPs2, "main", "ps_5_0"));
        std::vector<uint8_t> s;
        for (const std::vector<uint8_t>* x : {&a, &b, &c}) {
            s.insert(s.end(), x->begin(), x->end());
            while (s.size() % 8) s.push_back(0);
        }
        Status st = sec.parse(buildPe({{".text", 0x1000, 0x10, std::vector<uint8_t>(0x10, 0xC3)},
                                       {".shader", 0x2000, static_cast<uint32_t>(s.size()), s}}));
        CHECK_OK(st);
        if (sec.blobs().size() == 3) {
            vs = sec.blobs()[0].va;
            ps = sec.blobs()[1].va;
            ps2 = sec.blobs()[2].va;
        }
    }
};

const CbLayout kA = {"A", -1, 0x50, "", {{"M", 0x00, MemberType::Float4x4, 1, "", Conf::Inferred},
                                         {"P", 0x40, MemberType::Float4, 1, "", Conf::Inferred}}};
const CbLayout kB = {"B", 3, 0x80, "", {{"Arr", 0x00, MemberType::Float4, 8, "", Conf::Inferred}}};

ShaderSetDesc makeSet(const Fixture& f) {
    ShaderSetDesc s;
    s.name = "Synthetic";
    s.material = 0;
    s.role = "test";
    s.drawnBy = "";
    PassDesc p = {0, {0x100, 0x104, 0, 0}, {f.vs, f.ps, 0, 0}, {0, 0}, {0, 0}, ""};
    s.passes = {p};
    s.cbuffers = {{0, &kA}, {1, &kB}};
    s.psTextures = {{1, TexDim::Tex3D, "V", "", -1, Conf::Inferred}};
    s.vsTextures = {{0, TexDim::Tex2D, "T0", "", -1, Conf::Inferred}};
    s.vertexInput = "";
    s.notes = "";
    s.identification = Conf::Inferred;
    return s;
}

} // namespace

C5R_TEST(terrainsets_synthetic_set_validates) {
    Fixture f;
    CHECK(f.vs && f.ps && f.ps2);
    CHECK_OK(validateSet(makeSet(f), f.sec));
}

C5R_TEST(terrainsets_layout_smaller_than_declared_buffer) {
    Fixture f;
    ShaderSetDesc s = makeSet(f);
    CbLayout small = kA;
    small.sizeBytes = 0x40; // the shaders declare cb0[5] = 80 bytes
    small.members = {{"M", 0x00, MemberType::Float4x4, 1, "", Conf::Inferred}};
    s.cbuffers = {{0, &small}, {1, &kB}};
    CHECK_ERR(validateSet(s, f.sec), Err::BadLayout);
}

C5R_TEST(terrainsets_read_register_left_uncovered) {
    Fixture f;
    ShaderSetDesc s = makeSet(f);
    CbLayout noP = kA;
    noP.members = {{"M", 0x00, MemberType::Float4x4, 1, "", Conf::Inferred}}; // cb0[4] (P) is read, nothing covers it
    s.cbuffers = {{0, &noP}, {1, &kB}};
    CHECK_ERR(validateSet(s, f.sec), Err::BadLayout);
    // a member that covers only .xy of a register read as .xyzw
    CbLayout half = kA;
    half.members = {{"M", 0x00, MemberType::Float4x4, 1, "", Conf::Inferred},
                    {"P", 0x40, MemberType::Float2, 1, "", Conf::Inferred}};
    s.cbuffers = {{0, &half}, {1, &kB}};
    CHECK_ERR(validateSet(s, f.sec), Err::BadLayout);
}

C5R_TEST(terrainsets_dynamic_read_needs_an_array) {
    Fixture f;
    ShaderSetDesc s = makeSet(f);
    CbLayout flat = kB;
    flat.members.clear();
    for (uint32_t i = 0; i < 8; ++i)
        flat.members.push_back({"Arr", i * 16, MemberType::Float4, 1, "", Conf::Inferred});
    s.cbuffers = {{0, &kA}, {1, &flat}};
    CHECK_ERR(validateSet(s, f.sec), Err::BadLayout);
}

C5R_TEST(terrainsets_declared_buffer_without_layout) {
    Fixture f;
    ShaderSetDesc s = makeSet(f);
    s.cbuffers = {{0, &kA}}; // the VS declares cb1
    CHECK_ERR(validateSet(s, f.sec), Err::BadLayout);
}

C5R_TEST(terrainsets_texture_dimension_and_presence) {
    Fixture f;
    ShaderSetDesc s = makeSet(f);
    s.psTextures = {{1, TexDim::Tex2D, "V", "", -1, Conf::Inferred}}; // the PS declares a 3D texture
    CHECK_ERR(validateSet(s, f.sec), Err::BadLayout);
    s = makeSet(f);
    s.psTextures.clear(); // t1 not described
    CHECK_ERR(validateSet(s, f.sec), Err::BadLayout);
    s = makeSet(f);
    s.vsTextures = {{0, TexDim::Cube, "T0", "", -1, Conf::Inferred}};
    CHECK_ERR(validateSet(s, f.sec), Err::BadLayout);
}

C5R_TEST(terrainsets_sampler_without_texture_slot) {
    Fixture f;
    ShaderSetDesc s = makeSet(f);
    s.passes[0].sm50[1] = f.ps2; // samples t1 with s2: no texture slot 2
    CHECK_ERR(validateSet(s, f.sec), Err::BadLayout);
    s.psTextures.push_back({2, TexDim::Tex2D, "unused", "", -1, Conf::Inferred});
    CHECK_OK(validateSet(s, f.sec));
}

C5R_TEST(terrainsets_blob_of_the_wrong_stage) {
    Fixture f;
    ShaderSetDesc s = makeSet(f);
    s.passes[0].sm50[0] = f.ps; // a pixel shader named as the vertex shader
    CHECK_ERR(validateSet(s, f.sec), Err::BadLayout);
    s = makeSet(f);
    s.passes[0].sm41[1] = f.vs; // a vertex shader named as the 4_1 pixel shader
    CHECK_ERR(validateSet(s, f.sec), Err::BadLayout);
}

C5R_TEST(terrainsets_blob_not_in_section) {
    Fixture f;
    ShaderSetDesc s = makeSet(f);
    s.passes[0].sm40[0] = f.vs + 4;
    CHECK_ERR(validateSet(s, f.sec), Err::NotFound);
}

C5R_TEST(terrainsets_handle_mismatch) {
    Fixture f;
    ShaderSetDesc s = makeSet(f);
    ShaderBlob b = *f.sec.findByVa(f.vs);
    b.handleGlobal = 0x100;
    CHECK_OK(validateBlob(s, b, Stage::Vertex, 0x100, true));
    CHECK_ERR(validateBlob(s, b, Stage::Vertex, 0x104, true), Err::BadLayout);
    CHECK_OK(validateBlob(s, b, Stage::Vertex, 0x104, false)); // not checked when the create sites are not applied
}

C5R_TEST(terrainsets_layout_checks) {
    CHECK_OK(validateLayout(kA));
    CbLayout l = kA;
    l.members = {{"M", 0x00, MemberType::Float4x4, 1, "", Conf::Inferred}, {"P", 0x30, MemberType::Float4, 1, "", Conf::Inferred}};
    CHECK_ERR(validateLayout(l), Err::BadLayout); // overlap
    l.members = {{"P", 0x50, MemberType::Float4, 1, "", Conf::Inferred}};
    CHECK_ERR(validateLayout(l), Err::BadLayout); // past the end
    l.members = {{"Q", 0x08, MemberType::Float3, 1, "", Conf::Inferred}};
    CHECK_ERR(validateLayout(l), Err::BadLayout); // straddles registers 0 and 1
    l.members = {{"Q", 0x08, MemberType::Float4x4, 1, "", Conf::Inferred}};
    CHECK_ERR(validateLayout(l), Err::BadLayout); // a matrix not on a register boundary
    l.members = {{"Q", 0x00, MemberType::Float4, 0, "", Conf::Inferred}};
    CHECK_ERR(validateLayout(l), Err::BadLayout); // count 0
    l.members = {{"Q", 0x00, MemberType::Float2, 2, "", Conf::Inferred}};
    CHECK_ERR(validateLayout(l), Err::BadLayout); // arrays of short types are not described
    l.members = {{"Q", 0x08, MemberType::Float2, 1, "", Conf::Inferred}};
    CHECK_OK(validateLayout(l));                  // .zw of register 0 is fine
    l.sizeBytes = 0x48;
    CHECK_ERR(validateLayout(l), Err::BadLayout); // size not a multiple of 16
    l.sizeBytes = 0;
    CHECK_ERR(validateLayout(l), Err::BadLayout);
}

C5R_TEST(terrainsets_builtin_descriptions_are_consistent) {
    const std::vector<ShaderSetDesc>& sets = allSets();
    CHECK(sets.size() >= 10);
    std::set<std::string> names;
    std::set<uint32_t> materials;
    for (const ShaderSetDesc& s : sets) {
        CHECK(names.insert(s.name).second);
        CHECK(materials.insert(s.material).second);
        CHECK(findSet(s.name) == &s);
        CHECK(!s.passes.empty());
        for (const CbBinding& b : s.cbuffers) {
            CHECK_OK(validateLayout(*b.layout));
            CHECK((b.slot == 0) == (b.layout->sharedId < 0)); // slot 0 is the program's own buffer
        }
        for (const std::vector<TexSlot>* v : {&s.psTextures, &s.vsTextures}) {
            std::set<uint32_t> slots;
            for (const TexSlot& t : *v) {
                CHECK(slots.insert(t.slot).second);
                CHECK(t.samplerState < 0 || samplerState(static_cast<uint32_t>(t.samplerState)) != nullptr);
            }
        }
        for (const PassDesc& p : s.passes) {
            CHECK(p.sm50[0] && p.sm50[1] && p.handle[0] && p.handle[1]);
            CHECK((p.sm50[2] == 0) == (p.handle[2] == 0));
            CHECK((p.sm50[3] == 0) == (p.handle[3] == 0));
        }
    }
    CHECK(findSet("TerrainShader") != nullptr);
    CHECK(findSet("NoSuchSet") == nullptr);
    CHECK(findSet(nullptr) == nullptr);
    CHECK_EQ(samplerStates().size(), size_t(13));
    CHECK(samplerState(13) == nullptr);
    CHECK_EQ(samplerState(5)->filter, 0x15u);
    CHECK_EQ(samplerState(9)->address[0], 1u);
}
