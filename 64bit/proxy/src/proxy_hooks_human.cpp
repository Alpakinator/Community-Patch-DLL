// proxy_hooks_human.cpp - proxy hooks of the human-game paths the headless host never calls (issue 2503, epic 2500
// item e). Hand-written; the generator (proxy/gen/gen_runtime.py) finds the ProxyHook_/EngHook_ definitions by name.
// The server halves: server/src/remote/srv_remote_human.cpp (ServerHook_*) and the RemoteGen subclasses in
// server/src/remote/srv_remote_objects.cpp. Wire formats are documented at each hook (CALL = what the caller sends
// after the self handle, RETURN = what comes back); payload encoding ipc/PROTOCOL.md 5.
//
// DLL call sites and implementations cited as [C] are read in the DLL fork at the pinned commit e9d0c4837. What the
// EXE does with a value is not known from source; such statements are marked [I].
#include "proxy_rt_internal.h"
#include "proxy_ifaces.gen.h"
#include <FireWorks/FIGameIniParser.h>
#include <new>
#include <string.h>
#include <vector>

using namespace ProxyRtInt;

namespace
{
const uint32_t kMaxNodes = 4096;     // units on one plot / selected cities: far above any game
const uint32_t kMaxTable = 1024;     // static tables (8 and 91 entries at the pin)

class CsLock
{
public:
	explicit CsLock(CRITICAL_SECTION& cs) : m_cs(cs) { EnterCriticalSection(&m_cs); }
	~CsLock() { LeaveCriticalSection(&m_cs); }

private:
	CsLock(const CsLock&);
	CsLock& operator=(const CsLock&);
	CRITICAL_SECTION& m_cs;
};

CRITICAL_SECTION* Cs()
{
	// Created on first use (no static constructor order to rely on); never deleted.
	static CRITICAL_SECTION* volatile s_pcs = NULL;
	if (!s_pcs)
	{
		CRITICAL_SECTION* p = new CRITICAL_SECTION;
		InitializeCriticalSection(p);
		if (InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(&s_pcs), p, NULL) != NULL)
		{
			DeleteCriticalSection(p);
			delete p;
		}
	}
	return s_pcs;
}

// Logs the first 20 occurrences of a named condition, then every 1000th.
void LogSome(long& counter, const char* fmt, const char* what)
{
	const long n = InterlockedIncrement(&counter);
	if (n <= 20 || n % 1000 == 0) Log(fmt, what, n);
}

// ---------------------------------------------------------------------------------------------- unit node snapshots
// ICvPlot1::HeadUnitNode / NextUnitNode: the engine walks a plot's unit list by IDInfo* nodes that point into the
// DLL's list [C CvDllPlot.cpp:339-352 -> CvPlot::headUnitNode/nextUnitNode]. A pointer into the server cannot cross,
// so HeadUnitNode fetches the whole list once (one call) and returns a pointer into a proxy-side snapshot;
// NextUnitNode is answered from the snapshot that contains the node (no call). The snapshot is the list as it was at
// HeadUnitNode: the engine reads it in one walk on one thread [I]. 64 snapshots are kept (ring), so 63 newer walks
// may start before a walk's nodes go stale; a node from an evicted snapshot is answered NULL (logged, counted).
struct NodeSnap
{
	IDInfo* p;
	uint32_t n;
};
const uint32_t kSnaps = 64;
NodeSnap s_snaps[kSnaps];
uint32_t s_nextSnap = 0;
long s_lStaleNodes = 0;

IDInfo* StoreSnap(const std::vector<IDInfo>& v)
{
	if (v.empty()) return NULL;
	IDInfo* p = new IDInfo[v.size()];
	for (size_t i = 0; i < v.size(); ++i) p[i] = v[i];
	CsLock l(*Cs());
	NodeSnap& s = s_snaps[s_nextSnap];
	s_nextSnap = (s_nextSnap + 1) % kSnaps;
	delete[] s.p;
	s.p = p;
	s.n = (uint32_t)v.size();
	return p;
}

