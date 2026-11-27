// src/internal.h - seams between the library's modules. Owner: epic 1200 coordinator.
// Implementations: ring -> task 1201 (src/ring.cpp); waiting -> 1202 (src/wait.cpp);
// endpoint hooks -> 1202 (src/endpoint.cpp and friends); channel -> 1203 (src/channel.cpp).
// Changing a declaration here needs the coordinator's agreement (it is the contract between workers).
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "vpipc/vpipc.h"

namespace vpipc {

// ---------------------------------------------------------------------------------------------------------
// Rings (task 1201). Single producer, single consumer, byte records (PROTOCOL.md section 4).
// ---------------------------------------------------------------------------------------------------------
class RingProducer {
public:
    void init(RingIndexLine* write, const RingIndexLine* read, uint8_t* data, uint32_t bytes);
    void reset_local();                  // cursors to 0: only after channel_reset_shared zeroed the block (1207)
    // Copy one record in WITHOUT publishing it (task 1206: one publish per batch). False if the free space is
    // too small: the caller publishes what is pending (publish()), wakes the consumer, waits on
    // read_index_word() and retries. len must be <= max_payload() and method != kRingPadMethod: for those
    // try_write returns false forever, so the caller checks them first (PayloadTooLarge / ProtocolError) and
    // never waits on them. The free space is computed from a cached read index, re-read only when short.
    bool try_write(uint32_t method, const void* payload, uint32_t len);
    // Make every written record visible to the consumer: one release store of the write index, not a full
    // fence (see wake_peer). No-op when nothing is pending.
    void publish();
    uint32_t unpublished() const { return w_ - pub_; }  // bytes written and not yet published
    // try_write + publish (one record, published at once).
    bool try_push(uint32_t method, const void* payload, uint32_t len);
    uint32_t used() const;               // bytes written (published or not) and not yet consumed (incl. padding)
    uint32_t capacity() const { return bytes_; }
    uint32_t max_payload() const { return bytes_ / 4; }
    const volatile uint32_t* read_index_word() const { return &read_->index; }

private:
    RingIndexLine* write_ = nullptr;
    const RingIndexLine* read_ = nullptr;
    uint8_t* data_ = nullptr;
    uint32_t bytes_ = 0;
    uint32_t w_ = 0;                     // local write index: records written
    uint32_t pub_ = 0;                   // write index last published
    uint32_t rCache_ = 0;                // read index last seen
};

class RingConsumer {
public:
    void init(const RingIndexLine* write, RingIndexLine* read, uint8_t* data, uint32_t bytes);
    void reset_local();
    // The next record, skipping pad records. False if empty or corrupt (corrupt() then true).
    // The payload stays valid until pop().
    bool peek(uint32_t& method, const uint8_t*& payload, uint32_t& len);
    void pop();                          // release the record from the last successful peek()
    bool empty() const;
    bool corrupt() const { return corrupt_; }
    const volatile uint32_t* write_index_word() const { return &write_->index; }

private:
    const RingIndexLine* write_ = nullptr;
    RingIndexLine* read_ = nullptr;
    uint8_t* data_ = nullptr;
    uint32_t bytes_ = 0;
    uint32_t r_ = 0;                     // local copy of the read index
    uint32_t pending_ = 0;               // bytes the last peek() covers (record + any skipped pad)
    bool corrupt_ = false;
};

// ---------------------------------------------------------------------------------------------------------
// Waiting (task 1202). Spin, announce sleep, block on {my event, peer process} with a tick (PROTOCOL.md 6).
// ---------------------------------------------------------------------------------------------------------
struct WaitEnv {
    volatile uint32_t* mySleeping = nullptr;    // this side's SideCtlLine::sleeping
    volatile uint32_t* peerSleeping = nullptr;  // the peer's SideCtlLine::sleeping
    HANDLE myEvent = nullptr;                   // auto-reset; this side blocks on it
    HANDLE peerEvent = nullptr;                 // wakes the peer
    EndpointImpl* ep = nullptr;                 // nullptr in single-process unit tests (see ep_* below)
};

enum class WaitResult { Changed, PeerDied, PeerClosed, PeerHung, Timeout };

// Wait until any words[i] != seen[i]. spinUs: spin budget before blocking. timeoutMs: 0xFFFFFFFF = none
// (the WaitConfig callTimeoutMs/failOnHung policy is applied inside). stats may be nullptr.
WaitResult wait_change(WaitEnv& env, const volatile uint32_t* const* words, const uint32_t* seen, uint32_t n,
                       uint32_t spinUs, uint32_t timeoutMs, ChannelStats* stats);

// Store with a full fence (InterlockedExchange), then wake the peer if it announced sleep.
void publish(WaitEnv& env, volatile uint32_t* word, uint32_t value, ChannelStats* stats);
// Wake the peer if it announced sleep (no store). Returns true if an event was set.
// Contract (task 1201's finding, 2026-11-10): wake_peer issues a full fence (MemoryBarrier or an interlocked
// operation) before reading the peer's sleeping flag, because a ring push publishes with a release store only;
// without the fence a store-load reordering can lose the wake-up of a peer that is just going to sleep.
bool wake_peer(WaitEnv& env, ChannelStats* stats);

uint64_t qpc_now();
uint64_t qpc_frequency();

// ---------------------------------------------------------------------------------------------------------
// Endpoint hooks used by channels and waits (task 1202, src/endpoint.cpp).
// Every ep_* accepts ep == nullptr and then behaves as a permanently connected, never-dying peer with a
// default WaitConfig and logging to stderr, so channel unit tests can run without an endpoint.
// ---------------------------------------------------------------------------------------------------------
HANDLE ep_peer_process(EndpointImpl* ep);        // nullptr while no peer is attached
uint32_t ep_conn_state(EndpointImpl* ep);        // ConnState
bool ep_peer_hung(EndpointImpl* ep);             // monitor's current verdict
const WaitConfig& ep_wait_config(EndpointImpl* ep);
Side ep_side(EndpointImpl* ep);
uint32_t ep_generation(EndpointImpl* ep);
// A channel calls ep_enter when it starts a top-level call (initiating side) or starts handling a
// top-level call (serving side), and ep_leave when it is done. On the client this checks the connection,
// runs the resync hook after a generation change, runs channel_reset_local for this channel at its first
// ep_enter after a generation change, and holds off attach while any thread is inside. On a shared channel
// the caller already holds the channel lock. Non-Ok fails the call before anything is sent.
Status ep_enter(EndpointImpl* ep, uint32_t channel, bool initiating);
void ep_leave(EndpointImpl* ep, uint32_t channel, bool initiating);
void ep_log(EndpointImpl* ep, LogLevel level, const char* fmt, ...);

// ---------------------------------------------------------------------------------------------------------
// Channels (task 1203, src/channel.cpp). The endpoint creates one per ChannelDesc.
// ---------------------------------------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4324)  // padded because ChannelDesc is alignas(64): intended
struct ChannelEnv {
    Side side = Side::Client;
    uint32_t index = 0;
    ChannelBlock* block = nullptr;
    ChannelDesc desc = {};                      // copy of the section's descriptor
    uint8_t* data = nullptr;                    // dataBytes
    uint8_t* ringC2S = nullptr;                 // ringBytes
    uint8_t* ringS2C = nullptr;                 // ringBytes
    WaitEnv wait;
    EndpointImpl* ep = nullptr;
};
#pragma warning(pop)

ChannelImpl* channel_create(const ChannelEnv& env);
void channel_destroy(ChannelImpl* ch);
Channel& channel_public(ChannelImpl* ch);
// Zero the shared state of one block (sequence numbers, message lines, ring indices, sleeping flags).
// Called by the client's monitor at attach, while no thread is inside any call.
void channel_reset_shared(ChannelBlock* block, const ChannelDesc& desc);
// Set local cursors (last seen seq, ring positions, call ids, depth) to 0 (bug 1207). Precondition: the block was
// zeroed by channel_reset_shared for this generation and this side has not used the channel since.
void channel_reset_local(ChannelImpl* ch);
// Number of threads currently inside a call on this channel on this side (0 or 1 except while a shared
// channel's lock is contended). The client's monitor reads it to decide when an attach may proceed.
uint32_t channel_active(ChannelImpl* ch);

}  // namespace vpipc
