// proxy_lua.cpp - the engine side of the Lua bridge (epic 2300, task 2302; proxy-design 11.5). Hand-written.
// All Lua runs in the engine's VM; the DLL's C functions run in the server. This file holds:
//   - the Lua value codec of vp_wire.h (encode from / decode onto an engine lua_State);
//   - C thunks for DLL functions (kLCFunc) and server-side values (kLSRef), instance tables with tokens, LuaTypes;
//     on upstream master's DLL (issue 0314) instances and statics as userdata with tokens (PushMasterInstance);
//   - the proxy hooks of ICvScriptSystemUtility1 (RegisterScriptLibraries, Push/Get*Instance, PushReplayFromStream);
//   - the engine hooks of ICvEngineScriptSystem1 (CreateLuaThread, FreeLuaThread, LoadFile) and
//     ICvEngineScriptSystemArgs1::GetString (team-2300 contract section D);
//   - the served runtime methods kRtE_LuaGetGlobal, kRtE_LuaCall, kRtE_LuaRelease and (issue 2504) kRtE_LuaRunChunk:
//     a chunk the DLL loads on its own Lua thread (InitLuaFramework's VP_LUAAPI framework) runs on the engine thread;
//     the kRtE_Db* methods are passed on to proxy_db.cpp.
// Rules kept here: a Lua error is raised (lua_error / luaL_error) only from a frame that holds no C++ object with a
// destructor; anything decoded from a RETURN or CALL payload is decoded from a private copy, because decoding an
// instance can make a nested call (kRt_LuaDescribeType) that invalidates the channel's payload; __gc never calls the
// server (it queues a release that the next kRt_LuaCall flushes).
#include "proxy_rt_internal.h"
#include "proxy_ifaces.gen.h"
#include "proxy_local.h"
#include "proxy_luaexec.h"  // ticket 0805: ProxyLuaExec::InertImageValues
#include "proxy_purity.h"
#include "proxy_ui_bridge_rt.h"
#include "proxy_session.h"  // ticket 0765: the Exe table's GetBuildName
#include "proxy_uidraw.h"   // ticket 0765: the Exe table's UI draw-list answers
#include "vp_luaraise.h"  // ticket 4804: kRtE_LuaRaise
// Issue 3518: the Lua 5.1 C API is included here, not taken from the DLL's precompiled header. Lua/CvLuaSupport.h
// included <lua.h>, <lualib.h> and <lauxlib.h> up to Release-5.4.8; on upstream master it no longer does.
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
#include <algorithm>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace ProxyRtInt;

static lua_State* ThreadOf(uint32_t id);  // below (engine hooks); issue 0315's MakePostCalls uses it

namespace
{
// ============================================================================================ state
struct LuaLock
{
	LuaLock() { EnterCriticalSection(&Cs()); }
	~LuaLock() { LeaveCriticalSection(&Cs()); }
	static CRITICAL_SECTION& Cs()
	{
		static CRITICAL_SECTION* s = NULL;
		if (!s)
		{
			// First use happens on a single thread (boot / RegisterScriptLibraries); a race here would only leak.
			CRITICAL_SECTION* c = new CRITICAL_SECTION;
			InitializeCriticalSection(c);
			s = c;
		}
		return *s;
	}
};
// Created while the DLL loads (CRT initialisation), so the lazy path above never races.
struct LuaLockInit
{
	LuaLockInit() { LuaLock::Cs(); }
} g_luaLockInit;

// Registry keys: addresses of these bytes, pushed as light userdata.
char kKeyAnchor, kKeySRefMt, kKeySRefGcMt, kKeySRefCache, kKeyMethodCache;
char kKeyMasterTypes, kKeyMasterInst, kKeyInstMarker;  // issue 0314
volatile LONG g_nMethodHits = 0, g_nMethodFills = 0, g_nPureStubCalls = 0;  // issue 0313
volatile LONG g_nMasterInstPushed = 0, g_nMasterInstMade = 0, g_nMasterTypes = 0;  // issue 0314

// Issue 0314: upstream master's instance form (#13489, Lua/!CvLuaApiInstanceTemplates.h): Game, Map and every instance
// are full userdata whose metatable is the registry's vpdllinternals[<tag>].__meta (__index = the type's function
// table, __metatable = false). The server sends them as kLInstance(type, pointer) (statics with the pointer 1) and
// describes such a type with a first member kFormKey = "userdata" (srv_remote_lua.cpp has the same literal). Here an
// instance of such a type is a userdata too: an InstUd (the token) whose metatable, one per type and global state in
// the registry, has __index = the function table of DLL-function thunks, __metatable = false and the marker key
// kKeyInstMarker = the type name; one userdata per token and global state (weak-valued cache), as master's
// vpdllinternals cache gives one per object and state.
const char* const kFormKey = "__vpproxy_form";
const uint32_t kInstMagic = 0x4E494D56u;  // 'VMIN'
struct InstUd
{
	uint32_t magic;
	uint32_t token;
};
std::map<std::string, int> g_typeForm;  // type name -> 1 master form (userdata), 0 the pin's (instance tables)

const uint32_t kSRefMagic = 0x46455253u;  // 'SREF'
struct SRefUd
{
	uint32_t magic;
	uint32_t id;
	uint32_t chan;
};

struct Token
{
	uint64_t ptr;
	std::string name;
};
std::vector<Token> g_tokens;  // instance token = index + 1
std::map<std::pair<std::string, uint64_t>, uint32_t> g_tokenIndex;

std::map<std::string, std::vector<uint8_t> > g_typeDesc;  // LuaTypes name -> kRt_LuaDescribeType RETURN

struct ERef
{
	lua_State* anchor;  // a thread anchored in the registry of the global state the ref lives in
	int ref;
	bool live;
};
std::vector<ERef> g_erefs;  // eref id = index + 1
std::vector<uint32_t> g_erefFree;

std::map<uint32_t, lua_State*> g_threads;  // engine Lua thread id -> engine thread (CreateLuaThread)
uint32_t g_nextThread = 0;

std::vector<uint32_t> g_release[vpipc::kMaxChannels];  // queued kLSRef releases per channel
std::set<std::string> g_unsupLogged;

// ============================================================================================ helpers
void NoteUnsupported(const char* what)
{
	CountUnsupported(what);
	bool first;
	{
		LuaLock l;
		first = g_unsupLogged.insert(what).second;
	}
	if (first) Log("Lua: a value of type '%s' cannot cross; it travels as kLUnsupported (nil on arrival; logged once per type, counted)", what);
}

int AbsIndex(lua_State* L, int idx)
{
	return (idx > 0 || idx <= LUA_REGISTRYINDEX) ? idx : lua_gettop(L) + idx + 1;
}

void PushRegistry(lua_State* L, void* key)
{
	lua_pushlightuserdata(L, key);
	lua_rawget(L, LUA_REGISTRYINDEX);
}

bool HasMeta(lua_State* L, int idx, void* key)
{
	if (!lua_getmetatable(L, idx)) return false;
	PushRegistry(L, key);
	const bool eq = lua_rawequal(L, -1, -2) != 0;
	lua_pop(L, 2);
	return eq;
}

// A thread anchored in the registry of L's global state, so a ref made on a short-lived coroutine can be released
// later through a state that is still alive.
lua_State* Anchor(lua_State* L)
{
	PushRegistry(L, &kKeyAnchor);
	if (lua_isthread(L, -1))
	{
		lua_State* a = lua_tothread(L, -1);
		lua_pop(L, 1);
		return a;
	}
	lua_pop(L, 1);
	lua_pushlightuserdata(L, &kKeyAnchor);
	lua_State* a = lua_newthread(L);
	lua_rawset(L, LUA_REGISTRYINDEX);
	return a;
}

uint32_t MakeERef(lua_State* L, int idx)
{
	lua_State* a = Anchor(L);
	lua_pushvalue(L, idx);
	const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
	LuaLock l;
	ERef e = {a, ref, true};
	if (!g_erefFree.empty())
	{
		const uint32_t id = g_erefFree.back();
		g_erefFree.pop_back();
		g_erefs[id - 1] = e;
		return id;
	}
	g_erefs.push_back(e);
	return (uint32_t)g_erefs.size();
}

uint32_t TokenFor(const std::string& name, uint64_t ptr)
{
	LuaLock l;
	const std::pair<std::string, uint64_t> k(name, ptr);
	std::map<std::pair<std::string, uint64_t>, uint32_t>::iterator it = g_tokenIndex.find(k);
	if (it != g_tokenIndex.end()) return it->second;
	Token t;
	t.ptr = ptr;
	t.name = name;
	g_tokens.push_back(t);
	const uint32_t tok = (uint32_t)g_tokens.size();
	g_tokenIndex[k] = tok;
	return tok;
}

bool TokenLookup(uint32_t tok, std::string& name, uint64_t& ptr)
{
	LuaLock l;
	if (tok == 0 || tok > g_tokens.size()) return false;
	name = g_tokens[tok - 1].name;
	ptr = g_tokens[tok - 1].ptr;
	return true;
}

// Issue 0314: the value at idx is a master-form instance made here (an InstUd whose metatable carries the marker); its
// token in *tok.
bool MasterInstanceAt(lua_State* L, int idx, uint32_t* tok)
{
	if (lua_type(L, idx) != LUA_TUSERDATA || lua_objlen(L, idx) != sizeof(InstUd)) return false;
	const InstUd* u = static_cast<const InstUd*>(lua_touserdata(L, idx));
	if (!u || u->magic != kInstMagic || !lua_getmetatable(L, idx)) return false;
	lua_pushlightuserdata(L, &kKeyInstMarker);
	lua_rawget(L, -2);
	const bool marked = lua_type(L, -1) == LUA_TSTRING;
	lua_pop(L, 2);
	if (marked && tok) *tok = u->token;
	return marked;
}

// ============================================================================================ encode
int FuncThunk(lua_State* L);
int SRefFuncThunk(lua_State* L);
void NoteThunkName(lua_State* L, int idx, const std::string& name);  // issue 2805

struct EncCtx
{
	uint32_t start;
	bool over;
};

void EncUnsupported(vpipc::Writer& w, const char* what)
{
	w.u32(vpw::kLUnsupported);
	w.str(what);
	NoteUnsupported(what);
}

void Enc(lua_State* L, int idx, vpipc::Writer& w, uint32_t depth, EncCtx& cx);

void EncTable(lua_State* L, int idx, vpipc::Writer& w, uint32_t depth, EncCtx& cx)
{
	// An instance table: {__instance = lightuserdata token}.
	lua_pushstring(L, "__instance");
	lua_rawget(L, idx);
	if (lua_islightuserdata(L, -1))
	{
		const uint32_t tok = (uint32_t)(uintptr_t)lua_touserdata(L, -1);
		lua_pop(L, 1);
		std::string name;
		uint64_t ptr = 0;
		if (TokenLookup(tok, name, ptr))
		{
			w.u32(vpw::kLInstance);
			w.str(name.c_str(), (uint32_t)name.size());
			w.u64(ptr);
		}
		else
			EncUnsupported(w, "instance (unknown token)");
		return;
	}
	lua_pop(L, 1);
	if (depth >= vpw::kLuaMaxDepth || cx.over)
	{
		EncUnsupported(w, depth >= vpw::kLuaMaxDepth ? "table (deeper than kLuaMaxDepth)" : "table (over kLuaMaxBytes)");
		return;
	}
	if (!lua_checkstack(L, 6))
	{
		EncUnsupported(w, "table (Lua stack full)");
		return;
	}
	w.u32(vpw::kLTable);
	const uint32_t off = w.size();
	w.u32(0);
	uint32_t n = 0;
	lua_pushnil(L);
	while (lua_next(L, idx))
	{
		const int kt = lua_type(L, -2);
		if (kt != LUA_TNUMBER && kt != LUA_TSTRING && kt != LUA_TBOOLEAN && kt != LUA_TTABLE)
		{
			NoteUnsupported(lua_typename(L, kt));  // a key that cannot cross: the pair is left out
			lua_pop(L, 1);
			continue;
		}
		const int top = lua_gettop(L);
		Enc(L, top - 1, w, depth + 1, cx);
		Enc(L, top, w, depth + 1, cx);
		++n;
		lua_pop(L, 1);
		if (w.size() - cx.start > vpw::kLuaMaxBytes)
		{
			cx.over = true;
			lua_pop(L, 1);
			break;
		}
	}
	if (!w.overflowed()) memcpy(const_cast<uint8_t*>(w.data()) + off, &n, 4);
}

void Enc(lua_State* L, int idx, vpipc::Writer& w, uint32_t depth, EncCtx& cx)
{
	idx = AbsIndex(L, idx);
	switch (lua_type(L, idx))
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
		size_t len = 0;
		const char* s = lua_tolstring(L, idx, &len);
		w.u32(vpw::kLString);
		w.str(s, (uint32_t)len);
		return;
	}
	case LUA_TTABLE: EncTable(L, idx, w, depth, cx); return;
	case LUA_TFUNCTION:
	{
		const lua_CFunction cf = lua_tocfunction(L, idx);
		if (cf == FuncThunk || cf == SRefFuncThunk)
		{
			lua_getupvalue(L, idx, 1);
			const uint32_t id = (uint32_t)lua_tonumber(L, -1);
			lua_pop(L, 1);
			if (cf == FuncThunk)
			{
				w.u32(vpw::kLCFunc);
				w.u32(id);
			}
			else
			{
				w.u32(vpw::kLSRef);
				w.u32(id);
				w.u32(LUA_TFUNCTION);
			}
			return;
		}
		// A Lua function of the engine (or an engine C function): it stays here, the server gets a reference.
		w.u32(vpw::kLERef);
		w.u32(MakeERef(L, idx));
		w.u32(LUA_TFUNCTION);
		return;
	}
	case LUA_TUSERDATA:
	{
		if (HasMeta(L, idx, &kKeySRefMt))
		{
			const SRefUd* u = static_cast<const SRefUd*>(lua_touserdata(L, idx));
			w.u32(vpw::kLSRef);
			w.u32(u->id);
			w.u32(LUA_TUSERDATA);
			return;
		}
		uint32_t tok = 0;
		if (MasterInstanceAt(L, idx, &tok))  // issue 0314: upstream master's instance form
		{
			std::string name;
			uint64_t ptr = 0;
			if (TokenLookup(tok, name, ptr))
			{
				w.u32(vpw::kLInstance);
				w.str(name.c_str(), (uint32_t)name.size());
				w.u64(ptr);
			}
			else
				EncUnsupported(w, "instance (unknown token)");
			return;
		}
		EncUnsupported(w, "userdata");
		return;
	}
	case LUA_TLIGHTUSERDATA: EncUnsupported(w, "lightuserdata"); return;
	case LUA_TTHREAD: EncUnsupported(w, "thread"); return;
	default: EncUnsupported(w, "unknown"); return;
	}
}

