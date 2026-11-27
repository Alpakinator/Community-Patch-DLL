// ipc/testapps/vpipc_test_server.cpp - the robustness test server (task 1204), built x64 and x86.
//
//   vpipc_test_server --ipc <section> [--attach-timeout ms] [--fake-version] [--fake-layout] [--schema hex]
//                     [--tick ms] [--hung-after ms]
//
// Attaches to the client's section, serves every channel on its own thread, and exits with:
//   0 client closed, 10 client died, 11 refused (version / layout / schema), 12 attach timeout,
//   13 any other error, 14 serve() did not return within 3 s of the client's death, 99 fault injection.
// stdout is the log (the client redirects it to a file).
#include "testapp_common.h"

#include <tlhelp32.h>

#include <vector>

namespace vt {
LogState g_log;
}

using namespace vt;
using vpipc::Channel;
using vpipc::Endpoint;
using vpipc::Reader;
using vpipc::Status;
using vpipc::Writer;

namespace {

struct ServerChan {
    uint32_t index;
    volatile LONG notesIn;
    volatile LONG noteErrors;
    uint32_t nextNote;       // next outgoing P_Note sequence number
    uint32_t expectNoteIn;   // next expected incoming P_Note sequence number
};

ServerChan g_chan[vpipc::kMaxChannels];
Endpoint* g_ep = nullptr;
volatile LONG g_exitCode = -1;       // first serving thread to finish decides
volatile LONG g_peerDiedSeen = 0;
HANDLE g_doneEvent = nullptr;        // set when a serving thread ends

void hook_log(void*, vpipc::LogLevel level, const char* message) {
    if (level == vpipc::LogLevel::Debug) return;
    logf("lib %s: %s", level_name(level), message);
}

void hook_event(void*, vpipc::PeerEvent ev, uint32_t generation) {
    logf("peer event %s (generation %u)", event_name(ev), generation);
    if (ev == vpipc::PeerEvent::Died) InterlockedExchange(&g_peerDiedSeen, 1);
}

int exit_code_for(Status s) {
    switch (s) {
    case Status::PeerClosed: return kExitClientClosed;
    case Status::PeerDied: return kExitClientDied;
    case Status::VersionMismatch:
    case Status::LayoutMismatch:
    case Status::SchemaMismatch: return kExitRefused;
    case Status::Timeout: return kExitTimeout;
    default: return kExitOtherError;
    }
}

DWORD WINAPI crash_after_thread(void* arg) {
    Sleep((DWORD)(uintptr_t)arg);
    logf("fault injection: TerminateProcess(self, %d) now", kExitSelfCrash);
    TerminateProcess(GetCurrentProcess(), kExitSelfCrash);
    return 0;
}

// Hang: suspend every other thread of this process (the monitor thread stops writing heartbeats, the other
// serving threads stop), sleep, resume. Nothing between suspend and resume touches the heap or the CRT.
void hang_process(uint32_t ms) {
    const DWORD self = GetCurrentThreadId();
    const DWORD pid = GetCurrentProcessId();
    HANDLE threads[256];
    int n = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te;
        te.dwSize = sizeof(te);
        if (Thread32First(snap, &te)) {
            do {
                if (te.th32OwnerProcessID == pid && te.th32ThreadID != self && n < 256) {
                    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                    if (h) threads[n++] = h;
                }
                te.dwSize = sizeof(te);
            } while (Thread32Next(snap, &te));
        }
        CloseHandle(snap);
    }
    logf("fault injection: hang %u ms, suspending %d other threads", ms, n);
    for (int i = 0; i < n; ++i) SuspendThread(threads[i]);
    Sleep(ms);
    for (int i = 0; i < n; ++i) { ResumeThread(threads[i]); CloseHandle(threads[i]); }
    logf("fault injection: hang over, threads resumed");
}

void note_status(const char* what, Status s) {
    if (s != Status::Ok) logf("%s -> %s", what, vpipc::status_name(s));
}

