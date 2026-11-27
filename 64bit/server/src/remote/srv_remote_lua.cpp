// srv_remote_lua.cpp - Lua in the server's remote mode (epic 2300, task 2303; proxy-design.md 11.5, D21 items 1-2).
//
// All game Lua runs in the engine's VM. The server keeps one scratch lua_State per service thread (lua51_x64.dll,
// luaL_openlibs, then the DLL's own RegisterScriptLibraries through the context's ICvScriptSystemUtility1), created
// when first needed, and runs there:
//   * kRt_LuaRegister: the globals RegisterScriptLibraries created or changed (diffed against a fresh state);
//   * kRt_LuaCall: a DLL C function (or a server-side closure, kLSRef) called in protected mode with decoded arguments;
//   * kRt_LuaDescribeType, kRt_LuaIndex, kRt_LuaRelease;
//   * the ServerHooks of ICvScriptSystemUtility1::Push/Get*Instance and PushReplayFromStream (contract D);
//   * the DLL's own Lua threads (map generation): CreateLuaThread makes the engine thread T (forwarded) and a scratch
//     thread S whose globals table has an __index that fetches T's globals (kRtE_LuaGetGlobal, never cached);
//     LoadFile / FreeLuaThread are forwarded for T; CallFunction / CallCFunction run locally on S (protected);
//     a chunk the DLL loads on S with luaL_loadstring (InitLuaFramework's VP_LUAAPI framework) runs on T in the engine
//     VM (kRtE_LuaRunChunk, issue 2504; the DLL's luaL_loadstring import slot points at HookLoadString).
//
// Value codec: proxy/include/vp_wire.h (LuaTag). Encoding rules specific to the server:
//   * an instance table (rawget __instance is a light userdata) -> kLInstance(type name, pointer); the type name is
//     the LuaTypes key of its metatable, else its hidden field __vptype (set on tables this side decoded);
//   * a LuaTypes type table (LuaTypes.City, the compat globals Player and Team, ...) is sent as an EMPTY kLTable:
//     the proxy builds the members of each type with kRt_LuaDescribeType (which skips __instances and __index);
//     so the global LuaTypes arrives as {Name = {}, ...};
//   * a C function without upvalues -> kLCFunc (registry of pointers, ids shared by every channel); since issue 0313
//     also a closure of upstream master's protectedWrapperAux (one upvalue: the DLL's C function), rebuilt on decoding;
//     any other C closure with upvalues, a Lua function or a full userdata -> kLSRef (luaL_ref in that state's
//     registry; the id carries the channel: ((channel + 1) << 24) | ref); an engine-value stub -> its kLERef;
//   * a table nested deeper than kLuaMaxDepth, a cycle, a value over kLuaMaxBytes, a thread or a light userdata
//     -> kLUnsupported (nil on arrival; logged once per kind, counted).
//   * issue 0314: upstream master's instance form (userdata of the registry's vpdllinternals types): a static (Game,
//     Map) -> kLInstance(type, 1), a live instance the DLL's cache confirms -> kLInstance(type, object) (EncodeMaster);
//     decoded back to the DLL's own userdata (DecodeMaster), found again by its identity on another channel's state.
// Decoding: kLInstance -> LuaTypes[name].__instances[pointer] when it exists, else a new {__instance = lightuserdata,
// __vptype = name} with metatable LuaTypes[name] (recorded in __instances when that exists); kLERef -> a C closure
// that calls kRtE_LuaCall on the calling thread's channel (nret LUA_MULTRET sent as 0xFFFFFFFF); its eref is released
// (kRtE_LuaRelease, batched) after the closure is collected.
#include "srv_remote.h"
#include "srv_lua_stale.h"	// ticket 0576
#include "srv_options.h"	// ticket 4804: [Performance] GameEventsOneCrossing
#include "vp_ids.gen.h"
#include "server_hooks.gen.h"
#include "vp_luaraise.h"	// ticket 4804: kRtE_LuaRaise's codec
#include "vpipc/stl.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <list>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

void SrvDbFlush();	// srv_remote_db.cpp (2504 follow-up: batched database writes)

namespace
{
	// ---- counters and the error list ----
	volatile long s_lErrors = 0, s_lUnsupported = 0, s_lBadRefs = 0, s_lUnknownThreads = 0, s_lLuaCalls = 0;
	CRITICAL_SECTION s_csLog;
	std::vector<std::string> s_errors;					// first 50 scratch Lua errors
	std::map<std::string, long> s_unsupportedKinds;
	struct InitCs
	{
		InitCs() { InitializeCriticalSection(&s_csLog); }
	} s_initCs;

	void CountError(const char* szWhere, const char* szMsg)
	{
		InterlockedIncrement(&s_lErrors);
		std::string line = std::string(szWhere) + ": " + (szMsg ? szMsg : "(non-string error)");
		SrvLog("remote: scratch Lua error: %s", line.c_str());
		EnterCriticalSection(&s_csLog);
		if(s_errors.size() < 50)
			s_errors.push_back(line);
		LeaveCriticalSection(&s_csLog);
	}

	void CountUnsupported(const char* szKind)
	{
		InterlockedIncrement(&s_lUnsupported);
		EnterCriticalSection(&s_csLog);
		const bool bFirst = ++s_unsupportedKinds[szKind] == 1;
		LeaveCriticalSection(&s_csLog);
		if(bFirst)
			SrvLog("remote: Lua codec: a %s cannot cross (sent as kLUnsupported, nil on arrival)", szKind);
	}

	// ---- C functions of the DLL: pointer <-> id (ids shared by every channel) ----
	// Issue 0313: upstream master (#13489, Lua/!CvLuaApi.h) redirects lua_pushcclosure to luaVp_pushcclosure, which
	// pushes every DLL C function f as the C closure protectedWrapperAux with one upvalue, f itself (a C function
	// without upvalues). Such a closure is rebuilt exactly in any Lua state from (wrapper, f), so it crosses as
	// kLCFunc like a plain C function at the pin: the id stands for the pair, and decoding pushes the closure again.
	// The wrapper's address is learnt from the DLL's registration (LearnWrapper); a closure of any other C function, or
	// with other upvalues (iterators such as Player:Cities(), luaVp_pushcclosure with a debug helper), stays kLSRef.
	struct FuncDesc
	{
		lua_CFunction f;	// the C function (for a wrapped one: upvalue 1 of the wrapper)
		lua_CFunction wrap;	// NULL for a plain C function; else the wrapper closure's C function
	};
	SRWLOCK s_lockF = SRWLOCK_INIT;
	std::vector<FuncDesc> s_funcs(1, FuncDesc());
	std::unordered_map<void*, uint32_t> s_funcIds;		// plain C functions
	std::unordered_map<void*, uint32_t> s_wrapIds;		// wrapped ones, by the wrapped function
	void* volatile s_wrapper = NULL;					// learnt by LearnWrapper (once per process; NULL at the pin)
	uintptr_t s_dllLo = 0, s_dllHi = 0;					// the DLL's image (SrvLuaInstall)
	thread_local bool t_trustWrapShape = false;			// issue 0315: CFuncIdAt, while a DLL thread's globals go out
	volatile long s_lWrapperLogged = 0;

	FuncDesc FuncDescOf(lua_CFunction f, lua_CFunction wrap)
	{
		FuncDesc d;
		d.f = f;
		d.wrap = wrap;
		return d;
	}

	uint32_t FuncId(lua_CFunction f, lua_CFunction wrap = NULL)
	{
		std::unordered_map<void*, uint32_t>& ids = wrap ? s_wrapIds : s_funcIds;
		AcquireSRWLockShared(&s_lockF);
		std::unordered_map<void*, uint32_t>::const_iterator it = ids.find((void*)f);
		uint32_t id = it != ids.end() ? it->second : 0;
		// Issue 0315: a wrapped function is one id per (function, wrapper); the same function in another wrapper has none.
		if(id && wrap && s_funcs[id].wrap != wrap)
			id = UINT32_MAX;
		ReleaseSRWLockShared(&s_lockF);
		if(id == UINT32_MAX)
			return 0;
		if(id)
			return id;
		AcquireSRWLockExclusive(&s_lockF);
		it = ids.find((void*)f);
		if(it != ids.end())
			id = wrap && s_funcs[it->second].wrap != wrap ? 0 : it->second;
		else
		{
			id = (uint32_t)s_funcs.size();
			s_funcs.push_back(FuncDescOf(f, wrap));
			ids[(void*)f] = id;
		}
		ReleaseSRWLockExclusive(&s_lockF);
		return id;
	}
	FuncDesc FuncOf(uint32_t id)
	{
		AcquireSRWLockShared(&s_lockF);
		const FuncDesc d = id < s_funcs.size() ? s_funcs[id] : FuncDesc();
		ReleaseSRWLockShared(&s_lockF);
		return d;
	}

	// Pushes the function of a kLCFunc id: the C function, or the wrapper closure around it (issue 0313).
	void PushFunc(lua_State* L, const FuncDesc& d)
	{
		lua_pushcfunction(L, d.f);
		if(d.wrap)
			lua_pushcclosure(L, d.wrap, 1);
	}

	bool InDll(const void* p) { return s_dllLo && (uintptr_t)p >= s_dllLo && (uintptr_t)p < s_dllHi; }

	// Issue 0313: the C function wrapped by the closure at idx when it has the wrapper's shape (exactly one upvalue, a
	// C function without upvalues); else NULL. Leaves the stack as it was. wrapper NULL: any C closure function.
	lua_CFunction WrappedFunc(lua_State* L, int idx, lua_CFunction wrapper)
	{
		if(!lua_iscfunction(L, idx) || (wrapper && lua_tocfunction(L, idx) != wrapper))
			return NULL;
		if(!lua_getupvalue(L, idx, 1))
			return NULL;
		lua_CFunction inner = NULL;
		if(lua_iscfunction(L, -1))
		{
			inner = lua_tocfunction(L, -1);
			if(lua_getupvalue(L, -1, 1))
			{
				lua_pop(L, 1);	// the inner function has upvalues of its own (an iterator, Fractal's methods)
				inner = NULL;
			}
		}
		lua_pop(L, 1);
		if(inner && lua_getupvalue(L, idx, 2))
		{
			lua_pop(L, 1);		// a second upvalue (luaVp_pushcclosure's debug helper and hint): not rebuilt here
			inner = NULL;
		}
		return inner;
	}

	// The kLCFunc id of the function at idx, or 0 when it does not cross as kLCFunc (a closure with upvalues other than
	// the learnt wrapper's, a Lua function).
	uint32_t CFuncIdAt(lua_State* L, int idx)
	{
		if(!lua_iscfunction(L, idx))
			return 0;
		if(!lua_getupvalue(L, idx, 1))
			return FuncId(lua_tocfunction(L, idx));
		lua_pop(L, 1);
		const lua_CFunction w = reinterpret_cast<lua_CFunction>(s_wrapper);
		if(!w)
		{
			// Issue 0315: before the wrapper is learnt (upstream master's NotifyDatabaseCached runs GamecoreInit.lua
			// before the first registration), a closure of the wrapper's shape whose two C functions lie in the DLL
			// is still rebuilt exactly from (function, closure function) where t_trustWrapShape is set: the globals
			// of a DLL thread sent to its engine thread (vputils). A server value there would work on this channel
			// only, and the engine copies it into every UI context.
			if(!t_trustWrapShape)
				return 0;
			const lua_CFunction outer = lua_tocfunction(L, idx);
			const lua_CFunction in0 = WrappedFunc(L, idx, NULL);
			return in0 && InDll((const void*)outer) && InDll((const void*)in0) ? FuncId(in0, outer) : 0;
		}
		const lua_CFunction inner = WrappedFunc(L, idx, w);
		return inner ? FuncId(inner, w) : 0;
	}

	// ---- issue 2805: kRt_LuaCall / kRt_LuaIndex per Lua function (always on) ----
	// Why: proxy-calls.tsv counts kRt_LuaCall as one row (4.9 M calls in the huge-save run live10/H1-on); the hybrid
	// UI-Lua mirror (D21 option C) needs them per binding. One cell per (function id, channel, phase), interlocked adds
	// only (about 4 per call against a ~1 us crossing). Names are the Lua paths the DLL registered the C function
	// under ("Player.IsAlive", "Game.GetActivePlayer"), first name wins, recorded once after RegisterScriptLibraries.
	// Phase is what the proxy saw before the call (bits 30-31 of the nargs word, proxy_lua.cpp LuaCallImpl):
	// 1 = the epoch mirror was open (nothing impure in flight: an idle UI read), 2 = an impure call was in flight (an
	// AI turn inside ICvGame1::Update, or the call is nested in another impure crossing: D30's limit), 0 = unknown
	// (local answers off, an older proxy, kRt_LuaIndex). Written to proxy-lua-calls.tsv beside proxy-calls.tsv.
	const uint32_t kLuaStatIds = 16384;
	const uint32_t kLuaStatSRef = 0;					// callee is a server-side closure / userdata (kLSRef), not a C function
	const uint32_t kLuaStatIndex = kLuaStatIds - 2;	// kRt_LuaIndex (index of a server-side value)
	const uint32_t kLuaStatOver = kLuaStatIds - 1;	// a function id beyond the table
	struct LuaStat
	{
		volatile LONG64 calls, ticks, bytesIn, bytesOut, would;
	};
	LuaStat s_luaStat[kLuaStatIds][3][3];				// [id][channel ui/core/aux][phase unknown/open/in flight]
	std::vector<std::string> s_funcNames;				// by function id, under s_lockF
	std::vector<uint32_t> s_funcAliases;				// further names of the same C function
	std::map<std::string, uint32_t> s_funcAllNames;		// every name -> function id (issue 0313: sent to the proxy)
	volatile long s_lNamed = 0;
	volatile long s_lNamesSent = 0;						// issue 0313: kLuaFuncNamesKey sent once per process
	// Issue 0313: the reserved name of the kRt_LuaRegister entry that carries the function names (proxy_lua.cpp has
	// the same literal; never a global of the DLL).
	const char* const kLuaFuncNamesKey = "__vpproxy_function_names";
	// Issue 0315: two more reserved entries of a non-empty kRt_LuaRegister reply (proxy_lua.cpp has the same literals):
	// the DLL's registry types in upstream master's form ({[i] = {tag = n, hint = s, name = s}}: the proxy mirrors them
	// into the engine registry's vpdllinternals, which master's GamecoreInit.lua reads), and the engine calls kept at the
	// registration ({[i] = {thread = n, name = s, n = number of arguments, args = {...}}}).
	const char* const kLuaDllTypesKey = "__vpproxy_dll_types";
	const char* const kLuaPostCallsKey = "__vpproxy_post_calls";

	void LuaStatAdd(uint32_t id, uint32_t chan, uint32_t phase, LONG64 ticks, uint32_t bytesIn, uint32_t bytesOut,
	                bool would = false)
	{
		if(id >= kLuaStatIds)
			id = kLuaStatOver;
		LuaStat& s = s_luaStat[id][chan < 3u ? chan : 2u][phase < 3u ? phase : 0u];
		InterlockedIncrement64(&s.calls);
		InterlockedExchangeAdd64(&s.ticks, ticks);
		InterlockedExchangeAdd64(&s.bytesIn, bytesIn);
		InterlockedExchangeAdd64(&s.bytesOut, bytesOut);
		if(would)
			InterlockedIncrement64(&s.would);
	}

	void NameFunc(uint32_t id, const std::string& name)
	{
		AcquireSRWLockExclusive(&s_lockF);
		if(s_funcNames.size() <= id)
		{
			s_funcNames.resize(id + 1);
			s_funcAliases.resize(id + 1, 0);
		}
		if(s_funcNames[id].empty())
			s_funcNames[id] = name;
		else if(s_funcNames[id] != name)
			++s_funcAliases[id];
		s_funcAllNames[name] = id;
		ReleaseSRWLockExclusive(&s_lockF);
	}

	// ---- engine values (kLERef) waiting to be released ----
	CRITICAL_SECTION s_csRel;
	std::vector<uint32_t> s_pendingRel;
	struct InitRel
	{
		InitRel() { InitializeCriticalSection(&s_csRel); }
	} s_initRel;

	// Sends the collected eref releases. Only at points where no outbound call is being built on this thread.
	void FlushReleases()
	{
		std::vector<uint32_t> ids;
		EnterCriticalSection(&s_csRel);
		ids.swap(s_pendingRel);
		LeaveCriticalSection(&s_csRel);
		if(ids.empty() || SrvRemote::ThreadChannel() < 0)
		{
			if(!ids.empty())
			{
				EnterCriticalSection(&s_csRel);
				s_pendingRel.insert(s_pendingRel.end(), ids.begin(), ids.end());
				LeaveCriticalSection(&s_csRel);
			}
			return;
		}
		vpipc::Channel& ch = SrvRt::Chan();
		vpipc::Writer& w = SrvRt::Begin(ch, vpw::kRtE_LuaRelease);
		w.u32((uint32_t)ids.size());
		for(size_t i = 0; i < ids.size(); ++i)
			w.u32(ids[i]);
		vpipc::Reader r;
		if(SrvRt::End(ch, vpw::kRtE_LuaRelease, r))
			SrvRt::DecodeCheck(vpw::kRtE_LuaRelease, r);
	}

