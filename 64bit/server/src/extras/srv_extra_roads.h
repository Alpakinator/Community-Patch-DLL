// srv_extra_roads.h - RoadLayout's producer in server.exe (epic 0454, package X3, ticket 4934 as FixRoadLayout; ticket 4940:
// the all-connections layout, RoadStyleFor; docs/extras/roads.md 4b, 4g).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The farm producer's pattern (server/src/render/srv_farmfields.h): from the feed's plot table (the active team's revealed
// route, its route state and the pillaged bit) it keeps the load's route bands (c5r::extras::roads::MapIn), lays out the
// roads and railroads with the product style (variant (e) of round 2, D212: ProductRoadStyle) on a worker thread whenever a
// band changes, and joins them to the decal tap's set (composeRoadSet: the tap's route curves dropped, ours added).
// takeDecalChanges() hands the decals that changed (roadChange) as one RoadsChange in its own generation band.
// The trees' cut (MC #7664): our pieces' footprint (RoadFootprint, bit 0x02, the EXE's route bit, trees.md 14.1) and a mask
// of the EXE's own route cells to clear (StockRouteClear: the tap's route-curve triangles widened to the EXE's band, on
// plots with no improvement and no city), which the scene publishes through CutOverlaySlot (srv_cut_tap.h).
// Every call on the render thread but the worker's production. Failures in play are LOUD (the first 20, then every 100th),
// counted, never fatal: a refused production keeps the roads in force; a refused compose hands on the tap's set.
// Display only: it reads the feed's plot table and the tap's set, never the DLL.
#pragma once
#include "srv_extras.h"
#include "feed/srv_feed.h"
#include "../../../render/src/extras/c5r/extras/roads.h"   // c5r::extras::roads (render/src/extras: no include path)
#include <condition_variable>
#include <mutex>
#include <thread>

namespace srvextras {

namespace roadsx = c5r::extras::roads;

// The product style (D212, MC #7621, 2026-11-20: the maintainer picked round 2's variant (e)): straight from plot centre
// to plot centre, turns as short fillets of radius 22, branches merging into the through-line with curves of legs 22.
roadsx::LayoutOptions ProductRoadStyle();
// Ticket 4940: the style of a layout choice (RoadLayoutChoice, srv_extras.h): layout 1 = ProductRoadStyle() (junction and
// jitter not used); layout 2 = the same with every neighbouring pair joined (Connect::All), the centre treatment
// `junction` (roads::Junction: 0 through, 1 cross, 2 ring, 3 rosette; from 4 links, cities stay hubs) and the pass points
// moved by `jitter` (LayoutOptions::lineJitter, 0..7). A value out of range is clamped (the parsers refuse it first).
roadsx::LayoutOptions RoadStyleFor(const RoadLayoutChoice& c);
// "layout 2 (all connections), junction cross, jitter 3" / "layout 1 (tidy, variant (e))": the producer's start line.
std::string RoadLayoutChoiceText(const RoadLayoutChoice& c);

// The route bands of a plot table (width x height records, index y * width + x): bandFromFeed(route, routeState, flags) on
// the plots the active team has revealed (fow bit 0), every other plot none; the city mark from flags bit 4 (never set by
// the feed today, srv_feed.h). A table in which no plot has fow bit 0 (no active team: headless) takes every plot. False:
// a table whose size is not width x height, or a size above kMaxPlotsPerAxis.
bool RoadMapFromTable(const std::vector<SrvFeed::PlotRecord>& rec, uint32_t width, uint32_t height, bool wrapX,
                      roadsx::MapIn& out);

// render_roadlayout_state: "rl1 W H WRAP N i:band:city ..." (the N route plots, ascending index). Parse refuses another
// form, a size out of range, an index outside the map or out of order, a band outside 0 .. 6.
std::string RoadSpanText(const roadsx::MapIn& m);
bool ParseRoadSpanText(const std::string& text, roadsx::MapIn& out, std::string& why);
const char* const kRoadStateForm = "rl1 ";

// The EXE's route cells to clear (MC #7664, option (A)): every cut cell whose sample point (2 col, 2 row) lies within `band`
// iso samples of a triangle of the tap's route-curve records (isTapRouteCurve), on a plot with no improvement and no city.
// bitsAt gives kRoadFootprintBit there, 0 elsewhere (the reader clears those bits of the EXE's grid).
class StockRouteClear : public c5r::forest::CutSource
{
public:
	// plots: the table the band rule uses (improvement < 0 and no city bit = bare); the world rectangle of the tap's set.
	c5r::Status build(const c5r::decal::DecalSet& tap, const std::vector<SrvFeed::PlotRecord>& plots, uint32_t mapW, uint32_t mapH,
	                  float band);
	void setFrame(float ox, float oy) { m_ox = ox; m_oy = oy; }
	uint8_t bitsAt(float worldX, float worldY) const override;
	bool cell(int32_t col, int32_t row) const;
	uint64_t generation() const override { return m_generation; }
	void setGeneration(uint64_t g) { m_generation = g; }
	uint32_t width() const { return m_w; }
	uint32_t height() const { return m_h; }
	uint64_t cellsCleared = 0;      // cells marked to clear
	uint64_t cellsKept = 0;         // cells under a route curve left as they are: an improvement or a city on the plot
	uint32_t routeCurves = 0;       // the tap's route-curve records rasterised
	uint32_t plotsCleared = 0, plotsKept = 0;   // distinct plots with cleared / kept cells

private:
	uint32_t m_w = 0, m_h = 0, m_wpr = 0;
	float m_ox = 0, m_oy = 0;
	std::vector<uint64_t> m_bits;
	uint64_t m_generation = 0;
};

// The plot whose hexagon holds a sample point (the nearest plot centre in the iso frame), -1 off the map. Exposed for tests.
int32_t PlotAtSample(float sx, float sy, uint32_t mapW, uint32_t mapH);

// Ticket 0808: the bridge placer of one production (BridgePlacer, srv_extras.h): roadsx::fitBridgeMesh of the draw's vertex
// buffer, then roadsx::placeBridge on the production's map, network and style in the field's frame (ox, oy).
class RoadBridgePlacer : public BridgePlacer
{
public:
	RoadBridgePlacer(const roadsx::MapIn& m, const roadsx::Network& n, const roadsx::LayoutOptions& o, float ox, float oy, uint64_t gen)
	    : m_in(m), m_net(n), m_style(o), m_ox(ox), m_oy(oy), m_gen(gen)
	{
	}
	BridgePlaceResult place(const float rowsIn[16], const uint8_t* vb, size_t bytes, uint32_t stride) const override;
	uint64_t generation() const override { return m_gen; }
	const roadsx::MapIn& input() const { return m_in; }
	const roadsx::Network& network() const { return m_net; }
	float ox() const { return m_ox; }
	float oy() const { return m_oy; }

private:
	roadsx::MapIn m_in;
	roadsx::Network m_net;
	roadsx::LayoutOptions m_style;
	float m_ox, m_oy;
	uint64_t m_gen;
};

class RoadLayoutProducer : public SceneRoads
{
public:
	RoadLayoutProducer();
	~RoadLayoutProducer() override;   // stops and joins the worker
	RoadLayoutProducer(const RoadLayoutProducer&) = delete;
	RoadLayoutProducer& operator=(const RoadLayoutProducer&) = delete;

