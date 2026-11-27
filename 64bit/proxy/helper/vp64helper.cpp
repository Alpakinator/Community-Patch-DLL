// vp64helper.exe: the config.ini switch and the error popup of the Vox Populi 64-bit patch (issue 3520; decisions D118,
// D119, D120, D122, D125). Built by proxy/build_proxy.py step 5b (x86, static CRT, imports only kernel32, user32, gdi32,
// shell32) and shipped beside the proxy DLL. No network code (D120).
//
//   vp64helper on     [--config <config.ini>] [--options <vpproxy.ini>] [--vp64-options <VP64Options.ini>]
//   vp64helper off    [--config <config.ini>] [--options <vpproxy.ini>] [--vp64-options <VP64Options.ini>]
//   vp64helper status [--config <config.ini>] [--options <vpproxy.ini>] [--vp64-options <VP64Options.ini>]
//   vp64helper failed --pid <EXE pid> --report <dir> [--options <vpproxy.ini>] [--config <config.ini>]
//                     [--vp64-options <VP64Options.ini>] [--deadline-s N] [--no-ui] [--no-ui-choice open|remove|close]
//   vp64helper popup  --report <dir> [--options <vpproxy.ini>] [--no-ui] [--no-ui-choice open|close]
//   vp64helper visual-additions on|off|status [--vp64-options <VP64Options.ini>] [--config <config.ini>]
//
// visual-additions (ticket 4931; docs/extras/flags-and-messages-design.md 1.7): the installer's one line, [Extras]
//         EnableVisualAdditions = 1 (on) or 0 (off) in VP64Options.ini, the player's choice: no side file, and on/off
//         above never touch it. status prints the value the patch's parser takes and whether a line decides it. Exit 0
//         ok, 1 failed (nothing changed), 2 usage.
//
// --config defaults to cfgini::ConfigIniPath() (VPPROXY_TEST_CONFIG_INI, C5_DOCUMENTS, else Documents), --options to
// vpproxy.ini beside this EXE, --vp64-options to cfgini::OptionsIniPath() (VP64_OPTIONS, else VP64Options.ini beside
// config.ini: the proxy's own path rule). vpproxy.ini is edited in place (one key's line; every other byte kept; temp +
// rename).
//
// The one switch (issue 0030, the MC's ruling of 2026-11-16): on and off set the whole shell configuration, config.ini's
// D33 keys and VP64Options.ini's [Renderer] ServerRenderer together (proxy_cfgini.h; installer-notes.md 9).
//
// on      (the installer) ServerRenderer = 1 in VP64Options.ini, config.ini's D33
//         keys on in the A'' form (proxy_cfgini.cpp), then mode=on and no failed= line in vpproxy.ini. vpproxy.ini must
//         exist first: config.ini is never left in D33 while the proxy would boot the 32-bit DLL. Exit 0 ok, 1 refused
//         (nothing changed).
// off     (the off switch; the failed popup's "Remove 64bit patch", D240) config.ini back to the player's values, then
//         VP64Options.ini, then mode=off. A config.ini
//         that cannot be restored leaves everything else as it is (D33 with the 32-bit DLL would draw no terrain); a
//         VP64Options.ini that cannot be restored still gives mode=off, with exit 1. Exit 0 ok, 1 failed. Without a
//         usable side file (on never ran, or the file was lost or damaged) the file is left as it is and the output
//         line says so (issue 0378: a key at 1 may be the player's own; nothing is reset to 0).
// status  prints config.ini's and VP64Options.ini's states and the keys that differ, and mode / failed from vpproxy.ini.
//         Exit 0 off, 3 on (both files), 4 partial (or the two files differ), 5 no config.ini (or no [DisableSystems]).
// failed  started by the proxy (issue 3523) after it has written the report folder, when the 64-bit path fails in a
//         D33 session: waits until the EXE (--pid) has ended (S0, release-driver.md W3c.2: the EXE writes config.ini only
//         at its start, so a restore after its end stays), then the popup. The patch STAYS ON (D240, ticket 0682):
//         nothing is restored or set, the next start uses the 64-bit patch again, and Close keeps it so. The popup's
//         opt-in button "Remove 64bit patch" runs off (config.ini and VP64Options.ini back, mode=off); its result is
//         added to the popup and to helper.log. Two stages keep the switch-off of before D240 (off's config.ini and
//         VP64Options.ini parts, then failed=1 in vpproxy.ini, which the proxy reads as mode=off), with no Remove button
//         (the patch is off already): off-switch (ticket 0635: the patch was switched off) and exe-build (ticket 0588:
//         an EXE build this patch does not support, where every start would end with no play). Stage fallback-dll (ticket
//         0721: the 32-bit DLL could not be loaded, so the proxy ended the game rather than hand the EXE no context; also
//         in a stock session) changes nothing and has no Remove button: its words ask for a new installation. Every step is logged to
//         <report>\helper.log. The EXE not ended within --deadline-s (default 180): nothing is restored and nothing is
//         set, no Remove button, the popup says so (exit 3). config.ini not restorable (a switch-off stage): failed= is
//         not written (exit 4). Exit 0 done, 1 the report folder is missing (the rest done), 2 usage. A Remove that fails
//         does not change the exit code (the popup and helper.log say what failed).
// popup   a failure in a stock session (D125: config.ini was not ours): the popup only, nothing restored or set. Its
//         first line follows the report's stage (issue 0361; ReportStage): at the boot (pack, server-start, handshake)
//         the game goes on as the 32-bit game; after a server death in a game (server-death) the proxy ended the EXE
//         (on_server_death=exit), so the line says the game had to end; the server refused a save of the other DLL
//         variant (save-variant, ticket 0354): the save cannot be loaded with the installed version, so the game had
//         to end; the EXE itself crashed (exe-crash, ticket 0418: the server wrote the report and started the popup):
//         the game crashed, not the server; the DLL in the server stopped on a failed check (dll-fatal, ticket 0422: the
//         server wrote the report and started the popup): the game stopped on an internal check in the game's DLL.
//         Exit 0, 1 report folder missing, 2 usage.
//
// The popup: (ticket 0511) first the server's own reason when report.txt's [failure] names one ("server's reason: ",
// proxy_report.cpp ServerReason), what happened, what the report holds (the first line of <report>\popup-summary.txt
// when the proxy wrote one, after a server's reason as "Details: <that line>", else a fixed line), a line when the folder holds a *.dmp, the report's path, the hand-in line (handin= of
// vpproxy.ini, else the repository's new-issue page, D122) and that issues there are public (D122). Buttons "Open
// folder", "Remove 64bit patch" (failed only, when the patch stayed on; D240) and "Close". --no-ui prints it as
// "popup: ..." lines and opens no window: the only form tests use on the workstation (docs/briefs/COMMON.md rule 14);
// --no-ui-choice says which button the printed popup "presses" (open prints the folder; remove runs off and prints
// "remove: ..." lines; close keeps the patch as it is).
// Knowledge base: research/06-tooling-and-workflows/release-driver.md W3c.
#include "proxy_cfgini.h"
#include "proxy_session_keys.h"  // proxy/src: the build and the tests pass /I proxy/src

