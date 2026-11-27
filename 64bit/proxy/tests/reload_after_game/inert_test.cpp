// inert_test.cpp - ticket 0805: offline test of ProxyLuaExec::InertImageValues (proxy/src/proxy_luaexec.cpp, compiled in
// here with VP_LUAEXEC_UNIT_TEST) against stock Lua 5.1.4, as proxy_lua.cpp's ProxyLua_InertImage uses it at the orderly
// shutdown: the server-value metatables lose their metamethods, the master-form function tables are emptied, the image's
// registry entries go. What must hold afterwards: a collection finalizes nothing through the old __gc, indexing or calling
// an old stub or instance raises a Lua error (no C function of the "unloaded image" runs), the registry no longer has the
// image's keys, and the counts are exact. The negative control: the same set-up WITHOUT the call finalizes every stub
// through __gc (so the test sees finalizers at all). Exit 0 when every check passes; the last line
// "inert_test: N/M checks passed". -v prints every check. GPL-3.0-or-later.
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>

// The file under test logs through ProxyRtInt::Log in the proxy; here through this (as proxy/tests/luaexec does).
static bool g_verbose = false;
static void Log(const char* szFormat, ...)
{
	if (!g_verbose) return;
	va_list args;
	va_start(args, szFormat);
	printf("    log: ");
	vprintf(szFormat, args);
	printf("\n");
	va_end(args);
}

#define VP_LUAEXEC_UNIT_TEST 1
#include "proxy_luaexec.cpp"

extern "C" {
#include <lualib.h>
}

