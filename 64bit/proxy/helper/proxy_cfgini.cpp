// The config.ini routine of decisions D119 and D125 (issue 3520), linked into vp64helper.exe. See proxy_cfgini.h for the contract; the rules in short:
//   On     the player's values of our keys go to the side file (temp file + rename), then ours (1) into config.ini.
//   Off    the player's values come back, only for our keys and only where a key still holds ours; then the side file
//          goes. Without a usable side file config.ini is left as it is and the case is reported (Result.note; keys
//          at ours named "left:<key>"): a 1 there may be the player's own, and only the side file tells it from ours
//          (issue 0378, 2026-11-15; until then the stock values were written, "stock:<key>", which reset a player's own
//          1 to 0). An unusable side file is moved aside to vp64-config-side.ini.unusable.
//   Status reads only.
// Only the value bytes of our keys' lines change (or a line of ours is appended to / removed from [DisableSystems]
// when the key was absent); every other byte of config.ini stays as it was: other sections, order, comments, line
// ends, a UTF-8 byte-order mark. config.ini itself is replaced through a temp file + MoveFileExW as well.
// Knowledge base: research/06-tooling-and-workflows/release-driver.md W3c.
#include "proxy_cfgini.h"
#include "proxy_session_keys.h"  // proxy/src: the build and the tests pass /I proxy/src

#include <windows.h>

#include <stdio.h>
#include <string.h>

namespace cfgini
{
namespace
{
// shell/tools/darkred.py D33_OFF minus River and Arrows (D119, the A'' form), in darkred.py's order: the one list in
// proxy/src/proxy_session_keys.h, shared with the proxy's session rule (issue 3523). proxy/tools/test_cfgini.py
// compares the compiled list with darkred.py.
//
// The former keys (issue 3805, MC #1093, 2026-11-14): keys that were ours in an earlier build and left the list since
// (Waves: epic 3600; Minimap: issue 0192; MovementPath: issue 3803; Particle, Combat and Projectile: issue 0368, the owner's
// decision D147, 2026-11-15). A side file written by such a build has a line for
// them, and that build's On left them at 1. They follow our keys in kAll: ReadSide accepts their lines (optional), Off
// and On put each one back to the side file's value where config.ini still holds ours (reported "former:<key>"), and
// Status calls such a file partial. On never writes ours into a former key.
#define VP64_FORMER_KEYS "Waves", "Minimap", "MovementPath", "Particle", "Combat", "Projectile"
const char* const kAll[] = {VP64_D33_KEYS_A2, VP64_FORMER_KEYS};
const char* const kFormer[] = {VP64_FORMER_KEYS};
const size_t kNumAll = sizeof(kAll) / sizeof(kAll[0]);
const size_t kNumFormer = sizeof(kFormer) / sizeof(kFormer[0]);
const size_t kNumKeys = kNumAll - kNumFormer;  // our keys: kAll[0 .. kNumKeys)
const char* const* const kKeys = kAll;
// The keys the EXE's stock config.ini already has at 1 (darkred.py STOCK_OFF): a 1 there says nothing about the patch,
// so Off without a side file does not name them "left:".
bool StockOff(const char* k) { return !strcmp(k, "OverlayDebug") || !strcmp(k, "ColorKeyEdit"); }
const char* const kOurValue = "1";
const char* const kAbsent = "<absent>";
const char* const kSection = "DisableSystems";
const char* const kSideHeader = "; vp64-config-side.ini: the player's own values of the config.ini keys the Vox Populi 64-bit";
const char* const kSideMagic = "[PlayerValues]";

std::string Narrow(const std::wstring& w)
{
	if (w.empty()) return std::string();
	int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
	std::string s(n > 0 ? n : 0, '\0');
	if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
	return s;
}

std::string WinErr(const char* what, DWORD e)
{
	char b[160];
	_snprintf_s(b, sizeof(b), _TRUNCATE, "%s (Windows error %lu)", what, (unsigned long)e);
	return b;
}

std::string Num(size_t n)  // VC9's library has no std::to_string
{
	char b[32];
	_snprintf_s(b, sizeof(b), _TRUNCATE, "%lu", (unsigned long)n);
	return b;
}

// Reads a whole file. 0 = ok, 1 = does not exist, 2 = could not be read (err set).
int ReadAll(const std::wstring& path, std::string* out, std::string* err)
{
	out->clear();
	HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
	{
		DWORD e = GetLastError();
		if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return 1;
		*err = WinErr("cannot open for reading", e);
		return 2;
	}
	LARGE_INTEGER size;
	if (!GetFileSizeEx(h, &size) || size.QuadPart > 64 * 1024 * 1024)
	{
		CloseHandle(h);
		*err = "cannot read the size, or larger than 64 MB";
		return 2;
	}
	out->resize((size_t)size.QuadPart);
	DWORD got = 0;
	BOOL ok = size.QuadPart == 0 || ReadFile(h, &(*out)[0], (DWORD)size.QuadPart, &got, NULL);
	DWORD e = GetLastError();
	CloseHandle(h);
	if (!ok || got != (DWORD)size.QuadPart)
	{
		*err = WinErr("read failed", e);
		return 2;
	}
	return 0;
}

// Writes `data` to `path` through `path`.vp64tmp and MoveFileExW (replace, write through). A read-only target is
// refused before anything is written.
bool WriteAtomic(const std::wstring& path, const std::string& data, std::string* err)
{
	DWORD attr = GetFileAttributesW(path.c_str());
	if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY))
	{
		*err = "the file is read-only: " + Narrow(path);
		return false;
	}
	std::wstring tmp = path + L".vp64tmp";
	HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
	{
		*err = WinErr("cannot create the temporary file", GetLastError()) + ": " + Narrow(tmp);
		return false;
	}
	DWORD put = 0;
	BOOL ok = data.empty() || WriteFile(h, data.data(), (DWORD)data.size(), &put, NULL);
	DWORD e = GetLastError();
	ok = ok && FlushFileBuffers(h);
	CloseHandle(h);
	if (!ok || put != (DWORD)data.size())
	{
		DeleteFileW(tmp.c_str());
		*err = WinErr("write failed", e) + ": " + Narrow(tmp);
		return false;
	}
	if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		e = GetLastError();
		DeleteFileW(tmp.c_str());
		*err = WinErr("cannot replace the file", e) + ": " + Narrow(path);
		return false;
	}
	return true;
}

bool IEq(const char* a, size_t n, const char* b)
{
	return strlen(b) == n && _strnicmp(a, b, n) == 0;
}

