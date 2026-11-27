// forest_tap_host.cpp - test entry of the forest tap (ticket 0420): a console program that links composite/forest_tap.cpp
// and calls ForestTap_OnPresent as the composite's Present hook does, without D3D (the tap uses none). Written 2026-11-16
// for this repository (GPL-3.0-or-later), after decal_tap_host.cpp. Used by shell/tests/test_forest_tap.py; never loaded
// into the game. The tap reads its keys from the environment (VPSHELL_FOREST_*) or shell_composite.ini beside this exe.
//
//   forest_tap_host.exe --seconds S --out DIR [--period-ms 5] [--exe self|none] [--unvalidated] [--grid 0|1] --log FILE
// --exe self: the real path, with this program's own image as "the EXE" (no pattern matches: refused for good);
// --exe none (default): exeBase 0 (with forest_test set, the synthetic Forest object). A file `forest_tap.off` beside the
// exe is the switch off (the composite's forest_tap=0); `forest_tap.grid` holding a number sets gridSeq (a new load);
// `forest_tap.shutdown` (deleted when seen) calls ForestTap_Shutdown as the composite's resource release does.
// Exit 0 after S seconds; the last line is the cost of the calls (CPU and wall, maximum and mean).
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <share.h>
#include "forest_tap.h"

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
	double seconds = 3; int period = 5; bool self = false, validated = true; int grid = 1;
	const wchar_t* logPath = nullptr; const wchar_t* outArg = nullptr;
	for (int i = 1; i < argc; ++i)
	{
		if (!wcscmp(argv[i], L"--seconds") && i + 1 < argc) seconds = _wtof(argv[++i]);
		else if (!wcscmp(argv[i], L"--period-ms") && i + 1 < argc) period = _wtoi(argv[++i]);
		else if (!wcscmp(argv[i], L"--exe") && i + 1 < argc) self = !wcscmp(argv[++i], L"self");
		else if (!wcscmp(argv[i], L"--unvalidated")) validated = false;
		else if (!wcscmp(argv[i], L"--grid") && i + 1 < argc) grid = _wtoi(argv[++i]);
		else if (!wcscmp(argv[i], L"--log") && i + 1 < argc) logPath = argv[++i];
		else if (!wcscmp(argv[i], L"--out") && i + 1 < argc) outArg = argv[++i];
		else { fwprintf(stderr, L"unknown argument %ls\n", argv[i]); return 2; }
	}
	if (!logPath || !outArg) { fwprintf(stderr, L"--log and --out required\n"); return 2; }
	wchar_t outDir[MAX_PATH];
	_snwprintf_s(outDir, _TRUNCATE, L"%ls%ls", outArg, outArg[wcslen(outArg) - 1] == L'\\' ? L"" : L"\\");
	g_f = _wfsopen(logPath, L"a", _SH_DENYNO);   // shared: the test reads it while the host runs
	if (!g_f) return 3;
	QueryPerformanceFrequency(&g_freq); QueryPerformanceCounter(&g_t0);
	wchar_t dir[MAX_PATH]; GetModuleFileNameW(nullptr, dir, MAX_PATH);
	wchar_t* sl = wcsrchr(dir, L'\\'); if (sl) sl[1] = 0;
	wchar_t off[MAX_PATH], gridf[MAX_PATH], shut[MAX_PATH];
	_snwprintf_s(off, _TRUNCATE, L"%sforest_tap.off", dir);
	_snwprintf_s(gridf, _TRUNCATE, L"%sforest_tap.grid", dir);
	_snwprintf_s(shut, _TRUNCATE, L"%sforest_tap.shutdown", dir);
	static VPShellCamera cam = {};
	cam.gridValid = grid ? 1u : 0u; cam.gridSeq = 1;
	Log("forest_tap_host: pid %lu, %.1f s, period %d ms, exe %s, validated %d", GetCurrentProcessId(), seconds, period,
		self ? "self" : "none", validated ? 1 : 0);
	double wallMax = 0, wallSum = 0;
	const ULONGLONG end = GetTickCount64() + (ULONGLONG)(seconds * 1000);
	unsigned frames = 0;
	while (GetTickCount64() < end)
	{
		FILE* g = nullptr;
		if ((g = _wfsopen(gridf, L"r", _SH_DENYNO)) != nullptr) { unsigned s = 0; if (fscanf_s(g, "%u", &s) == 1 && s) cam.gridSeq = s; fclose(g); }
		if (GetFileAttributesW(shut) != INVALID_FILE_ATTRIBUTES) { DeleteFileW(shut); ForestTap_Shutdown("host: forest_tap.shutdown"); }
		ForestTapEnv env = {};
		env.exeBase = self ? (uint32_t)(uintptr_t)GetModuleHandleW(nullptr) : 0;
		env.exeValidated = validated ? 1u : 0u;
		env.cam = &cam;
		env.enabled = GetFileAttributesW(off) == INVALID_FILE_ATTRIBUTES;
		env.outDir = outDir;
		env.log = Log;
		LARGE_INTEGER w0, w1;
		QueryPerformanceCounter(&w0);
		ForestTap_OnPresent(env);
		QueryPerformanceCounter(&w1);
		const double wall = (double)(w1.QuadPart - w0.QuadPart) * 1000.0 / (double)g_freq.QuadPart;
		if (frames && wall > wallMax) wallMax = wall;
		wallSum += wall;
		frames++;
		Sleep(period);
	}
	ForestTap_Shutdown("host exit");
	Log("forest_tap_host: %u Presents, wall max %.3f ms (after the first), mean %.4f ms", frames, wallMax, frames ? wallSum / frames : 0.0);
	fclose(g_f);
	return 0;
}
