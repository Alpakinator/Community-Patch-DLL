// test_icewaves.cpp - tests of the server's side of ticket 0586 (server/src/render/srv_liveterrain_icewaves.cpp): the
// decisions (what the EXE should rebuild, the gate), the section, and a stand-in proxy that answers as
// proxy/src/proxy_livereload.cpp does (its own tests: proxy/tests/livereload/). CPU only.
// Written 2026-11-19 for this repository (GPL-3.0-or-later). Built by ../test_liveterrain_build.py --icewaves.
#include "srv_liveterrain_icewaves.h"
#include "../../../../../proxy/include/vp_livereload.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

using namespace srvrender::liveterrain;

namespace {
int g_checks = 0, g_failed = 0;
int g_caseFailed = 0;

void Check(const char* what, bool ok, const std::string& detail = std::string())
{
	++g_checks;
	if(!ok)
	{
		++g_failed;
		++g_caseFailed;
		printf("  FAIL %s %s\n", what, detail.c_str());
	}
}

const uint32_t W = 6, H = 4;

SrvFeed::PlotRecord Plot(int terrain, int plotType, int feature)
{
	SrvFeed::PlotRecord r = {};
	r.terrain = (int8_t)terrain;
	r.plotType = (int8_t)plotType;
	r.feature = (int8_t)feature;
	return r;
}

// A small map: row 0 ocean with ice at (0,0) and (1,0); row 1 coast; rows 2-3 grassland (terrain 0, flat land 2).
std::vector<SrvFeed::PlotRecord> Base()
{
	std::vector<SrvFeed::PlotRecord> t(W * H);
	for(uint32_t y = 0; y < H; ++y)
		for(uint32_t x = 0; x < W; ++x)
		{
			SrvFeed::PlotRecord& r = t[y * W + x];
			if(y == 0)
				r = Plot(kTerrainOcean, 3, x < 2 ? kFeatureIce : -1);
			else if(y == 1)
				r = Plot(kTerrainCoast, 3, -1);
			else
				r = Plot(0, 2, -1);
		}
	return t;
}

// ---- the stand-in proxy: answers the section's request with the given results, flipping the grid it holds ----
struct FakeProxy
{
	HANDLE map = nullptr;
	vplr::Section* s = nullptr;
	std::vector<uint8_t> grid;   // per plot
	int iceResult = vplr::kOk, wavesResult = vplr::kOk;
	bool writeBits = true;
	int served = 0;
	bool open(uint32_t pid)
	{
		wchar_t name[64];
		_snwprintf_s(name, 64, _TRUNCATE, VPLR_SECTION_FMT, (unsigned)pid);
		map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
		if(!map)
			return false;
		s = (vplr::Section*)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(vplr::Section));
		return s && s->magic == vplr::kMagic;
	}
	~FakeProxy()
	{
		if(s)
			UnmapViewOfFile(s);
		if(map)
			CloseHandle(map);
	}
	bool serve()
	{
		if(!s || s->reqSeq == s->ackSeq)
			return false;
		++served;
		uint32_t set = 0, cleared = 0, flips = 0;
		const bool ice = (s->reqFlags & vplr::kReqIce) != 0;
		if(ice)
			for(uint32_t i = 0; i < s->mapW * s->mapH; ++i)
				flips += vplr::BitGet(s->ice, i) != (grid[i] != 0) ? 1u : 0u;
		s->iceResult = ice ? (flips == s->expectFlips ? iceResult : (int)vplr::kGridMismatch) : (int)vplr::kNotAsked;
		if(ice && writeBits && flips == s->expectFlips && iceResult != vplr::kRefusedExe)
			for(uint32_t i = 0; i < s->mapW * s->mapH; ++i)
			{
				const uint8_t want = vplr::BitGet(s->ice, i) ? 1 : 0;
				if(want != grid[i])
				{
					(want ? set : cleared)++;
					grid[i] = want;
				}
			}
		s->wavesResult = (s->reqFlags & vplr::kReqWaves) ? wavesResult : (int)vplr::kNotAsked;
		s->bitsSet = set;
		s->bitsCleared = cleared;
		strcpy_s(s->why, sizeof(s->why), s->iceResult < 0 || s->wavesResult < 0 ? "stand-in refusal" : "");
		MemoryBarrier();
		s->ackSeq = s->reqSeq;
		return true;
	}
};

