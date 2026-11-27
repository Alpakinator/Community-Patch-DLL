// crashdump_test.cpp - the test program of server/src/srv_crashdump.cpp (issue 3522). Built x64 with the server's
// toolchain and /DSRV_CRASHDUMP_TEST by run_crashdump_test.py, run on a hidden desktop. Shows no window; error boxes are
// off (SetErrorMode, as server.exe does).
//
//   crashdump_test.exe av <dir> [--twice] [--no-filter] [--fail-write]   access violation (write to address 0x10)
//   crashdump_test.exe throw <dir> [--no-filter]                         a C++ exception that escapes main
//   crashdump_test.exe ok <dir> [--twice]                                installs, exits 0 (no dump)
//   crashdump_test.exe check <dump file>                                 opens a dump with dbghelp (MiniDumpReadDumpStream)
//   crashdump_test.exe --remote --section S --config <server.cfg>        as a fake server.exe for the proxy's tests:
//                                                                        installs on server.cfg's run_dir, then faults
// Before faulting it prints "TID <thread id>" and the install line. "check" prints one line
//   CHECK ok code 0x... address 0x... tid N thread_listed 1 modules N in_module <name>   (exit 0)
// or "CHECK FAIL <why>" (exit 1).
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdexcept>

#include "srv_crashdump.h"

namespace
{
bool Flag(int argc, char** argv, const char* name)
{
	for(int i = 1; i < argc; ++i)
		if(!strcmp(argv[i], name))
			return true;
	return false;
}

const char* Arg(int argc, char** argv, const char* name)
{
	for(int i = 1; i + 1 < argc; ++i)
		if(!strcmp(argv[i], name))
			return argv[i + 1];
	return NULL;
}

void Install(const char* dir, bool twice)
{
	printf("INSTALL %s\n", SrvCrashDumpInstall(dir));
	if(twice)
		printf("INSTALL %s\n", SrvCrashDumpInstall(dir));
}

__declspec(noinline) void Fault()
{
	volatile int* p = (volatile int*)0x10;
	*p = 1;
}

__declspec(noinline) void Throw() { throw std::runtime_error("issue 3522: a C++ exception that escapes"); }

typedef BOOL(WINAPI* ReadStreamFn)(PVOID, ULONG, PMINIDUMP_DIRECTORY*, PVOID*, ULONG*);

int Check(const char* path)
{
	HMODULE hDbg = LoadLibraryExW(L"dbghelp.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
	ReadStreamFn read = hDbg ? (ReadStreamFn)(void*)GetProcAddress(hDbg, "MiniDumpReadDumpStream") : NULL;
	if(!read)
	{
		printf("CHECK FAIL dbghelp MiniDumpReadDumpStream not available\n");
		return 1;
	}
	HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if(hFile == INVALID_HANDLE_VALUE)
	{
		printf("CHECK FAIL cannot open %s (error %lu)\n", path, GetLastError());
		return 1;
	}
	HANDLE hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
	void* pBase = hMap ? MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0) : NULL;
	int rc = 1;
	if(!pBase)
		printf("CHECK FAIL cannot map %s\n", path);
	else
	{
		MINIDUMP_EXCEPTION_STREAM* pExc = NULL;
		MINIDUMP_THREAD_LIST* pThreads = NULL;
		MINIDUMP_MODULE_LIST* pModules = NULL;
		ULONG cb = 0;
		PMINIDUMP_DIRECTORY pDir = NULL;
		const bool bExc = read(pBase, ExceptionStream, &pDir, (PVOID*)&pExc, &cb) && pExc;
		const bool bThr = read(pBase, ThreadListStream, &pDir, (PVOID*)&pThreads, &cb) && pThreads;
		const bool bMod = read(pBase, ModuleListStream, &pDir, (PVOID*)&pModules, &cb) && pModules;
		if(!bExc || !bThr || !bMod)
			printf("CHECK FAIL streams: exception %d, thread list %d, module list %d\n", bExc, bThr, bMod);
		else
		{
			const ULONG64 addr = pExc->ExceptionRecord.ExceptionAddress;
			bool listed = false;
			for(ULONG i = 0; i < pThreads->NumberOfThreads; ++i)
				if(pThreads->Threads[i].ThreadId == pExc->ThreadId)
					listed = true;
			char szIn[MAX_PATH] = "(none)";
			for(ULONG i = 0; i < pModules->NumberOfModules; ++i)
			{
				const MINIDUMP_MODULE& m = pModules->Modules[i];
				if(addr >= m.BaseOfImage && addr < m.BaseOfImage + m.SizeOfImage)
				{
					const MINIDUMP_STRING* s = (const MINIDUMP_STRING*)((const char*)pBase + m.ModuleNameRva);
					const wchar_t* w = s->Buffer;
					const wchar_t* slash = wcsrchr(w, L'\\');
					WideCharToMultiByte(CP_UTF8, 0, slash ? slash + 1 : w, -1, szIn, sizeof(szIn), NULL, NULL);
				}
			}
			printf("CHECK ok code 0x%08lX address 0x%016llX tid %lu thread_listed %d threads %lu modules %lu in_module %s\n",
			       (unsigned long)pExc->ExceptionRecord.ExceptionCode, (unsigned long long)addr,
			       (unsigned long)pExc->ThreadId, listed ? 1 : 0, (unsigned long)pThreads->NumberOfThreads,
			       (unsigned long)pModules->NumberOfModules, szIn);
			rc = 0;
		}
		UnmapViewOfFile(pBase);
	}
	if(hMap)
		CloseHandle(hMap);
	CloseHandle(hFile);
	return rc;
}

// server.cfg's run_dir= (the proxy's run folder), as srv_main.cpp's LoadConfig reads key=value lines.
bool RunDirOf(const char* cfg, char* out, size_t cap)
{
	FILE* f = NULL;
	if(fopen_s(&f, cfg, "rb") || !f)
		return false;
	char line[2048];
	bool found = false;
	while(!found && fgets(line, sizeof(line), f))
	{
		if(strncmp(line, "run_dir=", 8))
			continue;
		char* v = line + 8;
		size_t n = strlen(v);
		while(n && (v[n - 1] == '\n' || v[n - 1] == '\r'))
			v[--n] = 0;
		strcpy_s(out, cap, v);
		found = true;
	}
	fclose(f);
	return found;
}
}  // namespace