bool IsSpace(char c) { return c == ' ' || c == '\t'; }

// One line of config.ini: [begin, end) without its line end; [end, next) is the line end (may be empty at EOF).
struct Line
{
	size_t begin, end, next;
};

std::vector<Line> SplitLines(const std::string& t)
{
	std::vector<Line> v;
	size_t p = 0;
	while (p < t.size())
	{
		size_t e = t.find('\n', p);
		Line l;
		l.begin = p;
		if (e == std::string::npos)
		{
			l.end = l.next = t.size();
		}
		else
		{
			l.end = (e > p && t[e - 1] == '\r') ? e - 1 : e;
			l.next = e + 1;
		}
		v.push_back(l);
		p = l.next;
	}
	return v;
}

// A key's place in [DisableSystems]: the line index, and the value's bytes [vb, ve) (trimmed; may be empty).
struct KeyPos
{
	bool found;
	size_t line, vb, ve;
	std::string value;
};

struct Parsed
{
	std::string text;
	std::vector<Line> lines;
	bool section;
	size_t secHeader;   // line index of [DisableSystems]
	size_t secEnd;      // first line index after the section (lines.size() at EOF)
	std::string eol;    // the file's first line end ("\r\n" or "\n"; "\r\n" when there is none)
	KeyPos keys[kNumAll];
	std::vector<std::string> dupes;  // our keys found more than once in the section
};

// Section header of a line: true and the name when the trimmed line is [name].
bool SectionName(const std::string& t, const Line& l, size_t skip, std::string* name)
{
	size_t b = l.begin + skip, e = l.end;
	while (b < e && IsSpace(t[b])) ++b;
	while (e > b && IsSpace(t[e - 1])) --e;
	if (e - b < 2 || t[b] != '[' || t[e - 1] != ']') return false;
	*name = t.substr(b + 1, e - b - 2);
	return true;
}

// 0 ok; 1 no file; 2 unreadable (err); 3 not a text file we can edit (err); 4 no [DisableSystems] section.
int Parse(const std::wstring& path, Parsed* p, std::string* err)
{
	int r = ReadAll(path, &p->text, err);
	if (r) return r;
	const std::string& t = p->text;
	if (t.size() >= 2 && ((unsigned char)t[0] == 0xFF && (unsigned char)t[1] == 0xFE ||
	                      (unsigned char)t[0] == 0xFE && (unsigned char)t[1] == 0xFF))
	{
		*err = "config.ini is UTF-16; this routine edits only single-byte text";
		return 3;
	}
	if (t.find('\0') != std::string::npos)
	{
		*err = "config.ini holds NUL bytes; not a text file";
		return 3;
	}
	size_t bom = (t.size() >= 3 && (unsigned char)t[0] == 0xEF && (unsigned char)t[1] == 0xBB &&
	              (unsigned char)t[2] == 0xBF) ? 3 : 0;
	p->lines = SplitLines(t);
	p->eol = "\r\n";
	size_t nl = t.find('\n');
	if (nl != std::string::npos) p->eol = (nl > 0 && t[nl - 1] == '\r') ? "\r\n" : "\n";
	p->section = false;
	p->secHeader = p->secEnd = 0;
	for (size_t i = 0; i < kNumAll; ++i)
	{
		p->keys[i].found = false;
		p->keys[i].line = p->keys[i].vb = p->keys[i].ve = 0;
		p->keys[i].value.clear();
	}
	for (size_t i = 0; i < p->lines.size(); ++i)
	{
		std::string name;
		if (!SectionName(t, p->lines[i], i == 0 ? bom : 0, &name)) continue;
		if (!p->section && IEq(name.data(), name.size(), kSection))
		{
			p->section = true;
			p->secHeader = i;
			p->secEnd = p->lines.size();
		}
		else if (p->section && p->secEnd == p->lines.size() && i > p->secHeader)
		{
			p->secEnd = i;
		}
	}
	if (!p->section) return 4;
	for (size_t i = p->secHeader + 1; i < p->secEnd; ++i)
	{
		const Line& l = p->lines[i];
		size_t b = l.begin;
		while (b < l.end && IsSpace(t[b])) ++b;
		if (b == l.end || t[b] == ';' || t[b] == '#') continue;
		size_t eq = t.find('=', b);
		if (eq == std::string::npos || eq >= l.end) continue;
		size_t ke = eq;
		while (ke > b && IsSpace(t[ke - 1])) --ke;
		size_t vb = eq + 1, ve = l.end;
		while (vb < ve && IsSpace(t[vb])) ++vb;
		while (ve > vb && IsSpace(t[ve - 1])) --ve;
		for (size_t k = 0; k < kNumAll; ++k)
		{
			if (!IEq(t.data() + b, ke - b, kAll[k])) continue;
			if (p->keys[k].found)
			{
				if (k < kNumKeys) p->dupes.push_back(kKeys[k]);  // a former key twice: the first line counts
				break;
			}
			p->keys[k].found = true;
			p->keys[k].line = i;
			p->keys[k].vb = vb;
			p->keys[k].ve = ve;
			p->keys[k].value = t.substr(vb, ve - vb);
			break;
		}
	}
	return 0;
}

std::string ParseError(int r, const std::wstring& path, const std::string& err)
{
	switch (r)
	{
	case 1: return "no config.ini at " + Narrow(path) + " (start the game once so that it writes one)";
	case 4: return "config.ini has no [DisableSystems] section: " + Narrow(path) +
		           " (start the game once so that it writes one)";
	default: return "config.ini: " + err + ": " + Narrow(path);
	}
}

// The side file. 0 ok; 1 none; 2 unreadable or not ours or incomplete (err).
int ReadSide(const std::wstring& path, std::string vals[], std::string* err)
{
	std::string t;
	int r = ReadAll(path, &t, err);
	if (r) return r;
	bool magic = false;
	bool have[kNumAll] = {};
	std::vector<Line> lines = SplitLines(t);
	for (size_t i = 0; i < lines.size(); ++i)
	{
		std::string l = t.substr(lines[i].begin, lines[i].end - lines[i].begin);
		if (l.empty() || l[0] == ';') continue;
		if (l == kSideMagic)
		{
			magic = true;
			continue;
		}
		size_t eq = l.find('=');
		if (!magic || eq == std::string::npos)
		{
			*err = "side file line " + Num(i + 1) + " not understood";
			return 2;
		}
		std::string k = l.substr(0, eq), v = l.substr(eq + 1);
		size_t idx = kNumAll;  // our keys and the former keys (issue 3805); any other key refuses the whole file
		for (size_t j = 0; j < kNumAll; ++j)
			if (k == kAll[j]) idx = j;
		if (idx == kNumAll || have[idx] || v.empty() || v.find_first_of("\r\n") != std::string::npos)
		{
			*err = "side file line " + Num(i + 1) + ": unknown, repeated or empty key " + k;
			return 2;
		}
		have[idx] = true;
		vals[idx] = v;
	}
	for (size_t j = 0; j < kNumKeys; ++j)
	{
		if (!have[j])
		{
			*err = std::string("side file without key ") + kKeys[j];
			return 2;
		}
	}
	return 0;
}

