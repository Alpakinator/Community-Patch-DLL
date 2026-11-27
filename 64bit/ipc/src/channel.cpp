// src/channel.cpp - vpipc::Channel: synchronous calls, nesting, chunking, posts (PROTOCOL.md 3 and 4).
// Owner: task 1203. Library core rules: C++14, no STL, malloc/free, Interlocked*, SRWLOCK, no exceptions.
//
// Summary of the implementation choices (details in tracker/issues/1203-ipc-channel.md):
// - stack_ counts the calls outstanding on the channel in both directions. Both sides agree on it because
//   of the ping-pong rule, so a CALL carries depth = stack_ and the receiver checks depth == its stack_.
//   A RETURN carries the depth of the CALL it answers.
// - Call arguments are written straight into the data area (zero copy when 32 < size <= dataBytes).
//   A handler's RETURN is written into a per-depth slot and copied at send, because the handler's
//   arguments may still be in the data area and the handler may make nested calls while writing.
// - Posts are copied out of the ring and popped before their handler runs, so draining is re-entrant
//   (a post handler may post into a full ring, whose wait drains the incoming ring again).
// - Top-level operations (a call or post outside any call on this thread, a top-level CALL being served)
//   run inside a region: the shared channel's SRWLOCK (client side of a shared channel), the active count,
//   and ep_enter / ep_leave. Nested operations on the owning thread do not re-enter.
#include "channel_impl.h"
#include "endpoint_impl.h"
#include "wait_fast.h"

// The member functions every call runs are forced inline into their callers in this file (task 1209: -18 ns per
// empty round trip pinned, 2026-11-10; the compiler's own heuristics left send/next_message/try_receive/handle_call
// out of line). Build with -DVPIPC_FORCEINLINE=0 to compare.
#ifndef VPIPC_FORCEINLINE
#define VPIPC_FORCEINLINE 1
#endif
#if VPIPC_FORCEINLINE
#define VPIPC_HOT __forceinline
#else
#define VPIPC_HOT
#endif

#include <intrin.h>
#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace vpipc {

namespace {

const uint32_t kInfinite = 0xFFFFFFFFu;
const uint32_t kTrimBytes = 4u << 20;
const uint32_t kPublishFraction = 8;  // posts: publish when the unpublished bytes reach ringBytes / 8  // heap buffers above this are released after use

// Copy of a small payload without a CRT call (msvcr90's memcpy costs ~30 ns for 64 bytes in the x86 tree).
inline void copy_small(uint8_t* d, const uint8_t* s, uint32_t n) {
    uint32_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t v;
        memcpy(&v, s + i, 8);  // constant size: inlined
        memcpy(d + i, &v, 8);
    }
    for (; i < n; ++i) d[i] = s[i];
}

// Release a Writer's large spill buffer (reset() keeps it for reuse otherwise).
void trim_writer(Writer& w) {
    if (w.spilled() && w.size() > kTrimBytes) {
        w.~Writer();
        new (&w) Writer();
    }
}

void WINAPI free_thread_result(void* v);

Status map_wait(WaitResult r) {
    switch (r) {
    case WaitResult::Changed: return Status::Ok;
    case WaitResult::PeerDied: return Status::PeerDied;
    case WaitResult::PeerClosed: return Status::PeerClosed;
    case WaitResult::PeerHung: return Status::PeerHung;
    case WaitResult::Timeout: return Status::Timeout;
    }
    return Status::ProtocolError;
}

uint32_t remaining_ms(uint32_t timeoutMs, uint64_t startQpc) {
    if (timeoutMs == kInfinite) return kInfinite;
    uint64_t elapsedMs = (qpc_now() - startQpc) * 1000u / qpc_frequency();
    return elapsedMs >= timeoutMs ? 0u : (uint32_t)(timeoutMs - elapsedMs);
}

