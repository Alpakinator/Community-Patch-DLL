// srv_crashdump.cpp - server.exe writes its own minidump on an unhandled exception (issue 3522): see srv_crashdump.h.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>	// _countof
#include <string.h>
#include <wchar.h>

#include "srv_crashdump.h"

namespace
{
typedef BOOL(WINAPI* MiniDumpWriteDumpFn)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                          PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);

// Everything the filter needs is prepared at install time: no allocation in the filter.
MiniDumpWriteDumpFn s_pfnWrite = NULL;
LPTOP_LEVEL_EXCEPTION_FILTER s_pfnPrevious = NULL;
volatile LONG s_lInstalled = 0;
volatile LONG s_lEntered = 0;
wchar_t s_wszPrefix[MAX_PATH * 2] = L"";  // <runDir>\server-<pid>-
wchar_t s_wszWritten[MAX_PATH * 2] = L"";
char s_szLine[MAX_PATH * 3 + 160] = "";
#ifdef SRV_CRASHDUMP_TEST
bool s_bTestFailWrite = false;
#endif

LONG WINAPI Filter(EXCEPTION_POINTERS* pInfo)
{
	// Only the first exception: a fault inside the filter, or another thread's at the same time, passes on.
	if(InterlockedExchange(&s_lEntered, 1) == 0 && s_pfnWrite && s_wszPrefix[0])
	{
		SYSTEMTIME t;
		GetLocalTime(&t);
		wchar_t wszDump[MAX_PATH * 2 + 32];
		wchar_t wszTmp[MAX_PATH * 2 + 40];
		_snwprintf_s(wszDump, _countof(wszDump), _TRUNCATE, L"%ls%04u%02u%02u-%02u%02u%02u.dmp", s_wszPrefix, t.wYear,
		             t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
		_snwprintf_s(wszTmp, _countof(wszTmp), _TRUNCATE, L"%ls.tmp", wszDump);
		const HANDLE hFile = CreateFileW(wszTmp, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		                                 FILE_ATTRIBUTE_NORMAL, NULL);
		if(hFile != INVALID_HANDLE_VALUE)
		{
			MINIDUMP_EXCEPTION_INFORMATION kExc;
			kExc.ThreadId = GetCurrentThreadId();
			kExc.ExceptionPointers = pInfo;
			kExc.ClientPointers = FALSE;
			const MINIDUMP_TYPE eType =
			    (MINIDUMP_TYPE)(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
			BOOL bOk = s_pfnWrite(GetCurrentProcess(), GetCurrentProcessId(), hFile, eType, &kExc, NULL, NULL);
#ifdef SRV_CRASHDUMP_TEST
			if(s_bTestFailWrite)
				bOk = FALSE;
#endif
			CloseHandle(hFile);
			if(bOk && MoveFileExW(wszTmp, wszDump, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
				wcscpy_s(s_wszWritten, _countof(s_wszWritten), wszDump);
			else
				DeleteFileW(wszTmp);	// no partial file
		}
	}
	// As without the filter: the replaced filter decides (the C++ runtime's terminate for an escaping throw), else the
	// system's default handling ends the process with the exception code.
	if(s_pfnPrevious)
		return s_pfnPrevious(pInfo);
	return EXCEPTION_CONTINUE_SEARCH;
}
}  // namespace

const char* SrvCrashDumpInstall(const char* szRunDir)
{
	if(!szRunDir || !szRunDir[0])
	{
		strcpy_s(s_szLine, "crash dump filter NOT installed: no run folder");
		return s_szLine;
	}
	wchar_t wszDir[MAX_PATH * 2];
	if(!MultiByteToWideChar(CP_ACP, 0, szRunDir, -1, wszDir, _countof(wszDir)))
	{
		strcpy_s(s_szLine, "crash dump filter NOT installed: the run folder's name cannot be converted");
		return s_szLine;
	}
	size_t n = wcslen(wszDir);
	while(n > 0 && (wszDir[n - 1] == L'\\' || wszDir[n - 1] == L'/'))
		wszDir[--n] = 0;
	if(!s_pfnWrite)
	{
		// From System32 only (never a dbghelp.dll beside the server or in the working directory).
		HMODULE hDbg = LoadLibraryExW(L"dbghelp.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
		s_pfnWrite = hDbg ? (MiniDumpWriteDumpFn)(void*)GetProcAddress(hDbg, "MiniDumpWriteDump") : NULL;
		if(!s_pfnWrite)
		{
			_snprintf_s(s_szLine, _countof(s_szLine), _TRUNCATE, "crash dump filter NOT installed: %s (error %lu)",
			            hDbg ? "dbghelp.dll has no MiniDumpWriteDump" : "dbghelp.dll cannot be loaded", GetLastError());
			return s_szLine;
		}
	}
	wchar_t wszPrefix[MAX_PATH * 2];
	_snwprintf_s(wszPrefix, _countof(wszPrefix), _TRUNCATE, L"%ls\\server-%lu-", wszDir, GetCurrentProcessId());
	wcscpy_s(s_wszPrefix, _countof(s_wszPrefix), wszPrefix);
	const bool bFirst = InterlockedExchange(&s_lInstalled, 1) == 0;
	if(bFirst)
		s_pfnPrevious = SetUnhandledExceptionFilter(Filter);	// once: a second install never chains to itself
	_snprintf_s(s_szLine, _countof(s_szLine), _TRUNCATE, "crash dump filter %s: %s\\server-%lu-<yyyymmdd-hhmmss>.dmp "
	            "(MiniDumpNormal + thread info + unloaded modules; dbghelp from System32)%s",
	            bFirst ? "installed" : "already installed, folder replaced", szRunDir, GetCurrentProcessId(),
	            bFirst && s_pfnPrevious ? "; the previous filter is called after the dump" : "");
	return s_szLine;
}

const wchar_t* SrvCrashDumpWritten() { return s_wszWritten; }

#ifdef SRV_CRASHDUMP_TEST
void SrvCrashDumpTestFailWrite(bool bFail) { s_bTestFailWrite = bFail; }
#endif