const IDInfo* NextInSnap(const IDInfo* node)
{
	if (!node) return NULL;
	{
		CsLock l(*Cs());
		for (uint32_t i = 0; i < kSnaps; ++i)
		{
			const NodeSnap& s = s_snaps[i];
			if (s.p && node >= s.p && node < s.p + s.n) return node + 1 < s.p + s.n ? node + 1 : NULL;
		}
	}
	LogSome(s_lStaleNodes, "%s: a node that is in no snapshot (evicted or foreign); answered NULL (%ld)",
	        "ICvPlot1::NextUnitNode");
	return NULL;
}

// Reads "u32 n, n x (i32 eOwner, i32 iID)".
bool ReadNodes(vpipc::Reader& r, std::vector<IDInfo>& v)
{
	const uint32_t n = r.u32();
	if (!r.ok() || n > kMaxNodes) return false;
	v.resize(n);
	for (uint32_t i = 0; i < n; ++i)
	{
		v[i].eOwner = static_cast<PlayerTypes>(r.i32());
		v[i].iID = r.i32();
	}
	return r.ok();
}

// --------------------------------------------------------------------------------------------- static tables (cached)
// The four int tables and the layer names are static data of the DLL [C CvGlobals.h:2669-2673, CvGlobals.cpp:5479]:
// fetched once per process and kept (never freed), so the pointer the engine holds stays valid.
int* s_tables[4] = {NULL, NULL, NULL, NULL};

int* FetchTable(int which, Proxy_ICvGameContext3& self, uint32_t method)
{
	{
		CsLock l(*Cs());
		if (s_tables[which]) return s_tables[which];
	}
	vpipc::Channel& ch = ProxyRt::Chan();
	vpipc::Writer& w = ProxyRt::Begin(ch, method);
	w.handle(self.ProxyHandle());
	vpipc::Reader r;
	if (!ProxyRt::End(ch, method, r)) return NULL;
	const uint32_t n = r.u32();
	if (!r.ok() || n == 0 || n > kMaxTable)
	{
		ProxyRt::DecodeCheck(method, r);
		Log("%s: bad table size %u from the server; answered NULL", vpk_method_key(method), n);
		return NULL;
	}
	int* t = new int[n];
	for (uint32_t i = 0; i < n; ++i) t[i] = r.i32();
	ProxyRt::DecodeCheck(method, r);
	CsLock l(*Cs());
	if (s_tables[which])
	{
		delete[] t;  // another thread fetched it meanwhile
		return s_tables[which];
	}
	s_tables[which] = t;
	Log("%s: %u entries fetched and cached", vpk_method_key(method), n);
	return t;
}

// --------------------------------------------------------------------------------------------- combat member ring
// ICvCombatInfo1::GetDamageMember returns a pointer into the DLL's CvCombatInfo [C CvDllCombatInfo.cpp:127-132]. The
// struct is plain data with the same layout on both sides (36 bytes, model.json records); the copy lives in a ring
// of 256 entries, valid while the engine reads one combat's members [I].
const uint32_t kMembers = 256;
CvCombatMemberEntry s_members[kMembers];
uint32_t s_nextMember = 0;

// ------------------------------------------------------------------------------------------------- loadFromIni
// The parser of the ICvPreGame1::loadFromIni call in progress; its token travels with the CALL, every
// kRtH_IniGetKeyValue names it (a stale or foreign token fails the call).
FIGameIniParser* s_pIni = NULL;
uint32_t s_uIniToken = 0;
long s_lTokens = 0;

