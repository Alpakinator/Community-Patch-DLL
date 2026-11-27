// src/wait.cpp - spin, announce sleep, block (PROTOCOL.md section 6) and publish (section 3.5). Task 1202.
//
// wait_change(env, words, seen, n, spinUs, timeoutMs, stats)
//   1. One check of the watched words. timeoutMs == 0: that check is all (a poll; no spin, no block).
//   2. Spin: _mm_pause, check the words after every pause, read the clock (QueryPerformanceCounter) every
//      kSpinClockEvery pauses until spinUs has passed. spinUs == 0 skips the spin.
//   3. Announce sleep: InterlockedExchange(mySleeping, 1) (a full fence), then re-check the words.
//   4. Block: WaitForMultipleObjects({myEvent, peerProcess}, min(tickMs, time left)). After every return
//      (wake, tick, stale wake) the words are checked first, then the endpoint's verdict (peer gone, closed,
//      hung with failOnHung) and the deadline; otherwise it announces sleep again and blocks again.
//   The sleeping flag is cleared (plain store) on every return.
// Timeouts: timeoutMs == INFINITE (0xFFFFFFFF) applies the WaitConfig policy: callTimeoutMs when non-zero,
// otherwise no limit. Any other timeoutMs is used as given and callTimeoutMs is not applied. failOnHung
// applies to every wait. A signalled peer-process handle ends the wait with PeerDied (unless the words
// changed: a message published just before the peer died is still delivered).
//
// publish(env, word, value): InterlockedExchange(word, value) (full fence: the Dekker pairing with the
//   peer's announce in step 3), then a plain read of the peer's sleeping flag; only if it is 1,
//   InterlockedCompareExchange(peerSleeping, 0, 1) and SetEvent(peerEvent) when that CAS won.
// wake_peer(env): InterlockedCompareExchange(peerSleeping, 0, 1) (itself the full fence that orders an
//   earlier plain or release store, e.g. RingProducer::try_push's write index, before the flag read: the
//   internal.h contract; tested by test_wait ring_push_then_wake_peer_no_lost_wakeup) and SetEvent when it won.
//   No plain pre-read of the flag here, unlike publish(): without a preceding fence it could be stale.
// Hot path (wait, publish, wake_peer, ep_enter/ep_leave) calls no memcpy/strlen (slow in msvcr90 for small sizes).
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <emmintrin.h>
#include <intrin.h>

#include "endpoint_impl.h"
#include "wait_fast.h"