// Encodes one value; a table is built in a scratch writer first so that one over kLuaMaxBytes can be replaced by
// kLUnsupported as a whole.
void EncTop(lua_State* L, int idx, vpipc::Writer& w)
{
	idx = AbsIndex(L, idx);
	if (lua_type(L, idx) != LUA_TTABLE)
	{
		EncCtx cx = {w.size(), false};
		Enc(L, idx, w, 0, cx);
		return;
	}
	vpipc::Writer s;
	uint8_t first[256];
	s.reset(first, sizeof(first), true);
	EncCtx cx = {0, false};
	Enc(L, idx, s, 0, cx);
	if (cx.over || s.overflowed())
	{
		EncUnsupported(w, "table (over kLuaMaxBytes)");
		return;
	}
	const uint32_t n = s.size();
	uint8_t* p = w.reserve(n);
	if (p) memcpy(p, s.data(), n);
}

// ============================================================================================ decode
bool Dec(lua_State* L, vpipc::Reader& r, uint32_t depth);
void PushInstance(lua_State* L, const std::string& name, uint64_t ptr);

void QueueRelease(uint32_t chan, uint32_t id)
{
	LuaLock l;
	if (chan < vpipc::kMaxChannels) g_release[chan].push_back(id);
}

int SRefGc(lua_State* L)
{
	SRefUd* u = static_cast<SRefUd*>(lua_touserdata(L, 1));
	if (u && u->magic == kSRefMagic)
	{
		QueueRelease(u->chan, u->id);
		u->magic = 0;
	}
	return 0;
}

int SRefIndex(lua_State* L);
int SRefCall(lua_State* L);

void PushMeta(lua_State* L, void* key, bool full)
{
	PushRegistry(L, key);
	if (lua_istable(L, -1)) return;
	lua_pop(L, 1);
	lua_createtable(L, 0, 4);
	lua_pushcfunction(L, SRefGc);
	lua_setfield(L, -2, "__gc");
	if (full)
	{
		lua_pushcfunction(L, SRefIndex);
		lua_setfield(L, -2, "__index");
		lua_pushcfunction(L, SRefCall);
		lua_setfield(L, -2, "__call");
		lua_pushstring(L, "vpproxy server value");
		lua_setfield(L, -2, "__metatable");
	}
	lua_pushlightuserdata(L, key);
	lua_pushvalue(L, -2);
	lua_rawset(L, LUA_REGISTRYINDEX);
}

// Issue 0314: the stub at idx is alive (its release has not been queued) and of the Lua type the server sent. A function
// stub (SRefFuncThunk) is alive while upvalue 2, the userdata whose __gc releases the id, still has its magic.
// Why a cache hit must be checked: Lua 5.1 keeps a value in a weak table for one more cycle when a userdata being
// finalized in that cycle reaches it (lgc.c atomic: marktmu marks the finalizable userdata and what they reach before
// cleartable runs). Every stub's userdata has as its environment the globals table that was current when it was made
// (a host thread's environment: CreateLuaThread's {StateName = ...}), and that table holds the thread's function
// stubs. When such a thread is collected, the function stubs' __gc-userdata are finalized (their ids released to the
// server, which reuses a released registry slot first: luaL_ref's free list) while the closures stay in this cache for
// one more cycle; the server's next value with that id then met the old closure (0308's intermittent failure: "Game"
// and Map.GetPlotByIndex(i) arrived as functions).
volatile LONG g_nStaleStubs = 0, g_nStubTypeMismatch = 0;
bool SRefStubLive(lua_State* L, int idx, uint32_t ltype)
{
	idx = AbsIndex(L, idx);
	if (lua_type(L, idx) == LUA_TUSERDATA)
	{
		const SRefUd* u = static_cast<const SRefUd*>(lua_touserdata(L, idx));
		return u && u->magic == kSRefMagic;
	}
	if (lua_tocfunction(L, idx) != SRefFuncThunk || !lua_getupvalue(L, idx, 2)) return false;
	const SRefUd* u = static_cast<const SRefUd*>(lua_touserdata(L, -1));
	lua_pop(L, 1);
	return u && u->magic == kSRefMagic;
}

void PushSRef(lua_State* L, uint32_t id, uint32_t ltype, uint32_t chan)
{
	// Identity: one engine value per (channel, sref id), kept in a weak-valued cache.
	PushRegistry(L, &kKeySRefCache);
	if (!lua_istable(L, -1))
	{
		lua_pop(L, 1);
		lua_newtable(L);
		lua_createtable(L, 0, 1);
		lua_pushstring(L, "v");
		lua_setfield(L, -2, "__mode");
		lua_setmetatable(L, -2);
		lua_pushlightuserdata(L, &kKeySRefCache);
		lua_pushvalue(L, -2);
		lua_rawset(L, LUA_REGISTRYINDEX);
	}
	const double key = (double)chan * 4294967296.0 + (double)id;
	lua_pushnumber(L, key);
	lua_rawget(L, -2);
	if (!lua_isnil(L, -1))
	{
		// Issue 0314: only a live stub of the sent type is the server's value; a finalized one is replaced (the server
		// reused its released id). A live stub of another type would be a second value under one live id (a release
		// the server did not get from this stub): counted and replaced, the server's word wins.
		const bool isFn = lua_type(L, -1) == LUA_TFUNCTION;
		const bool sameType = isFn == (ltype == LUA_TFUNCTION);
		if (SRefStubLive(L, -1, ltype) && sameType)
		{
			lua_replace(L, -2);  // drop the cache
			return;
		}
		const LONG n = SRefStubLive(L, -1, ltype) ? InterlockedIncrement(&g_nStubTypeMismatch)
		                                          : InterlockedIncrement(&g_nStaleStubs);
		if (n <= 20)
			Log("Lua: %s server-value stub for id 0x%08x on channel %u (a %s, the server sent a %s): replaced by a new stub "
			    "(issue 0314)%s", SRefStubLive(L, -1, ltype) ? "LOUD: a LIVE" : "a finalized", id, chan,
			    lua_typename(L, lua_type(L, -1)), ltype == LUA_TFUNCTION ? "function" : "userdata",
			    n == 20 ? "; further ones counted only" : "");
	}
	lua_pop(L, 1);
	if (ltype == LUA_TFUNCTION)
	{
		lua_pushnumber(L, (lua_Number)id);
		SRefUd* u = static_cast<SRefUd*>(lua_newuserdata(L, sizeof(SRefUd)));
		u->magic = kSRefMagic;
		u->id = id;
		u->chan = chan;
		PushMeta(L, &kKeySRefGcMt, false);
		lua_setmetatable(L, -2);
		lua_pushcclosure(L, SRefFuncThunk, 2);
	}
	else
	{
		SRefUd* u = static_cast<SRefUd*>(lua_newuserdata(L, sizeof(SRefUd)));
		u->magic = kSRefMagic;
		u->id = id;
		u->chan = chan;
		PushMeta(L, &kKeySRefMt, true);
		lua_setmetatable(L, -2);
	}
	lua_pushnumber(L, key);
	lua_pushvalue(L, -2);
	lua_rawset(L, -4);   // cache[key] = value
	lua_replace(L, -2);  // drop the cache
}

void PushERef(lua_State* L, uint32_t id)
{
	int ref = LUA_NOREF;
	{
		LuaLock l;
		if (id && id <= g_erefs.size() && g_erefs[id - 1].live) ref = g_erefs[id - 1].ref;
	}
	if (ref == LUA_NOREF)
	{
		Log("Lua: unknown engine reference %u (nil)", id);
		lua_pushnil(L);
		return;
	}
	lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
}

bool Dec(lua_State* L, vpipc::Reader& r, uint32_t depth)
{
	if (!lua_checkstack(L, 6))
	{
		Log("Lua: stack full while decoding a value");
		return false;
	}
	const uint32_t tag = r.u32();
	if (!r.ok())
	{
		lua_pushnil(L);
		return false;
	}
	switch (tag)
	{
	case vpw::kLNil: lua_pushnil(L); return true;
	case vpw::kLFalse: lua_pushboolean(L, 0); return true;
	case vpw::kLTrue: lua_pushboolean(L, 1); return true;
	case vpw::kLNumber: lua_pushnumber(L, (lua_Number)r.f64()); return r.ok();
	case vpw::kLString:
	{
		uint32_t len = 0;
		const char* s = r.cstr(&len);
		if (s) lua_pushlstring(L, s, len);
		else lua_pushnil(L);
		return r.ok();
	}
	case vpw::kLTable:
	{
		const uint32_t n = r.u32();
		if (!r.ok() || depth > 2 * vpw::kLuaMaxDepth)
		{
			lua_pushnil(L);
			return false;
		}
		lua_createtable(L, 0, n < 256 ? (int)n : 256);
		for (uint32_t i = 0; i < n; ++i)
		{
			const bool okK = Dec(L, r, depth + 1);
			const bool okV = Dec(L, r, depth + 1);
			if (lua_isnil(L, -2) || !okK || !okV)
			{
				lua_pop(L, 2);
				if (!r.ok()) return false;
				continue;
			}
			lua_rawset(L, -3);
		}
		return r.ok();
	}
	case vpw::kLCFunc:
	{
		const uint32_t id = r.u32();
		lua_pushnumber(L, (lua_Number)id);
		lua_pushcclosure(L, FuncThunk, 1);
		return r.ok();
	}
	case vpw::kLSRef:
	{
		const uint32_t id = r.u32();
		const uint32_t ty = r.u32();
		if (!r.ok())
		{
			lua_pushnil(L);
			return false;
		}
		PushSRef(L, id, ty, CurrentChannelIndex());
		return true;
	}
	case vpw::kLInstance:
	{
		uint32_t len = 0;
		const char* s = r.cstr(&len);
		const uint64_t ptr = r.u64();
		if (!r.ok() || !s)
		{
			lua_pushnil(L);
			return false;
		}
		PushInstance(L, std::string(s, len), ptr);
		return true;
	}
	case vpw::kLERef:
	{
		const uint32_t id = r.u32();
		r.u32();  // Lua type
		PushERef(L, id);
		return r.ok();
	}
	case vpw::kLUnsupported:
	{
		const char* s = r.cstr();
		CountUnsupported(s ? s : "?");
		lua_pushnil(L);
		return r.ok();
	}
	default:
		Log("Lua: unknown value tag %u", tag);
		lua_pushnil(L);
		return false;
	}
}

