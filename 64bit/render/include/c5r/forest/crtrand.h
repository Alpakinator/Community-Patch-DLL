// c5r/forest/crtrand.h - the MSVC C runtime's rand() as the EXE's Forest system draws it (epic 0431; shared by the
// placement, ticket 4550, and the draw, ticket 4551; one header since ticket 4554, so that a unit can include both).
//
// MSVC's rand(): state = state * 214013 + 2531011, value = (state >> 16) & 0x7FFF; srand(seed) sets the state (the CRT's
// start state is 1). The caller seeds it: stock's sequence depends on every earlier rand() of the EXE.
#pragma once
#include <cstdint>

namespace c5r { namespace forest {

class CrtRand {
public:
    explicit CrtRand(uint32_t seed = 1) : state_(seed) {}
    int next() {
        state_ = state_ * 214013u + 2531011u;
        return static_cast<int>((state_ >> 16) & 0x7FFFu);
    }
    uint32_t state() const { return state_; }

private:
    uint32_t state_;
};

}} // namespace c5r::forest
