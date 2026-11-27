// ui_bridge_test.cpp - unit test of proxy/src/proxy_ui_bridge.cpp (issue 0178) against stock Lua 5.1.4 built from
// source, with a fake engine that behaves as the EXE was found to behave live (research/03-dll-engine-boundary/
// proxy-design.md 24.2, c5h-b 2026-11-14):
//   - every UI context is a Lua thread with its own environment, listed in _G.Threads (thread -> environment) with its
//     StateName; the EXE's API (Events, UnitMoving) is set into each environment, and the DLL's globals are COPIED into
//     it from the registration thread's environment when the context is made;
//   - every context has its OWN Events table; Events.X.Add(f) (a dot call) registers f for that context; Events.X(...)
//     raised through any context's table calls the listeners of all contexts; Events.X:Count() counts the context's own;
//   - a context that goes away takes its listeners with it.
// Built and run by test_ui_bridge.py (the proxy's toolchain, and MSVC ASan). Prints "ui_bridge_test: N/M checks
// passed"; exit 0 only when all pass.
#include "proxy_ui_bridge.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

namespace
{
int g_checks = 0, g_failed = 0;
bool g_verbose = false;

void Check(const char* name, bool ok, const std::string& detail = std::string())
{
	++g_checks;
	if (!ok) ++g_failed;
	if (!ok || g_verbose)
		printf("%s %s%s%s\n", ok ? "ok  " : "FAIL", name, detail.empty() ? "" : "  -- ", detail.c_str());
}

std::vector<UiBridge::Record> g_records;
std::vector<std::string> g_log;
uint32_t g_now = 1000;

void Post(const UiBridge::Record& r) { g_records.push_back(r); }
void LogLine(const char* s)
{
	g_log.push_back(s);
	if (g_verbose) printf("    log: %s\n", s);
}
uint32_t Now() { return g_now; }

bool LogHas(const char* needle)
{
	for (size_t i = 0; i < g_log.size(); ++i)
		if (g_log[i].find(needle) != std::string::npos) return true;
	return false;
}
size_t LogCount(const char* needle)
{
	size_t n = 0;
	for (size_t i = 0; i < g_log.size(); ++i)
		if (g_log[i].find(needle) != std::string::npos) ++n;
	return n;
}

// The fake engine, in the main state's _G (the EXE's own machinery; UI files never see _G directly).
const char* const kEngine =
	"Threads = {}\n"
	"local registry = {}          -- event name -> list of { ctx = id, f = f }\n"
	"local names = { SpawnArrowEvent = 1, RemoveAllArrowsEvent = 1, MinimapTextureBroadcastEvent = 1,\n"
	"                DisplayMovementIndicator = 1, LocalMachineAppUpdate = 1, SerialEventGameDataDirty = 1, Faulty = 1,\n"
	"                LocalMachineUnitPositionChanged = 1, UnitMoveQueueChanged = 1, UIPathFinderUpdate = 1 }\n"
	"function EngineDispatch(name, ...)\n"
	"  local list = registry[name] or {}\n"
	"  local copy = {} for i, l in ipairs(list) do copy[i] = l end\n"
	"  for _, l in ipairs(copy) do l.f(...) end\n"
	"end\n"
	"function EngineEvents(ctx)\n"
	"  return setmetatable({}, { __index = function(t, name)\n"
	"    if not names[name] then return nil end\n"
	"    local ev = {}\n"
	"    ev.Add = function(f) assert(type(f) == 'function', 'Add: not a function')\n"
	"      registry[name] = registry[name] or {} ; table.insert(registry[name], { ctx = ctx, f = f }) end\n"
	"    ev.Count = function(self) assert(self == ev, 'Not a valid instance') local n = 0\n"
	"      for _, l in ipairs(registry[name] or {}) do if l.ctx == ctx then n = n + 1 end end return n end\n"
	"    setmetatable(ev, { __call = function(self, ...) EngineDispatch(name, ...) end })\n"
	"    rawset(t, name, ev)\n"
	"    return ev\n"
	"  end })\n"
	"end\n"
	"function EnginePurge(ctx)\n"
	"  for name, list in pairs(registry) do\n"
	"    for i = #list, 1, -1 do if list[i].ctx == ctx then table.remove(list, i) end end\n"
	"  end\n"
	"end\n"
	"Faulty = 0\n";

const char* const kUnitMoving = "return function(p, u) return p == 1 and u == 2 end";

struct Engine
{
	lua_State* L;
	int nextCtx;
};

lua_State* g_reg = NULL;   // the registration thread of the current engine (its env holds the DLL's globals)

bool RunIn(lua_State* L, const char* code, std::string* err = NULL)
{
	const bool ok = luaL_loadstring(L, code) == 0 && lua_pcall(L, 0, 0, 0) == 0;
	if (!ok)
	{
		if (err) *err = lua_isstring(L, -1) ? lua_tostring(L, -1) : "?";
		if (g_verbose) printf("    lua error: %s\n", lua_isstring(L, -1) ? lua_tostring(L, -1) : "?");
		lua_pop(L, 1);
	}
	return ok;
}

// A new engine state; the registration thread gets the DLL's globals (here only what the bridge sets) and is anchored.
Engine NewEngine()
{
	Engine e;
	e.L = luaL_newstate();
	e.nextCtx = 1;
	luaL_openlibs(e.L);
	RunIn(e.L, kEngine);
	RunIn(e.L, "Faulty = nil");
	RunIn(e.L, "EngineEvents(0).Faulty.Add(function() error('a listener failed') end)");
	lua_State* R = lua_newthread(e.L);
	lua_setfield(e.L, LUA_REGISTRYINDEX, "registration");
	lua_newtable(R);
	lua_replace(R, LUA_GLOBALSINDEX);
	g_reg = R;
	return e;
}

// A UI context: thread + env (StateName, Events of its own, UnitMoving, the DLL's globals copied from the registration
// env, base functions through __index to _G), listed in _G.Threads; then the file's code runs in it.
lua_State* NewContext(Engine& e, const char* name, const char* code)
{
	const int id = e.nextCtx++;
	lua_State* T = lua_newthread(e.L);              // [thread]
	lua_newtable(e.L);                              // [thread, env]
	lua_pushstring(e.L, name);
	lua_setfield(e.L, -2, "StateName");
	lua_pushinteger(e.L, id);
	lua_setfield(e.L, -2, "ContextId");
	lua_getglobal(e.L, "EngineEvents");
	lua_pushinteger(e.L, id);
	lua_call(e.L, 1, 1);
	lua_setfield(e.L, -2, "Events");
	luaL_loadstring(e.L, kUnitMoving);
	lua_call(e.L, 0, 1);
	lua_setfield(e.L, -2, "UnitMoving");
	if (g_reg)                                       // the DLL's globals, copied
	{
		lua_pushnil(g_reg);
		while (lua_next(g_reg, LUA_GLOBALSINDEX) != 0)
		{
			lua_pushvalue(g_reg, -2);
			lua_insert(g_reg, -2);
			lua_xmove(g_reg, e.L, 2);
			lua_rawset(e.L, -3);
		}
	}
	lua_newtable(e.L);                               // env's metatable: base functions from _G
	lua_pushvalue(e.L, LUA_GLOBALSINDEX);
	lua_setfield(e.L, -2, "__index");
	lua_setmetatable(e.L, -2);
	lua_getglobal(e.L, "Threads");                   // [thread, env, Threads]
	lua_pushvalue(e.L, -3);
	lua_pushvalue(e.L, -3);
	lua_rawset(e.L, -3);                             // Threads[thread] = env
	lua_pop(e.L, 1);                                 // [thread, env]
	lua_xmove(e.L, T, 1);                            // env -> T
	lua_replace(T, LUA_GLOBALSINDEX);
	lua_pop(e.L, 1);                                 // the thread stays referenced by Threads
	if (code) RunIn(T, code);
	return T;
}

// The context goes away: out of Threads, its listeners purged (the EXE's behaviour as the live run showed it).
void DestroyContext(Engine& e, lua_State* T)
{
	lua_getglobal(T, "ContextId");
	const int id = (int)lua_tointeger(T, -1);
	lua_pop(T, 1);
	lua_getglobal(e.L, "EnginePurge");
	lua_pushinteger(e.L, id);
	lua_call(e.L, 1, 0);
	lua_getglobal(e.L, "Threads");
	lua_pushthread(T);
	lua_xmove(T, e.L, 1);
	lua_pushnil(e.L);
	lua_rawset(e.L, -3);
	lua_pop(e.L, 1);
	lua_gc(e.L, LUA_GCCOLLECT, 0);
}

// Only the listeners of a context are gone (its env stays): a UI reload of that context's file without a new env.
void PurgeListeners(Engine& e, lua_State* T)
{
	lua_getglobal(T, "ContextId");
	const int id = (int)lua_tointeger(T, -1);
	lua_pop(T, 1);
	lua_getglobal(e.L, "EnginePurge");
	lua_pushinteger(e.L, id);
	lua_call(e.L, 1, 0);
}

double Num(lua_State* T, const char* global)
{
	lua_getglobal(T, global);
	const double v = lua_tonumber(T, -1);
	lua_pop(T, 1);
	return v;
}

double Arg(lua_State* T, int i)
{
	lua_getglobal(T, "args");
	double v = -1;
	if (lua_istable(T, -1))
	{
		lua_rawgeti(T, -1, i);
		v = lua_isnumber(T, -1) ? lua_tonumber(T, -1) : -1;
		lua_pop(T, 1);
	}
	lua_pop(T, 1);
	return v;
}

// The EXE's per-frame event, raised by the engine itself.
void Frame(Engine& e) { RunIn(e.L, "EngineDispatch('LocalMachineAppUpdate', 0, 0.016)"); }

// A UI file's call of a DLL function: the proxy's FuncThunk calls OnLuaEntry with the calling thread.
bool LuaEntry(lua_State* T) { return UiBridge::OnLuaEntry(T); }

// Runs fn on another OS thread while this one waits.
struct Job { void (*fn)(void*); void* arg; };
DWORD WINAPI JobThread(LPVOID p)
{
	Job* j = static_cast<Job*>(p);
	j->fn(j->arg);
	return 0;
}
void OnOtherThread(void (*fn)(void*), void* arg)
{
	Job j = { fn, arg };
	HANDLE h = CreateThread(NULL, 0, JobThread, &j, 0, NULL);
	WaitForSingleObject(h, INFINITE);
	CloseHandle(h);
}
void QueueDirty(void*) { UiBridge::QueueRaise("SerialEventGameDataDirty", 0, NULL); }
void RunCode(void* p) { RunIn(static_cast<lua_State*>(p), "Events.SpawnArrowEvent(1, 2, 3, 4)"); }
void EntryOther(void* p) { UiBridge::OnLuaEntry(static_cast<lua_State*>(p)); }

std::string S(double v)
{
	char b[64];
	_snprintf_s(b, sizeof(b), _TRUNCATE, "%g", v);
	return b;
}

const char* const kMiniMap =
	"hits, args = 0, nil\n"
	"Events.MinimapTextureBroadcastEvent.Add(function(h, w, ht) hits = hits + 1 ; args = { h, w, ht } end)\n"
	"Events.SerialEventGameDataDirty.Add(function(...) hits = hits + 1 end)\n";
const char* const kPopup =
	"hits = 0\n"
	"Events.SerialEventGameDataDirty.Add(function(...) hits = hits + 1 end)\n";
const char* const kFlags =
	"local UM = UnitMoving\n"                        // EUI's UnitFlagManager: local UnitMoving = UnitMoving
	"function Moving(p, u) moving = UM(p, u) return moving end\n";
const char* const kInGame =
	"function Moving(p, u) moving = UnitMoving(p, u) return moving end\n";

// ---- issue 4754 helpers ----
int32_t FB(double v)          // the bits of (float)v
{
	const float f = (float)v;
	int32_t b;
	memcpy(&b, &f, sizeof(b));
	return b;
}
// The EXE raises an event with Lua arguments written as Lua source.
void Raise(Engine& e, const char* name, const char* args)
{
	char code[512];
	_snprintf_s(code, sizeof(code), _TRUNCATE, "EngineDispatch('%s', %s)", name, args);
	RunIn(e.L, code);
}
void RaisePos(Engine& e, const char* args) { Raise(e, "LocalMachineUnitPositionChanged", args); }
void RaiseQueue(Engine& e, const char* args) { Raise(e, "UnitMoveQueueChanged", args); }
size_t CountKind(int kind)
{
	size_t n = 0;
	for (size_t i = 0; i < g_records.size(); ++i)
		if (g_records[i].kind == kind) ++n;
	return n;
}
bool IsPos(const UiBridge::Record& r, int player, int unit, double x, double y, double z, bool arrival)
{
	return r.kind == UiBridge::kKindUnitPosition && r.version == 1 && r.a[0] == unit && r.a[1] == FB(x) &&
	       r.a[2] == FB(y) && r.a[3] == FB(z) &&
	       r.flags == (((uint32_t)player << UiBridge::kPosPlayerShift) | (arrival ? (uint32_t)UiBridge::kPosArrival : 0u));
}
bool IsMark(const UiBridge::Record& r, uint32_t frame, int count)
{
	return r.kind == UiBridge::kKindFrameMark && r.version == 1 && r.flags == 0 && r.a[0] == (int32_t)frame &&
	       r.a[1] == count && r.a[2] == 0 && r.a[3] == 0;
}
void RaisePosOther(void* p) { RaisePos(*static_cast<Engine*>(p), "1, 2, { x = 1, y = 2, z = 3, w = 0 }"); }

// ---- ticket 0300 part 2 helpers ----
// The fake tests hook: records the query; answers embark on node 2, attack on the last node, swap at the cursor.
UiBridge::PathQuery g_lastQuery;
int g_testCalls = 0;
void FakePathTests(const UiBridge::PathQuery& q, UiBridge::PathAnswer& a)
{
	++g_testCalls;
	g_lastQuery = q;
	a.tested = true;
	if (q.count > 2) a.embark[2] = 1;
	a.embark[0] = 1;   // entry 0 is never posted
	a.lastAttack = q.count >= 2;
	a.cursorSwap = q.count == 0;
}
void RaisePathOther(void* p) { Raise(*static_cast<Engine*>(p), "UIPathFinderUpdate", "{ { x = 1, y = 2, turn = 3 } }"); }
uint32_t PathBits(const UiBridge::Record& r) { return (uint32_t)r.a[2]; }
bool IsNode(const UiBridge::Record& r, int x, int y, int turn, int index, int count, uint32_t extra, int update)
{
	return r.kind == UiBridge::kKindPathNode && r.version == 1 && r.flags == 0 &&
	       r.a[0] == (int32_t)(((uint32_t)(uint16_t)x) | ((uint32_t)(uint16_t)y << 16)) && r.a[1] == turn &&
	       PathBits(r) == ((uint32_t)index | ((uint32_t)count << UiBridge::kPathCountShift) | extra) && r.a[3] == update;
}
const char* const kPathInGame =
	"function MakeUnit(owner, id) return { GetOwner = function(self) return owner end, GetID = function(self) return id end } end\n"
	"headUnit = MakeUnit(0, 1002)\n"
	"UI = { GetHeadSelectedUnit = function() return headUnit end, GetMouseOverHex = function() return 7, 9 end }\n"
	"vpHits = 0\n"
	// VP's WorldView: on every path update, DisplayMovementIndicator(true) (in move mode)
	"Events.UIPathFinderUpdate.Add(function(path) vpHits = vpHits + 1 ; Events.DisplayMovementIndicator(true) end)\n";
}  // namespace

