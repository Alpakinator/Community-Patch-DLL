// c5r/status.h - error reporting shared by every render/ module (epic 2900).
// Parsers never throw and never crash on bad input: they return a Status whose code says which check failed, so
// that each negative test can assert the exact reason.
#pragma once
#include <string>

namespace c5r {

enum class Err {
    Ok = 0,
    Io,          // file could not be opened or read
    Truncated,   // data ends before a structure it announces
    BadMagic,    // magic / signature mismatch
    BadVersion,  // known magic, unsupported version
    BadLayout,   // fields inconsistent with each other (offset past end, overlapping, count too large, ...)
    Unsupported, // well-formed but outside what this code handles (e.g. a DDS pixel format)
    NotFound,    // a lookup by name or id found nothing
    Device,      // a D3D11 / DXGI / WIC call failed
    OutOfMemory, // E_OUTOFMEMORY / DXGI_ERROR_DEVICE_... memory refusals, and allocation failures (3400, 2026-11-13)
};

const char* errName(Err e);

struct Status {
    Err code = Err::Ok;
    std::string msg;
    bool ok() const { return code == Err::Ok; }
    explicit operator bool() const { return ok(); }
    static Status Ok() { return Status(); }
    static Status fail(Err c, std::string m) { Status s; s.code = c; s.msg = std::move(m); return s; }
    std::string str() const { return std::string(errName(code)) + (msg.empty() ? "" : ": " + msg); }
};

#define C5R_TRY(expr) do { ::c5r::Status c5r_s_ = (expr); if (!c5r_s_.ok()) return c5r_s_; } while (0)

} // namespace c5r
