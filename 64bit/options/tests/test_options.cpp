// test_options.cpp - unit tests of the options file loader (one-off 9042). Built in both trees (x86 VC9 and x64 MSVC 14)
// by options/CMakeLists.txt; run by `python ipc/build.py --test`, and under AddressSanitizer by options/test_asan.py.
//
//   test_options <docs/OPTIONS.md> <scratch folder>
//
// Every validation and error path has its negative case, checking the values AND the warnings. Files are written only
// under <scratch folder> (created, emptied of what this test wrote at the end). Nothing reads or writes the real
// Documents folder: DefaultPath is checked for its shape only. Exit 0 when every check passes, 1 otherwise, 2 on usage.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "vp64_options.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

// Ticket 4940: the synthetic rows below leave Def's trailing valueCategory out (zero: no category per value), as the table
// does for every row but RoadLayout; clang-cl's -Wextra would call that a missing initializer.
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif

using namespace Vp64Opt;

namespace
{
int g_fail = 0, g_pass = 0;

void Check(bool ok, const char* name, int line)
{
	if (ok)
		++g_pass;
	else
	{
		++g_fail;
		printf("FAIL line %d: %s\n", line, name);
	}
}
#define CHECK(x) Check((x), #x, __LINE__)

// Collects warnings into one buffer, one per line.
struct Sink
{
	char text[8192];
	size_t len;
	unsigned n;
	Sink() : len(0), n(0) { text[0] = 0; }
	bool Has(const char* s) const { return strstr(text, s) != NULL; }
};
void Collect(void* ctx, const char* line)
{
	Sink* s = (Sink*)ctx;
	++s->n;
	const size_t l = strlen(line);
	if (s->len + l + 2 < sizeof(s->text))
	{
		memcpy(s->text + s->len, line, l);
		s->len += l;
		s->text[s->len++] = '\n';
		s->text[s->len] = 0;
	}
}

unsigned P(const char* text, Options& o, Sink& s) { return Parse(text, strlen(text), o, Collect, &s); }

const int kDef = -1;  // BakedTileCameraRadius default (D88)

void TestDefaults()
{
	Options o;
	CHECK(o.BakedTileCameraRadius() == kDef);
	CHECK(o.fromFile[kBakedTileCameraRadius] == 0);
	CHECK(Definition(kBakedTileCameraRadius).defaultValue == -1);
	CHECK(Definition(kBakedTileCameraRadius).minValue == -1);
	CHECK(Definition(kBakedTileCameraRadius).maxValue == 128);
	CHECK(strcmp(Definition(kBakedTileCameraRadius).section, "Renderer") == 0);
	CHECK(FindKey("bakedtilecameraradius", 21) == kBakedTileCameraRadius);
	CHECK(FindKey("BakedTileCameraRadiu", 20) == kCount);
	CHECK(FindKey("", 0) == kCount);
	Sink s;
	CHECK(P("", o, s) == 0 && o.BakedTileCameraRadius() == kDef && s.n == 0);
	CHECK(Parse(NULL, 5, o, Collect, &s) == 0 && o.BakedTileCameraRadius() == kDef);
}

void TestValid()
{
	Options o;
	Sink s;
	CHECK(P("[Renderer]\nBakedTileCameraRadius = 5\n", o, s) == 0 && o.BakedTileCameraRadius() == 5 &&
	      o.fromFile[kBakedTileCameraRadius] == 1);
	Sink s2;  // case, spacing, tabs, CRLF, no final newline, BOM
	CHECK(P("\xEF\xBB\xBF  [ renderer ]  \r\n\tbakedTILEcameraRADIUS\t=\t-1\t", o, s2) == 0 &&
	      o.BakedTileCameraRadius() == -1 && o.fromFile[0] == 1);
	Sink s3;  // CR-only line ends, comments of both kinds, blank lines
	CHECK(P("; c\r# c\r\r[Renderer]\rBakedTileCameraRadius=128\r", o, s3) == 0 && o.BakedTileCameraRadius() == 128);
	Sink s4;  // inline comments, quotes, sign, leading zeros
	CHECK(P("[Renderer]\nBakedTileCameraRadius = 7 ; less memory\n", o, s4) == 0 && o.BakedTileCameraRadius() == 7);
	CHECK(P("[Renderer]\nBakedTileCameraRadius = 7\t# x\n", o, s4) == 0 && o.BakedTileCameraRadius() == 7);
	CHECK(P("[Renderer]\nBakedTileCameraRadius = \"12\"\n", o, s4) == 0 && o.BakedTileCameraRadius() == 12);
	CHECK(P("[Renderer]\nBakedTileCameraRadius = +0\n", o, s4) == 0 && o.BakedTileCameraRadius() == 0);
	CHECK(P("[Renderer]\nBakedTileCameraRadius = 0005\n", o, s4) == 0 && o.BakedTileCameraRadius() == 5);
	CHECK(s4.n == 0);
	// a parse starts from the defaults: an earlier parse's value does not leak
	CHECK(P("", o, s4) == 0 && o.BakedTileCameraRadius() == kDef && o.fromFile[0] == 0);
}

void TestBadValues()
{
	const char* bad[] = {"abc", "5.0", "0x10", "-2", "129", "999999999", "9999999999", "-", "+", "5 5", "1e3", "\"\"",
	                     "five", "--1", "5abc"};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		char text[128];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Renderer]\nBakedTileCameraRadius = %s\n", bad[i]);
		Options o;
		Sink s;
		const unsigned n = P(text, o, s);
		const bool ok = n == 1 && s.n == 1 && o.BakedTileCameraRadius() == kDef && o.fromFile[0] == 0;
		if (!ok) printf("  bad value '%s': %u warnings, value %d: %s", bad[i], n, o.BakedTileCameraRadius(), s.text);
		CHECK(ok);
	}
	Options o;
	Sink s;
	CHECK(P("[Renderer]\nBakedTileCameraRadius = abc\n", o, s) == 1);
	CHECK(s.Has("line 2: BakedTileCameraRadius = abc: not a whole number from -1 to 128; the default -1 is used"));
	Sink s2;  // empty value, and a value that is only a comment
	CHECK(P("[Renderer]\nBakedTileCameraRadius =\n", o, s2) == 1 && o.BakedTileCameraRadius() == kDef);
	CHECK(s2.Has("line 2: BakedTileCameraRadius has no value; the default -1 is used"));
	Sink s3;
	CHECK(P("[Renderer]\nBakedTileCameraRadius = ; nothing\n", o, s3) == 1 && o.BakedTileCameraRadius() == kDef);
	// a valid value followed by an invalid one: the last line decides, and it says "default"
	Sink s4;
	CHECK(P("[Renderer]\nBakedTileCameraRadius = 5\nBakedTileCameraRadius = x\n", o, s4) == 2 &&
	      o.BakedTileCameraRadius() == kDef && o.fromFile[0] == 0);
	CHECK(s4.Has("line 3: BakedTileCameraRadius is set again (first on line 2); the last one counts"));
	// a ';' inside a value without a space before it is part of the value (and so invalid here)
	Sink s5;
	CHECK(P("[Renderer]\nBakedTileCameraRadius = 5;x\n", o, s5) == 1 && o.BakedTileCameraRadius() == kDef);
	// a long bad value is cut in the warning
	Sink s6;
	char text[600];
	strcpy_s(text, "[Renderer]\nBakedTileCameraRadius = ");
	for (int i = 0; i < 300; ++i) strcat_s(text, "z");
	CHECK(P(text, o, s6) == 1 && o.BakedTileCameraRadius() == kDef && s6.len < 300);
}

void TestStructure()
{
	Options o;
	Sink s;  // unknown key: ignored, other keys still read
	CHECK(P("[Renderer]\nFoo = 3\nBakedTileCameraRadius = 4\n", o, s) == 1 && o.BakedTileCameraRadius() == 4);
	CHECK(s.Has("line 2: unknown option 'Foo' (ignored)"));
	Sink s2;  // unknown section: warned, its known keys honoured with a second warning
	CHECK(P("[Graphics]\nBakedTileCameraRadius = 3\n", o, s2) == 2 && o.BakedTileCameraRadius() == 3);
	CHECK(s2.Has("line 1: [Graphics]: no option belongs to this section; its keys are still read"));
	CHECK(s2.Has("line 2: BakedTileCameraRadius belongs in [Renderer], not [Graphics]; it is read anyway"));
	Sink s3;  // before any section
	CHECK(P("BakedTileCameraRadius = 2\n", o, s3) == 1 && o.BakedTileCameraRadius() == 2);
	CHECK(s3.Has("line 1: BakedTileCameraRadius comes before any section; it belongs in [Renderer] and is read anyway"));
	Sink s4;  // duplicate valid: last wins
	CHECK(P("[Renderer]\nBakedTileCameraRadius = 2\nBakedTileCameraRadius = 9\n", o, s4) == 1 &&
	      o.BakedTileCameraRadius() == 9);
	Sink s5;  // garbage lines
	CHECK(P("[Renderer\nhello\n= 5\n[Renderer]\nBakedTileCameraRadius = 6\n", o, s5) == 3 &&
	      o.BakedTileCameraRadius() == 6);
	CHECK(s5.Has("line 1: '[Renderer': a section header must end with ']' (ignored)"));
	CHECK(s5.Has("line 2: 'hello': not a comment, a [section] or key = value (ignored)"));
	CHECK(s5.Has("line 3: '= 5': no key before '=' (ignored)"));
	Sink s6;  // an empty section header
	CHECK(P("[]\n", o, s6) == 1);
	Sink s7;  // a NUL byte in a line: no crash, the line is not a known key
	const char nul[] = "[Renderer]\nBaked\0TileCameraRadius = 3\n";
	CHECK(Parse(nul, sizeof(nul) - 1, o, Collect, &s7) == 1 && o.BakedTileCameraRadius() == kDef);
	// NULL callback: still counted
	CHECK(Parse("x\ny\n", 4, o, NULL, NULL) == 2);
	// a very long section name is cut, not overflowed
	Sink s8;
	char text[400] = "[";
	for (int i = 0; i < 200; ++i) strcat_s(text, "S");
	strcat_s(text, "]\nBakedTileCameraRadius = 1\n");
	CHECK(P(text, o, s8) == 2 && o.BakedTileCameraRadius() == 1);
}

void TestDefaultText()
{
	const size_t n = DefaultFileText(NULL, 0);
	// < 30000: was < 8000 until ticket 4931's [Extras] block (each later extra adds about 0.4 KB), < 4000 until ticket
	// 4853's ServerFarmFields took the text past 4 KB. Every buffer of the default text in this file is 32768 (0185/4853:
	// a full buffer ended the process in strcpy_s).
	CHECK(n > 200 && n < 30000);
	printf("  default file text: %u bytes\n", (unsigned)n);
	static char buf[32768];   // the default file text of every option (it grows with each option)
	CHECK(DefaultFileText(buf, sizeof(buf)) == n && strlen(buf) == n);
	// round trip: the default file parses with no warning and gives the defaults (the option lines are commented out)
	Options o;
	Sink s;
	CHECK(Parse(buf, n, o, Collect, &s) == 0 && o.BakedTileCameraRadius() == kDef && o.fromFile[0] == 0);
	CHECK(strstr(buf, "[Renderer]\r\n") != NULL);
	CHECK(strstr(buf, "\r\n; BakedTileCameraRadius = -1\r\n") != NULL);
	// uncommenting the option line gives a file that sets it, with no warning
	static char edited[32768];   // 8192 until ticket 4931; 4096 until ticket 4853 (the default text passed 4 KB: strcpy_s
	                             // ended the process)
	strcpy_s(edited, buf);
	char* p = strstr(edited, "; BakedTileCameraRadius = -1");
	CHECK(p != NULL);
	if (p)
	{
		memmove(p, p + 2, strlen(p + 2) + 1);
		p[strlen("BakedTileCameraRadius = ")] = '5';
		p[strlen("BakedTileCameraRadius = ") + 1] = ' ';
		Sink s2;
		CHECK(Parse(edited, strlen(edited), o, Collect, &s2) == 0 && o.BakedTileCameraRadius() == 5);
	}
	// plain ASCII, CRLF only, no line over 100 characters
	bool ascii = true, crlf = true, shortLines = true;
	size_t lineStart = 0;
	for (size_t i = 0; i < n; ++i)
	{
		if ((unsigned char)buf[i] > 126 || ((unsigned char)buf[i] < 32 && buf[i] != '\r' && buf[i] != '\n')) ascii = false;
		if (buf[i] == '\n' && (i == 0 || buf[i - 1] != '\r')) crlf = false;
		if (buf[i] == '\n')
		{
			if (i - lineStart > 100) shortLines = false;
			lineStart = i + 1;
		}
	}
	CHECK(ascii);
	CHECK(crlf);
	CHECK(shortLines);
	// truncation: snprintf semantics
	char small[10];
	CHECK(DefaultFileText(small, sizeof(small)) == n && strlen(small) == 9);
	char one[1] = {'x'};
	CHECK(DefaultFileText(one, 1) == n && one[0] == 0);
}

// ---------------------------------------------------------------------------------------------------- files
wchar_t g_scratch[MAX_PATH];

void PathIn(wchar_t* out, const wchar_t* name)
{
	_snwprintf_s(out, MAX_PATH, _TRUNCATE, L"%s\\%s", g_scratch, name);
}

bool WriteBytes(const wchar_t* path, const void* data, DWORD n)
{
	HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return false;
	DWORD put = 0;
	const bool ok = WriteFile(h, data, n, &put, NULL) && put == n;
	CloseHandle(h);
	return ok;
}

DWORD ReadBytes(const wchar_t* path, char* buf, DWORD cap)
{
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	DWORD got = 0;
	ReadFile(h, buf, cap, &got, NULL);
	CloseHandle(h);
	return got;
}

void TestFiles()
{
	wchar_t path[MAX_PATH];
	Options o;
	unsigned nw = 99;
	// missing: defaults, no warning
	PathIn(path, L"missing.ini");
	DeleteFileW(path);
	Sink s;
	o.value[0] = 77;
	CHECK(LoadFile(path, o, Collect, &s, &nw) == kMissing && nw == 0 && s.n == 0 && o.BakedTileCameraRadius() == kDef);
	wchar_t deep[MAX_PATH];
	PathIn(deep, L"no-such-folder\\VP64Options.ini");
	CHECK(LoadFile(deep, o, Collect, &s, NULL) == kMissing);
	// create: written once, then never overwritten
	PathIn(path, L"VP64Options.ini");
	DeleteFileW(path);
	unsigned long err = 1;
	CHECK(CreateDefaultFile(path, &err) == kCreated && err == 0);
	static char a[32768], b[32768];   // 8192 until ticket 4931's [Extras] block
	const DWORD an = ReadBytes(path, a, sizeof(a));
	CHECK(an == DefaultFileText(NULL, 0) && an > 0);
	b[0] = 0;
	DefaultFileText(b, sizeof(b));
	CHECK(memcmp(a, b, an) == 0);
	Sink s2;
	CHECK(LoadFile(path, o, Collect, &s2, &nw) == kLoaded && nw == 0 && o.BakedTileCameraRadius() == kDef);
	const char mine[] = "[Renderer]\r\nBakedTileCameraRadius = 5\r\n";
	CHECK(WriteBytes(path, mine, sizeof(mine) - 1));
	CHECK(CreateDefaultFile(path, &err) == kExists);
	CHECK(ReadBytes(path, a, sizeof(a)) == sizeof(mine) - 1 && memcmp(a, mine, sizeof(mine) - 1) == 0);
	CHECK(LoadFile(path, o, Collect, &s2, &nw) == kLoaded && nw == 0 && o.BakedTileCameraRadius() == 5 &&
	      o.fromFile[0] == 1);
	// create into a missing folder: fails, error code reported, nothing created
	err = 0;
	CHECK(CreateDefaultFile(deep, &err) == kCreateFailed && err == ERROR_PATH_NOT_FOUND);
	CHECK(GetFileAttributesW(deep) == INVALID_FILE_ATTRIBUTES);
	// warnings counted through LoadFile
	const char bad[] = "[Renderer]\nBakedTileCameraRadius = big\nX = 1\n";
	CHECK(WriteBytes(path, bad, sizeof(bad) - 1));
	Sink s3;
	CHECK(LoadFile(path, o, Collect, &s3, &nw) == kLoaded && nw == 2 && s3.n == 2 && o.BakedTileCameraRadius() == kDef);
	// UTF-16LE with BOM (Notepad "Unicode")
	const wchar_t wide[] = L"\xFEFF[Renderer]\r\nBakedTileCameraRadius = 3\r\n";
	CHECK(WriteBytes(path, wide, (DWORD)(wcslen(wide) * sizeof(wchar_t))));
	Sink s4;
	CHECK(LoadFile(path, o, Collect, &s4, &nw) == kLoaded && nw == 0 && o.BakedTileCameraRadius() == 3);
	// UTF-16LE BOM only (empty)
	CHECK(WriteBytes(path, "\xFF\xFE", 2));
	CHECK(LoadFile(path, o, Collect, &s4, &nw) == kLoaded && nw == 0 && o.BakedTileCameraRadius() == kDef);
	// UTF-16BE: refused, defaults, one warning
	const char be[] = "\xFE\xFF\0[\0R";
	CHECK(WriteBytes(path, be, sizeof(be) - 1));
	Sink s5;
	CHECK(LoadFile(path, o, Collect, &s5, &nw) == kUnreadable && nw == 1 && o.BakedTileCameraRadius() == kDef);
	CHECK(s5.Has("UTF-16 big-endian"));
	// too large: not read, defaults, one warning (a valid setting at the start must NOT be taken)
	static char big[kMaxFileBytes + 2];
	memset(big, ';', sizeof(big));
	memcpy(big, mine, sizeof(mine) - 1);
	big[sizeof(mine) - 1] = '\n';
	CHECK(WriteBytes(path, big, sizeof(big)));
	Sink s6;
	CHECK(LoadFile(path, o, Collect, &s6, &nw) == kTooLarge && nw == 1 && o.BakedTileCameraRadius() == kDef);
	// exactly the limit: read
	CHECK(WriteBytes(path, big, kMaxFileBytes));
	Sink s7;
	CHECK(LoadFile(path, o, Collect, &s7, &nw) == kLoaded && o.BakedTileCameraRadius() == 5);
	// empty file
	CHECK(WriteBytes(path, "", 0));
	CHECK(LoadFile(path, o, Collect, &s7, &nw) == kLoaded && nw == 0 && o.BakedTileCameraRadius() == kDef);
	// locked against reading (opened without sharing): unreadable, defaults, one warning
	CHECK(WriteBytes(path, mine, sizeof(mine) - 1));
	HANDLE lock = CreateFileW(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	CHECK(lock != INVALID_HANDLE_VALUE);
	Sink s8;
	CHECK(LoadFile(path, o, Collect, &s8, &nw) == kUnreadable && nw == 1 && o.BakedTileCameraRadius() == kDef);
	CHECK(s8.Has("cannot open the file: error 32"));
	if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
	// a folder in place of the file: unreadable
	wchar_t dir[MAX_PATH];
	PathIn(dir, L"a-folder.ini");
	CreateDirectoryW(dir, NULL);
	Sink s9;
	CHECK(LoadFile(dir, o, Collect, &s9, &nw) == kUnreadable && nw == 1);
	CHECK(CreateDefaultFile(dir, &err) != kCreated);
	RemoveDirectoryW(dir);
	// a non-ASCII path
	PathIn(path, L"\x0410\x0440\x0442 options.ini");
	DeleteFileW(path);
	CHECK(CreateDefaultFile(path, &err) == kCreated);
	CHECK(LoadFile(path, o, Collect, &s9, &nw) == kLoaded && nw == 0);
	char u8[MAX_PATH * 3];
	wchar_t back[MAX_PATH];
	CHECK(WideToUtf8(path, u8, sizeof(u8)) && Utf8ToWide(u8, back, MAX_PATH) && wcscmp(back, path) == 0);
	DeleteFileW(path);
	PathIn(path, L"VP64Options.ini");
	DeleteFileW(path);
}

void TestConversions()
{
	char u8[8];
	CHECK(WideToUtf8(L"abc", u8, sizeof(u8)) && strcmp(u8, "abc") == 0);
	CHECK(!WideToUtf8(L"abcdefghij", u8, sizeof(u8)) && u8[0] == 0);  // too small
	CHECK(!WideToUtf8(NULL, u8, sizeof(u8)) && !WideToUtf8(L"a", NULL, 4) && !WideToUtf8(L"a", u8, 0));
	wchar_t w[8];
	CHECK(Utf8ToWide("\xD0\x90", w, 8) && w[0] == 0x0410 && w[1] == 0);
	CHECK(!Utf8ToWide("\xC0\xC0", w, 8) && w[0] == 0);  // invalid UTF-8
	CHECK(!Utf8ToWide("abcdefghij", w, 8) && w[0] == 0);
}

void TestDefaultPath()
{
	wchar_t buf[1024];
	wchar_t saved[1024];
	const DWORD had = GetEnvironmentVariableW(VP64OPT_ENV_VAR, saved, 1024);
	// the environment variable wins
	SetEnvironmentVariableW(VP64OPT_ENV_VAR, L"X:\\somewhere\\opts.ini");
	CHECK(DefaultPath(buf, 1024) == kPathEnv && wcscmp(buf, L"X:\\somewhere\\opts.ini") == 0);
	// set but longer than the buffer: refused, not silently replaced by Documents
	wchar_t tiny[8];
	CHECK(DefaultPath(tiny, 8) == kPathNone && tiny[0] == 0);
	// empty: Documents (shape only; nothing is created or read there)
	SetEnvironmentVariableW(VP64OPT_ENV_VAR, L"");
	CHECK(DefaultPath(buf, 1024) == kPathDocuments);
	const wchar_t* tail = L"\\My Games\\Sid Meier's Civilization 5\\VP64Options.ini";
	const size_t bl = wcslen(buf), tl = wcslen(tail);
	CHECK(bl > tl && wcscmp(buf + bl - tl, tail) == 0);
	SetEnvironmentVariableW(VP64OPT_ENV_VAR, NULL);
	CHECK(DefaultPath(buf, 1024) == kPathDocuments);
	CHECK(DefaultPath(buf, 10) == kPathNone);  // buffer too small for the Documents path
	CHECK(DefaultPath(NULL, 10) == kPathNone && DefaultPath(buf, 0) == kPathNone);
	if (had && had < 1024) SetEnvironmentVariableW(VP64OPT_ENV_VAR, saved);
}

// Issue 3305: [Renderer] ServerRenderer, 0 (default) or 1; everything else gives the default with one warning.
void TestServerRenderer()
{
	Options o;
	CHECK(o.ServerRenderer() == 0 && o.fromFile[kServerRenderer] == 0);
	CHECK(Definition(kServerRenderer).defaultValue == 0 && Definition(kServerRenderer).minValue == 0 &&
	      Definition(kServerRenderer).maxValue == 1);
	CHECK(strcmp(Definition(kServerRenderer).section, "Renderer") == 0);
	CHECK(FindKey("serverrenderer", 14) == kServerRenderer && FindKey("ServerRendere", 13) == kCount);
	Sink s;
	CHECK(P("[Renderer]\nServerRenderer = 1\n", o, s) == 0 && o.ServerRenderer() == 1 &&
	      o.fromFile[kServerRenderer] == 1 && o.BakedTileCameraRadius() == kDef && o.fromFile[kBakedTileCameraRadius] == 0);
	Sink s2;
	CHECK(P("[Renderer]\nServerRenderer = 0\nBakedTileCameraRadius = 5\n", o, s2) == 0 && o.ServerRenderer() == 0 &&
	      o.fromFile[kServerRenderer] == 1 && o.BakedTileCameraRadius() == 5);
	const char* bad[] = {"2", "-1", "on", "yes", "1.0", ""};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		char text[128];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Renderer]\nServerRenderer = %s\n", bad[i]);
		Sink sb;
		const unsigned n = P(text, o, sb);
		const bool ok = n == 1 && o.ServerRenderer() == 0 && o.fromFile[kServerRenderer] == 0;
		if (!ok) printf("  ServerRenderer bad value '%s': %u warnings, value %d: %s", bad[i], n, o.ServerRenderer(), sb.text);
		CHECK(ok);
	}
	Sink s3;
	CHECK(P("[Renderer]\nServerRenderer = 2\n", o, s3) == 1);
	CHECK(s3.Has("line 2: ServerRenderer = 2: not a whole number from 0 to 1; the default 0 is used"));
	// the default file lists it commented out at 0 and parses back to the defaults
	static char buf[32768];   // the default file text of every option (it grows with each option)
	const size_t n = DefaultFileText(buf, sizeof(buf));
	CHECK(n < sizeof(buf) && strstr(buf, "\r\n; ServerRenderer = 0\r\n") != NULL);
	Sink s4;
	CHECK(Parse(buf, n, o, Collect, &s4) == 0 && o.ServerRenderer() == 0 && o.fromFile[kServerRenderer] == 0);
}

