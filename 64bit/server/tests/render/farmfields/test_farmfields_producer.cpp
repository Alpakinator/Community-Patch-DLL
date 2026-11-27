// test_farmfields_producer.cpp - the farm producer of server.exe's renderer (server/src/render/srv_farmfields.cpp, epic
// 0528, ticket 4852). Written 2026-11-18 for this repository (GPL-3.0-or-later). Model: server/tests/render/forest/.
//
// CPU only (no device, no install, no EXE): a fake plot table (FowPlotTable), batches made here, farm_types.xml's shape
// set by hand (or read from a folder the case writes), the ids set or published through srv_farm_ids.h. Cases: the worker
// (busy, settle, a change in play from the cached block graphs, not threaded), the id-wait LOUD line, the ids by art
// define, the revealed-farm rule and its fallbacks, the seed fallback, the composition through Scene (forest(farm(tap))
// with the tap's farm records dropped or kept), the bundle round trip (the span text recorded, restored in a second
// producer: byte-equal decals) and its size, and a negative per error path. Ticket 0653: the farm buildings' list (one
// record per farm, the same step as the fields, built / pillaged / repaired / removed / era / owner, a new load's empty
// list, the worker's step, the rows' LOUD lines, the height hook, a restored span input).
// Ticket 0657: the points for the EXE's placer (vp_farm_points.h): PRODUCING at a load, DONE with one sorted entry per
// record in the world frame, a removal, a new frame, a second load, no DONE without a frame, the named section.
// Output: one line per case, `CASE <name> PASS|FAIL <detail>`; exit code = the number of failed cases, 2 on usage.
//
//   test_farmfields_producer.exe [case ...]      (no argument: every case)
#include "srv_farmfields.h"
#include "srv_farm_ids.h"
#include "feed/srv_feed_server.h"   // SrvFeed::EmptyRecord
#include "srv_render_common.h"
#include "srv_render_scene.h"
#include "srv_options.h"
#include "c5r/farmfields/farmfields.h"
#include "c5r/farmfields/layout.h"
#include "c5r/farmfields/buildings.h"     // ticket 0653
#include "c5r/heightfield/heightfield.h"  // ticket 0653: the height hook
#include "srv_scene_bundle.h"
#include "vp_farm_points.h"        // ticket 0657: the points for the EXE's placer
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <map>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

// server.exe's SrvOptions() (server/src/srv_options.cpp) for the render units: every option at its default
static Vp64Opt::Options g_options;
const Vp64Opt::Options& SrvOptions() { return g_options; }

using namespace srvrender;
namespace cff = c5r::farmfields;

