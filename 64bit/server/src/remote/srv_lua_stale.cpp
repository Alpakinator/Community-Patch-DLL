// srv_lua_stale.cpp - the stale-instance guard of the server's Lua bridge (ticket 0576). Design: srv_lua_stale.h and
// research/03-dll-engine-boundary/lua-stale-instance.md.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "srv_lua_stale.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>

namespace SrvLuaStale
{
namespace
{
	// The DLL's functions, per kind (process-wide: one DLL in the process).
	lua_CFunction volatile s_getId[kKinds] = {};
	lua_CFunction volatile s_getOwner[kKinds] = {};
	lua_CFunction volatile s_byId[kKinds] = {};
	const char* const kNames[kKinds] = {"Unit", "City"};
	const char* const kById[kKinds] = {"GetUnitByID", "GetCityByID"};

	volatile LONG s_lEnabled = -1;  // -1: not read yet
	volatile LONG64 s_checked[kKinds] = {}, s_stale[kKinds] = {}, s_faulted[kKinds] = {}, s_calls = 0, s_ticks = 0;

	// The owner range: MAX_PLAYERS is 64 at the pin (22 majors + 41 city states + barbarians; 43-civ builds keep the
	// same total); anything above 255 is certainly garbage.
	const int kMaxOwner = 256;

	// The C function of the function at idx: a plain C function, or the inner C function of a closure with exactly one
	// upvalue that is a C function without upvalues (upstream master's protectedWrapperAux, issue 0313).
	lua_CFunction CFunctionAt(lua_State* L, int idx)
	{
		if(!lua_iscfunction(L, idx))
			return nullptr;
		if(!lua_getupvalue(L, idx, 1))
			return lua_tocfunction(L, idx);
		lua_CFunction inner = nullptr;
		if(lua_iscfunction(L, -1))
		{
			inner = lua_tocfunction(L, -1);
			if(lua_getupvalue(L, -1, 1))
			{
				lua_pop(L, 1);
				inner = nullptr;
			}
		}
		lua_pop(L, 1);
		if(inner && lua_getupvalue(L, idx, 2))
		{
			lua_pop(L, 1);
			inner = nullptr;
		}
		return inner;
	}

	lua_CFunction FieldFunc(lua_State* L, int tableIdx, const char* name)
	{
		lua_getfield(L, tableIdx, name);
		const lua_CFunction f = CFunctionAt(L, -1);
		lua_pop(L, 1);
		return f;
	}

	int AvFilter(DWORD code)
	{
		return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER
		                                                                              : EXCEPTION_CONTINUE_SEARCH;
	}

	// One of the DLL's getters (GetID, GetOwner: GetInstance(L) at index 1, one field read, lua_pushinteger), called
	// directly in the probe's own frame. Returns false when it faulted: GetInstance has restored the stack before the
	// read, so nothing of Lua is half done; the frame is cut back to its base by the caller. No C++ object lives here.
	bool ProtectedGet(lua_CFunction f, lua_State* L, int* pValue)
	{
		__try
		{
			if(f(L) != 1)
				return false;
		}
		__except(AvFilter(GetExceptionCode()))
		{
			return false;
		}
		*pValue = (int)lua_tointeger(L, -1);
		return true;
	}

	struct ProbeArgs
	{
		int kind;
		const void* p;
		bool readable;
		int owner, id;
		const void* live;
	};

