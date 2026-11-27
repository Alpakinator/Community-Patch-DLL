// tests/test_wait.cpp - spin-then-block waiting, publish, wake-up costs (task 1202, PROTOCOL.md section 6).
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
// Single process except peer_died_*, which spawns this executable as a child (--child-sleep) and kills it.
#include "check.h"
#include "endpoint_impl.h"
#include "internal.h"
#include "support/test_lifecycle/proc_util.h"

#include <emmintrin.h>
#include <malloc.h>

using namespace vpipc;

namespace {

// One heap channel block and two unnamed auto-reset events: side A plays the client, side B the server.
struct Pair {
    ChannelBlock* block;
    HANDLE evA, evB;
    WaitEnv a, b;
    Pair(EndpointImpl* epA = nullptr, EndpointImpl* epB = nullptr) {
        block = (ChannelBlock*)_aligned_malloc(sizeof(ChannelBlock), 64);
        memset((void*)block, 0, sizeof(ChannelBlock));
        evA = CreateEventA(NULL, FALSE, FALSE, NULL);
        evB = CreateEventA(NULL, FALSE, FALSE, NULL);
        a.mySleeping = &block->clientCtl.sleeping;
        a.peerSleeping = &block->serverCtl.sleeping;
        a.myEvent = evA;
        a.peerEvent = evB;
        a.ep = epA;
        b.mySleeping = &block->serverCtl.sleeping;
        b.peerSleeping = &block->clientCtl.sleeping;
        b.myEvent = evB;
        b.peerEvent = evA;
        b.ep = epB;
    }
    ~Pair() {
        CloseHandle(evA);
        CloseHandle(evB);
        _aligned_free(block);
    }
};

WaitResult wait1(WaitEnv& env, const volatile uint32_t* w, uint32_t seen, uint32_t spinUs, uint32_t timeoutMs,
                 ChannelStats* st = nullptr) {
    const volatile uint32_t* words[1] = {w};
    return wait_change(env, words, &seen, 1, spinUs, timeoutMs, st);
}

const char* wr_name(WaitResult r) {
    switch (r) {
    case WaitResult::Changed: return "Changed";
    case WaitResult::PeerDied: return "PeerDied";
    case WaitResult::PeerClosed: return "PeerClosed";
    case WaitResult::PeerHung: return "PeerHung";
    case WaitResult::Timeout: return "Timeout";
    }
    return "?";
}

// ---- ping-pong ----
struct PingPong {
    Pair* p;
    uint32_t n;
    uint32_t spinUs;
    volatile LONG failures;
    ChannelStats statsA, statsB;
};

DWORD WINAPI pong_main(void* arg) {
    PingPong& pp = *(PingPong*)arg;
    for (uint32_t i = 1; i <= pp.n; ++i) {
        WaitResult r = wait1(pp.p->b, &pp.p->block->c2s.seq, i - 1, pp.spinUs, 10000, &pp.statsB);
        if (r != WaitResult::Changed || pp.p->block->c2s.seq != i) {
            printf("  pong: iteration %u: %s, seq %u\n", i, wr_name(r), (unsigned)pp.p->block->c2s.seq);
            InterlockedIncrement(&pp.failures);
            return 1;
        }
        pp.p->block->s2c.method = pp.p->block->c2s.method + 1;  // payload the other side verifies
        publish(pp.p->b, &pp.p->block->s2c.seq, i, &pp.statsB);
    }
    return 0;
}

void ping_pong(uint32_t n, uint32_t spinUs) {
    Pair p;
    PingPong pp;
    pp.p = &p;
    pp.n = n;
    pp.spinUs = spinUs;
    pp.failures = 0;
    tu::Thread t;
    t.start(&pong_main, &pp);
    double t0 = tu::qpc_us();
    for (uint32_t i = 1; i <= n; ++i) {
        p.block->c2s.method = i;
        publish(p.a, &p.block->c2s.seq, i, &pp.statsA);
        WaitResult r = wait1(p.a, &p.block->s2c.seq, i - 1, spinUs, 10000, &pp.statsA);
        if (r != WaitResult::Changed || p.block->s2c.method != i + 1) {
            printf("  ping: iteration %u: %s, method %u\n", i, wr_name(r), (unsigned)p.block->s2c.method);
            InterlockedIncrement(&pp.failures);
            break;
        }
    }
    double t1 = tu::qpc_us();
    CHECK(t.join(30000));
    CHECK_EQ(pp.failures, 0);
    printf("  %u round trips, spin %u us: %.3f s, %.2f us per round trip; ping blocked %llu / spun %llu, "
           "pong blocked %llu / spun %llu, wakes sent %llu + %llu\n",
           n, spinUs, (t1 - t0) / 1e6, (t1 - t0) / n, (unsigned long long)pp.statsA.blockedWaits,
           (unsigned long long)pp.statsA.spinWaits, (unsigned long long)pp.statsB.blockedWaits,
           (unsigned long long)pp.statsB.spinWaits, (unsigned long long)pp.statsA.wakesSent,
           (unsigned long long)pp.statsB.wakesSent);
    if (spinUs == 0) CHECK(pp.statsB.blockedWaits > n / 2);  // with no spin most waits really block
}

// ---- waits against a real peer process ----
struct DeathWait {
    WaitEnv* env;
    volatile uint32_t* word;
    uint32_t spinUs;
    uint32_t timeoutMs;
    WaitResult result;
    double returnedUs;
};

DWORD WINAPI death_wait_main(void* arg) {
    DeathWait& d = *(DeathWait*)arg;
    d.result = wait1(*d.env, d.word, 0, d.spinUs, d.timeoutMs);
    d.returnedUs = tu::qpc_us();
    return 0;
}

void peer_died(uint32_t spinUs) {
    tu::Child c = tu::spawn_self("--child-sleep 20000");
    REQUIRE(c.process != NULL);
    WaitConfig wc;
    wc.tickMs = 100;
    EndpointImpl* ep = EndpointImpl::create_detached_for_test(Side::Client, c.process, wc);
    REQUIRE(ep != nullptr);
    Pair p(ep, nullptr);
    DeathWait d;
    d.env = &p.a;
    d.word = &p.block->s2c.seq;
    d.spinUs = spinUs;
    d.timeoutMs = 10000;
    d.result = WaitResult::Changed;
    tu::Thread t;
    t.start(&death_wait_main, &d);
    Sleep(300);
    double killedUs = tu::qpc_us();
    TerminateProcess(c.process, 9);
    CHECK(t.join(5000));
    double ms = (d.returnedUs - killedUs) / 1000.0;
    printf("  spin %u us: wait returned %s %.2f ms after TerminateProcess (limit tick 100 + 50 ms)\n", spinUs,
           wr_name(d.result), ms);
    CHECK(d.result == WaitResult::PeerDied);
    CHECK(ms < 150.0);
    // A wait that starts after the peer is already dead returns at once.
    double s0 = tu::qpc_us();
    CHECK(wait1(p.a, &p.block->s2c.seq, 0, spinUs, 10000) == WaitResult::PeerDied);
    CHECK(tu::qpc_us() - s0 < 50000.0);
    // ... unless the word changed: a message published before the death is still delivered.
    p.block->s2c.seq = 5;
    CHECK(wait1(p.a, &p.block->s2c.seq, 0, spinUs, 10000) == WaitResult::Changed);
    EndpointImpl::destroy(ep);
    tu::join(c, 5000);
}

}  // namespace

