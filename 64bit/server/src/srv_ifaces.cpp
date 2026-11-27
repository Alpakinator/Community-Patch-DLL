// The engine-side objects the server hands to the DLL (issues 1501, 1502).
//
// Every virtual of ICvEngineUtility4, ICvUserInterface2, ICvEngineScriptSystem1, FIFileSystem, FILogFileMgr and
// FILogFile is a generated override (host_ifaces.gen.h, tools/x64-dll/smoke-host/gen_ifaces.py, from the fork's
// headers: typed, slots from the compiler) that counts its call and returns zero / null / empty. The methods written
// here are the services the EXE gives the DLL, from research/sources/civ5-headless/engine-contract.md:
//   lock and thread role (1.2, 1.3, 2), net loopback (1.4, 1.5, 6.4), turn signalling (6.3), new-game answers (5.3),
//   Lua (10), logging. Each override marks itself "implemented" so the report separates services from stubs.
#include "srv_common.h"
#include "host_ifaces.gen.h"
#include "srv_tempheap.h"
#include "remote/srv_remote_savemods.h"
#include <FireWorks/FTempHeap.h>
#include <stdio.h>
#include <stdarg.h>
#include <deque>
#include <functional>

#define HIT(iface, m) do { HostIfaceHit(iface, m); } while(0)

// ---------------------------------------------------------------------------------------------------------
// Slot decoding (MS-ABI vcall thunks; see tools/x64-dll/smoke-host/src/host_ifaces.cpp)

int HostSlotFromThunk(const void* pThunk)
{
	const unsigned char* p = static_cast<const unsigned char*>(pThunk);
	for(int hop = 0; hop < 4 && p && p[0] == 0xE9; ++hop)
	{
		int rel;
		memcpy(&rel, p + 1, 4);
		p = p + 5 + rel;
	}
	if(!p)
		return -1;
	for(int i = 0; i < 24; ++i)
	{
		if(p[i] != 0x48 || p[i + 1] != 0x8B || p[i + 2] != 0x01)
			continue;
		const unsigned char* q = p + i + 3;
		unsigned char modrm;
		if(q[0] == 0x48 && q[1] == 0x8B && (q[2] == 0x00 || q[2] == 0x40 || q[2] == 0x80))
			modrm = q[2], q += 3;
		else if(q[0] == 0xFF && (q[1] == 0x20 || q[1] == 0x60 || q[1] == 0xA0))
			modrm = q[1] - 0x20, q += 2;
		else
			return -1;
		if(modrm == 0x00)
			return 0;
		if(modrm == 0x40)
			return static_cast<signed char>(q[0]) / 8;
		int d;
		memcpy(&d, q, 4);
		return d / 8;
	}
	return -1;
}

// ---------------------------------------------------------------------------------------------------------
// Call accounting

namespace
{
	volatile long s_alCalls[HI_COUNT][300];
	bool s_abImpl[HI_COUNT][300];
}
static void LogFirstUnserved(int iIface, int iMethod);

// Issue 9007: a dropped call must never pass unnoticed. A method without a server override logs its name once, on
// its first call; every call is counted and the report lists them under "engine calls not served by the server".
void HostIfaceHit(int iIface, int iMethod)
{
	if(InterlockedIncrement(&s_alCalls[iIface][iMethod]) == 1 && !s_abImpl[iIface][iMethod])
		LogFirstUnserved(iIface, iMethod);
}
long HostIfaceCalls(int iIface, int iMethod) { return s_alCalls[iIface][iMethod]; }
bool HostIfaceImplemented(int iIface, int iMethod) { return s_abImpl[iIface][iMethod]; }
void HostIfaceMarkImplemented(int iIface, int iMethod) { s_abImpl[iIface][iMethod] = true; }

// ---------------------------------------------------------------------------------------------------------
// Game-core lock (engine-contract 1.3): a real recursive lock; HasGameCoreLock = this thread's depth > 0.
// IsGameCoreThread (1.2): the role, not the thread: true only while the core thread is inside Update.

namespace
{
	CRITICAL_SECTION s_csCore;
	__declspec(thread) int t_iLockDepth = 0;
	__declspec(thread) bool t_bInUpdate = false;
	ICvGameContext1* s_pContext = NULL;
	volatile bool s_bDone = false;
	long s_lUpdateIndex = 0;
	long s_lAICivsProcessedAt = -1;
	bool s_bLoadedGame = false;
}

void SrvSetLoadedGame(bool b) { s_bLoadedGame = b; }

void SrvLockInit() { InitializeCriticalSection(&s_csCore); }
void SrvSetCoreThreadUpdating(bool b) { t_bInUpdate = b; }
void SrvSetContext(ICvGameContext1* p) { s_pContext = p; }
void SrvNoteUpdateIndex(long l) { s_lUpdateIndex = l; }
void SrvSetDone(bool b) { s_bDone = b; }

// ---------------------------------------------------------------------------------------------------------
// Net loopback (engine-contract 6.4; issue 9007). A send* with a Response* is queued with its arguments and delivered
// in order, on the main thread, after the current top-level call (Update, SendPlayerOptions, arming) returns and before
// the next Update. The handler is the DLL's ICvNetMessageHandler3 (VP implements it; it derives from 1 and 2).

namespace
{
	typedef std::function<void(ICvNetMessageHandler3*)> NetResponse;
	CRITICAL_SECTION s_csNet;
	bool s_bNetInit = false;
	std::deque<std::pair<std::string, NetResponse> > s_netQueue;
	std::map<std::string, long> s_netCounts;	// "queued <name>", "undelivered <name>"
	SrvNetStats s_netStats = { 0, 0, 0 };

	void NetInit()
	{
		if(!s_bNetInit)
		{
			InitializeCriticalSection(&s_csNet);
			s_bNetInit = true;
		}
	}
	void NetQueue(const char* szName, NetResponse f)
	{
		NetInit();
		EnterCriticalSection(&s_csNet);
		s_netQueue.push_back(std::make_pair(std::string(szName), f));
		++s_netCounts[std::string("queued ") + szName];
		++s_netStats.lQueued;
		LeaveCriticalSection(&s_csNet);
	}
	PlayerTypes ActivePlayer()
	{
		if(!s_pContext)
			return NO_PLAYER;
		ICvGame1* pGame = s_pContext->GetGame();
		const PlayerTypes e = pGame ? pGame->GetActivePlayer() : NO_PLAYER;
		if(pGame)
			pGame->Destroy();
		return e;
	}
	// IsPlayerConnected: a local game connects exactly the slots a person has taken (the 32-bit reference's measured
	// answer: true for the human slot only).
	bool PlayerSlotTaken(PlayerTypes ePlayer)
	{
		if(!s_pContext || ePlayer < 0 || ePlayer >= MAX_PLAYERS)
			return false;
		ICvPreGame1* p = s_pContext->GetPreGame();
		if(!p)
			return false;
		const bool b = p->slotStatus(ePlayer) == SS_TAKEN;
		p->Destroy();
		return b;
	}
	// The nth (0-based) declaration of an engine utility method name in the generated table (overloads that are not
	// adjacent, e.g. ICvEngineUtility2::sendPurchase).
	int EngineOverloadIndex(const char* szName, int nth)
	{
		for(int k = 0; k < HostEngineUtility_NUM_METHODS; ++k)
			if(!strcmp(s_aHostEngineUtilityMethods[k].szName, szName) && nth-- == 0)
				return k;
		return 0;
	}
}