void on_call(void* ctx, Channel& ch, uint32_t method, Reader& a, Writer& ret) {
    ServerChan* sc = (ServerChan*)ctx;
    switch (method) {
    case M_Info: {
        ret.u32(ch.index());
        ret.u32((uint32_t)(sizeof(void*) * 8));
        ret.u32(GetCurrentProcessId());
        break;
    }
    case M_EchoAll: {
        // Decode everything first (the RETURN may share the data area with the arguments).
        int32_t i = a.i32();
        uint32_t u = a.u32();
        int64_t i6 = a.i64();
        uint64_t u6 = a.u64();
        float f = a.f32();
        double d = a.f64();
        bool b0 = a.b();
        bool b1 = a.b();
        int32_t e = a.i32();
        uint32_t slen = 0, nlen = 0, elen = 0, wlen = 0, wnlen = 0, blen = 0;
        const char* s = a.cstr(&slen);
        std::vector<char> sc_(s ? slen + 1 : 1);
        if (s) memcpy(&sc_[0], s, slen + 1);
        const char* sn = a.cstr(&nlen);
        bool snNull = (sn == nullptr);
        const char* se = a.cstr(&elen);
        bool seNull = (se == nullptr);
        const wchar_t* w = a.wcstr(&wlen);
        std::vector<wchar_t> wc_(w ? wlen + 1 : 1);
        if (w) memcpy(&wc_[0], w, (wlen + 1) * sizeof(wchar_t));
        const wchar_t* wn = a.wcstr(&wnlen);
        bool wnNull = (wn == nullptr);
        const uint8_t* bp = a.bytes(&blen);
        std::vector<uint8_t> bc_(blen + 1);
        if (blen) memcpy(&bc_[0], bp, blen);
        vpipc::Handle h = a.handle();
        bool ok = a.ok() && a.remaining() == 0;
        ret.b(ok);
        ret.i32(i);
        ret.u32(u);
        ret.i64(i6);
        ret.u64(u6);
        ret.f32(f);
        ret.f64(d);
        ret.b(b0);
        ret.b(b1);
        ret.i32(e);
        if (s) ret.str(&sc_[0], slen); else ret.str((const char*)nullptr);
        if (snNull) ret.str((const char*)nullptr); else ret.str("?", 1);
        if (seNull) ret.str((const char*)nullptr); else ret.str("", 0);
        if (w) ret.wstr(&wc_[0], wlen); else ret.wstr((const wchar_t*)nullptr);
        if (wnNull) ret.wstr((const wchar_t*)nullptr); else ret.wstr(L"?", 1);
        ret.bytes(blen ? &bc_[0] : nullptr, blen);
        ret.handle(h);
        break;
    }
    case M_EchoBytes: {
        uint32_t n = 0;
        const uint8_t* p = a.bytes(&n);
        if (!a.ok()) { ch.set_return_status(vpipc::kRetHandlerError); ret.str("bad args"); break; }
        std::vector<uint8_t> copy(n + 1);
        if (n) memcpy(&copy[0], p, n);
        ret.bytes(n ? &copy[0] : nullptr, n);
        break;
    }
    case M_Add: {
        int32_t x = a.i32(), y = a.i32(), z = a.i32();
        ret.i32(x + y + z);
        break;
    }
    case M_Callbacks: {
        uint32_t n = a.u32();
        uint32_t good = 0;
        for (uint32_t k = 0; k < n; ++k) {
            Writer& w = ch.begin_call(CB_Add);
            w.i32((int32_t)k);
            w.i32(7);
            Reader r;
            Status s = ch.end_call(r);
            if (s != Status::Ok) { note_status("CB_Add", s); break; }
            if (r.i32() == (int32_t)k + 7 && r.ok()) ++good;
        }
        ret.u32(good);
        break;
    }
    case M_Nest: {
        uint32_t depth = a.u32();
        uint32_t result = 0;
        if (depth > 0) {
            Writer& w = ch.begin_call(CB_Nest);
            w.u32(depth - 1);
            Reader r;
            Status s = ch.end_call(r);
            if (s != Status::Ok) {
                note_status("CB_Nest", s);
                ch.set_return_status(vpipc::kRetHandlerError);
                ret.str("nested CB_Nest failed");
                break;
            }
            result = r.u32() + 1;
        }
        ret.u32(result);
        break;
    }
    case M_PostN: {
        uint32_t n = a.u32();
        uint32_t sent = 0;
        for (uint32_t k = 0; k < n; ++k) {
            Writer& w = ch.begin_post(P_Note);
            w.u32(sc->nextNote);
            Status s = ch.end_post();
            if (s != Status::Ok) { note_status("post P_Note", s); break; }
            ++sc->nextNote;
            ++sent;
        }
        ret.u32(sent);
        break;
    }
    case M_PostCount: {
        ret.u32((uint32_t)sc->notesIn);
        ret.u32((uint32_t)sc->noteErrors);
        break;
    }
    case M_Hash: {
        uint32_t n = 0;
        const uint8_t* p = a.bytes(&n);
        uint64_t h = a.ok() ? hash64(p, n) : 0;
        ret.u64(h);
        ret.u32(n);
        break;
    }
    case M_Generate: {
        uint32_t n = a.u32(), seed = a.u32();
        ret.u32(n);
        uint8_t* p = ret.reserve(n);
        if (p) fill_pattern(p, n, seed);
        break;
    }
    case M_Fail: {
        ch.set_return_status(vpipc::kRetHandlerError);
        ret.str("M_Fail: deliberate handler error");
        break;
    }
    case M_Crash: {
        logf("fault injection: TerminateProcess(self, %d) inside the handler", kExitSelfCrash);
        TerminateProcess(GetCurrentProcess(), kExitSelfCrash);
        break;
    }
    case M_CrashAfter: {
        uint32_t ms = a.u32();
        HANDLE t = CreateThread(nullptr, 0, crash_after_thread, (void*)(uintptr_t)ms, 0, nullptr);
        if (t) CloseHandle(t);
        ret.u32(ms);
        break;
    }
    case M_NestedThenCrash: {
        uint32_t crashMs = a.u32(), clientSleepMs = a.u32();
        HANDLE t = CreateThread(nullptr, 0, crash_after_thread, (void*)(uintptr_t)crashMs, 0, nullptr);
        if (t) CloseHandle(t);
        Writer& w = ch.begin_call(CB_Sleep);
        w.u32(clientSleepMs);
        Reader r;
        Status s = ch.end_call(r);  // the process dies during this wait
        note_status("CB_Sleep", s);
        ret.u32(0);
        break;
    }
    case M_Hang: {
        uint32_t ms = a.u32();
        hang_process(ms);
        ret.u32(ms);
        break;
    }
    case M_NestDieClient: {
        double t0 = qpc_ms();
        Writer& w = ch.begin_call(CB_Die);
        w.u32(0);
        Reader r;
        Status s = ch.end_call(r);
        logf("CB_Die returned %s after %.1f ms", vpipc::status_name(s), qpc_ms() - t0);
        ret.u32((uint32_t)s);
        break;
    }
    case M_Sleep: {
        uint32_t ms = a.u32();
        Sleep(ms);
        ret.u32(ms);
        break;
    }
    default:
        ch.set_return_status(vpipc::kRetUnknownMethod);
        ret.str("unknown method");
        break;
    }
}

