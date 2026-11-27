// srv_remote_feed.cpp - the DLL-side capture of the renderer's event feed (issue 3303, epic 3300). See
// srv_remote_feed.h; the feed itself (queue, table) is server/src/feed/srv_feed.cpp.
//
// DLL calls made here, all on the DLL's own thread and all on the D30 purity list (proxy/model/pure-methods.tsv):
//   ICvPlot1: GetPlotIndex, GetTerrainType, GetPlotType, GetFeatureType, GetImprovementType, GetOwner, IsRiver,
//             GetActiveFogOfWarMode, GetContinentType (issue 4305: PlotRecord::artType), IsWOfRiver, IsNWOfRiver,
//             IsNEOfRiver (ticket 0631: PlotRecord::riverEdges);  ICvMap1: GetPlot(int, int),
//             PlotNum;  ICvGameContext1: GetMap, GetFeatureInfo;
//   ICvFeatureInfo1: IsNaturalWonder;  ICvGame1: GetActiveTeam;  ICvCity1: GetOwner, GetID;  ICvUnknown: Destroy.
// Getters the feed would use but which are NOT on the list (named in the issue, not used): ICvMap1::GetGridWidth /
// GetGridHeight / GetPlotByIndex (the grid is derived from PlotNum and GetPlot instead), ICvPlot1::GetResourceType /
// GetRouteType / IsImprovementPillaged / IsRoutePillaged / IsCity / GetRevealedImprovementType, ICvCity1::GetPlot (so a
// city event carries no plot).
// Ticket 5062 (2026-11-23): ICvCity1::GetX / GetY, called ONLY in OnCombat on the attacking city of a kCityCombat (its
// strike's origin, kCityCombat's a[3]). Audited here, server-only (the proxy's list, proxy/model/pure-methods.tsv, is not
// changed): CvDllCity.cpp 143-146 / 148-151 return m_pCity->getX() / getY(), CvCity.h 549-552 / 554-557 inline
// `return m_iX;` / `return m_iY;` (the fork at the D30 audit's commit 1b87dd185, where ICvCity1::GetID's audited row
// cites CvCity.h 541-544): plain member reads, no side effect (research unit-combat-server.md 18.37.3).
// Issue 4452 (epic 0375, the unit feed; unit-server-scene.md 2.1-2.5), all pure (pure-methods.tsv; the rows of
// GetDamage, GetMaxHitPoints, IsEmbarked, ICvPlayer1::GetArtStyleType / IsBarbarian and the ICvCombatInfo1 getters were
// added by 4452 from the DLL fork at the x64 pin, tracker/issues/4459-premise-reads.md):
//   ICvUnit1: GetOwner, GetID, GetUnitType, GetDomainType, GetFacingDirection(false), GetPlot, GetDamage,
//             GetMaxHitPoints, IsEmbarked, IsGarrisoned, IsInvisible(active team, false), IsDelayedDeathExported;
//             on a unit in GameplayUnitDestroyed (called from CvUnit::~CvUnit) only GetOwner and GetID;
//   ICvPlayer1: IsEverAlive, GetTeam, IsAlive, IsMinorCiv, IsBarbarian, GetPlayerColor, GetCivilizationType,
//               GetCurrentEra, GetArtStyleType, IsHuman, IsTurnActive, GetMinorCivType (minor civilizations only;
//               issue 4460, pure-methods.tsv);  ICvGame1: GetPlayer, GetActiveTeam,
//               GetActivePlayer, IsOption(GAMEOPTION_QUICK_COMBAT);  ICvGameContext1: GetGame (only for a player row the
//               feed has not read yet and for a combat's game flags; it takes the game-core lock until Destroy, which
//               the calling DLL thread already holds);
//   ICvCombatInfo1: GetUnit, GetCity, GetPlot, GetAttackerAdvances, GetVisualizeCombat, GetDefenderRetaliates,
//                   GetNuclearDamageLevel, GetAttackIsRanged, GetAttackIsBombingMission, GetAttackIsAirSweep,
//                   GetDefenderCaptured, GetFinalDamage, GetDamageMemberCount, GetMaxDamageMemberCount, GetDamageMember;
//   Issue 4726: GetUnit(BATTLE_UNIT_INTERCEPTOR) and on that unit GetOwner, GetID, GetDamage, GetMaxHitPoints;
//   ICvCombatInfo1::GetDamageInflicted (pure-methods.tsv 'conditional': it fires the Lua GameEvent BattleDamageDelta when
//   EVENTS_BATTLES and EVENTS_BATTLES_DAMAGE are on) is called ONLY while the DLL's CustomModOptions has that event off
//   (SrvDbBattleDamageEvents, read once per load), i.e. only while it is a plain read.
#include "srv_remote.h"
#include "srv_remote_feed.h"
#include "feed/srv_feed.h"
#include "feed/srv_feed_server.h"
#include "feed/srv_render_events.h"   // ticket 4932: SetTurn
#include "forest/srv_forest_ids.h"   // epic 0431, ticket 4554: header only (the trees' feature ids by art define)
#include "render/srv_farm_ids.h"     // epic 0528, ticket 4852: header only (the farm's and wheat's ids by art define)
#include "render/srv_liveterrain_features.h"   // epic 0465, ticket 4907: header only (feature names by art define)
#include <string.h>
#include <vector>

// Issue 4726 (srv_remote_db.cpp): whether the DLL's battle-damage Lua event is on (1), off (0) or not known (-1), from its
// game connection's CustomModOptions.
int SrvDbBattleDamageEvents();

namespace
{
	SRWLOCK s_lock = SRWLOCK_INIT;              // the dirty list and the state below
	std::vector<uint32_t> s_dirty;              // plots named by notifications since the previous AtLeaveUpdate
	std::vector<uint8_t> s_dirtyMark;
	const size_t kDirtyCap = 1u << 20;
	volatile LONG s_turn = -1;                  // the game turn at the previous AtLeaveUpdate (events carry it)
	int s_lastTurn = INT32_MIN;                 // core thread only (AtLeaveUpdate)
	int s_lastRowTurn = INT32_MIN;              // core thread only: the turn of the last feed-per-turn.tsv row
	uint64_t s_lastDropped = 0;                 // core thread only
	bool s_bHaveTable = false;                  // core thread only
	uint32_t s_w = 0, s_h = 0;                  // core thread only
	volatile LONG64 s_nSnapshots = 0, s_nRefreshAll = 0, s_nRefreshDirty = 0, s_nPlotReads = 0, s_nExceptions = 0,
	                s_nNoMap = 0, s_nBadGrid = 0, s_nDirtyDropped = 0;
	// records a full refresh (turn change) found changed: changes no notification had patched (the refresh rule's
	// share), and the same for the dirty refreshes (fields the notification itself did not carry)
	volatile LONG64 s_nRefreshAllChanged = 0, s_nRefreshDirtyChanged = 0;
	// per getter field: how often a full refresh found it changed (terrain, plot type, feature, improvement, owner,
	// river, natural wonder, fog, art type (issue 4305)); core thread only
	LONG64 s_nRefreshField[9] = {};
	// issue 4305: plot reads whose continent art type was outside 0..4 (stored as 0)
	volatile LONG64 s_nArtTypeOutside = 0;

	void CountFieldChanges(const SrvFeed::PlotRecord& o, const SrvFeed::PlotRecord& n)
	{
		if(o.terrain != n.terrain) ++s_nRefreshField[0];
		if(o.plotType != n.plotType) ++s_nRefreshField[1];
		if(o.feature != n.feature) ++s_nRefreshField[2];
		if(!(o.known & 2) && o.improvement != n.improvement) ++s_nRefreshField[3];
		if(o.owner != n.owner) ++s_nRefreshField[4];
		if((o.flags & 1) != (n.flags & 1) || o.riverEdges != n.riverEdges) ++s_nRefreshField[5];   // 0631: the edges too
		if((o.flags & 8) != (n.flags & 8)) ++s_nRefreshField[6];
		if(o.fow != n.fow) ++s_nRefreshField[7];
		if(o.artType != n.artType) ++s_nRefreshField[8];
	}
	volatile LONG64 s_qwReadTicks = 0;          // QPC ticks spent in plot reads (snapshot, refreshes, rolling refresh)
	// The rolling refresh (epic 0615, ticket 4914; I3 finding, live-terrain.md 11.7; MC #9700): a SECOND way to find a
	// changed plot besides the notifications. At every LeaveUpdate that is not a snapshot or a turn change the next
	// s_rollK plots of the grid (round robin, the cursor kept across calls, wrapping at the plot count, reset by a
	// snapshot) are read through ReadPlot + UpdatePlot(kFGetterAll) exactly as a dirty plot is, so a plot-type or terrain
	// change no notification named (equal yields, or no area: 11.7) reaches the table within ceil(W * H / K) Updates.
	// K from server.cfg feed_rolling_plots (default kRollDefault; 0 off, for comparisons), read again at each snapshot.
	// Log (all plain, never per plot): one line per LeaveUpdate where the rolling refresh or the plot-state path (MC #9712,
	// below) found any change ("feed: plots found changed at turn T: by the plot-state path N (x,y: ...), by the rolling
	// refresh M (x,y: ...); ..."); one cost line per turn change after a turn with passes; the K at each snapshot. It runs whenever the feed is on (render=1 or
	// feed=1), whatever live terrain's switch says. Core thread only, except the counters (Interlocked, as the others).
	const int kRollDefault = 256;
	const int kRollMax = 1 << 20;
	int s_rollK = -1;                           // plots per LeaveUpdate (-1 not read yet)
	uint32_t s_rollCursor = 0;                  // the next plot index of the round robin
	volatile LONG64 s_nRollReads = 0, s_nRollFound = 0, s_nRollPasses = 0, s_nRollGridSkips = 0, s_qwRollTicks = 0;
	// the totals at the previous turn change (passes, reads, found, ticks) and the longest pass since: the cost line once
	// per turn change that had passes
	LONG64 s_rollPrev[4] = {}, s_qwRollMax = 0;
	// MC #9712 (ticket 4914): a GameplayPlotStateChange also marks its plot dirty (the DLL sends one for every plot whose
	// layout flag was set, e.g. by CvPlot::setPlotType with bRebuildGraphics, at the EXE's next UpdateLayout pass;
	// live-terrain.md 11.7). s_stateMark (under s_lock) remembers which dirty plots came from that path, so the
	// per-update line can say which path found a change; s_nStateFound counts the records the plot-state path found
	// changed, s_statePrev its total at the previous turn change.
	std::vector<uint8_t> s_stateMark;
	volatile LONG64 s_nStateFound = 0;
	LONG64 s_statePrev = 0;
	FILE* s_pPerTurn = NULL;
	bool s_bPerTurnTried = false;

