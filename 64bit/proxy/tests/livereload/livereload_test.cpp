// livereload_test.cpp - offline test of proxy/src/proxy_livereload.cpp (ticket 0586) against stock Lua 5.1.4, with a
// stand-in EXE image (PE header with the time stamp, Ice_Reload's code site, the ice object and its grid) in this
// process's memory and the real named section of this process's id (the server's role is played here).
// Written 2026-11-19 for this repository (GPL-3.0-or-later). Run by test_livereload.py.
#include "proxy_livereload.h"
#include "vp_livereload.h"
#include <windows.h>
#include "proxy_exe.h"   // ticket 0619: the build registry and the build table
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

namespace
{
int g_checks = 0, g_passed = 0;
bool g_verbose = false;
std::vector<std::string> g_log;

void Check(const char* name, bool ok, const std::string& detail = std::string())
{
	++g_checks;
	if (ok) ++g_passed;
	if (!ok || g_verbose)
		printf("%s %s%s%s\n", ok ? "ok  " : "FAIL", name, detail.empty() ? "" : "  [", detail.empty() ? "" : (detail + "]").c_str());
}

void LogHook(const char* line)
{
	g_log.push_back(line);
	if (g_verbose) printf("    log: %s\n", line);
}

bool LogHas(const char* s)
{
	for (size_t i = 0; i < g_log.size(); ++i)
		if (g_log[i].find(s) != std::string::npos) return true;
	return false;
}

// ---- the stand-in image ----
// Ticket 0619 (epic 0590, D202): the stand-in image is laid out for a build of the registry, with the addresses of its
// table (ice_reload, ice_ice, cam_grid); the size covers both builds' Ice objects.
size_t kImageBytes = 0;
uint8_t* g_img = NULL;
int g_build = kVpExeBuild4390913;

size_t ImageBytesForAllBuilds()
{
	size_t need = 0x1000;
	for (int b = 0; b < kVpExeBuildCount; ++b)
	{
		const size_t ice = VpExeRva(b, VPX_ice_ice) + 0x10000, site = VpExeRva(b, VPX_ice_reload) + 16;
		if (ice > need) need = ice;
		if (site > need) need = site;
	}
	return (need + 0xFFFF) & ~(size_t)0xFFFF;
}

uint8_t* Ice() { return g_img + VpExeRva(g_build, VPX_ice_ice); }
uint8_t* Grid() { return Ice() + vplr::kIceOffGrid; }

void Put32(uint8_t* p, uint32_t v) { memcpy(p, &v, 4); }

// build: a build of the registry (its stamp and SizeOfImage in the header), or kVpExeBuildUnknown with `stamp` (the Ice
// object and the site then at build 4390913's places, so a wrongly accepted image would be noticed).
void MakeImage(int build, bool pattern, uint32_t w, uint32_t h, uint32_t stamp = 0)
{
	const VpExeBuildInfo* info = VpExeBuildInfoOf(build);
	g_build = info ? build : kVpExeBuild4390913;
	memset(g_img, 0, kImageBytes);
	IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)g_img;
	dos->e_magic = IMAGE_DOS_SIGNATURE;
	dos->e_lfanew = 0x100;
	Put32(g_img + 0x100, IMAGE_NT_SIGNATURE);
	Put32(g_img + 0x108, info ? info->timeDateStamp : stamp);
	g_img[0x100 + 24] = 0x0B;   // OptionalHeader.Magic 0x10B (PE32)
	g_img[0x100 + 25] = 0x01;
	Put32(g_img + 0x100 + 24 + 56, info ? info->sizeOfImage : 0x06519000u);   // OptionalHeader.SizeOfImage
	uint8_t* site = g_img + VpExeRva(g_build, VPX_ice_reload);
	const uint32_t base = (uint32_t)(uintptr_t)g_img;
	site[0] = 0x68;
	Put32(site + 1, base + VpExeRva(g_build, VPX_cam_grid));
	site[5] = 0xB9;
	Put32(site + 6, base + VpExeRva(g_build, VPX_ice_ice) + (pattern ? 0u : 4u));
	site[10] = 0xE8;
	Put32(Ice() + vplr::kIceOffW, w);
	Put32(Ice() + vplr::kIceOffH, h);
	ProxyLiveReload::SetImageForTest(g_img, kImageBytes);
}

