// test_roads_producer.cpp - unit tests of FixRoadLayout's producer in server.exe (ticket 4934, epic 0454; MC #7609 named
// this file). SPDX-License-Identifier: GPL-3.0-or-later
// Built and run by server/tests/extras/run_extras_test.py (clang-cl x64 /W4 /WX, with server/src/extras/*.cpp and the road
// generator). No GPU, no game: synthetic plot tables and tap sets. Every check has a negative counterpart (COMMON 9).
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <memory>
#include <string>
#include <vector>
#include "extras/srv_extra_roads.h"
#include "extras/srv_extras.h"
#include "feed/srv_feed.h"
#include "c5r/bake/grid.h"
#include "render/srv_cut_tap.h"   // the cut overlay (MC #7664)

// srv_cut_tap.cpp's log and clock (srv_render_common.cpp is not linked here)
namespace srvrender {
int64_t Qpc() { return 0; }
int64_t QpcFreq() { return 1; }
double QpcToMs(int64_t) { return 0; }
void RLog(const char*, ...) {}
void RNote(const char*, ...) {}
}  // namespace srvrender

using namespace srvextras;
namespace rx = c5r::extras::roads;

static int g_pass = 0, g_fail = 0;
#define CHECK(x) Check((x), #x, __LINE__)
static void Check(bool ok, const char* what, int line)
{
	if(ok)
		g_pass++;
	else
	{
		g_fail++;
		printf("FAIL line %d: %s\n", line, what);
	}
}

// the registry: srv_render_scene.cpp holds it in server.exe; this program links no scene
static SceneExtrasFactory g_xf = nullptr;
static SceneRoadsFactory g_rf = nullptr;
namespace srvextras {
void RegisterSceneExtrasFactory(SceneExtrasFactory f) { g_xf = f; }
std::unique_ptr<SceneExtras> MakeSceneExtras() { return g_xf ? g_xf() : std::unique_ptr<SceneExtras>(); }
void RegisterSceneRoadsFactory(SceneRoadsFactory f) { g_rf = f; }
std::unique_ptr<SceneRoads> MakeSceneRoads() { return g_rf ? g_rf() : std::unique_ptr<SceneRoads>(); }
}  // namespace srvextras

struct Log
{
	std::vector<std::string> lines;
	static void Fn(void* ctx, const char* line) { static_cast<Log*>(ctx)->lines.push_back(line); }
	int count(const char* s) const
	{
		int n = 0;
		for(const std::string& l : lines)
			n += l.find(s) != std::string::npos ? 1 : 0;
		return n;
	}
};

const uint32_t W = 12, H = 10;

SrvFeed::PlotRecord Blank()
{
	SrvFeed::PlotRecord r;
	memset(&r, 0, sizeof(r));
	r.route = -1;
	r.routeState = -1;
	r.improvement = -1;
	r.resource = -1;
	r.feature = -1;
	r.owner = -1;
	r.improvementState = -1;
	r.fow = 3;   // revealed and visible
	return r;
}

// a table with a road along row 4 from x = 2 to x = 8, and a railroad down from (5,4) to (5,7)
std::vector<SrvFeed::PlotRecord> Table()
{
	std::vector<SrvFeed::PlotRecord> t(W * H, Blank());
	for(uint32_t x = 2; x <= 8; ++x)
	{
		t[4 * W + x].route = 0;
		t[4 * W + x].routeState = 0;
	}
	for(uint32_t y = 5; y <= 7; ++y)
	{
		t[y * W + 5].route = 1;
		t[y * W + 5].routeState = 1;
	}
	return t;
}

struct TableSrc
{
	std::vector<SrvFeed::PlotRecord> rec;
	uint32_t gen = 1, w = W, h = H;
	int reads = 0;
	RoadsTableFn fn()
	{
		return [this](std::vector<SrvFeed::PlotRecord>& out, uint32_t& g, uint32_t& ww, uint32_t& hh) {
			reads++;
			out = rec;
			g = gen;
			ww = w;
			hh = h;
		};
	}
};