size_t SrvNetDeliver()
{
	NetInit();
	size_t n = 0;
	for(;;)
	{
		EnterCriticalSection(&s_csNet);
		if(s_netQueue.empty())
		{
			LeaveCriticalSection(&s_csNet);
			break;
		}
		std::pair<std::string, NetResponse> item = s_netQueue.front();
		s_netQueue.pop_front();
		LeaveCriticalSection(&s_csNet);
		ICvNetMessageHandler1* pH1 = s_pContext ? s_pContext->GetNetMessageHandler() : NULL;
		ICvNetMessageHandler3* pH = pH1 ? pH1->QueryInterface<ICvNetMessageHandler3>() : NULL;
		EnterCriticalSection(&s_csNet);
		if(pH)
			++s_netStats.lDelivered;
		else
		{
			++s_netStats.lUndelivered;
			++s_netCounts[std::string("undelivered ") + item.first];
		}
		LeaveCriticalSection(&s_csNet);
		if(pH)
			item.second(pH);
		else
			SrvLog("net loopback: the DLL's net message handler has no ICvNetMessageHandler3; %s dropped", item.first.c_str());
		++n;
	}
	return n;
}

SrvNetStats SrvNetGetStats() { return s_netStats; }
std::string SrvNetSummary()
{
	std::string s;
	char sz[160];
	for(std::map<std::string, long>::const_iterator it = s_netCounts.begin(); it != s_netCounts.end(); ++it)
	{
		sprintf_s(sz, "%s%s %ld", s.empty() ? "" : ", ", it->first.c_str(), it->second);
		s += sz;
	}
	return s.empty() ? "none" : s;
}

// ---------------------------------------------------------------------------------------------------------
// Player options (engine-contract 1.4): the PlayerOptions rows' defaults, plus PLAYEROPTION_SAFE_AUTOMATION = true
// (the 32-bit host's reference profile; server/evidence/1505/newgame-sequence.md).

namespace
{
	const int kEngineKnownPlayerOptions = 11;	// the EXE's own player options: PlayerOptions rows 0..10 (issue 2402)

	bool PlayerOptionValue(PlayerOptionTypes eHash)
	{
		if(!s_pContext)
			return false;
		const int idx = s_pContext->GetInfoTypeForHash((uint)eHash, true);
		if(idx < 0)
			return false;
		ICvPlayerOptionInfo1* pInfo = s_pContext->GetPlayerOptionInfo((PlayerOptionTypes)idx);
		if(!pInfo)
			return false;
		bool b = pInfo->GetDefault();
		pInfo->Destroy();
		std::string type;
		Database::Connection* pDb = SrvDb();
		if(pDb)
		{
			Database::Results* pr = new Database::Results();
			if(pDb->Execute(*pr, "SELECT Type FROM PlayerOptions WHERE ID = ?"))
			{
				pr->Bind(1, idx);
				if(pr->Step())
					type = pr->GetText(0);
			}
			delete pr;
		}
		if(type == "PLAYEROPTION_SAFE_AUTOMATION")
			b = true;
		return b;
	}
}

// ---------------------------------------------------------------------------------------------------------
// Script system

