// options_boot_test.cpp - offline test of ticket 0624 (proxy/src/proxy_options.cpp, proxy_options.h): the release's
// option set, the options file created with it in a D33 session, the existing file that lacks keys, and the renderer
// guard. Built and run by test_options_boot.py with the proxy's toolchain and under MSVC ASan. Nothing of the game.
// SPDX-License-Identifier: GPL-3.0-or-later
//
//   options_boot_test.exe <scratch dir> [-v] [--expect-fail]       (the new code)
//   options_boot_test.exe <scratch dir> --old [-v]                 (VP_OPTIONS_BOOT_OLD: main's OptionsBoot() before
//                                                                    0624, the reproduction: prints what it gives)
// Ends with "options_boot_test: N/M checks passed"; exit 0 when all pass.
#include "proxy_options.h"
#include "vp64_options.h"
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

namespace ProxyRtInt
{
#ifdef VP_OPTIONS_BOOT_OLD
std::string OptionsBoot();
#else
std::string OptionsBoot(const std::string& x64Dir, bool d33, const std::string& sessionWhy);
std::string OptionsGuard(bool d33, const std::string& sessionWhy, bool writesServerCfg);
std::string CompositeGuard(bool d33, const std::string& sessionWhy, bool writesServerCfg, const char* refused);
#endif
const Vp64Opt::Options& ProxyOptions();

std::vector<std::string> g_lines;
bool g_verbose = false;
void Log(const char* fmt, ...)
{
	char buf[4096];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
	va_end(ap);
	g_lines.push_back(buf);
	if (g_verbose) printf("    log| %s\n", buf);
}
}  // namespace ProxyRtInt

namespace
{
int g_pass = 0, g_total = 0;
bool g_expectFail = false;

bool Check(const char* name, bool ok, const std::string& detail = std::string())
{
	++g_total;
	if (ok)
		++g_pass;
	printf("%s %s\n", ok ? "ok  " : "FAIL", name);
	if (!ok && !detail.empty()) printf("  (%s)\n", detail.c_str());
	return ok;
}

std::string g_root;

std::wstring W(const std::string& s)
{
	wchar_t b[2048];
	MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, b, 2048);
	return b;
}