namespace vpipc {

namespace {

// Pauses between clock reads in the spin. Measured 2026-11-10 (i9-13900KS, tests/test_wait.cpp measure_costs,
// clang-cl Release): _mm_pause 33-47 ns, one QueryPerformanceCounter 24-34 ns, x86 and x64. Reading the clock every
// 8 pauses costs ~10% of the spin and honours the budget to within ~0.35 us.
const uint32_t kSpinClockEvery = 8;

uint64_t g_qpcFreq = 0;
volatile LONG g_qpcFreqReady = 0;

using waitfast::any_changed;

}  // namespace

uint64_t qpc_now() {
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return (uint64_t)li.QuadPart;
}

uint64_t qpc_frequency() {
    if (g_qpcFreqReady) return g_qpcFreq;
    LARGE_INTEGER li;
    QueryPerformanceFrequency(&li);
    g_qpcFreq = (uint64_t)li.QuadPart;
    InterlockedExchange(&g_qpcFreqReady, 1);
    return g_qpcFreq;
}

WaitResult wait_change(WaitEnv& env, const volatile uint32_t* const* words, const uint32_t* seen, uint32_t n,
                       uint32_t spinUs, uint32_t timeoutMs, ChannelStats* stats) {
    if (any_changed(words, seen, n)) {
        _ReadWriteBarrier();
        if (stats) ++stats->spinWaits;
        return WaitResult::Changed;
    }
    if (timeoutMs == 0) return WaitResult::Timeout;

    // Pre-spin (task 1209): most waits for a reply or the next back-to-back call end within a few hundred ns, so
    // look a few times before paying for the clock (QueryPerformanceCounter, 24-34 ns) and the deadline arithmetic.
    // waitfast::kPreSpin pauses (~1-1.5 us) are spent on top of spinUs; spinUs == 0 skips them.
    if (spinUs && waitfast::quick(words, seen, n, spinUs, stats)) return WaitResult::Changed;

    const WaitConfig& wc = ep_wait_config(env.ep);
    uint32_t limitMs = timeoutMs;
    if (timeoutMs == INFINITE && wc.callTimeoutMs) limitMs = wc.callTimeoutMs;
    const uint64_t freq = qpc_frequency();
    const uint64_t start = qpc_now();
    const uint64_t deadline = limitMs == INFINITE ? ~0ull : start + (uint64_t)limitMs * freq / 1000u;

    // 2. spin
    if (spinUs) {
        const uint64_t spinEnd = start + (uint64_t)spinUs * freq / 1000000u;
        for (;;) {
            for (uint32_t k = 0; k < kSpinClockEvery; ++k) {
                _mm_pause();
                if (any_changed(words, seen, n)) {
                    _ReadWriteBarrier();
                    if (stats) ++stats->spinWaits;
                    return WaitResult::Changed;
                }
            }
            if (qpc_now() >= spinEnd) break;
        }
    }

    // 3-4. announce, re-check, block
    HANDLE peer = ep_peer_process(env.ep);
    const uint32_t gen0 = ep_generation(env.ep);
    HANDLE hs[2] = {env.myEvent, peer};
    const DWORD nh = peer ? 2u : 1u;
    const uint32_t tick = wc.tickMs ? wc.tickMs : 1u;
    bool blocked = false;
    bool failedOnce = false;
    WaitResult res;
    for (;;) {
        InterlockedExchange((volatile LONG*)env.mySleeping, 1);
        if (any_changed(words, seen, n)) { res = WaitResult::Changed; break; }
        WaitResult v;
        if (env.ep && env.ep->wait_verdict(gen0, &v)) { res = v; break; }
        const uint64_t now = qpc_now();
        if (now >= deadline) { res = WaitResult::Timeout; break; }
        DWORD ms = tick;
        if (deadline != ~0ull) {
            const uint64_t left = (deadline - now) * 1000u / freq + 1u;
            if (left < ms) ms = (DWORD)left;
        }
        const DWORD r = WaitForMultipleObjects(nh, hs, FALSE, ms);
        blocked = true;
        if (any_changed(words, seen, n)) { res = WaitResult::Changed; break; }
        if (r == WAIT_OBJECT_0 + 1) { res = WaitResult::PeerDied; break; }
        if (r == WAIT_FAILED) {
            if (!failedOnce) {
                ep_log(env.ep, LogLevel::Error, "wait_change: WaitForMultipleObjects failed, error %u",
                       (unsigned)GetLastError());
                failedOnce = true;
            }
            Sleep(1);
        }
    }
    *env.mySleeping = 0;
    _ReadWriteBarrier();
    if (stats) {
        if (blocked) ++stats->blockedWaits;
        else ++stats->spinWaits;
    }
    return res;
}

void publish(WaitEnv& env, volatile uint32_t* word, uint32_t value, ChannelStats* stats) {
    waitfast::publish(env, word, value, stats);  // the body is inline in wait_fast.h (channel.cpp calls it directly)
}

bool wake_peer(WaitEnv& env, ChannelStats* stats) {
    if (InterlockedCompareExchange((volatile LONG*)env.peerSleeping, 0, 1) == 1) {
        SetEvent(env.peerEvent);
        if (stats) ++stats->wakesSent;
        return true;
    }
    return false;
}

}  // namespace vpipc