std::string SideText(const std::string vals[])
{
	std::string s = std::string(kSideHeader) + "\r\n; patch turned off in it (decision D119). Written by the patch; read by \"vp64helper off\".\r\n"
	                "; Do not edit. <absent> = the key was not in config.ini.\r\n" + kSideMagic + "\r\n";
	for (size_t j = 0; j < kNumKeys; ++j) s += std::string(kKeys[j]) + "=" + vals[j] + "\r\n";
	// a former key's record (issue 3805) is kept while the side file lives (only an older build wrote one)
	for (size_t j = kNumKeys; j < kNumAll; ++j)
		if (!vals[j].empty()) s += std::string(kAll[j]) + "=" + vals[j] + "\r\n";
	return s;
}

bool IsOurs(const KeyPos& k) { return k.found && k.value == kOurValue; }

// Issue 3805: a former key whose old side file recorded a value other than ours while config.ini still holds ours (the
// state an older build's On left).
bool FormerPending(const Parsed& p, const std::string vals[], size_t j)
{
	return j >= kNumKeys && j < kNumAll && !vals[j].empty() && vals[j] != kOurValue && IsOurs(p.keys[j]);
}

// Puts every pending former key back to its side-file value (its line removed where it was absent); returns how many.
size_t RestoreFormer(const Parsed& p, const std::string vals[], const char* setv[], bool rem[],
                     std::vector<std::string>* keys)
{
	size_t n = 0;
	for (size_t j = kNumKeys; j < kNumAll; ++j)
	{
		if (!FormerPending(p, vals, j)) continue;
		if (vals[j] == kAbsent) rem[j] = true;
		else setv[j] = vals[j].c_str();
		keys->push_back(std::string("former:") + kAll[j]);
		++n;
	}
	return n;
}

// Applies edits to the parsed text: for each key, set its value (setv[k] non-null), remove its line (rem[k]), or
// append "Key = v" at the end of the section (add[k] non-null). Returns the new text.
std::string Apply(const Parsed& p, const char* const setv[], const bool rem[], const char* const add[])
{
	const std::string& t = p.text;
	std::string out;
	out.reserve(t.size() + 256);
	// lines before the section's end, with edits
	size_t lastNonBlank = p.secHeader;  // where appended lines go: after the section's last non-blank line
	for (size_t i = p.secHeader + 1; i < p.secEnd; ++i)
	{
		const Line& l = p.lines[i];
		size_t b = l.begin;
		while (b < l.end && IsSpace(t[b])) ++b;
		if (b < l.end) lastNonBlank = i;
	}
	std::string appended;
	for (size_t k = 0; k < kNumAll; ++k)
		if (add[k]) appended += std::string(kAll[k]) + " = " + add[k] + p.eol;
	size_t pos = 0;
	for (size_t i = 0; i < p.lines.size(); ++i)
	{
		const Line& l = p.lines[i];
		size_t kk = kNumAll;
		for (size_t k = 0; k < kNumAll; ++k)
			if (p.keys[k].found && p.keys[k].line == i) kk = k;
		if (kk < kNumAll && rem[kk])
		{
			// the line and its line end go
		}
		else if (kk < kNumAll && setv[kk])
		{
			out.append(t, l.begin, p.keys[kk].vb - l.begin);
			out += setv[kk];
			out.append(t, p.keys[kk].ve, l.next - p.keys[kk].ve);
		}
		else
		{
			out.append(t, l.begin, l.next - l.begin);
		}
		if (i == lastNonBlank && !appended.empty())
		{
			if (l.next == l.end) out += p.eol;  // the last line of the file had no line end
			out += appended;
		}
		pos = l.next;
	}
	(void)pos;
	return out;
}
}  // namespace

const std::vector<const char*>& OurKeys()
{
	static const std::vector<const char*> v(kKeys, kKeys + kNumKeys);
	return v;
}

const std::vector<const char*>& FormerKeys()
{
	static const std::vector<const char*> v(kFormer, kFormer + kNumFormer);
	return v;
}

std::wstring SidePath(const std::wstring& configIni)
{
	size_t s = configIni.find_last_of(L"\\/");
	return (s == std::wstring::npos ? std::wstring() : configIni.substr(0, s + 1)) + L"vp64-config-side.ini";
}

bool ConfigIniPath(std::wstring* out)
{
	out->clear();
	wchar_t env[MAX_PATH * 2];
	DWORD n = GetEnvironmentVariableW(L"VPPROXY_TEST_CONFIG_INI", env, MAX_PATH * 2);
	if (n > 0 && n < MAX_PATH * 2)
	{
		*out = env;
		return true;
	}
	n = GetEnvironmentVariableW(L"C5_DOCUMENTS", env, MAX_PATH * 2);
	if (n > 0 && n < MAX_PATH * 2)
	{
		*out = std::wstring(env) + L"\\My Games\\Sid Meier's Civilization 5\\config.ini";
		return true;
	}
	// SHGetFolderPathW(CSIDL_PERSONAL) through GetProcAddress, as proxy_rt_logic.cpp does (no shell32 import).
	typedef HRESULT(WINAPI * PFN)(HWND, int, HANDLE, DWORD, LPWSTR);
	HMODULE sh = LoadLibraryW(L"shell32.dll");
	PFN f = sh ? (PFN)GetProcAddress(sh, "SHGetFolderPathW") : NULL;
	wchar_t w[MAX_PATH];
	if (!f || f(NULL, 0x0005 /* CSIDL_PERSONAL */, NULL, 0, w) != S_OK) return false;
	*out = std::wstring(w) + L"\\My Games\\Sid Meier's Civilization 5\\config.ini";
	return true;
}

