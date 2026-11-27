// dll_fatal_unit.cpp - the test program of ticket 0422 (server/tests/remote/test_dll_fatal_report.py): it links
// server/src/remote/srv_dll_fatal.cpp and srv_exe_crash.cpp with stand-ins for the server functions they use and plays
// the server whose DLL (dll_fatal_standin.dll, TEST ONLY) stops on a failed check:
//   dll_fatal_unit.exe child <parent pid>     the "EXE": stays until its parent has ended (at most 120 s), exit 0
//   dll_fatal_unit.exe fatal <server.cfg> <what>
//       the "server": reads run_dir and every key from <server.cfg>, starts "child <own pid>", calls OnAttach(child pid),
//       NoteBoot and NoteLoad, loads server.cfg unit_standin (the stand-in DLL), SrvDllFatal::Install(it), sets the
//       context texts (unit_lua=0: none) and the fail hook (prints "FAILHOOK <why>"), installs an unhandled-exception
//       filter as the DLL's CustomFilter does (writes <run>\crashlogs\crashes.log, prints "FILTER 0x%08X report=<0|1>
//       marker=<0|1>": whether the report folder and dll-fatal.txt already existed, then EXCEPTION_EXECUTE_HANDLER, so
//       the process ends with the exception code), then on a second thread (as a service thread):
//         check  StandinCheck(-1): the failed check eImprovementNum > -1
//         msg    StandinCheckMsg(-1): the same with a message
//         ok     StandinCheck(3): no stop; prints "RETURNED 4", exit 0
//         bare   StandinBareTrap(): a ud2 with no call before it
//         av     StandinAccessViolation(): 0xC0000005 in the stand-in, not a failed check
//         own    a ud2 in this program, outside the stand-in's image
//         twice  StandinCheck(-1) on two threads at once: one report
//       Without an exception the program prints "RETURNED <n>" and exits 0.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>
#include <intrin.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "remote/srv_dll_fatal.h"
#include "remote/srv_exe_crash.h"

namespace
{
std::string g_cfgText, g_runDir;
LARGE_INTEGER g_t0, g_freq;
CRITICAL_SECTION g_cs;

bool ReadText(const char* path, std::string& out)
{
	FILE* f = NULL;
	if(fopen_s(&f, path, "rb") || !f)
		return false;
	char buf[4096];
	size_t n;
	while((n = fread(buf, 1, sizeof(buf), f)) > 0)
		out.append(buf, n);
	fclose(f);
	return true;
}
}  // namespace

void SrvLog(const char* szFormat, ...)
{
	char sz[4096];
	va_list v;
	va_start(v, szFormat);
	vsnprintf(sz, sizeof(sz), szFormat, v);
	va_end(v);
	EnterCriticalSection(&g_cs);
	printf("[log] %s\n", sz);
	fflush(stdout);
	if(!g_runDir.empty())
	{
		FILE* f = NULL;
		if(!fopen_s(&f, (g_runDir + "\\server.log").c_str(), "ab") && f)
		{
			fprintf(f, "[%9.3f] %s\n", 0.0, sz);
			fclose(f);
		}
	}
	LeaveCriticalSection(&g_cs);
}

const char* SrvRunDir() { return g_runDir.c_str(); }

std::string SrvCfgStr(const char* szKey, const char* szDefault)
{
	std::string val = szDefault ? szDefault : "";
	size_t pos = 0;
	const size_t klen = strlen(szKey);
	while(pos < g_cfgText.size())
	{
		size_t e = g_cfgText.find('\n', pos);
		if(e == std::string::npos)
			e = g_cfgText.size();
		std::string line = g_cfgText.substr(pos, e - pos);
		pos = e + 1;
		while(!line.empty() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == ' '))
			line.erase(line.size() - 1);
		if(line.size() > klen && line.compare(0, klen, szKey) == 0 && line[klen] == '=')
			val = line.substr(klen + 1);
	}
	return val;
}

double SrvNow()
{
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return (double)(t.QuadPart - g_t0.QuadPart) / (double)g_freq.QuadPart;
}

void SrvBuildIdText(char* out, size_t outSize) { _snprintf_s(out, outSize, _TRUNCATE, "build: dll_fatal_unit (ticket 0422 test)"); }

