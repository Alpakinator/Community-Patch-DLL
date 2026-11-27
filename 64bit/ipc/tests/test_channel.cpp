// tests/test_channel.cpp - vpipc::Channel in one process (task 1203). Two ChannelImpls (client side and
// server side) over one block of memory, unnamed auto-reset events, ep = nullptr.
// Every test runs under a watchdog: a hang prints the test name and terminates the process (exit 3).
// Test code: C++03 library API only (CreateThread, QueryPerformanceCounter, Interlocked*).
#include <intrin.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include "check.h"
#include "internal.h"

using namespace vpipc;

#ifdef _MSC_VER
#pragma warning(disable : 4324)  // Pair holds an alignas(64) ChannelDesc: padding intended
#endif

// ---------------------------------------------------------------------------------------------------------
// infrastructure
// ---------------------------------------------------------------------------------------------------------
struct WatchdogArgs {
    HANDLE done;
    DWORD limitMs;
    const char* name;
};
static DWORD WINAPI watchdog_main(void* p) {
    WatchdogArgs* a = (WatchdogArgs*)p;
    if (WaitForSingleObject(a->done, a->limitMs) == WAIT_TIMEOUT) {
        printf("  WATCHDOG: %s did not finish in %u ms: hang, terminating\n", a->name, (unsigned)a->limitMs);
        fflush(stdout);
        TerminateProcess(GetCurrentProcess(), 3);
    }
    return 0;
}
struct Watchdog {
    WatchdogArgs a;
    HANDLE th;
    Watchdog(const char* name, DWORD limitMs) {
        a.done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        a.limitMs = limitMs;
        a.name = name;
        th = CreateThread(nullptr, 0, &watchdog_main, &a, 0, nullptr);
    }
    ~Watchdog() {
        SetEvent(a.done);
        WaitForSingleObject(th, INFINITE);
        CloseHandle(th);
        CloseHandle(a.done);
    }
};
#define WATCHDOG(sec) Watchdog wd_(__FUNCTION__, (sec) * 1000u)

static uint32_t fnv_hash(const uint8_t* p, uint32_t n) {
    uint32_t h = 2166136261u;
    uint32_t i = 0;
    for (; i + 4 <= n; i += 4) {
        uint32_t v;
        memcpy(&v, p + i, 4);
        h = (h ^ v) * 16777619u;
    }
    for (; i < n; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}
static void fill_pattern(uint8_t* p, uint32_t n, uint32_t seed) {
    uint32_t x = seed * 2654435761u + 1u;
    uint32_t i = 0;
    for (; i + 4 <= n; i += 4) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        memcpy(p + i, &x, 4);
    }
    for (; i < n; ++i) p[i] = (uint8_t)(i * 31u + seed);
}
static uint32_t round4(uint32_t n) { return (n + 3u) & ~3u; }
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 1u) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    uint32_t below(uint32_t n) { return n ? next() % n : 0; }
};

// ---- methods ----
enum {
    M_EMPTY = 1,
    M_ADD3,
    M_ECHO,
    M_HASH,
    M_TYPES,
    M_TYPES_PEER,
    M_NEST,
    M_FAIL,
    M_PEER_ECHO,
    M_POSTCHECK,
    M_GET_POSTCALL,
    M_POSTCALL_BACK,
    M_GET_BIG,
    M_QUIT,
    M_IDCB,
    M_ID2,
    M_ACTIVE,
    M_ACTIVE_PEER,
    // posts
    P_SEQ = 100,
    P_THAT_CALLS,
    P_BIG,
    P_TIMED,
};

struct Ctx {
    volatile LONG quit;
    uint32_t postsSeen;
    uint32_t expectedSeq;
    uint32_t postSeqErrors;
    uint32_t postCallRan;
    Status postCallStatus;
    uint32_t nestedProtocolErrors;
    uint32_t bigPosts;
    uint32_t bigLen;
    uint32_t handlerErrors;
    Ctx() { memset(this, 0, sizeof(*this)); }
};

// Every value type of PROTOCOL.md section 5, written and checked identically on both sides.
static char g_long[1001];
static void write_types(Writer& w) {
    w.i32(-123456789);
    w.u32(0xDEADBEEFu);
    w.i64(-0x123456789ABCDEFLL);
    w.u64(0xFEDCBA9876543210ULL);
    w.f32(3.5f);
    w.f32(-0.0f);
    w.f64(-2.25e300);
    w.b(true);
    w.b(false);
    w.e(Side::Server);
    w.str((const char*)0);
    w.str("");
    w.str("hello");
    w.str(g_long);
    w.wstr((const wchar_t*)0);
    w.wstr(L"wé中");
    static const uint8_t raw[13] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
    w.bytes(raw, 13);
    Handle h;
    h.type = 5;
    h.id = -77;
    w.handle(h);
}
static bool check_types(Reader& r) {
    bool ok = true;
    ok &= r.i32() == -123456789;
    ok &= r.u32() == 0xDEADBEEFu;
    ok &= r.i64() == -0x123456789ABCDEFLL;
    ok &= r.u64() == 0xFEDCBA9876543210ULL;
    ok &= r.f32() == 3.5f;
    float nz = r.f32();
    uint32_t nzBits;
    memcpy(&nzBits, &nz, 4);
    ok &= nzBits == 0x80000000u;
    ok &= r.f64() == -2.25e300;
    ok &= r.b() == true;
    ok &= r.b() == false;
    ok &= r.e<Side>() == Side::Server;
    uint32_t n = 99;
    ok &= r.cstr(&n) == nullptr && n == 0;
    const char* s = r.cstr(&n);
    ok &= s != nullptr && n == 0 && s[0] == 0;
    s = r.cstr(&n);
    ok &= s != nullptr && n == 5 && strcmp(s, "hello") == 0;
    s = r.cstr(&n);
    ok &= s != nullptr && n == 1000 && strcmp(s, g_long) == 0;
    ok &= r.wcstr(&n) == nullptr;
    const wchar_t* ws = r.wcstr(&n);
    ok &= ws != nullptr && n == 3 && wcscmp(ws, L"wé中") == 0;
    const uint8_t* b = r.bytes(&n);
    ok &= b != nullptr && n == 13 && b[0] == 1 && b[12] == 13;
    Handle h = r.handle();
    ok &= h.type == 5 && h.id == -77;
    ok &= r.ok() && r.remaining() == 0;
    return ok;
}

static void post_seq(Channel& ch, uint32_t seq, Status* st) {
    Writer& w = ch.begin_post(P_SEQ);
    w.u32(seq);
    uint32_t len = (seq * 7u) % 41u;
    uint8_t buf[64];
    fill_pattern(buf, len, seq);
    w.bytes(buf, len);
    *st = ch.end_post();
}

