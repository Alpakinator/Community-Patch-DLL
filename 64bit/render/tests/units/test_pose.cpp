// render/tests/units/test_pose.cpp - issue 4353: the pose layer (c5r/units/pose.h) on synthetic data, no game file.
// Positive: equal to models' own pipeline; models and clips in another basis give the stock basis's rows; the
// skeleton of a mesh's model entry; every track group; clip look-up forms; the default clip; the clip cache; the
// module's PoseSource (96 rows, identity padding). Negative: one per status, each asserting code, message and output.
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "c5r/units/pose.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::units;

namespace {

// ---------------------------------------------------------------------------------------------- synthetic data
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

// models' own pipeline (first skeleton binding of a mesh given), for comparison
std::vector<c5m::Row34> direct(const c5m::FileInfo& model, size_t skel, const c5m::Mesh& mesh,
                               const c5m::FileInfo& clip, float t, float s, float yaw, const float T[3]) {
    c5m::BoundClip bc;
    c5m::AnimError e;
    if (!c5m::bind_clip_all_groups(clip, 0, model.skeletons[skel], bc, &e)) return {};
    std::vector<Mat4> local, world;
    c5m::sample_local(bc, model.skeletons[skel], c5m::clip_time(t, bc.duration, true), local);
    c5m::world_from_local(model.skeletons[skel], local, world, &e);
    std::vector<c5m::Row34> skin, pal;
    c5m::skin_rows(model.skeletons[skel], mesh, world, skin, &e);
    c5m::palette_rows(skin, s, yaw, T, pal);
    return pal;
}

float maxDiff(const std::vector<c5m::Row34>& a, const std::vector<c5m::Row34>& b) {
    if (a.size() != b.size()) return 1e30f;
    float d = 0;
    for (size_t k = 0; k < a.size(); ++k)
        for (int j = 0; j < 12; ++j) d = std::fmax(d, std::fabs(a[k][j] - b[k][j]));
    return d;
}

const float kT[3] = {939.5f, -342.25f, 12.0f};

PoseModel model(bool yup) {
    PoseModel pm;
    PoseStatus s = buildPoseModel(makeModel(yup), anims(), yup ? "yup.gr2" : "stock.gr2", pm);
    if (!s.ok()) test::fail(__FILE__, __LINE__, s.str());
    return pm;
}

void expectFail(const PoseStatus& s, PoseErr code, const char* fragment, const std::vector<MeshPalette>& out,
                int line) {
    if (s.code != code || s.msg.find(fragment) == std::string::npos)
        test::fail(__FILE__, line, std::string("want ") + poseErrName(code) + " with \"" + fragment + "\", got " +
                                       s.str());
    if (!out.empty()) test::fail(__FILE__, line, "output not empty on failure");
}

}  // namespace

C5R_TEST(equals_models_pipeline) {
    Loader ld;
    ClipCache cache(ld.fn());
    PoseModel pm = model(false);
    CHECK(pm.basis.identity);
    c5m::FileInfo f = makeModel(false), clip = makeClip("idleA", 6.0f, false, false);
    for (float t : {0.0f, 1.3f, 2.4f, 5.99f, 7.1f}) {   // 7.1 wraps to 1.1
        std::vector<MeshPalette> out;
        CHECK(poseFigure(cache, pm, "Unit_IdleA", t, true, 0.14f, 0.5235988f, kT, {}, out).ok());
        CHECK_EQ(out.size(), static_cast<size_t>(4));
        if (out.size() != 4) continue;
        CHECK(maxDiff(out[0].rows, direct(f, 0, f.meshes[0], clip, t, 0.14f, 0.5235988f, kT)) <= 1e-4f);
        CHECK(maxDiff(out[1].rows, direct(f, 0, f.meshes[1], clip, t, 0.14f, 0.5235988f, kT)) <= 1e-4f);
        CHECK(maxDiff(out[2].rows, direct(f, 1, f.meshes[2], clip, t, 0.14f, 0.5235988f, kT)) <= 1e-4f);
        CHECK(out[3].rows.empty());   // Prop: no bindings
        CHECK_EQ(out[2].mesh, 2u);
    }
    CHECK_EQ(cache.stats().binds, static_cast<size_t>(2));   // skeletons 0 and 1, bound once each
    CHECK(cache.stats().hits >= 8u);
    CHECK(cache.stats().bytes > 0u);
    CHECK_EQ(ld.calls, 2);
}

