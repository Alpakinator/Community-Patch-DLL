// Unit test of the server's VFS resolution rule (bug 0100). Built and run by server/tests/run_vfs_test.py, which makes
// the fake game folder (argv[1]) described there and compiles server/src/srv_vfs.cpp with /DSRV_VFS_UNIT_TEST.
// Each file of the fake folder holds one word naming its copy ("base", "vp", "low", ...); a case passes when VfsResolve
// returns the expected result and, for a hit, the file it names holds the expected word.
#include "vfs_test_common.h"
#include <stdio.h>

static int s_nFail = 0, s_nPass = 0;

static std::string Trim(const std::string& s)
{
	size_t e = s.size();
	while(e > 0 && (s[e - 1] == '\r' || s[e - 1] == '\n' || s[e - 1] == ' '))
		--e;
	return s.substr(0, e);
}

// szWant: the word the resolved file must hold, or NULL when the name must not resolve.
static void Case(const char* szWhat, const char* szName, const char* szWant)
{
	std::string full, vfs, data;
	const bool bHit = VfsResolve(szName, full, vfs);
	bool bOk;
	std::string got;
	if(!bHit)
	{
		got = "(not found)";
		bOk = szWant == NULL;
	}
	else
	{
		got = VfsReadFile(full, data) ? Trim(data) : std::string("(unreadable)");
		bOk = szWant != NULL && got == szWant;
	}
	printf("%s %-58s name=\"%s\" want=%s got=%s vfs=%s\n", bOk ? "PASS" : "FAIL", szWhat, szName ? szName : "(null)",
	       szWant ? szWant : "(not found)", got.c_str(), bHit ? vfs.c_str() : "-");
	++(bOk ? s_nPass : s_nFail);
}

int main(int argc, char** argv)
{
	if(argc != 2)
	{
		fprintf(stderr, "usage: test_vfs_resolve <fake game dir>\n");
		return 2;
	}
	std::string why;
	if(!VfsInit(argv[1], why))
	{
		printf("FAIL VfsInit: %s\n", why.c_str());
		return 1;
	}
	printf("VfsInit: %s\n", why.c_str());
	const std::string game = argv[1];
	const std::string absBase = game + "\\Assets\\Maps\\Communitu_79a.lua";
	const std::string absMissing = game + "\\NoSuchFolder\\Communitu_79a.lua";

	// The bug: a script name present in the base game and in two packages; the highest priority wins over the
	// literal relative path (engine-contract.md 10.3; civ5-headless ResolveFile step 2 before step 3).
	Case("relative base path, name in two packages + base", "Assets/Maps/Communitu_79a.lua", "vp");
	Case("same with backslashes", "Assets\\Maps\\Communitu_79a.lua", "vp");
	Case("relative path into the lower package", "Assets/DLC/Low/Maps/Communitu_79a.lua", "vp");
	Case("bare name", "Communitu_79a.lua", "vp");
	Case("bare name, other case, no extension", "COMMUNITU_79A", "vp");
	Case("absolute path is taken as it is", absBase.c_str(), "base");
	Case("absolute path missing: falls back to the name (as before 0100)", absMissing.c_str(), "vp");
	Case("name only in the base game", "Assets/Maps/Continents.lua", "basecont");
	Case("include name inside one package: later in the walk wins", "Shared.lua", "low-ui");
	Case("relative path to a file outside the index (literal fallback)", "Assets/DLC/Expansion/Maps/OnlyInExpansion.lua", "exp");
	Case("relative path outside Assets (literal fallback)", "Extra/Outside.lua", "outside");
	// Negative cases.
	Case("NEG: name only in a package left out (Expansion)", "OnlyInExpansion.lua", NULL);
	Case("NEG: relative path to a missing file", "Assets/Maps/NoSuch.lua", NULL);
	Case("NEG: missing bare name", "NoSuch", NULL);
	Case("NEG: empty name", "", NULL);
	Case("NEG: null name", NULL, NULL);
	Case("NEG: missing absolute path of a missing name", (game + "\\NoSuchFolder\\NoSuch.lua").c_str(), NULL);

	printf("vfs resolve test: %d passed, %d failed\n", s_nPass, s_nFail);
	return s_nFail == 0 ? 0 : 1;
}
