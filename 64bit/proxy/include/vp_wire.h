// vp_wire.h - wire conventions shared by the x86 proxy and the x64 server (epic 2300).
// Owner: the team-2300 coordinator. C++03 language, no standard library: it is compiled by clang-cl -m32 against the
// VC9 headers (proxy) and by clang-cl -m64 against the MSVC 14 STL (server). The design these values implement is
// research/03-dll-engine-boundary/proxy-design.md section 11. Payload encoding is ipc/PROTOCOL.md section 5.
#pragma once
#include <stdint.h>

namespace vpw {

// ------------------------------------------------------------------------------------------------- handles
// vpipc::Handle.type:
//   bits 0-15  kind: interface type id kT_<I> (1-based position of I in proxy/model/interfaces.json) of the MOST
//              DERIVED interface the object implements (generated table vpk_most_derived), so one wrapper class per
//              object answers every interface of its chain.
//   bits 16-23 owner: player id + 1 when the handle carries it (units, cities); 0 = not carried. Epic 2300 always
//              sends 0 (every call crosses; answering GetOwner/GetID locally is a later optimisation, D21 item 4).
//   bit 31     1 = an engine object (lives in the EXE / host process, slot in the proxy's engine table);
//              0 = a DLL object (lives in the server, slot in the server's handle table).
// id: the slot in the owning side's table, > 0. The null handle is {0, 0}.
const uint32_t kKindMask = 0x0000FFFFu;
const uint32_t kOwnerShift = 16;
const uint32_t kOwnerMask = 0x00FF0000u;
const uint32_t kEngineBit = 0x80000000u;
inline uint32_t Kind(uint32_t type) { return type & kKindMask; }
inline bool IsEngine(uint32_t type) { return (type & kEngineBit) != 0; }
inline uint32_t MakeType(uint32_t kind, bool engine) { return (kind & kKindMask) | (engine ? kEngineBit : 0u); }

// --------------------------------------------------------------------------------------- runtime methods
// Method ids of generated calls are (kT_<I> << 16) | method index. Runtime methods use type ids that no interface
// has: 0xFFF0 for proxy -> server, 0xFFF1 for server -> proxy (engine side). "ret" is the RETURN payload.
enum RtMethod
{
	// proxy -> server
	kRt_Boot = 0xFFF00001u,            // args: u32 proxy pid, str game DLL path ("" = the server's config).
	                                   // ret: u32 ok (1/0), str message, handle of the DLL's game context (kind of
	                                   // its most derived ICvGameContext), u32 server pid.
	kRt_Shutdown = 0xFFF00002u,        // args: u32 reason (0 = orderly, after SetDLLIFace(NULL)). ret: str report path.
	                                   // The server writes its report and exits after sending the RETURN.
	kRt_Stats = 0xFFF00003u,           // args: none. ret: str (the server's counter table, TSV).
	kRt_LuaRegister = 0xFFF00010u,     // args: none, or (ticket 4804) u32 the proxy's Lua bridge version
	                                   // (vp_luaraise.h vplr::kVersion; the server refuses another). ret: u32 n, then
	                                   // n x (str global name, value): the globals the DLL's RegisterScriptLibraries
	                                   // creates or changes in a fresh Lua state.
	kRt_LuaDescribeType = 0xFFF00011u, // args: str type name (LuaTypes key). ret: u32 n, then n x (str key, value):
	                                   // the members of LuaTypes[name] except __instances, __index.
	kRt_LuaCall = 0xFFF00012u,         // args: value callee (kLCFunc or kLSRef), u32 nargs, nargs x value.
	                                   // Issue 2805: bits 30-31 of the nargs word carry the phase the proxy saw
	                                   // (0 unknown = local answers off, 1 open, 2 in flight); bit 29 marks a Lua-mirror would-hit
	                                   // (lua_mirror=0) or the verifying read of a hit (issue 2805); the server masks them
	                                   // out and counts per function and phase (proxy-lua-calls.tsv).
	                                   // ret: u32 status (0 ok, 1 Lua error); ok: u32 nres, nres x value;
	                                   // error: str message (raised in the engine VM with lua_error).
	kRt_LuaIndex = 0xFFF00013u,        // args: u32 sref id, value key. ret: value (rawget, then __index).
	kRt_LuaRelease = 0xFFF00014u,      // args: u32 n, n x u32 sref id. ret: nothing.
	kRt_StreamOpen = 0xFFF00020u,      // args: bytes (the rest of an engine read stream). ret: u32 session id.
	// issue 2500 (coordinator): the harness Lua channel runs a chunk on the core thread at the top of Update while the
	// server thread serving the core channel holds the DLL's game-core lock (taken by the CvDllGame wrapper of GetGame);
	// the DLL's own channel released it around the chunk (CvLuaSupport.cpp PollExternalLuaRequest). Same dance here.
	kRt_CoreLockYield = 0xFFF00030u,   // args: none. ret: i32 depth released (all levels held by the serving thread).
	kRt_CoreLockResume = 0xFFF00031u,  // args: i32 depth. ret: nothing (the serving thread takes the lock depth times).
	// issue 2500 (live): an engine read stream without a known size (the real EXE's decompressing save stream reports
	// GetSizeLeft() 0x7FFFFFFF and ReadIt never comes up short) cannot be read ahead: the server pulls exactly the bytes
	// the DLL reads (kRtE_StreamRead), so the engine stream ends where the in-process DLL would have left it.
	kRt_StreamOpenPull = 0xFFF00022u,  // args: u32 proxy stream token. ret: u32 session id (a pull session).
	// issue 2700 (local answers, proxy-design section 16): canonical DLL objects and the game-core lock they stand for.
	kRt_Canon = 0xFFF00040u,           // args: u32 generation, u32 code (vpw::CanonCode), i32 index. ret: handle (the
	                                   // server's permanent object of that code and index, made with the lock
	                                   // suppressed; null when the DLL returns NULL). A new generation drops the old
	                                   // table (the objects are kept alive, never reused). Issue 2801: the handle
	                                   // {type 0, id -1} is a refusal (a generation change is being served on some
	                                   // thread): the proxy then lets the getter cross.
	kRt_LockSync = 0xFFF00041u,        // args: none. ret: nothing. The server's prologue of every call on ui/core sets
	                                   // the serving thread's explicit lock depth to the proxy's want word (shared
	                                   // memory Local\VPProxyLock_<proxy pid>, 16 LONGs: 0-2 want (ui, core, aux
	                                   // unused), 3 flags (bit0 local answers on, bit1 local_verify), 4-6 the server's
	                                   // DLL -> engine call counts per channel, 8 the proxy's lock-count errors (issue
	                                   // 2801: non-zero fails the run at the server's next served call);
	                                   // this call only carries it.
	// issue 0178 (the proxy's Lua bridge, proxy/src/proxy_ui_bridge.h): a record of the UI's state, made on the EXE's
	// main thread and sent on the ui channel as a POST only (no answer; a full ring makes the post wait, a failed post is
	// counted, never retried as a call). args: bytes (32: UiBridge::Record, version 1). The server puts it into the
	// renderer's feed (SrvFeed::kUiMirror) and counts it; no DLL code reads it (rule R1).
	kRt_UiRecord = 0xFFF00050u,