#include <windows.h>
#include <shellapi.h>

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include <string>
#include <vector>

namespace
{
const wchar_t* const kTitle = L"Vox Populi 64-bit patch: error report";
const wchar_t* const kDefaultHandin =
	L"Hand it in as a new issue with the report attached: https://github.com/LoneGazebo/Community-Patch-DLL/issues/new";
const wchar_t* const kPublic =
	L"Issues there are public: look the report over before you attach it, and leave out anything you do not want to share.";
const wchar_t* const kDefaultSummary =
	L"The report holds the patch's version and file checks, the step that failed and its error, the patch's logs and "
	L"options, your game's display and graphics settings, and system details (Windows version, graphics card and driver, "
	L"memory). It holds no saves, no user or computer names and no Steam id.";
const wchar_t* const kDump =
	L"It also holds a memory dump of the patch's server process, which contains the state of the game you were playing.";
const wchar_t* const kReasonHead = L"The game's 64-bit server stopped with this error: ";   // ticket 0511
const char* const kStageOffSwitch = "off-switch";   // ticket 0635: proxy_rt.cpp OffD33NoPlay's stage
const char* const kStageExeBuild = "exe-build";     // ticket 0588: proxy_rt.cpp's EXE-build gate (an unsupported build)
const char* const kStageSaveMods = "save-mods";     // ticket 0681: proxy_rt.cpp, the server's code save-mod-mismatch
const char* const kStageFallbackDll = "fallback-dll";  // ticket 0721: proxy_rt.cpp BootOff, the 32-bit DLL not loaded
// Ticket 0681: the popup's first line for a save made with other mods (the server's summary line follows it); nothing
// technical before it (MC #10855): the server's reason goes after the summary as its details.
const wchar_t* const kSaveModsHead =
	L"This save was made with a different set of mods than the mods active now, so the game could not load it; this is "
	L"not a failure of the 64-bit patch, which stays on.";
const wchar_t* const kRemoveLabel = L"Remove 64bit patch";   // D240 (ticket 0682): the failed popup's opt-in off switch
// D240 (ticket 0682, MC #10736): the exe-build stage keeps the switch-off, and its popup says why.
const wchar_t* const kExeBuildHead =
	L"The 64-bit part of the patch failed, so the game was ended. This version of the patch does not support this build "
	L"of the game, so the patch could not stay on: every start would end the same way.";
const DWORD kDefaultDeadlineS = 180;

// Ticket 0811: how to put config.ini right by hand when the side file is lost (the Remove button's line and stage
// off-switch's). On writes 1 into all our keys; the game's own values are 0 for all but the stock-off pair, which the
// game ships at 1 (proxy_session_keys.h). Until 2026-11-26 the text told the player to set every key to 1, which left
// six of the game's render systems off in the plain game. Built from the one key list so that it follows it.
std::wstring ConfigByHandText()
{
	static const char* const keys[] = {VP64_D33_KEYS_A2};
	static const char* const stockOff[] = {VP64_STOCK_OFF_KEYS};
	std::vector<std::wstring> zero, one;
	for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); ++k)
	{
		bool so = false;
		for (size_t j = 0; j < sizeof(stockOff) / sizeof(stockOff[0]); ++j)
			if (!strcmp(keys[k], stockOff[j])) so = true;
		std::wstring w;
		for (const char* c = keys[k]; *c; ++c) w += (wchar_t)(unsigned char)*c;
		(so ? one : zero).push_back(w);
	}
	struct Joiner
	{
		static std::wstring Join(const std::vector<std::wstring>& v)
		{
			std::wstring r;
			for (size_t i = 0; i < v.size(); ++i)
				r += (i == 0 ? L"" : (i + 1 == v.size() ? L" and " : L", ")) + v[i];
			return r;
		}
	};
	return L"Under [DisableSystems] in config.ini, set " + Joiner::Join(zero) +
	       L" back to 0 by hand (the game's own values) and leave " + Joiner::Join(one) +
	       L" at 1, or delete config.ini so that the game writes a new one at its next start.";
}

std::wstring Widen(const std::string& s)
{
	if (s.empty()) return std::wstring();
	int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), NULL, 0);
	UINT cp = CP_UTF8;
	if (n <= 0)
	{
		cp = CP_ACP;  // not UTF-8: the system code page (Notepad's ANSI)
		n = MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), NULL, 0);
	}
	std::wstring w(n > 0 ? n : 0, L'\0');
	if (n > 0) MultiByteToWideChar(cp, cp == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, s.data(), (int)s.size(), &w[0], n);
	return w;
}

std::string ToUtf8(const std::wstring& w)
{
	if (w.empty()) return std::string();
	int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
	std::string s(n > 0 ? n : 0, '\0');
	if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
	return s;
}

std::wstring Num(unsigned long n)
{
	wchar_t b[32];
	_snwprintf_s(b, 32, _TRUNCATE, L"%lu", n);
	return b;
}

// Reads a file; with stripBom a UTF-8 byte-order mark is dropped. False when it cannot be opened.
bool ReadSmallFile(const std::wstring& path, std::string* out, bool stripBom = true)
{
	out->clear();
	HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
	                       FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return false;
	char buf[4096];
	DWORD got = 0;
	while (out->size() < 1024 * 1024 && ReadFile(h, buf, sizeof(buf), &got, NULL) && got > 0) out->append(buf, got);
	CloseHandle(h);
	if (stripBom && out->size() >= 3 && (unsigned char)(*out)[0] == 0xEF && (unsigned char)(*out)[1] == 0xBB &&
	    (unsigned char)(*out)[2] == 0xBF)
		out->erase(0, 3);
	return true;
}

bool WriteAtomic(const std::wstring& path, const std::string& data, std::wstring* err)
{
	DWORD attr = GetFileAttributesW(path.c_str());
	if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY))
	{
		*err = L"read-only: " + path;
		return false;
	}
	std::wstring tmp = path + L".vp64tmp";
	HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
	{
		*err = L"cannot create " + tmp + L" (Windows error " + Num(GetLastError()) + L")";
		return false;
	}
	DWORD put = 0;
	BOOL ok = data.empty() || WriteFile(h, data.data(), (DWORD)data.size(), &put, NULL);
	ok = ok && FlushFileBuffers(h);
	CloseHandle(h);
	if (!ok || put != (DWORD)data.size() ||
	    !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		DWORD e = GetLastError();
		DeleteFileW(tmp.c_str());
		*err = L"cannot write " + path + L" (Windows error " + Num(e) + L")";
		return false;
	}
	return true;
}

std::string Trim(const std::string& s)
{
	size_t b = 0, e = s.size();
	while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
	while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
	return s.substr(b, e - b);
}

