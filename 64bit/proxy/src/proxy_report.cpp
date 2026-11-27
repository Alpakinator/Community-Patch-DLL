// proxy_report.cpp - the local error report of decision D119 (issue 3521): see proxy_report.h. Hand-written.
// SPDX-License-Identifier: GPL-3.0-or-later
// user32, advapi32 and ntdll functions are looked up by name: the proxy's import list does not change.
#include "proxy_report.h"
#include "proxy_pack.h"
#include "proxy_session.h"
#include <stdio.h>
#include <string.h>

namespace ProxyReport
{
namespace
{
std::string Fmt(const char* fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
	va_end(ap);
	return buf;
}

char LowerA(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }
bool AlnumA(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (unsigned char)c >= 0x80;
}

bool MatchAt(const std::string& s, size_t at, const std::string& what)
{
	if (at + what.size() > s.size()) return false;
	for (size_t i = 0; i < what.size(); ++i)
		if (LowerA(s[at + i]) != LowerA(what[i])) return false;
	return true;
}

// Replaces every case-insensitive occurrence of what (bounded: no letter or digit right before or after it when
// bounded is true) by with.
std::string ReplaceAll(const std::string& s, const std::string& what, const std::string& with, bool bounded)
{
	if (what.empty()) return s;
	std::string out;
	out.reserve(s.size());
	size_t i = 0;
	while (i < s.size())
	{
		if (MatchAt(s, i, what) &&
		    (!bounded || ((i == 0 || !AlnumA(s[i - 1])) && (i + what.size() == s.size() || !AlnumA(s[i + what.size()])))))
		{
			out += with;
			i += what.size();
		}
		else
			out += s[i++];
	}
	return out;
}

std::string Acp(const std::wstring& w)
{
	if (w.empty()) return std::string();
	const int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
	std::string a(n > 0 ? n : 0, '\0');
	if (n > 0) WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), &a[0], n, NULL, NULL);
	return a;
}

std::wstring EnvW(const wchar_t* name)
{
	wchar_t buf[1024];
	const DWORD n = GetEnvironmentVariableW(name, buf, 1024);
	return (n > 0 && n < 1024) ? std::wstring(buf, n) : std::wstring();
}

// SteamID64: 17 digits starting 7656119, not part of a longer number.
std::string ScrubSteamIds(const std::string& s)
{
	std::string out;
	size_t i = 0;
	while (i < s.size())
	{
		if (i + 17 <= s.size() && s.compare(i, 7, "7656119") == 0 && (i == 0 || !(s[i - 1] >= '0' && s[i - 1] <= '9')))
		{
			size_t j = i;
			while (j < s.size() && s[j] >= '0' && s[j] <= '9') ++j;
			if (j - i == 17)
			{
				out += "<steamid>";
				i = j;
				continue;
			}
		}
		out += s[i++];
	}
	return out;
}

bool WriteAll(const std::wstring& path, const std::string& bytes, DWORD* err)
{
	HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
	{
		*err = GetLastError();
		return false;
	}
	DWORD w = 0;
	const BOOL ok = bytes.empty() || WriteFile(h, bytes.data(), (DWORD)bytes.size(), &w, NULL);
	*err = ok ? 0 : GetLastError();
	CloseHandle(h);
	return ok && w == bytes.size();
}

std::string Crlf(const std::string& s)
{
	std::string o;
	o.reserve(s.size() + s.size() / 32);
	for (size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] == '\n' && (i == 0 || s[i - 1] != '\r')) o += '\r';
		o += s[i];
	}
	return o;
}

