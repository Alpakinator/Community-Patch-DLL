// render/tests/unitanim/pose_fixture.h - issue 4409: the synthetic member model and clips of 0041's
// render/tests/units/test_pose.cpp (copied unchanged, lines "realArray" to "Loader"), for the pose-program tests.
// No game file (D83). Header-only, one test executable includes it.
#pragma once
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "c5r/units/pose.h"

namespace posefix {

using namespace c5r::units;

c5m::CurveArray realArray(const char* name, const std::vector<float>& v) {
    c5m::CurveArray a;
    a.name = name;
    a.type = c5m::MemberType::Real32;
    a.count = static_cast<uint32_t>(v.size());
    a.data.resize(v.size() * 4);
    if (!v.empty()) std::memcpy(a.data.data(), v.data(), v.size() * 4);
    return a;
}
c5m::Curve k32(uint32_t degree, const std::vector<float>& knots, const std::vector<float>& controls) {
    c5m::Curve c;
    c.present = true;
    c.format = 1;
    c.degree = degree;
    c.format_name = "DaK32fC32f";
    c.arrays.push_back(realArray("Knots", knots));
    c.arrays.push_back(realArray("Controls", controls));
    return c;
}
c5m::Curve identityCurve() {
    c5m::Curve c;
    c.present = true;
    c.format = 2;
    c.format_name = "DaIdentity";
    return c;
}

struct Quat { float x, y, z, w; };
Quat qmul(const Quat& a, const Quat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
Quat qnorm(Quat q) {
    float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}
const float kS = 0.70710678f;
// M: the y-up / back +z file basis -> the stock one, (x, y, z) -> (x, -z, y) = a rotation of +90 degrees about x.
const Quat kQM = {kS, 0, 0, kS};
const Quat kQMi = {-kS, 0, 0, kS};
// file-space value of a stock-space one: X_file = M^-1 X M
void toFileVec(const float v[3], float out[3]) {   // M^-1 (x, y, z) = (x, z, -y)
    out[0] = v[0];
    out[1] = v[2];
    out[2] = -v[1];
}
Quat toFileQuat(const Quat& q) { return qmul(qmul(kQMi, q), kQM); }

using c5m::Mat4;
Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k) r[i * 4 + j] += a[i * 4 + k] * b[k * 4 + j];
    return r;
}
Mat4 rigidInverse(const Mat4& m) {
    Mat4 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r[i * 4 + j] = m[j * 4 + i];
    for (int i = 0; i < 3; ++i) r[i * 4 + 3] = -(r[i * 4 + 0] * m[3] + r[i * 4 + 1] * m[7] + r[i * 4 + 2] * m[11]);
    r[15] = 1;
    return r;
}

struct BoneSpec { const char* name; int parent; Quat q; float t[3]; };
const BoneSpec kBones[] = {
    {"Root", -1, {0, 0, 0.2588190f, 0.9659258f}, {1, 2, 0}},
    {"Spine", 0, {0.1305262f, 0, 0, 0.9914449f}, {0, 0, 30}},
    {"Arm", 1, {0, 0.3826834f, 0, 0.9238795f}, {12, 0, 5}},
    {"Leg", 0, {0, 0, -0.0871557f, 0.9961947f}, {-4, 1, -8}},
};