static void on_call(void* vctx, Channel& ch, uint32_t method, Reader& args, Writer& ret) {
    Ctx* ctx = (Ctx*)vctx;
    switch (method) {
    case M_EMPTY: break;
    case M_ADD3: {
        int32_t a = args.i32(), b = args.i32(), c = args.i32();
        ret.i32(a + b + c);
        break;
    }
    case M_ECHO: {
        uint32_t n = args.remaining();
        uint8_t* p = ret.reserve(n);
        if (p && n) memcpy(p, args.cursor(), n);
        break;
    }
    case M_HASH: {
        uint32_t n = args.remaining();
        ret.u32(fnv_hash(args.cursor(), n));
        ret.u32(n);
        break;
    }
    case M_TYPES:
        if (!check_types(args)) {
            ++ctx->handlerErrors;
            ch.set_return_status(kRetHandlerError);
            ret.str("types mismatch");
            break;
        }
        write_types(ret);
        break;
    case M_TYPES_PEER: {
        Writer& w = ch.begin_call(M_TYPES);
        write_types(w);
        Reader r;
        Status s = ch.end_call(r);
        ret.u32(s == Status::Ok && check_types(r) ? 1u : 0u);
        break;
    }
    case M_NEST: {
        int32_t n = args.i32();
        if (n <= 0) {
            ret.i32(0);
            break;
        }
        Writer& w = ch.begin_call(M_NEST);
        w.i32(n - 1);
        Reader r;
        Status s = ch.end_call(r);
        if (s == Status::Ok) {
            ret.i32(r.i32() + 1);
        } else {
            // pass the innermost failure up unchanged
            char buf[256];
            const char* inner = s == Status::RemoteError ? r.cstr() : nullptr;
            if (inner)
                strncpy_s(buf, sizeof(buf), inner, _TRUNCATE);
            else
                _snprintf_s(buf, sizeof(buf), _TRUNCATE, "nested call at n=%d failed: %s", n - 1, status_name(s));
            if (s == Status::ProtocolError) ++ctx->nestedProtocolErrors;
            ch.set_return_status(kRetHandlerError);
            ret.str(buf);
        }
        break;
    }
    case M_FAIL:
        ch.set_return_status(kRetHandlerError);
        ret.str("handler failed on purpose (42)");
        break;
    case M_PEER_ECHO: {
        uint32_t n = args.u32(), seed = args.u32();
        Writer& w = ch.begin_call(M_ECHO);
        uint8_t* p = w.reserve(n);
        uint32_t want = 0;
        if (p) {
            fill_pattern(p, round4(n), seed);
            want = fnv_hash(p, round4(n));
        }
        Reader r;
        Status s = ch.end_call(r);
        ret.u32(s == Status::Ok && p && r.remaining() == round4(n) && fnv_hash(r.cursor(), r.remaining()) == want);
        break;
    }
    case M_POSTCHECK: {
        uint32_t expected = args.u32(), k = args.u32(), start = args.u32();
        uint32_t ok = ctx->postsSeen == expected && ctx->postSeqErrors == 0;
        for (uint32_t j = 0; j < k; ++j) {
            Status st;
            post_seq(ch, start + j, &st);
            if (st != Status::Ok) ok = 0;
        }
        ret.u32(ok);
        ret.u32(ctx->postsSeen);
        break;
    }
    case M_GET_POSTCALL:
        ret.u32(ctx->postCallRan);
        ret.u32((uint32_t)ctx->postCallStatus);
        break;
    case M_POSTCALL_BACK: {
        ch.begin_post(P_THAT_CALLS);
        ret.u32(ch.end_post() == Status::Ok);
        break;
    }
    case M_GET_BIG:
        ret.u32(ctx->bigPosts);
        ret.u32(ctx->bigLen);
        break;
    case M_QUIT: _InterlockedExchange(&ctx->quit, 1); break;
    case M_IDCB: {
        uint32_t t = args.u32(), i = args.u32();
        Writer& w = ch.begin_call(M_ID2);
        w.u32(t);
        w.u32(i);
        Reader r;
        Status s = ch.end_call(r);
        uint32_t v = r.u32();
        if (s != Status::Ok || v != t * 1000003u + i * 7u) {
            ++ctx->handlerErrors;
            ch.set_return_status(kRetHandlerError);
            ret.str("callback mismatch");
            break;
        }
        ret.u32(v + 1u);
        break;
    }
    case M_ID2: {
        uint32_t t = args.u32(), i = args.u32();
        ret.u32(t * 1000003u + i * 7u);
        break;
    }
    case M_ACTIVE: ret.u32(channel_active(ch.impl())); break;
    case M_ACTIVE_PEER: {
        uint32_t mine = channel_active(ch.impl());
        ch.begin_call(M_ACTIVE);
        Reader r;
        Status s = ch.end_call(r);
        ret.u32(s == Status::Ok ? r.u32() : 0xFFFFFFFFu);
        ret.u32(mine);
        break;
    }
    default: {
        char buf[64];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "unknown method %u", method);
        ch.set_return_status(kRetUnknownMethod);
        ret.str(buf);
        break;
    }
    }
}

static volatile uint64_t g_timedRecv[64];  // P_TIMED: receive time per sequence number (bug 1208 test)

static void on_post(void* vctx, Channel& ch, uint32_t method, Reader& args) {
    Ctx* ctx = (Ctx*)vctx;
    switch (method) {
    case P_SEQ: {
        uint32_t seq = args.u32();
        uint32_t len = 0;
        const uint8_t* p = args.bytes(&len);
        uint8_t buf[64];
        uint32_t want = (seq * 7u) % 41u;
        fill_pattern(buf, want, seq);
        if (seq != ctx->expectedSeq || len != want || (len && memcmp(p, buf, len) != 0) || !args.ok())
            ++ctx->postSeqErrors;
        ctx->expectedSeq = seq + 1;
        ++ctx->postsSeen;
        break;
    }
    case P_THAT_CALLS: {
        ch.begin_call(M_EMPTY);
        Reader r;
        ctx->postCallStatus = ch.end_call(r);
        ctx->postCallRan = 1;
        break;
    }
    case P_TIMED: {
        uint32_t i = args.u32();
        if (i < sizeof(g_timedRecv) / sizeof(g_timedRecv[0])) g_timedRecv[i] = qpc_now();
        break;
    }
    case P_BIG:
        ++ctx->bigPosts;
        ctx->bigLen = args.remaining();
        break;
    default: break;
    }
}

