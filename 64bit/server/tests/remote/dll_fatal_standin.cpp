// dll_fatal_standin.cpp - TEST ONLY (ticket 0422): a stand-in for the DLL's failed check, built as a DLL by
// server/tests/remote/test_dll_fatal_report.py with the server's clang-cl flags (/O2). It has the shape of the fork's
// PRECONDITION (Community-Patch-DLL, CvGameCoreDLL_Expansion2/CvGameCoreDLLPCH.h and CvGameCoreUtils.cpp
// CvPreconditionFailedFormat at commit 04b1211b8, GPL-3.0): a noinline failure function formats the message (a call),
// calls a noinline "dialog" with (expr, file, line, message) and then traps with ud2. Nothing of the fork is copied; the
// shape is what srv_dll_fatal.cpp reads at the trap.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdarg.h>
#include <stdio.h>
#include <string>

// External linkage and exported, as CvPreconditionDlg and CvPreconditionFailedFormat are declared in the fork's
// headers: a function of internal linkage lets clang drop or fold its arguments (seen 2026-11-16: the dialog's
// arguments were folded into one constant and no register moves were left to read).
volatile long g_sink = 0;

__declspec(dllexport) __declspec(noinline) void StandinFormat(std::string& out, const char* fmt, va_list ap)
{
	char buf[256] = "";
	if(fmt)
		vsnprintf(buf, sizeof(buf), fmt, ap);
	out = buf;
}

// The "dialog": in the server's DLL every popup is off (VP_OUT_OF_PROCESS), so it only looks at its arguments.
__declspec(dllexport) __declspec(noinline) void StandinDlg(const char* expr, const char* file, unsigned int line, const char* msg)
{
	g_sink += (long)(expr[0] + file[0] + (long)line + msg[0]);
}

__declspec(dllexport) __declspec(noinline) __declspec(noreturn) void StandinFailed(const char* expr, const char* file, unsigned int line)
{
	std::string message;
	va_list none = NULL;
	StandinFormat(message, NULL, none);
	StandinDlg(expr, file, line, message.c_str());
	__builtin_trap();
}

__declspec(dllexport) __declspec(noinline) __declspec(noreturn) void StandinFailedMsg(const char* expr, const char* file, unsigned int line,
                                                                 const char* fmt, ...)
{
	std::string message;
	va_list ap;
	va_start(ap, fmt);
	StandinFormat(message, fmt, ap);
	va_end(ap);
	StandinDlg(expr, file, line, message.c_str());
	__builtin_trap();
}

#define STANDIN_PRECONDITION(expr) if(!(expr)) StandinFailed(#expr, __FILE__, __LINE__)
#define STANDIN_PRECONDITION_MSG(expr, ...) if(!(expr)) StandinFailedMsg(#expr, __FILE__, __LINE__, __VA_ARGS__)

// The check of 4510's stop: getImprovementInfo(-1).
extern "C" __declspec(dllexport) int StandinCheck(int eImprovementNum)
{
	STANDIN_PRECONDITION(eImprovementNum > -1);
	return eImprovementNum + 1;
}

// The same with a message (the variadic overload).
extern "C" __declspec(dllexport) int StandinCheckMsg(int eImprovementNum)
{
	STANDIN_PRECONDITION_MSG(eImprovementNum > -1, "improvement %d is out of range", eImprovementNum);
	return eImprovementNum + 1;
}

// A ud2 with no call before it (as CvDllGame::Uninit's guard): a stop whose text cannot be read.
extern "C" __declspec(dllexport) void StandinBareTrap()
{
	if(g_sink != 12345)
		__builtin_trap();
}

// An access violation inside the "DLL": not a failed check, no dll-fatal report.
extern "C" __declspec(dllexport) void StandinAccessViolation()
{
	int* volatile p = NULL;  // read back at run time, so the compiler cannot turn the store into a trap
	*p = 1;
}
