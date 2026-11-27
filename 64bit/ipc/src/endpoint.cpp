// src/endpoint.cpp - section, kernel objects, attach state machine, monitor thread, call admission, thread
// binding, ep_* hooks. Task 1202. PROTOCOL.md sections 2, 6 and 7.
//
// Attach protocol as implemented (PROTOCOL.md 7.2, with the details the draft leaves open):
//   server: open the section (retry until the timeout) -> wait for identity.magic (the client writes it
//     last) -> claim: while connState == NO_SERVER, CAS attachPid 0 -> own pid (one claimant at a time) ->
//     write the server line -> compare version, layout hash, schema hash with the identity line; on a
//     mismatch write the refusal text and reason, server.state = Refused, CAS NO_SERVER -> REFUSED, return
//     the mismatch -> otherwise open the client process, the events, create the channels, CAS NO_SERVER ->
//     ATTACH_REQUESTED -> when the generation changes (client phase 1): channel_reset_local on every channel,
//     server.state = Ready -> wait for CONNECTED (or REFUSED with client.refuseReason set: read clientRefusal,
//     ack with server.state = Refused) -> start the monitor.
//     A server that times out while ATTACH_REQUESTED withdraws: CAS ATTACH_REQUESTED -> NO_SERVER, then
//     releases its claim.
//   client monitor: ATTACH_REQUESTED -> check the server line the same way (refuse: clientRefusal text,
//     client.refuseReason, REFUSED; back to NO_SERVER once the server acks, exits, or after 2 s) -> open the
//     server process -> phase 1, once no thread is inside a call and channel_active() == 0 everywhere:
//     channel_reset_shared + channel_reset_local on every channel, generation++ (state stays ATTACH_REQUESTED,
//     so client calls still fail fast) -> phase 2, once server.state == Ready (the server has reset its cursors
//     on the zeroed blocks): CAS ATTACH_REQUESTED -> CONNECTED -> Attached hook. A server that withdraws or dies
//     between the phases: the increment is undone. (Found 2026-11-10 with real channels: resetting the server's
//     cursors after CONNECTED could read the seq of a client CALL already published and never serve it.)
//   NO_SERVER with a claim whose process is gone: the claim is cleared.
//   PEER_DEAD / server CLOSING / refusal handled: the client returns the section to NO_SERVER (server line
//     cleared, attachPid 0) as soon as no client thread is inside a call.
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <intrin.h>

#include "endpoint_impl.h"