	// ---- per service thread: the scratch state ----
	struct ScratchThread
	{
		lua_State* S;
		int anchor;			// registry ref keeping the thread alive
		uint32_t engineId;	// the engine thread T
		std::string name;
	};
	// Issue 0314: a type of upstream master's instance form (#13489, Lua/!CvLuaApiInstanceTemplates.h): the registry's
	// vpdllinternals[<tag>] = {__hint = TypeName(), __meta = the metatable of its userdata, [lightuserdata object] =
	// userdata, ...}. A static type (Game, Map) has one zero-size userdata, a global; a dynamic one (Player, City, Unit,
	// Plot, ...) one LuaTaggedAutoPointer userdata per object, cached in that table by the object's address.
	enum MasterResolve
	{
		kResNone = 0, kResUnit = 1, kResCity = 2, kResPlot = 3, kResKinds = 4
	};
	struct MasterType
	{
		std::string name;			// the Lua type name (the __hint; PlayerBase -> Player, PinTypeName)
		std::string hint;			// issue 0315: the __hint as the DLL wrote it (the engine-side vpdllinternals mirror)
		uint32_t tag = 0;			// the vpdllinternals key (luaEx_pushtag: the DWORD tag as a light userdata)
		int staticRef = LUA_NOREF;	// a static type: registry ref of its global userdata
		int resolve = kResNone;		// how a missing instance is found again (DecodeMaster)
	};
	// Issue 0315: an engine function the DLL called during its RegisterScriptLibraries on a scratch state (upstream
	// master's executeSupportEvent("gc_post"): _eventfunc of its thread !VPDLL, CvLuaSupport.cpp DoRegisterScriptData).
	// Kept by its engine thread, global name and scalar arguments; the proxy makes the call after every engine
	// registration of this channel, once that state has its globals (kLuaPostCallsKey).
	struct PostArg
	{
		int type = LUA_TNIL;	// LUA_TNIL, LUA_TBOOLEAN, LUA_TNUMBER or LUA_TSTRING
		double num = 0;
		std::string str;
	};
	struct PostCall
	{
		uint32_t thread = 0;	// the engine Lua thread id (CreateLuaThread)
		std::string name;		// the global of that thread the DLL fetched
		std::vector<PostArg> args;
	};
	struct LuaCtx
	{
		lua_State* L = NULL;
		uint32_t chan = 0;
		int globalsRef = LUA_NOREF;	// the main state's globals table (S threads have their own)
		bool registered = false;
		std::map<const void*, std::string> typeNames;	// LuaTypes[name] table -> name
		std::map<lua_State*, ScratchThread> threads;
		// Issue 0314: master's types by their __meta table and by name (empty at the pin); learnt after registration.
		std::map<const void*, MasterType> masterByMt;
		std::map<std::string, const void*> masterByName;
		bool masterLearnt = false;
		// Ticket 0576: the stale-instance guard (srv_lua_stale.h): this state's LuaTypes.Unit / City tables, the kinds
		// it covers here (0 until RebuildTypeNames learnt them) and a registry ref of the Players table.
		const void* staleMts[SrvLuaStale::kKinds] = {};
		unsigned staleMask = 0;
		int playersRef = LUA_NOREF;
		std::vector<PostCall> postCalls;	// issue 0315: kept at this state's registration, sent with every reply
	};
	thread_local LuaCtx* t_ctx = NULL;
	// Issue 0315: the state whose RegisterScriptLibraries the DLL is running on this service thread (RegisteredCtx), or
	// NULL. While it is set, a function the DLL fetches from an engine thread's globals is deferred (GlobalIndex).
	thread_local LuaCtx* t_collect = NULL;
	volatile long s_lPostCallsKept = 0, s_lPostCallsSent = 0, s_lPostCallsNow = 0;

	const char* const kERefMeta = "vp.eref";
	struct ERef
	{
		uint32_t id;
		uint32_t type;
		uint32_t thread;	// engine Lua thread id the value came from (0: unknown)
	};

	int ERefGc(lua_State* L)
	{
		ERef* e = static_cast<ERef*>(lua_touserdata(L, 1));
		if(e && e->id)
		{
			EnterCriticalSection(&s_csRel);
			s_pendingRel.push_back(e->id);
			LeaveCriticalSection(&s_csRel);
			e->id = 0;
		}
		return 0;
	}

	int RegisterTramp(lua_State* L)
	{
		ICvScriptSystemUtility1* u = static_cast<ICvScriptSystemUtility1*>(lua_touserdata(L, 1));
		lua_settop(L, 0);
		u->RegisterScriptLibraries(L);
		return 0;
	}

