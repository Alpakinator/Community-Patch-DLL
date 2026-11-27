// tests/test_ring.cpp - RingProducer / RingConsumer (PROTOCOL.md section 4). Task 1201.
// C++03 library API only in tests (VC9 build): no vector::data(), std::thread, std::atomic, std::chrono.
#include <string.h>

#include <vector>

#include "check.h"
#include "internal.h"

using namespace vpipc;

namespace {

#pragma warning(push)
#pragma warning(disable : 4324)  // padded because RingIndexLine is alignas(64): intended
struct TestRing {
    RingIndexLine w;
    RingIndexLine r;
    std::vector<uint8_t> data;
    RingProducer p;
    RingConsumer c;
    explicit TestRing(uint32_t bytes, uint32_t startIndex = 0) : data(bytes, 0xCD) {
        memset(&w, 0, sizeof w);
        memset(&r, 0, sizeof r);
        w.index = startIndex;
        r.index = startIndex;
        p.init(&w, &r, &data[0], bytes);
        c.init(&w, &r, &data[0], bytes);
    }
    RingRecord at(uint32_t pos) const {
        RingRecord h;
        memcpy(&h, &data[pos], sizeof h);
        return h;
    }
};
#pragma warning(pop)

uint32_t rnd(uint32_t& s) {  // xorshift32
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

bool pop_one(RingConsumer& c, uint32_t& method, std::vector<uint8_t>& out) {
    const uint8_t* p = 0;
    uint32_t len = 0;
    if (!c.peek(method, p, len)) return false;
    out.assign(p, p + len);
    c.pop();
    return true;
}

double qpc_seconds() {
    LARGE_INTEGER t, f;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&f);
    return (double)t.QuadPart / (double)f.QuadPart;
}

}  // namespace

TEST(ring_basic_push_peek_pop) {
    TestRing t(4096);
    CHECK(t.c.empty());
    uint32_t m = 0;
    const uint8_t* pl = 0;
    uint32_t len = 0;
    CHECK(!t.c.peek(m, pl, len));
    t.c.pop();  // no-op without a peek
    CHECK_EQ(t.r.index, 0u);

    const char msg[] = "hello ring";
    REQUIRE(t.p.try_push(7, msg, sizeof msg));
    CHECK_EQ(t.w.index, ring_record_bytes(sizeof msg));
    CHECK_EQ(t.p.used(), ring_record_bytes(sizeof msg));
    REQUIRE(t.p.try_push(8, 0, 0));
    CHECK(!t.c.empty());

    REQUIRE(t.c.peek(m, pl, len));
    CHECK_EQ(m, 7u);
    CHECK_EQ(len, (uint32_t)sizeof msg);
    CHECK(memcmp(pl, msg, sizeof msg) == 0);
    REQUIRE(t.c.peek(m, pl, len));  // peek again: same record
    CHECK_EQ(m, 7u);
    CHECK_EQ(t.r.index, 0u);         // nothing released before pop
    t.c.pop();
    CHECK_EQ(t.r.index, ring_record_bytes(sizeof msg));
    REQUIRE(t.c.peek(m, pl, len));
    CHECK_EQ(m, 8u);
    CHECK_EQ(len, 0u);
    t.c.pop();
    CHECK(t.c.empty());
    CHECK_EQ(t.p.used(), 0u);
    CHECK(!t.c.corrupt());
}

TEST(ring_all_payload_lengths) {
    TestRing t(4096);
    std::vector<uint8_t> src(1100);
    for (size_t i = 0; i < src.size(); ++i) src[i] = (uint8_t)(i * 13 + 1);
    int bad = 0;
    for (uint32_t n = 0; n <= t.p.max_payload(); ++n) {
        const uint32_t align = n & 3u;
        if (!t.p.try_push(n, &src[align], n)) { ++bad; continue; }
        uint32_t m;
        std::vector<uint8_t> out;
        if (!pop_one(t.c, m, out)) { ++bad; continue; }
        bad += m != n || out.size() != n || (n && memcmp(&out[0], &src[align], n) != 0);
    }
    CHECK_EQ(bad, 0);
    CHECK(!t.c.corrupt());
}