// Issue 0172: [Renderer] WhiteFogMask, 0 or 1; everything else gives the default with one warning. The default is 1
// (on) since D136 (issue 0185); 0 still turns it off.
void TestWhiteFogMask()
{
	Options o;
	CHECK(o.WhiteFogMask() == 1 && o.fromFile[kWhiteFogMask] == 0);
	CHECK(Definition(kWhiteFogMask).defaultValue == 1 && Definition(kWhiteFogMask).minValue == 0 &&
	      Definition(kWhiteFogMask).maxValue == 1);
	CHECK(strcmp(Definition(kWhiteFogMask).section, "Renderer") == 0);
	CHECK(FindKey("whitefogmask", 12) == kWhiteFogMask && FindKey("WhiteFogMas", 11) == kCount);
	Sink s;
	CHECK(P("[Renderer]\nWhiteFogMask = 0\n", o, s) == 0 && o.WhiteFogMask() == 0 && o.fromFile[kWhiteFogMask] == 1 &&
	      o.ServerRenderer() == 0 && o.fromFile[kServerRenderer] == 0);
	Sink s1;
	CHECK(P("[Renderer]\nWhiteFogMask = 1\n", o, s1) == 0 && o.WhiteFogMask() == 1 && o.fromFile[kWhiteFogMask] == 1);
	Sink s2;
	CHECK(P("[Renderer]\nServerRenderer = 1\nWhiteFogMask = 0\n", o, s2) == 0 && o.WhiteFogMask() == 0 &&
	      o.fromFile[kWhiteFogMask] == 1 && o.ServerRenderer() == 1);
	// a file without the key, or with only another key, keeps it on
	Sink s5;
	CHECK(P("[Renderer]\nServerRenderer = 1\n", o, s5) == 0 && o.WhiteFogMask() == 1 && o.fromFile[kWhiteFogMask] == 0);
	Sink s6;
	CHECK(P("", o, s6) == 0 && o.WhiteFogMask() == 1 && o.fromFile[kWhiteFogMask] == 0);
	const char* bad[] = {"2", "-1", "on", "off", "yes", "1.0", ""};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		char text[128];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Renderer]\nWhiteFogMask = %s\n", bad[i]);
		Sink sb;
		const unsigned n = P(text, o, sb);
		const bool ok = n == 1 && o.WhiteFogMask() == 1 && o.fromFile[kWhiteFogMask] == 0;
		if (!ok) printf("  WhiteFogMask bad value '%s': %u warnings, value %d: %s", bad[i], n, o.WhiteFogMask(), sb.text);
		CHECK(ok);
	}
	Sink s3;
	CHECK(P("[Renderer]\nWhiteFogMask = 2\n", o, s3) == 1);
	CHECK(s3.Has("line 2: WhiteFogMask = 2: not a whole number from 0 to 1; the default 1 is used"));
	static char buf[32768];   // the default file text of every option (it grows with each option)
	const size_t n = DefaultFileText(buf, sizeof(buf));
	CHECK(n < sizeof(buf) && strstr(buf, "\r\n; WhiteFogMask = 1\r\n") != NULL && strstr(buf, "1 = on (default)") != NULL);
	Sink s4;
	CHECK(Parse(buf, n, o, Collect, &s4) == 0 && o.WhiteFogMask() == 1 && o.fromFile[kWhiteFogMask] == 0);
}

// Ticket 0487 made [Renderer] UiDrawListGrowth (id 3, default 1); the owner, in person, one-off 9081, 2026-11-22, removed it
// as an option (the proxy always grows the UI draw buffers): id 3 is a reserved row, and a player's line for the old key
// is an unknown option (one warning, ignored).
void TestUiDrawListGrowth()
{
	const Def& d = Definition(kReserved3);
	CHECK(d.key[0] == 0 && d.title == NULL && d.category == kCatNone && d.needsKey == NULL &&
	      strcmp(d.section, "Renderer") == 0);
	CHECK(FindKey("uidrawlistgrowth", 16) == kCount && FindKey("UiDrawListGrowth", 16) == kCount);
	Options o;
	Sink s;
	CHECK(P("[Renderer]\nUiDrawListGrowth = 0\nWhiteFogMask = 0\n", o, s) == 1 && o.WhiteFogMask() == 0);
	CHECK(s.Has("line 2: unknown option 'UiDrawListGrowth' (ignored)"));
	for (int i = 0; i < kCount; ++i) CHECK(o.value[i] == Definition((Id)i).defaultValue || i == kWhiteFogMask);
	static char buf[32768];
	const size_t n = DefaultFileText(buf, sizeof(buf));
	CHECK(n < sizeof(buf) && strstr(buf, "UiDrawListGrowth") == NULL);
	Sink s4;
	CHECK(Parse(buf, n, o, Collect, &s4) == 0);
}

// Issue 4759: [Renderer] ServerUnits, 0 (default, epic 0040's O1 (a)) or 1; everything else gives the default with one
// warning.
void TestServerUnits()
{
	Options o;
	CHECK(o.ServerUnits() == 0 && o.fromFile[kServerUnits] == 0);
	CHECK(Definition(kServerUnits).defaultValue == 0 && Definition(kServerUnits).minValue == 0 &&
	      Definition(kServerUnits).maxValue == 1);
	CHECK(strcmp(Definition(kServerUnits).section, "Renderer") == 0);
	CHECK(FindKey("serverunits", 11) == kServerUnits && FindKey("ServerUnit", 10) == kCount);
	Sink s;
	CHECK(P("[Renderer]\nServerUnits = 1\n", o, s) == 0 && o.ServerUnits() == 1 && o.fromFile[kServerUnits] == 1 &&
	      o.ServerRenderer() == 0 && o.fromFile[kServerRenderer] == 0);
	Sink s1;
	CHECK(P("[Renderer]\nServerRenderer = 1\nServerUnits = 0\n", o, s1) == 0 && o.ServerUnits() == 0 &&
	      o.fromFile[kServerUnits] == 1 && o.ServerRenderer() == 1);
	Sink s2;  // a file without the key keeps the default
	CHECK(P("[Renderer]\nServerRenderer = 1\n", o, s2) == 0 && o.ServerUnits() == 0 && o.fromFile[kServerUnits] == 0);
	const char* bad[] = {"2", "-1", "on", "off", "yes", "1.0", ""};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		char text[128];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Renderer]\nServerUnits = %s\n", bad[i]);
		Sink sb;
		const unsigned n = P(text, o, sb);
		const bool ok = n == 1 && o.ServerUnits() == 0 && o.fromFile[kServerUnits] == 0;
		if (!ok) printf("  ServerUnits bad value '%s': %u warnings, value %d: %s", bad[i], n, o.ServerUnits(), sb.text);
		CHECK(ok);
	}
	Sink s3;
	CHECK(P("[Renderer]\nServerUnits = 2\n", o, s3) == 1);
	CHECK(s3.Has("line 2: ServerUnits = 2: not a whole number from 0 to 1; the default 0 is used"));
	static char buf[32768];   // the default file text of every option (it grows with each option)
	const size_t n = DefaultFileText(buf, sizeof(buf));
	CHECK(n < sizeof(buf) && strstr(buf, "\r\n; ServerUnits = 0\r\n") != NULL &&
	      strstr(buf, "0 = off (default): the game draws the units itself.") != NULL);
	Sink s4;
	CHECK(Parse(buf, n, o, Collect, &s4) == 0 && o.ServerUnits() == 0 && o.fromFile[kServerUnits] == 0);
}

// Ticket 0199 part 2: [Renderer] ServerArc, 0 (default until the live checks pass) or 1; everything else gives the
// default with one warning.
void TestServerArc()
{
	Options o;
	CHECK(o.ServerArc() == 0 && o.fromFile[kServerArc] == 0);
	CHECK(Definition(kServerArc).defaultValue == 0 && Definition(kServerArc).minValue == 0 &&
	      Definition(kServerArc).maxValue == 1);
	CHECK(strcmp(Definition(kServerArc).section, "Renderer") == 0);
	CHECK(FindKey("serverarc", 9) == kServerArc && FindKey("ServerArcs", 10) == kCount);
	Sink s;
	CHECK(P("[Renderer]\nServerArc = 1\n", o, s) == 0 && o.ServerArc() == 1 && o.fromFile[kServerArc] == 1 &&
	      o.ServerRenderer() == 0 && o.ServerUnits() == 0);
	Sink s1;
	CHECK(P("[Renderer]\nServerRenderer = 1\nServerArc = 0\n", o, s1) == 0 && o.ServerArc() == 0 &&
	      o.fromFile[kServerArc] == 1 && o.ServerRenderer() == 1);
	Sink s2;  // a file without the key keeps the default
	CHECK(P("[Renderer]\nServerRenderer = 1\n", o, s2) == 0 && o.ServerArc() == 0 && o.fromFile[kServerArc] == 0);
	const char* bad[] = {"2", "-1", "on", "off", "yes", "1.0", ""};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		char text[128];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Renderer]\nServerArc = %s\n", bad[i]);
		Sink sb;
		const unsigned n = P(text, o, sb);
		const bool ok = n == 1 && o.ServerArc() == 0 && o.fromFile[kServerArc] == 0;
		if (!ok) printf("  ServerArc bad value '%s': %u warnings, value %d: %s", bad[i], n, o.ServerArc(), sb.text);
		CHECK(ok);
	}
	Sink s3;
	CHECK(P("[Renderer]\nServerArc = 2\n", o, s3) == 1);
	CHECK(s3.Has("line 2: ServerArc = 2: not a whole number from 0 to 1; the default 0 is used"));
	static char buf[32768];
	const size_t n = DefaultFileText(buf, sizeof(buf));
	CHECK(n < sizeof(buf) && strstr(buf, "\r\n; ServerArc = 0\r\n") != NULL &&
	      strstr(buf, "0 = off (default): the game draws the arc itself.") != NULL);
	Sink s4;
	CHECK(Parse(buf, n, o, Collect, &s4) == 0 && o.ServerArc() == 0 && o.fromFile[kServerArc] == 0);
}

// Ticket 0300 part 2: [Renderer] ServerPath, 0 (default until the live checks pass) or 1; everything else gives the
// default with one warning; it is independent of ServerArc.
void TestServerPath()
{
	Options o;
	CHECK(o.ServerPath() == 0 && o.fromFile[kServerPath] == 0);
	CHECK(Definition(kServerPath).defaultValue == 0 && Definition(kServerPath).minValue == 0 &&
	      Definition(kServerPath).maxValue == 1);
	CHECK(strcmp(Definition(kServerPath).section, "Renderer") == 0);
	CHECK(FindKey("serverpath", 10) == kServerPath && FindKey("ServerPaths", 11) == kCount &&
	      FindKey("ServerPat", 9) == kCount);
	Sink s;
	CHECK(P("[Renderer]\nServerPath = 1\n", o, s) == 0 && o.ServerPath() == 1 && o.fromFile[kServerPath] == 1 &&
	      o.ServerRenderer() == 0 && o.ServerArc() == 0 && o.ServerUnits() == 0);
	Sink s1;
	CHECK(P("[Renderer]\nServerRenderer = 1\nServerArc = 1\nServerPath = 0\n", o, s1) == 0 && o.ServerPath() == 0 &&
	      o.fromFile[kServerPath] == 1 && o.ServerArc() == 1 && o.ServerRenderer() == 1);
	Sink s2;  // a file without the key keeps the default
	CHECK(P("[Renderer]\nServerRenderer = 1\n", o, s2) == 0 && o.ServerPath() == 0 && o.fromFile[kServerPath] == 0);
	const char* bad[] = {"2", "-1", "on", "off", "yes", "1.0", "", "0x1"};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		char text[128];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Renderer]\nServerPath = %s\n", bad[i]);
		Sink sb;
		const unsigned n = P(text, o, sb);
		const bool ok = n == 1 && o.ServerPath() == 0 && o.fromFile[kServerPath] == 0;
		if (!ok) printf("  ServerPath bad value '%s': %u warnings, value %d: %s", bad[i], n, o.ServerPath(), sb.text);
		CHECK(ok);
	}
	Sink s3;
	CHECK(P("[Renderer]\nServerPath = 2\n", o, s3) == 1);
	CHECK(s3.Has("line 2: ServerPath = 2: not a whole number from 0 to 1; the default 0 is used"));
	static char buf[32768];
	const size_t n = DefaultFileText(buf, sizeof(buf));
	CHECK(n < sizeof(buf) && strstr(buf, "\r\n; ServerPath = 0\r\n") != NULL &&
	      strstr(buf, "0 = off (default): the game draws the markers itself.") != NULL);
	Sink s4;
	CHECK(Parse(buf, n, o, Collect, &s4) == 0 && o.ServerPath() == 0 && o.fromFile[kServerPath] == 0);
}

// Ticket 4853 (epic 0528 D): [Renderer] ServerFarmFields, 0 (default) or 1; everything else gives the default with one
// warning; independent of the other server options.
void TestServerFarmFields()
{
	Options o;
	CHECK(o.ServerFarmFields() == 0 && o.fromFile[kServerFarmFields] == 0);
	CHECK(Definition(kServerFarmFields).defaultValue == 0 && Definition(kServerFarmFields).minValue == 0 &&
	      Definition(kServerFarmFields).maxValue == 1);
	CHECK(strcmp(Definition(kServerFarmFields).section, "Renderer") == 0);
	CHECK(FindKey("serverfarmfields", 16) == kServerFarmFields && FindKey("ServerFarmField", 15) == kCount &&
	      FindKey("ServerFarmFieldss", 17) == kCount);
	Sink s;
	CHECK(P("[Renderer]\nServerFarmFields = 1\n", o, s) == 0 && o.ServerFarmFields() == 1 &&
	      o.fromFile[kServerFarmFields] == 1 && o.ServerRenderer() == 0 && o.ServerPath() == 0 && o.ServerArc() == 0);
	Sink s1;
	CHECK(P("[Renderer]\nServerRenderer = 1\nServerPath = 1\nServerFarmFields = 0\n", o, s1) == 0 &&
	      o.ServerFarmFields() == 0 && o.fromFile[kServerFarmFields] == 1 && o.ServerPath() == 1 && o.ServerRenderer() == 1);
	Sink s2;  // a file without the key keeps the default
	CHECK(P("[Renderer]\nServerRenderer = 1\n", o, s2) == 0 && o.ServerFarmFields() == 0 && o.fromFile[kServerFarmFields] == 0);
	const char* bad[] = {"2", "-1", "on", "off", "yes", "1.0", "", "0x1"};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		char text[128];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Renderer]\nServerFarmFields = %s\n", bad[i]);
		Sink sb;
		const unsigned n = P(text, o, sb);
		const bool ok = n == 1 && o.ServerFarmFields() == 0 && o.fromFile[kServerFarmFields] == 0;
		if (!ok) printf("  ServerFarmFields bad value '%s': %u warnings, value %d: %s", bad[i], n, o.ServerFarmFields(), sb.text);
		CHECK(ok);
	}
	Sink s3;
	CHECK(P("[Renderer]\nServerFarmFields = 2\n", o, s3) == 1);
	CHECK(s3.Has("line 2: ServerFarmFields = 2: not a whole number from 0 to 1; the default 0 is used"));
	static char buf[32768];
	const size_t n = DefaultFileText(buf, sizeof(buf));
	CHECK(n < sizeof(buf) && strstr(buf, "\r\n; ServerFarmFields = 0\r\n") != NULL &&
	      strstr(buf, "0 = off (default): the game lays out the farm fields itself.") != NULL);
	Sink s4;
	CHECK(Parse(buf, n, o, Collect, &s4) == 0 && o.ServerFarmFields() == 0 && o.fromFile[kServerFarmFields] == 0);
}

// Ticket 4662 (D165 option 3): [Performance] AiUpdateTickMs, -1 (stock) or 0..100; everything else gives the default
// with one warning. Ticket 0566 (D191): the default is 0 (was -1); an explicit -1 still gives the stock pacing.
// AiSkipVisualDrain (option 2, ticket 4665) is its own key (TestAiSkipVisualDrain).
void TestAiUpdateTickMs()
{
	Options o;
	CHECK(o.AiUpdateTickMs() == 0 && o.fromFile[kAiUpdateTickMs] == 0);
	CHECK(Definition(kAiUpdateTickMs).defaultValue == 0 && Definition(kAiUpdateTickMs).minValue == -1 &&
	      Definition(kAiUpdateTickMs).maxValue == 100);
	CHECK(strcmp(Definition(kAiUpdateTickMs).section, "Performance") == 0);
	CHECK(FindKey("aiupdatetickms", 14) == kAiUpdateTickMs && FindKey("AiUpdateTickM", 13) == kCount);
	const char* good[] = {"-1", "0", "1", "50", "100", " 0 ", "\"0\"", "0 ; on"};
	const int goodValue[] = {-1, 0, 1, 50, 100, 0, 0, 0};
	for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); ++i)
	{
		char text[128];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Performance]\nAiUpdateTickMs = %s\n", good[i]);
		Sink sg;
		const unsigned n = P(text, o, sg);
		const bool ok = n == 0 && o.AiUpdateTickMs() == goodValue[i] && o.fromFile[kAiUpdateTickMs] == 1 &&
		                o.WhiteFogMask() == 1 && o.fromFile[kWhiteFogMask] == 0;
		if (!ok) printf("  AiUpdateTickMs good value '%s': %u warnings, value %d: %s", good[i], n, o.AiUpdateTickMs(), sg.text);
		CHECK(ok);
	}
	// beside the other sections: each keeps its own value
	Sink s;
	CHECK(P("[Renderer]\nWhiteFogMask = 0\n[Performance]\nAiUpdateTickMs = 0\n", o, s) == 0 && o.AiUpdateTickMs() == 0 &&
	      o.WhiteFogMask() == 0 && o.fromFile[kWhiteFogMask] == 1);
	// a file without the key takes the default 0 (ticket 0566; was the stock pacing -1)
	Sink s5;
	CHECK(P("[Renderer]\nServerRenderer = 1\n", o, s5) == 0 && o.AiUpdateTickMs() == 0 && o.fromFile[kAiUpdateTickMs] == 0);
	Sink s6;
	CHECK(P("[Performance]\n", o, s6) == 0 && o.AiUpdateTickMs() == 0 && o.fromFile[kAiUpdateTickMs] == 0);
	Sink s6e;
	CHECK(P("", o, s6e) == 0 && o.AiUpdateTickMs() == 0 && o.fromFile[kAiUpdateTickMs] == 0);
	// a commented line (as the default file of older builds wrote it) is no value: the new default 0
	Sink s6c;
	CHECK(P("[Performance]\n; AiUpdateTickMs = -1\n", o, s6c) == 0 && o.AiUpdateTickMs() == 0 &&
	      o.fromFile[kAiUpdateTickMs] == 0);
	// an explicit -1 still gives the stock pacing, from the file, with no warning (ticket 0566)
	Sink s6m;
	CHECK(P("[Performance]\nAiUpdateTickMs = -1\n", o, s6m) == 0 && o.AiUpdateTickMs() == -1 &&
	      o.fromFile[kAiUpdateTickMs] == 1);
	Sink s6n;
	CHECK(P("[Renderer]\nServerRenderer = 1\n[Performance]\nAiSkipVisualDrain = 2\nAiUpdateTickMs = -1\n", o, s6n) == 0 &&
	      o.AiUpdateTickMs() == -1 && o.fromFile[kAiUpdateTickMs] == 1 && o.AiSkipVisualDrain() == 2);
	// out of range and parse errors: the default 0, one warning each
	const char* bad[] = {"101", "-2", "1000", "2147483648", "on", "off", "1.5", "0x10", "", "-", "5ms"};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		char text[128];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Performance]\nAiUpdateTickMs = %s\n", bad[i]);
		Sink sb;
		const unsigned n = P(text, o, sb);
		const bool ok = n == 1 && o.AiUpdateTickMs() == 0 && o.fromFile[kAiUpdateTickMs] == 0;
		if (!ok) printf("  AiUpdateTickMs bad value '%s': %u warnings, value %d: %s", bad[i], n, o.AiUpdateTickMs(), sb.text);
		CHECK(ok);
	}
	Sink s3;
	CHECK(P("[Performance]\nAiUpdateTickMs = 101\n", o, s3) == 1 && o.AiUpdateTickMs() == 0);
	CHECK(s3.Has("line 2: AiUpdateTickMs = 101: not a whole number from -1 to 100; the default 0 is used"));
	Sink s3m;
	CHECK(P("[Performance]\nAiUpdateTickMs = -2\n", o, s3m) == 1 && o.AiUpdateTickMs() == 0 &&
	      s3m.Has("line 2: AiUpdateTickMs = -2: not a whole number from -1 to 100; the default 0 is used"));
	// an earlier valid value is not kept when the last line is bad (the default wins)
	Sink s7;
	CHECK(P("[Performance]\nAiUpdateTickMs = -1\nAiUpdateTickMs = 200\n", o, s7) == 2 && o.AiUpdateTickMs() == 0 &&
	      o.fromFile[kAiUpdateTickMs] == 0);
	// under the wrong section: honoured with a warning
	Sink s8;
	CHECK(P("[Renderer]\nAiUpdateTickMs = 10\n", o, s8) == 1 && o.AiUpdateTickMs() == 10);
	CHECK(s8.Has("line 2: AiUpdateTickMs belongs in [Performance], not [Renderer]; it is read anyway"));
	// [Performance] is a known section now; option 2's key (ticket 4665) is its own option and leaves this one alone
	Sink s9;
	CHECK(P("[Performance]\nAiSkipVisualDrain = 1\n", o, s9) == 0 && o.AiUpdateTickMs() == 0 &&
	      o.fromFile[kAiUpdateTickMs] == 0 && o.AiSkipVisualDrain() == 1);
	CHECK(!s9.Has("unknown option") && !s9.Has("no option belongs"));
	// the default file lists it commented out at 0 under [Performance] (ticket 0566; -1 before), with the help text
	// naming 0 as the default and -1 as the game's own pacing, and parses back to the defaults
	static char buf[32768];   // 8192 until ticket 4931
	const size_t n = DefaultFileText(buf, sizeof(buf));
	CHECK(n < sizeof(buf) && strstr(buf, "\r\n[Performance]\r\n") != NULL &&
	      strstr(buf, "\r\n; AiUpdateTickMs = 0\r\n") != NULL && strstr(buf, "AiUpdateTickMs = -1") == NULL);
	CHECK(strstr(buf, "only while the AI players move (default, highest performance)") != NULL &&   // the owner, 2026-11-22
	      strstr(buf, "-1 = as the game does it: every 100 ms at most") != NULL);
	Sink s4;
	CHECK(Parse(buf, n, o, Collect, &s4) == 0 && o.AiUpdateTickMs() == 0 && o.fromFile[kAiUpdateTickMs] == 0);
}

