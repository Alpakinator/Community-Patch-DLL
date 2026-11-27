// srv_remote_objects.cpp - the engine objects of the server's remote mode (epic 2300, task 2303): one remote object
// per engine handle (EngineWrap, cached, so pointer identity holds for the DLL), and the hand-written subclasses of
// the generated RemoteGen_<I> classes. Design: research/03-dll-engine-boundary/proxy-design.md 11.2, 11.3.
//
// Server-local in remote mode (never cross): the game-core lock (Get/Release/Has/TryGameCoreLock: a recursive lock,
// depth per thread, as the direct server), IsGameCoreThread (true only inside the dispatch of ICvGame1::Update),
// IsGameCoreExecuting (false). Everything else of the engine interfaces crosses (send* included: the engine loops
// them back; the direct server's loopback does not run in remote mode).
#include "srv_remote.h"
#include "srv_remote_feed.h"   // issue 3303: the renderer's event feed
#include "feed/srv_feed.h"
#include "vp_ids.gen.h"
#include "server_hooks.gen.h"
#include <map>
#include <unordered_map>

namespace
{
	CRITICAL_SECTION s_csCore;
	__declspec(thread) int t_iLockDepth = 0;
	// Issue 2700: the canonical objects are made with the DLL wrappers' lock calls suppressed (they stand for no
	// lock; the proxy's want word carries the lock of each local wrapper), and the explicit depth of this thread.
	__declspec(thread) bool t_bSuppressLock = false;
	__declspec(thread) int t_iExplicit = 0;
	__declspec(thread) bool t_bYielded = false;
	volatile LONG* s_pWant = NULL;
	// D30 conditions 2 and 4 (2026-11-10): per channel, the DLL -> engine calls this server thread made (crossing or
	// answered here), published in want words 4..6 so the proxy can see that a read it wants to keep made none; and
	// DLL -> engine calls from a thread that is not inside a served call (must stay 0: no DLL code runs outside one).
	__declspec(thread) int t_iServed = 0;
	volatile long s_lOutside = 0;
	volatile long s_lDllLockCalls = 0, s_lDllLockFirst = 0, s_lSyncFirst = 0;
	// Per channel (ui, core, aux, other thread): the DLL's lock calls and releases, the first-level acquisitions (the
	// ones that can wait) and the lock-want word's acquisitions and releases (MC 2026-11-10: reported per thread).
	volatile long s_lc[4][6] = {};  // 0 DLL get, 1 DLL get first-level, 2 DLL release, 3 want acquire, 4 want acquire
	                                // first-level, 5 want release
	inline int LockRow()
	{
		const int ch = SrvRemote::ThreadChannel();
		return ch >= 0 && ch < 3 ? ch : 3;
	}
	bool s_bOutsideTestDone = false;
	ICvEngineScriptSystem1* volatile s_pScriptSystem = NULL;
	volatile long s_lScriptLocal = 0, s_lScriptVerified = 0, s_lScriptMismatch = 0;
	long s_lLockSyncAcq = 0, s_lLockSyncRel = 0;
	// Issue 2801: lock-count disagreements, each failing the run (LockSync).
	volatile long s_lProxyLockErrorsSeen = 0, s_lWantOutOfRange = 0, s_lReleaseUnderflow = 0;

	class RemoteEngineUtility : public RemoteGen_ICvEngineUtility4
	{
	public:
		explicit RemoteEngineUtility(vpipc::Handle h) : RemoteGen_ICvEngineUtility4(h) {}
		void GetGameCoreLock() override
		{
			if(t_bSuppressLock)
				return;
			SrvRemote::NoteEngineCall();
			EnterCriticalSection(&s_csCore);
			InterlockedIncrement(&s_lDllLockCalls);
			InterlockedIncrement(&s_lc[LockRow()][0]);
			if(t_iLockDepth == 0)
			{
				InterlockedIncrement(&s_lDllLockFirst);
				InterlockedIncrement(&s_lc[LockRow()][1]);
			}
			++t_iLockDepth;
		}
		void ReleaseGameCoreLock() override
		{
			if(t_bSuppressLock)
				return;
			SrvRemote::NoteEngineCall();
			if(t_iLockDepth > 0)
			{
				--t_iLockDepth;
				InterlockedIncrement(&s_lc[LockRow()][2]);
				LeaveCriticalSection(&s_csCore);
			}
		}
		bool TryGameCoreLock() override
		{
			SrvRemote::NoteEngineCall();
			if(!TryEnterCriticalSection(&s_csCore))
				return false;
			++t_iLockDepth;
			return true;
		}
		bool HasGameCoreLock() override
		{
			SrvRemote::NoteEngineCall();
			return t_iLockDepth > 0;
		}
		// Issue 2700 (item 3): the EXE's GetScriptSystem returns one static object for the whole process (0x2DD2034,
		// made on first use, freed at exit; issue 2702 [C]). With the proxy's local answers on, the first answer is
		// kept and every later call is answered here; with local_verify=1 every call still crosses and is compared.
		ICvEngineScriptSystem1* GetScriptSystem() override
		{
			SrvRemote::NoteEngineCall();
			if(!SrvRemote::LocalOn())
				return RemoteGen_ICvEngineUtility4::GetScriptSystem();
			ICvEngineScriptSystem1* pKept = s_pScriptSystem;
			if(pKept && !SrvRemote::LocalVerify())
			{
				InterlockedIncrement(&s_lScriptLocal);
				return pKept;
			}
			ICvEngineScriptSystem1* p = RemoteGen_ICvEngineUtility4::GetScriptSystem();
			if(pKept)
			{
				InterlockedIncrement(&s_lScriptVerified);
				if(p != pKept && InterlockedIncrement(&s_lScriptMismatch) <= 5)
					SrvLog("local: LOUD: GetScriptSystem returned %p, the kept answer is %p", (void*)p, (void*)pKept);
			}
			else if(p)
				s_pScriptSystem = p;
			return p;
		}
		bool IsGameCoreThread() override
		{
			SrvRemote::NoteEngineCall();
			return SrvRemote::InUpdate();
		}
		bool IsGameCoreExecuting() override
		{
			SrvRemote::NoteEngineCall();
			return false;
		}
		// The engine's cache folder (forwarded) is where the DLL writes stats.db and, on a load, the save's embedded
		// Civ5SavedGameDatabase.db through FIFileSystem::Create: the server's file system must accept it as a second
		// write root (integration 2026-11-10: refused, Game::Read went on misaligned and Map::Read crashed).
		const char* GetCacheFolderPath() override
		{
			const char* sz = RemoteGen_ICvEngineUtility4::GetCacheFolderPath();
			if(sz && *sz)
				SrvSetExtraWriteRoot(sz);
			return sz;
		}
		// GameplayUnitMoved(ICvUnit1*, const CvPlotIndexVector&) (issue 2503): the vector crosses element-wise; the
		// proxy rebuilds an FFastVector-layout view over its own buffer (EngHook_ICvEngineUtility4_GameplayUnitMoved,
		// proxy_hooks_human.cpp). CALL: self, handle unit (temporary), u32 n, n x i32 plot index. RETURN: nothing.
		void GameplayUnitMoved(ICvUnit1* pThisUnit, const CvPlotIndexVector& kPlots) override
		{
			// issue 4452: the unit feed (kind 12) before the crossing; the whole path (the side table keeps 65,536)
			SrvRemoteFeed::OnUnitMoved(pThisUnit, kPlots.size() ? &kPlots[0] : NULL, (uint32_t)kPlots.size());
			const uint32_t n = kPlots.size() > 65536u ? 65536u : (uint32_t)kPlots.size();
			vpipc::Channel& ch = SrvRt::Chan();
			vpipc::Writer& w = SrvRt::Begin(ch, kM_ICvEngineUtility4_GameplayUnitMoved);
			w.handle(m_h);
			w.handle(SrvRt::RegisterTemp(pThisUnit, kT_ICvUnit2));
			w.u32(n);
			for(uint32_t i = 0; i < n; ++i)
				w.i32(kPlots[i]);
			vpipc::Reader r;
			if(SrvRt::End(ch, kM_ICvEngineUtility4_GameplayUnitMoved, r))
				SrvRt::DecodeCheck(kM_ICvEngineUtility4_GameplayUnitMoved, r);
		}

