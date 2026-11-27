// test_liveterrain.cpp - tests of the live-terrain region hook's core (server/src/render/srv_liveterrain.*, ticket 4907)
// against the test stub of 4903's producer (stub/). Written 2026-11-18 by w5-4907 for this repository (GPL-3.0-or-later).
// Exit 0 when every check passed; one line per failed check and a summary line "test_liveterrain: N checks, M failed".
#include "srv_liveterrain.h"
#include "srv_liveterrain_features.h"
#include "c5r/liveterrain/producer.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

using namespace srvrender::liveterrain;
namespace c5l = c5r::liveterrain;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond)                                                                                     \
	do                                                                                                  \
	{                                                                                                   \
		g_checks++;                                                                                     \
		if(!(cond))                                                                                     \
		{                                                                                               \
			g_failed++;                                                                                 \
			printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);                                  \
		}                                                                                               \
	} while(0)

static std::vector<std::string> g_lines;
static void Log(const char* line)
{
	g_lines.push_back(line);
}
static bool LogHas(const char* s)
{
	for(const std::string& l : g_lines)
		if(l.find(s) != std::string::npos)
			return true;
	return false;
}

static const uint32_t W = 8, H = 6;   // the map; the field has H + 4 rows of plots

static SrvFeed::PlotRecord Rec(int8_t terrain, int8_t plotType)
{
	SrvFeed::PlotRecord r;
	memset(&r, 0, sizeof(r));
	r.terrain = terrain;
	r.plotType = plotType;
	r.feature = -1;
	r.route = -1;
	r.resource = -1;
	r.improvement = -1;
	r.owner = -1;
	r.known = 1;
	return r;
}

// grassland flat everywhere, a coast column at x = 0 (terrain 5, plot type ocean)
static std::vector<SrvFeed::PlotRecord> Table()
{
	std::vector<SrvFeed::PlotRecord> t((size_t)W * H);
	for(uint32_t y = 0; y < H; ++y)
		for(uint32_t x = 0; x < W; ++x)
			t[(size_t)y * W + x] = x == 0 ? Rec(5, 3) : Rec(0, 2);
	return t;
}

static std::vector<uint8_t> Constants(uint32_t gw, uint32_t gh, int32_t wrap)
{
	std::vector<uint8_t> b(kConstantsBlockBytes, 0);
	memcpy(&b[0], &wrap, 4);
	memcpy(&b[0xD91264 - 0xD90FBC], &gw, 4);
	memcpy(&b[0xD91268 - 0xD90FBC], &gh, 4);
	return b;
}

static c5r::heightfield::HeightGrid Grid()
{
	c5r::heightfield::HeightGrid g;
	g.mapW = W;
	g.mapH = H + 4;
	g.samplesX = W * 64;
	g.samplesY = (H + 4) * 64;
	g.h.assign((size_t)g.samplesX * g.samplesY, 15330);
	g.mat.assign(g.h.size(), 3);
	return g;
}

static LoadIn Load(uint32_t seq, const c5r::heightfield::HeightGrid& g, const std::vector<SrvFeed::PlotRecord>& t,
                   const std::vector<uint8_t>& constants, const FeatureNameFn& names = nullptr)
{
	LoadIn in;
	in.loadSeq = seq;
	in.constants = constants;
	std::string why;
	PlotTableFromFeed(t, W, H, names, in.table, why);
	in.samplesX = g.samplesX;
	in.samplesY = g.samplesY;
	in.heights = g.h;
	in.materials = g.mat;
	return in;
}

static std::wstring g_lib20, g_lib19, g_libMissing;

static void WriteFile(const std::wstring& p, uint32_t v)
{
	FILE* f = nullptr;
	_wfopen_s(&f, p.c_str(), L"wb");
	if(f)
	{
		fwrite(&v, 4, 1, f);
		fclose(f);
	}
}

static void TestTableAndFilter()
{
	std::vector<SrvFeed::PlotRecord> t = Table();
	PlotTableIn out;
	std::string why;
	CHECK(!PlotTableFromFeed(t, W, H + 1, nullptr, out, why) && why.find("not 8 x 7") != std::string::npos);
	CHECK(!PlotTableFromFeed(t, 0, H, nullptr, out, why));
	t[9].flags = 0x1;                            // a river
	t[10].flags = 0x8;                           // a natural wonder
	t[10].feature = 17;
	t[11].artType = 9;                           // out of range -> 0
	t[12].artType = 3;
	CHECK(PlotTableFromFeed(t, W, H, [](int32_t id) { return id == 17 ? std::string("FEATURE_ULURU") : std::string(); }, out, why));
	CHECK(out.W == W && out.H == H && out.plots.size() == (size_t)W * H);
	CHECK(out.plots[9].riverEdges == 7 && out.plots[8].riverEdges == 0);
	CHECK(out.plots[10].naturalWonder && out.plots[10].featureType == "FEATURE_ULURU");
	CHECK(out.plots[11].continent == 0 && out.plots[12].continent == 3);
	CHECK(out.plots[0].terrain == 5 && out.plots[0].plotType == 3);
	// the filter
	SrvFeed::PlotRecord a = Rec(0, 2), b = a;
	CHECK(!FieldRelevantChange(a, b));
	b.owner = 3;
	b.fow = 3;
	b.feature = 5;                               // a forest: not the field's
	b.route = 1;
	b.improvement = 4;
	CHECK(!FieldRelevantChange(a, b));
	b = a;
	b.terrain = 2;
	CHECK(FieldRelevantChange(a, b));
	b = a;
	b.plotType = 1;
	CHECK(FieldRelevantChange(a, b));
	b = a;
	b.flags = 0x8;
	CHECK(FieldRelevantChange(a, b));
	b = a;
	b.flags = 0x1;
	CHECK(FieldRelevantChange(a, b));
	a.flags = b.flags = 0x8;
	a.feature = 17;
	b.feature = 18;
	CHECK(FieldRelevantChange(a, b));
}

// Ticket 0631: the plots' own river edges from the feed (known bit 2), the old any-edge rule only for records without them
// (scene bundles recorded before 0631), the filter on the edges, the producer's river reason on the real edges, and the
// rule named in the live and the replay logs (rule A).
static LiveTerrain::Config Cfg(bool threaded, const std::wstring& lib);
static std::vector<SrvFeed::PlotRecord> EdgeTable()
{
	std::vector<SrvFeed::PlotRecord> t = Table();
	for(SrvFeed::PlotRecord& r : t)
		r.known |= SrvFeed::kPlotKnownRiverEdges;
	return t;
}

static void TestRiverEdges()
{
	std::string why;
	// (1) the mapping: the plot's own three bits; a plot that only touches a neighbour's edge (flag set, no own bit) gets 0
	std::vector<SrvFeed::PlotRecord> t = EdgeTable();
	t[9].flags = 0x1;
	t[9].riverEdges = 0x5;                       // W of river and NE of river
	t[10].flags = 0x1;                           // the DLL's isRiver from a neighbour's edge only
	t[10].riverEdges = 0;
	t[11].riverEdges = 0xFA;                     // bits above the three are not edges
	t[12].riverEdges = 0x2;                      // an own edge without the flag (the DLL's crossing count can lag): kept
	PlotTableIn out;
	CHECK(PlotTableFromFeed(t, W, H, nullptr, out, why));
	CHECK(out.plots[9].riverEdges == 5 && out.plots[10].riverEdges == 0 && out.plots[11].riverEdges == 2 &&
	      out.plots[12].riverEdges == 2 && out.plots[8].riverEdges == 0);
	CHECK(out.oldRuleRecords == 0 && out.oldRuleRiverPlots == 0);
	CHECK(RiverEdgeRuleText(out).find("own edges (ticket 0631)") != std::string::npos);
	// (2) records without the edges (known bit 2 clear: a bundle recorded before 0631) take the old rule, counted
	std::vector<SrvFeed::PlotRecord> m = t;
	m[20].known = 1;
	m[20].flags = 0x1;
	m[21].known = 1;
	m[21].riverEdges = 0x7;                      // no edges read: the field is not read either
	CHECK(PlotTableFromFeed(m, W, H, nullptr, out, why));
	CHECK(out.plots[20].riverEdges == 7 && out.plots[21].riverEdges == 0 && out.plots[9].riverEdges == 5);
	CHECK(out.oldRuleRecords == 2 && out.oldRuleRiverPlots == 1);
	const std::string text = RiverEdgeRuleText(out);
	CHECK(text.find("2 of 48 record(s)") != std::string::npos && text.find("recorded before ticket 0631") != std::string::npos &&
	      text.find("on the 1 with") != std::string::npos);
	CHECK(PlotTableFromFeed(t, W, H, nullptr, out, why) && out.oldRuleRecords == 0);   // the counts start again per table
	std::vector<SrvFeed::PlotRecord> old = Table();   // a whole old table (every record known = 1)
	old[9].flags = 0x1;
	CHECK(PlotTableFromFeed(old, W, H, nullptr, out, why) && out.oldRuleRecords == W * H && out.oldRuleRiverPlots == 1 &&
	      out.plots[9].riverEdges == 7);
	// (3) the filter: with edges on both records only the edges count; the flag only where a record has none
	SrvFeed::PlotRecord a = t[30], b = a;
	b.flags = 0x1;
	CHECK(!FieldRelevantChange(a, b));           // a neighbour's edge appeared: the producer's table does not change
	b = a;
	b.riverEdges = 0x1;
	CHECK(FieldRelevantChange(a, b));
	b = a;
	b.riverEdges = 0x8;                          // not an edge bit
	CHECK(!FieldRelevantChange(a, b));
	b = a;
	b.known = 1;                                 // one record without edges: relevant (the rule differs)
	CHECK(FieldRelevantChange(a, b));
	a.known = b.known = 1;
	b.flags = 0x1;
	CHECK(FieldRelevantChange(a, b));            // the old rule as before 0631
	b.flags = 0;
	b.riverEdges = 0x3;
	CHECK(!FieldRelevantChange(a, b));           // the old rule reads no edges
	// (4) the producer's RiverEdge reason (the stub's window rule) on the real edges: a change beside a plot that only
	// touches a neighbour's river is live; beside a plot with its own edge it waits
	g_lines.clear();
	{
		LiveTerrain lt(Cfg(false, g_lib20));
		c5r::heightfield::HeightGrid g = Grid();
		std::vector<SrvFeed::PlotRecord> e = EdgeTable();
		e[(size_t)4 * W + 6].flags = 0x1;        // (6, 4): river flag, no own edge
		lt.onField(Load(3, g, e, Constants(W, H + 4, 0)), e);
		CHECK(lt.ready() && LogHas("river edges: the plots' own edges (ticket 0631)"));
		std::vector<SrvFeed::PlotRecord> e2 = e;
		e2[(size_t)4 * W + 5].terrain = 2;       // (5, 4) beside it
		lt.onTable(e2, W, H, nullptr);
		Result r;
		CHECK(lt.takeResult(r) && r.out.live && r.out.reason == Wait::None);
		// the same plot with its own edge (an own-edge change alone is relevant: posted, and the window rule holds it)
		std::vector<SrvFeed::PlotRecord> e3 = e2;
		e3[(size_t)4 * W + 6].riverEdges = 0x1;
		e3[(size_t)4 * W + 5].terrain = 3;
		lt.onTable(e3, W, H, nullptr);
		CHECK(lt.takeResult(r) && !r.out.live && r.out.reason == Wait::RiverEdge);
	}
	// the old rule: the same flag-only plot holds the change (as before 0631), and the load's line says why
	g_lines.clear();
	{
		LiveTerrain lt(Cfg(false, g_lib20));
		c5r::heightfield::HeightGrid g = Grid();
		std::vector<SrvFeed::PlotRecord> o = Table();
		o[(size_t)4 * W + 6].flags = 0x1;
		lt.onField(Load(4, g, o, Constants(W, H + 4, 0)), o);
		CHECK(LogHas("48 of 48 record(s) carry no river edges (recorded before ticket 0631)"));
		std::vector<SrvFeed::PlotRecord> o2 = o;
		o2[(size_t)4 * W + 5].terrain = 2;
		lt.onTable(o2, W, H, nullptr);
		Result r;
		CHECK(lt.takeResult(r) && !r.out.live && r.out.reason == Wait::RiverEdge);
		// (5) rule A: a replay of this load's journal names the old rule; one of a 0631 journal the own edges
		Replay rp;
		CHECK(rp.init(MakeC5rProducerFactory(g_lib20), lt.journal(), Grid()).ok());
		CHECK(rp.riverEdgeRule().find("recorded before ticket 0631") != std::string::npos);
	}
	{
		LiveTerrain lt(Cfg(false, g_lib20));
		std::vector<SrvFeed::PlotRecord> e = EdgeTable();
		lt.onField(Load(5, Grid(), e, Constants(W, H + 4, 0)), e);
		Replay rp;
		CHECK(rp.init(MakeC5rProducerFactory(g_lib20), lt.journal(), Grid()).ok());
		CHECK(rp.riverEdgeRule() == "the plots' own edges (ticket 0631)");
	}
}

