// test_model.cpp - L1 models of the unit module (issue 4352; the EXE's load rule since ticket 0447, unit-renderer.md
// 10.9): the 20-byte and 28-byte repacks on hand-made meshes, the material choice by bone bindings, the weight rule
// (no tie vertices left), every refusal; then, from the install (C5R_GAME_DIR, C5R_EXE), the warrior's
// three meshes against the captured buffers' FNV-1a 64 fingerprints (unit-playback.md 3), the model cache's
// refusals, and the twelve unit programs found in the EXE by digest. VP's models come from the install (VP as DLC) or
// its mod roots (issue 0443, units_mods.h: C5R_UNITS_MODS, else a pool instance's MODS folders).
#include <cmath>
#include <cstring>

#include "c5models/unit_mesh.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/units/draw.h"
#include "units_mods.h"
#include "units_testkit.h"

using namespace c5r::units;

namespace {

void addComp(c5m::Mesh& m, c5m::MemberType t, const char* name, uint32_t count, uint32_t size) {
    c5m::VertexComponent c;
    c.type = t;
    c.name = name;
    c.count = count;
    c.offset = m.format.stride;
    m.format.components.push_back(c);
    m.format.stride += size;
}

// A 3-vertex mesh; skinned adds BoneWeights / BoneIndices with the given weights of vertex 1.
c5m::Mesh makeMesh(bool skinned, const uint8_t w1[4] = nullptr, uint32_t bones = 2) {
    c5m::Mesh m;
    m.name = skinned ? "skin" : "rigid";
    addComp(m, c5m::MemberType::Real32, "Position", 3, 12);
    if (skinned) {
        addComp(m, c5m::MemberType::NormalUInt8, "BoneWeights", 4, 4);
        addComp(m, c5m::MemberType::UInt8, "BoneIndices", 4, 4);
    }
    addComp(m, c5m::MemberType::Real32, "Normal", 3, 12);
    addComp(m, c5m::MemberType::Real32, "TextureCoordinates0", 2, 8);
    m.vertex_count = 3;
    m.vertices.assign(3 * m.format.stride, 0);
    for (uint32_t v = 0; v < 3; ++v) {
        uint8_t* p = m.vertices.data() + v * m.format.stride;
        const float pos[3] = {1.0f + v, -2.0f, 0.5f * v};
        const float nrm[3] = {0, 0, 1};
        const float uv[2] = {0.25f * v, 1.0f};
        size_t o = 0;
        std::memcpy(p, pos, 12);
        o = 12;
        if (skinned) {
            uint8_t w[4] = {255, 0, 0, 0};
            if (v == 1 && w1) std::memcpy(w, w1, 4);
            const uint8_t b[4] = {0, 1, 0, 0};
            std::memcpy(p + o, w, 4);
            std::memcpy(p + o + 4, b, 4);
            o += 8;
        }
        std::memcpy(p + o, nrm, 12);
        std::memcpy(p + o + 12, uv, 8);
    }
    m.indices = {0, 1, 2};
    m.index_size = 2;
    for (uint32_t i = 0; i < bones; ++i) {
        c5m::BoneBinding b;
        b.bone_name = "b" + std::to_string(i);
        m.bone_bindings.push_back(b);
    }
    return m;
}

uint16_t h16(const std::vector<uint8_t>& v, size_t off) { return static_cast<uint16_t>(v[off] | (v[off + 1] << 8)); }

C5R_TEST(static_stream_layout) {
    c5m::Mesh m = makeMesh(false, nullptr, 0);
    c5m::FileInfo f;
    Material mat;
    CHECK_UNIT(meshMaterial(f, m, mat), UnitCode::Ok);
    CHECK(mat == Material::Static);
    // the stream follows the bone bindings (10.9): one binding with weights is the 20-byte stream; two bindings
    // without weights are refused
    c5m::Mesh one = makeMesh(true, nullptr, 1);
    CHECK_UNIT(meshMaterial(f, one, mat), UnitCode::Ok);
    CHECK(mat == Material::Static);
    ModelMesh oneOut;
    CHECK_UNIT(repackStatic(one, 0.5f, false, oneOut), UnitCode::Ok);
    CHECK_EQ(oneOut.stride, kStaticStride);
    CHECK_EQ(oneOut.bones, 1u);
    c5m::Mesh two = makeMesh(false, nullptr, 2);
    CHECK_UNIT(meshMaterial(f, two, mat), UnitCode::UnknownLayout);
    ModelMesh out;
    CHECK_UNIT(repackStatic(m, 0.5f, false, out), UnitCode::Ok);
    CHECK_EQ(out.stride, kStaticStride);
    CHECK_EQ(out.vertices.size(), static_cast<size_t>(60));
    // vertex 2: position (3, -2, 1) x 0.5, w 0; normal (0, 0, 1) / 0.5; uv (0.5, 1) (k = 0.5 exactly at 0.5: the
    // stock basis's k and cofactor inverse are exact here)
    const size_t b = 40;
    CHECK_EQ(h16(out.vertices, b + 0), c5m::half_from_float_granny(1.5f));
    CHECK_EQ(h16(out.vertices, b + 2), c5m::half_from_float_granny(-1.0f));
    CHECK_EQ(h16(out.vertices, b + 4), c5m::half_from_float_granny(0.5f));
    CHECK_EQ(h16(out.vertices, b + 6), static_cast<uint16_t>(0));
    CHECK_EQ(h16(out.vertices, b + 12), c5m::half_from_float_granny(2.0f));
    CHECK_EQ(h16(out.vertices, b + 14), static_cast<uint16_t>(0));
    CHECK_EQ(h16(out.vertices, b + 16), c5m::half_from_float_granny(0.5f));
    CHECK_EQ(h16(out.vertices, b + 18), c5m::half_from_float_granny(1.0f));
    CHECK_EQ(out.indices.size(), static_cast<size_t>(3));
    // y up (VP's FBX ArtToolInfo) -> z up: (x, y, z) -> (x, -z, y)
    ModelMesh up;
    CHECK_UNIT(repackStatic(m, 1.0f, true, up), UnitCode::Ok);
    CHECK_EQ(h16(up.vertices, 40 + 2), c5m::half_from_float_granny(-1.0f));
    CHECK_EQ(h16(up.vertices, 40 + 4), c5m::half_from_float_granny(-2.0f));
}

C5R_TEST(static_refusals) {
    ModelMesh out;
    c5m::Mesh m = makeMesh(false, nullptr, 0);
    CHECK_UNIT(repackStatic(m, 0.0f, false, out), UnitCode::BadModel);
    CHECK_UNIT(repackStatic(m, NAN, false, out), UnitCode::BadModel);
    c5m::Mesh bad = m;
    bad.indices = {0, 1, 7};
    CHECK_UNIT(repackStatic(bad, 1.0f, false, out), UnitCode::BadModel);
    bad = m;
    bad.vertices.pop_back();
    CHECK_UNIT(repackStatic(bad, 1.0f, false, out), UnitCode::BadModel);
    bad = m;
    bad.format.components.pop_back();   // no TextureCoordinates0
    c5m::FileInfo f;
    Material mat;
    CHECK_UNIT(meshMaterial(f, bad, mat), UnitCode::UnknownLayout);
    CHECK_UNIT(repackStatic(bad, 1.0f, false, out), UnitCode::UnknownLayout);
    c5m::Mesh skin = makeMesh(true);
    skin.format.components[2].type = c5m::MemberType::UInt16;   // BoneIndices of another type
    CHECK_UNIT(meshMaterial(f, skin, mat), UnitCode::UnknownLayout);
}

C5R_TEST(skinned_material_and_weights) {
    const uint8_t tie[4] = {119, 62, 57, 17};   // w0 = 127.5 exactly: warrior_v2 vertex 428 (unit-playback.md 3)
    c5m::Mesh m = makeMesh(true, tie);
    c5m::FileInfo f;
    Material mat;
    CHECK_UNIT(meshMaterial(f, m, mat), UnitCode::Ok);
    CHECK(mat == Material::Skinned);
    c5m::Material alpha;
    alpha.name = "sail";
    alpha.shader_set = "UnitShader_SkinnedAlpha";
    f.materials.push_back(alpha);
    m.materials = {0};
    CHECK_UNIT(meshMaterial(f, m, mat), UnitCode::Ok);
    CHECK(mat == Material::SkinnedAlpha);
    ModelMesh out;
    CHECK_UNIT(repackSkinned(m, 0.14f, false, out), UnitCode::Ok);
    CHECK_EQ(out.stride, kSkinnedStride);
    // the EXE's float rule (10.9): the exact half lands below, as captured (127, 66, 62, 0); no tie vertices are named
    CHECK(out.tie_vertices.empty());
    CHECK(out.vertices[28 + 8] == 127 && out.vertices[28 + 9] == 66 && out.vertices[28 + 10] == 62 &&
          out.vertices[28 + 11] == 0);
    const uint8_t plain[4] = {122, 80, 53, 0};   // fourth 0, sum 255: a fixed point of the rule
    c5m::Mesh n = makeMesh(true, plain);
    CHECK_UNIT(repackSkinned(n, 0.14f, false, out), UnitCode::Ok);
    CHECK(out.tie_vertices.empty());
    CHECK(out.vertices[28 + 8] == 122 && out.vertices[28 + 9] == 80 && out.vertices[28 + 10] == 53);
}

C5R_TEST(skinned_refusals) {
    ModelMesh out;
    c5m::Mesh m = makeMesh(true, nullptr, 33);
    CHECK_UNIT(repackSkinned(m, 0.1f, false, out), UnitCode::TooManyBones);
    m = makeMesh(true);
    CHECK_UNIT(repackSkinned(m, -1.0f, false, out), UnitCode::BadModel);
    m.indices = {0, 1, 3};
    CHECK_UNIT(repackSkinned(m, 1.0f, false, out), UnitCode::BadModel);
    // only a fourth weight: kept as it is (the EXE's rule leaves s = 0 alone), its bone index checked
    const uint8_t onlyFourth[4] = {0, 0, 0, 255};
    m = makeMesh(true, onlyFourth);
    CHECK_UNIT(repackSkinned(m, 1.0f, false, out), UnitCode::Ok);
    CHECK(out.vertices[28 + 8] == 0 && out.vertices[28 + 11] == 255);
    m.vertices[m.format.stride + 16 + 3] = 9;   // vertex 1's fourth bone index past the 2 bindings
    CHECK_UNIT(repackSkinned(m, 1.0f, false, out), UnitCode::BadModel);
    // no BoneWeights: not the 28-byte stream's source
    m = makeMesh(false, nullptr, 2);
    CHECK_UNIT(repackSkinned(m, 1.0f, false, out), UnitCode::UnknownLayout);
}

C5R_TEST(cache_refusals) {
    units_test::MemFiles files;
    files.files["garbage.gr2"] = "this is not a granny file";
    ModelCache cache(files);
    MemberArt m;
    m.member = "M";
    m.scale = 0.1f;
    m.files.mesh.name = "Absent.gr2";
    const UnitModel* out = nullptr;
    CHECK_UNIT(cache.get(m, out), UnitCode::ModelNotFound);
    CHECK(out == nullptr);
    CHECK_UNIT(cache.get(m, out), UnitCode::ModelNotFound);   // the failure is remembered
    m.files.mesh.name = "Garbage.gr2";
    CHECK_UNIT(cache.get(m, out), UnitCode::BadModel);
    CHECK_EQ(cache.models(), static_cast<size_t>(0));
}

// ---------------------------------------------------------------------------------------------- install
struct Install {
    c5r::gamefs::GameFiles gf;
    std::unique_ptr<GameFilesProvider> files;
    c5m::StringDatabase strings;
    Install() {
        const std::string dir = c5r::test::gameDir();
        c5r::gamefs::GameFiles::Options o;
        o.extraLooseRoots = units_test::installModRoots(dir);   // issue 0443: VP as mods on a pool instance
        CHECK_OK(gf.open(c5r::gamefs::fromUtf8(dir), o));
        files.reset(new GameFilesProvider(gf));
        CHECK_UNIT(loadStringDatabases(gf.names(), *files, strings), UnitCode::Ok);
    }
};

// Hides some names of the install (negative tests of the cache against real models).
class Hiding : public c5m::FileProvider {
public:
    Hiding(c5m::FileProvider& in, std::vector<std::string> hide) : in_(in), hide_(std::move(hide)) {}
    bool read(const std::string& n, c5m::Bytes& out, std::string* o) override {
        for (auto& h : hide_)
            if (n == h) return false;
        return in_.read(n, out, o);
    }
private:
    c5m::FileProvider& in_;
    std::vector<std::string> hide_;
};

// Returns bytes that are not a DDS for one name (negative test: a texture present but unreadable).
class Corrupting : public c5m::FileProvider {
public:
    Corrupting(c5m::FileProvider& in, std::string name) : in_(in), name_(std::move(name)) {}
    bool read(const std::string& n, c5m::Bytes& out, std::string* o) override {
        if (n == name_) {
            out.assign(16, 0x5a);
            if (o) *o = "corrupt:" + n;
            return true;
        }
        return in_.read(n, out, o);
    }
private:
    c5m::FileProvider& in_;
    std::string name_;
};

MemberArt member(const char* gr2, float scale) {
    MemberArt m;
    m.member = gr2;
    m.scale = scale;
    m.files.mesh.name = gr2;
    m.files.mesh.found = true;
    return m;
}

C5R_TEST(warrior_from_install) {
    Install in;
    ModelCache cache(*in.files);
    cache.setStrings(&in.strings);
    struct W { const char* gr2; uint64_t vb; };
    // unit-playback.md 3: the captured fingerprints of all three; warrior_v2's two former tie vertices 428 and 442
    // equal the capture under the EXE's float weight rule (ticket 0447, unit-renderer.md 10.9; 4152's integer rule gave
    // 0xded26cc25d5c8467)
    const W ws[] = {{"warrior.gr2", 0x185b675f25084168ull}, {"warrior_v4.gr2", 0xc368ed5b584dc4cdull},
                    {"warrior_v2.gr2", 0xa06d5cd8b80e2ab7ull}};
    for (auto& w : ws) {
        const UnitModel* m = nullptr;
        CHECK_UNIT(cache.get(member(w.gr2, 0.14f), m), UnitCode::Ok);
        if (!m) continue;
        CHECK_EQ(m->meshes.size(), static_cast<size_t>(1));
        const ModelMesh& mm = m->meshes[0];
        CHECK(mm.material == Material::Skinned);
        CHECK_EQ(c5m::fnv1a64(mm.vertices.data(), mm.vertices.size()), w.vb);
        std::printf("%s: vb 0x%016llx  t0 %s t1 %s\n", w.gr2,
                    static_cast<unsigned long long>(c5m::fnv1a64(mm.vertices.data(), mm.vertices.size())),
                    mm.base_texture.c_str(), mm.sref_texture.c_str());
        CHECK(mm.tie_vertices.empty());
        CHECK(mm.t0 != nullptr);
        CHECK(!mm.base_texture.empty());
    }
    CHECK_EQ(cache.models(), static_cast<size_t>(3));
}

// 4355: a model's meshes come in its first Model's MeshBindings order, the order the EXE numbers a figure's blocks
// in. chariotarcher.gr2 lists Chariot x3, Driver, Archer in Meshes but binds Driver, Archer, Chariot x3; the
// captured L02 blocks follow the bindings (unit-renderer.md 13). The warrior (one mesh) is unchanged above.
C5R_TEST(mesh_order_follows_mesh_bindings) {
    Install in;
    ModelCache cache(*in.files);
    cache.setStrings(&in.strings);
    const UnitModel* m = nullptr;
    CHECK_UNIT(cache.get(member("chariotarcher.gr2", 0.12f), m), UnitCode::Ok);
    if (!m) return;
    std::vector<std::string> names;
    std::vector<uint32_t> idx;
    for (const ModelMesh& mm : m->meshes) {
        names.push_back(mm.name);
        idx.push_back(mm.index);
    }
    CHECK_EQ(names.size(), static_cast<size_t>(5));
    if (names.size() != 5) return;
    CHECK_EQ(names[0], std::string("ChariotArcher_Driver"));
    CHECK_EQ(names[1], std::string("ChariotArcher_Archer"));
    CHECK_EQ(names[2], std::string("ChariotArcher_Chariot"));
    CHECK(idx == std::vector<uint32_t>({3, 4, 0, 1, 2}));   // the .gr2 index is kept
}

}  // namespace
namespace c5r { namespace units {
bool materialTextureParams(const uint8_t* data, size_t size, const c5m::StringDatabase* strings,
                           std::vector<std::vector<std::pair<std::string, std::string>>>& out, std::string* why);
}}  // namespace c5r::units
namespace {

// 4356: VP's Black Tug model (FBX export, no 3ds Max Maps) names its textures only in the material's FireGrafix
// UnitShaderTextures parameters; the EXE binds those (unit-renderer.md 10.8).
C5R_TEST(material_parameter_textures) {
    Install in;
    c5m::Bytes b;
    std::string origin;
    if (!in.files->read("knight_turkey.gr2", b, &origin)) c5r::test::skip("knight_turkey.gr2 not in this install nor its mod roots");
    std::vector<std::vector<std::pair<std::string, std::string>>> params;
    std::string why;
    CHECK(materialTextureParams(b.data(), b.size(), &in.strings, params, &why));
    std::printf("knight_turkey.gr2 (%s): %zu materials%s%s\n", origin.c_str(), params.size(), why.empty() ? "" : ": ",
                why.c_str());
    bool base = false, gloss = false;
    for (auto& m : params)
        for (auto& p : m) {
            std::printf("  %s = %s\n", p.first.c_str(), p.second.c_str());
            base = base || (p.first == "BaseTextureMap" && c5m::ascii_lower(p.second) == "knight_turkey.dds");
            gloss = gloss || (p.first == "SREFMap" && c5m::ascii_lower(p.second) == "knight_turkey_gloss.dds");
        }
    CHECK(base && gloss);
    ModelCache cache(*in.files);
    cache.setStrings(&in.strings);
    const UnitModel* m = nullptr;
    CHECK_UNIT(cache.get(member("knight_turkey.gr2", 0.12f), m), UnitCode::Ok);
    // the rider mesh "Material" (.gr2 index 0; since 4355 the meshes follow the MeshBindings order, horse first)
    if (m) {
        const ModelMesh* rider = nullptr;
        for (const ModelMesh& mm : m->meshes)
            if (mm.name == "Material") rider = &mm;
        CHECK(rider != nullptr);
        if (rider) CHECK_EQ(rider->base_texture, std::string("knight_turkey.dds"));
        CHECK(!m->meshes.empty() && m->meshes[0].name == "Horse_Material");   // MeshBindings: Horse_Material, Material
    }
    // a file that is not a granny file: false with a reason, nothing out
    const uint8_t junk[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(!materialTextureParams(junk, sizeof junk, nullptr, params, &why));
    CHECK(params.empty());
    CHECK(!why.empty());
}

C5R_TEST(cache_texture_not_found) {
    Install in;
    // find the warrior's base texture name, then hide it (and its .dds stem)
    ModelCache probe(*in.files);
    probe.setStrings(&in.strings);
    const UnitModel* m = nullptr;
    CHECK_UNIT(probe.get(member("warrior.gr2", 0.14f), m), UnitCode::Ok);
    if (!m) return;
    std::string tex = m->meshes[0].base_texture;
    size_t dot = tex.rfind('.');
    // in no file: the EXE's 8 x 8 fallback is bound and named (4356, unit-renderer.md 10.8)
    Hiding hide(*in.files, {tex, (dot == std::string::npos ? tex : tex.substr(0, dot)) + ".dds"});
    ModelCache cache(hide);
    cache.setStrings(&in.strings);
    CHECK_UNIT(cache.get(member("warrior.gr2", 0.14f), m), UnitCode::Ok);
    if (m) {
        CHECK_EQ(m->fallbacks.size(), static_cast<size_t>(1));
        CHECK(m->meshes[0].t0 != nullptr);
        if (m->meshes[0].t0) {
            const UnitTexture& f = *m->meshes[0].t0;
            CHECK_EQ(f.name, std::string(kFallbackTexture));
            CHECK_EQ(f.width, 8u);
            CHECK_EQ(f.payload.size(), static_cast<size_t>(256));
            CHECK_EQ(f.payload[0], static_cast<uint8_t>(255));
            CHECK_EQ(f.payload[1], static_cast<uint8_t>(0));
            CHECK_EQ(f.payload[3], static_cast<uint8_t>(255));
            size_t nonzero = 0;
            for (uint8_t b : f.payload) nonzero += b != 0;
            CHECK_EQ(nonzero, static_cast<size_t>(2));
        }
        // the fallback uploads like any texture (WARP)
        ID3D11Device* dev = nullptr;
        if (SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev,
                                        nullptr, nullptr))) {
            CHECK_OK(cache.upload(dev));
            CHECK(m->meshes[0].t0 && m->meshes[0].t0->srv != nullptr);
            cache.clear();
            dev->Release();
        }
    }
    // present but not a DDS: TextureNotFound (no fallback)
    Corrupting bad(*in.files, tex);
    ModelCache cache2(bad);
    cache2.setStrings(&in.strings);
    CHECK_UNIT(cache2.get(member("warrior.gr2", 0.14f), m), UnitCode::TextureNotFound);
}

C5R_TEST(programs_from_exe) {
    UnitPrograms p;
    const std::string exe = c5r::test::exePath();
    CHECK_OK(p.open(c5r::gamefs::fromUtf8(exe)));
    for (auto& r : unitProgramTable()) {
        const std::vector<uint8_t>* b = p.bytes(r.material, r.pass, r.vertex);
        CHECK(b != nullptr);
        if (b) CHECK_EQ(sha256Hex(b->data(), b->size()), std::string(r.sha256));
    }
    CHECK_EQ(p.sha256(Material::Skinned, PassKind::Reflection, true), p.sha256(Material::Skinned, PassKind::Scene, true));
}

C5R_TEST(programs_not_found) {
    UnitPrograms p;
    std::vector<std::vector<uint8_t>> blobs = {{'D', 'X', 'B', 'C', 0, 1, 2}};
    CHECK_ERR(p.openFromBlobs(blobs), c5r::Err::NotFound);
    CHECK(p.bytes(Material::Skinned, PassKind::Scene, true) == nullptr);
    CHECK_EQ(sha256Hex(reinterpret_cast<const uint8_t*>("abc"), 3),
             std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

}  // namespace

C5R_TEST_MAIN
