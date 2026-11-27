// luaexec_test.cpp - unit test of the Lua channel's runner in proxy/src/proxy_luaexec.cpp (ticket 0591, epic 0590)
// against stock Lua 5.1.4 built from source, in a state set up as Firaxis' build 26577625 sets up the engine's VM:
// the standard libraries, then _G.loadstring = nil, _G.io = nil and os.execute/exit/getenv/remove/rename/setlocale/
// sleep = nil (research/02-exe-internals/new-exe-build-26577625.md, "The Lua sandbox"). A metatable on _G counts every
// read of the name "loadstring" that misses _G, so a runner that still reads _G.loadstring is seen even though the
// value is nil. The file under test is compiled into this one with VP_LUAEXEC_UNIT_TEST, which leaves out the engine
// parts (thread creation, Poll); RunExternalLua, LuaExecCompile and the runner text are the product's.
// Built and run by test_luaexec.py (the proxy's toolchain, and MSVC ASan). Prints "luaexec_test: N/M checks passed";
// exit 0 only when all pass.
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>

// The file under test logs through ProxyRtInt::Log in the proxy; here through this.
static bool g_verbose = false;
static void Log(const char* szFormat, ...)
{
	if (!g_verbose)
		return;
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
#include "lualib.h"
}

namespace
{
int g_checks = 0, g_failed = 0;

void Check(const char* name, bool ok, const std::string& detail = std::string())
{
	++g_checks;
	if (!ok) ++g_failed;
	if (!ok || g_verbose)
		printf("%s %s%s%s\n", ok ? "ok  " : "FAIL", name, detail.empty() ? "" : "  -- ", detail.c_str());
}

bool Has(const std::string& s, const char* needle) { return s.find(needle) != std::string::npos; }
bool Starts(const std::string& s, const char* prefix) { return s.compare(0, strlen(prefix), prefix) == 0; }

// Runs Lua set-up text with the C API (the state has no loadstring). Returns "" or the error.
std::string Exec(lua_State* L, const char* code)
{
	if (luaL_loadbuffer(L, code, strlen(code), "=setup") != 0 || lua_pcall(L, 0, 0, 0) != 0)
	{
		std::string err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "?";
		lua_pop(L, 1);
		return err;
	}
	return std::string();
}

// The sandbox of build 26577625, a trap that counts reads of _G.loadstring, and a Game for the turn query.
const char* const SANDBOX =
	"loadstring = nil\n"
	"io = nil\n"
	"if type(os) == 'table' then\n"
	"  for _, k in ipairs({ 'execute', 'exit', 'getenv', 'remove', 'rename', 'setlocale', 'sleep' }) do os[k] = nil end\n"
	"end\n"
	"VP_TEST_LOADSTRING_READS = 0\n"
	"setmetatable(_G, { __index = function(t, k)\n"
	"  if k == 'loadstring' then rawset(t, 'VP_TEST_LOADSTRING_READS', rawget(t, 'VP_TEST_LOADSTRING_READS') + 1) end\n"
	"  return nil\n"
	"end })\n"
	"Game = { GetGameTurn = function() return 7 end }\n";

lua_State* NewSandbox(bool bNoLoad)
{
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);
	std::string err = Exec(L, SANDBOX);
	if (!err.empty())
		printf("FAIL sandbox set-up: %s\n", err.c_str());
	if (bNoLoad)
		Exec(L, "load = nil");
	return L;
}

int LoadstringReads(lua_State* L)
{
	lua_pushstring(L, "VP_TEST_LOADSTRING_READS");
	lua_rawget(L, LUA_GLOBALSINDEX);
	const int n = lua_isnumber(L, -1) ? static_cast<int>(lua_tonumber(L, -1)) : -1;
	lua_pop(L, 1);
	return n;
}

struct Outcome
{
	std::string strResult;
	std::string strTurn;
	bool bRan;
	int iCpcall;
};

// One request as Poll makes it: RunExternalLua on the state, through lua_cpcall (the engine's CallCFunction is a
// protected call with the request as light userdata).
Outcome Run(lua_State* L, const char* code, const char* state, size_t uiBytes = (size_t)-1)
{
	ExternalLuaRequest kRequest;
	kRequest.pCode = code;
	kRequest.uiCodeBytes = (uiBytes == (size_t)-1) ? strlen(code) : uiBytes;
	kRequest.szState = state;
	kRequest.bRan = false;
	kRequest.dwStart = GetTickCount();
	kRequest.dwRequestMs = 0;
	Outcome o;
	o.iCpcall = lua_cpcall(L, RunExternalLua, &kRequest);
	o.strResult = kRequest.strResult;
	o.strTurn = kRequest.strTurnResult;
	o.bRan = kRequest.bRan;
	return o;
}