	void PushMainGlobal(lua_State* L, LuaCtx* ctx, const char* szName)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->globalsRef);
		lua_getfield(L, -1, szName);
		lua_remove(L, -2);
	}

	void RebuildTypeNames(LuaCtx* ctx)
	{
		lua_State* L = ctx->L;
		ctx->typeNames.clear();
		PushMainGlobal(L, ctx, "LuaTypes");
		if(lua_istable(L, -1))
		{
			lua_pushnil(L);
			while(lua_next(L, -2))
			{
				if(lua_type(L, -2) == LUA_TSTRING && lua_istable(L, -1))
					ctx->typeNames[lua_topointer(L, -1)] = lua_tostring(L, -2);
				lua_pop(L, 1);
			}
		}
		// Ticket 0576: the guard's functions and this state's Unit / City type tables (the instances' metatables).
		const unsigned before = ctx->staleMask;
		ctx->staleMask = SrvLuaStale::Learn(L, lua_gettop(L), ctx->staleMts);
		if(ctx->staleMask && ctx->playersRef == LUA_NOREF)
		{
			PushMainGlobal(L, ctx, "Players");
			if(lua_istable(L, -1))
				ctx->playersRef = luaL_ref(L, LUA_REGISTRYINDEX);
			else
				lua_pop(L, 1);
		}
		if(ctx->playersRef == LUA_NOREF)
			ctx->staleMask = 0;
		if(ctx->staleMask != before)
			SrvLog("remote: Lua stale-instance guard (ticket 0576) on channel %u: Unit %s, City %s%s", ctx->chan,
			       (ctx->staleMask & 1u) ? "checked" : "NOT checked", (ctx->staleMask & 2u) ? "checked" : "NOT checked",
			       SrvLuaStale::Enabled() ? "" : " (VP64_LUA_STALE_GUARD=0: off)");
		lua_pop(L, 1);
	}

	// Issue 2805: names the C functions (the ones that cross as kLCFunc: without upvalues, or since issue 0313 the
	// closures of master's wrapper) of the table at the top of L as "<prefix>.<key>". Raw iteration only (no metamethod
	// runs).
	void NameTableFuncs(lua_State* L, const std::string& prefix)
	{
		const int t = lua_gettop(L);
		lua_pushnil(L);
		while(lua_next(L, t))
		{
			if(lua_type(L, -2) == LUA_TSTRING && lua_iscfunction(L, -1))
			{
				const uint32_t id = CFuncIdAt(L, lua_gettop(L));
				if(id)
					NameFunc(id, prefix + "." + lua_tostring(L, -2));
			}
			lua_pop(L, 1);
		}
	}

	// Issue 0313: the function table of a value whose metatable's __index is a table (master's Game and Map: zero-size
	// full userdata, CvLuaStaticInstance::RegisterGlobals); pushes it and returns true, else pushes nothing.
	bool PushMetaIndexTable(lua_State* L, int idx)
	{
		if(!lua_getmetatable(L, idx))
			return false;
		lua_pushstring(L, "__index");
		lua_rawget(L, -2);
		lua_remove(L, -2);
		if(lua_istable(L, -1))
			return true;
		lua_pop(L, 1);
		return false;
	}

	// Issue 0313: master's type name of a vpdllinternals entry (its __hint, TypeGlobals<T>::TypeName()) as the pin's
	// LuaTypes key: CvPlayer's TypeName is "PlayerBase" and shares the tag PLYR with CvPlayerAI's "Player" (the last
	// registered wins: CvPlayer, CvLuaSupport.cpp DoRegisterScriptData); Lua code knows the type as Player.
	std::string PinTypeName(const std::string& hint) { return hint == "PlayerBase" ? std::string("Player") : hint; }

	// Issue 2805: once per process, after the DLL's RegisterScriptLibraries: LuaTypes.<Type>.<key> first (named
	// "<Type>.<key>"), then (issue 0313, master) the function tables of the registry's vpdllinternals[<tag>].__meta
	// (named "<__hint>.<key>"), then every other global table one level deep ("Game.GetActivePlayer"), the function
	// table of a global whose metatable's __index is a table (master's Game, Map) and global functions.
	void NameFuncs(LuaCtx* ctx)
	{
		if(InterlockedExchange(&s_lNamed, 1))
			return;
		lua_State* L = ctx->L;
		const int top = lua_gettop(L);
		lua_checkstack(L, 16);
		PushMainGlobal(L, ctx, "LuaTypes");
		if(lua_istable(L, -1))
		{
			const int lt = lua_gettop(L);
			lua_pushnil(L);
			while(lua_next(L, lt))
			{
				if(lua_type(L, -2) == LUA_TSTRING && lua_istable(L, -1))
				{
					const std::string type = lua_tostring(L, -2);
					NameTableFuncs(L, type);
				}
				lua_pop(L, 1);
			}
		}
		lua_settop(L, top);
		lua_getfield(L, LUA_REGISTRYINDEX, "vpdllinternals");
		if(lua_istable(L, -1))
		{
			const int vi = lua_gettop(L);
			lua_pushnil(L);
			while(lua_next(L, vi))
			{
				if(lua_istable(L, -1))
				{
					const int e = lua_gettop(L);
					lua_getfield(L, e, "__hint");
					const std::string hint = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "";
					lua_pop(L, 1);
					lua_getfield(L, e, "__meta");
					if(!hint.empty() && lua_istable(L, -1))
					{
						lua_getfield(L, -1, "__index");
						if(lua_istable(L, -1))
							NameTableFuncs(L, PinTypeName(hint));
					}
					lua_settop(L, e);
				}
				lua_pop(L, 1);
			}
		}
		lua_settop(L, top);
		lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->globalsRef);
		const int g = lua_gettop(L);
		lua_pushnil(L);
		while(lua_next(L, g))
		{
			if(lua_type(L, -2) == LUA_TSTRING)
			{
				const std::string k = lua_tostring(L, -2);
				const int v = lua_gettop(L);
				if(lua_istable(L, v) && k != "LuaTypes" && k != "_G" && k != "package")
					NameTableFuncs(L, k);
				else if(lua_isuserdata(L, v) && !lua_islightuserdata(L, v) && PushMetaIndexTable(L, v))
					NameTableFuncs(L, k);
				else if(lua_iscfunction(L, v))
				{
					const uint32_t id = CFuncIdAt(L, v);
					if(id)
						NameFunc(id, k);
				}
				lua_settop(L, v);
			}
			lua_pop(L, 1);
		}
		lua_settop(L, top);
		AcquireSRWLockShared(&s_lockF);
		const size_t n = s_funcNames.size();
		size_t named = 0;
		for(size_t i = 0; i < n; ++i)
			named += s_funcNames[i].empty() ? 0 : 1;
		ReleaseSRWLockShared(&s_lockF);
		SrvLog("remote: Lua function names recorded for the per-function counters (issue 2805): ids up to %zu, %zu named",
		       n, named);
	}

	// Issue 0313: learns master's protectedWrapperAux from what RegisterScriptLibraries registered: among the C closures
	// with the wrapper's shape (WrappedFunc) whose C function and wrapped function both lie in the DLL's image, the C
	// function most of them share, when at least kWrapMin share it. Looked at: global functions, the functions of global
	// tables and of the __index table of a global's metatable (Game, Map), and the registry's
	// vpdllinternals[<tag>].__meta.__index tables. At the pin no DLL function is such a closure and nothing is learnt.
	// Each channel's state looks once until one finds it; the result is the same for every state of one DLL.
	const int kWrapMin = 8;
	void CountWrapped(lua_State* L, int t, std::map<lua_CFunction, int>& n)
	{
		lua_pushnil(L);
		while(lua_next(L, t))
		{
			const int v = lua_gettop(L);
			if(lua_iscfunction(L, v) && InDll((const void*)lua_tocfunction(L, v)))
			{
				const lua_CFunction inner = WrappedFunc(L, v, NULL);
				if(inner && InDll((const void*)inner))
					++n[lua_tocfunction(L, v)];
			}
			lua_settop(L, v - 1);
		}
	}
	void LearnWrapper(LuaCtx* ctx)
	{
		if(s_wrapper)
			return;
		lua_State* L = ctx->L;
		const int top = lua_gettop(L);
		lua_checkstack(L, 16);
		std::map<lua_CFunction, int> n;
		lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->globalsRef);
		const int g = lua_gettop(L);
		CountWrapped(L, g, n);
		lua_pushnil(L);
		while(lua_next(L, g))
		{
			const int v = lua_gettop(L);
			if(lua_type(L, v - 1) == LUA_TSTRING && strcmp(lua_tostring(L, v - 1), "_G") &&
			   strcmp(lua_tostring(L, v - 1), "package"))
			{
				if(lua_istable(L, v))
					CountWrapped(L, v, n);
				else if(lua_isuserdata(L, v) && !lua_islightuserdata(L, v) && PushMetaIndexTable(L, v))
					CountWrapped(L, lua_gettop(L), n);
			}
			lua_settop(L, v - 1);
		}
		lua_settop(L, top);
		lua_getfield(L, LUA_REGISTRYINDEX, "vpdllinternals");
		if(lua_istable(L, -1))
		{
			const int vi = lua_gettop(L);
			lua_pushnil(L);
			while(lua_next(L, vi))
			{
				const int e = lua_gettop(L);
				if(lua_istable(L, e))
				{
					lua_getfield(L, e, "__meta");
					if(lua_istable(L, -1))
					{
						lua_getfield(L, -1, "__index");
						if(lua_istable(L, -1))
							CountWrapped(L, lua_gettop(L), n);
					}
				}
				lua_settop(L, e - 1);
			}
		}
		lua_settop(L, top);
		lua_CFunction best = NULL;
		int bestN = 0, total = 0;
		for(std::map<lua_CFunction, int>::const_iterator it = n.begin(); it != n.end(); ++it)
		{
			total += it->second;
			if(it->second > bestN)
			{
				best = it->first;
				bestN = it->second;
			}
		}
		if(best && bestN >= kWrapMin)
		{
			if(!InterlockedCompareExchangePointer(&s_wrapper, (void*)best, NULL))
			{
				SrvLog("remote: Lua: %d DLL functions are C closures of one wrapper at DLL+0x%llx (upstream master's "
				       "protectedWrapperAux, #13489; %d closures of that shape in all): they cross as kLCFunc of the "
				       "wrapped function (issue 0313)", bestN, (unsigned long long)((uintptr_t)best - s_dllLo), total);
				SrvRemote::Report("lua: DLL functions wrapped in one C closure (upstream master's protectedWrapperAux): "
				                  "%d, crossing as kLCFunc (issue 0313)", bestN);
			}
		}
		else if(!InterlockedExchange(&s_lWrapperLogged, 1))
			SrvLog("remote: Lua: no wrapper closure in the DLL's registration (%d closures of that shape, at most %d of "
			       "one C function; the pin's form): C closures with upvalues cross as kLSRef (issue 0313)", total, bestN);
	}

	// ---- issue 0314: upstream master's instance form ----
	// The userdata of a dynamic instance: LuaTaggedAutoPointer {DWORD tag; AutoInvalidatedPointerBase ptr;} with the
	// base {void* obj; prev; next} (CvGameCoreDLLUtil/include/CvStructs.h at 76b3a5f70): 32 bytes on x64, obj at 8.
	// The layout is never trusted alone: an instance is sent as (type, obj) only when the DLL's own cache
	// vpdllinternals[tag][obj] holds this very userdata (EncodeMaster), else it stays a server value (kLSRef).
	struct MasterUd
	{
		uint32_t tag;
		void* obj;
		void* prev;
		void* next;
	};
	const uint64_t kMasterStaticPtr = 1;	// the "pointer" of a static instance (Game, Map) in kLInstance
	const char* const kFormKey = "__vpproxy_form";	// the describe reply's first member (proxy_lua.cpp: the same literal)
	volatile long s_lMasterEnc = 0, s_lMasterStaticEnc = 0, s_lMasterNotCached = 0, s_lMasterDec = 0, s_lMasterMiss = 0,
	              s_lMasterResolved = 0, s_lMasterDead = 0, s_lMasterLost = 0, s_lMasterLearnt = 0;
	// The DLL's getters a missing instance is identified with ([kind][0]: GetID / GetX, [kind][1]: GetOwner / GetY).
	lua_CFunction volatile s_resGet[kResKinds][2] = {};

	void PushInternalsType(lua_State* L, uint32_t tag)	// vpdllinternals[tag] or nil
	{
		lua_getfield(L, LUA_REGISTRYINDEX, "vpdllinternals");
		if(!lua_istable(L, -1))
			return;
		lua_pushlightuserdata(L, (void*)(uintptr_t)tag);
		lua_rawget(L, -2);
		lua_remove(L, -2);
	}

	// The C function the DLL registered at idx: a plain C function, or the one inside master's wrapper closure.
	lua_CFunction InnerCFunc(lua_State* L, int idx)
	{
		if(!lua_iscfunction(L, idx))
			return NULL;
		if(!lua_getupvalue(L, idx, 1))
			return lua_tocfunction(L, idx);
		lua_pop(L, 1);
		return WrappedFunc(L, idx, NULL);
	}

	// A __meta table this bridge can rebuild: __index a table, no metamethod but __index, __gc and __metatable.
	bool MasterMetaSupported(lua_State* L, int mt)
	{
		lua_pushstring(L, "__index");
		lua_rawget(L, mt);
		const bool ok = lua_istable(L, -1);
		lua_pop(L, 1);
		if(!ok)
			return false;
		bool other = false;
		lua_pushnil(L);
		while(lua_next(L, mt))
		{
			if(lua_type(L, -2) != LUA_TSTRING || (strcmp(lua_tostring(L, -2), "__index") &&
			   strcmp(lua_tostring(L, -2), "__gc") && strcmp(lua_tostring(L, -2), "__metatable")))
				other = true;
			lua_pop(L, 1);
		}
		return !other;
	}

	int ResolveKindOf(const std::string& name)
	{
		return name == "Unit" ? kResUnit : name == "City" ? kResCity : name == "Plot" ? kResPlot : kResNone;
	}

	// Learns master's types of this state once (after the DLL's RegisterScriptLibraries): every vpdllinternals entry
	// with a __hint and a supported __meta, and the statics (global zero-size userdata with such a metatable). At the
	// pin there is no vpdllinternals and nothing is learnt.
	void LearnMasterTypes(LuaCtx* ctx)
	{
		if(ctx->masterLearnt)
			return;
		lua_State* L = ctx->L;
		const int top = lua_gettop(L);
		lua_checkstack(L, 16);
		lua_getfield(L, LUA_REGISTRYINDEX, "vpdllinternals");
		if(!lua_istable(L, -1))
		{
			lua_settop(L, top);
			return;
		}
		const int vi = lua_gettop(L);
		lua_pushnil(L);
		while(lua_next(L, vi))
		{
			const int e = lua_gettop(L);
			if(lua_type(L, e - 1) == LUA_TLIGHTUSERDATA && lua_istable(L, e))
			{
				const uint32_t tag = (uint32_t)(uintptr_t)lua_touserdata(L, e - 1);
				lua_getfield(L, e, "__hint");
				const std::string hint = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "";
				lua_pop(L, 1);
				lua_getfield(L, e, "__meta");
				if(!hint.empty() && lua_istable(L, -1) && MasterMetaSupported(L, lua_gettop(L)))
				{
					MasterType t;
					t.name = PinTypeName(hint);
					t.hint = hint;
					t.tag = tag;
					t.resolve = ResolveKindOf(t.name);
					const void* mt = lua_topointer(L, -1);
					ctx->masterByMt[mt] = t;
					ctx->masterByName[t.name] = mt;
					// the getters that identify a missing instance (the same functions in every state of one DLL)
					if(t.resolve && !s_resGet[t.resolve][0])
					{
						lua_getfield(L, -1, "__index");
						const char* a = t.resolve == kResPlot ? "GetX" : "GetID";
						const char* b = t.resolve == kResPlot ? "GetY" : "GetOwner";
						lua_getfield(L, -1, a);
						const lua_CFunction fa = InnerCFunc(L, lua_gettop(L));
						lua_getfield(L, -2, b);
						const lua_CFunction fb = InnerCFunc(L, lua_gettop(L));
						if(fa && fb)
						{
							s_resGet[t.resolve][1] = fb;
							s_resGet[t.resolve][0] = fa;
						}
					}
				}
				else if(!hint.empty())
					SrvLog("remote: Lua: master type %s (tag 0x%08x) has a metatable this bridge does not rebuild: its "
					       "values stay server values (issue 0314)", hint.c_str(), tag);
				lua_settop(L, e);
			}
			lua_pop(L, 1);
		}
		lua_settop(L, top);
		size_t nStatic = 0;
		lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->globalsRef);
		const int g = lua_gettop(L);
		lua_pushnil(L);
		while(lua_next(L, g))
		{
			const int v = lua_gettop(L);
			if(lua_type(L, v) == LUA_TUSERDATA && lua_objlen(L, v) == 0 && lua_getmetatable(L, v))
			{
				std::map<const void*, MasterType>::iterator it = ctx->masterByMt.find(lua_topointer(L, -1));
				lua_pop(L, 1);
				if(it != ctx->masterByMt.end() && it->second.staticRef == LUA_NOREF)
				{
					lua_pushvalue(L, v);
					it->second.staticRef = luaL_ref(L, LUA_REGISTRYINDEX);
					++nStatic;
				}
			}
			lua_settop(L, v - 1);
		}
		lua_settop(L, top);
		ctx->masterLearnt = !ctx->masterByMt.empty();
		if(ctx->masterLearnt && InterlockedIncrement(&s_lMasterLearnt) <= 8)
			SrvLog("remote: Lua: %zu types of upstream master's instance form (%zu static) on channel %u: instances cross "
			       "as kLInstance(type, object), statics with pointer 1; resolvers Unit %s, City %s, Plot %s (issue 0314)",
			       ctx->masterByMt.size(), nStatic, ctx->chan, s_resGet[kResUnit][0] ? "yes" : "no",
			       s_resGet[kResCity][0] ? "yes" : "no", s_resGet[kResPlot][0] ? "yes" : "no");
	}

	LuaCtx* Ctx()
	{
		if(t_ctx)
			return t_ctx;
		LuaCtx* ctx = new LuaCtx();
		const int iChan = SrvRemote::ThreadChannel();
		if(iChan < 0)
			SrvLog("remote: LOUD: a scratch Lua state for thread %lu, which serves no channel", GetCurrentThreadId());
		ctx->chan = iChan < 0 ? 15u : (uint32_t)iChan;
		ctx->L = luaL_newstate();
		luaL_openlibs(ctx->L);
		lua_pushvalue(ctx->L, LUA_GLOBALSINDEX);
		ctx->globalsRef = luaL_ref(ctx->L, LUA_REGISTRYINDEX);
		luaL_newmetatable(ctx->L, kERefMeta);
		lua_pushcfunction(ctx->L, ERefGc);
		lua_setfield(ctx->L, -2, "__gc");
		lua_pop(ctx->L, 1);
		t_ctx = ctx;
		SrvLog("remote: scratch Lua state %p for channel %u", (void*)ctx->L, ctx->chan);
		return ctx;
	}

	// The number of keys of the scratch main state's globals table (raw iteration).
	int CountGlobals(LuaCtx* ctx)
	{
		lua_State* L = ctx->L;
		int n = 0;
		lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->globalsRef);
		const int g = lua_gettop(L);
		lua_pushnil(L);
		while(lua_next(L, g))
		{
			++n;
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
		return n;
	}

	volatile long s_lEmptyRegistrations = 0;	// issue 0308: RegisterScriptLibraries calls that registered no global

	// The DLL's RegisterScriptLibraries on this thread's scratch state (once it has registered something).
	// Issue 0308: upstream master's LuaSupport::NotifyDatabaseCached (CvLuaSupport.cpp, after #13489) sets
	// skipGamecoreCreation around CreateLuaThread("!VPDLL"), and while it is set RegisterScriptData registers nothing.
	// That CreateLuaThread is forwarded to the engine, whose nested RegisterScriptLibraries reaches this function as
	// the service thread's first kRt_LuaRegister; marking the state registered then kept it empty for every later
	// kRt_LuaRegister (Game, Map, Players nil in every host state). So a call that added no global leaves the state
	// unregistered and the next call registers again. At the pin every call registers (no such flag), so the first
	// call marks the state as before. `registered` is set during the call so that a nested request returns at once.
	LuaCtx* RegisteredCtx()
	{
		LuaCtx* ctx = Ctx();
		if(ctx->registered)
			return ctx;
		ctx->registered = true;
		ICvGameContext1* pContext = SrvRemote::Context();
		ICvScriptSystemUtility1* u = pContext ? pContext->GetScriptSystemUtility() : NULL;
		if(!u)
		{
			CountError("RegisterScriptLibraries", "no game context yet (kRt_Boot not received)");
			return ctx;
		}
		lua_State* L = ctx->L;
		const int before = CountGlobals(ctx);
		// Issue 0315: calls of engine functions the DLL makes during the registration are kept (GlobalIndex).
		ctx->postCalls.clear();
		LuaCtx* const outer = t_collect;
		t_collect = ctx;
		const int rc = lua_cpcall(L, RegisterTramp, u);
		t_collect = outer;
		if(rc != 0)
		{
			CountError("RegisterScriptLibraries", lua_tostring(L, -1));
			lua_pop(L, 1);
		}
		else if(CountGlobals(ctx) == before)
		{
			ctx->postCalls.clear();
			ctx->registered = false;
			const long n = InterlockedIncrement(&s_lEmptyRegistrations);
			if(n <= 20)
				SrvLog("remote: RegisterScriptLibraries on the scratch state of channel %u registered no global (the "
				       "DLL skipped it, as master's NotifyDatabaseCached does around CreateLuaThread); the state stays "
				       "unregistered and the next request registers again (issue 0308)", ctx->chan);
			return ctx;
		}
		RebuildTypeNames(ctx);
		LearnMasterTypes(ctx);  // issue 0314: before any value of this state is encoded
		LearnWrapper(ctx);  // issue 0313: before any value of this state is encoded, and before the names
		NameFuncs(ctx);  // issue 2805
		for(size_t i = 0; i < ctx->postCalls.size(); ++i)
			SrvLog("remote: the DLL called the engine function %s of engine thread %u (%zu arguments) during "
			       "RegisterScriptLibraries on channel %u: the proxy makes that call after each engine state's "
			       "registration on this channel, once the state has its globals (issue 0315)",
			       ctx->postCalls[i].name.c_str(), ctx->postCalls[i].thread, ctx->postCalls[i].args.size(), ctx->chan);
		return ctx;
	}

	// ---- encoding ----
	struct Enc
	{
		LuaCtx* ctx;
		std::vector<const void*> path;	// tables being encoded (cycle check)
	};

	uint32_t SRefId(LuaCtx* ctx, int ref) { return ((ctx->chan + 1u) << 24) | ((uint32_t)ref & 0x00FFFFFFu); }

	void WriteUnsupported(vpipc::Writer& w, const char* szKind)
	{
		CountUnsupported(szKind);
		w.u32(vpw::kLUnsupported);
		w.str(szKind);
	}

	int AbsIndex(lua_State* L, int idx) { return idx > 0 || idx <= LUA_REGISTRYINDEX ? idx : lua_gettop(L) + idx + 1; }

	// Name of an instance table's type: the LuaTypes key of its metatable, else its __vptype field.
	bool InstanceTypeName(lua_State* L, int idx, LuaCtx* ctx, std::string& name)
	{
		if(lua_getmetatable(L, idx))
		{
			const void* mt = lua_topointer(L, -1);
			lua_pop(L, 1);
			std::map<const void*, std::string>::const_iterator it = ctx->typeNames.find(mt);
			if(it == ctx->typeNames.end())
			{
				RebuildTypeNames(ctx);
				it = ctx->typeNames.find(mt);
			}
			if(it != ctx->typeNames.end())
			{
				name = it->second;
				return true;
			}
		}
		lua_getfield(L, idx, "__vptype");
		const bool ok = lua_type(L, -1) == LUA_TSTRING;
		if(ok)
			name = lua_tostring(L, -1);
		lua_pop(L, 1);
		return ok;
	}

	void EncodeValue(lua_State* L, int idx, vpipc::Writer& w, Enc& e, uint32_t depth);

	void EncodeSRef(lua_State* L, int idx, vpipc::Writer& w, Enc& e, int type)
	{
		lua_pushvalue(L, idx);
		const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
		w.u32(vpw::kLSRef);
		w.u32(SRefId(e.ctx, ref));
		w.u32((uint32_t)type);
	}

	// Issue 0314: a userdata of master's instance form as kLInstance: a static as (type, 1), a live dynamic instance as
	// (type, object) when the DLL's own cache holds this userdata for that object. Returns false (nothing written) for
	// anything else, which then crosses as kLSRef as before: another userdata, an instance the DLL invalidated (its
	// object was deleted: obj NULL), or one the cache does not confirm.
	bool EncodeMaster(lua_State* L, int idx, vpipc::Writer& w, LuaCtx* ctx)
	{
		if(ctx->masterByMt.empty() || !lua_getmetatable(L, idx))
			return false;
		std::map<const void*, MasterType>::const_iterator it = ctx->masterByMt.find(lua_topointer(L, -1));
		lua_pop(L, 1);
		if(it == ctx->masterByMt.end())
			return false;
		const MasterType& t = it->second;
		const size_t len = lua_objlen(L, idx);
		if(len == 0)
		{
			if(t.staticRef == LUA_NOREF)
				return false;
			w.u32(vpw::kLInstance);
			vpipc::write_string(w, t.name);
			w.u64(kMasterStaticPtr);
			InterlockedIncrement(&s_lMasterStaticEnc);
			return true;
		}
		const MasterUd* u = static_cast<const MasterUd*>(lua_touserdata(L, idx));
		if(len != sizeof(MasterUd) || !u || u->tag != t.tag || !u->obj)
			return false;
		lua_checkstack(L, 4);
		PushInternalsType(L, t.tag);
		bool cached = false;
		if(lua_istable(L, -1))
		{
			lua_pushlightuserdata(L, u->obj);
			lua_rawget(L, -2);
			cached = lua_rawequal(L, -1, idx) != 0;
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
		if(!cached)
		{
			if(InterlockedIncrement(&s_lMasterNotCached) <= 20)
				SrvLog("remote: LOUD: a %s userdata whose object %p the DLL's vpdllinternals cache does not hold: sent "
				       "as a server value (issue 0314)", t.name.c_str(), u->obj);
			return false;
		}
		w.u32(vpw::kLInstance);
		vpipc::write_string(w, t.name);
		w.u64((uint64_t)(uintptr_t)u->obj);
		InterlockedIncrement(&s_lMasterEnc);
		return true;
	}

	// Issue 0314: pushes the DLL's cached userdata of object p of the type's tag when it is live for p (its obj is p);
	// else pushes nothing and returns false.
	bool PushCachedMaster(lua_State* L, uint32_t tag, void* p)
	{
		const int base = lua_gettop(L);
		PushInternalsType(L, tag);
		if(lua_istable(L, -1))
		{
			lua_pushlightuserdata(L, p);
			lua_rawget(L, -2);
			const MasterUd* u = static_cast<const MasterUd*>(lua_touserdata(L, -1));
			if(lua_type(L, -1) == LUA_TUSERDATA && lua_objlen(L, -1) == sizeof(MasterUd) && u && u->tag == tag &&
			   u->obj == p)
			{
				lua_replace(L, base + 1);
				lua_settop(L, base + 1);
				return true;
			}
		}
		lua_settop(L, base);
		return false;
	}

	int ResolveAvFilter(DWORD code)
	{
		return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER
		                                                                              : EXCEPTION_CONTINUE_SEARCH;
	}

	// One of the DLL's getters (GetID, GetOwner, GetX, GetY: GetInstance(L) at index 1, one field read, one integer
	// pushed), called directly under a structured-exception guard, as ticket 0576's srv_lua_stale.cpp ProtectedGet: an
	// object whose memory is gone faults here and nothing of Lua is half done. No C++ object lives in this frame.
	bool ResolveGet(lua_CFunction f, lua_State* L, int* pValue)
	{
		__try
		{
			if(!f || f(L) != 1)
				return false;
		}
		__except(ResolveAvFilter(GetExceptionCode()))
		{
			return false;
		}
		*pValue = (int)lua_tointeger(L, -1);
		return true;
	}

	struct ResolveArgs
	{
		const MasterType* t;
		LuaCtx* ctx;
		void* p;
		bool readable;
		int a, b;
	};

	// lua_cpcall'd on the scratch main state: identifies object p (a Unit, City or Plot this state never pushed: the
	// engine got it on another channel) through a temporary LuaTaggedAutoPointer that is not linked into the object's
	// pointer list (prev and next point at itself, no metatable, so no finalizer ever runs on it), and asks the DLL's
	// own lookup for it: Players[owner]:GetUnitByID(id) / GetCityByID(id), Map.GetPlot(x, y). That lookup pushes, and so
	// caches, the DLL's own linked userdata; the caller then finds it in vpdllinternals with its obj == p, or the
	// object is not what p was (stale).
	int ResolveFrame(lua_State* L)
	{
		ResolveArgs* ra = static_cast<ResolveArgs*>(lua_touserdata(L, 1));
		lua_settop(L, 0);
		const int k = ra->t->resolve;
		MasterUd* tmp = static_cast<MasterUd*>(lua_newuserdata(L, sizeof(MasterUd)));
		tmp->tag = ra->t->tag;
		tmp->obj = ra->p;
		tmp->prev = &tmp->obj;
		tmp->next = &tmp->obj;
		int a = 0, b = 0;
		if(!ResolveGet(s_resGet[k][0], L, &a))
			return 0;
		lua_settop(L, 1);
		if(!ResolveGet(s_resGet[k][1], L, &b))
			return 0;
		lua_settop(L, 1);
		ra->readable = true;
		ra->a = a;
		ra->b = b;
		if(k == kResPlot)
		{
			std::map<std::string, const void*>::const_iterator m = ra->ctx->masterByName.find("Map");
			if(m == ra->ctx->masterByName.end())
				return 0;
			const int mapRef = ra->ctx->masterByMt[m->second].staticRef;
			if(mapRef == LUA_NOREF)
				return 0;
			lua_rawgeti(L, LUA_REGISTRYINDEX, mapRef);
			lua_getfield(L, -1, "GetPlot");
			lua_pushinteger(L, a);
			lua_pushinteger(L, b);
			lua_call(L, 2, 1);
			return 0;
		}
		if(b < 0 || b >= 256)
			return 0;
		lua_rawgeti(L, LUA_REGISTRYINDEX, ra->ctx->globalsRef);
		lua_getfield(L, -1, "Players");
		if(!lua_istable(L, -1))
			return 0;
		lua_rawgeti(L, -1, b);
		if(lua_type(L, -1) != LUA_TUSERDATA)
			return 0;
		lua_getfield(L, -1, k == kResUnit ? "GetUnitByID" : "GetCityByID");
		lua_pushvalue(L, -2);
		lua_pushinteger(L, a);
		lua_call(L, 2, 1);
		return 0;
	}

	// Issue 0314: decodes kLInstance(name, p) when `name` is a master type of this state; returns false otherwise (the
	// pin's form decodes it). A static is this state's own global userdata. A dynamic instance is the DLL's cached
	// userdata for p when it is live for p; else (the engine got it on another channel, whose scratch state is another
	// Lua state) it is looked up again by the object's identity (ResolveFrame); failing that, the DLL's invalidated
	// userdata of that address when there is one (a call on it gets the DLL's own "Invalid instance!"), else nil.
	bool DecodeMaster(lua_State* L, LuaCtx* ctx, const std::string& name, void* p)
	{
		if(ctx->masterByName.empty())
			return false;
		std::map<std::string, const void*>::const_iterator it = ctx->masterByName.find(name);
		if(it == ctx->masterByName.end())
			return false;
		const MasterType& t = ctx->masterByMt[it->second];
		lua_checkstack(L, 8);
		if(t.staticRef != LUA_NOREF)
		{
			lua_rawgeti(L, LUA_REGISTRYINDEX, t.staticRef);
			return true;
		}
		if(PushCachedMaster(L, t.tag, p))
		{
			InterlockedIncrement(&s_lMasterDec);
			return true;
		}
		InterlockedIncrement(&s_lMasterMiss);
		ResolveArgs ra = {&t, ctx, p, false, 0, 0};
		if(t.resolve)
		{
			lua_State* M = ctx->L;  // the main scratch state (L may be a scratch thread of it: the same registry)
			if(lua_cpcall(M, ResolveFrame, &ra) != 0)
				lua_pop(M, 1);
			if(PushCachedMaster(L, t.tag, p))
			{
				const long n = InterlockedIncrement(&s_lMasterResolved);
				if(n <= 20)
					SrvLog("remote: Lua: a %s instance %p this state had not seen (channel %u) found again by its "
					       "identity (%d, %d) (issue 0314)", t.name.c_str(), p, ctx->chan, ra.a, ra.b);
				return true;
			}
		}
		const int base = lua_gettop(L);
		PushInternalsType(L, t.tag);
		if(lua_istable(L, -1))
		{
			lua_pushlightuserdata(L, p);
			lua_rawget(L, -2);
			if(lua_type(L, -1) == LUA_TUSERDATA)
			{
				lua_replace(L, base + 1);
				lua_settop(L, base + 1);
				if(InterlockedIncrement(&s_lMasterDead) <= 20)
					SrvLog("remote: LOUD: a %s instance %p is no longer live for this object (deleted%s): the DLL's "
					       "invalidated userdata is passed (issue 0314)", t.name.c_str(), p,
					       t.resolve ? ", and its identity read found no live object" : "");
				return true;
			}
		}
		lua_settop(L, base);
		lua_pushnil(L);
		const long n = InterlockedIncrement(&s_lMasterLost);
		if(n <= 20)
			SrvLog("remote: LOUD: a %s instance %p is unknown to the scratch state of channel %u%s: nil (issue 0314)",
			       t.name.c_str(), p, ctx->chan, !t.resolve ? " (no resolver for this type)" :
			       ra.readable ? " (its identity names no live object)" : " (its identity read faulted)");
		return true;
	}

	int EStub(lua_State* L);

	void EncodeTable(lua_State* L, int idx, vpipc::Writer& w, Enc& e, uint32_t depth)
	{
		// instance table
		lua_pushstring(L, "__instance");
		lua_rawget(L, idx);
		if(lua_type(L, -1) == LUA_TLIGHTUSERDATA)
		{
			const void* p = lua_touserdata(L, -1);
			lua_pop(L, 1);
			std::string name;
			if(!InstanceTypeName(L, idx, e.ctx, name))
				CountUnsupported("instance table without a known type (sent with type \"\")");
			w.u32(vpw::kLInstance);
			vpipc::write_string(w, name);
			w.u64((uint64_t)(uintptr_t)p);
			return;
		}
		lua_pop(L, 1);
		// a LuaTypes type table: empty (its members come from kRt_LuaDescribeType)
		const void* t = lua_topointer(L, idx);
		if(e.ctx->typeNames.count(t))
		{
			w.u32(vpw::kLTable);
			w.u32(0);
			return;
		}
		if(depth >= vpw::kLuaMaxDepth)
		{
			WriteUnsupported(w, "table nested too deep");
			return;
		}
		for(size_t i = 0; i < e.path.size(); ++i)
			if(e.path[i] == t)
			{
				WriteUnsupported(w, "table that contains itself");
				return;
			}
		uint32_t n = 0;
		lua_pushnil(L);
		while(lua_next(L, idx))
		{
			++n;
			lua_pop(L, 1);
		}
		e.path.push_back(t);
		w.u32(vpw::kLTable);
		w.u32(n);
		lua_checkstack(L, 4);
		lua_pushnil(L);
		while(lua_next(L, idx))
		{
			const int top = lua_gettop(L);
			EncodeValue(L, top - 1, w, e, depth + 1);
			EncodeValue(L, top, w, e, depth + 1);
			lua_pop(L, 1);
		}
		e.path.pop_back();
	}

	void EncodeValue(lua_State* L, int idx, vpipc::Writer& w, Enc& e, uint32_t depth)
	{
		idx = AbsIndex(L, idx);
		const int type = lua_type(L, idx);
		switch(type)
		{
		case LUA_TNONE:
		case LUA_TNIL: w.u32(vpw::kLNil); return;
		case LUA_TBOOLEAN: w.u32(lua_toboolean(L, idx) ? vpw::kLTrue : vpw::kLFalse); return;
		case LUA_TNUMBER:
			w.u32(vpw::kLNumber);
			w.f64((double)lua_tonumber(L, idx));
			return;
		case LUA_TSTRING:
		{
			size_t n = 0;
			const char* s = lua_tolstring(L, idx, &n);
			w.u32(vpw::kLString);
			w.str(s, (uint32_t)n);
			return;
		}
		case LUA_TTABLE: EncodeTable(L, idx, w, e, depth); return;
		case LUA_TFUNCTION:
			if(lua_iscfunction(L, idx))
			{
				lua_CFunction f = lua_tocfunction(L, idx);
				if(f == EStub && lua_getupvalue(L, idx, 1))
				{
					const ERef* er = static_cast<const ERef*>(lua_touserdata(L, -1));
					lua_pop(L, 1);
					if(er)
					{
						w.u32(vpw::kLERef);
						w.u32(er->id);
						w.u32(er->type);
						return;
					}
				}
				const uint32_t fid = CFuncIdAt(L, idx);  // issue 0313: a closure of master's wrapper too
				if(fid)
				{
					w.u32(vpw::kLCFunc);
					w.u32(fid);
					return;
				}
			}
			EncodeSRef(L, idx, w, e, type);
			return;
		case LUA_TUSERDATA:
			if(EncodeMaster(L, idx, w, e.ctx))  // issue 0314: master's instances and statics
				return;
			EncodeSRef(L, idx, w, e, type);
			return;
		case LUA_TLIGHTUSERDATA: WriteUnsupported(w, "light userdata"); return;
		case LUA_TTHREAD: WriteUnsupported(w, "thread"); return;
		default: WriteUnsupported(w, "value of an unknown Lua type"); return;
		}
	}

	// One top-level value: a table is encoded into a scratch buffer first so the size limit can be applied.
	void EncodeTop(lua_State* L, int idx, vpipc::Writer& w, LuaCtx* ctx)
	{
		Enc e;
		e.ctx = ctx;
		idx = AbsIndex(L, idx);
		if(lua_type(L, idx) != LUA_TTABLE)
		{
			EncodeValue(L, idx, w, e, 0);
			return;
		}
		vpipc::Writer tmp;
		tmp.reset(NULL, 0, true);
		EncodeValue(L, idx, tmp, e, 0);
		if(tmp.overflowed() || tmp.size() > vpw::kLuaMaxBytes)
		{
			WriteUnsupported(w, "table over kLuaMaxBytes");
			return;
		}
		uint8_t* d = w.reserve(tmp.size());
		if(d && tmp.size())
			memcpy(d, tmp.data(), tmp.size());
	}

	// ---- decoding: pushes exactly one value ----
	void PushInstance(lua_State* L, LuaCtx* ctx, const std::string& name, void* p)
	{
		lua_checkstack(L, 8);
		const int base = lua_gettop(L);
		PushMainGlobal(L, ctx, "LuaTypes");				// base+1
		int typeIdx = 0, instIdx = 0;
		if(lua_istable(L, -1))
		{
			lua_getfield(L, -1, name.c_str());			// base+2
			if(lua_istable(L, -1))
			{
				typeIdx = base + 2;
				lua_pushstring(L, "__instances");
				lua_rawget(L, typeIdx);					// base+3
				if(lua_istable(L, -1))
				{
					instIdx = base + 3;
					lua_pushlightuserdata(L, p);
					lua_rawget(L, instIdx);
					if(lua_istable(L, -1))
					{
						lua_replace(L, base + 1);
						lua_settop(L, base + 1);
						return;
					}
					lua_pop(L, 1);
				}
			}
		}
		lua_createtable(L, 0, 2);
		const int t = lua_gettop(L);
		lua_pushstring(L, "__instance");
		lua_pushlightuserdata(L, p);
		lua_rawset(L, t);
		lua_pushstring(L, "__vptype");
		lua_pushstring(L, name.c_str());
		lua_rawset(L, t);
		if(typeIdx)
		{
			lua_pushvalue(L, typeIdx);
			lua_setmetatable(L, t);
		}
		if(instIdx)
		{
			lua_pushlightuserdata(L, p);
			lua_pushvalue(L, t);
			lua_rawset(L, instIdx);
		}
		lua_replace(L, base + 1);
		lua_settop(L, base + 1);
	}

	void DecodeValue(lua_State* L, vpipc::Reader& r, LuaCtx* ctx, uint32_t engineThread, uint32_t depth)
	{
		lua_checkstack(L, 4);
		const uint32_t tag = r.u32();
		if(!r.ok())
		{
			lua_pushnil(L);
			return;
		}
		switch(tag)
		{
		case vpw::kLNil: lua_pushnil(L); return;
		case vpw::kLFalse: lua_pushboolean(L, 0); return;
		case vpw::kLTrue: lua_pushboolean(L, 1); return;
		case vpw::kLNumber: lua_pushnumber(L, (lua_Number)r.f64()); return;
		case vpw::kLString:
		{
			uint32_t n = 0;
			const char* s = r.cstr(&n);
			if(s)
				lua_pushlstring(L, s, n);
			else
				lua_pushnil(L);
			return;
		}
		case vpw::kLTable:
		{
			const uint32_t n = r.u32();
			lua_createtable(L, 0, 0);
			const int t = lua_gettop(L);
			for(uint32_t i = 0; i < n && r.ok(); ++i)
			{
				DecodeValue(L, r, ctx, engineThread, depth + 1);
				DecodeValue(L, r, ctx, engineThread, depth + 1);
				if(lua_isnil(L, -2))
					lua_pop(L, 2);
				else
					lua_rawset(L, t);
			}
			return;
		}
		case vpw::kLCFunc:
		{
			const FuncDesc d = FuncOf(r.u32());
			if(d.f)
				PushFunc(L, d);  // issue 0313: a wrapped function arrives inside its wrapper closure again
			else
			{
				InterlockedIncrement(&s_lBadRefs);
				SrvLog("remote: LOUD: unknown C function id (nil)");
				lua_pushnil(L);
			}
			return;
		}
		case vpw::kLSRef:
		{
			const uint32_t id = r.u32();
			r.u32();	// Lua type
			if((id >> 24) != ctx->chan + 1u)
			{
				const long n = InterlockedIncrement(&s_lBadRefs);
				if(n <= 20)
					SrvLog("remote: LOUD: server value 0x%08x of channel %u used on channel %u (nil)", id, (id >> 24) - 1u,
					       ctx->chan);
				lua_pushnil(L);
				return;
			}
			lua_rawgeti(L, LUA_REGISTRYINDEX, (int)(id & 0x00FFFFFFu));
			return;
		}
		case vpw::kLInstance:
		{
			const std::string name = vpipc::read_string(r);
			const uint64_t p = r.u64();
			if(!p)
				lua_pushnil(L);
			else if(!DecodeMaster(L, ctx, name, (void*)(uintptr_t)p))  // issue 0314: master's form first
				PushInstance(L, ctx, name, (void*)(uintptr_t)p);
			return;
		}
		case vpw::kLERef:
		{
			ERef* e = static_cast<ERef*>(lua_newuserdata(L, sizeof(ERef)));
			e->id = r.u32();
			e->type = r.u32();
			e->thread = engineThread;
			luaL_getmetatable(L, kERefMeta);
			lua_setmetatable(L, -2);
			if(e->type != LUA_TFUNCTION)
				CountUnsupported("engine value other than a function (a callable stub stands for it)");
			lua_pushcclosure(L, EStub, 1);
			return;
		}
		case vpw::kLUnsupported:
			vpipc::read_string(r);
			InterlockedIncrement(&s_lUnsupported);
			lua_pushnil(L);
			return;
		default:
			SrvLog("remote: LOUD: Lua value tag %u unknown", tag);
			lua_pushnil(L);
			return;
		}
	}

	// ---- engine values: a call through the engine's own CallFunction (kRtE_LuaCall) ----
	// Returns the number of results pushed, or -1 with the error message on the stack.
	int EStubCall(lua_State* L)
	{
		SrvDbFlush();	// the DLL's pending database writes reach the engine first (srv_remote_db.cpp)
		const ERef* e = static_cast<const ERef*>(lua_touserdata(L, lua_upvalueindex(1)));
		LuaCtx* ctx = Ctx();
		const int nargs = lua_gettop(L);
		vpipc::Channel& ch = SrvRt::Chan();
		vpipc::Writer& w = SrvRt::Begin(ch, vpw::kRtE_LuaCall);
		w.u32(e->thread);
		w.u32(vpw::kLERef);
		w.u32(e->id);
		w.u32(e->type);
		w.u32((uint32_t)nargs);
		for(int i = 1; i <= nargs; ++i)
			EncodeTop(L, i, w, ctx);
		w.u32((uint32_t)LUA_MULTRET);
		vpipc::Reader r;
		if(!SrvRt::End(ch, vpw::kRtE_LuaCall, r))
		{
			lua_pushstring(L, "kRtE_LuaCall failed (see server.log)");
			return -1;
		}
		const uint32_t status = r.u32();
		if(status != 0)
		{
			const std::string msg = vpipc::read_string(r);
			SrvRt::DecodeCheck(vpw::kRtE_LuaCall, r);
			lua_pushlstring(L, msg.data(), msg.size());
			return -1;
		}
		const uint32_t n = r.u32();
		lua_settop(L, 0);
		for(uint32_t i = 0; i < n && r.ok(); ++i)
			DecodeValue(L, r, ctx, e->thread, 0);
		SrvRt::DecodeCheck(vpw::kRtE_LuaCall, r);
		return lua_gettop(L);
	}
	int EStub(lua_State* L)
	{
		const int n = EStubCall(L);
		if(n < 0)
			return lua_error(L);	// no C++ object with a destructor lives in this frame
		return n;
	}

	// Issue 0315: what the DLL gets for an engine function it fetches from one of its threads' globals during its own
	// RegisterScriptLibraries (upvalues: engine thread id, global name, the engine-value stub). In the EXE that call runs
	// at the end of the registration of the engine state the EXE asked for (master's DoRegisterScriptData ends with
	// executeSupportEvent("gc_post")); here the registration runs on a scratch state, once per channel, while the engine
	// state has no globals yet. So a call with scalar arguments only is kept (LuaCtx::postCalls) and returns nothing (the
	// DLL's lua_pcall(supportState, 1, 0, 0) asks for none); the proxy makes it after each engine registration
	// (kLuaPostCallsKey). Any other call, or a call after the registration ended, goes to the engine at once as before.
	int DeferredCall(lua_State* L)
	{
		LuaCtx* const c = t_collect;
		const int n = lua_gettop(L);
		bool scalar = true;
		for(int i = 1; i <= n && scalar; ++i)
		{
			const int t = lua_type(L, i);
			scalar = t == LUA_TNIL || t == LUA_TBOOLEAN || t == LUA_TNUMBER || t == LUA_TSTRING;
		}
		if(c && scalar)
		{
			{
				PostCall pc;
				pc.thread = (uint32_t)lua_tonumber(L, lua_upvalueindex(1));
				pc.name = lua_tostring(L, lua_upvalueindex(2));
				for(int i = 1; i <= n; ++i)
				{
					PostArg a;
					a.type = lua_type(L, i);
					if(a.type == LUA_TBOOLEAN)
						a.num = lua_toboolean(L, i) ? 1 : 0;
					else if(a.type == LUA_TNUMBER)
						a.num = lua_tonumber(L, i);
					else if(a.type == LUA_TSTRING)
					{
						size_t len = 0;
						const char* s = lua_tolstring(L, i, &len);
						a.str.assign(s, len);
					}
					pc.args.push_back(a);
				}
				c->postCalls.push_back(pc);
			}
			InterlockedIncrement(&s_lPostCallsKept);
			return 0;
		}
		if(InterlockedIncrement(&s_lPostCallsNow) <= 20)
			SrvLog("remote: the engine function %s fetched during RegisterScriptLibraries is called %s: it goes to the "
			       "engine at once (issue 0315)", lua_tostring(L, lua_upvalueindex(2)),
			       c ? "with an argument that is not a scalar" : "after the registration ended");
		lua_pushvalue(L, lua_upvalueindex(3));
		lua_insert(L, 1);
		lua_call(L, n, LUA_MULTRET);
		return lua_gettop(L);
	}

	// __index of a scratch thread's globals: the engine thread's global (kRtE_LuaGetGlobal), never cached.
	int GlobalIndex(lua_State* L)
	{
		SrvDbFlush();	// the DLL's pending database writes reach the engine first (srv_remote_db.cpp)
		if(lua_type(L, 2) != LUA_TSTRING)
			return 0;
		const uint32_t uThread = (uint32_t)lua_tonumber(L, lua_upvalueindex(1));
		LuaCtx* ctx = Ctx();
		size_t len = 0;
		const char* name = lua_tolstring(L, 2, &len);
		vpipc::Channel& ch = SrvRt::Chan();
		vpipc::Writer& w = SrvRt::Begin(ch, vpw::kRtE_LuaGetGlobal);
		w.u32(uThread);
		w.str(name, (uint32_t)len);
		vpipc::Reader r;
		if(!SrvRt::End(ch, vpw::kRtE_LuaGetGlobal, r))
			return 0;
		DecodeValue(L, r, ctx, uThread, 0);
		SrvRt::DecodeCheck(vpw::kRtE_LuaGetGlobal, r);
		// Issue 0315: during the DLL's RegisterScriptLibraries an engine function is handed out deferred.
		if(t_collect && lua_tocfunction(L, -1) == EStub)
		{
			lua_pushnumber(L, (lua_Number)uThread);
			lua_pushvalue(L, 2);
			lua_pushvalue(L, -3);
			lua_pushcclosure(L, DeferredCall, 3);
			lua_remove(L, -2);
		}
		return 1;
	}

	int IndexTramp(lua_State* L)
	{
		lua_gettable(L, 1);
		return 1;
	}

	ScratchThread* FindThread(lua_State* S)
	{
		LuaCtx* ctx = Ctx();
		std::map<lua_State*, ScratchThread>::iterator it = ctx->threads.find(S);
		return it != ctx->threads.end() ? &it->second : NULL;
	}

	void UnknownThread(const char* szWhat, lua_State* S)
	{
		const long n = InterlockedIncrement(&s_lUnknownThreads);
		if(n <= 20)
			SrvLog("remote: LOUD: %s(%p): not a scratch thread of this service thread", szWhat, (void*)S);
	}

	// ---- chunks the DLL loads on its own Lua threads run in the engine VM (issue 2504) ----
	// LuaSupport::InitLuaFramework (CvLuaSupport.cpp) does, on the thread CreateLuaThread("VP_LUAAPI") gave it:
	// lua_pushvalue(L, LUA_REGISTRYINDEX); lua_setglobal(L, "REGISTRY"); luaL_dostring(L, <the VP_LUAAPI framework>).
	// In the EXE that chunk runs in the engine VM (it installs a metatable on the engine's global Threads table, the
	// hook the VPUI framework hangs on). Here the DLL's luaL_loadstring on a scratch thread S (its import slot points at
	// HookLoadString) pushes a stub instead; the DLL's lua_pcall of the stub sends kRtE_LuaRunChunk: the proxy sets
	// S's own globals on the engine thread T (S's registry stands for T's registry), loads the same text with
	// luaL_loadstring on T and runs it there with lua_pcall; its results or its error come back to S.
	typedef int (*PLoadString)(lua_State*, const char*);
	PLoadString o_loadstring = NULL;
	volatile long s_lChunks = 0, s_lChunkErrors = 0;

	// Returns the number of results pushed, or -1 with the error message on the stack.
	int ChunkStubCall(lua_State* S)
	{
		SrvDbFlush();	// the DLL's pending database writes reach the engine first (srv_remote_db.cpp)
		ScratchThread* t = FindThread(S);
		if(!t)
		{
			lua_pushstring(S, "kRtE_LuaRunChunk: not a scratch thread");
			return -1;
		}
		LuaCtx* ctx = Ctx();
		const uint32_t engineId = t->engineId;
		const std::string threadName = t->name;
		size_t len = 0;
		const char* chunk = lua_tolstring(S, lua_upvalueindex(1), &len);
		// S's own globals (rawly: its globals table has an __index into the engine thread)
		lua_checkstack(S, 8);
		const int base = lua_gettop(S);
		std::vector<std::string> names;
		lua_pushvalue(S, LUA_GLOBALSINDEX);
		const int g = lua_gettop(S);
		lua_pushnil(S);
		while(lua_next(S, g))
		{
			if(lua_type(S, -2) == LUA_TSTRING)
				names.push_back(lua_tostring(S, -2));
			lua_pop(S, 1);
		}
		vpipc::Channel& ch = SrvRt::Chan();
		vpipc::Writer& w = SrvRt::Begin(ch, vpw::kRtE_LuaRunChunk);
		w.u32(engineId);
		w.u32((uint32_t)names.size());
		for(size_t i = 0; i < names.size(); ++i)
		{
			w.str(names[i].c_str());
			lua_pushstring(S, names[i].c_str());
			lua_rawget(S, g);
			if(lua_rawequal(S, -1, LUA_REGISTRYINDEX))
				w.u32(1);
			else
			{
				w.u32(0);
				EncodeTop(S, -1, w, ctx);
			}
			lua_pop(S, 1);
		}
		w.str(chunk ? chunk : "", (uint32_t)len);
		lua_settop(S, base);
		InterlockedIncrement(&s_lChunks);
		vpipc::Reader r;
		if(!SrvRt::End(ch, vpw::kRtE_LuaRunChunk, r))
		{
			InterlockedIncrement(&s_lChunkErrors);
			lua_pushstring(S, "kRtE_LuaRunChunk failed (see server.log)");
			return -1;
		}
		const uint32_t status = r.u32();
		if(status != 0)
		{
			const std::string msg = vpipc::read_string(r);
			SrvRt::DecodeCheck(vpw::kRtE_LuaRunChunk, r);
			InterlockedIncrement(&s_lChunkErrors);
			CountError("kRtE_LuaRunChunk (in the engine VM)", msg.c_str());
			lua_pushlstring(S, msg.data(), msg.size());
			return -1;
		}
		const uint32_t n = r.u32();
		lua_settop(S, 0);
		for(uint32_t i = 0; i < n && r.ok(); ++i)
			DecodeValue(S, r, ctx, engineId, 0);
		SrvRt::DecodeCheck(vpw::kRtE_LuaRunChunk, r);
		SrvLog("remote: kRtE_LuaRunChunk on engine thread %u (%s), %zu bytes, %zu globals: ran in the engine VM, %u results",
		       engineId, threadName.c_str(), len, names.size(), n);
		return lua_gettop(S);
	}
	int ChunkStub(lua_State* S)
	{
		const int n = ChunkStubCall(S);
		if(n < 0)
			return lua_error(S);	// no C++ object with a destructor lives in this frame
		return n;
	}

	// The DLL's luaL_loadstring (its import slot): a scratch thread of this service thread gets the stub; any other
	// state (none at the pin) loads as before.
	int HookLoadString(lua_State* L, const char* s)
	{
		if(L && s && FindThread(L))
		{
			lua_pushstring(L, s);
			lua_pushcclosure(L, ChunkStub, 1);
			return 0;
		}
		return o_loadstring(L, s);
	}

	// ---- ticket 0576: the stale-instance guard (srv_lua_stale.h, research 03 lua-stale-instance.md) ----
	volatile long s_lStaleCalls = 0;	// calls not made because an argument was stale (each one LOUD line, see below)
	const uint32_t kStaleNoFid = 0xFFFFFFFFu;

	// The callee's name for the LOUD line: the Lua path the DLL registered the function under.
	std::string StaleCalleeName(uint32_t fid, const char* szFallback)
	{
		if(szFallback)
			return szFallback;
		if(fid == 0)
			return "a server-side Lua function";
		std::string name;
		AcquireSRWLockShared(&s_lockF);
		if(fid < s_funcNames.size())
			name = s_funcNames[fid];
		ReleaseSRWLockShared(&s_lockF);
		if(name.empty())
		{
			char sz[48];
			sprintf_s(sz, "DLL Lua function #%u", fid);
			name = sz;
		}
		return name;
	}

	// Checks the values at [first, last] of the scratch state right before the DLL gets them. On a stale Unit or City
	// instance: one LOUD line (kind, pointer, owner and id as read, the function, the argument, the channel) and true;
	// the caller then does not call the DLL with it. The line goes out for the first 200 and then every 1000th.
	bool StaleArgs(LuaCtx* ctx, int first, int last, uint32_t fid, const char* szCallee)
	{
		if(!ctx->staleMask || first > last)
			return false;
		lua_State* L = ctx->L;
		lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->playersRef);
		const int players = lua_gettop(L);
		SrvLuaStale::Miss m;
		const int n = SrvLuaStale::CheckArgs(L, first, last, ctx->staleMts, ctx->staleMask, players, &m);
		lua_pop(L, 1);
		if(!n)
			return false;
		const long nCall = InterlockedIncrement(&s_lStaleCalls);
		if(nCall <= 200 || nCall % 1000 == 0)
		{
			char who[128];
			if(m.readable)
				sprintf_s(who, "owner %d, id %d: the owner's list holds %s", m.owner, m.id,
				          m.live ? "another object for that id" : "no object for that id");
			else
				sprintf_s(who, "its memory is gone: the identity read faulted");
			SrvLog("remote: LOUD: stale %s instance %p (%s) in argument %d of %s on channel %u: the DLL is not called "
			       "with it (ticket 0576; %d stale argument(s) in this call; stale call %ld)",
			       SrvLuaStale::KindName(m.kind), m.p, who, m.arg - first + 1, StaleCalleeName(fid, szCallee).c_str(),
			       ctx->chan, n, nCall);
		}
		return true;
	}

	// ---- ICvScriptSystemUtility1 hooks: the DLL's own method on the scratch state ----
	enum UtilOp
	{
		kPushCity, kPushDeal, kPushPlot, kPushUnit, kGetCity, kGetDeal, kGetPlot, kGetUnit, kPushReplay
	};
	struct UtilArgs
	{
		ICvScriptSystemUtility1* u;
		UtilOp op;
		void* p;			// push: the DLL object
		bool bErrorOnFail;	// get
		void* result;		// get: the new DLL wrapper
		FDataStream* s;		// replay
	};
	int UtilTramp(lua_State* L)
	{
		// stack: [value (get only)], light userdata UtilArgs
		UtilArgs* a = static_cast<UtilArgs*>(lua_touserdata(L, lua_gettop(L)));
		lua_pop(L, 1);
		switch(a->op)
		{
		case kPushCity: a->u->PushCvCityInstance(L, static_cast<ICvCity1*>(a->p)); return 1;
		case kPushDeal: a->u->PushCvDealInstance(L, static_cast<ICvDeal1*>(a->p)); return 1;
		case kPushPlot: a->u->PushCvPlotInstance(L, static_cast<ICvPlot1*>(a->p)); return 1;
		case kPushUnit: a->u->PushCvUnitInstance(L, static_cast<ICvUnit1*>(a->p)); return 1;
		case kGetCity: a->result = a->u->GetCvCityInstance(L, 1, a->bErrorOnFail); return 0;
		case kGetDeal: a->result = a->u->GetCvDealInstance(L, 1, a->bErrorOnFail); return 0;
		case kGetPlot: a->result = a->u->GetCvPlotInstance(L, 1, a->bErrorOnFail); return 0;
		case kGetUnit: a->result = a->u->GetCvUnitInstance(L, 1, a->bErrorOnFail); return 0;
		case kPushReplay:
		{
			const int top = lua_gettop(L);
			a->u->PushReplayFromStream(L, *a->s);
			if(lua_gettop(L) == top)
				lua_pushnil(L);
			return 1;
		}
		}
		return 0;
	}

	// Push*Instance: CALL self, handle of the object; RETURN the Lua value.
	bool HookPush(ICvScriptSystemUtility1* self, UtilOp op, uint32_t kind, vpipc::Reader& args, vpipc::Writer& ret,
	              vpipc::Channel& ch, const char* szKey)
	{
		const vpipc::Handle h = args.handle();
		if(!args.ok())
			return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, szKey);
		void* p = SrvRt::Resolve(h, kind);
		if(!p && (h.type || h.id))
			return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadHandle, szKey);
		LuaCtx* ctx = RegisteredCtx();
		lua_State* L = ctx->L;
		const int top = lua_gettop(L);
		UtilArgs a = {self, op, p, false, NULL, NULL};
		lua_pushcfunction(L, UtilTramp);
		lua_pushlightuserdata(L, &a);
		if(LuaPCall(L, 1, 1) != 0)
		{
			CountError(szKey, lua_tostring(L, -1));
			lua_settop(L, top);
			lua_pushnil(L);
		}
		EncodeTop(L, -1, ret, ctx);
		lua_settop(L, top);
		return true;
	}

	// Get*Instance: CALL self, Lua value, bool bErrorOnFail; RETURN handle, u32 hasError, str message.
	bool HookGet(ICvScriptSystemUtility1* self, UtilOp op, uint32_t kind, vpipc::Reader& args, vpipc::Writer& ret,
	             vpipc::Channel& ch, const char* szKey)
	{
		LuaCtx* ctx = RegisteredCtx();
		lua_State* L = ctx->L;
		const int top = lua_gettop(L);
		lua_pushcfunction(L, UtilTramp);
		DecodeValue(L, args, ctx, 0, 0);
		const bool bErr = args.b();
		if(!args.ok())
		{
			lua_settop(L, top);
			return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, szKey);
		}
		// Ticket 0576: a stale unit or city instance is handed to the DLL as nil (the answer a script's nil gets).
		if((op == kGetUnit || op == kGetCity) && StaleArgs(ctx, top + 2, top + 2, kStaleNoFid, szKey))
		{
			lua_pushnil(L);
			lua_replace(L, top + 2);
		}
		UtilArgs a = {self, op, NULL, bErr, NULL, NULL};
		lua_pushlightuserdata(L, &a);
		std::string msg;
		const bool bFailed = LuaPCall(L, 2, 0) != 0;
		if(bFailed)
			msg = lua_tostring(L, -1) ? lua_tostring(L, -1) : "(non-string error)";
		lua_settop(L, top);
		const vpipc::Handle null_ = {0u, 0};
		ret.handle(a.result ? SrvRt::Register(a.result, vpk_most_derived(kind)) : null_);
		ret.u32(bFailed ? 1u : 0u);
		vpipc::write_string(ret, msg);
		return true;
	}

	// ---- ticket 4804 (speedup P2): a GameEvents raise in one crossing (kRtE_LuaRaise) ----
	// Design: research/08-performance/speedup-p2-gameevents.md. With [Performance] GameEventsOneCrossing = 1 the DLL's
	// args objects are LocalArgs (no crossing to make, fill or free them); a raise sends the values with the call and the
	// proxy makes the EXE's CreateArgs, pushes, Call* and DestroyArgs on the waiting EXE thread (vp_luaraise.h Serve).
	// The DLL reads an args object only in DeferHook (CvLuaSupport.cpp, city capture), before any raise: the first
	// getter materialises the EXE's object the old way (generated CreateArgs and pushes) and everything of that object
	// goes the old way from then on, so the deferral sees exactly what the EXE answers.
	// Statics (the two-thread hazard, design section 4): s_iOneCrossing (read once, then read-only), the raise table
	// (own SRW lock, never held across a crossing), interlocked counters. A LocalArgs belongs to the DLL's
	// CvLuaArgsHandle that made it, as the EXE's object does today.
	volatile long s_iOneCrossing = -1;	// -1 not read yet, 0 off, 1 on
	volatile long s_lRaiseOne = 0, s_lRaiseOld = 0, s_lMaterialised = 0, s_lReraise = 0, s_lNoArgs = 0;
	volatile long s_uProxyBridge = -1;	// the proxy's Lua bridge version from kRt_LuaRegister (-1: none seen, 0: none sent)

	bool OneCrossing()
	{
		long v = s_iOneCrossing;
		if(v < 0)
		{
			v = SrvOptions().GameEventsOneCrossing() == 1 ? 1 : 0;
			if(InterlockedCompareExchange(&s_iOneCrossing, v, -1) == -1)
				SrvLog("remote: [Performance] GameEventsOneCrossing = %ld: %s (ticket 4804)", v,
				       v ? "a GameEvents raise crosses once (kRtE_LuaRaise)" : "off, every raise crosses n + 3 times");
		}
		return v == 1;
	}

	__declspec(noreturn) void RaiseFatal(const char* szFormat, ...)
	{
		char sz[512];
		va_list ap;
		va_start(ap, szFormat);
		vsnprintf_s(sz, sizeof(sz), _TRUNCATE, szFormat, ap);
		va_end(ap);
		SrvLog("remote: LOUD: %s", sz);
		SrvRemote::Fail(sz);
		SrvRemote::ReportAndExit(1, sz);	// D118: the EXE did not run the event; the game must not go on
	}

	// The raise table: (name, kind) -> counts, both paths (design section 3.5).
	struct RaiseRow
	{
		long long raises[3];	// path: 0 one crossing, 1 old (option off), 2 materialised
		long long handled, changed;
	};
	struct NameLess
	{
		bool operator()(const std::pair<const char*, uint32_t>& a, const std::pair<const char*, uint32_t>& b) const
		{
			const int c = strcmp(a.first, b.first);
			return c != 0 ? c < 0 : a.second < b.second;
		}
	};
	SRWLOCK s_lockRaise = SRWLOCK_INIT;
	std::map<std::pair<const char*, uint32_t>, RaiseRow, NameLess> s_raises;	// keys own a _strdup'd name

	void CountRaise(const char* szName, uint32_t kind, int path, bool bHandled, bool bChanged)
	{
		const char* name = szName ? szName : "(null)";
		AcquireSRWLockExclusive(&s_lockRaise);
		std::map<std::pair<const char*, uint32_t>, RaiseRow, NameLess>::iterator it =
		    s_raises.find(std::make_pair(name, kind));
		if(it == s_raises.end())
		{
			RaiseRow z = {{0, 0, 0}, 0, 0};
			it = s_raises.insert(std::make_pair(std::make_pair((const char*)_strdup(name), kind), z)).first;
		}
		++it->second.raises[path];
		it->second.handled += bHandled ? 1 : 0;
		it->second.changed += bChanged ? 1 : 0;
		ReleaseSRWLockExclusive(&s_lockRaise);
	}

	class LocalArgs : public ICvEngineScriptSystemArgs1
	{
	public:
		static const uint32_t kMagic = 0x4752414Cu;	// 'LARG'
		LocalArgs(RemoteGen_ICvEngineScriptSystem1* ss, bool bReserve, unsigned int uReserve)
		    : m_magic(kMagic), m_ss(ss), m_bReserve(bReserve), m_uReserve(uReserve), m_remote(NULL), m_raises(0)
		{
			if(bReserve && uReserve <= vplr::kMaxArgs)
				m_v.reserve(uReserve);
		}
		~LocalArgs() { m_magic = 0; }

		unsigned int Push(bool b) override
		{
			if(m_remote)
				return m_remote->Push(b);
			return Add(vplr::kBool, b ? 1u : 0u);
		}
		unsigned int Push(const char* s, size_t n) override
		{
			if(m_remote)
				return m_remote->Push(s, n);
			vplr::Arg a;
			a.tag = vplr::kString;
			a.bits = (uint32_t)n;	// narrowed as the generated path did
			a.null = s == NULL;
			if(s)
				a.s = s;	// the C string, as the generated path sent it (str: strlen)
			m_v.push_back(a);
			return (unsigned int)(m_v.size() - 1);
		}
		unsigned int Push(int i) override
		{
			if(m_remote)
				return m_remote->Push(i);
			return Add(vplr::kInt, (uint32_t)i);
		}
		unsigned int Push(float f) override
		{
			if(m_remote)
				return m_remote->Push(f);
			return Add(vplr::kFloat, vplr::FloatBits(f));
		}
		unsigned int PushNULL() override
		{
			if(m_remote)
				return m_remote->PushNULL();
			return Add(vplr::kNull, 0);
		}
		// Getters (only DeferHook at the pin): the EXE's object answers.
		unsigned int Count() override { return Remote()->Count(); }
		ArgType GetType(unsigned int k) override { return Remote()->GetType(k); }
		bool GetBool(unsigned int k, bool& b) override { return Remote()->GetBool(k, b); }
		bool GetFloat(unsigned int k, float& f) override { return Remote()->GetFloat(k, f); }
		bool GetInt(unsigned int k, int& i) override { return Remote()->GetInt(k, i); }
		bool GetString(unsigned int k, char*& s) override { return Remote()->GetString(k, s); }

		bool Valid() const { return m_magic == kMagic; }
		ICvEngineScriptSystemArgs1* Materialised() const { return m_remote; }
		const std::vector<vplr::Arg>& Values() const { return m_v; }
		bool Reserve(unsigned int& n) const
		{
			n = m_uReserve;
			return m_bReserve;
		}
		uint32_t NoteRaise() { return ++m_raises; }

	private:
		unsigned int Add(uint32_t tag, uint32_t bits)
		{
			vplr::Arg a;
			a.tag = tag;
			a.bits = bits;
			a.null = false;
			m_v.push_back(a);
			return (unsigned int)(m_v.size() - 1);	// the EXE's Push returns the 0-based index (engine-contract 10.1)
		}
		// The EXE's object, made the old way on first use: CreateArgs (or CreateArgs(n)) and every push so far.
		ICvEngineScriptSystemArgs1* Remote()
		{
			if(m_remote)
				return m_remote;
			ICvEngineScriptSystemArgs1* r = m_bReserve ? m_ss->RemoteGen_ICvEngineScriptSystem1::CreateArgs(m_uReserve)
			                                           : m_ss->RemoteGen_ICvEngineScriptSystem1::CreateArgs();
			if(!r)
				RaiseFatal("gameevents_raise: materialising an args object for a getter: the EXE's CreateArgs returned "
				           "no object");
			for(size_t i = 0; i < m_v.size(); ++i)
			{
				const vplr::Arg& a = m_v[i];
				switch(a.tag)
				{
				case vplr::kBool: r->Push(a.bits != 0); break;
				case vplr::kInt: r->Push((int)a.bits); break;
				case vplr::kFloat: r->Push(vplr::BitsFloat(a.bits)); break;
				case vplr::kString: r->Push(a.null ? NULL : a.s.c_str(), (size_t)a.bits); break;
				default: r->PushNULL(); break;
				}
			}
			m_remote = r;
			const long n = InterlockedIncrement(&s_lMaterialised);
			if(n <= 5)
				SrvLog("remote: an args object read by the DLL (DeferHook) was materialised in the EXE (%zu values; "
				       "%ld so far, ticket 4804)", m_v.size(), n);
			return r;
		}

		uint32_t m_magic;
		RemoteGen_ICvEngineScriptSystem1* m_ss;
		bool m_bReserve;
		unsigned int m_uReserve;
		ICvEngineScriptSystemArgs1* m_remote;
		uint32_t m_raises;
		std::vector<vplr::Arg> m_v;
	};

	// ---- the remote script system and args objects ----
	class RemoteScriptSystem : public RemoteGen_ICvEngineScriptSystem1
	{
	public:
		explicit RemoteScriptSystem(vpipc::Handle h) : RemoteGen_ICvEngineScriptSystem1(h) {}

		// ---- ticket 4804: args objects and raises ----
		ICvEngineScriptSystemArgs1* CreateArgs() override
		{
			if(!OneCrossing())
				return RemoteGen_ICvEngineScriptSystem1::CreateArgs();
			SrvRemote::NoteEngineCall();	// an engine method answered here (D30 conditions 2, 4), as issue 2801 does
			return new LocalArgs(this, false, 0);
		}
		ICvEngineScriptSystemArgs1* CreateArgs(unsigned int uiReserve) override
		{
			if(!OneCrossing())
				return RemoteGen_ICvEngineScriptSystem1::CreateArgs(uiReserve);
			SrvRemote::NoteEngineCall();
			return new LocalArgs(this, true, uiReserve);
		}
		void DestroyArgs(ICvEngineScriptSystemArgs1* pArgs) override
		{
			LocalArgs* la = AsLocal(pArgs);
			if(!la)
			{
				RemoteGen_ICvEngineScriptSystem1::DestroyArgs(pArgs);
				return;
			}
			SrvRemote::NoteEngineCall();
			if(la->Materialised())
				RemoteGen_ICvEngineScriptSystem1::DestroyArgs(la->Materialised());
			delete la;
		}
		bool CallHook(const char* szName, ICvEngineScriptSystemArgs1* args, bool& value) override
		{
			return Raise(vplr::kHook, szName, args, &value, NULL, NULL);
		}
		bool CallTestAll(const char* szName, ICvEngineScriptSystemArgs1* args, bool& value) override
		{
			return Raise(vplr::kTestAll, szName, args, &value, NULL, NULL);
		}
		bool CallTestAny(const char* szName, ICvEngineScriptSystemArgs1* args, bool& value) override
		{
			return Raise(vplr::kTestAny, szName, args, &value, NULL, NULL);
		}
		bool CallAccumulator(const char* szName, ICvEngineScriptSystemArgs1* args, int& value) override
		{
			return Raise(vplr::kAccumInt, szName, args, NULL, &value, NULL);
		}
		bool CallAccumulator(const char* szName, ICvEngineScriptSystemArgs1* args, float& value) override
		{
			return Raise(vplr::kAccumFloat, szName, args, NULL, NULL, &value);
		}

		lua_State* CreateLuaThread(const char* szName) override
		{
			SrvRemote::NoteEngineCall();	// issue 2801: counted even where this method answers without a crossing
			SrvDbFlush();	// the DLL's pending database writes reach the engine first (srv_remote_db.cpp)
			vpipc::Channel& ch = SrvRt::Chan();
			vpipc::Writer& w = SrvRt::Begin(ch, kM_ICvEngineScriptSystem1_CreateLuaThread);
			w.handle(m_h);
			w.str(szName);
			vpipc::Reader r;
			if(!SrvRt::End(ch, kM_ICvEngineScriptSystem1_CreateLuaThread, r))
				return NULL;
			const uint32_t id = r.u32();
			SrvRt::DecodeCheck(kM_ICvEngineScriptSystem1_CreateLuaThread, r);
			if(!id)
			{
				SrvLog("remote: CreateLuaThread(%s): the engine returned no thread", szName ? szName : "");
				return NULL;
			}
			LuaCtx* ctx = RegisteredCtx();
			lua_State* L = ctx->L;
			lua_State* S = lua_newthread(L);
			ScratchThread t;
			t.S = S;
			t.anchor = luaL_ref(L, LUA_REGISTRYINDEX);
			t.engineId = id;
			t.name = szName ? szName : "";
			lua_newtable(S);					// S's globals
			lua_newtable(S);					// its metatable
			lua_pushnumber(S, (lua_Number)id);
			lua_pushcclosure(S, GlobalIndex, 1);
			lua_setfield(S, -2, "__index");
			lua_setmetatable(S, -2);
			lua_replace(S, LUA_GLOBALSINDEX);
			ctx->threads[S] = t;
			SrvLog("remote: CreateLuaThread(%s): engine thread %u, scratch thread %p (channel %u)", t.name.c_str(), id,
			       (void*)S, ctx->chan);
			return S;
		}

		void FreeLuaThread(lua_State* S) override
		{
			SrvRemote::NoteEngineCall();	// issue 2801
			SrvDbFlush();	// the DLL's pending database writes reach the engine first (srv_remote_db.cpp)
			ScratchThread* t = FindThread(S);
			if(!t)
			{
				UnknownThread("FreeLuaThread", S);
				return;
			}
			const ScratchThread copy = *t;
			LuaCtx* ctx = Ctx();
			ctx->threads.erase(S);
			if(copy.name == "VP_LUAAPI")
				SrvLog("remote: VP_LUAAPI freed; chunks run in the engine VM so far: %ld (%ld failed)", s_lChunks,
				       s_lChunkErrors);
			vpipc::Channel& ch = SrvRt::Chan();
			vpipc::Writer& w = SrvRt::Begin(ch, kM_ICvEngineScriptSystem1_FreeLuaThread);
			w.handle(m_h);
			w.u32(copy.engineId);
			vpipc::Reader r;
			if(SrvRt::End(ch, kM_ICvEngineScriptSystem1_FreeLuaThread, r))
				SrvRt::DecodeCheck(kM_ICvEngineScriptSystem1_FreeLuaThread, r);
			luaL_unref(ctx->L, LUA_REGISTRYINDEX, copy.anchor);
			FlushReleases();
		}

		bool LoadFile(lua_State* S, const char* szFilename) override
		{
			SrvDbFlush();	// the DLL's pending database writes reach the engine first (srv_remote_db.cpp)
			ScratchThread* t = FindThread(S);
			if(!t)
			{
				UnknownThread("LoadFile", S);
				return false;
			}
			// Issue 0315: in the EXE the DLL's thread is the engine thread, so the globals the DLL set on it before
			// loading a file are there for the file (upstream master: RegisterSupportStuff's vputils, then
			// RunScript("GamecoreInit.lua"), whose pushUtils copies vputils into every UI context). Here they are on S:
			// they go to T first, by kRtE_LuaRunChunk with an empty chunk (as a chunk the DLL loads on S, issue 2504).
			{
				bool own = false;
				lua_pushvalue(S, LUA_GLOBALSINDEX);
				lua_pushnil(S);
				while(!own && lua_next(S, -2))
				{
					own = lua_type(S, -2) == LUA_TSTRING;
					lua_pop(S, 1);
				}
				lua_settop(S, own ? lua_gettop(S) - 2 : lua_gettop(S) - 1);
				if(own)
				{
					lua_pushstring(S, "");
					lua_pushcclosure(S, ChunkStub, 1);
					t_trustWrapShape = true;	// the DLL's functions among them cross as kLCFunc (CFuncIdAt)
					const int rcg = lua_pcall(S, 0, 0, 0);
					t_trustWrapShape = false;
					if(rcg != 0)
					{
						CountError("LoadFile: the DLL thread's globals for the engine thread", lua_tostring(S, -1));
						lua_pop(S, 1);
					}
					else
						SrvLog("remote: LoadFile(%s) on engine thread %u (%s): the DLL thread's own globals were set there "
						       "first (issue 0315)", szFilename ? szFilename : "", t->engineId, t->name.c_str());
				}
			}
			vpipc::Channel& ch = SrvRt::Chan();
			vpipc::Writer& w = SrvRt::Begin(ch, kM_ICvEngineScriptSystem1_LoadFile);
			w.handle(m_h);
			w.u32(t->engineId);
			w.str(szFilename);
			vpipc::Reader r;
			if(!SrvRt::End(ch, kM_ICvEngineScriptSystem1_LoadFile, r))
				return false;
			const bool b = r.b();
			SrvRt::DecodeCheck(kM_ICvEngineScriptSystem1_LoadFile, r);
			return b;
		}

		bool CallFunction(lua_State* S, unsigned int nArgs, unsigned int nRet) override
		{
			SrvRemote::NoteEngineCall();	// issue 2801: answered here, still an engine method (D30 conditions 2, 4)
			bool ok = true;
			if(LuaPCall(S, (int)nArgs, (int)nRet) != 0)
			{
				CountError("CallFunction", lua_tostring(S, -1));
				lua_pop(S, 1);
				ok = false;
			}
			FlushReleases();
			return ok;
		}

		bool CallCFunction(lua_State* S, lua_CFunction func, void* args) override
		{
			SrvRemote::NoteEngineCall();	// issue 2801
			bool ok = true;
			if(lua_cpcall(S, func, args) != 0)
			{
				CountError("CallCFunction", lua_tostring(S, -1));
				lua_pop(S, 1);
				ok = false;
			}
			FlushReleases();
			return ok;
		}

	private:
		// With the option on every args object the DLL holds came from CreateArgs above: anything else is a defect.
		LocalArgs* AsLocal(ICvEngineScriptSystemArgs1* p)
		{
			if(!p || !OneCrossing())
				return NULL;
			LocalArgs* la = static_cast<LocalArgs*>(p);
			if(!la->Valid())
				RaiseFatal("gameevents_raise: an args object the server did not make (or one already destroyed) reached "
				           "the script system");
			return la;
		}

		// The five Call*: kind, name, args; exactly one of pb, pi, pf is the in/out value.
		bool Raise(uint32_t kind, const char* szName, ICvEngineScriptSystemArgs1* args, bool* pb, int* pi, float* pf)
		{
			const uint32_t before = pb ? (*pb ? 1u : 0u) : (pi ? (uint32_t)*pi : vplr::FloatBits(*pf));
			LocalArgs* la = AsLocal(args);
			bool handled = false;
			int path;
			if(!OneCrossing() || (la && la->Materialised()))
			{
				// The old way: the generated call with the EXE's args object (n + 3 crossings in all).
				ICvEngineScriptSystemArgs1* ra = la ? la->Materialised() : args;
				path = la ? 2 : 1;
				InterlockedIncrement(&s_lRaiseOld);
				switch(kind)
				{
				case vplr::kHook: handled = RemoteGen_ICvEngineScriptSystem1::CallHook(szName, ra, *pb); break;
				case vplr::kTestAll: handled = RemoteGen_ICvEngineScriptSystem1::CallTestAll(szName, ra, *pb); break;
				case vplr::kTestAny: handled = RemoteGen_ICvEngineScriptSystem1::CallTestAny(szName, ra, *pb); break;
				case vplr::kAccumInt: handled = RemoteGen_ICvEngineScriptSystem1::CallAccumulator(szName, ra, *pi); break;
				default: handled = RemoteGen_ICvEngineScriptSystem1::CallAccumulator(szName, ra, *pf); break;
				}
			}
			else
			{
				path = 0;
				InterlockedIncrement(&s_lRaiseOne);
				const uint32_t n = la ? (uint32_t)la->Values().size() : 0u;
				const vplr::Arg* pv = n ? &la->Values()[0] : NULL;
				uint32_t flags = 0, reserve = 0;
				if(la)
				{
					flags |= vplr::kFlagArgs;
					unsigned int n = 0;
					if(la->Reserve(n))
					{
						flags |= vplr::kFlagReserve;
						reserve = (uint32_t)n;
					}
					if(la->NoteRaise() == 2)
						InterlockedIncrement(&s_lReraise);	// design 3.6: a fresh EXE object for the second raise
				}
				else
					InterlockedIncrement(&s_lNoArgs);
				const char* name = szName ? szName : "";
				if(n > vplr::kMaxArgs || vplr::CallBytes(name, pv, n) > vplr::kMaxBytes)
					RaiseFatal("gameevents_raise: %s(%s) has %u arguments (%llu bytes): above kRtE_LuaRaise's limits (%u "
					           "arguments, %u bytes)", vplr::KindName(kind), name, n,
					           (unsigned long long)vplr::CallBytes(name, pv, n), vplr::kMaxArgs, vplr::kMaxBytes);
				SrvDbFlush();	// the DLL's pending database writes reach the engine first (a listener may read them)
				vpipc::Channel& ch = SrvRt::Chan();
				vpipc::Writer& w = SrvRt::Begin(ch, vpw::kRtE_LuaRaise);	// NoteEngineCall (D30 conditions 2 and 4)
				vplr::Encode(w, kind, flags, reserve, name, before, pv, n);
				vpipc::Reader r;
				if(!SrvRt::End(ch, vpw::kRtE_LuaRaise, r))	// End syncs the lock depth (e7ea828)
					RaiseFatal("gameevents_raise: kRtE_LuaRaise %s(%s) failed (see server.log): the EXE did not run "
					           "the event", vplr::KindName(kind), name);
				handled = r.u32() != 0;
				const uint32_t after = r.u32();
				SrvRt::DecodeCheck(vpw::kRtE_LuaRaise, r);
				if(!r.ok())
					RaiseFatal("gameevents_raise: kRtE_LuaRaise %s(%s): short reply", vplr::KindName(kind), name);
				if(pb)
					*pb = after != 0;
				else if(pi)
					*pi = (int)after;
				else
					*pf = vplr::BitsFloat(after);
			}
			const uint32_t now = pb ? (*pb ? 1u : 0u) : (pi ? (uint32_t)*pi : vplr::FloatBits(*pf));
			CountRaise(szName, kind, path, handled, now != before);
			return handled;
		}
	};

	class RemoteArgs : public RemoteGen_ICvEngineScriptSystemArgs1
	{
	public:
		explicit RemoteArgs(vpipc::Handle h) : RemoteGen_ICvEngineScriptSystemArgs1(h) {}

		// The string stays alive in this object until DestroyArgs (EngineForget deletes it).
		bool GetString(unsigned int argIndex, char*& szValue) override
		{
			szValue = NULL;
			vpipc::Channel& ch = SrvRt::Chan();
			vpipc::Writer& w = SrvRt::Begin(ch, kM_ICvEngineScriptSystemArgs1_GetString);
			w.handle(m_h);
			w.u32(argIndex);
			vpipc::Reader r;
			if(!SrvRt::End(ch, kM_ICvEngineScriptSystemArgs1_GetString, r))
				return false;
			const bool b = r.b();
			uint32_t n = 0;
			const char* s = r.cstr(&n);
			SrvRt::DecodeCheck(kM_ICvEngineScriptSystemArgs1_GetString, r);
			if(s)
			{
				m_strings.push_back(std::string(s, n));
				szValue = &m_strings.back()[0];
			}
			return b;
		}

	private:
		std::list<std::string> m_strings;
	};
}  // namespace

