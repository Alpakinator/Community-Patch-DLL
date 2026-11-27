// proxy_luaexec.cpp - the harness's external Lua channel served by the x86 proxy against the ENGINE's Lua VM (epic
// 2500, issue 2505). Hand-written. Header: proxy_luaexec.h. Client: tools/harness/lua.py (unchanged).
//
// Ported from the Community-Patch-DLL fork (GPL-3.0), branch instance-aware-instruments, commit 440878983:
//   CvGameCoreDLL_Expansion2/InstanceNames.h                  -> namespace InstanceNames below (names, rule, log line)
//   CvGameCoreDLL_Expansion2/Lua/CvLuaSupport.cpp             -> LUAEXEC_RUNNER (verbatim), RunExternalLua,
//                                                               IsExternalLuaEnabled, PollExternalLuaRequest (Poll)
//   CvGameCoreDLL_Expansion2/MemoryDiagnostics.cpp            -> GetProbeLuaState (the persistent thread)
// The protocol is the DLL's, byte for byte: events Local\VPLuaExec / Local\VPLuaExecDone, with "_<id>" under
// VP_INSTANCE=<id> ([A-Za-z0-9_-]{1,32}; anything else REJECTED -> legacy names and a log line); request
// <cache>luaexec_request.lua (at most 1 MB, read with delete-on-close, optional UTF-8 BOM, headers --@id= and
// --@state=), result <cache>luaexec_result.txt ("id=..\tstate=..\tms=..\tturn=..\n" + the runner's text), the cache
// folder being the real engine's ICvEngineUtility1::GetCacheFolderPath(); opt-in by VP_LUAEXEC=1 (=0 forces off) or
// <cache>luaexec.enabled (rechecked every 2 s). Differences from the DLL, each forced by the proxy's position:
//   - the persistent thread is created by the proxy (OnGameDatabaseCached, from this file's hook of
//     ICvDLLDatabaseUtility1::CacheGameDatabaseData on the main thread), named "VP_LUAEXEC";
//   - the game-core lock is the server's: the DLL's Has/Release/GetGameCoreLock dance becomes
//     VpProxy_CoreLockYield/Resume (proxy_luaexec.h; reasoning at Poll below);
//   - turn= comes from running "return Game.GetGameTurn()" through the same runner after the request (the proxy has
//     no CvGame); -1 when that fails.
//   - ticket 0498 (the modpack form, installer-notes 14.16): the runner sets the thread's own globals
//     (LUA_GLOBALSINDEX) to the target environment while a chunk runs, with the UI contexts' current ExposedMembers
//     in front of it when the target's own is another, because the EXE's C __index of UI objects (LuaContext,
//     controls) finds their methods in the CALLING thread's global ExposedMembers, and in the modpack form _G and
//     this thread hold an older table; and when this thread's own environment holds only the DLL's registration
//     globals (no base library, no GameInfo) it is given the real _G behind them, once. RunExternalLua keeps the
//     thread's own environment in the registry and puts it back before and after every run.
//   - ticket 0591 (epic 0590): Firaxis' build 26577625 sets _G.loadstring to nil in the engine's one Lua VM
//     (research/02-exe-internals/new-exe-build-26577625.md, "The Lua sandbox"). The runner never reads _G.loadstring:
//     RunExternalLua passes it a C function (LuaExecCompile, luaL_loadbuffer: the C API that build left alone) as its
//     third argument, and without one the runner compiles with load(reader), which that build keeps. Unit test:
//     proxy/tests/luaexec/ (it compiles this file with VP_LUAEXEC_UNIT_TEST, which leaves out the engine parts).
#include "proxy_luaexec.h"  // declarations only; also in the unit tests (ticket 0805's InertImageValues)
#ifndef VP_LUAEXEC_UNIT_TEST
#include "proxy_rt_internal.h"
#include "proxy_ifaces.gen.h"
#else
#include <windows.h>
#endif
// Issue 3518: the Lua 5.1 C API is included here, not taken from the DLL's precompiled header. Lua/CvLuaSupport.h
// included <lua.h>, <lualib.h> and <lauxlib.h> up to Release-5.4.8; on upstream master it no longer does.
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#ifndef VP_LUAEXEC_UNIT_TEST
using namespace ProxyRtInt;

// ============================================================================================ link-time fallbacks
// The accessors of proxy_luaexec.h are defined by the proxy runtime once its owner adds them; until then (or if they
// are never added) the linker resolves them to these. x86 C names carry a leading underscore.
extern "C" ICvEngineUtility1* VpProxyLuaExec_NoEngineUtility() { return NULL; }
extern "C" int VpProxyLuaExec_NoCoreLockYield() { return -1; }
extern "C" void VpProxyLuaExec_NoCoreLockResume(int) {}
#pragma comment(linker, "/alternatename:_VpProxy_EngineUtility=_VpProxyLuaExec_NoEngineUtility")
#pragma comment(linker, "/alternatename:_VpProxy_CoreLockYield=_VpProxyLuaExec_NoCoreLockYield")
#pragma comment(linker, "/alternatename:_VpProxy_CoreLockResume=_VpProxyLuaExec_NoCoreLockResume")
#endif  // VP_LUAEXEC_UNIT_TEST

