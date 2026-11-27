// render/tests/unitanim/test_action_skip.cpp - issue 4406 round 2 (epic 0374): ActionSkip (clock.h), the
// action-request skip rule held as data (lead-0374 00:21, 4405 B's Q4a and 4407's P04 [L]; which frame [I]). A unit
// given an action request takes no step on the order's frame (OrderFrame) or the frame after it (NextFrame); another
// unit on the same frames advances; None (the default) skips nothing; a dt-0 frame does not consume it.
#include "anim_testkit.h"

using namespace c5r::unitanim;
using namespace anim_test;

namespace {

const double kH = 1.0 / 30.0;

TimelineEvent createEv(int id) {
    TimelineEvent e;
    e.kind = EventKind::Create;
    e.player = 0;
    e.id = id;
    e.unit = Rig::unit(0, id);
    return e;
}
TimelineEvent attackEv(int id) {
    TimelineEvent e;
    e.kind = EventKind::Action;
    e.player = 0;
    e.id = id;
    e.action = ActionKind::Attack;
    return e;
}

// Two units (ids 1 and 2) idle for 3 frames; unit 1 is ordered to attack on frame 5.
void setup(Rig& r, ActionSkip mode) {
    r.opt.action_skip = mode;
    r.open(1, 0.0f);
    TimelineFrame f = Rig::frame(1, 0);
    f.events.push_back(createEv(1));
    f.events.push_back(createEv(2));
    CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
    for (uint64_t s = 2; s < 5; ++s) CHECK_ANIM(r.c.apply(Rig::frame(s, kH)), AnimCode::Ok);
    CHECK_NEAR(r.prog(0, 1, 0)->program.nodes[0].t, 3 * kH, 1e-5);
}

}  // namespace

C5R_TEST(none_skips_nothing) {
    Rig r;
    setup(r, ActionSkip::None);
    TimelineFrame g = Rig::frame(5, kH);
    g.events.push_back(attackEv(1));
    CHECK_ANIM(r.c.apply(g), AnimCode::Ok);
    CHECK_EQ(r.prog(0, 1, 0)->state, 9);    // the step advanced idle, then the machine took the request
    CHECK_NEAR(r.prog(0, 2, 0)->program.nodes[0].t, 4 * kH, 1e-5);
}

C5R_TEST(order_frame_skips_the_order_frame) {
    Rig r;
    setup(r, ActionSkip::OrderFrame);
    TimelineFrame g = Rig::frame(5, kH);
    g.events.push_back(attackEv(1));
    CHECK_ANIM(r.c.apply(g), AnimCode::Ok);
    CHECK_EQ(r.prog(0, 1, 0)->state, 1);                                 // no update: idle, not advanced
    CHECK_NEAR(r.prog(0, 1, 0)->program.nodes[0].t, 3 * kH, 1e-5);
    CHECK_NEAR(r.prog(0, 2, 0)->program.nodes[0].t, 4 * kH, 1e-5);      // the other unit advances
    CHECK_ANIM(r.c.apply(Rig::frame(6, kH)), AnimCode::Ok);
    CHECK_EQ(r.prog(0, 1, 0)->state, 9);                                 // the request is seen on the next update
    CHECK_NEAR(r.prog(0, 1, 0)->program.nodes[0].t, 0.0, 1e-6);
    CHECK_ANIM(r.c.apply(Rig::frame(7, kH)), AnimCode::Ok);
    CHECK_NEAR(r.prog(0, 1, 0)->program.nodes[0].t, kH, 1e-5);          // one skip only
}

C5R_TEST(next_frame_skips_the_frame_after) {
    Rig r;
    setup(r, ActionSkip::NextFrame);
    TimelineFrame g = Rig::frame(5, kH);
    g.events.push_back(attackEv(1));
    CHECK_ANIM(r.c.apply(g), AnimCode::Ok);
    CHECK_EQ(r.prog(0, 1, 0)->state, 9);                                 // the order's frame runs as usual
    CHECK_NEAR(r.prog(0, 1, 0)->program.nodes[0].t, 0.0, 1e-6);
    CHECK_ANIM(r.c.apply(Rig::frame(6, 0)), AnimCode::Ok);              // dt 0: no step, the rule waits
    CHECK_ANIM(r.c.apply(Rig::frame(7, kH)), AnimCode::Ok);
    CHECK_NEAR(r.prog(0, 1, 0)->program.nodes[0].t, 0.0, 1e-6);          // skipped
    CHECK_NEAR(r.prog(0, 2, 0)->program.nodes[0].t, 5 * kH, 1e-5);
    CHECK_ANIM(r.c.apply(Rig::frame(8, kH)), AnimCode::Ok);
    CHECK_NEAR(r.prog(0, 1, 0)->program.nodes[0].t, kH, 1e-5);
}

C5R_TEST_MAIN
