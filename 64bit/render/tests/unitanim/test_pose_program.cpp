// render/tests/unitanim/test_pose_program.cpp - issue 4409: ModulePose::rowsProgram, poseFigureProgram (pose.h) and
// the controller's PoseSource (controller_pose.h) on 0041's synthetic model (pose_fixture.h). MC condition (1): a
// program with one Sample is bit-identical (memcmp) to ModulePose::rows for the same (clip, t). Blends per bone as
// blendfit.py, masks, makeEcMap; every refusal with code and text.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "c5r/unitanim/controller_pose.h"
#include "pose_fixture.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::units;
using namespace c5r::unitanim;

namespace {

struct Fix {
    posefix::Loader ld;
    MemberArt member;
    UnitModel um;
    ModelMesh mesh;
    UnitInput unit;
    PoseRequest req;
    Fix() {
        member.member = "U_TEST";
        member.scale = 0.14f;
        member.files.fxsxml.name = "Unit.fxsxml";
        for (const c5m::FxsAnimation& a : posefix::anims()) {
            c5m::ResolvedAnimation ra;
            ra.file.name = a.file;
            ra.ec = a.ec;
            ra.ec_text = a.ec_text;
            ra.name = a.name;
            member.files.animations.push_back(ra);
        }
        um.gr2 = "unit.gr2";
        um.file.reset(new c5m::FileInfo(posefix::makeModel(false)));
        mesh.name = "Body";
        mesh.index = 0;
        unit.player = 3;
        unit.id = 77;
        req.unit = &unit;
        req.member = &member;
        req.model = &um;
        req.mesh = &mesh;
        req.figure = 2;
        req.scale = 0.14f;
        req.yaw = 0.5235988f;
        const float T[3] = {939.5f, -342.25f, 12.0f};
        std::memcpy(req.T, T, sizeof T);
    }
};

PoseNode sample(const char* clip, int32_t ec, float t) {
    PoseNode n;
    n.kind = PoseNode::Sample;
    n.clip = clip;
    n.ec = ec;
    n.t = t;
    return n;
}
PoseNode blend(int32_t a, int32_t b, float w, int32_t mask = -1) {
    PoseNode n;
    n.kind = PoseNode::Blend;
    n.a = a;
    n.b = b;
    n.w = w;
    n.mask = mask;
    return n;
}
float maxDiff(const Float4* a, const Float4* b, size_t n) {
    float d = 0;
    for (size_t i = 0; i < n; ++i) {
        d = std::fmax(d, std::fabs(a[i].x - b[i].x));
        d = std::fmax(d, std::fabs(a[i].y - b[i].y));
        d = std::fmax(d, std::fabs(a[i].z - b[i].z));
        d = std::fmax(d, std::fabs(a[i].w - b[i].w));
    }
    return d;
}
void expectFail(ModulePose& pose, const PoseRequest& req, const PoseProgram& p, UnitCode code, const char* fragment,
                int line) {
    Float4 rows[kPaletteBones * 3];
    UnitStatus s = pose.rowsProgram(req, p, rows);
    std::printf("  refused: %s\n", s.text.c_str());
    if (s.code != code || s.text.find(fragment) == std::string::npos)
        test::fail(__FILE__, line, std::string("want \"") + fragment + "\", got \"" + s.text + "\"");
    if (!(rows[0].x == 1.0f && rows[4].y == 1.0f && rows[95].z == 1.0f && rows[1].x == 0.0f))
        test::fail(__FILE__, line, "rows not identity on failure");
}

}  // namespace

C5R_TEST(single_sample_bit_identical_to_rows) {
    Fix f;
    ModulePose pose(f.ld.fn());
    const char* clips[] = {"Unit_IdleA.gr2", "Unit_FidgetA.gr2", "Units\\Unit_Fortify_Idle.gr2"};
    const float ts[] = {0.0f, 0.4f, 1.25f, 2.9999f};
    int n = 0;
    for (const char* c : clips)
        for (float t : ts)
            for (uint32_t mi = 0; mi < 3; ++mi) {
                ModelMesh m;
                m.index = mi;
                PoseRequest r = f.req;
                r.mesh = &m;
                r.clip = c;
                r.t = t;
                Float4 a[kPaletteBones * 3], b[kPaletteBones * 3];
                UnitStatus sa = pose.rows(r, a);
                PoseProgram p;
                p.nodes.push_back(sample(c, 0, t));
                UnitStatus sb = pose.rowsProgram(r, p, b);
                CHECK(sa.ok() && sb.ok());
                CHECK(std::memcmp(a, b, sizeof a) == 0);
                ++n;
            }
    std::printf("  %d single-source requests bit-identical\n", n);
}

