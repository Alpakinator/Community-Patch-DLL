// proxy_kbhook.cpp - the EXE's global keyboard hook moved off its main thread (ticket 0720). See proxy_kbhook.h.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Written 2026-11-23 for this repository. The EXE's behaviour it reproduces is the knowledge base's description
// (research/02-exe-internals/keyboard-hook-and-hangs.md section 3, D83): the hook swallows WM_SYSKEYDOWN with VK_RETURN
// when nCode >= 0 and passes everything else on. The import-slot walk follows proxy_queue_guard.cpp's (ticket 0492).
// Toolchain: clang-cl -m32, VC9 headers, C++03. x86 only (pointers are DWORDs).
#include "proxy_kbhook.h"
#include "proxy_exe.h"  // the build table: cam_app (g_Civ5App)
#include <windows.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "user32.lib")  // the unit tests' builds; the proxy links user32 anyway

namespace ProxyRtInt  // proxy_rt_internal.h (declared here so that the offline unit test needs no generated header)
{
void Log(const char* fmt, ...);
}  // namespace ProxyRtInt

namespace
{
using ProxyKbHook::Test::Api;
using ProxyKbHook::Test::SetHookFn;
using ProxyKbHook::Test::UnhookFn;

// ================================================================================================ the layout
// The app object (g_Civ5App, the build table's cam_app) in both builds (research file section 3, [C] byte read of
// both golden copies 2026-11-23): the hook's handle, the EXE's HINSTANCE (the hMod of its installs) and the main window.
const DWORD APP_HOOK = 0x7C;
const DWORD APP_HINSTANCE = 0xA0;
const DWORD APP_HWND = 0xA4;
const DWORD LOG_EVERY_MS = 10000;  // OnUpdate's change line: at most one per 10 s
const UINT WM_KB_SYNC = WM_APP + 0x720;  // to the helper thread: apply the current request

// ================================================================================================ state
Api g_api = {NULL, NULL, NULL, NULL, NULL};
SetHookFn g_prevSet = NULL;      // the slots' previous values (what the EXE's other calls are forwarded to)
UnhookFn g_prevUnhook = NULL;
DWORD* g_pSetSlot = NULL;
DWORD* g_pUnhookSlot = NULL;
char g_pseudoTag = 0;            // its address is the pseudo handle the thunk gives the EXE
HMODULE g_hSelf = NULL;          // the helper hook's hMod (this image)

bool g_bTried = false;
bool g_bInstalled = false;
const char* g_szStatus = "not installed (no boot yet)";
char g_szStatusBuf[300] = {0};
char g_szReason[200] = {0};

volatile LONG g_wanted = 0;      // the EXE's request (1 between its install and its unhook)
volatile LONG g_stop = 0;        // the unload: the helper removes its hook and stops
volatile LONG g_reqSeq = 0;      // requests made (Ask)
volatile LONG g_doneSeq = 0;     // requests the helper has applied
volatile LONG g_takenOver = 0;   // the handle taken over at boot (released by the EXE's unhook of it)

HANDLE g_hThread = NULL;
DWORD g_helperTid = 0;
HANDLE g_hReady = NULL;          // the helper has its message queue
HANDLE g_hStopped = NULL;        // the helper has removed its hook for the unload
HHOOK g_hHelperHook = NULL;      // helper thread only
volatile LONG g_helperHooked = 0;
volatile LONG g_helperErr = 0;   // GetLastError of the last failed helper install

volatile LONG g_requests = 0, g_releases = 0, g_forwarded = 0, g_swallowed = 0, g_passedOther = 0, g_hookErrors = 0;
volatile LONG g_events = 0;      // key events through the helper's hook
volatile LONG g_maxLagMs = 0;    // largest (now - event time) seen in the helper's hook
// OnUpdate's own state (game-core thread only)
LONG g_logged[6] = {0, 0, 0, 0, 0, 0};
DWORD g_lastLogTick = 0;
bool g_errSaid = false;

void Raise(volatile LONG* p, LONG v)
{
	LONG seen = *p;
	while (v > seen)
	{
		const LONG prev = InterlockedCompareExchange(p, v, seen);
		if (prev == seen) break;
		seen = prev;
	}
}

HHOOK Pseudo()
{
	return reinterpret_cast<HHOOK>(&g_pseudoTag);
}

// ================================================================================================ the helper thread
bool ForegroundIsOurs()
{
	HWND fg = g_api.foreground();
	if (fg == NULL) return false;
	DWORD pid = 0;
	g_api.windowPid(fg, &pid);
	return pid == GetCurrentProcessId();
}

LRESULT CALLBACK HelperHookProc(int code, WPARAM wParam, LPARAM lParam)
{
	if (code == HC_ACTION && lParam != 0)
	{
		const KBDLLHOOKSTRUCT* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
		InterlockedIncrement(&g_events);
		Raise(&g_maxLagMs, static_cast<LONG>(GetTickCount() - k->time));
		if (wParam == WM_SYSKEYDOWN && k->vkCode == VK_RETURN)
		{
			// The EXE's rule (Alt+Enter swallowed), only while the game is in front (MC #12349).
			if (ForegroundIsOurs())
			{
				InterlockedIncrement(&g_swallowed);
				return 1;
			}
			InterlockedIncrement(&g_passedOther);
		}
	}
	return CallNextHookEx(NULL, code, wParam, lParam);
}

// Helper thread: installs or removes its hook to match the request.
void Apply()
{
	const LONG seq = g_reqSeq;
	const bool want = g_wanted != 0 && g_stop == 0;
	if (want && g_hHelperHook == NULL)
	{
		g_hHelperHook = g_api.setHook(WH_KEYBOARD_LL, HelperHookProc, g_hSelf, 0);
		if (g_hHelperHook == NULL)
		{
			InterlockedExchange(&g_helperErr, static_cast<LONG>(GetLastError()));
			InterlockedIncrement(&g_hookErrors);
		}
	}
	else if (!want && g_hHelperHook != NULL)
	{
		g_api.unhook(g_hHelperHook);
		g_hHelperHook = NULL;
	}
	InterlockedExchange(&g_helperHooked, g_hHelperHook != NULL ? 1 : 0);
	InterlockedExchange(&g_doneSeq, seq);
}

DWORD WINAPI HelperMain(LPVOID)
{
	MSG m;
	PeekMessageW(&m, NULL, WM_USER, WM_USER, PM_NOREMOVE);  // the thread's message queue exists from here
	SetEvent(g_hReady);
	Apply();
	// Nothing but GetMessage: the hook procedure runs inside it, so this thread never holds up the keyboard.
	while (GetMessageW(&m, NULL, 0, 0) > 0)
	{
		if (m.hwnd == NULL && m.message == WM_KB_SYNC)
		{
			Apply();
			if (g_stop != 0)
			{
				SetEvent(g_hStopped);
				for (;;) Sleep(INFINITE);  // ended by OnUnload's TerminateThread (the loader lock is held there)
			}
		}
	}
	return 0;
}

// Any thread: asks the helper to apply the current request.
void Ask()
{
	InterlockedIncrement(&g_reqSeq);
	if (g_helperTid != 0) PostThreadMessageW(g_helperTid, WM_KB_SYNC, 0, 0);
}

// ================================================================================================ the thunks
HHOOK WINAPI SetHookThunk(int idHook, HOOKPROC proc, HINSTANCE mod, DWORD threadId)
{
	if (idHook == WH_KEYBOARD_LL && threadId == 0 && g_stop == 0)
	{
		InterlockedIncrement(&g_requests);
		InterlockedExchange(&g_wanted, 1);
		Ask();
		return Pseudo();
	}
	InterlockedIncrement(&g_forwarded);
	return g_prevSet(idHook, proc, mod, threadId);
}

BOOL WINAPI UnhookThunk(HHOOK hook)
{
	const LONG taken = g_takenOver;
	if (hook != NULL && (hook == Pseudo() || (taken != 0 && reinterpret_cast<LONG>(hook) == taken)))
	{
		if (hook != Pseudo()) InterlockedCompareExchange(&g_takenOver, 0, taken);  // a handle value is released once
		InterlockedIncrement(&g_releases);
		InterlockedExchange(&g_wanted, 0);
		Ask();
		return TRUE;
	}
	InterlockedIncrement(&g_forwarded);
	return g_prevUnhook(hook);
}

// ================================================================================================ the checks
bool IsReadable(DWORD addr, DWORD bytes)
{
	return ProxyExe::Readable(static_cast<uintptr_t>(addr), bytes);
}

// The image's import slot of USER32.dll!<name>, found by name, or NULL (as proxy_queue_guard.cpp's FindImportSlot).
DWORD* FindUser32Slot(DWORD base, const IMAGE_NT_HEADERS* nt, const char* name)
{
	const DWORD size = nt->OptionalHeader.SizeOfImage;
	const size_t nameLen = strlen(name) + 1;
	const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (dir.VirtualAddress == 0 || dir.Size == 0 || dir.VirtualAddress >= size) return NULL;
	for (DWORD desc = base + dir.VirtualAddress;; desc += sizeof(IMAGE_IMPORT_DESCRIPTOR))
	{
		if (desc + sizeof(IMAGE_IMPORT_DESCRIPTOR) > base + size || !IsReadable(desc, sizeof(IMAGE_IMPORT_DESCRIPTOR)))
			return NULL;
		const IMAGE_IMPORT_DESCRIPTOR* imp = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(desc);
		if (imp->Name == 0) return NULL;
		if (imp->Name >= size || imp->OriginalFirstThunk == 0 || imp->FirstThunk == 0 || imp->OriginalFirstThunk >= size ||
		    imp->FirstThunk >= size || !IsReadable(base + imp->Name, 11) ||
		    _strnicmp(reinterpret_cast<const char*>(base + imp->Name), "USER32.dll", 11) != 0)
			continue;
		DWORD at = base + imp->OriginalFirstThunk;
		DWORD iat = base + imp->FirstThunk;
		for (;; at += 4, iat += 4)
		{
			if (at + 4 > base + size || iat + 4 > base + size || !IsReadable(at, 4) || !IsReadable(iat, 4)) return NULL;
			const DWORD entry = *reinterpret_cast<const DWORD*>(at);
			if (entry == 0) break;
			if (IMAGE_SNAP_BY_ORDINAL32(entry) || entry + 2 + nameLen > size || !IsReadable(base + entry + 2, nameLen))
				continue;
			if (memcmp(reinterpret_cast<const char*>(base + entry + 2), name, nameLen) == 0)
				return reinterpret_cast<DWORD*>(iat);
		}
	}
}

// NULL if the module may be installed on the image at base, else the reason. *known: the build is one the patch knows
// (a refusal is LOUD then).
const char* VerifyExe(DWORD base, bool* known, DWORD** setSlot, DWORD** unhookSlot, DWORD* app)
{
	*known = false;
	if (!IsReadable(base, sizeof(IMAGE_DOS_HEADER))) return "off: no EXE image";
	const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x10000 ||
	    !IsReadable(base + dos->e_lfanew, sizeof(IMAGE_NT_HEADERS)))
		return "off: no EXE image";
	const IMAGE_NT_HEADERS* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE) return "off: no EXE image";
	const int build = VpExeBuildFromHeader(nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
	if (build == kVpExeBuildUnknown)
		return ProxyExe::UnknownBuild(g_szReason, sizeof(g_szReason), "off: ", nt->FileHeader.TimeDateStamp,
		                              nt->OptionalHeader.SizeOfImage);
	*known = true;
	if (!ProxyExe::Va(build, VPX_cam_app)) return ProxyExe::NotCarried(g_szReason, sizeof(g_szReason), "off: ", build, VPX_cam_app);
	const DWORD appGlobal = static_cast<DWORD>(ProxyExe::Addr(build, VPX_cam_app, base));
	if (!IsReadable(appGlobal, 4)) return "off: g_Civ5App is not readable";
	const DWORD a = *reinterpret_cast<const DWORD*>(appGlobal);
	if (a == 0 || !IsReadable(a, APP_HWND + 4)) return "off: the app object is not there";
	if (*reinterpret_cast<const DWORD*>(a + APP_HINSTANCE) != base)
		return "off: the app object's HINSTANCE is not the EXE (the layout is not as expected)";
	if (!g_api.isWindow(*reinterpret_cast<HWND const*>(a + APP_HWND)))
		return "off: the app object's window is not a window (the layout is not as expected)";

	DWORD* s = FindUser32Slot(base, nt, "SetWindowsHookExA");
	DWORD* u = FindUser32Slot(base, nt, "UnhookWindowsHookEx");
	if (s == NULL) return "off: no USER32.dll SetWindowsHookExA import found by name";
	if (u == NULL) return "off: no USER32.dll UnhookWindowsHookEx import found by name";
	if (*s == 0 || *u == 0) return "off: an import slot is not bound";
	if (*s == reinterpret_cast<DWORD>(&SetHookThunk) || *u == reinterpret_cast<DWORD>(&UnhookThunk))
		return "off: an import slot already holds this module's thunk";
	*setSlot = s;
	*unhookSlot = u;
	*app = a;
	return NULL;
}

