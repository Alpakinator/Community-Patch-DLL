// proxy_rt.h - the x86 proxy runtime API that the GENERATED proxy-side code calls (epic 2300).
// Declarations frozen by the team-2300 coordinator (generated code depends on them); implementation: proxy_rt.cpp.
// Toolchain: clang-cl -m32, VC9 headers and msvcr90 (the engine's heap), C++11 language at most; no <atomic>,
// <thread>, <mutex> (research/06-tooling-and-workflows/build-and-install-dll.md section 5). Design:
// research/03-dll-engine-boundary/proxy-design.md section 11; wire values: proxy/include/vp_wire.h.
#pragma once
#include "CvGameCoreDLLPCH.h"  // the DLL's interface declarations (ICv*, GUIDs, FDataStream, CvString)
// Issue 4253 (epic 0334, MC #1614 G1): one proxy serves the 22- and the 43-major DLL. The major/minor split is the DLL's
// compile-time choice and the proxy must never bake it in: its names are poisoned after the fork include (this header is
// the only proxy header that includes the fork's; vp_gen_common.h, the generated code's first include, includes it).
// The totals MAX_PLAYERS, MAX_TEAMS, MAX_CIV_PLAYERS are the same in both builds. The server has the same block
// (server/src/srv_common.h). research/03-dll-engine-boundary/player-limits.md section 8.2.
#undef MAX_MAJOR_CIVS
#undef MAX_MINOR_CIVS
#undef OBSERVER_TEAM
#undef MAX_PREGAME_MAJOR_CIVS
#define MAX_MAJOR_CIVS _Pragma("GCC error \"MAX_MAJOR_CIVS differs between the DLL builds: the proxy must not use it; read GameDefines/Defines at run time (issue 4253)\"") 0
#define MAX_MINOR_CIVS _Pragma("GCC error \"MAX_MINOR_CIVS differs between the DLL builds: the proxy must not use it; read GameDefines/Defines at run time (issue 4253)\"") 0
#define OBSERVER_TEAM _Pragma("GCC error \"OBSERVER_TEAM depends on the DLL's major count: the proxy must not use it (issue 4253)\"") 0
#define MAX_PREGAME_MAJOR_CIVS _Pragma("GCC error \"MAX_PREGAME_MAJOR_CIVS exists only in the 43-major DLL build (issue 4253)\"") 0
#include <stdint.h>
#include "vpipc/vpipc.h"
#include "vp_wire.h"

