// srv_crashdump.h - server.exe writes its own minidump on an unhandled exception (issue 3522; decisions D119, D124).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Why: the error report of issue 3521 (research/03-dll-engine-boundary/proxy-design.md section 21) copies every *.dmp of
// the boot's run folder, but the 32-bit proxy cannot write a minidump of the 64-bit server (MiniDumpWriteDump fails with
// 0x8007012B, measured by 3521). So the server writes one of itself.
//
// SrvCrashDumpInstall(runDir), called once from main() as soon as server.cfg's run_dir is known (the proxy's run folder
// in remote mode), loads dbghelp.dll from System32, prepares the path prefix <runDir>\server-<pid>- and installs a
// top-level filter (SetUnhandledExceptionFilter). On an unhandled exception the filter writes
// <runDir>\server-<pid>-<yyyymmdd-hhmmss>.dmp (MiniDumpNormal + MiniDumpWithThreadInfo + MiniDumpWithUnloadedModules:
// the threads with their stacks, the modules and the exception record; no full memory) through a .tmp file renamed at
// the end, so an unwritable folder or a failed write leaves no partial file. Then it hands the exception on to the filter
// it replaced (the C++ runtime's for an escaping C++ throw) or returns EXCEPTION_CONTINUE_SEARCH: the process ends with
// the same exit code as without the filter, and the proxy sees the death as before. Only the first exception is
// dumped (a fault inside the filter, or a second thread, passes straight on). No heap allocation in the filter.
// A second SrvCrashDumpInstall only replaces the folder (the filter stays installed once and never chains to itself).
// The dump holds the server's stacks, which are the tester's game state; 3520's popup says so for any *.dmp.
#pragma once
#include <stddef.h>

// Installs the filter (or, on a second call, only replaces the folder). Returns one line for server.log ("crash dump
// filter installed: <runDir>\server-<pid>-<stamp>.dmp" or why not: dbghelp.dll or MiniDumpWriteDump missing, no folder).
const char* SrvCrashDumpInstall(const char* szRunDir);

// The path of the dump the filter wrote, "" when none (for tests).
const wchar_t* SrvCrashDumpWritten();

#ifdef SRV_CRASHDUMP_TEST
// TEST BUILD ONLY (server/tests/crashdump): the filter opens the .tmp file, then acts as if MiniDumpWriteDump failed.
void SrvCrashDumpTestFailWrite(bool bFail);
#endif
