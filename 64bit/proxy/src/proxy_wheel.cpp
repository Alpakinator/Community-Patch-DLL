// proxy_wheel.cpp - [Extras] FixSmoothWheel: whole notches from a trackpad or a smooth wheel (ticket 0755). See
// proxy_wheel.h.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Written 2026-11-25 for this repository. The behaviour is the knowledge base's (research/02-exe-internals/
// camera-and-picking.md 11.4, from the prototype of perf/evidence/5042/wheel_probe.cpp, this repository's own test
// instrument); the import-slot walk follows proxy_kbhook.cpp's (ticket 0720). No EXE code is read or reproduced (D83).
// Toolchain: clang-cl -m32, VC9 headers, C++03. x86 only (pointers are DWORDs).
#include "proxy_wheel.h"
#include "vp_exe_build.h"  // the known builds (exebuild/include; header only, no addresses)
#include <windows.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "user32.lib")  // the unit tests' builds; the proxy links user32 anyway

namespace ProxyRtInt  // proxy_rt_internal.h (declared here so that the offline unit test needs no generated header)
{
void Log(const char* fmt, ...);
}  // namespace ProxyRtInt

namespace ProxyWheel
{
// ================================================================================================ the core
void Reset(Summer& s)
{
	s.remainder = 0;
	s.lastMs = 0;
	s.droppedBySign = 0;
	s.droppedByTime = 0;
}

bool Expire(Summer& s, DWORD nowMs)
{
	if (s.remainder == 0 || nowMs - s.lastMs <= kResetMs) return false;
	s.remainder = 0;
	++s.droppedByTime;
	return true;
}

int Take(Summer& s, int delta, DWORD nowMs)
{
	if (delta == 0) return 0;
	if (s.remainder != 0)
	{
		if (!Expire(s, nowMs) && (s.remainder > 0) != (delta > 0))
		{
			s.remainder = 0;
			++s.droppedBySign;
		}
	}
	s.lastMs = nowMs;
	s.remainder += delta;
	const int notches = s.remainder / kNotch;  // truncated toward zero: the rest keeps the sign
	s.remainder -= notches * kNotch;
	return notches;
}

namespace
{
using ProxyWheel::Test::Api;
using ProxyWheel::Test::PeekFn;

const DWORD LOG_EVERY_MS = 10000;  // the thunk's counters line: at most one per 10 s

// ================================================================================================ state
Api g_api = {NULL, NULL, NULL};
PeekFn g_prevPeek = NULL;       // the slot's previous value (every call is forwarded to it)
DWORD* g_pSlot = NULL;
DWORD g_mainTid = 0;

bool g_bTried = false;
bool g_bInstalled = false;
const char* g_szStatus = "not installed (no boot yet)";
char g_szStatusBuf[300] = {0};
char g_szReason[200] = {0};

// main-thread state (the thunk's main-loop calls only)
Summer g_sum = {0, 0, 0, 0};
int g_pending = 0;              // further notches of the last merged message still to hand out
MSG g_pendingMsg;               // what each of them is (the message as it passed, delta one notch)
bool g_front = false;           // the game's thread held the foreground when the state became non-empty
DWORD g_lastLogTick = 0;
long g_logged[8] = {0, 0, 0, 0, 0, 0, 0, 0};

// counters (written on the main thread; read anywhere)
volatile LONG g_wheel = 0, g_passed = 0, g_held = 0, g_handed = 0, g_merged = 0, g_capped = 0, g_focus = 0;
volatile LONG g_forwarded = 0;

bool Holding()
{
	return g_pending > 0 || g_sum.remainder != 0;
}

bool InFront()
{
	HWND fg = g_api.foreground();
	if (fg == NULL) return false;
	return g_api.windowThread(fg, NULL) == g_mainTid;
}

void DropAll()
{
	g_sum.remainder = 0;
	g_pending = 0;
}

void CountersText(char* out, size_t cap)
{
	_snprintf_s(out, cap, _TRUNCATE,
	            "wheel messages %ld: passed %ld (one notch each), held %ld (WM_NULL); merged messages %ld, further notches "
	            "handed out %ld, capped %ld; remainders dropped: turned %ld, older than %lu ms %ld, foreground change %ld; "
	            "other calls forwarded %ld",
	            static_cast<long>(g_wheel), static_cast<long>(g_passed), static_cast<long>(g_held),
	            static_cast<long>(g_merged), static_cast<long>(g_handed), static_cast<long>(g_capped),
	            g_sum.droppedBySign, static_cast<unsigned long>(kResetMs), g_sum.droppedByTime, static_cast<long>(g_focus),
	            static_cast<long>(g_forwarded));
}

// At most one counters line per 10 s, when something changed (main thread, after a wheel message).
void MaybeLog(DWORD now)
{
	const long cur[8] = {g_wheel, g_passed, g_held, g_handed, g_merged, g_focus, g_sum.droppedBySign, g_sum.droppedByTime};
	bool changed = false;
	for (int i = 0; i < 8; ++i) changed = changed || cur[i] != g_logged[i];
	if (!changed || (g_lastLogTick != 0 && now - g_lastLogTick < LOG_EVERY_MS)) return;
	g_lastLogTick = now ? now : 1;
	for (int i = 0; i < 8; ++i) g_logged[i] = cur[i];
	char line[400];
	CountersText(line, sizeof(line));
	ProxyRtInt::Log("wheel: %s (ticket 0755)", line);
}

// ================================================================================================ the thunk
BOOL WINAPI PeekThunk(MSG* m, HWND w, UINT lo, UINT hi, UINT flags)
{
	if (m == NULL || (flags & PM_REMOVE) == 0 || w != NULL || lo != 0 || hi != 0 || GetCurrentThreadId() != g_mainTid)
	{
		InterlockedIncrement(&g_forwarded);
		return g_prevPeek(m, w, lo, hi, flags);
	}
	const DWORD now = g_api.tick();
	if (Holding())
	{
		Expire(g_sum, now);
		const bool front = InFront();
		if (front != g_front)
		{
			// the game's window gained or lost the foreground (its WM_ACTIVATE) since the state was formed
			DropAll();
			InterlockedIncrement(&g_focus);
			g_front = front;
		}
	}
	if (g_pending > 0)
	{
		*m = g_pendingMsg;
		--g_pending;
		InterlockedIncrement(&g_handed);
		return TRUE;
	}
	const BOOL r = g_prevPeek(m, w, lo, hi, flags);
	if (!r || m->message != WM_MOUSEWHEEL) return r;
	InterlockedIncrement(&g_wheel);
	const bool held = Holding();
	const int n = Take(g_sum, GET_WHEEL_DELTA_WPARAM(m->wParam), now);
	if (n == 0)
	{
		m->message = WM_NULL;  // the main loop dispatches it: nothing happens (11.4, proven by the prototype)
		m->wParam = 0;
		m->lParam = 0;
		InterlockedIncrement(&g_held);
	}
	else
	{
		int count = n < 0 ? -n : n;
		if (count > kMaxNotches)
		{
			InterlockedExchangeAdd(&g_capped, count - kMaxNotches);
			count = kMaxNotches;
		}
		const short one = static_cast<short>(n < 0 ? -kNotch : kNotch);
		m->wParam = static_cast<WPARAM>((static_cast<DWORD>(static_cast<WORD>(one)) << 16) | LOWORD(m->wParam));
		InterlockedIncrement(&g_passed);
		if (count > 1)
		{
			g_pendingMsg = *m;
			g_pending = count - 1;
			InterlockedIncrement(&g_merged);
		}
	}
	if (!held && Holding()) g_front = InFront();  // the state is formed now: its foreground reference
	MaybeLog(now);
	return r;
}

// ================================================================================================ the checks
bool IsReadable(DWORD addr, DWORD bytes)
{
	if (addr == 0 || bytes == 0) return false;
	DWORD at = addr;
	const DWORD end = addr + bytes;
	if (end < addr) return false;
	while (at < end)
	{
		MEMORY_BASIC_INFORMATION mbi;
		if (VirtualQuery(reinterpret_cast<LPCVOID>(at), &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
		if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
		    (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
		                    PAGE_EXECUTE_WRITECOPY)) == 0)
			return false;
		const DWORD regionEnd = reinterpret_cast<DWORD>(mbi.BaseAddress) + static_cast<DWORD>(mbi.RegionSize);
		if (regionEnd <= at) return false;
		at = regionEnd;
	}
	return true;
}

// The image's import slot of USER32.dll!<name>, found by name, or NULL (as proxy_kbhook.cpp's FindUser32Slot).
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

// NULL if the thunk may be installed on the image at base, else the reason. *known: the build is one the patch knows (a
// refusal is LOUD then).
const char* VerifyExe(DWORD base, bool* known, DWORD** slot)
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
	{
		_snprintf_s(g_szReason, sizeof(g_szReason), _TRUNCATE,
		            "off: an EXE build the patch does not know (time stamp 0x%08lX, image size 0x%08lX)",
		            static_cast<unsigned long>(nt->FileHeader.TimeDateStamp),
		            static_cast<unsigned long>(nt->OptionalHeader.SizeOfImage));
		return g_szReason;
	}
	*known = true;
	DWORD* s = FindUser32Slot(base, nt, "PeekMessageW");
	if (s == NULL) return "off: no USER32.dll PeekMessageW import found by name";
	if (*s == 0) return "off: the PeekMessageW import slot is not bound";
	if (*s == reinterpret_cast<DWORD>(&PeekThunk)) return "off: the import slot already holds this module's thunk";
	*slot = s;
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

// A compare-and-swap: a hook another module wrote on top of the thunk is left alone. True when the slot held the thunk.
bool RestoreSlot(DWORD* slot, DWORD previous, DWORD thunk)
{
	if (slot == NULL) return false;
	DWORD old = 0, ignored = 0;
	if (!VirtualProtect(slot, sizeof(DWORD), PAGE_READWRITE, &old)) return false;
	const LONG was = InterlockedCompareExchange(reinterpret_cast<LONG volatile*>(slot), static_cast<LONG>(previous),
	                                            static_cast<LONG>(thunk));
	VirtualProtect(slot, sizeof(DWORD), old, &ignored);
	return was == static_cast<LONG>(thunk);
}

const char* InstallOn(DWORD base, DWORD mainTid, const Api& api)
{
	if (g_bTried) return g_bInstalled ? NULL : g_szStatus;
	g_bTried = true;
	g_api = api;
	if (!g_api.tick) g_api.tick = &GetTickCount;
	if (!g_api.foreground) g_api.foreground = &GetForegroundWindow;
	if (!g_api.windowThread) g_api.windowThread = &GetWindowThreadProcessId;

	bool known = false;
	DWORD* slot = NULL;
	const char* problem = VerifyExe(base, &known, &slot);
	if (problem == NULL)
	{
		g_prevPeek = reinterpret_cast<PeekFn>(*slot);
		g_mainTid = mainTid;
		Reset(g_sum);
		g_pending = 0;
		if (!WriteSlot(slot, reinterpret_cast<DWORD>(&PeekThunk)))
			problem = "off: the PeekMessageW import slot could not be made writable";
	}
	if (problem != NULL)
	{
		g_prevPeek = NULL;
		g_szStatus = problem;
		ProxyRtInt::Log("%swheel: every wheel message stays one step, as stock: %s%s", known ? "LOUD: " : "", problem,
		                known ? " (ticket 0755)" : "");
		return problem;
	}
	g_pSlot = slot;
	g_bInstalled = true;
	_snprintf_s(g_szStatusBuf, sizeof(g_szStatusBuf), _TRUNCATE, "active (slot 0x%08lX, main loop thread %lu)",
	            static_cast<unsigned long>(reinterpret_cast<DWORD>(slot)), static_cast<unsigned long>(mainTid));
	g_szStatus = g_szStatusBuf;
	ProxyRtInt::Log("wheel: [Extras] FixSmoothWheel: the EXE's PeekMessageW import slot points at the proxy's thunk: %s; "
	                "wheel deltas are summed into whole notches (a remainder dropped when the wheel turns, after %lu ms or "
	                "at a foreground change) and a merged message's notches are handed out one by one (ticket 0755)",
	                g_szStatus, static_cast<unsigned long>(kResetMs));
	return NULL;
}

bool Unload()
{
	if (!g_bInstalled) return false;
	RestoreSlot(g_pSlot, reinterpret_cast<DWORD>(g_prevPeek), reinterpret_cast<DWORD>(&PeekThunk));
	return true;
}
}  // namespace

void Install(bool on, const char* offWhy)
{
	if (!on)
	{
		g_bTried = true;
		g_szStatus = "off by the options file: every wheel message one step, as stock";
		ProxyRtInt::Log("wheel: off (%s): every wheel message stays one step, as stock (ticket 0755)",
		                offWhy ? offWhy : "FixSmoothWheel not in effect");
		return;
	}
	Api api = {NULL, NULL, NULL};
	InstallOn(reinterpret_cast<DWORD>(GetModuleHandleA(NULL)), GetCurrentThreadId(), api);
}

bool OnUnload(char* line, size_t cap)
{
	if (line && cap) line[0] = 0;
	if (!Unload()) return false;
	if (line && cap)
	{
		char counters[400];
		CountersText(counters, sizeof(counters));
		_snprintf_s(line, cap, _TRUNCATE, "wheel: the PeekMessageW import slot given back; %s (ticket 0755)", counters);
	}
	return true;
}

void StatusLine(char* out, size_t cap)
{
	char counters[400];
	CountersText(counters, sizeof(counters));
	_snprintf_s(out, cap, _TRUNCATE, "smooth wheel: %s; %s", g_szStatus, counters);
}

namespace Test
{
const char* InstallAt(void* base, DWORD mainTid, const Api& api)
{
	return InstallOn(reinterpret_cast<DWORD>(base), mainTid, api);
}
const void* Thunk()
{
	return reinterpret_cast<const void*>(&PeekThunk);
}
bool Installed()
{
	return g_bInstalled;
}
const char* Status()
{
	return g_szStatus;
}
long WheelMessages()
{
	return g_wheel;
}
long Passed()
{
	return g_passed;
}
long Held()
{
	return g_held;
}
long HandedOut()
{
	return g_handed;
}
long Merged()
{
	return g_merged;
}
long Capped()
{
	return g_capped;
}
long FocusResets()
{
	return g_focus;
}
long Forwarded()
{
	return g_forwarded;
}
int Pending()
{
	return g_pending;
}
const Summer& State()
{
	return g_sum;
}
void Reset()
{
	Unload();
	g_prevPeek = NULL;
	g_pSlot = NULL;
	g_mainTid = 0;
	g_bTried = g_bInstalled = false;
	g_szStatus = "not installed (no boot yet)";
	ProxyWheel::Reset(g_sum);
	g_pending = 0;
	g_front = false;
	g_lastLogTick = 0;
	for (int i = 0; i < 8; ++i) g_logged[i] = 0;
	g_wheel = g_passed = g_held = g_handed = g_merged = g_capped = g_focus = g_forwarded = 0;
	Api none = {NULL, NULL, NULL};
	g_api = none;
}
}  // namespace Test
}  // namespace ProxyWheel
