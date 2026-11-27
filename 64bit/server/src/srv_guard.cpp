// srv_guard.cpp - the window guard of server.exe (issue 2501).
//
// Why: the server (x64) is a child process of the game EXE (the proxy starts it with CreateProcessA, CREATE_NO_WINDOW,
// inheriting the EXE's environment) and runs the DLL, whose CvPreconditionDlg shows a NULL-owner MessageBoxA on the
// game-core thread. On 2026-11-10 such a box of the DLL stood uncloaked on the owner's desktop for eight minutes (issue
// 1118, then inside the EXE). The EXE's guard (tools/harness/native/src/focus_guard.cpp, 32-bit) knows nothing of
// another process, so the server carries its own.
//
// Design: an x64 port of the 1118 approach. Every window of every thread is created by win32u!NtUserCreateWindowEx
// and shown by win32u!NtUserShowWindow[Async], whatever user32 path (CreateWindowEx, MessageBox, DialogBox,
// CreateDialog) led there; foreground and activation requests of MessageBox's MB_SETFOREGROUND and system-modal
// paths end in win32u!NtUserSetForegroundWindow / NtUserSetActiveWindow. The guard hooks these syscall stubs in the
// process' own win32u.dll (x64 shape checked byte for byte before patching: 4C 8B D1 B8 imm32 F6 04 25 08 03 FE 7F
// 01 75 03 0F 05 C3 CD 2E C3; trampoline = a verbatim copy of the 24 bytes, position independent), so it covers every
// thread, including threads the DLL creates later, without per-thread setup. While the guard is enabled:
//   - NtUserCreateWindowEx, top-level window (not WS_CHILD, not message-only), any class, owner and thread: a WH_CBT
//     hook (activation veto) is put on the creating thread first; WS_VISIBLE is taken out of the style; after the
//     creation the window is cloaked (DWMWA_CLOAK) and, if it asked for WS_VISIBLE, shown with SW_SHOWNA, cloaked;
//   - NtUserShowWindow / NtUserShowWindowAsync: an uncloaked own top-level window is cloaked before it is shown, and
//     every activating show command becomes its non-activating form;
//   - NtUserSetForegroundWindow and NtUserSetActiveWindow: always refused (the server never takes the foreground);
//   - WH_CBT HCBT_ACTIVATE on every thread that created a window: activation vetoed;
//   - a watchdog thread sweeps the process' top-level windows every 100 ms and cloaks any visible uncloaked one
//     (counted in win_cloaked_watchdog: non-zero means a path above was missed);
//   - DisableProcessWindowsGhosting(): a hung window gets no DWM ghost window (another process, uncloakable).
// Nothing here writes to the EXE: this is the server's own image and its own win32u copy (CEG concerns the 32-bit EXE).
//
// Shared memory Local\CivBgGuard_<pid> (4096 bytes): the GuardShm layout of focus_guard.cpp, offsets fixed
// (tools/harness/native/README.md): magic 'CBG1' at 0, enabled at 8, the counters' slot 12 (ActivateVeto), status at
// 408, and the 1118 window counters at 1240..1264; since 2703 also 1268..1280 (9009) and guard_log at 1284 (9010);
// since 0475 the children's fields of 0473 at 1572..1596 (the guard follows the server's 32-bit children, see below).
// Log: %TEMP%\civbg_guard_<pid>_<creation time UTC YYYYMMDD-HHMMSS-mmm>.log (9010 naming; before 2703's follow-up
// it was civbg_guard_<pid>.log). The server never has a main window (main_hwnd stays 0).
//
// Fail closed: with CIVBG_GUARD=1, a missing shared memory, a hook that cannot be installed or a watchdog that cannot
// start makes SrvGuardInstall return SRV_GUARD_FAILED; main() then logs why and exits with SRV_GUARD_EXIT_CODE before
// loading anything.
//
// Issue 2703 (after one-off 9009, where the EXE's WH_CBT veto failed with error 126 and was only logged): the veto is
// counted (win_veto_ok / win_veto_failed at 1268 / 1272), every part is self-tested at install, including one WH_CBT
// installation on the installing thread, and the result is published as guard_state / guard_missing (1276 / 1280),
// with the meanings of focus_guard.cpp. A self-test failure (a part missing although the install itself went through)
// ends the server with SRV_GUARD_SELFTEST_EXIT_CODE (0xC5F00032) before any DLL; a veto that cannot be installed on a
// later thread (a window that could be activated) is counted, logged and ends the server with
// SRV_GUARD_VETO_EXIT_CODE (0xC5F00033) before that window exists. Test switch C5_GUARD_TEST_FAIL (as in 9009):
// "veto" every veto install fails (the self-test fails: exit 0xC5F00032), "veto-report" the same but the self-test
// only publishes guard_state -1 and the server runs on (the proxy's check is exercised), "veto-late" the self-test
// passes and every later veto install fails (exit 0xC5F00033 at the first top-level window).
#include "srv_guard.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <share.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

namespace
{
#pragma pack(push, 4)
	struct GuardShm
	{
		DWORD magic;					// 0: 'CBG1'
		DWORD version;					// 4: 1
		volatile LONG enabled;			// 8
		volatile LONG pretend_active;	// 12 (unused by the server)
		volatile LONG calls[16];		// 16 (slot 12 = ActivateVeto)
		volatile LONG altered[16];		// 80
		volatile DWORD main_hwnd;		// 144 (the server has none: 0)
		DWORD pid;						// 148
		volatile BYTE vk_down[256];		// 152 (unused by the server)
		char status[512];				// 408
		volatile LONG cloak;			// 920 (unused)
		volatile LONG cloak_applied;	// 924
		volatile LONG cloak_hr;			// 928
		volatile LONG isolate;			// 932 (unused)
		volatile LONG vcursor_on;		// 936 (unused)
		volatile LONG vcursor_x;		// 940
		volatile LONG vcursor_y;		// 944
		DWORD shim_magic;				// 948: 0 in the server (no folder shim)
		volatile LONG redirect_state;	// 952
		volatile LONG isolate_state;	// 956
		volatile LONG shim_requested;	// 960
		volatile LONG redirect_calls;	// 964
		volatile LONG isolate_calls;	// 968
		volatile LONG isolate_hidden;	// 972
		volatile LONG mods_total;		// 976
		volatile LONG mods_attached;	// 980
		char selftest[256];				// 984: the server writes "server.exe x64 guard (issue 2501)" here
		volatile LONG win_hooks;		// 1240: bit0 NtUserCreateWindowEx, bit1 NtUserShowWindow, bit2
										//       NtUserSetForegroundWindow, bit3 NtUserSetActiveWindow, bit4
										//       NtUserShowWindowAsync hooked; bit8 ghosting disabled; bit9 watchdog
		volatile LONG win_created;			// 1244
		volatile LONG win_cloaked_create;	// 1248
		volatile LONG win_cloaked_show;		// 1252
		volatile LONG win_cloaked_watchdog;	// 1256
		volatile LONG win_show_rewritten;	// 1260
		volatile LONG win_fg_blocked;		// 1264
		// ---- issue 2703: the fields of issue 9009, same offsets and meanings as focus_guard.cpp ----
		volatile LONG win_veto_ok;			// 1268: threads on which the WH_CBT activation veto was installed
		volatile LONG win_veto_failed;		// 1272: threads on which installing it failed (must stay 0)
		volatile LONG guard_state;			// 1276: 1 every part installed and verified, -1 a part is missing, 0 not run
		volatile LONG guard_missing;		// 1280: bit1 a win32u hook (win_hooks & 0x1F), bit2 the WH_CBT veto,
											//       bit3 the cloak API (dwmapi), bit5 the watchdog thread, bit6 the
											//       children (C5_CHILD_GUARD set, not covered; issue 0475). bit0
											//       (user32 hooks) and bit4 (private shared memory) do not apply
		// ---- issue 9010 (focus_guard.cpp), taken over by 2703: one guard log per process ----
		char guard_log[260];				// 1284: full path of this process' guard log, NUL-terminated (empty when
											//       it could not be opened): %TEMP%\civbg_guard_<pid>_<creation time
											//       UTC YYYYMMDD-HHMMSS-mmm>.log
		volatile LONG framecap[7];			// 1544..1568: the EXE's frame cap (issue 0157); unused by the server: 0
		// ---- issue 0475: the fields of issue 0473 (focus_guard.cpp), same offsets and meanings ----
		volatile LONG child_guard;			// 1572: 1 kernelbase!CreateProcessInternalW hooked and C5_CHILD_GUARD names
											//       the 32-bit guard DLL with bg_helper32.exe beside it; 0 not
											//       requested; -1 requested, not installed (guard_missing bit6)
		volatile DWORD child_parent;		// 1576: in a CHILD's section: the pid of the guarded process that started it
		volatile LONG child_seen;			// 1580: processes created while the guard was on
		volatile LONG child_guarded;		// 1584: ... of which run with the guard (guard_state 1 read back first)
		volatile LONG child_other;			// 1588: ... of which are not 32-bit (not guarded)
		volatile LONG child_failed;			// 1592: ... of which could not be guarded: terminated before their first
											//       instruction, CreateProcess failed with ERROR_DLL_INIT_FAILED
		volatile DWORD child_last_pid;		// 1596: the last child guarded
	};
#pragma pack(pop)
	static_assert(offsetof(GuardShm, enabled) == 8 && offsetof(GuardShm, calls) == 16, "layout (native/README.md)");
	static_assert(offsetof(GuardShm, status) == 408 && offsetof(GuardShm, shim_magic) == 948, "layout (native/README.md)");
	static_assert(offsetof(GuardShm, selftest) == 984, "layout (native/README.md)");
	static_assert(offsetof(GuardShm, win_hooks) == 1240 && offsetof(GuardShm, win_fg_blocked) == 1264, "layout (README)");
	static_assert(offsetof(GuardShm, win_veto_ok) == 1268 && offsetof(GuardShm, win_veto_failed) == 1272, "layout (README)");
	static_assert(offsetof(GuardShm, guard_state) == 1276 && offsetof(GuardShm, guard_missing) == 1280, "layout (README)");
	static_assert(offsetof(GuardShm, guard_log) == 1284, "layout (README, issue 9010)");
	static_assert(offsetof(GuardShm, child_guard) == 1572 && offsetof(GuardShm, child_last_pid) == 1596,
	              "layout (README, issues 0473 and 0475)");
	static_assert(sizeof(GuardShm) <= 4096, "section is 4096 bytes");

