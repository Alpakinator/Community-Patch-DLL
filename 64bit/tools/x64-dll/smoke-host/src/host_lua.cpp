// The host's Lua (issue 1009): one main state in the shared lua51_x64.dll, the same Lua copy the DLL links
// through lua51_x64.lib (one Lua copy per process). The host links the import library with /DELAYLOAD and
// loads the DLL by full path first, so both resolve to the one module.
//
// Engine behaviour reproduced, after the headless host (civ5-headless's host/lua/HHScriptSystem.cpp, read-only
// reference):
//   * main state: luaL_newstate + luaL_openlibs (stock libraries, debug included: the boot-time VP_LUAAPI
//     chunk reads debug.getinfo), print -> Logs\Lua.log as "StateName: text", globals Threads = {} and
//     IncludeFileList = {}.
//   * CreateLuaThread(name): lua_newthread, anchored in the registry; its globals are a new table
//     {StateName = name} with metatable {__index = _G}; Threads[thread] = env (rawset); then
//     ICvScriptSystemUtility1::RegisterScriptLibraries(thread) in a protected call (the engine does this nested
//     in CreateLuaThread, trace S1).
//   * FreeLuaThread: Threads[thread] = nil, unref.
// Not reproduced (the EXE's extras): the index-0-visiting ipairs, hiding debug after boot, include(), the VFS,
// UI contexts.
#include "host_common.h"
#include <stdio.h>
#include <map>
#include <string>

namespace
{
	lua_State* s_L = NULL;
	HMODULE s_hLua = NULL;
	ICvScriptSystemUtility1* s_pUtility = NULL;
	std::map<lua_State*, int> s_threads;
	FILE* s_pLuaLog = NULL;

