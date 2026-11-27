// test_derived.cpp - offline proof of ticket 4904 (epic 0465): the server-drawn trees take the new ground height after a
// live window, and only the window's trees are built again (server/src/render/srv_liveterrain_derived.*). Written
// 2026-11-19 by w7-4904 for this repository (GPL-3.0-or-later). Findings: research/05-graphics-composition/live-terrain.md
// 8.12.
//
// Inputs (read-only): the install (C5R_GAME_DIR: forests.xml and the masks of the placement), 4903's recorded inputs and
// chained producer outputs (C5_4904_DATA, default C:/c5-64bit-work/keep/liveterrain-derived-4903, made again from 4902's
// recordings by make_inputs.sh beside this file after 4903's own folder was cleaned up: tiny/ and huge/ block.bin, plots.txt,
// h16.raw; fh/field.<k>.h16 and fh/r<k+1>.json; hugechain/ the same), and a Tiny tap of 4906's bundles for the frame check
// (C5_4904_TINY_TAP). Nothing is written except with --dump DIR (CSV of the trees around each window, for the pictures).
//
// Per recorded step (field before -> field after, the step's window rectangle):
//   * every forest / jungle plot of the plot table gets its record; every record is built on the field before (the worst
//     case: the whole map seen);
//   * the window path: ReheightBuilt with the window's box; the reference: a copy of the placement with every block built
//     again on the field after (what the forest's whole-map path gives);
//   * checks: every plot's instances bit-equal to the reference; the plots not built again bit-equal to before; every
//     drawn tree's z equal to the field after at the sample the build reads (|dz| = 0); the trees that would stand on
//     stale ground without the re-height counted;
//   * cost: the window path and the whole-map path, 7 runs each, median (single machine, an indication, COMMON 23).
// Cases without recorded data: the empty box list, a box far from every tree, a box over one plot, the x wrap, a missing
// record (failed, the fallback signal), DerivedAfterWindow without a forest.
//
//   test_derived.exe [case ...] [--dump DIR]      cases: frame units tiny_fh huge_chain (default: all)
// Exit 0 all PASS, 1 a case failed, 2 bad arguments or inputs missing.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "srv_liveterrain_derived.h"
#include "c5r/forest/cut.h"
#include "c5r/forest/placement.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/heightfield/heightfield.h"

using namespace srvrender::liveterrain;
namespace c5f = c5r::forest;
namespace c5h = c5r::heightfield;

