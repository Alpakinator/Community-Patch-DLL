// srv_units_queue.h - the per-unit hold of a fighter's DLL-side events (epic 0042, issue 4774, route S: D210;
// research/05-graphics-composition/unit-combat-server.md 6 "The per-unit hold and the fight player (4774)"; the rules from
// combat-choreography.md 3.2-3.4 and 8 items 1-2, the Architect's route S note 3.1). Written 2026-11-20 for this repository
// (GPL-3.0-or-later). No EXE code (D83): the behaviour is the knowledge base's description of the EXE's queue.
//
// The EXE's queue keeps a fighter's later records (its damage, its death, its removal, its moves) waiting behind the held
// RunCombatSim record until that fight's simulation ends, then lets them go in posting order; records of other units
// pass (0321 3.2, 3.4). The server's DLL-side events of a fighter arrive at once (4771 P1 [L]), so the server keeps the
// same wait itself, released by its own fight clock (srv_units_fights.h, 4782):
//
//   UnitHold   the ops of kinds 12 moved, 13 teleported, 14 destroyed, 15 died in combat, 16 damage, 30 rebased and the
//              EXE's arrival (mode exe: AnimFeed::arrive) of a unit named by a pending or playing fight wait in that unit's
//              queue, each owned by the latest fight that named the unit when it came. When a fight ends, the front run of
//              each of its units' queues whose owners have all ended comes back in order (several fights on one unit:
//              what came between fight A and fight B comes back at A's end, before B starts). Other kinds and other
//              units pass. A combat with Quick Combat on, or not visualised, is no fight (the clock queues none), so it
//              holds nothing: everything passes at once.
//   HoldView   what the scene draws of a held unit: the unit as it was listed in the last drawn frame before its hold
//              began (plot, facing, damage, flags, seed, its wrap copies), with the ops that came back since applied
//              (damage, place, move to the path's end, death); the feed's table, which is final at once, is not drawn
//              for it. A held unit whose removal waits stays listed in the copies it was last listed in.
//
// Limits (named, counted, LOUD through the caller; never a silent drop): a unit queue above kHoldUnitMax ops or all
// queues above kHoldTotalMax: that unit's queue comes back at once, in order, and the unit passes everything until its
// fights end ("broken"). A load (kind 32) drops every held op (the load removes every unit anyway) and every view.
// Nothing throws. One thread (the render thread).
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "c5r/units/unit_scene.h"

