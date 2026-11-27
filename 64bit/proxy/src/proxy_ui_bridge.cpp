// proxy_ui_bridge.cpp - the Lua bridge of the proxy (issue 0178). Hand-written. Header and design: proxy_ui_bridge.h.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// No dependency on the proxy runtime: the post and the log go through UiBridge::Hooks, so the same file is built into
// the proxy (clang-cl -m32, the fork's Lua 5.1 headers, the game's lua51_Win32.dll) and into the unit test
// (proxy/tests/ui_bridge/, stock Lua 5.1.4 built from source, also under MSVC ASan).
//
// Rules kept in this file:
//   - Every function Lua calls (the listeners, the drain, the UnitMoving replacement, the test table) is a plain C-style
//     function: no C++ object with a destructor lives in its frame, because a Lua error longjmps through it.
//   - The bridge never keeps a lua_State: each closure works on the state Lua passes to it. Closures carry their
//     generation as upvalue 1; an older generation's closure returns at once (counted as staleCalls).
//   - Installation runs under lua_cpcall (protected), so a missing Events table, a missing event or an error inside the
//     EXE's Add is logged, not raised into the EXE.
//   - The raise queue is the only state shared between threads (QueueRaise on any thread, the drain on the main thread):
//     a critical section around a fixed array, no allocation.
//   - Issue 4754 (epic 0040): the unit positions (kind 6) are held in a fixed buffer on the main thread and posted by the
//     drain, after one frame mark (kind 7) that counts them; the units' last positions sit in a fixed open-addressing
//     table (main thread only). Both are installed with the whole bridge (ui_bridge=1) when asked for, or alone
//     ("positions only": ConfigurePositions without Configure's enabled), which is the unit option's install (4759).
#include "proxy_ui_bridge.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace
{
using namespace UiBridge;

// ============================================================================================ state
Hooks g_hooks = { NULL, NULL, NULL };
volatile LONG g_enabled = 0;
volatile LONG g_test = 0;
void (*volatile g_frameHook)() = NULL;   // ticket 0487: called by the drain once per frame
void (*volatile g_frameLuaHook)(lua_State*) = NULL;   // ticket 0586: the same, with the drain's Lua state
inline bool HaveFrameHook() { return g_frameHook != NULL || g_frameLuaHook != NULL; }
volatile LONG g_mainThread = 0;
volatile LONG g_gen = 0;           // current generation; written on the main thread only
volatile LONG g_seq = 0;
Stats g_stats;                     // counters; written with Interlocked* where more than one thread writes

// Heartbeat (OnUpdate on the core thread reads what the drain writes on the main thread).
volatile LONG g_drainTick = 0;     // incremented by every drain of the current generation
LONG g_seenTick = 0;               // core thread only
uint32_t g_updatesSinceTick = 0;   // core thread only
bool g_stalled = false;            // core thread only

// The events the bridge listens to. The minimap broadcast carries up to four values (handle, width, height, and
// paddingX in some UI files).
struct ListenSpec
{
	const char* event;
	int kind;
};
const ListenSpec kListen[] = {
	{ "SpawnArrowEvent", kKindSpawnArrow },
	{ "RemoveAllArrowsEvent", kKindRemoveAllArrows },
	{ "MinimapTextureBroadcastEvent", kKindMinimapTexture },
	{ "DisplayMovementIndicator", kKindDisplayMovement },
};
const int kListenCount = sizeof(kListen) / sizeof(kListen[0]);
const char* const kDrainEvent = "LocalMachineAppUpdate";
const char* const kReplacedGlobal = "UnitMoving";
const char* const kTestTable = "VP64UiBridgeTest";
const char* const kInGameState = "InGame";
char kMarkerKey = 0;                 // its address keys the generation marker in the installed environment (not const:
                                     // a writable byte cannot be folded with another constant by the linker)

// The installation's place (main thread only).
const void* g_installedEnv = NULL;   // identity of the InGame environment installed into (never dereferenced)
bool g_installedPositions = false;   // issue 4754: the current generation's install has the position listeners
uint32_t g_lastCheck = 0;            // the clock at the last OnLuaEntry check
bool g_checked = false;
LONG g_tickAtCheck = 0;              // the drain tick seen at the last check
uint32_t g_tickChangedAt = 0;        // the clock when the drain tick last changed (or the install)

// ---- the raise queue -----------------------------------------------------------------------------------------------
enum { kQueueCap = 64, kNameCap = 64, kArgCap = 8 };
struct Raise
{
	char name[kNameCap];
	int nargs;
	Arg args[kArgCap];
};
Raise g_queue[kQueueCap];
int g_queueHead = 0, g_queueCount = 0;   // under g_cs
CRITICAL_SECTION g_cs;
struct CsInit
{
	CsInit() { InitializeCriticalSection(&g_cs); }
} g_csInit;

// ---- issue 4754: the unit positions (main thread only, except the switch) ------------------------------------------
volatile LONG g_positions = 0;        // ConfigurePositions(true)
const char* const kPositionEvent = "LocalMachineUnitPositionChanged";   // (playerID, unitID, {x, y, z, w})
const char* const kArrivalEvent = "UnitMoveQueueChanged";              // (playerID, unitID, bRemainingMoves)
enum { kPosListenCount = 2 };
Record g_posBuf[kPosBufCap];          // kind-6 records of one frame; a fuller frame is flushed early (a second mark)
int g_posCount = 0;
UiBridge::PositionStats g_pstats;
enum { kArgLogLines = 3 };            // F6: the first calls of each listener log their arguments

// ---- ticket 0199 part 2: the arc's end points (main thread only, except the switch) ---------------------------------
volatile LONG g_arrows = 0;           // ConfigureArrows(true)
const char* const kArrowEvent = "SpawnArrowEvent";
const char* const kRemoveArrowsEvent = "RemoveAllArrowsEvent";
char kArrowWrapKey = 0;               // its address keys "this table is the bridge's wrapper" (rawset true)
char kArrowOrigKey = 0;               // its address keys the wrapped original in the wrapper table
bool g_installedArrows = false;       // the current generation's install has the wrapper and the remove listener
bool g_arrowsTried = false;           // the current generation's install was asked for the arrows (not retried in it)
UiBridge::ArrowStats g_astats;
uint32_t g_arrowCalls = 0;            // main thread: the first calls log their arguments (as F6)

// ---- ticket 0300 part 2: the move-order path (main thread only, except the switches) ---------------------------------
volatile LONG g_path = 0;             // ConfigurePath(true)
const char* const kPathEvent = "UIPathFinderUpdate";        // (path): { {x, y, turn}, ... }
const char* const kShowEvent = "DisplayMovementIndicator";  // (bShow)
UiBridge::PathTestsFn volatile g_pathTests = NULL;
bool g_installedPath = false;         // the current generation's install has the path listener (and a show listener)
bool g_pathTried = false;             // the current generation's install was asked for the path (not retried in it)
UiBridge::PathStats g_pathStats;
uint32_t g_pathCalls = 0;             // main thread: the first calls log what they read
uint32_t g_pathUpdate = 0;            // the update number of the current generation (a[3] of the records)

// The units' last positions, for the arrival record: (player, unit) -> x, y, z bits. Open addressing, linear probing;
// cleared at a new InGame environment and when three quarters full (counted).
enum { kLastCap = 8192 };             // a power of two
struct LastPos
{
	int32_t unit;
	int16_t player;                   // -1 = empty slot
	int16_t pad;
	int32_t xyz[3];
};
LastPos g_last[kLastCap];
uint32_t g_lastUsed = 0;
bool g_lastInit = false;

void LastClear()
{
	for (int i = 0; i < kLastCap; ++i) g_last[i].player = -1;
	g_lastUsed = 0;
	g_lastInit = true;
}

uint32_t LastSlot(int player, int32_t unit)
{
	uint32_t h = (uint32_t)unit * 2654435761u ^ (uint32_t)player * 40503u;
	h ^= h >> 15;
	return h & (kLastCap - 1);
}

LastPos* LastFind(int player, int32_t unit, bool add)
{
	if (!g_lastInit) LastClear();
	if (add && g_lastUsed >= (kLastCap / 4) * 3)
	{
		LastClear();
		++g_pstats.tableClears;
	}
	for (uint32_t i = LastSlot(player, unit), n = 0; n < (uint32_t)kLastCap; i = (i + 1) & (kLastCap - 1), ++n)
	{
		LastPos& s = g_last[i];
		if (s.player == player && s.unit == unit) return &s;
		if (s.player < 0)
		{
			if (!add) return NULL;
			s.player = (int16_t)player;
			s.unit = unit;
			++g_lastUsed;
			return &s;
		}
	}
	return NULL;
}

// ============================================================================================ helpers
void Logf(const char* fmt, ...)
{
	if (!g_hooks.log) return;
	char line[768];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
	va_end(ap);
	g_hooks.log(line);
}

uint32_t NowMs()
{
	return g_hooks.now ? g_hooks.now() : (uint32_t)GetTickCount();
}

bool OnMainThread()
{
	const LONG main = g_mainThread;
	return main != 0 && (DWORD)main == GetCurrentThreadId();
}

const char* KindName(int k)
{
	switch (k)
	{
	case kKindSpawnArrow: return "arrow";
	case kKindRemoveAllArrows: return "remove-arrows";
	case kKindMinimapTexture: return "minimap";
	case kKindDisplayMovement: return "movement";
	case kKindUnitMovingCalled: return "unit-moving";
	case kKindUnitPosition: return "unit-position";
	case kKindFrameMark: return "frame-mark";
	case kKindPathNode: return "path-node";
	default: return "?";
	}
}

// Upvalue 1 of every closure: its generation. true when the closure is current; counts a stale call otherwise.
bool Current(lua_State* L)
{
	const LONG gen = (LONG)lua_tointeger(L, lua_upvalueindex(1));
	if (gen == g_gen) return true;
	InterlockedIncrement((volatile LONG*)&g_stats.staleCalls);
	return false;
}

// Packs Lua value at stack index idx into a[i] and the record's flags.
void PackArg(lua_State* L, int idx, int i, Record& r)
{
	switch (lua_type(L, idx))
	{
	case LUA_TNUMBER:
	{
		const lua_Number v = lua_tonumber(L, idx);
		lua_Number whole = v < 0 ? ceil(v) : floor(v);
		if (whole != v) r.flags |= kFlagFraction;
		if (whole > 2147483647.0) whole = 2147483647.0;
		if (whole < -2147483648.0) whole = -2147483648.0;
		r.a[i] = (int32_t)whole;
		break;
	}
	case LUA_TBOOLEAN:
		r.a[i] = lua_toboolean(L, idx) ? 1 : 0;
		break;
	case LUA_TLIGHTUSERDATA:
		r.a[i] = (int32_t)(intptr_t)lua_touserdata(L, idx);
		r.flags |= (kFlagLightUserdata0 << i) | (kFlagNotNumber0 << i);
		break;
	default:
		r.a[i] = 0;
		r.flags |= (kFlagNotNumber0 << i);
		break;
	}
}

void Emit(Record& r)
{
	r.version = kRecordVersion;
	r.seq = (uint32_t)InterlockedIncrement(&g_seq);
	r.gen = (uint32_t)g_gen;
	if (r.kind < kKindCount) InterlockedIncrement((volatile LONG*)&g_stats.records[r.kind]);
	if (g_hooks.post)
	{
		g_hooks.post(r);
		InterlockedIncrement((volatile LONG*)&g_stats.posted);
	}
}

// ============================================================================================ closures Lua calls
// A listener: upvalue 1 = generation, upvalue 2 = kind.
int Listener(lua_State* L)
{
	if (!Current(L)) return 0;
	if (!OnMainThread())
	{
		InterlockedIncrement((volatile LONG*)&g_stats.wrongThread);
		return 0;
	}
	Record r;
	memset(&r, 0, sizeof(r));
	r.kind = (uint16_t)lua_tointeger(L, lua_upvalueindex(2));
	const int n = lua_gettop(L);
	for (int i = 0; i < n && i < 4; ++i) PackArg(L, i + 1, i, r);
	if (n > 4) r.flags |= kFlagMoreArgs;
	Emit(r);
	return 0;
}

// ---- issue 4754: the position listeners (upvalue 1 = generation) and the frame mark ---------------------------------
// Posts the frame's held kind-6 records after one mark (a[0] = frame, a[1] = their count). Main thread.
void FlushPositions(uint32_t frame, bool early)
{
	if (g_posCount == 0) return;
	Record m;
	memset(&m, 0, sizeof(m));
	m.kind = kKindFrameMark;
	m.a[0] = (int32_t)frame;
	m.a[1] = g_posCount;
	Emit(m);
	++g_pstats.marks;
	if (early) ++g_pstats.earlyFlushes;
	const int n = g_posCount;
	g_posCount = 0;
	for (int i = 0; i < n; ++i) Emit(g_posBuf[i]);
}

void HoldPosition(const Record& r)
{
	if (g_posCount >= kPosBufCap) FlushPositions(g_stats.drains + 1, true);   // the frame the next drain ends
	g_posBuf[g_posCount++] = r;
}

// A whole number in [lo, hi] at stack index idx.
bool WholeArg(lua_State* L, int idx, double lo, double hi, int32_t* out)
{
	if (lua_type(L, idx) != LUA_TNUMBER) return false;
	const lua_Number v = lua_tonumber(L, idx);
	if (!(v >= lo && v <= hi) || floor(v) != v) return false;
	*out = (int32_t)v;
	return true;
}

// table.<k> (stack index t) as the bits of a float; false when it is not a number.
bool FieldBits(lua_State* L, int t, const char* k, int32_t* out)
{
	lua_getfield(L, t, k);
	const bool ok = lua_type(L, -1) == LUA_TNUMBER;
	if (ok)
	{
		const float f = (float)lua_tonumber(L, -1);
		memcpy(out, &f, sizeof(f));
	}
	lua_pop(L, 1);
	return ok;
}

// Premise F6: the first kArgLogLines calls of each listener log what they were given (types and values).
void LogArgs(lua_State* L, const char* event, uint32_t nth)
{
	char line[480];
	line[0] = 0;
	size_t used = 0;
	const int n = lua_gettop(L);
	for (int i = 1; i <= n && i <= 6; ++i)
	{
		char one[120];
		const int t = lua_type(L, i);
		if (t == LUA_TNUMBER) _snprintf_s(one, sizeof(one), _TRUNCATE, "number %.9g", (double)lua_tonumber(L, i));
		else if (t == LUA_TBOOLEAN) _snprintf_s(one, sizeof(one), _TRUNCATE, "boolean %s", lua_toboolean(L, i) ? "true" : "false");
		else if (t == LUA_TTABLE)
		{
			double v[4];
			bool have[4];
			const char* const keys[4] = { "x", "y", "z", "w" };
			for (int k = 0; k < 4; ++k)
			{
				lua_getfield(L, i, keys[k]);
				have[k] = lua_type(L, -1) == LUA_TNUMBER;
				v[k] = have[k] ? (double)lua_tonumber(L, -1) : 0.0;
				lua_pop(L, 1);
			}
			_snprintf_s(one, sizeof(one), _TRUNCATE, "table {x %s%.7g, y %s%.7g, z %s%.7g, w %s%.7g}", have[0] ? "" : "(none) ",
			            v[0], have[1] ? "" : "(none) ", v[1], have[2] ? "" : "(none) ", v[2], have[3] ? "" : "(none) ", v[3]);
		}
		else _snprintf_s(one, sizeof(one), _TRUNCATE, "%s", lua_typename(L, t));
		_snprintf_s(line + used, sizeof(line) - used, _TRUNCATE, "%s%s", used ? ", " : "", one);
		used = strlen(line);
	}
	Logf("ui bridge: %s call %u: %d argument(s)%s%s", event, nth, n, n ? ": " : "", line);
}

uint32_t g_posCalls = 0, g_arrCalls = 0;   // main thread

// LocalMachineUnitPositionChanged(playerID, unitID, {x, y, z, w}): one kind-6 record, held for the frame's mark.
int PositionListener(lua_State* L)
{
	if (!Current(L)) return 0;
	if (!OnMainThread())
	{
		InterlockedIncrement((volatile LONG*)&g_stats.wrongThread);
		return 0;
	}
	if (++g_posCalls <= (uint32_t)kArgLogLines) LogArgs(L, kPositionEvent, g_posCalls);
	int32_t player = 0, unit = 0;
	Record r;
	memset(&r, 0, sizeof(r));
	if (!WholeArg(L, 1, 0.0, 255.0, &player) || !WholeArg(L, 2, -2147483648.0, 2147483647.0, &unit) ||
	    !lua_istable(L, 3) || !FieldBits(L, 3, "x", &r.a[1]) || !FieldBits(L, 3, "y", &r.a[2]) ||
	    !FieldBits(L, 3, "z", &r.a[3]))
	{
		++g_pstats.badArgs;
		return 0;
	}
	r.kind = kKindUnitPosition;
	r.flags = (uint32_t)player << kPosPlayerShift;
	r.a[0] = unit;
	LastPos* s = LastFind(player, unit, true);
	if (s) memcpy(s->xyz, &r.a[1], sizeof(s->xyz));
	++g_pstats.positions;
	HoldPosition(r);
	return 0;
}

// UnitMoveQueueChanged(playerID, unitID, bRemainingMoves): with bRemainingMoves false or nil (as the UI files read it:
// `if not bRemainingMoves`) one kind-6 arrival record with the unit's last position; otherwise counted only.
int ArrivalListener(lua_State* L)
{
	if (!Current(L)) return 0;
	if (!OnMainThread())
	{
		InterlockedIncrement((volatile LONG*)&g_stats.wrongThread);
		return 0;
	}
	if (++g_arrCalls <= (uint32_t)kArgLogLines) LogArgs(L, kArrivalEvent, g_arrCalls);
	int32_t player = 0, unit = 0;
	if (!WholeArg(L, 1, 0.0, 255.0, &player) || !WholeArg(L, 2, -2147483648.0, 2147483647.0, &unit))
	{
		++g_pstats.badArgs;
		return 0;
	}
	if (lua_toboolean(L, 3))
	{
		++g_pstats.queueRemaining;
		return 0;
	}
	Record r;
	memset(&r, 0, sizeof(r));
	r.kind = kKindUnitPosition;
	r.flags = ((uint32_t)player << kPosPlayerShift) | kPosArrival;
	r.a[0] = unit;
	const LastPos* s = LastFind(player, unit, false);
	if (s) memcpy(&r.a[1], s->xyz, sizeof(s->xyz));
	else ++g_pstats.arrivalsNoPosition;
	++g_pstats.arrivals;
	HoldPosition(r);
	return 0;
}

// ---- ticket 0199 part 2: the wrapper's __call (upvalue 1 = generation, upvalue 2 = the original) ----------------------
// Lua calls it as __call(wrapper, x0, y0, x1, y1). Posts kind 1 with the four plot coordinates when they are whole
// numbers and the closure is current on the main thread, then ALWAYS calls the original with the same arguments and
// returns its results (an error of the original propagates as it would have without the wrapper).
int ArrowWrapperCall(lua_State* L)
{
	if (lua_gettop(L) == 0) lua_pushnil(L);   // never from __call (it passes the wrapper); keeps slot 1 valid below
	const int nargs = lua_gettop(L) - 1;     // without the wrapper itself
	const bool current = lua_tointeger(L, lua_upvalueindex(1)) == g_gen;
	if (!current)
		++g_astats.staleCalls;
	else if (!OnMainThread())
		InterlockedIncrement((volatile LONG*)&g_stats.wrongThread);
	else
	{
		if (++g_arrowCalls <= (uint32_t)kArgLogLines) LogArgs(L, kArrowEvent, g_arrowCalls);
		Record r;
		memset(&r, 0, sizeof(r));
		bool ok = nargs == 4;
		for (int i = 0; ok && i < 4; ++i)
			ok = WholeArg(L, i + 2, -2147483648.0, 2147483647.0, &r.a[i]);
		if (!ok)
			++g_astats.badArgs;
		else
		{
			r.kind = kKindSpawnArrow;
			++g_astats.spawns;
			Emit(r);
		}
	}
	lua_pushvalue(L, lua_upvalueindex(2));
	lua_replace(L, 1);                 // the original in place of the wrapper: original(x0, y0, x1, y1)
	lua_call(L, nargs, LUA_MULTRET);
	return lua_gettop(L);
}

// The wrapper's fields: __index(wrapper, key) = original[key]; a function is handed out as a forwarder (ArrowMethod) that
// puts the original in place of the wrapper when it is the first argument, so a colon call (Events.SpawnArrowEvent:Count(),
// which the EXE checks against its own object) works as on the original, and a dot call (Add(f), as the UI files make
// it) passes its arguments unchanged.
int ArrowMethod(lua_State* L)   // upvalue 1 = the function, 2 = the wrapper, 3 = the original
{
	const int nargs = lua_gettop(L);
	if (nargs >= 1 && lua_rawequal(L, 1, lua_upvalueindex(2)))
	{
		lua_pushvalue(L, lua_upvalueindex(3));
		lua_replace(L, 1);
	}
	lua_pushvalue(L, lua_upvalueindex(1));
	lua_insert(L, 1);
	lua_call(L, nargs, LUA_MULTRET);
	return lua_gettop(L);
}

int ArrowIndex(lua_State* L)    // upvalue 1 = the original; arguments (wrapper, key)
{
	lua_settop(L, 2);
	lua_pushvalue(L, lua_upvalueindex(1));
	lua_pushvalue(L, 2);
	lua_gettable(L, -2);           // original[key] (through its own metatable)
	if (!lua_isfunction(L, -1)) return 1;
	lua_pushvalue(L, 1);           // the wrapper
	lua_pushvalue(L, lua_upvalueindex(1));
	lua_pushcclosure(L, ArrowMethod, 3);
	return 1;
}

// RemoveAllArrowsEvent in the arrows-only install (upvalue 1 = generation): one kind-2 record.
int RemoveArrowsListener(lua_State* L)
{
	if (!Current(L)) return 0;
	if (!OnMainThread())
	{
		InterlockedIncrement((volatile LONG*)&g_stats.wrongThread);
		return 0;
	}
	Record r;
	memset(&r, 0, sizeof(r));
	r.kind = kKindRemoveAllArrows;
	++g_astats.removes;
	Emit(r);
	return 0;
}

// ---- ticket 0300 part 2: the path listener (upvalue 1 = generation) -------------------------------------------------
// What the protected read (PathReadBody) fills: the nodes, their turn values and the head selected unit.
struct PathRead
{
	UiBridge::PathQuery q;
	int32_t turn[kPathMaxNodes];
	int raw;            // the path's length as given (before the cut to kPathMaxNodes)
	bool bad;           // the argument is no table, or a node has no whole-number x / y / turn
};

// A number field of the table at stack index t as an integer in [lo, hi]; false when absent or not a whole number.
bool IntField(lua_State* L, int t, const char* k, double lo, double hi, int32_t* out)
{
	lua_getfield(L, t, k);
	const bool ok = WholeArg(L, lua_gettop(L), lo, hi, out);
	lua_pop(L, 1);
	return ok;
}

// A method call obj:name() with no arguments, one number result: true and *out when it gave a whole number.
bool CallIntMethod(lua_State* L, int obj, const char* name, int32_t* out)
{
	lua_getfield(L, obj, name);
	if (!lua_isfunction(L, -1))
	{
		lua_pop(L, 1);
		return false;
	}
	lua_pushvalue(L, obj);
	lua_call(L, 1, 1);
	const bool ok = WholeArg(L, lua_gettop(L), -2147483648.0, 2147483647.0, out);
	lua_pop(L, 1);
	return ok;
}

// lua_pcall body: argument 1 = the PathRead (light userdata), argument 2 = the event's first argument (the path).
// Reads the nodes (array part 1..n), then UI.GetHeadSelectedUnit():GetOwner() / :GetID(), and for an empty path
// UI.GetMouseOverHex(). An error (a Lua UI function failing) leaves the unit and the mouse unknown; the nodes read stay.
int PathReadBody(lua_State* L)
{
	PathRead& r = *static_cast<PathRead*>(lua_touserdata(L, 1));
	if (!lua_istable(L, 2))
	{
		r.bad = true;
		return 0;
	}
	r.raw = (int)lua_objlen(L, 2);
	const int keep = r.raw < kPathMaxNodes ? r.raw : (int)kPathMaxNodes;
	for (int i = 0; i < keep; ++i)
	{
		lua_rawgeti(L, 2, i + 1);
		const int t = lua_gettop(L);
		int32_t x = 0, y = 0, turn = 0;
		if (!lua_istable(L, t) || !IntField(L, t, "x", -32768.0, 32767.0, &x) || !IntField(L, t, "y", -32768.0, 32767.0, &y) ||
		    !IntField(L, t, "turn", -2147483648.0, 2147483647.0, &turn))
		{
			r.bad = true;
			return 0;
		}
		r.q.x[i] = (int16_t)x;
		r.q.y[i] = (int16_t)y;
		r.turn[i] = turn;
		lua_settop(L, 2);
	}
	r.q.count = keep;
	// UI from this function's environment, which it took from the listener: the InGame context's (the install made the
	// listener there). LUA_GLOBALSINDEX would be the globals of whichever thread the EXE raises the event on.
	lua_getfield(L, LUA_ENVIRONINDEX, "UI");
	const int ui = lua_gettop(L);
	if (!lua_istable(L, ui) && !lua_isuserdata(L, ui)) return 0;
	lua_getfield(L, ui, "GetHeadSelectedUnit");
	if (lua_isfunction(L, -1))
	{
		lua_call(L, 0, 1);
		const int unit = lua_gettop(L);
		int32_t owner = -1, id = -1;
		if ((lua_istable(L, unit) || lua_isuserdata(L, unit)) && CallIntMethod(L, unit, "GetOwner", &owner) &&
		    CallIntMethod(L, unit, "GetID", &id))
		{
			r.q.unitOwner = owner;
			r.q.unitId = id;
		}
	}
	lua_settop(L, ui);
	if (r.q.count == 0)
	{
		lua_getfield(L, ui, "GetMouseOverHex");
		if (lua_isfunction(L, -1))
		{
			lua_call(L, 0, 2);
			int32_t mx = 0, my = 0;
			if (WholeArg(L, ui + 1, -32768.0, 32767.0, &mx) && WholeArg(L, ui + 2, -32768.0, 32767.0, &my))
			{
				r.q.mouseX = mx;
				r.q.mouseY = my;
			}
		}
	}
	return 0;
}

int PathListenerBody(lua_State* L);

// UIPathFinderUpdate(path): the path's records (kKindPathNode), the tests' answers in their bits. Timed (the cost of the
// listener inside the EXE's raise: the MC's condition, 2026-11-18).
int PathListener(lua_State* L)
{
	LARGE_INTEGER t0, t1, fq;
	QueryPerformanceCounter(&t0);
	PathListenerBody(L);
	QueryPerformanceCounter(&t1);
	QueryPerformanceFrequency(&fq);
	const uint64_t us = fq.QuadPart ? (uint64_t)((t1.QuadPart - t0.QuadPart) * 1000000 / fq.QuadPart) : 0;
	g_pathStats.costUs += us;
	if (us > g_pathStats.costMaxUs) g_pathStats.costMaxUs = (uint32_t)(us > 0xFFFFFFFFull ? 0xFFFFFFFFull : us);
	return 0;
}

int PathListenerBody(lua_State* L)
{
	if (lua_tointeger(L, lua_upvalueindex(1)) != g_gen)
	{
		++g_pathStats.staleCalls;
		return 0;
	}
	if (!OnMainThread())
	{
		InterlockedIncrement((volatile LONG*)&g_stats.wrongThread);
		return 0;
	}
	PathRead rd;
	memset(&rd, 0, sizeof(rd));
	rd.q.unitOwner = rd.q.unitId = -1;
	rd.q.mouseX = rd.q.mouseY = -1;
	const int top = lua_gettop(L);
	lua_pushcfunction(L, PathReadBody);
	lua_pushlightuserdata(L, &rd);
	if (top >= 1) lua_pushvalue(L, 1);
	else lua_pushnil(L);
	const bool readOk = lua_pcall(L, 2, 0, 0) == 0;
	if (!readOk)
	{
		if (++g_pathStats.readFailures <= (uint32_t)kArgLogLines)
			Logf("ui bridge: %s: reading the unit or the mouse failed: %s (the path is posted untested)", kPathEvent,
			     lua_isstring(L, -1) ? lua_tostring(L, -1) : "(no message)");
		rd.q.unitOwner = rd.q.unitId = -1;
	}
	lua_settop(L, top);
	if (rd.bad)
	{
		++g_pathStats.badArgs;
		if (g_pathStats.badArgs <= (uint32_t)kArgLogLines) LogArgs(L, kPathEvent, g_pathStats.badArgs);
		return 0;
	}
	UiBridge::PathAnswer an;
	memset(&an, 0, sizeof(an));
	UiBridge::PathTestsFn tests = g_pathTests;
	if (rd.q.unitOwner < 0) ++g_pathStats.noUnit;
	else if (tests) tests(rd.q, an);
	if (an.tested) ++g_pathStats.tested;
	const int n = rd.q.count;
	if (++g_pathCalls <= (uint32_t)kArgLogLines)
		Logf("ui bridge: %s call %u: %d node(s)%s, first (%d, %d, turn %d), unit %d/%d, mouse (%d, %d), tested %d", kPathEvent,
		     g_pathCalls, rd.raw, rd.raw > n ? " (cut)" : "", n ? rd.q.x[0] : 0, n ? rd.q.y[0] : 0, n ? rd.turn[0] : 0,
		     rd.q.unitOwner, rd.q.unitId, rd.q.mouseX, rd.q.mouseY, an.tested ? 1 : 0);
	++g_pathUpdate;
	++g_pathStats.updates;
	if (rd.raw > n) ++g_pathStats.truncated;
	const uint32_t common = ((uint32_t)n << kPathCountShift) | (an.tested ? (uint32_t)kPathTested : 0u) |
	                        (rd.raw > n ? (uint32_t)kPathTruncated : 0u);
	Record r;
	if (n == 0)
	{
		memset(&r, 0, sizeof(r));
		r.kind = kKindPathNode;
		r.a[0] = (int32_t)(((uint32_t)(uint16_t)(int16_t)rd.q.mouseX) | ((uint32_t)(uint16_t)(int16_t)rd.q.mouseY << 16));
		r.a[2] = (int32_t)(common | (an.tested && an.cursorAttack ? (uint32_t)kPathAttack : 0u) |
		                   (an.tested && an.cursorSwap ? (uint32_t)kPathSwap : 0u));
		r.a[3] = (int32_t)g_pathUpdate;
		if (an.tested && an.cursorAttack) ++g_pathStats.attacks;
		if (an.tested && an.cursorSwap) ++g_pathStats.swaps;
		++g_pathStats.empty;
		Emit(r);
		return 0;
	}
	for (int i = 0; i < n; ++i)
	{
		memset(&r, 0, sizeof(r));
		r.kind = kKindPathNode;
		r.a[0] = (int32_t)(((uint32_t)(uint16_t)rd.q.x[i]) | ((uint32_t)(uint16_t)rd.q.y[i] << 16));
		r.a[1] = rd.turn[i];
		uint32_t bits = common | (uint32_t)i;
		if (an.tested && i >= 1 && an.embark[i])
		{
			bits |= kPathEmbark;
			++g_pathStats.embarks;
		}
		if (an.tested && i == n - 1 && n >= 2 && an.lastAttack)
		{
			bits |= kPathAttack;
			++g_pathStats.attacks;
		}
		r.a[2] = (int32_t)bits;
		r.a[3] = (int32_t)g_pathUpdate;
		++g_pathStats.nodes;
		Emit(r);
	}
	return 0;
}

// Pops one queued raise into out; false when the queue is empty.
bool PopRaise(Raise& out)
{
	EnterCriticalSection(&g_cs);
	const bool have = g_queueCount > 0;
	if (have)
	{
		out = g_queue[g_queueHead];
		g_queueHead = (g_queueHead + 1) % kQueueCap;
		--g_queueCount;
	}
	LeaveCriticalSection(&g_cs);
	return have;
}

// One raise, called with lua_pcall: argument 1 = the Events table of the installed context (the drain's upvalue 2),
// argument 2 = the RaiseRun (light userdata). Events[name](args...). A missing event is reported in the result; an
// error of a listener is caught by the inner lua_pcall, an error of the lookup itself by the outer lua_pcall.
struct RaiseRun
{
	const Raise* q;
	int result;          // 0 ok, 1 no Events table, 2 no such event, 3 an error was raised
	char err[200];
};

int RaiseBody(lua_State* L)
{
	RaiseRun& run = *static_cast<RaiseRun*>(lua_touserdata(L, 2));
	const Raise& q = *run.q;
	lua_settop(L, 1);
	if (!lua_istable(L, 1) && !lua_isuserdata(L, 1)) { run.result = 1; return 0; }
	lua_getfield(L, 1, q.name);
	if (lua_isnil(L, -1)) { run.result = 2; return 0; }
	for (int i = 0; i < q.nargs; ++i)
	{
		if (q.args[i].type == Arg::kBoolean) lua_pushboolean(L, q.args[i].value != 0);
		else lua_pushnumber(L, (lua_Number)q.args[i].value);
	}
	if (lua_pcall(L, q.nargs, 0, 0) != 0)
	{
		run.result = 3;
		_snprintf_s(run.err, sizeof(run.err), _TRUNCATE, "%s", lua_isstring(L, -1) ? lua_tostring(L, -1) : "(no message)");
	}
	return 0;
}

// Runs one raise on L through the Events table at stack index eventsIdx; the stack is left as found.
void RunRaise(lua_State* L, int eventsIdx, const Raise& q)
{
	const int top = lua_gettop(L);
	RaiseRun run;
	memset(&run, 0, sizeof(run));
	run.q = &q;
	lua_pushcfunction(L, RaiseBody);
	lua_pushvalue(L, eventsIdx);
	lua_pushlightuserdata(L, &run);
	if (lua_pcall(L, 2, 0, 0) != 0)
	{
		run.result = 3;
		_snprintf_s(run.err, sizeof(run.err), _TRUNCATE, "%s", lua_isstring(L, -1) ? lua_tostring(L, -1) : "(no message)");
	}
	lua_settop(L, top);
	if (run.result == 1) Logf("ui bridge: raise Events.%s: no Events table", q.name);
	else if (run.result == 2) Logf("ui bridge: raise Events.%s: no such event", q.name);
	else if (run.result == 3) Logf("ui bridge: raise Events.%s failed: %s", q.name, run.err);
	InterlockedIncrement((volatile LONG*)(run.result == 0 ? &g_stats.raisesDone : &g_stats.raisesFailed));
}

// The per-frame drain: a listener of Events.LocalMachineAppUpdate (main thread). Upvalue 1 = generation, upvalue 2 =
// the Events table of the context it was installed into (a Lua reference, so the raise does not depend on which thread
// or environment the EXE calls the listener with).
int Drain(lua_State* L)
{
	if (!Current(L)) return 0;
	if (!OnMainThread())
	{
		InterlockedIncrement((volatile LONG*)&g_stats.wrongThread);
		return 0;
	}
	InterlockedIncrement(&g_drainTick);
	InterlockedIncrement((volatile LONG*)&g_stats.drains);
	FlushPositions(g_stats.drains, false);   // issue 4754: this frame's positions, after their mark
	// At most the queue's capacity per frame, so a raise that queues another raise cannot loop forever.
	Raise q;
	for (int k = 0; k < kQueueCap && PopRaise(q); ++k)
		RunRaise(L, lua_upvalueindex(2), q);
	void (*hook)() = g_frameHook;
	if (hook)
	{
		InterlockedIncrement((volatile LONG*)&g_stats.frameHookCalls);
		hook();   // plain C, no Lua call (ticket 0487: proxy_uidraw.cpp)
	}
	void (*luaHook)(lua_State*) = g_frameLuaHook;
	if (luaHook)
	{
		// ticket 0586 (proxy_livereload.cpp): called with this listener's state, whose environment is the InGame context's;
		// the hook makes its own Lua calls in protected mode (lua_cpcall), so no error longjmps through this frame
		InterlockedIncrement((volatile LONG*)&g_stats.frameHookCalls);
		const int top = lua_gettop(L);
		luaHook(L);
		lua_settop(L, top);
	}
	return 0;
}

// The replaced global: upvalue 1 = generation, upvalue 2 = the original function. Always calls the original (also for
// an older generation or another thread: replacing must never change what the UI sees).
int UnitMovingReplacement(lua_State* L)
{
	InterlockedIncrement((volatile LONG*)&g_stats.unitMovingCalls);
	const int nargs = lua_gettop(L);
	const bool current = lua_tointeger(L, lua_upvalueindex(1)) == g_gen;
	int a0 = 0, a1 = 0;
	if (nargs >= 1) a0 = (int)lua_tointeger(L, 1);
	if (nargs >= 2) a1 = (int)lua_tointeger(L, 2);
	lua_pushvalue(L, lua_upvalueindex(2));
	lua_insert(L, 1);
	lua_call(L, nargs, LUA_MULTRET);  // an error of the original propagates as it would have
	const int nret = lua_gettop(L);
	if (current && g_test && OnMainThread())
	{
		Record r;
		memset(&r, 0, sizeof(r));
		r.kind = kKindUnitMovingCalled;
		r.a[0] = a0;
		r.a[1] = a1;
		r.a[2] = (nret >= 1 && lua_toboolean(L, 1)) ? 1 : 0;
		Emit(r);
	}
	return nret;
}

// ---- the test table (ui_bridge_test=1): VP64UiBridgeTest.QueueRaise(name, ...), VP64UiBridgeTest.Stats() ----------
int TestQueueRaise(lua_State* L)
{
	const char* name = luaL_checkstring(L, 1);
	const int n = lua_gettop(L) - 1;
	Arg args[kArgCap];
	if (n > kArgCap) return luaL_error(L, "VP64UiBridgeTest.QueueRaise: at most %d arguments", (int)kArgCap);
	for (int i = 0; i < n; ++i)
	{
		const int idx = i + 2;
		if (lua_type(L, idx) == LUA_TBOOLEAN) { args[i].type = Arg::kBoolean; args[i].value = lua_toboolean(L, idx) ? 1 : 0; }
		else { args[i].type = Arg::kNumber; args[i].value = (double)luaL_checknumber(L, idx); }
	}
	lua_pushboolean(L, QueueRaise(name, n, args) ? 1 : 0);
	return 1;
}

void SetIntField(lua_State* L, const char* k, uint32_t v)
{
	lua_pushnumber(L, (lua_Number)v);
	lua_setfield(L, -2, k);
}

int TestStats(lua_State* L)
{
	const Stats s = GetStats();
	lua_newtable(L);
	SetIntField(L, "gen", s.gen);
	SetIntField(L, "installs", s.installs);
	SetIntField(L, "installFailures", s.installFailures);
	SetIntField(L, "listenersAdded", s.listenersAdded);
	for (int k = 1; k < kKindCount; ++k) SetIntField(L, KindName(k), s.records[k]);
	SetIntField(L, "posted", s.posted);
	SetIntField(L, "drains", s.drains);
	SetIntField(L, "raisesQueued", s.raisesQueued);
	SetIntField(L, "raisesDone", s.raisesDone);
	SetIntField(L, "raisesFailed", s.raisesFailed);
	SetIntField(L, "raisesDropped", s.raisesDropped);
	SetIntField(L, "unitMovingCalls", s.unitMovingCalls);
	SetIntField(L, "unitMovingReplaced", s.unitMovingReplaced);
	SetIntField(L, "staleCalls", s.staleCalls);
	SetIntField(L, "wrongThread", s.wrongThread);
	SetIntField(L, "stalls", s.stalls);
	SetIntField(L, "seq", (uint32_t)g_seq);
	return 1;
}

// Sets the global VP64UiBridgeTest = { QueueRaise, Stats } in L's globals.
void SetTestTable(lua_State* L)
{
	lua_newtable(L);
	lua_pushcfunction(L, TestQueueRaise);
	lua_setfield(L, -2, "QueueRaise");
	lua_pushcfunction(L, TestStats);
	lua_setfield(L, -2, "Stats");
	lua_setglobal(L, kTestTable);
}

int TestTableBody(lua_State* L)
{
	lua_settop(L, 0);
	SetTestTable(L);
	return 0;
}

// ============================================================================================ installation
struct InstallCtx
{
	LONG gen;
	int listeners;    // Add calls that succeeded
	int missing;      // events not present in the state
	bool haveEvents;
	bool drain;
	bool replaced;
	bool replaceFound;
	bool full;        // the whole bridge (ui_bridge=1): the generic listeners, UnitMoving, the test table
	bool positions;   // issue 4754: the position listeners
	int posListeners; // ... of them added
	bool arrows;      // ticket 0199: the SpawnArrowEvent wrapper and the remove listener
	bool arrowWrapped;
	bool arrowUnwrapped;
	bool arrowRemove; // the remove listener added (or the whole bridge's generic one)
	bool path;        // ticket 0300 part 2: the UIPathFinderUpdate listener and a DisplayMovementIndicator listener
	bool pathListener;
	bool pathShow;    // the show listener added (or the whole bridge's generic one)
	char missingNames[256];
};

// Ticket 0199: Events.SpawnArrowEvent (Events at stack index `events`) replaced by the wrapper. true when it was put in.
bool WrapArrowEvent(lua_State* L, int events, InstallCtx& c)
{
	const int top = lua_gettop(L);
	lua_getfield(L, events, kArrowEvent);
	if (lua_isnil(L, -1))
	{
		++c.missing;
		const size_t used = strlen(c.missingNames);
		_snprintf_s(c.missingNames + used, sizeof(c.missingNames) - used, _TRUNCATE, "%s%s", used ? ", " : "", kArrowEvent);
		lua_settop(L, top);
		return false;
	}
	const int orig = lua_gettop(L);
	if (lua_istable(L, orig))
	{
		lua_pushlightuserdata(L, (void*)&kArrowWrapKey);
		lua_rawget(L, orig);
		const bool ours = lua_toboolean(L, -1) != 0;
		lua_pop(L, 1);
		if (ours)   // an older generation's wrapper: wrap its original instead, so wrappers never nest
		{
			lua_pushlightuserdata(L, (void*)&kArrowOrigKey);
			lua_rawget(L, orig);
			lua_replace(L, orig);
			c.arrowUnwrapped = true;
		}
	}
	lua_newtable(L);                       // the wrapper W
	const int w = lua_gettop(L);
	lua_pushlightuserdata(L, (void*)&kArrowWrapKey);
	lua_pushboolean(L, 1);
	lua_rawset(L, w);
	lua_pushlightuserdata(L, (void*)&kArrowOrigKey);
	lua_pushvalue(L, orig);
	lua_rawset(L, w);
	lua_newtable(L);                       // its metatable
	lua_pushinteger(L, c.gen);
	lua_pushvalue(L, orig);
	lua_pushcclosure(L, ArrowWrapperCall, 2);
	lua_setfield(L, -2, "__call");
	lua_pushvalue(L, orig);
	lua_pushcclosure(L, ArrowIndex, 1);
	lua_setfield(L, -2, "__index");        // Add, Remove, Count, ... of the original (ArrowIndex: colon calls too)
	lua_setmetatable(L, w);
	lua_pushvalue(L, w);
	lua_setfield(L, events, kArrowEvent);  // a plain assignment, as P9's Lua probe did
	lua_settop(L, top);
	return true;
}

// Events.<name>.Add(closure on top of the stack). Pops the closure. true when Add ran without an error.
bool AddListener(lua_State* L, int events, const char* name, InstallCtx& c)
{
	const int top = lua_gettop(L);         // the closure is at top
	lua_getfield(L, events, name);         // Events.X (a table or userdata; nil when absent)
	if (lua_isnil(L, -1))
	{
		++c.missing;
		const size_t used = strlen(c.missingNames);
		_snprintf_s(c.missingNames + used, sizeof(c.missingNames) - used, _TRUNCATE, "%s%s", used ? ", " : "", name);
		lua_settop(L, top - 1);
		return false;
	}
	lua_getfield(L, -1, "Add");            // Events.X.Add (called with a dot: Add(f), as the UI files do)
	lua_pushvalue(L, top);
	const bool ok = lua_pcall(L, 1, 0, 0) == 0;
	if (!ok)
		Logf("ui bridge: Events.%s.Add failed: %s", name, lua_isstring(L, -1) ? lua_tostring(L, -1) : "(no message)");
	lua_settop(L, top - 1);
	return ok;
}

// The protected body of an installation (lua_cpcall): the light userdata argument is the InstallCtx.
int InstallBody(lua_State* L)
{
	InstallCtx& c = *static_cast<InstallCtx*>(lua_touserdata(L, 1));
	lua_settop(L, 0);
	lua_getglobal(L, "Events");
	c.haveEvents = lua_istable(L, 1) || lua_isuserdata(L, 1);
	if (c.haveEvents)
	{
		for (int i = 0; c.full && i < kListenCount; ++i)
		{
			if (c.arrows && kListen[i].kind == kKindSpawnArrow) continue;   // ticket 0199: the wrapper posts kind 1
			lua_pushinteger(L, c.gen);
			lua_pushinteger(L, kListen[i].kind);
			lua_pushcclosure(L, Listener, 2);
			const bool added = AddListener(L, 1, kListen[i].event, c);
			if (added) ++c.listeners;
			if (added && kListen[i].kind == kKindRemoveAllArrows) c.arrowRemove = true;
			if (added && kListen[i].kind == kKindDisplayMovement) c.pathShow = true;   // ticket 0300
		}
		if (c.path)   // ticket 0300 part 2
		{
			lua_pushinteger(L, c.gen);
			lua_pushcclosure(L, PathListener, 1);
			c.pathListener = AddListener(L, 1, kPathEvent, c);
			if (!c.full)
			{
				lua_pushinteger(L, c.gen);
				lua_pushinteger(L, kKindDisplayMovement);
				lua_pushcclosure(L, Listener, 2);
				c.pathShow = AddListener(L, 1, kShowEvent, c);
			}
		}
		if (c.arrows)   // ticket 0199
		{
			c.arrowWrapped = WrapArrowEvent(L, 1, c);
			if (!c.full)
			{
				lua_pushinteger(L, c.gen);
				lua_pushcclosure(L, RemoveArrowsListener, 1);
				c.arrowRemove = AddListener(L, 1, kRemoveArrowsEvent, c);
			}
		}
		if (c.positions)   // issue 4754
		{
			lua_pushinteger(L, c.gen);
			lua_pushcclosure(L, PositionListener, 1);
			if (AddListener(L, 1, kPositionEvent, c)) ++c.posListeners;
			lua_pushinteger(L, c.gen);
			lua_pushcclosure(L, ArrivalListener, 1);
			if (AddListener(L, 1, kArrivalEvent, c)) ++c.posListeners;
		}
		lua_pushinteger(L, c.gen);
		lua_pushvalue(L, 1);                 // the Events table, upvalue 2
		lua_pushcclosure(L, Drain, 2);
		c.drain = AddListener(L, 1, kDrainEvent, c);
	}
	lua_settop(L, 0);

	// The replaced global, in this context's environment (each context has its own UnitMoving, a C function of the EXE;
	// a file that copied it into a local at load, as EUI's UnitFlagManager does, keeps the original). Not in the drain-
	// only mode (ticket 0487).
	lua_getglobal(L, kReplacedGlobal);
	c.replaceFound = lua_isfunction(L, 1) != 0;
	if (c.replaceFound && c.full)   // positions only (issue 4754): UnitMoving stays the engine's
	{
		// Unwrap an earlier generation's replacement (its upvalue 2 is the engine's original), so wrappers never nest.
		if (lua_tocfunction(L, 1) == UnitMovingReplacement)
		{
			lua_getupvalue(L, 1, 2);
			lua_replace(L, 1);
		}
		lua_pushinteger(L, c.gen);
		lua_pushvalue(L, 1);
		lua_pushcclosure(L, UnitMovingReplacement, 2);
		lua_setglobal(L, kReplacedGlobal);
		c.replaced = true;
	}
	lua_settop(L, 0);
	if (c.full && g_test) SetTestTable(L);
	// The marker: this environment holds generation c.gen (a light-userdata key no UI file can name).
	lua_pushlightuserdata(L, (void*)&kMarkerKey);
	lua_pushinteger(L, c.gen);
	lua_rawset(L, LUA_GLOBALSINDEX);
	return 0;
}

// ---- finding the in-game context ---------------------------------------------------------------------------------
// _LOADED._G.Threads maps each context thread to its environment (research/06-tooling-and-workflows/
// run-and-drive-the-game.md section 3; the Lua channel's runner reads it the same way). When the table is a proxy, its
// contents sit behind the metatable's __index.
struct FindCtx
{
	lua_State* thread;      // the InGame context's thread (valid only during this call)
	const void* env;        // its environment table (identity only, for the log)
	LONG marker;            // the generation installed into that environment (0 = none)
	int contexts;           // entries seen
};

// Pushes the environment's StateName (rawget) and compares it.
bool IsInGame(lua_State* L, int envIdx)
{
	if (!lua_istable(L, envIdx)) return false;
	lua_pushstring(L, "StateName");
	lua_rawget(L, envIdx);
	const bool yes = lua_type(L, -1) == LUA_TSTRING && strcmp(lua_tostring(L, -1), kInGameState) == 0;
	lua_pop(L, 1);
	return yes;
}

void ScanThreads(lua_State* L, int tableIdx, FindCtx& f)
{
	lua_pushnil(L);
	while (lua_next(L, tableIdx) != 0)
	{
		++f.contexts;
		if (!f.thread && lua_type(L, -2) == LUA_TTHREAD && IsInGame(L, lua_gettop(L)))
		{
			f.thread = lua_tothread(L, -2);
			f.env = lua_topointer(L, -1);
			lua_pushlightuserdata(L, (void*)&kMarkerKey);
			lua_rawget(L, -2);
			f.marker = (LONG)lua_tointeger(L, -1);
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
	}
}

int FindBody(lua_State* L)
{
	FindCtx& f = *static_cast<FindCtx*>(lua_touserdata(L, 1));
	lua_settop(L, 0);
	lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
	if (!lua_istable(L, 1)) return 0;
	lua_getfield(L, 1, "_G");
	if (!lua_istable(L, 2)) return 0;
	lua_pushstring(L, "Threads");
	lua_rawget(L, 2);
	if (!lua_istable(L, 3)) return 0;
	ScanThreads(L, 3, f);
	if (!f.thread && f.contexts == 0 && lua_getmetatable(L, 3))
	{
		lua_pushstring(L, "__index");
		lua_rawget(L, -2);
		if (lua_istable(L, -1)) ScanThreads(L, lua_gettop(L), f);
	}
	return 0;
}
}  // namespace

// ============================================================================================ public
namespace UiBridge
{
void SetHooks(const Hooks& h) { g_hooks = h; }

void Configure(bool enabled, bool test)
{
	InterlockedExchange(&g_enabled, enabled ? 1 : 0);
	InterlockedExchange(&g_test, test ? 1 : 0);
	Logf("ui bridge: %s%s (ui_bridge=%d ui_bridge_test=%d)", enabled ? "on" : "off",
	     enabled && test ? ", TEST table VP64UiBridgeTest and UnitMoving records" : "", enabled ? 1 : 0, test ? 1 : 0);
}

bool Enabled() { return g_enabled != 0; }

void ConfigurePositions(bool on)
{
	InterlockedExchange(&g_positions, on ? 1 : 0);
	Logf("ui bridge: unit positions %s (kinds 6 and 7: %s and %s, frame marks from the drain)%s", on ? "on" : "off",
	     kPositionEvent, kArrivalEvent,
	     !on ? "" : (g_enabled ? ", with the whole bridge (ui_bridge=1)"
	                           : ", positions only: no test table, no UnitMoving replacement, no other listener, no raises"));
}

bool PositionsOn() { return g_positions != 0; }

PositionStats GetPositionStats()
{
	PositionStats s = g_pstats;
	s.unitsKnown = g_lastUsed;
	return s;
}

void ConfigureArrows(bool on)
{
	InterlockedExchange(&g_arrows, on ? 1 : 0);
	Logf("ui bridge: arrows %s (ticket 0199: a wrapper of the InGame context's Events.%s posts kind 1 with the four plot "
	     "coordinates; Events.%s posts kind 2)%s", on ? "on" : "off", kArrowEvent, kRemoveArrowsEvent,
	     !on ? "" : (g_enabled ? ", with the whole bridge (ui_bridge=1; its generic SpawnArrowEvent listener is left out)"
	                           : ", arrows only: no test table, no UnitMoving replacement, no other listener, no raises"));
}

bool ArrowsOn() { return g_arrows != 0; }

ArrowStats GetArrowStats() { return g_astats; }

void ConfigurePath(bool on)
{
	InterlockedExchange(&g_path, on ? 1 : 0);
	Logf("ui bridge: path %s (ticket 0300: a listener of the InGame context's Events.%s posts kind 8 per node, with the "
	     "EXE's tests from the tests hook; Events.%s posts kind 4)%s", on ? "on" : "off", kPathEvent, kShowEvent,
	     !on ? "" : (g_enabled ? ", with the whole bridge (ui_bridge=1; its generic DisplayMovementIndicator listener serves)"
	                           : ", path only: no test table, no UnitMoving replacement, no other listener, no raises"));
}

bool PathOn() { return g_path != 0; }

void SetPathTests(PathTestsFn fn) { g_pathTests = fn; }

PathStats GetPathStats() { return g_pathStats; }

void SetFrameHook(void (*onFrame)())
{
	g_frameHook = onFrame;
	Logf("ui bridge: frame hook %s%s", onFrame ? "set" : "cleared",
	     onFrame && !g_enabled ? " (the bridge is off: it installs the drain alone)" : "");
}

void SetFrameLuaHook(void (*onFrame)(lua_State* L))
{
	g_frameLuaHook = onFrame;
	Logf("ui bridge: frame Lua hook %s%s", onFrame ? "set" : "cleared",
	     onFrame && !g_enabled ? " (the bridge is off: it installs the drain alone)" : "");
}

bool Active() { return g_enabled != 0 || HaveFrameHook(); }

void SetMainThread(uint32_t threadId) { InterlockedExchange(&g_mainThread, (LONG)threadId); }

bool OnStateRegistered(lua_State* L)
{
	if (!g_enabled || !g_test || !L || !OnMainThread()) return false;
	const int top = lua_gettop(L);
	const bool ok = lua_cpcall(L, TestTableBody, NULL) == 0;
	lua_settop(L, top);
	Logf("ui bridge: test table %s in the registration thread %p (copied into every UI context made later)",
	     ok ? "set" : "NOT set", (void*)L);
	return ok;
}

bool OnLuaEntry(lua_State* L)
{
	if ((!g_enabled && !g_positions && !g_arrows && !g_path && !HaveFrameHook()) || !L) return false;
	if (!OnMainThread()) return false;   // the Lua channel's chunks call DLL functions on the core thread: not ours
	const uint32_t t = NowMs();
	if (g_checked && t - g_lastCheck < (uint32_t)kCheckMs) return false;
	g_checked = true;
	g_lastCheck = t;
	const LONG tick = g_drainTick;
	if (tick != g_tickAtCheck)
	{
		g_tickAtCheck = tick;
		g_tickChangedAt = t;
	}

	FindCtx f;
	memset(&f, 0, sizeof(f));
	const int top = lua_gettop(L);
	const int frc = lua_cpcall(L, FindBody, &f);
	lua_settop(L, top);
	if (frc != 0 || !f.thread) return false;   // no in-game context (front end, load screen): nothing to do
	// Install when this environment does not hold the current generation (a new InGame context: a new game, a load, a
	// context made again), or when the current generation's drain ran and then stopped for kSilentMs while Lua calls go
	// on (its listeners were dropped). A drain that never ran (the load screen: the EXE raises no LocalMachineAppUpdate
	// for the in-game contexts until the game is shown, 59-82 s on the huge save, c5h-b 2026-11-14) is not "silent".
	const bool otherEnv = f.marker == 0 || f.marker != g_gen;
	const bool silent = !otherEnv && g_stats.drains > 0 && t - g_tickChangedAt >= (uint32_t)kSilentMs;
	// Issue 4754: the positions were asked for after this environment's install.
	const bool addPositions = !otherEnv && !silent && g_positions && !g_installedPositions;
	// Ticket 0199: the arrows were asked for after this environment's install (a failed wrap is not retried here: the
	// event is missing from this environment, logged once at the install).
	const bool addArrows = !otherEnv && !silent && g_arrows && !g_installedArrows && !g_arrowsTried;
	// Ticket 0300 part 2: the path was asked for after this environment's install (a failed install is not retried here).
	const bool addPath = !otherEnv && !silent && g_path && !g_installedPath && !g_pathTried;
	if (!otherEnv && !silent && !addPositions && !addArrows && !addPath) return false;

	// Issue 4754: the held positions go out under the generation that made them, before the new one starts; a new
	// environment (a new game or load) forgets the units' last positions.
	FlushPositions(g_stats.drains + 1, false);
	if (otherEnv) LastClear();
	InstallCtx c;
	memset(&c, 0, sizeof(c));
	c.full = g_enabled != 0;
	c.positions = g_positions != 0;
	c.arrows = g_arrows != 0;   // ticket 0199
	c.path = g_path != 0;       // ticket 0300
	c.gen = InterlockedIncrement(&g_gen);   // closures of every older generation are stale from here on
	g_pathUpdate = 0;           // ticket 0300: the update numbers start again with the generation
	g_stats.gen = (uint32_t)c.gen;
	InterlockedExchange((volatile LONG*)&g_stats.drains, 0);
	// The Add calls run on the InGame context's own thread, as that context's own files make them.
	lua_State* T = f.thread;
	const int ttop = lua_gettop(T);
	const int rc = lua_cpcall(T, InstallBody, &c);
	if (rc != 0)
		Logf("ui bridge: gen %ld: install raised an error: %s", c.gen, lua_isstring(T, -1) ? lua_tostring(T, -1) : "(no message)");
	lua_settop(T, ttop);
	g_installedEnv = f.env;
	g_tickChangedAt = t;
	g_tickAtCheck = g_drainTick;
	g_stats.listenersAdded += (uint32_t)c.listeners + (uint32_t)c.posListeners + (c.drain ? 1u : 0u);
	if (c.replaced) ++g_stats.unitMovingReplaced;
	g_installedPositions = c.positions && c.posListeners == kPosListenCount;
	g_installedArrows = c.arrows && c.arrowWrapped && c.arrowRemove;   // ticket 0199
	g_arrowsTried = c.arrows;
	if (c.arrowWrapped) ++g_astats.wraps;
	if (c.arrowUnwrapped) ++g_astats.unwraps;
	g_installedPath = c.path && c.pathListener && c.pathShow;   // ticket 0300
	g_pathTried = c.path;
	if (c.path && !c.full)
		g_stats.listenersAdded += (c.pathListener ? 1u : 0u) + (c.pathShow ? 1u : 0u);
	else if (c.path)
		g_stats.listenersAdded += c.pathListener ? 1u : 0u;
	const int wantListeners = c.arrows ? kListenCount - 1 : kListenCount;   // ticket 0199: no generic SpawnArrowEvent one
	const bool ok = rc == 0 && c.haveEvents && c.drain && (!c.full || (c.listeners == wantListeners && c.replaced)) &&
	                (!c.positions || c.posListeners == kPosListenCount) && (!c.arrows || g_installedArrows) &&
	                (!c.path || g_installedPath);
	if (ok) ++g_stats.installs;
	else ++g_stats.installFailures;
	if (c.path)   // ticket 0300: its own line
		Logf("ui bridge: gen %ld: path: Events.%s listener %s, %s listener %s%s, tests %s", c.gen, kPathEvent,
		     c.pathListener ? "added" : "NOT added", kShowEvent, c.pathShow ? "added" : "NOT added",
		     c.full ? " (the whole bridge's)" : " (path only)", g_pathTests ? "set (the DLL's answers)" : "NOT set (untested records)");
	if (c.arrows)   // ticket 0199: its own line, so the ui_bridge=1 and positions lines stay as they were
		Logf("ui bridge: gen %ld: arrows: Events.%s %s%s, %s listener %s%s", c.gen, kArrowEvent,
		     c.arrowWrapped ? "wrapped" : "NOT wrapped", c.arrowUnwrapped ? " (an older generation's wrapper taken off)" : "",
		     kRemoveArrowsEvent, c.arrowRemove ? "added" : "NOT added", c.full ? " (the whole bridge's)" : " (arrows only)");
	char posPart[96] = "";
	if (!c.full && !c.positions && !c.arrows && !c.path)   // ticket 0487: drain only, for the frame hook
		Logf("ui bridge: gen %ld installed (drain only, for the frame hook) into the %s context (thread %p, environment %p, "
		     "%d contexts; %s) from a Lua call on thread %lu: Events %s, drain (%s) %s", c.gen, kInGameState, (void*)T,
		     f.env, f.contexts, otherEnv ? "a new environment" : "the drain was silent", GetCurrentThreadId(),
		     c.haveEvents ? "found" : "MISSING", kDrainEvent, c.drain ? "added" : "NOT added");
	else
	{
		if (c.positions)
			_snprintf_s(posPart, sizeof(posPart), _TRUNCATE, ", unit positions %d of %d%s", c.posListeners, (int)kPosListenCount,
			            c.full ? "" : " (positions only)");
		Logf("ui bridge: gen %ld installed into the %s context (thread %p, environment %p, %d contexts; %s) from a Lua call on "
		     "thread %lu: Events %s, listeners %d of %d%s%s%s%s, drain (%s) %s, global %s %s", c.gen, kInGameState, (void*)T,
		     f.env, f.contexts,
		     otherEnv ? "a new environment" : (silent ? "the drain was silent" : (addPositions ? "the positions were asked for"
		                                                                     : (addArrows ? "the arrows were asked for"
		                                                                                  : "the path was asked for"))),
		     GetCurrentThreadId(), c.haveEvents ? "found" : "MISSING", c.listeners, c.full ? wantListeners : 0,
		     c.missing ? " (missing: " : "", c.missingNames, c.missing ? ")" : "", posPart, kDrainEvent,
		     c.drain ? "added" : "NOT added", kReplacedGlobal,
		     c.replaced ? "replaced" : (!c.full ? "kept" : (c.replaceFound ? "NOT replaced" : "absent")));
	}
	return true;
}

bool QueueRaise(const char* eventName, int nargs, const Arg* args)
{
	if (!g_enabled || !eventName || nargs < 0 || nargs > kArgCap || strlen(eventName) >= kNameCap || (nargs && !args))
	{
		InterlockedIncrement((volatile LONG*)&g_stats.raisesDropped);
		return false;
	}
	bool ok = false;
	EnterCriticalSection(&g_cs);
	if (g_queueCount < kQueueCap)
	{
		Raise& q = g_queue[(g_queueHead + g_queueCount) % kQueueCap];
		strcpy_s(q.name, sizeof(q.name), eventName);
		q.nargs = nargs;
		for (int i = 0; i < nargs; ++i) q.args[i] = args[i];
		++g_queueCount;
		ok = true;
	}
	LeaveCriticalSection(&g_cs);
	InterlockedIncrement((volatile LONG*)(ok ? &g_stats.raisesQueued : &g_stats.raisesDropped));
	return ok;
}

void OnUpdate()
{
	if ((!g_enabled && !g_positions && !g_arrows && !g_path && !HaveFrameHook()) || g_stats.installs == 0) return;
	const LONG tick = g_drainTick;
	if (tick != g_seenTick)
	{
		g_seenTick = tick;
		g_updatesSinceTick = 0;
		if (g_stalled)
		{
			g_stalled = false;
			Logf("ui bridge: the drain runs again (gen %ld, drains %lu)", g_gen, (unsigned long)g_stats.drains);
		}
		return;
	}
	if (++g_updatesSinceTick == (uint32_t)kStallUpdates && !g_stalled)
	{
		g_stalled = true;
		InterlockedIncrement((volatile LONG*)&g_stats.stalls);
		Logf("ui bridge: STALL: the drain (Events.%s listener, gen %ld) has not run for %d Updates (drains so far %lu); "
		     "raises wait in the queue", kDrainEvent, g_gen, (int)kStallUpdates, (unsigned long)g_stats.drains);
	}
}

Stats GetStats()
{
	Stats s = g_stats;
	s.gen = (uint32_t)g_gen;
	return s;
}

void FormatStats(char* out, size_t cap)
{
	const Stats s = GetStats();
	_snprintf_s(out, cap, _TRUNCATE,
	            "ui bridge: gen %u, installs %u (failed %u), listeners added %u, records arrow %u remove-arrows %u minimap %u "
	            "movement %u unit-moving %u, posted %u, drains %u, raises queued %u done %u failed %u dropped %u, UnitMoving "
	            "calls %u (replaced %u), stale calls %u, wrong-thread %u, stalls %u, frame hook calls %u",
	            s.gen, s.installs, s.installFailures, s.listenersAdded, s.records[kKindSpawnArrow],
	            s.records[kKindRemoveAllArrows], s.records[kKindMinimapTexture], s.records[kKindDisplayMovement],
	            s.records[kKindUnitMovingCalled], s.posted, s.drains, s.raisesQueued, s.raisesDone, s.raisesFailed,
	            s.raisesDropped, s.unitMovingCalls, s.unitMovingReplaced, s.staleCalls, s.wrongThread, s.stalls,
	            s.frameHookCalls);
	if (g_positions)   // issue 4754: the line of ui_bridge=1 alone is unchanged
	{
		const PositionStats p = GetPositionStats();
		const size_t used = strlen(out);
		if (used + 1 >= cap) return;
		_snprintf_s(out + used, cap - used, _TRUNCATE,
		            "; unit positions: records unit-position %u frame-mark %u, positions %u, arrivals %u (no position %u), "
		            "queue changed with moves left %u, bad arguments %u, marks %u (early %u), units known %u (clears %u)",
		            s.records[kKindUnitPosition], s.records[kKindFrameMark], p.positions, p.arrivals, p.arrivalsNoPosition,
		            p.queueRemaining, p.badArgs, p.marks, p.earlyFlushes, p.unitsKnown, p.tableClears);
	}
	if (g_arrows)   // ticket 0199
	{
		const ArrowStats a = g_astats;
		const size_t used = strlen(out);
		if (used + 1 >= cap) return;
		_snprintf_s(out + used, cap - used, _TRUNCATE,
		            "; arrows: spawns %u, removes %u, bad arguments %u, stale wrapper calls %u, wraps %u (unwraps %u)",
		            a.spawns, a.removes, a.badArgs, a.staleCalls, a.wraps, a.unwraps);
	}
	if (g_path)   // ticket 0300
	{
		const PathStats p = g_pathStats;
		const size_t used = strlen(out);
		if (used + 1 >= cap) return;
		_snprintf_s(out + used, cap - used, _TRUNCATE,
		            "; path: updates %u (empty %u, cut %u), node records %u, tested %u, no unit %u, embark %u, attack %u, "
		            "swap %u, bad arguments %u, stale calls %u, movement records %u, listener time %.3f ms (mean %.1f us, "
		            "max %u us)",
		            p.updates, p.empty, p.truncated, p.nodes, p.tested, p.noUnit, p.embarks, p.attacks, p.swaps, p.badArgs,
		            p.staleCalls, s.records[kKindDisplayMovement], (double)p.costUs / 1000.0,
		            p.updates ? (double)p.costUs / p.updates : 0.0, p.costMaxUs);
	}
}

void ResetForTest()
{
	EnterCriticalSection(&g_cs);
	g_queueHead = g_queueCount = 0;
	LeaveCriticalSection(&g_cs);
	memset(&g_stats, 0, sizeof(g_stats));
	g_gen = 0;
	g_seq = 0;
	g_drainTick = 0;
	g_seenTick = 0;
	g_updatesSinceTick = 0;
	g_stalled = false;
	g_installedEnv = NULL;
	g_checked = false;
	g_lastCheck = 0;
	g_tickAtCheck = 0;
	g_tickChangedAt = 0;
	g_enabled = 0;
	g_test = 0;
	g_frameHook = NULL;
	g_frameLuaHook = NULL;         // ticket 0586
	g_mainThread = 0;
	g_positions = 0;               // issue 4754
	g_installedPositions = false;
	g_posCount = 0;
	memset(&g_pstats, 0, sizeof(g_pstats));
	g_posCalls = g_arrCalls = 0;
	g_arrows = 0;                  // ticket 0199
	g_installedArrows = false;
	g_arrowsTried = false;
	memset(&g_astats, 0, sizeof(g_astats));
	g_arrowCalls = 0;
	g_path = 0;                    // ticket 0300
	g_pathTests = NULL;
	g_installedPath = false;
	g_pathTried = false;
	memset(&g_pathStats, 0, sizeof(g_pathStats));
	g_pathCalls = 0;
	g_pathUpdate = 0;
	LastClear();
}
}  // namespace UiBridge