// ============================================================================================ LuaTypes, instances
// Members of LuaTypes[name] from the server (kRt_LuaDescribeType), cached per name; returns a malloc'd copy.
uint8_t* TypeDescription(const std::string& name, uint32_t& len)
{
	{
		LuaLock l;
		std::map<std::string, std::vector<uint8_t> >::iterator it = g_typeDesc.find(name);
		if (it != g_typeDesc.end())
		{
			len = (uint32_t)it->second.size();
			uint8_t* p = static_cast<uint8_t*>(malloc(len ? len : 4));
			if (p && len) memcpy(p, &it->second[0], len);
			return p;
		}
	}
	vpipc::Channel& ch = ProxyRt::Chan();
	vpipc::Writer& w = ProxyRt::Begin(ch, vpw::kRt_LuaDescribeType);
	w.str(name.c_str(), (uint32_t)name.size());
	vpipc::Reader r;
	std::vector<uint8_t> bytes;
	if (ProxyRt::End(ch, vpw::kRt_LuaDescribeType, r) && r.remaining())
		bytes.assign(r.cursor(), r.cursor() + r.remaining());
	else
		Log("Lua: kRt_LuaDescribeType(%s) failed; LuaTypes.%s gets no members", name.c_str(), name.c_str());
	len = (uint32_t)bytes.size();
	uint8_t* p = static_cast<uint8_t*>(malloc(len ? len : 4));
	if (p && len) memcpy(p, &bytes[0], len);
	// Issue 0314: an answer without members is not kept (the server may not know the type yet: a scratch state whose
	// registration the DLL skipped, issue 0308); the next use asks again.
	if (bytes.size() < 4 || (bytes[0] | bytes[1] | bytes[2] | bytes[3]) == 0) return p;
	LuaLock l;
	g_typeDesc[name].swap(bytes);
	return p;
}

// Ticket 4804 (LU12): the key strings of every instance push go in with their length known (no strlen); Lua interns
// them as before (luaS_newlstr), and every table read is the same operation (lua_getglobal and lua_getfield are
// lua_gettable with the key pushed), so behaviour is unchanged by construction.
#define VP_PUSHLIT(L, s) lua_pushlstring((L), "" s, sizeof(s) - 1)

// As the DLL's CvLuaScopedInstance::PushTypeTable: LuaTypes[name] in L's globals, built on first use; leaves it on
// the stack.
void PushTypeTable(lua_State* L, const std::string& name)
{
	VP_PUSHLIT(L, "LuaTypes");
	lua_gettable(L, LUA_GLOBALSINDEX);  // lua_getglobal(L, "LuaTypes")
	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setglobal(L, "LuaTypes");
	}
	lua_pushlstring(L, name.data(), name.size());
	lua_gettable(L, -2);  // lua_getfield(L, -1, name.c_str())
	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
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
		lua_pushstring(L, name.c_str());
		lua_pushvalue(L, t);
		lua_rawset(L, t - 1);  // LuaTypes[name] = Type before the members (a member may be an instance of name)
		uint32_t len = 0;
		uint8_t* buf = TypeDescription(name, len);
		if (buf)
		{
			vpipc::Reader r(buf, len);
			const uint32_t n = len ? r.u32() : 0;
			for (uint32_t i = 0; i < n && r.ok(); ++i)
			{
				uint32_t kl = 0;
				const char* k = r.cstr(&kl);
				Dec(L, r, 0);
				if (!k || !strcmp(k, "__instances") || !strcmp(k, "__index") || !strcmp(k, kFormKey))
				{
					lua_pop(L, 1);
					continue;
				}
				NoteThunkName(L, -1, name + "." + std::string(k, kl));  // issue 2805: the UI-Lua mirror's names
				lua_pushlstring(L, k, kl);
				lua_insert(L, -2);
				lua_rawset(L, t);
			}
			free(buf);
		}
	}
	lua_replace(L, -2);
}

// Issue 0314: whether `name` is a type of upstream master's form: its description's first member is kFormKey =
// "userdata" (srv_remote_lua.cpp DescribeMasterType). Decided once per type and process (one DLL per process).
bool MasterForm(const std::string& name)
{
	{
		LuaLock l;
		std::map<std::string, int>::const_iterator it = g_typeForm.find(name);
		if (it != g_typeForm.end()) return it->second == 1;
	}
	uint32_t len = 0;
	uint8_t* buf = TypeDescription(name, len);
	int form = 0;
	bool known = false;  // a description with members: the decision is kept
	if (buf && len >= 4)
	{
		vpipc::Reader r(buf, len);
		if (r.u32() > 0)
		{
			known = true;
			uint32_t kl = 0;
			const char* k = r.cstr(&kl);
			if (k && kl == strlen(kFormKey) && !memcmp(k, kFormKey, kl) && r.u32() == vpw::kLString)
			{
				uint32_t vl = 0;
				const char* v = r.cstr(&vl);
				form = r.ok() && v && vl == 8 && !memcmp(v, "userdata", 8) ? 1 : 0;
			}
		}
	}
	free(buf);
	if (!known) return false;  // not kept: asked again at the next use
	LuaLock l;
	g_typeForm[name] = form;
	return form == 1;
}

// Issue 0314: pushes the engine-side metatable of the master-form type `name` (one per type and global state, kept in
// the registry): __index = the type's function table (DLL-function thunks from kRt_LuaDescribeType), __metatable =
// false (as master's), the marker kKeyInstMarker = name.
void PushMasterMeta(lua_State* L, const std::string& name)
{
	PushRegistry(L, &kKeyMasterTypes);
	if (!lua_istable(L, -1))
	{
		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushlightuserdata(L, &kKeyMasterTypes);
		lua_pushvalue(L, -2);
		lua_rawset(L, LUA_REGISTRYINDEX);
	}
	const int types = lua_gettop(L);
	lua_pushlstring(L, name.data(), name.size());
	lua_rawget(L, types);
	if (lua_istable(L, -1))
	{
		lua_replace(L, types);
		return;
	}
	lua_pop(L, 1);
	lua_createtable(L, 0, 4);
	const int mt = lua_gettop(L);
	lua_newtable(L);
	const int ft = lua_gettop(L);
	uint32_t len = 0;
	uint8_t* buf = TypeDescription(name, len);
	if (buf)
	{
		vpipc::Reader r(buf, len);
		const uint32_t n = len ? r.u32() : 0;
		for (uint32_t i = 0; i < n && r.ok(); ++i)
		{
			uint32_t kl = 0;
			const char* k = r.cstr(&kl);
			Dec(L, r, 0);
			if (!k || !strcmp(k, kFormKey))
			{
				lua_pop(L, 1);
				continue;
			}
			NoteThunkName(L, -1, name + "." + std::string(k, kl));  // the UI-Lua mirror's names
			lua_pushlstring(L, k, kl);
			lua_insert(L, -2);
			lua_rawset(L, ft);
		}
		free(buf);
	}
	lua_pushstring(L, "__index");
	lua_pushvalue(L, ft);
	lua_rawset(L, mt);
	lua_pushstring(L, "__metatable");
	lua_pushboolean(L, 0);
	lua_rawset(L, mt);
	lua_pushlightuserdata(L, &kKeyInstMarker);
	lua_pushlstring(L, name.data(), name.size());
	lua_rawset(L, mt);
	lua_settop(L, mt);
	lua_pushlstring(L, name.data(), name.size());
	lua_pushvalue(L, mt);
	lua_rawset(L, types);
	lua_replace(L, types);
	InterlockedIncrement(&g_nMasterTypes);
}

// Issue 0314: a master-form instance: the userdata of this token in L's global state (made on first use).
void PushMasterInstance(lua_State* L, const std::string& name, uint64_t ptr)
{
	const uint32_t tok = TokenFor(name, ptr);
	PushRegistry(L, &kKeyMasterInst);
	if (!lua_istable(L, -1))
	{
		lua_pop(L, 1);
		lua_newtable(L);
		lua_createtable(L, 0, 1);
		lua_pushstring(L, "v");
		lua_setfield(L, -2, "__mode");
		lua_setmetatable(L, -2);
		lua_pushlightuserdata(L, &kKeyMasterInst);
		lua_pushvalue(L, -2);
		lua_rawset(L, LUA_REGISTRYINDEX);
	}
	const int cache = lua_gettop(L);
	InterlockedIncrement(&g_nMasterInstPushed);
	lua_pushnumber(L, (lua_Number)tok);
	lua_rawget(L, cache);
	if (lua_type(L, -1) == LUA_TUSERDATA)
	{
		lua_replace(L, cache);
		return;
	}
	lua_pop(L, 1);
	InstUd* u = static_cast<InstUd*>(lua_newuserdata(L, sizeof(InstUd)));
	u->magic = kInstMagic;
	u->token = tok;
	PushMasterMeta(L, name);  // may cross (kRt_LuaDescribeType) once per type and process
	lua_setmetatable(L, -2);
	lua_pushnumber(L, (lua_Number)tok);
	lua_pushvalue(L, -2);
	lua_rawset(L, cache);
	lua_replace(L, cache);
	InterlockedIncrement(&g_nMasterInstMade);
}

// As the DLL's CvLuaScopedInstance::Push, with a token in __instance instead of the server pointer.
void PushInstance(lua_State* L, const std::string& name, uint64_t ptr)
{
	if (!ptr)
	{
		lua_pushnil(L);
		return;
	}
	if (MasterForm(name))  // issue 0314
	{
		PushMasterInstance(L, name, ptr);
		return;
	}
	const uint32_t tok = TokenFor(name, ptr);
	void* lud = (void*)(uintptr_t)tok;
	PushTypeTable(L, name);  // Type
	VP_PUSHLIT(L, "__instances");
	lua_rawget(L, -2);  // Instances, Type
	if (!lua_istable(L, -1))
	{
		lua_pop(L, 1);
		lua_newtable(L);
		VP_PUSHLIT(L, "__instances");
		lua_pushvalue(L, -2);
		lua_rawset(L, -4);
	}
	lua_pushlightuserdata(L, lud);
	lua_rawget(L, -2);  // Obj/nil, Instances, Type
	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_createtable(L, 0, 1);
		VP_PUSHLIT(L, "__instance");
		lua_pushlightuserdata(L, lud);
		lua_rawset(L, -3);
		lua_pushvalue(L, -3);
		lua_setmetatable(L, -2);
		lua_pushlightuserdata(L, lud);
		lua_pushvalue(L, -2);
		lua_rawset(L, -4);
	}
	lua_replace(L, -3);  // Instances, Obj
	lua_pop(L, 1);       // Obj
}

// ============================================================================================ calls into the server
void FlushReleases(uint32_t chan)
{
	std::vector<uint32_t> ids;
	{
		LuaLock l;
		if (chan >= vpipc::kMaxChannels || g_release[chan].empty()) return;
		ids.swap(g_release[chan]);
	}
	vpipc::Channel& ch = ProxyRt::Chan();
	vpipc::Writer& w = ProxyRt::Begin(ch, vpw::kRt_LuaRelease);
	w.u32((uint32_t)ids.size());
	for (size_t i = 0; i < ids.size(); ++i) w.u32(ids[i]);
	vpipc::Reader r;
	ProxyRt::End(ch, vpw::kRt_LuaRelease, r);
}

// ============================================================================================ the UI-Lua mirror (2805)
// Issue 2805 (proxy-design 17): kRt_LuaCall of a DLL Lua function that is `pure` in proxy/model/lua-pure-bindings.tsv
// (the `lua` lines of the loaded purity.tsv, derived from the table's pure rows; issue 3503), with scalar or instance
// arguments and an all-scalar result, is answered from ProxyLocal's Lua table while the epoch it was read in holds (lua_mirror=1), or only counted
// as a would-hit (lua_mirror=0). Function names are learnt where the proxy builds the thunks (PushTypeTable:
// "<Type>.<key>", RegisterScriptLibraries: "<global>.<key>" and "<global>"); a function id is pure when any of its
// names is (the purity belongs to the C function).
const uint32_t kFnMax = 16384;
std::string g_fnName[kFnMax];
volatile uint8_t g_fnPure[kFnMax];
volatile LONG g_fnCand[kFnMax], g_fnHit[kFnMax], g_fnWould[kFnMax], g_fnStore[kFnMax];
const uint32_t kWordWould = 1u << 29;  // bit 29 of the nargs word: a would-hit (shadow) or the verifying call of a hit

// Issue 3503: the pure Lua names are data (the `lua` lines of purity.tsv, loaded at the boot; proxy_purity.h), no longer
// a compiled kLuaPure; with no purity file every Lua call crosses.
void NoteFuncName(uint32_t id, const std::string& name)
{
	if (!id || id >= kFnMax) return;
	const bool pure = ProxyPurity::IsPureLuaName(name.c_str());
	LuaLock l;
	if (g_fnName[id].empty()) g_fnName[id] = name;
	if (pure) g_fnPure[id] = 1;
}

// Issue 0313: the function ids marked pure so far (any of their names is pure-listed).
uint32_t PureFuncIds()
{
	uint32_t n = 0;
	for (uint32_t i = 0; i < kFnMax; ++i) n += g_fnPure[i] ? 1u : 0u;
	return n;
}

// Issue 0313: the reserved name of the kRt_LuaRegister entry that carries the server's function names
// (srv_remote_lua.cpp has the same literal).
const char* const kLuaFuncNamesKey = "__vpproxy_function_names";

