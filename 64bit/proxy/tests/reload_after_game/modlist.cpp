// 0805: the modules of a running 32-bit process (read only): base, size and path of each, optionally filtered by a
// substring of the path. Built x86 (a WOW64 process's modules are not listed by a 64-bit caller).
//   modlist.exe PID [SUBSTRING]
// Exit 0 listed, 2 bad arguments, 3 the process could not be opened or read.
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

int wmain(int argc, wchar_t** argv)
{
	if (argc < 2) return 2;
	const DWORD pid = (DWORD)wcstoul(argv[1], NULL, 10);
	const wchar_t* filter = argc > 2 ? argv[2] : NULL;
	HANDLE p = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
	if (!p)
	{
		wprintf(L"OpenProcess(%lu) failed: %lu\n", pid, GetLastError());
		return 3;
	}
	HMODULE mods[1024];
	DWORD need = 0;
	if (!EnumProcessModulesEx(p, mods, sizeof(mods), &need, LIST_MODULES_ALL))
	{
		wprintf(L"EnumProcessModulesEx failed: %lu\n", GetLastError());
		CloseHandle(p);
		return 3;
	}
	const DWORD n = need / sizeof(HMODULE);
	unsigned shown = 0;
	for (DWORD i = 0; i < n && i < 1024; ++i)
	{
		wchar_t path[MAX_PATH * 2];
		if (!GetModuleFileNameExW(p, mods[i], path, MAX_PATH * 2)) continue;
		if (filter && !wcsstr(path, filter)) continue;
		MODULEINFO mi;
		if (!GetModuleInformation(p, mods[i], &mi, sizeof(mi))) memset(&mi, 0, sizeof(mi));
		wprintf(L"%08lX %08lX %s\n", (unsigned long)(ULONG_PTR)mi.lpBaseOfDll, (unsigned long)mi.SizeOfImage, path);
		++shown;
	}
	wprintf(L"%u of %lu modules shown (pid %lu)\n", shown, n, pid);
	CloseHandle(p);
	return 0;
}