// ============================================================================================ InstanceNames
// From the fork's CvGameCoreDLL_Expansion2/InstanceNames.h @ 440878983 (GPL-3.0): the Lua channel's two names only
// (the memory snapshot and TurnByTurn objects belong to the DLL's instruments, which do not run in the proxy). The
// format strings "Local\\VPLuaExec%s%s" are what tools/instances/provision.py looks for to classify a binary as
// "instance-aware" (lua_channel_names), so they stay literally as in the DLL.
namespace InstanceNames
{
const DWORD MAX_ID_CHARS = 32;

struct Names
{
	char szId[MAX_ID_CHARS + 1];  // "" for the legacy names
	char szLuaExecRequest[64];
	char szLuaExecDone[64];
};

inline bool IsValidId(const char* sz, DWORD dwLength)
{
	if (dwLength < 1 || dwLength > MAX_ID_CHARS)
		return false;
	for (DWORD i = 0; i < dwLength; i++)
	{
		const char c = sz[i];
		const bool bOk = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
		if (!bOk)
			return false;
	}
	return true;
}

// The DLL's line format, to the same file (crashlogs\instruments.log relative to the current folder: the install
// folder for the EXE; no folder, no log) and to the debugger; also to vpproxy.log.
inline void AppendToLog(const char* szMessage)
{
	SYSTEMTIME kNow;
	GetLocalTime(&kNow);
	char szLine[1024];
	_snprintf_s(szLine, sizeof(szLine), _TRUNCATE, "%04d-%02d-%02d %02d:%02d:%02d  pid %lu  %s\r\n",
		kNow.wYear, kNow.wMonth, kNow.wDay, kNow.wHour, kNow.wMinute, kNow.wSecond, GetCurrentProcessId(), szMessage);
	OutputDebugStringA(szLine);
	Log("%s", szMessage);

	HANDLE hLog = CreateFileA("crashlogs\\instruments.log", FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hLog == INVALID_HANDLE_VALUE)
		return;
	DWORD dwWritten = 0;
	WriteFile(hLog, szLine, static_cast<DWORD>(strlen(szLine)), &dwWritten, NULL);
	CloseHandle(hLog);
}

inline void Resolve(Names& kNames)
{
	char szValue[MAX_ID_CHARS + 2];  // one more than allowed, to tell "too long" from "exactly 32"
	SetLastError(ERROR_SUCCESS);
	const DWORD dwLength = GetEnvironmentVariableA("VP_INSTANCE", szValue, sizeof(szValue));
	const DWORD dwError = GetLastError();

	char szWhy[96] = "";
	kNames.szId[0] = '\0';
	if (dwLength == 0 && dwError == ERROR_ENVVAR_NOT_FOUND)
		strcpy_s(szWhy, sizeof(szWhy), "VP_INSTANCE unset");
	else if (dwLength >= sizeof(szValue))
		_snprintf_s(szWhy, sizeof(szWhy), _TRUNCATE, "VP_INSTANCE REJECTED (%lu chars; must be 1-32 of [A-Za-z0-9_-])", dwLength - 1);
	else if (!IsValidId(szValue, dwLength))
		_snprintf_s(szWhy, sizeof(szWhy), _TRUNCATE, "VP_INSTANCE REJECTED (%lu chars; must be 1-32 of [A-Za-z0-9_-])", dwLength);
	else
	{
		memcpy(kNames.szId, szValue, dwLength);
		kNames.szId[dwLength] = '\0';
	}

	const char* szSuffix = (kNames.szId[0] != '\0') ? "_" : "";
	_snprintf_s(kNames.szLuaExecRequest, sizeof(kNames.szLuaExecRequest), _TRUNCATE, "Local\\VPLuaExec%s%s", szSuffix, kNames.szId);
	_snprintf_s(kNames.szLuaExecDone, sizeof(kNames.szLuaExecDone), _TRUNCATE, "Local\\VPLuaExecDone%s%s", szSuffix, kNames.szId);

	char szMessage[640];
	if (kNames.szId[0] != '\0')
		_snprintf_s(szMessage, sizeof(szMessage), _TRUNCATE, "instance names (proxy Lua channel): VP_INSTANCE=%s;", kNames.szId);
	else
		_snprintf_s(szMessage, sizeof(szMessage), _TRUNCATE, "instance names (proxy Lua channel): %s, legacy names;", szWhy);
	const size_t uiUsed = strlen(szMessage);
	_snprintf_s(szMessage + uiUsed, sizeof(szMessage) - uiUsed, _TRUNCATE, " %s %s", kNames.szLuaExecRequest, kNames.szLuaExecDone);
	AppendToLog(szMessage);
}

//! The effective names. The first call reads VP_INSTANCE and logs; later calls return the same names.
//! Safe from any thread: a racing caller waits for the first one to finish.
inline const Names& Get()
{
	static Names s_kNames;               // zero-initialised, no constructor: no hidden init guard
	static volatile LONG s_lState = 0;   // 0 not started, 1 resolving, 2 done
	if (s_lState == 2)
		return s_kNames;
	if (InterlockedCompareExchange(&s_lState, 1, 0) == 0)
	{
		Resolve(s_kNames);
		InterlockedExchange(&s_lState, 2);
	}
	else
	{
		while (s_lState != 2)
			Sleep(0);
	}
	return s_kNames;
}
}  // namespace InstanceNames