// An FString for the engine to write into. FStringA's constructor takes the EXE's empty-string data
// (FStringA_GetStringInitData, defined below); the engine allocates the value [I]. The object is never destroyed:
// FStringA's destructor would free that block through the proxy's operator delete[], which is not known to be the
// allocator the engine used. Leaks a few bytes per string key, once per loadFromIni (15 keys at the pin).
const char* ReadEngineString(FIGameIniParser* ini, const char* group, const char* key, const char* desc,
                             const char* def, bool present, bool& result)
{
	if (!present)
	{
		result = ini->GetKeyValue(group, key, static_cast<FString*>(NULL), desc, def);
		return "";
	}
	void* mem = ::operator new(sizeof(FString));
	FString* s = new (mem) FString();
	result = ini->GetKeyValue(group, key, s, desc, def);
	const char* v = s->GetCString();
	return v ? v : "";
}
}  // namespace

// The EXE exports FStringA_GetStringInitData; the DLL's FireWorks library reaches it through
// GetProcAddress(GetModuleHandle(NULL), ...) (as recorded in tools/x64-dll/closedlib-stubs/FireWorks/FExeExports.cpp).
// The proxy does not link FireWorks, so FString's inline constructor finds it here. Declared by FString.h (FXS_IS_DLL).
FStringA::FStringAData* FStringA_GetStringInitData()
{
	typedef FStringA::FStringAData*(__cdecl * Fn)();
	static Fn s_fn = NULL;
	if (!s_fn)
	{
		s_fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleA(NULL), "FStringA_GetStringInitData"));
		if (!s_fn)
		{
			Log("FATAL: the host executable does not export FStringA_GetStringInitData (needed by loadFromIni)");
			LogFlush();
			abort();
		}
	}
	return s_fn();
}

// ===================================================================================================== engine -> DLL

// ICvPlot1::HeadUnitNode(). CALL: self. RETURN: u32 n (<= 4096), n x (i32 eOwner, i32 iID): the plot's unit list in
// order (ServerHook_ICvPlot1_HeadUnitNode). Returns a pointer to the first node of a new snapshot, NULL when empty.
IDInfo* ProxyHook_ICvPlot1_HeadUnitNode(Proxy_ICvPlot1& self)
{
	vpipc::Channel& ch = ProxyRt::Chan();
	vpipc::Writer& w = ProxyRt::Begin(ch, kM_ICvPlot1_HeadUnitNode);
	w.handle(self.ProxyHandle());
	vpipc::Reader r;
	if (!ProxyRt::End(ch, kM_ICvPlot1_HeadUnitNode, r)) return NULL;
	std::vector<IDInfo> v;
	const bool ok = ReadNodes(r, v);
	ProxyRt::DecodeCheck(kM_ICvPlot1_HeadUnitNode, r);
	return ok ? StoreSnap(v) : NULL;
}

// ICvPlot1::NextUnitNode(IDInfo*) and the const overload: no call; the next node of the snapshot holding pNode.
IDInfo* ProxyHook_ICvPlot1_NextUnitNode_s50(Proxy_ICvPlot1& self, IDInfo* pNode)
{
	(void)self;
	return const_cast<IDInfo*>(NextInSnap(pNode));
}
const IDInfo* ProxyHook_ICvPlot1_NextUnitNode_s51(const Proxy_ICvPlot1& self, const IDInfo* pNode)
{
	(void)self;
	return NextInSnap(pNode);
}

// ICvGameContext1 static tables. CALL: self. RETURN: u32 n (1..1024), n x i32 (ServerHook_*: 8 direction entries,
// NUM_DIRECTION_TYPES + 2 [C CvGlobals.h:2669]; 91 city plot entries, MAX_CITY_PLOTS at MAX_CITY_RADIUS 5 [C
// CvDefines.h:18-19]). Fetched once, cached for the process.
int* ProxyHook_ICvGameContext3_GetPlotDirectionX(Proxy_ICvGameContext3& self)
{
	return FetchTable(0, self, kM_ICvGameContext3_GetPlotDirectionX);
}
int* ProxyHook_ICvGameContext3_GetPlotDirectionY(Proxy_ICvGameContext3& self)
{
	return FetchTable(1, self, kM_ICvGameContext3_GetPlotDirectionY);
}
int* ProxyHook_ICvGameContext3_GetCityPlotX(Proxy_ICvGameContext3& self)
{
	return FetchTable(2, self, kM_ICvGameContext3_GetCityPlotX);
}
int* ProxyHook_ICvGameContext3_GetCityPlotY(Proxy_ICvGameContext3& self)
{
	return FetchTable(3, self, kM_ICvGameContext3_GetCityPlotY);
}