	inline int32_t Turn() { return (int32_t)s_turn; }

	void MarkDirty(int32_t iIndex)
	{
		if(iIndex < 0)
			return;
		AcquireSRWLockExclusive(&s_lock);
		const size_t i = (size_t)iIndex;
		if(i >= s_dirtyMark.size())
		{
			if(i >= kDirtyCap)
			{
				++s_nDirtyDropped;
				ReleaseSRWLockExclusive(&s_lock);
				return;
			}
			s_dirtyMark.resize(i + 1, 0);
		}
		if(!s_dirtyMark[i])
		{
			s_dirtyMark[i] = 1;
			s_dirty.push_back((uint32_t)iIndex);
		}
		ReleaseSRWLockExclusive(&s_lock);
	}

	// MC #9712: MarkDirty for a GameplayPlotStateChange, the plot also marked as named by that path
	void MarkDirtyByState(int32_t iIndex)
	{
		MarkDirty(iIndex);
		if(iIndex < 0 || (size_t)iIndex >= kDirtyCap)
			return;
		AcquireSRWLockExclusive(&s_lock);
		if((size_t)iIndex >= s_stateMark.size())
			s_stateMark.resize((size_t)iIndex + 1, 0);
		s_stateMark[(size_t)iIndex] = 1;
		ReleaseSRWLockExclusive(&s_lock);
	}

	void TakeDirty(std::vector<uint32_t>& out)
	{
		out.clear();
		AcquireSRWLockExclusive(&s_lock);
		out.swap(s_dirty);
		for(size_t k = 0; k < out.size(); ++k)
			s_dirtyMark[out[k]] = 0;
		ReleaseSRWLockExclusive(&s_lock);
	}

	SrvFeed::Event MakeEvent(uint16_t uKind, int32_t iPlot)
	{
		SrvFeed::Event e = {};
		e.kind = uKind;
		e.plot = iPlot;
		e.turn = Turn();
		return e;
	}

	inline int8_t I8(int v) { return (int8_t)(v < -128 ? -128 : v > 127 ? 127 : v); }
	inline int16_t I16(int v) { return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }

	// FogOfWarModeTypes (CvEnums.h: 0 off, 1 unexplored, 2 no vis) -> PlotRecord::fow (bit 0 revealed, bit 1 visible)
	inline uint8_t FowBits(int iMode) { return (uint8_t)(iMode == 0 ? 3 : iMode == 2 ? 1 : 0); }

	// Natural-wonder flags per feature type, read once per snapshot / refresh pass.
	struct FeatureCache
	{
		ICvGameContext1* pCtx;
		std::vector<int8_t> nw;   // -1 unknown, 0, 1
		bool IsNW(int iFeature)
		{
			if(iFeature < 0 || iFeature > 4096)
				return false;
			if((size_t)iFeature >= nw.size())
				nw.resize((size_t)iFeature + 1, -1);
			if(nw[iFeature] < 0)
			{
				ICvFeatureInfo1* p = pCtx->GetFeatureInfo((FeatureTypes)iFeature);
				nw[iFeature] = (int8_t)(p && p->IsNaturalWonder() ? 1 : 0);
				if(p)   // epic 0431, ticket 4554: the forest / jungle ids by name (GetArtDefineTag: pure, D30 list)
				{
					const char* tag = p->GetArtDefineTag();
					srvrender::ForestFeatureTagSeen(iFeature, tag);
					srvrender::LiveTerrainFeatureArtTagSeen(iFeature, tag);   // epic 0465, ticket 4907: the producer's names
				}
				if(p)
					p->Destroy();
			}
			return nw[iFeature] != 0;
		}
	};

	// One plot through pure getters into v (the getter fields of srv_feed_server.h kFGetterAll).
	void ReadPlot(ICvPlot1* p, bool bFow, FeatureCache& fc, SrvFeed::PlotRecord& v)
	{
		v = SrvFeed::EmptyRecord();
		v.terrain = I8(p->GetTerrainType());
		v.plotType = I8(p->GetPlotType());
		v.feature = I8(p->GetFeatureType());
		v.improvement = I16(p->GetImprovementType());
		v.owner = I8(p->GetOwner());
		uint8_t f = 0;
		if(p->IsRiver())
			f |= 1;
		if(fc.IsNW(p->GetFeatureType()))
			f |= 8;
		v.flags = f;
		v.fow = bFow ? FowBits(p->GetActiveFogOfWarMode()) : 0;
		// issue 4305: the continent art type (pure, proxy/model/pure-methods.tsv); outside 0..4 -> 0, counted
		bool bOutside = false;
		v.artType = SrvFeed::ArtTypeFromDll((int)p->GetContinentType(), &bOutside);
		if(bOutside)
			InterlockedIncrement64(&s_nArtTypeOutside);
		// ticket 0631: the plot's own three river edges (pure, proxy/model/pure-methods.tsv); bits as the producer's
		// PlotInfo::riverEdges: 1 W of river, 2 NW of river, 4 NE of river. UpdatePlot sets known bit 2 (kFRiverEdges).
		v.riverEdges = (uint8_t)((p->IsWOfRiver() ? 1 : 0) | (p->IsNWOfRiver() ? 2 : 0) | (p->IsNEOfRiver() ? 4 : 0));
	}

	// The grid from pure getters only: width = PlotNum(0, 1); height = the first row y at which GetPlot(0, y) is NULL
	// (no Civ V map wraps in y; a wrap would show as GetPlotIndex != y * width and also ends the count).
	bool Grid(ICvMap1* pMap, uint32_t& w, uint32_t& h)
	{
		const int iW = pMap->PlotNum(0, 1);
		if(iW <= 0 || iW > 4096)
			return false;
		int y = 0;
		for(; y < 4096; ++y)
		{
			ICvPlot1* p = pMap->GetPlot(0, y);
			if(!p)
				break;
			const int idx = p->GetPlotIndex();
			p->Destroy();
			if(idx != y * iW)
				break;
		}
		if(y <= 0)
			return false;
		w = (uint32_t)iW;
		h = (uint32_t)y;
		return true;
	}

	double TicksToMs(LONG64 t)
	{
		LARGE_INTEGER f;
		QueryPerformanceFrequency(&f);
		return f.QuadPart ? 1000.0 * (double)t / (double)f.QuadPart : 0.0;
	}

	void PerTurnRow(int iTurn)
	{
		if(!s_bPerTurnTried)
		{
			s_bPerTurnTried = true;
			s_pPerTurn = _fsopen("feed-per-turn.tsv", "w", _SH_DENYWR);
			if(s_pPerTurn)
				fprintf(s_pPerTurn, "turn\tplot_state\tfeature\tfow\tcity_created\tcity_destroyed\tcity_captured\t"
				                    "natural_wonder\twall\tyield\tborder\tsnapshot\toverflow\t"
				                    "rf_terrain\trf_plot_type\trf_feature\trf_improvement\trf_owner\trf_river\trf_nw\t"
				                    "rf_fow\tdropped\tpatches_changed\t"
				                    "patches_same\tsnapshots\trefresh_all\trefresh_dirty\tplot_reads\tread_ms\texceptions\t"
				                    "refresh_all_changed\trefresh_dirty_changed\trf_art_type\tart_type_outside\n");
		}
		if(!s_pPerTurn)
			return;
		const SrvFeed::Counters c = SrvFeed::GetCounters();
		const SrvFeed::Counters2 c2 = SrvFeed::GetCounters2();
		fprintf(s_pPerTurn, "%d", iTurn);
		static const uint16_t kKinds[] = {SrvFeed::kPlotState, SrvFeed::kFeatureChanged, SrvFeed::kFowChanged,
		                                  SrvFeed::kCityCreated, SrvFeed::kCityDestroyed, SrvFeed::kCityCaptured,
		                                  SrvFeed::kNaturalWonder, SrvFeed::kWallCreated, SrvFeed::kYieldChanged,
		                                  SrvFeed::kBorderChanged, SrvFeed::kSnapshot, SrvFeed::kOverflow};
		for(size_t k = 0; k < sizeof(kKinds) / sizeof(kKinds[0]); ++k)
			fprintf(s_pPerTurn, "\t%llu", (unsigned long long)c2.perKind[kKinds[k]]);
		for(int k = 0; k < 8; ++k)
			fprintf(s_pPerTurn, "\t%lld", (long long)s_nRefreshField[k]);
		// issue 4305: rf_art_type and art_type_outside appended at the end (readers of the earlier columns unchanged)
		fprintf(s_pPerTurn, "\t%llu\t%llu\t%llu\t%lld\t%lld\t%lld\t%lld\t%.3f\t%lld\t%lld\t%lld\t%lld\t%lld\n",
		        (unsigned long long)c.dropped, (unsigned long long)c2.patchesChanged, (unsigned long long)c2.patchesSame,
		        (long long)s_nSnapshots, (long long)s_nRefreshAll, (long long)s_nRefreshDirty, (long long)s_nPlotReads,
		        TicksToMs(s_qwReadTicks), (long long)s_nExceptions, (long long)s_nRefreshAllChanged,
		        (long long)s_nRefreshDirtyChanged, (long long)s_nRefreshField[8], (long long)s_nArtTypeOutside);
		fflush(s_pPerTurn);
	}