bool WriteSlot(DWORD* slot, DWORD value)
{
	DWORD old = 0, ignored = 0;
	if (!VirtualProtect(slot, sizeof(DWORD), PAGE_READWRITE, &old)) return false;
	InterlockedExchange(reinterpret_cast<LONG volatile*>(slot), static_cast<LONG>(value));
	VirtualProtect(slot, sizeof(DWORD), old, &ignored);
	return true;
}

// A compare-and-swap: a hook another module wrote on top of the thunk is left alone.
void RestoreSlot(DWORD* slot, DWORD previous, DWORD thunk)
{
	if (slot == NULL) return;
	DWORD old = 0, ignored = 0;
	if (!VirtualProtect(slot, sizeof(DWORD), PAGE_READWRITE, &old)) return;
	InterlockedCompareExchange(reinterpret_cast<LONG volatile*>(slot), static_cast<LONG>(previous), static_cast<LONG>(thunk));
	VirtualProtect(slot, sizeof(DWORD), old, &ignored);
}

void StopHelper()
{
	if (g_hThread == NULL) return;
	InterlockedExchange(&g_stop, 1);
	Ask();
	if (g_hStopped) WaitForSingleObject(g_hStopped, 500);  // the helper removes its hook (microseconds)
	TerminateThread(g_hThread, 0);
	WaitForSingleObject(g_hThread, 1000);  // a terminated thread is signalled at once; no loader lock is needed for it
	CloseHandle(g_hThread);
	g_hThread = NULL;
	g_helperTid = 0;
	g_hHelperHook = NULL;  // removed by the helper, or gone with it
	InterlockedExchange(&g_helperHooked, 0);
}

