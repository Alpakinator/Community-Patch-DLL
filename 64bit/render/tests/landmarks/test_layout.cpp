// test_layout.cpp - c5r::landmarks layers 1 and 2 on synthetic model files (ticket 4872; no game files): the key
// (FNV-1a 64, the pad element left out, the vertex buffer in it), the mesh order, the layout of one file (the pad, the
// two vertex buffers, the parts' ranges, textures), the three vertex forms' bytes, and a negative test for every check.
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "c5models/unit_mesh.h"
#include "c5r/landmarks/library.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::landmarks;

namespace {

c5m::Material material(const char* name, const char* set, const char* diffuse, const char* sref) {
    c5m::Material m;
    m.name = name;
    m.shader_set = set;
    if (diffuse) {
        c5m::TextureMap t;
        t.usage = "Diffuse";
        t.basename = diffuse;
        m.maps.push_back(t);
    }
    if (sref) {
        c5m::TextureMap t;
        t.usage = "BuildingSREF";
        t.basename = sref;
        m.maps.push_back(t);
    }
    return m;
}

struct V { float p[3], n[3], t[2]; uint8_t w[4], i[4]; };

// A mesh of Real32 Position:3, Normal:3, TextureCoordinates0:2 (+ BoneWeights / BoneIndices when weights), one tri group
// per entry of groups (material slot, first triangle, triangle count) over material slots `mats`.
c5m::Mesh mesh(const char* name, const std::vector<V>& vs, const std::vector<uint32_t>& idx, bool weights,
               const std::vector<int32_t>& mats, const std::vector<std::vector<uint32_t>>& groups, int bones) {
    c5m::Mesh m;
    m.name = name;
    auto add = [&](c5m::MemberType t, const char* n, uint32_t count, uint32_t size) {
        c5m::VertexComponent c;
        c.type = t;
        c.name = n;
        c.count = count;
        c.offset = m.format.stride;
        m.format.components.push_back(c);
        m.format.stride += size;
    };
    add(c5m::MemberType::Real32, "Position", 3, 12);
    if (weights) {
        add(c5m::MemberType::NormalUInt8, "BoneWeights", 4, 4);
        add(c5m::MemberType::UInt8, "BoneIndices", 4, 4);
    }
    add(c5m::MemberType::Real32, "Normal", 3, 12);
    add(c5m::MemberType::Real32, "TextureCoordinates0", 2, 8);
    m.vertex_count = static_cast<uint32_t>(vs.size());
    m.vertices.resize(vs.size() * m.format.stride);
    for (size_t v = 0; v < vs.size(); ++v) {
        uint8_t* d = &m.vertices[v * m.format.stride];
        std::memcpy(d, vs[v].p, 12);
        size_t o = 12;
        if (weights) {
            std::memcpy(d + o, vs[v].w, 4);
            std::memcpy(d + o + 4, vs[v].i, 4);
            o += 8;
        }
        std::memcpy(d + o, vs[v].n, 12);
        std::memcpy(d + o + 12, vs[v].t, 8);
    }
    m.index_size = 2;
    m.indices = idx;
    for (const std::vector<uint32_t>& g : groups) {
        c5m::TriGroup tg;
        tg.material = g[0];
        tg.tri_first = g[1];
        tg.tri_count = g[2];
        m.groups.push_back(tg);
    }
    m.materials = mats;
    for (int b = 0; b < bones; ++b) {
        c5m::BoneBinding bb;
        bb.bone_name = "bone" + std::to_string(b);
        m.bone_bindings.push_back(bb);
    }
    return m;
}

V vert(float x, float y, float z) {
    V v = {{x, y, z}, {0, 0, 1}, {0.25f, 0.75f}, {255, 0, 0, 0}, {1, 0, 0, 0}};
    return v;
}

// A file: mesh 0 rigid "Block" (BuildingShader, 4 vertices, 2 triangles in two groups), mesh 1 "Ground"
// (LandmarkShader, not packed), mesh 2 rigid "Roof" (3 vertices), mesh 3 skinned "Bird" (BuildingShader_Skinned, two
// bones). Models: model 0 binds 2 then 0, model 1 binds 3 and 1: the packing order is 2, 0, 3 (1 not packed).
c5m::FileInfo sampleFile() {
    c5m::FileInfo f;
    f.materials.push_back(material("Stone", "BuildingShader", "stone_diff.dds", "stone_sref.dds"));
    f.materials.push_back(material("Ground", "LandmarkShader", nullptr, nullptr));
    f.materials.push_back(material("Bird", "BuildingShader_Skinned", "bird_diff.dds", "bird_sref.dds"));
    f.materials.push_back(material("Roof", "BuildingShader", "roof_diff.dds", nullptr));
    std::vector<V> quad = {vert(0, 0, 0), vert(2, 0, 0), vert(2, 4, 0), vert(0, 4, -6)};
    f.meshes.push_back(mesh("Block", quad, {0, 1, 2, 2, 3, 0}, false, {0, 3}, {{0, 0, 1}, {1, 1, 1}}, 1));
    f.meshes.push_back(mesh("Ground", quad, {0, 1, 2}, false, {1}, {{0, 0, 1}}, 1));
    f.meshes.push_back(mesh("Roof", {vert(1, 1, 1), vert(2, 1, 1), vert(1, 2, 1)}, {0, 1, 2}, false, {3}, {{0, 0, 1}}, 1));
    f.meshes.push_back(mesh("Bird", {vert(0, 0, 8), vert(1, 0, 8), vert(0, 1, 8)}, {2, 1, 0}, true, {2}, {{0, 0, 1}}, 2));
    c5m::ModelEntry a, b;
    a.name = "House";
    a.meshes = {2, 0};
    b.name = "Birds";
    b.meshes = {3, 1};
    f.models.push_back(a);
    f.models.push_back(b);
    return f;
}

uint16_t u16(const std::vector<uint8_t>& b, size_t at) {
    uint16_t v;
    std::memcpy(&v, &b[at], 2);
    return v;
}

}  // namespace

