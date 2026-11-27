// cut_tap_host.cpp - test entry of the cut-mask tap (ticket 4553): a console program that links composite/cut_tap.cpp and
// calls CutTap_OnPresent as the composite's Present hook does, without D3D (the tap uses none). Written 2026-11-16 for this
// repository (GPL-3.0-or-later), after forest_tap_host.cpp. Used by shell/tests/test_cut_tap.py; never loaded into the
// game. The tap reads its keys from the environment (VPSHELL_CUT_*) or shell_composite.ini beside this exe.
//
//   cut_tap_host.exe --seconds S --out DIR [--period-ms 5] [--exe self|none] [--unvalidated] [--grid 0|1] --log FILE
// --exe self: the real path, with this program's own image as "the EXE" (no pattern matches: refused for good);
// --exe none (default): exeBase 0 (with cut_test set, the synthetic grid). A file `cut_tap.off` beside the exe is the
// switch off (the composite's cut_tap=0); `cut_tap.grid` holding a number sets gridSeq (a new load); `cut_tap.nomap`
// clears gridValid while it exists. Every Present the host reads its own control block (Local\VPShellCutTap.<pid>, as the
// server does) and, for each new generation, writes DIR\control_<generation>.bin (the block) and DIR\image_<generation>.bin
// (the image section's imageBytes); DIR\control_last.bin at the end. Exit 0 after S seconds.
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <share.h>
#include "cut_tap.h"

static FILE* g_f;
static LARGE_INTEGER g_t0, g_freq;
static CRITICAL_SECTION g_lock;

static void Log(const char* fmt, ...)
{
	if (!g_f) return;
	EnterCriticalSection(&g_lock);
	LARGE_INTEGER t; QueryPerformanceCounter(&t);
	fprintf(g_f, "[%9.3f] [%5lu] ", (double)(t.QuadPart - g_t0.QuadPart) / (double)g_freq.QuadPart, GetCurrentThreadId());
	va_list ap; va_start(ap, fmt); vfprintf(g_f, fmt, ap); va_end(ap);
	fputc('\n', g_f);
	fflush(g_f);
	LeaveCriticalSection(&g_lock);
}

static bool WriteAll(const wchar_t* path, const void* p, size_t n)
{
	FILE* f = nullptr;
	if (_wfopen_s(&f, path, L"wb") || !f) return false;
	const bool ok = fwrite(p, 1, n, f) == n;
	fclose(f);
	return ok;
}