TEST(ring_wrap_with_pad) {
    TestRing t(64);  // max payload 16
    CHECK_EQ(t.p.max_payload(), 16u);
    uint8_t buf[16];
    for (int i = 0; i < 16; ++i) buf[i] = (uint8_t)(0xA0 + i);
    // 3 records of 16 bytes (24 each) = 72 > 64: fill 48, consume, then the next needs a pad at 48.
    REQUIRE(t.p.try_push(1, buf, 16));
    REQUIRE(t.p.try_push(2, buf, 16));
    CHECK_EQ(t.w.index, 48u);
    uint32_t m;
    std::vector<uint8_t> out;
    REQUIRE(pop_one(t.c, m, out) && m == 1);
    REQUIRE(pop_one(t.c, m, out) && m == 2);
    REQUIRE(t.p.try_push(3, buf, 16));  // 16 left before the wrap, record is 24: pad 16 + record at 0
    CHECK_EQ(t.w.index, 48u + 16u + 24u);
    RingRecord pad = t.at(48);
    CHECK_EQ(pad.method, kRingPadMethod);
    CHECK_EQ(pad.length, 8u);
    RingRecord rec = t.at(0);
    CHECK_EQ(rec.method, 3u);
    CHECK_EQ(rec.length, 16u);
    CHECK_EQ(t.p.used(), 40u);
    REQUIRE(pop_one(t.c, m, out));
    CHECK_EQ(m, 3u);
    REQUIRE(out.size() == 16);
    CHECK(memcmp(&out[0], buf, 16) == 0);
    CHECK_EQ(t.r.index, 88u);  // the pad is released together with the record
    CHECK(t.c.empty());
    // A record that ends exactly at the wrap point needs no pad.
    TestRing u(64);
    REQUIRE(u.p.try_push(1, buf, 16));  // 0..24
    REQUIRE(u.p.try_push(2, buf, 16));  // 24..48
    REQUIRE(u.p.try_push(3, buf, 8));   // 48..64
    CHECK_EQ(u.w.index, 64u);
    REQUIRE(pop_one(u.c, m, out) && m == 1);
    REQUIRE(u.p.try_push(4, buf, 0));   // 0..8 after the wrap, no pad
    CHECK_EQ(u.w.index, 72u);
    REQUIRE(pop_one(u.c, m, out) && m == 2);
    REQUIRE(pop_one(u.c, m, out) && m == 3 && out.size() == 8);
    REQUIRE(pop_one(u.c, m, out) && m == 4 && out.empty());
    CHECK(!u.c.corrupt());
}

TEST(ring_full_detection) {
    TestRing t(64);
    uint8_t buf[16] = {0};
    REQUIRE(t.p.try_push(1, buf, 16));  // 24
    REQUIRE(t.p.try_push(2, buf, 16));  // 48
    REQUIRE(t.p.try_push(3, buf, 0));   // 56
    REQUIRE(t.p.try_push(4, buf, 0));   // 64: exactly full
    CHECK_EQ(t.p.used(), 64u);
    CHECK(!t.p.try_push(5, buf, 0));
    CHECK_EQ(t.w.index, 64u);  // a refused push publishes nothing
    uint32_t m;
    std::vector<uint8_t> out;
    REQUIRE(pop_one(t.c, m, out) && m == 1);  // frees 24
    CHECK(t.p.try_push(5, buf, 16));
    CHECK(!t.p.try_push(6, buf, 0));
    // Pad case: free space is enough for the record but not for pad + record.
    TestRing u(64);
    REQUIRE(u.p.try_push(1, buf, 16));  // 0..24
    REQUIRE(u.p.try_push(2, buf, 16));  // 24..48
    REQUIRE(pop_one(u.c, m, out) && m == 1);  // used 24, free 40, 16 to the wrap
    REQUIRE(u.p.try_push(3, buf, 16));  // pad 16 + 24 = 40: fits exactly
    CHECK_EQ(u.p.used(), 64u);
    TestRing v(64);
    REQUIRE(v.p.try_push(1, buf, 8));   // 0..16
    REQUIRE(v.p.try_push(2, buf, 16));  // 16..40
    REQUIRE(v.p.try_push(3, buf, 0));   // 40..48
    REQUIRE(pop_one(v.c, m, out) && m == 1);  // used 32, free 32, 16 to the wrap
    CHECK(!v.p.try_push(4, buf, 16));   // record 24 <= free, but pad 16 + 24 = 40 > 32
    CHECK_EQ(v.p.used(), 32u);
    CHECK_EQ(v.w.index, 48u);
    REQUIRE(pop_one(v.c, m, out) && m == 2);
    CHECK(v.p.try_push(4, buf, 16));
    REQUIRE(pop_one(v.c, m, out) && m == 3);
    REQUIRE(pop_one(v.c, m, out) && m == 4 && out.size() == 16);
}

