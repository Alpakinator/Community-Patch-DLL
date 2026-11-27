// srv_rt.h - the x64 server runtime API that the GENERATED server-side code calls in remote mode (epic 2300).
// Declarations frozen by the team-2300 coordinator (generated code depends on them); implementation:
// server/src/remote/srv_rt.cpp. Toolchain: the server's (clang-cl -m64, MSVC 14.4x STL, C++14, /MD, D15).
// Design: research/03-dll-engine-boundary/proxy-design.md section 11; wire values: proxy/include/vp_wire.h.
#pragma once
#include "srv_common.h"  // the DLL's interface headers with the server's settings
#include <stdint.h>
#include "vpipc/vpipc.h"
#include "vp_wire.h"

namespace SrvRt
{
// ---- calls the server makes (DLL -> engine, and runtime calls) -----------------------------------------------
// The channel of the calling server thread (one service thread per channel: ui, core, aux). A call from any other
// thread is a loud error (logged, counted) and gets the ui channel.
vpipc::Channel& Chan();
vpipc::Writer& Begin(vpipc::Channel& ch, uint32_t method);
// true: r reads the RETURN payload. false: failed; logged and counted; if the proxy is gone the server writes its
// report and exits (it never outlives the engine). The generated caller then returns SrvDefault<R>::Get().
bool End(vpipc::Channel& ch, uint32_t method, vpipc::Reader& r);
void DecodeCheck(uint32_t method, const vpipc::Reader& r);

// ---- DLL objects (they live here; the proxy holds wrappers) --------------------------------------------------
// Register a DLL object returned to the engine; kind = the most derived kind of its static type (generated code
// passes it). Find-or-create by pointer; the slot lives until the proxy forwards Destroy (Unregister).
vpipc::Handle Register(const void* p, uint32_t kind);
// The same for a DLL object passed as an argument of a DLL -> engine call: released when that call returns (unless
// it was or becomes persistently registered meanwhile).
vpipc::Handle RegisterTemp(const void* p, uint32_t kind);
// Handle -> object, as `kind`; checks that `kind` is in the chain of the handle's kind (vpk_in_chain). NULL when the
// handle is null, unknown or of another kind (logged, counted).
void* Resolve(vpipc::Handle h, uint32_t kind);
// After a forwarded Destroy: the slot is removed.
void Unregister(vpipc::Handle h);

// ---- engine objects (they live in the engine; the DLL holds RemoteGen_<I> objects) ---------------------------
// A remote object's engine handle (the DLL passes an engine object back as an argument). NULL -> {0, 0}.
vpipc::Handle EngineHandleOf(const void* pRemote);
// Engine handle -> the remote object that stands for it (one per handle, cached), as a pointer to the MOST DERIVED
// engine interface of the handle's kind, as void*: the generated caller casts it to that interface first.
// Null handle -> NULL. Implemented by hand (srv_remote_objects.cpp) because the hand-written subclasses of the
// generated RemoteGen_<I> classes are the objects to create.
void* EngineWrap(vpipc::Handle h);
// The remote object for a handle is dropped (after DestroyArgs, ICvEngineUtility Destroy, ...).
void EngineForget(vpipc::Handle h);

// ---- FDataStream& parameters (vp_wire.h "stream parameters") -------------------------------------------------
FDataStream& StreamIn(vpipc::Reader& args);             // the session stream, positioned at the offset
void StreamInRet(vpipc::Writer& ret, FDataStream& s);   // writes the bytes consumed by this call
FDataStream& StreamOut();                               // a fresh memory stream for this call (per thread)
void StreamOutRet(vpipc::Writer& ret, FDataStream& s);  // writes its bytes

// ---- serving calls from the proxy (engine -> DLL) ------------------------------------------------------------
bool DispatchFail(vpipc::Channel& ch, vpipc::Writer& ret, uint32_t status, const char* message);
// Called by the generated dispatcher of ICvGame1::Update around the DLL call (thread role: IsGameCoreThread is true
// only inside Update, engine-contract 1.2, as the direct server answers).
void EnterUpdate();
void LeaveUpdate();

// ---- loud stubs ----------------------------------------------------------------------------------------------
// A DLL -> engine method that has no remote treatment (generated default of a non-mechanical method nobody
// overrode), or an engine -> DLL hook nobody wrote: logs the first call, counts every call, lists it in the report;
// with fail_on_unexpected=1 in the server config the server exits with code 4 after reporting.
void Unserved(uint32_t method, const char* key);
}  // namespace SrvRt

template<class T> struct SrvDefault
{
	static T Get() { return T(); }
};
template<> struct SrvDefault<void>
{
	static void Get() {}
};
template<class T> struct SrvDefault<T&>
{
	static T& Get()
	{
		static typename std::remove_const<T>::type s_value = typename std::remove_const<T>::type();
		return s_value;
	}
};
