// srv_units_seed.cpp - the formation-seed allocator of the server's unit scene (epic 0375, issue 4453).
// Written 2026-11-15 for this repository (GPL-3.0-or-later).
//
// Re-implemented from the knowledge base's description of the EXE's art-record table (D83: no EXE code, no decompiler
// text): research/05-graphics-composition/unit-formations.md 12.1 (the table), 12.7.1 (the creation and removal
// handlers), 12.7.2 (the free rule), 12.7.3 (S1-S6), 12.7.6 (the live checks and the hold table of decision 14);
// unit-server-scene.md sections 4 and 10. Every rule below names the paragraph it follows.
// Issue 4462 (2026-11-16): rules (a) era re-creation and (b) paradrop as a removal and a creation at once (12.7.7),
// (d) a combat death's removal after the group's creations (12.7.10), (e) late creations of stacked and embarked units
// (12.7.9), and the hold's band per class; replayed against the reader on 4458's recorded feeds
// (server/evidence/0375/4462/).
// Issue 4469 (2026-11-16): the era window follows the DLL's re-creations, not an art test (unit-formations.md 12.7.11
// [L]): inside kind 33's window a kUnitCreated of a held key of the era player is a removal and a creation at once
// whatever the unit's art (4466's great war infantry), and a kUnitEmbark of a held key is the ordinary embark swap of
// the frame group (the EXE frees every embarked re-creation of one era change first and creates them after, in walk
// order: 4465 J2 and 4469's recorded run); nothing is freed for a unit the DLL did not re-create.
#include "srv_units_seed.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

namespace srvunits {

namespace {

const uint32_t kNone = 0xFFFFFFFFu;

uint64_t KeyOf(UnitKey k) { return (static_cast<uint64_t>(static_cast<uint8_t>(k.owner)) << 32) | static_cast<uint32_t>(k.id); }

// The S4 caps of 12.7.3: a combat's simulation is held at most 12 s, 20 s with a projectile. Used only to count
// ambiguity when no hold table was set (never as a modelled length).
const double kUnmodelledWindowSec = 20.0;

}  // namespace

struct SeedAllocator::Impl
{
	ArtChangesFn artChanges;
	LogFn log;

	// The array (12.1): per index the free-list link (free records) or the live-list links (live records).
	uint32_t size = 0;
	std::vector<uint32_t> freeNext;     // free list link, valid while the index is free
	std::vector<uint32_t> liveNext, livePrev;
	std::vector<uint8_t> inUse;
	std::vector<UnitKey> keyAt;
	std::vector<int16_t> eraAt;         // the owner's era when the record was created (the EXE's stored art value, S3)
	uint32_t freeHead = kNone;
	uint32_t liveHead = kNone, liveTail = kNone;
	std::unordered_map<uint64_t, uint32_t> map;   // key -> index (the EXE's key map, +0xE8)

	// Player eras as the allocator last saw them (kind 33, else the unit record's era at its creation).
	int32_t playerEra[256];

	// Load window: from kind 32 to the first creation (the DLL's CvPlayer::reset removals find no key, S5).
	bool inLoad = false;

	// Era window (kind 33): open from the era change to the first event that is neither a re-creation of the era
	// player's unit nor one of the setup calls that follow it (issue 4469: no art test, no pending list).
	bool eraOpen = false;
	int eraPlayer = -1;
	int eraNew = -1;

	// Decision 14 (a).
	bool haveHold = false;
	HoldParams hold{};
	struct Exposure { double until; uint32_t seq; double band; };
	std::unordered_map<uint64_t, Exposure> exposed;   // participants of a held combat: removal deferred until `until`
	struct Held { double release; uint32_t seq; UnitKey key; double band; };
	std::vector<Held> held;                           // deferred removals, applied at Advance
	std::vector<Held> released;                       // held removals applied within the last band (ambiguity both sides)
	struct Unmodelled { double until; uint32_t seq; };
	std::unordered_map<uint64_t, Unmodelled> unmodelledExposed;  // no hold table: participants (for the ambiguity count)
	std::vector<Unmodelled> unmodelledRemovals;       // removals applied in feed order that a hold may have deferred