bool Exists(const std::string& p) { return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::string ReadAll(const std::string& p)
{
	std::string t;
	FILE* f = NULL;
	if (fopen_s(&f, p.c_str(), "rb") != 0 || !f) return t;
	char b[4096];
	size_t n;
	while ((n = fread(b, 1, sizeof(b), f)) > 0) t.append(b, n);
	fclose(f);
	return t;
}

void WriteAll(const std::string& p, const std::string& t)
{
	FILE* f = NULL;
	if (fopen_s(&f, p.c_str(), "wb") == 0 && f)
	{
		fwrite(t.data(), 1, t.size(), f);
		fclose(f);
	}
}

std::string DefaultText()
{
	const size_t n = Vp64Opt::DefaultFileText(NULL, 0);
	std::string t(n + 1, '\0');
	Vp64Opt::DefaultFileText(&t[0], n + 1);
	t.resize(n);
	return t;
}

int CountWith(const char* needle)
{
	int n = 0;
	for (size_t i = 0; i < ProxyRtInt::g_lines.size(); ++i)
		if (ProxyRtInt::g_lines[i].find(needle) != std::string::npos) ++n;
	return n;
}

std::string LinesWith(const char* needle)
{
	std::string s;
	for (size_t i = 0; i < ProxyRtInt::g_lines.size(); ++i)
		if (ProxyRtInt::g_lines[i].find(needle) != std::string::npos) s += ProxyRtInt::g_lines[i] + " | ";
	return s;
}

bool Has(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }

std::string Str(int v)
{
	char b[16];
	_snprintf_s(b, sizeof(b), _TRUNCATE, "%d", v);
	return b;
}

// The first beta's option set as rc0b's descriptor names it (release-builds.md 4.3), sorted, CRLF, as
// build_installer.py writes VP64\release-options.txt.
const char kSet[] =
    "Performance\tAiDrainUnordered\t1\r\n"
    "Performance\tAiSkipVisualDrain\t1\r\n"
    "Performance\tAiUpdateTickMs\t0\r\n"
    "Performance\tHiddenEmbarks\t1\r\n"
    "Renderer\tServerArc\t0\r\n"
    "Renderer\tServerPath\t0\r\n"
    "Renderer\tServerUnits\t1\r\n";

struct Case
{
	std::string dir, opt, x64;
};

Case Prepare(const char* name, bool withSet, const char* setText = kSet)
{
	Case c;
	c.dir = g_root + "\\" + name;
	CreateDirectoryA(c.dir.c_str(), NULL);
	c.opt = c.dir + "\\VP64Options.ini";
	c.x64 = c.dir + "\\VP64";
	CreateDirectoryA(c.x64.c_str(), NULL);
	DeleteFileA(c.opt.c_str());
	DeleteFileA((c.x64 + "\\" VP64_RELEASE_SET_FILE).c_str());
	if (withSet) WriteAll(c.x64 + "\\" VP64_RELEASE_SET_FILE, setText);
	SetEnvironmentVariableA("VP64_OPTIONS", c.opt.c_str());
	SetCurrentDirectoryA(c.dir.c_str());  // the guard's test switch crashlogs\shell.helper is relative to the cwd
	ProxyRtInt::g_lines.clear();
	return c;
}

int Get(Vp64Opt::Id id) { return ProxyRtInt::ProxyOptions().value[id]; }
bool FromFile(Vp64Opt::Id id) { return ProxyRtInt::ProxyOptions().fromFile[id] != 0; }

#ifdef VP_OPTIONS_BOOT_OLD
// ---------------------------------------------------------------------------------------------- the reproduction
void Old()
{
	// A deleted file in what is, for the product, a D33 session: main's OptionsBoot has no notion of the session
	Case c = Prepare("old-deleted", true);
	ProxyRtInt::OptionsBoot();
	const std::string t = ReadAll(c.opt);
	printf("old: created: %s, bytes equal to DefaultFileText: %s\n", Exists(c.opt) ? "yes" : "no",
	       t == DefaultText() ? "yes" : "no");
	printf("old: ServerRenderer=%d ServerUnits=%d ServerCities=%d ServerLandmarks=%d HiddenEmbarks=%d AiSkipVisualDrain=%d "
	       "AiDrainUnordered=%d AiUpdateTickMs=%d\n",
	       Get(Vp64Opt::kServerRenderer), Get(Vp64Opt::kServerUnits), Get(Vp64Opt::kServerCities),
	       Get(Vp64Opt::kServerLandmarks), Get(Vp64Opt::kHiddenEmbarks), Get(Vp64Opt::kAiSkipVisualDrain),
	       Get(Vp64Opt::kAiDrainUnordered), Get(Vp64Opt::kAiUpdateTickMs));
	printf("old: LOUD lines: %d; lines naming the release set: %d\n", CountWith("LOUD"), CountWith("release"));
	for (size_t i = 0; i < ProxyRtInt::g_lines.size(); ++i)
		if (Has(ProxyRtInt::g_lines[i], "did not exist") || Has(ProxyRtInt::g_lines[i], "): read"))
			printf("old: log| %s\n", ProxyRtInt::g_lines[i].c_str());
	Check("old (reproduction): the deleted file is re-created at the table's defaults: ServerRenderer 0 and every server "
	      "feature 0, with no LOUD line and no word of the release's set",
	      Exists(c.opt) && t == DefaultText() && Get(Vp64Opt::kServerRenderer) == 0 && Get(Vp64Opt::kServerUnits) == 0 &&
	          Get(Vp64Opt::kHiddenEmbarks) == 0 && Get(Vp64Opt::kAiSkipVisualDrain) == 0 && CountWith("LOUD") == 0 &&
	          CountWith("release") == 0);
	// An existing file of an older release (only ServerRenderer = 1, as vp64helper on writes it into a fresh file)
	c = Prepare("old-older-file", true);
	WriteAll(c.opt, "[Renderer]\r\nServerRenderer = 1\r\n");
	ProxyRtInt::OptionsBoot();
	printf("old: older file: ServerRenderer=%d ServerUnits=%d HiddenEmbarks=%d AiSkipVisualDrain=%d; lines naming a "
	       "missing key: %d\n",
	       Get(Vp64Opt::kServerRenderer), Get(Vp64Opt::kServerUnits), Get(Vp64Opt::kHiddenEmbarks),
	       Get(Vp64Opt::kAiSkipVisualDrain), CountWith("does not set"));
	Check("old (reproduction): an existing file that lacks the set's keys gives the table's defaults without a word",
	      Get(Vp64Opt::kServerRenderer) == 1 && Get(Vp64Opt::kServerUnits) == 0 && Get(Vp64Opt::kHiddenEmbarks) == 0 &&
	          CountWith("does not set") == 0);
}
#else
// ---------------------------------------------------------------------------------------------- ParseSet
void TestParseSet()
{
	std::vector<ProxyOptions::SetRow> rows;
	std::string why;
	bool ok = ProxyOptions::ParseSet(kSet, strlen(kSet), &rows, &why);
	Check("set: the first beta's seven rows parse", ok && rows.size() == 7, why);
	Check("set: rows carry the table's ids, spelling and values (ServerUnits = 1, AiUpdateTickMs = 0)",
	      rows.size() == 7 && rows[6].id == Vp64Opt::kServerUnits && rows[6].key == "ServerUnits" &&
	          rows[6].section == "Renderer" && rows[6].value == 1 && rows[2].id == Vp64Opt::kAiUpdateTickMs &&
	          rows[2].value == 0);
	const std::string bom = std::string("\xEF\xBB\xBF# the set\n\n  \t\nrenderer\tserverunits\t1\nPerformance\tAiUpdateTickMs\t-1");
	ok = ProxyOptions::ParseSet(bom.data(), bom.size(), &rows, &why);
	Check("set: BOM, '#' comment, blank lines, LF, no final line end, any case, -1: two rows in the table's spelling",
	      ok && rows.size() == 2 && rows[0].key == "ServerUnits" && rows[0].section == "Renderer" && rows[1].value == -1, why);
	ok = ProxyOptions::ParseSet(" Renderer \t ServerUnits \t 1 \r\n", 30, &rows, &why);
	Check("set: blanks around the fields are trimmed (as install.iss's Trim)", ok && rows.size() == 1, why);

	struct Bad
	{
		const char* text;
		const char* want;
		const char* name;
	};
	const Bad bad[] = {
	    {"Renderer\tServerRenderer\t1\r\n", "ServerRenderer is in the set", "[Renderer] ServerRenderer refused"},
	    {"Renderer\tServerUnits\t1\r\nrenderer\tSERVERUNITS\t0\r\n", "twice", "a key twice (any case) refused"},
	    {"", "empty", "an empty file refused"},
	    {"# only a comment\r\n\r\n", "empty", "only comments refused"},
	    {"Renderer\tServerUnits\r\n", "not \"Section<TAB>Key<TAB>whole number\"", "two fields refused"},
	    {"Renderer\tServerUnits\t1\tx\r\n", "not \"Section", "four fields refused"},
	    {"Renderer ServerUnits 1\r\n", "not \"Section", "spaces instead of tabs refused"},
	    {"1Renderer\tServerUnits\t1\r\n", "not \"Section", "a section name starting with a digit refused"},
	    {"Renderer\tServerUnits\t1.0\r\n", "not \"Section", "1.0 refused"},
	    {"Renderer\tServerUnits\ton\r\n", "not \"Section", "on refused"},
	    {"Renderer\tServerUnits\t\r\n", "not \"Section", "an empty value refused"},
	    {"Renderer\tServerUnits\t1234567890\r\n", "not \"Section", "ten digits refused"},
	    {"Renderer\tServerUnitz\t1\r\n", "no such option", "an unknown key refused (W31: install.iss checks syntax only)"},
	    {"Performance\tServerUnits\t1\r\n", "section is [Renderer]", "a key in another section refused"},
	    {"Renderer\tServerUnits\t2\r\n", "outside its range 0 to 1", "a value out of range refused"},
	};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		ok = ProxyOptions::ParseSet(bad[i].text, strlen(bad[i].text), &rows, &why);
		Check((std::string("set (negative): ") + bad[i].name).c_str(), !ok && Has(why, bad[i].want),
		      "ok=" + std::string(ok ? "1" : "0") + " why=" + why);
	}
	const char nul[] = "Renderer\tServerUnits\t1\0\r\n";
	ok = ProxyOptions::ParseSet(nul, sizeof(nul) - 1, &rows, &why);
	Check("set (negative): a NUL byte refused", !ok && Has(why, "NUL"), why);
	ok = ProxyOptions::ParseSet("Renderer\tServerUnits\t1\r\nbad row\r\n", 33, &rows, &why);
	Check("set (negative): the refusal names the line (line 2)", !ok && Has(why, "line 2: "), why);
}

