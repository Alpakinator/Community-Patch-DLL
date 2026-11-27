// test_units_seed.cpp - unit tests of the formation-seed allocator (server/src/units/srv_units_seed.cpp; issue 4453,
// epic 0375, 2026-11-15). Built and run standalone by run_units_seed_test.py (plain clang-cl and MSVC ASan).
//
// Rules under test: research/05-graphics-composition/unit-formations.md 12.1, 12.7.1-12.7.3, 12.7.6;
// unit-server-scene.md sections 4 and 10. Every status of SeedStatus has a negative test.
//
// Second use (the offline comparison of 12.7.6): test_units_seed.exe --replay FILE prints the allocator's table at
// each report line of FILE. Format (one record per line, '#' comments, numbers only):
//   H <melee s> <ranged s> <band s> [<ranged band s>]       hold table (SetHoldParams); one band: both classes
//   E <seq> <t> <kind> <owner> <id> <a2> <a3> [<plot>]     a feed event (kind 14: removal; 11 creation; 32 load ...);
//                                                          the plot (default -1) for issue 4462's rule (e)
//   C <seq> <t> <kind> <attOwner> <attId> <defOwner> <defId> <visualize> <humanTurn> <quick> <cls>   a combat
//   A <owner> <id>                                         this unit's art changes at the next kind 33
//   D <count>                                              dropped events
//   V <t>                                                  Advance(t)
//   Z <size> <n> <i o id>... <m> <i>...                     the state from a reader table (RestoreForTest)
//   R <name>                                               Advance to the last time, print "R name size", "L index owner id mapped" per live
//                                                          record (creation order) and "F i i i" (free list)
//   X <tag> <n> <owner id index>...                        issue 4462: the reader's table at a check: Advance to the last
//                                                          time, compare every unit's seed, print "X tag units n equal m
//                                                          diffs [owner id reader allocator]..." (-1: no seed); at the end
//                                                          "X total checks c units N equal M"
// Exit codes: 0 all checks passed / replay done (every X unit equal), 1 a check failed (or an X unit differed), 2 replay
// input refused (line named).
#include "../../src/units/srv_units_seed.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace srvunits;
using SrvFeed::Event;
using SrvFeed::UnitRecord;

static int g_failed = 0, g_passed = 0;
static const char* g_test = "";