	int LuaPrint(lua_State* L)
	{
		std::string line;
		const int n = lua_gettop(L);
		lua_getglobal(L, "tostring");
		for(int i = 1; i <= n; ++i)
		{
			lua_pushvalue(L, -1);
			lua_pushvalue(L, i);
			lua_call(L, 1, 1);
			const char* s = lua_tostring(L, -1);
			if(i > 1)
				line += '\t';
			line += s ? s : "?";
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
		lua_getglobal(L, "StateName");
		const char* szState = lua_tostring(L, -1);
		std::string state = szState ? szState : "Main";
		lua_pop(L, 1);
		if(s_pLuaLog)
		{
			fprintf(s_pLuaLog, "%s: %s\n", state.c_str(), line.c_str());
			fflush(s_pLuaLog);
		}
		return 0;
	}

	struct ThreadEntryArgs
	{
		int iRef;
		bool bClear;
	};

	// Threads[thread] = env (stack [2]) or nil; rawset, as the headless host: the VP_LUAAPI chunk later puts a
	// __newindex listener on Threads entries meant for the engine's UI contexts.
	int LuaSetThreadEntry(lua_State* L)
	{
		ThreadEntryArgs* p = static_cast<ThreadEntryArgs*>(lua_touserdata(L, 1));
		lua_getglobal(L, "Threads");
		if(!lua_istable(L, -1))
			return 0;
		lua_rawgeti(L, LUA_REGISTRYINDEX, p->iRef);
		if(p->bClear)
			lua_pushnil(L);
		else
			lua_pushvalue(L, 2);
		lua_rawset(L, -3);
		return 0;
	}

	int LuaRegisterLibraries(lua_State* L)
	{
		ICvScriptSystemUtility1* pUtility = static_cast<ICvScriptSystemUtility1*>(lua_touserdata(L, 1));
		lua_settop(L, 0);
		pUtility->RegisterScriptLibraries(L);
		return 0;
	}
}

// Loads lua51_x64.dll by full path and creates the main state. Returns false (and the reason) on failure.
bool HostLuaStartup(const wchar_t* wszLuaDll, char* szWhy, size_t cchWhy)
{
	s_hLua = LoadLibraryExW(wszLuaDll, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	if(!s_hLua)
	{
		sprintf_s(szWhy, cchWhy, "LoadLibrary(%ls) failed, error %lu", wszLuaDll, GetLastError());
		return false;
	}
	char szLog[MAX_PATH];
	sprintf_s(szLog, "%s\\Logs\\Lua.log", HostWorkDir());
	fopen_s(&s_pLuaLog, szLog, "w");

	s_L = luaL_newstate();	// first call through the delay-load thunk: resolves to the module loaded above
	if(!s_L)
	{
		sprintf_s(szWhy, cchWhy, "luaL_newstate returned NULL");
		return false;
	}
	luaL_openlibs(s_L);
	lua_pushcfunction(s_L, LuaPrint);
	lua_setglobal(s_L, "print");
	lua_newtable(s_L);
	lua_setglobal(s_L, "Threads");
	lua_newtable(s_L);
	lua_setglobal(s_L, "IncludeFileList");
	char szPath[MAX_PATH] = "";
	GetModuleFileNameA(s_hLua, szPath, MAX_PATH);
	sprintf_s(szWhy, cchWhy, "%s at %p, main state %p, stock libraries opened", szPath, (void*)s_hLua, (void*)s_L);
	return true;
}

bool HostLuaAvailable() { return s_L != NULL; }
HMODULE HostLuaModule() { return s_hLua; }

void HostLuaSetUtility(ICvScriptSystemUtility1* pUtility) { s_pUtility = pUtility; }

lua_State* HostLuaCreateThread(const char* szName)
{
	if(!s_L)
	{
		HostNote("CreateLuaThread(\"%s\") -> NULL (no Lua state: lua51_x64.dll not loaded)", szName ? szName : "");
		return NULL;
	}
	lua_State* T = lua_newthread(s_L);
	const int iRef = luaL_ref(s_L, LUA_REGISTRYINDEX);
	s_threads[T] = iRef;

	lua_newtable(s_L);						// env
	lua_pushstring(s_L, szName ? szName : "");
	lua_setfield(s_L, -2, "StateName");
	lua_newtable(s_L);						// {__index = _G}
	lua_pushvalue(s_L, LUA_GLOBALSINDEX);
	lua_setfield(s_L, -2, "__index");
	lua_setmetatable(s_L, -2);
	lua_pushvalue(s_L, -1);
	lua_xmove(s_L, T, 1);
	lua_replace(T, LUA_GLOBALSINDEX);		// the thread's globals are env

	ThreadEntryArgs args = { iRef, false };
	lua_pushcfunction(s_L, LuaSetThreadEntry);
	lua_pushlightuserdata(s_L, &args);
	lua_pushvalue(s_L, -3);
	if(lua_pcall(s_L, 2, 0, 0) != 0)
	{
		HostNote("CreateLuaThread(\"%s\"): Threads registration failed: %s", szName, lua_tostring(s_L, -1));
		lua_pop(s_L, 1);
	}
	lua_pop(s_L, 1);						// env

	if(s_pUtility)
	{
		HostNote("CreateLuaThread(\"%s\") -> %p; calling RegisterScriptLibraries", szName ? szName : "", (void*)T);
		if(lua_cpcall(T, LuaRegisterLibraries, s_pUtility) != 0)
		{
			HostNote("CreateLuaThread(\"%s\"): RegisterScriptLibraries failed: %s", szName, lua_tostring(T, -1));
			lua_pop(T, 1);
		}
	}
	else
		HostNote("CreateLuaThread(\"%s\") -> %p (no script-system utility yet: RegisterScriptLibraries not called)",
		         szName ? szName : "", (void*)T);
	return T;
}

void HostLuaFreeThread(lua_State* L)
{
	std::map<lua_State*, int>::iterator it = s_threads.find(L);
	if(!L || it == s_threads.end())
	{
		HostNote("FreeLuaThread(%p): unknown thread", (void*)L);
		return;
	}
	ThreadEntryArgs args = { it->second, true };
	if(lua_cpcall(s_L, LuaSetThreadEntry, &args) != 0)
		lua_pop(s_L, 1);
	lua_settop(L, 0);
	luaL_unref(s_L, LUA_REGISTRYINDEX, it->second);
	s_threads.erase(it);
	HostNote("FreeLuaThread(%p) done", (void*)L);
}

void HostLuaShutdown()
{
	if(s_L)
		lua_close(s_L);
	s_L = NULL;
	if(s_pLuaLog)
		fclose(s_pLuaLog);
	s_pLuaLog = NULL;
}