	const DWORD kMagic = 0x31474243;	// 'CBG1'
	const int kSlotActivateVeto = 12;

	GuardShm* g_shm = NULL;
	FILE* g_log = NULL;
	void (*g_sink)(const char*) = NULL;	// server.log, once main() has opened it (SrvGuardSetLogSink)
	unsigned long g_exitCode = SRV_GUARD_EXIT_CODE;
	CRITICAL_SECTION g_csLog, g_csHooked, g_csSecondary;
	BYTE* g_tramp = NULL;
	int g_trampUsed = 0;

	void glog(const char* szFormat, ...)
	{
		if(!g_log)
			return;
		char sz[1024];
		va_list v;
		va_start(v, szFormat);
		vsnprintf(sz, sizeof(sz), szFormat, v);
		va_end(v);
		SYSTEMTIME st;
		GetLocalTime(&st);
		EnterCriticalSection(&g_csLog);
		fprintf(g_log, "%04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu tid=%lu server-guard %s\n", st.wYear, st.wMonth, st.wDay,
		        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentProcessId(), GetCurrentThreadId(), sz);
		fflush(g_log);
		LeaveCriticalSection(&g_csLog);
	}

	bool On() { return g_shm && g_shm->enabled; }

	bool OwnTopLevel(HWND h)
	{
		DWORD pid = 0;
		if(!h || !GetWindowThreadProcessId(h, &pid) || pid != GetCurrentProcessId())
			return false;
		return (GetWindowLongPtrW(h, GWL_STYLE) & WS_CHILD) == 0;
	}

	typedef HRESULT(WINAPI* PFN_DWMSET)(HWND, DWORD, LPCVOID, DWORD);
	typedef HRESULT(WINAPI* PFN_DWMGET)(HWND, DWORD, PVOID, DWORD);
	PFN_DWMSET s_dwmSet = NULL;
	PFN_DWMGET s_dwmGet = NULL;

	bool IsCloaked(HWND h)
	{
		DWORD v = 0;
		return s_dwmGet && SUCCEEDED(s_dwmGet(h, 14 /* DWMWA_CLOAKED */, &v, sizeof(v))) && v != 0;
	}

	HWND s_secondary[512];
	int s_nSecondary = 0;

	void Cloak(HWND h, volatile LONG* pCounter, const char* szWhere)
	{
		BOOL on = TRUE;
		const HRESULT hr = s_dwmSet ? s_dwmSet(h, 13 /* DWMWA_CLOAK */, &on, sizeof(on)) : E_FAIL;
		InterlockedIncrement(pCounter);
		EnterCriticalSection(&g_csSecondary);
		bool have = false;
		for(int i = 0; i < s_nSecondary; ++i)
			have = have || s_secondary[i] == h;
		if(!have)
		{
			if(s_nSecondary == 512)
			{
				int k = 0;
				for(int i = 0; i < s_nSecondary; ++i)
					if(IsWindow(s_secondary[i]))
						s_secondary[k++] = s_secondary[i];
				s_nSecondary = k;
			}
			if(s_nSecondary < 512)
				s_secondary[s_nSecondary++] = h;
		}
		LeaveCriticalSection(&g_csSecondary);
		char cls[64] = "";
		GetClassNameA(h, cls, sizeof(cls));
		wchar_t wt[96] = L"";
		InternalGetWindowText(h, wt, 96);
		char title[192] = "";
		WideCharToMultiByte(CP_UTF8, 0, wt, -1, title, sizeof(title), NULL, NULL);
		glog("cloaked %p (%s, class %s, title \"%s\", thread %lu) hr 0x%08lx", (void*)h, szWhere, cls, title,
		     GetWindowThreadProcessId(h, NULL), (unsigned long)hr);
	}

	void ReleaseSecondaries()
	{
		EnterCriticalSection(&g_csSecondary);
		for(int i = 0; i < s_nSecondary; ++i)
			if(IsWindow(s_secondary[i]) && s_dwmSet)
			{
				BOOL off = FALSE;
				s_dwmSet(s_secondary[i], 13, &off, sizeof(off));
				glog("uncloaked %p (guard switched off)", (void*)s_secondary[i]);
			}
		s_nSecondary = 0;
		LeaveCriticalSection(&g_csSecondary);
	}

	// ---- the activation veto (WH_CBT), per thread that owns a window ----
	DWORD s_hookedTids[256];
	int s_nHooked = 0;

	LRESULT CALLBACK CbtProc(int code, WPARAM w, LPARAM l)
	{
		if(code == HCBT_ACTIVATE && On())
		{
			InterlockedIncrement(&g_shm->calls[kSlotActivateVeto]);
			InterlockedIncrement(&g_shm->altered[kSlotActivateVeto]);
			glog("vetoed activation of %p (HCBT_ACTIVATE)", (void*)w);
			return 1;
		}
		return CallNextHookEx(NULL, code, w, l);
	}

	// Issue 2703: C5_GUARD_TEST_FAIL (the switch of issue 9009, focus_guard.cpp, plus "veto-late"). Read once.
	enum { kTestOff = 0, kTestVeto = 1, kTestVetoReport = 2, kTestVetoLate = 3 };
	int s_testMode = -1;
	int TestMode()
	{
		if(s_testMode < 0)
		{
			char v[16] = "";
			GetEnvironmentVariableA("C5_GUARD_TEST_FAIL", v, sizeof(v));
			s_testMode = !strcmp(v, "veto") ? kTestVeto : !strcmp(v, "veto-report") ? kTestVetoReport
			           : !strcmp(v, "veto-late") ? kTestVetoLate : kTestOff;
		}
		return s_testMode;
	}
	const char* TestModeName()
	{
		static const char* const n[] = {"", ", C5_GUARD_TEST_FAIL=veto", ", C5_GUARD_TEST_FAIL=veto-report",
		                                ", C5_GUARD_TEST_FAIL=veto-late"};
		return n[TestMode()];
	}