	// The EXE's frame order (12.7.6 [L]): inside one frame the EXE dispatches a creation after the removals of other
	// units posted in the same frame; records of one unit keep their order. A group = the events applied with one tSec
	// (one drain); creations, and every later event of a unit with a creation pending, wait until the group ends.
	bool grouping = true;
	double groupT = -1e300;
	// Issue 4462: what a deferred entry does at the group's end, and in which pass (12.7.9, 12.7.10 [L]): pass 1 the
	// creations of units alone on their plot in the group (and the events of those units queued behind them), pass 2
	// the late creations (after another unit's event on the plot; an Embark creation of a key with no record), pass 3
	// the removals of units that died in combat.
	enum What : uint8_t { kDefCreate = 0, kDefRemove = 1, kDefCombatRemove = 2 };
	struct Deferred { What what; uint8_t pass; UnitKey key; int16_t era; SrvFeed::Event e; double t; };
	std::vector<Deferred> deferred;
	std::unordered_map<uint64_t, uint8_t> pendingCreates;   // key -> the pass of its latest creation pending in the group
	// Issue 4462: per plot, the units with an event on it in the group (kinds 11-13, 16, 17, 21-24 with a plot), in
	// order of their first event (12.7.9's "an event of another unit on the same plot").
	std::unordered_map<int32_t, std::vector<uint64_t>> plotKeys;
	// Issue 4462: the keys whose kind 15 (death in combat) the feed has carried and whose removal is not applied yet.
	std::unordered_set<uint64_t> diedInCombat;
	// Issue 4462: this group holds a load's first creation (the save's SetupGraphical): its creations stay in feed order
	// (S2 [L] by 4453, and 12.7.7's reload with an Embark(true) unit equal in feed order [L] by 4458), rule (e) not applied.
	bool loadSetupGroup = false;

	uint32_t lastSeq = 0;
	bool outOfStep = false;
	SeedCounters c{};

	Impl()
	{
		for(int i = 0; i < 256; ++i)
			playerEra[i] = -1;
	}

	void Report(SeedStatus s, const SrvFeed::Event& e, const char* detail)
	{
		switch(s)
		{
		case SeedStatus::kUnknownRemoval: ++c.unknownRemoval; break;
		case SeedStatus::kKnownCreation: ++c.knownCreation; break;
		case SeedStatus::kCombatHoldAmbiguous: ++c.combatHoldAmbiguous; break;
		case SeedStatus::kOutOfStep: ++c.outOfStepEpisodes; outOfStep = true; break;
		default: break;
		}
		if(log)
			log(s, e, detail);
	}

	// ---- the table (12.1, 12.7.2) ----
	uint32_t Alloc(UnitKey key, int16_t era)
	{
		uint32_t i;
		if(freeHead == kNone)
		{
			i = size++;
			freeNext.push_back(kNone);
			liveNext.push_back(kNone);
			livePrev.push_back(kNone);
			inUse.push_back(0);
			keyAt.push_back(UnitKey{0, 0});
			eraAt.push_back(-1);
		}
		else
		{
			i = freeHead;
			freeHead = freeNext[i];
		}
		freeNext[i] = kNone;
		inUse[i] = 1;
		keyAt[i] = key;
		eraAt[i] = era;
		// appended to the live list (creation order)
		livePrev[i] = liveTail;
		liveNext[i] = kNone;
		if(liveTail != kNone)
			liveNext[liveTail] = i;
		else
			liveHead = i;
		liveTail = i;
		map[KeyOf(key)] = i;   // the creation handler maps the key without looking it up first (12.7.1)
		++c.created;
		return i;
	}

	void FreeIndex(uint32_t i)
	{
		// unlink from the live list
		if(livePrev[i] != kNone) liveNext[livePrev[i]] = liveNext[i]; else liveHead = liveNext[i];
		if(liveNext[i] != kNone) livePrev[liveNext[i]] = livePrev[i]; else liveTail = livePrev[i];
		liveNext[i] = livePrev[i] = kNone;
		inUse[i] = 0;
		// 12.7.2: below the head (or an empty list) -> the new head; else directly behind the head
		if(freeHead == kNone || i < freeHead)
		{
			freeNext[i] = freeHead;
			freeHead = i;
		}
		else
		{
			freeNext[i] = freeNext[freeHead];
			freeNext[freeHead] = i;
		}
		++c.freed;
	}

