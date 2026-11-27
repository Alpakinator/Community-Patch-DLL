// srv_liveterrain_scene.cpp - Scene::liveTerrainStep: the region hook's place in the server's terrain scene (epic 0465,
// ticket 4907). Written 2026-11-18 by w5-4907 for this repository (GPL-3.0-or-later).
//
// A Scene member defined here (declared in srv_render_scene.h) so that srv_render_scene.cpp, shared with epic 0561 under
// the MC's terms (#6462 (2)), carries only the declaration, the member and one call (Scene::poll, the residency's
// block). Render thread only. What it does, once per poll():
//   1. the first time: a LiveTerrain when the scene has a residency (render_tiles=bake), a section (the live server; the
//      offline player replays the field generations through the bundle, live-terrain.md 6.5) and the switch
//      C5_RENDER_LIVE_TERRAIN is not "0" (a test and tool switch, as C5_RENDER_HELD_BAKE; no VP64Options option: K4);
//   2. a field of a load the hook has not seen: the producer's inputs (the field copied, the bake's plot table, the EXE's
//      grid constants block read as data from the EXE's process) to LiveTerrain::onField; inputs that cannot be had are
//      one LOUD line (onFieldUnavailable) and the load stays as stock draws it;
//   3. the bake's plot table, when the residency took a change, to LiveTerrain::onTable;
//   4. every finished change: a live one's window written into m_grid (ApplyWindow), then the field re-applied once for
//      all of them (applyField: the residency re-bakes only tiles whose hash changed, the trees take the new heights, the
//      water module its field); a "wait for reload" leaves the field as it is (LiveTerrain logs the reason).
//      Since ticket 4909 the re-apply is by region in two steps (ApplyJob below); since ticket 0587 every write (the EXE's
//      field, m_grid) is in the second step, the poll that replaces the pages, and the offline player applies by region too.
#include "srv_render_scene.h"
#include "srv_liveterrain.h"
#include "srv_liveterrain_features.h"
#include "srv_liveterrain_derived.h"   // ticket 4904: the trees on the new ground
#include "srv_liveterrain_icewaves.h"  // ticket 0586: the EXE's ice and coast waves
#include "srv_liveterrain_pages.h"     // ticket 0587: the worker's pages with the windows over the field
#include "c5r/bake/layers.h"                 // ticket 4914 (1): the land/water step's inputs (MakeInstallLandWaterEnabler)
#include "c5r/gamefs/gamefiles.h"
#include "c5r/liveterrain/coastlines.h"
#include "c5r/liveterrain/coaststrips.h"
#include "c5r/liveterrain/producer.h"
#include "srv_options.h"                     // ticket 0613: [Extras] FixUnknownWonderBase (SrvOptions)
#include <stdlib.h>
#include <chrono>
#include <future>
#include <map>
#include <memory>
#include <mutex>

