// tests/test_lifecycle.cpp - section, attach, refusals, liveness, restart and resync, close (task 1202,
// PROTOCOL.md sections 2 and 7). Cross-process cases spawn this executable as a child:
//   test_lifecycle --child server <section> <outfile> [minor=N layout=X schema=X timeout=MS then=hold|close|serve
//                  holdms=N]      (serve: channel ui with kMEcho / kMDie / kMGen until the client goes away)
//   test_lifecycle --child client <tag> <outfile> [then=hold|closeafterattach holdms=N]
// Calls are real Channel calls where the scenario allows; ep_enter / wait_change are driven directly only where a
// test needs a state a real call cannot hold (a call "inside" while a new server arrives, a blocked wait at close).
// Children write key=value lines to <outfile> and live in a kill-on-close job. Every wait is bounded.
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "check.h"
#include "endpoint_impl.h"
#include "internal.h"
#include "support/test_lifecycle/proc_util.h"

using namespace vpipc;

namespace {

// ---------------------------------------------------------------------------------------------------------
// API adapter: the only test code that depends on how vpipc.h spells configs and endpoint ownership.
// ---------------------------------------------------------------------------------------------------------
void put(char* dst, size_t cap, const char* src) {  // writes up to cap bytes; NUL only if it fits
    size_t n = strlen(src);
    memcpy(dst, src, n < cap ? n + 1 : cap);
}
void cfg_section(EndpointConfig& c, const char* name) { put(c.sectionName, sizeof(c.sectionName), name); }
void cfg_tag(EndpointConfig& c, const char* tag) { put(c.tag, sizeof(c.tag), tag); }
void cfg_channel_count(EndpointConfig& c, uint32_t n) {
    for (uint32_t i = c.channelCount; i < n && i < kMaxChannels; ++i) {
        c.channels[i] = ChannelConfig();
        char nm[4] = {'x', (char)('0' + i % 10), 0, 0};
        put(c.channels[i].name, kChannelNameBytes, nm);
    }
    c.channelCount = n;
}
void cfg_channel_name(EndpointConfig& c, uint32_t i, const char* name) { put(c.channels[i].name, kChannelNameBytes, name); }
Endpoint* make_client(const EndpointConfig& c, Status* s) {
    Endpoint* p = nullptr;
    *s = Endpoint::create_client(c, &p);
    if (*s != Status::Ok) CHECK(p == nullptr);
    return p;
}
Endpoint* make_server(const EndpointConfig& c, uint32_t timeoutMs, Status* s) {
    Endpoint* p = nullptr;
    *s = Endpoint::attach_server(c, &p, timeoutMs);
    if (*s != Status::Ok) CHECK(p == nullptr);
    return p;
}
// The endpoint's own last error, or (after a failed create/attach, which returns no endpoint) this thread's.
std::string err_of(Endpoint* e) { return e ? std::string(e->last_error()) : std::string(Endpoint::last_error_text()); }
std::string name_of(Endpoint* e) { return std::string(e->section_name()); }

// ---------------------------------------------------------------------------------------------------------
std::string fmtu(unsigned v) {
    char b[32];
    sprintf(b, "%u", v);
    return b;
}
std::string fmtd(double v) {
    char b[32];
    sprintf(b, "%.2f", v);
    return b;
}

struct Rec {
    volatile LONG n[6];
    volatile LONG gen[6];
    double atUs[6];
};
void on_peer_event(void* ctx, PeerEvent ev, uint32_t gen) {
    Rec* r = (Rec*)ctx;
    int i = (int)ev;
    r->atUs[i] = tu::qpc_us();
    r->gen[i] = (LONG)gen;
    InterlockedIncrement(&r->n[i]);
}
LONG nev(Rec& r, PeerEvent ev) { return r.n[(int)ev]; }

EndpointConfig client_cfg(const char* tag, Rec* rec) {
    EndpointConfig c;
    cfg_tag(c, tag);
    c.wait.tickMs = 50;
    c.onPeerEvent = &on_peer_event;
    c.hookCtx = rec;
    return c;
}

// The WaitEnv of one channel and the word that side waits on (the peer's MsgLine seq).
WaitEnv env_for(EndpointImpl* ep, uint32_t ch, volatile uint32_t** watched) {
    ChannelBlock* b = (ChannelBlock*)(ep->base_ + ep->hdr_->channels[ch].offset);
    const bool client = ep->side_ == Side::Client;
    WaitEnv w;
    w.mySleeping = client ? &b->clientCtl.sleeping : &b->serverCtl.sleeping;
    w.peerSleeping = client ? &b->serverCtl.sleeping : &b->clientCtl.sleeping;
    w.myEvent = ep->events_[2 * ch + (client ? 0 : 1)];
    w.peerEvent = ep->events_[2 * ch + (client ? 1 : 0)];
    w.ep = ep;
    *watched = client ? &b->s2c.seq : &b->c2s.seq;
    return w;
}

struct BgWait {
    WaitEnv env;
    volatile uint32_t* word;
    uint32_t seen;
    uint32_t timeoutMs;
    WaitResult result;
    double retUs;
};
DWORD WINAPI bg_wait_main(void* p) {
    BgWait& w = *(BgWait*)p;
    const volatile uint32_t* words[1] = {w.word};
    w.result = wait_change(w.env, words, &w.seen, 1, 50, w.timeoutMs, nullptr);
    w.retUs = tu::qpc_us();
    return 0;
}

std::string server_args(const std::string& section, const std::string& out, const char* opts) {
    return "--child server " + section + " " + out + " " + opts;
}

void bump64(volatile uint64_t* p) {
    for (;;) {
        LONG64 c = InterlockedCompareExchange64((volatile LONG64*)p, 0, 0);
        if (InterlockedCompareExchange64((volatile LONG64*)p, c + 1, c) == c) return;
    }
}

// An in-process "server" that writes its line by hand (to play an old server that does not check, or one that
// stops its heartbeat). Returns after requesting the attach.
void fake_server_request(Header* h, uint16_t minor) {
    h->server.pid = GetCurrentProcessId();
    h->server.pointerBits = (uint32_t)(sizeof(void*) * 8);
    h->server.versionMajor = kVersionMajor;
    h->server.versionMinor = minor;
    h->server.layoutHash = kLayoutHash;
    h->server.appSchemaHash = 0;
    h->server.state = kSideStarting;
    InterlockedCompareExchange((volatile LONG*)&h->conn.attachPid, (LONG)GetCurrentProcessId(), 0);
    InterlockedCompareExchange((volatile LONG*)&h->conn.connState, kConnAttachRequested, kConnNoServer);
}

// ---------------------------------------------------------------------------------------------------------
// methods the test servers handle (channel ui)
// ---------------------------------------------------------------------------------------------------------
const uint32_t kMEcho = 1;  // i32 -> the same i32
const uint32_t kMDie = 2;   // the server process terminates inside the call
const uint32_t kMGen = 3;   // -> u32 the server's generation
// Bug 0113: a server that is alive but stops answering, and slow but living calls.
const uint32_t kMHang = 4;         // the handler never returns (sleeps 60 s); the process and its heartbeat live on
const uint32_t kMSlowQuiet = 5;    // u32 ms: sleeps that long without any message, then returns
const uint32_t kMSlowNested = 6;   // u32 ms, u32 every: calls kMClientTick back on the client every `every` ms
const uint32_t kMClientTick = 7;   // served by the client: -> u32 1 (the "engine" side of a nested call)

void server_on_call(void* ctx, Channel& ch, uint32_t method, Reader& args, Writer& ret) {
    Endpoint* ep = (Endpoint*)ctx;
    switch (method) {
    case kMEcho: ret.i32(args.i32()); break;
    case kMDie: TerminateProcess(GetCurrentProcess(), 3); break;
    case kMGen: ret.u32(ep->generation()); break;
    case kMHang: Sleep(60000); break;
    case kMSlowQuiet: Sleep(args.u32()); ret.u32(1); break;
    case kMSlowNested: {
        const uint32_t ms = args.u32(), every = args.u32();
        const double end = tu::qpc_us() + ms * 1000.0;
        uint32_t n = 0;
        while (tu::qpc_us() < end) {
            Sleep(every);
            ch.begin_call(kMClientTick);
            Reader r;
            if (ch.end_call(r) != Status::Ok) break;
            ++n;
        }
        ret.u32(n);
        break;
    }
    default: ch.set_return_status(kRetUnknownMethod); break;
    }
}
void client_on_call(void*, Channel& ch, uint32_t method, Reader&, Writer& ret) {
    if (method == kMClientTick) ret.u32(1);
    else ch.set_return_status(kRetUnknownMethod);
}
void client_on_post(void*, Channel&, uint32_t, Reader&) {}
void server_on_post(void*, Channel&, uint32_t, Reader&) {}

// A real call on this thread's channel.
Status call_echo(Endpoint* ep, int32_t v, int32_t* out) {
    Channel& ch = ep->current_channel();
    ch.begin_call(kMEcho).i32(v);
    Reader r;
    Status s = ch.end_call(r);
    *out = s == Status::Ok ? r.i32() : -1;
    if (s != Status::Ok) printf("  call_echo(%d): %s, %s\n", v, status_name(s), ep->last_error());
    return s;
}
Status call_u32(Endpoint* ep, uint32_t method, uint32_t* out) {
    Channel& ch = ep->current_channel();
    ch.begin_call(method);
    Reader r;
    Status s = ch.end_call(r);
    *out = s == Status::Ok ? r.u32() : 0xFFFFFFFFu;
    if (s != Status::Ok) printf("  call_u32(%u): %s\n", method, status_name(s));
    return s;
}

// ---------------------------------------------------------------------------------------------------------
// child processes
// ---------------------------------------------------------------------------------------------------------
std::string opt(int argc, char** argv, int from, const char* key, const char* def) {
    size_t k = strlen(key);
    for (int i = from; i < argc; ++i)
        if (!strncmp(argv[i], key, k) && argv[i][k] == '=') return argv[i] + k + 1;
    return def;
}

int child_server(int argc, char** argv) {
    const std::string section = argv[3], out = argv[4];
    EndpointConfig cfg;
    cfg_section(cfg, section.c_str());
    cfg.wait.tickMs = 50;
    std::string v = opt(argc, argv, 5, "minor", "");
    if (!v.empty()) {
        cfg.test.enabled = true;
        cfg.test.versionMinor = (uint16_t)atoi(v.c_str());
    }
    v = opt(argc, argv, 5, "layout", "");
    if (!v.empty()) {
        cfg.test.enabled = true;
        cfg.test.layoutHash = (uint32_t)strtoul(v.c_str(), NULL, 0);
    }
    v = opt(argc, argv, 5, "schema", "");
    if (!v.empty()) cfg.appSchemaHash = (uint64_t)_strtoui64(v.c_str(), NULL, 0);
    const uint32_t timeoutMs = (uint32_t)atoi(opt(argc, argv, 5, "timeout", "10000").c_str());
    const std::string then = opt(argc, argv, 5, "then", "hold");
    const DWORD holdMs = (DWORD)atoi(opt(argc, argv, 5, "holdms", "20000").c_str());
    Status s;
    double t0 = tu::qpc_us();
    Endpoint* ep = make_server(cfg, timeoutMs, &s);
    tu::append_kv(out, "status", status_name(s));
    tu::append_kv(out, "attachms", fmtd((tu::qpc_us() - t0) / 1000.0));
    tu::append_kv(out, "error", err_of(ep));
    if (s != Status::Ok) {
        delete ep;
        return 100 + (int)s;
    }
    tu::append_kv(out, "gen", fmtu(ep->generation()));
    int code = 0;
    if (then == "close") {
        Sleep(holdMs);
        ep->close();
        tu::append_kv(out, "closed", "1");
    } else if (then == "serve") {  // serve channel ui until the client goes away (bounded: 20 s)
        Channel& ch = ep->channel(kChannelUi);
        ch.set_handlers(&server_on_call, &server_on_post, ep);
        uint32_t served = 0, loops = 0;
        Status r = Status::Timeout;
        const double begin = tu::qpc_us();
        const double end = begin + 20e6;
        while (tu::qpc_us() < end) {
            ++loops;
            r = ch.serve(1000);
            if (r == Status::Ok) ++served;
            else if (r != Status::Timeout) break;
        }
        tu::append_kv(out, "served", fmtu(served));
        tu::append_kv(out, "loops", fmtu(loops));
        tu::append_kv(out, "servems", fmtd((tu::qpc_us() - begin) / 1000.0));
        tu::append_kv(out, "serve", status_name(r));
        tu::append_kv(out, "enter", status_name(ep_enter(ep->impl(), 0, false)));
        code = 20 + (int)r;
    } else {
        Sleep(holdMs);
    }
    delete ep;
    return code;
}

int child_client(int argc, char** argv) {
    const std::string tag = argv[3], out = argv[4];
    EndpointConfig cfg;
    cfg_tag(cfg, tag.c_str());
    cfg.wait.tickMs = 50;
    const std::string then = opt(argc, argv, 5, "then", "hold");
    const DWORD holdMs = (DWORD)atoi(opt(argc, argv, 5, "holdms", "20000").c_str());
    Status s;
    Endpoint* ep = make_client(cfg, &s);
    tu::append_kv(out, "status", status_name(s));
    if (s != Status::Ok) {
        delete ep;
        return 100 + (int)s;
    }
    tu::append_kv(out, "section", name_of(ep));
    if (then == "closeafterattach") {
        Status w = ep->wait_for_server(10000);
        tu::append_kv(out, "attached", status_name(w));
        ep->bind_current_thread(kChannelUi);
        int32_t got = 0;
        Status cs = call_echo(ep, 5, &got);
        tu::append_kv(out, "call", status_name(cs));
        tu::append_kv(out, "echo", fmtu((unsigned)got));
        Sleep(holdMs);
        ep->close();
        tu::append_kv(out, "closed", "1");
        Sleep(500);
    } else {
        Sleep(holdMs);
    }
    delete ep;
    return 0;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------
// in-process
// ---------------------------------------------------------------------------------------------------------
TEST(config_validation) {
    Status s;
    {
        EndpointConfig c;
        cfg_tag(c, "cv1");
        Endpoint* ep = make_client(c, &s);
        CHECK(s == Status::Ok);
        REQUIRE(ep != nullptr);
        char expect[96];
        sprintf(expect, "Local\\VPIpc.%u.cv1", (unsigned)GetCurrentProcessId());
        CHECK(name_of(ep) == expect);
        CHECK_EQ(ep->conn_state(), (uint32_t)kConnNoServer);
        CHECK_EQ(ep->channel_count(), 3u);
        CHECK_EQ(ep->generation(), 0u);
        CHECK(ep->side() == Side::Client);
        CHECK(ep->wait_for_server(30) == Status::Timeout);
        CHECK(ep_enter(ep->impl(), 0, true) == Status::Disconnected);  // fail fast without a server
        CHECK(ep_enter(ep->impl(), 9, true) == Status::ProtocolError);
        Endpoint* dup = make_client(c, &s);  // same name
        CHECK(s == Status::SystemError);
        CHECK(tu::contains(err_of(dup), "already exists"));
        delete dup;
        CHECK(ep->close() == Status::Ok);
        CHECK(ep->close() == Status::Ok);  // idempotent
        CHECK(ep_enter(ep->impl(), 0, true) == Status::Disconnected);
        delete ep;
    }
    struct Bad {
        const char* what;
        uint32_t dataBytes, ringBytes, count;
        const char* name;
    } bad[] = {
        {"dataBytes", 100, 65536, 3, nullptr},       {"dataBytes", 0, 65536, 3, nullptr},
        {"ringBytes", 65536, 5000, 3, nullptr},      {"ringBytes", 65536, 2048, 3, nullptr},
        {"channel count", 65536, 65536, 9, nullptr}, {"name", 65536, 65536, 3, "sixteen_chars_xx"},
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        EndpointConfig c;
        cfg_tag(c, "cvbad");
        cfg_channel_count(c, bad[i].count);
        if (bad[i].count <= kMaxChannels) {
            c.channels[1].dataBytes = bad[i].dataBytes;
            c.channels[1].ringBytes = bad[i].ringBytes;
        }
        if (bad[i].name) cfg_channel_name(c, 0, bad[i].name);
        Endpoint* ep = make_client(c, &s);
        CHECK(s == Status::InvalidArgument);
        printf("  bad %s -> %s: %s\n", bad[i].what, status_name(s), err_of(ep).c_str());
        CHECK(tu::contains(err_of(ep), bad[i].what));
        delete ep;
    }
    {  // 8 channels, small sizes, a 15-char name: fine
        EndpointConfig c;
        cfg_tag(c, "cv8");
        cfg_channel_count(c, 8);
        cfg_channel_name(c, 7, "fifteen_chars_x");
        for (uint32_t i = 0; i < 8; ++i) {
            c.channels[i].dataBytes = 64;
            c.channels[i].ringBytes = 4096;
        }
        Endpoint* ep = make_client(c, &s);
        CHECK(s == Status::Ok);
        if (ep) CHECK_EQ(ep->channel_count(), 8u);
        delete ep;
    }
}

namespace {
struct BindProbe {
    Endpoint* ep;
    Channel* seen;
};
DWORD WINAPI bind_probe_main(void* p) {
    BindProbe& b = *(BindProbe*)p;
    b.seen = &b.ep->current_channel();
    return 0;
}
}  // namespace

TEST(thread_binding) {
    Status s;
    EndpointConfig c;
    cfg_tag(c, "bind");
    Endpoint* ep = make_client(c, &s);
    REQUIRE(s == Status::Ok && ep);
    CHECK(&ep->current_channel() == &ep->channel(kChannelAux));  // unbound: the first shared channel
    ep->bind_current_thread(kChannelUi);
    CHECK(&ep->current_channel() == &ep->channel(kChannelUi));
    ep->bind_current_thread(kChannelCore);
    CHECK(&ep->current_channel() == &ep->channel(kChannelCore));
    BindProbe b = {ep, nullptr};
    tu::Thread t;
    t.start(&bind_probe_main, &b);
    CHECK(t.join(5000));
    CHECK(b.seen == &ep->channel(kChannelAux));  // binding is per thread
    {  // a second endpoint on the same thread has its own binding
        EndpointConfig c2;
        cfg_tag(c2, "bind2");
        Endpoint* ep2 = make_client(c2, &s);
        REQUIRE(ep2 != nullptr);
        CHECK(&ep2->current_channel() == &ep2->channel(kChannelAux));
        ep2->bind_current_thread(kChannelUi);
        CHECK(&ep2->current_channel() == &ep2->channel(kChannelUi));
        CHECK(&ep->current_channel() == &ep->channel(kChannelCore));
        delete ep2;
    }
    {  // no shared channel: unbound threads use channel 0
        EndpointConfig c3;
        cfg_tag(c3, "bind3");
        c3.channels[2].shared = false;
        Endpoint* ep3 = make_client(c3, &s);
        REQUIRE(ep3 != nullptr);
        CHECK(&ep3->current_channel() == &ep3->channel(0));
        delete ep3;
    }
    delete ep;
}

TEST(server_timeout_without_client) {
    char name[96];
    sprintf(name, "Local\\VPIpc.%u.nobody", (unsigned)GetCurrentProcessId());
    EndpointConfig c;
    cfg_section(c, name);
    Status s;
    double t0 = tu::qpc_us();
    Endpoint* ep = make_server(c, 300, &s);
    double ms = (tu::qpc_us() - t0) / 1000.0;
    printf("  %s after %.1f ms: %s\n", status_name(s), ms, err_of(ep).c_str());
    CHECK(s == Status::Timeout);
    CHECK(ms >= 290 && ms < 1500);
    CHECK(tu::contains(err_of(ep), "did not appear"));
    delete ep;
    EndpointConfig none;
    ep = make_server(none, 100, &s);
    CHECK(s == Status::InvalidArgument);  // sectionName is required on the server
    delete ep;
}

TEST(client_refuses_unchecked_server) {
    Rec rec = {};
    Status s;
    EndpointConfig c = client_cfg("old", &rec);
    Endpoint* ep = make_client(c, &s);
    REQUIRE(s == Status::Ok && ep);
    Header* h = ep->impl()->hdr_;
    fake_server_request(h, 9);  // an "old" server that claims 1.9 and does not check
    CHECK(tu::wait_until([&] { return h->conn.connState == kConnRefused; }, 3000));
    CHECK_EQ(h->client.refuseReason, (uint32_t)kRefuseVersion);
    std::string text(h->clientRefusal);
    printf("  client refusal text: %s\n", text.c_str());
    CHECK(tu::contains(text, "refused by the client") && tu::contains(text, "protocol version"));
    CHECK(tu::contains(text, "client 1.0") && tu::contains(text, "server 1.9"));
    CHECK(tu::wait_until([&] { return nev(rec, PeerEvent::Refused) == 1; }, 3000));
    CHECK(err_of(ep) == text);
    h->server.state = kSideRefused;  // the server's ack
    CHECK(tu::wait_until([&] { return h->conn.connState == kConnNoServer && h->conn.attachPid == 0; }, 3000));
    CHECK_EQ(ep->generation(), 0u);
    delete ep;
}

TEST(hung_and_recovered) {
    Rec rec = {};
    Status s;
    EndpointConfig c = client_cfg("hung", &rec);
    c.wait.hungAfterMs = 300;
    c.wait.failOnHung = true;
    Endpoint* ep = make_client(c, &s);
    REQUIRE(s == Status::Ok && ep);
    Header* h = ep->impl()->hdr_;
    fake_server_request(h, kVersionMinor);  // never beats
    // two-phase accept: the client publishes generation 1 while still ATTACH_REQUESTED, the server reports Ready
    CHECK(tu::wait_until([&] { return h->conn.generation == 1; }, 3000));
    CHECK_EQ(ep->conn_state(), (uint32_t)kConnAttachRequested);
    Sleep(30);
    CHECK_EQ(ep->conn_state(), (uint32_t)kConnAttachRequested);  // not CONNECTED before the server is Ready
    InterlockedExchange((volatile LONG*)&h->server.state, kSideReady);
    CHECK(ep->wait_for_server(3000) == Status::Ok);
    CHECK_EQ(ep->generation(), 1u);
    double t0 = tu::qpc_us();
    CHECK(tu::wait_until([&] { return nev(rec, PeerEvent::Hung) == 1; }, 3000));
    printf("  Hung reported %.0f ms after attach (hungAfterMs 300, tick 50)\n", (rec.atUs[(int)PeerEvent::Hung] - t0) / 1000.0);
    ep->bind_current_thread(kChannelUi);
    int32_t got = 0;
    t0 = tu::qpc_us();
    CHECK(call_echo(ep, 5, &got) == Status::PeerHung);  // a real call to a hung peer, failOnHung set
    printf("  call to the hung server returned PeerHung after %.1f ms\n", (tu::qpc_us() - t0) / 1000.0);
    CHECK(tu::qpc_us() - t0 < 1000000.0);
    bump64(&h->server.heartbeatCount);
    CHECK(tu::wait_until([&] { return nev(rec, PeerEvent::Recovered) == 1; }, 3000));
    // the fake server closes: Closed, then the section returns to NO_SERVER
    InterlockedExchange((volatile LONG*)&h->conn.connState, kConnClosing);
    CHECK(tu::wait_until([&] { return nev(rec, PeerEvent::Closed) == 1; }, 3000));
    CHECK(tu::wait_until([&] { return h->conn.connState == kConnNoServer; }, 3000));
    CHECK(ep_enter(ep->impl(), 0, true) == Status::Disconnected);
    delete ep;
}

// ---------------------------------------------------------------------------------------------------------
// cross-process
// ---------------------------------------------------------------------------------------------------------
TEST(client_call_timeout_default) {  // bug 0113: a client is bounded by default, a server is not
    EndpointConfig d;
    CHECK_EQ(d.wait.callTimeoutMs, 0u);
    CHECK_EQ(d.wait.clientCallTimeoutMs, 600000u);
    Status s;
    EndpointConfig c;
    cfg_tag(c, "ctd");
    Endpoint* ep = make_client(c, &s);
    REQUIRE(s == Status::Ok && ep);
    CHECK_EQ(ep->impl()->cfg_.wait.callTimeoutMs, 600000u);   // the effective bound of this client's reply waits
    delete ep;
    EndpointConfig x;
    cfg_tag(x, "ctx");
    x.wait.callTimeoutMs = 1234;                                 // an explicit value wins
    ep = make_client(x, &s);
    REQUIRE(s == Status::Ok && ep);
    CHECK_EQ(ep->impl()->cfg_.wait.callTimeoutMs, 1234u);
    delete ep;
}

namespace {
struct BgCall {
    Endpoint* ep;
    uint32_t method;
    Status status;
    double retUs;
};
DWORD WINAPI bg_call_main(void* p) {
    BgCall& b = *(BgCall*)p;
    b.ep->bind_current_thread(kChannelUi);
    Channel& ch = b.ep->current_channel();
    ch.begin_call(b.method);
    Reader r;
    b.status = ch.end_call(r);
    b.retUs = tu::qpc_us();
    return 0;
}
Endpoint* client_with_server(const char* tag, Rec* rec, uint32_t clientCallTimeoutMs, tu::Child* srv, std::string* out) {
    Status s;
    EndpointConfig c = client_cfg(tag, rec);
    c.wait.clientCallTimeoutMs = clientCallTimeoutMs;
    Endpoint* ep = make_client(c, &s);
    if (s != Status::Ok || !ep) return nullptr;
    ep->channel(kChannelUi).set_handlers(&client_on_call, &client_on_post, nullptr);
    *out = tu::temp_file(tag);
    *srv = tu::spawn_self(server_args(name_of(ep), *out, "then=serve"));
    if (ep->wait_for_server(10000) != Status::Ok) {
        delete ep;
        return nullptr;
    }
    return ep;
}
}  // namespace

TEST(hung_server_call_is_bounded_slow_calls_are_not) {
    // Bug 0113: a server that stops answering but stays alive (its monitor thread keeps the heartbeat, so the
    // existing PeerHung check never fires) ends the client's call with Timeout; slow calls that keep the channel busy,
    // and quiet ones shorter than the bound, are not cut.
    Rec rec = {};
    tu::Child srv;
    std::string out;
    Endpoint* ep = client_with_server("hb", &rec, 1500, &srv, &out);
    REQUIRE(ep != nullptr);
    CHECK_EQ(ep->impl()->cfg_.wait.callTimeoutMs, 1500u);
    ep->bind_current_thread(kChannelUi);
    Channel& ch = ep->current_channel();
    Reader r;
    double t0 = tu::qpc_us();
    ch.begin_call(kMSlowQuiet).u32(800);                    // quiet, shorter than the bound
    CHECK(ch.end_call(r) == Status::Ok);
    printf("  quiet 800 ms call: Ok after %.0f ms\n", (tu::qpc_us() - t0) / 1000.0);
    t0 = tu::qpc_us();
    {
        Writer& w = ch.begin_call(kMSlowNested);           // 4 s, twice the bound, but the server calls back every 300 ms
        w.u32(4000);
        w.u32(300);
    }
    Status sn = ch.end_call(r);
    const double nestedMs = (tu::qpc_us() - t0) / 1000.0;
    const uint32_t ticks = sn == Status::Ok ? r.u32() : 0;
    printf("  slow 4 s call with nested calls every 300 ms: %s after %.0f ms, %u nested calls\n", status_name(sn),
           nestedMs, ticks);
    CHECK(sn == Status::Ok);
    CHECK(nestedMs >= 4000.0);
    CHECK(ticks >= 10);
    t0 = tu::qpc_us();
    ch.begin_call(kMHang);                                  // the server's handler never returns
    Status sh = ch.end_call(r);
    const double hangMs = (tu::qpc_us() - t0) / 1000.0;
    printf("  call to a hung, living server: %s after %.0f ms (bound 1500 ms, tick 50 ms)\n", status_name(sh), hangMs);
    CHECK(sh == Status::Timeout);
    CHECK(hangMs >= 1450.0 && hangMs < 3000.0);
    CHECK(WaitForSingleObject(srv.process, 0) == WAIT_TIMEOUT);  // the server is still alive: hung, not dead
    t0 = tu::qpc_us();
    ch.begin_call(kMEcho).i32(1);
    CHECK(ch.end_call(r) == Status::Timeout);               // the channel stays broken (bug 1207): fails at once
    CHECK(tu::qpc_us() - t0 < 200000.0);
    tu::kill(srv);
    tu::join(srv, 3000);
    delete ep;
    DeleteFileA(out.c_str());
}

TEST(hung_server_without_bound_waits_until_killed) {
    // The behaviour before bug 0113 (clientCallTimeoutMs 0 = callTimeoutMs 0): the same hung call does not return
    // while the server lives (checked for 4 s, then the server is killed and the call ends with PeerDied).
    Rec rec = {};
    tu::Child srv;
    std::string out;
    Endpoint* ep = client_with_server("hn", &rec, 0, &srv, &out);
    REQUIRE(ep != nullptr);
    CHECK_EQ(ep->impl()->cfg_.wait.callTimeoutMs, 0u);
    BgCall b = {ep, kMHang, Status::Ok, 0.0};
    tu::Thread t;
    t.start(&bg_call_main, &b);
    const bool returned = t.join(4000);
    printf("  unbounded call to a hung server: %s after 4 s\n", returned ? "returned" : "still waiting");
    CHECK(!returned);
    const double killUs = tu::qpc_us();
    tu::kill(srv);
    CHECK(t.join(5000));
    printf("  after the kill: %s %.0f ms later\n", status_name(b.status), (b.retUs - killUs) / 1000.0);
    CHECK(b.status == Status::PeerDied);
    tu::join(srv, 3000);
    delete ep;
    DeleteFileA(out.c_str());
}

TEST(attach_then_client_close) {
    Rec rec = {};
    Status s;
    EndpointConfig c = client_cfg("a1", &rec);
    Endpoint* ep = make_client(c, &s);
    REQUIRE(s == Status::Ok && ep);
    std::string out = tu::temp_file("a1");
    double t0 = tu::qpc_us();
    tu::Child srv = tu::spawn_self(server_args(name_of(ep), out, "then=serve"));
    CHECK(ep->wait_for_server(10000) == Status::Ok);
    printf("  attached %.1f ms after the server process was started\n", (tu::qpc_us() - t0) / 1000.0);
    CHECK_EQ(ep->generation(), 1u);
    CHECK_EQ(ep->conn_state(), (uint32_t)kConnConnected);
    CHECK_EQ(ep->peer_pid(), (uint32_t)srv.pid);
    CHECK(tu::wait_until([&] { return nev(rec, PeerEvent::Attached) == 1; }, 1000));
    CHECK_EQ(rec.gen[(int)PeerEvent::Attached], 1);
    ep->bind_current_thread(kChannelUi);
    int32_t got = 0;
    CHECK(call_echo(ep, 41, &got) == Status::Ok);
    CHECK_EQ(got, 41);
    uint32_t gen = 0;
    CHECK(call_u32(ep, kMGen, &gen) == Status::Ok);
    CHECK_EQ(gen, 1u);
    CHECK(ep->close() == Status::Ok);
    CHECK(call_echo(ep, 1, &got) == Status::Disconnected);  // after close
    int code = tu::join(srv, 5000);
    std::string r = tu::read_file(out);
    printf("  server child: exit %d, %s", code, r.c_str());
    CHECK(tu::kv(r, "status") == "Ok");
    CHECK(tu::kv(r, "gen") == "1");
    CHECK(tu::kv(r, "served") == "2");
    CHECK(tu::kv(r, "serve") == "PeerClosed");
    CHECK(tu::kv(r, "enter") == "PeerClosed");
    CHECK_EQ(code, 20 + (int)Status::PeerClosed);
    delete ep;
    DeleteFileA(out.c_str());
}

namespace {
void refusal_case(const char* tag, const char* opts, Status expect, const char* what, const char* clientShows,
                  const char* serverShows) {
    Rec rec = {};
    Status s;
    EndpointConfig c = client_cfg(tag, &rec);
    c.appSchemaHash = 0x1111;
    Endpoint* ep = make_client(c, &s);
    REQUIRE(s == Status::Ok && ep);
    std::string out = tu::temp_file(tag);
    tu::Child srv = tu::spawn_self(server_args(name_of(ep), out, opts));
    int code = tu::join(srv, 10000);
    std::string r = tu::read_file(out);
    printf("  server child: exit %d, status %s\n  server text: %s\n", code, tu::kv(r, "status").c_str(),
           tu::kv(r, "error").c_str());
    CHECK_EQ(code, 100 + (int)expect);
    CHECK(tu::kv(r, "status") == status_name(expect));
    std::string stext = tu::kv(r, "error");
    CHECK(tu::contains(stext, "refused by the server") && tu::contains(stext, what));
    CHECK(tu::contains(stext, clientShows) && tu::contains(stext, serverShows));
    CHECK(tu::wait_until([&] { return nev(rec, PeerEvent::Refused) == 1; }, 3000));
    std::string ctext = err_of(ep);
    printf("  client text: %s\n", ctext.c_str());
    CHECK(ctext == stext);
    CHECK(tu::wait_until([&] { return ep->conn_state() == kConnNoServer; }, 3000));
    // a matching server attaches afterwards
    const char* sch = "schema=0x1111 then=hold holdms=10000";
    std::string out2 = tu::temp_file(tag);
    tu::Child ok = tu::spawn_self(server_args(name_of(ep), out2, sch));
    CHECK(ep->wait_for_server(10000) == Status::Ok);
    CHECK_EQ(ep->generation(), 1u);
    tu::kill(ok);
    tu::join(ok, 1000);
    delete ep;
    DeleteFileA(out.c_str());
    DeleteFileA(out2.c_str());
}
}  // namespace

TEST(refusal_version) {
    refusal_case("rv", "minor=7 schema=0x1111 then=hold", Status::VersionMismatch, "protocol version", "client 1.0",
                 "server 1.7");
}
TEST(refusal_layout) {
    refusal_case("rl", "layout=0x12345678 schema=0x1111 then=hold", Status::LayoutMismatch, "layout hash",
                 "layout 0x244ED16D", "layout 0x12345678");
}
TEST(refusal_schema) {
    refusal_case("rs", "schema=0x2222 then=hold", Status::SchemaMismatch, "app schema hash", "schema 0x0000000000001111",
                 "schema 0x0000000000002222");
}

namespace {
struct ResyncCtx {
    volatile LONG calls;
    volatile LONG lastGen;
    double startUs, endUs;
    Status nested;
};
void resync_hook(void* ctx, Endpoint& ep, uint32_t gen) {
    ResyncCtx& r = *(ResyncCtx*)ctx;
    r.startUs = tu::qpc_us();
    r.lastGen = (LONG)gen;
    r.nested = ep_enter(ep.impl(), 0, true);  // the hook may call on its thread's channel
    if (r.nested == Status::Ok) ep_leave(ep.impl(), 0, true);
    Sleep(200);
    InterlockedIncrement(&r.calls);
    r.endUs = tu::qpc_us();
}
struct Enterer {
    EndpointImpl* ep;
    uint32_t ch;
    HANDLE go;
    Status s;
    double retUs;
};
DWORD WINAPI enterer_main(void* p) {
    Enterer& e = *(Enterer*)p;
    WaitForSingleObject(e.go, 5000);
    e.s = ep_enter(e.ep, e.ch, true);
    e.retUs = tu::qpc_us();
    if (e.s == Status::Ok) {
        Sleep(5);
        ep_leave(e.ep, e.ch, true);
    }
    return 0;
}
}  // namespace

TEST(server_killed_then_restart_and_resync) {
    Rec rec = {};
    Status s;
    EndpointConfig c = client_cfg("rsy", &rec);
    Endpoint* ep = make_client(c, &s);
    REQUIRE(s == Status::Ok && ep);
    ResyncCtx rc = {};
    ep->set_resync_hook(&resync_hook, &rc);
    std::string out = tu::temp_file("rsy");
    tu::Child a = tu::spawn_self(server_args(name_of(ep), out, "then=hold holdms=20000"));
    CHECK(ep->wait_for_server(10000) == Status::Ok);
    CHECK_EQ(ep->generation(), 1u);
    CHECK(ep_enter(ep->impl(), 1, true) == Status::Ok);  // first call of generation 1 runs the hook
    ep_leave(ep->impl(), 1, true);
    CHECK_EQ(rc.calls, 1);
    CHECK_EQ(rc.lastGen, 1);
    CHECK(rc.nested == Status::Ok);
    CHECK(ep_enter(ep->impl(), 1, true) == Status::Ok);  // not again
    ep_leave(ep->impl(), 1, true);
    CHECK_EQ(rc.calls, 1);

    // the server is killed while idle
    double k0 = tu::qpc_us();
    tu::kill(a);
    CHECK(tu::wait_until([&] { return nev(rec, PeerEvent::Died) == 1; }, 3000));
    printf("  Died reported %.2f ms after TerminateProcess\n", (rec.atUs[(int)PeerEvent::Died] - k0) / 1000.0);
    CHECK_EQ(rec.gen[(int)PeerEvent::Died], 1);
    CHECK(ep_enter(ep->impl(), 0, true) == Status::Disconnected);
    CHECK(ep_enter(ep->impl(), 2, true) == Status::Disconnected);
    CHECK(tu::wait_until([&] { return ep->conn_state() == kConnNoServer; }, 3000));
    tu::join(a, 1000);

    // a second server attaches: generation 2, the hook runs exactly once, other threads wait for it
    tu::Child b = tu::spawn_self(server_args(name_of(ep), out, "then=hold holdms=20000"));
    CHECK(ep->wait_for_server(10000) == Status::Ok);
    CHECK_EQ(ep->generation(), 2u);
    CHECK(tu::wait_until([&] { return nev(rec, PeerEvent::Attached) == 2; }, 1000));
    CHECK_EQ(rec.gen[(int)PeerEvent::Attached], 2);
    HANDLE go = CreateEventA(NULL, TRUE, FALSE, NULL);
    Enterer e1 = {ep->impl(), 0, go, Status::SystemError, 0};
    Enterer e2 = {ep->impl(), 1, go, Status::SystemError, 0};
    Enterer e3 = {ep->impl(), 2, go, Status::SystemError, 0};
    tu::Thread t1, t2, t3;
    t1.start(&enterer_main, &e1);
    t2.start(&enterer_main, &e2);
    t3.start(&enterer_main, &e3);
    SetEvent(go);
    CHECK(t1.join(5000) && t2.join(5000) && t3.join(5000));
    CloseHandle(go);
    CHECK(e1.s == Status::Ok && e2.s == Status::Ok && e3.s == Status::Ok);
    CHECK_EQ(rc.calls, 2);
    CHECK_EQ(rc.lastGen, 2);
    CHECK(rc.nested == Status::Ok);
    CHECK(e1.retUs >= rc.endUs && e2.retUs >= rc.endUs && e3.retUs >= rc.endUs);  // nobody overtook the hook
    printf("  resync hook ran once for generation 2 (%.0f ms); callers returned %.1f / %.1f / %.1f ms after it ended\n",
           (rc.endUs - rc.startUs) / 1000.0, (e1.retUs - rc.endUs) / 1000.0, (e2.retUs - rc.endUs) / 1000.0,
           (e3.retUs - rc.endUs) / 1000.0);
    tu::kill(b);
    tu::join(b, 1000);
    delete ep;
    DeleteFileA(out.c_str());
}

// Real calls across a server restart: a call in flight when the server dies returns PeerDied, calls fail fast
// while no server is attached, and the first call after the new server attaches runs the resync hook (which itself
// calls the new server) before it succeeds.
namespace {
struct CallResync {
    Endpoint* ep;
    volatile LONG calls;
    volatile LONG lastGen;
    Status hookCall;
    int32_t hookEcho;
    uint32_t hookServerGen;
};
void call_resync_hook(void* ctx, Endpoint& ep, uint32_t gen) {
    CallResync& r = *(CallResync*)ctx;
    r.lastGen = (LONG)gen;
    r.hookCall = call_echo(&ep, 7000 + (int32_t)gen, &r.hookEcho);
    if (r.hookCall == Status::Ok) call_u32(&ep, kMGen, &r.hookServerGen);
    InterlockedIncrement(&r.calls);
}
}  // namespace

TEST(call_across_server_restart) {
    Rec rec = {};
    Status s;
    EndpointConfig c = client_cfg("crs", &rec);
    Endpoint* ep = make_client(c, &s);
    REQUIRE(s == Status::Ok && ep);
    ep->bind_current_thread(kChannelUi);
    CallResync rc;
    memset(&rc, 0, sizeof(rc));
    rc.ep = ep;
    ep->set_resync_hook(&call_resync_hook, &rc);
    std::string out = tu::temp_file("crs");
    tu::Child a = tu::spawn_self(server_args(name_of(ep), out, "then=serve"));
    CHECK(ep->wait_for_server(10000) == Status::Ok);
    int32_t got = 0;
    CHECK(call_echo(ep, 41, &got) == Status::Ok);
    CHECK_EQ(got, 41);
    CHECK_EQ(rc.calls, 1);  // generation 1: the hook ran before the first call, and its own call worked
    CHECK(rc.hookCall == Status::Ok);
    CHECK_EQ(rc.hookEcho, 7001);
    CHECK_EQ(rc.hookServerGen, 1u);

    // the server dies inside a call
    double t0 = tu::qpc_us();
    uint32_t dummy = 0;
    CHECK(call_u32(ep, kMDie, &dummy) == Status::PeerDied);
    printf("  call in flight when the server died: PeerDied after %.2f ms\n", (tu::qpc_us() - t0) / 1000.0);
    CHECK_EQ(tu::join(a, 5000), 3);
    CHECK(tu::wait_until([&] { return nev(rec, PeerEvent::Died) == 1; }, 3000));
    t0 = tu::qpc_us();
    CHECK(call_echo(ep, 1, &got) == Status::Disconnected);  // fail fast, nothing sent
    CHECK(tu::qpc_us() - t0 < 1000.0);

    // a new server: generation 2, the hook runs (and calls the new server), then the call succeeds
    std::string outB = tu::temp_file("crsb");
    tu::Child b = tu::spawn_self(server_args(name_of(ep), outB, "then=serve"));
    CHECK(ep->wait_for_server(10000) == Status::Ok);
    CHECK_EQ(ep->generation(), 2u);
    CHECK_EQ(rc.calls, 1);  // not before the first call
    CHECK(call_echo(ep, 42, &got) == Status::Ok);
    CHECK_EQ(got, 42);
    CHECK_EQ(rc.calls, 2);
    CHECK_EQ(rc.lastGen, 2);
    CHECK(rc.hookCall == Status::Ok);
    CHECK_EQ(rc.hookEcho, 7002);
    CHECK_EQ(rc.hookServerGen, 2u);
    CHECK(call_echo(ep, 43, &got) == Status::Ok);
    CHECK_EQ(got, 43);
    CHECK_EQ(rc.calls, 2);  // once per generation
    CHECK(ep->close() == Status::Ok);
    CHECK_EQ(tu::join(b, 5000), 20 + (int)Status::PeerClosed);
    std::string r = tu::read_file(outB);
    printf("  server B wrote: %s", r.c_str());
    CHECK(tu::kv(r, "served") == "4");  // server B: hook echo, hook gen, 42, 43
    delete ep;
    DeleteFileA(out.c_str());
    DeleteFileA(outB.c_str());
}

TEST(attach_held_while_a_call_is_inside) {
    Rec rec = {};
    Status s;
    EndpointConfig c = client_cfg("hold", &rec);
    Endpoint* ep = make_client(c, &s);
    REQUIRE(s == Status::Ok && ep);
    std::string out = tu::temp_file("hold");
    tu::Child a = tu::spawn_self(server_args(name_of(ep), out, "then=hold holdms=20000"));
    CHECK(ep->wait_for_server(10000) == Status::Ok);
    CHECK(ep_enter(ep->impl(), 0, true) == Status::Ok);  // a call is in progress ...
    tu::kill(a);                                          // ... when the server dies
    tu::join(a, 1000);
    CHECK(tu::wait_until([&] { return nev(rec, PeerEvent::Died) == 1; }, 3000));
    tu::Child b = tu::spawn_self(server_args(name_of(ep), out, "then=hold holdms=20000"));
    Sleep(700);
    CHECK(ep->conn_state() != kConnConnected);  // held off
    CHECK_EQ(ep->generation(), 1u);
    ep_leave(ep->impl(), 0, true);  // the call returns
    double t0 = tu::qpc_us();
    CHECK(ep->wait_for_server(5000) == Status::Ok);
    printf("  attach completed %.1f ms after the last call left\n", (tu::qpc_us() - t0) / 1000.0);
    CHECK_EQ(ep->generation(), 2u);
    tu::kill(b);
    tu::join(b, 1000);
    delete ep;
    DeleteFileA(out.c_str());
}

TEST(server_close_seen_by_client) {
    Rec rec = {};
    Status s;
    EndpointConfig c = client_cfg("sc", &rec);
    Endpoint* ep = make_client(c, &s);
    REQUIRE(s == Status::Ok && ep);
    std::string out = tu::temp_file("sc");
    tu::Child a = tu::spawn_self(server_args(name_of(ep), out, "then=close holdms=300"));
    CHECK(ep->wait_for_server(10000) == Status::Ok);
    // a client call blocked when the server closes returns PeerClosed
    CHECK(ep_enter(ep->impl(), 0, true) == Status::Ok);
    BgWait bw;
    volatile uint32_t* w;
    bw.env = env_for(ep->impl(), 0, &w);
    bw.word = w;
    bw.seen = *w;
    bw.timeoutMs = 10000;
    bw.result = WaitResult::Changed;
    bg_wait_main(&bw);
    ep_leave(ep->impl(), 0, true);
    CHECK(bw.result == WaitResult::PeerClosed);
    CHECK(tu::wait_until([&] { return nev(rec, PeerEvent::Closed) == 1; }, 3000));
    CHECK(tu::wait_until([&] { return ep->conn_state() == kConnNoServer; }, 3000));
    ep->bind_current_thread(kChannelUi);
    int32_t got = 0;
    CHECK(call_echo(ep, 1, &got) == Status::Disconnected);  // a real call after the close fails fast
    CHECK_EQ(tu::join(a, 5000), 0);
    CHECK(tu::kv(tu::read_file(out), "closed") == "1");
    delete ep;
    DeleteFileA(out.c_str());
}

TEST(server_started_before_client) {
    Rec rec = {};
    char name[96];
    sprintf(name, "Local\\VPIpc.%u.late", (unsigned)GetCurrentProcessId());
    std::string out = tu::temp_file("late");
    tu::Child a = tu::spawn_self(server_args(name, out, "timeout=10000 then=hold holdms=20000"));
    Sleep(500);
    Status s;
    EndpointConfig c = client_cfg("late", &rec);
    Endpoint* ep = make_client(c, &s);
    REQUIRE(s == Status::Ok && ep);
    CHECK(name_of(ep) == name);
    double t0 = tu::qpc_us();
    CHECK(ep->wait_for_server(10000) == Status::Ok);
    printf("  waiting server attached %.1f ms after the client created the section\n", (tu::qpc_us() - t0) / 1000.0);
    tu::kill(a);
    tu::join(a, 1000);
    delete ep;
    DeleteFileA(out.c_str());
}

// The test process is the server here; the child is the client.
namespace {
struct BgServe {  // serve() until something other than a served call or a timeout chunk
    Channel* ch;
    uint32_t timeoutMs;
    Status result;
    double retUs;
};
DWORD WINAPI bg_serve_main(void* p) {
    BgServe& b = *(BgServe*)p;
    do b.result = b.ch->serve(b.timeoutMs);
    while (b.result == Status::Ok);
    b.retUs = tu::qpc_us();
    return 0;
}
struct ServerSide {
    tu::Child cli;
    Endpoint* ep;
    Rec rec;
    std::string out;
};
bool attach_to_child_client(ServerSide& ss, const char* tag, const char* opts) {
    memset(&ss.rec, 0, sizeof(ss.rec));
    ss.out = tu::temp_file(tag);
    ss.cli = tu::spawn_self(std::string("--child client ") + tag + " " + ss.out + " " + opts);
    if (!ss.cli.process) return false;
    char name[96];
    sprintf(name, "Local\\VPIpc.%u.%s", (unsigned)ss.cli.pid, tag);
    EndpointConfig c;
    cfg_section(c, name);
    c.wait.tickMs = 50;
    c.onPeerEvent = &on_peer_event;
    c.hookCtx = &ss.rec;
    Status s;
    double t0 = tu::qpc_us();
    ss.ep = make_server(c, 10000, &s);
    printf("  attach_server: %s in %.1f ms %s\n", status_name(s), (tu::qpc_us() - t0) / 1000.0, err_of(ss.ep).c_str());
    return s == Status::Ok;
}
}  // namespace

TEST(client_killed_server_sees_peer_died) {
    ServerSide ss;
    REQUIRE(attach_to_child_client(ss, "ck", "then=hold holdms=20000"));
    CHECK_EQ(ss.ep->generation(), 1u);
    CHECK(ss.ep->side() == Side::Server);
    CHECK_EQ(ss.ep->peer_pid(), (uint32_t)ss.cli.pid);
    CHECK_EQ(nev(ss.rec, PeerEvent::Attached), 1);
    BgServe bs;
    bs.ch = &ss.ep->channel(kChannelUi);
    bs.ch->set_handlers(&server_on_call, &server_on_post, ss.ep);
    bs.timeoutMs = 10000;
    bs.result = Status::Ok;
    tu::Thread t;
    t.start(&bg_serve_main, &bs);
    Sleep(300);
    double k0 = tu::qpc_us();
    tu::kill(ss.cli);
    CHECK(t.join(5000));
    double waitMs = (bs.retUs - k0) / 1000.0;
    CHECK(tu::wait_until([&] { return nev(ss.rec, PeerEvent::Died) == 1; }, 3000));
    double eventMs = (ss.rec.atUs[(int)PeerEvent::Died] - k0) / 1000.0;
    printf("  client killed: server serve() returned %s after %.2f ms, Died event after %.2f ms (limit 1000)\n",
           status_name(bs.result), waitMs, eventMs);
    CHECK(bs.result == Status::PeerDied);
    CHECK(waitMs < 1000 && eventMs < 1000);
    CHECK(ep_enter(ss.ep->impl(), 0, false) == Status::PeerDied);
    tu::join(ss.cli, 1000);
    delete ss.ep;
    DeleteFileA(ss.out.c_str());
}

TEST(client_close_server_sees_peer_closed) {
    ServerSide ss;
    REQUIRE(attach_to_child_client(ss, "cc", "then=closeafterattach holdms=300"));
    BgServe bs;
    bs.ch = &ss.ep->channel(kChannelUi);
    bs.ch->set_handlers(&server_on_call, &server_on_post, ss.ep);
    bs.timeoutMs = 10000;
    bs.result = Status::Ok;
    bg_serve_main(&bs);
    if (bs.result != Status::PeerClosed)
        printf("  serve returned %s; client child wrote: %s\n", status_name(bs.result), tu::read_file(ss.out).c_str());
    CHECK(bs.result == Status::PeerClosed);
    CHECK(tu::wait_until([&] { return nev(ss.rec, PeerEvent::Closed) == 1; }, 3000));
    CHECK(ep_enter(ss.ep->impl(), 0, false) == Status::PeerClosed);
    CHECK_EQ(tu::join(ss.cli, 5000), 0);
    std::string r = tu::read_file(ss.out);
    CHECK(tu::kv(r, "attached") == "Ok" && tu::kv(r, "closed") == "1");
    CHECK(tu::kv(r, "call") == "Ok" && tu::kv(r, "echo") == "5");  // the client's real call was served first
    delete ss.ep;
    DeleteFileA(ss.out.c_str());
}

int main(int argc, char** argv) {
    if (argc >= 5 && strcmp(argv[1], "--child") == 0) {
        if (strcmp(argv[2], "server") == 0) return child_server(argc, argv);
        if (strcmp(argv[2], "client") == 0) return child_client(argc, argv);
        return 2;
    }
    return vpipc_test::run_all(argc, argv);
}