void* SrvPatchImport(HMODULE hModule, const char* szDll, const char* szFunc, void* pHook);	// srv_remote_db.cpp

// After kRt_Boot loaded the DLL (issue 2504): the DLL's luaL_loadstring import slot (its x64 image only).
void SrvLuaInstall(HMODULE hDll)
{
	// Issue 0313: the DLL's image, where LearnWrapper looks for master's wrapper closure.
	if(hDll)
	{
		const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(hDll);
		const IMAGE_NT_HEADERS* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const BYTE*>(hDll) + dos->e_lfanew);
		if(dos->e_magic == IMAGE_DOS_SIGNATURE && nt->Signature == IMAGE_NT_SIGNATURE)
		{
			s_dllLo = (uintptr_t)hDll;
			s_dllHi = s_dllLo + nt->OptionalHeader.SizeOfImage;
		}
	}
	o_loadstring = reinterpret_cast<PLoadString>(SrvPatchImport(hDll, "lua51_x64.dll", "luaL_loadstring", (void*)&HookLoadString));
	SrvRemote::Report("lua: luaL_loadstring of the DLL %s: chunks it loads on its Lua threads run in the engine VM",
	                  o_loadstring ? "hooked" : "NOT imported (nothing to redirect)");
}

ICvEngineScriptSystem1* VpNewRemoteScriptSystem(vpipc::Handle h) { return new RemoteScriptSystem(h); }
ICvEngineScriptSystemArgs1* VpNewRemoteArgs(vpipc::Handle h) { return new RemoteArgs(h); }
void VpDeleteRemoteArgs(ICvEngineScriptSystemArgs1* p) { delete static_cast<RemoteArgs*>(p); }