// ============================================================================================ the channel
namespace
{
// Event names: Local\VPLuaExec and Local\VPLuaExecDone, with "_<id>" under VP_INSTANCE (InstanceNames above).
const wchar_t* const LUAEXEC_REQUEST_FILE = L"luaexec_request.lua";
const wchar_t* const LUAEXEC_RESULT_FILE = L"luaexec_result.txt";
const wchar_t* const LUAEXEC_ENABLE_FILE = L"luaexec.enabled";
const DWORD LUAEXEC_MAX_REQUEST_BYTES = 1024 * 1024;
const DWORD LUAEXEC_ENABLE_RECHECK_MS = 2000;
const char* const LUAEXEC_THREAD_NAME = "VP_LUAEXEC";

//! From the fork's Lua/CvLuaSupport.cpp @ 440878983 (GPL-3.0), verbatim but for the two 0498 blocks and the 0591
//! compiler marked below. The Lua half, compiled per request and called with the registry, this thread's own
//! environment (T) and the C compile function (0591: LuaExecCompile; may be absent, then load(reader)). It returns the
//! function that runs one chunk: find the target environment by StateName, give the chunk its own environment that
//! reads and writes through to the target (so globals persist there, as they do in FireTuner's console) but captures
//! print, run it under xpcall with a traceback, and render every return value as text - all under size caps, because
//! this runs inside a process that is short of address space. Everything is reached through the real global table,
//! because this thread's own environment is wrapped by VP_LUAAPI's registration listeners.
const char* const LUAEXEC_RUNNER =
"local REG, T = ...\n"
"local _, _, compileC = ...\n"
"local G = REG._LOADED._G\n"
"local pairs, ipairs, type, tostring, select, pcall = G.pairs, G.ipairs, G.type, G.tostring, G.select, G.pcall\n"
"local rawget, rawset, setfenv, xpcall = G.rawget, G.rawset, G.setfenv, G.xpcall\n"
"local setmetatable = G.setmetatable\n"
"local getmetatable = (G.debug and G.debug.getmetatable) or G.getmetatable\n"
"local tconcat, tsort = G.table.concat, G.table.sort\n"
"local sformat, smatch = G.string.format, G.string.match\n"
"local traceback = G.debug and G.debug.traceback\n"
"\n"
"-- Caps, so that returning _G or printing in a loop cannot freeze the game or exhaust its address space.\n"
"local RESULT_LIMIT = 1048576\n"
"local PRINT_LIMIT = 262144\n"
"\n"
"-- 0591: build 26577625 removes the global string compiler, so this text never names it: the chunk is compiled by\n"
"-- the C function the caller passes (luaL_loadbuffer), or, without one, by load() with a reader, which that build keeps.\n"
"local function compile(code, name)\n"
"\tif type(compileC) == 'function' then return compileC(code, name) end\n"
"\tlocal load = G.load\n"
"\tif type(load) ~= 'function' then return nil, 'no compiler: no C compile function was passed and load is nil' end\n"
"\tlocal src = code\n"
"\treturn load(function() local s = src ; src = nil ; return s end, name)\n"
"end\n"
"\n"
"local persist = rawget(REG, 'VP_LUAEXEC_PERSIST')\n"
"if type(persist) ~= 'table' then persist = {} ; rawset(REG, 'VP_LUAEXEC_PERSIST', persist) end\n"
"\n"
"-- 0498: in the modpack form this thread's own environment holds only the DLL's registration globals (Game, Players,\n"
"-- the *Types tables, StateName; no base library, no GameInfo, no metatable). Put the real globals behind them, once.\n"
"if type(T) == 'table' and T ~= G and rawget(T, 'type') == nil and getmetatable(T) == nil then\n"
"\tsetmetatable(T, { __index = G })\n"
"end\n"
"\n"
"-- G.Threads maps each Lua thread to its environment. If it is a proxy, its contents sit behind __index.\n"
"local function threadEnvs()\n"
"\tlocal list = {}\n"
"\tlocal threads = G.Threads\n"
"\tif type(threads) == 'table' then\n"
"\t\tfor _, env in pairs(threads) do list[#list + 1] = env end\n"
"\t\tif #list == 0 then\n"
"\t\t\tlocal mt = getmetatable(threads)\n"
"\t\t\tlocal backing = type(mt) == 'table' and rawget(mt, '__index')\n"
"\t\t\tif type(backing) == 'table' then\n"
"\t\t\t\tfor _, env in pairs(backing) do list[#list + 1] = env end\n"
"\t\t\tend\n"
"\t\tend\n"
"\tend\n"
"\treturn list\n"
"end\n"
"\n"
"local function stateNameOf(env)\n"
"\tif type(env) ~= 'table' then return nil end\n"
"\tlocal name = rawget(env, 'StateName')\n"
"\tif name == nil then\n"
"\t\tlocal ok, value = pcall(function() return env.StateName end)\n"
"\t\tif ok then name = value end\n"
"\tend\n"
"\treturn name\n"
"end\n"
"\n"
"local function stateNames()\n"
"\tlocal names, seen = {}, {}\n"
"\tfor _, env in ipairs(threadEnvs()) do\n"
"\t\tlocal name = stateNameOf(env)\n"
"\t\tif name ~= nil then\n"
"\t\t\tname = tostring(name)\n"
"\t\t\tif not seen[name] then seen[name] = true ; names[#names + 1] = name end\n"
"\t\tend\n"
"\tend\n"
"\ttsort(names)\n"
"\treturn names\n"
"end\n"
"\n"
"local function findState(name)\n"
"\tfor _, env in ipairs(threadEnvs()) do\n"
"\t\tif stateNameOf(env) == name then return env end\n"
"\tend\n"
"end\n"
"\n"
"-- The game-core API (Game, Players, Map...) is registered into thread environments, not into _G, so\n"
"-- \"Main\" means the first environment that can see it: this thread's own, then _G, then InGame, then any.\n"
"local function seesGame(env)\n"
"\tif type(env) ~= 'table' then return false end\n"
"\tlocal ok, game = pcall(function() return env.Game end)\n"
"\treturn ok and type(game) == 'table'\n"
"end\n"
"\n"
"-- 0498: UI objects' methods work only when the calling thread's globals hold the UI contexts' current\n"
"-- ExposedMembers (the 'Metatable for type: ...' tables) and ExposedTypes. The UI contexts share one pair; in the\n"
"-- modpack form _G holds another, older pair and this thread's own environment none or another. The pair of the\n"
"-- ExposedMembers table most environments hold is the current one.\n"
"local EXPOSED = { 'ExposedMembers', 'ExposedTypes' }\n"
"local function currentExposed()\n"
"\tlocal count, best, bestN = {}, nil, 0\n"
"\tfor _, env in ipairs(threadEnvs()) do\n"
"\t\tlocal em = type(env) == 'table' and rawget(env, 'ExposedMembers')\n"
"\t\tif type(em) == 'table' then\n"
"\t\t\tlocal n = (count[em] or 0) + 1\n"
"\t\t\tcount[em] = n\n"
"\t\t\tif n > bestN then best, bestN = env, n end\n"
"\t\tend\n"
"\tend\n"
"\tif best == nil then return nil end\n"
"\tlocal pair = {}\n"
"\tfor _, k in ipairs(EXPOSED) do pair[k] = rawget(best, k) end\n"
"\treturn pair\n"
"end\n"
"\n"
"local function resolveMain()\n"
"\tif seesGame(T) then return T, 'thread' end\n"
"\tif seesGame(G) then return G, '_G' end\n"
"\tlocal inGame = findState('InGame')\n"
"\tif seesGame(inGame) then return inGame, 'InGame' end\n"
"\tfor _, env in ipairs(threadEnvs()) do\n"
"\t\tif type(env) == 'table' and type(rawget(env, 'Game')) == 'table' then\n"
"\t\t\treturn env, tostring(stateNameOf(env))\n"
"\t\tend\n"
"\tend\n"
"\treturn G, '_G'\n"
"end\n"
"\n"
"local function newRenderer(limit)\n"
"\tlocal used = 0\n"
"\tlocal function render(v, depth, seen)\n"
"\t\tif used > limit then return '<truncated>' end\n"
"\t\tlocal t = type(v)\n"
"\t\tif t == 'table' and not seen[v] and depth > 0 then\n"
"\t\t\tseen[v] = true\n"
"\t\t\tlocal parts = {}\n"
"\t\t\tfor k, val in pairs(v) do\n"
"\t\t\t\tif #parts >= 200 or used > limit then parts[#parts + 1] = '...' ; break end\n"
"\t\t\t\tlocal key = (type(k) == 'string' and smatch(k, '^[%a_][%w_]*$')) and k or ('[' .. render(k, 0, seen) .. ']')\n"
"\t\t\t\tused = used + #key + 5\n"
"\t\t\t\tparts[#parts + 1] = key .. ' = ' .. render(val, depth - 1, seen)\n"
"\t\t\tend\n"
"\t\t\tseen[v] = nil\n"
"\t\t\treturn '{ ' .. tconcat(parts, ', ') .. ' }'\n"
"\t\tend\n"
"\t\tlocal s\n"
"\t\tif t == 'string' then s = sformat('%q', v)\n"
"\t\telseif t == 'table' then s = seen[v] and '<cycle>' or '{...}'\n"
"\t\telse s = tostring(v) end\n"
"\t\tif type(s) ~= 'string' then s = '<' .. t .. '>' end\n"
"\t\tused = used + #s\n"
"\t\treturn s\n"
"\tend\n"
"\treturn render\n"
"end\n"
"\n"
"local function pack(...) return select('#', ...), { ... } end\n"
"\n"
"return function(code, stateName)\n"
"\tlocal base, envName\n"
"\tif stateName == nil or stateName == '' or stateName == 'Main' or stateName == 'Main State' then\n"
"\t\tbase, envName = resolveMain()\n"
"\telseif stateName == '_G' then\n"
"\t\tbase, envName = G, '_G'\n"
"\telse\n"
"\t\tbase, envName = findState(stateName), stateName\n"
"\t\tif not base then\n"
"\t\t\treturn 'ok=0\\tenv=none\\n--- error ---\\nno Lua state named ' .. stateName .. '\\nstates: ' .. tconcat(stateNames(), ', ') .. '\\n'\n"
"\t\tend\n"
"\tend\n"
"\tlocal chunk, err = compile(code, '=luaexec')\n"
"\tif not chunk then return 'ok=0\\tenv=' .. envName .. '\\n--- error ---\\n' .. tostring(err) .. '\\n' end\n"
"\n"
"\tlocal printed, printedBytes, printTruncated = {}, 0, false\n"
"\tlocal env = setmetatable({}, { __index = base, __newindex = base })\n"
"\trawset(env, 'print', function(...)\n"
"\t\tif printedBytes > PRINT_LIMIT then printTruncated = true ; return end\n"
"\t\tlocal p = {}\n"
"\t\tfor i = 1, select('#', ...) do\n"
"\t\t\tlocal ok, text = pcall(tostring, (select(i, ...)))\n"
"\t\t\tp[i] = (ok and type(text) == 'string') and text or '<unprintable>'\n"
"\t\tend\n"
"\t\tlocal line = tconcat(p, '\\t')\n"
"\t\tprintedBytes = printedBytes + #line + 1\n"
"\t\tprinted[#printed + 1] = line\n"
"\tend)\n"
"\trawset(env, 'vp', persist)\n"
"\trawset(env, 'States', stateNames)\n"
"\trawset(env, 'G', G)\n"
"\tsetfenv(chunk, env)\n"
"\n"
"\t-- 0498: the C __index of UI objects finds their methods through the calling thread's globals, so the thread's\n"
"\t-- globals are the target environment while the chunk runs, seen through a table that adds the current\n"
"\t-- ExposedMembers and ExposedTypes when the target's own are others (RunExternalLua puts T back in every case).\n"
"\tlocal globals = base\n"
"\tlocal pair = currentExposed()\n"
"\tif pair ~= nil then\n"
"\t\tlocal same = true\n"
"\t\tfor _, k in ipairs(EXPOSED) do\n"
"\t\t\tlocal okK, have = pcall(function() return base[k] end)\n"
"\t\t\tif not okK or have ~= pair[k] then same = false end\n"
"\t\tend\n"
"\t\tif not same then\n"
"\t\t\tglobals = setmetatable(pair, { __index = base, __newindex = base })\n"
"\t\tend\n"
"\tend\n"
"\tsetfenv(0, globals)\n"
"\tlocal n, r = pack(xpcall(chunk, function(e)\n"
"\t\tlocal ok, text = pcall(tostring, e)\n"
"\t\ttext = (ok and type(text) == 'string') and text or '<unprintable error>'\n"
"\t\treturn traceback and traceback(text, 2) or text\n"
"\tend))\n"
"\tsetfenv(0, T)\n"
"\n"
"\tlocal out = {}\n"
"\tif r[1] then\n"
"\t\tout[1] = 'ok=1\\tnret=' .. (n - 1) .. '\\tenv=' .. envName\n"
"\t\tlocal render = newRenderer(RESULT_LIMIT)\n"
"\t\tfor i = 2, n do\n"
"\t\t\tlocal ok, text = pcall(render, r[i], 4, {})\n"
"\t\t\tif not (ok and type(text) == 'string') then\n"
"\t\t\t\ttext = '<render error: ' .. ((type(text) == 'string') and text or '?') .. '>'\n"
"\t\t\tend\n"
"\t\t\tout[#out + 1] = '[' .. (i - 1) .. '] ' .. text\n"
"\t\tend\n"
"\telse\n"
"\t\tout[1] = 'ok=0\\tenv=' .. envName\n"
"\t\tout[2] = '--- error ---'\n"
"\t\tout[3] = (type(r[2]) == 'string') and r[2] or '<unprintable error>'\n"
"\tend\n"
"\tif #printed > 0 or printTruncated then\n"
"\t\tout[#out + 1] = '--- print ---'\n"
"\t\tfor _, line in ipairs(printed) do out[#out + 1] = line end\n"
"\t\tif printTruncated then out[#out + 1] = '<print output truncated at ' .. PRINT_LIMIT .. ' bytes>' end\n"
"\tend\n"
"\treturn tconcat(out, '\\n') .. '\\n'\n"
"end\n";

// The chunk that fills turn= (the DLL used GC.getGame().getGameTurn(); the proxy has no CvGame, so it asks the game
// through the same runner and the "Main" environment).
const char* const LUAEXEC_TURN_CODE = "return Game.GetGameTurn()";

struct ExternalLuaRequest
{
	const char* pCode;
	size_t uiCodeBytes;
	const char* szState;
	std::string strResult;
	bool bRan;
	std::string strTurnResult;  // the runner's text for LUAEXEC_TURN_CODE ("" when it did not run)
	DWORD dwRequestMs;          // the request's own time, measured before the turn query
	DWORD dwStart;
};

//! Ported from the fork's RunExternalLua @ 440878983. Runs inside ICvEngineScriptSystem1::CallCFunction, which holds
//! the engine's Lua lock: the UI runs Lua on the main thread while the proxied Update runs on the game-core thread,
//! so calling lua_pcall directly would race it. Addition: after the request, the same runner function answers
//! LUAEXEC_TURN_CODE for the result's turn= field.
// 0498: the registry key that keeps this thread's own environment (its globals when the proxy created it). The runner
// sets the thread's globals to a chunk's target environment for the chunk's duration; this key puts them back.
const char* const LUAEXEC_HOME_KEY = "VP_LUAEXEC_HOME";

//! 0498: put the thread's own environment back as its globals (the first call records it). Called before and after
//! every run, so a run that ended without the runner's own reset (an error outside xpcall) cannot leave a UI state's
//! environment as this thread's globals for the next request.
void RestoreHomeGlobals(lua_State* L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, LUAEXEC_HOME_KEY);
	if (lua_istable(L, -1))
	{
		lua_replace(L, LUA_GLOBALSINDEX);
	}
	else
	{
		lua_pop(L, 1);
		lua_pushvalue(L, LUA_GLOBALSINDEX);
		lua_setfield(L, LUA_REGISTRYINDEX, LUAEXEC_HOME_KEY);
	}
}