// The value at idx: a DLL function thunk? Then its id gets the name.
void NoteThunkName(lua_State* L, int idx, const std::string& name)
{
	idx = AbsIndex(L, idx);
	if (lua_tocfunction(L, idx) != FuncThunk) return;
	lua_getupvalue(L, idx, 1);
	const uint32_t id = (uint32_t)lua_tonumber(L, -1);
	lua_pop(L, 1);
	NoteFuncName(id, name);
}

// Arguments the key can carry exactly: nil, boolean, number, string and instance tables (encoded as type name and
// server pointer; a pointer is reused only after a deletion, which runs DLL code and moves the epoch).
bool ArgsSimple(lua_State* L, int first, int top)
{
	for (int i = first; i <= top; ++i)
	{
		switch (lua_type(L, i))
		{
		case LUA_TNIL:
		case LUA_TBOOLEAN:
		case LUA_TNUMBER:
		case LUA_TSTRING: break;
		case LUA_TTABLE:
		{
			lua_pushstring(L, "__instance");
			lua_rawget(L, i);
			const bool inst = lua_islightuserdata(L, -1) != 0;
			lua_pop(L, 1);
			if (!inst) return false;
			break;
		}
		case LUA_TUSERDATA:  // issue 0314: a master-form instance is keyed as (type name, server pointer) too
			if (!MasterInstanceAt(L, i, NULL)) return false;
			break;
		default: return false;
		}
	}
	return true;
}

// Issue 0313: arguments of a pure-listed call that may cross without moving the epoch but are no mirror key: what
// ArgsSimple accepts, and server-side userdata stubs.
bool ArgsPureStubs(lua_State* L, int first, int top)
{
	for (int i = first; i <= top; ++i)
	{
		if (lua_type(L, i) == LUA_TUSERDATA)
		{
			if (!HasMeta(L, i, &kKeySRefMt) && !MasterInstanceAt(L, i, NULL)) return false;
		}
		else if (!ArgsSimple(L, i, i))
			return false;
	}
	return true;
}

std::string LuaKey(lua_State* L, uint32_t id, int first, int nargs)
{
	vpipc::Writer w;
	uint8_t buf[512];
	w.reset(buf, sizeof(buf), true);
	w.u32(id);
	w.u32((uint32_t)nargs);
	for (int i = 0; i < nargs; ++i) EncTop(L, first + i, w);
	return std::string(reinterpret_cast<const char*>(w.data()), w.size());
}

// A reply (status, n, values) that holds only scalars: the values that may be kept.
bool ReplyScalar(const std::string& reply)
{
	vpipc::Reader r(reinterpret_cast<const uint8_t*>(reply.data()), (uint32_t)reply.size());
	if (r.u32() != 0) return false;
	const uint32_t n = r.u32();
	for (uint32_t i = 0; i < n && r.ok(); ++i)
	{
		switch (r.u32())
		{
		case vpw::kLNil:
		case vpw::kLTrue:
		case vpw::kLFalse: break;
		case vpw::kLNumber: r.f64(); break;
		case vpw::kLString: r.cstr(); break;
		default: return false;
		}
	}
	return r.ok() && r.remaining() == 0;
}

// kRt_LuaCall with the arguments L[firstArg..firstArg+nargs-1]; the reply (status onwards) copied into out.
bool SendLuaCall(lua_State* L, uint32_t tag, uint32_t id, uint32_t ltype, int firstArg, int nargs, uint32_t word,
                 std::string& out)
{
	vpipc::Channel& ch = ProxyRt::Chan();
	vpipc::Writer& w = ProxyRt::Begin(ch, vpw::kRt_LuaCall);
	w.u32(tag);
	w.u32(id);
	if (tag == vpw::kLSRef) w.u32(ltype);
	w.u32((uint32_t)nargs | word);
	for (int i = 0; i < nargs; ++i) EncTop(L, firstArg + i, w);
	vpipc::Reader r0;
	if (!ProxyRt::End(ch, vpw::kRt_LuaCall, r0)) return false;
	const uint32_t n = r0.ok() ? r0.remaining() : 0u;
	out.assign(n ? reinterpret_cast<const char*>(r0.cursor()) : "", n);
	return true;
}

// Pushes the results of a reply (or the error message). Returns the count, or -1 with the message on top of L.
int PushReply(lua_State* L, const std::string& reply)
{
	vpipc::Reader r(reinterpret_cast<const uint8_t*>(reply.data()), (uint32_t)reply.size());
	const uint32_t status = r.u32();
	int result;
	if (status == 0)
	{
		const uint32_t n = r.u32();
		if (!r.ok() || !lua_checkstack(L, (int)n + 8))
		{
			lua_pushstring(L, "vpproxy: bad RETURN of a Lua call");
			result = -1;
		}
		else
		{
			for (uint32_t i = 0; i < n; ++i) Dec(L, r, 0);
			result = (int)n;
		}
	}
	else
	{
		uint32_t len = 0;
		const char* m = r.cstr(&len);
		if (m) lua_pushlstring(L, m, len);
		else lua_pushstring(L, "vpproxy: Lua error in the 64-bit DLL (no message)");
		result = -1;
	}
	ProxyRt::DecodeCheck(vpw::kRt_LuaCall, r);
	return result;
}

// The phase the server counts a call under (srv_remote_lua.cpp LuaStatAdd), bits 30-31 of the nargs word: 1 = the
// epoch mirror is open (nothing impure in flight), 2 = an impure call is in flight, 0 = unknown (local answers off).
uint32_t PhaseWord()
{
	if (!ProxyLocal::On()) return 0;
	ProxyLocal::Token t = ProxyLocal::MirrorBegin(vpipc::Handle());  // not canonical: ok=false, nothing counted
	t.ok = true;
	return (ProxyLocal::StillValid(t) ? 1u : 2u) << 30;
}

// kRt_LuaCall with the arguments L[firstArg..top], or its answer from the UI-Lua mirror. Returns the number of
// results pushed, or -1 with an error message on the top of L (the caller raises it; no C++ object of this frame is
// alive then).
int LuaCallImpl(lua_State* L, uint32_t tag, uint32_t id, uint32_t ltype, int firstArg)
{
	const int top = lua_gettop(L);
	const int nargs = top >= firstArg ? top - firstArg + 1 : 0;
	const int mode = ProxyLocal::LuaMode();
	const bool pureFn = mode != 0 && tag == vpw::kLCFunc && id < kFnMax && g_fnPure[id];
	const bool cand = pureFn && ArgsSimple(L, firstArg, top);
	// Issue 0313: a pure-listed function whose arguments include server-side userdata (upstream master's instances,
	// until they cross as instances: 0314) cannot be kept (a stub's id is no stable key) but, being pure, does not move
	// the epoch either, as the pin's object-returning pure calls (ForcePureNext below). Only data and userdata stubs:
	// a function argument could be called by the DLL function.
	const bool pureStub = pureFn && !cand && ArgsPureStubs(L, firstArg, top);
	if (pureStub) InterlockedIncrement(&g_nPureStubCalls);
	std::string key, reply;
	bool would = false;
	if (cand)
	{
		InterlockedIncrement(&g_fnCand[id]);
		key = LuaKey(L, id, firstArg, nargs);
		std::string val;
		if (ProxyLocal::LuaGet(key, val))
		{
			if (mode == 2)
			{
				InterlockedIncrement(&g_fnHit[id]);
				if (ProxyLocal::Verify())
				{
					// The verifying read is pure in this epoch (the function is pure-listed): it must not move it.
					std::string srv;
					ProxyLocal::ForcePureNext();
					if (SendLuaCall(L, tag, id, ltype, firstArg, nargs, PhaseWord() | kWordWould, srv))
						ProxyLocal::LuaVerifyResult(g_fnName[id].c_str(), srv == val);
				}
				return PushReply(L, val);
			}
			would = true;
			InterlockedIncrement(&g_fnWould[id]);
		}
	}
	FlushReleases(CurrentChannelIndex());
	const uint32_t word = PhaseWord() | (would ? kWordWould : 0u);
	const ProxyLocal::Token t = cand ? ProxyLocal::LuaBegin() : ProxyLocal::Token();
	if (cand || pureStub)
	{
		if (mode == 2) ProxyLocal::ForcePureNext();  // the pure-listed call does not move the epoch (exactly this one)
		else ProxyLocal::LuaShadowMarkNext();       // shadow: it moves g_epoch as before, discounted in the Lua epoch
	}
	const bool ok = SendLuaCall(L, tag, id, ltype, firstArg, nargs, word, reply);
	if ((cand || pureStub) && mode == 1) ProxyLocal::LuaShadowEnd();
	if (!ok)
	{
		lua_pushstring(L, "vpproxy: the call into the 64-bit DLL failed (see vpproxy.log)");
		return -1;
	}
	if (cand && t.ok && ReplyScalar(reply))
	{
		ProxyLocal::LuaPut(t, key, reply);
		InterlockedIncrement(&g_fnStore[id]);
	}
	return PushReply(L, reply);
}

// The C thunk of a DLL function (upvalue 1 = function id). No C++ object lives in this frame (lua_error longjmps).
int FuncThunk(lua_State* L)
{
	ProxyUiBridgeRt::OnLuaEntry(L);   // issue 0178: one flag load when ui_bridge=0; the bridge's install check otherwise
	const uint32_t id = (uint32_t)lua_tonumber(L, lua_upvalueindex(1));
	const int n = LuaCallImpl(L, vpw::kLCFunc, id, 0, 1);
	if (n < 0) return lua_error(L);
	return n;
}

// The C thunk of a server-side function value (upvalue 1 = sref id, upvalue 2 = the userdata whose __gc releases it).
int SRefFuncThunk(lua_State* L)
{
	const uint32_t id = (uint32_t)lua_tonumber(L, lua_upvalueindex(1));
	const int n = LuaCallImpl(L, vpw::kLSRef, id, LUA_TFUNCTION, 1);
	if (n < 0) return lua_error(L);
	return n;
}

// __call of a server-side userdata.
int SRefCall(lua_State* L)
{
	const SRefUd* u = static_cast<const SRefUd*>(lua_touserdata(L, 1));
	const uint32_t id = u ? u->id : 0;
	const int n = LuaCallImpl(L, vpw::kLSRef, id, LUA_TUSERDATA, 2);
	if (n < 0) return lua_error(L);
	return n;
}

// Issue 0313: the DLL functions a server-side userdata's string keys gave, per stub (weak keys: the entries go with the
// stub). Upstream master's Game, Map and instances are userdata whose metatable's __index is the type's function table
// (Lua/!CvLuaApiInstanceTemplates.h; no type overrides CustomizeMTT at 76b3a5f70), filled once at registration: the
// function a key names never changes for one object, so `Game.GetGameTurn` crosses once per stub, not at every lookup.
// Only DLL-function results (FuncThunk, kLCFunc) are kept; every other value crosses each time. Pushes the cache table
// of the stub at index 1 (created when create is set) or nothing; returns whether it pushed one.
bool PushMethodCache(lua_State* L, bool create)
{
	PushRegistry(L, &kKeyMethodCache);
	if (!lua_istable(L, -1))
	{
		lua_pop(L, 1);
		if (!create) return false;
		lua_newtable(L);
		lua_createtable(L, 0, 1);
		lua_pushstring(L, "k");
		lua_setfield(L, -2, "__mode");
		lua_setmetatable(L, -2);
		lua_pushlightuserdata(L, &kKeyMethodCache);
		lua_pushvalue(L, -2);
		lua_rawset(L, LUA_REGISTRYINDEX);
	}
	lua_pushvalue(L, 1);
	lua_rawget(L, -2);  // per-stub table or nil, all
	if (lua_istable(L, -1))
	{
		lua_replace(L, -2);
		return true;
	}
	lua_pop(L, 1);
	if (!create)
	{
		lua_pop(L, 1);
		return false;
	}
	lua_newtable(L);
	lua_pushvalue(L, 1);
	lua_pushvalue(L, -2);
	lua_rawset(L, -4);  // all[stub] = per-stub
	lua_replace(L, -2);
	return true;
}

int SRefIndexImpl(lua_State* L)
{
	const SRefUd* u = static_cast<const SRefUd*>(lua_touserdata(L, 1));
	const bool strKey = lua_type(L, 2) == LUA_TSTRING;
	if (strKey && PushMethodCache(L, false))
	{
		lua_pushvalue(L, 2);
		lua_rawget(L, -2);
		if (!lua_isnil(L, -1))
		{
			lua_replace(L, -2);
			InterlockedIncrement(&g_nMethodHits);
			return 1;
		}
		lua_pop(L, 2);
	}
	vpipc::Channel& ch = ProxyRt::Chan();
	vpipc::Writer& w = ProxyRt::Begin(ch, vpw::kRt_LuaIndex);
	w.u32(u ? u->id : 0u);
	EncTop(L, 2, w);
	vpipc::Reader r0;
	if (!ProxyRt::End(ch, vpw::kRt_LuaIndex, r0))
	{
		lua_pushstring(L, "vpproxy: index of a 64-bit DLL value failed (see vpproxy.log)");
		return -1;
	}
	vpipc::Reader r;
	uint8_t* buf = CopyRest(r0, r);
	Dec(L, r, 0);
	ProxyRt::DecodeCheck(vpw::kRt_LuaIndex, r);
	free(buf);
	if (strKey && lua_tocfunction(L, -1) == FuncThunk && PushMethodCache(L, true))  // issue 0313
	{
		lua_pushvalue(L, 2);
		lua_pushvalue(L, -3);
		lua_rawset(L, -3);
		lua_pop(L, 1);
		InterlockedIncrement(&g_nMethodFills);
	}
	return 1;
}

