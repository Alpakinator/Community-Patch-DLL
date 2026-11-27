// proxy_farm_guard.cpp - the farm builder guard on every 64-bit start (ticket 0535). See proxy_farm_guard.h.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The guard itself is shell/composite/farm_guard.cpp (this repository, ticket 0527 and its successors 4853-4855), compiled
// into the proxy unchanged by proxy/build_proxy.py; this file gives it its owner's frame: the shared page (one owner per
// process), the keys, the EXE check and the watcher thread. Only data is written into the EXE (the listener node's
// function word, under the EXE's own lock, D4); nothing of the EXE is in this file (D83).
// Toolchain: clang-cl -m32, VC9 headers, C++11 at most; Win32 primitives only.
#include "proxy_farm_guard.h"
#include "farm_guard.h"
#include "../../exebuild/include/vp_exe_build.h"  // ticket 4963: the build registry (relative: no generated header needed)
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

namespace ProxyRtInt  // proxy_rt_internal.h (declared here so that the offline unit test needs no generated header)
{
void Log(const char* fmt, ...);
}  // namespace ProxyRtInt

namespace
{
const DWORD kTickMs = 2;            // the watcher's period: the swap must land between the registration and the first dispatch
const DWORD kKeysEveryMs = 1000;    // the keys are read again this often
// The builds this owner hands to the guard: every known build (the registry, by PE stamp and SizeOfImage). Since ticket
// 4965 (epic 0590) shell/composite/farm_guard.cpp takes its addresses from the build table: its ReadyCheck resolves the
// build from the image again, needs every one of its symbols carried and its 13 byte patterns matching, and refuses
// LOUD otherwise ("REFUSED: ... the farm builder is not guarded"). So nothing is read or written in a build without a
// complete table here either; this check only keeps an unknown EXE from reaching the guard at all. (Until 2026-11-20
// this was build 4390913 only, which left the builder unguarded on build 26577625: found live by ticket 4967.)
bool GuardKnowsBuild(int build)
{
	return build != kVpExeBuildUnknown;
}

bool g_installed;
bool g_test;
FgWorld g_testWorld;
wchar_t g_outDir[MAX_PATH * 2];
wchar_t g_ini[MAX_PATH * 2];
volatile LONG g_composite;          // the composite is loaded (farm_builder = 0 acts only then)
volatile LONG g_farmGuard = 1, g_farmBuilder = 1;
volatile LONG g_farmEndPost = 1;
volatile LONG g_farmEdge = 1;       // ticket 0657: farm_edge (the test switch of the edge placement)
volatile LONG g_farmAreaProbe = 0;   // ticket 0655 (P1): farm_area_probe, a diagnostic (default 0)   // ticket 0655: farm_end_post (the test switch of the farm-end post)
volatile LONG g_treadFilter = 0;     // ticket 0784 (D268): tread_filter (proxy_shell.cpp sets it from [Renderer] HideTreadEffect)
DWORD g_keysTick;
int g_keysLogged = -1;
uint32_t g_exeBase;
int32_t g_exeValidated;

HANDLE g_hThread, g_hTimer;
volatile LONG g_stop, g_busy, g_ticks;

// A key as the composite reads it (shell_composite.cpp Config): VPSHELL_<KEY> first, then [composite] of the ini; an empty
// value counts as absent. *from: "env", "ini" or "default".
int ReadKey(const wchar_t* key, const wchar_t* envName, int def, const char** from)
{
	wchar_t v[64];
	DWORD n = GetEnvironmentVariableW(envName, v, 64);
	if (n > 0 && n < 64 && v[0]) { *from = "env"; return _wtoi(v) != 0 ? 1 : 0; }
	if (g_ini[0])
	{
		GetPrivateProfileStringW(L"composite", key, L"", v, 64, g_ini);
		if (v[0]) { *from = "ini"; return _wtoi(v) != 0 ? 1 : 0; }
	}
	*from = "default";
	return def;
}

void ReadKeys(bool force)
{
	const DWORD now = GetTickCount();
	if (!force && now - g_keysTick < kKeysEveryMs) return;
	g_keysTick = now;
	const char *fg = "", *fb = "", *fp = "";
	const int guard = ReadKey(L"farm_guard", L"VPSHELL_FARM_GUARD", 1, &fg);
	const int builder = ReadKey(L"farm_builder", L"VPSHELL_FARM_BUILDER", 1, &fb);
	const int post = ReadKey(L"farm_end_post", L"VPSHELL_FARM_END_POST", 1, &fp);   // ticket 0655
	const char* fa = "";
	const int probe = ReadKey(L"farm_area_probe", L"VPSHELL_FARM_AREA_PROBE", 0, &fa);   // ticket 0655 (P1), a diagnostic
	const char* fe = "";
	const int edge = ReadKey(L"farm_edge", L"VPSHELL_FARM_EDGE", 1, &fe);   // ticket 0657
	const char* ft = "";
	const int tread = ReadKey(L"tread_filter", L"VPSHELL_TREAD_FILTER", 0, &ft);   // ticket 0784
	InterlockedExchange(&g_treadFilter, tread);
	InterlockedExchange(&g_farmEdge, edge);
	InterlockedExchange(&g_farmAreaProbe, probe);
	InterlockedExchange(&g_farmGuard, guard);
	InterlockedExchange(&g_farmBuilder, builder);
	InterlockedExchange(&g_farmEndPost, post);
	const int skip = builder == 0 && g_composite != 0;
	const int treadOn = tread != 0 && g_composite != 0;
	const int code = treadOn * 128 + tread * 64 + edge * 32 + probe * 16 + post * 8 + guard * 4 + builder * 2 + skip;
	if (code != g_keysLogged)
	{
		g_keysLogged = code;
		ProxyRtInt::Log("farm_guard: proxy keys: farm_guard=%d (%s), farm_builder=%d (%s), composite %s: the builder is %s; "
		                "farm_end_post=%d (%s)%s%s; farm_edge=%d (%s)%s; tread_filter=%d (%s)%s",
		                guard, fg, builder, fb, g_composite ? "loaded" : "not loaded",
		                skip ? "never called (the server lays out the farm fields)"
		                     : builder == 0 ? "called through the guard (farm_builder=0 acts only with the composite)"
		                                    : guard ? "called through the guard" : "called by its own listener word (farm_guard=0)",
		                post, fp,
		                !skip ? "" : post ? ": the farm-end post makes the farm buildings (ticket 0655)"
		                                  : ": no farm-end post, no farm building (the test switch of ticket 0655)",
		                probe ? "; farm_area_probe=1 (a DIAGNOSTIC: the area listener is timed per call)" : "", edge, fe,
		                !skip ? "" : edge ? ": the buildings go to the server's points (ticket 0657)"
		                                  : ": no edge pass-through, every building at its plot centre (the test switch of ticket 0657)",
		                tread, ft,
		                treadOn ? ": the EXE's two landship tread effects are dropped (ticket 0784)"
		                        : tread ? ": no effect without the composite (the game draws its own units)" : "");
	}
}

// The EXE's build (PE time stamp and SizeOfImage through the registry, ticket 4963; the composite validates the EXE
// before the guard reads it; the proxy checks this much, and the guard's 13 patterns do the rest).
int32_t ValidateExe(uint32_t base)
{
	__try
	{
		const unsigned char* b = (const unsigned char*)(uintptr_t)base;
		if (!b || b[0] != 'M' || b[1] != 'Z') return -1;
		const uint32_t lfanew = *(const uint32_t*)(b + 0x3C);
		if (lfanew > 0x1000 || *(const uint32_t*)(b + lfanew) != 0x4550u) return -1;
		const int build = VpExeBuildFromHeader(*(const uint32_t*)(b + lfanew + 8), *(const uint32_t*)(b + lfanew + 24 + 56));
		return GuardKnowsBuild(build) ? 1 : -1;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

void Tick()
{
	ReadKeys(false);
	FarmGuardEnv e;
	memset(&e, 0, sizeof(e));
	e.exeBase = g_exeBase;
	e.exeValidated = g_exeValidated;
	e.enabled = g_farmGuard != 0;
	e.outDir = g_outDir;
	e.log = ProxyRtInt::Log;
	e.skipBuilder = (g_farmBuilder == 0 && g_composite != 0) ? 1 : 0;
	e.farmEndPostOff = g_farmEndPost == 0 ? 1 : 0;   // ticket 0655
	e.areaProbe = g_farmAreaProbe != 0 ? 1 : 0;      // ticket 0655 (P1)
	e.farmEdgeOff = g_farmEdge == 0 ? 1 : 0;          // ticket 0657
	e.treadFilter = (g_treadFilter != 0 && g_composite != 0) ? 1 : 0;   // ticket 0784: only while the server draws (the composite)
	FarmGuard_OnTick(e);
	InterlockedIncrement(&g_ticks);
}

HANDLE CreatePeriodTimer()
{
	typedef HANDLE(WINAPI * CreateTimerExFn)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);
	CreateTimerExFn create = (CreateTimerExFn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreateWaitableTimerExW");
	if (!create) return NULL;
	const DWORD kHighResolution = 0x00000002;  // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
	const DWORD kTimerAllAccess = 0x001F0003;  // TIMER_ALL_ACCESS
	return create(NULL, NULL, kHighResolution, kTimerAllAccess);
}

void WaitPeriod()
{
	if (g_hTimer)
	{
		LARGE_INTEGER due;
		due.QuadPart = -(LONGLONG)kTickMs * 10000;
		if (SetWaitableTimer(g_hTimer, &due, 0, NULL, NULL, FALSE) && WaitForSingleObject(g_hTimer, 1000) == WAIT_OBJECT_0) return;
	}
	Sleep(kTickMs);
}

DWORD WINAPI WatchThread(LPVOID)
{
	// Never returns: OnUnload terminates it while it is outside its work (g_busy == 0): the EXE's registration lock is
	// never held then, and no CRT lock of a log line.
	for (;;)
	{
		InterlockedExchange(&g_busy, 1);
		if (g_stop == 0) Tick();
		InterlockedExchange(&g_busy, 0);
		WaitPeriod();
	}
}

void StartWatcher()
{
	g_stop = 0;
	g_hTimer = CreatePeriodTimer();
	DWORD tid = 0;
	g_hThread = CreateThread(NULL, 64 * 1024, WatchThread, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, &tid);
	if (g_hThread)
		ProxyRtInt::Log("farm_guard: proxy watcher thread %lu every %lu ms (%s): the swap of the builder's listener node, the burst lines",
		                tid, kTickMs, g_hTimer ? "high-resolution timer" : "Sleep, up to ~16 ms");
	else
	{
		ProxyRtInt::Log("farm_guard: LOUD: the proxy's watcher thread could not be created (error %lu): the farm builder is not "
		                "guarded in this process", GetLastError());
		if (g_hTimer) CloseHandle(g_hTimer);
		g_hTimer = NULL;
	}
}

// Under the loader lock (or from a test): no logging.
void StopWatcher()
{
	if (!g_hThread) return;
	InterlockedExchange(&g_stop, 1);
	for (int i = 0; i < 500 && g_busy != 0; ++i) Sleep(1);   // a tick takes microseconds; a log line, a few ms
	TerminateThread(g_hThread, 0);
	WaitForSingleObject(g_hThread, 1000);   // a terminated thread is signalled at once
	CloseHandle(g_hThread);
	g_hThread = NULL;
	if (g_hTimer) CloseHandle(g_hTimer);
	g_hTimer = NULL;
}

void Start(const wchar_t* outDir, const wchar_t* iniPath)
{
	g_installed = true;
	wcsncpy_s(g_outDir, outDir ? outDir : L"", _TRUNCATE);
	wcsncpy_s(g_ini, iniPath ? iniPath : L"", _TRUNCATE);
	char why[400] = "";
	if (!FarmGuard_OwnerAttach("proxy", why, sizeof(why)))
	{
		ProxyRtInt::Log("farm_guard: LOUD: the proxy could not attach the guard (%s): the farm builder is not guarded in this "
		                "process, and a composite installs its own", why);
		return;
	}
	ProxyRtInt::Log("farm_guard: owner proxy (ticket 0535): %s", why);
	g_keysLogged = -1;
	ReadKeys(true);
	StartWatcher();
}
}  // namespace

namespace ProxyFarmGuard
{
void Install(const wchar_t* outDir, const wchar_t* iniPath)
{
	if (g_installed) return;
	g_exeBase = (uint32_t)(uintptr_t)GetModuleHandleW(NULL);
	g_exeValidated = ValidateExe(g_exeBase);
	if (g_exeValidated < 0)
		ProxyRtInt::Log("farm_guard: the EXE at 0x%08X is not an EXE build the guard's patterns describe (known: %s and %s, "
		                "by PE time stamp and SizeOfImage, exebuild/include/vp_exe_build.h): the guard attaches but reads and "
		                "writes nothing in it", g_exeBase, VpExeBuildInfoOf(kVpExeBuild4390913)->label,
		                VpExeBuildInfoOf(kVpExeBuild26577625)->label);
	Start(outDir, iniPath);
}

void SetComposite(bool loaded)
{
	InterlockedExchange(&g_composite, loaded ? 1 : 0);
	if (g_installed) ReadKeys(true);
}

void OnUnload()
{
	StopWatcher();
	FarmGuard_OwnerDetach();
	g_installed = false;
}

namespace Test
{
void InstallWorld(const void* fgWorld, const wchar_t* recordDir, const wchar_t* iniPath)
{
	g_test = true;
	g_testWorld = *(const FgWorld*)fgWorld;
	FarmGuard_TestUse(&g_testWorld, recordDir);
	g_exeBase = 0;
	g_exeValidated = 0;
	Start(recordDir, iniPath);
}
void Keys(int* farmGuard, int* farmBuilder, int* skip)
{
	*farmGuard = g_farmGuard;
	*farmBuilder = g_farmBuilder;
	*skip = g_farmBuilder == 0 && g_composite != 0;
}
int FarmEndPost() { return (int)g_farmEndPost; }
int FarmEdge() { return (int)g_farmEdge; }
int TreadFilter() { return (int)(g_treadFilter != 0 && g_composite != 0); }
unsigned long Ticks() { return (unsigned long)g_ticks; }
void Unload() { OnUnload(); }
int ValidateImage(const void* image) { return (int)ValidateExe((uint32_t)(uintptr_t)image); }
}  // namespace Test
}  // namespace ProxyFarmGuard

#ifndef PROXY_FARM_GUARD_NO_SHELL
// The registration with proxy_shell.cpp (ProxyShell::g_farmGuardHooks; a program without this file has none).
#include "proxy_shell.h"
namespace
{
const ProxyShell::FarmGuardHooks kHooks = { ProxyFarmGuard::Install, ProxyFarmGuard::SetComposite, ProxyFarmGuard::OnUnload };
struct Register
{
	Register() { ProxyShell::g_farmGuardHooks = &kHooks; }
} g_register;
}  // namespace
#endif