static void TestApplyAndHash()
{
	c5r::heightfield::HeightGrid g = Grid();
	const std::string h0 = RegionSha256(g, 0, 0, 64, 64);
	CHECK(h0.size() == 64);
	CHECK(RegionSha256(g, 0, 0, 0, 64).empty());
	CHECK(RegionSha256(g, 0, 0, g.samplesX + 1, 64).empty());
	Rect r;
	r.x0 = 10;
	r.y0 = 20;
	r.w = 4;
	r.h = 2;
	r.heights.assign(8, 15330);
	r.heights[3] = 20000;
	r.materials.assign(8, 3);
	r.materials[5] = 9;
	ApplyStats st;
	std::string why;
	CHECK(ApplyWindow(g, {r}, st, why));
	CHECK(st.written == 8 && st.heightsChanged == 1 && st.materialsChanged == 1);
	CHECK(st.x0 == 10 && st.y0 == 20 && st.x1 == 14 && st.y1 == 22);
	CHECK(g.h[(size_t)20 * g.samplesX + 13] == 20000 && g.mat[(size_t)21 * g.samplesX + 11] == 9);
	CHECK(RegionSha256(g, 0, 0, 64, 64) != h0);
	// refusals write nothing
	Rect bad = r;
	bad.x0 = g.samplesX - 2;
	Rect ok2 = r;
	ok2.heights.assign(8, 1);
	c5r::heightfield::HeightGrid before = g;
	CHECK(!ApplyWindow(g, {ok2, bad}, st, why) && why.find("outside the field") != std::string::npos);
	CHECK(g.h == before.h);
	Rect shortArr = r;
	shortArr.materials.resize(7);
	CHECK(!ApplyWindow(g, {shortArr}, st, why) && why.find("not w x h") != std::string::npos);
	Rect empty = r;
	empty.w = 0;
	CHECK(!ApplyWindow(g, {empty}, st, why));
	c5r::heightfield::HeightGrid broken = g;
	broken.h.resize(5);
	CHECK(!ApplyWindow(broken, {r}, st, why));
	// two rectangles: the box covers both
	Rect r2 = r;
	r2.x0 = 100;
	r2.y0 = 5;
	CHECK(ApplyWindow(g, {r, r2}, st, why) && st.x0 == 10 && st.y0 == 5 && st.x1 == 104 && st.y1 == 22 && st.written == 16);
}

static LiveTerrain::Config Cfg(bool threaded, const std::wstring& lib)
{
	LiveTerrain::Config c;
	c.factory = MakeC5rProducerFactory(lib);
	c.threaded = threaded;
	c.log = Log;
	return c;
}

static void TestLiveInline()
{
	g_lines.clear();
	LiveTerrain lt(Cfg(false, g_lib20));
	c5r::heightfield::HeightGrid g = Grid();
	std::vector<SrvFeed::PlotRecord> t = Table();
	lt.onField(Load(7, g, t, Constants(W, H + 4, 0)), t);
	CHECK(lt.ready() && lt.loadSeq() == 7 && lt.counters().creates == 1);
	CHECK(LogHas("producer made"));
	// nothing relevant: no job
	std::vector<SrvFeed::PlotRecord> t2 = t;
	t2[(size_t)3 * W + 4].owner = 2;
	lt.onTable(t2, W, H, nullptr);
	CHECK(lt.counters().changesPosted == 0);
	Result r;
	CHECK(!lt.takeResult(r));
	// flat -> hills at (4, 3): live
	t2[(size_t)3 * W + 4].plotType = 1;
	lt.onTable(t2, W, H, nullptr);
	CHECK(lt.counters().changesPosted == 1);
	CHECK(lt.takeResult(r));
	CHECK(r.loadSeq == 7 && r.job == 1 && r.out.live && r.out.reason == Wait::None);
	CHECK(r.out.changedPlots.size() == 1 && r.out.changedPlots[0].first == 4 && r.out.changedPlots[0].second == 3);
	CHECK(r.out.window.size() == 1 && r.out.inW1 == 64 * 64 && r.out.inRing > 0);
	ApplyStats st;
	std::string why;
	CHECK(ApplyWindow(g, r.out.window, st, why));
	CHECK(g.h[(size_t)(5 * 64 + 10) * g.samplesX + 4 * 64 + 10] == 20000);   // inside the plot's box: the own height
	CHECK(st.heightsChanged == 64 * 64);   // the ring blends towards the neighbours' own height, which the stub makes equal
	CHECK(lt.noteApplied(r, st, 1.0, RegionSha256(g, st.x0, st.y0, st.x1, st.y1)) == 1);
	CHECK(lt.counters().fieldGeneration == 1 && lt.counters().applied == 1 && LogHas("field generation 1"));
	// the same table again: nothing
	lt.onTable(t2, W, H, nullptr);
	CHECK(lt.counters().changesPosted == 1);
	// next to the coast column: Water, one log line naming the reason and the plot
	g_lines.clear();
	std::vector<SrvFeed::PlotRecord> t3 = t2;
	t3[(size_t)2 * W + 1].plotType = 0;
	lt.onTable(t3, W, H, nullptr);
	CHECK(lt.takeResult(r) && !r.out.live && r.out.reason == Wait::Water);
	CHECK(LogHas("wait for reload: Water") && LogHas("[plots 1,2]"));
	CHECK(lt.counters().waits == 1);
	// a river plot in the window: RiverEdge
	std::vector<SrvFeed::PlotRecord> t4 = t3;
	t4[(size_t)4 * W + 6].flags = 0x1;
	lt.onTable(t4, W, H, nullptr);
	CHECK(lt.takeResult(r) && !r.out.live && r.out.reason == Wait::NoChange);   // the river alone changes no pattern cell
	std::vector<SrvFeed::PlotRecord> t5 = t4;
	t5[(size_t)4 * W + 5].terrain = 2;
	lt.onTable(t5, W, H, nullptr);
	CHECK(lt.takeResult(r) && r.out.reason == Wait::RiverEdge);
	CHECK(lt.counters().noChange == 1);
	CHECK(lt.counters().held == 2);   // (1, 2) after Water, (5, 4) after RiverEdge
	// a clean change elsewhere is live although two plots wait for the reload (they are sent as the producer has them)
	std::vector<SrvFeed::PlotRecord> t5b = t5;
	t5b[(size_t)1 * W + 4].plotType = 1;
	lt.onTable(t5b, W, H, nullptr);
	CHECK(lt.takeResult(r) && r.out.live && r.out.changedPlots.size() == 1 && r.out.changedPlots[0].first == 4 &&
	      r.out.changedPlots[0].second == 1);
	// a held plot changed back: released, nothing posted
	const int64_t before = lt.counters().changesPosted;
	std::vector<SrvFeed::PlotRecord> t5c = t5b;
	t5c[(size_t)2 * W + 1].plotType = 2;
	lt.onTable(t5c, W, H, nullptr);
	CHECK(lt.counters().heldReleased == 1 && lt.counters().changesPosted == before);
	CHECK(!lt.takeResult(r));
	// a held plot changed again to something else: released and tried again (and held again: still next to the coast)
	std::vector<SrvFeed::PlotRecord> t5d = t5b;
	t5d[(size_t)4 * W + 5].terrain = 3;   // (5, 4): desert -> tundra, still next to the river
	lt.onTable(t5d, W, H, nullptr);
	CHECK(lt.counters().heldReleased == 2);
	CHECK(lt.takeResult(r) && r.out.reason == Wait::Water && r.out.changedPlots.size() == 2);   // (1, 2) is mountain again in t5b
	CHECK(lt.counters().held == 4);
	t5 = t5d;
	// a table of another size: ignored
	std::vector<SrvFeed::PlotRecord> small((size_t)4 * 4, Rec(0, 2));
	const int64_t posted = lt.counters().changesPosted;
	lt.onTable(small, 4, 4, nullptr);
	CHECK(lt.counters().changesPosted == posted);
	// the same load's field again: no new producer
	lt.onField(Load(7, g, t, Constants(W, H + 4, 0)), t);
	CHECK(lt.counters().creates == 1 && lt.counters().loads == 1);
	// a change error: LOUD, counted, the field untouched
	g_lines.clear();
	c5l::Producer::s_failChange = c5r::Err::BadLayout;
	std::vector<SrvFeed::PlotRecord> t6 = t5;
	t6[(size_t)1 * W + 5].plotType = 1;
	lt.onTable(t6, W, H, nullptr);
	CHECK(lt.takeResult(r) && !r.out.live && r.out.reason == Wait::Error);
	CHECK(LogHas("LOUD: live terrain: load seq 7 change") && LogHas("change fails on purpose"));
	CHECK(lt.counters().changeErrors == 1);
	c5l::Producer::s_failChange = c5r::Err::Ok;
	char line[512];
	lt.statsLine(line, sizeof(line));
	CHECK(strstr(line, "live terrain: loads 1, producers 1") != nullptr);
}

