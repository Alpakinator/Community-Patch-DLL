// proxy_hooks_local.cpp - the hand-written proxy hooks of issue 2700 (local answers). Each hook answers locally when
// proxy_local.cpp can (canonical handles, the epoch mirror) and otherwise calls the generated forwarder (ProxyFwd_),
// which crosses exactly as the generated body did before. Design and the invalidation of each answer:
// research/03-dll-engine-boundary/proxy-design.md section 16; the purity of every mirrored getter:
// proxy/model/pure-methods.tsv (issue 2701, [C] from the DLL source at the pin).
#include "proxy_local.h"
#include "proxy_rt_internal.h"
#include "vp_ids.gen.h"
#include "proxy_ifaces.gen.h"

// ---------------------------------------------------------------------------------------------- canonical objects
// The object getters of the slot poll and the renderer. A local answer is a new wrapper over the canonical object
// (the DLL would also make a new wrapper); GetGame and GetPlayer stand for the game-core lock (proxy_local.h item 2).
#define VP_LOCAL_OBJECT(RET, MD, CALL, CODE, INDEX)                                                                    \
	void* p_ = ProxyLocal::Answer(kM_##CALL, CODE, (int32_t)(INDEX));                                                 \
	if (p_ != ProxyLocal::kCross) return static_cast<RET*>(static_cast<MD*>(p_));

ICvGame1* ProxyHook_ICvGameContext3_GetGame(Proxy_ICvGameContext3& self)
{
	VP_LOCAL_OBJECT(ICvGame1, ICvGame2, ICvGameContext3_GetGame, vpw::kCanonGame, 0)
	return ProxyFwd_ICvGameContext3_GetGame(self);
}

ICvPreGame1* ProxyHook_ICvGameContext3_GetPreGame(Proxy_ICvGameContext3& self)
{
	VP_LOCAL_OBJECT(ICvPreGame1, ICvPreGame2, ICvGameContext3_GetPreGame, vpw::kCanonPreGame, 0)
	return ProxyFwd_ICvGameContext3_GetPreGame(self);
}

ICvMap1* ProxyHook_ICvGameContext3_GetMap(Proxy_ICvGameContext3& self)
{
	VP_LOCAL_OBJECT(ICvMap1, ICvMap1, ICvGameContext3_GetMap, vpw::kCanonMap, 0)
	return ProxyFwd_ICvGameContext3_GetMap(self);
}

ICvGameAsynch1* ProxyHook_ICvGameContext3_GetGameAsynch(Proxy_ICvGameContext3& self)
{
	VP_LOCAL_OBJECT(ICvGameAsynch1, ICvGameAsynch1, ICvGameContext3_GetGameAsynch, vpw::kCanonGameAsynch, 0)
	return ProxyFwd_ICvGameContext3_GetGameAsynch(self);
}

ICvTeam1* ProxyHook_ICvGameContext3_GetTeam(Proxy_ICvGameContext3& self, TeamTypes eTeam)
{
	VP_LOCAL_OBJECT(ICvTeam1, ICvTeam1, ICvGameContext3_GetTeam, vpw::kCanonTeam, eTeam)
	return ProxyFwd_ICvGameContext3_GetTeam(self, eTeam);
}

ICvTerrainInfo1* ProxyHook_ICvGameContext3_GetTerrainInfo(Proxy_ICvGameContext3& self, TerrainTypes eTerrainNum)
{
	VP_LOCAL_OBJECT(ICvTerrainInfo1, ICvTerrainInfo1, ICvGameContext3_GetTerrainInfo, vpw::kCanonTerrainInfo, eTerrainNum)
	return ProxyFwd_ICvGameContext3_GetTerrainInfo(self, eTerrainNum);
}

ICvFeatureInfo1* ProxyHook_ICvGameContext3_GetFeatureInfo(Proxy_ICvGameContext3& self, FeatureTypes eFeatureNum)
{
	VP_LOCAL_OBJECT(ICvFeatureInfo1, ICvFeatureInfo1, ICvGameContext3_GetFeatureInfo, vpw::kCanonFeatureInfo, eFeatureNum)
	return ProxyFwd_ICvGameContext3_GetFeatureInfo(self, eFeatureNum);
}

ICvPlayer1* ProxyHook_ICvGame2_GetPlayer(Proxy_ICvGame2& self, const PlayerTypes ePlayer)
{
	VP_LOCAL_OBJECT(ICvPlayer1, ICvPlayer3, ICvGame2_GetPlayer, vpw::kCanonPlayer, ePlayer)
	return ProxyFwd_ICvGame2_GetPlayer(self, ePlayer);
}

// ---------------------------------------------------------------------------------------------- mirrored getters
// Answered from the epoch mirror while nothing impure ran since the value was read (proxy_local.h item 3). Only
// values read on canonical handles (and the pinned context) are kept.
PlayerTypes ProxyHook_ICvGame2_GetActivePlayer(Proxy_ICvGame2& self)
{
	struct F
	{
		Proxy_ICvGame2& s;
		PlayerTypes operator()() const { return ProxyFwd_ICvGame2_GetActivePlayer(s); }
	} f = {self};
	return VpMirrored<PlayerTypes>(kM_ICvGame2_GetActivePlayer, self.ProxyHandle(), 0, f);
}

TeamTypes ProxyHook_ICvGame2_GetActiveTeam(Proxy_ICvGame2& self)
{
	struct F
	{
		Proxy_ICvGame2& s;
		TeamTypes operator()() const { return ProxyFwd_ICvGame2_GetActiveTeam(s); }
	} f = {self};
	return VpMirrored<TeamTypes>(kM_ICvGame2_GetActiveTeam, self.ProxyHandle(), 0, f);
}

int ProxyHook_ICvGame2_GetGameTurn(const Proxy_ICvGame2& self)
{
	struct F
	{
		const Proxy_ICvGame2& s;
		int operator()() const { return ProxyFwd_ICvGame2_GetGameTurn(s); }
	} f = {self};
	return VpMirrored<int>(kM_ICvGame2_GetGameTurn, self.ProxyHandle(), 0, f);
}

int ProxyHook_ICvGame2_CountNumHumanGameTurnActive(Proxy_ICvGame2& self)
{
	struct F
	{
		Proxy_ICvGame2& s;
		int operator()() const { return ProxyFwd_ICvGame2_CountNumHumanGameTurnActive(s); }
	} f = {self};
	return VpMirrored<int>(kM_ICvGame2_CountNumHumanGameTurnActive, self.ProxyHandle(), 0, f);
}

GameStateTypes ProxyHook_ICvGame2_GetGameState(Proxy_ICvGame2& self)
{
	struct F
	{
		Proxy_ICvGame2& s;
		GameStateTypes operator()() const { return ProxyFwd_ICvGame2_GetGameState(s); }
	} f = {self};
	return VpMirrored<GameStateTypes>(kM_ICvGame2_GetGameState, self.ProxyHandle(), 0, f);
}

int ProxyHook_ICvGame2_GetNumGameTurnActive(Proxy_ICvGame2& self)
{
	struct F
	{
		Proxy_ICvGame2& s;
		int operator()() const { return ProxyFwd_ICvGame2_GetNumGameTurnActive(s); }
	} f = {self};
	return VpMirrored<int>(kM_ICvGame2_GetNumGameTurnActive, self.ProxyHandle(), 0, f);
}

bool ProxyHook_ICvGame2_IsFinalInitialized(const Proxy_ICvGame2& self)
{
	struct F
	{
		const Proxy_ICvGame2& s;
		bool operator()() const { return ProxyFwd_ICvGame2_IsFinalInitialized(s); }
	} f = {self};
	return VpMirrored<bool>(kM_ICvGame2_IsFinalInitialized, self.ProxyHandle(), 0, f);
}

bool ProxyHook_ICvGame2_IsNetworkMultiPlayer(const Proxy_ICvGame2& self)
{
	struct F
	{
		const Proxy_ICvGame2& s;
		bool operator()() const { return ProxyFwd_ICvGame2_IsNetworkMultiPlayer(s); }
	} f = {self};
	return VpMirrored<bool>(kM_ICvGame2_IsNetworkMultiPlayer, self.ProxyHandle(), 0, f);
}

bool ProxyHook_ICvGame2_IsOption(const Proxy_ICvGame2& self, GameOptionTypes eIndex)
{
	struct F
	{
		const Proxy_ICvGame2& s;
		GameOptionTypes a;
		bool operator()() const { return ProxyFwd_ICvGame2_IsOption(s, a); }
	} f = {self, eIndex};
	return VpMirrored<bool>(kM_ICvGame2_IsOption, self.ProxyHandle(), (int32_t)eIndex, f);
}

bool ProxyHook_ICvGame2_IsPaused(Proxy_ICvGame2& self)
{
	struct F
	{
		Proxy_ICvGame2& s;
		bool operator()() const { return ProxyFwd_ICvGame2_IsPaused(s); }
	} f = {self};
	return VpMirrored<bool>(kM_ICvGame2_IsPaused, self.ProxyHandle(), 0, f);
}

TeamTypes ProxyHook_ICvGameAsynch1_GetActiveTeam(Proxy_ICvGameAsynch1& self)
{
	struct F
	{
		Proxy_ICvGameAsynch1& s;
		TeamTypes operator()() const { return ProxyFwd_ICvGameAsynch1_GetActiveTeam(s); }
	} f = {self};
	return VpMirrored<TeamTypes>(kM_ICvGameAsynch1_GetActiveTeam, self.ProxyHandle(), 0, f);
}

bool ProxyHook_ICvGameAsynch1_IsFinalInitialized(const Proxy_ICvGameAsynch1& self)
{
	struct F
	{
		const Proxy_ICvGameAsynch1& s;
		bool operator()() const { return ProxyFwd_ICvGameAsynch1_IsFinalInitialized(s); }
	} f = {self};
	return VpMirrored<bool>(kM_ICvGameAsynch1_IsFinalInitialized, self.ProxyHandle(), 0, f);
}

bool ProxyHook_ICvGameContext3_IsGraphicsInitialized(const Proxy_ICvGameContext3& self)
{
	struct F
	{
		const Proxy_ICvGameContext3& s;
		bool operator()() const { return ProxyFwd_ICvGameContext3_IsGraphicsInitialized(s); }
	} f = {self};
	return VpMirrored<bool>(kM_ICvGameContext3_IsGraphicsInitialized, self.ProxyHandle(), 0, f);
}

bool ProxyHook_ICvPlayer3_IsHuman(const Proxy_ICvPlayer3& self)
{
	struct F
	{
		const Proxy_ICvPlayer3& s;
		bool operator()() const { return ProxyFwd_ICvPlayer3_IsHuman(s); }
	} f = {self};
	return VpMirrored<bool>(kM_ICvPlayer3_IsHuman, self.ProxyHandle(), 0, f);
}

bool ProxyHook_ICvPlayer3_IsAlive(const Proxy_ICvPlayer3& self)
{
	struct F
	{
		const Proxy_ICvPlayer3& s;
		bool operator()() const { return ProxyFwd_ICvPlayer3_IsAlive(s); }
	} f = {self};
	return VpMirrored<bool>(kM_ICvPlayer3_IsAlive, self.ProxyHandle(), 0, f);
}

bool ProxyHook_ICvPlayer3_IsEverAlive(const Proxy_ICvPlayer3& self)
{
	struct F
	{
		const Proxy_ICvPlayer3& s;
		bool operator()() const { return ProxyFwd_ICvPlayer3_IsEverAlive(s); }
	} f = {self};
	return VpMirrored<bool>(kM_ICvPlayer3_IsEverAlive, self.ProxyHandle(), 0, f);
}

bool ProxyHook_ICvPlayer3_IsTurnActive(const Proxy_ICvPlayer3& self)
{
	struct F
	{
		const Proxy_ICvPlayer3& s;
		bool operator()() const { return ProxyFwd_ICvPlayer3_IsTurnActive(s); }
	} f = {self};
	return VpMirrored<bool>(kM_ICvPlayer3_IsTurnActive, self.ProxyHandle(), 0, f);
}

PlayerTypes ProxyHook_ICvPlayer3_GetID(const Proxy_ICvPlayer3& self)
{
	struct F
	{
		const Proxy_ICvPlayer3& s;
		PlayerTypes operator()() const { return ProxyFwd_ICvPlayer3_GetID(s); }
	} f = {self};
	return VpMirrored<PlayerTypes>(kM_ICvPlayer3_GetID, self.ProxyHandle(), 0, f);
}

TeamTypes ProxyHook_ICvPlayer3_GetTeam(const Proxy_ICvPlayer3& self)
{
	struct F
	{
		const Proxy_ICvPlayer3& s;
		TeamTypes operator()() const { return ProxyFwd_ICvPlayer3_GetTeam(s); }
	} f = {self};
	return VpMirrored<TeamTypes>(kM_ICvPlayer3_GetTeam, self.ProxyHandle(), 0, f);
}

bool ProxyHook_ICvPlayer3_IsOptionKey(const Proxy_ICvPlayer3& self, PlayerOptionTypes eOptionID)
{
	struct F
	{
		const Proxy_ICvPlayer3& s;
		PlayerOptionTypes a;
		bool operator()() const { return ProxyFwd_ICvPlayer3_IsOptionKey(s, a); }
	} f = {self, eOptionID};
	return VpMirrored<bool>(kM_ICvPlayer3_IsOptionKey, self.ProxyHandle(), (int32_t)eOptionID, f);
}

SlotStatus ProxyHook_ICvPreGame2_slotStatus(Proxy_ICvPreGame2& self, PlayerTypes eID)
{
	struct F
	{
		Proxy_ICvPreGame2& s;
		PlayerTypes a;
		SlotStatus operator()() const { return ProxyFwd_ICvPreGame2_slotStatus(s, a); }
	} f = {self, eID};
	return VpMirrored<SlotStatus>(kM_ICvPreGame2_slotStatus, self.ProxyHandle(), (int32_t)eID, f);
}

CivilizationTypes ProxyHook_ICvPreGame2_civilization(Proxy_ICvPreGame2& self, PlayerTypes p)
{
	struct F
	{
		Proxy_ICvPreGame2& s;
		PlayerTypes a;
		CivilizationTypes operator()() const { return ProxyFwd_ICvPreGame2_civilization(s, a); }
	} f = {self, p};
	return VpMirrored<CivilizationTypes>(kM_ICvPreGame2_civilization, self.ProxyHandle(), (int32_t)p, f);
}

PlayerColorTypes ProxyHook_ICvPreGame2_playerColor(Proxy_ICvPreGame2& self, PlayerTypes p)
{
	struct F
	{
		Proxy_ICvPreGame2& s;
		PlayerTypes a;
		PlayerColorTypes operator()() const { return ProxyFwd_ICvPreGame2_playerColor(s, a); }
	} f = {self, p};
	return VpMirrored<PlayerColorTypes>(kM_ICvPreGame2_playerColor, self.ProxyHandle(), (int32_t)p, f);
}

bool ProxyHook_ICvPreGame2_isNetworkMultiplayerGame(Proxy_ICvPreGame2& self)
{
	struct F
	{
		Proxy_ICvPreGame2& s;
		bool operator()() const { return ProxyFwd_ICvPreGame2_isNetworkMultiplayerGame(s); }
	} f = {self};
	return VpMirrored<bool>(kM_ICvPreGame2_isNetworkMultiplayerGame, self.ProxyHandle(), 0, f);
}

// ---------------------------------------------------------------------------------------------- HasTurnTimerExpired
// ICvPlayer2::HasTurnTimerExpired is conditional (issue 2701): CvPlayer::hasTurnTimerExpired calls
// CvGame::hasTurnTimerExpired(GetID()) (CvPlayer.cpp:49274-49277), which returns false without touching anything when
// GAMEOPTION_END_TURN_TIMER_ENABLED is off (CvGame.cpp:2068-2070; isOption reads CvPreGame, CvGame.cpp:8102). So
// while the mirror holds IsOption(END_TURN_TIMER) = false (epoch unchanged, nothing in flight), the answer is false
// with no crossing. Otherwise the call crosses and counts as impure (it may reset the timer and write pregame).
bool ProxyHook_ICvPlayer3_HasTurnTimerExpired(Proxy_ICvPlayer3& self)
{
	vpipc::Handle g;
	if (ProxyLocal::On() && ProxyLocal::IsCanonHandle(self.ProxyHandle()) && ProxyLocal::GameHandle(g))
	{
		int32_t opt = 1;
		const int32_t eOpt = (int32_t)GAMEOPTION_END_TURN_TIMER_ENABLED;
		bool known = ProxyLocal::MirrorGet(kM_ICvGame2_IsOption, g, eOpt, opt);
		if (!known)
		{
			// Read the option (a pure crossing) and keep it.
			const ProxyLocal::Token t = ProxyLocal::MirrorBegin(g);
			vpipc::Channel& ch = ProxyRt::Chan();
			vpipc::Writer& w = ProxyRt::Begin(ch, kM_ICvGame2_IsOption);
			w.handle(g);
			w.i32(eOpt);
			vpipc::Reader r;
			if (ProxyRt::End(ch, kM_ICvGame2_IsOption, r))
			{
				const bool b = r.b();
				ProxyRt::DecodeCheck(kM_ICvGame2_IsOption, r);
				if (r.ok())
				{
					ProxyLocal::MirrorPut(t, kM_ICvGame2_IsOption, g, eOpt, b ? 1 : 0);
					// Usable now only if nothing impure started meanwhile (the same test a later hit makes).
					known = ProxyLocal::StillValid(t);
					opt = b ? 1 : 0;
				}
			}
		}
		if (known && opt == 0)
		{
			ProxyRtInt::NoteLocal(kM_ICvPlayer3_HasTurnTimerExpired);
			if (ProxyLocal::Verify())
			{
				ProxyLocal::ForcePureNext();  // pure in this epoch (the option is off): the check must not invalidate
				ProxyLocal::VerifyResult(kM_ICvPlayer3_HasTurnTimerExpired, 0,
				                         ProxyFwd_ICvPlayer3_HasTurnTimerExpired(self) ? 1 : 0);
			}
			return false;
		}
	}
	return ProxyFwd_ICvPlayer3_HasTurnTimerExpired(self);
}