const float kWorld[4] = {0.0f, 0.0f, 64.0f * W + 64.0f, 64.0f * H + 256.0f};

// a tap set: one ordinary decal and one route curve (priority 0.7, RoadsandRails texture) across plots (3,2) and (4,2)
std::shared_ptr<c5r::decal::DecalSet> Tap(uint32_t gen, bool withRoute = true)
{
	std::shared_ptr<c5r::decal::DecalSet> s(new c5r::decal::DecalSet());
	s->generation = gen;
	s->loadSeq = 1;
	memcpy(s->worldRect, kWorld, sizeof(kWorld));
	s->textures = {"some_decal.dds", "RoadsandRails_new_D.dds", "RoadsandRails_new_H.dds", "RoadsandRails_new_SREF.dds"};
	auto add = [&](float x0, float y0, float x1, float y1, float prio, uint32_t tex) {
		c5r::decal::Decal d;
		d.rect[0] = x0;
		d.rect[1] = y0;
		d.rect[2] = x1;
		d.rect[3] = y1;
		d.priority = prio;
		d.texture[0] = tex;
		d.firstVertex = (uint32_t)s->vertices.size();
		d.vertexCount = 6;
		d.listOrder = (uint32_t)s->decals.size();
		// a horizontal strip through the rectangle's middle row, half height 2.5 samples: two triangles
		const float mid = 0.5f, hh = 2.5f / (y1 - y0);
		const float pts[6][2] = {{0, mid - hh}, {1, mid - hh}, {1, mid + hh}, {0, mid - hh}, {1, mid + hh}, {0, mid + hh}};
		for(int k = 0; k < 6; ++k)
			s->vertices.push_back(c5r::decal::DecalVertex{c5r::bake::floatToHalf(pts[k][0]), c5r::bake::floatToHalf(pts[k][1]),
			                                              c5r::bake::floatToHalf(pts[k][0]), c5r::bake::floatToHalf(0.35f)});
		s->decals.push_back(d);
	};
	add(100.0f, 300.0f, 140.0f, 340.0f, 0.5f, 0);
	if(withRoute)   // plot (3,2) centre (224, 256), plot (4,2) centre (288, 256): a curve from 224 to 288 along y = 256
		add(224.0f, 236.0f, 288.0f, 276.0f, 0.7f, 1);
	return s;
}

uint32_t RouteCurves(const c5r::decal::DecalSet& s)
{
	uint32_t n = 0;
	for(const c5r::decal::Decal& d : s.decals)
		n += rx::isTapRouteCurve(s, d) ? 1 : 0;
	return n;
}

void TestStyleAndTable()
{
	const rx::LayoutOptions o = ProductRoadStyle();
	CHECK(o.style == rx::Style::Line && o.filletRadius == 22.0f && o.mergeLength == 22.0f);
	CHECK(!(o.style == rx::Style::Spline));   // negative: not the generator's default style
	rx::MapIn m;
	std::vector<SrvFeed::PlotRecord> t = Table();
	CHECK(RoadMapFromTable(t, W, H, false, m) && m.width == W && m.height == H && m.plots.size() == W * H);
	CHECK(m.plots[4 * W + 2].band == rx::kBandRoad && m.plots[6 * W + 5].band == rx::kBandRail && m.plots[0].band == rx::kBandNone);
	// unrevealed plots carry no route (negative: the same plot revealed does)
	t[4 * W + 3].fow = 0;
	CHECK(RoadMapFromTable(t, W, H, false, m) && m.plots[4 * W + 3].band == rx::kBandNone && m.plots[4 * W + 4].band == rx::kBandRoad);
	// a table without any revealed plot (headless): every plot counts
	for(SrvFeed::PlotRecord& r : t)
		r.fow = 0;
	CHECK(RoadMapFromTable(t, W, H, false, m) && m.plots[4 * W + 3].band == rx::kBandRoad);
	// size mismatches refused
	CHECK(!RoadMapFromTable(t, W + 1, H, false, m) && m.plots.empty());
	CHECK(!RoadMapFromTable(t, 0, 0, false, m));
	// pillaged, trade route, under construction
	std::vector<SrvFeed::PlotRecord> u = Table();
	u[4 * W + 2].flags = 4;
	u[4 * W + 3].routeState = 6;
	u[4 * W + 4].routeState = 4;
	CHECK(RoadMapFromTable(u, W, H, true, m) && m.wrapX && m.plots[4 * W + 2].band == rx::kBandRoadPillaged &&
	      m.plots[4 * W + 3].band == rx::kBandRoadTrade && m.plots[4 * W + 4].band == rx::kBandNone);
}

