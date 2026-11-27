// src/marshal.cpp - Writer / Reader (PROTOCOL.md section 5). Task 1201.
//
// Encoding: little-endian, every value 4-byte aligned, no type tags. Every encoded value occupies a multiple
// of 4 bytes, so a complete payload's length is always a multiple of 4 and padding bytes are always zero
// (payloads are byte-for-byte deterministic).
//
// Writer: writes into the caller's fixed buffer; when a write would pass the buffer's capacity it either moves
// what it has into a malloc'd spill buffer it owns and continues there (allowSpill), or sets overflowed() and
// drops that and every further write. A failed spill allocation also sets overflowed(). A payload never exceeds
// kMaxPayload (1 GiB), even when spilling: a larger write sets overflowed() without touching the source (this
// also catches lengths such as 0xFFFFFFF0 passed by mistake). reserve() returns nullptr once overflowed.
//
// Reader: every read is bounds-checked. A read past the end or a malformed length clears ok() and returns a
// zero result (0, 0.0, false, nullptr with length 0, Handle{0,0}); once ok() is false every later
// read returns a zero result too. A string whose terminating NUL (unit) is missing is malformed: cstr()/wcstr()
// promise a terminated string inside the payload.
#include <emmintrin.h>
#include <stdlib.h>
#include <string.h>

#include "vpipc/layout.h"
#include "vpipc/marshal.h"

