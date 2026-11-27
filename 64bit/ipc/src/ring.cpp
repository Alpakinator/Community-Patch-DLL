// src/ring.cpp - single-producer single-consumer byte rings for posts (PROTOCOL.md section 4). Task 1201.
//
// Task 1201; batching (try_write / publish) task 1206.
//
// Ring data is `bytes` long (a power of two). The write and read indices are monotonically increasing u32 byte
// counts (they wrap at 2^32; used = write - read in modular arithmetic); position = index & (bytes - 1).
// A record is RingRecord {u32 length, u32 method} followed by the payload, padded to 8 bytes
// (ring_record_bytes). Every record is a multiple of 8 bytes, so both indices are always multiples of 8 and the
// space left before the wrap point is always a multiple of 8 and at least 8: the "fewer than 8 bytes before the
// wrap" case cannot arise, there is never implicit padding, and the consumer treats an index distance that is
// not a multiple of 8 as corruption. When a record does not fit before the wrap point the producer writes a pad
// record {length = toEnd - 8, method = kRingPadMethod} covering exactly the rest of the ring, then the record at
// offset 0; both become visible with the same write-index store.
//
// Batching (task 1206): try_write() writes a record without publishing it; publish() stores the write index once
// for everything written since the last publish. The consumer therefore sees a whole batch at once instead of
// chasing the producer record by record, and the producer reads the consumer's read index only when its cached
// copy says the ring is too full. try_push() = try_write() + publish().
//
// Memory order (x86/x64 are TSO): the producer writes the record bytes, then a compiler barrier, then stores the
// write index; the consumer loads the write index, a compiler barrier, then reads the record. The consumer
// stores the read index after pop() (its payload reads are done; loads are not reordered with later stores).
// The index stores are release stores, not full fences: a caller that next checks the peer's sleeping flag
// (section 6 handshake) must issue a full fence first (publish() / MemoryBarrier), or a store-load reordering
// can lose a wake-up.
#include <emmintrin.h>
#include <intrin.h>
#include <string.h>

#include "internal.h"

