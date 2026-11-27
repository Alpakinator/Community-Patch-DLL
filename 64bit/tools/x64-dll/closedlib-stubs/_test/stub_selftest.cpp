// Self-test for the issue 1005 libraries: lua51.lib and the stub log (CvGameDatabase.lib until issue 1402,
// CvLocalization.lib until issue 1703: both are real libraries now, tools/x64-dll/cvgamedatabase/ and cvlocalization/).
//
// Compiled against the fork's headers exactly as the DLL sees them (CvGameDatabaseAPI / CvLocalizationAPI
// left as __declspec(dllimport)), so every call goes through an __imp_ pointer as in the DLL. Checks results,
// then flushes the log and checks its content. Exit code 0 = pass. Build and run: see run_selftest.py.
//
// FLua::Details::Error belongs to issue 1006's FLua group; a test-only definition is given here.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdio.h>
#include <share.h>
#include <string.h>
#include <string>
#include <new>
#include <tchar.h>
#include <assert.h>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include <FireWorks/FFireTypes.h>
#include <FireWorks/FAssert.h>
#undef FAssert
#define FAssert(expr) ((void)0)
// FLua.h itself is `#error` on upstream master (#13489, dae4c612e; issue 3511); its body is unchanged since
// Release-5.4.8, so include the headers it includes (the same declarations on both).
#include <FireWorks/FLua/include/FLuaTypes.h>
#include <FireWorks/FLua/include/FLuaGlobalData.h>
#include <FireWorks/FLua/include/FLuaStaticFunctions.h>
#include <FireWorks/FLua/include/FLuaTypeExposure.h>
#include <FireWorks/FLua/include/FLuaPointerProtection.h>
#include "stublog.h"

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

static char g_lastError[512];
namespace FLua { namespace Details {
	void Error(const char* szFormat, ...)
	{
		va_list ap;
		va_start(ap, szFormat);
		_vsnprintf_s(g_lastError, sizeof(g_lastError), _TRUNCATE, szFormat, ap);
		va_end(ap);
	}
} }

static int RaiseError(lua_State* L)
{
	return luaL_error(L, "boom %d", 7);
}

static int SetGlobalFromUserdata(lua_State* L)
{
	int* p = (int*)lua_touserdata(L, 1);
	lua_pushinteger(L, *p);
	lua_setfield(L, LUA_GLOBALSINDEX, "fromC");
	return 0;
}

int main()
{
	// CvGameDatabase (issue 1402) and CvLocalization (issues 1701/1703) are real libraries with their own A/B
	// (tools/x64-dll/cvgamedatabase/, tools/x64-dll/cvlocalization/); their stub groups are deleted.

	// --- Lua 5.1.4: real
	{
		lua_State* L = luaL_newstate();
		luaL_openlibs(L);
		CHECK(luaL_loadstring(L, "return _VERSION, 7 * 6") == 0);
		CHECK(lua_pcall(L, 0, 2, 0) == 0);
		CHECK(strcmp(lua_tostring(L, -2), "Lua 5.1") == 0);
		CHECK(lua_tointeger(L, -1) == 42);
		lua_settop(L, 0);

		// number -> integer like the game's x87 fistp: round half to even; out of range -> INT_MIN
		const double in[] = { 1.5, 2.5, -1.5, 1.7, -1.7, 0.49999999999999994, 3e9, -3e9 };
		const lua_Integer want[] = { 2, 2, -2, 2, -2, 0, (lua_Integer)(-2147483647 - 1), (lua_Integer)(-2147483647 - 1) };
		for (int i = 0; i < 8; ++i)
		{
			lua_pushnumber(L, in[i]);
			lua_Integer got = lua_tointeger(L, -1);
			if (got != want[i])
				printf("FAIL lua_tointeger(%.17g) = %lld, want %lld\n", in[i], (long long)got, (long long)want[i]);
			g_fail += (got != want[i]);
			lua_pop(L, 1);
		}
		CHECK(luaL_loadstring(L, "return string.sub('abcdef', 2.5, 4.5)") == 0 && lua_pcall(L, 0, 1, 0) == 0);
		CHECK(strcmp(lua_tostring(L, -1), "bcd") == 0);	// 2.5 -> 2, 4.5 -> 4 (truncation would give the same; see next)
		lua_settop(L, 0);
		CHECK(luaL_loadstring(L, "return string.sub('abcdef', 1.7, 3.7)") == 0 && lua_pcall(L, 0, 1, 0) == 0);
		CHECK(strcmp(lua_tostring(L, -1), "bcd") == 0);	// 1.7 -> 2, 3.7 -> 4 (truncation would give "abc")
		lua_settop(L, 0);

		// FLua members in lua51.lib (no destructor call: ~Value is issue 1006's)
		lua_pushstring(L, "held");
		alignas(FLua::Value) unsigned char buf[sizeof(FLua::Value)];
		FLua::Value* v = new (buf) FLua::Value(L, -1);
		lua_pop(L, 1);
		CHECK(v->Valid() && v->GetLuaState() == L);
		lua_rawgeti(L, LUA_REGISTRYINDEX, v->GetRef());
		CHECK(lua_isstring(L, -1) && strcmp(lua_tostring(L, -1), "held") == 0);
		lua_pop(L, 1);
		lua_pushnil(L);
		v->Set(L, -1);
		lua_pop(L, 1);
		CHECK(!v->Valid());

		int payload = 99;
		CHECK(FLua::Details::CCallWithErrorHandling(L, &SetGlobalFromUserdata, &payload));
		lua_getfield(L, LUA_GLOBALSINDEX, "fromC");
		CHECK(lua_tointeger(L, -1) == 99);
		lua_pop(L, 1);
		int top = lua_gettop(L);
		CHECK(!FLua::Details::CCallWithErrorHandling(L, &RaiseError, NULL));
		CHECK(lua_gettop(L) == top);
		CHECK(strstr(g_lastError, "boom 7") != NULL);
		lua_close(L);
	}

	// --- stub log
	StubLog_Flush();
	char log[1 << 16] = { 0 };
	FILE* f = _fsopen(StubLog_Path(), "rb", _SH_DENYNO);	// the log stays open for writing
	CHECK(f != NULL);
	if (f)
	{
		fread(log, 1, sizeof(log) - 1, f);
		fclose(f);
	}
	CHECK(strstr(log, "#\tcv64-stublog 1") != NULL);
	CHECK(strstr(log, "FIRST\tseq=1\t") != NULL);
	CHECK(strstr(log, "lua51\tbool __cdecl FLua::Details::CCallWithErrorHandling") != NULL);
	CHECK(strstr(log, "#\tflush 1\t") != NULL);
	printf("log: %s\n%s", StubLog_Path(), g_fail ? "SELFTEST FAILED\n" : "SELFTEST PASSED\n");
	return g_fail ? 1 : 0;
}
