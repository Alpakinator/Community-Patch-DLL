// srv_remote_human.cpp - server halves of the human-game paths the headless host never calls (issue 2503, epic 2500
// item e): ServerHook_* of engine -> DLL methods the generator leaves non-mechanical, the local enumerators the DLL is
// given, and the remote ini parser of loadFromIni. The proxy halves and the wire formats: proxy/src/
// proxy_hooks_human.cpp (each hook documents its CALL and RETURN). The RemoteGen overrides of the DLL -> engine
// methods (selected cities, selected units, GameplayUnitMoved) are in srv_remote_objects.cpp.
// [C] = read in the DLL fork at the pinned commit e9d0c4837.
#include "srv_remote.h"
#include "vp_ids.gen.h"
#include "server_hooks.gen.h"
#include "vpipc/stl.h"
#include <FireWorks/FIGameIniParser.h>
#include <atomic>
#include <string>
#include <vector>

namespace
{
const uint32_t kMaxNodes = 4096;

// The four static int tables have fixed sizes in the DLL [C CvGlobals.h:2669-2673]: m_aiPlotDirectionX/Y are
// int[NUM_DIRECTION_TYPES + 2], m_aiRingPlotX/YOffset are int[MAX_CITY_PLOTS], MAX_CITY_PLOTS =
// (6 * (1 + MAX_CITY_RADIUS) * MAX_CITY_RADIUS / 2) + 1 with MAX_CITY_RADIUS 5 [C CvDefines.h:18-19] = 91.
const uint32_t kDirectionEntries = NUM_DIRECTION_TYPES + 2;
const uint32_t kCityPlotEntries = (6 * (1 + 5) * 5 / 2) + 1;
static_assert(NUM_DIRECTION_TYPES == 6, "direction table size");

bool SendTable(const int* t, uint32_t n, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch, const char* what)
{
	if(!args.ok())
		return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, what);
	if(!t)
		return SrvRt::DispatchFail(ch, ret, vpw::kStatusFailed, what);
	ret.u32(n);
	for(uint32_t i = 0; i < n; ++i)
		ret.i32(t[i]);
	return true;
}

// A DLC package info object of the server, for the enumerator SetDLCAvailable gives the DLL.
class LocalDlcInfo final : public ICvDlcPackageInfo1
{
public:
	explicit LocalDlcInfo(const GUID& g) : m_g(g), m_refs(1) {}
	void* DLLCALL QueryInterface(GUID guid) override
	{
		if(guid == ICvUnknown::GetInterfaceId() || guid == ICvDlcPackageInfo1::GetInterfaceId())
		{
			++m_refs;
			return this;
		}
		return NULL;
	}
	void DLLCALL Destroy() override
	{
		if(--m_refs == 0)
			delete this;
	}
	GUID DLLCALL GetPackageID() override { return m_g; }

private:
	GUID m_g;
	std::atomic<long> m_refs;
};