C5R_TEST(blends_per_bone) {
    Fix f;
    ModulePose pose(f.ld.fn());
    Float4 a[kPaletteBones * 3], b[kPaletteBones * 3], o[kPaletteBones * 3];
    PoseProgram pa, pb;
    pa.nodes.push_back(sample("Unit_IdleA.gr2", 1000, 1.25f));
    pb.nodes.push_back(sample("Unit_FidgetA.gr2", 1040, 0.5f));
    CHECK(pose.rowsProgram(f.req, pa, a).ok());
    CHECK(pose.rowsProgram(f.req, pb, b).ok());
    PoseProgram p;
    p.nodes = {pa.nodes[0], pb.nodes[0], blend(0, 1, 0.0f)};
    CHECK(pose.rowsProgram(f.req, p, o).ok());
    CHECK(maxDiff(o, a, kPaletteBones * 3) <= 2e-4f);   // w 0 = a (up to the nlerp's renormalisation)
    p.nodes[2].w = 1.0f;
    CHECK(pose.rowsProgram(f.req, p, o).ok());
    CHECK(maxDiff(o, b, kPaletteBones * 3) <= 2e-4f);
    p.nodes[2].w = 0.5f;
    CHECK(pose.rowsProgram(f.req, p, o).ok());
    CHECK(maxDiff(o, a, kPaletteBones * 3) > 1e-3f && maxDiff(o, b, kPaletteBones * 3) > 1e-3f);
    // a clip blended with itself is the clip
    PoseProgram s;
    s.nodes = {pa.nodes[0], pa.nodes[0], blend(0, 1, 0.37f)};
    CHECK(pose.rowsProgram(f.req, s, o).ok());
    CHECK(maxDiff(o, a, kPaletteBones * 3) <= 2e-4f);
    // a mask naming Arm with weight 0 and default 1, w = 1: Root (row block 0) takes b, Arm (block 2) does not
    PoseProgram m;
    m.nodes = {pa.nodes[0], pb.nodes[0], blend(0, 1, 1.0f, 0)};
    TrackMaskWeights tm;
    tm.default_weight = 1;
    tm.bones = {"Arm"};
    tm.weights = {0};
    m.masks.push_back(tm);
    CHECK(pose.rowsProgram(f.req, m, o).ok());
    CHECK(maxDiff(o, b, 3) <= 2e-4f);
    CHECK(maxDiff(o + 6, b + 6, 3) > 1e-3f);
}

C5R_TEST(controller_pose_source_and_ec_map) {
    Fix f;
    ModulePose pose(f.ld.fn());
    ControllerPose cp(pose);
    Float4 a[kPaletteBones * 3], b[kPaletteBones * 3];
    UnitStatus s = cp.rows(f.req, a);
    CHECK(s.code == UnitCode::BadInput && s.text.find("no pose program for unit (3, 77) figure 2") != std::string::npos);
    PoseProgram p;
    p.nodes.push_back(sample("Unit_FidgetA.gr2", 1040, 0.5f));
    cp.setProgram(3, 77, 2, p);
    CHECK(cp.rows(f.req, a).ok());
    CHECK(pose.rowsProgram(f.req, p, b).ok());
    CHECK(std::memcmp(a, b, sizeof a) == 0);
    CHECK_EQ(cp.programCount(), static_cast<size_t>(1));
    cp.clearPrograms();
    CHECK(!cp.rows(f.req, a).ok());
    // the member's ec map: durations from the clip cache; clips that do not bind are left out, named in notes
    EcMap em;
    std::string notes;
    CHECK(makeEcMap(pose, f.member, f.um, em, &notes).ok());
    CHECK(em.find(1000) && *em.find(1000) == "Unit_IdleA.gr2");
    CHECK(std::fabs(em.duration(1000) - 6.0f) < 1e-6f && std::fabs(em.duration(1520) - 4.0f) < 1e-6f &&
          std::fabs(em.duration(1040) - 3.0f) < 1e-6f);
    CHECK(!em.find(3002) && !em.find(3003) && !em.find(3004));   // bad curve, missing, broken
    CHECK(notes.find("Unit_Bad.gr2") != std::string::npos && notes.find("Unit_Missing.gr2") != std::string::npos);
    std::printf("  ec map: %zu codes; left out: %s\n", em.ecs.size(), notes.c_str());
}

C5R_TEST(refusals) {
    Fix f;
    ModulePose pose(f.ld.fn());
    PoseProgram p;
    expectFail(pose, f.req, p, UnitCode::BadInput, "empty pose program", __LINE__);
    p.nodes = {sample("Unit_IdleA.gr2", 1000, 1), blend(0, 1, 0.5f)};
    expectFail(pose, f.req, p, UnitCode::BadInput, "not earlier nodes", __LINE__);
    p.nodes = {sample("Unit_IdleA.gr2", 1000, 1), sample("Unit_FidgetA.gr2", 1040, 1), blend(0, 1, NAN)};
    expectFail(pose, f.req, p, UnitCode::BadInput, "weight not finite", __LINE__);
    p.nodes[2].w = 0.5f;
    p.nodes[1].t = INFINITY;
    expectFail(pose, f.req, p, UnitCode::BadInput, "t not finite", __LINE__);
    p.nodes[1].t = 1;
    p.nodes[2].mask = 0;
    expectFail(pose, f.req, p, UnitCode::BadInput, "mask 0 out of range", __LINE__);
    TrackMaskWeights tm;
    tm.binding = "MASK_LAYER";
    p.masks.push_back(tm);
    expectFail(pose, f.req, p, UnitCode::BadInput, "track mask MASK_LAYER not resolved to bones", __LINE__);
    p.masks[0].binding.clear();
    p.masks[0].bones = {"Arm"};
    expectFail(pose, f.req, p, UnitCode::BadInput, "bones and weights differ", __LINE__);
    p.masks.clear();
    p.nodes[2].mask = -1;
    p.nodes[1].clip = "unit_yup.gr2";   // a clip in the y-up basis blended with a stock one
    expectFail(pose, f.req, p, UnitCode::BadModel, "different file bases", __LINE__);
    p.nodes[1].clip = "Unit_Nothing.gr2";
    expectFail(pose, f.req, p, UnitCode::ClipNotFound, "Unit_Nothing", __LINE__);
    p.nodes[1].clip = "Unit_Bad.gr2";
    expectFail(pose, f.req, p, UnitCode::CurveUnsupported, "", __LINE__);
    p.nodes[1].clip = "Unit_FidgetA.gr2";
    PoseRequest r = f.req;
    r.scale = NAN;
    expectFail(pose, r, p, UnitCode::BadInput, "not finite", __LINE__);
    r = f.req;
    r.member = nullptr;
    expectFail(pose, r, p, UnitCode::BadInput, "without member", __LINE__);
}

C5R_TEST_MAIN