//! 0591: the runner's compiler, compile(code [, chunkname]) -> function | nil, message, as Lua 5.1's loadstring
//! answers. Build 26577625 sets _G.loadstring to nil; luaL_loadbuffer is the C API, which that build left alone. The
//! new function's environment is the calling thread's globals (lua_load), as with loadstring; the runner then gives
//! it its own with setfenv. Only lua_* calls and luaL_loadbuffer, which the proxy already imports from lua51_Win32.dll.
int LuaExecCompile(lua_State* L)
{
	if (lua_type(L, 1) != LUA_TSTRING)
	{
		lua_pushnil(L);
		lua_pushstring(L, "VP_LUAEXEC compile: the chunk is not a string");
		return 2;
	}
	size_t uiBytes = 0;
	const char* pCode = lua_tolstring(L, 1, &uiBytes);
	const char* szName = (lua_type(L, 2) == LUA_TSTRING) ? lua_tolstring(L, 2, NULL) : "=luaexec";
	if (luaL_loadbuffer(L, pCode, uiBytes, szName) != 0)
	{
		lua_pushnil(L);
		lua_insert(L, -2);  // nil, message
		return 2;
	}
	return 1;
}

int RunExternalLua(lua_State* L)
{
	ExternalLuaRequest* pRequest = static_cast<ExternalLuaRequest*>(lua_touserdata(L, 1));
	const int iTop = lua_gettop(L);
	RestoreHomeGlobals(L);

	if (luaL_loadbuffer(L, LUAEXEC_RUNNER, strlen(LUAEXEC_RUNNER), "=VP_LUAEXEC") == 0)
	{
		lua_pushvalue(L, LUA_REGISTRYINDEX);
		lua_pushvalue(L, LUA_GLOBALSINDEX);
		lua_pushcfunction(L, LuaExecCompile);  // 0591: the compiler, so the runner never needs _G.loadstring
		if (lua_pcall(L, 3, 1, 0) == 0 && lua_isfunction(L, -1))
		{
			const int iRunner = lua_gettop(L);
			lua_pushvalue(L, iRunner);
			lua_pushlstring(L, pRequest->pCode, pRequest->uiCodeBytes);
			lua_pushstring(L, pRequest->szState);
			if (lua_pcall(L, 2, 1, 0) == 0 && lua_type(L, -1) == LUA_TSTRING)
			{
				size_t uiLength = 0;
				const char* szResult = lua_tolstring(L, -1, &uiLength);
				pRequest->strResult.assign(szResult, uiLength);
				pRequest->bRan = true;
				lua_pop(L, 1);
			}
			pRequest->dwRequestMs = GetTickCount() - pRequest->dwStart;

			if (pRequest->bRan)
			{
				lua_pushvalue(L, iRunner);
				lua_pushstring(L, LUAEXEC_TURN_CODE);
				lua_pushstring(L, "");
				if (lua_pcall(L, 2, 1, 0) == 0 && lua_type(L, -1) == LUA_TSTRING)
					pRequest->strTurnResult = lua_tostring(L, -1);
			}
		}
	}

	if (!pRequest->bRan)
	{
		pRequest->dwRequestMs = GetTickCount() - pRequest->dwStart;
		pRequest->strResult = "ok=0\n--- error ---\nVP_LUAEXEC runner failed: ";
		pRequest->strResult += (lua_type(L, -1) == LUA_TSTRING) ? lua_tostring(L, -1) : "no message";
		pRequest->strResult += "\n";
	}

	lua_settop(L, iTop);
	RestoreHomeGlobals(L);
	return 0;
}

