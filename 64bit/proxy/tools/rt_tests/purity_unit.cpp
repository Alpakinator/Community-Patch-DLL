// purity_unit.cpp - unit test of proxy/src/proxy_purity.cpp (issue 3503): the purity file loader, every refusal path,
// and the interim test load. Built by proxy/tools/test_purity.py with the proxy's toolchain and once under MSVC
// /fsanitize=address. Usage: purity_unit.exe <scratch folder>. Prints one line per check and
// "purity_unit: N/M checks passed"; exit 0 only when all passed.
// The proxy's STL layout (proxy_rt_logic.h; proxy_purity.cpp does the same), so that this test links like the proxy.
#if defined(_MSC_VER) && !defined(_SECURE_SCL)
#define _SECURE_SCL 0
#endif
#if defined(_MSC_VER) && !defined(_HAS_ITERATOR_DEBUGGING)
#define _HAS_ITERATOR_DEBUGGING 0
#endif
#include "proxy_purity.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

namespace ProxyPurity
{
bool LoadFromTestEnvironment(std::string* why);
bool LoadBesideX64(const char* x64Dir, std::string* why);
}

static int g_n = 0, g_ok = 0;
static std::string g_dir;
static const char* const kSha = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";  // SHA-256("abc")
static const char* const kOther = "0000000000000000000000000000000000000000000000000000000000000000";

static void Check(const char* name, bool ok, const std::string& detail = std::string())
{
	++g_n;
	if (ok) ++g_ok;
	printf("%s %s%s%s\n", ok ? "ok  " : "FAIL", name, detail.empty() ? "" : "  [", detail.empty() ? "" : (detail + "]").c_str());
}

static std::string Write(const char* name, const std::string& body)
{
	const std::string p = g_dir + "\\" + name;
	FILE* f = NULL;
	if (fopen_s(&f, p.c_str(), "wb") != 0 || !f)
	{
		printf("FAIL cannot write %s\n", p.c_str());
		exit(2);
	}
	if (!body.empty()) fwrite(body.data(), 1, body.size(), f);
	fclose(f);
	return p;
}

static std::string Head(const char* sha = kSha) { return std::string("# vp64-purity 1\ndll_sha256\t") + sha + "\nderived_from\taaa\tbbb\n"; }

static const char* const kBody = "method\tICvCity1::GetOwner() const\nmethod\tICvGame1::GetPlayer(const PlayerTypes)\n"
                                 "lua\tPlot.GetX\nlua\tGame.GetGameTurn\n";

