// proxy_crash_filter.h - the proxy's chained top-level exception filter (ticket 0419, D118 for an EXE crash, part 2).
//
// The proxy runs inside the EXE. When the EXE (or anything else in its process) faults and nothing handles it, the
// filter installed here writes <run folder>\exe-crash.txt: the exception code, the faulting module and RVA (and, in the
// EXE, the VA at the EXE file's ImageBase), the access address of an access violation, and the thread id. Nothing else: no code
// bytes, no stack, no memory of the process (D83, D119's privacy rule). The 64-bit server reads that file after the EXE
// has ended and quotes its first line in its D119 report (stage exe-crash, ticket 0418,
// research/03-dll-engine-boundary/error-report-exe-crash.md section 4 and 10).
//
// The filter chains: it hands every exception on to the filter it replaced (Goldberg's crash printer on a test
// instance, the EXE's own filter that writes Steam's minidump) and returns that filter's answer, so the process ends as
// it did before. It records once per process; a second fault (another thread, or one inside the filter) goes straight
// to the previous filter. In the filter nothing is allocated: one static buffer, CreateFileA/WriteFile/CloseHandle.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <stddef.h>

namespace ProxyCrashFilter
{
// Installs the filter (once per process; a second call changes nothing and returns false with the reason).
//   recordPath  the file the filter writes (<run>\exe-crash.txt); NULL or "" = no file, the log line only
//   log         an open handle the filter writes one line to (vpproxy.log), or INVALID_HANDLE_VALUE / NULL
//   bootThread  the proxy's boot thread (the record says whether the fault is on it)
//   now         the log's clock in seconds (the "[%10.6f]" of vpproxy.log's lines), or NULL
// *line receives one line for the boot log: what was installed and which filter it chains to.
bool Install(const char* recordPath, HANDLE log, DWORD bootThread, double (*now)(), char* line, size_t lineCap);

// Before the proxy's image is unloaded (FreeLibrary at a mod change; DllMain's detach): drops the log handle and puts
// the previous filter back when this one is still the process's top-level filter. When a later filter replaced it,
// that one is left in place (a later filter that chains to this one would call into the unloaded image: the line says
// so, LOUD). Returns true when the previous filter was put back; *line is "" when the filter was never installed.
bool Uninstall(char* line, size_t lineCap);

// The record for one exception, as the filter writes it (exposed for the tests). Lines end in "\r\n"; the first line
// is the one the server quotes. Returns the length written (always NUL-terminated; cut at cap - 1).
size_t FormatRecord(const EXCEPTION_RECORD* er, DWORD threadId, DWORD bootThread, char* out, size_t cap);

// "<module file name>+0x<RVA>" for an address inside a mapped image, else "an address in no module (0x...)"; *base
// is 0 when there is no module; *imageBase is the EXE file's preferred ImageBase when the address is in the EXE, else 0
// (the knowledge base's VA form is for the EXE). No allocation.
void DescribeAddress(const void* addr, char* out, size_t cap, ULONG_PTR* base, ULONG_PTR* imageBase);

// The name of an exception code ("EXCEPTION_ACCESS_VIOLATION"), or "" for one that has none here.
const char* CodeName(DWORD code);

// The filter itself (tests call it through SetUnhandledExceptionFilter like the system does).
LONG WINAPI Filter(EXCEPTION_POINTERS* ep);

// Tests only: whether a record was written in this process (the filter's once-flag).
bool Recorded();
}  // namespace ProxyCrashFilter
