// proxy_purity.cpp - the purity lists as data bound to the x64 DLL (issue 3503, epic 3500; interface proxy_purity.h,
// file format tools/release/README.md section 4 and research/03-dll-engine-boundary/proxy-design.md section 16.9).
// Replaces the compiled lists (proxy_pure.inc's kPureKeys, proxy_lua.cpp's kLuaPure): the proxy holds no purity list of
// its own any more; with no file, a refused file or a file for another DLL both lists are empty and every call
// crosses (decision D30's safe default).
//
// The file (UTF-8; LF, a CR before an LF is accepted):
//   # vp64-purity 1                                   line 1, exactly (the format version)
//   dll_sha256<TAB><64 hex digits>                    exactly once: the x64 DLL the lists were derived for
//   derived_from<TAB><commit><TAB><commit>            exactly once: the last audited fork commit, the commit derived for
//   method<TAB><model key>                            an engine -> DLL method key, "ICvCity1::GetOwner() const"
//   lua<TAB><Table.Function>                          a DLL Lua function name, "Plot.GetX"
//   # ... and empty lines                             ignored after line 1
// Anything else refuses the whole file: an unknown record, a missing or repeated header record, a field count other
// than the record's, an empty field, a control character, a byte >= 0x80 in a key, a line over 1024 bytes, a file over
// 1 MiB, more than 65536 entries, a key listed twice, a key that is not of its record's shape.
//
// Threads: Load and Clear run at the boot, before any engine thread asks (proxy_rt.cpp, issue 3504); the Is* queries
// only read the sets afterwards.
//
// STL layout (proxy_rt_logic.h, 2026-11-10): every proxy TU turns the VC9 iterator checks off before any STL header, as
// the fork's CvGameCoreDLLPCH.h does; otherwise std::string and std::vector have another size here than in the other
// TUs, and the linker keeps one copy of each shared template (std::vector<std::string>::insert): this file then
// crashed the headless host at its first push_back (3503, 2026-11-13 22:3x, b2; found under cdb, fixed by these lines).
#if defined(_MSC_VER) && !defined(_SECURE_SCL)
#define _SECURE_SCL 0
#endif
#if defined(_MSC_VER) && !defined(_HAS_ITERATOR_DEBUGGING)
#define _HAS_ITERATOR_DEBUGGING 0
#endif
#include "proxy_purity.h"
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <set>
#include <string>
#include <vector>