static void TestCreateFailures()
{
	struct Case
	{
		const std::wstring* lib;
		std::vector<uint8_t> constants;
		bool materials;
		const char* expect;
	} cases[] = {
	    {&g_libMissing, Constants(W, H + 4, 0), true, "Io"},
	    {&g_lib19, Constants(W, H + 4, 0), true, "BadVersion"},
	    {&g_lib20, Constants(W, H + 5, 0), true, "grid size in the constants block 8 x 7 plots, the plot table 8 x 6"},
	    {&g_lib20, std::vector<uint8_t>(100, 0), true, "constants block of 100 bytes"},
	    {&g_lib20, Constants(W, H + 4, 0), false, "tap size"},
	};
	for(const Case& k : cases)
	{
		g_lines.clear();
		LiveTerrain lt(Cfg(false, *k.lib));
		c5r::heightfield::HeightGrid g = Grid();
		std::vector<SrvFeed::PlotRecord> t = Table();
		LoadIn in = Load(3, g, t, k.constants);
		if(!k.materials)
			in.materials.clear();
		lt.onField(std::move(in), t);
		CHECK(!lt.ready() && lt.counters().createErrors == 1);
		CHECK(LogHas("LOUD: live terrain: load seq 3: the producer could not be made") && LogHas(k.expect));
		if(!LogHas(k.expect))
			printf("  (expected '%s' in the log)\n", k.expect);
		// a change in that load: an error result, the field untouched
		std::vector<SrvFeed::PlotRecord> t2 = t;
		t2[(size_t)3 * W + 4].plotType = 1;
		lt.onTable(t2, W, H, nullptr);
		Result r;
		CHECK(lt.takeResult(r) && !r.out.live && r.out.reason == Wait::Error && r.out.reasonText == "no producer for this load");
	}
	// a load whose inputs cannot be had
	g_lines.clear();
	LiveTerrain lt(Cfg(false, g_lib20));
	lt.onFieldUnavailable(4, "the EXE's grid constants block: test");
	CHECK(LogHas("LOUD: live terrain: load seq 4: no producer (the EXE's grid constants block: test)"));
	CHECK(lt.counters().createErrors == 1 && !lt.ready());
	std::vector<SrvFeed::PlotRecord> t = Table(), t2 = t;
	t2[(size_t)3 * W + 4].plotType = 1;
	lt.onTable(t, W, H, nullptr);    // no load table: the first one is the baseline (size unknown -> ignored)
	lt.onTable(t2, W, H, nullptr);
	Result r;
	CHECK(!lt.takeResult(r));
}

static void TestThreaded()
{
	g_lines.clear();
	c5l::Producer::s_createDelayMs = 300;
	c5l::Producer::s_changeDelayMs = 50;
	{
		LiveTerrain lt(Cfg(true, g_lib20));
		c5r::heightfield::HeightGrid g = Grid();
		std::vector<SrvFeed::PlotRecord> t = Table();
		const DWORD t0 = GetTickCount();
		lt.onField(Load(11, g, t, Constants(W, H + 4, 0)), t);
		CHECK(GetTickCount() - t0 < 200);   // the render thread does not wait for create
		CHECK(!lt.ready() && lt.busy());
		// three changes while create runs: merged into one job holding the newest table
		std::vector<SrvFeed::PlotRecord> a = t;
		a[(size_t)3 * W + 4].plotType = 1;
		lt.onTable(a, W, H, nullptr);
		a[(size_t)3 * W + 5].plotType = 1;
		lt.onTable(a, W, H, nullptr);
		a[(size_t)2 * W + 4].plotType = 0;
		lt.onTable(a, W, H, nullptr);
		CHECK(lt.counters().changesPosted == 3 && lt.counters().changesMerged == 2);
		CHECK(lt.waitIdle(5000));
		CHECK(lt.ready());
		Result r;
		CHECK(lt.takeResult(r) && r.out.live && r.out.changedPlots.size() == 3 && r.job == 3);
		CHECK(r.queuedMs >= r.workerMs && r.workerMs >= 40);
		CHECK(!lt.takeResult(r));
		// a new load while a change of the old one runs: its result is dropped as stale
		a[(size_t)1 * W + 6].plotType = 1;
		lt.onTable(a, W, H, nullptr);
		Sleep(25);   // the change job starts (it sleeps 50 ms in the stub)
		lt.onField(Load(12, g, t, Constants(W, H + 4, 0)), t);
		CHECK(lt.waitIdle(5000));
		while(lt.takeResult(r))
			CHECK(r.loadSeq == 12);
		CHECK(lt.counters().staleResults == 1 && lt.counters().loads == 2 && lt.counters().creates == 2);
		CHECK(lt.loadSeq() == 12 && lt.ready() && lt.counters().fieldGeneration == 0);
		// the destructor stops the worker while a create runs
		lt.onField(Load(13, g, t, Constants(W, H + 4, 0)), t);
	}
	c5l::Producer::s_createDelayMs = 0;
	c5l::Producer::s_changeDelayMs = 0;
	CHECK(true);
}

// The journal: what a player needs to rebuild every generation of a load, and a replay of it through a second producer.
static void TestJournal()
{
	g_lines.clear();
	LiveTerrain lt(Cfg(false, g_lib20));
	c5r::heightfield::HeightGrid g = Grid();
	const c5r::heightfield::HeightGrid load = g;
	std::vector<SrvFeed::PlotRecord> t = Table();
	const std::vector<uint8_t> k = Constants(W, H + 4, 0);
	lt.onField(Load(5, g, t, k), t);
	CHECK(lt.journal().open && lt.journal().loadSeq == 5 && lt.journal().constants == k && lt.journal().baseline.size() == t.size());
	std::vector<std::vector<SrvFeed::PlotRecord>> sent;
	std::vector<std::string> shas;
	auto step = [&](std::vector<SrvFeed::PlotRecord>& tab) {
		lt.onTable(tab, W, H, nullptr);
		Result r;
		if(!lt.takeResult(r))
			return;
		sent.push_back(r.sent);
		if(r.out.live)
		{
			ApplyStats st;
			std::string why;
			ApplyWindow(g, r.out.window, st, why);
			shas.push_back(RegionSha256(g, st.x0, st.y0, st.x1, st.y1));
			lt.noteApplied(r, st, 0.5, shas.back());
		}
	};
	std::vector<SrvFeed::PlotRecord> a = t;
	a[(size_t)3 * W + 4].plotType = 1;   // live
	step(a);
	a[(size_t)2 * W + 1].plotType = 0;   // next to the coast: wait (held)
	step(a);
	a[(size_t)4 * W + 5].terrain = 2;    // live (sent with (1, 2) as the producer has it)
	step(a);
	const Journal& j = lt.journal();
	CHECK(j.jobs.size() == 3 && j.generations.size() == 2);
	CHECK(j.jobs.size() == 3 && j.jobs[0].diffs.size() == 1 && j.jobs[1].diffs.size() == 1 && j.jobs[2].diffs.size() == 2);
	CHECK(j.generations.size() == 2 && j.generations[0].gen == 1 && j.generations[0].job == 1 && j.generations[1].gen == 2 &&
	      j.generations[1].job == 3 && j.generations[1].sha == shas[1]);
	// the tables rebuilt from the journal equal what the producer was given
	std::vector<SrvFeed::PlotRecord> tab = j.baseline;
	std::string why;
	for(size_t n = 0; n < j.jobs.size() && n < sent.size(); ++n)
	{
		CHECK(ApplyJournalJob(j.jobs[n], j.W, j.H, tab, why));
		CHECK(memcmp(tab.data(), sent[n].data(), tab.size() * sizeof(SrvFeed::PlotRecord)) == 0);
	}
	// a replay: a second producer from the load's copy, the constants and the baseline, each job in order
	ProducerFactory f = MakeC5rProducerFactory(g_lib20);
	std::unique_ptr<RegionProducer> p = f();
	LoadIn in;
	in.loadSeq = j.loadSeq;
	in.constants = j.constants;
	CHECK(PlotTableFromFeed(j.baseline, j.W, j.H, nullptr, in.table, why));
	in.samplesX = load.samplesX;
	in.samplesY = load.samplesY;
	in.heights = load.h;
	in.materials = load.mat;
	CHECK(p->create(in).ok());
	c5r::heightfield::HeightGrid rg = load;
	tab = j.baseline;
	size_t gi = 0;
	for(const JournalJob& job : j.jobs)
	{
		CHECK(ApplyJournalJob(job, j.W, j.H, tab, why));
		PlotTableIn pt;
		CHECK(PlotTableFromFeed(tab, j.W, j.H, nullptr, pt, why));
		ChangeOut o;
		CHECK(p->change(pt, o).ok());
		const bool listed = gi < j.generations.size() && j.generations[gi].job == job.id;
		CHECK(o.live == listed);
		if(o.live && listed)
		{
			ApplyStats st;
			CHECK(ApplyWindow(rg, o.window, st, why));
			CHECK(RegionSha256(rg, st.x0, st.y0, st.x1, st.y1) == j.generations[gi].sha);
			gi++;
		}
	}
	CHECK(gi == j.generations.size() && rg.h == g.h && rg.mat == g.mat);
	// a bad journal job is refused
	JournalJob bad;
	bad.id = 99;
	bad.diffs.push_back(std::make_pair((uint32_t)(W * H), SrvFeed::PlotRecord()));
	CHECK(!ApplyJournalJob(bad, W, H, tab, why) && why.find("outside the table") != std::string::npos);
	std::vector<SrvFeed::PlotRecord> shortTab(3);
	CHECK(!ApplyJournalJob(j.jobs[0], W, H, shortTab, why));
	// a new load starts a new journal; a load without inputs has a closed one
	lt.onField(Load(6, load, t, k), t);
	CHECK(lt.journal().loadSeq == 6 && lt.journal().jobs.empty() && lt.journal().generations.empty());
	lt.onFieldUnavailable(7, "test");
	CHECK(!lt.journal().open && lt.journal().loadSeq == 7);
}

// Merged changes leave one journal job holding every change (the queued job's entry is replaced).
static void TestJournalMerged()
{
	c5l::Producer::s_createDelayMs = 200;
	{
		LiveTerrain lt(Cfg(true, g_lib20));
		c5r::heightfield::HeightGrid g = Grid();
		std::vector<SrvFeed::PlotRecord> t = Table();
		lt.onField(Load(21, g, t, Constants(W, H + 4, 0)), t);
		std::vector<SrvFeed::PlotRecord> a = t;
		a[(size_t)3 * W + 4].plotType = 1;
		lt.onTable(a, W, H, nullptr);
		a[(size_t)3 * W + 5].plotType = 1;
		lt.onTable(a, W, H, nullptr);
		CHECK(lt.waitIdle(5000));
		const Journal& j = lt.journal();
		CHECK(lt.counters().changesMerged == 1);
		CHECK(j.jobs.size() == 1 && j.jobs[0].id == 2 && j.jobs[0].diffs.size() == 2);
		Result r;
		CHECK(lt.takeResult(r) && r.job == 2 && r.out.live);
		// a third change after the merged job ran: its diff is against the merged table
		a[(size_t)1 * W + 5].plotType = 1;
		lt.onTable(a, W, H, nullptr);
		CHECK(lt.waitIdle(5000));
		CHECK(j.jobs.size() == 2 && j.jobs[1].id == 3 && j.jobs[1].diffs.size() == 1);
	}
	c5l::Producer::s_createDelayMs = 0;
}