	void NoteException(const char* szWhere)
	{
		const LONG64 n = InterlockedIncrement64(&s_nExceptions);
		if(n <= 5)
			SrvLog("feed: LOUD: exception caught in %s (%lld so far); the feed goes on, the game is not touched", szWhere,
			       (long long)n);
	}

	// Ticket 0529 (R2 of 0424's premise check; terrain-surface-verification.md 14.4): the plot snapshot under the load
	// screen. s_bRefreshNext: the load's snapshot was taken, the next LeaveUpdate (the first Update after the click) re-reads
	// every plot as a turn change does (no new generation; what changed in between is counted and named in one line).
	// s_bSnapshotNext: a load was seen but its snapshot could not be taken there, so the next LeaveUpdate takes it whatever
	// the turn says (a reload of the same turn would otherwise keep the earlier load's table). Both under the game-core lock,
	// as the rest of the snapshot state (the load path takes it through ICvGameContext1::GetGame, LeaveUpdate holds it).
	bool s_bRefreshNext = false, s_bSnapshotNext = false;
	volatile LONG64 s_nLoadSnapshots = 0, s_nLoadRefreshes = 0, s_nLoadRefreshChanged = 0, s_nLoadSkipped = 0;
	// the load's 0..63 sequence of ICvPlayer1::SetupGraphical (AfterSetupGraphical; Interlocked, any DLL thread)
	const int kLoadLastPlayer = 63;                // MAX_PLAYERS - 1 in the 22- and 43-civ builds (player-limits.md)
	volatile LONG s_lLoadPrevPlayer = INT32_MAX;   // the player id of the previous call
	volatile LONG s_lLoadArmed = 0;                // a sequence began (a call whose id was not above the previous one)
	                                               // and no LeaveUpdate came since

	// The snapshot / refresh point. bAtLoad (ticket 0529): called from AfterSetupGraphical under the load screen, which
	// takes a snapshot whatever the table and the turn say. Returns true when a snapshot was taken.
	bool AtLeaveUpdateBody(ICvGameContext1* pCtx, ICvGame1* pGame, int iTurn, bool bAtLoad = false)
	{
		InterlockedExchange(&s_turn, (LONG)iTurn);
		SrvRenderEvents::SetTurn(iTurn);   // ticket 4932: the turn the DLL's render messages carry
		const bool bTurnChanged = iTurn != s_lastTurn;
		const uint64_t dropped = SrvFeed::GetCounters().dropped;
		const bool bOverflow = dropped != s_lastDropped;
		s_lastDropped = dropped;
		bool bSnapshot = bAtLoad || s_bSnapshotNext || !s_bHaveTable || bOverflow || iTurn < s_lastTurn ||
		                 (s_lastTurn != INT32_MIN && iTurn > s_lastTurn + 1);
		// ticket 0529: the first LeaveUpdate after the load's snapshot reads every plot, as a turn change does
		const bool bLoadRefresh = !bAtLoad && s_bRefreshNext;
		const bool bFull = bTurnChanged || bLoadRefresh;
		if(bTurnChanged && s_lastTurn != INT32_MIN && s_lastTurn != s_lastRowTurn)
		{
			PerTurnRow(s_lastTurn);
			s_lastRowTurn = s_lastTurn;   // once per turn, also when the grid read below fails and is retried
			// the rolling refresh's cost in the turn that ended (one plain line, only when it ran)
			const LONG64 nPasses = s_nRollPasses - s_rollPrev[0];
			if(nPasses > 0)
			{
				const LONG64 nRd = s_nRollReads - s_rollPrev[1], nFd = s_nRollFound - s_rollPrev[2];
				const double ms = TicksToMs(s_qwRollTicks - s_rollPrev[3]);
				SrvLog("feed: rolling refresh in turn %d: %lld passes, %lld plots read, %lld found, %.3f ms (%.4f ms per "
				       "pass, longest %.4f ms); K %d; the plot-state path found %lld", s_lastTurn, (long long)nPasses,
				       (long long)nRd, (long long)nFd, ms, ms / (double)nPasses, TicksToMs(s_qwRollMax), s_rollK,
				       (long long)(s_nStateFound - s_statePrev));
			}
			s_statePrev = s_nStateFound;
			s_rollPrev[0] = s_nRollPasses;
			s_rollPrev[1] = s_nRollReads;
			s_rollPrev[2] = s_nRollFound;
			s_rollPrev[3] = s_qwRollTicks;
			s_qwRollMax = 0;
		}
		std::vector<uint32_t> dirty;
		TakeDirty(dirty);
		// MC #9712: which of the dirty plots a GameplayPlotStateChange named (their marks cleared with the list)
		std::vector<uint8_t> byState(dirty.size(), 0);
		AcquireSRWLockExclusive(&s_lock);
		for(size_t k = 0; k < dirty.size(); ++k)
			if(dirty[k] < s_stateMark.size() && s_stateMark[dirty[k]])
			{
				byState[k] = 1;
				s_stateMark[dirty[k]] = 0;
			}
		ReleaseSRWLockExclusive(&s_lock);
		// the rolling refresh runs between turns only (a snapshot or a turn change reads every plot anyway)
		const bool bRoll = !bSnapshot && !bFull && s_rollK > 0 && s_w > 0 && s_h > 0;
		if(!bSnapshot && !bFull && dirty.empty() && !bRoll)
			return false;
		ICvMap1* pMap = pCtx->GetMap();
		if(!pMap)
		{
			InterlockedIncrement64(&s_nNoMap);
			return false;
		}
		uint32_t w = 0, h = 0;
		if(bSnapshot || bFull)
		{
			if(!Grid(pMap, w, h))
			{
				pMap->Destroy();
				// no map yet (an Update before the map exists): the snapshot is retried at the next LeaveUpdate
				if(InterlockedIncrement64(&s_nBadGrid) <= 3)
					SrvLog("feed: no plot grid readable at turn %d (PlotNum(0, 1) <= 0 or GetPlot(0, 0) NULL); retried at "
					       "the next Update", iTurn);
				return false;
			}
			if(w != s_w || h != s_h)
				bSnapshot = true;
		}
		else
		{
			w = s_w;
			h = s_h;
		}
		const int iTeam = pGame->GetActiveTeam();
		const bool bFow = iTeam >= 0;   // GetActiveFogOfWarMode's PRECONDITION(activeTeam != NO_TEAM)
		FeatureCache fc;
		fc.pCtx = pCtx;
		LARGE_INTEGER t0, t1;
		QueryPerformanceCounter(&t0);
		LONG64 nReads = 0;
		SrvFeed::PlotRecord v;
		if(bSnapshot || bFull)
		{
			if(bSnapshot)
				SrvFeed::BeginSnapshot(w, h);
			// ticket 0529: the load refresh's own counts (the per-field totals before it)
			LONG64 fieldsBefore[9];
			memcpy(fieldsBefore, s_nRefreshField, sizeof(fieldsBefore));
			uint32_t nLoadChanged = 0, loadFirst[4] = {};
			for(uint32_t y = 0; y < h; ++y)
				for(uint32_t x = 0; x < w; ++x)
				{
					ICvPlot1* p = pMap->GetPlot((int)x, (int)y);
					if(!p)
						continue;
					ReadPlot(p, bFow, fc, v);
					p->Destroy();
					SrvFeed::PlotRecord old;
					const bool bOld = !bSnapshot && SrvFeed::GetPlot(y * w + x, old);
					if(SrvFeed::UpdatePlot(y * w + x, SrvFeed::kFGetterAll, v) && !bSnapshot)
					{
						InterlockedIncrement64(&s_nRefreshAllChanged);
						if(bOld)
							CountFieldChanges(old, v);
						if(nLoadChanged < 4)
							loadFirst[nLoadChanged] = y * w + x;
						++nLoadChanged;
					}
					++nReads;
				}
			if(bLoadRefresh && !bSnapshot)
			{
				// one plain line: whether the load's snapshot still held at the first Update (the fog bits above all)
				LONG64 d[9];
				for(int k = 0; k < 9; ++k)
					d[k] = s_nRefreshField[k] - fieldsBefore[k];
				char szAt[96] = {};
				int at = 0;
				for(uint32_t q = 0; q < nLoadChanged && q < 4 && at >= 0 && at < 96; ++q)
					at += snprintf(szAt + at, 96 - (size_t)at, "%s%u,%u", q ? " " : "", loadFirst[q] % w, loadFirst[q] / w);
				if(nLoadChanged > 4 && at >= 0 && at < 96)
					snprintf(szAt + at, 96 - (size_t)at, " ...");
				SrvLog("feed: the load's snapshot re-read at the first Update (turn %d): %u of %lld records changed (x,y: %s); "
				       "fields: terrain %lld, plot type %lld, feature %lld, improvement %lld, owner %lld, river %lld, natural "
				       "wonder %lld, fog %lld, art type %lld (ticket 0529)",
				       iTurn, nLoadChanged, (long long)nReads, szAt, (long long)d[0], (long long)d[1], (long long)d[2],
				       (long long)d[3], (long long)d[4], (long long)d[5], (long long)d[6], (long long)d[7], (long long)d[8]);
				InterlockedIncrement64(&s_nLoadRefreshes);
				InterlockedExchangeAdd64(&s_nLoadRefreshChanged, nLoadChanged);
			}
			if(!bAtLoad)
				s_bRefreshNext = false;   // done (or superseded by the snapshot of this LeaveUpdate)
			if(bSnapshot)
			{
				// epic 0528, ticket 4852: the farm improvement's and the wheat resource's ids by art define (pure getters, D30)
				for(int i = 0, n = pCtx->GetNumImprovementInfos(); i < n; ++i)
					if(ICvImprovementInfo1* q = pCtx->GetImprovementInfo((ImprovementTypes)i))
					{
						srvrender::FarmImprovementTagSeen(i, q->GetArtDefineTag());
						q->Destroy();
					}
				for(int i = 0, n = pCtx->GetNumResourceInfos(); i < n; ++i)
					if(ICvResourceInfo1* q = pCtx->GetResourceInfo((ResourceTypes)i))
					{
						srvrender::WheatResourceTagSeen(i, q->GetArtDefineTag());
						q->Destroy();
					}
				srvrender::FarmIdsResolved();
				srvrender::ForestFeatureIdsResolved();   // epic 0431, ticket 4554: every feature on the map was read above
				SrvFeed::EndSnapshot();
				s_bHaveTable = true;
				s_bSnapshotNext = false;
				s_bRefreshNext = bAtLoad;   // ticket 0529: the first Update after the load re-reads every plot
				s_w = w;
				s_h = h;
				InterlockedIncrement64(&s_nSnapshots);
				// the rolling refresh starts again at plot 0 of the new table, with K read again
				const int k = SrvCfgInt("feed_rolling_plots", kRollDefault);
				s_rollK = k < 0 ? 0 : k > kRollMax ? kRollMax : k;
				s_rollCursor = 0;
				if(s_rollK > 0)
					SrvLog("feed: rolling refresh: %d plots per Update (server.cfg feed_rolling_plots); a sweep of the %u "
					       "plots takes %u Updates", s_rollK, w * h, (w * h + (uint32_t)s_rollK - 1) / (uint32_t)s_rollK);
				else
					SrvLog("feed: rolling refresh off (server.cfg feed_rolling_plots=%d)", k);
			}
			else
				InterlockedIncrement64(&s_nRefreshAll);
		}
		else
		{
			uint32_t nStateFound = 0, stateFirst[4] = {}, nFound = 0, nRead = 0, first[4] = {};
			LARGE_INTEGER r0 = {}, r1 = {};
			for(size_t k = 0; k < dirty.size(); ++k)
			{
				const uint32_t idx = dirty[k];
				if(!w || idx >= w * h)
					continue;
				ICvPlot1* p = pMap->GetPlot((int)(idx % w), (int)(idx / w));
				if(!p)
					continue;
				ReadPlot(p, bFow, fc, v);
				p->Destroy();
				if(SrvFeed::UpdatePlot(idx, SrvFeed::kFGetterAll, v))
				{
					InterlockedIncrement64(&s_nRefreshDirtyChanged);
					if(byState[k])   // MC #9712: a change the plot-state path found (named by the per-update line)
					{
						if(nStateFound < 4)
							stateFirst[nStateFound] = idx;
						++nStateFound;
					}
				}
				++nReads;
			}
			if(!dirty.empty())
				InterlockedIncrement64(&s_nRefreshDirty);
			if(nStateFound)
				InterlockedExchangeAdd64(&s_nStateFound, nStateFound);
			// the rolling refresh, after the dirty plots (a plot read there is found unchanged here: only plots the
			// notifications missed count as found). Its reads are in nReads too (the totals' plot reads and read time).
			if(bRoll && pMap->PlotNum(0, 1) != (int)w)
			{
				// the map's width is not the table's (a map changed without a snapshot yet): no plot of the old grid is
				// read; the next snapshot or turn change reads the grid again
				s_rollCursor = 0;
				if(InterlockedIncrement64(&s_nRollGridSkips) <= 3)
					SrvLog("feed: rolling refresh skipped at turn %d: the map's width is not the table's %u", iTurn, w);
			}
			else if(bRoll)
			{
				QueryPerformanceCounter(&r0);
				const uint32_t n = w * h;
				const uint32_t k = (uint32_t)s_rollK < n ? (uint32_t)s_rollK : n;
				if(s_rollCursor >= n)
					s_rollCursor = 0;
				for(uint32_t j = 0; j < k; ++j)
				{
					const uint32_t idx = s_rollCursor;
					s_rollCursor = idx + 1 < n ? idx + 1 : 0;
					ICvPlot1* p = pMap->GetPlot((int)(idx % w), (int)(idx / w));
					if(!p)
						continue;
					ReadPlot(p, bFow, fc, v);
					p->Destroy();
					++nRead;
					if(SrvFeed::UpdatePlot(idx, SrvFeed::kFGetterAll, v))
					{
						if(nFound < 4)
							first[nFound] = idx;
						++nFound;
					}
				}
				QueryPerformanceCounter(&r1);
				if(r1.QuadPart - r0.QuadPart > s_qwRollMax)
					s_qwRollMax = r1.QuadPart - r0.QuadPart;
				nReads += nRead;
				InterlockedIncrement64(&s_nRollPasses);
				InterlockedExchangeAdd64(&s_nRollReads, nRead);
				InterlockedExchangeAdd64(&s_qwRollTicks, r1.QuadPart - r0.QuadPart);
				if(nFound)
					InterlockedExchangeAdd64(&s_nRollFound, nFound);
			}
			// one plain line per LeaveUpdate that found any change by either path, with the path per plot (MC #9712 (iii))
			if(nFound || nStateFound)
			{
				char szState[96] = {}, szRoll[96] = {};
				const uint32_t* lists[2] = {stateFirst, first};
				const uint32_t counts[2] = {nStateFound, nFound};
				char* outs[2] = {szState, szRoll};
				for(int L = 0; L < 2; ++L)
				{
					int at = 0;
					for(uint32_t q = 0; q < counts[L] && q < 4 && at >= 0 && at < 96; ++q)
						at += snprintf(outs[L] + at, 96 - (size_t)at, "%s%u,%u", q ? " " : "", lists[L][q] % w,
						               lists[L][q] / w);
					if(counts[L] > 4 && at >= 0 && at < 96)
						snprintf(outs[L] + at, 96 - (size_t)at, " ...");
				}
				SrvLog("feed: plots found changed at turn %d: by the plot-state path %u (x,y: %s), by the rolling refresh "
				       "%u (x,y: %s); rolling %u plots read in %.3f ms; totals: plot-state %lld, rolling %lld passes, %lld "
				       "read, %lld found, %.1f ms",
				       iTurn, nStateFound, szState, nFound, szRoll, nRead, TicksToMs(r1.QuadPart - r0.QuadPart),
				       (long long)s_nStateFound, (long long)s_nRollPasses, (long long)s_nRollReads, (long long)s_nRollFound,
				       TicksToMs(s_qwRollTicks));
			}
		}
		QueryPerformanceCounter(&t1);
		InterlockedExchangeAdd64(&s_qwReadTicks, t1.QuadPart - t0.QuadPart);
		InterlockedExchangeAdd64(&s_nPlotReads, nReads);
		pMap->Destroy();
		if(bSnapshot)
			SrvLog("feed: snapshot %u x %u at turn %d (%lld plots read, %.2f ms)", w, h, iTurn, (long long)nReads,
			       TicksToMs(t1.QuadPart - t0.QuadPart));
		s_lastTurn = iTurn;
		return bSnapshot;
	}