const char* InstallOn(DWORD base, const Api& api)
{
	if (g_bTried) return g_bInstalled ? NULL : g_szStatus;
	g_bTried = true;
	g_api = api;
	if (!g_api.setHook) g_api.setHook = &SetWindowsHookExA;
	if (!g_api.unhook) g_api.unhook = &UnhookWindowsHookEx;
	if (!g_api.foreground) g_api.foreground = &GetForegroundWindow;
	if (!g_api.windowPid) g_api.windowPid = &GetWindowThreadProcessId;
	if (!g_api.isWindow) g_api.isWindow = &IsWindow;

	bool known = false;
	DWORD *setSlot = NULL, *unhookSlot = NULL, app = 0;
	const char* problem = VerifyExe(base, &known, &setSlot, &unhookSlot, &app);
	if (problem == NULL &&
	    !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                        reinterpret_cast<LPCWSTR>(&HelperHookProc), &g_hSelf))
		problem = "off: this module's handle is not known (GetModuleHandleEx failed)";
	if (problem == NULL)
	{
		g_stop = 0;
		g_hReady = CreateEventW(NULL, TRUE, FALSE, NULL);
		g_hStopped = CreateEventW(NULL, TRUE, FALSE, NULL);
		if (g_hReady && g_hStopped) g_hThread = CreateThread(NULL, 64 * 1024, HelperMain, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, &g_helperTid);
		if (g_hThread == NULL || WaitForSingleObject(g_hReady, 2000) != WAIT_OBJECT_0)
		{
			if (g_hThread) StopHelper();
			problem = "off: the helper thread could not be started";
		}
	}
	if (problem == NULL)
	{
		g_prevSet = reinterpret_cast<SetHookFn>(*setSlot);
		g_prevUnhook = reinterpret_cast<UnhookFn>(*unhookSlot);
		g_pSetSlot = setSlot;
		g_pUnhookSlot = unhookSlot;
		if (!WriteSlot(setSlot, reinterpret_cast<DWORD>(&SetHookThunk)))
			problem = "off: the SetWindowsHookExA import slot could not be made writable";
		else if (!WriteSlot(unhookSlot, reinterpret_cast<DWORD>(&UnhookThunk)))
		{
			RestoreSlot(setSlot, reinterpret_cast<DWORD>(g_prevSet), reinterpret_cast<DWORD>(&SetHookThunk));
			problem = "off: the UnhookWindowsHookEx import slot could not be made writable";
		}
		if (problem) StopHelper();
	}
	if (problem != NULL)
	{
		g_pSetSlot = g_pUnhookSlot = NULL;
		g_szStatus = problem;
		ProxyRtInt::Log("%skbhook: the EXE's keyboard hook stays on its main thread: %s%s", known ? "LOUD: " : "", problem,
		                known ? " (ticket 0720)" : "");
		return problem;
	}
	g_bInstalled = true;

	// The take-over (this thread is the EXE's main thread in the product, the one that installed the hook): the
	// handle the EXE stored last is removed for real; the request is on when the game is in front.
	const DWORD handle = *reinterpret_cast<const DWORD*>(app + APP_HOOK);
	char took[160];
	if (handle != 0 && handle != reinterpret_cast<DWORD>(Pseudo()))
	{
		SetLastError(0);
		const BOOL ok = g_api.unhook(reinterpret_cast<HHOOK>(handle));
		const DWORD err = ok ? 0 : GetLastError();
		InterlockedExchange(&g_takenOver, static_cast<LONG>(handle));
		if (ok)
			_snprintf_s(took, sizeof(took), _TRUNCATE, "the EXE's hook 0x%08lX removed", static_cast<unsigned long>(handle));
		else
			_snprintf_s(took, sizeof(took), _TRUNCATE,
			            "the EXE's hook 0x%08lX not removed (UnhookWindowsHookEx error %lu: already gone)",
			            static_cast<unsigned long>(handle), static_cast<unsigned long>(err));
	}
	else
		_snprintf_s(took, sizeof(took), _TRUNCATE, "no EXE hook stored (0x%08lX)", static_cast<unsigned long>(handle));
	const bool front = ForegroundIsOurs();
	if (front)
	{
		InterlockedExchange(&g_wanted, 1);
		Ask();
	}
	_snprintf_s(g_szStatusBuf, sizeof(g_szStatusBuf), _TRUNCATE,
	            "active (slots 0x%08lX and 0x%08lX; helper thread %lu; at boot %s; the game %s in front)",
	            static_cast<unsigned long>(reinterpret_cast<DWORD>(setSlot)),
	            static_cast<unsigned long>(reinterpret_cast<DWORD>(unhookSlot)), static_cast<unsigned long>(g_helperTid),
	            took, front ? "was" : "was not");
	g_szStatus = g_szStatusBuf;
	ProxyRtInt::Log("kbhook: the EXE's low-level keyboard hook runs on the proxy's helper thread: %s; Alt+Enter is "
	                "swallowed only while the game is in front; hooks the EXE leaked before the proxy loaded have no "
	                "readable handle and stay (ticket 0720)",
	                g_szStatus);
	return NULL;
}