void TestSpanText()
{
	rx::MapIn m;
	CHECK(RoadMapFromTable(Table(), W, H, true, m));
	m.plots[4 * W + 8].city = 1;
	const std::string s = RoadSpanText(m);
	CHECK(s.compare(0, 4, "rl1 ") == 0 && s.find(" 56:1:1") != std::string::npos && s.find(" 56:1:0") == std::string::npos &&
	      s.find(" 50:1:0") != std::string::npos);
	rx::MapIn back;
	std::string why;
	CHECK(ParseRoadSpanText(s, back, why) && back.width == W && back.height == H && back.wrapX);
	bool same = back.plots.size() == m.plots.size();
	for(size_t i = 0; same && i < m.plots.size(); ++i)
		same = back.plots[i].band == m.plots[i].band && back.plots[i].city == m.plots[i].city;
	CHECK(same && back.plots[4 * W + 8].city == 1);
	CHECK(RoadSpanText(back) == s);
	// refusals
	CHECK(!ParseRoadSpanText("fv1 12 10 0 0", back, why) && back.plots.empty());
	CHECK(!ParseRoadSpanText("rl1 12 10 0 1 5:7:0", back, why));     // band 7
	CHECK(!ParseRoadSpanText("rl1 12 10 0 2 6:1:0 5:1:0", back, why)); // out of order
	CHECK(!ParseRoadSpanText("rl1 12 10 0 1 120:1:0", back, why));   // outside the map
	CHECK(!ParseRoadSpanText("rl1 12 10 0 2 5:1:0", back, why));     // fewer than N
	CHECK(!ParseRoadSpanText("rl1 12 10 0 1 5:1:0 6:1:0", back, why)); // more than N
	CHECK(!ParseRoadSpanText("rl1 12 10 2 0", back, why));           // wrap 2
	CHECK(ParseRoadSpanText("rl1 12 10 0 0", back, why) && back.plots.size() == W * H);   // control: an empty layout
}

void TestPlotAt()
{
	float cx, cy;
	rx::plotCentre(5, 3, cx, cy);
	CHECK(PlotAtSample(cx, cy, W, H) == (int32_t)(3 * W + 5));
	CHECK(PlotAtSample(cx + 20.0f, cy, W, H) == (int32_t)(3 * W + 5));
	CHECK(PlotAtSample(cx + 40.0f, cy, W, H) == (int32_t)(3 * W + 6));   // negative: past the edge, the neighbour
	CHECK(PlotAtSample(-500.0f, cy, W, H) == -1);
}