const char* kind_name(uint32_t k) {
    switch (k) {
    case kMsgCall: return "CALL";
    case kMsgReturn: return "RETURN";
    case kMsgChunk: return "CHUNK";
    case kMsgChunkAck: return "CHUNK_ACK";
    }
    return "?";
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------
// construction
// ---------------------------------------------------------------------------------------------------------
ChannelImpl::ChannelImpl(const ChannelEnv& env)
    : pub_(this),
      side_(env.side),
      initiatorSide_(env.desc.initiator == kInitiatorServer ? Side::Server : Side::Client),
      index_(env.index),
      initiator_(false),
      sharedLock_(false),
      fls_(FLS_OUT_OF_INDEXES),
      block_(env.block),
      out_(nullptr),
      in_(nullptr),
      myCtl_(nullptr),
      data_(env.data),
      dataBytes_(env.desc.dataBytes),
      ringBytes_(env.desc.ringBytes),
      wait_(env.wait),
      ep_(env.ep),
      flushTicks_(0),
      lastPublishQpc_(0),
      unpubRecords_(0),
      mySeq_(0),
      lastInSeq_(0),
      nextCallId_(0),
      stack_(0),
      turn_(false),
      broken_(false),
      brokenStatus_(Status::Ok),
      inPost_(0),
      frame_(nullptr),
      lastRemoteStatus_(0),
      postScratch_(nullptr),
      callOpen_(false),
      callOpened_(false),
      callEntered_(false),
      callPending_(Status::Ok),
      callMethod_(0),
      postOpen_(false),
      postOpened_(false),
      postEntered_(false),
      postPending_(Status::Ok),
      postMethod_(0),
      ret_(nullptr),
      retCount_(0),
      asm_(nullptr),
      asmCap_(0),
      asmLen_(0),
      asmTotal_(0),
      asmActive_(false),
      owner_(0),
      lockRec_(0),
      top_(0),
      active_(0),
      onCall_(nullptr),
      onPost_(nullptr),
      ctx_(nullptr),
      dbgQHead_(0),
      dbgQBytes_(0) {
    initiator_ = (side_ == initiatorSide_);
    sharedLock_ = side_ == Side::Client && (env.desc.flags & kChannelShared) != 0;
    fls_ = sharedLock_ ? FlsAlloc(&free_thread_result) : FLS_OUT_OF_INDEXES;
    if (side_ == Side::Client) {
        out_ = &block_->c2s;
        in_ = &block_->s2c;
        myCtl_ = &block_->clientCtl;
        prod_.init(&block_->c2sWrite, &block_->c2sRead, env.ringC2S, ringBytes_);
        cons_.init(&block_->s2cWrite, &block_->s2cRead, env.ringS2C, ringBytes_);
    } else {
        out_ = &block_->s2c;
        in_ = &block_->c2s;
        myCtl_ = &block_->serverCtl;
        prod_.init(&block_->s2cWrite, &block_->s2cRead, env.ringS2C, ringBytes_);
        cons_.init(&block_->c2sWrite, &block_->c2sRead, env.ringC2S, ringBytes_);
    }
    InitializeSRWLock(&lock_);
    memset(&stats_, 0, sizeof(stats_));
    memset(dummyBuf_, 0, sizeof(dummyBuf_));
    dbgLive_.base = nullptr;
    dbgLive_.bytes = 0;
    for (uint32_t i = 0; i < kDbgQuarantine; ++i) {
        dbgQ_[i].base = nullptr;
        dbgQ_[i].bytes = 0;
    }
    postScratch_ = (uint8_t*)malloc(prod_.max_payload() ? prod_.max_payload() : 4);
    wc_ = &ep_wait_config(ep_);
    flushTicks_ = (uint64_t)wc_->flushIntervalUs * qpc_frequency() / 1000000u;
    reset_local();
}

ChannelImpl::~ChannelImpl() {
    if (fls_ != FLS_OUT_OF_INDEXES) FlsFree(fls_);  // runs free_thread_result for every thread's buffer
    dbg_expire();
    for (uint32_t i = 0; i < kDbgQuarantine; ++i)
        if (dbgQ_[i].base) VirtualFree(dbgQ_[i].base, 0, MEM_RELEASE);
    for (uint32_t i = 0; i < retCount_; ++i) {
        if (ret_[i]) {
            ret_[i]->~RetSlot();
            free(ret_[i]);
        }
    }
    free(ret_);
    free(asm_);
    free(postScratch_);
}

void ChannelImpl::reset_local() {
    // Touches protocol cursors only: never the lock, the region counters or an open begin_call / begin_post.
    //
    // Precondition (bug 1207 B1): the block was zeroed by channel_reset_shared for the current generation and this
    // side has not used the channel since. The peer may already have used it (published a CALL, posts), so every
    // cursor is set to 0, never re-read from the shared words: re-reading would take a message or record the peer
    // published before this reset as already seen, and it would be lost. Callers, all of which keep this:
    //   - channel_create (the constructor), on a freshly created (zero) section on the client, and on the server
    //     before attach, where the server's reset after CONNECTED below runs before any use;
    //   - the server's attach_server after it sees CONNECTED (the client's monitor zeroed every block first);
    //   - the client's ep_enter at this channel's first entry in a new generation (the monitor zeroed every
    //     block before it set CONNECTED; this side has not entered the channel in this generation);
    //   - tests, after channel_reset_shared.
    mySeq_ = 0;
    lastInSeq_ = 0;
    prod_.reset_local();
    cons_.reset_local();
    nextCallId_ = 0;
    stack_ = 0;
    turn_ = initiator_;
    broken_ = false;
    brokenStatus_ = Status::Ok;
    asmActive_ = false;
    asmLen_ = 0;
    asmTotal_ = 0;
    lastPublishQpc_ = 0;
    unpubRecords_ = 0;
    set_depth(0);
    dbg_expire();
}

void ChannelImpl::set_handlers(CallHandler onCall, PostHandler onPost, void* ctx) {
    onCall_ = onCall;
    onPost_ = onPost;
    ctx_ = ctx;
}

void ChannelImpl::set_return_status(uint32_t status) {
    if (frame_)
        frame_->status = status;
    else
        ep_log(ep_, LogLevel::Warn, "vpipc: channel %u: set_return_status outside a call handler", index_);
}

VPIPC_HOT void ChannelImpl::set_depth(uint32_t d) {
    // SideCtlLine::activeDepth shares its cache line with this side's sleeping flag, which the peer reads in every
    // publish. Writing it on every call made the next locked operation wait for that line to come back from the
    // peer: 65 ns of a 0.40 us empty round trip (task 1209, 2026-11-10). It now records the deepest depth this side
    // has reached (diagnostic), written only when that grows.
    if (d > stats_.maxDepth) {
        stats_.maxDepth = d;
        myCtl_->activeDepth = d;
    }
}

Writer& ChannelImpl::dummy() {
    dummyW_.reset(dummyBuf_, sizeof(dummyBuf_), false);
    return dummyW_;
}

Status ChannelImpl::broken(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    broken_ = true;
    brokenStatus_ = Status::ProtocolError;
    ep_log(ep_, LogLevel::Error, "vpipc: channel %u (%s side): protocol error: %s", index_,
           side_ == Side::Client ? "client" : "server", buf);
    return Status::ProtocolError;
}

Status ChannelImpl::fail_wait(Status s) {
    // A wait inside a call sequence failed: the channel's turn state is unknown until a reset.
    broken_ = true;
    brokenStatus_ = s;  // Timeout, PeerDied, PeerClosed, PeerHung: later calls report the same (bug 1207 B2)
    ep_log(ep_, LogLevel::Warn, "vpipc: channel %u: wait failed inside a call: %s", index_, status_name(s));
    return s;
}

void ChannelImpl::debug_assert_fail(const char* what) {
    ep_log(ep_, LogLevel::Error, "vpipc: channel %u: %s", index_, what);
#if !defined(NDEBUG)
    if (IsDebuggerPresent()) __debugbreak();
#endif
}

// ---------------------------------------------------------------------------------------------------------
// top-level region
// ---------------------------------------------------------------------------------------------------------
VPIPC_HOT bool ChannelImpl::in_top() const {
    if (top_ == 0) return false;
    return !sharedLock_ || owner_ == GetCurrentThreadId();
}

void ChannelImpl::lock() {
    if (!sharedLock_) return;
    DWORD me = GetCurrentThreadId();
    if (owner_ == me) {
        ++lockRec_;
        return;
    }
    AcquireSRWLockExclusive(&lock_);
    owner_ = me;
    lockRec_ = 1;
}

void ChannelImpl::unlock() {
    if (!sharedLock_) return;
    if (--lockRec_ == 0) {
        owner_ = 0;
        ReleaseSRWLockExclusive(&lock_);
    }
}

VPIPC_HOT Status ChannelImpl::enter_top(bool initiating, bool& entered) {
    // On return this thread holds the channel (lock, active count) whatever the status, so the caller may
    // record the failure in the channel's state; leave_top releases it. entered = ep_enter succeeded.
    // Non-shared channels have one thread per side: the monitor only reads active_, so no locked op is needed.
    if (sharedLock_)
        _InterlockedIncrement(&active_);
    else
        active_ = active_ + 1;
    lock();
    // top_ is raised only after ep_enter returns: calls the resync hook makes from inside ep_enter on this
    // thread are top-level calls of their own and go through ep_enter themselves.
    Status s = (ep_ && ep_->enter_fast(index_)) ? Status::Ok : ep_enter(ep_, index_, initiating);
    entered = s == Status::Ok;
    if (entered) ++top_;
    return s;
}

VPIPC_HOT void ChannelImpl::leave_top(bool initiating, bool entered) {
    if (entered) {
        --top_;
        if (!ep_ || !ep_->leave_fast(index_)) ep_leave(ep_, index_, initiating);
    }
    unlock();
    if (sharedLock_)
        _InterlockedDecrement(&active_);
    else
        active_ = active_ - 1;
}

// ---------------------------------------------------------------------------------------------------------
// Debug validity check
// ---------------------------------------------------------------------------------------------------------
const uint8_t* ChannelImpl::dbg_hold(const uint8_t* p, uint32_t len) {
#if VPIPC_VALIDITY_CHECK
    if (len == 0) return p;
    dbg_expire();
    uint32_t bytes = (len + 4095u) & ~4095u;
    void* b = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!b) return p;
    memcpy(b, p, len);
    dbgLive_.base = b;
    dbgLive_.bytes = bytes;
    return (const uint8_t*)b;
#else
    (void)len;
    return p;
#endif
}