int SRefIndex(lua_State* L)
{
	const int n = SRefIndexImpl(L);
	if (n < 0) return lua_error(L);
	return n;
}

// ============================================================================================ instance hooks
void PushInstanceHook(uint32_t method, vpipc::Handle self, lua_State* L, const void* obj)
{
	if (!L) return;
	if (!obj)
	{
		lua_pushnil(L);
		return;
	}
	vpipc::Channel& ch = ProxyRt::Chan();
	vpipc::Writer& w = ProxyRt::Begin(ch, method);
	w.handle(self);
	w.handle(ProxyRt::HandleOf(obj));
	vpipc::Reader r0;
	if (!ProxyRt::End(ch, method, r0))
	{
		lua_pushnil(L);
		return;
	}
	vpipc::Reader r;
	uint8_t* buf = CopyRest(r0, r);
	Dec(L, r, 0);
	ProxyRt::DecodeCheck(method, r);
	free(buf);
}

// Returns the handle; when the server reports an error and bErrorOnFail is set, copies the message to err and sets
// raise (the caller raises it from a frame without C++ objects).
vpipc::Handle GetInstanceCall(uint32_t method, vpipc::Handle self, lua_State* L, int index, bool bErrorOnFail,
                              char* err, size_t errCap, bool& raise)
{
	vpipc::Handle h = {0, 0};
	raise = false;
	if (!L) return h;
	const int idx = AbsIndex(L, index);
	vpipc::Channel& ch = ProxyRt::Chan();
	vpipc::Writer& w = ProxyRt::Begin(ch, method);
	w.handle(self);
	EncTop(L, idx, w);
	w.b(bErrorOnFail);
	vpipc::Reader r;
	if (!ProxyRt::End(ch, method, r)) return h;
	h = r.handle();
	const uint32_t hasError = r.u32();
	const char* msg = r.cstr();
	ProxyRt::DecodeCheck(method, r);
	if (hasError && bErrorOnFail)
	{
		strncpy_s(err, errCap, msg ? msg : "vpproxy: instance expected", _TRUNCATE);
		raise = true;
		h.type = 0;
		h.id = 0;
	}
	return h;
}

// ============================================================================================ issue 0315
// Upstream master's GamecoreInit.lua (CP's Gamecore folder, run by LuaSupport::NotifyDatabaseCached on the DLL's thread
// !VPDLL) runs here in the engine VM. Every DoRegisterScriptData of the DLL ends with executeSupportEvent("gc_post"),
// whose postinit reads the registry's vpdllinternals (the DLL's types: __hint, __meta) and builds GameCore.LuaTypes,
// Players.Majors and the rest (GamecorePostProcess.lua). On this route the DLL's registry is the server's, and the
// server's registration is once per channel on a scratch state. So the server sends, with a non-empty kRt_LuaRegister
// reply, master's types (kLuaDllTypesKey) and the engine calls the DLL made during its registration (kLuaPostCallsKey);
// here the types are mirrored into the engine registry's vpdllinternals ({__hint, __meta = the engine-side metatable of
// that type, PushMasterMeta}) and then the calls are made, after the state has its globals, as at the end of
// DoRegisterScriptData in the EXE.
const char* const kLuaDllTypesKey = "__vpproxy_dll_types";
const char* const kLuaPostCallsKey = "__vpproxy_post_calls";
volatile LONG g_nPostCalls = 0, g_nPostCallErrors = 0, g_nDllTypeMirrors = 0;

// t[key] as a string (empty when it is not one); t at idx.
std::string FieldString(lua_State* L, int idx, const char* key)
{
	lua_getfield(L, idx, key);
	size_t n = 0;
	const char* s = lua_type(L, -1) == LUA_TSTRING ? lua_tolstring(L, -1, &n) : NULL;
	const std::string v = s ? std::string(s, n) : std::string();
	lua_pop(L, 1);
	return v;
}

lua_Number FieldNumber(lua_State* L, int idx, const char* key)
{
	lua_getfield(L, idx, key);
	const lua_Number v = lua_tonumber(L, -1);
	lua_pop(L, 1);
	return v;
}

// The engine registry's vpdllinternals (made as master's pushInternalsTable makes it when gc_reset has removed it), with
// one new entry per type: [tag light userdata] = {__hint = hint, __meta = the type's engine-side metatable}. The table at
// idx is the server's {[i] = {tag, hint, name}}.
void MirrorDllTypes(lua_State* L, int idx)
{
	lua_checkstack(L, 12);
	const int top = lua_gettop(L);
	lua_getfield(L, LUA_REGISTRYINDEX, "vpdllinternals");
	if (!lua_istable(L, -1))
	{
		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setfield(L, LUA_REGISTRYINDEX, "vpdllinternals");
	}
	const int vi = lua_gettop(L);
	uint32_t n = 0;
	for (int i = 1;; ++i)
	{
		lua_rawgeti(L, idx, i);
		if (!lua_istable(L, -1)) break;
		const int e = lua_gettop(L);
		const uint32_t tag = (uint32_t)FieldNumber(L, e, "tag");
		const std::string hint = FieldString(L, e, "hint");
		const std::string name = FieldString(L, e, "name");
		lua_settop(L, e);
		if (!tag || hint.empty() || name.empty()) continue;
		lua_pushlightuserdata(L, (void*)(uintptr_t)tag);
		lua_createtable(L, 0, 2);
		lua_pushlstring(L, hint.data(), hint.size());
		lua_setfield(L, -2, "__hint");
		PushMasterMeta(L, name);  // may cross (kRt_LuaDescribeType) once per type and process
		lua_setfield(L, -2, "__meta");
		lua_settable(L, vi);
		lua_settop(L, e);
		++n;
	}
	lua_settop(L, top);
	if (InterlockedIncrement(&g_nDllTypeMirrors) <= 8)
		Log("RegisterScriptLibraries(%p): %u DLL types mirrored into the engine registry's vpdllinternals (issue 0315)", L, n);
}

int PostGetGlobalFn(lua_State* L)
{
	lua_getfield(L, LUA_GLOBALSINDEX, lua_tostring(L, 1));
	return 1;
}

// The engine calls the DLL made during its registration ({[i] = {thread, name, n, args}} at idx): name is a global of
// engine thread `thread`; called there with lua_pcall as the DLL's executeSupportEvent does (no results, the error not
// raised: the DLL ignores it), after the state L has its globals.
void MakePostCalls(lua_State* L, int idx)
{
	lua_checkstack(L, 8);
	for (int i = 1;; ++i)
	{
		lua_rawgeti(L, idx, i);
		if (!lua_istable(L, -1))
		{
			lua_pop(L, 1);
			break;
		}
		const int e = lua_gettop(L);
		const uint32_t thread = (uint32_t)FieldNumber(L, e, "thread");
		const std::string name = FieldString(L, e, "name");
		const int nargs = (int)FieldNumber(L, e, "n");
		lua_State* T = ThreadOf(thread);
		if (!T || name.empty() || nargs < 0 || nargs > 64 || !lua_checkstack(T, nargs + 8))
		{
			Log("LOUD: RegisterScriptLibraries(%p): the DLL's call of %s on engine thread %u not made (%s; issue 0315)", L,
			    name.c_str(), thread, T ? "bad arguments" : "unknown thread");
			lua_settop(L, e - 1);
			continue;
		}
		const int base = lua_gettop(T);
		lua_pushcfunction(T, PostGetGlobalFn);
		lua_pushlstring(T, name.data(), name.size());
		int rc = lua_pcall(T, 1, 1, 0);
		if (rc == 0)
		{
			lua_getfield(L, e, "args");
			const int a = lua_gettop(L);
			for (int k = 1; k <= nargs; ++k)
			{
				lua_rawgeti(L, a, k);
				switch (lua_type(L, -1))
				{
				case LUA_TBOOLEAN: lua_pushboolean(T, lua_toboolean(L, -1)); break;
				case LUA_TNUMBER: lua_pushnumber(T, lua_tonumber(L, -1)); break;
				case LUA_TSTRING:
				{
					size_t n = 0;
					const char* s = lua_tolstring(L, -1, &n);
					lua_pushlstring(T, s, n);
					break;
				}
				default: lua_pushnil(T); break;
				}
				lua_pop(L, 1);
			}
			rc = lua_pcall(T, nargs, 0, 0);
		}
		InterlockedIncrement(&g_nPostCalls);
		if (rc != 0)
		{
			const LONG ne = InterlockedIncrement(&g_nPostCallErrors);
			if (ne <= 20)
				Log("RegisterScriptLibraries(%p): the DLL's call of %s on engine thread %u failed: %s (issue 0315; the DLL "
				    "ignores it, as in the EXE)", L, name.c_str(), thread, lua_tostring(T, -1) ? lua_tostring(T, -1) : "?");
		}
		lua_settop(T, base);
		lua_settop(L, e - 1);
	}
}
}  // namespace

// Ticket 0805 (proxy_luaexec.h, proxy-design 38): run by ProxyLuaExec::OnShutdown on the persistent thread, through the
// engine's CallCFunction, at the orderly shutdown. This image's registry entries: the server-value stubs' metatables
// (kKeySRefMt: __gc, __index, __call; kKeySRefGcMt: __gc) lose their metamethods, the master-form types (kKeyMasterTypes)
// lose their function tables, and every entry under this image's keys leaves the registry (the caches and the anchor
// thread too). Stubs and instances that Lua still holds keep their metatables, now without this image's functions: a
// later collection finalizes nothing here, a later index or call raises a Lua error instead of running unmapped code.
int ProxyLua_InertImage(lua_State* L)
{
	ProxyLuaExec::InertCounts* c = static_cast<ProxyLuaExec::InertCounts*>(lua_touserdata(L, 1));
	void* const metaKeys[] = {&kKeySRefMt, &kKeySRefGcMt};
	void* const dropKeys[] = {&kKeySRefCache, &kKeyMethodCache, &kKeyMasterInst, &kKeyAnchor};
	{
		LuaLock l;  // the bridge's own maps are not touched; the lock orders this against a late served Lua call
		ProxyLuaExec::InertImageValues(L, metaKeys, 2, &kKeyMasterTypes, dropKeys, 4, c);
	}
	return 0;
}