namespace vpipc {

namespace {
// Same helper as in marshal.cpp, kept file-local.
// Small copies inline (16-byte SSE2 chunks, overlapping tail); large ones through memcpy. Measured 2026-11-10:
// msvcr90's memcpy (the x86 proxy's CRT) costs ~30 ns for a 64-byte copy to a 4-aligned destination.
inline void copy_small(uint8_t* d, const void* src, uint32_t n) {
    const uint8_t* s = static_cast<const uint8_t*>(src);
    if (n > 256) {
        memcpy(d, s, n);
    } else if (n >= 16) {
        const uint8_t* last = s + n - 16;
        uint8_t* dlast = d + n - 16;
        for (; s < last; s += 16, d += 16) _mm_storeu_si128((__m128i*)d, _mm_loadu_si128((const __m128i*)s));
        _mm_storeu_si128((__m128i*)dlast, _mm_loadu_si128((const __m128i*)last));
    } else if (n >= 8) {
        uint64_t a, b;
        memcpy(&a, s, 8);
        memcpy(&b, s + n - 8, 8);
        memcpy(d, &a, 8);
        memcpy(d + n - 8, &b, 8);
    } else if (n >= 4) {
        uint32_t a, b;
        memcpy(&a, s, 4);
        memcpy(&b, s + n - 4, 4);
        memcpy(d, &a, 4);
        memcpy(d + n - 4, &b, 4);
    } else {
        for (uint32_t i = 0; i < n; ++i) d[i] = s[i];
    }
}
inline uint32_t load_acquire(const volatile uint32_t* p) {
    const uint32_t v = *p;
    _ReadWriteBarrier();
    return v;
}
inline void store_release(volatile uint32_t* p, uint32_t v) {
    _ReadWriteBarrier();
    *p = v;
}
}  // namespace

// ------------------------------------------------------------------------------------------------ producer

void RingProducer::init(RingIndexLine* write, const RingIndexLine* read, uint8_t* data, uint32_t bytes) {
    write_ = write;
    read_ = read;
    data_ = data;
    bytes_ = bytes;
    // init adopts the indices as they are (a ring may start anywhere, e.g. near 2^32 in the tests);
    // reset_local is for a block that channel_reset_shared has just zeroed.
    w_ = write_ ? write_->index : 0;
    pub_ = w_;
    rCache_ = read_ ? read_->index : 0;
}

// After channel_reset_shared (bug 1207 B1): the shared indices are known to be zero, so the cursors are set to 0,
// never re-read (the consumer may already have moved the read index of the new generation).
void RingProducer::reset_local() {
    w_ = 0;
    pub_ = 0;
    rCache_ = 0;
}

uint32_t RingProducer::used() const { return w_ - load_acquire(&read_->index); }

// False when there is not enough free space now (retry after the consumer moves the read index), and also,
// permanently, for len > max_payload() or method == kRingPadMethod: callers check those first.
// Task 1206: the record is written but not published; publish() makes everything written visible at once.
bool RingProducer::try_write(uint32_t method, const void* payload, uint32_t len) {
    if (len > bytes_ / 4u || method == kRingPadMethod) return false;
    const uint32_t rec = ring_record_bytes(len);
    const uint32_t mask = bytes_ - 1u;
    const uint32_t pos = w_ & mask;
    const uint32_t toEnd = bytes_ - pos;
    const uint32_t need = rec > toEnd ? toEnd + rec : rec;
    // Free space from the cached read index; the consumer's line is read only when that is not enough, so a
    // batch does not pull the read index across cores record by record.
    if (need > bytes_ - (w_ - rCache_)) {
        rCache_ = load_acquire(&read_->index);
        if (need > bytes_ - (w_ - rCache_)) return false;
    }
    uint8_t* at = data_ + pos;
    if (rec > toEnd) {  // pad to the wrap point, record at offset 0
        RingRecord pad;
        pad.length = toEnd - (uint32_t)sizeof(RingRecord);
        pad.method = kRingPadMethod;
        memcpy(at, &pad, sizeof pad);
        at = data_;
    }
    RingRecord hdr;
    hdr.length = len;
    hdr.method = method;
    memcpy(at, &hdr, sizeof hdr);
    copy_small(at + sizeof hdr, payload, len);
    w_ += need;
    return true;
}

void RingProducer::publish() {
    if (pub_ == w_) return;
    pub_ = w_;
    store_release(&write_->index, w_);
}

bool RingProducer::try_push(uint32_t method, const void* payload, uint32_t len) {
    if (!try_write(method, payload, len)) return false;
    publish();
    return true;
}

// ------------------------------------------------------------------------------------------------ consumer

void RingConsumer::init(const RingIndexLine* write, RingIndexLine* read, uint8_t* data, uint32_t bytes) {
    write_ = write;
    read_ = read;
    data_ = data;
    bytes_ = bytes;
    r_ = read_ ? read_->index : 0;  // adopt the index as it is (see RingProducer::init)
    pending_ = 0;
    corrupt_ = false;
}

// After channel_reset_shared (bug 1207 B1): the read index is known to be zero.
void RingConsumer::reset_local() {
    r_ = 0;
    pending_ = 0;
    corrupt_ = false;
}

bool RingConsumer::empty() const { return load_acquire(&write_->index) == r_; }

bool RingConsumer::peek(uint32_t& method, const uint8_t*& payload, uint32_t& len) {
    pending_ = 0;
    if (corrupt_) return false;
    const uint32_t avail = load_acquire(&write_->index) - r_;
    if (avail == 0) return false;
    if (avail > bytes_ || (avail & 7u) || (r_ & 7u)) {
        corrupt_ = true;
        return false;
    }
    uint32_t pos = r_ & (bytes_ - 1u);
    uint32_t skip = 0;
    RingRecord hdr;
    memcpy(&hdr, data_ + pos, sizeof hdr);
    if (hdr.method == kRingPadMethod) {
        const uint32_t toEnd = bytes_ - pos;
        if (hdr.length != toEnd - (uint32_t)sizeof(RingRecord) || toEnd > avail || pos == 0) {
            corrupt_ = true;
            return false;
        }
        skip = toEnd;
        pos = 0;
        if (skip == avail) {  // a pad published on its own: consume it, nothing else is there yet
            r_ += skip;
            store_release(&read_->index, r_);
            return false;
        }
        memcpy(&hdr, data_, sizeof hdr);
        if (hdr.method == kRingPadMethod) {  // two pads in a row cannot happen
            corrupt_ = true;
            return false;
        }
    }
    if (hdr.length > bytes_ / 4u) {  // impossible length
        corrupt_ = true;
        return false;
    }
    const uint32_t rec = ring_record_bytes(hdr.length);
    if (rec > bytes_ - pos || rec > avail - skip) {  // crosses the wrap point or past the write index
        corrupt_ = true;
        return false;
    }
    method = hdr.method;
    payload = data_ + pos + sizeof(RingRecord);
    len = hdr.length;
    pending_ = skip + rec;
    return true;
}

void RingConsumer::pop() {
    if (!pending_) return;
    r_ += pending_;
    pending_ = 0;
    store_release(&read_->index, r_);
}

}  // namespace vpipc