namespace
{
int g_checks = 0, g_passed = 0;

void Check(const char* name, bool ok)
{
	++g_checks;
	if (ok) ++g_passed;
	if (!ok || g_verbose) printf("%s %s\n", ok ? "ok  " : "FAIL", name);
}

// The "image's" C functions: each counts its calls (none may run after the inert step).
int g_gcCalls = 0, g_indexCalls = 0, g_callCalls = 0, g_funcCalls = 0;
int FakeGc(lua_State*) { ++g_gcCalls; return 0; }
int FakeIndex(lua_State* L) { ++g_indexCalls; lua_pushinteger(L, 7); return 1; }
int FakeCall(lua_State* L) { ++g_callCalls; lua_pushinteger(L, 8); return 1; }
int FakeFunc(lua_State* L) { ++g_funcCalls; lua_pushinteger(L, 9); return 1; }

// The image's registry keys (their addresses), as proxy_lua.cpp's kKey* bytes.
char kSRefMt, kSRefGcMt, kSRefCache, kMethodCache, kMasterTypes, kMasterInst, kAnchor, kOther;

void SetMeta(lua_State* L, void* key, bool full)
{
	lua_pushlightuserdata(L, key);
	lua_createtable(L, 0, 4);
	lua_pushcfunction(L, FakeGc);
	lua_setfield(L, -2, "__gc");
	if (full)
	{
		lua_pushcfunction(L, FakeIndex);
		lua_setfield(L, -2, "__index");
		lua_pushcfunction(L, FakeCall);
		lua_setfield(L, -2, "__call");
	}
	lua_rawset(L, LUA_REGISTRYINDEX);
}

// n stubs with the metatable under key: `keep` of them in the global table name, the rest garbage at once.
void MakeStubs(lua_State* L, void* key, int n, int keep, const char* name)
{
	lua_newtable(L);
	for (int i = 0; i < n; ++i)
	{
		lua_newuserdata(L, 12);
		lua_pushlightuserdata(L, key);
		lua_rawget(L, LUA_REGISTRYINDEX);
		lua_setmetatable(L, -2);
		if (i < keep)
			lua_rawseti(L, -2, i + 1);
		else
			lua_pop(L, 1);
	}
	lua_setglobal(L, name);
}

// One master-form type "Game" (metatable with __index = {GetGameTurn = FakeFunc, IsPaused = FakeFunc}) and one instance
// of it in the global Game; a second type "Map" with three functions and no instance.
void MakeMasterTypes(lua_State* L)
{
	lua_pushlightuserdata(L, &kMasterTypes);
	lua_newtable(L);
	const char* const names[] = {"Game", "Map"};
	const int nf[] = {2, 3};
	for (int t = 0; t < 2; ++t)
	{
		lua_createtable(L, 0, 2);  // the metatable
		lua_newtable(L);           // the function table
		for (int f = 0; f < nf[t]; ++f)
		{
			lua_pushcfunction(L, FakeFunc);
			char k[16];
			sprintf_s(k, sizeof(k), "F%d", f);
			lua_setfield(L, -2, k);
		}
		if (t == 0)
		{
			lua_pushcfunction(L, FakeFunc);
			lua_setfield(L, -2, "GetGameTurn");
		}
		lua_setfield(L, -2, "__index");
		lua_setfield(L, -2, names[t]);
	}
	lua_rawset(L, LUA_REGISTRYINDEX);
	// the instance
	lua_newuserdata(L, 8);
	lua_pushlightuserdata(L, &kMasterTypes);
	lua_rawget(L, LUA_REGISTRYINDEX);
	lua_getfield(L, -1, "Game");
	lua_setmetatable(L, -3);
	lua_pop(L, 1);
	lua_setglobal(L, "Game");
}

void SetRegistry(lua_State* L, void* key)
{
	lua_pushlightuserdata(L, key);
	lua_newtable(L);
	lua_rawset(L, LUA_REGISTRYINDEX);
}

bool InRegistry(lua_State* L, void* key)
{
	lua_pushlightuserdata(L, key);
	lua_rawget(L, LUA_REGISTRYINDEX);
	const bool has = !lua_isnil(L, -1);
	lua_pop(L, 1);
	return has;
}

// Runs code; true when it raised an error (pcall false).
bool Raises(lua_State* L, const char* code)
{
	if (luaL_loadstring(L, code) != 0)
	{
		lua_pop(L, 1);
		return false;
	}
	const bool err = lua_pcall(L, 0, 0, 0) != 0;
	if (err) lua_pop(L, 1);
	return err;
}

struct InertArgs
{
	ProxyLuaExec::InertCounts counts;
};

// As proxy_lua.cpp's ProxyLua_InertImage, run through lua_cpcall as the engine's CallCFunction does.
int InertThunk(lua_State* L)
{
	InertArgs* a = static_cast<InertArgs*>(lua_touserdata(L, 1));
	void* const metaKeys[] = {&kSRefMt, &kSRefGcMt};
	void* const dropKeys[] = {&kSRefCache, &kMethodCache, &kMasterInst, &kAnchor};
	ProxyLuaExec::InertImageValues(L, metaKeys, 2, &kMasterTypes, dropKeys, 4, &a->counts);
	return 0;
}

void SetUp(lua_State* L)
{
	g_gcCalls = g_indexCalls = g_callCalls = g_funcCalls = 0;
	SetMeta(L, &kSRefMt, true);
	SetMeta(L, &kSRefGcMt, false);
	MakeStubs(L, &kSRefMt, 10, 4, "keepFull");
	MakeStubs(L, &kSRefGcMt, 6, 2, "keepGc");
	MakeMasterTypes(L);
	SetRegistry(L, &kSRefCache);
	SetRegistry(L, &kMethodCache);
	SetRegistry(L, &kMasterInst);
	SetRegistry(L, &kOther);  // not the image's: must stay
	lua_pushlightuserdata(L, &kAnchor);
	lua_newthread(L);
	lua_rawset(L, LUA_REGISTRYINDEX);
}

void Control()
{
	// Without the inert step: every stub is finalized through __gc (10 + 6), the old functions answer.
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);
	SetUp(L);
	const bool indexed = luaL_dostring(L, "return keepFull[1].x") == 0 && lua_tointeger(L, -1) == 7;
	lua_settop(L, 0);
	const bool called = luaL_dostring(L, "return Game.GetGameTurn()") == 0 && lua_tointeger(L, -1) == 9;
	lua_settop(L, 0);
	Check("control: an old stub's __index runs before the inert step (the test reaches the image's functions)",
	      indexed && g_indexCalls == 1);
	Check("control: an old master instance's function runs before the inert step", called && g_funcCalls == 1);
	luaL_dostring(L, "keepFull = nil keepGc = nil collectgarbage('collect') collectgarbage('collect')");
	Check("control: without the inert step the collection finalizes every stub through the old __gc (16)", g_gcCalls == 16);
	lua_close(L);
}