void ChannelImpl::dbg_expire() {
#if VPIPC_VALIDITY_CHECK
    if (!dbgLive_.base) return;
    // Decommit (any access faults) but keep the address range reserved for a while so it is not reused.
    VirtualFree(dbgLive_.base, dbgLive_.bytes, MEM_DECOMMIT);
    DbgRegion& slot = dbgQ_[dbgQHead_];
    if (slot.base) {
        VirtualFree(slot.base, 0, MEM_RELEASE);
        dbgQBytes_ -= slot.bytes;
    }
    slot = dbgLive_;
    dbgQBytes_ += slot.bytes;
    dbgQHead_ = (dbgQHead_ + 1) % kDbgQuarantine;
    dbgLive_.base = nullptr;
    dbgLive_.bytes = 0;
    // Bound the reserved address space (x86): release the oldest regions beyond 64 MiB.
    for (uint32_t i = 0; i < kDbgQuarantine && dbgQBytes_ > (64u << 20); ++i) {
        DbgRegion& old = dbgQ_[(dbgQHead_ + i) % kDbgQuarantine];
        if (old.base) {
            VirtualFree(old.base, 0, MEM_RELEASE);
            dbgQBytes_ -= old.bytes;
            old.base = nullptr;
            old.bytes = 0;
        }
    }
#endif
}

// ---------------------------------------------------------------------------------------------------------
// sending
// ---------------------------------------------------------------------------------------------------------
VPIPC_HOT void ChannelImpl::send_piece(uint16_t kind, uint16_t flags, uint32_t method, uint32_t callId, uint32_t depth,
                             uint32_t status, const uint8_t* p, uint32_t len, uint32_t total) {
    MsgLine* o = out_;
    if (len <= kInlineBytes) {
        copy_small(o->inlineData, p, len);
        flags = (uint16_t)(flags | kFlagInline);
    } else if (p != data_) {
        memcpy(data_, p, len);
    }
    o->kind = kind;
    o->flags = flags;
    o->method = method;
    o->callId = callId;
    o->depth = depth;
    o->length = len;
    o->totalLength = total;
    o->status = status;
    // Rule 1 (PROTOCOL.md 4): pending posts become visible before the message. Both are plain stores and the
    // seq store below is an interlocked exchange, so the consumer that sees seq also sees the write index.
    if (unpubRecords_) ring_publish();
    turn_ = false;
    ++mySeq_;
    waitfast::publish(wait_, &o->seq, mySeq_, &stats_);
}

