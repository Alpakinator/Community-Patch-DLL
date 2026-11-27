// Ticket 0589 (D200): which installed game the render tests' C++ programs read, and which EXE build they accept.
//
// Since 2026-11-19 the Steam install is Firaxis' new build; the tests are keyed to build 4390913. So no test reads the
// Steam install by default (they did before 0589). The install is, in this order:
//   1. C5_GAME_DIR;
//   2. C5R_GAME_DIR (what server/tests/render/run_all.py sets, and the other render suites read);
//   3. the setting render_game_dir of tools/release/settings.py: C5_RENDER_GAME_DIR, else `render_game_dir = <path>` in
//      the settings file (C5_SETTINGS_FILE, else deps/local-settings.ini of the checkout this header was compiled from);
// and with none of them a placeholder path that names the variables, so the test that needs the install fails loud.
//
// An EXE is accepted only when it is build 4390913, tools/harness/exe_build.py's rule: PE TimeDateStamp 0x546CD0A8 and
// one of the two known sizes. C5_ALLOW_UNKNOWN_EXE_BUILD=1 lets another build through (logged); it is for the migration
// to the new build only, once that is approved (ticket 0583).
//
// Header only, Windows only, read-only: nothing is written anywhere.
#pragma once

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace c5test {

constexpr DWORD kKnownStamp = 0x546CD0A8;                        // exe_build.py KNOWN_STAMP
constexpr ULONGLONG kKnownSizes[] = {16982584ull, 16982840ull};  // exe_build.py KNOWN_SIZES
constexpr const wchar_t* kAllowEnv = L"C5_ALLOW_UNKNOWN_EXE_BUILD";

inline std::wstring EnvW(const wchar_t* name)
{
	std::vector<wchar_t> buf(32768);
	const DWORD n = GetEnvironmentVariableW(name, buf.data(), (DWORD)buf.size());
	return n && n < buf.size() ? std::wstring(buf.data(), n) : std::wstring();
}

inline std::wstring Trim(const std::wstring& s)
{
	const size_t a = s.find_first_not_of(L" \t\r\n");
	if(a == std::wstring::npos)
		return std::wstring();
	return s.substr(a, s.find_last_not_of(L" \t\r\n") - a + 1);
}

inline std::wstring FullPath(const std::wstring& p)
{
	std::vector<wchar_t> buf(32768);
	const DWORD n = GetFullPathNameW(p.c_str(), (DWORD)buf.size(), buf.data(), nullptr);
	return n && n < buf.size() ? std::wstring(buf.data(), n) : p;
}

// The checkout this header was compiled from (server/tests/render/test_install.h -> three folders up), or "".
inline std::wstring CompiledRepo()
{
	const char* f = __FILE__;
	std::wstring p(f, f + strlen(f));
	for(wchar_t& c : p)
		if(c == L'/')
			c = L'\\';
	p = FullPath(p);
	for(int i = 0; i < 4; ++i)
	{
		const size_t s = p.find_last_of(L'\\');
		if(s == std::wstring::npos)
			return std::wstring();
		p.resize(s);
	}
	return p;
}

// tools/release/settings.py's value expansion for a path: %VAR%, a leading ~, a relative value from the checkout.
inline std::wstring ExpandSettingPath(const std::wstring& v, const std::wstring& repo)
{
	std::vector<wchar_t> buf(32768);
	const DWORD n = ExpandEnvironmentStringsW(v.c_str(), buf.data(), (DWORD)buf.size());
	std::wstring p = n && n <= buf.size() ? std::wstring(buf.data()) : v;
	if(!p.empty() && p[0] == L'~')
		p = EnvW(L"USERPROFILE") + p.substr(1);
	const bool absolute = p.size() >= 2 && (p[1] == L':' || (p[0] == L'\\' && p[1] == L'\\'));
	if(!absolute && !repo.empty())
		p = repo + L"\\" + p;
	return p;
}

// `key = value` of the settings file (UTF-8, `#` comments), or "".
inline std::wstring SettingFromFile(const std::wstring& file, const wchar_t* key)
{
	HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if(h == INVALID_HANDLE_VALUE)
		return std::wstring();
	std::string bytes;
	char chunk[4096];
	DWORD got = 0;
	while(ReadFile(h, chunk, sizeof(chunk), &got, nullptr) && got)
		bytes.append(chunk, got);
	CloseHandle(h);
	std::wstring text;
	if(!bytes.empty())
	{
		const int n = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
		text.resize(n > 0 ? n : 0);
		if(n > 0)
			MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), &text[0], n);
	}
	size_t pos = 0;
	while(pos <= text.size())
	{
		size_t e = text.find(L'\n', pos);
		if(e == std::wstring::npos)
			e = text.size();
		const std::wstring line = Trim(text.substr(pos, e - pos));
		pos = e + 1;
		if(line.empty() || line[0] == L'#')
			continue;
		const size_t eq = line.find(L'=');
		if(eq != std::wstring::npos && Trim(line.substr(0, eq)) == key)
			return Trim(line.substr(eq + 1));
	}
	return std::wstring();
}

