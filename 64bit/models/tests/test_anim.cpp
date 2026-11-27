// test_anim.cpp - the pose sampler (c5models/anim.h) against the Python reference on synthetic data
// (anim_reference.h, written by models/tools/posefit/make_anim_reference.py from curves.py and posefit.py), and its
// validation paths. Epic 0327, issue 4151. No game files needed.
#include "c5models/anim.h"

#include "anim_reference.h"
#include "testing.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace ref {
c5m::CurveArray make_array(const char* name, c5m::MemberType type, std::vector<double> v) {
    c5m::CurveArray a;
    a.name = name;
    a.type = type;
    a.count = (uint32_t)v.size();
    const uint32_t es = c5m::member_type_size(type);
    a.data.resize(v.size() * es);
    for (size_t i = 0; i < v.size(); ++i) {
        if (type == c5m::MemberType::Real32) {
            float f = (float)v[i];
            std::memcpy(&a.data[4 * i], &f, 4);
        } else if (type == c5m::MemberType::UInt16) {
            uint16_t u = (uint16_t)v[i];
            std::memcpy(&a.data[2 * i], &u, 2);
        } else {
            a.data[i] = (uint8_t)v[i];
        }
    }
    return a;
}
}  // namespace ref

namespace {

double g_max_curve_err = 0, g_max_skin_err = 0, g_max_pal_err = 0;

bool near(float a, float b, float tol_rel, float tol_abs) {
    return std::fabs(a - b) <= tol_abs + tol_rel * std::fabs(b);
}

void test_curves_equal_reference() {
    auto cases = ref::curve_cases();
    CHECK(cases.size() == 20);
    for (const auto& k : cases) {
        c5m::DecodedCurve d;
        c5m::AnimError e;
        bool ok = c5m::decode_curve(k.curve, k.slot, d, &e);
        CHECK(ok);
        if (!ok) {
            std::printf("  format %u: %s %s\n", k.curve.format, c5m::anim_err_name(e.code), e.what.c_str());
            continue;
        }
        CHECK(d.dim == c5m::slot_dim(k.slot));
        if (!k.knots.empty()) {
            CHECK(d.knots.size() == k.knots.size());
            for (size_t i = 0; i < k.knots.size() && i < d.knots.size(); ++i) CHECK(near(d.knots[i], k.knots[i], 1e-6f, 1e-7f));
        }
        const bool quat = k.slot == c5m::CurveSlot::Orientation;
        for (size_t i = 0; i < k.ts.size(); ++i) {
            float v[9];
            c5m::eval_curve(d, k.ts[i], quat, v);
            for (size_t j = 0; j < d.dim; ++j) {
                const double err = std::fabs(v[j] - k.values[i][j]);
                if (err > g_max_curve_err) g_max_curve_err = err;
                const bool good = near(v[j], k.values[i][j], 2e-6f, 2e-6f);
                CHECK(good);
                if (!good)
                    std::printf("  format %u degree %u t %.6f comp %zu: %.9g vs %.9g\n", k.curve.format, k.curve.degree,
                                k.ts[i], j, v[j], k.values[i][j]);
            }
        }
    }
}

void test_pose_equal_reference() {
    ref::PoseCase pc = ref::pose_case();
    c5m::BoundClip clip;
    c5m::AnimError e;
    CHECK(c5m::bind_clip(pc.anim, 0, pc.skel, clip, &e));
    CHECK(clip.unbound_tracks == 1);            // "Ghost"
    CHECK(clip.bone_track.size() == 5);
    CHECK(clip.bone_track[3] == -1);            // B1 has no track: rest pose
    CHECK(clip.duration == 2.0f);
    for (size_t ti = 0; ti < pc.ts.size(); ++ti) {
        std::vector<c5m::Mat4> local, world;
        std::vector<c5m::Row34> skin, pal;
        c5m::sample_local(clip, pc.skel, pc.ts[ti], local);
        CHECK(c5m::world_from_local(pc.skel, local, world, &e));
        CHECK(c5m::skin_rows(pc.skel, pc.mesh, world, skin, &e));
        CHECK(skin.size() == 4);
        c5m::palette_rows(skin, pc.scale, pc.yaw, pc.T, pal);
        for (size_t k = 0; k < skin.size() && k < pc.skin[ti].size(); ++k) {
            // float rounding scales with the matrix's largest element (synthetic translations reach 1e4)
            float big_s = 0, big_p = 0;
            for (int j = 0; j < 12; ++j) {
                big_s = std::fmax(big_s, std::fabs(pc.skin[ti][k][j]));
                big_p = std::fmax(big_p, std::fabs(pc.palette[ti][k][j]));
            }
            for (int j = 0; j < 12; ++j) {
                const double es = std::fabs(skin[k][j] - pc.skin[ti][k][j]);
                const double ep = std::fabs(pal[k][j] - pc.palette[ti][k][j]);
                if (es > g_max_skin_err) g_max_skin_err = es;
                if (ep > g_max_pal_err) g_max_pal_err = ep;
                // rotation parts are O(1), translations up to about 100 (skin) and 950 (palette)
                CHECK(near(skin[k][j], pc.skin[ti][k][j], 0.0f, 2e-5f + 1e-6f * big_s));
                CHECK(near(pal[k][j], pc.palette[ti][k][j], 0.0f, 2e-5f + 1e-6f * big_p));
            }
        }
    }
}

// a valid curve of each kind, for the negative tests to break
c5m::Curve d4n_curve() {
    c5m::Curve c;
    c.present = true;
    c.format = 8;
    c.degree = 2;
    c.format_name = "D4nK16uC15u";
    c.scalars.push_back({"ScaleOffsetTableEntries", {0.0f}});
    c.scalars.push_back({"OneOverKnotScale", {65535.0f}});
    c.arrays.push_back(ref::make_array("KnotsControls", c5m::MemberType::UInt16,
                                       {0, 0, 30000, 65535, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}));
    return c;
}
c5m::Curve k32_curve(std::vector<double> knots) {
    c5m::Curve c;
    c.present = true;
    c.format = 1;
    c.degree = 2;
    c.format_name = "DaK32fC32f";
    std::vector<double> ctrl(knots.size() * 3, 1.0);
    c.arrays.push_back(ref::make_array("Knots", c5m::MemberType::Real32, knots));
    c.arrays.push_back(ref::make_array("Controls", c5m::MemberType::Real32, ctrl));
    return c;
}

void expect_fail(const c5m::Curve& c, c5m::CurveSlot slot, c5m::AnimErr code) {
    c5m::DecodedCurve d;
    d.knots.assign(3, 1.0f);   // stale content that a failure must clear
    c5m::AnimError e;
    const bool ok = c5m::decode_curve(c, slot, d, &e);
    CHECK(!ok);
    CHECK(e.code == code);
    CHECK(!e.what.empty());
    CHECK(d.knots.empty() && d.controls.empty() && d.dim == 0);
    if (e.code != code) std::printf("  format %u: got %s\n", c.format, c5m::anim_err_name(e.code));
}

void test_negative_curves() {
    {   // the valid ones decode
        c5m::DecodedCurve d;
        CHECK(c5m::decode_curve(d4n_curve(), c5m::CurveSlot::Orientation, d, nullptr));
        CHECK(d.knots.size() == 4 && d.controls.size() == 16);
        CHECK(c5m::decode_curve(k32_curve({0, 0, 0.5, 1}), c5m::CurveSlot::Position, d, nullptr));
    }
    // unknown / unsupported formats: 0, 6, 7, 12-16, 19
    for (uint32_t f : {0u, 6u, 7u, 12u, 13u, 14u, 15u, 16u, 19u}) {
        c5m::Curve c = d4n_curve();
        c.format = f;
        expect_fail(c, c5m::CurveSlot::Orientation, c5m::AnimErr::UnsupportedFormat);
    }
    {   // array length mismatch: 15 words is not 4 per knot
        c5m::Curve c = d4n_curve();
        c.arrays[0] = ref::make_array("KnotsControls", c5m::MemberType::UInt16, std::vector<double>(15, 1.0));
        expect_fail(c, c5m::CurveSlot::Orientation, c5m::AnimErr::ArrayMismatch);
    }
    {   // controls not n x dim
        c5m::Curve c = k32_curve({0, 0, 0.5, 1});
        c.arrays[1] = ref::make_array("Controls", c5m::MemberType::Real32, std::vector<double>(13, 1.0));
        expect_fail(c, c5m::CurveSlot::Position, c5m::AnimErr::ArrayMismatch);
    }
    {   // wrong element type (bytes where words belong) and a missing scalar
        c5m::Curve c = d4n_curve();
        c.arrays[0].type = c5m::MemberType::UInt8;
        expect_fail(c, c5m::CurveSlot::Orientation, c5m::AnimErr::MissingMember);
        c5m::Curve c2 = d4n_curve();
        c2.scalars.pop_back();
        expect_fail(c2, c5m::CurveSlot::Orientation, c5m::AnimErr::MissingMember);
        c5m::Curve c3;
        expect_fail(c3, c5m::CurveSlot::Position, c5m::AnimErr::MissingMember);   // curve not present
    }
    // non-monotonic knots, a NaN knot
    expect_fail(k32_curve({0, 0, 0.5, 0.3}), c5m::CurveSlot::Position, c5m::AnimErr::KnotsNotMonotonic);
    expect_fail(k32_curve({0, 0, std::numeric_limits<double>::quiet_NaN(), 1}), c5m::CurveSlot::Position,
                c5m::AnimErr::KnotsNotMonotonic);
    {   // a NaN control
        c5m::Curve c = k32_curve({0, 0, 0.5, 1});
        c.arrays[1] = ref::make_array("Controls", c5m::MemberType::Real32,
                                      {1, 1, 1, 1, std::numeric_limits<double>::quiet_NaN(), 1, 1, 1, 1, 1, 1, 1});
        expect_fail(c, c5m::CurveSlot::Position, c5m::AnimErr::NonFinite);
    }
    {   // dimension: a position curve in the orientation slot, a 4-control constant in the scale/shear slot
        c5m::Curve c = k32_curve({0, 0, 0.5, 1});
        expect_fail(c, c5m::CurveSlot::Orientation, c5m::AnimErr::DimensionMismatch);
        c5m::Curve k;
        k.present = true;
        k.format = 3;
        k.format_name = "DaConstant32f";
        k.arrays.push_back(ref::make_array("Controls", c5m::MemberType::Real32, {0, 0, 0, 1}));
        expect_fail(k, c5m::CurveSlot::ScaleShear, c5m::AnimErr::DimensionMismatch);
        c5m::DecodedCurve d;
        CHECK(c5m::decode_curve(k, c5m::CurveSlot::Orientation, d, nullptr));   // the same curve is a quaternion
    }
    {   // degree 4
        c5m::Curve c = d4n_curve();
        c.degree = 4;
        expect_fail(c, c5m::CurveSlot::Orientation, c5m::AnimErr::BadDegree);
    }
}

void test_negative_pose() {
    ref::PoseCase pc = ref::pose_case();
    c5m::AnimError e;
    c5m::BoundClip clip;
    CHECK(c5m::bind_clip(pc.anim, 0, pc.skel, clip, &e));
    std::vector<c5m::Mat4> local, world;
    c5m::sample_local(clip, pc.skel, 0.5f, local);
    CHECK(c5m::world_from_local(pc.skel, local, world, &e));
    {   // unbound bone name
        c5m::Mesh m = pc.mesh;
        m.bone_bindings[2].bone_name = "Nope";
        std::vector<c5m::Row34> rows(3);
        c5m::AnimError e2;
        CHECK(!c5m::skin_rows(pc.skel, m, world, rows, &e2));
        CHECK(e2.code == c5m::AnimErr::UnboundBone);
        CHECK(rows.empty());
    }
    {   // parent after child, and a parent out of range
        c5m::Skeleton s = pc.skel;
        s.bones[1].parent = 3;
        std::vector<c5m::Mat4> w(2);
        c5m::AnimError e2;
        CHECK(!c5m::world_from_local(s, local, w, &e2));
        CHECK(e2.code == c5m::AnimErr::ParentOrder);
        CHECK(w.empty());
        s.bones[1].parent = 99;
        c5m::AnimError e3;
        CHECK(!c5m::world_from_local(s, local, w, &e3));
        CHECK(e3.code == c5m::AnimErr::ParentOrder);
    }
    {   // a track with an unsupported curve: the clip is refused as a whole
        c5m::FileInfo f = pc.anim;
        f.track_groups[0].transform_tracks[1].scale_shear.format = 13;
        c5m::BoundClip c2;
        c5m::AnimError e2;
        CHECK(!c5m::bind_clip(f, 0, pc.skel, c2, &e2));
        CHECK(e2.code == c5m::AnimErr::UnsupportedFormat);
        CHECK(c2.tracks.empty() && c2.bone_track.empty());
    }
    {   // no track group, no such animation
        c5m::FileInfo f = pc.anim;
        f.animations[0].track_groups.clear();
        c5m::BoundClip c2;
        c5m::AnimError e2;
        CHECK(!c5m::bind_clip(f, 0, pc.skel, c2, &e2));
        CHECK(e2.code == c5m::AnimErr::NoTrackGroup);
        c5m::AnimError e3;
        CHECK(!c5m::bind_clip(pc.anim, 5, pc.skel, c2, &e3));
        CHECK(e3.code == c5m::AnimErr::NoTrackGroup);
    }
}

void test_clip_time() {
    CHECK(c5m::clip_time(6.2340f, 6.0f, true) > 0.2339f && c5m::clip_time(6.2340f, 6.0f, true) < 0.2341f);
    CHECK(c5m::clip_time(-0.5f, 6.0f, true) > 5.49f && c5m::clip_time(-0.5f, 6.0f, true) < 5.51f);
    CHECK(c5m::clip_time(12.0f, 6.0f, true) == 0.0f);
    CHECK(c5m::clip_time(7.0f, 6.0f, false) == 6.0f);
    CHECK(c5m::clip_time(-1.0f, 6.0f, false) == 0.0f);
    CHECK(c5m::clip_time(1.0f, 0.0f, true) == 0.0f);
    CHECK(c5m::clip_time(std::numeric_limits<float>::quiet_NaN(), 6.0f, true) == 0.0f);
}

// bind_clip_all_groups (epic 0041, issue 4353): equal to bind_clip on a one-group clip; with a group before the
// skeleton's one (VP's hakkapelitta clips) the first track of a bone name wins; tracks naming no bone are counted and
// never decoded, so a format the sampler refuses there does not refuse the clip; a bound track's does.
bool same_local(const c5m::BoundClip& a, const c5m::BoundClip& b, const c5m::Skeleton& s, float t) {
    std::vector<c5m::Mat4> la, lb;
    c5m::sample_local(a, s, t, la);
    c5m::sample_local(b, s, t, lb);
    if (la.size() != lb.size()) return false;
    for (size_t i = 0; i < la.size(); ++i)
        for (int k = 0; k < 16; ++k)
            if (la[i][k] != lb[i][k]) return false;
    return true;
}

void test_all_groups() {
    ref::PoseCase pc = ref::pose_case();
    c5m::AnimError e;
    c5m::BoundClip one, all;
    CHECK(c5m::bind_clip(pc.anim, 0, pc.skel, one, &e));
    CHECK(c5m::bind_clip_all_groups(pc.anim, 0, pc.skel, all, &e));
    CHECK(all.unbound_tracks == 1);   // "Ghost"
    CHECK(all.bone_track == one.bone_track);
    CHECK(all.duration == one.duration && all.name == one.name);
    for (float t : {0.0f, 0.37f, 1.0f, 2.0f}) CHECK(same_local(one, all, pc.skel, t));

    // A group in front: "Point01" (no bone, broken format) and an "A1" track that must win over the main group's.
    c5m::FileInfo f = pc.anim;
    c5m::TrackGroup front;
    front.name = "Point01";
    c5m::TransformTrack p01 = f.track_groups[0].transform_tracks[0];
    p01.name = "Point01";
    p01.orientation.format = 13;      // refused by decode_curve, but on no bone of the skeleton
    front.transform_tracks.push_back(p01);
    c5m::TransformTrack a1 = f.track_groups[0].transform_tracks[0];   // Root's curves under A1's name
    a1.name = "A1";
    front.transform_tracks.push_back(a1);
    f.track_groups.push_back(front);
    f.animations[0].track_groups = {1, 0};
    c5m::BoundClip two;
    CHECK(c5m::bind_clip_all_groups(f, 0, pc.skel, two, &e));
    CHECK(two.unbound_tracks == 3);   // Point01, the main group's later A1, Ghost
    CHECK(two.bone_track[1] == 0);    // A1 bound first, from the front group
    std::vector<c5m::Mat4> lt, lr;
    c5m::sample_local(two, pc.skel, 0.5f, lt);
    c5m::sample_local(one, pc.skel, 0.5f, lr);
    CHECK(lt.size() == 5 && lr.size() == 5);
    if (lt.size() == 5 && lr.size() == 5) {
        bool a1_is_root = true, a2_same = true;
        for (int k = 0; k < 16; ++k) {
            a1_is_root = a1_is_root && lt[1][k] == lr[0][k];
            a2_same = a2_same && lt[2][k] == lr[2][k];
        }
        CHECK(a1_is_root);
        CHECK(a2_same);
    }
    // bind_clip (first group only) sees just the front group and refuses its format 13.
    c5m::BoundClip first;
    c5m::AnimError e1;
    CHECK(!c5m::bind_clip(f, 0, pc.skel, first, &e1));
    CHECK(e1.code == c5m::AnimErr::UnsupportedFormat);

    // Negative: a bound track's unsupported format refuses the clip as a whole, output cleared, message names it.
    c5m::FileInfo g = f;
    g.track_groups[0].transform_tracks[1].scale_shear.format = 14;   // the main group's A1 (unused: A1 is bound
    c5m::BoundClip ok2;                                               // from the front group), so still fine
    c5m::AnimError e2;
    CHECK(c5m::bind_clip_all_groups(g, 0, pc.skel, ok2, &e2));
    g.track_groups[0].transform_tracks[2].scale_shear.format = 14;   // the main group's A2: bound, refused
    c5m::BoundClip bad;
    CHECK(!c5m::bind_clip_all_groups(g, 0, pc.skel, bad, &e2));
    CHECK(e2.code == c5m::AnimErr::UnsupportedFormat);
    CHECK(e2.what.find("track A2") != std::string::npos);
    CHECK(bad.tracks.empty() && bad.bone_track.empty() && bad.name.empty());
    // Negative: no group at all, a group index out of range only, no such animation.
    c5m::FileInfo h = pc.anim;
    h.animations[0].track_groups.clear();
    c5m::AnimError e3;
    CHECK(!c5m::bind_clip_all_groups(h, 0, pc.skel, bad, &e3));
    CHECK(e3.code == c5m::AnimErr::NoTrackGroup);
    h.animations[0].track_groups = {7};
    c5m::AnimError e4;
    CHECK(!c5m::bind_clip_all_groups(h, 0, pc.skel, bad, &e4));
    CHECK(e4.code == c5m::AnimErr::NoTrackGroup);
    c5m::AnimError e5;
    CHECK(!c5m::bind_clip_all_groups(pc.anim, 3, pc.skel, bad, &e5));
    CHECK(e5.code == c5m::AnimErr::NoTrackGroup);
}

}  // namespace

int main() {
    test_curves_equal_reference();
    test_pose_equal_reference();
    test_negative_curves();
    test_negative_pose();
    test_clip_time();
    test_all_groups();
    std::printf("max |C++ - Python|: curves %.3g, skin rows %.3g, palette rows %.3g\n", g_max_curve_err, g_max_skin_err,
                g_max_pal_err);
    return t::finish();
}
