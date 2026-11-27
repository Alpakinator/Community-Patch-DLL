// srv_exe_crash.cpp - D118 for an EXE crash (ticket 0418): see srv_exe_crash.h. Hand-written.
// SPDX-License-Identifier: GPL-3.0-or-later
// The report folder has the form of the proxy's D119 report (proxy/src/proxy_report.cpp, issue 3521): popup-summary.txt,
// report.txt with the sections [failure] (stage exe-crash, code, details, server.log's last FAILED or LOUD line, issue
// 0186), [patch], [hashes], [session], [exe], [mods], [system], [attachments], [server minidump], [left out]; the logs'
// tails (*.tail.txt, 64 KB each) and small text files copied whole; every text scrubbed of the profile folder, the user
// and computer names and SteamID64s. The scrub and the FAILED/LOUD line follow proxy_report.cpp's rules (same project,
// GPL-3.0-or-later); the proxy's code is not linked here (x86 VC9 STL settings, the EXE-only mask reads).
#include "srv_exe_crash.h"
// ticket 4966: the build the report names; by path, so every test builder of this file finds it without a new /I
#include "../../../exebuild/include/vp_exe_build.h"
#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

void SrvLog(const char* szFormat, ...);
const char* SrvRunDir();
std::string SrvCfgStr(const char* szKey, const char* szDefault);
double SrvNow();
void SrvBuildIdText(char* out, size_t outSize);  // srv_buildid.cpp

namespace SrvExeCrash
{
namespace
{
const size_t kTailBytes = 64 * 1024;
const size_t kCopyMax = 256 * 1024;
const size_t kFailLineMax = 400;

HANDLE s_hPeer = NULL;
uint32_t s_uPeerPid = 0;
std::wstring s_strPeerImage;
double s_tAttach = -1.0, s_tBoot = -1.0, s_tLoad = -1.0;
volatile LONG s_lLoads = 0;
volatile LONG s_lState = 0;  // 0 not run, 1 running, 2 done
std::string s_strResult;

// test_exe_kill
enum KillEvent { kKillNone = 0, kKillAttach, kKillBoot, kKillLoad };
KillEvent s_killEvent = kKillNone;
DWORD s_dwKillMs = 0;
DWORD s_dwKillCode = 0xC0000005u;
volatile LONG s_lKillArmed = 0;

std::string Fmt(const char* fmt, ...)
{
	char buf[2048];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return buf;
}

std::wstring FromCp(const std::string& s, UINT cp)
{
	if(s.empty())
		return std::wstring();
	const int n = MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), NULL, 0);
	std::wstring w(n > 0 ? n : 0, L'\0');
	if(n > 0)
		MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), &w[0], n);
	return w;
}
std::wstring Acp(const std::string& s) { return FromCp(s, CP_ACP); }
std::string ToCp(const std::wstring& w, UINT cp)
{
	if(w.empty())
		return std::string();
	const int n = WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), NULL, 0, NULL, NULL);
	std::string a(n > 0 ? n : 0, '\0');
	if(n > 0)
		WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), &a[0], n, NULL, NULL);
	return a;
}
std::string U8(const std::wstring& w) { return ToCp(w, CP_UTF8); }

std::wstring EnvW(const wchar_t* name)
{
	wchar_t buf[1024];
	const DWORD n = GetEnvironmentVariableW(name, buf, 1024);
	return (n > 0 && n < 1024) ? std::wstring(buf, n) : std::wstring();
}