bool MakeDirsW(const std::wstring& dir, DWORD* err)
{
	if (dir.empty()) return false;
	const DWORD a = GetFileAttributesW(dir.c_str());
	if (a != INVALID_FILE_ATTRIBUTES)
	{
		if (a & FILE_ATTRIBUTE_DIRECTORY) return true;
		*err = ERROR_ALREADY_EXISTS;  // a file in the way
		return false;
	}
	const size_t cut = dir.find_last_of(L"\\/");
	if (cut != std::wstring::npos && cut > 2 && !MakeDirsW(dir.substr(0, cut), err)) return false;
	if (CreateDirectoryW(dir.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS) return true;
	*err = GetLastError();
	return false;
}

std::string BaseName(const std::string& p)
{
	const size_t cut = p.find_last_of("\\/");
	return cut == std::string::npos ? p : p.substr(cut + 1);
}

std::wstring BaseNameW(const std::wstring& p)
{
	const size_t cut = p.find_last_of(L"\\/");
	return cut == std::wstring::npos ? p : p.substr(cut + 1);
}

// The whole file, or its last cap bytes; false when it cannot be read.
bool ReadCappedW(const std::wstring& path, size_t cap, std::string& out, bool* cut, DWORD* err)
{
	*cut = false;
	HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
	{
		*err = GetLastError();
		return false;
	}
	LARGE_INTEGER sz;
	if (!GetFileSizeEx(h, &sz))
	{
		*err = GetLastError();
		CloseHandle(h);
		return false;
	}
	LONGLONG start = 0;
	if (sz.QuadPart > (LONGLONG)cap)
	{
		start = sz.QuadPart - (LONGLONG)cap;
		*cut = true;
	}
	LARGE_INTEGER at;
	at.QuadPart = start;
	SetFilePointerEx(h, at, NULL, FILE_BEGIN);
	out.resize((size_t)(sz.QuadPart - start));
	DWORD got = 0;
	const BOOL ok = out.empty() || ReadFile(h, &out[0], (DWORD)out.size(), &got, NULL);
	*err = ok ? 0 : GetLastError();
	CloseHandle(h);
	if (!ok) return false;
	out.resize(got);
	if (*cut)
	{
		const size_t nl = out.find('\n');
		if (nl != std::string::npos) out.erase(0, nl + 1);
	}
	return true;
}

std::string Hex32(DWORD v) { return Fmt("0x%08lX", v); }
}  // namespace

// ================================================================================================ privacy
std::string Scrub(const std::string& text)
{
	std::string s = text;
	// 1. the profile folder, in UTF-8 and the ANSI code page, with either separator and JSON's doubled backslashes
	std::wstring prof = EnvW(L"USERPROFILE");
	while (!prof.empty() && (prof[prof.size() - 1] == L'\\' || prof[prof.size() - 1] == L'/')) prof.erase(prof.size() - 1);
	if (prof.size() > 3)
	{
		const std::string forms[2] = {ProxySession::Utf8(prof), Acp(prof)};
		for (int f = 0; f < 2; ++f)
		{
			std::string p = forms[f], fwd = p, dbl;
			for (size_t i = 0; i < fwd.size(); ++i)
				if (fwd[i] == '\\') fwd[i] = '/';
			for (size_t i = 0; i < p.size(); ++i)
				dbl += p[i] == '\\' ? std::string("\\\\") : std::string(1, p[i]);
			s = ReplaceAll(s, dbl, "%USERPROFILE%", false);
			s = ReplaceAll(s, p, "%USERPROFILE%", false);
			s = ReplaceAll(s, fwd, "%USERPROFILE%", false);
		}
	}
	// 2. the user and computer names anywhere else, as whole words
	const wchar_t* names[3] = {L"USERNAME", L"COMPUTERNAME", L"USERDOMAIN"};
	const char* subst[3] = {"<user>", "<computer>", "<computer>"};
	for (int k = 0; k < 3; ++k)
	{
		const std::wstring v = EnvW(names[k]);
		if (v.empty()) continue;
		s = ReplaceAll(s, ProxySession::Utf8(v), subst[k], true);
		s = ReplaceAll(s, Acp(v), subst[k], true);
	}
	// 3. SteamID64s
	return ScrubSteamIds(s);
}