		// Issue 3303 (epic 3300): the renderer's event feed. Each override hands the notification to the feed
		// (srv_remote_feed.cpp: pure getters only, nothing when the feed is off) and then forwards it unchanged, in the
		// same order, through the generated crossing: what the EXE receives and when does not change.
		void GameplayPlotStateChange(const ICvPlot1* pThisPlot, const ResourceTypes eRevealedResource,
		                             const ImprovementTypes eRevealedImprovement, const int iRevealedImprovementState,
		                             const RouteTypes eRevealedRoute, const int iRouteState) override
		{
			SrvRemoteFeed::OnPlotStateChange(pThisPlot, eRevealedResource, eRevealedImprovement,
			                                 iRevealedImprovementState, eRevealedRoute, iRouteState);
			RemoteGen_ICvEngineUtility4::GameplayPlotStateChange(pThisPlot, eRevealedResource, eRevealedImprovement,
			                                                     iRevealedImprovementState, eRevealedRoute, iRouteState);
		}
		void GameplayFeatureChanged(ICvPlot1* pThisPlot, FeatureTypes newFeature) override
		{
			SrvRemoteFeed::OnFeatureChanged(pThisPlot, newFeature);
			RemoteGen_ICvEngineUtility4::GameplayFeatureChanged(pThisPlot, newFeature);
		}
		void GameplayFOWChanged(int x, int y, int eFOWType, bool bSetAll) override
		{
			SrvRemoteFeed::OnFowChanged(x, y, eFOWType, bSetAll);
			RemoteGen_ICvEngineUtility4::GameplayFOWChanged(x, y, eFOWType, bSetAll);
		}
		void GameplayCityCreated(ICvCity1* pThisCity, EraTypes eCurrentEra) override
		{
			SrvRemoteFeed::OnCity(SrvFeed::kCityCreated, pThisCity, eCurrentEra);
			RemoteGen_ICvEngineUtility4::GameplayCityCreated(pThisCity, eCurrentEra);
		}
		void GameplayCityDestroyed(ICvCity1* pThisCity, PlayerTypes eNewOwner) override
		{
			SrvRemoteFeed::OnCity(SrvFeed::kCityDestroyed, pThisCity, eNewOwner);
			RemoteGen_ICvEngineUtility4::GameplayCityDestroyed(pThisCity, eNewOwner);
		}
		void GameplayCityCaptured(ICvCity1* pThisCity, PlayerTypes eNewOwner) override
		{
			SrvRemoteFeed::OnCity(SrvFeed::kCityCaptured, pThisCity, eNewOwner);
			RemoteGen_ICvEngineUtility4::GameplayCityCaptured(pThisCity, eNewOwner);
		}
		void GameplayNaturalWonderRevealed(ICvPlot1* pThisPlot) override
		{
			SrvRemoteFeed::OnPlotOnly(SrvFeed::kNaturalWonder, pThisPlot);
			RemoteGen_ICvEngineUtility4::GameplayNaturalWonderRevealed(pThisPlot);
		}
		void GameplayWallCreated(ICvPlot1* pThisPlot) override
		{
			SrvRemoteFeed::OnPlotOnly(SrvFeed::kWallCreated, pThisPlot);
			RemoteGen_ICvEngineUtility4::GameplayWallCreated(pThisPlot);
		}
		void GameplayYieldMightHaveChanged(ICvPlot1* pThisPlot) override
		{
			SrvRemoteFeed::OnPlotOnly(SrvFeed::kYieldChanged, pThisPlot);
			RemoteGen_ICvEngineUtility4::GameplayYieldMightHaveChanged(pThisPlot);
		}