// Both sides of one channel over private memory.
struct Pair {
    uint8_t* mem;
    ChannelBlock* block;
    ChannelDesc desc;
    HANDLE evC, evS;
    ChannelImpl* ci;
    ChannelImpl* si;
    Ctx cctx, sctx;
    explicit Pair(uint32_t dataBytes = 256 * 1024, uint32_t ringBytes = 64 * 1024, bool shared = false,
                  Side initiator = Side::Client, bool handlers = true) {
        uint32_t bytes = channel_block_bytes(dataBytes, ringBytes);
        mem = (uint8_t*)VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        block = (ChannelBlock*)mem;
        memset(&desc, 0, sizeof(desc));
        desc.initiator = initiator == Side::Server ? kInitiatorServer : kInitiatorClient;
        desc.flags = shared ? kChannelShared : 0;
        desc.dataBytes = dataBytes;
        desc.ringBytes = ringBytes;
        strcpy_s(desc.name, sizeof(desc.name), "test");
        evC = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        evS = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        channel_reset_shared(block, desc);
        ci = make(Side::Client);
        si = make(Side::Server);
        if (handlers) {
            c().set_handlers(&on_call, &on_post, &cctx);
            s().set_handlers(&on_call, &on_post, &sctx);
        }
    }
    ChannelImpl* make(Side side) {
        ChannelEnv env;
        env.side = side;
        env.index = 0;
        env.block = block;
        env.desc = desc;
        env.data = mem + sizeof(ChannelBlock);
        env.ringC2S = env.data + desc.dataBytes;
        env.ringS2C = env.ringC2S + desc.ringBytes;
        bool client = side == Side::Client;
        env.wait.mySleeping = client ? &block->clientCtl.sleeping : &block->serverCtl.sleeping;
        env.wait.peerSleeping = client ? &block->serverCtl.sleeping : &block->clientCtl.sleeping;
        env.wait.myEvent = client ? evC : evS;
        env.wait.peerEvent = client ? evS : evC;
        env.wait.ep = nullptr;
        env.ep = nullptr;
        return channel_create(env);
    }
    ~Pair() {
        channel_destroy(ci);
        channel_destroy(si);
        CloseHandle(evC);
        CloseHandle(evS);
        VirtualFree(mem, 0, MEM_RELEASE);
    }
    Channel& c() { return channel_public(ci); }
    Channel& s() { return channel_public(si); }
};

// A thread serving one side of a channel until M_QUIT.
struct Server {
    Channel* ch;
    Ctx* ctx;
    HANDLE th;
    Status last;
    static DWORD WINAPI main(void* p) {
        Server* s = (Server*)p;
        while (!s->ctx->quit) {
            Status st = s->ch->serve(100);
            if (st == Status::Timeout) continue;
            if (st != Status::Ok) {
                s->last = st;
                break;
            }
        }
        return 0;
    }
    Server(Channel& c, Ctx& x) : ch(&c), ctx(&x), th(nullptr), last(Status::Ok) {
        _InterlockedExchange(&ctx->quit, 0);
        th = CreateThread(nullptr, 0, &Server::main, this, 0, nullptr);
    }
    // Ask the server to stop with a call on `client`, then join. True if it stopped cleanly.
    bool stop(Channel& client) {
        client.begin_call(M_QUIT);
        Reader r;
        Status s = client.end_call(r);
        bool ok = WaitForSingleObject(th, 10000) == WAIT_OBJECT_0;
        CloseHandle(th);
        th = nullptr;
        return ok && s == Status::Ok && last == Status::Ok;
    }
    ~Server() {
        if (th) {
            _InterlockedExchange(&ctx->quit, 1);
            WaitForSingleObject(th, 10000);
            CloseHandle(th);
        }
    }
};

static Status call_empty(Channel& ch) {
    ch.begin_call(M_EMPTY);
    Reader r;
    return ch.end_call(r);
}

// ---------------------------------------------------------------------------------------------------------
// tests
// ---------------------------------------------------------------------------------------------------------
TEST(types_roundtrip_both_directions) {
    WATCHDOG(30);
    for (int i = 0; i < 1000; ++i) g_long[i] = (char)('a' + i % 26);
    g_long[1000] = 0;
    Pair p;
    Server srv(p.s(), p.sctx);
    Channel& c = p.c();
    {
        Writer& w = c.begin_call(M_TYPES);
        write_types(w);
        Reader r;
        CHECK(c.end_call(r) == Status::Ok);
        CHECK(check_types(r));
    }
    {
        c.begin_call(M_TYPES_PEER);
        Reader r;
        CHECK(c.end_call(r) == Status::Ok);
        CHECK_EQ(r.u32(), 1u);
    }
    {
        Writer& w = c.begin_call(M_ADD3);
        w.i32(1);
        w.i32(20);
        w.i32(300);
        Reader r;
        CHECK(c.end_call(r) == Status::Ok);
        CHECK_EQ(r.i32(), 321);
    }
    CHECK_EQ(p.sctx.handlerErrors + p.cctx.handlerErrors, 0u);
    CHECK(srv.stop(c));
}

TEST(payload_sizes_both_directions) {
    WATCHDOG(120);
    const uint32_t D = 256 * 1024;
    Pair p(D);
    Server srv(p.s(), p.sctx);
    Channel& c = p.c();
    const uint32_t sizes[] = {0, 4, 28, 32, 33, 36, 64, D - 4, D, D + 1, D + 4, 3 * D + 7, 50u * 1024 * 1024};
    for (size_t k = 0; k < sizeof(sizes) / sizeof(sizes[0]); ++k) {
        uint32_t n = sizes[k];
        // client -> server CALL of n bytes, RETURN of n bytes
        Writer& w = c.begin_call(M_ECHO);
        uint8_t* q = w.reserve(n);
        REQUIRE(q || n == 0);
        fill_pattern(q, round4(n), n);
        uint32_t want = fnv_hash(q, round4(n));
        Reader r;
        Status s = c.end_call(r);
        CHECK(s == Status::Ok);
        CHECK_EQ(r.remaining(), round4(n));
        CHECK_EQ(fnv_hash(r.cursor(), r.remaining()), want);
        // server -> client CALL of n bytes (nested), RETURN of n bytes
        Writer& w2 = c.begin_call(M_PEER_ECHO);
        w2.u32(n);
        w2.u32(n + 1);
        Reader r2;
        CHECK(c.end_call(r2) == Status::Ok);
        CHECK_EQ(r2.u32(), 1u);
        if (vpipc_test::failures()) printf("  failed at size %u\n", n);
    }
    ChannelStats cs = c.stats(), ss = p.s().stats();
    printf("  client: chunksOut %llu chunksIn %llu; server: chunksOut %llu chunksIn %llu\n",
           (unsigned long long)cs.chunksOut, (unsigned long long)cs.chunksIn, (unsigned long long)ss.chunksOut,
           (unsigned long long)ss.chunksIn);
    CHECK(cs.chunksOut > 0 && cs.chunksIn > 0 && ss.chunksOut > 0 && ss.chunksIn > 0);
    CHECK(srv.stop(c));
}

