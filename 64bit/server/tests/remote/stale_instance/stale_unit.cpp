// stale_unit.cpp - the test program of ticket 0576 (server/tests/remote/stale_instance/test_stale_instance.py). It links
// server/src/remote/srv_lua_stale.cpp with lua51_x64 and plays the DLL's Lua side with stand-ins that have the DLL's
// shapes (CvLuaScopedInstance at the pin): LuaTypes.Unit / City / Player type tables (weak __instances, __index = the
// table), instance tables {__instance = light userdata} with the type table as metatable, the Players global, and C
// functions that read the object through GetInstance(L) (index 1, lua_getfield "__instance", stack restored, then the
// read). Each unit and city lives alone in its own VirtualAlloc region, so "freed" means unmapped, as the CvUnit of
// 0573's crash e711. Calls go through the same sequence as the server's kRt_LuaCall: callee and arguments on the stack,
// SrvLuaStale::CheckArgs over the arguments, then lua_pcall (or nil and a LOUD line when an argument is stale).
//
//   stale_unit.exe <case>      prints "RESULT ..." lines, "DONE" and exits 0; an unhandled exception prints
//                              "UNHANDLED 0x%08X" and ends the process with that code (no Windows error box).
// Cases: live, freed, freed-unguarded, off-env, deleted, reused, garbage, city-freed, city-deleted, others, nolearn,
//        wrapped, stack, bench [N].
// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include "remote/srv_lua_stale.h"
extern "C" {
#include <lauxlib.h>
#include <lualib.h>
}