// The bundle's text form of the journal and the player's Replay over it.
static void TestBundleText()
{
	g_lines.clear();
	LiveTerrain lt(Cfg(false, g_lib20));
	c5r::heightfield::HeightGrid g = Grid();
	const c5r::heightfield::HeightGrid load = g;
	std::vector<SrvFeed::PlotRecord> t = Table();
	t[(size_t)5 * W + 7].feature = 3;   // a named feature in the baseline
	const FeatureNameFn nm = [](int32_t id) { return id == 3 ? std::string("FEATURE_MARSH") : std::string(); };
	lt.onField(Load(9, g, t, Constants(W, H + 4, 0), nm), t);
	std::vector<SrvFeed::PlotRecord> a = t;
	std::vector<std::vector<Rect>> liveRects;   // ticket 0587: the rectangles of each window written, per generation
	auto step = [&]() {
		lt.onTable(a, W, H, nm);
		Result r;
		if(lt.takeResult(r) && r.out.live)
		{
			ApplyStats st;
			std::string why;
			ApplyWindow(g, r.out.window, st, why);
			lt.noteApplied(r, st, 0.1, RegionSha256(g, st.x0, st.y0, st.x1, st.y1));
			liveRects.push_back(r.out.window);
		}
	};
	a[(size_t)3 * W + 4].plotType = 1;
	step();
	a[(size_t)2 * W + 1].plotType = 0;   // waits (coast)
	step();
	a[(size_t)4 * W + 5].plotType = 0;
	step();
	const Journal& j = lt.journal();
	CHECK(j.generations.size() == 2);
	std::vector<FrameGen> frames = {{100, 0}, {104, 1}, {109, 2}};
	const std::string text = EncodeSpanText(j, frames, nm);
	CHECK(text.compare(0, 4, "lj1 ") == 0);
	Journal d;
	std::vector<FrameGen> df;
	std::string why;
	CHECK(DecodeSpanText(text, d, df, why));
	if(!why.empty())
		printf("  decode: %s\n", why.c_str());
	CHECK(d.loadSeq == 9 && d.W == W && d.H == H && d.constants == j.constants && d.jobs.size() == j.jobs.size() &&
	      d.generations.size() == 2 && df.size() == 3 && df[2].frameId == 109 && df[2].gen == 2);
	CHECK(d.baseline.size() == j.baseline.size() &&
	      memcmp(d.baseline.data(), j.baseline.data(), j.baseline.size() * sizeof(SrvFeed::PlotRecord)) == 0);
	CHECK(d.generations.size() == 2 && d.generations[1].sha == j.generations[1].sha && d.generations[1].x1 == j.generations[1].x1);
	CHECK(d.featureNames.size() == 4 && d.featureNames[3] == "FEATURE_MARSH" && d.featureNames[0].empty());
	CHECK(Replay::GenerationAt(df, 99) == 0 && Replay::GenerationAt(df, 100) == 0 && Replay::GenerationAt(df, 105) == 1 &&
	      Replay::GenerationAt(df, 500) == 2);
	// the replay: every generation equal to the live field
	Replay rp;
	ProducerFactory f = MakeC5rProducerFactory(g_lib20);
	c5r::heightfield::HeightGrid rg = load;
	CHECK(rp.init(f, d, rg).ok());
	bool changed = false;
	std::vector<Rect> rr;   // ticket 0587: the player's region step takes the rectangles of the windows written
	CHECK(rp.advanceTo(0, rg, changed, nullptr, &rr).ok() && !changed && rr.empty());
	CHECK(rp.advanceTo(1, rg, changed, nullptr, &rr).ok() && changed && rp.generation() == 1);
	auto sameRects = [](const std::vector<Rect>& got, const std::vector<Rect>& want) {
		if(got.size() != want.size())
			return false;
		for(size_t i = 0; i < got.size(); ++i)
			if(got[i].x0 != want[i].x0 || got[i].y0 != want[i].y0 || got[i].w != want[i].w || got[i].h != want[i].h ||
			   !got[i].heights.empty() || !got[i].materials.empty())
				return false;
		return true;
	};
	CHECK(liveRects.size() == 2 && !liveRects[0].empty() && sameRects(rr, liveRects[0]));
	rr.clear();
	CHECK(rp.advanceTo(2, rg, changed, nullptr, &rr).ok() && changed && rp.generation() == 2);
	CHECK(liveRects.size() == 2 && sameRects(rr, liveRects[1]));
	CHECK(rg.h == g.h && rg.mat == g.mat);
	rr.clear();
	CHECK(rp.advanceTo(2, rg, changed, nullptr, &rr).ok() && !changed && rr.empty());
	c5r::Status st = rp.advanceTo(3, rg, changed);
	CHECK(!st.ok() && st.code == c5r::Err::NotFound);
	// a tampered hash is a MISMATCH, not a tolerance
	Journal bad = d;
	bad.generations[1].sha[0] = bad.generations[1].sha[0] == 'a' ? 'b' : 'a';
	Replay rb;
	c5r::heightfield::HeightGrid bg = load;
	CHECK(rb.init(f, bad, bg).ok());
	CHECK(rb.advanceTo(1, bg, changed).ok());
	st = rb.advanceTo(2, bg, changed);
	CHECK(!st.ok() && st.msg.find("MISMATCH") != std::string::npos);
	// a field of another map
	c5r::heightfield::HeightGrid other = load;
	other.mapW = W + 1;
	CHECK(!rb.init(f, d, other).ok());
	// refusals of the text form
	CHECK(!DecodeSpanText("lj2 AAAA", d, df, why) && why.find("does not start") != std::string::npos);
	CHECK(!DecodeSpanText("lj1 A*AA", d, df, why) && why.find("base64") != std::string::npos);
	CHECK(!DecodeSpanText(text.substr(0, text.size() - 8), d, df, why));
	const std::string noMagic = EncodeSpanText(Journal(), {}, nullptr);
	CHECK(!DecodeSpanText(noMagic, d, df, why));   // W = H = 0: header out of range
	std::vector<FrameGen> back = {{100, 1}, {99, 2}};
	CHECK(!DecodeSpanText(EncodeSpanText(j, back, nullptr), d, df, why) && why.find("frame") != std::string::npos);
	std::vector<FrameGen> tooHigh = {{100, 3}};
	CHECK(!DecodeSpanText(EncodeSpanText(j, tooHigh, nullptr), d, df, why));
	Journal gap = j;
	gap.generations[1].gen = 5;
	CHECK(!DecodeSpanText(EncodeSpanText(gap, frames, nullptr), d, df, why) && why.find("generation") != std::string::npos);
	// the checker's rules (srv_scene_bundle.cpp's CheckBundle calls CheckLiveTerrainConfig)
	std::map<std::string, std::string> rc;
	std::vector<std::string> notes;
	bool missing = false;
	CHECK(CheckLiveTerrainConfig(rc, notes, why, missing));   // a bundle from before 4907
	rc["render_live_terrain"] = "0";
	CHECK(CheckLiveTerrainConfig(rc, notes, why, missing));
	rc["render_live_terrain"] = "2";
	CHECK(!CheckLiveTerrainConfig(rc, notes, why, missing) && !missing && why.find("not 0 or 1") != std::string::npos);
	rc["render_live_terrain"] = "1";
	CHECK(!CheckLiveTerrainConfig(rc, notes, why, missing) && missing);
	notes.push_back(std::string(kLiveTerrainNote) + ": test");
	CHECK(CheckLiveTerrainConfig(rc, notes, why, missing));
	notes.clear();
	rc["render_live_terrain_journal"] = text;
	CHECK(CheckLiveTerrainConfig(rc, notes, why, missing));
	rc["render_live_terrain_journal"] = "lj1 AAAA";
	CHECK(!CheckLiveTerrainConfig(rc, notes, why, missing) && !missing && why.find("journal") != std::string::npos);
	rc["render_live_terrain"] = "0";
	rc["render_live_terrain_journal"] = text;
	CHECK(!CheckLiveTerrainConfig(rc, notes, why, missing) && why.find("while render_live_terrain is not 1") != std::string::npos);
}

// ticket 4912: base64 both ways for the trailer's negative cases (the module's own coder is internal)
static std::string TB64(const std::vector<uint8_t>& b)
{
	static const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string s;
	for(size_t i = 0; i < b.size(); i += 3)
	{
		const uint32_t v = (uint32_t)b[i] << 16 | (i + 1 < b.size() ? (uint32_t)b[i + 1] << 8 : 0) | (i + 2 < b.size() ? b[i + 2] : 0);
		s += a[v >> 18 & 63];
		s += a[v >> 12 & 63];
		s += i + 1 < b.size() ? a[v >> 6 & 63] : '=';
		s += i + 2 < b.size() ? a[v & 63] : '=';
	}
	return s;
}
static std::vector<uint8_t> TUnB64(const std::string& s)
{
	std::vector<uint8_t> out;
	uint32_t v = 0;
	int n = 0;
	for(char c : s)
	{
		int d = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1;
		if(d < 0)
			continue;
		v = v << 6 | (uint32_t)d;
		if(++n == 4)
		{
			out.push_back((uint8_t)(v >> 16));
			out.push_back((uint8_t)(v >> 8));
			out.push_back((uint8_t)v);
			v = 0;
			n = 0;
		}
	}
	if(n == 3)
	{
		out.push_back((uint8_t)(v >> 10));
		out.push_back((uint8_t)(v >> 2));
	}
	else if(n == 2)
		out.push_back((uint8_t)(v >> 4));
	return out;
}