// The runner called directly, with an explicit runner text and with or without the C compiler (the fallback path:
// a caller that passes no compiler, as the fork's CvLuaSupport.cpp does if it takes the runner text alone).
struct DirectCall
{
	const char* szRunner;
	bool bWithCompiler;
	const char* szCode;
	std::string strOut;
	std::string strError;
};

int CallRunnerDirect(lua_State* L)
{
	DirectCall* p = static_cast<DirectCall*>(lua_touserdata(L, 1));
	if (luaL_loadbuffer(L, p->szRunner, strlen(p->szRunner), "=VP_LUAEXEC") != 0)
	{
		p->strError = lua_tostring(L, -1);
		return 0;
	}
	lua_pushvalue(L, LUA_REGISTRYINDEX);
	lua_pushvalue(L, LUA_GLOBALSINDEX);
	int nArgs = 2;
	if (p->bWithCompiler)
	{
		lua_pushcfunction(L, LuaExecCompile);
		nArgs = 3;
	}
	if (lua_pcall(L, nArgs, 1, 0) != 0 || !lua_isfunction(L, -1))
	{
		p->strError = lua_isstring(L, -1) ? lua_tostring(L, -1) : "runner set-up returned no function";
		return 0;
	}
	lua_pushstring(L, p->szCode);
	lua_pushstring(L, "");
	if (lua_pcall(L, 2, 1, 0) != 0 || lua_type(L, -1) != LUA_TSTRING)
	{
		p->strError = lua_isstring(L, -1) ? lua_tostring(L, -1) : "runner returned no text";
		return 0;
	}
	p->strOut = lua_tostring(L, -1);
	return 0;
}

std::string Direct(lua_State* L, const char* runner, bool bWithCompiler, const char* code, std::string* pError = NULL)
{
	DirectCall k;
	k.szRunner = runner;
	k.bWithCompiler = bWithCompiler;
	k.szCode = code;
	lua_cpcall(L, CallRunnerDirect, &k);
	if (pError) *pError = k.strError;
	return k.strOut;
}

// LuaExecCompile called as Lua calls it, with an argument that is not a string.
int CallCompileWithNumber(lua_State* L)
{
	std::string* pOut = static_cast<std::string*>(lua_touserdata(L, 1));
	lua_pushcfunction(L, LuaExecCompile);
	lua_pushnumber(L, 42);
	lua_call(L, 1, 2);
	*pOut = std::string(lua_isnil(L, -2) ? "nil" : "not nil") + "|" + (lua_isstring(L, -1) ? lua_tostring(L, -1) : "?");
	return 0;
}
}  // namespace

