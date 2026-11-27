// proxy_shell.cpp - the shell's in-process jobs on the proxy route in their product form (issue 3305; first written as
// a port by one-off 9040): the composite DLL's load and the view-mask write. See proxy_shell.h.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Origin (COMMON rule 13, decision D22): the test-switch loader and the mask code are ported from
// the project's fork of Community-Patch-DLL, branch i2-shell-test, commit b92373aeb (GPL-3.0, like this repository):
//   CvGameCoreDLL_Expansion2/ShellHelperLoader.cpp (last changed in 0a7795fdf)  -> namespace Loader below
//   CvGameCoreDLL_Expansion2/ViewMaskSync.cpp      (last changed in b92373aeb)  -> namespace Mask below
// The EXE checks (timestamp, six code patterns, g_Civ5App inside two of them, the GameViewState vtable, VirtualQuery of
// every pointer) and the write (compare-and-swap of [GVS+0x1B4]) are the origin's, unchanged. Only data is written; no
// EXE code byte is touched (decision D4).
//
// Product form (issue 3305, research/05-graphics-composition/proxy-route-compositor.md section 4):
//   - switch: VP64Options.ini [Renderer] ServerRenderer (0 = nothing at all, as the proxy before 9040);
//   - the composite: <proxy folder>\shell_composite.dll, loaded only when <proxy folder>\shell_composite.sha256 names
//     the proxy's own SHA-256 (the build it was packaged with) and the composite's SHA-256; the file is held open
//     without write sharing from the check to LoadLibrary, so the bytes checked are the bytes loaded;
//   - names: server.cfg render=1, render_section=Local\VP64Shell_<pid>; VPSHELL_SECTION=<the same> in the EXE's
//     environment before LoadLibrary (the composite reads every ini key overridden by VPSHELL_<KEY>);
//   - the mask: allowed = (config | 0xFF000000) & ~Terrain (Terrain only while the composite is loaded: D7 c1; Waves
//     with it; Arrows with ServerArc = 1, ticket 0199);
//   - the window between GVS::StartupGame (which ORs Terrain back at every load) and the first Update after it: a
//     watcher thread repeats the re-check every 10 ms (no EXE code is hooked: D4), and the first Update logs how long
//     after the watcher's clear it came, i.e. how long the bit would have stayed set without it.
//   - issue 4759: [Renderer] ServerUnits = 1 adds render_units=1, render_units_passes=1, render_units_positions=exe to
//     the render lines and asks the Lua bridge for its positions-only install (UnitLines; nothing without the render lines);
//     ticket 4783: with [Renderer] ServerCombat = 1 (default) also render_units_triggers=1 (route S's question).
//   - ticket 0199 part 2: [Renderer] ServerArc = 1 adds render_arc=1, asks the Lua bridge for its arrows install and
//     clears the Arrows view bit 0x400000 while the composite is loaded (ArcLines; nothing without the render lines).
//   - ticket 0300 part 2: [Renderer] ServerPath = 1 adds render_path=1, asks the Lua bridge for its path install and
//     clears the MovementPath view bit 0x800000 while the composite is loaded (PathLines; nothing without the render lines).
// Test switches: the 9040 markers (crashlogs\shell.helper, crashlogs\viewmask.enabled with keep= / clear=), their logs
// and the once-a-minute read-back. Toolchain: clang-cl -m32, VC9 headers. x86 only (pointers are DWORDs).
#include "proxy_shell.h"
#include "proxy_queue_guard.h"  // ticket 0492
#include "proxy_kbhook.h"       // ticket 0720
#include "vp64_options.h"
#include "proxy_exe.h"  // ticket 4963: the build table (the view mask's and the white mask's addresses and patterns)
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ProxyRtInt  // proxy_rt_internal.h (declared here so that the offline unit test needs no generated header)
{
void Log(const char* fmt, ...);
const Vp64Opt::Options& ProxyOptions();
}  // namespace ProxyRtInt

namespace
{
// ================================================================================================ small helpers
double NowMs()
{
	static LARGE_INTEGER s_freq;
	if (s_freq.QuadPart == 0) QueryPerformanceFrequency(&s_freq);
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return (double)t.QuadPart * 1000.0 / (double)s_freq.QuadPart;
}

// UTF-8 of a wide path for the log ("(path not printable)" when it cannot be converted).
void Narrow(const wchar_t* w, char* out, int cap)
{
	if (WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap, NULL, NULL) <= 0)
		_snprintf_s(out, cap, _TRUNCATE, "(path not printable)");
}

// The folder of the module that holds this code (the proxy), with a trailing backslash. False when it cannot be found.
bool OwnFolder(wchar_t* out, size_t cap, wchar_t* selfPath, size_t selfCap)
{
	HMODULE self = NULL;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                        reinterpret_cast<LPCWSTR>(&OwnFolder), &self))
		return false;
	const DWORD n = GetModuleFileNameW(self, selfPath, (DWORD)selfCap);
	if (n == 0 || n >= selfCap) return false;
	wcsncpy_s(out, cap, selfPath, _TRUNCATE);
	wchar_t* slash = wcsrchr(out, L'\\');
	if (!slash) return false;
	slash[1] = L'\0';
	return true;
}

// ================================================================================================ SHA-256 (bcrypt)
// SHA-256 of what is left of the open file h from its start. bcrypt.dll is loaded by name (the proxy's import list is
// checked by build_proxy.py), as in proxy/include/vp_buildid.h. *why on failure.
bool Sha256Handle(HANDLE h, unsigned char out[32], const char** why)
{
	typedef LONG(WINAPI * OpenAlgFn)(void**, const wchar_t*, const wchar_t*, ULONG);
	typedef LONG(WINAPI * CreateHashFn)(void*, void**, unsigned char*, ULONG, unsigned char*, ULONG, ULONG);
	typedef LONG(WINAPI * HashDataFn)(void*, unsigned char*, ULONG, ULONG);
	typedef LONG(WINAPI * FinishHashFn)(void*, unsigned char*, ULONG, ULONG);
	typedef LONG(WINAPI * DestroyHashFn)(void*);
	typedef LONG(WINAPI * CloseAlgFn)(void*, ULONG);
	*why = "bcrypt.dll is not available";
	HMODULE bc = LoadLibraryW(L"bcrypt.dll");
	if (!bc) return false;
	OpenAlgFn openAlg = (OpenAlgFn)GetProcAddress(bc, "BCryptOpenAlgorithmProvider");
	CreateHashFn createHash = (CreateHashFn)GetProcAddress(bc, "BCryptCreateHash");
	HashDataFn hashData = (HashDataFn)GetProcAddress(bc, "BCryptHashData");
	FinishHashFn finishHash = (FinishHashFn)GetProcAddress(bc, "BCryptFinishHash");
	DestroyHashFn destroyHash = (DestroyHashFn)GetProcAddress(bc, "BCryptDestroyHash");
	CloseAlgFn closeAlg = (CloseAlgFn)GetProcAddress(bc, "BCryptCloseAlgorithmProvider");
	void* alg = NULL;
	void* hash = NULL;
	bool ok = false;
	unsigned char* buf = NULL;
	LARGE_INTEGER zero;
	zero.QuadPart = 0;
	if (!openAlg || !createHash || !hashData || !finishHash || !destroyHash || !closeAlg) goto done;
	*why = "SHA-256 could not be started";
	if (openAlg(&alg, L"SHA256", NULL, 0) != 0 || createHash(alg, &hash, NULL, 0, NULL, 0, 0) != 0) goto done;
	*why = "the file could not be read";
	if (!SetFilePointerEx(h, zero, NULL, FILE_BEGIN)) goto done;
	buf = static_cast<unsigned char*>(malloc(1 << 16));
	if (!buf) goto done;
	for (;;)
	{
		DWORD got = 0;
		if (!ReadFile(h, buf, 1 << 16, &got, NULL)) goto done;
		if (got == 0) break;
		if (hashData(hash, buf, got, 0) != 0) goto done;
	}
	if (finishHash(hash, out, 32, 0) != 0) goto done;
	ok = true;
	*why = NULL;
done:
	free(buf);
	if (hash) destroyHash(hash);
	if (alg) closeAlg(alg, 0);
	FreeLibrary(bc);
	return ok;
}

void HexOf(const unsigned char sha[32], char out[65])
{
	for (int i = 0; i < 32; ++i) _snprintf_s(out + 2 * i, 65 - 2 * i, _TRUNCATE, "%02x", sha[i]);
	out[64] = 0;
}