namespace srvrender {

namespace liveterrain {
// Ticket 4909 (live-terrain.md 8.14.3): a live apply between its two render-thread steps. Step 1
// (Scene::liveTerrainStep): the height pages the windows reach built by `built` (std::async) from the scene's field with
// the windows written over it (ticket 0587: BuildPageWithWindows; the field is read only until it ends, nothing written).
// Step 2 (a later poll, once `built` is ready): the EXE write and ApplyWindow per change, the region updates
// (Scene::applyFieldRegion) or the whole field, then noteApplied per change, all in that one poll (0587, 8.14.8).
struct ApplyJob
{
	uint32_t loadSeq = 0;
	std::vector<Result> live;
	std::vector<ApplyStats> stats;
	std::vector<std::string> sha;
	std::vector<bool> ok;
	std::vector<c5r::terrain::SampleRect> allRects;   // every window's rectangles (step 1, for the worker's pages)
	std::vector<c5r::terrain::SampleRect> rects;      // the rectangles of the windows written (step 2)
	std::vector<c5r::bake::HeightPage> pages;         // the worker's
	c5r::Status buildStatus = c5r::Status::Ok();
	double step1Ms = 0, exeMs = 0, windowMs = 0, buildMs = 0, checkMs = 0;
	int64_t q0 = 0;
	uint32_t polls = 0;
	// last: destroyed first, so its destructor waits for the worker before the pages it writes go
	std::future<void> built;
};
void ApplyJobDelete::operator()(ApplyJob* j) const
{
	delete j;
}
// The pages (terrain::pagesForRects of job.allRects) of g with every live window of the job written over it, into
// job.pages (ticket 0587: g is not written), the time into job.buildMs, a failure into job.buildStatus. The worker's
// body (also called inline when the async step is off).
void BuildApplyPages(const c5r::heightfield::HeightGrid& g, ApplyJob& job);
}  // namespace liveterrain
typedef std::unique_ptr<liveterrain::ApplyJob, liveterrain::ApplyJobDelete> ApplyJobPtr;

namespace {
void LiveLog(const char* line)
{
	RNote("%s", line);
}

bool LiveTerrainWanted()
{
	char v[8] = {};
	size_t n = 0;
	if(getenv_s(&n, v, sizeof(v), "C5_RENDER_LIVE_TERRAIN") == 0 && n > 0 && v[0] == '0')
		return false;
	return true;
}

// ticket 4909: a test and tool switch, on unless set to "0" (as C5_RENDER_LIVE_TERRAIN)
bool EnvOn(const char* name)
{
	char v[8] = {};
	size_t n = 0;
	return !(getenv_s(&n, v, sizeof(v), name) == 0 && n > 0 && v[0] == '0');
}

// issue 4305's tile styles, as srv_render_scene.cpp's ApplyTileStyles (a static there): the residency's art types
bool ApplyStylesFromResidency(c5r::terrain::Renderer& r, const ResidencySource& res)
{
	const uint8_t* art = nullptr;
	uint32_t ax = 0, ay = 0;
	bool wrap = false;
	if(!res.artTypes(art, ax, ay, wrap))
		return false;
	const c5r::Status st = r.setTileStyles(art, ax, ay, wrap);
	if(!st.ok())
		RNote("LOUD: terrain: setTileStyles(%ux%u plots) failed: %s; the tiles keep their styles", ax, ay, st.str().c_str());
	return st.ok();
}

// The EXE's grid constants block (VA 0xD90FBC at ImageBase 0x400000, 880 bytes), read as data from the EXE's process
// (no code byte, D4) through 4905's target (srv_liveterrain_exefield.h: OpenExeFieldTarget found the module base, checked
// the PE stamp 0x546CD0A8 and that [0xD90FC4] is the rebased 0xD91270). The EXE is relocated in a live process (module
// bases 0x00740000, 0x00F60000, 0x00EB0000 measured: camera-and-picking.md; W6's note of 2026-11-19), so the block is
// read at exeBase + (0xD90FBC - 0x400000). The block's own GridHeightmap pointer (0xD90FC4, offset 8) must be the
// target's grid address. False (why) otherwise. [I: not yet run live, 2026-11-19 01:50]
bool ReadConstantsBlock(const liveterrain::ExeFieldTarget& t, std::vector<uint8_t>& out, std::string& why)
{
	out.assign(liveterrain::kConstantsBlockBytes, 0);
	SIZE_T got = 0;
	const uint64_t at = t.constantsAddr;   // ticket 4966: the build's own block (cam_wrapx, OpenExeFieldTarget)
	if(!t.process || !ReadProcessMemory((HANDLE)t.process, (LPCVOID)(uintptr_t)at, out.data(), out.size(), &got) || got != out.size())
	{
		why = "ReadProcessMemory of the block (880 bytes at " + std::to_string((unsigned long long)at) + ") failed, error " +
		      std::to_string(GetLastError());
		return false;
	}
	uint32_t gridPtr = 0;
	memcpy(&gridPtr, &out[8], 4);
	if((uint64_t)gridPtr != t.gridAddr)
	{
		char b[160];
		_snprintf_s(b, sizeof(b), _TRUNCATE, "the block's GridHeightmap pointer is %08X, not the target's grid %08llX", gridPtr,
		            (unsigned long long)t.gridAddr);
		why = b;
		return false;
	}
	return true;
}

// ---- ticket 0586: the EXE's ice and coast waves (srv_liveterrain_icewaves.h) ----
// The state per scene lives here, keyed by the scene, so that srv_render_scene.h (shared with epic 0561) gets no member:
// one scene per server process; the entry goes with the process. Render thread only.
struct IceWavesHold
{
	liveterrain::IceWaves iw;
	liveterrain::LiveReloadLink link;
	bool tried = false, on = false;
	uint32_t tableGen = 0;
	int64_t plotsSeen = -1;
	bool dirty = true;
	int64_t postedQpc = 0;
	bool timeoutNoted = false;
};
std::map<const void*, std::unique_ptr<IceWavesHold>>& IceWavesHolds()
{
	static std::map<const void*, std::unique_ptr<IceWavesHold>> m;
	return m;
}
IceWavesHold* IceWavesFor(const void* scene, bool make)
{
	auto& m = IceWavesHolds();
	auto it = m.find(scene);
	if(it != m.end())
		return it->second.get();
	if(!make)
		return nullptr;
	IceWavesHold* h = new IceWavesHold();
	m[scene].reset(h);
	return h;
}
const double kIceWavesAnswerMs = 10000.0;   // no answer after this: one LOUD line per load (the request stays posted)

// A new load of the scene's field (its producer was asked for): the EXE built its ice and waves from this table.
// landWater (MC #9538 item 4): the load's land/water step is wanted; without it a live window owes no Ice_Reload (the
// refresh of 4914 is that step's), so height-only changes behave as before epic 0615.
void IceWavesLoad(const void* scene, uint32_t exePid, uint32_t loadSeq, uint32_t W, uint32_t H,
                  const std::vector<SrvFeed::PlotRecord>& recs, bool landWater)
{
	IceWavesHold* h = IceWavesFor(scene, false);
	if(!h || !h->on)
		return;
	h->iw.setFieldRefresh(landWater);
	if(!h->link.isOpen() || h->link.exePid() != exePid)
	{
		std::string why;
		if(!exePid || !h->link.open(exePid, why))
		{
			RNote("LOUD: live ice/waves: the live reload section for EXE pid %u cannot be made (%s): the ice and coast waves stay as "
			      "loaded until the reload", exePid, exePid ? why.c_str() : "no EXE pid");
			return;
		}
		RNote("live ice/waves: section Local\\VP64LiveReload.%u made (ticket 0586)", exePid);
	}
	h->iw.onLoad(loadSeq, W, H, recs);
	h->dirty = true;
	h->postedQpc = 0;
	h->timeoutNoted = false;
	if(!h->iw.haveLoad())
		RNote("live ice/waves: load seq %u: the table is not %u x %u plots: nothing follows in this load", loadSeq, W, H);
}

void IceWavesDrawn(const void* scene, const std::vector<SrvFeed::PlotRecord>& sent)
{
	IceWavesHold* h = IceWavesFor(scene, false);
	if(!h || !h->on)
		return;
	h->iw.onDrawn(sent);
	h->dirty = true;
}

// Ticket 4914 (live-terrain.md 10.9): the same for a change whose window was written into the field. The window's sample
// rectangles go with it, so a window beside the EXE's ice owes one Ice_Reload (IceWaves::onDrawn with the boxes).
void IceWavesDrawn(const void* scene, const std::vector<SrvFeed::PlotRecord>& sent, const std::vector<liveterrain::Rect>& window)
{
	IceWavesHold* h = IceWavesFor(scene, false);
	if(!h || !h->on)
		return;
	std::vector<liveterrain::IceWaves::SampleBox> boxes;
	boxes.reserve(window.size());
	for(const liveterrain::Rect& wr : window)
	{
		liveterrain::IceWaves::SampleBox b;
		b.x0 = wr.x0;
		b.y0 = wr.y0;
		b.w = wr.w;
		b.h = wr.h;
		boxes.push_back(b);
	}
	h->iw.onDrawn(sent, boxes);
	h->dirty = true;
}

// Every poll: an answer taken (after a good Waves_Reload the water tap is asked for a new copy of the load), then, when the
// table or the field moved, the next request.
void IceWavesPoll(const void* scene, VPShellHeader* hdr, uint32_t fieldLoadSeq, const std::vector<SrvFeed::PlotRecord>& recs,
                  bool tableFits, uint32_t gen, int64_t plotsChanged)
{
	IceWavesHold* h = IceWavesFor(scene, false);
	if(!h || !h->on || !h->link.isOpen() || !h->iw.haveLoad() || h->iw.loadSeq() != fieldLoadSeq)
		return;
	liveterrain::IceWavesAnswer a;
	if(h->link.poll(a))
	{
		bool loud = false;
		const std::string line = h->iw.answered(a, loud);
		RNote("%s", line.c_str());
		if(a.wavesResult == 0 && hdr)   // vplr::kOk: only when the waves were asked (else kNotAsked)
		{
			const uint32_t r = (uint32_t)InterlockedIncrement((volatile LONG*)&VPSHELL_WATERTAP(hdr)->requestSeq);
			RNote("live ice/waves: load seq %u: the coast waves were rebuilt: new water tap copy requested (requestSeq %u)", fieldLoadSeq, r);
		}
		h->postedQpc = 0;
		h->dirty = true;
	}
	else if(h->link.waiting() && h->postedQpc && !h->timeoutNoted && QpcToMs(Qpc() - h->postedQpc) > kIceWavesAnswerMs)
	{
		h->timeoutNoted = true;
		h->iw.noteTimeout();
		RNote("LOUD: live ice/waves: load seq %u: no answer from the proxy for %.0f s (a proxy without ticket 0586, or no in-game "
		      "frame drawn since): the ice and coast waves stay as they are until it answers or the reload",
		      fieldLoadSeq, kIceWavesAnswerMs / 1000.0);
	}
	if(!tableFits)
		return;
	if(gen != h->tableGen || plotsChanged != h->plotsSeen)
	{
		h->tableGen = gen;
		h->plotsSeen = plotsChanged;
		h->dirty = true;
	}
	if(!h->dirty || h->iw.inFlight())
		return;
	h->dirty = false;
	liveterrain::IceWavesRequest req;
	std::string note;
	const bool want = h->iw.want(recs, req, note);
	if(!note.empty())
		RNote("%s", note.c_str());
	if(!want)
		return;
	std::string why;
	if(!h->link.post(req, fieldLoadSeq, why))
	{
		RNote("LOUD: live ice/waves: load seq %u: the request could not be posted (%s)", fieldLoadSeq, why.c_str());
		return;
	}
	h->iw.posted(req);
	h->postedQpc = Qpc();
	RNote("live ice/waves: load seq %u: request posted: %s%s%s (%u ice plots, %u to flip; %u plot(s) of another wave class)", fieldLoadSeq,
	      req.ice ? "ice" : "", req.ice && req.waves ? " + " : "", req.waves ? "waves" : "", req.iceCount, req.expectFlips, req.wavePlots);
}

c5r::terrain::SampleRect SampleRectOf(const liveterrain::Rect& wr)
{
	c5r::terrain::SampleRect sr;
	sr.x0 = wr.x0;
	sr.y0 = wr.y0;
	sr.x1 = wr.x0 + wr.w;
	sr.y1 = wr.y0 + wr.h;
	return sr;
}

// ticket 0587: a test and tool switch, off unless set to "1"
bool EnvSet(const char* name)
{
	char v[8] = {};
	size_t n = 0;
	return getenv_s(&n, v, sizeof(v), name) == 0 && n > 0 && v[0] == '1';
}
// (ticket 4904's InclusiveBox, the change's bounding box, is gone: MC #9538 item 3 passes each rectangle,
// liveterrain::SampleBoxOfRect)

// Ticket 0613: TEST ONLY. C5_RENDER_LIVE_TEST_UNKNOWN_WONDERS=FEATURE_A,FEATURE_B (env only, never set by the product)
// makes the producer see those feature Types as natural wonders the EXE does not know ("C5H_TEST_UNKNOWN_" in front), so
// FixUnknownWonderBase can be checked live: no mod of the test mod set adds a wonder unknown to the EXE (live-terrain.md
// 8.13.5). The EXE still bakes the wonder's own ground at load; the server's producer starts from that bake, as it would
// start from the EXE's Fuji for a real unknown wonder. Scene bundles keep the feed's own names (srv_liveterrain_bundle.cpp),
// so a bundle recorded with the switch does not replay its generations: a test run's bundles are not kept.
const std::vector<std::string>& TestUnknownWonders()
{
	static std::vector<std::string> s_list;
	static bool s_read = false;
	if(!s_read)
	{
		s_read = true;
		char v[512] = {};
		size_t n = 0;
		if(getenv_s(&n, v, sizeof(v), "C5_RENDER_LIVE_TEST_UNKNOWN_WONDERS") == 0 && n > 1)
		{
			std::string cur;
			for(const char* p = v;; ++p)
			{
				if(*p == ',' || *p == 0)
				{
					if(!cur.empty())
						s_list.push_back(cur);
					cur.clear();
					if(!*p)
						break;
				}
				else if(*p != ' ')
					cur += *p;
			}
		}
	}
	return s_list;
}
// Ticket 0613: [Extras] FixUnknownWonderBase's effective state (EnableBugfixes and its own flag), taken once per process
// with one log line (the options are read once at the server's start; render thread only)
bool LiveNeutralWonders()
{
	static int s_on = -1;
	if(s_on < 0)
	{
		const Vp64Opt::ExtraState e = Vp64Opt::Extra(SrvOptions(), Vp64Opt::kFixUnknownWonderBase);
		s_on = e.on ? 1 : 0;
		char d[256];
		Vp64Opt::DescribeExtra(SrvOptions(), Vp64Opt::kFixUnknownWonderBase, d, sizeof(d));
		RNote("live terrain: %s: natural wonders unknown to the EXE %s (ticket 0613)", d,
		      e.on ? "on their own ground, with a load-time pass per load" : "on the EXE's Mount Fuji ground");
		if(!TestUnknownWonders().empty())
		{
			std::string names;
			for(const std::string& w : TestUnknownWonders())
				names += (names.empty() ? "" : ",") + w;
			RNote("LOUD: TEST ONLY: C5_RENDER_LIVE_TEST_UNKNOWN_WONDERS=%s: the producer takes these wonders as unknown to the EXE "
			      "(never set by the product)", names.c_str());
		}
	}
	return s_on == 1;
}
// The feature Type as the producer is given it (render thread only): LiveTerrainFeatureType, or under the test switch above
// the test's name for a listed Type
std::string LiveTerrainProducerFeatureType(int32_t featureId)
{
	std::string t = LiveTerrainFeatureType(featureId);
	for(const std::string& w : TestUnknownWonders())
		if(!t.empty() && t == w)
			return "C5H_TEST_UNKNOWN_" + t;
	return t;
}
}  // namespace

void Scene::liveTerrainStep()
{
	if(!m_liveTried)
	{
		m_liveTried = true;
		if(!m_hdr || !m_res || m_opt.install.empty())
			RNote("live terrain: off (%s)", !m_hdr ? "no section" : !m_res ? "render_tiles is not bake" : "no game install");
		else if(!LiveTerrainWanted())
			RNote("live terrain: off (C5_RENDER_LIVE_TERRAIN=0)");
		else
		{
			liveterrain::LiveTerrain::Config c;
			// ticket 4914 (1): land/water changes go live (4913's step) unless C5_RENDER_LIVE_LANDWATER=0 (a test switch)
			c.factory = liveterrain::MakeC5rProducerFactory(liveterrain::LibraryPathFor(m_opt.install),
			                                                liveterrain::MakeInstallLandWaterEnabler(m_opt.install, LiveLog));
			c.log = LiveLog;
			m_live.reset(new liveterrain::LiveTerrain(c));
			LiveNeutralWonders();   // ticket 0613: its line once, at the start
			RNote("live terrain: on (piece library %s); land/water changes %s", Narrow(liveterrain::LibraryPathFor(m_opt.install)).c_str(),
			      EnvOn("C5_RENDER_LIVE_LANDWATER") ? "live with the load's map seed (ticket 4914)"
			                                        : "wait for the reload (C5_RENDER_LIVE_LANDWATER=0)");
			// ticket 0586: the EXE's ice and coast waves follow (C5_RENDER_LIVE_ICEWAVES=0 turns that off: a test switch)
			IceWavesHold* iwh = IceWavesFor(this, true);
			iwh->tried = true;
			iwh->on = EnvOn("C5_RENDER_LIVE_ICEWAVES");
			RNote("live ice/waves: %s", iwh->on ? "on (the EXE's Ice_Reload / Waves_Reload through the proxy, ticket 0586)"
			                                    : "off (C5_RENDER_LIVE_ICEWAVES=0)");
			char ug[8] = {};
			size_t ugn = 0;
			if(iwh->on && getenv_s(&ugn, ug, sizeof(ug), "C5_RENDER_LIVE_ICEWAVES_TEST_UNGATED") == 0 && ugn > 0 && ug[0] == '1')
			{   // TEST ONLY: "1" lifts the waves' gate (the live check of Waves_Reload while every coast change waits in the field)
				iwh->iw.setWavesGateForTest(false);
				RNote("LOUD: TEST ONLY: C5_RENDER_LIVE_ICEWAVES_TEST_UNGATED: the coast waves follow the DLL's table without the gate "
				      "(never set by the product)");
			}
		}
	}
	if(!m_live || !m_res || !m_haveField || !m_haveGrid)
		return;
	uint32_t gen = 0, w = 0, h = 0;
	const std::vector<SrvFeed::PlotRecord>& recs = m_res->plotRecords(gen, w, h);
	const bool tableFits = m_grid.mapH > 4 && w == m_grid.mapW && h == m_grid.mapH - 4 && recs.size() == (size_t)w * h;
	// 2. a new load
	if(m_liveLoad != m_fieldLoadSeq || !m_liveHaveLoad)
	{
		if(!tableFits)
		{
			if(!m_liveTableWaitNoted)
			{
				m_liveTableWaitNoted = true;
				RNote("live terrain: load seq %u: waiting for the bake's plot table of the field's size (%ux%u plots; table %ux%u)",
				      m_fieldLoadSeq, m_grid.mapW, m_grid.mapH > 4 ? m_grid.mapH - 4 : 0, w, h);
			}
			return;
		}
		m_liveTableWaitNoted = false;
		m_liveHaveLoad = true;
		m_liveLoad = m_fieldLoadSeq;
		m_liveTableGen = gen;
		m_livePlotsSeen = m_res->plotsChanged;
		liveterrain::LoadIn in;
		in.loadSeq = m_fieldLoadSeq;
		std::string why;
		// 4905: the EXE's process, once per load (the EXE's own field is written with every live window; without it no
		// live change is drawn in this load, so the server's picture and the EXE's field never disagree, D118)
		m_liveExe.reset();
		std::unique_ptr<liveterrain::ExeLink> link(new liveterrain::ExeLink());
		if(!liveterrain::OpenExeFieldTarget(exePid(), link->t, why))
		{
			m_live->onFieldUnavailable(m_fieldLoadSeq, "the EXE's field (4905's target): " + why);
			return;
		}
		link->open = true;
		if(!ReadConstantsBlock(link->t, in.constants, why))
		{
			m_live->onFieldUnavailable(m_fieldLoadSeq, "the EXE's grid constants block: " + why);
			return;
		}
		m_liveExe = std::move(link);
		if(!liveterrain::PlotTableFromFeed(recs, w, h, LiveTerrainProducerFeatureType, in.table, why))
		{
			m_live->onFieldUnavailable(m_fieldLoadSeq, why);
			return;
		}
		in.samplesX = m_grid.samplesX;
		in.samplesY = m_grid.samplesY;
		in.heights = m_grid.h;
		in.materials = m_grid.mat;
		in.landWater = EnvOn("C5_RENDER_LIVE_LANDWATER");   // ticket 4914 (1): a test switch, on unless "0"
		in.neutralWonders = LiveNeutralWonders();            // ticket 0613: [Extras] FixUnknownWonderBase (taken once)
		// ticket 4912: the load's map seed from the hosted DLL's PreGame, read here on the render thread when the field is
		// handed over (the DLL read the save's pregame on the game thread before the EXE built this field; one line per load).
		// MC #9538 item 4: only for the land/water step (its one input): with C5_RENDER_LIVE_LANDWATER=0 no read, no LOUD line
		// and no D118 report, one plain line
		const bool landWaterWanted = in.landWater;
		if(!landWaterWanted)
			RNote("%s", liveterrain::MapSeedNotWanted(m_fieldLoadSeq).line.c_str());
		else
		{
			static bool s_seedFaultNoted = false;   // one scene per server process: the fault's LOUD line and report once
			const liveterrain::MapSeedFn seedFn = liveterrain::GetMapSeedSource();
			std::string seedWhy;
			uint32_t seed = 0;
			const bool readOk = seedFn && seedFn(seed, seedWhy);
			const liveterrain::MapSeedVerdict v = liveterrain::DecideMapSeed(m_fieldLoadSeq, seedFn != nullptr, readOk, seed, seedWhy,
			                                                                  liveterrain::HostIsServerExe(), s_seedFaultNoted);
			in.haveMapSeed = v.have;
			in.mapSeed = v.have ? seed : 0;
			RNote("%s", v.line.c_str());
			if(v.report)
			{
				if(const liveterrain::MapSeedFaultFn rep = liveterrain::GetMapSeedFaultReport())
					rep(v.line);
				else
					RNote("LOUD: live terrain: no D118 report writer is registered (it lives beside the seed reader in "
					      "server/src/srv_liveterrain_seed.cpp): the fault is in this log only");
			}
		}
		m_live->onField(std::move(in), recs);
		IceWavesLoad(this, exePid(), m_fieldLoadSeq, w, h, recs, landWaterWanted);   // ticket 0586; MC #9538 item 4
	}
	// 3. the table the residency compared last
	else if(tableFits && (gen != m_liveTableGen || m_res->plotsChanged != m_livePlotsSeen))
	{
		m_liveTableGen = gen;
		m_livePlotsSeen = m_res->plotsChanged;
		m_live->onTable(recs, w, h, LiveTerrainProducerFeatureType);
	}
	// ticket 0586: the EXE's ice and coast waves (an answer, then the next request when the table or the field moved)
	IceWavesPoll(this, m_hdr, m_fieldLoadSeq, recs, tableFits, gen, m_res->plotsChanged);
	// 4. finished changes. Ticket 4909: while a change's pages are being built (step 1 done), the next results wait in
	// LiveTerrain's queue (a newer table merges into them); the job's step 2 runs once its pages are there.
	if(m_liveJob)
	{
		m_liveJob->polls++;
		if(m_liveJob->built.valid() && m_liveJob->built.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			return;
		liveTerrainFinish(false);
		return;
	}
	if(!m_liveRegionTried)
	{
		m_liveRegionTried = true;
		m_liveRegion = EnvOn("C5_RENDER_LIVE_REGION");
		m_liveAsync = EnvOn("C5_RENDER_LIVE_ASYNC");
		RNote("live terrain: apply %s%s (C5_RENDER_LIVE_REGION, C5_RENDER_LIVE_ASYNC; ticket 4909)",
		      m_liveRegion ? "by region (the pages and tiles the window reaches)" : "whole field (C5_RENDER_LIVE_REGION=0, before 4909)",
		      m_liveRegion ? (m_liveAsync ? ", pages built on a worker" : ", pages built on the render thread") : "");
	}
	liveterrain::Result r;
	std::vector<liveterrain::Result> live;
	while(m_live->takeResult(r))
	{
		if(r.out.live)
			live.push_back(std::move(r));
		else if(r.out.reason == liveterrain::Wait::NoChange)
			IceWavesDrawn(this, r.sent);   // ticket 0586: nothing to draw: the field shows this table
	}
	if(live.empty())
		return;
	// Ticket 0587: step 1 writes nothing. The worker builds the pages from m_grid (the field before the windows) with the
	// windows written over it (BuildPageWithWindows); the EXE's field and m_grid are written in step 2, in the poll that
	// replaces the pages, so no frame is drawn with the field and the pages disagreeing (live-terrain.md 8.14.8).
	const int64_t q0 = Qpc();
	ApplyJobPtr job(new liveterrain::ApplyJob());
	job->loadSeq = m_fieldLoadSeq;
	job->q0 = q0;
	job->stats.resize(live.size());
	job->sha.resize(live.size());
	job->ok.assign(live.size(), false);
	job->live = std::move(live);
	if(m_liveRegion)
	{
		for(const liveterrain::Result& lr : job->live)
			for(const liveterrain::Rect& wr : lr.out.window)
				job->allRects.push_back(SampleRectOf(wr));
		if(m_liveAsync)
		{   // the pages on a worker: m_grid is read only until the job ends (no live window is written meanwhile, tapPoll joins it)
			liveterrain::ApplyJob* jp = job.get();
			const c5r::heightfield::HeightGrid* gp = &m_grid;
			job->built = std::async(std::launch::async, [jp, gp]() { liveterrain::BuildApplyPages(*gp, *jp); });
		}
		else
			liveterrain::BuildApplyPages(m_grid, *job);
	}
	job->step1Ms = QpcToMs(Qpc() - q0);
	m_liveJob = std::move(job);
	if(!m_liveAsync || !m_liveRegion)
		liveTerrainFinish(false);   // the same poll, as before 4909 (C5_RENDER_LIVE_REGION=0 is today's whole-field path)
}

namespace liveterrain {
void BuildApplyPages(const c5r::heightfield::HeightGrid& g, ApplyJob& job)
{
	const int64_t q0 = Qpc();
	std::vector<c5r::bake::HeightPageIndex> idx;
	std::vector<const std::vector<Rect>*> windows;
	for(const Result& r : job.live)
		windows.push_back(&r.out.window);
	c5r::Status st = c5r::terrain::pagesForRects(g, job.allRects, idx);
	job.pages.resize(st.ok() ? idx.size() : 0);
	for(size_t i = 0; st.ok() && i < idx.size(); ++i)
		st = BuildPageWithWindows(g, idx[i], c5r::bake::kHeightPageMips, windows, job.pages[i]);
	if(!st.ok())
		job.pages.clear();
	job.buildStatus = st;
	job.buildMs = QpcToMs(Qpc() - q0);
}

// Ticket 4914 (1): the land/water step's inputs from the player's install at run time (D83: nothing of the game's files
// is in the repository): coaststyles.xml and the character spline files it names (Resource/Common/TerrainModels.fpk,
// live-terrain.md 8.10.2) and the TerrainDef's strip materials (8.16), read through the install's index as the residency
// reads its TerrainDef (srv_residency_source.cpp). Read once per factory, on the first load's worker (never the render
// thread); then each load's producer is enabled with that load's map seed.
LandWaterEnabler MakeInstallLandWaterEnabler(const std::wstring& install, void (*log)(const char* line))
{
	struct Inputs
	{
		std::mutex mu;
		bool tried = false, ok = false;
		std::string why;
		c5r::liveterrain::CoastStyles styles;
		c5r::liveterrain::StripMaterials materials;
	};
	std::shared_ptr<Inputs> in = std::make_shared<Inputs>();
	return [in, install, log](c5r::liveterrain::Producer& p, uint32_t mapSeed, std::string& why) -> bool {
		char b[512];
		std::lock_guard<std::mutex> lk(in->mu);
		if(!in->tried)
		{
			in->tried = true;
			const int64_t q0 = Qpc();
			c5r::gamefs::GameFiles fs;
			c5r::Status st = install.empty() ? c5r::Status::fail(c5r::Err::NotFound, "no game install") : fs.open(install);
			const double indexMs = QpcToMs(Qpc() - q0);
			std::vector<uint8_t> bytes;
			std::string origin;
			if(st.ok())
				st = fs.read("coaststyles.xml", bytes, &origin);
			if(st.ok())
			{
				const std::string xml(bytes.begin(), bytes.end());
				st = c5r::liveterrain::CoastStyles::parse(
				    xml,
				    [&fs](const std::string& name, std::string& text) {
					    std::string lower = name;
					    for(char& c : lower)
						    if(c >= 'A' && c <= 'Z')
							    c = (char)(c - 'A' + 'a');
					    std::vector<uint8_t> f;
					    if(!fs.read(lower, f).ok())
						    return false;
					    text.assign(f.begin(), f.end());
					    return true;
				    },
				    in->styles);
			}
			c5r::bake::LayerTables lt;
			if(st.ok())
				st = c5r::bake::loadTerrainDef(fs, lt);
			if(st.ok())
				st = c5r::liveterrain::StripMaterials::fromLayerTables(lt, in->materials);
			in->ok = st.ok();
			in->why = st.ok() ? std::string() : st.str();
			if(log)
			{
				if(in->ok)
					_snprintf_s(b, sizeof(b), _TRUNCATE,
					            "live terrain: land/water inputs read in %.0f ms (the install's index %.0f ms; coaststyles.xml from %s, "
					            "TerrainDef; ticket 4914)",
					            QpcToMs(Qpc() - q0), indexMs, origin.c_str());
				else
					_snprintf_s(b, sizeof(b), _TRUNCATE,
					            "LOUD: live terrain: the land/water step's inputs cannot be read from the install (%s): land/water changes "
					            "wait for the reload (LandWaterInputs) in every load of this session; height changes go on live",
					            in->why.c_str());
				log(b);
			}
		}
		if(!in->ok)
		{
			why = "the install's coast styles or TerrainDef could not be read: " + in->why;
			return false;
		}
		const int64_t q1 = Qpc();
		const c5r::Status st = p.enableLandWater(in->styles, in->materials, mapSeed);
		const double ms = QpcToMs(Qpc() - q1);
		if(!st.ok())
		{
			why = "enableLandWater: " + st.str();
			if(log)
			{
				_snprintf_s(b, sizeof(b), _TRUNCATE,
				            "LOUD: live terrain: the land/water step could not be turned on for this load (%s, %.0f ms): its land/water "
				            "changes wait for the reload (LandWaterInputs)",
				            st.str().c_str(), ms);
				log(b);
			}
			return false;
		}
		if(log)
		{
			_snprintf_s(b, sizeof(b), _TRUNCATE, "live terrain: land/water step on for this load (enableLandWater %.1f ms, map seed %u)", ms,
			            mapSeed);
			log(b);
			if(const uint32_t bad = p.loadCoastDegenerate())   // MC #9538 item 1 (live-terrain.md 8.10.3)
			{
				_snprintf_s(b, sizeof(b), _TRUNCATE,
				            "LOUD: live terrain: the load's coast curves had %u zero-length segment(s) or tangent(s), stepped over (no NaN "
				            "point; live-terrain.md 8.10.3)",
				            bad);
				log(b);
			}
		}
		return true;
	};
}
}  // namespace liveterrain

void Scene::liveTerrainJoin()
{
	if(m_res)
		m_res->prepJoin();   // ticket 0712: the bake's prep worker reads m_grid too (tapPoll replaces it after this call)
	if(!m_liveJob)
		return;
	if(m_liveJob->built.valid())
		m_liveJob->built.wait();
	liveTerrainFinish(true);
}

// Step 2 of a live apply (ticket 4909): the region updates with the worker's pages, or today's whole field (the switch,
// or a region step that failed: LOUD, D118), then noteApplied per change. discard: a new load's field replaces m_grid
// (the changes are of the old load; nothing is drawn from them).
void Scene::liveTerrainFinish(bool discard)
{
	ApplyJobPtr job = std::move(m_liveJob);
	if(!job)
		return;
	if(job->built.valid())
		job->built.wait();
	if(discard || job->loadSeq != m_fieldLoadSeq || !m_haveField)
	{
		RNote("live terrain: %zu change(s) of load seq %u dropped before their region step (%s; nothing of them was written)",
		      job->live.size(), job->loadSeq, discard ? "a new load's field" : "the field changed");
		return;
	}
	const int64_t s0 = Qpc();
	if(m_res)
		m_res->prepJoin();   // ticket 0712: the bake's prep worker reads m_grid; the windows below write it in place
	// Ticket 0587: the writes, in the poll that replaces the pages (before 0587 they were step 1's)
	bool any = false, all = true;
	for(size_t k = 0; k < job->live.size(); ++k)
	{
		std::string why;
		// 4905: the EXE's field first, while m_grid is still the field before this window (it refuses when the EXE's
		// field differs from it); only a window the EXE took is drawn
		if(!m_liveExe)
		{
			m_live->noteApplyError(job->live[k], "no EXE field target for this load");
			all = false;
			continue;
		}
		const int64_t x0 = Qpc();
		const liveterrain::ExeFieldResult xr = liveterrain::WriteExeFieldWindow(m_liveExe->t, m_grid, job->live[k].out.window);
		job->exeMs += QpcToMs(Qpc() - x0);
		if(!xr.ok())
		{
			m_live->noteApplyError(job->live[k], "the EXE's field: " + xr.line);
			all = false;
			continue;
		}
		RLog("live terrain: change %llu: %s", (unsigned long long)job->live[k].job, xr.line.c_str());
		const int64_t w0 = Qpc();
		job->ok[k] = liveterrain::ApplyWindow(m_grid, job->live[k].out.window, job->stats[k], why);
		if(!job->ok[k])
		{
			m_live->noteApplyError(job->live[k], why);
			all = false;
		}
		else   // the generation's hash right after its own window (the next one may overlap it): replayable job by job
		{
			job->sha[k] = liveterrain::RegionSha256(m_grid, job->stats[k].x0, job->stats[k].y0, job->stats[k].x1, job->stats[k].y1);
			for(const liveterrain::Rect& wr : job->live[k].out.window)
				job->rects.push_back(SampleRectOf(wr));
		}
		job->windowMs += QpcToMs(Qpc() - w0);
		any = any || job->ok[k];
	}
	if(!any)
		return;   // nothing written (each refusal has its line)
	// 4904: the trees rebuild only the built plots whose trees read a written box (inclusive samples) at their onField.
	// MC #9538 item 3: one box per written rectangle, not the change's bounding box (a window across the x seam is two
	// rectangles at the field's ends; their bounding box is the whole width)
	std::vector<liveterrain::SampleBox> boxes;
	for(size_t k = 0; k < job->live.size(); ++k)
		if(job->ok[k])
			for(const liveterrain::Rect& wr : job->live[k].out.window)
				boxes.push_back(liveterrain::SampleBoxOfRect(wr.x0, wr.y0, wr.w, wr.h));
	liveterrain::DerivedAfterWindow(m_forest.get(), boxes);
	std::string how;
	bool region = false;
	if(m_liveRegion)
	{
		std::string why, line;
		// the worker's pages hold every window; when one was not written they are not the field's: built again here
		const std::vector<c5r::bake::HeightPage>* pages = all ? &job->pages : nullptr;
		if(!all)
			RNote("live terrain: a window of this apply was not written: its pages are built again from the field on the render thread");
		if(pages && EnvSet("C5_RENDER_LIVE_PAGECHECK"))
		{   // TEST ONLY (ticket 0587): the worker's pages against a build from the written field, page by page and level by level
			const int64_t c0 = Qpc();
			size_t same = 0;
			for(const c5r::bake::HeightPage& wp : job->pages)
			{
				c5r::bake::HeightPage fp;
				const c5r::Status cs = c5r::bake::buildHeightPage(m_grid, wp.index, c5r::bake::kHeightPageMips, fp);
				if(cs.ok() && fp.levels == wp.levels)
					same++;
				else
					RNote("LOUD: TEST ONLY: live terrain page check: page (%u, %u) of the worker differs from a build from the written field%s",
					      wp.index.px, wp.index.py, cs.ok() ? "" : (" (" + cs.str() + ")").c_str());
			}
			job->checkMs = QpcToMs(Qpc() - c0);
			RNote("live terrain page check (TEST ONLY, C5_RENDER_LIVE_PAGECHECK): %zu of %zu worker page(s) equal to a build from the "
			      "written field, every level (%.1f ms, not in the apply's times)", same, job->pages.size(), job->checkMs);
		}
		if(!job->buildStatus.ok())
			why = "the pages' build failed: " + job->buildStatus.str();
		else
			region = applyFieldRegion(job->rects, pages, why, line);
		if(region)
			how = line;
		else
			RNote("LOUD: live terrain: the region step failed (%s): the whole field is applied (D118: never a half-updated field)",
			      why.c_str());
	}
	if(!region)
	{
		m_res->keepTableOnNextField();   // a live edit: no feed read (the bundle's read order is that of a load)
		const int64_t a0 = Qpc();
		applyField(m_fieldTilePlots);
		char b[96];
		_snprintf_s(b, sizeof(b), _TRUNCATE, "whole field %.1f ms", QpcToMs(Qpc() - a0));
		how = b;
	}
	m_dirty = true;
	const double step2 = QpcToMs(Qpc() - s0) - job->checkMs;
	const double ms = job->step1Ms + step2;   // the render thread's time for the apply
	// ticket 0587: the EXE write and the windows are step 2's now ("written in step 2" marks the line's new form)
	RNote("live terrain: apply of %zu change(s) (load seq %u): render thread %.1f ms = step 1 %.1f + step 2 %.1f (written in step 2: "
	      "EXE write %.1f, windows %.1f; %s); pages %zu built %s in %.1f ms; %u poll(s) between, %.1f ms from step 1 to the end",
	      job->live.size(), job->loadSeq, ms, job->step1Ms, step2, job->exeMs, job->windowMs, how.c_str(), job->pages.size(),
	      m_liveAsync && m_liveRegion ? "on the worker" : "on the render thread", job->buildMs, job->polls,
	      QpcToMs(Qpc() - job->q0) - job->checkMs);
	for(size_t k = 0; k < job->live.size(); ++k)
		if(job->ok[k])
		{
			m_live->noteApplied(job->live[k], job->stats[k], ms, job->sha[k]);
			// ticket 0586: the field now shows this table; 4914: with the change's written rectangles (its part of job->rects)
			IceWavesDrawn(this, job->live[k].sent, job->live[k].out.window);
		}
}

// Ticket 4909: the region step (srv_render_scene.h). The parts of applyField a live edit of the same field needs, each by
// its module's region call; the pages from the worker (built) when given.
bool Scene::applyFieldRegion(const std::vector<c5r::terrain::SampleRect>& rects, const std::vector<c5r::bake::HeightPage>* built,
                             std::string& why, std::string& line)
{
	if(!m_rInit || !m_haveField || !m_res)
	{
		why = !m_rInit ? "the renderer is not initialised" : !m_haveField ? "no field drawn" : "no residency";
		return false;
	}
	const int64_t q0 = Qpc();
	c5r::terrain::RegionUpdateStats ts;
	const c5r::Status st = m_r->updateHeightRegion(m_grid, rects, built, &ts);
	if(!st.ok())
	{
		why = "terrain: " + st.str();
		if(!m_r->tileGrid().tilesX)
			m_haveField = false;   // the renderer dropped its field (a part failed after pages were replaced)
		return false;
	}
	const int64_t q1 = Qpc();
	c5r::bake::RegionFieldStats rs;
	if(!m_res->onFieldRegion(m_dev, m_grid, rects, &rs, why))
	{
		why = "residency: " + why;
		return false;
	}
	const int64_t q2 = Qpc();
	ApplyStylesFromResidency(*m_r, *m_res);   // issue 4305: as applyField
	m_res->takeStylesChanged();
	const int64_t q3 = Qpc();
	c5r::terrain::RegionUpdateStats ws;
	m_water.onFieldRegion(m_grid, rects, built, &ws);
	const int64_t q4 = Qpc();
	if(m_forest)
		m_forest->onField(m_grid, m_fieldLoadSeq);   // epic 0431, ticket 4554 (4904: only the blocks the windows reach)
	const int64_t q5 = Qpc();
	if(m_farm)
	{   // as applyField (the same values: a no-op for the same load)
		const float wr[4] = {0.0f, 0.0f, (float)m_grid.samplesX - 1.0f, (float)m_grid.samplesY - 1.0f};
		m_farm->setGameSeed(m_grid.mapSeed);
		m_farm->setCutFrame(m_grid.ox, m_grid.oy);
		m_farm->setWorld(wr, m_fieldLoadSeq);
	}
	m_dirty = true;
	char b[400];
	_snprintf_s(b, sizeof(b), _TRUNCATE,
	            "region %zu rect(s): terrain %.1f ms (%u pages, %u from the worker, build %.1f, upload %.1f, tiles %.1f: bounds %u, "
	            "material %u, land %u, EXE list %u), residency %.1f ms (%u tiles compared, %u changed), styles %.1f ms, water %.1f ms "
	            "(%u pages), trees %.1f ms, farm %.1f ms",
	            rects.size(), QpcToMs(q1 - q0), ts.pagesRebuilt, ts.pagesFromWorker, ts.buildMs, ts.uploadMs, ts.tilesMs, ts.tilesBounds,
	            ts.tilesMat, ts.tilesLand, ts.exeTiles, QpcToMs(q2 - q1), rs.tilesScanned, rs.tilesChanged, QpcToMs(q3 - q2),
	            QpcToMs(q4 - q3), ws.pagesRebuilt, QpcToMs(q5 - q4), QpcToMs(Qpc() - q5));
	line = b;
	return true;
}

// ---- the offline player ----------------------------------------------------------------------------------------------
c5r::Status Scene::liveTerrainReplayInit(const std::string& journalText)
{
	m_liveTried = true;   // the player never runs the live hook
	liveterrain::Journal j;
	std::string why;
	if(!liveterrain::DecodeSpanText(journalText, j, m_liveReplayFrames, why))
		return c5r::Status::fail(c5r::Err::BadLayout, "render_live_terrain_journal: " + why);
	if(!m_haveGrid)
		return c5r::Status::fail(c5r::Err::NotFound, "live terrain replay: no field loaded yet");
	if(m_opt.install.empty())
		return c5r::Status::fail(c5r::Err::NotFound, "live terrain replay: no game install (the piece library)");
	std::unique_ptr<liveterrain::Replay> r(new liveterrain::Replay());
	// ticket 4914 (1): the same land/water step as the live server, used only when the journal says its producer had it
	C5R_TRY(r->init(liveterrain::MakeC5rProducerFactory(liveterrain::LibraryPathFor(m_opt.install),
	                                                    liveterrain::MakeInstallLandWaterEnabler(m_opt.install, LiveLog)),
	                j, m_grid));
	RNote("live terrain replay: load seq %u, %zu jobs, %zu generations, %zu frame entries; land/water step %s; river edges: %s",
	      j.loadSeq, j.jobs.size(), j.generations.size(), m_liveReplayFrames.size(),
	      j.landWater ? "on (the journal's mark)" : "off (no mark: as recorded)", r->riverEdgeRule().c_str());
	m_liveReplay = std::move(r);
	return c5r::Status::Ok();
}

c5r::Status Scene::liveTerrainReplayTo(int64_t frameId, uint32_t* generation)
{
	if(!m_liveReplay)
		return c5r::Status::fail(c5r::Err::NotFound, "live terrain replay: not initialised");
	const uint32_t want = liveterrain::Replay::GenerationAt(m_liveReplayFrames, frameId);
	bool changed = false;
	const int64_t q0 = Qpc();
	if(want > m_liveReplay->generation())
	{
		std::vector<liveterrain::ApplyStats> written;
		std::vector<liveterrain::Rect> wrects;
		C5R_TRY(m_liveReplay->advanceTo(want, m_grid, changed, &written, &wrects));
		if(changed)
		{
			std::vector<liveterrain::SampleBox> boxes;   // 4904, as the live path: one box per rectangle (MC #9538 item 3)
			for(const liveterrain::Rect& wr : wrects)
				boxes.push_back(liveterrain::SampleBoxOfRect(wr.x0, wr.y0, wr.w, wr.h));
			liveterrain::DerivedAfterWindow(m_forest.get(), boxes);
			// Ticket 0587: the region step as the live scene's (its pages built here, on the player's one thread), the whole
			// field with C5_RENDER_LIVE_REGION=0 or when the region step refuses (as live: LOUD, D118)
			if(!m_liveRegionTried)
			{
				m_liveRegionTried = true;
				m_liveRegion = EnvOn("C5_RENDER_LIVE_REGION");
				m_liveAsync = false;
				RNote("live terrain replay: apply %s (C5_RENDER_LIVE_REGION; ticket 0587)",
				      m_liveRegion ? "by region (the pages and tiles the windows reach), as the live server" : "whole field (C5_RENDER_LIVE_REGION=0)");
			}
			bool region = false;
			std::string how;
			if(m_liveRegion)
			{
				std::vector<c5r::terrain::SampleRect> rects;
				for(const liveterrain::Rect& wr : wrects)
					rects.push_back(SampleRectOf(wr));
				std::string why;
				region = applyFieldRegion(rects, nullptr, why, how);
				if(!region)
					RNote("LOUD: live terrain replay: the region step failed (%s): the whole field is applied", why.c_str());
			}
			if(!region)
			{
				if(m_res)
					m_res->keepTableOnNextField();
				applyField(m_fieldTilePlots);
				how = "whole field";
			}
			m_dirty = true;
			RNote("live terrain replay: frame %lld: field generation %u (hash checked) in %.1f ms (%s)", (long long)frameId,
			      m_liveReplay->generation(), QpcToMs(Qpc() - q0), how.c_str());
		}
	}
	if(generation)
		*generation = m_liveReplay->generation();
	return c5r::Status::Ok();
}

}  // namespace srvrender