void Inert()
{
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);
	SetUp(L);
	// the 10 + 6 - 4 - 2 = 10 unreferenced stubs are already garbage; the inert step comes before any collection
	InertArgs a;
	a.counts.metaFields = a.counts.typeTables = a.counts.typeFunctions = a.counts.registryKeys = -1;
	const int top = lua_gettop(L);
	const int rc = lua_cpcall(L, InertThunk, &a);
	Check("inert: InertImageValues ran under lua_cpcall without a Lua error", rc == 0);
	Check("inert: the stack is as before", lua_gettop(L) == top);
	Check("inert: 4 metamethods removed (__gc, __index, __call of the full stub metatable; __gc of the gc one)",
	      a.counts.metaFields == 4);
	Check("inert: 2 master-form function tables emptied", a.counts.typeTables == 2);
	Check("inert: 6 functions removed from them (Game 2 + GetGameTurn, Map 3)", a.counts.typeFunctions == 6);
	Check("inert: 7 registry entries removed (4 drop keys, 2 metatables, the types table)", a.counts.registryKeys == 7);
	Check("inert: the image's keys are gone from the registry",
	      !InRegistry(L, &kSRefMt) && !InRegistry(L, &kSRefGcMt) && !InRegistry(L, &kSRefCache) &&
	          !InRegistry(L, &kMethodCache) && !InRegistry(L, &kMasterTypes) && !InRegistry(L, &kMasterInst) &&
	          !InRegistry(L, &kAnchor));
	Check("inert: a key that is not the image's stays in the registry", InRegistry(L, &kOther));
	// after the inert step nothing of the "image" may run
	Check("inert: indexing an old stub raises a Lua error", Raises(L, "return keepFull[1].x"));
	Check("inert: calling an old stub raises a Lua error", Raises(L, "return keepFull[2]()"));
	Check("inert: an old master instance's function is gone (calling it raises a Lua error)",
	      Raises(L, "return Game.GetGameTurn()"));
	luaL_dostring(L, "collectgarbage('collect') collectgarbage('collect')");
	Check("inert: a collection of the unreferenced stubs finalizes nothing through the old __gc", g_gcCalls == 0);
	luaL_dostring(L, "keepFull = nil keepGc = nil Game = nil collectgarbage('collect') collectgarbage('collect')");
	Check("inert: a collection of the rest finalizes nothing either", g_gcCalls == 0);
	Check("inert: no old __index, __call or function ran", g_indexCalls == 0 && g_callCalls == 0 && g_funcCalls == 0);
	lua_close(L);
	Check("inert: closing the state finalizes nothing through the old __gc", g_gcCalls == 0);

	// a second run on a state where the image left nothing (an image that never made a stub) changes nothing
	lua_State* E = luaL_newstate();
	InertArgs b;
	const int rc2 = lua_cpcall(E, InertThunk, &b);
	Check("inert: on a state without the image's entries it runs and counts 0 everywhere",
	      rc2 == 0 && b.counts.metaFields == 0 && b.counts.typeTables == 0 && b.counts.typeFunctions == 0 &&
	          b.counts.registryKeys == 0);
	lua_close(E);
}
}  // namespace

int main(int argc, char** argv)
{
	g_verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
	Control();
	Inert();
	printf("inert_test: %d/%d checks passed\n", g_passed, g_checks);
	return g_passed == g_checks ? 0 : 1;
}