C5R_TEST(other_basis_gives_stock_rows) {
    Loader ld;
    ClipCache cache(ld.fn());
    PoseModel stock = model(false), yup = model(true);
    CHECK(!yup.basis.identity);
    // M = (x, y, z) -> (x, -z, y): rows of the map
    const float want[9] = {1, 0, 0, 0, 0, -1, 0, 1, 0};
    for (int i = 0; i < 9; ++i) CHECK(std::fabs(yup.basis.m[i] - want[i]) < 1e-6f);
    for (float t : {0.0f, 0.9f, 3.3f}) {
        std::vector<MeshPalette> a, b, c, d;
        CHECK(poseFigure(cache, stock, "unit_idlea.gr2", t, true, 0.12f, 1.1f, kT, {0, 1, 2}, a).ok());
        CHECK(poseFigure(cache, yup, "unit_idlea.gr2", t, true, 0.12f, 1.1f, kT, {0, 1, 2}, b).ok());   // model y up
        CHECK(poseFigure(cache, stock, "UNIT_YUP", t, true, 0.12f, 1.1f, kT, {0, 1, 2}, c).ok());       // clip y up
        CHECK(poseFigure(cache, yup, "unit_yup", t, true, 0.12f, 1.1f, kT, {0, 1, 2}, d).ok());         // both
        if (a.size() != 3 || b.size() != 3 || c.size() != 3 || d.size() != 3) {
            CHECK(false);
            continue;
        }
        for (int m = 0; m < 3; ++m) {
            CHECK(maxDiff(a[m].rows, b[m].rows) <= 2e-4f);
            CHECK(maxDiff(a[m].rows, c[m].rows) <= 2e-4f);
            CHECK(maxDiff(a[m].rows, d[m].rows) <= 2e-4f);
        }
    }
}

C5R_TEST(front_group_and_lookup_forms) {
    Loader ld;
    ClipCache cache(ld.fn());
    PoseModel pm = model(false);
    std::vector<MeshPalette> a, b;
    CHECK(poseFigure(cache, pm, "unit_idlea", 2.0f, true, 0.14f, 0, kT, {0}, a).ok());
    CHECK(poseFigure(cache, pm, "Unit_Front.gr2", 2.0f, true, 0.14f, 0, kT, {0}, b).ok());
    CHECK(maxDiff(a[0].rows, b[0].rows) == 0.0f);
    // look-up: file with and without .gr2 and directory, case, the name attribute, ec codes
    CHECK_EQ(findAnimation(pm, "UNIT_IDLEA.GR2"), 0);
    CHECK_EQ(findAnimation(pm, "unit_fortify_idle"), 1);
    CHECK_EQ(findAnimation(pm, "Assets/X/Unit_Fortify_Idle.gr2"), 1);
    CHECK_EQ(findAnimation(pm, "fidget"), 2);
    CHECK_EQ(findAnimation(pm, "ec:1040"), 2);
    CHECK_EQ(findAnimation(pm, "3000"), 3);
    CHECK_EQ(findAnimation(pm, "ec:9999"), -1);
    CHECK_EQ(findAnimation(pm, ""), -1);
    CHECK_EQ(findAnimation(pm, "Unit_Run"), -1);
    CHECK(defaultClip(pm, false) == "Unit_IdleA.gr2");
    CHECK(defaultClip(pm, true) == "Units\\Unit_Fortify_Idle.gr2");
    PoseModel none = pm;
    none.animations.resize(1);
    none.animations[0].ec = {2000};
    CHECK(defaultClip(none, true).empty());
    // loop and clamp
    std::vector<MeshPalette> w, c, e;
    CHECK(poseFigure(cache, pm, "unit_idlea", 8.0f, true, 0.14f, 0, kT, {0}, w).ok());
    CHECK(poseFigure(cache, pm, "unit_idlea", 2.0f, true, 0.14f, 0, kT, {0}, c).ok());
    CHECK(maxDiff(w[0].rows, c[0].rows) <= 1e-5f);
    CHECK(poseFigure(cache, pm, "unit_idlea", 8.0f, false, 0.14f, 0, kT, {0}, e).ok());
    std::vector<MeshPalette> end;
    CHECK(poseFigure(cache, pm, "unit_idlea", 6.0f, false, 0.14f, 0, kT, {0}, end).ok());
    CHECK(maxDiff(e[0].rows, end[0].rows) == 0.0f);
}