// ================================================================================================ proxy hooks
// ---- ticket 0765: the Exe table of Vox Populi 5.5 on the product route --------------------------------------------------
// VP 5.5 ships upstream's EXE interop (#13379, #13406): the DLL registers a global table Exe (Lua/CvLuaExe.cpp) whose
// functions act on the EXE, and VP's Lua calls them without a guard: InGame.lua's OnUpdate every frame
// (TryGrowUICommandStream, until ok or a refusal other than "pending"), YieldIconManager.lua at load
// (TryDisableEngineYieldIconManager), both DiploCorner.lua copies (CanScheduleResync each turn and on a multiplayer
// update, TryScheduleResync on its button, TryEnableTunerInMultiplayer on a key, multiplayer only). On this route the DLL
// runs in server.exe and answers as for an unknown EXE. The proxy is in the EXE's process and answers instead: after the
// DLL's globals are set in a registration state (the EXE copies them into every UI context, proxy-design 24.2 B1), each
// function of the registered Exe table is replaced by the proxy's own C function; no call crosses to the server.
//   GetBuildName: "DX11" on a build of the EXE registry (the product runs only the DX11 Steam EXE), else "Unknown".
//   Can/TryGrowUICommandStream, Can/TryGetUICommandStreamUsage: ProxyUiDraw's state (proxy_uidraw.h LuaCanGrow ...):
//     the bridge's frame hook grows the streams, the Lua call never does, so no stream is grown twice.
//   Can/TryScheduleResync, Can/TryDisableEngineYieldIconManager, Can/TryEnableTunerInMultiplayer: "not_implemented"
//     (each acts on EXE objects; no port yet). Single player, the product's game: stock hides the resync button too
//     (it answers "not_network_game"), and the tuner key is not asked outside multiplayer. The engine's own
//     YieldIconManager keeps raising Events.ShowHexYield, which VP 5.5's own YieldIconManager.lua does not listen to.
// Off the EXE's main thread (the thread of the ui channel's registrations) every function answers "not_ui_thread", as
// upstream's do on the game-core thread. A registration without an Exe table (VP 5.4.8's DLL) is left as it is.
namespace
{
volatile LONG g_exeUiThread = 0;   // the thread of the ui channel's RegisterScriptLibraries (the EXE's main thread)
volatile LONG g_exeTables = 0;     // registration states whose Exe table was replaced
volatile LONG g_exeLogs = 0;       // answer lines logged (the first answer of each function)
enum { kExeFuncs = 11, kExeLogMax = 40 };
volatile LONG g_exeFirst[kExeFuncs];

bool ExeOffMainThread()
{
	const LONG t = g_exeUiThread;
	return t && (DWORD)t != GetCurrentThreadId();
}

int ExeReply(lua_State* L, int fn, const char* name, const char* why)
{
	if (fn >= 0 && fn < kExeFuncs && InterlockedCompareExchange(&g_exeFirst[fn], 1, 0) == 0 &&
	    InterlockedIncrement(&g_exeLogs) <= kExeLogMax)
		Log("exe table: first call of Exe.%s: %s%s (answered by the proxy, ticket 0765)", name, why ? "false, " : "true",
		    why ? why : "");
	lua_pushboolean(L, why == NULL);
	if (!why) return 1;
	lua_pushstring(L, why);
	return 2;
}

const char* ExeNotImplemented()
{
	return ExeOffMainThread() ? "not_ui_thread" : "not_implemented";
}

int ExeGetBuildName(lua_State* L)
{
	uint32_t stamp = 0;
	lua_pushstring(L, ProxySession::IsKnownExe(GetModuleHandleW(NULL), &stamp) ? "DX11" : "Unknown");
	return 1;
}
int ExeCanScheduleResync(lua_State* L) { return ExeReply(L, 1, "CanScheduleResync", ExeNotImplemented()); }
int ExeTryScheduleResync(lua_State* L) { return ExeReply(L, 2, "TryScheduleResync", ExeNotImplemented()); }
int ExeCanDisableYieldIcons(lua_State* L)
{
	return ExeReply(L, 3, "CanDisableEngineYieldIconManager", ExeNotImplemented());
}
int ExeTryDisableYieldIcons(lua_State* L)
{
	return ExeReply(L, 4, "TryDisableEngineYieldIconManager", ExeNotImplemented());
}
int ExeCanEnableTuner(lua_State* L) { return ExeReply(L, 5, "CanEnableTunerInMultiplayer", ExeNotImplemented()); }
int ExeTryEnableTuner(lua_State* L) { return ExeReply(L, 6, "TryEnableTunerInMultiplayer", ExeNotImplemented()); }
int ExeCanGrow(lua_State* L)
{
	return ExeReply(L, 7, "CanGrowUICommandStream", ExeOffMainThread() ? "not_ui_thread" : ProxyUiDraw::LuaCanGrow());
}
int ExeTryGrow(lua_State* L)
{
	// "pending" is answered every frame until the frame hook has grown the streams: logged once, with the first answer
	return ExeReply(L, 8, "TryGrowUICommandStream", ExeOffMainThread() ? "not_ui_thread" : ProxyUiDraw::LuaTryGrow());
}
int ExeCanUsage(lua_State* L)
{
	return ExeReply(L, 9, "CanGetUICommandStreamUsage",
	                ExeOffMainThread() ? "not_ui_thread" : ProxyUiDraw::LuaUsage(NULL, NULL));
}
int ExeTryUsage(lua_State* L)
{
	unsigned long used = 0, cap = 0;
	const char* why = ExeOffMainThread() ? "not_ui_thread" : ProxyUiDraw::LuaUsage(&used, &cap);
	ExeReply(L, 10, "TryGetUICommandStreamUsage", why);   // pushes false and the reason, or true
	if (why) return 2;
	lua_createtable(L, 0, 2);
	lua_pushinteger(L, (lua_Integer)used);
	lua_setfield(L, -2, "used");
	lua_pushinteger(L, (lua_Integer)cap);
	lua_setfield(L, -2, "capacity");
	return 2;
}

// Replaces the functions of the Exe table the DLL registered in L (nothing when there is none): upstream's 11 names.
void ReplaceExeTable(lua_State* L)
{
	static const struct
	{
		const char* name;
		lua_CFunction fn;
	} kFuncs[] = {
		{"GetBuildName", ExeGetBuildName},
		{"CanScheduleResync", ExeCanScheduleResync},
		{"TryScheduleResync", ExeTryScheduleResync},
		{"CanDisableEngineYieldIconManager", ExeCanDisableYieldIcons},
		{"TryDisableEngineYieldIconManager", ExeTryDisableYieldIcons},
		{"CanEnableTunerInMultiplayer", ExeCanEnableTuner},
		{"TryEnableTunerInMultiplayer", ExeTryEnableTuner},
		{"CanGrowUICommandStream", ExeCanGrow},
		{"TryGrowUICommandStream", ExeTryGrow},
		{"CanGetUICommandStreamUsage", ExeCanUsage},
		{"TryGetUICommandStreamUsage", ExeTryUsage},
	};
	const int top = lua_gettop(L);
	lua_getfield(L, LUA_GLOBALSINDEX, "Exe");
	if (!lua_istable(L, -1))
	{
		lua_settop(L, top);
		return;
	}
	unsigned had = 0;
	for (size_t i = 0; i < sizeof(kFuncs) / sizeof(kFuncs[0]); ++i)
	{
		lua_getfield(L, -1, kFuncs[i].name);
		had += lua_isnil(L, -1) ? 0u : 1u;
		lua_pop(L, 1);
		lua_pushcfunction(L, kFuncs[i].fn);
		lua_setfield(L, -2, kFuncs[i].name);
	}
	lua_settop(L, top);
	const LONG n = InterlockedIncrement(&g_exeTables);
	if (n <= 3 || n % 100 == 0)
		Log("exe table: the DLL's Exe table in state %p (%u of %u functions registered by the DLL) answered by the proxy "
		    "from now on (Vox Populi 5.5, ticket 0765; registration %ld)", L, had,
		    (unsigned)(sizeof(kFuncs) / sizeof(kFuncs[0])), n);
}
}  // namespace

// RegisterScriptLibraries(L): the server runs the DLL's own RegisterScriptLibraries on a fresh Lua state and returns
// the globals it created or changed (kRt_LuaRegister); they are rebuilt here with thunks. A global that is a table
// on both sides is merged (the server's keys win); LuaTypes is built lazily per type instead (PushTypeTable).
void ProxyHook_ICvScriptSystemUtility1_RegisterScriptLibraries(Proxy_ICvScriptSystemUtility1&, lua_State* L)
{
	if (!L) return;
	const double t0 = Now();
	vpipc::Channel& ch = ProxyRt::Chan();
	ProxyRt::Begin(ch, vpw::kRt_LuaRegister).u32(vplr::kVersion);  // ticket 4804: the Lua bridge version
	vpipc::Reader r0;
	if (!ProxyRt::End(ch, vpw::kRt_LuaRegister, r0))
	{
		Log("RegisterScriptLibraries(%p): kRt_LuaRegister failed; no DLL globals in this state", L);
		return;
	}
	vpipc::Reader r;
	uint8_t* buf = CopyRest(r0, r);
	const uint32_t n = r.u32();
	uint32_t set = 0, merged = 0, skipped = 0;
	const int base = lua_gettop(L);
	int typesRef = LUA_NOREF, postRef = LUA_NOREF;  // issue 0315
	for (uint32_t i = 0; i < n && r.ok(); ++i)
	{
		uint32_t len = 0;
		const char* s = r.cstr(&len);
		const std::string name = s ? std::string(s, len) : std::string();
		Dec(L, r, 0);
		// Issue 0315: master's types and the DLL's engine calls of its registration, used after the globals are set.
		if (name == kLuaDllTypesKey || name == kLuaPostCallsKey)
		{
			int& ref = name == kLuaDllTypesKey ? typesRef : postRef;
			if (lua_istable(L, -1) && ref == LUA_NOREF)
				ref = luaL_ref(L, LUA_REGISTRYINDEX);
			else
				lua_pop(L, 1);
			++skipped;
			continue;
		}
		// Issue 0313: on upstream master the server adds one entry, {name = DLL function, ...}, with every name it
		// recorded (Game, Map and the instance types are userdata there, whose function tables this side never sees):
		// the names only, never a global.
		if (name == kLuaFuncNamesKey)
		{
			uint32_t named = 0;
			if (lua_istable(L, -1))
			{
				lua_pushnil(L);
				while (lua_next(L, -2))
				{
					if (lua_type(L, -2) == LUA_TSTRING && lua_tocfunction(L, -1) == FuncThunk)
					{
						NoteThunkName(L, -1, lua_tostring(L, -2));
						++named;
					}
					lua_pop(L, 1);
				}
			}
			lua_pop(L, 1);
			++skipped;
			Log("RegisterScriptLibraries(%p): %u DLL function names from the server (issue 0313; %u pure-listed ids)", L,
			    named, PureFuncIds());
			continue;
		}
		// Issue 2805: the UI-Lua mirror's names ("Game.GetGameTurn", global functions by their own name).
		if (!name.empty() && name != "LuaTypes")
		{
			if (lua_istable(L, -1))
			{
				lua_pushnil(L);
				while (lua_next(L, -2))
				{
					if (lua_type(L, -2) == LUA_TSTRING) NoteThunkName(L, -1, name + "." + lua_tostring(L, -2));
					lua_pop(L, 1);
				}
			}
			else
				NoteThunkName(L, -1, name);
		}
		if (name.empty() || name == "LuaTypes")
		{
			lua_pop(L, 1);
			++skipped;
			continue;
		}
		lua_pushlstring(L, name.c_str(), name.size());
		lua_rawget(L, LUA_GLOBALSINDEX);  // old, new
		if (lua_istable(L, -1) && lua_istable(L, -2))
		{
			lua_pushnil(L);
			while (lua_next(L, -3))
			{
				lua_pushvalue(L, -2);
				lua_insert(L, -2);
				lua_rawset(L, -4);
			}
			lua_pop(L, 2);
			++merged;
		}
		else
		{
			lua_pop(L, 1);
			lua_setfield(L, LUA_GLOBALSINDEX, name.c_str());
			++set;
		}
	}
	lua_settop(L, base);
	// The DLL sets the globals Team and Player to LuaTypes.Team / LuaTypes.Player (CvLuaSupport.cpp:69-74); the server
	// sends type tables empty, so alias them here to the engine-side type tables (integration 2026-11-10).
	{
		// Issue 0314: upstream master's DLL sets neither global (its Team and Player are userdata types in the registry):
		// no alias for a master-form type.
		// Not after a registration that brought no global (upstream master's DLL registers nothing in the thread of its
		// NotifyDatabaseCached, issue 0308): the server knows no type then, and a description asked for now would be
		// empty.
		static const char* const kAliases[] = { "Team", "Player" };
		for (int k = 0; k < 2 && n > 0; ++k)
		{
			if (MasterForm(kAliases[k])) continue;
			PushTypeTable(L, kAliases[k]);
			lua_setfield(L, LUA_GLOBALSINDEX, kAliases[k]);
		}
		lua_settop(L, base);
	}
	ProxyRt::DecodeCheck(vpw::kRt_LuaRegister, r);
	free(buf);
	// Issue 0315: the engine registry's vpdllinternals, then the DLL's calls (upstream master's gc_post), as at the end
	// of DoRegisterScriptData in the EXE.
	if (typesRef != LUA_NOREF)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, typesRef);
		MirrorDllTypes(L, lua_gettop(L));
		luaL_unref(L, LUA_REGISTRYINDEX, typesRef);
		lua_settop(L, base);
	}
	if (postRef != LUA_NOREF)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, postRef);
		MakePostCalls(L, lua_gettop(L));
		luaL_unref(L, LUA_REGISTRYINDEX, postRef);
		lua_settop(L, base);
	}
	Log("RegisterScriptLibraries(%p): %u globals from the server: %u set, %u merged, %u skipped (%.1f ms) on thread %lu", L,
	    n, set, merged, skipped, (Now() - t0) * 1000.0, GetCurrentThreadId());
	// Ticket 0765: VP 5.5's Exe table answered by the proxy (above); the ui channel's thread is the EXE's main thread.
	{
		uint32_t chan = 0;
		if (ProxyRtInt::ThreadChannel(chan) && chan == vpipc::kChannelUi)
			InterlockedExchange(&g_exeUiThread, (LONG)GetCurrentThreadId());
		ReplaceExeTable(L);
	}
	// Issue 0178: the Lua bridge reads its switches here and learns the main thread (vpproxy.ini ui_bridge=1; nothing
	// otherwise); it installs later, from FuncThunk, into the in-game context (proxy_ui_bridge.h).
	ProxyUiBridgeRt::OnStateRegistered(L);
}