class SrvScriptSystem : public HostScriptSystemGen
{
public:
	lua_State* CreateLuaThread(_In_z_ const char* szName) override
	{
		HIT(HI_SCRIPT, HM_HostScriptSystem_CreateLuaThread);
		return LuaCreateThread(szName);
	}
	void FreeLuaThread(_In_opt_ lua_State* L) override
	{
		HIT(HI_SCRIPT, HM_HostScriptSystem_FreeLuaThread);
		LuaFreeThread(L);
	}
	bool LoadFile(_In_ lua_State* L, _In_z_ const char* szFilename) override
	{
		HIT(HI_SCRIPT, HM_HostScriptSystem_LoadFile);
		return LuaLoadFile(L, szFilename);
	}
	ICvEngineScriptSystemArgs1* CreateArgs() override
	{
		HIT(HI_SCRIPT, HM_HostScriptSystem_CreateArgs);
		return LuaCreateArgs();
	}
	ICvEngineScriptSystemArgs1* CreateArgs(uint) override
	{
		HIT(HI_SCRIPT, HM_HostScriptSystem_CreateArgs + 1);
		return LuaCreateArgs();
	}
	void DestroyArgs(_In_opt_ ICvEngineScriptSystemArgs1* p) override
	{
		HIT(HI_SCRIPT, HM_HostScriptSystem_DestroyArgs);
		LuaDestroyArgs(p);
	}
	bool CallCFunction(_In_ lua_State* L, lua_CFunction func, void* args) override
	{
		HIT(HI_SCRIPT, HM_HostScriptSystem_CallCFunction);
		if(lua_cpcall(L, func, args) != 0)
		{
			SrvLog("CallCFunction error: %s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "?");
			LuaCountError("CallCFunction", lua_tostring(L, -1));
			lua_pop(L, 1);
			return false;
		}
		return true;
	}
	bool CallFunction(_In_ lua_State* L, unsigned int nArgs, unsigned int nRet) override
	{
		HIT(HI_SCRIPT, HM_HostScriptSystem_CallFunction);
		if(LuaPCall(L, (int)nArgs, (int)nRet) != 0)
		{
			SrvLog("CallFunction error: %s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "?");
			LuaCountError("CallFunction", lua_tostring(L, -1));
			lua_pop(L, 1);
			return false;
		}
		return true;
	}
	bool CallHook(_In_z_ const char* szName, _In_opt_ ICvEngineScriptSystemArgs1* args, bool& value) override
	{
		HIT(HI_SCRIPT, HM_HostScriptSystem_CallHook);
		return LuaDispatch(0, szName, args, &value, NULL, NULL);
	}
	bool CallAccumulator(_In_z_ const char* szName, _In_opt_ ICvEngineScriptSystemArgs1* args, int& value) override
	{
		HIT(HI_SCRIPT, HM_HostScriptSystem_CallAccumulator);
		return LuaDispatch(3, szName, args, NULL, &value, NULL);
	}
	bool CallAccumulator(_In_z_ const char* szName, _In_opt_ ICvEngineScriptSystemArgs1* args, float& value) override
	{
		HIT(HI_SCRIPT, HM_HostScriptSystem_CallAccumulator + 1);
		return LuaDispatch(4, szName, args, NULL, NULL, &value);
	}
	bool CallTestAll(_In_z_ const char* szName, _In_opt_ ICvEngineScriptSystemArgs1* args, bool& value) override
	{
		HIT(HI_SCRIPT, HM_HostScriptSystem_CallTestAll);
		return LuaDispatch(1, szName, args, &value, NULL, NULL);
	}
	bool CallTestAny(_In_z_ const char* szName, _In_opt_ ICvEngineScriptSystemArgs1* args, bool& value) override
	{
		HIT(HI_SCRIPT, HM_HostScriptSystem_CallTestAny);
		return LuaDispatch(2, szName, args, &value, NULL, NULL);
	}
};

// ---------------------------------------------------------------------------------------------------------
// Engine utility

#define E(name) HM_HostEngineUtility_##name
#define EHIT(name) HIT(HI_ENGINE, E(name))

class SrvEngineUtility : public HostEngineUtilityGen
{
public:
	void* DLLCALL QueryInterface(GUID guid) override
	{
		EHIT(QueryInterface);
		if(IsEqualGUID(guid, guidICvUnknown) || IsEqualGUID(guid, guidICvEngineUtility1) ||
		   IsEqualGUID(guid, guidICvEngineUtility2) || IsEqualGUID(guid, guidICvEngineUtility3) ||
		   IsEqualGUID(guid, guidICvEngineUtility4))
			return static_cast<ICvEngineUtility4*>(this);
		return NULL;
	}
	void DLLCALL Destroy() override { EHIT(Destroy); }
	ICvEngineScriptSystem1* GetScriptSystem() override { EHIT(GetScriptSystem); return SrvScriptSystem(); }
	const char* GetCacheFolderPath() override { EHIT(GetCacheFolderPath); return SrvCacheFolder(); }

	// Lock and thread role
	void GetGameCoreLock() override { EHIT(GetGameCoreLock); EnterCriticalSection(&s_csCore); ++t_iLockDepth; }
	void ReleaseGameCoreLock() override
	{
		EHIT(ReleaseGameCoreLock);
		if(t_iLockDepth > 0)
		{
			--t_iLockDepth;
			LeaveCriticalSection(&s_csCore);
		}
	}
	bool TryGameCoreLock() override
	{
		EHIT(TryGameCoreLock);
		if(!TryEnterCriticalSection(&s_csCore))
			return false;
		++t_iLockDepth;
		return true;
	}
	bool HasGameCoreLock() override { EHIT(HasGameCoreLock); return t_iLockDepth > 0; }
	bool IsGameCoreThread() override { EHIT(IsGameCoreThread); return t_bInUpdate; }
	bool IsGameCoreExecuting() override { EHIT(IsGameCoreExecuting); return false; }

	// New game (5.3)
	bool DLLCALL GetEvaluatedMapScriptPath(const char* szPath, char* szOut, unsigned int cch) const override
	{
		HIT(HI_ENGINE, E(GetEvaluatedMapScriptPath));
		if(!szPath || !szOut || cch == 0)
			return false;
		strncpy_s(szOut, cch, szPath, _TRUNCATE);
		return true;
	}
	bool getPlayerOption(PlayerOptionTypes e) override { EHIT(getPlayerOption); return PlayerOptionValue(e); }
	bool CanAdvanceTurn() override { EHIT(CanAdvanceTurn); return true; }
	bool GetDone() override { EHIT(GetDone); return s_bDone; }
	void SetDone(bool b) override { EHIT(SetDone); s_bDone = b; }
	bool GetAutorun() override { EHIT(GetAutorun); return false; }
	int GetAudioTagIndex(const char*, int) override { EHIT(GetAudioTagIndex); return -1; }
	bool ShouldValidateGameDatabase() const override { HIT(HI_ENGINE, E(ShouldValidateGameDatabase)); return false; }
	void netMessageDebugLog(const std::string&) const override { HIT(HI_ENGINE, E(netMessageDebugLog)); }

	// Turn signalling (6.3)
	void SendAICivsProcessed() const override
	{
		HIT(HI_ENGINE, E(SendAICivsProcessed));
		s_lAICivsProcessedAt = s_lUpdateIndex;
	}
	bool allAICivsProcessedThisTurn() const override
	{
		HIT(HI_ENGINE, E(allAICivsProcessedThisTurn));
		return s_lAICivsProcessedAt >= 0 && s_lUpdateIndex > s_lAICivsProcessedAt;
	}
	void PublishNewGameTurn(int) override { EHIT(PublishNewGameTurn); s_lAICivsProcessedAt = -1; }

	// Single-player host answers (issue 9007 audit, server-host.md section 11): the EXE of a local game is the host and
	// its own player's slot is connected; Civ4's 240 turn slices per minute (getMinutesPlayed divides by it when a game
	// ends; the generated 0 would divide by zero at any victory).
	bool IsHost() const override { HIT(HI_ENGINE, E(IsHost)); return true; }
	bool IsPlayerConnected(PlayerTypes ePlayer) override { EHIT(IsPlayerConnected); return PlayerSlotTaken(ePlayer); }
	int getTurnsPerMinute() override { EHIT(getTurnsPerMinute); return 240; }

	// Net loopback (6.4): every send* the DLL can call that has a Response* is queued and delivered between top-level
	// calls (issue 9007). The sender of a message without a player argument is the local player, i.e. the active
	// player when the message is sent. Not looped back, left to the generated stubs (counted and reported): sends
	// without a Response* (sendPlayerInfo, sendTurnComplete, sync checks, chat, ...) and sendNetDealAccepted /
	// sendNetDemandAccepted (human only; the EXE's translation into ResponsePlayerDealFinalized is not known).
	void sendPlayerOption(PlayerOptionTypes eOption, bool bValue) override
	{
		EHIT(sendPlayerOption);	// the active player at delivery (epic 1500, unchanged)
		// Issue 2402: the EXE answers with its own option index (= PlayerOptions row ID) for the 11 options it knows
		// (the base game's rows 0..10) and with the type hash for any other row (civ5-headless trace S3b, host/engine/
		// HHEngineServices.cpp sendPlayerOption; a real-game save holds keys 0..10 and -542730216 in m_aOptions). With
		// the hash for every row a loaded game's option list grew from 12 to 23 entries.
		const int idx = s_pContext ? s_pContext->GetInfoTypeForHash((uint)eOption, true) : -1;
		const PlayerOptionTypes eDelivered = (idx >= 0 && idx < kEngineKnownPlayerOptions) ? (PlayerOptionTypes)idx : eOption;
		NetQueue("PlayerOption", [eDelivered, bValue](ICvNetMessageHandler3* h) { h->ResponsePlayerOption(ActivePlayer(), eDelivered, bValue); });
	}
	void sendMinorPledgeProtection(PlayerTypes eMajor, PlayerTypes eMinor, bool bProtect, bool bBroken) override
	{
		EHIT(sendMinorPledgeProtection);
		NetQueue("PledgeMinorProtection", [=](ICvNetMessageHandler3* h) { h->ResponsePledgeMinorProtection(eMajor, eMinor, bProtect, bBroken); });
	}
	void sendMinorBullyGold(PlayerTypes eBully, PlayerTypes eMinor, int iGold) override
	{
		EHIT(sendMinorBullyGold);
		NetQueue("MinorCivBullyGold", [=](ICvNetMessageHandler3* h) { h->ResponseMinorCivBullyGold(eBully, eMinor, iGold); });
	}
	void sendMinorBullyUnit(PlayerTypes eBully, PlayerTypes eMinor, UnitTypes eUnit) override
	{
		EHIT(sendMinorBullyUnit);
		NetQueue("MinorCivBullyUnit", [=](ICvNetMessageHandler3* h) { h->ResponseMinorCivBullyUnit(eBully, eMinor, eUnit); });
	}
	void sendMinorGiftTileImprovement(PlayerTypes eMajor, PlayerTypes eMinor, int x, int y) override
	{
		EHIT(sendMinorGiftTileImprovement);
		NetQueue("MinorCivGiftTileImprovement", [=](ICvNetMessageHandler3* h) { h->ResponseMinorCivGiftTileImprovement(eMajor, eMinor, x, y); });
	}
	void sendMinorBuyout(PlayerTypes eMajor, PlayerTypes eMinor) override
	{
		EHIT(sendMinorBuyout);
		NetQueue("MinorCivBuyout", [=](ICvNetMessageHandler3* h) { h->ResponseMinorCivBuyout(eMajor, eMinor); });
	}
	void sendMinorGiftGold(PlayerTypes eMinor, int iGold) override
	{
		EHIT(sendMinorGiftGold);
		const PlayerTypes eFrom = ActivePlayer();
		NetQueue("MinorCivGiftGold", [=](ICvNetMessageHandler3* h) { h->ResponseMinorCivGiftGold(eFrom, eMinor, iGold); });
	}
	// VP's NetMessageExt multiplexes its own messages through this pair (CvDllNetMessageExt.cpp, last argument
	// 999-1006: event choices, minor bully annex, vassal tax, liberate vassal, deal renewal); the AI sends the vassal
	// tax, the liberation and the bully annex (epic 2200: x86 and x64 differed from turn 354 of the huge save without it).
	void sendMoveGreatWorks(PlayerTypes ePlayer, int i1, int i2, int i3, int i4, int i5, int i6) override
	{
		EHIT(sendMoveGreatWorks);
		NetQueue("MoveGreatWorks", [=](ICvNetMessageHandler3* h) { h->ResponseMoveGreatWorks(ePlayer, i1, i2, i3, i4, i5, i6); });
	}
	void sendSetSwappableGreatWork(PlayerTypes ePlayer, int iClass, int iIndex) override
	{
		EHIT(sendSetSwappableGreatWork);
		NetQueue("SetSwappableGreatWork", [=](ICvNetMessageHandler3* h) { h->ResponseSetSwappableGreatWork(ePlayer, iClass, iIndex); });
	}
	void sendSwapGreatWorks(PlayerTypes e1, int i1, PlayerTypes e2, int i2) override
	{
		EHIT(sendSwapGreatWorks);
		NetQueue("SwapGreatWorks", [=](ICvNetMessageHandler3* h) { h->ResponseSwapGreatWorks(e1, i1, e2, i2); });
	}
	void sendDoTask(int iCity, TaskTypes eTask, int i1, int i2, bool bOption, bool bAlt, bool bShift, bool bCtrl) override
	{
		EHIT(sendDoTask);
		const PlayerTypes eFrom = ActivePlayer();
		NetQueue("CityDoTask", [=](ICvNetMessageHandler3* h) { h->ResponseCityDoTask(eFrom, iCity, eTask, i1, i2, bOption, bAlt, bShift, bCtrl); });
	}
	void sendLaunch(PlayerTypes ePlayer, VictoryTypes eVictory) override
	{
		EHIT(sendLaunch);
		NetQueue("LaunchSpaceship", [=](ICvNetMessageHandler3* h) { h->ResponseLaunchSpaceship(ePlayer, eVictory); });
	}
	void SendFaithPurchase(PlayerTypes ePlayer, FaithPurchaseTypes eType, int iIndex) override
	{
		EHIT(SendFaithPurchase);
		NetQueue("FaithPurchase", [=](ICvNetMessageHandler3* h) { h->ResponseFaithPurchase(ePlayer, eType, iIndex); });
	}
	void SendFoundPantheon(PlayerTypes ePlayer, BeliefTypes eBelief) override
	{
		EHIT(SendFoundPantheon);
		NetQueue("FoundPantheon", [=](ICvNetMessageHandler3* h) { h->ResponseFoundPantheon(ePlayer, eBelief); });
	}
	void sendGreatPersonChoice(PlayerTypes ePlayer, UnitTypes eUnit) override
	{
		EHIT(sendGreatPersonChoice);
		NetQueue("GreatPersonChoice", [=](ICvNetMessageHandler3* h) { h->ResponseGreatPersonChoice(ePlayer, eUnit); });
	}
	void sendFaithGreatPersonChoice(PlayerTypes ePlayer, UnitTypes eUnit) override
	{
		EHIT(sendFaithGreatPersonChoice);
		NetQueue("FaithGreatPersonChoice", [=](ICvNetMessageHandler3* h) { h->ResponseFaithGreatPersonChoice(ePlayer, eUnit); });
	}
	void sendMayaBonusChoice(PlayerTypes ePlayer, UnitTypes eUnit) override
	{
		EHIT(sendMayaBonusChoice);
		NetQueue("MayaBonusChoice", [=](ICvNetMessageHandler3* h) { h->ResponseMayaBonusChoice(ePlayer, eUnit); });
	}
	void sendGoodyChoice(PlayerTypes ePlayer, int x, int y, GoodyTypes eGoody, int iUnit) override
	{
		EHIT(sendGoodyChoice);
		NetQueue("GoodyChoice", [=](ICvNetMessageHandler3* h) { h->ResponseGoodyChoice(ePlayer, x, y, eGoody, iUnit); });
	}
	// Selection and city-screen messages of the local player (CvGame::selectionListGameNetMessage and friends).
	void sendPushMission(int iUnit, MissionTypes eMission, int i1, int i2, int iFlags, bool bShift) override
	{
		EHIT(sendPushMission);
		const PlayerTypes eFrom = ActivePlayer();
		NetQueue("PushMission", [=](ICvNetMessageHandler3* h) { h->ResponsePushMission(eFrom, iUnit, eMission, i1, i2, iFlags, bShift); });
	}
	void sendSwapUnits(int iUnit, MissionTypes eMission, int i1, int i2, int iFlags, bool bShift) override
	{
		EHIT(sendSwapUnits);
		const PlayerTypes eFrom = ActivePlayer();
		NetQueue("SwapUnits", [=](ICvNetMessageHandler3* h) { h->ResponseSwapUnits(eFrom, iUnit, eMission, i1, i2, iFlags, bShift); });
	}
	void sendAutoMission(int iUnit) override
	{
		EHIT(sendAutoMission);
		const PlayerTypes eFrom = ActivePlayer();
		NetQueue("AutoMission", [=](ICvNetMessageHandler3* h) { h->ResponseAutoMission(eFrom, iUnit); });
	}
	void sendDoCommand(int iUnit, CommandTypes eCommand, int i1, int i2, bool bAlt) override
	{
		EHIT(sendDoCommand);
		const PlayerTypes eFrom = ActivePlayer();
		NetQueue("DoCommand", [=](ICvNetMessageHandler3* h) { h->ResponseDoCommand(eFrom, iUnit, eCommand, i1, i2, bAlt); });
	}
	void sendPushOrder(int iCity, OrderTypes eOrder, int iData, bool bAlt, bool bShift, bool bCtrl) override
	{
		EHIT(sendPushOrder);
		const PlayerTypes eFrom = ActivePlayer();
		NetQueue("CityPushOrder", [=](ICvNetMessageHandler3* h) { h->ResponseCityPushOrder(eFrom, iCity, eOrder, iData, bAlt, bShift, bCtrl); });
	}
	void sendPopOrder(int iCity, int iNum) override
	{
		EHIT(sendPopOrder);
		const PlayerTypes eFrom = ActivePlayer();
		NetQueue("CityPopOrder", [=](ICvNetMessageHandler3* h) { h->ResponseCityPopOrder(eFrom, iCity, iNum); });
	}
	void sendSwapOrder(int iCity, int iNum) override
	{
		EHIT(sendSwapOrder);
		const PlayerTypes eFrom = ActivePlayer();
		NetQueue("CitySwapOrder", [=](ICvNetMessageHandler3* h) { h->ResponseCitySwapOrder(eFrom, iCity, iNum); });
	}
	void sendPurchase(int iCity, UnitTypes eUnit, BuildingTypes eBuilding, ProjectTypes eProject) override
	{
		HIT(HI_ENGINE, E(sendPurchase));
		const PlayerTypes eFrom = ActivePlayer();
		NetQueue("CityPurchase", [=](ICvNetMessageHandler3* h) {
			static_cast<ICvNetMessageHandler1*>(h)->ResponseCityPurchase(eFrom, iCity, eUnit, eBuilding, eProject);	// H2 hides the H1 overload
		});
	}
	void sendPurchase(int iCity, UnitTypes eUnit, BuildingTypes eBuilding, ProjectTypes eProject, int iYield) override
	{
		static const int s_iIndex = EngineOverloadIndex("sendPurchase", 1);	// ICvEngineUtility2's overload
		HIT(HI_ENGINE, s_iIndex);
		const PlayerTypes eFrom = ActivePlayer();
		NetQueue("CityPurchaseYield", [=](ICvNetMessageHandler3* h) { h->ResponseCityPurchase(eFrom, iCity, eUnit, eBuilding, eProject, iYield); });
	}
	void sendFromUIDiploEvent(PlayerTypes eOther, FromUIDiploEventTypes eEvent, int i1, int i2) override
	{
		EHIT(sendFromUIDiploEvent);
		const PlayerTypes eFrom = ActivePlayer();
		NetQueue("DiplomacyFromUI", [=](ICvNetMessageHandler3* h) { h->ResponseDiplomacyFromUI(eFrom, eOther, eEvent, i1, i2); });
	}
	void sendResearch(TechTypes eTech, int iDiscover, PlayerTypes eStealFrom, bool bShift) override
	{
		EHIT(sendResearch);
		const PlayerTypes eFrom = ActivePlayer();
		NetQueue("Research", [=](ICvNetMessageHandler3* h) { h->ResponseResearch(eFrom, eTech, iDiscover, eStealFrom, bShift); });
	}
	void sendChangeWar(TeamTypes eTeam, bool bWar) override
	{
		EHIT(sendChangeWar);
		const PlayerTypes eFrom = ActivePlayer();
		NetQueue("ChangeWar", [=](ICvNetMessageHandler3* h) { h->ResponseChangeWar(eFrom, eTeam, bWar); });
	}
	void sendUpdatePolicies(bool bNotBranch, int iId, bool bValue) override
	{
		EHIT(sendUpdatePolicies);
		const PlayerTypes eFrom = ActivePlayer();
		NetQueue("UpdatePolicies", [=](ICvNetMessageHandler3* h) { h->ResponseUpdatePolicies(eFrom, bNotBranch, iId, bValue); });
	}
	void SendLeagueVoteEnact(LeagueTypes eLeague, int iId, PlayerTypes eVoter, int iVotes, int iChoice) override
	{
		EHIT(SendLeagueVoteEnact);
		NetQueue("LeagueVoteEnact", [=](ICvNetMessageHandler3* h) { h->ResponseLeagueVoteEnact(eLeague, iId, eVoter, iVotes, iChoice); });
	}
	void SendLeagueVoteRepeal(LeagueTypes eLeague, int iId, PlayerTypes eVoter, int iVotes, int iChoice) override
	{
		EHIT(SendLeagueVoteRepeal);
		NetQueue("LeagueVoteRepeal", [=](ICvNetMessageHandler3* h) { h->ResponseLeagueVoteRepeal(eLeague, iId, eVoter, iVotes, iChoice); });
	}
	void SendLeagueVoteAbstain(LeagueTypes eLeague, PlayerTypes eVoter, int iVotes) override
	{
		EHIT(SendLeagueVoteAbstain);
		NetQueue("LeagueVoteAbstain", [=](ICvNetMessageHandler3* h) { h->ResponseLeagueVoteAbstain(eLeague, eVoter, iVotes); });
	}
	void SendLeagueProposeEnact(LeagueTypes eLeague, ResolutionTypes eResolution, PlayerTypes eProposer, int iChoice) override
	{
		EHIT(SendLeagueProposeEnact);
		NetQueue("LeagueProposeEnact", [=](ICvNetMessageHandler3* h) { h->ResponseLeagueProposeEnact(eLeague, eResolution, eProposer, iChoice); });
	}
	void SendLeagueProposeRepeal(LeagueTypes eLeague, int iId, PlayerTypes eProposer) override
	{
		EHIT(SendLeagueProposeRepeal);
		NetQueue("LeagueProposeRepeal", [=](ICvNetMessageHandler3* h) { h->ResponseLeagueProposeRepeal(eLeague, iId, eProposer); });
	}
};

// ---------------------------------------------------------------------------------------------------------
// User interface: everything generated (nothing selected, no screens), except the identity methods.

class SrvUserInterface : public HostUserInterfaceGen
{
public:
	void* DLLCALL QueryInterface(GUID guid) override
	{
		HIT(HI_UI, HM_HostUserInterface_QueryInterface);
		if(IsEqualGUID(guid, guidICvUnknown) || IsEqualGUID(guid, guidICvUserInterface1) ||
		   IsEqualGUID(guid, guidICvUserInterface2))
			return static_cast<ICvUserInterface2*>(this);
		return NULL;
	}
	void DLLCALL Destroy() override { HIT(HI_UI, HM_HostUserInterface_Destroy); }
	bool IsLoadedGame() const override { HIT(HI_UI, HM_HostUserInterface_IsLoadedGame); return s_bLoadedGame; }
};

// ---------------------------------------------------------------------------------------------------------
// File system: generated (the DLL does not use it in these runs). Log files: real files under <run>\Logs for the
// server's own few logs; the DLL's AI logs are written only with --dll-logs (they are large and change nothing in
// the game: the logging flags are set the same either way).

// FIFileSystem::Create: a real file, only inside the run folder. Needed by CvGame::readSaveGameDB, which writes the
// save's embedded Civ5SavedGameDatabase.db into the cache folder through FFILESYSTEM.Create(path, modeWrite) while a
// save is loaded; with the generated NULL answer the embedded database is not consumed and every later section of the
// body is read misaligned (CvMap::InitPlots bad_alloc, found 2026-11-10). Everything else stays generated.
//
// Ticket 0488 (2026-11-17): the check compares normalised paths (runfolder::Inside below), and a write create that is
// refused or fails is fatal: CvGame::readSaveGameDB (the DLL's only FIFileSystem::Create caller at fork 04b1211b8) only
// asserts when the create returns NULL and leaves the database's bytes unread, so CvMap::Serialize reads the map's width
// and height from the SQLite header ("SQLi", "te f": 1,766,609,235 x 1,713,399,156, 252,352,668 plots as a 32-bit int)
// and InitPlots asks for new CvPlot[252,352,668] (456 B each, 115 GB) before it fails in bad_alloc. A run_dir with '/'
// separators did exactly that on 2026-11-17 (131 GB private on the workstation). Knowledge base:
// research/03-dll-engine-boundary/server-host.md section 18.

// BEGIN 0488 RUN FOLDER (server/tests/runfolder/run_runfolder_test.py compiles this block on its own: keep it
// self-contained, <windows.h> and the standard library only)
namespace runfolder
{
// The form two paths are compared in: absolute (GetFullPathNameW: '/' becomes '\', "." and ".." resolved, repeated
// separators collapsed), every 8.3 short name expanded (GetLongPathNameW on the longest prefix that exists, so a file
// that is about to be created keeps its own last parts), no trailing separator. Case is left as it is: Inside compares
// without case, as the file system does (CompareStringOrdinal, bIgnoreCase). False for an empty or unusable path.
inline bool Normalise(const wchar_t* in, std::wstring& out)
{
	out.clear();
	if(!in || !*in)
		return false;
	const DWORD nFull = GetFullPathNameW(in, 0, NULL, NULL);
	if(!nFull)
		return false;
	std::wstring full(nFull, L'\0');
	const DWORD nGot = GetFullPathNameW(in, nFull, &full[0], NULL);
	if(!nGot || nGot >= nFull)
		return false;
	full.resize(nGot);
	// Expand short names: the longest existing prefix through GetLongPathNameW, the parts after it as they are. The
	// first separator after the root ("C:\" or "\\server\share\") is never cut off, so "C:" alone (a drive-relative
	// path) is never asked for.
	size_t nRoot = 0;
	if(full.size() >= 3 && full[1] == L':' && full[2] == L'\\')
		nRoot = 3;
	else if(full.size() >= 2 && full[0] == L'\\' && full[1] == L'\\')
	{
		const size_t a = full.find(L'\\', 2);
		const size_t b = a == std::wstring::npos ? std::wstring::npos : full.find(L'\\', a + 1);
		nRoot = b == std::wstring::npos ? full.size() : b + 1;
	}
	std::wstring head = full, tail;
	for(;;)
	{
		const DWORD nLong = GetLongPathNameW(head.c_str(), NULL, 0);
		if(nLong)
		{
			std::wstring lng(nLong, L'\0');
			const DWORD k = GetLongPathNameW(head.c_str(), &lng[0], nLong);
			if(k && k < nLong)
			{
				lng.resize(k);
				head = lng;
				break;
			}
		}
		const size_t p = head.find_last_of(L'\\');
		if(p == std::wstring::npos || p < nRoot)
			break;
		tail = head.substr(p) + tail;
		head.resize(p);
	}
	out = head + tail;
	while(out.size() > 1 && out[out.size() - 1] == L'\\')
		out.resize(out.size() - 1);
	return !out.empty();
}

// True when `file` lies inside the folder `root` (not the folder itself), both normalised first. A root that cannot be
// normalised accepts nothing.
inline bool Inside(const wchar_t* file, const wchar_t* root)
{
	std::wstring f, r;
	if(!Normalise(file, f) || !Normalise(root, r))
		return false;
	return f.size() > r.size() && f[r.size()] == L'\\' &&
	       CompareStringOrdinal(f.c_str(), (int)r.size(), r.c_str(), (int)r.size(), TRUE) == CSTR_EQUAL;
}
}  // namespace runfolder
// END 0488 RUN FOLDER

class SrvFile : public FIFile
{
public:
	SrvFile() : m_h(INVALID_HANDLE_VALUE) { m_wsz[0] = 0; }
	~SrvFile() { Close(); }
	FileErr Open(const char* sz, OpenMode e, dword, uint) override
	{
		wchar_t w[MAX_PATH];
		MultiByteToWideChar(CP_UTF8, 0, sz ? sz : "", -1, w, MAX_PATH);
		return Open(w, e, 0, 0);
	}
	FileErr Open(const wchar* wsz, OpenMode e, dword, uint) override
	{
		Close();
		m_dwError = 0;
		if(!wsz || !InsideRun(wsz))
			return FILE_BADNAME;
		wcsncpy_s(m_wsz, wsz, _TRUNCATE);
		const DWORD acc = e == modeRead ? GENERIC_READ : e == modeWrite ? GENERIC_WRITE : GENERIC_READ | GENERIC_WRITE;
		const DWORD disp = e == modeRead ? OPEN_EXISTING : e == modeWrite ? CREATE_ALWAYS : OPEN_ALWAYS;
		m_h = CreateFileW(wsz, acc, FILE_SHARE_READ, NULL, disp, FILE_ATTRIBUTE_NORMAL, NULL);
		if(m_h == INVALID_HANDLE_VALUE)
			m_dwError = GetLastError();
		return m_h == INVALID_HANDLE_VALUE ? FILE_NOTFOUND : FILE_OK;
	}
	DWORD LastError() const { return m_dwError; }
	FileErr Close() override
	{
		if(m_h != INVALID_HANDLE_VALUE)
			CloseHandle(m_h);
		m_h = INVALID_HANDLE_VALUE;
		return FILE_OK;
	}
	const wchar_t* GetFileName() override { return m_wsz; }
	byte* GetViewCaret() const override { return NULL; }
	byte* GetViewEnd() const override { return NULL; }
	byte* GetViewPtr() const override { return NULL; }
	uint GetLength() const override { return m_h == INVALID_HANDLE_VALUE ? 0 : (uint)GetFileSize(m_h, NULL); }
	void SetLength(uint size) override
	{
		LARGE_INTEGER li;
		li.QuadPart = size;
		SetFilePointerEx(m_h, li, NULL, FILE_BEGIN);
		SetEndOfFile(m_h);
	}
	bool IsOpen() const override { return m_h != INVALID_HANDLE_VALUE; }
	bool IsMapped() const override { return false; }
	uint Seek(long l, SeekMode e) override
	{
		return SetFilePointer(m_h, l, NULL, e == begin ? FILE_BEGIN : e == current ? FILE_CURRENT : FILE_END);
	}
	uint SeekToBegin() override { return Seek(0, begin); }
	uint SeekToEnd() override { return Seek(0, end); }
	uint GetPosition() override { return SetFilePointer(m_h, 0, NULL, FILE_CURRENT); }
	void Rewind() override { Seek(0, begin); }
	bool IsEOF() override { return GetPosition() >= GetLength(); }
	uint Read(void* p, uint n) override
	{
		DWORD got = 0;
		return ReadFile(m_h, p, n, &got, NULL) ? got : 0;
	}
	uint Write(const void* p, uint n) override
	{
		DWORD put = 0;
		return WriteFile(m_h, p, n, &put, NULL) ? put : 0;
	}
	uint Flush() override { FlushFileBuffers(m_h); return 0; }

private:
	// Ticket 0488: both sides normalised (runfolder::Inside). Before, only the file went through GetFullPathNameW and
	// the root was compared as written, so a run_dir with '/' separators refused every file inside it.
	static bool InsideRoot(const wchar_t* wsz, const char* szRoot)
	{
		wchar_t root[MAX_PATH];
		if(!szRoot || !*szRoot || !MultiByteToWideChar(CP_ACP, 0, szRoot, -1, root, MAX_PATH))
			return false;
		return runfolder::Inside(wsz, root);
	}
	static bool InsideRun(const wchar_t* wsz)
	{
		return InsideRoot(wsz, SrvRunDir()) || InsideRoot(wsz, s_szExtraWriteRoot);
	}
public:
	static char s_szExtraWriteRoot[MAX_PATH];
private:
	HANDLE m_h;
	DWORD m_dwError = 0;
	wchar_t m_wsz[MAX_PATH];
};

// Ticket 0488: a write create that is refused or fails. The first reason is kept for the headless load
// (SrvFileCreateFailure, checked by srv_save.cpp right after Game::Read); remote mode installs a hook that ends the server
// through its report (srv_remote_main.cpp: ReportAndExit, the D118 path).
namespace
{
	std::string s_strCreateFailure;
	SrvFileFailFn s_pfnCreateFail = NULL;

	void CreateFailed(const wchar_t* wsz, FIFile::OpenMode e, FileErr err, DWORD dwError)
	{
		char szPath[MAX_PATH * 3] = "";
		WideCharToMultiByte(CP_UTF8, 0, wsz ? wsz : L"", -1, szPath, sizeof(szPath), NULL, NULL);
		char szWhy[MAX_PATH * 3 + 512];
		if(err == FILE_BADNAME)
			sprintf_s(szWhy, "FIFileSystem::Create(\"%s\") refused: not inside the run folder \"%s\"%s%s%s", szPath,
			          SrvRunDir() ? SrvRunDir() : "", SrvFile::s_szExtraWriteRoot[0] ? " nor the cache folder \"" : "",
			          SrvFile::s_szExtraWriteRoot, SrvFile::s_szExtraWriteRoot[0] ? "\"" : "");
		else
			sprintf_s(szWhy, "FIFileSystem::Create(\"%s\") failed: CreateFileW error %lu", szPath, dwError);
		SrvLog("%s", szWhy);
		if(e == FIFile::modeRead)
			return;	// a read of a file that is not there is an answer, not a failure
		std::string why = std::string(szWhy) +
		                  "; the DLL would leave the save's embedded database unread and read the rest of the body "
		                  "misaligned, so the load stops here (ticket 0488)";
		SrvLog("LOUD: %s", why.c_str());
		if(s_strCreateFailure.empty())
			s_strCreateFailure = why;
		if(s_pfnCreateFail)
			s_pfnCreateFail(why.c_str());
	}
}
const char* SrvFileCreateFailure() { return s_strCreateFailure.c_str(); }
void SrvSetFileCreateFailHook(SrvFileFailFn pfn) { s_pfnCreateFail = pfn; }
char SrvFile::s_szExtraWriteRoot[MAX_PATH] = {0};
void SrvSetExtraWriteRoot(const char* szDir)
{
	if(szDir && strcmp(szDir, SrvFile::s_szExtraWriteRoot))
	{
		strncpy_s(SrvFile::s_szExtraWriteRoot, szDir, _TRUNCATE);
		SrvLog("file system: the engine's cache folder %s is a write root too", szDir);
	}
}

class SrvFileSystem : public HostFileSystemGen
{
public:
	FIFile* Create(const char* sz, FIFile::OpenMode e, dword f, uint u) override
	{
		HIT(HI_FILESYS, HM_HostFileSystem_Create);
		wchar_t w[MAX_PATH];
		MultiByteToWideChar(CP_UTF8, 0, sz ? sz : "", -1, w, MAX_PATH);
		return Make(sz ? w : NULL, e, f, u);
	}
	FIFile* Create(const wchar* wsz, FIFile::OpenMode e, dword f, uint u) override
	{
		HIT(HI_FILESYS, HM_HostFileSystem_Create + 1);
		return Make(wsz, e, f, u);
	}

private:
	static FIFile* Make(const wchar_t* wsz, FIFile::OpenMode e, dword f, uint u)
	{
		SrvFile* p = new SrvFile();
		if(!wsz)
			return p;
		const FileErr err = p->Open(wsz, e, f, u);
		if(err != FILE_OK)
		{
			const DWORD dwError = p->LastError();
			delete p;
			CreateFailed(wsz, e, err, dwError);	// may not return (remote mode's hook)
			return NULL;
		}
		return p;
	}
};

class SrvLogFile : public HostLogFileGen
{
public:
	SrvLogFile(const char* szName, bool bWrite) : m_pFile(NULL)
	{
		strncpy_s(m_szName, szName, _TRUNCATE);
		MultiByteToWideChar(CP_ACP, 0, m_szName, -1, m_wszName, MAX_PATH);
		if(bWrite)
		{
			char szPath[MAX_PATH * 2];
			sprintf_s(szPath, "%s\\Logs\\%s", SrvRunDir(), m_szName);
			fopen_s(&m_pFile, szPath, "w");
		}
	}
	void Msg(const TCHAR* format, ...) override { HIT(HI_LOGFILE, HM_HostLogFile_Msg); if(m_pFile) { va_list v; va_start(v, format); Write("", format, v); va_end(v); } }
	void DebugMsg(const TCHAR* format, ...) override { HIT(HI_LOGFILE, HM_HostLogFile_DebugMsg); if(m_pFile) { va_list v; va_start(v, format); Write("[debug] ", format, v); va_end(v); } }
	void WarningMsg(const TCHAR* format, ...) override { HIT(HI_LOGFILE, HM_HostLogFile_WarningMsg); if(m_pFile) { va_list v; va_start(v, format); Write("[warning] ", format, v); va_end(v); } }
	void ErrorMsg(const TCHAR* format, ...) override { HIT(HI_LOGFILE, HM_HostLogFile_ErrorMsg); if(m_pFile) { va_list v; va_start(v, format); Write("[error] ", format, v); va_end(v); } }
	LPCWSTR GetFileName() const override { HIT(HI_LOGFILE, HM_HostLogFile_GetFileName); return m_wszName; }
	const char* Name() const { return m_szName; }
	void CloseFile()
	{
		if(m_pFile)
			fclose(m_pFile);
		m_pFile = NULL;
	}

private:
	void Write(const char* szPrefix, const char* szFormat, va_list v)
	{
		if(!szFormat)
			return;
		const bool bWatch = !_stricmp(m_szName, "custommods.log");
		char sz[2048];
		if(bWatch)
		{
			va_list c;
			va_copy(c, v);
			vsnprintf(sz, sizeof(sz), szFormat, c);
			va_end(c);
		}
		fputs(szPrefix, m_pFile);
		vfprintf(m_pFile, szFormat, v);
		fputc('\n', m_pFile);
		// ticket 0681: the DLL's save data hash mismatch (CvGame::Read) is told to the player (srv_remote_savemods.h);
		// ticket 0744: after the line is in the file, since its first "Deserialization Error" ends the server here
		// (remote mode; the end closes this file)
		if(bWatch)
			SrvSaveMods::OnDllLog(m_szName, sz);
	}
	FILE* m_pFile;
	char m_szName[MAX_PATH];
	wchar_t m_wszName[MAX_PATH];
};

class SrvLogFileMgr : public HostLogFileMgrGen
{
public:
	FILogFile* GetLog(LPCWSTR wszFileName, uint uiFlags, const TCHAR* szTitle = NULL) override
	{
		HIT(HI_LOGMGR, HM_HostLogFileMgr_GetLog);
		char sz[MAX_PATH] = "";
		if(wszFileName)
			WideCharToMultiByte(CP_ACP, 0, wszFileName, -1, sz, MAX_PATH, NULL, NULL);
		return Get(sz, uiFlags, szTitle);
	}
	FILogFile* GetLog(LPCSTR szFileName, uint uiFlags, const TCHAR* szTitle = NULL) override
	{
		HIT(HI_LOGMGR, HM_HostLogFileMgr_GetLog + 1);
		return Get(szFileName ? szFileName : "", uiFlags, szTitle);
	}
	bool IsLoggingEnabled() override { HIT(HI_LOGMGR, HM_HostLogFileMgr_IsLoggingEnabled); return true; }
	void CloseAll()
	{
		EnterCriticalSection(&m_cs);
		for(std::map<std::string, SrvLogFile*>::iterator it = m_logs.begin(); it != m_logs.end(); ++it)
			it->second->CloseFile();
		LeaveCriticalSection(&m_cs);
	}
	SrvLogFileMgr() { InitializeCriticalSection(&m_cs); }

private:
	FILogFile* Get(const char* szName, uint, const char*)
	{
		char szSafe[MAX_PATH];
		strncpy_s(szSafe, szName, _TRUNCATE);
		for(char* p = szSafe; *p; ++p)
		{
			if(*p == '\\' || *p == '/' || *p == ':')
				*p = '_';
			*p = (char)tolower((unsigned char)*p);
		}
		EnterCriticalSection(&m_cs);
		std::map<std::string, SrvLogFile*>::iterator it = m_logs.find(szSafe);
		SrvLogFile* p = NULL;
		if(it != m_logs.end())
			p = it->second;
		else
		{
			// Always written: the DLL's error and database logs; everything else only with --dll-logs.
			const bool bWrite = SrvKeepDllLogs() || !_stricmp(szSafe, "cvassert.log") || !_stricmp(szSafe, "database.log") ||
			                    !_stricmp(szSafe, "gamecore.log") || !_stricmp(szSafe, "custommods.log") || !_stricmp(szSafe, "xml.log");
			p = new SrvLogFile(szSafe, bWrite);
			m_logs[szSafe] = p;
		}
		LeaveCriticalSection(&m_cs);
		return p;
	}
public:
	// Issue 2800: the log files the DLL asked for (its AI and other logs open only while the DLL's logging flags, set by
	// the EXE's ICvGameContext1::SetLogging..., are on), whether each is written (dll_logs or an always-written log).
	void ReportRequests(FILE* f)
	{
		EnterCriticalSection(&m_cs);
		unsigned nWritten = 0;
		std::string names;
		for(std::map<std::string, SrvLogFile*>::const_iterator it = m_logs.begin(); it != m_logs.end(); ++it)
		{
			const bool w = SrvKeepDllLogs() || !_stricmp(it->first.c_str(), "cvassert.log") || !_stricmp(it->first.c_str(), "database.log") ||
			               !_stricmp(it->first.c_str(), "gamecore.log") || !_stricmp(it->first.c_str(), "custommods.log") || !_stricmp(it->first.c_str(), "xml.log");
			nWritten += w ? 1 : 0;
			if(names.size() < 1500)
				names += (names.empty() ? "" : ", ") + it->first + (w ? "" : " (not written)");
		}
		fprintf(f, "dll logs: the DLL asked for %u log files, %u of them written (dll_logs=%d): %s\n", (unsigned)m_logs.size(), nWritten,
		        SrvKeepDllLogs() ? 1 : 0, names.c_str());
		LeaveCriticalSection(&m_cs);
	}

private:
	CRITICAL_SECTION m_cs;
	std::map<std::string, SrvLogFile*> m_logs;
};

// ---------------------------------------------------------------------------------------------------------
// Instances and tables

static SrvEngineUtility s_engine;
static SrvUserInterface s_ui;
static class SrvScriptSystem s_script;
static SrvFileSystem s_fileSystem;
static SrvLogFileMgr s_logMgr;

void* HostGetFileSystemPtr() { return static_cast<FIFileSystem*>(&s_fileSystem); }
void* HostGetLogFileMgrPtr() { return static_cast<FILogFileMgr*>(&s_logMgr); }
void SrvLogReport(FILE* f) { s_logMgr.ReportRequests(f); }  // issue 2800

struct TempHeapLayout : TempHeap
{
	static constexpr size_t MemHead() { return offsetof(TempHeapLayout, m_pMemHead); }
	static constexpr size_t Offset() { return offsetof(TempHeapLayout, m_uiCurrentOffset); }
	static constexpr size_t Capacity() { return offsetof(TempHeapLayout, m_uiCapacity); }
	static constexpr size_t Flags() { return offsetof(TempHeapLayout, m_dwFlags); }
};
static_assert(TempHeapLayout::MemHead() == offsetof(HostTempHeap, pMemHead), "TempHeap layout");
static_assert(TempHeapLayout::Offset() == offsetof(HostTempHeap, lCurrentOffset), "TempHeap layout");
static_assert(TempHeapLayout::Capacity() == offsetof(HostTempHeap, uiCapacity), "TempHeap layout");
static_assert(TempHeapLayout::Flags() == offsetof(HostTempHeap, dwFlags), "TempHeap layout");
static_assert(sizeof(TempHeap) == sizeof(HostTempHeap), "TempHeap size");

ICvEngineUtility4* SrvEngine() { return &s_engine; }
ICvUserInterface2* SrvUI() { return &s_ui; }
ICvEngineScriptSystem1* SrvScriptSystem() { return &s_script; }
void SrvLogFilesClose() { s_logMgr.CloseAll(); }

static int s_aiEngineSlots[HostEngineUtility_NUM_METHODS];
static int s_aiUISlots[HostUserInterface_NUM_METHODS];
static int s_aiScriptSlots[HostScriptSystem_NUM_METHODS];
static int s_aiFileSysSlots[HostFileSystem_NUM_METHODS];
static int s_aiLogMgrSlots[HostLogFileMgr_NUM_METHODS];
static int s_aiLogFileSlots[HostLogFile_NUM_METHODS];

static const HostIfaceInfo s_aIfaces[HI_COUNT] = {
	{ "ICvEngineUtility4", s_aHostEngineUtilityMethods, HostEngineUtility_NUM_METHODS, s_aiEngineSlots },
	{ "ICvUserInterface2", s_aHostUserInterfaceMethods, HostUserInterface_NUM_METHODS, s_aiUISlots },
	{ "ICvEngineScriptSystem1", s_aHostScriptSystemMethods, HostScriptSystem_NUM_METHODS, s_aiScriptSlots },
	{ "FIFileSystem", s_aHostFileSystemMethods, HostFileSystem_NUM_METHODS, s_aiFileSysSlots },
	{ "FILogFileMgr", s_aHostLogFileMgrMethods, HostLogFileMgr_NUM_METHODS, s_aiLogMgrSlots },
	{ "FILogFile", s_aHostLogFileMethods, HostLogFile_NUM_METHODS, s_aiLogFileSlots },
};
static_assert(HostEngineUtility_NUM_METHODS <= 300 && HostUserInterface_NUM_METHODS <= 300, "call table size");

const HostIfaceInfo& HostGetIface(int iIface) { return s_aIfaces[iIface]; }

static void LogFirstUnserved(int iIface, int iMethod)
{
	const HostMethodInfo& m = s_aIfaces[iIface].pMethods[iMethod];
	SrvLog("engine call not served by the server (generated stub, answers zero/NULL): %s::%s, first call; counted in server-report.txt",
	       m.szClass, m.szName);
}

// A method is "implemented" when the server's override is not the generated one: its slot's vtable entry differs
// from the generated class's.
template<class Gen, class Impl> static void MarkOverrides(int iIface, const Gen& gen, const Impl& impl, int nSlots)
{
	void* const* vg = *reinterpret_cast<void* const* const*>(&gen);
	void* const* vi = *reinterpret_cast<void* const* const*>(&impl);
	const HostIfaceInfo& f = s_aIfaces[iIface];
	for(int k = 0; k < f.nMethods; ++k)
		if(f.piSlots[k] >= 0 && f.piSlots[k] < nSlots && vg[f.piSlots[k]] != vi[f.piSlots[k]])
			HostIfaceMarkImplemented(iIface, k);
}

namespace
{
	// Instances of the pure generated classes, only to read their vtables.
	struct GenEngine : HostEngineUtilityGen
	{
		void* DLLCALL QueryInterface(GUID) override { return NULL; }
		void DLLCALL Destroy() override {}
		ICvEngineScriptSystem1* GetScriptSystem() override { return NULL; }
		const char* GetCacheFolderPath() override { return NULL; }
	};
	struct GenScript : HostScriptSystemGen
	{
		lua_State* CreateLuaThread(const char*) override { return NULL; }
		void FreeLuaThread(lua_State*) override {}
	};
}

int SrvResolveSlots()
{
	HostEngineUtilityResolveSlots<SrvEngineUtility>(s_aiEngineSlots);
	HostUserInterfaceResolveSlots<SrvUserInterface>(s_aiUISlots);
	HostScriptSystemResolveSlots<class SrvScriptSystem>(s_aiScriptSlots);
	HostFileSystemResolveSlots<SrvFileSystem>(s_aiFileSysSlots);
	HostLogFileMgrResolveSlots<SrvLogFileMgr>(s_aiLogMgrSlots);
	HostLogFileResolveSlots<SrvLogFile>(s_aiLogFileSlots);
	int nBad = 0;
	for(int i = 0; i < HI_COUNT; ++i)
	{
		const HostIfaceInfo& f = s_aIfaces[i];
		std::vector<int> seen;
		for(int k = 0; k < f.nMethods; ++k)
		{
			if(f.piSlots[k] < 0 || std::find(seen.begin(), seen.end(), f.piSlots[k]) != seen.end())
				++nBad;
			seen.push_back(f.piSlots[k]);
		}
	}
	static GenEngine s_genEngine;
	static GenScript s_genScript;
	MarkOverrides(HI_ENGINE, s_genEngine, s_engine, HostEngineUtility_NUM_METHODS + 1);
	MarkOverrides(HI_SCRIPT, s_genScript, s_script, HostScriptSystem_NUM_METHODS + 1);
	for(int k = 0; k < HostLogFileMgr_NUM_METHODS; ++k)
		HostIfaceMarkImplemented(HI_LOGMGR, k);
	for(int k = 0; k < HostLogFile_NUM_METHODS; ++k)
		HostIfaceMarkImplemented(HI_LOGFILE, k);
	HostIfaceMarkImplemented(HI_UI, HM_HostUserInterface_QueryInterface);
	HostIfaceMarkImplemented(HI_UI, HM_HostUserInterface_Destroy);
	HostIfaceMarkImplemented(HI_UI, HM_HostUserInterface_IsLoadedGame);
	HostIfaceMarkImplemented(HI_FILESYS, HM_HostFileSystem_Create);
	HostIfaceMarkImplemented(HI_FILESYS, HM_HostFileSystem_Create + 1);
	return nBad;
}