VPIPC_HOT Status ChannelImpl::send(uint16_t kind, uint32_t method, uint32_t callId, uint32_t depth, uint32_t status,
                         const uint8_t* p, uint32_t len) {
    if (!turn_) return broken("send of %s without the turn", kind_name(kind));
    dbg_expire();
    if (!asmActive_ && asmCap_ > kTrimBytes) {  // the received payload is dead: return a large buffer
        free(asm_);
        asm_ = nullptr;
        asmCap_ = 0;
    }
    if (len <= dataBytes_) {
        send_piece(kind, 0, method, callId, depth, status, p, len, len);
        return Status::Ok;
    }
    // Chunked (PROTOCOL.md 3.4): CHUNK pieces of dataBytes, each acknowledged, then the final piece flagged.
    uint32_t off = 0;
    while (len - off > dataBytes_) {
        send_piece(kMsgChunk, 0, method, callId, depth, 0, p + off, dataBytes_, len);
        ++stats_.chunksOut;
        off += dataBytes_;
        RecvMsg m;
        Status s = next_message(m, wc_->replySpinUs, kInfinite, false);
        if (s != Status::Ok) return fail_wait(s);
        if (m.kind != kMsgChunkAck) return broken("expected CHUNK_ACK, got %s", kind_name(m.kind));
    }
    send_piece(kind, kFlagChunked, method, callId, depth, status, p + off, len - off, len);
    return Status::Ok;
}

// ---------------------------------------------------------------------------------------------------------
// synchronous calls
// ---------------------------------------------------------------------------------------------------------
VPIPC_HOT Writer& ChannelImpl::begin_call(uint32_t method) {
    // Take the channel first (shared channel: another thread may be inside a call); only then touch state.
    bool opened = false, entered = false;
    Status pending = Status::Ok;
    if (!in_top()) {
        opened = true;
        pending = enter_top(true, entered);
    }
    if (callOpen_) {
        // begin_call twice on this thread without end_call: the first call is abandoned.
        ep_log(ep_, LogLevel::Error, "vpipc: channel %u: begin_call while a call is open", index_);
        if (callOpened_) leave_top(true, callEntered_);
    }
    callOpen_ = true;
    callOpened_ = opened;
    callEntered_ = entered;
    callPending_ = pending;
    callMethod_ = method;
    if (pending != Status::Ok) return dummy();
    if (inPost_ > 0) {
        callPending_ = Status::ProtocolError;
        debug_assert_fail("synchronous call from inside a post handler (PROTOCOL.md 4, rule 3)");
        return dummy();
    }
    if (broken_) {
        callPending_ = brokenStatus_;  // e.g. Timeout after a call timed out (also inside the resync hook)
        return dummy();
    }
    if (!turn_) {
        // Idle non-initiator (a top-level call from the wrong side), or a call while the peer holds the turn.
        callPending_ = Status::ProtocolError;
        ep_log(ep_, LogLevel::Error, "vpipc: channel %u: call 0x%08X without the turn (%s)", index_, method,
               stack_ == 0 ? "top-level call from the non-initiator" : "peer holds the turn");
        return dummy();
    }
    if (stack_ > wc_->maxDepth) {
        callPending_ = Status::ProtocolError;
        ep_log(ep_, LogLevel::Error, "vpipc: channel %u: call 0x%08X at depth %u exceeds maxDepth %u", index_,
               method, stack_, wc_->maxDepth);
        return dummy();
    }
    dbg_expire();  // the data area is about to be overwritten: a received payload is dead from here on
    callW_.reset(data_, dataBytes_, true);
    return callW_;
}

// Returns Ok, RemoteError or ProtocolError for a RETURN, or the status that ended the wait: Timeout (callTimeoutMs),
// PeerDied, PeerClosed, PeerHung. After a failed wait the channel stays broken until channel_reset_local (a late
// RETURN would arrive out of turn): every later call and serve() on it returns that same status at once.
VPIPC_HOT Status ChannelImpl::end_call(Reader& result) {
    if (!callOpen_) {
        result.reset(nullptr, 0);
        ep_log(ep_, LogLevel::Error, "vpipc: channel %u: end_call without begin_call", index_);
        return Status::ProtocolError;
    }
    callOpen_ = false;
    const bool opened = callOpened_, entered = callEntered_;  // nested calls in do_call reuse the members
    Status s = callPending_;
    if (s == Status::Ok)
        s = do_call(callMethod_, result);
    else
        result.reset(nullptr, 0);
    // Shared channel: once the lock is released another thread may send at once, which would end the
    // RETURN payload's validity for this thread. Move it into this thread's own buffer first.
    if (opened && sharedLock_ && result.remaining()) keep_result_for_thread(result);
    if (opened) leave_top(true, entered);
    return s;
}

namespace {
struct ThreadResult {
    uint8_t* p;
    uint32_t cap;
};
void WINAPI free_thread_result(void* v) {
    ThreadResult* t = (ThreadResult*)v;
    if (t) {
        free(t->p);
        free(t);
    }
}
}  // namespace