void TestProducer(bool threaded)
{
	Log log;
	TableSrc src;
	src.rec = Table();
	RoadLayoutProducer p;
	p.setLog(&Log::Fn, &log);
	SceneRoadsOptions o;
	o.threaded = threaded;
	p.setOptions(o);
	p.setTable(src.fn());
	std::shared_ptr<c5r::decal::DecalSet> tap = Tap(5);
	// before any world: nothing
	CHECK(!p.refresh() && !p.haveDecals());
	CHECK(p.composeDecals(tap) == tap);   // the tap's set as it is before the first production
	p.setWorld(kWorld, 1, false, 0.0f, 0.0f);
	CHECK(src.reads == 1 && p.loads == 1);
	CHECK(p.settle(10000));
	CHECK(p.haveDecals() && p.productions == 1 && p.lastRoutePlots == 10);
	CHECK(log.count("roadlayout: 10 route plots") == 1);
	// compose: the tap's route curve dropped, ours added, the other decal kept
	std::shared_ptr<const c5r::decal::DecalSet> out = p.composeDecals(tap);
	CHECK(out && out != tap && out->decals.size() == 1 + p.decals().decals.size() && RouteCurves(*out) == p.decals().decals.size());
	CHECK(out->decals[0].priority == 0.5f && RouteCurves(*tap) == 1);
	// cached: the same set again gives the same pointer (negative: a new tap set gives another)
	CHECK(p.composeDecals(tap) == out && p.composeCached == 1);
	std::shared_ptr<c5r::decal::DecalSet> tap2 = Tap(6);
	CHECK(p.composeDecals(tap2) != out);
	// the first production is a change: our decals and the tap's route curves they replace (the newest tap set's)
	std::vector<RoadsChange> ch;
	CHECK(p.takeDecalChanges(ch) && ch.size() == 1 && ch[0].generation == kRoadDecalGenBase + 1 &&
	      ch[0].rects.size() == p.decals().decals.size() + 1);
	CHECK(!p.takeDecalChanges(ch) && ch.size() == 1);   // taken once
	ch.clear();
	// a route added: the batch re-reads, the production changes some decals, the change lies in the roads' band
	src.rec[4 * W + 9].route = 0;
	src.rec[4 * W + 9].routeState = 0;
	SrvFeed::Batch b;
	b.snapshotGen = src.gen;
	b.width = W;
	b.height = H;
	b.changedPlots.push_back(4 * W + 9);
	CHECK(p.onBatch(b));
	CHECK(p.settle(10000) && p.productions == 2);
	CHECK(p.takeDecalChanges(ch) && ch.size() == 1 && ch[0].generation == kRoadDecalGenBase + 2 && !ch[0].rects.empty() &&
	      !ch[0].areaTiles.empty() && ch[0].rects.size() < p.decals().decals.size());
	CHECK(!p.takeDecalChanges(ch) && ch.size() == 1);   // taken once
	// a batch that changes no band: no production (negative of the above)
	src.rec[0].feature = 5;
	CHECK(!p.onBatch(b));
	CHECK(p.settle(10000) && p.productions == 2 && !p.takeDecalChanges(ch));
	// railroad to road on one plot: a change
	src.rec[6 * W + 5].route = 0;
	src.rec[6 * W + 5].routeState = 0;
	CHECK(p.onBatch(b) && p.settle(10000) && p.productions == 3 && p.takeDecalChanges(ch) && ch.back().generation == kRoadDecalGenBase + 3);
	// E6: the table taken away in the middle: the roads in force stay, nothing produced
	const size_t nDecals = p.decals().decals.size();
	src.rec.clear();
	src.w = src.h = 0;
	CHECK(!p.onBatch(b));
	CHECK(p.settle(10000) && p.haveDecals() && p.decals().decals.size() == nDecals && p.productions == 3);
	// a table of the wrong size: LOUD, counted, the roads in force stay
	src.rec = Table();
	src.rec.pop_back();
	src.w = W;
	src.h = H;
	CHECK(!p.onBatch(b) && p.tableErrors == 1 && log.count("LOUD: roadlayout: the plot table holds") == 1 && p.haveDecals());
	// the footprint: our road's cells carry bit 0x02, a far plot none
	std::shared_ptr<const c5r::forest::CutSource> fp = p.footprint();
	CHECK(fp != nullptr && p.cutVersion() > 0);
	if(fp)
	{
		float cx, cy;
		rx::plotCentre(4, 4, cx, cy);
		CHECK(fp->bitsAt(cx, cy * rx::kIsoY) == rx::kRoadFootprintBit);
		rx::plotCentre(9, 8, cx, cy);
		CHECK(fp->bitsAt(cx, cy * rx::kIsoY) == 0);
	}
	// the span state, and a new load drops everything
	std::string st, why;
	CHECK(p.spanState(st, why) && st.compare(0, 4, "rl1 ") == 0);
	p.setWorld(kWorld, 2, false, 0.0f, 0.0f);
	CHECK(!p.haveDecals() && p.loads == 2 && p.footprint() == nullptr && p.stockClear() == nullptr);
	CHECK(!p.spanState(st, why) && st.empty());
	char line[1024];
	p.totalsLine(line, sizeof(line));
	CHECK(strstr(line, "roadlayout totals:") == line && strstr(line, "productions=3") != nullptr);
}