// A consistent copy of the control block (the seqlock), or false.
static bool SnapControl(const VPShellCutTapControl* src, VPShellCutTapControl* out)
{
	for (int i = 0; i < 64; ++i)
	{
		const uint32_t s1 = src->seq;
		if (s1 == 0) return false;
		if (s1 & 1) { Sleep(0); continue; }
		MemoryBarrier();
		memcpy(out, (const void*)src, sizeof(*out));
		MemoryBarrier();
		if (src->seq == s1) return true;
	}
	return false;
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
	InitializeCriticalSection(&g_lock);
	g_f = _wfsopen(logPath, L"a", _SH_DENYNO);   // shared: the test reads it while the host runs
	if (!g_f) return 3;
	QueryPerformanceFrequency(&g_freq); QueryPerformanceCounter(&g_t0);
	wchar_t dir[MAX_PATH]; GetModuleFileNameW(nullptr, dir, MAX_PATH);
	wchar_t* sl = wcsrchr(dir, L'\\'); if (sl) sl[1] = 0;
	wchar_t off[MAX_PATH], gridf[MAX_PATH], nomap[MAX_PATH];
	_snwprintf_s(off, _TRUNCATE, L"%scut_tap.off", dir);
	_snwprintf_s(gridf, _TRUNCATE, L"%scut_tap.grid", dir);
	_snwprintf_s(nomap, _TRUNCATE, L"%scut_tap.nomap", dir);
	uint32_t gridSeq = 1;
	Log("cut_tap_host: pid %lu, %.1f s, period %d ms, exe %s, validated %d", GetCurrentProcessId(), seconds, period,
		self ? "self" : "none", validated ? 1 : 0);
	wchar_t ctlName[64]; _snwprintf_s(ctlName, _TRUNCATE, VPSHELL_CUTTAP_CONTROL_FMT, GetCurrentProcessId());
	HANDLE ctl = nullptr; const VPShellCutTapControl* blk = nullptr;
	uint32_t lastGen = 0;
	double wallMax = 0, wallSum = 0;
	const ULONGLONG end = GetTickCount64() + (ULONGLONG)(seconds * 1000);
	unsigned frames = 0;
	while (GetTickCount64() < end)
	{
		FILE* g = nullptr;
		if ((g = _wfsopen(gridf, L"r", _SH_DENYNO)) != nullptr) { unsigned s = 0; if (fscanf_s(g, "%u", &s) == 1 && s) gridSeq = s; fclose(g); }
		CutTapEnv env = {};
		env.exeBase = self ? (uint32_t)(uintptr_t)GetModuleHandleW(nullptr) : 0;
		env.exeValidated = validated ? 1u : 0u;
		env.gridValid = grid && GetFileAttributesW(nomap) == INVALID_FILE_ATTRIBUTES ? 1u : 0u;
		env.gridSeq = gridSeq;
		env.enabled = GetFileAttributesW(off) == INVALID_FILE_ATTRIBUTES;
		env.log = Log;
		LARGE_INTEGER w0, w1;
		QueryPerformanceCounter(&w0);
		CutTap_OnPresent(env);
		QueryPerformanceCounter(&w1);
		const double wall = (double)(w1.QuadPart - w0.QuadPart) * 1000.0 / (double)g_freq.QuadPart;
		if (frames && wall > wallMax) wallMax = wall;
		wallSum += wall;
		frames++;
		// the server's side, as a reader would see it
		if (!blk)
		{
			ctl = OpenFileMappingW(FILE_MAP_READ, FALSE, ctlName);
			if (ctl) blk = (const VPShellCutTapControl*)MapViewOfFile(ctl, FILE_MAP_READ, 0, 0, VPSHELL_CUTTAP_CONTROL_BYTES);
		}
		VPShellCutTapControl c;
		if (blk && SnapControl(blk, &c) && c.state == VPCUT_READY && c.generation != lastGen)
		{
			lastGen = c.generation;
			wchar_t p[MAX_PATH];
			_snwprintf_s(p, _TRUNCATE, L"%ls\\control_%u.bin", outArg, c.generation);
			WriteAll(p, &c, sizeof(c));
			HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, c.imageName);
			const void* v = h ? MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0) : nullptr;
			if (v)
			{
				_snwprintf_s(p, _TRUNCATE, L"%ls\\image_%u.bin", outArg, c.generation);
				WriteAll(p, v, (size_t)c.imageBytes);
				UnmapViewOfFile(v);
				Log("cut_tap_host: generation %u read (%llu bytes)", c.generation, (unsigned long long)c.imageBytes);
			}
			else Log("cut_tap_host: generation %u: the image '%ls' does not open (error %lu)", c.generation, c.imageName, GetLastError());
			if (h) CloseHandle(h);
		}
		Sleep(period);
	}
	CutTap_StopForTest(5000);
	if (blk)
	{
		VPShellCutTapControl c;
		if (SnapControl(blk, &c))
		{
			wchar_t p[MAX_PATH]; _snwprintf_s(p, _TRUNCATE, L"%ls\\control_last.bin", outArg);
			WriteAll(p, &c, sizeof(c));
		}
		UnmapViewOfFile(blk);
	}
	if (ctl) CloseHandle(ctl);
	Log("cut_tap_host: %u Presents, wall max %.3f ms (after the first), mean %.4f ms", frames, wallMax, frames ? wallSum / frames : 0.0);
	fclose(g_f);
	return 0;
}