	int32_t PlotIndex(const ICvPlot1* p) { return p ? (int32_t)p->GetPlotIndex() : -1; }

	// ---- issue 4452 (epic 0375): the unit feed ----
	// Counters (the feed's own are SrvFeed::Counters2): per DLL thread role the unit calls taken (ui, core, aux, other)
	// and how many came inside ICvGame1::Update (premise F8: which threads make unit calls).
	volatile LONG64 s_nUnitCalls[4] = {}, s_nUnitCallsInUpdate = 0, s_nUnitNull = 0, s_nCombatNull = 0,
	                s_nPlayerReads = 0, s_nPlayerReadFails = 0, s_nNoActiveTeam = 0, s_nLoads = 0, s_nCombatGameReads = 0,
	                s_nMembersCut = 0, s_nMembersNull = 0;
	// issue 4726: interceptors read (GetUnit(BATTLE_UNIT_INTERCEPTOR), one call per combat) and appended as a member;
	// combats whose per-combat damage was read, combats where the battle-damage Lua event was on or not known (no read),
	// CustomModOptions reads (one per load) and the reads that found nothing
	volatile LONG64 s_nInterceptors = 0, s_nDamageRead = 0, s_nDamageGated = 0, s_nModOptReads = 0, s_nModOptFails = 0;
	// ticket 5062: an attacking city's plot read (ICvCity1::GetX / GetY) into kCityCombat's a[3], and the reads outside
	// the table's grid (no table yet, or a map changed without a snapshot: a[3] stays 0, not known)
	volatile LONG64 s_nCityPlots = 0, s_nCityPlotsOutside = 0;
	volatile LONG s_activeTeam = -2;            // the active team of the last player-row read (-2 never read)
	volatile LONG s_lastSetupPlayer = INT32_MAX; // the player id of the previous ICvPlayer1::SetupGraphical
	volatile LONG s_playersStale = 1;           // the rows are re-read at the next AtLeaveUpdate
	int s_lastPlayersTurn = INT32_MIN;          // core thread only
	uint64_t s_lastDroppedUnit = 0;             // core thread only
	volatile LONG64 s_nDroppedUnitLoud = 0;

	void CountUnitCall()
	{
		const int ch = SrvRemote::ThreadChannel();
		InterlockedIncrement64(&s_nUnitCalls[ch >= 0 && ch < 3 ? ch : 3]);
		if(SrvRemote::InUpdate())
			InterlockedIncrement64(&s_nUnitCallsInUpdate);
	}

	void NoteNull(const char* szWhere, volatile LONG64* pCounter)
	{
		const LONG64 n = InterlockedIncrement64(pCounter);
		if(n <= 5)
			SrvLog("feed: LOUD: %s with a NULL argument (%lld so far): pushed with key -1, no record changed", szWhere,
			       (long long)n);
	}

	// One player's row through pure getters (ICvPlayer1::*) of the player object p.
	void FillPlayerRow(ICvPlayer1* p, int iPlayer, int iActiveTeam, SrvFeed::PlayerRecord& r)
	{
		r = SrvFeed::PlayerRecord();
		r.player = (int8_t)iPlayer;
		r.team = I8((int)p->GetTeam());
		r.alive = p->IsAlive() ? 1 : 0;
		const bool bMinor = p->IsMinorCiv();
		r.flags = (uint8_t)((bMinor ? 1 : 0) | (p->IsBarbarian() ? 2 : 0));
		r.colour = (int32_t)p->GetPlayerColor();
		// issue 4460 (A1): a city-state's row carries its MinorCivilizations index (srv_feed.h PlayerRecord)
		r.civilization = bMinor ? (int32_t)p->GetMinorCivType() : (int32_t)p->GetCivilizationType();
		r.era = (int32_t)p->GetCurrentEra();
		r.artStyle = (int32_t)p->GetArtStyleType();
		r.activeTeam = iActiveTeam;
		r.state = (p->IsHuman() ? 1u : 0u) | (p->IsTurnActive() ? 2u : 0u);
		InterlockedIncrement64(&s_nPlayerReads);
	}

