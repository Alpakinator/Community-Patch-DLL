// tests/support/test_lifecycle/proc_util.h - spawn this test executable as a child, kill it, pass results back
// through a temp file, time things. Header-only; used by test_lifecycle and test_wait. Tests may use C++03 STL.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

namespace tu {

// Every child goes into a kill-on-close job: nothing outlives the test executable.
inline HANDLE job() {
    static HANDLE j = NULL;
    if (!j) {
        j = CreateJobObjectA(NULL, NULL);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
        memset(&li, 0, sizeof(li));
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (j) SetInformationJobObject(j, JobObjectExtendedLimitInformation, &li, sizeof(li));
    }
    return j;
}

struct Child {
    HANDLE process;
    DWORD pid;
    Child() : process(NULL), pid(0) {}
};

inline Child spawn_self(const std::string& args) {
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    std::string cmd = std::string("\"") + exe + "\" " + args;
    std::vector<char> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    Child c;
    if (!CreateProcessA(exe, &buf[0], NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        printf("  CreateProcess failed: %u\n", (unsigned)GetLastError());
        return c;
    }
    AssignProcessToJobObject(job(), pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    c.process = pi.hProcess;
    c.pid = pi.dwProcessId;
    return c;
}

inline void kill(Child& c) {
    if (c.process) {
        TerminateProcess(c.process, 9);
        WaitForSingleObject(c.process, 5000);
    }
}

// Waits up to ms for the child to exit; returns its exit code, or -1 on timeout (the child is then killed).
inline int join(Child& c, DWORD ms) {
    if (!c.process) return -2;
    int code = -1;
    if (WaitForSingleObject(c.process, ms) == WAIT_OBJECT_0) {
        DWORD x = 0;
        GetExitCodeProcess(c.process, &x);
        code = (int)x;
    } else {
        kill(c);
    }
    CloseHandle(c.process);
    c.process = NULL;
    return code;
}

inline std::string temp_file(const char* what) {
    static LONG n = 0;
    char dir[MAX_PATH];
    GetTempPathA(MAX_PATH, dir);
    char name[MAX_PATH + 64];
    sprintf(name, "%svpipc_%s_%u_%u.txt", dir, what, (unsigned)GetCurrentProcessId(), (unsigned)InterlockedIncrement(&n));
    DeleteFileA(name);
    return name;
}

inline std::string read_file(const std::string& path) {
    std::string s;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return s;
    char b[4096];
    size_t k;
    while ((k = fread(b, 1, sizeof(b), f)) > 0) s.append(b, k);
    fclose(f);
    return s;
}

// "key=value" lines.
inline std::string kv(const std::string& text, const std::string& key) {
    std::string k = key + "=";
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        if (text.compare(p, k.size(), k) == 0) return text.substr(p + k.size(), e - p - k.size());
        p = e + 1;
    }
    return std::string();
}

inline void append_kv(const std::string& path, const std::string& key, const std::string& value) {
    FILE* f = fopen(path.c_str(), "ab");
    if (!f) return;
    fprintf(f, "%s=%s\n", key.c_str(), value.c_str());
    fclose(f);
}

inline bool contains(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }

inline double qpc_us() {
    LARGE_INTEGER c, f;
    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    return (double)c.QuadPart * 1e6 / (double)f.QuadPart;
}

// Polls cond every millisecond for up to ms. Returns whether it became true.
template <class F> bool wait_until(F cond, DWORD ms) {
    double end = qpc_us() + ms * 1000.0;
    for (;;) {
        if (cond()) return true;
        if (qpc_us() > end) return false;
        Sleep(1);
    }
}

inline double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    size_t i = (size_t)(p * (double)(v.size() - 1) + 0.5);
    return v[i];
}

struct Thread {
    HANDLE h;
    Thread() : h(NULL) {}
    template <class T> void start(DWORD(WINAPI* fn)(void*), T* arg) { h = CreateThread(NULL, 0, fn, arg, 0, NULL); }
    bool join(DWORD ms) {
        if (!h) return false;
        bool ok = WaitForSingleObject(h, ms) == WAIT_OBJECT_0;
        if (ok) {
            CloseHandle(h);
            h = NULL;
        }
        return ok;
    }
};

}  // namespace tu
