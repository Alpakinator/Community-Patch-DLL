// srv_feed.cpp - srv_feed.h and srv_feed_server.h: the queue, the plot table, the field patches, the counters.
// First version by the epic 3300 lead (2026-11-13: queue, table, counters); owner from wave 1: issue 3303, which added
// the field patches (UpdatePlot), the layout patches kept before the first snapshot, the carry-over of the layout fields
// into a snapshot of the same grid, the per-kind counters and the test reset. The DLL-side capture (the notification
// overrides, the snapshot and refresh at SrvRt::LeaveUpdate) is server/src/remote/srv_remote_feed.cpp.
// No DLL header: this file is built and tested alone (server/tests/feed/).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <unordered_map>
#include "srv_feed.h"
#include "srv_feed_server.h"

namespace SrvFeed {
namespace {

const size_t kPendingCap = 1u << 20;   // layout patches kept before the first snapshot (plots of a huge map: 10,240)

struct Pending
{
	uint32_t mask;
	PlotRecord v;
};

SRWLOCK g_lock = SRWLOCK_INIT;
volatile LONG g_enabled = 0;
size_t g_cap = 65536;
uint32_t g_seq = 0;
std::vector<Event> g_events;
std::vector<uint32_t> g_changed;
std::vector<uint8_t> g_changedMark;   // one byte per plot: already in g_changed since the last Drain
std::vector<PlotRecord> g_plots;      // the published table (the reader's)
std::vector<PlotRecord> g_build;      // the table a snapshot is filling (published by EndSnapshot)
uint32_t g_w = 0, g_h = 0;
uint32_t g_bw = 0, g_bh = 0;
uint32_t g_gen = 0;
bool g_inSnapshot = false;
bool g_dirty = false;
uint64_t g_dropped = 0, g_droppedSinceDrain = 0;
Counters g_c = {};
Counters2 g_c2 = {};
std::unordered_map<uint32_t, Pending> g_pending;   // layout patches before the first snapshot (no table yet)

// ---- issue 4452 (epic 0375): the unit table, the player table, the path and combat side tables ----
volatile LONG g_unitsEnabled = 0;
std::map<uint64_t, UnitRecord> g_units;               // key (owner, id), ordered by owner then id
struct PreBits { uint8_t mask, bits; };
std::unordered_map<uint64_t, PreBits> g_pre;          // call-derived bits of keys not created yet
const size_t kPreCap = 65536;
uint32_t g_unitGen = 0;
std::vector<int32_t> g_pathPlots;
std::vector<PathEntry> g_paths;
std::vector<CombatRecord> g_combats;
std::vector<CombatMember> g_members;
// issue 4783: late combat results (srv_feed.h LateCombatResult), published with the next Drain
std::vector<LateCombatResult> g_late;
const size_t kLateCap = 256;
uint64_t g_lateLost = 0;
bool g_lateKept = true;   // SetLateResultsKept (the tests' negative)
const size_t kPathPlotsCapDefault = 1u << 22, kCombatsCapDefault = 65536, kMembersCapDefault = 1u << 20;
size_t g_pathPlotsCap = kPathPlotsCapDefault, g_combatsCap = kCombatsCapDefault, g_membersCap = kMembersCapDefault;
const uint32_t kPathKeep = 65536;                     // plots kept per path (the crossing's own cap)
PlayerRecord g_players[64];
bool g_playerSet[64] = {};
std::vector<int> g_playersChanged;                    // players whose row changed since the last Drain (unique)
uint32_t g_playerGen = 0;
std::wstring g_unitDbPath;
uint32_t g_unitDbGen = 0;
bool g_unitDbSet = false;
// issue 4463: the unit table as the last Drain left it (CopyUnits' answer) and whether the table changed since that copy
std::vector<UnitRecord> g_drainUnits;
uint32_t g_drainUnitGen = 0;
bool g_unitsChangedSinceCopy = true;

inline bool UnitKind(uint16_t k) { return k >= kUnitCreated && k <= kEraChanged; }
// issue 4463: the kinds whose getter facing the record takes (a creation in the EXE: 11, 21, 19; a move: 12, 30).
// Issue 0449: not a teleport (13): the EXE keeps the unit's heading there although the DLL's setXY turned it
// (unit-server-scene.md 2.2 and 12.4 [L]).
inline bool FacingKind(uint16_t k)
{
	return k == kUnitCreated || k == kUnitEmbark || k == kUnitParadrop || k == kUnitMoved || k == kUnitRebased;
}
inline uint64_t UnitKeyOf(int owner, int32_t id) { return ((uint64_t)(uint8_t)owner << 32) | (uint32_t)id; }

// false when dropped (queue cap)
bool PushLocked(Event& e)
{
	++g_c2.perKind[e.kind < 128 ? e.kind : 127];
	if (UnitKind(e.kind)) ++g_c2.unitEvents;
	if (g_events.size() >= g_cap)
	{
		++g_dropped; ++g_droppedSinceDrain; ++g_c.dropped;
		if (UnitKind(e.kind)) ++g_c2.droppedUnit;
		g_dirty = true;
		e.seq = 0;
		return false;
	}
	e.seq = ++g_seq;
	g_events.push_back(e);
	++g_c.pushed;
	g_dirty = true;
	return true;
}

// The event's own effect on a record's flags (srv_feed.h, PushUnit).
uint8_t EventBits(const Event& e, uint8_t flags)
{
	if (e.kind == kUnitFortify) flags = (uint8_t)(e.a[2] ? (flags | kUnitFlagFortified) : (flags & ~kUnitFlagFortified));
	else if (e.kind == kUnitVisibility)
		flags = (uint8_t)((e.a[2] & 1) ? (flags | kUnitFlagVisible) : (flags & ~kUnitFlagVisible));
	else if (e.kind == kUnitDestroyedInCombat) flags = (uint8_t)(flags | kUnitFlagDead);
	return flags;
}
uint8_t EventMask(const Event& e)
{
	return e.kind == kUnitFortify ? (uint8_t)kUnitFlagFortified : e.kind == kUnitVisibility ? (uint8_t)kUnitFlagVisible :
	       e.kind == kUnitDestroyedInCombat ? (uint8_t)kUnitFlagDead : (uint8_t)0;
}

// Applies op for the event (seq already assigned or 0) to the table. Lock held.
void ApplyUnitLocked(const Event& e, const UnitRecord* rec, UnitOp op)
{
	if (op == UnitOp::kNone || !rec)
		return;
	if (rec->owner < 0 || rec->owner > 63)
	{
		++g_c2.unitBadKey;
		return;
	}
	const uint64_t key = UnitKeyOf(rec->owner, rec->id);
	auto it = g_units.find(key);
	g_unitsChangedSinceCopy = true;   // issue 4463: the next Drain copies the table
	if (op == UnitOp::kRemove)
	{
		if (it == g_units.end()) ++g_c2.unitRemoveUnknown;
		else { g_units.erase(it); ++g_c2.unitRemoves; }
		g_pre.erase(key);
		return;
	}
	if (op == UnitOp::kPatchFlags)
	{
		if (it == g_units.end()) { ++g_c2.unitPatchUnknown; return; }
		it->second.flags = rec->flags;
		it->second.seq = e.seq;
		++g_c2.unitUpdates;
		return;
	}
	// kUpsert
	UnitRecord r = *rec;
	r.seq = e.seq;
	r.reserved = 0;
	if (it != g_units.end())
	{
		if (e.kind == kUnitCreated) ++g_c2.unitCreateKnown;
		r.flags = (uint8_t)((r.flags & ~kUnitFlagsFromCalls) | (it->second.flags & kUnitFlagsFromCalls));
		r.flags = EventBits(e, r.flags);
		if (!FacingKind(e.kind))
			r.facing = it->second.facing;   // issue 4463: the EXE keeps the creation's facing until a move
		it->second = r;
		++g_c2.unitUpdates;
		return;
	}
	if (e.kind != kUnitCreated && e.kind != kUnitEmbark)
	{
		// issue 4463: a Visibility before the creation is ignored (the EXE ignores it, unit-server-scene.md 12.1 [L])
		const uint8_t m = e.kind == kUnitVisibility ? (uint8_t)0 : EventMask(e);
		++g_c2.preCreationEvents;
		if (!m)
			return;
		auto p = g_pre.find(key);
		if (p != g_pre.end())
		{
			p->second.mask |= m;
			p->second.bits = (uint8_t)((p->second.bits & ~m) | (EventBits(e, 0) & m));
		}
		else if (g_pre.size() < kPreCap)
		{
			PreBits b = {m, (uint8_t)(EventBits(e, 0) & m)};
			g_pre.emplace(key, b);
		}
		else
			++g_c2.preCreationDropped;
		return;
	}
	if (e.kind == kUnitEmbark) ++g_c2.embarkCreations;
	uint8_t f = (uint8_t)((r.flags & ~kUnitFlagsFromCalls) | kUnitFlagVisible);
	auto p = g_pre.find(key);
	if (p != g_pre.end())
	{
		f = (uint8_t)((f & ~p->second.mask) | (p->second.bits & p->second.mask));
		g_pre.erase(p);
	}
	r.flags = EventBits(e, f);
	g_units.emplace(key, r);
	++g_c2.unitInserts;
}

// Issue 4463: kEventNoRecord on an event whose key the table does not hold and which does not create it. Lock held.
void MarkNoRecordLocked(Event& e, const UnitRecord* rec, UnitOp op)
{
	if (op == UnitOp::kNone || !rec || rec->owner < 0 || rec->owner > 63)
		return;
	if (op == UnitOp::kUpsert && (e.kind == kUnitCreated || e.kind == kUnitEmbark))
		return;
	if (g_units.find(UnitKeyOf(rec->owner, rec->id)) == g_units.end())
		e.flags = (uint16_t)(e.flags | kEventNoRecord);
}

// Pushes a unit event and applies its table effect. Lock held.
uint32_t PushUnitLocked(Event& e, const UnitRecord* rec, UnitOp op)
{
	MarkNoRecordLocked(e, rec, op);
	PushLocked(e);
	ApplyUnitLocked(e, rec, op);
	return e.seq;
}

// A side-table entry's event that cannot be kept: counted as a drop (kOverflow follows), table effect applied.
uint32_t DropSideLocked(Event& e, const UnitRecord* rec, UnitOp op)
{
	++g_c2.perKind[e.kind < 128 ? e.kind : 127];
	++g_c2.unitEvents;
	++g_c2.sideTableFull;
	++g_c2.droppedUnit;
	++g_dropped; ++g_droppedSinceDrain; ++g_c.dropped;
	g_dirty = true;
	e.seq = 0;
	e.a[2] = -1;
	ApplyUnitLocked(e, rec, op);
	return 0;
}

inline void SetFlag(PlotRecord& r, uint8_t bit, bool on)
{
	r.flags = (uint8_t)(on ? (r.flags | bit) : (r.flags & ~bit));
}

// Copies the masked fields of v into r. Returns true when r changed (changeGen and known excepted: known is set by
// kFGetters / kFLayout and counts as a change only the first time).
bool ApplyMask(PlotRecord& r, uint32_t mask, const PlotRecord& v)
{
	const PlotRecord before = r;
	if (mask & kFTerrain) r.terrain = v.terrain;
	if (mask & kFPlotType) r.plotType = v.plotType;
	if (mask & kFFeature) r.feature = v.feature;
	if (mask & kFRoute) r.route = v.route;
	if (mask & kFResource) r.resource = v.resource;
	if (mask & kFImprovement) r.improvement = v.improvement;
	if ((mask & kFImprovementGetter) && !(r.known & 2) && !(mask & kFLayout)) r.improvement = v.improvement;
	if (mask & kFOwner) r.owner = v.owner;
	if (mask & kFRiver) SetFlag(r, 1, (v.flags & 1) != 0);
	if (mask & kFImpPillaged) SetFlag(r, 2, (v.flags & 2) != 0);
	if (mask & kFRoutePillaged) SetFlag(r, 4, (v.flags & 4) != 0);
	if (mask & kFNaturalWonder) SetFlag(r, 8, (v.flags & 8) != 0);
	if (mask & kFCity) SetFlag(r, 16, (v.flags & 16) != 0);
	if (mask & kFFow) r.fow = v.fow;
	if (mask & kFImprovementState) r.improvementState = v.improvementState;
	if (mask & kFRouteState) r.routeState = v.routeState;
	if (mask & kFArtType) r.artType = v.artType;   // issue 4305
	if (mask & kFRiverEdges)   // ticket 0631: the three own edge bits only; known bit 2 says they were read
	{
		r.riverEdges = (uint8_t)(v.riverEdges & 7u);
		r.known |= kPlotKnownRiverEdges;
	}
	if (mask & kFGetters) r.known |= 1;
	if (mask & kFLayout) r.known |= 2;
	return before.terrain != r.terrain || before.plotType != r.plotType || before.feature != r.feature ||
	       before.route != r.route || before.resource != r.resource || before.improvement != r.improvement ||
	       before.owner != r.owner || before.flags != r.flags || before.fow != r.fow ||
	       before.improvementState != r.improvementState || before.routeState != r.routeState || before.known != r.known ||
	       before.artType != r.artType || before.riverEdges != r.riverEdges;
}

void MarkChangedLocked(uint32_t index)
{
	if (!g_changedMark[index]) { g_changedMark[index] = 1; g_changed.push_back(index); }
	g_dirty = true;
}

}  // namespace

PlotRecord EmptyRecord()
{
	PlotRecord r;
	r.terrain = -1; r.plotType = -1; r.feature = -1; r.route = -1;
	r.resource = -1; r.improvement = -1; r.owner = -1;
	r.flags = 0; r.fow = 0; r.riverEdges = 0; r.changeGen = 0;
	r.improvementState = -1; r.routeState = -1; r.known = 0; r.artType = 0;
	return r;
}

uint8_t ArtTypeFromDll(int v, bool* outside)
{
	const bool out = v < 0 || v > 4;
	if (outside) *outside = out;
	return out ? (uint8_t)0 : (uint8_t)v;
}

void Push(const Event& e)
{
	if (!g_enabled) return;
	Event x = e;
	AcquireSRWLockExclusive(&g_lock);
	PushLocked(x);
	ReleaseSRWLockExclusive(&g_lock);
}

// ---- issue 4452 (epic 0375): the unit writers ----
uint32_t PushUnit(Event e, const UnitRecord* rec, UnitOp op)
{
	if (!g_enabled || !g_unitsEnabled) return 0;
	AcquireSRWLockExclusive(&g_lock);
	const uint32_t s = PushUnitLocked(e, rec, op);
	ReleaseSRWLockExclusive(&g_lock);
	return s;
}

uint32_t PushUnitPath(Event e, const UnitRecord* rec, UnitOp op, const int32_t* plots, uint32_t n)
{
	if (!g_enabled || !g_unitsEnabled) return 0;
	const uint32_t keep = std::min(n, kPathKeep);
	e.a[3] = (int32_t)std::min<uint32_t>(n, 0x7fffffff);
	AcquireSRWLockExclusive(&g_lock);
	uint32_t s;
	if (g_events.size() >= g_cap)
	{
		e.a[2] = -1;   // the queue drops it (kOverflow at the next Drain); the table effect applies
		s = PushUnitLocked(e, rec, op);
	}
	else if (g_pathPlots.size() + keep > g_pathPlotsCap)
		s = DropSideLocked(e, rec, op);
	else
	{
		e.a[2] = (int32_t)g_paths.size();
		s = PushUnitLocked(e, rec, op);
		PathEntry p = {s, (uint32_t)g_pathPlots.size(), keep};
		if (keep && plots) g_pathPlots.insert(g_pathPlots.end(), plots, plots + keep);
		else p.length = 0;
		g_paths.push_back(p);
		++g_c2.pathsKept;
		g_c2.pathPlotsKept += p.length;
	}
	ReleaseSRWLockExclusive(&g_lock);
	return s;
}

uint32_t PushCombat(Event e, const CombatRecord& c, const CombatMember* members, uint32_t nMembers,
                    const UnitRecord* attacker, const UnitRecord* defender)
{
	if (!g_enabled || !g_unitsEnabled) return 0;
	if (!members) nMembers = 0;
	AcquireSRWLockExclusive(&g_lock);
	uint32_t s;
	if (g_events.size() >= g_cap)
	{
		e.a[2] = -1;
		s = PushUnitLocked(e, nullptr, UnitOp::kNone);
	}
	else if (g_combats.size() + 1 > g_combatsCap || g_members.size() + nMembers > g_membersCap)
		s = DropSideLocked(e, nullptr, UnitOp::kNone);
	else
	{
		e.a[2] = (int32_t)g_combats.size();
		// ticket 5062 (W108): kind 28 keeps the caller's a[3] (the attacking city's plot + 1, srv_feed.h); live, this line
		// zeroed it and route S never placed the city (research unit-combat-server.md 18.37.4); kind 25's a[3] stays 0
		if (e.kind != kCityCombat) e.a[3] = 0;
		s = PushUnitLocked(e, nullptr, UnitOp::kNone);
		CombatRecord r = c;
		r.seq = s;
		r.kind = e.kind;
		r.firstMember = (uint32_t)g_members.size();
		r.nMembers = nMembers;
		if (!(r.flags & kCombatDamageKnown)) r.reserved = 0;   // issue 4726: the per-combat damage words
		r.flags = (uint16_t)(r.flags & ~kCombatReturnedKnown);
		r.returned = 0;
		g_combats.push_back(r);
		if (nMembers) g_members.insert(g_members.end(), members, members + nMembers);
		++g_c2.combatsKept;
		g_c2.combatMembersKept += nMembers;
	}
	// the attacker's and defender's records, refreshed with the combat (no event of their own; the combat's seq)
	if (attacker) ApplyUnitLocked(e, attacker, UnitOp::kUpsert);
	if (defender) ApplyUnitLocked(e, defender, UnitOp::kUpsert);
	ReleaseSRWLockExclusive(&g_lock);
	return s;
}

bool SetCombatResult(uint32_t seq, uint32_t returned)
{
	if (!g_enabled || !g_unitsEnabled || !seq) return false;
	bool ok = false;
	AcquireSRWLockExclusive(&g_lock);
	for (size_t i = g_combats.size(); i-- > 0;)
		if (g_combats[i].seq == seq)
		{
			g_combats[i].returned = returned;
			g_combats[i].flags = (uint16_t)(g_combats[i].flags | kCombatReturnedKnown);
			ok = true;
			break;
		}
	if (ok) ++g_c2.combatResultsSet;
	else
	{
		++g_c2.combatResultsLate;
		// issue 4783: the record went with an earlier Drain; the pair goes with the next one (Batch::lateResults)
		if (g_lateKept)
		{
			if (g_late.size() >= kLateCap)
			{
				g_late.erase(g_late.begin());
				++g_lateLost;
			}
			LateCombatResult l;
			l.seq = seq;
			l.returned = returned;
			g_late.push_back(l);
			g_dirty = true;
		}
	}
	ReleaseSRWLockExclusive(&g_lock);
	return ok;
}

void SetPlayer(const PlayerRecord& r)
{
	if (!g_enabled || !g_unitsEnabled) return;
	AcquireSRWLockExclusive(&g_lock);
	if (r.player < 0 || r.player > 63)
		++g_c2.playerBadIndex;
	else
	{
		const int p = r.player;
		PlayerRecord x = r;
		x.changeGen = g_playerSet[p] ? g_players[p].changeGen : 0;
		if (g_playerSet[p] && memcmp(&x, &g_players[p], sizeof(PlayerRecord)) == 0)
			++g_c2.playerRowsSame;
		else
		{
			x.changeGen = ++g_playerGen;
			g_players[p] = x;
			g_playerSet[p] = true;
			if (std::find(g_playersChanged.begin(), g_playersChanged.end(), p) == g_playersChanged.end())
				g_playersChanged.push_back(p);
			++g_c2.playerRowsChanged;
			g_dirty = true;
		}
	}
	ReleaseSRWLockExclusive(&g_lock);
}

uint32_t ResetUnitsAt(int32_t player)
{
	if (!g_enabled || !g_unitsEnabled) return 0;
	AcquireSRWLockExclusive(&g_lock);
	g_units.clear();
	g_pre.clear();
	++g_unitGen;
	g_unitsChangedSinceCopy = true;   // issue 4463
	++g_c2.unitResets;
	g_dirty = true;
	Event e = {};
	e.kind = kLoadBegin; e.plot = -1; e.turn = -1;
	e.a[0] = (int32_t)g_unitGen; e.a[1] = player;
	PushLocked(e);
	ReleaseSRWLockExclusive(&g_lock);
	return e.seq;
}

void ResetUnits() { ResetUnitsAt(0); }

// Issue 4463: the copy the last Drain took (srv_feed.h), not the live table: a PushUnit between the render thread's Drain
// and this call (the DLL's game-core thread does that, unit-server-scene.md 3.4) is not in it.
void CopyUnits(std::vector<UnitRecord>& out, uint32_t& unitTableGen)
{
	AcquireSRWLockShared(&g_lock);
	out = g_drainUnits;
	unitTableGen = g_drainUnitGen;
	ReleaseSRWLockShared(&g_lock);
}

void CopyPlayers(std::vector<PlayerRecord>& out)
{
	AcquireSRWLockShared(&g_lock);
	out.clear();
	for (int p = 0; p < 64; ++p)
		if (g_playerSet[p]) out.push_back(g_players[p]);
	ReleaseSRWLockShared(&g_lock);
}

bool GetPlayerRow(int player, PlayerRecord& out)
{
	if (player < 0 || player > 63) return false;
	AcquireSRWLockShared(&g_lock);
	const bool ok = g_playerSet[player];
	if (ok) out = g_players[player];
	ReleaseSRWLockShared(&g_lock);
	return ok;
}

bool GetUnit(int owner, int32_t id, UnitRecord& out)
{
	if (owner < 0 || owner > 63) return false;
	AcquireSRWLockShared(&g_lock);
	auto it = g_units.find(UnitKeyOf(owner, id));
	const bool ok = it != g_units.end();
	if (ok) out = it->second;
	ReleaseSRWLockShared(&g_lock);
	return ok;
}

void SetUnitDatabase(const std::wstring& path, uint32_t gen)
{
	AcquireSRWLockExclusive(&g_lock);
	g_unitDbPath = path;
	g_unitDbGen = gen;
	g_unitDbSet = true;
	++g_c2.unitDbSets;
	ReleaseSRWLockExclusive(&g_lock);
}

bool GetUnitDatabase(std::wstring& path, uint32_t& gen)
{
	AcquireSRWLockShared(&g_lock);
	const bool ok = g_unitDbSet;
	if (ok) { path = g_unitDbPath; gen = g_unitDbGen; }
	ReleaseSRWLockShared(&g_lock);
	return ok;
}

void SetUnitsEnabled(bool on) { InterlockedExchange(&g_unitsEnabled, on ? 1 : 0); }
bool UnitsEnabled() { return g_enabled != 0 && g_unitsEnabled != 0; }

// ticket 4875 (epic 0561): the model module's library needs the same database copy (srv_feed.h)
static volatile LONG g_modelDbWanted = 0;
void SetModelDatabaseWanted(bool on) { InterlockedExchange(&g_modelDbWanted, on ? 1 : 0); }
bool ModelDatabaseWanted() { return g_enabled != 0 && g_modelDbWanted != 0; }
bool DatabaseCopyWanted() { return UnitsEnabled() || ModelDatabaseWanted(); }

uint32_t PushModelLoadBegin(int32_t player)
{
	if (!g_enabled || g_unitsEnabled) return 0;   // the unit feed's ResetUnitsAt pushes it (never twice)
	AcquireSRWLockExclusive(&g_lock);
	g_dirty = true;
	Event e = {};
	e.kind = kLoadBegin; e.plot = -1; e.turn = -1;
	e.a[0] = (int32_t)g_unitGen; e.a[1] = player;
	PushLocked(e);
	ReleaseSRWLockExclusive(&g_lock);
	return e.seq;
}

void SetSideCapsForTest(size_t pathPlots, size_t combats, size_t members)
{
	AcquireSRWLockExclusive(&g_lock);
	g_pathPlotsCap = pathPlots ? pathPlots : kPathPlotsCapDefault;
	g_combatsCap = combats ? combats : kCombatsCapDefault;
	g_membersCap = members ? members : kMembersCapDefault;
	ReleaseSRWLockExclusive(&g_lock);
}

void BeginSnapshot(uint32_t width, uint32_t height)
{
	if (!g_enabled) return;
	const size_t n = (size_t)width * height;
	std::vector<PlotRecord> fresh(n, EmptyRecord());
	AcquireSRWLockExclusive(&g_lock);
	// Issue 3303: the layout fields have no pure getter; a snapshot of the same grid keeps the ones the events gave.
	// The new table is built beside the published one; the reader sees the old table until EndSnapshot.
	if (width == g_w && height == g_h && g_plots.size() == n)
		for (size_t i = 0; i < n; ++i)
			if (g_plots[i].known & 2)
				ApplyMask(fresh[i], kFLayoutAll, g_plots[i]);
	g_build.swap(fresh);
	g_bw = width; g_bh = height;
	g_inSnapshot = true;
	ReleaseSRWLockExclusive(&g_lock);
}

void SetPlot(uint32_t index, const PlotRecord& r)
{
	if (!g_enabled) return;
	AcquireSRWLockExclusive(&g_lock);
	std::vector<PlotRecord>& t = g_inSnapshot ? g_build : g_plots;
	if (index < t.size())
	{
		PlotRecord x = r;
		x.changeGen = g_gen + (g_inSnapshot ? 1 : 0);
		t[index] = x;
		++g_c.plotSets;
		if (!g_inSnapshot) MarkChangedLocked(index);
	}
	else
		++g_c2.badIndex;
	ReleaseSRWLockExclusive(&g_lock);
}

bool UpdatePlot(uint32_t index, uint32_t mask, const PlotRecord& v)
{
	if (!g_enabled) return false;
	bool changed = false;
	AcquireSRWLockExclusive(&g_lock);
	std::vector<PlotRecord>& t = g_inSnapshot ? g_build : g_plots;
	if (t.empty() && !g_inSnapshot)
	{
		const uint32_t lm = mask & kFLayoutAll;
		if (lm)
		{
			auto it = g_pending.find(index);
			if (it != g_pending.end())
			{
				ApplyMask(it->second.v, lm, v);
				it->second.mask |= lm;
				++g_c2.pendingKept;
			}
			else if (g_pending.size() < kPendingCap)
			{
				Pending p;
				p.mask = lm;
				p.v = EmptyRecord();
				ApplyMask(p.v, lm, v);
				g_pending.emplace(index, p);
				++g_c2.pendingKept;
			}
			else
				++g_c2.pendingDropped;
		}
	}
	else if (index < t.size())
	{
		PlotRecord& r = t[index];
		changed = ApplyMask(r, mask, v);
		if (changed)
		{
			r.changeGen = g_gen + (g_inSnapshot ? 1 : 0);
			++g_c.plotSets;
			if (!g_inSnapshot)
			{
				++g_c2.patchesChanged;
				MarkChangedLocked(index);
			}
		}
		else if (!g_inSnapshot)
			++g_c2.patchesSame;
	}
	else
		++g_c2.badIndex;
	ReleaseSRWLockExclusive(&g_lock);
	return changed;
}

void EndSnapshot()
{
	if (!g_enabled) return;
	AcquireSRWLockExclusive(&g_lock);
	if (g_inSnapshot)
	{
		for (auto& kv : g_pending)
		{
			if (kv.first < g_build.size())
			{
				ApplyMask(g_build[kv.first], kv.second.mask, kv.second.v);
				++g_c2.pendingApplied;
			}
			else
				++g_c2.pendingDropped;
		}
		g_pending.clear();
		g_plots.swap(g_build);
		g_build.clear();
		g_build.shrink_to_fit();
		g_w = g_bw; g_h = g_bh;
		g_changed.clear();
		g_changedMark.assign(g_plots.size(), 0);
		g_inSnapshot = false;
		++g_gen;
		++g_c.snapshots;
		Event e = {};
		e.kind = kSnapshot; e.plot = -1; e.turn = -1;
		e.a[0] = (int32_t)g_w; e.a[1] = (int32_t)g_h; e.a[2] = (int32_t)g_gen;
		PushLocked(e);
	}
	ReleaseSRWLockExclusive(&g_lock);
}

bool Drain(Batch& out)
{
	out.events.clear();
	out.changedPlots.clear();
	out.pathPlots.clear(); out.paths.clear(); out.players.clear(); out.combats.clear(); out.combatMembers.clear();
	out.lateResults.clear();
	AcquireSRWLockExclusive(&g_lock);
	// issue 4452: the unit side tables of this batch and the changed player rows
	out.pathPlots.swap(g_pathPlots);
	out.paths.swap(g_paths);
	out.combats.swap(g_combats);
	out.combatMembers.swap(g_members);
	out.lateResults.swap(g_late);   // issue 4783
	out.lateResultsLost = g_lateLost;
	for (int p : g_playersChanged) out.players.push_back(g_players[p]);
	g_playersChanged.clear();
	out.unitTableGen = g_unitGen;
	// issue 4463: the unit table as these events leave it, in this lock acquisition (CopyUnits returns it)
	if (g_unitsEnabled && g_unitsChangedSinceCopy)
	{
		g_drainUnits.clear();
		g_drainUnits.reserve(g_units.size());
		for (const auto& kv : g_units) g_drainUnits.push_back(kv.second);
		g_drainUnitGen = g_unitGen;
		g_unitsChangedSinceCopy = false;
	}
	bool any = g_dirty;
	if (g_droppedSinceDrain)
	{
		Event e = {};
		e.seq = ++g_seq; e.kind = kOverflow; e.plot = -1; e.turn = -1;
		e.a[0] = (int32_t)std::min<uint64_t>(g_droppedSinceDrain, 0x7fffffff);
		g_events.push_back(e);
		++g_c2.perKind[kOverflow];
		g_droppedSinceDrain = 0;
	}
	out.events.swap(g_events);
	out.changedPlots.swap(g_changed);
	for (uint32_t i : out.changedPlots) if (i < g_changedMark.size()) g_changedMark[i] = 0;
	out.snapshotGen = g_gen;
	out.width = g_w; out.height = g_h;
	out.dropped = g_dropped;
	g_dirty = false;
	++g_c.drains;
	g_events.reserve(1024);
	ReleaseSRWLockExclusive(&g_lock);
	return any;
}

void CopyPlots(std::vector<PlotRecord>& out, uint32_t& snapshotGen, uint32_t& width, uint32_t& height)
{
	AcquireSRWLockShared(&g_lock);
	out = g_plots;   // the published table; a snapshot being filled is not visible before EndSnapshot (issue 3303)
	snapshotGen = g_gen; width = g_w; height = g_h;
	ReleaseSRWLockShared(&g_lock);
}

bool GetPlot(uint32_t index, PlotRecord& out)
{
	AcquireSRWLockShared(&g_lock);
	const bool ok = index < g_plots.size();
	if (ok) out = g_plots[index];
	ReleaseSRWLockShared(&g_lock);
	return ok;
}

void GetSize(uint32_t& width, uint32_t& height)
{
	AcquireSRWLockShared(&g_lock);
	width = g_w; height = g_h;
	ReleaseSRWLockShared(&g_lock);
}

void SetEnabled(bool on) { InterlockedExchange(&g_enabled, on ? 1 : 0); }
bool Enabled() { return g_enabled != 0; }

void SetQueueCap(size_t cap)
{
	AcquireSRWLockExclusive(&g_lock);
	g_cap = cap ? cap : 1;
	ReleaseSRWLockExclusive(&g_lock);
}

void SetLateResultsKept(bool on)
{
	AcquireSRWLockExclusive(&g_lock);
	g_lateKept = on;
	if (!on) g_late.clear();
	ReleaseSRWLockExclusive(&g_lock);
}

Counters GetCounters()
{
	AcquireSRWLockShared(&g_lock);
	Counters c = g_c;
	ReleaseSRWLockShared(&g_lock);
	return c;
}

Counters2 GetCounters2()
{
	AcquireSRWLockShared(&g_lock);
	Counters2 c = g_c2;
	ReleaseSRWLockShared(&g_lock);
	return c;
}

void ResetForTest()
{
	AcquireSRWLockExclusive(&g_lock);
	g_enabled = 0;
	g_cap = 65536;
	g_seq = 0;
	g_events.clear(); g_changed.clear(); g_changedMark.clear(); g_plots.clear(); g_build.clear(); g_pending.clear();
	g_w = g_h = 0; g_bw = g_bh = 0; g_gen = 0;
	g_inSnapshot = false; g_dirty = false;
	g_dropped = g_droppedSinceDrain = 0;
	g_c = Counters();
	g_c2 = Counters2();
	// issue 4452
	g_unitsEnabled = 0;
	g_units.clear(); g_pre.clear(); g_unitGen = 0;
	g_pathPlots.clear(); g_paths.clear(); g_combats.clear(); g_members.clear();
	g_late.clear(); g_lateLost = 0; g_lateKept = true;   // issue 4783
	g_pathPlotsCap = kPathPlotsCapDefault; g_combatsCap = kCombatsCapDefault; g_membersCap = kMembersCapDefault;
	for (int p = 0; p < 64; ++p) { g_playerSet[p] = false; g_players[p] = PlayerRecord(); }
	g_playersChanged.clear(); g_playerGen = 0;
	g_unitDbPath.clear(); g_unitDbGen = 0; g_unitDbSet = false;
	g_drainUnits.clear(); g_drainUnitGen = 0; g_unitsChangedSinceCopy = true;   // issue 4463
	ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace SrvFeed