void TestDeterminism()
{
	TableSrc a, b;
	a.rec = Table();
	b.rec = Table();
	RoadLayoutProducer pa, pb;
	SceneRoadsOptions o;
	o.threaded = false;
	pa.setOptions(o);
	pb.setOptions(o);
	pa.setTable(a.fn());
	pb.setTable(b.fn());
	pa.setWorld(kWorld, 1, false, 0, 0);
	pb.setWorld(kWorld, 7, false, 0, 0);
	CHECK(pa.settle(5000) && pb.settle(5000));
	bool same = pa.decals().decals.size() == pb.decals().decals.size() && pa.decals().vertices.size() == pb.decals().vertices.size();
	for(size_t i = 0; same && i < pa.decals().decals.size(); ++i)
		same = rx::contentHash(pa.decals().decals[i], pa.decals().textures, pa.decals().vertices) ==
		       rx::contentHash(pb.decals().decals[i], pb.decals().textures, pb.decals().vertices);
	CHECK(same && !pa.decals().decals.empty());
	// another layout: another result (negative)
	b.rec[4 * W + 9].route = 0;
	b.rec[4 * W + 9].routeState = 0;
	pb.setWorld(kWorld, 8, false, 0, 0);
	CHECK(pb.settle(5000) && pb.decals().decals.size() != pa.decals().decals.size());
}

// the restored span input (the offline player): no table read, the same decals as the live production
void TestRestore()
{
	TableSrc a;
	a.rec = Table();
	RoadLayoutProducer live, played;
	SceneRoadsOptions o;
	o.threaded = false;
	live.setOptions(o);
	played.setOptions(o);
	live.setTable(a.fn());
	live.setWorld(kWorld, 1, false, 0, 0);
	CHECK(live.settle(5000));
	std::string st, why;
	CHECK(live.spanState(st, why));
	TableSrc other;   // the player's table would differ; a restored input never reads it
	other.rec = std::vector<SrvFeed::PlotRecord>(W * H, Blank());
	played.setTable(other.fn());
	CHECK(played.restoreSpanInput(st, why));
	played.setWorld(kWorld, 1, false, 0, 0);
	CHECK(other.reads == 0 && played.settle(5000) && played.decals().decals.size() == live.decals().decals.size());
	RoadLayoutProducer bad;
	CHECK(!bad.restoreSpanInput("rl1 nonsense", why) && !why.empty());
}

