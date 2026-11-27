// vpipc/vpipc.h - public API of the shared-memory transport (ipc/PROTOCOL.md).
// Declarations by the epic 1200 coordinator. Implementation: Endpoint -> task 1202 (src/endpoint.cpp),
// Channel -> task 1203 (src/channel.cpp). Signature changes need the coordinator's agreement.
// Core rule (PROTOCOL.md section 11): C++11 language, no C++ standard library types; builds under VC9 and MSVC 14.
#pragma once
#include <stdint.h>

#include "vpipc/layout.h"
#include "vpipc/marshal.h"

namespace vpipc {

enum class Status : uint32_t {
    Ok = 0,
    RemoteError,      // the peer's handler returned a non-zero ReturnStatus; see Channel::last_remote_status()
    PeerDied,         // the peer process exited during the wait
    PeerClosed,       // the peer closed the endpoint in order
    PeerHung,         // heartbeat stale and failOnHung set
    Timeout,          // callTimeoutMs or the given timeout elapsed
    Disconnected,     // no server attached (client) / not connected yet
    VersionMismatch,  // refused: protocol version differs
    LayoutMismatch,   // refused: layout hash differs
    SchemaMismatch,   // refused: app schema hash differs
    ProtocolError,    // turn-order violation, callId mismatch, depth limit, sync call from a post handler, ...
    PayloadTooLarge,  // a post larger than ringBytes / 4
    SystemError,      // a Win32 call failed; last_error() has GetLastError and the call
    InvalidArgument,  // bad configuration (sizes, counts, names) or API misuse
};
const char* status_name(Status s);

enum class Side : uint8_t { Client = 0, Server = 1 };

enum class LogLevel : uint8_t { Debug = 0, Info = 1, Warn = 2, Error = 3 };

enum class PeerEvent : uint8_t {
    Attached,   // client: a server attached (generation in the hook argument); server: connected
    Died,       // peer process exited
    Closed,     // peer closed in order
    Hung,       // peer heartbeat older than hungAfterMs
    Recovered,  // heartbeat resumed after Hung
    Refused,    // an attach was refused (text in Endpoint::last_error())
};

struct WaitConfig {
    uint32_t replySpinUs = 100;       // measured, task 1205 (PROTOCOL.md 6): 50-1000 all cost one wake; 100 covers its p99
    uint32_t idleSpinUs = 50;         // measured, task 1205: covers 10 us gaps; 200 needed for 100 us gaps costs 4x the spin
    uint32_t tickMs = 100;            // monitor and blocked-wait tick
    uint32_t hungAfterMs = 5000;
    uint32_t callTimeoutMs = 0;       // 0 = wait as long as the peer is alive (but see clientCallTimeoutMs)
    // Bug 0113 (2026-11-13): on the client side (the proxy in the EXE) a callTimeoutMs of 0 is replaced by this bound,
    // so a server that hangs but stays alive cannot hold the EXE forever. It bounds each wait for the server's next
    // message within a call (a nested call, a chunk or the RETURN), not the whole call: a long AI turn keeps calling
    // back into the engine and is never cut. 600 s is 18x a whole huge-save AI turn through the proxy (26.7-33.4 s)
    // and 3.6x the whole launch with the huge save's load (2 min 45 s), each an upper bound for any single gap inside
    // it (research/03-dll-engine-boundary/proxy-design.md 18.9).
    // 0 = the old behaviour (no bound). The server side ignores it: the EXE may legitimately sit in a dialog.
    uint32_t clientCallTimeoutMs = 600000;
    bool failOnHung = false;
    uint32_t flushIntervalUs = 1000;  // measured, task 1205: posts' delivery delay ~0.57x median, ~1.05x p99 of this
    uint32_t maxDepth = 64;
};

struct ChannelConfig {
    char name[kChannelNameBytes] = {};  // <= 15 chars + NUL, stored in the section
    Side initiator = Side::Client;
    bool shared = false;                // several client threads, serialised per top-level call
    uint32_t dataBytes = 256 * 1024;    // multiple of 64
    uint32_t ringBytes = 64 * 1024;     // power of two, >= 4096
};

constexpr uint32_t kChannelUi = 0;
constexpr uint32_t kChannelCore = 1;
constexpr uint32_t kChannelAux = 2;
constexpr uint32_t kSectionNameBytes = 128;
constexpr uint32_t kTagBytes = 32;

using LogHook = void (*)(void* ctx, LogLevel level, const char* message);
using PeerEventHook = void (*)(void* ctx, PeerEvent ev, uint32_t generation);

class Endpoint;
class Channel;

// Called on the first client thread that starts a top-level call after a (re)attach, before that call.
using ResyncHook = void (*)(void* ctx, Endpoint& ep, uint32_t generation);

// Handlers run on the thread that services the channel. A call handler writes its result into `ret`;
// to fail, call Channel::set_return_status() and write a UTF-8 message.
using CallHandler = void (*)(void* ctx, Channel& ch, uint32_t method, Reader& args, Writer& ret);
using PostHandler = void (*)(void* ctx, Channel& ch, uint32_t method, Reader& args);

// Tests only: make this endpoint claim to be a different build, to exercise the refusal paths.
struct TestOverrides {
    bool enabled = false;
    uint16_t versionMajor = kVersionMajor;
    uint16_t versionMinor = kVersionMinor;
    uint32_t layoutHash = kLayoutHash;
};

// Fills the three default channels (ui, core, aux: PROTOCOL.md section 2.2) into cfg-style arrays.
void default_channels(ChannelConfig* channels, uint32_t* count);

struct EndpointConfig {
    char sectionName[kSectionNameBytes] = {};  // client: "" -> "Local\\VPIpc.<pid>.<tag>"; server: required
    char tag[kTagBytes] = {'m', 'a', 'i', 'n', 0};
    uint64_t appSchemaHash = 0;                // 0 on both sides = not checked
    ChannelConfig channels[kMaxChannels];      // client only; the server reads the section
    uint32_t channelCount = 0;
    WaitConfig wait;
    LogHook onLog = nullptr;
    PeerEventHook onPeerEvent = nullptr;
    void* hookCtx = nullptr;
    TestOverrides test;