	// One WH_CBT installation (hMod NULL: the hook procedure is in this process and the thread belongs to it).
	// bSelfTest: the install-time test (C5_GUARD_TEST_FAIL veto / veto-report fail it, veto-late does not).
	HHOOK InstallVeto(DWORD tid, bool bSelfTest, DWORD* pErr)
	{
		const int m = TestMode();
		const bool force = m == kTestVeto || m == kTestVetoReport || (m == kTestVetoLate && !bSelfTest);
		HHOOK hk = force ? NULL : SetWindowsHookExW(WH_CBT, CbtProc, NULL, tid);
		*pErr = hk ? 0 : force ? ERROR_MOD_NOT_FOUND : GetLastError();
		return hk;
	}

	// Ends the server: a guarded process must never run with a window that can be activated (issue 2703).
	void FailClosed(unsigned long code, const char* szWhy)
	{
		char line[600];
		_snprintf_s(line, sizeof(line), _TRUNCATE, "server: FAILED: window guard: %s; exit 0x%08lX (issue 2703)", szWhy, code);
		glog("%s", line);
		fprintf(stderr, "%s\n", line);
		if(g_sink)
			g_sink(line);
		if(g_log)
			fflush(g_log);
		TerminateProcess(GetCurrentProcess(), code);
	}

	void EnsureThreadHooked(DWORD tid)
	{
		EnterCriticalSection(&g_csHooked);
		bool have = false;
		for(int i = 0; i < s_nHooked && !have; ++i)
			have = s_hookedTids[i] == tid;
		if(!have && s_nHooked < 256)
		{
			DWORD err = 0;
			HHOOK hk = InstallVeto(tid, false, &err);
			s_hookedTids[s_nHooked++] = tid;	// recorded even on failure: not retried on every call
			InterlockedIncrement(hk ? &g_shm->win_veto_ok : &g_shm->win_veto_failed);
			glog("activation veto (WH_CBT) on thread %lu: %s (error %lu%s)", tid, hk ? "ok" : "FAILED", err, TestModeName());
			if(!hk)
			{
				// Issue 2703: a window on this thread could be activated (the incident of 9009). Publish, then end
				// the server before the window exists (the caller is the NtUserCreateWindowEx detour or the watchdog).
				InterlockedOr(&g_shm->guard_missing, 0x04);
				InterlockedExchange(&g_shm->guard_state, -1);
				char why[160];
				_snprintf_s(why, sizeof(why), _TRUNCATE, "the WH_CBT activation veto could not be installed on thread "
				            "%lu (error %lu%s)", tid, err, TestModeName());
				FailClosed(SRV_GUARD_VETO_EXIT_CODE, why);
			}
		}
		LeaveCriticalSection(&g_csHooked);
	}

	// ---- the win32u hooks ----
	typedef UINT_PTR U;
	typedef U(NTAPI* PFN_CWE)(U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U);
	typedef BOOL(NTAPI* PFN_SHOW)(HWND, int);
	typedef BOOL(NTAPI* PFN_SETFG)(HWND);
	typedef HWND(NTAPI* PFN_SETACT)(HWND);
	PFN_CWE o_CreateWindowEx = NULL;
	PFN_SHOW o_ShowWindow = NULL, o_ShowWindowAsync = NULL;
	PFN_SETFG o_SetForegroundWindow = NULL;
	PFN_SETACT o_SetActiveWindow = NULL;

	int NonActivating(int cmd)
	{
		switch(cmd)
		{
		case SW_SHOWNORMAL: case SW_RESTORE: case SW_SHOWDEFAULT: case SW_SHOWMAXIMIZED: return SW_SHOWNOACTIVATE;
		case SW_SHOW: return SW_SHOWNA;
		case SW_SHOWMINIMIZED: case SW_MINIMIZE: return SW_SHOWMINNOACTIVE;
		default: return cmd;
		}
	}

	U NTAPI d_CreateWindowEx(U a0, U a1, U a2, U a3, U a4, U a5, U a6, U a7, U a8, U a9, U a10, U a11, U a12, U a13, U a14,
	                         U a15, U a16)
	{
		// a4 = style, a9 = parent/owner (the same argument order as the 32-bit stub, 17 arguments: the WOW64 stub of
		// the same build ends "ret 0x44", focus_guard.cpp).
		const DWORD style = (DWORD)a4;
		const bool top = On() && !(style & WS_CHILD) && (HWND)a9 != HWND_MESSAGE;
		bool wasVisible = false;
		if(top)
		{
			EnsureThreadHooked(GetCurrentThreadId());	// the veto exists before the window does
			if(style & WS_VISIBLE)
			{
				a4 &= ~(U)WS_VISIBLE;
				wasVisible = true;
			}
		}
		const U r = o_CreateWindowEx(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);
		HWND h = (HWND)r;
		if(top && h)
		{
			const DWORD err = GetLastError();
			InterlockedIncrement(&g_shm->win_created);
			Cloak(h, &g_shm->win_cloaked_create, wasVisible ? "at creation, WS_VISIBLE deferred" : "at creation");
			if(wasVisible)
				o_ShowWindow(h, SW_SHOWNA);
			SetLastError(err);
		}
		return r;
	}

	BOOL ShowCommon(PFN_SHOW orig, HWND h, int cmd)
	{
		if(On() && cmd != SW_HIDE && OwnTopLevel(h))
		{
			if(!IsCloaked(h))
				Cloak(h, &g_shm->win_cloaked_show, "at show");
			const int nc = NonActivating(cmd);
			if(nc != cmd)
			{
				InterlockedIncrement(&g_shm->win_show_rewritten);
				cmd = nc;
			}
		}
		return orig(h, cmd);
	}
	BOOL NTAPI d_ShowWindow(HWND h, int cmd) { return ShowCommon(o_ShowWindow, h, cmd); }
	BOOL NTAPI d_ShowWindowAsync(HWND h, int cmd) { return ShowCommon(o_ShowWindowAsync, h, cmd); }

	BOOL NTAPI d_SetForegroundWindow(HWND h)
	{
		if(On())
		{
			InterlockedIncrement(&g_shm->win_fg_blocked);
			glog("refused NtUserSetForegroundWindow(%p)", (void*)h);
			return TRUE;
		}
		return o_SetForegroundWindow(h);
	}

	// Issue 9026: Windows 10 (19045) has no win32u!NtUserSetForegroundWindow. There user32!SetForegroundWindow is
	// "mov edx, imm32; jmp qword ptr [rip+rel32]" into win32u!NtUserCallHwndLock(hwnd, index) [C, the testbed's x64
	// user32.dll and win32u.dll, 2026-11-11: index 0x70]. The guard then hooks NtUserCallHwndLock and refuses exactly
	// that index; every other index passes. The index is read from user32's own code, never assumed.
	typedef BOOL(NTAPI* PFN_CALLHWNDLOCK)(HWND, U);
	PFN_CALLHWNDLOCK o_CallHwndLock = NULL;
	LONG g_sfwIndex = -1;

	// Returns the NtUserCallHwndLock index that x64 user32!SetForegroundWindow at p passes, or -1 when p is not exactly
	// "BA imm32 / 48 FF 25 rel32" with a slot that holds target (win32u!NtUserCallHwndLock).
	LONG SfwIndexX64(const BYTE* p, const void* target)
	{
		if(!p || !target || p[0] != 0xBA || p[5] != 0x48 || p[6] != 0xFF || p[7] != 0x25)
			return -1;
		DWORD idx = 0;
		INT32 rel = 0;
		memcpy(&idx, p + 1, 4);
		memcpy(&rel, p + 8, 4);
		if(idx > 0xFFFF)
			return -1;
		void* const* slot = (void* const*)(p + 12 + rel);
		if(*slot != target)
			return -1;
		return (LONG)idx;
	}