// MC #7664 option (A): the EXE's route cells cleared only on bare plots; the named risk (a model's tree-cut surface on a
// route plot with an improvement or a city) is kept
void TestStockClear()
{
	std::vector<SrvFeed::PlotRecord> t(W * H, Blank());
	std::shared_ptr<c5r::decal::DecalSet> tap = Tap(3);
	StockRouteClear c;
	CHECK(c.build(*tap, t, W, H, 3.75f).ok());
	CHECK(c.routeCurves == 1 && c.cellsCleared > 0 && c.cellsKept == 0 && c.plotsCleared == 2);
	float cx, cy;
	rx::plotCentre(3, 2, cx, cy);   // (224, 256)
	const float px = cx + 16.0f, py = cy * rx::kIsoY;
	CHECK(c.bitsAt(px, py) == rx::kRoadFootprintBit);
	CHECK(c.bitsAt(px, (cy + 30.0f) * rx::kIsoY) == 0);   // off the band: nothing
	// the band is about the EXE's: cells up to ~3.75 iso samples past the strip's 2.5-sample half height, not 10 past it
	CHECK(c.bitsAt(px, (cy + 5.0f) * rx::kIsoY) == rx::kRoadFootprintBit && c.bitsAt(px, (cy + 12.0f) * rx::kIsoY) == 0);
	const uint64_t allBare = c.cellsCleared;
	// the risk test: plot (3,2) carries an improvement (its model's tree-cut surface stays cut), plot (4,2) a city
	t[2 * W + 3].improvement = 7;
	t[2 * W + 4].flags = 16;
	StockRouteClear k;
	CHECK(k.build(*tap, t, W, H, 3.75f).ok());
	CHECK(k.cellsCleared == 0 && k.cellsKept == allBare && k.plotsKept == 2 && k.bitsAt(px, py) == 0);
	// only the improvement: half of the band stays
	t[2 * W + 4].flags = 0;
	StockRouteClear h;
	CHECK(h.build(*tap, t, W, H, 3.75f).ok() && h.cellsCleared > 0 && h.cellsKept > 0 && h.cellsCleared + h.cellsKept == allBare);
	rx::plotCentre(4, 2, cx, cy);
	CHECK(h.bitsAt(cx - 16.0f, cy * rx::kIsoY) == rx::kRoadFootprintBit && h.bitsAt(px, py) == 0);
	// no route curve in the set: nothing to clear (negative)
	StockRouteClear n;
	CHECK(n.build(*Tap(4, false), t, W, H, 3.75f).ok() && n.routeCurves == 0 && n.cellsCleared == 0);
	// refusals
	StockRouteClear r;
	CHECK(!r.build(*tap, t, W + 1, H, 3.75f).ok() && !r.build(*tap, t, W, H, 40.0f).ok());
}

// the producer publishes the clear mask of each new tap set once it has roads in force
void TestProducerClear()
{
	TableSrc a;
	a.rec = Table();
	Log log;
	RoadLayoutProducer p;
	p.setLog(&Log::Fn, &log);
	SceneRoadsOptions o;
	o.threaded = false;
	p.setOptions(o);
	p.setTable(a.fn());
	p.setWorld(kWorld, 1, false, 0, 0);
	std::shared_ptr<c5r::decal::DecalSet> tap = Tap(9);
	p.composeDecals(tap);
	CHECK(p.stockClear() == nullptr);   // before the first production: stock's lanes stay
	CHECK(p.settle(5000));
	const uint64_t v0 = p.cutVersion();
	p.composeDecals(tap);
	CHECK(p.stockClear() != nullptr && p.clearBuilds == 1 && p.cutVersion() > v0);
	CHECK(log.count("the EXE's route cells to clear") == 1);
	p.composeDecals(tap);
	CHECK(p.clearBuilds == 1);   // the same set: not built again
	p.composeDecals(Tap(10));
	CHECK(p.clearBuilds == 2);
	// threaded: the mask is built on the worker and put in force by refresh() (never inside composeDecals)
	TableSrc b;
	b.rec = Table();
	RoadLayoutProducer q;
	SceneRoadsOptions qo;
	qo.threaded = true;
	q.setOptions(qo);
	q.setTable(b.fn());
	q.setWorld(kWorld, 1, false, 0, 0);
	CHECK(q.settle(5000));
	q.composeDecals(Tap(12));
	CHECK(q.stockClear() == nullptr && q.clearBuilds == 0);   // not yet: queued on the worker
	CHECK(q.settle(5000) && q.stockClear() != nullptr && q.clearBuilds == 1);
}

