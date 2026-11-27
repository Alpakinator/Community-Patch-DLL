// child_guard_unit.cpp - ticket 0475: plays server.exe for test_server_child_guard.py. It links the real
// server/src/srv_guard.cpp, installs the guard as srv_main.cpp does (SrvGuardInstall first), then starts a child exactly
// as remote/srv_exe_crash.cpp StartHelper starts vp64helper.exe (CreateProcessW, DETACHED_PROCESS |
// CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB, again without the breakaway on ERROR_ACCESS_DENIED, zeroed
// STARTUPINFOW, the child's folder as its working directory).
//
//   child_guard_unit.exe spawn "<command line>" [--hold SEC]   install the guard, start the child, keep running SEC s
//   child_guard_unit.exe noop                                   exit 0 at once (a 64-bit child for the tests)
//
// Prints GUARD state=<n> <why>, then SPAWN pid=<pid> or SPAWN FAILED error=<n>, and DESCRIBE <SrvGuardDescribe line>
// after the start and again after the hold. A failed guard install exits with SrvGuardExitCode() as main() does.
// Runs only on a hidden desktop (the test refuses the interactive one, COMMON rule 14).
#include "srv_guard.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* ArgValue(int argc, char** argv, const char* name, const char* def)
{
	for(int i = 1; i + 1 < argc; ++i)
		if(!strcmp(argv[i], name))
			return argv[i + 1];
	return def;
}

int main(int argc, char** argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if(argc >= 2 && !strcmp(argv[1], "noop"))
		return 0;
	char why[1024];
	const int st = SrvGuardInstall(why, sizeof(why));
	printf("GUARD state=%d %s\n", st, why);
	if(st == SRV_GUARD_FAILED)
	{
		printf("EXIT 0x%08lX\n", SrvGuardExitCode());
		return (int)SrvGuardExitCode();
	}
	if(argc < 3 || strcmp(argv[1], "spawn") != 0)
	{
		printf("usage: child_guard_unit spawn \"<command line>\" [--hold SEC] | noop\n");
		return 2;
	}
	wchar_t cmd[4096];
	if(!MultiByteToWideChar(CP_ACP, 0, argv[2], -1, cmd, 4096))
		return 2;
	// The working directory: the folder of the child's image (StartHelper uses the proxy's folder, the helper's own).
	wchar_t dir[MAX_PATH] = L"";
	{
		const wchar_t* p = cmd[0] == L'"' ? cmd + 1 : cmd;
		const wchar_t* end = cmd[0] == L'"' ? wcschr(p, L'"') : wcschr(p, L' ');
		size_t n = end ? (size_t)(end - p) : wcslen(p);
		if(n < MAX_PATH)
		{
			wcsncpy_s(dir, p, n);
			wchar_t* slash = wcsrchr(dir, L'\\');
			if(slash)
				*slash = 0;
			else
				dir[0] = 0;
		}
	}
	STARTUPINFOW si;
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi;
	memset(&pi, 0, sizeof(pi));
	const DWORD base = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;
	BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, FALSE, base | CREATE_BREAKAWAY_FROM_JOB, NULL, dir[0] ? dir : NULL,
	                         &si, &pi);
	DWORD err = ok ? 0 : GetLastError();
	if(!ok && err == ERROR_ACCESS_DENIED)
	{
		ok = CreateProcessW(NULL, cmd, NULL, NULL, FALSE, base, NULL, dir[0] ? dir : NULL, &si, &pi);
		err = ok ? 0 : GetLastError();
	}
	if(ok)
	{
		printf("SPAWN pid=%lu\n", pi.dwProcessId);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}
	else
		printf("SPAWN FAILED error=%lu\n", err);
	char line[2048];
	SrvGuardDescribe(st, line, sizeof(line));
	printf("DESCRIBE %s\n", line);
	const double hold = atof(ArgValue(argc, argv, "--hold", "0"));
	if(hold > 0)
		Sleep((DWORD)(hold * 1000));
	SrvGuardDescribe(st, line, sizeof(line));
	printf("DESCRIBE %s\n", line);
	printf("END\n");
	return 0;
}