// Ticket 4912: the map seed from LoadIn into the journal, its trailer in the span text, a journal without it as before
// 4912, the trailer's refusals, and the source registration (none in a test binary).
static void TestMapSeed()
{
	CHECK(GetMapSeedSource() == nullptr);   // only server.exe registers one (server/src/srv_liveterrain_seed.cpp)
	struct S
	{
		static bool Read(uint32_t& s, std::string&)
		{
			s = 7;
			return true;
		}
	};
	SetMapSeedSource(&S::Read);
	uint32_t got = 0;
	std::string w;
	CHECK(GetMapSeedSource() == &S::Read && GetMapSeedSource()(got, w) && got == 7);
	SetMapSeedSource(nullptr);
	CHECK(GetMapSeedSource() == nullptr);

	g_lines.clear();
	LiveTerrain lt(Cfg(false, g_lib20));
	c5r::heightfield::HeightGrid g = Grid();
	std::vector<SrvFeed::PlotRecord> t = Table();
	LoadIn in = Load(3, g, t, Constants(W, H + 4, 0));
	in.haveMapSeed = true;
	in.mapSeed = 0x3DFC9816u;   // the Tiny save's (c5h-9028-tiny-t0008's pregame, live-terrain.md 6.9)
	lt.onField(std::move(in), t);
	const Journal& j = lt.journal();
	CHECK(j.haveMapSeed && j.mapSeed == 0x3DFC9816u);
	const std::vector<FrameGen> frames = {{10, 0}};
	const std::string withSeed = EncodeSpanText(j, frames, nullptr);
	Journal noSeed = j;
	noSeed.haveMapSeed = false;
	noSeed.mapSeed = 0x3DFC9816u;   // ignored without the flag
	const std::string without = EncodeSpanText(noSeed, frames, nullptr);
	CHECK(withSeed.compare(0, 4, "lj1 ") == 0 && without.compare(0, 4, "lj1 ") == 0);
	const std::vector<uint8_t> bs = TUnB64(withSeed.substr(4)), bn = TUnB64(without.substr(4));
	// the trailer is the only difference: 12 bytes "SEED", 1, the seed after the bytes of the form before 4912
	CHECK(bs.size() == bn.size() + 12 && std::equal(bn.begin(), bn.end(), bs.begin()));
	CHECK(bs.size() >= 12 && memcmp(&bs[bs.size() - 12], "SEED\x01\x00\x00\x00\x16\x98\xFC\x3D", 12) == 0);
	CHECK(TB64(bn) == without.substr(4));   // the test's coder agrees with the module's
	Journal d;
	std::vector<FrameGen> df;
	std::string why;
	CHECK(DecodeSpanText(withSeed, d, df, why) && d.haveMapSeed && d.mapSeed == 0x3DFC9816u && df.size() == 1);
	CHECK(DecodeSpanText(without, d, df, why) && !d.haveMapSeed && d.mapSeed == 0);
	// refusals: another tag, another version, a cut trailer, bytes after the trailer
	std::vector<uint8_t> b = bs;
	b[b.size() - 12] = 'X';
	CHECK(!DecodeSpanText("lj1 " + TB64(b), d, df, why) && why.find("trailer") != std::string::npos);
	b = bs;
	b[b.size() - 8] = 2;
	CHECK(!DecodeSpanText("lj1 " + TB64(b), d, df, why) && why.find("trailer") != std::string::npos);
	b.assign(bs.begin(), bs.end() - 4);
	CHECK(!DecodeSpanText("lj1 " + TB64(b), d, df, why) && why.find("truncated") != std::string::npos);
	b = bs;
	b.push_back(0);
	CHECK(!DecodeSpanText("lj1 " + TB64(b), d, df, why) && why.find("after its end") != std::string::npos);
	// the checker takes both forms
	std::map<std::string, std::string> rc;
	std::vector<std::string> notes;
	bool missing = false;
	rc["render_live_terrain"] = "1";
	rc["render_live_terrain_journal"] = withSeed;
	CHECK(CheckLiveTerrainConfig(rc, notes, why, missing));
	rc["render_live_terrain_journal"] = without;
	CHECK(CheckLiveTerrainConfig(rc, notes, why, missing));
	// the replay keeps the journal's seed (its producer's LoadIn)
	Replay rp;
	c5r::heightfield::HeightGrid rg = Grid();
	CHECK(DecodeSpanText(withSeed, d, df, why) && rp.init(MakeC5rProducerFactory(g_lib20), d, rg).ok());
	// a load without a seed source: the journal carries none
	LiveTerrain lt2(Cfg(false, g_lib20));
	lt2.onField(Load(4, g, t, Constants(W, H + 4, 0)), t);
	CHECK(!lt2.journal().haveMapSeed && EncodeSpanText(lt2.journal(), frames, nullptr) != withSeed);

	// the MC's #8552 condition 1: a load without the seed never draws a land/water change live (the adapter's wait,
	// the producer not asked); the same change with the seed reaches the producer (the stub draws it)
	auto landWater = [&](bool seeded, Result& r) {
		LiveTerrain l(Cfg(false, g_lib20));
		LoadIn li = Load(5, g, t, Constants(W, H + 4, 0));
		li.haveMapSeed = seeded;
		li.mapSeed = seeded ? 0x067F5502u : 0;
		l.onField(std::move(li), t);
		std::vector<SrvFeed::PlotRecord> a = t;
		a[(size_t)2 * W + 0] = a[(size_t)2 * W + 2];   // (0,2): coast water to flat grass
		a[(size_t)3 * W + 3].plotType = 1;             // and a land plot to hills in the same table
		l.onTable(a, W, H, nullptr);
		return l.takeResult(r);
	};
	Result r0, r1;
	CHECK(landWater(false, r0) && !r0.out.live && r0.out.reason == Wait::Water &&
	      r0.out.reasonText.find("no map seed") != std::string::npos);
	bool has02 = false, has33 = false;
	for(const std::pair<int32_t, int32_t>& p : r0.out.changedPlots)
	{
		has02 = has02 || (p.first == 0 && p.second == 2);
		has33 = has33 || (p.first == 3 && p.second == 3);
	}
	CHECK(has02 && has33 && r0.out.changedPlots.size() == 2);   // every plot that differs is held, as the producer's waits
	CHECK(landWater(true, r1) && r1.out.reasonText.find("no map seed") == std::string::npos);

	// DecideMapSeed: the four outcomes
	bool noted = false;
	MapSeedVerdict v = DecideMapSeed(3, true, true, 0x3DFC9816u, "", true, noted);
	CHECK(v.have && !v.loud && !v.report && !noted && v.line.find("1039964182 (0x3DFC9816)") != std::string::npos);
	v = DecideMapSeed(3, false, false, 0, "", false, noted);   // a test or tool binary: the silent default
	CHECK(!v.have && !v.loud && !v.report && !noted && v.line.find("not server.exe") != std::string::npos);
	v = DecideMapSeed(3, false, false, 0, "", true, noted);    // server.exe without the reader: a fault, LOUD + report once
	CHECK(!v.have && v.loud && v.report && noted && v.line.compare(0, 6, "LOUD: ") == 0 && v.line.find("build fault") != std::string::npos);
	v = DecideMapSeed(4, false, false, 0, "", true, noted);    // the next load: a plain line, no second report
	CHECK(!v.have && !v.loud && !v.report && v.line.find("the fault above") != std::string::npos);
	noted = false;
	v = DecideMapSeed(5, true, false, 0, "no module exports DllGetGameContext", true, noted);   // the reader failed
	CHECK(!v.have && v.loud && v.report && v.line.find("no module exports") != std::string::npos);
	v = DecideMapSeed(6, true, true, 0, "", true, noted);      // a seed of 0 is not a seed: LOUD again, no second report
	CHECK(!v.have && v.loud && !v.report && v.line.find("is 0") != std::string::npos);
	CHECK(!HostIsServerExe());   // a test binary exports none of server.exe's host functions
	CHECK(GetMapSeedFaultReport() == nullptr);
}

// Ticket 4914 (1): the land/water step turned on through the factory's enabler (server.exe's reads the install; here a
// stand-in): called after create only with the seed and LoadIn::landWater; the journal's mark and its trailer; a failure
// makes land/water changes wait with LandWaterInputs while other changes go on; the replay calls it only on the mark.
static void TestLandWater()
{
	struct Calls
	{
		int n = 0;
		uint32_t seed = 0;
		bool ok = true;
	};
	std::shared_ptr<Calls> calls = std::make_shared<Calls>();
	const LandWaterEnabler en = [calls](c5r::liveterrain::Producer&, uint32_t seed, std::string& why) {
		calls->n++;
		calls->seed = seed;
		if(!calls->ok)
			why = "coaststyles.xml: no file named 'coaststyles.xml' (test)";
		return calls->ok;
	};
	auto cfg = [&]() {
		LiveTerrain::Config c = Cfg(false, g_lib20);
		c.factory = MakeC5rProducerFactory(g_lib20, en);
		return c;
	};
	c5r::heightfield::HeightGrid g = Grid();
	const std::vector<SrvFeed::PlotRecord> t = Table();
	std::vector<SrvFeed::PlotRecord> lw = t;
	lw[(size_t)2 * W + 0] = lw[(size_t)2 * W + 2];   // (0,2): coast water to flat grass
	std::vector<SrvFeed::PlotRecord> hills = t;
	hills[(size_t)3 * W + 3].plotType = 1;           // (3,3): a land plot to hills
	auto seeded = [&](uint32_t seq, bool seed, bool want) {
		LoadIn li = Load(seq, g, t, Constants(W, H + 4, 0));
		li.haveMapSeed = seed;
		li.mapSeed = seed ? 0x067F5502u : 0;
		li.landWater = want;
		return li;
	};

	// on: the enabler is called once, with the load's seed; the mark reaches the journal and its trailer
	{
		g_lines.clear();
		LiveTerrain l(cfg());
		l.onField(seeded(11, true, true), t);
		CHECK(calls->n == 1 && calls->seed == 0x067F5502u && LogHas("land/water changes can go live"));
		Result r;
		CHECK(!l.takeResult(r) && l.journal().landWater && l.journal().haveMapSeed);   // the mark, copied on the render thread
		l.onTable(lw, W, H, nullptr);
		CHECK(l.takeResult(r) && r.out.reason != Wait::LandWaterInputs && r.out.reasonText.find("no map seed") == std::string::npos);
		const std::vector<FrameGen> frames = {{10, 0}};
		const std::string text = EncodeSpanText(l.journal(), frames, nullptr);
		Journal off = l.journal();
		off.landWater = false;
		const std::string textOff = EncodeSpanText(off, frames, nullptr);
		const std::vector<uint8_t> bon = TUnB64(text.substr(4)), boff = TUnB64(textOff.substr(4));
		// the mark is the only difference: 8 bytes "LWAT", 1 after the seed's trailer
		CHECK(bon.size() == boff.size() + 8 && std::equal(boff.begin(), boff.end(), bon.begin()));
		CHECK(bon.size() >= 8 && memcmp(&bon[bon.size() - 8], "LWAT\x01\x00\x00\x00", 8) == 0);
		Journal d;
		std::vector<FrameGen> df;
		std::string why;
		CHECK(DecodeSpanText(text, d, df, why) && d.landWater && d.haveMapSeed && d.mapSeed == 0x067F5502u);
		CHECK(DecodeSpanText(textOff, d, df, why) && !d.landWater);
		// refusals: another tag after the seed's, the mark without the seed's trailer before it, a cut mark
		std::vector<uint8_t> b = bon;
		b[b.size() - 8] = 'X';
		CHECK(!DecodeSpanText("lj1 " + TB64(b), d, df, why) && why.find("land/water") != std::string::npos);
		b = bon;
		b[b.size() - 4] = 2;
		CHECK(!DecodeSpanText("lj1 " + TB64(b), d, df, why) && why.find("land/water") != std::string::npos);
		b.assign(bon.begin(), bon.end() - 4);
		CHECK(!DecodeSpanText("lj1 " + TB64(b), d, df, why) && why.find("after its end") != std::string::npos);
		Journal noSeed = l.journal();
		noSeed.haveMapSeed = false;   // the encoder writes the mark only with the seed
		CHECK(DecodeSpanText(EncodeSpanText(noSeed, frames, nullptr), d, df, why) && !d.landWater && !d.haveMapSeed);
		std::vector<uint8_t> markOnly = TUnB64(EncodeSpanText(noSeed, frames, nullptr).substr(4));
		const uint8_t lwat[8] = {'L', 'W', 'A', 'T', 1, 0, 0, 0};
		markOnly.insert(markOnly.end(), lwat, lwat + 8);
		CHECK(!DecodeSpanText("lj1 " + TB64(markOnly), d, df, why));
		// the replay calls the enabler only on the mark (a journal without it replays what its live server did)
		const int before = calls->n;
		Replay rp;
		c5r::heightfield::HeightGrid rg = Grid();
		CHECK(DecodeSpanText(textOff, d, df, why) && rp.init(MakeC5rProducerFactory(g_lib20, en), d, rg).ok() && calls->n == before);
		CHECK(DecodeSpanText(text, d, df, why) && rp.init(MakeC5rProducerFactory(g_lib20, en), d, rg).ok() && calls->n == before + 1 &&
		      calls->seed == 0x067F5502u);
	}
	// not called: no seed (the 4912 wait, as before), the switch off, a factory without an enabler
	{
		const int before = calls->n;
		LiveTerrain a(cfg());
		a.onField(seeded(12, false, true), t);
		LiveTerrain b(cfg());
		b.onField(seeded(13, true, false), t);
		LiveTerrain c(Cfg(false, g_lib20));
		c.onField(seeded(14, true, true), t);
		Result r;
		CHECK(calls->n == before && !a.takeResult(r) && !b.takeResult(r) && !c.takeResult(r));
		CHECK(!a.journal().landWater && !b.journal().landWater && !c.journal().landWater);
		a.onTable(lw, W, H, nullptr);
		CHECK(a.takeResult(r) && !r.out.live && r.out.reason == Wait::Water && r.out.reasonText.find("no map seed") != std::string::npos);
	}
	// the failure path: the land/water change waits with its own reason; a height change still goes live; no mark
	{
		calls->ok = false;
		g_lines.clear();
		LiveTerrain l(cfg());
		l.onField(seeded(15, true, true), t);
		Result r;
		CHECK(l.ready() && !l.takeResult(r) && !l.journal().landWater && !LogHas("land/water changes can go live"));
		l.onTable(lw, W, H, nullptr);
		CHECK(l.takeResult(r) && !r.out.live && r.out.reason == Wait::LandWaterInputs &&
		      r.out.reasonText.find("the land/water step is off for this load") != std::string::npos &&
		      r.out.reasonText.find("no file named 'coaststyles.xml'") != std::string::npos);
		CHECK(LogHas("wait for reload: LandWaterInputs"));
		CHECK(std::string(WaitName(Wait::LandWaterInputs)) == "LandWaterInputs");
		LiveTerrain h(cfg());
		h.onField(seeded(16, true, true), t);
		h.onTable(hills, W, H, nullptr);
		CHECK(h.takeResult(r) && r.out.live && r.out.reason == Wait::None);
		calls->ok = true;
	}
}