// =====================================================================================================================
// Ticket 0422: the kRt_LuaCall this thread serves, for the dll-fatal report (srv_dll_fatal.cpp). Two pointers per call;
// the text is made only when the DLL stops. The arguments are read again from the call's private copy (OnCall's
// argsCopy), which lives as long as the call.

namespace
{
	struct LuaCallRec
	{
		uint32_t statId, chan;
		const uint8_t* args;  // at the nargs word, after the callee
		uint32_t len;
		LuaCallRec* prev;
	};
	__declspec(thread) LuaCallRec* t_pLuaCall = NULL;
	struct LuaCallScope
	{
		LuaCallRec rec;
		LuaCallScope(uint32_t statId, uint32_t chan, const uint8_t* p, uint32_t n)
		{
			rec.statId = statId;
			rec.chan = chan;
			rec.args = p;
			rec.len = n;
			rec.prev = t_pLuaCall;
			t_pLuaCall = &rec;
		}
		~LuaCallScope() { t_pLuaCall = rec.prev; }
	};

	// One argument as the report names it; false when the wire value is malformed.
	bool DescribeValue(vpipc::Reader& r, uint32_t depth, std::string* out)
	{
		const uint32_t tag = r.u32();
		if(!r.ok() || depth > vpw::kLuaMaxDepth)
			return false;
		char sz[64];
		switch(tag)
		{
		case vpw::kLNil: *out = "nil"; return true;
		case vpw::kLFalse: *out = "false"; return true;
		case vpw::kLTrue: *out = "true"; return true;
		case vpw::kLNumber:
			sprintf_s(sz, "%.15g", r.f64());
			*out = sz;
			return r.ok();
		case vpw::kLString:
		{
			uint32_t n = 0;
			const char* s = r.cstr(&n);
			std::string v = s ? std::string(s, n < 60 ? n : 60) : std::string();
			for(size_t i = 0; i < v.size(); ++i)
				if((unsigned char)v[i] < 0x20)
					v[i] = ' ';
			*out = "\"" + v + (n > 60 ? "...\"" : "\"");
			return r.ok();
		}
		case vpw::kLTable:
		{
			const uint32_t n = r.u32();
			std::string ignored;
			for(uint32_t i = 0; i < n && r.ok(); ++i)
				if(!DescribeValue(r, depth + 1, &ignored) || !DescribeValue(r, depth + 1, &ignored))
					return false;
			sprintf_s(sz, "a table (%u entries)", n);
			*out = sz;
			return r.ok();
		}
		case vpw::kLCFunc: r.u32(); *out = "a DLL function"; return r.ok();
		case vpw::kLSRef: r.u32(); r.u32(); *out = "a server-side value"; return r.ok();
		case vpw::kLInstance:
		{
			uint32_t n = 0;
			const char* s = r.cstr(&n);
			r.u64();
			*out = std::string("a ") + (s ? std::string(s, n < 40 ? n : 40) : std::string("?")) + " object";
			return r.ok();
		}
		case vpw::kLERef: r.u32(); r.u32(); *out = "an engine-side value"; return r.ok();
		case vpw::kLUnsupported:
		{
			uint32_t n = 0;
			const char* s = r.cstr(&n);
			*out = std::string("an unsupported ") + (s ? std::string(s, n < 40 ? n : 40) : std::string("value"));
			return r.ok();
		}
		default: return false;
		}
	}
}  // namespace