TEST(nesting_depths_and_limit) {
    WATCHDOG(30);
    Pair p;
    Server srv(p.s(), p.sctx);
    Channel& c = p.c();
    const int depths[] = {1, 2, 16, 64};
    for (int k = 0; k < 4; ++k) {
        Writer& w = c.begin_call(M_NEST);
        w.i32(depths[k]);
        Reader r;
        Status s = c.end_call(r);
        CHECK(s == Status::Ok);
        CHECK_EQ(r.i32(), depths[k]);
    }
    CHECK(c.stats().maxDepth >= 64u);
    // depth 65: the side that would send the 66th call in the chain gets ProtocolError locally
    Writer& w = c.begin_call(M_NEST);
    w.i32(65);
    Reader r;
    Status s = c.end_call(r);
    CHECK(s == Status::RemoteError);
    const char* msg = r.cstr();
    CHECK(msg && strstr(msg, "ProtocolError"));
    if (msg) printf("  depth 65 -> %.120s...\n", msg);
    CHECK_EQ(p.cctx.nestedProtocolErrors + p.sctx.nestedProtocolErrors, 1u);
    // the channel is still in step
    CHECK(call_empty(c) == Status::Ok);
    Writer& w2 = c.begin_call(M_NEST);
    w2.i32(3);
    Reader r2;
    CHECK(c.end_call(r2) == Status::Ok);
    CHECK_EQ(r2.i32(), 3);
    CHECK(srv.stop(c));
}

TEST(remote_errors_readable) {
    WATCHDOG(30);
    {
        Pair p;
        Server srv(p.s(), p.sctx);
        Channel& c = p.c();
        c.begin_call(0x7777);
        Reader r;
        CHECK(c.end_call(r) == Status::RemoteError);
        CHECK_EQ(c.last_remote_status(), (uint32_t)kRetUnknownMethod);
        const char* m = r.cstr();
        CHECK(m && strstr(m, "unknown method 30583"));
        c.begin_call(M_FAIL);
        CHECK(c.end_call(r) == Status::RemoteError);
        CHECK_EQ(c.last_remote_status(), (uint32_t)kRetHandlerError);
        m = r.cstr();
        CHECK(m && strcmp(m, "handler failed on purpose (42)") == 0);
        CHECK(call_empty(c) == Status::Ok);
        CHECK_EQ(c.last_remote_status(), (uint32_t)kRetOk);
        CHECK(srv.stop(c));
    }
    {
        // no handlers on the serving side: the library answers UnknownMethod with its own text
        Pair p(256 * 1024, 64 * 1024, false, Side::Client, false);
        p.c().set_handlers(&on_call, &on_post, &p.cctx);
        Ctx quitCtx;
        Server srv(p.s(), quitCtx);
        Reader r;
        p.c().begin_call(M_EMPTY);
        CHECK(p.c().end_call(r) == Status::RemoteError);
        CHECK_EQ(p.c().last_remote_status(), (uint32_t)kRetUnknownMethod);
        const char* m = r.cstr();
        CHECK(m && strstr(m, "no call handler"));
        _InterlockedExchange(&quitCtx.quit, 1);
    }
}

TEST(top_level_call_from_non_initiator) {
    WATCHDOG(30);
    Pair p;
    // no thread serves: a non-initiator call must fail at once, without sending
    Reader r;
    p.s().begin_call(M_EMPTY);
    CHECK(p.s().end_call(r) == Status::ProtocolError);
    // and the channel still works in the right direction
    Server srv(p.s(), p.sctx);
    CHECK(call_empty(p.c()) == Status::Ok);
    CHECK(srv.stop(p.c()));
}

static void posts_case(uint32_t ringBytes, uint32_t total) {
    Pair p(256 * 1024, ringBytes);
    Server srv(p.s(), p.sctx);
    Channel& c = p.c();
    uint32_t serverPosted = 0, failures = 0;
    for (uint32_t i = 0; i < total; ++i) {
        Status st;
        post_seq(c, i, &st);
        if (st != Status::Ok) ++failures;
        if ((i + 1) % 1000 == 0) {
            Writer& w = c.begin_call(M_POSTCHECK);
            w.u32(i + 1);
            w.u32(1000);
            w.u32(serverPosted);
            Reader r;
            Status s = c.end_call(r);
            uint32_t ok = r.u32();
            serverPosted += 1000;
            // ordering rule 1, both directions: the server saw every post before this call, and the client
            // ran every post the server made before its RETURN.
            if (s != Status::Ok || !ok || p.cctx.postsSeen != serverPosted) {
                if (failures < 5)
                    printf("  at %u: status %s ok %u client saw %u of %u\n", i, status_name(s), ok,
                           p.cctx.postsSeen, serverPosted);
                ++failures;
            }
        }
    }
    CHECK_EQ(failures, 0u);
    CHECK_EQ(p.sctx.postsSeen, total);
    CHECK_EQ(p.cctx.postsSeen, serverPosted);
    CHECK_EQ(p.sctx.postSeqErrors + p.cctx.postSeqErrors, 0u);
    ChannelStats cs = c.stats();
    printf("  ring %u: client postsOut %llu postsIn %llu wakesSent %llu spinWaits %llu blockedWaits %llu\n",
           ringBytes, (unsigned long long)cs.postsOut, (unsigned long long)cs.postsIn,
           (unsigned long long)cs.wakesSent, (unsigned long long)cs.spinWaits, (unsigned long long)cs.blockedWaits);
    CHECK(srv.stop(c));
}

TEST(posts_1m_both_directions_ring_64k) {
    WATCHDOG(120);
    posts_case(64 * 1024, 1000000);
}

TEST(posts_1m_both_directions_ring_4k) {
    WATCHDOG(120);
    posts_case(4096, 1000000);
}

static bool wait_posts_seen(Ctx& ctx, uint32_t n, DWORD ms) {
    DWORD t0 = GetTickCount();
    while (*(volatile uint32_t*)&ctx.postsSeen < n) {
        if (GetTickCount() - t0 > ms) return false;
        Sleep(1);
    }
    return true;
}

// Task 1206: posts are published per batch; flush() and the 1/8-ring threshold deliver them without a call.
TEST(posts_batch_publish_flush_and_threshold) {
    WATCHDOG(30);
    Pair p;
    Server srv(p.s(), p.sctx);
    Channel& c = p.c();
    Status st;
    for (uint32_t i = 0; i < 5; ++i) post_seq(c, i, &st);
    CHECK(c.flush() == Status::Ok);
    CHECK(wait_posts_seen(p.sctx, 5, 2000));
    // 2000 more posts (~32 KiB of records, 64 KiB ring): every 8 KiB is published by the threshold rule
    for (uint32_t i = 5; i < 2005; ++i) post_seq(c, i, &st);
    CHECK(wait_posts_seen(p.sctx, 1500, 2000));
    uint32_t before = *(volatile uint32_t*)&p.sctx.postsSeen;
    printf("  seen without flush: %u of 2005\n", before);
    CHECK(c.flush() == Status::Ok);
    CHECK(wait_posts_seen(p.sctx, 2005, 2000));
    CHECK_EQ(p.sctx.postSeqErrors, 0u);
    CHECK(srv.stop(c));
}

