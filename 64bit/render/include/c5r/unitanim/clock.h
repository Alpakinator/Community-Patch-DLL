// c5r/unitanim/clock.h - the controller's step rule, K6 (epic 0374, issue 4403; research/05-graphics-composition/
// unit-controller.md 7.2 and 9, as revised by MC #2023).
//
// Per unit an accumulator of the caller's frame time, consumed in whole steps of h seconds (1/30 s), at most
// max_steps (6, i.e. 0.2 s) per frame; when the cap is reached the rest of the accumulator is dropped, so a short
// frame after a capped one may give 0 steps (the 0.200 s pairs of P04). h and the cap are parameters. The figures'
// clocks, their state times and their machine updates advance by these steps; one machine update per step.
//
// What makes a unit or a figure skip a step in the recordings is not known (7.2); it is data: SkipRule lists, per
// frame, steps to remove from a unit or a figure, and StepHook lets a caller decide them in code. Neither is used in
// free running.
#pragma once
#include <cstdint>
#include <vector>

#include "c5r/unitanim/status.h"

namespace c5r { namespace unitanim {

// 4406 (additive; lead-0374 2026-11-15 23:15, MC #2088 pending): the continuous reading of K6. 4405 B's single
// captures at frame caps 30 and 60 put clip times off the 1/30 lattice, which whole steps cannot give; the candidate
// rule is per unit t += min(dt, max_dt) per frame. Continuous: one update per frame (dt > 0) of length min(dt, max_dt);
// Steps: the whole-step accumulator above. MC #2089 (2026-11-15 23:20): Continuous is the default, plus the per-unit
// skip rules held as data; Steps is kept for tests and as a comparison line, never as a fallback.
enum class ClockMode : uint8_t { Steps = 0, Continuous = 1 };

struct ClockParams {
    double h = 1.0 / 30.0;      // seconds per step
    int max_steps = 6;          // per frame
    ClockMode mode = ClockMode::Continuous;   // 4406: the default since MC #2089 (Steps: tests and comparison only)
    double max_dt = 0.2;        // Continuous: the per-frame ceiling (seconds)
};

class UnitClock {
public:
    // Adds dt (seconds, finite, >= 0, else BadEvent) and returns the whole steps this frame takes (0..max_steps).
    AnimStatus take(double dt, const ClockParams& p, int& steps);
    // 4406: the same with the length of each step: h in Steps mode; in Continuous mode one step (none for dt 0) of
    // min(dt, max_dt) seconds, nothing carried. BadEvent also for a max_dt that is not finite and > 0.
    AnimStatus take(double dt, const ClockParams& p, int& steps, double& step_len);
    double pending() const { return acc_; }   // seconds carried to the next frame (< h)
    void reset() { acc_ = 0; }
private:
    double acc_ = 0;
};

// Steps removed from a unit (figure -1) or one figure on the frame with sequence number seq (the timeline's frame
// seq). The oracle fills them from recordings; a removed step leaves that figure's clips, state time and machine as
// they were for that step.
struct SkipRule {
    uint64_t seq = 0;
    int32_t player = -1, id = -1;
    int32_t figure = -1;        // -1 = every figure of the unit
    int steps = 1;
};

// 4406 round 2 (additive; lead-0374 00:21 from 4405 B's Q4a and 4407's P04 [L]): a unit given an action request
// (an `action` event) does not advance on one frame, while the others do. Which frame (the order's or the next) the
// recordings cannot separate, so both are held as data [I]: OrderFrame skips the frame that carries the request,
// NextFrame the frame after it. Like a SkipRule, a skipped update leaves the unit's clips, state time and machine as
// they were (the request is seen on the next update). None (the default) skips nothing.
enum class ActionSkip : uint8_t { None, OrderFrame, NextFrame };

// A caller's own skip decision (after the rules): return how many of `steps` the figure takes (0..steps).
class StepHook {
public:
    virtual ~StepHook() = default;
    virtual int steps(uint64_t seq, int32_t player, int32_t id, int32_t figure, int steps) = 0;
};

}}  // namespace c5r::unitanim