#define CHECK(c)                                                                                  \
	do                                                                                            \
	{                                                                                             \
		if(c) ++g_passed;                                                                         \
		else { ++g_failed; std::printf("FAIL %s: %s (line %d)\n", g_test, #c, __LINE__); }        \
	} while(0)

// ---- a small harness: a feed of events in seq order and the unit table after each one ----
struct Rig
{
	std::vector<UnitRecord> table;
	std::set<std::pair<int, int>> artChanging;
	std::vector<SeedStatus> statuses;
	std::vector<std::string> details;
	uint32_t seq = 0;
	bool batch = false;   // true: events stay in one group until the test calls alloc.Advance
	SeedAllocator alloc;

	Rig()
	    : alloc(
	          [this](const UnitRecord& u, int, int) { return artChanging.count(std::make_pair((int)u.owner, (int)u.id)) != 0; },
	          [this](SeedStatus s, const Event&, const char* d) { statuses.push_back(s); details.push_back(d ? d : ""); })
	{
	}

	Event Ev(uint16_t kind, int owner, int id, int a2 = 0, int a3 = 0)
	{
		Event e{};
		e.seq = ++seq;
		e.kind = kind;
		e.plot = -1;
		e.turn = 1;
		e.a[0] = owner;
		e.a[1] = id;
		e.a[2] = a2;
		e.a[3] = a3;
		return e;
	}
	// an event on a plot (issue 4462's rule (e) reads the plot); the unit table follows creations
	void OnPlot(uint16_t kind, int owner, int id, int plot, double t, int a2 = 0)
	{
		Event e = Ev(kind, owner, id, a2);
		e.plot = plot;
		if(kind == SrvFeed::kUnitCreated || kind == SrvFeed::kUnitEmbark)
			Upsert(owner, id);
		Raw(e, t);
	}
	void Raw(const Event& e, double t)
	{
		alloc.Apply(e, t, table);
		if(!batch)
			alloc.Advance(t);
	}
	void Upsert(int owner, int id, int era = 0)
	{
		for(auto& r : table)
			if(r.owner == owner && r.id == id) { r.era = (int16_t)era; return; }
		UnitRecord r{};
		r.owner = (int8_t)owner;
		r.id = id;
		r.era = (int16_t)era;
		table.push_back(r);
	}
	void Erase(int owner, int id)
	{
		for(size_t i = 0; i < table.size(); ++i)
			if(table[i].owner == owner && table[i].id == id) { table.erase(table.begin() + (long)i); return; }
	}
	void Create(int owner, int id, double t = 0, int era = 0)
	{
		Upsert(owner, id, era);
		Raw(Ev(SrvFeed::kUnitCreated, owner, id), t);
	}
	void Destroy(int owner, int id, double t = 0)
	{
		Erase(owner, id);
		Raw(Ev(SrvFeed::kUnitDestroyed, owner, id), t);
	}
	void Embark(int owner, int id, int on, double t = 0)
	{
		Upsert(owner, id);
		Raw(Ev(SrvFeed::kUnitEmbark, owner, id, on), t);
	}
	void Load(double t = 0)
	{
		table.clear();
		Event e{};
		e.seq = ++seq;
		e.kind = SrvFeed::kLoadBegin;
		Raw(e, t);
	}
	void Era(int player, int era, double t = 0)
	{
		Event e{};
		e.seq = ++seq;
		e.kind = SrvFeed::kEraChanged;
		e.a[0] = player;
		e.a[1] = era;
		Raw(e, t);
	}
	void Combat(int ao, int ai, int downer, int did, bool vis, bool human, bool quick, HoldClass cls, double t)
	{
		CombatNote c{};
		c.attacker = UnitKey{(int8_t)ao, ai};
		c.defender = UnitKey{(int8_t)downer, did};
		c.visualize = vis;
		c.humanTurn = human;
		c.quickCombat = quick;
		c.cls = cls;
		alloc.NoteCombat(Ev(SrvFeed::kUnitCombat, ao, ai), t, c);
	}
	int64_t Seed(int owner, int id) const
	{
		uint32_t s = 0;
		return alloc.SeedOf(UnitKey{(int8_t)owner, id}, s) ? (int64_t)s : -1;
	}
	std::vector<uint32_t> Free() const
	{
		std::vector<uint32_t> f;
		alloc.FreeList(f);
		return f;
	}
	size_t Count(SeedStatus s) const
	{
		size_t n = 0;
		for(auto x : statuses) n += (x == s);
		return n;
	}
};

static std::vector<uint32_t> V(std::initializer_list<uint32_t> l) { return std::vector<uint32_t>(l); }

// 12.7.2's example: free 20, 21, ..., 31 in that order from an empty list gives 20, 31, 30, ..., 21.
static void TestFreeRule()
{
	g_test = "free rule (12.7.2)";
	Rig r;
	for(int i = 0; i < 32; ++i) r.Create(0, 1000 + i);
	CHECK(r.alloc.ArraySize() == 32);
	CHECK(r.Free().empty());
	for(int i = 20; i < 32; ++i) r.Destroy(0, 1000 + i);
	CHECK(r.Free() == V({20, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21}));
	// below the head: becomes the head
	r.Destroy(0, 1005);
	CHECK(r.Free().front() == 5 && r.Free()[1] == 20);
	// the next creation takes min(head, freed)
	r.Create(1, 1);
	CHECK(r.Seed(1, 1) == 5);
	r.Create(1, 2);
	CHECK(r.Seed(1, 2) == 20);
	r.Create(1, 3);
	CHECK(r.Seed(1, 3) == 31);
	CHECK(r.alloc.ArraySize() == 32);   // no append while the free list holds indices
	CHECK(r.statuses.empty());
}

// Append when the free list is empty; the live list is kept in creation order.
static void TestAppendAndLiveList()
{
	g_test = "append, live list";
	Rig r;
	r.Create(0, 1);
	r.Create(1, 2);
	r.Create(63, 3);
	CHECK(r.Seed(0, 1) == 0 && r.Seed(1, 2) == 1 && r.Seed(63, 3) == 2);
	r.Destroy(1, 2);
	r.Create(5, 9);   // takes 1
	std::vector<SeedRecord> live;
	r.alloc.LiveRecords(live);
	CHECK(live.size() == 3);
	CHECK(live[0].index == 0 && live[1].index == 2 && live[2].index == 1);
	CHECK(live[2].key == (UnitKey{5, 9}) && live[2].mapped);
	SeedCounters c = r.alloc.Counters();
	CHECK(c.created == 4 && c.freed == 1);
	CHECK(r.Seed(1, 2) == -1);
}

// 3907's catalogue as regression (12.1 / 12.7.2): 20 records at the load, then frames L01-L16 of 12 objects each,
// cleared by the runner in object order 1-12 (clear.lua). Odd frames: seed = object + 19; even frames: object 1 gets
// 20, object k >= 2 gets 33 - k.
static void TestCatalogue()
{
	g_test = "catalogue L01-L16 (3907)";
	Rig r;
	r.Load();
	for(int i = 0; i < 20; ++i) r.Create(i < 10 ? 0 : 1, 100 + i);
	for(int i = 0; i < 20; ++i) CHECK(r.Seed(i < 10 ? 0 : 1, 100 + i) == i);
	int nextId = 1000;
	bool allOk = true;
	for(int frame = 1; frame <= 16; ++frame)
	{
		std::vector<int> ids;
		for(int obj = 1; obj <= 12; ++obj)
		{
			const int owner = (obj % 3 == 0) ? 1 : 0;   // the catalogue mixes owners; the table is one for all
			r.Create(owner, nextId);
			ids.push_back(nextId++);
		}
		for(int obj = 1; obj <= 12; ++obj)
		{
			const int owner = (obj % 3 == 0) ? 1 : 0;
			const int64_t want = (frame % 2 == 1) ? obj + 19 : (obj == 1 ? 20 : 33 - obj);
			if(r.Seed(owner, ids[obj - 1]) != want)
			{
				allOk = false;
				std::printf("  frame L%02d object %d: seed %lld, want %lld\n", frame, obj,
				            (long long)r.Seed(owner, ids[obj - 1]), (long long)want);
			}
		}
		for(int obj = 1; obj <= 12; ++obj) r.Destroy((obj % 3 == 0) ? 1 : 0, ids[obj - 1]);
	}
	CHECK(allOk);
	CHECK(r.alloc.ArraySize() == 32);
	CHECK(r.statuses.empty());
}

// S5: kind 32 frees every live record from the last created back to the first by the free rule; the size is kept.
static void TestLoadClear()
{
	g_test = "load clear (S5)";
	{
		Rig r;
		for(int i = 0; i < 5; ++i) r.Create(0, i + 1);
		r.Destroy(0, 2);   // index 1 free
		r.Load();
		CHECK(r.alloc.ArraySize() == 5);
		CHECK(r.Free() == V({0, 1, 2, 3, 4}));
		for(int i = 0; i < 6; ++i) r.Create(2, 50 + i);
		CHECK(r.Seed(2, 50) == 0 && r.Seed(2, 54) == 4 && r.Seed(2, 55) == 5);
		CHECK(r.alloc.Counters().loadClears == 1);
	}
	{
		// a live list not in index order: 2, 0, 1 -> clear frees 1, 0, 2 -> free list 0, 2, 1
		Rig r;
		r.Create(0, 1);   // 0
		r.Create(0, 2);   // 1
		r.Create(0, 3);   // 2
		r.Destroy(0, 1);  // free 0
		r.Destroy(0, 2);  // free 1 -> 0, 1
		r.Create(0, 4);   // 0
		r.Create(0, 5);   // 1  (live order: 2, 0, 1)
		std::vector<SeedRecord> live;
		r.alloc.LiveRecords(live);
		CHECK(live.size() == 3 && live[0].index == 2 && live[1].index == 0 && live[2].index == 1);
		r.Load();
		CHECK(r.Free() == V({0, 2, 1}));
		r.Create(0, 4);
		r.Create(0, 5);
		r.Create(0, 3);
		CHECK(r.Seed(0, 4) == 0 && r.Seed(0, 5) == 2 && r.Seed(0, 3) == 1);
	}
	{
		// the DLL's reset removals inside the load window find no key: no-op, counted as load removals, no status;
		// the same removal after the first creation is a fault
		Rig r;
		r.Create(0, 7);
		r.Create(0, 8);
		r.Load();
		r.Destroy(0, 8);
		r.Destroy(0, 7);
		CHECK(r.statuses.empty());
		CHECK(r.alloc.Counters().loadRemovals == 2);
		CHECK(r.Free() == V({0, 1}));
		r.Create(0, 7);
		CHECK(r.Seed(0, 7) == 0);
		r.Destroy(0, 99);
		CHECK(r.Count(SeedStatus::kUnknownRemoval) == 1);
	}
}

// S3: kind 21 is remove + create for a known key in both directions; for an unknown key a creation.
static void TestEmbark()
{
	g_test = "embark (S3)";
	Rig r;
	for(int i = 0; i < 5; ++i) r.Create(0, i + 1);   // 0..4
	r.Destroy(0, 2);                                   // free 1
	r.Embark(0, 4, 1);                                 // index 3 above the head 1: freed behind it, takes 1
	CHECK(r.Seed(0, 4) == 1);
	CHECK(r.Free() == V({3}));
	r.Embark(0, 4, 0);                                 // disembark: 1 below head 3: becomes head, taken again
	CHECK(r.Seed(0, 4) == 1);
	CHECK(r.Free() == V({3}));
	r.Embark(0, 5, 1);                                 // index 4 above head 3 -> 3
	CHECK(r.Seed(0, 5) == 3);
	CHECK(r.Free() == V({4}));
	// unknown key: a creation (a unit set up embarked at a load), both directions
	r.Embark(1, 77, 1);
	CHECK(r.Seed(1, 77) == 4);
	r.Embark(1, 78, 0);
	CHECK(r.Seed(1, 78) == 5);
	CHECK(r.alloc.Counters().embarkSwaps == 3);
	CHECK(r.statuses.empty());
}

// S1: kind 15 frees nothing; the free comes at kind 14. Paradrop (kind 19): remove + create (issue 4462; was a free).
static void TestDeathKinds()
{
	g_test = "kinds 14, 15, 19 (S1, S3)";
	Rig r;
	r.Create(0, 1);
	r.Create(63, 2);
	r.Raw(r.Ev(SrvFeed::kUnitDestroyedInCombat, 63, 2), 0);
	CHECK(r.Seed(63, 2) == 1);
	CHECK(r.alloc.Counters().freed == 0);
	r.Destroy(63, 2);
	CHECK(r.Seed(63, 2) == -1);
	r.Raw(r.Ev(SrvFeed::kUnitParadrop, 0, 1), 0);
	CHECK(r.Seed(0, 1) == 0);    // freed 0 (free list 1 -> 0, 1), created again at the head: 0
	CHECK(r.Free() == V({1}));
	CHECK(r.alloc.Counters().paradropFrees == 1);
	// other kinds change nothing
	r.Create(0, 3);
	const uint16_t none[] = {SrvFeed::kUnitMoved, SrvFeed::kUnitTeleported, SrvFeed::kUnitSetDamage, SrvFeed::kUnitFortify,
	                         SrvFeed::kUnitWork, SrvFeed::kUnitActivate, SrvFeed::kUnitGarrison, SrvFeed::kUnitShouldDimFlag,
	                         SrvFeed::kUnitVisibility, SrvFeed::kUnitMissionEnd, SrvFeed::kUnitRebased,
	                         SrvFeed::kUnitResetAnimationState, SrvFeed::kCitySetDamage, SrvFeed::kPlotState};
	const SeedCounters before = r.alloc.Counters();
	for(uint16_t k : none) r.Raw(r.Ev(k, 0, 3), 0);
	const SeedCounters after = r.alloc.Counters();
	CHECK(before.created == after.created && before.freed == after.freed);
	CHECK(r.statuses.empty());
}

// Upgrade (create before remove) and capture (remove then create) follow the feed's order with no special rule.
static void TestUpgradeCapture()
{
	g_test = "upgrade, capture (S3)";
	Rig r;
	for(int i = 0; i < 4; ++i) r.Create(0, i + 1);   // 0..3
	r.Destroy(0, 2);                                   // free 1
	r.Create(0, 10);                                   // upgrade of unit 3: the new unit first -> 1
	r.Destroy(0, 3);                                   // then the old one at its delayed death -> free 2
	CHECK(r.Seed(0, 10) == 1);
	CHECK(r.Free() == V({2}));
	r.Destroy(0, 1);                                   // capture: the captured unit removed (0) ...
	r.Create(1, 11);                                   // ... then the captor's new unit: the lowest freed, 0
	CHECK(r.Seed(1, 11) == 0);
	CHECK(r.statuses.empty());
}

// S3 era change: kind 33 opens the window; each re-creation the DLL sends for a held key of the era player is remove +
// create, interleaved as the EXE walks. Issue 4469 (unit-formations.md 12.7.11 [L]): whatever the art test says (the
// allocator no longer asks it); a unit the DLL does not re-create keeps its record; other players' units are untouched.
static void TestEraChange()
{
	g_test = "era change (S3, kind 33; 4469 window)";
	Rig r;
	for(int i = 0; i < 6; ++i) r.Create(i < 4 ? 0 : 1, i + 1);   // 0..5; player 0 owns ids 1-4
	r.Destroy(1, 5);                                                // free 4
	r.Destroy(0, 2);                                                // free 1 -> 1, 4
	r.artChanging = {{0, 4}, {1, 6}};                               // the art test is not asked (4469): 1 and 3 do not "change"
	r.Era(0, 1);
	// the DLL re-creates 1 and 3 (interleaved); 4 is not re-created and keeps its record
	r.Raw(r.Ev(SrvFeed::kUnitCreated, 0, 1), 0);   // free 0 -> 0,1,4 ; take 0
	r.Raw(r.Ev(SrvFeed::kUnitShouldDimFlag, 0, 1), 0);
	r.Raw(r.Ev(SrvFeed::kUnitCreated, 0, 3), 0);   // free 2 -> 1,2,4 ; take 1
	CHECK(r.Seed(0, 1) == 0);
	CHECK(r.Seed(0, 3) == 1);
	CHECK(r.Free() == V({2, 4}));
	r.Raw(r.Ev(SrvFeed::kUnitMoved, 1, 6), 0);     // closes the window: nothing freed (4462 freed unit 4 here)
	CHECK(r.Seed(0, 4) == 3);
	CHECK(r.Free() == V({2, 4}));
	CHECK(r.Seed(1, 6) == 5);                                        // another player's unit untouched
	SeedCounters c = r.alloc.Counters();
	CHECK(c.eraRecreated == 2 && c.eraFreedOnly == 0);
	CHECK(r.statuses.empty());                                       // no known-creation status inside the window
	// the window is closed now: a creation of a held key is a second record (KnownCreation), as outside any era change
	r.Raw(r.Ev(SrvFeed::kUnitCreated, 0, 4), 0);
	CHECK(r.Count(SeedStatus::kKnownCreation) == 1);
	// the art test is never called (4469): an era change with a test that would answer true still takes only re-creations
	bool asked = false;
	SeedAllocator probe([&](const UnitRecord&, int, int) { asked = true; return true; }, nullptr);
	std::vector<UnitRecord> t(1);
	t[0].owner = 2; t[0].id = 1; t[0].era = 3;
	Event e{}; e.seq = 1; e.kind = SrvFeed::kUnitCreated; e.a[0] = 2; e.a[1] = 1;
	probe.Apply(e, 0, t);
	probe.Advance(0);
	Event ec{}; ec.seq = 2; ec.kind = SrvFeed::kEraChanged; ec.a[0] = 2; ec.a[1] = 4;
	probe.Apply(ec, 0, t);
	Event er = e; er.seq = 3;
	probe.Apply(er, 0, t);
	probe.Advance(0);
	CHECK(!asked && probe.Counters().eraRecreated == 1 && probe.Counters().knownCreation == 0);
	// the interleaving matters for land re-creations: remove-all-then-create would give 0 then 4 here, at once 0 then 1
	Rig z;
	for(int i = 0; i < 5; ++i) z.Create(0, i + 1);   // 0..4
	z.Destroy(0, 2);                                   // head 1
	z.Era(0, 2);
	z.Raw(z.Ev(SrvFeed::kUnitCreated, 0, 3), 0);   // free 2 behind head: 1,2 ; take 1
	z.Raw(z.Ev(SrvFeed::kUnitCreated, 0, 5), 0);   // free 4 behind head 2: 2,4 ; take 2
	CHECK(z.Seed(0, 3) == 1 && z.Seed(0, 5) == 2);
	// an embarked unit re-created through Embark(true) inside the window: the embark swap (remove + create, the
	// creation at the group's end), counted as an era re-creation, the window stays open
	Rig y;
	y.Create(0, 1);
	y.Create(0, 2);
	y.Era(0, 1);
	y.Embark(0, 2, 1);
	CHECK(y.Seed(0, 2) == 1 && y.alloc.Counters().eraRecreated == 1 && y.alloc.Counters().embarkSwaps == 1);
	y.Raw(y.Ev(SrvFeed::kUnitCreated, 0, 1), 0);   // still inside the window: a re-creation, not a second record
	CHECK(y.Seed(0, 1) == 0 && y.alloc.Counters().eraRecreated == 2 && y.statuses.empty());
}

// Negative cases of the 4469 window: a held key of ANOTHER player, a key with no record of the era player, and a held
// key after a non-setup event closed the window are not re-creations.
static void TestEraWindowNegative()
{
	g_test = "era window negatives (4469)";
	{
		Rig r;
		r.Create(0, 1);
		r.Create(1, 2);
		r.Era(0, 3);
		r.Raw(r.Ev(SrvFeed::kUnitCreated, 1, 2), 0);   // player 1's held key: KnownCreation (a second record), window closed
		CHECK(r.Count(SeedStatus::kKnownCreation) == 1 && r.alloc.Counters().eraRecreated == 0);
		r.Raw(r.Ev(SrvFeed::kUnitCreated, 0, 1), 0);   // the window is closed: KnownCreation too
		CHECK(r.Count(SeedStatus::kKnownCreation) == 2);
	}
	{
		Rig r;
		r.Create(0, 1);                                  // 0
		r.Era(0, 3);
		r.Create(0, 7);                                  // a new unit of the era player: a plain creation (1), window closed
		CHECK(r.Seed(0, 7) == 1 && r.alloc.Counters().eraRecreated == 0 && r.statuses.empty());
		r.Embark(0, 1, 1);                               // window closed: an ordinary embark swap, not counted as era
		CHECK(r.alloc.Counters().eraRecreated == 0 && r.alloc.Counters().embarkSwaps == 1);
	}
	{
		Rig r;
		r.Create(0, 1);
		r.Era(0, 3);
		r.Raw(r.Ev(SrvFeed::kUnitFortify, 0, 1, 1), 0);   // a setup call keeps the window open
		r.Raw(r.Ev(SrvFeed::kUnitCreated, 0, 1), 0);
		CHECK(r.alloc.Counters().eraRecreated == 1 && r.statuses.empty());
		r.Era(9, 3);                                        // the next era change (player 9) replaces the window
		r.Raw(r.Ev(SrvFeed::kUnitCreated, 0, 1), 0);
		CHECK(r.Count(SeedStatus::kKnownCreation) == 1);
	}
}

// Issue 4469 (unit-formations.md 12.7.11 [L], 4465 J2 and 4469's run r1 on the CAT_0009 copy): an era change of
// Polynesia (player 0) re-creates its five embarked units through Embark(true) in walk order 1043, 1044, 1045, 1049,
// 1050 (indices 29..33) with the free list [34, 37, 36, 35]; the EXE frees all five, then creates them in walk order:
// 1043 -> 29, 1044 -> 33, 1045 -> 32, 1049 -> 31, 1050 -> 30 (era 1 and era 6); the next era change gives 29..33 back
// (era 4). 4462's rule took 1044 (an embarked worker whose land art row changes at era 5) at once: 30, 33, 32, 31.
static void TestEraEmbarkedGrouped()
{
	g_test = "era: embarked re-creations grouped (4469)";
	const int emb[5] = {1043, 1044, 1045, 1049, 1050};
	// indices 0..21 player 1, 22..28 player 0's other units, 29..33 the five embarked, 34..37 freed
	Rig q;
	for(int i = 0; i < 29; ++i) q.Create(i < 22 ? 1 : 0, 2000 + i);
	for(int k = 0; k < 5; ++k) q.Create(0, emb[k]);
	for(int i = 34; i < 38; ++i) q.Create(1, 2000 + i);
	q.Destroy(1, 2034); q.Destroy(1, 2035); q.Destroy(1, 2036); q.Destroy(1, 2037);
	CHECK(q.Free() == V({34, 37, 36, 35}));
	q.artChanging = {{0, 1044}};    // what 4462's art test said: ignored now
	const uint32_t want[3][5] = {{29, 33, 32, 31, 30}, {29, 30, 31, 32, 33}, {29, 33, 32, 31, 30}};
	for(int step = 0; step < 3; ++step)
	{
		q.batch = true;
		const double t = 10.0 + step;
		q.Era(0, step == 0 ? 1 : step == 1 ? 4 : 6, t);
		for(int k = 0; k < 5; ++k)
		{
			Event e = q.Ev(SrvFeed::kUnitEmbark, 0, emb[k], 1);
			e.plot = (k < 2) ? 777 : 780 + k;    // 1043 and 1044 share a plot (the embarked stack)
			q.Raw(e, t);
			q.Raw(q.Ev(SrvFeed::kUnitShouldDimFlag, 0, emb[k]), t);
		}
		q.alloc.Advance(t);
		q.batch = false;
		for(int k = 0; k < 5; ++k)
			CHECK(q.Seed(0, emb[k]) == (int64_t)want[step][k]);
		CHECK(q.Free() == V({34, 37, 36, 35}));
	}
	CHECK(q.alloc.Counters().eraRecreated == 15 && q.statuses.empty());
}
// Decision 14 (a): a visible human-turn combat with quick combat off holds its participants' removals until the
// modelled end; a creation before it allocates first; one within the band is ambiguous (counted LOUD).
static void TestCombatHold()
{
	g_test = "combat hold (decision 14 a)";
	HoldParams hp{};
	hp.lengthSec[(int)HoldClass::kMelee] = 4.0;
	hp.lengthSec[(int)HoldClass::kRanged] = 6.0;
	hp.bandSec[(int)HoldClass::kMelee] = hp.bandSec[(int)HoldClass::kRanged] = 0.5;
	{
		Rig r;
		r.alloc.SetHoldParams(hp);
		for(int i = 0; i < 4; ++i) r.Create(i < 2 ? 0 : 63, i + 1);   // 0..3; barbarian ids 3, 4
		r.Destroy(0, 1);                                              // head 0
		r.Combat(0, 2, 63, 4, true, true, false, HoldClass::kMelee, 10.0);
		r.Raw(r.Ev(SrvFeed::kUnitDestroyedInCombat, 63, 4), 10.1);
		r.Destroy(63, 4, 10.2);                                       // held until 14.0
		CHECK(r.alloc.PendingHeld() == 1);
		CHECK(r.Seed(63, 4) == 3);
		r.Create(0, 9, 11.0);                                         // passes the held removal: takes 0
		CHECK(r.Seed(0, 9) == 0);
		r.Create(0, 10, 12.0);                                        // free list empty: appends 4
		CHECK(r.Seed(0, 10) == 4);
		r.alloc.Advance(14.0);                                        // the removal: 3 freed
		CHECK(r.alloc.PendingHeld() == 0);
		CHECK(r.Seed(63, 4) == -1);
		CHECK(r.Free() == V({3}));
		CHECK(r.alloc.Counters().heldRemovals == 1);
		CHECK(r.statuses.empty());
		// in feed order the creation at 11.0 would have taken 0 too; the next case shows the difference
	}
	{
		// the case where the hold changes the index: free list empty, removal held, a creation appends instead of
		// taking the dead unit's index
		Rig r;
		r.alloc.SetHoldParams(hp);
		r.Create(0, 1);
		r.Create(63, 2);
		r.Combat(0, 1, 63, 2, true, true, false, HoldClass::kRanged, 0.0);
		r.Destroy(63, 2, 0.3);
		r.Create(0, 3, 1.0);
		CHECK(r.Seed(0, 3) == 2);                                     // feed order would give 1
		r.Create(0, 4, 6.6);                                          // after the end (6.0) and its band: Apply advances first
		CHECK(r.Seed(0, 4) == 1);
		CHECK(r.statuses.empty());
	}
	{
		// ambiguity: a creation within +-band of the modelled end
		Rig r;
		r.alloc.SetHoldParams(hp);
		r.Create(0, 1);
		r.Create(63, 2);
		r.Combat(0, 1, 63, 2, true, true, false, HoldClass::kMelee, 0.0);
		r.Destroy(63, 2, 0.2);
		r.Create(0, 3, 3.7);                                          // end 4.0, band 0.5
		CHECK(r.Count(SeedStatus::kCombatHoldAmbiguous) == 1);
		CHECK(r.alloc.Counters().combatHoldAmbiguous == 1);
		CHECK(r.Seed(0, 3) == 2);                                     // the model's order: created first
		// just after the modelled end (the removal already applied): ambiguous too
		r.Create(63, 5, 5.0);
		r.Combat(0, 1, 63, 5, true, true, false, HoldClass::kMelee, 5.0);
		r.Destroy(63, 5, 5.1);                                        // held until 9.0
		r.Create(0, 6, 9.3);                                          // released at 9.3's step, then created: within band
		CHECK(r.Count(SeedStatus::kCombatHoldAmbiguous) == 2);
		r.Create(0, 7, 12.0);                                         // far from any end: not ambiguous
		CHECK(r.Count(SeedStatus::kCombatHoldAmbiguous) == 2);
	}
	{
		// no hold: AI turn, quick combat, an invisible combat, a city side
		for(int variant = 0; variant < 3; ++variant)
		{
			Rig r;
			r.alloc.SetHoldParams(hp);
			r.Create(0, 1);
			r.Create(63, 2);
			r.Combat(0, 1, 63, 2, variant != 2, variant != 0, variant == 1, HoldClass::kMelee, 0.0);
			r.Destroy(63, 2, 0.2);
			CHECK(r.alloc.PendingHeld() == 0);
			r.Create(0, 3, 1.0);
			CHECK(r.Seed(0, 3) == 1);
		}
		Rig r;
		r.alloc.SetHoldParams(hp);
		r.Create(0, 1);
		r.Combat(-1, -1, 0, 1, true, true, false, HoldClass::kRanged, 0.0);   // a city attacks
		r.Destroy(0, 1, 0.1);
		CHECK(r.alloc.PendingHeld() == 1);                            // the defender's removal is held
		r.alloc.Advance(6.0);
		CHECK(r.Seed(0, 1) == -1);
	}
	{
		// no hold table: removals in feed order; a creation behind one that a hold may have deferred is ambiguous
		Rig r;
		r.Create(0, 1);
		r.Create(63, 2);
		r.Combat(0, 1, 63, 2, true, true, false, HoldClass::kMelee, 0.0);
		r.Destroy(63, 2, 0.2);
		CHECK(r.alloc.PendingHeld() == 0);
		r.Create(0, 3, 5.0);
		CHECK(r.Seed(0, 3) == 1);
		CHECK(r.Count(SeedStatus::kCombatHoldAmbiguous) == 1);
		r.Create(0, 4, 25.0);                                         // past the 20 s window: not ambiguous
		CHECK(r.Count(SeedStatus::kCombatHoldAmbiguous) == 1);
	}
	{
		// a load drops the held removals (the EXE's reset frees every live record)
		Rig r;
		r.alloc.SetHoldParams(hp);
		r.Create(0, 1);
		r.Create(63, 2);
		r.Combat(0, 1, 63, 2, true, true, false, HoldClass::kMelee, 0.0);
		r.Destroy(63, 2, 0.2);
		r.Load(1.0);
		CHECK(r.alloc.PendingHeld() == 0);
		CHECK(r.Free() == V({0, 1}));
		r.alloc.Advance(10.0);
		CHECK(r.Free() == V({0, 1}));
	}
}

// The EXE's frame order (12.7.6 [L], the probes of 2026-11-16 on c5h-c): inside one frame (one group) a creation is
// applied after the removals of other units; a unit's own events keep their order behind its creation.
static void Base(Rig& r, int n)
{
	for(int i = 0; i < n; ++i) r.Create(i % 2 ? 1 : 0, 2000 + i);
}

static void TestFrameOrder()
{
	g_test = "frame order (12.7.6)";
	{
		// ORD-create-then-kill: 22 live, free none; one frame: create, kill index 21 -> the creation takes 21, size 22
		Rig r;
		Base(r, 22);
		r.batch = true;
		r.Create(0, 1040, 100);
		r.Destroy(1, 2021, 100);
		CHECK(r.Seed(0, 1040) == -1 && r.alloc.PendingCreations() == 1);   // still pending in the group
		r.alloc.Advance(100);
		CHECK(r.Seed(0, 1040) == 21 && r.alloc.ArraySize() == 22);
		CHECK(r.alloc.Counters().deferredCreates == 23);   // 22 base creations (each its own frame) + 1
		CHECK(r.statuses.empty());
	}
	{
		// ORD-P1 / ORD-P4: free [18,20]; one frame: c A, k 2, c B, k 1 -> A=1, B=2, free [18,20] (feed order: 18, 2)
		Rig r;
		Base(r, 24);
		r.Destroy(0, 2018);
		r.Destroy(0, 2020);
		CHECK(r.Free() == V({18, 20}));
		r.batch = true;
		r.Create(0, 3001, 100);
		r.Destroy(0, 2002, 100);
		r.Create(0, 3002, 100);
		r.Destroy(1, 2001, 100);
		r.alloc.Advance(100.0);
		CHECK(r.Seed(0, 3001) == 1 && r.Seed(0, 3002) == 2);
		CHECK(r.Free() == V({18, 20}));
		// the same events in feed order (grouping off)
		Rig f;
		f.alloc.SetFrameGrouping(false);
		Base(f, 24);
		f.Destroy(0, 2018);
		f.Destroy(0, 2020);
		f.batch = true;
		f.Create(0, 3001, 100);
		f.Destroy(0, 2002, 100);
		f.Create(0, 3002, 100);
		f.Destroy(1, 2001, 100);
		CHECK(f.Seed(0, 3001) == 18 && f.Seed(0, 3002) == 2);
	}
	{
		// S3-embark-above-head: 22 live; one frame: c K, k 1, embark K -> K at 1, size 22 (its removal waits behind its
		// creation: no second record)
		Rig r;
		Base(r, 22);
		r.batch = true;
		r.Create(0, 1039, 100);
		r.Destroy(1, 2001, 100);
		r.Embark(0, 1039, 1, 100);
		r.alloc.Advance(100);
		CHECK(r.Seed(0, 1039) == 1 && r.alloc.ArraySize() == 22);
		std::vector<SeedRecord> live;
		r.alloc.LiveRecords(live);
		CHECK(live.size() == 22);
		CHECK(r.statuses.empty());
	}
	{
		// S3-upgrade: one frame: the new unit's creation, the old unit's removal -> the new unit takes the old index
		Rig r;
		Base(r, 26);
		r.batch = true;
		r.Create(0, 1057, 100);
		r.Destroy(0, 2000, 100);
		r.alloc.Advance(100);
		CHECK(r.Seed(0, 1057) == 0 && r.alloc.ArraySize() == 26);
	}
	{
		// a later tSec ends the group by itself (Apply flushes before the new group); a removal of a key whose creation
		// is pending is not an unknown removal
		Rig r;
		r.batch = true;
		r.Create(0, 1, 1.0);
		r.Destroy(0, 1, 1.0);
		CHECK(r.statuses.empty());
		r.Create(0, 2, 2.0);
		CHECK(r.alloc.PendingCreations() == 1);
		r.alloc.Advance(2.0);
		CHECK(r.Seed(0, 1) == -1 && r.Seed(0, 2) == 0);
		CHECK(r.Free().empty() && r.alloc.ArraySize() == 1);
		// a load flushes the pending group first
		r.Create(0, 3, 3.0);
		r.Load(3.0);
		CHECK(r.alloc.ArraySize() == 2 && r.Free() == V({0, 1}));
	}
}

// ---- issue 4462: the rules 4458 measured on the product path (unit-formations.md 12.7.7-12.7.10) ----

// (a) 12.7.7 (2): an era re-creation is a removal and a creation applied at once, one unit at a time, also inside a
// drain's group: each re-created unit keeps its index (free list empty), only the live list's order changes.
static void TestEraAtOnce()
{
	g_test = "era re-creation at once (4462 a)";
	{
		Rig r;
		for(int i = 0; i < 26; ++i) r.Create(i < 20 ? 0 : 63, 1020 + i);   // 0..25, player 0 owns 1020-1039
		r.artChanging = {{0, 1022}, {0, 1024}, {0, 1025}};
		r.batch = true;
		r.Era(0, 6, 50.0);
		r.Raw(r.Ev(SrvFeed::kUnitCreated, 0, 1022), 50.0);
		r.Raw(r.Ev(SrvFeed::kUnitShouldDimFlag, 0, 1022), 50.0);
		CHECK(r.Seed(0, 1022) == 2);                       // at once: seen before the group ends
		// 4458's case (12.7.7 (2)): the worker (Created), then two embarked warriors (Embark(true)); issue 4469: the
		// embarked ones are the group's swaps (both removals, then both creations: indices kept here)
		r.Raw(r.Ev(SrvFeed::kUnitEmbark, 0, 1024, 1), 50.0);
		r.Raw(r.Ev(SrvFeed::kUnitEmbark, 0, 1025, 1), 50.0);
		r.alloc.Advance(50.0);
		CHECK(r.Seed(0, 1022) == 2 && r.Seed(0, 1024) == 4 && r.Seed(0, 1025) == 5);
		CHECK(r.Free().empty() && r.alloc.ArraySize() == 26);
		std::vector<SeedRecord> live;
		r.alloc.LiveRecords(live);
		CHECK(live.size() == 26 && live[23].index == 2 && live[24].index == 4 && live[25].index == 5);
		CHECK(r.alloc.Counters().eraRecreated == 3 && r.alloc.PendingCreations() == 0);
		CHECK(r.statuses.empty());
	}
	{
		// a free head below the re-created units: each creation takes the head and its own index goes behind it (the
		// free rule unchanged); the old rule (removals at once, creations at the group's end) gave 3 -> 0, 5 -> 2 too
		// here but permuted 4458's case above (removals 2, 4, 5 then creations: 2, 5, 4)
		Rig r;
		for(int i = 0; i < 6; ++i) r.Create(0, i + 1);   // 0..5
		r.Destroy(0, 1);                                   // free [0]: a unit freed above the head goes behind it
		r.artChanging = {{0, 3}, {0, 5}};
		r.batch = true;
		r.Era(0, 2, 9.0);
		r.Raw(r.Ev(SrvFeed::kUnitCreated, 0, 3), 9.0);   // free 2 behind head 0: 0, 2; take 0
		r.Raw(r.Ev(SrvFeed::kUnitCreated, 0, 5), 9.0);   // free 4 behind head 2: 2, 4; take 2
		r.alloc.Advance(9.0);
		CHECK(r.Seed(0, 3) == 0 && r.Seed(0, 5) == 2 && r.Free() == V({4}));
	}
	{
		// a unit with a creation pending in the group keeps its order behind it (no second record)
		Rig r;
		r.Create(0, 1);
		r.batch = true;
		r.Create(0, 2, 3.0);                               // pending in the group
		r.artChanging = {{0, 1}, {0, 2}};
		r.Era(0, 1, 3.0);                                  // 2 has no record yet: not in the walk
		r.Raw(r.Ev(SrvFeed::kUnitCreated, 0, 1), 3.0);    // 1 re-created at once: free 0, take 0
		CHECK(r.Seed(0, 1) == 0 && r.alloc.PendingCreations() == 1);
		r.alloc.Advance(3.0);
		CHECK(r.Seed(0, 2) == 1 && r.alloc.ArraySize() == 2);
	}
}

// (b) 12.7.7 (3): a paradrop is a removal and a creation at once: with the free list empty the unit keeps its index,
// with a free head below it the unit moves to the head and its old index goes behind.
static void TestParadrop()
{
	g_test = "paradrop remove + create (4462 b)";
	{
		Rig r;
		for(int i = 0; i < 27; ++i) r.Create(0, 1000 + i);   // 0..26
		r.batch = true;
		r.OnPlot(SrvFeed::kUnitParadrop, 0, 1026, 500, 7.0);
		CHECK(r.Seed(0, 1026) == 26);                          // at once, before the group's end
		r.alloc.Advance(7.0);
		r.batch = false;
		r.Create(0, 1027, 7.5);                                // 27
		r.Destroy(0, 1000, 7.6);                               // free [0]
		r.batch = true;
		r.OnPlot(SrvFeed::kUnitParadrop, 0, 1027, 501, 8.0);
		CHECK(r.Seed(0, 1027) == 0);
		r.alloc.Advance(8.0);
		CHECK(r.Free() == V({27}) && r.alloc.ArraySize() == 28);
		CHECK(r.alloc.Counters().paradropFrees == 2 && r.statuses.empty());
	}
	{
		// negative: a paradrop of a key with no record outside a load: kUnknownRemoval (LOUD), and the unit created
		Rig r;
		r.Create(0, 1);
		r.Raw(r.Ev(SrvFeed::kUnitParadrop, 0, 9), 1.0);
		CHECK(r.Count(SeedStatus::kUnknownRemoval) == 1 && r.alloc.Counters().unknownRemoval == 1);
		CHECK(!r.details.empty() && r.details.back().find("paradrop of unknown key (0,9)") != std::string::npos);
		CHECK(r.Seed(0, 9) == 1 && r.alloc.Counters().paradropFrees == 0);
		// inside a load window: the EXE's expected no-op, no status
		r.Load(2.0);
		r.Raw(r.Ev(SrvFeed::kUnitParadrop, 0, 9), 2.0);
		CHECK(r.Count(SeedStatus::kUnknownRemoval) == 1);
	}
}

// (d) 12.7.8 / 12.7.10: the removal of a unit that died in combat (kind 15 seen) is applied after the same group's
// creations, in AI turns too (no hold, no visible combat); a removal without kind 15 still comes before them.
static void TestCombatDeathLate()
{
	g_test = "combat death freed late (4462 d)";
	{
		// turn 28 -> 29 of 12.7.8: 53 live, the barbarian at 48 died in combat; the DLL then created 1073, 1074, 1075:
		// the EXE gave 53, 54 to the first two and the freed 48 to the third, the old rule 48 to the first
		Rig r;
		for(int i = 0; i < 53; ++i) r.Create(i == 48 ? 63 : 3, 1000 + i);
		r.batch = true;
		r.Combat(3, 1001, 63, 1048, false, false, false, HoldClass::kMelee, 20.0);   // AI turn, not shown
		r.Raw(r.Ev(SrvFeed::kUnitDestroyedInCombat, 63, 1048), 20.0);
		r.Destroy(63, 1048, 20.0);
		CHECK(r.Seed(63, 1048) == 48);                       // still held by the group
		r.Create(3, 1073, 20.0);
		r.Create(3, 1074, 20.0);
		r.alloc.Advance(20.0);
		CHECK(r.Seed(3, 1073) == 53 && r.Seed(3, 1074) == 54 && r.Seed(63, 1048) == -1);
		CHECK(r.Free() == V({48}));
		r.Create(24, 1075, 21.0);                            // the next drain takes the freed 48
		r.alloc.Advance(21.0);
		CHECK(r.Seed(24, 1075) == 48);
		CHECK(r.alloc.Counters().combatLateFrees == 1 && r.alloc.PendingHeld() == 0 && r.statuses.empty());
	}
	{
		// turn 34 -> 35: three combat deaths (45, 60, 65) and three creations in one group: the creations append or take
		// what was free before, the three frees come after them
		Rig r;
		for(int i = 0; i < 67; ++i) r.Create(i == 45 || i == 60 || i == 65 ? 63 : 3, 1000 + i);
		r.batch = true;
		for(int dead : {45, 60, 65})
		{
			r.Raw(r.Ev(SrvFeed::kUnitDestroyedInCombat, 63, 1000 + dead), 30.0);
			r.Destroy(63, 1000 + dead, 30.0);
		}
		r.Create(3, 1089, 30.0);
		r.alloc.Advance(30.0);
		CHECK(r.Seed(3, 1089) == 67);
		CHECK(r.Free() == V({45, 65, 60}));
		CHECK(r.alloc.Counters().combatLateFrees == 3);
	}
	{
		// a removal without kind 15 (an out-of-combat death) still comes before the group's creations
		Rig r;
		for(int i = 0; i < 4; ++i) r.Create(0, i + 1);
		r.batch = true;
		r.Destroy(0, 2, 5.0);
		r.Create(0, 9, 5.0);
		r.alloc.Advance(5.0);
		CHECK(r.Seed(0, 9) == 1 && r.alloc.Counters().combatLateFrees == 0);
	}
	{
		// grouping off: feed order, the removal at once
		Rig r;
		r.alloc.SetFrameGrouping(false);
		for(int i = 0; i < 4; ++i) r.Create(0, i + 1);
		r.batch = true;
		r.Raw(r.Ev(SrvFeed::kUnitDestroyedInCombat, 0, 2), 5.0);
		r.Destroy(0, 2, 5.0);
		r.Create(0, 9, 5.0);
		CHECK(r.Seed(0, 9) == 1);
	}
	{
		// a visible human-turn combat: decision 14's hold takes the removal (seconds), not the group's end
		HoldParams hp{};
		hp.lengthSec[0] = hp.lengthSec[1] = 4.0;
		hp.bandSec[0] = hp.bandSec[1] = 0.3;
		Rig r;
		r.alloc.SetHoldParams(hp);
		r.Create(0, 1);
		r.Create(63, 2);
		r.Combat(0, 1, 63, 2, true, true, false, HoldClass::kMelee, 0.0);
		r.Raw(r.Ev(SrvFeed::kUnitDestroyedInCombat, 63, 2), 0.1);
		r.Destroy(63, 2, 0.2);
		CHECK(r.alloc.PendingHeld() == 1 && r.Seed(63, 2) == 1);
		r.alloc.Advance(4.1);
		CHECK(r.Seed(63, 2) == -1 && r.alloc.Counters().combatLateFrees == 0 && r.alloc.Counters().heldRemovals == 1);
		// a load forgets the deaths seen: the same key's later removal is an ordinary one
		r.Create(63, 3, 5.0);
		r.Raw(r.Ev(SrvFeed::kUnitDestroyedInCombat, 63, 3), 5.0);
		r.Load(6.0);
		r.Create(63, 3, 6.5);
		r.Create(0, 4, 7.0);
		r.batch = true;
		r.Destroy(63, 3, 8.0);
		r.Create(0, 5, 8.0);
		r.alloc.Advance(8.0);
		CHECK(r.Seed(0, 5) == 0 && r.alloc.Counters().combatLateFrees == 0);   // late, it would have appended 2
	}
}

// (e) 12.7.9: inside one group, after the removals, first the creations of units with no earlier event of another
// unit on their plot, then (in feed order) the creations after such an event and the Embark creations of keys with no
// record. The E-R frame of 4458: ten creations, 22 records live, free list empty.
static void TestStackedLate()
{
	g_test = "stacked and embarked creations late (4462 e)";
	{
		Rig r;
		Base(r, 22);
		r.batch = true;
		const double t = 135789.7667;
		using namespace SrvFeed;
		// 1039, 1040 on plot 834
		r.OnPlot(kUnitTeleported, 0, 1039, 834, t); r.OnPlot(kUnitVisibility, 0, 1039, 834, t, 1);
		r.OnPlot(kUnitShouldDimFlag, 0, 1039, 834, t); r.OnPlot(kUnitCreated, 0, 1039, 834, t);
		r.OnPlot(kUnitFortify, 0, 1039, 834, t);
		r.OnPlot(kUnitTeleported, 0, 1040, 834, t); r.OnPlot(kUnitVisibility, 0, 1040, 834, t, 1);
		r.OnPlot(kUnitVisibility, 0, 1039, 834, t, 0); r.OnPlot(kUnitShouldDimFlag, 0, 1040, 834, t);
		r.OnPlot(kUnitCreated, 0, 1040, 834, t);
		// 1041, 1042 in the city (835)
		r.OnPlot(kUnitVisibility, 0, 1041, 835, t); r.OnPlot(kUnitTeleported, 0, 1041, 835, t);
		r.OnPlot(kUnitCreated, 0, 1041, 835, t); r.OnPlot(kUnitGarrison, 0, 1041, 835, t, 1);
		r.OnPlot(kUnitVisibility, 0, 1042, 835, t); r.OnPlot(kUnitTeleported, 0, 1042, 835, t);
		r.OnPlot(kUnitVisibility, 0, 1041, 835, t); r.OnPlot(kUnitCreated, 0, 1042, 835, t);
		// 1043, 1044 on water (779), 1045 alone on water (723): Embark(1), no Created
		r.OnPlot(kUnitTeleported, 0, 1043, 779, t); r.OnPlot(kUnitEmbark, 0, 1043, 779, t, 1);
		r.OnPlot(kUnitTeleported, 0, 1044, 779, t); r.OnPlot(kUnitVisibility, 0, 1043, 779, t, 1);
		r.OnPlot(kUnitEmbark, 0, 1044, 779, t, 1);
		r.OnPlot(kUnitTeleported, 0, 1045, 723, t); r.OnPlot(kUnitEmbark, 0, 1045, 723, t, 1);
		// three lone warriors
		const int lone[3][2] = {{1046, 889}, {1047, 892}, {1048, 948}};
		for(const auto& u : lone)
		{
			r.OnPlot(kUnitTeleported, 0, u[0], u[1], t);
			r.OnPlot(kUnitCreated, 0, u[0], u[1], t);
			r.OnPlot(kUnitFortify, 0, u[0], u[1], t);
		}
		r.alloc.Advance(t);
		const int want[10][2] = {{1039, 22}, {1040, 27}, {1041, 23}, {1042, 28}, {1043, 29},
		                         {1044, 30}, {1045, 31}, {1046, 24}, {1047, 25}, {1048, 26}};
		bool allOk = true;
		for(const auto& w : want)
			if(r.Seed(0, w[0]) != w[1])
			{
				allOk = false;
				std::printf("  unit %d: seed %lld, the EXE's %d\n", w[0], (long long)r.Seed(0, w[0]), w[1]);
			}
		CHECK(allOk);
		CHECK(r.alloc.Counters().lateCreates == 5 && r.statuses.empty());
	}
	{
		// an Embark of a known key (remove + create) is not late; a Created without a plot is never late; the unit's
		// own earlier events on its plot do not make it late
		Rig r;
		for(int i = 0; i < 3; ++i) r.Create(0, i + 1);   // 0..2
		r.batch = true;
		r.OnPlot(SrvFeed::kUnitTeleported, 0, 7, 10, 4.0);
		r.OnPlot(SrvFeed::kUnitCreated, 1, 8, 10, 4.0);    // after 7's event on plot 10: late
		r.OnPlot(SrvFeed::kUnitTeleported, 0, 9, 11, 4.0);
		r.OnPlot(SrvFeed::kUnitCreated, 0, 9, 11, 4.0);    // its own event only: first pass
		r.OnPlot(SrvFeed::kUnitEmbark, 0, 3, 10, 4.0, 1);  // known key: remove + create, first pass
		r.alloc.Advance(4.0);
		CHECK(r.Seed(0, 9) == 2 && r.Seed(0, 3) == 3 && r.Seed(1, 8) == 4);   // 3's free (index 2) at once, 9 takes it
		CHECK(r.alloc.Counters().lateCreates == 1);
		// grouping off: everything in feed order
		Rig f;
		f.alloc.SetFrameGrouping(false);
		f.batch = true;
		f.OnPlot(SrvFeed::kUnitTeleported, 0, 7, 10, 4.0);
		f.OnPlot(SrvFeed::kUnitCreated, 1, 8, 10, 4.0);
		f.OnPlot(SrvFeed::kUnitCreated, 0, 9, 11, 4.0);
		CHECK(f.Seed(1, 8) == 0 && f.Seed(0, 9) == 1 && f.alloc.Counters().lateCreates == 0);
		// a new group forgets the plots of the last one
		Rig g;
		g.batch = true;
		g.OnPlot(SrvFeed::kUnitTeleported, 0, 7, 10, 1.0);
		g.OnPlot(SrvFeed::kUnitCreated, 1, 8, 10, 2.0);
		g.alloc.Advance(2.0);
		CHECK(g.alloc.Counters().lateCreates == 0);
	}
	{
		// a load's setup (the group of its first creation) keeps feed order (S2; 12.7.7's reload with an Embark(true)
		// unit equal in feed order): an embarked unit and a second unit on a plot are not late there; the next group is
		Rig r;
		r.Load(1.0);
		r.batch = true;
		r.OnPlot(SrvFeed::kUnitCreated, 0, 1, 20, 2.0);
		r.OnPlot(SrvFeed::kUnitEmbark, 0, 2, 21, 2.0, 1);
		r.OnPlot(SrvFeed::kUnitCreated, 0, 3, 20, 2.0);   // second on plot 20
		r.OnPlot(SrvFeed::kUnitCreated, 0, 4, 22, 2.0);
		r.alloc.Advance(2.0);
		CHECK(r.Seed(0, 1) == 0 && r.Seed(0, 2) == 1 && r.Seed(0, 3) == 2 && r.Seed(0, 4) == 3);
		CHECK(r.alloc.Counters().lateCreates == 0);
		r.OnPlot(SrvFeed::kUnitEmbark, 0, 5, 23, 3.0, 1);
		r.OnPlot(SrvFeed::kUnitCreated, 0, 6, 24, 3.0);
		r.alloc.Advance(3.0);
		CHECK(r.Seed(0, 6) == 4 && r.Seed(0, 5) == 5 && r.alloc.Counters().lateCreates == 1);
	}
}

// The hold table's band per class (4462): a creation 0.5 s after a melee hold's end is ambiguous with band 0.6,
// one 0.5 s after a ranged hold's end is not with band 0.35.
static void TestBandPerClass()
{
	g_test = "hold band per class (4462)";
	HoldParams hp{};
	hp.lengthSec[(int)HoldClass::kMelee] = 4.3;
	hp.lengthSec[(int)HoldClass::kRanged] = 3.9;
	hp.bandSec[(int)HoldClass::kMelee] = 0.6;
	hp.bandSec[(int)HoldClass::kRanged] = 0.35;
	for(int cls = 0; cls < 2; ++cls)
	{
		Rig r;
		r.alloc.SetHoldParams(hp);
		r.Create(0, 1);
		r.Create(63, 2);
		r.Combat(0, 1, 63, 2, true, true, false, (HoldClass)cls, 10.0);
		r.Destroy(63, 2, 10.2);
		const double end = 10.0 + hp.lengthSec[cls];
		r.Create(0, 3, end + 0.5);
		CHECK(r.Count(SeedStatus::kCombatHoldAmbiguous) == (cls == 0 ? 1u : 0u));
		CHECK(r.Seed(0, 3) == 1);   // the removal at the end applied first (the model's order)
		Rig b;
		b.alloc.SetHoldParams(hp);
		b.Create(0, 1);
		b.Create(63, 2);
		b.Combat(0, 1, 63, 2, true, true, false, (HoldClass)cls, 10.0);
		b.Destroy(63, 2, 10.2);
		b.Create(0, 3, end - 0.3);  // inside both bands: ambiguous, created before the free
		CHECK(b.Count(SeedStatus::kCombatHoldAmbiguous) == 1 && b.Seed(0, 3) == 2);
	}
}

static void TestRestore()
{
	g_test = "RestoreForTest";
	Rig r;
	std::vector<SeedRecord> live = {{2, UnitKey{0, 7}, true}, {0, UnitKey{1, 8}, true}};
	CHECK(r.alloc.RestoreForTest(4, live, V({3, 1})));
	CHECK(r.Seed(0, 7) == 2 && r.Seed(1, 8) == 0 && r.Free() == V({3, 1}));
	r.Create(0, 9);
	CHECK(r.Seed(0, 9) == 3);
	CHECK(!r.alloc.RestoreForTest(4, live, V({3})));        // not a partition
	CHECK(!r.alloc.RestoreForTest(4, live, V({3, 2})));     // index twice
}

// Negative tests: every status.
static void TestStatuses()
{
	g_test = "statuses";
	{
		Rig r;
		r.Destroy(0, 42);
		CHECK(r.Count(SeedStatus::kUnknownRemoval) == 1 && r.alloc.Counters().unknownRemoval == 1);
		CHECK(r.details.back().find("(0,42)") != std::string::npos);
		CHECK(!r.alloc.OutOfStep());
	}
	{
		Rig r;
		r.Create(0, 1);
		r.Create(0, 1);   // known creation outside an era change: a second record, the old one an orphan
		CHECK(r.Count(SeedStatus::kKnownCreation) == 1 && r.alloc.Counters().knownCreation == 1);
		CHECK(r.Seed(0, 1) == 1);
		std::vector<SeedRecord> live;
		r.alloc.LiveRecords(live);
		CHECK(live.size() == 2 && !live[0].mapped && live[1].mapped);
		r.Destroy(0, 1);  // frees the newer record only (the key map's)
		CHECK(r.Free() == V({1}));
		r.alloc.LiveRecords(live);
		CHECK(live.size() == 1 && live[0].index == 0);
		r.Load();         // the orphan goes at the load clear
		CHECK(r.Free() == V({0, 1}));
	}
	{
		Rig r;
		r.alloc.NoteDropped(3);
		CHECK(r.alloc.OutOfStep() && r.Count(SeedStatus::kOutOfStep) == 1 && r.alloc.Counters().outOfStepEpisodes == 1);
	}
	{
		Rig r;
		r.Create(0, 1);
		Event e = r.Ev(SrvFeed::kUnitCreated, 0, 2);
		e.seq = 1;   // not above the last one
		r.Raw(e, 0);
		CHECK(r.Count(SeedStatus::kOutOfStep) == 1 && r.Seed(0, 2) == -1);
	}
	{
		Rig r;
		r.Raw(r.Ev(SrvFeed::kUnitCreated, 64, 1), 0);
		r.Raw(r.Ev(SrvFeed::kUnitCreated, 0, -5), 0);
		Event e{}; e.seq = ++r.seq; e.kind = SrvFeed::kEraChanged; e.a[0] = 300; e.a[1] = 1;
		r.Raw(e, 0);
		CHECK(r.Count(SeedStatus::kOutOfStep) == 3);
		CHECK(r.alloc.ArraySize() == 0);
	}
	{
		HoldParams hp{};
		hp.lengthSec[0] = hp.lengthSec[1] = 5.0;
		hp.bandSec[0] = hp.bandSec[1] = 0.1;
		Rig r;
		r.alloc.SetHoldParams(hp);
		r.Create(0, 1);
		r.Create(63, 2);
		r.Combat(0, 1, 63, 2, true, true, false, HoldClass::kMelee, 0.0);
		r.Destroy(63, 2, 0.1);
		r.Raw(r.Ev(SrvFeed::kUnitDestroyed, 63, 2), 0.2);   // a second removal of a held key
		CHECK(r.Count(SeedStatus::kOutOfStep) == 1);
	}
	{
		// an era change whose unit is missing from the unit table: since issue 4469 the window walks no table (the
		// DLL's re-creations are its input), so nothing to refuse; the re-creation still applies (was kOutOfStep)
		Rig r;
		r.Create(0, 1);
		r.table.clear();
		r.Era(0, 1);
		r.Raw(r.Ev(SrvFeed::kUnitCreated, 0, 1), 0);
		CHECK(r.statuses.empty() && r.Seed(0, 1) == 0 && r.alloc.Counters().eraRecreated == 1);
	}
	{
		// a combat event out of seq order
		Rig r;
		r.Create(0, 1);
		CombatNote c{};
		Event e = r.Ev(SrvFeed::kUnitCombat, 0, 1);
		e.seq = 1;
		r.alloc.NoteCombat(e, 0, c);
		CHECK(r.Count(SeedStatus::kOutOfStep) == 1);
	}
#ifdef SEED_TEST_INJECT_FAIL
	g_test = "injected failure";
	CHECK(false);
#endif
}

// ---- replay ----
static int Replay(const char* path)
{
	FILE* f = std::fopen(path, "r");
	if(!f)
	{
		std::printf("replay: cannot open %s\n", path);
		return 2;
	}
	std::vector<UnitRecord> table;
	std::set<std::pair<int, int>> art;
	std::vector<std::string> log;
	unsigned long long xUnits = 0, xEqual = 0, xChecks = 0;
	SeedAllocator alloc([&](const UnitRecord& u, int, int) { return art.count(std::make_pair((int)u.owner, (int)u.id)) != 0; },
	                    [&](SeedStatus s, const Event& e, const char* d) {
		                    std::printf("S %d seq %u kind %u %s\n", (int)s, e.seq, (unsigned)e.kind, d ? d : "");
	                    });
	static char line[65536];
	int ln = 0;
	double lastT = 0.0;
	while(std::fgets(line, (int)sizeof line, f))
	{
		++ln;
		if(line[0] == '#' || line[0] == '\n' || line[0] == '\r' || line[0] == 0)
			continue;
		char tag = line[0];
		if(tag == 'H')
		{
			HoldParams hp{};
			const int n = std::sscanf(line + 1, "%lf %lf %lf %lf", &hp.lengthSec[0], &hp.lengthSec[1], &hp.bandSec[0],
			                          &hp.bandSec[1]);
			if(n == 3)
				hp.bandSec[1] = hp.bandSec[0];
			else if(n != 4)
				goto bad;
			alloc.SetHoldParams(hp);
		}
		else if(tag == 'E')
		{
			unsigned seq, kind;
			double t;
			int owner, id, a2, a3, plot = -1;
			const int n = std::sscanf(line + 1, "%u %lf %u %d %d %d %d %d", &seq, &t, &kind, &owner, &id, &a2, &a3, &plot);
			if(n != 7 && n != 8)
				goto bad;
			lastT = t;
			Event e{};
			e.seq = seq;
			e.kind = (uint16_t)kind;
			e.plot = plot;
			e.a[0] = owner; e.a[1] = id; e.a[2] = a2; e.a[3] = a3;
			if(kind == SrvFeed::kLoadBegin)
				table.clear();
			else if(kind == SrvFeed::kUnitCreated || kind == SrvFeed::kUnitEmbark)
			{
				bool found = false;
				for(auto& r : table) found = found || (r.owner == owner && r.id == id);
				if(!found) { UnitRecord r{}; r.owner = (int8_t)owner; r.id = id; table.push_back(r); }
			}
			alloc.Apply(e, t, table);
			if(kind == SrvFeed::kEraChanged)
				art.clear();
			if(kind == SrvFeed::kUnitDestroyed)
				for(size_t i = 0; i < table.size(); ++i)
					if(table[i].owner == owner && table[i].id == id) { table.erase(table.begin() + (long)i); break; }
		}
		else if(tag == 'C')
		{
			unsigned seq, kind;
			double t;
			int ao, ai, downer, did, vis, hum, quick, cls;
			if(std::sscanf(line + 1, "%u %lf %u %d %d %d %d %d %d %d %d", &seq, &t, &kind, &ao, &ai, &downer, &did, &vis,
			               &hum, &quick, &cls) != 11 || cls < 0 || cls > 1)
				goto bad;
			Event e{};
			e.seq = seq;
			e.kind = (uint16_t)kind;
			e.a[0] = ao; e.a[1] = ai;
			CombatNote c{};
			c.attacker = UnitKey{(int8_t)ao, ai};
			c.defender = UnitKey{(int8_t)downer, did};
			c.visualize = vis != 0; c.humanTurn = hum != 0; c.quickCombat = quick != 0; c.cls = (HoldClass)cls;
			alloc.NoteCombat(e, t, c);
		}
		else if(tag == 'A')
		{
			int owner, id;
			if(std::sscanf(line + 1, "%d %d", &owner, &id) != 2)
				goto bad;
			art.insert(std::make_pair(owner, id));
		}
		else if(tag == 'D')
		{
			unsigned long long n;
			if(std::sscanf(line + 1, "%llu", &n) != 1)
				goto bad;
			alloc.NoteDropped(n);
		}
		else if(tag == 'Z')
		{
			// Z <size> <n live> <index owner id>... <n free> <i>...: the reader's table as the state (RestoreForTest)
			std::vector<SeedRecord> live;
			std::vector<uint32_t> fl;
			const char* q = line + 1;
			int used = 0;
			unsigned size, nl, nf;
			if(std::sscanf(q, "%u %u%n", &size, &nl, &used) != 2)
				goto bad;
			q += used;
			for(unsigned k = 0; k < nl; ++k)
			{
				unsigned idx;
				int owner, id;
				if(std::sscanf(q, "%u %d %d%n", &idx, &owner, &id, &used) != 3)
					goto bad;
				q += used;
				live.push_back(SeedRecord{idx, UnitKey{(int8_t)owner, id}, true});
			}
			if(std::sscanf(q, "%u%n", &nf, &used) != 1)
				goto bad;
			q += used;
			for(unsigned k = 0; k < nf; ++k)
			{
				unsigned idx;
				if(std::sscanf(q, "%u%n", &idx, &used) != 1)
					goto bad;
				q += used;
				fl.push_back(idx);
			}
			if(!alloc.RestoreForTest(size, live, fl))
				goto bad;
			table.clear();
			for(const auto& r : live) { UnitRecord u{}; u.owner = r.key.owner; u.id = r.key.id; table.push_back(u); }
		}
		else if(tag == 'V')
		{
			double t;
			if(std::sscanf(line + 1, "%lf", &t) != 1)
				goto bad;
			alloc.Advance(t);
		}
		else if(tag == 'X')
		{
			// X <tag> <n> <owner id index>...: the reader's table at a check, compared unit by unit (issue 4462)
			char name[128] = {0};
			const char* q = line + 1;
			int used = 0;
			unsigned n;
			if(std::sscanf(q, "%127s %u%n", name, &n, &used) != 2)
				goto bad;
			q += used;
			alloc.Advance(lastT);
			unsigned equal = 0;
			std::string diffs;
			for(unsigned k = 0; k < n; ++k)
			{
				int owner, id;
				unsigned idx;
				if(std::sscanf(q, "%d %d %u%n", &owner, &id, &idx, &used) != 3 || owner < -128 || owner > 127)
					goto bad;
				q += used;
				uint32_t seed = 0;
				const bool have = alloc.SeedOf(UnitKey{(int8_t)owner, id}, seed);
				if(have && seed == idx)
					++equal;
				else
				{
					char d[64];
					std::snprintf(d, sizeof d, " [%d %d %u %lld]", owner, id, idx, have ? (long long)seed : -1LL);
					diffs += d;
				}
			}
			int extra = 0;
			if(std::sscanf(q, "%d", &extra) == 1)
				goto bad;   // more units than n
			std::printf("X %s units %u equal %u diffs%s\n", name, n, equal, diffs.c_str());
			xUnits += n;
			xEqual += equal;
			++xChecks;
		}
		else if(tag == 'R')
		{
			char name[128] = {0};
			if(std::sscanf(line + 1, "%127s", name) != 1)
				goto bad;
			alloc.Advance(lastT);   // a report ends the group (the scene advances before it reads seeds)
			std::vector<SeedRecord> live;
			alloc.LiveRecords(live);
			std::vector<uint32_t> fl;
			alloc.FreeList(fl);
			std::printf("R %s size %u live %zu pending %zu\n", name, alloc.ArraySize(), live.size(), alloc.PendingHeld());
			for(const auto& r : live)
				std::printf("L %u %d %d %d\n", r.index, r.key.owner, r.key.id, r.mapped ? 1 : 0);
			std::printf("F");
			for(uint32_t i : fl) std::printf(" %u", i);
			std::printf("\n");
		}
		else
			goto bad;
		continue;
	bad:
		std::printf("replay: refused line %d: %s", ln, line);
		std::fclose(f);
		return 2;
	}
	std::fclose(f);
	const SeedCounters c = alloc.Counters();
	std::printf("counters created %llu freed %llu unknownRemoval %llu knownCreation %llu ambiguous %llu outOfStep %llu "
	            "held %llu loadClears %llu loadRemovals %llu eraRecreated %llu eraFreedOnly %llu embarkSwaps %llu paradrop %llu "
	            "combatLate %llu lateCreates %llu\n",
	            (unsigned long long)c.created, (unsigned long long)c.freed, (unsigned long long)c.unknownRemoval,
	            (unsigned long long)c.knownCreation, (unsigned long long)c.combatHoldAmbiguous,
	            (unsigned long long)c.outOfStepEpisodes, (unsigned long long)c.heldRemovals,
	            (unsigned long long)c.loadClears, (unsigned long long)c.loadRemovals, (unsigned long long)c.eraRecreated,
	            (unsigned long long)c.eraFreedOnly, (unsigned long long)c.embarkSwaps, (unsigned long long)c.paradropFrees,
	            (unsigned long long)c.combatLateFrees, (unsigned long long)c.lateCreates);
	if(xChecks)
	{
		std::printf("X total checks %llu units %llu equal %llu\n", xChecks, xUnits, xEqual);
		return xEqual == xUnits ? 0 : 1;
	}
	return 0;
}

int main(int argc, char** argv)
{
	if(argc == 3 && std::strcmp(argv[1], "--replay") == 0)
		return Replay(argv[2]);
	if(argc != 1)
	{
		std::printf("usage: test_units_seed [--replay FILE]\n");
		return 2;
	}
	TestFreeRule();
	TestAppendAndLiveList();
	TestCatalogue();
	TestLoadClear();
	TestEmbark();
	TestDeathKinds();
	TestUpgradeCapture();
	TestEraChange();
	TestEraWindowNegative();
	TestEraEmbarkedGrouped();
	TestCombatHold();
	TestFrameOrder();
	TestEraAtOnce();
	TestParadrop();
	TestCombatDeathLate();
	TestStackedLate();
	TestBandPerClass();
	TestRestore();
	TestStatuses();
	std::printf("test_units_seed: %d passed, %d failed\n", g_passed, g_failed);
	return g_failed ? 1 : 0;
}
