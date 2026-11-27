// render/tests/unitanim/test_clock.cpp - issue 4403: the K6 step rule (clock.h; unit-controller.md 7.2, 9).
#include <cmath>
#include <limits>

#include "anim_testkit.h"

using namespace c5r::unitanim;

C5R_TEST(frame_cap_steps_one_per_frame) {
    UnitClock c;
    ClockParams p;
    int total = 0, steps = 0;
    for (int i = 0; i < 300; ++i) {
        CHECK_ANIM(c.take(1.0 / 30.0, p, steps), AnimCode::Ok);
        CHECK_EQ(steps, 1);
        total += steps;
    }
    CHECK_EQ(total, 300);
    CHECK(c.pending() < 1e-6);
}

C5R_TEST(short_frames_accumulate_whole_steps) {
    UnitClock c;
    ClockParams p;
    int steps = 0, total = 0;
    const int want[] = {0, 1, 0, 1, 1};   // 0.02 s frames: 0.02, 0.04, 0.0267, 0.0467, 0.0333
    for (int i = 0; i < 5; ++i) {
        CHECK_ANIM(c.take(0.02, p, steps), AnimCode::Ok);
        CHECK_EQ(steps, want[i]);
        total += steps;
    }
    CHECK_EQ(total, 3);
}

C5R_TEST(long_frame_capped_rest_dropped) {
    // the capture regime (4407, 7.2): a stalled frame gives 6 steps (0.2 s); the short frame after it gives 0
    // (P04: 0.200 s per captured pair) and a second long frame 6 again (P01: 0.400 s per pair)
    UnitClock c;
    ClockParams p;
    int steps = 0;
    CHECK_ANIM(c.take(0.95, p, steps), AnimCode::Ok);
    CHECK_EQ(steps, 6);
    CHECK_EQ(c.pending(), 0.0);
    CHECK_ANIM(c.take(0.025, p, steps), AnimCode::Ok);
    CHECK_EQ(steps, 0);
    CHECK_ANIM(c.take(1.2, p, steps), AnimCode::Ok);
    CHECK_EQ(steps, 6);
    CHECK_ANIM(c.take(0.3, p, steps), AnimCode::Ok);
    CHECK_EQ(steps, 6);
}

C5R_TEST(parameters) {
    UnitClock c;
    ClockParams p;
    p.h = 1.0 / 60.0;
    p.max_steps = 12;
    int steps = 0;
    CHECK_ANIM(c.take(0.05, p, steps), AnimCode::Ok);
    CHECK_EQ(steps, 3);
    CHECK_ANIM(c.take(1.0, p, steps), AnimCode::Ok);
    CHECK_EQ(steps, 12);
    p.max_steps = 0;
    CHECK_ANIM(c.take(1.0, p, steps), AnimCode::Ok);
    CHECK_EQ(steps, 0);
}

C5R_TEST(refuse_bad_step) {
    UnitClock c;
    ClockParams p;
    int steps = 7;
    CHECK_ANIM(c.take(-0.01, p, steps), AnimCode::BadEvent);
    CHECK_EQ(steps, 0);
    CHECK_ANIM(c.take(std::numeric_limits<double>::quiet_NaN(), p, steps), AnimCode::BadEvent);
    CHECK_ANIM(c.take(std::numeric_limits<double>::infinity(), p, steps), AnimCode::BadEvent);
    p.h = 0;
    CHECK_ANIM(c.take(0.1, p, steps), AnimCode::BadEvent);
    p.h = 1.0 / 30;
    p.max_steps = -1;
    CHECK_ANIM(c.take(0.1, p, steps), AnimCode::BadEvent);
    CHECK_EQ(c.pending(), 0.0);
}

C5R_TEST_MAIN