// One round: want -> post -> the stand-in serves -> poll -> answered. Returns what was asked ("" none, "ice", "waves",
// "ice+waves").
std::string Round(IceWaves& iw, LiveReloadLink& link, FakeProxy& p, const std::vector<SrvFeed::PlotRecord>& cur, std::string* note = nullptr,
                  bool* loud = nullptr)
{
	IceWavesRequest req;
	std::string n;
	const bool w = iw.want(cur, req, n);
	if(note)
		*note = n;
	if(!w)
		return "";
	std::string why;
	if(!link.post(req, iw.loadSeq(), why))
		return "POST FAILED: " + why;
	iw.posted(req);
	p.serve();
	IceWavesAnswer a;
	if(!link.poll(a))
		return "NO ANSWER";
	bool l = false;
	iw.answered(a, l);
	if(loud)
		*loud = l;
	return std::string(req.ice ? "ice" : "") + (req.ice && req.waves ? "+" : "") + (req.waves ? "waves" : "");
}

uint32_t g_pid = 0;

void CaseDecisions()
{
	IceWaves iw;
	LiveReloadLink link;
	std::string why;
	Check("link opens", link.open(g_pid, why), why);
	FakeProxy p;
	Check("stand-in proxy opens the section", p.open(g_pid));
	std::vector<SrvFeed::PlotRecord> t = Base();
	iw.onLoad(7, W, H, t);
	p.grid.assign(W * H, 0);
	p.grid[0] = p.grid[1] = 1;
	Check("load: have", iw.haveLoad() && iw.loadSeq() == 7);
	Check("no change: nothing asked", Round(iw, link, p, t) == "");

	// ice added on a water plot (an ice feature is not a field change): live at once
	t[2].feature = kFeatureIce;
	Check("ice added: ice asked", Round(iw, link, p, t) == "ice");
	Check("ice added: the EXE holds it", iw.exeIce()[2] == 1 && p.grid[2] == 1);
	Check("ice added: nothing more", Round(iw, link, p, t) == "");
	// ice removed
	t[0].feature = -1;
	Check("ice removed: ice asked", Round(iw, link, p, t) == "ice");
	Check("ice removed: the EXE's grid", iw.exeIce()[0] == 0 && p.grid[0] == 0);

	// land made water at (3,2) (plot type 2 -> 3, grass -> coast): a field change that waits: the waves wait with it
	t[2 * W + 3] = Plot(kTerrainCoast, 3, -1);
	std::string note;
	Check("land to water, field waits: nothing asked", Round(iw, link, p, t, &note) == "");
	Check("land to water: the waves' wait is noted once", note.find("coast waves wait for the reload") != std::string::npos, note);
	Check("land to water: noted only once", Round(iw, link, p, t, &note) == "" && note.empty(), note);
	// the ice of a plot whose field waits: (1,0) loses its ice and becomes land (the DLL's erase): held
	t[1] = Plot(0, 2, -1);
	Check("ice plot made land, field waits: nothing asked", Round(iw, link, p, t, &note) == "");
	Check("ice plot made land: the EXE keeps its ice", iw.exeIce()[1] == 1 && p.grid[1] == 1);
	Check("ice plot made land: noted", note.find("wait with their field change") != std::string::npos, note);

	// the field goes live for the table (option B, or a reload of the same table): ice and waves follow
	iw.onDrawn(t);
	Check("field shows the table: ice + waves asked", Round(iw, link, p, t) == "ice+waves");
	Check("field shows the table: ice cleared at (1,0)", iw.exeIce()[1] == 0 && p.grid[1] == 0);
	Check("field shows the table: the waves' classes taken", iw.exeWaves()[2 * W + 3] == 1 && iw.exeWaves()[1] == 0);
	Check("then nothing", Round(iw, link, p, t) == "");

	// a coast plot made ocean (a water plot's terrain: a field change, waits) and then drawn: waves only
	t[1 * W + 5].terrain = kTerrainOcean;
	Check("coast to ocean, waits: nothing", Round(iw, link, p, t) == "");
	iw.onDrawn(t);
	Check("coast to ocean drawn: waves only", Round(iw, link, p, t) == "waves");

	// a land change away from water (hills) changes neither: nothing asked even while it waits
	t[3 * W + 0].plotType = 1;
	Check("hills on land: nothing", Round(iw, link, p, t) == "");
	const IceWavesCounters& c = iw.counters();
	Check("counters", c.requests == 4 && c.iceOk == 3 && c.wavesOk == 2 && c.failures == 0,
	      std::to_string(c.requests) + " " + std::to_string(c.iceOk) + " " + std::to_string(c.wavesOk));
}