C5R_TEST(module_pose_source) {
    Loader ld;
    ModulePose pose(ld.fn());
    MemberArt member;
    member.member = "U_TEST";
    member.scale = 0.14f;
    member.files.fxsxml.name = "Unit.fxsxml";
    for (const c5m::FxsAnimation& a : anims()) {
        c5m::ResolvedAnimation ra;
        ra.file.name = a.file;
        ra.ec = a.ec;
        ra.ec_text = a.ec_text;
        ra.name = a.name;
        member.files.animations.push_back(ra);
    }
    UnitModel um;
    um.gr2 = "unit.gr2";
    um.file.reset(new c5m::FileInfo(makeModel(false)));
    ModelMesh mesh;
    mesh.name = "Legs";
    mesh.index = 1;
    UnitInput unit;
    unit.flags = kFlagFortified;
    PoseRequest req;
    req.unit = &unit;
    req.member = &member;
    req.model = &um;
    req.mesh = &mesh;
    req.clip = "";   // default: fortified -> the fortify idle
    req.t = 1.25f;
    req.scale = 0.14f;
    req.yaw = 0.5235988f;
    std::memcpy(req.T, kT, sizeof kT);
    Float4 rows[kPaletteBones * 3];
    UnitStatus s = pose.rows(req, rows);
    // UnitStatus::text, not str(): unitCodeName is 4352's (status.cpp), not linked into this branch's library yet
    if (!s.ok()) test::fail(__FILE__, __LINE__, s.text);
    c5m::FileInfo f = makeModel(false);
    std::vector<c5m::Row34> want = direct(f, 0, f.meshes[1], makeClip("fortify", 4.0f, false, false), 1.25f, 0.14f,
                                          0.5235988f, kT);
    CHECK_EQ(want.size(), static_cast<size_t>(2));
    float d = 0;
    for (size_t k = 0; k < want.size(); ++k)
        for (int r = 0; r < 3; ++r) {
            const Float4& o = rows[k * 3 + static_cast<size_t>(r)];
            d = std::fmax(d, std::fabs(o.x - want[k][r * 4 + 0]));
            d = std::fmax(d, std::fabs(o.y - want[k][r * 4 + 1]));
            d = std::fmax(d, std::fabs(o.z - want[k][r * 4 + 2]));
            d = std::fmax(d, std::fabs(o.w - want[k][r * 4 + 3]));
        }
    CHECK(d <= 1e-4f);
    for (size_t i = 6; i < kPaletteBones * 3; ++i) {   // slots 2..31 identity
        const Float4& o = rows[i];
        const float ex = (i % 3 == 0) ? 1.0f : 0.0f, ey = (i % 3 == 1) ? 1.0f : 0.0f, ez = (i % 3 == 2) ? 1.0f : 0.0f;
        CHECK(o.x == ex && o.y == ey && o.z == ez && o.w == 0.0f);
    }
    CHECK_EQ(pose.models(), static_cast<size_t>(1));
    CHECK_EQ(pose.samples(), static_cast<size_t>(1));
    // bindAll: every clip of the member against skeletons 0 and 1; the bad, missing and broken clips refuse it
    UnitStatus all = pose.bindAll(member, um);
    CHECK(all.code == UnitCode::CurveUnsupported);
    member.files.animations.resize(5);
    MemberArt m2 = member;
    m2.files.fxsxml.name = "Unit2.fxsxml";
    CHECK(pose.bindAll(m2, um).ok());
    CHECK_EQ(pose.cache().stats().failed_entries, static_cast<size_t>(1));
}

// ---------------------------------------------------------------------------------------------- 4466: the memo