void on_post(void* ctx, Channel&, uint32_t method, Reader& a) {
    ServerChan* sc = (ServerChan*)ctx;
    if (method != P_Note) { InterlockedIncrement(&sc->noteErrors); return; }
    uint32_t seq = a.u32();
    if (!a.ok() || seq != sc->expectNoteIn) InterlockedIncrement(&sc->noteErrors);
    sc->expectNoteIn = seq + 1;
    InterlockedIncrement(&sc->notesIn);
}

DWORD WINAPI serve_thread(void* arg) {
    uint32_t i = (uint32_t)(uintptr_t)arg;
    Channel& ch = g_ep->channel(i);
    Status s;
    uint64_t calls = 0;
    for (;;) {
        s = ch.serve(INFINITE);
        if (s != Status::Ok) break;
        ++calls;
    }
    logf("channel %u: serve() -> %s after %llu top-level calls", i, vpipc::status_name(s),
         (unsigned long long)calls);
    InterlockedCompareExchange(&g_exitCode, exit_code_for(s), -1);
    SetEvent(g_doneEvent);
    return 0;
}

bool parse_u32(const char* s, uint32_t& out) {
    char* end = nullptr;
    unsigned long v = strtoul(s, &end, 0);
    if (!end || *end) return false;
    out = (uint32_t)v;
    return true;
}