	// The removal handler (12.7.1): an unknown key does nothing; a known one is freed and erased from the map.
	bool FreeKey(UnitKey key)
	{
		auto it = map.find(KeyOf(key));
		if(it == map.end())
			return false;
		const uint32_t i = it->second;
		map.erase(it);
		FreeIndex(i);
		return true;
	}

	// S5: every live record freed from the last created back to the first, size kept, the key map emptied.
	void LoadClear()
	{
		while(liveTail != kNone)
			FreeIndex(liveTail);
		map.clear();
		++c.loadClears;
	}

	int16_t EraFor(UnitKey key, const std::vector<SrvFeed::UnitRecord>& table) const
	{
		const int o = static_cast<uint8_t>(key.owner);
		if(playerEra[o] >= 0)
			return static_cast<int16_t>(playerEra[o]);
		for(const auto& r : table)
			if(r.owner == key.owner && r.id == key.id)
				return r.era;
		return -1;
	}

	// ---- decision 14 (a) ----
	void ApplyHeldUpTo(double tSec)
	{
		if(held.empty())
			return;
		std::stable_sort(held.begin(), held.end(), [](const Held& a, const Held& b) {
			return a.release < b.release || (a.release == b.release && a.seq < b.seq);
		});
		size_t n = 0;
		while(n < held.size() && held[n].release <= tSec)
		{
			FreeKey(held[n].key);
			released.push_back(held[n]);
			++n;
		}
		held.erase(held.begin(), held.begin() + static_cast<std::ptrdiff_t>(n));
	}

	void ExpireExposure(double tSec)
	{
		for(auto it = exposed.begin(); it != exposed.end();)
			it = (it->second.until <= tSec) ? exposed.erase(it) : std::next(it);
		for(auto it = unmodelledExposed.begin(); it != unmodelledExposed.end();)
			it = (it->second.until <= tSec) ? unmodelledExposed.erase(it) : std::next(it);
		released.erase(std::remove_if(released.begin(), released.end(),
		                              [tSec](const Held& h) { return h.release + h.band < tSec; }),
		               released.end());
		unmodelledRemovals.erase(std::remove_if(unmodelledRemovals.begin(), unmodelledRemovals.end(),
		                                        [tSec](const Unmodelled& u) { return u.until <= tSec; }),
		                         unmodelledRemovals.end());
	}

	void CheckAmbiguity(const SrvFeed::Event& e, double tSec)
	{
		char buf[160];
		for(const auto* list : {&held, &released})
			for(const auto& h : *list)
				if(tSec >= h.release - h.band && tSec <= h.release + h.band)
				{
					std::snprintf(buf, sizeof buf, "creation at %.3f s within %.3f s of the held removal of (%d,%d) ending %.3f s",
					              tSec, h.band, h.key.owner, h.key.id, h.release);
					Report(SeedStatus::kCombatHoldAmbiguous, e, buf);
					return;
				}
		if(!haveHold && !unmodelledRemovals.empty())
		{
			std::snprintf(buf, sizeof buf, "creation at %.3f s behind a removal of a visible human-turn combat (seq %u); "
			              "no hold table: applied in feed order", tSec, unmodelledRemovals.front().seq);
			Report(SeedStatus::kCombatHoldAmbiguous, e, buf);
		}
	}

	// ---- era window (S3) ----
	// Issue 4469: closing frees nothing. Every unit the EXE removes at an era change is re-created by the DLL's
	// SetupGraphical in the same call (12.7.3 S3), so the feed's re-creations are the whole set; 4462's art test (the
	// pending list, eraFreedOnly) left out re-creations whose land art row does not change: 4466's great war infantry and
	// every embarked unit, which the EXE re-creates at every era change (4469 [L], unit-formations.md 12.7.11).
	void CloseEra() { eraOpen = false; }

	// A creation, deferred to the group's end in pass `pass` (1 or 2; never earlier than the unit's own pending events).
	void Create(const SrvFeed::Event& e, double tSec, UnitKey key, const std::vector<SrvFeed::UnitRecord>& table, uint8_t pass = 1)
	{
		inLoad = false;
		if(grouping)
		{
			auto it = pendingCreates.find(KeyOf(key));
			if(it != pendingCreates.end() && it->second > pass)
				pass = it->second;
			deferred.push_back(Deferred{kDefCreate, pass, key, EraFor(key, table), e, tSec});
			pendingCreates[KeyOf(key)] = pass;
			++c.deferredCreates;
			if(pass == 2)
				++c.lateCreates;
			return;
		}
		CheckAmbiguity(e, tSec);
		Alloc(key, EraFor(key, table));
	}