// ModulePose (since 4466: the default clip found once per model, the clip binding and the model by the last request,
// each skeleton sampled once per figure through a ring of world transforms) against the pre-4466 per-mesh path:
// poseFigure / poseFigureProgram for that one mesh with their own cache and model (defaultClip by name). Rows are
// compared byte for byte (memcmp of the 96 Float4), statuses by code and message.
namespace {

const char* kMemoKey = "unit.gr2|unit.fxsxml";

MemberArt memoMember(const std::string& fxs, const std::vector<c5m::FxsAnimation>& list) {
    MemberArt member;
    member.member = "U_TEST";
    member.scale = 0.14f;
    member.files.fxsxml.name = fxs;
    for (const c5m::FxsAnimation& a : list) {
        c5m::ResolvedAnimation ra;
        ra.file.name = a.file;
        ra.ec = a.ec;
        ra.ec_text = a.ec_text;
        ra.name = a.name;
        member.files.animations.push_back(ra);
    }
    return member;
}

UnitModel memoModel(const char* gr2, bool yup) {
    UnitModel um;
    um.gr2 = gr2;
    um.file.reset(new c5m::FileInfo(makeModel(yup)));
    for (uint32_t i = 0; i < um.file->meshes.size(); ++i) {
        ModelMesh mm;
        mm.name = um.file->meshes[i].name;
        mm.index = i;
        um.meshes.push_back(mm);
    }
    return um;
}

void identity96(Float4 out[kPaletteBones * 3]) {
    for (uint32_t i = 0; i < kPaletteBones * 3; ++i) {
        out[i] = Float4();
        if (i % 3 == 0) out[i].x = 1;
        else if (i % 3 == 1) out[i].y = 1;
        else out[i].z = 1;
    }
}

void to96(const std::vector<MeshPalette>& mp, Float4 out[kPaletteBones * 3]) {
    identity96(out);
    if (mp.empty()) return;
    const std::vector<c5m::Row34>& rows = mp[0].rows;
    for (size_t k = 0; k < rows.size() && k < kPaletteBones; ++k)
        for (int r = 0; r < 3; ++r) {
            Float4& o = out[k * 3 + static_cast<size_t>(r)];
            o.x = rows[k][static_cast<size_t>(r) * 4 + 0];
            o.y = rows[k][static_cast<size_t>(r) * 4 + 1];
            o.z = rows[k][static_cast<size_t>(r) * 4 + 2];
            o.w = rows[k][static_cast<size_t>(r) * 4 + 3];
        }
}

// The pre-4466 ModulePose::rows: defaultClip by name, poseFigure for the one mesh.
PoseStatus refRows(ClipCache& cache, const PoseModel& pm, const PoseRequest& req, Float4 out[kPaletteBones * 3]) {
    identity96(out);
    std::string clip = req.clip;
    if (clip.empty()) {
        const bool fortified = req.unit && (req.unit->flags & kFlagFortified);
        clip = defaultClip(pm, fortified);
        if (clip.empty()) {
            PoseStatus s;
            s.code = PoseErr::ClipNotFound;
            s.msg = pm.key + ": no default clip (ec 1000" + std::string(fortified ? " or 1520" : "") + ")";
            return s;
        }
    }
    std::vector<MeshPalette> mp;
    PoseStatus st = poseFigure(cache, pm, clip, req.t, true, req.scale, req.yaw, req.T,
                               std::vector<uint32_t>(1, req.mesh->index), mp);
    if (st.ok()) to96(mp, out);
    return st;
}

PoseStatus refProgram(ClipCache& cache, const PoseModel& pm, const PoseRequest& req, const unitanim::PoseProgram& prog,
                      Float4 out[kPaletteBones * 3]) {
    identity96(out);
    std::vector<MeshPalette> mp;
    PoseStatus st = poseFigureProgram(cache, pm, prog, req.scale, req.yaw, req.T,
                                      std::vector<uint32_t>(1, req.mesh->index), mp);
    if (st.ok()) to96(mp, out);
    return st;
}

bool same96(const Float4* a, const Float4* b) { return std::memcmp(a, b, sizeof(Float4) * kPaletteBones * 3) == 0; }

// One request through the module and the reference: same code, same message, same bytes.
void expectSame(const UnitStatus& got, const PoseStatus& want, const Float4* a, const Float4* b, const std::string& at,
                int line) {
    if (got.code != poseErrUnitCode(want.code) || got.text != want.msg)
        test::fail(__FILE__, line, at + ": module " + got.text + " / reference " + want.str());
    if (!same96(a, b)) test::fail(__FILE__, line, at + ": rows differ");
}

unitanim::PoseProgram sampleProgram(const char* clip, float t) {
    unitanim::PoseProgram p;
    unitanim::PoseNode n;
    n.clip = clip;
    n.t = t;
    p.nodes.push_back(n);
    return p;
}

unitanim::PoseProgram blendProgram(float ta, float tb, float w, bool mask) {
    unitanim::PoseProgram p;
    unitanim::PoseNode a, b, c;
    a.clip = "Unit_IdleA.gr2";
    a.t = ta;
    b.clip = "Fidget";
    b.t = tb;
    c.kind = unitanim::PoseNode::Blend;
    c.a = 0;
    c.b = 1;
    c.w = w;
    if (mask) {
        unitanim::TrackMaskWeights m;
        m.default_weight = 0.25f;
        m.bones = {"Spine", "Arm"};
        m.weights = {1.0f, 0.5f};
        p.masks.push_back(m);
        c.mask = 0;
    }
    p.nodes = {a, b, c};
    return p;
}

}  // namespace