// Ticket 4665 (D165 option 2): [Performance] AiSkipVisualDrain, 0 (default, stock), 1 or (ticket 0571, D191) 2 = value
// 1 with the hold at AI gaps; everything else gives the default with one warning.
void TestAiSkipVisualDrain()
{
	Options o;
	CHECK(o.AiSkipVisualDrain() == 0 && o.fromFile[kAiSkipVisualDrain] == 0);
	CHECK(Definition(kAiSkipVisualDrain).defaultValue == 0 && Definition(kAiSkipVisualDrain).minValue == 0 &&
	      Definition(kAiSkipVisualDrain).maxValue == 2);
	CHECK(strcmp(Definition(kAiSkipVisualDrain).section, "Performance") == 0);
	CHECK(FindKey("aiskipvisualdrain", 17) == kAiSkipVisualDrain && FindKey("AiSkipVisualDrai", 16) == kCount);
	const char* good[] = {"0", "1", " 1 ", "\"1\"", "1 ; on", "1 # on", "2", " 2 ", "\"2\"", "2 ; hold"};
	const int goodValue[] = {0, 1, 1, 1, 1, 1, 2, 2, 2, 2};
	for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); ++i)
	{
		char text[128];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Performance]\nAiSkipVisualDrain = %s\n", good[i]);
		Sink sg;
		const unsigned n = P(text, o, sg);
		const bool ok = n == 0 && o.AiSkipVisualDrain() == goodValue[i] && o.fromFile[kAiSkipVisualDrain] == 1 &&
		                o.AiUpdateTickMs() == 0 && o.fromFile[kAiUpdateTickMs] == 0;
		if (!ok) printf("  AiSkipVisualDrain good value '%s': %u warnings, value %d: %s", good[i], n, o.AiSkipVisualDrain(), sg.text);
		CHECK(ok);
	}
	// both options of [Performance] together: each keeps its own value
	Sink s;
	CHECK(P("[Performance]\nAiUpdateTickMs = 0\nAiSkipVisualDrain = 1\n", o, s) == 0 && o.AiUpdateTickMs() == 0 &&
	      o.AiSkipVisualDrain() == 1 && o.fromFile[kAiUpdateTickMs] == 1 && o.fromFile[kAiSkipVisualDrain] == 1);
	// a file without the key keeps the stock pacing
	Sink s5;
	CHECK(P("[Performance]\nAiUpdateTickMs = 0\n", o, s5) == 0 && o.AiSkipVisualDrain() == 0 &&
	      o.fromFile[kAiSkipVisualDrain] == 0);
	// out of range and parse errors: the default, one warning each
	const char* bad[] = {"3", "-1", "10", "2147483648", "on", "off", "true", "yes", "1.0", "2.0", "0x2", "", "-", "1x", "2x"};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		char text[128];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Performance]\nAiSkipVisualDrain = %s\n", bad[i]);
		Sink sb;
		const unsigned n = P(text, o, sb);
		const bool ok = n == 1 && o.AiSkipVisualDrain() == 0 && o.fromFile[kAiSkipVisualDrain] == 0;
		if (!ok) printf("  AiSkipVisualDrain bad value '%s': %u warnings, value %d: %s", bad[i], n, o.AiSkipVisualDrain(), sb.text);
		CHECK(ok);
	}
	// ticket 0571: 3 is refused with the default and one warning naming the new range
	Sink s3;
	CHECK(P("[Performance]\nAiSkipVisualDrain = 3\n", o, s3) == 1 && o.AiSkipVisualDrain() == 0 &&
	      o.fromFile[kAiSkipVisualDrain] == 0);
	CHECK(s3.Has("line 2: AiSkipVisualDrain = 3: not a whole number from 0 to 2; the default 0 is used"));
	// value 2 with option 4 and option 3 at its default: each keeps its own value (the proxy decides what 2 needs)
	Sink s10;
	CHECK(P("[Performance]\nAiSkipVisualDrain = 2\nAiDrainUnordered = 1\n", o, s10) == 0 && o.AiSkipVisualDrain() == 2 &&
	      o.AiDrainUnordered() == 1 && o.AiUpdateTickMs() == 0 && o.fromFile[kAiUpdateTickMs] == 0);
	// an earlier 2 is not kept when the last line is bad
	Sink s11;
	CHECK(P("[Performance]\nAiSkipVisualDrain = 2\nAiSkipVisualDrain = 3\n", o, s11) == 2 && o.AiSkipVisualDrain() == 0 &&
	      o.fromFile[kAiSkipVisualDrain] == 0);
	// an earlier valid value is not kept when the last line is bad (the default wins)
	Sink s7;
	CHECK(P("[Performance]\nAiSkipVisualDrain = 1\nAiSkipVisualDrain = on\n", o, s7) == 2 && o.AiSkipVisualDrain() == 0 &&
	      o.fromFile[kAiSkipVisualDrain] == 0);
	CHECK(s7.Has("line 3: AiSkipVisualDrain is set again (first on line 2); the last one counts"));
	// under the wrong section: honoured with a warning
	Sink s8;
	CHECK(P("[Renderer]\nAiSkipVisualDrain = 1\n", o, s8) == 1 && o.AiSkipVisualDrain() == 1);
	CHECK(s8.Has("line 2: AiSkipVisualDrain belongs in [Performance], not [Renderer]; it is read anyway"));
	// the default file lists it commented out at 0 under [Performance], after AiUpdateTickMs, and parses back to defaults
	static char buf[32768];
	const size_t n = DefaultFileText(buf, sizeof(buf));
	const char* tick = strstr(buf, "\r\n; AiUpdateTickMs = 0\r\n");
	const char* skip = strstr(buf, "\r\n; AiSkipVisualDrain = 0\r\n");
	CHECK(n < sizeof(buf) && tick != NULL && skip != NULL && tick < skip);
	Sink s4;
	CHECK(Parse(buf, n, o, Collect, &s4) == 0 && o.AiSkipVisualDrain() == 0 && o.fromFile[kAiSkipVisualDrain] == 0);
}

// Ticket 4675 (D165 option 4): [Performance] AiDrainUnordered, 0 (default, stock) or 1; everything else gives the
// default with one warning. It does not change the other [Performance] keys (the proxy ties it to AiSkipVisualDrain).
void TestAiDrainUnordered()
{
	Options o;
	CHECK(o.AiDrainUnordered() == 0 && o.fromFile[kAiDrainUnordered] == 0);
	CHECK(kAiDrainUnordered == 10 && kCount == 41);   // 11 GameEventsOneCrossing (0504), 14 reserved, 12 and 13 the models' (4875),
	                                                  // 15 HiddenEmbarks (0573), 16-27 [Extras] (4931; 16 before it; 22 4938,
	                                                  // 23-27 epic 0455), 28 ServerCombat (4783)
	CHECK(Definition(kAiDrainUnordered).defaultValue == 0 && Definition(kAiDrainUnordered).minValue == 0 &&
	      Definition(kAiDrainUnordered).maxValue == 1);
	CHECK(strcmp(Definition(kAiDrainUnordered).section, "Performance") == 0);
	CHECK(FindKey("aidrainunordered", 16) == kAiDrainUnordered && FindKey("AiDrainUnordere", 15) == kCount);
	const char* good[] = {"0", "1", " 1 ", "\"1\"", "1 ; on", "1 # on"};
	const int goodValue[] = {0, 1, 1, 1, 1, 1};
	for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); ++i)
	{
		char text[128];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Performance]\nAiDrainUnordered = %s\n", good[i]);
		Sink sg;
		const unsigned n = P(text, o, sg);
		const bool ok = n == 0 && o.AiDrainUnordered() == goodValue[i] && o.fromFile[kAiDrainUnordered] == 1 &&
		                o.AiSkipVisualDrain() == 0 && o.fromFile[kAiSkipVisualDrain] == 0 && o.AiUpdateTickMs() == 0;
		if (!ok) printf("  AiDrainUnordered good value '%s': %u warnings, value %d: %s", good[i], n, o.AiDrainUnordered(), sg.text);
		CHECK(ok);
	}
	// the three options of [Performance] together: each keeps its own value
	Sink s;
	CHECK(P("[Performance]\nAiUpdateTickMs = 0\nAiSkipVisualDrain = 1\nAiDrainUnordered = 1\n", o, s) == 0 &&
	      o.AiUpdateTickMs() == 0 && o.AiSkipVisualDrain() == 1 && o.AiDrainUnordered() == 1 &&
	      o.fromFile[kAiDrainUnordered] == 1);
	// a file without the key keeps the stock order
	Sink s5;
	CHECK(P("[Performance]\nAiSkipVisualDrain = 1\n", o, s5) == 0 && o.AiDrainUnordered() == 0 &&
	      o.fromFile[kAiDrainUnordered] == 0);
	// out of range and parse errors: the default, one warning each
	const char* bad[] = {"2", "-1", "10", "2147483648", "on", "off", "true", "yes", "1.0", "0x1", "", "-", "1x"};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		char text[128];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Performance]\nAiDrainUnordered = %s\n", bad[i]);
		Sink sb;
		const unsigned n = P(text, o, sb);
		const bool ok = n == 1 && o.AiDrainUnordered() == 0 && o.fromFile[kAiDrainUnordered] == 0;
		if (!ok) printf("  AiDrainUnordered bad value '%s': %u warnings, value %d: %s", bad[i], n, o.AiDrainUnordered(), sb.text);
		CHECK(ok);
	}
	Sink s3;
	CHECK(P("[Performance]\nAiDrainUnordered = 2\n", o, s3) == 1);
	CHECK(s3.Has("line 2: AiDrainUnordered = 2: not a whole number from 0 to 1; the default 0 is used"));
	// an earlier valid value is not kept when the last line is bad (the default wins)
	Sink s7;
	CHECK(P("[Performance]\nAiDrainUnordered = 1\nAiDrainUnordered = on\n", o, s7) == 2 && o.AiDrainUnordered() == 0 &&
	      o.fromFile[kAiDrainUnordered] == 0);
	CHECK(s7.Has("line 3: AiDrainUnordered is set again (first on line 2); the last one counts"));
	// under the wrong section: honoured with a warning
	Sink s8;
	CHECK(P("[Renderer]\nAiDrainUnordered = 1\n", o, s8) == 1 && o.AiDrainUnordered() == 1);
	CHECK(s8.Has("line 2: AiDrainUnordered belongs in [Performance], not [Renderer]; it is read anyway"));
	// the default file lists it commented out at 0 under [Performance], after AiSkipVisualDrain, and parses back to defaults
	static char buf[32768];
	const size_t n = DefaultFileText(buf, sizeof(buf));
	const char* skip = strstr(buf, "\r\n; AiSkipVisualDrain = 0\r\n");
	const char* drain = strstr(buf, "\r\n; AiDrainUnordered = 0\r\n");
	CHECK(n < sizeof(buf) && skip != NULL && drain != NULL && skip < drain);
	Sink s4;
	CHECK(Parse(buf, n, o, Collect, &s4) == 0 && o.AiDrainUnordered() == 0 && o.fromFile[kAiDrainUnordered] == 0);
}

// Ticket 4875 (epic 0561): [Renderer] ServerCities (id 12) and ServerLandmarks (id 13), 0 (default) or 1, each on its
// own; everything else gives the default with one warning. Id 14 is reserved (another branch's): its empty key is never
// found, never written into the default file, and a file cannot set it.
void TestModelOption(Id id, const char* key, Id other)
{
	Options o;
	CHECK(o.Get(id) == 0 && o.fromFile[id] == 0);
	CHECK(Definition(id).defaultValue == 0 && Definition(id).minValue == 0 && Definition(id).maxValue == 1);
	CHECK(strcmp(Definition(id).section, "Renderer") == 0 && strcmp(Definition(id).key, key) == 0);
	const size_t kl = strlen(key);
	char lower[64];
	for (size_t i = 0; i <= kl; ++i) lower[i] = (char)((key[i] >= 'A' && key[i] <= 'Z') ? key[i] - 'A' + 'a' : key[i]);
	CHECK(FindKey(lower, kl) == id && FindKey(key, kl - 1) == kCount);
	char text[160];
	_snprintf_s(text, sizeof(text), _TRUNCATE, "[Renderer]\n%s = 1\n", key);
	Sink s;
	CHECK(P(text, o, s) == 0 && o.Get(id) == 1 && o.fromFile[id] == 1 && o.Get(other) == 0 && o.fromFile[other] == 0 &&
	      o.ServerRenderer() == 0 && o.ServerUnits() == 0);
	_snprintf_s(text, sizeof(text), _TRUNCATE, "[Renderer]\nServerRenderer = 1\n%s = 0\n", key);
	Sink s1;
	CHECK(P(text, o, s1) == 0 && o.Get(id) == 0 && o.fromFile[id] == 1 && o.ServerRenderer() == 1);
	Sink s2;  // a file without the key keeps the default
	CHECK(P("[Renderer]\nServerRenderer = 1\n", o, s2) == 0 && o.Get(id) == 0 && o.fromFile[id] == 0);
	const char* bad[] = {"2", "-1", "on", "off", "yes", "1.0", ""};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Renderer]\n%s = %s\n", key, bad[i]);
		Sink sb;
		const unsigned n = P(text, o, sb);
		const bool ok = n == 1 && o.Get(id) == 0 && o.fromFile[id] == 0;
		if (!ok) printf("  %s bad value '%s': %u warnings, value %d: %s", key, bad[i], n, o.Get(id), sb.text);
		CHECK(ok);
	}
	_snprintf_s(text, sizeof(text), _TRUNCATE, "[Renderer]\n%s = 2\n", key);
	Sink s3;
	CHECK(P(text, o, s3) == 1);
	char want[160];
	_snprintf_s(want, sizeof(want), _TRUNCATE, "line 2: %s = 2: not a whole number from 0 to 1; the default 0 is used", key);
	CHECK(s3.Has(want));
	static char buf[32768];
	const size_t n = DefaultFileText(buf, sizeof(buf));
	_snprintf_s(want, sizeof(want), _TRUNCATE, "\r\n; %s = 0\r\n", key);
	CHECK(n < sizeof(buf) && strstr(buf, want) != NULL);
	Sink s4;
	CHECK(Parse(buf, n, o, Collect, &s4) == 0 && o.Get(id) == 0 && o.fromFile[id] == 0);
}

void TestModelOptions()
{
	CHECK(kServerCities == 12 && kServerLandmarks == 13 && kGameEventsOneCrossing == 11 && kFixUnknownWonderBase == 14 &&
	      kCount == 41);   // 16 until 4931, 22 until 4938, 23 until 0455, 28 until 4783
	TestModelOption(kServerCities, "ServerCities", kServerLandmarks);
	TestModelOption(kServerLandmarks, "ServerLandmarks", kServerCities);
	Options o;
	CHECK(o.ServerCities() == 0 && o.ServerLandmarks() == 0);
	Sink s;
	CHECK(P("[Renderer]\nServerRenderer = 1\nServerCities = 1\nServerLandmarks = 1\n", o, s) == 0 && o.ServerCities() == 1 &&
	      o.ServerLandmarks() == 1 && o.ServerRenderer() == 1);
	// id 14 is no longer reserved (ticket 0613: [Extras] FixUnknownWonderBase); an empty key is still never found
	CHECK(strcmp(Definition(kFixUnknownWonderBase).key, "FixUnknownWonderBase") == 0 && FindKey("", 0) == kCount &&
	      o.fromFile[kFixUnknownWonderBase] == 0);
	static char buf[32768];
	const size_t n = DefaultFileText(buf, sizeof(buf));
	CHECK(n < sizeof(buf) && strstr(buf, "\r\n;  = ") == NULL && strstr(buf, "\r\n; = ") == NULL);
	// the order of the default file: by id (AiDrainUnordered, then the models' under a [Renderer] header of their own)
	const char* drain = strstr(buf, "\r\n; AiDrainUnordered = 0\r\n");
	const char* cities = strstr(buf, "\r\n; ServerCities = 0\r\n");
	const char* landmarks = strstr(buf, "\r\n; ServerLandmarks = 0\r\n");
	CHECK(drain && cities && landmarks && drain < cities && cities < landmarks);
	Sink s5;
	CHECK(P("[Performance]\n = 1\n", o, s5) == 1);   // a line without a key sets no option
	CHECK(o.fromFile[kFixUnknownWonderBase] == 0);
}


// Ticket 4804: [Performance] GameEventsOneCrossing, 0 (default) or 1; everything else gives the default with one warning.
void TestGameEventsOneCrossing()
{
	Options o;
	CHECK(o.GameEventsOneCrossing() == 0 && o.fromFile[kGameEventsOneCrossing] == 0);
	CHECK(kGameEventsOneCrossing == 11 && kCount == 41);   // 16 when 0504 was cut; 29 with 4783's ServerCombat (28)
	CHECK(Definition(kGameEventsOneCrossing).defaultValue == 0 && Definition(kGameEventsOneCrossing).minValue == 0 &&
	      Definition(kGameEventsOneCrossing).maxValue == 1);
	CHECK(strcmp(Definition(kGameEventsOneCrossing).section, "Performance") == 0);
	CHECK(FindKey("gameeventsonecrossing", 21) == kGameEventsOneCrossing && FindKey("GameEventsOneCrossin", 20) == kCount);
	Sink s;
	CHECK(P("[Performance]\nGameEventsOneCrossing = 1\n", o, s) == 0 && o.GameEventsOneCrossing() == 1 &&
	      o.fromFile[kGameEventsOneCrossing] == 1 && o.ServerUnits() == 0);
	Sink s1;  // a known key outside its home section is honoured with a warning
	CHECK(P("[Renderer]\nGameEventsOneCrossing = 1\n", o, s1) == 1 && o.GameEventsOneCrossing() == 1);
	const char* bad[] = {"2", "-1", "on", "off", "yes", "1.0", ""};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		char text[128];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Performance]\nGameEventsOneCrossing = %s\n", bad[i]);
		Sink sb;
		const unsigned n = P(text, o, sb);
		const bool ok = n == 1 && o.GameEventsOneCrossing() == 0 && o.fromFile[kGameEventsOneCrossing] == 0;
		if (!ok)
			printf("  GameEventsOneCrossing bad value '%s': %u warnings, value %d: %s", bad[i], n, o.GameEventsOneCrossing(),
			       sb.text);
		CHECK(ok);
	}
	Sink s3;
	CHECK(P("[Performance]\nGameEventsOneCrossing = 2\n", o, s3) == 1);
	CHECK(s3.Has("line 2: GameEventsOneCrossing = 2: not a whole number from 0 to 1; the default 0 is used"));
	static char buf[32768];
	const size_t n = DefaultFileText(buf, sizeof(buf));
	CHECK(n < sizeof(buf) && strstr(buf, "\r\n[Performance]\r\n") != NULL &&
	      strstr(buf, "\r\n; GameEventsOneCrossing = 0\r\n") != NULL);
	Sink s4;
	CHECK(Parse(buf, n, o, Collect, &s4) == 0 && o.GameEventsOneCrossing() == 0 && o.fromFile[kGameEventsOneCrossing] == 0);
}