namespace vpipc {

struct EpTlsSlot {
    uint32_t epId;      // 0 = free
    uint32_t depth;     // successful enter() not yet left
    uint32_t bound;     // channel index + 1; 0 = unbound
    uint32_t inResync;  // this thread is running the resync hook
};

namespace {

// ---- atomics on section words (x86 and x64; 64-bit via cmpxchg8b on x86) ----
inline uint32_t ld(const volatile uint32_t* p) {
    uint32_t v = *p;
    _ReadWriteBarrier();
    return v;
}
inline void st(volatile uint32_t* p, uint32_t v) { InterlockedExchange((volatile LONG*)p, (LONG)v); }
inline bool cas(volatile uint32_t* p, uint32_t expected, uint32_t desired) {
    return (uint32_t)InterlockedCompareExchange((volatile LONG*)p, (LONG)desired, (LONG)expected) == expected;
}
inline uint64_t ld64(volatile uint64_t* p) {
    return (uint64_t)InterlockedCompareExchange64((volatile LONG64*)p, 0, 0);
}
inline void st64(volatile uint64_t* p, uint64_t v) {
    LONG64 cur = InterlockedCompareExchange64((volatile LONG64*)p, 0, 0);
    for (;;) {
        LONG64 prev = InterlockedCompareExchange64((volatile LONG64*)p, (LONG64)v, cur);
        if (prev == cur) return;
        cur = prev;
    }
}

uint32_t ms_since(uint64_t qpcStart) {
    uint64_t d = qpc_now() - qpcStart;
    return (uint32_t)(d * 1000u / qpc_frequency());
}
bool expired(uint64_t qpcStart, uint32_t timeoutMs) {
    return timeoutMs != INFINITE && ms_since(qpcStart) >= timeoutMs;
}

void vfmt(char* out, uint32_t cap, const char* f, va_list ap) {
    if (!cap) return;
    int n = _vsnprintf(out, cap - 1, f, ap);
    if (n < 0 || (uint32_t)n >= cap - 1) out[cap - 1] = 0;
    else out[n] = 0;
}
void fmt(char* out, uint32_t cap, const char* f, ...) {
    va_list ap;
    va_start(ap, f);
    vfmt(out, cap, f, ap);
    va_end(ap);
}
void copy_str(char* dst, uint32_t cap, const char* src) {
    uint32_t i = 0;
    if (src)
        for (; i + 1 < cap && src[i]; ++i) dst[i] = src[i];
    if (cap) dst[i] = 0;
}
// Bounded copy out of the section (the peer may have left it unterminated).
void copy_section_text(char* dst, uint32_t cap, const volatile char* src, uint32_t srcCap) {
    uint32_t i = 0;
    for (; i + 1 < cap && i < srcCap; ++i) {
        char c = src[i];
        if (!c) break;
        dst[i] = c;
    }
    dst[i] = 0;
}

const char* side_name(Side s) { return s == Side::Client ? "client" : "server"; }

// ---- thread-local state: one TLS index for the process, a small block per thread ----
typedef EpTlsSlot TlsSlot;
const uint32_t kTlsSlots = 8;
struct TlsBlock {
    TlsSlot slot[kTlsSlots];
    uint32_t evict;
    char lastError[kErrorBytes];  // Endpoint::last_error_text(): the last failed create/attach on this thread
};
volatile LONG g_tlsIndex = (LONG)TLS_OUT_OF_INDEXES;
volatile LONG g_nextId = 0;

DWORD tls_index() {
    LONG v = g_tlsIndex;
    if (v != (LONG)TLS_OUT_OF_INDEXES) return (DWORD)v;
    DWORD idx = TlsAlloc();
    if (idx == TLS_OUT_OF_INDEXES) return idx;
    LONG prev = InterlockedCompareExchange(&g_tlsIndex, (LONG)idx, (LONG)TLS_OUT_OF_INDEXES);
    if (prev != (LONG)TLS_OUT_OF_INDEXES) {
        TlsFree(idx);
        return (DWORD)prev;
    }
    return idx;
}

// The block is never freed (~2.2 KB per thread that used an endpoint; the library is not told when a thread
// exits). Preserves GetLastError (TlsGetValue clears it).
TlsBlock* tls_block(bool create) {
    DWORD idx = tls_index();
    if (idx == TLS_OUT_OF_INDEXES) return nullptr;
    DWORD err = GetLastError();
    TlsBlock* b = (TlsBlock*)TlsGetValue(idx);
    if (!b && create) {
        b = (TlsBlock*)calloc(1, sizeof(TlsBlock));
        if (b) TlsSetValue(idx, b);
    }
    SetLastError(err);
    return b;
}

TlsSlot* tls_slot(uint32_t epId, bool create) {
    TlsBlock* b = tls_block(create);
    TlsSlot* res = nullptr;
    if (b) {
        for (uint32_t i = 0; i < kTlsSlots && !res; ++i)
            if (b->slot[i].epId == epId) res = &b->slot[i];
        if (!res && create) {
            for (uint32_t i = 0; i < kTlsSlots && !res; ++i)
                if (b->slot[i].epId == 0) res = &b->slot[i];
            if (!res) res = &b->slot[b->evict++ % kTlsSlots];
            res->epId = epId;
            res->depth = 0;
            res->bound = 0;
            res->inResync = 0;
        }
    }
    return res;
}
void tls_forget(uint32_t epId) {
    TlsSlot* t = tls_slot(epId, false);
    if (t) t->epId = 0;
}

WaitConfig g_defaultWait;
Channel g_nullChannel(nullptr);

RefuseReason compare_sides(const SideInfo& a, const SideInfo& b) {
    if (a.versionMajor != b.versionMajor || a.versionMinor != b.versionMinor) return kRefuseVersion;
    if (a.layoutHash != b.layoutHash) return kRefuseLayout;
    if (a.appSchemaHash != b.appSchemaHash) return kRefuseSchema;
    return kRefuseNone;
}
Status status_for(RefuseReason r) {
    switch (r) {
    case kRefuseVersion: return Status::VersionMismatch;
    case kRefuseLayout: return Status::LayoutMismatch;
    case kRefuseSchema: return Status::SchemaMismatch;
    default: return Status::ProtocolError;
    }
}
SideInfo side_info(const SideLine& l) {
    SideInfo s;
    s.versionMajor = l.versionMajor;
    s.versionMinor = l.versionMinor;
    s.layoutHash = l.layoutHash;
    s.appSchemaHash = l.appSchemaHash;
    s.pid = l.pid;
    s.pointerBits = l.pointerBits;
    return s;
}
bool process_alive(uint32_t pid) {
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!h) return false;
    bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
}
bool is_pow2(uint32_t v) { return v && !(v & (v - 1)); }

bool has_nul(const char* s, uint32_t cap) { return memchr(s, 0, cap) != nullptr; }

// Reads the public EndpointConfig into the internal form. The only code that touches the public config struct.
void copy_config(const EndpointConfig& in, Side side, EpCfg& c) {
    memset(c.sectionName, 0, sizeof(c.sectionName));
    memset(c.tag, 0, sizeof(c.tag));
    memset(c.ch, 0, sizeof(c.ch));
    memset(c.badConfig, 0, sizeof(c.badConfig));
    c.appSchemaHash = in.appSchemaHash;
    c.wait = in.wait;
    c.onLog = in.onLog;
    c.onPeerEvent = in.onPeerEvent;
    c.hookCtx = in.hookCtx;
    c.test = in.test;
    c.channelCount = 0;
    if (!has_nul(in.sectionName, kSectionNameBytes)) {
        fmt(c.badConfig, sizeof(c.badConfig), "sectionName is not NUL-terminated within %u chars", kSectionNameBytes);
        return;
    }
    copy_str(c.sectionName, kSectionNameBytes, in.sectionName);
    if (!has_nul(in.tag, kTagBytes)) {
        fmt(c.badConfig, sizeof(c.badConfig), "tag is not NUL-terminated within %u chars", kTagBytes);
        return;
    }
    copy_str(c.tag, kTagBytes, in.tag);
    if (side == Side::Server) return;  // the server reads the channel table from the section
    if (in.channelCount < 1 || in.channelCount > kMaxChannels) {
        fmt(c.badConfig, sizeof(c.badConfig), "channel count %u not in 1..%u", in.channelCount, kMaxChannels);
        return;
    }
    c.channelCount = in.channelCount;
    for (uint32_t i = 0; i < c.channelCount; ++i) {
        const ChannelConfig& s = in.channels[i];
        ChanCfg& d = c.ch[i];
        if (!s.name[0] || !has_nul(s.name, kChannelNameBytes)) {
            fmt(c.badConfig, sizeof(c.badConfig), "channel %u: name must be 1..%u chars", i, kChannelNameBytes - 1);
            return;
        }
        copy_str(d.name, kChannelNameBytes, s.name);
        d.initiator = s.initiator == Side::Client ? kInitiatorClient : kInitiatorServer;
        d.flags = s.shared ? kChannelShared : 0u;
        d.dataBytes = s.dataBytes;
        d.ringBytes = s.ringBytes;
    }
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------
// refusal text
// ---------------------------------------------------------------------------------------------------------
const char* refuse_reason_name(RefuseReason r) {
    switch (r) {
    case kRefuseNone: return "none";
    case kRefuseVersion: return "protocol version";
    case kRefuseLayout: return "layout hash";
    case kRefuseSchema: return "app schema hash";
    case kRefuseBusy: return "busy";
    }
    return "unknown";
}

void format_refusal(char* out, uint32_t cap, const char* refuser, RefuseReason reason, const SideInfo& c,
                    const SideInfo& s) {
    fmt(out, cap,
        "vpipc attach refused by the %s: %s differs (client %u.%u layout 0x%08X schema 0x%08X%08X pid %u %u-bit; "
        "server %u.%u layout 0x%08X schema 0x%08X%08X pid %u %u-bit)",
        refuser, refuse_reason_name(reason), (unsigned)c.versionMajor, (unsigned)c.versionMinor,
        (unsigned)c.layoutHash, (unsigned)(c.appSchemaHash >> 32), (unsigned)c.appSchemaHash, (unsigned)c.pid,
        (unsigned)c.pointerBits, (unsigned)s.versionMajor, (unsigned)s.versionMinor, (unsigned)s.layoutHash,
        (unsigned)(s.appSchemaHash >> 32), (unsigned)s.appSchemaHash, (unsigned)s.pid, (unsigned)s.pointerBits);
}

// ---------------------------------------------------------------------------------------------------------
// construction and teardown
// ---------------------------------------------------------------------------------------------------------
static EndpointImpl* alloc_impl(const EpCfg& cfg, Side side) {
    void* mem = malloc(sizeof(EndpointImpl));
    if (!mem) return nullptr;
    EndpointImpl* ep = EndpointImpl::construct_(mem);
    ep->side_ = side;
    ep->cfg_ = cfg;
    if (!ep->cfg_.wait.tickMs) ep->cfg_.wait.tickMs = 1;
    // Bug 0113: a client never waits on a living but silent server without a bound (vpipc.h, clientCallTimeoutMs).
    if (side == Side::Client && !ep->cfg_.wait.callTimeoutMs)
        ep->cfg_.wait.callTimeoutMs = ep->cfg_.wait.clientCallTimeoutMs;
    ep->id_ = (uint32_t)InterlockedIncrement(&g_nextId);
    ep->self_.versionMajor = cfg.test.enabled ? cfg.test.versionMajor : kVersionMajor;
    ep->self_.versionMinor = cfg.test.enabled ? cfg.test.versionMinor : kVersionMinor;
    ep->self_.layoutHash = cfg.test.enabled ? cfg.test.layoutHash : kLayoutHash;
    ep->self_.appSchemaHash = cfg.appSchemaHash;
    ep->self_.pid = GetCurrentProcessId();
    ep->self_.pointerBits = (uint32_t)(sizeof(void*) * 8);
    ep->stopEvent_ = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    return ep;
}

EndpointImpl* EndpointImpl::construct_(void* mem) { return new (mem) EndpointImpl(); }

EndpointImpl* EndpointImpl::create_detached_for_test(Side side, HANDLE peerProcess, const WaitConfig& wait) {
    EpCfg cfg;
    memset(cfg.sectionName, 0, sizeof(cfg.sectionName));
    memset(cfg.tag, 0, sizeof(cfg.tag));
    memset(cfg.ch, 0, sizeof(cfg.ch));
    memset(cfg.badConfig, 0, sizeof(cfg.badConfig));
    cfg.appSchemaHash = 0;
    cfg.channelCount = 0;
    cfg.wait = wait;
    cfg.onLog = nullptr;
    cfg.onPeerEvent = nullptr;
    cfg.hookCtx = nullptr;
    EndpointImpl* ep = alloc_impl(cfg, side);
    if (!ep) return nullptr;
    ep->detached_ = true;
    void* mem = malloc(sizeof(ConnLine) + 64);
    ep->detachedMem_ = mem;
    ConnLine* c = (ConnLine*)(((uintptr_t)mem + 63) & ~(uintptr_t)63);
    memset((void*)c, 0, sizeof(ConnLine));
    c->connState = kConnConnected;
    c->generation = 1;
    ep->conn_ = c;
    ep->myGeneration_ = 1;
    ep->peerProcess_ = peerProcess;
    ep->ownPeerProcess_ = false;
    return ep;
}

void EndpointImpl::destroy(EndpointImpl* ep) {
    if (!ep) return;
    if (!ep->detached_ && ep->hdr_ && !ep->closed_) ep->close();
    ep->stop_monitor();
    ep->release_resources();
    tls_forget((uint32_t)ep->id_);
    ep->~EndpointImpl();
    free(ep);
}

void EndpointImpl::stop_monitor() {
    if (!monitorThread_) return;
    SetEvent(stopEvent_);
    if (GetCurrentThreadId() == monitorTid_) {
        logf(LogLevel::Error, "endpoint stopped from its own monitor thread (a peer-event hook): not joined");
        return;
    }
    WaitForSingleObject(monitorThread_, INFINITE);
    CloseHandle(monitorThread_);
    monitorThread_ = nullptr;
}

void EndpointImpl::release_resources() {
    for (uint32_t i = 0; i < kMaxChannels; ++i) {
        if (chImpl_[i]) channel_destroy(chImpl_[i]);
        chImpl_[i] = nullptr;
    }
    if (chPublic_) {
        for (uint32_t i = 0; i < chPublicCount_; ++i) chPublic_[i].~Channel();
        free(chPublic_);
        chPublic_ = nullptr;
        chPublicCount_ = 0;
    }
    for (uint32_t i = 0; i < 2 * kMaxChannels; ++i) {
        if (events_[i]) CloseHandle(events_[i]);
        events_[i] = nullptr;
    }
    if (base_) UnmapViewOfFile(base_);
    base_ = nullptr;
    hdr_ = nullptr;
    if (mapping_) CloseHandle(mapping_);
    mapping_ = nullptr;
    if (peerProcess_ && ownPeerProcess_) CloseHandle(peerProcess_);
    peerProcess_ = nullptr;
    for (uint32_t i = 0; i < oldPeerCount_; ++i) CloseHandle(oldPeers_[i]);
    oldPeerCount_ = 0;
    if (pending_) CloseHandle(pending_);
    pending_ = nullptr;
    if (stopEvent_) CloseHandle(stopEvent_);
    stopEvent_ = nullptr;
    if (detachedMem_) free(detachedMem_);
    detachedMem_ = nullptr;
    conn_ = nullptr;
}

bool EndpointImpl::open_events(bool create) {
    for (uint32_t i = 0; i < channelCount_; ++i) {
        for (uint32_t s = 0; s < 2; ++s) {
            char name[kSectionNameBytes + 16];
            fmt(name, sizeof(name), "%s.w%u.%c", sectionName_, i, s == 0 ? 'c' : 's');
            HANDLE h;
            if (create) {
                h = CreateEventA(nullptr, FALSE, FALSE, name);
                if (h && GetLastError() == ERROR_ALREADY_EXISTS) {
                    CloseHandle(h);
                    set_error("event %s already exists", name);
                    return false;
                }
            } else {
                h = OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, name);
            }
            if (!h) {
                set_error("%s(%s) failed, error %u", create ? "CreateEvent" : "OpenEvent", name,
                          (unsigned)GetLastError());
                return false;
            }
            events_[2 * i + s] = h;
        }
    }
    return true;
}

bool EndpointImpl::create_channels() {
    chPublic_ = (Channel*)malloc(sizeof(Channel) * (channelCount_ ? channelCount_ : 1));
    if (!chPublic_) {
        set_error("out of memory");
        return false;
    }
    for (uint32_t i = 0; i < channelCount_; ++i) {
        const ChannelDesc& d = hdr_->channels[i];
        ChannelEnv env;
        env.side = side_;
        env.index = i;
        env.block = (ChannelBlock*)(base_ + d.offset);
        env.desc = d;
        env.data = (uint8_t*)env.block + sizeof(ChannelBlock);
        env.ringC2S = env.data + d.dataBytes;
        env.ringS2C = env.ringC2S + d.ringBytes;
        const bool client = side_ == Side::Client;
        env.wait.mySleeping = client ? &env.block->clientCtl.sleeping : &env.block->serverCtl.sleeping;
        env.wait.peerSleeping = client ? &env.block->serverCtl.sleeping : &env.block->clientCtl.sleeping;
        env.wait.myEvent = events_[2 * i + (client ? 0 : 1)];
        env.wait.peerEvent = events_[2 * i + (client ? 1 : 0)];
        env.wait.ep = this;
        env.ep = this;
        chImpl_[i] = channel_create(env);
        if (!chImpl_[i]) {
            set_error("channel_create(%u) failed", i);
            return false;
        }
        new (&chPublic_[i]) Channel(chImpl_[i]);
        chPublicCount_ = i + 1;
    }
    return true;
}

bool EndpointImpl::start_monitor() {
    if (!stopEvent_) {
        set_error("CreateEvent (monitor stop) failed");
        return false;
    }
    monitorThread_ = CreateThread(nullptr, 64 * 1024, &EndpointImpl::monitor_main, this,
                                  STACK_SIZE_PARAM_IS_A_RESERVATION, &monitorTid_);
    if (!monitorThread_) {
        set_error("CreateThread (monitor) failed, error %u", (unsigned)GetLastError());
        return false;
    }
    return true;
}

Status EndpointImpl::create_client_impl(const EpCfg& cfg, EndpointImpl** out) {
    EndpointImpl* ep = alloc_impl(cfg, Side::Client);
    *out = ep;
    if (!ep) return Status::SystemError;
    InterlockedExchange(&ep->closed_, 1);  // until fully up
    if (cfg.badConfig[0]) {
        ep->set_error("create_client: invalid config: %s", cfg.badConfig);
        ep->logf(LogLevel::Error, "%s", ep->lastError_);
        return Status::InvalidArgument;
    }
    // validate channels
    uint64_t total = kHeaderBytes;
    for (uint32_t i = 0; i < cfg.channelCount; ++i) {
        const ChanCfg& c = cfg.ch[i];
        if (!c.dataBytes || (c.dataBytes % kLine) || c.dataBytes > (256u << 20)) {
            ep->set_error("create_client: channel %u (%s): dataBytes %u must be a non-zero multiple of 64 (<= 256 MiB)",
                          i, c.name, c.dataBytes);
            ep->logf(LogLevel::Error, "%s", ep->lastError_);
            return Status::InvalidArgument;
        }
        if (!is_pow2(c.ringBytes) || c.ringBytes < 4096 || c.ringBytes > (64u << 20)) {
            ep->set_error("create_client: channel %u (%s): ringBytes %u must be a power of two in 4096..64 MiB", i,
                          c.name, c.ringBytes);
            ep->logf(LogLevel::Error, "%s", ep->lastError_);
            return Status::InvalidArgument;
        }
        total += channel_block_bytes(c.dataBytes, c.ringBytes);
    }
    if (total > (1ull << 30)) {
        ep->set_error("create_client: section of %u MiB is too large", (unsigned)(total >> 20));
        return Status::InvalidArgument;
    }
    if (cfg.sectionName[0]) copy_str(ep->sectionName_, kSectionNameBytes, cfg.sectionName);
    else
        fmt(ep->sectionName_, kSectionNameBytes, "Local\\VPIpc.%u.%s", (unsigned)GetCurrentProcessId(),
            cfg.tag[0] ? cfg.tag : "main");

    ep->mapping_ = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD)total, ep->sectionName_);
    if (!ep->mapping_) {
        ep->set_error("CreateFileMapping(%s, %u bytes) failed, error %u", ep->sectionName_, (unsigned)total,
                      (unsigned)GetLastError());
        ep->logf(LogLevel::Error, "%s", ep->lastError_);
        return Status::SystemError;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        ep->set_error("section %s already exists (another client with this name)", ep->sectionName_);
        ep->logf(LogLevel::Error, "%s", ep->lastError_);
        return Status::SystemError;
    }
    ep->base_ = (uint8_t*)MapViewOfFile(ep->mapping_, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, (SIZE_T)total);
    if (!ep->base_) {
        ep->set_error("MapViewOfFile(%s) failed, error %u", ep->sectionName_, (unsigned)GetLastError());
        ep->logf(LogLevel::Error, "%s", ep->lastError_);
        return Status::SystemError;
    }
    ep->totalBytes_ = (uint32_t)total;
    Header* h = (Header*)ep->base_;  // pagefile-backed: zero-filled
    ep->hdr_ = h;
    ep->conn_ = &h->conn;
    ep->mine_ = &h->client;
    ep->peer_ = &h->server;
    ep->channelCount_ = cfg.channelCount;

    IdentityLine& id = h->identity;
    id.versionMajor = ep->self_.versionMajor;
    id.versionMinor = ep->self_.versionMinor;
    id.headerBytes = kHeaderBytes;
    id.totalBytes = (uint32_t)total;
    id.layoutHash = ep->self_.layoutHash;
    id.channelCount = cfg.channelCount;
    id.appSchemaHash = ep->self_.appSchemaHash;
    id.qpcFrequency = qpc_frequency();
    id.createdQpc = qpc_now();
    SideLine& me = h->client;
    me.pid = ep->self_.pid;
    me.pointerBits = ep->self_.pointerBits;
    me.state = kSideStarting;
    me.versionMajor = ep->self_.versionMajor;
    me.versionMinor = ep->self_.versionMinor;
    me.layoutHash = ep->self_.layoutHash;
    me.appSchemaHash = ep->self_.appSchemaHash;
    uint32_t off = kHeaderBytes;
    for (uint32_t i = 0; i < cfg.channelCount; ++i) {
        ChannelDesc& d = h->channels[i];
        const ChanCfg& c = cfg.ch[i];
        d.offset = off;
        d.initiator = c.initiator;
        d.flags = c.flags;
        d.dataBytes = c.dataBytes;
        d.ringBytes = c.ringBytes;
        memcpy(d.name, c.name, kChannelNameBytes);
        off += channel_block_bytes(c.dataBytes, c.ringBytes);
    }
    h->conn.connState = kConnNoServer;
    h->conn.generation = 0;
    h->conn.attachPid = 0;

    if (!ep->open_events(true) || !ep->create_channels()) {
        ep->logf(LogLevel::Error, "%s", ep->lastError_);
        return Status::SystemError;
    }
    ep->heartbeat();
    st(&me.state, kSideReady);
    InterlockedExchange(&ep->closed_, 0);
    if (!ep->start_monitor()) {
        InterlockedExchange(&ep->closed_, 1);
        ep->logf(LogLevel::Error, "%s", ep->lastError_);
        return Status::SystemError;
    }
    st((volatile uint32_t*)&id.magic, kMagic);  // last: the server reads nothing before it sees the magic
    ep->logf(LogLevel::Info, "section %s created: %u channels, %u bytes, protocol %u.%u, layout 0x%08X",
             ep->sectionName_, cfg.channelCount, (unsigned)total, (unsigned)ep->self_.versionMajor,
             (unsigned)ep->self_.versionMinor, (unsigned)ep->self_.layoutHash);
    return Status::Ok;
}

