// proxy_wheel.h - [Extras] FixSmoothWheel: whole notches from a trackpad or a smooth wheel (ticket 0755). Hand-written.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The EXE takes every WM_MOUSEWHEEL as one full step and reads only the sign of its delta (the world view's zoom and the
// UI's scroll panels alike; research/02-exe-internals/camera-and-picking.md 11.1). A trackpad or a high-resolution wheel
// sends a notch (120) as several small deltas, and Windows merges the events of one frame into one message, so stock
// zooms one step per frame a flick spans (3 steps for one notch at 30 fps) and a merged message of several notches one
// step only (11.3b). This module points the EXE's import slot of USER32!PeekMessageW at a thunk (a data write found by
// name in the import directory, D228; no EXE code byte is read or written) that does what 11.4 describes:
//   - only the EXE's main loop: the thread that booted the proxy, PeekMessageW(msg, NULL, 0, 0, PM_REMOVE ...); every
//     other call (another thread, a window or message filter, PM_NOREMOVE: the loop's mouse-move peek) is forwarded
//     unchanged;
//   - each WM_MOUSEWHEEL delta is added to a remainder (the core below); a remainder of the other sign, or older than
//     400 ms, is dropped first; whole notches leave the remainder: the message passes with a delta of one notch (its
//     key-state word kept), and each further notch it carried is handed out on the next calls of the main loop, before
//     anything else is taken off the queue (a pending count and a copy of the message; no PostMessage); a message that
//     completes no notch becomes WM_NULL (the loop dispatches it, nothing happens);
//   - a notched wheel is unchanged: a message of exactly +-120 with an empty remainder passes byte for byte;
//   - the remainder and the pending notches are dropped when the game's window gains or loses the foreground (its
//     WM_ACTIVATE; sent messages never pass PeekMessageW's return, so the thunk compares the foreground window's thread
//     with the main thread at each call while it holds anything, which is at most 400 ms after a partial notch);
//   - WM_MOUSEHWHEEL is not touched (the EXE does not use it, 11.1).
// Off (the key 0, EnableBugfixes = 0, or a refusal): the slot is never written, the EXE's own calls as stock.
//
// Lifetime: Install from proxy_rt.cpp's DllGetGameContext on a 64-bit start (mode=on), right after ProxyShell::OnBoot,
// on the EXE's main thread (the thread that loads the game DLL; ticket 0720's proxy_kbhook.h relies on the same).
// The front end before the DLL loads keeps stock behaviour. OnUnload (DLL_PROCESS_DETACH, loader lock) gives the slot
// its previous value back if it still holds the thunk, and returns the counters line for the detach log.
#pragma once
#include <windows.h>
#include <stddef.h>

namespace ProxyWheel
{
const int kNotch = 120;          // WHEEL_DELTA
const DWORD kResetMs = 400;      // a remainder older than this is dropped (11.4, the prototype's value)
const int kMaxNotches = 32;      // notches handed out for one message at most (the rest counted, dropped)

// ---- the core: no Windows call (the offline tests drive it directly) -----------------------------------------------
struct Summer
{
	int remainder;               // the summed delta not let through, |remainder| < kNotch between calls
	DWORD lastMs;                // when the last wheel message was taken (valid while remainder != 0)
	long droppedBySign;          // remainders dropped because the wheel turned the other way
	long droppedByTime;          // remainders dropped because they were older than kResetMs
};
void Reset(Summer& s);           // everything 0
// One WM_MOUSEWHEEL of `delta` taken at `nowMs` (GetTickCount's clock): the signed number of whole notches it lets
// through (0 = hold the message). A delta of 0 changes nothing and gives 0.
int Take(Summer& s, int delta, DWORD nowMs);
// Drops the remainder (counted in droppedByTime) when it is older than kResetMs; true when it did.
bool Expire(Summer& s, DWORD nowMs);

// ---- the product -----------------------------------------------------------------------------------------------
// From DllGetGameContext (mode=on, the EXE's main thread). on = [Extras] FixSmoothWheel in effect; offWhy = why not
// (for the log line) when !on. Checks the EXE, writes the slot; one log line. A second call in one image does nothing.
void Install(bool on, const char* offWhy);
// From DetachOnUnload (loader lock; logs nothing): the slot's previous value back if it still holds the thunk. Fills
// `line` with the counters line (the caller logs it) and returns true when the thunk was installed.
bool OnUnload(char* line, size_t cap);
// "smooth wheel: <status>; wheel messages N, passed P, held H, ...".
void StatusLine(char* out, size_t cap);

// ---- for the offline tests (proxy/tests/wheel/wheel_test.cpp); the product calls none of these -------------------
namespace Test
{
typedef BOOL(WINAPI* PeekFn)(MSG* msg, HWND wnd, UINT lo, UINT hi, UINT flags);
// What the module calls instead of the system (each NULL = the real function).
struct Api
{
	DWORD(WINAPI* tick)();                    // GetTickCount
	HWND(WINAPI* foreground)();               // GetForegroundWindow
	DWORD(WINAPI* windowThread)(HWND, LPDWORD); // GetWindowThreadProcessId
};
// Install against the image at `base`, the main loop's thread `mainTid`; NULL = installed, else the reason refused.
const char* InstallAt(void* base, DWORD mainTid, const Api& api);
const void* Thunk();             // what the slot holds once installed
bool Installed();
const char* Status();
long WheelMessages();            // WM_MOUSEWHEEL taken off the queue by the main loop's calls
long Passed();                   // let through with one notch
long Held();                     // turned into WM_NULL
long HandedOut();                // further notches handed out from the pending count
long Merged();                   // messages that carried more than one notch
long Capped();                   // notches above kMaxNotches dropped
long FocusResets();              // remainders or pending notches dropped by a foreground change
long Forwarded();                // calls passed on unchanged (another thread, a filter, PM_NOREMOVE)
int Pending();
const Summer& State();
void Reset();                    // the slot back (as OnUnload), every counter and the state forgotten
}  // namespace Test
}  // namespace ProxyWheel