void CaseFailures()
{
	IceWaves iw;
	LiveReloadLink link;
	std::string why;
	Check("link opens", link.open(g_pid, why), why);
	FakeProxy p;
	Check("stand-in opens", p.open(g_pid));
	std::vector<SrvFeed::PlotRecord> t = Base();
	iw.onLoad(8, W, H, t);
	p.grid.assign(W * H, 0);
	p.grid[0] = p.grid[1] = 1;

	// the proxy refuses the EXE: LOUD, nothing written, ice not asked again in this load
	p.iceResult = vplr::kRefusedExe;
	t[3].feature = kFeatureIce;
	bool loud = false;
	Check("refused: asked", Round(iw, link, p, t, nullptr, &loud) == "ice");
	Check("refused: loud", loud);
	Check("refused: the EXE's grid unchanged", iw.exeIce()[3] == 0 && p.grid[3] == 0);
	t[4].feature = kFeatureIce;
	Check("refused: ice not asked again", Round(iw, link, p, t) == "");
	// waves still go
	t[1 * W + 0].terrain = kTerrainOcean;
	iw.onDrawn(t);
	Check("waves still asked after an ice failure", Round(iw, link, p, t) == "waves");
	Check("failures counted", iw.counters().failures == 1);

	// a new load: everything again from its table
	p.iceResult = vplr::kOk;
	iw.onLoad(9, W, H, t);
	for(uint32_t i = 0; i < W * H; ++i)
		p.grid[i] = IcePlot(t[i]) ? 1 : 0;
	t[5].feature = kFeatureIce;
	Check("new load: ice asked again", Round(iw, link, p, t) == "ice");

	// a grid that is not the one the server thinks (the stand-in counts other flips): kGridMismatch, the EXE's state kept
	p.grid[10] = 1;   // a bit the server does not know of
	t[5].feature = -1;
	Check("mismatch: asked", Round(iw, link, p, t, nullptr, &loud) == "ice");
	Check("mismatch: loud and the server's grid unchanged", loud && iw.exeIce()[5] == 1);

	// Lua error after the bits were written (all flips counted): the server takes the grid as written
	IceWaves iw2;
	iw2.onLoad(10, W, H, Base());
	std::vector<SrvFeed::PlotRecord> t2 = Base();
	p.grid.assign(W * H, 0);
	p.grid[0] = p.grid[1] = 1;
	p.iceResult = vplr::kLuaError;
	t2[2].feature = kFeatureIce;
	Check("Lua error: asked", Round(iw2, link, p, t2, nullptr, &loud) == "ice");
	Check("Lua error: loud, the grid as written", loud && iw2.exeIce()[2] == 1);
}