Status EndpointImpl::attach_server_impl(const EpCfg& cfg, EndpointImpl** out, uint32_t timeoutMs) {
    EndpointImpl* ep = alloc_impl(cfg, Side::Server);
    *out = ep;
    if (!ep) return Status::SystemError;
    InterlockedExchange(&ep->closed_, 1);  // until connected
    const uint64_t t0 = qpc_now();
    if (cfg.badConfig[0]) {
        ep->set_error("attach_server: invalid config: %s", cfg.badConfig);
        ep->logf(LogLevel::Error, "%s", ep->lastError_);
        return Status::InvalidArgument;
    }
    if (!cfg.sectionName[0]) {
        ep->set_error("attach_server: EndpointConfig::sectionName is required");
        ep->logf(LogLevel::Error, "%s", ep->lastError_);
        return Status::InvalidArgument;
    }
    copy_str(ep->sectionName_, kSectionNameBytes, cfg.sectionName);

    // 1. open, retrying while the client is not up yet
    for (;;) {
        ep->mapping_ = OpenFileMappingA(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, ep->sectionName_);
        if (ep->mapping_) break;
        DWORD e = GetLastError();
        if (e != ERROR_FILE_NOT_FOUND) {
            ep->set_error("OpenFileMapping(%s) failed, error %u", ep->sectionName_, (unsigned)e);
            ep->logf(LogLevel::Error, "%s", ep->lastError_);
            return Status::SystemError;
        }
        if (expired(t0, timeoutMs)) {
            ep->set_error("attach_server: section %s did not appear within %u ms", ep->sectionName_, timeoutMs);
            ep->logf(LogLevel::Error, "%s", ep->lastError_);
            return Status::Timeout;
        }
        Sleep(10);
    }
    ep->base_ = (uint8_t*)MapViewOfFile(ep->mapping_, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
    if (!ep->base_) {
        ep->set_error("MapViewOfFile(%s) failed, error %u", ep->sectionName_, (unsigned)GetLastError());
        ep->logf(LogLevel::Error, "%s", ep->lastError_);
        return Status::SystemError;
    }
    MEMORY_BASIC_INFORMATION mbi;
    SIZE_T mapped = VirtualQuery(ep->base_, &mbi, sizeof(mbi)) ? mbi.RegionSize : 0;
    if (mapped < kHeaderBytes) {
        ep->set_error("section %s is too small (%u bytes)", ep->sectionName_, (unsigned)mapped);
        return Status::ProtocolError;
    }
    Header* h = (Header*)ep->base_;
    ep->hdr_ = h;
    ep->conn_ = &h->conn;
    ep->mine_ = &h->server;
    ep->peer_ = &h->client;

    // 2. the client writes the magic last
    while (ld((volatile uint32_t*)&h->identity.magic) != kMagic) {
        if (expired(t0, timeoutMs)) {
            ep->set_error("attach_server: section %s was not initialised within %u ms", ep->sectionName_, timeoutMs);
            ep->logf(LogLevel::Error, "%s", ep->lastError_);
            return Status::Timeout;
        }
        Sleep(1);
    }

    // 3. claim the server line
    const uint32_t myPid = ep->self_.pid;
    for (;;) {
        uint32_t s = ld(&h->conn.connState);
        if (s == kConnClosing && ld(&h->client.state) == kSideClosing) {
            ep->set_error("attach_server: the client of %s is closing", ep->sectionName_);
            return Status::Disconnected;
        }
        if (s == kConnNoServer && cas(&h->conn.attachPid, 0, myPid)) {
            if (ld(&h->conn.connState) == kConnNoServer) break;
            cas(&h->conn.attachPid, myPid, 0);
        }
        if (expired(t0, timeoutMs)) {
            ep->set_error("attach_server: section %s stayed busy for %u ms (state %u, claimed by pid %u)",
                          ep->sectionName_, timeoutMs, (unsigned)ld(&h->conn.connState),
                          (unsigned)ld(&h->conn.attachPid));
            ep->logf(LogLevel::Error, "%s", ep->lastError_);
            return Status::Timeout;
        }
        Sleep(5);
    }

    // 4. version check (the identity line is the client's claim)
    SideInfo cli;
    cli.versionMajor = h->identity.versionMajor;
    cli.versionMinor = h->identity.versionMinor;
    cli.layoutHash = h->identity.layoutHash;
    cli.appSchemaHash = h->identity.appSchemaHash;
    cli.pid = h->client.pid;
    cli.pointerBits = h->client.pointerBits;
    SideLine& me = h->server;
    me.pid = myPid;
    me.pointerBits = ep->self_.pointerBits;
    me.versionMajor = ep->self_.versionMajor;
    me.versionMinor = ep->self_.versionMinor;
    me.layoutHash = ep->self_.layoutHash;
    me.appSchemaHash = ep->self_.appSchemaHash;
    me.refuseReason = kRefuseNone;
    st64(&me.heartbeatCount, 0);
    st64(&me.heartbeatQpc, qpc_now());
    st(&me.state, kSideStarting);
    RefuseReason why = compare_sides(cli, ep->self_);
    if (why != kRefuseNone) {
        char text[kRefusalTextBytes];
        format_refusal(text, sizeof(text), "server", why, cli, ep->self_);
        memcpy(h->serverRefusal, text, sizeof(text));
        me.refuseReason = why;
        st(&me.state, kSideRefused);
        cas(&h->conn.connState, kConnNoServer, kConnRefused);
        ep->set_error("%s", text);
        ep->logf(LogLevel::Error, "%s", text);
        return status_for(why);
    }

    // 5. client process, channel table, events, channels
    ep->peerPid_ = cli.pid;
    ep->peerProcess_ = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, cli.pid);
    if (!ep->peerProcess_) {
        cas(&h->conn.attachPid, myPid, 0);
        ep->set_error("attach_server: cannot open client process %u, error %u", (unsigned)cli.pid,
                      (unsigned)GetLastError());
        ep->logf(LogLevel::Error, "%s", ep->lastError_);
        return Status::PeerDied;
    }
    const uint32_t total = h->identity.totalBytes;
    const uint32_t count = h->identity.channelCount;
    bool tableOk = h->identity.headerBytes == kHeaderBytes && total <= mapped && count >= 1 && count <= kMaxChannels;
    for (uint32_t i = 0; tableOk && i < count; ++i) {
        const ChannelDesc& d = h->channels[i];
        uint64_t end = (uint64_t)d.offset + channel_block_bytes(d.dataBytes, d.ringBytes);
        tableOk = d.offset >= kHeaderBytes && (d.offset % kLine) == 0 && d.dataBytes && (d.dataBytes % kLine) == 0 &&
                  is_pow2(d.ringBytes) && d.ringBytes >= 4096 && end <= total;
    }
    if (!tableOk) {
        cas(&h->conn.attachPid, myPid, 0);
        ep->set_error("attach_server: section %s has an invalid header or channel table", ep->sectionName_);
        ep->logf(LogLevel::Error, "%s", ep->lastError_);
        return Status::ProtocolError;
    }
    ep->totalBytes_ = total;
    ep->channelCount_ = count;
    if (!ep->open_events(false) || !ep->create_channels()) {
        cas(&h->conn.attachPid, myPid, 0);
        ep->logf(LogLevel::Error, "%s", ep->lastError_);
        return Status::SystemError;
    }

    // 6. request, 7. wait for the client's verdict. Accept is two-phase: the client resets the blocks and
    // publishes generation + 1 while still ATTACH_REQUESTED; this side then resets its cursors on the zeroed
    // blocks and reports Ready; only then does the client set CONNECTED (and its threads start calling).
    const uint32_t genAtRequest = ld(&h->conn.generation);
    bool ready = false;
    if (!cas(&h->conn.connState, kConnNoServer, kConnAttachRequested)) {
        cas(&h->conn.attachPid, myPid, 0);
        ep->set_error("attach_server: section %s changed state during attach (state %u)", ep->sectionName_,
                      (unsigned)ld(&h->conn.connState));
        return Status::Disconnected;
    }
    for (;;) {
        uint32_t s = ld(&h->conn.connState);
        if (s == kConnAttachRequested && !ready && ld(&h->conn.generation) != genAtRequest) {
            ep->myGeneration_ = ld(&h->conn.generation);
            for (uint32_t i = 0; i < count; ++i) {
                channel_reset_local(ep->chImpl_[i]);
                ep->chGen_[i] = (LONG)ep->myGeneration_;
            }
            ready = true;
            st(&me.state, kSideReady);
            continue;
        }
        if (s == kConnConnected && ld(&h->conn.attachPid) == myPid && ready) break;
        if (s == kConnRefused && h->client.refuseReason != kRefuseNone) {
            RefuseReason r = (RefuseReason)h->client.refuseReason;
            char text[kRefusalTextBytes];
            copy_section_text(text, sizeof(text), h->clientRefusal, kRefusalTextBytes);
            st(&me.state, kSideRefused);  // ack: the client returns the section to NO_SERVER
            ep->set_error("%s", text);
            ep->logf(LogLevel::Error, "%s", text);
            return status_for(r);
        }
        if (WaitForSingleObject(ep->peerProcess_, 0) == WAIT_OBJECT_0) {
            ep->set_error("attach_server: client process %u exited during attach", (unsigned)cli.pid);
            ep->logf(LogLevel::Error, "%s", ep->lastError_);
            return Status::PeerDied;
        }
        if (s != kConnAttachRequested) {
            ep->set_error("attach_server: section %s left ATTACH_REQUESTED for state %u (phase %s)", ep->sectionName_,
                          (unsigned)s, ready ? "ready" : "requested");
            ep->logf(LogLevel::Error, "%s", ep->lastError_);
            return Status::Disconnected;
        }
        if (expired(t0, timeoutMs)) {
            if (cas(&h->conn.connState, kConnAttachRequested, kConnNoServer)) {
                cas(&h->conn.attachPid, myPid, 0);
                st(&me.state, kSideAbsent);
                ep->set_error("attach_server: client pid %u did not accept within %u ms", (unsigned)cli.pid,
                              timeoutMs);
                ep->logf(LogLevel::Error, "%s", ep->lastError_);
                return Status::Timeout;
            }
            continue;  // the client moved first: re-evaluate
        }
        Sleep(1);
    }

    ep->peerBeatCount_ = ld64(&h->client.heartbeatCount);
    ep->peerBeatSeenQpc_ = qpc_now();
    ep->heartbeat();
    InterlockedExchange(&ep->closed_, 0);
    if (!ep->start_monitor()) {
        ep->logf(LogLevel::Error, "%s", ep->lastError_);
        ep->close();
        return Status::SystemError;
    }
    ep->logf(LogLevel::Info, "attached to %s as generation %u (client pid %u, %u channels, %u ms)", ep->sectionName_,
             ep->myGeneration_, (unsigned)cli.pid, count, ms_since(t0));
    ep->fire(PeerEvent::Attached, ep->myGeneration_);
    return Status::Ok;
}