// ---------------------------------------------------------------------------------------------- TextWithSet
void TestTextWithSet()
{
	std::vector<ProxyOptions::SetRow> rows;
	std::string why, text;
	ProxyOptions::ParseSet(kSet, strlen(kSet), &rows, &why);
	bool ok = ProxyOptions::TextWithSet(rows, true, &text, &why);
	Check("text: made with the set and ServerRenderer = 1", ok, why);
	Vp64Opt::Options o;
	struct W
	{
		static void Warn(void* ctx, const char* line) { ((std::string*)ctx)->append(line).append(" | "); }
	};
	std::string warns;
	const unsigned nw = Vp64Opt::Parse(text.data(), text.size(), o, &W::Warn, &warns);
	Check("text: parses with no warning", nw == 0, warns);
	bool all = o.value[Vp64Opt::kServerRenderer] == 1 && o.fromFile[Vp64Opt::kServerRenderer];
	for (size_t i = 0; i < rows.size(); ++i) all = all && o.value[rows[i].id] == rows[i].value && o.fromFile[rows[i].id];
	Check("text: every set key and ServerRenderer are set from the file with the set's values", all);
	int others = 0;
	for (int i = 0; i < Vp64Opt::kCount; ++i)
	{
		bool inSet = i == Vp64Opt::kServerRenderer;
		for (size_t k = 0; k < rows.size(); ++k) inSet = inSet || rows[k].id == i;
		if (!inSet && o.fromFile[i]) ++others;
	}
	Check("text: no other option is set (they keep following the table's defaults)", others == 0);
	// only the eight commented default lines changed, each into "Key = value"
	const std::string def = DefaultText();
	std::vector<std::string> a, b;
	for (size_t p = 0, q; p < def.size(); p = q + 2)
	{
		q = def.find("\r\n", p);
		if (q == std::string::npos) q = def.size();
		a.push_back(def.substr(p, q - p));
	}
	for (size_t p = 0, q; p < text.size(); p = q + 2)
	{
		q = text.find("\r\n", p);
		if (q == std::string::npos) q = text.size();
		b.push_back(text.substr(p, q - p));
	}
	int diff = 0;
	bool shape = a.size() == b.size();
	for (size_t i = 0; shape && i < a.size(); ++i)
		if (a[i] != b[i])
		{
			++diff;
			shape = shape && a[i].compare(0, 2, "; ") == 0 && a[i].substr(2, b[i].find(" = ")) == b[i].substr(0, b[i].find(" = "));
		}
	Check("text: line for line the default file's text, only the 8 commented default lines changed (\"; Key = d\" -> "
	      "\"Key = v\"); the help line \"; ServerRenderer = 1).\" untouched",
	      shape && diff == 8 && Has(text, "\r\nServerRenderer = 1\r\n") && !Has(text, "\r\n; ServerRenderer = 0\r\n"),
	      "lines " + Str((int)a.size()) + "/" + Str((int)b.size()) + " diff " + Str(diff));
	ok = ProxyOptions::TextWithSet(rows, false, &text, &why);
	Vp64Opt::Parse(text.data(), text.size(), o, NULL, NULL);
	Check("text: serverRendererOn false leaves ServerRenderer commented (0, the default)",
	      ok && o.value[Vp64Opt::kServerRenderer] == 0 && !o.fromFile[Vp64Opt::kServerRenderer]);
}

