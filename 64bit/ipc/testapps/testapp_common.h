// ipc/testapps/testapp_common.h - shared by vpipc_test_client (x86) and vpipc_test_server (x64, x86).
// Task 1204. Method ids, the test schema hash, payload generators and hashes, time and log helpers.
// Toolchain rule (coordinator, 2026-11-10): C++14 language, VC9 library (msvcr90) in the x86 tree:
// no C++11 library headers, no thread_local, no function-local statics with dynamic initialisers.
#pragma once
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vpipc/vpipc.h"

namespace vt {

// ---- the test application's "schema" ------------------------------------------------------------------
const uint64_t kSchemaHash = 0x7E57A11C0DE01204ull;   // both test apps; --schema overrides on the server
const uint64_t kWrongSchema = 0x0BADC0DE0BADC0DEull;

// Calls the client makes (client -> server).
enum ServerMethod : uint32_t {
    M_Info = 1,              // -> u32 channel index, u32 pointer bits, u32 pid
    M_EchoAll = 2,           // every marshalled type, echoed in order
    M_EchoBytes = 3,         // bytes -> the same bytes
    M_Add = 4,               // i32 a, i32 b, i32 c -> i32 a + b + c
    M_Callbacks = 5,         // u32 n -> server makes n nested CB_Add calls, returns the number verified
    M_Nest = 6,              // u32 depth -> alternating nesting with CB_Nest, returns depth
    M_PostN = 7,             // u32 n -> server posts n P_Note to the client, then returns n
    M_PostCount = 8,         // -> u32 notes received on this channel, u32 order errors
    M_Hash = 9,              // bytes -> u64 hash, u32 length
    M_Generate = 10,         // u32 length, u32 seed -> bytes
    M_Fail = 11,             // -> RETURN status kRetHandlerError with a message
    M_Crash = 12,            // TerminateProcess(self) inside the handler
    M_CrashAfter = 13,       // u32 ms -> returns at once; a thread terminates the process after ms
    M_NestedThenCrash = 14,  // u32 crashAfterMs, u32 clientSleepMs -> CB_Sleep into the client, dies meanwhile
    M_Hang = 15,             // u32 ms -> suspend every other server thread (heartbeat stops), sleep, resume
    M_NestDieClient = 16,    // -> CB_Die into the client (the client terminates itself in the handler)
    M_Sleep = 17,            // u32 ms -> sleeps in the handler, returns ms
    M_Unknown = 999,         // no handler: RETURN status kRetUnknownMethod
};

// Calls the server makes into the client (nested, inside one of the above).
enum ClientMethod : uint32_t {
    CB_Add = 101,    // i32 a, i32 b -> a + b
    CB_Nest = 102,   // u32 depth -> calls M_Nest(depth - 1) if depth > 0, returns result + 1
    CB_Sleep = 103,  // u32 ms -> sleeps, returns ms
    CB_Die = 104,    // client terminates itself
};

// Posts, both directions.
enum PostMethod : uint32_t {
    P_Note = 201,    // u32 per-channel sequence number
};

// Exit codes of vpipc_test_server.
enum ServerExit {
    kExitClientClosed = 0,
    kExitUsage = 2,
    kExitClientDied = 10,
    kExitRefused = 11,
    kExitTimeout = 12,
    kExitOtherError = 13,
    kExitServeStuck = 14,     // the client died but serve() did not return within 3 s
    kExitSelfCrash = 99,      // fault injection: TerminateProcess on itself
};

// ---- payload generator and hash -------------------------------------------------------------------------
inline void fill_pattern(uint8_t* p, uint32_t n, uint32_t seed) {
    uint32_t x = seed ? seed : 0x9E3779B9u;
    uint32_t i = 0;
    for (; i + 4 <= n; i += 4) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        memcpy(p + i, &x, 4);
    }
    for (; i < n; ++i) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; p[i] = (uint8_t)x; }
}

inline uint64_t hash64(const uint8_t* p, uint32_t n) {  // FNV-1a 64
    uint64_t h = 1469598103934665603ull;
    for (uint32_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

// ---- time ------------------------------------------------------------------------------------------------
inline double qpc_ms() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

// ---- logging (stdout, flushed per line, serialised across threads) ----------------------------------------
struct LogState {
    SRWLOCK lock;
    double t0;
    const char* who;
};
extern LogState g_log;

inline void log_init(const char* who) {
    InitializeSRWLock(&g_log.lock);
    g_log.t0 = qpc_ms();
    g_log.who = who;
}

inline void logf(const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    if (n < 0) n = (int)strlen(buf);
    AcquireSRWLockExclusive(&g_log.lock);
    printf("[%9.3f %s %lu] %s\n", qpc_ms() - g_log.t0, g_log.who, (unsigned long)GetCurrentThreadId(), buf);
    fflush(stdout);
    ReleaseSRWLockExclusive(&g_log.lock);
}

// Raw line, no prefix (RESULT / METRIC / TEXT lines the script parses).
inline void outf(const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    if (n < 0) n = (int)strlen(buf);
    AcquireSRWLockExclusive(&g_log.lock);
    printf("%s\n", buf);
    fflush(stdout);
    ReleaseSRWLockExclusive(&g_log.lock);
}

inline const char* level_name(vpipc::LogLevel l) {
    switch (l) {
    case vpipc::LogLevel::Debug: return "debug";
    case vpipc::LogLevel::Info: return "info";
    case vpipc::LogLevel::Warn: return "warn";
    case vpipc::LogLevel::Error: return "error";
    }
    return "?";
}

inline const char* event_name(vpipc::PeerEvent e) {
    switch (e) {
    case vpipc::PeerEvent::Attached: return "Attached";
    case vpipc::PeerEvent::Died: return "Died";
    case vpipc::PeerEvent::Closed: return "Closed";
    case vpipc::PeerEvent::Hung: return "Hung";
    case vpipc::PeerEvent::Recovered: return "Recovered";
    case vpipc::PeerEvent::Refused: return "Refused";
    }
    return "?";
}

// Copy into a fixed char array (EndpointConfig / ChannelConfig names are fixed arrays).
template <size_t N> inline void set_str(char (&dst)[N], const char* src) {
    strncpy(dst, src, N - 1);
    dst[N - 1] = 0;
}

// Visible text: printable ASCII only, so a refusal text can go on one report line.
inline void sanitize_line(const char* in, char* out, size_t cap) {
    size_t j = 0;
    for (size_t i = 0; in && in[i] && j + 1 < cap; ++i) {
        unsigned char c = (unsigned char)in[i];
        out[j++] = (c >= 32 && c < 127) ? (char)c : ' ';
    }
    out[j] = 0;
}

}  // namespace vt
