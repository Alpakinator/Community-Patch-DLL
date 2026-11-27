// proxy_pack.cpp - the DLL pack and the run-time match (epic 3500, issue 3504). See proxy_pack.h.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "proxy_pack.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <vector>

namespace
{
double NowMs()
{
	LARGE_INTEGER f, t;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&t);
	return (double)t.QuadPart * 1000.0 / (double)f.QuadPart;
}

std::string Fmt(const char* fmt, ...)
{
	char b[1024];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(b, sizeof(b), _TRUNCATE, fmt, ap);
	va_end(ap);
	return b;
}

// ================================================================================================ a small JSON reader
// Enough JSON for the manifest: objects, arrays, strings (with escapes), numbers, true, false, null. The result is
// flat: every value under its dotted path ("x64_dll.sha256"; array items "a.0", "a.1"), with its JSON type.
struct JValue
{
	char type;  // 'o' object, 'a' array, 's' string, 'n' number, 'b' true/false, 'z' null
	std::string text;
};
typedef std::map<std::string, JValue> JMap;

class JReader
{
public:
	JReader(const std::string& s, JMap& out) : m_s(s), m_i(0), m_out(out) {}
	bool Document(std::string& why)
	{
		Ws();
		if (m_i >= m_s.size() || m_s[m_i] != '{') return Err(why, "the text is not a JSON object");
		if (!Value("", 0, why)) return false;
		Ws();
		if (m_i != m_s.size()) return Err(why, "text after the JSON object");
		return true;
	}

private:
	const std::string& m_s;
	size_t m_i;
	JMap& m_out;