TEST(ring_max_payload_enforced) {
    TestRing t(4096);
    CHECK_EQ(t.p.max_payload(), 1024u);
    CHECK_EQ(t.p.capacity(), 4096u);
    std::vector<uint8_t> big(2048, 0x5A);
    CHECK(!t.p.try_push(1, &big[0], 1025));
    CHECK_EQ(t.w.index, 0u);
    CHECK(t.p.try_push(1, &big[0], 1024));
    CHECK(!t.p.try_push(kRingPadMethod, &big[0], 4));  // reserved method
    // A max-size record always fits in an empty ring wherever the write position is.
    for (uint32_t i = 0; i < 64; ++i) {
        uint32_t m;
        std::vector<uint8_t> out;
        REQUIRE(pop_one(t.c, m, out));
        CHECK_EQ(out.size(), (size_t)1024);
        REQUIRE(t.p.try_push(1 + i, &big[0], 8 * i));  // walk the write position
        REQUIRE(pop_one(t.c, m, out));
        REQUIRE(t.p.try_push(1, &big[0], 1024));
    }
}

TEST(ring_u32_index_wrap) {
    TestRing t(256, 0xFFFFFF00u);
    uint32_t s = 99;
    uint32_t pushed = 0, popped = 0;
    std::vector<uint8_t> src(64);
    for (size_t i = 0; i < src.size(); ++i) src[i] = (uint8_t)i;
    std::vector<uint32_t> lens;
    while (popped < 2000) {
        while (pushed < 2000) {
            const uint32_t len = rnd(s) % 65;
            if (!t.p.try_push(pushed, &src[0], len)) break;  // full: the length is dropped, not reused
            lens.push_back(len);
            ++pushed;
        }
        uint32_t m;
        std::vector<uint8_t> out;
        while (pop_one(t.c, m, out)) {
            CHECK_EQ(m, popped);
            CHECK_EQ(out.size(), (size_t)lens[popped]);
            if (!out.empty()) CHECK(memcmp(&out[0], &src[0], out.size()) == 0);
            ++popped;
        }
    }
    CHECK(t.w.index < 0x00100000u);  // started at 0xFFFFFF00: wrapped through 2^32
    CHECK(!t.c.corrupt());
}

TEST(ring_corrupt_detection) {
    uint32_t m;
    const uint8_t* pl;
    uint32_t len;
    {  // impossible length
        TestRing t(64);
        RingRecord h = {17, 1};  // > 64 / 4
        memcpy(&t.data[0], &h, 8);
        t.w.index = 32;
        CHECK(!t.c.peek(m, pl, len));
        CHECK(t.c.corrupt());
        CHECK(!t.c.peek(m, pl, len));  // sticky
        t.c.pop();
        CHECK_EQ(t.r.index, 0u);
        t.w.index = 0;
        t.c.reset_local();  // clears it
        CHECK(!t.c.corrupt());
    }
    {  // malformed length 0xFFFFFFF0
        TestRing t(4096);
        RingRecord h = {0xFFFFFFF0u, 1};
        memcpy(&t.data[0], &h, 8);
        t.w.index = 16;
        CHECK(!t.c.peek(m, pl, len));
        CHECK(t.c.corrupt());
    }
    {  // record longer than what was published
        TestRing t(64);
        RingRecord h = {16, 1};  // needs 24
        memcpy(&t.data[0], &h, 8);
        t.w.index = 16;
        CHECK(!t.c.peek(m, pl, len));
        CHECK(t.c.corrupt());
    }
    {  // record crossing the wrap point
        TestRing t(64, 48);
        RingRecord h = {16, 1};  // 24 bytes from 48
        memcpy(&t.data[48], &h, 8);
        t.w.index = 48 + 24;
        CHECK(!t.c.peek(m, pl, len));
        CHECK(t.c.corrupt());
    }
    {  // pad record with the wrong length
        TestRing t(64, 48);
        RingRecord pad = {0, kRingPadMethod};
        memcpy(&t.data[48], &pad, 8);
        RingRecord h = {0, 1};
        memcpy(&t.data[0], &h, 8);
        t.w.index = 48 + 16 + 8;
        CHECK(!t.c.peek(m, pl, len));
        CHECK(t.c.corrupt());
    }
    {  // distance not a multiple of 8, or larger than the ring
        TestRing t(64);
        t.w.index = 12;
        CHECK(!t.c.peek(m, pl, len));
        CHECK(t.c.corrupt());
        TestRing u(64);
        u.w.index = 72;
        CHECK(!u.c.peek(m, pl, len));
        CHECK(u.c.corrupt());
    }
    {  // a pad published on its own is consumed, not corrupt
        TestRing t(64, 48);
        RingRecord pad = {8, kRingPadMethod};
        memcpy(&t.data[48], &pad, 8);
        t.w.index = 64;
        CHECK(!t.c.peek(m, pl, len));
        CHECK(!t.c.corrupt());
        CHECK_EQ(t.r.index, 64u);
        CHECK(t.c.empty());
    }
}