// Ticket 0573 (D193): [Performance] HiddenEmbarks, 0 (default) or 1; everything else gives the default with one warning.
// (Ids 12-14 were reserved for other branches (K4); 14 since ticket 0613 is [Extras] FixUnknownWonderBase.)
void TestHiddenEmbarks()
{
	Options o;
	CHECK(o.HiddenEmbarks() == 0 && o.fromFile[kHiddenEmbarks] == 0);
	CHECK(kHiddenEmbarks == 15 && kGameEventsOneCrossing == 11 && kFixUnknownWonderBase == 14 && kCount == 41);   // 16 until 4931,
	                                                                                                    // 22 until 4938, 23 until 0455, 28 until 4783
	CHECK(Definition(kHiddenEmbarks).defaultValue == 0 && Definition(kHiddenEmbarks).minValue == 0 &&
	      Definition(kHiddenEmbarks).maxValue == 1);
	CHECK(strcmp(Definition(kHiddenEmbarks).section, "Performance") == 0);
	CHECK(FindKey("hiddenembarks", 13) == kHiddenEmbarks && FindKey("HiddenEmbark", 12) == kCount &&
	      FindKey("HiddenEmbarkss", 14) == kCount);
	CHECK(FindKey("", 0) == kCount);   // (id 14 was reserved here until ticket 0613 filled it)
	const char* good[] = {"0", "1", " 1 ", "\"1\"", "1 ; on"};
	const int goodValue[] = {0, 1, 1, 1, 1};
	for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); ++i)
	{
		char text[128];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Performance]\nHiddenEmbarks = %s\n", good[i]);
		Sink sg;
		const unsigned n = P(text, o, sg);
		const bool ok = n == 0 && o.HiddenEmbarks() == goodValue[i] && o.fromFile[kHiddenEmbarks] == 1 &&
		                o.AiDrainUnordered() == 0 && o.AiSkipVisualDrain() == 0 && o.AiUpdateTickMs() == 0;
		if (!ok) printf("  HiddenEmbarks good value '%s': %u warnings, value %d: %s", good[i], n, o.HiddenEmbarks(), sg.text);
		CHECK(ok);
	}
	const char* bad[] = {"2", "-1", "on", "off", "true", "1.0", "", "1x"};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		char text[128];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Performance]\nHiddenEmbarks = %s\n", bad[i]);
		Sink sb;
		const unsigned n = P(text, o, sb);
		const bool ok = n == 1 && o.HiddenEmbarks() == 0 && o.fromFile[kHiddenEmbarks] == 0;
		if (!ok) printf("  HiddenEmbarks bad value '%s': %u warnings, value %d: %s", bad[i], n, o.HiddenEmbarks(), sb.text);
		CHECK(ok);
	}
	Sink s3;
	CHECK(P("[Performance]\nHiddenEmbarks = 2\n", o, s3) == 1);
	CHECK(s3.Has("line 2: HiddenEmbarks = 2: not a whole number from 0 to 1; the default 0 is used"));
	Sink s5;   // a line without a key is not a reserved option
	CHECK(P("[Performance]\n = 1\n", o, s5) == 1 && o.fromFile[kFixUnknownWonderBase] == 0);
	// the default file lists it commented out at 0, after AiDrainUnordered, with no empty key line, and parses back
	static char buf[32768];
	const size_t n = DefaultFileText(buf, sizeof(buf));
	const char* drain = strstr(buf, "\r\n; AiDrainUnordered = 0\r\n");
	const char* he = strstr(buf, "\r\n; HiddenEmbarks = 0\r\n");
	CHECK(n < sizeof(buf) && drain != NULL && he != NULL && drain < he && strstr(buf, "\r\n;  = 0\r\n") == NULL &&
	      strstr(buf, "\r\n; = 0\r\n") == NULL);
	Sink s4;
	CHECK(Parse(buf, n, o, Collect, &s4) == 0 && o.HiddenEmbarks() == 0 && o.fromFile[kHiddenEmbarks] == 0);
}

// Ticket 4783 (MC #8627): [Renderer] ServerCombat, 1 (default: route S arms as one with ServerUnits = 1) or 0 (the server
// never asks; stock fights); everything else gives the default with one warning.
void TestServerCombat()
{
	Options o;
	CHECK(o.ServerCombat() == 1 && o.fromFile[kServerCombat] == 0);
	CHECK(kServerCombat == 28 && kCount == 41);
	CHECK(Definition(kServerCombat).defaultValue == 1 && Definition(kServerCombat).minValue == 0 &&
	      Definition(kServerCombat).maxValue == 1);
	CHECK(strcmp(Definition(kServerCombat).section, "Renderer") == 0 && Definition(kServerCombat).category == kCatNone &&
	      Definition(kServerCombat).needsKey == NULL);
	CHECK(FindKey("servercombat", 12) == kServerCombat && FindKey("ServerCombat", 12) == kServerCombat &&
	      FindKey("ServerComba", 11) == kCount);
	CHECK(Extra(o, kServerCombat).why == kExtraNotAnExtra);
	const char* good[] = {"0", "1", " 0 ", "\"0\"", "0 ; off"};
	const int goodValue[] = {0, 1, 0, 0, 0};
	for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); ++i)
	{
		char text[128];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Renderer]\nServerUnits = 1\nServerCombat = %s\n", good[i]);
		Sink sg;
		const unsigned n = P(text, o, sg);
		const bool ok = n == 0 && o.ServerCombat() == goodValue[i] && o.fromFile[kServerCombat] == 1 &&
		                o.ServerUnits() == 1 && o.ServerRenderer() == 0;
		if (!ok) printf("  ServerCombat good value '%s': %u warnings, value %d: %s", good[i], n, o.ServerCombat(), sg.text);
		CHECK(ok);
	}
	const char* bad[] = {"2", "-1", "on", "off", "true", "0.0", "", "0x"};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		char text[128];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Renderer]\nServerCombat = %s\n", bad[i]);
		Sink sb;
		const unsigned n = P(text, o, sb);
		const bool ok = n == 1 && o.ServerCombat() == 1 && o.fromFile[kServerCombat] == 0;
		if (!ok) printf("  ServerCombat bad value '%s': %u warnings, value %d: %s", bad[i], n, o.ServerCombat(), sb.text);
		CHECK(ok);
	}
	Sink s3;
	CHECK(P("[Renderer]\nServerCombat = 2\n", o, s3) == 1);
	CHECK(s3.Has("line 2: ServerCombat = 2: not a whole number from 0 to 1; the default 1 is used"));
	// ServerUnits alone leaves it at its default (the proxy reads it only with ServerUnits = 1)
	Sink s6;
	CHECK(P("[Renderer]\nServerRenderer = 1\nServerUnits = 1\n", o, s6) == 0 && o.ServerCombat() == 1 &&
	      o.fromFile[kServerCombat] == 0);
	// the default file lists it commented out at 1 with its help, outside [Extras], and parses back
	static char buf[32768];
	const size_t n = DefaultFileText(buf, sizeof(buf));
	const char* sc = strstr(buf, "\r\n; ServerCombat = 1\r\n");
	const char* extras = strstr(buf, "\r\n[Extras]\r\n");
	CHECK(n < sizeof(buf) && sc != NULL && extras != NULL && sc < extras &&
	      strstr(buf, "Switch this off if fights look or sound wrong") != NULL);
	Sink s4;
	CHECK(Parse(buf, n, o, Collect, &s4) == 0 && o.ServerCombat() == 1 && o.fromFile[kServerCombat] == 0);
}

// Ticket 0789 (D269, D255): [Renderer] ModelStore, id 40 (39 is 0784's HideTreadEffect), 1 (default: the server's model store)
// or 0 (ticket 0709's keep); everything else gives the default with one warning
void TestModelStore()
{
	Options o;
	CHECK(o.ModelStore() == 1 && o.fromFile[kModelStore] == 0);
	CHECK(kModelStore == 40 && kHideTreadEffect == 39 && kCount == 41);
	CHECK(Definition(kModelStore).defaultValue == 1 && Definition(kModelStore).minValue == 0 &&
	      Definition(kModelStore).maxValue == 1);
	CHECK(strcmp(Definition(kModelStore).section, "Renderer") == 0 && Definition(kModelStore).category == kCatNone &&
	      Definition(kModelStore).needsKey == NULL && Definition(kModelStore).title != NULL);
	CHECK(strcmp(Definition(kHideTreadEffect).key, "HideTreadEffect") == 0);   // the merge filled 0789's reserved row
	CHECK(FindKey("modelstore", 10) == kModelStore && FindKey("ModelStore", 10) == kModelStore &&
	      FindKey("ModelStor", 9) == kCount && FindKey("", 0) == kCount);
	CHECK(Extra(o, kModelStore).why == kExtraNotAnExtra);
	const char* good[] = {"0", "1", " 0 ", "\"0\"", "0 ; off"};
	const int goodValue[] = {0, 1, 0, 0, 0};
	for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); ++i)
	{
		char text[128];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Renderer]\nModelStore = %s\n", good[i]);
		Sink sg;
		const unsigned n = P(text, o, sg);
		const bool ok = n == 0 && o.ModelStore() == goodValue[i] && o.fromFile[kModelStore] == 1;
		if (!ok) printf("  ModelStore good value '%s': %u warnings, value %d: %s", good[i], n, o.ModelStore(), sg.text);
		CHECK(ok);
	}
	const char* bad[] = {"2", "-1", "on", "off", "true", "0.0", "", "0x"};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		char text[128];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Renderer]\nModelStore = %s\n", bad[i]);
		Sink sb;
		const unsigned n = P(text, o, sb);
		const bool ok = n == 1 && o.ModelStore() == 1 && o.fromFile[kModelStore] == 0;
		if (!ok) printf("  ModelStore bad value '%s': %u warnings, value %d: %s", bad[i], n, o.ModelStore(), sb.text);
		CHECK(ok);
	}
	Sink s3;
	CHECK(P("[Renderer]\nModelStore = 2\n", o, s3) == 1);
	CHECK(s3.Has("line 2: ModelStore = 2: not a whole number from 0 to 1; the default 1 is used"));
	// the default file lists it commented out at 1 with its help, outside [Extras], and parses back
	static char buf[32768];
	const size_t n = DefaultFileText(buf, sizeof(buf));
	const char* ms = strstr(buf, "\r\n; ModelStore = 1\r\n");
	const char* extras = strstr(buf, "\r\n[Extras]\r\n");
	CHECK(n < sizeof(buf) && ms != NULL && extras != NULL && ms < extras &&
	      strstr(buf, "flashes away for a frame") != NULL);
	Sink s4;
	CHECK(Parse(buf, n, o, Collect, &s4) == 0 && o.ModelStore() == 1 && o.fromFile[kModelStore] == 0);
}

// Ticket 0635 (MC #9004, #9011; D224, D225): [Proxy] GuardsWhenOff, id 29, default 1 (after 4783's ServerCombat, 28).
void TestGuardsWhenOff()
{
	Options o;
	CHECK(o.GuardsWhenOff() == 1 && o.fromFile[kGuardsWhenOff] == 0);
	CHECK(kServerCombat == 28 && kGuardsWhenOff == 29 && kCount == 41);
	const Def& d = Definition(kGuardsWhenOff);
	CHECK(strcmp(d.key, "GuardsWhenOff") == 0 && strcmp(d.section, "Proxy") == 0 && d.defaultValue == 1 &&
	      d.minValue == 0 && d.maxValue == 1 && d.category == kCatNone && d.needsKey == NULL);
	CHECK(FindKey("guardswhenoff", 13) == kGuardsWhenOff && FindKey("GuardsWhenOf", 12) == kCount &&
	      FindKey("GuardsWhenOfff", 14) == kCount);
	CHECK(Extra(o, kGuardsWhenOff).why == kExtraNotAnExtra);
	const char* good[] = {"0", "1", " 0 ", "\"0\"", "0 ; off"};
	const int goodValue[] = {0, 1, 0, 0, 0};
	for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); ++i)
	{
		char text[128];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Proxy]\nGuardsWhenOff = %s\n", good[i]);
		Sink sg;
		const unsigned n = P(text, o, sg);
		const bool ok = n == 0 && o.GuardsWhenOff() == goodValue[i] && o.fromFile[kGuardsWhenOff] == 1;
		if (!ok) printf("  GuardsWhenOff good value '%s': %u warnings, value %d: %s", good[i], n, o.GuardsWhenOff(), sg.text);
		CHECK(ok);
	}
	const char* bad[] = {"2", "-1", "on", "off", "", "1x"};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		char text[128];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Proxy]\nGuardsWhenOff = %s\n", bad[i]);
		Sink sb;
		const unsigned n = P(text, o, sb);
		const bool ok = n == 1 && o.GuardsWhenOff() == 1 && o.fromFile[kGuardsWhenOff] == 0;
		if (!ok) printf("  GuardsWhenOff bad value '%s': %u warnings, value %d: %s", bad[i], n, o.GuardsWhenOff(), sb.text);
		CHECK(ok);
	}
	Sink s3;
	CHECK(P("[Proxy]\nGuardsWhenOff = 2\n", o, s3) == 1);
	CHECK(s3.Has("line 2: GuardsWhenOff = 2: not a whole number from 0 to 1; the default 1 is used"));
	Sink s6;   // outside its home section: read, with a warning
	CHECK(P("[Renderer]\nGuardsWhenOff = 0\n", o, s6) == 1 && o.GuardsWhenOff() == 0 &&
	      s6.Has("GuardsWhenOff belongs in [Proxy], not [Renderer]; it is read anyway"));
	// the default file: [Proxy] after ServerCombat's [Renderer] and before [Extras], commented out at 1; parses back
	static char buf[32768];
	const size_t n = DefaultFileText(buf, sizeof(buf));
	const char* sc = strstr(buf, "\r\n; ServerCombat = 1\r\n");
	const char* px = strstr(buf, "\r\n[Proxy]\r\n");
	const char* qg = strstr(buf, "\r\n; GuardsWhenOff = 1\r\n");
	const char* ex = strstr(buf, "\r\n[Extras]\r\n");
	CHECK(n < sizeof(buf) && sc && px && qg && ex && sc < px && px < qg && qg < ex);
	Sink s4;
	CHECK(Parse(buf, n, o, Collect, &s4) == 0 && o.GuardsWhenOff() == 1 && o.fromFile[kGuardsWhenOff] == 0);
}

// Ticket 0745 (D255, 0739's audit gaps G2-G4): [Proxy] KeyboardHookThread (1), [Proxy] LeaderScenes (0) and
// [Performance] RenderFrameJobs (-1), each at the behaviour main had before the key existed.
void TestD255Options()
{
	Options o;
	CHECK(kKeyboardHookThread == 34 && kLeaderScenes == 35 && kRenderFrameJobs == 36 && kCount == 41);
	CHECK(o.KeyboardHookThread() == 1 && o.LeaderScenes() == 0 && o.RenderFrameJobs() == -1);
	CHECK(o.fromFile[kKeyboardHookThread] == 0 && o.fromFile[kLeaderScenes] == 0 && o.fromFile[kRenderFrameJobs] == 0);
	struct Row
	{
		Id id;
		const char* section;
		const char* key;
		int def, lo, hi;
	};
	const Row rows[] = {{kKeyboardHookThread, "Proxy", "KeyboardHookThread", 1, 0, 1},
	                    {kLeaderScenes, "Proxy", "LeaderScenes", 0, 0, 2},
	                    {kRenderFrameJobs, "Performance", "RenderFrameJobs", -1, -1, 8}};
	for (size_t r = 0; r < sizeof(rows) / sizeof(rows[0]); ++r)
	{
		const Row& w = rows[r];
		const Def& d = Definition(w.id);
		CHECK(strcmp(d.key, w.key) == 0 && strcmp(d.section, w.section) == 0 && d.defaultValue == w.def &&
		      d.minValue == w.lo && d.maxValue == w.hi && d.category == kCatNone && d.needsKey == NULL && d.title &&
		      d.testOnly == 0 && d.valueCategory == NULL);
		const size_t kl = strlen(w.key);
		char lower[64];
		for (size_t i = 0; i <= kl; ++i) lower[i] = (char)((w.key[i] >= 'A' && w.key[i] <= 'Z') ? w.key[i] + 32 : w.key[i]);
		CHECK(FindKey(lower, kl) == w.id && FindKey(w.key, kl - 1) == kCount);
		CHECK(Extra(o, w.id).why == kExtraNotAnExtra);
		for (int v = w.lo; v <= w.hi; ++v)   // every value in range is read from the file, without a warning
		{
			char text[128];
			_snprintf_s(text, sizeof(text), _TRUNCATE, "[%s]\n%s = %d\n", w.section, w.key, v);
			Options g;
			Sink sg;
			const unsigned n = P(text, g, sg);
			const bool ok = n == 0 && g.Get(w.id) == v && g.fromFile[w.id] == 1;
			if (!ok) printf("  %s good value %d: %u warnings, value %d: %s", w.key, v, n, g.Get(w.id), sg.text);
			CHECK(ok);
		}
		const int badInt[] = {w.lo - 1, w.hi + 1};
		for (int b = 0; b < 2; ++b)   // out of range: one warning, the default
		{
			char text[128];
			_snprintf_s(text, sizeof(text), _TRUNCATE, "[%s]\n%s = %d\n", w.section, w.key, badInt[b]);
			Options g;
			Sink sb;
			const unsigned n = P(text, g, sb);
			char want[200];
			_snprintf_s(want, sizeof(want), _TRUNCATE,
			            "line 2: %s = %d: not a whole number from %d to %d; the default %d is used", w.key, badInt[b], w.lo,
			            w.hi, w.def);
			const bool ok = n == 1 && g.Get(w.id) == w.def && g.fromFile[w.id] == 0 && sb.Has(want);
			if (!ok) printf("  %s bad value %d: %u warnings, value %d: %s", w.key, badInt[b], n, g.Get(w.id), sb.text);
			CHECK(ok);
		}
		char text[128];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[%s]\n%s = auto\n", w.section, w.key);
		Options g;
		Sink sa;
		CHECK(P(text, g, sa) == 1 && g.Get(w.id) == w.def && g.fromFile[w.id] == 0);
	}
	// the default file: each commented out at its default, in its section, and the file parses back to the defaults
	static char buf[32768];
	const size_t n = DefaultFileText(buf, sizeof(buf));
	const char* gw = strstr(buf, "\r\n; GuardsWhenOff = 1\r\n");
	const char* kh = strstr(buf, "\r\n; KeyboardHookThread = 1\r\n");
	const char* ls = strstr(buf, "\r\n; LeaderScenes = 0\r\n");
	const char* fj = strstr(buf, "\r\n; RenderFrameJobs = -1\r\n");
	const char* perf2 = gw ? strstr(gw, "\r\n[Performance]\r\n") : NULL;   // a second [Performance] block, after [Proxy]
	const char* sh = strstr(buf, "\r\n[Shell]\r\n");
	CHECK(n < sizeof(buf) && gw && kh && ls && fj && perf2 && sh && gw < kh && kh < ls && ls < perf2 && perf2 < fj &&
	      fj < sh);
	CHECK(strstr(buf, "\r\n;   -1 = automatic (default)") && strstr(buf, "\r\n;   1 to 8 = that many helper threads.\r\n") &&
	      strstr(buf, "about 32 MB"));
	Options back;
	Sink s4;
	CHECK(Parse(buf, n, back, Collect, &s4) == 0 && back.KeyboardHookThread() == 1 && back.LeaderScenes() == 0 &&
	      back.RenderFrameJobs() == -1 && back.fromFile[kRenderFrameJobs] == 0);
	// G1: the text option's title is the installer page's checkbox label
	CHECK(strcmp(TextDefinition(kWindowTitlePrefix).title, "Show [VP64] in the window title") == 0);
}

// Ticket 0747 (D255 G5/G5b): [Logs] KeepRuns, id 38, 1..100, default 10 (the proxy's keep_runs before the key): every value
// in range read without a warning, 0, 101, -1 and text give the default with one warning; its own [Logs] block in the
// default file after [Performance] RenderFrameJobs and before [Shell]; its help's one value line covers 1..100 (the
// installer page's one button with a number box).
void TestKeepRuns()
{
	Options o;
	CHECK(kKeepRuns == 38 && kCount == 41 && o.KeepRuns() == 10 && o.fromFile[kKeepRuns] == 0);
	const Def& d = Definition(kKeepRuns);
	CHECK(strcmp(d.section, "Logs") == 0 && strcmp(d.key, "KeepRuns") == 0 && d.defaultValue == 10 && d.minValue == 1 &&
	      d.maxValue == 100 && d.category == kCatNone && d.needsKey == NULL && d.title && strlen(d.title) <= 45 &&
	      d.testOnly == 0 && d.valueCategory == NULL);
	CHECK(FindKey("keepruns", 8) == kKeepRuns && FindKey("KeepRun", 7) == kCount);
	CHECK(Extra(o, kKeepRuns).why == kExtraNotAnExtra);
	int good = 0;
	for (int v = 1; v <= 100; ++v)
	{
		char text[64];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Logs]\nKeepRuns = %d\n", v);
		Options g;
		Sink sg;
		if (P(text, g, sg) == 0 && g.KeepRuns() == v && g.fromFile[kKeepRuns] == 1) ++good;
		else printf("  KeepRuns good value %d: value %d: %s", v, g.KeepRuns(), sg.text);
	}
	CHECK(good == 100);
	const char* bad[] = {"0", "101", "-1", "ten", "5x", ""};
	for (size_t b = 0; b < sizeof(bad) / sizeof(bad[0]); ++b)
	{
		char text[64];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Logs]\nKeepRuns = %s\n", bad[b]);
		Options g;
		Sink sb;
		const unsigned n = P(text, g, sb);
		const bool ok = n == 1 && g.KeepRuns() == 10 && g.fromFile[kKeepRuns] == 0;
		if (!ok) printf("  KeepRuns bad value '%s': %u warnings, value %d: %s", bad[b], n, g.KeepRuns(), sb.text);
		CHECK(ok);
	}
	{
		Options g;
		Sink sb;
		CHECK(P("[Logs]\nKeepRuns = 0\n", g, sb) == 1 &&
		      sb.Has("line 2: KeepRuns = 0: not a whole number from 1 to 100; the default 10 is used"));
	}
	{
		Options g;
		Sink sb;   // outside its home section: honoured with a warning (the file's rule)
		CHECK(P("[Proxy]\nKeepRuns = 3\n", g, sb) == 1 && g.KeepRuns() == 3);
	}
	static char buf[32768];
	const size_t n = DefaultFileText(buf, sizeof(buf));
	const char* fj = strstr(buf, "\r\n; RenderFrameJobs = -1\r\n");
	const char* logs = strstr(buf, "\r\n[Logs]\r\n");
	const char* kr = strstr(buf, "\r\n; KeepRuns = 10\r\n");
	const char* sh = strstr(buf, "\r\n[Shell]\r\n");
	CHECK(n < sizeof(buf) && fj && logs && kr && sh && fj < logs && logs < kr && kr < sh);
	CHECK(logs && strstr(logs + 1, "\r\n[Logs]\r\n") == NULL);   // one [Logs] block
	CHECK(strstr(buf, "\r\n;   1 to 100 = keep that many of each (default 10); the newest is always kept.\r\n; KeepRuns = 10\r\n"));
	CHECK(strstr(buf, "Logs\\VP64\\run") != NULL);
	Options back;
	Sink s4;
	CHECK(Parse(buf, n, back, Collect, &s4) == 0 && back.KeepRuns() == 10 && back.fromFile[kKeepRuns] == 0);
}

