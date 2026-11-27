// test_unit_mesh.cpp - the repack of a .gr2 unit mesh into the EXE's unit vertex streams (issue 4152, K1; the EXE's
// load rule since ticket 0447, unit-renderer.md 10.9). Synthetic part: half rounding (IEEE and Granny's), the weight
// rule, the basis conversion, hand-made meshes (both streams, the FBX basis, signed zeros, NaN, Real16 sources), every
// refusal; expected values from 0379's reference emulation (render/evidence/0379/vstep_check.py). Sample part (C5M_SAMPLES):
// warrior.gr2, warrior_v2.gr2 and warrior_v4.gr2 at the members' Scale 0.14, fingerprinted (FNV-1a 64) against the
// vertex and index buffers of 3901's capture (frame 1762, draws 369-371), the fingerprints measured 2026-11-15 and
// written here as numbers (D83: no captured bytes in the repository).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

#include "c5models/unit_mesh.h"
#include "testing.h"

using namespace c5m;

namespace {

void halves() {
    CHECK_EQ(half_from_double(0.0), 0x0000);
    CHECK_EQ(half_from_double(-0.0), 0x8000);
    CHECK_EQ(half_from_double(1.0), 0x3C00);
    CHECK_EQ(half_from_double(-2.0), 0xC000);
    CHECK_EQ(half_from_double(65504.0), 0x7BFF);
    CHECK_EQ(half_from_double(65519.99), 0x7BFF);
    CHECK_EQ(half_from_double(65520.0), 0x7C00);          // the tie above the largest half goes to infinity
    CHECK_EQ(half_from_double(1e30), 0x7C00);
    CHECK_EQ(half_from_double(std::ldexp(1.0, -24)), 0x0001);
    CHECK_EQ(half_from_double(std::ldexp(1.0, -25)), 0x0000);           // tie to even (0)
    CHECK_EQ(half_from_double(std::ldexp(3.0, -25)), 0x0002);           // 1.5 ulp: tie to even (2)
    CHECK_EQ(half_from_double(std::ldexp(1.0, -14)), 0x0400);           // smallest normal
    CHECK_EQ(half_from_double(std::ldexp(1023.5, -24)), 0x0400);        // subnormal tie carries into the normals
    CHECK_EQ(half_from_double(1.0 + std::ldexp(1.0, -11)), 0x3C00);     // tie to even
    CHECK_EQ(half_from_double(1.0 + std::ldexp(3.0, -11)), 0x3C02);     // tie to even (up)
    CHECK_EQ(half_from_double(1.0 + std::ldexp(1.0, -11) + 1e-12), 0x3C01);
    CHECK_EQ(half_from_double(0.1), 0x2E66);
    CHECK((half_from_double(std::nan("")) & 0x7E00) == 0x7E00);
    // round trip of every finite half
    int bad = 0;
    for (uint32_t h = 0; h < 0x10000; ++h) {
        if ((h & 0x7C00) == 0x7C00) continue;
        if (half_from_double(double_from_half(static_cast<uint16_t>(h))) != h && h != 0x8000) ++bad;
    }
    CHECK_EQ(bad, 0);
    // a single rounding from double differs from rounding through float first (the normals of K1)
    // 4 + 2^-9 + 1e-9 lies just above the tie between 4.0 (0x4400) and the next half (ulp 2^-8): one rounding gives
    // 0x4401; through float32 the 1e-9 is lost, the tie goes to even, 0x4400
    const double x = 4.0 + std::ldexp(1.0, -9) + 1e-9;
    CHECK_EQ(half_from_double(x), 0x4401);
    CHECK_EQ(half_from_double(static_cast<double>(static_cast<float>(x))), 0x4400);
    // the EXE's conversion (wave 2): ties away from zero
    CHECK_EQ(half_from_float_away(1.0f + std::ldexp(1.0f, -11)), 0x3C01);
    CHECK_EQ(half_from_float_away(-(1.0f + std::ldexp(1.0f, -11))), 0xBC01);
    CHECK_EQ(half_from_float_away(1.0f + std::ldexp(3.0f, -11)), 0x3C02);
    CHECK_EQ(half_from_float_away(std::ldexp(1.0f, -25)), 0x0001);
    CHECK_EQ(half_from_float_away(65520.0f), 0x7C00);
    CHECK_EQ(half_from_float_away(0.1f), 0x2E66);
    CHECK_EQ(half_from_float_away(1.0f), 0x3C00);
    int diff = 0;   // away and even differ only at ties
    for (uint32_t h = 0; h < 0x7C00; ++h) {
        const float v = static_cast<float>(double_from_half(static_cast<uint16_t>(h)));
        if (half_from_float_away(v) != h) ++diff;
    }
    CHECK_EQ(diff, 0);
}

void granny_halves() {
    // Granny's conversion (unit-renderer.md 10.9 step 3); values from 0379's reference emulation (vstep_check.py)
    CHECK_EQ(half_from_float_granny(1.0f), 0x3C00);
    CHECK_EQ(half_from_float_granny(-0.0f), 0x8000);
    CHECK_EQ(half_from_float_granny(0.0f), 0x0000);
    CHECK_EQ(half_from_float_granny(1.0f + std::ldexp(1.0f, -11)), 0x3C01);      // tie away from zero
    CHECK_EQ(half_from_float_granny(-(1.0f + std::ldexp(1.0f, -11))), 0xBC01);
    CHECK_EQ(half_from_float_granny(std::ldexp(1.0f, -25)), 0x0001);             // subnormal tie, away
    CHECK_EQ(half_from_float_granny(std::ldexp(1.5f, -24)), 0x0002);
    CHECK_EQ(half_from_float_granny(std::ldexp(1.00048828125f, -15)), 0x0200);
    CHECK_EQ(half_from_float_granny(1e-10f), 0x0000);
    CHECK_EQ(half_from_float_granny(65519.0f), 0x7BFF);
    CHECK_EQ(half_from_float_granny(65520.0f), 0x7C00);
    CHECK_EQ(half_from_float_granny(1e30f), 0x7C00);
    CHECK_EQ(half_from_float_granny(-INFINITY), 0xFC00);
    // a NaN keeps its sign and its top ten mantissa bits, 1 when they are all zero
    auto nan = [](uint32_t b) {
        float f;
        std::memcpy(&f, &b, 4);
        return half_from_float_granny(f);
    };
    CHECK_EQ(nan(0x7FA00001u), 0x7D00);
    CHECK_EQ(nan(0xFF800001u), 0xFC01);
    CHECK_EQ(nan(0x7F800000u | (0x155u << 13)), 0x7D55);
    CHECK_EQ(nan(0xFFC00000u), 0xFE00);
    // every finite half round-trips; on a sweep of floats it agrees with ties-away rounding (the NaNs aside)
    int bad = 0;
    for (uint32_t h = 0; h < 0x10000; ++h) {
        if ((h & 0x7C00) == 0x7C00) continue;
        if (half_from_float_granny(static_cast<float>(double_from_half(static_cast<uint16_t>(h)))) != h) ++bad;
    }
    CHECK_EQ(bad, 0);
    int differ = 0;
    for (uint32_t b = 0; b < 0x7F800000u; b += 97) {   // every exponent, normal and subnormal halves, overflow
        float f;
        std::memcpy(&f, &b, 4);
        if (half_from_float_granny(f) != half_from_float_away(f)) ++differ;
        if (half_from_float_granny(-f) != half_from_float_away(-f)) ++differ;
    }
    CHECK_EQ(differ, 0);
}

void weights() {
    // step 4 (unit-renderer.md 10.9); expected values from 0379's reference emulation
    struct C { uint8_t in[4], out[4]; };
    const C cs[] = {
        {{255, 0, 0, 0}, {255, 0, 0, 0}},
        {{120, 80, 55, 0}, {120, 80, 55, 0}},      // sum 255, fourth 0: unchanged
        {{119, 62, 57, 17}, {127, 66, 62, 0}},     // warrior_v2 vertex 428: the exact half 127.5 lands below (captured)
        {{122, 78, 44, 11}, {128, 82, 45, 0}},     // the other three captured ties, rounded up
        {{122, 87, 35, 11}, {128, 91, 36, 0}},
        {{91, 85, 62, 17}, {98, 91, 66, 0}},
        {{100, 50, 50, 55}, {128, 64, 63, 0}},
        {{90, 60, 30, 75}, {128, 85, 42, 0}},
        {{200, 30, 0, 0}, {222, 33, 0, 0}},        // fourth 0 but not summing to 255: renormalised too (licorne.gr2)
        {{1, 1, 0, 9}, {128, 128, 255, 0}},        // a2 = -1: its low byte [I]
        {{0, 0, 0, 255}, {0, 0, 0, 255}},          // s = 0: as it is
        {{0, 0, 0, 0}, {0, 0, 0, 0}},
    };
    for (const C& c : cs) {
        uint8_t w[4];
        std::memcpy(w, c.in, 4);
        unit_weights(w);
        if (std::memcmp(w, c.out, 4) != 0)
            std::printf("weights %u %u %u %u -> %u %u %u %u\n", c.in[0], c.in[1], c.in[2], c.in[3], w[0], w[1], w[2], w[3]);
        CHECK(std::memcmp(w, c.out, 4) == 0);
    }
    std::mt19937 rng(4152);
    int bad = 0;
    for (int i = 0; i < 100000; ++i) {
        uint8_t w[4];
        for (auto& x : w) x = static_cast<uint8_t>(rng() & 0xFF);
        if (i & 1) {   // half the cases sum to 255 with a fourth of 0 (a fixed point), when they can
            w[2] = static_cast<uint8_t>(255 - w[0] - w[1]);
            w[3] = 0;
        }
        uint8_t keep[4];
        std::memcpy(keep, w, 4);
        unit_weights(w);
        const uint32_t s = static_cast<uint32_t>(keep[0]) + keep[1] + keep[2];
        if (s == 0) {
            if (std::memcmp(w, keep, 4)) ++bad;
        } else if (((w[0] + w[1] + w[2]) & 0xFF) != 255 || w[3] != 0) {
            ++bad;
        } else if (s == 255 && keep[3] == 0 && std::memcmp(w, keep, 4)) {
            ++bad;
        }
    }
    CHECK_EQ(bad, 0);
}

uint32_t fbits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

void bases() {
    // k = fl(fl(UnitsPerMeter x Scale) / UnitsPerMeter): one ulp above Scale at 0.12, one below at 0.11, Scale at
    // 0.07, 0.08, 0.13, 0.14 for the stock files (unit-renderer.md 10.9); always Scale for FBX (1.0 units per metre).
    // Values from 0379's reference emulation.
    struct K { float s; uint32_t k, i0; };
    const K ks[] = {{0.07f, 0x3d8f5c29u, 0x41649249u}, {0.08f, 0x3da3d70au, 0x41480000u},
                    {0.11f, 0x3de147adu, 0x4111745eu}, {0.12f, 0x3df5c290u, 0x41055555u},
                    {0.13f, 0x3e051eb8u, 0x40f62764u}, {0.14f, 0x3e0f5c29u, 0x40e49249u},
                    {0.088f, 0x3db43958u, 0x4135d175u}};
    std::string err;
    for (const K& c : ks) {
        UnitBasis b;
        CHECK(unit_basis_stock(c.s, b, &err));
        CHECK_EQ(fbits(b.k), c.k);
        CHECK_EQ(fbits(b.L[0]), c.k);
        CHECK_EQ(fbits(b.L[4]), c.k);
        CHECK_EQ(fbits(b.I[0]), c.i0);   // 0.13, 0.088: one ulp above fl(1 / k) (the cofactor rounding)
        CHECK_EQ(fbits(b.I[8]), c.i0);
        CHECK_EQ(fbits(b.I[1]), 0x80000000u);   // signed zeros off the diagonal, as the reference has them
        CHECK_EQ(fbits(b.I[2]), 0x00000000u);
        UnitBasis f;
        CHECK(unit_basis_fbx(c.s, f, &err));
        CHECK_EQ(fbits(f.k), fbits(c.s));
    }
    UnitBasis f;
    CHECK(unit_basis_fbx(0.12f, f, &err));
    // (x, y, z) -> (x, -z, y), times k
    CHECK(f.L[0] == 0.12f && f.L[5] == -0.12f && f.L[7] == 0.12f && f.L[1] == 0 && f.L[4] == 0 && f.L[8] == 0);
    // FileInfo: its ArtToolInfo; none: the stock basis at 39.37008
    FileInfo fi;
    UnitBasis a, s;
    CHECK(unit_basis(fi, 0.12f, a, &err));
    CHECK(unit_basis_stock(0.12f, s, &err));
    CHECK(std::memcmp(&a, &s, sizeof a) == 0);
    fi.units_per_meter = 1.0f;
    fi.right[0] = 1;
    fi.up[1] = 1;
    fi.back[2] = 1;
    CHECK(unit_basis(fi, 0.12f, a, &err));
    CHECK(std::memcmp(&a, &f, sizeof a) == 0);
    fi.origin[0] = 2.0f;
    CHECK(unit_basis(fi, 0.12f, a, &err));
    CHECK_EQ(a.A[0], -2.0f);
    // refusals
    const float r[3] = {1, 0, 0}, up[3] = {0, 0, 1}, back[3] = {0, -1, 0}, o[3] = {0, 0, 0}, flat[3] = {1, 0, 0};
    CHECK(!unit_basis(39.37f, r, up, back, o, 0.0f, a, &err) && err.find("scale") != std::string::npos);
    CHECK(!unit_basis(39.37f, r, up, back, o, std::nanf(""), a, &err));
    CHECK(!unit_basis(0.0f, r, up, back, o, 0.1f, a, &err) && err.find("UnitsPerMeter") != std::string::npos);
    CHECK(!unit_basis(-1.0f, r, up, back, o, 0.1f, a, &err));
    CHECK(!unit_basis(39.37f, r, flat, back, o, 0.1f, a, &err) && err.find("singular") != std::string::npos);
    const float nanv[3] = {std::nanf(""), 0, 0};
    CHECK(!unit_basis(39.37f, r, up, back, nanv, 0.1f, a, &err) && err.find("finite") != std::string::npos);
    fi = FileInfo();
    fi.units_per_meter = 39.37f;   // an ArtToolInfo whose axes are all zero: singular, refused
    CHECK(!unit_basis(fi, 0.1f, a, &err) && err.find("singular") != std::string::npos);
}

// a hand-made two-vertex-format mesh: the .gr2 68-byte skinned vertex (Position, BoneWeights, BoneIndices, Normal,
// Tangent, Binormal, TextureCoordinates0[3])
Mesh synthetic(uint32_t bones) {
    Mesh m;
    m.name = "synthetic";
    auto add = [&](MemberType t, const char* n, uint32_t count, uint32_t off) {
        VertexComponent c;
        c.type = t;
        c.name = n;
        c.count = count;
        c.offset = off;
        m.format.components.push_back(c);
    };
    add(MemberType::Real32, "Position", 3, 0);
    add(MemberType::NormalUInt8, "BoneWeights", 4, 12);
    add(MemberType::UInt8, "BoneIndices", 4, 16);
    add(MemberType::Real32, "Normal", 3, 20);
    add(MemberType::Real32, "Tangent", 3, 32);
    add(MemberType::Real32, "Binormal", 3, 44);
    add(MemberType::Real32, "TextureCoordinates0", 3, 56);
    m.format.stride = 68;
    m.vertex_count = 3;
    m.vertices.assign(3 * 68, 0);
    const float pos[3][3] = {{100, -50, 0}, {7.25f, 3, 117.9f}, {-95.3f, -12.5f, 0}};
    const uint8_t w[3][4] = {{255, 0, 0, 0}, {100, 50, 50, 55}, {128, 127, 0, 0}};
    const uint8_t b[3][4] = {{2, 2, 2, 2}, {0, 1, 4, 7}, {3, 1, 0, 0}};
    const float n[3][3] = {{1, 0, 0}, {0, 0.6f, 0.8f}, {-0.9688789f, -0.0504792f, -0.2423336f}};
    const float uv[3][3] = {{0.25f, 0.75f, 0.5f}, {0.964f, 0.5303f, 0.57f}, {0, 1, 0}};
    for (int v = 0; v < 3; ++v) {
        uint8_t* p = m.vertices.data() + v * 68;
        std::memcpy(p, pos[v], 12);
        std::memcpy(p + 12, w[v], 4);
        std::memcpy(p + 16, b[v], 4);
        std::memcpy(p + 20, n[v], 12);
        std::memcpy(p + 56, uv[v], 12);
    }
    m.indices = {0, 1, 2};
    m.index_size = 2;
    m.bone_bindings.resize(bones);
    return m;
}

uint16_t rd16(const std::vector<uint8_t>& v, size_t off) { return static_cast<uint16_t>(v[off] | (v[off + 1] << 8)); }

void repack_synthetic() {
    const float scale = 0.14f;
    UnitMesh u;
    std::string err;
    CHECK(repack_unit_mesh(synthetic(20), scale, u, &err));
    CHECK_EQ(u.vertex_count, 3u);
    CHECK_EQ(u.stride, kUnitVertexStride);
    CHECK_EQ(u.vertices.size(), 3u * 28u);
    CHECK_EQ(u.renormalised, 1u);
    CHECK_EQ(u.bones, 20u);
    CHECK(u.indices == std::vector<uint16_t>({0, 1, 2}));
    // expected halves: 0379's reference emulation (vstep_check.py exe_matrices, stock ArtToolInfo, Scale 0.14)
    if (u.vertices.size() == 84) {
        CHECK_EQ(rd16(u.vertices, 0), 0x4B00);
        CHECK_EQ(rd16(u.vertices, 2), 0xC700);
        CHECK_EQ(rd16(u.vertices, 4), 0x0000);
        CHECK_EQ(rd16(u.vertices, 6), 0);   // w = 0
        CHECK(u.vertices[8] == 255 && u.vertices[9] == 0 && u.vertices[12] == 2 && u.vertices[15] == 2);
        CHECK_EQ(rd16(u.vertices, 16), 0x4725);   // the normal through I^T, not renormalised
        CHECK_EQ(rd16(u.vertices, 18), 0x0000);
        CHECK_EQ(rd16(u.vertices, 22), 0);
        CHECK_EQ(rd16(u.vertices, 24), half_from_float_granny(0.25f));
        CHECK_EQ(rd16(u.vertices, 26), half_from_float_granny(0.75f));
        // vertex 1: renormalised weights, indices as they are (the fourth kept)
        CHECK(u.vertices[28 + 8] == 128 && u.vertices[28 + 9] == 64 && u.vertices[28 + 10] == 63 && u.vertices[28 + 11] == 0);
        CHECK(u.vertices[28 + 12] == 0 && u.vertices[28 + 13] == 1 && u.vertices[28 + 14] == 4 && u.vertices[28 + 15] == 7);
        CHECK_EQ(rd16(u.vertices, 28 + 0), 0x3C0F);
        CHECK_EQ(rd16(u.vertices, 28 + 2), 0x36B8);
        CHECK_EQ(rd16(u.vertices, 28 + 4), 0x4C20);
        CHECK_EQ(rd16(u.vertices, 28 + 16), 0x0000);
        CHECK_EQ(rd16(u.vertices, 28 + 18), 0x4449);
        CHECK_EQ(rd16(u.vertices, 28 + 20), 0x45B7);
        // vertex 2: two weights summing to 255, unchanged
        CHECK(u.vertices[56 + 8] == 128 && u.vertices[56 + 9] == 127 && u.vertices[56 + 10] == 0);
        CHECK_EQ(rd16(u.vertices, 56 + 0), 0xCAAC);
        CHECK_EQ(rd16(u.vertices, 56 + 2), 0xBF00);
        CHECK_EQ(rd16(u.vertices, 56 + 16), 0xC6EC);
        CHECK_EQ(rd16(u.vertices, 56 + 18), 0xB5C5);
        CHECK_EQ(rd16(u.vertices, 56 + 20), 0xBEEC);
    }
    // the 20-byte stream of the same mesh: position @0, normal @8, uv @16; no weights
    UnitBasis b;
    CHECK(unit_basis_stock(scale, b, &err));
    UnitMesh st;
    CHECK(repack_unit_mesh(synthetic(1), b, false, st, &err));
    CHECK_EQ(st.stride, kUnitStaticStride);
    CHECK_EQ(st.vertices.size(), 3u * 20u);
    CHECK_EQ(st.renormalised, 0u);
    if (st.vertices.size() == 60) {
        for (uint32_t v = 0; v < 3; ++v) {
            for (int c = 0; c < 3; ++c) {
                CHECK_EQ(rd16(st.vertices, v * 20 + 2 * c), rd16(u.vertices, v * 28 + 2 * c));
                CHECK_EQ(rd16(st.vertices, v * 20 + 8 + 2 * c), rd16(u.vertices, v * 28 + 16 + 2 * c));
            }
            CHECK_EQ(rd16(st.vertices, v * 20 + 6), 0);
            CHECK_EQ(rd16(st.vertices, v * 20 + 14), 0);
            CHECK_EQ(rd16(st.vertices, v * 20 + 16), rd16(u.vertices, v * 28 + 24));
            CHECK_EQ(rd16(st.vertices, v * 20 + 18), rd16(u.vertices, v * 28 + 26));
        }
    }
    // a 20-byte mesh without weights at all, and with more than 32 bindings (no palette block: not refused)
    {
        Mesh m = synthetic(40);
        m.format.components.erase(m.format.components.begin() + 1, m.format.components.begin() + 3);
        CHECK(repack_unit_mesh(m, b, false, st, &err));
        CHECK(!repack_unit_mesh(m, b, true, st, &err) && err.find("BoneWeights") != std::string::npos);
    }
    // the FBX basis: (x, y, z) -> (x, -z, y) x k; positions (1, 2, 3) at 0.12 and normal (0, 0, 1) (reference values)
    {
        Mesh m = synthetic(20);
        const float p[3] = {1, 2, 3}, n[3] = {0, 0, 1};
        std::memcpy(m.vertices.data(), p, 12);
        std::memcpy(m.vertices.data() + 20, n, 12);
        UnitBasis f;
        CHECK(unit_basis_fbx(0.12f, f, &err));
        CHECK(repack_unit_mesh(m, f, true, u, &err));
        CHECK(rd16(u.vertices, 0) == 0x2FAE && rd16(u.vertices, 2) == 0xB5C3 && rd16(u.vertices, 4) == 0x33AE);
        CHECK(rd16(u.vertices, 16) == 0x0000 && rd16(u.vertices, 18) == 0xC82B && rd16(u.vertices, 20) == 0x0000);
    }
    // signed zeros: each output component is -0 only when its three products are (reference values)
    {
        Mesh m = synthetic(20);
        const float p[3] = {-0.0f, -0.0f, -0.0f}, n[3] = {-0.0f, -0.0f, 1.0f};
        std::memcpy(m.vertices.data(), p, 12);
        std::memcpy(m.vertices.data() + 20, n, 12);
        CHECK(repack_unit_mesh(m, scale, u, &err));
        CHECK(rd16(u.vertices, 0) == 0 && rd16(u.vertices, 2) == 0 && rd16(u.vertices, 4) == 0);   // the affine + 0
        CHECK(rd16(u.vertices, 16) == 0 && rd16(u.vertices, 18) == 0 && rd16(u.vertices, 20) == 0x4725);
    }
    // a NaN coordinate: all three coordinates the first NaN, quieted, its payload's top bits kept
    {
        Mesh m = synthetic(20);
        const uint32_t nanbits = 0xFF812345u;   // signalling, sign set
        std::memcpy(m.vertices.data() + 4, &nanbits, 4);
        CHECK(repack_unit_mesh(m, scale, u, &err));
        const uint16_t h = half_from_float_granny([] {
            const uint32_t q = 0xFFC12345u;
            float f;
            std::memcpy(&f, &q, 4);
            return f;
        }());
        CHECK_EQ(h, 0xFE09);
        CHECK(rd16(u.vertices, 0) == h && rd16(u.vertices, 2) == h && rd16(u.vertices, 4) == h);
    }
    // Real16 normals and texture coordinates (several DLC units): the normal widened exactly and converted, a -0 x
    // component comes out +0 (10.2's special case is the general rule); the coordinates copied as they are
    {
        Mesh m;
        m.name = "half";
        auto add = [&](MemberType t, const char* nm, uint32_t count, uint32_t off) {
            VertexComponent c;
            c.type = t;
            c.name = nm;
            c.count = count;
            c.offset = off;
            m.format.components.push_back(c);
        };
        add(MemberType::Real32, "Position", 3, 0);
        add(MemberType::NormalUInt8, "BoneWeights", 4, 12);
        add(MemberType::UInt8, "BoneIndices", 4, 16);
        add(MemberType::Real16, "Normal", 3, 20);
        add(MemberType::Real16, "TextureCoordinates0", 2, 26);
        m.format.stride = 30;
        m.vertex_count = 1;
        m.vertices.assign(30, 0);
        const float p[3] = {100, -50, 0};
        std::memcpy(m.vertices.data(), p, 12);
        m.vertices[12] = 255;
        const uint16_t nh[3] = {0x8000, 0x3800, 0x3800}, th[2] = {0x3555, 0x7E01};   // (-0, 0.5, 0.5); a NaN uv kept
        std::memcpy(m.vertices.data() + 20, nh, 6);
        std::memcpy(m.vertices.data() + 26, th, 4);
        m.indices = {0, 0, 0};
        m.bone_bindings.resize(2);
        CHECK(repack_unit_mesh(m, scale, u, &err));
        CHECK(rd16(u.vertices, 0) == 0x4B00 && rd16(u.vertices, 2) == 0xC700);
        CHECK(rd16(u.vertices, 16) == 0x0000 && rd16(u.vertices, 18) == 0x4325 && rd16(u.vertices, 20) == 0x4325);
        CHECK(rd16(u.vertices, 24) == 0x3555 && rd16(u.vertices, 26) == 0x7E01);
    }
}

void refusals() {
    UnitMesh u;
    std::string err;
    {
        Mesh m = synthetic(20);
        m.format.components.erase(m.format.components.begin() + 3);   // no Normal
        CHECK(!repack_unit_mesh(m, 0.14f, u, &err) && err.find("needs") != std::string::npos && u.vertices.empty());
    }
    {
        Mesh m = synthetic(20);
        m.format.components[1].type = MemberType::UInt8;   // weights of the wrong type
        CHECK(!repack_unit_mesh(m, 0.14f, u, &err) && u.vertices.empty());
    }
    CHECK(!repack_unit_mesh(synthetic(33), 0.14f, u, &err) && err.find("K8") != std::string::npos);
    CHECK(repack_unit_mesh(synthetic(32), 0.14f, u, &err));
    CHECK(!repack_unit_mesh(synthetic(20), 0.0f, u, &err) && err.find("scale") != std::string::npos);
    CHECK(!repack_unit_mesh(synthetic(20), std::nanf(""), u, &err));
    CHECK(!repack_unit_mesh(synthetic(20), -1.0f, u, &err));
    // 4 bones: vertex 1 weights bone 4 (its fourth index 7 has weight 0 after the rule and is not checked)
    CHECK(!repack_unit_mesh(synthetic(4), 0.14f, u, &err) && err.find("names bone") != std::string::npos);
    CHECK(repack_unit_mesh(synthetic(5), 0.14f, u, &err));
    {
        Mesh m = synthetic(20);
        m.indices = {0, 1, 3};
        CHECK(!repack_unit_mesh(m, 0.14f, u, &err) && err.find("index") != std::string::npos);
    }
    {
        Mesh m = synthetic(20);
        m.vertices.pop_back();
        CHECK(!repack_unit_mesh(m, 0.14f, u, &err) && err.find("count x stride") != std::string::npos);
    }
    {
        Mesh m = synthetic(20);
        m.format.components[6].offset = 64;   // TextureCoordinates0[2] would end at byte 72 of a 68-byte vertex
        CHECK(!repack_unit_mesh(m, 0.14f, u, &err) && err.find("outside") != std::string::npos);
    }
    {
        Mesh m = synthetic(20);
        // only a fourth weight: the EXE's rule leaves it as it is (s = 0), no longer refused (ticket 0447)
        const uint8_t only4[4] = {0, 0, 0, 255};
        std::memcpy(m.vertices.data() + 12, only4, 4);
        CHECK(repack_unit_mesh(m, 0.14f, u, &err));
        CHECK(u.vertices.size() == 84 && std::memcmp(u.vertices.data() + 8, only4, 4) == 0);
        // ... but its bone index is checked like any weighted one
        m.vertices[16 + 3] = 20;
        CHECK(!repack_unit_mesh(m, 0.14f, u, &err) && err.find("names bone 20") != std::string::npos);
    }
    {
        Mesh m = synthetic(20);
        m.vertex_count = 70000;
        m.vertices.assign(static_cast<size_t>(70000) * 68, 0);
        m.indices.clear();
        CHECK(!repack_unit_mesh(m, 0.14f, u, &err) && err.find("65,536") != std::string::npos);
    }
}

bool load_db(const char* rel, StringDatabase& db) {
    Bytes b;
    if (!t::sample(rel, b)) return false;
    Error e;
    const bool ok = db.add_gsd(b.data(), b.size(), &e);
    CHECK(ok);
    return ok;
}

void samples() {
    StringDatabase units;
    if (!load_db("fpk/UnitModels/unitmodels_stringdatabase.gsd", units)) return;
    struct Case {
        const char* file;
        uint32_t mesh;
        float scale;
        uint32_t vertices, renormalised;
        uint64_t vb, ib;      // fingerprints of the rule's output
        bool equals_capture;  // the rule's vertex buffer is the captured one (index buffers all are)
    };
    // 3901's capture, frame 1762: draw 370 (warrior), 369 (warrior_v2), 371 (warrior_v4). warrior_v2's two vertices
    // with the weights (119, 62, 57, 17) are captured as (127, 66, 62); 4152's integer rule gave (128, 66, 61) and the
    // fingerprint 0xded26cc25d5c8467; the EXE's float rule (ticket 0447, unit-renderer.md 10.9) gives the captured
    // buffer, 0xa06d5cd8b80e2ab7 (unit-playback.md section 3). The basis comes from each file's ArtToolInfo.
    // wave 2 (2026-11-15): 3907's catalogue, units_land/L09 (tank, draws 39-40, 118-119, 571-572), L01 (anti-aircraft
    // gun, draws 18-20, 75-77, 457-459), units_sea/S03 (trireme, draws 33-35, 81-83, 364-366): every drawn mesh equal.
    const Case cases[] = {
        {"fpk/UnitModels/warrior.gr2", 0, 0.14f, 863, 0, 0x185b675f25084168ull, 0x7db04194ce75d55cull, true},
        {"fpk/UnitModels/warrior_v2.gr2", 0, 0.14f, 829, 48, 0xa06d5cd8b80e2ab7ull, 0x5a7ddf2351704394ull, true},
        {"fpk/UnitModels/warrior_v4.gr2", 0, 0.14f, 796, 8, 0xc368ed5b584dc4cdull, 0x9dfc85421b19aecfull, true},
        {"fpk/UnitModels/tank.gr2", 0, 0.12f, 104, 0, 0x80cd5b2e3e16cf3aull, 0xa391fc49a0ccdc13ull, true},
        {"fpk/UnitModels/tank.gr2", 1, 0.12f, 1440, 0, 0xf78a59703b4357e6ull, 0x9ebc6d8f4e6b1cc1ull, true},
        {"fpk/UnitModels/antiaircraftgun.gr2", 0, 0.12f, 710, 0, 0xd9fd155f8100a8baull, 0x57f74c6a5e25567dull, true},
        {"fpk/UnitModels/antiaircraftgun.gr2", 1, 0.12f, 1041, 0, 0x211028e312325075ull, 0x94d98206453c9f92ull, true},
        {"fpk/UnitModels/antiaircraftgun.gr2", 2, 0.12f, 2415, 0, 0xe8de0cb871d17edcull, 0xdfb466f34c10e003ull, true},
        {"fpk/UnitModels/trireme.gr2", 2, 0.07f, 792, 0, 0x6eb2982600fbe0c3ull, 0x67a79f3b9c6bbaa9ull, true},
        {"fpk/UnitModels/trireme.gr2", 3, 0.07f, 864, 0, 0xb083a18691ca53c1ull, 0xe34652a6bf814fdeull, true},
        {"fpk/UnitModels/trireme.gr2", 4, 0.07f, 1499, 0, 0x1ba83cd828d4cbc6ull, 0x7e9c62f41078c224ull, true},
    };
    for (const Case& c : cases) {
        Bytes b;
        if (!t::sample(c.file, b)) continue;
        FileInfo fi;
        Error e;
        CHECK(read_gr2(b.data(), b.size(), &units, fi, &e));
        if (fi.meshes.size() <= c.mesh) {
            CHECK(fi.meshes.size() > c.mesh);
            continue;
        }
        UnitMesh u;
        std::string err;
        UnitBasis basis;
        CHECK(unit_basis(fi, c.scale, basis, &err));
        CHECK(unit_mesh_skinned(fi.meshes[c.mesh]));
        CHECK(repack_unit_mesh(fi.meshes[c.mesh], basis, true, u, &err));
        CHECK_EQ(u.vertex_count, c.vertices);
        CHECK_EQ(u.renormalised, c.renormalised);
        CHECK(u.bones <= kUnitPaletteBones && u.bones == fi.meshes[c.mesh].bone_bindings.size());
        const uint64_t vb = fnv1a64(u.vertices.data(), u.vertices.size());
        const uint64_t ib = fnv1a64(reinterpret_cast<const uint8_t*>(u.indices.data()), u.indices.size() * 2);
        std::printf("%s mesh %u: %u vertices, %u renormalised, vb 0x%016llx, ib 0x%016llx%s\n", c.file, c.mesh, u.vertex_count,
                    u.renormalised, static_cast<unsigned long long>(vb), static_cast<unsigned long long>(ib),
                    c.equals_capture ? " (= the capture's)" : "");
        CHECK_EQ(vb, c.vb);
        CHECK_EQ(ib, c.ib);
    }
}

}  // namespace

int main() {
    halves();
    granny_halves();
    weights();
    bases();
    repack_synthetic();
    refusals();
    samples();
    return t::finish();
}
