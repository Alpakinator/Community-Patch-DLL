// src/endpoint_impl.h - the endpoint: section, kernel objects, attach state machine, monitor thread,
// call admission (ep_enter / ep_leave) and thread binding. Task 1202. PROTOCOL.md sections 2, 6 and 7.
// Library-core rules (PROTOCOL.md section 11): C++11, no STL, no thread_local, no function-local statics with
// dynamic initialisers, Interlocked* atomics, CreateThread, malloc/free, Windows 7 API level.
#pragma once
#include <intrin.h>
#include <stdarg.h>

#include <new>

#include "internal.h"

namespace vpipc {

// Internal copy of EndpointConfig in fixed-size, STL-free form (the only code that reads the public config
// struct is copy_config() in endpoint.cpp).
struct ChanCfg {
    char name[kChannelNameBytes];
    uint32_t initiator;  // ChannelInitiator
    uint32_t flags;      // ChannelFlags
    uint32_t dataBytes;
    uint32_t ringBytes;
};

constexpr uint32_t kErrorBytes = 2048;
constexpr uint32_t kOldPeerHandles = 64;

struct EpCfg {
    char sectionName[kSectionNameBytes];
    char tag[kTagBytes];
    uint64_t appSchemaHash;
    uint32_t channelCount;
    ChanCfg ch[kMaxChannels];
    WaitConfig wait;
    LogHook onLog;
    PeerEventHook onPeerEvent;
    void* hookCtx;
    TestOverrides test;
    char badConfig[256];  // non-empty: copy_config found the public config invalid (text says why)
};

struct EpTlsSlot;  // per-thread, per-endpoint state (endpoint.cpp)

// What one side claims to be (for the version check and the refusal text).
struct SideInfo {
    uint16_t versionMajor;
    uint16_t versionMinor;
    uint32_t layoutHash;
    uint64_t appSchemaHash;
    uint32_t pid;
    uint32_t pointerBits;
};

class EndpointImpl {
public:
    // ---- construction (endpoint.cpp) ----
    static Status create_client_impl(const EpCfg& cfg, EndpointImpl** out);
    static Status attach_server_impl(const EpCfg& cfg, EndpointImpl** out, uint32_t timeoutMs);
    // Tests only: no section, no monitor; permanently CONNECTED (generation 1) to `peerProcess`, which the
    // caller keeps owning. Used by test_wait to exercise wait_change against a real process handle.
    static EndpointImpl* create_detached_for_test(Side side, HANDLE peerProcess, const WaitConfig& wait);
    static void destroy(EndpointImpl* ep);  // close() if needed, stop the monitor, free everything
    static EndpointImpl* construct_(void* mem);    // placement-constructs into malloc'd memory
    static Endpoint* make_public(EndpointImpl* impl);  // the vpipc::Endpoint that owns impl (nullptr: impl freed)

    // ---- public API behind vpipc::Endpoint ----
    Status close();
    Status wait_for_server(uint32_t timeoutMs);
    void set_resync_hook(ResyncHook hook, void* ctx);
    uint32_t channel_count() const { return channelCount_; }
    Channel& channel(uint32_t index);
    void bind_current_thread(uint32_t index);
    Channel& current_channel();
    uint32_t current_channel_index();
    uint32_t generation() const;
    uint32_t conn_state() const { return conn_->connState; }
    uint32_t peer_pid() const { return peerPid_; }
    void copy_last_error(char* out, uint32_t cap) const;

    // ---- hooks for channels and waits (internal.h) ----
    Status enter(uint32_t channel, bool initiating);
    void leave(uint32_t channel, bool initiating);
    // While a blocked wait ticks: true and *out set when the wait must end (peer gone, closed, hung + failOnHung).
    bool wait_verdict(uint32_t generationAtStart, WaitResult* out) const;
    void log(LogLevel level, const char* fmt, va_list ap) const;
    void logf(LogLevel level, const char* fmt, ...) const;
    void set_error(const char* fmt, ...);

    // ---- state (public: this class is internal to the library) ----
    Side side_ = Side::Client;
    EpCfg cfg_;
    uint64_t id_ = 0;  // unique per process; key of the thread-local binding
    Endpoint* owner_ = nullptr;
    bool detached_ = false;

    HANDLE mapping_ = nullptr;
    uint8_t* base_ = nullptr;
    uint32_t totalBytes_ = 0;
    Header* hdr_ = nullptr;
    ConnLine* conn_ = nullptr;  // in the section, or detachedConn_
    void* detachedMem_ = nullptr;  // detached test endpoint: holds its ConnLine
    SideLine* mine_ = nullptr;
    SideLine* peer_ = nullptr;
    SideInfo self_ = {};  // what this side claims (TestOverrides applied)

    uint32_t channelCount_ = 0;
    HANDLE events_[2 * kMaxChannels] = {};  // [2i] = <section>.w<i>.c, [2i+1] = <section>.w<i>.s
    ChannelImpl* chImpl_[kMaxChannels] = {};
    Channel* chPublic_ = nullptr;  // chPublicCount_ handles, malloc + placement new
    uint32_t chPublicCount_ = 0;
    volatile LONG chGen_[kMaxChannels] = {};  // generation each channel was last reset_local for