// Ticket 4914 (epic 0615): land/water windows going live. The waves follow once the field shows the plots; an ice plot
// made land loses its ice when its feature goes (the DLL's erase or SetFeatureType), and keeps it while FEATURE_ICE
// stays on the plot (CvPlot::setPlotType does not clear features; the EXE's grid takes every feature-0 plot at a load, so
// a reload shows the same); a field window written beside the EXE's ice asks Ice_Reload once (live-terrain.md 10.7).
void CaseLandWater()
{
	IceWaves iw;
	LiveReloadLink link;
	std::string why;
	Check("link opens", link.open(g_pid, why), why);
	FakeProxy p;
	Check("stand-in opens", p.open(g_pid));
	std::vector<SrvFeed::PlotRecord> t = Base();
	iw.onLoad(11, W, H, t);
	p.grid.assign(W * H, 0);
	p.grid[0] = p.grid[1] = 1;
	typedef IceWaves::SampleBox Box;
	auto plotBox = [](uint32_t x, uint32_t y) { Box b; b.x0 = 64 * x; b.y0 = 64 * (y + 2); b.w = 64; b.h = 64; return b; };

	// land made water (grass -> coast at (4,3)) and drawn live with its window, far from the ice: waves only, no Ice_Reload
	t[3 * W + 4] = Plot(kTerrainCoast, 3, -1);
	Check("land to water, waits: nothing", Round(iw, link, p, t) == "");
	iw.onDrawn(t, std::vector<Box>(1, plotBox(4, 3)));
	Check("land to water drawn away from ice: no Ice_Reload owed", !iw.iceRefreshPending());
	Check("land to water drawn: waves asked", Round(iw, link, p, t) == "waves");
	Check("land to water drawn: the class taken", iw.exeWaves()[3 * W + 4] == 1);

	// an ice plot made land by setPlotType only (FEATURE_ICE stays on the plot): drawn -> its ice stays (as a reload),
	// its waves follow (ocean -> none); the window lies on the ice, so the EXE rebuilds its ice over the new field
	t[1] = Plot(0, 2, kFeatureIce);
	Check("ice plot made land (feature kept), waits: nothing", Round(iw, link, p, t) == "");
	iw.onDrawn(t, std::vector<Box>(1, plotBox(1, 0)));
	Check("ice plot made land (feature kept) drawn: an Ice_Reload owed", iw.iceRefreshPending());
	Check("ice plot made land (feature kept) drawn: ice (no flip) + waves", Round(iw, link, p, t) == "ice+waves");
	Check("feature kept: the EXE keeps the plot's ice", iw.exeIce()[1] == 1 && p.grid[1] == 1);
	Check("feature kept: the Ice_Reload covered the window", !iw.iceRefreshPending());
	Check("then nothing", Round(iw, link, p, t) == "");

	// the feature goes (the mod's SetFeatureType or the DLL's erase): ice is not a field change, so it follows at once
	t[1].feature = -1;
	Check("feature removed on the new land: ice asked", Round(iw, link, p, t) == "ice");
	Check("feature removed: ice cleared", iw.exeIce()[1] == 0 && p.grid[1] == 0);

	// an ice plot made land with its feature removed in the same change: held until drawn, then ice + waves together
	t[0] = Plot(0, 2, -1);
	Check("ice plot made land (feature removed), waits: nothing", Round(iw, link, p, t) == "");
	Check("held: the EXE keeps its ice", iw.exeIce()[0] == 1 && p.grid[0] == 1);
	iw.onDrawn(t, std::vector<Box>(1, plotBox(0, 0)));
	Check("drawn: ice + waves", Round(iw, link, p, t) == "ice+waves");
	Check("drawn: ice cleared, waves taken", iw.exeIce()[0] == 0 && p.grid[0] == 0 && iw.exeWaves()[0] == 0);
	const int64_t req0 = iw.counters().requests;

	// a land window (no table change) beside ice: Ice_Reload with no flip; far from ice: nothing
	IceWaves iw2;
	std::vector<SrvFeed::PlotRecord> b = Base();
	iw2.onLoad(12, W, H, b);
	p.grid.assign(W * H, 0);
	p.grid[0] = p.grid[1] = 1;
	iw2.onDrawn(b, std::vector<Box>(1, plotBox(5, 3)));
	Check("window far from the ice: nothing owed", !iw2.iceRefreshPending() && Round(iw2, link, p, b) == "");
	iw2.onDrawn(b, std::vector<Box>(1, plotBox(3, 1)));   // one ring from (1,0)... widened by the half-plot shift
	Check("window beside the ice: owed", iw2.iceRefreshPending() && iw2.counters().iceRefreshes == 1);
	IceWavesRequest req;
	std::string note;
	Check("window beside the ice: an ice request with no flip", iw2.want(b, req, note) && req.ice && !req.waves &&
	                                                            req.expectFlips == 0 && req.iceCount == 2);
	// a second window arrives while that request is in flight: it is owed again after the answer
	Check("post", link.post(req, iw2.loadSeq(), why), why);
	iw2.posted(req);
	iw2.onDrawn(b, std::vector<Box>(1, plotBox(0, 1)));
	p.serve();
	IceWavesAnswer a;
	bool loud = true;
	Check("answered", link.poll(a));
	iw2.answered(a, loud);
	Check("answer ok, nothing flipped", !loud && a.bitsSet == 0 && a.bitsCleared == 0 && p.grid[0] == 1 && p.grid[1] == 1);
	Check("the window written during the request is still owed", iw2.iceRefreshPending());
	Check("so a second Ice_Reload", Round(iw2, link, p, b) == "ice" && !iw2.iceRefreshPending());
	Check("then nothing", Round(iw2, link, p, b) == "");
	// a window across the x seam: plots W-1 and 0 reach the ice at (0,0) and (1,0)
	Box seam;
	seam.x0 = 64 * W - 32;
	seam.y0 = 64 * 3;
	seam.w = 32;
	seam.h = 16;
	iw2.onDrawn(b, std::vector<Box>(1, seam));
	Check("a window at the seam wraps to the ice", iw2.iceRefreshPending());
	// a new load clears what was owed (the load's ice is built from the load's field)
	iw2.onLoad(13, W, H, b);
	Check("a new load: nothing owed", !iw2.iceRefreshPending() && Round(iw2, link, p, b) == "");
	// a failed ice kind is not asked again in the load, owed or not
	p.iceResult = vplr::kLuaError;
	iw2.onDrawn(b, std::vector<Box>(1, plotBox(1, 1)));
	Check("failure: asked once", Round(iw2, link, p, b, nullptr, &loud) == "ice" && loud);
	iw2.onDrawn(b, std::vector<Box>(1, plotBox(1, 1)));
	Check("failure: not asked again", Round(iw2, link, p, b) == "");
	p.iceResult = vplr::kOk;
	// an empty or mis-sized table changes nothing
	const int64_t refreshes = iw2.counters().iceRefreshes;
	iw2.onDrawn(std::vector<SrvFeed::PlotRecord>(3), std::vector<Box>(1, plotBox(1, 1)));
	Check("a table of another size: ignored", iw2.counters().iceRefreshes == refreshes && refreshes == 5,
	      std::to_string(refreshes));
	Check("counters of the first map", req0 == 4, std::to_string(req0));
}