// ICvGameContext1::GetHexDebugLayerNames(): a NULL-terminated static array of names [C CvGlobals.cpp:5479-5494].
// CALL: self. RETURN: u32 n (<= 1024), n x str. Fetched once; the copy is NULL-terminated and never freed.
const char** ProxyHook_ICvGameContext3_GetHexDebugLayerNames(Proxy_ICvGameContext3& self)
{
	static const char** s_names = NULL;
	{
		CsLock l(*Cs());
		if (s_names) return s_names;
	}
	const uint32_t m = kM_ICvGameContext3_GetHexDebugLayerNames;
	vpipc::Channel& ch = ProxyRt::Chan();
	vpipc::Writer& w = ProxyRt::Begin(ch, m);
	w.handle(self.ProxyHandle());
	vpipc::Reader r;
	if (!ProxyRt::End(ch, m, r)) return NULL;
	const uint32_t n = r.u32();
	if (!r.ok() || n > kMaxTable)
	{
		ProxyRt::DecodeCheck(m, r);
		return NULL;
	}
	const char** names = new const char*[n + 1];
	for (uint32_t i = 0; i < n; ++i)
	{
		const char* s = r.cstr();
		names[i] = _strdup(s ? s : "");
	}
	names[n] = NULL;
	ProxyRt::DecodeCheck(m, r);
	CsLock l(*Cs());
	if (!s_names) s_names = names;  // a second fetch by another thread is leaked (once)
	return s_names;
}

// ICvGameContext3::Debug_GetHeap(): the DLL's private heap [C CvDllContext.cpp:276-279] is in the server; a handle of
// another process means nothing here. Local answer: an empty private heap of the proxy, created once, so a caller that
// inspects it (HeapWalk, HeapValidate) gets a valid empty heap [I: the EXE's use is not known].
HANDLE ProxyHook_ICvGameContext3_Debug_GetHeap(const Proxy_ICvGameContext3& self)
{
	(void)self;
	static HANDLE s_h = NULL;
	CsLock l(*Cs());
	if (!s_h)
	{
		s_h = HeapCreate(0, 0, 0);
		Log("ICvGameContext3::Debug_GetHeap: answered locally with an empty proxy heap %p (the DLL heap is in the server)",
		    s_h);
	}
	return s_h;
}

// ICvCombatInfo1::GetDamageMember(int). CALL: self, i32 index. RETURN: b present, then 36 bytes (the struct, same
// layout on both sides; static_assert here and in the server). Returns a pointer into the ring, NULL when absent.
const CvCombatMemberEntry* ProxyHook_ICvCombatInfo1_GetDamageMember(const Proxy_ICvCombatInfo1& self, int iIndex)
{
	static_assert(sizeof(CvCombatMemberEntry) == 36, "CvCombatMemberEntry layout (model.json records: 36 bytes)");
	const uint32_t m = kM_ICvCombatInfo1_GetDamageMember;
	vpipc::Channel& ch = ProxyRt::Chan();
	vpipc::Writer& w = ProxyRt::Begin(ch, m);
	w.handle(self.ProxyHandle());
	w.i32(iIndex);
	vpipc::Reader r;
	if (!ProxyRt::End(ch, m, r)) return NULL;
	const bool present = r.b();
	uint32_t len = 0;
	const uint8_t* p = present ? r.bytes(&len) : NULL;
	ProxyRt::DecodeCheck(m, r);
	if (!present || !p || len != sizeof(CvCombatMemberEntry)) return NULL;
	CsLock l(*Cs());
	CvCombatMemberEntry* e = &s_members[s_nextMember];
	s_nextMember = (s_nextMember + 1) % kMembers;
	memcpy(e, p, sizeof(CvCombatMemberEntry));
	return e;
}