	// Every ever-alive player's row (the snapshot point, each turn change, after a load), through ICvGame1::GetPlayer.
	void RefreshPlayers(ICvGame1* pGame)
	{
		const int iTeam = (int)pGame->GetActiveTeam();
		InterlockedExchange(&s_activeTeam, (LONG)iTeam);
		for(int i = 0; i < 64; ++i)
		{
			ICvPlayer1* p = pGame->GetPlayer((PlayerTypes)i);
			if(!p)
			{
				InterlockedIncrement64(&s_nPlayerReadFails);
				continue;
			}
			if(p->IsEverAlive())
			{
				SrvFeed::PlayerRecord r;
				FillPlayerRow(p, i, iTeam, r);
				SrvFeed::SetPlayer(r);
			}
			p->Destroy();
		}
	}

	// The owner's row from the feed, read through the game context when the feed has none yet (a creation before the
	// first AtLeaveUpdate of a load, or a player that became alive in the middle of a turn).
	bool OwnerRow(int iOwner, SrvFeed::PlayerRecord& r)
	{
		if(iOwner < 0 || iOwner > 63)
			return false;
		if(SrvFeed::GetPlayerRow(iOwner, r))
			return true;
		ICvGameContext1* pCtx = SrvRemote::Context();
		ICvGame1* pGame = pCtx ? pCtx->GetGame() : NULL;
		if(!pGame)
		{
			InterlockedIncrement64(&s_nPlayerReadFails);
			return false;
		}
		const int iTeam = (int)pGame->GetActiveTeam();
		InterlockedExchange(&s_activeTeam, (LONG)iTeam);
		ICvPlayer1* p = pGame->GetPlayer((PlayerTypes)iOwner);
		if(p)
		{
			FillPlayerRow(p, iOwner, iTeam, r);
			p->Destroy();
			SrvFeed::SetPlayer(r);
		}
		else
			InterlockedIncrement64(&s_nPlayerReadFails);
		pGame->Destroy();
		return p != NULL;
	}

	// A unit's record through pure getters (srv_feed.h UnitRecord). The call-derived flag bits are the feed's.
	void ReadUnit(ICvUnit1* u, SrvFeed::UnitRecord& r)
	{
		r = SrvFeed::UnitRecord();
		const int iOwner = (int)u->GetOwner();
		r.owner = I8(iOwner);
		r.id = (int32_t)u->GetID();
		r.unitType = (int32_t)u->GetUnitType();
		r.domain = I8((int)u->GetDomainType());
		// true always gives NO_DIRECTION (CvUnit.cpp 25338-25349); the table takes this value only at a creation or a move
		// (issue 4463, srv_feed.cpp FacingKind: a Lua setFacingDirection sends no call and the EXE keeps its facing)
		r.facing = I8((int)u->GetFacingDirection(false));
		ICvPlot1* p = u->GetPlot();
		r.plot = PlotIndex(p);
		if(p)
			p->Destroy();
		r.damage = I16(u->GetDamage());
		r.maxHP = I16(u->GetMaxHitPoints());
		uint8_t f = 0;
		if(u->IsEmbarked())
			f |= SrvFeed::kUnitFlagEmbarked;
		if(u->IsGarrisoned())
			f |= SrvFeed::kUnitFlagGarrisoned;
		if(u->IsDelayedDeathExported())
			f |= SrvFeed::kUnitFlagDelayedDeath;
		SrvFeed::PlayerRecord pr;
		const bool bRow = OwnerRow(iOwner, pr);
		const int iTeam = (int)s_activeTeam;
		if(iTeam >= 0)
		{
			if(u->IsInvisible((TeamTypes)iTeam, false))
				f |= SrvFeed::kUnitFlagInvisible;
		}
		else
			InterlockedIncrement64(&s_nNoActiveTeam);   // no team to ask (a NO_TEAM index would read outside arrays)
		r.flags = f;
		r.artStyle = bRow ? I16(pr.artStyle) : (int16_t)-1;
		r.era = bRow ? I16(pr.era) : (int16_t)-1;
	}

	SrvFeed::Event UnitEvent(uint16_t uKind, const SrvFeed::UnitRecord& r)
	{
		SrvFeed::Event e = MakeEvent(uKind, r.plot);
		e.a[0] = r.owner;
		e.a[1] = r.id;
		return e;
	}

	// The unit pointer was NULL: the event with key -1, no record (counted, LOUD first 5).
	void PushNullUnit(uint16_t uKind, int iA2, int iA3)
	{
		NoteNull("a unit call", &s_nUnitNull);
		SrvFeed::Event e = MakeEvent(uKind, -1);
		e.a[0] = -1;
		e.a[1] = -1;
		e.a[2] = iA2;
		e.a[3] = iA3;
		SrvFeed::PushUnit(e, NULL, SrvFeed::UnitOp::kNone);
	}

	// The game flags of a combat (CombatFlagBits 8-10): quick combat option, the active player's turn.
	uint16_t CombatGameFlags()
	{
		ICvGameContext1* pCtx = SrvRemote::Context();
		ICvGame1* pGame = pCtx ? pCtx->GetGame() : NULL;
		if(!pGame)
			return 0;
		uint16_t f = SrvFeed::kCombatGameRead;
		if(pGame->IsOption(GAMEOPTION_QUICK_COMBAT))
			f |= SrvFeed::kCombatQuickOption;
		const int iActive = (int)pGame->GetActivePlayer();
		if(iActive >= 0 && iActive < 64)
		{
			ICvPlayer1* p = pGame->GetPlayer((PlayerTypes)iActive);
			if(p)
			{
				if(p->IsTurnActive())
					f |= SrvFeed::kCombatActiveTurn;
				p->Destroy();
			}
		}
		pGame->Destroy();
		InterlockedIncrement64(&s_nCombatGameReads);
		return f;
	}

	// Issue 4726: whether ICvCombatInfo1::GetDamageInflicted is a plain read now. It fires the Lua GameEvent
	// BattleDamageDelta only with MOD_EVENTS_BATTLES_DAMAGE = EVENTS_BATTLES && EVENTS_BATTLES_DAMAGE (CustomMods.h; an
	// option without a row is 0), which the DLL caches from its CustomModOptions at CacheGameDatabaseData. Read once per
	// load (the first combat after a kLoadBegin) from the DLL's game connection; a failed read counts as "event on" (no
	// GetDamageInflicted call; the damage stays unknown). DLL thread only.
	LONG64 s_modOptLoad = -1;     // s_nLoads at the last read
	bool s_damageEventOff = false;
	bool BattleDamageEventOff()
	{
		const LONG64 nLoads = s_nLoads;
		if(s_modOptLoad == nLoads)
			return s_damageEventOff;
		s_modOptLoad = nLoads;
		InterlockedIncrement64(&s_nModOptReads);
		const int on = SrvDbBattleDamageEvents();
		if(on < 0)
			InterlockedIncrement64(&s_nModOptFails);
		s_damageEventOff = on == 0;
		return s_damageEventOff;
	}

	// The checks at each LeaveUpdate (core thread): the player rows at a turn change or after a load, and a LOUD line
	// when unit events were dropped (the allocator is out of step, unit-server-scene.md 2.5).
	void UnitsAtLeaveUpdate(ICvGame1* pGame, int iTurn)
	{
		if(!SrvFeed::UnitsEnabled())
			return;
		if(InterlockedExchange(&s_playersStale, 0) || iTurn != s_lastPlayersTurn)
		{
			RefreshPlayers(pGame);
			s_lastPlayersTurn = iTurn;
		}
		const uint64_t d = SrvFeed::GetCounters2().droppedUnit;
		if(d != s_lastDroppedUnit)
		{
			if(InterlockedIncrement64(&s_nDroppedUnitLoud) <= 5)
				SrvLog("feed: LOUD: %llu unit events dropped so far (queue cap or a full side table; %llu since the last "
				       "check): the render side's seed allocator is out of step (unit-server-scene.md 2.5)",
				       (unsigned long long)d, (unsigned long long)(d - s_lastDroppedUnit));
			s_lastDroppedUnit = d;
		}
	}
}  // namespace