namespace ProxyRt
{
// ---- calls the proxy makes (engine -> DLL, and runtime calls) ------------------------------------------------
// The channel bound to the calling engine thread, by role (issue 2502): the thread that called DllGetGameContext is
// the ui channel; the first other thread whose first call is ICvGameContext1::GetGame or ICvGame1::Update is the core
// channel (the EXE's game-core thread); every other thread uses the shared aux channel. A thread's channel is chosen
// at its first call and never changes. Before that call, Chan() returns a placeholder that Begin/End resolve: pass
// the returned channel only to Begin and End (generated code does).
vpipc::Channel& Chan();
// Start a call: counts it and starts its clock. Returns the writer for the arguments.
vpipc::Writer& Begin(vpipc::Channel& ch, uint32_t method);
// Publish, wait, service nested calls and posts. true: r reads the RETURN payload. false: the call failed; the
// failure has been handled by the policy (logged, counted; with on_server_death=exit the process has ended and this
// does not return), and the generated caller returns ProxyDefault<R>::Get().
bool End(vpipc::Channel& ch, uint32_t method, vpipc::Reader& r);
// After the generated decoder: logs and counts a RETURN that was shorter than the signature (r.ok() false).
void DecodeCheck(uint32_t method, const vpipc::Reader& r);

// ---- DLL objects (they live in the server; the engine holds Proxy_<I> wrappers) -------------------------------
// A wrapper's handle (the engine passes a DLL object back as an argument). NULL -> {0, 0}. A pointer that is not a
// live wrapper -> {0, 0}, logged and counted.
vpipc::Handle HandleOf(const void* pInterface);
// A new wrapper for a handle the server returned; the engine owns it (Destroy forwards and frees it). Returns the
// wrapper as a pointer to the MOST DERIVED interface of the handle's kind (vpk_most_derived), as void*: the generated
// caller casts it to that interface first. Null handle -> NULL.
void* Wrap(vpipc::Handle h);
// The same for a DLL object passed as an argument of a DLL -> engine call: the wrapper lives until the dispatch of
// that call returns (DispatchScope), because the DLL frees its wrapper object after the call.
void* WrapTemp(vpipc::Handle h);
// Called by a generated Destroy() after it forwarded the call: forgets the wrapper (the caller then deletes it).
void ForgetWrapper(const void* pInterface);
// The generated wrapper constructor and destructor register and unregister the live wrapper.
void AddWrapper(const void* pInterface, vpipc::Handle h);
// A local QueryInterface answer is one more reference; Destroy releases one and forwards only the last (true).
// (Added 2026-11-10 in integration: the engine destroys the net message handler and its QueryInterface result.)
void WrapperAddRef(const void* pInterface);
bool WrapperRelease(const void* pInterface);
// Issue 2500 (live, 2026-11-10): the DLL's game context is a singleton whose Destroy does nothing
// (CvDllGameContext::Destroy); the real EXE Destroys it right after boot (GetPreGame round). Its wrapper is pinned
// at boot: Destroy on a pinned wrapper neither crosses nor frees it.
bool IsPinned(const void* pInterface);
void PinContext(const void* pInterface);
// Issue 2700: after the last reference of a wrapper was released. true: a local wrapper over a canonical object
// (proxy_local.h); its lock released if it stands for one; the generated Destroy forgets and deletes it without a
// crossing. destroyMethod: the Destroy method id (counted as a local answer).
bool LocalRelease(const void* pInterface, uint32_t destroyMethod);  // true: handled (the wrapper is pooled, not deleted)
// Issue 2700 item 4: a temporary wrapper (WrapTemp) the engine QueryInterfaces is kept instead of freed when the
// dispatch scope ends: a QueryInterface(ICvUnknown) is forwarded on its handle (the server registers the object
// persistently and the DLL counts one more reference), and its last Destroy forwards. true: it was temporary and is
// promoted now. The caller counts the engine's reference (WrapperAddRef) in every case. Issue 2708: the scope keeps
// its own reference until the call returns (the EXE uses the borrowed argument after Destroying its QueryInterface
// result); if the engine's references drop to 0 inside the call, WrapperRelease demotes the wrapper back to its scope
// and forwards one Destroy (balancing the promotion); at the scope's end a still-held wrapper only loses the scope's
// reference.
bool KeepIfTemp(const void* pInterface);

// ---- engine objects (they live here; the server holds RemoteGen_<I> objects) ----------------------------------
// Find or register an engine object and return its handle (kind = the engine interface kind given; engine bit set).
vpipc::Handle EngineHandleOf(const void* p, uint32_t kind);
// Handle -> engine object, checked against the expected kind (chain); NULL when unknown (logged, counted).
void* EngineResolve(vpipc::Handle h, uint32_t kind);
// Forget an engine object (after the server destroyed it, e.g. DestroyArgs, Destroy of an engine utility).
void EngineForget(vpipc::Handle h);

// ---- values returned by pointer or reference must outlive the call -------------------------------------------
const char* Intern(const char* s);
const wchar_t* InternW(const wchar_t* s);
const CvString& InternCv(const char* s);

// ---- FDataStream& parameters (vp_wire.h "stream parameters") -------------------------------------------------
void StreamInArg(vpipc::Writer& w, FDataStream& s);   // opens or reuses a session, writes session id + offset
void StreamInRet(vpipc::Reader& r, FDataStream& s);   // reads the consumed count, advances the engine stream
void StreamOutRet(vpipc::Reader& r, FDataStream& s);  // reads the bytes and writes them into the engine stream

// ---- serving calls from the server (DLL -> engine) -----------------------------------------------------------
// Each served CALL runs inside a DispatchScope (the runtime's channel handler creates it); WrapTemp wrappers die
// with it.
struct DispatchScope
{
	DispatchScope();
	~DispatchScope();
};
// A dispatch that cannot be carried out: sets the return status and writes the message. Returns true (handled).
bool DispatchFail(vpipc::Channel& ch, vpipc::Writer& ret, uint32_t status, const char* message);

// ---- loud stubs ----------------------------------------------------------------------------------------------
// A generated default for a hook nobody wrote (proxy side): logs the first call, counts every call; with strict=1 in
// vpproxy.ini the process ends with exit code 4 (fail on unexpected).
void Unimplemented(uint32_t method, const char* key);
}  // namespace ProxyRt

// Value-initialised default for a failed call or an unimplemented method.
template<class T> struct ProxyDefault
{
	static T Get() { return T(); }
};
template<> struct ProxyDefault<void>
{
	static void Get() {}
};
template<class T> struct ProxyDefault<T&>
{
	static T& Get()
	{
		static T s_value;
		return s_value;
	}
};
template<class T> struct ProxyDefault<const T&>
{
	static const T& Get()
	{
		static T s_value;
		return s_value;
	}
};
