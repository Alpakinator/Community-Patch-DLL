// render/tests/unitanim/test_precheck_masks.cpp - issue 4410: Controller::precheck checks the track masks of a member's
// reachable graphs against its model's bones (unit-controller.md 13.3). The idle machine of anim_testkit.h with its
// idle graph carrying the aim layer's binding; the model from ControllerOptions::model_reader (0041's synthetic
// model of pose_fixture.h, with and without mask data) or, without a reader, read through the controller's files.
// No game file (D83).
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include "anim_testkit.h"
#include "pose_fixture.h"

using namespace anim_test;

namespace {

const char* kAim = "TrackMaskAIM_LAYER";

c5m::FileInfo modelWith(bool main, bool cart) {
    c5m::FileInfo f = posefix::makeModel(false);
    if (main)
        for (c5m::Bone& b : f.skeletons[0].bones)
            b.track_masks.push_back(std::make_pair(std::string(kAim), b.name == "Arm" ? 1.0f : 0.0f));
    if (cart)
        for (c5m::Bone& b : f.skeletons[1].bones) b.track_masks.push_back(std::make_pair(std::string(kAim), 0.0f));
    return f;
}

c5m::DagFile maskedIdle() {
    c5m::DagFile f = dag({1000, 2000});
    f.dags[0].track_masks = {kAim};
    return f;
}

c5r::units::UnitArt row(const std::string& name) {
    c5r::units::UnitArt a;
    a.art_row = name;
    c5r::units::MemberArt m = memberArt("M_" + name, 2, "omni.fsmxml", warriorClips());
    m.files.mesh.name = "Unit.gr2";
    a.members.push_back(m);
    return a;
}

void expect(const AnimStatus& s, AnimCode code, const char* fragment, int line) {
    std::printf("  %s: %s\n", name(s.code), s.text.c_str());
    if (s.code != code || s.text.find(fragment) == std::string::npos)
        c5r::test::fail(__FILE__, line, std::string("want ") + name(code) + " with \"" + fragment + "\", got " +
                                            name(s.code) + " \"" + s.text + "\"");
}

}  // namespace

C5R_TEST(precheck_track_masks) {
    int reads = 0;
    c5m::FileInfo model;
    bool fail_read = false;
    Rig r;
    r.opt.model_reader = [&](const c5r::units::MemberArt& m, c5m::FileInfo& out, std::string& why) {
        ++reads;
        if (fail_read) {
            why = m.files.mesh.name + ": refused by the test";
            return false;
        }
        out = model;
        return true;
    };
    r.open();
    // the graphs without masks: no model is read
    std::vector<ArtCheck> out = r.c.precheck({row("PLAIN")});
    CHECK_ANIM(out[0].status, AnimCode::Ok);
    CHECK_EQ(reads, 0);
    r.c.logic()->putGraph("idle.dge", maskedIdle());
    // every skeleton carries the binding
    model = modelWith(true, true);
    expect(r.c.checkArt(row("FULL")), AnimCode::Ok, "", __LINE__);
    CHECK_EQ(reads, 1);
    // 0617: one skeleton (Cart, mesh 2) without data: stock binds no mask there and blends unmasked: passes
    model = modelWith(true, false);
    expect(r.c.checkArt(row("CART")), AnimCode::Ok, "", __LINE__);
    // 0617: a model without any mask data (the four VP edited ships, 13.3; before 0617 TrackMaskNotBound): passes
    model = modelWith(false, false);
    out = r.c.precheck({row("NONE"), row("NONE2")});
    CHECK_EQ(out.size(), static_cast<size_t>(2));
    CHECK_ANIM(out[0].status, AnimCode::Ok);
    CHECK_ANIM(out[1].status, AnimCode::Ok);
    CHECK_EQ(out[1].art_row, std::string("NONE2"));
    // what stays refused: a weight that is not finite, named with member, graph, binding, bone and skeleton
    model = modelWith(true, true);
    model.skeletons[0].bones[2].track_masks[0].second = NAN;
    expect(r.c.checkArt(row("NAN")), AnimCode::TrackMaskNotBound,
           "M_NAN: idle.dge: track mask TrackMaskAIM_LAYER: bone Arm of skeleton Main: weight not finite", __LINE__);
    // the model cannot be read
    fail_read = true;
    expect(r.c.checkArt(row("UNREAD")), AnimCode::TrackMaskNotBound,
           "M_UNREAD: model not read: Unit.gr2: refused by the test", __LINE__);
}

C5R_TEST(precheck_track_masks_reads_through_files) {
    Rig r;   // no model_reader: the mesh is read through the controller's files
    r.open();
    r.c.logic()->putGraph("idle.dge", maskedIdle());
    expect(r.c.checkArt(row("MISSING")), AnimCode::TrackMaskNotBound, "M_MISSING: model not read: model unit.gr2 not found",
           __LINE__);
    r.files.files["unit.gr2"] = "not a granny file";
    expect(r.c.checkArt(row("BROKEN")), AnimCode::TrackMaskNotBound, "M_BROKEN: model not read: unit.gr2 (mem:unit.gr2)",
           __LINE__);
}

C5R_TEST_MAIN