namespace
{
typedef int(__cdecl* IntFn)(int);
typedef void(__cdecl* VoidFn)();
HMODULE g_hStandin = NULL;
std::string g_what;

std::string FakeServed() { return "runtime::kRt_LuaCall"; }
std::string FakeLua() { return "Plot.IsResourceConnectedByImprovement(a Plot object, -1) on the ui channel"; }
void FakeFail(const char* why)
{
	printf("FAILHOOK %s\n", why);
	fflush(stdout);
}

bool Exists(const std::string& p) { return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

LONG WINAPI DllLikeFilter(EXCEPTION_POINTERS* p)
{
	const DWORD code = p->ExceptionRecord->ExceptionCode;
	const std::string reports = SrvCfgStr("report_root", "");
	bool report = false;
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((reports + "\\*").c_str(), &fd);
	if(h != INVALID_HANDLE_VALUE)
	{
		do
			if((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != '.' &&
			   Exists(reports + "\\" + fd.cFileName + "\\report.txt"))
				report = true;
		while(FindNextFileA(h, &fd));
		FindClose(h);
	}
	const bool marker = Exists(g_runDir + "\\dll-fatal.txt");
	CreateDirectoryA((g_runDir + "\\crashlogs").c_str(), NULL);
	FILE* f = NULL;
	if(!fopen_s(&f, (g_runDir + "\\crashlogs\\crashes.log").c_str(), "ab") && f)
	{
		fprintf(f, "Exception: 0x%08lx (the unit's stand-in of the DLL's CustomFilter)\n", code);
		fclose(f);
	}
	printf("FILTER 0x%08lX report=%d marker=%d\n", code, report ? 1 : 0, marker ? 1 : 0);
	fflush(stdout);
	return EXCEPTION_EXECUTE_HANDLER;
}

__declspec(noinline) void OwnTrap()
{
#if defined(__clang__)
	__builtin_trap();
#else
	__ud2();  // MSVC (the --asan build): the same ud2
#endif
}

DWORD WINAPI Work(void*)
{
	if(g_what == "check" || g_what == "twice")
	{
		IntFn f = (IntFn)(void*)GetProcAddress(g_hStandin, "StandinCheck");
		printf("RETURNED %d\n", f(-1));
	}
	else if(g_what == "msg")
	{
		IntFn f = (IntFn)(void*)GetProcAddress(g_hStandin, "StandinCheckMsg");
		printf("RETURNED %d\n", f(-1));
	}
	else if(g_what == "ok")
	{
		IntFn f = (IntFn)(void*)GetProcAddress(g_hStandin, "StandinCheck");
		printf("RETURNED %d\n", f(3));
	}
	else if(g_what == "bare")
	{
		VoidFn f = (VoidFn)(void*)GetProcAddress(g_hStandin, "StandinBareTrap");
		f();
		printf("RETURNED 0\n");
	}
	else if(g_what == "av")
	{
		VoidFn f = (VoidFn)(void*)GetProcAddress(g_hStandin, "StandinAccessViolation");
		f();
		printf("RETURNED 0\n");
	}
	else if(g_what == "own")
	{
		OwnTrap();
		printf("RETURNED 0\n");
	}
	fflush(stdout);
	return 0;
}

int Child(int argc, char** argv)
{
	if(argc < 3)
		return 2;
	HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)strtoul(argv[2], NULL, 10));
	if(h)
	{
		WaitForSingleObject(h, 120000);
		CloseHandle(h);
	}
	return 0;
}

int Fatal(int argc, char** argv)
{
	if(argc < 4)
		return 2;
	if(!ReadText(argv[2], g_cfgText))
	{
		printf("cannot read %s\n", argv[2]);
		return 2;
	}
	g_runDir = SrvCfgStr("run_dir", "");
	g_what = argv[3];
	wchar_t self[MAX_PATH * 2];
	GetModuleFileNameW(NULL, self, MAX_PATH * 2);
	wchar_t cmd[MAX_PATH * 3];
	_snwprintf_s(cmd, MAX_PATH * 3, _TRUNCATE, L"\"%s\" child %lu", self, GetCurrentProcessId());
	STARTUPINFOW si;
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi;
	memset(&pi, 0, sizeof(pi));
	if(!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
	{
		printf("CreateProcess failed: %lu\n", GetLastError());
		return 2;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	SrvExeCrash::OnAttach(pi.dwProcessId);
	SrvExeCrash::NoteBoot();
	SrvExeCrash::NoteLoad();
	const std::string standin = SrvCfgStr("unit_standin", "");
	g_hStandin = LoadLibraryA(standin.c_str());
	if(!g_hStandin)
	{
		printf("cannot load %s (error %lu)\n", standin.c_str(), GetLastError());
		return 2;
	}
	printf("INSTALL %s\n", SrvDllFatal::Install(g_hStandin).c_str());
	SrvDllFatal::SetContextText(&FakeServed, SrvCfgStr("unit_lua", "1") == "0" ? NULL : &FakeLua);
	SrvDllFatal::SetFailHook(&FakeFail);
	SetUnhandledExceptionFilter(DllLikeFilter);
	fflush(stdout);
	const int nThreads = g_what == "twice" ? 2 : 1;
	HANDLE th[2];
	for(int i = 0; i < nThreads; ++i)
		th[i] = CreateThread(NULL, 0, Work, NULL, 0, NULL);
	WaitForMultipleObjects(nThreads, th, TRUE, 60000);
	return 0;
}
}  // namespace

int main(int argc, char** argv)
{
	InitializeCriticalSection(&g_cs);
	QueryPerformanceFrequency(&g_freq);
	QueryPerformanceCounter(&g_t0);
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	if(argc >= 2 && !strcmp(argv[1], "child"))
		return Child(argc, argv);
	if(argc >= 2 && !strcmp(argv[1], "fatal"))
		return Fatal(argc, argv);
	printf("usage: dll_fatal_unit child <parent pid> | fatal <server.cfg> check|msg|ok|bare|av|own|twice\n");
	return 2;
}