TEST(null_endpoint_hooks) {
    CHECK(ep_enter(nullptr, 0, true) == Status::Ok);
    CHECK(ep_enter(nullptr, 7, false) == Status::Ok);
    ep_leave(nullptr, 0, true);
    ep_leave(nullptr, 3, true);  // without enter: harmless
    CHECK_EQ(ep_conn_state(nullptr), (uint32_t)kConnConnected);
    CHECK_EQ(ep_generation(nullptr), 1u);
    CHECK(ep_peer_process(nullptr) == nullptr);
    CHECK(!ep_peer_hung(nullptr));
    CHECK(ep_side(nullptr) == Side::Client);
    const WaitConfig& wc = ep_wait_config(nullptr);
    CHECK_EQ(wc.replySpinUs, 100u);  // default set from task 1205's measurements
    CHECK_EQ(wc.tickMs, 100u);
    CHECK_EQ(wc.callTimeoutMs, 0u);
    CHECK(!wc.failOnHung);
    ep_log(nullptr, LogLevel::Debug, "debug from test_wait (not printed) %d", 1);
    ep_log(nullptr, LogLevel::Info, "info from test_wait %s", "(printed to stderr)");
}

TEST(poll_and_timeouts) {
    Pair p;
    ChannelStats st;
    CHECK(wait1(p.a, &p.block->s2c.seq, 0, 200, 0, &st) == WaitResult::Timeout);  // poll
    p.block->s2c.seq = 1;
    CHECK(wait1(p.a, &p.block->s2c.seq, 0, 200, 0, &st) == WaitResult::Changed);
    CHECK_EQ(*p.a.mySleeping, 0u);
    double t0 = tu::qpc_us();
    CHECK(wait1(p.a, &p.block->s2c.seq, 1, 0, 60, &st) == WaitResult::Timeout);
    double ms = (tu::qpc_us() - t0) / 1000.0;
    printf("  60 ms timeout took %.1f ms\n", ms);
    CHECK(ms >= 59.0 && ms < 200.0);
    CHECK_EQ(*p.a.mySleeping, 0u);
    // Multiple words: a change in the second one ends the wait.
    const volatile uint32_t* words[2] = {&p.block->s2c.seq, &p.block->s2cWrite.index};
    uint32_t seen[2] = {1, 0};
    p.block->s2cWrite.index = 8;
    CHECK(wait_change(p.a, words, seen, 2, 0, 1000, &st) == WaitResult::Changed);
    // callTimeoutMs applies to INFINITE waits only; an explicit timeout is used as given.
    WaitConfig wc;
    wc.callTimeoutMs = 80;
    EndpointImpl* ep = EndpointImpl::create_detached_for_test(Side::Client, NULL, wc);
    REQUIRE(ep != nullptr);
    Pair q(ep, nullptr);
    t0 = tu::qpc_us();
    CHECK(wait1(q.a, &q.block->s2c.seq, 0, 0, INFINITE) == WaitResult::Timeout);
    ms = (tu::qpc_us() - t0) / 1000.0;
    printf("  callTimeoutMs 80: INFINITE wait returned Timeout after %.1f ms\n", ms);
    CHECK(ms >= 79.0 && ms < 250.0);
    t0 = tu::qpc_us();
    CHECK(wait1(q.a, &q.block->s2c.seq, 0, 0, 200) == WaitResult::Timeout);
    ms = (tu::qpc_us() - t0) / 1000.0;
    CHECK(ms >= 199.0 && ms < 400.0);
    EndpointImpl::destroy(ep);
}