struct Install
{
	std::wstring dir;   // the install, or a placeholder that names the variables (never the Steam install)
	std::string from;   // where it came from, for the log
	bool set = false;   // false: no variable or setting named one
};

inline Install GameDir()
{
	Install r;
	if(!(r.dir = EnvW(L"C5_GAME_DIR")).empty())
		r.from = "C5_GAME_DIR";
	else if(!(r.dir = EnvW(L"C5R_GAME_DIR")).empty())
		r.from = "C5R_GAME_DIR";
	else if(!(r.dir = EnvW(L"C5_RENDER_GAME_DIR")).empty())
		r.from = "C5_RENDER_GAME_DIR (setting render_game_dir)";
	else
	{
		const std::wstring repo = CompiledRepo();
		std::wstring file = EnvW(L"C5_SETTINGS_FILE");
		file = file.empty() ? (repo.empty() ? std::wstring() : repo + L"\\deps\\local-settings.ini") : ExpandSettingPath(file, repo);
		const std::wstring v = file.empty() ? std::wstring() : SettingFromFile(file, L"render_game_dir");
		if(!v.empty())
		{
			r.dir = ExpandSettingPath(v, repo);
			r.from = "render_game_dir of the settings file";
		}
	}
	r.set = !r.dir.empty();
	if(!r.set)
	{
		r.dir = L"<no install: set C5_GAME_DIR or C5R_GAME_DIR to a pre-update copy (build 4390913), or the setting "
		        L"render_game_dir of tools/release/settings.py; the Steam install is never the default (ticket 0589)>";
		r.from = "nothing set";
	}
	return r;
}

// "" when exe is build 4390913 (exe_build.py's rule) or C5_ALLOW_UNKNOWN_EXE_BUILD=1 lets it through; else why it is
// refused.
inline std::string ExeBuildWhy(const std::wstring& exe)
{
	auto narrow = [](const std::wstring& w) {
		const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
		std::string s(n > 0 ? n : 0, '\0');
		if(n > 0)
			WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
		return s;
	};
	char why[1024] = "";
	WIN32_FILE_ATTRIBUTE_DATA fa = {};
	DWORD stamp = 0;
	bool pe = false;
	ULONGLONG size = 0;
	if(!GetFileAttributesExW(exe.c_str(), GetFileExInfoStandard, &fa) || (fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		snprintf(why, sizeof(why), "%s does not exist", narrow(exe).c_str());
	else
	{
		size = ((ULONGLONG)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
		HANDLE h = CreateFileW(exe.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
		if(h != INVALID_HANDLE_VALUE)
		{
			unsigned char head[4096] = {};
			DWORD got = 0;
			if(ReadFile(h, head, sizeof(head), &got, nullptr) && got >= 64 && head[0] == 'M' && head[1] == 'Z')
			{
				const DWORD lfanew = *(const DWORD*)(head + 0x3C);
				if(lfanew + 12 <= got && !memcmp(head + lfanew, "PE\0\0", 4))
				{
					stamp = *(const DWORD*)(head + lfanew + 8);
					pe = true;
				}
			}
			CloseHandle(h);
		}
		bool knownSize = false;
		for(ULONGLONG s : kKnownSizes)
			knownSize = knownSize || s == size;
		if(!pe)
			snprintf(why, sizeof(why), "%s (%llu bytes) is not a PE file", narrow(exe).c_str(), size);
		else if(stamp == kKnownStamp && knownSize)
			return std::string();
		else
			snprintf(why, sizeof(why), "%s has TimeDateStamp 0x%08lX, %llu bytes: %s", narrow(exe).c_str(), (unsigned long)stamp,
			         size, stamp != kKnownStamp ? "a different build" : "build 4390913's stamp but an unknown size");
	}
	if(EnvW(kAllowEnv) == L"1")
	{
		printf("test_install: C5_ALLOW_UNKNOWN_EXE_BUILD=1 lets an unknown EXE through: %s\n", why);
		return std::string();
	}
	return std::string("refusing an EXE that is not build 4390913 (TimeDateStamp 0x546CD0A8, 16982584 or 16982840 bytes; "
	                   "tools/harness/exe_build.py, ticket 0589): ") + why +
	       ". Point C5_GAME_DIR / C5R_EXE at a pre-update copy; C5_ALLOW_UNKNOWN_EXE_BUILD=1 only once the migration is approved";
}

}  // namespace c5test