void ProxyHook_ICvScriptSystemUtility1_PushCvCityInstance(Proxy_ICvScriptSystemUtility1& self, lua_State* L, ICvCity1* pkCity)
{
	PushInstanceHook(kM_ICvScriptSystemUtility1_PushCvCityInstance, self.ProxyHandle(), L, pkCity);
}
void ProxyHook_ICvScriptSystemUtility1_PushCvDealInstance(Proxy_ICvScriptSystemUtility1& self, lua_State* L, ICvDeal1* pkDeal)
{
	PushInstanceHook(kM_ICvScriptSystemUtility1_PushCvDealInstance, self.ProxyHandle(), L, pkDeal);
}
void ProxyHook_ICvScriptSystemUtility1_PushCvPlotInstance(Proxy_ICvScriptSystemUtility1& self, lua_State* L, ICvPlot1* pkPlot)
{
	PushInstanceHook(kM_ICvScriptSystemUtility1_PushCvPlotInstance, self.ProxyHandle(), L, pkPlot);
}
void ProxyHook_ICvScriptSystemUtility1_PushCvUnitInstance(Proxy_ICvScriptSystemUtility1& self, lua_State* L, ICvUnit1* pkUnit)
{
	PushInstanceHook(kM_ICvScriptSystemUtility1_PushCvUnitInstance, self.ProxyHandle(), L, pkUnit);
}

// Get*Instance: no C++ object with a destructor may live in these frames (luaL_error longjmps).
ICvCity1* ProxyHook_ICvScriptSystemUtility1_GetCvCityInstance(Proxy_ICvScriptSystemUtility1& self, lua_State* L, int index, bool bErrorOnFail)
{
	char err[1024];
	bool raise;
	const vpipc::Handle h = GetInstanceCall(kM_ICvScriptSystemUtility1_GetCvCityInstance, self.ProxyHandle(), L, index,
	                                        bErrorOnFail, err, sizeof(err), raise);
	if (raise) luaL_error(L, "%s", err);
	return static_cast<ICvCity1*>(ProxyRt::Wrap(h));
}
ICvDeal1* ProxyHook_ICvScriptSystemUtility1_GetCvDealInstance(Proxy_ICvScriptSystemUtility1& self, lua_State* L, int index, bool bErrorOnFail)
{
	char err[1024];
	bool raise;
	const vpipc::Handle h = GetInstanceCall(kM_ICvScriptSystemUtility1_GetCvDealInstance, self.ProxyHandle(), L, index,
	                                        bErrorOnFail, err, sizeof(err), raise);
	if (raise) luaL_error(L, "%s", err);
	return static_cast<ICvDeal1*>(ProxyRt::Wrap(h));
}
ICvPlot1* ProxyHook_ICvScriptSystemUtility1_GetCvPlotInstance(Proxy_ICvScriptSystemUtility1& self, lua_State* L, int index, bool bErrorOnFail)
{
	char err[1024];
	bool raise;
	const vpipc::Handle h = GetInstanceCall(kM_ICvScriptSystemUtility1_GetCvPlotInstance, self.ProxyHandle(), L, index,
	                                        bErrorOnFail, err, sizeof(err), raise);
	if (raise) luaL_error(L, "%s", err);
	return static_cast<ICvPlot1*>(ProxyRt::Wrap(h));
}
ICvUnit1* ProxyHook_ICvScriptSystemUtility1_GetCvUnitInstance(Proxy_ICvScriptSystemUtility1& self, lua_State* L, int index, bool bErrorOnFail)
{
	char err[1024];
	bool raise;
	const vpipc::Handle h = GetInstanceCall(kM_ICvScriptSystemUtility1_GetCvUnitInstance, self.ProxyHandle(), L, index,
	                                        bErrorOnFail, err, sizeof(err), raise);
	if (raise) luaL_error(L, "%s", err);
	// The handle's kind is ICvUnit2 (MD of ICvUnit1): Wrap returns an ICvUnit2*.
	return static_cast<ICvUnit1*>(static_cast<ICvUnit2*>(ProxyRt::Wrap(h)));
}

// PushReplayFromStream(L, stream): CALL self, stream-in; RETURN Lua value, u32 consumed.
void ProxyHook_ICvScriptSystemUtility1_PushReplayFromStream(Proxy_ICvScriptSystemUtility1& self, lua_State* L, FDataStream& stream)
{
	if (!L) return;
	const uint32_t m = kM_ICvScriptSystemUtility1_PushReplayFromStream;
	vpipc::Channel& ch = ProxyRt::Chan();
	vpipc::Writer& w = ProxyRt::Begin(ch, m);
	w.handle(self.ProxyHandle());
	ProxyRt::StreamInArg(w, stream);
	vpipc::Reader r0;
	if (!ProxyRt::End(ch, m, r0))
	{
		lua_pushnil(L);
		return;
	}
	vpipc::Reader r;
	uint8_t* buf = CopyRest(r0, r);
	Dec(L, r, 0);
	ProxyRt::StreamInRet(r, stream);
	ProxyRt::DecodeCheck(m, r);
	free(buf);
}

// ================================================================================================ engine hooks
// CreateLuaThread(name): the real engine thread is kept in the proxy's thread table; the server gets its id.
bool EngHook_ICvEngineScriptSystem1_CreateLuaThread(ICvEngineScriptSystem1* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	const char* name = args.cstr();
	if (!args.ok()) return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "CreateLuaThread: bad arguments");
	lua_State* T = self->CreateLuaThread(name ? name : "");
	uint32_t id = 0;
	if (T)
	{
		LuaLock l;
		id = ++g_nextThread;
		g_threads[id] = T;
	}
	Log("CreateLuaThread(%s): engine thread %p, id %u", name ? name : "", T, id);
	ret.u32(id);
	return true;
}

bool EngHook_ICvEngineScriptSystem1_FreeLuaThread(ICvEngineScriptSystem1* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	const uint32_t id = args.u32();
	if (!args.ok()) return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "FreeLuaThread: bad arguments");
	lua_State* T = NULL;
	{
		LuaLock l;
		std::map<uint32_t, lua_State*>::iterator it = g_threads.find(id);
		if (it != g_threads.end())
		{
			T = it->second;
			g_threads.erase(it);
		}
	}
	if (!T)
	{
		Log("FreeLuaThread(%u): unknown engine Lua thread id", id);
		return true;
	}
	self->FreeLuaThread(T);
	Log("FreeLuaThread(%u): engine thread %p freed", id, T);
	return true;
}

static lua_State* ThreadOf(uint32_t id)
{
	LuaLock l;
	std::map<uint32_t, lua_State*>::iterator it = g_threads.find(id);
	return it == g_threads.end() ? NULL : it->second;
}

bool EngHook_ICvEngineScriptSystem1_LoadFile(ICvEngineScriptSystem1* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	const uint32_t id = args.u32();
	const char* file = args.cstr();
	if (!args.ok()) return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "LoadFile: bad arguments");
	lua_State* T = ThreadOf(id);
	if (!T) return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadHandle, "LoadFile: unknown engine Lua thread id");
	const bool ok = self->LoadFile(T, file ? file : "");
	Log("LoadFile(thread %u, %s): %s", id, file ? file : "", ok ? "ok" : "FAILED");
	ret.b(ok);
	return true;
}

bool EngHook_ICvEngineScriptSystemArgs1_GetString(ICvEngineScriptSystemArgs1* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	const uint32_t index = args.u32();
	if (!args.ok()) return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "GetString: bad arguments");
	char* s = NULL;
	const bool ok = self->GetString(index, s);
	ret.b(ok);
	ret.str(ok ? s : NULL);
	return true;
}

// ================================================================================================ served runtime methods
namespace
{
int GetGlobalFn(lua_State* L)
{
	lua_getfield(L, LUA_GLOBALSINDEX, lua_tostring(L, 1));
	return 1;
}

bool ServeGetGlobal(vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	const uint32_t id = args.u32();
	uint32_t len = 0;
	const char* name = args.cstr(&len);
	if (!args.ok() || !name) return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "LuaGetGlobal: bad arguments");
	lua_State* T = ThreadOf(id);
	if (!T)
	{
		char msg[256];
		size_t n;
		{
			LuaLock l;
			n = g_threads.size();
		}
		_snprintf_s(msg, sizeof(msg), _TRUNCATE, "LuaGetGlobal: unknown engine Lua thread id %u (global '%s'; %u threads known, next id %u)",
		            id, name, (unsigned)n, g_nextThread + 1);
		return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadHandle, msg);
	}
	if (!lua_checkstack(T, 8)) return ProxyRt::DispatchFail(ch, ret, vpw::kStatusFailed, "LuaGetGlobal: Lua stack full");
	const int base = lua_gettop(T);
	lua_pushcfunction(T, GetGlobalFn);
	lua_pushlstring(T, name, len);
	if (lua_pcall(T, 1, 1, 0) != 0)
	{
		const std::string msg = std::string("LuaGetGlobal: ") + (lua_tostring(T, -1) ? lua_tostring(T, -1) : "error");
		lua_settop(T, base);
		return ProxyRt::DispatchFail(ch, ret, vpw::kStatusFailed, msg.c_str());
	}
	EncTop(T, -1, ret);
	lua_settop(T, base);
	return true;
}

bool ServeLuaCall(vpipc::Reader& args0, vpipc::Writer& ret, vpipc::Channel& ch)
{
	// Decoding can make nested calls (instances: kRt_LuaDescribeType): decode from a copy.
	vpipc::Reader args;
	uint8_t* buf = CopyRest(args0, args);
	const uint32_t id = args.u32();
	lua_State* T = ThreadOf(id);
	if (!T || !args.ok())
	{
		free(buf);
		return ProxyRt::DispatchFail(ch, ret, T ? vpw::kStatusBadArguments : vpw::kStatusBadHandle,
		                             "LuaCall: unknown engine Lua thread id or bad arguments");
	}
	ICvEngineScriptSystem1* ss = EngineScriptSystem();
	const int base = lua_gettop(T);
	Dec(T, args, 0);  // callee
	const uint32_t nargs = args.u32();
	if (!args.ok() || !lua_checkstack(T, (int)nargs + 8))
	{
		lua_settop(T, base);
		free(buf);
		return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "LuaCall: bad arguments");
	}
	for (uint32_t i = 0; i < nargs; ++i) Dec(T, args, 0);
	const uint32_t nret = args.u32();
	const bool argsOk = args.ok();
	free(buf);
	if (!argsOk)
	{
		lua_settop(T, base);
		return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "LuaCall: bad arguments");
	}
	if (!ss)
	{
		lua_settop(T, base);
		return ProxyRt::DispatchFail(ch, ret, vpw::kStatusFailed, "LuaCall: no engine script system (SetDLLIFace not seen)");
	}
	const bool ok = ss->CallFunction(T, nargs, nret);
	const int top = lua_gettop(T);
	if (ok)
	{
		const int n = top > base ? top - base : 0;
		ret.u32(0);
		ret.u32((uint32_t)n);
		for (int i = 1; i <= n; ++i) EncTop(T, base + i, ret);
	}
	else
	{
		const char* m = (top > base && lua_isstring(T, -1)) ? lua_tostring(T, -1) : "CallFunction failed";
		ret.u32(1);
		ret.str(m);
	}
	lua_settop(T, base);
	return true;
}

bool ServeLuaRelease(vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	const uint32_t n = args.u32();
	for (uint32_t i = 0; i < n && args.ok(); ++i)
	{
		const uint32_t id = args.u32();
		if (!args.ok()) break;
		lua_State* a = NULL;
		int ref = LUA_NOREF;
		{
			LuaLock l;
			if (id && id <= g_erefs.size() && g_erefs[id - 1].live)
			{
				a = g_erefs[id - 1].anchor;
				ref = g_erefs[id - 1].ref;
				g_erefs[id - 1].live = false;
				g_erefFree.push_back(id);
			}
		}
		if (a) luaL_unref(a, LUA_REGISTRYINDEX, ref);
	}
	if (!args.ok()) return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "LuaRelease: bad arguments");
	return true;
}

// kRtE_LuaRunChunk (issue 2504): a chunk the DLL loaded with luaL_loadstring on a thread it got from CreateLuaThread
// (LuaSupport::InitLuaFramework, CvLuaSupport.cpp: the VP_LUAAPI framework) runs here, in the engine VM, on that
// thread, as it does when the DLL is in the EXE. The globals the DLL set on its thread first (InitLuaFramework:
// REGISTRY = the thread's registry) are set on the engine thread the same way (lua_setfield, so an __newindex of the
// engine's environment sees them as it would).
int SetGlobalFn(lua_State* L)
{
	// stack: name, value
	lua_settop(L, 2);
	lua_setfield(L, LUA_GLOBALSINDEX, lua_tostring(L, 1));
	return 0;
}