std::wstring DirOf(const std::wstring& p)
{
	const size_t cut = p.find_last_of(L"\\/");
	return cut == std::wstring::npos ? std::wstring() : p.substr(0, cut);
}
std::wstring BaseOf(const std::wstring& p)
{
	const size_t cut = p.find_last_of(L"\\/");
	return cut == std::wstring::npos ? p : p.substr(cut + 1);
}
bool IsFile(const std::wstring& p)
{
	const DWORD a = GetFileAttributesW(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// ---- privacy (proxy_report.cpp's rules) ----------------------------------------------------------------------------
char LowerA(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }
bool AlnumA(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (unsigned char)c >= 0x80;
}
bool MatchAt(const std::string& s, size_t at, const std::string& what)
{
	if(at + what.size() > s.size())
		return false;
	for(size_t i = 0; i < what.size(); ++i)
		if(LowerA(s[at + i]) != LowerA(what[i]))
			return false;
	return true;
}
std::string ReplaceAll(const std::string& s, const std::string& what, const std::string& with, bool bounded)
{
	if(what.empty())
		return s;
	std::string out;
	out.reserve(s.size());
	size_t i = 0;
	while(i < s.size())
	{
		if(MatchAt(s, i, what) &&
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
std::string ScrubSteamIds(const std::string& s)
{
	std::string out;
	size_t i = 0;
	while(i < s.size())
	{
		if(i + 17 <= s.size() && s.compare(i, 7, "7656119") == 0 && (i == 0 || !(s[i - 1] >= '0' && s[i - 1] <= '9')))
		{
			size_t j = i;
			while(j < s.size() && s[j] >= '0' && s[j] <= '9')
				++j;
			if(j - i == 17)
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
std::string Scrub(const std::string& text)
{
	std::string s = text;
	std::wstring prof = EnvW(L"USERPROFILE");
	while(!prof.empty() && (prof[prof.size() - 1] == L'\\' || prof[prof.size() - 1] == L'/'))
		prof.erase(prof.size() - 1);
	if(prof.size() > 3)
	{
		const std::string forms[2] = {U8(prof), ToCp(prof, CP_ACP)};
		for(int f = 0; f < 2; ++f)
		{
			std::string p = forms[f], fwd = p, dbl;
			for(size_t i = 0; i < fwd.size(); ++i)
				if(fwd[i] == '\\')
					fwd[i] = '/';
			for(size_t i = 0; i < p.size(); ++i)
				dbl += p[i] == '\\' ? std::string("\\\\") : std::string(1, p[i]);
			s = ReplaceAll(s, dbl, "%USERPROFILE%", false);
			s = ReplaceAll(s, p, "%USERPROFILE%", false);
			s = ReplaceAll(s, fwd, "%USERPROFILE%", false);
		}
	}
	const wchar_t* names[3] = {L"USERNAME", L"COMPUTERNAME", L"USERDOMAIN"};
	const char* subst[3] = {"<user>", "<computer>", "<computer>"};
	for(int k = 0; k < 3; ++k)
	{
		const std::wstring v = EnvW(names[k]);
		if(v.empty())
			continue;
		s = ReplaceAll(s, U8(v), subst[k], true);
		s = ReplaceAll(s, ToCp(v, CP_ACP), subst[k], true);
	}
	return ScrubSteamIds(s);
}

bool WordCharA(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
bool HasWord(const std::string& s, size_t line, size_t end, const char* word)
{
	const size_t n = strlen(word);
	for(size_t at = s.find(word, line); at != std::string::npos && at + n <= end; at = s.find(word, at + 1))
		if((at == line || !WordCharA(s[at - 1])) && (at + n == end || !WordCharA(s[at + n])))
			return true;
	return false;
}
// issue 0186's rule: the last line holding the word FAILED or LOUD, one line, clipped
std::string LastFailureLine(const std::string& text, size_t maxLen)
{
	size_t end = text.size();
	while(end > 0)
	{
		const size_t nl = text.rfind('\n', end - 1);
		const size_t start = nl == std::string::npos ? 0 : nl + 1;
		if(start < end && (HasWord(text, start, end, "FAILED") || HasWord(text, start, end, "LOUD")))
		{
			std::string line = text.substr(start, end - start);
			for(size_t i = 0; i < line.size(); ++i)
				if((unsigned char)line[i] < 0x20 || line[i] == 0x7F)
					line[i] = ' ';
			while(!line.empty() && line[line.size() - 1] == ' ')
				line.erase(line.size() - 1);
			if(line.size() > maxLen)
			{
				size_t cut = maxLen;
				while(cut > 0 && ((unsigned char)line[cut] & 0xC0) == 0x80)
					--cut;
				line = line.substr(0, cut) + " [...]";
			}
			return line;
		}
		if(nl == std::string::npos)
			break;
		end = nl;
	}
	return std::string();
}

// ---- files ---------------------------------------------------------------------------------------------------------
bool ReadCapped(const std::wstring& path, size_t cap, std::string& out, bool* cut, DWORD* err)
{
	*cut = false;
	HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(h == INVALID_HANDLE_VALUE)
	{
		*err = GetLastError();
		return false;
	}
	LARGE_INTEGER sz;
	if(!GetFileSizeEx(h, &sz))
	{
		*err = GetLastError();
		CloseHandle(h);
		return false;
	}
	LONGLONG start = 0;
	if(sz.QuadPart > (LONGLONG)cap)
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
	if(!ok)
		return false;
	out.resize(got);
	if(*cut)
	{
		const size_t nl = out.find('\n');
		if(nl != std::string::npos)
			out.erase(0, nl + 1);
	}
	return true;
}

bool WriteAll(const std::wstring& path, const std::string& bytes, DWORD* err)
{
	HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if(h == INVALID_HANDLE_VALUE)
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
	for(size_t i = 0; i < s.size(); ++i)
	{
		if(s[i] == '\n' && (i == 0 || s[i - 1] != '\r'))
			o += '\r';
		o += s[i];
	}
	return o;
}

bool MakeDirs(const std::wstring& dir, DWORD* err)
{
	if(dir.empty())
		return false;
	const DWORD a = GetFileAttributesW(dir.c_str());
	if(a != INVALID_FILE_ATTRIBUTES)
	{
		if(a & FILE_ATTRIBUTE_DIRECTORY)
			return true;
		*err = ERROR_ALREADY_EXISTS;
		return false;
	}
	const size_t cut = dir.find_last_of(L"\\/");
	if(cut != std::wstring::npos && cut > 2 && !MakeDirs(dir.substr(0, cut), err))
		return false;
	if(CreateDirectoryW(dir.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS)
		return true;
	*err = GetLastError();
	return false;
}

// SHA-256 of a file as lower-case hex, bcrypt.dll loaded by name (no import added); "(why)" otherwise.
std::string Sha256File(const std::wstring& path)
{
	typedef LONG(WINAPI * OpenAlgFn)(void**, const wchar_t*, const wchar_t*, ULONG);
	typedef LONG(WINAPI * CreateHashFn)(void*, void**, unsigned char*, ULONG, unsigned char*, ULONG, ULONG);
	typedef LONG(WINAPI * HashDataFn)(void*, unsigned char*, ULONG, ULONG);
	typedef LONG(WINAPI * FinishHashFn)(void*, unsigned char*, ULONG, ULONG);
	typedef LONG(WINAPI * DestroyHashFn)(void*);
	typedef LONG(WINAPI * CloseAlgFn)(void*, ULONG);
	HMODULE bc = LoadLibraryW(L"bcrypt.dll");
	if(!bc)
		return "(bcrypt.dll not available)";
	OpenAlgFn openAlg = (OpenAlgFn)(void*)GetProcAddress(bc, "BCryptOpenAlgorithmProvider");
	CreateHashFn createHash = (CreateHashFn)(void*)GetProcAddress(bc, "BCryptCreateHash");
	HashDataFn hashData = (HashDataFn)(void*)GetProcAddress(bc, "BCryptHashData");
	FinishHashFn finishHash = (FinishHashFn)(void*)GetProcAddress(bc, "BCryptFinishHash");
	DestroyHashFn destroyHash = (DestroyHashFn)(void*)GetProcAddress(bc, "BCryptDestroyHash");
	CloseAlgFn closeAlg = (CloseAlgFn)(void*)GetProcAddress(bc, "BCryptCloseAlgorithmProvider");
	if(!openAlg || !createHash || !hashData || !finishHash || !destroyHash || !closeAlg)
		return "(bcrypt functions missing)";
	HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(f == INVALID_HANDLE_VALUE)
		return Fmt("(cannot be read, error %lu)", GetLastError());
	void* alg = NULL;
	void* hash = NULL;
	std::string result;
	if(openAlg(&alg, L"SHA256", NULL, 0) != 0 || createHash(alg, &hash, NULL, 0, NULL, 0, 0) != 0)
		result = "(bcrypt SHA256 not available)";
	else
	{
		std::vector<unsigned char> buf(1 << 20);
		DWORD got = 0;
		bool ok = true;
		while(ReadFile(f, &buf[0], (DWORD)buf.size(), &got, NULL) && got > 0)
			if(hashData(hash, &buf[0], got, 0) != 0)
			{
				ok = false;
				break;
			}
		unsigned char d[32];
		if(ok && finishHash(hash, d, 32, 0) == 0)
			for(int i = 0; i < 32; ++i)
				result += Fmt("%02x", d[i]);
		else
			result = "(hash failed)";
	}
	if(hash)
		destroyHash(hash);
	if(alg)
		closeAlg(alg, 0);
	CloseHandle(f);
	return result;
}

// The PE TimeDateStamp of an image file; false when the file is not readable as a PE image.
// Ticket 4966: the EXE file's PE stamp and SizeOfImage and the build they name (exebuild/include/vp_exe_build.h:
// kVpExeBuildUnknown for any EXE the patch does not know). False when the file is unreadable or not a 32-bit PE.
bool PeBuild(const std::wstring& path, uint32_t* stamp, uint32_t* sizeOfImage, int* build)
{
	HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(f == INVALID_HANDLE_VALUE)
		return false;
	unsigned char b[4096];
	DWORD got = 0;
	const BOOL ok = ReadFile(f, b, sizeof(b), &got, NULL);
	CloseHandle(f);
	if(!ok)
		return false;
	*build = VpExeBuildOfImage(b, got, stamp, sizeOfImage);
	return *stamp != 0 || *sizeOfImage != 0;
}

// ---- where things are ----------------------------------------------------------------------------------------------
std::wstring ServerDir()
{
	wchar_t p[MAX_PATH * 2];
	const DWORD n = GetModuleFileNameW(NULL, p, MAX_PATH * 2);
	return n ? DirOf(std::wstring(p, n)) : std::wstring();
}

// The proxy's folder: server.cfg proxy_dir, else the parent of server.exe's folder (D28: <proxy dir>\VP64\server.exe).
std::wstring ProxyDir(std::string* how)
{
	const std::string cfg = SrvCfgStr("proxy_dir", "");
	if(!cfg.empty())
	{
		*how = "server.cfg proxy_dir";
		return Acp(cfg);
	}
	*how = "the parent of server.exe's folder (D28 layout)";
	return DirOf(ServerDir());
}

// vpproxy.ini: VPPROXY_INI (the proxy's own override, inherited from the EXE), else <proxy dir>\vpproxy.ini.
std::wstring ProxyIni(const std::wstring& proxyDir)
{
	const std::wstring env = EnvW(L"VPPROXY_INI");
	return env.empty() ? proxyDir + L"\\vpproxy.ini" : env;
}

// A key's value in an ini text (key=value, '#'/';' comments, quotes stripped; the last line wins, as the proxy reads
// the server.cfg it appends to, and the first for vpproxy.ini does not matter: the proxy keeps the last too).
bool IniValue(const std::string& text, const char* key, std::string* value)
{
	bool found = false;
	size_t pos = 0;
	while(pos < text.size())
	{
		size_t e = text.find('\n', pos);
		if(e == std::string::npos)
			e = text.size();
		std::string line = text.substr(pos, e - pos);
		pos = e + 1;
		while(!line.empty() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == ' ' || line[line.size() - 1] == '\t'))
			line.erase(line.size() - 1);
		size_t b = 0;
		while(b < line.size() && (line[b] == ' ' || line[b] == '\t'))
			++b;
		if(b >= line.size() || line[b] == '#' || line[b] == ';')
			continue;
		const size_t eq = line.find('=', b);
		if(eq == std::string::npos)
			continue;
		std::string k = line.substr(b, eq - b);
		while(!k.empty() && (k[k.size() - 1] == ' ' || k[k.size() - 1] == '\t'))
			k.erase(k.size() - 1);
		if(k != key)
			continue;
		std::string v = line.substr(eq + 1);
		size_t vb = 0;
		while(vb < v.size() && (v[vb] == ' ' || v[vb] == '\t'))
			++vb;
		v = v.substr(vb);
		if(v.size() >= 2 && v[0] == '"' && v[v.size() - 1] == '"')
			v = v.substr(1, v.size() - 2);
		*value = v;
		found = true;
	}
	return found;
}

// The player's folder (Documents\My Games\Sid Meier's Civilization 5, the folder of config.ini, MODS and Logs), found as
// the proxy finds it inside the EXE: VPPROXY_TEST_CONFIG_INI (inherited), the run folder <player>\Logs\VP64\run\<boot>
// (issue 0148's run root), the options file beside config.ini (server.cfg options_file, unless VP64_OPTIONS moved it),
// else SHGetFolderPathW(CSIDL_PERSONAL) of the server's own process.
std::wstring PlayerDir(std::string* how)
{
	const std::wstring env = EnvW(L"VPPROXY_TEST_CONFIG_INI");
	if(!env.empty())
	{
		*how = "VPPROXY_TEST_CONFIG_INI";
		return DirOf(env);
	}
	const std::wstring run = Acp(SrvRunDir() ? SrvRunDir() : "");
	{
		// <player>\Logs\VP64\run\<boot>
		std::wstring d = run;
		while(!d.empty() && (d[d.size() - 1] == L'\\' || d[d.size() - 1] == L'/'))
			d.erase(d.size() - 1);
		const std::wstring runRoot = DirOf(d), vp64 = DirOf(runRoot), logs = DirOf(vp64);
		if(!logs.empty() && _wcsicmp(BaseOf(runRoot).c_str(), L"run") == 0 && _wcsicmp(BaseOf(vp64).c_str(), L"VP64") == 0 &&
		   _wcsicmp(BaseOf(logs).c_str(), L"Logs") == 0 && !DirOf(logs).empty())
		{
			*how = "the run folder (<player>\\Logs\\VP64\\run\\<boot>)";
			return DirOf(logs);
		}
	}
	const std::string opt = SrvCfgStr("options_file", "");
	if(!opt.empty() && EnvW(L"VP64_OPTIONS").empty())
	{
		*how = "server.cfg options_file (beside config.ini)";
		return DirOf(FromCp(opt, CP_UTF8));
	}
	typedef HRESULT(WINAPI * PFN)(HWND, int, HANDLE, DWORD, LPWSTR);
	HMODULE sh = LoadLibraryW(L"shell32.dll");
	PFN f = sh ? (PFN)(void*)GetProcAddress(sh, "SHGetFolderPathW") : NULL;
	wchar_t w[MAX_PATH];
	if(f && f(NULL, 0x0005 /* CSIDL_PERSONAL */, NULL, 0, w) == S_OK)
	{
		*how = "the Documents folder of the server's process";
		return std::wstring(w) + L"\\My Games\\Sid Meier's Civilization 5";
	}
	*how = "not found";
	return std::wstring();
}

// ---- the report's sections -----------------------------------------------------------------------------------------
std::string SystemText()
{
	std::string s;
	typedef LONG(WINAPI * RtlGetVersionFn)(OSVERSIONINFOEXW*);
	HMODULE nt = GetModuleHandleW(L"ntdll.dll");
	RtlGetVersionFn rgv = nt ? (RtlGetVersionFn)(void*)GetProcAddress(nt, "RtlGetVersion") : NULL;
	OSVERSIONINFOEXW vi;
	memset(&vi, 0, sizeof(vi));
	vi.dwOSVersionInfoSize = sizeof(vi);
	if(rgv && rgv(&vi) == 0)
		s += Fmt("windows: %lu.%lu build %lu, service pack %u.%u, product type %u\n", vi.dwMajorVersion, vi.dwMinorVersion,
		         vi.dwBuildNumber, vi.wServicePackMajor, vi.wServicePackMinor, vi.wProductType);
	else
		s += "windows: RtlGetVersion not available\n";
	SYSTEM_INFO si;
	GetNativeSystemInfo(&si);
	s += Fmt("processors: %lu (native architecture %u); written by the 64-bit server\n", si.dwNumberOfProcessors,
	         si.wProcessorArchitecture);
	MEMORYSTATUSEX ms;
	ms.dwLength = sizeof(ms);
	if(GlobalMemoryStatusEx(&ms))
		s += Fmt("memory: physical %llu MB, available %llu MB; page file (commit) %llu MB, available %llu MB\n",
		         (unsigned long long)(ms.ullTotalPhys >> 20), (unsigned long long)(ms.ullAvailPhys >> 20),
		         (unsigned long long)(ms.ullTotalPageFile >> 20), (unsigned long long)(ms.ullAvailPageFile >> 20));
	for(DWORD i = 0; i < 16; ++i)
	{
		DISPLAY_DEVICEA dd;
		memset(&dd, 0, sizeof(dd));
		dd.cb = sizeof(dd);
		if(!EnumDisplayDevicesA(NULL, i, &dd, 0))
			break;
		const bool primary = (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0;
		const bool attached = (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0;
		if(!attached && !primary)
			continue;
		std::string drv = "driver version not readable";
		const char* key = strstr(dd.DeviceKey, "\\System\\");
		if(!key)
			key = strstr(dd.DeviceKey, "\\SYSTEM\\");
		HKEY hk;
		if(key && RegOpenKeyExA(HKEY_LOCAL_MACHINE, key + 1, 0, KEY_READ, &hk) == 0)
		{
			char ver[128] = "", date[128] = "";
			DWORD t = 0, n1 = sizeof(ver) - 1, n2 = sizeof(date) - 1;
			RegQueryValueExA(hk, "DriverVersion", NULL, &t, (LPBYTE)ver, &n1);
			RegQueryValueExA(hk, "DriverDate", NULL, &t, (LPBYTE)date, &n2);
			RegCloseKey(hk);
			if(ver[0])
				drv = Fmt("driver %s (%s)", ver, date[0] ? date : "no date");
		}
		std::string mode = "mode not readable";
		DEVMODEA dm;
		memset(&dm, 0, sizeof(dm));
		dm.dmSize = sizeof(dm);
		if(EnumDisplaySettingsA(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm))
			mode = Fmt("%lux%lu, %lu bpp, %lu Hz", dm.dmPelsWidth, dm.dmPelsHeight, dm.dmBitsPerPel, dm.dmDisplayFrequency);
		s += Fmt("display %lu%s: %s; %s; %s\n", i, primary ? " (primary)" : "", dd.DeviceString, drv.c_str(), mode.c_str());
	}
	return s;
}

std::string FileTimeText(const FILETIME& ft)
{
	FILETIME lt;
	SYSTEMTIME st;
	memset(&st, 0, sizeof(st));
	if(FileTimeToLocalFileTime(&ft, &lt))
		FileTimeToSystemTime(&lt, &st);
	return Fmt("%04u-%02u-%02u %02u:%02u:%02u.%03u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
	           st.wMilliseconds);
}

double FtSeconds(const FILETIME& a, const FILETIME& b)  // b - a
{
	ULARGE_INTEGER x, y;
	x.LowPart = a.dwLowDateTime;
	x.HighPart = a.dwHighDateTime;
	y.LowPart = b.dwLowDateTime;
	y.HighPart = b.dwHighDateTime;
	return ((double)(LONGLONG)(y.QuadPart - x.QuadPart)) / 1e7;
}

struct Crash
{
	uint32_t code;
	const char* name;
	double tEnd;       // SrvNow() when the server saw the EXE gone (dll-fatal: when the DLL stopped)
	std::string times; // the EXE's start and end, its run time
	std::wstring proxyDir, proxyIni, playerDir, root;
	std::string proxyDirHow, playerDirHow;
	// Ticket 0422: the same report for the server's own DLL stopping (stage dll-fatal) while the EXE still runs. For an
	// EXE crash these keep 0418's texts (exeCrash true, the rest empty).
	bool exeCrash;
	std::string stage, title, summary, intro, failure;
	// Ticket 0519: the EXE ended with a code that is not an exception code (255 after Windows Error Reporting's "Close
	// program") and the proxy's crash record proved the crash; code and name are then the record's, exitCode the EXE's.
	bool byRecord;
	uint32_t exitCode;
	Crash() : code(0), name(""), tEnd(0.0), exeCrash(true), byRecord(false), exitCode(0) {}
};

std::string SinceText(double t, double tEnd, const char* what, bool exeCrash)
{
	if(t < 0)
		return Fmt("%s: none in this server's session", what);
	return Fmt("%s: %.1f s before %s", what, tEnd - t, exeCrash ? "the EXE was seen gone" : "the DLL stopped");
}

// Writes the report folder; *dir = the folder (or "" when none could be made). Lines for server.log go to SrvLog.
bool WriteReport(const Crash& c, std::wstring* dir)
{
	dir->clear();
	SYSTEMTIME t;
	GetLocalTime(&t);
	wchar_t name[64];
	_snwprintf_s(name, 64, _TRUNCATE, L"%04u%02u%02u-%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
	             t.wSecond);
	DWORD err = 0;
	const char* tag = c.exeCrash ? "exe-crash" : c.stage.c_str();  // server.log's prefix
	if(c.root.empty() || !MakeDirs(c.root, &err))
	{
		SrvLog("%s", Scrub(Fmt("%s: report: FAILED: the reports folder %s cannot be created (error %lu); no report", tag,
		                       U8(c.root).c_str(), err)).c_str());
		return false;
	}
	bool made = false;
	for(int n = 1; n < 100 && !made; ++n)
	{
		std::wstring d = c.root + L"\\" + name;
		if(n > 1)
		{
			wchar_t suf[16];
			_snwprintf_s(suf, 16, _TRUNCATE, L"-%d", n);
			d += suf;
		}
		if(CreateDirectoryW(d.c_str(), NULL))
		{
			*dir = d;
			made = true;
		}
		else if(GetLastError() != ERROR_ALREADY_EXISTS)
		{
			err = GetLastError();
			break;
		}
	}
	if(!made)
	{
		SrvLog("%s", Scrub(Fmt("%s: report: FAILED: no report folder can be created under %s (error %lu); no report", tag,
		                       U8(c.root).c_str(), err)).c_str());
		return false;
	}
	const std::wstring& d = *dir;
	std::vector<std::string> attached;
	const std::string summary =
	    c.exeCrash ? Fmt("The game itself crashed (exe-crash: 0x%08X %s); the game had to end. The 64-bit server was still "
	                     "running and wrote this report.",
	                     c.code, c.name)
	               : c.summary;
	if(WriteAll(d + L"\\popup-summary.txt", Scrub(summary) + "\r\n", &err))
		attached.push_back("popup-summary.txt: the popup's summary line");
	else
		SrvLog("%s: report: popup-summary.txt could not be written (error %lu)", tag, err);

	// the logs: every *.log of the run folder (server.log, render.log, vpproxy.log, ...) and the game's Lua.log
	if(!LogSync(2000))   // ticket 5054: the lines queued so far are on disk first
		SrvLog("%s: report: LOUD: the log writer did not write its queue within 2 s: the log tails may miss the last lines", tag);
	const std::wstring run = Acp(SrvRunDir() ? SrvRunDir() : "");
	std::vector<std::wstring> tails;
	if(!run.empty())
	{
		WIN32_FIND_DATAW fd;
		HANDLE h = FindFirstFileW((run + L"\\*.log").c_str(), &fd);
		if(h != INVALID_HANDLE_VALUE)
		{
			do
				if(!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
					tails.push_back(run + L"\\" + fd.cFileName);
			while(FindNextFileW(h, &fd));
			FindClose(h);
		}
	}
	if(!c.playerDir.empty())
		tails.push_back(c.playerDir + L"\\Logs\\Lua.log");
	std::string serverLine;
	for(size_t i = 0; i < tails.size(); ++i)
	{
		std::string text;
		bool cut = false;
		const bool serverLog = _wcsicmp(BaseOf(tails[i]).c_str(), L"server.log") == 0;
		if(!ReadCapped(tails[i], kTailBytes, text, &cut, &err))
		{
			attached.push_back(Scrub(Fmt("(not attached: %s cannot be read, error %lu)", U8(tails[i]).c_str(), err)));
			if(serverLog)
				serverLine = Fmt("(server.log cannot be read, error %lu)", err);
			continue;
		}
		const std::string scrubbed = Scrub(text);
		if(serverLog)
		{
			serverLine = LastFailureLine(scrubbed, kFailLineMax);
			if(serverLine.empty())
				serverLine = Fmt("(none in the last %u KB of server.log)", (unsigned)(kTailBytes / 1024));
		}
		std::wstring nm = BaseOf(tails[i]) + L".tail.txt";
		if(GetFileAttributesW((d + L"\\" + nm).c_str()) != INVALID_FILE_ATTRIBUTES)
			nm = Acp(Fmt("%u-", (unsigned)i)) + nm;
		if(WriteAll(d + L"\\" + nm, scrubbed, &err))
			attached.push_back(Scrub(Fmt("%s: the last %u KB of %s", U8(nm).c_str(), (unsigned)(kTailBytes / 1024),
			                             U8(tails[i]).c_str())));
		else
			SrvLog("%s: report: %s could not be written (error %lu)", tag, U8(nm).c_str(), err);
	}

	// small text files copied whole: vpproxy.ini, the pack's manifest, the options file, server.cfg, and the proxy's
	// crash record of ticket 0419 (<run>\exe-crash.txt) when the proxy wrote one
	std::vector<std::wstring> copies;
	copies.push_back(c.proxyIni);
	copies.push_back(ServerDir() + L"\\manifest.json");
	{
		const std::string opt = SrvCfgStr("options_file", "");
		if(!opt.empty())
			copies.push_back(FromCp(opt, CP_UTF8));
	}
	if(!run.empty())
		copies.push_back(run + L"\\server.cfg");
	const std::wstring crashRecord = run.empty() || !c.exeCrash ? std::wstring() : run + L"\\exe-crash.txt";
	std::string crashRecordText;
	if(!crashRecord.empty() && IsFile(crashRecord))
		copies.push_back(crashRecord);
	for(size_t i = 0; i < copies.size(); ++i)
	{
		std::string text;
		bool cut = false;
		if(!ReadCapped(copies[i], kCopyMax, text, &cut, &err))
		{
			attached.push_back(Scrub(Fmt("(not attached: %s cannot be read, error %lu)", U8(copies[i]).c_str(), err)));
			continue;
		}
		if(copies[i] == crashRecord)
			crashRecordText = Scrub(text);
		std::wstring nm = BaseOf(copies[i]);
		if(GetFileAttributesW((d + L"\\" + nm).c_str()) != INVALID_FILE_ATTRIBUTES)
			nm = Acp(Fmt("%u-", (unsigned)i)) + nm;
		if(WriteAll(d + L"\\" + nm, Scrub(text), &err))
			attached.push_back(Scrub(U8(nm) + ": a copy of " + U8(copies[i]) + (cut ? " (its last 256 KB)" : "")));
		else
			SrvLog("%s: report: a copy of %s could not be written (error %lu)", tag, U8(nm).c_str(), err);
	}

	// *.dmp of the run folder: named, never copied (MC #577, issue 3523)
	std::vector<std::string> dumps;
	if(!run.empty())
	{
		WIN32_FIND_DATAW fd;
		HANDLE h = FindFirstFileW((run + L"\\*.dmp").c_str(), &fd);
		if(h != INVALID_HANDLE_VALUE)
		{
			do
				if(!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
					dumps.push_back(Scrub(Fmt("%s\\%s: %llu bytes, written %s (local time)", U8(run).c_str(),
					                          U8(fd.cFileName).c_str(),
					                          ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow,
					                          FileTimeText(fd.ftLastWriteTime).c_str())));
			while(FindNextFileW(h, &fd));
			FindClose(h);
		}
	}

	std::string r;
	r += std::string("VP64 error report (Vox Populi 64-bit patch; decision D119, issue 3521; ") +
	     (c.exeCrash ? "an EXE crash, ticket 0418" : c.title.c_str()) + ")\n";
	r += Fmt("written %04u-%02u-%02u %02u:%02u:%02u (local time) by the 64-bit server\n", t.wYear, t.wMonth, t.wDay,
	         t.wHour, t.wMinute, t.wSecond);
	r += "folder: " + U8(d) + "\n";
	if(c.exeCrash)
		r += "\nThe game's own process (the 32-bit EXE) crashed while the 64-bit patch was running, so the game had to end.\n"
		     "The 64-bit server was still running and wrote this report. Please send this folder in (the popup says where).\n";
	else
		r += "\n" + c.intro;
	r += "\n[failure]\n";
	r += "stage: " + std::string(c.exeCrash ? "exe-crash" : c.stage.c_str()) + "\n";
	r += Fmt("code: 0x%08X %s\n", c.code, c.name);
	r += c.failure;  // ticket 0422: the check, the place, the calls that led there (empty for an EXE crash)
	r += "exe: " + U8(s_strPeerImage.empty() ? std::wstring(L"(image path not read)") : s_strPeerImage) +
	     Fmt(" (pid %u)\n", s_uPeerPid);
	r += c.times;
	r += SinceText(s_tLoad, c.tEnd, "the last save load began", c.exeCrash) +
	     Fmt(" (%ld save load(s) in this session)\n", s_lLoads);
	r += SinceText(s_tBoot, c.tEnd, "kRt_Boot (the x64 DLL loaded)", c.exeCrash) + "\n";
	r += SinceText(s_tAttach, c.tEnd, "the server attached", c.exeCrash) + "\n";
	// the proxy's crash record (ticket 0419) is about the EXE's own crash: not in a dll-fatal report
	if(c.exeCrash && !crashRecordText.empty())
	{
		std::string first = crashRecordText.substr(0, crashRecordText.find('\n'));
		while(!first.empty() && (first[first.size() - 1] == '\r' || first[first.size() - 1] == ' '))
			first.erase(first.size() - 1);
		r += "the proxy's crash record (exe-crash.txt, ticket 0419): " + first.substr(0, kFailLineMax) + "\n";
	}
	else if(c.exeCrash)
		r += "the proxy's crash record (exe-crash.txt, ticket 0419): none in the run folder\n";
	if(!serverLine.empty())
		r += "server.log, last FAILED or LOUD line: " + serverLine + "\n";
	r += "\n[patch]\n";
	{
		char id[1024];
		SrvBuildIdText(id, sizeof(id));
		r += std::string("server: ") + id + "\n";
	}
	r += "proxy folder: " + U8(c.proxyDir) + " (" + c.proxyDirHow + ")\n";
	r += "\n[hashes] (SHA-256 found; expected: see manifest.json among the attachments)\n";
	{
		struct H
		{
			const char* label;
			std::wstring path;
		};
		wchar_t self[MAX_PATH * 2];
		const DWORD n = GetModuleFileNameW(NULL, self, MAX_PATH * 2);
		const H hs[] = {
		    {"32-bit DLL (fallback_dll, default name)", c.proxyDir + L"\\CvGameCore_Expansion2_x86.dll"},
		    {"x64 DLL (server.cfg dll)", Acp(SrvCfgStr("dll", ""))},
		    {"proxy", c.proxyDir + L"\\CvGameCore_Expansion2.dll"},
		    {"server.exe", n ? std::wstring(self, n) : std::wstring()},
		    {"EXE", s_strPeerImage},
		};
		for(size_t i = 0; i < sizeof(hs) / sizeof(hs[0]); ++i)
		{
			r += std::string(hs[i].label) + ": " + U8(hs[i].path) + "\n";
			r += "  found:    " + (hs[i].path.empty() ? std::string("(no path)") : Sha256File(hs[i].path)) + "\n";
		}
	}
	r += "\n[session]\n";
	r += "player folder: " + U8(c.playerDir) + " (" + c.playerDirHow + ")\n";
	r += c.exeCrash ? "the session's kind and masks are not read: the EXE had ended (the proxy's reports name them)\n"
	                : "the session's kind and masks are not read: the server writes this report (the proxy's reports name them)\n";
	r += "\n[exe]\n";
	{
		uint32_t stamp = 0, soi = 0;
		int build = kVpExeBuildUnknown;
		if(!s_strPeerImage.empty() && PeBuild(s_strPeerImage, &stamp, &soi, &build))
			r += Fmt("build stamp (PE TimeDateStamp): 0x%08X, SizeOfImage 0x%08X = %s\n", stamp, soi,
			         build != kVpExeBuildUnknown ? VpExeBuildInfoOf(build)->label
			                                     : "unknown build (not a CivilizationV_DX11.exe build the patch knows)");
		else
			r += "build stamp: not readable\n";
		WIN32_FILE_ATTRIBUTE_DATA fa;
		if(!s_strPeerImage.empty() && GetFileAttributesExW(s_strPeerImage.c_str(), GetFileExInfoStandard, &fa))
			r += Fmt("file size: %llu bytes\n", ((unsigned long long)fa.nFileSizeHigh << 32) | fa.nFileSizeLow);
		if(c.exeCrash && c.byRecord)
			r += Fmt("exit code: 0x%08X (%u; not an exception code: the crash is proven by the proxy's crash record, see "
			         "[failure])\n",
			         c.exitCode, c.exitCode);
		else if(c.exeCrash)
			r += Fmt("exit code: 0x%08X (%s)\n", c.code, c.name);
		else
			r += "the EXE was still running when this report was written; the proxy ends it when it sees the server gone\n";
	}
	r += c.exeCrash ? "\n[mods]\n(not read: the server writes this report after the EXE ended; Lua.log among the attachments "
	                  "names the mods the game loaded)\n"
	                : "\n[mods]\n(not read: the server writes this report; Lua.log among the attachments names the mods the "
	                  "game loaded)\n";
	r += "\n[system]\n" + SystemText();
	r += "\n[attachments] (in this folder)\n";
	for(size_t i = 0; i < attached.size(); ++i)
		r += attached[i] + "\n";
	if(attached.empty())
		r += "(none)\n";
	r += "\n[server minidump] (not in this folder)\n";
	if(dumps.empty())
		r += "(none: the server wrote no minidump of itself)\n";
	else
	{
		for(size_t i = 0; i < dumps.size(); ++i)
			r += dumps[i] + "\n";
		r += "The server wrote this minidump when it crashed. It is NOT in this folder: it holds your game state and\n"
		     "may hold paths with your Windows user name, and reports are public. Do not attach it to a public issue; the\n"
		     "developers may ask for it.\n";
	}
	if(!c.exeCrash)
		r += Scrub(Fmt("The DLL writes its own crashlogs\\crashes.log and a minidump into %s\\crashlogs just after this report "
		               "(not in this folder; the same rules as above apply to that minidump).\n",
		               U8(run).c_str()));
	r += "\n[left out]\n"
	     "Your Steam id, computer name and Windows user name (every path is written relative to %USERPROFILE%), your\n"
	     "saves, any dump of the game's own process, and the server's minidump (named above, not copied).\n";
	if(!WriteAll(d + L"\\report.txt", Crlf(Scrub(r)), &err))
	{
		SrvLog("%s", Scrub(Fmt("%s: report: FAILED: %s\\report.txt could not be written (error %lu)", tag, U8(d).c_str(),
		                       err)).c_str());
		return false;
	}
	SrvLog("%s", Scrub(Fmt("%s: report: written to %s (report.txt and %u attachment line(s))", tag, U8(d).c_str(),
	                       (unsigned)attached.size())).c_str());
	return true;
}

// vp64helper.exe popup --report "<dir>" --options "<vpproxy.ini>", detached, out of a job when the job allows it (as
// proxy_session.cpp StartHelper).
bool StartHelper(const Crash& c, const std::wstring& reportDir)
{
	const char* tag = c.exeCrash ? "exe-crash" : c.stage.c_str();
	const std::wstring helper = c.proxyDir + L"\\vp64helper.exe";
	if(!IsFile(helper))
	{
		SrvLog("%s", Scrub(std::string(tag) + ": helper: " + U8(helper) + " does not exist: not started (no popup; the report and "
		                   "the log line remain)").c_str());
		return false;
	}
	std::wstring dir = reportDir;
	while(!dir.empty() && (dir[dir.size() - 1] == L'\\' || dir[dir.size() - 1] == L'/'))
		dir.erase(dir.size() - 1);
	std::wstring cmd = L"\"" + helper + L"\" popup --report \"" + dir + L"\"";
	if(IsFile(c.proxyIni))
		cmd += L" --options \"" + c.proxyIni + L"\"";
	const std::string extra = SrvCfgStr("exe_crash_helper_args", "");
	if(!extra.empty())
	{
		SrvLog("exe-crash: LOUD: TEST ONLY: server.cfg exe_crash_helper_args=%s appended to the helper's command line "
		       "(ticket 0418)", extra.c_str());
		cmd += L" " + Acp(extra);
	}
	std::vector<wchar_t> buf(cmd.begin(), cmd.end());
	buf.push_back(0);
	STARTUPINFOW si;
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi;
	memset(&pi, 0, sizeof(pi));
	const DWORD base = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;
	BOOL ok = CreateProcessW(helper.c_str(), &buf[0], NULL, NULL, FALSE, base | CREATE_BREAKAWAY_FROM_JOB, NULL,
	                         c.proxyDir.c_str(), &si, &pi);
	DWORD err = ok ? 0 : GetLastError();
	const bool breakaway = ok != FALSE;
	if(!ok && err == ERROR_ACCESS_DENIED)
	{
		ok = CreateProcessW(helper.c_str(), &buf[0], NULL, NULL, FALSE, base, NULL, c.proxyDir.c_str(), &si, &pi);
		err = ok ? 0 : GetLastError();
	}
	if(!ok)
	{
		SrvLog("%s", Scrub(Fmt("%s: helper: %s could not be started: CreateProcessW error %lu", tag, U8(helper).c_str(),
		                       err)).c_str());
		return false;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	SrvLog("%s", Scrub(Fmt("%s: helper: started %s: pid %lu (%s)", tag, U8(cmd).c_str(), pi.dwProcessId,
	                       breakaway ? "out of the server's job" : "the job allows no breakaway: inside it")).c_str());
	return true;
}

// exe_crash_report: auto (vpproxy.ini pack=1) | 0 | 1. *why: one clause.
bool ReportEnabled(const std::wstring& proxyIni, std::string* why)
{
	const std::string v = SrvCfgStr("exe_crash_report", "auto");
	if(v == "0")
	{
		*why = "server.cfg exe_crash_report=0";
		return false;
	}
	if(v == "1")
	{
		*why = "server.cfg exe_crash_report=1";
		return true;
	}
	if(v != "auto")
		SrvLog("exe-crash: LOUD: server.cfg exe_crash_report=%s is not auto, 0 or 1: taken as auto", v.c_str());
	std::string text, pack;
	bool cut = false;
	DWORD err = 0;
	if(!ReadCapped(proxyIni, kCopyMax, text, &cut, &err))
	{
		*why = Scrub(Fmt("auto: %s cannot be read (error %lu): not the shipped layout", U8(proxyIni).c_str(), err));
		return false;
	}
	const bool on = IniValue(text, "pack", &pack) && (pack == "1" || pack == "true" || pack == "yes" || pack == "on");
	*why = Scrub(Fmt("auto: %s says pack=%s", U8(proxyIni).c_str(), pack.empty() ? "(none: 0)" : pack.c_str()));
	return on;
}

DWORD WINAPI KillThread(void*)
{
	Sleep(s_dwKillMs);
	if(!s_hPeer)
		return 0;
	SrvLog("remote: LOUD: TEST ONLY: test_exe_kill: ending the EXE (pid %u) with exit code 0x%08lX now (ticket 0418)",
	       s_uPeerPid, s_dwKillCode);
	if(!TerminateProcess(s_hPeer, s_dwKillCode))
		SrvLog("remote: LOUD: TEST ONLY: test_exe_kill: TerminateProcess failed (error %lu)", GetLastError());
	return 0;
}

void MaybeKill(KillEvent ev)
{
	if(s_killEvent != ev || InterlockedExchange(&s_lKillArmed, 1))
		return;
	SrvLog("remote: LOUD: TEST ONLY: test_exe_kill: the EXE will be ended with 0x%08lX in %lu ms (ticket 0418)", s_dwKillCode,
	       s_dwKillMs);
	HANDLE h = CreateThread(NULL, 0, KillThread, NULL, 0, NULL);
	if(h)
		CloseHandle(h);
}

void ParseKill()
{
	const std::string v = SrvCfgStr("test_exe_kill", "");
	if(v.empty())
		return;
	const size_t c1 = v.find(':');
	const std::string ev = v.substr(0, c1);
	KillEvent k = ev == "attach" ? kKillAttach : ev == "boot" ? kKillBoot : ev == "load" ? kKillLoad : kKillNone;
	char* end = NULL;
	const unsigned long ms = c1 == std::string::npos ? 0 : strtoul(v.c_str() + c1 + 1, &end, 10);
	unsigned long code = 0xC0000005ul;
	bool ok = k != kKillNone && c1 != std::string::npos && end && end != v.c_str() + c1 + 1 && ms <= 3600000ul;
	if(ok && *end == ':')
	{
		char* e2 = NULL;
		code = strtoul(end + 1, &e2, 16);
		ok = e2 && e2 != end + 1 && *e2 == 0;
	}
	else if(ok)
		ok = *end == 0;
	if(!ok)
	{
		SrvLog("remote: LOUD: TEST ONLY: server.cfg test_exe_kill=%s not understood (attach|boot|load:<ms>[:<hex code>]): "
		       "ignored (ticket 0418)",
		       v.c_str());
		return;
	}
	s_killEvent = k;
	s_dwKillMs = ms;
	s_dwKillCode = code;
	SrvLog("remote: LOUD: TEST ONLY: server.cfg test_exe_kill=%s: the EXE will be ended with 0x%08lX %lu ms after %s "
	       "(ticket 0418)",
	       v.c_str(), s_dwKillCode, s_dwKillMs, ev.c_str());
}

// ---- ticket 0519: the proxy's crash record as proof of an EXE crash -------------------------------------------------
std::string s_strRecordName;  // the record's code name (Crash::name points here for a crash proven by the record)

// The value of the first line that starts with `key` (CR and trailing blanks cut); false when no line does.
bool RecordLine(const std::string& text, const char* key, std::string* value)
{
	const size_t n = strlen(key);
	size_t pos = 0;
	while(pos < text.size())
	{
		size_t e = text.find('\n', pos);
		if(e == std::string::npos)
			e = text.size();
		if(e - pos >= n && text.compare(pos, n, key) == 0)
		{
			std::string v = text.substr(pos + n, e - pos - n);
			while(!v.empty() && (v[v.size() - 1] == '\r' || v[v.size() - 1] == ' ' || v[v.size() - 1] == '\t'))
				v.erase(v.size() - 1);
			*value = v;
			return true;
		}
		pos = e + 1;
	}
	return false;
}

// "0x<hex>[ <name>]" (the record's hex form, proxy_crash_filter.cpp Out::hex) -> *code and *name (printable, at most 80
// characters); false when no hex number of at most 32 bits leads.
bool ParseRecordCode(const std::string& v, uint32_t* code, std::string* name)
{
	if(v.size() < 3 || v[0] != '0' || (v[1] != 'x' && v[1] != 'X'))
		return false;
	size_t i = 2;
	unsigned long long x = 0;
	while(i < v.size() && i < 2 + 16 && isxdigit((unsigned char)v[i]))
	{
		const char ch = v[i++];
		x = x * 16 + (unsigned)(ch <= '9' ? ch - '0' : (ch | 0x20) - 'a' + 10);
	}
	if(i == 2 || x > 0xFFFFFFFFull || (i < v.size() && v[i] != ' '))
		return false;
	*code = (uint32_t)x;
	while(i < v.size() && v[i] == ' ')
		++i;
	std::string nm = v.substr(i, 80);
	for(size_t k = 0; k < nm.size(); ++k)
		if((unsigned char)nm[k] < 0x20 || nm[k] == 0x7F)
			nm[k] = ' ';
	*name = nm;
	return true;
}

// True when <run>\exe-crash.txt exists, its "process:" line names the EXE's pid and it was last written after the EXE
// started (a record of an earlier EXE with the same pid, or of another EXE, does not count). *code and *name come from
// its "code:" line, else from its first line ("exception 0x... <name> at ..."), else the exit code with "(the crash
// record names no exception code)". *why: one clause for server.log and the report, either way.
bool RecordProvesCrash(const FILETIME* exeStart, uint32_t exitCode, uint32_t* code, std::string* name, std::string* why)
{
	const std::wstring run = Acp(SrvRunDir() ? SrvRunDir() : "");
	if(run.empty())
	{
		*why = "no run folder, so no exe-crash.txt to read";
		return false;
	}
	const std::wstring path = run + L"\\exe-crash.txt";
	if(!IsFile(path))
	{
		*why = "no exe-crash.txt (the proxy's crash record) in the run folder";
		return false;
	}
	std::string text;
	bool cut = false;
	DWORD err = 0;
	if(!ReadCapped(path, kCopyMax, text, &cut, &err))
	{
		*why = Fmt("exe-crash.txt cannot be read (error %lu)", err);
		return false;
	}
	std::string pidText;
	if(!RecordLine(text, "process: ", &pidText) || pidText.empty() ||
	   pidText.find_first_not_of("0123456789") != std::string::npos || pidText.size() > 10)
	{
		*why = "exe-crash.txt has no \"process:\" line with a pid: not taken as proof";
		return false;
	}
	const unsigned long long pid = _strtoui64(pidText.c_str(), NULL, 10);
	if(pid != s_uPeerPid)
	{
		*why = Fmt("exe-crash.txt names process %llu, not this EXE (pid %u): a stale record, not taken as proof", pid,
		           s_uPeerPid);
		return false;
	}
	WIN32_FILE_ATTRIBUTE_DATA fa;
	if(!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa))
	{
		*why = Fmt("exe-crash.txt's time cannot be read (error %lu)", GetLastError());
		return false;
	}
	std::string when = "the EXE's start time not readable, the pid alone checked";
	if(exeStart)
	{
		if(CompareFileTime(&fa.ftLastWriteTime, exeStart) < 0)
		{
			*why = Fmt("exe-crash.txt names this pid but was written %s, before this EXE started %s: a stale record, not "
			           "taken as proof",
			           FileTimeText(fa.ftLastWriteTime).c_str(), FileTimeText(*exeStart).c_str());
			return false;
		}
		when = Fmt("%.1f s after the EXE started", FtSeconds(*exeStart, fa.ftLastWriteTime));
	}
	std::string v;
	if(RecordLine(text, "code: ", &v) && ParseRecordCode(v, code, name))
		;
	else if(RecordLine(text, "exception ", &v) && ParseRecordCode(v.substr(0, v.find(" at ")), code, name))
		;
	else
	{
		*code = exitCode;
		*name = "(the crash record names no exception code)";
	}
	if(name->empty())
		*name = (*code & 0xC0000000u) == 0xC0000000u ? "NTSTATUS error" : "(no name)";
	*why = Fmt("exe-crash.txt names this EXE (process %u), written %s (local time, %s)", s_uPeerPid,
	           FileTimeText(fa.ftLastWriteTime).c_str(), when.c_str());
	return true;
}

std::string Work()
{
	if(!s_hPeer)
	{
		SrvLog("remote: exe-crash check: no handle of the EXE (pid %u): its exit code is not checked (ticket 0418)", s_uPeerPid);
		return std::string();
	}
	const DWORD w = WaitForSingleObject(s_hPeer, kPeerEndWaitMs);
	if(w != WAIT_OBJECT_0)
	{
		SrvLog("remote: exe-crash check: the EXE (pid %u) is still running %u ms after its channel failed: no EXE crash "
		       "(ticket 0418)",
		       s_uPeerPid, (unsigned)kPeerEndWaitMs);
		return std::string();
	}
	DWORD code = 0;
	if(!GetExitCodeProcess(s_hPeer, &code))
	{
		SrvLog("remote: exe-crash check: GetExitCodeProcess failed (error %lu) (ticket 0418)", GetLastError());
		return std::string();
	}
	Crash c;
	c.code = code;
	c.exitCode = code;
	c.tEnd = SrvNow();
	FILETIME exeStart;
	bool haveStart = false;
	{
		FILETIME cr, ex, k, u;
		if(GetProcessTimes(s_hPeer, &cr, &ex, &k, &u))
		{
			c.times = Fmt("the EXE started %s, ended %s (local time), ran %.1f s\n", FileTimeText(cr).c_str(),
			              FileTimeText(ex).c_str(), FtSeconds(cr, ex));
			exeStart = cr;
			haveStart = true;
		}
		else
			c.times = "the EXE's start and end times: not readable\n";
	}
	const bool crash = IsCrashCode(code, &c.name);
	if(crash)
		SrvLog("remote: the EXE (pid %u) ended with exit code 0x%08lX (%lu): an exception code, %s", s_uPeerPid, code, code,
		       c.name);
	else
	{
		// Ticket 0519: an exit code that is not an exception code still ends in the report when the proxy's crash
		// record (ticket 0419) proves the crash: on a machine where Windows Error Reporting shows its "has stopped
		// working" box, the crashed EXE stays frozen until "Close program" and then ends with 255. Exit code 0 is the
		// game's own clean end: a record then means an exception the process survived, not a crash (logged only).
		uint32_t recCode = 0;
		std::string why;
		const bool proven = RecordProvesCrash(haveStart ? &exeStart : NULL, code, &recCode, &s_strRecordName, &why);
		if(!proven || code == 0)
		{
			SrvLog("%s", Scrub(Fmt("remote: the EXE (pid %u) ended with exit code 0x%08lX (%lu): not a crash code; no "
			                       "exe-crash report (ticket 0418; %s%s; ticket 0519)",
			                       s_uPeerPid, code, code, why.c_str(),
			                       proven ? ", but exit code 0 is the game's own clean end" : "")).c_str());
			return std::string();
		}
		c.byRecord = true;
		c.code = recCode;
		c.name = s_strRecordName.c_str();
		c.failure = Scrub(Fmt("exit code: the EXE ended with 0x%08lX (%lu), not an exception code (after a crash, Windows "
		                      "Error Reporting's \"has stopped working\" box ends the EXE so when the player presses "
		                      "\"Close program\")\n"
		                      "proof of the crash: the proxy's crash record: %s (ticket 0519)\n",
		                      code, code, why.c_str()));
		SrvLog("%s", Scrub(Fmt("remote: the EXE (pid %u) ended with exit code 0x%08lX (%lu): not a crash code, but the "
		                       "proxy's crash record proves a crash: 0x%08X %s (%s; ticket 0519)",
		                       s_uPeerPid, code, code, c.code, c.name, why.c_str())).c_str());
	}
	const std::string clause =
	    c.byRecord ? Scrub(Fmt("the EXE ended with exit code 0x%08lX (%lu) after a crash its proxy recorded: exception code "
	                           "0x%08X (%s)",
	                           code, code, c.code, c.name))
	               : Fmt("the EXE ended with exception code 0x%08lX (%s)", code, c.name);
	c.proxyDir = ProxyDir(&c.proxyDirHow);
	c.proxyIni = ProxyIni(c.proxyDir);
	std::string why;
	if(!ReportEnabled(c.proxyIni, &why))
	{
		SrvLog("remote: LOUD: exe-crash: %s; no report and no popup (%s; ticket 0418)", clause.c_str(), why.c_str());
		return clause;
	}
	c.playerDir = PlayerDir(&c.playerDirHow);
	const std::string root = SrvCfgStr("report_root", "");
	c.root = !root.empty() ? Acp(root) : c.playerDir.empty() ? std::wstring() : c.playerDir + L"\\VP64\\reports";
	SrvLog("%s", Scrub(Fmt("remote: LOUD: exe-crash: %s; writing the D119 report under %s and starting vp64helper popup "
	                       "(%s; player folder from %s; ticket 0418)",
	                       clause.c_str(), U8(c.root).c_str(), why.c_str(), c.playerDirHow.c_str())).c_str());
	const double t0 = SrvNow();
	std::wstring dir;
	WriteReport(c, &dir);
	StartHelper(c, dir.empty() ? c.root : dir);
	SrvLog("exe-crash: done in %.1f ms", (SrvNow() - t0) * 1000.0);
	return clause;
}
}  // namespace

bool IsCrashCode(uint32_t code, const char** pName)
{
	static const struct
	{
		uint32_t code;
		const char* name;
	} s_names[] = {
	    {0xC0000005u, "EXCEPTION_ACCESS_VIOLATION"},       {0xC0000006u, "EXCEPTION_IN_PAGE_ERROR"},
	    {0xC0000017u, "STATUS_NO_MEMORY"},                 {0xC000001Du, "EXCEPTION_ILLEGAL_INSTRUCTION"},
	    {0xC0000025u, "EXCEPTION_NONCONTINUABLE_EXCEPTION"}, {0xC0000026u, "EXCEPTION_INVALID_DISPOSITION"},
	    {0xC000008Cu, "EXCEPTION_ARRAY_BOUNDS_EXCEEDED"},  {0xC000008Du, "EXCEPTION_FLT_DENORMAL_OPERAND"},
	    {0xC000008Eu, "EXCEPTION_FLT_DIVIDE_BY_ZERO"},     {0xC000008Fu, "EXCEPTION_FLT_INEXACT_RESULT"},
	    {0xC0000090u, "EXCEPTION_FLT_INVALID_OPERATION"},  {0xC0000091u, "EXCEPTION_FLT_OVERFLOW"},
	    {0xC0000092u, "EXCEPTION_FLT_STACK_CHECK"},        {0xC0000093u, "EXCEPTION_FLT_UNDERFLOW"},
	    {0xC0000094u, "EXCEPTION_INT_DIVIDE_BY_ZERO"},     {0xC0000095u, "EXCEPTION_INT_OVERFLOW"},
	    {0xC0000096u, "EXCEPTION_PRIV_INSTRUCTION"},       {0xC00000FDu, "EXCEPTION_STACK_OVERFLOW"},
	    {0xC0000142u, "STATUS_DLL_INIT_FAILED"},           {0xC0000374u, "STATUS_HEAP_CORRUPTION"},
	    {0xC0000409u, "STATUS_STACK_BUFFER_OVERRUN (fail fast)"}, {0xC0000417u, "STATUS_INVALID_CRUNTIME_PARAMETER"},
	    {0xE06D7363u, "an unhandled C++ exception (MSVC 0xE06D7363)"},
	    {0x80000002u, "EXCEPTION_DATATYPE_MISALIGNMENT"},  {0x80000003u, "EXCEPTION_BREAKPOINT"},
	    {0x80000004u, "EXCEPTION_SINGLE_STEP"},
	};
	const char* name = "";
	bool crash = false;
	if(code == 0xC000013Au)  // STATUS_CONTROL_C_EXIT: a console close or Ctrl+C, not a crash
		crash = false;
	else if((code & 0xC0000000u) == 0xC0000000u)
	{
		crash = true;
		name = "NTSTATUS error";
	}
	else if(code >= 0x80000002u && code <= 0x80000004u)
		crash = true;
	if(crash)
		for(size_t i = 0; i < sizeof(s_names) / sizeof(s_names[0]); ++i)
			if(s_names[i].code == code)
				name = s_names[i].name;
	if(pName)
		*pName = name;
	return crash;
}

void OnAttach(uint32_t uPeerPid)
{
	s_tAttach = SrvNow();
	s_uPeerPid = uPeerPid;
	ParseKill();
	const DWORD access = SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | (s_killEvent != kKillNone ? PROCESS_TERMINATE : 0);
	s_hPeer = OpenProcess(access, FALSE, uPeerPid);
	if(!s_hPeer)
	{
		SrvLog("remote: LOUD: exe-crash: cannot open the EXE's process (pid %u, error %lu): an EXE crash will not be "
		       "reported (ticket 0418)",
		       uPeerPid, GetLastError());
		return;
	}
	wchar_t img[MAX_PATH * 2];
	DWORD n = MAX_PATH * 2;
	if(QueryFullProcessImageNameW(s_hPeer, 0, img, &n))
		s_strPeerImage.assign(img, n);
	SrvLog("remote: exe-crash: watching the EXE (pid %u, %s) for an exception exit code (ticket 0418)", uPeerPid,
	       Scrub(U8(s_strPeerImage)).c_str());
	MaybeKill(kKillAttach);
}

void NoteBoot()
{
	s_tBoot = SrvNow();
	MaybeKill(kKillBoot);
}

void NoteLoad()
{
	s_tLoad = SrvNow();
	InterlockedIncrement(&s_lLoads);
	MaybeKill(kKillLoad);
}

std::string OnPeerGone()
{
	const LONG prev = InterlockedCompareExchange(&s_lState, 1, 0);
	if(prev != 0)
	{
		// another service thread does the work: wait for it (at most 60 s), so this thread does not end the process
		for(int i = 0; i < 6000 && s_lState != 2; ++i)
			Sleep(10);
		return s_lState == 2 ? s_strResult : std::string();
	}
	std::string r;
	try
	{
		r = Work();
	}
	catch(...)
	{
		SrvLog("remote: LOUD: exe-crash: an exception in the EXE-crash report (ticket 0418)");
	}
	s_strResult = r;
	InterlockedExchange(&s_lState, 2);
	return r;
}

std::string ReportServerFailure(const ServerFailure& f, bool* pOn)
{
	*pOn = false;
	Crash c;
	c.exeCrash = false;
	c.code = f.code;
	c.name = f.codeName ? f.codeName : "";
	c.stage = f.stage;
	c.title = f.title;
	c.summary = f.summary;
	c.intro = f.intro;
	c.failure = f.failure;
	c.tEnd = SrvNow();
	{
		FILETIME cr, ex, k, u;
		if(s_hPeer && GetProcessTimes(s_hPeer, &cr, &ex, &k, &u))
			c.times = Fmt("the EXE started %s (local time) and was still running\n", FileTimeText(cr).c_str());
		else
			c.times = "the EXE's start time: not readable\n";
	}
	c.proxyDir = ProxyDir(&c.proxyDirHow);
	c.proxyIni = ProxyIni(c.proxyDir);
	std::string why;
	if(!ReportEnabled(c.proxyIni, &why))
	{
		SrvLog("%s: LOUD: no report and no popup (%s; ticket 0422)", c.stage.c_str(), why.c_str());
		return std::string();
	}
	*pOn = true;
	c.playerDir = PlayerDir(&c.playerDirHow);
	const std::string root = SrvCfgStr("report_root", "");
	c.root = !root.empty() ? Acp(root) : c.playerDir.empty() ? std::wstring() : c.playerDir + L"\\VP64\\reports";
	SrvLog("%s", Scrub(Fmt("%s: writing the D119 report under %s and starting vp64helper popup (%s; player folder from %s; "
	                       "ticket 0422)",
	                       c.stage.c_str(), U8(c.root).c_str(), why.c_str(), c.playerDirHow.c_str())).c_str());
	const double t0 = SrvNow();
	std::wstring dir;
	WriteReport(c, &dir);
	StartHelper(c, dir.empty() ? c.root : dir);
	SrvLog("%s: done in %.1f ms", c.stage.c_str(), (SrvNow() - t0) * 1000.0);
	return U8(dir);
}

std::string ScrubText(const std::string& text) { return Scrub(text); }

// ticket 5054: server.exe's log writer (null in the unit tests)
namespace
{
bool (*volatile s_pfnLogSync)(unsigned long) = NULL;
}
void SetLogSync(bool (*fn)(unsigned long ms)) { s_pfnLogSync = fn; }
bool LogSync(unsigned long ms)
{
	bool (*fn)(unsigned long) = s_pfnLogSync;
	return fn ? fn(ms) : true;
}

bool ShippedLayout(std::string* pWhy)
{
	std::string how, why;
	const bool on = ReportEnabled(ProxyIni(ProxyDir(&how)), &why);
	if(pWhy)
		*pWhy = why;
	return on;
}

// ---- ticket 0641: the server's own reason for a deliberate end (srv_exe_crash.h) ----
namespace
{
std::string OneLine(const std::string& s, size_t maxLen)
{
	std::string o;
	for(size_t i = 0; i < s.size() && o.size() < maxLen; ++i)
		o += (s[i] == '\r' || s[i] == '\n' || s[i] == '\t') ? ' ' : s[i];
	if(s.size() > maxLen)
		o += " ...";
	return o;
}
bool StartsWith(const std::string& s, const char* p) { return s.compare(0, strlen(p), p) == 0; }
}  // namespace

FatalRecord FatalWords(int code, const std::string& why)
{
	FatalRecord r;
	const uint32_t u = (uint32_t)code;
	if(code == 0 || code == 3 || (u & 0xFFFF0000u) == 0x20560000u)
		return r;
	if(code == 5)
	{
		// the render modules' fatal steps (HostConfig::fatal(5, why)); the reason begins with the module's name
		struct M
		{
			const char* prefix;
			const char* token;
			const char* what;
		};
		static const M kModules[] = {
		    {"models: cities", "render-models", "the cities"},
		    {"models: landmarks", "render-models", "the landmarks"},
		    {"models:", "render-models", "the cities and landmarks"},
		    {"units", "render-units", "the units"},
		    {"unit shadow", "render-units", "the units' shadows"},
		    {"terrain", "render-terrain", "the terrain"},
		    {"fog", "render-fog", "the fog of war"},
		    {"decal", "render-decals", "the marks on the terrain (roads, farms, improvements)"},
		    {"shadow", "render-shadows", "the shadows"},
		    {"ice", "render-ice", "the ice"},
		    {"overlay", "render-overlay", "the map's overlays"},
		    {"forest", "render-forest", "the forests and jungles"},
		    {"the server's renderer could not start", "render-start", "the game's world"},
		};
		const M* m = NULL;
		for(size_t i = 0; i < sizeof(kModules) / sizeof(kModules[0]) && !m; ++i)
			if(StartsWith(why, kModules[i].prefix))
				m = &kModules[i];
		r.token = m ? m->token : "render";
		r.summary = Fmt("The 64-bit server could no longer draw %s, so it ended the game.", m ? m->what : "the game's world");
		return r;
	}
	switch(code)
	{
	case 1:
		r.token = "server-error";
		r.summary = Fmt("The 64-bit server met an error it cannot go on from, so it ended the game.");
		break;
	case 2:
		r.token = "server-exception";
		r.summary = Fmt("The 64-bit server crashed, so the game had to end.");
		break;
	case 4:
		r.token = "database";
		r.summary = Fmt("The 64-bit server's copy of the game's database went wrong, so it ended the game.");
		break;
	case 6:
		r.token = "engine-exception";
		r.summary = Fmt("The game's engine failed inside a call from the 64-bit server, so the game had to end.");
		break;
	default:
		r.token = Fmt("exit-%d", code);
		r.summary = Fmt("The 64-bit server ended the game (exit code %d).", code);
		break;
	}
	return r;
}

// Ticket 0681: set at start-up by srv_remote_savemods.cpp (SrvSaveMods::OverrideFatal) in server.exe; NULL in a program
// that links this file without it (the hermetic report tests), so they link unchanged. The same declaration is in
// srv_remote_savemods.cpp (the header srv_exe_crash.h is left as it is).
typedef bool (*FatalOverrideFn)(std::string* pToken, std::string* pSummary, std::string* pReason);
extern FatalOverrideFn g_pfnFatalOverride;
FatalOverrideFn g_pfnFatalOverride = NULL;

bool WriteFatalRecord(int code, const std::string& why)
{
	FatalRecord r = FatalWords(code, why);
	if(r.token.empty())
		return false;
	std::string reason = why;
	if(g_pfnFatalOverride)  // ticket 0681: after the DLL's game data mismatch (srv_remote_savemods.cpp sets it)
		g_pfnFatalOverride(&r.token, &r.summary, &reason);
	const char* run = SrvRunDir();
	if(!run || !run[0])
	{
		SrvLog("server-fatal: no run folder: server-fatal.txt not written (ticket 0641)");
		return false;
	}
	std::string t;
	t += "server-fatal: the 64-bit server ended on purpose (ticket 0641)\n";
	t += Fmt("exit: %d\n", code);
	t += Fmt("pid: %lu\n", (unsigned long)GetCurrentProcessId());   // ticket 0744: the proxy's proof after an exception death
	t += "code: " + r.token + "\n";
	t += "summary: " + r.summary + "\n";
	t += "reason: " + OneLine(reason, 4000) + "\n";
	const std::wstring path = Acp(run) + L"\\server-fatal.txt";
	DWORD err = 0;
	if(!WriteAll(path, Crlf(Scrub(t)), &err))
	{
		SrvLog("server-fatal: LOUD: %s\\server-fatal.txt could not be written (error %lu): the proxy's report will not name "
		       "the server's reason (ticket 0641)",
		       run, err);
		return false;
	}
	SrvLog("%s", Scrub(Fmt("server-fatal: %s\\server-fatal.txt written (exit %d, code %s): the proxy's report names the "
	                       "server's reason (ticket 0641)",
	                       run, code, r.token.c_str())).c_str());
	return true;
}
}  // namespace SrvExeCrash
