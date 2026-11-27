// srv_dll_fatal.h - D118 for the server's DLL stopping on a failed check (ticket 0422; decisions D118, D119): when the x64
// DLL inside the server stops on a failed PRECONDITION (CvPreconditionFailedFormat: CvPreconditionDlg, then ud2, exception
// 0xC000001D, the DLL's "Deliberate stop"), the server writes the D119 report (stage dll-fatal: the check's text, file
// and line, the Lua call and the engine calls that led there, the log tails) and starts vp64helper popup before its
// process ends, and so before the proxy ends the EXE.
// SPDX-License-Identifier: GPL-3.0-or-later
// Knowledge base: research/03-dll-engine-boundary/error-report-exe-crash.md section 9.
//
// Why a vectored handler: the server's DLL is built with VP_OUT_OF_PROCESS, which turns every error popup of the DLL off
// (CvGlobals.cpp g_bPopupsDisabledByUser), and its CvPreconditionDlg then logs nothing (CvAssert.log stays empty for a
// precondition); the DLL's CvGlobals init replaces the server's unhandled-exception filter with its CustomFilter, which
// writes crashlogs\crashes.log and a minidump and ends the process without chaining. A vectored handler runs before both
// [C, 2026-11-16]. It acts only on EXCEPTION_ILLEGAL_INSTRUCTION at a ud2 (0F 0B) inside the DLL's image, once per
// process, and always returns EXCEPTION_CONTINUE_SEARCH: the DLL's own filter then writes crashes.log and its minidump
// and the process ends with 0xC000001D as before.
//
// The check's text: at the ud2 the call to CvPreconditionDlg(expr, file, line, msg) has just returned; its argument
// registers (rcx, rdx, r8, r9) are volatile, but they were loaded by register moves from callee-saved registers just
// before the call (x64 pin 04b1211b8: mov rcx,rbx; mov rdx,rdi; mov r8d,esi; mov r9,r14; call; ud2, in both overloads
// [C, the 4510 minidump]). The handler decodes those moves backwards from the call and reads the source registers from
// the exception's CONTEXT. The strings must lie in the DLL's image and be printable, the file must end in a source
// extension and the line be 1..10^6; otherwise the report says the text could not be read and names the trap's place.
#pragma once
#include <stdint.h>
#include <string>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace SrvDllFatal
{
// Installs the vectored handler (once) for hModule's image; a later call replaces the image (one DLL per server).
// Returns one line for server.log.
std::string Install(HMODULE hModule);

// Texts of the faulting thread's context, from the dispatch (srv_rt.cpp, srv_remote_lua.cpp): "" when none.
typedef std::string (*TextFn)();
void SetContextText(TextFn pfnServedCalls, TextFn pfnLuaCall);
// Called after the D119 report and the helper's start, on the faulting thread: the server's own server-report.txt.
typedef void (*FailFn)(const char* szWhy);
void SetFailHook(FailFn pfn);

struct Check
{
	bool decoded;          // the call's argument moves were decoded and every value checked out
	std::string expr, file, message;
	uint32_t line;
	std::string how;       // how the text was read (the registers), or why it could not be
	Check() : decoded(false), line(0) {}
};
// Reads the check at a ud2 at `trap` inside [base, end) from the registers in `ctx` (exposed for the tests).
Check ReadCheck(const CONTEXT* ctx, uintptr_t trap, uintptr_t base, uintptr_t end);
}  // namespace SrvDllFatal
