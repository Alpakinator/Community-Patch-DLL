// proxy_hooks.cpp - hand-written proxy hooks of ICvGameContext (team-2300 contract section D; SetGameDatabase is in
// proxy_db.cpp since issue 2504) and of
// ICvUserInterface1::AddPopup (issue 9008). Hand-written.
// The generator (proxy/gen/gen_runtime.py) finds these definitions by name and calls them instead of the generated
// bodies; SetDLLIFace is mechanical, so its generated body is available as ProxyFwd_ICvGameContext3_SetDLLIFace.
#include "proxy_rt_internal.h"
#include "proxy_ifaces.gen.h"
#include <string.h>

using namespace ProxyRtInt;

// SetGameDatabase(Database::Connection*) moved to proxy_db.cpp (issue 2504: the proxy finds the engine's database
// itself; the DLL's writes are replayed on it).

// ICvUserInterface1::AddPopup(const CvPopupInfo&) (issue 9008; the DLL calls it in a Tiny AI game, epic 2300 runs):
// the struct crosses by value, field by field in declaration order (CvGameCoreDLLUtil/include/CvStructs.h: iData1-3,
// iFlags, bOption1, bOption2, eButtonPopupType, szText[512]); the server side is RemoteUserInterface::AddPopup
// (server/src/remote/srv_remote_objects.cpp). CALL: self, i32 x4, b x2, i32 type, str text. RETURN: nothing.
bool EngHook_ICvUserInterface2_AddPopup_s45(ICvUserInterface2* self, vpipc::Reader& args, vpipc::Writer& ret,
                                            vpipc::Channel& ch)
{
	CvPopupInfo info;
	info.iData1 = args.i32();
	info.iData2 = args.i32();
	info.iData3 = args.i32();
	info.iFlags = args.i32();
	info.bOption1 = args.b();
	info.bOption2 = args.b();
	info.eButtonPopupType = static_cast<ButtonPopupTypes>(args.i32());
	const char* text = args.cstr();
	if (!args.ok())
		return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "ICvUserInterface1::AddPopup(const CvPopupInfo &): bad arguments");
	strncpy_s(info.szText, sizeof(info.szText), text ? text : "", _TRUNCATE);
	static_cast<ICvUserInterface1*>(self)->AddPopup(info);
	return true;
}

// SetDLLIFace(ICvEngineUtility1*): forwarded; the engine's last call is SetDLLIFace(NULL) (engine-contract 8), after
// which the proxy shuts the server down (kRt_Shutdown, wait for the process, close the endpoint).
bool ProxyHook_ICvGameContext3_SetDLLIFace(Proxy_ICvGameContext3& self, ICvEngineUtility1* pDll)
{
	if (pDll) SetEngineUtility(pDll);
	Log("SetDLLIFace(%p)", pDll);
	const bool r = ProxyFwd_ICvGameContext3_SetDLLIFace(self, pDll);
	if (!pDll)
	{
		SetEngineUtility(NULL);
		Shutdown();
	}
	return r;
}