	bool Err(std::string& why, const char* what)
	{
		why = Fmt("%s (at byte %u)", what, (unsigned)m_i);
		return false;
	}
	void Ws()
	{
		while (m_i < m_s.size() && (m_s[m_i] == ' ' || m_s[m_i] == '\t' || m_s[m_i] == '\r' || m_s[m_i] == '\n')) ++m_i;
	}
	static std::string Join(const std::string& path, const std::string& key)
	{
		return path.empty() ? key : path + "." + key;
	}
	bool Put(const std::string& path, char type, const std::string& text, std::string& why)
	{
		if (m_out.count(path)) return Err(why, ("duplicate key '" + path + "'").c_str());
		JValue v;
		v.type = type;
		v.text = text;
		m_out[path] = v;
		return true;
	}
	bool Hex4(unsigned& u)
	{
		if (m_i + 4 > m_s.size()) return false;
		u = 0;
		for (int k = 0; k < 4; ++k)
		{
			const char c = m_s[m_i++];
			u <<= 4;
			if (c >= '0' && c <= '9') u |= (unsigned)(c - '0');
			else if (c >= 'a' && c <= 'f') u |= (unsigned)(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F') u |= (unsigned)(c - 'A' + 10);
			else return false;
		}
		return true;
	}
	bool String(std::string& out, std::string& why)
	{
		++m_i;  // the opening quote
		out.clear();
		while (m_i < m_s.size())
		{
			const unsigned char c = (unsigned char)m_s[m_i++];
			if (c == '"') return true;
			if (c < 0x20) return Err(why, "a control character in a string");
			if (c != '\\')
			{
				out += (char)c;
				continue;
			}
			if (m_i >= m_s.size()) break;
			const char e = m_s[m_i++];
			switch (e)
			{
			case '"': out += '"'; break;
			case '\\': out += '\\'; break;
			case '/': out += '/'; break;
			case 'b': out += '\b'; break;
			case 'f': out += '\f'; break;
			case 'n': out += '\n'; break;
			case 'r': out += '\r'; break;
			case 't': out += '\t'; break;
			case 'u':
			{
				unsigned u = 0;
				if (!Hex4(u)) return Err(why, "a bad \\u escape");
				// UTF-8 of the code unit (a surrogate pair stays two 3-byte sequences: no manifest field needs one)
				if (u < 0x80) out += (char)u;
				else if (u < 0x800)
				{
					out += (char)(0xC0 | (u >> 6));
					out += (char)(0x80 | (u & 0x3F));
				}
				else
				{
					out += (char)(0xE0 | (u >> 12));
					out += (char)(0x80 | ((u >> 6) & 0x3F));
					out += (char)(0x80 | (u & 0x3F));
				}
				break;
			}
			default: return Err(why, "a bad escape in a string");
			}
		}
		return Err(why, "an unterminated string");
	}
	bool Number(std::string& out, std::string& why)
	{
		const size_t s = m_i;
		if (m_s[m_i] == '-') ++m_i;
		size_t d = m_i;
		while (m_i < m_s.size() && m_s[m_i] >= '0' && m_s[m_i] <= '9') ++m_i;
		if (m_i == d) return Err(why, "a bad number");
		if (m_i < m_s.size() && m_s[m_i] == '.')
		{
			d = ++m_i;
			while (m_i < m_s.size() && m_s[m_i] >= '0' && m_s[m_i] <= '9') ++m_i;
			if (m_i == d) return Err(why, "a bad number");
		}
		if (m_i < m_s.size() && (m_s[m_i] == 'e' || m_s[m_i] == 'E'))
		{
			++m_i;
			if (m_i < m_s.size() && (m_s[m_i] == '+' || m_s[m_i] == '-')) ++m_i;
			d = m_i;
			while (m_i < m_s.size() && m_s[m_i] >= '0' && m_s[m_i] <= '9') ++m_i;
			if (m_i == d) return Err(why, "a bad number");
		}
		out = m_s.substr(s, m_i - s);
		return true;
	}
	bool Word(const char* w)
	{
		const size_t n = strlen(w);
		if (m_s.compare(m_i, n, w) != 0) return false;
		m_i += n;
		return true;
	}
	bool Value(const std::string& path, int depth, std::string& why)
	{
		if (depth > 16) return Err(why, "nested too deeply");
		Ws();
		if (m_i >= m_s.size()) return Err(why, "the text ends where a value was expected");
		const char c = m_s[m_i];
		std::string t;
		if (c == '{')
		{
			++m_i;
			if (!path.empty() && !Put(path, 'o', "", why)) return false;
			Ws();
			if (m_i < m_s.size() && m_s[m_i] == '}')
			{
				++m_i;
				return true;
			}
			for (;;)
			{
				Ws();
				if (m_i >= m_s.size() || m_s[m_i] != '"') return Err(why, "a key was expected");
				std::string key;
				if (!String(key, why)) return false;
				Ws();
				if (m_i >= m_s.size() || m_s[m_i] != ':') return Err(why, "':' was expected");
				++m_i;
				if (!Value(Join(path, key), depth + 1, why)) return false;
				Ws();
				if (m_i < m_s.size() && m_s[m_i] == ',')
				{
					++m_i;
					continue;
				}
				if (m_i < m_s.size() && m_s[m_i] == '}')
				{
					++m_i;
					return true;
				}
				return Err(why, "',' or '}' was expected");
			}
		}
		if (c == '[')
		{
			++m_i;
			if (!Put(path, 'a', "", why)) return false;
			Ws();
			if (m_i < m_s.size() && m_s[m_i] == ']')
			{
				++m_i;
				return true;
			}
			for (unsigned n = 0;; ++n)
			{
				if (!Value(Join(path, Fmt("%u", n)), depth + 1, why)) return false;
				Ws();
				if (m_i < m_s.size() && m_s[m_i] == ',')
				{
					++m_i;
					continue;
				}
				if (m_i < m_s.size() && m_s[m_i] == ']')
				{
					++m_i;
					return true;
				}
				return Err(why, "',' or ']' was expected");
			}
		}
		if (c == '"') return String(t, why) && Put(path, 's', t, why);
		if (c == '-' || (c >= '0' && c <= '9')) return Number(t, why) && Put(path, 'n', t, why);
		if (Word("true")) return Put(path, 'b', "true", why);
		if (Word("false")) return Put(path, 'b', "false", why);
		if (Word("null")) return Put(path, 'z', "", why);
		return Err(why, "a value was expected");
	}
};

bool IsHex64(const std::string& s)
{
	if (s.size() != 64) return false;
	for (size_t i = 0; i < s.size(); ++i)
	{
		const char c = s[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
	}
	return true;
}

std::string Lower(std::string s)
{
	for (size_t i = 0; i < s.size(); ++i)
		if (s[i] >= 'A' && s[i] <= 'Z') s[i] = (char)(s[i] - 'A' + 'a');
	return s;
}

// A plain file name: no folder part, no drive, not "." or "..", printable ASCII.
bool IsPlainName(const std::string& s)
{
	if (s.empty() || s == "." || s == ".." || s.size() > 200) return false;
	for (size_t i = 0; i < s.size(); ++i)
	{
		const unsigned char c = (unsigned char)s[i];
		if (c < 0x20 || c >= 0x7F || c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
		    c == '>' || c == '|')
			return false;
	}
	return true;
}

std::string Short(const std::string& h)
{
	return h.size() > 8 ? h.substr(0, 8) + "..." : h;
}

std::string PathJoin(const std::string& dir, const std::string& name)
{
	if (dir.empty()) return name;
	const char last = dir[dir.size() - 1];
	return (last == '\\' || last == '/') ? dir + name : dir + "\\" + name;
}

// ================================================================================================ the notice
typedef BOOL(WINAPI* EnumWindowsFn)(WNDENUMPROC, LPARAM);
typedef DWORD(WINAPI* GetWindowThreadProcessIdFn)(HWND, LPDWORD);
typedef BOOL(WINAPI* IsWindowVisibleFn)(HWND);
typedef HWND(WINAPI* GetWindowFn)(HWND, UINT);
typedef int(WINAPI* GetClassNameAFn)(HWND, LPSTR, int);
typedef BOOL(WINAPI* GetWindowRectFn)(HWND, LPRECT);
typedef int(WINAPI* GetWindowTextWFn)(HWND, LPWSTR, int);
typedef LRESULT(WINAPI* SendMessageTimeoutWFn)(HWND, UINT, WPARAM, LPARAM, UINT, UINT, PDWORD_PTR);

struct FindCtx
{
	GetWindowThreadProcessIdFn tid;
	IsWindowVisibleFn visible;
	GetWindowFn getWindow;
	GetClassNameAFn className;
	GetWindowRectFn rect;
	DWORD pid;
	HWND best;
	LONGLONG bestArea;
	unsigned seen;
};

BOOL CALLBACK FindMain(HWND h, LPARAM lp)
{
	FindCtx* c = reinterpret_cast<FindCtx*>(lp);
	DWORD pid = 0;
	c->tid(h, &pid);
	if (pid != c->pid || !c->visible(h) || c->getWindow(h, GW_OWNER) != NULL) return TRUE;
	char cls[64];
	cls[0] = 0;
	c->className(h, cls, sizeof(cls));
	if (!strcmp(cls, "IME") || !strcmp(cls, "MSCTFIME UI")) return TRUE;
	++c->seen;
	RECT r;
	if (!c->rect(h, &r)) return TRUE;
	const LONGLONG area = (LONGLONG)(r.right - r.left) * (LONGLONG)(r.bottom - r.top);
	if (!c->best || area > c->bestArea)
	{
		c->best = h;
		c->bestArea = area;
	}
	return TRUE;
}
}  // namespace

namespace ProxyPack
{
const char* StatusName(Status s)
{
	switch (s)
	{
	case kOk: return "ok";
	case kNoPack: return "no-pack";
	case kUnreadable: return "unreadable";
	case kBadJson: return "bad-json";
	case kUnknownFormat: return "unknown-format";
	case kMissingField: return "missing-field";
	case kBadField: return "bad-field";
	case kNoFallback: return "no-fallback";
	case kOtherDll: return "other-dll";
	case kFileMissing: return "file-missing";
	case kHashMismatch: return "hash-mismatch";
	}
	return "?";
}

Status ParseManifest(const std::string& text, Manifest& m, std::string& why)
{
	m = Manifest();
	JMap j;
	JReader rd(text, j);
	if (!rd.Document(why))
	{
		why = "manifest.json is not valid JSON: " + why;
		return kBadJson;
	}
	JMap::const_iterator f = j.find("format");
	if (f == j.end() || f->second.type != 'n')
	{
		why = f == j.end() ? "manifest.json has no \"format\"" : "manifest.json: \"format\" is not a number";
		return kMissingField;
	}
	if (f->second.text != "1")
	{
		why = "manifest.json has format " + f->second.text + "; this proxy reads format 1";
		return kUnknownFormat;
	}
	m.format = 1;
	static const char* const kReq[] = {"release", "x64_dll.file", "x64_dll.sha256", "purity.file", "purity.sha256"};
	std::string* const dst[] = {&m.release, &m.x64File, &m.x64Sha, &m.purityFile, &m.puritySha};
	for (size_t i = 0; i < sizeof(kReq) / sizeof(kReq[0]); ++i)
	{
		JMap::const_iterator v = j.find(kReq[i]);
		if (v == j.end() || v->second.type != 's')
		{
			why = Fmt("manifest.json: \"%s\" is %s", kReq[i], v == j.end() ? "missing" : "not a string");
			return kMissingField;
		}
		*dst[i] = v->second.text;
	}
	// x86_dlls: [{"sha256": ..., "origin": ...}, ...] (MC ruling, thread #203)
	{
		JMap::const_iterator a = j.find("x86_dlls");
		if (a == j.end() || a->second.type != 'a')
		{
			why = Fmt("manifest.json: \"x86_dlls\" is %s", a == j.end() ? "missing" : "not an array");
			return kMissingField;
		}
		for (unsigned i = 0;; ++i)
		{
			const std::string base = Fmt("x86_dlls.%u", i);
			JMap::const_iterator e = j.find(base);
			if (e == j.end()) break;
			JMap::const_iterator h = j.find(base + ".sha256"), o = j.find(base + ".origin");
			if (e->second.type != 'o' || h == j.end() || h->second.type != 's' || o == j.end() || o->second.type != 's')
			{
				why = Fmt("manifest.json: \"x86_dlls\" item %u is not an object with the strings sha256 and origin", i);
				return kMissingField;
			}
			if (!IsHex64(h->second.text))
			{
				why = Fmt("manifest.json: \"x86_dlls\" item %u: sha256 is not 64 hex digits", i);
				return kBadField;
			}
			m.x86Shas.push_back(Lower(h->second.text));
			m.x86Origins.push_back(o->second.text);
		}
		if (m.x86Shas.empty() || m.x86Shas.size() > 16)
		{
			why = Fmt("manifest.json: \"x86_dlls\" has %u items (1 to 16 allowed)", (unsigned)m.x86Shas.size());
			return m.x86Shas.empty() ? kMissingField : kBadField;
		}
	}
	JMap::const_iterator b = j.find("built.fork_commit");
	if (b != j.end() && b->second.type == 's') m.forkCommit = b->second.text;
	b = j.find("built.tools_commit");
	if (b != j.end() && b->second.type == 's') m.toolsCommit = b->second.text;
	const char* const shaName[] = {"x64_dll.sha256", "purity.sha256"};
	std::string* const sha[] = {&m.x64Sha, &m.puritySha};
	for (int i = 0; i < 2; ++i)
	{
		if (!IsHex64(*sha[i]))
		{
			why = Fmt("manifest.json: \"%s\" is not 64 hex digits", shaName[i]);
			return kBadField;
		}
		*sha[i] = Lower(*sha[i]);
	}
	if (!IsPlainName(m.x64File) || !IsPlainName(m.purityFile))
	{
		why = Fmt("manifest.json: \"%s\" is not a plain file name", !IsPlainName(m.x64File) ? "x64_dll.file" : "purity.file");
		return kBadField;
	}
	if (m.release.empty())
	{
		why = "manifest.json: \"release\" is empty";
		return kBadField;
	}
	why = "manifest.json format 1, release " + m.release;
	return kOk;
}

bool Sha256File(const std::string& path, std::string& hex, std::string& why, HANDLE* keep)
{
	typedef LONG(WINAPI * OpenAlgFn)(void**, const wchar_t*, const wchar_t*, ULONG);
	typedef LONG(WINAPI * CreateHashFn)(void*, void**, unsigned char*, ULONG, unsigned char*, ULONG, ULONG);
	typedef LONG(WINAPI * HashDataFn)(void*, unsigned char*, ULONG, ULONG);
	typedef LONG(WINAPI * FinishHashFn)(void*, unsigned char*, ULONG, ULONG);
	typedef LONG(WINAPI * DestroyHashFn)(void*);
	typedef LONG(WINAPI * CloseAlgFn)(void*, ULONG);
	hex.clear();
	if (keep) *keep = INVALID_HANDLE_VALUE;
	// Read sharing only: from here until the caller closes it, nobody can change the bytes (LoadLibrary still can map it).
	HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN,
	                       NULL);
	if (h == INVALID_HANDLE_VALUE)
	{
		why = Fmt("cannot open %s (error %u)", path.c_str(), (unsigned)GetLastError());
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
	bool ok = false;
	unsigned char* buf = NULL;
	unsigned char out[32];
	why = "bcrypt.dll is not available";
	if (!openAlg || !createHash || !hashData || !finishHash || !destroyHash || !closeAlg) goto done;
	why = "SHA-256 could not be started";
	if (openAlg(&alg, L"SHA256", NULL, 0) != 0 || createHash(alg, &hash, NULL, 0, NULL, 0, 0) != 0) goto done;
	why = Fmt("cannot read %s", path.c_str());
	buf = static_cast<unsigned char*>(malloc(1 << 20));
	if (!buf) goto done;
	for (;;)
	{
		DWORD got = 0;
		if (!ReadFile(h, buf, 1 << 20, &got, NULL)) goto done;
		if (got == 0) break;
		if (hashData(hash, buf, got, 0) != 0) goto done;
	}
	if (finishHash(hash, out, 32, 0) != 0) goto done;
	for (int i = 0; i < 32; ++i) hex += Fmt("%02x", out[i]);
	ok = true;
	why.clear();
done:
	free(buf);
	if (hash) destroyHash(hash);
	if (alg) closeAlg(alg, 0);
	if (bc) FreeLibrary(bc);
	if (ok && keep)
		*keep = h;
	else
		CloseHandle(h);
	return ok;
}

Status Check(const std::string& dir, const std::string& fallbackDll, Result& r)
{
	ReleaseX64(r);
	ReleasePurity(r);
	const double t0 = NowMs();
	r.dir = dir;
	r.m = Manifest();
	r.x64Path.clear();
	r.purityPath.clear();
	r.fallbackSha.clear();
	r.x86Origin.clear();
	r.notice.clear();
	const std::string mpath = PathJoin(dir, VP64PACK_MANIFEST);
	std::string why;
	Status st = kOk;
	std::string text;
	{
		HANDLE h = CreateFileA(mpath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (h == INVALID_HANDLE_VALUE)
		{
			const DWORD e = GetLastError();
			if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)
			{
				st = kNoPack;
				why = "no DLL pack: " + mpath + " does not exist";
			}
			else
			{
				st = kUnreadable;
				why = Fmt("%s cannot be opened (error %u)", mpath.c_str(), (unsigned)e);
			}
		}
		else
		{
			LARGE_INTEGER sz;
			if (!GetFileSizeEx(h, &sz) || sz.QuadPart > 64 * 1024)
			{
				st = kUnreadable;
				why = mpath + " is larger than 64 KiB or its size cannot be read";
			}
			else
			{
				text.resize((size_t)sz.QuadPart);
				DWORD got = 0;
				if (sz.QuadPart > 0 && (!ReadFile(h, &text[0], (DWORD)sz.QuadPart, &got, NULL) || got != (DWORD)sz.QuadPart))
				{
					st = kUnreadable;
					why = mpath + " cannot be read";
				}
			}
			CloseHandle(h);
		}
	}
	if (st == kOk) st = ParseManifest(text, r.m, why);
	if (st == kOk)
	{
		std::string w;
		if (!Sha256File(fallbackDll, r.fallbackSha, w, NULL))
		{
			st = kNoFallback;
			why = "the 32-bit DLL fallback_dll cannot be hashed: " + w;
		}
		else
		{
			size_t k = 0;
			while (k < r.m.x86Shas.size() && r.m.x86Shas[k] != r.fallbackSha) ++k;
			if (k < r.m.x86Shas.size())
				r.x86Origin = r.m.x86Origins[k];
			else
			{
				// The loud line names the full hash (MC ruling): a player's DLL the release does not list.
				st = kOtherDll;
				why = "fallback_dll " + fallbackDll + " has SHA-256 " + r.fallbackSha + ", which the pack (release " +
				      r.m.release + ") does not list:";
				for (size_t i = 0; i < r.m.x86Shas.size(); ++i)
					why += (i ? ", " : " ") + Short(r.m.x86Shas[i]) + " (" + r.m.x86Origins[i] + ")";
			}
		}
	}
	if (st == kOk)
	{
		const std::string names[2] = {r.m.x64File, r.m.purityFile};
		const std::string want[2] = {r.m.x64Sha, r.m.puritySha};
		HANDLE* held[2] = {&r.heldX64, &r.heldPurity};
		std::string* paths[2] = {&r.x64Path, &r.purityPath};
		for (int i = 0; i < 2 && st == kOk; ++i)
		{
			*paths[i] = PathJoin(dir, names[i]);
			std::string got, w;
			if (!Sha256File(*paths[i], got, w, held[i]))
			{
				st = w.compare(0, 11, "cannot open") == 0 ? kFileMissing : kUnreadable;
				why = Fmt("%s %s: %s", i == 0 ? "the x64 DLL" : "the purity file", names[i].c_str(), w.c_str());
			}
			else if (got != want[i])
			{
				st = kHashMismatch;
				why = Fmt("%s %s has SHA-256 %s, the manifest says %s", i == 0 ? "the x64 DLL" : "the purity file",
				          paths[i]->c_str(), Short(got).c_str(), Short(want[i]).c_str());
			}
		}
	}
	r.status = st;
	r.ms = NowMs() - t0;
	if (st == kOk)
		r.why = Fmt("pack %s ok: release %s, 32-bit DLL %s matches x86_dlls (%s), x64 DLL %s %s, purity %s %s (%s; built from "
		            "fork %s, tools %s; checked in %.1f ms)",
		            dir.c_str(), r.m.release.c_str(), Short(r.fallbackSha).c_str(), r.x86Origin.c_str(), r.m.x64File.c_str(),
		            Short(r.m.x64Sha).c_str(), r.m.purityFile.c_str(), Short(r.m.puritySha).c_str(), "held open read-only",
		            r.m.forkCommit.empty() ? "?" : r.m.forkCommit.c_str(),
		            r.m.toolsCommit.empty() ? "?" : r.m.toolsCommit.c_str(), r.ms);
	else
	{
		ReleaseX64(r);
		ReleasePurity(r);
		r.why = Fmt("pack check %s: ", StatusName(st)) + why + Fmt(" (%.1f ms)", r.ms);
		switch (st)
		{
		case kNoPack: r.notice = "no 64-bit DLL pack is installed"; break;
		case kOtherDll: r.notice = "the 64-bit DLL pack is for another Vox Populi version"; break;
		case kNoFallback: r.notice = "the original 32-bit DLL is missing"; break;
		default: r.notice = "the 64-bit DLL pack is damaged"; break;
		}
	}
	return st;
}

void ReleaseX64(Result& r)
{
	if (r.heldX64 != INVALID_HANDLE_VALUE) CloseHandle(r.heldX64);
	r.heldX64 = INVALID_HANDLE_VALUE;
}

void ReleasePurity(Result& r)
{
	if (r.heldPurity != INVALID_HANDLE_VALUE) CloseHandle(r.heldPurity);
	r.heldPurity = INVALID_HANDLE_VALUE;
}

std::string ShowNotice(const std::string& text, const std::string& stripPrefix)
{
	HMODULE u = LoadLibraryW(L"user32.dll");
	if (!u) return "notice: user32.dll could not be loaded; the log line is the only notice";
	FindCtx c;
	memset(&c, 0, sizeof(c));
	EnumWindowsFn enumWindows = (EnumWindowsFn)GetProcAddress(u, "EnumWindows");
	c.tid = (GetWindowThreadProcessIdFn)GetProcAddress(u, "GetWindowThreadProcessId");
	c.visible = (IsWindowVisibleFn)GetProcAddress(u, "IsWindowVisible");
	c.getWindow = (GetWindowFn)GetProcAddress(u, "GetWindow");
	c.className = (GetClassNameAFn)GetProcAddress(u, "GetClassNameA");
	c.rect = (GetWindowRectFn)GetProcAddress(u, "GetWindowRect");
	GetWindowTextWFn getText = (GetWindowTextWFn)GetProcAddress(u, "GetWindowTextW");
	SendMessageTimeoutWFn send = (SendMessageTimeoutWFn)GetProcAddress(u, "SendMessageTimeoutW");
	std::string res;
	if (!enumWindows || !c.tid || !c.visible || !c.getWindow || !c.className || !c.rect || !getText || !send)
		res = "notice: a user32 function is missing; the log line is the only notice";
	else
	{
		c.pid = GetCurrentProcessId();
		enumWindows(FindMain, reinterpret_cast<LPARAM>(&c));
		if (!c.best)
			res = "notice: this process has no visible top-level window; the log line is the only notice";
		else
		{
			wchar_t title[512];
			title[0] = 0;
			getText(c.best, title, 512);
			std::wstring wmark, wtext;
			const std::string mark = VP64PACK_NOTICE_MARK;
			for (size_t i = 0; i < mark.size(); ++i) wmark += (wchar_t)(unsigned char)mark[i];
			for (size_t i = 0; i < text.size(); ++i) wtext += (wchar_t)(unsigned char)text[i];
			std::wstring t = UnprefixedTitle(title, Widen(stripPrefix));  // ticket 0680: the patch is off: no prefix
			const size_t at = t.find(L" - " + wmark);
			if (at != std::wstring::npos) t = t.substr(0, at);  // a second boot in one process: one notice, the newest
			t += L" - " + wmark + L": " + wtext;
			DWORD_PTR r = 0;
			const LRESULT ok = send(c.best, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(t.c_str()),
			                        SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 1000, &r);
			char cls[64];
			cls[0] = 0;
			c.className(c.best, cls, sizeof(cls));
			if (ok && r)
				res = Fmt("notice: the title of window %p (class %s, %u candidate(s)) now ends with \" - %s: %s\"", c.best,
				          cls, c.seen, VP64PACK_NOTICE_MARK, text.c_str());
			else
				res = Fmt("notice: WM_SETTEXT to window %p (class %s) failed or timed out (error %u); the log line is the "
				          "only notice",
				          c.best, cls, (unsigned)GetLastError());
		}
	}
	FreeLibrary(u);
	return res;
}

// ================================================================================================ the title prefix
std::wstring PrefixedTitle(const std::wstring& current, const std::wstring& prefix, bool* changed)
{
	const bool add = !prefix.empty() && current.compare(0, prefix.size(), prefix) != 0;
	if (changed) *changed = add;
	return add ? prefix + current : current;
}

std::wstring UnprefixedTitle(const std::wstring& current, const std::wstring& prefix)
{
	if (prefix.empty() || current.compare(0, prefix.size(), prefix) != 0) return current;
	return current.substr(prefix.size());
}

std::wstring Widen(const std::string& utf8)
{
	if (utf8.empty()) return std::wstring();
	const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), (int)utf8.size(), NULL, 0);
	if (n <= 0) return std::wstring();
	std::wstring w((size_t)n, L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), (int)utf8.size(), &w[0], n);
	return w;
}