static int CountLines(const char* s)
{
	int n = 0;
	for(const std::string& l : g_lines)
		n += l.find(s) != std::string::npos ? 1 : 0;
	return n;
}

// MC #9538 item 1 (live-terrain.md 8.10.3): a change whose coast met zero-length segments (stepped over) or refused strip
// triangles gets ONE LOUD line, live or not; a change without them gets none.
static void TestCoastDegenerate()
{
	g_lines.clear();
	LiveTerrain lt(Cfg(false, g_lib20));
	c5r::heightfield::HeightGrid g = Grid();
	std::vector<SrvFeed::PlotRecord> t = Table();
	lt.onField(Load(21, g, t, Constants(W, H + 4, 0)), t);
	c5l::ChangeResult& k = c5l::Producer::s_landWaterCounts;
	k = c5l::ChangeResult();
	k.coastDegenerate = 2;
	k.stripTrianglesRefused = 3;
	std::vector<SrvFeed::PlotRecord> a = t;
	a[(size_t)3 * W + 4].plotType = 1;
	lt.onTable(a, W, H, nullptr);
	Result r;
	CHECK(lt.takeResult(r) && r.out.live && r.out.coastDegenerate == 2 && r.out.stripTrianglesRefused == 3);
	CHECK(CountLines("LOUD: live terrain: load seq 21 change 1: the coast had 2 zero-length segment(s) or tangent(s)") == 1);
	CHECK(LogHas("3 strip triangle(s) refused") && LogHas("the change is drawn without them"));
	// a waiting change with them: the line too, saying it is not drawn
	g_lines.clear();
	std::vector<SrvFeed::PlotRecord> b = a;
	b[(size_t)2 * W + 1].plotType = 0;   // next to the coast column: the stub's Water
	lt.onTable(b, W, H, nullptr);
	CHECK(lt.takeResult(r) && !r.out.live && r.out.reason == Wait::Water);
	CHECK(CountLines("zero-length segment(s)") == 1 && LogHas("the change is not drawn"));
	// none: no line
	k = c5l::ChangeResult();
	g_lines.clear();
	std::vector<SrvFeed::PlotRecord> c = a;
	c[(size_t)1 * W + 5].plotType = 1;
	lt.onTable(c, W, H, nullptr);
	CHECK(lt.takeResult(r) && r.out.live && r.out.coastDegenerate == 0 && CountLines("zero-length") == 0);
}

// MC #9538 item 2 (live-terrain.md 8.17): the producer's coast counts reach ChangeOut, and a change with any of the named,
// not drawn parts gets one plain (not LOUD) line; a change without them gets none
static void TestCoastCounts()
{
	g_lines.clear();
	LiveTerrain lt(Cfg(false, g_lib20));
	c5r::heightfield::HeightGrid g = Grid();
	std::vector<SrvFeed::PlotRecord> t = Table();
	lt.onField(Load(22, g, t, Constants(W, H + 4, 0)), t);
	c5l::ChangeResult& k = c5l::Producer::s_landWaterCounts;
	k = c5l::ChangeResult();
	k.landWater = true;
	k.coastLinesRedrawn = 2;
	k.coastLinesBeyondWindow = 1;
	k.coastLinesReloadMoves = 5;
	k.samplesWonderKept = 77;
	std::vector<SrvFeed::PlotRecord> a = t;
	a[(size_t)3 * W + 4].plotType = 1;
	lt.onTable(a, W, H, nullptr);
	Result r;
	CHECK(lt.takeResult(r) && r.out.live && r.out.landWater && r.out.coastLinesRedrawn == 2 && r.out.coastLinesBeyondWindow == 1 &&
	      r.out.coastLinesReloadMoves == 5 && r.out.samplesWonderKept == 77);
	CHECK(CountLines("live terrain: load seq 22 change 1: coast (live): 2 line(s) redrawn in the window, 1 of them also reach beyond it; "
	                 "5 unchanged line(s) a reload would re-jitter") == 1);
	CHECK(LogHas("77 sample(s) kept on natural wonders' hexes") && CountLines("LOUD:") == 0);
	// one count alone is enough for the line
	k.coastLinesBeyondWindow = 0;
	k.coastLinesReloadMoves = 0;
	k.samplesWonderKept = 3;
	g_lines.clear();
	std::vector<SrvFeed::PlotRecord> b = a;
	b[(size_t)1 * W + 5].plotType = 1;
	lt.onTable(b, W, H, nullptr);
	CHECK(lt.takeResult(r) && r.out.live && CountLines("coast (live)") == 1 && LogHas("3 sample(s) kept"));
	// redrawn lines alone (all inside the window) and no other count: no line
	k.samplesWonderKept = 0;
	g_lines.clear();
	std::vector<SrvFeed::PlotRecord> c = b;
	c[(size_t)4 * W + 5].plotType = 1;
	lt.onTable(c, W, H, nullptr);
	CHECK(lt.takeResult(r) && r.out.live && r.out.coastLinesRedrawn == 2 && CountLines("coast (") == 0);
	k = c5l::ChangeResult();
}

// MC #9538 item 4: with the land/water step not wanted (C5_RENDER_LIVE_LANDWATER=0) no seed is read (one plain line, no
// LOUD, no report) and a land/water change reaches the producer, which waits with its own Water as before epic 0615 (not
// the 4912 "no map seed" wait); height changes go live as before.
static void TestLandWaterOff()
{
	const MapSeedVerdict v = MapSeedNotWanted(9);
	CHECK(!v.have && !v.loud && !v.report && v.line.compare(0, 6, "LOUD: ") != 0 &&
	      v.line.find("load seq 9: map seed not read (C5_RENDER_LIVE_LANDWATER=0") != std::string::npos);
	c5r::heightfield::HeightGrid g = Grid();
	const std::vector<SrvFeed::PlotRecord> t = Table();
	std::vector<SrvFeed::PlotRecord> lw = t;
	lw[(size_t)2 * W + 0] = lw[(size_t)2 * W + 2];   // (0,2): coast water to flat grass
	auto run = [&](bool want, const std::vector<SrvFeed::PlotRecord>& table, Result& r) {
		LiveTerrain l(Cfg(false, g_lib20));
		LoadIn li = Load(30, g, t, Constants(W, H + 4, 0));
		li.haveMapSeed = false;   // the scene reads none with the step off
		li.landWater = want;
		l.onField(std::move(li), t);
		l.onTable(table, W, H, nullptr);
		return l.takeResult(r);
	};
	Result r;
	CHECK(run(false, lw, r) && !r.out.live && r.out.reason == Wait::Water && r.out.reasonText.find("no map seed") == std::string::npos &&
	      r.out.reasonText.find("is water") != std::string::npos);   // the stub producer's own Water
	CHECK(run(true, lw, r) && !r.out.live && r.out.reason == Wait::Water && r.out.reasonText.find("no map seed") != std::string::npos);
	std::vector<SrvFeed::PlotRecord> hills = t;
	hills[(size_t)3 * W + 3].plotType = 1;
	CHECK(run(false, hills, r) && r.out.live);
}