namespace {

std::vector<std::string> g_log;
void HostLog(const char* line) { g_log.push_back(line); }
int CountLog(const char* needle)
{
	int n = 0;
	for(const auto& l : g_log)
		if(l.find(needle) != std::string::npos)
			n++;
	return n;
}
#define CHECK(c, msg)        \
	do                       \
	{                        \
		if(!(c))             \
		{                    \
			why = (msg);     \
			return false;    \
		}                    \
	} while(0)

const uint32_t kW = 16, kH = 12;   // the table (DLL map) in plots
const int kFarm = 3, kWheat = 7;

struct FakeTable : FowPlotTable
{
	std::vector<SrvFeed::PlotRecord> rec;
	uint32_t gen = 1;
	void copy(std::vector<SrvFeed::PlotRecord>& out, uint32_t& g, uint32_t& w, uint32_t& h) override
	{
		out = rec;
		g = gen;
		w = kW;
		h = kH;
	}
};

SrvFeed::PlotRecord Plain(uint8_t terrain = 1)
{
	SrvFeed::PlotRecord r = SrvFeed::EmptyRecord();
	r.terrain = (int8_t)terrain;
	r.plotType = 2;
	r.feature = -1;
	r.improvement = -1;
	r.fow = 3;
	r.known = 3;
	return r;
}

// Every third plot a farm (Europe), laid out for the active team (known bit 1).
std::vector<SrvFeed::PlotRecord> Farmland()
{
	std::vector<SrvFeed::PlotRecord> v(kW * kH, Plain());
	for(uint32_t y = 0; y < kH; ++y)
		for(uint32_t x = 0; x < kW; ++x)
			if((x + 2 * y) % 3 == 0)
			{
				SrvFeed::PlotRecord& r = v[y * kW + x];
				r.improvement = kFarm;
				r.improvementState = 2;
				r.artType = 4;
			}
	return v;
}

cff::FarmTypes VpTypes()
{
	cff::FarmTypes t;
	const uint32_t n[cff::kCategoryCount] = {8, 8, 1, 1, 8, 1, 1, 1};
	for(uint32_t c = 0; c < cff::kCategoryCount; ++c)
		t.hasSpec[c].assign(n[c], 0);
	return t;
}

const float kWorld[4] = {0.0f, 0.0f, 64.0f * kW - 1.0f, 64.0f * (kH + 4) - 1.0f};

// A producer on a fake table with the ids and types set: `table` keeps a pointer to change the records.
std::unique_ptr<FarmFieldsProducer> Make(FakeTable** table, bool threaded = true, std::vector<SrvFeed::PlotRecord> rec = Farmland())
{
	FakeTable* t = new FakeTable();
	t->rec = std::move(rec);
	*table = t;
	std::unique_ptr<FarmFieldsProducer> p(new FarmFieldsProducer(std::unique_ptr<FowPlotTable>(t)));
	FarmFieldsOptions o;
	o.useFeedIds = false;
	o.threaded = threaded;
	p->setOptions(o);
	p->setFarmImprovementId(kFarm);
	p->setWheatResourceId(kWheat);
	p->setFarmTypes(VpTypes());
	return p;
}

uint64_t Hash(const cff::FarmDecals& d)
{
	uint64_t h = 0xcbf29ce484222325ull;
	auto add = [&](const void* p, size_t n) {
		const uint8_t* b = (const uint8_t*)p;
		for(size_t i = 0; i < n; ++i)
		{
			h ^= b[i];
			h *= 0x100000001b3ull;
		}
	};
	if(!d.decals.empty())
		add(d.decals.data(), d.decals.size() * sizeof(c5r::decal::Decal));
	if(!d.vertices.empty())
		add(d.vertices.data(), d.vertices.size() * sizeof(c5r::decal::DecalVertex));
	for(const std::string& s : d.textures)
		add(s.c_str(), s.size() + 1);
	return h;
}

SrvFeed::Batch ChangedBatch(uint32_t plot, uint32_t gen = 1)
{
	SrvFeed::Batch b;
	b.snapshotGen = gen;
	b.width = kW;
	b.height = kH;
	b.changedPlots.push_back(plot);
	return b;
}

// ---- cases ----

bool CaseSettleBusy(std::string& why)
{
	FakeTable* t = nullptr;
	auto p = Make(&t);
	p->setGameSeed(4242);
	p->setWorld(kWorld, 1);
	CHECK(!p->refresh(), "the first refresh only hands the job to the worker");
	CHECK(p->busy() || !p->producer().decals().empty(), "neither busy nor done after the hand-over");
	CHECK(p->settle(20000), "the worker did not settle within 20 s");
	CHECK(!p->busy(), "busy after settle");
	CHECK(!p->producer().decals().empty(), "no decals after the first production");
	CHECK(p->productions == 1 && p->lastMisses > 0, "productions " + std::to_string(p->productions) + ", blocks laid out " +
	                                                     std::to_string(p->lastMisses));
	std::vector<DecalChange> ch;
	CHECK(p->takeDecalChanges(ch) && ch.size() == 1 && ch[0].generation == kFarmDecalGenBase + 1 && !ch[0].rects.empty(),
	      "the first production's change");
	const uint64_t v1 = p->version();
	// a farm pillaged in play: the table changes, a batch names the plot; the change comes from the cached block graphs
	t->rec[0].improvementState = 3;
	CHECK(p->onBatch(ChangedBatch(0)), "onBatch did not see the farm plots change");
	p->refresh();
	CHECK(p->settle(20000), "no settle after the change");
	CHECK(p->version() > v1 && p->lastMisses == 0, "the change: version " + std::to_string(p->version()) + ", blocks laid out " +
	                                                    std::to_string(p->lastMisses) + " (expected 0: cached)");
	ch.clear();
	CHECK(p->takeDecalChanges(ch) && ch.size() == 1 && ch[0].generation == kFarmDecalGenBase + 2, "the change's DecalChange");
	// a batch that changes nothing of the farms: no production
	t->rec[1].feature = 5;
	CHECK(!p->onBatch(ChangedBatch(1)), "a non-farm change counted as a farm change");
	why = "productions " + std::to_string(p->productions) + ", " + std::to_string(p->producer().decals().decals.size()) + " decals, change " +
	      std::to_string(p->lastProduceMs) + " ms";
	return true;
}

bool CaseNotThreaded(std::string& why)
{
	FakeTable* t = nullptr;
	auto p = Make(&t, false);
	p->setGameSeed(4242);
	p->setWorld(kWorld, 1);
	CHECK(p->refresh(), "not threaded: the first refresh produces in place");
	CHECK(!p->busy(), "not threaded: busy");
	// the same input threaded gives the same bytes
	FakeTable* t2 = nullptr;
	auto q = Make(&t2, true);
	q->setGameSeed(4242);
	q->setWorld(kWorld, 1);
	q->refresh();
	CHECK(q->settle(20000), "threaded: no settle");
	CHECK(Hash(p->producer().decals()) == Hash(q->producer().decals()), "threaded and not threaded differ");
	why = std::to_string(p->producer().decals().decals.size()) + " decals both ways";
	return true;
}

bool CaseIdsWaitLoud(std::string& why)
{
	FakeTable* t = nullptr;
	auto p = Make(&t, false);
	p->setFarmImprovementId(-1);
	FarmFieldsOptions o;
	o.useFeedIds = false;
	o.threaded = false;
	o.idsWaitMs = 50;
	p->setOptions(o);
	g_log.clear();
	p->setWorld(kWorld, 1);
	p->refresh();
	CHECK(!p->idsMissing, "LOUD before the wait ran out");
	Sleep(120);
	p->refresh();
	p->refresh();
	CHECK(p->idsMissing && CountLog("LOUD: farmfields: render_farmfields=1 but no farm improvement id") == 1, "the LOUD line, once");
	// without an id the tap's set is handed on as it is
	auto tap = std::make_shared<c5r::decal::DecalSet>();
	tap->worldRect[2] = kWorld[2];
	tap->worldRect[3] = kWorld[3];
	const std::shared_ptr<const c5r::decal::DecalSet> ts = tap;
	CHECK(p->composeDecals(ts) == ts && p->composesWithoutId == 1, "compose without an id is not the tap's set");
	why = "one LOUD line after 50 ms";
	return true;
}

bool CaseIdsByName(std::string& why)
{
	FarmIdsReset();
	FarmImprovementTagSeen(9, "ART_DEF_IMPROVEMENT_PLANTATION");
	FarmImprovementTagSeen(kFarm, "ART_DEF_IMPROVEMENT_FARM");
	FarmImprovementTagSeen(11, "ART_DEF_IMPROVEMENT_FARM");   // a second type with the tag: the lowest id stays
	WheatResourceTagSeen(kWheat, "ART_DEF_RESOURCE_WHEAT");
	WheatResourceTagSeen(-1, "ART_DEF_RESOURCE_WHEAT");
	FarmImprovementTagSeen(4, nullptr);
	FarmIds ids = FarmIdsNow();
	CHECK(ids.farm == kFarm && ids.wheat == kWheat && !ids.resolved, "the ids before resolve");
	FarmIdsResolved();
	ids = FarmIdsNow();
	CHECK(ids.resolved, "not resolved");
	// the producer polls them (useFeedIds), and wheat reaches the farms through the resource
	std::vector<SrvFeed::PlotRecord> rec = Farmland();
	rec[0].resource = kWheat;
	FakeTable* t = nullptr;
	auto p = Make(&t, false, rec);
	p->setFarmImprovementId(-1);
	p->setWheatResourceId(-1);
	FarmFieldsOptions o;
	o.threaded = false;   // useFeedIds on
	p->setOptions(o);
	p->setWorld(kWorld, 1);
	p->refresh();
	CHECK(!p->input().plots.empty() && p->input().plots[0].wheat, "the polled ids did not reach the farm plots");
	FarmIdsReset();
	CHECK(!FarmIdsNow().resolved && FarmIdsNow().farm == -1, "reset");
	why = std::to_string(p->input().plots.size()) + " farms, the first on wheat";
	return true;
}

bool CaseRevealedRule(std::string& why)
{
	std::vector<SrvFeed::PlotRecord> rec(kW * kH, Plain());
	for(auto& r : rec)
	{
		r.improvement = kFarm;
		r.known = 1;   // getter fields only
		r.fow = 0;
	}
	std::vector<cff::FarmPlot> out;
	// (a) no plot laid out, no fog read: every farm counts
	CHECK(FarmPlotsFromTable(rec, kW, kH, kFarm, true, out) && out.size() == kW * kH, "no layout, no fog: every plot");
	// (b) fog read: the revealed ones
	rec[5].fow = 1;
	rec[6].fow = 3;
	CHECK(FarmPlotsFromTable(rec, kW, kH, kFarm, true, out) && out.size() == 2, "fog only: the 2 revealed");
	// (c) a layout seen anywhere: only plots with known bit 1 (their improvement is the revealed one)
	rec[7].known = 3;
	CHECK(FarmPlotsFromTable(rec, kW, kH, kFarm, true, out) && out.size() == 1 && out[0].x == 7, "layout: the 1 laid out");
	rec[8].known = 3;
	rec[8].improvement = 4;   // laid out, revealed as another improvement
	CHECK(FarmPlotsFromTable(rec, kW, kH, kFarm, true, out) && out.size() == 1, "a revealed non-farm counted");
	// revealedOnly off: every true farm
	CHECK(FarmPlotsFromTable(rec, kW, kH, kFarm, false, out) && out.size() == kW * kH - 1, "revealedOnly off");
	// states: half-built, pillaged by state or flag, art type out of range
	rec[7].improvementState = 1;
	CHECK(FarmPlotsFromTable(rec, kW, kH, kFarm, true, out) && out[0].state == cff::FarmState::HalfBuilt, "half-built");
	rec[7].improvementState = 2;
	rec[7].flags = 2;
	CHECK(FarmPlotsFromTable(rec, kW, kH, kFarm, true, out) && out[0].state == cff::FarmState::Pillaged, "pillaged by the flag");
	// negatives: a table of the wrong size, no size
	CHECK(!FarmPlotsFromTable(rec, kW, kH + 1, kFarm, true, out) && out.empty(), "a table of the wrong size accepted");
	CHECK(!FarmPlotsFromTable(rec, 0, 0, kFarm, true, out) && out.empty(), "a table without size accepted");
	why = "layout bit, fog bit, every plot, and the states";
	return true;
}

bool CaseSeedFallback(std::string& why)
{
	FakeTable* t = nullptr;
	auto p = Make(&t, false);
	p->setGameSeed(0);
	p->setWorld(kWorld, 1);
	p->refresh();
	const uint32_t st = StaticPlotSeed(t->rec);
	CHECK(st != 0 && p->producer().input().gameSeed == st, "mapSeed 0: the static plot seed");
	// another map's plots give another seed; the same plots the same
	std::vector<SrvFeed::PlotRecord> other = t->rec;
	other[3].terrain = 5;
	CHECK(StaticPlotSeed(other) != st && StaticPlotSeed(t->rec) == st, "the static seed does not follow the plots");
	// a feature changed in play does not move the seed of the load
	t->rec[2].feature = 5;
	p->onBatch(ChangedBatch(2));
	t->rec[0].improvementState = 3;
	p->onBatch(ChangedBatch(0));
	p->refresh();
	CHECK(p->producer().input().gameSeed == st, "the seed moved inside the load");
	// a map seed wins
	p->setGameSeed(99);
	p->refresh();
	CHECK(p->producer().input().gameSeed == 99, "mapSeed 99 not used");
	why = "static seed " + std::to_string(st);
	return true;
}

std::shared_ptr<c5r::decal::DecalSet> TapWithFarms()
{
	auto tap = std::make_shared<c5r::decal::DecalSet>();
	tap->generation = 5;
	tap->worldRect[2] = kWorld[2];
	tap->worldRect[3] = kWorld[3];
	tap->textures = {"ground.dds", "@farm_types.xml/Crops_Europe/0/albedo", "blank_texture.dds"};
	auto add = [&](float x0, float prio, uint32_t t0, uint32_t t1) {
		c5r::decal::Decal d;
		d.rect[0] = x0;
		d.rect[1] = 200;
		d.rect[2] = x0 + 10;
		d.rect[3] = 210;
		d.priority = prio;
		d.texture[0] = t0;
		d.texture[1] = t1;
		d.texture[2] = 2;
		d.firstVertex = (uint32_t)tap->vertices.size();
		d.vertexCount = 3;
		d.listOrder = (uint32_t)tap->decals.size();
		tap->vertices.resize(tap->vertices.size() + 3, c5r::decal::DecalVertex{0, 0, 0, 0});
		tap->decals.push_back(d);
	};
	add(100, 0.0f, 0, 0);                                                   // a ground
	add(120, 0.5f, 1, 1);                                                   // an EXE farm field
	add(140, 0.6f, c5r::decal::kNoTexture, c5r::decal::kNoTexture);         // an EXE farm track
	c5r::decal::buildIndex(*tap);
	return tap;
}

bool CaseComposeThroughScene(std::string& why)
{
	FakeTable* t = nullptr;
	auto p = Make(&t, false);
	p->setGameSeed(7);
	p->setWorld(kWorld, 1);
	p->refresh();
	const size_t farmDecals = p->producer().decals().decals.size();
	CHECK(farmDecals > 0, "no farm decals");
	Scene scene;
	scene.setFarm(std::move(p));
	const std::shared_ptr<const c5r::decal::DecalSet> tap = TapWithFarms();
	auto out = scene.decalsFor(tap);
	CHECK(out && out != tap, "the scene handed on the tap's set");
	CHECK(out->decals.size() == 1 + farmDecals, "the composed set holds " + std::to_string(out->decals.size()) + " decals, expected " +
	                                                std::to_string(1 + farmDecals) + " (the ground and the server's farms)");
	CHECK(cff::tapFarmCount(*out).fields + cff::tapFarmCount(*out).tracks == farmDecals, "the composed set's farm records are not the server's");
	CHECK(scene.decalsFor(tap) == out, "the composition is not cached per tap set");
	CHECK(scene.decalsFor(nullptr) == nullptr, "a null tap set");
	// keep_tap: a tap set with farm records is handed on as it is (the forest's rule)
	FarmFieldsOptions o;
	o.useFeedIds = false;
	o.threaded = false;
	o.removeTapFarm = false;
	scene.farm()->setOptions(o);
	CHECK(scene.decalsFor(tap) == tap && scene.farm()->tapRefusals == 1, "keep_tap: not the tap's set");
	why = std::to_string(out->decals.size()) + " decals composed";
	return true;
}

bool CaseBundleRoundTrip(std::string& why)
{
	// live: a production on the worker, its span text
	FakeTable* t = nullptr;
	auto live = Make(&t);
	live->setGameSeed(0);   // the static seed path, recorded as the seed in force
	live->setWorld(kWorld, 3);
	live->refresh();
	CHECK(live->settle(20000), "live: no settle");
	std::string text, w;
	CHECK(live->spanState(text, w), "live: no span state: " + w);
	CHECK(text.compare(0, 4, "fv1 ") == 0, "the span text's form");
	// replay: a producer without a table restores it, reads its own types, and produces the same bytes
	auto replay = std::unique_ptr<FarmFieldsProducer>(new FarmFieldsProducer(std::unique_ptr<FowPlotTable>(new FakeTable())));
	FarmFieldsOptions o;
	o.threaded = true;
	replay->setOptions(o);
	CHECK(replay->restoreSpanInput(text, w), "restore refused: " + w);
	replay->setFarmTypes(VpTypes());
	replay->setGameSeed(12345);   // the player's field: the recorded seed stands
	const float otherWorld[4] = {0, 0, 10, 10};
	replay->setWorld(otherWorld, 1);   // the recorded world stands
	SrvFeed::Batch b = ChangedBatch(0, 9);
	CHECK(!replay->onBatch(b), "a restored producer read a table");
	replay->refresh();
	CHECK(replay->settle(20000), "replay: no settle");
	const cff::FarmDecals& a = live->producer().decals();
	const cff::FarmDecals& r = replay->producer().decals();
	CHECK(a.decals.size() == r.decals.size() && Hash(a) == Hash(r), "replay differs: " + std::to_string(r.decals.size()) + " decals against " +
	                                                                    std::to_string(a.decals.size()));
	std::string again;
	CHECK(replay->spanState(again, w) && again == text, "the replay's span text differs");
	why = std::to_string(a.decals.size()) + " decals, hash " + std::to_string(Hash(a)) + ", span text " + std::to_string(text.size()) +
	      " bytes for " + std::to_string(live->producer().input().plots.size()) + " farms";
	return true;
}

// ---- a real bundle on disk (sb::Writer, CheckBundle, ReadManifest): frames and cameras only, as bundle_format_test's ----
namespace sb = scenebundle;

std::wstring TempBundle(const char* name)
{
	wchar_t tmp[MAX_PATH];
	GetTempPathW(MAX_PATH, tmp);
	std::wstring d = std::wstring(tmp) + L"c5_4852_" + std::to_wstring(GetCurrentProcessId()) + L"_";
	for(const char* p = name; *p; ++p)
		d += (wchar_t)*p;
	return d;
}

bool WriteBundle(const std::wstring& dir, const std::map<std::string, std::string>& rc, std::string& why)
{
	sb::Writer w;
	bool ok = w.open(dir).ok();
	ok = ok && w.appendStart("frames.bin", sb::MemberKind::Frames).ok() && w.appendStart("cameras.bin", sb::MemberKind::CameraBlocks).ok();
	for(int k = 0; k < 3; ++k)
	{
		sb::FrameRecord r;
		memset(&r, 0, sizeof(r));
		r.frameId = 100 + k;
		r.loopIter = 6 + (uint64_t)k;
		r.nowMs = 5000.0 + 16.6 * k;
		r.width = 8;
		r.height = 4;
		VPShellCamera c;
		memset(&c, 0, sizeof(c));
		c.status = 1;
		c.presentCount = 1000 + k;
		c.frameStamp = 100 + k;
		c.numViews = 1;
		c.vpW = c.bbW = 8;
		c.vpH = c.bbH = 4;
		c.gridSeq = 3;
		c.wrapPeriod = 127;
		c.sx = 1;
		c.sy = 0.866f;
		c.samplesX = 128;
		c.samplesY = 192;
		ok = ok && w.append("frames.bin", &r, sizeof(r)).ok() && w.append("cameras.bin", &c, sizeof(c)).ok();
	}
	sb::Manifest m;
	m.bundleId = "SYNTH-4852_20261118-000000_test";
	m.created = "2026-11-18T00:00:00-05:00";
	m.recorderKeys["render_test_record"] = "x";
	m.recorderKeys["render_test_record_frames"] = "3";
	m.serverExeSha256 = std::string(64, 'a');
	m.repoCommit = std::string(40, 'e');
	m.buildConfig = "release";
	m.pins["dll_lock.dll_sha256"] = std::string(64, 'b');
	m.pins["server_sha256"] = std::string(64, 'c');
	m.saveName = "SYNTH";
	m.saveSha256 = std::string(64, 'd');
	m.mapW = kW;
	m.mapH = kH;
	m.turn = 9;
	m.loadSeq = 1;
	m.vt = "absent";
	m.install = "C:/nowhere";
	m.machine = "test";
	m.adapterLuid = "00000000-0000abcd";
	m.adapterDescription = "Synthetic";
	m.adapterDriver = "1.2.3";
	m.renderConfig["render_pass"] = "-1";
	m.renderConfig["render_tiles"] = "bake";
	for(const auto& kv : rc)
		m.renderConfig[kv.first] = kv.second;
	m.versionNotes.push_back("gap: bake-cache tap left out (render_tiles=bake)");
	m.firstFrameId = 100;
	m.frameCount = 3;
	if(!ok || !w.finish(m).ok())
		return why = "the bundle was not written", false;
	sb::CheckResult r;
	if(!sb::CheckBundle(dir, &r).ok())
		return why = "CheckBundle refused the bundle: " + r.line(), false;
	return true;
}

// The bundle a farm-producing server records, played back by the player's rules (render_farmfields=1: the state from the
// manifest read off disk, restored into a producer that is not threaded and has no table, as sceneshot's): exactly the
// recorded producer output (count and hash of the records).
bool CaseBundlePlaybackEqualsRecorded(std::string& why)
{
	FakeTable* t = nullptr;
	auto live = Make(&t);
	live->setGameSeed(0);
	live->setWorld(kWorld, 3);
	live->refresh();
	CHECK(live->settle(20000), "live: no settle");
	std::string text, w;
	CHECK(live->spanState(text, w), "live: no span state: " + w);
	const std::wstring dir = TempBundle("playback");
	std::map<std::string, std::string> rc;
	rc["render_farmfields"] = "1";
	rc["render_farmfields_state"] = text;
	std::string ww;
	CHECK(WriteBundle(dir, rc, ww), ww);
	sb::Manifest m;
	sb::CheckResult r;
	CHECK(sb::ReadManifest(dir, &m, &r).ok(), "manifest not read back: " + r.line());
	CHECK(m.renderConfig["render_farmfields"] == "1" && m.renderConfig["render_farmfields_state"] == text, "the keys did not survive");
	// the player: not threaded, no table, the state restored, farm_types.xml from its install (here: the same shape)
	auto player = std::unique_ptr<FarmFieldsProducer>(new FarmFieldsProducer(std::unique_ptr<FowPlotTable>(new FakeTable())));
	FarmFieldsOptions o;
	o.threaded = false;
	player->setOptions(o);
	player->setFarmTypes(VpTypes());
	CHECK(player->restoreSpanInput(m.renderConfig["render_farmfields_state"], w), "restore refused: " + w);
	player->setWorld(kWorld, 1);
	player->refresh();
	const cff::FarmDecals& a = live->producer().decals();
	const cff::FarmDecals& b = player->producer().decals();
	CHECK(!a.decals.empty() && a.decals.size() == b.decals.size() && Hash(a) == Hash(b),
	      "played back " + std::to_string(b.decals.size()) + " decals (hash " + std::to_string(Hash(b)) + ") against recorded " +
	          std::to_string(a.decals.size()) + " (hash " + std::to_string(Hash(a)) + ")");
	why = "recorded = played back: " + std::to_string(a.decals.size()) + " records, hash " + std::to_string(Hash(a)) + "; state " +
	      std::to_string(text.size()) + " bytes";
	return true;
}

// An older player knows no farm keys: the same bundle loads (the checker refuses no render_config key it does not know,
// and the manifest's other content is unchanged), and with render_farmfields off (what an older player's options give) the
// scene hands the residency the tap's set as it is: the tap's own farm fields.
bool CaseOlderPlayerLoadsFarmBundle(std::string& why)
{
	const std::wstring dir = TempBundle("older");
	std::map<std::string, std::string> rc;
	rc["render_farmfields"] = "1";
	rc["render_farmfields_state"] = "fv1 seed=1 farm=3 wheat=-1 map=16,12 world=0x0p+0,0x0p+0,0x1.ffcp+9,0x1.ffcp+9 farms=1.1.2.4.0";
	rc["render_farmfields_from_a_later_ticket"] = "x";   // a key this reader does not know: not refused either
	std::string ww;
	CHECK(WriteBundle(dir, rc, ww), ww);
	sb::Manifest m;
	sb::CheckResult r;
	CHECK(sb::ReadManifest(dir, &m, &r).ok() && m.frameCount == 3, "the bundle does not read back: " + r.line());
	Scene older;   // no producer: render_farmfields not known, so off
	const std::shared_ptr<const c5r::decal::DecalSet> tap = TapWithFarms();
	CHECK(older.farm() == nullptr && older.decalsFor(tap) == tap, "without a producer the scene does not hand on the tap's set");
	why = "the farm bundle passes CheckBundle and reads back; the older scene draws the tap's farm fields";
	return true;
}

bool CaseNegatives(std::string& why)
{
	FarmFieldsProducer p(std::unique_ptr<FowPlotTable>(new FakeTable()));
	std::string text, w;
	CHECK(!p.spanState(text, w) && text.empty() && !w.empty(), "a span state without a production");
	const char* bad[] = {"", "xx1 seed=1", "fv2 seed=1 farm=3 wheat=-1 map=16,12 world=0x0p+0,0x0p+0,0x1p+10,0x1p+10 farms=",
	                     "fv1 seed=1 farm=3", "fv1 seed=1 farm=3 wheat=-1 map=16,12 world=0x0p+0,0x0p+0,0x1p+10,0x1p+10 farms=1.1.9.1.0",
	                     "fv1 seed=1 farm=3 wheat=-1 map=16,12 world=0x0p+0,0x0p+0,0x1p+10,0x1p+10 farms=1.1.2.1",
	                     "fv1 seed=1 farm=3 wheat=-1 map=16,12 world=0x0p+0,0x0p+0,0x1p+10,0x1p+10 farms=1.1.2.1.0;",
	                     "fv1 seed=1 farm=3 wheat=-1 map=16,12 world=0x0p+0,0x0p+0,0x1p+10,0x1p+10 farms=40.1.2.1.0",
	                     "fv1 seed=1 farm=3 wheat=-1 map=0,0 world=0x0p+0,0x0p+0,0x1p+10,0x1p+10 farms="};
	for(const char* s : bad)
	{
		w.clear();
		CHECK(!p.restoreSpanInput(s, w) && !w.empty() && !p.restored(), std::string("a bad span text restored: '") + s + "'");
	}
	cff::LayoutInput li;
	int f = 0, wh = 0;
	CHECK(cff::parseSpanText(bad[2], li, f, wh).code == c5r::Err::BadVersion, "fv2 is not BadVersion");
	// farm_types.xml that does not parse, or no install
	cff::FarmTypes ft;
	CHECK(!FarmTypesFromXml("<farm_types><Crops_Africa>", ft, w) && !w.empty(), "an unclosed farm_types.xml parsed");
	g_log.clear();
	FarmFieldsOptions o;
	o.useFeedIds = false;
	o.threaded = true;
	p.setOptions(o);
	p.loadFarmTypesFrom(L"Z:\\no\\such\\install", std::vector<std::wstring>());
	CHECK(p.settle(20000), "no settle after the types read");
	p.refresh();
	CHECK(CountLog("LOUD: farmfields: farm_types.xml cannot be read") == 1, "no LOUD line for a missing farm_types.xml");
	// a table of the wrong size: counted, LOUD, no farms
	FakeTable* t = nullptr;
	std::vector<SrvFeed::PlotRecord> shortRec = Farmland();
	shortRec.pop_back();
	auto q = Make(&t, false, shortRec);
	q->setWorld(kWorld, 1);
	CHECK(q->tableErrors == 1 && q->input().plots.empty(), "a short table accepted");
	// (a production the invariants refuse is not reachable from the table: the feed stores art types 0..4 and states 1..3,
	// and a restored input passed checkInput; c5r::farmfields' own suite tests that path, render/tests/farmfields)
	why = "9 bad span texts, farm_types.xml, a short table";
	return true;
}

// Ticket 4853 (farm-fields-layout.md 14.2): the fields' footprints (bit 0x01) of the production in force, and through
// CutOverlaySlot in the forest's cut source (a CutTapReader without a control block: its own grid is empty, so every bit it
// reports is the overlay's).
bool CaseFootprintIntoCut(std::string& why)
{
	FakeTable* t = nullptr;
	auto p = Make(&t, false);
	const float ox = 31.5f, oy = 12.25f;
	p->setCutFrame(ox, oy);
	p->setGameSeed(4853);
	p->setWorld(kWorld, 1);
	CHECK(!p->footprint() && p->footprintVersion() == 0, "a footprint before the first production");
	p->refresh();
	CHECK(!p->producer().decals().empty() && p->footprint(), "no footprint with the first production");
	const uint64_t v1 = p->footprintVersion();
	CHECK(v1 == 1 && p->footprints == 1 && p->footprint()->generation() == 1, "footprint version " + std::to_string(v1));
	const size_t fields1 = p->producer().layout().fields.size();
	// the same cells as a footprint built from the layout in force
	cff::FieldFootprint ref;
	CHECK(ref.build(p->producer().layout(), kWorld).ok(), "the reference footprint did not build");
	CHECK(ref.hash() == p->footprint()->hash() && ref.cellsSet() == p->footprint()->cellsSet() && ref.cellsSet() > 0,
	      "the producer's footprint differs from the layout's (" + std::to_string(p->footprint()->cellsSet()) + " / " +
	          std::to_string(ref.cellsSet()) + " cells)");
	// a field's vertex mean in world units (cutCell's frame inverted) reads bit 0x01 through the reader and the slot
	int32_t col = -1, row = -1;
	for(const cff::FieldPolygon& f : p->producer().layout().fields)
	{
		double cx = 0, cy = 0;
		for(size_t k = 0; k < f.corners.size() / 2; ++k)
		{
			cx += f.corners[2 * k];
			cy += f.corners[2 * k + 1];
		}
		cx /= (double)(f.corners.size() / 2);
		cy /= (double)(f.corners.size() / 2);
		if(p->footprint()->cell((int32_t)cx >> 1, (int32_t)cy >> 1))
		{
			col = (int32_t)cx >> 1;
			row = (int32_t)cy >> 1;
			break;
		}
	}
	CHECK(col >= 0, "no field's vertex mean lies in a set cell");
	const float wx = (float)(2 * col + 1) - ox, wy = (float)(2 * row + 1) / c5r::forest::kCutRowScale - oy;
	CutTapReader reader(0xFFFFFFF0u, L"Local\\VPShellCutTap.test4853.none");
	CHECK(reader.bitsAt(wx, wy) == 0 && !CutOverlaySlot().active(), "bits before the overlay");
	const uint64_t g0 = reader.generation();
	CutOverlaySlot().set(p->footprint());
	CHECK(reader.bitsAt(wx, wy) == cff::kFieldFootprintBit && reader.generation() == g0 + 1, "the overlay's bit or generation missing");
	CHECK(reader.bitsAt(-5000.0f, wy) == 0, "a bit outside the world");
	// a pillage keeps the geometry (a Crops_Damage copy): the decals change, the footprint does not
	const uint32_t mid = 6 * kW + 6;   // plot (6, 6): a farm of Farmland() away from the map's edges
	CHECK(t->rec[mid].improvement == kFarm, "plot (6, 6) is not a farm");
	t->rec[mid].improvementState = 3;
	CHECK(p->onBatch(ChangedBatch(mid)), "the pillage not seen");
	p->refresh();
	CHECK(p->productions == 2 && p->footprintVersion() == v1, "a pillage moved the footprint (version " +
	                                                              std::to_string(p->footprintVersion()) + ", productions " +
	                                                              std::to_string(p->productions) + ")");
	// a farm removed: its fields go, the footprint moves
	t->rec[mid].improvement = -1;
	CHECK(p->onBatch(ChangedBatch(mid)), "the removal not seen");
	p->refresh();
	CHECK(p->footprintVersion() == v1 + 1 && p->footprint()->cellsSet() < ref.cellsSet(), "the removal did not shrink the footprint");
	CutOverlaySlot().set(p->footprint());
	CHECK(reader.generation() == g0 + 2, "the second overlay did not move the reader's generation");
	// a new load drops it
	p->setWorld(kWorld, 2);
	CHECK(!p->footprint() && p->footprintVersion() == v1 + 2, "the footprint survived a new load");
	CutOverlaySlot().set(nullptr);
	CHECK(reader.bitsAt(wx, wy) == 0 && !CutOverlaySlot().active(), "the cleared overlay still reports bits");
	char tl[1024];
	p->totalsLine(tl, sizeof(tl));
	CHECK(std::string(tl).find("footprints=2 footprint_err=0") != std::string::npos, std::string("totals: ") + tl);
	why = std::to_string(ref.cellsSet()) + " cells under " + std::to_string(fields1) + " fields at first";
	return true;
}

// ---- ticket 0653: the farm buildings' records ----

cff::FarmRowChoice Rows()
{   // the CP + VP table: three Ancient rows (level 1), three Industrial rows (level 3)
	cff::FarmRowChoice r;
	r.level[1] = {100, 101, 102};
	r.level[3] = {200, 201, 202};
	r.farmHandlerRows = 24;
	return r;
}

// Farmland() with owners: x < 8 player 1, else player 2
std::vector<SrvFeed::PlotRecord> OwnedFarmland()
{
	std::vector<SrvFeed::PlotRecord> v = Farmland();
	for(uint32_t y = 0; y < kH; ++y)
		for(uint32_t x = 0; x < kW; ++x)
			v[y * kW + x].owner = (int8_t)(x < 8 ? 1 : 2);
	return v;
}

// The records a direct placeBuildings gives for the production in force (owners from `rec`).
bool SameAsDirect(const FarmFieldsProducer& p, const std::vector<SrvFeed::PlotRecord>& rec, const cff::FarmRowChoice& rows, std::string& why)
{
	const cff::LayoutInput& in = p.producer().input();
	cff::BuildingFacts f;
	for(int i = 0; i < 64; ++i)
		f.eraOfPlayer[i] = p.ownerEra(i);
	for(const cff::FarmPlot& fp : in.plots)
		f.owner.push_back(rec[fp.y * kW + fp.x].owner);
	std::vector<cff::FarmBuilding> want;
	const c5r::Status st = cff::placeBuildings(in, p.producer().layout(), f, rows, nullptr, want);
	if(!st.ok())
		return why = "direct placeBuildings: " + st.str(), false;
	if(!(p.buildings()->buildings == want))
		return why = "the producer's records differ from a direct placeBuildings", false;
	return true;
}

const cff::FarmBuilding* Rec(const FarmFieldsProducer& p, int32_t x, int32_t y)
{
	for(const cff::FarmBuilding& b : p.buildings()->buildings)
		if(b.x == x && b.y == y)
			return &b;
	return nullptr;
}

bool CaseBuildingsFollowTheFields(std::string& why)
{
	FakeTable* t = nullptr;
	auto p = Make(&t, false, OwnedFarmland());
	FarmFieldsOptions o;
	o.useFeedIds = false;
	o.threaded = false;
	o.useFeedPlayers = false;
	p->setOptions(o);
	const uint64_t g0 = p->buildingsGeneration();
	CHECK(p->buildings() && p->buildings()->buildings.empty() && g0 == 0, "a new producer's list is not empty generation 0");
	p->setGameSeed(4242);
	p->setWorld(kWorld, 1);
	const uint64_t gLoad = p->buildingsGeneration();
	CHECK(gLoad == g0 + 1 && p->buildings()->buildings.empty() && p->buildings()->loadSeq == 1, "the load's first list is not an empty new generation");
	p->setOwnerEra(1, 0);   // Ancient -> level 1
	p->setOwnerEra(2, 5);   // Modern -> level 4 -> the rows of level 3
	p->setFarmRows(Rows());
	CHECK(p->refresh(), "the first production");
	const auto l1 = p->buildings();
	CHECK(l1->generation == gLoad + 1, "the first production made " + std::to_string(l1->generation - gLoad) + " lists (want 1)");
	CHECK(l1->buildings.size() == p->producer().input().plots.size() && !l1->buildings.empty(), "not one record per farm plot");
	CHECK(l1->fieldsVersion == p->version(), "the list names another fields version");
	if(!SameAsDirect(*p, t->rec, Rows(), why))
		return false;
	for(const cff::FarmBuilding& b : l1->buildings)
	{
		CHECK(b.owner == (b.x < 8 ? 1 : 2), "an owner not from the table at " + cff::buildingText(b));
		CHECK(b.rowLevel == (b.x < 8 ? 1 : 3), "a row level not from the owner's era at " + cff::buildingText(b));
		CHECK(!b.hasHeight, "a height without the hook");
	}
	// a farm pillaged: its record changes in the SAME refresh as its fields
	const int32_t mx = 6, my = 6;
	const uint32_t mid = my * kW + mx;
	CHECK(t->rec[mid].improvement == kFarm && Rec(*p, mx, my) && !Rec(*p, mx, my)->pillaged, "plot (6, 6) is no unpillaged farm");
	t->rec[mid].improvementState = 3;
	const uint64_t v1 = p->version(), gb1 = p->buildingsGeneration();
	CHECK(p->onBatch(ChangedBatch(mid)), "the pillage not seen");
	p->refresh();
	CHECK(p->version() > v1 && p->buildingsGeneration() == gb1 + 1, "the pillage: fields version " + std::to_string(p->version() - v1) +
	                                                                     " steps, building generation " +
	                                                                     std::to_string(p->buildingsGeneration() - gb1) + " steps (want both in one refresh)");
	CHECK(Rec(*p, mx, my) && Rec(*p, mx, my)->pillaged, "the record is not pillaged");
	CHECK(l1->buildings.size() == p->buildings()->buildings.size() && l1->generation == gb1, "the earlier list changed after it was handed out");
	// repaired
	t->rec[mid].improvementState = 2;
	p->onBatch(ChangedBatch(mid));
	p->refresh();
	CHECK(p->buildingsGeneration() == gb1 + 2 && !Rec(*p, mx, my)->pillaged, "the repair");
	// removed
	t->rec[mid].improvement = -1;
	p->onBatch(ChangedBatch(mid));
	p->refresh();
	CHECK(p->buildingsGeneration() == gb1 + 3 && !Rec(*p, mx, my) && p->buildings()->buildings.size() == l1->buildings.size() - 1, "the removal");
	// built again: the same record as at first
	t->rec[mid].improvement = kFarm;
	p->onBatch(ChangedBatch(mid));
	p->refresh();
	CHECK(p->buildingsGeneration() == gb1 + 4 && Rec(*p, mx, my), "the farm built again");
	bool same = p->buildings()->buildings == l1->buildings;
	CHECK(same, "built again: the records differ from the first list");
	// nothing of the farms changes: no new list
	t->rec[1].feature = 5;
	p->onBatch(ChangedBatch(1));
	p->refresh();
	p->refresh();
	CHECK(p->buildingsGeneration() == gb1 + 4, "a non-farm change made a new list");
	// an era change: the rows of player 1's farms change (level 1 -> 3), nothing else
	p->setOwnerEra(1, 4);
	p->refresh();
	CHECK(p->buildingsGeneration() == gb1 + 5 && Rec(*p, 0, 0) && Rec(*p, 0, 0)->rowLevel == 3, "the era change");
	// an owner change in the table: the record follows (culture flip of plot (6, 6) to player 2)
	t->rec[mid].owner = 2;
	p->onBatch(ChangedBatch(mid));
	p->refresh();
	CHECK(p->buildingsGeneration() == gb1 + 6 && Rec(*p, mx, my)->owner == 2, "the owner change");
	if(!SameAsDirect(*p, t->rec, Rows(), why))
		return false;
	// a new load starts from an empty list, filled only by that load's production
	const uint64_t gb7 = p->buildingsGeneration();
	p->setWorld(kWorld, 2);
	CHECK(p->buildingsGeneration() == gb7 + 1 && p->buildings()->buildings.empty() && p->buildings()->loadSeq == 2, "the new load's list is not empty");
	CHECK(p->ownerEra(1) == -1, "the eras survived the new load");
	p->refresh();
	CHECK(p->buildingsGeneration() == gb7 + 2 && p->buildings()->buildings.size() == l1->buildings.size() && p->buildings()->loadSeq == 2,
	      "the new load's production");
	char tl[1024];
	p->totalsLine(tl, sizeof(tl));
	CHECK(std::string(tl).find("buildings=") != std::string::npos, std::string("totals: ") + tl);
	why = std::to_string(l1->buildings.size()) + " records; " + std::to_string(p->buildingLists) + " lists";
	return true;
}

bool CaseBuildingsThreaded(std::string& why)
{
	FakeTable* t = nullptr;
	auto p = Make(&t, true, OwnedFarmland());
	FarmFieldsOptions o;
	o.useFeedIds = false;
	o.useFeedPlayers = false;
	p->setOptions(o);
	p->setGameSeed(4242);
	p->setWorld(kWorld, 1);
	p->setFarmRows(Rows());
	const uint64_t g1 = p->buildingsGeneration();
	p->refresh();
	CHECK(p->buildings()->buildings.empty(), "records before the worker's production was put in force");
	CHECK(p->settle(20000), "no settle");
	CHECK(p->buildingsGeneration() == g1 + 1 && !p->buildings()->buildings.empty(), "no records after the worker's production");
	// a pillage: the records change only in the refresh that puts the worker's production in force
	const uint32_t mid = 6 * kW + 6;
	t->rec[mid].improvementState = 3;
	const uint64_t v1 = p->version(), g2 = p->buildingsGeneration();
	p->onBatch(ChangedBatch(mid));
	int steps = 0;
	while(p->version() == v1 && steps < 2000)
	{
		CHECK(p->buildingsGeneration() == g2, "the records changed before the fields");
		p->refresh();
		Sleep(5);
		steps++;
	}
	CHECK(p->version() > v1 && p->buildingsGeneration() == g2 + 1 && Rec(*p, 6, 6)->pillaged, "the pillage's records not in the fields' step");
	why = std::to_string(steps) + " refreshes until the worker's result";
	return true;
}

bool CaseBuildingsRowsAndHeight(std::string& why)
{
	FakeTable* t = nullptr;
	auto p = Make(&t, false, OwnedFarmland());
	FarmFieldsOptions o;
	o.useFeedIds = false;
	o.threaded = false;
	o.useFeedPlayers = true;   // the batch's player rows (the feed itself is empty here)
	p->setOptions(o);
	p->setGameSeed(4242);
	p->setWorld(kWorld, 1);
	// eras from a batch's player rows
	SrvFeed::Batch b;
	b.snapshotGen = 1;
	b.width = kW;
	b.height = kH;
	SrvFeed::PlayerRecord pr = {};
	pr.player = 2;
	pr.era = 4;
	b.players.push_back(pr);
	p->onBatch(b);
	CHECK(p->ownerEra(2) == 4, "the batch's player row did not set the era");
	// rows: two refused rows are two LOUD lines naming them; the same table again writes nothing
	cff::FarmRowChoice r = Rows();
	cff::FarmRowRefusal f1, f2;
	f1.row = 300;
	f1.text = "row 300 (ART_DEF_IMPROVEMENT_FARM / no resource / Era Ancient / X.fxsxml): its model cannot be read (test)";
	f2.row = 301;
	f2.text = "row 301 (ART_DEF_IMPROVEMENT_FARM / no resource / Era Ancient / NULL model): the row names no model";
	r.refused = {f1, f2};
	g_log.clear();
	p->setFarmRows(r);
	CHECK(CountLog("LOUD: farmfields: farm building row 300") == 1 && CountLog("LOUD: farmfields: farm building row 301") == 1,
	      "the refused rows' LOUD lines");
	p->setFarmRows(r);
	CHECK(CountLog("LOUD:") == 2, "the same table wrote again");
	p->refresh();
	CHECK(!p->buildings()->buildings.empty() && p->buildings()->withoutRow == 0, "records without a row with a table");
	// no row at all: one LOUD line, every record row -1 (nothing drawn, no stand-in)
	g_log.clear();
	const uint64_t g = p->buildingsGeneration();
	p->setFarmRows(cff::FarmRowChoice());
	CHECK(CountLog("LOUD: farmfields: no FARM row of ArtDefine_Landmarks can be drawn") == 1, "the no-row LOUD line");
	p->refresh();
	CHECK(p->buildingsGeneration() == g + 1 && !p->buildings()->buildings.empty() &&
	          p->buildings()->withoutRow == p->buildings()->buildings.size(),
	      "records with a row without a table");
	// the height hook: every record gets the field's height at its position
	auto hg = std::make_shared<c5r::heightfield::HeightGrid>();
	hg->mapW = kW;
	hg->mapH = kH + 4;
	hg->samplesX = kW * 64;
	hg->samplesY = (kH + 4) * 64;
	hg->sz = 0.25f;
	hg->h.assign((size_t)hg->samplesX * hg->samplesY, 200);
	p->setHeightField(hg);
	p->refresh();
	CHECK(p->buildingsGeneration() == g + 2 && p->buildings()->withHeight == p->buildings()->buildings.size(), "the height hook");
	const float want = 200.0f / 255.0f * 0.25f;
	for(const cff::FarmBuilding& rb : p->buildings()->buildings)
		CHECK(rb.hasHeight && std::fabs(rb.z - want) < 1e-6f, "a record's height " + std::to_string(rb.z));
	p->setHeightField(nullptr);
	p->refresh();
	CHECK(p->buildings()->withHeight == 0, "the height stayed after the hook was cleared");
	why = std::to_string(p->buildings()->buildings.size()) + " records";
	return true;
}

bool CaseBuildingsRestoredSpan(std::string& why)
{
	// the bundle's player: a restored span input has no table, so no owners: every record era level 1 (said in the contract)
	FakeTable* t = nullptr;
	auto p = Make(&t, false, OwnedFarmland());
	FarmFieldsOptions o;
	o.useFeedIds = false;
	o.threaded = false;
	o.useFeedPlayers = false;
	p->setOptions(o);
	p->setGameSeed(4242);
	p->setWorld(kWorld, 1);
	p->setFarmRows(Rows());
	p->setOwnerEra(2, 5);
	p->refresh();
	std::string text, w;
	CHECK(p->spanState(text, w), "no span text: " + w);
	auto q = std::make_unique<FarmFieldsProducer>(std::unique_ptr<FowPlotTable>(new FakeTable()));
	q->setOptions(o);
	q->setFarmTypes(VpTypes());
	q->setWorld(kWorld, 1);
	CHECK(q->restoreSpanInput(text, w), "restore: " + w);
	q->setFarmRows(Rows());
	q->refresh();
	CHECK(!p->buildings()->buildings.empty() && q->buildings()->buildings.size() == p->buildings()->buildings.size(),
	      "a restored input's record count");
	for(size_t i = 0; i < q->buildings()->buildings.size(); ++i)
	{
		const cff::FarmBuilding& a = p->buildings()->buildings[i];
		const cff::FarmBuilding& b = q->buildings()->buildings[i];
		CHECK(b.owner == -1 && b.eraLevel == 1, "a restored record with an owner");
		CHECK(memcmp(a.pos, b.pos, sizeof(a.pos)) == 0 && a.facing == b.facing && a.pick == b.pick, "a restored record's place differs");
	}
	why = std::to_string(q->buildings()->buildings.size()) + " records, positions equal";
	return true;
}

// Ticket 0657: the points for the EXE's placer (vp_farm_points.h), read back as the farm guard reads them.
struct PointsCopy
{
	VPFarmHeader h;
	std::vector<VPFarmEntry> e;
};
bool ReadPoints(const void* block, PointsCopy& out)
{
	const VPFarmHeader* h = (const VPFarmHeader*)block;
	const uint32_t s1 = h->seq;
	if(!s1 || (s1 & 1))
		return false;
	MemoryBarrier();
	out.h = *h;
	out.e.assign(VPFARM_ENTRIES(block), VPFARM_ENTRIES(block) + (h->count <= VPFARM_CAPACITY ? h->count : 0));
	MemoryBarrier();
	return h->seq == s1;
}

bool CaseBuildingPoints(std::string& why)
{
	FakeTable* t = nullptr;
	auto p = Make(&t, false, OwnedFarmland());
	FarmFieldsOptions o;
	o.useFeedIds = false;
	o.threaded = false;
	o.useFeedPlayers = false;
	p->setOptions(o);
	std::vector<char> block(VPFARM_SECTION_BYTES, 0);
	p->attachPointMemory(block.data());
	PointsCopy c;
	CHECK(ReadPoints(block.data(), c) && c.h.state == VPFARM_PRODUCING && c.h.count == 0, "an attached block is not PRODUCING and empty");
	const float sx = 1.0f, sy = 0.866f, ox = 512.0f, oy = 300.5f;
	p->setPointFrame(sx, sy, ox, oy);
	p->setGameSeed(4242);
	p->setWorld(kWorld, 7);
	CHECK(ReadPoints(block.data(), c) && c.h.magic == VPFARM_MAGIC && c.h.version == VPFARM_VERSION, "no header after the new load");
	CHECK(c.h.loadSeq == 7 && c.h.state == VPFARM_PRODUCING && c.h.count == 0, "the new load is not PRODUCING with 0 entries");
	const uint32_t gen1 = c.h.generation;
	p->setFarmRows(Rows());
	CHECK(p->refresh(), "the first production");
	CHECK(ReadPoints(block.data(), c) && c.h.state == VPFARM_DONE && c.h.loadSeq == 7, "not DONE after the production");
	CHECK(c.h.generation > gen1 && c.h.serverPid == GetCurrentProcessId(), "generation or pid");
	const auto l1 = p->buildings();
	const auto& bl = l1->buildings;
	CHECK(c.h.count == bl.size() && !bl.empty() && c.h.dropped == 0, "one entry per record");
	// sorted by (hexY, hexX), unique; each entry the record's point in the world frame (or the centre kind)
	uint32_t points = 0;
	for(size_t i = 0; i < c.e.size(); ++i)
	{
		if(i)
			CHECK(VPFarmKeyCmp(c.e[i - 1].hexX, c.e[i - 1].hexY, c.e[i].hexX, c.e[i].hexY) < 0, "entries not sorted and unique");
	}
	for(const cff::FarmBuilding& b : bl)
	{
		const int32_t hx = b.x - (b.y - (b.y & 1)) / 2;
		const VPFarmEntry* e = VPFarmFind(c.e.data(), (uint32_t)c.e.size(), hx, b.y);
		CHECK(e != nullptr, "no entry for " + cff::buildingText(b));
		CHECK(e->kind == (b.field >= 0 ? (uint32_t)VPFARM_KIND_POINT : (uint32_t)VPFARM_KIND_CENTRE), "kind of " + cff::buildingText(b));
		CHECK(e->wx == b.pos[0] * sx - ox && e->wy == b.pos[1] * sy - oy, "world point of " + cff::buildingText(b));
		points += e->kind == VPFARM_KIND_POINT ? 1 : 0;
	}
	CHECK(points == c.h.points && points > 0, "the points count");
	CHECK(!VPFarmFind(c.e.data(), (uint32_t)c.e.size(), 9999, 0), "an entry found for a plot that has none");
	// nothing changed: no new publish
	const uint32_t gen2 = c.h.generation;
	p->refresh();
	CHECK(ReadPoints(block.data(), c) && c.h.generation == gen2, "a publish without a change");
	// a farm removed: its entry goes in the same refresh
	const int32_t mx = 6, my = 6;
	const uint32_t mid = my * kW + mx;
	t->rec[mid].improvement = -1;
	p->onBatch(ChangedBatch(mid));
	p->refresh();
	CHECK(ReadPoints(block.data(), c) && c.h.generation > gen2 && c.h.count == bl.size() - 1 && c.h.state == VPFARM_DONE, "the removal's publish");
	CHECK(!VPFarmFind(c.e.data(), (uint32_t)c.e.size(), mx - (my - (my & 1)) / 2, my), "the removed farm's entry stayed");
	// the frame moves the points
	const uint32_t gen3 = c.h.generation;
	p->setPointFrame(sx, sy, ox + 10.0f, oy);
	CHECK(ReadPoints(block.data(), c) && c.h.generation > gen3, "a new frame not published");
	// a new load: PRODUCING with the new loadSeq at once, 0 entries (the guard never takes the old load's points)
	p->setWorld(kWorld, 8);
	CHECK(ReadPoints(block.data(), c) && c.h.loadSeq == 8 && c.h.state == VPFARM_PRODUCING && c.h.count == 0, "the second load");
	p->refresh();
	CHECK(ReadPoints(block.data(), c) && c.h.loadSeq == 8 && c.h.state == VPFARM_DONE && c.h.count > 0, "the second load's DONE");
	// no frame yet on a fresh producer: never DONE (the points' frame is unknown)
	FakeTable* t2 = nullptr;
	auto q = Make(&t2, false, OwnedFarmland());
	q->setOptions(o);
	std::vector<char> block2(VPFARM_SECTION_BYTES, 0);
	q->attachPointMemory(block2.data());
	q->setGameSeed(4242);
	q->setWorld(kWorld, 1);
	q->setFarmRows(Rows());
	q->refresh();
	CHECK(ReadPoints(block2.data(), c) && c.h.state == VPFARM_PRODUCING && c.h.count == 0, "DONE without a frame");
	q->setPointFrame(sx, sy, ox, oy);
	CHECK(ReadPoints(block2.data(), c) && c.h.state == VPFARM_DONE && c.h.count > 0, "the frame's arrival did not publish DONE");
	// the section by name: made, mapped, published; a second handle sees the same bytes
	const std::wstring base = L"Local\\VP0657Test." + std::to_wstring(GetCurrentProcessId());
	q->setPointSection(base);
	HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, (base + VPFARM_SECTION_SUFFIX).c_str());
	CHECK(h != nullptr, "the section was not made");
	const void* v = MapViewOfFile(h, FILE_MAP_READ, 0, 0, VPFARM_SECTION_BYTES);
	CHECK(v != nullptr, "the section could not be mapped");
	PointsCopy viaName;
	const bool okName = ReadPoints(v, viaName);
	UnmapViewOfFile(v);
	CloseHandle(h);
	CHECK(okName && viaName.h.state == VPFARM_DONE && viaName.h.count == c.h.count, "the named section's contents");
	why = std::to_string(bl.size()) + " records, " + std::to_string(points) + " at a field's centroid; " +
	      std::to_string(p->pointPublishes) + " publishes";
	return true;
}