// MC #7664 (A) in the forest's cut source: a CutTapReader without a control block (its own grid empty) shows the overlay's
// bits; the clear source takes bits out of the EXE's grid only (never out of the overlays)
void TestCutOverlay()
{
	using srvrender::CutOverlaySlot;
	TableSrc a;
	a.rec = Table();
	RoadLayoutProducer p;
	SceneRoadsOptions o;
	o.threaded = false;
	p.setOptions(o);
	p.setTable(a.fn());
	p.setWorld(kWorld, 1, false, 0, 0);
	CHECK(p.settle(5000));
	std::shared_ptr<c5r::decal::DecalSet> tap = Tap(11);
	p.composeDecals(tap);
	float cx, cy;
	rx::plotCentre(4, 4, cx, cy);
	const float rx_ = cx, ry = cy * rx::kIsoY;   // on our road
	rx::plotCentre(3, 2, cx, cy);
	const float sx = cx + 16.0f, sy = cy * rx::kIsoY;   // on the tap's route curve (bare plot)
	srvrender::CutTapReader reader(0xFFFFFFF0u, L"Local\\VPShellCutTap.test4934.none");
	CHECK(reader.bitsAt(rx_, ry) == 0 && !CutOverlaySlot().roadsActive());
	const uint64_t g0 = reader.generation();
	CutOverlaySlot().setRoads(p.footprint(), p.stockClear());
	CHECK(CutOverlaySlot().roadsActive() && reader.generation() > g0);
	CHECK(reader.bitsAt(rx_, ry) == rx::kRoadFootprintBit);   // our road cuts
	CHECK(CutOverlaySlot().clearAt(sx, sy) == rx::kRoadFootprintBit && reader.bitsAt(sx, sy) == 0);   // nothing to clear in an empty grid
	CHECK(CutOverlaySlot().clearAt(rx_, ry) == 0);   // negative: no tap curve under our road here
	CutOverlaySlot().setRoads(nullptr, nullptr);
	CHECK(reader.bitsAt(rx_, ry) == 0 && !CutOverlaySlot().roadsActive());
}

// ticket 0808: the bridge placer. A bridge-like mesh (11.6 x 3.7, half floats, stride 20) turned `deg` about z.
std::vector<uint8_t> BridgeVb(float deg)
{
	const float a = deg * 3.14159265f / 180.0f, c = cosf(a), s = sinf(a);
	std::vector<uint8_t> b;
	for(int i = 0; i <= 10; ++i)
		for(int j = 0; j <= 2; ++j)
		{
			const float x = -5.8f + 1.16f * (float)i, y = -1.85f + 1.85f * (float)j;
			const float p[3] = {x * c - y * s, x * s + y * c, 12.5f};
			uint8_t v[20] = {};
			for(int q = 0; q < 3; ++q)
			{
				const uint16_t h = c5r::bake::floatToHalf(p[q]);
				memcpy(v + 2 * q, &h, 2);
			}
			b.insert(b.end(), v, v + 20);
		}
	return b;
}

