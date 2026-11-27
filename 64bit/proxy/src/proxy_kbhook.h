// proxy_kbhook.h - the EXE's global keyboard hook moved off its main thread (ticket 0720). Hand-written.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The EXE installs a global low-level keyboard hook (SetWindowsHookExA(WH_KEYBOARD_LL)) whose only job is to swallow
// Alt+Enter. A low-level hook runs on the thread that installed it, the EXE's main thread: while that thread does not
// pump messages (the patch holds it for seconds in a lockstep wait or a load), every keystroke of the whole session
// waits for it, in every program. The EXE also leaks the hook: each of its three installs overwrites the stored handle
// without unhooking it, so one or two hooks stay on the main thread for the life of the process
// (research/02-exe-internals/keyboard-hook-and-hangs.md section 3).
//
// This module points two of the EXE's import slots at thunks (data writes only, D228; no EXE code byte is read or
// written; the slots are found by name in the import directory):
//   USER32!SetWindowsHookExA  a WH_KEYBOARD_LL request for every thread (the EXE's only use) is not installed: the
//                             thunk returns a pseudo handle and asks the proxy's own helper thread for the hook;
//                             every other hook type is forwarded to the slot's previous value.
//   USER32!UnhookWindowsHookEx the pseudo handle (and the handle taken over at boot) ends the request; every other
//                             handle is forwarded.
// The helper thread does nothing but pump messages (GetMessage), so its hook never holds up the keyboard. Its hook
// procedure does what the EXE's did, with one change the MC asked for (#12349): Alt+Enter (WM_SYSKEYDOWN with
// VK_RETURN, nCode >= 0) is swallowed only while the foreground window belongs to this process; in every other program
// it passes (stock swallowed it in every program while its hook was installed: a stock bug, dropped).
//
// At boot, on the main thread: the hook the EXE installed last (its handle at [g_Civ5App] + 0x7C, the build table's
// cam_app; the layout is checked by [app] + 0xA0 == the EXE's HINSTANCE and [app] + 0xA4 == a window) is removed with
// the real UnhookWindowsHookEx, and the request is on when the game's window is the foreground window. Hooks the EXE
// leaked before the proxy was loaded have no readable handle and stay (Windows drops a low-level hook that times out,
// research file section 3). DisableProcessWindowsGhosting is called once (MC #12349): a main thread that does not pump
// for 5 s no longer gets a DWM ghost window that takes the focus.
//
// Lifetime: Install from ProxyShell::OnBoot (mode=on; the EXE's main thread, DllGetGameContext); never in mode=off (the
// 32-bit DLL's game does not stall its main thread). OnUpdate from ProxyShell::OnUpdate logs what changed (at most one
// line per 10 s). OnUnload (DLL_PROCESS_DETACH, loader lock, no logging) gives both slots their previous values back if
// they still hold the thunks, removes the helper's hook and ends the helper thread.
#pragma once
#include <windows.h>
#include <stddef.h>

namespace ProxyKbHook
{
// From ProxyShell::OnBoot (the EXE's main thread). Checks the EXE, writes the two slots, takes over the EXE's hook,
// starts the helper thread, disables ghosting; one log line each. A second call in one image does nothing.
void Install();
// From ProxyShell::OnUpdate (the game-core thread). Cheap when nothing changed.
void OnUpdate();
// From ProxyShell::OnUnload (loader lock). Logs nothing.
void OnUnload();
// One line for the status: "keyboard hook: <status>; requests ..., swallowed ...".
void StatusLine(char* out, size_t cap);

// ---- for the offline tests (proxy/tools/rt_tests/kbhook_unit.cpp); the product calls none of these ----------------
namespace Test
{
typedef HHOOK(WINAPI* SetHookFn)(int idHook, HOOKPROC proc, HINSTANCE mod, DWORD threadId);
typedef BOOL(WINAPI* UnhookFn)(HHOOK hook);
// What the module calls instead of user32 (each NULL = the real function).
struct Api
{
	SetHookFn setHook;                       // the helper's own install (the real SetWindowsHookExA)
	UnhookFn unhook;                         // the helper's removal and the boot take-over (the real UnhookWindowsHookEx)
	HWND(WINAPI* foreground)();              // GetForegroundWindow
	DWORD(WINAPI* windowPid)(HWND, LPDWORD); // GetWindowThreadProcessId
	BOOL(WINAPI* isWindow)(HWND);            // IsWindow
};
// Install against the image at `base` with the given functions (the boot take-over uses api.unhook too); NULL =
// installed, else the reason it was refused. Ghosting is not touched (the product's Install does that).
const char* InstallAt(void* base, const Api& api);
// One OnUpdate step with `tick` as the time (GetTickCount's clock).
void UpdateAt(DWORD tick);
// Waits until the helper thread has applied every request made so far (true) or `ms` passed (false).
bool WaitSynced(DWORD ms);
// The thunks' own addresses (what the slots hold once installed) and the pseudo handle they return.
const void* SetThunk();
const void* UnhookThunk();
HHOOK Pseudo();
// The helper's hook procedure, called directly (as Windows would on the helper thread).
LRESULT CALLBACK HookProc(int code, WPARAM wParam, LPARAM lParam);
// State.
bool Installed();
bool Wanted();          // the EXE's request is on
bool HelperHooked();    // the helper thread holds its hook
const char* Status();
long Requests();        // WH_KEYBOARD_LL installs the thunk answered
long Releases();        // unhooks of the pseudo handle (or the taken-over one)
long Forwarded();       // calls passed to the previous slot values
long Swallowed();       // Alt+Enter swallowed (the game in front)
long PassedOther();     // Alt+Enter passed (another program in front)
long HelperHookErrors(); // helper installs that failed
DWORD TakenOver();      // the handle taken over at boot (0: none)
// Gives the slots back, ends the helper thread and forgets everything (as at an unload, then a fresh image).
void Reset();
}  // namespace Test
}  // namespace ProxyKbHook
