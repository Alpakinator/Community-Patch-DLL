// srv_feed.h - the renderer's event feed inside server.exe (epic 3300, decision D81 point 1; the Architect's 3.1).
//
// Written by the team-3300 lead before wave 1 (2026-11-13) with a first working implementation (srv_feed.cpp: the queue
// only). Owner of both files after that: issue 3303. Design: research/05-graphics-composition/renderer-in-server-design.md
// section 2.
//
// The rule: the render thread never calls into the DLL and never reads a DLL object (bug 0103: a read with a lazy side
// effect changed the game). What the renderer knows about the game comes only from here:
//   * events: the DLL's own notifications to the engine (GameplayPlotStateChange, GameplayFeatureChanged,
//     GameplayFOWChanged, GameplayCity*, GameplayNaturalWonderRevealed, ... the rows of proxy/model/policy-table.tsv whose
//     consumer is "renderer" or "both"), taken in the server's DLL -> engine overrides (srv_remote_objects.cpp,
//     RemoteEngineUtility) on the DLL's own thread, BEFORE they are forwarded to the EXE unchanged;
//   * plot records: one PlotRecord per plot, a whole table taken after a load at a defined point of the DLL's thread
//     (SrvRt::LeaveUpdate of the first Update after a load) through getters on the D30 purity list
//     (proxy/model/pure-methods.tsv) only, then patched from the events.
// Writers: any DLL thread (the ui, core and aux service threads), always inside a DLL -> engine call or inside the
// Update dispatch. Reader: the render thread, once per loop iteration (Drain, CopyPlots). One SRW lock; no allocation on
// the writer's path after the first growth of the queue.
// Compiles as C++14 (server/src) and C++17 (server/src/render); no DLL header here, so the render side can include it
// without the fork's include path.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <string>