TEST(close_ends_wait) {
    WaitConfig wc;
    wc.tickMs = 50;
    EndpointImpl* ep = EndpointImpl::create_detached_for_test(Side::Server, NULL, wc);
    REQUIRE(ep != nullptr);
    Pair p(nullptr, ep);
    DeathWait d;
    d.env = &p.b;
    d.word = &p.block->c2s.seq;
    d.spinUs = 0;
    d.timeoutMs = 10000;
    d.result = WaitResult::Changed;
    tu::Thread t;
    t.start(&death_wait_main, &d);
    Sleep(100);
    double t0 = tu::qpc_us();
    ep->close();
    CHECK(t.join(5000));
    printf("  close(): wait returned %s after %.1f ms\n", wr_name(d.result), (d.returnedUs - t0) / 1000.0);
    CHECK(d.result == WaitResult::PeerClosed);
    CHECK(d.returnedUs - t0 < 150000.0);
    EndpointImpl::destroy(ep);
}

TEST(publish_and_wake_peer) {
    Pair p;
    ChannelStats st;
    publish(p.a, &p.block->c2s.seq, 1, &st);  // peer not sleeping: no event
    CHECK_EQ(st.wakesSent, 0ull);
    CHECK(WaitForSingleObject(p.evB, 0) == WAIT_TIMEOUT);
    p.block->serverCtl.sleeping = 1;
    publish(p.a, &p.block->c2s.seq, 2, &st);
    CHECK_EQ(st.wakesSent, 1ull);
    CHECK_EQ(p.block->serverCtl.sleeping, 0u);
    CHECK(WaitForSingleObject(p.evB, 0) == WAIT_OBJECT_0);
    CHECK(!wake_peer(p.a, &st));
    p.block->serverCtl.sleeping = 1;
    CHECK(wake_peer(p.a, &st));
    CHECK_EQ(st.wakesSent, 2ull);
    CHECK(WaitForSingleObject(p.evB, 0) == WAIT_OBJECT_0);
}

TEST(pingpong_1m_spin0) { ping_pong(1000000, 0); }
TEST(pingpong_1m_spin200) { ping_pong(1000000, 200); }

