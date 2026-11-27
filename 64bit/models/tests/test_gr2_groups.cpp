// test_gr2_groups.cpp - read_gr2 and an animation's track groups that the root list does not hold (epic 0041, issue
// 4353). VP's FBX clips (e.g. flagbearer_idle.gr2) leave the root TrackGroups list empty and reference their one group
// only from Animations[0].TrackGroups; the reader resolved groups through the root list only, so such a clip had no
// group and the pose step refused it (NoTrackGroup). Synthetic files built with tests/gr2_writer.h, no game data.
#include "c5models/anim.h"
#include "c5models/gr2.h"
#include "gr2_writer.h"
#include "testing.h"

#include <string>

using namespace c5m;
using namespace gr2w;

namespace {

// Root {ArrayOfReferences TrackGroups -> TG; ArrayOfReferences Animations -> Anim}
// TG {String Name; ReferenceToArray TransformTracks -> TT}, TT {String Name}
// Anim {String Name; Real32 Duration; ArrayOfReferences TrackGroups -> TG}
// root_lists_group: the root list holds the group (the stock form); anim_ref: 0 = the animation references the
// group, 1 = a null reference, 2 = no reference at all.
Bytes make(bool root_lists_group, int anim_ref) {
    std::vector<Sec> s(2);
    Sec& o = s[S_OBJ];
    Sec& t = s[S_TYPE];
    TypeWriter TT(t); TT.member(8, "Name"); uint32_t tTT = TT.end();
    TypeWriter TG(t); TG.member(8, "Name"); TG.member(3, "TransformTracks", tTT); uint32_t tTG = TG.end();
    TypeWriter A(t); A.member(8, "Name"); A.member(10, "Duration"); A.member(4, "TrackGroups", tTG); uint32_t tA = A.end();
    TypeWriter R(t); R.member(4, "TrackGroups", tTG); R.member(4, "Animations", tA); uint32_t tR = R.end();
    finish_names(t, {&TT, &TG, &A, &R});
    uint32_t root = o.put32(root_lists_group ? 1 : 0); uint32_t rtg = o.put32(0);
    o.put32(1); uint32_t ranim = o.put32(0);
    uint32_t rtg_arr = o.put32(0);
    uint32_t tg = o.put32(0); o.put32(2); uint32_t tts = o.put32(0);
    uint32_t tt0 = o.put32(0); uint32_t tt1 = o.put32(0);
    uint32_t anim_arr = o.put32(0);
    uint32_t anim = o.put32(0); o.putf(2.0f); o.put32(anim_ref == 2 ? 0 : 1); uint32_t atg = o.put32(0);
    uint32_t atg_arr = o.put32(0);
    if (root_lists_group) {
        o.ptr(rtg, S_OBJ, rtg_arr);
        o.ptr(rtg_arr, S_OBJ, tg);
    }
    o.ptr(ranim, S_OBJ, anim_arr);
    o.ptr(anim_arr, S_OBJ, anim);
    o.ptr(tg, S_OBJ, o.cstr("Root"));
    o.ptr(tts, S_OBJ, tt0);
    o.ptr(tt0, S_OBJ, o.cstr("Pelvis"));
    o.ptr(tt1, S_OBJ, o.cstr("Spine"));
    o.ptr(anim, S_OBJ, o.cstr("Default Take"));
    if (anim_ref != 2) o.ptr(atg, S_OBJ, atg_arr);
    if (anim_ref == 0) o.ptr(atg_arr, S_OBJ, tg);
    return write_file(s, tR, root);
}

bool read(const Bytes& b, FileInfo& fi) {
    Error e;
    bool ok = read_gr2(b.data(), b.size(), nullptr, fi, &e);
    if (!ok) std::printf("  read_gr2: %s\n", e.what.c_str());
    return ok;
}

void test_root_list() {   // the stock form: unchanged behaviour
    FileInfo fi;
    CHECK(read(make(true, 0), fi));
    CHECK_EQ(fi.track_groups.size(), (size_t)1);
    CHECK_EQ(fi.animation_only_track_groups, 0u);
    CHECK_EQ(fi.animations.size(), (size_t)1);
    CHECK(fi.animations[0].track_groups == std::vector<int32_t>(1, 0));
    CHECK(fi.track_groups[0].name == "Root" && fi.track_groups[0].transform_tracks.size() == 2);
    CHECK(canonical_summary(fi, "x").find("\"tgs\":[{\"name\":\"Root\"") != std::string::npos);
}

void test_animation_only() {   // VP's FBX form: the root list empty, the group reached through the animation
    FileInfo fi;
    CHECK(read(make(false, 0), fi));
    CHECK_EQ(fi.track_groups.size(), (size_t)1);
    CHECK_EQ(fi.animation_only_track_groups, 1u);
    CHECK(fi.animations[0].track_groups == std::vector<int32_t>(1, 0));
    CHECK(fi.animations[0].track_group_names == std::vector<std::string>(1, "Root"));
    CHECK(fi.track_groups[0].name == "Root");
    CHECK(fi.track_groups[0].transform_tracks.size() == 2 && fi.track_groups[0].transform_tracks[1].name == "Spine");
    // the canonical summary lists the root list only, as the Python reference does
    CHECK(canonical_summary(fi, "x").find("\"tgs\":[]") != std::string::npos);
    // the pose layer now finds the group (its tracks have no curves: MissingMember, not NoTrackGroup)
    Skeleton sk;
    Bone b;
    b.name = "Pelvis";
    sk.bones.push_back(b);
    BoundClip bc;
    AnimError e;
    CHECK(!bind_clip_all_groups(fi, 0, sk, bc, &e));
    CHECK(e.code == AnimErr::MissingMember);
}

void test_negative() {   // a null reference and no reference: still no group, the pose layer refuses
    for (int ref : {1, 2}) {
        FileInfo fi;
        CHECK(read(make(false, ref), fi));
        CHECK(fi.track_groups.empty());
        CHECK_EQ(fi.animation_only_track_groups, 0u);
        CHECK_EQ(fi.animations.size(), (size_t)1);
        if (ref == 1) CHECK(fi.animations[0].track_groups == std::vector<int32_t>(1, -1));
        else CHECK(fi.animations[0].track_groups.empty());
        Skeleton sk;
        BoundClip bc;
        AnimError e;
        CHECK(!bind_clip_all_groups(fi, 0, sk, bc, &e));
        CHECK(e.code == AnimErr::NoTrackGroup);
    }
}

}  // namespace

int main() {
    test_root_list();
    test_animation_only();
    test_negative();
    return t::finish();
}