// ICvCombatInfo1::TEMPGetRawCombatInfo(): a pointer to the DLL's CvCombatInfo [C CvDllCombatInfo.cpp:122-125], a
// 64-bit DLL-private layout the 32-bit EXE cannot read, and no interface method takes it back. Local answer NULL,
// logged on the first call [I: the EXE's use is not known; a NULL check there would skip a debug path].
void* ProxyHook_ICvCombatInfo1_TEMPGetRawCombatInfo(const Proxy_ICvCombatInfo1& self)
{
	(void)self;
	static long s_l = 0;
	if (InterlockedIncrement(&s_l) == 1)
		Log("ICvCombatInfo1::TEMPGetRawCombatInfo: answered NULL locally (DLL-private 64-bit struct); further calls not logged");
	return NULL;
}

// ICvPreGame1::SetDLCAvailable(PlayerTypes, ICvEnumerator*): the engine's list of DLC package infos; the DLL reads only
// GetPackageID of each [C CvDllPreGame.cpp:1103-1121]. The proxy walks the engine's enumerator as the DLL would (its
// own references, released) and sends the ids. CALL: self, i32 player, b list present, u32 n (<= 4096), n x 16-byte
// GUID. RETURN: nothing. The server builds its own enumerator for the DLL (ServerHook_ICvPreGame2_SetDLCAvailable).
void ProxyHook_ICvPreGame2_SetDLCAvailable(Proxy_ICvPreGame2& self, PlayerTypes p, ICvEnumerator* pList)
{
	std::vector<GUID> ids;
	if (pList)
	{
		while (ids.size() < kMaxNodes && pList->MoveNext())
		{
			ICvUnknown* c = pList->GetCurrent();
			if (!c) continue;
			ICvDlcPackageInfo1* pi = c->QueryInterface<ICvDlcPackageInfo1>();
			if (pi)
			{
				ids.push_back(pi->GetPackageID());
				pi->Destroy();
			}
			c->Destroy();
		}
	}
	const uint32_t m = kM_ICvPreGame2_SetDLCAvailable;
	vpipc::Channel& ch = ProxyRt::Chan();
	vpipc::Writer& w = ProxyRt::Begin(ch, m);
	w.handle(self.ProxyHandle());
	w.i32(static_cast<int32_t>(p));
	w.b(pList != NULL);
	w.u32(static_cast<uint32_t>(ids.size()));
	for (size_t i = 0; i < ids.size(); ++i) w.bytes(&ids[i], 16u);
	vpipc::Reader r;
	if (ProxyRt::End(ch, m, r)) ProxyRt::DecodeCheck(m, r);
}

// ICvPreGame1::loadFromIni(FIGameIniParser&): the DLL reads [GAME] and [CONFIG] keys through the parser [C
// CvPreGame.cpp:1356-1440: QuickCombat, GameName, WorldSize, Climate, SeaLevel, Era, GameSpeed, VictoryConditions,
// GameOptions, MaxTurns, EnableMultiplayerAI, SyncRandSeed, MapRandSeed, GameType, Map]. The parser stays here; the
// server gives the DLL a parser whose GetKeyValue calls come back as kRtH_IniGetKeyValue (vp_wire.h) while this
// call is in progress. CALL: self, u32 token. RETURN: nothing.
void ProxyHook_ICvPreGame2_loadFromIni(Proxy_ICvPreGame2& self, FIGameIniParser& iniParser)
{
	const uint32_t m = kM_ICvPreGame2_loadFromIni;
	FIGameIniParser* const pOld = s_pIni;
	const uint32_t uOld = s_uIniToken;
	const uint32_t token = static_cast<uint32_t>(InterlockedIncrement(&s_lTokens));
	s_pIni = &iniParser;
	s_uIniToken = token;
	vpipc::Channel& ch = ProxyRt::Chan();
	vpipc::Writer& w = ProxyRt::Begin(ch, m);
	w.handle(self.ProxyHandle());
	w.u32(token);
	vpipc::Reader r;
	if (ProxyRt::End(ch, m, r)) ProxyRt::DecodeCheck(m, r);
	s_pIni = pOld;
	s_uIniToken = uOld;
}