TEST(peer_died_spin0) { peer_died(0); }
TEST(peer_died_spin200) { peer_died(200); }

// ---- the wake_peer fence contract (internal.h): a ring push publishes with a release store only ----
namespace {
struct RingWake {
    Pair* p;
    uint8_t* data;
    uint32_t n;
    volatile LONG failures;
    ChannelStats statsP, statsC;
};
DWORD WINAPI ring_consumer_main(void* arg) {
    RingWake& rw = *(RingWake*)arg;
    RingConsumer c;
    c.init(&rw.p->block->c2sWrite, &rw.p->block->c2sRead, rw.data, 4096);
    uint32_t expect = 1;
    while (expect <= rw.n) {
        const uint32_t w = *c.write_index_word();
        uint32_t method, len;
        const uint8_t* payload;
        bool popped = false;
        while (c.peek(method, payload, len)) {
            uint32_t v;
            memcpy(&v, payload, 4);
            if (method != 1 || len != 4 || v != expect) {
                printf("  consumer: record %u has method %u len %u value %u\n", expect, method, len, v);
                InterlockedIncrement(&rw.failures);
                return 1;
            }
            c.pop();
            ++expect;
            popped = true;
        }
        if (popped) wake_peer(rw.p->b, &rw.statsC);  // the read index moved: wake a producer waiting for space
        if (expect > rw.n) break;
        if (wait1(rw.p->b, c.write_index_word(), w, 0, 10000, &rw.statsC) != WaitResult::Changed) {
            printf("  consumer: lost wake-up waiting for record %u\n", expect);
            InterlockedIncrement(&rw.failures);
            return 1;
        }
    }
    return 0;
}
}  // namespace

TEST(ring_push_then_wake_peer_no_lost_wakeup) {
    Pair p;
    RingWake rw;
    rw.p = &p;
    rw.data = (uint8_t*)_aligned_malloc(4096, 64);
    rw.n = 300000;
    rw.failures = 0;
    RingProducer prod;
    prod.init(&p.block->c2sWrite, &p.block->c2sRead, rw.data, 4096);
    tu::Thread t;
    t.start(&ring_consumer_main, &rw);
    double t0 = tu::qpc_us();
    for (uint32_t i = 1; i <= rw.n && !rw.failures; ++i) {
        for (;;) {
            const uint32_t r = *prod.read_index_word();
            if (prod.try_push(1, &i, 4)) break;
            if (wait1(p.a, prod.read_index_word(), r, 0, 10000, &rw.statsP) != WaitResult::Changed) {
                printf("  producer: lost wake-up waiting for ring space at record %u\n", i);
                InterlockedIncrement(&rw.failures);
                break;
            }
        }
        wake_peer(p.a, &rw.statsP);  // after a release-store publish: wake_peer must fence
    }
    CHECK(t.join(30000));
    CHECK_EQ(rw.failures, 0);
    printf("  %u posts with spin 0 in %.2f s: consumer blocked %llu times, producer blocked %llu times, wakes %llu + %llu\n",
           rw.n, (tu::qpc_us() - t0) / 1e6, (unsigned long long)rw.statsC.blockedWaits,
           (unsigned long long)rw.statsP.blockedWaits, (unsigned long long)rw.statsP.wakesSent,
           (unsigned long long)rw.statsC.wakesSent);
    // The consumer really went to sleep, many times. Was > 1000 until 2026-11-10: a fast run (0.04 s) blocks only
    // 700-950 times, so the check failed ~2 runs in 9 (x86 Debug, coordinator, no wake-up lost in any run).
    CHECK(rw.statsC.blockedWaits > 100);
    _aligned_free(rw.data);
}