namespace SrvFeed {

// What happened. Values are stable (they appear in logs and tests); add new kinds at the end.
enum Kind : uint16_t
{
	kNone = 0,
	// (issue 3303 settled the argument layout below; plot = -1 where no pure getter gives the plot)
	kPlotState = 1,          // GameplayPlotStateChange(plot, resource, improvement, improvementState, route, routeState):
	                         // a[0] = resource, a[1] = improvement, a[2] = improvementState, a[3] = route in the low 16
	                         // bits (int16) and routeState in the high 16 bits (int16)
	kFeatureChanged = 2,     // GameplayFeatureChanged(plot, feature): a[0] = feature
	kFowChanged = 3,         // GameplayFOWChanged(x, y, mode, setAll): plot from x, y (-1 before the first snapshot);
	                         // a[0] = mode (FogOfWarModeTypes: 0 off, 1 unexplored, 2 no vis), a[1] = setAll, a[2] = x,
	                         // a[3] = y
	kCityCreated = 4,        // GameplayCityCreated(city, era): plot -1; a[0] = era, a[1] = owner, a[2] = city id
	kCityDestroyed = 5,      // GameplayCityDestroyed(city, player): plot -1; a[0] = player, a[1] = owner, a[2] = city id
	kCityCaptured = 6,       // GameplayCityCaptured(city, player): plot -1; a[0] = player, a[1] = owner, a[2] = city id
	kNaturalWonder = 7,      // GameplayNaturalWonderRevealed(plot)
	kWallCreated = 8,        // GameplayWallCreated(plot)
	kYieldChanged = 9,       // GameplayYieldMightHaveChanged(plot)
	kBorderChanged = 10,     // ICvUserInterface1::UpdateCountryBorder(plot) (CvPlot::setOwner calls it, CvPlot.cpp:7133-7137;
	                         // only the CvDLLInterfaceIFaceBase twin is never called): a[0] = the plot's new owner
	// ---- epic 0375 (unit-server-scene.md 2.1; unit-feed-contract.md 2.1 plus kinds 32-33 by the MC's C1 answer #2105).
	// Pushed only while SetUnitsEnabled(true) (render_units=1). Unit kinds: plot = the unit's plot index at Push (-1 before
	// read by GetPlot()->GetPlotIndex(), which works before the first snapshot), a[0] = owner, a[1] = unit id, a[2..3]
	// as listed. City kinds 26-28: plot -1 (no pure getter gives a city's plot), a[0] = owner, a[1] = city id.
	// Issue 4452 (2026-11-15) settled every value below; a value not listed is 0. Unit kinds whose unit pointer was NULL
	// carry a[0] = a[1] = -1 and change no record (counted, Counters2::unitNullArg). Pushed BEFORE the call is forwarded
	// to the EXE, so the seq precedes the EXE's reaction (unit-feed-contract.md 2.1).
	kUnitCreated = 11,       // a[2] = unit type (GetUnitType), a[3] = 0
	kUnitMoved = 12,         // a[2] = path index in Batch::paths (-1 when the path was dropped: side table full, counted),
	                         // a[3] = the path length the DLL passed (the side table keeps at most 65,536 plots of it, as
	                         // the crossing does; PathEntry::length is what was kept)
	kUnitTeleported = 13,    // a[2] = target plot index (the call's plot, -1 when NULL), a[3] = 0
	kUnitDestroyed = 14,     // the EXE's record is freed by this one (unit-formations.md 12.7, S1); a[2] = a[3] = 0;
	                         // plot = the plot of the unit's record (no getter is called on a unit in ~CvUnit), -1 when
	                         // the key is not in the table
	kUnitDestroyedInCombat = 15, // a[2] = a[3] = 0; sets kUnitFlagDead (the record stays until kind 14)
	kUnitSetDamage = 16,     // a[2] = damage, a[3] = previous damage (the call's arguments)
	kUnitFortify = 17,       // a[2] = 0/1; sets / clears kUnitFlagFortified (no getter, contract 2.2)
	kUnitWork = 18,          // a[2] = build type
	kUnitParadrop = 19,
	kUnitActivate = 20,
	kUnitEmbark = 21,        // a[2] = 0/1 (remove + create in the EXE, both directions); a key not in the table is a
	                         // creation for the table (unit-server-scene.md 2.5; counted Counters2::embarkCreations);
	                         // a[3] (issue 0439, MC #2908): EmbarkArtBits, the inputs of the EXE's embarked art read at
	                         // the call for a[2] = 1 (0 for a[2] = 0 and in recordings made before 0439)
	kUnitGarrison = 22,      // a[2] = 0/1
	kUnitShouldDimFlag = 23, // a[2] = 0/1
	kUnitVisibility = 24,    // a[2] = visible + 2 x checkPlot, a[3] = blendTime as float bits; bit 0 of a[2] sets /
	                         // clears kUnitFlagVisible
	kUnitCombat = 25,        // plot = the combat's target plot (GetPlot, -1 none); a[0], a[1] = the attacker's owner and
	                         // unit id (-1, -1 when the attacker is not a unit); a[2] = combat index in Batch::combats
	                         // (-1 when dropped: side table full, counted); a[3] = 0 (Q7: the returned value is in the
	                         // CombatRecord, `returned`)
	kCitySetDamage = 26,     // a[2] = damage, a[3] = previous
	kCityPopulationChanged = 27, // a[2] = population, a[3] = 0
	kCityCombat = 28,        // as 25, except a[0], a[1] = the owner and city id of the combat's city (the attacker's
	                         // city, else the defender's; -1, -1 when neither side is a city); ticket 5062: a[3] = the
	                         // ATTACKING city's plot index + 1 (ICvCity1::GetX / GetY on the feed's grid), 0 = not known
	                         // (no attacking city, outside the grid, or a recording made before 5062)
	kUnitMissionEnd = 29,
	kUnitRebased = 30,       // a[2] = from plot, a[3] = to plot (the call's plots, -1 when NULL)
	kUnitResetAnimationState = 31,
	kLoadBegin = 32,         // the server enters a game load, before SetupGraphical (the allocator's load clear, S5):
	                         // pushed by ResetUnits; plot -1, turn -1, a[0] = the new unitTableGen, a[1] = the player id
	                         // of the ICvPlayer1::SetupGraphical call that started the load (4452: the first call of a
	                         // load is the one whose player id is not above the previous call's, 0 at every logged load)
	kEraChanged = 33,        // GameplayEraChanged(player, era): plot -1, a[0] = player, a[1] = new era (remove + create of
	                         // art-changing units, S3); the player row's era is set from a[1] at the same time
	kSnapshot = 100,         // a whole plot table was taken (a[0] = width, a[1] = height, a[2] = generation)
	kOverflow = 101,         // the queue dropped events (a[0] = count): the reader re-reads the plot table
	kUiMirror = 102,         // issue 0178: a record of the UI's state from the proxy's Lua bridge (vp_wire.h kRt_UiRecord,
	                         // proxy/src/proxy_ui_bridge.h UiBridge::Record): flags = the record's kind (UiBridge::Kind:
	                         // 1 SpawnArrowEvent, 2 RemoveAllArrowsEvent, 3 MinimapTextureBroadcastEvent, 4
	                         // DisplayMovementIndicator, 5 UnitMoving called, 6 unit position, 7 frame mark), plot -1,
	                         // turn -1, a[0..3] = its four values. Kind 6 (issue 4754, unit-feed-contract.md 3.3):
	                         // plot = the player (0..255), turn = 1 for an arrival (UnitMoveQueueChanged with no moves
	                         // left), 0 for a position (LocalMachineUnitPositionChanged); a[0] = the unit id, a[1..3] =
	                         // x, y, z in world units as float bits. Kind 7: a[0] = the EXE frame (the bridge's drain
	                         // number in its install generation), a[1] = the kind-6 events that follow it, all of that
	                         // frame. Decoded and mapped by SrvRemote::UiRec (server/src/remote/srv_remote.h).
	                         // Pushed by the ui service thread's post handler; no DLL code reads it (rule R1)
	// ---- extras (ticket 4932, epic 0454 X2; docs/extras/flags-and-messages-design.md 2.2): render messages the DLL sends
	// when it is built with VP64_RENDER_EVENTS, through server.exe's export VP64RenderEvent (feed/srv_render_events.cpp),
	// on the DLL's own thread after the state change they report. flags 0; turn = the game turn at the send (-1 unknown);
	// plot and a[0..3] as the DLL sent them. The numbers mirror the fork's CvGameCoreDLL_Expansion2/VP64RenderEvents.h;
	// srv_render_events.cpp checks them against it when the server is built. Add at the end; never reuse a number. The
	// unit scene ignores these kinds (srv_units_core.cpp IsUnitKind); a consumer that makes them unit kinds adds them to
	// LoadSetupGoesOn's setup calls (CvUnit::setupGraphical sends them inside a load's setup run).
	kExtrasFirst = 1000,
	kUnitActivityChanged = 1000, // plot = the unit's plot index (-1 none); a[0] = owner, a[1] = unit id, a[2] = the new
	                             // ActivityTypes (0 awake, 1 hold, 2 sleep, 3 heal, 4 sentry, 5 intercept, 6 mission),
	                             // a[3] = the old one; a[3] == a[2]: the state at the unit's setup for display (a load or a
	                             // creation), not a change (CvUnit::SetActivityType sends only when the value changes)
	kUnitCarrierChanged = 1001,  // plot as 1000; a[0] = owner, a[1] = unit id, a[2] = the transport's unit id (-1: no longer
	                             // cargo), a[3] = the transport's owner (-1 none); also sent at setup while the unit is cargo
	kExtrasLast = 1999,
};

// Issue 0439 (MC #2908; unit-server-scene.md 3.2, the embarked row): kUnitEmbark's a[3] for an embark (a[2] = 1), read
// on the DLL side at the call, so it holds what the EXE's GameplayUnitEmbark handler reads for the same unit at the same
// time: the team's all-water passage (ICvTeam1::CanEmbarkAllWaterPassage of the unit owner's team) and the owner's
// embarked graphic override (ICvPlayer1::GetEmbarkedGraphicOverride, a string: carried as a 24-bit hash, which the render
// side matches against the ArtDefine_UnitInfos Types; an override that names no art define is ignored by the EXE too).
enum EmbarkArtBits : uint32_t
{
	kEmbarkArtRead = 1u << 0,        // the team and the player were read (clear: a[3] carries nothing; the older rule)
	kEmbarkArtAllWater = 1u << 1,    // the team can embark across all water
	kEmbarkArtOverride = 1u << 2,    // the override string is not empty; bits 8-31 hold EmbarkArtHash24 of it
	kEmbarkArtHashShift = 8,
};
// FNV-1a 32 of the string's bytes, the top 24 bits (0 for NULL). Both sides use this one function.
inline uint32_t EmbarkArtHash24(const char* s)
{
	uint32_t h = 2166136261u;
	if(!s)
		return 0;
	for(; *s; ++s)
		h = (h ^ (uint8_t)*s) * 16777619u;
	return h >> 8;
}

// Event::flags of the unit kinds (11-31), set by PushUnit (issue 4463). Every other kind keeps 0 (kUiMirror its own value).
enum EventFlagBits : uint16_t
{
	// The event's key had no record in the unit table when it was pushed and the event did not make one (a kUpsert,
	// kPatchFlags or kRemove of an unknown key; kUnitCreated and the creating kUnitEmbark never carry it). The EXE has no
	// record for such a unit either and ignores the call (unit-server-scene.md 12.1 [L]: a Visibility before the
	// creation); a reader takes nothing from it (the scene sets no fade from it).
	kEventNoRecord = 1u << 0,
};

struct Event              // 32 bytes
{
	uint32_t seq;            // feed-wide sequence number (1, 2, ...), assigned by Push
	uint16_t kind;           // Kind
	uint16_t flags;          // 0, except EventFlagBits on the unit kinds (issue 4463) and kUiMirror's record kind
	int32_t plot;            // plot index y * width + x, -1 when the event has no plot
	int32_t turn;            // the game turn at Push (-1 unknown)
	int32_t a[4];            // arguments, per kind above
};
static_assert(sizeof(Event) == 32, "Event layout");

// The renderer's copy of one plot. Values are the DLL's type indices (-1 = none). Issue 3303 fills it through pure
// getters only and may extend it (append fields, keep the size a multiple of 4) before wave 2; the render side reads
// only what it documents here.
struct PlotRecord          // 16 bytes
{
	int8_t terrain;          // TerrainTypes
	int8_t plotType;         // PlotTypes (mountain, hills, land, ocean)
	int8_t feature;          // FeatureTypes
	int8_t route;            // RouteTypes
	int16_t resource;        // ResourceTypes
	int16_t improvement;     // ImprovementTypes
	int8_t owner;            // PlayerTypes
	uint8_t flags;           // bit 0 river on any edge, bit 1 improvement pillaged, bit 2 route pillaged, bit 3 natural
	                         // wonder, bit 4 city on the plot
	uint8_t fow;             // for the active player's team: bit 0 revealed, bit 1 visible (fog is not drawn in 3300)
	// ---- ticket 0631 (2026-11-23): was reserved0 (always 0), so the size and every offset stay ----
	uint8_t riverEdges;      // the plot's OWN river edges: bit 0 W of river (ICvPlot1::IsWOfRiver, the edge to the east
	                         // neighbour), bit 1 NW of river (IsNWOfRiver, south-east edge), bit 2 NE of river (IsNEOfRiver,
	                         // south-west edge); pure getters (proxy/model/pure-methods.tsv), snapshot and refresh as
	                         // terrain. Valid only with known bit 2: a record without it (a scene bundle recorded before
	                         // 0631) has 0 here, and a reader takes "river on any edge" (flags bit 0) instead. The other
	                         // three edges of a plot are its neighbours' own bits; flags bit 0 is the DLL's isRiver (a
	                         // crossing count), which can differ from the bits (live-terrain.md 8.18).
	uint32_t changeGen;      // the feed generation that last changed this record
	// ---- appended by issue 3303 (2026-11-13), per the rule above ----
	// Where each field comes from (research/05-graphics-composition/renderer-in-server-design.md section 2):
	//   terrain, plotType, feature, owner, flags bits 0 and 3, fow: pure getters (snapshot, refresh at LeaveUpdate);
	//   resource, route, flags bits 1 and 2, improvementState, routeState: GameplayPlotStateChange only (the active
	//     team's revealed layout, what the EXE draws; no getter for them is on the purity list);
	//   improvement: GameplayPlotStateChange (revealed) once one was seen for the plot, the getter before that;
	//   flags bit 4 (city): never set in 3303 (no pure getter gives a city's plot; named in the hand-back);
	//   artType: pure getter ICvPlot1::GetContinentType (issue 4305; snapshot and refresh, as terrain);
	//   riverEdges and known bit 2: pure getters IsWOfRiver, IsNWOfRiver, IsNEOfRiver (ticket 0631; as terrain).
	int8_t improvementState; // GameplayPlotStateChange's 4th argument: 1 half-built, 2 built, 3 pillaged; -1 never seen
	int8_t routeState;       // its 6th argument, the DLL's eRoadTypes (CvPlot.cpp): 0 road, 1 railroad, 2 road pillaged,
	                         // 3 railroad pillaged, 4/5 under construction, 6/7 trade route, 8 none; -1 never seen
	uint8_t known;           // bit 0 getter fields read (snapshot or refresh), bit 1 a GameplayPlotStateChange was seen,
	                         // bit 2 riverEdges read (ticket 0631; kPlotKnownRiverEdges)
	// ---- issue 4305 (epic 0169, 2026-11-15): was reserved1 (0), so the size and every offset stay ----
	uint8_t artType;         // the DLL's continent art type (CvPlot::GetContinentType = Lua's GetContinentArtType): 0 none,
	                         // 1 America, 2 Asia, 3 Africa, 4 Europe; a value outside 0..4 is stored as 0 (the tile-style
	                         // rule takes no style from it either; srv_remote_feed.cpp counts it). 0 before a getter read.
};
static_assert(sizeof(PlotRecord) == 20, "PlotRecord layout");
static_assert(offsetof(PlotRecord, artType) == 19, "PlotRecord layout: artType is the old reserved1");
static_assert(offsetof(PlotRecord, riverEdges) == 11, "PlotRecord layout: riverEdges is the old reserved0 (ticket 0631)");
static_assert(offsetof(PlotRecord, changeGen) == 12, "PlotRecord layout");
// PlotRecord::known bit 2 (ticket 0631): riverEdges holds the DLL's edge bits (set with every getter read since 0631)
const uint8_t kPlotKnownRiverEdges = 1u << 2;

// ---- epic 0375: the unit table and the player table (unit-server-scene.md 2.2, 2.4). Interface fixed by lead-0375 before
// wave 2 so that 4452 (writers, implementation), 4453 (allocator) and 4454 (scene) build against one definition; 4452
// may append fields within the sizes below and documents every value. ----
// UnitRecord::flags (issue 4452): the same bit values as c5r::units::UnitFlag (render/include/c5r/units/unit_scene.h,
// kFlagEmbarked ... kFlagDead), repeated here so that this header names no render header; server/tests/feed checks
// that they agree. Where each bit comes from (unit-feed-contract.md 2.2):
//   embarked, garrisoned, invisible, delayed death: pure getters at each unit event (IsEmbarked, IsGarrisoned,
//     IsInvisible(the active team, false), IsDelayedDeathExported);
//   fortified: the last kUnitFortify; visible: the last kUnitVisibility (bit 0 of a[2]) after the record's creation,
//     set at every creation (issue 4463: a kUnitVisibility before the creation is ignored, as the EXE ignores it,
//     unit-server-scene.md 12.1 [L]; was: "set at a record's creation when no kUnitVisibility came before it");
//   dead: kUnitDestroyedInCombat (a record removed by kUnitDestroyed is gone from the table).
enum UnitFlagBits : uint8_t
{
	kUnitFlagEmbarked = 1u << 0,
	kUnitFlagGarrisoned = 1u << 1,
	kUnitFlagFortified = 1u << 2,
	kUnitFlagVisible = 1u << 3,
	kUnitFlagInvisible = 1u << 4,
	kUnitFlagDelayedDeath = 1u << 5,
	kUnitFlagDead = 1u << 6,
	kUnitFlagsFromCalls = kUnitFlagFortified | kUnitFlagVisible | kUnitFlagDead,   // bits no getter gives
};

struct UnitRecord          // 32 bytes (unit-feed-contract.md 2.2)
{
	int8_t owner;            // PlayerTypes 0..63 (a record of another owner is refused: Counters2::unitBadKey)
	int8_t domain;           // DomainTypes (GetDomainType)
	int8_t facing;           // DirectionTypes (GetFacingDirection(false); 7 = the fork's NO_DIRECTION), taken only at a
	                         // creation (kinds 11, 21, 19) and a move (12, 13, 30); every other event keeps the
	                         // record's (issue 4463: the EXE takes no later facing, unit-server-scene.md 12.1 [L])
	uint8_t flags;           // UnitFlagBits above (= c5r::units::UnitFlag)
	int32_t id;              // the unit's id (unique across players, F3)
	int32_t unitType;        // UnitTypes (database row index)
	int32_t plot;            // plot index y * width + x (GetPlot()->GetPlotIndex(); -1 when the unit has no plot)
	int16_t damage, maxHP;   // GetDamage, GetMaxHitPoints (clamped to int16)
	int16_t artStyle, era;   // the owner's PlayerRecord::artStyle / era when the record last changed (the feed's player
	                         // row; -1 when no row was readable)
	uint32_t seq;            // feed seq of the last event that changed the record (0: an event the queue dropped; the
	                         // reader re-copies the table at kOverflow)
	uint32_t reserved;       // 0
};
static_assert(sizeof(UnitRecord) == 32, "UnitRecord layout");

struct PlayerRecord        // 32 bytes (record 6 PLAYERS, decision 2 with the note's refinement)
{
	int8_t player;           // PlayerTypes 0..63
	int8_t team;             // TeamTypes (GetTeam)
	uint8_t alive;           // 0/1 (IsAlive)
	uint8_t flags;           // bit 0 minor civ (IsMinorCiv), bit 1 barbarian (IsBarbarian)
	int32_t colour;          // PlayerColorTypes (PlayerColors row ID; GetPlayerColor)
	int32_t civilization;    // CivilizationTypes (GetCivilizationType); for a minor civilization (flags bit 0) the
	                         // MinorCivTypes index instead (ICvPlayer1::GetMinorCivType, the MinorCivilizations row ID;
	                         // -1 = NO_MINORCIV): every city-state's GetCivilizationType is CIVILIZATION_MINOR, whose
	                         // ArtStyleSuffix is NULL, and its art follows its MinorCivilizations row (issue 4460, A1)
	int32_t era;             // EraTypes (GetCurrentEra; set from kEraChanged's a[1] between two reads)
	int32_t artStyle;        // ArtStyleTypes (GetArtStyleType)
	uint32_t changeGen;      // feed generation of the last change (a counter of player-row changes, 1, 2, ...)
	int32_t activeTeam;      // the game's active team at the read (same in every row)
	union                    // issue 4452: `state` names the old `reserved` word; `reserved` stays a valid name
	{
		uint32_t reserved;
		uint32_t state;      // bit 0 IsHuman, bit 1 IsTurnActive (both at the read: decision 14's human-turn input)
	};
};
static_assert(sizeof(PlayerRecord) == 32, "PlayerRecord layout");

struct PathEntry { uint32_t seq; uint32_t first; uint32_t length; };   // into Batch::pathPlots

// Issue 4452 (unit-server-scene.md 2.3 and 11, record 5 COMBATS): one per kUnitCombat / kCityCombat, read from the
// ICvCombatInfo1 before the call is forwarded (the EXE's handler borrows the argument). 64 bytes.
// GetDamageInflicted is not pure (CvCombatInfo::getDamageInflicted fires the Lua GameEvent BattleDamageDelta when the mod
// options EVENTS_BATTLES and EVENTS_BATTLES_DAMAGE are on; CvGameCoreStructs.cpp 345-394 at the x64 pin), so the note's two
// damageInflicted words became firstMember and reserved. Corrected 2026-11-20 by issue 4726 (was: "is NOT read; a consumer
// derives the damage of this combat from finalDamage and the unit's damage before it"): the feed reads it only while that
// event is off and puts it in reserved with kCombatDamageKnown; deriving it from finalDamage is wrong (0477's submarine
// fix changes only DamageInflicted on the engine's copy, unit-combat-server.md 12).
enum CombatFlagBits : uint16_t
{
	kCombatAttackerAdvances = 1u << 0,   // GetAttackerAdvances
	kCombatDefenderRetaliates = 1u << 1, // GetDefenderRetaliates
	kCombatRanged = 1u << 2,             // GetAttackIsRanged
	kCombatBombing = 1u << 3,            // GetAttackIsBombingMission
	kCombatAirSweep = 1u << 4,           // GetAttackIsAirSweep
	kCombatDefenderCaptured = 1u << 5,   // GetDefenderCaptured
	kCombatVisualize = 1u << 6,          // GetVisualizeCombat (the target plot visible to the active player)
	kCombatReturnedKnown = 1u << 7,      // `returned` holds the EXE's answer (clear: the batch was drained before the
	                                     // forwarded call returned; Counters2::combatResultsLate)
	kCombatQuickOption = 1u << 8,        // ICvGame1::IsOption(GAMEOPTION_QUICK_COMBAT) at the combat
	kCombatActiveTurn = 1u << 9,         // the active player's IsTurnActive at the combat (decision 14: the human's turn)
	kCombatGameRead = 1u << 10,          // bits 8-9 were read (clear: no game object, they are 0 and mean nothing)
	// issue 4726 (unit-combat-server.md 12): CombatRecord::reserved holds the per-combat damage (CombatDamage below) and
	// the interceptor member's `damage` is its damage dealt. Set only when GetDamageInflicted was called, which the feed
	// does only while the DLL's battle-damage Lua event is off (CustomModOptions EVENTS_BATTLES or EVENTS_BATTLES_DAMAGE
	// 0: then the getter is a plain read). Clear: reserved is 0 and the per-combat damage is not known.
	kCombatDamageKnown = 1u << 11,
	// issue 4726: the members end with the interceptor's (kCombatMemberInterceptor). With bits 11 and 12 a record says it
	// carries 4726's content: a reader built before 4726 (srvunits::rec's decoder: flags above bit 10 are BadRecord) refuses
	// such a record by name instead of reading the interceptor as a damage member; a record with neither bit is byte for
	// byte a pre-4726 record (bundles recorded before 4726 read as before; no format_version change).
	kCombatInterceptorMember = 1u << 12,
};
// Issue 4726: a damage member with this flag is not a damage member: it is the combat's interceptor
// (GetUnit(BATTLE_UNIT_INTERCEPTOR); 0321 4.1 participant 2), appended after the damage members (the DLL's member flags
// use bit 0 only, CvStructs.h). Its fields: player, id, flags = kCombatMemberInterceptor, damage = the damage it deals
// (GetDamageInflicted(BATTLE_UNIT_INTERCEPTOR); 0 unless kCombatDamageKnown), finalDamage = the unit's damage (an
// interceptor takes none in the DLL's resolve), maxHP, x, y = -1, combatType = -1, era = -1. At most one per combat.
constexpr uint16_t kCombatMemberInterceptor = 0x8000;
struct CombatRecord
{
	uint32_t seq;            // the event's seq
	uint16_t kind;           // kUnitCombat or kCityCombat
	uint16_t flags;          // CombatFlagBits
	int32_t plot;            // GetPlot()->GetPlotIndex() (-1 none)
	int32_t attackerOwner, attackerUnit, attackerCity;   // BATTLE_UNIT_ATTACKER: owner, unit id, city id (-1 = none)
	int32_t defenderOwner, defenderUnit, defenderCity;   // BATTLE_UNIT_DEFENDER
	int32_t nuclearLevel;    // GetNuclearDamageLevel
	uint32_t firstMember;    // index of its first member in Batch::combatMembers
	uint32_t reserved;       // 0; issue 4726, with kCombatDamageKnown: the damage each side takes in this combat, two int16
	                         // (low: the attacker's = GetDamageInflicted(DEFENDER); high: the defender's =
	                         // GetDamageInflicted(ATTACKER)), as the EXE's participant records carry it [I: the mapping
	                         // from 0477's fix, combat-choreography.md 13.4 and 13.6]. Read it with CombatDamage.
	int32_t finalDamage[2];  // GetFinalDamage(ATTACKER), GetFinalDamage(DEFENDER)
	uint32_t returned;       // the value GameplayUnitCombat / GameplayCityCombat returned (valid with kCombatReturnedKnown)
	uint32_t nMembers;       // GetDamageMemberCount, capped at GetMaxDamageMemberCount and 256 (issue 4726: plus the
	                         // interceptor's member, kCombatMemberInterceptor)
};
static_assert(sizeof(CombatRecord) == 64, "CombatRecord layout");
// Issue 4726: the damage side (0 attacker, 1 defender) takes in this combat, -1 when not known (kCombatDamageKnown clear).
inline int32_t CombatDamage(const CombatRecord& r, int side)
{
	if(!(r.flags & kCombatDamageKnown))
		return -1;
	return (int32_t)(int16_t)(uint16_t)(side ? (r.reserved >> 16) : (r.reserved & 0xFFFFu));
}
inline uint32_t CombatDamageWords(int32_t attacker, int32_t defender)
{
	auto clip = [](int32_t v) -> uint32_t { return (uint16_t)(int16_t)(v < 0 ? 0 : v > 32767 ? 32767 : v); };
	return clip(attacker) | (clip(defender) << 16);
}

struct CombatMember        // 28 bytes: CvCombatMemberEntry's fields (CvStructs.h) as the note's section 11 lists them
{
	int32_t player, id;      // m_ePlayer, m_iID
	uint16_t flags;          // m_bfFlags (bit 0 INFO_FLAG_IS_CITY)
	int16_t damage, finalDamage, maxHP, x, y;   // m_iDamage, m_iFinalDamage, m_iMaxHitPoints, m_iX, m_iY
	int32_t combatType, era; // m_eCombatType, m_eEra
};
static_assert(sizeof(CombatMember) == 28, "CombatMember layout");

// Issue 4783 (stage 5, W33 #8801, lead #8802): the EXE's answer to a forwarded GameplayUnitCombat / GameplayCityCombat that
// came after the render thread's Drain had taken its combat record (SetCombatResult found it no longer pending; counted
// Counters2::combatResultsLate). The feed keeps such pairs in a small ring (at most 256; the oldest is dropped and counted
// in Batch::lateResultsLost) and publishes them with the next Drain: the fight clock takes the event id then
// (srvunits::FightClock::setOwner), so the fight's later trigger records and its OwnerEnd carry it.
struct LateCombatResult
{
	uint32_t seq;            // the combat event's seq (CombatRecord::seq of an earlier batch)
	uint32_t returned;       // the value the forwarded call returned (the EXE's event id)
};

struct Batch
{
	std::vector<Event> events;          // events since the previous Drain, in seq order
	// epic 0375 (filled only with SetUnitsEnabled(true)); combats: Batch::combats, layout defined by 4452 (note 11)
	std::vector<int32_t> pathPlots;     // plot indices of every kUnitMoved path of this batch
	std::vector<PathEntry> paths;       // one per kUnitMoved, indexed by the event's a[2]
	std::vector<PlayerRecord> players;  // player rows changed since the previous Drain (one per player, the newest)
	uint32_t unitTableGen = 0;          // +1 when the unit table is reset (a load); a change means "CopyUnits again"
	std::vector<CombatRecord> combats;  // one per kUnitCombat / kCityCombat, indexed by the event's a[2] (issue 4452)
	std::vector<CombatMember> combatMembers;   // CombatRecord::firstMember / nMembers index this (issue 4452)
	std::vector<LateCombatResult> lateResults; // issue 4783: results of earlier batches' combats, in the order they came
	uint64_t lateResultsLost = 0;       // issue 4783: late results dropped from the full ring since the start
	std::vector<uint32_t> changedPlots; // plot indices whose record changed since the previous Drain (unique)
	uint32_t snapshotGen = 0;           // generation of the plot table (+1 per snapshot); a change means "re-read all"
	uint32_t width = 0, height = 0;     // plot grid of the current table (0 before the first snapshot)
	uint64_t dropped = 0;               // events dropped since the start (queue cap)
};

// ---- writers (DLL threads) ----
void Push(const Event& e);                                // seq assigned here
void BeginSnapshot(uint32_t width, uint32_t height);      // a new table; plots default to all -1 / 0
void SetPlot(uint32_t index, const PlotRecord& r);        // inside a snapshot, or a patch after one (marks it changed)
void EndSnapshot();                                       // publishes the table (snapshotGen + 1, a kSnapshot event)

// ---- reader (the render thread) ----
// Moves the events and changed plots accumulated since the last call into `out` (cleared first). False when nothing
// changed since the previous call. With the units on it also refreshes, in the same lock acquisition, the unit table copy
// that CopyUnits returns (issue 4463).
bool Drain(Batch& out);
// Copies the whole plot table (empty before the first snapshot) and its generation.
void CopyPlots(std::vector<PlotRecord>& out, uint32_t& snapshotGen, uint32_t& width, uint32_t& height);

// ---- epic 0375: units (implemented by issue 4452) ----
enum class UnitOp : uint8_t { kUpsert, kPatchFlags, kRemove, kNone };
// Assigns e.seq, stamps rec->seq, applies op to the unit table and appends the event, all under the feed's one lock.
// Returns the seq. (CopyUnits returns the table as the last Drain left it: issue 4463, below.)
// Issue 4452, the table effect (key = (rec->owner, rec->id); owner outside 0..63: no table change, Counters2::unitBadKey):
//   kUpsert   the record becomes *rec, except the kUnitFlagsFromCalls bits, which are kept from the record it replaces,
//             and the facing, which is kept unless the event is a creation or a move (issue 4463: kinds 11, 21, 19 and
//             12, 13, 30; UnitRecord::facing); then the event's own effect: kUnitFortify (a[2]) sets/clears fortified,
//             kUnitVisibility (a[2] bit 0) visible, kUnitDestroyedInCombat sets dead. A key not in the table is
//             inserted only by kUnitCreated and kUnitEmbark (either direction, unit-server-scene.md 2.5), always
//             visible. Any other kind for an unknown key inserts nothing, is flagged kEventNoRecord and counted
//             Counters2::preCreationEvents; a kUnitFortify or kUnitDestroyedInCombat keeps its bit aside for the key's
//             creation [I: not measured], a kUnitVisibility keeps nothing (issue 4463: the EXE ignores a Visibility
//             before the creation, unit-server-scene.md 12.1 [L]; was: its bit applied at the creation, 4459 F7).
//   kPatchFlags  an existing record's flags become rec->flags (unknown key: nothing, counted unitPatchUnknown).
//   kRemove   the record leaves the table (unknown key: nothing, counted unitRemoveUnknown; the EXE ignores it too).
//   kNone     no table change (rec may be NULL).
// The table is changed whether or not the queue keeps the event (a dropped event stamps seq 0 and counts
// Counters2::droppedUnit). Returns the seq, 0 when the event was dropped or the units are off.
uint32_t PushUnit(Event e, const UnitRecord* rec, UnitOp op);
// Keeps the row and publishes it with the next Drain (Batch::players) when it differs from the kept row in any field
// but changeGen (activeTeam and state included); changeGen is set here. A player outside 0..63 is refused
// (Counters2::playerBadIndex).
void SetPlayer(const PlayerRecord& r);
// At a load (kLoadBegin): empties the table and the kept pre-creation bits, unitTableGen + 1, and appends the kLoadBegin
// event (a[0] = the new unitTableGen, a[1] = 0; the writer's ResetUnitsAt sets a[1]) under the same lock.
void ResetUnits();
// Issue 4463: the unit table as the last Drain left it, sorted by (owner, id), and its generation: Drain copies the
// table in the same lock acquisition that takes the events whenever the table changed since its last copy, so the copy
// is the state after exactly the events drained so far (a PushUnit between a Drain and this call is in the next copy,
// never in this one; unit-server-scene.md 3.4's race). Before the first Drain with the units on: empty, generation 0.
void CopyUnits(std::vector<UnitRecord>& out, uint32_t& unitTableGen);
void CopyPlayers(std::vector<PlayerRecord>& out);                       // every row ever set, sorted by player
// A consistent copy of the game database for the render side's unit art (MC #2105 Q5: SQLite's backup API or an
// in-memory copy, never immutable=1 on the file the DLL writes). Written by 4452 at game start (and after a mod or
// database change), read by the scene (4454); gen + 1 per new copy.
void SetUnitDatabase(const std::wstring& path, uint32_t gen);
bool GetUnitDatabase(std::wstring& path, uint32_t& gen);   // false before the first copy
void SetUnitsEnabled(bool on);  // server.cfg render_units=1; disabled: every unit writer returns at once
bool UnitsEnabled();
// Ticket 4875 (epic 0561): server.cfg render_cities=1 or render_landmarks=1 wants the same database copy for the model
// library (one shared copy per load, whichever asks; MC #6166): the load mark and SetUnitDatabase run with it even while
// UnitsEnabled() is false, and the unit writers stay off. Set by the render host at HostStart.
void SetModelDatabaseWanted(bool on);
bool ModelDatabaseWanted();   // false while the feed is disabled
// Ticket 4882: the load mark of a server without the unit feed (srv_remote_main.cpp ModelDatabaseLoadMark) appends the
// kLoadBegin event itself (plot -1, turn -1, a[0] = the unchanged unitTableGen, a[1] = the player id), so the render side's
// load rules (the model bits cleared, the model scene cleared; the arc's, the arrows' and the path's clears) run at a load
// without the units too. Returns the seq; 0 (nothing pushed) while the feed is disabled or the unit feed is on (its
// ResetUnitsAt pushes the event: never twice).
uint32_t PushModelLoadBegin(int32_t player);
bool DatabaseCopyWanted();    // UnitsEnabled() || ModelDatabaseWanted(): srv_remote_main.cpp makes the copy only then

// ---- control ----
void SetEnabled(bool on);       // server.cfg render=1 (or feed=1); disabled: every writer call returns at once
bool Enabled();
void SetQueueCap(size_t cap);   // events kept between two Drains (default 65536); beyond it they are dropped (kOverflow)
// Issue 4783: the late-result ring (LateCombatResult) on (the default, and after ResetForTest) or off; off = the feed before
// 4783 (a late result is counted and dropped). The tests' negative only; no product setting turns it off.
void SetLateResultsKept(bool on);
struct Counters { uint64_t pushed, dropped, drains, snapshots, plotSets; };
Counters GetCounters();

}  // namespace SrvFeed
