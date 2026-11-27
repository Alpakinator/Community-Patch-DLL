// random.cpp - the controller's random source (epic 0374, issue 4403; random.h). splitmix64 and xoshiro256** written
// from their published descriptions (public domain algorithms).
#include "c5r/unitanim/random.h"

namespace c5r { namespace unitanim {

namespace {
uint64_t splitmix(uint64_t& x) {
    uint64_t z = (x += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
}  // namespace

uint64_t unitSeed(uint64_t scene_seed, int32_t player, int32_t id) {
    uint64_t x = scene_seed;
    uint64_t a = splitmix(x);
    x = a ^ (static_cast<uint64_t>(static_cast<uint32_t>(player)) << 32 | static_cast<uint32_t>(id));
    return splitmix(x);
}

void Rng::reseed(uint64_t seed) {
    uint64_t x = seed;
    for (int i = 0; i < 4; ++i) s_[i] = splitmix(x);
}

uint64_t Rng::next() {
    const uint64_t r = rotl(s_[1] * 5, 7) * 9;
    const uint64_t t = s_[1] << 17;
    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = rotl(s_[3], 45);
    return r;
}

double Rng::uniform() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }

int Rng::below(int n) {
    if (n <= 1) return 0;
    // rejection keeps it unbiased
    const uint64_t un = static_cast<uint64_t>(n);
    const uint64_t lim = (~0ull) - (~0ull) % un;
    uint64_t v;
    do { v = next(); } while (v >= lim);
    return static_cast<int>(v % un);
}

int Rng::pick(const std::vector<float>& w) {
    double sum = 0;
    for (float x : w) {
        if (!(x >= 0)) return -1;
        sum += x;
    }
    if (!(sum > 0)) return -1;
    double u = uniform() * sum, acc = 0;
    for (size_t i = 0; i < w.size(); ++i) {
        acc += w[i];
        if (u < acc) return static_cast<int>(i);
    }
    for (size_t i = w.size(); i-- > 0;)
        if (w[i] > 0) return static_cast<int>(i);
    return -1;
}

void ForcedChoices::push(int32_t figure, const std::string& point, int32_t value) {
    q_[std::make_pair(figure, point)].push_back(value);
}

bool ForcedChoices::take(int32_t figure, const std::string& point, int32_t& value) {
    auto it = q_.find(std::make_pair(figure, point));
    if (it == q_.end() || it->second.empty()) return false;
    value = it->second.front();
    it->second.pop_front();
    if (it->second.empty()) q_.erase(it);
    return true;
}

size_t ForcedChoices::pending() const {
    size_t n = 0;
    for (auto& kv : q_) n += kv.second.size();
    return n;
}

}}  // namespace c5r::unitanim