namespace
{
struct FakeObj  // the fields a getter reads; x at +0x48 as CvUnit's m_iX in the crash's stack (CvUnit::getX)
{
	int pad[18];
	int x, y, id, owner;
};
static_assert(offsetof(FakeObj, x) == 0x48, "x at +0x48");
struct FakePlayer
{
	int id;
	std::map<int, FakeObj*> units, cities;
};
FakePlayer g_players[64];
bool g_wrapped = false;

FakeObj* NewObj(int owner, int id, int x, int y, void* at = nullptr)
{
	void* p = at ? VirtualAlloc(at, sizeof(FakeObj), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)
	             : VirtualAlloc(nullptr, sizeof(FakeObj), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if(!p)
	{
		printf("FAIL VirtualAlloc %lu\n", GetLastError());
		exit(3);
	}
	FakeObj* o = static_cast<FakeObj*>(p);
	memset(o, 0x11, sizeof(*o));
	o->x = x;
	o->y = y;
	o->id = id;
	o->owner = owner;
	return o;
}
void FreeObj(FakeObj* o) { VirtualFree(o, 0, MEM_RELEASE); }

// CvLuaScopedInstance<Derived, T>::GetInstance at the pin (bErrorOnFail): index 1, unchecked pointer.
void* GetInstance(lua_State* L, int idx = 1)
{
	const int top = lua_gettop(L);
	void* p = nullptr;
	if(lua_type(L, idx) == LUA_TTABLE)
	{
		lua_getfield(L, idx, "__instance");
		if(lua_type(L, -1) == LUA_TLIGHTUSERDATA)
			p = lua_touserdata(L, -1);
	}
	lua_settop(L, top);
	if(!p)
		luaL_error(L, "Not a valid instance.  Either the instance is NULL or you used '.' instead of ':'.");
	return p;
}

// CvLuaScopedInstance::Push: the cached table in LuaTypes[type].__instances, else a new one.
void PushInstance(lua_State* L, const char* type, void* p)
{
	if(!p)
	{
		lua_pushnil(L);
		return;
	}
	lua_getglobal(L, "LuaTypes");
	lua_getfield(L, -1, type);
	lua_remove(L, -2);
	lua_pushstring(L, "__instances");
	lua_rawget(L, -2);
	lua_pushlightuserdata(L, p);
	lua_rawget(L, -2);
	if(lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_createtable(L, 0, 1);
		lua_pushstring(L, "__instance");
		lua_pushlightuserdata(L, p);
		lua_rawset(L, -3);
		lua_pushvalue(L, -3);
		lua_setmetatable(L, -2);
		lua_pushlightuserdata(L, p);
		lua_pushvalue(L, -2);
		lua_rawset(L, -4);
	}
	lua_replace(L, -3);
	lua_pop(L, 1);
}

int UnitGetID(lua_State* L)
{
	FakeObj* o = static_cast<FakeObj*>(GetInstance(L));
	lua_pushinteger(L, o->id);
	return 1;
}
int UnitGetOwner(lua_State* L)
{
	FakeObj* o = static_cast<FakeObj*>(GetInstance(L));
	lua_pushinteger(L, o->owner);
	return 1;
}
int UnitGetPlot(lua_State* L)  // CvLuaUnit::lGetPlot's first read: getX
{
	FakeObj* o = static_cast<FakeObj*>(GetInstance(L));
	lua_pushinteger(L, o->x * 1000 + o->y);
	return 1;
}
int UnitGetTransportUnit(lua_State* L)  // CvLuaUnit::lGetTransportUnit (0573's crash 9a2f): the first member read
{
	FakeObj* o = static_cast<FakeObj*>(GetInstance(L));
	if(o->pad[0] == 0x11111111)  // NewObj's fill: no transport
		lua_pushnil(L);
	else
		lua_pushinteger(L, o->pad[0]);
	return 1;
}
int CityGetPopulation(lua_State* L)  // a city method
{
	FakeObj* o = static_cast<FakeObj*>(GetInstance(L));
	lua_pushinteger(L, o->y);
	return 1;
}
int UnitDistance(lua_State* L)  // (unit, otherUnit): an instance in argument 2
{
	FakeObj* a = static_cast<FakeObj*>(GetInstance(L, 1));
	FakeObj* b = static_cast<FakeObj*>(GetInstance(L, 2));
	lua_pushinteger(L, abs(a->x - b->x) + abs(a->y - b->y));
	return 1;
}
int PlayerGetUnitByID(lua_State* L)
{
	FakePlayer* pl = static_cast<FakePlayer*>(GetInstance(L));
	const int id = (int)lua_tointeger(L, 2);
	std::map<int, FakeObj*>::const_iterator it = pl->units.find(id);
	PushInstance(L, "Unit", it != pl->units.end() ? it->second : nullptr);
	return 1;
}
int PlayerGetCityByID(lua_State* L)
{
	FakePlayer* pl = static_cast<FakePlayer*>(GetInstance(L));
	const int id = (int)lua_tointeger(L, 2);
	std::map<int, FakeObj*>::const_iterator it = pl->cities.find(id);
	PushInstance(L, "City", it != pl->cities.end() ? it->second : nullptr);
	return 1;
}
int PlayerGetID(lua_State* L)
{
	FakePlayer* pl = static_cast<FakePlayer*>(GetInstance(L));
	lua_pushinteger(L, pl->id);
	return 1;
}
int GameNumber(lua_State* L)  // a function without instances
{
	lua_pushnumber(L, lua_tonumber(L, 1) + 1);
	return 1;
}

// upstream master's protectedWrapperAux shape: a C closure whose one upvalue is the C function
int Wrapper(lua_State* L)
{
	lua_CFunction f = lua_tocfunction(L, lua_upvalueindex(1));
	return f(L);
}

void Method(lua_State* L, int t, const char* name, lua_CFunction f)
{
	lua_pushcfunction(L, f);
	if(g_wrapped)
		lua_pushcclosure(L, Wrapper, 1);
	lua_setfield(L, t, name);
}

// CvLuaScopedInstance::PushTypeTable: LuaTypes[name] = {__instances = weak, __index = itself}
int TypeTable(lua_State* L, const char* name)
{
	lua_getglobal(L, "LuaTypes");
	lua_newtable(L);
	const int t = lua_gettop(L);
	lua_pushstring(L, "__instances");
	lua_newtable(L);
	lua_createtable(L, 0, 1);
	lua_pushstring(L, "__mode");
	lua_pushstring(L, "v");
	lua_rawset(L, -3);
	lua_setmetatable(L, -2);
	lua_rawset(L, t);
	lua_pushstring(L, "__index");
	lua_pushvalue(L, t);
	lua_rawset(L, t);
	lua_pushvalue(L, t);
	lua_setfield(L, -3, name);
	lua_remove(L, -2);
	return lua_gettop(L);
}

lua_State* NewState(bool withPlayerType)
{
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);
	lua_newtable(L);
	lua_setglobal(L, "LuaTypes");
	int t = TypeTable(L, "Unit");
	Method(L, t, "GetID", UnitGetID);
	Method(L, t, "GetOwner", UnitGetOwner);
	Method(L, t, "GetPlot", UnitGetPlot);
	Method(L, t, "Distance", UnitDistance);
	Method(L, t, "GetTransportUnit", UnitGetTransportUnit);
	lua_pop(L, 1);
	t = TypeTable(L, "City");
	Method(L, t, "GetID", UnitGetID);	 // the same field layout
	Method(L, t, "GetOwner", UnitGetOwner);
	Method(L, t, "GetPlot", UnitGetPlot);
	Method(L, t, "GetPopulation", CityGetPopulation);
	lua_pop(L, 1);
	t = TypeTable(L, "Plot");  // another kind: never checked
	Method(L, t, "GetX", UnitGetPlot);
	lua_pop(L, 1);
	if(withPlayerType)
	{
		t = TypeTable(L, "Player");
		Method(L, t, "GetUnitByID", PlayerGetUnitByID);
		Method(L, t, "GetCityByID", PlayerGetCityByID);
		Method(L, t, "GetID", PlayerGetID);
		lua_pop(L, 1);
		lua_newtable(L);
		for(int i = 0; i < 64; ++i)
		{
			g_players[i].id = i;
			PushInstance(L, "Player", &g_players[i]);
			lua_rawseti(L, -2, i);
		}
		lua_setglobal(L, "Players");
	}
	else
	{
		lua_newtable(L);
		lua_setglobal(L, "Players");
	}
	return L;
}

// The server side: what kRt_LuaCall does (srv_remote_lua.cpp), with the guard's state learnt as RebuildTypeNames does.
struct Server
{
	lua_State* L;
	const void* mts[SrvLuaStale::kKinds];
	unsigned mask;
	int playersRef;
	long loud;
};

void Learn(Server& s)
{
	lua_getglobal(s.L, "LuaTypes");
	s.mask = SrvLuaStale::Learn(s.L, lua_gettop(s.L), s.mts);
	lua_pop(s.L, 1);
	lua_getglobal(s.L, "Players");
	s.playersRef = luaL_ref(s.L, LUA_REGISTRYINDEX);
	s.loud = 0;
}

// Pushes LuaTypes[type][name] (the callee as the proxy names it by its kLCFunc id).
void PushMethod(lua_State* L, const char* type, const char* name)
{
	lua_getglobal(L, "LuaTypes");
	lua_getfield(L, -1, type);
	lua_getfield(L, -1, name);
	lua_replace(L, -3);
	lua_pop(L, 1);
}

// Callee and n arguments are on the stack above top. Returns the result as text: "nil(stale)", "error: ..." or a value.
const char* Call(Server& s, int top, int n, bool guarded, char* buf, size_t cb)
{
	lua_State* L = s.L;
	if(guarded && s.mask)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, s.playersRef);
		const int players = lua_gettop(L);
		SrvLuaStale::Miss m;
		const int nStale = SrvLuaStale::CheckArgs(L, top + 2, top + 1 + n, s.mts, s.mask, players, &m);
		lua_pop(L, 1);
		if(nStale)
		{
			++s.loud;
			printf("LOUD: stale %s instance %p (readable %d, owner %d, id %d, live %p) in argument %d: not called, nil "
			       "returned (%d stale)\n", SrvLuaStale::KindName(m.kind), m.p, m.readable ? 1 : 0, m.owner, m.id, m.live,
			       m.arg - top - 1, nStale);
			lua_settop(L, top);
			_snprintf_s(buf, cb, _TRUNCATE, "nil(stale %s readable=%d owner=%d id=%d)", SrvLuaStale::KindName(m.kind),
			            m.readable ? 1 : 0, m.owner, m.id);
			return buf;
		}
	}
	if(lua_pcall(L, n, 1, 0) != 0)
		_snprintf_s(buf, cb, _TRUNCATE, "error: %s", lua_tostring(L, -1));
	else if(lua_isnil(L, -1))
		_snprintf_s(buf, cb, _TRUNCATE, "nil");
	else
		_snprintf_s(buf, cb, _TRUNCATE, "%s", lua_tostring(L, -1));
	lua_settop(L, top);
	return buf;
}