		// Issue 4452 (epic 0375): the unit feed (unit-server-scene.md 2.1, kinds 11-31 and 33). Each override hands the
		// call to the feed (srv_remote_feed.cpp: pure getters only, nothing unless render_units=1) and then forwards it
		// unchanged, in the same order: what the EXE receives and when does not change. The combat information is
		// borrowed by the EXE's handler: the feed reads it before the base call; the returned value goes into the
		// combat record after it (Q7).
		void GameplayUnitCreated(ICvUnit1* pThisUnit) override
		{
			SrvRemoteFeed::OnUnit(SrvFeed::kUnitCreated, pThisUnit, 0, 0);   // a[2] = the unit type, read in the feed
			RemoteGen_ICvEngineUtility4::GameplayUnitCreated(pThisUnit);
		}
		void GameplayUnitTeleported(ICvUnit1* pThisUnit, ICvPlot1* pThisPlot) override
		{
			SrvRemoteFeed::OnUnitPlot(SrvFeed::kUnitTeleported, pThisUnit, NULL, pThisPlot);
			RemoteGen_ICvEngineUtility4::GameplayUnitTeleported(pThisUnit, pThisPlot);
		}
		void GameplayUnitDestroyed(ICvUnit1* pThisUnit) override
		{
			SrvRemoteFeed::OnUnit(SrvFeed::kUnitDestroyed, pThisUnit, 0, 0);
			RemoteGen_ICvEngineUtility4::GameplayUnitDestroyed(pThisUnit);
		}
		void GameplayUnitDestroyedInCombat(ICvUnit1* pUnit) override
		{
			SrvRemoteFeed::OnUnit(SrvFeed::kUnitDestroyedInCombat, pUnit, 0, 0);
			RemoteGen_ICvEngineUtility4::GameplayUnitDestroyedInCombat(pUnit);
		}
		void GameplayUnitSetDamage(ICvUnit1* pThisUnit, int iDamage, int iPreviousDamage) override
		{
			SrvRemoteFeed::OnUnit(SrvFeed::kUnitSetDamage, pThisUnit, iDamage, iPreviousDamage);
			RemoteGen_ICvEngineUtility4::GameplayUnitSetDamage(pThisUnit, iDamage, iPreviousDamage);
		}
		void GameplayUnitFortify(ICvUnit1* pThisUnit, bool bFortify) override
		{
			SrvRemoteFeed::OnUnit(SrvFeed::kUnitFortify, pThisUnit, bFortify ? 1 : 0, 0);
			RemoteGen_ICvEngineUtility4::GameplayUnitFortify(pThisUnit, bFortify);
		}
		void GameplayUnitWork(ICvUnit1* pThisUnit, int iWorkType) override
		{
			SrvRemoteFeed::OnUnit(SrvFeed::kUnitWork, pThisUnit, iWorkType, 0);
			RemoteGen_ICvEngineUtility4::GameplayUnitWork(pThisUnit, iWorkType);
		}
		void GameplayUnitParadrop(ICvUnit1* pThisUnit) override
		{
			SrvRemoteFeed::OnUnit(SrvFeed::kUnitParadrop, pThisUnit, 0, 0);
			RemoteGen_ICvEngineUtility4::GameplayUnitParadrop(pThisUnit);
		}
		void GameplayUnitActivate(ICvUnit1* pThisUnit) override
		{
			SrvRemoteFeed::OnUnit(SrvFeed::kUnitActivate, pThisUnit, 0, 0);
			RemoteGen_ICvEngineUtility4::GameplayUnitActivate(pThisUnit);
		}
		void GameplayUnitEmbark(ICvUnit1* pThisUnit, bool bEmbark) override
		{
			SrvRemoteFeed::OnUnit(SrvFeed::kUnitEmbark, pThisUnit, bEmbark ? 1 : 0, 0);
			RemoteGen_ICvEngineUtility4::GameplayUnitEmbark(pThisUnit, bEmbark);
		}
		void GameplayUnitGarrison(ICvUnit1* pThisUnit, bool bGarrison) override
		{
			SrvRemoteFeed::OnUnit(SrvFeed::kUnitGarrison, pThisUnit, bGarrison ? 1 : 0, 0);
			RemoteGen_ICvEngineUtility4::GameplayUnitGarrison(pThisUnit, bGarrison);
		}
		void GameplayUnitShouldDimFlag(ICvUnit1* pThisUnit, bool bDim) override
		{
			SrvRemoteFeed::OnUnit(SrvFeed::kUnitShouldDimFlag, pThisUnit, bDim ? 1 : 0, 0);
			RemoteGen_ICvEngineUtility4::GameplayUnitShouldDimFlag(pThisUnit, bDim);
		}
		void GameplayUnitVisibility(ICvUnit1* pThisUnit, bool bVisible, bool bChangeInvisibility,
		                            const float fBlendTime) override
		{
			SrvRemoteFeed::OnUnitVisibility(pThisUnit, bVisible, bChangeInvisibility, fBlendTime);
			RemoteGen_ICvEngineUtility4::GameplayUnitVisibility(pThisUnit, bVisible, bChangeInvisibility, fBlendTime);
		}
		uint GameplayUnitCombat(ICvCombatInfo1* pCombatInfo) override
		{
			const uint32_t uSeq = SrvRemoteFeed::OnCombat(SrvFeed::kUnitCombat, pCombatInfo);
			const uint r = RemoteGen_ICvEngineUtility4::GameplayUnitCombat(pCombatInfo);
			SrvRemoteFeed::OnCombatResult(uSeq, r);
			return r;
		}
		void GameplayCitySetDamage(ICvCity1* pThisCity, int iDamage, int iPreviousDamage) override
		{
			SrvRemoteFeed::OnCityEvent(SrvFeed::kCitySetDamage, pThisCity, iDamage, iPreviousDamage);
			RemoteGen_ICvEngineUtility4::GameplayCitySetDamage(pThisCity, iDamage, iPreviousDamage);
		}
		void GameplayCityPopulationChanged(ICvCity1* pThisCity, int newPopulation) override
		{
			SrvRemoteFeed::OnCityEvent(SrvFeed::kCityPopulationChanged, pThisCity, newPopulation, 0);
			RemoteGen_ICvEngineUtility4::GameplayCityPopulationChanged(pThisCity, newPopulation);
		}
		uint GameplayCityCombat(ICvCombatInfo1* pInfo) override
		{
			const uint32_t uSeq = SrvRemoteFeed::OnCombat(SrvFeed::kCityCombat, pInfo);
			const uint r = RemoteGen_ICvEngineUtility4::GameplayCityCombat(pInfo);
			SrvRemoteFeed::OnCombatResult(uSeq, r);
			return r;
		}
		void GameplayUnitMissionEnd(ICvUnit1* pThisUnit) override
		{
			SrvRemoteFeed::OnUnit(SrvFeed::kUnitMissionEnd, pThisUnit, 0, 0);
			RemoteGen_ICvEngineUtility4::GameplayUnitMissionEnd(pThisUnit);
		}
		void GameplayUnitRebased(ICvUnit1* pThisUnit, ICvPlot1* pOldPlot, ICvPlot1* pThisPlot) override
		{
			SrvRemoteFeed::OnUnitPlot(SrvFeed::kUnitRebased, pThisUnit, pOldPlot, pThisPlot);
			RemoteGen_ICvEngineUtility4::GameplayUnitRebased(pThisUnit, pOldPlot, pThisPlot);
		}
		void GameplayUnitResetAnimationState(ICvUnit1* pThisUnit) override
		{
			SrvRemoteFeed::OnUnit(SrvFeed::kUnitResetAnimationState, pThisUnit, 0, 0);
			RemoteGen_ICvEngineUtility4::GameplayUnitResetAnimationState(pThisUnit);
		}
		void GameplayEraChanged(PlayerTypes playerID, EraTypes newEra) override   // MC #2105 Q6: kind 33
		{
			SrvRemoteFeed::OnEraChanged((int)playerID, (int)newEra);
			RemoteGen_ICvEngineUtility4::GameplayEraChanged(playerID, newEra);
		}
	};