	BOOL NTAPI d_CallHwndLock(HWND h, U idx)
	{
		if(On() && g_sfwIndex >= 0 && (DWORD)idx == (DWORD)g_sfwIndex)
		{
			InterlockedIncrement(&g_shm->win_fg_blocked);
			glog("refused NtUserCallHwndLock(%p, 0x%lx) = SetForegroundWindow", (void*)h, (unsigned long)(DWORD)idx);
			return TRUE;
		}
		return o_CallHwndLock(h, idx);
	}
	HWND NTAPI d_SetActiveWindow(HWND h)
	{
		if(On())
		{
			InterlockedIncrement(&g_shm->win_fg_blocked);
			glog("refused NtUserSetActiveWindow(%p)", (void*)h);
			return GetActiveWindow();
		}
		return o_SetActiveWindow(h);
	}

	const BYTE kStubHead[4] = {0x4C, 0x8B, 0xD1, 0xB8};
	const BYTE kStubTail[16] = {0xF6, 0x04, 0x25, 0x08, 0x03, 0xFE, 0x7F, 0x01, 0x75, 0x03, 0x0F, 0x05, 0xC3, 0xCD, 0x2E, 0xC3};
	const int kStubLen = 24;

	// Hooks one x64 syscall stub of win32u.dll; returns false (and says why in st) if its shape is not the known one.
	bool HookStubAt(BYTE* p, const char* szName, void* pDetour, void** ppOrig, char* st, size_t cap);
	bool HookStub(HMODULE hWin32u, const char* szName, void* pDetour, void** ppOrig, char* st, size_t cap)
	{
		return HookStubAt((BYTE*)GetProcAddress(hWin32u, szName), szName, pDetour, ppOrig, st, cap);
	}
	// The same at an address (issue 9026: tested on synthetic stubs, tools/harness/native/tests/test_guard_shapes.py).
	bool HookStubAt(BYTE* p, const char* szName, void* pDetour, void** ppOrig, char* st, size_t cap)
	{
		size_t len = strlen(st);
		if(!p)
		{
			_snprintf_s(st + len, cap - len, _TRUNCATE, "%s:missing ", szName);
			return false;
		}
		if(memcmp(p, kStubHead, 4) != 0 || memcmp(p + 8, kStubTail, 16) != 0)
		{
			_snprintf_s(st + len, cap - len, _TRUNCATE, "%s:shape(%02x %02x %02x %02x) ", szName, p[0], p[1], p[2], p[3]);
			return false;
		}
		if(g_trampUsed + 32 > 4096)
		{
			_snprintf_s(st + len, cap - len, _TRUNCATE, "%s:no-trampoline-space ", szName);
			return false;
		}
		BYTE* t = g_tramp + g_trampUsed;
		g_trampUsed += 32;
		memcpy(t, p, kStubLen);
		*ppOrig = t;	// published before the jump is written
		MemoryBarrier();
		BYTE jmp[12] = {0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};	// mov rax, imm64; jmp rax
		const UINT64 dest = (UINT64)(UINT_PTR)pDetour;
		memcpy(jmp + 2, &dest, 8);
		DWORD old = 0;
		if(!VirtualProtect(p, sizeof(jmp), PAGE_EXECUTE_READWRITE, &old))
		{
			_snprintf_s(st + len, cap - len, _TRUNCATE, "%s:protect-failed(%lu) ", szName, GetLastError());
			return false;
		}
		memcpy(p, jmp, sizeof(jmp));
		VirtualProtect(p, sizeof(jmp), old, &old);
		FlushInstructionCache(GetCurrentProcess(), p, sizeof(jmp));
		_snprintf_s(st + len, cap - len, _TRUNCATE, "%s:ok ", szName);
		return true;
	}

	// ---- the watchdog ----
	BOOL CALLBACK SweepOne(HWND h, LPARAM)
	{
		DWORD pid = 0;
		const DWORD tid = GetWindowThreadProcessId(h, &pid);
		if(pid != GetCurrentProcessId())
			return TRUE;
		if(On())
		{
			EnsureThreadHooked(tid);
			if(IsWindowVisible(h) && !IsCloaked(h))
				Cloak(h, &g_shm->win_cloaked_watchdog, "by the watchdog (LATE)");
		}
		return TRUE;
	}

	DWORD WINAPI Watchdog(void*)
	{
		LONG wasOn = 1;
		for(;;)
		{
			const LONG on = On() ? 1 : 0;
			if(on)
				EnumWindows(SweepOne, 0);
			else if(wasOn)
				ReleaseSecondaries();
			wasOn = on;
			Sleep(100);
		}
	}

	// =================================================================================================================
	// Issue 0475: the guard follows the server's 32-bit children (vp64helper.exe, the patch's D118 popup)
	// =================================================================================================================
	// The server starts vp64helper.exe itself for the EXE-crash and DLL-fatal reports (remote/srv_exe_crash.cpp
	// StartHelper: CreateProcessW, DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB). Its popup
	// (class VP64HelperPopup, WS_EX_TOPMOST, ShowWindow(SW_SHOWNORMAL) + SetForegroundWindow) belongs to that process, so
	// this guard never saw it. Issue 0473 gave the EXE's 32-bit guard a hook on kernelbase!CreateProcessInternalW; this
	// is the server's copy of it. With C5_CHILD_GUARD=<absolute path of the 32-bit focus_guard.dll> in the environment
	// (winapi.helper_launch sets it for the EXE; the proxy's CreateProcessA hands the EXE's environment to the server)
	// and the guard on, every process the server creates goes through d_CreateProcessInternalW:
	//   - it is created suspended;
	//   - not 32-bit: counted in child_other and resumed unchanged (no 64-bit child is known; logged);
	//   - 32-bit: the server creates the child's section Local\CivBgGuard_<child pid> with the guard's defaults, cloak on
	//     and child_parent = the server's pid, and has bg_helper32.exe (the harness' 32-bit helper, beside the guard DLL)
	//     load the guard into it: `bg_helper32.exe inject <pid> <dll>`, a remote LoadLibraryW from a 32-bit process (the
	//     child's process initialisation runs on that thread; its main thread has not run). A 64-bit process cannot start
	//     that thread itself: the 32-bit LoadLibraryW is not mapped in the server. Only with bg_helper32's exit 0 and
	//     the child's guard_state 1 read back is the child resumed (child_guarded); otherwise it is terminated before its
	//     first instruction (0xC5F00051) and CreateProcess fails with ERROR_DLL_INIT_FAILED (child_failed; fail closed).
	// In the child the guard runs in child mode (focus_guard.cpp, issue 0473): every top-level window cloaked at creation,
	// foreground and activation refused, the window still enumerable so that dismiss can press its own buttons (0472).
	// The x64 kernelbase!CreateProcessInternalW starts with twelve bytes of pushes, the same on Windows 11 26100 and
	// Windows 10 19045 [C, 2026-11-17]; any other shape is not patched and the part counts as missing (bit 0x40: the
	// self-test fails and the server ends before any DLL). C5_CHILD_TEST_FAIL=inject (tests only) fails every injection.
	typedef BOOL(WINAPI* PFN_CPIW)(HANDLE, LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD,
	                               LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION, PHANDLE);
	PFN_CPIW o_CreateProcessInternalW = NULL;
	wchar_t g_childGuard[MAX_PATH];			// C5_CHILD_GUARD; "" = children are not followed
	wchar_t g_childInjector[MAX_PATH];		// bg_helper32.exe beside it
	thread_local bool t_startingInjector = false;	// this thread is starting bg_helper32 itself: not followed
	const DWORD kChildTerminateCode = 0xC5F00051;	// the code of focus_guard.cpp (0473) for a child not guarded
	const DWORD kInjectorWaitMs = 30000;			// bg_helper32 waits up to 15 s for its remote thread
	// push rbx (REX); push rsi; push rdi; push r12; push r13; push r14; push r15: position independent, 12 bytes.
	const BYTE kCpiwHead[12] = {0x40, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};