	// Issue 4462, rules (a) and (b): a removal and a creation applied at once (12.7.7 (2), (3) [L]): the creation takes
	// the free list's head after the free, so a unit freed at or below the head gets its own index back. A unit with a
	// creation pending in the group keeps its order behind it instead.
	void RecreateNow(const SrvFeed::Event& e, double tSec, UnitKey key, const std::vector<SrvFeed::UnitRecord>& table)
	{
		inLoad = false;
		if(grouping && Pending(key))
		{
			Remove(e, tSec, key);
			Create(e, tSec, key, table);
			return;
		}
		FreeKey(key);
		CheckAmbiguity(e, tSec);
		Alloc(key, EraFor(key, table));
	}

	bool Pending(UnitKey key) const { return pendingCreates.count(KeyOf(key)) != 0; }

	// A removal of `key` now, or behind its own creation pending in this group (the records of one unit keep order).
	void Remove(const SrvFeed::Event& e, double tSec, UnitKey key)
	{
		if(grouping && Pending(key))
		{
			deferred.push_back(Deferred{kDefRemove, pendingCreates[KeyOf(key)], key, -1, e, tSec});
			return;
		}
		FreeKey(key);
	}

	// Issue 4462, rule (d) (12.7.10 [L]): the removal of a unit that died in combat comes after the group's creations
	// (the EXE frees it 0.11-0.30 s after the DLL's removal, behind its combat, also in AI turns and for a combat not
	// shown). Grouping off: at once.
	void CombatRemove(const SrvFeed::Event& e, double tSec, UnitKey key)
	{
		diedInCombat.erase(KeyOf(key));
		if(grouping && !Pending(key))
		{
			deferred.push_back(Deferred{kDefCombatRemove, 3, key, -1, e, tSec});
			return;
		}
		Remove(e, tSec, key);
	}

	// Issue 4462, rule (e) (12.7.9): note the unit's event on its plot; true when another unit had an event on the
	// same plot earlier in the group.
	bool NotePlot(const SrvFeed::Event& e, UnitKey key)
	{
		using namespace SrvFeed;
		const uint16_t k = e.kind;
		const bool onPlot = k == kUnitCreated || k == kUnitMoved || k == kUnitTeleported || k == kUnitSetDamage ||
		                    k == kUnitFortify || k == kUnitEmbark || k == kUnitGarrison || k == kUnitShouldDimFlag ||
		                    k == kUnitVisibility;
		if(!grouping || !onPlot || e.plot < 0)
			return false;
		std::vector<uint64_t>& keys = plotKeys[e.plot];
		const uint64_t me = KeyOf(key);
		bool other = false;
		bool mine = false;
		for(uint64_t x : keys)
		{
			if(x == me) mine = true;
			else other = true;
		}
		if(!mine)
			keys.push_back(me);
		return other;
	}

	// The group's end: pass 1 (creations and the events of their units queued behind them), pass 2 (the late
	// creations and theirs), each in feed order; then the combat deaths' removals in feed order (issue 4462).
	void Flush()
	{
		std::vector<Deferred> d;
		d.swap(deferred);
		pendingCreates.clear();
		plotKeys.clear();
		loadSetupGroup = false;
		for(uint8_t pass = 1; pass <= 3; ++pass)
			for(const auto& x : d)
			{
				if(x.pass != pass)
					continue;
				if(x.what == kDefCreate)
				{
					CheckAmbiguity(x.e, x.t);
					Alloc(x.key, x.era);
				}
				else if(FreeKey(x.key) && x.what == kDefCombatRemove)
					++c.combatLateFrees;
			}
	}