	// Local answers: none (proxy-design 11.2: everything crosses). The hand-written methods below carry what the
	// generator cannot: AddPopup(const CvPopupInfo&) (9008), the selected-cities walker and GetSelectedUnits (2503).
	class RemoteUserInterface : public RemoteGen_ICvUserInterface2
	{
	public:
		explicit RemoteUserInterface(vpipc::Handle h) : RemoteGen_ICvUserInterface2(h) {}
		using RemoteGen_ICvUserInterface2::AddPopup;
		// AddPopup(const CvPopupInfo&) (issue 9008): the struct crosses by value, field by field in declaration order
		// (CvGameCoreDLLUtil/include/CvStructs.h: iData1-3, iFlags, bOption1, bOption2, eButtonPopupType, szText[512]);
		// the proxy rebuilds it and calls the engine (EngHook_ICvUserInterface2_AddPopup_s45, proxy_hooks.cpp).
		void AddPopup(const CvPopupInfo& kPopup) override
		{
			vpipc::Channel& ch = SrvRt::Chan();
			vpipc::Writer& w = SrvRt::Begin(ch, kM_ICvUserInterface2_AddPopup_s45);
			w.handle(m_h);
			w.i32(kPopup.iData1);
			w.i32(kPopup.iData2);
			w.i32(kPopup.iData3);
			w.i32(kPopup.iFlags);
			w.b(kPopup.bOption1);
			w.b(kPopup.bOption2);
			w.i32(static_cast<int32_t>(kPopup.eButtonPopupType));
			w.str(kPopup.szText, (uint32_t)strnlen(kPopup.szText, sizeof(kPopup.szText) - 1));
			vpipc::Reader r;
			if(SrvRt::End(ch, kM_ICvUserInterface2_AddPopup_s45, r))
				SrvRt::DecodeCheck(kM_ICvUserInterface2_AddPopup_s45, r);
		}

		// headSelectedCitiesNode / nextSelectedCitiesNode (issue 2503): the DLL walks the engine's selected-cities list
		// by IDInfo* nodes [C CvGame.cpp:2877-2882]. headSelectedCitiesNode fetches the whole list (CALL: self.
		// RETURN: u32 n, n x (i32 eOwner, i32 iID); EngHook_ICvUserInterface2_headSelectedCitiesNode) into a snapshot
		// per server thread; the next overloads answer from it without a call. A node outside the snapshot -> NULL.
		IDInfo* headSelectedCitiesNode() override
		{
			std::vector<IDInfo>& v = t_cityNodes;
			v.clear();
			vpipc::Channel& ch = SrvRt::Chan();
			vpipc::Writer& w = SrvRt::Begin(ch, kM_ICvUserInterface2_headSelectedCitiesNode);
			w.handle(m_h);
			vpipc::Reader r;
			if(!SrvRt::End(ch, kM_ICvUserInterface2_headSelectedCitiesNode, r))
				return NULL;
			const uint32_t n = r.u32();
			for(uint32_t i = 0; i < n && i < 4096u && r.ok(); ++i)
			{
				IDInfo id;
				id.eOwner = (PlayerTypes)r.i32();
				id.iID = r.i32();
				if(r.ok())
					v.push_back(id);
			}
			SrvRt::DecodeCheck(kM_ICvUserInterface2_headSelectedCitiesNode, r);
			return v.empty() ? NULL : &v[0];
		}
		// Issue 3303: the renderer's feed (owner change of a plot; CvPlot::setOwner), then forwarded unchanged.
		void UpdateCountryBorder(ICvPlot1* pThisPlot) override
		{
			SrvRemoteFeed::OnBorder(pThisPlot);
			RemoteGen_ICvUserInterface2::UpdateCountryBorder(pThisPlot);
		}

