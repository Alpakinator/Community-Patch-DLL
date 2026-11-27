// render/tests/unitanim/test_random.cpp - issue 4403: the per-unit random source and forced choices (random.h), and
// DoD D's distribution checks at the generator: 10^5 draws within a 99.9 % binomial interval.
#include <cmath>
#include <set>

#include "anim_testkit.h"

using namespace c5r::unitanim;

namespace {
const double kZ = 3.2905;   // two-sided 99.9 %
bool inInterval(long k, long n, double p) {
    const double m = n * p, sd = std::sqrt(n * p * (1 - p));
    return std::fabs(k - m) <= kZ * sd;
}
}  // namespace

C5R_TEST(seeds_are_per_unit_and_deterministic) {
    CHECK_EQ(unitSeed(7, 0, 1), unitSeed(7, 0, 1));
    std::set<uint64_t> s = {unitSeed(7, 0, 1), unitSeed(7, 0, 2), unitSeed(7, 1, 1), unitSeed(8, 0, 1),
                            unitSeed(7, 1, 0), unitSeed(0, 0, 0)};
    CHECK_EQ(s.size(), static_cast<size_t>(6));
    Rng a(unitSeed(7, 0, 1)), b(unitSeed(7, 0, 1));
    for (int i = 0; i < 1000; ++i) CHECK_EQ(a.next(), b.next());
}

C5R_TEST(uniform_range_and_below) {
    Rng r(42);
    long counts[5] = {0, 0, 0, 0, 0};
    const long n = 100000;
    for (long i = 0; i < n; ++i) {
        double u = r.uniform();
        CHECK(u >= 0 && u < 1);
        int k = r.below(5);
        CHECK(k >= 0 && k < 5);
        counts[k]++;
    }
    for (int k = 0; k < 5; ++k) CHECK(inInterval(counts[k], n, 0.2));
    CHECK_EQ(r.below(1), 0);
}

C5R_TEST(roll_rates_dod_d) {
    // the files' chances: 0.0007 (omni idle), 0.0001 (fortify, civilians), and a large one
    const float chances[] = {0.0007f, 0.0001f, 0.3f};
    for (float p : chances) {
        Rng r(unitSeed(4403, 0, static_cast<int>(p * 1e6)));
        const long n = 100000;
        long k = 0;
        for (long i = 0; i < n; ++i) k += r.roll(p);
        CHECK(inInterval(k, n, p));
    }
}

C5R_TEST(pick_by_weights_dod_d) {
    Rng r(99);
    const long n = 100000;
    long k = 0;
    for (long i = 0; i < n; ++i) k += r.pick({0.6f, 0.4f}) == 0;
    CHECK(inInterval(k, n, 0.6));
    k = 0;
    for (long i = 0; i < n; ++i) k += r.pick({0.7f, 0.3f}) == 1;
    CHECK(inInterval(k, n, 0.3));
    CHECK_EQ(r.pick({0.0f, 0.0f}), -1);
    CHECK_EQ(r.pick({0.5f, -1.0f}), -1);
    CHECK_EQ(r.pick({}), -1);
    CHECK_EQ(r.pick({0.0f, 2.0f}), 1);
}

C5R_TEST(forced_choices_queue) {
    ForcedChoices f;
    int32_t v = -1;
    CHECK(!f.take(0, "roll:m:1", v));
    f.push(0, "roll:m:1", 1);
    f.push(0, "roll:m:1", 0);
    f.push(1, "roll:m:1", 1);
    f.push(0, "option:m:1", 2);
    CHECK_EQ(f.pending(), static_cast<size_t>(4));
    CHECK(f.take(0, "roll:m:1", v));
    CHECK_EQ(v, 1);
    CHECK(f.take(0, "roll:m:1", v));
    CHECK_EQ(v, 0);
    CHECK(!f.take(0, "roll:m:1", v));
    CHECK(!f.take(0, "roll:m:2", v));
    CHECK(f.take(0, "option:m:1", v));
    CHECK_EQ(v, 2);
    CHECK_EQ(f.pending(), static_cast<size_t>(1));
    f.clear();
    CHECK_EQ(f.pending(), static_cast<size_t>(0));
}

C5R_TEST_MAIN