//! The turn from the runner's answer to LUAEXEC_TURN_CODE: "ok=1\tnret=1\tenv=..\n[1] <n>\n"; -1 otherwise.
int TurnFromRunner(const std::string& strText)
{
	if (strText.compare(0, 5, "ok=1\t") != 0)
		return -1;
	const size_t uiAt = strText.find("\n[1] ");
	if (uiAt == std::string::npos)
		return -1;
	const char* sz = strText.c_str() + uiAt + 5;
	char* pEnd = NULL;
	const long lTurn = strtol(sz, &pEnd, 10);
	if (pEnd == sz || (*pEnd != '\n' && *pEnd != '\0'))
		return -1;
	return static_cast<int>(lTurn);
}

//! Ported from the fork's IsExternalLuaEnabled @ 440878983. Opt-in. VP_LUAEXEC=1 enables and VP_LUAEXEC=0 disables
//! for the life of the process; otherwise the channel stays off until luaexec.enabled exists in the cache folder,
//! checked every couple of seconds, so a session can be switched on without restarting the game.
bool IsExternalLuaEnabled(const wchar_t* wszFolder)
{
	static int s_iState = -1;   // -1 undecided, 0 off for good, 1 on
	static DWORD s_dwLastCheck = 0;
	if (s_iState >= 0)
		return s_iState == 1;

	if (s_dwLastCheck == 0)
	{
		char szValue[8];
		const DWORD dwLength = GetEnvironmentVariableA("VP_LUAEXEC", szValue, sizeof(szValue));
		if (dwLength > 0 && dwLength < sizeof(szValue))
		{
			s_iState = (szValue[0] == '0') ? 0 : 1;
			Log("Lua channel: VP_LUAEXEC=%c: %s for the life of the process", szValue[0], s_iState ? "on" : "off");
			return s_iState == 1;
		}
	}

	const DWORD dwNow = GetTickCount();
	if (s_dwLastCheck != 0 && dwNow - s_dwLastCheck < LUAEXEC_ENABLE_RECHECK_MS)
		return false;
	s_dwLastCheck = (dwNow == 0) ? 1 : dwNow;

	if (wszFolder[0] == L'\0')
		return false;  // no cache folder (yet): nothing to look in
	wchar_t wszEnable[MAX_PATH];
	_snwprintf_s(wszEnable, MAX_PATH, _TRUNCATE, L"%s%s", wszFolder, LUAEXEC_ENABLE_FILE);
	if (GetFileAttributesW(wszEnable) != INVALID_FILE_ATTRIBUTES)
	{
		s_iState = 1;
		Log("Lua channel: luaexec.enabled found in the cache folder: on");
	}
	return s_iState == 1;
}