Result On(const std::wstring& configIni)
{
	Result res = {false, std::string(), std::vector<std::string>()};
	Parsed p;
	std::string err;
	int r = Parse(configIni, &p, &err);
	if (r)
	{
		res.error = ParseError(r, configIni, err);
		return res;
	}
	if (!p.dupes.empty())
	{
		res.error = "config.ini has key " + p.dupes[0] + " more than once in [DisableSystems]; nothing changed";
		return res;
	}
	const std::wstring side = SidePath(configIni);
	std::string vals[kNumAll];
	int sr = ReadSide(side, vals, &err);
	if (sr == 2)
	{
		res.error = "the side file " + Narrow(side) + " cannot be used (" + err +
		            "); nothing changed. Run \"vp64helper off\" first (it leaves config.ini as it is and moves the side "
		            "file aside) or delete it";
		return res;
	}
	bool sideExisted = sr == 0;
	// The player's value of each key: today's value where it is not ours (the player's newest), else the side file's
	// (a key that already holds ours with no side file is the player's own 1).
	bool sideChanged = !sideExisted;
	for (size_t k = 0; k < kNumKeys; ++k)
	{
		std::string v = p.keys[k].found ? p.keys[k].value : std::string(kAbsent);
		if (sideExisted && IsOurs(p.keys[k])) continue;
		if (!sideExisted || vals[k] != v)
		{
			vals[k] = v;
			sideChanged = true;
		}
	}
	const char* setv[kNumAll] = {};
	const char* add[kNumAll] = {};
	bool rem[kNumAll] = {};
	bool change = false;
	for (size_t k = 0; k < kNumKeys; ++k)
	{
		if (IsOurs(p.keys[k])) continue;
		if (p.keys[k].found) setv[k] = kOurValue;
		else add[k] = kOurValue;
		res.keys.push_back(kKeys[k]);
		change = true;
	}
	// issue 3805: an older build's side file names former keys that build switched off; on puts them back (never off)
	if (sideExisted && RestoreFormer(p, vals, setv, rem, &res.keys)) change = true;
	if (change)
	{
		DWORD attr = GetFileAttributesW(configIni.c_str());
		if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY))
		{
			res.keys.clear();
			res.error = "config.ini is read-only: " + Narrow(configIni) + "; nothing changed";
			return res;
		}
	}
	if (sideChanged && !WriteAtomic(side, SideText(vals), &err))
	{
		res.keys.clear();
		res.error = "cannot write the side file: " + err + "; nothing changed";
		return res;
	}
	if (change && !WriteAtomic(configIni, Apply(p, setv, rem, add), &err))
	{
		if (!sideExisted) DeleteFileW(side.c_str());
		res.keys.clear();
		res.error = "cannot write config.ini: " + err + (sideExisted ? "" : "; the side file was removed again");
		return res;
	}
	res.ok = true;
	return res;
}

