// srv_units_seed.h - the formation-seed allocator of the server's unit scene (epic 0375, issue 4453).
// Written 2026-11-15 for this repository (GPL-3.0-or-later).
//
// The allocator: the EXE's art-record index rule re-implemented from the knowledge base's description
// (research/05-graphics-composition/unit-formations.md 12.7, unit-server-scene.md section 4; D83: no EXE code).
// Interface fixed by lead-0375 before wave 2 (epic 0375); issue 4453 implements it and may extend it (additively) and
// documents every behaviour against 12.7; issue 4454's scene calls only what is declared here.
#pragma once
#include <cstdint>
#include <functional>
#include <vector>

#include "../feed/srv_feed.h"

namespace srvunits {

struct UnitKey
{
	int8_t owner;
	int32_t id;
	bool operator==(const UnitKey& o) const { return owner == o.owner && id == o.id; }
};

// Named statuses (unit-server-scene.md section 10); each is counted and logged LOUD by the caller's logger.
enum class SeedStatus : uint8_t
{
	kOk = 0,
	kUnknownRemoval,       // kind 14 (or 19, issue 4462) for a key not held outside a load (the EXE's no-op too)
	kKnownCreation,        // kind 11 for a held key outside an era change (mirrored: a second record)
	kCombatHoldAmbiguous,  // a creation inside the uncertainty band of a held removal's modelled end (decision 14)
	kOutOfStep,            // a dropped event, an event the rules cannot place, or (tests) the reader disagreeing
};

struct SeedCounters
{
	uint64_t created, freed, unknownRemoval, knownCreation, combatHoldAmbiguous, outOfStepEpisodes, heldRemovals;
	// ---- appended by issue 4453 (2026-11-15) ----
	uint64_t loadClears;      // kind 32 applied
	uint64_t loadRemovals;    // kind 14 of an unknown key inside a load window (the EXE's expected no-op, S5): not a fault
	uint64_t eraRecreated;    // remove + create inside an era-change window (kind 33, S3); since issue 4469 every
	                          // Created / Embark of a held key of the era player there, whatever its art
	uint64_t eraFreedOnly;    // 0 since issue 4469 (was: art-changing units the DLL did not re-create, freed at the
	                          // window's end; the EXE removes only what the DLL re-creates, 12.7.11)
	uint64_t embarkSwaps;     // kind 21 of a known key (remove + create)
	uint64_t paradropFrees;   // kind 19 of a known key: remove + create at once (12.7.7 (3); issue 4462, was a free only)
	uint64_t deferredCreates; // creations held to their group's end (the EXE's frame order, 12.7.6)
	// ---- appended by issue 4462 (2026-11-16) ----
	uint64_t combatLateFrees; // removals of units that died in combat (kind 15 seen) applied after the group's creations (12.7.10)
	uint64_t lateCreates;     // creations applied after the group's other creations (stacked / embarked, 12.7.9)
};

// ---- issue 4453: decision 14 (a), the combat-held removal (S4). ----
// Combat classes of the hold table; the scene picks one per combat record.
enum class HoldClass : uint8_t { kMelee = 0, kRanged = 1, kCount = 2 };

// The hold's length and its uncertainty band per class, in seconds of the server's clock from the kind 25 event's
// drain. Values: research/05-graphics-composition/unit-formations.md 12.7.6 (stock path, 4453) and 12.7.7 (1) (the
// product path, 4458; the table that covers both is the core's, issue 4462). There is no built-in default: a
// SeedAllocator without SetHoldParams applies no hold (feed order) and every creation that falls behind a removal the
// hold would have deferred is counted kCombatHoldAmbiguous (never a guessed length).
struct HoldParams
{
	double lengthSec[static_cast<int>(HoldClass::kCount)];
	// a creation within +-band of a held removal's modelled end is ambiguous; per class since issue 4462 (was one value)
	double bandSec[static_cast<int>(HoldClass::kCount)];
};

// One combat (kind 25 or 28) as the hold needs it: the scene fills it from Batch::combats and the player/game record.
struct CombatNote
{
	UnitKey attacker, defender;   // owner -1 for a city side
	bool visualize;               // ICvCombatInfo1::GetVisualizeCombat
	bool humanTurn;               // the active (human) player's turn is in progress (AI turns: no long hold; a combat
	                              // death's removal still follows the group's creations, issue 4462, 12.7.10)
	bool quickCombat;             // the game option GAMEOPTION_QUICK_COMBAT (or PreGame::quickCombat for the human)
	HoldClass cls;
};

// One allocator record as a reader would list it (tests, the offline replay, the reader comparison).
struct SeedRecord
{
	uint32_t index;
	UnitKey key;
	bool mapped;              // false for an orphan of a known creation (the EXE's key map points to the newer record)
};

class SeedAllocator
{
public:
	// Does the unit's art define differ between two eras for its owner (the EXE's era-change test, 12.7.3 S3)?
	// Supplied by the scene through 0041's art resolver. Issue 4469: no longer called (the era window takes the DLL's
	// re-creations as they come, unit-formations.md 12.7.11 [L]); kept so the callers' constructor stays as it was.
	using ArtChangesFn = std::function<bool(const SrvFeed::UnitRecord& unit, int oldEra, int newEra)>;
	using LogFn = std::function<void(SeedStatus, const SrvFeed::Event&, const char* detail)>;