// Ticket 0784 (D268, D255): [Renderer] HideTreadEffect, id 39, 0 to 1, default 1; in the [Renderer] block of the default
// file (the next free id after 0747's KeepRuns).
void TestHideTreadEffect()
{
	Options o;
	CHECK(kHideTreadEffect == 39 && kKeepRuns == 38 && kCount == 41);   // 40 until 0789's ModelStore
	CHECK(o.HideTreadEffect() == 1 && o.fromFile[kHideTreadEffect] == 0);
	const Def& d = Definition(kHideTreadEffect);
	CHECK(strcmp(d.section, "Renderer") == 0 && strcmp(d.key, "HideTreadEffect") == 0 && d.defaultValue == 1 &&
	      d.minValue == 0 && d.maxValue == 1 && d.category == kCatNone && d.needsKey == NULL && d.title &&
	      strlen(d.title) <= 45 && d.testOnly == 0 && d.valueCategory == NULL);
	CHECK(FindKey("hidetreadeffect", 15) == kHideTreadEffect && FindKey("HideTreadEffec", 14) == kCount &&
	      FindKey("HideTreadEffectt", 16) == kCount);
	CHECK(Extra(o, kHideTreadEffect).why == kExtraNotAnExtra);
	Sink s;
	CHECK(P("[Renderer]\nHideTreadEffect = 0\n", o, s) == 0 && o.HideTreadEffect() == 0 && o.fromFile[kHideTreadEffect] == 1 &&
	      o.ServerUnits() == 0);
	Options o1;
	Sink s1;
	CHECK(P("[Renderer]\nServerUnits = 1\nHideTreadEffect = 1\n", o1, s1) == 0 && o1.HideTreadEffect() == 1 &&
	      o1.fromFile[kHideTreadEffect] == 1 && o1.ServerUnits() == 1);
	const char* bad[] = {"2", "-1", "yes", "1x", ""};
	for (size_t b = 0; b < sizeof(bad) / sizeof(bad[0]); ++b)
	{
		char text[64];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Renderer]\nHideTreadEffect = %s\n", bad[b]);
		Options g;
		Sink sb;
		const unsigned n = P(text, g, sb);
		const bool ok = n == 1 && g.HideTreadEffect() == 1 && g.fromFile[kHideTreadEffect] == 0;
		if (!ok) printf("  HideTreadEffect bad value '%s': %u warnings, value %d: %s", bad[b], n, g.HideTreadEffect(), sb.text);
		CHECK(ok);
	}
	{
		Options g;
		Sink sb;
		CHECK(P("[Renderer]\nHideTreadEffect = 2\n", g, sb) == 1 &&
		      sb.Has("line 2: HideTreadEffect = 2: not a whole number from 0 to 1; the default 1 is used"));
	}
	static char buf[32768];
	const size_t n = DefaultFileText(buf, sizeof(buf));
	// the default file writes the whole-number options by id, a section line where the section changes: id 39 comes after
	// [Logs] KeepRuns (38), in a [Renderer] block of its own before the text options' [Shell]
	const char* ht = strstr(buf, "\r\n; HideTreadEffect = 1\r\n");
	const char* kr = strstr(buf, "\r\n; KeepRuns = 10\r\n");
	const char* sh = strstr(buf, "\r\n[Shell]\r\n");
	const char* sec = NULL;
	for (const char* p = strstr(buf, "\r\n["); p && ht && p < ht; p = strstr(p + 1, "\r\n[")) sec = p;
	CHECK(n < sizeof(buf) && ht && kr && sh && kr < ht && ht < sh && sec && strncmp(sec, "\r\n[Renderer]\r\n", 14) == 0);
	Options back;
	Sink s4;
	CHECK(Parse(buf, n, back, Collect, &s4) == 0 && back.HideTreadEffect() == 1 && back.fromFile[kHideTreadEffect] == 0);
}

// ---------------------------------------------------------------------------------------------------- [Extras] (4931)
// Ticket 4931 (epic 0454, D169/D170; docs/extras/flags-and-messages-design.md section 1).

// E4's review hook (design 1.6): an [Extras] row whose default is not 0 must be named here, in a change that names the maintainer's
// decision in a comment. So a feature's default changes only in a change that also edits this list.
const char* const kApprovedDefaults[] = {
	"EnableBugfixes",   // a category switch: bugfixes on by default (proposal 4.1, approved 2026-11-16)
	"FixAirBases",      // D235 (2) (the owner, 2026-11-22): on by default after its live smoke (ticket 4985, 01:26)
	"FixUnknownWonderBase",   // ticket 0613, MC #13471 (D169: a bugfix is on by default); the owner told at the merge
	"FixMinimapUnexplored",   // ticket 0727 (D169: a bugfix is on by default; the ticket's "default 1", the MC 2026-11-24)
	"FixSmoothWheel",   // ticket 0755 (D255, D169: a bugfix is on by default; proposed 1, the owner confirms at the merge)
};

bool StartsWith(const char* s, const char* prefix) { return strncmp(s, prefix, strlen(prefix)) == 0; }
bool StartsWithNoCase(const char* s, const char* prefix) { return _strnicmp(s, prefix, strlen(prefix)) == 0; }

// The naming and table rules of design 1.6 over any table of rows (the real one, or a broken one in a negative test).
// Returns the number of violations; each is one line in s.
unsigned ExtrasRuleViolations(const Def* rows, int n, const char* const* approved, size_t nApproved, Sink& s)
{
	unsigned bad = 0;
	char line[300];
#define VIOLATION(...)                                                     \
	do                                                                     \
	{                                                                      \
		_snprintf_s(line, sizeof(line), _TRUNCATE, __VA_ARGS__);          \
		Collect(&s, line);                                                 \
		++bad;                                                             \
	} while (0)
	for (int i = 0; i < n; ++i)
	{
		const Def& d = rows[i];
		const bool keyed = d.key && d.key[0];
		for (int j = 0; keyed && j < i; ++j)
			if (rows[j].key && rows[j].key[0] && _stricmp(rows[j].key, d.key) == 0)
				VIOLATION("row %d: key %s is also row %d's (keys are unique in the whole file)", i, d.key, j);
		if (keyed && _stricmp(d.key, "EnableGameplayChanges") == 0)
			VIOLATION("row %d: EnableGameplayChanges is the reserved third category: no row", i);
		const bool extras = strcmp(d.section, "Extras") == 0;
		if (!extras || !keyed)
		{
			if (!extras && d.category != kCatNone) VIOLATION("row %d: %s outside [Extras] has a category", i, d.key);
			if (d.needsKey) VIOLATION("row %d: %s outside [Extras] or without a key needs %s", i, d.key, d.needsKey);
			if (!extras && keyed &&
			    (StartsWithNoCase(d.key, "Enable") || StartsWithNoCase(d.key, "Fix") || StartsWithNoCase(d.key, "Experiment")))
				VIOLATION("row %d: %s outside [Extras] starts with Enable, Fix or Experiment", i, d.key);
			if (extras && !keyed && d.category != kCatNone) VIOLATION("row %d: a reserved [Extras] row has a category", i);
			if (d.valueCategory) VIOLATION("row %d: %s outside [Extras] or without a key has a category per value", i, d.key);
			continue;
		}
		const bool experiment = StartsWith(d.key, "Experiment");
		// ticket 4940 (MC #8261): a row with a category per value (RoadLayout): its key names the thing, not a category;
		// value 0 is stock (kCatNone); every other value a bugfix or a visual addition; the row's category is its first
		// non-zero value's (its place in the default file and on the installer's page)
		if (d.valueCategory)
		{
			if (StartsWithNoCase(d.key, "Fix") || StartsWithNoCase(d.key, "Enable") || StartsWithNoCase(d.key, "Experiment"))
				VIOLATION("row %d: %s has a category per value but starts with Fix, Enable or Experiment", i, d.key);
			if (d.minValue != 0 || d.maxValue < 1)
				VIOLATION("row %d: %s has a category per value but its range %d-%d is not 0 to N", i, d.key, d.minValue, d.maxValue);
			else
			{
				unsigned char first = kCatNone;
				for (int v = d.minValue; v <= d.maxValue; ++v)
				{
					const unsigned char c = d.valueCategory[v - d.minValue];
					if (v == 0 && c != kCatNone) VIOLATION("row %d: %s's value 0 has a category (%d): 0 is stock", i, d.key, (int)c);
					if (v != 0 && c != kCatBugfix && c != kCatVisual)
						VIOLATION("row %d: %s's value %d is neither a bugfix nor a visual addition (%d)", i, d.key, v, (int)c);
					if (v != 0 && first == kCatNone && (c == kCatBugfix || c == kCatVisual)) first = c;
				}
				if (first != kCatNone && d.category != first)
					VIOLATION("row %d: %s's category %d is not its first non-zero value's (%d)", i, d.key, (int)d.category, (int)first);
			}
		}
		switch (d.category)
		{
		case kCatSwitch:
			if (!StartsWith(d.key, "Enable")) VIOLATION("row %d: switch %s does not start with Enable", i, d.key);
			if (d.minValue != 0 || d.maxValue != 1) VIOLATION("row %d: switch %s is not 0-1", i, d.key);
			if (d.needsKey) VIOLATION("row %d: switch %s needs %s", i, d.key, d.needsKey);
			break;
		case kCatBugfix:
			if (!StartsWith(d.key, "Fix") && !experiment && !d.valueCategory)
				VIOLATION("row %d: bugfix %s starts with neither Fix nor Experiment", i, d.key);
			break;
		case kCatVisual:   // (Enable: the rule below, for every non-switch)
			if (StartsWith(d.key, "Fix")) VIOLATION("row %d: visual addition %s starts with Fix", i, d.key);
			break;
		default:
			VIOLATION("row %d: [Extras] row %s has no category (or an unknown one: %d)", i, d.key, (int)d.category);
			continue;
		}
		if (d.category != kCatSwitch && StartsWith(d.key, "Enable"))
			VIOLATION("row %d: %s starts with Enable but is no switch", i, d.key);
		if (d.category != kCatSwitch && (d.minValue > 0 || d.maxValue < 0))
			VIOLATION("row %d: %s's range %d-%d does not hold 0 (0 = stock)", i, d.key, d.minValue, d.maxValue);
		if (experiment && d.defaultValue != 0) VIOLATION("row %d: experiment %s defaults to %d, not 0", i, d.key, d.defaultValue);
		if (d.defaultValue != 0)
		{
			bool ok = false;
			for (size_t k = 0; k < nApproved; ++k) ok = ok || strcmp(approved[k], d.key) == 0;
			if (!ok) VIOLATION("row %d: %s defaults to %d without the maintainer's approval (kApprovedDefaults)", i, d.key, d.defaultValue);
		}
		if (d.needsKey && d.category != kCatSwitch)   // (a switch's need is reported above)
		{
			// the need names an [Extras] bugfix or visual row of this table; following needs never comes back (no cycle)
			int cur = i, steps = 0;
			while (cur >= 0 && rows[cur].needsKey && steps <= n)
			{
				int next = -1;
				for (int j = 0; j < n; ++j)
					if (rows[j].key && rows[j].key[0] && _stricmp(rows[j].key, rows[cur].needsKey) == 0) next = j;
				if (next < 0 || strcmp(rows[next].section, "Extras") != 0 ||
				    (rows[next].category != kCatBugfix && rows[next].category != kCatVisual))
				{
					if (cur == i) VIOLATION("row %d: %s needs %s, which is no bugfix or visual [Extras] row", i, d.key, d.needsKey);
					break;
				}
				cur = next;
				++steps;
				if (cur == i)
				{
					VIOLATION("row %d: %s needs itself through a cycle", i, d.key);
					break;
				}
			}
		}
	}
#undef VIOLATION
	return bad;
}

void TestExtrasTable()
{
	// ids pinned (MC #7273, K4: never renumbered)
	CHECK(kEnableBugfixes == 16 && kEnableVisualAdditions == 17 && kRoadLayout == 18 && kReserved19 == 19 &&
	      kReserved20 == 20 && kReserved21 == 21 && kVisibleAreaOutline == 22 && kFixUnitPlacement == 23 &&
	      kGarrisonVisible == 24 && kShipsMoored == 25 && kFixAirBases == 26 && kAirInterceptPatrol == 27 &&
	      kServerCombat == 28 && kGuardsWhenOff == 29 && kRoadJunction == 30 && kRoadJitter == 31 &&
	      kPerimeterGarrisons == 32 && kFixMinimapUnexplored == 33 && kFixSmoothWheel == 37 &&
	      kCount == 41);   // 23-26: MC #7491, #7901; 27: MC #8114; 28: MC #8627 (4783); 29: MC #9004 (0635);
	                       // 30-31: MC #10673 (4940); 18 renamed FixRoadLayout -> RoadLayout (4940, MC #8258);
	                       // 32: MC #11877 (4983, D243); 33: ticket 0727; 34-36: ticket 0745 (outside [Extras]);
	                       // 37: ticket 0755; 38: ticket 0747 (outside [Extras])
	CHECK(strcmp(Definition(kEnableBugfixes).key, "EnableBugfixes") == 0 &&
	      strcmp(Definition(kEnableBugfixes).section, "Extras") == 0 && Definition(kEnableBugfixes).defaultValue == 1 &&
	      Definition(kEnableBugfixes).minValue == 0 && Definition(kEnableBugfixes).maxValue == 1 &&
	      Definition(kEnableBugfixes).category == kCatSwitch && Definition(kEnableBugfixes).needsKey == NULL);
	CHECK(strcmp(Definition(kEnableVisualAdditions).key, "EnableVisualAdditions") == 0 &&
	      strcmp(Definition(kEnableVisualAdditions).section, "Extras") == 0 &&
	      Definition(kEnableVisualAdditions).defaultValue == 0 && Definition(kEnableVisualAdditions).minValue == 0 &&
	      Definition(kEnableVisualAdditions).maxValue == 1 && Definition(kEnableVisualAdditions).category == kCatSwitch);
	for (int id = kReserved19; id <= kReserved21; ++id)
		CHECK(Definition((Id)id).key[0] == 0 && strcmp(Definition((Id)id).section, "Extras") == 0 &&
		      Definition((Id)id).category == kCatNone && Definition((Id)id).needsKey == NULL);
	// every row outside [Extras] keeps kCatNone and no needsKey (the two trailing fields zero)
	for (int i = 0; i < kEnableBugfixes; ++i)
		if (i != kFixUnknownWonderBase)   // ticket 0613: the one [Extras] row below the switches (id reserved under K4)
			CHECK(Definition((Id)i).category == kCatNone && Definition((Id)i).needsKey == NULL);
	CHECK(strcmp(Definition(kFixUnknownWonderBase).section, "Extras") == 0 &&
	      Definition(kFixUnknownWonderBase).category == kCatBugfix && Definition(kFixUnknownWonderBase).needsKey == NULL &&
	      Definition(kFixUnknownWonderBase).defaultValue == 1 && Definition(kFixUnknownWonderBase).minValue == 0 &&
	      Definition(kFixUnknownWonderBase).maxValue == 1);
	// the reserved third category is no row
	CHECK(FindKey("EnableGameplayChanges", 21) == kCount && FindKey("enablebugfixes", 14) == kEnableBugfixes &&
	      FindKey("ENABLEVISUALADDITIONS", 21) == kEnableVisualAdditions);
	// the rules over the real table: no violation
	static Def rows[kCount];
	for (int i = 0; i < kCount; ++i) rows[i] = Definition((Id)i);
	const size_t nApproved = sizeof(kApprovedDefaults) / sizeof(kApprovedDefaults[0]);
	Sink s;
	const unsigned v = ExtrasRuleViolations(rows, kCount, kApprovedDefaults, nApproved, s);
	if (v) printf("  extras rules: %s", s.text);
	CHECK(v == 0);
	// negative cases: each broken table gives exactly its one violation, named
	static const unsigned char kPvGood[3] = {kCatNone, kCatBugfix, kCatVisual};
	static const unsigned char kPvZero[2] = {kCatBugfix, kCatBugfix};
	static const unsigned char kPvSwitch[2] = {kCatNone, kCatSwitch};
	static const unsigned char kPvOrder[3] = {kCatNone, kCatVisual, kCatBugfix};
	struct Broken
	{
		Def row;
		const char* want;
	};
	const Broken broken[] = {
		{{"Extras", "FixThing", kTypeInt, 0, 0, 1, "", kCatBugfix, NULL, NULL}, NULL},   // the control: a good bugfix row
		{{"Extras", "ServerRenderer", kTypeInt, 0, 0, 1, "", kCatVisual, NULL, NULL}, "is also row 1's"},
		{{"Extras", "serverrenderer", kTypeInt, 0, 0, 1, "", kCatVisual, NULL, NULL}, "is also row 1's"},
		{{"Extras", "EnableGameplayChanges", kTypeInt, 0, 0, 1, "", kCatSwitch, NULL, NULL}, "reserved third category"},
		{{"Renderer", "Shiny", kTypeInt, 0, 0, 1, "", kCatVisual, NULL, NULL}, "outside [Extras] has a category"},
		{{"Renderer", "Shiny", kTypeInt, 0, 0, 1, "", kCatNone, "FixThing", NULL}, "outside [Extras] or without a key needs"},
		{{"Renderer", "FixShiny", kTypeInt, 0, 0, 1, "", kCatNone, NULL, NULL}, "starts with Enable, Fix or Experiment"},
		{{"Performance", "enableShiny", kTypeInt, 0, 0, 1, "", kCatNone, NULL, NULL}, "starts with Enable, Fix or Experiment"},
		{{"Renderer", "ExperimentShiny", kTypeInt, 0, 0, 1, "", kCatNone, NULL, NULL}, "starts with Enable, Fix or Experiment"},
		{{"Extras", "", kTypeInt, 0, 0, 0, "", kCatBugfix, NULL, NULL}, "a reserved [Extras] row has a category"},
		{{"Extras", "ShinySwitch", kTypeInt, 0, 0, 1, "", kCatSwitch, NULL, NULL}, "does not start with Enable"},
		{{"Extras", "EnableShiny", kTypeInt, 0, 0, 2, "", kCatSwitch, NULL, NULL}, "is not 0-1"},
		{{"Extras", "EnableShiny", kTypeInt, 0, 0, 1, "", kCatSwitch, "FixThing", NULL}, "switch EnableShiny needs"},
		{{"Extras", "RoadLayout", kTypeInt, 0, 0, 1, "", kCatBugfix, NULL, NULL}, "starts with neither Fix nor Experiment"},
		{{"Extras", "FixShiny", kTypeInt, 0, 0, 1, "", kCatVisual, NULL, NULL}, "visual addition FixShiny starts with Fix"},
		{{"Extras", "EnableShiny", kTypeInt, 0, 0, 1, "", kCatVisual, NULL, NULL}, "starts with Enable but is no switch"},
		{{"Extras", "Shiny", kTypeInt, 0, 0, 1, "", kCatNone, NULL, NULL}, "has no category"},
		{{"Extras", "Shiny", kTypeInt, 0, 0, 1, "", 7, NULL, NULL}, "has no category (or an unknown one: 7)"},
		{{"Extras", "Shiny", kTypeInt, 0, 1, 2, "", kCatVisual, NULL, NULL}, "does not hold 0"},
		{{"Extras", "Shiny", kTypeInt, 0, -2, -1, "", kCatVisual, NULL, NULL}, "does not hold 0"},
		{{"Extras", "Shiny", kTypeInt, 1, 0, 1, "", kCatVisual, NULL, NULL}, "without the maintainer's approval"},
		{{"Extras", "FixShiny", kTypeInt, 1, 0, 1, "", kCatBugfix, NULL, NULL}, "without the maintainer's approval"},
		{{"Extras", "Shiny", kTypeInt, 0, 0, 1, "", kCatVisual, "ServerRenderer", NULL}, "which is no bugfix or visual [Extras] row"},
		{{"Extras", "Shiny", kTypeInt, 0, 0, 1, "", kCatVisual, "NoSuchKey", NULL}, "which is no bugfix or visual [Extras] row"},
		{{"Extras", "Shiny", kTypeInt, 0, 0, 1, "", kCatVisual, "Shiny", NULL}, "through a cycle"},
		// ticket 4940: rows with a category per value (RoadLayout's rule)
		{{"Extras", "RoadShape", kTypeInt, 0, 0, 2, "", kCatBugfix, NULL, NULL, kPvGood}, NULL},   // the control
		{{"Extras", "FixShape", kTypeInt, 0, 0, 2, "", kCatBugfix, NULL, NULL, kPvGood}, "starts with Fix, Enable or Experiment"},
		{{"Extras", "RoadShape", kTypeInt, 0, 0, 0, "", kCatBugfix, NULL, NULL, kPvGood}, "is not 0 to N"},
		{{"Extras", "RoadShape", kTypeInt, 0, 0, 1, "", kCatBugfix, NULL, NULL, kPvZero}, "value 0 has a category"},
		{{"Extras", "RoadShape", kTypeInt, 0, 0, 1, "", kCatBugfix, NULL, NULL, kPvSwitch}, "neither a bugfix nor a visual"},
		{{"Extras", "RoadShape", kTypeInt, 0, 0, 2, "", kCatBugfix, NULL, NULL, kPvOrder}, "is not its first non-zero value's"},
		{{"Renderer", "Shape", kTypeInt, 0, 0, 2, "", kCatNone, NULL, NULL, kPvGood}, "without a key has a category per value"},
	};
	for (size_t b = 0; b < sizeof(broken) / sizeof(broken[0]); ++b)
	{
		// the real table's first rows plus the broken row (row 1 is ServerRenderer, row 0 BakedTileCameraRadius)
		Def t[3] = {Definition(kBakedTileCameraRadius), Definition(kServerRenderer), broken[b].row};
		Sink sb;
		const unsigned nv = ExtrasRuleViolations(t, 3, kApprovedDefaults, nApproved, sb);
		const bool ok = broken[b].want ? (nv == 1 && sb.Has(broken[b].want)) : nv == 0;
		if (!ok) printf("  extras rules, case %u (%s): %u violations: %s", (unsigned)b, broken[b].row.key, nv, sb.text);
		CHECK(ok);
	}
	// an experiment's default may not be approved away: the experiment rule fires besides the approval rule
	{
		const char* const approvedExp[] = {"ExperimentShiny"};
		Def t[1] = {{"Extras", "ExperimentShiny", kTypeInt, 1, 0, 1, "", kCatVisual, NULL, NULL}};
		Sink sb;
		CHECK(ExtrasRuleViolations(t, 1, approvedExp, 1, sb) == 1 && sb.Has("experiment ExperimentShiny defaults to 1"));
		// an approved feature default passes; the same row without the approval fails
		const char* const approvedFix[] = {"FixShiny"};
		Def u[1] = {{"Extras", "FixShiny", kTypeInt, 1, 0, 1, "", kCatBugfix, NULL, NULL}};
		Sink s1, s2;
		CHECK(ExtrasRuleViolations(u, 1, approvedFix, 1, s1) == 0 && ExtrasRuleViolations(u, 1, NULL, 0, s2) == 1);
		// a two-row cycle; a need chain that ends well; a need on a switch
		Def c[2] = {{"Extras", "FixA", kTypeInt, 0, 0, 1, "", kCatBugfix, "FixB", NULL},
		            {"Extras", "FixB", kTypeInt, 0, 0, 1, "", kCatBugfix, "FixA", NULL}};
		Sink s3;
		CHECK(ExtrasRuleViolations(c, 2, NULL, 0, s3) == 2 && s3.Has("FixA needs itself") && s3.Has("FixB needs itself"));
		Def ch[3] = {{"Extras", "FixA", kTypeInt, 0, 0, 1, "", kCatBugfix, "FixB", NULL},
		             {"Extras", "FixB", kTypeInt, 0, 0, 1, "", kCatBugfix, "FixC", NULL},
		             {"Extras", "FixC", kTypeInt, 0, 0, 1, "", kCatBugfix, NULL, NULL}};
		Sink s4;
		CHECK(ExtrasRuleViolations(ch, 3, NULL, 0, s4) == 0);
		Def sw[2] = {{"Extras", "EnableBugfixes", kTypeInt, 1, 0, 1, "", kCatSwitch, NULL, NULL},
		             {"Extras", "FixA", kTypeInt, 0, 0, 1, "", kCatBugfix, "EnableBugfixes", NULL}};
		Sink s5;
		CHECK(ExtrasRuleViolations(sw, 2, kApprovedDefaults, nApproved, s5) == 1 && s5.Has("which is no bugfix"));
	}
}