// ===================================================================================================== DLL -> engine

// ICvUserInterface1::headSelectedCitiesNode(): the engine's selected-cities list, walked here with the engine's own
// head/next [C use: CvGame.cpp:2877-2882, the DLL walks it once and reads each node]. CALL: self. RETURN: u32 n
// (<= 4096), n x (i32 eOwner, i32 iID). The server keeps the list as a snapshot and answers nextSelectedCitiesNode
// from it (RemoteUserInterface, srv_remote_objects.cpp), so the two next overloads never cross.
bool EngHook_ICvUserInterface2_headSelectedCitiesNode(ICvUserInterface2* self, vpipc::Reader& args, vpipc::Writer& ret,
                                                      vpipc::Channel& ch)
{
	if (!args.ok())
		return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "ICvUserInterface1::headSelectedCitiesNode(): bad arguments");
	ICvUserInterface1* ui = self;
	std::vector<IDInfo> v;
	for (IDInfo* p = ui->headSelectedCitiesNode(); p && v.size() < kMaxNodes; p = ui->nextSelectedCitiesNode(p))
		v.push_back(*p);
	ret.u32(static_cast<uint32_t>(v.size()));
	for (size_t i = 0; i < v.size(); ++i)
	{
		ret.i32(static_cast<int32_t>(v[i].eOwner));
		ret.i32(v[i].iID);
	}
	return true;
}

// ICvUserInterface1::GetSelectedUnits(): an enumerator the engine implements; its items are DLL objects (units) the
// engine holds [I], each returned with a reference [C: CvEnumerator::GetCurrent wraps it in CvInterfacePtr]. The
// proxy walks it, hands each item to the server with VpGenGiveDll (the server takes its own DLL reference, the
// engine's is released) and destroys the engine's enumerator. CALL: self. RETURN: u32 n (<= 4096), n x handle (null
// for a NULL item). The server builds its own enumerator over the units (RemoteUserInterface::GetSelectedUnits).
bool EngHook_ICvUserInterface2_GetSelectedUnits(ICvUserInterface2* self, vpipc::Reader& args, vpipc::Writer& ret,
                                                vpipc::Channel& ch)
{
	if (!args.ok())
		return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "ICvUserInterface1::GetSelectedUnits(): bad arguments");
	std::vector<vpipc::Handle> hs;
	ICvEnumerator* e = static_cast<ICvUserInterface1*>(self)->GetSelectedUnits();
	if (e)
	{
		// VpGenGiveDll makes nested calls: every handle is collected before anything is written into the RETURN.
		while (hs.size() < kMaxNodes && e->MoveNext()) hs.push_back(VpGenGiveDll(e->GetCurrent()));
		e->Destroy();
	}
	ret.u32(static_cast<uint32_t>(hs.size()));
	for (size_t i = 0; i < hs.size(); ++i) ret.handle(hs[i]);
	return true;
}

// ICvEngineUtility1::GameplayUnitMoved(ICvUnit1*, const CvPlotIndexVector&): CvPlotIndexVector is FFastVector<int,
// true, c_eMPoolTypeGame> [C ICvDLLUtility.h:47], whose members are T* m_pData, uint m_uiCurrSize, uint m_uiCurrMaxSize
// and nothing else (FFastVector.h:236-238; BREAK_ON_REPEATED_RESIZE is off). The engine only reads it (const&) [C use:
// CvUnit.cpp:29813]; the proxy builds an object of that layout over its own buffer, so no FireWorks allocator is
// involved and nothing is freed by the engine. CALL: self, handle unit (temporary), u32 n (<= 65536), n x i32 plot
// index. RETURN: nothing.
namespace
{
struct PlotIndexVectorView
{
	int* m_pData;
	unsigned int m_uiCurrSize;
	unsigned int m_uiCurrMaxSize;
};
}  // namespace