// Bug 1208: sparse posts (spaced wider than flushIntervalUs / 16) with no call or flush are each visible within
// about flushIntervalUs + one spacing. The old code read the clock only at the 1st and every 16th unpublished
// record, so after a publish the next 15 posts waited (up to 15 spacings: 7.5 ms here).
TEST(sparse_posts_visible_within_flush_interval_plus_spacing) {
    WATCHDOG(30);
    const WaitConfig& wc = ep_wait_config(nullptr);
    const uint32_t flushUs = wc.flushIntervalUs;  // 1000 by default
    const uint32_t spacingUs = flushUs / 2;       // > flushUs / 16
    const uint32_t n = 48, checked = n - 4;       // the last posts have no later post to publish them
    Pair p;
    Server srv(p.s(), p.sctx);
    Channel& c = p.c();
    const uint64_t f = qpc_frequency();
    std::vector<uint64_t> sent(n);
    memset((void*)g_timedRecv, 0, sizeof(g_timedRecv));
    CHECK(call_empty(c) == Status::Ok);  // a call first: no post-time publish yet, clean start
    uint64_t next = qpc_now();
    for (uint32_t i = 0; i < n; ++i) {
        while (qpc_now() < next) {}
        sent[i] = qpc_now();
        Writer& w = c.begin_post(P_TIMED);
        w.u32(i);
        CHECK(c.end_post() == Status::Ok);
        next = sent[i] + (uint64_t)spacingUs * f / 1000000u;
    }
    Sleep(20);  // let the consumer drain what was published
    std::vector<double> delay(checked);
    uint32_t missing = 0;
    for (uint32_t i = 0; i < checked; ++i) {
        uint64_t r = g_timedRecv[i];
        if (!r) {
            ++missing;
            delay[i] = 1e9;
            continue;
        }
        delay[i] = (double)(r - sent[i]) * 1e6 / (double)f;
    }
    CHECK(c.flush() == Status::Ok);
    std::vector<double> sorted(delay);
    std::sort(sorted.begin(), sorted.end());
    double med = sorted[checked / 2], mx = sorted[checked - 1];
    const double bound = flushUs + spacingUs + 3000.0;  // + a scheduling margin
    printf("  %u posts every %u us, flush %u us: delay median %.0f us, max %.0f us (bound %.0f), %u not seen\n", n,
           spacingUs, flushUs, med, mx, bound, missing);
    CHECK_EQ(missing, 0u);
    CHECK(med <= flushUs + spacingUs);
    CHECK(mx <= bound);
    CHECK(srv.stop(c));
}

TEST(post_handler_sync_call_is_protocol_error) {
    WATCHDOG(30);
    Pair p;
    Server srv(p.s(), p.sctx);
    Channel& c = p.c();
    // server-side post handler calls
    c.begin_post(P_THAT_CALLS);
    CHECK(c.end_post() == Status::Ok);
    c.begin_call(M_GET_POSTCALL);
    Reader r;
    CHECK(c.end_call(r) == Status::Ok);
    CHECK_EQ(r.u32(), 1u);
    CHECK_EQ(r.u32(), (uint32_t)Status::ProtocolError);
    // client-side post handler calls (the server posts inside a handler, the client drains before the RETURN)
    c.begin_call(M_POSTCALL_BACK);
    CHECK(c.end_call(r) == Status::Ok);
    CHECK_EQ(r.u32(), 1u);
    CHECK_EQ(p.cctx.postCallRan, 1u);
    CHECK(p.cctx.postCallStatus == Status::ProtocolError);
    CHECK(call_empty(c) == Status::Ok);
    CHECK(srv.stop(c));
}

TEST(post_too_large) {
    WATCHDOG(30);
    const uint32_t ring = 64 * 1024, maxPayload = ring / 4;
    Pair p(256 * 1024, ring);
    Server srv(p.s(), p.sctx);
    Channel& c = p.c();
    Writer& w = c.begin_post(P_BIG);
    w.reserve(maxPayload + 4);
    CHECK(c.end_post() == Status::PayloadTooLarge);
    Writer& w2 = c.begin_post(P_BIG);
    uint8_t* q = w2.reserve(maxPayload);
    CHECK(q != nullptr);
    if (q) memset(q, 0xAB, maxPayload);
    CHECK(c.end_post() == Status::Ok);
    c.begin_call(M_GET_BIG);
    Reader r;
    CHECK(c.end_call(r) == Status::Ok);
    CHECK_EQ(r.u32(), 1u);
    CHECK_EQ(r.u32(), maxPayload);
    CHECK(srv.stop(c));
}

struct SharedArgs {
    Channel* ch;
    uint32_t t;
    uint32_t calls;
    uint32_t errors;
};
static DWORD WINAPI shared_client_main(void* vp) {
    SharedArgs* a = (SharedArgs*)vp;
    for (uint32_t i = 0; i < a->calls; ++i) {
        Writer& w = a->ch->begin_call(M_IDCB);
        w.u32(a->t);
        w.u32(i);
        Reader r;
        Status s = a->ch->end_call(r);
        if (s != Status::Ok || r.u32() != a->t * 1000003u + i * 7u + 1u) {
            if (a->errors < 3) printf("  thread %u call %u: %s\n", a->t, i, status_name(s));
            ++a->errors;
        }
    }
    return 0;
}

TEST(shared_channel_4_threads_100k_nested) {
    WATCHDOG(240);
    Pair p(256 * 1024, 64 * 1024, true);
    Server srv(p.s(), p.sctx);
    SharedArgs args[4];
    HANDLE th[4];
    uint64_t t0 = qpc_now();
    for (uint32_t t = 0; t < 4; ++t) {
        args[t].ch = &p.c();
        args[t].t = t + 1;
        args[t].calls = 100000;
        args[t].errors = 0;
        th[t] = CreateThread(nullptr, 0, &shared_client_main, &args[t], 0, nullptr);
    }
    CHECK(WaitForMultipleObjects(4, th, TRUE, 200000) == WAIT_OBJECT_0);
    double sec = (double)(qpc_now() - t0) / (double)qpc_frequency();
    uint32_t errors = 0;
    for (int t = 0; t < 4; ++t) {
        errors += args[t].errors;
        CloseHandle(th[t]);
    }
    CHECK_EQ(errors, 0u);
    CHECK_EQ(p.sctx.handlerErrors, 0u);
    CHECK_EQ(channel_active(p.ci), 0u);
    ChannelStats cs = p.c().stats();
    CHECK_EQ(cs.callsOut, 400000ull);
    CHECK_EQ(cs.callsIn, 400000ull);
    printf("  400k top-level calls with a nested callback each in %.2f s (%.2f us per top-level call)\n", sec,
           sec * 1e6 / 400000.0);
    CHECK(srv.stop(p.c()));
}

