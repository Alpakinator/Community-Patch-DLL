// The server's Lua (issues 1501, 1502): one main state in the shared lua51_x64.dll, the same Lua copy the DLL links
// through lua51_x64.lib (one Lua copy per process).
//
// Written from research/sources/civ5-headless/engine-contract.md section 10 (documents only) and the DLL's headers:
//   * CreateLuaThread(name): a registry-anchored lua_newthread whose globals are {StateName = name} with
//     {__index = _G}; Threads[thread] = env (rawset). The engine registers the DLL's script libraries nested in the
//     first thread (VP_LUAAPI, trace S1); later threads (Map Script) find them in _G (6.7).
//   * the EXE's extras (10.5): ipairs visits index 0; the debug library is hidden after boot; include() (10.3)
//     runs a file found through the VFS in the caller's environment and returns the table of included paths.
//   * GameEvents (10.1): listeners run all, in order, no short-circuit; a raising listener does not vote; handled is
//     true iff at least one listener is registered. CallHook's value is always false; CallTestAll: no listeners ->
//     unchanged, else true unless a listener returned false/nil; CallTestAny: no listeners -> unchanged, else true
//     iff a listener returned a truthy value; CallAccumulator: init + sum of numeric results.
//   * the args object: Push returns the 0-based index of the pushed value (10.1).
//   * the engine's Lua tables used by map scripts: GameInfo (rows by ID or Type, iteration with an SQL filter string
//     or a table of column values), GameDefines (Defines), DB.Query / DB.CreateQuery, Locale (over the real CvLocalization:
//     srv_loc.cpp, issue 1704), MapModData, Modding (no mods), Events / LuaEvents (no-op).
#include "srv_common.h"
#include <stdio.h>
#include <stdarg.h>
#include <map>
#include <string>

namespace
{
	lua_State* s_L = NULL;
	HMODULE s_hLua = NULL;
	ICvScriptSystemUtility1* s_pUtility = NULL;
	Database::Connection* s_pDb = NULL;
	std::map<lua_State*, int> s_threads;
	FILE* s_pLuaLog = NULL;
	bool s_bRegisteredInMain = false;
	volatile long s_lErrors = 0;

	void LogLine(const char* szState, const char* szText)
	{
		if(s_pLuaLog)
		{
			fprintf(s_pLuaLog, "%s: %s\n", szState, szText);
			fflush(s_pLuaLog);
		}
	}