	// Hooks a function whose first len (>= 12) bytes equal head and hold whole position-independent instructions:
	// trampoline = those bytes + "jmp qword ptr [rip]" back to p + len; the function gets "mov rax, imm64; jmp rax"
	// (rax is not an argument register). Returns false (and says why in st) on any other shape.
	bool HookPrologueAt(BYTE* p, const BYTE* head, int len, const char* szName, void* pDetour, void** ppOrig, char* st,
	                    size_t cap)
	{
		size_t used = strlen(st);
		if(!p)
		{
			_snprintf_s(st + used, cap - used, _TRUNCATE, "%s:missing ", szName);
			return false;
		}
		if(len < 12 || len + 14 > 32 || memcmp(p, head, len) != 0)
		{
			_snprintf_s(st + used, cap - used, _TRUNCATE, "%s:shape(%02x %02x %02x %02x) ", szName, p[0], p[1], p[2], p[3]);
			return false;
		}
		if(g_trampUsed + 32 > 4096)
		{
			_snprintf_s(st + used, cap - used, _TRUNCATE, "%s:no-trampoline-space ", szName);
			return false;
		}
		BYTE* t = g_tramp + g_trampUsed;
		g_trampUsed += 32;
		memcpy(t, p, len);
		const BYTE jmpBack[6] = {0xFF, 0x25, 0, 0, 0, 0};	// jmp qword ptr [rip+0]; the address follows
		memcpy(t + len, jmpBack, sizeof(jmpBack));
		const UINT64 back = (UINT64)(UINT_PTR)(p + len);
		memcpy(t + len + 6, &back, 8);
		*ppOrig = t;	// published before the jump is written
		MemoryBarrier();
		BYTE jmp[12] = {0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};	// mov rax, imm64; jmp rax
		const UINT64 dest = (UINT64)(UINT_PTR)pDetour;
		memcpy(jmp + 2, &dest, 8);
		DWORD old = 0;
		if(!VirtualProtect(p, sizeof(jmp), PAGE_EXECUTE_READWRITE, &old))
		{
			_snprintf_s(st + used, cap - used, _TRUNCATE, "%s:protect-failed(%lu) ", szName, GetLastError());
			return false;
		}
		memcpy(p, jmp, sizeof(jmp));
		VirtualProtect(p, sizeof(jmp), old, &old);
		FlushInstructionCache(GetCurrentProcess(), p, sizeof(jmp));
		_snprintf_s(st + used, cap - used, _TRUNCATE, "%s:ok ", szName);
		return true;
	}

	bool ChildTestFail()
	{
		char v[16] = "";
		GetEnvironmentVariableA("C5_CHILD_TEST_FAIL", v, sizeof(v));
		return !strcmp(v, "inject");
	}

	void Utf8(const wchar_t* w, char* out, int cap)
	{
		if(!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap, NULL, NULL))
			out[0] = 0;
		out[cap - 1] = 0;
	}

	// The 32-bit guard into a suspended 32-bit child, through bg_helper32. True only when the child's guard reported
	// guard_state 1 for that pid.
	bool GuardChild(HANDLE hChild, DWORD pid, char* why, size_t cap)
	{
		char name[64];
		_snprintf_s(name, sizeof(name), _TRUNCATE, "Local\\CivBgGuard_%lu", pid);
		HANDLE map = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, name);
		GuardShm* s = map ? (GuardShm*)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 4096) : NULL;
		if(!s)
		{
			_snprintf_s(why, cap, _TRUNCATE, "section %s: error %lu", name, GetLastError());
			if(map)
				CloseHandle(map);
			return false;
		}
		// A section of that name can outlive an older process with the same pid (a reader still holds it): start clean.
		memset((void*)s, 0, 4096);
		s->version = 1;
		s->enabled = 1;
		s->cloak = 1;
		s->cloak_applied = -1;
		s->isolate = 1;
		s->child_parent = GetCurrentProcessId();
		MemoryBarrier();
		s->magic = kMagic;
		bool ok = false;
		if(ChildTestFail())
			_snprintf_s(why, cap, _TRUNCATE, "C5_CHILD_TEST_FAIL=inject (test)");
		else
		{
			wchar_t cmd[3 * MAX_PATH];
			_snwprintf_s(cmd, _countof(cmd), _TRUNCATE, L"\"%s\" inject %lu \"%s\"", g_childInjector, pid, g_childGuard);
			STARTUPINFOW si;
			memset(&si, 0, sizeof(si));
			si.cb = sizeof(si);
			PROCESS_INFORMATION hp;
			memset(&hp, 0, sizeof(hp));
			t_startingInjector = true;
			const BOOL started = CreateProcessW(g_childInjector, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si,
			                                    &hp);
			const DWORD startErr = started ? 0 : GetLastError();
			t_startingInjector = false;
			if(!started)
				_snprintf_s(why, cap, _TRUNCATE, "bg_helper32.exe could not be started: error %lu", startErr);
			else
			{
				const DWORD w = WaitForSingleObject(hp.hProcess, kInjectorWaitMs);
				DWORD code = 0, childCode = 0;
				if(w != WAIT_OBJECT_0)
				{
					TerminateProcess(hp.hProcess, kChildTerminateCode);
					WaitForSingleObject(hp.hProcess, 5000);
				}
				GetExitCodeProcess(hp.hProcess, &code);
				GetExitCodeProcess(hChild, &childCode);
				if(w != WAIT_OBJECT_0)
					_snprintf_s(why, cap, _TRUNCATE, "bg_helper32 inject: no end within %lu s", kInjectorWaitMs / 1000);
				else if(code != 0)
					_snprintf_s(why, cap, _TRUNCATE, "bg_helper32 inject: exit %lu (2 OpenProcess, 3 injection, 5 refused "
					            "path)", code);
				else if(childCode != STILL_ACTIVE)
					_snprintf_s(why, cap, _TRUNCATE, "the child ended during the injection (exit 0x%08lx)", childCode);
				else if(s->pid != pid || s->guard_state != 1)
					_snprintf_s(why, cap, _TRUNCATE, "the child's guard: pid %lu, guard_state %ld, missing 0x%lx", s->pid,
					            (long)s->guard_state, (unsigned long)s->guard_missing);
				else
				{
					ok = true;
					char log[200];
					strncpy_s(log, sizeof(log), s->guard_log, _TRUNCATE);
					_snprintf_s(why, cap, _TRUNCATE, "guard_state 1 (bg_helper32 pid %lu), log %s", hp.dwProcessId, log);
				}
				CloseHandle(hp.hThread);
				CloseHandle(hp.hProcess);
			}
		}
		UnmapViewOfFile((void*)s);
		CloseHandle(map);	// the child's guard holds its own handle
		return ok;
	}

	BOOL WINAPI d_CreateProcessInternalW(HANDLE token, LPCWSTR app, LPWSTR cmd, LPSECURITY_ATTRIBUTES pa,
	                                     LPSECURITY_ATTRIBUTES ta, BOOL inherit, DWORD flags, LPVOID env, LPCWSTR cwd,
	                                     LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi, PHANDLE newToken)
	{
		if(!g_childGuard[0] || !On() || !pi || t_startingInjector)
			return o_CreateProcessInternalW(token, app, cmd, pa, ta, inherit, flags, env, cwd, si, pi, newToken);
		if(!o_CreateProcessInternalW(token, app, cmd, pa, ta, inherit, flags | CREATE_SUSPENDED, env, cwd, si, pi, newToken))
			return FALSE;	// the caller sees the original error
		InterlockedIncrement(&g_shm->child_seen);
		wchar_t imageW[MAX_PATH] = L"?";
		DWORD n = MAX_PATH;
		QueryFullProcessImageNameW(pi->hProcess, 0, imageW, &n);
		char image[3 * MAX_PATH];
		Utf8(imageW, image, sizeof(image));
		BOOL wow = FALSE;
		const BOOL known = IsWow64Process(pi->hProcess, &wow);
		char why[600] = "";
		bool guarded = false;
		if(known && !wow)
		{
			InterlockedIncrement(&g_shm->child_other);
			glog("child pid %lu (%s): not 32-bit: not guarded", pi->dwProcessId, image);
		}
		else if(!known)
			_snprintf_s(why, sizeof(why), _TRUNCATE, "IsWow64Process: error %lu", GetLastError());
		else
			guarded = GuardChild(pi->hProcess, pi->dwProcessId, why, sizeof(why));
		if(known && wow && guarded)
		{
			InterlockedIncrement(&g_shm->child_guarded);
			g_shm->child_last_pid = pi->dwProcessId;
			glog("child pid %lu (%s): guarded (%s)", pi->dwProcessId, image, why);
		}
		else if(!(known && !wow))
		{
			InterlockedIncrement(&g_shm->child_failed);
			glog("child pid %lu (%s): NOT guarded (%s): terminated before its first instruction; CreateProcess fails",
			     pi->dwProcessId, image, why);
			TerminateProcess(pi->hProcess, kChildTerminateCode);
			WaitForSingleObject(pi->hProcess, 5000);
			CloseHandle(pi->hThread);
			CloseHandle(pi->hProcess);
			memset(pi, 0, sizeof(*pi));
			SetLastError(ERROR_DLL_INIT_FAILED);
			return FALSE;
		}
		if(!(flags & CREATE_SUSPENDED))
			ResumeThread(pi->hThread);
		return TRUE;
	}

	// C5_CHILD_GUARD: an absolute path of an existing file with bg_helper32.exe beside it, else the part counts as
	// missing (fail closed). Called while the trampoline page is still writable.
	void InstallChildHook(char* st, size_t cap)
	{
		wchar_t v[MAX_PATH];
		const DWORD n = GetEnvironmentVariableW(L"C5_CHILD_GUARD", v, MAX_PATH);
		if(!n)
		{
			g_shm->child_guard = 0;
			return;
		}
		char v8[3 * MAX_PATH];
		Utf8(n < MAX_PATH ? v : L"(too long)", v8, sizeof(v8));
		const DWORD attr = n < MAX_PATH ? GetFileAttributesW(v) : INVALID_FILE_ATTRIBUTES;
		const size_t used = strlen(st);
		if(attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) || !(v[0] && v[1] == L':' && v[2] == L'\\'))
		{
			g_shm->child_guard = -1;
			_snprintf_s(st + used, cap - used, _TRUNCATE, "children:C5_CHILD_GUARD-not-a-file ");
			glog("children: C5_CHILD_GUARD=%s is not an absolute path of a file: NOT covered", v8);
			return;
		}
		wchar_t inj[MAX_PATH];
		wcscpy_s(inj, v);
		wchar_t* slash = wcsrchr(inj, L'\\');
		const size_t room = slash ? _countof(inj) - (size_t)(slash + 1 - inj) : 0;
		const DWORD ia = slash && wcscpy_s(slash + 1, room, L"bg_helper32.exe") == 0 ? GetFileAttributesW(inj)
		                                                                              : INVALID_FILE_ATTRIBUTES;
		if(ia == INVALID_FILE_ATTRIBUTES || (ia & FILE_ATTRIBUTE_DIRECTORY))
		{
			g_shm->child_guard = -1;
			_snprintf_s(st + used, cap - used, _TRUNCATE, "children:no-bg_helper32 ");
			glog("children: no bg_helper32.exe beside C5_CHILD_GUARD=%s: NOT covered", v8);
			return;
		}
		HMODULE kb = GetModuleHandleA("kernelbase.dll");
		BYTE* p = kb ? (BYTE*)GetProcAddress(kb, "CreateProcessInternalW") : NULL;
		if(!HookPrologueAt(p, kCpiwHead, (int)sizeof(kCpiwHead), "CreateProcessInternalW", (void*)d_CreateProcessInternalW,
		                   (void**)&o_CreateProcessInternalW, st, cap))
		{
			g_shm->child_guard = -1;
			glog("children: CreateProcessInternalW not hooked: NOT covered (%s)", st + used);
			return;
		}
		wcscpy_s(g_childInjector, inj);
		wcscpy_s(g_childGuard, v);	// last: the detour follows children only from here on
		g_shm->child_guard = 1;
		glog("children: CreateProcessInternalW hooked; 32-bit children get %s through bg_helper32.exe (issue 0475)", v8);
	}

	bool GuardRequested()
	{
		char v[8] = "";
		const DWORD n = GetEnvironmentVariableA("CIVBG_GUARD", v, sizeof(v));
		return n == 1 && v[0] == '1';
	}
}