C5R_TEST(memo_rows_byte_equal) {
    Loader lm, lr;
    ModulePose pose(lm.fn());
    ClipCache refCache(lr.fn());
    MemberArt member = memoMember("Unit.fxsxml", anims());
    UnitModel um = memoModel("unit.gr2", false), uy = memoModel("yup.gr2", true);
    PoseModel ref, refY;
    CHECK(buildPoseModel(makeModel(false), anims(), kMemoKey, ref).ok());
    CHECK(buildPoseModel(makeModel(true), anims(), "yup.gr2|unit.fxsxml", refY).ok());
    CHECK_EQ(ref.idle, 0);
    CHECK_EQ(ref.fortify, 1);
    CHECK_EQ(defaultClipIndex(ref, false), 0);
    CHECK_EQ(defaultClipIndex(ref, true), 1);
    const char* clips[] = {"", "Fidget", "unit_idlea", "ec:1040", "Unit_Front.gr2"};
    size_t requests = 0;
    Float4 a[kPaletteBones * 3], b[kPaletteBones * 3];
    // frames x figures x meshes, as UnitRenderer asks: every mesh of a figure in a row, figures of one unit at shared
    // and at different times, two models alternating (the last-pair memo switches), the fortified default
    for (int frame = 0; frame < 3; ++frame)
        for (int fig = 0; fig < 10; ++fig) {
            UnitInput unit;
            unit.flags = static_cast<uint8_t>((fig % 3 == 1) ? kFlagFortified : 0u);
            const bool y = fig % 4 == 3;
            PoseRequest req;
            req.unit = &unit;
            req.member = &member;
            req.model = y ? &uy : &um;
            req.figure = static_cast<uint32_t>(fig);
            req.clip = clips[(fig + frame) % 5];
            req.t = (fig % 2 == 0) ? 0.37f * frame : 0.37f * frame + 0.11f * fig;   // even figures share a time
            req.scale = 0.14f;
            req.yaw = 0.3f * fig - 1.0f;
            req.T[0] = kT[0] + 12.0f * fig;
            req.T[1] = kT[1] - 3.0f * frame;
            req.T[2] = kT[2];
            for (const ModelMesh& mesh : req.model->meshes) {
                req.mesh = &mesh;
                const UnitStatus got = pose.rows(req, a);
                const PoseStatus want = refRows(refCache, y ? refY : ref, req, b);
                expectSame(got, want, a, b, "rows frame " + std::to_string(frame) + " figure " + std::to_string(fig) +
                                                " mesh " + mesh.name, __LINE__);
                CHECK(got.ok());
                ++requests;
            }
            // the controller's programs: one Sample (clamped) and blends with and without a mask
            const unitanim::PoseProgram progs[] = {sampleProgram("Unit_FidgetA", 0.5f * fig + 0.2f * frame),
                                                   blendProgram(0.2f * frame, 0.1f * fig, 0.3f, false),
                                                   blendProgram(0.2f * frame, 0.1f * fig, 0.7f, true)};
            for (const unitanim::PoseProgram& prog : progs)
                for (const ModelMesh& mesh : req.model->meshes) {
                    req.mesh = &mesh;
                    const UnitStatus got = pose.rowsProgram(req, prog, a);
                    const PoseStatus want = refProgram(refCache, y ? refY : ref, req, prog, b);
                    expectSame(got, want, a, b, "program frame " + std::to_string(frame) + " figure " +
                                                    std::to_string(fig) + " mesh " + mesh.name, __LINE__);
                    CHECK(got.ok());
                    ++requests;
                }
        }
    CHECK_EQ(requests, static_cast<size_t>(3 * 10 * 4 * 4));
    CHECK_EQ(pose.samples(), requests);
    CHECK_EQ(pose.models(), static_cast<size_t>(2));
    // sampled at most once per figure and skeleton for each pose (two skeletons, three programs); the shared times hit
    // across figures; meshes without bindings (Prop) never sample
    CHECK(pose.worldSamples() > 0u);
    CHECK(pose.worldSamples() <= static_cast<size_t>(3 * 10 * 4 * 2));
    CHECK(pose.worldHits() >= static_cast<size_t>(3 * 10 * 4));   // Body and Legs share skeleton 0: one hit each
    CHECK_EQ(pose.worldHits() + pose.worldSamples(), static_cast<size_t>(3 * 10 * 4 * 3));   // Body, Legs, Cart
    CHECK_EQ(lm.calls, lr.calls);   // the same clip files read, once each
}