// ---------------------------------------------------------------------------------------------------------
// public behaviour
// ---------------------------------------------------------------------------------------------------------
Status EndpointImpl::close() {
    if (InterlockedExchange(&closed_, 1)) return Status::Ok;
    if (hdr_) {
        st(&mine_->state, kSideClosing);
        if (side_ == Side::Client) {
            st(&conn_->connState, kConnClosing);
        } else if (ld(&conn_->generation) == myGeneration_) {
            cas(&conn_->connState, kConnConnected, kConnClosing);
        }
        for (uint32_t i = 0; i < 2 * channelCount_; ++i)
            if (events_[i]) SetEvent(events_[i]);  // blocked waits on both sides re-check the state now
        logf(LogLevel::Info, "closed %s", sectionName_);
    }
    stop_monitor();
    return Status::Ok;
}

Status EndpointImpl::wait_for_server(uint32_t timeoutMs) {
    const uint64_t t0 = qpc_now();
    for (;;) {
        if (closed_) return Status::Disconnected;
        if (side_ == Side::Server) return Status::Ok;
        if (ld(&conn_->connState) == kConnConnected) return Status::Ok;
        if (expired(t0, timeoutMs)) return Status::Timeout;
        Sleep(1);
    }
}

void EndpointImpl::set_resync_hook(ResyncHook hook, void* ctx) {
    AcquireSRWLockExclusive(&resyncLock_);
    resyncHook_ = hook;
    resyncCtx_ = ctx;
    ReleaseSRWLockExclusive(&resyncLock_);
}