struct MixArgs {
    Pair* p;
    uint32_t seed;
    double seconds;
    uint32_t ops;
    uint32_t errors;
};
static DWORD WINAPI mix_client_main(void* vp) {
    MixArgs* a = (MixArgs*)vp;
    Channel& c = a->p->c();
    Rng rng(a->seed);
    uint64_t end = qpc_now() + (uint64_t)(a->seconds * (double)qpc_frequency());
    uint32_t posted = 0, serverPosted = 0;
    std::vector<uint8_t> tmp;
    while (qpc_now() < end) {
        uint32_t op = rng.below(100);
        Status s = Status::Ok;
        bool ok = true;
        if (op < 30) {
            s = call_empty(c);
        } else if (op < 50) {
            int32_t x = (int32_t)rng.next(), y = (int32_t)rng.next(), z = (int32_t)rng.next();
            Writer& w = c.begin_call(M_ADD3);
            w.i32(x);
            w.i32(y);
            w.i32(z);
            Reader r;
            s = c.end_call(r);
            ok = r.i32() == (int32_t)((uint32_t)x + (uint32_t)y + (uint32_t)z);
        } else if (op < 65) {
            uint32_t n = rng.below(100) < 95 ? rng.below(2048) : rng.below(700 * 1024);
            Writer& w = c.begin_call(M_ECHO);
            uint8_t* q = w.reserve(n);
            fill_pattern(q, round4(n), n ^ a->seed);
            uint32_t want = fnv_hash(q, round4(n));
            Reader r;
            s = c.end_call(r);
            ok = r.remaining() == round4(n) && fnv_hash(r.cursor(), r.remaining()) == want;
        } else if (op < 75) {
            int32_t d = (int32_t)rng.below(9);
            Writer& w = c.begin_call(M_NEST);
            w.i32(d);
            Reader r;
            s = c.end_call(r);
            ok = r.i32() == d;
        } else if (op < 80) {
            uint32_t n = rng.below(300 * 1024);
            Writer& w = c.begin_call(M_PEER_ECHO);
            w.u32(n);
            w.u32(rng.next());
            Reader r;
            s = c.end_call(r);
            ok = r.u32() == 1u;
        } else {
            uint32_t k = rng.below(200), back = rng.below(200);
            for (uint32_t j = 0; j < k && s == Status::Ok; ++j) post_seq(c, posted++, &s);
            if (s == Status::Ok) {
                Writer& w = c.begin_call(M_POSTCHECK);
                w.u32(posted);
                w.u32(back);
                w.u32(serverPosted);
                Reader r;
                s = c.end_call(r);
                serverPosted += back;
                ok = r.u32() == 1u && a->p->cctx.postsSeen == serverPosted && a->p->cctx.postSeqErrors == 0;
            }
        }
        if (s != Status::Ok || !ok) {
            if (a->errors < 3) printf("  mix seed %u op %u: %s ok %d\n", a->seed, op, status_name(s), (int)ok);
            ++a->errors;
        }
        ++a->ops;
    }
    return 0;
}

TEST(two_channels_concurrent_10s) {
    WATCHDOG(120);
    Pair p1, p2(64 * 1024, 4096);
    Server s1(p1.s(), p1.sctx), s2(p2.s(), p2.sctx);
    MixArgs a[2];
    a[0].p = &p1;
    a[1].p = &p2;
    HANDLE th[2];
    for (int i = 0; i < 2; ++i) {
        a[i].seed = 1234u + (uint32_t)i * 777u;
        a[i].seconds = 10.0;
        a[i].ops = 0;
        a[i].errors = 0;
        th[i] = CreateThread(nullptr, 0, &mix_client_main, &a[i], 0, nullptr);
    }
    CHECK(WaitForMultipleObjects(2, th, TRUE, 60000) == WAIT_OBJECT_0);
    for (int i = 0; i < 2; ++i) {
        CloseHandle(th[i]);
        CHECK_EQ(a[i].errors, 0u);
        printf("  channel %d: %u verified operations in 10 s\n", i, a[i].ops);
    }
    CHECK(s1.stop(p1.c()));
    CHECK(s2.stop(p2.c()));
}

TEST(active_count_and_reset) {
    WATCHDOG(30);
    Pair p;
    {
        Server srv(p.s(), p.sctx);
        CHECK_EQ(channel_active(p.ci), 0u);
        p.c().begin_call(M_ACTIVE_PEER);
        Reader r;
        CHECK(p.c().end_call(r) == Status::Ok);
        CHECK_EQ(r.u32(), 1u);  // client, seen from inside its nested handler
        CHECK_EQ(r.u32(), 1u);  // server, inside its top-level handler
        CHECK_EQ(channel_active(p.ci), 0u);
        CHECK(channel_active(p.si) <= 1u);  // a serving thread counts while it waits inside serve()
        for (int i = 0; i < 10; ++i) CHECK(call_empty(p.c()) == Status::Ok);
        Status st;
        post_seq(p.c(), 0, &st);
        CHECK(srv.stop(p.c()));
        CHECK_EQ(channel_active(p.si), 0u);
    }
    // attach-style reset: shared state zeroed, both sides forget their cursors
    channel_reset_shared(p.block, p.desc);
    channel_reset_local(p.ci);
    channel_reset_local(p.si);
    CHECK_EQ(p.block->c2s.seq, 0u);
    p.sctx = Ctx();
    Server srv(p.s(), p.sctx);
    Status st;
    post_seq(p.c(), 0, &st);
    CHECK(st == Status::Ok);
    Writer& w = p.c().begin_call(M_POSTCHECK);
    w.u32(1);
    w.u32(5);
    w.u32(0);
    Reader r;
    CHECK(p.c().end_call(r) == Status::Ok);
    CHECK_EQ(r.u32(), 1u);
    CHECK(srv.stop(p.c()));
}

// ---------------------------------------------------------------------------------------------------------
// bug 1207
// ---------------------------------------------------------------------------------------------------------
struct CallInThread {
    Channel* ch;
    Status st;
    int32_t v;
};
static DWORD WINAPI add3_main(void* vp) {
    CallInThread* a = (CallInThread*)vp;
    Writer& w = a->ch->begin_call(M_ADD3);
    w.i32(1);
    w.i32(2);
    w.i32(3);
    Reader r;
    a->st = a->ch->end_call(r);
    a->v = r.i32();
    return 0;
}

// B1: the peer publishes (a CALL, posts) after channel_reset_shared and before this side's channel_reset_local,
// as in attach_server. The reset must not take them as seen.
TEST(b1_call_published_before_peer_reset_local_is_handled) {
    WATCHDOG(30);
    Pair p;
    CallInThread a = {&p.c(), Status::SystemError, 0};
    HANDLE th = CreateThread(nullptr, 0, &add3_main, &a, 0, nullptr);
    DWORD t0 = GetTickCount();
    while (*(volatile uint32_t*)&p.block->c2s.seq == 0 && GetTickCount() - t0 < 5000) Sleep(1);
    REQUIRE(p.block->c2s.seq == 1u);  // the client's CALL is published ...
    channel_reset_local(p.si);        // ... when the server resets its cursors (attach_server after CONNECTED)
    Server srv(p.s(), p.sctx);
    CHECK(WaitForSingleObject(th, 5000) == WAIT_OBJECT_0);
    CloseHandle(th);
    CHECK(a.st == Status::Ok);
    CHECK_EQ(a.v, 6);
    CHECK(srv.stop(p.c()));
}