// ---- vpproxy.ini: flat key=value lines as proxy_rt_logic.cpp ParseSettings reads them (; and # comments, keys exact,
// a value in double quotes loses them; the first line of a key counts) ---------------------------------------------
bool IniGet(const std::wstring& ini, const char* key, std::string* value)
{
	std::string t;
	if (!ReadSmallFile(ini, &t)) return false;
	size_t pos = 0;
	while (pos <= t.size())
	{
		size_t e = t.find('\n', pos);
		if (e == std::string::npos) e = t.size();
		std::string line = Trim(t.substr(pos, e - pos));
		pos = e + 1;
		if (line.empty() || line[0] == '#' || line[0] == ';') continue;
		size_t eq = line.find('=');
		if (eq == std::string::npos || Trim(line.substr(0, eq)) != key) continue;
		std::string v = Trim(line.substr(eq + 1));
		if (v.size() >= 2 && v[0] == '"' && v[v.size() - 1] == '"') v = v.substr(1, v.size() - 2);
		*value = v;
		return true;
	}
	return false;
}

// Sets key=value (value NULL: removes every line of the key). The first line of the key is replaced in place (its line
// end kept); later lines of the same key are removed; a missing key is appended with the file's line end. Every other
// byte stays. The file must exist.
bool IniSet(const std::wstring& ini, const char* key, const char* value, std::wstring* err)
{
	std::string t;
	if (!ReadSmallFile(ini, &t, false))
	{
		*err = L"no options file at " + ini;
		return false;
	}
	std::string eol = "\r\n";
	size_t nl = t.find('\n');
	if (nl != std::string::npos) eol = (nl > 0 && t[nl - 1] == '\r') ? "\r\n" : "\n";
	std::string out;
	bool done = false;
	size_t pos = 0;
	while (pos < t.size())
	{
		size_t e = t.find('\n', pos);
		size_t next = e == std::string::npos ? t.size() : e + 1;
		size_t end = e == std::string::npos ? t.size() : ((e > pos && t[e - 1] == '\r') ? e - 1 : e);
		std::string line = t.substr(pos, end - pos);
		std::string tl = Trim(line);
		bool hit = false;
		if (!tl.empty() && tl[0] != '#' && tl[0] != ';')
		{
			size_t eq = tl.find('=');
			hit = eq != std::string::npos && Trim(tl.substr(0, eq)) == key;
		}
		if (!hit) out.append(t, pos, next - pos);
		else if (value && !done)
		{
			out += std::string(key) + "=" + value;
			out.append(t, end, next - end);
			done = true;
		}
		pos = next;
	}
	if (value && !done)
	{
		if (!out.empty() && out[out.size() - 1] != '\n') out += eol;
		out += std::string(key) + "=" + value + eol;
	}
	if (out == t) return true;
	return WriteAtomic(ini, out, err);
}

std::wstring FirstLine(const std::wstring& file)
{
	std::string t;
	if (!ReadSmallFile(file, &t)) return std::wstring();
	size_t e = t.find('\n');
	return Widen(Trim(t.substr(0, e == std::string::npos ? t.size() : e)));
}

// Issue 0361: the failure's stage as the proxy wrote it (proxy_report.cpp): the "stage: " line of report.txt's [failure]
// section, else the "(<stage>: <code>)" of popup-summary.txt's first line ("The 64-bit part of the patch failed (...)");
// empty when neither names one (no report, an older proxy's folder).
std::string ReportStage(const std::wstring& dir)
{
	std::string t;
	if (ReadSmallFile(dir + L"\\report.txt", &t))
	{
		bool failure = false;
		size_t pos = 0;
		while (pos < t.size())
		{
			size_t e = t.find('\n', pos);
			if (e == std::string::npos) e = t.size();
			std::string line = Trim(t.substr(pos, e - pos));
			pos = e + 1;
			if (!line.empty() && line[0] == '[') failure = line == "[failure]";
			else if (failure && line.compare(0, 6, "stage:") == 0) return Trim(line.substr(6));
		}
	}
	if (ReadSmallFile(dir + L"\\popup-summary.txt", &t))
	{
		size_t e = t.find('\n');
		std::string line = t.substr(0, e == std::string::npos ? t.size() : e);
		const char* const kHead = "The 64-bit part of the patch failed (";
		if (line.compare(0, strlen(kHead), kHead) == 0)
		{
			size_t c = line.find(':', strlen(kHead));
			if (c != std::string::npos) return Trim(line.substr(strlen(kHead), c - strlen(kHead)));
		}
	}
	return std::string();
}

// Ticket 0511: the server's own reason for its end as the proxy wrote it (proxy_report.cpp, ServerReason): the
// "server's reason: " line of report.txt's [failure] section; empty when there is none (no reason from the server, an
// older proxy's folder, no report.txt). Clipped to kReasonShown characters.
const size_t kReasonShown = 700;
std::wstring ReportServerReason(const std::wstring& dir)
{
	std::string t;
	if (!ReadSmallFile(dir + L"\\report.txt", &t)) return std::wstring();
	static const char kKey[] = "server's reason:";
	bool failure = false;
	size_t pos = 0;
	while (pos < t.size())
	{
		size_t e = t.find('\n', pos);
		if (e == std::string::npos) e = t.size();
		std::string line = Trim(t.substr(pos, e - pos));
		pos = e + 1;
		if (!line.empty() && line[0] == '[') failure = line == "[failure]";
		else if (failure && line.compare(0, sizeof(kKey) - 1, kKey) == 0)
		{
			std::wstring why = Widen(Trim(line.substr(sizeof(kKey) - 1)));
			if (why.size() > kReasonShown) why = why.substr(0, kReasonShown) + L" [...]";
			return why;
		}
	}
	return std::wstring();
}

bool HasDump(const std::wstring& dir)
{
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"\\*.dmp").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return false;
	bool any = false;
	do
	{
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) any = true;
	} while (!any && FindNextFileW(h, &fd));
	FindClose(h);
	return any;
}

std::wstring FullPath(const std::wstring& p)
{
	wchar_t buf[MAX_PATH * 4];
	DWORD n = GetFullPathNameW(p.c_str(), MAX_PATH * 4, buf, NULL);
	if (n == 0 || n >= MAX_PATH * 4) return p;
	std::wstring s(buf);
	while (s.size() > 3 && (s[s.size() - 1] == L'\\' || s[s.size() - 1] == L'/')) s.erase(s.size() - 1);
	return s;
}

std::wstring ExeDir()
{
	wchar_t buf[MAX_PATH * 4];
	DWORD n = GetModuleFileNameW(NULL, buf, MAX_PATH * 4);
	std::wstring s(buf, n < MAX_PATH * 4 ? n : 0);
	size_t k = s.find_last_of(L"\\/");
	return k == std::wstring::npos ? std::wstring(L".") : s.substr(0, k);
}