#ifndef VP_LUAEXEC_UNIT_TEST
// The persistent engine Lua thread (the DLL's MemoryDiagnostics::GetProbeLuaState). Written once on the main thread
// (OnGameDatabaseCached), read on the core thread (Poll): a volatile pointer is enough (x86, aligned).
lua_State* volatile g_pkThread = NULL;
volatile LONG g_lThreadTried = 0;
// Ticket 0805: the script system the thread was made with, kept for OnShutdown (SetDLLIFace(NULL) clears the proxy's
// engine utility, and with it EngineScriptSystem(), before Shutdown runs). The EXE's object, alive while the EXE runs.
ICvEngineScriptSystem1* volatile g_pkThreadScriptSystem = NULL;
volatile LONG g_lShutdownDone = 0;

// Cache folder, UTF-8 from the engine converted once (it does not change during a session). Empty until the engine
// utility is known.
wchar_t g_wszFolder[MAX_PATH] = {0};
bool g_bHaveFolder = false;

bool HaveFolder()
{
	if (g_bHaveFolder)
		return true;
	ICvEngineUtility1* pkEngine = VpProxy_EngineUtility();
	if (pkEngine == NULL)
	{
		static bool s_bSaid = false;
		if (!s_bSaid)
		{
			s_bSaid = true;
			Log("Lua channel: no engine utility (VpProxy_EngineUtility() is NULL: before SetDLLIFace, or the runtime "
			    "does not provide the accessor yet); the channel cannot find the cache folder and answers nothing");
		}
		return false;
	}
	g_bHaveFolder = true;
	const char* szFolder = pkEngine->GetCacheFolderPath();
	if (szFolder == NULL || MultiByteToWideChar(CP_UTF8, 0, szFolder, -1, g_wszFolder, MAX_PATH) == 0)
		g_wszFolder[0] = L'\0';
	Log("Lua channel: cache folder '%s'", szFolder ? szFolder : "(NULL)");
	return true;
}
#endif  // VP_LUAEXEC_UNIT_TEST
}  // namespace

// ============================================================================================ ticket 0805: inert values
// Raw accesses only (no metamethod runs, no Lua error): the registry, the metatables and the function tables are plain
// tables the proxy made itself. Fields of a table are cleared during its own lua_next traversal, which Lua 5.1 allows
// ("you may clear existing fields", lua_next in the reference manual).
void ProxyLuaExec::InertImageValues(lua_State* L, void* const* metaKeys, int nMeta, void* typesKey, void* const* dropKeys,
                                    int nDrop, InertCounts* counts)
{
	static const char* const kFields[] = {"__gc", "__index", "__call", "__newindex", "__tostring", "__eq", "__len"};
	InertCounts c = {0, 0, 0, 0};
	const int top = lua_gettop(L);
	for (int i = 0; i < nMeta; ++i)
	{
		lua_pushlightuserdata(L, metaKeys[i]);
		lua_rawget(L, LUA_REGISTRYINDEX);
		if (lua_istable(L, -1))
		{
			const int mt = lua_gettop(L);
			for (size_t f = 0; f < sizeof(kFields) / sizeof(kFields[0]); ++f)
			{
				lua_pushstring(L, kFields[f]);
				lua_rawget(L, mt);
				const bool had = !lua_isnil(L, -1);
				lua_pop(L, 1);
				if (!had) continue;
				lua_pushstring(L, kFields[f]);
				lua_pushnil(L);
				lua_rawset(L, mt);
				++c.metaFields;
			}
		}
		lua_settop(L, top);
	}
	if (typesKey)
	{
		lua_pushlightuserdata(L, typesKey);
		lua_rawget(L, LUA_REGISTRYINDEX);
		if (lua_istable(L, -1))
		{
			const int types = lua_gettop(L);
			lua_pushnil(L);
			while (lua_next(L, types) != 0)
			{
				// key at -2, the type's metatable at -1
				if (lua_istable(L, -1))
				{
					const int mt = lua_gettop(L);
					lua_pushstring(L, "__index");
					lua_rawget(L, mt);
					if (lua_istable(L, -1))
					{
						const int ft = lua_gettop(L);
						lua_pushnil(L);
						while (lua_next(L, ft) != 0)
						{
							lua_pop(L, 1);         // the function
							lua_pushvalue(L, -1);  // the key, kept for lua_next
							lua_pushnil(L);
							lua_rawset(L, ft);
							++c.typeFunctions;
						}
						++c.typeTables;
					}
					lua_pop(L, 1);  // __index (table or not)
					lua_pushstring(L, "__index");
					lua_pushnil(L);
					lua_rawset(L, mt);
				}
				lua_pop(L, 1);  // the metatable; the key stays for lua_next
			}
		}
		lua_settop(L, top);
	}
	for (int pass = 0; pass < 3; ++pass)
	{
		void* const* keys = pass == 0 ? dropKeys : pass == 1 ? metaKeys : &typesKey;
		const int n = pass == 0 ? nDrop : pass == 1 ? nMeta : (typesKey ? 1 : 0);
		for (int i = 0; i < n; ++i)
		{
			lua_pushlightuserdata(L, keys[i]);
			lua_rawget(L, LUA_REGISTRYINDEX);
			const bool had = !lua_isnil(L, -1);
			lua_pop(L, 1);
			if (!had) continue;
			lua_pushlightuserdata(L, keys[i]);
			lua_pushnil(L);
			lua_rawset(L, LUA_REGISTRYINDEX);
			++c.registryKeys;
		}
	}
	lua_settop(L, top);
	if (counts) *counts = c;
}

#ifndef VP_LUAEXEC_UNIT_TEST  // the engine parts: thread creation, test chunk, Poll (to the end of the file)