void CaseLink()
{
	LiveReloadLink a;
	std::string why;
	Check("open", a.open(g_pid, why), why);
	IceWavesRequest r;
	r.ice = true;
	r.W = W;
	r.H = H;
	r.grid.assign(W * H, 0);
	r.grid[7] = 1;
	r.iceCount = 1;
	r.expectFlips = 1;
	Check("post", a.post(r, 1, why), why);
	Check("a second post before the answer is refused", !a.post(r, 1, why) && why.find("unanswered") != std::string::npos, why);
	FakeProxy p;
	Check("stand-in opens", p.open(g_pid));
	Check("the request's bitmap", vplr::BitGet(p.s->ice, 7) && !vplr::BitGet(p.s->ice, 6) && p.s->iceCount == 1 &&
	                                  p.s->expectFlips == 1 && p.s->reqFlags == vplr::kReqIce && p.s->mapW == W && p.s->mapH == H);
	// a second server of the same EXE (a restart) opens the section again: the sequence goes on, the unanswered request is
	// dropped (answered to nobody)
	LiveReloadLink b;
	Check("a second open of the same section", b.open(g_pid, why), why);
	Check("the sequence goes on", p.s->reqSeq == p.s->ackSeq && p.s->reqSeq >= 1);
	Check("the restarted server posts", b.post(r, 2, why), why);
	p.grid.assign(W * H, 0);
	Check("served", p.serve());
	IceWavesAnswer ans;
	Check("answered", b.poll(ans) && ans.iceResult == vplr::kOk && ans.bitsSet == 1);
	Check("an answer is taken once", !b.poll(ans));
	// sizes the section cannot hold
	IceWavesRequest big = r;
	big.W = 256;
	big.grid.assign(256 * H, 0);
	Check("a map wider than 255 is refused", !b.post(big, 3, why) && why.find("does not fit") != std::string::npos, why);
	IceWavesRequest bad = r;
	bad.grid.resize(3);
	Check("a grid of another size is refused", !b.post(bad, 3, why));
	// another version in the section
	p.s->version = 99;
	LiveReloadLink c;
	Check("a section of another version is refused", !c.open(g_pid, why) && why.find("version 99") != std::string::npos, why);
	p.s->version = vplr::kVersion;
}