	void setLog(ExtrasLogFn log, void* ctx) override
	{
		m_log = log;
		m_logCtx = ctx;
	}
	void setOptions(const SceneRoadsOptions& o) override { m_opt = o; }
	// tests (the product: RoadStyleFor(the choice in RoadLayoutChoiceSlot() when the producer was made), ticket 4940)
	void setStyle(const roadsx::LayoutOptions& s) { m_style = s; m_dirty = true; }
	const roadsx::LayoutOptions& style() const { return m_style; }
	const RoadLayoutChoice& choice() const { return m_choice; }
	void setTable(RoadsTableFn fn) override { m_table = fn; }
	void setWorld(const float worldRect[4], uint32_t loadSeq, bool wrapX, float cutOx, float cutOy) override;
	bool onBatch(const SrvFeed::Batch& b) override;
	bool refresh() override;
	bool settle(double ms) override;
	bool busy() const;
	std::shared_ptr<const c5r::decal::DecalSet> composeDecals(const std::shared_ptr<const c5r::decal::DecalSet>& tap) override;
	bool takeDecalChanges(std::vector<RoadsChange>& out) override;
	std::shared_ptr<const c5r::forest::CutSource> footprint() const override { return m_footprint; }
	std::shared_ptr<const c5r::forest::CutSource> stockClear() const override { return m_clear; }
	uint64_t cutVersion() const override { return m_cutVersion; }
	uint64_t version() const override { return m_decalsVersion; }
	bool spanState(std::string& text, std::string& why) const override;
	bool restoreSpanInput(const std::string& text, std::string& why) override;
	void totalsLine(char* out, size_t n) const override;

	const roadsx::MapIn& input() const { return m_in; }
	const roadsx::RoadDecals& decals() const { return m_decals; }
	bool haveDecals() const { return m_haveDecals; }
	const StockRouteClear* clearMask() const { return m_clear.get(); }
	// ticket 0808: the bridge placer this producer published (null: none in the slot from it)
	std::shared_ptr<const RoadBridgePlacer> bridgePlacer() const { return m_bridges; }
	int64_t bridgePlacers = 0;