namespace SrvRemoteFeed
{
void OnPlotStateChange(const ICvPlot1* pPlot, int iResource, int iImprovement, int iImprovementState, int iRoute,
                       int iRouteState)
{
	if(!SrvFeed::Enabled())
		return;
	try
	{
		const int32_t idx = PlotIndex(pPlot);
		SrvFeed::Event e = MakeEvent(SrvFeed::kPlotState, idx);
		e.a[0] = iResource;
		e.a[1] = iImprovement;
		e.a[2] = iImprovementState;
		e.a[3] = (int32_t)(((uint32_t)(uint16_t)I16(iRoute)) | ((uint32_t)(uint16_t)I16(iRouteState) << 16));
		SrvFeed::Push(e);
		if(idx < 0)
			return;
		SrvFeed::PlotRecord v = SrvFeed::EmptyRecord();
		v.resource = I16(iResource);
		v.improvement = I16(iImprovement);
		v.route = I8(iRoute);
		v.improvementState = I8(iImprovementState);
		v.routeState = I8(iRouteState);
		v.flags = (uint8_t)((iImprovementState == 3 ? 2 : 0) | (iRouteState == 2 || iRouteState == 3 ? 4 : 0));
		SrvFeed::UpdatePlot((uint32_t)idx, SrvFeed::kFLayoutAll, v);
		MarkDirtyByState(idx);   // MC #9712: the getter fields (plot type, terrain, ...) at the next LeaveUpdate
	}
	catch(...)
	{
		NoteException("GameplayPlotStateChange");
	}
}

void OnFeatureChanged(ICvPlot1* pPlot, int iFeature)
{
	if(!SrvFeed::Enabled())
		return;
	try
	{
		const int32_t idx = PlotIndex(pPlot);
		SrvFeed::Event e = MakeEvent(SrvFeed::kFeatureChanged, idx);
		e.a[0] = iFeature;
		SrvFeed::Push(e);
		if(idx < 0)
			return;
		SrvFeed::PlotRecord v = SrvFeed::EmptyRecord();
		v.feature = I8(iFeature);
		SrvFeed::UpdatePlot((uint32_t)idx, SrvFeed::kFFeature, v);
		MarkDirty(idx);   // the natural-wonder flag and the rest at the next LeaveUpdate
	}
	catch(...)
	{
		NoteException("GameplayFeatureChanged");
	}
}

void OnFowChanged(int iX, int iY, int iMode, bool bSetAll)
{
	if(!SrvFeed::Enabled())
		return;
	try
	{
		uint32_t w = 0, h = 0;
		SrvFeed::GetSize(w, h);
		const bool bIn = iX >= 0 && iY >= 0 && (uint32_t)iX < w && (uint32_t)iY < h;
		const int32_t idx = bIn ? (int32_t)((uint32_t)iY * w + (uint32_t)iX) : -1;
		SrvFeed::Event e = MakeEvent(SrvFeed::kFowChanged, idx);
		e.a[0] = iMode;
		e.a[1] = bSetAll ? 1 : 0;
		e.a[2] = iX;
		e.a[3] = iY;
		SrvFeed::Push(e);
		if(idx < 0)
			return;
		SrvFeed::PlotRecord v = SrvFeed::EmptyRecord();
		v.fow = FowBits(iMode);
		SrvFeed::UpdatePlot((uint32_t)idx, SrvFeed::kFFow, v);
	}
	catch(...)
	{
		NoteException("GameplayFOWChanged");
	}
}

void OnCity(uint16_t uKind, ICvCity1* pCity, int iArg)
{
	if(!SrvFeed::Enabled())
		return;
	try
	{
		SrvFeed::Event e = MakeEvent(uKind, -1);
		e.a[0] = iArg;
		e.a[1] = pCity ? (int32_t)pCity->GetOwner() : -1;
		e.a[2] = pCity ? (int32_t)pCity->GetID() : -1;
		SrvFeed::Push(e);
	}
	catch(...)
	{
		NoteException("GameplayCity*");
	}
}

void OnPlotOnly(uint16_t uKind, ICvPlot1* pPlot)
{
	if(!SrvFeed::Enabled())
		return;
	try
	{
		const int32_t idx = PlotIndex(pPlot);
		SrvFeed::Push(MakeEvent(uKind, idx));
		MarkDirty(idx);
	}
	catch(...)
	{
		NoteException("a plot notification");
	}
}

void OnBorder(ICvPlot1* pPlot)
{
	if(!SrvFeed::Enabled())
		return;
	try
	{
		const int32_t idx = PlotIndex(pPlot);
		SrvFeed::Event e = MakeEvent(SrvFeed::kBorderChanged, idx);
		const int iOwner = pPlot ? (int)pPlot->GetOwner() : -1;
		e.a[0] = iOwner;
		SrvFeed::Push(e);
		if(idx < 0)
			return;
		SrvFeed::PlotRecord v = SrvFeed::EmptyRecord();
		v.owner = I8(iOwner);
		SrvFeed::UpdatePlot((uint32_t)idx, SrvFeed::kFOwner, v);
	}
	catch(...)
	{
		NoteException("UpdateCountryBorder");
	}
}

void AtLeaveUpdate(ICvGameContext1* pContext, ICvGame1* pGame, int iTurn)
{
	if(!SrvFeed::Enabled() || !pContext || !pGame)
		return;
	InterlockedExchange(&s_lLoadArmed, 0);   // ticket 0529: a SetupGraphical sequence an Update interrupts is no load's
	try
	{
		UnitsAtLeaveUpdate(pGame, iTurn);   // issue 4452: player rows, dropped unit events (nothing with units off)
	}
	catch(...)
	{
		NoteException("the player rows at LeaveUpdate");
	}
	try
	{
		AtLeaveUpdateBody(pContext, pGame, iTurn);
	}
	catch(...)
	{
		NoteException("the snapshot at LeaveUpdate");
	}
}

// Ticket 0529 (R2 of 0424's premise check; terrain-surface-verification.md 14.4). The EXE calls ICvPlayer1::SetupGraphical
// for players 0..63 once per load, on its UI thread, near the load's end and before the first ICvGame1::Update (which comes
// only after the player's click; 14.2). The fog bits of the table are the save's own there [C]: GetActiveFogOfWarMode reads
// CvPlot's m_bfRevealed and m_aiVisibilityCount, which CvPlot's serializer restores, and CvPlayer / CvCity / CvUnit
// ::setupGraphical only notify the engine. What may still change before the first Update (CvGame::DoGameStarted's
// CvPlot::Validate) is caught by the full re-read of the first LeaveUpdate (s_bRefreshNext) and named in its line.
void AfterSetupGraphical(int iPlayer)
{
	if(!SrvFeed::Enabled())
		return;
	try
	{
		const LONG prev = InterlockedExchange(&s_lLoadPrevPlayer, (LONG)iPlayer);
		if(iPlayer <= (int)prev)
			InterlockedExchange(&s_lLoadArmed, 1);   // the first call of a load: a player id not above the previous one
		if(iPlayer != kLoadLastPlayer || !InterlockedExchange(&s_lLoadArmed, 0))
			return;
		const int iOn = SrvCfgInt("feed_load_snapshot", 1);
		const char* szWhy = NULL;
		if(SrvRemote::InUpdate())
			szWhy = "the call came inside an ICvGame1::Update (not a load)";
		else if(iOn == 0)
			szWhy = "server.cfg feed_load_snapshot=0";
		ICvGameContext1* pCtx = szWhy ? NULL : SrvRemote::Context();
		if(!szWhy && !pCtx)
			szWhy = "no game context";
		ICvGame1* pGame = pCtx ? pCtx->GetGame() : NULL;   // takes the game-core lock until Destroy
		if(!szWhy && !pGame)
			szWhy = "no game";
		bool bTaken = false;
		if(pGame)
		{
			try
			{
				const int iTurn = pGame->GetGameTurn();
				if((int)pGame->GetActiveTeam() < 0)
					szWhy = "no active team (the fog bits would all read unexplored)";
				else
				{
					LARGE_INTEGER t0, t1;
					QueryPerformanceCounter(&t0);
					bTaken = AtLeaveUpdateBody(pCtx, pGame, iTurn, true);
					QueryPerformanceCounter(&t1);
					if(bTaken)
					{
						const LONG64 n = InterlockedIncrement64(&s_nLoadSnapshots);
						SrvLog("feed: load snapshot %lld taken under the load screen at ICvPlayer1::SetupGraphical(%d), turn %d "
						       "(%.2f ms in the call); the first Update re-reads every plot (ticket 0529)",
						       (long long)n, iPlayer, iTurn, TicksToMs(t1.QuadPart - t0.QuadPart));
					}
					else
						szWhy = "no plot grid readable";
				}
				// a load seen but its snapshot not taken (with the lock held): the first LeaveUpdate takes it, whatever
				// the turn says (a reload of the same turn would otherwise keep the earlier load's table)
				if(!bTaken)
					s_bSnapshotNext = true;
			}
			catch(...)
			{
				pGame->Destroy();   // the game-core lock is never left held
				throw;
			}
			pGame->Destroy();
		}
		if(!bTaken && InterlockedIncrement64(&s_nLoadSkipped) <= 8)
			SrvLog("feed: no load snapshot at ICvPlayer1::SetupGraphical(%d): %s; the plot snapshot comes at the first Update "
			       "(ticket 0529)", iPlayer, szWhy ? szWhy : "-");
	}
	catch(...)
	{
		NoteException("ICvPlayer1::SetupGraphical (load snapshot)");
	}
}

void WriteTotals(FILE* f)
{
	const SrvFeed::Counters c = SrvFeed::GetCounters();
	fprintf(f, "feed: %s; events pushed %llu, dropped %llu, drains %llu; snapshots %lld, full refreshes %lld, dirty "
	           "refreshes %lld, plot reads %lld (%.1f ms); exceptions %lld, no map %lld, bad grid %lld, dirty dropped %lld\n",
	        SrvFeed::Enabled() ? "on" : "off", (unsigned long long)c.pushed, (unsigned long long)c.dropped,
	        (unsigned long long)c.drains, (long long)s_nSnapshots, (long long)s_nRefreshAll, (long long)s_nRefreshDirty,
	        (long long)s_nPlotReads, TicksToMs(s_qwReadTicks), (long long)s_nExceptions, (long long)s_nNoMap,
	        (long long)s_nBadGrid, (long long)s_nDirtyDropped);
	// ticket 0529: the snapshots under the load screen
	fprintf(f, "feed load snapshots (0529): taken %lld, not taken %lld; first-Update re-reads %lld, records changed in "
	           "them %lld\n",
	        (long long)s_nLoadSnapshots, (long long)s_nLoadSkipped, (long long)s_nLoadRefreshes,
	        (long long)s_nLoadRefreshChanged);
	// issue 4452: the unit feed
	const SrvFeed::Counters2 c2 = SrvFeed::GetCounters2();
	fprintf(f, "feed units: %s; unit events %llu (dropped %llu, side table full %llu), loads %lld, table inserts %llu, "
	           "updates %llu, removes %llu, unknown removals %llu, known creations %llu, embark creations %llu, "
	           "pre-creation events %llu (not kept %llu), bad keys %llu, patch unknown %llu; paths %llu (%llu plots), "
	           "combats %llu (%llu members, members cut %lld, NULL members %lld), combat results %llu (late %llu), "
	           "combat game reads %lld; player rows changed %llu, same %llu, bad %llu, reads %lld, read failures %lld; "
	           "no active team %lld; NULL units %lld, NULL combats %lld; database copies %llu\n",
	        SrvFeed::UnitsEnabled() ? "on" : "off", (unsigned long long)c2.unitEvents, (unsigned long long)c2.droppedUnit,
	        (unsigned long long)c2.sideTableFull, (long long)s_nLoads, (unsigned long long)c2.unitInserts,
	        (unsigned long long)c2.unitUpdates, (unsigned long long)c2.unitRemoves, (unsigned long long)c2.unitRemoveUnknown,
	        (unsigned long long)c2.unitCreateKnown, (unsigned long long)c2.embarkCreations,
	        (unsigned long long)c2.preCreationEvents, (unsigned long long)c2.preCreationDropped,
	        (unsigned long long)c2.unitBadKey, (unsigned long long)c2.unitPatchUnknown, (unsigned long long)c2.pathsKept,
	        (unsigned long long)c2.pathPlotsKept, (unsigned long long)c2.combatsKept,
	        (unsigned long long)c2.combatMembersKept, (long long)s_nMembersCut, (long long)s_nMembersNull,
	        (unsigned long long)c2.combatResultsSet, (unsigned long long)c2.combatResultsLate, (long long)s_nCombatGameReads,
	        (unsigned long long)c2.playerRowsChanged, (unsigned long long)c2.playerRowsSame,
	        (unsigned long long)c2.playerBadIndex, (long long)s_nPlayerReads, (long long)s_nPlayerReadFails,
	        (long long)s_nNoActiveTeam, (long long)s_nUnitNull, (long long)s_nCombatNull, (unsigned long long)c2.unitDbSets);
	// issue 4726
	fprintf(f, "feed combats (4726): interceptors %lld, per-combat damage read %lld, not read (battle-damage event on or "
	           "not known) %lld; CustomModOptions reads %lld, not known %lld\n",
	        (long long)s_nInterceptors, (long long)s_nDamageRead, (long long)s_nDamageGated, (long long)s_nModOptReads,
	        (long long)s_nModOptFails);
	// ticket 5062
	fprintf(f, "feed combats (5062): attacking city plots read %lld, outside the grid %lld\n", (long long)s_nCityPlots,
	        (long long)s_nCityPlotsOutside);
	fprintf(f, "feed units per kind:");
	for(int k = SrvFeed::kUnitCreated; k <= SrvFeed::kEraChanged; ++k)
		fprintf(f, " %d=%llu", k, (unsigned long long)c2.perKind[k]);
	fprintf(f, "; unit calls by thread: ui %lld, core %lld, aux %lld, other %lld; inside Update %lld\n",
	        (long long)s_nUnitCalls[0], (long long)s_nUnitCalls[1], (long long)s_nUnitCalls[2], (long long)s_nUnitCalls[3],
	        (long long)s_nUnitCallsInUpdate);
}

// Issue 0439 (MC #2908; srv_feed.h EmbarkArtBits): kind 21's a[3] for an embark, read through pure getters
// (proxy/model/pure-methods.tsv): the owner's team's all-water passage and the owner's embarked graphic override. The
// owner's team comes from its feed row (ReadUnit's OwnerRow has just filled it). 0 when a read fails (counted).
static volatile LONG64 s_nEmbarkArtReads = 0, s_nEmbarkArtFails = 0;
static uint32_t EmbarkArtOf(int iOwner)
{
	SrvFeed::PlayerRecord pr;
	ICvGameContext1* pCtx = SrvRemote::Context();
	if(iOwner < 0 || iOwner > 63 || !pCtx || !SrvFeed::GetPlayerRow(iOwner, pr) || pr.team < 0)
	{
		InterlockedIncrement64(&s_nEmbarkArtFails);
		return 0;
	}
	ICvGame1* pGame = pCtx->GetGame();
	ICvTeam1* pTeam = pCtx->GetTeam((TeamTypes)pr.team);
	ICvPlayer1* pPlayer = pGame ? pGame->GetPlayer((PlayerTypes)iOwner) : NULL;
	uint32_t bits = 0;
	if(pTeam && pPlayer)
	{
		bits = SrvFeed::kEmbarkArtRead;
		if(pTeam->CanEmbarkAllWaterPassage())
			bits |= SrvFeed::kEmbarkArtAllWater;
		const char* sz = pPlayer->GetEmbarkedGraphicOverride();
		if(sz && *sz)
			bits |= SrvFeed::kEmbarkArtOverride | (SrvFeed::EmbarkArtHash24(sz) << SrvFeed::kEmbarkArtHashShift);
		InterlockedIncrement64(&s_nEmbarkArtReads);
	}
	else
		InterlockedIncrement64(&s_nEmbarkArtFails);
	if(pPlayer)
		pPlayer->Destroy();
	if(pTeam)
		pTeam->Destroy();
	if(pGame)
		pGame->Destroy();
	return bits;
}

// ---- issue 4452 (epic 0375): the unit feed ----
void OnUnit(uint16_t uKind, ICvUnit1* pUnit, int iA2, int iA3)
{
	if(!SrvFeed::UnitsEnabled())
		return;
	try
	{
		CountUnitCall();
		if(!pUnit)
		{
			PushNullUnit(uKind, iA2, iA3);
			return;
		}
		SrvFeed::UnitRecord r;
		SrvFeed::UnitOp op = SrvFeed::UnitOp::kUpsert;
		if(uKind == SrvFeed::kUnitDestroyed)
		{
			// called from CvUnit::~CvUnit: only the key is read; the plot is the record's
			r = SrvFeed::UnitRecord();
			r.owner = I8((int)pUnit->GetOwner());
			r.id = (int32_t)pUnit->GetID();
			SrvFeed::UnitRecord old;
			r.plot = SrvFeed::GetUnit(r.owner, r.id, old) ? old.plot : -1;
			op = SrvFeed::UnitOp::kRemove;
		}
		else
			ReadUnit(pUnit, r);
		SrvFeed::Event e = UnitEvent(uKind, r);
		e.a[2] = uKind == SrvFeed::kUnitCreated ? r.unitType : iA2;
		e.a[3] = iA3;
		if(uKind == SrvFeed::kUnitEmbark && iA2)
			e.a[3] = (int32_t)EmbarkArtOf(r.owner);   // issue 0439 (MC #2908): the embarked art's inputs at the call
		SrvFeed::PushUnit(e, &r, op);
	}
	catch(...)
	{
		NoteException("a unit call (GameplayUnit*)");
	}
}

void OnUnitMoved(ICvUnit1* pUnit, const int* piPlots, uint32_t n)
{
	if(!SrvFeed::UnitsEnabled())
		return;
	try
	{
		CountUnitCall();
		if(!pUnit)
		{
			PushNullUnit(SrvFeed::kUnitMoved, -1, (int)n);
			return;
		}
		SrvFeed::UnitRecord r;
		ReadUnit(pUnit, r);
		SrvFeed::PushUnitPath(UnitEvent(SrvFeed::kUnitMoved, r), &r, SrvFeed::UnitOp::kUpsert,
		                      reinterpret_cast<const int32_t*>(piPlots), piPlots ? n : 0);
	}
	catch(...)
	{
		NoteException("GameplayUnitMoved");
	}
}

void OnUnitPlot(uint16_t uKind, ICvUnit1* pUnit, ICvPlot1* pFrom, ICvPlot1* pTo)
{
	if(!SrvFeed::UnitsEnabled())
		return;
	try
	{
		CountUnitCall();
		const int iFrom = PlotIndex(pFrom), iTo = PlotIndex(pTo);
		const int a2 = uKind == SrvFeed::kUnitRebased ? iFrom : iTo, a3 = uKind == SrvFeed::kUnitRebased ? iTo : 0;
		if(!pUnit)
		{
			PushNullUnit(uKind, a2, a3);
			return;
		}
		SrvFeed::UnitRecord r;
		ReadUnit(pUnit, r);
		SrvFeed::Event e = UnitEvent(uKind, r);
		e.a[2] = a2;
		e.a[3] = a3;
		SrvFeed::PushUnit(e, &r, SrvFeed::UnitOp::kUpsert);
	}
	catch(...)
	{
		NoteException("GameplayUnitTeleported / GameplayUnitRebased");
	}
}

void OnUnitVisibility(ICvUnit1* pUnit, bool bVisible, bool bCheck, float fBlendTime)
{
	if(!SrvFeed::UnitsEnabled())
		return;
	try
	{
		int32_t iBlend = 0;
		memcpy(&iBlend, &fBlendTime, sizeof(iBlend));
		const int a2 = (bVisible ? 1 : 0) + (bCheck ? 2 : 0);
		CountUnitCall();
		if(!pUnit)
		{
			PushNullUnit(SrvFeed::kUnitVisibility, a2, iBlend);
			return;
		}
		SrvFeed::UnitRecord r;
		ReadUnit(pUnit, r);
		SrvFeed::Event e = UnitEvent(SrvFeed::kUnitVisibility, r);
		e.a[2] = a2;
		e.a[3] = iBlend;
		SrvFeed::PushUnit(e, &r, SrvFeed::UnitOp::kUpsert);
	}
	catch(...)
	{
		NoteException("GameplayUnitVisibility");
	}
}

uint32_t OnCombat(uint16_t uKind, ICvCombatInfo1* pInfo)
{
	if(!SrvFeed::UnitsEnabled())
		return 0;
	try
	{
		CountUnitCall();
		SrvFeed::Event e = MakeEvent(uKind, -1);
		if(!pInfo)
		{
			NoteNull("a combat call", &s_nCombatNull);
			e.a[0] = e.a[1] = e.a[2] = -1;
			return SrvFeed::PushUnit(e, NULL, SrvFeed::UnitOp::kNone);
		}
		SrvFeed::CombatRecord c = SrvFeed::CombatRecord();
		ICvPlot1* pPlot = pInfo->GetPlot();
		c.plot = PlotIndex(pPlot);
		if(pPlot)
			pPlot->Destroy();
		SrvFeed::UnitRecord rec[2];
		bool bRec[2] = {false, false};
		int32_t* pOwner[2] = {&c.attackerOwner, &c.defenderOwner};
		int32_t* pUnitId[2] = {&c.attackerUnit, &c.defenderUnit};
		int32_t* pCityId[2] = {&c.attackerCity, &c.defenderCity};
		int32_t iAttackerCityPlot = -1;   // ticket 5062: kCityCombat's a[3] - 1
		for(int side = 0; side < 2; ++side)
		{
			const BattleUnitTypes bt = side == 0 ? BATTLE_UNIT_ATTACKER : BATTLE_UNIT_DEFENDER;
			*pOwner[side] = *pUnitId[side] = *pCityId[side] = -1;
			ICvUnit1* u = pInfo->GetUnit(bt);
			if(u)
			{
				ReadUnit(u, rec[side]);
				bRec[side] = true;
				*pOwner[side] = rec[side].owner;
				*pUnitId[side] = rec[side].id;
				u->Destroy();
			}
			ICvCity1* pCity = pInfo->GetCity(bt);
			if(pCity)
			{
				if(!u)
					*pOwner[side] = (int32_t)pCity->GetOwner();
				*pCityId[side] = (int32_t)pCity->GetID();
				// ticket 5062: the ATTACKING city's plot (its strike's origin), the only side whose plot the record lacks
				// (a defending city stands on the combat's plot). GetX / GetY: inline reads of m_iX / m_iY (audit in the
				// header comment); the index as OnFowChanged makes it, on the feed's own grid
				if(side == 0 && uKind == SrvFeed::kCityCombat)
				{
					const int iX = pCity->GetX(), iY = pCity->GetY();
					uint32_t w = 0, h = 0;
					SrvFeed::GetSize(w, h);
					if(iX >= 0 && iY >= 0 && (uint32_t)iX < w && (uint32_t)iY < h)
					{
						iAttackerCityPlot = (int32_t)((uint32_t)iY * w + (uint32_t)iX);
						InterlockedIncrement64(&s_nCityPlots);
					}
					else
						InterlockedIncrement64(&s_nCityPlotsOutside);
				}
				pCity->Destroy();
			}
			c.finalDamage[side] = (int32_t)pInfo->GetFinalDamage(bt);
		}
		uint16_t fl = 0;
		if(pInfo->GetAttackerAdvances()) fl |= SrvFeed::kCombatAttackerAdvances;
		if(pInfo->GetDefenderRetaliates()) fl |= SrvFeed::kCombatDefenderRetaliates;
		if(pInfo->GetAttackIsRanged()) fl |= SrvFeed::kCombatRanged;
		if(pInfo->GetAttackIsBombingMission()) fl |= SrvFeed::kCombatBombing;
		if(pInfo->GetAttackIsAirSweep()) fl |= SrvFeed::kCombatAirSweep;
		if(pInfo->GetDefenderCaptured()) fl |= SrvFeed::kCombatDefenderCaptured;
		if(pInfo->GetVisualizeCombat()) fl |= SrvFeed::kCombatVisualize;
		fl |= CombatGameFlags();
		c.flags = fl;
		c.nuclearLevel = (int32_t)pInfo->GetNuclearDamageLevel();
		// the damage members (CvStructs.h CvCombatMemberEntry), at most GetMaxDamageMemberCount and 256
		int n = pInfo->GetDamageMemberCount();
		const int nMax = pInfo->GetMaxDamageMemberCount();
		const int nCap = nMax < 256 ? nMax : 256;
		if(n > nCap)
		{
			InterlockedIncrement64(&s_nMembersCut);
			n = nCap;
		}
		SrvFeed::CombatMember m[257];   // issue 4726: 256 damage members, then the interceptor
		uint32_t nm = 0;
		for(int i = 0; i < n; ++i)
		{
			const CvCombatMemberEntry* p = pInfo->GetDamageMember(i);
			if(!p)
			{
				InterlockedIncrement64(&s_nMembersNull);
				continue;
			}
			SrvFeed::CombatMember& x = m[nm++];
			x.player = (int32_t)p->m_ePlayer;
			x.id = p->m_iID;
			x.flags = (uint16_t)p->m_bfFlags;
			x.damage = p->m_iDamage;
			x.finalDamage = p->m_iFinalDamage;
			x.maxHP = p->m_iMaxHitPoints;
			x.x = p->m_iX;
			x.y = p->m_iY;
			x.combatType = (int32_t)p->m_eCombatType;
			x.era = (int32_t)p->m_eEra;
		}
		// issue 4726 (unit-combat-server.md 12): the per-combat damage, only while GetDamageInflicted is a plain read
		// (SrvFeed::kCombatDamageKnown: the attacker's = the defender's damage dealt, and the reverse; the EXE's mapping)
		const bool bDamage = BattleDamageEventOff();
		if(bDamage)
		{
			c.reserved = SrvFeed::CombatDamageWords((int32_t)pInfo->GetDamageInflicted(BATTLE_UNIT_DEFENDER),
			                                        (int32_t)pInfo->GetDamageInflicted(BATTLE_UNIT_ATTACKER));
			c.flags |= SrvFeed::kCombatDamageKnown;
			InterlockedIncrement64(&s_nDamageRead);
		}
		else
			InterlockedIncrement64(&s_nDamageGated);
		// issue 4726: the interceptor (participant 2), appended as a member flagged kCombatMemberInterceptor
		if(ICvUnit1* pI = pInfo->GetUnit(BATTLE_UNIT_INTERCEPTOR))
		{
			// four plain getters of the pure list (not ReadUnit: the interceptor's table record is not refreshed here)
			SrvFeed::CombatMember& x = m[nm++];
			x.player = (int32_t)pI->GetOwner();
			x.id = pI->GetID();
			const int iDmg = pI->GetDamage(), iMax = pI->GetMaxHitPoints();
			pI->Destroy();
			x.flags = SrvFeed::kCombatMemberInterceptor;
			const int iDealt = bDamage ? (int)pInfo->GetDamageInflicted(BATTLE_UNIT_INTERCEPTOR) : 0;
			x.damage = (int16_t)(iDealt < 0 ? 0 : iDealt > 32767 ? 32767 : iDealt);
			x.finalDamage = (int16_t)(iDmg < 0 ? 0 : iDmg > 32767 ? 32767 : iDmg);
			x.maxHP = (int16_t)(iMax < 0 ? 0 : iMax > 32767 ? 32767 : iMax);
			x.x = x.y = -1;
			x.combatType = -1;
			x.era = -1;
			c.flags |= SrvFeed::kCombatInterceptorMember;
			InterlockedIncrement64(&s_nInterceptors);
		}
		e.plot = c.plot;
		if(uKind == SrvFeed::kCityCombat)
		{
			const int side = c.attackerCity >= 0 ? 0 : c.defenderCity >= 0 ? 1 : -1;
			e.a[0] = side < 0 ? -1 : *pOwner[side];
			e.a[1] = side < 0 ? -1 : *pCityId[side];
			e.a[3] = c.attackerCity >= 0 && iAttackerCityPlot >= 0 ? iAttackerCityPlot + 1 : 0;   // ticket 5062
		}
		else
		{
			e.a[0] = c.attackerUnit >= 0 ? c.attackerOwner : -1;
			e.a[1] = c.attackerUnit;
		}
		return SrvFeed::PushCombat(e, c, m, nm, bRec[0] ? &rec[0] : NULL, bRec[1] ? &rec[1] : NULL);
	}
	catch(...)
	{
		NoteException("GameplayUnitCombat / GameplayCityCombat");
		return 0;
	}
}

void OnCombatResult(uint32_t uSeq, unsigned int uReturned)
{
	if(!uSeq || !SrvFeed::UnitsEnabled())
		return;
	SrvFeed::SetCombatResult(uSeq, (uint32_t)uReturned);
}

void OnCityEvent(uint16_t uKind, ICvCity1* pCity, int iA2, int iA3)
{
	if(!SrvFeed::UnitsEnabled())
		return;
	try
	{
		SrvFeed::Event e = MakeEvent(uKind, -1);
		if(!pCity)
			NoteNull("a city call", &s_nUnitNull);
		e.a[0] = pCity ? (int32_t)pCity->GetOwner() : -1;
		e.a[1] = pCity ? (int32_t)pCity->GetID() : -1;
		e.a[2] = iA2;
		e.a[3] = iA3;
		SrvFeed::PushUnit(e, NULL, SrvFeed::UnitOp::kNone);
	}
	catch(...)
	{
		NoteException("GameplayCitySetDamage / GameplayCityPopulationChanged");
	}
}

void OnEraChanged(int iPlayer, int iEra)
{
	if(!SrvFeed::UnitsEnabled())
		return;
	try
	{
		SrvFeed::Event e = MakeEvent(SrvFeed::kEraChanged, -1);
		e.a[0] = iPlayer;
		e.a[1] = iEra;
		SrvFeed::PushUnit(e, NULL, SrvFeed::UnitOp::kNone);
		// the player row's era from the call (no getter needed; the next read confirms it)
		SrvFeed::PlayerRecord r;
		if(SrvFeed::GetPlayerRow(iPlayer, r))
		{
			r.era = iEra;
			SrvFeed::SetPlayer(r);
		}
	}
	catch(...)
	{
		NoteException("GameplayEraChanged");
	}
}

bool OnSetupGraphical(int iPlayer)
{
	if(!SrvFeed::UnitsEnabled())
		return false;
	try
	{
		const LONG prev = InterlockedExchange(&s_lastSetupPlayer, (LONG)iPlayer);
		if(iPlayer > (int)prev)
			return false;
		SrvFeed::ResetUnitsAt(iPlayer);
		InterlockedExchange(&s_playersStale, 1);
		const LONG64 n = InterlockedIncrement64(&s_nLoads);
		SrvLog("feed: units: load %lld begins at ICvPlayer1::SetupGraphical(%d): kLoadBegin pushed, unit table emptied",
		       (long long)n, iPlayer);
		return true;
	}
	catch(...)
	{
		NoteException("ICvPlayer1::SetupGraphical (load mark)");
		return false;
	}
}
}  // namespace SrvRemoteFeed