// After the client's death: serve() must return within 3 s, or the process exits 14 (a finding, not a hang).
DWORD WINAPI death_watch_thread(void*) {
    while (!g_peerDiedSeen) Sleep(10);
    Sleep(3000);
    logf("serve() did not return within 3 s of the Died event: exit %d", kExitServeStuck);
    fflush(stdout);
    TerminateProcess(GetCurrentProcess(), kExitServeStuck);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    log_init("server");
    const char* section = nullptr;
    uint32_t attachTimeout = 10000, tickMs = 0, hungAfter = 0;
    bool fakeVersion = false, fakeLayout = false;
    uint64_t schema = kSchemaHash;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        bool more = i + 1 < argc;
        if (!strcmp(a, "--ipc") && more) section = argv[++i];
        else if (!strcmp(a, "--attach-timeout") && more) { if (!parse_u32(argv[++i], attachTimeout)) return kExitUsage; }
        else if (!strcmp(a, "--tick") && more) { if (!parse_u32(argv[++i], tickMs)) return kExitUsage; }
        else if (!strcmp(a, "--hung-after") && more) { if (!parse_u32(argv[++i], hungAfter)) return kExitUsage; }
        else if (!strcmp(a, "--fake-version")) fakeVersion = true;
        else if (!strcmp(a, "--fake-layout")) fakeLayout = true;
        else if (!strcmp(a, "--schema") && more) schema = _strtoui64(argv[++i], nullptr, 16);
        else {
            fprintf(stderr, "usage: vpipc_test_server --ipc <section> [--attach-timeout ms] [--fake-version]\n"
                            "       [--fake-layout] [--schema hex] [--tick ms] [--hung-after ms]\n");
            return kExitUsage;
        }
    }
    if (!section) { fprintf(stderr, "--ipc is required\n"); return kExitUsage; }

    logf("pid %lu, %u-bit, section %s, attach timeout %u ms%s%s, schema %016llx",
         (unsigned long)GetCurrentProcessId(), (unsigned)(sizeof(void*) * 8), section, attachTimeout,
         fakeVersion ? ", FAKE VERSION" : "", fakeLayout ? ", FAKE LAYOUT" : "", (unsigned long long)schema);

    vpipc::EndpointConfig cfg;
    set_str(cfg.sectionName, section);
    cfg.appSchemaHash = schema;
    cfg.onLog = hook_log;
    cfg.onPeerEvent = hook_event;
    if (tickMs) cfg.wait.tickMs = tickMs;
    if (hungAfter) cfg.wait.hungAfterMs = hungAfter;
    if (fakeVersion || fakeLayout) {
        cfg.test.enabled = true;
        if (fakeVersion) cfg.test.versionMinor = (uint16_t)(vpipc::kVersionMinor + 1);
        if (fakeLayout) cfg.test.layoutHash = vpipc::kLayoutHash ^ 0x5A5A5A5Au;
    }

    double t0 = qpc_ms();
    Endpoint* ep = nullptr;
    Status s = Endpoint::attach_server(cfg, &ep, attachTimeout);
    if (s != Status::Ok) {
        int code = exit_code_for(s);
        const char* text = Endpoint::last_error_text();
        logf("attach_server -> %s after %.1f ms; last_error_text: %s", vpipc::status_name(s), qpc_ms() - t0,
             text ? text : "(null)");
        // One line the client's scenario 7 reads back from this log.
        if (code == kExitRefused) {
            char line[1100];
            sanitize_line(text, line, sizeof(line));
            printf("REFUSAL %s\n", line);
            fflush(stdout);
        }
        logf("exit %d", code);
        delete ep;  // nullptr on failure
        return code;
    }
    g_ep = ep;
    uint32_t n = ep->channel_count();
    logf("attached after %.1f ms: generation %u, %u channels, client pid %u", qpc_ms() - t0, ep->generation(), n,
         ep->peer_pid());

    g_doneEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    HANDLE watch = CreateThread(nullptr, 0, death_watch_thread, nullptr, 0, nullptr);
    if (watch) CloseHandle(watch);
    for (uint32_t i = 0; i < n; ++i) {
        g_chan[i].index = i;
        ep->channel(i).set_handlers(on_call, on_post, &g_chan[i]);
    }
    for (uint32_t i = 0; i < n; ++i) {
        HANDLE t = CreateThread(nullptr, 0, serve_thread, (void*)(uintptr_t)i, 0, nullptr);
        if (!t) { logf("CreateThread failed: %lu", GetLastError()); return kExitOtherError; }
        CloseHandle(t);
    }
    // The first serving thread that ends decides the exit code. The others are blocked in serve() on the
    // same dead or closed peer; the process ends without unwinding them (as the real server will).
    WaitForSingleObject(g_doneEvent, INFINITE);
    int code = (int)g_exitCode;
    logf("exit %d", code);
    fflush(stdout);
    TerminateProcess(GetCurrentProcess(), (UINT)code);
    return code;
}