// The two switches as the parser reads them (design 1.9 TestExtrasSwitches).
void TestExtrasSwitches()
{
	Options o;
	CHECK(o.EnableBugfixes() == 1 && o.EnableVisualAdditions() == 0 && o.fromFile[kEnableBugfixes] == 0 &&
	      o.fromFile[kEnableVisualAdditions] == 0);
	Sink s;
	CHECK(P("[Extras]\nEnableBugfixes = 0\nEnableVisualAdditions = 1\n", o, s) == 0 && o.EnableBugfixes() == 0 &&
	      o.EnableVisualAdditions() == 1 && o.fromFile[kEnableBugfixes] == 1 && o.fromFile[kEnableVisualAdditions] == 1 &&
	      s.n == 0);
	Sink s1;
	CHECK(P("[Extras]\nEnableBugfixes = 1\nEnableVisualAdditions = 0\n", o, s1) == 0 && o.EnableBugfixes() == 1 &&
	      o.EnableVisualAdditions() == 0 && o.fromFile[kEnableBugfixes] == 1 && o.fromFile[kEnableVisualAdditions] == 1);
	const char* keys[] = {"EnableBugfixes", "EnableVisualAdditions"};
	const Id ids[] = {kEnableBugfixes, kEnableVisualAdditions};
	const int defs[] = {1, 0};
	const char* bad[] = {"2", "-1", "on", "yes", "1.0", "", "1x", "off", "true"};
	for (int k = 0; k < 2; ++k)
	{
		for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
		{
			char text[128];
			_snprintf_s(text, sizeof(text), _TRUNCATE, "[Extras]\n%s = %s\n", keys[k], bad[i]);
			Sink sb;
			const unsigned n = P(text, o, sb);
			const bool ok = n == 1 && sb.n == 1 && o.Get(ids[k]) == defs[k] && o.fromFile[ids[k]] == 0;
			if (!ok) printf("  %s bad value '%s': %u warnings, value %d: %s", keys[k], bad[i], n, o.Get(ids[k]), sb.text);
			CHECK(ok);
		}
		char text[160], want[200];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Extras]\n%s = 2\n", keys[k]);
		Sink s2;
		CHECK(P(text, o, s2) == 1);
		_snprintf_s(want, sizeof(want), _TRUNCATE, "line 2: %s = 2: not a whole number from 0 to 1; the default %d is used",
		            keys[k], defs[k]);
		CHECK(s2.Has(want));
		// in another section: honoured, with the "belongs in [Extras]" warning
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Renderer]\n%s = %d\n", keys[k], 1 - defs[k]);
		Sink s3;
		CHECK(P(text, o, s3) == 1 && o.Get(ids[k]) == 1 - defs[k] && o.fromFile[ids[k]] == 1);
		_snprintf_s(want, sizeof(want), _TRUNCATE, "line 2: %s belongs in [Extras], not [Renderer]; it is read anyway", keys[k]);
		CHECK(s3.Has(want));
		// before any section: honoured, with its warning
		_snprintf_s(text, sizeof(text), _TRUNCATE, "%s = %d\n[Extras]\n", keys[k], 1 - defs[k]);
		Sink s4;
		CHECK(P(text, o, s4) == 1 && o.Get(ids[k]) == 1 - defs[k]);
		_snprintf_s(want, sizeof(want), _TRUNCATE, "line 1: %s comes before any section; it belongs in [Extras] and is read anyway",
		            keys[k]);
		CHECK(s4.Has(want));
		// set twice: the last line counts; a bad last line gives the default, not the earlier valid value
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Extras]\n%s = %d\n%s = %d\n", keys[k], defs[k], keys[k], 1 - defs[k]);
		Sink s5;
		CHECK(P(text, o, s5) == 1 && o.Get(ids[k]) == 1 - defs[k]);
		_snprintf_s(want, sizeof(want), _TRUNCATE, "line 3: %s is set again (first on line 2); the last one counts", keys[k]);
		CHECK(s5.Has(want));
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Extras]\n%s = %d\n%s = on\n", keys[k], 1 - defs[k], keys[k]);
		Sink s6;
		CHECK(P(text, o, s6) == 2 && o.Get(ids[k]) == defs[k] && o.fromFile[ids[k]] == 0);
	}
	// a switch never touches another option: with both categories off (and with both on), every option outside
	// [Extras] has its default, not from the file, exactly as a file without the lines
	for (int both = 0; both < 2; ++both)
	{
		Sink s7;
		CHECK(P(both ? "[Extras]\nEnableBugfixes = 1\nEnableVisualAdditions = 1\n"
		             : "[Extras]\nEnableBugfixes = 0\nEnableVisualAdditions = 0\n",
		        o, s7) == 0);
		bool same = true;
		for (int i = 0; i < kEnableBugfixes; ++i)
			same = same && o.value[i] == Definition((Id)i).defaultValue && o.fromFile[i] == 0;
		CHECK(same && o.EnableBugfixes() == both && o.EnableVisualAdditions() == both);
	}
	// [Extras] is a known section; the reserved rows cannot be set (a line without a key is no option)
	Sink s8;
	CHECK(P("[Extras]\n = 1\n", o, s8) == 1 && !s8.Has("no option belongs to this section"));
	for (int id = kReserved19; id <= kReserved21; ++id) CHECK(o.fromFile[id] == 0 && o.Get((Id)id) == 0);
}

// ExtraOf on synthetic rows: the rules switch -> own flag -> needed extra, by file and by default (design 1.9).
void TestExtraOf()
{
	const Def bug = {"Extras", "FixSynthetic", kTypeInt, 0, 0, 1, "", kCatBugfix, NULL, NULL};
	const Def vis = {"Extras", "SyntheticPolish", kTypeInt, 0, 0, 2, "", kCatVisual, NULL, NULL};
	const Def needy = {"Extras", "FixSyntheticB", kTypeInt, 0, 0, 1, "", kCatBugfix, "FixSynthetic", NULL};
	// all four switch x flag combinations, the switch from the file and by default, for a bugfix row and a visual row
	for (int fromFile = 0; fromFile < 2; ++fromFile)
		for (int sw = 0; sw < 2; ++sw)
			for (int flag = 0; flag < 2; ++flag)
				for (int row = 0; row < 2; ++row)
				{
					const Def& d = row ? vis : bug;
					const Id swId = row ? kEnableVisualAdditions : kEnableBugfixes;
					Options o;
					Sink s;
					if (fromFile)
					{
						char text[128];
						_snprintf_s(text, sizeof(text), _TRUNCATE, "[Extras]\n%s = %d\n", Definition(swId).key, sw);
						P(text, o, s);
					}
					// by default the bugfix switch is 1 and the visual one 0: those combinations only
					if (!fromFile && sw != Definition(swId).defaultValue) continue;
					const ExtraState e = ExtraOf(d, flag, NULL, o);
					bool ok;
					if (!sw)
						ok = !e.on && e.value == 0 && e.why == kExtraCategoryOff && e.decidedBy == swId;
					else if (!flag)
						ok = !e.on && e.value == 0 && e.why == kExtraFlagOff && e.decidedBy == kCount;
					else
						ok = e.on && e.value == 1 && e.why == kExtraOn && e.decidedBy == kCount;
					if (!ok)
						printf("  ExtraOf %s file=%d switch=%d flag=%d: on %d value %d why %d by %d\n", d.key, fromFile, sw, flag,
						       (int)e.on, e.value, (int)e.why, (int)e.decidedBy);
					CHECK(ok);
				}
	Options on;
	Sink s;
	CHECK(P("[Extras]\nEnableBugfixes = 1\nEnableVisualAdditions = 1\n", on, s) == 0);
	// a value besides on and off passes through when on, gives 0 when off
	ExtraState e = ExtraOf(vis, 2, NULL, on);
	CHECK(e.on && e.value == 2 && e.why == kExtraOn);
	Options visOff;
	e = ExtraOf(vis, 2, NULL, visOff);
	CHECK(!e.on && e.value == 0 && e.why == kExtraCategoryOff && e.decidedBy == kEnableVisualAdditions);
	// the needed extra: off -> kExtraNeedsOff (decidedBy = FindKey of the need: kCount, the row is not in the table)
	const ExtraState needOff = ExtraOf(bug, 0, NULL, on);
	const ExtraState needOn = ExtraOf(bug, 1, NULL, on);
	CHECK(!needOff.on && needOn.on);
	e = ExtraOf(needy, 1, &needOff, on);
	CHECK(!e.on && e.value == 0 && e.why == kExtraNeedsOff && e.decidedBy == kCount);
	e = ExtraOf(needy, 1, &needOn, on);
	CHECK(e.on && e.value == 1 && e.why == kExtraOn);
	// a need named but not given: the safe side, off
	e = ExtraOf(needy, 1, NULL, on);
	CHECK(!e.on && e.why == kExtraNeedsOff);
	// the order of the rules: the switch before the own flag before the need
	Options bugOff;
	Sink s2;
	P("[Extras]\nEnableBugfixes = 0\n", bugOff, s2);
	e = ExtraOf(needy, 0, &needOff, bugOff);
	CHECK(e.why == kExtraCategoryOff && e.decidedBy == kEnableBugfixes);
	e = ExtraOf(needy, 0, &needOff, on);
	CHECK(e.why == kExtraFlagOff);
	// not an extra: a row of no category, a switch, a row without a key, an unknown category
	const Def none = {"Renderer", "ServerRenderer", kTypeInt, 0, 0, 1, "", kCatNone, NULL, NULL};
	const Def swRow = Definition(kEnableBugfixes);
	const Def empty = {"Extras", "", kTypeInt, 0, 0, 0, "", kCatBugfix, NULL, NULL};
	const Def odd = {"Extras", "Odd", kTypeInt, 0, 0, 1, "", 9, NULL, NULL};
	const Def* nots[] = {&none, &swRow, &empty, &odd};
	for (size_t i = 0; i < sizeof(nots) / sizeof(nots[0]); ++i)
	{
		e = ExtraOf(*nots[i], 1, NULL, on);
		CHECK(!e.on && e.value == 0 && e.why == kExtraNotAnExtra && e.decidedBy == kCount);
	}
}

// Extra() over the table: no extra row exists before the first feature (X3), so every id is no extra (design 1.9).
void TestExtra()
{
	Options o;
	Sink s;
	CHECK(P("[Extras]\nEnableBugfixes = 1\nEnableVisualAdditions = 1\n", o, s) == 0);
	const int ids[] = {kBakedTileCameraRadius, kServerRenderer, kHiddenEmbarks, kEnableBugfixes, kEnableVisualAdditions,
	                   kGameEventsOneCrossing, kReserved19, kReserved20, kReserved21, kCount, -1, 1000,
	                   -2147483647 - 1};
	for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
	{
		const ExtraState e = Extra(o, (Id)ids[i]);
		const bool ok = !e.on && e.value == 0 && e.why == kExtraNotAnExtra && e.decidedBy == kCount;
		if (!ok) printf("  Extra(%d): on %d why %d\n", ids[i], (int)e.on, (int)e.why);
		CHECK(ok);
		if (ids[i] >= 0 && ids[i] < kCount)
		{
			const ExtraState m = o.Extra((Id)ids[i]);
			CHECK(m.on == e.on && m.why == e.why && m.decidedBy == e.decidedBy);
		}
	}
	// the keyed [Extras] rows that are a bugfix or a visual addition: ticket 4938's VisibleAreaOutline (id 22) only (0
	// until 4938); each feature ticket changes this pin
	// and ticket 4934's FixRoadLayout (id 18, RoadLayout since 4940); epic 0455's five unit extras (ids 23-27, 2 rows until
	// them); ticket 4940's RoadJunction and RoadJitter (ids 30-31, 7 rows until them); ticket 4983's PerimeterGarrisons
	// (id 32, D243; 9 rows until it); ticket 0613's FixUnknownWonderBase (id 14, 10 rows until it); ticket 0727's
	// FixMinimapUnexplored (id 33, 11 rows until it); ticket 0755's FixSmoothWheel (id 37, 12 rows until it)
	int extras = 0, extraIds[13] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
	for (int i = 0; i < kCount; ++i)
		if (Definition((Id)i).category == kCatBugfix || Definition((Id)i).category == kCatVisual)
		{
			if (extras < 13) extraIds[extras] = i;
			++extras;
		}
	CHECK(extras == 13 && extraIds[0] == kFixUnknownWonderBase && extraIds[1] == kRoadLayout &&
	      extraIds[2] == kVisibleAreaOutline && extraIds[3] == kFixUnitPlacement && extraIds[4] == kGarrisonVisible &&
	      extraIds[5] == kShipsMoored && extraIds[6] == kFixAirBases && extraIds[7] == kAirInterceptPatrol &&
	      extraIds[8] == kRoadJunction && extraIds[9] == kRoadJitter && extraIds[10] == kPerimeterGarrisons &&
	      extraIds[11] == kFixMinimapUnexplored && extraIds[12] == kFixSmoothWheel);
	// no row has a category per value since one-off 9081 (RoadLayout, the only one of ticket 4940, is a plain visual
	// addition, the owner 2026-11-23); the rule stays (ExtrasRuleViolations, the RoadShape rows above)
	for (int i = 0; i < kCount; ++i)
		CHECK(Definition((Id)i).valueCategory == NULL);
	// epic 0455's rows (MC #7491, #7901, #8114): key, category, default 0, range 0-1, no needed extra (ServerUnits = 1 is
	// the consumer's check, not a needsKey: needsKey names [Extras] rows only); the two bugfixes under EnableBugfixes, the
	// three visual additions under EnableVisualAdditions, each off under the defaults and on with its switch and its flag
	{
		struct Row
		{
			Id id;
			const char* key;
			unsigned char category;
			int def;   // D235 (2) (the owner, 2026-11-22): FixAirBases defaults to 1 after its live smoke (ticket 4985)
			int max;   // D250 (the owner, 2026-11-24): ShipsMoored 0-2 (1 = look (d), 2 = look (b); ticket 4984)
		};
		const Row rows[] = {{kFixUnitPlacement, "FixUnitPlacement", kCatBugfix, 0, 1},
		                    {kGarrisonVisible, "GarrisonVisible", kCatVisual, 0, 1},
		                    {kShipsMoored, "ShipsMoored", kCatVisual, 0, 2},
		                    {kFixAirBases, "FixAirBases", kCatBugfix, 1, 1},
		                    {kAirInterceptPatrol, "AirInterceptPatrol", kCatVisual, 0, 1},
		                    {kPerimeterGarrisons, "PerimeterGarrisons", kCatVisual, 0, 1}};   // ticket 4983 (D243)
		for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
		{
			const Def& d = Definition(rows[i].id);
			const bool rowOk = strcmp(d.section, "Extras") == 0 && strcmp(d.key, rows[i].key) == 0 &&
			                   d.category == rows[i].category && d.needsKey == NULL && d.defaultValue == rows[i].def &&
			                   d.minValue == 0 && d.maxValue == rows[i].max && strstr(d.help, "Needs ServerUnits = 1") != NULL;
			if (!rowOk) printf("  row %d (%s) differs from epic 0455's\n", (int)rows[i].id, rows[i].key);
			CHECK(rowOk);
			CHECK(FindKey(rows[i].key, strlen(rows[i].key)) == rows[i].id);
			const bool bug = rows[i].category == kCatBugfix;
			Options def;
			const ExtraState e0 = Extra(def, rows[i].id);   // defaults: off (own flag 0, or the visual switch 0); a row
			                                                // whose default is 1 (FixAirBases) on under EnableBugfixes' 1
			if (rows[i].def == 1)
				CHECK(e0.on && e0.why == kExtraOn && e0.decidedBy == rows[i].id);
			else
				CHECK(!e0.on && e0.why == (bug ? kExtraFlagOff : kExtraCategoryOff) &&
				      e0.decidedBy == (bug ? rows[i].id : kEnableVisualAdditions));
			char text[200];
			_snprintf_s(text, sizeof(text), _TRUNCATE, "[Extras]\n%s = 1\n%s", rows[i].key,
			            bug ? "" : "EnableVisualAdditions = 1\n");
			Options on;
			Sink so;
			CHECK(P(text, on, so) == 0);
			const ExtraState e1 = Extra(on, rows[i].id);
			CHECK(e1.on && e1.why == kExtraOn && e1.decidedBy == rows[i].id && e1.value == 1);
			// its category switch off: off, whatever its own line says
			_snprintf_s(text, sizeof(text), _TRUNCATE, "[Extras]\n%s = 1\n%s", rows[i].key,
			            bug ? "EnableBugfixes = 0\n" : "EnableVisualAdditions = 0\n");
			Options sw;
			Sink ss;
			P(text, sw, ss);
			const ExtraState e2 = Extra(sw, rows[i].id);
			CHECK(!e2.on && e2.why == kExtraCategoryOff && e2.decidedBy == (bug ? kEnableBugfixes : kEnableVisualAdditions));
			// every value of the range in effect as itself (ShipsMoored's 2: look (b), D250)
			for (int v = 2; v <= rows[i].max; ++v)
			{
				_snprintf_s(text, sizeof(text), _TRUNCATE, "[Extras]\n%s = %d\n%s", rows[i].key, v,
				            bug ? "" : "EnableVisualAdditions = 1\n");
				Options ov;
				Sink sv;
				CHECK(P(text, ov, sv) == 0);
				const ExtraState ev = Extra(ov, rows[i].id);
				CHECK(ev.on && ev.why == kExtraOn && ev.decidedBy == rows[i].id && ev.value == v);
			}
			// out of range: the default (0) with a warning
			_snprintf_s(text, sizeof(text), _TRUNCATE, "[Extras]\nEnableVisualAdditions = 1\n%s = %d\n", rows[i].key,
			            rows[i].max + 1);
			Options oor;
			Sink sr;
			CHECK(P(text, oor, sr) == 1 && Extra(oor, rows[i].id).on == (rows[i].def == 1));   // the row's default
		}
		// GarrisonVisible's start-log lines (the unit extras module prints them as "units <line>")
		Options c;
		Sink cs;
		P("[Extras]\nEnableVisualAdditions = 1\nGarrisonVisible = 1\n", c, cs);
		char buf[200];
		DescribeExtra(c, kGarrisonVisible, buf, sizeof(buf));
		CHECK(strcmp(buf, "GarrisonVisible: in effect (GarrisonVisible = 1 from the file; EnableVisualAdditions = 1 from the "
		                  "file)") == 0);
		Options d;
		DescribeExtra(d, kGarrisonVisible, buf, sizeof(buf));
		CHECK(strcmp(buf, "GarrisonVisible: off: EnableVisualAdditions = 0 (default) switches off every visual addition") == 0);
	}
	// RoadLayout (ticket 4934's FixRoadLayout, D212; ticket 4940, D215): default 0 for now, range 0-2, no needed extra; a
	// visual addition with every value since one-off 9081 (the owner, 2026-11-23; was a category per value, 1 a bugfix, MC #8261):
	// EnableVisualAdditions governs 1 and 2 alike, EnableBugfixes neither
	{
		const Def& fr = Definition(kRoadLayout);
		CHECK(strcmp(fr.section, "Extras") == 0 && strcmp(fr.key, "RoadLayout") == 0 && fr.category == kCatVisual &&
		      fr.needsKey == NULL && fr.defaultValue == 0 && fr.minValue == 0 && fr.maxValue == 2 && fr.valueCategory == NULL);
		CHECK(FindKey("roadlayout", 10) == kRoadLayout && FindKey("RoadLayou", 9) == kCount &&
		      FindKey("FixRoadLayout", 13) == kCount);   // the old name is gone (never shipped, MC #8258)
		CHECK(CategoryOf(fr, 0) == kCatVisual && CategoryOf(fr, 1) == kCatVisual && CategoryOf(fr, 2) == kCatVisual &&
		      CategoryOf(fr, 3) == kCatVisual && CategoryOf(fr, -1) == kCatVisual &&
		      CategoryOf(Definition(kGarrisonVisible), 1) == kCatVisual);
		struct RCase
		{
			const char* text;
			bool on;
			ExtraWhy why;
			Id decidedBy;
			int value;
			unsigned warnings;
		};
		const RCase rc[] = {
			// the visual switch defaults to 0: stock under the defaults, whatever RoadLayout says
			{"", false, kExtraCategoryOff, kEnableVisualAdditions, 0, 0},
			{"[Extras]\nRoadLayout = 1\n", false, kExtraCategoryOff, kEnableVisualAdditions, 0, 0},
			{"[Extras]\nRoadLayout = 2\n", false, kExtraCategoryOff, kEnableVisualAdditions, 0, 0},
			{"[Extras]\nEnableVisualAdditions = 1\nRoadLayout = 0\n", false, kExtraFlagOff, kRoadLayout, 0, 0},
			{"[Extras]\nEnableVisualAdditions = 1\nRoadLayout = 1\n", true, kExtraOn, kRoadLayout, 1, 0},
			{"[Extras]\nEnableVisualAdditions = 1\nRoadLayout = 2\n", true, kExtraOn, kRoadLayout, 2, 0},
			// the bugfix switch governs no value, either way
			{"[Extras]\nEnableVisualAdditions = 1\nEnableBugfixes = 0\nRoadLayout = 1\n", true, kExtraOn, kRoadLayout, 1, 0},
			{"[Extras]\nEnableVisualAdditions = 1\nEnableBugfixes = 0\nRoadLayout = 2\n", true, kExtraOn, kRoadLayout, 2, 0},
			{"[Extras]\nEnableVisualAdditions = 0\nEnableBugfixes = 1\nRoadLayout = 1\n", false, kExtraCategoryOff,
			 kEnableVisualAdditions, 0, 0},
			// out of range: the default (0) with a warning
			{"[Extras]\nRoadLayout = 3\n", false, kExtraCategoryOff, kEnableVisualAdditions, 0, 1},
			{"[Extras]\nEnableVisualAdditions = 1\nRoadLayout = -1\n", false, kExtraFlagOff, kRoadLayout, 0, 1},
		};
		for (size_t i = 0; i < sizeof(rc) / sizeof(rc[0]); ++i)
		{
			Options c;
			Sink cs;
			const unsigned w = P(rc[i].text, c, cs);
			const ExtraState e = Extra(c, kRoadLayout);
			const bool ok = e.on == rc[i].on && e.why == rc[i].why && e.decidedBy == rc[i].decidedBy &&
			                e.value == rc[i].value && w == rc[i].warnings;
			if (!ok)
				printf("  RoadLayout case %u: on %d why %d decidedBy %d value %d warnings %u\n", (unsigned)i, (int)e.on,
				       (int)e.why, (int)e.decidedBy, e.value, w);
			CHECK(ok);
		}
		struct DCase
		{
			const char* text;
			const char* line;
		};
		const DCase dc[] = {
			{"[Extras]\nEnableVisualAdditions = 1\nRoadLayout = 1\n",
			 "RoadLayout: in effect (RoadLayout = 1 from the file; EnableVisualAdditions = 1 from the file)"},
			{"[Extras]\nEnableVisualAdditions = 1\nRoadLayout = 2\n",
			 "RoadLayout: in effect (RoadLayout = 2 from the file; EnableVisualAdditions = 1 from the file)"},
			{"[Extras]\nRoadLayout = 1\n",
			 "RoadLayout: off: EnableVisualAdditions = 0 (default) switches off every visual addition"},
			{"", "RoadLayout: off: EnableVisualAdditions = 0 (default) switches off every visual addition"},
			{"[Extras]\nEnableVisualAdditions = 1\n", "RoadLayout: off: RoadLayout = 0 (default)"},
		};
		for (size_t i = 0; i < sizeof(dc) / sizeof(dc[0]); ++i)
		{
			Options c;
			Sink cs;
			P(dc[i].text, c, cs);
			char buf[200];
			DescribeExtra(c, kRoadLayout, buf, sizeof(buf));
			const bool ok = strcmp(buf, dc[i].line) == 0;
			if (!ok) printf("  RoadLayout line %u: %s\n", (unsigned)i, buf);
			CHECK(ok);
		}
	}
	// RoadJunction and RoadJitter (ticket 4940, MC #10673): visual additions, default 0 (the owner picks from the sheets),
	// no needed extra (the consumer reads them only while RoadLayout = 2 is in effect)
	{
		const Def& j = Definition(kRoadJunction);
		const Def& t = Definition(kRoadJitter);
		CHECK(strcmp(j.section, "Extras") == 0 && strcmp(j.key, "RoadJunction") == 0 && j.category == kCatVisual &&
		      j.needsKey == NULL && j.defaultValue == 0 && j.minValue == 0 && j.maxValue == 3 && j.valueCategory == NULL);
		CHECK(strcmp(t.section, "Extras") == 0 && strcmp(t.key, "RoadJitter") == 0 && t.category == kCatVisual &&
		      t.needsKey == NULL && t.defaultValue == 0 && t.minValue == 0 && t.maxValue == 7 && t.valueCategory == NULL);
		CHECK(FindKey("roadjunction", 12) == kRoadJunction && FindKey("ROADJITTER", 10) == kRoadJitter);
		struct JCase
		{
			const char* text;
			int junction, jitter;   // Extra().value of each
			unsigned warnings;
		};
		const JCase jc[] = {
			{"", 0, 0, 0},
			{"[Extras]\nRoadJunction = 2\nRoadJitter = 5\n", 0, 0, 0},   // the visual switch off: both 0
			{"[Extras]\nEnableVisualAdditions = 1\nRoadJunction = 3\nRoadJitter = 7\n", 3, 7, 0},
			{"[Extras]\nEnableVisualAdditions = 1\nRoadJunction = 1\nRoadJitter = 1\n", 1, 1, 0},
			{"[Extras]\nEnableVisualAdditions = 1\nRoadJunction = 4\nRoadJitter = 8\n", 0, 0, 2},   // out of range: default
			{"[Extras]\nEnableVisualAdditions = 1\nRoadJunction = -1\nRoadJitter = -1\n", 0, 0, 2},
		};
		for (size_t i = 0; i < sizeof(jc) / sizeof(jc[0]); ++i)
		{
			Options c;
			Sink cs;
			const unsigned w = P(jc[i].text, c, cs);
			const ExtraState ej = Extra(c, kRoadJunction), et = Extra(c, kRoadJitter);
			const bool ok = ej.value == jc[i].junction && et.value == jc[i].jitter && w == jc[i].warnings &&
			                ej.on == (jc[i].junction != 0) && et.on == (jc[i].jitter != 0);
			if (!ok)
				printf("  RoadJunction/RoadJitter case %u: %d %d warnings %u\n", (unsigned)i, ej.value, et.value, w);
			CHECK(ok);
		}
	}
	// VisibleAreaOutline (ticket 4938): a visual addition with no needed extra, default 0 (E4), range 0-1
	const Def& vo = Definition(kVisibleAreaOutline);
	CHECK(strcmp(vo.section, "Extras") == 0 && strcmp(vo.key, "VisibleAreaOutline") == 0 && vo.category == kCatVisual &&
	      vo.needsKey == NULL && vo.defaultValue == 0 && vo.minValue == 0 && vo.maxValue == 1);
	CHECK(FindKey("visibleareaoutline", 18) == kVisibleAreaOutline && FindKey("VisibleAreaOutlin", 17) == kCount);
	struct Case
	{
		const char* text;
		bool on;
		ExtraWhy why;
		Id decidedBy;
	};
	const Case cases[] = {
		{"", false, kExtraCategoryOff, kEnableVisualAdditions},                                     // all defaults
		{"[Extras]\nVisibleAreaOutline = 1\n", false, kExtraCategoryOff, kEnableVisualAdditions},   // switch default 0
		{"[Extras]\nEnableVisualAdditions = 1\n", false, kExtraFlagOff, kVisibleAreaOutline},       // own flag default 0
		{"[Extras]\nEnableVisualAdditions = 1\nVisibleAreaOutline = 0\n", false, kExtraFlagOff, kVisibleAreaOutline},
		{"[Extras]\nEnableVisualAdditions = 0\nVisibleAreaOutline = 1\n", false, kExtraCategoryOff, kEnableVisualAdditions},
		{"[Extras]\nEnableVisualAdditions = 1\nVisibleAreaOutline = 1\n", true, kExtraOn, kVisibleAreaOutline},
		// E5: the bugfix switch does not govern a visual addition, either way
		{"[Extras]\nEnableBugfixes = 0\nEnableVisualAdditions = 1\nVisibleAreaOutline = 1\n", true, kExtraOn, kVisibleAreaOutline},
		{"[Extras]\nEnableBugfixes = 1\nEnableVisualAdditions = 0\nVisibleAreaOutline = 1\n", false, kExtraCategoryOff,
		 kEnableVisualAdditions},
		// a value out of range gives the default (0) with a warning: off
		{"[Extras]\nEnableVisualAdditions = 1\nVisibleAreaOutline = 2\n", false, kExtraFlagOff, kVisibleAreaOutline},
		// the key honoured in another section (with its warning)
		{"[Renderer]\nVisibleAreaOutline = 1\n[Extras]\nEnableVisualAdditions = 1\n", true, kExtraOn, kVisibleAreaOutline},
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		Options c;
		Sink cs;
		P(cases[i].text, c, cs);
		const ExtraState e = Extra(c, kVisibleAreaOutline);
		const bool ok = e.on == cases[i].on && e.why == cases[i].why && e.decidedBy == cases[i].decidedBy &&
		                e.value == (cases[i].on ? 1 : 0);
		if (!ok)
			printf("  VisibleAreaOutline case %u: on %d why %d decidedBy %d value %d\n", (unsigned)i, (int)e.on, (int)e.why,
			       (int)e.decidedBy, e.value);
		CHECK(ok);
		const ExtraState m = c.Extra(kVisibleAreaOutline);
		CHECK(m.on == e.on && m.why == e.why && m.decidedBy == e.decidedBy);
	}
	// the warnings of the two cases that must warn, and none for the plain ones (negative: a case that warns wrongly)
	{
		Options c;
		Sink cs;
		CHECK(P("[Extras]\nEnableVisualAdditions = 1\nVisibleAreaOutline = 2\n", c, cs) == 1);
		Options c2;
		Sink cs2;
		CHECK(P("[Renderer]\nVisibleAreaOutline = 1\n", c2, cs2) == 1);
		Options c3;
		Sink cs3;
		CHECK(P("[Extras]\nEnableVisualAdditions = 1\nVisibleAreaOutline = 1\n", c3, cs3) == 0);
	}
	// its start-log line
	{
		Options c;
		Sink cs;
		P("[Extras]\nEnableVisualAdditions = 1\nVisibleAreaOutline = 1\n", c, cs);
		char buf[200];
		DescribeExtra(c, kVisibleAreaOutline, buf, sizeof(buf));
		CHECK(strcmp(buf, "VisibleAreaOutline: in effect (VisibleAreaOutline = 1 from the file; EnableVisualAdditions = 1 "
		                  "from the file)") == 0);
		Options d;
		DescribeExtra(d, kVisibleAreaOutline, buf, sizeof(buf));
		CHECK(strcmp(buf, "VisibleAreaOutline: off: EnableVisualAdditions = 0 (default) switches off every visual addition") == 0);
	}
}

