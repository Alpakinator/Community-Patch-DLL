// lt_hook_run - the server's live-terrain hook (srv_liveterrain*.cpp: LiveTerrain, the adapter, ApplyWindow, the journal
// and its Replay) on recorded inputs with 4903's real producer (ticket 4907). Offline only; not shipped.
// Written 2026-11-19 by w5-4907 for this repository (GPL-3.0-or-later).
//
//   lt_hook_run --lib FXSB --block BLOCK.bin --plots PLOTS.txt --h16 H.raw --m8 M.raw --change A.txt[,B.txt...]
//               [--field-after PREFIX] [--river-rule edges|old]   |   ... --survey N (no --change: the live share per kind
//               of change)
//
// The inputs are 4903's runner's (render/tests/liveterrain/producer/lt_producer_run.cpp): PLOTS.txt "W H", then one line
// per plot "x y terrain hills mountain feature featureType continentArt riverEdges lake". Each table is turned into the
// feed's records as the server has them (srv_feed.h PlotRecord: plot type from hills / mountain / water terrain 5-6, the
// river flag from any edge bit and, since ticket 0631, the edge bits themselves with known bit 2; --river-rule old leaves
// the edges out, as the records of a bundle recorded before 0631), no lake bit, the natural-wonder bit for a FEATURE_ name other than FEATURE_MARSH, the art
// type), names by feature id from the file, then: LiveTerrain (inline) onField, each change onTable, each live window
// ApplyWindow'd into the field and noted (no EXE: the EXE field write is 4905's, not run here). At the end the journal goes
// through EncodeSpanText / DecodeSpanText and a Replay from the load's field, whose result must equal the live field.
// One JSON line per step on stdout; PREFIX.<k>.h16 / .m8 the field after change k. Exit 0 ok, 2 bad input, 1 an error
// (a create error, an apply error, the replay not equal).
#include "srv_liveterrain.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace srvrender::liveterrain;

namespace {
bool ReadFile(const std::string& p, std::vector<uint8_t>& out)
{
	std::ifstream f(p, std::ios::binary);
	if(!f)
		return false;
	out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	return true;
}
bool WriteFile(const std::string& p, const void* d, size_t n)
{
	std::ofstream f(p, std::ios::binary);
	if(!f)
		return false;
	f.write((const char*)d, (std::streamsize)n);
	return (bool)f;
}

std::map<int, std::string> g_names;
bool g_oldRiverRule = false;   // --river-rule old: the records carry no edges (as before ticket 0631: flag -> 7 edges)

bool ReadPlots(const std::string& p, std::vector<SrvFeed::PlotRecord>& recs, uint32_t& W, uint32_t& H)
{
	std::ifstream f(p);
	int w = 0, h = 0;
	if(!f || !(f >> w >> h) || w <= 0 || h <= 0 || w > 256 || h > 256)
		return false;
	W = (uint32_t)w;
	H = (uint32_t)h;
	recs.assign((size_t)w * h, SrvFeed::PlotRecord());
	std::vector<uint8_t> seen(recs.size(), 0);
	for(size_t i = 0; i < recs.size(); ++i)
	{
		int x, y, terrain, hills, mountain, feature, cart, river, lake;
		std::string ft;
		if(!(f >> x >> y >> terrain >> hills >> mountain >> feature >> ft >> cart >> river >> lake))
			return false;
		if(x < 0 || x >= w || y < 0 || y >= h || seen[(size_t)y * w + x])
			return false;
		seen[(size_t)y * w + x] = 1;
		SrvFeed::PlotRecord& r = recs[(size_t)y * w + x];
		memset(&r, 0, sizeof(r));
		r.terrain = (int8_t)terrain;
		r.plotType = (int8_t)(mountain ? 0 : hills ? 1 : (terrain == 5 || terrain == 6) ? 3 : 2);
		r.feature = (int8_t)feature;
		r.route = -1;
		r.resource = -1;
		r.improvement = -1;
		r.owner = -1;
		r.flags = (uint8_t)((river ? 0x1 : 0) | (ft != "-" && ft.compare(0, 8, "FEATURE_") == 0 && ft != "FEATURE_MARSH" ? 0x8 : 0));
		r.artType = (uint8_t)cart;
		r.known = 1;
		if(!g_oldRiverRule)   // ticket 0631: the plot's own edge bits, as the server's feed has them since 0631
		{
			r.riverEdges = (uint8_t)(river & 7);
			r.known |= SrvFeed::kPlotKnownRiverEdges;
		}
		(void)lake;
		if(ft != "-" && feature >= 0)
			g_names[feature] = ft;
	}
	return true;
}
std::string Name(int32_t id)
{
	auto it = g_names.find(id);
	return it == g_names.end() ? std::string() : it->second;
}
std::vector<std::string> Split(const std::string& s)
{
	std::vector<std::string> v;
	std::stringstream ss(s);
	std::string t;
	while(std::getline(ss, t, ','))
		if(!t.empty())
			v.push_back(t);
	return v;
}
void Log(const char* line)
{
	fprintf(stderr, "%s\n", line);
}
}  // namespace