void UpdateOn(DWORD now)
{
	if (!g_bInstalled) return;
	if (g_hookErrors != 0 && !g_errSaid)
	{
		g_errSaid = true;
		ProxyRtInt::Log("LOUD: kbhook: the helper thread's SetWindowsHookExA(WH_KEYBOARD_LL) failed (error %ld): Alt+Enter "
		                "is not swallowed in the game (ticket 0720)",
		                static_cast<long>(g_helperErr));
	}
	const LONG cur[6] = {g_requests, g_releases, g_forwarded, g_swallowed, g_passedOther, g_hookErrors};
	bool changed = false;
	for (int i = 0; i < 6; ++i) changed = changed || cur[i] != g_logged[i];
	if (!changed || (g_lastLogTick != 0 && now - g_lastLogTick < LOG_EVERY_MS)) return;
	g_lastLogTick = now ? now : 1;
	for (int i = 0; i < 6; ++i) g_logged[i] = cur[i];
	ProxyRtInt::Log("kbhook: the EXE asked for its hook %ld time(s) and released it %ld time(s) (request now %s; helper "
	                "hook %s); Alt+Enter swallowed %ld (the game in front), passed %ld (another program in front); other "
	                "calls forwarded %ld; key events %ld, largest hook lag %ld ms; helper install errors %ld",
	                static_cast<long>(cur[0]), static_cast<long>(cur[1]), g_wanted ? "on" : "off",
	                g_helperHooked ? "installed" : "not installed", static_cast<long>(cur[3]), static_cast<long>(cur[4]),
	                static_cast<long>(cur[2]), static_cast<long>(g_events), static_cast<long>(g_maxLagMs),
	                static_cast<long>(cur[5]));
}