namespace vpipc {

static_assert(sizeof(wchar_t) == 2, "wstr encodes UTF-16 units");
static_assert(sizeof(float) == 4 && sizeof(double) == 8, "IEEE 754");

namespace {
constexpr uint32_t kMaxPayload = 0x40000000u;  // 1 GiB: a sanity bound, far above any real payload (50 MB saves)

inline uint32_t round4(uint32_t n) { return (n + 3u) & ~3u; }  // callers keep n <= kMaxPayload
inline void put32(uint8_t* p, uint32_t v) { memcpy(p, &v, 4); }
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
inline void zero_last_word(uint8_t* area, uint32_t n) {  // n: rounded size of the area, > 0
    memset(area + n - 4, 0, 4);
}
}  // namespace

// ------------------------------------------------------------------------------------------------- Writer

Writer::~Writer() { free(spill_); }

void Writer::reset(uint8_t* buf, uint32_t cap, bool allowSpill) {
    buf_ = buf;
    cap_ = buf ? cap : 0;
    len_ = 0;
    allowSpill_ = allowSpill;
    spilled_ = false;
    overflowed_ = false;
    // spill_ / spillCap_ are kept for the next payload
}

const uint8_t* Writer::data() const { return spilled_ ? spill_ : buf_; }

// n is a multiple of 4. Returns where to write n bytes and advances len_, or nullptr (overflowed).
uint8_t* Writer::grow(uint32_t n) {
    if (overflowed_) return nullptr;
    if (n > kMaxPayload - len_) {  // len_ <= kMaxPayload always
        overflowed_ = true;
        return nullptr;
    }
    const uint32_t at = len_;
    const uint32_t need = at + n;
    if (!spilled_) {
        if (n <= cap_ - at) {
            len_ = need;
            return buf_ + at;
        }
        if (!allowSpill_) {
            overflowed_ = true;
            return nullptr;
        }
    }
    if (need > spillCap_) {  // geometric growth, at least twice the fixed buffer, at least 256 bytes
        uint32_t want = spillCap_ ? spillCap_ : 256u;
        if (want < cap_ * 2u && cap_ <= kMaxPayload) want = cap_ * 2u;
        while (want < need) want *= 2u;  // want <= 2 GiB here, need <= 1 GiB
        if (want > kMaxPayload) want = kMaxPayload;  // need <= kMaxPayload
        uint8_t* p = static_cast<uint8_t*>(realloc(spill_, want));
        if (!p) {
            overflowed_ = true;
            return nullptr;
        }
        spill_ = p;
        spillCap_ = want;
    }
    if (!spilled_) {  // move what the fixed buffer holds
        if (at) memcpy(spill_, buf_, at);
        spilled_ = true;
    }
    len_ = need;
    return spill_ + at;
}

// i32 / u32 / f32 / b are inline in marshal.h (put4).
void Writer::i64(int64_t v) {
    if (uint8_t* p = grow(8)) memcpy(p, &v, 8);
}
void Writer::u64(uint64_t v) {
    if (uint8_t* p = grow(8)) memcpy(p, &v, 8);
}
void Writer::f64(double v) {
    if (uint8_t* p = grow(8)) memcpy(p, &v, 8);
}

void Writer::str(const char* s) {
    if (!s) {
        u32(kNullLength);
        return;
    }
    const size_t n = strlen(s);
    if (n >= kMaxPayload) {
        overflowed_ = true;
        return;
    }
    str(s, (uint32_t)n);
}

// (nullptr, 0) is the empty string; (nullptr, len > 0) has no bytes to send and is encoded as null.
void Writer::str(const char* s, uint32_t len) {
    if (!s && len) {
        u32(kNullLength);
        return;
    }
    if (len >= kMaxPayload) {
        overflowed_ = true;
        return;
    }
    const uint32_t body = round4(len + 1u);
    uint8_t* p = grow(4u + body);
    if (!p) return;
    put32(p, len);
    zero_last_word(p + 4, body);  // NUL and pad are in the last word
    copy_small(p + 4, s, len);
}

void Writer::wstr(const wchar_t* s) {
    if (!s) {
        u32(kNullLength);
        return;
    }
    const size_t n = wcslen(s);
    if (n >= kMaxPayload / 2) {
        overflowed_ = true;
        return;
    }
    wstr(s, (uint32_t)n);
}

void Writer::wstr(const wchar_t* s, uint32_t units) {
    if (!s && units) {
        u32(kNullLength);
        return;
    }
    if (units >= kMaxPayload / 2) {
        overflowed_ = true;
        return;
    }
    const uint32_t body = round4(units * 2u + 2u);
    uint8_t* p = grow(4u + body);
    if (!p) return;
    put32(p, units);
    zero_last_word(p + 4, body);  // NUL unit and pad are in the last word
    copy_small(p + 4, s, units * 2u);
}

void Writer::bytes(const void* src, uint32_t len) {
    if (len >= kMaxPayload) {
        overflowed_ = true;
        return;
    }
    const uint32_t body = round4(len);
    uint8_t* p = grow(4u + body);
    if (!p) return;
    put32(p, len);
    if (len) {
        zero_last_word(p + 4, body);
        copy_small(p + 4, src, len);
    }
}

void Writer::handle(Handle h) {
    if (uint8_t* p = grow(8)) {
        memcpy(p, &h.type, 4);
        memcpy(p + 4, &h.id, 4);
    }
}

uint8_t* Writer::reserve(uint32_t n) {
    if (n >= kMaxPayload) {
        overflowed_ = true;
        return nullptr;
    }
    const uint32_t r = round4(n);
    uint8_t* p = grow(r);
    if (p && r) zero_last_word(p, r);  // the pad past n is zero; the caller fills [p, p + n)
    return p;
}

// ------------------------------------------------------------------------------------------------- Reader

void Reader::reset(const uint8_t* p, uint32_t len) {
    p_ = p;
    end_ = p ? p + len : p;
    ok_ = !(p == nullptr && len != 0);
}

// Returns n bytes (n rounded up to 4) and advances, or nullptr with ok_ = false.
const uint8_t* Reader::take(uint32_t n) {
    if (!ok_) return nullptr;
    const uint64_t r = ((uint64_t)n + 3u) & ~(uint64_t)3u;
    if (r > (uint64_t)(end_ - p_)) {
        ok_ = false;
        return nullptr;
    }
    const uint8_t* q = p_;
    p_ += (size_t)r;
    return q;
}

uint32_t Reader::u32() {
    uint32_t v = 0;
    if (const uint8_t* p = take(4)) memcpy(&v, p, 4);
    return v;
}
int32_t Reader::i32() { return (int32_t)u32(); }
uint64_t Reader::u64() {
    uint64_t v = 0;
    if (const uint8_t* p = take(8)) memcpy(&v, p, 8);
    return v;
}
int64_t Reader::i64() { return (int64_t)u64(); }
float Reader::f32() {
    float v = 0.0f;
    if (const uint8_t* p = take(4)) memcpy(&v, p, 4);
    return v;
}
double Reader::f64() {
    double v = 0.0;
    if (const uint8_t* p = take(8)) memcpy(&v, p, 8);
    return v;
}
bool Reader::b() { return u32() != 0; }

const char* Reader::cstr(uint32_t* len) {
    if (len) *len = 0;
    const uint32_t n = u32();
    if (!ok_ || n == kNullLength) return nullptr;
    const uint8_t* p = take(n + 1u);  // n <= 0xFFFFFFFE here, no overflow
    if (!p) return nullptr;
    if (p[n] != 0) {  // missing terminator: malformed
        ok_ = false;
        return nullptr;
    }
    if (len) *len = n;
    return reinterpret_cast<const char*>(p);
}

const wchar_t* Reader::wcstr(uint32_t* units) {
    if (units) *units = 0;
    const uint32_t n = u32();
    if (!ok_ || n == kNullLength) return nullptr;
    if (n > remaining() / 2u) {  // also guards n * 2 + 2 against overflow
        ok_ = false;
        return nullptr;
    }
    const uint8_t* p = take(n * 2u + 2u);
    if (!p) return nullptr;
    if (p[n * 2u] != 0 || p[n * 2u + 1u] != 0) {
        ok_ = false;
        return nullptr;
    }
    if (units) *units = n;
    return reinterpret_cast<const wchar_t*>(p);
}

const uint8_t* Reader::bytes(uint32_t* len) {
    if (len) *len = 0;
    const uint32_t n = u32();
    if (!ok_) return nullptr;
    const uint8_t* p = take(n);
    if (!p) return nullptr;
    if (len) *len = n;
    return p;
}

Handle Reader::handle() {
    Handle h = {0, 0};
    if (const uint8_t* p = take(8)) {
        memcpy(&h.type, p, 4);
        memcpy(&h.id, p + 4, 4);
    }
    return h;
}

}  // namespace vpipc