Result Off(const std::wstring& configIni)
{
	Result res = {false, std::string(), std::vector<std::string>()};
	Parsed p;
	std::string err;
	const std::wstring side = SidePath(configIni);
	int r = Parse(configIni, &p, &err);
	if (r)
	{
		res.error = ParseError(r, configIni, err);
		return res;
	}
	if (!p.dupes.empty())
	{
		res.error = "config.ini has key " + p.dupes[0] + " more than once in [DisableSystems]; nothing changed";
		return res;
	}
	std::string vals[kNumAll];
	int sr = ReadSide(side, vals, &err);
	if (sr != 0)
	{
		// Issue 0378: no usable record of the player's values. A key at 1 may be the player's own (the thin client never
		// ran On; a player sets FluidFOW = 1 by hand), so nothing is written; the keys at ours are named and the case
		// goes into note for the helper's output line and helper.log.
		std::string left;
		for (size_t k = 0; k < kNumKeys; ++k)
		{
			if (!IsOurs(p.keys[k]) || StockOff(kKeys[k])) continue;
			res.keys.push_back(std::string("left:") + kKeys[k]);
			left += std::string(left.empty() ? "" : ",") + kKeys[k];
		}
		if (sr == 1)
			res.note = "no side file " + Narrow(side) + ": config.ini left as it is, no key written";
		else
		{
			res.note = "side file " + Narrow(side) + " unusable (" + err + "): config.ini left as it is, no key written";
			const std::wstring aside = side + L".unusable";
			if (MoveFileExW(side.c_str(), aside.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
				res.note += "; the side file moved to " + Narrow(aside);
			else
				res.note += "; " + WinErr("the side file could not be moved aside", GetLastError());
		}
		if (!left.empty())
			res.note += "; keys at 1 left as they are: " + left +
			            " (the player's own values, or the patch's if its side file was lost)";
		res.ok = true;
		return res;
	}
	const char* setv[kNumAll] = {};
	const char* add[kNumAll] = {};
	bool rem[kNumAll] = {};
	bool change = false;
	for (size_t k = 0; k < kNumKeys; ++k)
	{
		if (!IsOurs(p.keys[k]))
		{
			if (vals[k] != (p.keys[k].found ? p.keys[k].value : std::string(kAbsent)))
				res.keys.push_back(std::string("kept:") + kKeys[k]);
			continue;
		}
		if (vals[k] == kOurValue) continue;
		if (vals[k] == kAbsent) rem[k] = true;
		else setv[k] = vals[k].c_str();
		res.keys.push_back(kKeys[k]);
		change = true;
	}
	// issue 3805: former keys an older build switched off go back to the side file's values
	if (RestoreFormer(p, vals, setv, rem, &res.keys)) change = true;
	if (change && !WriteAtomic(configIni, Apply(p, setv, rem, add), &err))
	{
		res.keys.clear();
		res.error = "cannot write config.ini: " + err + "; the side file is kept";
		return res;
	}
	if (!DeleteFileW(side.c_str()))
	{
		DWORD e = GetLastError();
		if (e != ERROR_FILE_NOT_FOUND)
		{
			res.error = WinErr("config.ini restored, but the side file cannot be removed", e) + ": " + Narrow(side);
			return res;
		}
	}
	res.ok = true;
	return res;
}

State Status(const std::wstring& configIni, Result* detail)
{
	Result tmp = {true, std::string(), std::vector<std::string>()};
	Result& d = detail ? *detail : tmp;
	d.ok = true;
	d.error.clear();
	d.keys.clear();
	Parsed p;
	std::string err;
	int r = Parse(configIni, &p, &err);
	if (r)
	{
		d.ok = false;
		d.error = ParseError(r, configIni, err);
		return State::NoFile;
	}
	std::string vals[kNumAll];
	int sr = ReadSide(SidePath(configIni), vals, &err);
	if (sr == 2)
	{
		d.error = "side file unusable: " + err;
		for (size_t k = 0; k < kNumKeys; ++k)
			if (!IsOurs(p.keys[k])) d.keys.push_back(kKeys[k]);
		return State::Partial;
	}
	if (!p.dupes.empty()) d.error = "key " + p.dupes[0] + " more than once in [DisableSystems]";
	size_t ours = 0;
	for (size_t k = 0; k < kNumKeys; ++k)
		if (IsOurs(p.keys[k])) ++ours;
	if (sr == 1)
	{
		// No side file: the patch's switch is off. All of our keys at 1 without our record is D33 put there by hand or
		// by a test tool (darkred.py): partial, listing them.
		if (ours == kNumKeys)
		{
			for (size_t k = 0; k < kNumKeys; ++k) d.keys.push_back(kKeys[k]);
			return State::Partial;
		}
		return State::Off;
	}
	for (size_t k = 0; k < kNumKeys; ++k)
		if (!IsOurs(p.keys[k])) d.keys.push_back(kKeys[k]);
	bool former = false;  // issue 3805: a former key an older build switched off is still off: on or off has work to do
	for (size_t j = kNumKeys; j < kNumAll; ++j)
		if (FormerPending(p, vals, j))
		{
			d.keys.push_back(std::string("former:") + kAll[j]);
			former = true;
		}
	return (ours == kNumKeys && p.dupes.empty() && !former) ? State::On : State::Partial;
}

// ---- VP64Options.ini's [Renderer] ServerRenderer (issue 0030; the MC's one-switch ruling, 2026-11-16) ---------------
// The contract is in proxy_cfgini.h. The reading rules mirror options/src/vp64_options.cpp Parse (LF, CRLF or CR line
// ends; a UTF-8 byte-order mark skipped; ; and # comments; keys without regard to case; an inline comment after a space;
// one pair of quotes; the last line of a key decides; an empty, invalid or out-of-range value gives the default 0);
// proxy/tools/rt_tests/cfgini_unit.cpp compares RendererValue with that parser on a set of texts.
namespace
{
const char* const kOptKey = "ServerRenderer";
const char* const kOptLine = "ServerRenderer = 1";  // the line On writes
const char* const kOptSection = "Renderer";

// The key the scanner looks for (ticket 4931: the scanner takes the key and section as parameters). value is what the
// options parser takes: the deciding line's whole number when it lies in [minV, maxV], else defV; no line: defV.
struct OptSpec
{
	const char* key;
	const char* section;
	int minV, maxV, defV;
};
const OptSpec kRendererSpec = {kOptKey, kOptSection, 0, 1, 0};  // [Renderer] ServerRenderer: 0-1, default 0
const size_t kOptMaxBytes = 64 * 1024;  // Vp64Opt::kMaxFileBytes: a larger file is not read by the proxy
const char* const kOptSideMagic = "[OptionsBefore]";
const char* const kOptNewFile =
	"; VP64Options.ini - options of the Vox Populi 64-bit patch (written by \"vp64helper on\"; every option is\r\n"
	"; described in OPTIONS.md, shipped with the patch).\r\n";

std::vector<Line> SplitAnyLines(const std::string& t)  // LF, CRLF or CR, as Vp64Opt::Parse
{
	std::vector<Line> v;
	size_t p = 0;
	while (p < t.size())
	{
		size_t e = p;
		while (e < t.size() && t[e] != '\n' && t[e] != '\r') ++e;
		Line l;
		l.begin = p;
		l.end = e;
		l.next = e < t.size() ? e + ((t[e] == '\r' && e + 1 < t.size() && t[e + 1] == '\n') ? 2 : 1) : e;
		v.push_back(l);
		p = l.next;
	}
	return v;
}

struct OptScan
{
	std::vector<Line> lines;
	size_t bom;            // 3 when the text starts with a UTF-8 byte-order mark
	std::string eol;       // the first line end ("\r\n" when there is none)
	int value;             // what the parser takes (the spec's range and default)
	bool deciding;         // a "<key> = ..." line exists; decLine is the last one
	size_t decLine;
	bool commented;        // a commented "; <key> = ..." line exists; comLine is the last one
	size_t comLine;
	bool section;          // a [<section>] header exists; secLine is the first one
	size_t secLine;
	size_t Begin(size_t i) const { return lines[i].begin + (i == 0 ? bom : 0); }
};

bool OptParseInt(const char* v, size_t n, int* out)  // Vp64Opt's ParseInt: optional sign, 1 to 9 digits
{
	size_t i = 0;
	bool neg = false;
	if (i < n && (v[i] == '+' || v[i] == '-')) neg = v[i++] == '-';
	if (i == n || n - i > 9) return false;
	int x = 0;
	for (; i < n; ++i)
	{
		if (v[i] < '0' || v[i] > '9') return false;
		x = x * 10 + (v[i] - '0');
	}
	*out = neg ? -x : x;
	return true;
}

void OptScanText(const std::string& t, const OptSpec& spec, OptScan* s)
{
	const char* const kOptKey = spec.key;          // (shadows the file's ServerRenderer constants: the scanner reads
	const char* const kOptSection = spec.section;  // only the spec's key and section)
	s->bom = (t.size() >= 3 && (unsigned char)t[0] == 0xEF && (unsigned char)t[1] == 0xBB && (unsigned char)t[2] == 0xBF)
	             ? 3 : 0;
	s->lines = SplitAnyLines(t);
	s->eol = "\r\n";
	size_t nl = t.find_first_of("\r\n");
	if (nl != std::string::npos)
		s->eol = t[nl] == '\n' ? "\n" : (nl + 1 < t.size() && t[nl + 1] == '\n') ? "\r\n" : "\r";
	s->value = spec.defV;
	s->deciding = s->commented = s->section = false;
	s->decLine = s->comLine = s->secLine = 0;
	for (size_t i = 0; i < s->lines.size(); ++i)
	{
		size_t b = s->Begin(i), e = s->lines[i].end;
		if (b > e) b = e;
		while (b < e && IsSpace(t[b])) ++b;
		while (e > b && IsSpace(t[e - 1])) --e;
		if (b == e) continue;
		if (t[b] == ';' || t[b] == '#')
		{
			size_t k = b + 1;
			while (k < e && IsSpace(t[k])) ++k;
			size_t n = strlen(kOptKey);
			if (e - k >= n && _strnicmp(t.data() + k, kOptKey, n) == 0)
			{
				size_t q = k + n;
				while (q < e && IsSpace(t[q])) ++q;
				// Ticket 4931: only the commented default line the proxy writes, "; Key = <whole number>" and nothing
				// after it. Before, any comment line that began "Key =" counted, and the last one won: the default file's
				// help of ServerPath has a line "; ServerRenderer = 1)." after "; ServerRenderer = 0", so on replaced
				// that help line (the result still parsed as 1, and off gave the bytes back).
				size_t v = q + 1;
				while (v < e && IsSpace(t[v])) ++v;
				size_t ve = v;
				if (ve < e && (t[ve] == '+' || t[ve] == '-')) ++ve;
				const size_t digits = ve;
				while (ve < e && t[ve] >= '0' && t[ve] <= '9') ++ve;
				if (q < e && t[q] == '=' && ve > digits && ve == e)
				{
					s->commented = true;
					s->comLine = i;
				}
			}
			continue;
		}
		if (t[b] == '[')
		{
			if (t[e - 1] != ']' || e - b < 2) continue;
			size_t nb = b + 1, ne = e - 1;
			while (nb < ne && IsSpace(t[nb])) ++nb;
			while (ne > nb && IsSpace(t[ne - 1])) --ne;
			if (!s->section && IEq(t.data() + nb, ne - nb, kOptSection))
			{
				s->section = true;
				s->secLine = i;
			}
			continue;
		}
		size_t eq = t.find('=', b);
		if (eq == std::string::npos || eq >= e) continue;
		size_t ke = eq;
		while (ke > b && IsSpace(t[ke - 1])) --ke;
		if (!IEq(t.data() + b, ke - b, kOptKey)) continue;
		size_t vb = eq + 1, ve = e;
		while (vb < ve && IsSpace(t[vb])) ++vb;
		for (size_t c = vb; c < ve; ++c)
			if ((t[c] == ';' || t[c] == '#') && (c == vb || IsSpace(t[c - 1])))
			{
				ve = c;
				while (ve > vb && IsSpace(t[ve - 1])) --ve;
				break;
			}
		if (ve - vb >= 2 && t[vb] == '"' && t[ve - 1] == '"')
		{
			++vb;
			--ve;
			while (vb < ve && IsSpace(t[vb])) ++vb;
			while (ve > vb && IsSpace(t[ve - 1])) --ve;
		}
		int v = 0;
		s->value = (vb < ve && OptParseInt(t.data() + vb, ve - vb, &v) && v >= spec.minV && v <= spec.maxV) ? v : spec.defV;
		s->deciding = true;
		s->decLine = i;
	}
}

std::string Hex(const std::string& s)
{
	static const char d[] = "0123456789abcdef";
	std::string h;
	h.reserve(s.size() * 2);
	for (size_t i = 0; i < s.size(); ++i)
	{
		h += d[(unsigned char)s[i] >> 4];
		h += d[(unsigned char)s[i] & 15];
	}
	return h;
}

bool Unhex(const std::string& h, std::string* out)
{
	out->clear();
	if (h.size() % 2) return false;
	for (size_t i = 0; i < h.size(); i += 2)
	{
		int v = 0;
		for (int j = 0; j < 2; ++j)
		{
			char c = h[i + j];
			int x = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
			if (x < 0) return false;
			v = v * 16 + x;
		}
		*out += (char)v;
	}
	return true;
}

struct OptSide
{
	bool existed;          // the options file existed before On
	std::string before;    // its bytes then (empty when it did not exist)
	std::string after;     // the bytes On left
	std::string replaced;  // the line On replaced (without its line end); empty when On added the line
};

std::string OptSideText(const OptSide& s)
{
	return "; vp64-options-side.ini: VP64Options.ini as it was before \"vp64helper on\" set [Renderer] ServerRenderer = 1\r\n"
	       "; (the Vox Populi 64-bit patch's one switch, issue 0030). Written by the patch; read by \"vp64helper off\".\r\n"
	       "; Do not edit. The files are written as hexadecimal bytes.\r\n" +
	       std::string(kOptSideMagic) + "\r\nexisted=" + (s.existed ? "1" : "0") + "\r\nbefore=" + Hex(s.before) +
	       "\r\nafter=" + Hex(s.after) + "\r\nreplaced=" + Hex(s.replaced) + "\r\n";
}

// 0 ok; 1 none; 2 unusable (err).
int ReadOptSide(const std::wstring& path, OptSide* s, std::string* err)
{
	std::string t;
	int r = ReadAll(path, &t, err);
	if (r) return r;
	bool magic = false, have[4] = {};
	const char* const names[4] = {"existed", "before", "after", "replaced"};
	std::vector<Line> lines = SplitLines(t);
	for (size_t i = 0; i < lines.size(); ++i)
	{
		std::string l = t.substr(lines[i].begin, lines[i].end - lines[i].begin);
		if (l.empty() || l[0] == ';') continue;
		if (l == kOptSideMagic && !magic)
		{
			magic = true;
			continue;
		}
		size_t eq = l.find('=');
		size_t idx = 4;
		if (magic && eq != std::string::npos)
			for (size_t j = 0; j < 4; ++j)
				if (l.compare(0, eq, names[j]) == 0 && eq == strlen(names[j])) idx = j;
		if (idx == 4 || have[idx])
		{
			*err = "side file line " + Num(i + 1) + " not understood";
			return 2;
		}
		have[idx] = true;
		std::string v = l.substr(eq + 1);
		bool ok = true;
		if (idx == 0)
		{
			ok = v == "0" || v == "1";
			s->existed = v == "1";
		}
		else
			ok = Unhex(v, idx == 1 ? &s->before : idx == 2 ? &s->after : &s->replaced);
		if (!ok)
		{
			*err = "side file line " + Num(i + 1) + ": bad value of " + names[idx];
			return 2;
		}
	}
	for (size_t j = 0; j < 4; ++j)
		if (!have[j])
		{
			*err = std::string("side file without ") + names[j];
			return 2;
		}
	if (s->after.empty())
	{
		*err = "side file with an empty record of the file on left";
		return 2;
	}
	return 0;
}

// Reads the options file: 0 ok, 1 none, 2 unreadable or not editable (err).
int ReadOptions(const std::wstring& path, std::string* t, std::string* err)
{
	int r = ReadAll(path, t, err);
	if (r) return r;
	if (t->size() > kOptMaxBytes)
	{
		*err = "VP64Options.ini is larger than 64 KB (the patch does not read such a file): " + Narrow(path);
		return 2;
	}
	if (t->size() >= 2 && ((unsigned char)(*t)[0] == 0xFF && (unsigned char)(*t)[1] == 0xFE ||
	                       (unsigned char)(*t)[0] == 0xFE && (unsigned char)(*t)[1] == 0xFF))
	{
		*err = "VP64Options.ini is saved as UTF-16 (\"Unicode\"); save it as UTF-8 or ANSI so that the switch can edit it: " +
		       Narrow(path);
		return 2;
	}
	if (t->find('\0') != std::string::npos)
	{
		*err = "VP64Options.ini holds NUL bytes; not a text file: " + Narrow(path);
		return 2;
	}
	return 0;
}

bool ReadOnly(const std::wstring& path)
{
	DWORD a = GetFileAttributesW(path.c_str());
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY);
}
}  // namespace

int RendererValue(const std::string& text, bool* deciding)
{
	OptScan s;
	OptScanText(text, kRendererSpec, &s);
	if (deciding) *deciding = s.deciding;
	return s.value;
}

int OptionValue(const std::string& text, const char* key, int minV, int maxV, int defV, bool* deciding)
{
	OptSpec spec = {key, "", minV, maxV, defV};
	OptScan s;
	OptScanText(text, spec, &s);
	if (deciding) *deciding = s.deciding;
	return s.value;
}

// Ticket 4931 (design 1.7): one option's line, the player's choice; no side file. The placement rules are RendererOn's.
Result SetOptionLine(const std::wstring& optionsIni, const char* section, const char* key, int value)
{
	Result res = {false, std::string(), std::vector<std::string>()};
	if (!section || !section[0] || !key || !key[0] || strpbrk(key, "=;#[] \t\r\n") || strpbrk(section, "[]\r\n"))
	{
		res.error = "internal: SetOptionLine needs a plain key and section; nothing changed";
		return res;
	}
	std::string t, err;
	int r = ReadOptions(optionsIni, &t, &err);
	if (r == 2)
	{
		res.error = err + "; nothing changed";
		return res;
	}
	const bool exists = r == 0;
	// value already decided by the last line of the key: nothing to write. (The scan's range is [value, value] with a
	// default that is not value, so s.value == value only when the deciding line holds exactly value.)
	OptSpec spec = {key, section, value, value, value == 0 ? 1 : 0};
	OptScan s;
	OptScanText(t, spec, &s);
	if (exists && s.deciding && s.value == value)
	{
		res.ok = true;
		return res;
	}
	char num[16];
	_snprintf_s(num, sizeof(num), _TRUNCATE, "%d", value);
	const std::string line = std::string(key) + " = " + num;
	std::string out;
	if (!exists)
	{
		out = "; VP64Options.ini - options of the Vox Populi 64-bit patch (written by vp64helper; every option is\r\n"
		      "; described in OPTIONS.md, shipped with the patch).\r\n\r\n[" +
		      std::string(section) + "]\r\n" + line + "\r\n";
	}
	else if (s.deciding || s.commented)
	{
		size_t i = s.deciding ? s.decLine : s.comLine;
		size_t b = s.Begin(i), e = s.lines[i].end;
		out = t.substr(0, b) + line + t.substr(e);
	}
	else if (s.section)
	{
		const Line& h = s.lines[s.secLine];
		out = t.substr(0, h.next) + (h.next == h.end ? s.eol : std::string()) + line + s.eol + t.substr(h.next);
	}
	else
	{
		out = t;
		if (!out.empty() && out[out.size() - 1] != '\n' && out[out.size() - 1] != '\r') out += s.eol;
		out += std::string("[") + section + "]" + s.eol + line + s.eol;
	}
	if (exists && ReadOnly(optionsIni))
	{
		res.error = "VP64Options.ini is read-only: " + Narrow(optionsIni) + "; nothing changed";
		return res;
	}
	if (!WriteAtomic(optionsIni, out, &err))
	{
		res.error = "cannot write VP64Options.ini: " + err + "; nothing changed";
		return res;
	}
	res.keys.push_back(key);
	res.ok = true;
	return res;
}

std::wstring OptionsIniPath(const std::wstring& configIni)
{
	wchar_t env[MAX_PATH * 4];
	DWORD n = GetEnvironmentVariableW(L"VP64_OPTIONS", env, MAX_PATH * 4);
	if (n > 0 && n < MAX_PATH * 4) return env;
	size_t s = configIni.find_last_of(L"\\/");
	return (s == std::wstring::npos ? std::wstring() : configIni.substr(0, s + 1)) + L"VP64Options.ini";
}

std::wstring OptionsSidePath(const std::wstring& optionsIni)
{
	size_t s = optionsIni.find_last_of(L"\\/");
	return (s == std::wstring::npos ? std::wstring() : optionsIni.substr(0, s + 1)) + L"vp64-options-side.ini";
}

Result RendererOn(const std::wstring& optionsIni)
{
	Result res = {false, std::string(), std::vector<std::string>()};
	std::string t, err;
	int r = ReadOptions(optionsIni, &t, &err);
	if (r == 2)
	{
		res.error = err + "; nothing changed";
		return res;
	}
	const bool exists = r == 0;
	const std::wstring side = OptionsSidePath(optionsIni);
	OptSide old;
	int sr = ReadOptSide(side, &old, &err);
	if (sr == 2)
	{
		res.error = "the side file " + Narrow(side) + " cannot be used (" + err +
		            "); nothing changed. Run \"vp64helper off\" first (it leaves VP64Options.ini as it is and moves the "
		            "side file aside) or delete it";
		return res;
	}
	OptScan s;
	OptScanText(t, kRendererSpec, &s);
	if (exists && s.value == 1 && sr == 0)
	{
		res.ok = true;  // on already: the side file holds the player's file
		return res;
	}
	OptSide rec;
	rec.existed = exists;
	rec.before = t;
	std::string out;
	if (exists && s.value == 1)
	{
		// the player's own 1 (no side file): recorded, nothing written; off then gives the same bytes back
		rec.replaced = t.substr(s.Begin(s.decLine), s.lines[s.decLine].end - s.Begin(s.decLine));
		out = t;
	}
	else if (!exists)
	{
		out = std::string(kOptNewFile) + "\r\n[" + kOptSection + "]\r\n" + kOptLine + "\r\n";
	}
	else if (s.deciding || s.commented)
	{
		size_t i = s.deciding ? s.decLine : s.comLine;
		size_t b = s.Begin(i), e = s.lines[i].end;
		rec.replaced = t.substr(b, e - b);
		out = t.substr(0, b) + kOptLine + t.substr(e);
	}
	else if (s.section)
	{
		const Line& h = s.lines[s.secLine];
		out = t.substr(0, h.next) + (h.next == h.end ? s.eol : std::string()) + kOptLine + s.eol + t.substr(h.next);
	}
	else
	{
		out = t;
		if (!out.empty() && out[out.size() - 1] != '\n' && out[out.size() - 1] != '\r') out += s.eol;
		out += std::string("[") + kOptSection + "]" + s.eol + kOptLine + s.eol;
	}
	rec.after = out;
	const bool change = out != t || !exists;
	if (change && ReadOnly(optionsIni))
	{
		res.error = "VP64Options.ini is read-only: " + Narrow(optionsIni) + "; nothing changed";
		return res;
	}
	if (!WriteAtomic(side, OptSideText(rec), &err))
	{
		res.error = "cannot write the side file: " + err + "; nothing changed";
		return res;
	}
	if (change && !WriteAtomic(optionsIni, out, &err))
	{
		if (sr == 1) DeleteFileW(side.c_str());
		else WriteAtomic(side, OptSideText(old), &err);  // the earlier record back
		res.error = "cannot write VP64Options.ini: " + err + "; nothing changed";
		return res;
	}
	if (change) res.keys.push_back(kOptKey);
	res.ok = true;
	return res;
}

Result RendererOff(const std::wstring& optionsIni)
{
	Result res = {false, std::string(), std::vector<std::string>()};
	std::string t, err;
	const std::wstring side = OptionsSidePath(optionsIni);
	OptSide rec;
	int sr = ReadOptSide(side, &rec, &err);
	if (sr != 0)
	{
		// like config.ini (issue 0378): without our record a 1 may be the player's own; nothing is written
		std::string cur, e2;
		bool one = ReadOptions(optionsIni, &cur, &e2) == 0 && RendererValue(cur, NULL) == 1;
		if (sr == 1)
			res.note = "no side file " + Narrow(side) + ": VP64Options.ini left as it is";
		else
		{
			res.note = "side file " + Narrow(side) + " unusable (" + err + "): VP64Options.ini left as it is";
			const std::wstring aside = side + L".unusable";
			if (MoveFileExW(side.c_str(), aside.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
				res.note += "; the side file moved to " + Narrow(aside);
			else
				res.note += "; " + WinErr("the side file could not be moved aside", GetLastError());
		}
		if (one)
		{
			res.keys.push_back(std::string("left:") + kOptKey);
			res.note += "; ServerRenderer = 1 left as it is (the player's own value, or the patch's if its side file was lost)";
		}
		res.ok = true;
		return res;
	}
	int r = ReadOptions(optionsIni, &t, &err);
	if (r == 2)
	{
		res.error = err + "; the side file is kept";
		return res;
	}
	bool write = false, remove = false;
	std::string out;
	if (r == 0 && t == rec.after)
	{
		// untouched since On: its record back byte for byte
		if (!rec.existed) remove = true;
		else if (rec.before != t)
		{
			out = rec.before;
			write = true;
		}
		if (remove || write) res.keys.push_back(kOptKey);
	}
	else if (r == 1)
	{
		res.keys.push_back(std::string("kept:") + kOptKey);  // the file was removed after On: the default 0 holds
		res.note = "VP64Options.ini was removed after on: nothing to put back";
	}
	else
	{
		OptScan s;
		OptScanText(t, kRendererSpec, &s);
		size_t b = s.deciding ? s.Begin(s.decLine) : 0, e = s.deciding ? s.lines[s.decLine].end : 0;
		if (s.deciding && t.compare(b, e - b, kOptLine) == 0)
		{
			if (!rec.replaced.empty()) out = t.substr(0, b) + rec.replaced + t.substr(e);
			else out = t.substr(0, b) + t.substr(s.lines[s.decLine].next);
			write = out != t;
			res.keys.push_back(kOptKey);
			res.note = "VP64Options.ini was changed after on: only its ServerRenderer line put back, the other changes kept";
		}
		else
		{
			res.keys.push_back(std::string("kept:") + kOptKey);
			res.note = "ServerRenderer was changed after on: left as it is";
		}
	}
	if ((write || remove) && ReadOnly(optionsIni))
	{
		res.keys.clear();
		res.note.clear();
		res.error = "VP64Options.ini is read-only: " + Narrow(optionsIni) + "; the side file is kept";
		return res;
	}
	if (write && !WriteAtomic(optionsIni, out, &err))
	{
		res.keys.clear();
		res.note.clear();
		res.error = "cannot write VP64Options.ini: " + err + "; the side file is kept";
		return res;
	}
	if (remove && !DeleteFileW(optionsIni.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)
	{
		DWORD e = GetLastError();
		res.keys.clear();
		res.note.clear();
		res.error = WinErr("cannot remove VP64Options.ini (on created it)", e) + ": " + Narrow(optionsIni) +
		            "; the side file is kept";
		return res;
	}
	if (!DeleteFileW(side.c_str()))
	{
		DWORD e = GetLastError();
		if (e != ERROR_FILE_NOT_FOUND)
		{
			res.error = WinErr("VP64Options.ini restored, but the side file cannot be removed", e) + ": " + Narrow(side);
			return res;
		}
	}
	res.ok = true;
	return res;
}

State RendererStatus(const std::wstring& optionsIni, Result* detail)
{
	Result tmp = {true, std::string(), std::vector<std::string>()};
	Result& d = detail ? *detail : tmp;
	d.ok = true;
	d.error.clear();
	d.keys.clear();
	d.note.clear();
	std::string t, err;
	int r = ReadOptions(optionsIni, &t, &err);
	if (r == 2) d.error = err;
	int v = r == 0 ? RendererValue(t, NULL) : 0;
	OptSide rec;
	int sr = ReadOptSide(OptionsSidePath(optionsIni), &rec, &err);
	if (sr == 2)
	{
		d.error = "side file unusable: " + err;
		d.keys.push_back(kOptKey);
		return State::Partial;
	}
	if (sr == 1)
	{
		if (v == 1) d.keys.push_back(kOptKey);  // 1 set by hand, without the switch
		return v == 1 ? State::Partial : State::Off;
	}
	if (v != 1) d.keys.push_back(kOptKey);
	return v == 1 ? State::On : State::Partial;
}
}  // namespace cfgini