bool ServeRunChunk(vpipc::Reader& args0, vpipc::Writer& ret, vpipc::Channel& ch)
{
	vpipc::Reader args;
	uint8_t* buf = CopyRest(args0, args);  // decoding values can make nested calls
	const uint32_t id = args.u32();
	lua_State* T = ThreadOf(id);
	if (!T || !args.ok())
	{
		free(buf);
		return ProxyRt::DispatchFail(ch, ret, T ? vpw::kStatusBadArguments : vpw::kStatusBadHandle,
		                             "LuaRunChunk: unknown engine Lua thread id or bad arguments");
	}
	const int base = lua_gettop(T);
	const uint32_t ng = args.u32();
	std::string err;
	for (uint32_t i = 0; i < ng && args.ok() && err.empty(); ++i)
	{
		const char* name = args.cstr();
		const uint32_t kind = args.u32();
		if (!args.ok() || !name || !lua_checkstack(T, 8)) break;
		const std::string nm = name;
		lua_pushcfunction(T, SetGlobalFn);
		lua_pushstring(T, nm.c_str());
		if (kind == 1)
			lua_pushvalue(T, LUA_REGISTRYINDEX);
		else
			Dec(T, args, 0);
		if (lua_pcall(T, 2, 0, 0) != 0)
			err = "setting global " + nm + ": " + (lua_tostring(T, -1) ? lua_tostring(T, -1) : "error");
		lua_settop(T, base);
	}
	uint32_t clen = 0;
	const char* c = args.cstr(&clen);
	const std::string chunk = c ? std::string(c, clen) : std::string();
	const bool argsOk = args.ok();
	free(buf);
	if (!argsOk)
	{
		lua_settop(T, base);
		return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "LuaRunChunk: bad arguments");
	}
	if (err.empty())
	{
		int rc = luaL_loadstring(T, chunk.c_str());
		if (rc == 0) rc = lua_pcall(T, 0, LUA_MULTRET, 0);
		if (rc != 0) err = lua_tostring(T, -1) ? lua_tostring(T, -1) : "(non-string error)";
	}
	if (err.empty())
	{
		const int n = lua_gettop(T) - base;
		ret.u32(0);
		ret.u32((uint32_t)(n > 0 ? n : 0));
		for (int i = 1; i <= n; ++i) EncTop(T, base + i, ret);
	}
	else
	{
		ret.u32(1);
		ret.str(err.c_str());
	}
	lua_settop(T, base);
	Log("LuaRunChunk(thread %u, %u globals, %u bytes): %s%s", id, ng, (unsigned)chunk.size(), err.empty() ? "ok" : "error: ",
	    err.c_str());
	return true;
}

// kRtE_LuaRaise (ticket 4804, speedup P2): one GameEvents raise of the DLL, served on the EXE thread that waits in it.
// Everything is decoded and checked from a private copy first (the listeners call back into the server on this channel,
// which reuses the channel's payload area); then vplr::Serve makes the EXE's CreateArgs, the pushes, the Call* and
// DestroyArgs in that order, as the generated calls did one crossing each. A refused CALL makes no EXE call; the server
// ends the session on any failure (D118). State kept: two interlocked counters (for the log lines), nothing else.
volatile LONG g_nRaises = 0, g_nRaiseRefused = 0;

bool ServeLuaRaise(vpipc::Reader& args0, vpipc::Writer& ret, vpipc::Channel& ch)
{
	vpipc::Reader args;
	uint8_t* buf = CopyRest(args0, args);
	vplr::Call c;
	const char* why = NULL;
	if (!vplr::Decode(args, c, why))
	{
		free(buf);
		char msg[160];
		_snprintf_s(msg, sizeof(msg), _TRUNCATE, "LuaRaise: bad arguments (%s)", why ? why : "?");
		if (InterlockedIncrement(&g_nRaiseRefused) <= 20) Log("LOUD: %s", msg);
		return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, msg);
	}
	bool handled = false;
	uint32_t value = 0;
	const int st = vplr::Serve<ICvEngineScriptSystem1, ICvEngineScriptSystemArgs1>(EngineScriptSystem(), c, handled, value);
	if (st != vplr::kServed)
	{
		char msg[256];
		_snprintf_s(msg, sizeof(msg), _TRUNCATE, "LuaRaise %s(%s): %s", vplr::KindName(c.kind), c.name,
		            st == vplr::kNoScriptSystem ? "no engine script system (SetDLLIFace not seen)"
		                                        : "the engine's CreateArgs returned no object");
		free(buf);
		if (InterlockedIncrement(&g_nRaiseRefused) <= 20) Log("LOUD: %s", msg);
		return ProxyRt::DispatchFail(ch, ret, vpw::kStatusFailed, msg);
	}
	free(buf);
	if (InterlockedIncrement(&g_nRaises) == 1) Log("LuaRaise: first GameEvents raise in one crossing (ticket 4804)");
	ret.u32(handled ? 1u : 0u);
	ret.u32(value);
	return true;
}
}  // namespace

bool ProxyDbServe(uint32_t method, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch);  // proxy_db.cpp

namespace ProxyRtInt
{
bool LuaServe(uint32_t method, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	switch (method)
	{
	case vpw::kRtE_LuaGetGlobal: return ServeGetGlobal(args, ret, ch);
	case vpw::kRtE_LuaCall: return ServeLuaCall(args, ret, ch);
	case vpw::kRtE_LuaRelease: return ServeLuaRelease(args, ret, ch);
	case vpw::kRtE_LuaRunChunk: return ServeRunChunk(args, ret, ch);
	case vpw::kRtE_LuaRaise: return ServeLuaRaise(args, ret, ch);  // ticket 4804
	default: return ProxyDbServe(method, args, ret, ch);  // kRtE_Db* (proxy_db.cpp)
	}
}
}  // namespace ProxyRtInt

// Issue 2805: per-function lines of the UI-Lua mirror (ProxyLocal::Report: every snapshot and the shutdown): the 20
// pure-listed functions with the most candidate calls (pure-listed, simple arguments), with hits (lua_mirror=1) or
// would-hits (lua_mirror=0) and fills (scalar replies offered to the table).
void ProxyLuaMirrorReport()
{
	std::vector<std::pair<LONG, uint32_t> > v;
	for (uint32_t i = 1; i < kFnMax; ++i)
		if (g_fnCand[i]) v.push_back(std::make_pair(g_fnCand[i], i));
	std::sort(v.begin(), v.end());
	std::reverse(v.begin(), v.end());
	for (size_t k = 0; k < v.size() && k < 20; ++k)
	{
		const uint32_t i = v[k].second;
		Log("local: UI-Lua mirror %-42s candidates %ld, hits %ld, would-hits %ld, fills %ld", g_fnName[i].c_str(),
		    g_fnCand[i], g_fnHit[i], g_fnWould[i], g_fnStore[i]);
	}
	Log("local: Lua bridge (issue 0313): DLL functions of server-side userdata from the per-stub cache %ld (filled %ld); "
	    "pure-listed calls with server-side userdata arguments crossed without moving the epoch %ld",
	    g_nMethodHits, g_nMethodFills, g_nPureStubCalls);
	// Issue 0314: the server-value stub cache's checks and the instances of upstream master's form.
	Log("local: Lua bridge (issue 0314): finalized server-value stubs met in the cache and replaced %ld, LIVE stubs of "
	    "another type than the server sent %ld; master-form instances pushed %ld (made %ld), master-form types %ld",
	    g_nStaleStubs, g_nStubTypeMismatch, g_nMasterInstPushed, g_nMasterInstMade, g_nMasterTypes);
	// Issue 0315: upstream master's GamecoreInit.lua data on the engine side.
	Log("local: Lua bridge (issue 0315): vpdllinternals mirrors %ld; the DLL's registration calls made after an engine "
	    "registration %ld (failed %ld)", g_nDllTypeMirrors, g_nPostCalls, g_nPostCallErrors);
}

// ================================================================================================ ticket 0343
// ICvWorldBuilderMapLoader1's six Lua methods: the four a map start needs (LoadModData, GetMapPreview, GetMapPlayers,
// AddRandomItems; ticket 0343) and ScatterResources and ScatterGoodies (ticket 0326). The engine passes one of its own lua_States; for the length of the call
// it is an engine Lua thread the server can reach (globals, LoadFile, calls of the functions it fetches: the server's
// WbServe in srv_remote_lua.cpp, with the protocol there). The method's results are pushed here; for LoadModData the
// two fields it sets (Globals, Plots) are set into the engine's MapModData table. A Lua error in the DLL's method is
// logged and the call returns no results (the 32-bit DLL would raise it in the calling script).
namespace
{
int WbLuaMethod(Proxy_ICvWorldBuilderMapLoader2& self, lua_State* L, uint32_t m, const char* what, bool bModData)
{
	if (!L) return 0;
	const int top = lua_gettop(L);
	int iModData = 0;
	if (bModData)
	{
		lua_getglobal(L, "MapModData");
		if (!lua_istable(L, -1))
		{
			lua_settop(L, top);  // the DLL's LoadModData returns 0 without a MapModData table
			return 0;
		}
		iModData = lua_gettop(L);
	}
	uint32_t id;
	{
		LuaLock l;
		id = ++g_nextThread;
		g_threads[id] = L;
	}
	FlushReleases(CurrentChannelIndex());
	vpipc::Channel& ch = ProxyRt::Chan();
	vpipc::Writer& w = ProxyRt::Begin(ch, m);
	w.handle(self.ProxyHandle());
	w.u32(id);
	w.u32(bModData ? 1u : 0u);
	w.u32((uint32_t)top);
	for (int i = 1; i <= top; ++i) EncTop(L, i, w);
	vpipc::Reader r0;
	int pushed = 0;
	if (ProxyRt::End(ch, m, r0))
	{
		vpipc::Reader r;
		uint8_t* buf = CopyRest(r0, r);
		const uint32_t status = r.u32();
		if (status == 0)
		{
			r.i32();  // the DLL's result: the count of values that follow
			const uint32_t n = r.u32();
			if (r.ok() && n < 256 && lua_checkstack(L, (int)n + 8))
			{
				for (uint32_t i = 0; i < n; ++i) Dec(L, r, 0);
				pushed = (int)n;
				if (bModData)
				{
					lua_pushstring(L, "Globals");
					Dec(L, r, 0);
					lua_rawset(L, iModData);
					lua_pushstring(L, "Plots");
					Dec(L, r, 0);
					lua_rawset(L, iModData);
					lua_remove(L, iModData);
				}
			}
		}
		else
		{
			uint32_t len = 0;
			const char* msg = r.cstr(&len);
			Log("WorldBuilder %s: Lua error in the 64-bit DLL: %s", what, msg ? msg : "(no message)");
		}
		ProxyRt::DecodeCheck(m, r);
		free(buf);
	}
	{
		LuaLock l;
		g_threads.erase(id);
	}
	if (pushed == 0) lua_settop(L, top);
	return pushed;
}
}  // namespace

int ProxyHook_ICvWorldBuilderMapLoader2_LoadModData(Proxy_ICvWorldBuilderMapLoader2& self, lua_State* L)
{
	return WbLuaMethod(self, L, kM_ICvWorldBuilderMapLoader2_LoadModData, "LoadModData", true);
}
int ProxyHook_ICvWorldBuilderMapLoader2_GetMapPreview(Proxy_ICvWorldBuilderMapLoader2& self, lua_State* L)
{
	return WbLuaMethod(self, L, kM_ICvWorldBuilderMapLoader2_GetMapPreview, "GetMapPreview", false);
}
int ProxyHook_ICvWorldBuilderMapLoader2_GetMapPlayers(Proxy_ICvWorldBuilderMapLoader2& self, lua_State* L)
{
	return WbLuaMethod(self, L, kM_ICvWorldBuilderMapLoader2_GetMapPlayers, "GetMapPlayers", false);
}
int ProxyHook_ICvWorldBuilderMapLoader2_AddRandomItems(Proxy_ICvWorldBuilderMapLoader2& self, lua_State* L)
{
	return WbLuaMethod(self, L, kM_ICvWorldBuilderMapLoader2_AddRandomItems, "AddRandomItems", false);
}
// Ticket 0326: the two the engine's map tools call (each loads WorldBuilderRandomItems.lua into the engine's state and
// calls one of its global functions there: AddResourcesForWorldBuilderMap, AddGoodies; the DLL's methods return 0).
int ProxyHook_ICvWorldBuilderMapLoader2_ScatterResources(Proxy_ICvWorldBuilderMapLoader2& self, lua_State* L)
{
	return WbLuaMethod(self, L, kM_ICvWorldBuilderMapLoader2_ScatterResources, "ScatterResources", false);
}
int ProxyHook_ICvWorldBuilderMapLoader2_ScatterGoodies(Proxy_ICvWorldBuilderMapLoader2& self, lua_State* L)
{
	return WbLuaMethod(self, L, kM_ICvWorldBuilderMapLoader2_ScatterGoodies, "ScatterGoodies", false);
}
