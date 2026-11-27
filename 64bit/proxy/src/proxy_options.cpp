// proxy_options.cpp - the proxy's part of the options file (one-off 9042, decisions D89 and D88). Hand-written.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// At a mode=on boot the proxy, the only part of the patch inside the EXE and so the only one that sees the player's own
// Documents folder (the EXE's, redirected per test instance by the harness's shim), finds VP64Options.ini, creates it
// with the commented defaults when it is missing, reads it, logs every warning and every value in vpproxy.log, and
// hands the file's path to the server as the server.cfg line options_file=<UTF-8 path>. The server reads the same
// file with the same code (server/src/srv_options.cpp). Loader and rules: options/include/vp64_options.h; design:
// research/07-architecture-decisions/options-file.md. Toolchain: clang-cl -m32, VC9 headers (proxy_rt_internal.h).
//
// Ticket 0624: in a D33 session (config.ini in the patch's configuration) a missing file is created with the release's
// option set (<x64_dir>\release-options.txt, ticket 0574) and ServerRenderer = 1, or not at all when that set is missing
// or unusable; and OptionsGuard, called once by Boot (proxy_rt.cpp) right after OptionsBoot, refuses a D33 boot whose
// options say ServerRenderer = 0 (proxy_options.h). Offline test: proxy/tests/options_boot/.
#include "proxy_options.h"
#include "vp64_options.h"
#include <windows.h>
#include <string.h>

namespace ProxyRtInt  // proxy_rt_internal.h (declared here, as proxy_shell.cpp does, so that the offline test of ticket
{                     // 0624 needs no generated header; a signature that differs fails the proxy's link)
void Log(const char* fmt, ...);
std::string OptionsBoot(const std::string& x64Dir, bool d33, const std::string& sessionWhy);
std::string OptionsGuard(bool d33, const std::string& sessionWhy, bool writesServerCfg);
std::string CompositeGuard(bool d33, const std::string& sessionWhy, bool writesServerCfg, const char* refused);
const Vp64Opt::Options& ProxyOptions();
}  // namespace ProxyRtInt