int main(int argc, char** argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);	// as server.exe
	if(argc < 3)
	{
		printf("usage: see the top of crashdump_test.cpp\n");
		return 2;
	}
	if(const char* cfg = Arg(argc, argv, "--config"))
	{
		char dir[MAX_PATH * 2];
		if(!RunDirOf(cfg, dir, sizeof(dir)))
		{
			printf("FAKE no run_dir in %s\n", cfg);
			return 7;
		}
		printf("TID %lu\n", GetCurrentThreadId());
		Install(dir, false);
		Fault();
		return 0;
	}
	const char* mode = argv[1];
	if(!strcmp(mode, "check"))
		return Check(argv[2]);
	const char* dir = argv[2];
	const bool filter = !Flag(argc, argv, "--no-filter");
	if(filter)
		Install(dir, Flag(argc, argv, "--twice"));
	if(Flag(argc, argv, "--fail-write"))
		SrvCrashDumpTestFailWrite(true);
	printf("TID %lu\n", GetCurrentThreadId());
	if(!strcmp(mode, "av"))
		Fault();
	else if(!strcmp(mode, "throw"))
		Throw();
	else if(!strcmp(mode, "ok"))
	{
		printf("OK no fault; written '%ls'\n", SrvCrashDumpWritten());
		return 0;
	}
	printf("not reached\n");
	return 5;
}