// ---------------------------------------------------------------------------------------------- the key
C5R_TEST(fnv_reference_values) {
    CHECK_EQ(fnv1a64("", 0), kFnvOffset);
    CHECK_EQ(fnv1a64("a", 1), 0xaf63dc4c8601ec8cull);      // the published FNV-1a 64 test vector
    CHECK_EQ(fnv1a64("foobar", 6), 0x85944171f73967e8ull);
}

C5R_TEST(key_leaves_out_the_pad_and_holds_the_vertex_buffer) {
    std::vector<uint16_t> a = {77, 0, 1, 2, 2, 3, 0}, b = {12345, 0, 1, 2, 2, 3, 0}, c = {77, 0, 1, 2, 2, 3, 1};
    const uint8_t vb[8] = {1, 2, 3, 4, 5, 6, 7, 8}, vb2[8] = {1, 2, 3, 4, 5, 6, 7, 9};
    MeshKey ka, kb, kc, kd;
    CHECK_OK(meshKey(a.data(), a.size(), 2, 1, vb, 8, 1, 6, 0, ka));
    CHECK_OK(meshKey(b.data(), b.size(), 2, 1, vb, 8, 1, 6, 0, kb));
    CHECK_OK(meshKey(c.data(), c.size(), 2, 1, vb, 8, 1, 6, 0, kc));
    CHECK_OK(meshKey(a.data(), a.size(), 2, 1, vb2, 8, 1, 6, 0, kd));
    CHECK(ka == kb);                       // the pad element does not count
    CHECK(ka != kc);                       // any other index does, inside or outside the range
    CHECK(ka != kd);                       // and every vertex byte
    CHECK_EQ(ka.indexHash, fnv1a64(a.data() + 1, 12));
    CHECK_EQ(ka.vertexHash, fnv1a64(vb, 8));
    CHECK_EQ(ka.firstIndex, 1u);
    CHECK_EQ(ka.indexCount, 6u);
    MeshKey ke;
    CHECK_OK(meshKey(a.data(), a.size(), 2, 1, vb, 8, 4, 3, 2, ke));
    CHECK(ke != ka);                       // the range is part of the key
    CHECK(ke < ka || ka < ke);
    CHECK(MeshKeyHash()(ka) == MeshKeyHash()(kb));
    std::vector<uint32_t> w = {9, 0, 1, 2};
    MeshKey k32;
    CHECK_OK(meshKey(w.data(), w.size(), 4, 1, vb, 8, 1, 3, 0, k32));
    CHECK_EQ(k32.indexHash, fnv1a64(w.data() + 1, 12));
}

