// srv_remote_feed.h - the DLL-side capture of the renderer's event feed (issue 3303, epic 3300; design:
// research/05-graphics-composition/renderer-in-server-design.md section 2). Implementation: srv_remote_feed.cpp.
//
// Called on the DLL's own threads only: the On* functions from the notification overrides in
// srv_remote_objects.cpp (RemoteEngineUtility, RemoteUserInterface) BEFORE the call is forwarded unchanged; AtLeaveUpdate
// from SrvRt::LeaveUpdate (srv_rt.cpp), inside the dispatch of ICvGame1::Update after the DLL's Update returned. Every
// DLL call made here is a getter of proxy/model/pure-methods.tsv (bug 0103). Every function returns at once while the
// feed is off (server.cfg render=0 and feed=0) and never lets an exception reach the DLL.
#pragma once
#include <stdint.h>
#include <stdio.h>

class ICvPlot1;
class ICvCity1;
class ICvGame1;
class ICvGameContext1;
class ICvUnit1;
class ICvCombatInfo1;

namespace SrvRemoteFeed
{
void OnPlotStateChange(const ICvPlot1* pPlot, int iResource, int iImprovement, int iImprovementState, int iRoute,
                       int iRouteState);
void OnFeatureChanged(ICvPlot1* pPlot, int iFeature);
void OnFowChanged(int iX, int iY, int iMode, bool bSetAll);
void OnCity(uint16_t uKind, ICvCity1* pCity, int iArg);        // kCityCreated / kCityDestroyed / kCityCaptured
void OnPlotOnly(uint16_t uKind, ICvPlot1* pPlot);               // kNaturalWonder / kWallCreated / kYieldChanged
void OnBorder(ICvPlot1* pPlot);                                 // ICvUserInterface1::UpdateCountryBorder
// The snapshot / refresh point: the first call after a load (no table, another grid, the turn not the previous + 1)
// takes a snapshot; a turn change re-reads every plot (the refresh rule for changes no notification carries); any
// other call re-reads the plots the notifications named since the previous call.
void AtLeaveUpdate(ICvGameContext1* pContext, ICvGame1* pGame, int iTurn);
// Ticket 0529 (R2 of 0424's premise check; terrain-surface-verification.md 14.4): the plot snapshot under the load screen.
// Called by the engine's ICvPlayer1::SetupGraphical hook (srv_remote_main.cpp) AFTER the call was forwarded, for every
// player id (-1 when unknown). The last call of a load's 0..63 sequence (63 after a call whose id was not above the one
// before it), outside an ICvGame1::Update, takes the snapshot there, with the game-core lock taken through
// SrvRemote::Context()->GetGame(); the first AtLeaveUpdate after it re-reads every plot (no new generation) and names in
// one line what changed in between. server.cfg feed_load_snapshot=0: off (the snapshot at the first Update, as before).
// Not taken (no active team, no grid): the first AtLeaveUpdate takes the snapshot whatever the turn says.
void AfterSetupGraphical(int iPlayer);
// Totals for a report (counts per kind, snapshots, refreshes, exceptions caught; the unit feed's totals, issue 4452).
// Called by SrvRemote::WriteReport (srv_remote_main.cpp) into server-report.txt.
void WriteTotals(FILE* f);

// ---- issue 4452 (epic 0375): the unit feed (research/05-graphics-composition/unit-server-scene.md 2.1-2.5) ----
// Each returns at once unless SrvFeed::UnitsEnabled() (server.cfg render_units=1, set by the render host), makes only
// calls of proxy/model/pure-methods.tsv on the DLL objects it is given (and on ICvGame1 / ICvPlayer1 through
// SrvRemote::Context() for a player row not read yet and for a combat's game flags), never lets an exception reach the
// DLL (NoteException) and is called BEFORE the call is forwarded. srv_feed.h's kinds document every value pushed.
// kinds 11, 14-23, 29, 31 (a2, a3 = the call's arguments as srv_feed.h lists them)
void OnUnit(uint16_t uKind, ICvUnit1* pUnit, int iA2, int iA3);
void OnUnitMoved(ICvUnit1* pUnit, const int* piPlots, uint32_t n);                      // kind 12
void OnUnitPlot(uint16_t uKind, ICvUnit1* pUnit, ICvPlot1* pFrom, ICvPlot1* pTo);       // 13 (pFrom NULL), 30
void OnUnitVisibility(ICvUnit1* pUnit, bool bVisible, bool bCheck, float fBlendTime);  // kind 24
// kinds 25 / 28: reads the borrowed combat information; returns the event's seq (0: dropped or off) for OnCombatResult
uint32_t OnCombat(uint16_t uKind, ICvCombatInfo1* pInfo);
void OnCombatResult(uint32_t uSeq, unsigned int uReturned);                              // after the forwarded call
void OnCityEvent(uint16_t uKind, ICvCity1* pCity, int iA2, int iA3);                    // kinds 26, 27
void OnEraChanged(int iPlayer, int iEra);                                               // kind 33
// ICvPlayer1::SetupGraphical(player) from the engine (srv_remote_main.cpp's hook), before it is forwarded: true when it
// starts a load (the first call, or a player id not above the previous call's), which pushes kLoadBegin (kind 32,
// SrvFeed::ResetUnitsAt) and marks the player rows for a re-read.
bool OnSetupGraphical(int iPlayer);
}  // namespace SrvRemoteFeed