// ---------------------------------------------------------------------------------------- two-thread stress

namespace {
struct Stress {
    TestRing* ring;
    const uint8_t* src;
    uint32_t maxLen;
    uint32_t count;
    uint32_t seed;
    uint64_t sumP;
    uint64_t sumC;
    uint32_t errors;
    uint32_t fullWaits;
};

inline void spin_pause(uint32_t& n) {
    if (++n < 64) YieldProcessor();
    else { SwitchToThread(); n = 0; }
}

inline uint64_t mix(uint64_t h, uint32_t v) {
    h ^= v;
    h *= 0x100000001B3ull;
    return h;
}

inline uint32_t word_at(const uint8_t* p, uint32_t len, uint32_t off) {
    uint32_t v = 0;
    if (len >= off + 4) memcpy(&v, p + off, 4);
    else if (len > off) memcpy(&v, p + off, len - off);
    return v;
}

DWORD WINAPI producer_main(void* arg) {
    Stress& s = *static_cast<Stress*>(arg);
    uint32_t rng = s.seed;
    uint64_t h = 1469598103934665603ull;
    uint32_t spins = 0;
    for (uint32_t i = 0; i < s.count; ++i) {
        const uint32_t len = rnd(rng) % (s.maxLen + 1);
        const uint32_t off = rnd(rng) % s.maxLen;
        const uint8_t* p = s.src + off;
        while (!s.ring->p.try_push(i, p, len)) {
            ++s.fullWaits;
            spin_pause(spins);
        }
        h = mix(mix(mix(mix(h, i), len), word_at(p, len, 0)), len ? p[len - 1] : 0);
    }
    s.sumP = h;
    return 0;
}

DWORD WINAPI consumer_main(void* arg) {
    Stress& s = *static_cast<Stress*>(arg);
    uint32_t rng = s.seed;
    uint64_t h = 1469598103934665603ull;
    uint32_t spins = 0;
    for (uint32_t i = 0; i < s.count; ++i) {
        const uint32_t len = rnd(rng) % (s.maxLen + 1);
        const uint32_t off = rnd(rng) % s.maxLen;
        uint32_t m = 0, n = 0;
        const uint8_t* p = 0;
        while (!s.ring->c.peek(m, p, n)) {
            if (s.ring->c.corrupt()) {
                ++s.errors;
                return 1;
            }
            spin_pause(spins);
        }
        if (m != i || n != len || (n && memcmp(p, s.src + off, n) != 0)) {
            if (++s.errors < 5) printf("  record %u: method %u len %u (expected %u)\n", i, m, n, len);
        }
        h = mix(mix(mix(mix(h, m), n), word_at(p, n, 0)), n ? p[n - 1] : 0);
        s.ring->c.pop();
    }
    s.sumC = h;
    return 0;
}

void run_stress(uint32_t ringBytes, uint32_t count, uint32_t startIndex) {
    TestRing ring(ringBytes, startIndex);
    const uint32_t maxLen = ring.p.max_payload();
    std::vector<uint8_t> src(2 * maxLen + 4);
    uint32_t r = 4242;
    for (size_t i = 0; i < src.size(); ++i) src[i] = (uint8_t)rnd(r);
    Stress s;
    memset(&s, 0, sizeof s);
    s.ring = &ring;
    s.src = &src[0];
    s.maxLen = maxLen;
    s.count = count;
    s.seed = 0x9E3779B9u;
    const double t0 = qpc_seconds();
    HANDLE th[2];
    th[0] = CreateThread(0, 0, consumer_main, &s, CREATE_SUSPENDED, 0);
    th[1] = CreateThread(0, 0, producer_main, &s, CREATE_SUSPENDED, 0);
    REQUIRE(th[0] && th[1]);
    // The two threads hand the ring back and forth millions of times; on a loaded machine (other builds) a
    // descheduled peer costs a whole quantum per hand-off. Above-normal priority keeps the run time bounded.
    SetThreadPriority(th[0], THREAD_PRIORITY_ABOVE_NORMAL);
    SetThreadPriority(th[1], THREAD_PRIORITY_ABOVE_NORMAL);
    ResumeThread(th[0]);
    ResumeThread(th[1]);
    WaitForMultipleObjects(2, th, TRUE, INFINITE);
    CloseHandle(th[0]);
    CloseHandle(th[1]);
    const double dt = qpc_seconds() - t0;
    printf("  ring %u B, %u records of 0..%u B: %.2f s, %.1f ns/record, producer full-waits %u, end index 0x%08X\n",
           ringBytes, count, maxLen, dt, dt * 1e9 / count, s.fullWaits, (unsigned)ring.w.index);
    CHECK_EQ(s.errors, 0u);
    CHECK(s.sumP == s.sumC);
    CHECK(!ring.c.corrupt());
    CHECK(ring.c.empty());
    CHECK_EQ(ring.w.index, ring.r.index);
}
}  // namespace

