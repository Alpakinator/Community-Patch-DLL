// vpipc/marshal.h - Writer / Reader for call and post payloads (PROTOCOL.md section 5).
// Declarations by the epic 1200 coordinator; implementation and ownership: task 1201.
// Public signatures are a contract with tasks 1203-1205 and the proxy generator: change them only with the
// coordinator's agreement. Hot-path bodies may be made inline in this header.
// Core rule (PROTOCOL.md section 11): C++11 language, no C++ standard library types. STL helpers: vpipc/stl.h.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace vpipc {

// An object the other side owns (player, unit, city, plot, wrapper): never a pointer.
struct Handle {
    uint32_t type;
    int32_t id;
};

// Encoding: little-endian, every value 4-byte aligned, no type tags.
//   i32/u32/enum/bool -> 4 bytes; i64/u64/f64 -> 8 bytes (4-aligned); f32 -> 4 bytes
//   str  -> u32 length (kNullLength = null), bytes, NUL, pad to 4
//   wstr -> u32 UTF-16 units (kNullLength = null), units, NUL unit, pad to 4
//   bytes-> u32 length, bytes, pad to 4
//   handle -> u32 type, i32 id
class Writer {
public:
    Writer() {}
    ~Writer();

    // Start writing into a fixed buffer (the channel data area or a post scratch buffer). When a write would
    // pass `cap`, the writer moves what it has into a malloc'd spill buffer it owns and continues there
    // (spilled() == true). If allowSpill is false it sets overflowed() instead and drops further writes.
    // reset() keeps an existing spill allocation for reuse.
    void reset(uint8_t* buf, uint32_t cap, bool allowSpill);

    void i32(int32_t v);
    void u32(uint32_t v);
    void i64(int64_t v);
    void u64(uint64_t v);
    void f32(float v);
    void f64(double v);
    void b(bool v);
    template <class E> void e(E v) { i32(static_cast<int32_t>(v)); }
    void str(const char* s);                // null allowed (encoded as null, distinct from "")
    void str(const char* s, uint32_t len);  // s may be null only when len == 0 (encodes "")
    void wstr(const wchar_t* s);            // null allowed
    void wstr(const wchar_t* s, uint32_t units);
    void bytes(const void* p, uint32_t len);
    void handle(Handle h);
    uint8_t* reserve(uint32_t n);           // raw space, rounded up to 4; caller fills it; nullptr if overflowed

    uint32_t size() const { return len_; }
    const uint8_t* data() const;            // start of the written payload (buffer or spill)
    bool spilled() const { return spilled_; }
    bool overflowed() const { return overflowed_; }

private:
    Writer(const Writer&);                  // not copyable (C++03 style: VC9-safe)
    Writer& operator=(const Writer&);
    uint8_t* grow(uint32_t n);              // returns where to write n bytes (4-aligned length)
    void put4(const void* v);               // inline fast path for the 4-byte encodings
    uint8_t* buf_ = nullptr;
    uint32_t cap_ = 0;
    uint32_t len_ = 0;
    bool allowSpill_ = false;
    bool spilled_ = false;
    bool overflowed_ = false;
    uint8_t* spill_ = nullptr;              // malloc'd
    uint32_t spillCap_ = 0;
};

// Inline fast path (task 1201): a 4-byte value that fits in the fixed buffer is stored without a call.
inline void Writer::put4(const void* v) {
    uint8_t* p;
    if (!spilled_ && !overflowed_ && cap_ - len_ >= 4u) {
        p = buf_ + len_;
        len_ += 4u;
    } else if (!(p = grow(4u))) {
        return;
    }
    memcpy(p, v, 4);
}
inline void Writer::i32(int32_t v) { put4(&v); }
inline void Writer::u32(uint32_t v) { put4(&v); }
inline void Writer::f32(float v) { put4(&v); }
inline void Writer::b(bool v) { const uint32_t u = v ? 1u : 0u; put4(&u); }

class Reader {
public:
    Reader() {}
    Reader(const uint8_t* p, uint32_t len) { reset(p, len); }
    void reset(const uint8_t* p, uint32_t len);

    int32_t i32();
    uint32_t u32();
    int64_t i64();
    uint64_t u64();
    float f32();
    double f64();
    bool b();
    template <class E> E e() { return static_cast<E>(i32()); }
    const char* cstr(uint32_t* len = nullptr);       // points into the payload, NUL-terminated; null -> nullptr
    const wchar_t* wcstr(uint32_t* units = nullptr); // same for UTF-16
    const uint8_t* bytes(uint32_t* len);             // points into the payload
    Handle handle();

    bool ok() const { return ok_; }                  // false after any read past the end or malformed length
    uint32_t remaining() const { return (uint32_t)(end_ - p_); }
    const uint8_t* cursor() const { return p_; }

private:
    const uint8_t* take(uint32_t n);                 // n rounded up to 4; nullptr (and ok_ = false) if short
    const uint8_t* p_ = nullptr;
    const uint8_t* end_ = nullptr;
    bool ok_ = true;
};

}  // namespace vpipc