namespace
{
typedef int(WINAPI* InternalGetWindowTextFn)(HWND, LPWSTR, int);

struct GameFind
{
	GetWindowThreadProcessIdFn tid;
	GetWindowFn getWindow;
	GetClassNameAFn className;
	GetWindowRectFn rect;
	DWORD pid;
	HWND best;
	LONGLONG bestArea;
};

BOOL CALLBACK FindGame(HWND h, LPARAM lp)
{
	GameFind* c = reinterpret_cast<GameFind*>(lp);
	DWORD pid = 0;
	c->tid(h, &pid);
	if (pid != c->pid || c->getWindow(h, GW_OWNER) != NULL) return TRUE;
	char cls[64];
	cls[0] = 0;
	c->className(h, cls, sizeof(cls));
	if (strcmp(cls, VP64PACK_GAME_CLASS) != 0) return TRUE;
	RECT r;
	LONGLONG area = 0;
	if (c->rect(h, &r)) area = (LONGLONG)(r.right - r.left) * (LONGLONG)(r.bottom - r.top);
	if (!c->best || area > c->bestArea)
	{
		c->best = h;
		c->bestArea = area;
	}
	return TRUE;
}

// user32 by name (the proxy's import list stays kernel32, lua51_Win32, msvcr90, msvcp90); loaded once, never freed
// while a keeper may run.
struct User32
{
	HMODULE mod;
	EnumWindowsFn enumWindows;
	GetWindowThreadProcessIdFn tid;
	GetWindowFn getWindow;
	GetClassNameAFn className;
	GetWindowRectFn rect;
	InternalGetWindowTextFn internalText;
	SendMessageTimeoutWFn send;
	bool Load()
	{
		if (!mod) mod = LoadLibraryW(L"user32.dll");
		if (!mod) return false;
		enumWindows = (EnumWindowsFn)GetProcAddress(mod, "EnumWindows");
		tid = (GetWindowThreadProcessIdFn)GetProcAddress(mod, "GetWindowThreadProcessId");
		getWindow = (GetWindowFn)GetProcAddress(mod, "GetWindow");
		className = (GetClassNameAFn)GetProcAddress(mod, "GetClassNameA");
		rect = (GetWindowRectFn)GetProcAddress(mod, "GetWindowRect");
		internalText = (InternalGetWindowTextFn)GetProcAddress(mod, "InternalGetWindowText");
		send = (SendMessageTimeoutWFn)GetProcAddress(mod, "SendMessageTimeoutW");
		return enumWindows && tid && getWindow && className && rect && internalText && send;
	}
};
User32 g_u32;

struct Keeper
{
	HANDLE thread;
	volatile LONG stop, busy;
	volatile LONG ticks, noWindow, applied, failed;
	std::wstring prefix;
	std::string prefixUtf8;
	unsigned everyMs;
	TitleLogFn log;
	double startMs;
};
Keeper g_keeper;

std::string Narrow(const std::wstring& w)
{
	if (w.empty()) return std::string();
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), NULL, 0, NULL, NULL);
	if (n <= 0) return std::string();
	std::string s((size_t)n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, NULL, NULL);
	return s;
}