// ---------------------------------------------------------------------------------------------- GuardWhy
void TestGuardWhy()
{
	int refusals = 0, bad = 0;
	for (int m = 0; m < 16; ++m)
	{
		const bool d33 = (m & 1) != 0, writes = (m & 2) != 0, marker = (m & 4) != 0;
		const int sr = (m & 8) ? 1 : 0;
		const std::string w = ProxyOptions::GuardWhy(d33, "D33 session: test", writes, marker, sr, true, "");
		const bool want = d33 && writes && !marker && sr == 0;
		if (!w.empty()) ++refusals;
		if (w.empty() == want) ++bad;
	}
	Check("guard rule: of the 16 combinations of D33 / writes server.cfg / shell.helper / ServerRenderer only "
	      "D33 + writes + no switch + ServerRenderer 0 refuses",
	      refusals == 1 && bad == 0);
	const std::string w = ProxyOptions::GuardWhy(true, "D33 session: config mask 0x1", true, false, 0, false, "the note");
	Check("guard rule: the reason names ServerRenderer = 0, that the file does not set it, the session and the note",
	      Has(w, "ServerRenderer = 0") && Has(w, "does not set it") && Has(w, "config mask 0x1") && Has(w, "; the note") &&
	          Has(w, "a degraded world"),
	      w);
	Check("guard rule: a file that sets 0 is named as such",
	      Has(ProxyOptions::GuardWhy(true, "s", true, false, 0, true, ""), "set in the file"));

	// ticket 0624 item 5: the composite guard's rule over D33 x writes x marker x ServerRenderer 0/1 x refused or not
	int crefusals = 0, cbad = 0;
	for (int m = 0; m < 32; ++m)
	{
		const bool d33 = (m & 1) != 0, writes = (m & 2) != 0, marker = (m & 4) != 0, refused = (m & 16) != 0;
		const int sr = (m & 8) ? 1 : 0;
		const std::string w = ProxyOptions::CompositeGuardWhy(d33, "D33 session: test", writes, marker, sr,
		                                                      refused ? "composite X REFUSED: missing" : "");
		const bool want = d33 && writes && !marker && sr == 1 && refused;
		if (!w.empty()) ++crefusals;
		if (w.empty() == want) ++cbad;
	}
	Check("composite guard rule: of 32 combinations (D33, writes server.cfg, shell.helper, ServerRenderer 0/1, refused) "
	      "only D33 + writes + no switch + ServerRenderer 1 + refused refuses",
	      crefusals == 1 && cbad == 0);
	const std::string cw = ProxyOptions::CompositeGuardWhy(true, "D33 session: config mask 0x1", true, false, 1,
	                                                       "composite C:\\x\\shell_composite.dll REFUSED: shell_composite.dll is missing");
	Check("composite guard rule: the reason names ServerRenderer = 1, the session, the refusal and the partial install",
	      Has(cw, "ServerRenderer = 1 in a D33 session") && Has(cw, "config mask 0x1") &&
	          Has(cw, "REFUSED: shell_composite.dll is missing") && Has(cw, "partial or damaged install"),
	      cw);
}