// =====================================================================================================================
// Runtime methods kRt_Lua*

namespace SrvRemote
{
std::string LuaCallText()
{
	const LuaCallRec* rec = t_pLuaCall;
	if(!rec)
		return std::string();
	std::string name;
	if(rec->statId == kLuaStatSRef)
		name = "a server-side Lua function";
	else
	{
		// try-lock: the stopped thread may never hold s_lockF here, but a report must not wait on another thread
		if(TryAcquireSRWLockShared(&s_lockF))
		{
			if(rec->statId < s_funcNames.size() && !s_funcNames[rec->statId].empty())
				name = s_funcNames[rec->statId];
			ReleaseSRWLockShared(&s_lockF);
		}
		if(name.empty())
		{
			char sz[64];
			sprintf_s(sz, "DLL Lua function #%u (no name recorded)", rec->statId);
			name = sz;
		}
	}
	vpipc::Reader r;
	r.reset(rec->args, rec->len);
	const uint32_t n = r.u32() & 0x1FFFFFFFu;
	std::string args;
	for(uint32_t i = 0; i < n && i < 8 && r.ok(); ++i)
	{
		std::string v;
		if(!DescribeValue(r, 0, &v))
		{
			args += i ? ", ?" : "?";
			break;
		}
		args += (i ? ", " : "") + v;
	}
	if(n > 8)
		args += ", ...";
	static const char* const chans[3] = {"ui", "core", "aux"};
	int depth = 0;
	for(const LuaCallRec* p = rec->prev; p; p = p->prev)
		++depth;
	char tail[160];
	sprintf_s(tail, ") on the %s channel%s", rec->chan < 3 ? chans[rec->chan] : "?",
	          depth ? " (inside an outer Lua call)" : "");
	return name + "(" + args + tail;
}

long LuaScratchErrors() { return s_lErrors; }

void WriteLuaCallTable(FILE* report);  // issue 2805, below
void WriteRaiseTable(FILE* report);    // ticket 4804, below

void LuaReport(FILE* f)
{
	EnterCriticalSection(&s_csLog);
	fprintf(f, "Lua: %ld kRt_LuaCall, %ld scratch Lua errors, %ld values that could not cross, %ld bad references, "
	           "%ld unknown scratch threads\n", s_lLuaCalls, s_lErrors, s_lUnsupported, s_lBadRefs, s_lUnknownThreads);
	fprintf(f, "Lua: %ld chunks of the DLL's Lua threads ran in the engine VM (kRtE_LuaRunChunk), %ld failed; "
	           "luaL_loadstring hook %s\n", s_lChunks, s_lChunkErrors, o_loadstring ? "installed" : "NOT installed");
	fprintf(f, "Lua: %ld RegisterScriptLibraries calls on a scratch state registered no global and were repeated "
	           "(issue 0308)\n", s_lEmptyRegistrations);
	fprintf(f, "Lua: master-form instances (issue 0314): sent %ld (statics %ld), not confirmed by the DLL's cache %ld; "
	           "received %ld from the cache, %ld not live in it: %ld found again by identity, %ld passed invalidated, %ld "
	           "nil\n", s_lMasterEnc, s_lMasterStaticEnc, s_lMasterNotCached, s_lMasterDec, s_lMasterMiss,
	        s_lMasterResolved, s_lMasterDead, s_lMasterLost);
	fprintf(f, "Lua: engine calls the DLL made during RegisterScriptLibraries (issue 0315): kept %ld, sent to the proxy "
	           "with replies %ld, made at once %ld\n", s_lPostCallsKept, s_lPostCallsSent, s_lPostCallsNow);
	{
		// Ticket 0576: the stale-instance guard's counters and its mean cost per call that checked an instance.
		SrvLuaStale::Counts c;
		SrvLuaStale::GetCounts(&c);
		LARGE_INTEGER qf;
		QueryPerformanceFrequency(&qf);
		const double ns = c.calls && qf.QuadPart ? 1e9 * (double)c.ticks / (double)qf.QuadPart / (double)c.calls : 0.0;
		fprintf(f, "Lua: stale-instance guard (ticket 0576)%s: %lld Unit and %lld City instances checked in %lld calls "
		           "(mean %.0f ns per call); stale %lld Unit (%lld unreadable), %lld City (%lld unreadable); %ld calls "
		           "not made\n", SrvLuaStale::Enabled() ? "" : " OFF (VP64_LUA_STALE_GUARD=0)", c.checked[0], c.checked[1],
		        c.calls, ns, c.stale[0], c.faulted[0], c.stale[1], c.faulted[1], s_lStaleCalls);
	}
	for(std::map<std::string, long>::const_iterator it = s_unsupportedKinds.begin(); it != s_unsupportedKinds.end(); ++it)
		fprintf(f, "Lua: not carried: %s: %ld\n", it->first.c_str(), it->second);
	for(size_t i = 0; i < s_errors.size(); ++i)
		fprintf(f, "Lua error (scratch state): %s\n", s_errors[i].c_str());
	LeaveCriticalSection(&s_csLog);
	WriteLuaCallTable(f);
	WriteRaiseTable(f);
}

// Ticket 4804 (design section 3.5): gameevents-raises.tsv beside proxy-calls.tsv, one row per (event name, kind) with
// raises, handled (a listener exists in the EXE), value_changed (the in/out value differs after the call) and the
// raises by path (one crossing, the old way with the option off, materialised by a getter). Both arms of the option
// write it; for one game they agree on name, kind, raises, handled and value_changed.
void WriteRaiseTable(FILE* report)
{
	long long total[3] = {0, 0, 0}, handled = 0;
	const std::string path = std::string(SrvRunDir()) + "\\gameevents-raises.tsv";
	FILE* f = NULL;
	const bool bFile = !fopen_s(&f, path.c_str(), "w") && f;
	if(bFile)
		fprintf(f, "name\tkind\traises\thandled\tvalue_changed\tone\told\tmaterialised\n");
	AcquireSRWLockShared(&s_lockRaise);
	for(std::map<std::pair<const char*, uint32_t>, RaiseRow, NameLess>::const_iterator it = s_raises.begin();
	    it != s_raises.end(); ++it)
	{
		const RaiseRow& r = it->second;
		for(int p = 0; p < 3; ++p)
			total[p] += r.raises[p];
		handled += r.handled;
		if(bFile)
			fprintf(f, "%s\t%s\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\n", it->first.first, vplr::KindName(it->first.second),
			        r.raises[0] + r.raises[1] + r.raises[2], r.handled, r.changed, r.raises[0], r.raises[1], r.raises[2]);
	}
	const size_t names = s_raises.size();
	ReleaseSRWLockShared(&s_lockRaise);
	if(f)
		fclose(f);
	fprintf(report, "GameEvents (ticket 4804): GameEventsOneCrossing %s; %lld raises (one crossing %lld, old way %lld, "
	                "materialised %lld), %lld handled, %zu (name, kind) pairs; %ld args objects materialised by a getter, "
	                "%ld second raises of one args object, %ld raises without args; proxy Lua bridge version %ld; %s %s\n",
	        s_iOneCrossing == 1 ? "on" : (s_iOneCrossing == 0 ? "off" : "not read"), total[0] + total[1] + total[2],
	        total[0], total[1], total[2], handled, names, s_lMaterialised, s_lReraise, s_lNoArgs, s_uProxyBridge,
	        bFile ? "written to" : "cannot write", path.c_str());
}

// Issue 2805: proxy-lua-calls.tsv beside proxy-calls.tsv, one row per (Lua function, channel, phase) with calls, sorted
// by calls. Columns: function (the Lua path the DLL registered it under; "(server-side closure or userdata)" for kLSRef
// callees, "(kRt_LuaIndex)" for indexing a server-side value, "(id N, no name)" when the function was never seen under
// a name), id, aliases (further names of the same C function), channel (ui/core/aux), phase (open = the proxy's epoch
// mirror was open: no impure call in flight, an idle UI read; in_flight = an impure call was in flight, e.g. an AI turn
// inside ICvGame1::Update: no epoch mirror can answer then; unknown = local answers off or kRt_LuaIndex), calls,
// seconds (the server's handler time: decode, the DLL's C function, encode; the round trip adds the transit of
// kRt_LuaCall, proxy-calls.tsv against vpproxy-calls.tsv), bytes_in, bytes_out, us_per_call, would_hit (calls the
// proxy marked: with lua_mirror=0 the UI-Lua mirror would have answered them; with lua_mirror=1 and local_verify=1 they
// are the verifying reads of mirror hits; the hits themselves never reach the server).
void WriteLuaCallTable(FILE* report)
{
	LARGE_INTEGER fq;
	QueryPerformanceFrequency(&fq);
	const double freq = fq.QuadPart ? (double)fq.QuadPart : 1.0;
	struct Row
	{
		LONG64 calls, ticks, bin, bout, would;
		uint32_t id, chan, phase;
	};
	std::vector<Row> rows;
	LONG64 total = 0, byPhase[3] = {};
	for(uint32_t id = 0; id < kLuaStatIds; ++id)
		for(uint32_t c = 0; c < 3; ++c)
			for(uint32_t p = 0; p < 3; ++p)
			{
				const LuaStat& s = s_luaStat[id][c][p];
				if(!s.calls)
					continue;
				Row r = {s.calls, s.ticks, s.bytesIn, s.bytesOut, s.would, id, c, p};
				rows.push_back(r);
				total += s.calls;
				byPhase[p] += s.calls;
			}
	struct ByCalls
	{
		bool operator()(const Row& a, const Row& b) const { return a.calls != b.calls ? a.calls > b.calls : a.id < b.id; }
	};
	std::sort(rows.begin(), rows.end(), ByCalls());
	const std::string path = std::string(SrvRunDir()) + "\\proxy-lua-calls.tsv";
	FILE* f = NULL;
	if(fopen_s(&f, path.c_str(), "w") || !f)
	{
		fprintf(report, "Lua per function (issue 2805): cannot write %s\n", path.c_str());
		return;
	}
	static const char* const kChan[3] = {"ui", "core", "aux"};
	static const char* const kPhase[3] = {"unknown", "open", "in_flight"};
	fprintf(f, "function\tid\taliases\tchannel\tphase\tcalls\tseconds\tbytes_in\tbytes_out\tus_per_call\twould_hit\n");
	AcquireSRWLockShared(&s_lockF);
	for(size_t i = 0; i < rows.size(); ++i)
	{
		const Row& r = rows[i];
		char nm[64];
		std::string name;
		uint32_t aliases = 0;
		if(r.id == kLuaStatSRef)
			name = "(server-side closure or userdata)";
		else if(r.id == kLuaStatIndex)
			name = "(kRt_LuaIndex)";
		else if(r.id == kLuaStatOver)
			name = "(function id beyond the table)";
		else if(r.id < s_funcNames.size() && !s_funcNames[r.id].empty())
		{
			name = s_funcNames[r.id];
			aliases = s_funcAliases[r.id];
		}
		else
		{
			sprintf_s(nm, "(id %u, no name)", r.id);
			name = nm;
		}
		const double sec = (double)r.ticks / freq;
		fprintf(f, "%s\t%u\t%u\t%s\t%s\t%lld\t%.6f\t%lld\t%lld\t%.3f\t%lld\n", name.c_str(), r.id, aliases, kChan[r.chan],
		        kPhase[r.phase], (long long)r.calls, sec, (long long)r.bin, (long long)r.bout,
		        r.calls ? sec * 1e6 / (double)r.calls : 0.0, (long long)r.would);
	}
	ReleaseSRWLockShared(&s_lockF);
	fclose(f);
	fprintf(report, "Lua per function (issue 2805): %lld calls in %zu rows (phase open %lld, in_flight %lld, unknown %lld) "
	                "written to %s\n", (long long)total, rows.size(), (long long)byPhase[1], (long long)byPhase[2],
	        (long long)byPhase[0], path.c_str());
	// Issue 0313: every name recorded for a DLL Lua function (name, id, wrapped = 1 for a closure of master's wrapper),
	// called or not, so a check can find the functions it expects (for example lua-pure-bindings.tsv's rows).
	const std::string npath = std::string(SrvRunDir()) + "\\proxy-lua-names.tsv";
	FILE* nf = NULL;
	if(fopen_s(&nf, npath.c_str(), "w") || !nf)
	{
		fprintf(report, "Lua function names (issue 0313): cannot write %s\n", npath.c_str());
		return;
	}
	fprintf(nf, "name\tid\twrapped\n");
	size_t nNames = 0, nWrapped = 0;
	AcquireSRWLockShared(&s_lockF);
	for(std::map<std::string, uint32_t>::const_iterator it = s_funcAllNames.begin(); it != s_funcAllNames.end(); ++it)
	{
		const bool wrapped = it->second < s_funcs.size() && s_funcs[it->second].wrap != NULL;
		fprintf(nf, "%s\t%u\t%d\n", it->first.c_str(), it->second, wrapped ? 1 : 0);
		++nNames;
		nWrapped += wrapped ? 1 : 0;
	}
	ReleaseSRWLockShared(&s_lockF);
	fclose(nf);
	fprintf(report, "Lua function names (issue 0313): %zu names (%zu of closures of the DLL's wrapper; wrapper %s) written "
	                "to %s\n", nNames, nWrapped, s_wrapper ? "learnt" : "none", npath.c_str());
}

bool HandleLuaRt(uint32_t method, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	switch(method)
	{
	case vpw::kRt_LuaRegister:
	{
		// Ticket 4804: the proxy's Lua bridge version (none from a proxy older than kRtE_LuaRaise). Refused when it differs,
		// or when it is missing while [Performance] GameEventsOneCrossing = 1 (that proxy cannot serve kRtE_LuaRaise).
		const uint32_t uBridge = args.remaining() >= 4 ? args.u32() : 0u;
		if(InterlockedExchange(&s_uProxyBridge, (long)uBridge) != (long)uBridge)
			SrvLog("remote: the proxy's Lua bridge version: %u (the server's: %u)", uBridge, vplr::kVersion);
		if(uBridge != 0 && uBridge != vplr::kVersion)
			RaiseFatal("lua_bridge_version: the proxy's Lua bridge is version %u, the server's %u (a proxy and a server of "
			           "different builds)", uBridge, vplr::kVersion);
		if(uBridge == 0 && OneCrossing())
			RaiseFatal("lua_bridge_version: [Performance] GameEventsOneCrossing = 1 but the proxy sent no Lua bridge "
			           "version (a proxy older than kRtE_LuaRaise, version %u)", vplr::kVersion);
		LuaCtx* ctx = RegisteredCtx();
		lua_State* L = ctx->L;
		const int top = lua_gettop(L);
		// The globals RegisterScriptLibraries created or changed: compared with a fresh state (stock libraries).
		lua_State* F = luaL_newstate();
		luaL_openlibs(F);
		std::vector<std::string> names;
		lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->globalsRef);
		const int g = lua_gettop(L);
		lua_pushnil(L);
		while(lua_next(L, g))
		{
			if(lua_type(L, -2) == LUA_TSTRING)
			{
				const char* k = lua_tostring(L, -2);
				lua_getglobal(F, k);
				const int tf = lua_type(F, -1), tl = lua_type(L, -1);
				bool changed = tf != tl;
				if(!changed)
				{
					switch(tl)
					{
					case LUA_TNUMBER: changed = lua_tonumber(F, -1) != lua_tonumber(L, -1); break;
					case LUA_TBOOLEAN: changed = lua_toboolean(F, -1) != lua_toboolean(L, -1); break;
					case LUA_TSTRING: changed = strcmp(lua_tostring(F, -1), lua_tostring(L, -1)) != 0; break;
					case LUA_TFUNCTION: changed = lua_tocfunction(F, -1) != lua_tocfunction(L, -1); break;
					default: break;	// a table of the stock libraries: treated as unchanged
					}
				}
				lua_pop(F, 1);
				if(changed)
					names.push_back(k);
			}
			lua_pop(L, 1);
		}
		lua_close(F);
		RebuildTypeNames(ctx);
		// Issue 0313: on master the proxy cannot learn the functions' names where it builds the thunks (Game, Map and
		// the instance types are userdata whose function tables stay here), so the first non-empty reply of the process
		// carries one more entry, kLuaFuncNamesKey = {name = kLCFunc id, ...}: every name NameFuncs recorded. The proxy
		// takes the names from it and never sets it as a global. At the pin (no wrapper learnt) nothing changes.
		const bool sendNames = s_wrapper && !names.empty() && !InterlockedExchange(&s_lNamesSent, 1);
		// Issue 0315: master's types and the kept engine calls, with every non-empty reply (none at the pin).
		const bool sendTypes = !names.empty() && !ctx->masterByMt.empty();
		const bool sendPost = !names.empty() && !ctx->postCalls.empty();
		ret.u32((uint32_t)names.size() + (sendNames ? 1u : 0u) + (sendTypes ? 1u : 0u) + (sendPost ? 1u : 0u));
		for(size_t i = 0; i < names.size(); ++i)
		{
			vpipc::write_string(ret, names[i]);
			lua_getfield(L, g, names[i].c_str());
			EncodeTop(L, -1, ret, ctx);
			lua_pop(L, 1);
		}
		size_t nNames = 0;
		if(sendNames)
		{
			vpipc::write_string(ret, std::string(kLuaFuncNamesKey));
			AcquireSRWLockShared(&s_lockF);
			ret.u32(vpw::kLTable);
			ret.u32((uint32_t)s_funcAllNames.size());
			for(std::map<std::string, uint32_t>::const_iterator it = s_funcAllNames.begin(); it != s_funcAllNames.end(); ++it)
			{
				ret.u32(vpw::kLString);
				ret.str(it->first.c_str(), (uint32_t)it->first.size());
				ret.u32(vpw::kLCFunc);
				ret.u32(it->second);
			}
			nNames = s_funcAllNames.size();
			ReleaseSRWLockShared(&s_lockF);
		}
		if(sendTypes)
		{
			vpipc::write_string(ret, std::string(kLuaDllTypesKey));
			ret.u32(vpw::kLTable);
			ret.u32((uint32_t)ctx->masterByMt.size());
			uint32_t i = 0;
			for(std::map<const void*, MasterType>::const_iterator it = ctx->masterByMt.begin(); it != ctx->masterByMt.end();
			    ++it)
			{
				ret.u32(vpw::kLNumber);
				ret.f64((double)++i);
				ret.u32(vpw::kLTable);
				ret.u32(3);
				ret.u32(vpw::kLString);
				ret.str("tag", 3);
				ret.u32(vpw::kLNumber);
				ret.f64((double)it->second.tag);
				ret.u32(vpw::kLString);
				ret.str("hint", 4);
				ret.u32(vpw::kLString);
				ret.str(it->second.hint.c_str(), (uint32_t)it->second.hint.size());
				ret.u32(vpw::kLString);
				ret.str("name", 4);
				ret.u32(vpw::kLString);
				ret.str(it->second.name.c_str(), (uint32_t)it->second.name.size());
			}
		}
		if(sendPost)
		{
			vpipc::write_string(ret, std::string(kLuaPostCallsKey));
			ret.u32(vpw::kLTable);
			ret.u32((uint32_t)ctx->postCalls.size());
			for(size_t i = 0; i < ctx->postCalls.size(); ++i)
			{
				const PostCall& pc = ctx->postCalls[i];
				ret.u32(vpw::kLNumber);
				ret.f64((double)(i + 1));
				ret.u32(vpw::kLTable);
				ret.u32(4);
				ret.u32(vpw::kLString);
				ret.str("thread", 6);
				ret.u32(vpw::kLNumber);
				ret.f64((double)pc.thread);
				ret.u32(vpw::kLString);
				ret.str("name", 4);
				ret.u32(vpw::kLString);
				ret.str(pc.name.c_str(), (uint32_t)pc.name.size());
				ret.u32(vpw::kLString);
				ret.str("n", 1);
				ret.u32(vpw::kLNumber);
				ret.f64((double)pc.args.size());
				ret.u32(vpw::kLString);
				ret.str("args", 4);
				ret.u32(vpw::kLTable);
				ret.u32((uint32_t)pc.args.size());
				for(size_t a = 0; a < pc.args.size(); ++a)
				{
					const PostArg& v = pc.args[a];
					ret.u32(vpw::kLNumber);
					ret.f64((double)(a + 1));
					switch(v.type)
					{
					case LUA_TBOOLEAN: ret.u32(v.num != 0 ? vpw::kLTrue : vpw::kLFalse); break;
					case LUA_TNUMBER: ret.u32(vpw::kLNumber); ret.f64(v.num); break;
					case LUA_TSTRING: ret.u32(vpw::kLString); ret.str(v.str.c_str(), (uint32_t)v.str.size()); break;
					default: ret.u32(vpw::kLNil); break;
					}
				}
			}
			InterlockedExchangeAdd(&s_lPostCallsSent, (long)ctx->postCalls.size());
		}
		lua_settop(L, top);
		Report("lua: kRt_LuaRegister on channel %u: %zu globals", ctx->chan, names.size());
		if(sendNames)
			Report("lua: %zu DLL function names sent to the proxy with kRt_LuaRegister (issue 0313)", nNames);
		return true;
	}
	case vpw::kRt_LuaDescribeType:
	{
		const std::string name = vpipc::read_string(args);
		if(!args.ok())
			return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "kRt_LuaDescribeType: bad arguments");
		LuaCtx* ctx = RegisteredCtx();
		lua_State* L = ctx->L;
		const int top = lua_gettop(L);
		PushMainGlobal(L, ctx, "LuaTypes");
		if(lua_istable(L, -1))
			lua_getfield(L, -1, name.c_str());
		else
			lua_pushnil(L);
		// Issue 0314: a type of master's form (no LuaTypes there): the first member kFormKey = "userdata" tells the proxy
		// to make its instances userdata; the other members are the __meta.__index function table's.
		bool master = false;
		if(!lua_istable(L, -1) && ctx->masterByName.count(name))
		{
			lua_pop(L, 1);
			PushInternalsType(L, ctx->masterByMt[ctx->masterByName[name]].tag);
			if(lua_istable(L, -1))
			{
				lua_getfield(L, -1, "__meta");
				lua_remove(L, -2);
				if(lua_istable(L, -1))
				{
					lua_getfield(L, -1, "__index");
					lua_remove(L, -2);
				}
			}
			master = lua_istable(L, -1);
		}
		const int t = lua_gettop(L);
		std::vector<std::string> keys;
		if(master)
			keys.push_back(kFormKey);
		if(lua_istable(L, t))
		{
			lua_pushnil(L);
			while(lua_next(L, t))
			{
				if(lua_type(L, -2) == LUA_TSTRING)
				{
					const char* k = lua_tostring(L, -2);
					if(strcmp(k, "__instances") && strcmp(k, "__index"))
						keys.push_back(k);
				}
				lua_pop(L, 1);
			}
		}
		else
			SrvLog("remote: kRt_LuaDescribeType(%s): no such type in LuaTypes", name.c_str());
		ret.u32((uint32_t)keys.size());
		for(size_t i = 0; i < keys.size(); ++i)
		{
			vpipc::write_string(ret, keys[i]);
			if(master && i == 0)
			{
				ret.u32(vpw::kLString);
				ret.str("userdata", 8);
				continue;
			}
			lua_getfield(L, t, keys[i].c_str());
			EncodeTop(L, -1, ret, ctx);
			lua_pop(L, 1);
		}
		lua_settop(L, top);
		return true;
	}
	case vpw::kRt_LuaCall:
	{
		InterlockedIncrement(&s_lLuaCalls);
		// Issue 2805: the callee's function id (peeked; kLSRef callees share one row) and the handler's time.
		LARGE_INTEGER q0;
		QueryPerformanceCounter(&q0);
		const uint32_t bytesIn = args.remaining();
		uint32_t statId = kLuaStatSRef, calleeFid = 0;
		{
			vpipc::Reader peek = args;
			if(peek.u32() == vpw::kLCFunc)
			{
				const uint32_t fid = peek.u32();
				statId = peek.ok() && fid != 0 && fid < kLuaStatIndex ? fid : kLuaStatOver;
				calleeFid = peek.ok() ? fid : 0;
			}
		}
		LuaCtx* ctx = RegisteredCtx();
		lua_State* L = ctx->L;
		const int top = lua_gettop(L);
		lua_checkstack(L, 64);
		DecodeValue(L, args, ctx, 0, 0);
		const LuaCallScope luaCallScope(statId, ctx->chan, args.cursor(), args.remaining());  // ticket 0422
		// Issue 2805: bits 30-31 of the nargs word carry the proxy's phase (see LuaStatAdd), bit 29 that the UI-Lua
		// mirror would have answered this call (lua_mirror=0) or that it is the verifying read of a mirror hit
		// (lua_mirror=1, local_verify=1); the count is below 2^29.
		const uint32_t nWord = args.u32();
		const uint32_t n = nWord & 0x1FFFFFFFu;
		const uint32_t phase = nWord >> 30;
		const bool would = ((nWord >> 29) & 1u) != 0;
		for(uint32_t i = 0; i < n && args.ok(); ++i)
		{
			lua_checkstack(L, 8);
			DecodeValue(L, args, ctx, 0, 0);
		}
		if(!args.ok())
		{
			lua_settop(L, top);
			return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "kRt_LuaCall: bad arguments");
		}
		// Ticket 0576: a Unit or City instance whose DLL object is gone never reaches the DLL: the script gets nil.
		if(n && StaleArgs(ctx, top + 2, top + 1 + (int)n, calleeFid, NULL))
		{
			lua_settop(L, top);
			FlushReleases();
			ret.u32(0);
			ret.u32(1);
			ret.u32(vpw::kLNil);
			LARGE_INTEGER qs;
			QueryPerformanceCounter(&qs);
			LuaStatAdd(statId, ctx->chan, phase, qs.QuadPart - q0.QuadPart, bytesIn, ret.size(), would);
			return true;
		}
		const int rc = LuaPCall(L, (int)n, LUA_MULTRET);
		FlushReleases();
		if(rc != 0)
		{
			const std::string msg = lua_tostring(L, -1) ? lua_tostring(L, -1) : "(non-string error)";
			CountError("kRt_LuaCall", msg.c_str());
			ret.u32(1);
			vpipc::write_string(ret, msg);
		}
		else
		{
			const int nres = lua_gettop(L) - top;
			ret.u32(0);
			ret.u32((uint32_t)nres);
			for(int i = 1; i <= nres; ++i)
				EncodeTop(L, top + i, ret, ctx);
		}
		lua_settop(L, top);
		LARGE_INTEGER q1;
		QueryPerformanceCounter(&q1);
		LuaStatAdd(statId, ctx->chan, phase, q1.QuadPart - q0.QuadPart, bytesIn, ret.size(), would);
		return true;
	}
	case vpw::kRt_LuaIndex:
	{
		LARGE_INTEGER q0;  // issue 2805
		QueryPerformanceCounter(&q0);
		const uint32_t bytesIn = args.remaining();
		LuaCtx* ctx = RegisteredCtx();
		lua_State* L = ctx->L;
		const int top = lua_gettop(L);
		const uint32_t id = args.u32();
		lua_pushcfunction(L, IndexTramp);
		if((id >> 24) == ctx->chan + 1u)
			lua_rawgeti(L, LUA_REGISTRYINDEX, (int)(id & 0x00FFFFFFu));
		else
		{
			InterlockedIncrement(&s_lBadRefs);
			SrvLog("remote: LOUD: kRt_LuaIndex: server value 0x%08x is not of channel %u", id, ctx->chan);
			lua_pushnil(L);
		}
		DecodeValue(L, args, ctx, 0, 0);
		if(!args.ok())
		{
			lua_settop(L, top);
			return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "kRt_LuaIndex: bad arguments");
		}
		if(LuaPCall(L, 2, 1) != 0)
		{
			CountError("kRt_LuaIndex", lua_tostring(L, -1));
			lua_pop(L, 1);
			lua_pushnil(L);
		}
		EncodeTop(L, -1, ret, ctx);
		lua_settop(L, top);
		LARGE_INTEGER q1;
		QueryPerformanceCounter(&q1);
		LuaStatAdd(kLuaStatIndex, ctx->chan, 0, q1.QuadPart - q0.QuadPart, bytesIn, ret.size());
		return true;
	}
	case vpw::kRt_LuaRelease:
	{
		LuaCtx* ctx = Ctx();
		const uint32_t n = args.u32();
		for(uint32_t i = 0; i < n && args.ok(); ++i)
		{
			const uint32_t id = args.u32();
			if(args.ok() && (id >> 24) == ctx->chan + 1u)
				luaL_unref(ctx->L, LUA_REGISTRYINDEX, (int)(id & 0x00FFFFFFu));
			else if(args.ok())
				InterlockedIncrement(&s_lBadRefs);
		}
		return true;
	}
	default:
		return false;
	}
}
}  // namespace SrvRemote