unsigned long SrvParentPid()
{
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if(snap == INVALID_HANDLE_VALUE)
		return 0;
	PROCESSENTRY32W pe;
	pe.dwSize = sizeof(pe);
	unsigned long ppid = 0;
	const DWORD self = GetCurrentProcessId();
	for(BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
		if(pe.th32ProcessID == self)
		{
			ppid = pe.th32ParentProcessID;
			break;
		}
	CloseHandle(snap);
	return ppid;
}

int SrvGuardInstall(char* szWhy, size_t cchWhy)
{
	szWhy[0] = 0;
	if(!GuardRequested())
	{
		_snprintf_s(szWhy, cchWhy, _TRUNCATE, "not requested (CIVBG_GUARD is not 1)");
		return SRV_GUARD_NOT_REQUESTED;
	}
	InitializeCriticalSection(&g_csLog);
	InitializeCriticalSection(&g_csHooked);
	InitializeCriticalSection(&g_csSecondary);
	char path[MAX_PATH], tmp[MAX_PATH];
	GetTempPathA(MAX_PATH, tmp);
	// Issue 9010's naming (focus_guard.cpp, issue 2703 for the server): the pid AND the process' creation time (UTC, from
	// GetProcessTimes), so a reused pid never appends to another process' log; the path is published at shm 1284.
	FILETIME ctFt, exFt, kFt, uFt;
	SYSTEMTIME ctSt;
	if(GetProcessTimes(GetCurrentProcess(), &ctFt, &exFt, &kFt, &uFt) && FileTimeToSystemTime(&ctFt, &ctSt))
		_snprintf_s(path, sizeof(path), _TRUNCATE, "%scivbg_guard_%lu_%04u%02u%02u-%02u%02u%02u-%03u.log", tmp,
		            GetCurrentProcessId(), ctSt.wYear, ctSt.wMonth, ctSt.wDay, ctSt.wHour, ctSt.wMinute, ctSt.wSecond,
		            ctSt.wMilliseconds);
	else
		_snprintf_s(path, sizeof(path), _TRUNCATE, "%scivbg_guard_%lu_unknown-%lu.log", tmp, GetCurrentProcessId(),
		            GetTickCount());
	g_log = _fsopen(path, "a", _SH_DENYNO);	// readable by the harness while the server runs

	char name[64];
	_snprintf_s(name, sizeof(name), _TRUNCATE, "Local\\CivBgGuard_%lu", GetCurrentProcessId());
	HANDLE map = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, name);
	GuardShm* shm = map ? (GuardShm*)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 4096) : NULL;
	if(!shm)
	{
		_snprintf_s(szWhy, cchWhy, _TRUNCATE, "cannot create the shared memory %s (error %lu)", name, GetLastError());
		glog("FAILED: %s", szWhy);
		return SRV_GUARD_FAILED;
	}
	// The handle stays open for the life of the process (the harness opens the section by name).
	if(shm->magic != kMagic)
	{
		memset(shm, 0, sizeof(*shm));
		shm->version = 1;
		shm->enabled = 1;
		shm->cloak_applied = -1;
	}
	shm->pid = GetCurrentProcessId();
	if(g_log)
		strncpy_s(shm->guard_log, sizeof(shm->guard_log), path, _TRUNCATE);	// issue 9010 field (2703)
	else
		shm->guard_log[0] = 0;
	strcpy_s(shm->selftest, "server.exe x64 guard (issue 2501)");
	g_shm = shm;

	HMODULE hDwm = LoadLibraryA("dwmapi.dll");
	s_dwmSet = hDwm ? (PFN_DWMSET)GetProcAddress(hDwm, "DwmSetWindowAttribute") : NULL;
	s_dwmGet = hDwm ? (PFN_DWMGET)GetProcAddress(hDwm, "DwmGetWindowAttribute") : NULL;
	HMODULE hUser = LoadLibraryA("user32.dll");	// static import of server.exe anyway; also loads win32u.dll
	HMODULE hWin32u = GetModuleHandleA("win32u.dll");
	g_tramp = (BYTE*)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	char st[512] = "server x64: ";
	bool ok = s_dwmSet && s_dwmGet && hUser && hWin32u && g_tramp;