		IDInfo* nextSelectedCitiesNode(IDInfo* pNode) override { return const_cast<IDInfo*>(NextCity(pNode)); }
		const IDInfo* nextSelectedCitiesNode(const IDInfo* pNode) const override { return NextCity(pNode); }

		// GetSelectedUnits (issue 2503): the engine's enumerator stays in the engine; the proxy walks it and sends one
		// handle per unit with a server reference taken for it (EngHook_ICvUserInterface2_GetSelectedUnits, VpGenGiveDll).
		// CALL: self. RETURN: u32 n, n x handle. The DLL gets a local enumerator owning those references.
		ICvEnumerator* GetSelectedUnits() override
		{
			vpipc::Channel& ch = SrvRt::Chan();
			vpipc::Writer& w = SrvRt::Begin(ch, kM_ICvUserInterface2_GetSelectedUnits);
			w.handle(m_h);
			vpipc::Reader r;
			if(!SrvRt::End(ch, kM_ICvUserInterface2_GetSelectedUnits, r))
				return NULL;
			const uint32_t n = r.u32();
			std::vector<ICvUnknown*> items;
			for(uint32_t i = 0; i < n && i < 4096u && r.ok(); ++i)
			{
				const vpipc::Handle h = r.handle();
				if(r.ok())
					items.push_back(static_cast<ICvUnknown*>(VpGenAdoptDll(h, kT_ICvUnknown)));
			}
			SrvRt::DecodeCheck(kM_ICvUserInterface2_GetSelectedUnits, r);
			return VpNewLocalEnumerator(items);
		}

	private:
		static thread_local std::vector<IDInfo> t_cityNodes;
		static const IDInfo* NextCity(const IDInfo* pNode)
		{
			SrvRemote::NoteEngineCall();  // issue 2801 (verification 2700 A2): an engine method answered here still counts
			const std::vector<IDInfo>& v = t_cityNodes;
			if(!pNode || v.empty() || pNode < &v[0] || pNode >= &v[0] + v.size())
			{
				if(pNode)
					SrvLog("remote: LOUD: nextSelectedCitiesNode(%p): not a node of this thread's snapshot; NULL",
					       (const void*)pNode);
				return NULL;
			}
			return pNode + 1 < &v[0] + v.size() ? pNode + 1 : NULL;
		}
	};
	thread_local std::vector<IDInfo> RemoteUserInterface::t_cityNodes;

	struct Entry
	{
		void* pMd;		// the object as a pointer to the most derived interface of its kind
		uint32_t kind;
	};
	SRWLOCK s_lock = SRWLOCK_INIT;
	std::map<std::pair<uint32_t, int32_t>, Entry> s_byHandle;	// (type, id) -> object
	std::unordered_map<const void*, vpipc::Handle> s_byPtr;		// object -> handle
	long s_lCreated = 0, s_lForgotten = 0, s_lUnknown = 0;

	void* NewRemote(vpipc::Handle h, uint32_t kind)
	{
		switch(kind)
		{
		case kT_ICvEngineUtility4: return static_cast<ICvEngineUtility4*>(new RemoteEngineUtility(h));
		case kT_ICvUserInterface2: return static_cast<ICvUserInterface2*>(new RemoteUserInterface(h));
		case kT_ICvEngineScriptSystem1: return VpNewRemoteScriptSystem(h);
		case kT_ICvEngineScriptSystemArgs1: return VpNewRemoteArgs(h);
		case kT_CvDLLInterfaceIFaceBase: return static_cast<CvDLLInterfaceIFaceBase*>(new RemoteGen_CvDLLInterfaceIFaceBase(h));
		case kT_FIFileSystem: return static_cast<FIFileSystem*>(new RemoteGen_FIFileSystem(h));
		case kT_FILogFile: return static_cast<FILogFile*>(new RemoteGen_FILogFile(h));
		case kT_FILogFileMgr: return static_cast<FILogFileMgr*>(new RemoteGen_FILogFileMgr(h));
		default: return NULL;
		}
	}
}