void ChannelImpl::keep_result_for_thread(Reader& result) {
    // Valid until this thread's next call on this channel (the buffer is per thread, freed at thread exit
    // or channel destruction through the FLS callback).
    const uint32_t n = result.remaining();
    if (fls_ == FLS_OUT_OF_INDEXES) return;
    ThreadResult* t = (ThreadResult*)FlsGetValue(fls_);
    if (!t) {
        t = (ThreadResult*)calloc(1, sizeof(ThreadResult));
        if (!t) return;
        FlsSetValue(fls_, t);
    }
    if (t->cap < n) {
        uint8_t* p = (uint8_t*)malloc(n);
        if (!p) return;  // keep pointing into the channel: still correct while no other thread sends
        free(t->p);
        t->p = p;
        t->cap = n;
    }
    memcpy(t->p, result.cursor(), n);
    result.reset(t->p, n);
}

VPIPC_HOT Status ChannelImpl::do_call(uint32_t method, Reader& result) {
    result.reset(nullptr, 0);
    if (callW_.overflowed()) return Status::SystemError;  // spill allocation failed
    const uint32_t callId = ++nextCallId_;
    const uint32_t depth = stack_;
    Status s = send(kMsgCall, method, callId, depth, 0, callW_.data(), callW_.size());
    trim_writer(callW_);
    if (s != Status::Ok) return s;
    ++stats_.callsOut;
    ++stack_;
    set_depth(stack_);
    return wait_return(callId, depth, result);
}

VPIPC_HOT Status ChannelImpl::wait_return(uint32_t callId, uint32_t depth, Reader& result) {
    const uint32_t spinUs = wc_->replySpinUs;
    for (;;) {
        RecvMsg m;
        Status s = next_message(m, spinUs, kInfinite, true);
        if (s != Status::Ok) return broken_ ? s : fail_wait(s);
        if (m.kind == kMsgCall) {
            s = handle_call(m);
            if (broken_) return brokenStatus_;
            continue;
        }
        if (m.kind != kMsgReturn) return broken("waiting for RETURN %u, got %s", callId, kind_name(m.kind));
        if (m.callId != callId || m.depth != depth)
            return broken("RETURN callId %u depth %u, expected callId %u depth %u", m.callId, m.depth, callId,
                          depth);
        --stack_;
        set_depth(stack_);
        lastRemoteStatus_ = m.status;
        result.reset(m.payload, m.length);
        switch (m.status) {
        case kRetOk: return Status::Ok;
        case kRetProtocolError: return Status::ProtocolError;
        default: return Status::RemoteError;
        }
    }
}

VPIPC_HOT Writer& ChannelImpl::ret_writer(uint32_t depth) {
    if (depth >= retCount_) {
        uint32_t n = depth + 8;
        RetSlot** a = (RetSlot**)realloc(ret_, n * sizeof(RetSlot*));
        if (!a) return dummy();
        for (uint32_t i = retCount_; i < n; ++i) a[i] = nullptr;
        ret_ = a;
        retCount_ = n;
    }
    if (!ret_[depth]) {
        void* mem = malloc(sizeof(RetSlot));
        if (!mem) return dummy();
        ret_[depth] = new (mem) RetSlot();
    }
    RetSlot* r = ret_[depth];
    r->w.reset(r->buf, sizeof(r->buf), true);
    return r->w;
}

VPIPC_HOT Status ChannelImpl::handle_call(const RecvMsg& m) {
    const WaitConfig& wc = *wc_;
    char err[160];
    err[0] = 0;
    if (m.depth != stack_)
        _snprintf_s(err, sizeof(err), _TRUNCATE, "CALL 0x%08X at depth %u, receiver expected depth %u", m.method, m.depth,
                 stack_);
    else if (m.depth == 0 && initiator_)
        _snprintf_s(err, sizeof(err), _TRUNCATE, "top-level CALL 0x%08X from the non-initiator", m.method);
    else if (m.depth > wc.maxDepth)
        _snprintf_s(err, sizeof(err), _TRUNCATE, "CALL 0x%08X at depth %u exceeds the receiver's maxDepth %u", m.method,
                 m.depth, wc.maxDepth);
    err[sizeof(err) - 1] = 0;

    bool opened = false, entered = false;
    if (!err[0] && m.depth == 0 && !in_top()) {  // serve()/poll() have entered already
        opened = true;
        Status es = enter_top(false, entered);
        if (es != Status::Ok)
            _snprintf_s(err, sizeof(err), _TRUNCATE, "serving side not ready: %s", status_name(es));
    }

    ++stats_.callsIn;
    ++stack_;
    set_depth(stack_);
    HandlerFrame f;
    f.prev = frame_;
    f.status = kRetOk;
    frame_ = &f;
    Writer& w = ret_writer(stack_);
    if (err[0]) {
        ep_log(ep_, LogLevel::Error, "vpipc: channel %u: protocol error: %s", index_, err);
        f.status = kRetProtocolError;
        w.str(err);
    } else if (!onCall_) {
        _snprintf_s(err, sizeof(err), _TRUNCATE, "no call handler on channel %u (method 0x%08X)", index_, m.method);
        f.status = kRetUnknownMethod;
        w.str(err);
    } else {
        Reader args(m.payload, m.length);
        onCall_(ctx_, pub_, m.method, args, w);
    }
    frame_ = f.prev;
    --stack_;
    set_depth(stack_);

    Status s;
    if (broken_)
        s = brokenStatus_;  // a nested wait failed: the channel is out of step, do not answer
    else if (w.overflowed())
        s = broken("RETURN for 0x%08X could not be allocated", m.method);
    else
        s = send(kMsgReturn, m.method, m.callId, m.depth, f.status, w.data(), w.size());
    trim_writer(w);
    if (opened) leave_top(false, entered);
    return s;
}