C5R_TEST(memo_errors_as_before) {
    Loader lm, lr;
    ModulePose pose(lm.fn());
    ClipCache refCache(lr.fn());
    MemberArt member = memoMember("Unit.fxsxml", anims());
    UnitModel um = memoModel("unit.gr2", false);
    PoseModel ref;
    CHECK(buildPoseModel(makeModel(false), anims(), kMemoKey, ref).ok());
    UnitInput unit;
    PoseRequest req;
    req.unit = &unit;
    req.member = &member;
    req.model = &um;
    req.mesh = &um.meshes[0];
    req.scale = 0.14f;
    std::memcpy(req.T, kT, sizeof kT);
    Float4 a[kPaletteBones * 3], b[kPaletteBones * 3], id[kPaletteBones * 3];
    identity96(id);
    auto both = [&](int line, UnitCode code, const char* fragment) {
        const UnitStatus got = pose.rows(req, a);
        const PoseStatus want = refRows(refCache, ref, req, b);
        expectSame(got, want, a, b, std::string("rows ") + fragment, line);
        if (got.code != code || got.text.find(fragment) == std::string::npos)
            test::fail(__FILE__, line, std::string("want ") + fragment + ", got " + got.text);
        if (code != UnitCode::Ok && !same96(a, id)) test::fail(__FILE__, line, "rows not identity on failure");
    };
    // a good request first, so that every failure below runs with a warm memo
    both(__LINE__, UnitCode::Ok, "");
    // a mesh index out of range
    ModelMesh bad;
    bad.name = "Ghost";
    bad.index = 7;
    req.mesh = &bad;
    both(__LINE__, UnitCode::BadInput, "mesh 7 of 4");
    // non-finite t, yaw, T
    req.mesh = &um.meshes[0];
    req.t = std::nanf("");
    both(__LINE__, UnitCode::BadInput, "not finite");
    req.t = 0;
    req.yaw = INFINITY;
    both(__LINE__, UnitCode::BadInput, "not finite");
    req.yaw = 0;
    req.T[1] = -INFINITY;
    both(__LINE__, UnitCode::BadInput, "not finite");
    std::memcpy(req.T, kT, sizeof kT);
    // a clip the .fxsxml does not name: refused on a skinned mesh, not asked on a mesh without bindings (as before)
    req.clip = "Unit_Run";
    both(__LINE__, UnitCode::ClipNotFound, "names no clip \"Unit_Run\"");
    both(__LINE__, UnitCode::ClipNotFound, "names no clip \"Unit_Run\"");   // not memoized: asked again, same answer
    req.mesh = &um.meshes[3];
    both(__LINE__, UnitCode::Ok, "");
    // a clip that fails to bind: the failed binding is kept, the second ask gives the same answer without a read
    req.mesh = &um.meshes[0];
    req.clip = "Unit_Bad";
    both(__LINE__, UnitCode::CurveUnsupported, "track Spine");
    const int reads = lm.calls;
    both(__LINE__, UnitCode::CurveUnsupported, "track Spine");
    CHECK_EQ(lm.calls, reads);
    req.clip = "Unit_Missing";
    both(__LINE__, UnitCode::ModelNotFound, "in no source");
    req.clip = "Unit_Broken";
    both(__LINE__, UnitCode::BadModel, "BadMagic");
    // a mesh binding a bone no skeleton has
    req.clip = "";
    {
        UnitModel tail = memoModel("tail.gr2", false);
        tail.file->meshes[1].bone_bindings[0].bone_name = "Tail";
        PoseModel rt;
        CHECK(buildPoseModel(*tail.file, anims(), "tail.gr2|unit.fxsxml", rt).ok());
        req.model = &tail;
        req.mesh = &tail.meshes[1];
        const UnitStatus got = pose.rows(req, a);
        const PoseStatus want = refRows(refCache, rt, req, b);
        expectSame(got, want, a, b, "unresolved mesh", __LINE__);
        CHECK(got.code == UnitCode::BadModel && got.text.find("mesh Legs binds bones") != std::string::npos);
        req.model = &um;
        req.mesh = &um.meshes[0];
    }
    // after the failures, a good request is still byte-equal
    req.t = 2.5f;
    both(__LINE__, UnitCode::Ok, "");
    // no default clip: a member with neither ec 1000 nor 1520; fortified without 1520 falls back to ec 1000
    std::vector<c5m::FxsAnimation> noIdle = anims();
    noIdle[0].ec = {2000};
    noIdle[1].ec = {2001};
    MemberArt m2 = memoMember("NoIdle.fxsxml", noIdle);
    PoseModel ref2;
    CHECK(buildPoseModel(makeModel(false), noIdle, "unit.gr2|noidle.fxsxml", ref2).ok());
    CHECK_EQ(defaultClipIndex(ref2, false), -1);
    CHECK_EQ(defaultClipIndex(ref2, true), -1);
    req.member = &m2;
    unit.flags = kFlagFortified;
    {
        const UnitStatus got = pose.rows(req, a);
        const PoseStatus want = refRows(refCache, ref2, req, b);
        expectSame(got, want, a, b, "no default clip", __LINE__);
        CHECK(got.code == UnitCode::ClipNotFound && got.text.find("no default clip (ec 1000 or 1520)") != std::string::npos);
    }
    std::vector<c5m::FxsAnimation> noFort = anims();
    noFort[1].ec = {2001};
    MemberArt m3 = memoMember("NoFort.fxsxml", noFort);
    PoseModel ref3;
    CHECK(buildPoseModel(makeModel(false), noFort, "unit.gr2|nofort.fxsxml", ref3).ok());
    CHECK_EQ(defaultClipIndex(ref3, true), 0);
    req.member = &m3;
    {
        const UnitStatus got = pose.rows(req, a);
        const PoseStatus want = refRows(refCache, ref3, req, b);
        expectSame(got, want, a, b, "fortified falls back to idle", __LINE__);
        CHECK(got.ok());
    }
    // the same member object renamed in place (art reused at one address): a new model, not the last pair's
    const size_t models = pose.models();
    m3.files.fxsxml.name = "Renamed.fxsxml";
    m3.files.animations[0].ec = {2002};   // idle now the fidget (ec 1040 -> 1000)
    m3.files.animations[2].ec = {1000};
    std::vector<c5m::FxsAnimation> renamed = noFort;
    renamed[0].ec = {2002};
    renamed[2].ec = {1000};
    PoseModel ref4;
    CHECK(buildPoseModel(makeModel(false), renamed, "unit.gr2|renamed.fxsxml", ref4).ok());
    unit.flags = 0;
    {
        const UnitStatus got = pose.rows(req, a);
        const PoseStatus want = refRows(refCache, ref4, req, b);
        expectSame(got, want, a, b, "renamed member", __LINE__);
        CHECK(got.ok());
    }
    CHECK_EQ(pose.models(), models + 1);
    // programs: empty, a node that is not earlier, a non-finite weight, a mask out of range: poseFigureProgram's answers
    req.member = &member;
    unitanim::PoseProgram empty;
    {
        const UnitStatus got = pose.rowsProgram(req, empty, a);
        const PoseStatus want = refProgram(refCache, ref, req, empty, b);
        expectSame(got, want, a, b, "empty program", __LINE__);
        CHECK(got.code == UnitCode::BadInput && got.text.find("empty pose program") != std::string::npos);
    }
    unitanim::PoseProgram p1 = blendProgram(0.1f, 0.2f, 0.5f, false);
    p1.nodes[2].b = 2;
    unitanim::PoseProgram p2 = blendProgram(0.1f, 0.2f, std::nanf(""), false);
    unitanim::PoseProgram p3 = blendProgram(0.1f, 0.2f, 0.5f, false);
    p3.nodes[2].mask = 4;
    unitanim::PoseProgram p4 = blendProgram(0.1f, 0.2f, 0.5f, false);
    p4.nodes[1].clip = "Unit_Run";
    unitanim::PoseProgram p5 = sampleProgram("Unit_Run", 0.5f);
    const unitanim::PoseProgram* bads[] = {&p1, &p2, &p3, &p4, &p5};
    const char* frags[] = {"not earlier nodes", "weight not finite", "mask 4 out of range", "no clip \"Unit_Run\"",
                           "no clip \"Unit_Run\""};
    const UnitCode codes[] = {UnitCode::BadInput, UnitCode::BadInput, UnitCode::BadInput, UnitCode::ClipNotFound,
                              UnitCode::ClipNotFound};
    for (int i = 0; i < 5; ++i) {
        const UnitStatus got = pose.rowsProgram(req, *bads[i], a);
        const PoseStatus want = refProgram(refCache, ref, req, *bads[i], b);
        expectSame(got, want, a, b, std::string("program ") + frags[i], __LINE__);
        if (got.code != codes[i] || got.text.find(frags[i]) == std::string::npos)
            test::fail(__FILE__, __LINE__, std::string("program: want ") + frags[i] + ", got " + got.text);
        if (!same96(a, id)) test::fail(__FILE__, __LINE__, "program rows not identity on failure");
    }
}