TEST(b1_posts_published_before_peer_reset_local_are_delivered) {
    WATCHDOG(30);
    Pair p;
    Status st;
    for (uint32_t i = 0; i < 3; ++i) post_seq(p.c(), i, &st);
    CHECK(p.c().flush() == Status::Ok);
    CHECK(p.block->c2sWrite.index != 0u);
    channel_reset_local(p.si);  // the consumer side resets after the producer published
    CHECK(p.s().poll() == Status::Ok);
    CHECK_EQ(p.sctx.postsSeen, 3u);
    CHECK_EQ(p.sctx.postSeqErrors, 0u);
    // and the other direction: the server posts before the client resets
    p.s().begin_post(P_BIG);
    CHECK(p.s().end_post() == Status::Ok);
    CHECK(p.s().flush() == Status::Ok);
    channel_reset_local(p.ci);
    CHECK(p.c().poll() == Status::Ok);
    CHECK_EQ(p.cctx.bigPosts, 1u);
}

// B2: a call that runs out of callTimeoutMs returns Timeout, also when it is made by the resync hook; the channel
// stays broken afterwards and reports Timeout again. Real endpoints in this process (client and server threads).
struct B2Server {
    char section[kSectionNameBytes];
    volatile LONG ready;
    volatile LONG quit;
    Status attach;
};
static void b2_on_call(void* ctx, Channel& ch, uint32_t method, Reader& args, Writer& ret) {
    (void)ctx;
    (void)ch;
    (void)args;
    if (method == M_EMPTY) return;
    Sleep(600);  // M_ADD3 and anything else: slower than the client's callTimeoutMs
    ret.i32(0);
}
static DWORD WINAPI b2_server_main(void* vp) {
    B2Server* b = (B2Server*)vp;
    EndpointConfig cfg;
    strcpy_s(cfg.sectionName, sizeof(cfg.sectionName), b->section);
    Endpoint* ep = nullptr;
    b->attach = Endpoint::attach_server(cfg, &ep, 5000);
    if (b->attach != Status::Ok) {
        _InterlockedExchange(&b->ready, 1);
        return 0;
    }
    ep->channel(kChannelUi).set_handlers(&b2_on_call, nullptr, nullptr);
    _InterlockedExchange(&b->ready, 1);
    while (!b->quit) {
        Status s = ep->channel(kChannelUi).serve(100);
        if (s != Status::Ok && s != Status::Timeout) break;
    }
    delete ep;
    return 0;
}
struct B2Hook {
    Status st;
    int runs;
};
static void b2_resync(void* ctx, Endpoint& ep, uint32_t generation) {
    (void)generation;
    B2Hook* h = (B2Hook*)ctx;
    ++h->runs;
    Channel& ch = ep.channel(kChannelUi);
    ch.begin_call(M_ADD3);  // times out
    Reader r;
    h->st = ch.end_call(r);
}

TEST(b2_call_timeout_returns_timeout) {
    WATCHDOG(60);
    for (int withHook = 0; withHook < 2; ++withHook) {
        EndpointConfig cfg;
        _snprintf_s(cfg.tag, sizeof(cfg.tag), _TRUNCATE, "t1207b2%d", withHook);
        cfg.wait.callTimeoutMs = 200;
        Endpoint* cli = nullptr;
        REQUIRE(Endpoint::create_client(cfg, &cli) == Status::Ok);
        B2Hook hook = {Status::Ok, 0};
        if (withHook) cli->set_resync_hook(&b2_resync, &hook);
        cli->bind_current_thread(kChannelUi);
        B2Server b;
        memset(&b, 0, sizeof(b));
        strcpy_s(b.section, sizeof(b.section), cli->section_name());
        HANDLE th = CreateThread(nullptr, 0, &b2_server_main, &b, 0, nullptr);
        CHECK(cli->wait_for_server(5000) == Status::Ok);
        Channel& ch = cli->channel(kChannelUi);
        uint64_t t0 = qpc_now();
        ch.begin_call(withHook ? M_EMPTY : M_ADD3);
        Reader r;
        Status s = ch.end_call(r);
        double ms = (double)(qpc_now() - t0) * 1000.0 / (double)qpc_frequency();
        printf("  %s: first call -> %s after %.0f ms (hook runs %d, hook call %s)\n",
               withHook ? "resync hook times out" : "call times out", status_name(s), ms, hook.runs,
               status_name(hook.st));
        CHECK(s == Status::Timeout);
        CHECK(ms >= 150.0);
        if (withHook) {
            CHECK_EQ(hook.runs, 1);
            CHECK(hook.st == Status::Timeout);
        }
        // the channel stays broken: the next call fails at once with the same status
        t0 = qpc_now();
        ch.begin_call(M_EMPTY);
        s = ch.end_call(r);
        ms = (double)(qpc_now() - t0) * 1000.0 / (double)qpc_frequency();
        CHECK(s == Status::Timeout);
        CHECK(ms < 100.0);
        _InterlockedExchange(&b.quit, 1);
        cli->close();
        CHECK(WaitForSingleObject(th, 10000) == WAIT_OBJECT_0);
        CloseHandle(th);
        CHECK(b.attach == Status::Ok);
        delete cli;
    }
}

#if !defined(NDEBUG)
// clang-cl without /EHa (-fasync-exceptions) only unwinds SEH around calls: the faulting access must be in a
// separate, non-inlined function.
static __declspec(noinline) int read_byte(const volatile uint8_t* q) { return *q; }
static __declspec(noinline) int read_reader(Reader* r) { return (int)(r->u32() & 0xFF); }
static int read_guarded(const volatile uint8_t* q) {
    __try {
        return read_byte(q);
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
                                                                    : EXCEPTION_CONTINUE_SEARCH) {
        return -1;
    }
}
static int reader_guarded(Reader* r) {
    __try {
        return read_reader(r);
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
                                                                    : EXCEPTION_CONTINUE_SEARCH) {
        return -1;
    }
}
#endif

TEST(debug_detects_reader_after_validity) {
#if defined(NDEBUG)
    printf("  skipped: Release build (the check exists in Debug builds only)\n");
#else
    WATCHDOG(30);
    REQUIRE(read_guarded((const volatile uint8_t*)16) == -1);  // the fault catcher itself works
    Pair p;
    Server srv(p.s(), p.sctx);
    Channel& c = p.c();
    const uint32_t sizes[] = {16, 4096, 300 * 1024};  // inline, data area, chunked
    for (int k = 0; k < 3; ++k) {
        Writer& w = c.begin_call(M_ECHO);
        uint8_t* q = w.reserve(sizes[k]);
        memset(q, 0x5A, sizes[k]);
        Reader r;
        CHECK(c.end_call(r) == Status::Ok);
        const uint8_t* payload = r.cursor();
        Reader stale = r;
        CHECK_EQ(read_guarded(payload), 0x5A);
        c.begin_call(M_EMPTY);  // this side's next call: the RETURN payload is dead from here
        CHECK_EQ(read_guarded(payload), -1);
        CHECK_EQ(reader_guarded(&stale), -1);
        Reader r2;
        CHECK(c.end_call(r2) == Status::Ok);
    }
    CHECK(srv.stop(c));
#endif
}