    EndpointConfig() { default_channels(channels, &channelCount); }
};

struct ChannelStats {
    uint64_t callsOut = 0, callsIn = 0, postsOut = 0, postsIn = 0;
    uint64_t chunksOut = 0, chunksIn = 0;
    uint64_t spinWaits = 0, blockedWaits = 0, wakesSent = 0;
    uint32_t maxDepth = 0;
};

class ChannelImpl;
class EndpointImpl;

class Channel {
public:
    // Synchronous call. begin_call returns a writer for the arguments; end_call publishes, waits, services
    // nested calls and posts, and on Ok leaves `result` reading the RETURN payload, valid until this side's
    // next send on this channel (PROTOCOL.md 3.3). Works at any depth: inside a handler it is a nested call.
    // Returns Timeout when callTimeoutMs expires (or PeerDied / PeerClosed / PeerHung). After such a failure the
    // channel stays broken until the next attach: a late RETURN would arrive out of turn, so every later call on
    // it returns the same status at once (bug 1207, 2026-11-10).
    Writer& begin_call(uint32_t method);
    Status end_call(Reader& result);
    uint32_t last_remote_status() const;  // ReturnStatus of the last RETURN

    // Fire-and-forget. Delivered in order with calls on this channel (PROTOCOL.md section 4).
    Writer& begin_post(uint32_t method);
    Status end_post();
    Status flush();  // wake the consumer now if it sleeps and records are pending

    // Serving side.
    void set_handlers(CallHandler onCall, PostHandler onPost, void* ctx);
    void set_return_status(uint32_t status);  // inside a call handler
    Status serve(uint32_t timeoutMs);         // wait for and handle one top-level CALL (posts are handled on the
                                              // way but do not end it); the idle wait never uses callTimeoutMs
    Status poll();                            // handle what is pending without waiting

    uint32_t index() const;
    Side initiator() const;
    Side side() const;
    ChannelStats stats() const;

    explicit Channel(ChannelImpl* impl) : impl_(impl) {}
    ChannelImpl* impl() const { return impl_; }

private:
    ChannelImpl* impl_;
};

class Endpoint {
public:
    // Client: create the section and events, start the monitor. Returns immediately; no server needed.
    // On Ok *out is a new Endpoint the caller deletes. On failure *out is nullptr.
    static Status create_client(const EndpointConfig& cfg, Endpoint** out);
    // Server: open the section (retrying up to timeoutMs), check version/layout/schema, attach, wait for
    // CONNECTED. On refusal returns the mismatch status, *out is nullptr and last_error_text() has the text.
    static Status attach_server(const EndpointConfig& cfg, Endpoint** out, uint32_t timeoutMs);
    // Text of the last failure of create_client / attach_server on this thread (refusal text included).
    static const char* last_error_text();

    ~Endpoint();

    Status wait_for_server(uint32_t timeoutMs);  // client: until CONNECTED
    void set_resync_hook(ResyncHook hook, void* ctx);
    Status close();                              // orderly; peer sees PeerClosed

    uint32_t channel_count() const;
    Channel& channel(uint32_t index);
    void bind_current_thread(uint32_t index);    // per thread (TLS); unbound threads use the first shared channel
    Channel& current_channel();

    Side side() const;
    const char* section_name() const;
    uint32_t generation() const;
    uint32_t conn_state() const;                 // ConnState
    uint32_t peer_pid() const;
    const char* last_error() const;              // latest refusal or failure text on this endpoint

    EndpointImpl* impl() const { return impl_; }

private:
    explicit Endpoint(EndpointImpl* impl) : impl_(impl) {}
    Endpoint(const Endpoint&);                   // not copyable (C++03 style: VC9-safe)
    Endpoint& operator=(const Endpoint&);
    friend class EndpointImpl;
    EndpointImpl* impl_;
};

}  // namespace vpipc