	// server -> proxy (served by the proxy on the thread waiting in a call)
	kRtE_LuaGetGlobal = 0xFFF10001u,   // args: u32 engine Lua thread id, str name. ret: value.
	kRtE_LuaCall = 0xFFF10002u,        // args: u32 engine Lua thread id, value callee (kLERef), u32 nargs, nargs x value,
	                                   // u32 nret. Runs through the engine's own ICvEngineScriptSystem1::CallFunction
	                                   // (so its Lua error accounting sees it). ret: u32 status (0 ok, 1 failed);
	                                   // ok: u32 n, n x value; failed: str message.
	kRtE_LuaRelease = 0xFFF10003u,     // args: u32 n, n x u32 eref id. ret: nothing.
	// issue 2504 (proxy_db.cpp, srv_remote_db.cpp, proxy_lua.cpp, srv_remote_lua.cpp):
	kRtE_DbOps = 0xFFF10004u,          // args: u32 n, n x bytes (vpdb::Op, proxy/include/vp_dbfp.h): writes the DLL made
	                                   // to its game database, applied in order on the engine's connection.
	                                   // ret: u32 n, n x u32 result (vpdb::Apply), f64 seconds the proxy spent
	                                   // applying them (added by the 2504 follow-up: batches).
	kRtE_DbFingerprint = 0xFFF10005u,  // args: str when (for the log). ret: bytes (vpdb::Fingerprint of the engine's
	                                   // database).
	kRtE_LuaRunChunk = 0xFFF10006u,    // args: u32 engine Lua thread id, u32 n, n x (str global name, u32 kind: 0 =
	                                   // value follows, 1 = the engine thread's registry), str chunk. Sets the globals
	                                   // on the engine thread (lua_setfield), then luaL_loadstring + lua_pcall of the
	                                   // chunk there. ret: u32 status (0 ok, 1 failed); ok: u32 n, n x value; failed:
	                                   // str message.
	kRtE_DbSnapshot = 0xFFF10007u,     // 2504 follow-up. args: str path (the server's run folder). The proxy writes the
	                                   // engine's database there (BinaryIO::Save). ret: u32 ok, f64 seconds.
	// ticket 4804 (speedup P2): one GameEvents raise in one crossing. The proxy makes the EXE's CreateArgs, the pushes,
	// the Call* and DestroyArgs on the thread that waits in the raise. args and ret: proxy/include/vp_luaraise.h.
	kRtE_LuaRaise = 0xFFF10008u
};
const uint32_t kRtTypeProxyToServer = 0xFFF0u;
const uint32_t kRtTypeServerToProxy = 0xFFF1u;

// Issue 2700: what kRt_Canon makes (the DLL call the server makes with the game-core lock suppressed).
enum CanonCode
{
	kCanonGame = 1,        // ICvGameContext1::GetGame()           CvDllGame (locks in its constructor)
	kCanonPreGame = 2,     // ICvGameContext1::GetPreGame()        CvDllPreGame (stateless)
	kCanonMap = 3,         // ICvGameContext1::GetMap()            CvDllMap
	kCanonGameAsynch = 4,  // ICvGameContext1::GetGameAsynch()     CvDllGameAsynch
	kCanonTeam = 5,        // ICvGameContext1::GetTeam(index)      CvDllTeam
	kCanonPlayer = 6,      // ICvGame1::GetPlayer(index) on the canonical game; CvDllPlayer (locks)
	kCanonTerrainInfo = 7, // ICvGameContext1::GetTerrainInfo(index)
	kCanonFeatureInfo = 8, // ICvGameContext1::GetFeatureInfo(index)
	kCanonCount = 9
};
// Name of the shared lock-want memory: Local\VPProxyLock_<proxy pid>; three LONGs (ui, core, aux; aux unused).
const char* const kLockShmPrefix = "Local\\VPProxyLock_";

// ------------------------------------------------------------------------------------------ Lua value codec
// One value = u32 tag, then the tag's payload. Tables are copied by value (keys and values recursively, at most
// kLuaMaxDepth levels and kLuaMaxBytes per value; beyond that the value is sent as kLUnsupported and counted).
enum LuaTag
{
	kLNil = 0,
	kLFalse = 1,
	kLTrue = 2,
	kLNumber = 3,      // f64
	kLString = 4,      // str (length-prefixed; may contain NUL bytes)
	kLTable = 5,       // u32 n pairs, then n x (key value, value value)
	kLCFunc = 6,       // u32 function id: a C function of the DLL without upvalues (server registry of pointers)
	kLSRef = 7,        // u32 ref id, u32 Lua type (LUA_T*): a server-side value kept in the registry of the
	                   // server Lua state of the channel it was made on (closures such as Player.Units' iterator,
	                   // userdata such as Fractal objects). Engine side: a function becomes a thunk closure, a
	                   // userdata a proxy userdata whose __index asks kRt_LuaIndex; both encode back as kLSRef.
	kLInstance = 8,    // str type name (LuaTypes key, e.g. "Unit"), u64 pointer of the DLL object in the server:
	                   // a DLL instance table {__instance = lightuserdata}. Engine side: an instance table whose
	                   // __instance is a lightuserdata token (proxy table token -> pointer), metatable LuaTypes[name].
	kLERef = 9,        // u32 ref id, u32 Lua type: an engine-side value kept in the engine registry (a Lua function
	                   // of the map script). Server side: a stub C closure that calls kRtE_LuaCall.
	kLUnsupported = 10 // str Lua type name of a value the codec cannot carry (decoded as nil, counted, logged once)
};
const uint32_t kLuaMaxDepth = 16;
const uint32_t kLuaMaxBytes = 4u * 1024u * 1024u;

// ------------------------------------------------------------------------------------- stream parameters
// FDataStream& parameters of generated methods (direction by method name: Read*/read* = in, Write*/write* = out):
//   in:  args  u32 session id (from kRt_StreamOpen), u32 offset (bytes of the session already consumed)
//        ret   (after the return value and the other out-parameters) u32 bytes consumed by this call
//   out: args  nothing
//        ret   (same place) bytes: what the DLL wrote; the proxy WriteIt()s them into the engine's stream
// The proxy opens one session per engine stream and position (a load reads Game, Map, 64 teams, 64 players and the
// supporting data from one stream: one transfer, decision D21 item 6) and reuses it while the stream's position equals
// the session's start plus what was consumed.

// ------------------------------------------------------------------------------------- remote statuses
// Channel::set_return_status values of a failed dispatch (the RETURN payload is then a UTF-8 message).
enum ReturnStatus
{
	kStatusBadHandle = 16,  // 0-3 are the transport's own (layout.h kRet*: 3 = protocol error); corrected 2026-11-10 (was 2..6)
	kStatusBadArguments = 17,
	kStatusUnknownMethod = 18,
	kStatusUnimplemented = 19,
	kStatusFailed = 20,
	// issue 9036 (bug 0102, MC decision 2026-11-12): the engine threw a C++ exception inside a served call (the proxy
	// caught it at OnCall, so its lock is released). The call did not complete in the EXE, so the session must end:
	// the server reports `exe_exception` and exits (code 6); the proxy then sees the server gone (on_server_death).
	kStatusEngineException = 21
};

// ------------------------------------------------------------------------- human-game runtime methods (issue 2503)
// server -> proxy, served by the proxy's generated EngDispatchGenerated through the hand-written VpHumanServe
// (proxy/src/proxy_hooks_human.cpp); type 0xFFF2 so the runtime's Lua channel (0xFFF1) is not involved.
// issue 2500 (live): server -> proxy read of a pull session's engine stream. args: u32 token, u32 n. ret: bytes
// (what the engine stream's ReadIt(n) gave). Outside the enum so appends there by other issues cannot collide.
const uint32_t kRtE_StreamRead = 0xFFF10010u;

const uint32_t kRtTypeServerToProxyHuman = 0xFFF2u;
enum RtHumanMethod
{
	// FIGameIniParser::GetKeyValue on the engine's parser of the ICvPreGame1::loadFromIni call in progress.
	// args: u32 parser token (the loadFromIni CALL carries it), u32 value type (kIniString, kIniInt, kIniUInt,
	//       kIniFloat), str group, str key, str description (null kept), the default (str with null kept | i32 | u32 |
	//       f32), b value pointer present.
	// ret:  b result of GetKeyValue, then the value when the pointer was present (str | i32 | u32 | f32).
	kRtH_IniGetKeyValue = 0xFFF20001u
};
enum IniValueType
{
	kIniString = 0,  // FString*
	kIniInt = 1,     // int*
	kIniUInt = 2,    // uint*
	kIniFloat = 3    // float*
};

}  // namespace vpw
