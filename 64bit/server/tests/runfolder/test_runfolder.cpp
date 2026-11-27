// test_runfolder.cpp - ticket 0488: the run-folder check of server/src/srv_ifaces.cpp (block "0488 RUN FOLDER", extracted
// by run_runfolder_test.py into runfolder.gen.h). Usage: test_runfolder.exe WORKDIR. WORKDIR must exist; the program
// makes WORKDIR\run\cache, WORKDIR\runner and WORKDIR\Long Folder Name 0488 in it. One line per case: "PASS name" or
// "FAIL name -- detail"; a last line "RESULT n/m"; exit 0 when every case passes, 1 otherwise, 2 on a set-up error.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#include <string>
#include "runfolder.gen.h"

static int s_nPass = 0, s_nAll = 0;

static void Check(const char* szName, bool bOk, const std::wstring& detail = L"")
{
	++s_nAll;
	if(bOk)
	{
		++s_nPass;
		printf("PASS %s\n", szName);
	}
	else
		printf("FAIL %s -- %ls\n", szName, detail.c_str());
	fflush(stdout);
}

static std::wstring Slashes(std::wstring s)
{
	for(size_t i = 0; i < s.size(); ++i)
		if(s[i] == L'\\')
			s[i] = L'/';
	return s;
}

static std::wstring Upper(std::wstring s)
{
	for(size_t i = 0; i < s.size(); ++i)
		s[i] = (wchar_t)towupper(s[i]);
	return s;
}

static std::wstring Short(const std::wstring& s)
{
	wchar_t b[1024];
	const DWORD n = GetShortPathNameW(s.c_str(), b, 1024);
	return n && n < 1024 ? std::wstring(b, n) : std::wstring();
}

static void Expect(const char* szName, const std::wstring& file, const std::wstring& root, bool bInside)
{
	const bool b = runfolder::Inside(file.c_str(), root.c_str());
	Check(szName, b == bInside, L"Inside(\"" + file + L"\", \"" + root + L"\") = " + (b ? L"true" : L"false"));
}

int wmain(int argc, wchar_t** argv)
{
	if(argc < 2)
	{
		printf("usage: test_runfolder.exe WORKDIR\n");
		return 2;
	}
	std::wstring w;
	if(!runfolder::Normalise(argv[1], w))
	{
		printf("cannot normalise WORKDIR\n");
		return 2;
	}
	const std::wstring run = w + L"\\run", cache = run + L"\\cache", runner = w + L"\\runner";
	const std::wstring longDir = w + L"\\Long Folder Name 0488";
	CreateDirectoryW(run.c_str(), NULL);
	CreateDirectoryW(cache.c_str(), NULL);
	CreateDirectoryW(runner.c_str(), NULL);
	CreateDirectoryW(longDir.c_str(), NULL);
	CreateDirectoryW((longDir + L"\\cache").c_str(), NULL);
	if(GetFileAttributesW(cache.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		printf("cannot make %ls\n", cache.c_str());
		return 2;
	}
	const std::wstring db = cache + L"\\Civ5SavedGameDatabase.db";

	// Separators: the incident's form (run_dir with '/', the DLL's path with '\' after it) and the others.
	Expect("mixed: root with '/', file with '\\' (the 2026-11-17 incident)", db, Slashes(run), true);
	Expect("mixed: root '/' up to the last part, '\\' after it (server.cfg of 4664)", db,
	       Slashes(w) + L"\\run", true);
	Expect("mixed: root '\\', file '/'", Slashes(db), run, true);
	Expect("mixed: file with '/' and '\\' mixed", Slashes(cache) + L"\\Civ5SavedGameDatabase.db", run, true);
	Expect("root with a trailing '\\'", db, run + L"\\", true);
	Expect("root with a trailing '/'", db, Slashes(run) + L"/", true);
	Expect("repeated separators", w + L"\\\\run\\\\cache\\Civ5SavedGameDatabase.db", run, true);
	Expect("case: root upper case", db, Upper(run), true);
	Expect("case: file upper case", Upper(db), run, true);
	Expect("dots: file through cache\\..\\cache", cache + L"\\..\\cache\\Civ5SavedGameDatabase.db", run, true);
	Expect("a file that does not exist yet in a folder that does not exist yet", run + L"\\new\\deeper\\x.db", run,
	       true);

	// Short (8.3) names: the folder made here, or C:\Program Files when the volume makes none.
	std::wstring longRoot = longDir, shortRoot = Short(longDir);
	if(shortRoot.empty() || shortRoot == longRoot)
	{
		longRoot = L"C:\\Program Files";
		shortRoot = Short(longRoot);
	}
	const bool bHaveShort = !shortRoot.empty() && Upper(shortRoot) != Upper(longRoot);
	Check("short names: a folder with an 8.3 name exists to test with", bHaveShort, longRoot + L" -> " + shortRoot);
	if(bHaveShort)
	{
		Expect("short: root short, file long", longRoot + L"\\cache\\Civ5SavedGameDatabase.db", shortRoot, true);
		Expect("short: root long, file short", shortRoot + L"\\cache\\Civ5SavedGameDatabase.db", longRoot, true);
		Expect("short: root short with '/', file long", longRoot + L"\\cache\\x.db", Slashes(shortRoot), true);
		Expect("short: outside stays outside", w + L"\\run\\x.db", shortRoot, false);
	}

	// Outside: refused.
	Expect("outside: a sibling whose name starts with the root's (run -> runner)", runner + L"\\x.db", run, false);
	Expect("outside: '..' out of the root", run + L"\\..\\runner\\x.db", run, false);
	Expect("outside: '..' out of the root, '/' form", Slashes(run) + L"/../runner/x.db", Slashes(run), false);
	Expect("outside: the root folder itself", run, run, false);
	Expect("outside: another drive's folder", L"C:\\Windows\\Temp\\x.db", run, false);
	Expect("outside: the parent of the root", w + L"\\x.db", run, false);
	Expect("outside: an empty root", db, L"", false);
	Check("outside: a NULL file", !runfolder::Inside(NULL, run.c_str()));
	Check("outside: a NULL root", !runfolder::Inside(db.c_str(), NULL));
	std::wstring n;
	Check("Normalise: an empty path is refused", !runfolder::Normalise(L"", n));
	Check("Normalise: '/' becomes '\\', no trailing separator",
	      runfolder::Normalise((Slashes(run) + L"/").c_str(), n) && Upper(n) == Upper(run), n);
	Check("Normalise: a drive root keeps its form", runfolder::Normalise(L"C:/", n) && n.size() >= 2 && n[1] == L':',
	      n);

	printf("RESULT %d/%d\n", s_nPass, s_nAll);
	return s_nPass == s_nAll ? 0 : 1;
}
