// render/tests/unitanim/test_clock_continuous.cpp - issue 4406: K6's continuous reading beside the step reading
// (clock.h ClockMode; unit-controller.md 15): per unit one update per frame of min(dt, max_dt), nothing carried.
#include "anim_testkit.h"

using namespace anim_test;

C5R_TEST(continuous_take_gives_one_step_of_the_capped_frame) {
    ClockParams p;
    p.mode = ClockMode::Continuous;
    UnitClock c;
    int n = -1;
    double len = 0;
    CHECK_ANIM(c.take(0.05, p, n, len), AnimCode::Ok);
    CHECK_EQ(n, 1);
    CHECK_NEAR(len, 0.05, 1e-12);
    CHECK_ANIM(c.take(0.5, p, n, len), AnimCode::Ok);
    CHECK_EQ(n, 1);
    CHECK_NEAR(len, 0.2, 1e-12);
    CHECK_ANIM(c.take(0.0, p, n, len), AnimCode::Ok);
    CHECK_EQ(n, 0);
    CHECK_EQ(c.pending(), 0.0);
    // refusals
    CHECK_ANIM(c.take(-1.0, p, n, len), AnimCode::BadEvent);
    CHECK_ANIM(c.take(std::nan(""), p, n, len), AnimCode::BadEvent);
    p.max_dt = 0;
    CHECK_ANIM(c.take(0.1, p, n, len), AnimCode::BadEvent);
    // the step mode through the same call: whole steps of h
    ClockParams s;
    s.mode = ClockMode::Steps;
    UnitClock d;
    CHECK_ANIM(d.take(0.1 + 1e-9, s, n, len), AnimCode::Ok);
    CHECK_EQ(n, 3);
    CHECK_NEAR(len, 1.0 / 30.0, 1e-12);
}

C5R_TEST(continuous_clock_puts_clip_time_off_the_lattice) {
    Rig r;
    r.opt.clock.mode = ClockMode::Continuous;
    r.open(1, 0.0f);
    TimelineFrame f = Rig::frame(1, 0);
    TimelineEvent e;
    e.kind = EventKind::Create;
    e.player = 0;
    e.id = 1;
    e.unit = Rig::unit(0, 1);
    f.events.push_back(e);
    CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
    CHECK_EQ(r.prog(0, 1, 0)->program.nodes[0].t, 0.0f);
    CHECK_ANIM(r.c.apply(Rig::frame(2, 0.05)), AnimCode::Ok);
    CHECK_NEAR(r.prog(0, 1, 0)->program.nodes[0].t, 0.05, 1e-6);
    CHECK_ANIM(r.c.apply(Rig::frame(3, 0.9)), AnimCode::Ok);   // capped at 0.2
    CHECK_NEAR(r.prog(0, 1, 0)->program.nodes[0].t, 0.25, 1e-6);
    CHECK_ANIM(r.c.apply(Rig::frame(4, 0.01)), AnimCode::Ok);  // a short frame after a capped one still counts
    CHECK_NEAR(r.prog(0, 1, 0)->program.nodes[0].t, 0.26, 1e-6);
    // the step mode on the same frames: 0, 1 step (0.0333), 6 steps (0.2333), 0 steps
    Rig s;
    s.opt.clock.mode = ClockMode::Steps;
    s.open(1, 0.0f);
    CHECK_ANIM(s.c.apply(f), AnimCode::Ok);
    CHECK_ANIM(s.c.apply(Rig::frame(2, 0.05)), AnimCode::Ok);
    CHECK_NEAR(s.prog(0, 1, 0)->program.nodes[0].t, 1.0 / 30.0, 1e-6);
    CHECK_ANIM(s.c.apply(Rig::frame(3, 0.9)), AnimCode::Ok);
    CHECK_NEAR(s.prog(0, 1, 0)->program.nodes[0].t, 7.0 / 30.0, 1e-6);
    CHECK_ANIM(s.c.apply(Rig::frame(4, 0.01)), AnimCode::Ok);
    CHECK_NEAR(s.prog(0, 1, 0)->program.nodes[0].t, 7.0 / 30.0, 1e-6);
}

C5R_TEST_MAIN