// Ticket 0717, rule A: the journal's river band mark ("RBND", u32 1 after the feature names, before the seed's trailer).
// The live scene writes it always and its producer draws with the band and the land region; a journal without it (every
// bundle before 0717) replays the producer before 0717 (Options riverKeepDisc and landRegion false, the stub's
// s_lastOptions). riverEdgesFromAnyFlag only for a table with records that carried no edges (before 0631).
static void TestRiverBandMark()
{
	typedef c5r::liveterrain::Producer P;
	g_lines.clear();
	LiveTerrain lt(Cfg(false, g_lib20));
	c5r::heightfield::HeightGrid g = Grid();
	const c5r::heightfield::HeightGrid load = g;
	std::vector<SrvFeed::PlotRecord> e = EdgeTable();
	lt.onField(Load(6, g, e, Constants(W, H + 4, 0)), e);
	CHECK(lt.ready() && lt.journal().riverBand);
	std::vector<SrvFeed::PlotRecord> a = e;
	a[(size_t)3 * W + 4].plotType = 0;   // flat to mountain, inland
	P::s_lastOptions = P::Options();
	P::s_lastOptions.riverKeepDisc = true;   // so the live change has to set it back
	lt.onTable(a, W, H, nullptr);
	Result r;
	CHECK(lt.takeResult(r) && r.out.live);
	CHECK(!P::s_lastOptions.riverKeepDisc && P::s_lastOptions.landRegion && !P::s_lastOptions.riverEdgesFromAnyFlag);
	{
		ApplyStats st;
		std::string why;
		ApplyWindow(g, r.out.window, st, why);
		lt.noteApplied(r, st, 0.1, RegionSha256(g, st.x0, st.y0, st.x1, st.y1));
	}
	const Journal& j = lt.journal();
	CHECK(j.generations.size() == 1 && j.riverBand);
	const std::vector<FrameGen> frames = {{10, 0}, {11, 1}};
	const std::string text = EncodeSpanText(j, frames, nullptr);
	Journal old = j;
	old.riverBand = false;
	const std::string textOld = EncodeSpanText(old, frames, nullptr);
	const std::vector<uint8_t> bon = TUnB64(text.substr(4)), boff = TUnB64(textOld.substr(4));
	// without a seed the mark is the last 8 bytes; the form without it is the journal before 0717 byte for byte
	static const uint8_t kMark[8] = {'R', 'B', 'N', 'D', 1, 0, 0, 0};
	CHECK(bon.size() == boff.size() + 8 && std::equal(boff.begin(), boff.end(), bon.begin()));
	CHECK(bon.size() >= 8 && memcmp(&bon[bon.size() - 8], kMark, 8) == 0);
	// with the seed (and the land/water mark) the mark comes before the seed's trailer, which stays at the end
	Journal seeded = j;
	seeded.haveMapSeed = true;
	seeded.mapSeed = 0x01020304u;
	seeded.landWater = true;
	static const uint8_t kTail[20] = {'S', 'E', 'E', 'D', 1, 0, 0, 0, 4, 3, 2, 1, 'L', 'W', 'A', 'T', 1, 0, 0, 0};
	const std::vector<uint8_t> bs = TUnB64(EncodeSpanText(seeded, frames, nullptr).substr(4));
	CHECK(bs.size() == bon.size() + 20 && std::equal(bon.begin(), bon.end(), bs.begin()) &&
	      memcmp(&bs[bs.size() - 20], kTail, 20) == 0);
	Journal d;
	std::vector<FrameGen> df;
	std::string why;
	CHECK(DecodeSpanText(text, d, df, why) && d.riverBand && !d.haveMapSeed);
	CHECK(DecodeSpanText(textOld, d, df, why) && !d.riverBand);
	CHECK(DecodeSpanText("lj1 " + TB64(bs), d, df, why) && d.riverBand && d.haveMapSeed && d.mapSeed == 0x01020304u && d.landWater);
	Journal seededOld = seeded;
	seededOld.riverBand = false;
	CHECK(DecodeSpanText(EncodeSpanText(seededOld, frames, nullptr), d, df, why) && !d.riverBand && d.haveMapSeed && d.landWater);
	// refusals: another version of the mark; the mark twice (the second is not the seed's trailer); a cut mark
	std::vector<uint8_t> b = bon;
	b[b.size() - 4] = 2;
	CHECK(!DecodeSpanText("lj1 " + TB64(b), d, df, why) && why.find("river band") != std::string::npos);
	b = bon;
	b.insert(b.end(), kMark, kMark + 8);
	CHECK(!DecodeSpanText("lj1 " + TB64(b), d, df, why));
	b.assign(bon.begin(), bon.end() - 4);
	CHECK(!DecodeSpanText("lj1 " + TB64(b), d, df, why));
	// the checker takes both forms
	std::map<std::string, std::string> rc;
	std::vector<std::string> notes;
	bool missing = false;
	rc["render_live_terrain"] = "1";
	rc["render_live_terrain_journal"] = text;
	CHECK(CheckLiveTerrainConfig(rc, notes, why, missing));
	rc["render_live_terrain_journal"] = textOld;
	CHECK(CheckLiveTerrainConfig(rc, notes, why, missing));
	// the replay: with the mark the band, without it the producer before 0717; both equal the live field (the stub)
	for(int withMark = 0; withMark < 2; ++withMark)
	{
		CHECK(DecodeSpanText(withMark ? text : textOld, d, df, why));
		Replay rp;
		c5r::heightfield::HeightGrid rg = load;
		CHECK(rp.init(MakeC5rProducerFactory(g_lib20), d, rg).ok());
		P::s_lastOptions = P::Options();
		P::s_lastOptions.riverKeepDisc = !withMark;
		P::s_lastOptions.landRegion = !!withMark;
		bool changed = false;
		CHECK(rp.advanceTo(1, rg, changed).ok() && changed && rg.h == g.h);
		CHECK(P::s_lastOptions.riverKeepDisc == !withMark && P::s_lastOptions.landRegion == !!withMark &&
		      !P::s_lastOptions.riverEdgesFromAnyFlag);
	}
	// a table recorded before 0631 (no record carries edges): the band reads riverEdges as the any-edge flag
	{
		LiveTerrain lo(Cfg(false, g_lib20));
		std::vector<SrvFeed::PlotRecord> o = Table();
		lo.onField(Load(7, Grid(), o, Constants(W, H + 4, 0)), o);
		std::vector<SrvFeed::PlotRecord> o2 = o;
		o2[(size_t)3 * W + 4].plotType = 0;
		P::s_lastOptions = P::Options();
		lo.onTable(o2, W, H, nullptr);
		Result ro;
		CHECK(lo.takeResult(ro) && ro.out.live && P::s_lastOptions.riverEdgesFromAnyFlag && !P::s_lastOptions.riverKeepDisc);
	}
}