int HexNibble(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

// ================================================================================================ the manifest
// shell_composite.sha256 (written by proxy/tools/shell_manifest.py when the patch is packaged):
//   # comment lines and blank lines are ignored
//   proxy <SHA-256 of the proxy DLL this composite was packaged with, 64 hex digits>
//   composite <SHA-256 of shell_composite.dll, 64 hex digits>
// Both lines are required, each once; any other line makes the manifest invalid. At most 4096 bytes.
const size_t kManifestMax = 4096;

bool ParseHex32(const char* s, size_t n, unsigned char out[32])
{
	if (n != 64) return false;
	for (size_t i = 0; i < 32; ++i)
	{
		const int hi = HexNibble(s[2 * i]), lo = HexNibble(s[2 * i + 1]);
		if (hi < 0 || lo < 0) return false;
		out[i] = (unsigned char)(hi * 16 + lo);
	}
	return true;
}

bool ParseManifestText(const char* text, size_t len, unsigned char proxySha[32], unsigned char compositeSha[32],
                       const char** why)
{
	if (!text) len = 0;
	if (len > kManifestMax)
	{
		*why = "larger than 4096 bytes";
		return false;
	}
	bool haveProxy = false, haveComposite = false;
	size_t at = 0;
	while (at < len)
	{
		size_t end = at;
		while (end < len && text[end] != '\n' && text[end] != '\r') ++end;
		size_t s = at, e = end;
		while (s < e && (text[s] == ' ' || text[s] == '\t')) ++s;
		while (e > s && (text[e - 1] == ' ' || text[e - 1] == '\t')) --e;
		at = end + 1;
		if (s == e || text[s] == '#') continue;
		size_t k = s;
		while (k < e && text[k] != ' ' && text[k] != '\t') ++k;
		size_t v = k;
		while (v < e && (text[v] == ' ' || text[v] == '\t')) ++v;
		const size_t klen = k - s;
		bool* have = NULL;
		unsigned char* dst = NULL;
		if (klen == 5 && memcmp(text + s, "proxy", 5) == 0)
		{
			have = &haveProxy;
			dst = proxySha;
		}
		else if (klen == 9 && memcmp(text + s, "composite", 9) == 0)
		{
			have = &haveComposite;
			dst = compositeSha;
		}
		else
		{
			*why = "a line is neither a comment, \"proxy <sha256>\" nor \"composite <sha256>\"";
			return false;
		}
		if (*have)
		{
			*why = "a hash is named twice";
			return false;
		}
		if (!ParseHex32(text + v, e - v, dst))
		{
			*why = "a hash is not 64 hex digits";
			return false;
		}
		*have = true;
	}
	if (!haveProxy || !haveComposite)
	{
		*why = !haveProxy ? "no \"proxy <sha256>\" line" : "no \"composite <sha256>\" line";
		return false;
	}
	*why = NULL;
	return true;
}

// ================================================================================================ the configuration
struct Config
{
	bool configured;           // Configure ran for this boot
	int option;                // [Renderer] ServerRenderer
	bool helperMarker;         // crashlogs\shell.helper exists (test switch)
	bool productComposite;     // the product composite passed the checks and is held open for the load
	bool compositeLoaded;      // OnBoot loaded a composite (product or test switch)
	HANDLE held;               // the checked composite, opened without write sharing until LoadLibrary
	wchar_t compositePath[MAX_PATH * 2];
	char base[64];             // Local\VP64Shell_<pid>, from the second boot on Local\VP64Shell_<pid>_<boot> (issue 3308)
	char texture[64];          // "" on the first boot (the server's default), else Local\VP64ShellWorld_<pid>_<boot>
	unsigned long boot;        // this boot's number in the EXE process (VP64SHELL_BOOT_ENV), 0 before names were given
	bool arc;                  // ticket 0199 part 2: Configure wrote render_arc=1 ([Renderer] ServerArc = 1)
	bool path;                 // ticket 0300 part 2: Configure wrote render_path=1 ([Renderer] ServerPath = 1)
	bool farm;                 // ticket 4853: Configure wrote render_farmfields=1 ([Renderer] ServerFarmFields = 1)
	bool cities;               // ticket 4875: Configure wrote render_cities=1 ([Renderer] ServerCities = 1)
	bool landmarks;            // ticket 4875: Configure wrote render_landmarks=1 ([Renderer] ServerLandmarks = 1)
};
Config g_cfg = {false, 0, false, false, false, INVALID_HANDLE_VALUE, {0}, {0}, {0}, 0, false, false, false, false, false};
char g_refused[400] = "";  // ticket 0624: why ConfigureRender refused the product composite ("" = not refused)

// Issue 3308: the boot number of this EXE process. The proxy can be unloaded and loaded again in one EXE process (a
// second boot: its globals start again at zero), while the composite it loaded stays and the first boot's server may
// still hold its names. So the count lives in the EXE process's environment, which outlives the proxy's image: read,
// add one, write back. A value that is not a decimal number from 1 to 1000000 counts as 0 (logged).
unsigned long NextBoot()
{
	char v[32] = "";
	const DWORD n = GetEnvironmentVariableA(VP64SHELL_BOOT_ENV, v, sizeof(v));
	unsigned long prev = 0;
	if (n > 0 && n < sizeof(v))
	{
		char* end = NULL;
		const unsigned long x = strtoul(v, &end, 10);
		if (end && *end == 0 && end != v && x >= 1 && x <= 1000000) prev = x;
		else ProxyRtInt::Log("shell: " VP64SHELL_BOOT_ENV "=%s in the EXE's environment is not a boot number: counted as 0", v);
	}
	else if (n >= sizeof(v))
		ProxyRtInt::Log("shell: " VP64SHELL_BOOT_ENV " in the EXE's environment is too long: counted as 0");
	const unsigned long boot = prev >= 1000000 ? 1 : prev + 1;
	char out[16];
	_snprintf_s(out, sizeof(out), _TRUNCATE, "%lu", boot);
	if (!SetEnvironmentVariableA(VP64SHELL_BOOT_ENV, out))
		ProxyRtInt::Log("shell: " VP64SHELL_BOOT_ENV " could not be set (error %lu): the next boot may repeat these names",
		                GetLastError());
	return boot;
}

// The names of a boot: the first keeps 3305's contract (Local\VP64Shell_<pid>, the server's default texture base); a
// later boot gets its own section base and texture base, so that it never meets the earlier boot's server or textures.
void MakeNames(unsigned long pid, unsigned long boot, char* base, size_t baseCap, char* texture, size_t textureCap)
{
	if (boot <= 1)
	{
		_snprintf_s(base, baseCap, _TRUNCATE, VP64SHELL_BASE_PREFIX "%lu", pid);
		texture[0] = 0;
	}
	else
	{
		_snprintf_s(base, baseCap, _TRUNCATE, VP64SHELL_BASE_PREFIX "%lu_%lu", pid, boot);
		_snprintf_s(texture, textureCap, _TRUNCATE, VP64SHELL_TEXTURE_PREFIX "%lu_%lu", pid, boot);
	}
}

// Issue 0148: the folder for every file the product composite writes (its log, per-frame records, shots), outside the
// EXE's MODS tree. The EXE watches <user folder>\MODS with FindFirstChangeNotificationW (subtree, file name / folder name /
// last write); every change under it costs the EXE's main thread a stall of ~150 ms on a huge VP game, and the composite
// beside the proxy appended its records every 0.5 s (research/05-graphics-composition/renderer-in-server-design.md,
// section 4.14). For a DLL path <user>\MODS\<mod>\...\file the folder is <user>\Logs (the game's own log folder, not
// watched); false (nothing set: the composite writes beside itself, as before) when no folder of the path is named MODS.
// Ticket 0623: the modpack form is checked first. VP inside <game>\Assets\DLC\<pack>\Mods\(1) Community Patch\ has a Mods
// folder of its own, which the MODS rule took for the user folder's: the composite's log went to
// <game>\Assets\DLC\<pack>\Logs, inside the player's game install, where the installer's removal does not look
// (research/02-exe-internals/new-exe-build-26577625.md C.7.4 A). A path with an Assets\DLC folder pair (any case) gives
// <the folder of the game's config.ini>\Logs (ProxySession::ConfigIniPath), the user folder whose Logs\VP64 the proxy's
// run folders and error report already use in that form (ProxyLogic::UserLogsDir, issue 2606); false when that path is
// unknown or does not fit.
}  // namespace
namespace ProxySession
{
bool ConfigIniPath(std::wstring* out);  // proxy_session.h
}  // namespace ProxySession
namespace
{
bool IsFolderAt(const wchar_t* p, size_t n, size_t i, const wchar_t* name)   // a whole path component, any case
{
	const size_t k = wcslen(name);
	if (i + k > n || _wcsnicmp(p + i, name, k) != 0) return false;
	return (i == 0 || p[i - 1] == L'\\' || p[i - 1] == L'/') && (i + k == n || p[i + k] == L'\\' || p[i + k] == L'/');
}

bool OutDirFor(const wchar_t* dllPath, wchar_t* out, size_t cap)
{
	out[0] = 0;
	const size_t n = wcslen(dllPath);
	for (size_t i = 0; i < n; ++i)
	{
		// Assets\DLC\ as folders: something follows DLC (a file named DLC does not count)
		if (i + 10 >= n || !IsFolderAt(dllPath, n, i, L"Assets") || !IsFolderAt(dllPath, n, i + 7, L"DLC")) continue;
		std::wstring ini;
		if (!ProxySession::ConfigIniPath(&ini)) return false;
		const size_t cut = ini.find_last_of(L"\\/");
		if (cut == std::wstring::npos || cut == 0 || cut + 6 > cap) return false;   // the folder, "\Logs" and the terminator
		wcsncpy_s(out, cap, ini.c_str(), cut);
		wcscat_s(out, cap, L"\\Logs");
		for (wchar_t* c = out; *c; ++c)
			if (*c == L'/') *c = L'\\';
		return true;
	}
	// the last "\MODS\" in the path, any case
	for (size_t i = n; i-- > 0;)
	{
		if (dllPath[i] != L'\\' || i + 6 > n) continue;
		if (_wcsnicmp(dllPath + i, L"\\MODS\\", 6) != 0) continue;
		if (i == 0 || i + 6 > cap) return false;   // the user folder, "\Logs" and the terminator must fit
		wcsncpy_s(out, cap, dllPath, i);
		wcscat_s(out, cap, L"\\Logs");
		return true;
	}
	return false;
}

void ReleaseHeld()
{
	if (g_cfg.held != INVALID_HANDLE_VALUE) CloseHandle(g_cfg.held);
	g_cfg.held = INVALID_HANDLE_VALUE;
}

// The product composite's checks. NULL when it may be loaded (g_cfg.held is then open), else the reason.
const char* CheckComposite(char* detail, size_t detailCap)
{
	detail[0] = 0;
	wchar_t folder[MAX_PATH * 2], self[MAX_PATH * 2];
	if (!OwnFolder(folder, MAX_PATH * 2, self, MAX_PATH * 2)) return "the proxy's own folder could not be found";
	_snwprintf_s(g_cfg.compositePath, MAX_PATH * 2, _TRUNCATE, L"%s%s", folder, VP64SHELL_COMPOSITE_FILE);
	wchar_t manifestPath[MAX_PATH * 2];
	_snwprintf_s(manifestPath, MAX_PATH * 2, _TRUNCATE, L"%s%s", folder, VP64SHELL_MANIFEST_FILE);

	if (GetFileAttributesW(g_cfg.compositePath) == INVALID_FILE_ATTRIBUTES) return "shell_composite.dll is missing";
	HANDLE mf = CreateFileW(manifestPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (mf == INVALID_HANDLE_VALUE)
	{
		const DWORD e = GetLastError();
		if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return "shell_composite.sha256 is missing";
		_snprintf_s(detail, detailCap, _TRUNCATE, "error %lu", e);
		return "shell_composite.sha256 cannot be opened";
	}
	char text[kManifestMax + 1];
	DWORD got = 0;
	const BOOL rd = ReadFile(mf, text, (DWORD)kManifestMax + 1, &got, NULL);
	CloseHandle(mf);
	if (!rd) return "shell_composite.sha256 cannot be read";
	unsigned char wantProxy[32], wantComposite[32];
	const char* why = NULL;
	if (!ParseManifestText(text, got, wantProxy, wantComposite, &why))
	{
		_snprintf_s(detail, detailCap, _TRUNCATE, "%s", why);
		return "shell_composite.sha256 is not valid";
	}

	// The proxy's own bytes: the manifest belongs to one build of the proxy.
	HANDLE pf = CreateFileW(self, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
	                        FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (pf == INVALID_HANDLE_VALUE) return "the proxy's own file cannot be opened";
	unsigned char haveProxy[32];
	const bool hashedProxy = Sha256Handle(pf, haveProxy, &why);
	CloseHandle(pf);
	if (!hashedProxy)
	{
		_snprintf_s(detail, detailCap, _TRUNCATE, "%s", why);
		return "the proxy's own SHA-256 could not be computed";
	}
	char hexHave[65], hexWant[65];
	if (memcmp(haveProxy, wantProxy, 32) != 0)
	{
		HexOf(haveProxy, hexHave);
		HexOf(wantProxy, hexWant);
		_snprintf_s(detail, detailCap, _TRUNCATE, "proxy %s, manifest %s", hexHave, hexWant);
		return "shell_composite.sha256 belongs to another build of the proxy";
	}

	// The composite: opened without write or delete sharing (nobody can change or replace it until the load), hashed
	// through this handle.
	g_cfg.held = CreateFileW(g_cfg.compositePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
	                         FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (g_cfg.held == INVALID_HANDLE_VALUE)
	{
		_snprintf_s(detail, detailCap, _TRUNCATE, "error %lu", GetLastError());
		return "shell_composite.dll cannot be opened without write sharing";
	}
	unsigned char haveComposite[32];
	if (!Sha256Handle(g_cfg.held, haveComposite, &why))
	{
		ReleaseHeld();
		_snprintf_s(detail, detailCap, _TRUNCATE, "%s", why);
		return "the SHA-256 of shell_composite.dll could not be computed";
	}
	if (memcmp(haveComposite, wantComposite, 32) != 0)
	{
		ReleaseHeld();
		HexOf(haveComposite, hexHave);
		HexOf(wantComposite, hexWant);
		_snprintf_s(detail, detailCap, _TRUNCATE, "file %s, manifest %s", hexHave, hexWant);
		return "the SHA-256 of shell_composite.dll differs from the manifest";
	}
	HexOf(haveComposite, hexHave);
	_snprintf_s(detail, detailCap, _TRUNCATE, "%s", hexHave);
	return NULL;
}

// ================================================================================================ the test-switch loader
namespace Loader
{
const char MARKER_PATH[] = "crashlogs\\shell.helper";
const char LOG_PATH[] = "crashlogs\\shell.log";

void AppendToLog(const char* szMessage)
{
	SYSTEMTIME kNow;
	GetLocalTime(&kNow);
	char szLine[1400];
	_snprintf_s(szLine, sizeof(szLine), _TRUNCATE, "%04d-%02d-%02d %02d:%02d:%02d  pid %lu  %s\r\n", kNow.wYear,
	            kNow.wMonth, kNow.wDay, kNow.wHour, kNow.wMinute, kNow.wSecond, GetCurrentProcessId(), szMessage);
	HANDLE hLog = CreateFileA(LOG_PATH, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hLog == INVALID_HANDLE_VALUE) return;
	DWORD dwWritten = 0;
	WriteFile(hLog, szLine, static_cast<DWORD>(strlen(szLine)), &dwWritten, NULL);
	CloseHandle(hLog);
}

bool IsBlank(wchar_t c)
{
	return c == L' ' || c == L'\t' || c == L'"' || c == L'\'';
}

// The marker's first line as a wide string, trimmed. False if the file cannot be read or is empty.
bool ReadHelperPath(wchar_t* wszOut, int iChars)
{
	HANDLE hFile = CreateFileA(MARKER_PATH, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
	                           FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) return false;
	char acBytes[2048 + 2];
	DWORD dwRead = 0;
	const BOOL bRead = ReadFile(hFile, acBytes, 2048, &dwRead, NULL);
	CloseHandle(hFile);
	if (!bRead) return false;
	acBytes[dwRead] = '\0';
	acBytes[dwRead + 1] = '\0';

	int iLen = 0;
	if (dwRead >= 2 && static_cast<unsigned char>(acBytes[0]) == 0xFF && static_cast<unsigned char>(acBytes[1]) == 0xFE)
	{
		const wchar_t* wsz = reinterpret_cast<const wchar_t*>(acBytes + 2);
		const int iAvailable = static_cast<int>((dwRead - 2) / 2);
		for (; iLen < iAvailable && iLen < iChars - 1; iLen++) wszOut[iLen] = wsz[iLen];
	}
	else
	{
		const char* sz = acBytes;
		if (dwRead >= 3 && static_cast<unsigned char>(sz[0]) == 0xEF && static_cast<unsigned char>(sz[1]) == 0xBB &&
		    static_cast<unsigned char>(sz[2]) == 0xBF)
			sz += 3;
		iLen = MultiByteToWideChar(CP_UTF8, 0, sz, -1, wszOut, iChars);
		if (iLen <= 0) return false;
		iLen--;  // the terminator was counted
	}
	wszOut[iLen] = L'\0';

	// First line only, without blanks or quotes around it.
	for (int i = 0; i < iLen; i++)
	{
		if (wszOut[i] == L'\r' || wszOut[i] == L'\n')
		{
			wszOut[i] = L'\0';
			iLen = i;
			break;
		}
	}
	while (iLen > 0 && IsBlank(wszOut[iLen - 1])) wszOut[--iLen] = L'\0';
	int iStart = 0;
	while (iStart < iLen && IsBlank(wszOut[iStart])) iStart++;
	if (iStart > 0) memmove(wszOut, wszOut + iStart, (iLen - iStart + 1) * sizeof(wchar_t));
	return wszOut[0] != L'\0';
}

// The test switch: loads the DLL the marker names, without a hash check. True when it was loaded.
bool LoadFromMarker()
{
	wchar_t wszPath[MAX_PATH * 2];
	if (!ReadHelperPath(wszPath, sizeof(wszPath) / sizeof(wszPath[0])))
	{
		AppendToLog("shell.helper present but holds no path; nothing loaded");
		ProxyRtInt::Log("shell: test switch crashlogs\\shell.helper present but holds no path; nothing loaded");
		return false;
	}
	char szPath[MAX_PATH * 4];
	Narrow(wszPath, szPath, sizeof(szPath));
	char szMessage[MAX_PATH * 4 + 128];
	HMODULE hHelper = LoadLibraryW(wszPath);
	const DWORD dwError = GetLastError();
	if (hHelper != NULL)
		_snprintf_s(szMessage, sizeof(szMessage), _TRUNCATE, "loaded %s at 0x%08lx", szPath,
		            reinterpret_cast<unsigned long>(hHelper));
	else
		_snprintf_s(szMessage, sizeof(szMessage), _TRUNCATE, "FAILED to load %s: GetLastError %lu", szPath, dwError);
	AppendToLog(szMessage);
	ProxyRtInt::Log("shell: helper (test switch crashlogs\\shell.helper, no hash check): %s", szMessage);
	return hHelper != NULL;
}
}  // namespace Loader

// ================================================================================================ the view-mask write
namespace Mask
{
const char MARKER_PATH[] = "crashlogs\\viewmask.enabled";
const char LOG_PATH[] = "crashlogs\\viewmask.log";

// Ticket 4963 (epic 0590): the build from the image's PE stamp and SizeOfImage; g_Civ5App (cam_app), the GameViewState
// vtable (ses_gvs_vtbl) and the patterns' places from its build table (exebuild/symbols). Set by VerifyExe.
int g_iBuild = kVpExeBuildUnknown;
DWORD RVA_APP_GLOBAL = 0;   // g_Civ5App of the verified build
DWORD RVA_GVS_VTABLE = 0;   // the GameViewState vtable of the verified build
const DWORD APP_OFF_GVS = 0xC0;
const DWORD APP_OFF_CFG = 0x940;
const DWORD GVS_OFF_VIEW = 0x1B4;
const DWORD GVS_SIZE = 0x8D0;
const DWORD KNOWN_BITS = 0x00FFFFFF;      // the 24 [DisableSystems] keys, Terrain (bit 0) .. MovementPath (bit 23)
const DWORD TERRAIN_BIT = 0x00000001;     // D7 option c1: the system runs its per-load bake, is neither updated nor drawn
const DWORD NEVER_RESTORE = 0x00400100;   // River, Arrows: set while their system is off = crash at VA 0x8CD492
const DWORD ARROWS_BIT = 0x00400000;      // ticket 0199 part 2: cleared while the server draws the arc ([Renderer] ServerArc;
                                          // config.ini keeps Arrows on, so the system is started and never crashes on it)
const DWORD MOVEMENTPATH_BIT = 0x00800000;   // ticket 0300 part 2: cleared while the server draws the path's models
                                             // ([Renderer] ServerPath; config.ini keeps MovementPath on, as Arrows)
const DWORD MARKER_EVERY_MS = 2000;
const DWORD READBACK_EVERY_MS = 60000;    // test switch: one read-back line a minute (origin: one per game turn)
const DWORD WATCH_EVERY_MS = 10;          // the watcher's period (Sleep; the system timer may round it up to ~15.6 ms)
const int MAX_LOG_LINES = 600;

// A code pattern: one X row (symbol, name, bytes) of a table below. The bytes in vp_exe_patterns.h's syntax: "??" any
// byte, R(name) the 4 bytes of a symbol's address in the running build (rebased), E8/E9 + 4 literal bytes: the rel32 is
// free (it moves with every link). exebuild/patterns.txt lists this file: tools/exebuild/gen_table.py checks every row
// on every build it generates (ticket 4963).
struct CodePattern
{
	int iSym;
	const char* szName;
	const char* szBytes;
};
#define VP_SHELL_ROW(sym, name, bytes) {VPX_##sym, name, bytes},

// GVS::StartupGame, Terrain: or [esi+1B4h],1; mov ecx,[g_Civ5App]; test byte [ecx+940h],1. Terrain render: mov
// edx,[g_Civ5App]; mov eax,[edx+0C0h]; test [eax+1B4h],100h (River). App Startup: test eax,100h (River) / test
// eax,400000h (Arrows), jz +7Ah, mov ecx,<system>. Render pass B: test [esi+1B4h],400000h (Arrows). GameViewState ctor:
// mov [esi+1B4h],ebp (ebp = -1).
#define VP_SHELL_MASK_PATTERNS(X) \
	X(pxm_terrain_gate, "terrain gate", "83 8E B4 01 00 00 01 8B 0D R(cam_app) F6 81 40 09 00 00 01") \
	X(pxm_river_test, "river test", "8B 15 R(cam_app) 8B 82 C0 00 00 00 F7 80 B4 01 00 00 00 01 00 00") \
	X(pxm_river_gate, "river gate", "A9 00 01 00 00 74 7A B9") \
	X(pxm_arrows_gate, "arrows gate", "A9 00 00 40 00 74 7A B9") \
	X(pxm_arrows_test, "arrows test", "F7 86 B4 01 00 00 00 00 40 00 74 0B") \
	X(pxm_viewmask_init, "view mask init", "89 86 AC 01 00 00 89 86 B0 01 00 00 89 AE B4 01 00 00")
const CodePattern CODE_PATTERNS[] = {VP_SHELL_MASK_PATTERNS(VP_SHELL_ROW)};

// State. Apply runs on the game-core thread (Update) and on the watcher; g_lock serialises them.
CRITICAL_SECTION g_lock;
volatile LONG g_lockReady = 0;
bool g_bActive = false;
bool g_bTestSwitch = false;  // crashlogs\viewmask.enabled: re-read, crashlogs\viewmask.log, read-back lines
bool g_bTerrain = false;     // the product rule: clear the Terrain bit (the composite is loaded)
bool g_bProduct = false;     // ServerRenderer = 1: the config rule (and Terrain) stay in force without the test switch
bool g_bArrows = false;      // ticket 0199 part 2: clear the Arrows bit (the server draws the arc; set before InstallAt)
bool g_bPath = false;        // ticket 0300 part 2: clear the MovementPath bit (the server draws the path; before InstallAt)
DWORD g_dwBase = 0;
DWORD g_dwLoggedBadApp = 0;
DWORD g_dwKeep = 0;
DWORD g_dwClear = 0;    // "clear=" bits of the test switch
DWORD g_dwCleared = 0;  // bits this write cleared in the current GameViewState
DWORD g_dwLastGvs = 0;
DWORD g_dwLastApp = 0;
bool g_bLastGvsValid = false;
DWORD g_dwLastMarkerTick = 0;
DWORD g_dwLastReadbackTick = 0;
DWORD g_dwUpdates = 0;
DWORD g_dwWrites = 0;
int g_iLogLines = 0;
bool g_bLoggedWaitApp = false;
bool g_bLoggedWaitGvs = false;
double g_watchClearedAtMs = 0;  // when the watcher last cleared the Terrain bit (0 = not since the last Update)
// the watcher thread
HANDLE g_hWatch = NULL;
volatile LONG g_watchStop = 0;
volatile LONG g_watchBusy = 0;
volatile LONG g_watchPolls = 0;
DWORD g_watchFaults = 0;  // polls ended by an access violation (the EXE freed what the poll read)
double g_watchLastPollMs = 0;

void EnsureLock()
{
	if (InterlockedCompareExchange(&g_lockReady, 1, 0) == 0) InitializeCriticalSection(&g_lock);
}

void AppendToLog(const char* szMessage)
{
	if (!g_bTestSwitch || g_iLogLines >= MAX_LOG_LINES) return;
	g_iLogLines++;
	SYSTEMTIME kNow;
	GetLocalTime(&kNow);
	char szLine[1024];
	_snprintf_s(szLine, sizeof(szLine), _TRUNCATE, "%04d-%02d-%02d %02d:%02d:%02d.%03d  pid %lu  %s%s\r\n", kNow.wYear,
	            kNow.wMonth, kNow.wDay, kNow.wHour, kNow.wMinute, kNow.wSecond, kNow.wMilliseconds,
	            GetCurrentProcessId(), szMessage, g_iLogLines == MAX_LOG_LINES ? "  (log line limit reached)" : "");
	HANDLE hLog = CreateFileA(LOG_PATH, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hLog == INVALID_HANDLE_VALUE) return;
	DWORD dwWritten = 0;
	WriteFile(hLog, szLine, static_cast<DWORD>(strlen(szLine)), &dwWritten, NULL);
	CloseHandle(hLog);
}

// One line into vpproxy.log ("shell: view mask: ..."), and into crashlogs\viewmask.log under the test switch.
void Logf(const char* szFormat, ...)
{
	char szMessage[900];
	va_list args;
	va_start(args, szFormat);
	_vsnprintf_s(szMessage, sizeof(szMessage), _TRUNCATE, szFormat, args);
	va_end(args);
	AppendToLog(szMessage);
	ProxyRtInt::Log("shell: view mask: %s", szMessage);
}

// True if [dwAddr, dwAddr + dwBytes) lies in committed memory that is readable (and writable if asked).
bool IsAccessible(DWORD dwAddr, DWORD dwBytes, bool bWrite)
{
	if (dwAddr < 0x10000 || dwAddr + dwBytes < dwAddr) return false;
	DWORD dwAt = dwAddr;
	const DWORD dwEnd = dwAddr + dwBytes;
	while (dwAt < dwEnd)
	{
		MEMORY_BASIC_INFORMATION kInfo;
		if (VirtualQuery(reinterpret_cast<LPCVOID>(dwAt), &kInfo, sizeof(kInfo)) != sizeof(kInfo)) return false;
		if (kInfo.State != MEM_COMMIT || (kInfo.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
		const DWORD dwReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
		                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
		const DWORD dwWritable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
		if ((kInfo.Protect & (bWrite ? dwWritable : dwReadable)) == 0) return false;
		const DWORD dwRegionEnd = reinterpret_cast<DWORD>(kInfo.BaseAddress) + static_cast<DWORD>(kInfo.RegionSize);
		if (dwRegionEnd <= dwAt) return false;
		dwAt = dwRegionEnd;
	}
	return true;
}

// The name of the R(...) operand whose 4 bytes hold byte iOff of the template, or NULL when iOff is a plain byte.
const char* OperandAt(const char* szTemplate, int iOff, char* szName, size_t cap)
{
	int iAt = 0;
	for (const char* p = szTemplate; *p;)
	{
		if (*p == ' ' || *p == '\t')
		{
			++p;
			continue;
		}
		if (p[0] == 'R' && p[1] == '(')
		{
			const char* e = strchr(p, ')');
			if (!e) return NULL;
			if (iOff >= iAt && iOff < iAt + 4)
			{
				_snprintf_s(szName, cap, _TRUNCATE, "%.*s", static_cast<int>(e - p - 2), p + 2);
				return szName;
			}
			iAt += 4;
			p = e + 1;
			continue;
		}
		++iAt;
		p += 2;
	}
	return NULL;
}

// NULL when every row matches in the image at dwBase for the build, else the reason (in szWhy): "<prefix>EXE code
// differs from the known layout (name at VA 0x..., +N)", an operand that does not name the build's address, or a
// symbol the build does not carry.
const char* CheckRows(int iBuild, DWORD dwBase, const CodePattern* pRows, size_t n, const char* szPrefix, char* szWhy,
                      size_t cap)
{
	for (size_t i = 0; i < n; i++)
	{
		const CodePattern& k = pRows[i];
		if (!ProxyExe::Va(iBuild, k.iSym))
			return ProxyExe::NotCarried(szWhy, cap, szPrefix, iBuild, k.iSym);
		VpExePatternResult r;
		if (ProxyExe::CheckPattern(iBuild, dwBase, k.iSym, k.szBytes, &r)) continue;
		char szOperand[64];
		const unsigned long ulVa = ProxyExe::Va(iBuild, k.iSym);
		if (r.offset >= 0 && OperandAt(k.szBytes, r.offset, szOperand, sizeof(szOperand)))
		{
			if (strcmp(szOperand, "cam_app") == 0)
				_snprintf_s(szWhy, cap, _TRUNCATE, "%sg_Civ5App is not where the mask code reads it (%s at VA 0x%lx)",
				            szPrefix, k.szName, ulVa);
			else
				_snprintf_s(szWhy, cap, _TRUNCATE,
				            "%s%s (VA 0x%lx): its operand R(%s) at +%d does not name the build's address", szPrefix,
				            k.szName, ulVa, szOperand, r.offset);
		}
		else
			_snprintf_s(szWhy, cap, _TRUNCATE, "%sEXE code differs from the known layout (%s at VA 0x%lx: %s at +%d)",
			            szPrefix, k.szName, ulVa, r.reason, r.offset);
		return szWhy;
	}
	return NULL;
}

// NULL if the image at dwBase is a build the patch knows whose table carries the mask's symbols and whose code matches
// the patterns, else the reason not to touch it. Sets the layout of that build (ticket 4963).
const char* VerifyExe(DWORD dwBase)
{
	static char s_szReason[220];
	if (!IsAccessible(dwBase, sizeof(IMAGE_DOS_HEADER), false)) return "off: no EXE image";
	const IMAGE_DOS_HEADER* pDos = reinterpret_cast<const IMAGE_DOS_HEADER*>(dwBase);
	if (pDos->e_magic != IMAGE_DOS_SIGNATURE || pDos->e_lfanew <= 0 || pDos->e_lfanew > 0x10000000 ||
	    !IsAccessible(dwBase + pDos->e_lfanew, sizeof(IMAGE_NT_HEADERS), false))
		return "off: no EXE image";
	const IMAGE_NT_HEADERS* pNt = reinterpret_cast<const IMAGE_NT_HEADERS*>(dwBase + pDos->e_lfanew);
	if (pNt->Signature != IMAGE_NT_SIGNATURE) return "off: no EXE image";
	const int iBuild = VpExeBuildFromHeader(pNt->FileHeader.TimeDateStamp, pNt->OptionalHeader.SizeOfImage);
	if (iBuild == kVpExeBuildUnknown)
		return ProxyExe::UnknownBuild(s_szReason, sizeof(s_szReason), "off: ", pNt->FileHeader.TimeDateStamp,
		                              pNt->OptionalHeader.SizeOfImage);
	if (!ProxyExe::Va(iBuild, VPX_cam_app))
		return ProxyExe::NotCarried(s_szReason, sizeof(s_szReason), "off: ", iBuild, VPX_cam_app);
	if (!ProxyExe::Va(iBuild, VPX_ses_gvs_vtbl))
		return ProxyExe::NotCarried(s_szReason, sizeof(s_szReason), "off: ", iBuild, VPX_ses_gvs_vtbl);
	const char* szRows = CheckRows(iBuild, dwBase, CODE_PATTERNS, sizeof(CODE_PATTERNS) / sizeof(CODE_PATTERNS[0]),
	                               "off: ", s_szReason, sizeof(s_szReason));
	if (szRows != NULL) return szRows;
	g_iBuild = iBuild;
	RVA_APP_GLOBAL = ProxyExe::Rva(iBuild, VPX_cam_app);
	RVA_GVS_VTABLE = ProxyExe::Rva(iBuild, VPX_ses_gvs_vtbl);
	if (!IsAccessible(dwBase + RVA_APP_GLOBAL, 4, false) || !IsAccessible(dwBase + RVA_GVS_VTABLE, 4, false))
		return "off: g_Civ5App or the GameViewState vtable is not readable";
	return NULL;
}

// Reads crashlogs\viewmask.enabled. False if it is gone; *pdwKeep = the "keep=" bits, *pdwClear = the "clear=" bits
// (0 if none). A marker that cannot be read (held open by an editor) keeps the values the caller passed in.
bool ReadMarker(DWORD* pdwKeep, DWORD* pdwClear)
{
	HANDLE hFile = CreateFileA(MARKER_PATH, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND;  // held: keep going
	char acBytes[257];
	DWORD dwRead = 0;
	const BOOL bRead = ReadFile(hFile, acBytes, 256, &dwRead, NULL);
	CloseHandle(hFile);
	if (!bRead) return true;
	*pdwKeep = 0;
	*pdwClear = 0;
	acBytes[dwRead] = '\0';
	const char* szKeep = strstr(acBytes, "keep=");
	if (szKeep != NULL) *pdwKeep = strtoul(szKeep + 5, NULL, 0) & KNOWN_BITS;
	const char* szClear = strstr(acBytes, "clear=");
	if (szClear != NULL) *pdwClear = strtoul(szClear + 6, NULL, 0) & KNOWN_BITS;
	return true;
}

// Sets bits back with a compare-and-swap; returns the value written.
DWORD OrBits(volatile LONG* pView, DWORD dwBits)
{
	LONG lOld = *pView;
	for (;;)
	{
		const LONG lNew = static_cast<LONG>(static_cast<DWORD>(lOld) | dwBits);
		const LONG lSeen = InterlockedCompareExchange(pView, lNew, lOld);
		if (lSeen == lOld) return static_cast<DWORD>(lNew);
		lOld = lSeen;
	}
}

// The GameViewState's mask, or NULL (logged once per kind of reason) when a pointer on the way fails a check.
volatile LONG* FindViewMask(DWORD* pdwConfig, DWORD* pdwGvs)
{
	const DWORD dwApp = *reinterpret_cast<const DWORD*>(g_dwBase + RVA_APP_GLOBAL);
	if (dwApp == 0)
	{
		if (!g_bLoggedWaitApp) Logf("waiting: g_Civ5App is NULL");
		g_bLoggedWaitApp = true;
		return NULL;
	}
	if (dwApp != g_dwLastApp)
	{
		if (!IsAccessible(dwApp, APP_OFF_CFG + 4, false))
		{
			if (dwApp != g_dwLoggedBadApp) Logf("skip: g_Civ5App 0x%08lx is not readable memory", dwApp);
			g_dwLoggedBadApp = dwApp;
			return NULL;
		}
		g_dwLastApp = dwApp;
	}
	const DWORD dwGvs = *reinterpret_cast<const DWORD*>(dwApp + APP_OFF_GVS);
	if (dwGvs == 0)
	{
		if (!g_bLoggedWaitGvs) Logf("waiting: app 0x%08lx has no GameViewState yet", dwApp);
		g_bLoggedWaitGvs = true;
		return NULL;
	}
	if (dwGvs != g_dwLastGvs || !g_bLastGvsValid)
	{
		const bool bFirst = g_dwLastGvs == 0;
		const bool bSame = dwGvs == g_dwLastGvs;
		const DWORD dwPrevious = g_dwLastGvs;
		g_dwLastGvs = dwGvs;
		g_bLastGvsValid = false;
		g_dwCleared = 0;
		if (!IsAccessible(dwGvs, GVS_SIZE, true))
		{
			if (!bSame) Logf("skip: GameViewState 0x%08lx is not writable memory", dwGvs);
			return NULL;
		}
		const DWORD dwVtable = *reinterpret_cast<const DWORD*>(dwGvs);
		if (dwVtable != g_dwBase + RVA_GVS_VTABLE)
		{
			if (!bSame)
				Logf("skip: object at 0x%08lx has vtable 0x%08lx, not the GameViewState's 0x%08lx", dwGvs, dwVtable,
				     g_dwBase + RVA_GVS_VTABLE);
			return NULL;
		}
		g_bLastGvsValid = true;
		if (bFirst)
			Logf("GameViewState at 0x%08lx (app 0x%08lx)", dwGvs, dwApp);
		else if (bSame)
			Logf("GameViewState at 0x%08lx passes the checks now (app 0x%08lx)", dwGvs, dwApp);
		else
			Logf("GameViewState RECREATED at 0x%08lx (was 0x%08lx; app 0x%08lx)", dwGvs, dwPrevious, dwApp);
	}
	*pdwConfig = *reinterpret_cast<const DWORD*>(dwApp + APP_OFF_CFG);
	*pdwGvs = dwGvs;
	return reinterpret_cast<volatile LONG*>(dwGvs + GVS_OFF_VIEW);
}

DWORD ClearBits()
{
	// 3606: + Waves (0x200000), D33 decision 2 (i); ticket 0199 part 2: + Arrows (0x400000) while the server draws the arc
	// ticket 0300 part 2: + MovementPath (0x800000) while the server draws the path's models
	return (g_bTerrain ? (TERRAIN_BIT | 0x00200000) : 0) | (g_bArrows ? ARROWS_BIT : 0) | (g_bPath ? MOVEMENTPATH_BIT : 0) |
	       g_dwClear;
}

// szWhy "boot" / "re-check" (the Update hook) / "watch"; bReadback: log the mask even when nothing is cleared (test switch, once a minute).
// Returns the bits cleared now (0 = nothing written). Caller holds g_lock.
DWORD Apply(const char* szWhy, bool bReadback, double dPollGapMs)
{
	DWORD dwConfig = 0;
	DWORD dwGvs = 0;
	volatile LONG* pView = FindViewMask(&dwConfig, &dwGvs);
	if (pView == NULL) return 0;

	// The rule bits (Terrain, clear=) win over the config and over "keep=": those bits are cleared even when the system
	// is started (Terrain c1: the system runs its per-load bake and keeps the heights, but is neither updated nor drawn).
	const DWORD dwClearBits = ClearBits();
	const DWORD dwAllowed = (dwConfig | ~KNOWN_BITS | g_dwKeep) & ~dwClearBits;

	// Bits this write cleared earlier that are allowed now (test switch: put in "keep=", or taken out of "clear="):
	// set them back (never River or Arrows).
	const DWORD dwRestore = dwAllowed & g_dwCleared & ~NEVER_RESTORE;
	if (dwRestore != 0)
	{
		const DWORD dwBefore = static_cast<DWORD>(*pView);
		const DWORD dwAfter = OrBits(pView, dwRestore);
		g_dwCleared &= ~dwRestore;
		g_dwWrites++;
		Logf("%s (Updates %lu): keep=0x%08lx clear=0x%08lx: set back 0x%08lx: view 0x%08lx -> 0x%08lx", szWhy, g_dwUpdates,
		     g_dwKeep, dwClearBits, dwRestore, dwBefore, dwAfter);
	}

	LONG lOld = *pView;
	if ((static_cast<DWORD>(lOld) & ~dwAllowed) != 0)
	{
		LONG lNew = 0;
		for (;;)
		{
			lNew = static_cast<LONG>(static_cast<DWORD>(lOld) & dwAllowed);
			const LONG lSeen = InterlockedCompareExchange(pView, lNew, lOld);
			if (lSeen == lOld) break;
			lOld = lSeen;
		}
		const DWORD dwCleared = static_cast<DWORD>(lOld) & ~dwAllowed;
		g_dwCleared |= dwCleared;
		g_dwWrites++;
		char szGap[96] = "";
		if (dPollGapMs >= 0)
			_snprintf_s(szGap, sizeof(szGap), _TRUNCATE, " (watcher: the previous poll %.1f ms before saw them clear)",
			            dPollGapMs);
		Logf("%s (Updates %lu): config 0x%08lx keep 0x%08lx clear 0x%08lx: view 0x%08lx -> 0x%08lx (cleared 0x%08lx), read "
		     "back 0x%08lx, GameViewState 0x%08lx%s",
		     szWhy, g_dwUpdates, dwConfig, g_dwKeep, dwClearBits, static_cast<DWORD>(lOld), static_cast<DWORD>(lNew),
		     dwCleared, static_cast<DWORD>(*pView), dwGvs, szGap);
		return dwCleared;
	}
	if (bReadback)
	{
		Logf("%s (Updates %lu): config 0x%08lx view 0x%08lx (view & config 0x%08lx; nothing to clear)", szWhy, g_dwUpdates,
		     dwConfig, static_cast<DWORD>(lOld), static_cast<DWORD>(lOld) & dwConfig);
	}
	return 0;
}

// One watcher poll under SEH: the GameViewState is the EXE's, and its main thread may free it at any time (an exit to
// the main menu); a fault while reading it ends the poll, never the game.
DWORD PollGuarded(double dGapMs)
{
	DWORD dwCleared = 0;
	__try
	{
		dwCleared = Apply("watch", false, dGapMs);
	}
	__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		g_bLastGvsValid = false;  // re-check the pointers at the next poll
		g_dwLastApp = 0;
		dwCleared = 0;
		g_watchFaults++;
	}
	return dwCleared;
}

// A high-resolution waitable timer (Windows 10 1803+, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION) gives the watcher its
// 10 ms without changing the system's timer resolution; without one, Sleep(10) waits up to two ticks (~31 ms).
HANDLE CreatePeriodTimer()
{
	typedef HANDLE(WINAPI * CreateTimerExFn)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);
	CreateTimerExFn create =
	    (CreateTimerExFn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreateWaitableTimerExW");
	if (!create) return NULL;
	const DWORD kHighResolution = 0x00000002;  // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
	const DWORD kTimerAllAccess = 0x001F0003;  // TIMER_ALL_ACCESS
	return create(NULL, NULL, kHighResolution, kTimerAllAccess);
}

void WaitPeriod(HANDLE hTimer)
{
	if (hTimer)
	{
		LARGE_INTEGER due;
		due.QuadPart = -(LONGLONG)WATCH_EVERY_MS * 10000;  // relative, in 100 ns
		if (SetWaitableTimer(hTimer, &due, 0, NULL, NULL, FALSE) && WaitForSingleObject(hTimer, 1000) == WAIT_OBJECT_0)
			return;
	}
	Sleep(WATCH_EVERY_MS);
}

HANDLE g_hWatchTimer = NULL;  // the watcher's high-resolution timer (NULL: Sleep)

DWORD WINAPI WatchThread(LPVOID pTimer)
{
	// Never returns: OnUnload terminates it while it is outside its work (g_watchBusy == 0), under the loader lock,
	// where a thread that ended by itself could not be waited for.
	HANDLE hTimer = static_cast<HANDLE>(pTimer);
	for (;;)
	{
		InterlockedExchange(&g_watchBusy, 1);
		if (g_watchStop == 0)
		{
			EnterCriticalSection(&g_lock);
			const double dNow = NowMs();
			const double dGap = g_watchLastPollMs > 0 ? dNow - g_watchLastPollMs : -1;
			g_watchLastPollMs = dNow;
			InterlockedIncrement(&g_watchPolls);
			if (g_bActive && (PollGuarded(dGap) & TERRAIN_BIT) != 0) g_watchClearedAtMs = NowMs();
			LeaveCriticalSection(&g_lock);
		}
		InterlockedExchange(&g_watchBusy, 0);
		WaitPeriod(hTimer);
	}
}

void StartWatcher()
{
	if (g_hWatch) return;
	g_watchStop = 0;
	g_watchLastPollMs = 0;
	DWORD tid = 0;
	g_hWatchTimer = CreatePeriodTimer();
	g_hWatch = CreateThread(NULL, 64 * 1024, WatchThread, g_hWatchTimer, STACK_SIZE_PARAM_IS_A_RESERVATION, &tid);
	if (g_hWatch)
		ProxyRtInt::Log("shell: view mask: watcher thread %lu re-checks every %lu ms (%s; the window between "
		                "GVS::StartupGame and the first Update)",
		                tid, WATCH_EVERY_MS, g_hWatchTimer ? "high-resolution timer" : "Sleep, up to ~31 ms");
	else
	{
		ProxyRtInt::Log("shell: view mask: the watcher thread could not be created (error %lu); the Update hook alone "
		                "re-checks",
		                GetLastError());
		if (g_hWatchTimer) CloseHandle(g_hWatchTimer);
		g_hWatchTimer = NULL;
	}
}

// Under the loader lock (or from a test): no logging.
void StopWatcher()
{
	if (!g_hWatch) return;
	InterlockedExchange(&g_watchStop, 1);
	for (int i = 0; i < 500 && g_watchBusy != 0; ++i) Sleep(1);  // its work takes microseconds; a log line, a few ms
	TerminateThread(g_hWatch, 0);
	WaitForSingleObject(g_hWatch, 1000);  // a terminated thread is signalled at once; no loader lock is needed for it
	CloseHandle(g_hWatch);
	g_hWatch = NULL;
	if (g_hWatchTimer) CloseHandle(g_hWatchTimer);
	g_hWatchTimer = NULL;
}

// Installs against the image at dwBase. The caller has decided the rule bits.
bool InstallAt(DWORD dwBase, bool bProduct, bool bTerrain, DWORD dwKeep, DWORD dwClear, bool bTestSwitch, bool bWatcher)
{
	EnsureLock();
	const char* szProblem = VerifyExe(dwBase);
	g_bTestSwitch = bTestSwitch;
	g_bProduct = bProduct;
	if (szProblem != NULL)
	{
		AppendToLog(szProblem);
		ProxyRtInt::Log("shell: view mask: %s; nothing written", szProblem);
		return false;
	}
	EnterCriticalSection(&g_lock);
	g_dwBase = dwBase;
	g_bTerrain = bTerrain;
	g_dwKeep = dwKeep;
	g_dwClear = dwClear;
	g_dwLastMarkerTick = GetTickCount();
	g_dwLastReadbackTick = g_dwLastMarkerTick;
	g_bActive = true;
	Logf("active: exe=%s base=0x%08lx rule: view &= config | 0xFF000000%s%s%s; keep=0x%08lx "
	     "clear=0x%08lx%s",
	     VpExeBuildLabel(g_iBuild), g_dwBase, bTerrain ? ", Terrain (0x1) cleared (the composite is loaded)" : "",
	     g_bArrows ? ", Arrows (0x400000) cleared (the server draws the arc, ticket 0199)" : "",
	     g_bPath ? ", MovementPath (0x800000) cleared (the server draws the path's models, ticket 0300)" : "", g_dwKeep, g_dwClear,
	     bTestSwitch ? " (test switch crashlogs\\viewmask.enabled; log crashlogs\\viewmask.log)" : "");
	Apply("boot", true, -1);
	LeaveCriticalSection(&g_lock);
	if (bWatcher) StartWatcher();
	return true;
}

void OnUpdate()
{
	if (!g_bActive) return;
	EnterCriticalSection(&g_lock);
	g_dwUpdates++;
	const DWORD dwNow = GetTickCount();
	if (g_bTestSwitch && dwNow - g_dwLastMarkerTick >= MARKER_EVERY_MS)
	{
		g_dwLastMarkerTick = dwNow;
		DWORD dwKeep = g_dwKeep;
		DWORD dwClear = g_dwClear;
		if (!ReadMarker(&dwKeep, &dwClear))
		{
			// The test switch was removed: what it cleared is set back, except River and Arrows and, with
			// ServerRenderer = 1, what the product rule still clears (the config's off bits, Terrain while the composite
			// is loaded); the product rule goes on.
			DWORD dwConfig = 0;
			DWORD dwGvs = 0;
			volatile LONG* pView = FindViewMask(&dwConfig, &dwGvs);
			DWORD dwRestore = g_dwCleared & ~NEVER_RESTORE;
			if (g_bProduct) dwRestore &= (dwConfig | ~KNOWN_BITS) & ~(g_bTerrain ? TERRAIN_BIT : 0);
			if (pView != NULL && dwRestore != 0)
			{
				const DWORD dwBefore = static_cast<DWORD>(*pView);
				const DWORD dwAfter = OrBits(pView, dwRestore);
				g_dwCleared &= ~dwRestore;
				g_dwWrites++;
				Logf("test switch removed (Updates %lu): set back 0x%08lx: view 0x%08lx -> 0x%08lx", g_dwUpdates, dwRestore,
				     dwBefore, dwAfter);
			}
			else
				Logf("test switch removed (Updates %lu): nothing to set back", g_dwUpdates);
			g_dwKeep = 0;
			g_dwClear = 0;
			g_bTestSwitch = false;
			if (!g_bProduct)
			{
				g_bActive = false;  // nothing of the product is on: stopped, as the origin did
				Logf("stopped (ServerRenderer = 0)");
			}
			LeaveCriticalSection(&g_lock);
			return;
		}
		if (dwKeep != g_dwKeep)
		{
			Logf("keep changed: 0x%08lx -> 0x%08lx", g_dwKeep, dwKeep);
			g_dwKeep = dwKeep;
		}
		if (dwClear != g_dwClear)
		{
			Logf("clear changed: 0x%08lx -> 0x%08lx", g_dwClear, dwClear);
			g_dwClear = dwClear;
		}
	}
	const bool bReadback = g_bTestSwitch && dwNow - g_dwLastReadbackTick >= READBACK_EVERY_MS;
	if (bReadback) g_dwLastReadbackTick = dwNow;
	if (g_watchClearedAtMs > 0)
	{
		// The window the watcher closed: how long the Terrain bit would have stayed set with the Update hook alone.
		Logf("first Update %.0f ms after the watcher cleared the Terrain bit (without the watcher it stays set that long "
		     "after GVS::StartupGame)",
		     NowMs() - g_watchClearedAtMs);
		g_watchClearedAtMs = 0;
	}
	Apply("re-check", bReadback, -1);
	LeaveCriticalSection(&g_lock);
}

void Reset()
{
	StopWatcher();
	EnsureLock();
	EnterCriticalSection(&g_lock);
	g_bActive = false;
	g_bTestSwitch = false;
	g_bTerrain = false;
	g_bProduct = false;
	g_bArrows = false;   // ticket 0199
	g_bPath = false;     // ticket 0300
	g_dwBase = g_dwKeep = g_dwClear = g_dwCleared = g_dwLastGvs = g_dwLastApp = g_dwLoggedBadApp = 0;
	g_bLastGvsValid = false;
	g_dwUpdates = g_dwWrites = 0;
	g_iLogLines = 0;
	g_bLoggedWaitApp = g_bLoggedWaitGvs = false;
	g_watchClearedAtMs = 0;
	g_watchPolls = 0;
	g_watchFaults = 0;
	LeaveCriticalSection(&g_lock);
}
}  // namespace Mask

// ================================================================================================ the white fog mask
// Issue 0172 (the product form of issue 3011's test-DLL fix, decision D56; whether it ships is the owner's question 0098).
// Ported from the project's fork of Community-Patch-DLL, branch i2-white-mask, commit 66b0b2bdc (GPL-3.0, like this
// repository), CvGameCoreDLL_Expansion2/ViewMaskSync.cpp, "whitemask=2" (COMMON rule 13, decision D22). What it does and
// why (research/02-exe-internals/disable-systems-and-masks.md section 5, "Why the models are dark red in D33" and the
// white mask stand-in, and "The white mask on the product path"):
//   - with FluidFOW off the FOW mask texture handle (VA 0xDA12AC) stays 0, so the building, improvement, resource and
//     wonder shaders draw the fog base layer instead of the lit model (dark red) and the forest shader discards every tree;
//   - at the boot (DllGetGameContext, the EXE's main thread at the front end) the EXE's own texture loader (VA 0x8C6540,
//     __thiscall, ECX = [VA 0x3010570], one argument: the path; called, never changed) loads the map editor's
//     assets\terrain\editor\White.dds once per process, and the handle goes into 0xDA12AC with a compare-and-swap;
//   - the draw objects built at app Startup, before the proxy is loaded (city buildings and wonders, trees), copied the
//     handle while it was 0: one pass over the EXE's static draw-object pool (VA 0x313DE88, 0x80 bytes per entry; its
//     allocator at VA 0x30BCE00: +0x40 high-water index, +0x44 + 4 * index use count) writes the handle into the mask
//     slot where it is 0 and the fog-ramp slot holds the fog ramp (building family programs 1-6: t7, t5; forest 44: t6,
//     t4); the pass is repeated at most once a second from the Update hook (the game-core thread; never the loader), for
//     draw objects made from the boot-time templates later (a city founded, a building built).
// Every guard is on bytes that are the same in every installed copy of the EXE (research/06-tooling-and-workflows/
// quiet-testbed.md 15.3: .text and the link timestamp): Mask::VerifyExe first, then the code patterns below and the
// absolute operands inside them. Against the origin, its check of the White.dds string in the EXE's .rdata is dropped
// (.rdata differs per copy); the proxy passes its own copy of the path. Data writes only; no EXE code byte is touched (D4).
// The texture is the process's: a later boot of the proxy in the same EXE process (issue 3308) finds the handle in
// 0xDA12AC and in VP64SHELL_WHITE in the EXE's environment, and takes it over instead of loading a second one.
namespace White
{
// Ticket 4963: the addresses of the build Verify found, from its table (exebuild/symbols/proxy.tsv; the VAs of build
// 4390913 in parentheses): pxw_tex_loader (0x8C6540), pxw_renderer (0x3010570: the loader's this), pxw_device
// (0x301057C: the D3D11 device the loader creates the texture with), pxw_fow_mask (0xDA12AC: FOW mask texture handle),
// pxw_fow_base (0xDA12B0: FOW base layer handle), pxw_fog_ramp (0xDA12B4: distance-fog ramp handle, t5 building family,
// t4 forest), pxw_tex_views (0x414C348: 12 bytes per handle, +0 = shader resource view), pxw_white_path (0xA3D66C: the
// editor's path string, only its address is compared), pxw_drawobj_alloc (0x30BCE00), pxw_drawobj_pool (0x313DE88).
DWORD RVA_TEX_LOADER = 0, RVA_RENDERER_GLOBAL = 0, RVA_DEVICE_GLOBAL = 0, RVA_FOW_MASK = 0, RVA_FOW_BASE = 0,
      RVA_FOG_RAMP = 0, RVA_TEX_VIEWS = 0, RVA_DRAWOBJ_ALLOC = 0, RVA_DRAWOBJ_POOL = 0;
const DWORD MAX_TEX_HANDLE = 0x3000;
const DWORD DRAWOBJ_SIZE = 0x80;
const DWORD MAX_DRAWOBJ = 0x20000;
const DWORD FLUIDFOW_BIT = 0x8000;           // [DisableSystems] FluidFOW, config bit 15
const WORD PROGRAM_BUILDING_FIRST = 1;       // BuildingShader .. BuildingShader_Skinned_CODE: t5 fog ramp, t7 FOW mask
const WORD PROGRAM_BUILDING_LAST = 6;
const WORD PROGRAM_FOREST = 44;              // ForestShader: t4 fog ramp, t6 FOW mask
const DWORD PASS_EVERY_MS = 1000;
const int MAX_LOG_LINES = 300;
const char WHITE_PATH[] = "assets\\terrain\\editor\\White.dds";
const char ENV_HANDLE[] = "VP64SHELL_WHITE";  // the handle this EXE process loaded, for a later boot (issue 3308)
// Ticket 0339 (MC answer #2058): the model fog. The composite (shell/composite/fow_models.cpp) puts the server's mask
// behind the white handle; then a model on an unexplored texel shows the fog base layer, so the base-layer slots must hold
// the cloud base FluidFOW would have loaded (FogCloudTile_dif_02.dds, by the same loader, bare name, stored at 0xDA12B0:
// FluidFOW's own call, pxw_fluidfow_base_load). One more call of the loader at the boot, its handle written into 0xDA12B0
// (compare-and-swap from 0) and into the base-layer slot of every draw object whose mask slot holds the white handle and
// whose base slot is 0 (building family t8 +0x28, forest t7 +0x26: the builders' own copies below). A value that is not
// the expected old one is a mismatch: VP64SHELL_FOWBASE_WHY names it and the composite takes the D125 route with it.
// The model fog's default when neither VPSHELL_MODEL_FOW nor shell_composite.ini's model_fow says: must equal
// shell/composite/fow_models.h FOW_MODELS_DEFAULT (the composite reads the same switch).
#define VPSHELL_MODEL_FOW_DEFAULT 1
const char BASE_PATH[] = "FogCloudTile_dif_02.dds";
const char ENV_BASE[] = "VP64SHELL_FOWBASE";       // the cloud base handle (for the composite and a later boot)
const char ENV_BASE_WHY[] = "VP64SHELL_FOWBASE_WHY"; // the model fog is wanted but the base layer failed: why

// Code patterns (.text; research section "The white mask on the product path" lists them with their meaning). The
// absolute operands inside them (relocated with the image) are R(symbol) bytes: the editor's loader call takes this from
// the renderer global, pushes the White.dds path and stores the handle in the FOW mask global; the building, forest and
// city builders read the FOW mask (and the city builder the fog ramp); the draw-object allocation names the allocator and
// the pool (ticket 4963: was a separate operand table).
#define VP_SHELL_WHITE_PATTERNS(X) \
	X(pxw_tex_loader, "texture loader entry", "55 8B EC 83 E4 F0 B8 88 05 01 00 E8 C0 03 06 00 8B 45 08 56 57 8B F9 50") \
	X(pxw_tex_loader_create, "texture loader create", "51 8B F0 56 8B CF E8 5A 57 FF FF") \
	X(pxw_tex_loader_return, "texture loader return", "8B C6 5E 8B E5 5D C2 04 00") \
	X(pxw_editor_site, "editor white load", "8B 0D R(pxw_renderer) 68 R(pxw_white_path) A3 ?? ?? ?? ?? E8 60 45 36 00 8B CE A3 R(pxw_fow_mask)") \
	X(pxw_builder_read, "builder mask copy", "0F B7 05 R(pxw_fow_mask) 8B 54 24 1C 66 89 42 26") \
	X(pxw_pool_alloc_fn, "draw-object alloc", "56 57 B9 R(pxw_drawobj_alloc) E8 44 F2 FF FF 8B F8 C1 E0 07 68 80 00 00 00 8D B0 R(pxw_drawobj_pool)") \
	X(pxw_drawobj_highwater, "draw-object high-water", "8B 7E 40 8D 47 01 3D 00 00 02 00 72 0A") \
	X(pxw_drawobj_usecount, "draw-object use count", "89 46 40 FF 44 BE 44 8B C7") \
	X(pxw_forest_program, "forest program", "6A 2C 52 8D 44 24 18 50 E8 97 48 20 00") \
	X(pxw_forest_read, "forest mask copy", "0F B7 15 R(pxw_fow_mask) 8B 4C 24 28 66 89 51 24") \
	X(pxw_city_read, "city mask copy", "66 8B 0D R(pxw_fow_mask) 33 D2 66 89 4E 26") \
	X(pxw_city_ramp, "city fog ramp copy", "0F B7 05 R(pxw_fog_ramp) 66 89 46 22")
const Mask::CodePattern PATTERNS[] = {VP_SHELL_WHITE_PATTERNS(VP_SHELL_ROW)};

// Ticket 0339: checked only when the model fog is wanted (a refusal here never turns the white mask off). FluidFOW's
// cloud base load takes this from the renderer global and stores into the base-layer global; the three builders read it.
#define VP_SHELL_BASE_PATTERNS(X) \
	X(pxw_fluidfow_base_load, "FluidFOW cloud base load", "8B 0D R(pxw_renderer) 68 ?? ?? ?? ?? E8 0D B6 47 00 8B 0D ?? ?? ?? ?? 68 ?? ?? ?? ?? A3 R(pxw_fow_base)") \
	X(pxw_base_building_read, "builder base copy", "0F B7 0D R(pxw_fow_base) 8B 44 24 1C 66 89 48 28") \
	X(pxw_base_forest_read, "forest base copy", "0F B7 05 R(pxw_fow_base) 8B 54 24 28 66 89 42 26") \
	X(pxw_base_city_read, "city base copy", "0F B7 05 R(pxw_fow_base) 83 C4 24 33 C9 66 89 46 28")
const Mask::CodePattern BASE_PATTERNS[] = {VP_SHELL_BASE_PATTERNS(VP_SHELL_ROW)};

typedef DWORD(__fastcall* LoaderFn)(void* pThis, void* pUnusedEdx, const char* szPath);  // __thiscall through __fastcall

struct PoolCounts
{
	DWORD dwHighWater, dwLive;
	DWORD dwBuildingOurs, dwBuildingZero, dwBuildingOther;
	DWORD dwForestOurs, dwForestZero, dwForestOther;
	DWORD dwBaseOurs, dwBaseZero, dwBaseOther;  // ticket 0339: base-layer slots of the entries whose mask slot is white
	bool bFault;
};

volatile LONG g_active = 0;  // 1 once the handle is in 0xDA12AC (set last at the install)
bool g_bPool = false;        // the pool pass is on (cleared by a fault)
DWORD g_dwBase = 0;
WORD g_wHandle = 0;
DWORD g_dwView = 0;
DWORD g_dwLastPassTick = 0;
DWORD g_dwPasses = 0;
DWORD g_dwPatched = 0;  // mask slots written, all passes
DWORD g_dwLastOther = 0;
bool g_bLoggedForeign = false;
bool g_bLoggedViewChange = false;
int g_iLogLines = 0;
WORD g_wBase = 0;              // ticket 0339: the cloud base handle (0 = the model fog's base layer is not installed)
DWORD g_dwBasePatched = 0;     // base-layer slots written, all passes
bool g_bBaseFailed = false;    // a base-layer mismatch was reported (VP64SHELL_FOWBASE_WHY); no more base writes

void Logf(const char* szFormat, ...)
{
	if (g_iLogLines >= MAX_LOG_LINES) return;
	++g_iLogLines;
	char szMessage[900];
	va_list args;
	va_start(args, szFormat);
	_vsnprintf_s(szMessage, sizeof(szMessage), _TRUNCATE, szFormat, args);
	va_end(args);
	ProxyRtInt::Log("shell: white mask: %s%s", szMessage,
	                g_iLogLines == MAX_LOG_LINES ? " (white mask log line limit reached)" : "");
}

// NULL when every white-mask guard passes on the image at dwBase (Mask::VerifyExe included), else the reason.
const char* Verify(DWORD dwBase)
{
	const char* szExe = Mask::VerifyExe(dwBase);
	if (szExe != NULL) return szExe;
	// ticket 4963: the white mask's addresses in the build Mask::VerifyExe found
	static char s_szWhy[220];
	const int iBuild = Mask::g_iBuild;
	const struct
	{
		DWORD* pRva;
		int iSym;
	} kLayout[] = {{&RVA_TEX_LOADER, VPX_pxw_tex_loader},     {&RVA_RENDERER_GLOBAL, VPX_pxw_renderer},
	               {&RVA_DEVICE_GLOBAL, VPX_pxw_device},      {&RVA_FOW_MASK, VPX_pxw_fow_mask},
	               {&RVA_FOW_BASE, VPX_pxw_fow_base},         {&RVA_FOG_RAMP, VPX_pxw_fog_ramp},
	               {&RVA_TEX_VIEWS, VPX_pxw_tex_views},       {&RVA_DRAWOBJ_ALLOC, VPX_pxw_drawobj_alloc},
	               {&RVA_DRAWOBJ_POOL, VPX_pxw_drawobj_pool}};
	for (size_t i = 0; i < sizeof(kLayout) / sizeof(kLayout[0]); i++)
		if (!ProxyExe::Va(iBuild, kLayout[i].iSym))
			return ProxyExe::NotCarried(s_szWhy, sizeof(s_szWhy), "off: ", iBuild, kLayout[i].iSym);
	for (size_t i = 0; i < sizeof(kLayout) / sizeof(kLayout[0]); i++) *kLayout[i].pRva = ProxyExe::Rva(iBuild, kLayout[i].iSym);
	const IMAGE_DOS_HEADER* pDos = reinterpret_cast<const IMAGE_DOS_HEADER*>(dwBase);
	const IMAGE_NT_HEADERS* pNt = reinterpret_cast<const IMAGE_NT_HEADERS*>(dwBase + pDos->e_lfanew);
	const DWORD dwImageSize = pNt->OptionalHeader.SizeOfImage;
	if (RVA_TEX_VIEWS + MAX_TEX_HANDLE * 12 > dwImageSize || RVA_DRAWOBJ_POOL + MAX_DRAWOBJ * DRAWOBJ_SIZE > dwImageSize ||
	    RVA_DRAWOBJ_ALLOC + 0x44 + MAX_DRAWOBJ * 4 > dwImageSize)
		return "off: image smaller than expected";
	const char* szRows = Mask::CheckRows(iBuild, dwBase, PATTERNS, sizeof(PATTERNS) / sizeof(PATTERNS[0]), "off: ",
	                                     s_szWhy, sizeof(s_szWhy));
	if (szRows != NULL) return szRows;
	if (!Mask::IsAccessible(dwBase + RVA_FOW_MASK, 12, true) || !Mask::IsAccessible(dwBase + RVA_RENDERER_GLOBAL, 16, false) ||
	    !Mask::IsAccessible(dwBase + RVA_TEX_VIEWS, MAX_TEX_HANDLE * 12, false) ||
	    !Mask::IsAccessible(dwBase + RVA_DRAWOBJ_ALLOC, 0x44 + MAX_DRAWOBJ * 4, false) ||
	    !Mask::IsAccessible(dwBase + RVA_DRAWOBJ_POOL, MAX_DRAWOBJ * DRAWOBJ_SIZE, true))
		return "off: the EXE's globals, texture table or draw-object pool are not accessible";
	return NULL;
}

// The EXE's texture loader under SEH (no C++ objects in this function). 0 on failure; *pbFault on an exception.
DWORD CallLoader(LoaderFn pfn, void* pThis, const char* szPath, bool* pbFault)
{
	*pbFault = false;
	DWORD dwHandle = 0;
	__try
	{
		dwHandle = pfn(pThis, NULL, szPath);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		*pbFault = true;
		dwHandle = 0;
	}
	return dwHandle;
}

DWORD ViewOf(DWORD dwHandle)
{
	return *reinterpret_cast<const DWORD*>(g_dwBase + RVA_TEX_VIEWS + dwHandle * 12);
}

// One pass over the live draw objects (use count > 0, index below the high-water mark). Counts the building-family and
// forest entries by their mask slot; with bWrite, writes wHandle where the mask slot is 0 and the fog-ramp slot holds the
// fog ramp (the builders' own layout). No locks, no calls. Not inlined: ScanPool calls it inside __try, because clang-cl
// catches a fault reliably only at a call inside the guarded block (as PollGuarded does with Apply); a fault in the loop
// itself, inlined, ended the offline test's process (2026-11-14).
__declspec(noinline) void ScanPoolUnguarded(bool bWrite, WORD wHandle, WORD wBase, PoolCounts* pCounts)
{
	const DWORD dwAlloc = g_dwBase + RVA_DRAWOBJ_ALLOC;
	const DWORD dwPool = g_dwBase + RVA_DRAWOBJ_POOL;
	const WORD wFogRamp = *reinterpret_cast<const volatile WORD*>(g_dwBase + RVA_FOG_RAMP);
	DWORD dwHigh = *reinterpret_cast<const volatile DWORD*>(dwAlloc + 0x40);
	if (dwHigh > MAX_DRAWOBJ) dwHigh = MAX_DRAWOBJ;
	pCounts->dwHighWater = dwHigh;
	for (DWORD i = 0; i < dwHigh; i++)
	{
		if (*reinterpret_cast<const volatile LONG*>(dwAlloc + 0x44 + i * 4) <= 0) continue;
		pCounts->dwLive++;
		unsigned char* pEntry = reinterpret_cast<unsigned char*>(dwPool + i * DRAWOBJ_SIZE);
		const WORD wProgram = *reinterpret_cast<const volatile WORD*>(pEntry);
		const BYTE bTextures = *reinterpret_cast<const volatile BYTE*>(pEntry + 0x7C);
		DWORD dwMaskOff = 0, dwRampOff = 0, dwBaseOff = 0;
		bool bForest = false;
		if (wProgram >= PROGRAM_BUILDING_FIRST && wProgram <= PROGRAM_BUILDING_LAST && bTextures >= 8)
		{
			dwMaskOff = 0x18 + 2 * 7;
			dwRampOff = 0x18 + 2 * 5;
			dwBaseOff = bTextures >= 9 ? 0x18 + 2 * 8 : 0;   // t8 (0x7B2695, 0x5A25E5)
		}
		else if (wProgram == PROGRAM_FOREST && bTextures >= 7)
		{
			dwMaskOff = 0x18 + 2 * 6;
			dwRampOff = 0x18 + 2 * 4;
			dwBaseOff = bTextures >= 8 ? 0x18 + 2 * 7 : 0;   // t7 (0x6C224A)
			bForest = true;
		}
		else
			continue;
		volatile WORD* pMask = reinterpret_cast<volatile WORD*>(pEntry + dwMaskOff);
		const WORD wMask = *pMask;
		bool bOurs = false;
		if (wMask == wHandle)
		{
			++(bForest ? pCounts->dwForestOurs : pCounts->dwBuildingOurs);
			bOurs = true;
		}
		else if (wMask == 0 && *reinterpret_cast<const volatile WORD*>(pEntry + dwRampOff) == wFogRamp)
		{
			++(bForest ? pCounts->dwForestZero : pCounts->dwBuildingZero);
			if (bWrite)
			{
				*pMask = wHandle;
				bOurs = true;
			}
		}
		else
			++(bForest ? pCounts->dwForestOther : pCounts->dwBuildingOther);
		// ticket 0339: the base layer of the entries whose mask is the white handle (expected old value 0)
		if (wBase == 0 || !bOurs || dwBaseOff == 0) continue;
		volatile WORD* pBase = reinterpret_cast<volatile WORD*>(pEntry + dwBaseOff);
		const WORD wOld = *pBase;
		if (wOld == wBase)
			++pCounts->dwBaseOurs;
		else if (wOld == 0)
		{
			++pCounts->dwBaseZero;
			if (bWrite) *pBase = wBase;
		}
		else
			++pCounts->dwBaseOther;
	}
}

// The pass under SEH: an access violation ends it (pCounts->bFault), never the game.
void ScanPool(bool bWrite, WORD wHandle, PoolCounts* pCounts)
{
	memset(pCounts, 0, sizeof(*pCounts));
	__try
	{
		ScanPoolUnguarded(bWrite, wHandle, g_bBaseFailed ? 0 : g_wBase, pCounts);
	}
	__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		pCounts->bFault = true;
	}
}

void LogPool(const char* szWhen, const PoolCounts& k)
{
	Logf("pool pass (%s): high-water %lu, live %lu; building family: ours %lu, zero->ours %lu, other %lu; forest: ours "
	     "%lu, zero->ours %lu, other %lu%s",
	     szWhen, k.dwHighWater, k.dwLive, k.dwBuildingOurs, k.dwBuildingZero, k.dwBuildingOther, k.dwForestOurs,
	     k.dwForestZero, k.dwForestOther, k.bFault ? "; ACCESS VIOLATION during the pass: the pass is off from now on" : "");
	if (g_wBase != 0)
		Logf("pool pass (%s): base layer (ticket 0339) of the white entries: ours %lu, zero->ours %lu, other %lu", szWhen,
		     k.dwBaseOurs, k.dwBaseZero, k.dwBaseOther);
}

// Ticket 0339: a base-layer value that is not the expected old one (MC #2058: never a silent skip). The composite reads
// VP64SHELL_FOWBASE_WHY and takes the D125 route with it; the proxy writes no more base slots.
void BaseFail(const char* szWhy)
{
	g_bBaseFailed = true;
	SetEnvironmentVariableA(ENV_BASE, NULL);
	SetEnvironmentVariableA(ENV_BASE_WHY, szWhy);
	Logf("base layer (ticket 0339) FAILED: %s; the model fog takes the D125 route (shell_composite.dll)", szWhy);
}

void Pass(const char* szWhen, bool bAlwaysLog)
{
	PoolCounts k;
	const double t0 = NowMs();
	ScanPool(true, g_wHandle, &k);
	const double dMs = NowMs() - t0;
	g_dwPasses++;
	g_dwPatched += k.dwBuildingZero + k.dwForestZero;
	g_dwBasePatched += k.dwBaseZero;
	const DWORD dwOther = k.dwBuildingOther + k.dwForestOther;
	if (bAlwaysLog || k.bFault || k.dwBuildingZero + k.dwForestZero + k.dwBaseZero + k.dwBaseOther != 0 || dwOther != g_dwLastOther)
	{
		LogPool(szWhen, k);
		if (bAlwaysLog) Logf("pool pass took %.2f ms (thread %lu)", dMs, GetCurrentThreadId());
	}
	g_dwLastOther = dwOther;
	if (k.bFault) g_bPool = false;
	if (g_wBase != 0 && !g_bBaseFailed)
	{
		char szWhy[200];
		if (k.bFault)
			BaseFail("an access violation in the draw-object pass");
		else if (k.dwBaseOther != 0)
		{
			_snprintf_s(szWhy, sizeof(szWhy), _TRUNCATE,
			            "%lu draw objects with the white mask hold a base layer that is neither 0 nor the cloud base 0x%x (%s pass)",
			            k.dwBaseOther, g_wBase, szWhen);
			BaseFail(szWhy);
		}
	}
}

// The handle an earlier boot of the proxy in this EXE process loaded (issue 3308), or 0.
DWORD EarlierHandle()
{
	char v[16] = "";
	const DWORD n = GetEnvironmentVariableA(ENV_HANDLE, v, sizeof(v));
	if (n == 0 || n >= sizeof(v)) return 0;
	char* end = NULL;
	const unsigned long x = strtoul(v, &end, 0);
	return (end && *end == 0 && x > 0 && x < MAX_TEX_HANDLE) ? x : 0;
}

// Ticket 0339: the cloud base for the model fog, after the white handle is in place (g_dwBase set). Every refusal and
// mismatch goes to BaseFail (the composite's D125 route); nothing of the white mask changes.
void InstallBase(LoaderFn pfnTest)
{
	SetEnvironmentVariableA(ENV_BASE_WHY, NULL);
	const DWORD dwBase = g_dwBase;
	char szWhy[200];
	if (Mask::CheckRows(Mask::g_iBuild, dwBase, BASE_PATTERNS, sizeof(BASE_PATTERNS) / sizeof(BASE_PATTERNS[0]), "",
	                    szWhy, sizeof(szWhy)) != NULL)
	{
		BaseFail(szWhy);
		return;
	}
	volatile LONG* pGlobal = reinterpret_cast<volatile LONG*>(dwBase + RVA_FOW_BASE);
	const DWORD dwBefore = static_cast<DWORD>(*pGlobal);
	DWORD dwHandle = 0;
	if (dwBefore != 0)
	{
		char v[16] = "";
		const DWORD n = GetEnvironmentVariableA(ENV_BASE, v, sizeof(v));
		const DWORD dwEarlier = (n > 0 && n < sizeof(v)) ? strtoul(v, NULL, 0) : 0;
		if (dwEarlier == 0 || dwEarlier != dwBefore || dwBefore >= MAX_TEX_HANDLE || ViewOf(dwBefore) == 0)
		{
			_snprintf_s(szWhy, sizeof(szWhy), _TRUNCATE,
			            "the base-layer global (pxw_fow_base) holds 0x%08lx at the boot, expected 0 (or the handle %s %lu of an earlier "
			            "boot)",
			            dwBefore, ENV_BASE, dwEarlier);
			BaseFail(szWhy);
			return;
		}
		dwHandle = dwBefore;
		Logf("base layer (ticket 0339): the base-layer global holds 0x%lx, the cloud base an earlier boot loaded: taken over",
		     dwHandle);
	}
	else
	{
		const DWORD dwRenderer = *reinterpret_cast<const DWORD*>(dwBase + RVA_RENDERER_GLOBAL);
		if (dwRenderer == 0 || !Mask::IsAccessible(dwRenderer, 4, false))
		{
			BaseFail("the EXE's renderer (pxw_renderer) is not ready at the boot");
			return;
		}
		LoaderFn pfn = pfnTest ? pfnTest : reinterpret_cast<LoaderFn>(dwBase + RVA_TEX_LOADER);
		bool bFault = false;
		const double t0 = NowMs();
		dwHandle = CallLoader(pfn, reinterpret_cast<void*>(dwRenderer), BASE_PATH, &bFault);
		const double dMs = NowMs() - t0;
		if (bFault || dwHandle == 0 || dwHandle >= MAX_TEX_HANDLE || ViewOf(dwHandle) == 0)
		{
			_snprintf_s(szWhy, sizeof(szWhy), _TRUNCATE, "the EXE's texture loader %s for %s (handle 0x%08lx, %.1f ms)",
			            bFault ? "raised an EXCEPTION" : "gave no usable texture", BASE_PATH, dwHandle, dMs);
			BaseFail(szWhy);
			return;
		}
		const LONG lSeen = InterlockedCompareExchange(pGlobal, static_cast<LONG>(dwHandle), 0);
		if (lSeen != 0)
		{
			_snprintf_s(szWhy, sizeof(szWhy), _TRUNCATE,
			            "the base-layer global (pxw_fow_base) became 0x%08lx during the load (expected 0); handle 0x%lx not written",
			            static_cast<DWORD>(lSeen), dwHandle);
			BaseFail(szWhy);
			return;
		}
		// ticket 0623: the symbol's name and the EXE build's label, not one build's address
		Logf("base layer (ticket 0339): loaded %s in %.1f ms: handle 0x%lx view 0x%08lx; pxw_fow_base of %s 0x00000000 -> 0x%08lx "
		     "(read back 0x%08lx)",
		     BASE_PATH, dMs, dwHandle, ViewOf(dwHandle), VpExeBuildLabel(Mask::g_iBuild), dwHandle, static_cast<DWORD>(*pGlobal));
	}
	char v[16];
	_snprintf_s(v, sizeof(v), _TRUNCATE, "0x%lx", dwHandle);
	SetEnvironmentVariableA(ENV_BASE, v);
	g_wBase = static_cast<WORD>(dwHandle);
}

// The boot step: pfnTest replaces the EXE's loader in the offline tests (NULL in the product). bModelFog: ticket 0339,
// also install the cloud base (the composite's model fog is on).
bool Install(DWORD dwBase, LoaderFn pfnTest, bool bModelFog)
{
	const char* szProblem = Verify(dwBase);
	if (szProblem != NULL)
	{
		Logf("%s; nothing loaded, nothing written", szProblem);
		return false;
	}
	g_dwBase = dwBase;
	const DWORD dwApp = *reinterpret_cast<const DWORD*>(dwBase + Mask::RVA_APP_GLOBAL);
	if (dwApp == 0 || !Mask::IsAccessible(dwApp, Mask::APP_OFF_CFG + 4, false))
	{
		Logf("off: g_Civ5App 0x%08lx is not readable at the boot; nothing loaded, nothing written", dwApp);
		return false;
	}
	const DWORD dwConfig = *reinterpret_cast<const DWORD*>(dwApp + Mask::APP_OFF_CFG);
	if ((dwConfig & FLUIDFOW_BIT) != 0)
	{
		Logf("off: config 0x%08lx has FluidFOW on (the game's fog owns the mask); nothing loaded, nothing written", dwConfig);
		return false;
	}
	volatile LONG* pMask = reinterpret_cast<volatile LONG*>(dwBase + RVA_FOW_MASK);
	const DWORD dwBefore = static_cast<DWORD>(*pMask);
	const DWORD dwBaseLayer = *reinterpret_cast<const DWORD*>(dwBase + RVA_FOW_BASE);
	DWORD dwHandle = 0;
	if (dwBefore != 0)
	{
		// a later boot of the proxy in this process: the handle the first boot loaded is still there
		const DWORD dwEarlier = EarlierHandle();
		if (dwEarlier == 0 || dwEarlier != dwBefore || ViewOf(dwBefore) == 0)
		{
			Logf("off: the mask global is already 0x%08lx (base layer 0x%08lx), not a handle this process's patch loaded "
			     "(" "VP64SHELL_WHITE" " %lu); left alone",
			     dwBefore, dwBaseLayer, dwEarlier);
			return false;
		}
		dwHandle = dwBefore;
		Logf("the mask global holds 0x%lx, the white texture an earlier boot of the proxy in this process loaded (view "
		     "0x%08lx): taken over, no second load",
		     dwHandle, ViewOf(dwHandle));
	}
	else
	{
		const DWORD dwRenderer = *reinterpret_cast<const DWORD*>(dwBase + RVA_RENDERER_GLOBAL);
		const DWORD dwDevice = *reinterpret_cast<const DWORD*>(dwBase + RVA_DEVICE_GLOBAL);
		if (dwRenderer == 0 || dwDevice == 0 || !Mask::IsAccessible(dwRenderer, 4, false) ||
		    !Mask::IsAccessible(dwDevice, 4, false))
		{
			Logf("off: renderer 0x%08lx or device 0x%08lx not ready at the boot; nothing loaded, nothing written", dwRenderer,
			     dwDevice);
			return false;
		}
		Logf("checks passed (EXE base 0x%08lx, loader 0x%08lx, mask global 0x%08lx, config 0x%08lx, thread %lu)", dwBase,
		     dwBase + RVA_TEX_LOADER, dwBase + RVA_FOW_MASK, dwConfig, GetCurrentThreadId());
		LoaderFn pfn = pfnTest ? pfnTest : reinterpret_cast<LoaderFn>(dwBase + RVA_TEX_LOADER);
		bool bFault = false;
		const double t0 = NowMs();
		dwHandle = CallLoader(pfn, reinterpret_cast<void*>(dwRenderer), WHITE_PATH, &bFault);
		const double dMs = NowMs() - t0;
		if (bFault)
		{
			Logf("off: EXCEPTION in the EXE's texture loader (renderer 0x%08lx, %.1f ms); nothing written", dwRenderer, dMs);
			return false;
		}
		if (dwHandle == 0 || dwHandle >= MAX_TEX_HANDLE)
		{
			Logf("off: the loader returned 0x%08lx for %s (%.1f ms); nothing written", dwHandle, WHITE_PATH, dMs);
			return false;
		}
		if (ViewOf(dwHandle) == 0)
		{
			Logf("off: handle 0x%lx has no shader resource view (%.1f ms); nothing written (the texture stays loaded)",
			     dwHandle, dMs);
			return false;
		}
		const LONG lSeen = InterlockedCompareExchange(pMask, static_cast<LONG>(dwHandle), 0);
		if (lSeen != 0)
		{
			Logf("off: the mask global became 0x%08lx during the load; handle 0x%lx not written", static_cast<DWORD>(lSeen),
			     dwHandle);
			return false;
		}
		char v[16];
		_snprintf_s(v, sizeof(v), _TRUNCATE, "0x%lx", dwHandle);
		SetEnvironmentVariableA(ENV_HANDLE, v);
		Logf("loaded %s in %.1f ms: handle 0x%lx view 0x%08lx; mask global 0x00000000 -> 0x%08lx (read back 0x%08lx); base "
		     "layer 0x%08lx; renderer 0x%08lx device 0x%08lx",
		     WHITE_PATH, dMs, dwHandle, ViewOf(dwHandle), dwHandle, static_cast<DWORD>(*pMask), dwBaseLayer, dwRenderer,
		     dwDevice);
	}
	g_wHandle = static_cast<WORD>(dwHandle);
	g_dwView = ViewOf(dwHandle);
	if (bModelFog) InstallBase(pfnTest);   // ticket 0339
	g_bPool = true;
	g_dwLastPassTick = GetTickCount();
	Pass("boot", true);
	InterlockedExchange(&g_active, 1);
	return true;
}

// From the Update hook (the game-core thread): at most once a second, the global's read-back and the pool pass.
void OnUpdate(bool bForce)
{
	if (g_active == 0) return;
	const DWORD dwNow = GetTickCount();
	if (!bForce && dwNow - g_dwLastPassTick < PASS_EVERY_MS) return;
	g_dwLastPassTick = dwNow;
	volatile LONG* pMask = reinterpret_cast<volatile LONG*>(g_dwBase + RVA_FOW_MASK);
	const DWORD dwGlobal = static_cast<DWORD>(*pMask);
	if (dwGlobal == 0)
	{
		const LONG lSeen = InterlockedCompareExchange(pMask, static_cast<LONG>(g_wHandle), 0);
		Logf("the mask global was 0: rewritten 0x%x (seen 0x%08lx, read back 0x%08lx)", g_wHandle, static_cast<DWORD>(lSeen),
		     static_cast<DWORD>(*pMask));
	}
	else if (dwGlobal != g_wHandle)
	{
		if (!g_bLoggedForeign)
			Logf("the mask global is 0x%08lx, not ours (0x%x): left alone, the pool pass stops", dwGlobal, g_wHandle);
		g_bLoggedForeign = true;
		return;
	}
	if (!g_bLoggedViewChange && ViewOf(g_wHandle) != g_dwView)
	{
		Logf("the view of handle 0x%x CHANGED: 0x%08lx -> 0x%08lx%s", g_wHandle, g_dwView, ViewOf(g_wHandle),
		     g_wBase != 0 ? " (expected: the composite's model fog puts the server's mask behind it, ticket 0339)" : "");
		g_bLoggedViewChange = true;
	}
	if (g_bPool) Pass("update", false);
}

void Reset()
{
	InterlockedExchange(&g_active, 0);
	g_bPool = false;
	g_dwBase = 0;
	g_wHandle = 0;
	g_dwView = 0;
	g_dwLastPassTick = 0;
	g_dwPasses = g_dwPatched = g_dwLastOther = 0;
	g_bLoggedForeign = g_bLoggedViewChange = false;
	g_iLogLines = 0;
	SetEnvironmentVariableA(ENV_HANDLE, NULL);
	g_wBase = 0;
	g_dwBasePatched = 0;
	g_bBaseFailed = false;
	SetEnvironmentVariableA(ENV_BASE, NULL);
	SetEnvironmentVariableA(ENV_BASE_WHY, NULL);
}

// What OnBoot does for the option. bModelFog (ticket 0339): the composite's model fog is on, so the cloud base is loaded
// too.
void Boot(DWORD dwBase, int iOption, LoaderFn pfnTest, bool bModelFog)
{
	if (iOption != 1)
	{
		ProxyRtInt::Log("shell: white mask: [Renderer] WhiteFogMask = 0: off (nothing loaded, nothing written)%s",
		                bModelFog ? "; the model fog (ticket 0339) needs the white mask: it stays off" : "");
		return;
	}
	ProxyRtInt::Log("shell: white mask: [Renderer] WhiteFogMask = 1: on (issue 0172)%s",
	                bModelFog ? "; model fog on (ticket 0339): the cloud base layer is loaded too" : "");
	Install(dwBase, pfnTest, bModelFog);
}

// Ticket 0339: the model fog's switch, read where the composite reads it: VPSHELL_MODEL_FOW in the EXE's environment,
// else [composite] model_fow of shell_composite.ini beside the composite, else the default (fow_models.h).
bool ModelFogWanted(const wchar_t* szCompositePath)
{
	wchar_t v[16] = L"";
	if (GetEnvironmentVariableW(L"VPSHELL_MODEL_FOW", v, 16) > 0 && v[0]) return _wtoi(v) != 0;
	wchar_t ini[MAX_PATH * 2];
	wcsncpy_s(ini, szCompositePath, _TRUNCATE);
	wchar_t* sl = wcsrchr(ini, L'\\');
	if (!sl) return VPSHELL_MODEL_FOW_DEFAULT != 0;
	wcscpy_s(sl + 1, MAX_PATH * 2 - (sl + 1 - ini), L"shell_composite.ini");
	GetPrivateProfileStringW(L"composite", L"model_fow", L"", v, 16, ini);
	return v[0] ? _wtoi(v) != 0 : VPSHELL_MODEL_FOW_DEFAULT != 0;
}
}  // namespace White
}  // namespace

// ================================================================================================ the unit option
// Issue 4759 (epic 0040, breakdown O1 (a) and O2 (a), research/07-architecture-decisions/epic-0040-breakdown.md 11):
// [Renderer] ServerUnits. With 1, and only when this boot writes the server's render lines (ServerRenderer = 1, the
// composite accepted or the test switch, no server_config=), server.cfg gets the unit keys and the Lua bridge is asked
// for its positions-only install (the listeners LocalMachineUnitPositionChanged and UnitMoveQueueChanged and the drain,
// research/03-dll-engine-boundary/proxy-design.md 24.6). Otherwise nothing is written and one line says why. With 0
// (the product default until 0042) nothing changes: no line in server.cfg, no request, no log line of its own.
//
// The glue (proxy_ui_bridge_rt.cpp) is linked into the proxy. Programs that link this file without it (the offline
// shell unit test) get the default below through /alternatename, which logs that the request went nowhere.
namespace ProxyUiBridgeRt
{
void RequestUnitPositions();
}  // namespace ProxyUiBridgeRt
namespace ProxyShellDefault
{
void RequestUnitPositions()
{
	ProxyRtInt::Log("shell: [Renderer] ServerUnits = 1: the Lua bridge's glue is not linked into this program: no position "
	                "listeners");
}
}  // namespace ProxyShellDefault
#pragma comment(linker, "/alternatename:?RequestUnitPositions@ProxyUiBridgeRt@@YAXXZ=?RequestUnitPositions@ProxyShellDefault@@YAXXZ")

namespace
{
const char kUnitLines[] = "render_units=1\nrender_units_passes=1\nrender_units_positions=exe\n";
const char kCombatLine[] = "render_units_triggers=1";   // ticket 4783: with ServerCombat = 1 (written with a newline)

// renderLines: Configure wrote render=1 for this boot.
std::string UnitLines(bool writesServerCfg, bool renderLines)
{
	if (ProxyRtInt::ProxyOptions().ServerUnits() == 0) return std::string();
	const char* why = NULL;
	if (g_cfg.option == 0)
		why = "[Renderer] ServerRenderer = 0 (the server draws the units only with its renderer on)";
	else if (!writesServerCfg)
		why = "vpproxy.ini server_config= names the server's config (that config decides)";
	else if (!renderLines)
		why = "the server's renderer is off for this boot (the composite was refused: the shell: line above)";
	if (why)
	{
		ProxyRtInt::Log("shell: [Renderer] ServerUnits = 1: no unit keys in server.cfg and no position listeners: %s; the "
		                "game draws its own units", why);
		return std::string();
	}
	ProxyRtInt::Log("shell: [Renderer] ServerUnits = 1: server.cfg gets render_units=1 render_units_passes=1 "
	                "render_units_positions=exe; the Lua bridge's position listeners are asked for");
	ProxyUiBridgeRt::RequestUnitPositions();
	std::string lines(kUnitLines);
	// Ticket 4783 (MC #8627): [Renderer] ServerCombat. 1 (default) adds render_units_triggers=1: the server's trigger
	// stream and its question to the proxy, at which route S arms as one (proxy_route_s.h); 0: never asked, stock fights.
	if (ProxyRtInt::ProxyOptions().ServerCombat() != 0)
	{
		ProxyRtInt::Log("shell: [Renderer] ServerCombat = 1: server.cfg gets %s; route S (the server's fight timing, "
		                "sounds and effects) arms as one at the server's question", kCombatLine);
		lines += kCombatLine;
		lines += "\n";
	}
	else
		ProxyRtInt::Log("shell: [Renderer] ServerCombat = 0: route S is off for this session (the player's choice, no "
		                "report): the server never asks, the fights run on the game's own timing, sounds and particles");
	return lines;
}
}  // namespace

// ================================================================================================ the arc option
// Ticket 0199 part 2 (research/05-graphics-composition/arrows-and-path-line.md 13): [Renderer] ServerArc. With 1, and
// only when this boot writes the server's render lines (as ServerUnits), server.cfg gets render_arc=1, the Lua bridge is
// asked for its arrows install (a wrapper of the InGame context's Events.SpawnArrowEvent and a RemoveAllArrowsEvent
// listener: the arc's end points), and OnBoot clears the EXE's Arrows view bit (0x400000) once the composite is loaded,
// so the player sees one arc, the server's. Otherwise nothing is written and one line says why. With 0 (the default)
// nothing changes. Programs without the bridge's glue get the default below (as RequestUnitPositions).
namespace ProxyUiBridgeRt
{
void RequestArrows();
}  // namespace ProxyUiBridgeRt
namespace ProxyShellDefault
{
void RequestArrows()
{
	ProxyRtInt::Log("shell: [Renderer] ServerArc = 1: the Lua bridge's glue is not linked into this program: no arrows "
	                "install");
}
}  // namespace ProxyShellDefault
#pragma comment(linker, "/alternatename:?RequestArrows@ProxyUiBridgeRt@@YAXXZ=?RequestArrows@ProxyShellDefault@@YAXXZ")

namespace
{
const char kArcLines[] = "render_arc=1\n";

std::string ArcLines(bool writesServerCfg, bool renderLines)
{
	g_cfg.arc = false;
	if (ProxyRtInt::ProxyOptions().ServerArc() == 0) return std::string();
	const char* why = NULL;
	if (g_cfg.option == 0)
		why = "[Renderer] ServerRenderer = 0 (the server draws the arc only with its renderer on)";
	else if (!writesServerCfg)
		why = "vpproxy.ini server_config= names the server's config (that config decides)";
	else if (!renderLines)
		why = "the server's renderer is off for this boot (the composite was refused: the shell: line above)";
	if (why)
	{
		ProxyRtInt::Log("shell: [Renderer] ServerArc = 1: no arc key in server.cfg, no arrows install, the Arrows view bit "
		                "kept: %s; the game draws its own arc", why);
		return std::string();
	}
	ProxyRtInt::Log("shell: [Renderer] ServerArc = 1: server.cfg gets render_arc=1; the Lua bridge's arrows install is "
	                "asked for; the EXE's Arrows view bit (0x400000) is cleared once the composite is loaded");
	ProxyUiBridgeRt::RequestArrows();
	g_cfg.arc = true;
	return std::string(kArcLines);
}
}  // namespace

// ================================================================================================ the path option
// Ticket 0300 part 2 (research/05-graphics-composition/path-models.md 7): [Renderer] ServerPath, as ServerArc above.
// With 1, and only when this boot writes the server's render lines, server.cfg gets render_path=1, the Lua bridge is
// asked for its path install (the UIPathFinderUpdate nodes with the EXE's DLL tests, DisplayMovementIndicator's show /
// hide), and OnBoot clears the EXE's MovementPath view bit (0x800000) once the composite is loaded (config.ini keeps
// the key on, so the system is started), so the player sees one set of markers, the server's. Otherwise nothing is
// written and one line says why. With 0 (the default) nothing changes. Programs without the bridge's glue get the
// default below.
namespace ProxyUiBridgeRt
{
void RequestPath();
}  // namespace ProxyUiBridgeRt
namespace ProxyShellDefault
{
void RequestPath()
{
	ProxyRtInt::Log("shell: [Renderer] ServerPath = 1: the Lua bridge's glue is not linked into this program: no path "
	                "install");
}
}  // namespace ProxyShellDefault
#pragma comment(linker, "/alternatename:?RequestPath@ProxyUiBridgeRt@@YAXXZ=?RequestPath@ProxyShellDefault@@YAXXZ")

namespace
{
const char kPathLines[] = "render_path=1\n";

std::string PathLines(bool writesServerCfg, bool renderLines)
{
	g_cfg.path = false;
	if (ProxyRtInt::ProxyOptions().ServerPath() == 0) return std::string();
	const char* why = NULL;
	if (g_cfg.option == 0)
		why = "[Renderer] ServerRenderer = 0 (the server draws the path's markers only with its renderer on)";
	else if (!writesServerCfg)
		why = "vpproxy.ini server_config= names the server's config (that config decides)";
	else if (!renderLines)
		why = "the server's renderer is off for this boot (the composite was refused: the shell: line above)";
	if (why)
	{
		ProxyRtInt::Log("shell: [Renderer] ServerPath = 1: no path key in server.cfg, no path install, the MovementPath "
		                "view bit kept: %s; the game draws its own path markers", why);
		return std::string();
	}
	ProxyRtInt::Log("shell: [Renderer] ServerPath = 1: server.cfg gets render_path=1; the Lua bridge's path install is "
	                "asked for; the EXE's MovementPath view bit (0x800000) is cleared once the composite is loaded");
	ProxyUiBridgeRt::RequestPath();
	g_cfg.path = true;
	return std::string(kPathLines);
}
}  // namespace

// ================================================================================================ the farm-field option
// Ticket 4853 (epic 0528 D, research/05-graphics-composition/farm-fields-layout.md 14): [Renderer] ServerFarmFields. With
// 1, and only when this boot writes the server's render lines, server.cfg gets render_farmfields=1 (the server lays out
// its own farm fields and tracks) and render_farmfields_keep_tap=0 (the decal tap's farm records are dropped), and OnBoot
// sets VPSHELL_FARM_BUILDER=0 in the EXE's environment before the composite is loaded: the composite's farm listener
// (shell/composite/farm_guard.cpp, ticket 0527) then never calls the EXE's farm builder. Otherwise nothing is written and
// one line says why. With 0 (the default) nothing changes.
namespace
{
const char kFarmLines[] = "render_farmfields=1\nrender_farmfields_keep_tap=0\n";

std::string FarmLines(bool writesServerCfg, bool renderLines)
{
	g_cfg.farm = false;
	if (ProxyRtInt::ProxyOptions().ServerFarmFields() == 0) return std::string();
	const char* why = NULL;
	if (g_cfg.option == 0)
		why = "[Renderer] ServerRenderer = 0 (the server draws the farm fields only with its renderer on)";
	else if (!writesServerCfg)
		why = "vpproxy.ini server_config= names the server's config (that config decides)";
	else if (!renderLines)
		why = "the server's renderer is off for this boot (the composite was refused: the shell: line above)";
	if (why)
	{
		ProxyRtInt::Log("shell: [Renderer] ServerFarmFields = 1: no farm-field keys in server.cfg, the EXE's farm builder "
		                "kept: %s; the game lays out its own farm fields", why);
		return std::string();
	}
	ProxyRtInt::Log("shell: [Renderer] ServerFarmFields = 1: server.cfg gets render_farmfields=1 "
	                "render_farmfields_keep_tap=0; the composite is told VPSHELL_FARM_BUILDER=0 (the EXE's farm builder "
	                "is not called once the composite is loaded)");
	g_cfg.farm = true;
	return std::string(kFarmLines);
}
}  // namespace

// ================================================================================================ the model options
// Ticket 4875 (epic 0561, research/05-graphics-composition/city-renderer.md 3.3 and 11): [Renderer] ServerCities (id 12)
// and ServerLandmarks (id 13). Each with 1, and only when this boot writes the server's render lines, adds its server.cfg
// line (render_cities=1, render_landmarks=1): the server's model module reads the compositor's tap, publishes its identity
// set and sets the mode word's bit 1 / bit 2 only while it can draw that class. The compositor needs no line of its own:
// its model_filter defaults to auto (the mode word's bits decide; ticket 4874). Otherwise nothing is written and one line
// says why. With 0 (the default) nothing changes.
namespace
{
std::string OneModelLine(int value, const char* option, const char* key, const char* what, bool writesServerCfg,
                         bool renderLines, bool& written)
{
	written = false;
	if (value == 0) return std::string();
	const char* why = NULL;
	if (g_cfg.option == 0)
		why = "[Renderer] ServerRenderer = 0 (the server draws models only with its renderer on)";
	else if (!writesServerCfg)
		why = "vpproxy.ini server_config= names the server's config (that config decides)";
	else if (!renderLines)
		why = "the server's renderer is off for this boot (the composite was refused: the shell: line above)";
	if (why)
	{
		ProxyRtInt::Log("shell: [Renderer] %s = 1: no %s key in server.cfg: %s; the game draws the %s", option, key, why, what);
		return std::string();
	}
	ProxyRtInt::Log("shell: [Renderer] %s = 1: server.cfg gets %s=1 (the server draws the %s it has; the compositor drops "
	                "the game's draws of them only while the mode word's bit says so)",
	                option, key, what);
	written = true;
	return std::string(key) + "=1\n";
}

std::string ModelLines(bool writesServerCfg, bool renderLines)
{
	const Vp64Opt::Options& o = ProxyRtInt::ProxyOptions();
	return OneModelLine(o.ServerCities(), "ServerCities", "render_cities", "cities", writesServerCfg, renderLines, g_cfg.cities) +
	       OneModelLine(o.ServerLandmarks(), "ServerLandmarks", "render_landmarks", "improvements, resources and natural wonders",
	                    writesServerCfg, renderLines, g_cfg.landmarks);
}
}  // namespace

// ================================================================================================ the API
namespace ProxyShell
{
// Issue 3305: [Renderer] ServerRenderer, the composite's check and the names (render=1, render_section=). The lines for
// server.cfg, or "" when the server's renderer is off for this boot.
static std::string ConfigureRender(bool writesServerCfg)
{
	ReleaseHeld();
	g_refused[0] = 0;
	g_cfg.configured = true;
	g_cfg.productComposite = false;
	g_cfg.compositeLoaded = false;
	g_cfg.compositePath[0] = 0;
	g_cfg.option = ProxyRtInt::ProxyOptions().ServerRenderer();
	g_cfg.helperMarker = GetFileAttributesA(Loader::MARKER_PATH) != INVALID_FILE_ATTRIBUTES;
	g_cfg.boot = 0;
	MakeNames(GetCurrentProcessId(), 1, g_cfg.base, sizeof(g_cfg.base), g_cfg.texture, sizeof(g_cfg.texture));
	if (g_cfg.option == 0)
	{
		ProxyRtInt::Log("shell: [Renderer] ServerRenderer = 0: no composite, no view-mask write, no render keys%s",
		                g_cfg.helperMarker ? " (the test switch crashlogs\\shell.helper is honoured at the boot)" : "");
		return std::string();
	}
	const char* refused = NULL;
	char detail[200] = "";
	if (g_cfg.helperMarker)
		ProxyRtInt::Log("shell: [Renderer] ServerRenderer = 1: the composite comes from the test switch "
		                "crashlogs\\shell.helper (no hash check)");
	else
	{
		refused = CheckComposite(detail, sizeof(detail));
		char path[MAX_PATH * 4];
		Narrow(g_cfg.compositePath, path, sizeof(path));
		if (refused)
		{
			ProxyRtInt::Log("shell: [Renderer] ServerRenderer = 1: composite %s REFUSED: %s%s%s%s; the game draws its own "
			                "terrain (the view-mask write keeps only the config rule)",
			                path[0] ? path : "(no path)", refused, detail[0] ? " (" : "", detail, detail[0] ? ")" : "");
			_snprintf_s(g_refused, sizeof(g_refused), _TRUNCATE, "composite %s REFUSED: %s%s%s%s", path[0] ? path : "(no path)",
			            refused, detail[0] ? " (" : "", detail, detail[0] ? ")" : "");  // ticket 0624: RefusedComposite
		}
		else
		{
			g_cfg.productComposite = true;
			ProxyRtInt::Log("shell: [Renderer] ServerRenderer = 1: composite %s sha256 %s matches "
			                "shell_composite.sha256 and this proxy's build; held until the load",
			                path, detail);
		}
	}
	if (!g_cfg.helperMarker && !g_cfg.productComposite) return std::string();
	// issue 3308: names of their own for every boot of this EXE process after the first
	g_cfg.boot = NextBoot();
	MakeNames(GetCurrentProcessId(), g_cfg.boot, g_cfg.base, sizeof(g_cfg.base), g_cfg.texture, sizeof(g_cfg.texture));
	if (g_cfg.boot > 1)
		ProxyRtInt::Log("shell: boot %lu of this EXE process: section base %s and texture base %s of its own", g_cfg.boot, g_cfg.base,
		                g_cfg.texture);
	if (!writesServerCfg)
	{
		ProxyRtInt::Log("shell: vpproxy.ini server_config= names the server's config: render=1 and render_section=%s are "
		                "NOT added (that config decides)",
		                g_cfg.base);
		return std::string();
	}
	ProxyRtInt::Log("shell: server.cfg gets render=1 render_section=%s render_handover=ring%s%s", g_cfg.base,
	                g_cfg.texture[0] ? " render_texture=" : "", g_cfg.texture);
	// issue 3308: the product's hand-over is the ring (epic 3300 scope 4: no keyed mutex, no copy in the EXE); the server's
	// own default stays v3 for direct runs and its tests. The composite's ini must say handover=ring too.
	std::string lines = std::string("render=1\nrender_section=") + g_cfg.base + "\nrender_handover=ring\n";
	if (g_cfg.texture[0]) lines += std::string("render_texture=") + g_cfg.texture + "\n";
	return lines;
}

const char* RefusedComposite() { return g_refused; }

std::string Configure(bool writesServerCfg)
{
	const std::string lines = ConfigureRender(writesServerCfg);
	return lines + UnitLines(writesServerCfg, !lines.empty())    // issue 4759: "" with ServerUnits = 0
	             + ArcLines(writesServerCfg, !lines.empty())     // ticket 0199: "" with ServerArc = 0
	             + PathLines(writesServerCfg, !lines.empty())    // ticket 0300: "" with ServerPath = 0
	             + FarmLines(writesServerCfg, !lines.empty())    // ticket 4853: "" with ServerFarmFields = 0
	             + ModelLines(writesServerCfg, !lines.empty());  // ticket 4875: "" with ServerCities = ServerLandmarks = 0
}

const FarmGuardHooks* g_farmGuardHooks = NULL;   // ticket 0535: set by proxy_farm_guard.cpp when it is linked
void (*g_serverHung)(const char* reason) = NULL;   // ticket 0691: set by proxy_rt.cpp when it is linked

// Ticket 0691: the composite's hung-server sink (its control thread, once per process).
static void __cdecl CompositeHungSink(const char* reason)
{
	const char* r = reason && *reason ? reason : "(no reason given)";
	ProxyRtInt::Log("shell: LOUD: the composite reports the server HUNG: %s (ticket 0691, D242)", r);
	if (g_serverHung)
		g_serverHung(r);
	else
		ProxyRtInt::Log("shell: no failure route linked (a test program): nothing more");
}

// Ticket 0697: the composite's last pacing lines before Fatal's TerminateProcess (proxy_shell.h FlushCompositeTotals).
typedef int(__cdecl * FlushTotalsFn)(const char* why, const char* detail, unsigned timeoutMs);
static FlushTotalsFn volatile g_flushTotals = NULL;   // set at the composite's load (OnBoot)
static volatile LONG g_flushLoaded = 0;               // 1 = OnBoot loaded a composite
static volatile LONG g_flushClaimed = 0;              // 1 = the one call of this process is made
struct FlushCall
{
	FlushTotalsFn fn;
	char why[32];
	char detail[480];
	unsigned timeoutMs;
	volatile LONG result;
};
static FlushCall g_flushCall;   // never on the stack: the thread may outlive the wait (the process ends at once anyway)

static DWORD WINAPI FlushTotalsThread(LPVOID)
{
	InterlockedExchange(&g_flushCall.result, g_flushCall.fn(g_flushCall.why, g_flushCall.detail, g_flushCall.timeoutMs));
	return 0;
}

void FlushCompositeTotals(const char* why, const char* detail, unsigned long waitMs)
{
	if (!g_flushLoaded || InterlockedCompareExchange(&g_flushClaimed, 1, 0) != 0) return;
	const FlushTotalsFn fn = g_flushTotals;
	if (!fn)
	{
		ProxyRtInt::Log("shell: the composite has no VPShell_FlushTotals (an older composite): no pacing totals line at this end "
		                "(ticket 0697)");
		return;
	}
	g_flushCall.fn = fn;
	_snprintf_s(g_flushCall.why, sizeof(g_flushCall.why), _TRUNCATE, "%s", why && *why ? why : "fatal");
	_snprintf_s(g_flushCall.detail, sizeof(g_flushCall.detail), _TRUNCATE, "%s", detail ? detail : "");
	// the composite bounds its own lock waits by half the wait, so it gives up before the proxy does
	g_flushCall.timeoutMs = (unsigned)(waitMs / 2);
	g_flushCall.result = -1;
	const double t0 = NowMs();
	HANDLE t = CreateThread(NULL, 0, FlushTotalsThread, NULL, 0, NULL);
	if (!t)
	{
		ProxyRtInt::Log("shell: the composite's totals: no thread (CreateThread error %lu): skipped, no pacing totals line "
		                "(ticket 0697)", GetLastError());
		return;
	}
	const DWORD w = WaitForSingleObject(t, waitMs);
	const double ms = NowMs() - t0;
	if (w == WAIT_OBJECT_0)
		ProxyRtInt::Log("shell: the composite's last pacing lines (why=%s): VPShell_FlushTotals returned %ld in %.1f ms "
		                "(ticket 0697)", g_flushCall.why, (long)g_flushCall.result, ms);
	else
		ProxyRtInt::Log("shell: LOUD: the composite's VPShell_FlushTotals did not return within %lu ms (%.1f ms waited): the "
		                "process ends without its pacing totals line (ticket 0697)", waitMs, ms);
	CloseHandle(t);
}

// ticket 0535: the farm builder guard (proxy_farm_guard.h) with 4855's rule 2: attaches the guard's shared page, reads
// [composite] farm_guard / farm_builder of shell_composite.ini beside the proxy and starts its watcher. From OnBoot
// (before the composite's LoadLibrary) and, ticket 0635 (D225), from OnBootOff.
static void InstallFarmGuard()
{
	wchar_t folder[MAX_PATH * 2], self[MAX_PATH * 2], ini[MAX_PATH * 2], out[MAX_PATH * 2];
	ini[0] = out[0] = 0;
	if (OwnFolder(folder, MAX_PATH * 2, self, MAX_PATH * 2))
	{
		_snwprintf_s(ini, MAX_PATH * 2, _TRUNCATE, L"%sshell_composite.ini", folder);
		// the record's folder: as the composite's (VPSHELL_OUT_DIR, else <user>\Logs for a proxy under MODS, issue 0148)
		const DWORD n = GetEnvironmentVariableW(L"VPSHELL_OUT_DIR", out, MAX_PATH * 2);
		if (n == 0 || n >= MAX_PATH * 2)
		{
			out[0] = 0;
			if (!OutDirFor(self, out, MAX_PATH * 2)) wcsncpy_s(out, MAX_PATH * 2, folder, _TRUNCATE);
		}
	}
	if (g_farmGuardHooks) g_farmGuardHooks->install(out, ini);
	else ProxyRtInt::Log("shell: LOUD: the farm builder guard is not linked into this program (ticket 0535): not installed");
}

void OnBootOff()
{
	// ---- ticket 0635 (D225): the farm builder guard with the patch switched off. It works on the EXE's own farm builder
	// (the listener of queue B's type 59) and its graph, not on the DLL, so the player's own 32-bit DLL changes nothing
	// for it. No composite is loaded in off mode, so farm_builder = 0 does not act (the builder is always called
	// through the guard).
	ProxyRtInt::Log("shell: the patch is switched off: the farm builder guard is installed as on a 64-bit start "
	                "(ticket 0635)");
	InstallFarmGuard();
	if (g_farmGuardHooks) g_farmGuardHooks->setComposite(false);
}

void OnBoot()
{
	// ---- ticket 0492: the guard of the EXE's 4 MB UI message queue (proxy_queue_guard.h), before any save can load
	ProxyQueueGuard::Install();

	// ---- ticket 0720: the EXE's global keyboard hook off its main thread (proxy_kbhook.h); this is the main thread
	ProxyKbHook::Install();

	// ---- ticket 0535: the farm builder guard, on every 64-bit start and before the composite's LoadLibrary (the
	// composite's copy then finds the owner's page and installs nothing)
	InstallFarmGuard();

	// ---- the composite
	bool loaded = false;
	if (g_cfg.option == 1 && (g_cfg.helperMarker || g_cfg.productComposite))
	{
		wchar_t wbase[64];
		MultiByteToWideChar(CP_ACP, 0, g_cfg.base, -1, wbase, 64);
		if (SetEnvironmentVariableW(L"VPSHELL_SECTION", wbase))
			ProxyRtInt::Log("shell: VPSHELL_SECTION=%s set in the EXE's environment for the composite", g_cfg.base);
		else
			ProxyRtInt::Log("shell: VPSHELL_SECTION could not be set (error %lu)", GetLastError());
		// ticket 4853: the server draws the farm fields, so the composite's farm listener skips the EXE's builder (the
		// composite reads [composite] farm_builder from VPSHELL_FARM_BUILDER first)
		if (g_cfg.farm)
		{
			if (SetEnvironmentVariableW(L"VPSHELL_FARM_BUILDER", L"0"))
				ProxyRtInt::Log("shell: VPSHELL_FARM_BUILDER=0 set in the EXE's environment: the composite does not call the "
				                "EXE's farm builder ([Renderer] ServerFarmFields = 1)");
			else
				ProxyRtInt::Log("shell: VPSHELL_FARM_BUILDER could not be set (error %lu): the EXE's farm builder stays on "
				                "while the server draws its own fields", GetLastError());
		}
		// ticket 0784 (D268): [Renderer] HideTreadEffect = 1 (default) with ServerUnits = 1: the farm guard's owner drops the
		// EXE's two landship tread effects (it reads [composite] tread_filter from VPSHELL_TREAD_FILTER first)
		if (ProxyRtInt::ProxyOptions().ServerUnits() != 0)
		{
			const bool hide = ProxyRtInt::ProxyOptions().HideTreadEffect() != 0;
			if (SetEnvironmentVariableW(L"VPSHELL_TREAD_FILTER", hide ? L"1" : L"0"))
				ProxyRtInt::Log("shell: VPSHELL_TREAD_FILTER=%d set in the EXE's environment: [Renderer] HideTreadEffect = %d, so the "
				                "EXE's landship tread effect is %s", hide ? 1 : 0, hide ? 1 : 0,
				                hide ? "dropped (ticket 0784)" : "drawn by the game");
			else
				ProxyRtInt::Log("shell: VPSHELL_TREAD_FILTER could not be set (error %lu): the EXE's tread effect is drawn by the game",
				                GetLastError());
		}
	}
	if (g_cfg.option == 1 && g_cfg.productComposite && !g_cfg.helperMarker)
	{
		// issue 0148: the composite's log and records go outside the EXE's watched MODS tree (a value already in the EXE's
		// environment, set by a test, is kept)
		wchar_t have[8];
		wchar_t dir[MAX_PATH * 2];
		char path[MAX_PATH * 4];
		if (GetEnvironmentVariableW(L"VPSHELL_OUT_DIR", have, 8) > 0)
			ProxyRtInt::Log("shell: VPSHELL_OUT_DIR already set in the EXE's environment: kept");
		else if (!OutDirFor(g_cfg.compositePath, dir, MAX_PATH * 2))
			ProxyRtInt::Log("shell: the composite is not under a MODS folder: it writes its log and records beside itself");
		else
		{
			Narrow(dir, path, sizeof(path));
			if (SetEnvironmentVariableW(L"VPSHELL_OUT_DIR", dir))
				ProxyRtInt::Log("shell: VPSHELL_OUT_DIR=%s set: the composite's log and records go there, outside the MODS tree "
				                "the EXE watches (issue 0148)",
				                path);
			else
				ProxyRtInt::Log("shell: VPSHELL_OUT_DIR could not be set (error %lu)", GetLastError());
		}
	}
	if (g_cfg.helperMarker || (!g_cfg.configured && GetFileAttributesA(Loader::MARKER_PATH) != INVALID_FILE_ATTRIBUTES))
	{
		ReleaseHeld();
		loaded = Loader::LoadFromMarker();
	}
	else if (g_cfg.option == 1 && g_cfg.productComposite)
	{
		char path[MAX_PATH * 4];
		Narrow(g_cfg.compositePath, path, sizeof(path));
		const double t0 = NowMs();
		HMODULE h = LoadLibraryW(g_cfg.compositePath);  // the checked file is still held without write sharing
		const DWORD err = GetLastError();
		ReleaseHeld();
		if (h)
			ProxyRtInt::Log("shell: composite %s loaded at 0x%08lx (%.1f ms)", path, reinterpret_cast<unsigned long>(h),
			                NowMs() - t0);
		else
			ProxyRtInt::Log("shell: composite %s FAILED to load: error %lu; the game draws its own terrain", path, err);
		loaded = h != NULL;
		if (h)   // ticket 0691: the hung-server sink
		{
			// ticket 0697: the composite's last pacing lines at a Fatal (FlushCompositeTotals); known before the sink is passed,
			// so a hung report that comes at once still finds it
			g_flushTotals = (FlushTotalsFn)GetProcAddress(h, "VPShell_FlushTotals");
			InterlockedExchange(&g_flushLoaded, 1);
			ProxyRtInt::Log(g_flushTotals ? "shell: the composite has VPShell_FlushTotals: a fatal end writes its pacing totals (ticket 0697)"
			                              : "shell: the composite has no VPShell_FlushTotals (an older composite): a fatal end has no pacing totals line");
			typedef void(__cdecl * SetHungSinkFn)(Test::HungSinkFn);
			SetHungSinkFn set = (SetHungSinkFn)GetProcAddress(h, "VPShell_SetHungSink");
			if (set) set(&CompositeHungSink);
			ProxyRtInt::Log(set ? "shell: hung-server sink passed to the composite (ticket 0691)"
			                    : "shell: the composite has no VPShell_SetHungSink (an older composite): a hung server reaches no failure route");
		}
	}
	g_cfg.compositeLoaded = loaded;
	if (g_farmGuardHooks) g_farmGuardHooks->setComposite(loaded);   // ticket 0535: farm_builder = 0 acts only with the composite (as with 4853)

	// ---- the view-mask write: the product rule with ServerRenderer = 1, or the test switch (9040's marker)
	DWORD keep = 0, clear = 0;
	const bool marker = GetFileAttributesA(Mask::MARKER_PATH) != INVALID_FILE_ATTRIBUTES && Mask::ReadMarker(&keep, &clear);
	const DWORD exeBase = reinterpret_cast<DWORD>(GetModuleHandleA(NULL));
	if (g_cfg.option == 1 || marker)
	{
		const bool terrain = g_cfg.option == 1 && loaded;
		Mask::g_bArrows = g_cfg.option == 1 && loaded && g_cfg.arc;   // ticket 0199: the server draws the arc
		if (g_cfg.arc && !Mask::g_bArrows)
			ProxyRtInt::Log("shell: [Renderer] ServerArc = 1 but the composite is not loaded: the Arrows view bit is kept "
			                "(the game draws its own arc)");
		Mask::g_bPath = g_cfg.option == 1 && loaded && g_cfg.path;   // ticket 0300: the server draws the path's models
		if (g_cfg.path && !Mask::g_bPath)
			ProxyRtInt::Log("shell: [Renderer] ServerPath = 1 but the composite is not loaded: the MovementPath view bit is "
			                "kept (the game draws its own path markers)");
		Mask::InstallAt(exeBase, g_cfg.option == 1, terrain, keep, clear, marker, true);
	}

	// ---- the white fog mask (issue 0172): [Renderer] WhiteFogMask, independent of ServerRenderer
	// ticket 0339: with the composite loaded and its model_fow on, the cloud base layer as well
	White::Boot(exeBase, ProxyRtInt::ProxyOptions().WhiteFogMask(), NULL, loaded && White::ModelFogWanted(g_cfg.compositePath));
}

void OnUpdate()
{
	Mask::OnUpdate();
	White::OnUpdate(false);
	ProxyQueueGuard::OnUpdate();  // ticket 0492: the drop report
	ProxyKbHook::OnUpdate();      // ticket 0720: what changed, at most one line per 10 s
}

void OnUnload()
{
	Mask::StopWatcher();
	ReleaseHeld();
	ProxyQueueGuard::OnUnload();  // ticket 0492: the import slot's previous value back (the thunk is in this image)
	ProxyKbHook::OnUnload();      // ticket 0720: both import slots back, the helper's hook removed, its thread ended
	if (g_farmGuardHooks) g_farmGuardHooks->unload();   // ticket 0535: the watcher stopped, the hook stub returns at once until the next boot
}

namespace Test
{
bool ParseManifest(const char* text, size_t len, unsigned char proxySha[32], unsigned char compositeSha[32],
                   const char** why)
{
	return ParseManifestText(text, len, proxySha, compositeSha, why);
}
const char* VerifyExe(const void* base)
{
	return Mask::VerifyExe(reinterpret_cast<DWORD>(base));
}
size_t PatternRows()
{
	return sizeof(Mask::CODE_PATTERNS) / sizeof(Mask::CODE_PATTERNS[0]) +
	       sizeof(White::PATTERNS) / sizeof(White::PATTERNS[0]) + sizeof(White::BASE_PATTERNS) / sizeof(White::BASE_PATTERNS[0]);
}
bool PatternRow(size_t i, int* group, int* sym, const char** name, const char** bytes)
{
	const size_t nMask = sizeof(Mask::CODE_PATTERNS) / sizeof(Mask::CODE_PATTERNS[0]);
	const size_t nWhite = sizeof(White::PATTERNS) / sizeof(White::PATTERNS[0]);
	const Mask::CodePattern* row = NULL;
	if (i < nMask)
		row = &Mask::CODE_PATTERNS[i], *group = 0;
	else if (i < nMask + nWhite)
		row = &White::PATTERNS[i - nMask], *group = 1;
	else if (i < PatternRows())
		row = &White::BASE_PATTERNS[i - nMask - nWhite], *group = 2;
	if (!row) return false;
	*sym = row->iSym;
	*name = row->szName;
	*bytes = row->szBytes;
	return true;
}
bool InstallAt(const void* base, bool terrain, unsigned long keep, unsigned long clear, bool watcher)
{
	return Mask::InstallAt(reinterpret_cast<DWORD>(base), true, terrain, keep, clear, false, watcher);
}
void Update()
{
	Mask::OnUpdate();
}
void Reset()
{
	Mask::Reset();
	White::Reset();
	ReleaseHeld();
	g_cfg.configured = false;
	g_cfg.option = 0;
	g_cfg.helperMarker = g_cfg.productComposite = g_cfg.compositeLoaded = false;
	g_cfg.boot = 0;
	g_cfg.arc = false;   // ticket 0199
	g_cfg.path = false;  // ticket 0300
	SetEnvironmentVariableA(VP64SHELL_BOOT_ENV, NULL);   // issue 3308: the next Configure is a first boot again
}
unsigned long Boot()
{
	return g_cfg.boot;
}
bool OutDir(const wchar_t* dllPath, wchar_t* out, size_t cap)
{
	return OutDirFor(dllPath, out, cap);
}
void Names(unsigned long pid, unsigned long boot, char* base, size_t baseCap, char* texture, size_t textureCap)
{
	MakeNames(pid, boot, base, baseCap, texture, textureCap);
}
unsigned long WatcherPolls()
{
	return static_cast<unsigned long>(Mask::g_watchPolls);
}
unsigned long WatcherFaults()
{
	return Mask::g_watchFaults;
}
unsigned long Writes()
{
	return Mask::g_dwWrites;
}
const char* WhiteVerify(const void* base)
{
	return White::Verify(reinterpret_cast<DWORD>(base));
}
bool WhiteBoot(const void* base, int option, WhiteLoaderFn loader)
{
	White::Boot(reinterpret_cast<DWORD>(base), option, reinterpret_cast<White::LoaderFn>(loader), false);
	return White::g_active != 0;
}
bool WhiteBootModelFog(const void* base, int option, WhiteLoaderFn loader)
{
	White::Boot(reinterpret_cast<DWORD>(base), option, reinterpret_cast<White::LoaderFn>(loader), true);
	return White::g_active != 0;
}
unsigned long WhiteBase()
{
	return White::g_bBaseFailed ? 0 : White::g_wBase;
}
unsigned long WhiteBasePatched()
{
	return White::g_dwBasePatched;
}
void WhiteUpdate(bool force)
{
	White::OnUpdate(force);
}
unsigned long WhitePasses()
{
	return White::g_dwPasses;
}
unsigned long WhitePatched()
{
	return White::g_dwPatched;
}
HungSinkFn HungSink()   // ticket 0691
{
	return &CompositeHungSink;
}
unsigned long WhiteHandle()
{
	return White::g_active ? White::g_wHandle : 0;
}
}  // namespace Test
}  // namespace ProxyShell