// The start log's lines (design 1.3, 1.4): exact text, and snprintf semantics.
void TestDescribeExtra()
{
	const Def road = {"Extras", "FixRoadLayout", kTypeInt, 0, 0, 1, "", kCatBugfix, NULL, NULL};
	const Def bridges = {"Extras", "FixRoadBridges", kTypeInt, 0, 0, 1, "", kCatBugfix, "FixRoadLayout", NULL};
	const Def sharper = {"Extras", "ExperimentSharperTerrain", kTypeInt, 0, 0, 1, "", kCatVisual, NULL, NULL};
	char buf[400];
	Options def;   // defaults: EnableBugfixes = 1, EnableVisualAdditions = 0
	ExtraState e = ExtraOf(road, 1, NULL, def);
	CHECK(DescribeExtraOf(road, 1, true, e, def, buf, sizeof(buf)) == strlen(buf) &&
	      strcmp(buf, "FixRoadLayout: in effect (FixRoadLayout = 1 from the file; EnableBugfixes = 1 default)") == 0);
	Options off;
	Sink s;
	P("[Extras]\nEnableBugfixes = 0\n", off, s);
	e = ExtraOf(road, 1, NULL, off);
	DescribeExtraOf(road, 1, true, e, off, buf, sizeof(buf));
	CHECK(strcmp(buf, "FixRoadLayout: off: EnableBugfixes = 0 (from the file) switches off every bugfix") == 0);
	const ExtraState roadOff = ExtraOf(road, 0, NULL, def);
	e = ExtraOf(bridges, 1, &roadOff, def);
	DescribeExtraOf(bridges, 1, true, e, def, buf, sizeof(buf));
	CHECK(strcmp(buf, "FixRoadBridges: off: it needs FixRoadLayout, which is off") == 0);
	Options visOn;
	Sink s2;
	P("[Extras]\nEnableVisualAdditions = 1\n", visOn, s2);
	e = ExtraOf(sharper, 0, NULL, visOn);
	DescribeExtraOf(sharper, 0, false, e, visOn, buf, sizeof(buf));
	CHECK(strcmp(buf, "ExperimentSharperTerrain: off: ExperimentSharperTerrain = 0 (default)") == 0);
	e = ExtraOf(sharper, 0, NULL, def);
	DescribeExtraOf(sharper, 0, false, e, def, buf, sizeof(buf));
	CHECK(strcmp(buf, "ExperimentSharperTerrain: off: EnableVisualAdditions = 0 (default) switches off every visual "
	                  "addition") == 0);
	DescribeExtraOf(road, 0, true, ExtraOf(road, 0, NULL, def), def, buf, sizeof(buf));
	CHECK(strcmp(buf, "FixRoadLayout: off: FixRoadLayout = 0 (from the file)") == 0);
	// not an extra, by row and by id
	const Def empty = {"Extras", "", kTypeInt, 0, 0, 0, "", kCatNone, NULL, NULL};
	DescribeExtraOf(empty, 0, false, ExtraOf(empty, 0, NULL, def), def, buf, sizeof(buf));
	CHECK(strcmp(buf, "(no key): not an extra") == 0);
	DescribeExtra(def, kServerRenderer, buf, sizeof(buf));
	CHECK(strcmp(buf, "ServerRenderer: not an extra") == 0);
	DescribeExtra(def, kEnableBugfixes, buf, sizeof(buf));
	CHECK(strcmp(buf, "EnableBugfixes: not an extra") == 0);
	DescribeExtra(def, kReserved19, buf, sizeof(buf));
	CHECK(strcmp(buf, "option 19: not an extra") == 0);
	// the real RoadLayout row (ticket 4934's FixRoadLayout, 4940): a visual addition since one-off 9081, so off by the
	// visual switch (default 0) under the defaults
	DescribeExtra(def, kRoadLayout, buf, sizeof(buf));
	CHECK(strcmp(buf, "RoadLayout: off: EnableVisualAdditions = 0 (default) switches off every visual addition") == 0);
	DescribeExtra(def, kCount, buf, sizeof(buf));
	CHECK(strcmp(buf, "option 41: not an extra") == 0);   // 23 until epic 0455's rows, 28 until 4783's ServerCombat, 29 until 0635,
	                                                      // 30 until 4940's RoadJunction and RoadJitter, 32 until 4983's
	                                                      // PerimeterGarrisons, 33 until 0727's FixMinimapUnexplored, 34
	                                                      // until 0745's rows, 37 until 0755's FixSmoothWheel, 38 until
	                                                      // 0747's KeepRuns, 39 until 0784's HideTreadEffect,
	                                                      // 40 until 0789's ModelStore
	DescribeExtra(def, kKeepRuns, buf, sizeof(buf));
	CHECK(strcmp(buf, "KeepRuns: not an extra") == 0);   // ticket 0747: outside [Extras]
	DescribeExtra(def, kGuardsWhenOff, buf, sizeof(buf));
	CHECK(strcmp(buf, "GuardsWhenOff: not an extra") == 0);   // ticket 0635: outside [Extras]
	DescribeExtra(def, (Id)-1, buf, sizeof(buf));
	CHECK(strcmp(buf, "option -1: not an extra") == 0);
	// the switches' line
	const size_t sl = DescribeSwitches(def, buf, sizeof(buf));
	CHECK(sl == strlen(buf) && strcmp(buf, "EnableBugfixes = 1 (default), EnableVisualAdditions = 0 (default)") == 0);
	DescribeSwitches(off, buf, sizeof(buf));
	CHECK(strcmp(buf, "EnableBugfixes = 0 (from the file), EnableVisualAdditions = 0 (default)") == 0);
	// truncation: snprintf semantics (cap 0, 1, 10); the full length is returned every time
	e = ExtraOf(road, 1, NULL, def);
	const size_t full = DescribeExtraOf(road, 1, true, e, def, NULL, 0);
	CHECK(full == strlen("FixRoadLayout: in effect (FixRoadLayout = 1 from the file; EnableBugfixes = 1 default)"));
	char one[1] = {'x'};
	CHECK(DescribeExtraOf(road, 1, true, e, def, one, 1) == full && one[0] == 0);
	char ten[10];
	memset(ten, 'x', sizeof(ten));
	CHECK(DescribeExtraOf(road, 1, true, e, def, ten, sizeof(ten)) == full && strcmp(ten, "FixRoadLa") == 0);
	CHECK(DescribeExtra(def, kServerRenderer, NULL, 0) == strlen("ServerRenderer: not an extra"));
	CHECK(DescribeExtra(def, kReserved19, ten, sizeof(ten)) == strlen("option 19: not an extra") && strcmp(ten, "option 19") == 0);
	CHECK(DescribeSwitches(def, one, 1) == sl && one[0] == 0);
	CHECK(DescribeSwitches(def, ten, sizeof(ten)) == sl && strcmp(ten, "EnableBug") == 0);
}

// The default file's [Extras] block: once, last, the comment block first, then the switches (design 1.5).
void TestDefaultTextExtras()
{
	static char buf[32768];
	const size_t n = DefaultFileText(buf, sizeof(buf));
	CHECK(n < sizeof(buf));
	const char* ex = strstr(buf, "\r\n[Extras]\r\n");
	CHECK(ex != NULL && strstr(ex + 3, "[Extras]") == NULL && strstr(buf, "[Extras]") == ex + 2);   // once
	if (!ex) return;
	CHECK(strstr(ex + 2, "\r\n[") == NULL);                    // last: no section header after it
	const char* after = ex + strlen("\r\n[Extras]\r\n");
	const char* comment = "; Extras are optional changes to what the patch draws.";
	CHECK(strncmp(after, comment, strlen(comment)) == 0);
	const char* bf = strstr(buf, "\r\n; EnableBugfixes = 1\r\n");
	const char* va = strstr(buf, "\r\n; EnableVisualAdditions = 0\r\n");
	const char* he = strstr(buf, "\r\n; HiddenEmbarks = 0\r\n");
	CHECK(bf && va && he && he < ex && ex < bf && bf < va);
	// epic 0455's rows in the block, by category: the bugfixes after the road fix, the visual additions after the outline
	// (one-off 9081, the owner 2026-11-23: RoadLayout a visual addition, and the road options RoadLayout, RoadJunction and
	// RoadJitter last among the visual additions, together)
	const char* road = strstr(buf, "\r\n; RoadLayout = 0\r\n");
	const char* fup = strstr(buf, "\r\n; FixUnitPlacement = 0\r\n");
	const char* fab = strstr(buf, "\r\n; FixAirBases = 1\r\n");   // D235 (2): default 1
	const char* outline = strstr(buf, "\r\n; VisibleAreaOutline = 0\r\n");
	const char* sm = strstr(buf, "\r\n; ShipsMoored = 0\r\n");
	const char* aip = strstr(buf, "\r\n; AirInterceptPatrol = 0\r\n");
	const char* rj = strstr(buf, "\r\n; RoadJunction = 0\r\n");
	const char* rw = strstr(buf, "\r\n; RoadJitter = 0\r\n");
	const char* pg = strstr(buf, "\r\n; PerimeterGarrisons = 0\r\n");   // ticket 4983 (D243): id 32, after the other
	                                                                    // visual additions, before the road options
	CHECK(road && fup && fab && outline && sm && va < fup && fup < fab && fab < outline &&
	      outline < sm && aip && sm < aip && pg && aip < pg && rj && rw && pg < road && road < rj && rj < rw);
	// ticket 4983 (D243, MC #11877): GarrisonVisible is a test value (Def::testOnly): not in the default file at all, and
	// the only such row; its key is still read (no warning) and documented (TestDocs)
	CHECK(strstr(buf, "GarrisonVisible") == NULL);
	for (int i = 0; i < kCount; ++i) CHECK((Definition((Id)i).testOnly != 0) == (i == kGarrisonVisible));
	{
		Options t;
		Sink st;
		const char* gt = "[Extras]\nEnableVisualAdditions = 1\nGarrisonVisible = 1\n";
		CHECK(Parse(gt, strlen(gt), t, Collect, &st) == 0 &&
		      t.Get(kGarrisonVisible) == 1 && t.fromFile[kGarrisonVisible] == 1 && Extra(t, kGarrisonVisible).on);
	}
	CHECK(strstr(buf, "FixRoadLayout") == NULL);   // the old name is nowhere in the file
	CHECK(strstr(ex, "\r\n;  = ") == NULL && strstr(ex, "\r\n; = ") == NULL);   // no reserved row written
	// the round trip: 0 warnings, the defaults
	Options o;
	Sink s;
	CHECK(Parse(buf, n, o, Collect, &s) == 0 && o.EnableBugfixes() == 1 && o.EnableVisualAdditions() == 0 &&
	      o.fromFile[kEnableBugfixes] == 0 && o.fromFile[kEnableVisualAdditions] == 0);
	// uncommenting both switch lines and setting them gives a file that sets them, with no warning
	static char edited[32768];
	strcpy_s(edited, buf);
	char* p = strstr(edited, "; EnableBugfixes = 1");
	char* q = strstr(edited, "; EnableVisualAdditions = 0");
	CHECK(p && q);
	if (p && q)
	{
		p[0] = ' ';
		p[strlen("; EnableBugfixes = ")] = '0';
		q[0] = ' ';
		q[strlen("; EnableVisualAdditions = ")] = '1';
		Sink s2;
		CHECK(Parse(edited, strlen(edited), o, Collect, &s2) == 0 && o.EnableBugfixes() == 0 &&
		      o.EnableVisualAdditions() == 1 && o.fromFile[kEnableBugfixes] == 1);
	}
	// the help lines of the switches as the design wrote them (1.2)
	CHECK(strstr(ex, "\r\n; Fixes for things the game draws wrongly (roads, bridges, buildings on roads, ...).\r\n") != NULL);
	CHECK(strstr(ex, "\r\n;   1 = on: every addition below that is switched on is used. The installer asks you about this.\r\n") !=
	      NULL);
}