bool EngHook_ICvEngineUtility4_GameplayUnitMoved(ICvEngineUtility4* self, vpipc::Reader& args, vpipc::Writer& ret,
                                                 vpipc::Channel& ch)
{
	static_assert(sizeof(CvPlotIndexVector) == sizeof(PlotIndexVectorView), "FFastVector layout (12 bytes on x86)");
	const vpipc::Handle hu = args.handle();
	const uint32_t n = args.u32();
	if (!args.ok() || n > 65536u)
		return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "ICvEngineUtility1::GameplayUnitMoved: bad arguments");
	std::vector<int> plots(n ? n : 1u, 0);
	for (uint32_t i = 0; i < n; ++i) plots[i] = args.i32();
	if (!args.ok())
		return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "ICvEngineUtility1::GameplayUnitMoved: bad arguments");
	ICvUnit1* unit = static_cast<ICvUnit1*>(static_cast<ICvUnit2*>(ProxyRt::WrapTemp(hu)));
	if (hu.type != 0u && !unit)
		return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadHandle, "ICvEngineUtility1::GameplayUnitMoved: bad handle argument");
	PlotIndexVectorView view;
	view.m_pData = n ? &plots[0] : NULL;
	view.m_uiCurrSize = n;
	view.m_uiCurrMaxSize = n;
	static_cast<ICvEngineUtility1*>(self)->GameplayUnitMoved(unit, *reinterpret_cast<const CvPlotIndexVector*>(&view));
	return true;
}

// ============================================================================================ served runtime methods
// VpHumanServe: the human-game runtime methods (vp_wire.h kRtTypeServerToProxyHuman), reached through the generated
// EngDispatchGenerated.
bool VpHumanServe(uint32_t method, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	switch (method)
	{
	case vpw::kRtH_IniGetKeyValue:
	{
		const uint32_t token = args.u32();
		const uint32_t type = args.u32();
		const char* group = args.cstr();
		const char* key = args.cstr();
		const char* desc = args.cstr();
		const char* sdef = NULL;
		int32_t idef = 0;
		uint32_t udef = 0;
		float fdef = 0.0f;
		switch (type)
		{
		case vpw::kIniString: sdef = args.cstr(); break;
		case vpw::kIniInt: idef = args.i32(); break;
		case vpw::kIniUInt: udef = args.u32(); break;
		case vpw::kIniFloat: fdef = args.f32(); break;
		default: return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "kRtH_IniGetKeyValue: unknown value type");
		}
		const bool present = args.b();
		if (!args.ok())
			return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "kRtH_IniGetKeyValue: bad arguments");
		FIGameIniParser* ini = s_pIni;
		if (!ini || token != s_uIniToken)
			return ProxyRt::DispatchFail(ch, ret, vpw::kStatusFailed, "kRtH_IniGetKeyValue: no loadFromIni call with this token");
		bool result = false;
		switch (type)
		{
		case vpw::kIniString:
		{
			const char* v = ReadEngineString(ini, group, key, desc, sdef, present, result);
			ret.b(result);
			if (present) ret.str(v);
			break;
		}
		case vpw::kIniInt:
		{
			int v = idef;
			result = ini->GetKeyValue(group, key, present ? &v : static_cast<int*>(NULL), desc, idef);
			ret.b(result);
			if (present) ret.i32(v);
			break;
		}
		case vpw::kIniUInt:
		{
			uint v = udef;
			result = ini->GetKeyValue(group, key, present ? &v : static_cast<uint*>(NULL), desc, udef);
			ret.b(result);
			if (present) ret.u32(v);
			break;
		}
		default:
		{
			float v = fdef;
			result = ini->GetKeyValue(group, key, present ? &v : static_cast<float*>(NULL), desc, fdef);
			ret.b(result);
			if (present) ret.f32(v);
			break;
		}
		}
		return true;
	}
	default:
		return false;
	}
}