Channel& EndpointImpl::channel(uint32_t index) {
    if (index >= chPublicCount_) {
        logf(LogLevel::Error, "channel(%u): no such channel (%u channels)", index, chPublicCount_);
        return g_nullChannel;
    }
    return chPublic_[index];
}

void EndpointImpl::bind_current_thread(uint32_t index) {
    TlsSlot* t = tls_slot((uint32_t)id_, true);
    if (t) t->bound = index < channelCount_ ? index + 1 : 0;
}

uint32_t EndpointImpl::current_channel_index() {
    TlsSlot* t = tls_slot((uint32_t)id_, false);
    if (t && t->bound) return t->bound - 1;
    for (uint32_t i = 0; i < channelCount_; ++i)
        if (hdr_ && (hdr_->channels[i].flags & kChannelShared)) return i;
    return 0;
}

Channel& EndpointImpl::current_channel() { return channel(current_channel_index()); }

uint32_t EndpointImpl::generation() const {
    if (side_ == Side::Server || !conn_) return myGeneration_;
    return ld(&conn_->generation);
}

void EndpointImpl::copy_last_error(char* out, uint32_t cap) const {
    AcquireSRWLockShared(&errLock_);
    copy_str(out, cap, lastError_);
    ReleaseSRWLockShared(&errLock_);
}