	void LogError(lua_State* L, const char* szWhere)
	{
		InterlockedIncrement(&s_lErrors);
		const char* sz = lua_tostring(L, -1);
		char buf[4096];
		sprintf_s(buf, "[ERROR] %s: %s", szWhere, sz ? sz : "(non-string error)");
		LogLine("Runtime Error", buf);
		SrvLog("Lua error: %s", buf);
	}

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
		const std::string state = szState ? szState : "Main";
		lua_pop(L, 1);
		LogLine(state.c_str(), line.c_str());
		return 0;
	}

	struct ThreadEntryArgs { int iRef; bool bClear; };

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

	// Loads a file's text as a chunk named "@<vfs path>"; leaves the function (or the error) on the stack.
	int LoadChunk(lua_State* L, const std::string& strFull, const std::string& strVfs)
	{
		std::string data;
		if(!VfsReadFile(strFull, data))
		{
			lua_pushfstring(L, "cannot read %s", strFull.c_str());
			return LUA_ERRFILE;
		}
		const char* p = data.data();
		size_t n = data.size();
		if(n >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
			p += 3, n -= 3;	// UTF-8 byte order mark
		const std::string chunk = "@" + strVfs;
		return luaL_loadbuffer(L, p, n, chunk.c_str());
	}

	// include(name): runs the file in the caller's environment; returns {path} (empty when not found).
	int LuaInclude(lua_State* L)
	{
		const char* szName = luaL_checkstring(L, 1);
		std::string full, vfs;
		lua_newtable(L);
		const int iResult = lua_gettop(L);
		if(!VfsResolve(szName, full, vfs))
		{
			char sz[512];
			sprintf_s(sz, "include(\"%s\"): not found", szName);
			LogLine("include", sz);
			return 1;
		}
		if(LoadChunk(L, full, vfs) != 0)
		{
			LogError(L, "include");
			lua_pop(L, 1);
			return 1;
		}
		// Environment of the calling function (no debug library needed).
		lua_Debug ar;
		if(lua_getstack(L, 1, &ar) && lua_getinfo(L, "f", &ar))
		{
			lua_getfenv(L, -1);
			lua_setfenv(L, -3);
			lua_pop(L, 1);
		}
		if(lua_pcall(L, 0, 0, 0) != 0)
		{
			LogError(L, vfs.c_str());
			lua_pop(L, 1);
		}
		lua_pushstring(L, vfs.c_str());
		lua_rawseti(L, iResult, 1);
		lua_settop(L, iResult);
		return 1;
	}

	// ---- database access for GameInfo, GameDefines, DB ---------------------------------------------------

	void PushColumn(lua_State* L, Database::Results& r, int i)
	{
		switch(r.ColumnType(i))
		{
		case Database::COLTYPE_BOOL: lua_pushboolean(L, r.GetBool(i)); break;
		case Database::COLTYPE_INT: lua_pushinteger(L, (lua_Integer)r.GetInt64(i)); break;
		case Database::COLTYPE_FLOAT: lua_pushnumber(L, r.GetDouble(i)); break;
		case Database::COLTYPE_TEXT: lua_pushstring(L, r.GetText(i)); break;
		default: lua_pushnil(L); break;
		}
	}

	// __SrvQuery(sql, ...): array of row tables (column name -> value; NULL columns absent).
	int LuaQuery(lua_State* L)
	{
		const char* szSql = luaL_checkstring(L, 1);
		const int nArgs = lua_gettop(L);
		Database::Results* pr = new Database::Results();
		if(!s_pDb || !s_pDb->Execute(*pr, szSql))
		{
			delete pr;
			return luaL_error(L, "DB query failed: %s", szSql);
		}
		for(int a = 2; a <= nArgs; ++a)
		{
			switch(lua_type(L, a))
			{
			case LUA_TNUMBER:
			{
				const lua_Number d = lua_tonumber(L, a);
				if(d == (lua_Number)(int)d)
					pr->Bind(a - 1, (int)d);
				else
					pr->Bind(a - 1, (double)d);
				break;
			}
			case LUA_TBOOLEAN: pr->Bind(a - 1, lua_toboolean(L, a) ? 1 : 0); break;
			case LUA_TSTRING: pr->Bind(a - 1, lua_tostring(L, a)); break;
			default: pr->BindNULL(a - 1); break;
			}
		}
		lua_newtable(L);
		int iRow = 0;
		while(pr->Step())
		{
			lua_newtable(L);
			const int nCols = pr->ColumnCount();
			for(int c = 0; c < nCols; ++c)
			{
				PushColumn(L, *pr, c);
				if(lua_isnil(L, -1))
				{
					lua_pop(L, 1);
					continue;
				}
				lua_setfield(L, -2, pr->ColumnName(c));
			}
			lua_rawseti(L, -2, ++iRow);
		}
		delete pr;
		return 1;
	}

	// Engine tables, built in Lua over __SrvQuery. Iteration order: rowid (SELECT * without ORDER BY).
	const char* kEngineTablesLua = R"LUA(
local Query = __SrvQuery
__SrvQuery = nil
local rawget, rawset, setmetatable, type, pairs, ipairs, tostring, error = rawget, rawset, setmetatable, type, pairs, ipairs, tostring, error

local function rowsIterator(rows)
	local i = 0
	return function() i = i + 1; return rows[i] end
end

local function newTable(name)
	local all, byKey = nil, {}
	local function load()
		if all then return end
		all = Query("SELECT * FROM " .. name)
		for _, row in ipairs(all) do
			if row.ID ~= nil then byKey[row.ID] = row end
			if row.Type ~= nil then byKey[row.Type] = row end
		end
	end
	-- A userdata, so that #GameInfo.X works (Lua 5.1 honours __len only for userdata): VP's AssignStartingPlots.lua
	-- computes MAX_RESOURCE_INDEX = #GameInfo.Resources - 1, i.e. # is the number of rows.
	local t = newproxy(true)
	local mt = getmetatable(t)
	mt.__len = function() load(); return #all end
	mt.__tostring = function() return "GameInfo." .. name end
	for k, v in pairs({
		__index = function(_, k) load(); return byKey[k] end,
		__call = function(_, filter)
			load()
			if filter == nil then return rowsIterator(all) end
			if type(filter) == "string" then
				return rowsIterator(Query("SELECT * FROM " .. name .. " WHERE " .. filter))
			end
			if type(filter) == "table" then
				local out = {}
				for _, row in ipairs(all) do
					local ok = true
					for k, v in pairs(filter) do if row[k] ~= v then ok = false; break end end
					if ok then out[#out + 1] = row end
				end
				return rowsIterator(out)
			end
			error("GameInfo." .. name .. ": bad filter " .. tostring(filter))
		end,
	}) do mt[k] = v end
	return t
end

GameInfo = setmetatable({}, { __index = function(t, name)
	local exists = Query("SELECT name FROM sqlite_master WHERE type IN ('table','view') AND name = ?", name)[1]
	if not exists then return nil end
	local tbl = newTable(name)
	rawset(t, name, tbl)
	return tbl
end })

GameDefines = {}
for _, row in ipairs(Query("SELECT Name, Value FROM Defines")) do GameDefines[row.Name] = row.Value end

DB = {}
function DB.Query(sql, ...) return rowsIterator(Query(sql, ...)) end
function DB.CreateQuery(sql) return function(...) return rowsIterator(Query(sql, ...)) end end

-- Locale: installed by LocInstallLua (srv_loc.cpp) before this chunk runs.

MapModData = {}
Modding = {
	GetActivatedMods = function() return {} end,
	GetActivatedModEntryPoints = function() return function() return nil end end,
	IsModActive = function() return false end,
}

local function noOpEventTable()
	return setmetatable({}, { __index = function(t, k)
		local e = setmetatable({ Add = function() end, Remove = function() end, RemoveAll = function() end,
			Count = function() return 0 end }, { __call = function() end })
		rawset(t, k, e)
		return e
	end })
end
Events = noOpEventTable()
LuaEvents = noOpEventTable()

-- GameEvents: the DLL's hooks are dispatched by the server (LuaDispatch) over __srvlisteners.
local function newEvent()
	local l = {}
	local e = {}
	e.__srvlisteners = l
	e.Add = function(f) l[#l + 1] = f end
	e.Remove = function(f) for i = #l, 1, -1 do if l[i] == f then table.remove(l, i) end end end
	e.RemoveAll = function() for i = #l, 1, -1 do l[i] = nil end end
	e.Count = function() return #l end
	e.Call = function(...) for i = 1, #l do l[i](...) end end
	return setmetatable(e, { __call = function(_, ...) return e.Call(...) end })
end
GameEvents = setmetatable({}, { __index = function(t, k)
	local e = newEvent()
	rawset(t, k, e)
	return e
end })
)LUA";

	const char* kIpairsLua = R"LUA(
local rawget = rawget
local aux = ipairs({})
ipairs = function(t)
	if rawget(t, 0) ~= nil then return aux, t, -1 end
	return aux, t, 0
end
-- Library additions the game's scripts use that stock Lua 5.1 lacks (found by scanning the game's Lua for
-- non-stock table/math members, 2026-11-10). table.fill(v, n) fills indices 1..n: AssignStartingPlots.lua notes
-- that index 0 of such a table is nil ("will crash if a Luxury is attached to resource ID 0").
-- Two forms are used: table.fill(v, n) returns a new table, table.fill(t, v, n) fills t (CP's MapmakerUtilities.lua).
function table.fill(a, b, c)
	local t, v, n
	if c ~= nil then t, v, n = a, b, c else t, v, n = {}, a, b end
	for i = 1, n do t[i] = v end
	return t
end
function table.count(t)
	local n = 0
	for _ in pairs(t) do n = n + 1 end
	return n
end
function math.clamp(x, lo, hi)
	if x < lo then return lo elseif x > hi then return hi end
	return x
end
)LUA";

	// ---- args -----------------------------------------------------------------------------------------------

	class SrvArgs : public ICvEngineScriptSystemArgs1
	{
	public:
		struct Val { ArgType t; bool b; int i; float f; std::string s; };
		std::vector<Val> v;
		unsigned int Add(const Val& x) { v.push_back(x); return (unsigned int)(v.size() - 1); }
		unsigned int Push(bool b) override { Val x; x.t = ARGTYPE_BOOL; x.b = b; x.i = 0; x.f = 0; return Add(x); }
		unsigned int Push(const char* s, size_t n) override
		{
			Val x; x.t = ARGTYPE_STRING; x.b = false; x.i = 0; x.f = 0;
			if(s)
				x.s.assign(s, n);
			return Add(x);
		}
		unsigned int Push(int i) override { Val x; x.t = ARGTYPE_INT; x.b = false; x.i = i; x.f = 0; return Add(x); }
		unsigned int Push(float f) override { Val x; x.t = ARGTYPE_FLOAT; x.b = false; x.i = 0; x.f = f; return Add(x); }
		unsigned int PushNULL() override { Val x; x.t = ARGTYPE_NULL; x.b = false; x.i = 0; x.f = 0; return Add(x); }
		unsigned int Count() override { return (unsigned int)v.size(); }
		ArgType GetType(unsigned int k) override { return k < v.size() ? v[k].t : ARGTYPE_NONE; }
		bool GetBool(unsigned int k, bool& b) override
		{
			if(k >= v.size() || v[k].t != ARGTYPE_BOOL) return false;
			b = v[k].b; return true;
		}
		bool GetFloat(unsigned int k, float& f) override
		{
			if(k >= v.size() || v[k].t != ARGTYPE_FLOAT) return false;
			f = v[k].f; return true;
		}
		bool GetInt(unsigned int k, int& i) override
		{
			if(k >= v.size() || v[k].t != ARGTYPE_INT) return false;
			i = v[k].i; return true;
		}
		bool GetString(unsigned int k, char*& s) override
		{
			if(k >= v.size() || v[k].t != ARGTYPE_STRING) return false;
			s = &v[k].s[0]; return true;
		}
		void PushAll(lua_State* L) const
		{
			for(size_t k = 0; k < v.size(); ++k)
			{
				switch(v[k].t)
				{
				case ARGTYPE_BOOL: lua_pushboolean(L, v[k].b); break;
				case ARGTYPE_INT: lua_pushinteger(L, v[k].i); break;
				case ARGTYPE_FLOAT: lua_pushnumber(L, v[k].f); break;
				case ARGTYPE_STRING: lua_pushlstring(L, v[k].s.data(), v[k].s.size()); break;
				default: lua_pushnil(L); break;
				}
			}
		}
	};
}