// ---------------------------------------------------------------------------------------------------------
// receiving
// ---------------------------------------------------------------------------------------------------------
Status ChannelImpl::drain_posts() {
    uint32_t method;
    const uint8_t* p;
    uint32_t len;
    while (cons_.peek(method, p, len)) {
        uint8_t local[256];
        uint8_t* copy = local;
        if (len > sizeof(local)) {
            copy = (uint8_t*)malloc(len);
            if (!copy) return broken("out of memory for a %u-byte post", len);
        }
        if (copy == local)
            copy_small(copy, p, len);
        else
            memcpy(copy, p, len);
        cons_.pop();
        ++stats_.postsIn;
        if (onPost_) {
            ++inPost_;
            Reader r(copy, len);
            onPost_(ctx_, pub_, method, r);
            --inPost_;
        }
        if (copy != local) free(copy);
        if (broken_) return brokenStatus_;
    }
    if (cons_.corrupt()) return broken("incoming post ring is corrupt");
    return Status::Ok;
}

VPIPC_HOT Status ChannelImpl::try_receive(RecvMsg& m, bool& got, bool acceptChunks) {
    got = false;
    for (;;) {
        const uint32_t seq = in_->seq;
        _ReadWriteBarrier();
        if (!cons_.empty()) {
            // Rule 1: records published before seq are visible now; drain them before the message.
            Status s = drain_posts();
            if (s != Status::Ok) return s;
            continue;
        }
        if (seq == lastInSeq_) return Status::Ok;
        if (seq != lastInSeq_ + 1u) return broken("incoming seq %u after %u", seq, lastInSeq_);
        const MsgLine* in = in_;
        m.kind = in->kind;
        m.flags = in->flags;
        m.method = in->method;
        m.callId = in->callId;
        m.depth = in->depth;
        m.status = in->status;
        const uint32_t len = in->length;
        const uint32_t total = in->totalLength;
        lastInSeq_ = seq;
        turn_ = true;

        const uint8_t* piece;
        if (m.flags & kFlagInline) {
            if (len > kInlineBytes) return broken("inline length %u", len);
            piece = in->inlineData;
        } else {
            if (len > dataBytes_) return broken("length %u exceeds the data area (%u)", len, dataBytes_);
            piece = data_;
        }

        if (m.kind == kMsgChunk) {
            if (!acceptChunks) return broken("unexpected CHUNK");
            if (!asmActive_) {
                if (total <= dataBytes_) return broken("CHUNK of a %u-byte payload", total);
                if (total > asmCap_) {
                    free(asm_);
                    asm_ = (uint8_t*)malloc(total);
                    asmCap_ = asm_ ? total : 0;
                    if (!asm_) return broken("out of memory for a %u-byte chunked payload", total);
                }
                asmActive_ = true;
                asmLen_ = 0;
                asmTotal_ = total;
            }
            if (total != asmTotal_ || len > asmTotal_ - asmLen_) return broken("CHUNK does not fit");
            memcpy(asm_ + asmLen_, piece, len);
            asmLen_ += len;
            ++stats_.chunksIn;
            send_piece(kMsgChunkAck, 0, m.method, m.callId, m.depth, 0, nullptr, 0, 0);
            continue;
        }
        if (m.kind == kMsgChunkAck) {
            m.payload = nullptr;
            m.length = 0;
            got = true;
            return Status::Ok;
        }
        if (m.flags & kFlagChunked) {
            if (!asmActive_ || total != asmTotal_ || len != asmTotal_ - asmLen_)
                return broken("final chunked piece does not complete the payload");
            memcpy(asm_ + asmLen_, piece, len);
            asmActive_ = false;
            m.payload = dbg_hold(asm_, total);
            m.length = total;
        } else {
            if (asmActive_) return broken("%s while a chunked payload is incomplete", kind_name(m.kind));
            m.payload = dbg_hold(piece, len);
            m.length = len;
        }
        got = true;
        return Status::Ok;
    }
}

VPIPC_HOT Status ChannelImpl::next_message(RecvMsg& m, uint32_t spinUs, uint32_t timeoutMs, bool acceptChunks) {
    const uint64_t start = timeoutMs != kInfinite ? qpc_now() : 0;
    for (;;) {
        // Sample the watched words before looking, so a change after the look ends the wait at once.
        const volatile uint32_t* words[2] = {&in_->seq, cons_.write_index_word()};
        uint32_t seen[2];
        seen[0] = *words[0];
        seen[1] = *words[1];
        _ReadWriteBarrier();
        bool got = false;
        Status s = try_receive(m, got, acceptChunks);
        if (s != Status::Ok || got) return s;
        if (in_->seq != seen[0] || *words[1] != seen[1]) continue;  // consumed something while looking
        if (unpubRecords_) publish_posts(0);  // never wait with records the peer cannot see
        // Look again and pre-spin inline before the out-of-line wait (it reads the clock): task 1209. If this ends
        // without a change, wait_change pre-spins once more before its first clock read (~2-3 us of pauses in all
        // before the spin budget starts counting).
        if (timeoutMs != 0 && waitfast::quick(words, seen, 2, spinUs, &stats_)) continue;
        WaitResult r = wait_change(wait_, words, seen, 2, spinUs, remaining_ms(timeoutMs, start), &stats_);
        if (r != WaitResult::Changed) return map_wait(r);
    }
}