void EndpointImpl::set_error(const char* f, ...) {
    char tmp[kErrorBytes];
    va_list ap;
    va_start(ap, f);
    vfmt(tmp, sizeof(tmp), f, ap);
    va_end(ap);
    AcquireSRWLockExclusive(&errLock_);
    memcpy(lastError_, tmp, sizeof(tmp));
    ReleaseSRWLockExclusive(&errLock_);
}

void EndpointImpl::log(LogLevel level, const char* f, va_list ap) const {
    char msg[1536];
    int n = _snprintf(msg, sizeof(msg) - 1, "vpipc %s %u: ", side_name(side_), (unsigned)self_.pid);
    if (n < 0) n = 0;
    vfmt(msg + n, (uint32_t)(sizeof(msg) - (size_t)n), f, ap);
    if (cfg_.onLog) cfg_.onLog(cfg_.hookCtx, level, msg);
    else if (level >= LogLevel::Info) {
        fputs(msg, stderr);
        fputc('\n', stderr);
    }
}

void EndpointImpl::logf(LogLevel level, const char* f, ...) const {
    va_list ap;
    va_start(ap, f);
    log(level, f, ap);
    va_end(ap);
}

void EndpointImpl::fire(PeerEvent ev, uint32_t gen) {
    if (cfg_.onPeerEvent) cfg_.onPeerEvent(cfg_.hookCtx, ev, gen);
}

// ---------------------------------------------------------------------------------------------------------
// call admission
// ---------------------------------------------------------------------------------------------------------
Status EndpointImpl::enter(uint32_t ch, bool initiating) {
    if (ch >= channelCount_ && !detached_) return Status::ProtocolError;
    if (enter_fast(ch)) return Status::Ok;
    TlsSlot* t = tls_slot((uint32_t)id_, true);
    InterlockedIncrement(&inside_);  // before reading the state: the monitor's attach checks inside_ after
    Status s = admit(ch, initiating, t);
    if (s != Status::Ok) {
        InterlockedDecrement(&inside_);
        return s;
    }
    if (t) ++t->depth;
    return Status::Ok;
}

Status EndpointImpl::admit(uint32_t ch, bool initiating, EpTlsSlot* t) {
    if (closed_) return Status::Disconnected;
    const uint32_t s = ld(&conn_->connState);
    const uint32_t gen = ld(&conn_->generation);
    if (side_ == Side::Server) {
        if (s == kConnConnected && gen == myGeneration_) return Status::Ok;
        if (s == kConnPeerDead) return Status::PeerDied;
        return Status::PeerClosed;
    }
    if (s != kConnConnected) return Status::Disconnected;
    if (detached_) return Status::Ok;
    if (initiating && (!t || !t->inResync) && resyncedGen_ != gen) {
        run_resync(gen, t);
        if (ld(&conn_->connState) != kConnConnected || ld(&conn_->generation) != gen) return Status::Disconnected;
    }
    if ((uint32_t)chGen_[ch] != gen) {
        LONG prev = InterlockedExchange(&chGen_[ch], (LONG)gen);
        if ((uint32_t)prev != gen) channel_reset_local(chImpl_[ch]);
    }
    return Status::Ok;
}

void EndpointImpl::run_resync(uint32_t gen, EpTlsSlot* t) {
    AcquireSRWLockExclusive(&resyncLock_);
    for (;;) {
        if (resyncedGen_ == gen) {
            ReleaseSRWLockExclusive(&resyncLock_);
            return;
        }
        if (!resyncRunning_) break;
        SleepConditionVariableSRW(&resyncCv_, &resyncLock_, INFINITE, 0);
    }
    resyncRunning_ = true;
    ResyncHook hook = resyncHook_;
    void* ctx = resyncCtx_;
    ReleaseSRWLockExclusive(&resyncLock_);
    if (hook && owner_) {
        logf(LogLevel::Info, "running the resync hook for generation %u", gen);
        if (t) t->inResync = 1;
        hook(ctx, *owner_, gen);
        if (t) t->inResync = 0;
    }
    AcquireSRWLockExclusive(&resyncLock_);
    resyncedGen_ = gen;
    resyncRunning_ = false;
    WakeAllConditionVariable(&resyncCv_);
    ReleaseSRWLockExclusive(&resyncLock_);
}

void EndpointImpl::leave(uint32_t ch, bool initiating) {
    (void)initiating;
    if (leave_fast(ch)) return;
    TlsSlot* t = tls_slot((uint32_t)id_, false);
    if (t) {
        if (!t->depth) return;  // leave without a successful enter: ignored
        --t->depth;
    } else if (inside_ <= 0) {
        return;
    }
    InterlockedDecrement(&inside_);
}

bool EndpointImpl::wait_verdict(uint32_t gen0, WaitResult* out) const {
    if (closed_) {
        *out = WaitResult::PeerClosed;
        return true;
    }
    const uint32_t s = ld(&conn_->connState);
    const uint32_t gen = ld(&conn_->generation);
    if (side_ == Side::Client) {
        if (s == kConnClosing) {
            *out = WaitResult::PeerClosed;
            return true;
        }
        if (s != kConnConnected || gen != gen0) {
            *out = WaitResult::PeerDied;
            return true;
        }
    } else {
        if (s == kConnPeerDead) {
            *out = WaitResult::PeerDied;
            return true;
        }
        if (s != kConnConnected || gen != myGeneration_) {
            *out = WaitResult::PeerClosed;
            return true;
        }
    }
    if (peerHung_ && cfg_.wait.failOnHung) {
        *out = WaitResult::PeerHung;
        return true;
    }
    return false;
}

bool EndpointImpl::any_active() const {
    // Fast-path admission (enter_fast) publishes a thread's presence with a plain store into its channel's active
    // count: make every such store visible, and order this thread's earlier state writes before the threads' later
    // loads, before reading the counts (Vista+; a cross-processor interrupt, only on the monitor's rare paths).
    FlushProcessWriteBuffers();
    if (inside_ > 0) return true;
    for (uint32_t i = 0; i < channelCount_; ++i)
        if (chImpl_[i] && channel_active(chImpl_[i])) return true;
    return false;
}

// ---------------------------------------------------------------------------------------------------------
// monitor
// ---------------------------------------------------------------------------------------------------------
DWORD WINAPI EndpointImpl::monitor_main(void* p) {
    ((EndpointImpl*)p)->monitor_loop();
    return 0;
}

void EndpointImpl::monitor_loop() {
    for (;;) {
        HANDLE hs[2];
        DWORD n = 0;
        hs[n++] = stopEvent_;
        HANDLE peer = peerProcess_;
        const uint32_t s = ld(&conn_->connState);
        bool watch = peer && (side_ == Side::Client ? s == kConnConnected : !diedReported_);
        if (watch) hs[n++] = peer;
        DWORD to = cfg_.wait.tickMs;
        if (side_ == Side::Client && s != kConnConnected && to > 10) to = 10;
        if (fastPoll_) to = 1;
        DWORD r = WaitForMultipleObjects(n, hs, FALSE, to);
        if (r == WAIT_OBJECT_0) break;
        if (r == WAIT_FAILED) Sleep(1);
        if (closed_) continue;
        heartbeat();
        if (side_ == Side::Client) client_tick();
        else server_tick();
    }
}