C5R_TEST(key_refusals) {
    std::vector<uint16_t> a = {0, 0, 1, 2};
    const uint8_t vb[4] = {1, 2, 3, 4};
    MeshKey k;
    k.indexHash = 5;
    CHECK_ERR(meshKey(a.data(), a.size(), 3, 1, vb, 4, 1, 3, 0, k), Err::BadLayout);
    CHECK_EQ(k.indexHash, 0u);             // the key is reset on failure
    CHECK_ERR(meshKey(a.data(), a.size(), 2, 1, vb, 4, 1, 0, 0, k), Err::BadLayout);
    CHECK_ERR(meshKey(a.data(), a.size(), 2, 1, vb, 4, 2, 3, 0, k), Err::BadLayout);
    CHECK_ERR(meshKey(a.data(), a.size(), 2, 5, vb, 4, 1, 3, 0, k), Err::BadLayout);
    CHECK_ERR(meshKey(nullptr, 0, 2, 0, vb, 4, 0, 3, 0, k), Err::BadLayout);
    CHECK_ERR(meshKey(a.data(), a.size(), 2, 1, nullptr, 4, 1, 3, 0, k), Err::BadLayout);
    CHECK_ERR(meshKey(a.data(), a.size(), 2, 1, vb, 0, 1, 3, 0, k), Err::BadLayout);
    CHECK_ERR(meshKey(a.data(), a.size(), 2, 1, vb, 4, 0xFFFFFFFFu, 3, 0, k), Err::BadLayout);   // no overflow
}

// ---------------------------------------------------------------------------------------------- the layout
C5R_TEST(mesh_order_follows_the_models) {
    c5m::FileInfo f = sampleFile();
    std::vector<uint32_t> o = meshOrder(f);
    CHECK_EQ(o.size(), 4u);
    CHECK(o == std::vector<uint32_t>({2, 0, 3, 1}));
    f.models[1].meshes = {3, 3, 0};        // a mesh bound twice is taken once; one bound by no model comes last
    o = meshOrder(f);
    CHECK(o == std::vector<uint32_t>({2, 0, 3, 1}));
    f.models.clear();
    CHECK(meshOrder(f) == std::vector<uint32_t>({0, 1, 2, 3}));
}

C5R_TEST(layout_of_a_file) {
    c5m::FileInfo f = sampleFile();
    FileLayout L;
    CHECK_OK(layoutFile(f, false, 0.5f, L));
    CHECK(L.form == VertexForm::Half20);
    CHECK_EQ(L.indexBytes, 2u);
    CHECK_EQ(L.vertexCount, 7u);            // Roof 3 + Block 4
    CHECK_EQ(L.skinnedVertexCount, 3u);     // Bird
    CHECK_EQ(L.indexCount, 1u + 3 + 6 + 3);
    CHECK_EQ(L.unpacked.size(), 1u);
    CHECK_EQ(L.unpacked[0], std::string("Ground"));
    CHECK_EQ(L.parts.size(), 4u);           // Roof, Block group 0, Block group 1, Bird
    if (L.parts.size() != 4) return;
    CHECK_EQ(L.parts[0].mesh, std::string("Roof"));
    CHECK_EQ(L.parts[0].key.firstIndex, 1u);
    CHECK_EQ(L.parts[0].key.indexCount, 3u);
    CHECK_EQ(L.parts[0].key.baseVertex, 0);
    CHECK_EQ(L.parts[0].t0, std::string("roof_diff.dds"));
    CHECK_EQ(L.parts[0].t1, std::string(""));
    CHECK_EQ(L.parts[1].mesh, std::string("Block"));
    CHECK_EQ(L.parts[1].key.firstIndex, 4u);
    CHECK_EQ(L.parts[1].key.indexCount, 3u);
    CHECK_EQ(L.parts[1].key.baseVertex, 3);
    CHECK_EQ(L.parts[1].material, std::string("Stone"));
    CHECK_EQ(L.parts[1].t0, std::string("stone_diff.dds"));
    CHECK_EQ(L.parts[1].t1, std::string("stone_sref.dds"));
    CHECK_EQ(L.parts[2].key.firstIndex, 7u);   // the second group of Block
    CHECK_EQ(L.parts[2].material, std::string("Roof"));
    CHECK_EQ(L.parts[3].mesh, std::string("Bird"));
    CHECK(L.parts[3].skinned);
    CHECK_EQ(L.parts[3].key.firstIndex, 10u);
    CHECK_EQ(L.parts[3].key.baseVertex, 0);    // counted in the skinned buffer
    CHECK_EQ(L.parts[3].shaderSet, std::string("BuildingShader_Skinned"));
    FileLayout B;
    CHECK_ERR(layoutFile(f, true, 1.0f, B), Err::Unsupported);   // a skinned mesh in a city set
    f.meshes[3].groups[0].material = 0;        // the bird as a rigid mesh: a city set takes it
    f.meshes[3].materials = {0};
    CHECK_OK(layoutFile(f, true, 1.0f, B));
    CHECK(B.form == VertexForm::Float32);
    CHECK_EQ(B.skinnedVertexCount, 0u);
    // 4878: a mesh stored with 32-bit indices that all fit 16 bits is drawn R16 by the EXE (city-renderer.md 12.4) ...
    f.meshes[0].index_size = 4;
    CHECK_OK(layoutFile(f, false, 1.0f, B));
    CHECK_EQ(B.indexBytes, 2u);
    // ... and R32 only when an index needs it.
    f.meshes[0].vertex_count = 0x10001u;
    f.meshes[0].indices[1] = 0x10000u;
    CHECK_OK(layoutFile(f, false, 1.0f, B));
    CHECK_EQ(B.indexBytes, 4u);
    f.meshes[0].indices[1] = 0xFFFFu;           // the largest 16-bit index still fits R16
    CHECK_OK(layoutFile(f, false, 1.0f, B));
    CHECK_EQ(B.indexBytes, 2u);
}