// unit:GetPlot() through the guard: the instance table is the one the script kept (pushed while the unit was live).
const char* CallPlot(Server& s, int keptRef, const char* type, bool guarded, char* buf, size_t cb)
{
	const int top = lua_gettop(s.L);
	PushMethod(s.L, type, "GetPlot");
	lua_rawgeti(s.L, LUA_REGISTRYINDEX, keptRef);
	return Call(s, top, 1, guarded, buf, cb);
}

// kept:Method() through the guard, for any method of the type (the check does not depend on the method).
const char* CallMethod(Server& s, int keptRef, const char* type, const char* name, bool guarded, char* buf, size_t cb)
{
	const int top = lua_gettop(s.L);
	PushMethod(s.L, type, name);
	lua_rawgeti(s.L, LUA_REGISTRYINDEX, keptRef);
	return Call(s, top, 1, guarded, buf, cb);
}

int Keep(lua_State* L, const char* type, void* p)
{
	PushInstance(L, type, p);
	return luaL_ref(L, LUA_REGISTRYINDEX);
}

LONG WINAPI Unhandled(EXCEPTION_POINTERS* p)
{
	const DWORD code = p && p->ExceptionRecord ? p->ExceptionRecord->ExceptionCode : 0;
	printf("UNHANDLED 0x%08lX\n", (unsigned long)code);
	fflush(stdout);
	TerminateProcess(GetCurrentProcess(), code);
	return EXCEPTION_EXECUTE_HANDLER;
}