// ---------------------------------------------------------------------------------------------------------
// serving
// ---------------------------------------------------------------------------------------------------------
Status ChannelImpl::serve(uint32_t timeoutMs) {
    if (inPost_ > 0) {
        debug_assert_fail("serve() from inside a post handler");
        return Status::ProtocolError;
    }
    lock();
    if (stack_ != 0 || in_top()) {
        unlock();
        ep_log(ep_, LogLevel::Error, "vpipc: channel %u: serve() inside a call", index_);
        return Status::ProtocolError;
    }
    // The idle wait runs in chunks, each inside ep_enter(serving) / ep_leave:
    // - ep_enter runs before any cursor is read, so a generation change resets this channel first (client);
    //   while a chunk is inside, the client's monitor holds off an attach, so cursors cannot go stale under
    //   the wait; between chunks an attach can proceed (and while one is pending, ep_enter fails fast).
    // - an explicit timeout per chunk keeps WaitConfig::callTimeoutMs (meant for replies) off the idle wait.
    const WaitConfig& wc = *wc_;
    const uint32_t chunkMs = side_ == Side::Client ? (wc.tickMs ? wc.tickMs : 100u) : 1000u;
    const uint64_t start = timeoutMs != kInfinite ? qpc_now() : 0;  // remaining_ms ignores it for kInfinite
    Status s;
    for (;;) {
        uint32_t waitMs = remaining_ms(timeoutMs, start);
        const bool last = waitMs <= chunkMs;
        if (!last) waitMs = chunkMs;
        bool entered = false;
        s = enter_top(false, entered);
        if (s == Status::Ok) {
            if (broken_) {
                s = brokenStatus_;
            } else {
                RecvMsg m;
                s = next_message(m, wc.idleSpinUs, waitMs, true);
                if (s == Status::Ok) {
                    if (m.kind == kMsgCall)
                        s = handle_call(m);
                    else
                        s = broken("serving: unexpected %s", kind_name(m.kind));
                }
            }
        }
        leave_top(false, entered);
        if (s != Status::Timeout || last) break;
    }
    unlock();
    return s;
}

Status ChannelImpl::poll() {
    if (inPost_ > 0) {
        debug_assert_fail("poll() from inside a post handler");
        return Status::ProtocolError;
    }
    lock();
    Status s = Status::Ok;
    if (stack_ == 0 && !in_top()) {
        bool entered = false;
        s = enter_top(false, entered);  // before reading any cursor (see serve)
        if (s == Status::Ok && broken_) s = brokenStatus_;
        if (s == Status::Ok) {
            RecvMsg m;
            bool got = false;
            s = try_receive(m, got, true);
            // A chunked message that has started is completed before returning.
            if (s == Status::Ok && !got && asmActive_) {
                s = next_message(m, wc_->replySpinUs, kInfinite, true);
                got = s == Status::Ok;
            }
            if (s == Status::Ok && got) {
                if (m.kind == kMsgCall)
                    s = handle_call(m);
                else
                    s = broken("polling: unexpected %s", kind_name(m.kind));
                if (s == Status::Ok) s = drain_posts();
            }
        }
        leave_top(false, entered);
    } else {
        // Inside a call on this channel (a handler polling): only posts can be pending.
        s = drain_posts();
    }
    unlock();
    return s;
}

// ---------------------------------------------------------------------------------------------------------
// posts
// ---------------------------------------------------------------------------------------------------------
// Posts are batched (task 1206): end_post writes the record into the ring but does not always make it visible.
// The consumer sees it when this side next sends a synchronous message on the channel (a call, a RETURN, a chunk),
// calls flush(), starts to wait (end_call, serve, a full ring), when the unpublished records reach 1/8 of the ring,
// or at a post made flushIntervalUs or more after the last post-time publish (the first post after a quiet
// period goes out at once). Bound (bug 1208), as long as posts keep coming: a post becomes visible within about
// flushIntervalUs + one post spacing when posts are sparse (fewer than 16 unpublished records, the clock is read
// at every post), and within 2 x flushIntervalUs in the worst case (a dense burst, clock read every 16th post).
// A producer that posts and then goes idle without any of these leaves its last records unpublished: call
// flush() at the end of a burst that no call follows (e.g. at the end of a frame).
Writer& ChannelImpl::begin_post(uint32_t method) {
    bool opened = false, entered = false;
    Status pending = Status::Ok;
    if (!in_top()) {
        opened = true;
        pending = enter_top(true, entered);
    }
    if (postOpen_) {
        ep_log(ep_, LogLevel::Error, "vpipc: channel %u: begin_post while a post is open", index_);
        if (postOpened_) leave_top(true, postEntered_);
    }
    postOpen_ = true;
    postOpened_ = opened;
    postEntered_ = entered;
    postPending_ = pending;
    postMethod_ = method;
    if (pending != Status::Ok) return dummy();
    if (!postScratch_) {
        postPending_ = Status::SystemError;
        return dummy();
    }
    postW_.reset(postScratch_, prod_.max_payload(), false);
    return postW_;
}

Status ChannelImpl::end_post() {
    if (!postOpen_) {
        ep_log(ep_, LogLevel::Error, "vpipc: channel %u: end_post without begin_post", index_);
        return Status::ProtocolError;
    }
    postOpen_ = false;
    const bool opened = postOpened_, entered = postEntered_;  // posts from handlers may reuse the members
    Status s = postPending_;
    if (s == Status::Ok) {
        if (postW_.overflowed())
            s = Status::PayloadTooLarge;
        else
            s = push_post(postMethod_, postW_.data(), postW_.size());
    }
    if (opened) leave_top(true, entered);
    return s;
}