// ---- measurements (printed; recorded in tracker/issues/1202) ----
namespace {
struct WakeBench {
    Pair* p;
    uint32_t n;
    uint32_t spinUs;
    std::vector<double> retUs;  // when the waiter returned
    volatile LONG ready;
};
DWORD WINAPI wake_waiter_main(void* arg) {
    WakeBench& w = *(WakeBench*)arg;
    for (uint32_t i = 1; i <= w.n; ++i) {
        WaitResult r = wait1(w.p->b, &w.p->block->c2s.seq, i - 1, w.spinUs, 10000);
        w.retUs[i - 1] = tu::qpc_us();
        if (r != WaitResult::Changed) return 1;
        publish(w.p->b, &w.p->block->s2c.seq, i, nullptr);  // ack: the publisher may start the next gap
    }
    return 0;
}
void wake_latency(uint32_t n, uint32_t spinUs, bool waitForSleep) {
    Pair p;
    WakeBench w;
    w.p = &p;
    w.n = n;
    w.spinUs = spinUs;
    w.retUs.resize(n);
    std::vector<double> sendUs(n), pubCost(n);
    tu::Thread t;
    t.start(&wake_waiter_main, &w);
    for (uint32_t i = 1; i <= n; ++i) {
        if (waitForSleep) {
            while (p.block->serverCtl.sleeping == 0) _mm_pause();  // the waiter has announced sleep
            double g = tu::qpc_us() + 20.0;
            while (tu::qpc_us() < g) _mm_pause();  // and is inside WaitForMultipleObjects by now
        } else {
            double g = tu::qpc_us() + 5.0;
            while (tu::qpc_us() < g) _mm_pause();
        }
        double s = tu::qpc_us();
        publish(p.a, &p.block->c2s.seq, i, nullptr);
        double e = tu::qpc_us();
        sendUs[i - 1] = s;
        pubCost[i - 1] = e - s;
        if (wait1(p.a, &p.block->s2c.seq, i - 1, 1000, 10000) != WaitResult::Changed) {
            CHECK(false);
            break;
        }
    }
    CHECK(t.join(30000));
    std::vector<double> lat(n);
    for (uint32_t i = 0; i < n; ++i) lat[i] = w.retUs[i] - sendUs[i];
    printf("  wake latency (%s, spin %u us, %u samples): median %.2f us, p90 %.2f, p99 %.2f, max %.2f; "
           "publish call median %.2f us, p99 %.2f\n",
           waitForSleep ? "waiter blocked" : "waiter spinning", spinUs, n, tu::percentile(lat, 0.5),
           tu::percentile(lat, 0.9), tu::percentile(lat, 0.99), tu::percentile(lat, 1.0),
           tu::percentile(pubCost, 0.5), tu::percentile(pubCost, 0.99));
}
}  // namespace

TEST(measure_costs) {
    printf("  pointer bits %u, QPC frequency %llu Hz\n", (unsigned)(sizeof(void*) * 8),
           (unsigned long long)qpc_frequency());
    const uint32_t N = 2000000;
    double t0 = tu::qpc_us();
    for (uint32_t i = 0; i < N; ++i) _mm_pause();
    double pauseNs = (tu::qpc_us() - t0) * 1000.0 / N;
    volatile uint64_t sink = 0;
    t0 = tu::qpc_us();
    for (uint32_t i = 0; i < N; ++i) sink += qpc_now();
    double qpcNs = (tu::qpc_us() - t0) * 1000.0 / N;
    Pair p;
    t0 = tu::qpc_us();
    for (uint32_t i = 0; i < N; ++i) publish(p.a, &p.block->c2s.seq, i, nullptr);
    double pubNs = (tu::qpc_us() - t0) * 1000.0 / N;
    const uint32_t M = 200000;
    t0 = tu::qpc_us();
    for (uint32_t i = 0; i < M; ++i) {
        p.block->serverCtl.sleeping = 1;
        publish(p.a, &p.block->c2s.seq, i, nullptr);
    }
    double pubSleepNs = (tu::qpc_us() - t0) * 1000.0 / M;
    t0 = tu::qpc_us();
    for (uint32_t i = 0; i < N; ++i) wake_peer(p.a, nullptr);
    double wakeNoneNs = (tu::qpc_us() - t0) * 1000.0 / N;
    printf("  _mm_pause %.2f ns; one clock check (QueryPerformanceCounter) %.2f ns; spin check interval "
           "8 pauses = %.0f ns\n", pauseNs, qpcNs, 8 * pauseNs + qpcNs);
    printf("  publish, peer not sleeping %.2f ns; publish, peer flag set (CAS + SetEvent, no thread waiting) "
           "%.0f ns; wake_peer, peer not sleeping %.2f ns\n", pubNs, pubSleepNs, wakeNoneNs);
    wake_latency(20000, 0, true);
    wake_latency(20000, 200, false);
}

int main(int argc, char** argv) {
    if (argc >= 3 && strcmp(argv[1], "--child-sleep") == 0) {
        Sleep((DWORD)atoi(argv[2]));
        return 0;
    }
    return vpipc_test::run_all(argc, argv);
}
