// test_feed.cpp - unit tests of the renderer's event feed (server/src/feed/srv_feed.cpp; issue 3303, 2026-11-13).
// Built and run by server/tests/feed/run_feed_test.py (clang-cl, and once with MSVC /fsanitize=address).
// Exit 0 when every check passed, 1 otherwise; every check prints one line. -DFEED_TEST_INJECT_FAIL adds one failing
// check (the runner's negative test: the binary must then exit 1).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <set>
#include <thread>
#include <atomic>
#include <stddef.h>
#include "../../src/feed/srv_feed.h"
#include "../../src/feed/srv_feed_server.h"
#include "../../src/feed/srv_feed_dbcopy.h"
#include "../../../render/include/c5r/units/unit_scene.h"
#include <map>
#include <string>

using namespace SrvFeed;

static int g_pass = 0, g_fail = 0;
static const char* g_test = "";

#define CHECK(cond)                                                                                                   \
	do                                                                                                                \
	{                                                                                                                 \
		if (cond) ++g_pass;                                                                                           \
		else { ++g_fail; printf("FAIL %s: %s (line %d)\n", g_test, #cond, __LINE__); }                                \
	} while (0)

static Event Ev(uint16_t kind, int32_t plot, int32_t a0 = 0)
{
	Event e = {};
	e.kind = kind; e.plot = plot; e.turn = 7; e.a[0] = a0;
	return e;
}

static void Fresh(bool enable = true)
{
	ResetForTest();
	SetEnabled(enable);
}

static PlotRecord GetterValues(int8_t terrain, int8_t owner)
{
	PlotRecord v = EmptyRecord();
	v.terrain = terrain; v.plotType = 2; v.feature = -1; v.improvement = 4; v.owner = owner; v.flags = 1; v.fow = 3;
	return v;
}

static PlotRecord LayoutValues(int16_t resource, int16_t improvement, int8_t impState, int8_t route, int8_t routeState)
{
	PlotRecord v = EmptyRecord();
	v.resource = resource; v.improvement = improvement; v.improvementState = impState; v.route = route;
	v.routeState = routeState;
	v.flags = (uint8_t)((impState == 3 ? 2 : 0) | (routeState == 2 || routeState == 3 ? 4 : 0));
	return v;
}

static void TestLayout()
{
	g_test = "layout";
	CHECK(sizeof(Event) == 32);
	CHECK(sizeof(PlotRecord) == 20);
	PlotRecord e = EmptyRecord();
	CHECK(e.terrain == -1 && e.resource == -1 && e.improvementState == -1 && e.routeState == -1 && e.known == 0);
}

static void TestDisabled()
{
	g_test = "disabled";
	Fresh(false);
	CHECK(!Enabled());
	Push(Ev(kPlotState, 1));
	BeginSnapshot(4, 4);
	SetPlot(0, EmptyRecord());
	CHECK(!UpdatePlot(0, kFGetterAll, GetterValues(1, 2)));
	EndSnapshot();
	Batch b;
	CHECK(!Drain(b));
	CHECK(b.events.empty() && b.changedPlots.empty() && b.snapshotGen == 0 && b.width == 0);
	std::vector<PlotRecord> t; uint32_t g = 9, w = 9, h = 9;
	CopyPlots(t, g, w, h);
	CHECK(t.empty() && g == 0 && w == 0 && h == 0);
	Counters c = GetCounters();
	CHECK(c.pushed == 0 && c.dropped == 0 && c.snapshots == 0 && c.plotSets == 0);
	// enabling afterwards works from a clean state
	SetEnabled(true);
	Push(Ev(kPlotState, 1));
	CHECK(Drain(b) && b.events.size() == 1 && b.events[0].seq == 1);
}

static void TestOrder()
{
	g_test = "order";
	Fresh();
	for (int i = 0; i < 100; ++i) Push(Ev((uint16_t)(1 + i % 10), i, i));
	Batch b;
	CHECK(Drain(b));
	CHECK(b.events.size() == 100);
	bool ok = true;
	for (size_t i = 0; i < b.events.size(); ++i)
		ok = ok && b.events[i].seq == i + 1 && b.events[i].plot == (int32_t)i && b.events[i].a[0] == (int32_t)i &&
		     (size_t)b.events[i].kind == 1 + i % 10 && b.events[i].turn == 7 && b.events[i].flags == 0;
	CHECK(ok);
	CHECK(!Drain(b));                      // nothing new
	CHECK(b.events.empty());
	Push(Ev(kWallCreated, 3));
	CHECK(Drain(b) && b.events.size() == 1 && b.events[0].seq == 101);
	Counters2 c2 = GetCounters2();
	CHECK(c2.perKind[kPlotState] == 10 && c2.perKind[kWallCreated] == 11);
	CHECK(GetCounters().drains == 3);
}

static void TestOverflow()
{
	g_test = "overflow";
	Fresh();
	SetQueueCap(10);
	for (int i = 0; i < 15; ++i) Push(Ev(kFowChanged, i));
	Batch b;
	CHECK(Drain(b));
	CHECK(b.events.size() == 11);
	CHECK(b.events.back().kind == kOverflow && b.events.back().a[0] == 5 && b.events.back().plot == -1);
	CHECK(b.events[9].seq == 10 && b.events.back().seq == 11);
	CHECK(b.dropped == 5 && GetCounters().dropped == 5);
	Push(Ev(kFowChanged, 1));
	CHECK(Drain(b) && b.events.size() == 1 && b.events[0].kind == kFowChanged && b.events[0].seq == 12);
	CHECK(b.dropped == 5);                 // cumulative
	// cap 0 is taken as 1 (negative: never an empty queue that drops everything)
	SetQueueCap(0);
	Push(Ev(kFowChanged, 1));
	Push(Ev(kFowChanged, 2));
	CHECK(Drain(b) && b.events.size() == 2 && b.events[0].plot == 1 && b.events[1].kind == kOverflow);
}

static void TestSnapshot()
{
	g_test = "snapshot";
	Fresh();
	std::vector<PlotRecord> t; uint32_t g = 0, w = 0, h = 0;
	BeginSnapshot(4, 3);
	for (uint32_t i = 0; i < 12; ++i) UpdatePlot(i, kFGetterAll, GetterValues((int8_t)i, 1));
	CopyPlots(t, g, w, h);
	CHECK(t.empty() && w == 0 && g == 0);   // not published before EndSnapshot
	PlotRecord r;
	CHECK(!GetPlot(0, r));
	EndSnapshot();
	CopyPlots(t, g, w, h);
	CHECK(t.size() == 12 && g == 1 && w == 4 && h == 3);
	// known 1 | 4: the getter fields and (ticket 0631) the river edges were read
	CHECK(t[5].terrain == 5 && t[5].owner == 1 && t[5].known == 5 && t[5].improvement == 4 && t[5].changeGen == 1);
	Batch b;
	CHECK(Drain(b));
	CHECK(b.events.size() == 1 && b.events[0].kind == kSnapshot && b.events[0].a[0] == 4 && b.events[0].a[1] == 3 &&
	      b.events[0].a[2] == 1);
	CHECK(b.changedPlots.empty() && b.snapshotGen == 1 && b.width == 4 && b.height == 3);
	CHECK(GetCounters().snapshots == 1);
	// EndSnapshot without BeginSnapshot: nothing
	EndSnapshot();
	CHECK(!Drain(b) && GetCounters().snapshots == 1);
}

static void TestPatches()
{
	g_test = "patches";
	Fresh();
	BeginSnapshot(4, 3);
	for (uint32_t i = 0; i < 12; ++i) UpdatePlot(i, kFGetterAll, GetterValues(1, 1));
	EndSnapshot();
	Batch b;
	Drain(b);
	PlotRecord v = EmptyRecord();
	v.owner = 3;
	CHECK(UpdatePlot(5, kFOwner, v));
	CHECK(UpdatePlot(2, kFOwner, v));
	v.owner = 4;
	CHECK(UpdatePlot(5, kFOwner, v));      // the same plot twice: listed once
	CHECK(Drain(b));
	CHECK(b.changedPlots.size() == 2 && b.changedPlots[0] == 5 && b.changedPlots[1] == 2);
	PlotRecord r;
	CHECK(GetPlot(5, r) && r.owner == 4 && r.terrain == 1 && r.changeGen == 1);
	CHECK(!UpdatePlot(5, kFOwner, v));     // no change: not reported
	CHECK(!Drain(b) && b.changedPlots.empty());
	Counters2 c2 = GetCounters2();
	CHECK(c2.patchesChanged == 3 && c2.patchesSame == 1);
	// a later Drain lists a plot again once it changes again
	v.owner = 5;
	CHECK(UpdatePlot(2, kFOwner, v));
	CHECK(Drain(b) && b.changedPlots.size() == 1 && b.changedPlots[0] == 2);
	// SetPlot after a snapshot replaces and marks
	SetPlot(7, GetterValues(9, 9));
	CHECK(Drain(b) && b.changedPlots.size() == 1 && b.changedPlots[0] == 7);
	CHECK(GetPlot(7, r) && r.terrain == 9);
	// fow and feature fields alone
	v = EmptyRecord(); v.fow = 1;
	CHECK(UpdatePlot(1, kFFow, v));
	CHECK(GetPlot(1, r) && r.fow == 1 && r.terrain == 1 && r.owner == 1);
}

static void TestLayoutBeforeTable()
{
	g_test = "layout-before-table";
	Fresh();
	// layout patches before the first snapshot are kept, getter patches are dropped
	CHECK(!UpdatePlot(7, kFLayoutAll, LayoutValues(12, 5, 3, 1, 3)));
	CHECK(!UpdatePlot(7, kFLayoutAll, LayoutValues(13, 5, 3, 1, 3)));   // the newer one wins
	CHECK(!UpdatePlot(8, kFGetterAll, GetterValues(1, 1)));
	CHECK(!UpdatePlot(99, kFLayoutAll, LayoutValues(1, 1, 2, 0, 0)));   // outside the coming table
	Counters2 c2 = GetCounters2();
	CHECK(c2.pendingKept == 3);
	BeginSnapshot(4, 3);
	for (uint32_t i = 0; i < 12; ++i) UpdatePlot(i, kFGetterAll, GetterValues(1, 1));
	EndSnapshot();
	PlotRecord r;
	CHECK(GetPlot(7, r));
	CHECK(r.resource == 13 && r.improvement == 5 && r.improvementState == 3 && r.route == 1 && r.routeState == 3);
	CHECK((r.flags & 2) && (r.flags & 4) && (r.flags & 1) && r.known == 7 && r.terrain == 1);   // 3 | 4 (0631's edges)
	CHECK(GetPlot(8, r) && r.resource == -1 && r.known == 5 && r.improvement == 4);
	c2 = GetCounters2();
	CHECK(c2.pendingApplied == 1 && c2.pendingDropped == 1);
}

static void TestCarryOver()
{
	g_test = "carry-over";
	Fresh();
	BeginSnapshot(4, 3);
	for (uint32_t i = 0; i < 12; ++i) UpdatePlot(i, kFGetterAll, GetterValues(1, 1));
	EndSnapshot();
	CHECK(UpdatePlot(3, kFLayoutAll, LayoutValues(20, 6, 2, 0, 6)));
	PlotRecord r;
	CHECK(GetPlot(3, r) && r.improvement == 6 && r.known == 7);
	// a getter refresh does not overwrite the revealed improvement once a layout was seen
	PlotRecord g = GetterValues(2, 1);
	g.improvement = 9;
	CHECK(UpdatePlot(3, kFGetterAll, g));          // terrain 1 -> 2
	CHECK(GetPlot(3, r) && r.improvement == 6 && r.terrain == 2);
	// ... but does on a plot without one
	CHECK(UpdatePlot(4, kFGetterAll, g));
	CHECK(GetPlot(4, r) && r.improvement == 9);
	// a new snapshot of the same grid keeps the layout fields
	BeginSnapshot(4, 3);
	for (uint32_t i = 0; i < 12; ++i) UpdatePlot(i, kFGetterAll, GetterValues(1, 1));
	// events during the snapshot go to the table being built; the published one is unchanged
	CHECK(GetPlot(3, r) && r.terrain == 2);
	EndSnapshot();
	CHECK(GetPlot(3, r) && r.resource == 20 && r.improvement == 6 && r.routeState == 6 && r.known == 7 && r.terrain == 1);
	CHECK(GetPlot(4, r) && r.improvement == 4 && r.resource == -1);
	// another grid: nothing is carried
	BeginSnapshot(5, 3);
	for (uint32_t i = 0; i < 15; ++i) UpdatePlot(i, kFGetterAll, GetterValues(1, 1));
	EndSnapshot();
	CHECK(GetPlot(3, r) && r.resource == -1 && r.known == 5);
	std::vector<PlotRecord> t; uint32_t gen = 0, w = 0, h = 0;
	CopyPlots(t, gen, w, h);
	CHECK(t.size() == 15 && gen == 3 && w == 5 && h == 3);
}

static void TestBadIndex()
{
	g_test = "bad-index";
	Fresh();
	BeginSnapshot(2, 2);
	EndSnapshot();
	CHECK(!UpdatePlot(4, kFOwner, GetterValues(1, 1)));
	CHECK(!UpdatePlot(0xffffffffu, kFOwner, GetterValues(1, 1)));
	SetPlot(4, EmptyRecord());
	Counters2 c2 = GetCounters2();
	CHECK(c2.badIndex == 3);
	PlotRecord r;
	CHECK(!GetPlot(4, r));
	Batch b;
	Drain(b);
	CHECK(b.changedPlots.empty());
	// an empty grid: a table of no plots counts as no table (getter patches dropped, nothing counted as bad)
	BeginSnapshot(0, 0);
	EndSnapshot();
	CHECK(!UpdatePlot(0, kFOwner, GetterValues(1, 1)));
	CHECK(GetCounters2().badIndex == 3);
	std::vector<PlotRecord> t; uint32_t gen = 0, w = 9, h = 9;
	CopyPlots(t, gen, w, h);
	CHECK(t.empty() && w == 0 && h == 0 && gen == 2);
	// mask 0 changes nothing
	BeginSnapshot(2, 2);
	EndSnapshot();
	CHECK(!UpdatePlot(0, 0, GetterValues(1, 1)));
}

static void TestDisableMidway()
{
	g_test = "disable-midway";
	Fresh();
	BeginSnapshot(2, 2);
	EndSnapshot();
	Batch b;
	Drain(b);
	SetEnabled(false);
	CHECK(!UpdatePlot(0, kFOwner, GetterValues(1, 3)));
	Push(Ev(kPlotState, 0));
	CHECK(!Drain(b) && b.events.empty() && b.snapshotGen == 1 && b.width == 2);   // reads still work
	PlotRecord r;
	CHECK(GetPlot(0, r) && r.owner == -1);
}

static const int kThreads = 4, kEach = 20000;   // file scope: the lambdas need no capture (MSVC C3493)

static void TestThreads()
{
	g_test = "threads";
	Fresh();
	SetQueueCap(1u << 20);
	std::atomic<int> done(0);
	std::vector<Event> all;
	std::vector<std::thread> ws;
	for (int t = 0; t < kThreads; ++t)
		ws.emplace_back([t, &done]() {
			for (int i = 0; i < kEach; ++i)
			{
				Event e = {};
				e.kind = kYieldChanged; e.plot = t; e.a[0] = i;
				Push(e);
			}
			++done;
		});
	Batch b;
	size_t drains = 0;
	for (;;)
	{
		const bool finished = done.load() == kThreads;
		if (Drain(b)) { all.insert(all.end(), b.events.begin(), b.events.end()); ++drains; }
		if (finished && b.events.empty()) break;
	}
	for (auto& w : ws) w.join();
	CHECK(all.size() == (size_t)kThreads * kEach);
	bool seqOk = true, perThreadOk = true;
	std::vector<int> next(kThreads, 0);
	for (size_t i = 0; i < all.size(); ++i)
	{
		seqOk = seqOk && all[i].seq == i + 1;
		const int t = all[i].plot;
		perThreadOk = perThreadOk && t >= 0 && t < kThreads && all[i].a[0] == next[t];
		if (t >= 0 && t < kThreads) ++next[t];
	}
	CHECK(seqOk);
	CHECK(perThreadOk);
	CHECK(GetCounters().dropped == 0);
	printf("info threads: %zu events in %zu drains\n", all.size(), drains);
}

// Issue 4305: PlotRecord::artType (the old reserved1) from the pure getter GetContinentType, at the snapshot and at every
// refresh (kFGetterAll carries kFArtType); every art type 0..4 kept, a value outside refused to 0 (ArtTypeFromDll).
static void TestArtType()
{
	g_test = "art-type";
	CHECK(offsetof(PlotRecord, artType) == 19 && offsetof(PlotRecord, known) == 18);
	CHECK(EmptyRecord().artType == 0);
	bool out = true;
	for (int a = 0; a <= 4; ++a)
	{
		CHECK(ArtTypeFromDll(a, &out) == a && !out);
	}
	const int bad[] = {-1, -128, 5, 31, 127, 255, 1000};
	for (int v : bad)
	{
		out = false;
		CHECK(ArtTypeFromDll(v, &out) == 0 && out);
	}
	CHECK(ArtTypeFromDll(3) == 3 && ArtTypeFromDll(-7) == 0);   // without the flag
	CHECK((kFGetterAll & kFArtType) != 0 && (kFLayoutAll & kFArtType) == 0);
	Fresh();
	// the snapshot: plot i gets art type i % 5 (every type, a non-Generic plot among them)
	BeginSnapshot(5, 2);
	for (uint32_t i = 0; i < 10; ++i)
	{
		PlotRecord v = GetterValues(1, 1);
		v.artType = ArtTypeFromDll((int)(i % 5));
		UpdatePlot(i, kFGetterAll, v);
	}
	EndSnapshot();
	std::vector<PlotRecord> t; uint32_t g = 0, w = 0, h = 0;
	CopyPlots(t, g, w, h);
	CHECK(t.size() == 10 && w == 5 && h == 2);
	bool all = t.size() == 10;
	for (uint32_t i = 0; i < t.size(); ++i) all = all && t[i].artType == i % 5 && (t[i].known & 1);
	CHECK(all);
	PlotRecord r;
	CHECK(GetPlot(3, r) && r.artType == 3 && r.terrain == 1);   // Africa arrives as Africa
	Batch b;
	Drain(b);
	// the refresh: a farm turns America (1) into Africa (3) (VP's CvPlot::setImprovementType): changed, listed
	PlotRecord v = GetterValues(1, 1);
	v.artType = 3;
	CHECK(UpdatePlot(1, kFGetterAll, v));
	CHECK(Drain(b) && b.changedPlots.size() == 1 && b.changedPlots[0] == 1);
	CHECK(GetPlot(1, r) && r.artType == 3 && r.changeGen == 1);
	CHECK(!UpdatePlot(1, kFGetterAll, v));   // the same value again: no change
	CHECK(!Drain(b));
	// an outside value read by the refresh is stored as 0
	v.artType = ArtTypeFromDll(9);
	CHECK(UpdatePlot(4, kFGetterAll, v));
	CHECK(GetPlot(4, r) && r.artType == 0);
	// the art type alone; a layout patch leaves it
	v = EmptyRecord(); v.artType = 2;
	CHECK(UpdatePlot(0, kFArtType, v));
	CHECK(GetPlot(0, r) && r.artType == 2 && r.terrain == 1 && r.owner == 1);
	CHECK(UpdatePlot(0, kFLayoutAll, LayoutValues(5, 6, 2, 0, 0)));
	CHECK(GetPlot(0, r) && r.artType == 2 && r.resource == 5);
	// a new snapshot of the same grid reads the getters again: the art type is not carried over, it is re-read
	BeginSnapshot(5, 2);
	for (uint32_t i = 0; i < 10; ++i) UpdatePlot(i, kFGetterAll, GetterValues(1, 1));   // art type 0 everywhere
	EndSnapshot();
	CHECK(GetPlot(0, r) && r.artType == 0 && r.resource == 5);   // layout carried, art type as read
	CHECK(GetPlot(3, r) && r.artType == 0);
}

// Ticket 0631: PlotRecord::riverEdges (the old reserved0, offset 11) from the pure getters Is{W,NW,NE}OfRiver, with known
// bit 2, at the snapshot and at every refresh (kFGetterAll carries kFRiverEdges); only the three edge bits are kept.
static void TestRiverEdges()
{
	g_test = "river-edges";
	CHECK(sizeof(PlotRecord) == 20 && offsetof(PlotRecord, riverEdges) == 11 && offsetof(PlotRecord, fow) == 10 &&
	      offsetof(PlotRecord, changeGen) == 12);
	CHECK(EmptyRecord().riverEdges == 0 && (EmptyRecord().known & kPlotKnownRiverEdges) == 0);
	CHECK((kFGetterAll & kFRiverEdges) != 0 && (kFLayoutAll & kFRiverEdges) == 0 && kPlotKnownRiverEdges == 4);
	Fresh();
	BeginSnapshot(4, 2);
	for (uint32_t i = 0; i < 8; ++i)
	{
		PlotRecord v = GetterValues(1, 1);
		v.riverEdges = (uint8_t)(i % 8);   // every combination of the three bits
		UpdatePlot(i, kFGetterAll, v);
	}
	EndSnapshot();
	std::vector<PlotRecord> t; uint32_t g = 0, w = 0, h = 0;
	CopyPlots(t, g, w, h);
	bool all = t.size() == 8;
	for (uint32_t i = 0; i < t.size(); ++i) all = all && t[i].riverEdges == i % 8 && (t[i].known & kPlotKnownRiverEdges);
	CHECK(all);
	Batch b;
	Drain(b);
	// the refresh: the edges alone change (a river laid by a mod's SetWOfRiver): changed and listed
	PlotRecord v = GetterValues(1, 1);
	v.riverEdges = 5;
	CHECK(UpdatePlot(2, kFGetterAll, v));
	CHECK(Drain(b) && b.changedPlots.size() == 1 && b.changedPlots[0] == 2);
	PlotRecord r;
	CHECK(GetPlot(2, r) && r.riverEdges == 5 && (r.flags & 1));
	CHECK(!UpdatePlot(2, kFGetterAll, v));   // the same again: no change
	// the edges cleared, the any-edge flag kept (the DLL's erase on a land/water change, live-terrain.md 8.18)
	v.riverEdges = 0;
	CHECK(UpdatePlot(2, kFGetterAll, v) && GetPlot(2, r) && r.riverEdges == 0 && (r.flags & 1));
	// bits above the three are dropped
	v.riverEdges = 0xF9;
	CHECK(UpdatePlot(3, kFRiverEdges, v) && GetPlot(3, r) && r.riverEdges == 1);
	// a layout patch and an owner patch leave the edges and known bit 2 as they are
	CHECK(UpdatePlot(3, kFLayoutAll, LayoutValues(5, 6, 2, 0, 0)));
	v = EmptyRecord(); v.owner = 7;
	CHECK(UpdatePlot(3, kFOwner, v));
	CHECK(GetPlot(3, r) && r.riverEdges == 1 && (r.known & kPlotKnownRiverEdges) && r.owner == 7);
	// a patch without kFRiverEdges on a fresh table sets no known bit 2 (a reader then takes the old rule)
	Fresh();
	BeginSnapshot(2, 1);
	PlotRecord nv = GetterValues(1, 1);
	nv.riverEdges = 3;
	UpdatePlot(0, kFGetterAll & ~kFRiverEdges, nv);
	EndSnapshot();
	CHECK(GetPlot(0, r) && r.riverEdges == 0 && (r.known & kPlotKnownRiverEdges) == 0 && (r.known & 1));
}

// ======================================================================================================================
// Issue 4452 (epic 0375): the unit table, the player table, the side tables, the database copy.

static UnitRecord Unit(int owner, int32_t id, int16_t damage = 0, uint8_t flags = 0)
{
	UnitRecord r = {};
	r.owner = (int8_t)owner; r.id = id; r.unitType = 7; r.plot = 33; r.damage = damage; r.maxHP = 100;
	r.domain = 2; r.facing = 1; r.flags = flags; r.artStyle = 3; r.era = 1;
	return r;
}

static Event UEv(uint16_t kind, int owner, int32_t id, int32_t a2 = 0, int32_t a3 = 0)
{
	Event e = {};
	e.kind = kind; e.plot = 33; e.turn = 5; e.a[0] = owner; e.a[1] = id; e.a[2] = a2; e.a[3] = a3;
	return e;
}

static void FreshUnits()
{
	Fresh();
	SetUnitsEnabled(true);
}

static bool Rec(int owner, int32_t id, UnitRecord& r) { return GetUnit(owner, id, r); }

static void TestUnitLayout()
{
	g_test = "unit-layout";
	CHECK(sizeof(UnitRecord) == 32 && sizeof(PlayerRecord) == 32 && sizeof(CombatRecord) == 64 &&
	      sizeof(CombatMember) == 28 && sizeof(PathEntry) == 12);
	CHECK(offsetof(PlayerRecord, reserved) == 28 && offsetof(PlayerRecord, state) == 28);
	CHECK(offsetof(CombatRecord, returned) == 56 && offsetof(CombatRecord, nMembers) == 60 &&
	      offsetof(CombatRecord, firstMember) == 40 && offsetof(CombatRecord, finalDamage) == 48);
	// the flag bits equal 0041's UnitFlag (render/include/c5r/units/unit_scene.h)
	const int ours[] = {kUnitFlagEmbarked, kUnitFlagGarrisoned, kUnitFlagFortified, kUnitFlagVisible, kUnitFlagInvisible,
	                    kUnitFlagDelayedDeath, kUnitFlagDead};
	const int theirs[] = {c5r::units::kFlagEmbarked, c5r::units::kFlagGarrisoned, c5r::units::kFlagFortified,
	                      c5r::units::kFlagVisible, c5r::units::kFlagInvisible, c5r::units::kFlagDelayedDeath,
	                      c5r::units::kFlagDead};
	CHECK(memcmp(ours, theirs, sizeof(ours)) == 0);
	CHECK(kUnitCreated == 11 && kUnitResetAnimationState == 31 && kLoadBegin == 32 && kEraChanged == 33);
}

// Units off (render_units=0): every unit writer is a no-op, also with the feed on; the plot feed is untouched.
static void TestUnitsOff()
{
	g_test = "units-off";
	Fresh();   // feed on, units off
	CHECK(Enabled() && !UnitsEnabled());
	UnitRecord u = Unit(1, 1001);
	CHECK(PushUnit(UEv(kUnitCreated, 1, 1001, 7), &u, UnitOp::kUpsert) == 0);
	const int32_t plots[] = {1, 2};
	CHECK(PushUnitPath(UEv(kUnitMoved, 1, 1001), &u, UnitOp::kUpsert, plots, 2) == 0);
	CombatRecord c = {};
	CHECK(PushCombat(UEv(kUnitCombat, 1, 1001), c, nullptr, 0, &u, nullptr) == 0);
	PlayerRecord p = {};
	p.player = 1;
	SetPlayer(p);
	CHECK(ResetUnitsAt(0) == 0);
	ResetUnits();
	CHECK(!SetCombatResult(1, 5));
	std::vector<UnitRecord> t; uint32_t gen = 9;
	CopyUnits(t, gen);
	std::vector<PlayerRecord> ps;
	CopyPlayers(ps);
	CHECK(t.empty() && gen == 0 && ps.empty());
	Batch b;
	CHECK(!Drain(b) && b.events.empty() && b.paths.empty() && b.combats.empty() && b.players.empty());
	const Counters2 c2 = GetCounters2();
	CHECK(c2.unitEvents == 0 && c2.unitInserts == 0);
	// units on but the feed off: still nothing
	Fresh(false);
	SetUnitsEnabled(true);
	CHECK(!UnitsEnabled());
	CHECK(PushUnit(UEv(kUnitCreated, 1, 1001, 7), &u, UnitOp::kUpsert) == 0);
	CopyUnits(t, gen);
	CHECK(t.empty());
}

// Kind by kind (11-33): the event goes into the queue unchanged (a[0..3], plot, turn, kind; seq assigned) and the table
// effect of srv_feed.h's PushUnit rules.
static void TestUnitKinds()
{
	g_test = "unit-kinds";
	FreshUnits();
	Batch b;
	UnitRecord r;
	// 11 created: inserted, visible set by the feed, call bits of rec ignored, getter bits kept
	UnitRecord u = Unit(2, 1001, 0, (uint8_t)(kUnitFlagEmbarked | kUnitFlagFortified | kUnitFlagDead));
	uint32_t s = PushUnit(UEv(kUnitCreated, 2, 1001, 7), &u, UnitOp::kUpsert);
	CHECK(s == 1 && Rec(2, 1001, r) && r.seq == 1 && r.unitType == 7 && r.flags == (kUnitFlagEmbarked | kUnitFlagVisible));
	CHECK(Drain(b) && b.events.size() == 1 && b.events[0].kind == kUnitCreated && b.events[0].a[0] == 2 &&
	      b.events[0].a[1] == 1001 && b.events[0].a[2] == 7 && b.events[0].plot == 33 && b.events[0].turn == 5 &&
	      b.events[0].seq == 1);
	// 13 teleported / 16 damage / 18 work / 19 paradrop / 20 activate / 22 garrison / 23 dim / 29 / 31: upsert from the
	// getters (rec), call bits kept
	struct K { uint16_t kind; int32_t a2, a3; } ks[] = {{kUnitTeleported, 40, 0}, {kUnitSetDamage, 30, 10},
		{kUnitWork, 4, 0}, {kUnitParadrop, 0, 0}, {kUnitActivate, 0, 0}, {kUnitGarrison, 1, 0},
		{kUnitShouldDimFlag, 1, 0}, {kUnitMissionEnd, 0, 0}, {kUnitResetAnimationState, 0, 0}, {kUnitRebased, 5, 40}};
	for (const K& k : ks)
	{
		UnitRecord v = Unit(2, 1001, (int16_t)(k.a2 + 1), kUnitFlagGarrisoned);
		v.plot = 40;
		s = PushUnit(UEv(k.kind, 2, 1001, k.a2, k.a3), &v, UnitOp::kUpsert);
		CHECK(s > 0 && Rec(2, 1001, r) && r.seq == s && r.plot == 40 && r.damage == k.a2 + 1 &&
		      r.flags == (kUnitFlagGarrisoned | kUnitFlagVisible));
		CHECK(Drain(b) && b.events.size() == 1 && b.events[0].kind == k.kind && b.events[0].a[2] == k.a2 &&
		      b.events[0].a[3] == k.a3 && b.events[0].seq == s);
	}
	// 17 fortify on / off
	s = PushUnit(UEv(kUnitFortify, 2, 1001, 1), &u, UnitOp::kUpsert);
	CHECK(Rec(2, 1001, r) && (r.flags & kUnitFlagFortified) && r.seq == s);
	PushUnit(UEv(kUnitFortify, 2, 1001, 0), &u, UnitOp::kUpsert);
	CHECK(Rec(2, 1001, r) && !(r.flags & kUnitFlagFortified));
	// 24 visibility: bit 0 of a[2]; check-plot bit and blend in the event only
	float blend = 0.5f; int32_t bits; memcpy(&bits, &blend, 4);
	PushUnit(UEv(kUnitVisibility, 2, 1001, 0 + 2 * 1, bits), &u, UnitOp::kUpsert);
	CHECK(Rec(2, 1001, r) && !(r.flags & kUnitFlagVisible));
	PushUnit(UEv(kUnitVisibility, 2, 1001, 1, bits), &u, UnitOp::kUpsert);
	CHECK(Rec(2, 1001, r) && (r.flags & kUnitFlagVisible));
	CHECK(Drain(b) && b.events.size() == 4 && b.events[2].a[2] == 2 && b.events[2].a[3] == bits);
	// 21 embark of a known key: an update (embarked from the getter)
	UnitRecord e1 = Unit(2, 1001, 0, kUnitFlagEmbarked);
	PushUnit(UEv(kUnitEmbark, 2, 1001, 1), &e1, UnitOp::kUpsert);
	CHECK(Rec(2, 1001, r) && (r.flags & kUnitFlagEmbarked) && (r.flags & kUnitFlagVisible));
	CHECK(GetCounters2().embarkCreations == 0);
	// 15 destroyed in combat: dead, the record stays
	PushUnit(UEv(kUnitDestroyedInCombat, 2, 1001), &u, UnitOp::kUpsert);
	CHECK(Rec(2, 1001, r) && (r.flags & kUnitFlagDead));
	// 14 destroyed: removed
	s = PushUnit(UEv(kUnitDestroyed, 2, 1001), &u, UnitOp::kRemove);
	CHECK(s > 0 && !Rec(2, 1001, r));
	// 12 moved: path side table
	UnitRecord m = Unit(3, 1002);
	PushUnit(UEv(kUnitCreated, 3, 1002, 7), &m, UnitOp::kUpsert);
	const int32_t path[] = {10, 11, 12};
	m.plot = 12;
	s = PushUnitPath(UEv(kUnitMoved, 3, 1002), &m, UnitOp::kUpsert, path, 3);
	CHECK(Rec(3, 1002, r) && r.plot == 12 && r.seq == s);
	// 25 combat, 28 city combat: combat side table
	CombatRecord c = {};
	c.plot = 12; c.attackerOwner = 3; c.attackerUnit = 1002; c.attackerCity = -1; c.defenderOwner = 4;
	c.defenderUnit = -1; c.defenderCity = 9; c.finalDamage[0] = 20; c.finalDamage[1] = 50;
	c.flags = kCombatRanged | kCombatVisualize | kCombatReturnedKnown;   // the returned bit is the feed's: cleared
	const uint32_t sc = PushCombat(UEv(kUnitCombat, 3, 1002, 99, 77), c, nullptr, 0, nullptr, nullptr);
	const uint32_t sc2 = PushCombat(UEv(kCityCombat, 4, 9, 99, 77), c, nullptr, 0, nullptr, nullptr);
	// 26 / 27 city kinds: events only
	PushUnit(UEv(kCitySetDamage, 4, 9, 30, 20), nullptr, UnitOp::kNone);
	PushUnit(UEv(kCityPopulationChanged, 4, 9, 6), nullptr, UnitOp::kNone);
	// 33 era changed: event only (the player row is the writer's)
	Event era = UEv(kEraChanged, 3, 2); era.plot = -1;
	PushUnit(era, nullptr, UnitOp::kNone);
	// 32 load begin
	const uint32_t sl = ResetUnitsAt(0);
	CHECK(Drain(b));
	CHECK(b.paths.size() == 1 && b.paths[0].seq == s && b.paths[0].first == 0 && b.paths[0].length == 3 &&
	      b.pathPlots.size() == 3 && b.pathPlots[2] == 12);
	CHECK(b.combats.size() == 2 && b.combats[0].seq == sc && b.combats[0].kind == kUnitCombat &&
	      b.combats[1].seq == sc2 && b.combats[1].kind == kCityCombat && b.combats[0].defenderCity == 9 &&
	      b.combats[0].finalDamage[1] == 50 && b.combats[0].flags == (kCombatRanged | kCombatVisualize) &&
	      b.combats[0].nMembers == 0 && b.combats[0].returned == 0);
	bool found[34] = {};
	for (const Event& ev : b.events)
	{
		if (ev.kind < 34) found[ev.kind] = true;
		if (ev.kind == kUnitMoved) CHECK(ev.a[2] == 0 && ev.a[3] == 3 && ev.seq == s);
		if (ev.kind == kUnitCombat) CHECK(ev.a[2] == 0 && ev.a[3] == 0 && ev.seq == sc);
		// ticket 5062 (W108): kind 28 keeps the caller's a[3] (the attacking city's plot + 1); kind 25's is zeroed
		if (ev.kind == kCityCombat) CHECK(ev.a[2] == 1 && ev.a[3] == 77 && ev.a[0] == 4 && ev.a[1] == 9);
		if (ev.kind == kCitySetDamage) CHECK(ev.a[2] == 30 && ev.a[3] == 20);
		if (ev.kind == kCityPopulationChanged) CHECK(ev.a[2] == 6);
		if (ev.kind == kEraChanged) CHECK(ev.a[0] == 3 && ev.a[1] == 2 && ev.plot == -1);
		if (ev.kind == kLoadBegin) CHECK(ev.seq == sl && ev.a[0] == 1 && ev.a[1] == 0 && ev.plot == -1 && ev.turn == -1);
	}
	CHECK(found[kUnitMoved] && found[kUnitCombat] && found[kCityCombat] && found[kCitySetDamage] &&
	      found[kCityPopulationChanged] && found[kEraChanged] && found[kLoadBegin]);
	CHECK(b.unitTableGen == 1);
	std::vector<UnitRecord> t; uint32_t gen = 0;
	CopyUnits(t, gen);
	CHECK(t.empty() && gen == 1);   // the load emptied the table
	const Counters2 c2 = GetCounters2();
	CHECK(c2.perKind[kUnitCreated] == 2 && c2.perKind[kUnitMoved] == 1 && c2.perKind[kLoadBegin] == 1 &&
	      c2.perKind[kEraChanged] == 1 && c2.unitResets == 1 && c2.droppedUnit == 0);
}

// The creation rules: events before a creation, the embarked creation, unknown removal, known creation, bad owner,
// patch flags.
static void TestUnitCreationRules()
{
	g_test = "unit-creation";
	FreshUnits();
	UnitRecord r, u = Unit(5, 2001);
	// a unit created in a city: Visibility(0) before Created (4459 F7): issue 4463: ignored as the EXE ignores it (note
	// 12.1 [L]), the record is created visible (was: created hidden)
	PushUnit(UEv(kUnitVisibility, 5, 2001, 0), &u, UnitOp::kUpsert);
	CHECK(!Rec(5, 2001, r));
	PushUnit(UEv(kUnitSetDamage, 5, 2001, 10, 0), &u, UnitOp::kUpsert);   // no call bit: counted, nothing kept
	CHECK(!Rec(5, 2001, r));
	PushUnit(UEv(kUnitCreated, 5, 2001, 7), &u, UnitOp::kUpsert);
	CHECK(Rec(5, 2001, r) && (r.flags & kUnitFlagVisible));
	CHECK(GetCounters2().preCreationEvents == 2);
	// a second creation of a known key (era change): an update, counted
	PushUnit(UEv(kUnitVisibility, 5, 2001, 0), &u, UnitOp::kUpsert);   // after the creation: taken
	PushUnit(UEv(kUnitCreated, 5, 2001, 7), &u, UnitOp::kUpsert);
	CHECK(Rec(5, 2001, r) && !(r.flags & kUnitFlagVisible) && GetCounters2().unitCreateKnown == 1);
	// a unit set up embarked at a load: kUnitEmbark(1) for an unknown key is a creation
	UnitRecord e = Unit(6, 2002, 0, kUnitFlagEmbarked);
	PushUnit(UEv(kUnitEmbark, 6, 2002, 1), &e, UnitOp::kUpsert);
	CHECK(Rec(6, 2002, r) && r.flags == (kUnitFlagEmbarked | kUnitFlagVisible) && GetCounters2().embarkCreations == 1);
	// fortify before creation is kept too
	UnitRecord f = Unit(6, 2003);
	PushUnit(UEv(kUnitFortify, 6, 2003, 1), &f, UnitOp::kUpsert);
	PushUnit(UEv(kUnitCreated, 6, 2003, 7), &f, UnitOp::kUpsert);
	CHECK(Rec(6, 2003, r) && r.flags == (kUnitFlagFortified | kUnitFlagVisible));
	// removal of an unknown key: no change, counted
	const size_t before = (size_t)GetCounters2().unitRemoves;
	UnitRecord x = Unit(7, 9999);
	CHECK(PushUnit(UEv(kUnitDestroyed, 7, 9999), &x, UnitOp::kRemove) > 0);
	CHECK(GetCounters2().unitRemoveUnknown == 1 && GetCounters2().unitRemoves == before);
	// a removal drops the kept pre-creation bits of the key (issue 4463: a kept Fortify; a Visibility is not kept at all)
	UnitRecord y = Unit(7, 9998);
	PushUnit(UEv(kUnitFortify, 7, 9998, 1), &y, UnitOp::kUpsert);
	PushUnit(UEv(kUnitDestroyed, 7, 9998), &y, UnitOp::kRemove);
	PushUnit(UEv(kUnitCreated, 7, 9998, 7), &y, UnitOp::kUpsert);
	CHECK(Rec(7, 9998, r) && (r.flags & kUnitFlagVisible) && !(r.flags & kUnitFlagFortified));
	// owner outside 0..63: the event is pushed, the table untouched
	UnitRecord bad = Unit(-1, 3000), bad2 = Unit(0, 3001);
	bad2.owner = 64;
	CHECK(PushUnit(UEv(kUnitCreated, -1, 3000, 7), &bad, UnitOp::kUpsert) > 0);
	CHECK(PushUnit(UEv(kUnitCreated, 64, 3001, 7), &bad2, UnitOp::kUpsert) > 0);
	CHECK(!Rec(-1, 3000, r) && GetCounters2().unitBadKey == 2);
	// a NULL record with an op: no change
	CHECK(PushUnit(UEv(kUnitCreated, 1, 1), nullptr, UnitOp::kUpsert) > 0);
	// kPatchFlags: an existing record's flags; unknown key counted
	UnitRecord pf = Unit(5, 2001, 0, kUnitFlagDead);
	PushUnit(UEv(kUnitActivate, 5, 2001), &pf, UnitOp::kPatchFlags);
	CHECK(Rec(5, 2001, r) && r.flags == kUnitFlagDead);
	UnitRecord pu = Unit(5, 2999);
	PushUnit(UEv(kUnitActivate, 5, 2999), &pu, UnitOp::kPatchFlags);
	CHECK(GetCounters2().unitPatchUnknown == 1 && !Rec(5, 2999, r));
	// CopyUnits is sorted by (owner, id) (issue 4463: the copy of the last Drain)
	std::vector<UnitRecord> t; uint32_t g = 0;
	Batch db;
	Drain(db);
	CopyUnits(t, g);
	bool sorted = !t.empty();
	for (size_t i = 1; i < t.size(); ++i)
		sorted = sorted && (t[i - 1].owner < t[i].owner || (t[i - 1].owner == t[i].owner && t[i - 1].id < t[i].id));
	CHECK(sorted && t.size() == 4);
	// a pre-creation table that is full: counted, nothing kept
}

// Paths: index, kept length, the 65,536-plot cut, a full side table (event dropped, table effect applied, kOverflow).
static void TestUnitPaths()
{
	g_test = "unit-paths";
	FreshUnits();
	UnitRecord u = Unit(1, 1001), r;
	PushUnit(UEv(kUnitCreated, 1, 1001, 7), &u, UnitOp::kUpsert);
	std::vector<int32_t> big(70000);
	for (size_t i = 0; i < big.size(); ++i) big[i] = (int32_t)i;
	const uint32_t s1 = PushUnitPath(UEv(kUnitMoved, 1, 1001), &u, UnitOp::kUpsert, big.data(), (uint32_t)big.size());
	const uint32_t s2 = PushUnitPath(UEv(kUnitMoved, 1, 1001), &u, UnitOp::kUpsert, nullptr, 0);   // empty path
	Batch b;
	CHECK(Drain(b) && b.paths.size() == 2 && b.paths[0].length == 65536 && b.paths[0].seq == s1 &&
	      b.pathPlots.size() == 65536 && b.pathPlots[65535] == 65535 && b.paths[1].length == 0 && b.paths[1].seq == s2);
	CHECK(b.events.size() == 3 && b.events[1].a[2] == 0 && b.events[1].a[3] == 70000 && b.events[2].a[2] == 1);
	// the next batch starts its indices at 0 again
	PushUnitPath(UEv(kUnitMoved, 1, 1001), &u, UnitOp::kUpsert, big.data(), 2);
	CHECK(Drain(b) && b.paths.size() == 1 && b.events[0].a[2] == 0 && b.paths[0].first == 0);
	// a full side table: the event is dropped (a[2] -1 never reaches a batch), the record still changes, kOverflow next
	SetSideCapsForTest(4, 0, 0);
	PushUnitPath(UEv(kUnitMoved, 1, 1001), &u, UnitOp::kUpsert, big.data(), 3);   // fits (3 <= 4)
	UnitRecord moved = Unit(1, 1001); moved.plot = 77;
	CHECK(PushUnitPath(UEv(kUnitMoved, 1, 1001), &moved, UnitOp::kUpsert, big.data(), 3) == 0);   // 6 > 4
	CHECK(Rec(1, 1001, r) && r.plot == 77 && r.seq == 0);
	CHECK(Drain(b) && b.paths.size() == 1 && b.events.size() == 2 && b.events[1].kind == kOverflow &&
	      b.events[1].a[0] == 1);
	const Counters2 c2 = GetCounters2();
	CHECK(c2.sideTableFull == 1 && c2.droppedUnit == 1 && c2.pathsKept == 4);
	SetSideCapsForTest(0, 0, 0);
}

// Combats: the record and its members, the attacker's and defender's records, the returned value (in time and late),
// full side tables.
static void TestUnitCombats()
{
	g_test = "unit-combats";
	FreshUnits();
	UnitRecord att = Unit(1, 1001), def = Unit(2, 1002), r;
	PushUnit(UEv(kUnitCreated, 1, 1001, 7), &att, UnitOp::kUpsert);
	PushUnit(UEv(kUnitCreated, 2, 1002, 7), &def, UnitOp::kUpsert);
	CombatMember m[3] = {};
	for (int i = 0; i < 3; ++i) { m[i].player = 2; m[i].id = 1002 + i; m[i].damage = (int16_t)(10 * i); m[i].era = 1; }
	CombatRecord c = {};
	c.attackerOwner = 1; c.attackerUnit = 1001; c.attackerCity = -1;
	c.defenderOwner = 2; c.defenderUnit = 1002; c.defenderCity = -1; c.nuclearLevel = 0; c.plot = 50;
	UnitRecord att2 = Unit(1, 1001, 15), def2 = Unit(2, 1002, 40);
	const uint32_t s = PushCombat(UEv(kUnitCombat, 1, 1001), c, m, 3, &att2, &def2);
	CHECK(s > 0 && Rec(1, 1001, r) && r.damage == 15 && r.seq == s && Rec(2, 1002, r) && r.damage == 40 && r.seq == s);
	CHECK(SetCombatResult(s, 42));
	const uint32_t s2 = PushCombat(UEv(kUnitCombat, 1, 1001), c, m, 1, nullptr, nullptr);
	Batch b;
	CHECK(Drain(b) && b.combats.size() == 2 && b.combatMembers.size() == 4);
	CHECK(b.combats[0].returned == 42 && (b.combats[0].flags & kCombatReturnedKnown) && b.combats[0].firstMember == 0 &&
	      b.combats[0].nMembers == 3 && b.combats[1].firstMember == 3 && b.combats[1].nMembers == 1 &&
	      b.combatMembers[2].id == 1004 && b.combatMembers[2].damage == 20 && b.combats[0].plot == 50);
	CHECK(!SetCombatResult(s2, 7) && !(b.combats[1].flags & kCombatReturnedKnown));   // drained before the return
	CHECK(GetCounters2().combatResultsLate == 1 && GetCounters2().combatResultsSet == 1);
	CHECK(!SetCombatResult(0, 1));   // seq 0 (a dropped event): nothing
	// issue 4783: the late result goes with the next Drain (Batch::lateResults), once
	CHECK(Drain(b) && b.combats.empty() && b.lateResults.size() == 1 && b.lateResults[0].seq == s2 &&
	      b.lateResults[0].returned == 7 && b.lateResultsLost == 0);
	CHECK(!Drain(b) && b.lateResults.empty());
	// the negative: with the ring off (the feed before 4783) the late result is counted and lost
	SetLateResultsKept(false);
	const uint32_t s3 = PushCombat(UEv(kUnitCombat, 1, 1001), c, m, 1, nullptr, nullptr);
	CHECK(Drain(b) && b.combats.size() == 1 && b.combats[0].seq == s3);
	CHECK(!SetCombatResult(s3, 8));
	CHECK(!Drain(b) && b.lateResults.empty() && GetCounters2().combatResultsLate == 2);
	SetLateResultsKept(true);
	// the ring holds 256: the oldest is dropped and counted
	for (uint32_t k = 0; k < 257; ++k) SetCombatResult(100000 + k, 9000 + k);
	CHECK(Drain(b) && b.lateResults.size() == 256 && b.lateResults[0].seq == 100001 && b.lateResults[255].seq == 100256 &&
	      b.lateResultsLost == 1);
	// full side tables
	SetSideCapsForTest(0, 1, 2);
	CHECK(PushCombat(UEv(kUnitCombat, 1, 1001), c, m, 2, nullptr, nullptr) > 0);
	CHECK(PushCombat(UEv(kUnitCombat, 1, 1001), c, m, 1, &att, nullptr) == 0);   // a second combat: cap 1
	CHECK(Rec(1, 1001, r) && r.damage == 0 && r.seq == 0);   // the attacker's record still refreshed
	CHECK(Drain(b) && b.combats.size() == 1 && b.events.back().kind == kOverflow);
	SetSideCapsForTest(0, 4, 2);
	CHECK(PushCombat(UEv(kUnitCombat, 1, 1001), c, m, 3, nullptr, nullptr) == 0);   // 3 members > 2
	CHECK(GetCounters2().sideTableFull == 2);
	SetSideCapsForTest(0, 0, 0);
}

// Player rows: publication on change only, one row per player per batch, bad index refused, CopyPlayers.
static void TestPlayers()
{
	g_test = "players";
	FreshUnits();
	PlayerRecord p = {};
	p.player = 3; p.team = 3; p.alive = 1; p.colour = 12; p.civilization = 20; p.era = 1; p.artStyle = 4;
	p.activeTeam = 0; p.state = 1;
	SetPlayer(p);
	Batch b;
	CHECK(Drain(b) && b.players.size() == 1 && b.players[0].changeGen == 1 && b.players[0].colour == 12 &&
	      b.players[0].state == 1);
	SetPlayer(p);   // the same row: not published
	CHECK(!Drain(b) && b.players.empty());
	p.changeGen = 77;   // changeGen is the feed's
	SetPlayer(p);
	CHECK(!Drain(b));
	p.era = 2;
	SetPlayer(p);
	p.era = 3;
	SetPlayer(p);   // two changes in one batch: one row, the newest
	PlayerRecord q = p;
	q.player = 0;
	SetPlayer(q);
	CHECK(Drain(b) && b.players.size() == 2 && b.players[0].era == 3 && b.players[0].changeGen == 3 &&
	      b.players[1].player == 0);
	PlayerRecord bad = p;
	bad.player = -1;
	SetPlayer(bad);
	bad.player = 64;   // int8: 64 fits
	SetPlayer(bad);
	CHECK(GetCounters2().playerBadIndex == 2 && GetCounters2().playerRowsSame == 2);
	std::vector<PlayerRecord> ps;
	CopyPlayers(ps);
	CHECK(ps.size() == 2 && ps[0].player == 0 && ps[1].player == 3 && ps[1].era == 3);
	PlayerRecord got;
	CHECK(GetPlayerRow(3, got) && got.era == 3 && !GetPlayerRow(5, got) && !GetPlayerRow(64, got));
	// a load does not drop the player rows (they are re-read at the snapshot point)
	ResetUnits();
	CopyPlayers(ps);
	CHECK(ps.size() == 2);
}

// The queue cap with unit events: the table changes anyway (seq 0), droppedUnit counted, kOverflow at the next Drain.
static void TestUnitOverflow()
{
	g_test = "unit-overflow";
	FreshUnits();
	SetQueueCap(2);
	UnitRecord r;
	for (int i = 0; i < 5; ++i)
	{
		UnitRecord u = Unit(1, 1000 + i);
		PushUnit(UEv(kUnitCreated, 1, 1000 + i, 7), &u, UnitOp::kUpsert);
	}
	CHECK(Rec(1, 1004, r) && r.seq == 0 && Rec(1, 1001, r) && r.seq == 2);
	const Counters2 c2 = GetCounters2();
	CHECK(c2.droppedUnit == 3 && c2.unitEvents == 5 && c2.unitInserts == 5);
	Batch b;
	CHECK(Drain(b) && b.events.size() == 3 && b.events[2].kind == kOverflow && b.events[2].a[0] == 3);
	// a path whose event the queue drops keeps nothing in the side table
	SetQueueCap(1);
	UnitRecord u = Unit(1, 1000);
	PushUnit(UEv(kUnitActivate, 1, 1000), &u, UnitOp::kUpsert);
	const int32_t path[] = {1, 2};
	CHECK(PushUnitPath(UEv(kUnitMoved, 1, 1000), &u, UnitOp::kUpsert, path, 2) == 0);
	CombatRecord c = {};
	CHECK(PushCombat(UEv(kUnitCombat, 1, 1000), c, nullptr, 0, nullptr, nullptr) == 0);
	CHECK(Drain(b) && b.paths.empty() && b.combats.empty() && b.pathPlots.empty());
	CHECK(GetCounters2().droppedUnit == 5);
}

// The load: the table and the pre-creation bits are emptied, unitTableGen + 1, the kLoadBegin event in the same lock.
static void TestUnitReset()
{
	g_test = "unit-reset";
	FreshUnits();
	UnitRecord u = Unit(1, 1001), r;
	PushUnit(UEv(kUnitCreated, 1, 1001, 7), &u, UnitOp::kUpsert);
	UnitRecord v = Unit(1, 1002);
	PushUnit(UEv(kUnitVisibility, 1, 1002, 0), &v, UnitOp::kUpsert);
	Batch b;
	Drain(b);
	CHECK(b.unitTableGen == 0);
	const uint32_t s = ResetUnitsAt(0);
	std::vector<UnitRecord> t; uint32_t g = 0;
	CopyUnits(t, g);   // issue 4463: the load is not drained yet: the copy is the last Drain's (one unit, generation 0)
	CHECK(t.size() == 1 && t[0].id == 1001 && g == 0 && s > 0);
	PushUnit(UEv(kUnitCreated, 1, 1002, 7), &v, UnitOp::kUpsert);
	CHECK(Rec(1, 1002, r) && (r.flags & kUnitFlagVisible));
	CHECK(Drain(b) && b.unitTableGen == 1 && b.events[0].kind == kLoadBegin && b.events[0].seq == s &&
	      b.events[0].a[0] == 1);
	CopyUnits(t, g);
	CHECK(t.size() == 1 && t[0].id == 1002 && g == 1);
	ResetUnits();
	ResetUnits();
	CHECK(Drain(b) && b.unitTableGen == 3 && b.events.size() == 2 && b.events[1].a[0] == 3);
	CHECK(GetCounters2().unitResets == 3);
}

// Concurrency: 4 writer threads each create, damage and remove their own units while the reader drains and copies.
// Every record of every copy must carry exactly the fields of the event its seq names (seq and content written in one
// lock), and the replay of every drained event must equal the final table.
static const int kUThreads = 4, kUEach = 20000;
static void TestUnitConcurrency()
{
	g_test = "unit-concurrency";
	FreshUnits();
	SetQueueCap(1u << 20);
	std::atomic<int> done(0);
	std::vector<std::thread> ws;
	for (int t = 0; t < kUThreads; ++t)
		ws.emplace_back([t, &done]() {
			for (int i = 0; i < kUEach; ++i)
			{
				if (i % 16 == 0) SwitchToThread();   // let the reader in between (copies taken while writers run)
				const int32_t id = 1000 + (i % 50);
				UnitRecord u = Unit(t, id, (int16_t)(i % 30000));
				const int step = i % 7;
				if (step == 6)
					PushUnit(UEv(kUnitDestroyed, t, id), &u, UnitOp::kRemove);
				else
					PushUnit(UEv(step == 0 ? kUnitCreated : kUnitSetDamage, t, id, i % 30000), &u, UnitOp::kUpsert);
			}
			++done;
		});
	std::map<uint32_t, Event> bySeq;
	std::map<uint64_t, UnitRecord> model;
	Batch b;
	size_t copies = 0;
	bool copiesOk = true;
	std::vector<std::vector<UnitRecord>> held;
	std::vector<Event> allEv;
	uint32_t lastDrained = 0;
	size_t ahead = 0;   // issue 4463: records of a copy newer than every event drained before it (must stay 0)
	for (;;)
	{
		const bool finished = done.load() == kUThreads;
		if (Drain(b)) allEv.insert(allEv.end(), b.events.begin(), b.events.end());
		if (!b.events.empty()) lastDrained = b.events.back().seq;
		std::vector<UnitRecord> t; uint32_t g = 0;   // the writers push between the Drain and this copy (note 3.4's race)
		CopyUnits(t, g);
		for (const UnitRecord& r : t) ahead += r.seq > lastDrained;
		if (held.size() < 2000) held.push_back(t);   // checked after the run
		++copies;
		if (finished && b.events.empty()) break;
	}
	CHECK(ahead == 0);
	for (auto& w : ws) w.join();
	for (const Event& e : allEv) bySeq[e.seq] = e;
	// every record of every copy matches the event of its seq
	for (const auto& t : held)
		for (const UnitRecord& r : t)
		{
			auto it = bySeq.find(r.seq);
			copiesOk = copiesOk && it != bySeq.end() && it->second.a[0] == r.owner && it->second.a[1] == r.id &&
			           it->second.a[2] == r.damage && it->second.kind != kUnitDestroyed;
		}
	CHECK(copiesOk);
	// replay
	for (const auto& kv : bySeq)
	{
		const Event& e = kv.second;
		const uint64_t key = ((uint64_t)(uint8_t)e.a[0] << 32) | (uint32_t)e.a[1];
		if (e.kind == kUnitDestroyed) model.erase(key);
		else if (e.kind == kUnitCreated || model.count(key))
		{
			UnitRecord r = Unit(e.a[0], e.a[1], (int16_t)e.a[2]);
			r.seq = e.seq;
			model[key] = r;
		}
	}
	std::vector<UnitRecord> fin; uint32_t g = 0;
	CopyUnits(fin, g);
	bool same = fin.size() == model.size();
	size_t k = 0;
	for (const auto& kv : model)
	{
		same = same && k < fin.size() && fin[k].owner == kv.second.owner && fin[k].id == kv.second.id &&
		       fin[k].damage == kv.second.damage && fin[k].seq == kv.second.seq;
		++k;
	}
	CHECK(same);
	CHECK(bySeq.size() == (size_t)kUThreads * kUEach && GetCounters2().droppedUnit == 0);
	CHECK(copies >= 20);   // the copies did overlap the writers
	printf("info unit-concurrency: %zu events, %zu copies, final table %zu units\n", bySeq.size(), copies, fin.size());
}

// Issue 4463, rule (f): a Visibility before the creation is ignored (the EXE ignores it, unit-server-scene.md 12.1 [L]):
// the record starts visible, by kUnitCreated and by the creating kUnitEmbark; such events carry kEventNoRecord, the
// creations and the events of a held key do not.
static void TestUnitPreCreationVisibility()
{
	g_test = "unit-pre-creation-visibility";
	FreshUnits();
	UnitRecord r, a = Unit(4, 5001), e = Unit(4, 5002, 0, kUnitFlagEmbarked);
	// 12.1's two units: the worker created second in a city (Visibility(0) twice before Created) and the worker on a
	// stacked embarked plot (Visibility(1) of the other unit, its own Visibility(0), then Embark(1) as its creation)
	PushUnit(UEv(kUnitVisibility, 4, 5001, 0), &a, UnitOp::kUpsert);
	PushUnit(UEv(kUnitTeleported, 4, 5001, 33), &a, UnitOp::kUpsert);
	PushUnit(UEv(kUnitVisibility, 4, 5001, 0), &a, UnitOp::kUpsert);
	PushUnit(UEv(kUnitCreated, 4, 5001, 7), &a, UnitOp::kUpsert);
	PushUnit(UEv(kUnitVisibility, 4, 5002, 0), &e, UnitOp::kUpsert);
	PushUnit(UEv(kUnitEmbark, 4, 5002, 1), &e, UnitOp::kUpsert);
	CHECK(Rec(4, 5001, r) && (r.flags & kUnitFlagVisible));
	CHECK(Rec(4, 5002, r) && r.flags == (kUnitFlagEmbarked | kUnitFlagVisible));
	// after the creation a Visibility counts again
	PushUnit(UEv(kUnitVisibility, 4, 5001, 0), &a, UnitOp::kUpsert);
	CHECK(Rec(4, 5001, r) && !(r.flags & kUnitFlagVisible));
	Batch b;
	CHECK(Drain(b) && b.events.size() == 7);
	const uint16_t want[7] = {kEventNoRecord, kEventNoRecord, kEventNoRecord, 0, kEventNoRecord, 0, 0};
	bool flagsOk = true;
	for (size_t i = 0; i < 7; ++i) flagsOk = flagsOk && b.events[i].flags == want[i];
	CHECK(flagsOk);
	// negative: kEventNoRecord also on a removal and a flag patch of an unknown key, never on a kNone event or a creation
	UnitRecord x = Unit(4, 5999);
	PushUnit(UEv(kUnitDestroyed, 4, 5999), &x, UnitOp::kRemove);
	PushUnit(UEv(kUnitActivate, 4, 5999), &x, UnitOp::kPatchFlags);
	PushUnit(UEv(kCitySetDamage, 4, 9, 1, 0), nullptr, UnitOp::kNone);
	PushUnit(UEv(kUnitCreated, 4, 5999, 7), &x, UnitOp::kUpsert);
	PushUnit(UEv(kUnitDestroyed, 4, 5999), &x, UnitOp::kRemove);
	CHECK(Drain(b) && b.events.size() == 5 && b.events[0].flags == kEventNoRecord && b.events[1].flags == kEventNoRecord &&
	      b.events[2].flags == 0 && b.events[3].flags == 0 && b.events[4].flags == 0);
	// a Fortify before the creation is still kept [I] (not measured)
	UnitRecord f = Unit(4, 5003);
	PushUnit(UEv(kUnitFortify, 4, 5003, 1), &f, UnitOp::kUpsert);
	PushUnit(UEv(kUnitCreated, 4, 5003, 7), &f, UnitOp::kUpsert);
	CHECK(Rec(4, 5003, r) && r.flags == (kUnitFlagFortified | kUnitFlagVisible));
}

// Issue 4463, rule (g): the record's facing is the creation's and changes only with a move (kinds 12, 30) or a
// re-creation in the EXE (11, 21, 19); every other event keeps it, also when the getter says otherwise (a Lua
// setFacingDirection sends no call; unit-server-scene.md 12.1 [L]). Issue 0449: a teleport (13) keeps it too, although
// the DLL's setXY turned the unit (the EXE keeps the heading there, unit-server-scene.md 12.4 [L]); its plot is taken.
static void TestUnitFacing()
{
	g_test = "unit-facing";
	FreshUnits();
	UnitRecord r, u = Unit(3, 6001);
	u.facing = 2;
	PushUnit(UEv(kUnitCreated, 3, 6001, 7), &u, UnitOp::kUpsert);
	UnitRecord later = Unit(3, 6001, 5);
	later.facing = 7;   // NO_DIRECTION, as Lua InitUnit leaves it after the creation
	const uint16_t keep[] = {kUnitSetDamage, kUnitFortify, kUnitWork, kUnitActivate, kUnitGarrison, kUnitShouldDimFlag,
	                         kUnitVisibility, kUnitDestroyedInCombat, kUnitMissionEnd, kUnitResetAnimationState};
	bool kept = true;
	for (uint16_t k : keep)
	{
		PushUnit(UEv(k, 3, 6001, 1), &later, UnitOp::kUpsert);
		kept = kept && Rec(3, 6001, r) && r.facing == 2 && r.damage == 5;   // the other getter fields are taken
	}
	CHECK(kept);
	// a combat's refresh of its units keeps the facing too
	CombatRecord c = {};
	c.attackerOwner = 3; c.attackerUnit = 6001; c.attackerCity = -1; c.defenderOwner = -1; c.defenderUnit = -1; c.defenderCity = -1;
	PushCombat(UEv(kUnitCombat, 3, 6001), c, nullptr, 0, &later, nullptr);
	CHECK(Rec(3, 6001, r) && r.facing == 2);
	// issue 0449: a teleport keeps the heading (the getter's 4, the step's direction, is not taken), its plot is taken;
	// twice, so that the kept value is the record's and not the event's
	for (int i = 0; i < 2; ++i)
	{
		UnitRecord t = Unit(3, 6001, 6);
		t.facing = (int8_t)(4 - i);
		t.plot = 41 + i;
		PushUnit(UEv(kUnitTeleported, 3, 6001, 41 + i), &t, UnitOp::kUpsert);
		CHECK(Rec(3, 6001, r) && r.facing == 2 && r.plot == 41 + i && r.damage == 6);
	}
	// the move kinds take it
	struct M { uint16_t kind; int8_t facing; } moves[] = {{kUnitRebased, 1}, {kUnitRebased, 4}};
	for (const M& m : moves)
	{
		UnitRecord v = Unit(3, 6001);
		v.facing = m.facing;
		PushUnit(UEv(m.kind, 3, 6001, 40), &v, UnitOp::kUpsert);
		CHECK(Rec(3, 6001, r) && r.facing == m.facing);
	}
	// after a rebase a teleport still keeps the facing the rebase set
	UnitRecord t2 = Unit(3, 6001);
	t2.facing = 0;
	PushUnit(UEv(kUnitTeleported, 3, 6001, 40), &t2, UnitOp::kUpsert);
	CHECK(Rec(3, 6001, r) && r.facing == 4);
	UnitRecord mv = Unit(3, 6001);
	mv.facing = 5;
	const int32_t path[] = {1, 2};
	PushUnitPath(UEv(kUnitMoved, 3, 6001), &mv, UnitOp::kUpsert, path, 2);
	CHECK(Rec(3, 6001, r) && r.facing == 5);
	// the re-creations take it: an embark (remove + create in the EXE), a paradrop, a second Created (era change)
	struct C { uint16_t kind; int8_t facing; } recs[] = {{kUnitEmbark, 0}, {kUnitParadrop, 3}, {kUnitCreated, 7}};
	for (const C& k : recs)
	{
		UnitRecord v = Unit(3, 6001);
		v.facing = k.facing;
		PushUnit(UEv(k.kind, 3, 6001, 1), &v, UnitOp::kUpsert);
		CHECK(Rec(3, 6001, r) && r.facing == k.facing);
	}
}

// Issue 4463, the race of unit-server-scene.md 3.4: CopyUnits returns the table as the last Drain left it, never an
// event pushed after that Drain; the next Drain carries it. The generation is the drained one too.
static void TestUnitCopyAtDrain()
{
	g_test = "unit-copy-at-drain";
	FreshUnits();
	std::vector<UnitRecord> t; uint32_t g = 9;
	UnitRecord a = Unit(2, 7001), c = Unit(2, 7002);
	PushUnit(UEv(kUnitCreated, 2, 7001, 7), &a, UnitOp::kUpsert);
	// negative: before the first Drain the copy is empty (no event drained yet), although the table holds the unit
	CopyUnits(t, g);
	UnitRecord r;
	CHECK(t.empty() && g == 0 && Rec(2, 7001, r));
	Batch b;
	CHECK(Drain(b) && b.events.size() == 1);
	const uint32_t last = b.events.back().seq;
	// the DLL's thread pushes between the render thread's Drain and its copy
	PushUnit(UEv(kUnitCreated, 2, 7002, 7), &c, UnitOp::kUpsert);
	UnitRecord d = Unit(2, 7001, 40);
	PushUnit(UEv(kUnitSetDamage, 2, 7001, 40, 0), &d, UnitOp::kUpsert);
	CopyUnits(t, g);
	CHECK(t.size() == 1 && t[0].id == 7001 && t[0].damage == 0 && t[0].seq <= last && g == 0);
	// the next Drain carries both events and the copy follows
	CHECK(Drain(b) && b.events.size() == 2);
	CopyUnits(t, g);
	CHECK(t.size() == 2 && t[0].damage == 40 && t[1].id == 7002 && t[0].seq <= b.events.back().seq && t[1].seq <= b.events.back().seq);
	// a load between the Drain and the copy: neither the emptied table nor the new generation shows before its Drain
	const uint32_t gen0 = b.unitTableGen;
	ResetUnitsAt(0);
	CopyUnits(t, g);
	CHECK(t.size() == 2 && g == gen0);
	CHECK(Drain(b) && b.unitTableGen == gen0 + 1);
	CopyUnits(t, g);
	CHECK(t.empty() && g == gen0 + 1);
	// a Drain without unit changes keeps the copy
	Push(Ev(kPlotState, 3));
	CHECK(Drain(b));
	CopyUnits(t, g);
	CHECK(t.empty() && g == gen0 + 1);
}

static void TestUnitDatabaseSlot()
{
	g_test = "unit-db-slot";
	Fresh();
	std::wstring p; uint32_t g = 9;
	CHECK(!GetUnitDatabase(p, g) && g == 9);
	SetUnitDatabase(L"C:/run/db/units-1.1.db", 1);
	CHECK(GetUnitDatabase(p, g) && p == L"C:/run/db/units-1.1.db" && g == 1);
	SetUnitDatabase(L"C:/run/db/units-1.2.db", 2);
	CHECK(GetUnitDatabase(p, g) && g == 2 && GetCounters2().unitDbSets == 2);
}

// ---- the database copy (MC #2105 Q5): a copy made while another connection writes is one committed state ----
namespace tdb
{
struct sqlite3;
struct sqlite3_stmt;
typedef int (*FnOpen)(const char*, sqlite3**, int, const char*);
typedef int (*FnClose)(sqlite3*);
typedef int (*FnExec)(sqlite3*, const char*, void*, void*, char**);
typedef int (*FnBusy)(sqlite3*, int);
typedef const char* (*FnErrmsg)(sqlite3*);
FnOpen open_; FnClose close_; FnExec exec_; FnBusy busy_; FnErrmsg errmsg_;
bool Load()
{
	HMODULE m = LoadLibraryExW(L"winsqlite3.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!m) return false;
	open_ = (FnOpen)(void*)GetProcAddress(m, "sqlite3_open_v2");
	close_ = (FnClose)(void*)GetProcAddress(m, "sqlite3_close");
	exec_ = (FnExec)(void*)GetProcAddress(m, "sqlite3_exec");
	busy_ = (FnBusy)(void*)GetProcAddress(m, "sqlite3_busy_timeout");
	errmsg_ = (FnErrmsg)(void*)GetProcAddress(m, "sqlite3_errmsg");
	return open_ && close_ && exec_ && busy_ && errmsg_;
}
}  // namespace tdb

static std::wstring TempDir()   // beside the test binary (the runner's --out folder), never the current directory
{
	wchar_t buf[MAX_PATH];
	GetModuleFileNameW(nullptr, buf, MAX_PATH);
	std::wstring d(buf);
	const size_t slash = d.find_last_of(L"\\/");
	if (slash != std::wstring::npos) d.resize(slash);
	return d + L"\\dbcopy";
}

static std::string Narrow(const std::wstring& w)
{
	std::string s;
	for (wchar_t c : w) s += (char)c;
	return s;
}

static std::atomic<int> g_writerStop(0), g_writerFails(0), g_writerCommits(0);

static void TestDbCopy()
{
	g_test = "db-copy";
	std::string why;
	CHECK(DbCopyAvailable(why));
	if (!tdb::Load()) { CHECK(!"winsqlite3 for the test"); return; }
	const std::wstring dir = TempDir();
	CreateDirectoryW(dir.c_str(), nullptr);
	const std::wstring src = dir + L"\\src.db";
	DeleteFileW(src.c_str());
	tdb::sqlite3* w = nullptr;
	CHECK(tdb::open_(Narrow(src).c_str(), &w, 0x2 | 0x4, nullptr) == 0);
	// as the server's copy is tuned (srv_remote_db.cpp 0129): journal in memory, no sync; and the DLL's busy handler:
	// 30 x 2 ms (busy_timeout 60 ms is the same bound)
	tdb::exec_(w, "PRAGMA journal_mode = MEMORY; PRAGMA synchronous = OFF; PRAGMA page_size = 1024;"
	              "CREATE TABLE t(k INTEGER PRIMARY KEY, a INTEGER, b INTEGER);"
	              "CREATE TABLE n(c INTEGER); INSERT INTO n VALUES(0);", nullptr, nullptr, nullptr);
	tdb::busy_(w, 60);
	tdb::exec_(w, "BEGIN", nullptr, nullptr, nullptr);
	for (int i = 0; i < 4000; ++i)
	{
		char sql[160];
		sprintf_s(sql, "INSERT INTO t VALUES(%d, %d, %d); UPDATE n SET c = c + 1;", i, i % 100, 100 - i % 100);
		tdb::exec_(w, sql, nullptr, nullptr, nullptr);
	}
	tdb::exec_(w, "COMMIT", nullptr, nullptr, nullptr);
	// unchanged source: the second call makes no copy
	SrvFeed::DbCopier cp;
	SrvFeed::DbCopyResult r;
	CHECK(cp.Copy(src, dir + L"\\c0.db", false, r) && r.ok && !r.unchanged && r.pages > 0);
	CHECK(cp.Copy(src, dir + L"\\c0b.db", false, r) && r.unchanged && GetFileAttributesW((dir + L"\\c0b.db").c_str()) ==
	      INVALID_FILE_ATTRIBUTES);
	// the writer: transactions that keep a + b = 100 in every row and n.c = the row count
	g_writerStop = 0; g_writerFails = 0; g_writerCommits = 0;
	std::thread wt([w]() {
		int i = 4000;
		while (!g_writerStop.load())
		{
			char sql[400];
			sprintf_s(sql, "BEGIN; UPDATE t SET a = (a + 7) %% 100, b = 100 - ((a + 7) %% 100) WHERE k %% 13 = %d;"
			               "INSERT INTO t VALUES(%d, 50, 50); UPDATE n SET c = c + 1; COMMIT;", i % 13, i);
			const ULONGLONG t0 = GetTickCount64();
			const int rc = tdb::exec_(w, sql, nullptr, nullptr, nullptr);
			if (rc != 0)
			{
				// ticket 0631 take 2: each failure named (code, message, how long the transaction waited)
				const int n = ++g_writerFails;
				if (n <= 4)
					printf("info db-copy: writer failure %d: rc %d (%s) after %llu ms\n", n, rc, tdb::errmsg_(w),
					       (unsigned long long)(GetTickCount64() - t0));
				tdb::exec_(w, "ROLLBACK", nullptr, nullptr, nullptr);
			}
			else
				++g_writerCommits;
			++i;
		}
	});
	// copies while it writes, each checked read-only and immutable (as the render side opens it) during the writes
	int good = 0, made = 0;
	double stepMax = 0, copyMax = 0;
	while (g_writerCommits.load() < 5 && !g_writerFails.load()) Sleep(1);
	for (int k = 0; k < 12; ++k)
	{
		const std::wstring dst = dir + L"\\c" + std::to_wstring(k + 1) + L".db";
		SrvFeed::DbCopyResult rr;
		const bool ok = cp.Copy(src, dst, false, rr);
		if (!ok) printf("info db-copy: copy %d failed: %s\n", k, rr.why.c_str());
		if (rr.stepMsMax > stepMax) stepMax = rr.stepMsMax;
		if (rr.ms > copyMax) copyMax = rr.ms;
		if (ok && !rr.unchanged) ++made;
		std::string v1, v2, why2;
		const bool c1 = DbCheckCopy(dst, "SELECT COUNT(*) FROM t WHERE a + b != 100", &v1, why2);
		const bool c2 = DbCheckCopy(dst, "SELECT (SELECT COUNT(*) FROM t) - (SELECT c FROM n)", &v2, why2);
		if (ok && !rr.unchanged && c1 && c2 && v1 == "0" && v2 == "0") ++good;
		else if (!(ok && rr.unchanged)) printf("info db-copy: copy %d check: %s (%s, %s)\n", k, why2.c_str(), v1.c_str(), v2.c_str());
		Sleep(5);
	}
	g_writerStop = 1;
	wt.join();
	CHECK(good == made && made >= 6);
	CHECK(g_writerFails.load() == 0 && g_writerCommits.load() > 0);   // the writer never failed because of a copy
	printf("info db-copy: %d copies checked during %d writer commits, writer failures %d\n", good,
	       g_writerCommits.load(), g_writerFails.load());
	printf("info db-copy: longest backup step (source read lock held) %.1f ms, longest copy %.1f ms\n", stepMax, copyMax);
	tdb::close_(w);
	// refusals, each named
	SrvFeed::DbCopier cp2;
	CHECK(!cp2.Copy(dir + L"\\missing.db", dir + L"\\x.db", true, r) && r.why.find("does not exist") != std::string::npos);
	CHECK(!cp2.Copy(src, dir + L"\\no-such-dir\\x.db", true, r) && r.why.find("cannot create the copy") != std::string::npos);
	CHECK(!cp2.Copy(L"", dir + L"\\x.db", true, r) && !r.why.empty());
	const std::wstring junk = dir + L"\\junk.db";
	{
		FILE* f = nullptr;
		_wfopen_s(&f, junk.c_str(), L"wb");
		if (f) { fputs("this is not a database file at all, sixteen bytes and more", f); fclose(f); }
	}
	CHECK(!cp2.Copy(junk, dir + L"\\y.db", true, r) && !r.why.empty());
	printf("info db-copy refusal: %s\n", r.why.c_str());
	std::string why3;
	CHECK(!DbCheckCopy(junk, nullptr, nullptr, why3) && !why3.empty());
	CHECK(!DbCheckCopy(dir + L"\\missing.db", nullptr, nullptr, why3));
}

// Issue 0439 (MC #2908): kind 21's a[3] (EmbarkArtBits) passes the feed unchanged, in the event and through Drain, and the
// table's record is not touched by it; EmbarkArtHash24 is FNV-1a 32's top 24 bits (reference values computed outside).
static void TestEmbarkArtBits()
{
	g_test = "embark-art-bits";
	CHECK(EmbarkArtHash24(NULL) == 0);
	CHECK(EmbarkArtHash24("") == 0x811c9du);
	CHECK(EmbarkArtHash24("a") == 0xe40c29u);
	CHECK(EmbarkArtHash24("ART_DEF_UNIT_U_DANISH_LONGBOAT") == 0x99816fu);
	CHECK(EmbarkArtHash24("ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE") == 0x4b63ecu);
	CHECK(kEmbarkArtRead == 1u && kEmbarkArtAllWater == 2u && kEmbarkArtOverride == 4u && kEmbarkArtHashShift == 8);
	FreshUnits();
	Batch b;
	UnitRecord r;
	UnitRecord u = Unit(3, 2001, 0, (uint8_t)kUnitFlagEmbarked);
	const uint32_t bits = kEmbarkArtRead | kEmbarkArtAllWater | kEmbarkArtOverride |
	                      (EmbarkArtHash24("ART_DEF_UNIT_U_DANISH_LONGBOAT") << kEmbarkArtHashShift);
	const uint32_t s1 = PushUnit(UEv(kUnitEmbark, 3, 2001, 1, (int32_t)bits), &u, UnitOp::kUpsert);
	CHECK(s1 > 0 && Rec(3, 2001, r) && r.seq == s1 && (r.flags & kUnitFlagEmbarked) && r.reserved == 0);
	UnitRecord land = Unit(3, 2001, 0, 0);
	const uint32_t s2 = PushUnit(UEv(kUnitEmbark, 3, 2001, 0, 0), &land, UnitOp::kUpsert);
	CHECK(s2 > s1 && Rec(3, 2001, r) && !(r.flags & kUnitFlagEmbarked));
	CHECK(Drain(b) && b.events.size() == 2 && b.events[0].kind == kUnitEmbark && (uint32_t)b.events[0].a[3] == bits &&
	      b.events[0].a[2] == 1 && b.events[1].a[2] == 0 && b.events[1].a[3] == 0);
}

int main()
{
	TestLayout();
	TestDisabled();
	TestOrder();
	TestOverflow();
	TestSnapshot();
	TestPatches();
	TestLayoutBeforeTable();
	TestCarryOver();
	TestBadIndex();
	TestDisableMidway();
	TestThreads();
	TestArtType();
	TestRiverEdges();   // ticket 0631
	// issue 4452
	TestUnitLayout();
	TestUnitsOff();
	TestUnitKinds();
	TestUnitCreationRules();
	TestUnitPaths();
	TestUnitCombats();
	TestPlayers();
	TestUnitOverflow();
	TestUnitReset();
	TestUnitConcurrency();
	// issue 4463
	TestUnitPreCreationVisibility();
	TestUnitFacing();
	TestUnitCopyAtDrain();
	TestUnitDatabaseSlot();
	TestDbCopy();
	// issue 0439
	TestEmbarkArtBits();
#ifdef FEED_TEST_INJECT_FAIL
	g_test = "injected";
	CHECK(1 == 2);
#endif
	printf("test_feed: %d checks passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