	// Called at the start of every Apply / NoteCombat: a later tSec ends the group; then the held removals due.
	void Step(double tSec)
	{
		if(tSec > groupT)
		{
			Flush();
			groupT = tSec;
		}
		ApplyHeldUpTo(tSec);
		ExpireExposure(tSec);
	}
};

SeedAllocator::SeedAllocator(ArtChangesFn artChanges, LogFn log) : m_impl(new Impl)
{
	m_impl->artChanges = std::move(artChanges);
	m_impl->log = std::move(log);
}

SeedAllocator::~SeedAllocator() { delete m_impl; }

void SeedAllocator::SetHoldParams(const HoldParams& p)
{
	m_impl->hold = p;
	m_impl->haveHold = true;
}

void SeedAllocator::Advance(double tSec)
{
	m_impl->Flush();
	if(tSec > m_impl->groupT)
		m_impl->groupT = tSec;
	m_impl->ApplyHeldUpTo(tSec);
	m_impl->ExpireExposure(tSec);
}

void SeedAllocator::SetFrameGrouping(bool on)
{
	m_impl->Flush();
	m_impl->grouping = on;
}

size_t SeedAllocator::PendingCreations() const { return m_impl->deferred.size(); }

void SeedAllocator::NoteDropped(uint64_t count)
{
	SrvFeed::Event e{};
	e.kind = SrvFeed::kOverflow;
	e.a[0] = static_cast<int32_t>(count > 0x7FFFFFFF ? 0x7FFFFFFF : count);
	char buf[96];
	std::snprintf(buf, sizeof buf, "%llu feed events dropped: the allocator cannot follow the EXE's table",
	              static_cast<unsigned long long>(count));
	m_impl->Report(SeedStatus::kOutOfStep, e, buf);
}

void SeedAllocator::NoteCombat(const SrvFeed::Event& e, double tSec, const CombatNote& cn)
{
	Impl& m = *m_impl;
	m.Step(tSec);
	if(e.seq != 0)
	{
		if(e.seq <= m.lastSeq)
		{
			m.Report(SeedStatus::kOutOfStep, e, "combat event seq not above the last one applied");
			return;
		}
		m.lastSeq = e.seq;
	}
	if(m.eraOpen)
		m.CloseEra();
	// 12.7.3 S4.1: only a visible combat in the human's turn with quick combat off holds a removal.
	if(!cn.visualize || !cn.humanTurn || cn.quickCombat)
		return;
	const UnitKey sides[2] = {cn.attacker, cn.defender};
	for(const auto& k : sides)
	{
		if(k.owner < 0 || k.id < 0)
			continue;   // a city side holds no unit record
		if(m.haveHold)
		{
			const int cls = static_cast<int>(cn.cls) < static_cast<int>(HoldClass::kCount) ? static_cast<int>(cn.cls) : 0;
			m.exposed[KeyOf(k)] = Impl::Exposure{tSec + m.hold.lengthSec[cls], e.seq, m.hold.bandSec[cls]};
		}
		else
			m.unmodelledExposed[KeyOf(k)] = Impl::Unmodelled{tSec + kUnmodelledWindowSec, e.seq};
	}
}

void SeedAllocator::Apply(const SrvFeed::Event& e, double tSec, const std::vector<SrvFeed::UnitRecord>& table)
{
	Impl& m = *m_impl;
	using namespace SrvFeed;
	m.Step(tSec);
	const bool unitKind = e.kind >= kUnitCreated && e.kind <= kUnitResetAnimationState;
	if(!unitKind && e.kind != kLoadBegin && e.kind != kEraChanged)
		return;   // plot, city and UI kinds do not touch the table
	if(e.kind == kUnitCombat || e.kind == kCityCombat)
		return;   // NoteCombat's
	if(e.seq <= m.lastSeq && e.seq != 0)
	{
		char buf[96];
		std::snprintf(buf, sizeof buf, "seq %u not above the last one applied (%u)", e.seq, m.lastSeq);
		m.Report(SeedStatus::kOutOfStep, e, buf);
		return;
	}
	if(e.seq != 0)
		m.lastSeq = e.seq;

	if(e.kind == kLoadBegin)
	{
		m.Flush();
		m.CloseEra();
		m.held.clear();               // the EXE's reset empties its queues and frees every live record (S5)
		m.diedInCombat.clear();
		m.released.clear();
		m.exposed.clear();
		m.unmodelledExposed.clear();
		m.unmodelledRemovals.clear();
		for(int i = 0; i < 256; ++i)
			m.playerEra[i] = -1;
		m.LoadClear();
		m.inLoad = true;
		return;
	}
	if(e.kind == kEraChanged)
	{
		m.CloseEra();
		const int player = e.a[0], era = e.a[1];
		if(player < 0 || player > 255)
		{
			m.Report(SeedStatus::kOutOfStep, e, "era change for a player outside 0..255");
			return;
		}
		m.eraOpen = true;
		m.eraPlayer = player;
		m.eraNew = era;
		// The EXE's walk (12.7.3 S3) calls the DLL back for each unit it removes; the DLL's Created / Embark(true) of
		// each comes next in the feed, in walk order (issue 4469: the window takes them as they come, no art test).
		m.playerEra[player] = era;
		return;
	}

	const UnitKey key{static_cast<int8_t>(e.a[0]), e.a[1]};
	if(e.a[0] < 0 || e.a[0] > 63 || e.a[1] < 0)
	{
		m.Report(SeedStatus::kOutOfStep, e, "unit event with an owner outside 0..63 or a negative id");
		return;
	}
	const bool known = m.map.count(KeyOf(key)) != 0 || m.Pending(key);
	if(m.inLoad && (e.kind == kUnitCreated || e.kind == kUnitEmbark))
		m.loadSetupGroup = true;   // the load's creations: feed order (S2)
	const bool otherOnPlot = m.NotePlot(e, key) && !m.loadSetupGroup;   // issue 4462, rule (e)

	// The era window stays open across its re-creations and the setup calls that follow each (12.7.3 S2 list).
	if(m.eraOpen)
	{
		const bool setupKind = e.kind == kUnitShouldDimFlag || e.kind == kUnitFortify || e.kind == kUnitSetDamage ||
		                       e.kind == kUnitWork || e.kind == kUnitVisibility || e.kind == kUnitGarrison;
		const bool eraUnit = static_cast<uint8_t>(key.owner) == m.eraPlayer && known;
		if(eraUnit && e.kind == kUnitCreated)
		{
			// issue 4462, rule (a) (12.7.7 (2) [L]): a land re-creation is the removal and the creation at once (the
			// creation takes back the index just freed when it is at or below the free list's head); not grouped.
			// Issue 4469: whatever the unit's art (4466's KnownCreation (4, 15542) [L]).
			++m.c.eraRecreated;
			m.RecreateNow(e, tSec, key, table);
			return;
		}
		if(eraUnit && e.kind == kUnitEmbark)
			++m.c.eraRecreated;   // issue 4469 [L]: the embark swap below, grouped (all removals, then the creations)
		else if(!setupKind)
			m.CloseEra();
	}

	switch(e.kind)
	{
	case kUnitCreated:
		if(known)
			m.Report(SeedStatus::kKnownCreation, e, "creation of a held key outside an era change: a second record (the EXE's)");
		// issue 4462, rule (e) (12.7.9 [L]): a creation after another unit's event on its plot in the group is late
		m.Create(e, tSec, key, table, otherOnPlot ? 2 : 1);
		break;
	case kUnitEmbark:
		// 12.7.3 S3: the EXE posts a removal then a creation in both directions; for an unknown key the removal is a no-op.
		if(known)
		{
			m.Remove(e, tSec, key);
			++m.c.embarkSwaps;
			m.Create(e, tSec, key, table);
		}
		else   // issue 4462, rule (e): the creation of a key with no record is late (12.7.9), except in a load's setup (S2)
			m.Create(e, tSec, key, table, m.loadSetupGroup ? 1 : 2);
		break;
	case kUnitDestroyed:
	{
		if(!known)
		{
			m.diedInCombat.erase(KeyOf(key));
			if(m.inLoad)
				++m.c.loadRemovals;
			else
			{
				char buf[64];
				std::snprintf(buf, sizeof buf, "removal of unknown key (%d,%d)", key.owner, key.id);
				m.Report(SeedStatus::kUnknownRemoval, e, buf);
			}
			break;
		}
		auto ex = m.exposed.find(KeyOf(key));
		if(ex != m.exposed.end() && ex->second.until > tSec)
		{
			for(const auto& h : m.held)
				if(h.key == key)
				{
					m.Report(SeedStatus::kOutOfStep, e, "a second removal of a key whose removal is held");
					return;
				}
			m.held.push_back(Impl::Held{ex->second.until, e.seq, key, ex->second.band});
			m.diedInCombat.erase(KeyOf(key));
			++m.c.heldRemovals;
			break;
		}
		auto ux = m.unmodelledExposed.find(KeyOf(key));
		if(ux != m.unmodelledExposed.end() && ux->second.until > tSec)
			m.unmodelledRemovals.push_back(ux->second);
		if(m.diedInCombat.count(KeyOf(key)))
			m.CombatRemove(e, tSec, key);   // issue 4462, rule (d): after the group's creations
		else
			m.Remove(e, tSec, key);
		break;
	}
	case kUnitDestroyedInCombat:
		// S1: frees nothing; issue 4462, rule (d): the unit's removal (kind 14) will come after its group's creations
		m.diedInCombat.insert(KeyOf(key));
		break;
	case kUnitParadrop:
		// Issue 4462, rule (b) (12.7.7 (3) [L], which corrects 12.7.3 S3's "a removal and no creation"): the EXE removes
		// the unit's record and creates a new one at once (the new one from the free list's head). A key with no record
		// outside a load: the removal is the EXE's no-op (counted kUnknownRemoval), the creation gives it a record [I].
		if(!known && !m.inLoad)
		{
			char buf[80];
			std::snprintf(buf, sizeof buf, "paradrop of unknown key (%d,%d): created", key.owner, key.id);
			m.Report(SeedStatus::kUnknownRemoval, e, buf);
		}
		m.RecreateNow(e, tSec, key, table);
		if(known)
			++m.c.paradropFrees;
		break;
	default:
		break;   // moves, teleports, rebases, damage, flags: no record change
	}
}

bool SeedAllocator::SeedOf(UnitKey key, uint32_t& seed) const
{
	auto it = m_impl->map.find(KeyOf(key));
	if(it == m_impl->map.end())
		return false;
	seed = it->second;
	return true;
}

bool SeedAllocator::OutOfStep() const { return m_impl->outOfStep; }
SeedCounters SeedAllocator::Counters() const { return m_impl->c; }
uint32_t SeedAllocator::ArraySize() const { return m_impl->size; }
size_t SeedAllocator::PendingHeld() const { return m_impl->held.size(); }

void SeedAllocator::LiveRecords(std::vector<SeedRecord>& out) const
{
	out.clear();
	const Impl& m = *m_impl;
	for(uint32_t i = m.liveHead; i != kNone; i = m.liveNext[i])
	{
		auto it = m.map.find(KeyOf(m.keyAt[i]));
		out.push_back(SeedRecord{i, m.keyAt[i], it != m.map.end() && it->second == i});
	}
}

bool SeedAllocator::RestoreForTest(uint32_t size, const std::vector<SeedRecord>& live, const std::vector<uint32_t>& freeList)
{
	Impl& m = *m_impl;
	if(live.size() + freeList.size() != size)
		return false;
	std::vector<uint8_t> seen(size, 0);
	for(const auto& r : live)
		if(r.index >= size || seen[r.index]++) return false;
	for(uint32_t i : freeList)
		if(i >= size || seen[i]++) return false;
	m.deferred.clear();
	m.pendingCreates.clear();
	m.plotKeys.clear();
	m.held.clear();
	m.map.clear();
	m.size = size;
	m.freeNext.assign(size, kNone);
	m.liveNext.assign(size, kNone);
	m.livePrev.assign(size, kNone);
	m.inUse.assign(size, 0);
	m.keyAt.assign(size, UnitKey{0, 0});
	m.eraAt.assign(size, -1);
	m.liveHead = m.liveTail = kNone;
	for(const auto& r : live)
	{
		const uint32_t i = r.index;
		m.inUse[i] = 1;
		m.keyAt[i] = r.key;
		m.livePrev[i] = m.liveTail;
		if(m.liveTail != kNone) m.liveNext[m.liveTail] = i; else m.liveHead = i;
		m.liveTail = i;
		m.map[KeyOf(r.key)] = i;
	}
	m.freeHead = freeList.empty() ? kNone : freeList[0];
	for(size_t k = 0; k + 1 < freeList.size(); ++k)
		m.freeNext[freeList[k]] = freeList[k + 1];
	return true;
}

void SeedAllocator::FreeList(std::vector<uint32_t>& out) const
{
	out.clear();
	for(uint32_t i = m_impl->freeHead; i != kNone; i = m_impl->freeNext[i])
		out.push_back(i);
}

}  // namespace srvunits