C5R_TEST(repack_bytes_of_the_three_forms) {
    c5m::FileInfo f = sampleFile();
    FileLayout L;
    CHECK_OK(layoutFile(f, false, 0.5f, L));
    FileBuffers b;
    CHECK_OK(repackFile(f, L, b));
    CHECK_EQ(b.vertices.size(), 7u * 20);
    CHECK_EQ(b.skinnedVertices.size(), 3u * 28);
    CHECK_EQ(b.indices.size(), 13u * 2);
    // Indices: the pad 0, Roof 0 1 2, Block 0 1 2 2 3 0, Bird 2 1 0, each mesh's own (base vertex added by the draw).
    const uint16_t want[13] = {0, 0, 1, 2, 0, 1, 2, 2, 3, 0, 2, 1, 0};
    for (int i = 0; i < 13; ++i) CHECK_EQ(u16(b.indices, static_cast<size_t>(i) * 2), want[i]);
    // Half20, Block vertex 3 = (0, 4, -6) x 0.5 at base 3: half(0), half(2), half(-3), 0; normal (0, 0, 1) / 0.5.
    const size_t o = (3 + 3) * 20;
    CHECK_EQ(u16(b.vertices, o + 0), 0x0000);
    CHECK_EQ(u16(b.vertices, o + 2), 0x4000);
    CHECK_EQ(u16(b.vertices, o + 4), 0xC200);
    CHECK_EQ(u16(b.vertices, o + 6), 0x0000);
    CHECK_EQ(u16(b.vertices, o + 12), 0x4000);
    CHECK_EQ(u16(b.vertices, o + 16), 0x3400);   // u 0.25
    CHECK_EQ(u16(b.vertices, o + 18), 0x3A00);   // v 0.75
    // Skinned28, Bird vertex 0: (0, 0, 8) x 0.5 -> half(4) at z; weights at 8, indices at 12 as stored.
    CHECK_EQ(u16(b.skinnedVertices, 4), 0x4400);
    CHECK_EQ(b.skinnedVertices[8], 255);
    CHECK_EQ(b.skinnedVertices[12], 1);
    CHECK_EQ(u16(b.skinnedVertices, 24), 0x3400);
    // The unit repack is the model reader's: the bytes equal repack_unit_mesh's for one mesh.
    c5m::UnitBasis basis;
    CHECK(c5m::unit_basis(f, 0.5f, basis, nullptr));
    c5m::UnitMesh um;
    CHECK(c5m::repack_unit_mesh(f.meshes[2], basis, false, um, nullptr));
    CHECK(std::memcmp(um.vertices.data(), b.vertices.data(), um.vertices.size()) == 0);
    // Float32 (a city set): the file's floats at scale 1.
    f.meshes[3].groups[0].material = 0;
    f.meshes[3].materials = {0};
    FileLayout C;
    CHECK_OK(layoutFile(f, true, 1.0f, C));
    FileBuffers cb;
    CHECK_OK(repackFile(f, C, cb));
    CHECK_EQ(cb.vertices.size(), 10u * 32);
    float v[8];
    std::memcpy(v, &cb.vertices[(3 + 3) * 32], 32);
    CHECK(v[0] == 0.0f && v[1] == 4.0f && v[2] == -6.0f && v[5] == 1.0f && v[6] == 0.25f && v[7] == 0.75f);
}

