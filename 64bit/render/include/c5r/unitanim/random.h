// c5r/unitanim/random.h - the controller's random source (epic 0374, issue 4403; unit-controller.md 11 decision 5,
// premise X7): one generator per unit, seeded from the scene seed and the unit's (player, id), so that a unit's draws
// do not depend on the other units; and the forced choices of the timeline's `choice` records.
//
// Decision points are named "roll:<machine file>:<socket index>" (a RandomRoll condition) and "option:<machine
// file>:<socket index>" (an EventRandomPathOption), the socket index in the machine's file order (as timeline.h).
#pragma once
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace c5r { namespace unitanim {

// splitmix64 of the scene seed, the player and the id.
uint64_t unitSeed(uint64_t scene_seed, int32_t player, int32_t id);

// xoshiro256** (public-domain algorithm by Blackman and Vigna, written here from its description).
class Rng {
public:
    explicit Rng(uint64_t seed = 0) { reseed(seed); }
    void reseed(uint64_t seed);
    uint64_t next();
    double uniform();                       // [0, 1), 53 bits
    bool roll(double p) { return uniform() < p; }
    int below(int n);                       // 0..n-1 uniformly (n >= 1)
    // An index drawn by the weights (non-negative, sum > 0; else -1).
    int pick(const std::vector<float>& weights);
private:
    uint64_t s_[4];
};

// The forced choices of one unit: per (figure, point) a queue of values, consumed in order.
class ForcedChoices {
public:
    void push(int32_t figure, const std::string& point, int32_t value);
    // Takes the next value for (figure, point); false when none is queued.
    bool take(int32_t figure, const std::string& point, int32_t& value);
    size_t pending() const;
    bool empty() const { return q_.empty(); }   // 0441: no choice queued (q_ drops a point when its queue empties)
    void clear() { q_.clear(); }
private:
    std::map<std::pair<int32_t, std::string>, std::deque<int32_t>> q_;
};

}}  // namespace c5r::unitanim