// ---- the remote ini parser of loadFromIni: GetKeyValue crosses back to the engine's parser (kRtH_IniGetKeyValue) ----
class RemoteIniParser : public FIGameIniParser
{
public:
	explicit RemoteIniParser(uint32_t token) : m_token(token) {}
	bool GetKeyValue(LPCTSTR szGroupKey, LPCTSTR szKeyValue, FString* pValue, LPCTSTR szKeyDesc, const char* keyDefaultVal) override
	{
		vpipc::Writer& w = Start(vpw::kIniString, szGroupKey, szKeyValue, szKeyDesc);
		w.str(keyDefaultVal);
		w.b(pValue != NULL);
		vpipc::Reader r;
		if(!End(r))
			return false;
		const bool res = r.b();
		if(pValue)
		{
			const std::string v = vpipc::read_string(r);
			if(r.ok())
				*pValue = v.c_str();
		}
		SrvRt::DecodeCheck(vpw::kRtH_IniGetKeyValue, r);
		return res;
	}
	bool GetKeyValue(LPCTSTR szGroupKey, LPCTSTR szKeyValue, int* pValue, LPCTSTR szKeyDesc, int keyDefaultVal) override
	{
		vpipc::Writer& w = Start(vpw::kIniInt, szGroupKey, szKeyValue, szKeyDesc);
		w.i32(keyDefaultVal);
		w.b(pValue != NULL);
		vpipc::Reader r;
		if(!End(r))
			return false;
		const bool res = r.b();
		if(pValue)
		{
			const int32_t v = r.i32();
			if(r.ok())
				*pValue = v;
		}
		SrvRt::DecodeCheck(vpw::kRtH_IniGetKeyValue, r);
		return res;
	}
	bool GetKeyValue(LPCTSTR szGroupKey, LPCTSTR szKeyValue, uint* pValue, LPCTSTR szKeyDesc, uint keyDefaultVal) override
	{
		vpipc::Writer& w = Start(vpw::kIniUInt, szGroupKey, szKeyValue, szKeyDesc);
		w.u32(keyDefaultVal);
		w.b(pValue != NULL);
		vpipc::Reader r;
		if(!End(r))
			return false;
		const bool res = r.b();
		if(pValue)
		{
			const uint32_t v = r.u32();
			if(r.ok())
				*pValue = v;
		}
		SrvRt::DecodeCheck(vpw::kRtH_IniGetKeyValue, r);
		return res;
	}
	bool GetKeyValue(LPCTSTR szGroupKey, LPCTSTR szKeyValue, float* pValue, LPCTSTR szKeyDesc, float keyDefaultVal) override
	{
		vpipc::Writer& w = Start(vpw::kIniFloat, szGroupKey, szKeyValue, szKeyDesc);
		w.f32(keyDefaultVal);
		w.b(pValue != NULL);
		vpipc::Reader r;
		if(!End(r))
			return false;
		const bool res = r.b();
		if(pValue)
		{
			const float v = r.f32();
			if(r.ok())
				*pValue = v;
		}
		SrvRt::DecodeCheck(vpw::kRtH_IniGetKeyValue, r);
		return res;
	}
	// The DLL's loadFromIni calls only GetKeyValue [C CvPreGame.cpp:1356-1440]; the rest is answered locally, logged.
	bool Write(void) override { return Other("Write"); }
	bool WriteIfDirty() override { return Other("WriteIfDirty"); }
	void EnableAutoWriting(bool) override { Other("EnableAutoWriting"); }
	bool IsDirty() const override { return Other("IsDirty"); }
	bool WarnCorrupted() const override { return Other("WarnCorrupted"); }
	void ClearCorruptedWarning() override { Other("ClearCorruptedWarning"); }
	FileErr GetLastFileErr() override
	{
		Other("GetLastFileErr");
		return FileErr();
	}
	bool IsInitialized() override { return true; }

private:
	vpipc::Writer& Start(uint32_t type, LPCTSTR g, LPCTSTR k, LPCTSTR d)
	{
		m_ch = &SrvRt::Chan();
		vpipc::Writer& w = SrvRt::Begin(*m_ch, vpw::kRtH_IniGetKeyValue);
		w.u32(m_token);
		w.u32(type);
		w.str(g);
		w.str(k);
		w.str(d);
		return w;
	}
	bool End(vpipc::Reader& r) { return SrvRt::End(*m_ch, vpw::kRtH_IniGetKeyValue, r); }
	static bool Other(const char* what)
	{
		SrvLog("remote: loadFromIni: FIGameIniParser::%s called by the DLL: answered locally (false / nothing)", what);
		return false;
	}
	uint32_t m_token;
	vpipc::Channel* m_ch = NULL;
};
}  // namespace

// ---- the server's enumerator over DLL objects or local objects (GetSelectedUnits, SetDLCAvailable) ----
namespace
{
class LocalEnumerator final : public ICvEnumerator
{
public:
	explicit LocalEnumerator(std::vector<ICvUnknown*>& items) : m_refs(1), m_i(-1) { m_items.swap(items); }
	// Issue 2801 (verification 2700 A2): every method is an engine method the server answers itself, so each counts as a
	// DLL -> engine call (NoteEngineCall: the engine-call word the mirror checks and the outside-a-served-call counter).
	void* DLLCALL QueryInterface(GUID guid) override
	{
		SrvRemote::NoteEngineCall();
		if(guid == ICvUnknown::GetInterfaceId() || guid == ICvEnumerator::GetInterfaceId())
		{
			++m_refs;
			return this;
		}
		return NULL;
	}
	void DLLCALL Destroy() override
	{
		SrvRemote::NoteEngineCall();
		if(--m_refs != 0)
			return;
		for(ICvUnknown* p : m_items)
			if(p)
				p->Destroy();
		delete this;
	}
	bool DLLCALL MoveNext() override
	{
		SrvRemote::NoteEngineCall();
		if(m_i + 1 >= (int)m_items.size())
		{
			m_i = (int)m_items.size();
			return false;
		}
		++m_i;
		return true;
	}
	void DLLCALL Reset() override
	{
		SrvRemote::NoteEngineCall();
		m_i = -1;
	}
	// A new reference to the current item, as the DLL expects (CvEnumerator::GetCurrent wraps it in CvInterfacePtr).
	ICvUnknown* DLLCALL GetCurrent() override
	{
		SrvRemote::NoteEngineCall();
		if(m_i < 0 || m_i >= (int)m_items.size() || !m_items[m_i])
			return NULL;
		return static_cast<ICvUnknown*>(m_items[m_i]->QueryInterface(ICvUnknown::GetInterfaceId()));
	}

private:
	std::vector<ICvUnknown*> m_items;	// one reference each, released by the last Destroy
	std::atomic<long> m_refs;
	int m_i;
};
}  // namespace

ICvEnumerator* VpNewLocalEnumerator(std::vector<ICvUnknown*>& items)
{
	return new LocalEnumerator(items);
}

// ===================================================================================================== ServerHooks