void Unload()
{
	if (!g_bInstalled) return;
	RestoreSlot(g_pSetSlot, reinterpret_cast<DWORD>(g_prevSet), reinterpret_cast<DWORD>(&SetHookThunk));
	RestoreSlot(g_pUnhookSlot, reinterpret_cast<DWORD>(g_prevUnhook), reinterpret_cast<DWORD>(&UnhookThunk));
	StopHelper();
}
}  // namespace

namespace ProxyKbHook
{
// Ticket 0745 (D255 gap G2): [Proxy] KeyboardHookThread of VP64Options.ini, set by proxy_rt.cpp after the options load
// and before ProxyShell::OnBoot (declared there and in rt_tests/kbhook_unit.cpp, as ProxyRtInt::Log is declared here,
// so that the offline unit test needs no options code). 1 (the default, ticket 0720's behaviour) = Install moves the
// hook and switches ghosting off; 0 = Install does nothing: the EXE's own hook on its main thread, as stock.
namespace
{
int g_threadOption = 1;
bool g_threadOptionFromFile = false;
}  // namespace
void SetThreadOption(int value, bool fromFile)
{
	g_threadOption = value;
	g_threadOptionFromFile = fromFile;
}

void Install()
{
	if (g_threadOption == 0)
	{
		g_szStatus = "off by [Proxy] KeyboardHookThread = 0: the EXE's own hook on its main thread, as stock";
		ProxyRtInt::Log("kbhook: [Proxy] KeyboardHookThread = 0 (%s): the EXE's low-level keyboard hook stays on its main "
		                "thread and window ghosting stays on, as stock (ticket 0745)",
		                g_threadOptionFromFile ? "from the file" : "default");
		return;
	}
	Api api = {NULL, NULL, NULL, NULL, NULL};
	InstallOn(reinterpret_cast<DWORD>(GetModuleHandleA(NULL)), api);
	// MC #12349: a main thread that does not pump for 5 s gets no DWM ghost window (which took the focus in the
	// owner's session). An API call on this process only; user32 has it since Windows XP.
	typedef void(WINAPI * DisableGhostingFn)();
	HMODULE user32 = GetModuleHandleA("user32.dll");
	DisableGhostingFn fn = user32 ? reinterpret_cast<DisableGhostingFn>(GetProcAddress(user32, "DisableProcessWindowsGhosting")) : NULL;
	if (fn) fn();
	ProxyRtInt::Log("kbhook: window ghosting %s for this process (a stalled main thread gets no DWM ghost window that "
	                "takes the focus; ticket 0720)",
	                fn ? "disabled" : "NOT disabled (DisableProcessWindowsGhosting not found)");
}

void OnUpdate()
{
	UpdateOn(GetTickCount());
}

void OnUnload()
{
	Unload();
}

void StatusLine(char* out, size_t cap)
{
	_snprintf_s(out, cap, _TRUNCATE,
	            "keyboard hook: %s; requests %ld, releases %ld, forwarded %ld; Alt+Enter swallowed %ld, passed %ld; key "
	            "events %ld, largest hook lag %ld ms; helper install errors %ld",
	            g_szStatus, static_cast<long>(g_requests), static_cast<long>(g_releases), static_cast<long>(g_forwarded),
	            static_cast<long>(g_swallowed), static_cast<long>(g_passedOther), static_cast<long>(g_events),
	            static_cast<long>(g_maxLagMs), static_cast<long>(g_hookErrors));
}

namespace Test
{
const char* InstallAt(void* base, const Api& api)
{
	return InstallOn(reinterpret_cast<DWORD>(base), api);
}
void UpdateAt(DWORD tick)
{
	UpdateOn(tick);
}
bool WaitSynced(DWORD ms)
{
	const DWORD t0 = GetTickCount();
	for (;;)
	{
		if (g_doneSeq == g_reqSeq) return true;
		if (GetTickCount() - t0 >= ms) return false;
		Sleep(1);
	}
}
const void* SetThunk()
{
	return reinterpret_cast<const void*>(&SetHookThunk);
}
const void* UnhookThunk()
{
	return reinterpret_cast<const void*>(&::UnhookThunk);
}
HHOOK Pseudo()
{
	return ::Pseudo();
}
LRESULT CALLBACK HookProc(int code, WPARAM wParam, LPARAM lParam)
{
	return HelperHookProc(code, wParam, lParam);
}
bool Installed()
{
	return g_bInstalled;
}
bool Wanted()
{
	return g_wanted != 0;
}
bool HelperHooked()
{
	return g_helperHooked != 0;
}
const char* Status()
{
	return g_szStatus;
}
long Requests()
{
	return g_requests;
}
long Releases()
{
	return g_releases;
}
long Forwarded()
{
	return g_forwarded;
}
long Swallowed()
{
	return g_swallowed;
}
long PassedOther()
{
	return g_passedOther;
}
long HelperHookErrors()
{
	return g_hookErrors;
}
DWORD TakenOver()
{
	return static_cast<DWORD>(g_takenOver);
}
void Reset()
{
	Unload();
	if (g_hReady) CloseHandle(g_hReady);
	if (g_hStopped) CloseHandle(g_hStopped);
	g_hReady = g_hStopped = NULL;
	g_prevSet = NULL;
	g_prevUnhook = NULL;
	g_pSetSlot = g_pUnhookSlot = NULL;
	g_bTried = g_bInstalled = g_errSaid = false;
	g_szStatus = "not installed (no boot yet)";
	g_wanted = g_stop = g_reqSeq = g_doneSeq = g_takenOver = g_helperHooked = g_helperErr = 0;
	g_requests = g_releases = g_forwarded = g_swallowed = g_passedOther = g_hookErrors = g_events = g_maxLagMs = 0;
	for (int i = 0; i < 6; ++i) g_logged[i] = 0;
	g_lastLogTick = 0;
}
}  // namespace Test
}  // namespace ProxyKbHook