// =====================================================================================================================
// ICvScriptSystemUtility1 hooks (contract D). Written out one by one: the generator finds hooks by their definitions.

bool ServerHook_ICvScriptSystemUtility1_PushCvCityInstance(ICvScriptSystemUtility1* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	return HookPush(self, kPushCity, kT_ICvCity1, args, ret, ch, "PushCvCityInstance");
}
bool ServerHook_ICvScriptSystemUtility1_PushCvDealInstance(ICvScriptSystemUtility1* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	return HookPush(self, kPushDeal, kT_ICvDeal1, args, ret, ch, "PushCvDealInstance");
}
bool ServerHook_ICvScriptSystemUtility1_PushCvPlotInstance(ICvScriptSystemUtility1* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	return HookPush(self, kPushPlot, kT_ICvPlot1, args, ret, ch, "PushCvPlotInstance");
}
bool ServerHook_ICvScriptSystemUtility1_PushCvUnitInstance(ICvScriptSystemUtility1* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	return HookPush(self, kPushUnit, kT_ICvUnit1, args, ret, ch, "PushCvUnitInstance");
}
bool ServerHook_ICvScriptSystemUtility1_GetCvCityInstance(ICvScriptSystemUtility1* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	return HookGet(self, kGetCity, kT_ICvCity1, args, ret, ch, "GetCvCityInstance");
}
bool ServerHook_ICvScriptSystemUtility1_GetCvDealInstance(ICvScriptSystemUtility1* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	return HookGet(self, kGetDeal, kT_ICvDeal1, args, ret, ch, "GetCvDealInstance");
}
bool ServerHook_ICvScriptSystemUtility1_GetCvPlotInstance(ICvScriptSystemUtility1* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	return HookGet(self, kGetPlot, kT_ICvPlot1, args, ret, ch, "GetCvPlotInstance");
}
bool ServerHook_ICvScriptSystemUtility1_GetCvUnitInstance(ICvScriptSystemUtility1* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	return HookGet(self, kGetUnit, kT_ICvUnit1, args, ret, ch, "GetCvUnitInstance");
}

