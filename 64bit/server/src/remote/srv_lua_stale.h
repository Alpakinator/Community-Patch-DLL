// srv_lua_stale.h - the stale-instance guard of the server's Lua bridge (ticket 0576, D185; research:
// research/03-dll-engine-boundary/lua-stale-instance.md).
//
// Why: a DLL instance table ({__instance = light userdata}) carries a raw CvUnit* / CvCity*. The interface Lua of the
// EXE may keep such a table after the DLL deleted the object; its next method call reached CvLuaScopedInstance::
// GetInstance, which returns the pointer unchecked, and the method read freed memory (0573's laptop crash e711: an
// access violation in CvUnit::getX inlined in CvLuaUnit::lGetPlot, server.exe, rt:LuaCall on the ui channel).
//
// What: before a kRt_LuaCall (and the engine's Get*Instance) runs a DLL function, every top-level argument that is a
// Unit or City instance table is resolved against the DLL's live object:
//   1. its identity (GetID, GetOwner) is read by the DLL's own Lua getters, called directly under a structured-exception
//      guard (they read one field each; an unmapped object faults there and nothing else is touched);
//   2. Players[owner]:GetUnitByID(id) (GetCityByID for a city), the DLL's own lookup in the owner's list, must return
//      the same pointer. Anything else (the read faulted, an owner out of range, no object, another object) is stale.
// A stale argument is never handed to the DLL; the caller returns nil and writes one LOUD line.
// The check never dereferences the pointer itself, and it changes nothing for a live instance (a freed object whose
// memory now holds another live object of the same kind at the same address is that object: as in stock).
//
// This file depends on Lua and Windows only (no server header), so server/tests/remote/stale_instance/ links it alone.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
extern "C" {
#include <lua.h>
}

namespace SrvLuaStale
{
enum Kind
{
	kUnit = 0,
	kCity = 1,
	kKinds = 2
};
const char* KindName(int kind);  // "Unit", "City"

// The first stale argument a check found. p is never dereferenced outside the protected getters.
struct Miss
{
	int kind = -1;			  // kUnit / kCity
	int arg = 0;			  // its absolute stack index
	const void* p = nullptr;  // the instance's pointer
	bool readable = false;	  // GetID and GetOwner returned (false: the read faulted, the object's memory is gone)
	int owner = -1, id = -1;  // as read (meaningful only when readable)
	const void* live = nullptr;  // what the owner's list holds for that id (nullptr: nothing)
};

// Learn the DLL functions from a state's LuaTypes table (absolute index luaTypesIdx): Unit.GetID, Unit.GetOwner,
// City.GetID, City.GetOwner, Player.GetUnitByID, Player.GetCityByID (a C closure of upstream master's protected wrapper
// gives its inner C function). The function pointers are process-wide (one DLL); mts receives this state's type tables
// LuaTypes.Unit and LuaTypes.City (lua_topointer; nullptr when missing), which are the metatables of its instance tables.
// Returns the mask of kinds the guard covers in this state (bit kind set: every function of that kind was found).
unsigned Learn(lua_State* L, int luaTypesIdx, const void* mts[kKinds]);

// On by default. VP64_LUA_STALE_GUARD=0 in the environment turns it off at the first call (test and measurement switch
// only, not an option of the shipped patch). SetEnabled is for tests.
bool Enabled();
void SetEnabled(bool on);

// Checks the values at absolute stack indices [first, last] of L. A value is checked when it is a table whose metatable
// is one of mts (the kinds of mask) and whose raw __instance is a light userdata. players: absolute index of the state's
// Players table. Returns the number of stale values (0: none), *miss = the first. L's stack is unchanged.
// Must run where the call it guards runs (same thread, same game-core lock), right before it.
int CheckArgs(lua_State* L, int first, int last, const void* const mts[kKinds], unsigned mask, int players, Miss* miss);

// Process-wide counters: values checked and stale per kind, stale ones whose identity read faulted, CheckArgs calls
// that checked at least one value and the time spent in them (QueryPerformanceCounter ticks).
struct Counts
{
	long long checked[kKinds];
	long long stale[kKinds];
	long long faulted[kKinds];
	long long calls;
	long long ticks;
};
void GetCounts(Counts* c);
}  // namespace SrvLuaStale