int main(int argc, char** argv)
{
	for (int i = 1; i < argc; ++i)
		if (strcmp(argv[i], "-v") == 0) g_verbose = true;

	// --- the sandbox itself: loadstring is gone, load stays, the trap counts.
	{
		lua_State* L = NewSandbox(false);
		Check("sandbox: _G.loadstring is nil, load is a function, io is nil, os.execute is nil",
		      Exec(L, "assert(loadstring == nil) assert(type(load) == 'function') assert(io == nil) assert(os.execute == nil)").empty());
		const int before = LoadstringReads(L);
		Exec(L, "local x = loadstring");
		Check("sandbox: the trap counts a read of _G.loadstring", LoadstringReads(L) == before + 1);
		lua_close(L);
	}

	// --- the product's runner through RunExternalLua (the C compiler path).
	{
		lua_State* L = NewSandbox(false);
		Exec(L, "VP_TEST_LOADSTRING_READS = 0");

		Outcome o = Run(L, "return 1 + 2", "");
		Check("runner: 'return 1 + 2' answers ok=1 with [1] 3", o.iCpcall == 0 && o.bRan &&
		      Starts(o.strResult, "ok=1\tnret=1\tenv=thread\n") && Has(o.strResult, "\n[1] 3\n"), o.strResult);
		Check("runner: the turn query answers 7 (TurnFromRunner)", TurnFromRunner(o.strTurn) == 7, o.strTurn);

		o = Run(L, "return +", "");
		Check("runner (negative): a syntax error answers ok=0 with the chunk name luaexec:1:", o.bRan &&
		      Starts(o.strResult, "ok=0\tenv=thread\n--- error ---\n") && Has(o.strResult, "luaexec:1:"), o.strResult);

		o = Run(L, "error('boom-0591')", "");
		Check("runner (negative): a run-time error answers ok=0 with its message and a traceback", o.bRan &&
		      Starts(o.strResult, "ok=0\tenv=thread\n--- error ---\n") && Has(o.strResult, "boom-0591") &&
		      Has(o.strResult, "stack traceback"), o.strResult);

		o = Run(L, "print('a', 1) return true", "");
		Check("runner: print is captured", Has(o.strResult, "\n[1] true\n--- print ---\na\t1\n"), o.strResult);

		Run(L, "x_0591 = 5", "");
		o = Run(L, "return x_0591", "");
		Check("runner: a global set by one request is seen by the next", Has(o.strResult, "\n[1] 5\n"), o.strResult);

		o = Run(L, "return 1", "NoSuchState");
		Check("runner (negative): an unknown state answers env=none", Starts(o.strResult, "ok=0\tenv=none\n") &&
		      Has(o.strResult, "no Lua state named NoSuchState"), o.strResult);

		o = Run(L, "return 1 + 1 ))) not Lua after the declared length", "", 12);
		Check("runner: the chunk's declared byte count is honoured (luaL_loadbuffer, not a C string)",
		      Has(o.strResult, "\n[1] 2\n"), o.strResult);

		o = Run(L, "return loadstring", "");
		Check("runner: a chunk sees loadstring as nil in the sandbox", Has(o.strResult, "\n[1] nil\n"), o.strResult);
		const int iChunkReads = LoadstringReads(L);
		char szReads[32];
		_snprintf_s(szReads, sizeof(szReads), _TRUNCATE, "reads=%d", iChunkReads);
		Check("runner: the only reads of _G.loadstring are the chunk's own (1); the runner made none",
		      iChunkReads == 1, szReads);
		lua_close(L);
	}

	// --- the same with load also nil: the C compiler does not need it.
	{
		lua_State* L = NewSandbox(true);
		Outcome o = Run(L, "return 6 * 7", "");
		Check("runner: with load nil too, the C compiler still compiles ([1] 42)", Has(o.strResult, "\n[1] 42\n"),
		      o.strResult);
		Check("runner: no read of _G.loadstring with load nil", LoadstringReads(L) == 0);
		lua_close(L);
	}

	// --- the fallback: the runner called without the C compiler compiles with load(reader).
	{
		lua_State* L = NewSandbox(false);
		std::string err;
		std::string out = Direct(L, LUAEXEC_RUNNER, false, "return 'via-load'", &err);
		Check("runner fallback: without the C compiler, load(reader) compiles", Has(out, "\n[1] \"via-load\"\n"),
		      out + err);
		out = Direct(L, LUAEXEC_RUNNER, false, "return +", &err);
		Check("runner fallback (negative): a syntax error through load(reader) answers ok=0 with luaexec:1:",
		      Starts(out, "ok=0\tenv=thread\n--- error ---\n") && Has(out, "luaexec:1:"), out + err);
		Check("runner fallback: no read of _G.loadstring", LoadstringReads(L) == 0);
		lua_close(L);
	}
	{
		lua_State* L = NewSandbox(true);
		std::string err;
		std::string out = Direct(L, LUAEXEC_RUNNER, false, "return 1", &err);
		Check("runner fallback (negative): without the C compiler and with load nil, ok=0 names the missing compiler",
		      Starts(out, "ok=0\tenv=thread\n--- error ---\n") && Has(out, "no compiler"), out + err);
		lua_close(L);
	}

	// --- LuaExecCompile directly.
	{
		lua_State* L = NewSandbox(false);
		std::string s;
		lua_cpcall(L, CallCompileWithNumber, &s);
		Check("compile (negative): a non-string chunk answers nil and a message", s == "nil|VP_LUAEXEC compile: the chunk is not a string", s);
		lua_close(L);
	}

	// --- negative control: the old runner (G.loadstring) fails in this sandbox, and the trap sees its read. This shows
	// the checks above can fail.
	{
		std::string old = LUAEXEC_RUNNER;
		const std::string from = "compile(code, '=luaexec')";
		const size_t at = old.find(from);
		Check("control: the runner compiles with compile(code, '=luaexec')", at != std::string::npos);
		if (at != std::string::npos)
		{
			old.replace(at, from.size(), "G.loadstring(code, '=luaexec')");
			lua_State* L = NewSandbox(false);
			std::string err;
			std::string out = Direct(L, old.c_str(), true, "return 1", &err);
			Check("control (negative): a runner that calls G.loadstring fails in the sandbox",
			      !Has(out, "ok=1") && (Has(out + err, "attempt to call") || Has(out + err, "nil value")), out + err);
			Check("control: the trap saw that runner read _G.loadstring", LoadstringReads(L) >= 1);
			lua_close(L);
		}
		Check("control: the runner text has no 'loadstring' at all", std::string(LUAEXEC_RUNNER).find("loadstring") == std::string::npos);
	}

	printf("luaexec_test: %d/%d checks passed\n", g_checks - g_failed, g_checks);
	return g_failed == 0 ? 0 : 1;
}