#ifdef VP_SERVER_TEST_DIALOG
	// TEST BUILD ONLY: VP_SERVER_TEST_GUARD_FAIL=1 makes the install fail before any hook (the fail-closed test).
	char szForce[8] = "";
	if(GetEnvironmentVariableA("VP_SERVER_TEST_GUARD_FAIL", szForce, sizeof(szForce)) == 1 && szForce[0] == '1')
	{
		ok = false;
		strcat_s(st, "TEST forced failure (VP_SERVER_TEST_GUARD_FAIL=1) ");
	}
	else
#endif
	if(!ok)
		_snprintf_s(st + strlen(st), sizeof(st) - strlen(st), _TRUNCATE, "setup-failed(dwm %d user32 %d win32u %d tramp %d) ",
		            s_dwmSet && s_dwmGet, hUser != NULL, hWin32u != NULL, g_tramp != NULL);
	else
	{
		struct
		{
			const char* n;
			void* d;
			void** o;
			LONG bit;
		} hs[] = {
			{"NtUserCreateWindowEx", (void*)d_CreateWindowEx, (void**)&o_CreateWindowEx, 1},
			{"NtUserShowWindow", (void*)d_ShowWindow, (void**)&o_ShowWindow, 2},
			{"NtUserSetForegroundWindow", (void*)d_SetForegroundWindow, (void**)&o_SetForegroundWindow, 4},
			{"NtUserSetActiveWindow", (void*)d_SetActiveWindow, (void**)&o_SetActiveWindow, 8},
			{"NtUserShowWindowAsync", (void*)d_ShowWindowAsync, (void**)&o_ShowWindowAsync, 16},
		};
		for(size_t i = 0; i < sizeof(hs) / sizeof(hs[0]); ++i)
		{
			if(hs[i].bit == 4 && !GetProcAddress(hWin32u, hs[i].n))
			{
				// Issue 9026 (Windows 10): the foreground request goes through NtUserCallHwndLock, see SfwIndexX64.
				g_sfwIndex = SfwIndexX64((const BYTE*)GetProcAddress(hUser, "SetForegroundWindow"),
				                         (const void*)GetProcAddress(hWin32u, "NtUserCallHwndLock"));
				size_t len = strlen(st);
				if(g_sfwIndex < 0)
				{
					_snprintf_s(st + len, sizeof(st) - len, _TRUNCATE,
					            "NtUserSetForegroundWindow:missing(user32 SetForegroundWindow shape unknown) ");
					ok = false;
				}
				else
				{
					_snprintf_s(st + len, sizeof(st) - len, _TRUNCATE, "NtUserSetForegroundWindow:missing->index 0x%lx ",
					            (unsigned long)g_sfwIndex);
					if(HookStub(hWin32u, "NtUserCallHwndLock", (void*)d_CallHwndLock, (void**)&o_CallHwndLock, st, sizeof(st)))
						g_shm->win_hooks |= hs[i].bit;
					else
						ok = false;
				}
				continue;
			}
			if(HookStub(hWin32u, hs[i].n, hs[i].d, hs[i].o, st, sizeof(st)))
				g_shm->win_hooks |= hs[i].bit;
			else
				ok = false;
		}
		InstallChildHook(st, sizeof(st));	// issue 0475: only with C5_CHILD_GUARD; a failure is a self-test failure
		DWORD old = 0;
		VirtualProtect(g_tramp, 4096, PAGE_EXECUTE_READ, &old);
	}
	if(ok)
	{
		DisableProcessWindowsGhosting();
		g_shm->win_hooks |= 0x100;
		HANDLE th = CreateThread(NULL, 0, Watchdog, NULL, 0, NULL);
		if(th)
		{
			CloseHandle(th);
			g_shm->win_hooks |= 0x200;
		}
		else
		{
			ok = false;
			_snprintf_s(st + strlen(st), sizeof(st) - strlen(st), _TRUNCATE, "watchdog:CreateThread-failed(%lu) ", GetLastError());
		}
	}
	// Issue 2703: the self-test of every part, published as guard_state / guard_missing (focus_guard.cpp meanings).
	LONG missing = 0;
	if((g_shm->win_hooks & 0x1F) != 0x1F)
		missing |= 0x02;
	DWORD vetoErr = 0;
	HHOOK hkTest = InstallVeto(GetCurrentThreadId(), true, &vetoErr);
	if(hkTest)
		UnhookWindowsHookEx(hkTest);
	else
		missing |= 0x04;
	if(!s_dwmSet || !s_dwmGet)
		missing |= 0x08;
	if(!(g_shm->win_hooks & 0x200))
		missing |= 0x20;
	if(g_shm->child_guard < 0)
		missing |= 0x40;	// issue 0475: C5_CHILD_GUARD set, children not covered
	g_shm->guard_missing = missing;
	g_shm->guard_state = missing ? -1 : 1;
	glog("guard self-test (issue 2703): %s missing=0x%lx (win32u 0x%lx, veto test error %lu%s; children %ld)",
	     missing ? "FAILED" : "ok", (unsigned long)missing, (unsigned long)g_shm->win_hooks, vetoErr, TestModeName(),
	     (long)g_shm->child_guard);
	_snprintf_s(st + strlen(st), sizeof(st) - strlen(st), _TRUNCATE, "self-test %s missing=0x%lx%s ",
	            missing ? "FAILED" : "ok", (unsigned long)missing, TestModeName());
	strcpy_s(g_shm->status, st);
	if(!ok)
	{
		// Leave nothing half-on: the caller exits before any DLL loads.
		g_shm->enabled = 0;
		g_exitCode = SRV_GUARD_EXIT_CODE;
		_snprintf_s(szWhy, cchWhy, _TRUNCATE, "guard not installed: %s", st);
		glog("FAILED: %s", szWhy);
		return SRV_GUARD_FAILED;
	}
	if(missing && TestMode() != kTestVetoReport)
	{
		// Every hook is in, but a part does not work (today: the WH_CBT veto). Same as above: nothing half-on.
		g_shm->enabled = 0;
		g_exitCode = SRV_GUARD_SELFTEST_EXIT_CODE;
		_snprintf_s(szWhy, cchWhy, _TRUNCATE, "guard self-test failed (missing 0x%lx): %s", (unsigned long)missing, st);
		glog("FAILED: %s", szWhy);
		return SRV_GUARD_FAILED;
	}
	g_shm->magic = kMagic;	// last: the harness reads a present guard only once it is complete
	_snprintf_s(szWhy, cchWhy, _TRUNCATE, "installed%s: hooks 0x%lx, guard_state %ld, %s",
	            missing ? " (TEST: guard_state -1 reported, the server runs on)" : "", (unsigned long)g_shm->win_hooks,
	            (long)g_shm->guard_state, st);
	glog("%s", szWhy);
	return SRV_GUARD_INSTALLED;
}

unsigned long SrvGuardExitCode() { return g_exitCode; }

void SrvGuardSetLogSink(void (*pfn)(const char*)) { g_sink = pfn; }