namespace SrvRemote
{
void LockInit()
{
	InitializeCriticalSection(&s_csCore);
	CanonInit();  // issue 2700
}

// Issue 2500 (coordinator): the proxy's harness Lua channel yields the lock around a chunk (vp_wire.h kRt_CoreLockYield).
int CoreLockYield()
{
	const int n = t_iLockDepth;
	// issue 2700: no want-word sync while the harness chunk runs without the lock. Issue 2801: only when something was
	// released; the proxy sends no kRt_CoreLockResume for a depth of 0 (proxy_update.cpp VpProxy_CoreLockResume), so a
	// flag set then was never cleared and this thread's want-word sync stayed off for the rest of the run.
	t_bYielded = n > 0;
	for(int i = 0; i < n; ++i)
	{
		--t_iLockDepth;
		LeaveCriticalSection(&s_csCore);
	}
	return n;
}

void CoreLockResume(int iDepth)
{
	for(int i = 0; i < iDepth; ++i)
	{
		EnterCriticalSection(&s_csCore);
		++t_iLockDepth;
	}
	t_bYielded = false;
}

// ---- issue 2700: the lock-want words and canonical objects (proxy-design section 16) --------------------------------
bool LockOpen(uint32_t uProxyPid)
{
	char name[64];
	sprintf_s(name, "%s%u", vpw::kLockShmPrefix, uProxyPid);
	HANDLE h = OpenFileMappingA(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
	if(!h)
	{
		Report("local: no lock-want memory %s (error %lu): the proxy's local answers of GetGame/GetPlayer are off",
		       name, GetLastError());
		return false;
	}
	s_pWant = static_cast<volatile LONG*>(MapViewOfFile(h, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 64));
	Report("local: lock-want memory %s %s", name, s_pWant ? "mapped" : "NOT mapped");
	return s_pWant != NULL;
}

void LockSync(uint32_t uChannel)
{
	if(!s_pWant)
		return;
	// Issue 2801: lock-count errors the proxy found (want word 8: a local lock wrapper released on another thread, a
	// count past 1000) fail the run here, at the next call served on any channel (kRt_Shutdown's included).
	if(s_pWant[8] != 0 && InterlockedCompareExchange(&s_lProxyLockErrorsSeen, 1, 0) == 0)
	{
		char sz[160];
		sprintf_s(sz, "the proxy reported %ld local lock-count error(s) (its log: LOCK ERROR); the server's lock no longer "
		              "matches the EXE's wrappers (issue 2801)", (long)s_pWant[8]);
		Fail(sz);
	}
	if(uChannel > 1 || t_bYielded)
		return;
	const LONG want = s_pWant[uChannel];
	if(want == t_iExplicit)
		return;
	if(want < 0 || want > 1000)
	{
		// Issue 2801: was ignored silently, freezing the depth; a leak of local lock wrappers must be loud.
		if(InterlockedIncrement(&s_lWantOutOfRange) == 1)
		{
			char sz[160];
			sprintf_s(sz, "lock-want word of channel %u is %ld (outside 0..1000): a leak of local lock wrappers; the "
			              "explicit depth stays %d (issue 2801)", uChannel, (long)want, t_iExplicit);
			Fail(sz);
		}
		return;
	}
	while(t_iExplicit < want)
	{
		EnterCriticalSection(&s_csCore);
		if(t_iLockDepth == 0)
		{
			++s_lSyncFirst;
			InterlockedIncrement(&s_lc[LockRow()][4]);
		}
		++t_iLockDepth;
		++t_iExplicit;
		++s_lLockSyncAcq;
		InterlockedIncrement(&s_lc[LockRow()][3]);
	}
	while(t_iExplicit > want)
	{
		--t_iExplicit;
		++s_lLockSyncRel;
		InterlockedIncrement(&s_lc[LockRow()][5]);
		if(t_iLockDepth > 0)
		{
			--t_iLockDepth;
			LeaveCriticalSection(&s_csCore);
		}
		else if(InterlockedIncrement(&s_lReleaseUnderflow) <= 5)
		{
			// Issue 2801 (verification 2700 B2): the DLL's own releases (CallTestAll's one-level release around a Lua
			// hook, CvLuaSupport.cpp) already took the depth to 0 and the engine then dropped the local wrapper that
			// level stood for. The DLL's re-acquire afterwards leaves a level no wrapper accounts for (in process the
			// EXE's wrapper release would have hit the same unbalanced lock). Loud, the run fails.
			char sz[200];
			sprintf_s(sz, "lock-want release on channel %u with the lock depth already 0 (the DLL released the level a "
			              "local wrapper stood for; its re-acquire will leave a level nothing releases; issue 2801)",
			          uChannel);
			SrvLog("local: LOUD: %s", sz);
			Fail(sz);
		}
	}
}

namespace
{
	CRITICAL_SECTION s_csCanon;
	bool s_bCanonInit = false;
	uint32_t s_uCanonGen = 0;
	std::map<std::pair<uint32_t, int32_t>, vpipc::Handle> s_canon;
	ICvGame1* s_pCanonGame = NULL;
	long s_lCanonMade = 0, s_lCanonGens = 0;
	// Issue 2801 (verification 2700 A5): Init / Uninit / CacheGameDatabaseData / FlushGameDatabaseData being served on
	// any server thread (under s_csCanon), the refusals it caused and the number of such calls.
	long s_lGenInflight = 0, s_lCanonRefused = 0, s_lGenChanges = 0;
}

bool GenChangeEnter(uint32_t method)
{
	if(method != kM_ICvGameContext3_Init && method != kM_ICvGameContext3_Uninit &&
	   method != kM_ICvDLLDatabaseUtility1_CacheGameDatabaseData &&
	   method != kM_ICvDLLDatabaseUtility1_FlushGameDatabaseData)
		return false;
	if(!s_bCanonInit)
		return false;
	// Waits for a Canon being built on another thread (it holds s_csCanon while it calls the DLL), so no canonical
	// object is made from the DLL's tables once the DLL starts to free or rebuild them.
	EnterCriticalSection(&s_csCanon);
	++s_lGenInflight;
	++s_lGenChanges;
	s_canon.clear();
	s_pCanonGame = NULL;
	LeaveCriticalSection(&s_csCanon);
	return true;
}

void GenChangeLeave(bool bEntered)
{
	if(!bEntered)
		return;
	EnterCriticalSection(&s_csCanon);
	--s_lGenInflight;
	s_canon.clear();  // nothing built during the call (Canon refused), but the table must not outlive it either
	s_pCanonGame = NULL;
	LeaveCriticalSection(&s_csCanon);
}

// kRt_Canon: the permanent object of (code, index) in this generation, made with the lock suppressed. A new
// generation forgets the table; the old objects stay alive and registered (a wrapper the engine still holds keeps
// working as a stale in-process wrapper would), so no handle is ever reused.
bool Canon(uint32_t uGen, uint32_t uCode, int32_t iIndex, vpipc::Handle& h, std::string& strErr)
{
	MaybeOutsideTest();
	h.type = 0;
	h.id = 0;
	ICvGameContext1* pCtx = Context();
	if(!pCtx)
	{
		strErr = "no context";
		return false;
	}
	if(!s_bCanonInit)
		return strErr = "not initialised", false;
	EnterCriticalSection(&s_csCanon);
	if(s_lGenInflight > 0)
	{
		// Issue 2801: the DLL may be freeing or rebuilding the objects (CacheGameDatabaseData deletes and re-creates
		// every info object); {0, -1} tells the proxy to let the getter cross (a fresh DLL wrapper for that one call).
		++s_lCanonRefused;
		h.id = -1;
		LeaveCriticalSection(&s_csCanon);
		return true;
	}
	if(uGen != s_uCanonGen)
	{
		s_uCanonGen = uGen;
		s_canon.clear();
		s_pCanonGame = NULL;
		++s_lCanonGens;
	}
	const std::pair<uint32_t, int32_t> key(uCode, iIndex);
	std::map<std::pair<uint32_t, int32_t>, vpipc::Handle>::const_iterator it = s_canon.find(key);
	if(it != s_canon.end())
	{
		h = it->second;
		LeaveCriticalSection(&s_csCanon);
		return true;
	}
	bool bOk = true;
	t_bSuppressLock = true;
	const void* p = NULL;
	uint32_t uKind = 0;
	switch(uCode)
	{
	case vpw::kCanonGame:
		p = s_pCanonGame = pCtx->GetGame();
		uKind = kT_ICvGame1;
		break;
	case vpw::kCanonPreGame: p = pCtx->GetPreGame(); uKind = kT_ICvPreGame1; break;
	case vpw::kCanonMap: p = pCtx->GetMap(); uKind = kT_ICvMap1; break;
	case vpw::kCanonGameAsynch: p = pCtx->GetGameAsynch(); uKind = kT_ICvGameAsynch1; break;
	case vpw::kCanonTeam: p = pCtx->GetTeam((TeamTypes)iIndex); uKind = kT_ICvTeam1; break;
	case vpw::kCanonTerrainInfo: p = pCtx->GetTerrainInfo((TerrainTypes)iIndex); uKind = kT_ICvTerrainInfo1; break;
	case vpw::kCanonFeatureInfo: p = pCtx->GetFeatureInfo((FeatureTypes)iIndex); uKind = kT_ICvFeatureInfo1; break;
	case vpw::kCanonPlayer:
		if(!s_pCanonGame)
		{
			s_pCanonGame = pCtx->GetGame();
			if(s_pCanonGame)
				s_canon[std::make_pair((uint32_t)vpw::kCanonGame, 0)] = SrvRt::Register(s_pCanonGame,
				    vpk_most_derived(kT_ICvGame1));
		}
		if(s_pCanonGame)
			p = s_pCanonGame->GetPlayer((PlayerTypes)iIndex);
		uKind = kT_ICvPlayer1;
		break;
	default:
		bOk = false;
		strErr = "unknown code";
		break;
	}
	t_bSuppressLock = false;
	if(bOk)
	{
		if(p)
			h = SrvRt::Register(p, vpk_most_derived(uKind));
		s_canon[key] = h;
		++s_lCanonMade;
	}
	LeaveCriticalSection(&s_csCanon);
	return bOk;
}

void CanonInit()
{
	InitializeCriticalSection(&s_csCanon);
	s_bCanonInit = true;
}

bool LocalOn() { return s_pWant != NULL && (s_pWant[3] & 1) != 0; }

void ServedEnter() { ++t_iServed; }
void ServedLeave() { --t_iServed; }

void NoteEngineCall()
{
	const int ch = ThreadChannel();
	if(s_pWant && ch >= 0 && ch < 3)
		InterlockedIncrement(const_cast<LONG*>(&s_pWant[4 + ch]));
	if(t_iServed <= 0)
	{
		const long n = InterlockedIncrement(&s_lOutside);
		if(n == 1)
			Fail("DLL code ran outside a served call (D30 condition 4): the proxy's local answers rest on that never happening");
		if(n <= 5)
			SrvLog("local: LOUD: a DLL -> engine call on thread %lu outside any served call (%ld so far)",
			       GetCurrentThreadId(), n);
	}
}

// D30 condition 4, its negative test: VP_SERVER_TEST_OUTSIDE=1 makes a thread of its own run DLL code (GetGame and
// its Destroy: the CvDllGame constructor and destructor call the engine's lock) outside any served call, once, at the
// first kRt_Canon; the counter must then be 1 or more and the run must fail under fail_on_unexpected.
static DWORD WINAPI OutsideTestThread(LPVOID pMode)
{
	if(pMode)
	{
		// Issue 2801: VP_SERVER_TEST_OUTSIDE=2 calls only engine methods the server answers itself (no crossing, no
		// lock): the selected-cities walker and a local enumerator. Before 2801 they were not counted.
		ICvUserInterface2* pUI = NULL;
		AcquireSRWLockShared(&s_lock);
		for(std::map<std::pair<uint32_t, int32_t>, Entry>::const_iterator it = s_byHandle.begin(); it != s_byHandle.end(); ++it)
			if(it->second.kind == kT_ICvUserInterface2)
				pUI = static_cast<ICvUserInterface2*>(it->second.pMd);
		ReleaseSRWLockShared(&s_lock);
		if(pUI)
			pUI->nextSelectedCitiesNode(static_cast<IDInfo*>(NULL));
		std::vector<ICvUnknown*> none;
		ICvEnumerator* e = VpNewLocalEnumerator(none);
		if(e)
		{
			e->MoveNext();
			e->Destroy();
		}
		Report("local: TEST (VP_SERVER_TEST_OUTSIDE=2): UI object %s, local enumerator %s", pUI ? "found" : "absent",
		       e ? "made" : "absent");
		return 0;
	}
	ICvGameContext1* pCtx = Context();
	ICvGame1* p = pCtx ? pCtx->GetGame() : NULL;
	if(p)
		p->Destroy();
	return 0;
}

void MaybeOutsideTest()
{
	if(s_bOutsideTestDone)
		return;
	s_bOutsideTestDone = true;
	char v[8] = {0};
	if(!GetEnvironmentVariableA("VP_SERVER_TEST_OUTSIDE", v, sizeof(v)) || (v[0] != '1' && v[0] != '2'))
		return;
	Report("local: TEST (VP_SERVER_TEST_OUTSIDE=%c): running %s on a thread outside any served call", v[0],
	       v[0] == '1' ? "DLL code" : "engine methods the server answers itself");
	HANDLE t = CreateThread(NULL, 0, OutsideTestThread, v[0] == '2' ? (LPVOID)1 : NULL, 0, NULL);
	if(t)
	{
		WaitForSingleObject(t, 10000);
		CloseHandle(t);
	}
}

long OutsideCount() { return s_lOutside; }
bool LocalVerify() { return s_pWant != NULL && (s_pWant[3] & 2) != 0; }

void CanonReport(FILE* f)
{
	fprintf(f, "local: GetScriptSystem answered here %ld, verified %ld, MISMATCHES %ld (local answers %s)\n",
	        s_lScriptLocal, s_lScriptVerified, s_lScriptMismatch, LocalOn() ? (LocalVerify() ? "on, verify" : "on") : "off");
	fprintf(f, "local: DLL -> engine calls outside any served call: %ld (must be 0, D30 condition 4)\n", s_lOutside);
	fprintf(f, "lock: the DLL's GetGameCoreLock calls %ld, of them first-level %ld; lock-want first-level acquisitions %ld\n",
	        s_lDllLockCalls, s_lDllLockFirst, s_lSyncFirst);
	fprintf(f, "local: canonical objects made %ld in %ld generations; lock-want syncs: %ld acquisitions, %ld releases; "
	        "want memory %s\n", s_lCanonMade, s_lCanonGens, s_lLockSyncAcq, s_lLockSyncRel, s_pWant ? "mapped" : "absent");
	fprintf(f, "local: generation changes served %ld, canonical requests refused while one was in flight %ld; lock errors: "
	        "reported by the proxy %ld, want word out of range %ld, release with the depth already 0 %ld (all must be 0, "
	        "issue 2801)\n", s_lGenChanges, s_lCanonRefused, s_pWant ? (long)s_pWant[8] : 0L, s_lWantOutOfRange,
	        s_lReleaseUnderflow);
	static const char* const kRows[4] = {"ui", "core", "aux", "other"};
	for(int i = 0; i < 4; ++i)
		fprintf(f, "lock per thread %s: DLL GetGameCoreLock %ld (first-level %ld), DLL ReleaseGameCoreLock %ld; lock-want "
		        "acquisitions %ld (first-level %ld), releases %ld\n", kRows[i], s_lc[i][0], s_lc[i][1], s_lc[i][2],
		        s_lc[i][3], s_lc[i][4], s_lc[i][5]);
}

void ObjectsReport(FILE* f)
{
	AcquireSRWLockShared(&s_lock);
	fprintf(f, "engine objects: %ld remote objects created, %ld forgotten, %zu live; %ld unknown pointers passed back\n",
	        s_lCreated, s_lForgotten, s_byHandle.size(), s_lUnknown);
	ReleaseSRWLockShared(&s_lock);
}
}

namespace SrvRt
{
vpipc::Handle EngineHandleOf(const void* pRemote)
{
	vpipc::Handle h = {0u, 0};
	if(!pRemote)
		return h;
	AcquireSRWLockShared(&s_lock);
	std::unordered_map<const void*, vpipc::Handle>::const_iterator it = s_byPtr.find(pRemote);
	const bool bFound = it != s_byPtr.end();
	if(bFound)
		h = it->second;
	ReleaseSRWLockShared(&s_lock);
	if(!bFound)
	{
		const long n = InterlockedIncrement(&s_lUnknown);
		if(n <= 20)
			SrvLog("remote: LOUD: EngineHandleOf(%p): not a remote engine object (sent as null)", pRemote);
	}
	return h;
}

void* EngineWrap(vpipc::Handle h)
{
	if(h.type == 0 && h.id == 0)
		return NULL;
	const uint32_t kind = vpk_most_derived(vpw::Kind(h.type));
	if(!vpw::IsEngine(h.type) || !kind || !vpk_is_engine_iface(kind))
	{
		SrvLog("remote: LOUD: EngineWrap(type 0x%08x id %d): not an engine handle", h.type, h.id);
		return NULL;
	}
	const std::pair<uint32_t, int32_t> key(h.type, h.id);
	AcquireSRWLockShared(&s_lock);
	std::map<std::pair<uint32_t, int32_t>, Entry>::const_iterator it = s_byHandle.find(key);
	void* p = it != s_byHandle.end() ? it->second.pMd : NULL;
	ReleaseSRWLockShared(&s_lock);
	if(p)
		return p;
	AcquireSRWLockExclusive(&s_lock);
	it = s_byHandle.find(key);
	if(it != s_byHandle.end())
		p = it->second.pMd;
	else
	{
		vpipc::Handle hk = h;
		hk.type = vpw::MakeType(kind, true);
		p = NewRemote(hk, kind);
		if(p)
		{
			Entry e;
			e.pMd = p;
			e.kind = kind;
			s_byHandle[key] = e;
			s_byPtr[p] = h;
			++s_lCreated;
		}
	}
	ReleaseSRWLockExclusive(&s_lock);
	if(!p)
		SrvLog("remote: LOUD: EngineWrap: no remote class for kind %s", vpk_iface_name(kind));
	return p;
}

void EngineForget(vpipc::Handle h)
{
	if(h.type == 0 && h.id == 0)
		return;
	AcquireSRWLockExclusive(&s_lock);
	std::map<std::pair<uint32_t, int32_t>, Entry>::iterator it = s_byHandle.find(std::make_pair(h.type, h.id));
	if(it != s_byHandle.end())
	{
		const Entry e = it->second;
		s_byPtr.erase(e.pMd);
		s_byHandle.erase(it);
		++s_lForgotten;
		// An args object is dead once DestroyArgs returned; other remote objects may still be referenced by the DLL
		// (a Destroy of the engine utility or the UI is a no-op in the EXE), so they are kept (not deleted).
		if(e.kind == kT_ICvEngineScriptSystemArgs1)
			VpDeleteRemoteArgs(static_cast<ICvEngineScriptSystemArgs1*>(e.pMd));
	}
	ReleaseSRWLockExclusive(&s_lock);
}
}  // namespace SrvRt
