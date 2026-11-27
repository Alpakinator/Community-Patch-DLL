// proxy_pack.h - the DLL pack and the run-time match (epic 3500, issue 3504; contract tools/release/README.md
// section 5). Hand-written. SPDX-License-Identifier: GPL-3.0-or-later
//
// The pack is the x64 folder of the D28 layout (vpproxy.ini x64_dir=, default <proxy dir>\VP64): beside server.exe and
// the x64 DLL that the server loads it holds
//   manifest.json   {"format": 1, "release": "...",
//                    "x86_dlls": [{"sha256": "<64 hex>", "origin": "..."}, ...],
//                    "x64_dll": {"file": "CvGameCore_Expansion2.dll", "sha256": "<64 hex>"},
//                    "purity": {"file": "purity.tsv", "sha256": "<64 hex>"},
//                    "built": {"fork_commit": "...", "tools_commit": "..."}}       (written by tools/release/pack.py)
//   purity.tsv      the purity lists bound to the x64 DLL (issue 3503, README section 4)
// x86_dlls lists every 32-bit DLL the pack fits (MC ruling 2026-11-13 22:20, thread #203: the release tag's committed
// DLL and the DLL of the release's published installer or modpack), each with where it was read; the player's Vox
// Populi DLL is kept beside the proxy as fallback_dll and must have one of these hashes. With vpproxy.ini pack=1 the boot (proxy_rt.cpp) checks the pack before the server starts; any
// failure boots that 32-bit DLL instead (the mode=off path) and says so once (one log line, the window-title notice).
// No engine, no vpipc: proxy/tools/test_proxy_pack.py unit-tests this file alone (rt_tests/pack_unit.cpp).
// Plain C++ for the VC9 headers (C++11 at most), like the rest of the proxy. bcrypt and user32 are loaded by name:
// the proxy's import list (kernel32, lua51_Win32, msvcr90, msvcp90) is checked by build_proxy.py.
#pragma once
// The STL layout must match the other proxy TUs (see proxy_rt_logic.h).
#if defined(_MSC_VER) && !defined(_SECURE_SCL)
#define _SECURE_SCL 0
#endif
#if defined(_MSC_VER) && !defined(_HAS_ITERATOR_DEBUGGING)
#define _HAS_ITERATOR_DEBUGGING 0
#endif
#include <windows.h>
#include <string>
#include <vector>