// The player documentation names every key, its section and its default.
// Ticket 0680: the text options ([Shell] WindowTitlePrefix): defaults, the quoted and unquoted forms, every refusal with
// its warning, and that the whole-number table is untouched by them.
void TestTextOptions()
{
	Options o;
	CHECK(kWindowTitlePrefix == 0 && kTextCount == 1 && kMaxTextBytes == 64);
	CHECK(strcmp(o.WindowTitlePrefix(), "[VP64] ") == 0 && o.textFromFile[kWindowTitlePrefix] == 0);
	CHECK(o.Text(kWindowTitlePrefix) == o.WindowTitlePrefix());
	const TextDef& d = TextDefinition(kWindowTitlePrefix);
	CHECK(strcmp(d.section, "Shell") == 0 && strcmp(d.key, "WindowTitlePrefix") == 0 &&
	      strcmp(d.defaultValue, "[VP64] ") == 0 && d.help && d.help[0] && d.title && d.title[0]);
	CHECK(FindTextKey("windowtitleprefix", 17) == kWindowTitlePrefix && FindTextKey("WindowTitlePrefi", 16) == kTextCount &&
	      FindTextKey("WindowTitlePrefixx", 18) == kTextCount);
	CHECK(FindKey("WindowTitlePrefix", 17) == kCount);   // not in the whole-number table (installer schema, kCount loops)
	struct Good
	{
		const char* line;
		const char* value;
	};
	const Good good[] = {
		{"WindowTitlePrefix = \"[VP64] \"", "[VP64] "},     // the default file's form: the space kept
		{"WindowTitlePrefix = \"\"", ""},                   // empty: the EXE's title unchanged
		{"WindowTitlePrefix =", ""},                        // empty without quotes too
		{"WindowTitlePrefix = [VP64]", "[VP64]"},           // unquoted: trimmed
		{"WindowTitlePrefix = [VP64]   ; a comment", "[VP64]"},
		{"WindowTitlePrefix = \"a ; b # c \"  ; after", "a ; b # c "},  // quoted: ';' and '#' are part of it
		{"WindowTitlePrefix = \"  two  \"# c", "  two  "},
		{"windowtitleprefix = \"x\"", "x"},                 // the key without regard to case
		{"WindowTitlePrefix = \"\xC3\x89t\xC3\xA9 \"", "\xC3\x89t\xC3\xA9 "},  // UTF-8
		{"WindowTitlePrefix = \"1234567890123456789012345678901234567890123456789012345678901234\"",
		 "1234567890123456789012345678901234567890123456789012345678901234"},  // 64 bytes: the most
	};
	for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); ++i)
	{
		char text[256];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Shell]\n%s\n", good[i].line);
		Sink sg;
		const unsigned n = P(text, o, sg);
		const bool ok = n == 0 && strcmp(o.WindowTitlePrefix(), good[i].value) == 0 && o.textFromFile[kWindowTitlePrefix] == 1;
		if (!ok) printf("  text good '%s': %u warnings, value '%s': %s", good[i].line, n, o.WindowTitlePrefix(), sg.text);
		CHECK(ok);
	}
	struct Bad
	{
		const char* line;
		const char* warning;
	};
	const Bad bad[] = {
		{"WindowTitlePrefix = \"[VP64] ", "has no closing double quote"},
		{"WindowTitlePrefix = \"a\" b", "has text after its closing double quote"},
		{"WindowTitlePrefix = \"12345678901234567890123456789012345678901234567890123456789012345\"", "longer than 64 bytes"},
		{"WindowTitlePrefix = \"a\tb\"", "a control character"},
		{"WindowTitlePrefix = \"\x7F\"", "a control character"},
		{"WindowTitlePrefix = \"\xC2\x85\"", "a control character"},   // U+0085, a C1 control
		{"WindowTitlePrefix = \"\xC3(\"", "not valid UTF-8"},
		{"WindowTitlePrefix = \"\xC0\xAF\"", "not valid UTF-8"},       // overlong
		{"WindowTitlePrefix = \"\xED\xA0\x80\"", "not valid UTF-8"},   // a surrogate
		{"WindowTitlePrefix = \"\xF4\x90\x80\x80\"", "not valid UTF-8"},  // above U+10FFFF
		{"WindowTitlePrefix = \"\xE2\x82\"", "not valid UTF-8"},       // cut short
	};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
	{
		char text[256];
		_snprintf_s(text, sizeof(text), _TRUNCATE, "[Shell]\nWindowTitlePrefix = \"x\"\n%s\n", bad[i].line);
		Sink sb;
		const unsigned n = P(text, o, sb);
		// the earlier valid line is not kept: the line written last decides (as for the whole numbers)
		const bool ok = n == 2 && sb.Has("is set again") && sb.Has(bad[i].warning) &&
		                strcmp(o.WindowTitlePrefix(), "[VP64] ") == 0 && o.textFromFile[kWindowTitlePrefix] == 0;
		if (!ok) printf("  text bad #%u: %u warnings, value '%s': %s", (unsigned)i, n, o.WindowTitlePrefix(), sb.text);
		CHECK(ok);
	}
	Sink s1;  // the first bad case's exact warning names the line
	CHECK(P("[Shell]\nWindowTitlePrefix = \"[VP64] \n", o, s1) == 1 &&
	      s1.Has("line 2: WindowTitlePrefix = \"[VP64]: has no closing double quote; the default \"[VP64] \" is used"));
	Sink s2;  // another section: read with a warning; before any section too
	CHECK(P("[Renderer]\nWindowTitlePrefix = \"r\"\n", o, s2) == 1 && strcmp(o.WindowTitlePrefix(), "r") == 0 &&
	      s2.Has("line 2: WindowTitlePrefix belongs in [Shell], not [Renderer]; it is read anyway"));
	Sink s3;
	CHECK(P("WindowTitlePrefix = \"q\"\n", o, s3) == 1 && strcmp(o.WindowTitlePrefix(), "q") == 0 &&
	      s3.Has("line 1: WindowTitlePrefix comes before any section; it belongs in [Shell] and is read anyway"));
	Sink s4;  // [Shell] is a known section; a repeated key: the last counts
	CHECK(P("[Shell]\nWindowTitlePrefix = \"a\"\nWindowTitlePrefix = \"b\"\n", o, s4) == 1 &&
	      strcmp(o.WindowTitlePrefix(), "b") == 0 && !s4.Has("no option belongs") &&
	      s4.Has("line 3: WindowTitlePrefix is set again (first on line 2); the last one counts"));
	Sink s5;  // a text option beside whole numbers: each read by its own rule
	CHECK(P("[Renderer]\nServerRenderer = 1\n[Shell]\nWindowTitlePrefix = \"[A] \"\n", o, s5) == 0 &&
	      o.ServerRenderer() == 1 && strcmp(o.WindowTitlePrefix(), "[A] ") == 0);
	o.SetDefaults();
	CHECK(strcmp(o.WindowTitlePrefix(), "[VP64] ") == 0 && o.textFromFile[kWindowTitlePrefix] == 0);
	// the default file: [Shell] with the commented default in quotes, before [Extras]; uncommented it sets the default
	static char buf[32768];
	const size_t n = DefaultFileText(buf, sizeof(buf));
	CHECK(n < sizeof(buf));
	const char* shell = strstr(buf, "\r\n[Shell]\r\n");
	const char* line = strstr(buf, "\r\n; WindowTitlePrefix = \"[VP64] \"\r\n");
	const char* extras = strstr(buf, "\r\n[Extras]\r\n");
	CHECK(shell && line && extras && shell < line && line < extras);
	static char edited[32768];
	strcpy_s(edited, buf);
	char* p = strstr(edited, "; WindowTitlePrefix = ");
	CHECK(p != NULL);
	if (p)
	{
		memmove(p, p + 2, strlen(p + 2) + 1);
		Sink se;
		CHECK(Parse(edited, strlen(edited), o, Collect, &se) == 0 && strcmp(o.WindowTitlePrefix(), "[VP64] ") == 0 &&
		      o.textFromFile[kWindowTitlePrefix] == 1);
	}
	// TextValueValid directly: NULL why is allowed
	CHECK(TextValueValid("abc", 3, NULL) && !TextValueValid("a\nb", 3, NULL) && TextValueValid("", 0, NULL));
	const char* why = NULL;
	CHECK(!TextValueValid("\xFF", 1, &why) && why && strcmp(why, "not valid UTF-8") == 0);
}

// Ticket 0613 (MC #13471): [Extras] FixUnknownWonderBase, id 14, a bugfix, 1 by default (D169): in effect by default, off
// with its own 0 or with EnableBugfixes = 0; a bad value gives the default with one warning; written in the [Extras] block.
void TestFixUnknownWonderBase()
{
	Options o;
	CHECK(o.Get(kFixUnknownWonderBase) == 1 && o.fromFile[kFixUnknownWonderBase] == 0);
	CHECK(FindKey("fixunknownwonderbase", 20) == kFixUnknownWonderBase && FindKey("FixUnknownWonderBas", 19) == kCount);
	ExtraState e = Extra(o, kFixUnknownWonderBase);
	CHECK(e.on && e.value == 1 && e.why == kExtraOn);
	Sink s;
	CHECK(P("[Extras]\nFixUnknownWonderBase = 0\n", o, s) == 0 && o.Get(kFixUnknownWonderBase) == 0 &&
	      o.fromFile[kFixUnknownWonderBase] == 1);
	e = Extra(o, kFixUnknownWonderBase);
	CHECK(!e.on && e.value == 0 && e.why == kExtraFlagOff && e.decidedBy == kFixUnknownWonderBase);
	Sink s1;
	CHECK(P("[Extras]\nEnableBugfixes = 0\n", o, s1) == 0 && o.Get(kFixUnknownWonderBase) == 1);
	e = Extra(o, kFixUnknownWonderBase);
	CHECK(!e.on && e.why == kExtraCategoryOff && e.decidedBy == kEnableBugfixes);
	Sink s2;
	CHECK(P("[Extras]\nFixUnknownWonderBase = 2\n", o, s2) == 1 && o.Get(kFixUnknownWonderBase) == 1 &&
	      o.fromFile[kFixUnknownWonderBase] == 0);
	CHECK(s2.Has("line 2: FixUnknownWonderBase = 2: not a whole number from 0 to 1; the default 1 is used"));
	static char buf[32768];
	const size_t n = DefaultFileText(buf, sizeof(buf));
	const char* extras = strstr(buf, "\r\n[Extras]\r\n");
	const char* line = strstr(buf, "\r\n; FixUnknownWonderBase = 1\r\n");
	CHECK(n < sizeof(buf) && extras && line && extras < line);
	char d[300];
	DescribeExtra(Options(), kFixUnknownWonderBase, d, sizeof(d));
	CHECK(strstr(d, "FixUnknownWonderBase: in effect") == d);
}

// Ticket 0727: [Extras] FixMinimapUnexplored, id 33, a bugfix, 1 by default (D169): in effect by default, off with its own
// 0 or with EnableBugfixes = 0; a bad value gives the default with one warning; written in the [Extras] block after the
// other bugfixes and before the visual additions.
void TestFixMinimapUnexplored()
{
	Options o;
	CHECK(o.Get(kFixMinimapUnexplored) == 1 && o.fromFile[kFixMinimapUnexplored] == 0);
	CHECK(FindKey("fixminimapunexplored", 20) == kFixMinimapUnexplored && FindKey("FixMinimapUnexplore", 19) == kCount);
	const Def& d = Definition(kFixMinimapUnexplored);
	CHECK(strcmp(d.section, "Extras") == 0 && d.category == kCatBugfix && d.needsKey == NULL && d.defaultValue == 1 &&
	      d.minValue == 0 && d.maxValue == 1 && d.title && strlen(d.title) <= 45 && d.testOnly == 0);
	ExtraState e = Extra(o, kFixMinimapUnexplored);
	CHECK(e.on && e.value == 1 && e.why == kExtraOn && e.decidedBy == kFixMinimapUnexplored);
	Sink s;
	CHECK(P("[Extras]\nFixMinimapUnexplored = 0\n", o, s) == 0 && o.Get(kFixMinimapUnexplored) == 0 &&
	      o.fromFile[kFixMinimapUnexplored] == 1);
	e = Extra(o, kFixMinimapUnexplored);
	CHECK(!e.on && e.value == 0 && e.why == kExtraFlagOff && e.decidedBy == kFixMinimapUnexplored);
	Sink s1;
	CHECK(P("[Extras]\nEnableBugfixes = 0\n", o, s1) == 0 && o.Get(kFixMinimapUnexplored) == 1);
	e = Extra(o, kFixMinimapUnexplored);
	CHECK(!e.on && e.why == kExtraCategoryOff && e.decidedBy == kEnableBugfixes);
	Sink s2;
	CHECK(P("[Extras]\nFixMinimapUnexplored = 2\n", o, s2) == 1 && o.Get(kFixMinimapUnexplored) == 1 &&
	      o.fromFile[kFixMinimapUnexplored] == 0);
	CHECK(s2.Has("line 2: FixMinimapUnexplored = 2: not a whole number from 0 to 1; the default 1 is used"));
	Sink s3;   // outside its home section: honoured with a warning
	CHECK(P("[Renderer]\nFixMinimapUnexplored = 0\n", o, s3) == 1 && o.Get(kFixMinimapUnexplored) == 0);
	static char buf[32768];
	const size_t n = DefaultFileText(buf, sizeof(buf));
	const char* extras = strstr(buf, "\r\n[Extras]\r\n");
	const char* fab = strstr(buf, "\r\n; FixAirBases = 1\r\n");
	const char* line = strstr(buf, "\r\n; FixMinimapUnexplored = 1\r\n");
	const char* outline = strstr(buf, "\r\n; VisibleAreaOutline = 0\r\n");
	CHECK(n < sizeof(buf) && extras && line && fab && outline && extras < line && fab < line && line < outline);
	char dd[300];
	DescribeExtra(Options(), kFixMinimapUnexplored, dd, sizeof(dd));
	CHECK(strstr(dd, "FixMinimapUnexplored: in effect") == dd);
	Options off;
	Sink s4;
	CHECK(P("[Extras]\nEnableBugfixes = 0\n", off, s4) == 0);
	DescribeExtra(off, kFixMinimapUnexplored, dd, sizeof(dd));
	CHECK(strcmp(dd, "FixMinimapUnexplored: off: EnableBugfixes = 0 (from the file) switches off every bugfix") == 0);
}

// Ticket 0755 (D255): [Extras] FixSmoothWheel, id 37, a bugfix, 1 by default as the ticket proposes (the owner confirms at
// the merge; kApprovedDefaults): in effect by default, off with its own 0 or with EnableBugfixes = 0; a bad value gives
// the default with one warning; written in the [Extras] block after the other bugfixes and before the visual additions.
void TestFixSmoothWheel()
{
	Options o;
	CHECK(o.Get(kFixSmoothWheel) == 1 && o.fromFile[kFixSmoothWheel] == 0);
	CHECK(FindKey("fixsmoothwheel", 14) == kFixSmoothWheel && FindKey("FixSmoothWhee", 13) == kCount);
	const Def& d = Definition(kFixSmoothWheel);
	CHECK(strcmp(d.section, "Extras") == 0 && d.category == kCatBugfix && d.needsKey == NULL && d.defaultValue == 1 &&
	      d.minValue == 0 && d.maxValue == 1 && d.title && strlen(d.title) <= 45 && d.testOnly == 0 &&
	      d.valueCategory == NULL);
	ExtraState e = Extra(o, kFixSmoothWheel);
	CHECK(e.on && e.value == 1 && e.why == kExtraOn && e.decidedBy == kFixSmoothWheel);
	Sink s;
	CHECK(P("[Extras]\nFixSmoothWheel = 0\n", o, s) == 0 && o.Get(kFixSmoothWheel) == 0 && o.fromFile[kFixSmoothWheel] == 1);
	e = Extra(o, kFixSmoothWheel);
	CHECK(!e.on && e.value == 0 && e.why == kExtraFlagOff && e.decidedBy == kFixSmoothWheel);
	Options o1;
	Sink s1;
	CHECK(P("[Extras]\nEnableBugfixes = 0\n", o1, s1) == 0 && o1.Get(kFixSmoothWheel) == 1);
	e = Extra(o1, kFixSmoothWheel);
	CHECK(!e.on && e.why == kExtraCategoryOff && e.decidedBy == kEnableBugfixes);
	Options o2;
	Sink s2;
	CHECK(P("[Extras]\nFixSmoothWheel = 2\n", o2, s2) == 1 && o2.Get(kFixSmoothWheel) == 1 && o2.fromFile[kFixSmoothWheel] == 0);
	CHECK(s2.Has("line 2: FixSmoothWheel = 2: not a whole number from 0 to 1; the default 1 is used"));
	Options o3;
	Sink s3;
	CHECK(P("[Extras]\nFixSmoothWheel = -1\n", o3, s3) == 1 && o3.Get(kFixSmoothWheel) == 1);
	Options o5;
	Sink s5;   // outside its home section: honoured with a warning
	CHECK(P("[Renderer]\nFixSmoothWheel = 0\n", o5, s5) == 1 && o5.Get(kFixSmoothWheel) == 0);
	static char buf[32768];
	const size_t n = DefaultFileText(buf, sizeof(buf));
	const char* extras = strstr(buf, "\r\n[Extras]\r\n");
	const char* mm = strstr(buf, "\r\n; FixMinimapUnexplored = 1\r\n");
	const char* line = strstr(buf, "\r\n; FixSmoothWheel = 1\r\n");
	const char* outline = strstr(buf, "\r\n; VisibleAreaOutline = 0\r\n");
	CHECK(n < sizeof(buf) && extras && line && mm && outline && extras < mm && mm < line && line < outline);
	CHECK(strstr(buf, "\r\n;   0 = off: as the game does it.\r\n; FixSmoothWheel = 1\r\n") != NULL);
	Options back;
	Sink s6;
	CHECK(Parse(buf, n, back, Collect, &s6) == 0 && back.Get(kFixSmoothWheel) == 1 && back.fromFile[kFixSmoothWheel] == 0);
	char dd[300];
	DescribeExtra(Options(), kFixSmoothWheel, dd, sizeof(dd));
	CHECK(strstr(dd, "FixSmoothWheel: in effect") == dd);
	DescribeExtra(o1, kFixSmoothWheel, dd, sizeof(dd));
	CHECK(strcmp(dd, "FixSmoothWheel: off: EnableBugfixes = 0 (from the file) switches off every bugfix") == 0);
}

void TestDocs(const char* docsPath)
{
	FILE* f = NULL;
	CHECK(fopen_s(&f, docsPath, "rb") == 0 && f);
	if (!f) return;
	static char doc[65536];
	const size_t n = fread(doc, 1, sizeof(doc) - 1, f);
	fclose(f);
	doc[n] = 0;
	CHECK(n > 0);
	for (int i = 0; i < kCount; ++i)
	{
		const Def& d = Definition((Id)i);
		if (!d.key[0]) continue;   // a reserved id (ticket 4875): no key, nothing documented
		char want[200];
		_snprintf_s(want, sizeof(want), _TRUNCATE, "%s = %d", d.key, d.defaultValue);
		const bool hasLine = strstr(doc, want) != NULL;
		_snprintf_s(want, sizeof(want), _TRUNCATE, "[%s]", d.section);
		const bool hasSection = strstr(doc, want) != NULL;
		if (!hasLine || !hasSection) printf("  docs: %s or [%s] missing in %s\n", d.key, d.section, docsPath);
		CHECK(hasLine && hasSection);
	}
	for (int i = 0; i < kTextCount; ++i)  // ticket 0680: the text options, with the default in quotes
	{
		const TextDef& d = TextDefinition((TextId)i);
		char want[200];
		_snprintf_s(want, sizeof(want), _TRUNCATE, "%s = \"%s\"", d.key, d.defaultValue);
		const bool hasLine = strstr(doc, want) != NULL;
		_snprintf_s(want, sizeof(want), _TRUNCATE, "[%s]", d.section);
		const bool hasSection = strstr(doc, want) != NULL;
		if (!hasLine || !hasSection) printf("  docs: %s or [%s] missing in %s\n", d.key, d.section, docsPath);
		CHECK(hasLine && hasSection);
	}
	CHECK(strstr(doc, "VP64Options.ini") != NULL);
	// ticket 4931: the Extras section of the document, and an entry for each switch
	CHECK(strstr(doc, "\n## Extras") != NULL && strstr(doc, "### [Extras] EnableBugfixes") != NULL &&
	      strstr(doc, "### [Extras] EnableVisualAdditions") != NULL && strstr(doc, "EnableGameplayChanges") != NULL);
	CHECK(n < sizeof(doc) - 1);   // the whole document was read (it is about 22 KB)
}
// One-off 9081 (the owner, 2026-11-21): every row with a key has a short title for the installer's options page: 1 to 45
// characters, no trailing period, no tab or line break; a reserved row (an empty key) has none.
void TestTitles()
{
	int n = 0;
	for (int i = 0; i < kCount; ++i)
	{
		const Def& d = Definition((Id)i);
		if (!d.key[0])
		{
			if (d.title) printf("  titles: reserved row %d has a title\n", i);
			CHECK(d.title == NULL);
			continue;
		}
		const size_t len = d.title ? strlen(d.title) : 0;
		const bool ok = d.title && len >= 1 && len <= 45 && d.title[len - 1] != '.' && !strchr(d.title, '\t') &&
		                !strchr(d.title, '\n') && !strchr(d.title, '\r');
		if (!ok) printf("  titles: %s: '%s' is no title of 1-45 characters without a period, tab or line break\n", d.key,
		                d.title ? d.title : "(none)");
		CHECK(ok);
		++n;
	}
	CHECK(n > 0);
	for (int i = 0; i < kTextCount; ++i)  // ticket 0680: the text options' titles follow the same rule
	{
		const TextDef& d = TextDefinition((TextId)i);
		const size_t len = d.title ? strlen(d.title) : 0;
		CHECK(d.title && len >= 1 && len <= 45 && d.title[len - 1] != '.' && !strchr(d.title, '\t') && !strchr(d.title, '\n'));
	}
}
}  // namespace

int main(int argc, char** argv)
{
	if (argc != 3)
	{
		printf("usage: test_options <docs/OPTIONS.md> <scratch folder>\n");
		return 2;
	}
	if (MultiByteToWideChar(CP_ACP, 0, argv[2], -1, g_scratch, MAX_PATH) <= 0) return 2;
	CreateDirectoryW(g_scratch, NULL);
	printf("vp64 options tests (%d-bit)\n", (int)(sizeof(void*) * 8));
	TestDefaults();
	TestValid();
	TestBadValues();
	TestStructure();
	TestDefaultText();
	TestFiles();
	TestConversions();
	TestDefaultPath();
	TestServerRenderer();
	TestWhiteFogMask();
	TestUiDrawListGrowth();
	TestServerUnits();
	TestServerArc();
	TestServerPath();
	TestServerFarmFields();
	TestAiUpdateTickMs();
	TestAiSkipVisualDrain();
	TestAiDrainUnordered();
	TestModelOptions();
	TestGameEventsOneCrossing();
	TestHiddenEmbarks();
	TestServerCombat();
	TestGuardsWhenOff();
	TestD255Options();      // ticket 0745
	TestKeepRuns();         // ticket 0747
	TestHideTreadEffect();  // ticket 0784
	TestModelStore();       // ticket 0789
	TestExtrasTable();
	TestExtrasSwitches();
	TestExtraOf();
	TestExtra();
	TestFixUnknownWonderBase();   // ticket 0613
	TestFixMinimapUnexplored();   // ticket 0727
	TestFixSmoothWheel();         // ticket 0755
	TestDescribeExtra();
	TestDefaultTextExtras();
	TestTitles();
	TestTextOptions();
	TestDocs(argv[1]);
	printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