// ================================================================================================ ticket 0624 (pure)
namespace ProxyOptions
{
namespace
{
bool IsName(const std::string& s)
{
	if (s.empty() || s.size() > 64) return false;
	for (size_t i = 0; i < s.size(); ++i)
	{
		const char c = s[i];
		const bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
		if (!(letter || (i > 0 && ((c >= '0' && c <= '9') || c == '_')))) return false;
	}
	return true;
}

bool IsWhole(const std::string& s, int* v)
{
	const size_t f = (!s.empty() && s[0] == '-') ? 1 : 0;
	if (s.size() <= f || s.size() - f > 9) return false;
	long n = 0;
	for (size_t i = f; i < s.size(); ++i)
	{
		if (s[i] < '0' || s[i] > '9') return false;
		n = n * 10 + (s[i] - '0');
	}
	*v = f ? -(int)n : (int)n;
	return true;
}

// As Inno Setup's Trim: spaces and control characters at both ends.
std::string TrimCtl(const std::string& s)
{
	size_t a = 0, b = s.size();
	while (a < b && (unsigned char)s[a] <= ' ') ++a;
	while (b > a && (unsigned char)s[b - 1] <= ' ') --b;
	return s.substr(a, b - a);
}

bool SameText(const char* a, const std::string& b) { return _stricmp(a, b.c_str()) == 0; }

std::string Num(int v)
{
	char b[16];
	_snprintf_s(b, sizeof(b), _TRUNCATE, "%d", v);
	return b;
}

// Replaces the option's commented default line "; Key = <default>" (a whole line, followed by an empty line or the
// end of the text, which no help line is: help lines are followed by the next comment line) by "Key = <value>".
bool SetLine(std::string& text, const Vp64Opt::Def& d, int value, std::string* why)
{
	const std::string needle = std::string("; ") + d.key + " = " + Num(d.defaultValue) + "\r\n";
	size_t found = std::string::npos;
	int n = 0;
	for (size_t p = text.find(needle); p != std::string::npos; p = text.find(needle, p + 1))
	{
		if (p > 0 && text[p - 1] != '\n') continue;
		const size_t after = p + needle.size();
		if (after != text.size() && text.compare(after, 2, "\r\n") != 0) continue;
		found = p;
		++n;
	}
	if (n != 1)
	{
		*why = "the default file's text has " + Num(n) + " commented default line(s) \"; " + d.key + " = " +
		       Num(d.defaultValue) + "\" (one expected)";
		return false;
	}
	text.replace(found, needle.size(), std::string(d.key) + " = " + Num(value) + "\r\n");
	return true;
}
}  // namespace

bool ParseSet(const char* text, size_t len, std::vector<SetRow>* rows, std::string* why)
{
	rows->clear();
	why->clear();
	size_t i = (len >= 3 && memcmp(text, "\xEF\xBB\xBF", 3) == 0) ? 3 : 0;
	int lineNo = 0;
	while (i < len)
	{
		size_t e = i;
		while (e < len && text[e] != '\n') ++e;
		std::string line(text + i, e - i);
		i = e + 1;
		++lineNo;
		if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
		const std::string where = "line " + Num(lineNo) + ": ";
		if (line.find('\0') != std::string::npos)
		{
			*why = where + "a NUL byte (not a text file)";
			return false;
		}
		const std::string t = TrimCtl(line);
		if (t.empty() || t[0] == '#') continue;
		std::vector<std::string> f;
		size_t a = 0;
		for (;;)
		{
			const size_t tab = line.find('\t', a);
			f.push_back(TrimCtl(line.substr(a, tab == std::string::npos ? std::string::npos : tab - a)));
			if (tab == std::string::npos) break;
			a = tab + 1;
		}
		SetRow r;
		if (f.size() != 3 || !IsName(f[0]) || !IsName(f[1]) || !IsWhole(f[2], &r.value))
		{
			*why = where + "not \"Section<TAB>Key<TAB>whole number\": " + t;
			return false;
		}
		if (SameText("Renderer", f[0]) && SameText("ServerRenderer", f[1]))
		{
			*why = where + "[Renderer] ServerRenderer is in the set (vp64helper on sets it; install.iss refuses it too)";
			return false;
		}
		const Vp64Opt::Id id = Vp64Opt::FindKey(f[1].c_str(), f[1].size());
		if (id >= Vp64Opt::kCount)
		{
			*why = where + "[" + f[0] + "] " + f[1] + ": no such option in this build's options table";
			return false;
		}
		const Vp64Opt::Def& d = Vp64Opt::Definition(id);
		if (!SameText(d.section, f[0]))
		{
			*why = where + "[" + f[0] + "] " + f[1] + ": the option's section is [" + d.section + "]";
			return false;
		}
		if (r.value < d.minValue || r.value > d.maxValue)
		{
			*why = where + "[" + f[0] + "] " + f[1] + " = " + f[2] + ": outside its range " + Num(d.minValue) + " to " +
			       Num(d.maxValue);
			return false;
		}
		for (size_t k = 0; k < rows->size(); ++k)
			if ((*rows)[k].id == (int)id)
			{
				*why = where + "[" + f[0] + "] " + f[1] + " is in the set twice";
				return false;
			}
		r.id = (int)id;
		r.section = d.section;
		r.key = d.key;
		rows->push_back(r);
	}
	if (rows->empty())
	{
		*why = "the set is empty";
		return false;
	}
	return true;
}

int ReadSet(const std::string& dir, std::vector<SetRow>* rows, std::string* path, std::string* why)
{
	rows->clear();
	why->clear();
	*path = dir + "\\" VP64_RELEASE_SET_FILE;
	HANDLE h = CreateFileA(path->c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
	{
		const DWORD err = GetLastError();
		if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
		{
			*why = "no such file";
			return 0;
		}
		*why = "cannot be opened (error " + Num((int)err) + ")";
		return -1;
	}
	std::string text(kMaxSetBytes + 1, '\0');
	DWORD got = 0;
	const BOOL ok = ReadFile(h, &text[0], (DWORD)text.size(), &got, NULL);
	CloseHandle(h);
	if (!ok)
	{
		*why = "cannot be read";
		return -1;
	}
	if (got > (DWORD)kMaxSetBytes)
	{
		*why = "larger than 64 KB";
		return -1;
	}
	text.resize(got);
	if (!ParseSet(text.data(), text.size(), rows, why)) return -1;
	return 1;
}

bool TextWithSet(const std::vector<SetRow>& rows, bool serverRendererOn, std::string* text, std::string* why)
{
	why->clear();
	const size_t n = Vp64Opt::DefaultFileText(NULL, 0);
	text->assign(n + 1, '\0');
	Vp64Opt::DefaultFileText(&(*text)[0], n + 1);
	text->resize(n);
	for (size_t i = 0; i < rows.size(); ++i)
		if (!SetLine(*text, Vp64Opt::Definition((Vp64Opt::Id)rows[i].id), rows[i].value, why)) return false;
	if (serverRendererOn && !SetLine(*text, Vp64Opt::Definition(Vp64Opt::kServerRenderer), 1, why)) return false;
	return true;
}

int CreateWithText(const wchar_t* path, const std::string& text, unsigned long* error)
{
	*error = 0;
	HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
	{
		const DWORD err = GetLastError();
		if (err == ERROR_FILE_EXISTS || err == ERROR_ALREADY_EXISTS) return 1;
		*error = err;
		return 2;
	}
	DWORD put = 0;
	const BOOL ok = WriteFile(h, text.data(), (DWORD)text.size(), &put, NULL) && put == (DWORD)text.size();
	const DWORD err = ok ? 0 : GetLastError();
	CloseHandle(h);
	if (!ok)
	{
		DeleteFileW(path);  // never a half-written file: the next start tries again
		*error = err ? err : ERROR_WRITE_FAULT;
		return 2;
	}
	return 0;
}

std::string GuardWhy(bool d33, const std::string& sessionWhy, bool writesServerCfg, bool helperMarker, int serverRenderer,
                     bool serverRendererFromFile, const std::string& createNote)
{
	if (!d33 || !writesServerCfg || helperMarker || serverRenderer != 0) return std::string();
	std::string why = "VP64Options.ini gives [Renderer] ServerRenderer = 0 (" +
	                  std::string(serverRendererFromFile ? "set in the file" : "the table's default: the file does not set it") +
	                  ") in a D33 session (" + sessionWhy + "): config.ini keeps the game's own fog of war, forests, "
	                  "decals and yield icons off and the server would draw none of them, a degraded world (ticket 0624)";
	if (!createNote.empty()) why += "; " + createNote;
	return why;
}

std::string CompositeGuardWhy(bool d33, const std::string& sessionWhy, bool writesServerCfg, bool helperMarker,
                              int serverRenderer, const std::string& refused)
{
	if (!d33 || !writesServerCfg || helperMarker || serverRenderer != 1 || refused.empty()) return std::string();
	return "[Renderer] ServerRenderer = 1 in a D33 session (" + sessionWhy + "), but the " + refused +
	       ": nothing would put the server's drawing into the game's picture while config.ini keeps the game's fog of war, "
	       "forests, decals and yield icons off, a degraded world (a partial or damaged install) (ticket 0624)";
}
}  // namespace ProxyOptions

namespace
{
Vp64Opt::Options g_options;  // the values in force in the proxy (defaults until the boot has read the file)
std::string g_createNote;    // ticket 0624: why a missing file was not created in a D33 session ("" otherwise)

void Warn(void* ctx, const char* line) { ProxyRtInt::Log("options: %s: %s", (const char*)ctx, line); }

const char* StatusText(Vp64Opt::LoadStatus st)
{
	switch (st)
	{
	case Vp64Opt::kLoaded: return "read";
	case Vp64Opt::kMissing: return "missing";
	case Vp64Opt::kUnreadable: return "unreadable";
	case Vp64Opt::kTooLarge: return "too large";
	}
	return "?";
}

void LogValues()
{
	for (int i = 0; i < Vp64Opt::kCount; ++i)
	{
		const Vp64Opt::Def& d = Vp64Opt::Definition((Vp64Opt::Id)i);
		ProxyRtInt::Log("options: [%s] %s = %d (%s)", d.section, d.key, g_options.value[i],
		                g_options.fromFile[i] ? "from the file" : "default");
	}
	for (int i = 0; i < Vp64Opt::kTextCount; ++i)  // ticket 0680: the text options
	{
		const Vp64Opt::TextDef& d = Vp64Opt::TextDefinition((Vp64Opt::TextId)i);
		ProxyRtInt::Log("options: [%s] %s = \"%s\" (%s)", d.section, d.key, g_options.text[i],
		                g_options.textFromFile[i] ? "from the file" : "default");
	}
}

// Ticket 4931: every extra's effective state and why (docs/extras/flags-and-messages-design.md 1.4). First both switches,
// then one line per [Extras] row with a key that is no switch (reserved rows skipped). The same lines as the server's.
void LogExtras()
{
	char line[512];
	Vp64Opt::DescribeSwitches(g_options, line, sizeof(line));
	ProxyRtInt::Log("options: extras: %s", line);
	int n = 0;
	for (int i = 0; i < Vp64Opt::kCount; ++i)
	{
		const Vp64Opt::Def& d = Vp64Opt::Definition((Vp64Opt::Id)i);
		if (!d.key[0] || (d.category != Vp64Opt::kCatBugfix && d.category != Vp64Opt::kCatVisual)) continue;
		Vp64Opt::DescribeExtra(g_options, (Vp64Opt::Id)i, line, sizeof(line));
		ProxyRtInt::Log("options: extras: %s", line);
		++n;
	}
	if (!n) ProxyRtInt::Log("options: extras: this build has no extra yet (only the two switches)");
}

void LogAll()
{
	LogValues();
	LogExtras();
}
}  // namespace

namespace ProxyRtInt
{
const Vp64Opt::Options& ProxyOptions() { return g_options; }

std::string OptionsBoot(const std::string& x64Dir, bool d33, const std::string& sessionWhy)
{
	g_options.SetDefaults();
	g_createNote.clear();
	wchar_t path[1024];
	const Vp64Opt::PathSource src = Vp64Opt::DefaultPath(path, sizeof(path) / sizeof(path[0]));
	if (src == Vp64Opt::kPathNone)
	{
		Log("options: no options file: %s; every option takes its default",
		    GetEnvironmentVariableW(VP64OPT_ENV_VAR, NULL, 0) ? "VP64_OPTIONS is longer than 1023 characters"
		                                                       : "the Documents folder could not be found");
		if (d33) g_createNote = "there is no options file to read (no path)";
		LogAll();
		return std::string();
	}
	char u8[3 * 1024 + 1];
	if (!Vp64Opt::WideToUtf8(path, u8, sizeof(u8)))
	{
		Log("options: the options file's path cannot be converted to UTF-8; every option takes its default");
		if (d33) g_createNote = "the options file's path cannot be converted to UTF-8";
		LogAll();
		return std::string();
	}
	const char* where = src == Vp64Opt::kPathEnv ? "named by VP64_OPTIONS" : "in the Documents folder";
	unsigned long err = 0;
	// Ticket 0624: a D33 session reads the release's option set (needed when the file is missing; named when it lacks keys)
	std::vector<ProxyOptions::SetRow> set;
	std::string setPath, setWhy;
	const int setSt = d33 ? ProxyOptions::ReadSet(x64Dir, &set, &setPath, &setWhy) : 0;
	const bool existed = GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
	if (!d33)
	{
		const Vp64Opt::CreateStatus cs = Vp64Opt::CreateDefaultFile(path, &err);
		if (cs == Vp64Opt::kCreated)
			Log("options: %s (%s) did not exist: created with every option at its default", u8, where);
		else if (cs == Vp64Opt::kCreateFailed)
			Log("options: %s (%s) does not exist and could not be created (error %lu)", u8, where, err);
	}
	else if (!existed && setSt != 1)
	{
		g_createNote = "the file did not exist and the release's option set " + setPath + " " +
		               (setSt == 0 ? std::string("does not exist") : "is not usable: " + setWhy) +
		               ", so the file was not created";
		Log("options: LOUD: %s (%s) does not exist in a D33 session (%s) and the release's option set %s %s: the file is "
		    "NOT created (the table's defaults would give ServerRenderer = 0, a degraded world) (ticket 0624)",
		    u8, where, sessionWhy.c_str(), setPath.c_str(),
		    setSt == 0 ? "does not exist" : ("is not usable: " + setWhy).c_str());
	}
	else if (!existed)
	{
		std::string text, why, names;
		for (size_t i = 0; i < set.size(); ++i)
		{
			char one[96];
			_snprintf_s(one, sizeof(one), _TRUNCATE, "%s%s = %d", i ? ", " : "", set[i].key.c_str(), set[i].value);
			names += one;
		}
		if (!ProxyOptions::TextWithSet(set, true, &text, &why))
		{
			g_createNote = "the file did not exist and its text with the release's option set could not be made: " + why;
			Log("options: LOUD: %s (%s) does not exist in a D33 session: NOT created: %s (ticket 0624)", u8, where, why.c_str());
		}
		else
		{
			const int cs = ProxyOptions::CreateWithText(path, text, &err);
			if (cs == 0)
				Log("options: LOUD: %s (%s) did not exist in a D33 session (%s): re-created with the release's option set %s "
				    "(%s) and ServerRenderer = 1, the other options at their defaults (ticket 0624)",
				    u8, where, sessionWhy.c_str(), setPath.c_str(), names.c_str());
			else if (cs == 2)
			{
				char e[64];
				_snprintf_s(e, sizeof(e), _TRUNCATE, "%lu", err);
				g_createNote = "the file did not exist and could not be created (error " + std::string(e) + ")";
				Log("options: LOUD: %s (%s) does not exist and could not be created with the release's option set (error "
				    "%lu) (ticket 0624)", u8, where, err);
			}
		}
	}
	unsigned warnings = 0;
	const Vp64Opt::LoadStatus st = Vp64Opt::LoadFile(path, g_options, Warn, u8, &warnings);
	Log("options: %s (%s): %s, %u warning%s", u8, where, StatusText(st), warnings, warnings == 1 ? "" : "s");
	if (d33 && existed && setSt == 1)
	{
		// Ticket 0624: an existing file (an older release's after an update, or keys commented out by hand) is never
		// rewritten by the proxy; the set's keys it does not set take the table's defaults, said once here.
		std::string missing;
		int n = 0;
		for (size_t i = 0; i < set.size(); ++i)
		{
			const int id = set[i].id;
			if (g_options.fromFile[id] || g_options.value[id] == set[i].value) continue;
			char one[128];
			_snprintf_s(one, sizeof(one), _TRUNCATE, "%s[%s] %s = %d (the release's %d)", n ? ", " : "",
			            set[i].section.c_str(), set[i].key.c_str(), g_options.value[id], set[i].value);
			missing += one;
			++n;
		}
		if (n)
			Log("options: note: %s does not set %d option(s) of the release's set %s, which take the table's defaults: %s; "
			    "the installer writes them at an update, or delete the file to have it re-created with the set (ticket 0624)",
			    u8, n, setPath.c_str(), missing.c_str());
	}
	else if (d33 && setSt == -1)
		Log("options: the release's option set %s is not usable: %s (ticket 0624)", setPath.c_str(), setWhy.c_str());
	LogAll();
	return u8;
}

std::string OptionsGuard(bool d33, const std::string& sessionWhy, bool writesServerCfg)
{
	const bool marker = GetFileAttributesA("crashlogs\\shell.helper") != INVALID_FILE_ATTRIBUTES;  // proxy_shell.cpp's switch
	const bool fromFile = g_options.fromFile[Vp64Opt::kServerRenderer] != 0;
	const std::string why = ProxyOptions::GuardWhy(d33, sessionWhy, writesServerCfg, marker, g_options.ServerRenderer(),
	                                               fromFile, g_createNote);
	if (why.empty() && d33 && g_options.ServerRenderer() == 0)
		Log("options: LOUD: TEST HOOK: the renderer guard was skipped (ServerRenderer = 0 in a D33 session): %s (ticket "
		    "0624)",
		    !writesServerCfg ? "vpproxy.ini server_config= names the server's config (a test's)"
		                     : "the test switch crashlogs\\shell.helper is present");
	return why;
}

std::string CompositeGuard(bool d33, const std::string& sessionWhy, bool writesServerCfg, const char* refused)
{
	const bool marker = GetFileAttributesA("crashlogs\\shell.helper") != INVALID_FILE_ATTRIBUTES;  // proxy_shell.cpp's switch
	const std::string r = refused ? refused : "";
	const std::string why = ProxyOptions::CompositeGuardWhy(d33, sessionWhy, writesServerCfg, marker,
	                                                        g_options.ServerRenderer(), r);
	if (why.empty() && d33 && g_options.ServerRenderer() == 1 && !r.empty())
		Log("options: LOUD: TEST HOOK: the composite guard was skipped (%s in a D33 session): %s (ticket 0624)", r.c_str(),
		    !writesServerCfg ? "vpproxy.ini server_config= names the server's config (a test's)"
		                     : "the test switch crashlogs\\shell.helper is present");
	return why;
}
}  // namespace ProxyRtInt