	// totals
	int64_t loads = 0, rereads = 0, plotChanges = 0, overflows = 0, tableErrors = 0, productions = 0, productionErrors = 0;
	int64_t composes = 0, composeCached = 0, composesBeforeFirst = 0, composeErrors = 0, decalChangesMade = 0, staleResults = 0, clearBuilds = 0;
	int64_t clearErrors = 0, footprints = 0, footprintErrors = 0;
	double lastProduceMs = 0, lastComposeMs = 0, lastClearMs = 0, lastFootprintMs = 0;
	uint32_t lastRoutePlots = 0, lastRejected = 0;

private:
	void say(const char* fmt, ...) const;
	bool loudDue(int64_t n) const { return n <= 20 || n % 100 == 0; }
	bool readTable(const char* why);   // true when the bands changed
	void startWorker();
	void workerLoop();
	bool takeResult();
	void publishBridges(const roadsx::MapIn& m, const roadsx::Network& n);   // ticket 0808
	void withdrawBridges();

	struct Job
	{
		roadsx::MapIn in;
		roadsx::LayoutOptions style;
		uint32_t loadSeq = 0;
		float worldRect[4] = {0, 0, 0, 0};
		float ox = 0, oy = 0, margin = 0;
	};
	struct Result
	{
		roadsx::MapIn in;
		uint32_t loadSeq = 0;
		c5r::Status st;
		roadsx::Network net;
		roadsx::RoadDecals decals;
		std::shared_ptr<roadsx::RoadFootprint> footprint;
		c5r::Status footprintSt;
		double ms = 0, footprintMs = 0;
	};
	static void Produce(const Job& j, Result& r);
	// the EXE's route cells to clear (StockRouteClear) of a tap set, on the worker when threaded
	struct ClearJob
	{
		std::shared_ptr<const c5r::decal::DecalSet> tap;
		std::vector<SrvFeed::PlotRecord> plots;
		uint32_t w = 0, h = 0, loadSeq = 0;
		float band = 0, ox = 0, oy = 0;
	};
	struct ClearResult
	{
		std::shared_ptr<StockRouteClear> mask;
		c5r::Status st;
		double ms = 0;
		uint32_t tapGen = 0, loadSeq = 0;
	};
	static void BuildClear(const ClearJob& j, ClearResult& r);
	void takeClear(std::unique_ptr<ClearResult> r);

	ExtrasLogFn m_log = nullptr;
	void* m_logCtx = nullptr;
	SceneRoadsOptions m_opt;
	RoadLayoutChoice m_choice;   // ticket 4940: RoadLayoutChoiceSlot() when made
	roadsx::LayoutOptions m_style;
	RoadsTableFn m_table;
	std::vector<SrvFeed::PlotRecord> m_rec;
	uint32_t m_w = 0, m_h = 0, m_tableGen = 0;
	bool m_haveTable = false, m_haveWorld = false, m_wrap = false, m_frozen = false, m_dirty = false;
	uint64_t m_lastDropped = 0;
	uint32_t m_loadSeq = 0;
	float m_worldRect[4] = {0, 0, 0, 0};
	float m_ox = 0, m_oy = 0;
	roadsx::MapIn m_in;

	// in force
	roadsx::RoadDecals m_decals;
	bool m_haveDecals = false;
	uint64_t m_decalsVersion = 0;
	std::vector<c5r::decal::DecalRect> m_pendingRects;
	bool m_pending = false, m_firstSwitch = false;
	uint32_t m_gen = 0;
	std::shared_ptr<const c5r::decal::DecalSet> m_lastTap;   // the newest tap set composed (the first change's route curves)
	std::shared_ptr<const roadsx::RoadFootprint> m_footprint;
	std::shared_ptr<StockRouteClear> m_clear;
	uint64_t m_cutVersion = 0;
	std::shared_ptr<const RoadBridgePlacer> m_bridges;   // ticket 0808: in BridgePlacerSlot while not null
	// compose cache: the tap set and the decals version it was made from
	std::shared_ptr<const c5r::decal::DecalSet> m_cacheTap, m_cacheOut;
	uint64_t m_cacheVersion = 0;
	uint32_t m_clearTapGen = 0xFFFFFFFFu, m_clearLoad = 0xFFFFFFFFu;
	const c5r::decal::DecalSet* m_clearTap = nullptr;
	std::string m_lastErr;

	// the worker: one job slot (a newer input replaces a queued one), one result slot
	std::thread m_worker;
	mutable std::mutex m_mx;
	std::condition_variable m_cv;
	bool m_stop = false, m_running = false, m_working = false;
	std::unique_ptr<Job> m_job;
	std::unique_ptr<Result> m_result;
	std::unique_ptr<ClearJob> m_clearJob;
	std::unique_ptr<ClearResult> m_clearResult;
};

}  // namespace srvextras