HWND FindGameWith(const User32& u)
{
	GameFind c;
	memset(&c, 0, sizeof(c));
	c.tid = u.tid;
	c.getWindow = u.getWindow;
	c.className = u.className;
	c.rect = u.rect;
	c.pid = GetCurrentProcessId();
	u.enumWindows(FindGame, reinterpret_cast<LPARAM>(&c));
	return c.best;
}

void KeeperTick()
{
	InterlockedIncrement(&g_keeper.ticks);
	const HWND h = FindGameWith(g_u32);
	if (!h)
	{
		InterlockedIncrement(&g_keeper.noWindow);
		return;
	}
	wchar_t buf[1024];
	const int n = g_u32.internalText(h, buf, 1024);
	if (n < 0 || n >= 1023) return;  // a title this long is not the EXE's: left alone
	const std::wstring cur(buf, (size_t)n);
	bool changed = false;
	const std::wstring want = PrefixedTitle(cur, g_keeper.prefix, &changed);
	if (!changed) return;
	DWORD_PTR r = 0;
	const LRESULT ok = g_u32.send(h, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(want.c_str()), SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT,
	                              250, &r);
	const double at = (NowMs() - g_keeper.startMs) / 1000.0;
	if (ok && r)
	{
		const LONG k = InterlockedIncrement(&g_keeper.applied);
		if (g_keeper.log)
			g_keeper.log(Fmt("title: %s window %p's title \"%s\" -> \"%s\" (%.1f s after the keeper's start)",
			                 k == 1 ? "set the prefix on the game" : "the title had lost its prefix: set it again on the game",
			                 h, Narrow(cur).c_str(), Narrow(want).c_str(), at)
			                 .c_str());
	}
	else
	{
		const DWORD err = GetLastError();
		const LONG k = InterlockedIncrement(&g_keeper.failed);
		if (g_keeper.log && k <= 3)
			g_keeper.log(Fmt("title: WM_SETTEXT to the game window %p failed or timed out (error %u, failure %ld; the keeper "
			                 "tries again in %u ms%s)",
			                 h, (unsigned)err, k, g_keeper.everyMs, k == 3 ? "; later failures are only counted" : "")
			                 .c_str());
	}
}

