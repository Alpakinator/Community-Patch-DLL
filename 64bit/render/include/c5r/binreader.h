// c5r/binreader.h - bounds-checked little-endian reader over a byte span (epic 2900).
// Every read checks the remaining size; a failed read latches the reader into the failed state and returns 0, so
// a parser can read a whole header and check ok() once. Use need() to report Truncated with context.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include "c5r/status.h"

namespace c5r {

class BinReader {
public:
    BinReader(const void* data, size_t size) : p_(static_cast<const uint8_t*>(data)), n_(size) {}
    size_t size() const { return n_; }
    size_t pos() const { return pos_; }
    size_t remaining() const { return pos_ <= n_ ? n_ - pos_ : 0; }
    bool ok() const { return ok_; }
    const uint8_t* data() const { return p_; }
    const uint8_t* cur() const { return p_ + pos_; }

    bool seek(size_t at) { if (at > n_) { ok_ = false; return false; } pos_ = at; return true; }
    bool skip(size_t k) { if (k > remaining()) { ok_ = false; return false; } pos_ += k; return true; }
    bool has(size_t k) const { return ok_ && k <= remaining(); }

    uint8_t u8() { uint8_t v = 0; raw(&v, 1); return v; }
    uint16_t u16() { uint16_t v = 0; raw(&v, 2); return v; }
    uint32_t u32() { uint32_t v = 0; raw(&v, 4); return v; }
    uint64_t u64() { uint64_t v = 0; raw(&v, 8); return v; }
    float f32() { float v = 0; raw(&v, 4); return v; }
    bool raw(void* out, size_t k) {
        if (!has(k)) { ok_ = false; std::memset(out, 0, k); return false; }
        std::memcpy(out, p_ + pos_, k); pos_ += k; return true;
    }
    std::string str(size_t k) {
        if (!has(k)) { ok_ = false; return std::string(); }
        std::string s(reinterpret_cast<const char*>(p_ + pos_), k); pos_ += k; return s;
    }
    // Truncated status with context if fewer than k bytes remain (or the reader already failed).
    Status need(size_t k, const char* what) const {
        if (has(k)) return Status::Ok();
        return Status::fail(Err::Truncated, std::string(what) + ": need " + std::to_string(k) + " bytes at " +
                            std::to_string(pos_) + ", have " + std::to_string(remaining()));
    }

private:
    const uint8_t* p_;
    size_t n_;
    size_t pos_ = 0;
    bool ok_ = true;
};

} // namespace c5r