bool IsDir(const std::wstring& p)
{
	DWORD a = GetFileAttributesW(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

void Out(const std::wstring& w)
{
	std::string s = ToUtf8(w) + "\n";
	fwrite(s.data(), 1, s.size(), stdout);
	fflush(stdout);
}

std::wstring Join(const std::vector<std::string>& v)
{
	std::string s;
	for (size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + v[i];
	return Widen(s.empty() ? std::string("(none)") : s);
}

// <report>\helper.log: one line per step of `failed`, appended (the report folder is the proxy's; nothing else in it
// is touched).
struct Log
{
	std::wstring path;
	void operator()(const std::wstring& line) const
	{
		Out(L"failed: " + line);
		if (path.empty()) return;
		SYSTEMTIME st;
		GetLocalTime(&st);
		wchar_t ts[40];
		_snwprintf_s(ts, 40, _TRUNCATE, L"%04u-%02u-%02u %02u:%02u:%02u ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
		             st.wSecond);
		std::string s = ToUtf8(ts + line) + "\r\n";
		HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (h == INVALID_HANDLE_VALUE) return;
		DWORD put = 0;
		WriteFile(h, s.data(), (DWORD)s.size(), &put, NULL);
		CloseHandle(h);
	}
};

// ---- on / off / status ---------------------------------------------------------------------------------------------
// The one switch (issue 0030, the MC's ruling of 2026-11-16): on and off set config.ini's D33 keys AND VP64Options.ini's
// [Renderer] ServerRenderer together, so that no player reaches config.ini in D33 with ServerRenderer = 0 (a game with no
// terrain). opt is the options file (cfgini::OptionsIniPath unless --vp64-options names it).
//
// on: vpproxy.ini must exist and be writable before anything changes. ServerRenderer = 1 is written first (alone it is
// harmless: the server draws, the EXE draws too), then config.ini; if config.ini fails, ServerRenderer is put back; if
// mode=on cannot be written afterwards, both are put back (Off) so that D33 never stays with a proxy that would boot the
// 32-bit DLL.
bool DoOn(const std::wstring& cfg, const std::wstring& ini, const std::wstring& opt, std::wstring* msg)
{
	std::string probe;
	DWORD a = GetFileAttributesW(ini.c_str());
	if (!ReadSmallFile(ini, &probe, false) || (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY)))
	{
		*msg = L"no writable options file at " + ini + L" (the patch is not installed there); config.ini not changed";
		return false;
	}
	cfgini::Result ro = cfgini::RendererOn(opt);
	if (!ro.ok)
	{
		*msg = L"VP64Options.ini: " + Widen(ro.error) + L"; config.ini not changed";
		return false;
	}
	cfgini::Result r = cfgini::On(cfg);
	if (!r.ok)
	{
		*msg = L"config.ini: " + Widen(r.error);
		if (!ro.keys.empty())
		{
			cfgini::Result back = cfgini::RendererOff(opt);
			*msg += back.ok ? L"; VP64Options.ini put back" : L"; VP64Options.ini could NOT be put back: " + Widen(back.error);
		}
		return false;
	}
	std::wstring err;
	if (!IniSet(ini, "mode", "on", &err) || !IniSet(ini, "failed", NULL, &err))
	{
		cfgini::Result back = cfgini::Off(cfg);
		cfgini::Result oback = cfgini::RendererOff(opt);
		*msg = L"options file: " + err + (back.ok ? L"; config.ini put back" : L"; config.ini could NOT be put back: " +
		                                                                        Widen(back.error)) +
		       (oback.ok ? L"; VP64Options.ini put back" : L"; VP64Options.ini could NOT be put back: " + Widen(oback.error));
		return false;
	}
	*msg = L"config.ini " + cfg + L": keys written " + Join(r.keys) + L"; VP64Options.ini " + opt + L": " +
	       (ro.keys.empty() ? L"ServerRenderer = 1 already" : L"ServerRenderer = 1 written") + L"; " + ini +
	       L": mode=on, failed cleared";
	return true;
}

// off's VP64Options.ini part (issue 0030): one message part; false when the file could not be put back.
bool DoOffOptions(const std::wstring& opt, std::wstring* msg)
{
	cfgini::Result r = cfgini::RendererOff(opt);
	if (!r.ok) *msg = L"VP64Options.ini: FAILED: " + Widen(r.error);
	else
	{
		*msg = L"VP64Options.ini " + opt + L": " + (r.keys.empty() ? std::wstring(L"nothing to put back") :
		                                            L"put back " + Join(r.keys));
		if (!r.note.empty()) *msg += L" (" + Widen(r.note) + L")";
	}
	return r.ok;
}

// untouched (may be null): set when there was no usable side file and config.ini was left as it is (issue 0378); the
// message then names the case instead of the restored keys.
bool DoOffConfig(const std::wstring& cfg, std::wstring* msg, bool* untouched = NULL)
{
	cfgini::Result r = cfgini::Off(cfg);
	if (untouched) *untouched = r.ok && !r.note.empty();
	if (!r.ok) *msg = L"config.ini: " + Widen(r.error);
	else if (!r.note.empty()) *msg = L"config.ini " + cfg + L": " + Widen(r.note);
	else *msg = L"config.ini " + cfg + L": restored " + Join(r.keys);
	return r.ok;
}

int CmdOn(const std::wstring& cfg, const std::wstring& ini, const std::wstring& opt)
{
	std::wstring m;
	bool ok = DoOn(cfg, ini, opt, &m);
	Out(std::wstring(ok ? L"on: ok: " : L"on: FAILED: ") + m);
	return ok ? 0 : 1;
}

// off: config.ini first; if it cannot be restored nothing else changes (D33 with the 32-bit DLL would draw no terrain).
// Then VP64Options.ini; if that fails, mode=off is still written (ServerRenderer means nothing to a mode=off start) and
// the result is a failure. *msg is the whole message without a head; *untouched as DoOffConfig's (may be null). True ok.
// Used by the off verb and by the failed popup's "Remove 64bit patch" (D240, ticket 0682).
bool DoOff(const std::wstring& cfg, const std::wstring& ini, const std::wstring& opt, std::wstring* msg,
           bool* untouched = NULL)
{
	std::wstring m;
	if (!DoOffConfig(cfg, &m, untouched))
	{
		*msg = m + L"; VP64Options.ini and the options file are left as they are";
		return false;
	}
	std::wstring om;
	bool optOk = DoOffOptions(opt, &om);
	m += L"; " + om;
	std::wstring err;
	DWORD a = GetFileAttributesW(ini.c_str());
	if (a == INVALID_FILE_ATTRIBUTES)
	{
		*msg = m + L"; no options file at " + ini + L" (the patch is off without one)";
		return optOk;
	}
	if (!IniSet(ini, "mode", "off", &err))
	{
		*msg = m + L"; options file: " + err;
		return false;
	}
	*msg = m + L"; " + ini + L": mode=off";
	return optOk;
}

int CmdOff(const std::wstring& cfg, const std::wstring& ini, const std::wstring& opt)
{
	std::wstring m;
	bool ok = DoOff(cfg, ini, opt, &m);
	Out(std::wstring(ok ? L"off: ok: " : L"off: FAILED: ") + m);
	return ok ? 0 : 1;
}

int CmdStatus(const std::wstring& cfg, const std::wstring& ini, const std::wstring& opt)
{
	cfgini::Result d;
	cfgini::State cst = cfgini::Status(cfg, &d);
	const wchar_t* names[] = {L"off", L"on", L"partial", L"nofile"};
	Out(std::wstring(L"status: config.ini ") + names[(int)cst] + L" (" + cfg + L")");
	if (!d.keys.empty()) Out(L"status: keys that differ: " + Join(d.keys));
	if (!d.error.empty()) Out(L"status: note: " + Widen(d.error));
	// issue 0030: the switch's second file
	cfgini::Result od;
	cfgini::State ost = cfgini::RendererStatus(opt, &od);
	std::string otext;
	bool odec = false;
	int ov = ReadSmallFile(opt, &otext, false) ? cfgini::RendererValue(otext, &odec) : 0;
	Out(std::wstring(L"status: VP64Options.ini ") + names[(int)ost] + L" (" + opt + L"): ServerRenderer = " + Num(ov) +
	    (odec ? L"" : L" (default)"));
	if (!od.error.empty()) Out(L"status: VP64Options.ini note: " + Widen(od.error));
	// the switch as a whole: on or off only when both files agree
	cfgini::State st = cst == cfgini::State::NoFile ? cst
	                   : cst == ost                 ? cst
	                                                : cfgini::State::Partial;
	if (st != cst) Out(std::wstring(L"status: switch ") + names[(int)st] + L" (config.ini and VP64Options.ini differ)");
	std::string mode, failed;
	bool have = GetFileAttributesW(ini.c_str()) != INVALID_FILE_ATTRIBUTES;
	if (!have) Out(L"status: options file: none at " + ini);
	else
	{
		Out(L"status: mode: " + (IniGet(ini, "mode", &mode) ? Widen(mode) : std::wstring(L"(not set)")));
		Out(L"status: failed: " + (IniGet(ini, "failed", &failed) ? Widen(failed) : std::wstring(L"(not set)")));
	}
	switch (st)
	{
	case cfgini::State::Off: return 0;
	case cfgini::State::On: return 3;
	case cfgini::State::Partial: return 4;
	default: return 5;
	}
}

// ---- visual-additions on / off / status (ticket 4931) ------------------------------------------------------------
// The installer's one line (docs/extras/flags-and-messages-design.md 1.7, proposal 4.1): [Extras] EnableVisualAdditions.
// on writes 1 (nothing when a line already decides 1), off writes 0 (likewise), status prints the value the options parser
// would take and whether a line decides it. Never a feature flag, never ServerRenderer, no side file. Exit 0 ok, 1 failed
// (nothing changed), 2 usage.
int CmdVisualAdditions(const std::wstring& sub, const std::wstring& opt)
{
	const char* const key = cfgini::kVisualAdditionsKey;
	if (sub == L"status")
	{
		std::string t;
		const bool have = ReadSmallFile(opt, &t, false);
		if (have && t.size() >= 2 && ((unsigned char)t[0] == 0xFF && (unsigned char)t[1] == 0xFE ||
		                              (unsigned char)t[0] == 0xFE && (unsigned char)t[1] == 0xFF))
		{
			Out(L"visual-additions: FAILED: " + opt + L" is saved as UTF-16; save it as UTF-8 or ANSI");
			return 1;
		}
		bool dec = false;
		const int v = have ? cfgini::OptionValue(t, key, cfgini::kVisualAdditionsMin, cfgini::kVisualAdditionsMax,
		                                         cfgini::kVisualAdditionsDefault, &dec)
		                   : cfgini::kVisualAdditionsDefault;
		Out(L"visual-additions: status: " + Widen(key) + L" = " + Num((unsigned long)v) +
		    (dec ? L" (a line in " + opt + L" decides it)"
		         : have ? L" (default: no line of it in " + opt + L")" : L" (default: no file " + opt + L")") +
		    (v ? L": visual additions on" : L": visual additions off"));
		return 0;
	}
	const int want = sub == L"on" ? 1 : 0;
	cfgini::Result r = cfgini::SetOptionLine(opt, cfgini::kExtrasSection, key, want);
	if (!r.ok)
	{
		Out(L"visual-additions " + sub + L": FAILED: " + Widen(r.error));
		return 1;
	}
	Out(L"visual-additions " + sub + L": ok: " + opt + L": " + Widen(key) + L" = " + Num((unsigned long)want) +
	    (r.keys.empty() ? L" already" : L" written"));
	return 0;
}

// ---- the popup -----------------------------------------------------------------------------------------------------
struct Popup
{
	std::wstring report;      // full path
	bool reportExists;
	bool remove;              // "Remove 64bit patch" offered (D240, ticket 0682: failed, the patch stayed on)
	bool removed;             // it was pressed and off succeeded
	std::wstring cfg, ini, opt;  // for remove (opt: VP64Options.ini, issue 0030)
	Log log;                  // failed: <report>\helper.log (the remove's line goes there too)
	std::vector<std::wstring> lines;
	Popup() : reportExists(false), remove(false), removed(false) {}
};

void ReportLines(Popup& p, const std::wstring& reportArg, const std::wstring& options)
{
	p.report = FullPath(reportArg);
	p.reportExists = IsDir(p.report);
	if (p.reportExists)
	{
		std::wstring sum = FirstLine(p.report + L"\\popup-summary.txt");
		// Ticket 0511: when the server named its own reason, that reason is the popup's first line, and the proxy's
		// summary (the stage and code, e.g. "server-death: PeerDied") follows as the details.
		const std::wstring why = ReportServerReason(p.report);
		// Ticket 0681: a save made with other mods keeps its plain first line; the server's reason follows the summary
		const bool saveMods = ReportStage(p.report) == kStageSaveMods;
		if (!why.empty() && !saveMods)
		{
			p.lines.insert(p.lines.begin(), std::wstring(kReasonHead) + why);
			if (!sum.empty()) sum = L"Details: " + sum;
		}
		p.lines.push_back(sum.empty() ? std::wstring(kDefaultSummary) : sum);
		if (!why.empty() && saveMods) p.lines.push_back(L"Details: " + why);
		if (HasDump(p.report)) p.lines.push_back(kDump);
		p.lines.push_back(L"Report: " + p.report);
	}
	else
	{
		p.lines.push_back(L"The error report could not be found at " + p.report + L". The patch's log, vpproxy.log, says why.");
	}
	std::string h;
	p.lines.push_back(IniGet(options, "handin", &h) && !h.empty() ? Widen(h) : std::wstring(kDefaultHandin));
	p.lines.push_back(kPublic);
}

void OpenFolder(const std::wstring& dir, bool noUi)
{
	if (noUi)
	{
		Out(L"open: explorer " + dir);
		return;
	}
	ShellExecuteW(NULL, L"explore", dir.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

const int kIdOpen = 101, kIdClose = 102, kIdRemove = 103;
Popup* g_popup = NULL;
HFONT g_font = NULL;
HWND g_edit = NULL;

// "Remove 64bit patch" (D240, ticket 0682): the off switch (DoOff), logged to helper.log; returns the line the popup adds.
std::wstring DoRemove(Popup& p, bool* ok)
{
	std::wstring m;
	bool untouched = false;
	*ok = DoOff(p.cfg, p.ini, p.opt, &m, &untouched);
	p.log(std::wstring(L"Remove 64bit patch pressed: off ") + (*ok ? L"ok: " : L"FAILED: ") + m);
	if (*ok) p.removed = true;
	if (*ok && untouched)
		return std::wstring(kRemoveLabel) + L": the patch is switched off, but your config.ini was left as it is: the patch's "
		       L"record of your own settings (vp64-config-side.ini) was missing or unreadable. " + ConfigByHandText();
	if (*ok)
		return std::wstring(kRemoveLabel) + L": done. The patch is switched off and your own settings are back: the next "
		       L"start is the normal 32-bit game. To switch the patch on again, run vp64helper.exe on (beside the patch's "
		       L"DLL).";
	return std::wstring(kRemoveLabel) + L" did not work: " + m + L". Close the game if it still runs, then run "
	       L"vp64helper.exe off (beside the patch's DLL).";
}

void Remove(HWND button)
{
	bool ok = false;
	std::wstring add = DoRemove(*g_popup, &ok);
	int n = GetWindowTextLengthW(g_edit);
	std::vector<wchar_t> buf(n + 1);
	GetWindowTextW(g_edit, &buf[0], n + 1);
	std::wstring text = std::wstring(&buf[0]) + L"\r\n\r\n" + add;
	SetWindowTextW(g_edit, text.c_str());
	if (ok) EnableWindow(button, FALSE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg)
	{
	case WM_COMMAND:
		if (LOWORD(wp) == kIdOpen && g_popup && g_popup->reportExists) OpenFolder(g_popup->report, false);
		else if (LOWORD(wp) == kIdRemove && g_popup && g_popup->remove && !g_popup->removed) Remove((HWND)lp);
		else if (LOWORD(wp) == kIdClose || LOWORD(wp) == IDCANCEL) DestroyWindow(hwnd);
		return 0;
	case WM_CLOSE:
		DestroyWindow(hwnd);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

int ShowWindowed(Popup& p)
{
	g_popup = &p;
	NONCLIENTMETRICSW ncm;
	ncm.cbSize = sizeof(ncm);
	if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) g_font = CreateFontIndirectW(&ncm.lfMessageFont);
	if (!g_font) g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
	HINSTANCE inst = GetModuleHandleW(NULL);
	WNDCLASSW wc;
	ZeroMemory(&wc, sizeof(wc));
	wc.lpfnWndProc = WndProc;
	wc.hInstance = inst;
	wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512) /* IDC_ARROW */);
	wc.hIcon = LoadIconW(NULL, MAKEINTRESOURCEW(32515) /* IDI_WARNING */);
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpszClassName = L"VP64HelperPopup";
	RegisterClassW(&wc);
	UINT dpi = 96;
	HDC dc = GetDC(NULL);
	if (dc)
	{
		dpi = (UINT)GetDeviceCaps(dc, LOGPIXELSY);
		ReleaseDC(NULL, dc);
	}
	const int W = MulDiv(660, dpi, 96), H = MulDiv(360, dpi, 96), m = MulDiv(12, dpi, 96), bw = MulDiv(150, dpi, 96),
	          bh = MulDiv(28, dpi, 96);
	RECT r = {0, 0, W, H};
	DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP;
	AdjustWindowRect(&r, style, FALSE);
	int ww = r.right - r.left, wh = r.bottom - r.top;
	int x = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;
	HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_DLGMODALFRAME, wc.lpszClassName, kTitle, style, x, y, ww, wh, NULL,
	                            NULL, inst, NULL);
	if (!hwnd) return 1;
	std::wstring text;
	for (size_t i = 0; i < p.lines.size(); ++i) text += (i ? L"\r\n\r\n" : L"") + p.lines[i];
	g_edit = CreateWindowExW(0, L"EDIT", text.c_str(), WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY |
	                                                       ES_AUTOVSCROLL,
	                         m, m, W - 2 * m, H - 3 * m - bh, hwnd, NULL, inst, NULL);
	std::vector<HWND> kids(1, g_edit);
	int nb = p.remove ? 3 : 2, xb = W - nb * (bw + m);
	kids.push_back(CreateWindowExW(0, L"BUTTON", L"Open folder", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON |
	                                                                 (p.reportExists ? 0 : WS_DISABLED),
	                               xb, H - m - bh, bw, bh, hwnd, (HMENU)(INT_PTR)kIdOpen, inst, NULL));
	if (p.remove)
	{
		xb += bw + m;
		kids.push_back(CreateWindowExW(0, L"BUTTON", kRemoveLabel, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, xb,
		                               H - m - bh, bw, bh, hwnd, (HMENU)(INT_PTR)kIdRemove, inst, NULL));
	}
	HWND close = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
	                             W - bw - m, H - m - bh, bw, bh, hwnd, (HMENU)(INT_PTR)kIdClose, inst, NULL);
	kids.push_back(close);
	for (size_t i = 0; i < kids.size(); ++i) SendMessageW(kids[i], WM_SETFONT, (WPARAM)g_font, TRUE);
	ShowWindow(hwnd, SW_SHOWNORMAL);
	SetForegroundWindow(hwnd);
	SetFocus(close);
	MSG msg;
	while (GetMessageW(&msg, NULL, 0, 0) > 0)
	{
		if (IsDialogMessageW(hwnd, &msg)) continue;
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	return 0;
}

void Show(Popup& p, bool noUi, const std::wstring& choice)
{
	if (!noUi)
	{
		ShowWindowed(p);
		return;
	}
	Out(std::wstring(L"popup: title: ") + kTitle);
	for (size_t i = 0; i < p.lines.size(); ++i) Out(L"popup: line: " + p.lines[i]);
	Out(std::wstring(L"popup: buttons: ") + (p.reportExists ? L"Open folder" : L"Open folder (disabled)") +
	    (p.remove ? std::wstring(L" | ") + kRemoveLabel : std::wstring()) + L" | Close");
	if (choice == L"open" && p.reportExists) OpenFolder(p.report, true);
	if (choice == L"remove" && p.remove)
	{
		bool ok = false;
		std::wstring add = DoRemove(p, &ok);
		Out(std::wstring(ok ? L"remove: off ok: " : L"remove: off FAILED: ") + add);
	}
	Out(L"popup: closed");
}

// ---- failed --------------------------------------------------------------------------------------------------------
int CmdFailed(DWORD pid, const std::wstring& reportArg, const std::wstring& cfg, const std::wstring& ini,
              const std::wstring& opt, DWORD deadlineS,
              bool noUi, const std::wstring& choice)
{
	Popup p;
	p.cfg = cfg;
	p.ini = ini;
	p.opt = opt;
	std::wstring report = FullPath(reportArg);
	Log log = {IsDir(report) ? report + L"\\helper.log" : std::wstring()};
	p.log = log;
	log(L"started for EXE pid " + Num(pid) + L", report " + report + L", config " + cfg + L", options " + ini +
	    L", deadline " + Num(deadlineS) + L" s");
	// Ticket 0635 (MC #9080): the patch switched off while config.ini was still in the patch's form (stage off-switch,
	// proxy_rt.cpp OffD33NoPlay) is not a failure of the 64-bit part: its own words, and no Remove button (it is off).
	const std::string stage = ReportStage(report);
	const bool offSwitch = stage == kStageOffSwitch;
	// D240 (ticket 0682): after a failure the patch stays on. Two stages keep the switch-off of before D240: off-switch
	// (above) and exe-build (ticket 0588: this EXE build is not supported, so every start would end with no play).
	const bool switchOff = offSwitch || stage == kStageExeBuild;
	if (stage != kStageFallbackDll)  // ticket 0721: that stage says its own line below
	log(switchOff ? L"stage " + Widen(stage) + L": the patch is switched off after the EXE's end (no Remove button)"
	              : L"stage " + (stage.empty() ? std::wstring(L"(none named)") : Widen(stage)) +
	                    L": the patch stays on (D240); the popup offers \"" + kRemoveLabel + L"\"");
	const std::wstring offHead = L"The patch is switched off, but the game's settings file (config.ini) was still in the form "
	                             L"the patch uses. The game would have drawn a broken map, so it was ended before the map "
	                             L"appeared.";
	// 1. the EXE's end
	int code = 0;
	bool ended = false;
	HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
	if (!h)
	{
		DWORD e = GetLastError();
		if (e == ERROR_INVALID_PARAMETER)
		{
			ended = true;
			log(L"the EXE had already ended (no process " + Num(pid) + L")");
		}
		else
			log(L"cannot wait for process " + Num(pid) + L" (Windows error " + Num(e) + L"): treated as not ended");
	}
	else
	{
		DWORD w = WaitForSingleObject(h, deadlineS * 1000);
		CloseHandle(h);
		ended = w == WAIT_OBJECT_0;
		log(ended ? L"the EXE ended" : L"the EXE did not end within the deadline");
	}
	if (!ended)
	{
		log(L"nothing restored, nothing set: config.ini still holds the patch's settings");
		if (switchOff)
			p.lines.push_back((offSwitch ? offHead + L" But the game did not close within "
			                             : L"The 64-bit part of the patch failed, and the game did not close within ") +
			                  Num(deadlineS) + L" seconds. Close the game, then run \"vp64helper off\" (beside the patch's DLL) "
			                  L"so that the next start is the normal game; config.ini was not changed yet.");
		else  // D240: no Remove button while the game may still run (the EXE writes config.ini at its start)
			p.lines.push_back(L"The 64-bit part of the patch failed, and the game did not close within " + Num(deadlineS) +
			                  L" seconds. The patch stays on: the next start uses the 64-bit patch again. To remove the "
			                  L"patch instead, close the game, then run \"vp64helper off\" (beside the patch's DLL); the next "
			                  L"start is then the normal game.");
		code = 3;
	}
	else if (stage == kStageFallbackDll)
	{
		// Ticket 0721: the 32-bit DLL could not be loaded (the proxy ended the game rather than hand the EXE no context).
		// Switching the patch off would not help (the next start needs the same DLL), so: nothing restored or set, no
		// Remove button, and the way out is a new installation.
		log(L"stage fallback-dll: nothing restored, nothing set, no Remove button (the 32-bit DLL is needed with the patch "
		    L"on or off)");
		p.lines.push_back(L"The game's own 32-bit DLL, which the patch loads in place of its 64-bit part, could not be "
		                  L"loaded, so the game was ended before it could crash. The installation is partial or damaged (a "
		                  L"file missing, or taken away by an antivirus program): install the patch again. Nothing was "
		                  L"changed.");
	}
	else if (!switchOff)
	{
		// D240 (ticket 0682): nothing restored or set; "Remove 64bit patch" is the opt-in off switch, Close keeps it on.
		log(L"nothing restored, nothing set: the patch stays on (config.ini, VP64Options.ini and " + ini +
		    L" as they are); the next start uses the 64-bit patch again");
		// Ticket 0681: a save made with other mods: its own plain first line, then the same D240 choice
		p.lines.push_back(std::wstring(stage == kStageSaveMods
		                                   ? std::wstring(kSaveModsHead) + L" The game was ended; the next start uses the "
		                                                                   L"64-bit patch again. \""
		                                   : std::wstring(L"The 64-bit part of the patch failed, so the game was ended. The "
		                                                  L"patch stays on: the next start uses the 64-bit patch again. \"")) +
		                  kRemoveLabel + L"\" switches the patch off and puts your own game settings back, so that the next "
		                  L"start is the normal 32-bit game (vp64helper.exe on switches it on again later). \"Close\" keeps "
		                  L"the patch on.");
		p.remove = true;
	}
	else
	{
		// 2. config.ini back, 3. failed=1
		std::wstring m;
		bool untouched = false;
		if (!DoOffConfig(cfg, &m, &untouched))
		{
			log(L"restore FAILED: " + m + L"; failed= not written");
			p.lines.push_back((offSwitch ? offHead + L" Your config.ini could NOT be put back ("
			                             : L"The 64-bit part of the patch failed and the game was ended. Your config.ini could "
			                               L"NOT be put back (") +
			                  m + L"). Run \"vp64helper off\" (beside the patch's DLL) before the next start.");
			code = 4;
		}
		else
		{
			log((untouched ? L"not restored: " : L"restored: ") + m);
			// issue 0030: the switch's second file as off puts it back (a failure here is logged; failed=1 still makes
			// the next start the 32-bit game, for which ServerRenderer means nothing)
			std::wstring om;
			log(DoOffOptions(p.opt, &om) ? om : om + L" (failed=1 is written all the same)");
			std::wstring err;
			if (IniSet(ini, "failed", "1", &err)) log(L"failed=1 written to " + ini);
			else log(L"failed=1 NOT written: " + err);
			if (offSwitch && untouched)
				p.lines.push_back(offHead + L" Your config.ini was left as it is: the patch's record of your own settings "
				                  L"(vp64-config-side.ini) was missing or unreadable, so it could not put them back. " +
				                  ConfigByHandText());
			else if (offSwitch)
				p.lines.push_back(offHead + L" Your config.ini is back to your own settings now: start the game again and "
				                  L"it plays as the normal game, with the patch off.");
			else if (untouched)  // issue 0378: no usable side file; config.ini was left as it is (stage exe-build)
				p.lines.push_back(std::wstring(kExeBuildHead) + L" The patch is switched off: the next start is the normal game. "
				                  L"Your config.ini was left as it is: the patch's record of your own settings "
				                  L"(vp64-config-side.ini) was missing or unreadable, so it changed nothing. vp64helper.exe on "
				                  L"(beside the patch's DLL) switches the patch on again once a version of the patch that "
				                  L"supports this game build is installed.");
			else  // stage exe-build
				p.lines.push_back(std::wstring(kExeBuildHead) + L" Your config.ini is back to your own settings and the patch "
				                  L"is switched off: the next start is the normal game. vp64helper.exe on (beside the patch's "
				                  L"DLL) switches the patch on again once a version of the patch that supports this game build "
				                  L"is installed.");
			if (offSwitch)
				p.lines.push_back(L"To switch the patch back on, run vp64helper.exe on. To switch it off in future, run "
				                  L"vp64helper.exe off instead of editing vpproxy.ini by hand; it puts config.ini back for you.");
		}
	}
	ReportLines(p, reportArg, ini);
	if (!p.reportExists && code == 0) code = 1;
	log(L"popup shown");
	Show(p, noUi, choice);
	if (p.remove)
		log(p.removed ? std::wstring(L"popup closed: the patch was removed (") + kRemoveLabel + L")"
		              : std::wstring(L"popup closed: the patch stays on"));
	return code;
}

int Usage()
{
	Out(L"usage: vp64helper on|off|status [--config <config.ini>] [--options <vpproxy.ini>] [--vp64-options <VP64Options.ini>]");
	Out(L"usage: vp64helper failed --pid <EXE pid> --report <dir> [--options <vpproxy.ini>] [--config <config.ini>] "
	    L"[--vp64-options <VP64Options.ini>] "
	    L"[--deadline-s N] [--no-ui] [--no-ui-choice open|remove|close]");
	Out(L"usage: vp64helper popup --report <dir> [--options <vpproxy.ini>] [--no-ui] [--no-ui-choice open|close]");
	Out(L"usage: vp64helper visual-additions on|off|status [--vp64-options <VP64Options.ini>] [--config <config.ini>]");
	return 2;
}

bool ParseUlong(const std::wstring& s, unsigned long* v)
{
	if (s.empty() || s.size() > 9) return false;
	unsigned long n = 0;
	for (size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] < L'0' || s[i] > L'9') return false;
		n = n * 10 + (s[i] - L'0');
	}
	*v = n;
	return true;
}
}  // namespace

