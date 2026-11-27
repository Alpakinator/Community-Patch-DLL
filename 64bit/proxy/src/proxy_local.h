// proxy_local.h - local answers of the x86 proxy (issue 2700, epic 2700). Hand-written.
// Design, invalidation of every local answer and the measurements: research/03-dll-engine-boundary/proxy-design.md
// section 16. Toolchain: clang-cl -m32, VC9 headers, msvcr90, C++11 language at most, Win32 primitives.
//
// Three mechanisms, all off with vpproxy.ini local_answers=0 (every call then crosses, as in epics 2300-2500):
// 1. Canonical handles. GetGame / GetPreGame / GetMap / GetGameAsynch / GetTeam(i) / GetPlayer(i) /
//    GetTerrainInfo(i) / GetFeatureInfo(i) return a NEW proxy wrapper (own reference count, as the DLL's fresh
//    wrapper has) over the server's permanent object of that code and index (kRt_Canon, fetched once per generation).
//    Its Destroy is local. The generation changes (and the table is dropped on both sides) at the start AND at the end
//    of every ICvGameContext1::Init / Uninit and ICvDLLDatabaseUtility1::CacheGameDatabaseData / FlushGameDatabaseData,
//    the only entry points that free or move the wrapped objects (issue 2701 [C]); while one of them is in flight on
//    any thread no canonical handle is fetched, stored or used (the getter crosses; issue 2801).
// 2. The game-core lock of the DLL's CvDllGame / CvDllPlayer wrappers (their constructors lock, destructors
//    release: CvDllGame.cpp:21-33, CvDllPlayer.cpp:21-33). Per EXE thread (ui and core only; aux threads always
//    cross) the proxy counts the live local lock wrappers C and publishes C in the lock-want word of the thread's
//    channel; the server sets its explicit lock depth to the word in the prologue of every call it serves on that
//    channel. The transition 0 -> 1 and the release to 0 cross at once (kRt_LockSync), so the lock is taken when
//    the in-process DLL would take it (a wait included) and is free when it would be free.
// 3. The epoch mirror of pure getters. Every crossing that is not proven pure (proxy/model/pure-methods.tsv, issue
//    2701) increments the epoch at its start and at its end and counts as in flight meanwhile. A getter result read
//    from the server while nothing was in flight is kept with the epoch it was read in; it is answered locally while
//    the epoch is unchanged and nothing is in flight. local_verify=1 asks the server as well and compares.
// 4. (issue 2805) The UI-Lua mirror: the same epoch rule for kRt_LuaCall of pure-listed DLL Lua functions with scalar
//    results (proxy/model/lua-pure-bindings.tsv), switched by vpproxy.ini lua_mirror (default 0 = shadow counters
//    only); declarations below (Lua*), the call path in proxy_lua.cpp LuaCallImpl.
#pragma once
#include "proxy_rt.h"
#include <string>