// PushReplayFromStream: CALL self, stream in (vp_wire.h); RETURN the Lua value, then u32 consumed.
bool ServerHook_ICvScriptSystemUtility1_PushReplayFromStream(ICvScriptSystemUtility1* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	FDataStream& s = SrvRt::StreamIn(args);
	if(!args.ok())
	{
		vpipc::Writer scratch;
		SrvRt::StreamInRet(scratch, s);	// drop the stream record
		return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "PushReplayFromStream: bad arguments");
	}
	LuaCtx* ctx = RegisteredCtx();
	lua_State* L = ctx->L;
	const int top = lua_gettop(L);
	UtilArgs a = {self, kPushReplay, NULL, false, NULL, &s};
	lua_pushcfunction(L, UtilTramp);
	lua_pushlightuserdata(L, &a);
	if(LuaPCall(L, 1, 1) != 0)
	{
		CountError("PushReplayFromStream", lua_tostring(L, -1));
		lua_settop(L, top);
		lua_pushnil(L);
	}
	EncodeTop(L, -1, ret, ctx);
	lua_settop(L, top);
	SrvRt::StreamInRet(ret, s);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Tickets 0343 and 0326: ICvWorldBuilderMapLoader1's six Lua methods (LoadModData, GetMapPreview, GetMapPlayers,
// AddRandomItems, ScatterResources, ScatterGoodies). The engine calls them with one of its own lua_States, which the proxy registers
// for the call as an engine Lua thread (proxy_lua.cpp WbLuaMethod). Here they run on a scratch thread borrowed for
// that engine thread, made as CreateLuaThread makes one (its globals fetch the engine thread's globals; LoadFile goes
// to it; functions fetched from it run in the engine VM when called) but neither created nor freed in the engine.
// CALL self, u32 engine thread id, u32 flags (1: LoadModData and the engine's MapModData is a table), u32 nargs,
//      nargs x value (the engine stack).
// RETURN u32 status; 0: i32 result, u32 n, n x value (the top n values the method left); for LoadModData then the
//      values of the Globals and Plots fields it set in MapModData; 1: str message.
// LoadModData writes into the global MapModData table: it gets a fresh table here, and the proxy sets the two fields
// into the engine's table.
namespace
{
	enum WbMethod { kWbLoadModData, kWbGetMapPreview, kWbGetMapPlayers, kWbAddRandomItems, kWbScatterResources,
	                kWbScatterGoodies };
	const char* const kWbNames[] = {"LoadModData", "GetMapPreview", "GetMapPlayers", "AddRandomItems",
	                                "ScatterResources", "ScatterGoodies"};
	struct WbArgs
	{
		ICvWorldBuilderMapLoader2* self;
		WbMethod which;
	};
	int WbTramp(lua_State* S)
	{
		WbArgs* a = static_cast<WbArgs*>(lua_touserdata(S, 1));
		lua_remove(S, 1);
		switch(a->which)
		{
		case kWbLoadModData: return a->self->LoadModData(S);
		case kWbGetMapPreview: return a->self->GetMapPreview(S);
		case kWbGetMapPlayers: return a->self->GetMapPlayers(S);
		case kWbAddRandomItems: return a->self->AddRandomItems(S);
		case kWbScatterResources: return a->self->ScatterResources(S);
		case kWbScatterGoodies: return a->self->ScatterGoodies(S);
		}
		return 0;
	}

	volatile long s_alWbCalls[6];

	bool WbServe(ICvWorldBuilderMapLoader2* self, WbMethod which, vpipc::Reader& args, vpipc::Writer& ret,
	             vpipc::Channel& ch)
	{
		const uint32_t id = args.u32();
		const uint32_t flags = args.u32();
		const uint32_t nargs = args.u32();
		if(!args.ok() || id == 0 || nargs > 64)
			return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, kWbNames[which]);
		if(InterlockedIncrement(&s_alWbCalls[which]) == 1)
			SrvLog("remote: WorldBuilder %s first call (engine thread %u, %u argument(s))", kWbNames[which], id, nargs);
		SrvDbFlush();
		LuaCtx* ctx = RegisteredCtx();
		lua_State* L = ctx->L;
		lua_State* S = lua_newthread(L);
		ScratchThread t;
		t.S = S;
		t.anchor = luaL_ref(L, LUA_REGISTRYINDEX);
		t.engineId = id;
		t.name = std::string("WB:") + kWbNames[which];
		lua_newtable(S);					// S's globals
		lua_newtable(S);					// its metatable
		lua_pushnumber(S, (lua_Number)id);
		lua_pushcclosure(S, GlobalIndex, 1);
		lua_setfield(S, -2, "__index");
		lua_setmetatable(S, -2);
		lua_replace(S, LUA_GLOBALSINDEX);
		ctx->threads[S] = t;
		if(which == kWbLoadModData && (flags & 1u))
		{
			lua_pushstring(S, "MapModData");
			lua_newtable(S);
			lua_rawset(S, LUA_GLOBALSINDEX);
		}
		WbArgs a = {self, which};
		lua_pushcfunction(S, WbTramp);
		lua_pushlightuserdata(S, &a);
		for(uint32_t i = 0; i < nargs; ++i)
			DecodeValue(S, args, ctx, id, 0);
		bool bOk = args.ok();
		std::string msg = "bad arguments";
		int n = 0;
		if(bOk)
		{
			if(LuaPCall(S, (int)nargs + 1, LUA_MULTRET) != 0)
			{
				bOk = false;
				msg = lua_tostring(S, -1) ? lua_tostring(S, -1) : "(non-string error)";
				CountError(kWbNames[which], msg.c_str());
			}
			else
				n = lua_gettop(S);
		}
		if(bOk)
		{
			ret.u32(0);
			ret.i32(n);
			ret.u32((uint32_t)n);
			for(int i = 1; i <= n; ++i)
				EncodeTop(S, i, ret, ctx);
			if(which == kWbLoadModData)
			{
				lua_pushstring(S, "MapModData");
				lua_rawget(S, LUA_GLOBALSINDEX);
				const int iTable = lua_gettop(S);
				lua_pushnil(S);
				lua_pushnil(S);
				if(lua_istable(S, iTable))
				{
					lua_getfield(S, iTable, "Globals");
					lua_replace(S, iTable + 1);
					lua_getfield(S, iTable, "Plots");
					lua_replace(S, iTable + 2);
				}
				EncodeTop(S, iTable + 1, ret, ctx);
				EncodeTop(S, iTable + 2, ret, ctx);
			}
		}
		else
		{
			ret.u32(1u);
			ret.str(msg.c_str());
		}
		lua_settop(S, 0);
		ctx->threads.erase(S);
		luaL_unref(L, LUA_REGISTRYINDEX, t.anchor);
		FlushReleases();
		return true;
	}
}  // namespace

bool ServerHook_ICvWorldBuilderMapLoader2_LoadModData(ICvWorldBuilderMapLoader2* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	return WbServe(self, kWbLoadModData, args, ret, ch);
}
bool ServerHook_ICvWorldBuilderMapLoader2_GetMapPreview(ICvWorldBuilderMapLoader2* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	return WbServe(self, kWbGetMapPreview, args, ret, ch);
}
bool ServerHook_ICvWorldBuilderMapLoader2_GetMapPlayers(ICvWorldBuilderMapLoader2* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	return WbServe(self, kWbGetMapPlayers, args, ret, ch);
}
bool ServerHook_ICvWorldBuilderMapLoader2_AddRandomItems(ICvWorldBuilderMapLoader2* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	return WbServe(self, kWbAddRandomItems, args, ret, ch);
}
bool ServerHook_ICvWorldBuilderMapLoader2_ScatterResources(ICvWorldBuilderMapLoader2* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	return WbServe(self, kWbScatterResources, args, ret, ch);
}
bool ServerHook_ICvWorldBuilderMapLoader2_ScatterGoodies(ICvWorldBuilderMapLoader2* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	return WbServe(self, kWbScatterGoodies, args, ret, ch);
}