C5R_TEST(layout_refusals) {
    FileLayout L;
    c5m::FileInfo f = sampleFile();
    CHECK_ERR(layoutFile(f, false, 0.0f, L), Err::BadLayout);
    CHECK_ERR(layoutFile(f, false, std::nanf(""), L), Err::BadLayout);
    CHECK_ERR(layoutFile(f, false, -1.0f, L), Err::BadLayout);
    CHECK_ERR(layoutFile(f, true, 1.0f, L), Err::Unsupported);       // a skinned mesh in a city set
    CHECK(L.parts.empty());                                           // nothing kept on failure
    {
        c5m::FileInfo g = sampleFile();
        g.meshes[0].indices[4] = 9;                                   // out of range
        CHECK_ERR(layoutFile(g, false, 1.0f, L), Err::Unsupported);
    }
    {
        c5m::FileInfo g = sampleFile();
        g.meshes[0].indices.pop_back();                               // not a triangle list
        CHECK_ERR(layoutFile(g, false, 1.0f, L), Err::Unsupported);
    }
    {
        c5m::FileInfo g = sampleFile();
        g.meshes[0].groups[1].tri_count = 0;                          // an empty group
        CHECK_ERR(layoutFile(g, false, 1.0f, L), Err::Unsupported);
    }
    {
        c5m::FileInfo g = sampleFile();
        g.meshes[0].groups[1].tri_first = 5;                          // a group past the indices
        CHECK_ERR(layoutFile(g, false, 1.0f, L), Err::Unsupported);
    }
    {
        c5m::FileInfo g = sampleFile();
        for (c5m::Material& m : g.materials) m.shader_set = "LandmarkShader";   // nothing to pack
        CHECK_ERR(layoutFile(g, false, 1.0f, L), Err::Unsupported);
    }
    {
        c5m::FileInfo g = sampleFile();
        g.meshes[3].groups[0].material = 0;
        g.meshes[3].materials = {0};
        c5m::VertexComponent& n = g.meshes[0].format.components[1];   // Block's Normal as Real16 in a city set
        n.type = c5m::MemberType::Real16;
        CHECK_ERR(layoutFile(g, true, 1.0f, L), Err::Unsupported);
    }
    {
        c5m::FileInfo g = sampleFile();
        g.meshes[0].materials = {7, 3};                               // a material slot past the file's materials
        CHECK_OK(layoutFile(g, false, 1.0f, L));                      // not a BuildingShader mesh: left unpacked
        CHECK_EQ(L.unpacked.size(), 2u);
    }
}

C5R_TEST(repack_refusals) {
    c5m::FileInfo f = sampleFile();
    FileLayout L;
    CHECK_OK(layoutFile(f, false, 0.5f, L));
    FileBuffers b;
    FileLayout bad = L;
    bad.form = VertexForm::Skinned28;
    CHECK_ERR(repackFile(f, bad, b), Err::BadLayout);
    CHECK(b.vertices.empty() && b.indices.empty());
    c5m::FileInfo smaller = f;
    smaller.meshes.resize(2);                                         // the layout names meshes the file lacks
    CHECK_ERR(repackFile(smaller, L, b), Err::BadLayout);
    c5m::FileInfo grown = f;
    grown.meshes[2].vertex_count = 2;                                 // a mesh no longer fits its range
    grown.meshes[2].vertices.resize(2 * grown.meshes[2].format.stride);
    CHECK(!repackFile(grown, L, b).ok());
    c5m::FileInfo noWeights = sampleFile();
    noWeights.meshes[3] = sampleFile().meshes[2];                     // the skinned slot gets a mesh without weights
    noWeights.meshes[3].materials = {2};
    FileLayout NL;
    CHECK_OK(layoutFile(noWeights, false, 0.5f, NL));
    CHECK_ERR(repackFile(noWeights, NL, b), Err::Unsupported);
    c5m::FileInfo badBasis = f;
    badBasis.units_per_meter = -1;                                    // a basis the model reader refuses
    badBasis.up[2] = 1;
    CHECK_ERR(repackFile(badBasis, L, b), Err::BadLayout);
}

C5R_TEST_MAIN