Status ChannelImpl::push_post(uint32_t method, const uint8_t* p, uint32_t len) {
    // try_write fails forever on these two (internal.h): never wait on them.
    if (len > prod_.max_payload()) return Status::PayloadTooLarge;
    if (method == kRingPadMethod) {
        ep_log(ep_, LogLevel::Error, "vpipc: channel %u: post method 0x%08X is reserved", index_, method);
        return Status::ProtocolError;
    }
    if (!prod_.try_write(method, p, len)) {
        // Ring full. Publish what is pending and wake the consumer, copy the record out of the scratch buffer
        // (post handlers that run while we wait may post), then wait for the read index, draining our incoming
        // ring meanwhile.
        publish_posts(qpc_now());
        uint8_t* copy = (uint8_t*)malloc(len ? len : 4u);
        if (!copy) return Status::SystemError;
        if (len) memcpy(copy, p, len);
        const uint32_t spinUs = wc_->replySpinUs;
        Status s = Status::Ok;
        for (;;) {
            const volatile uint32_t* words[2] = {prod_.read_index_word(), cons_.write_index_word()};
            uint32_t seen[2];
            seen[0] = *words[0];
            seen[1] = *words[1];
            _ReadWriteBarrier();
            if (prod_.try_write(method, copy, len)) break;
            publish_posts(0);  // a post handler that ran while we waited may have written records
            if (!cons_.empty()) {
                s = drain_posts();
                if (s != Status::Ok) break;
                continue;
            }
            WaitResult r = wait_change(wait_, words, seen, 2, spinUs, kInfinite, &stats_);
            if (r != WaitResult::Changed) {
                s = map_wait(r);
                break;
            }
        }
        free(copy);
        if (s != Status::Ok) return s;
    }
    ++stats_.postsOut;
    ++unpubRecords_;
    // Batch rules (task 1206). The record stays unpublished unless the batch reaches 1/8 of the ring, or
    // flushIntervalUs has passed since the last post-time publish. The clock (QueryPerformanceCounter, ~30 ns on
    // the development machine) is read at every post while fewer than 16 records are unpublished, then at every
    // 16th (bug 1208: reading it only at the 1st and every 16th delayed sparse posts by up to 15 spacings). So the
    // first post after a quiet period goes out at once, sparse posts are checked one by one, and a long burst
    // costs one clock read per 16 posts.
    // 1/8 rather than 1/2: a consumer that is spinning drains each published part while the producer writes the
    // next (measured 2026-11-10: batch of 1000 + call 41-48 ns/post at 1/8, 59-70 ns/post at 1/2).
    if (prod_.unpublished() >= ringBytes_ / kPublishFraction || flushTicks_ == 0) {
        publish_posts(0);
    } else if (unpubRecords_ < 16u || (unpubRecords_ & 15u) == 0) {
        const uint64_t now = qpc_now();
        if (now - lastPublishQpc_ >= flushTicks_) publish_posts(now);
    }
    return Status::Ok;
}

void ChannelImpl::ring_publish() {
    prod_.publish();
    unpubRecords_ = 0;
}

void ChannelImpl::publish_posts(uint64_t now) {
    // One release store for the whole batch, then wake_peer: its locked compare-exchange is the full fence the
    // ring publish needs before the consumer's sleeping flag is read (internal.h contract).
    if (prod_.unpublished()) ring_publish();
    unpubRecords_ = 0;
    if (now) lastPublishQpc_ = now;
    if (prod_.used()) wake_peer(wait_, &stats_);
}

Status ChannelImpl::flush() {
    lock();  // the shared channel's producer state belongs to the lock holder
    publish_posts(qpc_now());
    unlock();
    return Status::Ok;
}

// ---------------------------------------------------------------------------------------------------------
// Channel (public handle) and the internal.h seams
// ---------------------------------------------------------------------------------------------------------
Writer& Channel::begin_call(uint32_t method) { return impl_->begin_call(method); }
Status Channel::end_call(Reader& result) { return impl_->end_call(result); }
uint32_t Channel::last_remote_status() const { return impl_->last_remote_status(); }
Writer& Channel::begin_post(uint32_t method) { return impl_->begin_post(method); }
Status Channel::end_post() { return impl_->end_post(); }
Status Channel::flush() { return impl_->flush(); }
void Channel::set_handlers(CallHandler onCall, PostHandler onPost, void* ctx) {
    impl_->set_handlers(onCall, onPost, ctx);
}
void Channel::set_return_status(uint32_t status) { impl_->set_return_status(status); }
Status Channel::serve(uint32_t timeoutMs) { return impl_->serve(timeoutMs); }
Status Channel::poll() { return impl_->poll(); }
uint32_t Channel::index() const { return impl_->index(); }
Side Channel::initiator() const { return impl_->initiator(); }
Side Channel::side() const { return impl_->side(); }
ChannelStats Channel::stats() const { return impl_->stats(); }

ChannelImpl* channel_create(const ChannelEnv& env) {
    void* mem = malloc(sizeof(ChannelImpl));
    if (!mem) return nullptr;
    return new (mem) ChannelImpl(env);
}

void channel_destroy(ChannelImpl* ch) {
    if (!ch) return;
    ch->~ChannelImpl();
    free(ch);
}

Channel& channel_public(ChannelImpl* ch) { return ch->pub_; }

void channel_reset_shared(ChannelBlock* block, const ChannelDesc& desc) {
    (void)desc;
    memset((void*)block, 0, sizeof(ChannelBlock));
}

void channel_reset_local(ChannelImpl* ch) { ch->reset_local(); }

uint32_t channel_active(ChannelImpl* ch) { return ch->active(); }

}  // namespace vpipc