// ================================================================================================ sections
std::string ExeText()
{
	std::string s;
	wchar_t path[MAX_PATH * 2];
	const DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH * 2);
	const std::wstring exe = n ? std::wstring(path, n) : std::wstring(L"?");
	// Ticket 0588: the build from the one registry (exebuild/include/vp_exe_build.h), stamp and SizeOfImage together.
	const ProxySession::ExeBuild b = ProxySession::ReadExeBuild();
	const VpExeBuildInfo* info = VpExeBuildInfoOf(b.id);
	s += "module: " + ProxySession::Utf8(BaseNameW(exe)) + "\n";
	s += Fmt("build: PE TimeDateStamp 0x%08X, SizeOfImage 0x%08X = %s%s%s\n", b.stamp, b.sizeOfImage,
	         info ? info->label : "not a build the patch knows",
	         info ? (b.devReady ? ", supported only through the DEVELOPER switch VP64_DEV_READY_BUILD"
	                 : b.supported ? ", supported by this binary"
	                               : ", NOT supported by this version of the patch yet")
	              : "",
	         b.test ? " (VPPROXY_TEST_EXE_BUILD, a test switch)" : "");
	WIN32_FILE_ATTRIBUTE_DATA fa;
	if (GetFileAttributesExW(exe.c_str(), GetFileExInfoStandard, &fa))
		s += Fmt("file size: %I64u bytes\n", ((unsigned __int64)fa.nFileSizeHigh << 32) | fa.nFileSizeLow);
	return s;
}

