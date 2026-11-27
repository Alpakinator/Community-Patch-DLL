// decal_tap_host.cpp - test entry of the decal tap (issue 4102, epic 0166): a console program that links
// composite/decal_tap.cpp and calls DecalTap_OnPresent as the composite's Present hook does, without D3D (the tap uses
// none). Written 2026-11-15 for this repository (GPL-3.0-or-later). Used by shell/tests/test_decal_tap.py; never loaded
// into the game. The tap reads its keys from the environment (VPSHELL_DECAL_*) or shell_composite.ini beside this exe.
//
//   decal_tap_host.exe --seconds S [--period-ms 2] [--exe self|none] [--unvalidated] [--grid 0|1] --log FILE
// --exe self: the real path, with this program's own image as "the EXE" (no pattern matches: FAILED PATTERN);
// --exe none (default): exeBase 0 (with decal_test set, the synthetic decal system). A file `decal_tap.off` beside the
// exe is the kill switch (the composite's decal_tap=0); `decal_tap.grid` holding a number sets gridSeq (a new load).
// Exit 0 after S seconds. At exit it logs the cost of every DecalTap_OnPresent call (issue 4110): the thread's CPU time
// (QueryThreadCycleTime, calibrated against QueryPerformanceCounter at start: time the thread was not running is not
// counted) and the wall time, maximum and mean, in one line "decal_tap_host: Present cost ...".
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <share.h>
#include "decal_tap.h"

static FILE* g_f;
static LARGE_INTEGER g_t0, g_freq;

static void Log(const char* fmt, ...)
{
	if (!g_f) return;
	LARGE_INTEGER t; QueryPerformanceCounter(&t);
	fprintf(g_f, "[%9.3f] ", (double)(t.QuadPart - g_t0.QuadPart) / (double)g_freq.QuadPart);
	va_list ap; va_start(ap, fmt); vfprintf(g_f, fmt, ap); va_end(ap);
	fputc('\n', g_f);
	fflush(g_f);
}

int wmain(int argc, wchar_t** argv)
{
	double seconds = 5; int period = 2; bool self = false, validated = true; int grid = 1;
	const wchar_t* logPath = nullptr;
	for (int i = 1; i < argc; ++i)
	{
		if (!wcscmp(argv[i], L"--seconds") && i + 1 < argc) seconds = _wtof(argv[++i]);
		else if (!wcscmp(argv[i], L"--period-ms") && i + 1 < argc) period = _wtoi(argv[++i]);
		else if (!wcscmp(argv[i], L"--exe") && i + 1 < argc) self = !wcscmp(argv[++i], L"self");
		else if (!wcscmp(argv[i], L"--unvalidated")) validated = false;
		else if (!wcscmp(argv[i], L"--grid") && i + 1 < argc) grid = _wtoi(argv[++i]);
		else if (!wcscmp(argv[i], L"--log") && i + 1 < argc) logPath = argv[++i];
		else { fwprintf(stderr, L"unknown argument %ls\n", argv[i]); return 2; }
	}
	if (!logPath) { fwprintf(stderr, L"--log required\n"); return 2; }
	g_f = _wfsopen(logPath, L"a", _SH_DENYNO);   // shared: the test reads it while the host runs
	if (!g_f) return 3;
	QueryPerformanceFrequency(&g_freq); QueryPerformanceCounter(&g_t0);
	wchar_t dir[MAX_PATH]; GetModuleFileNameW(nullptr, dir, MAX_PATH);
	wchar_t* sl = wcsrchr(dir, L'\\'); if (sl) sl[1] = 0;
	wchar_t off[MAX_PATH], gridf[MAX_PATH];
	_snwprintf_s(off, _TRUNCATE, L"%sdecal_tap.off", dir);
	_snwprintf_s(gridf, _TRUNCATE, L"%sdecal_tap.grid", dir);
	static VPShellCamera cam = {};
	cam.gridValid = grid ? 1u : 0u; cam.gridSeq = 1;
	Log("decal_tap_host: pid %lu, %.1f s, period %d ms, exe %s, validated %d", GetCurrentProcessId(), seconds, period,
		self ? "self" : "none", validated ? 1 : 0);
	// cycles of this thread per ms: a 50 ms busy loop, cycles against QPC
	double cyclesPerMs = 0;
	{
		ULONG64 c0 = 0, c1 = 0; LARGE_INTEGER q0, q1;
		QueryThreadCycleTime(GetCurrentThread(), &c0); QueryPerformanceCounter(&q0);
		do { QueryPerformanceCounter(&q1); } while ((double)(q1.QuadPart - q0.QuadPart) * 1000.0 / (double)g_freq.QuadPart < 50.0);
		QueryThreadCycleTime(GetCurrentThread(), &c1);
		cyclesPerMs = (double)(c1 - c0) / ((double)(q1.QuadPart - q0.QuadPart) * 1000.0 / (double)g_freq.QuadPart);
	}
	double cpuMax = 0, cpuSum = 0, wallMax = 0, wallSum = 0, cpuFirst = 0, wallFirst = 0;
	const ULONGLONG end = GetTickCount64() + (ULONGLONG)(seconds * 1000);
	unsigned frames = 0;
	while (GetTickCount64() < end)
	{
		FILE* g = nullptr;
		if ((g = _wfsopen(gridf, L"r", _SH_DENYNO)) != nullptr) { unsigned s = 0; if (fscanf_s(g, "%u", &s) == 1 && s) cam.gridSeq = s; fclose(g); }
		DecalTapEnv env = {};
		env.hdr = nullptr;
		env.exeBase = self ? (uint32_t)(uintptr_t)GetModuleHandleW(nullptr) : 0;
		env.exeValidated = validated ? 1u : 0u;
		env.cam = &cam;
		env.enabled = GetFileAttributesW(off) == INVALID_FILE_ATTRIBUTES;
		env.log = Log;
		ULONG64 c0 = 0, c1 = 0; LARGE_INTEGER w0, w1;
		QueryThreadCycleTime(GetCurrentThread(), &c0); QueryPerformanceCounter(&w0);
		DecalTap_OnPresent(env);
		QueryPerformanceCounter(&w1); QueryThreadCycleTime(GetCurrentThread(), &c1);
		const double cpu = cyclesPerMs > 0 ? (double)(c1 - c0) / cyclesPerMs : 0;
		const double wall = (double)(w1.QuadPart - w0.QuadPart) * 1000.0 / (double)g_freq.QuadPart;
		if (!frames) { cpuFirst = cpu; wallFirst = wall; }      // the first Present sets up (a test world: not the tap's cost)
		else { if (cpu > cpuMax) cpuMax = cpu; if (wall > wallMax) wallMax = wall; }
		cpuSum += cpu; wallSum += wall;
		frames++;
		Sleep(period);
	}
	DecalTap_Shutdown("host exit");
	Log("decal_tap_host: Present cost over %u Presents after the first: CPU max %.3f ms, mean %.4f ms; wall max %.3f ms, mean %.4f ms "
		"(%.0f cycles per ms); the first Present (set-up) CPU %.3f ms, wall %.3f ms", frames ? frames - 1 : 0, cpuMax,
		frames > 1 ? (cpuSum - cpuFirst) / (frames - 1) : 0.0, wallMax, frames > 1 ? (wallSum - wallFirst) / (frames - 1) : 0.0,
		cyclesPerMs, cpuFirst, wallFirst);
	Log("decal_tap_host: exit after %u Presents", frames);
	fclose(g_f);
	return 0;
}
