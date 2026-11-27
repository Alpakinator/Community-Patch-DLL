// test_logic_masks.cpp - the bones' track-mask weights (epic 0374, issue 4410; unit-controller.md 13.3), 2026-11-15.
// A .dge Trackmask names its mask by a binding name only; the weights per bone are in the model's skeleton, in each
// bone's ExtendedData.GrannyTrackMasks struct (one Real32 member per binding name). Synthetic part: a small file built
// here (no game data) with a bone of each shape the reader must handle. Real part (C5M_SAMPLES): the tank (AIM_DR
// weight 1, every other bone 0) and the German panzer (every bone 0).
#include "c5models/gr2.h"
#include "gr2_writer.h"
#include "testing.h"

#include <cmath>
#include <cstring>

using namespace c5m;

namespace {

using namespace gr2w;

// Root {ArrayOfReferences Skeletons -> Sk}; Sk {String Name; ReferenceToArray Bones -> Bone}
// Bone {String Name; Int32 ParentIndex; VariantReference ExtendedData}
// TM {Real32 TrackMaskAIM_LAYER; Real32 TrackMaskOther; Int32 Skip[2]}
// bone 0: ED {Reference GrannyTrackMasks -> TM} (the game's form, 13.3)
// bone 1: ED {Inline GrannyTrackMasks -> TM}, other values
// bone 2: ED {Real32 GrannyTrackMasks} (not a struct: not read)
// bone 3: NULL ExtendedData
// bone 4: ED {String Note} (no GrannyTrackMasks)
// bone 5: ED {Reference GrannyTrackMasks -> TM} with a NULL reference
Bytes make_file() {
    std::vector<Sec> s(2);
    Sec& o = s[S_OBJ];
    Sec& t = s[S_TYPE];
    TypeWriter TM(t); TM.member(10, "TrackMaskAIM_LAYER"); TM.member(10, "TrackMaskOther"); TM.member(19, "Skip", ~0u, 2);
    uint32_t tTM = TM.end();
    TypeWriter ED(t); ED.member(1, "GrannyTrackMasks", tTM); uint32_t tED = ED.end();
    TypeWriter EDr(t); EDr.member(2, "GrannyTrackMasks", tTM); uint32_t tEDr = EDr.end();
    TypeWriter EDf(t); EDf.member(10, "GrannyTrackMasks"); uint32_t tEDf = EDf.end();
    TypeWriter EDs(t); EDs.member(8, "Note"); uint32_t tEDs = EDs.end();
    TypeWriter B(t); B.member(8, "Name"); B.member(19, "ParentIndex"); B.member(5, "ExtendedData"); uint32_t tB = B.end();
    TypeWriter S(t); S.member(8, "Name"); S.member(3, "Bones", tB); uint32_t tS = S.end();
    TypeWriter R(t); R.member(4, "Skeletons", tS); uint32_t tR = R.end();
    finish_names(t, {&TM, &ED, &EDr, &EDf, &EDs, &B, &S, &R});

    uint32_t root = o.put32(1); uint32_t sptr = o.put32(0);
    uint32_t sarr = o.put32(0);
    uint32_t sk = o.put32(0); o.put32(6); uint32_t bptr = o.put32(0);
    uint32_t bones = (uint32_t)o.b.size();
    uint32_t name_slot[6], ed_type[6], ed_obj[6];
    for (int i = 0; i < 6; ++i) {
        name_slot[i] = o.put32(0);
        o.put32((uint32_t)(i - 1));
        ed_type[i] = o.put32(0);
        ed_obj[i] = o.put32(0);
    }
    // ExtendedData objects
    uint32_t tm0 = o.putf(1.0f); o.putf(0.25f); o.put32(7); o.put32(8);
    uint32_t ed0 = o.put32(0);   // Reference slot -> tm0
    uint32_t ed5 = o.put32(0);   // Reference slot left NULL
    uint32_t ed1 = o.putf(0.0f); o.putf(0.5f); o.put32(0); o.put32(0);
    uint32_t ed2 = o.putf(1.0f);
    uint32_t ed4 = o.put32(0);
    o.ptr(ed4, S_OBJ, o.cstr("not a mask"));
    o.ptr(ed0, S_OBJ, tm0);
    o.ptr(sptr, S_OBJ, sarr);
    o.ptr(sarr, S_OBJ, sk);
    o.ptr(sk, S_OBJ, o.cstr("point_WORLD"));
    o.ptr(bptr, S_OBJ, bones);
    const char* names[6] = {"AIM_DR", "turret", "flat", "plain", "noted", "nullref"};
    for (int i = 0; i < 6; ++i) o.ptr(name_slot[i], S_OBJ, o.cstr(names[i]));
    o.ptr(ed_type[0], S_TYPE, tEDr); o.ptr(ed_obj[0], S_OBJ, ed0);
    o.ptr(ed_type[1], S_TYPE, tED); o.ptr(ed_obj[1], S_OBJ, ed1);
    o.ptr(ed_type[2], S_TYPE, tEDf); o.ptr(ed_obj[2], S_OBJ, ed2);
    // bone 3: both slots left without a fix-up = NULL
    o.ptr(ed_type[4], S_TYPE, tEDs); o.ptr(ed_obj[4], S_OBJ, ed4);
    o.ptr(ed_type[5], S_TYPE, tEDr); o.ptr(ed_obj[5], S_OBJ, ed5);
    return write_file(s, tR, root);
}

void synthetic() {
    Bytes f = make_file();
    FileInfo fi;
    Error e;
    const bool ok = read_gr2(f.data(), f.size(), nullptr, fi, &e);
    if (!ok) std::printf("  read failed: %s: %s\n", err_name(e.code), e.what.c_str());
    CHECK(ok);
    CHECK_EQ(fi.skeletons.size(), 1u);
    if (fi.skeletons.size() != 1 || fi.skeletons[0].bones.size() != 6) {
        CHECK(false);
        return;
    }
    const auto& b = fi.skeletons[0].bones;
    CHECK_EQ(b[0].name, std::string("AIM_DR"));
    CHECK_EQ(b[0].track_masks.size(), 2u);   // the Int32[2] member is not a scalar weight
    float w = -1;
    CHECK(b[0].track_mask("TrackMaskAIM_LAYER", w) && w == 1.0f);
    CHECK(b[0].track_mask("TrackMaskOther", w) && w == 0.25f);
    CHECK(!b[0].track_mask("TrackMaskGlobalLayer", w));
    CHECK(!b[0].track_mask("Skip", w));
    CHECK(b[1].track_mask("TrackMaskAIM_LAYER", w) && w == 0.0f);
    CHECK(b[1].track_mask("TrackMaskOther", w) && w == 0.5f);
    CHECK(b[2].track_masks.empty());   // GrannyTrackMasks not a struct
    CHECK(b[3].track_masks.empty());   // NULL ExtendedData
    CHECK(b[4].track_masks.empty());   // ExtendedData without GrannyTrackMasks
    CHECK(b[5].track_masks.empty());   // a NULL reference
    CHECK_EQ(b[1].parent, 0);
}

bool load_db(const char* rel, StringDatabase& db) {
    Bytes b;
    if (!t::sample(rel, b)) return false;
    Error e;
    const bool ok = db.add_gsd(b.data(), b.size(), &e);
    CHECK(ok);
    return ok;
}

void real_model(const StringDatabase& db, const char* rel, const char* aim_bone, size_t bones) {
    Bytes g;
    if (!t::sample(rel, g)) return;
    FileInfo fi;
    Error e;
    CHECK(read_gr2(g.data(), g.size(), &db, fi, &e));
    CHECK_EQ(fi.skeletons.size(), 1u);
    if (fi.skeletons.empty()) return;
    const Skeleton& sk = fi.skeletons[0];
    CHECK_EQ(sk.bones.size(), bones);
    size_t with = 0, ones = 0;
    for (const Bone& b : sk.bones) {
        float w = -1;
        if (!b.track_mask("TrackMaskAIM_LAYER", w)) continue;
        ++with;
        CHECK(w == 0.0f || w == 1.0f);
        if (w == 1.0f) {
            ++ones;
            CHECK(aim_bone && b.name == aim_bone);
        }
        CHECK_EQ(b.track_masks.size(), 1u);
    }
    CHECK_EQ(with, sk.bones.size());
    CHECK_EQ(ones, aim_bone ? 1u : 0u);
    std::printf("  %s: %zu bones, %zu with TrackMaskAIM_LAYER, %zu at weight 1\n", rel, sk.bones.size(), with, ones);
}

void real() {
    StringDatabase units;
    if (!load_db("fpk/UnitModels/unitmodels_stringdatabase.gsd", units)) return;
    real_model(units, "fpk/UnitModels/tank.gr2", "AIM_DR", 14);
    real_model(units, "fpk/UnitModels/u_german_panzer.gr2", nullptr, 17);
    // a unit outside the aim machines carries no mask data
    Bytes w;
    if (!t::sample("fpk/UnitModels/warrior.gr2", w)) return;
    FileInfo fi;
    Error e;
    CHECK(read_gr2(w.data(), w.size(), &units, fi, &e));
    size_t with = 0;
    for (const auto& sk : fi.skeletons)
        for (const auto& b : sk.bones) with += b.track_masks.size();
    CHECK_EQ(with, 0u);
}

}  // namespace

int main() {
    synthetic();
    real();
    return t::finish();
}