// A skeleton (stock basis, or written in the y-up basis when `fileBasis`), its inverse world transforms consistent.
c5m::Skeleton makeSkeleton(const char* name, bool yup, float shift) {
    c5m::Skeleton sk;
    sk.name = name;
    std::vector<Mat4> world;
    for (const BoneSpec& bs : kBones) {
        c5m::Bone b;
        b.name = bs.name;
        b.parent = bs.parent;
        float t[3] = {bs.t[0] + shift, bs.t[1], bs.t[2]};
        Quat q = bs.q;
        // world in stock space for the inverse world transform
        const float ss[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        const float qa[4] = {q.x, q.y, q.z, q.w};
        Mat4 local = c5m::compose_transform(t, qa, ss);
        Mat4 w = bs.parent < 0 ? local : mul(world[static_cast<size_t>(bs.parent)], local);
        world.push_back(w);
        Mat4 iw = rigidInverse(w);
        if (yup) {   // file space: M^-1 X M
            float tf[3];
            toFileVec(t, tf);
            Quat qf = toFileQuat(q);
            std::memcpy(b.local.translation, tf, sizeof tf);
            b.local.rotation[0] = qf.x; b.local.rotation[1] = qf.y; b.local.rotation[2] = qf.z; b.local.rotation[3] = qf.w;
            const float mi[16] = {1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1};   // M^-1: (x,y,z) -> (x,z,-y)
            const float m[16] = {1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1};    // M: (x,y,z) -> (x,-z,y)
            Mat4 Mi, M;
            for (int k = 0; k < 16; ++k) { Mi[k] = mi[k]; M[k] = m[k]; }
            iw = mul(mul(Mi, iw), M);
        } else {
            std::memcpy(b.local.translation, t, sizeof t);
            b.local.rotation[0] = q.x; b.local.rotation[1] = q.y; b.local.rotation[2] = q.z; b.local.rotation[3] = q.w;
        }
        for (int r = 0; r < 4; ++r)   // stored for row vectors: the transpose
            for (int c = 0; c < 4; ++c) b.inverse_world[c * 4 + r] = iw[r * 4 + c];
        sk.bones.push_back(b);
    }
    return sk;
}

void setBasis(c5m::FileInfo& f, bool yup) {
    f.units_per_meter = yup ? 1.0f : 39.3700790f;
    float r[3] = {1, 0, 0}, u[3] = {0, 0, 1}, k[3] = {0, -1, 0};
    if (yup) { u[1] = 1; u[2] = 0; k[1] = 0; k[2] = 1; }
    std::memcpy(f.right, r, sizeof r);
    std::memcpy(f.up, u, sizeof u);
    std::memcpy(f.back, k, sizeof k);
}

c5m::Mesh makeMesh(const char* name, std::vector<const char*> bones) {
    c5m::Mesh m;
    m.name = name;
    for (const char* b : bones) {
        c5m::BoneBinding bb;
        bb.bone_name = b;
        m.bone_bindings.push_back(bb);
    }
    return m;
}

// The member model: skeleton 0 "Main" (meshes 0 Body: Root Spine Arm, 1 Legs: Leg Root) and skeleton 1 "Cart"
// (bones shifted 50 units; mesh 2 Cart: Spine Leg, of model entry 1), mesh 3 Prop without bindings.
c5m::FileInfo makeModel(bool yup) {
    c5m::FileInfo f;
    setBasis(f, yup);
    f.skeletons.push_back(makeSkeleton("Main", yup, 0));
    f.skeletons.push_back(makeSkeleton("Cart", yup, 50));
    f.meshes.push_back(makeMesh("Body", {"Root", "Spine", "Arm"}));
    f.meshes.push_back(makeMesh("Legs", {"Leg", "Root"}));
    f.meshes.push_back(makeMesh("Cart", {"Spine", "Leg"}));
    f.meshes.push_back(makeMesh("Prop", {}));
    c5m::ModelEntry a, b;
    a.name = "Main";
    a.skeleton = 0;
    a.meshes = {0, 1, 3};
    b.name = "Cart";
    b.skeleton = 1;
    b.meshes = {2};
    f.models = {a, b};
    return f;
}

// Keys of a clip: per bone with a track (Root, Spine, Arm; Leg keeps its rest), 3 knots, degree 1 (positions) and 2
// (orientations); in the stock basis or written in the y-up one. `front`: a one-track group "Point01" first.
c5m::FileInfo makeClip(const char* name, float duration, bool yup, bool front, uint32_t badFormatOn = 99) {
    c5m::FileInfo f;
    setBasis(f, yup);
    c5m::TrackGroup g;
    g.name = "Main";
    const char* names[3] = {"Root", "Spine", "Arm"};
    for (int i = 0; i < 3; ++i) {
        c5m::TransformTrack tt;
        tt.name = names[i];
        std::vector<float> knots = {0, duration * 0.4f, duration};
        std::vector<float> pos, rot;
        for (int k = 0; k < 3; ++k) {
            const float fk = static_cast<float>(k), fi = static_cast<float>(i);
            float p[3] = {kBones[i].t[0] + fk * 1.5f + fi, kBones[i].t[1] - fk * 0.7f, kBones[i].t[2] + fk * 2.0f * fi};
            Quat q = qnorm(qmul(kBones[i].q, Quat{0.05f * fk, -0.03f * fk * fi, 0.08f * fk, 1}));
            if (yup) {
                float pf[3];
                toFileVec(p, pf);
                std::memcpy(p, pf, sizeof p);
                q = toFileQuat(q);
            }
            pos.insert(pos.end(), p, p + 3);
            rot.insert(rot.end(), {q.x, q.y, q.z, q.w});
        }
        tt.position = k32(1, knots, pos);
        tt.orientation = k32(2, knots, rot);
        tt.scale_shear = identityCurve();
        if (static_cast<uint32_t>(i) == badFormatOn) tt.orientation.format = 13;
        g.transform_tracks.push_back(tt);
    }
    c5m::Animation a;
    a.name = name;
    a.duration = duration;
    if (front) {
        c5m::TrackGroup p;
        p.name = "Point01";
        c5m::TransformTrack tt = g.transform_tracks[0];
        tt.name = "Point01";
        tt.orientation.format = 13;   // undecodable, but no bone of any skeleton: never decoded
        p.transform_tracks.push_back(tt);
        f.track_groups.push_back(p);
        a.track_groups.push_back(0);
    }
    f.track_groups.push_back(g);
    a.track_groups.push_back(static_cast<int32_t>(f.track_groups.size() - 1));
    f.animations.push_back(a);
    return f;
}

std::vector<c5m::FxsAnimation> anims() {
    std::vector<c5m::FxsAnimation> v;
    const char* files[] = {"Unit_IdleA.gr2", "Units\\Unit_Fortify_Idle.gr2", "Unit_FidgetA.gr2", "unit_yup.gr2",
                           "Unit_Front.gr2", "Unit_Bad.gr2", "Unit_Missing.gr2", "Unit_Broken.gr2"};
    const int ecs[] = {1000, 1520, 1040, 3000, 3001, 3002, 3003, 3004};
    for (int i = 0; i < 8; ++i) {
        c5m::FxsAnimation a;
        a.file = files[i];
        a.ec = {ecs[i]};
        a.ec_text = std::to_string(ecs[i]);
        if (i == 2) a.name = "Fidget";
        v.push_back(a);
    }
    return v;
}

struct Loader {
    int calls = 0;
    ClipFileLoader fn() {
        return [this](const std::string& name, c5m::FileInfo& out, bool& missing, std::string& why) {
            ++calls;
            missing = false;
            if (name == "unit_idlea.gr2") out = makeClip("idleA", 6.0f, false, false);
            else if (name == "unit_fortify_idle.gr2") out = makeClip("fortify", 4.0f, false, false);
            else if (name == "unit_fidgeta.gr2") out = makeClip("fidget", 3.0f, false, false);
            else if (name == "unit_yup.gr2") out = makeClip("idleA", 6.0f, true, false);
            else if (name == "unit_front.gr2") out = makeClip("idleA", 6.0f, false, true);
            else if (name == "unit_bad.gr2") out = makeClip("bad", 2.0f, false, false, 1);
            else if (name == "unit_broken.gr2") { why = name + ": BadMagic: not a granny file"; return false; }
            else { missing = true; why = name + ": in no source"; return false; }
            return true;
        };
    }
};

}  // namespace posefix