void SrvGuardDescribe(int iState, char* szOut, size_t cchOut)
{
	char cwd[MAX_PATH] = "", env[16] = "(unset)";
	GetCurrentDirectoryA(MAX_PATH, cwd);
	GetEnvironmentVariableA("CIVBG_GUARD", env, sizeof(env));
	char desk[64] = "?";
	DWORD need = 0;
	GetUserObjectInformationA(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, desk, sizeof(desk), &need);
	const char* szState = iState == SRV_GUARD_INSTALLED ? "installed" : iState == SRV_GUARD_FAILED ? "FAILED" : "not requested";
	_snprintf_s(szOut, cchOut, _TRUNCATE,
	            "guard: %s (CIVBG_GUARD=%s, hooks 0x%lx, windows created %ld, cloaked at creation %ld, at show %ld, by the "
	            "watchdog %ld, foreground/activation refused %ld, guard_state %ld, missing 0x%lx, veto ok %ld failed %ld; "
	            "children: guard %ld, seen %ld, guarded %ld, other %ld, failed %ld, last pid %lu); "
	            "console window %p; parent pid %lu; cwd %s; desktop %s",
	            szState, env, g_shm ? (unsigned long)g_shm->win_hooks : 0ul, g_shm ? (long)g_shm->win_created : 0l,
	            g_shm ? (long)g_shm->win_cloaked_create : 0l, g_shm ? (long)g_shm->win_cloaked_show : 0l,
	            g_shm ? (long)g_shm->win_cloaked_watchdog : 0l, g_shm ? (long)g_shm->win_fg_blocked : 0l,
	            g_shm ? (long)g_shm->guard_state : 0l, g_shm ? (unsigned long)g_shm->guard_missing : 0ul,
	            g_shm ? (long)g_shm->win_veto_ok : 0l, g_shm ? (long)g_shm->win_veto_failed : 0l,
	            g_shm ? (long)g_shm->child_guard : 0l, g_shm ? (long)g_shm->child_seen : 0l,
	            g_shm ? (long)g_shm->child_guarded : 0l, g_shm ? (long)g_shm->child_other : 0l,
	            g_shm ? (long)g_shm->child_failed : 0l, g_shm ? (unsigned long)g_shm->child_last_pid : 0ul,
	            (void*)GetConsoleWindow(), SrvParentPid(), cwd, desk);
}

#ifdef VP_SERVER_TEST_DIALOG
// =====================================================================================================================
// TEST BUILD ONLY (server/build_server.py --test-dialog, define VP_SERVER_TEST_DIALOG; output in a separate folder).
//
//   server.exe --test-dialog plain,sysmodal,setfg [--test-log FILE] [--test-linger SEC] [--test-no-guard]
//
// After main() installed the guard: one new thread per listed kind (none of them ever owned a window), each showing
// MessageBoxA(NULL, ...) with a known title and text, as the DLL's CvPreconditionDlg does on the game-core thread:
//   plain     MB_OK | MB_ICONWARNING
//   sysmodal  MB_OK | MB_SYSTEMMODAL
//   setfg     MB_OK | MB_SETFOREGROUND
// Then the main thread waits for its parent process to end and exits by itself (exit 0), as the server does when its
// peer dies; --test-linger SEC first stays SEC seconds longer (the harness's orphan test).
// --test-no-guard (read by main(): the guard is not installed even with CIVBG_GUARD=1) exists for the negative tests.
// Interlock: a box is shown only when the guard is installed, or when this process runs on a desktop that is not the
// input desktop (the hidden desktop of the negative tests). Otherwise nothing is shown and the process exits 7.
namespace
{
	FILE* s_testLog = NULL;
	void tlog(const char* szFormat, ...)
	{
		char sz[1024];
		va_list v;
		va_start(v, szFormat);
		vsnprintf(sz, sizeof(sz), szFormat, v);
		va_end(v);
		SYSTEMTIME st;
		GetLocalTime(&st);
		FILE* f = s_testLog ? s_testLog : stderr;
		fprintf(f, "%02u:%02u:%02u.%03u pid=%lu tid=%lu %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
		        GetCurrentProcessId(), GetCurrentThreadId(), sz);
		fflush(f);
	}

	struct TestBox
	{
		const char* kind;
		UINT flags;
		const char* title;
		const char* text;
	};
	const TestBox s_boxes[] = {
		{"plain", MB_OK | MB_ICONWARNING, "VP2501 server test (plain)",
		 "VP_SERVER_TEST_DIALOG plain: NULL-owner MessageBoxA on a new thread of server.exe"},
		{"sysmodal", MB_OK | MB_SYSTEMMODAL, "VP2501 server test (sysmodal)",
		 "VP_SERVER_TEST_DIALOG sysmodal: MB_SYSTEMMODAL MessageBoxA on a new thread of server.exe"},
		{"setfg", MB_OK | MB_SETFOREGROUND, "VP2501 server test (setfg)",
		 "VP_SERVER_TEST_DIALOG setfg: MB_SETFOREGROUND MessageBoxA on a new thread of server.exe"},
	};

	DWORD WINAPI BoxThread(void* pv)
	{
		const TestBox& b = *(const TestBox*)pv;
		tlog("box %s: MessageBoxA(NULL, \"%s\", \"%s\", 0x%x) on a new thread", b.kind, b.text, b.title, b.flags);
		const int r = MessageBoxA(NULL, b.text, b.title, b.flags);
		tlog("box %s: closed, MessageBoxA returned %d", b.kind, r);
		return 0;
	}

	void DesktopName(HDESK h, char* out, DWORD cap)
	{
		DWORD need = 0;
		out[0] = 0;
		if(!h || !GetUserObjectInformationA(h, UOI_NAME, out, cap, &need))
			out[0] = 0;
	}
}

bool SrvGuardTestArg(int argc, char** argv, const char* szName)
{
	for(int i = 1; i < argc; ++i)
		if(!strcmp(argv[i], szName))
			return true;
	return false;
}

static const char* TestArgValue(int argc, char** argv, const char* szName, const char* szDefault)
{
	for(int i = 1; i + 1 < argc; ++i)
		if(!strcmp(argv[i], szName))
			return argv[i + 1];
	return szDefault;
}

int SrvGuardTestDialogMain(int argc, char** argv, int iGuardState)
{
	const char* szLog = TestArgValue(argc, argv, "--test-log", NULL);
	if(szLog)
		s_testLog = _fsopen(szLog, "a", _SH_DENYNO);
	char line[1024];
	SrvGuardDescribe(iGuardState, line, sizeof(line));
	tlog("TEST START server.exe test-dialog build; %s", line);
	char thisDesk[64], inputDesk[64];
	DesktopName(GetThreadDesktop(GetCurrentThreadId()), thisDesk, sizeof(thisDesk));
	HDESK hIn = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
	DesktopName(hIn, inputDesk, sizeof(inputDesk));
	if(hIn)
		CloseDesktop(hIn);
	const bool hidden = thisDesk[0] && inputDesk[0] && _stricmp(thisDesk, inputDesk) != 0;
	tlog("desktop %s, input desktop %s, guard state %d", thisDesk[0] ? thisDesk : "?", inputDesk[0] ? inputDesk : "?",
	     iGuardState);
	if(iGuardState != SRV_GUARD_INSTALLED && !hidden)
	{
		tlog("REFUSED: no guard and this is the input desktop: no box is shown (exit 7)");
		if(s_testLog)
			fclose(s_testLog);
		return 7;
	}
	const char* szKinds = TestArgValue(argc, argv, "--test-dialog", "plain");
	for(size_t i = 0; i < sizeof(s_boxes) / sizeof(s_boxes[0]); ++i)
		if(strstr(szKinds, s_boxes[i].kind))
		{
			HANDLE th = CreateThread(NULL, 0, BoxThread, (void*)&s_boxes[i], 0, NULL);
			if(th)
				CloseHandle(th);
		}
	const DWORD ppid = SrvParentPid();
	HANDLE hParent = ppid ? OpenProcess(SYNCHRONIZE, FALSE, ppid) : NULL;
	if(!hParent)
	{
		tlog("cannot open the parent process %lu (error %lu): exit 9", ppid, GetLastError());
		return 9;
	}
	tlog("waiting for the parent pid %lu to end", ppid);
	WaitForSingleObject(hParent, INFINITE);
	const double linger = atof(TestArgValue(argc, argv, "--test-linger", "0"));
	SrvGuardDescribe(iGuardState, line, sizeof(line));
	tlog("parent ended; %s", line);
	if(linger > 0)
	{
		tlog("lingering %.1f s (orphan test)", linger);
		Sleep((DWORD)(linger * 1000));
	}
	tlog("TEST END: exiting by itself (exit 0)");
	if(s_testLog)
		fclose(s_testLog);
	TerminateProcess(GetCurrentProcess(), 0);	// the box threads, if any are still open, end with the process
	return 0;
}
#endif