	SeedAllocator(ArtChangesFn artChanges, LogFn log);

	// One feed event, in feed seq order. tSec = the server's monotonic clock when the event was drained (decision 14's
	// hold model). `table` = the unit table after this batch (for an era change's units and the owners' flags).
	void Apply(const SrvFeed::Event& e, double tSec, const std::vector<SrvFeed::UnitRecord>& table);
	// Applies held removals whose modelled end is at or before tSec (call once per frame before reading seeds).
	void Advance(double tSec);
	// A dropped-events notice (kOverflow): an out-of-step episode.
	void NoteDropped(uint64_t count);

	// The unit's current seed (its art-record index); false when the key is not held.
	bool SeedOf(UnitKey key, uint32_t& seed) const;
	bool OutOfStep() const;
	SeedCounters Counters() const;

	// ---- appended by issue 4453 ----
	// Decision 14 (a): the hold table (see HoldParams). Without it no removal is held.
	void SetHoldParams(const HoldParams& p);
	// A kind 25 / 28 event with its record, in feed order, in place of Apply for that event (Apply ignores 25 / 28).
	void NoteCombat(const SrvFeed::Event& e, double tSec, const CombatNote& c);
	// The EXE-side table as the allocator models it: array size, live records in creation order, free list from its head.
	uint32_t ArraySize() const;
	void LiveRecords(std::vector<SeedRecord>& out) const;
	void FreeList(std::vector<uint32_t>& out) const;
	size_t PendingHeld() const;     // removals waiting for their modelled combat end
	// The EXE's frame order (unit-formations.md 12.7.6 [L]): the events applied with one tSec form a group (one drain,
	// standing in for one EXE frame); inside a group a creation is applied after the removals of other units, and the
	// events of a unit stay in order behind its own creation. On by default; Advance ends the group, so call Advance
	// before reading seeds (SeedOf does not see a creation still pending in the group). Off: strict feed order.
	// Issue 4462 (12.7.9, 12.7.10 [L]): at the group's end the creations come in two passes, each in feed order: first
	// the kUnitCreated of units with no earlier event of another unit on their plot in the group, then the rest (a
	// kUnitCreated after such an event, and a kUnitEmbark creation of a key with no record); then the removals of units
	// that died in combat (kind 15 seen for the key) and are not held by decision 14. The group of a load's first
	// creation (the save's units) keeps feed order for its creations (S2, 12.7.7's reload [L]). An era re-creation by
	// kUnitCreated (kind 33's window) and a paradrop (kind 19) are a removal and a creation applied at once, outside the
	// group; an era re-creation by kUnitEmbark is the group's embark swap (issue 4469, unit-formations.md 12.7.11 [L]).
	void SetFrameGrouping(bool on);
	// Tests and the offline comparison only (the reader's table as the starting state of one step): false when the
	// indices are not a partition of 0..size-1. Pending creations and held removals are dropped.
	bool RestoreForTest(uint32_t size, const std::vector<SeedRecord>& live, const std::vector<uint32_t>& freeList);
	size_t PendingCreations() const;

private:
	struct Impl;
	Impl* m_impl;
public:
	~SeedAllocator();
	SeedAllocator(const SeedAllocator&) = delete;
	SeedAllocator& operator=(const SeedAllocator&) = delete;
};

}  // namespace srvunits