namespace {

int g_fail = 0;
std::string g_case;
std::string g_dump;

#define CHECK(c)                                                                                         \
	do                                                                                                   \
	{                                                                                                    \
		if(!(c))                                                                                         \
		{                                                                                                \
			std::printf("  CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #c);                          \
			++g_fail;                                                                                    \
		}                                                                                                \
	} while(0)

std::string env(const char* n, const char* def = "")
{
	char* v = nullptr;
	size_t len = 0;
	if(_dupenv_s(&v, &len, n) == 0 && v)
	{
		std::string s(v);
		free(v);
		if(!s.empty())
			return s;
	}
	return def;
}

std::wstring wide(const std::string& s)
{
	return std::wstring(s.begin(), s.end());
}

bool readFile(const std::string& p, std::vector<uint8_t>& out)
{
	std::ifstream f(p, std::ios::binary);
	if(!f)
		return false;
	out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	return true;
}

std::string dataDir()
{
	// the kept folder (ticket 4915, 2026-11-20): made by make_inputs.sh beside this file; README.txt there says how
	return env("C5_4904_DATA", "C:/c5-64bit-work/keep/liveterrain-derived-4903");
}

// The grid frame from the EXE's grid constants block (880 bytes from VA 0xD90FBC, as 4903's runner reads it): wrap
// +0x000, hexToWorld +0x074 (VA 0xD91030), waterZ +0x2B8 (VA 0xD91274), sx sy sz ox oy +0x35C..+0x36C. Checked against a
// tap header by case "frame".
bool frameFromBlock(const std::vector<uint8_t>& b, uint32_t mapW, uint32_t mapH, c5h::HeightGrid& g)
{
	if(b.size() < 0x370)
		return false;
	uint32_t wrap = 0;
	std::memcpy(&wrap, &b[0], 4);
	g.wrapX = wrap == 1;
	std::memcpy(g.hexToWorld, &b[0x74], 64);
	std::memcpy(&g.waterZ, &b[0x2B8], 4);
	std::memcpy(&g.sx, &b[0x35C], 4);
	std::memcpy(&g.sy, &b[0x360], 4);
	std::memcpy(&g.sz, &b[0x364], 4);
	std::memcpy(&g.ox, &b[0x368], 4);
	std::memcpy(&g.oy, &b[0x36C], 4);
	g.mapW = mapW;
	g.mapH = mapH;
	g.samplesX = mapW * 64;
	g.samplesY = mapH * 64;
	return true;
}

bool readH16(const std::string& p, const c5h::HeightGrid& g, std::vector<uint16_t>& h)
{
	std::vector<uint8_t> raw;
	if(!readFile(p, raw) || raw.size() != (size_t)g.samplesX * g.samplesY * 2)
		return false;
	h.resize((size_t)g.samplesX * g.samplesY);
	std::memcpy(h.data(), raw.data(), raw.size());
	return true;
}

struct Plot
{
	int x, y, hills, feature;
};

bool readPlots(const std::string& p, int& w, int& hgt, std::vector<Plot>& out)
{
	std::ifstream f(p);
	if(!f || !(f >> w >> hgt))
		return false;
	out.clear();
	for(int i = 0; i < w * hgt; ++i)
	{
		Plot q;
		int terrain, mountain, cart, river, lake;
		std::string ft;
		if(!(f >> q.x >> q.y >> terrain >> q.hills >> mountain >> q.feature >> ft >> cart >> river >> lake))
			return false;
		out.push_back(q);
	}
	return true;
}

// "window":[[x0,y0,w,h],...] of a 4903 result line
bool readWindow(const std::string& p, std::vector<SampleBox>& boxes)
{
	std::vector<uint8_t> raw;
	if(!readFile(p, raw))
		return false;
	const std::string s(raw.begin(), raw.end());
	const size_t k = s.find("\"window\":[");
	if(k == std::string::npos)
		return false;
	size_t i = k + 10;
	boxes.clear();
	while(i < s.size() && s[i] == '[')
	{
		int x0, y0, w, h;
		if(std::sscanf(s.c_str() + i, "[%d,%d,%d,%d]", &x0, &y0, &w, &h) != 4)
			return false;
		SampleBox b;
		b.x0 = x0;
		b.y0 = y0;
		b.x1 = x0 + w - 1;
		b.y1 = y0 + h - 1;
		boxes.push_back(b);
		i = s.find(']', i) + 1;
		if(i < s.size() && s[i] == ',')
			++i;
	}
	return !boxes.empty();
}

const int kForestId = 5, kJungleId = 1;   // FEATURE_FOREST / FEATURE_JUNGLE on these saves (trees.md 10.1, 4550)

c5r::gamefs::GameFiles& files()
{
	static c5r::gamefs::GameFiles* fs = nullptr;
	if(!fs)
	{
		fs = new c5r::gamefs::GameFiles();
		const std::string g = env("C5R_GAME_DIR");
		if(g.empty() || !fs->open(wide(g)).ok())
		{
			std::printf("C5R_GAME_DIR missing or not an install\n");
			std::exit(2);
		}
	}
	return *fs;
}

bool makePlacement(const c5h::HeightGrid& g, const std::vector<Plot>& plots, c5f::Placement& pl, std::vector<BuiltPlot>& built)
{
	const c5r::Status st = pl.init(files(), g.hexToWorld, 1234);
	if(!st.ok())
	{
		std::printf("  placement init: %s\n", st.str().c_str());
		return false;
	}
	for(const Plot& q : plots)
	{
		c5f::PlotInput in;
		in.x = q.x;
		in.y = q.y;
		in.forest = q.feature == kForestId;
		in.jungle = q.feature == kJungleId;
		in.hills = q.hills != 0;
		if(!in.forest && !in.jungle)
			continue;
		if(!pl.setPlot(in).ok())
			return false;
	}
	c5f::EmptyCut cut;
	built.clear();
	for(const c5f::PlotRecord& r : pl.records())
	{
		const c5f::PlotBlocks* b = nullptr;
		if(!pl.blocks(g, cut, r.plotX, r.plotY, &b).ok() || !b)
			return false;
		built.push_back({r.plotX, r.plotY, cut.generation()});
	}
	return true;
}

bool sameInstances(const c5f::PlotBlocks& a, const c5f::PlotBlocks& b)
{
	return a.instances.size() == b.instances.size() &&
	       std::memcmp(a.instances.data(), b.instances.data(), a.instances.size() * sizeof(c5f::Instance)) == 0 &&
	       a.counts.drawn == b.counts.drawn && a.counts.water == b.counts.water && a.counts.slope == b.counts.slope &&
	       a.drawn.size() == b.drawn.size();
}

double median(std::vector<double> v)
{
	std::sort(v.begin(), v.end());
	return v.empty() ? 0 : v[v.size() / 2];
}

// One recorded step: before -> after with the step's boxes. Returns false on a failed check.
void step(const char* name, const c5h::HeightGrid& g0, const c5h::HeightGrid& g1, const std::vector<Plot>& plots,
          const std::vector<SampleBox>& boxes)
{
	c5f::Placement pl;
	std::vector<BuiltPlot> built;
	CHECK(makePlacement(g0, plots, pl, built));
	c5f::EmptyCut cut;
	// before: every plot's blocks as built on g0
	std::map<std::pair<int, int>, c5f::PlotBlocks> before;
	for(const BuiltPlot& p : built)
	{
		const c5f::PlotBlocks* b = nullptr;
		pl.blocks(g0, cut, p.x, p.y, &b);
		before[{p.x, p.y}] = *b;
	}
	c5f::Placement ref = pl;   // the whole-map path's result
	std::set<std::pair<int, int>> rebuilt;
	const ReheightStats s = ReheightBuilt(pl, g1, cut, built, boxes, [&](int32_t x, int32_t y, const c5f::PlotRecord*, const c5f::PlotBlocks* b) {
		CHECK(b != nullptr);
		rebuilt.insert({x, y});
	});
	CHECK(s.failed == 0);
	CHECK(s.rebuilt == rebuilt.size());
	ref.invalidateAll();
	uint64_t trees = 0, drawn = 0, drawnInRebuilt = 0, staleDrawn = 0, cullChanged = 0, outsideChanged = 0;
	double staleMax = 0;
	uint32_t plotsDiffer = 0;
	FILE* csv = nullptr;
	if(!g_dump.empty())
	{
		const std::string p = g_dump + "/" + name + ".csv";
		fopen_s(&csv, p.c_str(), "w");
		CHECK(csv != nullptr);   // --dump names a folder that exists
		if(csv)
			std::fprintf(csv, "plotx,ploty,i,sx,sy,z_before,scale_before,z_after,scale_after,ground_after,rebuilt\n");
	}
	for(const BuiltPlot& p : built)
	{
		const c5f::PlotBlocks* a = nullptr;
		const c5f::PlotBlocks* r = nullptr;
		CHECK(pl.blocks(g1, cut, p.x, p.y, &a).ok() && a);
		CHECK(ref.blocks(g1, cut, p.x, p.y, &r).ok() && r);
		if(!a || !r)
			continue;
		const bool eq = sameInstances(*a, *r);
		CHECK(eq);
		if(!eq)
			plotsDiffer++;
		const c5f::PlotBlocks& b0 = before[{p.x, p.y}];
		const bool isRebuilt = rebuilt.count({p.x, p.y}) != 0;
		if(!isRebuilt)
			CHECK(sameInstances(*a, b0));
		else if(!sameInstances(*r, b0))
			;   // a changed plot: counted below
		if(!isRebuilt && !sameInstances(*r, b0))
			outsideChanged++;   // a plot the window path left whose reference changed: must stay 0
		for(size_t i = 0; i < a->instances.size(); ++i)
		{
			const c5f::Instance& in = a->instances[i];
			trees++;
			const float bx = (g1.ox + in.x) / g1.sx, by = (g1.oy + in.y) / g1.sy;
			const int c = (int)bx, rr = (int)by;
			const float ground = c5h::heightAtSample(g1, c, rr);
			if(in.scale > 0)
			{
				drawn++;
				CHECK(in.z == ground);
				if(isRebuilt)
					drawnInRebuilt++;
				const c5f::Instance& o = b0.instances[i];
				if(o.scale > 0 && o.z != ground)
				{
					staleDrawn++;
					staleMax = std::max(staleMax, (double)std::fabs(o.z - ground));
				}
			}
			if((in.scale > 0) != (b0.instances[i].scale > 0))
				cullChanged++;
			if(csv && isRebuilt)
				std::fprintf(csv, "%d,%d,%u,%.3f,%.3f,%.4f,%.4f,%.4f,%.4f,%.4f,%d\n", p.x, p.y, (unsigned)i, bx, by, b0.instances[i].z,
				             b0.instances[i].scale, in.z, in.scale, ground, 1);
		}
	}
	if(csv)
		std::fclose(csv);
	CHECK(outsideChanged == 0);
	// cost: 7 runs of each path from the before state
	std::vector<double> win, whole;
	for(int k = 0; k < 7; ++k)
	{
		c5f::Placement a = ref;
		a.invalidateAll();
		for(const BuiltPlot& p : built)
		{
			const c5f::PlotBlocks* b = nullptr;
			a.blocks(g0, cut, p.x, p.y, &b);
		}
		c5f::Placement w = a;
		const ReheightStats ws = ReheightBuilt(w, g1, cut, built, boxes, nullptr);
		win.push_back(ws.ms);
		const auto t0 = std::chrono::steady_clock::now();
		a.invalidateAll();
		for(const BuiltPlot& p : built)
		{
			const c5f::PlotBlocks* b = nullptr;
			a.blocks(g1, cut, p.x, p.y, &b);
		}
		whole.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
	}
	std::printf("  {\"step\":\"%s\",\"boxes\":%zu,\"box0\":[%d,%d,%d,%d],\"records\":%zu,\"trees\":%llu,\"drawn\":%llu,"
	            "\"plots_rebuilt\":%u,\"drawn_in_rebuilt\":%llu,\"drawn_stale_without_reheight\":%llu,\"stale_max_dz\":%.4f,"
	            "\"cull_changed\":%llu,\"plots_differing_from_whole_map\":%u,\"outside_changed\":%llu,\"window_ms_median\":%.3f,"
	            "\"window_ms_max\":%.3f,\"whole_map_ms_median\":%.3f,\"whole_map_ms_max\":%.3f}\n",
	            name, boxes.size(), boxes[0].x0, boxes[0].y0, boxes[0].x1, boxes[0].y1, built.size(), (unsigned long long)trees,
	            (unsigned long long)drawn, s.rebuilt, (unsigned long long)drawnInRebuilt, (unsigned long long)staleDrawn, staleMax,
	            (unsigned long long)cullChanged, plotsDiffer, (unsigned long long)outsideChanged, median(win),
	            *std::max_element(win.begin(), win.end()), median(whole), *std::max_element(whole.begin(), whole.end()));
}

struct Map
{
	c5h::HeightGrid g0;
	std::vector<Plot> plots;
};

bool loadMap(const std::string& dir, Map& m)
{
	int w = 0, h = 0;
	std::vector<uint8_t> blk;
	if(!readPlots(dir + "/plots.txt", w, h, m.plots) || !readFile(dir + "/block.bin", blk) ||
	   !frameFromBlock(blk, (uint32_t)w, (uint32_t)h + 4, m.g0) || !readH16(dir + "/h16.raw", m.g0, m.g0.h))
	{
		std::printf("  inputs missing or malformed under %s\n", dir.c_str());
		return false;
	}
	return true;
}

// a chain of recorded steps: field.<k>.h16 after r<k+1>.json, each step from the previous field
void chain(const char* prefix, const std::string& mapDir, const std::string& chainDir, int steps)
{
	Map m;
	if(!loadMap(mapDir, m))
	{
		++g_fail;
		return;
	}
	c5h::HeightGrid prev = m.g0;
	for(int k = 0; k < steps; ++k)
	{
		c5h::HeightGrid next = prev;
		std::vector<SampleBox> boxes;
		if(!readH16(chainDir + "/field." + std::to_string(k) + ".h16", next, next.h) ||
		   !readWindow(chainDir + "/r" + std::to_string(k + 1) + ".json", boxes))
		{
			std::printf("  step %d inputs missing under %s\n", k, chainDir.c_str());
			++g_fail;
			return;
		}
		// the field changed only inside the boxes (the producer's contract; so a box list is a complete change list)
		uint64_t outside = 0, inside = 0;
		for(uint32_t y = 0; y < prev.samplesY; ++y)
			for(uint32_t x = 0; x < prev.samplesX; ++x)
			{
				const size_t i = prev.index(x, y);
				if(prev.h[i] == next.h[i])
					continue;
				bool in = false;
				for(const SampleBox& b : boxes)
					in = in || ((int)x >= b.x0 && (int)x <= b.x1 && (int)y >= b.y0 && (int)y <= b.y1);
				(in ? inside : outside)++;
			}
		std::printf("  step %d: %llu samples changed inside the window, %llu outside\n", k, (unsigned long long)inside,
		            (unsigned long long)outside);
		CHECK(outside == 0);
		step((std::string(prefix) + "_" + std::to_string(k + 1)).c_str(), prev, next, m.plots, boxes);
		prev = next;
	}
}

// ---- cases ----

void caseFrame()
{
	const std::string tap = env("C5_4904_TINY_TAP",
	                            "F:/c5-64bit-work/0465/4906/scenes/bundles/PT01_20261118-233525_workstation/heightfield/load0003.c5hf");
	std::vector<uint8_t> raw, blk;
	if(!readFile(tap, raw) || !readFile(dataDir() + "/tiny/block.bin", blk))
	{
		std::printf("  tap or block missing\n");
		++g_fail;
		return;
	}
	c5h::HeightGrid t, b;
	CHECK(c5h::readTap(raw.data(), raw.size(), t).ok());
	CHECK(frameFromBlock(blk, t.mapW, t.mapH, b));
	CHECK(std::memcmp(t.hexToWorld, b.hexToWorld, 64) == 0);
	CHECK(t.sx == b.sx && t.sy == b.sy && t.sz == b.sz && t.ox == b.ox && t.oy == b.oy && t.waterZ == b.waterZ && t.wrapX == b.wrapX);
	std::printf("  tap %ux%u plots, wrap %d, sx %g sy %g sz %g ox %g oy %g waterZ %g: block frame %s\n", t.mapW, t.mapH, (int)t.wrapX, t.sx,
	            t.sy, t.sz, t.ox, t.oy, t.waterZ, std::memcmp(t.hexToWorld, b.hexToWorld, 64) == 0 ? "equal" : "DIFFERS");
}

void caseUnits()
{
	CHECK(DerivedAfterWindow(nullptr, std::vector<SampleBox>(1)) == 0);
	Map m;
	if(!loadMap(dataDir() + "/tiny", m))
	{
		++g_fail;
		return;
	}
	c5f::Placement pl;
	std::vector<BuiltPlot> built;
	CHECK(makePlacement(m.g0, m.plots, pl, built));
	c5f::EmptyCut cut;
	int calls = 0;
	auto count = [&](int32_t, int32_t, const c5f::PlotRecord*, const c5f::PlotBlocks*) { ++calls; };
	// no box: nothing
	ReheightStats s = ReheightBuilt(pl, m.g0, cut, built, {}, count);
	CHECK(s.touched == 0 && calls == 0 && s.builtSeen == built.size());
	// a box over the sea corner (0,0)-(3,3) samples: Tiny's corner plot (0,0) is ocean; no tree reads it
	SampleBox far;
	far.x0 = 1000000;
	far.y0 = 1000000;
	far.x1 = 1000001;
	far.y1 = 1000001;
	s = ReheightBuilt(pl, m.g0, cut, built, {far}, count);
	CHECK(s.touched == 0 && calls == 0);
	// one plot's own trees: a box over its first drawn tree's sample touches it
	const BuiltPlot p = built.front();
	const c5f::PlotBlocks* b = nullptr;
	CHECK(pl.blocks(m.g0, cut, p.x, p.y, &b).ok() && b && !b->drawn.empty());
	if(b && !b->drawn.empty())
	{
		const float bx = (m.g0.ox + b->drawn[0].worldX) / m.g0.sx, by = (m.g0.oy + b->drawn[0].worldY) / m.g0.sy;
		SampleBox one;
		one.x0 = one.x1 = (int)bx;
		one.y0 = one.y1 = (int)by;
		CHECK(BlocksReadBoxes(m.g0, *b, {one}));
		calls = 0;
		s = ReheightBuilt(pl, m.g0, cut, built, {one}, count);
		CHECK(s.touched >= 1 && s.rebuilt == s.touched && (uint32_t)calls == s.touched && s.failed == 0);
		CHECK(s.touched < built.size());
		std::printf("  a one-sample box at %d,%d: %u of %u built plots touched\n", one.x0, one.y0, s.touched, s.builtSeen);
		// the x wrap: the same box moved by one map width (a tree's column read modulo the width)
		if(m.g0.wrapX)
		{
			SampleBox wrapped = one;
			wrapped.x0 = wrapped.x1 = one.x0 - (int)m.g0.samplesX;
			CHECK(BlocksReadBoxes(m.g0, *b, {wrapped}));
		}
		// the margin: kTreeReadMargin samples away still touches, one more does not (when no other tree is near)
		SampleBox edge = one;
		edge.x0 = edge.x1 = one.x0 + kTreeReadMargin;
		CHECK(BlocksReadBoxes(m.g0, *b, {edge}));
	}
	// MC #9538 item 3: a window across the x seam is two rectangles at the field's ends. The hook now hands each over as
	// its own box (SampleBoxOfRect); their bounding box (the change's ApplyStats, before the fix) spans the whole width and
	// rebuilds the trees of every plot in those rows.
	if(m.g0.wrapX)
	{
		const uint32_t S = m.g0.samplesX, y0 = m.g0.samplesY / 2 - 64, h = 128;
		const std::vector<SampleBox> rects = {SampleBoxOfRect(S - 40, y0, 40, h), SampleBoxOfRect(0, y0, 40, h)};
		CHECK(rects[0].x0 == (int32_t)S - 40 && rects[0].x1 == (int32_t)S - 1 && rects[1].x0 == 0 && rects[1].x1 == 39 &&
		      rects[0].y1 == (int32_t)(y0 + h - 1));
		SampleBox bbox;
		bbox.x0 = 0;
		bbox.y0 = (int32_t)y0;
		bbox.x1 = (int32_t)S - 1;
		bbox.y1 = (int32_t)(y0 + h - 1);
		int inRects = 0, inBox = 0, notInBox = 0;
		for(const BuiltPlot& bp : built)
		{
			const c5f::PlotBlocks* pb = nullptr;
			if(!pl.blocks(m.g0, cut, bp.x, bp.y, &pb).ok() || !pb)
				continue;
			const bool r = BlocksReadBoxes(m.g0, *pb, rects), x = BlocksReadBoxes(m.g0, *pb, {bbox});
			inRects += r ? 1 : 0;
			inBox += x ? 1 : 0;
			notInBox += r && !x ? 1 : 0;
		}
		CHECK(notInBox == 0);          // the rectangles are inside their bounding box
		CHECK(inBox > inRects);        // and the box reaches plots the rectangles do not
		const auto t0 = std::chrono::steady_clock::now();
		calls = 0;
		const ReheightStats sr = ReheightBuilt(pl, m.g0, cut, built, rects, count);
		const auto t1 = std::chrono::steady_clock::now();
		const ReheightStats sb = ReheightBuilt(pl, m.g0, cut, built, {bbox}, count);
		const auto t2 = std::chrono::steady_clock::now();
		CHECK(sr.failed == 0 && sb.failed == 0 && sr.touched == (uint32_t)inRects && sb.touched == (uint32_t)inBox);
		std::printf("  a seam-crossing window (2 x 40 x %u samples): %u built plots rebuilt from its rectangles (%.1f ms), %u from "
		            "their bounding box (%.1f ms), of %u\n",
		            h, sr.touched, std::chrono::duration<double, std::milli>(t1 - t0).count(), sb.touched,
		            std::chrono::duration<double, std::milli>(t2 - t1).count(), sr.builtSeen);
	}
	// a plot listed as built whose record is gone: failed, the forest's signal for the whole-map fallback
	BuiltPlot ghost;
	ghost.x = 0;
	ghost.y = 0;
	ghost.cutGeneration = cut.generation() + 1;   // another generation: always touched
	calls = 0;
	s = ReheightBuilt(pl, m.g0, cut, {ghost}, {far}, count);
	CHECK(s.touched == 1 && s.failed == 1 && calls == 1 && !s.firstError.empty());
	std::printf("  ghost plot: %s\n", s.firstError.c_str());
}

void caseTiny()
{
	chain("tiny_fh", dataDir() + "/tiny", dataDir() + "/fh", 4);
}

void caseHuge()
{
	chain("huge", dataDir() + "/huge", dataDir() + "/hugechain", 2);
}

}  // namespace

int main(int argc, char** argv)
{
	struct Case
	{
		const char* name;
		void (*fn)();
	};
	const Case cases[] = {{"frame", caseFrame}, {"units", caseUnits}, {"tiny_fh", caseTiny}, {"huge_chain", caseHuge}};
	std::vector<std::string> want;
	for(int i = 1; i < argc; ++i)
	{
		if(std::strcmp(argv[i], "--dump") == 0 && i + 1 < argc)
			g_dump = argv[++i];
		else
			want.push_back(argv[i]);
	}
	int failedCases = 0, ran = 0;
	for(const Case& c : cases)
	{
		if(!want.empty() && std::find(want.begin(), want.end(), c.name) == want.end())
			continue;
		++ran;
		g_case = c.name;
		const int before = g_fail;
		c.fn();
		const bool ok = g_fail == before;
		std::printf("CASE %s %s\n", c.name, ok ? "PASS" : "FAIL");
		std::fflush(stdout);
		if(!ok)
			++failedCases;
	}
	if(!ran)
	{
		std::printf("no case matches\n");
		return 2;
	}
	return failedCases ? 1 : 0;
}