namespace
{
const char* const kHeader = "# vp64-purity 1";
const size_t kMaxFile = 1u << 20;
const size_t kMaxLine = 1024;
const size_t kMaxEntries = 65536;

std::set<std::string> g_methods;
std::set<std::string> g_lua;
bool g_attempted = false;

std::string Fmt(const char* fmt, ...)
{
	char b[1024];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(b, sizeof(b), _TRUNCATE, fmt, ap);
	va_end(ap);
	return b;
}

bool IsHexDigit(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

bool IsHex64(const std::string& s)
{
	if (s.size() != 64) return false;
	for (size_t i = 0; i < s.size(); ++i)
		if (!IsHexDigit(s[i])) return false;
	return true;
}

std::string Lower(const std::string& s)
{
	std::string r(s);
	for (size_t i = 0; i < r.size(); ++i)
		if (r[i] >= 'A' && r[i] <= 'F') r[i] = (char)(r[i] - 'A' + 'a');
	return r;
}

bool IsIdentStart(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
bool IsIdent(char c) { return IsIdentStart(c) || (c >= '0' && c <= '9'); }

// "Table.Function": two identifiers joined by one dot.
bool IsLuaName(const std::string& s)
{
	const size_t dot = s.find('.');
	if (dot == std::string::npos || dot == 0 || dot + 1 >= s.size() || s.find('.', dot + 1) != std::string::npos)
		return false;
	for (size_t i = 0; i < s.size(); ++i)
	{
		if (i == dot) continue;
		const bool start = i == 0 || i == dot + 1;
		if (start ? !IsIdentStart(s[i]) : !IsIdent(s[i])) return false;
	}
	return true;
}

// A model key: "<Interface>::<Method>(<params>)" with an optional " const": an identifier, "::", an identifier, "(",
// and the text ends with ")" or ") const"; printable ASCII only (checked by the caller).
bool IsMethodKey(const std::string& s)
{
	const size_t sep = s.find("::");
	if (sep == std::string::npos || sep == 0) return false;
	for (size_t i = 0; i < sep; ++i)
		if (i == 0 ? !IsIdentStart(s[i]) : !IsIdent(s[i])) return false;
	size_t i = sep + 2;
	if (i < s.size() && s[i] == '~') ++i;
	if (i >= s.size() || !IsIdentStart(s[i])) return false;
	while (i < s.size() && IsIdent(s[i])) ++i;
	if (i >= s.size() || s[i] != '(') return false;
	const std::string tail = ") const";
	if (s[s.size() - 1] == ')') return true;
	return s.size() > tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

void Split(const std::string& line, std::vector<std::string>& out)
{
	out.clear();
	size_t a = 0;
	for (;;)
	{
		const size_t t = line.find('\t', a);
		out.push_back(line.substr(a, t == std::string::npos ? std::string::npos : t - a));
		if (t == std::string::npos) break;
		a = t + 1;
	}
}

// Parses text into methods/lua. Returns "" on success, otherwise the reason (with the line number).
std::string Parse(const std::string& text, const std::string& wantSha, std::set<std::string>& methods,
                  std::set<std::string>& lua, std::string& derivedFrom, std::string& derivedFor)
{
	if (text.size() > kMaxFile) return Fmt("file larger than %u bytes", (unsigned)kMaxFile);
	if (text.find('\0') != std::string::npos) return "a NUL byte in the file";
	bool haveSha = false, haveDerived = false;
	size_t lineNo = 0, pos = 0;
	std::vector<std::string> f;
	while (pos < text.size())
	{
		size_t nl = text.find('\n', pos);
		if (nl == std::string::npos) nl = text.size();
		std::string line = text.substr(pos, nl - pos);
		pos = nl + 1;
		++lineNo;
		if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
		if (line.size() > kMaxLine) return Fmt("line %u: longer than %u bytes", (unsigned)lineNo, (unsigned)kMaxLine);
		for (size_t i = 0; i < line.size(); ++i)
		{
			const unsigned char c = (unsigned char)line[i];
			if ((c < 0x20 && c != '\t') || c == 0x7F)
				return Fmt("line %u: control character 0x%02X", (unsigned)lineNo, (unsigned)c);
			if (c >= 0x80) return Fmt("line %u: byte 0x%02X (keys are ASCII)", (unsigned)lineNo, (unsigned)c);
		}
		if (lineNo == 1)
		{
			if (line != kHeader)
				return Fmt("line 1 is not \"%s\" (an unknown format or version)", kHeader);
			continue;
		}
		if (line.empty() || line[0] == '#') continue;
		Split(line, f);
		const std::string& rec = f[0];
		for (size_t i = 1; i < f.size(); ++i)
			if (f[i].empty()) return Fmt("line %u: empty field %u of \"%s\"", (unsigned)lineNo, (unsigned)i + 1, rec.c_str());
		if (rec == "dll_sha256")
		{
			if (f.size() != 2) return Fmt("line %u: dll_sha256 takes 1 field, has %u", (unsigned)lineNo, (unsigned)f.size() - 1);
			if (haveSha) return Fmt("line %u: dll_sha256 repeated", (unsigned)lineNo);
			if (!IsHex64(f[1])) return Fmt("line %u: dll_sha256 is not 64 hex digits", (unsigned)lineNo);
			haveSha = true;
			if (Lower(f[1]) != wantSha)
				return Fmt("bound to another x64 DLL (file %.12s..., loaded DLL %.12s...)", Lower(f[1]).c_str(), wantSha.c_str());
		}
		else if (rec == "derived_from")
		{
			if (f.size() != 3) return Fmt("line %u: derived_from takes 2 fields, has %u", (unsigned)lineNo, (unsigned)f.size() - 1);
			if (haveDerived) return Fmt("line %u: derived_from repeated", (unsigned)lineNo);
			haveDerived = true;
			derivedFrom = f[1];
			derivedFor = f[2];
		}
		else if (rec == "method" || rec == "lua")
		{
			const bool m = rec == "method";
			if (f.size() != 2) return Fmt("line %u: %s takes 1 field, has %u", (unsigned)lineNo, rec.c_str(), (unsigned)f.size() - 1);
			if (m ? !IsMethodKey(f[1]) : !IsLuaName(f[1]))
				return Fmt("line %u: \"%s\" is not a %s", (unsigned)lineNo, f[1].c_str(),
				           m ? "method key (Interface::Method(params)[ const])" : "Lua name (Table.Function)");
			if (methods.size() + lua.size() >= kMaxEntries) return Fmt("more than %u entries", (unsigned)kMaxEntries);
			if (!(m ? methods : lua).insert(f[1]).second)
				return Fmt("line %u: %s \"%s\" listed twice", (unsigned)lineNo, rec.c_str(), f[1].c_str());
		}
		else
			return Fmt("line %u: unknown record \"%.40s\"", (unsigned)lineNo, rec.c_str());
	}
	if (lineNo == 0) return "the file is empty";
	if (!haveSha) return "no dll_sha256 line";
	if (!haveDerived) return "no derived_from line";
	return "";
}

bool ReadSmallFile(const char* path, std::string& text, std::string& why)
{
	HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
	{
		const DWORD e = GetLastError();
		why = (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) ? "no such file" : Fmt("cannot open (error %lu)", e);
		return false;
	}
	LARGE_INTEGER size;
	if (!GetFileSizeEx(h, &size))
	{
		why = Fmt("cannot read its size (error %lu)", GetLastError());
		CloseHandle(h);
		return false;
	}
	if (size.QuadPart > (LONGLONG)kMaxFile)
	{
		why = Fmt("file larger than %u bytes", (unsigned)kMaxFile);
		CloseHandle(h);
		return false;
	}
	text.resize((size_t)size.QuadPart);
	DWORD got = 0;
	const bool ok = text.empty() || (ReadFile(h, &text[0], (DWORD)text.size(), &got, NULL) && got == text.size());
	CloseHandle(h);
	if (!ok) why = "cannot read it";
	return ok;
}

// SHA-256 of a file (bcrypt.dll loaded by name, as proxy_shell.cpp: the proxy's import list is checked). Used only by
// the interim test load below.
bool Sha256FileHex(const char* path, std::string& hex, std::string& why)
{
	typedef LONG(WINAPI * OpenAlgFn)(void**, const wchar_t*, const wchar_t*, ULONG);
	typedef LONG(WINAPI * CreateHashFn)(void*, void**, unsigned char*, ULONG, unsigned char*, ULONG, ULONG);
	typedef LONG(WINAPI * HashDataFn)(void*, unsigned char*, ULONG, ULONG);
	typedef LONG(WINAPI * FinishHashFn)(void*, unsigned char*, ULONG, ULONG);
	typedef LONG(WINAPI * DestroyHashFn)(void*);
	typedef LONG(WINAPI * CloseAlgFn)(void*, ULONG);
	HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
	{
		why = Fmt("cannot open %s (error %lu)", path, GetLastError());
		return false;
	}
	HMODULE bc = LoadLibraryW(L"bcrypt.dll");
	OpenAlgFn openAlg = bc ? (OpenAlgFn)GetProcAddress(bc, "BCryptOpenAlgorithmProvider") : NULL;
	CreateHashFn createHash = bc ? (CreateHashFn)GetProcAddress(bc, "BCryptCreateHash") : NULL;
	HashDataFn hashData = bc ? (HashDataFn)GetProcAddress(bc, "BCryptHashData") : NULL;
	FinishHashFn finishHash = bc ? (FinishHashFn)GetProcAddress(bc, "BCryptFinishHash") : NULL;
	DestroyHashFn destroyHash = bc ? (DestroyHashFn)GetProcAddress(bc, "BCryptDestroyHash") : NULL;
	CloseAlgFn closeAlg = bc ? (CloseAlgFn)GetProcAddress(bc, "BCryptCloseAlgorithmProvider") : NULL;
	void* alg = NULL;
	void* hash = NULL;
	unsigned char* buf = NULL;
	unsigned char out[32];
	bool ok = false;
	why = "bcrypt.dll is not available";
	if (!openAlg || !createHash || !hashData || !finishHash || !destroyHash || !closeAlg) goto done;
	why = "SHA-256 could not be started";
	if (openAlg(&alg, L"SHA256", NULL, 0) != 0 || createHash(alg, &hash, NULL, 0, NULL, 0, 0) != 0) goto done;
	why = "the file could not be read";
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
	hex.clear();
	for (int i = 0; i < 32; ++i) hex += Fmt("%02x", out[i]);
	ok = true;
	why.clear();
done:
	free(buf);
	if (hash) destroyHash(hash);
	if (alg) closeAlg(alg, 0);
	if (bc) FreeLibrary(bc);
	CloseHandle(h);
	return ok;
}
}  // namespace

namespace ProxyPurity
{
bool Load(const char* path, const char* dllSha256Hex, std::string* why)
{
	g_attempted = true;
	g_methods.clear();
	g_lua.clear();
	const std::string p = path ? path : "";
	const std::string tail = "; both purity lists empty, every call crosses";
	if (p.empty())
	{
		if (why) *why = "purity: no file named" + tail;
		return false;
	}
	const std::string want = dllSha256Hex ? Lower(dllSha256Hex) : std::string();
	if (!IsHex64(want))
	{
		if (why) *why = "purity: " + p + ": the loaded x64 DLL's SHA-256 is not known (not 64 hex digits)" + tail;
		return false;
	}
	std::string text, err;
	if (!ReadSmallFile(p.c_str(), text, err))
	{
		if (why) *why = "purity: " + p + ": " + err + tail;
		return false;
	}
	std::set<std::string> m, l;
	std::string from, forCommit;
	err = Parse(text, want, m, l, from, forCommit);
	if (!err.empty())
	{
		if (why) *why = "purity: " + p + ": refused: " + err + tail;
		return false;
	}
	g_methods.swap(m);
	g_lua.swap(l);
	if (why)
		*why = Fmt("purity: %s: %u method keys, %u Lua names; bound to the x64 DLL %.12s...; derived from %.12s for %.12s",
		           p.c_str(), (unsigned)g_methods.size(), (unsigned)g_lua.size(), want.c_str(), from.c_str(),
		           forCommit.c_str());
	return true;
}

void Clear()
{
	g_methods.clear();
	g_lua.clear();
}

bool IsPureMethodKey(const char* key) { return key && g_methods.count(key) != 0; }

bool IsPureLuaName(const char* name) { return name && g_lua.count(name) != 0; }

size_t MethodCount() { return g_methods.size(); }
size_t LuaCount() { return g_lua.size(); }

// Whether Load ran at all (the boot's call, issue 3504; in proxy_purity.h since the 3500 thread #170).
bool LoadAttempted() { return g_attempted; }

// Interim until the boot calls Load (issue 3504): the test-only environment variable VP_PROXY_PURITY =
// "<purity.tsv>|<x64 DLL>" loads the file bound to that DLL's SHA-256. Returns false (lists empty) when it is not set.
// ProxyLocal::InitTables calls this only when LoadAttempted() is false.
bool LoadFromTestEnvironment(std::string* why)
{
	char spec[2048];
	const DWORD n = GetEnvironmentVariableA("VP_PROXY_PURITY", spec, sizeof(spec));
	if (n == 0 || n >= sizeof(spec))
	{
		if (why)
			*why = n == 0 ? "purity: the boot loaded no purity file (VP_PROXY_PURITY not set); both purity lists empty, "
			                "every call crosses"
			              : "purity: VP_PROXY_PURITY longer than 2047 characters; both purity lists empty, every call crosses";
		g_attempted = true;
		g_methods.clear();
		g_lua.clear();
		return false;
	}
	const std::string s(spec);
	const size_t bar = s.find('|');
	if (bar == std::string::npos)
	{
		if (why) *why = "purity: VP_PROXY_PURITY is not \"<purity.tsv>|<x64 DLL>\"; both purity lists empty, every call crosses";
		g_attempted = true;
		g_methods.clear();
		g_lua.clear();
		return false;
	}
	std::string hex, err;
	if (!Sha256FileHex(s.substr(bar + 1).c_str(), hex, err))
	{
		if (why) *why = "purity: VP_PROXY_PURITY: the x64 DLL: " + err + "; both purity lists empty, every call crosses";
		g_attempted = true;
		g_methods.clear();
		g_lua.clear();
		return false;
	}
	const bool ok = Load(s.substr(0, bar).c_str(), hex.c_str(), why);
	if (why) *why += " (interim test load from VP_PROXY_PURITY)";
	return ok;
}

// Interim as well (a live instance's D28 layout, where no environment variable reaches the EXE): <x64Dir>\purity.tsv
// bound to the x64 DLL there: <x64Dir>\CvGameCore_Expansion2_x64.dll when it exists, else <x64Dir>\CvGameCore_Expansion2.dll
// (issue 2606, the rule of ProxyLogic::X64DllName without a manifest). Returns false with the lists empty when the file is
// not there.
bool LoadBesideX64(const char* x64Dir, std::string* why)
{
	g_attempted = true;
	g_methods.clear();
	g_lua.clear();
	const std::string dir = x64Dir ? x64Dir : "";
	const std::string file = dir + "\\purity.tsv", renamed = dir + "\\CvGameCore_Expansion2_x64.dll";
	const DWORD at = GetFileAttributesA(renamed.c_str());
	const std::string dll =
	    at != INVALID_FILE_ATTRIBUTES && !(at & FILE_ATTRIBUTE_DIRECTORY) ? renamed : dir + "\\CvGameCore_Expansion2.dll";
	if (dir.empty() || GetFileAttributesA(file.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		if (why)
			*why = "purity: the boot loaded no purity file (VP_PROXY_PURITY not set, no " + (dir.empty() ? std::string("x64_dir")
			       : file) + "); both purity lists empty, every call crosses";
		return false;
	}
	std::string hex, err;
	if (!Sha256FileHex(dll.c_str(), hex, err))
	{
		if (why) *why = "purity: " + file + ": the x64 DLL: " + err + "; both purity lists empty, every call crosses";
		return false;
	}
	const bool ok = Load(file.c_str(), hex.c_str(), why);
	if (why) *why += " (interim load beside the x64 DLL)";
	return ok;
}
}  // namespace ProxyPurity