// ---------------------------------------------------------------------------------------------- negative

C5R_TEST(refuse_clip_not_found) {
    Loader ld;
    ClipCache cache(ld.fn());
    PoseModel pm = model(false);
    std::vector<MeshPalette> out(1);
    expectFail(poseFigure(cache, pm, "Unit_Run", 0, true, 0.14f, 0, kT, {}, out), PoseErr::ClipNotFound,
               "names no clip \"Unit_Run\"", out, __LINE__);
    out.resize(1);
    expectFail(poseFigure(cache, pm, "", 0, true, 0.14f, 0, kT, {}, out), PoseErr::ClipNotFound, "no clip", out,
               __LINE__);
    CHECK_EQ(ld.calls, 0);
    CHECK(poseErrUnitCode(PoseErr::ClipNotFound) == UnitCode::ClipNotFound);
}

C5R_TEST(refuse_curve_unsupported) {
    Loader ld;
    ClipCache cache(ld.fn());
    PoseModel pm = model(false);
    std::vector<MeshPalette> out(1);
    expectFail(poseFigure(cache, pm, "Unit_Bad", 0, true, 0.14f, 0, kT, {0}, out), PoseErr::CurveUnsupported,
               "track Spine", out, __LINE__);
    // cached: a second ask does not read the file again
    out.resize(1);
    expectFail(poseFigure(cache, pm, "Unit_Bad", 1, true, 0.14f, 0, kT, {0}, out), PoseErr::CurveUnsupported,
               "UnsupportedFormat", out, __LINE__);
    CHECK_EQ(ld.calls, 1);
    CHECK_EQ(cache.stats().failed_entries, static_cast<size_t>(1));
    CHECK(poseErrUnitCode(PoseErr::CurveUnsupported) == UnitCode::CurveUnsupported);
}