// Ticket 0613 ([Extras] FixUnknownWonderBase, live-terrain.md 8.13.2 (b)): the load-time pass. The stub's unknown wonder is
// a natural wonder whose Type starts with "UNKNOWN_" (the stock cell draws it as the stub's Fuji, 40000).
static std::string WonderNames(int32_t id)
{
	return id == 30 ? "UNKNOWN_TEST_WONDER" : id == 31 ? "FEATURE_ULURU" : std::string();
}
static void PutWonder(std::vector<SrvFeed::PlotRecord>& t, uint32_t x, uint32_t y, int8_t feature)
{
	t[(size_t)y * W + x].feature = feature;
	t[(size_t)y * W + x].flags |= 0x8;   // PlotRecord::flags bit 3: a natural wonder
}
static void FujiBox(c5r::heightfield::HeightGrid& g, uint32_t x, uint32_t y)   // the EXE's bake of the stub's Fuji
{
	for(uint32_t r = (y + 2) * 64; r < (y + 3) * 64; ++r)
		for(uint32_t c = x * 64; c < (x + 1) * 64; ++c)
			g.h[(size_t)r * g.samplesX + c] = 40000;
}
static void ApplyAndNote(LiveTerrain& lt, c5r::heightfield::HeightGrid& g, const Result& r)
{
	ApplyStats st;
	std::string why;
	ApplyWindow(g, r.out.window, st, why);
	lt.noteApplied(r, st, 0.1, RegionSha256(g, st.x0, st.y0, st.x1, st.y1));
}
static void TestUnknownWonderPass()
{
	typedef c5r::liveterrain::Producer P;
	// (1) option on, an unknown wonder at (4, 3): the pass is job 1, live, the wonder's plot on its own ground
	{
		g_lines.clear();
		LiveTerrain lt(Cfg(false, g_lib20));
		c5r::heightfield::HeightGrid g = Grid();
		std::vector<SrvFeed::PlotRecord> t = Table();
		PutWonder(t, 4, 3, 30);
		FujiBox(g, 4, 3);
		const c5r::heightfield::HeightGrid load = g;
		LoadIn in = Load(8, g, t, Constants(W, H + 4, 0), WonderNames);
		in.neutralWonders = true;
		P::s_lastOptions = P::Options();
		lt.onField(std::move(in), t);
		CHECK(LogHas("load-time pass requested") && lt.journal().neutralWonders);
		CHECK(P::s_lastOptions.neutralUnknownWonders);
		Result r;
		CHECK(lt.takeResult(r) && r.job == 1 && r.out.live && r.out.changedPlots.size() == 1 && r.out.changedPlots[0].first == 4 &&
		      r.out.changedPlots[0].second == 3);
		CHECK(LogHas("load-time pass (job 1, FixUnknownWonderBase): live: 1 plot(s) re-drawn on their own ground"));
		CHECK(lt.counters().loadPasses == 1 && lt.counters().loadPassesLive == 1 && lt.counters().changesPosted == 0);
		ApplyAndNote(lt, g, r);
		CHECK(g.h[(size_t)(5 * 64 + 10) * g.samplesX + 4 * 64 + 10] == 15330);   // the wonder's plot: its own ground
		const Journal& j = lt.journal();
		CHECK(j.jobs.size() == 1 && j.jobs[0].id == 1 && j.jobs[0].diffs.empty() && j.generations.size() == 1 &&
		      j.generations[0].job == 1);
		// a later change elsewhere: the rule stays on, the wonder is not part of it
		std::vector<SrvFeed::PlotRecord> a = t;
		a[(size_t)4 * W + 6].plotType = 1;
		P::s_lastOptions = P::Options();
		lt.onTable(a, W, H, WonderNames);
		CHECK(lt.takeResult(r) && r.out.live && r.out.changedPlots.size() == 1 && r.out.changedPlots[0].first == 6 &&
		      P::s_lastOptions.neutralUnknownWonders);
		ApplyAndNote(lt, g, r);
		// the journal: the "NWON" mark only when on; decoded; the replay makes both generations through the same pass
		const std::vector<FrameGen> frames = {{1, 0}, {2, 1}, {3, 2}};
		const std::string text = EncodeSpanText(lt.journal(), frames, WonderNames);
		Journal off = lt.journal();
		off.neutralWonders = false;
		const std::vector<uint8_t> bon = TUnB64(text.substr(4)), boff = TUnB64(EncodeSpanText(off, frames, WonderNames).substr(4));
		static const uint8_t kMark[8] = {'N', 'W', 'O', 'N', 1, 0, 0, 0};
		CHECK(bon.size() == boff.size() + 8 && std::equal(boff.begin(), boff.end(), bon.begin()) &&
		      memcmp(&bon[bon.size() - 8], kMark, 8) == 0);
		Journal both = lt.journal();   // with the river band's mark (before it) and the seed's trailer (after it)
		both.riverBand = true;
		both.haveMapSeed = true;
		both.mapSeed = 77;
		Journal d;
		std::vector<FrameGen> df;
		std::string why;
		CHECK(DecodeSpanText(EncodeSpanText(both, frames, WonderNames), d, df, why) && d.neutralWonders && d.riverBand &&
		      d.haveMapSeed && d.mapSeed == 77);
		std::vector<uint8_t> bad = bon;
		bad[bad.size() - 4] = 2;
		CHECK(!DecodeSpanText("lj1 " + TB64(bad), d, df, why) && why.find("neutral wonder base") != std::string::npos);
		CHECK(DecodeSpanText(text, d, df, why) && d.neutralWonders && d.jobs.size() == 2 && d.jobs[0].diffs.empty());
		Replay rp;
		c5r::heightfield::HeightGrid rg = load;
		CHECK(rp.init(MakeC5rProducerFactory(g_lib20), d, rg).ok());
		bool changed = false;
		CHECK(rp.advanceTo(2, rg, changed).ok() && changed && rg.h == g.h && rg.mat == g.mat);
		// a replay of the same journal without the mark: the pass finds nothing, generation 1 is not made
		Journal dn = d;
		dn.neutralWonders = false;
		Replay rn;
		c5r::heightfield::HeightGrid rng = load;
		CHECK(rn.init(MakeC5rProducerFactory(g_lib20), dn, rng).ok());
		CHECK(!rn.advanceTo(1, rng, changed).ok());
	}
	// (2) option on, no unknown wonder (a known one only): no window, the rule stays on for the load
	{
		g_lines.clear();
		LiveTerrain lt(Cfg(false, g_lib20));
		c5r::heightfield::HeightGrid g = Grid();
		std::vector<SrvFeed::PlotRecord> t = Table();
		PutWonder(t, 4, 3, 31);
		LoadIn in = Load(9, g, t, Constants(W, H + 4, 0), WonderNames);
		in.neutralWonders = true;
		lt.onField(std::move(in), t);
		Result r;
		CHECK(lt.takeResult(r) && r.job == 1 && !r.out.live && r.out.reason == Wait::NoChange && r.out.window.empty());
		CHECK(LogHas("load-time pass (job 1, FixUnknownWonderBase): no unknown natural wonder in this load (no window)"));
		CHECK(lt.counters().loadPasses == 1 && lt.counters().loadPassesLive == 0 && lt.journal().generations.empty());
		// an unknown wonder placed during play: on its own ground (the stub's plot height, not Fuji's)
		std::vector<SrvFeed::PlotRecord> a = t;
		PutWonder(a, 5, 2, 30);
		lt.onTable(a, W, H, WonderNames);
		CHECK(lt.takeResult(r) && r.out.live && P::s_lastOptions.neutralUnknownWonders);
		ApplyAndNote(lt, g, r);
		CHECK(g.h[(size_t)(4 * 64 + 10) * g.samplesX + 5 * 64 + 10] == 15330);   // not the stub's Fuji (40000)
	}
	// (3) option off: no pass, no mark, the producer's rule as before 0613
	{
		g_lines.clear();
		LiveTerrain lt(Cfg(false, g_lib20));
		c5r::heightfield::HeightGrid g = Grid();
		std::vector<SrvFeed::PlotRecord> t = Table();
		PutWonder(t, 4, 3, 30);
		lt.onField(Load(10, g, t, Constants(W, H + 4, 0), WonderNames), t);
		Result r;
		CHECK(!lt.takeResult(r) && !LogHas("load-time pass") && lt.journal().jobs.empty() && !lt.journal().neutralWonders);
		CHECK(lt.counters().loadPasses == 0);
		std::vector<SrvFeed::PlotRecord> a = t;
		PutWonder(a, 5, 2, 30);   // an unknown wonder placed live: the stub's Fuji
		P::s_lastOptions = P::Options();
		P::s_lastOptions.neutralUnknownWonders = true;   // so the change has to set it back
		lt.onTable(a, W, H, WonderNames);
		CHECK(lt.takeResult(r) && r.out.live && r.out.changedPlots.size() == 1 && !P::s_lastOptions.neutralUnknownWonders);
		ApplyAndNote(lt, g, r);
		CHECK(g.h[(size_t)(4 * 64 + 10) * g.samplesX + 5 * 64 + 10] == 40000);
	}
	// (4) the pass waits (the wonder next to the coast column: the stub's Water): the EXE's rule for the rest of the load
	{
		g_lines.clear();
		LiveTerrain lt(Cfg(false, g_lib20));
		c5r::heightfield::HeightGrid g = Grid();
		std::vector<SrvFeed::PlotRecord> t = Table();
		PutWonder(t, 1, 2, 30);
		LoadIn in = Load(11, g, t, Constants(W, H + 4, 0), WonderNames);
		in.neutralWonders = true;
		lt.onField(std::move(in), t);
		Result r;
		CHECK(lt.takeResult(r) && r.job == 1 && !r.out.live && r.out.reason == Wait::Water);
		CHECK(LogHas("not drawn (Water): unknown wonders keep the EXE's Fuji base in this load"));
		std::vector<SrvFeed::PlotRecord> a = t;
		a[(size_t)4 * W + 5].plotType = 1;
		lt.onTable(a, W, H, WonderNames);
		CHECK(lt.takeResult(r) && r.out.live && r.out.changedPlots.size() == 1 && r.out.changedPlots[0].first == 5 &&
		      !P::s_lastOptions.neutralUnknownWonders);
	}
	// (5) a table that arrives while the create runs merges into the queued pass: one job, the pass's line names it
	{
		g_lines.clear();
		P::s_createDelayMs = 200;
		{
			LiveTerrain lt(Cfg(true, g_lib20));
			c5r::heightfield::HeightGrid g = Grid();
			std::vector<SrvFeed::PlotRecord> t = Table();
			PutWonder(t, 4, 3, 30);
			LoadIn in = Load(12, g, t, Constants(W, H + 4, 0), WonderNames);
			in.neutralWonders = true;
			lt.onField(std::move(in), t);
			std::vector<SrvFeed::PlotRecord> a = t;
			a[(size_t)4 * W + 6].plotType = 1;
			lt.onTable(a, W, H, WonderNames);
			CHECK(lt.counters().changesMerged == 1 && lt.counters().changesPosted == 1);
			CHECK(lt.waitIdle(5000));
			Result r;
			CHECK(lt.takeResult(r) && r.job == 2 && r.out.live && r.out.changedPlots.size() == 2);
			CHECK(LogHas("load-time pass (job 2, FixUnknownWonderBase): live: 2 plot(s)"));
			CHECK(lt.journal().jobs.size() == 1 && lt.journal().jobs[0].id == 2 && lt.journal().jobs[0].diffs.size() == 1);
			CHECK(!lt.takeResult(r));
		}
		P::s_createDelayMs = 0;
	}
}

int main(int argc, char** argv)
{
	wchar_t tmp[MAX_PATH];
	GetTempPathW(MAX_PATH, tmp);
	const std::wstring dir = std::wstring(tmp) + L"c5_test_liveterrain_" + std::to_wstring(GetCurrentProcessId());
	CreateDirectoryW(dir.c_str(), nullptr);
	g_lib20 = dir + L"\\lib20.fxsb";
	g_lib19 = dir + L"\\lib19.fxsb";
	g_libMissing = dir + L"\\missing.fxsb";
	WriteFile(g_lib20, 20);
	WriteFile(g_lib19, 19);
	CHECK(LibraryPathFor(L"C:\\Games\\Civ5") == L"C:\\Games\\Civ5\\Resource\\Common\\TerrainPiecesLibrary.fxsb");
	CHECK(LibraryPathFor(L"C:\\Games\\Civ5\\") == L"C:\\Games\\Civ5\\Resource\\Common\\TerrainPiecesLibrary.fxsb");
	CHECK(std::string(WaitName(Wait::Seam)) == "Seam" && std::string(WaitName(Wait::Error)) == "Error");
	// the feature names by art define (srv_liveterrain_features.h; the mapping is [I], checked live)
	CHECK(srvrender::LiveTerrainTypeFromArtTag("ART_DEF_FEATURE_ULURU") == "FEATURE_ULURU");
	CHECK(srvrender::LiveTerrainTypeFromArtTag("ART_DEF_FEATURE_MARSH") == "FEATURE_MARSH");
	CHECK(srvrender::LiveTerrainTypeFromArtTag("ART_DEF_FEATURE_").empty());
	CHECK(srvrender::LiveTerrainTypeFromArtTag("ART_DEF_RESOURCE_IRON").empty());
	CHECK(srvrender::LiveTerrainTypeFromArtTag(nullptr).empty());
	srvrender::LiveTerrainFeatureArtTagSeen(17, "ART_DEF_FEATURE_KILIMANJARO");
	srvrender::LiveTerrainFeatureArtTagSeen(5000, "ART_DEF_FEATURE_X");
	CHECK(srvrender::LiveTerrainFeatureType(17) == "FEATURE_KILIMANJARO" && srvrender::LiveTerrainFeatureType(16).empty() &&
	      srvrender::LiveTerrainFeatureType(5000).empty() && srvrender::LiveTerrainFeatureType(-1).empty());
	srvrender::LiveTerrainFeatureArtTagSeen(17, "SOMETHING_ELSE");
	CHECK(srvrender::LiveTerrainFeatureType(17).empty());
	srvrender::LiveTerrainFeatureTypesReset();
	struct Case
	{
		const char* name;
		void (*fn)();
	} cases[] = {{"table_and_filter", TestTableAndFilter}, {"apply_and_hash", TestApplyAndHash}, {"live_inline", TestLiveInline},
	             {"create_failures", TestCreateFailures}, {"threaded", TestThreaded}, {"journal", TestJournal},
	             {"journal_merged", TestJournalMerged}, {"bundle_text", TestBundleText}, {"map_seed", TestMapSeed},
	             {"land_water", TestLandWater}, {"coast_degenerate", TestCoastDegenerate}, {"land_water_off", TestLandWaterOff},
	             {"coast_counts", TestCoastCounts}, {"river_edges", TestRiverEdges},
	             {"river_band_mark", TestRiverBandMark}, {"unknown_wonder_pass", TestUnknownWonderPass}};
	const char* only = argc > 1 ? argv[1] : nullptr;
	bool matched = false;
	for(const Case& c : cases)
	{
		if(only && strcmp(only, c.name) != 0)
			continue;
		matched = true;
		const int f0 = g_failed;
		c.fn();
		printf("CASE %s %s\n", c.name, g_failed == f0 ? "PASS" : "FAIL");
	}
	DeleteFileW(g_lib20.c_str());
	DeleteFileW(g_lib19.c_str());
	RemoveDirectoryW(dir.c_str());
	if(only && !matched)
	{
		printf("test_liveterrain: no case matches '%s'\n", only);
		return 2;
	}
	printf("test_liveterrain: %d checks, %d failed\n", g_checks, g_failed);
	return g_failed ? 1 : 0;
}