namespace srvunits {

const size_t kHoldUnitMax = 256;     // ops held for one unit (a fighter makes a handful per fight)
const size_t kHoldTotalMax = 8192;   // ops held for every unit together
const uint16_t kHoldArrive = 0xFF01; // the EXE's arrival of a walk (AnimFeed's own op kind for arrive(), mode exe)

// One held op: AnimFeed's op with its seq (0 for an arrival, which has none) and, for a kind 12, the path's last plot.
struct HeldOp
{
	uint16_t kind = 0;
	int32_t owner = -1, id = -1, a2 = 0, a3 = 0, pathLen = 0;
	int32_t dest = -1;           // kind 12: the plot index the path ends on (-1: not known)
	uint32_t seq = 0;
};

struct HoldCounters
{
	uint64_t begun = 0;          // (unit, fight) pairs that began a hold
	uint64_t held = 0;           // ops held
	uint64_t released = 0;       // ops given back at a fight's end
	uint64_t passedHeldUnit = 0; // ops of a held unit of a kind that does not wait (fortify, visibility, ...)
	uint64_t ends = 0;           // fight ends taken
	uint64_t endsUnknown = 0;    // an end for a fight that held no unit (or ended before): nothing changes
	uint64_t overflowUnit = 0;   // a unit queue above kHoldUnitMax: given back at once, the unit broken
	uint64_t overflowTotal = 0;  // all queues above kHoldTotalMax: the same for the unit whose op came
	uint64_t flushed = 0;        // ops given back by an overflow
	uint64_t passedBroken = 0;   // ops of a broken unit that passed
	uint64_t droppedLoad = 0;    // ops dropped by a load (clear())
	uint64_t clears = 0;
	size_t maxUnit = 0, maxTotal = 0;
};

class UnitHold
{
public:
	// The kinds that wait for a held unit (12, 13, 14, 15, 16, 30 and kHoldArrive).
	static bool waits(uint16_t kind);
	// A fight (its seq, the DLL's order) names the unit: from now on its waiting ops are held, owned by this fight.
	void begin(int32_t owner, int32_t id, uint32_t fightSeq);
	enum Result { Pass = 0, Held = 1, Overflow = 2 };
	// One op of the feed, in seq order. Pass: the caller applies it now. Held: it waits. Overflow: the unit's queue is
	// in `flushed` (in order; the caller applies those first, then this op) and `why` names the limit.
	Result take(const HeldOp& op, std::vector<HeldOp>& flushed, std::string& why);
	// The fight ended (any reason): the ops that come back now, in order, are appended to `out` (per unit in its order;
	// units in key order). False for a fight that held nothing (counted endsUnknown).
	bool end(uint32_t fightSeq, std::vector<HeldOp>& out);
	// A load: every queue and fight dropped (counted droppedLoad).
	void clear();
	bool held(int32_t owner, int32_t id) const;       // a fight names it and it is not broken
	bool named(int32_t owner, int32_t id) const;      // a fight names it (held or broken)
	size_t queued() const { return total_; }
	size_t queued(int32_t owner, int32_t id) const;
	size_t units() const { return units_.size(); }
	// Every unit a fight names, (owner, id).
	void namedUnits(std::set<std::pair<int32_t, int32_t>>& out) const;
	const HoldCounters& counters() const { return c_; }

private:
	struct Entry
	{
		uint32_t fight = 0;
		HeldOp op;
	};
	struct Unit
	{
		std::vector<uint32_t> fights;   // seqs of the fights that name it and have not ended, in order
		std::deque<Entry> ops;
		bool broken = false;
	};
	static int64_t key(int32_t owner, int32_t id) { return ((int64_t)owner << 32) | (uint32_t)id; }
	void flush(Unit& u, std::vector<HeldOp>& out);
	std::map<int64_t, Unit> units_;
	size_t total_ = 0;
	HoldCounters c_;
};

struct ViewCounters
{
	uint64_t frozen = 0;         // views taken (a unit's hold began)
	uint64_t missing = 0;        // a held unit not listed in the frame before its hold: drawn from the table (named)
	uint64_t substituted = 0;    // unit-frames drawn from a view instead of the table
	uint64_t relisted = 0;       // unit-frames of a held unit the table no longer lists (its removal waits)
	uint64_t applied = 0;        // ops given back that changed a view
	uint64_t released = 0;       // views dropped (the unit's hold ended)
};

class HoldView
{
public:
	// At each drawn frame, before the copies are applied: the copy counter starts again (copies come in order 0, 1, 2).
	void beginFrame();
	// The units listed in wrap copy `copy` this frame (after the view was applied): the base of a view taken later.
	void remember(const c5r::units::UnitScene& sc, int copy);
	// The unit's hold began: its view is the unit as last remembered. False when it was not listed (counted missing).
	bool freeze(int32_t owner, int32_t id);
	// An op given back for a frozen unit (or passed before its fight in the same frame): applied to its view. width: the
	// map's width (plot index -> x, y); facing: the record's facing at the frame's end (-1 keeps the view's).
	void apply(const HeldOp& op, uint32_t width, int facing);
	// The unit's hold ended: its view is dropped and the table is drawn again.
	void release(int32_t owner, int32_t id);
	void clear();
	bool frozen(int32_t owner, int32_t id) const;
	size_t views() const { return views_.size(); }
	// Copy `copy` of the frame: every listed held unit drawn from its view; a held unit the copy does not list (its removal
	// waits) listed again when its view was in that copy, with its player row.
	void applyTo(c5r::units::UnitScene& sc, int copy);
	// The copy index the next applyTo / remember of this frame stands for (0, 1, 2), counted by nextCopy().
	int nextCopy() { return copy_ < 3 ? copy_++ : 2; }
	const ViewCounters& counters() const { return c_; }

private:
	struct Last
	{
		c5r::units::UnitInput in;
		c5r::units::PlayerInput player;
		bool havePlayer = false;
		uint8_t copies = 0;     // bit c: listed in copy c
	};
	struct View
	{
		Last last;
		bool removed = false;   // its kind 14 came back: no longer listed
	};
	static int64_t key(int32_t owner, int32_t id) { return ((int64_t)owner << 32) | (uint32_t)id; }
	std::map<int64_t, Last> prev_, cur_;
	std::map<int64_t, View> views_;
	int copy_ = 0;
	ViewCounters c_;
};

}  // namespace srvunits