// ---- --replay FILE: the decisions on recorded tables (icewaves_replay.py writes FILE from a scene bundle's feed.bin) ----
// FILE: "W H N", then N lines of W * H plots "terrain plotType feature flags". Table 0 is the load (what the EXE built its
// ice and waves from, and what the field shows: the live field is never advanced here, as on a route where every field
// change of these tables waits or is not a field change); each later table is the DLL's, and every request is answered
// as done (the proxy's ok).
int Replay(const char* path)
{
	FILE* f = nullptr;
	if(fopen_s(&f, path, "r") || !f)
	{
		printf("replay: cannot open %s\n", path);
		return 2;
	}
	unsigned w = 0, h = 0, n = 0;
	if(fscanf_s(f, "%u %u %u", &w, &h, &n) != 3 || !w || !h || w > 255 || h > 254 || !n)
	{
		printf("replay: bad header\n");
		fclose(f);
		return 2;
	}
	std::vector<std::vector<SrvFeed::PlotRecord>> tables(n, std::vector<SrvFeed::PlotRecord>(w * h));
	for(unsigned k = 0; k < n; ++k)
		for(unsigned i = 0; i < w * h; ++i)
		{
			int t = 0, pt = 0, fe = 0, fl = 0;
			if(fscanf_s(f, "%d %d %d %d", &t, &pt, &fe, &fl) != 4)
			{
				printf("replay: table %u plot %u unreadable\n", k, i);
				fclose(f);
				return 2;
			}
			SrvFeed::PlotRecord& r = tables[k][i];
			r.terrain = (int8_t)t;
			r.plotType = (int8_t)pt;
			r.feature = (int8_t)fe;
			r.flags = (uint8_t)fl;
		}
	fclose(f);
	IceWaves iw;
	iw.onLoad(1, w, h, tables[0]);
	uint32_t ice0 = 0;
	for(const auto& r : tables[0])
		ice0 += IcePlot(r) ? 1u : 0u;
	printf("{\"table\": 0, \"plots\": %u, \"ice_plots\": %u}\n", w * h, ice0);
	for(unsigned k = 1; k < n; ++k)
	{
		IceWavesRequest req;
		std::string note;
		const bool want = iw.want(tables[k], req, note);
		std::string flips;
		if(want && req.ice)
			for(size_t i = 0; i < req.grid.size(); ++i)
				if(req.grid[i] != iw.exeIce()[i])
					flips += std::string(flips.empty() ? "" : ", ") + "[" + std::to_string(i % w) + ", " + std::to_string(i / w) + ", " +
					         std::to_string((int)req.grid[i]) + "]";
		printf("{\"table\": %u, \"ask_ice\": %s, \"ask_waves\": %s, \"ice_plots\": %u, \"flips\": [%s], \"wave_plots\": %u, \"note\": \"%s\"}\n",
		       k, want && req.ice ? "true" : "false", want && req.waves ? "true" : "false", want ? req.iceCount : 0u, flips.c_str(),
		       want ? req.wavePlots : 0u, note.c_str());
		if(want)
		{
			iw.posted(req);
			IceWavesAnswer a;
			a.iceResult = req.ice ? 0 : 1;
			a.wavesResult = req.waves ? 0 : 1;
			a.bitsSet = 0;
			a.bitsCleared = req.ice ? req.expectFlips : 0;   // the count is what the server checks (set + cleared)
			bool loud = false;
			iw.answered(a, loud);
		}
	}
	return 0;
}