std::string SystemText()
{
	std::string s;
	// Windows version (RtlGetVersion: GetVersionEx lies to programs without a manifest entry)
	typedef LONG(WINAPI * RtlGetVersionFn)(OSVERSIONINFOEXW*);
	HMODULE nt = GetModuleHandleW(L"ntdll.dll");
	RtlGetVersionFn rgv = nt ? (RtlGetVersionFn)GetProcAddress(nt, "RtlGetVersion") : NULL;
	OSVERSIONINFOEXW vi;
	memset(&vi, 0, sizeof(vi));
	vi.dwOSVersionInfoSize = sizeof(vi);
	if (rgv && rgv(&vi) == 0)
		s += Fmt("windows: %lu.%lu build %lu, service pack %u.%u, product type %u\n", vi.dwMajorVersion, vi.dwMinorVersion,
		         vi.dwBuildNumber, vi.wServicePackMajor, vi.wServicePackMinor, vi.wProductType);
	else
		s += "windows: RtlGetVersion not available\n";
	BOOL wow = FALSE;
	IsWow64Process(GetCurrentProcess(), &wow);
	SYSTEM_INFO si;
	GetNativeSystemInfo(&si);
	s += Fmt("processors: %lu (native architecture %u), this process %s\n", si.dwNumberOfProcessors,
	         si.wProcessorArchitecture, wow ? "32-bit on a 64-bit Windows (WOW64)" : "native");
	// memory
	MEMORYSTATUSEX ms;
	ms.dwLength = sizeof(ms);
	if (GlobalMemoryStatusEx(&ms))
		s += Fmt("memory: physical %I64u MB, available %I64u MB; this process's virtual %I64u MB, available %I64u MB\n",
		         ms.ullTotalPhys >> 20, ms.ullAvailPhys >> 20, ms.ullTotalVirtual >> 20, ms.ullAvailVirtual >> 20);
	{
		// the largest free block of this process's address space (the 32-bit EXE's limit, bug 0102)
		SIZE_T largest = 0;
		MEMORY_BASIC_INFORMATION mi;
		for (const char* p = (const char*)0x10000; VirtualQuery(p, &mi, sizeof(mi)) == sizeof(mi);)
		{
			if (mi.State == MEM_FREE && mi.RegionSize > largest) largest = mi.RegionSize;
			const char* next = (const char*)mi.BaseAddress + mi.RegionSize;
			if (next <= p) break;
			p = next;
		}
		s += Fmt("address space: largest free block %lu MB\n", (unsigned long)(largest >> 20));
	}
	typedef struct
	{
		DWORD cb, PageFaultCount;
		SIZE_T PeakWorkingSetSize, WorkingSetSize, QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage,
		    QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage, PagefileUsage, PeakPagefileUsage, PrivateUsage;
	} PMC;
	typedef BOOL(WINAPI * GetPmiFn)(HANDLE, PMC*, DWORD);
	GetPmiFn gpmi = (GetPmiFn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo");
	PMC pmc;
	memset(&pmc, 0, sizeof(pmc));
	pmc.cb = sizeof(pmc);
	if (gpmi && gpmi(GetCurrentProcess(), &pmc, sizeof(pmc)))
		s += Fmt("this process: private %lu MB, working set %lu MB (peak %lu MB)\n", (unsigned long)(pmc.PrivateUsage >> 20),
		         (unsigned long)(pmc.WorkingSetSize >> 20), (unsigned long)(pmc.PeakWorkingSetSize >> 20));
	// GPU, driver, display mode (user32 and advapi32 by name)
	HMODULE u = LoadLibraryW(L"user32.dll");
	HMODULE adv = LoadLibraryW(L"advapi32.dll");
	typedef BOOL(WINAPI * EnumDdFn)(LPCSTR, DWORD, PDISPLAY_DEVICEA, DWORD);
	typedef BOOL(WINAPI * EnumDsFn)(LPCSTR, DWORD, DEVMODEA*);
	typedef LONG(WINAPI * RegOpenFn)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
	typedef LONG(WINAPI * RegQueryFn)(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
	typedef LONG(WINAPI * RegCloseFn)(HKEY);
	EnumDdFn enumDd = u ? (EnumDdFn)GetProcAddress(u, "EnumDisplayDevicesA") : NULL;
	EnumDsFn enumDs = u ? (EnumDsFn)GetProcAddress(u, "EnumDisplaySettingsA") : NULL;
	RegOpenFn regOpen = adv ? (RegOpenFn)GetProcAddress(adv, "RegOpenKeyExA") : NULL;
	RegQueryFn regQuery = adv ? (RegQueryFn)GetProcAddress(adv, "RegQueryValueExA") : NULL;
	RegCloseFn regClose = adv ? (RegCloseFn)GetProcAddress(adv, "RegCloseKey") : NULL;
	if (enumDd)
	{
		std::string seen;
		for (DWORD i = 0; i < 16; ++i)
		{
			DISPLAY_DEVICEA dd;
			memset(&dd, 0, sizeof(dd));
			dd.cb = sizeof(dd);
			if (!enumDd(NULL, i, &dd, 0)) break;
			const bool primary = (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0;
			const bool attached = (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0;
			if (!attached && !primary) continue;
			std::string drv = "driver version not readable";
			// DeviceKey: \Registry\Machine\System\CurrentControlSet\Control\Video\{GUID}\0000
			const char* key = strstr(dd.DeviceKey, "\\System\\");
			if (!key) key = strstr(dd.DeviceKey, "\\SYSTEM\\");
			if (key && regOpen && regQuery && regClose)
			{
				HKEY hk;
				if (regOpen(HKEY_LOCAL_MACHINE, key + 1, 0, KEY_READ, &hk) == 0)
				{
					char ver[128] = "", date[128] = "";
					DWORD t = 0, n1 = sizeof(ver) - 1, n2 = sizeof(date) - 1;
					regQuery(hk, "DriverVersion", NULL, &t, (LPBYTE)ver, &n1);
					regQuery(hk, "DriverDate", NULL, &t, (LPBYTE)date, &n2);
					regClose(hk);
					if (ver[0]) drv = Fmt("driver %s (%s)", ver, date[0] ? date : "no date");
				}
			}
			std::string mode = "mode not readable";
			DEVMODEA dm;
			memset(&dm, 0, sizeof(dm));
			dm.dmSize = sizeof(dm);
			if (enumDs && enumDs(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm))
				mode = Fmt("%lux%lu, %lu bpp, %lu Hz", dm.dmPelsWidth, dm.dmPelsHeight, dm.dmBitsPerPel,
				           dm.dmDisplayFrequency);
			s += Fmt("display %lu%s: %s; %s; %s\n", i, primary ? " (primary)" : "", dd.DeviceString, drv.c_str(),
			         mode.c_str());
		}
	}
	else
		s += "display: user32 not available\n";
	return s;
}

std::string Tail(const std::string& path, size_t n, std::string* why)
{
	std::string out;
	bool cut = false;
	DWORD err = 0;
	if (!ReadCappedW(ProxySession::Widen(path), n, out, &cut, &err))
	{
		if (why) *why = Fmt("cannot be read (error %lu)", err);
		return std::string();
	}
	return out;
}

namespace
{
bool WordCharA(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

// word as a whole word of [line, end): no letter or digit right before or after it (an underscore separates)
bool HasWord(const std::string& s, size_t line, size_t end, const char* word)
{
	const size_t n = strlen(word);
	for (size_t at = s.find(word, line); at != std::string::npos && at + n <= end; at = s.find(word, at + 1))
		if ((at == line || !WordCharA(s[at - 1])) && (at + n == end || !WordCharA(s[at + n]))) return true;
	return false;
}
}  // namespace

std::string LastFailureLine(const std::string& text, size_t maxLen)
{
	size_t end = text.size();
	while (end > 0)
	{
		const size_t nl = text.rfind('\n', end - 1);
		const size_t start = nl == std::string::npos ? 0 : nl + 1;
		if (start < end && (HasWord(text, start, end, "FAILED") || HasWord(text, start, end, "LOUD")))
		{
			std::string line = text.substr(start, end - start);
			for (size_t i = 0; i < line.size(); ++i)
				if ((unsigned char)line[i] < 0x20 || line[i] == 0x7F) line[i] = ' ';
			while (!line.empty() && line[line.size() - 1] == ' ') line.erase(line.size() - 1);
			if (line.size() > maxLen)
			{
				size_t cut = maxLen;
				while (cut > 0 && ((unsigned char)line[cut] & 0xC0) == 0x80) --cut;  // not inside a UTF-8 sequence
				line = line.substr(0, cut) + " [...]";
			}
			return line;
		}
		if (nl == std::string::npos) break;
		end = nl;
	}
	return std::string();
}

std::string ServerReason(const std::string& text, size_t maxLen)
{
	static const char kVerdict[] = "result: ";
	static const char kFailed[] = "FAILED: ";
	static const char kTail[] = "; remote mode, section ";
	const size_t nv = sizeof(kVerdict) - 1, nf = sizeof(kFailed) - 1;
	size_t end = text.size();
	while (end > 0)
	{
		const size_t nl = text.rfind('\n', end - 1);
		const size_t start = nl == std::string::npos ? 0 : nl + 1;
		std::string line = text.substr(start, end - start);
		if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
		size_t at = std::string::npos;  // where "result: " begins: the line's start, or right after its "[ time] " stamp
		if (line.compare(0, nv, kVerdict) == 0)
			at = 0;
		else if (!line.empty() && line[0] == '[')
		{
			const size_t close = line.find(']');
			if (close != std::string::npos && line.compare(close + 1, 1 + nv, std::string(" ") + kVerdict) == 0) at = close + 2;
		}
		if (at != std::string::npos)
		{
			// the last verdict decides: a later "result: passed" (or a FAILED without a reason) means no reason
			if (line.compare(at + nv, nf, kFailed) != 0) return std::string();
			std::string why = line.substr(at + nv + nf);
			const size_t tail = why.rfind(kTail);
			if (tail != std::string::npos) why.erase(tail);
			for (size_t i = 0; i < why.size(); ++i)
				if ((unsigned char)why[i] < 0x20 || why[i] == 0x7F) why[i] = ' ';
			while (!why.empty() && why[why.size() - 1] == ' ') why.erase(why.size() - 1);
			while (!why.empty() && why[0] == ' ') why.erase(0, 1);
			if (why.size() > maxLen)
			{
				size_t cut = maxLen;
				while (cut > 0 && ((unsigned char)why[cut] & 0xC0) == 0x80) --cut;  // not inside a UTF-8 sequence
				why = why.substr(0, cut) + " [...]";
			}
			return why;
		}
		if (nl == std::string::npos) break;
		end = nl;
	}
	return std::string();
}

bool Root(std::wstring* out)
{
	std::wstring cfg;
	if (!ProxySession::ConfigIniPath(&cfg)) return false;
	const size_t cut = cfg.find_last_of(L"\\/");
	if (cut == std::wstring::npos) return false;
	*out = cfg.substr(0, cut) + L"\\VP64\\reports";
	return true;
}

// ================================================================================================ the report
bool Write(const Input& in, const std::wstring& root, std::wstring* dir, std::vector<std::string>* lines)
{
	SYSTEMTIME t;
	GetLocalTime(&t);
	wchar_t name[64];
	_snwprintf_s(name, 64, _TRUNCATE, L"%04u%02u%02u-%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
	             t.wSecond);
	*dir = root + L"\\" + name;
	DWORD err = 0;
	if (!MakeDirsW(root, &err))
	{
		lines->push_back(Scrub(Fmt("report: FAILED: the reports folder %s cannot be created (error %lu); no report",
		                           ProxySession::Utf8(root).c_str(), err)));
		return false;
	}
	bool made = false;
	for (int n = 1; n < 100 && !made; ++n)
	{
		std::wstring d = root + L"\\" + name;
		if (n > 1)
		{
			wchar_t suf[16];
			_snwprintf_s(suf, 16, _TRUNCATE, L"-%d", n);
			d += suf;
		}
		if (CreateDirectoryW(d.c_str(), NULL))
		{
			*dir = d;
			made = true;
		}
		else if (GetLastError() != ERROR_ALREADY_EXISTS)
		{
			err = GetLastError();
			break;
		}
	}
	if (!made)
	{
		lines->push_back(Scrub(Fmt("report: FAILED: no report folder can be created under %s (error %lu); no report",
		                           ProxySession::Utf8(root).c_str(), err)));
		return false;
	}
	const std::wstring& d = *dir;
	std::vector<std::string> attached;
	if (!in.summary.empty())
	{
		if (WriteAll(d + L"\\popup-summary.txt", Scrub(in.summary) + "\r\n", &err))
			attached.push_back("popup-summary.txt: the popup's summary line");
		else
			lines->push_back(Fmt("report: popup-summary.txt could not be written (error %lu)", err));
	}

	// the logs' tails; issue 0186: server.log's last FAILED or LOUD line is quoted under [failure]
	std::string serverLine;  // "" = no server.log among the tails
	std::string serverWhy;   // ticket 0511: the server's own reason for its end ("" = none)
	for (size_t i = 0; i < in.tails.size(); ++i)
	{
		std::string why;
		const std::string text = Tail(in.tails[i], kTailBytes, &why);
		const bool serverLog = _stricmp(BaseName(in.tails[i]).c_str(), "server.log") == 0;
		if (text.empty() && !why.empty())
		{
			attached.push_back(Scrub(Fmt("(not attached: %s %s)", in.tails[i].c_str(), why.c_str())));
			if (serverLog) serverLine = "(server.log " + why + ")";
			continue;
		}
		const std::string scrubbed = Scrub(text);
		if (serverLog)
		{
			serverLine = LastFailureLine(scrubbed, kFailLineMax);  // scrubbed before the clip (see the header)
			if (serverLine.empty())
				serverLine = Fmt("(none in the last %u KB of server.log)", (unsigned)(kTailBytes / 1024));
			serverWhy = ServerReason(scrubbed, kReasonMax);  // scrubbed before the clip, as above
		}
		std::string nm = BaseName(in.tails[i]) + ".tail.txt";
		if (GetFileAttributesW((d + L"\\" + ProxySession::Widen(nm)).c_str()) != INVALID_FILE_ATTRIBUTES)
			nm = Fmt("%u-", (unsigned)i) + nm;
		if (WriteAll(d + L"\\" + ProxySession::Widen(nm), scrubbed, &err))
			attached.push_back(Scrub(Fmt("%s: the last %u KB of %s", nm.c_str(), (unsigned)(kTailBytes / 1024),
			                             in.tails[i].c_str())));
		else
			lines->push_back(Fmt("report: %s could not be written (error %lu)", nm.c_str(), err));
	}
	// small text files copied whole (scrubbed)
	std::vector<std::wstring> copies = in.copiesW;
	for (size_t i = 0; i < in.copies.size(); ++i) copies.push_back(ProxySession::Widen(in.copies[i]));
	for (size_t i = 0; i < copies.size(); ++i)
	{
		std::string text;
		bool cut = false;
		const std::string shown = ProxySession::Utf8(copies[i]);
		if (!ReadCappedW(copies[i], kCopyMax, text, &cut, &err))
		{
			attached.push_back(Scrub(Fmt("(not attached: %s cannot be read, error %lu)", shown.c_str(), err)));
			continue;
		}
		std::wstring nm = BaseNameW(copies[i]);
		if (GetFileAttributesW((d + L"\\" + nm).c_str()) != INVALID_FILE_ATTRIBUTES)
			nm = ProxySession::Widen(Fmt("%u-", (unsigned)i)) + nm;
		if (WriteAll(d + L"\\" + nm, Scrub(text), &err))
			attached.push_back(Scrub(ProxySession::Utf8(nm) + ": a copy of " + shown + (cut ? " (its last 256 KB)" : "")));
		else
			lines->push_back(Fmt("report: a copy of %s could not be written (error %lu)", ProxySession::Utf8(nm).c_str(), err));
	}
	// The server's own minidump (issue 3522) is NOT copied (MC #577, issue 3523): reports are public GitHub issues (D122),
	// and a dump holds the game state and strings that may name the user. report.txt names it, its size and time.
	std::vector<std::string> dumps;
	if (!in.runDir.empty())
	{
		WIN32_FIND_DATAW fd;
		const std::wstring run = ProxySession::Widen(in.runDir);
		HANDLE h = FindFirstFileW((run + L"\\*.dmp").c_str(), &fd);
		if (h != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
				FILETIME lt;
				SYSTEMTIME st;
				memset(&st, 0, sizeof(st));
				if (FileTimeToLocalFileTime(&fd.ftLastWriteTime, &lt)) FileTimeToSystemTime(&lt, &st);
				dumps.push_back(Scrub(Fmt("%s\\%s: %I64u bytes, written %04u-%02u-%02u %02u:%02u:%02u (local time)",
				                          in.runDir.c_str(), ProxySession::Utf8(fd.cFileName).c_str(),
				                          ((unsigned __int64)fd.nFileSizeHigh << 32) | fd.nFileSizeLow, st.wYear,
				                          st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond)));
			} while (FindNextFileW(h, &fd));
			FindClose(h);
		}
	}

	// report.txt
	std::string r;
	r += "VP64 error report (Vox Populi 64-bit patch; decision D119, issue 3521)\n";
	r += Fmt("written %04u-%02u-%02u %02u:%02u:%02u (local time)\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
	         t.wSecond);
	r += "folder: " + ProxySession::Utf8(d) + "\n";
	if (in.nonFatal)   // ticket 0487: a failure the game goes on after (the stage names what was not done)
		r += "\nA part of the 64-bit patch could not do its work; the game went on with the 64-bit patch (see [failure]\n"
		     "for what was left undone). Please send this folder in (the popup says where).\n";
	else if (in.stage == "off-switch")   // ticket 0635 (MC #9080): the patch switched off with config.ini in its form
		r += "\nThe patch is switched off, but config.ini was still in the form the patch uses, so the game was ended before "
		     "it drew a broken map; config.ini is put back to your own settings and the next start is the normal game.\n";
	else if (in.bootEnded && in.stage == "fallback-dll")   // ticket 0721: the 32-bit DLL could not be loaded
		r += "\nThe game's own 32-bit DLL, which the patch loads in place of its 64-bit part, could not be loaded (see\n"
		     "[failure]), so there was no game to start and the game was ended at once. The installation is partial or\n"
		     "damaged (a file missing, or taken away by an antivirus program): install the patch again. Please send this\n"
		     "folder in (the popup says where).\n";
	else if (in.bootEnded)   // ticket 0721: the 64-bit boot failed and no 32-bit DLL was there to go on with
		r += "\nThe 64-bit part of the patch failed at the start of the game (see [failure]) and there was no 32-bit DLL\n"
		     "to go on with, so the game was ended at once. The patch stays on: the next start uses the 64-bit patch\n"
		     "again, unless \"Remove 64bit patch\" was chosen in the popup (helper.log in this folder says which). Please\n"
		     "send this folder in (the popup says where).\n";
	else if (in.stage == "exe-build" && in.d33)   // ticket 0588: kept switched off after it (D240, ticket 0682)
		r += "\nThe 64-bit part of the patch failed while the game ran with the patch's memory settings, so the game was\n"
		     "ended (the 32-bit game cannot play under them). The patch is switched off and your own settings are put back\n"
		     "until you switch it on again; the next start is the stock game. Please send this folder in (the popup says\n"
		     "where).\n";
	else if (in.d33)   // D240 (ticket 0682): the patch stays on unless the player chose "Remove 64bit patch"
		r += "\nThe 64-bit part of the patch failed while the game ran with the patch's memory settings, so the game was\n"
		     "ended (the 32-bit game cannot play under them). The patch stays on: the next start uses the 64-bit patch\n"
		     "again, unless \"Remove 64bit patch\" was chosen in the popup (helper.log in this folder says which). Please\n"
		     "send this folder in (the popup says where).\n";
	else
		r += "\nThe 64-bit part of the patch failed. At the start of a game the game went on as the stock game (the 32-bit\n"
		     "DLL); in a game the server's end ended the game. Please send this folder in (the popup says where).\n";
	r += "\n[failure]\n";
	r += "stage: " + in.stage + "\n";
	r += "code: " + in.code + "\n";
	for (size_t i = 0; i < in.details.size(); ++i) r += in.details[i] + "\n";
	if (!serverLine.empty()) r += "server.log, last FAILED or LOUD line: " + serverLine + "\n";
	// ticket 0511: vp64helper shows this line first in its popup (the stage and code above move to its details)
	if (!serverWhy.empty()) r += "server's reason: " + serverWhy + "\n";
	r += "\n[patch]\n";
	r += "proxy: " + in.patch + "\n";
	r += "pack release: " + (in.packRelease.empty() ? std::string("(no manifest read)") : in.packRelease) + "\n";
	r += "\n[hashes] (SHA-256 found; expected = the pack's manifest)\n";
	for (size_t i = 0; i < in.hashes.size(); ++i)
	{
		const Hashed& hh = in.hashes[i];
		std::string got, why;
		const bool ok = ProxyPack::Sha256File(hh.path, got, why, NULL);
		r += hh.label + ": " + hh.path + "\n";
		r += "  found:    " + (ok ? got : "(" + why + ")") + "\n";
		if (!hh.expected.empty())
			r += "  expected: " + hh.expected + (hh.expectedNote.empty() ? "" : " (" + hh.expectedNote + ")") +
			     (ok ? (got == hh.expected ? "  [match]" : "  [MISMATCH]") : "") + "\n";
		else if (!hh.expectedNote.empty())
			r += "  expected: " + hh.expectedNote + "\n";
	}
	r += "\n[session]\n";
	for (size_t i = 0; i < in.session.size(); ++i) r += in.session[i] + "\n";
	r += "\n[exe]\n" + ExeText();
	r += "\n[mods] (activated, from the game's mods database)\n";
	r += in.mods.empty() ? "(not read: " + in.modsWhy + ")\n" : in.mods;
	r += "\n[system]\n" + SystemText();
	r += "\n[attachments] (in this folder)\n";
	for (size_t i = 0; i < attached.size(); ++i) r += attached[i] + "\n";
	if (attached.empty()) r += "(none)\n";
	r += "\n[server minidump] (not in this folder)\n";
	if (dumps.empty())
		r += "(none: the server wrote no minidump of itself)\n";
	else
	{
		for (size_t i = 0; i < dumps.size(); ++i) r += dumps[i] + "\n";
		r += "The server wrote this minidump when it crashed. It is NOT in this folder: it holds your game state and\n"
		     "may hold paths with your Windows user name, and reports are public. Do not attach it to a public issue; the\n"
		     "developers may ask for it.\n";
	}
	r += "\n[left out]\n"
	     "Your Steam id, computer name and Windows user name (every path is written relative to %USERPROFILE%), your\n"
	     "saves, any dump of the game's own process, and the server's minidump (named above, not copied).\n";
	if (!WriteAll(d + L"\\report.txt", Crlf(Scrub(r)), &err))
	{
		lines->push_back(Scrub(Fmt("report: FAILED: %s\\report.txt could not be written (error %lu)",
		                           ProxySession::Utf8(d).c_str(), err)));
		return false;
	}
	lines->push_back(Scrub(Fmt("report: written to %s (report.txt and %u attachment line(s))", ProxySession::Utf8(d).c_str(),
	                           (unsigned)attached.size())));
	return true;
}
}  // namespace ProxyReport
