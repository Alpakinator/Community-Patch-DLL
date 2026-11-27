// src/channel_impl.h - ChannelImpl, the per-channel protocol state (PROTOCOL.md sections 3 and 4).
// Owner: task 1203. Library core rules: C++14, no STL, malloc/free, Interlocked*, SRWLOCK.
#pragma once
#include "internal.h"

namespace vpipc {

// Debug builds copy every received payload into its own pages and decommit them when the validity rule
// (PROTOCOL.md 3.3) expires, so a Reader or pointer used too late faults (access violation) at the read.
#if !defined(NDEBUG) && !defined(VPIPC_NO_VALIDITY_CHECK)
#define VPIPC_VALIDITY_CHECK 1
#else
#define VPIPC_VALIDITY_CHECK 0
#endif

// A received synchronous message, header fields copied out of the peer's MsgLine.
struct RecvMsg {
    uint16_t kind;
    uint16_t flags;
    uint32_t method;
    uint32_t callId;
    uint32_t depth;
    uint32_t status;
    const uint8_t* payload;  // assembled payload (inline, data area, chunk buffer or the Debug copy)
    uint32_t length;         // assembled payload bytes
};

// The call handler currently running on this thread for this channel (set_return_status target).
struct HandlerFrame {
    HandlerFrame* prev;
    uint32_t status;
};

// Writer for a handler's RETURN, one per depth: never in the data area, because the handler's arguments may
// still live there and the handler may make nested calls between writes.
struct RetSlot {
    Writer w;
    uint8_t buf[256];
};

class ChannelImpl {
public:
    explicit ChannelImpl(const ChannelEnv& env);
    ~ChannelImpl();

    Writer& begin_call(uint32_t method);
    Status end_call(Reader& result);
    Writer& begin_post(uint32_t method);
    Status end_post();
    Status flush();
    void set_handlers(CallHandler onCall, PostHandler onPost, void* ctx);
    void set_return_status(uint32_t status);
    Status serve(uint32_t timeoutMs);
    Status poll();

    uint32_t index() const { return index_; }
    Side initiator() const { return initiatorSide_; }
    Side side() const { return side_; }
    ChannelStats stats() const { return stats_; }
    uint32_t last_remote_status() const { return lastRemoteStatus_; }

    void reset_local();
    uint32_t active() const { return (uint32_t)active_; }

    Channel pub_;

private:
    ChannelImpl(const ChannelImpl&);
    ChannelImpl& operator=(const ChannelImpl&);

    // top-level region: shared-channel lock, active count, ep_enter / ep_leave
    bool in_top() const;
    void lock();
    void unlock();
    Status enter_top(bool initiating, bool& entered);
    void leave_top(bool initiating, bool entered);

    // sending
    Status send(uint16_t kind, uint32_t method, uint32_t callId, uint32_t depth, uint32_t status,
                const uint8_t* p, uint32_t len);
    void send_piece(uint16_t kind, uint16_t flags, uint32_t method, uint32_t callId, uint32_t depth,
                    uint32_t status, const uint8_t* p, uint32_t len, uint32_t total);
    Status do_call(uint32_t method, Reader& result);
    void keep_result_for_thread(Reader& result);
    Status push_post(uint32_t method, const uint8_t* p, uint32_t len);
    void publish_posts(uint64_t now);  // publish pending records, wake a sleeping consumer
    void ring_publish();               // publish pending records only

    // receiving
    Status try_receive(RecvMsg& m, bool& got, bool acceptChunks);
    Status next_message(RecvMsg& m, uint32_t spinUs, uint32_t timeoutMs, bool acceptChunks);
    Status drain_posts();
    Status wait_return(uint32_t callId, uint32_t depth, Reader& result);
    Status handle_call(const RecvMsg& m);
    Writer& ret_writer(uint32_t depth);

    Status broken(const char* fmt, ...);
    Status fail_wait(Status s);
    void debug_assert_fail(const char* what);
    void set_depth(uint32_t d);
    Writer& dummy();

    // Debug validity check
    const uint8_t* dbg_hold(const uint8_t* p, uint32_t len);
    void dbg_expire();

    // identity and shared memory
    Side side_;
    Side initiatorSide_;
    uint32_t index_;
    bool initiator_;   // this side is the channel's initiator
    bool sharedLock_;  // client side of a shared channel
    DWORD fls_;        // shared channel: per-thread RETURN copy (keep_result_for_thread)
    ChannelBlock* block_;
    MsgLine* out_;
    const MsgLine* in_;
    SideCtlLine* myCtl_;
    uint8_t* data_;
    uint32_t dataBytes_;
    uint32_t ringBytes_;
    WaitEnv wait_;
    EndpointImpl* ep_;
    const WaitConfig* wc_;  // the endpoint's WaitConfig (fixed at creation); read without a call (task 1209)
    RingProducer prod_;
    RingConsumer cons_;
    uint64_t flushTicks_;
    uint64_t lastPublishQpc_;  // last post-time publish (flush interval rule)
    uint32_t unpubRecords_;    // records written since the last publish

    // protocol state (owned by the thread inside the channel)
    uint32_t mySeq_;
    uint32_t lastInSeq_;
    uint32_t nextCallId_;
    uint32_t stack_;      // calls outstanding on the channel, both directions (= depth of the next CALL)
    bool turn_;           // this side may send
    bool broken_;
    Status brokenStatus_;  // what broke it: ProtocolError, or the status of a failed wait inside a call
    uint32_t inPost_;     // post handlers running on this side
    HandlerFrame* frame_;
    uint32_t lastRemoteStatus_;

    // building a call / post
    Writer callW_;
    Writer postW_;
    Writer dummyW_;
    uint8_t dummyBuf_[64];
    uint8_t* postScratch_;
    bool callOpen_;
    bool callOpened_;   // this begin_call opened the top-level region
    bool callEntered_;  // ... and ep_enter succeeded
    Status callPending_;
    uint32_t callMethod_;
    bool postOpen_;
    bool postOpened_;
    bool postEntered_;
    Status postPending_;
    uint32_t postMethod_;

    // RETURN writers per depth
    RetSlot** ret_;
    uint32_t retCount_;

    // chunk assembly
    uint8_t* asm_;
    uint32_t asmCap_;
    uint32_t asmLen_;
    uint32_t asmTotal_;
    bool asmActive_;

    // top-level region
    SRWLOCK lock_;
    volatile DWORD owner_;
    uint32_t lockRec_;
    uint32_t top_;
    volatile LONG active_;

    // handlers
    CallHandler onCall_;
    PostHandler onPost_;
    void* ctx_;

    ChannelStats stats_;

    // Debug validity check: the live received payload copy and a FIFO of decommitted ones
    struct DbgRegion {
        void* base;
        uint32_t bytes;
    };
    enum { kDbgQuarantine = 32 };
    DbgRegion dbgLive_;
    DbgRegion dbgQ_[kDbgQuarantine];
    uint32_t dbgQHead_;
    uint64_t dbgQBytes_;
};

}  // namespace vpipc