// ---------------------------------------------------------------------------------------------------------
// measurements (printed; meaningful in Release only)
// ---------------------------------------------------------------------------------------------------------
static double tsc_per_us() {
    uint64_t q0 = qpc_now(), t0 = __rdtsc();
    while ((qpc_now() - q0) * 1000u < qpc_frequency() * 100u) {}
    uint64_t q1 = qpc_now(), t1 = __rdtsc();
    return (double)(t1 - t0) / ((double)(q1 - q0) * 1e6 / (double)qpc_frequency());
}
static void percentiles(std::vector<uint64_t>& v, double perUs, double& med, double& p99) {
    std::sort(v.begin(), v.end());
    med = (double)v[v.size() / 2] / perUs;
    p99 = (double)v[v.size() * 99 / 100] / perUs;
}

TEST(bench_numbers) {
    WATCHDOG(120);
#if defined(NDEBUG)
    const char* cfg = "Release";
#else
    const char* cfg = "Debug";
#endif
    const unsigned bits = (unsigned)(sizeof(void*) * 8);
    double perUs = tsc_per_us();
    Pair p;
    Server srv(p.s(), p.sctx);
    Channel& c = p.c();
    const int N = 200000;
    std::vector<uint64_t> t(N);
    for (int i = 0; i < 20000; ++i) call_empty(c);
    for (int i = 0; i < N; ++i) {
        uint64_t a = __rdtsc();
        c.begin_call(M_EMPTY);
        Reader r;
        c.end_call(r);
        t[i] = __rdtsc() - a;
    }
    double med, p99;
    percentiles(t, perUs, med, p99);
    printf("  BENCH %u-bit %s: empty call round trip median %.3f us, p99 %.3f us\n", bits, cfg, med, p99);
    for (int i = 0; i < N; ++i) {
        uint64_t a = __rdtsc();
        Writer& w = c.begin_call(M_ADD3);
        w.i32(i);
        w.i32(2);
        w.i32(3);
        Reader r;
        c.end_call(r);
        volatile int32_t sink = r.i32();
        (void)sink;
        t[i] = __rdtsc() - a;
    }
    percentiles(t, perUs, med, p99);
    printf("  BENCH %u-bit %s: 3-int call round trip median %.3f us, p99 %.3f us\n", bits, cfg, med, p99);

    // Posts (task 1206 before/after): batches of 100 and 1000 12-byte posts, each followed by one empty call that
    // delivers them (the server drains the ring before it handles the call).
    //   spinning: back to back, the server spins on the ring and seq while the batch is written;
    //   sleeping: a 300 us gap before each batch (> idleSpinUs 50 us), so the server is blocked on its event;
    //   no consumer: no serving thread during the batch; the server side drains with poll() afterwards.
    uint32_t seq = 0;
    const int batches[] = {100, 1000};
    const uint64_t gapTicks = qpc_frequency() * 300u / 1000000u;
    for (int mode = 0; mode < 2; ++mode) {
        for (int b = 0; b < 2; ++b) {
            int n = batches[b], reps = mode == 0 ? (b == 0 ? 5000 : 500) : (b == 0 ? 2000 : 500);
            std::vector<uint64_t> prod(reps), total(reps);
            for (int k = 0; k < reps; ++k) {
                if (mode == 1) {
                    uint64_t g = qpc_now();
                    while (qpc_now() - g < gapTicks) {}
                }
                uint64_t a = __rdtsc();
                for (int j = 0; j < n; ++j) {
                    Writer& w = c.begin_post(P_BIG);
                    w.u32(seq++);
                    w.u32(7);
                    w.u32(9);
                    c.end_post();
                }
                uint64_t m = __rdtsc();
                call_empty(c);
                uint64_t e = __rdtsc();
                prod[k] = m - a;
                total[k] = e - a;
            }
            double pm, pp, tm, tp;
            percentiles(prod, perUs, pm, pp);
            percentiles(total, perUs, tm, tp);
            printf("  BENCH %u-bit %s: posts, consumer %s, batch %4d: producer %6.1f ns/post; batch + call %6.1f ns/post "
                   "(median batch %.2f us, with call %.2f us)\n",
                   bits, cfg, mode == 0 ? "spinning" : "sleeping", n, pm * 1000.0 / n, tm * 1000.0 / n, pm, tm);
        }
    }
    {
        Pair q;
        for (int b = 0; b < 2; ++b) {
            int n = batches[b], reps = 500;
            std::vector<uint64_t> prod(reps), cons(reps);
            for (int k = 0; k < reps; ++k) {
                uint64_t a = __rdtsc();
                for (int j = 0; j < n; ++j) {
                    Writer& w = q.c().begin_post(P_BIG);
                    w.u32(seq++);
                    w.u32(7);
                    w.u32(9);
                    q.c().end_post();
                }
                q.c().flush();
                uint64_t m = __rdtsc();
                q.s().poll();
                prod[k] = m - a;
                cons[k] = __rdtsc() - m;
            }
            double pm, pp, cm, cp;
            percentiles(prod, perUs, pm, pp);
            percentiles(cons, perUs, cm, cp);
            printf("  BENCH %u-bit %s: posts, no consumer thread, batch %4d: producer + flush %6.1f ns/post; "
                   "consumer poll() %6.1f ns/post\n", bits, cfg, n, pm * 1000.0 / n, cm * 1000.0 / n);
        }
    }

    const uint32_t big = 50u * 1024 * 1024;
    for (int dir = 0; dir < 2; ++dir) {
        double best = 1e30;
        for (int k = 0; k < 3; ++k) {
            uint64_t a = qpc_now();
            Writer& w = c.begin_call(dir == 0 ? M_HASH : M_ECHO);
            uint8_t* q = w.reserve(big);
            if (k == 0) fill_pattern(q, big, 3);
            Reader r;
            Status s = c.end_call(r);
            double sec = (double)(qpc_now() - a) / (double)qpc_frequency();
            CHECK(s == Status::Ok);
            if (sec < best) best = sec;
        }
        double mb = dir == 0 ? 50.0 : 100.0;
        printf("  BENCH %u-bit %s: 50 MiB %s through a 256 KiB data area: best of 3 %.1f ms, %.0f MiB/s "
               "(including the Writer fill of the spill buffer)\n",
               bits, cfg, dir == 0 ? "CALL (4-byte RETURN)" : "CALL + 50 MiB RETURN", best * 1000.0, mb / best);
    }
    CHECK(srv.stop(c));
}

VPIPC_TEST_MAIN()