int main(int argc, char** argv)
{
	for (int i = 1; i < argc; ++i)
		if (strcmp(argv[i], "-v") == 0) g_verbose = true;

	Check("record: 32 bytes", sizeof(UiBridge::Record) == 32);
	Check("record: field offsets 0/2/4/8/24/28", offsetof(UiBridge::Record, version) == 0 && offsetof(UiBridge::Record, kind) == 2 &&
	      offsetof(UiBridge::Record, flags) == 4 && offsetof(UiBridge::Record, a) == 8 && offsetof(UiBridge::Record, seq) == 24 &&
	      offsetof(UiBridge::Record, gen) == 28);

	UiBridge::Hooks hooks = { Post, LogLine, Now };
	UiBridge::SetHooks(hooks);

	// ---- off: nothing is installed, nothing queued ----
	{
		Engine e = NewEngine();
		UiBridge::Configure(false, false);
		UiBridge::SetMainThread(GetCurrentThreadId());
		Check("off: OnStateRegistered sets nothing", !UiBridge::OnStateRegistered(g_reg));
		lua_State* G = NewContext(e, "InGame", kInGame);
		g_now += 1000;
		Check("off: OnLuaEntry installs nothing", !LuaEntry(G) && UiBridge::GetStats().gen == 0);
		Check("off: QueueRaise refused", !UiBridge::QueueRaise("SerialEventGameDataDirty", 0, NULL));
		lua_close(e.L);
		g_reg = NULL;
	}

	UiBridge::ResetForTest();
	UiBridge::SetHooks(hooks);
	UiBridge::Configure(true, true);
	UiBridge::SetMainThread(GetCurrentThreadId());

	// ---- the registration thread: only the test table, copied into the contexts made later ----
	Engine e = NewEngine();
	Check("registration: the test table is set in the registration env", UiBridge::OnStateRegistered(g_reg) &&
	      LogHas("test table set in the registration thread"));
	Check("registration: nothing installed there", UiBridge::GetStats().gen == 0);

	// ---- front end / load screen: no InGame context, no install ----
	lua_State* LS = NewContext(e, "LoadScreen", NULL);
	g_now += 1000;
	Check("front end: no InGame context, no install", !LuaEntry(LS) && UiBridge::GetStats().gen == 0);

	// ---- the game's contexts appear; UnitFlagManager captures UnitMoving at load (before any install) ----
	lua_State* IG = NewContext(e, "InGame", kInGame);
	lua_State* MM = NewContext(e, "MiniMapPanel", kMiniMap);
	lua_State* UF = NewContext(e, "UnitFlagManager", kFlags);
	lua_State* PP = NewContext(e, "SomePopup", kPopup);
	{
		lua_getglobal(PP, "VP64UiBridgeTest");
		Check("contexts: each got a copy of the test table", lua_istable(PP, -1));
		lua_pop(PP, 1);
	}
	Check("rate limit: a call within 250 ms of the last check does nothing", !LuaEntry(PP) && UiBridge::GetStats().gen == 0);
	g_now += 300;
	Check("install: a Lua call from another context installs into InGame", LuaEntry(PP) && UiBridge::GetStats().gen == 1);
	UiBridge::Stats s = UiBridge::GetStats();
	Check("install: gen 1, one install, 5 listeners (4 events + the drain), UnitMoving replaced",
	      s.installs == 1 && s.listenersAdded == 5 && s.unitMovingReplaced == 1,
	      "installs " + S(s.installs) + " listeners " + S(s.listenersAdded));
	Check("install: logged with the InGame context and the reason", LogHas("gen 1 installed into the InGame context") &&
	      LogHas("a new environment") && LogHas("listeners 4 of 4") && LogHas("UnitMoving replaced"));
	RunIn(IG, "n1 = Events.SpawnArrowEvent:Count() ; n2 = Events.LocalMachineAppUpdate:Count()");
	RunIn(PP, "p1 = Events.SpawnArrowEvent:Count()");
	Check("install: the listeners belong to the InGame context (its Count 1, the popup's 0)", Num(IG, "n1") == 1 &&
	      Num(IG, "n2") == 1 && Num(PP, "p1") == 0);
	Check("install: the stacks are left as found", lua_gettop(PP) == 0 && lua_gettop(IG) == 0);
	g_now += 300;
	Check("install: the next check finds it installed (the environment's marker)", !LuaEntry(MM) &&
	      UiBridge::GetStats().gen == 1);
	for (int k = 0; k < 50; ++k) { g_now += 300; LuaEntry(MM); }    // 15 s of Lua calls, no frame yet: the load screen
	Check("install: a drain that never ran is not silent (no reinstall during the load, 15 s)", UiBridge::GetStats().gen == 1);

	// ---- listen: Lua raises from any context; the EXE raises ----
	g_records.clear();
	RunIn(MM, "Events.SpawnArrowEvent(10, 11, 12, 13)");
	Check("listen: SpawnArrowEvent raised by Lua in another context gives one record", g_records.size() == 1);
	if (g_records.size() == 1)
	{
		const UiBridge::Record& r = g_records[0];
		Check("listen: arrow record kind 1, version 1, args 10 11 12 13, no flags, gen 1",
		      r.kind == UiBridge::kKindSpawnArrow && r.version == 1 && r.a[0] == 10 && r.a[1] == 11 && r.a[2] == 12 &&
		      r.a[3] == 13 && r.flags == 0 && r.gen == 1);
	}
	RunIn(PP, "Events.RemoveAllArrowsEvent()");
	RunIn(e.L, "EngineDispatch('MinimapTextureBroadcastEvent', 1954, 320, 190)");   // the EXE raises it
	Check("listen: remove-arrows (Lua) and minimap (EXE) records follow in order", g_records.size() == 3 &&
	      g_records[1].kind == UiBridge::kKindRemoveAllArrows && g_records[1].seq == g_records[0].seq + 1 &&
	      g_records[2].kind == UiBridge::kKindMinimapTexture && g_records[2].a[0] == 1954 && g_records[2].a[1] == 320 &&
	      g_records[2].a[2] == 190);
	Check("listen: MiniMapPanel's own listener ran too", Num(MM, "hits") == 1);
	{
		g_records.clear();
		lua_pushlightuserdata(MM, (void*)0x1234);
		lua_setglobal(MM, "lud");
		RunIn(MM, "Events.SpawnArrowEvent(1.5, 'x', lud, true, 9)");
		const bool one = g_records.size() == 1;
		const UiBridge::Record r = one ? g_records[0] : UiBridge::Record();
		Check("listen: fraction, string, light userdata, boolean and a fifth argument are flagged",
		      one && r.a[0] == 1 && r.a[1] == 0 && r.a[2] == 0x1234 && r.a[3] == 1 &&
		      (r.flags & UiBridge::kFlagFraction) && (r.flags & (UiBridge::kFlagNotNumber0 << 1)) &&
		      (r.flags & (UiBridge::kFlagLightUserdata0 << 2)) && (r.flags & UiBridge::kFlagMoreArgs) &&
		      !(r.flags & (UiBridge::kFlagNotNumber0 << 3)));
		RunIn(IG, "Events.DisplayMovementIndicator(true)");
		Check("listen: DisplayMovementIndicator(true) gives kind 4, a[0] = 1", g_records.size() == 2 &&
		      g_records[1].kind == UiBridge::kKindDisplayMovement && g_records[1].a[0] == 1);
	}
	{
		g_records.clear();
		const uint32_t before = UiBridge::GetStats().wrongThread;
		OnOtherThread(RunCode, MM);
		Check("listen: a callback on another thread posts nothing and is counted", g_records.empty() &&
		      UiBridge::GetStats().wrongThread == before + 1);
		g_now += 300;
		OnOtherThread(EntryOther, PP);
		Check("install: OnLuaEntry on another thread (the Lua channel's) does nothing", UiBridge::GetStats().gen == 1);
	}

	// ---- replace: InGame's UnitMoving is the replacement; UnitFlagManager kept the original it captured ----
	{
		g_records.clear();
		const uint32_t before = UiBridge::GetStats().unitMovingCalls;
		RunIn(IG, "Moving(1, 2)");
		lua_getglobal(IG, "moving");
		const bool yes = lua_toboolean(IG, -1) != 0;
		lua_pop(IG, 1);
		RunIn(IG, "Moving(3, 4)");
		lua_getglobal(IG, "moving");
		const bool no = lua_isboolean(IG, -1) && !lua_toboolean(IG, -1);
		lua_pop(IG, 1);
		Check("replace: the original's answers come back (true for 1,2; false for 3,4)", yes && no);
		Check("replace: two calls counted, two test records (1 2 1, 3 4 0)", UiBridge::GetStats().unitMovingCalls == before + 2 &&
		      g_records.size() == 2 && g_records[0].kind == UiBridge::kKindUnitMovingCalled && g_records[0].a[2] == 1 &&
		      g_records[1].a[0] == 3 && g_records[1].a[2] == 0);
		RunIn(UF, "Moving(1, 2)");
		lua_getglobal(UF, "moving");
		const bool ufYes = lua_isboolean(UF, -1) && lua_toboolean(UF, -1);
		lua_pop(UF, 1);
		Check("replace: a context that captured UnitMoving at load still calls the original (answers, not counted)",
		      UiBridge::GetStats().unitMovingCalls == before + 2 && ufYes);
	}

	// ---- raise: queued from another thread, run on the main thread at the next frame ----
	{
		const double hm = Num(MM, "hits"), hp = Num(PP, "hits");
		OnOtherThread(QueueDirty, NULL);
		Check("raise: queued, nothing ran before the frame", Num(PP, "hits") == hp && UiBridge::GetStats().raisesQueued == 1);
		Frame(e);
		Check("raise: at the frame the listeners of the other contexts ran", Num(MM, "hits") == hm + 1 && Num(PP, "hits") == hp + 1);
		Check("raise: counted done, one drain", UiBridge::GetStats().raisesDone == 1 && UiBridge::GetStats().drains == 1);
		g_records.clear();
		UiBridge::Arg args[3] = { { UiBridge::Arg::kNumber, 9 }, { UiBridge::Arg::kNumber, 640 }, { UiBridge::Arg::kNumber, 380 } };
		UiBridge::QueueRaise("MinimapTextureBroadcastEvent", 3, args);
		Frame(e);
		Check("raise: MiniMapPanel's listener got the arguments (9, 640, 380)", Arg(MM, 1) == 9 && Arg(MM, 2) == 640 && Arg(MM, 3) == 380);
		Check("raise: the bridge's own listener mirrored the raise", g_records.size() == 1 &&
		      g_records[0].kind == UiBridge::kKindMinimapTexture && g_records[0].a[0] == 9);
		UiBridge::Arg b1 = { UiBridge::Arg::kBoolean, 1 };
		g_records.clear();
		UiBridge::QueueRaise("DisplayMovementIndicator", 1, &b1);
		Frame(e);
		Check("raise: a boolean argument arrives as a boolean", g_records.size() == 1 && g_records[0].a[0] == 1 && g_records[0].flags == 0);
	}
	{
		const uint32_t failed = UiBridge::GetStats().raisesFailed;
		UiBridge::QueueRaise("NoSuchEvent", 0, NULL);
		UiBridge::QueueRaise("Faulty", 0, NULL);
		UiBridge::QueueRaise("SerialEventGameDataDirty", 0, NULL);
		const double hp = Num(PP, "hits");
		Frame(e);
		Check("raise: a missing event and a failing listener: two failures, logged", UiBridge::GetStats().raisesFailed == failed + 2 &&
		      LogHas("raise Events.NoSuchEvent: no such event") && LogHas("raise Events.Faulty failed") && LogHas("a listener failed"));
		Check("raise: the next raise in the same frame still ran", Num(PP, "hits") == hp + 1);
		Check("raise: the engine's stack is left as found", lua_gettop(e.L) == 0 && lua_gettop(IG) == 0);
	}
	{
		const uint32_t dropped = UiBridge::GetStats().raisesDropped;
		int queued = 0;
		for (int i = 0; i < 65; ++i) queued += UiBridge::QueueRaise("SerialEventGameDataDirty", 0, NULL) ? 1 : 0;
		Check("raise: the queue holds 64, the 65th is dropped", queued == 64 && UiBridge::GetStats().raisesDropped == dropped + 1);
		const uint32_t done = UiBridge::GetStats().raisesDone;
		Frame(e);
		Check("raise: one frame drains all 64", UiBridge::GetStats().raisesDone == done + 64);
		char longName[80];
		memset(longName, 'x', sizeof(longName) - 1);
		longName[sizeof(longName) - 1] = 0;
		UiBridge::Arg nine[9];
		for (int i = 0; i < 9; ++i) { nine[i].type = UiBridge::Arg::kNumber; nine[i].value = i; }
		Check("raise: a 79-character name and 9 arguments are refused",
		      !UiBridge::QueueRaise(longName, 0, NULL) && !UiBridge::QueueRaise("SerialEventGameDataDirty", 9, nine));
		RunIn(PP, "VP64UiBridgeTest.QueueRaise('SerialEventGameDataDirty', 1, true) ; st = VP64UiBridgeTest.Stats()");
		lua_getglobal(PP, "st");
		lua_getfield(PP, -1, "raisesQueued");
		Check("raise: the test table (a context's copy) queues and reports", lua_tonumber(PP, -1) == UiBridge::GetStats().raisesQueued);
		lua_pop(PP, 2);
		Frame(e);
	}

	// ---- heartbeat on the core thread ----
	{
		for (int i = 0; i < UiBridge::kStallUpdates + 5; ++i) UiBridge::OnUpdate();
		Check("heartbeat: no drain for 600 Updates is logged once as a stall", UiBridge::GetStats().stalls == 1 && LogHas("STALL"));
		Frame(e);
		UiBridge::OnUpdate();
		Check("heartbeat: the drain running again is logged", LogHas("the drain runs again"));
	}

	// ---- B4: load from inside the game: every context goes, a new set comes ----
	{
		lua_State* oldIG = IG;
		DestroyContext(e, MM);
		DestroyContext(e, UF);
		DestroyContext(e, PP);
		DestroyContext(e, oldIG);
		IG = NewContext(e, "InGame", kInGame);
		MM = NewContext(e, "MiniMapPanel", kMiniMap);
		PP = NewContext(e, "SomePopup", kPopup);
		g_now += 300;
		Check("B4 load: a new InGame environment is installed into (gen 2)", LuaEntry(MM) && UiBridge::GetStats().gen == 2);
		g_records.clear();
		RunIn(PP, "Events.SpawnArrowEvent(5, 6, 7, 8)");
		Check("B4 load: one record, gen 2", g_records.size() == 1 && g_records[0].gen == 2 && g_records[0].a[0] == 5);
		const double hp = Num(PP, "hits");
		UiBridge::QueueRaise("SerialEventGameDataDirty", 0, NULL);
		Frame(e);
		Check("B4 load: raises run (the new drain)", Num(PP, "hits") == hp + 1 && UiBridge::GetStats().drains >= 1);
	}

	// ---- B4: the InGame listeners vanish while its environment stays (a UI reload): reinstalled after 2 s silent ----
	{
		PurgeListeners(e, IG);
		g_records.clear();
		RunIn(PP, "Events.SpawnArrowEvent(1, 1, 1, 1)");
		Check("B4 reload: with the listeners gone nothing is recorded", g_records.empty());
		for (int k = 0; k < 40; ++k) { g_now += 300; LuaEntry(PP); }    // 12 s of checks, no frame drained
		Check("B4 reload: after 10 s without a drain the same environment is installed into again (gen 3)",
		      UiBridge::GetStats().gen == 3 && LogHas("the drain was silent"));
		RunIn(PP, "Events.SpawnArrowEvent(2, 2, 2, 2)");
		Check("B4 reload: records again, gen 3", g_records.size() == 1 && g_records[0].gen == 3);
		lua_getglobal(IG, "UnitMoving");
		lua_getupvalue(IG, -1, 2);
		Check("B4 reload: UnitMoving wraps the engine's original, not the gen-2 wrapper", lua_isfunction(IG, -1) && !lua_iscfunction(IG, -1));
		lua_pop(IG, 2);
		const uint32_t calls = UiBridge::GetStats().unitMovingCalls;
		RunIn(IG, "Moving(1, 2)");
		Check("B4 reload: one UnitMoving call counts once", UiBridge::GetStats().unitMovingCalls == calls + 1);
	}
	{
		// frames keep coming: no reinstall
		const uint32_t gen = UiBridge::GetStats().gen;
		for (int k = 0; k < 10; ++k) { g_now += 300; Frame(e); LuaEntry(PP); }
		Check("steady: frames drained, 3 s of checks, no reinstall", UiBridge::GetStats().gen == gen);
		// issue 4754: ui_bridge=1 alone does not listen to the unit positions (its behaviour is unchanged)
		g_records.clear();
		RunIn(e.L, "EngineDispatch('LocalMachineUnitPositionChanged', 5, 17542, { x = 1.5, y = 2.5, z = 3.5, w = 0 })");
		RunIn(e.L, "EngineDispatch('UnitMoveQueueChanged', 5, 17542, false)");
		Frame(e);
		RunIn(IG, "c1 = Events.LocalMachineUnitPositionChanged:Count() ; c2 = Events.UnitMoveQueueChanged:Count()");
		Check("ui_bridge=1 alone: no position listener, no kind-6 or kind-7 record", g_records.empty() &&
		      Num(IG, "c1") == 0 && Num(IG, "c2") == 0 && UiBridge::GetStats().records[UiBridge::kKindUnitPosition] == 0 &&
		      UiBridge::GetStats().records[UiBridge::kKindFrameMark] == 0);
	}

	// ---- B4: exit to the menu: the whole state goes; a new state, a new registration, a new game ----
	{
		lua_close(e.L);   // the bridge keeps no pointer into it (ASan sees any use after free)
		g_reg = NULL;
		e = NewEngine();
		UiBridge::OnStateRegistered(g_reg);
		lua_State* FE = NewContext(e, "MainMenu", NULL);
		g_now += 300;
		Check("B4 exit: the front end of the new state: no install", !LuaEntry(FE) && UiBridge::GetStats().gen == 3);
		IG = NewContext(e, "InGame", kInGame);
		PP = NewContext(e, "SomePopup", kPopup);
		g_now += 300;
		Check("B4 exit: the next game's InGame is installed into (gen 4)", LuaEntry(PP) && UiBridge::GetStats().gen == 4);
		g_records.clear();
		RunIn(PP, "Events.RemoveAllArrowsEvent()");
		Frame(e);
		Check("B4 exit: records and drains in the new state", g_records.size() == 1 && g_records[0].gen == 4 &&
		      UiBridge::GetStats().drains >= 1);
	}

	// ---- an InGame context without some events: the rest is installed, the missing ones named ----
	{
		RunIn(e.L, "local f = EngineEvents ; EngineEvents = function(ctx) local t = f(ctx) ; "
		           "return setmetatable({}, { __index = function(_, k) if k == 'SpawnArrowEvent' or k == 'DisplayMovementIndicator' "
		           "then return nil end return t[k] end }) end");
		lua_State* G2 = NewContext(e, "InGame", kInGame);
		DestroyContext(e, IG);
		g_now += 300;
		LuaEntry(PP);
		Check("partial: the missing events are named in the log",
		      LogHas("listeners 2 of 4 (missing: SpawnArrowEvent, DisplayMovementIndicator)") && UiBridge::GetStats().installFailures == 1);
		(void)G2;
	}

	UiBridge::Configure(false, false);
	char line[640];
	UiBridge::FormatStats(line, sizeof(line));
	Check("stats: the summary line", strncmp(line, "ui bridge: gen ", 15) == 0, line);
	Check("stats: ui_bridge=1 alone: no unit-positions part in the line", strstr(line, "unit positions") == NULL);
	lua_close(e.L);
	g_reg = NULL;

	// ================================================================ issue 4754: positions only (the unit option)
	{
		UiBridge::ResetForTest();
		UiBridge::SetHooks(hooks);
		g_log.clear();
		UiBridge::Configure(false, false);
		UiBridge::ConfigurePositions(true);
		UiBridge::SetMainThread(GetCurrentThreadId());
		Check("positions: switched on, logged as positions only", UiBridge::PositionsOn() && !UiBridge::Enabled() &&
		      LogHas("unit positions on") && LogHas("positions only"));
		Engine p = NewEngine();
		Check("positions only: OnStateRegistered sets no test table", !UiBridge::OnStateRegistered(g_reg));
		lua_State* G = NewContext(p, "InGame", kInGame);
		lua_State* Q = NewContext(p, "SomePopup", kPopup);
		{
			lua_getglobal(Q, "VP64UiBridgeTest");
			Check("positions only: no context has the test table", lua_isnil(Q, -1));
			lua_pop(Q, 1);
		}
		g_now += 300;
		Check("positions only: installs into InGame (gen 1)", LuaEntry(Q) && UiBridge::GetStats().gen == 1);
		UiBridge::Stats s = UiBridge::GetStats();
		Check("positions only: one install, 3 listeners (2 positions + the drain), UnitMoving not replaced",
		      s.installs == 1 && s.installFailures == 0 && s.listenersAdded == 3 && s.unitMovingReplaced == 0,
		      "installs " + S(s.installs) + " listeners " + S(s.listenersAdded));
		Check("positions only: logged", LogHas("listeners 0 of 0, unit positions 2 of 2 (positions only)") &&
		      LogHas("global UnitMoving kept"));
		RunIn(G, "c1 = Events.LocalMachineUnitPositionChanged:Count() ; c2 = Events.UnitMoveQueueChanged:Count() ; "
		         "c3 = Events.LocalMachineAppUpdate:Count() ; c4 = Events.SpawnArrowEvent:Count() ; "
		         "um = debug.getinfo(UnitMoving).what");
		lua_getglobal(G, "um");
		const bool lua = lua_isstring(G, -1) && strcmp(lua_tostring(G, -1), "Lua") == 0;
		lua_pop(G, 1);
		Check("positions only: InGame has the two listeners and the drain, no arrow listener, its own UnitMoving", Num(G, "c1") == 1 &&
		      Num(G, "c2") == 1 && Num(G, "c3") == 1 && Num(G, "c4") == 0 && lua);
		Check("positions only: QueueRaise stays refused", !UiBridge::QueueRaise("SerialEventGameDataDirty", 0, NULL));
		g_records.clear();
		RunIn(Q, "Events.SpawnArrowEvent(1, 2, 3, 4)");
		Frame(p);
		Check("positions only: an arrow raise makes no record; a frame without positions makes no mark", g_records.empty());

		// ---- one position: held until the drain, then a mark and the record ----
		g_records.clear();
		RaisePos(p, "5, 17542, { x = 2303.9, y = -831.2, z = 12.04, w = 0 }");
		Check("position: held until the frame's drain", g_records.empty());
		Frame(p);
		const uint32_t frame = UiBridge::GetStats().drains;
		Check("position: a mark (frame = the drain's number, 1 record) then one kind-6 record", g_records.size() == 2 &&
		      IsMark(g_records[0], frame, 1) && g_records[1].seq == g_records[0].seq + 1 && g_records[1].gen == 1);
		Check("position: a[0] = unit, a[1..3] = the float bits of x, y, z exactly, player 5 in bits 16-23, no arrival",
		      g_records.size() == 2 && IsPos(g_records[1], 5, 17542, 2303.9, -831.2, 12.04, false));
		Check("F6 log: the first position call's arguments", LogHas("LocalMachineUnitPositionChanged call 1: 3 argument(s): "
		      "number 5, number 17542, table {x 2303.9, y -831.2, z 12.04, w 0}"));

		// ---- a walk's frame: two positions and the arrival ----
		g_records.clear();
		RaisePos(p, "5, 17542, { x = 10.25, y = 20.5, z = 1, w = 0 }");
		RaisePos(p, "5, 17542, { x = 11.75, y = 21.5, z = 2, w = 0 }");
		RaiseQueue(p, "5, 17542, false");
		Frame(p);
		Check("walk: one mark counting 3, then 2 positions and the arrival with the unit's last position", g_records.size() == 4 &&
		      IsMark(g_records[0], UiBridge::GetStats().drains, 3) && IsPos(g_records[1], 5, 17542, 10.25, 20.5, 1, false) &&
		      IsPos(g_records[2], 5, 17542, 11.75, 21.5, 2, false) && IsPos(g_records[3], 5, 17542, 11.75, 21.5, 2, true));
		Check("F6 log: the first UnitMoveQueueChanged call's arguments",
		      LogHas("UnitMoveQueueChanged call 1: 3 argument(s): number 5, number 17542, boolean false"));
		g_records.clear();
		RaiseQueue(p, "5, 17542");
		Frame(p);
		Check("arrival: bRemainingMoves nil counts as an arrival", g_records.size() == 2 &&
		      IsPos(g_records[1], 5, 17542, 11.75, 21.5, 2, true));
		g_records.clear();
		RaiseQueue(p, "5, 17542, true");
		Frame(p);
		UiBridge::PositionStats ps = UiBridge::GetPositionStats();
		Check("arrival: bRemainingMoves true makes no record and is counted", g_records.empty() && ps.queueRemaining == 1);
		g_records.clear();
		RaiseQueue(p, "7, 999, false");
		Frame(p);
		ps = UiBridge::GetPositionStats();
		Check("arrival: a unit with no known position: x, y, z = 0, counted", g_records.size() == 2 &&
		      g_records[1].kind == UiBridge::kKindUnitPosition && g_records[1].a[0] == 999 && g_records[1].a[1] == 0 &&
		      g_records[1].a[2] == 0 && g_records[1].a[3] == 0 &&
		      g_records[1].flags == ((7u << UiBridge::kPosPlayerShift) | (uint32_t)UiBridge::kPosArrival) &&
		      ps.arrivalsNoPosition == 1);
		Check("stats: positions 3, arrivals 3, marks 4", ps.positions == 3 && ps.arrivals == 3 && ps.marks == 4,
		      "positions " + S(ps.positions) + " arrivals " + S(ps.arrivals) + " marks " + S(ps.marks));

		// ---- negative: arguments that cannot be read are counted, no record ----
		{
			const uint32_t bad0 = UiBridge::GetPositionStats().badArgs;
			g_records.clear();
			RaisePos(p, "5, 17542, 7");                                     // not a table
			RaisePos(p, "5, 17542, { x = 1, y = 2 }");                      // a short table
			RaisePos(p, "5, 17542, { x = 1, y = 'a', z = 3 }");             // a field not a number
			RaisePos(p, "5, 17542");                                        // no position
			RaisePos(p, "'5', 17542, { x = 1, y = 2, z = 3 }");             // the player a string
			RaisePos(p, "300, 17542, { x = 1, y = 2, z = 3 }");             // the player out of 0..255
			RaisePos(p, "-1, 17542, { x = 1, y = 2, z = 3 }");
			RaisePos(p, "5, 1.5, { x = 1, y = 2, z = 3 }");                 // the unit not whole
			RaiseQueue(p, "5");                                             // no unit
			RaiseQueue(p, "nil, 17542, false");                             // no player
			Frame(p);
			const uint32_t bad = UiBridge::GetPositionStats().badArgs - bad0;
			Check("negative: 10 unreadable calls: 10 counted, no record, no mark", g_records.empty() && bad == 10,
			      "bad " + S(bad) + " records " + S((double)g_records.size()));
			Check("negative: the stacks are left as found", lua_gettop(p.L) == 0 && lua_gettop(G) == 0);
		}
		{
			g_records.clear();
			const uint32_t before = UiBridge::GetStats().wrongThread;
			OnOtherThread(RaisePosOther, &p);
			Frame(p);
			Check("negative: a position raised on another thread: counted, no record", g_records.empty() &&
			      UiBridge::GetStats().wrongThread == before + 1);
		}

		// ---- a frame with more records than the buffer: an early mark of its own, same frame number ----
		{
			g_records.clear();
			RunIn(p.L, "for i = 1, 300 do EngineDispatch('LocalMachineUnitPositionChanged', 3, i, { x = i, y = -i, z = 0.5 }) end");
			const size_t early = g_records.size();
			Frame(p);
			const uint32_t f = UiBridge::GetStats().drains;
			const int rest = 300 - UiBridge::kPosBufCap;
			Check("full buffer: 256 posted early after a mark, the other 44 after the drain's mark, same frame number",
			      early == (size_t)UiBridge::kPosBufCap + 1 && g_records.size() == 302 && IsMark(g_records[0], f, UiBridge::kPosBufCap) &&
			      IsMark(g_records[UiBridge::kPosBufCap + 1], f, rest) && IsPos(g_records[1], 3, 1, 1, -1, 0.5, false) &&
			      IsPos(g_records[301], 3, 300, 300, -300, 0.5, false) && UiBridge::GetPositionStats().earlyFlushes == 1,
			      "early " + S((double)early) + " total " + S((double)g_records.size()));
			bool seqOk = true;
			for (size_t i = 1; i < g_records.size(); ++i) seqOk = seqOk && g_records[i].seq == g_records[i - 1].seq + 1;
			Check("full buffer: the sequence has no gap", seqOk);
		}

		// ---- a new InGame environment: held positions leave under their generation; last positions forgotten ----
		{
			g_records.clear();
			RaisePos(p, "5, 17542, { x = 4, y = 5, z = 6 }");
			DestroyContext(p, G);
			G = NewContext(p, "InGame", kInGame);
			g_now += 300;
			Check("new environment: gen 2 installed", LuaEntry(Q) && UiBridge::GetStats().gen == 2);
			Check("new environment: the held position went out first, under gen 1, after its mark", g_records.size() == 2 &&
			      g_records[0].kind == UiBridge::kKindFrameMark && g_records[0].a[1] == 1 && g_records[1].gen == 1 &&
			      IsPos(g_records[1], 5, 17542, 4, 5, 6, false));
			g_records.clear();
			RaiseQueue(p, "5, 17542, false");
			Frame(p);
			Check("new environment: the old game's last position is forgotten (arrival without position), gen 2",
			      g_records.size() == 2 && g_records[1].gen == 2 && g_records[1].a[1] == 0 &&
			      (g_records[1].flags & UiBridge::kPosArrival));
			Check("new environment: no stale call (the old context's listeners went with it)", UiBridge::GetStats().staleCalls == 0);
		}
		UiBridge::FormatStats(line, sizeof(line));
		Check("positions only: the stats line has the positions part", strstr(line, "; unit positions: records unit-position") != NULL,
		      line);
		lua_close(p.L);
		g_reg = NULL;
	}

	// ================================================================ issue 4754: positions asked for after a full install
	{
		UiBridge::ResetForTest();
		UiBridge::SetHooks(hooks);
		g_log.clear();
		UiBridge::Configure(true, true);
		UiBridge::SetMainThread(GetCurrentThreadId());
		Engine f = NewEngine();
		UiBridge::OnStateRegistered(g_reg);
		lua_State* G = NewContext(f, "InGame", kInGame);
		lua_State* Q = NewContext(f, "SomePopup", kPopup);
		g_now += 300;
		LuaEntry(Q);
		Check("full then positions: gen 1 without positions", UiBridge::GetStats().gen == 1 && !LogHas("unit positions 2 of 2"));
		UiBridge::ConfigurePositions(true);
		Check("full then positions: logged with the whole bridge", LogHas("with the whole bridge (ui_bridge=1)"));
		g_now += 300;
		Check("full then positions: the next check installs gen 2 into the same environment", LuaEntry(Q) &&
		      UiBridge::GetStats().gen == 2 && LogHas("the positions were asked for") && LogHas("listeners 4 of 4, unit positions 2 of 2,"));
		g_now += 300;
		Check("full then positions: no third install", !LuaEntry(Q) && UiBridge::GetStats().gen == 2);
		g_records.clear();
		RunIn(Q, "Events.SpawnArrowEvent(1, 2, 3, 4)");
		RaisePos(f, "2, 77, { x = 1, y = 2, z = 3 }");
		Frame(f);
		Check("full + positions: one arrow record (gen 1's listener stale), then the mark and the position",
		      g_records.size() == 3 && g_records[0].kind == UiBridge::kKindSpawnArrow && g_records[0].gen == 2 &&
		      IsMark(g_records[1], UiBridge::GetStats().drains, 1) && IsPos(g_records[2], 2, 77, 1, 2, 3, false));
		g_records.clear();
		RunIn(G, "Moving(1, 2)");
		Check("full + positions: UnitMoving still replaced and recorded", g_records.size() == 1 &&
		      g_records[0].kind == UiBridge::kKindUnitMovingCalled);
		lua_close(f.L);
		g_reg = NULL;
	}

	// ================================================================ ticket 0199 part 2: arrows only (the arc option)
	{
		UiBridge::ResetForTest();
		UiBridge::SetHooks(hooks);
		g_log.clear();
		UiBridge::Configure(false, false);
		UiBridge::ConfigureArrows(true);
		UiBridge::SetMainThread(GetCurrentThreadId());
		Check("arrows: switched on, logged as arrows only", UiBridge::ArrowsOn() && !UiBridge::Enabled() &&
		      LogHas("arrows on") && LogHas("arrows only"));
		Engine a = NewEngine();
		Check("arrows only: OnStateRegistered sets no test table", !UiBridge::OnStateRegistered(g_reg));
		// InGame's own file adds a listener before the bridge wraps the event (as Bombardment.lua's context does)
		lua_State* G = NewContext(a, "InGame", "arrowHits, arrowArgs = 0, -1\n"
		                                       "Events.SpawnArrowEvent.Add(function(...) arrowHits = arrowHits + 1 ; arrowArgs = select('#', ...) "
		                                       "if ... == 99 then error('boom') end end)\n"
		                                       "removeHits = 0\nEvents.RemoveAllArrowsEvent.Add(function() removeHits = removeHits + 1 end)\n");
		lua_State* Q = NewContext(a, "SomePopup", kPopup);
		g_now += 300;
		Check("arrows only: installs into InGame (gen 1)", LuaEntry(Q) && UiBridge::GetStats().gen == 1);
		UiBridge::Stats s = UiBridge::GetStats();
		UiBridge::ArrowStats as = UiBridge::GetArrowStats();
		Check("arrows only: one install, no failure, one wrap, UnitMoving not replaced", s.installs == 1 && s.installFailures == 0 &&
		      as.wraps == 1 && as.unwraps == 0 && s.unitMovingReplaced == 0);
		Check("arrows only: logged", LogHas("gen 1: arrows: Events.SpawnArrowEvent wrapped, RemoveAllArrowsEvent listener added (arrows only)"));
		RunIn(G, "c1 = Events.SpawnArrowEvent:Count() ; c2 = Events.RemoveAllArrowsEvent:Count() ; c3 = Events.LocalMachineAppUpdate:Count() ; "
		         "w = type(Events.SpawnArrowEvent)");
		Check("arrows only: a colon call through the wrapper reaches the original (Count = the file's listener); the remove "
		      "listener and the drain added", Num(G, "c1") == 1 && Num(G, "c2") == 2 && Num(G, "c3") == 1);

		// ---- a raise from the InGame context: one kind-1 record with the four integers, the original still runs ----
		g_records.clear();
		RunIn(G, "Events.SpawnArrowEvent(50, 14, 49, 16)");
		Check("spawn: one kind-1 record a = 50, 14, 49, 16, flags 0, gen 1, posted at once", g_records.size() == 1 &&
		      g_records[0].kind == UiBridge::kKindSpawnArrow && g_records[0].a[0] == 50 && g_records[0].a[1] == 14 &&
		      g_records[0].a[2] == 49 && g_records[0].a[3] == 16 && g_records[0].flags == 0 && g_records[0].gen == 1 &&
		      g_records[0].version == 1);
		Check("spawn: the original ran with the same four arguments (the file's listener)", Num(G, "arrowHits") == 1 &&
		      Num(G, "arrowArgs") == 4);
		Check("F6 log: the first wrapper call's arguments", LogHas("SpawnArrowEvent call 1: 5 argument(s): table"));
		g_records.clear();
		RunIn(G, "Events.RemoveAllArrowsEvent()");
		Check("remove: one kind-2 record, the file's listener ran", g_records.size() == 1 &&
		      g_records[0].kind == UiBridge::kKindRemoveAllArrows && Num(G, "removeHits") == 1);
		g_records.clear();
		RunIn(G, "Events.SpawnArrowEvent.Add(function() late = (late or 0) + 1 end) ; Events.SpawnArrowEvent(-3, 0, 7, 2147483647)");
		Check("dot Add after the wrap reaches the original; negative and large plots kept", Num(G, "late") == 1 && g_records.size() == 1 &&
		      g_records[0].a[0] == -3 && g_records[0].a[3] == 2147483647);

		// ---- negative: arguments that are not four whole numbers: no record, counted, the original still called ----
		{
			g_records.clear();
			const double hits0 = Num(G, "arrowHits");
			RunIn(G, "Events.SpawnArrowEvent(1, 2)");
			RunIn(G, "Events.SpawnArrowEvent(1.5, 2, 3, 4)");
			RunIn(G, "Events.SpawnArrowEvent('a', 2, 3, 4)");
			RunIn(G, "Events.SpawnArrowEvent(1, 2, 3, 4, 5)");
			RunIn(G, "Events.SpawnArrowEvent()");
			RunIn(G, "Events.SpawnArrowEvent(1, 2, 3, 3000000000)");
			Check("negative: 6 bad calls: no record, 6 counted, the original called 6 times", g_records.empty() &&
			      UiBridge::GetArrowStats().badArgs == 6 && Num(G, "arrowHits") == hits0 + 6,
			      "bad " + S(UiBridge::GetArrowStats().badArgs) + " records " + S((double)g_records.size()));
			std::string err;
			const bool ok = RunIn(G, "Events.SpawnArrowEvent(99, 1, 2, 3)", &err);
			Check("negative: an error of the original propagates through the wrapper (the record went out before the call)",
			      !ok && err.find("boom") != std::string::npos && g_records.size() == 1 && g_records[0].a[0] == 99, err);
			Check("negative: the stacks are left as found", lua_gettop(a.L) == 0 && lua_gettop(G) == 0);
			g_records.clear();
			const uint32_t before = UiBridge::GetStats().wrongThread;
			OnOtherThread(RunCode, G);
			Check("negative: a raise on another thread: counted, no record, the original called", g_records.empty() &&
			      UiBridge::GetStats().wrongThread == before + 1);
			g_records.clear();
			RunIn(Q, "Events.SpawnArrowEvent(1, 2, 3, 4)");
			Check("another context's Events table is not wrapped: no record (Bombardment.lua raises it from InGame)", g_records.empty());
		}

		// ---- the drain went silent: gen 2 into the same environment, the old wrapper taken off, no nesting ----
		{
			Frame(a);
			g_now += 300;
			LuaEntry(Q);   // sees the drain's tick move
			g_now += UiBridge::kSilentMs + 300;
			const double hits0 = Num(G, "arrowHits");
			Check("re-install: gen 2 after the silent drain", LuaEntry(Q) && UiBridge::GetStats().gen == 2);
			as = UiBridge::GetArrowStats();
			Check("re-install: the older wrapper unwrapped (logged)", as.wraps == 2 && as.unwraps == 1 &&
			      LogHas("gen 2: arrows: Events.SpawnArrowEvent wrapped (an older generation's wrapper taken off)"));
			g_records.clear();
			RunIn(G, "Events.SpawnArrowEvent(5, 6, 7, 8)");
			Check("re-install: one record under gen 2, the original called once, no stale wrapper call", g_records.size() == 1 &&
			      g_records[0].gen == 2 && Num(G, "arrowHits") == hits0 + 1 && UiBridge::GetArrowStats().staleCalls == 0);
			g_now += 300;
			Check("re-install: no third install", !LuaEntry(Q) && UiBridge::GetStats().gen == 2);
		}
		char aline[1024];
		UiBridge::FormatStats(aline, sizeof(aline));
		Check("arrows: the stats line has the arrows part", strstr(aline, "; arrows: spawns 4, removes 1, bad arguments 6") != NULL, aline);
		lua_close(a.L);
		g_reg = NULL;
	}

	// ================================================================ ticket 0199 part 2: arrows with the whole bridge
	{
		UiBridge::ResetForTest();
		UiBridge::SetHooks(hooks);
		g_log.clear();
		UiBridge::Configure(true, false);
		UiBridge::ConfigureArrows(true);
		UiBridge::SetMainThread(GetCurrentThreadId());
		Engine f = NewEngine();
		lua_State* G = NewContext(f, "InGame", kInGame);
		lua_State* Q = NewContext(f, "SomePopup", kPopup);
		g_now += 300;
		LuaEntry(Q);
		UiBridge::Stats s = UiBridge::GetStats();
		Check("full + arrows: one install, no failure; the generic SpawnArrowEvent listener left out", s.installs == 1 &&
		      s.installFailures == 0 && LogHas("listeners 3 of 3") && LogHas("RemoveAllArrowsEvent listener added (the whole bridge's)"));
		g_records.clear();
		RunIn(G, "Events.SpawnArrowEvent(50, 14, 49, 16) ; Events.RemoveAllArrowsEvent()");
		Check("full + arrows: one kind-1 record WITH the end points (no zero record), one kind-2", g_records.size() == 2 &&
		      g_records[0].kind == UiBridge::kKindSpawnArrow && g_records[0].a[0] == 50 && g_records[0].a[3] == 16 &&
		      g_records[1].kind == UiBridge::kKindRemoveAllArrows);
		g_records.clear();
		RunIn(G, "Moving(1, 2)");
		Check("full + arrows: UnitMoving still replaced", UiBridge::GetStats().unitMovingReplaced == 1);
		lua_close(f.L);
		g_reg = NULL;
	}

	// ================================================================ ticket 0199 part 2: negative, no SpawnArrowEvent in InGame
	{
		UiBridge::ResetForTest();
		UiBridge::SetHooks(hooks);
		g_log.clear();
		UiBridge::Configure(false, false);
		UiBridge::ConfigureArrows(true);
		UiBridge::SetMainThread(GetCurrentThreadId());
		Engine m = NewEngine();
		RunIn(m.L, "local f = EngineEvents ; EngineEvents = function(ctx) local t = f(ctx) ; "
		           "return setmetatable({}, { __index = function(_, k) if k == 'SpawnArrowEvent' then return nil end return t[k] end }) end");
		lua_State* G = NewContext(m, "InGame", kInGame);
		g_now += 300;
		LuaEntry(G);
		Check("missing event: an install failure, logged NOT wrapped, the event named", UiBridge::GetStats().installFailures == 1 &&
		      UiBridge::GetStats().installs == 0 && LogHas("arrows: Events.SpawnArrowEvent NOT wrapped") &&
		      LogHas("(missing: SpawnArrowEvent)") && UiBridge::GetArrowStats().wraps == 0);
		g_now += 300;
		Check("missing event: not retried at every check (no install loop)", !LuaEntry(G) && UiBridge::GetStats().gen == 1);
		lua_close(m.L);
		g_reg = NULL;
	}

	// ================================================================ ticket 0300 part 2: path only (the path option)
	{
		UiBridge::ResetForTest();
		UiBridge::SetHooks(hooks);
		g_log.clear();
		UiBridge::Configure(false, false);
		UiBridge::SetPathTests(FakePathTests);
		UiBridge::ConfigurePath(true);
		UiBridge::SetMainThread(GetCurrentThreadId());
		Check("path: switched on, logged as path only", UiBridge::PathOn() && !UiBridge::Enabled() && LogHas("path on") &&
		      LogHas("path only"));
		Engine a = NewEngine();
		Check("path only: OnStateRegistered sets no test table", !UiBridge::OnStateRegistered(g_reg));
		lua_State* G = NewContext(a, "InGame", kPathInGame);
		lua_State* Q = NewContext(a, "SomePopup", kPopup);
		g_now += 300;
		Check("path only: installs into InGame (gen 1)", LuaEntry(Q) && UiBridge::GetStats().gen == 1);
		UiBridge::Stats s = UiBridge::GetStats();
		Check("path only: one install, no failure, UnitMoving not replaced, 3 listeners (path, show, drain)", s.installs == 1 &&
		      s.installFailures == 0 && s.unitMovingReplaced == 0 && s.listenersAdded == 3, "listeners " + S(s.listenersAdded));
		Check("path only: logged", LogHas("gen 1: path: Events.UIPathFinderUpdate listener added, DisplayMovementIndicator listener "
		                                  "added (path only), tests set"));
		RunIn(G, "c1 = Events.UIPathFinderUpdate:Count() ; c2 = Events.DisplayMovementIndicator:Count() ; c3 = Events.SpawnArrowEvent:Count()");
		Check("path only: the file's and the bridge's path listeners, one show listener, no arrow listener", Num(G, "c1") == 2 &&
		      Num(G, "c2") == 1 && Num(G, "c3") == 0);

		// ---- 3801's preview (5 nodes), raised by the EXE: VP's listener first raises the show, then the bridge's records ----
		g_records.clear();
		g_testCalls = 0;
		Raise(a, "UIPathFinderUpdate", "{ { x = 50, y = 13, turn = 22 }, { x = 51, y = 14, turn = 21 }, { x = 52, y = 14, turn = 120 }, "
		                               "{ x = 52, y = 13, turn = 121 }, { x = 53, y = 13, turn = 220 } }");
		const uint32_t T = UiBridge::kPathTested;
		Check("preview: VP's listener ran; one show record, then 5 node records", Num(G, "vpHits") == 1 && g_records.size() == 6 &&
		      g_records[0].kind == UiBridge::kKindDisplayMovement && g_records[0].a[0] == 1, "records " + S((double)g_records.size()));
		Check("preview: the nodes in order, index / count / tested bits, update 1",
		      g_records.size() == 6 && IsNode(g_records[1], 50, 13, 22, 0, 5, T, 1) && IsNode(g_records[2], 51, 14, 21, 1, 5, T, 1) &&
		      IsNode(g_records[3], 52, 14, 120, 2, 5, T | UiBridge::kPathEmbark, 1) && IsNode(g_records[4], 52, 13, 121, 3, 5, T, 1) &&
		      IsNode(g_records[5], 53, 13, 220, 4, 5, T | UiBridge::kPathAttack, 1));
		Check("preview: the tests hook got the unit (0, 1002) and the 5 plots; entry 0's embark not posted", g_testCalls == 1 &&
		      g_lastQuery.unitOwner == 0 && g_lastQuery.unitId == 1002 && g_lastQuery.count == 5 && g_lastQuery.x[4] == 53 &&
		      g_lastQuery.y[4] == 13 && (PathBits(g_records[1]) & UiBridge::kPathEmbark) == 0);
		Check("F6 log: the first update's reading", LogHas("UIPathFinderUpdate call 1: 5 node(s), first (50, 13, turn 22), unit 0/1002"));

		// ---- an empty path (the mode ends, or no route): one record with the mouse plot and the cursor's tests ----
		g_records.clear();
		Raise(a, "UIPathFinderUpdate", "{}");
		Check("empty: one record count 0 with the mouse plot (7, 9) and the swap bit, update 2", g_records.size() == 2 &&
		      IsNode(g_records[1], 7, 9, 0, 0, 0, T | UiBridge::kPathSwap, 2) && g_lastQuery.count == 0 && g_lastQuery.mouseX == 7 &&
		      g_lastQuery.mouseY == 9);
		// ---- a path longer than the EXE's record: cut to 64, marked ----
		g_records.clear();
		RunIn(a.L, "local p = {} for i = 1, 70 do p[i] = { x = i, y = 2, turn = 220 } end ; EngineDispatch('UIPathFinderUpdate', p)");
		Check("long: 64 node records, each marked cut, the 64th is node 63 of 64", g_records.size() == 65 &&
		      IsNode(g_records[64], 64, 2, 220, 63, 64, T | UiBridge::kPathTruncated | UiBridge::kPathAttack, 3) &&
		      UiBridge::GetPathStats().truncated == 1);
		// ---- negative and coordinates: an x / y beyond 16 bits is refused, negative ones kept ----
		g_records.clear();
		Raise(a, "UIPathFinderUpdate", "{ { x = -1, y = 0, turn = -5 }, { x = 32767, y = -32768, turn = 2147483647 } }");
		Check("edges: negative and 16-bit extreme plots and a 32-bit turn kept", g_records.size() == 3 &&
		      IsNode(g_records[1], -1, 0, -5, 0, 2, T, 4) && IsNode(g_records[2], 32767, -32768, 2147483647, 1, 2, T | UiBridge::kPathAttack, 4));
		{
			g_records.clear();
			const uint32_t bad0 = UiBridge::GetPathStats().badArgs;
			const uint32_t upd0 = UiBridge::GetPathStats().updates;
			Raise(a, "UIPathFinderUpdate", "nil");
			Raise(a, "UIPathFinderUpdate", "'path'");
			Raise(a, "UIPathFinderUpdate", "{ { x = 1, y = 2 } }");
			Raise(a, "UIPathFinderUpdate", "{ { x = 1.5, y = 2, turn = 3 } }");
			Raise(a, "UIPathFinderUpdate", "{ { x = 40000, y = 2, turn = 3 } }");
			Raise(a, "UIPathFinderUpdate", "{ 5 }");
			Check("negative: 6 unreadable paths: no node record (the shows of VP's listener only), 6 counted, no update number used",
			      CountKind(UiBridge::kKindPathNode) == 0 && UiBridge::GetPathStats().badArgs == bad0 + 6 &&
			      UiBridge::GetPathStats().updates == upd0, "bad " + S(UiBridge::GetPathStats().badArgs - bad0));
			Check("negative: the stacks are left as found", lua_gettop(a.L) == 0 && lua_gettop(G) == 0);
			// the UI's unit fails: the path is posted untested, the hook not called
			g_records.clear();
			g_testCalls = 0;
			RunIn(G, "UI.GetHeadSelectedUnit = function() error('no unit here') end");
			Raise(a, "UIPathFinderUpdate", "{ { x = 1, y = 2, turn = 3 }, { x = 2, y = 2, turn = 3 } }");
			Check("negative: a failing UI.GetHeadSelectedUnit: posted untested, no hook call, counted, logged",
			      g_records.size() == 3 && IsNode(g_records[2], 2, 2, 3, 1, 2, 0, 5) && g_testCalls == 0 &&
			      UiBridge::GetPathStats().noUnit >= 1 && LogHas("reading the unit or the mouse failed"));
			RunIn(G, "UI.GetHeadSelectedUnit = function() return nil end");
			g_records.clear();
			Raise(a, "UIPathFinderUpdate", "{ { x = 1, y = 2, turn = 3 } }");
			Check("negative: no selected unit: posted untested", g_records.size() == 2 && IsNode(g_records[1], 1, 2, 3, 0, 1, 0, 6) &&
			      g_testCalls == 0);
			RunIn(G, "UI.GetHeadSelectedUnit = function() return MakeUnit(0, 'x') end");
			g_records.clear();
			Raise(a, "UIPathFinderUpdate", "{ { x = 1, y = 2, turn = 3 } }");
			Check("negative: a unit id that is no number: posted untested", g_records.size() == 2 && PathBits(g_records[1]) == (1u << 8) &&
			      g_testCalls == 0);
			RunIn(G, "UI.GetHeadSelectedUnit = function() return headUnit end");
			g_records.clear();
			const uint32_t wt = UiBridge::GetStats().wrongThread;
			OnOtherThread(RaisePathOther, &a);
			Check("negative: an update on another thread: counted, no node record", CountKind(UiBridge::kKindPathNode) == 0 &&
			      UiBridge::GetStats().wrongThread > wt);
			// no tests hook: records untested
			UiBridge::SetPathTests(NULL);
			g_records.clear();
			Raise(a, "UIPathFinderUpdate", "{ { x = 1, y = 2, turn = 3 } }");
			Check("no hook: posted untested", g_records.size() == 2 && PathBits(g_records[1]) == (1u << 8));
			UiBridge::SetPathTests(FakePathTests);
		}
		// ---- the drain went silent: gen 2, the old listener is stale, update numbers start again ----
		{
			Frame(a);
			g_now += 300;
			LuaEntry(Q);
			g_now += UiBridge::kSilentMs + 300;
			Check("re-install: gen 2 after the silent drain", LuaEntry(Q) && UiBridge::GetStats().gen == 2);
			g_records.clear();
			const uint32_t stale0 = UiBridge::GetPathStats().staleCalls;
			Raise(a, "UIPathFinderUpdate", "{ { x = 3, y = 4, turn = 5 } }");
			Check("re-install: one node record under gen 2 (update 1), the gen-1 listener stale", CountKind(UiBridge::kKindPathNode) == 1 &&
			      g_records.back().gen == 2 && g_records.back().a[3] == 1 && UiBridge::GetPathStats().staleCalls == stale0 + 1);
		}
		char pline[1400];
		UiBridge::FormatStats(pline, sizeof(pline));
		Check("path: the stats line has the path part and the listener time", strstr(pline, "; path: updates ") != NULL &&
		      strstr(pline, "listener time") != NULL, pline);
		lua_close(a.L);
		g_reg = NULL;
	}

	// ================================================================ ticket 0300 part 2: path with the whole bridge
	{
		UiBridge::ResetForTest();
		UiBridge::SetHooks(hooks);
		g_log.clear();
		UiBridge::Configure(true, false);
		UiBridge::SetPathTests(FakePathTests);
		UiBridge::ConfigurePath(true);
		UiBridge::SetMainThread(GetCurrentThreadId());
		Engine f = NewEngine();
		lua_State* G = NewContext(f, "InGame", kPathInGame);
		g_now += 300;
		LuaEntry(G);
		UiBridge::Stats s = UiBridge::GetStats();
		Check("full + path: one install, no failure, the generic show listener serves (no second one)", s.installs == 1 &&
		      s.installFailures == 0 && LogHas("listeners 4 of 4") && LogHas("DisplayMovementIndicator listener added (the whole bridge's)"));
		g_records.clear();
		Raise(f, "UIPathFinderUpdate", "{ { x = 50, y = 13, turn = 22 }, { x = 51, y = 14, turn = 21 } }");
		Check("full + path: one show record (not two), two node records", CountKind(UiBridge::kKindDisplayMovement) == 1 &&
		      CountKind(UiBridge::kKindPathNode) == 2);
		lua_close(f.L);
		g_reg = NULL;
	}

	// ================================================================ ticket 0300 part 2: negative, no UIPathFinderUpdate in InGame
	{
		UiBridge::ResetForTest();
		UiBridge::SetHooks(hooks);
		g_log.clear();
		UiBridge::Configure(false, false);
		UiBridge::ConfigurePath(true);
		UiBridge::SetMainThread(GetCurrentThreadId());
		Engine m = NewEngine();
		RunIn(m.L, "local f = EngineEvents ; EngineEvents = function(ctx) local t = f(ctx) ; "
		           "return setmetatable({}, { __index = function(_, k) if k == 'UIPathFinderUpdate' then return nil end return t[k] end }) end");
		lua_State* G = NewContext(m, "InGame", kInGame);
		g_now += 300;
		LuaEntry(G);
		Check("missing event: an install failure, logged NOT added, tests NOT set, the event named",
		      UiBridge::GetStats().installFailures == 1 && UiBridge::GetStats().installs == 0 &&
		      LogHas("path: Events.UIPathFinderUpdate listener NOT added") && LogHas("tests NOT set") &&
		      LogHas("(missing: UIPathFinderUpdate)"));
		g_now += 300;
		Check("missing event: not retried at every check (no install loop)", !LuaEntry(G) && UiBridge::GetStats().gen == 1);
		lua_close(m.L);
		g_reg = NULL;
	}

	printf("ui_bridge_test: %d/%d checks passed\n", g_checks - g_failed, g_checks);
	return g_failed == 0 ? 0 : 1;
}
