// src/wait_fast.h - inline hot paths of waiting and publishing, shared by wait.cpp and channel.cpp. Task 1209.
// The library is built without link-time optimisation, so calls between its .cpp files are real calls; on an empty
// round trip the cross-file calls cost ~70 ns (measured 2026-11-10 against a ThinLTO build of the same code). The
// parts every round trip runs are kept here, inline, behind the unchanged declarations in internal.h.
// Library-core rules (PROTOCOL.md section 11): C++11, no STL.
#pragma once
#include <emmintrin.h>
#include <intrin.h>

#include "internal.h"

namespace vpipc {
namespace waitfast {

// Pauses before the first clock read of a wait (task 1209: -24 ns per empty round trip pinned). Spent on top of the
// spin budget; a wait with spinUs == 0 does not pre-spin.
const uint32_t kPreSpin = 32;

inline bool any_changed(const volatile uint32_t* const* words, const uint32_t* seen, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i)
        if (*words[i] != seen[i]) return true;
    return false;
}

// One look, then (spinUs != 0) up to kPreSpin pause-and-look steps, without reading the clock. True when a watched
// word changed (counted as a spin wait, as wait_change counts it).
inline bool quick(const volatile uint32_t* const* words, const uint32_t* seen, uint32_t n, uint32_t spinUs,
                  ChannelStats* stats) {
    bool changed = any_changed(words, seen, n);
    for (uint32_t k = 0; !changed && spinUs && k < kPreSpin; ++k) {
        _mm_pause();
        changed = any_changed(words, seen, n);
    }
    if (changed) {
        _ReadWriteBarrier();
        if (stats) ++stats->spinWaits;
    }
    return changed;
}

// publish() of internal.h: store with a full fence (the Dekker pairing with the peer's announce), then wake the peer
// only if it announced sleep (plain read first, so the common case writes nothing on the peer's line).
inline void publish(WaitEnv& env, volatile uint32_t* word, uint32_t value, ChannelStats* stats) {
    InterlockedExchange((volatile LONG*)word, (LONG)value);
    if (*env.peerSleeping && InterlockedCompareExchange((volatile LONG*)env.peerSleeping, 0, 1) == 1) {
        SetEvent(env.peerEvent);
        if (stats) ++stats->wakesSent;
    }
}

}  // namespace waitfast
}  // namespace vpipc
