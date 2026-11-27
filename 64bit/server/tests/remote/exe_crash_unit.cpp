// exe_crash_unit.cpp - the test program of ticket 0418 (server/tests/remote/test_exe_crash_report.py): it links
// server/src/remote/srv_exe_crash.cpp with stand-ins for the server functions it uses and plays both sides without
// vpipc and without the server:
//   exe_crash_unit.exe child <hex code> <ms>          the "EXE": sleeps <ms>, then ends itself with TerminateProcess(code)
//   exe_crash_unit.exe peer <server.cfg> [load] [early] [kill]
//       the "server": reads run_dir and every key from <server.cfg> (SrvCfgStr), starts "child <code> <ms>" (server.cfg
//       keys unit_child_code, unit_child_ms; kill: the child sleeps 60 s and test_exe_kill must end it), calls
//       OnAttach(child pid), NoteBoot, with load NoteLoad; waits for the child to end (early: does not wait), then calls
//       OnPeerGone from three threads at once (as three service threads whose channels failed) and prints
//       "RESULT <i> <clause>" for each, "CHILD exit 0x%08X", and ends with exit code 3 as the server does.
//   exe_crash_unit.exe codes                        prints "CODE 0x%08X crash=<0|1> name=<name>" for a fixed list
// Server functions stood in: SrvLog (stdout and <run_dir>\server.log), SrvRunDir, SrvCfgStr, SrvNow, SrvBuildIdText.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
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
	// key=value lines, the last one wins (as the server's reader)
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

void SrvBuildIdText(char* out, size_t outSize) { _snprintf_s(out, outSize, _TRUNCATE, "build: exe_crash_unit (ticket 0418 test)"); }

namespace
{
std::string g_results[3];

DWORD WINAPI GoneThread(void* pv)
{
	const int i = (int)(INT_PTR)pv;
	g_results[i] = SrvExeCrash::OnPeerGone();
	return 0;
}

int Child(int argc, char** argv)
{
	if(argc < 4)
		return 2;
	const unsigned long code = strtoul(argv[2], NULL, 16);
	Sleep((DWORD)strtoul(argv[3], NULL, 10));
	TerminateProcess(GetCurrentProcess(), (UINT)code);
	return 0;
}

int Peer(int argc, char** argv)
{
	if(argc < 3)
		return 2;
	if(!ReadText(argv[2], g_cfgText))
	{
		printf("cannot read %s\n", argv[2]);
		return 2;
	}
	g_runDir = SrvCfgStr("run_dir", "");
	bool load = false, early = false, kill = false;
	for(int i = 3; i < argc; ++i)
	{
		load |= !strcmp(argv[i], "load");
		early |= !strcmp(argv[i], "early");
		kill |= !strcmp(argv[i], "kill");
	}
	wchar_t self[MAX_PATH * 2];
	GetModuleFileNameW(NULL, self, MAX_PATH * 2);
	const std::string code = SrvCfgStr("unit_child_code", "C0000005");
	const std::string ms = kill ? std::string("60000") : SrvCfgStr("unit_child_ms", "300");
	wchar_t cmd[MAX_PATH * 3];
	_snwprintf_s(cmd, MAX_PATH * 3, _TRUNCATE, L"\"%s\" child %S %S", self, code.c_str(), ms.c_str());
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
	printf("CHILD pid %lu\n", pi.dwProcessId);
	fflush(stdout);
	SrvExeCrash::OnAttach(pi.dwProcessId);
	SrvExeCrash::NoteBoot();
	if(load)
		SrvExeCrash::NoteLoad();
	if(!early)
		WaitForSingleObject(pi.hProcess, 90000);
	HANDLE th[3];
	for(int i = 0; i < 3; ++i)
		th[i] = CreateThread(NULL, 0, GoneThread, (void*)(INT_PTR)i, 0, NULL);
	WaitForMultipleObjects(3, th, TRUE, 120000);
	for(int i = 0; i < 3; ++i)
	{
		CloseHandle(th[i]);
		printf("RESULT %d %s\n", i, g_results[i].c_str());
	}
	if(early)
	{
		TerminateProcess(pi.hProcess, 0x77);
		WaitForSingleObject(pi.hProcess, 10000);
	}
	DWORD exitCode = 0;
	GetExitCodeProcess(pi.hProcess, &exitCode);
	printf("CHILD exit 0x%08lX\n", exitCode);
	CloseHandle(pi.hProcess);
	fflush(stdout);
	return 3;
}

int Codes()
{
	static const unsigned int list[] = {0u, 1u, 3u, 5u, 6u, 15u, 0x77u, 0x8000DEADu, 0x80000001u, 0x80000002u, 0x80000003u,
	                                    0x80000004u, 0x80000005u, 0xC0000005u, 0xC000013Au, 0xC0000409u, 0xC00000FDu,
	                                    0xC0000374u, 0xE06D7363u, 0xC0001234u, 0x40010004u, 0xFFFFFFFFu};
	for(size_t i = 0; i < sizeof(list) / sizeof(list[0]); ++i)
	{
		const char* name = NULL;
		const bool crash = SrvExeCrash::IsCrashCode(list[i], &name);
		printf("CODE 0x%08X crash=%d name=%s\n", list[i], crash ? 1 : 0, name ? name : "(null)");
	}
	return 0;
}

// Ticket 0641: fatal <run dir or -> <code> <why>: FatalWords' token and summary ("WORDS <token>|<summary>", token "-" when
// none), then WriteFatalRecord into <run dir> ("-": no run folder) and "WROTE <0|1>"
int Fatal(int argc, char** argv)
{
	if(argc < 5)
		return 2;
	g_runDir = strcmp(argv[2], "-") ? argv[2] : "";
	const int code = (int)strtoul(argv[3], NULL, 0);
	const SrvExeCrash::FatalRecord r = SrvExeCrash::FatalWords(code, argv[4]);
	printf("WORDS %s|%s\n", r.token.empty() ? "-" : r.token.c_str(), r.summary.c_str());
	printf("PID %lu\n", GetCurrentProcessId());   // ticket 0744: the record's pid line
	printf("WROTE %d\n", SrvExeCrash::WriteFatalRecord(code, argv[4]) ? 1 : 0);
	fflush(stdout);
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
	if(argc >= 2 && !strcmp(argv[1], "peer"))
		return Peer(argc, argv);
	if(argc >= 2 && !strcmp(argv[1], "codes"))
		return Codes();
	if(argc >= 2 && !strcmp(argv[1], "fatal"))
		return Fatal(argc, argv);   // ticket 0641
	printf("usage: exe_crash_unit child <hex code> <ms> | peer <server.cfg> [load] [early] [kill] | codes | "
	       "fatal <run> <code> <why>\n");
	return 2;
}