// ---- the section (the server's side) ----
HANDLE g_map = NULL;
vplr::Section* g_sec = NULL;

void MakeSection()
{
	wchar_t name[64];
	_snwprintf_s(name, 64, _TRUNCATE, VPLR_SECTION_FMT, (unsigned)GetCurrentProcessId());
	g_map = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, vplr::kSectionBytes, name);
	g_sec = (vplr::Section*)MapViewOfFile(g_map, FILE_MAP_ALL_ACCESS, 0, 0, vplr::kSectionBytes);
	memset(g_sec, 0, sizeof(*g_sec));
	g_sec->version = vplr::kVersion;
	g_sec->bytes = sizeof(vplr::Section);
	g_sec->serverPid = 4242;
	g_sec->magic = vplr::kMagic;
}

void DropSection()
{
	if (g_sec) UnmapViewOfFile(g_sec);
	if (g_map) CloseHandle(g_map);
	g_sec = NULL;
	g_map = NULL;
}

void Request(uint32_t flags, uint32_t w, uint32_t h, const std::vector<int>& icePlots, uint32_t expectFlips)
{
	g_sec->reqFlags = flags;
	g_sec->mapW = w;
	g_sec->mapH = h;
	g_sec->loadSeq = 3;
	memset(g_sec->ice, 0, sizeof(g_sec->ice));
	for (size_t i = 0; i < icePlots.size(); ++i) vplr::BitSet(g_sec->ice, (uint32_t)icePlots[i], true);
	g_sec->iceCount = (uint32_t)icePlots.size();
	g_sec->expectFlips = expectFlips;
	MemoryBarrier();
	g_sec->reqSeq = g_sec->reqSeq + 1;
}

// ---- Lua: the two globals count their calls; Ice_Reload records the grid's ice bits it saw ----
lua_State* L = NULL;

int CountedIce(lua_State* s)
{
	lua_getglobal(s, "ice_calls");
	const int n = (int)lua_tointeger(s, -1);
	lua_pop(s, 1);
	lua_pushinteger(s, n + 1);
	lua_setglobal(s, "ice_calls");
	return 0;
}

int CountedWaves(lua_State* s)
{
	lua_getglobal(s, "waves_calls");
	const int n = (int)lua_tointeger(s, -1);
	lua_pop(s, 1);
	lua_pushinteger(s, n + 1);
	lua_setglobal(s, "waves_calls");
	return 0;
}

int GlobalInt(const char* name)
{
	lua_getglobal(L, name);
	const int n = (int)lua_tointeger(L, -1);
	lua_pop(L, 1);
	return n;
}

void SetGlobals(bool ice, bool waves)
{
	lua_pushinteger(L, 0);
	lua_setglobal(L, "ice_calls");
	lua_pushinteger(L, 0);
	lua_setglobal(L, "waves_calls");
	if (ice) lua_pushcfunction(L, CountedIce); else lua_pushnil(L);
	lua_setglobal(L, "Ice_Reload");
	if (waves) lua_pushcfunction(L, CountedWaves); else lua_pushnil(L);
	lua_setglobal(L, "Waves_Reload");
}

// The drain's stand-in: a C function whose environment is `env` (the InGame context's), calling OnFrame as the drain does.
int Drain(lua_State* s)
{
	const int top = lua_gettop(s);
	ProxyLiveReload::OnFrame(s);
	lua_settop(s, top);
	return 0;
}

void Frame()
{
	lua_pushcfunction(L, Drain);
	if (lua_pcall(L, 0, 0, 0) != 0)
	{
		printf("FAIL the drain raised: %s\n", lua_tostring(L, -1));
		++g_checks;
		lua_pop(L, 1);
	}
}