// ICvPlot1::HeadUnitNode(): CALL self. RETURN u32 n (<= 4096), n x (i32 eOwner, i32 iID), the plot's unit list walked
// with the DLL's own HeadUnitNode / NextUnitNode. The proxy answers NextUnitNode from that snapshot: the two
// NextUnitNode overloads never cross (their server hooks stay loud defaults).
bool ServerHook_ICvPlot1_HeadUnitNode(ICvPlot1* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	if(!args.ok())
		return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "ICvPlot1::HeadUnitNode(): bad arguments");
	std::vector<IDInfo> v;
	for(IDInfo* p = self->HeadUnitNode(); p && v.size() < kMaxNodes; p = self->NextUnitNode(p))
		v.push_back(*p);
	ret.u32((uint32_t)v.size());
	for(const IDInfo& i : v)
	{
		ret.i32((int32_t)i.eOwner);
		ret.i32(i.iID);
	}
	return true;
}

// ICvGameContext1 static tables: CALL self. RETURN u32 n, n x i32.
bool ServerHook_ICvGameContext3_GetPlotDirectionX(ICvGameContext3* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	return SendTable(self->GetPlotDirectionX(), kDirectionEntries, args, ret, ch, "ICvGameContext1::GetPlotDirectionX()");
}
bool ServerHook_ICvGameContext3_GetPlotDirectionY(ICvGameContext3* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	return SendTable(self->GetPlotDirectionY(), kDirectionEntries, args, ret, ch, "ICvGameContext1::GetPlotDirectionY()");
}
bool ServerHook_ICvGameContext3_GetCityPlotX(ICvGameContext3* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	return SendTable(self->GetCityPlotX(), kCityPlotEntries, args, ret, ch, "ICvGameContext1::GetCityPlotX()");
}
bool ServerHook_ICvGameContext3_GetCityPlotY(ICvGameContext3* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	return SendTable(self->GetCityPlotY(), kCityPlotEntries, args, ret, ch, "ICvGameContext1::GetCityPlotY()");
}

// ICvGameContext1::GetHexDebugLayerNames(): CALL self. RETURN u32 n, n x str (the NULL-terminated array, at most 1024).
bool ServerHook_ICvGameContext3_GetHexDebugLayerNames(ICvGameContext3* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	if(!args.ok())
		return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "ICvGameContext1::GetHexDebugLayerNames(): bad arguments");
	const char** names = self->GetHexDebugLayerNames();
	uint32_t n = 0;
	while(names && names[n] && n < 1024)
		++n;
	ret.u32(n);
	for(uint32_t i = 0; i < n; ++i)
		ret.str(names[i]);
	return true;
}

// ICvCombatInfo1::GetDamageMember(int): CALL self, i32 index. RETURN b present, 36 bytes.
bool ServerHook_ICvCombatInfo1_GetDamageMember(ICvCombatInfo1* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	static_assert(sizeof(CvCombatMemberEntry) == 36, "CvCombatMemberEntry layout (model.json records: 36 bytes)");
	const int i = args.i32();
	if(!args.ok())
		return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "ICvCombatInfo1::GetDamageMember(int): bad arguments");
	const CvCombatMemberEntry* e = self->GetDamageMember(i);
	ret.b(e != NULL);
	if(e)
		ret.bytes(e, (uint32_t)sizeof(CvCombatMemberEntry));
	return true;
}

// ICvPreGame1::SetDLCAvailable(PlayerTypes, ICvEnumerator*): CALL self, i32 player, b list present, u32 n, n x GUID.
// The DLL gets a local enumerator of local package infos (one reference, released after the call; the DLL takes its
// own with QueryInterface [C CvDllPreGame.cpp:1108]).
bool ServerHook_ICvPreGame2_SetDLCAvailable(ICvPreGame2* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	const PlayerTypes p = (PlayerTypes)args.i32();
	const bool present = args.b();
	const uint32_t n = args.u32();
	if(!args.ok() || n > kMaxNodes)
		return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "ICvPreGame1::SetDLCAvailable: bad arguments");
	std::vector<ICvUnknown*> items;
	for(uint32_t i = 0; i < n; ++i)
	{
		GUID g;
		memset(&g, 0, sizeof(g));
		uint32_t len = 0;
		const uint8_t* b = args.bytes(&len);
		if(b && len == 16u)
			memcpy(&g, b, 16u);
		items.push_back(new LocalDlcInfo(g));
	}
	if(!args.ok())
	{
		for(ICvUnknown* u : items)
			u->Destroy();
		return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "ICvPreGame1::SetDLCAvailable: bad arguments");
	}
	ICvEnumerator* e = present ? VpNewLocalEnumerator(items) : NULL;
	self->SetDLCAvailable(p, e);
	if(e)
		e->Destroy();
	return true;
}

// ICvPreGame1::loadFromIni(FIGameIniParser&): CALL self, u32 token. The DLL reads through a RemoteIniParser whose
// GetKeyValue calls are kRtH_IniGetKeyValue (vp_wire.h) on this thread's channel, nested in this call.
bool ServerHook_ICvPreGame2_loadFromIni(ICvPreGame2* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	const uint32_t token = args.u32();
	if(!args.ok())
		return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "ICvPreGame1::loadFromIni: bad arguments");
	RemoteIniParser parser(token);
	self->loadFromIni(parser);
	return true;
}