void EndpointImpl::heartbeat() {
    if (!mine_) return;
    st64(&mine_->heartbeatQpc, qpc_now());
    st64(&mine_->heartbeatCount, ++heartbeatCount_);
}

void EndpointImpl::check_hung() {
    const uint64_t c = ld64(&peer_->heartbeatCount);
    const uint64_t now = qpc_now();
    if (c != peerBeatCount_) {
        peerBeatCount_ = c;
        peerBeatSeenQpc_ = now;
        if (peerHung_) {
            InterlockedExchange(&peerHung_, 0);
            logf(LogLevel::Warn, "peer pid %u recovered (heartbeat resumed)", (unsigned)peerPid_);
            fire(PeerEvent::Recovered, generation());
        }
        return;
    }
    if (!peerHung_ && (now - peerBeatSeenQpc_) * 1000u / qpc_frequency() > cfg_.wait.hungAfterMs) {
        InterlockedExchange(&peerHung_, 1);
        logf(LogLevel::Warn, "peer pid %u hung: no heartbeat for %u ms", (unsigned)peerPid_,
             (unsigned)((now - peerBeatSeenQpc_) * 1000u / qpc_frequency()));
        fire(PeerEvent::Hung, generation());
    }
}

void EndpointImpl::retire_peer_process() {
    HANDLE old = peerProcess_;
    peerProcess_ = nullptr;
    if (!old) return;
    if (oldPeerCount_ == kOldPeerHandles) {  // 64 restarts: no wait can still hold the oldest
        CloseHandle(oldPeers_[0]);
        memmove(&oldPeers_[0], &oldPeers_[1], sizeof(HANDLE) * (kOldPeerHandles - 1));
        --oldPeerCount_;
    }
    oldPeers_[oldPeerCount_++] = old;
}

void EndpointImpl::client_reset_to_no_server(const char* why) {
    if (acceptGen_) {  // an attach between its two phases: the generation it published never connected
        st(&conn_->generation, acceptGen_ - 1);
        acceptGen_ = 0;
    }
    retire_peer_process();
    peerPid_ = 0;
    InterlockedExchange(&peerHung_, 0);
    st(&peer_->pid, 0);
    st(&peer_->state, kSideAbsent);
    peer_->refuseReason = kRefuseNone;
    mine_->refuseReason = kRefuseNone;
    if (pending_) CloseHandle(pending_);
    pending_ = nullptr;
    pendingPid_ = 0;
    st(&conn_->attachPid, 0);
    st(&conn_->connState, kConnNoServer);
    logf(LogLevel::Info, "section %s back to NO_SERVER (%s)", sectionName_, why);
}

void EndpointImpl::client_tick() {
    fastPoll_ = false;
    const uint32_t s = ld(&conn_->connState);
    switch (s) {
    case kConnNoServer: {
        if (acceptGen_) {  // the requesting server withdrew between the two accept phases
            st(&conn_->generation, acceptGen_ - 1);
            acceptGen_ = 0;
            if (pending_) CloseHandle(pending_);
            pending_ = nullptr;
            pendingPid_ = 0;
            logf(LogLevel::Warn, "the requesting server withdrew before the attach completed");
        }
        const uint32_t claim = ld(&conn_->attachPid);
        if (claim && !process_alive(claim) && cas(&conn_->attachPid, claim, 0))
            logf(LogLevel::Warn, "cleared the attach claim of exited pid %u", (unsigned)claim);
        break;
    }
    case kConnAttachRequested: client_try_accept(); break;
    case kConnConnected: {
        HANDLE p = peerProcess_;
        if (p && WaitForSingleObject(p, 0) == WAIT_OBJECT_0) {
            if (cas(&conn_->connState, kConnConnected, kConnPeerDead)) {
                DWORD code = 0;
                GetExitCodeProcess(p, &code);
                const uint32_t gen = ld(&conn_->generation);
                logf(LogLevel::Warn, "server pid %u exited (code %u), generation %u", (unsigned)peerPid_,
                     (unsigned)code, gen);
                fire(PeerEvent::Died, gen);
                if (!any_active()) client_reset_to_no_server("server died");
                else fastPoll_ = true;
            }
        } else {
            check_hung();
        }
        break;
    }
    case kConnPeerDead:
        if (!any_active()) client_reset_to_no_server("server died");
        else fastPoll_ = true;
        break;
    case kConnClosing:
        if (closed_) break;  // our own close
        if (!closedReported_) {
            closedReported_ = true;
            logf(LogLevel::Info, "server pid %u closed the connection", (unsigned)peerPid_);
            fire(PeerEvent::Closed, ld(&conn_->generation));
        }
        if (!any_active()) client_reset_to_no_server("server closed");
        else fastPoll_ = true;
        break;
    case kConnRefused: client_handle_refused(); break;
    default:
        logf(LogLevel::Error, "unknown connection state %u", s);
        client_reset_to_no_server("unknown state");
        break;
    }
}

void EndpointImpl::client_try_accept() {
    const uint32_t claim = ld(&conn_->attachPid);
    if (!pending_) {
        const uint32_t spid = ld(&peer_->pid);
        if (!claim || spid != claim) {
            logf(LogLevel::Warn, "attach request with inconsistent pids (claim %u, server line %u)", (unsigned)claim,
                 (unsigned)spid);
            client_reset_to_no_server("inconsistent attach request");
            return;
        }
        pending_ = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, claim);
        if (!pending_) {
            logf(LogLevel::Warn, "cannot open requesting server pid %u (error %u)", (unsigned)claim,
                 (unsigned)GetLastError());
            client_reset_to_no_server("requesting server gone");
            return;
        }
        pendingPid_ = claim;
        const RefuseReason why = compare_sides(self_, side_info(*peer_));
        if (why != kRefuseNone) {
            client_refuse(why);
            return;
        }
    }
    if (WaitForSingleObject(pending_, 0) == WAIT_OBJECT_0) {
        logf(LogLevel::Warn, "requesting server pid %u exited before the attach completed", (unsigned)pendingPid_);
        client_reset_to_no_server("requesting server exited");
        return;
    }
    if (!acceptGen_) {
        // Phase 1, only while no client thread is inside a call: reset every block and this side's cursors,
        // publish the new generation, stay ATTACH_REQUESTED (client calls keep failing fast, so nothing can be
        // published into the zeroed blocks yet).
        if (any_active()) {
            fastPoll_ = true;  // a client thread is still inside a call: accept as soon as it leaves
            return;
        }
        const uint32_t gen = ld(&conn_->generation) + 1;
        for (uint32_t i = 0; i < channelCount_; ++i) {
            channel_reset_shared((ChannelBlock*)(base_ + hdr_->channels[i].offset), hdr_->channels[i]);
            channel_reset_local(chImpl_[i]);
            InterlockedExchange(&chGen_[i], (LONG)gen);
        }
        acceptGen_ = gen;
        st(&conn_->generation, gen);
        fastPoll_ = true;
        return;
    }
    // Phase 2: the server has seen the generation and reset its own cursors on the zeroed blocks (Ready).
    // Only now may either side publish: a server reset after CONNECTED could swallow a client's first CALL.
    if (ld(&peer_->state) != kSideReady) {
        fastPoll_ = true;
        return;
    }
    const uint32_t gen = acceptGen_;
    retire_peer_process();
    peerProcess_ = pending_;
    peerPid_ = pendingPid_;
    InterlockedExchange(&peerHung_, 0);
    peerBeatCount_ = ld64(&peer_->heartbeatCount);
    peerBeatSeenQpc_ = qpc_now();
    if (!cas(&conn_->connState, kConnAttachRequested, kConnConnected)) {
        st(&conn_->generation, gen - 1);  // the server withdrew (its timeout) and released its claim
        acceptGen_ = 0;
        retire_peer_process();
        pending_ = nullptr;
        pendingPid_ = 0;
        peerPid_ = 0;
        logf(LogLevel::Warn, "the requesting server withdrew before the attach completed");
        return;
    }
    acceptGen_ = 0;
    pending_ = nullptr;
    pendingPid_ = 0;
    diedReported_ = false;
    closedReported_ = false;
    logf(LogLevel::Info, "server pid %u attached to %s, generation %u", (unsigned)peerPid_, sectionName_, gen);
    fire(PeerEvent::Attached, gen);
}