C5R_TEST(refuse_model_not_found) {
    Loader ld;
    ClipCache cache(ld.fn());
    PoseModel pm = model(false);
    std::vector<MeshPalette> out(1);
    expectFail(poseFigure(cache, pm, "Unit_Missing", 0, true, 0.14f, 0, kT, {0}, out), PoseErr::ModelNotFound,
               "unit_missing.gr2: in no source", out, __LINE__);
    ClipCache noLoader{ClipFileLoader()};
    out.resize(1);
    expectFail(poseFigure(noLoader, pm, "Unit_IdleA", 0, true, 0.14f, 0, kT, {0}, out), PoseErr::BadModel,
               "no loader", out, __LINE__);
    CHECK(poseErrUnitCode(PoseErr::ModelNotFound) == UnitCode::ModelNotFound);
}

C5R_TEST(refuse_bad_model) {
    Loader ld;
    ClipCache cache(ld.fn());
    PoseModel pm = model(false);
    std::vector<MeshPalette> out(1);
    expectFail(poseFigure(cache, pm, "Unit_Broken", 0, true, 0.14f, 0, kT, {0}, out), PoseErr::BadModel,
               "BadMagic", out, __LINE__);
    // a mesh binding a bone no skeleton has: building is fine, posing that mesh is refused
    c5m::FileInfo f = makeModel(false);
    f.meshes[1].bone_bindings[0].bone_name = "Tail";
    PoseModel p2;
    CHECK(buildPoseModel(f, anims(), "tail.gr2", p2).ok());
    out.resize(1);
    expectFail(poseFigure(cache, p2, "Unit_IdleA", 0, true, 0.14f, 0, kT, {1}, out), PoseErr::BadModel,
               "mesh Legs binds bones that no skeleton", out, __LINE__);
    std::vector<MeshPalette> ok;
    CHECK(poseFigure(cache, p2, "Unit_IdleA", 0, true, 0.14f, 0, kT, {0}, ok).ok());
    // a parent after its child
    c5m::FileInfo g = makeModel(false);
    g.skeletons[0].bones[1].parent = 2;
    PoseModel p3;
    PoseStatus s3 = buildPoseModel(g, anims(), "order.gr2", p3);
    CHECK(s3.code == PoseErr::BadModel && s3.msg.find("bone Spine has parent 2") != std::string::npos);
    CHECK(p3.skeletons.empty() && p3.meshes.empty());
    // axes that are not invertible
    c5m::FileInfo h = makeModel(false);
    h.up[0] = 1; h.up[1] = 0; h.up[2] = 0;
    PoseModel p4;
    PoseStatus s4 = buildPoseModel(h, anims(), "axes.gr2", p4);
    CHECK(s4.code == PoseErr::BadModel && s4.msg.find("not invertible") != std::string::npos);
    // more than 32 bound bones
    c5m::FileInfo k = makeModel(false);
    for (int i = 0; i < 31; ++i) k.meshes[0].bone_bindings.push_back(k.meshes[0].bone_bindings[0]);
    PoseModel p5;
    CHECK(buildPoseModel(k, anims(), "many.gr2", p5).ok());
    out.resize(1);
    expectFail(poseFigure(cache, p5, "Unit_IdleA", 0, true, 0.14f, 0, kT, {0}, out), PoseErr::BadModel,
               "binds 34 bones", out, __LINE__);
    CHECK(poseErrUnitCode(PoseErr::BadModel) == UnitCode::BadModel);
}

C5R_TEST(refuse_bad_input) {
    Loader ld;
    ClipCache cache(ld.fn());
    PoseModel pm = model(false);
    std::vector<MeshPalette> out(1);
    expectFail(poseFigure(cache, pm, "Unit_IdleA", std::nanf(""), true, 0.14f, 0, kT, {}, out), PoseErr::BadInput,
               "not finite", out, __LINE__);
    const float badT[3] = {0, INFINITY, 0};
    out.resize(1);
    expectFail(poseFigure(cache, pm, "Unit_IdleA", 0, true, 0.14f, 0, badT, {}, out), PoseErr::BadInput, "not finite",
               out, __LINE__);
    out.resize(1);
    expectFail(poseFigure(cache, pm, "Unit_IdleA", 0, true, 0.14f, 0, kT, {7}, out), PoseErr::BadInput, "mesh 7 of 4",
               out, __LINE__);
    const CachedClip* cc = nullptr;
    PoseStatus s = cache.get(pm, "Unit_IdleA", 5, cc);
    CHECK(s.code == PoseErr::BadInput && cc == nullptr);
    ModulePose pose(ld.fn());
    PoseRequest req;
    Float4 rows[kPaletteBones * 3];
    UnitStatus us = pose.rows(req, rows);
    CHECK(us.code == UnitCode::BadInput);
    CHECK(rows[0].x == 1.0f && rows[4].y == 1.0f && rows[95].z == 1.0f);
}

C5R_TEST_MAIN