namespace ProxyPack
{
#define VP64PACK_MANIFEST "manifest.json"
#define VP64PACK_FORMAT 1

enum Status
{
	kOk = 0,
	kNoPack,         // <dir>\manifest.json does not exist (or the folder does not)
	kUnreadable,     // it exists but cannot be read, or is larger than 64 KiB
	kBadJson,        // not a JSON object
	kUnknownFormat,  // "format" is not 1
	kMissingField,   // a required field is missing or has the wrong JSON type
	kBadField,       // a field has a bad value (not 64 hex digits; a file name with a path in it)
	kNoFallback,     // the 32-bit DLL (fallback_dll) cannot be read: nothing to compare the manifest with
	kOtherDll,       // fallback_dll's SHA-256 is none of the manifest's x86_dlls
	kFileMissing,    // the x64 DLL or the purity file named by the manifest cannot be opened
	kHashMismatch    // the x64 DLL's or the purity file's SHA-256 is not the manifest's
};
const char* StatusName(Status s);

struct Manifest
{
	int format;
	std::string release, x64File, x64Sha, purityFile, puritySha, forkCommit, toolsCommit;  // hashes lower-case
	std::vector<std::string> x86Shas, x86Origins;  // x86_dlls[i].sha256 (lower-case) and .origin, at least one
	Manifest() : format(0) {}
};

// Parses a manifest's text: format first (kUnknownFormat for any number other than 1, kMissingField without one),
// then the required fields (release, x86_dlls: a non-empty array of at most 16 objects {sha256, origin}, x64_dll.file,
// x64_dll.sha256, purity.file, purity.sha256; built.* optional). why = one line naming the field or the JSON position.
Status ParseManifest(const std::string& text, Manifest& m, std::string& why);

// SHA-256 of a file, lower-case hex. keep != NULL: the file stays open without write sharing and *keep receives the
// handle (the caller closes it); else it is closed. False with why on any failure (why starts with "cannot open" when
// the file could not be opened).
bool Sha256File(const std::string& path, std::string& hex, std::string& why, HANDLE* keep);

struct Result
{
	Status status;
	std::string why;        // one line: what was checked and what failed (or "ok ...")
	std::string notice;     // the short text of the player's notice (empty on kOk)
	Manifest m;
	std::string dir, x64Path, purityPath, fallbackSha, x86Origin;  // x86Origin: the matching x86_dlls item's origin
	double ms;              // time of the whole check
	HANDLE heldX64, heldPurity;  // on kOk: open without write sharing until Release()
	Result() : status(kNoPack), ms(0.0), heldX64(INVALID_HANDLE_VALUE), heldPurity(INVALID_HANDLE_VALUE) {}
};

// Checks the pack in `dir` against the 32-bit DLL `fallbackDll`. In this order: the manifest (kNoPack, kUnreadable,
// the ParseManifest codes), the 32-bit DLL's hash (kNoFallback, kOtherDll), the x64 DLL's and the purity file's
// (kFileMissing, kHashMismatch). On kOk the x64 DLL and the purity file are held open (Result::held*).
Status Check(const std::string& dir, const std::string& fallbackDll, Result& r);

// Closes what Check holds (each handle at most once; safe to call again).
void ReleaseX64(Result& r);
void ReleasePurity(Result& r);

// The player's notice: appends " - <text>" to the title of this process's main window (the largest visible top-level
// window without an owner; IME windows excluded), with WM_SETTEXT under SendMessageTimeout (1 s, abort if hung). It
// activates nothing and takes no focus. A title that already carries the marker is not extended again. Returns one
// line for the log (what was done, or why not: no window, the send failed).
// Ticket 0680: stripPrefix (UTF-8, may be empty) is the options file's [Shell] WindowTitlePrefix: a title that starts
// with it (a 64-bit boot earlier in this process put it there) loses it first, so the notice title is the game's own
// title with the suffix.
std::string ShowNotice(const std::string& text, const std::string& stripPrefix = std::string());
// The marker every notice starts with (the title suffix is " - " VP64PACK_NOTICE_MARK ...).
#define VP64PACK_NOTICE_MARK "VP64 64-bit patch OFF"

// ================================================================================================ the title prefix
// Ticket 0680: while the patch runs (a good mode=on boot), the game window's title carries the options file's [Shell]
// WindowTitlePrefix (default "[VP64] "): "[VP64] Sid Meier's Civilization V (DX11)". No code write to the EXE (D228):
// the window's own WM_SETTEXT, as the notice above.

// The title rule. The title the window should carry, given its current title: prefix + current, except when the prefix
// is empty (the current title: the option's "the EXE's title unchanged") or when current already starts with the prefix
// (no double prefix). *changed (may be NULL) = the result differs from current.
std::wstring PrefixedTitle(const std::wstring& current, const std::wstring& prefix, bool* changed);
// current without a leading prefix (unchanged when it does not start with it, or the prefix is empty).
std::wstring UnprefixedTitle(const std::wstring& current, const std::wstring& prefix);
// UTF-8 -> UTF-16 (an empty result for invalid UTF-8).
std::wstring Widen(const std::string& utf8);

// The window the prefix goes on: this process's top-level window of the EXE's class VP64PACK_GAME_CLASS without an owner
// (the largest when there are several; visible or not: a minimised or not yet shown window gets its title too). Never a
// dialog box (class #32770): the harness's box watcher reads box titles. NULL when there is none.
#define VP64PACK_GAME_CLASS "WinClass_FXS"
HWND FindGameWindow();

// The title keeper: a thread of its own that every everyMs (1000 in the product) finds the game window and, when its
// title does not start with the prefix (the first time, and again whenever the EXE has set its own title back), sets
// PrefixedTitle with WM_SETTEXT under SendMessageTimeoutW (250 ms, abort if hung): the window's own thread does the
// change at its next message check; the keeper never blocks the game and activates nothing. The title is read with
// InternalGetWindowText (no message). log (may be NULL) gets one line per change and per failure (at most 3 failures);
// it is called from the keeper's thread. An empty prefix starts nothing. Returns one line for the boot's log.
typedef void (*TitleLogFn)(const char* line);
std::string StartTitleKeeper(const std::string& prefixUtf8, unsigned everyMs, TitleLogFn log);
// Stops the keeper (safe without one, and under the loader lock: it logs nothing): waits up to 1.5 s for the tick in
// progress, then ends the thread. The title stays as it is.
void StopTitleKeeper();
// One line: ticks, changes (the first and the re-applications), failures.
std::string TitleKeeperStatus();
}  // namespace ProxyPack