// ---------------------------------------------------------------------------------------------- OptionsBoot + guard
const char kD33Why[] = "D33 session: config mask 0xFFEC3F67, rule bits 0x0003C090, rule keys off: test";

void TestBoot()
{
	// 1. created with the set: a D33 session, the file deleted, the payload's set present
	Case c = Prepare("created-with-set", true);
	std::string path = ProxyRtInt::OptionsBoot(c.x64, true, kD33Why);
	std::string g = ProxyRtInt::OptionsGuard(true, kD33Why, true);
	Check("created-with-set: the file is created; OptionsBoot returns its path", Exists(c.opt) && path == c.opt, path);
	Check("created-with-set: ServerRenderer 1 and the set's values in force (ServerUnits 1, HiddenEmbarks 1, "
	      "AiSkipVisualDrain 1, AiDrainUnordered 1, AiUpdateTickMs 0), each from the file",
	      Get(Vp64Opt::kServerRenderer) == 1 && Get(Vp64Opt::kServerUnits) == 1 && Get(Vp64Opt::kHiddenEmbarks) == 1 &&
	          Get(Vp64Opt::kAiSkipVisualDrain) == 1 && Get(Vp64Opt::kAiDrainUnordered) == 1 &&
	          Get(Vp64Opt::kAiUpdateTickMs) == 0 && FromFile(Vp64Opt::kServerRenderer) && FromFile(Vp64Opt::kServerUnits));
	Check("created-with-set: exactly one LOUD line, the re-creation, naming the set's file and its values",
	      CountWith("LOUD") == 1 && CountWith("re-created with the release's option set") == 1 &&
	          Has(LinesWith("re-created"), VP64_RELEASE_SET_FILE) && Has(LinesWith("re-created"), "ServerUnits = 1") &&
	          Has(LinesWith("re-created"), "ServerRenderer = 1"),
	      LinesWith("LOUD"));
	Check("created-with-set: the read line says 0 warnings; the guard lets the boot go on",
	      CountWith("): read, 0 warnings") == 1 && g.empty(), g);
	std::vector<ProxyOptions::SetRow> rows;
	std::string why, want;
	ProxyOptions::ParseSet(kSet, strlen(kSet), &rows, &why);
	ProxyOptions::TextWithSet(rows, true, &want, &why);
	Check("created-with-set: the file's bytes are TextWithSet's", ReadAll(c.opt) == want);
	// a second start reads the file it made: no LOUD line, no note
	ProxyRtInt::g_lines.clear();
	ProxyRtInt::OptionsBoot(c.x64, true, kD33Why);
	Check("created-with-set: the next start reads it: no LOUD line, no note, ServerRenderer 1",
	      CountWith("LOUD") == 0 && CountWith("note:") == 0 && Get(Vp64Opt::kServerRenderer) == 1, LinesWith("options:"));

	// 2. created without the set: refusal
	c = Prepare("created-without-set", false);
	ProxyRtInt::OptionsBoot(c.x64, true, kD33Why);
	g = ProxyRtInt::OptionsGuard(true, kD33Why, true);
	Check("created-without-set: the file is NOT created; one LOUD line says so and names the missing set",
	      !Exists(c.opt) && CountWith("LOUD") == 1 && Has(LinesWith("LOUD"), "is NOT created") &&
	          Has(LinesWith("LOUD"), "does not exist"),
	      LinesWith("LOUD"));
	Check("created-without-set: ServerRenderer 0 (the default) and the guard refuses, naming the missing set",
	      Get(Vp64Opt::kServerRenderer) == 0 && Has(g, "ServerRenderer = 0") && Has(g, "does not exist") &&
	          Has(g, "was not created"),
	      g);
	// 2b. a set that is there but unusable
	c = Prepare("created-bad-set", true, "Renderer\tServerUnits\t5\r\n");
	ProxyRtInt::OptionsBoot(c.x64, true, kD33Why);
	g = ProxyRtInt::OptionsGuard(true, kD33Why, true);
	Check("created-bad-set: not created; the guard refuses and names the set's fault (outside its range)",
	      !Exists(c.opt) && Has(g, "is not usable") && Has(g, "outside its range"), g);

	// 3. a stock session: as before 0624 (the table's defaults), the set never read
	c = Prepare("stock-created", true);
	ProxyRtInt::OptionsBoot(c.x64, false, "stock session: test");
	g = ProxyRtInt::OptionsGuard(false, "stock session: test", true);
	Check("stock-created (negative): the file is created with the default text byte for byte, ServerRenderer 0, no LOUD "
	      "line, no word of the set; the guard lets it go on",
	      ReadAll(c.opt) == DefaultText() && Get(Vp64Opt::kServerRenderer) == 0 && CountWith("LOUD") == 0 &&
	          CountWith("release") == 0 && g.empty(),
	      g + " " + LinesWith("options:"));

	// 4. an existing file that lacks keys of the set (an older release's file after an update)
	c = Prepare("existing-missing-keys", true);
	const std::string older = "[Renderer]\r\nServerRenderer = 1\r\nServerUnits = 1\r\n";
	WriteAll(c.opt, older);
	ProxyRtInt::OptionsBoot(c.x64, true, kD33Why);
	g = ProxyRtInt::OptionsGuard(true, kD33Why, true);
	const std::string note = LinesWith("note:");
	Check("existing-missing-keys: the file is not rewritten (bytes kept)", ReadAll(c.opt) == older);
	Check("existing-missing-keys: the missing keys take the table's defaults (HiddenEmbarks 0, AiSkipVisualDrain 0, "
	      "AiDrainUnordered 0)",
	      Get(Vp64Opt::kHiddenEmbarks) == 0 && Get(Vp64Opt::kAiSkipVisualDrain) == 0 && Get(Vp64Opt::kAiDrainUnordered) == 0);
	Check("existing-missing-keys: one note line names the 3 set keys whose default differs from the release's "
	      "(HiddenEmbarks, AiSkipVisualDrain, AiDrainUnordered), not ServerArc/ServerPath/AiUpdateTickMs (default = "
	      "release's) nor ServerUnits (set); no LOUD line; the guard lets it go on",
	      CountWith("note:") == 1 && Has(note, "does not set 3 option(s)") && Has(note, "HiddenEmbarks = 0 (the release's 1)") &&
	          !Has(note, "AiUpdateTickMs") && Has(note, "AiSkipVisualDrain = 0 (the release's 1)") &&
	          Has(note, "AiDrainUnordered") && !Has(note, "ServerArc") && !Has(note, "ServerPath") &&
	          !Has(note, "ServerUnits") && CountWith("LOUD") == 0 && g.empty(),
	      note + " / " + g);

	// 5. a hand-edited file: ServerRenderer = 0 set in a D33 session
	c = Prepare("hand-edited-0", true);
	const std::string hand = "[Renderer]\r\nServerRenderer = 0\r\n";
	WriteAll(c.opt, hand);
	ProxyRtInt::OptionsBoot(c.x64, true, kD33Why);
	g = ProxyRtInt::OptionsGuard(true, kD33Why, true);
	Check("hand-edited-0: the guard refuses (\"set in the file\", the session named); the file is kept as it is",
	      Has(g, "set in the file") && Has(g, "rule keys off: test") && ReadAll(c.opt) == hand, g);

	// 6. an existing file that does not set ServerRenderer at all (the proxy's own default text) in a D33 session
	c = Prepare("existing-default-text", true);
	WriteAll(c.opt, DefaultText());
	ProxyRtInt::OptionsBoot(c.x64, true, kD33Why);
	g = ProxyRtInt::OptionsGuard(true, kD33Why, true);
	Check("existing-default-text: the guard refuses (\"the table's default\")", Has(g, "the table's default"), g);

	// 7. the test hooks: one LOUD line each, never silent
	c = Prepare("hook-server-config", true);
	WriteAll(c.opt, hand);
	ProxyRtInt::OptionsBoot(c.x64, true, kD33Why);
	ProxyRtInt::g_lines.clear();
	g = ProxyRtInt::OptionsGuard(true, kD33Why, false);
	Check("hook server_config=: skipped, with ONE LOUD line naming the hook",
	      g.empty() && CountWith("LOUD") == 1 && Has(LinesWith("LOUD"), "TEST HOOK") && Has(LinesWith("LOUD"), "server_config="),
	      LinesWith("options:"));
	c = Prepare("hook-shell-helper", true);
	WriteAll(c.opt, hand);
	CreateDirectoryA((c.dir + "\\crashlogs").c_str(), NULL);
	WriteAll(c.dir + "\\crashlogs\\shell.helper", "");
	ProxyRtInt::OptionsBoot(c.x64, true, kD33Why);
	ProxyRtInt::g_lines.clear();
	g = ProxyRtInt::OptionsGuard(true, kD33Why, true);
	Check("hook crashlogs\\shell.helper: skipped, with ONE LOUD line naming the hook",
	      g.empty() && CountWith("LOUD") == 1 && Has(LinesWith("LOUD"), "crashlogs\\shell.helper"), LinesWith("options:"));
	DeleteFileA((c.dir + "\\crashlogs\\shell.helper").c_str());
	ProxyRtInt::g_lines.clear();
	g = ProxyRtInt::OptionsGuard(true, kD33Why, true);
	Check("hook crashlogs\\shell.helper (negative): the switch removed, the same session is refused", !g.empty(), g);

	// 7b. ticket 0624 item 5: the composite guard through ProxyRtInt::CompositeGuard (ServerRenderer = 1 in force)
	const char kRefused[] = "composite C:\\p\\shell_composite.dll REFUSED: shell_composite.dll is missing";
	c = Prepare("composite-guard", true);
	WriteAll(c.opt, "[Renderer]\r\nServerRenderer = 1\r\n");
	ProxyRtInt::OptionsBoot(c.x64, true, kD33Why);
	ProxyRtInt::g_lines.clear();
	g = ProxyRtInt::CompositeGuard(true, kD33Why, true, kRefused);
	Check("composite guard: D33, ServerRenderer 1, refused: refuses with the refusal in the reason, no line of its own",
	      Has(g, "REFUSED: shell_composite.dll is missing") && ProxyRtInt::g_lines.empty(), g);
	g = ProxyRtInt::CompositeGuard(true, kD33Why, true, "");
	Check("composite guard (negative): an accepted composite (\"\") goes on, no line", g.empty() && ProxyRtInt::g_lines.empty(), g);
	g = ProxyRtInt::CompositeGuard(true, kD33Why, true, NULL);
	Check("composite guard (negative): NULL as not refused", g.empty() && ProxyRtInt::g_lines.empty(), g);
	g = ProxyRtInt::CompositeGuard(false, "stock session: test", true, kRefused);
	Check("composite guard (negative): a refused composite in a stock session goes on as before 0624, no line",
	      g.empty() && ProxyRtInt::g_lines.empty(), g);
	g = ProxyRtInt::CompositeGuard(true, kD33Why, false, kRefused);
	Check("composite guard hook server_config=: skipped, with ONE LOUD TEST HOOK line naming the refusal",
	      g.empty() && CountWith("LOUD") == 1 && Has(LinesWith("LOUD"), "TEST HOOK: the composite guard was skipped") &&
	          Has(LinesWith("LOUD"), "server_config=") && Has(LinesWith("LOUD"), "is missing"),
	      LinesWith("options:"));
	ProxyRtInt::g_lines.clear();
	CreateDirectoryA((c.dir + "\\crashlogs").c_str(), NULL);
	WriteAll(c.dir + "\\crashlogs\\shell.helper", "");
	g = ProxyRtInt::CompositeGuard(true, kD33Why, true, kRefused);
	Check("composite guard hook crashlogs\\shell.helper: skipped, with ONE LOUD TEST HOOK line",
	      g.empty() && CountWith("LOUD") == 1 && Has(LinesWith("LOUD"), "crashlogs\\shell.helper"), LinesWith("options:"));
	DeleteFileA((c.dir + "\\crashlogs\\shell.helper").c_str());
	WriteAll(c.opt, "[Renderer]\r\nServerRenderer = 0\r\n");
	ProxyRtInt::OptionsBoot(c.x64, true, kD33Why);
	ProxyRtInt::g_lines.clear();
	g = ProxyRtInt::CompositeGuard(true, kD33Why, true, kRefused);
	Check("composite guard (negative): with ServerRenderer = 0 it is not its case (the renderer guard's), no line",
	      g.empty() && ProxyRtInt::g_lines.empty(), g);

	// 8. a stock session with ServerRenderer = 0 (the thin client, increment 1): nothing
	c = Prepare("stock-0", true);
	WriteAll(c.opt, hand);
	ProxyRtInt::OptionsBoot(c.x64, false, "stock session: test");
	ProxyRtInt::g_lines.clear();
	g = ProxyRtInt::OptionsGuard(false, "stock session: test", true);
	Check("stock-0 (negative): a stock session with ServerRenderer = 0 goes on, no line", g.empty() && ProxyRtInt::g_lines.empty(), g);

	// 9. a race: the file appears between the check and the write (CREATE_NEW): kept, read
	c = Prepare("create-exists", true);
	WriteAll(c.opt, "x");
	unsigned long err = 0;
	Check("CreateWithText: an existing file is never replaced (1, bytes kept)",
	      ProxyOptions::CreateWithText(W(c.opt).c_str(), "new", &err) == 1 && ReadAll(c.opt) == "x");
	Check("CreateWithText: a missing folder fails (2, an error code)",
	      ProxyOptions::CreateWithText(W(c.dir + "\\no-such-folder\\VP64Options.ini").c_str(), "new", &err) == 2 && err != 0);
	// 10. a set file over 64 KB is refused, a folder in its place cannot be read
	c = Prepare("set-too-large", true, "");
	WriteAll(c.x64 + "\\" VP64_RELEASE_SET_FILE, std::string(70000, '#'));
	std::string sp;
	Check("ReadSet: a set over 64 KB: -1, \"larger than 64 KB\"",
	      ProxyOptions::ReadSet(c.x64, &rows, &sp, &why) == -1 && Has(why, "64 KB"), why);
	Check("ReadSet: no file: 0, \"no such file\"", ProxyOptions::ReadSet(c.dir + "\\none", &rows, &sp, &why) == 0, why);
}
#endif
}  // namespace

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		printf("usage: options_boot_test <scratch dir> [-v] [--expect-fail]\n");
		return 2;
	}
	g_root = argv[1];
	for (int i = 2; i < argc; ++i)
	{
		if (!strcmp(argv[i], "-v")) ProxyRtInt::g_verbose = true;
		if (!strcmp(argv[i], "--expect-fail")) g_expectFail = true;
	}
	CreateDirectoryA(g_root.c_str(), NULL);
#ifdef VP_OPTIONS_BOOT_OLD
	Old();
#else
	TestParseSet();
	TestTextWithSet();
	TestGuardWhy();
	TestBoot();
	if (g_expectFail) Check("the deliberate failure of --expect-fail (FAIL line)", false, "expected");
#endif
	printf("options_boot_test: %d/%d checks passed\n", g_pass, g_total);
	return g_pass == g_total ? 0 : 1;
}