struct Case
{
	const char* name;
	bool (*fn)(std::string&);
};
const Case kCases[] = {{"settle_busy", CaseSettleBusy},       {"not_threaded", CaseNotThreaded},
                       {"ids_wait_loud", CaseIdsWaitLoud},    {"ids_by_name", CaseIdsByName},
                       {"revealed_rule", CaseRevealedRule},   {"seed_fallback", CaseSeedFallback},
                       {"compose_through_scene", CaseComposeThroughScene}, {"bundle_round_trip", CaseBundleRoundTrip},
                       {"bundle_playback_equals_recorded", CaseBundlePlaybackEqualsRecorded},
                       {"older_player_loads_farm_bundle", CaseOlderPlayerLoadsFarmBundle},
                       {"negatives", CaseNegatives},          {"footprint_into_cut", CaseFootprintIntoCut},
                       {"buildings_follow_the_fields", CaseBuildingsFollowTheFields},
                       {"buildings_threaded", CaseBuildingsThreaded},
                       {"buildings_rows_and_height", CaseBuildingsRowsAndHeight},
                       {"buildings_restored_span", CaseBuildingsRestoredSpan},
                       {"building_points", CaseBuildingPoints}};

}  // namespace

int main(int argc, char** argv)
{
	LogOpen(nullptr, HostLog);
	std::vector<const Case*> run;
	for(const Case& c : kCases)
	{
		bool want = argc < 2;
		for(int i = 1; i < argc; ++i)
			want = want || std::string(argv[i]) == c.name;
		if(want)
			run.push_back(&c);
	}
	if(run.empty())
	{
		printf("no case matches\n");
		return 2;
	}
	int failed = 0;
	for(const Case* c : run)
	{
		std::string why;
		const bool ok = c->fn(why);
		printf("CASE %s %s %s\n", c->name, ok ? "PASS" : "FAIL", why.c_str());
		fflush(stdout);
		failed += ok ? 0 : 1;
	}
	return failed;
}
