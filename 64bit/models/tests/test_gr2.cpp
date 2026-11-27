// test_gr2.cpp - tests of the core reader, every stage with negative tests (epic 3100, issue 3101), 2026-11-11.
// Synthetic part: a small uncompressed Granny file built here byte by byte (no game data). Real part (needs
// C5M_SAMPLES = the extraction root): Oodle1 on real sections, a unit, a wonder, a deer camp, and corruptions of
// a real file.
#include "c5models/gr2.h"
#include "gr2_writer.h"
#include "testing.h"

#include <cmath>
#include <cstring>
#include <initializer_list>
#include <random>

using namespace c5m;

namespace {

using namespace gr2w;   // the writer: tests/gr2_writer.h

// Root {String FromFileName; ArrayOfReferences Meshes -> Mesh}
// Mesh {String Name; Reference PrimaryVertexData -> VD; Reference PrimaryTopology -> Topo}
// VD {ReferenceToVariantArray Vertices}, vertex type V {Real32 Position[3]}
// Topo {ReferenceToArray Groups -> G{Int32 MaterialIndex, TriFirst, TriCount}; ReferenceToArray Indices16 -> I{UInt16 Int16}}
struct Synth {
    Bytes file;
    uint32_t meshes_ptr_slot = 0;    // section 0 offset of Root.Meshes' pointer
    uint32_t vertex_data_off = 0;    // section 0 offset of the vertex array
    uint32_t index_data_off = 0;
    uint32_t type_g_member0 = 0;     // section 1 offset of G's first member entry
};

Synth make(float nan_at = 0, uint16_t bad_index = 2) {
    std::vector<Sec> s(2);
    Sec& o = s[S_OBJ];
    Sec& t = s[S_TYPE];
    TypeWriter V(t); V.member(10, "Position", ~0u, 3); uint32_t tV = V.end();
    TypeWriter I(t); I.member(16, "Int16"); uint32_t tI = I.end();
    TypeWriter G(t); G.member(19, "MaterialIndex"); G.member(19, "TriFirst"); G.member(19, "TriCount"); uint32_t tG = G.end();
    TypeWriter T(t); T.member(3, "Groups", tG); T.member(3, "Indices16", tI); uint32_t tT = T.end();
    TypeWriter D(t); D.member(7, "Vertices"); uint32_t tD = D.end();
    TypeWriter M(t); M.member(8, "Name"); M.member(2, "PrimaryVertexData", tD); M.member(2, "PrimaryTopology", tT); uint32_t tM = M.end();
    TypeWriter R(t); R.member(8, "FromFileName"); R.member(4, "Meshes", tM); uint32_t tR = R.end();
    finish_names(t, {&V, &I, &G, &T, &D, &M, &R});
    Synth sy;
    sy.type_g_member0 = tG;
    // objects
    uint32_t root = o.put32(0); o.put32(1); uint32_t mptr = o.put32(0);
    sy.meshes_ptr_slot = mptr;
    uint32_t marr = o.put32(0);
    uint32_t mesh = o.put32(0); uint32_t vdp = o.put32(0); uint32_t tpp = o.put32(0);
    uint32_t vd = o.put32(0); o.put32(3); uint32_t vptr = o.put32(0);
    uint32_t verts = (uint32_t)o.b.size();
    float pos[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    if (nan_at) pos[4] = NAN;
    for (float p : pos) o.putf(p);
    uint32_t topo = o.put32(1); uint32_t gptr = o.put32(0); o.put32(3); uint32_t iptr = o.put32(0);
    uint32_t grp = o.put32(0); o.put32(0); o.put32(1);
    uint32_t idx = (uint32_t)o.b.size();
    o.put16(0); o.put16(1); o.put16(bad_index); o.align();
    sy.vertex_data_off = verts;
    sy.index_data_off = idx;
    o.ptr(root, S_OBJ, o.cstr("synthetic.max"));
    o.ptr(mptr, S_OBJ, marr);
    o.ptr(marr, S_OBJ, mesh);
    o.ptr(mesh, S_OBJ, o.cstr("Tri"));
    o.ptr(vdp, S_OBJ, vd);
    o.ptr(tpp, S_OBJ, topo);
    o.ptr(vd, S_TYPE, tV);
    o.ptr(vptr, S_OBJ, verts);
    o.ptr(gptr, S_OBJ, grp);
    o.ptr(iptr, S_OBJ, idx);
    sy.file = write_file(s, tR, root);
    return sy;
}

Err read_err(const Bytes& b) {
    FileInfo fi;
    Error e;
    if (read_gr2(b.data(), b.size(), nullptr, fi, &e)) return Err::None;
    std::printf("  (expected failure: %s: %s)\n", err_name(e.code), e.what.c_str());
    return e.code;
}

void synthetic() {
    Synth sy = make();
    FileInfo fi;
    Error e;
    CHECK(read_gr2(sy.file.data(), sy.file.size(), nullptr, fi, &e));
    CHECK_EQ(fi.from_file, std::string("synthetic.max"));
    CHECK_EQ(fi.meshes.size(), 1u);
    if (fi.meshes.size() == 1) {
        const Mesh& m = fi.meshes[0];
        CHECK_EQ(m.name, std::string("Tri"));
        CHECK_EQ(m.vertex_count, 3u);
        CHECK_EQ(m.format.stride, 12u);
        CHECK_EQ(m.format.key(), std::string("Real32:Position:3"));
        CHECK_EQ(m.indices.size(), 3u);
        CHECK_EQ(m.index_size, 2u);
        CHECK_EQ(m.groups.size(), 1u);
        float p[4];
        CHECK_EQ(m.read(1, "Position", p), 3u);
        CHECK(p[0] == 1.0f && p[1] == 0.0f);
    }
    GeometryReport rep;
    CHECK(check_geometry(fi, rep, &e));
    std::string sum = canonical_summary(fi, "synthetic");
    CHECK(sum.find("\"bbox\":[0,0,0,1,1,0]") != std::string::npos);
    CHECK(sum.find("\"fmt\":\"Real32:Position:3\"") != std::string::npos);

    // ---- negative tests, one per check
    { Bytes b(sy.file.begin(), sy.file.begin() + 40); CHECK_EQ(read_err(b), Err::Truncated); }
    { Bytes b(sy.file.begin(), sy.file.end() - 8); CHECK_EQ(read_err(b), Err::Truncated); }
    { Bytes b = sy.file; b[0] ^= 0xFF; CHECK_EQ(read_err(b), Err::BadMagic); }
    { Bytes b = sy.file; b[0x20] = 9; recrc(b); CHECK_EQ(read_err(b), Err::BadHeader); }
    { Bytes b = sy.file; b[b.size() - 1] ^= 1; CHECK_EQ(read_err(b), Err::BadCrc); }
    { Bytes b = sy.file; uint32_t big = 0x7FFFFFF; std::memcpy(&b[0x68 + 8], &big, 4); recrc(b); CHECK_EQ(read_err(b), Err::BadSectionTable); }
    { Bytes b = sy.file; uint32_t c = 1; std::memcpy(&b[0x68], &c, 4); recrc(b); CHECK_EQ(read_err(b), Err::UnsupportedCompression); }
    {   // a fix-up whose target lies outside the file's sections
        Bytes b = sy.file;
        uint32_t fix_off; std::memcpy(&fix_off, &b[0x68 + 28], 4);
        uint32_t bad = 0x100000; std::memcpy(&b[fix_off + 8], &bad, 4); recrc(b);
        CHECK_EQ(read_err(b), Err::FixupOutOfRange);
    }
    {   // a wrong fix-up: Root.Meshes points 4 bytes before the end of section 0, so the array read overruns
        Bytes b = sy.file;
        uint32_t fix_off, n, data_size; std::memcpy(&fix_off, &b[0x68 + 28], 4); std::memcpy(&n, &b[0x68 + 32], 4);
        std::memcpy(&data_size, &b[0x68 + 8], 4);
        for (uint32_t k = 0; k < n; ++k) {
            uint32_t src; std::memcpy(&src, &b[fix_off + 12 * k], 4);
            if (src == sy.meshes_ptr_slot) { uint32_t to = data_size - 2; std::memcpy(&b[fix_off + 12 * k + 8], &to, 4); }
        }
        recrc(b);
        CHECK_EQ(read_err(b), Err::ObjectOutOfRange);
    }
    {   // corrupt type tree: an unknown member type
        Bytes b = sy.file;
        uint32_t off1; std::memcpy(&off1, &b[0x68 + 44 + 4], 4);
        uint32_t bad = 99; std::memcpy(&b[off1 + sy.type_g_member0], &bad, 4); recrc(b);
        CHECK_EQ(read_err(b), Err::TypeTreeCorrupt);
    }
    {   // geometry: an index past the vertex count
        Synth bad = make(0, 7);
        FileInfo f2; CHECK(read_gr2(bad.file.data(), bad.file.size(), nullptr, f2, &e));
        GeometryReport r2; Error e2;
        CHECK(!check_geometry(f2, r2, &e2));
        CHECK_EQ(e2.code, Err::GeometryCheck);
        CHECK(!r2.violations.empty() && r2.violations[0].find("indices >= vertex count") != std::string::npos);
    }
    {   // geometry: a NaN position
        Synth bad = make(1);
        FileInfo f2; CHECK(read_gr2(bad.file.data(), bad.file.size(), nullptr, f2, &e));
        GeometryReport r2; Error e2;
        CHECK(!check_geometry(f2, r2, &e2));
        CHECK(!r2.violations.empty() && r2.violations[0].find("non-finite position") != std::string::npos);
        std::string s2 = canonical_summary(f2, "x");
        CHECK(s2.find("\"bbox\":[0,0,0,1,1,0]") != std::string::npos);   // NaN skipped per axis (y of vertex 1)
    }
    {   // geometry: vertex bytes that do not match count x stride (a hand-damaged structure)
        // (the per-vertex reads are skipped then: issue 9025; test_hostile covers the crash this once was)
        FileInfo f2 = fi; f2.meshes[0].vertices.resize(5);
        GeometryReport r2; CHECK(!check_geometry(f2, r2, nullptr));
        CHECK_EQ(r2.violations.size(), 1u);
        CHECK(!r2.violations.empty() && r2.violations[0].find("vertex bytes 5 != count x stride") != std::string::npos);
    }
    {   // string database: a model file is not a .gsd
        StringDatabase db; Error e2;
        CHECK(!db.add_gsd(sy.file.data(), sy.file.size(), &e2));
        CHECK_EQ(e2.code, Err::BadStringDatabase);
        CHECK_EQ(StringDatabase::hash(""), 0xFFFFFFFFu);
    }
}

void oodle_synthetic() {
    Bytes out;
    Error e;
    uint8_t tiny[10] = {0};
    CHECK(!oodle1_decompress(tiny, sizeof tiny, 0, 0, 100, out, &e));
    CHECK_EQ(e.code, Err::DecompressCorrupt);
    uint8_t z[64] = {0};
    CHECK(!oodle1_decompress(z, sizeof z, 50, 10, 100, out, &e));   // sub-stream ends out of order
    CHECK_EQ(e.code, Err::DecompressCorrupt);
    // random streams must fail cleanly or decode, never crash or overrun
    std::mt19937 rng(3100);
    int failed = 0;
    for (int k = 0; k < 300; ++k) {
        Bytes b(36 + rng() % 400);
        for (auto& x : b) x = (uint8_t)rng();
        uint32_t d = 1 + rng() % 5000;
        if (!oodle1_decompress(b.data(), b.size(), d / 3, d / 2, d, out, &e)) ++failed;
        else CHECK_EQ(out.size(), (size_t)d);
    }
    std::printf("random Oodle1 streams: %d of 300 refused\n", failed);
    CHECK(failed > 0);
}

// ------------------------------------------------------------------ real files
// every float of v equals the given bit pattern (exact; a NaN pattern compares as bits too)
void expect_bits(const float* v, std::initializer_list<uint32_t> want) {
    size_t i = 0;
    for (uint32_t w : want) {
        uint32_t got;
        std::memcpy(&got, &v[i], 4);
        CHECK_EQ(got, w);
        if (got != w) std::printf("  element %zu: 0x%08X, expected 0x%08X\n", i, got, w);
        ++i;
    }
}

bool load_db(const char* rel, StringDatabase& db) {
    Bytes b;
    if (!t::sample(rel, b)) return false;
    Error e;
    bool ok = db.add_gsd(b.data(), b.size(), &e);
    CHECK(ok);
    return ok;
}

void real() {
    StringDatabase units;
    Bytes w;
    if (!load_db("fpk/UnitModels/unitmodels_stringdatabase.gsd", units) || !t::sample("fpk/UnitModels/warrior.gr2", w)) return;
    CHECK(units.size() > 1000);
    FileInfo fi;
    Error e;
    CHECK(read_gr2(w.data(), w.size(), &units, fi, &e));
    CHECK_EQ(fi.meshes.size(), 1u);
    if (!fi.meshes.empty()) {
        CHECK_EQ(fi.meshes[0].vertex_count, 863u);   // landmarks-and-models.md 4.7
        CHECK_EQ(fi.meshes[0].format.stride, 68u);
        CHECK_EQ(fi.meshes[0].indices.size(), 674u * 3);
    }
    CHECK(!fi.skeletons.empty() && fi.skeletons[0].bones.size() == 23);
    if (!fi.skeletons.empty() && fi.skeletons[0].bones.size() > 2) {
        // bone transforms are read (a wrong member name once left them at identity; the canonical summary does
        // not carry them, so this test does): some bone has a non-zero translation and inverse-world offset
        float t = 0, iw = 0;
        for (const auto& b : fi.skeletons[0].bones) {
            t += std::fabs(b.local.translation[0]) + std::fabs(b.local.translation[1]) + std::fabs(b.local.translation[2]);
            iw += std::fabs(b.inverse_world[12]) + std::fabs(b.inverse_world[13]) + std::fabs(b.inverse_world[14]);
        }
        CHECK(t > 1.0f);
        CHECK(iw > 1.0f);
    }
    if (!fi.skeletons.empty() && fi.skeletons[0].bones.size() == 23) {
        // Exact values (bug 0093 V6, issue 9025): bone 1 'bone_CLUB' (the largest translation) and bone 3, every
        // float as its bit pattern, from one-off 9012's Python reader (research/05-graphics-composition/evidence/
        // 9025/ref_transforms.out.txt). A translation read from a wrong offset or a dropped field fails here.
        const Bone& b1 = fi.skeletons[0].bones[1];
        CHECK_EQ(b1.name, std::string("bone_CLUB"));
        CHECK_EQ(b1.parent, 0);
        CHECK_EQ(b1.local.flags, 3u);
        expect_bits(b1.local.translation, {0xC29B27E4, 0x3EF8E8E4, 0x42A73A49});
        expect_bits(b1.local.rotation, {0xBD2C2D00, 0x3E44E13B, 0xBCE76B46, 0x3F7AE396});
        expect_bits(b1.local.scale_shear, {0x3F800004, 0x32000000, 0x33000000, 0x31800000, 0x3F7FFFF8, 0x34580000,
                                           0x33000000, 0x34500000, 0x3F800002});
        expect_bits(b1.inverse_world + 12, {0x42CE2147, 0x4125AF56, 0xC23F01AA});
        const Bone& b3 = fi.skeletons[0].bones[3];
        CHECK_EQ(b3.name, std::string("Base HumanPelvis"));
        CHECK_EQ(b3.parent, 2);
        expect_bits(b3.local.translation, {0x3DEC3E00, 0x3FF783E4, 0xB3C9A300});
        expect_bits(b3.local.rotation, {0x33A36DA0, 0xB99EB3C1, 0x33970001, 0x3F7FFFFF});
        expect_bits(b3.inverse_world + 12, {0xC2770043, 0x402A7DBC, 0x3D191EFC});
    }
    CHECK_EQ(fi.unresolved_strings, 0u);
    GeometryReport rep;
    CHECK(check_geometry(fi, rep, &e));
    // without the database every string slot is unresolved
    FileInfo f0;
    CHECK(read_gr2(w.data(), w.size(), nullptr, f0, &e));
    CHECK(f0.unresolved_strings > 0);

    // corruption of a compressed section that keeps the CRC valid: must fail in decoding or in the object layer,
    // never crash; with the CRC left alone it must fail as BadCrc
    Header h;
    CHECK(parse_header(w.data(), w.size(), h, &e));
    const SectionHeader& s3 = h.sections[3];
    CHECK_EQ(s3.compression, 2u);
    {
        Bytes b = w; b[s3.data_offset + 40] ^= 0x5A;
        CHECK_EQ(read_err(b), Err::BadCrc);
    }
    int fails = 0, kinds[32] = {0};
    for (uint32_t k = 0; k < 40; ++k) {
        Bytes b = w;
        b[h.sections[0].data_offset + 37 + k * 7] ^= (uint8_t)(0x11 + k);
        recrc(b);
        FileInfo f2;
        Error e2;
        if (!read_gr2(b.data(), b.size(), &units, f2, &e2)) { ++fails; kinds[(int)e2.code]++; }
    }
    std::printf("40 corruptions of the warrior's object section (CRC fixed): %d refused (Decompress %d/%d, ObjectOutOfRange %d, TypeTree %d, Fixup %d)\n",
                fails, kinds[(int)Err::DecompressCorrupt], kinds[(int)Err::DecompressSize], kinds[(int)Err::ObjectOutOfRange],
                kinds[(int)Err::TypeTreeCorrupt], kinds[(int)Err::FixupOutOfRange]);
    CHECK(fails > 0);
    {   // a wrong expanded size for a real Oodle1 section
        Bytes b = w; uint32_t sz = s3.expanded_size + 64; std::memcpy(&b[0x68 + 44 * 3 + 12], &sz, 4); recrc(b);
        Err c = read_err(b);
        CHECK(c == Err::DecompressSize || c == Err::DecompressCorrupt);
    }

    StringDatabase wonders;
    Bytes st;
    if (load_db("fpk/WonderModels/wondermodels_stringdatabase.gsd", wonders) && t::sample("fpk/WonderModels/stonehenge.gr2", st)) {
        FileInfo f3;
        CHECK(read_gr2(st.data(), st.size(), &wonders, f3, &e));
        CHECK_EQ(f3.meshes.size(), 5u);
        bool shader = false;
        for (auto& m : f3.materials) shader |= m.shader_set == "BuildingShader";
        CHECK(shader);
    }
    StringDatabase imp;
    Bytes acad;
    if (load_db("fpk/ImprovementModels/improvementmodels_stringdatabase.gsd", imp) &&
        t::sample("fpk/ImprovementModels/anc_academy.gr2", acad)) {
        // InitialPlacement, exact (bug 0093 V6, issue 9025): the academy's models are placed by it (the
        // picture comparison of one-off 9021 showed the academy wrong when it is dropped); values as above
        FileInfo f6;
        CHECK(read_gr2(acad.data(), acad.size(), &imp, f6, &e));
        CHECK_EQ(f6.models.size(), 14u);
        if (f6.models.size() == 14) {
            const ModelEntry& m13 = f6.models[13];
            CHECK_EQ(m13.name, std::string("Anc_Academy"));
            CHECK_EQ(m13.initial_placement.flags, 1u);
            expect_bits(m13.initial_placement.translation, {0x3F9DC538, 0x41ABBEB4, 0x00000000});
            expect_bits(m13.initial_placement.rotation, {0, 0, 0, 0x3F800000});
            expect_bits(m13.initial_placement.scale_shear, {0x3F800000, 0, 0, 0, 0x3F800000, 0, 0, 0, 0x3F800000});
            CHECK_EQ(f6.models[1].name, std::string("Object12"));
            expect_bits(f6.models[1].initial_placement.translation, {0x4181CE92, 0x41AA8B4C, 0x00000000});
            expect_bits(f6.models[0].initial_placement.translation, {0x00000000, 0x00000000, 0x3E00CC00});
        }
    }
    Bytes deer;
    if (t::sample("fpk/MiscData/resource_deer.gr2", deer)) {   // MiscData keeps its strings inline
        FileInfo f4;
        CHECK(read_gr2(deer.data(), deer.size(), nullptr, f4, &e));
        uint32_t skinned = 0;
        for (auto& m : f4.meshes) skinned += m.format.find("BoneWeights") ? m.vertex_count : 0;
        CHECK_EQ(skinned, 2429u);   // 979 + 707 + 743 (landmarks-and-models.md 4.7)
    }
    Bytes idle;
    if (t::sample("fpk/UnitModels/warrior_idlea.gr2", idle)) {
        FileInfo f5;
        CHECK(read_gr2(idle.data(), idle.size(), &units, f5, &e));
        CHECK_EQ(f5.animations.size(), 1u);
        CHECK(!f5.track_groups.empty() && f5.track_groups[0].transform_tracks.size() == 23);
        if (!f5.animations.empty()) CHECK(std::fabs(f5.animations[0].duration - 6.0f) < 1e-3f);
    }
}

}  // namespace

int main() {
    synthetic();
    oodle_synthetic();
    real();
    return t::finish();
}
