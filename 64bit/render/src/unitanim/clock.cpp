// clock.cpp - the K6 step rule (epic 0374, issue 4403; clock.h).
#include "c5r/unitanim/clock.h"

#include <cmath>
#include <string>

namespace c5r { namespace unitanim {

AnimStatus UnitClock::take(double dt, const ClockParams& p, int& steps, double& step_len) {
    steps = 0;
    step_len = p.h;
    if (p.mode == ClockMode::Steps) return take(dt, p, steps);
    if (!std::isfinite(dt) || dt < 0)
        return AnimStatus::Fail(AnimCode::BadEvent, "frame step " + std::to_string(dt) + " s: not finite or negative");
    if (!(p.max_dt > 0) || !std::isfinite(p.max_dt))
        return AnimStatus::Fail(AnimCode::BadEvent, "clock parameters: max_dt must be finite and > 0");
    acc_ = 0;
    step_len = dt < p.max_dt ? dt : p.max_dt;
    steps = dt > 0 ? 1 : 0;
    return AnimStatus::Ok();
}

AnimStatus UnitClock::take(double dt, const ClockParams& p, int& steps) {
    steps = 0;
    if (!std::isfinite(dt) || dt < 0)
        return AnimStatus::Fail(AnimCode::BadEvent, "frame step " + std::to_string(dt) + " s: not finite or negative");
    if (!(p.h > 0) || !std::isfinite(p.h) || p.max_steps < 0)
        return AnimStatus::Fail(AnimCode::BadEvent, "clock parameters: h must be > 0 and max_steps >= 0");
    acc_ += dt;
    // whole steps; the tolerance keeps a sum of exact steps (10 x 1/30 s) from losing one to rounding
    double n = std::floor(acc_ / p.h + 1e-9);
    if (n >= p.max_steps) {
        steps = p.max_steps;
        acc_ = 0;      // the cap drops the rest (a short frame after a capped one takes 0 steps)
        return AnimStatus::Ok();
    }
    steps = static_cast<int>(n);
    acc_ -= n * p.h;
    if (acc_ < 0) acc_ = 0;
    return AnimStatus::Ok();
}

}}  // namespace c5r::unitanim