// ============================================================================================ the persistent thread
// The DLL creates its thread in MemoryDiagnostics::GetProbeLuaState(), first called from InstallLuaAllocHook() in
// LuaSupport::InitLuaFramework(), which CvDllDatabaseUtility::CacheGameDatabaseData() calls at its end: the engine
// calls CacheGameDatabaseData on its MAIN thread at database load (engine-contract: boot, ui thread). NEVER create
// or free a Lua thread at run time from the core thread: the first version of the DLL's channel did so per request
// and killed the game on its second request ("invalid key to 'next'", dump CvMiniDump_20260917_002305), because
// CreateLuaThread/FreeLuaThread change the engine's thread table outside the Lua lock while the main thread walks it.
void ProxyLuaExec::OnGameDatabaseCached()
{
	if (InterlockedCompareExchange(&g_lThreadTried, 1, 0) != 0)
		return;  // once per load of the proxy, like the DLL's s_bTried
	// The effective channel names are resolved and logged here, early, as the DLL does from DllGetGameContext
	// (CvGameCoreDLL.cpp, InstanceNames::Get()); this also keeps the name format strings in the binary whether or
	// not Poll is linked in, so tools/instances/provision.py classifies the proxy as "instance-aware".
	InstanceNames::Get();
	ICvEngineScriptSystem1* pkScriptSystem = EngineScriptSystem();
	if (pkScriptSystem == NULL)
	{
		Log("Lua channel: no engine script system at CacheGameDatabaseData; no persistent thread (requests will be "
		    "answered with an error)");
		return;
	}
	lua_State* L = pkScriptSystem->CreateLuaThread(LUAEXEC_THREAD_NAME);
	g_pkThreadScriptSystem = pkScriptSystem;  // ticket 0805: OnShutdown frees the thread with it
	g_pkThread = L;
	Log("Lua channel: persistent engine Lua thread %s = %p, created on thread %u (CacheGameDatabaseData)",
	    LUAEXEC_THREAD_NAME, L, (unsigned)GetCurrentThreadId());
}

// Ticket 0805 (proxy_luaexec.h, proxy-design 38): from Shutdown, on the thread that called SetDLLIFace(NULL).
void ProxyLuaExec::OnShutdown(bool mainThread)
{
	if (InterlockedCompareExchange(&g_lShutdownDone, 1, 0) != 0) return;
	lua_State* L = g_pkThread;
	ICvEngineScriptSystem1* pkScriptSystem = g_pkThreadScriptSystem;
	if (L == NULL || pkScriptSystem == NULL)
	{
		Log("shutdown: Lua: no persistent engine Lua thread of this image (none was made): nothing of this image is left "
		    "in the engine's Lua VM to make inert (ticket 0805)");
		return;
	}
	if (!mainThread)
	{
		LogErrPublic("shutdown: Lua: SetDLLIFace(NULL) came on thread %u, not the engine's main thread: the persistent thread "
		       "%s = %p and this image's Lua values are LEFT in the engine's VM (the thread table may change only on the "
		       "main thread; ticket 0805)", (unsigned)GetCurrentThreadId(), LUAEXEC_THREAD_NAME, L);
		return;
	}
	g_pkThread = NULL;  // the channel's Poll (game-core thread) answers "no persistent thread" from here
	ProxyLuaExec::InertCounts c = {-1, -1, -1, -1};
	const double t0 = Now();
	const bool ran = pkScriptSystem->CallCFunction(L, ProxyLua_InertImage, &c);
	pkScriptSystem->FreeLuaThread(L);
	Log("shutdown: Lua: this image's values made inert on the persistent thread (%s): %d metamethods removed from the "
	    "server-value metatables, %d master-form function tables emptied (%d functions), %d registry entries removed; "
	    "the persistent thread %s = %p freed (%.1f ms; ticket 0805)",
	    ran ? "ran" : "CallCFunction answered false", c.metaFields, c.typeTables, c.typeFunctions, c.registryKeys,
	    LUAEXEC_THREAD_NAME, L, (Now() - t0) * 1000.0);
}

// The engine's main thread calls CacheGameDatabaseData at database load (boot, and again, as a no-op, while a game
// is set up); the DLL's work is forwarded first, then the thread is made, once.
bool ProxyHook_ICvDLLDatabaseUtility1_CacheGameDatabaseData(Proxy_ICvDLLDatabaseUtility1& self)
{
	const bool bResult = ProxyFwd_ICvDLLDatabaseUtility1_CacheGameDatabaseData(self);
	ProxyLuaExec::OnGameDatabaseCached();
	return bResult;
}

// ============================================================================================ test chunk (2805)
bool ProxyLuaExec::RunTestChunk(const char* code, int rounds, int& result, std::string& err)
{
	result = -1;
	ICvEngineScriptSystem1* pkScriptSystem = EngineScriptSystem();
	if (pkScriptSystem == NULL || g_pkThread == NULL)
	{
		err = "no engine script system or no persistent Lua thread";
		return false;
	}
	char head[48];
	_snprintf_s(head, sizeof(head), _TRUNCATE, "local n = %d%s", rounds, "\n");
	const std::string full = std::string(head) + code;
	ExternalLuaRequest kRequest;
	kRequest.pCode = full.c_str();
	kRequest.uiCodeBytes = full.size();
	kRequest.szState = "";
	kRequest.bRan = false;
	kRequest.dwStart = GetTickCount();
	kRequest.dwRequestMs = 0;
	pkScriptSystem->CallCFunction(g_pkThread, RunExternalLua, &kRequest);
	if (!kRequest.bRan || kRequest.strResult.compare(0, 5, "ok=1\t") != 0)
	{
		err = kRequest.strResult.empty() ? std::string("the runner did not run") : kRequest.strResult;
		return false;
	}
	const size_t at = kRequest.strResult.find("\n[1] ");
	if (at != std::string::npos) result = atoi(kRequest.strResult.c_str() + at + 5);
	return true;
}