void TestBridgePlacer()
{
	CHECK(GetBridgePlacer() == nullptr);
	const float ox = 10.0f, oy = 40.0f;
	const std::vector<uint8_t> vb = BridgeVb(40.0f);
	float cx, cy;
	rx::plotCentre(3, 4, cx, cy);   // our road runs along row 4: the edge (3,4)-(4,4), its middle at (cx + 32, cy * kIsoY) iso
	float rows[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, cx + 32.0f - ox + 1.5f, cy * rx::kIsoY - oy + 1.0f, 0, 1};
	{
		Log log;
		TableSrc src;
		src.rec = Table();
		RoadLayoutProducer p;
		p.setLog(&Log::Fn, &log);
		SceneRoadsOptions o;
		o.threaded = false;
		p.setOptions(o);
		p.setTable(src.fn());
		p.setWorld(kWorld, 1, false, ox, oy);
		CHECK(GetBridgePlacer() == nullptr);   // none before the load's first production
		CHECK(p.settle(10000) && p.productions == 1);
		std::shared_ptr<const BridgePlacer> pl = GetBridgePlacer();
		CHECK(pl != nullptr && pl == p.bridgePlacer() && pl->generation() == 1 && p.bridgePlacers == 1);
		CHECK(log.count("roadlayout: bridges: placer 1 in force") == 1);
		if(pl)
		{
			const BridgePlaceResult r = pl->place(rows, vb.data(), vb.size(), 20);
			CHECK(r.move == BridgeMove::Move && !r.fitFailed && fabsf(r.turnDeg + 40.0f) < 0.5f && r.edgeKey == (4u * W + 3) * 6 + 1);
			// the mesh's centre lands on the edge's middle in the world (iso - (ox, oy))
			CHECK(fabsf(r.rows[12] - (cx + 32.0f - ox)) < 0.05f && fabsf(r.rows[13] - (cy * rx::kIsoY - oy)) < 0.05f && r.rows[14] == 0);
			// negative: the edge (3,4)-(3,5) has no road of ours: dropped; a mesh with no long axis: kept, fit failed
			float up[16];
			memcpy(up, rows, sizeof(up));
			up[12] = cx + 16.0f - ox;
			up[13] = cy * rx::kIsoY + 27.7f - oy;
			CHECK(pl->place(up, vb.data(), vb.size(), 20).move == BridgeMove::Drop);
			const std::vector<uint8_t> dot(20 * 3, 0);
			const BridgePlaceResult f = pl->place(rows, dot.data(), dot.size(), 20);
			CHECK(f.fitFailed && f.move == BridgeMove::Keep && memcmp(f.rows, rows, sizeof(rows)) == 0);
		}
		// a batch that changes the bands: a new placer (generation 2)
		src.rec[4 * W + 9].route = 0;
		src.rec[4 * W + 9].routeState = 0;
		SrvFeed::Batch b;
		b.snapshotGen = src.gen;
		b.width = W;
		b.height = H;
		b.changedPlots.push_back(4 * W + 9);
		CHECK(p.onBatch(b) && p.settle(10000) && p.productions == 2);
		CHECK(GetBridgePlacer() != nullptr && GetBridgePlacer()->generation() == 2);
		// the field's frame moves within the load: republished in the new frame
		p.setWorld(kWorld, 1, false, ox + 64.0f, oy);
		CHECK(GetBridgePlacer() != nullptr && GetBridgePlacer()->generation() == 3 && p.bridgePlacer()->ox() == ox + 64.0f);
		// a new load: withdrawn until its first production
		p.setWorld(kWorld, 2, false, ox, oy);
		CHECK(GetBridgePlacer() == nullptr && p.bridgePlacer() == nullptr);
		CHECK(p.settle(10000) && GetBridgePlacer() != nullptr);
	}
	CHECK(GetBridgePlacer() == nullptr);   // the producer's destructor withdrew its placer
	// RoadLayout 0: no producer, no placer (the model layer draws the bridges as tapped)
	{
		RoadLayoutProducer q;
		CHECK(GetBridgePlacer() == nullptr && q.bridgePlacer() == nullptr);
	}
	CHECK(IsBridgeTexture("bridges_diff.dds") && IsBridgeTexture("Bridges_Diff.DDS") && !IsBridgeTexture("bridges_sref.dds") &&
	      !IsBridgeTexture("great_wall_diff.dds") && !IsBridgeTexture(""));
}

void TestFactory()
{
	std::unique_ptr<SceneRoads> r = MakeSceneRoadsImpl();
	CHECK(r != nullptr);
	CHECK(g_rf != nullptr && MakeSceneRoads() != nullptr);   // srv_extras.cpp's registrar ran at start-up
}

int main()
{
	TestStyleAndTable();
	TestSpanText();
	TestPlotAt();
	TestProducer(false);
	TestProducer(true);
	TestDeterminism();
	TestRestore();
	TestStockClear();
	TestProducerClear();
	TestCutOverlay();
	TestBridgePlacer();
	TestFactory();
#ifdef EXTRAS_TEST_INJECT_FAIL
	CHECK(false);
#endif
	printf("test_roads_producer: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