double NsPerCall(LARGE_INTEGER a, LARGE_INTEGER b, long n)
{
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	return 1e9 * (double)(b.QuadPart - a.QuadPart) / (double)f.QuadPart / (double)n;
}
}  // namespace

int main(int argc, char** argv)
{
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	SetUnhandledExceptionFilter(Unhandled);
	setvbuf(stdout, nullptr, _IONBF, 0);
	if(argc < 2)
	{
		printf("usage: stale_unit.exe <case>\n");
		return 2;
	}
	const char* c = argv[1];
	char buf[256];
	g_wrapped = !strcmp(c, "wrapped");
	Server s;
	s.L = NewState(strcmp(c, "nolearn") != 0);
	Learn(s);
	printf("RESULT mask %u\n", s.mask);
	printf("RESULT enabled %d\n", SrvLuaStale::Enabled() ? 1 : 0);

	FakeObj* u = NewObj(3, 8195, 12, 34);
	g_players[3].units[u->id] = u;
	FakeObj* city = NewObj(5, 16390, 40, 41);
	g_players[5].cities[city->id] = city;
	const int keptUnit = Keep(s.L, "Unit", u);
	const int keptCity = Keep(s.L, "City", city);

	if(!strcmp(c, "live") || !strcmp(c, "wrapped"))
	{
		printf("RESULT unit %s\n", CallPlot(s, keptUnit, "Unit", true, buf, sizeof(buf)));
		printf("RESULT city %s\n", CallPlot(s, keptCity, "City", true, buf, sizeof(buf)));
		// an instance in argument 2: unit:Distance(otherUnit), both live
		FakeObj* u2 = NewObj(3, 8196, 15, 30);
		g_players[3].units[u2->id] = u2;
		const int keptU2 = Keep(s.L, "Unit", u2);
		const int top = lua_gettop(s.L);
		PushMethod(s.L, "Unit", "Distance");
		lua_rawgeti(s.L, LUA_REGISTRYINDEX, keptUnit);
		lua_rawgeti(s.L, LUA_REGISTRYINDEX, keptU2);
		printf("RESULT distance %s\n", Call(s, top, 2, true, buf, sizeof(buf)));
	}
	else if(!strcmp(c, "freed") || !strcmp(c, "freed-unguarded") || !strcmp(c, "off-env"))
	{
		// the unit dies: out of its owner's list, memory released (unmapped); the script still holds its table
		g_players[3].units.erase(u->id);
		FreeObj(u);
		const bool guarded = strcmp(c, "freed-unguarded") != 0;
		if(argc > 2 && !strcmp(argv[2], "transport"))  // the unguarded negative through GetTransportUnit first
			printf("RESULT transport %s\n", CallMethod(s, keptUnit, "Unit", "GetTransportUnit", guarded, buf, sizeof(buf)));
		printf("RESULT calling\n");
		printf("RESULT unit %s\n", CallPlot(s, keptUnit, "Unit", guarded, buf, sizeof(buf)));
		// 0573's second crash (9a2f): the same freed unit through another method
		printf("RESULT transport %s\n", CallMethod(s, keptUnit, "Unit", "GetTransportUnit", guarded, buf, sizeof(buf)));
		printf("RESULT city %s\n", CallPlot(s, keptCity, "City", guarded, buf, sizeof(buf)));
		printf("RESULT citypop %s\n", CallMethod(s, keptCity, "City", "GetPopulation", guarded, buf, sizeof(buf)));
	}
	else if(!strcmp(c, "deleted") || !strcmp(c, "garbage"))
	{
		// out of the list, memory still mapped (freed heap that stays committed): readable, but not the live object
		g_players[3].units.erase(u->id);
		if(!strcmp(c, "garbage"))
			memset(u, 0xA5, sizeof(*u));  // reused by something else: owner 0xA5A5A5A5 out of range
		printf("RESULT unit %s\n", CallPlot(s, keptUnit, "Unit", true, buf, sizeof(buf)));
	}
	else if(!strcmp(c, "reused"))
	{
		// unit A dies; unit B is made at the same address with a new id: the kept table now stands for B (as in stock)
		g_players[3].units.erase(u->id);
		void* at = u;
		FreeObj(u);
		FakeObj* b = NewObj(3, 24579, 7, 8, at);
		printf("RESULT same address %d\n", (void*)b == at ? 1 : 0);
		g_players[3].units[b->id] = b;
		printf("RESULT unit %s\n", CallPlot(s, keptUnit, "Unit", true, buf, sizeof(buf)));
	}
	else if(!strcmp(c, "city-freed") || !strcmp(c, "city-deleted"))
	{
		g_players[5].cities.erase(city->id);
		if(!strcmp(c, "city-freed"))
			FreeObj(city);
		printf("RESULT city %s\n", CallPlot(s, keptCity, "City", true, buf, sizeof(buf)));
		printf("RESULT citypop %s\n", CallMethod(s, keptCity, "City", "GetPopulation", true, buf, sizeof(buf)));
		printf("RESULT unit %s\n", CallPlot(s, keptUnit, "Unit", true, buf, sizeof(buf)));
		printf("RESULT transport %s\n", CallMethod(s, keptUnit, "Unit", "GetTransportUnit", true, buf, sizeof(buf)));
	}
	else if(!strcmp(c, "others"))
	{
		// values that are not Unit or City instances are never checked: numbers, strings, nil, plain tables, a table
		// with a fake __instance but no metatable, a Plot instance, a type table; and a freed unit in argument 2
		// (the plot is made before the unit is freed: else it may get the freed region, and the unit reads as player
		// 0's id 1, which is stale too but not the unreadable case this checks)
		FakeObj* plot = NewObj(0, 1, 2, 3);
		FakeObj* u3 = NewObj(3, 8197, 1, 1);
		g_players[3].units[u3->id] = u3;
		g_players[3].units.erase(u->id);
		FreeObj(u);
		int top = lua_gettop(s.L);
		lua_pushcfunction(s.L, GameNumber);
		lua_pushnumber(s.L, 41);
		lua_pushstring(s.L, "x");
		lua_pushnil(s.L);
		lua_newtable(s.L);
		lua_createtable(s.L, 0, 1);
		lua_pushlightuserdata(s.L, (void*)0x10);
		lua_setfield(s.L, -2, "__instance");
		PushInstance(s.L, "Plot", plot);
		lua_getglobal(s.L, "LuaTypes");
		lua_getfield(s.L, -1, "Unit");
		lua_remove(s.L, -2);
		printf("RESULT others %s\n", Call(s, top, 7, true, buf, sizeof(buf)));
		SrvLuaStale::Counts k;
		SrvLuaStale::GetCounts(&k);
		printf("RESULT checked %lld %lld\n", k.checked[0], k.checked[1]);
		// a live city calling with the freed unit as argument 2
		top = lua_gettop(s.L);
		PushMethod(s.L, "Unit", "Distance");
		PushInstance(s.L, "Unit", u3);
		lua_rawgeti(s.L, LUA_REGISTRYINDEX, keptUnit);
		printf("RESULT arg2 %s\n", Call(s, top, 2, true, buf, sizeof(buf)));
	}
	else if(!strcmp(c, "nolearn"))
	{
		// no Player type: the guard covers nothing (mask 0) and calls pass as before
		printf("RESULT unit %s\n", CallPlot(s, keptUnit, "Unit", true, buf, sizeof(buf)));
	}
	else if(!strcmp(c, "stack"))
	{
		// CheckArgs leaves the stack as it was, live or stale
		g_players[5].cities.erase(city->id);
		FreeObj(city);
		lua_pushstring(s.L, "below");
		const int top = lua_gettop(s.L);
		lua_rawgeti(s.L, LUA_REGISTRYINDEX, keptUnit);
		lua_rawgeti(s.L, LUA_REGISTRYINDEX, keptCity);
		lua_rawgeti(s.L, LUA_REGISTRYINDEX, s.playersRef);
		SrvLuaStale::Miss m;
		const int before = lua_gettop(s.L);
		const int n = SrvLuaStale::CheckArgs(s.L, top + 1, top + 2, s.mts, s.mask, before, &m);
		printf("RESULT stale %d kind %s arg %d readable %d\n", n, SrvLuaStale::KindName(m.kind), m.arg - top, m.readable ? 1 : 0);
		printf("RESULT top unchanged %d\n", lua_gettop(s.L) == before ? 1 : 0);
		printf("RESULT below %s\n", lua_tostring(s.L, top));
	}
	else if(!strcmp(c, "bench"))
	{
		// cost per call: unit:GetPlot() on a live unit, unguarded and guarded; a call without instance arguments
		const long n = argc > 2 ? atol(argv[2]) : 1000000;
		for(int round = 0; round < 3; ++round)
		{
			double r[4];
			for(int arm = 0; arm < 4; ++arm)
			{
				const bool guarded = (arm & 1) != 0;
				LARGE_INTEGER a, b;
				QueryPerformanceCounter(&a);
				for(long i = 0; i < n; ++i)
				{
					const int top = lua_gettop(s.L);
					if(arm < 2)
					{
						PushMethod(s.L, "Unit", "GetPlot");
						lua_rawgeti(s.L, LUA_REGISTRYINDEX, keptUnit);
						Call(s, top, 1, guarded, buf, sizeof(buf));
					}
					else
					{
						lua_pushcfunction(s.L, GameNumber);
						lua_pushnumber(s.L, 1);
						Call(s, top, 1, guarded, buf, sizeof(buf));
					}
				}
				QueryPerformanceCounter(&b);
				r[arm] = NsPerCall(a, b, n);
			}
			printf("RESULT bench round %d: unit unguarded %.1f ns, guarded %.1f ns (+%.1f); no instance unguarded %.1f ns, "
			       "guarded %.1f ns (+%.1f)\n", round, r[0], r[1], r[1] - r[0], r[2], r[3], r[3] - r[2]);
		}
		SrvLuaStale::Counts k;
		SrvLuaStale::GetCounts(&k);
		LARGE_INTEGER f;
		QueryPerformanceFrequency(&f);
		printf("RESULT guard counters: %lld checks, %lld calls, mean %.1f ns in CheckArgs\n", k.checked[0], k.calls,
		       k.calls ? 1e9 * (double)k.ticks / (double)f.QuadPart / (double)k.calls : 0.0);
	}
	else
	{
		printf("unknown case %s\n", c);
		return 2;
	}
	SrvLuaStale::Counts k;
	SrvLuaStale::GetCounts(&k);
	printf("RESULT counts checked %lld/%lld stale %lld/%lld faulted %lld/%lld loud %ld\n", k.checked[0], k.checked[1],
	       k.stale[0], k.stale[1], k.faulted[0], k.faulted[1], s.loud);
	printf("RESULT final top %d\n", lua_gettop(s.L));
	printf("DONE\n");
	return 0;
}