void Frames(int n)
{
	for (int i = 0; i < n; ++i) Frame();
}

uint8_t GridByte(uint32_t x, uint32_t y) { return Grid()[y * vplr::kIceStride + x]; }

std::string Num(long v)
{
	char b[32];
	_snprintf_s(b, sizeof(b), _TRUNCATE, "%ld", v);
	return b;
}
}  // namespace

int main(int argc, char** argv)
{
	g_verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
	kImageBytes = ImageBytesForAllBuilds();
	g_img = (uint8_t*)VirtualAlloc(NULL, kImageBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!g_img)
	{
		printf("FAIL no image memory\n");
		return 1;
	}
	ProxyLiveReload::Hooks h = { LogHook };
	ProxyLiveReload::SetHooks(h);
	L = luaL_newstate();
	luaL_openlibs(L);

	// 1. no section: nothing happens (one look, then one every kOpenEveryFrames frames)
	MakeImage(kVpExeBuild4390913, true, 4, 3);
	SetGlobals(true, true);
	Frames(5);
	Check("no section: no open, no request", ProxyLiveReload::GetStats().opens == 0 && ProxyLiveReload::GetStats().requests == 0);

	// 2. the section appears: opened within kOpenEveryFrames frames
	MakeSection();
	Frames(ProxyLiveReload::kOpenEveryFrames + 1);
	Check("section opened within the look interval", ProxyLiveReload::GetStats().opens == 1, Num(ProxyLiveReload::GetStats().opens));
	Check("no request: no answer", g_sec->ackSeq == 0 && GlobalInt("ice_calls") == 0);

	// 3. an ice request: the grid starts with ice at (0,0) and (1,0), other bits set in (2,2); the request wants (1,0)
	// and (3,2): 2 flips (clear (0,0), set (3,2))
	Grid()[0] = 0x80 | 0x05;
	Grid()[1] = 0x80;
	Grid()[2 * vplr::kIceStride + 2] = 0x11;
	{
		std::vector<int> want;
		want.push_back(1);
		want.push_back(2 * 4 + 3);
		Request(vplr::kReqIce, 4, 3, want, 2);
	}
	Frame();
	Check("ice: answered", g_sec->ackSeq == g_sec->reqSeq);
	Check("ice: result ok, waves not asked", g_sec->iceResult == vplr::kOk && g_sec->wavesResult == vplr::kNotAsked,
	      Num(g_sec->iceResult) + " " + Num(g_sec->wavesResult) + " " + g_sec->why);
	Check("ice: 1 set, 1 cleared", g_sec->bitsSet == 1 && g_sec->bitsCleared == 1);
	Check("ice: (0,0) cleared, its other bits kept", GridByte(0, 0) == 0x05);
	Check("ice: (1,0) kept", GridByte(1, 0) == 0x80);
	Check("ice: (3,2) set", GridByte(3, 2) == 0x80);
	Check("ice: (2,2)'s other bits untouched", GridByte(2, 2) == 0x11);
	Check("ice: Ice_Reload called once, Waves_Reload not", GlobalInt("ice_calls") == 1 && GlobalInt("waves_calls") == 0);
	Frames(3);
	Check("ice: an answered request is not served again", GlobalInt("ice_calls") == 1 && ProxyLiveReload::GetStats().requests == 1);

	// 4. expectFlips wrong: nothing written, nothing called
	{
		std::vector<int> want;   // all ice off: 2 flips, the server claims 1
		Request(vplr::kReqIce, 4, 3, want, 1);
	}
	Frame();
	Check("mismatch: kGridMismatch", g_sec->iceResult == vplr::kGridMismatch, Num(g_sec->iceResult));
	Check("mismatch: grid untouched, no call", GridByte(1, 0) == 0x80 && GridByte(3, 2) == 0x80 && GlobalInt("ice_calls") == 1);
	Check("mismatch: a LOUD line", LogHas("LOUD: livereload: request") && LogHas("GRID MISMATCH"));

	// 5. waves only
	{
		std::vector<int> want;
		want.push_back(1);
		want.push_back(11);
		Request(vplr::kReqWaves, 4, 3, want, 0);
	}
	Frame();
	Check("waves: ok, ice not asked", g_sec->wavesResult == vplr::kOk && g_sec->iceResult == vplr::kNotAsked);
	Check("waves: Waves_Reload called once, Ice_Reload not", GlobalInt("waves_calls") == 1 && GlobalInt("ice_calls") == 1);

	// 6. both at once
	{
		std::vector<int> want;
		want.push_back(1);
		Request(vplr::kReqIce | vplr::kReqWaves, 4, 3, want, 1);
	}
	Frame();
	Check("both: ok, ok", g_sec->iceResult == vplr::kOk && g_sec->wavesResult == vplr::kOk, g_sec->why);
	Check("both: (3,2) cleared, both called", GridByte(3, 2) == 0 && GlobalInt("ice_calls") == 2 && GlobalInt("waves_calls") == 2);

	// 7. Ice_Reload missing: kNoFunction, the grid untouched
	SetGlobals(false, true);
	{
		std::vector<int> want;
		Request(vplr::kReqIce, 4, 3, want, 1);
	}
	Frame();
	Check("no Ice_Reload: kNoFunction, grid untouched", g_sec->iceResult == vplr::kNoFunction && GridByte(1, 0) == 0x80 &&
	      g_sec->bitsCleared == 0, Num(g_sec->iceResult));

	// 8. the InGame environment is looked at first: the drain's environment holds Ice_Reload, the globals do not
	{
		lua_pushcfunction(L, Drain);
		lua_newtable(L);
		lua_pushcfunction(L, CountedIce);
		lua_setfield(L, -2, "Ice_Reload");
		lua_setfenv(L, -2);
		std::vector<int> want;
		Request(vplr::kReqIce, 4, 3, want, 1);
		const int rc = lua_pcall(L, 0, 0, 0);
		Check("environment: the drain ran", rc == 0);
		Check("environment: Ice_Reload found in the InGame environment and called",
		      g_sec->iceResult == vplr::kOk && GlobalInt("ice_calls") == 1 && GridByte(1, 0) == 0, Num(g_sec->iceResult));
	}

	// 9. Ice_Reload raises: kLuaError, the bits written (counted), the error text in why
	SetGlobals(true, true);
	luaL_dostring(L, "function Ice_Reload() error('boom') end");
	{
		std::vector<int> want;
		want.push_back(0);
		Request(vplr::kReqIce, 4, 3, want, 1);
	}
	Frame();
	Check("Lua error: kLuaError with the message", g_sec->iceResult == vplr::kLuaError && strstr(g_sec->why, "boom") != NULL,
	      g_sec->why);
	Check("Lua error: the written bits are counted", g_sec->bitsSet == 1 && GridByte(0, 0) == 0x85);
	SetGlobals(true, true);

	// 10. the grid's size is not the map's
	{
		std::vector<int> want;
		Request(vplr::kReqIce, 5, 3, want, 1);
	}
	Frame();
	Check("size: kGridMismatch", g_sec->iceResult == vplr::kGridMismatch, g_sec->why);

	// 11. a bad request
	{
		std::vector<int> want;
		Request(vplr::kReqIce, 0, 3, want, 0);
	}
	Frame();
	Check("bad request: kBadRequest", g_sec->iceResult == vplr::kBadRequest);
	{
		std::vector<int> want;
		Request(vplr::kReqIce, 255, 255, want, 0);
	}
	Frame();
	Check("bad request: 255 rows refused", g_sec->iceResult == vplr::kBadRequest);

	// 12. the grid page not writable
	{
		DWORD old = 0;
		VirtualProtect(Grid(), 1, PAGE_READONLY, &old);
		std::vector<int> want;
		Request(vplr::kReqIce, 4, 3, want, 1);
		Frame();
		Check("read-only grid: kNotWritable, no call", g_sec->iceResult == vplr::kNotWritable && GlobalInt("ice_calls") == 0,
		      Num(g_sec->iceResult));
		VirtualProtect(Grid(), 1, PAGE_READWRITE, &old);
	}

	// 13. another EXE build: everything refused, nothing read beyond the header, nothing written or called
	MakeImage(kVpExeBuildUnknown, true, 4, 3, 0x12345678);
	Grid()[1] = 0x80;
	{
		std::vector<int> want;
		Request(vplr::kReqIce | vplr::kReqWaves, 4, 3, want, 1);
	}
	Frame();
	Check("other build: kRefusedExe for both", g_sec->iceResult == vplr::kRefusedExe && g_sec->wavesResult == vplr::kRefusedExe,
	      g_sec->why);
	Check("other build: nothing written or called", GridByte(1, 0) == 0x80 && GlobalInt("ice_calls") == 0 && GlobalInt("waves_calls") == 0);

	// 14. the code site differs
	MakeImage(kVpExeBuild4390913, false, 4, 3);
	Grid()[1] = 0x80;
	Frame();   // the previous request was answered; a new one
	{
		std::vector<int> want;
		Request(vplr::kReqIce, 4, 3, want, 1);
	}
	Frame();
	Check("code pattern: kRefusedPattern, nothing written", g_sec->iceResult == vplr::kRefusedPattern && GridByte(1, 0) == 0x80,
	      g_sec->why);

	// 16. ticket 0619: build 26577625, laid out with its own table: the same ice request is served there
	MakeImage(kVpExeBuild26577625, true, 4, 3);
	Frame();   // the previous request was answered; a new one
	{
		const int callsBefore = GlobalInt("ice_calls");
		std::vector<int> want;
		want.push_back(2 * 4 + 1);   // (1,2) on: 1 flip
		Request(vplr::kReqIce, 4, 3, want, 1);
		Frame();
		Check("build 26577625: ice ok, 1 set", g_sec->iceResult == vplr::kOk && g_sec->bitsSet == 1 && g_sec->bitsCleared == 0,
		      Num(g_sec->iceResult) + " " + g_sec->why);
		Check("build 26577625: (1,2) set in that build's Ice object, Ice_Reload called",
		      GridByte(1, 2) == 0x80 && GlobalInt("ice_calls") == callsBefore + 1);
	}

	// 17. ticket 0619: a symbol the build's table does not carry: kRefusedExe naming it, nothing written or called
	ProxyExe::Test::DropSymbol(VPX_ice_ice);
	MakeImage(kVpExeBuild26577625, true, 4, 3);
	Frame();
	{
		const int callsBefore = GlobalInt("ice_calls");
		std::vector<int> want;
		want.push_back(0);
		Request(vplr::kReqIce, 4, 3, want, 1);
		Frame();
		Check("not carried: kRefusedExe naming ice_ice, nothing written or called",
		      g_sec->iceResult == vplr::kRefusedExe && strstr(g_sec->why, "ice_ice") != NULL && GridByte(0, 0) == 0 &&
		          GlobalInt("ice_calls") == callsBefore,
		      g_sec->why);
	}
	ProxyExe::Test::DropSymbol(-1);

	// 15. a section of another version is not used
	ProxyLiveReload::ResetForTest();
	ProxyLiveReload::SetHooks(h);
	MakeImage(kVpExeBuild4390913, true, 4, 3);
	g_sec->version = 99;
	Frames(2);
	Check("other section version: not opened, LOUD", ProxyLiveReload::GetStats().opens == 0 && LogHas("not used"));
	g_sec->version = vplr::kVersion;
	DropSection();

	char line[400];
	ProxyLiveReload::StatusLine(line, sizeof(line));
	Check("status line", strstr(line, "livereload:") != NULL, line);
	lua_close(L);
	VirtualFree(g_img, 0, MEM_RELEASE);
	printf("livereload_test: %d/%d checks passed\n", g_passed, g_checks);
	return g_passed == g_checks ? 0 : 1;
}
