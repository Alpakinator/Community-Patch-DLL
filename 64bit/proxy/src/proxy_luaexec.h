// proxy_luaexec.h - the harness's external Lua channel served by the x86 proxy against the ENGINE's Lua VM (epic
// 2500, issue 2505). Hand-written. Implementation and protocol: proxy_luaexec.cpp; the client is
// tools/harness/lua.py (unchanged: same event names, files, headers and result format as the DLL's channel).
//
// Why in the proxy: the DLL's own channel (LuaSupport::PollExternalLuaRequest) walks G.Threads of the engine VM to
// find the UI states; in the server the x64 DLL only sees scratch Lua states, so the channel has to live where the
// engine VM is (team-2500 decision, 2026-11-10).
#pragma once
#include <string>

class ICvEngineUtility1;
struct lua_State;

namespace ProxyLuaExec
{
// One poll of the channel. Called on the game-core thread at the top of every proxied ICvGame1::Update, BEFORE the
// call is forwarded to the server (worker 2502 owns that hook). Cheap when idle: an enable check every 2 s until the
// channel is on, then one WaitForSingleObject(0) per call.
void Poll();

// Creates the persistent engine Lua thread the channel runs chunks on, once per load of the proxy. Must be called
// on the engine's MAIN thread at database load (the moment the DLL's MemoryDiagnostics::GetProbeLuaState() creates
// its thread: inside ICvDLLDatabaseUtility1::CacheGameDatabaseData). proxy_luaexec.cpp calls it from its hook of
// CacheGameDatabaseData; it is public so that whoever owns that hook can call it if the hook moves. Idempotent.
void OnGameDatabaseCached();

// Issue 2805 (test_slot_poll's Lua leg, headless only): runs `code` with a local n = rounds prepended on the persistent
// thread in the "Main" environment (the same runner as the channel). true when it ran; result = its first return value
// as a number (-1 if none); err = the runner's text otherwise. Does not yield the game-core lock (the headless host has
// one thread, so the deadlock the channel avoids cannot occur); never use it in the real EXE.
bool RunTestChunk(const char* code, int rounds, int& result, std::string& err);

// Ticket 0805 (proxy-design 38): the engine's one Lua VM outlives this image (the EXE unloads the game DLL at a mod
// change and loads it again), so at the orderly shutdown (proxy_rt.cpp Shutdown, SetDLLIFace(NULL), the image still
// mapped) the proxy leaves nothing in the VM that would run this image's code later:
//   - the persistent VP_LUAEXEC thread is freed (FreeLuaThread), so G.Threads no longer lists its environment, whose
//     Game, Players, ... hold this image's DLL-function thunks (the Lua channel's "Main" resolution picked such an
//     environment during a later load and called an unloaded FuncThunk: the EXE crash of 38.3);
//   - first, on that thread, proxy_lua.cpp's values are made inert (InertImageValues below): the metamethods of the
//     server-value stubs (__gc, __index, __call: a __gc of an old stub would otherwise run inside any later Lua
//     allocation, also inside the next activation), the master-form types' function tables, and the registry entries
//     under this image's light-userdata keys (a later image mapped at the same base would find them again).
// mainThread: the caller is the engine's main thread (the thread that called SetDLLIFace(NULL)); the engine's thread
// table may only change there (see OnGameDatabaseCached), so on any other thread nothing is done (logged). Once.
void OnShutdown(bool mainThread);

// What InertImageValues changed (logged by OnShutdown, checked by proxy/tests/reload_after_game/).
struct InertCounts
{
	int metaFields;     // metamethods removed from the metatables named by metaKeys
	int typeTables;     // master-form type metatables whose __index function table was emptied
	int typeFunctions;  // functions removed from those tables
	int registryKeys;   // registry entries under dropKeys that existed and were removed
};
// The core of the shutdown step, on any thread of the VM (it only touches the registry): for each light-userdata key in
// metaKeys the registry's table loses __gc, __index, __call, __newindex, __tostring, __eq, __len; the registry's table
// under typesKey maps type names to metatables, and each metatable's __index table is emptied and __index removed; then
// every key in dropKeys (and metaKeys and typesKey) is removed from the registry. Raises no Lua error. Plain Lua 5.1 C
// API: compiled into the proxy and, with VP_LUAEXEC_UNIT_TEST, into proxy/tests/reload_after_game/inert_test.cpp.
void InertImageValues(lua_State* L, void* const* metaKeys, int nMeta, void* typesKey, void* const* dropKeys, int nDrop,
                      InertCounts* counts);
}  // namespace ProxyLuaExec

// proxy_lua.cpp (ticket 0805): the lua_CFunction that OnShutdown runs on the persistent thread through the engine's
// CallCFunction; its argument (index 1, light userdata) is a ProxyLuaExec::InertCounts*. It calls InertImageValues with
// proxy_lua.cpp's own registry keys.
int ProxyLua_InertImage(lua_State* L);

// Accessors of the proxy runtime this file needs (requested from the owner of proxy_rt.cpp, issue 2505 Findings).
// Until they exist the linker uses fallbacks defined in proxy_luaexec.cpp (/alternatename), so the proxy links either
// way:
//   VpProxy_EngineUtility()   the ICvEngineUtility1* the engine passed to SetDLLIFace (NULL before / after). Fallback:
//                             NULL, and the channel answers nothing (it cannot find the cache folder).
//   VpProxy_CoreLockYield()   on the game-core thread inside the proxied Update: make the server thread that serves
//                             the core channel release every level of the DLL's game-core lock it holds (the CvDllGame
//                             wrapper of the engine's GetGame() holds one); returns the depth released, or -1 when
//                             the runtime cannot do it. Fallback: -1.
//   VpProxy_CoreLockResume(d) take the lock again d times on that server thread (d from CoreLockYield; nothing for
//                             d <= 0). Fallback: nothing.
extern "C" ICvEngineUtility1* VpProxy_EngineUtility();
extern "C" int VpProxy_CoreLockYield();
extern "C" void VpProxy_CoreLockResume(int depth);