	// lua_pcall'd in a fresh frame: 1 = the instance table, 2 = the Players table, 3 = ProbeArgs (light userdata).
	int ProbeFrame(lua_State* L)
	{
		ProbeArgs* a = static_cast<ProbeArgs*>(lua_touserdata(L, 3));
		lua_settop(L, 2);
		const int k = a->kind;
		int id = -1, owner = -1;
		if(!ProtectedGet(s_getId[k], L, &id))
			return 0;
		lua_settop(L, 2);
		if(!ProtectedGet(s_getOwner[k], L, &owner))
			return 0;
		lua_settop(L, 2);
		a->readable = true;
		a->id = id;
		a->owner = owner;
		if(owner < 0 || owner >= kMaxOwner)
			return 0;
		lua_rawgeti(L, 2, owner);  // 3: Players[owner]
		if(!lua_istable(L, 3))
			return 0;
		// The lookup's frame: 1 = the player's instance table, 2 = the id (the DLL reads GetInstance(L) and index 2).
		lua_replace(L, 1);
		lua_pushinteger(L, id);
		lua_replace(L, 2);
		lua_settop(L, 2);
		if(s_byId[k](L) < 1 || lua_gettop(L) < 3 || !lua_istable(L, 3))
			return 0;
		lua_pushstring(L, "__instance");
		lua_rawget(L, 3);
		if(lua_type(L, -1) == LUA_TLIGHTUSERDATA)
			a->live = lua_touserdata(L, -1);
		return 0;
	}
}  // namespace

const char* KindName(int kind) { return kind >= 0 && kind < kKinds ? kNames[kind] : "?"; }

unsigned Learn(lua_State* L, int luaTypesIdx, const void* mts[kKinds])
{
	for(int k = 0; k < kKinds; ++k)
		mts[k] = nullptr;
	if(!lua_istable(L, luaTypesIdx))
		return 0;
	lua_getfield(L, luaTypesIdx, "Player");
	const int player = lua_gettop(L);
	unsigned mask = 0;
	for(int k = 0; k < kKinds; ++k)
	{
		lua_getfield(L, luaTypesIdx, kNames[k]);
		const int t = lua_gettop(L);
		if(lua_istable(L, t) && lua_istable(L, player))
		{
			const lua_CFunction gi = FieldFunc(L, t, "GetID"), go = FieldFunc(L, t, "GetOwner"),
			                    bi = FieldFunc(L, player, kById[k]);
			if(gi && go && bi)
			{
				s_getId[k] = gi;
				s_getOwner[k] = go;
				s_byId[k] = bi;
				mts[k] = lua_topointer(L, t);
				mask |= 1u << k;
			}
		}
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	return mask;
}

bool Enabled()
{
	LONG v = s_lEnabled;
	if(v < 0)
	{
		char buf[8] = {};
		const DWORD n = GetEnvironmentVariableA("VP64_LUA_STALE_GUARD", buf, sizeof(buf));
		v = (n > 0 && n < sizeof(buf) && buf[0] == '0' && buf[1] == 0) ? 0 : 1;
		InterlockedCompareExchange(&s_lEnabled, v, -1);
		v = s_lEnabled;
	}
	return v != 0;
}

void SetEnabled(bool on) { InterlockedExchange(&s_lEnabled, on ? 1 : 0); }

int CheckArgs(lua_State* L, int first, int last, const void* const mts[kKinds], unsigned mask, int players, Miss* miss)
{
	if(!mask || first > last || !Enabled())
		return 0;
	LARGE_INTEGER q0;
	q0.QuadPart = 0;
	int nStale = 0;
	bool bAny = false;
	for(int i = first; i <= last; ++i)
	{
		if(lua_type(L, i) != LUA_TTABLE || !lua_getmetatable(L, i))
			continue;
		const void* mt = lua_topointer(L, -1);
		lua_pop(L, 1);
		int k = 0;
		while(k < kKinds && !(mts[k] == mt && (mask & (1u << k))))
			++k;
		if(k == kKinds)
			continue;
		lua_pushstring(L, "__instance");
		lua_rawget(L, i);
		const void* p = lua_type(L, -1) == LUA_TLIGHTUSERDATA ? lua_touserdata(L, -1) : nullptr;
		lua_pop(L, 1);
		if(!p)
			continue;  // not an instance (a type table is never an argument here; GetInstance would refuse it anyway)
		if(!bAny)
		{
			bAny = true;
			QueryPerformanceCounter(&q0);
		}
		InterlockedIncrement64(&s_checked[k]);
		ProbeArgs a = {k, p, false, -1, -1, nullptr};
		bool bLive = false;
		if(lua_istable(L, players))
		{
			lua_checkstack(L, 8);
			lua_pushcfunction(L, ProbeFrame);
			lua_pushvalue(L, i);
			lua_pushvalue(L, players);
			lua_pushlightuserdata(L, &a);
			if(lua_pcall(L, 3, 0, 0) != 0)
				lua_pop(L, 1);	// an error inside the lookup: treated as stale (the object is not found)
			bLive = a.readable && a.live == p;
		}
		if(bLive)
			continue;
		InterlockedIncrement64(&s_stale[k]);
		if(!a.readable)
			InterlockedIncrement64(&s_faulted[k]);
		if(nStale++ == 0 && miss)
		{
			miss->kind = k;
			miss->arg = i;
			miss->p = p;
			miss->readable = a.readable;
			miss->owner = a.owner;
			miss->id = a.id;
			miss->live = a.live;
		}
	}
	if(bAny)
	{
		LARGE_INTEGER q1;
		QueryPerformanceCounter(&q1);
		InterlockedIncrement64(&s_calls);
		InterlockedExchangeAdd64(&s_ticks, q1.QuadPart - q0.QuadPart);
	}
	return nStale;
}

void GetCounts(Counts* c)
{
	for(int k = 0; k < kKinds; ++k)
	{
		c->checked[k] = s_checked[k];
		c->stale[k] = s_stale[k];
		c->faulted[k] = s_faulted[k];
	}
	c->calls = s_calls;
	c->ticks = s_ticks;
}
}  // namespace SrvLuaStale