// ============================================================================================ Poll
// Ported from the fork's LuaSupport::PollExternalLuaRequest @ 440878983.
//
// The lock (why the DLL's Has/Release/GetGameCoreLock dance becomes VpProxy_CoreLockYield/Resume here): Poll runs on
// the game-core thread inside the proxied Update, before it is forwarded. At that moment the server thread that
// serves the core channel holds the DLL's game-core lock (a real recursive lock in the server, shared by all its
// service threads) through the CvDllGame wrapper the engine's GetGame() created, whose constructor takes it. The
// chunk runs inside CallCFunction, which first takes the ENGINE's Lua lock. If the main thread holds the engine's Lua
// lock while it waits, through the ui channel, for a server thread that waits for the game-core lock, and the core
// thread waits for the Lua lock: deadlock, exactly the DLL's case ("the main thread may hold the Lua lock while it
// waits for the game core lock"). So the core channel's server thread must let the lock go for the duration, as the
// DLL did on its own thread. The chunk's own calls into the DLL (Lua thunks on the core channel) run on that same
// server thread and take the lock again recursively for their own duration, as any DLL code does. The proxy's own
// state needs no lock here: Poll and the thunks it causes run on the core thread only, and proxy_lua.cpp guards its
// tables itself.
void ProxyLuaExec::Poll()
{
	if (!HaveFolder())
		return;
	if (!IsExternalLuaEnabled(g_wszFolder))
		return;

	static HANDLE s_hRequest = NULL;
	static HANDLE s_hDone = NULL;
	if (s_hRequest == NULL)
	{
		// Created rather than opened, so either side may start first.
		s_hRequest = CreateEventA(NULL, FALSE, FALSE, InstanceNames::Get().szLuaExecRequest);
		s_hDone = CreateEventA(NULL, FALSE, FALSE, InstanceNames::Get().szLuaExecDone);
		if (s_hRequest == NULL)
			return;
	}

	if (WaitForSingleObject(s_hRequest, 0) != WAIT_OBJECT_0)
		return;
	// Decisive if a client created the name first as a manual-reset event.
	ResetEvent(s_hRequest);

	wchar_t wszRequestPath[MAX_PATH];
	wchar_t wszResultPath[MAX_PATH];
	_snwprintf_s(wszRequestPath, MAX_PATH, _TRUNCATE, L"%s%s", g_wszFolder, LUAEXEC_REQUEST_FILE);
	_snwprintf_s(wszResultPath, MAX_PATH, _TRUNCATE, L"%s%s", g_wszFolder, LUAEXEC_RESULT_FILE);

	// Read the chunk. Delete-on-close removes exactly the file that was read, never a newer request written after
	// it. No file means a stray signal: answer nothing rather than overwrite a result.
	HANDLE hFile = CreateFileW(wszRequestPath, GENERIC_READ | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return;

	std::string strRequest;
	bool bHaveRequest = false;
	const DWORD dwSize = GetFileSize(hFile, NULL);
	if (dwSize != INVALID_FILE_SIZE && dwSize <= LUAEXEC_MAX_REQUEST_BYTES)
	{
		strRequest.resize(dwSize);
		DWORD dwRead = 0;
		bHaveRequest = (dwSize == 0) || (ReadFile(hFile, &strRequest[0], dwSize, &dwRead, NULL) && dwRead == dwSize);
	}
	CloseHandle(hFile);

	// Header lines are ordinary Lua comments, so they stay in the chunk and line numbers stay true.
	size_t uiBody = 0;
	if (strRequest.size() >= 3 && static_cast<unsigned char>(strRequest[0]) == 0xEF
		&& static_cast<unsigned char>(strRequest[1]) == 0xBB && static_cast<unsigned char>(strRequest[2]) == 0xBF)
		uiBody = 3;
	std::string strId;
	std::string strState;
	for (size_t uiLine = uiBody; uiLine < strRequest.size() && strRequest.compare(uiLine, 3, "--@") == 0; )
	{
		size_t uiEnd = strRequest.find('\n', uiLine);
		const size_t uiStop = (uiEnd == std::string::npos) ? strRequest.size() : uiEnd;
		std::string strHeader = strRequest.substr(uiLine + 3, uiStop - uiLine - 3);
		if (!strHeader.empty() && strHeader[strHeader.size() - 1] == '\r')
			strHeader.erase(strHeader.size() - 1);
		if (strHeader.compare(0, 3, "id=") == 0)
			strId = strHeader.substr(3);
		else if (strHeader.compare(0, 6, "state=") == 0)
			strState = strHeader.substr(6);
		if (uiEnd == std::string::npos)
			break;
		uiLine = uiEnd + 1;
	}

	ExternalLuaRequest kRequest;
	kRequest.pCode = strRequest.c_str() + uiBody;
	kRequest.uiCodeBytes = strRequest.size() - uiBody;
	kRequest.szState = strState.c_str();
	kRequest.bRan = false;
	kRequest.dwStart = GetTickCount();
	kRequest.dwRequestMs = 0;

	ICvEngineScriptSystem1* pkScriptSystem = EngineScriptSystem();
	if (!bHaveRequest)
	{
		kRequest.strResult = "ok=0\n--- error ---\nluaexec_request.lua could not be read (or is larger than 1 MB)\n";
	}
	else if (pkScriptSystem == NULL)
	{
		kRequest.strResult = "ok=0\n--- error ---\nno script system\n";
	}
	else
	{
		// The lock dance (see above): the server thread of the core channel lets the game-core lock go while the
		// engine's Lua lock is taken.
		const int iYielded = VpProxy_CoreLockYield();
		if (iYielded < 0)
		{
			static bool s_bSaid = false;
			if (!s_bSaid)
			{
				s_bSaid = true;
				Log("Lua channel: WARNING: the runtime cannot yield the server's game-core lock "
				    "(VpProxy_CoreLockYield() = -1); a chunk runs with it held, which can deadlock the real EXE if the "
				    "main thread waits for that lock while it holds the engine's Lua lock");
			}
		}

		// NEVER CreateLuaThread/FreeLuaThread here (see OnGameDatabaseCached).
		lua_State* L = g_pkThread;
		if (L != NULL)
		{
			const bool bCalled = pkScriptSystem->CallCFunction(L, RunExternalLua, &kRequest);
			if (!bCalled && kRequest.strResult.empty())
				kRequest.strResult = "ok=0\n--- error ---\nCallCFunction failed before the runner produced a result\n";
		}
		else
		{
			kRequest.strResult = "ok=0\n--- error ---\nno persistent Lua thread (the proxy's VP_LUAEXEC thread) to run on\n";
		}

		if (iYielded > 0)
			VpProxy_CoreLockResume(iYielded);
	}
	if (kRequest.strResult.empty())
		kRequest.strResult = "ok=0\n--- error ---\nno result\n";
	if (!kRequest.bRan && kRequest.dwRequestMs == 0)
		kRequest.dwRequestMs = GetTickCount() - kRequest.dwStart;

	char szNumbers[64];
	_snprintf_s(szNumbers, sizeof(szNumbers), _TRUNCATE, "\tms=%lu\tturn=%d\n",
		static_cast<unsigned long>(kRequest.dwRequestMs), TurnFromRunner(kRequest.strTurnResult));
	std::string strOut = "id=" + strId + "\tstate=" + (strState.empty() ? std::string("Main") : strState) + szNumbers;
	strOut += kRequest.strResult;

	// Written in full before done is signalled; the client re-reads until the id and status line appear.
	HANDLE hResult = CreateFileW(wszResultPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE,
		NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hResult != INVALID_HANDLE_VALUE)
	{
		DWORD dwWritten = 0;
		WriteFile(hResult, strOut.data(), static_cast<DWORD>(strOut.size()), &dwWritten, NULL);
		CloseHandle(hResult);
	}

	if (s_hDone != NULL)
		SetEvent(s_hDone);
}
#endif  // VP_LUAEXEC_UNIT_TEST