    volatile HANDLE peerProcess_ = nullptr;  // nullptr while no peer is attached
    bool ownPeerProcess_ = true;
    HANDLE oldPeers_[kOldPeerHandles] = {};  // replaced peer handles; closed at destroy (a wait may hold one)
    uint32_t oldPeerCount_ = 0;
    volatile uint32_t peerPid_ = 0;
    volatile LONG peerHung_ = 0;
    volatile LONG inside_ = 0;  // threads between a successful slow-path enter() and its leave()
    // Per non-shared channel: the current top-level region was admitted by the fast path (task 1209). Touched only
    // by the one thread that uses the channel on this side.
    uint8_t fastIn_[kMaxChannels] = {};
    volatile LONG closed_ = 0;  // this side called close()
    uint32_t myGeneration_ = 0;  // server: the generation it attached as

    // resync (client)
    SRWLOCK resyncLock_ = SRWLOCK_INIT;
    CONDITION_VARIABLE resyncCv_ = CONDITION_VARIABLE_INIT;
    ResyncHook resyncHook_ = nullptr;
    void* resyncCtx_ = nullptr;
    volatile uint32_t resyncedGen_ = 0;
    bool resyncRunning_ = false;

    // monitor
    HANDLE monitorThread_ = nullptr;
    DWORD monitorTid_ = 0;
    HANDLE stopEvent_ = nullptr;
    // monitor-thread-only state
    uint64_t peerBeatCount_ = 0;
    uint64_t peerBeatSeenQpc_ = 0;
    uint64_t heartbeatCount_ = 0;
    bool diedReported_ = false;
    bool closedReported_ = false;
    bool lostReported_ = false;
    HANDLE pending_ = nullptr;  // client: the requesting server's process during attach / refusal
    uint32_t pendingPid_ = 0;
    uint64_t refusedAtQpc_ = 0;
    bool fastPoll_ = false;
    uint32_t acceptGen_ = 0;  // client: generation published to a requesting server, CONNECTED once it is Ready

    mutable SRWLOCK errLock_ = SRWLOCK_INIT;
    char lastError_[kErrorBytes] = {};
    char sectionName_[kSectionNameBytes] = {};

    // Fast admission (task 1209) for a channel that is not shared: one thread per side, and the channel's own
    // active count (ChannelImpl::active_, a plain store made by enter_top before this call) is what the monitor's
    // any_active() sees. No TLS lookup and no locked operation: the monitor issues FlushProcessWriteBuffers() before
    // any_active(), so either it sees this thread's active count, or this thread's loads below see the state the
    // monitor wrote before it (the asymmetric form of the Dekker pairing that inside_'s locked increment provides on
    // the slow path). Taken only in the steady state: connected, same generation, resync done and this channel
    // already reset for it; anything else goes through enter() (TLS, inside_, resync hook, channel_reset_local).
    // Inline so a channel calls it without a cross-file call. Measured 2026-11-10: the slow path's TLS lookups and
    // two locked operations cost 54 ns per empty round trip.
    bool enter_fast(uint32_t ch) {
        if (detached_ || ch >= channelCount_ || !hdr_ || (hdr_->channels[ch].flags & kChannelShared)) return false;
        _ReadWriteBarrier();
        if (closed_) return false;
        const uint32_t s = conn_->connState;
        const uint32_t gen = conn_->generation;
        _ReadWriteBarrier();
        if (s != kConnConnected) return false;
        if (side_ == Side::Server) {
            if (gen != myGeneration_) return false;
        } else if (resyncedGen_ != gen || (uint32_t)chGen_[ch] != gen) {
            return false;
        }
        fastIn_[ch] = 1;
        return true;
    }
    // The leave of a region enter_fast admitted (true), else false (the caller uses leave()).
    bool leave_fast(uint32_t ch) {
        if (ch >= kMaxChannels || !fastIn_[ch]) return false;
        fastIn_[ch] = 0;
        return true;
    }

private:
    friend class Endpoint;
    EndpointImpl() {}
    ~EndpointImpl() {}
    static DWORD WINAPI monitor_main(void* p);
    void monitor_loop();
    void heartbeat();
    void client_tick();
    void server_tick();
    void client_try_accept();
    void client_refuse(RefuseReason reason);
    void client_handle_refused();
    void client_reset_to_no_server(const char* why);
    void check_hung();
    void fire(PeerEvent ev, uint32_t generation);
    bool any_active() const;
    void retire_peer_process();
    bool open_events(bool create);
    bool create_channels();
    bool start_monitor();
    void release_resources();
    void stop_monitor();
    Status admit(uint32_t channel, bool initiating, EpTlsSlot* t);
    void run_resync(uint32_t generation, EpTlsSlot* t);
};

// Builds the one-line refusal text naming both sides.
void format_refusal(char* out, uint32_t cap, const char* refuser, RefuseReason reason, const SideInfo& client,
                    const SideInfo& server);
const char* refuse_reason_name(RefuseReason r);

}  // namespace vpipc