// ---------------------------------------------------------------------------------------------------------

bool LuaStartup(const wchar_t* wszLuaDll, char* szWhy, size_t cchWhy)
{
	s_hLua = LoadLibraryExW(wszLuaDll, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	if(!s_hLua)
	{
		sprintf_s(szWhy, cchWhy, "LoadLibrary(%ls) failed, error %lu", wszLuaDll, GetLastError());
		return false;
	}
	char szLog[MAX_PATH];
	sprintf_s(szLog, "%s\\Logs\\Lua.log", SrvRunDir());
	s_pLuaLog = _fsopen(szLog, "w", _SH_DENYWR);
	s_L = luaL_newstate();
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
	const std::vector<std::string>& list = VfsLuaFileList();
	for(size_t i = 0; i < list.size(); ++i)
	{
		lua_pushstring(s_L, list[i].c_str());
		lua_rawseti(s_L, -2, (int)i + 1);
	}
	lua_setglobal(s_L, "IncludeFileList");
	lua_pushcfunction(s_L, LuaInclude);
	lua_setglobal(s_L, "include");
	if(!LuaRunString(kIpairsLua, "=ipairs"))
	{
		sprintf_s(szWhy, cchWhy, "installing the engine ipairs failed");
		return false;
	}
	char szPath[MAX_PATH] = "";
	GetModuleFileNameA(s_hLua, szPath, MAX_PATH);
	sprintf_s(szWhy, cchWhy, "%s, main state %p, stock libraries, engine ipairs, include over %zu Lua names", szPath,
	          (void*)s_L, list.size());
	return true;
}

HMODULE LuaModule() { return s_hLua; }
lua_State* LuaMain() { return s_L; }
void LuaSetUtility(ICvScriptSystemUtility1* pUtility) { s_pUtility = pUtility; }
long LuaErrorCount() { return s_lErrors; }
void LuaCountError(const char* szWhere, const char* szText)
{
	InterlockedIncrement(&s_lErrors);
	char buf[4096];
	sprintf_s(buf, "[ERROR] %s: %s", szWhere, szText ? szText : "?");
	LogLine("Runtime Error", buf);
}

lua_State* LuaCreateThread(const char* szName)
{
	if(!s_L)
		return NULL;
	lua_State* T = lua_newthread(s_L);
	const int iRef = luaL_ref(s_L, LUA_REGISTRYINDEX);
	s_threads[T] = iRef;

	lua_newtable(s_L);
	lua_pushstring(s_L, szName ? szName : "");
	lua_setfield(s_L, -2, "StateName");
	lua_newtable(s_L);
	lua_pushvalue(s_L, LUA_GLOBALSINDEX);
	lua_setfield(s_L, -2, "__index");
	lua_setmetatable(s_L, -2);
	lua_pushvalue(s_L, -1);
	lua_xmove(s_L, T, 1);
	lua_replace(T, LUA_GLOBALSINDEX);

	ThreadEntryArgs args = { iRef, false };
	lua_pushcfunction(s_L, LuaSetThreadEntry);
	lua_pushlightuserdata(s_L, &args);
	lua_pushvalue(s_L, -3);
	if(lua_pcall(s_L, 2, 0, 0) != 0)
		lua_pop(s_L, 1);
	lua_pop(s_L, 1);

	// The engine registers the DLL's libraries nested in the first thread's creation (VP_LUAAPI, boot); once they
	// are registered in the main state, later threads find them through __index = _G.
	if(s_pUtility && !s_bRegisteredInMain)
	{
		if(lua_cpcall(T, LuaRegisterLibraries, s_pUtility) != 0)
		{
			LogError(T, "RegisterScriptLibraries");
			lua_pop(T, 1);
		}
	}
	return T;
}

void LuaFreeThread(lua_State* L)
{
	std::map<lua_State*, int>::iterator it = s_threads.find(L);
	if(!L || it == s_threads.end())
		return;
	ThreadEntryArgs args = { it->second, true };
	if(lua_cpcall(s_L, LuaSetThreadEntry, &args) != 0)
		lua_pop(s_L, 1);
	lua_settop(L, 0);
	luaL_unref(s_L, LUA_REGISTRYINDEX, it->second);
	s_threads.erase(it);
}

bool LuaLoadFile(lua_State* L, const char* szFile)
{
	std::string full, vfs;
	if(!VfsResolve(szFile, full, vfs))
	{
		char sz[600];
		sprintf_s(sz, "LoadFile(\"%s\"): not found", szFile ? szFile : "");
		LogLine("LoadFile", sz);
		return false;
	}
	if(LoadChunk(L, full, vfs) != 0)
	{
		LogError(L, "LoadFile");
		lua_pop(L, 1);
		return false;
	}
	if(lua_pcall(L, 0, 0, 0) != 0)
	{
		LogError(L, vfs.c_str());
		lua_pop(L, 1);
		return false;
	}
	SrvLog("LoadFile(\"%s\") -> %s", szFile, full.c_str());
	return true;
}

void LuaHideDebug()
{
	LuaRunString("debug = nil", "=hide debug");
}

void LuaInstallEngineTables(Database::Connection* pDb)
{
	s_pDb = pDb;
	lua_pushcfunction(s_L, LuaQuery);
	lua_setglobal(s_L, "__SrvQuery");
	LocInstallLua(s_L);
	LuaRunString(kEngineTablesLua, "=engine tables");
	// The DLL's libraries in the main state, as the engine registers them in its InGame state.
	if(s_pUtility)
	{
		if(lua_cpcall(s_L, LuaRegisterLibraries, s_pUtility) != 0)
		{
			LogError(s_L, "RegisterScriptLibraries(main)");
			lua_pop(s_L, 1);
		}
		s_bRegisteredInMain = true;
	}
}

bool LuaRunString(const char* szChunk, const char* szName, std::string* pstrErr)
{
	if(luaL_loadbuffer(s_L, szChunk, strlen(szChunk), szName) != 0 || lua_pcall(s_L, 0, 0, 0) != 0)
	{
		if(pstrErr)
			*pstrErr = lua_tostring(s_L, -1) ? lua_tostring(s_L, -1) : "?";
		LogError(s_L, szName);
		lua_pop(s_L, 1);
		return false;
	}
	return true;
}

bool LuaRunFile(const char* szPath, std::string* pstrErr)
{
	std::string data;
	if(!VfsReadFile(szPath, data))
	{
		if(pstrErr)
			*pstrErr = std::string("cannot read ") + szPath;
		return false;
	}
	const std::string name = std::string("@") + szPath;
	if(luaL_loadbuffer(s_L, data.data(), data.size(), name.c_str()) != 0 || lua_pcall(s_L, 0, 0, 0) != 0)
	{
		if(pstrErr)
			*pstrErr = lua_tostring(s_L, -1) ? lua_tostring(s_L, -1) : "?";
		LogError(s_L, szPath);
		lua_pop(s_L, 1);
		return false;
	}
	return true;
}

void LuaShutdown()
{
	if(s_L)
		lua_close(s_L);
	s_L = NULL;
	if(s_pLuaLog)
		fclose(s_pLuaLog);
	s_pLuaLog = NULL;
}

// Error handler that appends a stack trace (no debug library needed).
static int LuaTraceback(lua_State* L)
{
	std::string s = lua_tostring(L, 1) ? lua_tostring(L, 1) : "(non-string error)";
	lua_Debug ar;
	for(int level = 1; level < 24 && lua_getstack(L, level, &ar); ++level)
	{
		lua_getinfo(L, "Sln", &ar);
		char sz[512];
		sprintf_s(sz, "\n    %s:%d %s", ar.short_src, ar.currentline, ar.name ? ar.name : "?");
		s += sz;
	}
	lua_pushstring(L, s.c_str());
	return 1;
}

int LuaPCall(lua_State* L, int nArgs, int nRet)
{
	const int base = lua_gettop(L) - nArgs;
	lua_pushcfunction(L, LuaTraceback);
	lua_insert(L, base);
	const int rc = lua_pcall(L, nArgs, nRet, base);
	lua_remove(L, base);
	return rc;
}

ICvEngineScriptSystemArgs1* LuaCreateArgs() { return new SrvArgs(); }
void LuaDestroyArgs(ICvEngineScriptSystemArgs1* p) { delete static_cast<SrvArgs*>(p); }

bool LuaDispatch(int kind, const char* szName, ICvEngineScriptSystemArgs1* pArgs, bool* pb, int* pi, float* pf)
{
	if(kind == 0 && pb)
		*pb = false;	// CallHook's value is always false
	if(!s_L || !szName)
		return false;
	lua_State* L = s_L;
	const int top = lua_gettop(L);
	lua_getglobal(L, "GameEvents");
	if(!lua_istable(L, -1))
	{
		lua_settop(L, top);
		return false;
	}
	lua_pushstring(L, szName);
	lua_rawget(L, -2);
	if(!lua_istable(L, -1))
	{
		lua_settop(L, top);
		return false;	// never indexed: no listener
	}
	lua_getfield(L, -1, "__srvlisteners");
	if(!lua_istable(L, -1))
	{
		lua_settop(L, top);
		return false;
	}
	const int iList = lua_gettop(L);
	const int n = (int)lua_objlen(L, iList);
	if(n == 0)
	{
		lua_settop(L, top);
		return false;
	}
	const SrvArgs* pA = static_cast<const SrvArgs*>(pArgs);
	bool bAll = true, bAny = false;
	double dSum = 0;
	for(int k = 1; k <= n; ++k)
	{
		lua_rawgeti(L, iList, k);
		if(!lua_isfunction(L, -1))
		{
			lua_pop(L, 1);
			continue;
		}
		if(pA)
			pA->PushAll(L);
		if(lua_pcall(L, pA ? (int)pA->v.size() : 0, 1, 0) != 0)
		{
			LogError(L, szName);
			lua_pop(L, 1);
			continue;	// a raising listener does not vote
		}
		if(kind == 1 && !lua_toboolean(L, -1))
			bAll = false;
		if(kind == 2 && lua_toboolean(L, -1))
			bAny = true;
		if((kind == 3 || kind == 4) && lua_type(L, -1) == LUA_TNUMBER)
			dSum += lua_tonumber(L, -1);
		lua_pop(L, 1);
	}
	lua_settop(L, top);
	switch(kind)
	{
	case 1: if(pb) *pb = bAll; break;
	case 2: if(pb) *pb = bAny; break;
	case 3: if(pi) *pi = *pi + (int)dSum; break;
	case 4: if(pf) *pf = *pf + (float)dSum; break;
	default: break;
	}
	return true;
}