// MC #9538 item 4: with the land/water step off (setFieldRefresh(false), the scene's C5_RENDER_LIVE_LANDWATER=0) a window
// written beside the EXE's ice owes no Ice_Reload, as before epic 0615; the table the field shows is still taken (a
// land/water change drawn some other way still asks its waves), and turning it on again restores 4914's refresh.
void CaseFieldRefreshOff()
{
	IceWaves iw;
	LiveReloadLink link;
	std::string why;
	Check("link opens", link.open(g_pid, why), why);
	FakeProxy p;
	Check("stand-in opens", p.open(g_pid));
	std::vector<SrvFeed::PlotRecord> b = Base();
	Check("on by default", iw.fieldRefresh());
	iw.setFieldRefresh(false);
	iw.onLoad(21, W, H, b);
	p.grid.assign(W * H, 0);
	p.grid[0] = p.grid[1] = 1;
	typedef IceWaves::SampleBox Box;
	auto plotBox = [](uint32_t x, uint32_t y) { Box bx; bx.x0 = 64 * x; bx.y0 = 64 * (y + 2); bx.w = 64; bx.h = 64; return bx; };
	iw.onDrawn(b, std::vector<Box>(1, plotBox(1, 0)));   // on the ice
	iw.onDrawn(b, std::vector<Box>(1, plotBox(3, 1)));   // beside it
	Check("off: a window on or beside the ice owes nothing", !iw.iceRefreshPending() && iw.counters().iceRefreshes == 0);
	Check("off: no request", Round(iw, link, p, b) == "" && iw.counters().requests == 0);
	Check("off: kept across a load", (iw.onLoad(22, W, H, b), !iw.fieldRefresh()));
	// the table is still taken: a plot whose wave class changed, drawn, asks its waves
	std::vector<SrvFeed::PlotRecord> t = b;
	t[3 * W + 4] = Plot(kTerrainCoast, 3, -1);
	Check("off: held while not drawn", Round(iw, link, p, t) == "");
	iw.onDrawn(t, std::vector<Box>(1, plotBox(4, 3)));
	Check("off: drawn: waves asked", Round(iw, link, p, t) == "waves");
	// on again: 4914's refresh
	iw.setFieldRefresh(true);
	iw.onDrawn(t, std::vector<Box>(1, plotBox(3, 1)));
	Check("on: a window beside the ice owes an Ice_Reload", iw.iceRefreshPending() && Round(iw, link, p, t) == "ice");
}

struct Case
{
	const char* name;
	void (*fn)();
};
const Case kCases[] = {{"decisions", CaseDecisions}, {"failures", CaseFailures}, {"landwater", CaseLandWater}, {"link", CaseLink},
                       {"fieldrefresh_off", CaseFieldRefreshOff}};
}  // namespace

int main(int argc, char** argv)
{
	// a pid of its own per run, so the cases' sections never meet another process's (the section name carries it)
	g_pid = 0x70000000u | (GetCurrentProcessId() & 0xFFFFFu);
	if(argc > 2 && strcmp(argv[1], "--replay") == 0)
		return Replay(argv[2]);
	int ran = 0;
	for(const Case& c : kCases)
	{
		if(argc > 1 && strcmp(argv[1], c.name) != 0)
			continue;
		++ran;
		g_caseFailed = 0;
		c.fn();
		printf("CASE %s %s\n", c.name, g_caseFailed ? "FAIL" : "PASS");
	}
	if(!ran)
	{
		printf("no case matches '%s'\n", argc > 1 ? argv[1] : "");
		return 2;
	}
	printf("test_icewaves: %d checks, %d failed\n", g_checks, g_failed);
	return g_failed ? 1 : 0;
}