namespace ProxyLocal
{
// Settings (from vpproxy.ini through proxy_rt.cpp) and tables; after the counters exist (method keys).
void Configure(bool on, bool verify, int test);  // test: 0 none, 1 local_test=stale, 2 local_test=frozen
void InitTables();
// After the endpoint exists: the lock-want shared memory (Local\VPProxyLock_<pid>). false: local lock answers off.
bool InitShared();
bool On();
bool Verify();

// ---- epoch (ProxyRt::Begin / End call these for every call the proxy makes) -----------------------------------
bool IsPure(uint32_t method);
// The ForcePureNext flag of this thread, cleared (issue 2801, verification 2700 A6: ProxyRt::Begin takes it before
// anything can fail, so a Begin that fails before CallStart cannot leave it for the thread's next, unrelated call).
bool TakeForcePure();
void CallStart(uint32_t method, bool forcePure, bool& impure);  // impure: remember for CallEnd
void CallEnd(bool impure);
// Begin of a crossing: true when the method invalidates canonical handles (Init, Uninit, CacheGameDatabaseData,
// FlushGameDatabaseData); the table is dropped and the call counts as a generation change in flight until AfterCall.
bool BeforeCall(uint32_t method);
// End of a crossing whose BeforeCall returned true (every return path): a second generation and drop, then the
// in-flight count falls (issue 2801: a handle fetched while the call ran must not survive it).
void AfterCall(bool genChange);

// ---- canonical handles and local wrappers ---------------------------------------------------------------------
// A local answer of a getter returning a DLL object: the wrapper (as the MD interface of the handle's kind, void*),
// NULL for a canonical NULL, or kCross when the call must cross (local answers off, a lock kind on an aux or undecided
// thread, kRt_Canon failed).
extern void* const kCross;
void* Answer(uint32_t method, uint32_t code, int32_t index);
bool IsCanonHandle(vpipc::Handle h);
// A handle whose object lives as long as the DLL (the game context, CvDllContext singleton): mirror keys allowed.
void AddPermanent(vpipc::Handle h);
// Called by the generated Destroy after the last reference was released: true = a local wrapper (forgotten here,
// the lock released if it was a lock kind); the caller deletes it without a crossing.
int Release(const void* pInterface, uint32_t destroyMethod);  // 0 not local; 2 handled (pooled, never deleted)

// ---- mirror -----------------------------------------------------------------------------------------------------
struct Token
{
	bool ok;
	long epoch;
	long engine;  // the server's DLL -> engine call count for this thread's channel before the read
};
bool MirrorGet(uint32_t method, vpipc::Handle h, int32_t arg, int32_t& value);
Token MirrorBegin(vpipc::Handle h);
void MirrorPut(const Token& t, uint32_t method, vpipc::Handle h, int32_t arg, int32_t value);
// A value read under token t is still what the server would answer now (nothing impure started or is in flight).
bool StillValid(const Token& t);
// The next call this thread makes is pure whatever its method (a conditional method whose condition was just
// established in this epoch: the verifying HasTurnTimerExpired call).
void ForcePureNext();
// local_verify=1: a hit compared with the server's answer (counted; logged; fatal under strict=1).
void VerifyResult(uint32_t method, int32_t local, int32_t server);
// The canonical game handle (fetching it if needed); false if unavailable.
bool GameHandle(vpipc::Handle& h);

// ---- the UI-Lua mirror (issue 2805; proxy-design 17) ----------------------------------------------------------------
// kRt_LuaCall of a DLL Lua function that is `pure` in proxy/model/lua-pure-bindings.tsv, with scalar or instance
// arguments and an all-scalar result (nil, boolean, number, string), is kept with the epoch it was read in, keyed by
// (function id, encoded arguments). The epoch used here is the Lua epoch: g_epoch minus the bumps of pure-listed Lua
// calls in shadow mode (in mirror mode those calls are forced pure, so it equals g_epoch). Modes: 0 = off (local
// answers off), 1 = shadow (vpproxy.ini lua_mirror=0, the default: every call still crosses and bumps as before; a
// lookup that would have hit is only counted), 2 = mirror (lua_mirror=1: hits are answered here; the pure-listed call
// does not bump the epoch, which ForcePureNext makes exact for that one call).
int LuaMode();
bool LuaStaleTest();  // local_test=luastale: Lua entries are answered whatever their epoch (the negative test)
Token LuaBegin();      // ok when nothing impure (Lua epoch) is in flight and the engine word is known
// A hit: an entry of the current Lua epoch (any epoch under luastale) while nothing is in flight.
bool LuaGet(const std::string& key, std::string& value);
// Bracketed fill (D30 condition 3) and condition 2 (the server's engine word unchanged across the read).
void LuaPut(const Token& t, const std::string& key, const std::string& value);
// Shadow mode: the next crossing of this thread (the pure-listed kRt_LuaCall) is discounted in the Lua epoch; after
// its End, LuaShadowEnd closes the discount.
void LuaShadowMarkNext();
void LuaShadowEnd();
// local_verify=1: a Lua hit compared with the server's answer (bytes); fatal under strict=1 (exit 6).
void LuaVerifyResult(const char* function, bool equal);

// Shutdown report lines into the proxy log.
void Report();
// Issue 0119: frees this thread's per-thread tables (DllMain: DLL_THREAD_DETACH, and the unloading thread).
void FreeThread();
}  // namespace ProxyLocal

// proxy_lua.cpp: per-function lines of the UI-Lua mirror for ProxyLocal::Report (issue 2805).
void ProxyLuaMirrorReport();
// proxy_update.cpp: the vpproxy.ini settings as this process reads them (the same file proxy_rt.cpp reads; read once).
namespace ProxyLogic
{
struct Settings;
}
const ProxyLogic::Settings& ProxyIniSettings();

// The mirror around a forwarder: R is an integral or enum result; fwd() crosses.
template<class R, class F> R VpMirrored(uint32_t method, vpipc::Handle h, int32_t arg, F fwd)
{
	int32_t v = 0;
	if (ProxyLocal::MirrorGet(method, h, arg, v))
	{
		if (ProxyLocal::Verify())
		{
			const R s = fwd();
			ProxyLocal::VerifyResult(method, v, (int32_t)s);
		}
		return (R)v;
	}
	const ProxyLocal::Token t = ProxyLocal::MirrorBegin(h);
	const R r = fwd();
	ProxyLocal::MirrorPut(t, method, h, arg, (int32_t)r);
	return r;
}