TEST(ring_stress_two_threads_10M) { run_stress(4096, 10000000u, 0xFFF00000u); }

TEST(ring_stress_two_threads_default_size) { run_stress(64 * 1024, 300000u, 0); }

// ---------------------------------------------------------------------------------------- timing

TEST(bench_ring_push_pop_16) {
    TestRing t(64 * 1024);
    uint8_t payload[16];
    memset(payload, 0x11, sizeof payload);
    const uint32_t n = 20000000u;
    uint64_t sum = 0;
    const double t0 = qpc_seconds();
    for (uint32_t i = 0; i < n; ++i) {
        t.p.try_push(i, payload, 16);
        uint32_t m, len;
        const uint8_t* p;
        if (t.c.peek(m, p, len)) {
            sum += m + p[0];
            t.c.pop();
        }
    }
    const double dt = qpc_seconds() - t0;
    printf("  push+pop of a 16-byte payload: %.2f ns (%u iterations, single thread)\n", dt * 1e9 / n, n);
    CHECK(sum != 0);
    CHECK(t.c.empty());
}

// Task 1206: try_write writes without publishing; publish() makes the batch visible with one index store.
TEST(ring_write_then_publish_batch) {
    TestRing t(256);
    uint8_t buf[16];
    for (int i = 0; i < 16; ++i) buf[i] = (uint8_t)i;
    REQUIRE(t.p.try_write(1, buf, 16));
    REQUIRE(t.p.try_write(2, buf, 8));
    REQUIRE(t.p.try_write(3, buf, 0));
    CHECK_EQ(t.w.index, 0u);  // nothing visible yet
    CHECK_EQ(t.p.unpublished(), 24u + 16u + 8u);
    CHECK_EQ(t.p.used(), 48u);
    CHECK(t.c.empty());
    uint32_t m = 0;
    std::vector<uint8_t> out;
    CHECK(!pop_one(t.c, m, out));
    t.p.publish();
    CHECK_EQ(t.w.index, 48u);
    CHECK_EQ(t.p.unpublished(), 0u);
    t.p.publish();  // nothing pending: no store
    CHECK_EQ(t.w.index, 48u);
    REQUIRE(pop_one(t.c, m, out));
    CHECK_EQ(m, 1u);
    CHECK_EQ((uint32_t)out.size(), 16u);
    REQUIRE(pop_one(t.c, m, out));
    CHECK_EQ(m, 2u);
    REQUIRE(pop_one(t.c, m, out));
    CHECK_EQ(m, 3u);
    CHECK(!pop_one(t.c, m, out));
}

TEST(ring_write_full_with_unpublished_and_cached_read_index) {
    TestRing u(64);
    uint8_t buf[16] = {0};
    REQUIRE(u.p.try_write(1, buf, 16));  // 0..24
    REQUIRE(u.p.try_write(2, buf, 16));  // 24..48
    CHECK(!u.p.try_write(3, buf, 16));   // needs pad 16 + 24 = 40, only 16 free: full while unpublished
    CHECK_EQ(u.w.index, 0u);             // a refused write publishes nothing
    u.p.publish();
    CHECK_EQ(u.w.index, 48u);
    uint32_t m = 0;
    std::vector<uint8_t> out;
    REQUIRE(pop_one(u.c, m, out));       // read index 24: the producer's cached copy (0) is stale
    REQUIRE(u.p.try_write(3, buf, 16));  // re-reads the read index: 64 - 24 = 40 free, pad 16 + record 24
    CHECK_EQ(u.w.index, 48u);
    CHECK_EQ(u.p.unpublished(), 40u);
    u.p.publish();
    CHECK_EQ(u.w.index, 88u);
    REQUIRE(pop_one(u.c, m, out));
    CHECK_EQ(m, 2u);
    REQUIRE(pop_one(u.c, m, out));       // skips the pad
    CHECK_EQ(m, 3u);
    CHECK(!u.c.corrupt());
    CHECK(u.c.empty());
}

VPIPC_TEST_MAIN()