int main(int argc, char** argv)
{
	std::string lib, block, plots, h16, m8, change, fieldAfter;
	int surveyN = 0;
	for(int i = 1; i + 1 < argc; i += 2)
	{
		const std::string k = argv[i], v = argv[i + 1];
		if(k == "--lib") lib = v;
		else if(k == "--block") block = v;
		else if(k == "--plots") plots = v;
		else if(k == "--h16") h16 = v;
		else if(k == "--m8") m8 = v;
		else if(k == "--change") change = v;
		else if(k == "--field-after") fieldAfter = v;
		else if(k == "--survey") surveyN = atoi(v.c_str());
		else if(k == "--river-rule" && (v == "old" || v == "edges")) g_oldRiverRule = v == "old";   // ticket 0631
		else
		{
			fprintf(stderr, "lt_hook_run: unknown option %s\n", k.c_str());
			return 2;
		}
	}
	if(lib.empty() || block.empty() || plots.empty() || h16.empty() || m8.empty() || (change.empty() && surveyN <= 0))
	{
		fprintf(stderr, "lt_hook_run: --lib --block --plots --h16 --m8 --change are required\n");
		return 2;
	}
	std::vector<SrvFeed::PlotRecord> base;
	uint32_t W = 0, H = 0;
	std::vector<uint8_t> blk, hb, mb;
	if(!ReadPlots(plots, base, W, H) || !ReadFile(block, blk) || !ReadFile(h16, hb) || !ReadFile(m8, mb))
	{
		fprintf(stderr, "lt_hook_run: an input could not be read\n");
		return 2;
	}
	c5r::heightfield::HeightGrid g;
	g.mapW = W;
	g.mapH = H + 4;
	g.samplesX = W * 64;
	g.samplesY = (H + 4) * 64;
	const size_t n = (size_t)g.samplesX * g.samplesY;
	if(hb.size() != n * 2 || mb.size() != n)
	{
		fprintf(stderr, "lt_hook_run: the layers are not %ux%u samples\n", g.samplesX, g.samplesY);
		return 2;
	}
	g.h.resize(n);
	memcpy(g.h.data(), hb.data(), n * 2);
	g.mat = mb;
	const c5r::heightfield::HeightGrid load = g;
	const std::wstring wlib(lib.begin(), lib.end());
	LiveTerrain::Config c;
	c.factory = MakeC5rProducerFactory(wlib);
	c.threaded = false;
	c.log = Log;
	LiveTerrain lt(c);
	LoadIn in;
	in.loadSeq = 1;
	in.constants = blk;
	std::string why;
	if(!PlotTableFromFeed(base, W, H, Name, in.table, why))
	{
		fprintf(stderr, "lt_hook_run: %s\n", why.c_str());
		return 2;
	}
	in.samplesX = g.samplesX;
	in.samplesY = g.samplesY;
	in.heights = g.h;
	in.materials = g.mat;
	lt.onField(std::move(in), base);
	if(!lt.ready())
	{
		printf("{\"step\":\"create\",\"ok\":false}\n");
		return 1;
	}
	printf("{\"step\":\"create\",\"ok\":true,\"ms\":%.1f}\n", lt.counters().createMsLast);
	// --survey N: the live share per kind of change (MC #6636). N land plots (every k-th, map order), each changed from the
	// load's table and then changed back, through the hook: kinds "hills" (flat -> hills), "mountain" (flat or hills ->
	// mountain), "flat" (hills -> flat). One JSON line per kind with live / wait counts by reason.
	if(surveyN > 0)
	{
		const char* kinds[] = {"hills", "mountain", "flat"};
		for(int kind = 0; kind < 3; ++kind)
		{
			std::vector<size_t> cand;
			for(size_t i = 0; i < base.size(); ++i)
			{
				const SrvFeed::PlotRecord& r = base[i];
				if(r.plotType == 3 || (r.flags & 0x8))
					continue;
				if((kind == 0 && r.plotType == 2) || (kind == 1 && (r.plotType == 2 || r.plotType == 1)) || (kind == 2 && r.plotType == 1))
					cand.push_back(i);
			}
			const size_t step = cand.size() > (size_t)surveyN ? cand.size() / (size_t)surveyN : 1;
			std::map<std::string, int> count;
			std::string livePlots;
			int tried = 0, live = 0;
			double msSum = 0;
			for(size_t c = 0; c < cand.size() && tried < surveyN; c += step)
			{
				std::vector<SrvFeed::PlotRecord> t = base;
				SrvFeed::PlotRecord& r = t[cand[c]];
				if(kind == 0)
					r.plotType = 1;
				else if(kind == 1)
				{
					r.plotType = 0;
					r.terrain = 7;
				}
				else
					r.plotType = 2;
				lt.onTable(t, W, H, Name);
				Result res;
				if(!lt.takeResult(res))
					continue;
				tried++;
				msSum += res.out.producerMs;
				if(res.out.live)
				{
					ApplyStats st;
					if(ApplyWindow(g, res.out.window, st, why))
						lt.noteApplied(res, st, 0, RegionSha256(g, st.x0, st.y0, st.x1, st.y1));
					live++;
					count["live"]++;
					livePlots += (livePlots.empty() ? "" : ",") + std::string("[") + std::to_string(cand[c] % W) + "," + std::to_string(cand[c] / W) + "]";
				}
				else
					count[WaitName(res.out.reason)]++;
				lt.onTable(base, W, H, Name);   // back (a held plot is released without a job; a live one is changed back)
				while(lt.takeResult(res))
					if(res.out.live)
					{
						ApplyStats st;
						if(ApplyWindow(g, res.out.window, st, why))
							lt.noteApplied(res, st, 0, RegionSha256(g, st.x0, st.y0, st.x1, st.y1));
					}
			}
			std::string by;
			for(const auto& kv : count)
				by += (by.empty() ? "" : ",") + std::string("\"") + kv.first + "\":" + std::to_string(kv.second);
			printf("{\"step\":\"survey\",\"kind\":\"%s\",\"candidates\":%zu,\"tried\":%d,\"live\":%d,\"share\":%.3f,\"meanProducerMs\":%.1f,\"by\":{%s},\"livePlots\":[%s]}\n",
			       kinds[kind], cand.size(), tried, live, tried ? (double)live / tried : 0.0, tried ? msSum / tried : 0.0, by.c_str(),
			       livePlots.c_str());
		}
		return 0;
	}
	const std::vector<std::string> changes = Split(change);
	int rc = 0;
	for(size_t k = 0; k < changes.size(); ++k)
	{
		std::vector<SrvFeed::PlotRecord> t;
		uint32_t w2 = 0, h2 = 0;
		if(!ReadPlots(changes[k], t, w2, h2) || w2 != W || h2 != H)
		{
			fprintf(stderr, "lt_hook_run: %s: not a %ux%u table\n", changes[k].c_str(), W, H);
			return 2;
		}
		const int64_t posted = lt.counters().changesPosted;
		lt.onTable(t, W, H, Name);
		Result r;
		if(lt.counters().changesPosted == posted || !lt.takeResult(r))
		{
			printf("{\"step\":\"change\",\"k\":%zu,\"posted\":false}\n", k);
			continue;
		}
		std::string plotsTxt;
		for(size_t i = 0; i < r.out.changedPlots.size(); ++i)
			plotsTxt += (i ? "," : "") + std::string("[") + std::to_string(r.out.changedPlots[i].first) + "," +
			            std::to_string(r.out.changedPlots[i].second) + "]";
		ApplyStats st;
		std::string sha;
		if(r.out.live)
		{
			if(!ApplyWindow(g, r.out.window, st, why))
			{
				lt.noteApplyError(r, why);
				rc = 1;
			}
			else
			{
				sha = RegionSha256(g, st.x0, st.y0, st.x1, st.y1);
				lt.noteApplied(r, st, 0, sha);
			}
		}
		printf("{\"step\":\"change\",\"k\":%zu,\"live\":%s,\"reason\":\"%s\",\"changedPlots\":[%s],\"rects\":%zu,\"samplesD\":%llu,"
		       "\"samplesFull\":%llu,\"samplesRing\":%llu,\"heightsChanged\":%llu,\"box\":[%u,%u,%u,%u],\"producerMs\":%.1f,\"sha256\":\"%s\"}\n",
		       k, r.out.live ? "true" : "false", WaitName(r.out.reason), plotsTxt.c_str(), r.out.window.size(),
		       (unsigned long long)r.out.inD, (unsigned long long)r.out.inW1, (unsigned long long)r.out.inRing,
		       (unsigned long long)st.heightsChanged, st.x0, st.y0, st.x1, st.y1, r.out.producerMs, sha.c_str());
		if(!fieldAfter.empty())
		{
			WriteFile(fieldAfter + "." + std::to_string(k) + ".h16", g.h.data(), g.h.size() * 2);
			WriteFile(fieldAfter + "." + std::to_string(k) + ".m8", g.mat.data(), g.mat.size());
		}
	}
	// the bundle's round trip and the player's replay
	std::vector<FrameGen> frames;
	FrameGen f0;
	f0.frameId = 1;
	f0.gen = lt.counters().fieldGeneration;
	frames.push_back(f0);
	const std::string text = EncodeSpanText(lt.journal(), frames, Name);
	Journal j;
	std::vector<FrameGen> df;
	if(!DecodeSpanText(text, j, df, why))
	{
		printf("{\"step\":\"replay\",\"ok\":false,\"why\":\"decode: %s\"}\n", why.c_str());
		return 1;
	}
	Replay rp;
	c5r::heightfield::HeightGrid rg = load;
	c5r::Status s = rp.init(c.factory, j, rg);
	bool changed = false;
	if(s.ok())
		s = rp.advanceTo(f0.gen, rg, changed);
	const bool equal = s.ok() && rg.h == g.h && rg.mat == g.mat;
	printf("{\"step\":\"replay\",\"ok\":%s,\"generations\":%u,\"journalBytes\":%zu,\"equal\":%s,\"why\":\"%s\"}\n", s.ok() ? "true" : "false",
	       f0.gen, text.size(), equal ? "true" : "false", s.ok() ? "" : s.str().c_str());
	if(!equal)
		rc = 1;
	return rc;
}