// A refusal: Load returns false, both lists are empty (also after an earlier good load), `why` names the reason.
static void Refused(const char* name, const std::string& body, const char* expect, const char* sha = kSha)
{
	std::string why;
	const std::string p = Write("good.tsv", Head() + kBody);
	ProxyPurity::Load(p.c_str(), kSha, &why);  // lists full first: a refusal must empty them
	const std::string q = Write("case.tsv", body);
	const bool r = ProxyPurity::Load(q.c_str(), sha, &why);
	const bool ok = !r && ProxyPurity::MethodCount() == 0 && ProxyPurity::LuaCount() == 0 &&
	                !ProxyPurity::IsPureMethodKey("ICvCity1::GetOwner() const") && !ProxyPurity::IsPureLuaName("Plot.GetX") &&
	                why.find(expect) != std::string::npos && why.find("every call crosses") != std::string::npos;
	Check(name, ok, why);
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		printf("usage: purity_unit.exe <scratch folder>\n");
		return 2;
	}
	g_dir = argv[1];
	CreateDirectoryA(g_dir.c_str(), NULL);
	std::string why;

	// ---- nothing loaded yet
	Check("initial: not attempted, lists empty", !ProxyPurity::LoadAttempted() && ProxyPurity::MethodCount() == 0 &&
	                                                 !ProxyPurity::IsPureMethodKey("ICvCity1::GetOwner() const"));
	Check("NULL queries are false", !ProxyPurity::IsPureMethodKey(NULL) && !ProxyPurity::IsPureLuaName(NULL));

	// ---- good files
	std::string p = Write("good.tsv", Head() + kBody);
	bool r = ProxyPurity::Load(p.c_str(), kSha, &why);
	Check("good: loaded", r && ProxyPurity::LoadAttempted(), why);
	Check("good: counts 2 + 2", ProxyPurity::MethodCount() == 2 && ProxyPurity::LuaCount() == 2);
	Check("good: listed keys pure", ProxyPurity::IsPureMethodKey("ICvCity1::GetOwner() const") &&
	                                    ProxyPurity::IsPureMethodKey("ICvGame1::GetPlayer(const PlayerTypes)") &&
	                                    ProxyPurity::IsPureLuaName("Plot.GetX") && ProxyPurity::IsPureLuaName("Game.GetGameTurn"));
	Check("good: unlisted and cross-kind keys not pure", !ProxyPurity::IsPureMethodKey("ICvCity1::GetOwner()") &&
	                                                         !ProxyPurity::IsPureMethodKey("Plot.GetX") &&
	                                                         !ProxyPurity::IsPureLuaName("ICvCity1::GetOwner() const") &&
	                                                         !ProxyPurity::IsPureLuaName("Plot.GetY"));
	Check("good: why names counts and the binding", why.find("2 method keys, 2 Lua names") != std::string::npos &&
	                                                     why.find("ba7816bf8f01") != std::string::npos, why);
	r = ProxyPurity::Load(p.c_str(), "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", &why);
	Check("good: the DLL hash in upper case", r && ProxyPurity::MethodCount() == 2, why);
	{
		std::string crlf = "# vp64-purity 1\r\ndll_sha256\tBA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD\r\n"
		                   "derived_from\taaa\tbbb\r\n\r\n# a comment\r\nmethod\tICvUnit1::~ICvUnit1()\r\nlua\tUnit.GetX";
		p = Write("crlf.tsv", crlf);
		r = ProxyPurity::Load(p.c_str(), kSha, &why);
		Check("good: CRLF, blank and comment lines, upper-case hash in the file, no final newline, a destructor key",
		      r && ProxyPurity::MethodCount() == 1 && ProxyPurity::LuaCount() == 1 &&
		          ProxyPurity::IsPureMethodKey("ICvUnit1::~ICvUnit1()") && ProxyPurity::IsPureLuaName("Unit.GetX"), why);
	}
	p = Write("empty-lists.tsv", Head());
	r = ProxyPurity::Load(p.c_str(), kSha, &why);
	Check("good: header only (everything dropped): loaded, empty", r && ProxyPurity::MethodCount() == 0 &&
	                                                                  ProxyPurity::LuaCount() == 0, why);
	p = Write("good.tsv", Head() + kBody);
	ProxyPurity::Load(p.c_str(), kSha, NULL);
	ProxyPurity::Clear();
	Check("Clear empties both lists", ProxyPurity::MethodCount() == 0 && ProxyPurity::LuaCount() == 0 &&
	                                      !ProxyPurity::IsPureLuaName("Plot.GetX"));

	// ---- refusals of the loader's arguments
	{
		ProxyPurity::Load(p.c_str(), kSha, NULL);
		r = ProxyPurity::Load(NULL, kSha, &why);
		Check("refused: NULL path", !r && ProxyPurity::MethodCount() == 0 && why.find("no file named") != std::string::npos, why);
		ProxyPurity::Load(p.c_str(), kSha, NULL);
		r = ProxyPurity::Load("", kSha, &why);
		Check("refused: empty path", !r && ProxyPurity::MethodCount() == 0, why);
		const char* bad[] = {NULL, "", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015a",
		                     "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015adX",
		                     "za7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"};
		for (int i = 0; i < 5; ++i)
		{
			ProxyPurity::Load(p.c_str(), kSha, NULL);
			r = ProxyPurity::Load(p.c_str(), bad[i], &why);
			char name[96];
			_snprintf_s(name, sizeof(name), _TRUNCATE, "refused: the loaded DLL's hash malformed (case %d)", i);
			Check(name, !r && ProxyPurity::MethodCount() == 0 && why.find("SHA-256 is not known") != std::string::npos, why);
		}
		ProxyPurity::Load(p.c_str(), kSha, NULL);
		r = ProxyPurity::Load((g_dir + "\\no-such-file.tsv").c_str(), kSha, &why);
		Check("refused: missing file", !r && ProxyPurity::MethodCount() == 0 && why.find("no such file") != std::string::npos,
		      why);
		ProxyPurity::Load(p.c_str(), kSha, NULL);
		r = ProxyPurity::Load(g_dir.c_str(), kSha, &why);
		Check("refused: a folder, not a file", !r && ProxyPurity::MethodCount() == 0, why);
		ProxyPurity::Load(p.c_str(), kSha, &why);
		Check("Load with why = NULL is allowed", ProxyPurity::Load(p.c_str(), kSha, NULL) && ProxyPurity::MethodCount() == 2);
	}

	// ---- refusals of the file's content
	Refused("refused: empty file", "", "empty");
	Refused("refused: unknown format version", "# vp64-purity 2\ndll_sha256\t" + std::string(kSha) + "\nderived_from\ta\tb\n",
	        "unknown format");
	Refused("refused: no format line", "dll_sha256\t" + std::string(kSha) + "\nderived_from\ta\tb\n", "unknown format");
	Refused("refused: format line with trailing space", "# vp64-purity 1 \ndll_sha256\t" + std::string(kSha) + "\n",
	        "unknown format");
	Refused("refused: no dll_sha256", "# vp64-purity 1\nderived_from\ta\tb\n" + std::string(kBody), "no dll_sha256");
	Refused("refused: bound to another DLL", Head(kOther) + kBody, "another x64 DLL");
	Refused("refused: dll_sha256 not hex", "# vp64-purity 1\ndll_sha256\tzz7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n",
	        "not 64 hex");
	Refused("refused: dll_sha256 short", "# vp64-purity 1\ndll_sha256\tba78\n", "not 64 hex");
	Refused("refused: dll_sha256 repeated", Head() + "dll_sha256\t" + kSha + "\n", "repeated");
	Refused("refused: dll_sha256 with two fields", "# vp64-purity 1\ndll_sha256\t" + std::string(kSha) + "\tx\n",
	        "takes 1 field");
	Refused("refused: no derived_from", "# vp64-purity 1\ndll_sha256\t" + std::string(kSha) + "\n" + kBody, "no derived_from");
	Refused("refused: derived_from with one field", "# vp64-purity 1\ndll_sha256\t" + std::string(kSha) + "\nderived_from\ta\n",
	        "takes 2 fields");
	Refused("refused: derived_from repeated", Head() + "derived_from\ta\tb\n", "repeated");
	Refused("refused: unknown record", Head() + "pure\tICvCity1::GetOwner() const\n", "unknown record");
	Refused("refused: a record with an extra field", Head() + "method\tICvCity1::GetOwner() const\tpure\n", "takes 1 field");
	Refused("refused: an empty field", Head() + "lua\t\n", "empty field");
	Refused("refused: a method key without ::", Head() + "method\tGetOwner() const\n", "not a method key");
	Refused("refused: a method key without (", Head() + "method\tICvCity1::GetOwner\n", "not a method key");
	Refused("refused: a method key with junk after )", Head() + "method\tICvCity1::GetOwner() volatile\n", "not a method key");
	Refused("refused: a Lua name with two dots", Head() + "lua\tPlot.GetX.Y\n", "not a Lua name");
	Refused("refused: a Lua name without a dot", Head() + "lua\tPlot\n", "not a Lua name");
	Refused("refused: a Lua name starting with a digit", Head() + "lua\t1Plot.GetX\n", "not a Lua name");
	Refused("refused: a method listed twice", Head() + "method\tICvCity1::GetOwner() const\nmethod\tICvCity1::GetOwner() const\n",
	        "listed twice");
	Refused("refused: a Lua name listed twice", Head() + "lua\tPlot.GetX\nlua\tPlot.GetX\n", "listed twice");
	Refused("refused: a control character", Head() + "lua\tPlot.Get\x01X\n", "control character");
	Refused("refused: a lone CR inside a line", Head() + "lua\tPlot.GetX\rlua\tPlot.GetY\n", "control character");
	Refused("refused: a byte >= 0x80", Head() + "lua\tPlot.Get\xC3\xA9\n", "byte 0xC3");
	{
		std::string nul = Head() + "lua\tPlot.GetX";
		nul.push_back('\0');
		nul += "\n";
		Refused("refused: a NUL byte", nul, "NUL byte");
	}
	Refused("refused: a line over 1024 bytes", Head() + "lua\tPlot." + std::string(1100, 'X') + "\n", "longer than 1024");
	{
		std::string big = Head();
		while (big.size() <= (1u << 20)) big += "# padding padding padding padding padding padding padding padding\n";
		Refused("refused: a file over 1 MiB", big, "larger than");
	}
	{
		std::string many = Head();
		for (int i = 0; i < 65537; ++i)
		{
			char b[48];
			_snprintf_s(b, sizeof(b), _TRUNCATE, "lua\tT.F%d\n", i);
			many += b;
		}
		Refused("refused: more than 65536 entries", many, "more than 65536");
	}

	// ---- the interim test load (VP_PROXY_PURITY), until the boot calls Load (issue 3504)
	{
		const std::string dll = Write("fake.dll", "abc");
		p = Write("good.tsv", Head() + kBody);
		SetEnvironmentVariableA("VP_PROXY_PURITY", NULL);
		ProxyPurity::Load(p.c_str(), kSha, NULL);
		r = ProxyPurity::LoadFromTestEnvironment(&why);
		Check("interim: not set: false, lists empty", !r && ProxyPurity::MethodCount() == 0 &&
		                                                 ProxyPurity::LoadAttempted(), why);
		Check("interim: not set: why says so", why.find("VP_PROXY_PURITY not set") != std::string::npos, why);
		SetEnvironmentVariableA("VP_PROXY_PURITY", p.c_str());
		r = ProxyPurity::LoadFromTestEnvironment(&why);
		Check("interim: no '|': refused, empty", !r && ProxyPurity::MethodCount() == 0, why);
		SetEnvironmentVariableA("VP_PROXY_PURITY", (p + "|" + g_dir + "\\missing.dll").c_str());
		ProxyPurity::Load(p.c_str(), kSha, NULL);
		r = ProxyPurity::LoadFromTestEnvironment(&why);
		Check("interim: DLL missing: refused, empty", !r && ProxyPurity::MethodCount() == 0 &&
		                                                 why.find("cannot open") != std::string::npos, why);
		SetEnvironmentVariableA("VP_PROXY_PURITY", (p + "|" + dll).c_str());
		r = ProxyPurity::LoadFromTestEnvironment(&why);
		Check("interim: good: the DLL's SHA-256 computed, loaded", r && ProxyPurity::MethodCount() == 2 &&
		                                                               why.find("interim") != std::string::npos, why);
		const std::string other = Write("other.dll", "abd");
		SetEnvironmentVariableA("VP_PROXY_PURITY", (p + "|" + other).c_str());
		r = ProxyPurity::LoadFromTestEnvironment(&why);
		Check("interim: another DLL: refused, empty", !r && ProxyPurity::MethodCount() == 0 &&
		                                                 why.find("another x64 DLL") != std::string::npos, why);
		SetEnvironmentVariableA("VP_PROXY_PURITY", NULL);
	}

	// ---- the interim load beside the x64 DLL (a live instance's D28 layout)
	{
		const std::string x64 = g_dir + "\\x64";
		CreateDirectoryA(x64.c_str(), NULL);
		DeleteFileA((x64 + "\\purity.tsv").c_str());
		DeleteFileA((x64 + "\\CvGameCore_Expansion2.dll").c_str());
		p = Write("good.tsv", Head() + kBody);
		ProxyPurity::Load(p.c_str(), kSha, NULL);
		r = ProxyPurity::LoadBesideX64(x64.c_str(), &why);
		Check("beside: no purity.tsv: false, empty, says so", !r && ProxyPurity::MethodCount() == 0 &&
		                                                          why.find("no ") != std::string::npos, why);
		r = ProxyPurity::LoadBesideX64(NULL, &why);
		Check("beside: no x64_dir: false, empty", !r && ProxyPurity::MethodCount() == 0, why);
		Write("x64\\purity.tsv", Head() + kBody);
		r = ProxyPurity::LoadBesideX64(x64.c_str(), &why);
		Check("beside: purity.tsv but no DLL: refused, empty", !r && ProxyPurity::MethodCount() == 0 &&
		                                                          why.find("cannot open") != std::string::npos, why);
		Write("x64\\CvGameCore_Expansion2.dll", "abc");
		r = ProxyPurity::LoadBesideX64(x64.c_str(), &why);
		Check("beside: good: loaded", r && ProxyPurity::MethodCount() == 2 && ProxyPurity::LuaCount() == 2 &&
		                                  why.find("beside the x64 DLL") != std::string::npos, why);
		Write("x64\\CvGameCore_Expansion2.dll", "abcd");
		r = ProxyPurity::LoadBesideX64(x64.c_str(), &why);
		Check("beside: another DLL: refused, empty", !r && ProxyPurity::MethodCount() == 0 &&
		                                                why.find("another x64 DLL") != std::string::npos, why);
		// issue 2606: CvGameCore_Expansion2_x64.dll, when present, is the DLL the purity file is bound to
		Write("x64\\CvGameCore_Expansion2_x64.dll", "abc");
		r = ProxyPurity::LoadBesideX64(x64.c_str(), &why);
		Check("beside (2606): the _x64 file wins over the old name (good _x64, other old): loaded",
		      r && ProxyPurity::MethodCount() == 2 && why.find("beside the x64 DLL") != std::string::npos, why);
		Write("x64\\CvGameCore_Expansion2.dll", "abc");
		Write("x64\\CvGameCore_Expansion2_x64.dll", "abcd");
		r = ProxyPurity::LoadBesideX64(x64.c_str(), &why);
		Check("beside (2606, negative): another _x64 file with a good old one: refused, empty (no fallback to the old)",
		      !r && ProxyPurity::MethodCount() == 0 && why.find("another x64 DLL") != std::string::npos, why);
		DeleteFileA((x64 + "\\CvGameCore_Expansion2.dll").c_str());
		Write("x64\\CvGameCore_Expansion2_x64.dll", "abc");
		r = ProxyPurity::LoadBesideX64(x64.c_str(), &why);
		Check("beside (2606): only the _x64 file: loaded", r && ProxyPurity::MethodCount() == 2, why);
		DeleteFileA((x64 + "\\CvGameCore_Expansion2_x64.dll").c_str());
	}

	printf("purity_unit: %d/%d checks passed\n", g_ok, g_n);
	return g_ok == g_n ? 0 : 1;
}
