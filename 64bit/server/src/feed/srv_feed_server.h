// srv_feed_server.h - the writer side of the event feed beyond srv_feed.h (issue 3303, 2026-11-13). Server-only:
// the DLL-side capture (server/src/remote/srv_remote_feed.cpp) and the unit tests (server/tests/feed/) use it; the
// render side needs only srv_feed.h. No DLL header here either.
//
// Field patches. The capture code patches a record with the fields one notification or one pure-getter read
// carries (a mask), instead of replacing the record: the fields come from two sources (srv_feed.h, PlotRecord) and
// neither may overwrite the other's. A patch that changes nothing is not reported as a change.
#pragma once
#include "srv_feed.h"

namespace SrvFeed {

enum FieldBits : uint32_t
{
	kFTerrain = 1u << 0,
	kFPlotType = 1u << 1,
	kFFeature = 1u << 2,
	kFRoute = 1u << 3,          // with kFRouteState and flags bit 2 when they come from GameplayPlotStateChange
	kFResource = 1u << 4,
	kFImprovement = 1u << 5,    // the value from GameplayPlotStateChange (sets known bit 1 with kFLayout)
	kFImprovementGetter = 1u << 6,  // the getter's value: applied only while known bit 1 is clear
	kFOwner = 1u << 7,
	kFRiver = 1u << 8,          // flags bit 0
	kFImpPillaged = 1u << 9,    // flags bit 1
	kFRoutePillaged = 1u << 10, // flags bit 2
	kFNaturalWonder = 1u << 11, // flags bit 3
	kFCity = 1u << 12,          // flags bit 4
	kFFow = 1u << 13,
	kFImprovementState = 1u << 14,
	kFRouteState = 1u << 15,
	kFGetters = 1u << 16,       // sets known bit 0 (a getter read of the plot)
	kFLayout = 1u << 17,        // sets known bit 1 (a GameplayPlotStateChange for the plot)
	kFArtType = 1u << 18,       // issue 4305: the continent art type (pure getter GetContinentType)
	kFRiverEdges = 1u << 19,    // ticket 0631: riverEdges (pure getters Is{W,NW,NE}OfRiver); sets known bit 2
	// what GameplayPlotStateChange carries
	kFLayoutAll = kFRoute | kFResource | kFImprovement | kFImpPillaged | kFRoutePillaged | kFImprovementState |
	              kFRouteState | kFLayout,
	// what a pure-getter read of a plot carries
	kFGetterAll = kFTerrain | kFPlotType | kFFeature | kFImprovementGetter | kFOwner | kFRiver | kFNaturalWonder | kFFow |
	              kFArtType | kFRiverEdges | kFGetters,
};

// The empty record (all -1 / 0), as BeginSnapshot fills a new table.
PlotRecord EmptyRecord();

// Issue 4305: PlotRecord::artType from the DLL's GetContinentType value: 0..4 kept, anything else 0 (*outside = true).
// The EXE's style rule takes a style only from the art types 1..4 (bake::tileStyle, terrain-bake-passes.md 5.3), so 0
// gives the tile the same style as an art type outside 1..4 would.
uint8_t ArtTypeFromDll(int v, bool* outside = nullptr);

// Copies the fields of `mask` from `v` into record `index`. Before the first snapshot (no table) the layout fields
// (kFLayoutAll) are kept per plot index and applied by the next EndSnapshot (the DLL may lay plots out before the first
// Update); getter fields are then dropped (the snapshot reads them anyway). Inside a snapshot the change is not marked;
// after it a record that changed is marked (Drain's changedPlots). True when the record changed. Disabled, or index
// outside the table: false (counted in Counters2::badIndex).
bool UpdatePlot(uint32_t index, uint32_t mask, const PlotRecord& v);

// One record of the current table (false: no table, or index outside it).
bool GetPlot(uint32_t index, PlotRecord& out);

// The current grid (0, 0 before the first snapshot).
void GetSize(uint32_t& width, uint32_t& height);

// More counters (the srv_feed.h ones stay as they are).
struct Counters2
{
	uint64_t perKind[128];       // events pushed per Kind (kinds >= 128 count in perKind[127])
	uint64_t patchesChanged;     // UpdatePlot calls that changed a record (after the first snapshot)
	uint64_t patchesSame;        // ... that changed nothing
	uint64_t pendingKept;        // layout patches kept before the first snapshot
	uint64_t pendingApplied;     // ... applied by an EndSnapshot
	uint64_t pendingDropped;     // ... dropped because the table had no such index, or past the pending cap
	uint64_t badIndex;           // UpdatePlot / SetPlot with an index outside the table
	// ---- issue 4452 (epic 0375): the unit writers (srv_feed.h PushUnit ...); 0 while the units are off ----
	uint64_t unitEvents;         // unit-epic events offered (kinds 11-33, kept or dropped)
	uint64_t droppedUnit;        // ... of them dropped (queue cap or a full side table): the allocator is out of step
	uint64_t unitInserts, unitUpdates, unitRemoves;   // table effects
	uint64_t unitRemoveUnknown;  // kRemove of a key not in the table (a load's DLL-side reset, or a missed creation)
	uint64_t unitCreateKnown;    // kUnitCreated for a key already in the table (era change, or a KnownCreation)
	uint64_t embarkCreations;    // kUnitEmbark that inserted the key (a unit set up embarked, 2.5)
	uint64_t preCreationEvents;  // non-creation kUpsert events for a key not in the table (bits kept for its creation)
	uint64_t preCreationDropped; // ... not kept: the aside table (65,536 keys) was full
	uint64_t unitPatchUnknown;   // kPatchFlags of an unknown key
	uint64_t unitBadKey;         // a record whose owner is outside 0..63 (no table change)
	uint64_t unitResets;         // ResetUnits calls (loads)
	uint64_t pathsKept, pathPlotsKept, combatsKept, combatMembersKept;
	uint64_t sideTableFull;      // a path or combat whose event was dropped because its side table was full
	uint64_t combatResultsSet;   // SetCombatResult that found its record still in the batch
	uint64_t combatResultsLate;  // ... that did not (drained before the forwarded call returned)
	uint64_t playerRowsChanged, playerRowsSame, playerBadIndex;
	uint64_t unitDbSets;         // SetUnitDatabase calls
};
Counters2 GetCounters2();

// ---- issue 4452 (epic 0375): writer extras beyond srv_feed.h ----
// PushUnit for kUnitMoved with its path: the plots go to the batch's side table and e.a[2] = the path's index, e.a[3] =
// n (as passed), all under the lock. At most 65,536 plots of a path are kept (PathEntry::length); a side table above
// 4,194,304 plots drops the event (a[2] = -1, counted sideTableFull and droppedUnit); the table effect applies anyway.
uint32_t PushUnitPath(Event e, const UnitRecord* rec, UnitOp op, const int32_t* plots, uint32_t n);
// A combat event with its record and members, and the attacker's / defender's unit records (each kUpsert without an
// event of its own, NULL to skip), under the lock: e.a[2] = the combat's index, c.seq / kind / firstMember / nMembers
// are set here. The side tables keep at most 65,536 combats and 1,048,576 members between two Drains; beyond, the event
// is dropped (a[2] = -1, counted). Returns the seq (0 dropped).
uint32_t PushCombat(Event e, const CombatRecord& c, const CombatMember* members, uint32_t nMembers,
                    const UnitRecord* attacker, const UnitRecord* defender);
// The value the forwarded combat call returned: stored in the combat record of event `seq` (kCombatReturnedKnown) when
// that record is still in the pending batch; else counted combatResultsLate. False when not stored.
bool SetCombatResult(uint32_t seq, uint32_t returned);
// ResetUnits with the player id that started the load in the kLoadBegin event's a[1]. Returns the event's seq.
uint32_t ResetUnitsAt(int32_t player);
// The kept row of a player (false: none set, or outside 0..63).
bool GetPlayerRow(int player, PlayerRecord& out);
// One unit record of the table (false: not in it).
bool GetUnit(int owner, int32_t id, UnitRecord& out);
// Tests only: the side-table caps (0 = default).
void SetSideCapsForTest(size_t pathPlots, size_t combats, size_t members);

// Tests only: back to the state at process start (disabled, empty, cap 65536, counters 0).
void ResetForTest();

}  // namespace SrvFeed