int main()  // narrow entry: a wide one does not link with VC9 libcmt + lld (release-driver.md W3c.3); CommandLineToArgvW
{
	// Output goes to inherited handles (a test's pipe); started from a console, to that console.
	HANDLE so = GetStdHandle(STD_OUTPUT_HANDLE);
	if ((so == NULL || so == INVALID_HANDLE_VALUE) && AttachConsole(ATTACH_PARENT_PROCESS))
	{
		FILE* f = NULL;
		freopen_s(&f, "CONOUT$", "w", stdout);
	}
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (!argv || argc < 2) return Usage();
	std::wstring verb = argv[1];
	std::wstring report, options, cfg, opt, choice = L"close";
	unsigned long pid = 0, deadline = kDefaultDeadlineS;
	bool noUi = false, havePid = false;
	// ticket 4931: "visual-additions on|off|status" takes only --vp64-options and --config
	const bool visual = verb == L"visual-additions";
	std::wstring sub;
	if (visual)
	{
		sub = argc >= 3 ? argv[2] : L"";
		if (sub != L"on" && sub != L"off" && sub != L"status")
		{
			LocalFree(argv);
			return Usage();
		}
	}
	for (int i = visual ? 3 : 2; i < argc; ++i)
	{
		std::wstring a = argv[i];
		bool val = i + 1 < argc;
		if (visual && a != L"--vp64-options" && a != L"--config")
		{
			LocalFree(argv);
			return Usage();
		}
		if (a == L"--report" && val) report = argv[++i];
		else if (a == L"--options" && val) options = argv[++i];
		else if (a == L"--config" && val) cfg = argv[++i];
		else if (a == L"--vp64-options" && val) opt = argv[++i];
		else if (a == L"--no-ui") noUi = true;
		else if (a == L"--no-ui-choice" && val) choice = argv[++i];
		else if (a == L"--pid" && val && ParseUlong(argv[i + 1], &pid)) havePid = true, ++i;
		else if (a == L"--deadline-s" && val && ParseUlong(argv[i + 1], &deadline)) ++i;
		else return Usage();
	}
	if (options.empty()) options = ExeDir() + L"\\vpproxy.ini";
	options = FullPath(options);
	bool cfgVerb = verb == L"on" || verb == L"off" || verb == L"status" || verb == L"failed";
	if (cfgVerb && cfg.empty() && !cfgini::ConfigIniPath(&cfg))
	{
		Out(L"error: the Documents folder cannot be found; give --config");
		return 2;
	}
	if (visual && opt.empty() && cfg.empty() && !cfgini::ConfigIniPath(&cfg))
	{
		Out(L"error: the Documents folder cannot be found; give --vp64-options or --config");
		LocalFree(argv);
		return 2;
	}
	if ((cfgVerb || visual) && opt.empty()) opt = cfgini::OptionsIniPath(cfg);  // issue 0030: VP64_OPTIONS, else beside config.ini
	if (!opt.empty()) opt = FullPath(opt);
	int rc;
	if (visual) rc = CmdVisualAdditions(sub, opt);
	else if (verb == L"on" && report.empty() && !havePid) rc = CmdOn(cfg, options, opt);
	else if (verb == L"off" && report.empty() && !havePid) rc = CmdOff(cfg, options, opt);
	else if (verb == L"status" && report.empty() && !havePid) rc = CmdStatus(cfg, options, opt);
	else if (verb == L"failed" && havePid && pid != 0 && !report.empty() &&
	         (choice == L"open" || choice == L"close" || choice == L"remove"))
		rc = CmdFailed(pid, report, cfg, options, opt, deadline, noUi, choice);
	else if (verb == L"popup" && !report.empty() && !havePid && (choice == L"open" || choice == L"close"))
	{
		Popup p;
		// Issue 0361: a server death in a game ends the EXE (the proxy's on_server_death=exit), so the boot fallback's
		// "goes on as the normal (32-bit) game" would contradict the summary line under it.
		const std::string stage = ReportStage(FullPath(report));
		if (stage == "server-death")
			p.lines.push_back(L"The 64-bit part of the patch stopped during the game, so the game had to end.");
		// Ticket 0681: a save made with other mods (the server's code save-mod-mismatch); the server's summary follows
		else if (stage == kStageSaveMods)
			p.lines.push_back(kSaveModsHead);
		// Ticket 0354: the server refused a save of the other DLL variant (22 / 43 civilizations); the summary line under
		// this one names both versions.
		else if (stage == "save-variant")
			p.lines.push_back(L"This save cannot be loaded with the installed version of the 64-bit patch, so the game had to end.");
		// Ticket 0418: the EXE itself crashed (an exception exit code); the 64-bit server, still running, wrote the report.
		else if (stage == "exe-crash")
			p.lines.push_back(L"The game itself crashed (the game's own process, not the 64-bit server), so the game had to end.");
		// Ticket 0422: the DLL inside the 64-bit server stopped on a failed check (the server wrote the report; the summary
		// line under this one names the check).
		else if (stage == "dll-fatal")
			p.lines.push_back(L"The game stopped on an internal check in the game's DLL, so the game had to end.");
		else
			p.lines.push_back(L"The 64-bit part of the patch could not run. This game goes on as the normal (32-bit) game.");
		ReportLines(p, report, options);
		Show(p, noUi, choice);
		rc = p.reportExists ? 0 : 1;
	}
	else rc = Usage();
	LocalFree(argv);
	return rc;
}