DWORD WINAPI KeeperThread(LPVOID)
{
	// Never returns: StopTitleKeeper ends it while it is outside its tick (busy == 0), as the shell's watcher threads.
	for (;;)
	{
		InterlockedExchange(&g_keeper.busy, 1);
		if (g_keeper.stop == 0) KeeperTick();
		InterlockedExchange(&g_keeper.busy, 0);
		Sleep(g_keeper.everyMs);
	}
}
}  // namespace

HWND FindGameWindow()
{
	if (!g_u32.Load()) return NULL;
	return FindGameWith(g_u32);
}

std::string StartTitleKeeper(const std::string& prefixUtf8, unsigned everyMs, TitleLogFn log)
{
	if (g_keeper.thread) return "title: the keeper already runs (prefix \"" + g_keeper.prefixUtf8 + "\")";
	if (prefixUtf8.empty())
		return "title: [Shell] WindowTitlePrefix is empty: the game window keeps the EXE's own title (no keeper)";
	const std::wstring w = Widen(prefixUtf8);
	if (w.empty()) return "title: [Shell] WindowTitlePrefix is not valid UTF-8: no prefix (no keeper)";
	if (!g_u32.Load()) return "title: a user32 function is missing: no prefix (no keeper)";
	g_keeper.stop = 0;
	g_keeper.busy = 0;
	g_keeper.ticks = g_keeper.noWindow = g_keeper.applied = g_keeper.failed = 0;
	g_keeper.prefix = w;
	g_keeper.prefixUtf8 = prefixUtf8;
	g_keeper.everyMs = everyMs ? everyMs : 1000;
	g_keeper.log = log;
	g_keeper.startMs = NowMs();
	DWORD tid = 0;
	g_keeper.thread = CreateThread(NULL, 64 * 1024, KeeperThread, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, &tid);
	if (!g_keeper.thread)
		return Fmt("title: the keeper thread could not be created (error %u): no prefix", (unsigned)GetLastError());
	return Fmt("title: keeper thread %u puts \"%s\" in front of the game window's title (class %s), checked every %u ms "
	           "(ticket 0680)",
	           (unsigned)tid, prefixUtf8.c_str(), VP64PACK_GAME_CLASS, g_keeper.everyMs);
}

void StopTitleKeeper()
{
	if (!g_keeper.thread) return;
	InterlockedExchange(&g_keeper.stop, 1);
	for (int i = 0; i < 1500 && g_keeper.busy != 0; ++i) Sleep(1);  // a tick: microseconds, or the send's 250 ms at most
	TerminateThread(g_keeper.thread, 0);
	WaitForSingleObject(g_keeper.thread, 1000);  // a terminated thread is signalled at once
	CloseHandle(g_keeper.thread);
	g_keeper.thread = NULL;
}

std::string TitleKeeperStatus()
{
	if (g_keeper.prefixUtf8.empty()) return "title: no keeper ran (no prefix)";
	return Fmt("title: keeper %s, prefix \"%s\": %ld ticks (%ld without a game window), %ld title changes (the first and "
	           "%ld re-applications), %ld failed sends",
	           g_keeper.thread ? "running" : "stopped", g_keeper.prefixUtf8.c_str(), g_keeper.ticks, g_keeper.noWindow,
	           g_keeper.applied, g_keeper.applied > 0 ? g_keeper.applied - 1 : 0, g_keeper.failed);
}
}  // namespace ProxyPack