void EndpointImpl::client_refuse(RefuseReason why) {
    char text[kRefusalTextBytes];
    format_refusal(text, sizeof(text), "client", why, self_, side_info(*peer_));
    memcpy(hdr_->clientRefusal, text, sizeof(text));
    mine_->refuseReason = why;
    refusedAtQpc_ = qpc_now();
    st(&conn_->connState, kConnRefused);
    set_error("%s", text);
    logf(LogLevel::Error, "%s", text);
    fire(PeerEvent::Refused, ld(&conn_->generation));
    fastPoll_ = true;
}

void EndpointImpl::client_handle_refused() {
    if (mine_->refuseReason != kRefuseNone) {  // this client refused: wait for the server's ack
        const bool acked = ld(&peer_->state) == kSideRefused;
        const bool gone = !pending_ || WaitForSingleObject(pending_, 0) == WAIT_OBJECT_0;
        if (acked || gone || ms_since(refusedAtQpc_) > 2000) client_reset_to_no_server("refused an attach");
        else fastPoll_ = true;
        return;
    }
    char text[kRefusalTextBytes];
    copy_section_text(text, sizeof(text), hdr_->serverRefusal, kRefusalTextBytes);
    set_error("%s", text);
    logf(LogLevel::Error, "%s", text);
    fire(PeerEvent::Refused, ld(&conn_->generation));
    client_reset_to_no_server("server refused to attach");
}

void EndpointImpl::server_tick() {
    if (diedReported_ || closedReported_ || lostReported_) return;
    HANDLE p = peerProcess_;
    const uint32_t s = ld(&conn_->connState);
    const uint32_t gen = ld(&conn_->generation);
    if (p && WaitForSingleObject(p, 0) == WAIT_OBJECT_0) {
        diedReported_ = true;
        if (gen == myGeneration_) cas(&conn_->connState, kConnConnected, kConnPeerDead);
        DWORD code = 0;
        GetExitCodeProcess(p, &code);
        logf(LogLevel::Warn, "client pid %u exited (code %u)", (unsigned)peerPid_, (unsigned)code);
        fire(PeerEvent::Died, myGeneration_);
        return;
    }
    if (s == kConnClosing && gen == myGeneration_) {
        closedReported_ = true;
        logf(LogLevel::Info, "client pid %u closed the connection", (unsigned)peerPid_);
        fire(PeerEvent::Closed, myGeneration_);
        return;
    }
    if (s != kConnConnected || gen != myGeneration_) {
        lostReported_ = true;
        logf(LogLevel::Warn, "connection reset by the client (state %u, generation %u, mine %u)", s, gen,
             myGeneration_);
        fire(PeerEvent::Closed, myGeneration_);
        return;
    }
    check_hung();
}

// ---------------------------------------------------------------------------------------------------------
// ep_* hooks (internal.h). nullptr = a permanently connected, never-dying peer, default WaitConfig, stderr log.
// ---------------------------------------------------------------------------------------------------------
HANDLE ep_peer_process(EndpointImpl* ep) { return ep ? ep->peerProcess_ : nullptr; }
uint32_t ep_conn_state(EndpointImpl* ep) { return ep ? ep->conn_state() : (uint32_t)kConnConnected; }
bool ep_peer_hung(EndpointImpl* ep) { return ep ? ep->peerHung_ != 0 : false; }
const WaitConfig& ep_wait_config(EndpointImpl* ep) { return ep ? ep->cfg_.wait : g_defaultWait; }
Side ep_side(EndpointImpl* ep) { return ep ? ep->side_ : Side::Client; }
uint32_t ep_generation(EndpointImpl* ep) { return ep ? ep->generation() : 1u; }
Status ep_enter(EndpointImpl* ep, uint32_t channel, bool initiating) {
    return ep ? ep->enter(channel, initiating) : Status::Ok;
}
void ep_leave(EndpointImpl* ep, uint32_t channel, bool initiating) {
    if (ep) ep->leave(channel, initiating);
}
void ep_log(EndpointImpl* ep, LogLevel level, const char* f, ...) {
    va_list ap;
    va_start(ap, f);
    if (ep) {
        ep->log(level, f, ap);
    } else if (level >= LogLevel::Info) {
        char msg[1536];
        vfmt(msg, sizeof(msg), f, ap);
        fprintf(stderr, "vpipc: %s\n", msg);
    }
    va_end(ap);
}

// ---------------------------------------------------------------------------------------------------------
// vpipc::Endpoint (public API, vpipc.h)
// ---------------------------------------------------------------------------------------------------------
Endpoint* EndpointImpl::make_public(EndpointImpl* impl) {
    if (!impl) return nullptr;
    Endpoint* e = new (std::nothrow) Endpoint(impl);
    impl->owner_ = e;
    if (!e) destroy(impl);
    return e;
}

namespace {
// Shared tail of create_client / attach_server: Ok -> the endpoint; failure -> the text into this thread's
// last_error_text() buffer, the half-built endpoint freed, *out = nullptr.
Status finish_create(Status s, EndpointImpl* impl, Endpoint** out) {
    *out = nullptr;
    TlsBlock* b = tls_block(true);
    if (s == Status::Ok) {
        *out = EndpointImpl::make_public(impl);
        if (*out) {
            if (b) b->lastError[0] = 0;
            return Status::Ok;
        }
        if (b) copy_str(b->lastError, kErrorBytes, "out of memory (Endpoint)");
        return Status::SystemError;
    }
    if (b) {
        if (impl) impl->copy_last_error(b->lastError, kErrorBytes);
        else copy_str(b->lastError, kErrorBytes, "out of memory (EndpointImpl)");
    }
    EndpointImpl::destroy(impl);
    return s;
}
}  // namespace

Status Endpoint::create_client(const EndpointConfig& cfg, Endpoint** out) {
    EpCfg c;
    copy_config(cfg, Side::Client, c);
    EndpointImpl* impl = nullptr;
    Status s = EndpointImpl::create_client_impl(c, &impl);
    return finish_create(s, impl, out);
}

Status Endpoint::attach_server(const EndpointConfig& cfg, Endpoint** out, uint32_t timeoutMs) {
    EpCfg c;
    copy_config(cfg, Side::Server, c);
    EndpointImpl* impl = nullptr;
    Status s = EndpointImpl::attach_server_impl(c, &impl, timeoutMs);
    return finish_create(s, impl, out);
}

const char* Endpoint::last_error_text() {
    TlsBlock* b = tls_block(false);
    return b ? b->lastError : "";
}

Endpoint::~Endpoint() { EndpointImpl::destroy(impl_); }
Status Endpoint::wait_for_server(uint32_t timeoutMs) { return impl_->wait_for_server(timeoutMs); }
void Endpoint::set_resync_hook(ResyncHook hook, void* ctx) { impl_->set_resync_hook(hook, ctx); }
Status Endpoint::close() { return impl_->close(); }
uint32_t Endpoint::channel_count() const { return impl_->channel_count(); }
Channel& Endpoint::channel(uint32_t index) { return impl_->channel(index); }
void Endpoint::bind_current_thread(uint32_t index) { impl_->bind_current_thread(index); }
Channel& Endpoint::current_channel() { return impl_->current_channel(); }
Side Endpoint::side() const { return impl_->side_; }
const char* Endpoint::section_name() const { return impl_->sectionName_; }
uint32_t Endpoint::generation() const { return impl_->generation(); }
uint32_t Endpoint::conn_state() const { return impl_->conn_state(); }
uint32_t Endpoint::peer_pid() const { return impl_->peer_pid(); }
// The buffer is rewritten (under a lock) only when a refusal or failure happens; a reader racing such a rewrite
// may see a mix of the two texts.
const char* Endpoint::last_error() const { return impl_->lastError_; }

}  // namespace vpipc
