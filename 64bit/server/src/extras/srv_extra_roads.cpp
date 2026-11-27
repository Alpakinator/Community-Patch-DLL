// srv_extra_roads.cpp - FixRoadLayout's producer (ticket 4934). See srv_extra_roads.h.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "srv_extra_roads.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <set>
#include <sstream>

namespace srvextras {

namespace {

double NowMs()
{
	using namespace std::chrono;
	return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

// half floats of the decal vertices (IEEE 754 binary16)
float HalfToFloat(uint16_t h)
{
	const uint32_t s = (uint32_t)(h & 0x8000) << 16, e = (h >> 10) & 0x1F, m = h & 0x3FF;
	uint32_t f;
	if(e == 0)
	{
		if(m == 0)
			f = s;
		else
		{   // subnormal
			float v = (float)m / 1024.0f / 16384.0f;
			return s ? -v : v;
		}
	}
	else if(e == 31)
		f = s | 0x7F800000u | (m << 13);
	else
		f = s | ((e + 112) << 23) | (m << 13);
	float out;
	memcpy(&out, &f, sizeof out);
	return out;
}

struct P2
{
	float x, y;
};

float SegDist2(P2 p, P2 a, P2 b)
{
	const float abx = b.x - a.x, aby = b.y - a.y, l2 = abx * abx + aby * aby;
	float t = l2 > 0 ? ((p.x - a.x) * abx + (p.y - a.y) * aby) / l2 : 0.0f;
	t = (std::max)(0.0f, (std::min)(1.0f, t));
	const float dx = p.x - (a.x + abx * t), dy = p.y - (a.y + aby * t);
	return dx * dx + dy * dy;
}

bool InTri(P2 p, P2 a, P2 b, P2 c)
{
	const float d1 = (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y);
	const float d2 = (p.x - c.x) * (b.y - c.y) - (b.x - c.x) * (p.y - c.y);
	const float d3 = (p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y);
	const bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
	return !(neg && pos);
}

}  // namespace

roadsx::LayoutOptions ProductRoadStyle()
{
	roadsx::LayoutOptions o;
	o.style = roadsx::Style::Line;   // round 2, variant (e) (D212): candidates.py "line_wide_merge"
	o.filletRadius = 22.0f;
	o.mergeLength = 22.0f;
	return o;   // every other parameter at the generator's default (leafCurve 14, subStep 2.5, stubLength 16, priority 0.7)
}

roadsx::LayoutOptions RoadStyleFor(const RoadLayoutChoice& c)
{
	roadsx::LayoutOptions o = ProductRoadStyle();
	if(c.layout != 2)
		return o;   // layout 1 (and anything else: the tidy layout)
	o.connect = roadsx::Connect::All;   // part 1 (c7c6738a1): every pair of neighbouring route plots joined
	const int j = (std::min)(3, (std::max)(0, c.junction));
	o.junction = j == 1 ? roadsx::Junction::Cross : j == 2 ? roadsx::Junction::Ring : j == 3 ? roadsx::Junction::Rosette
	                                                                                    : roadsx::Junction::Through;
	o.lineJitter = (float)(std::min)(7, (std::max)(0, c.jitter));   // part 1b (f2c903ddb): at most 7 (roads.md 4f)
	return o;
}

std::string RoadLayoutChoiceText(const RoadLayoutChoice& c)
{
	if(c.layout != 2)
		return "layout 1 (tidy, variant (e))";
	const roadsx::LayoutOptions o = RoadStyleFor(c);
	char b[128];
	_snprintf_s(b, sizeof(b), _TRUNCATE, "layout 2 (all connections), junction %s, jitter %d", roadsx::junctionName(o.junction),
	            (int)o.lineJitter);
	return b;
}

bool RoadMapFromTable(const std::vector<SrvFeed::PlotRecord>& rec, uint32_t width, uint32_t height, bool wrapX,
                      roadsx::MapIn& out)
{
	out = roadsx::MapIn();
	if(!width || !height || width > roadsx::kMaxPlotsPerAxis || height > roadsx::kMaxPlotsPerAxis ||
	   rec.size() != (size_t)width * height)
		return false;
	bool anyFow = false;
	for(const SrvFeed::PlotRecord& r : rec)
		anyFow = anyFow || (r.fow & 1);
	out.width = width;
	out.height = height;
	out.wrapX = wrapX;
	out.plots.resize(rec.size());
	for(size_t i = 0; i < rec.size(); ++i)
	{
		const SrvFeed::PlotRecord& r = rec[i];
		if(anyFow && !(r.fow & 1))
			continue;   // not revealed to the active team: the EXE lays no route there either
		out.plots[i].band = (uint8_t)roadsx::bandFromFeed(r.route, r.routeState, r.flags);
		out.plots[i].city = (r.flags & 16) ? 1 : 0;
	}
	return true;
}

std::string RoadSpanText(const roadsx::MapIn& m)
{
	std::string s;
	size_t n = 0;
	for(const roadsx::PlotIn& p : m.plots)
		n += (p.band != roadsx::kBandNone || p.city) ? 1 : 0;
	char head[96];
	_snprintf_s(head, sizeof(head), _TRUNCATE, "%s%u %u %d %zu", kRoadStateForm, m.width, m.height, m.wrapX ? 1 : 0, n);
	s = head;
	for(size_t i = 0; i < m.plots.size(); ++i)
	{
		const roadsx::PlotIn& p = m.plots[i];
		if(p.band == roadsx::kBandNone && !p.city)
			continue;
		char b[48];
		_snprintf_s(b, sizeof(b), _TRUNCATE, " %zu:%u:%u", i, (unsigned)p.band, (unsigned)p.city);
		s += b;
	}
	return s;
}

bool ParseRoadSpanText(const std::string& text, roadsx::MapIn& out, std::string& why)
{
	out = roadsx::MapIn();
	if(text.compare(0, strlen(kRoadStateForm), kRoadStateForm) != 0)
		return why = std::string("not of the form \"") + kRoadStateForm + "...\"", false;
	std::istringstream in(text.substr(strlen(kRoadStateForm)));
	long long w = -1, h = -1, wrap = -1, n = -1;
	if(!(in >> w >> h >> wrap >> n) || w < 1 || h < 1 || w > roadsx::kMaxPlotsPerAxis || h > roadsx::kMaxPlotsPerAxis ||
	   (wrap != 0 && wrap != 1) || n < 0 || n > w * h)
		return why = "the header (W H WRAP N) is missing or out of range", false;
	roadsx::MapIn m;
	m.width = (uint32_t)w;
	m.height = (uint32_t)h;
	m.wrapX = wrap == 1;
	m.plots.resize((size_t)(w * h));
	long long last = -1;
	for(long long k = 0; k < n; ++k)
	{
		std::string tok;
		if(!(in >> tok))
			return why = "fewer plots than N", false;
		long long i = -1;
		unsigned band = 99, city = 99;
		char c1 = 0, c2 = 0, extra = 0;
		if(sscanf_s(tok.c_str(), "%lld%c%u%c%u%c", &i, &c1, 1, &band, &c2, 1, &city, &extra, 1) != 5 || c1 != ':' || c2 != ':')
			return why = "a plot entry is not i:band:city: '" + tok + "'", false;
		if(i <= last || i >= w * h)
			return why = "a plot index is outside the map or out of order: '" + tok + "'", false;
		if(band >= roadsx::kBandCount || city > 1)
			return why = "a band or city mark is out of range: '" + tok + "'", false;
		last = i;
		m.plots[(size_t)i].band = (uint8_t)band;
		m.plots[(size_t)i].city = (uint8_t)city;
	}
	std::string rest;
	if(in >> rest)
		return why = "more plots than N", false;
	out = std::move(m);
	return true;
}

int32_t PlotAtSample(float sx, float sy, uint32_t mapW, uint32_t mapH)
{
	const int y0 = (int)std::floor((sy - 128.0f) / 64.0f + 0.5f);
	int32_t best = -1;
	float bd = 1e30f;
	for(int y = y0 - 1; y <= y0 + 1; ++y)
	{
		if(y < 0 || (uint32_t)y >= mapH)
			continue;
		const int x = (int)std::floor((sx - 32.0f - 32.0f * (float)(y & 1)) / 64.0f + 0.5f);
		for(int xx = x - 1; xx <= x + 1; ++xx)
		{
			if(xx < 0 || (uint32_t)xx >= mapW)
				continue;
			float cx, cy;
			roadsx::plotCentre(xx, y, cx, cy);
			const float dx = sx - cx, dy = (sy - cy) * roadsx::kIsoY;
			const float d = dx * dx + dy * dy;
			if(d < bd)
			{
				bd = d;
				best = (int32_t)((uint32_t)y * mapW + (uint32_t)xx);
			}
		}
	}
	return best;
}

c5r::Status StockRouteClear::build(const c5r::decal::DecalSet& tap, const std::vector<SrvFeed::PlotRecord>& plots, uint32_t mapW,
                                   uint32_t mapH, float band)
{
	m_w = m_h = m_wpr = 0;
	m_bits.clear();
	cellsCleared = cellsKept = 0;
	routeCurves = plotsCleared = plotsKept = 0;
	const float* wr = tap.worldRect;
	if(!(std::isfinite(wr[2]) && std::isfinite(wr[3])) || wr[2] < 0 || wr[3] < 0)
		return c5r::Status::fail(c5r::Err::BadLayout, "StockRouteClear: the tap's world rectangle is not finite");
	if(!(band >= 0 && band <= 16))
		return c5r::Status::fail(c5r::Err::BadLayout, "StockRouteClear: band outside 0 .. 16");
	if(!mapW || !mapH || plots.size() != (size_t)mapW * mapH)
		return c5r::Status::fail(c5r::Err::BadLayout, "StockRouteClear: no plot table of the map's size");
	const uint32_t w = ((uint32_t)wr[2] >> 1) + 1, h = ((uint32_t)wr[3] >> 1) + 1;
	if(w > 16384 || h > 16384)
		return c5r::Status::fail(c5r::Err::BadLayout, "StockRouteClear: more than 16384 cells on an axis");
	const uint32_t wpr = (w + 63) / 64;
	std::vector<uint64_t> bits((size_t)wpr * h, 0), kept((size_t)wpr * h, 0);
	std::set<int32_t> pc, pk;
	const float iy = roadsx::kIsoY;
	for(const c5r::decal::Decal& d : tap.decals)
	{
		if(!roadsx::isTapRouteCurve(tap, d))
			continue;
		if((uint64_t)d.firstVertex + d.vertexCount > tap.vertices.size() || d.vertexCount % 3)
			return c5r::Status::fail(c5r::Err::BadLayout, "StockRouteClear: a route curve's vertex run is outside the set");
		routeCurves++;
		const float rw = d.rect[2] - d.rect[0], rh = d.rect[3] - d.rect[1];
		for(uint32_t t = 0; t < d.vertexCount; t += 3)
		{
			P2 v[3];
			for(int k = 0; k < 3; ++k)
			{
				const c5r::decal::DecalVertex& dv = tap.vertices[d.firstVertex + t + k];
				v[k].x = d.rect[0] + HalfToFloat(dv.px) * rw;
				v[k].y = (d.rect[1] + HalfToFloat(dv.py) * rh) * iy;   // iso
			}
			const float x0 = (std::min)((std::min)(v[0].x, v[1].x), v[2].x) - band;
			const float x1 = (std::max)((std::max)(v[0].x, v[1].x), v[2].x) + band;
			const float y0 = ((std::min)((std::min)(v[0].y, v[1].y), v[2].y) - band) / iy;
			const float y1 = ((std::max)((std::max)(v[0].y, v[1].y), v[2].y) + band) / iy;
			if(!(std::isfinite(x0) && std::isfinite(x1) && std::isfinite(y0) && std::isfinite(y1)))
				continue;
			const int32_t c0 = (std::max)(0, (int32_t)std::floor(x0 / 2)), c1 = (std::min)((int32_t)w - 1, (int32_t)std::ceil(x1 / 2));
			const int32_t r0 = (std::max)(0, (int32_t)std::floor(y0 / 2)), r1 = (std::min)((int32_t)h - 1, (int32_t)std::ceil(y1 / 2));
			const float b2 = band * band;
			for(int32_t row = r0; row <= r1; ++row)
				for(int32_t col = c0; col <= c1; ++col)
				{
					const P2 p{2.0f * col, 2.0f * row * iy};
					if(!(InTri(p, v[0], v[1], v[2]) || SegDist2(p, v[0], v[1]) <= b2 || SegDist2(p, v[1], v[2]) <= b2 ||
					     SegDist2(p, v[2], v[0]) <= b2))
						continue;
					const size_t wi = (size_t)row * wpr + ((uint32_t)col >> 6);
					const uint64_t bit = uint64_t(1) << (col & 63);
					if((bits[wi] | kept[wi]) & bit)
						continue;
					const int32_t pi = PlotAtSample(2.0f * col, 2.0f * row, mapW, mapH);
					const bool bare = pi >= 0 && plots[(size_t)pi].improvement < 0 && !(plots[(size_t)pi].flags & 16);
					if(bare)
					{
						bits[wi] |= bit;
						cellsCleared++;
						pc.insert(pi);
					}
					else
					{
						kept[wi] |= bit;
						cellsKept++;
						if(pi >= 0)
							pk.insert(pi);
					}
				}
		}
	}
	m_w = w;
	m_h = h;
	m_wpr = wpr;
	m_bits.swap(bits);
	plotsCleared = (uint32_t)pc.size();
	plotsKept = (uint32_t)pk.size();
	return c5r::Status::Ok();
}

bool StockRouteClear::cell(int32_t col, int32_t row) const
{
	if(col < 0 || row < 0 || (uint32_t)col >= m_w || (uint32_t)row >= m_h)
		return false;
	return (m_bits[(size_t)row * m_wpr + ((uint32_t)col >> 6)] >> (col & 63)) & 1;
}

uint8_t StockRouteClear::bitsAt(float worldX, float worldY) const
{
	const c5r::forest::CutCell c = c5r::forest::cutCell(worldX, worldY, m_ox, m_oy);
	return cell(c.col, c.row) ? roadsx::kRoadFootprintBit : 0;
}

// ---- the producer ----

// ticket 4940: the layout chosen by the configuration's parser (RoadLayoutChoiceSlot, srv_extras.h), read once
RoadLayoutProducer::RoadLayoutProducer() : m_choice(RoadLayoutChoiceSlot()), m_style(RoadStyleFor(RoadLayoutChoiceSlot())) {}

BridgePlaceResult RoadBridgePlacer::place(const float rowsIn[16], const uint8_t* vb, size_t bytes, uint32_t stride) const
{
	BridgePlaceResult r;
	if(!rowsIn)
		return r;
	memcpy(r.rows, rowsIn, sizeof(r.rows));
	roadsx::BridgeFit fit;
	if(!roadsx::fitBridgeMesh(vb, bytes, stride, fit).ok())
	{
		r.fitFailed = true;
		return r;
	}
	const roadsx::BridgePlacement p = roadsx::placeBridge(m_in, m_net, m_style, rowsIn, fit, m_ox, m_oy);
	r.move = p.action == roadsx::BridgeAction::Move ? BridgeMove::Move
	                                                : (p.action == roadsx::BridgeAction::Drop ? BridgeMove::Drop : BridgeMove::Keep);
	r.edgeKey = p.edgeKey;
	r.edgeDist = p.edgeDist;
	r.shift = p.shift;
	r.turnDeg = p.turnDeg;
	memcpy(r.rows, p.rows, sizeof(r.rows));
	return r;
}

RoadLayoutProducer::~RoadLayoutProducer()
{
	withdrawBridges();
	{
		std::lock_guard<std::mutex> lk(m_mx);
		m_stop = true;
	}
	m_cv.notify_all();
	if(m_worker.joinable())
		m_worker.join();
}

void RoadLayoutProducer::say(const char* fmt, ...) const
{
	if(!m_log)
		return;
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
	va_end(ap);
	m_log(m_logCtx, buf);
}

void RoadLayoutProducer::setWorld(const float worldRect[4], uint32_t loadSeq, bool wrapX, float cutOx, float cutOy)
{
	if(!worldRect)
		return;
	const bool newLoad = !m_haveWorld || loadSeq != m_loadSeq;
	m_ox = cutOx;
	m_oy = cutOy;
	if(newLoad)
	{
		loads++;
		m_loadSeq = loadSeq;
		m_decals = roadsx::RoadDecals();
		m_haveDecals = false;
		m_decalsVersion++;
		m_pending = false;
		m_firstSwitch = false;
		m_pendingRects.clear();
		m_lastTap.reset();
		m_cacheTap.reset();
		m_cacheOut.reset();
		if(m_footprint || m_clear)
		{
			m_footprint.reset();
			m_clear.reset();
			m_cutVersion++;
		}
		m_clearTap = nullptr;
		m_clearTapGen = 0xFFFFFFFFu;
		withdrawBridges();   // ticket 0808: the EXE's bridges as tapped until this load's first production
	}
	else if(m_bridges && (m_bridges->ox() != cutOx || m_bridges->oy() != cutOy))
	{
		const std::shared_ptr<const RoadBridgePlacer> b = m_bridges;
		publishBridges(b->input(), b->network());   // the same production in the field's new frame
	}
	memcpy(m_worldRect, worldRect, sizeof(m_worldRect));
	m_wrap = wrapX;   // a restored input keeps its recorded wrap (m_in is not read from a table then)
	m_haveWorld = true;
	m_dirty = true;
	if(newLoad)
	{
		say("roadlayout: field of load %u (%.0f x %.0f samples, wrap %d)%s; %s", loadSeq, worldRect[2] + 1.0f, worldRect[3] + 1.0f,
		    wrapX ? 1 : 0, m_frozen ? ": the restored input" : "", RoadLayoutChoiceText(m_choice).c_str());   // 4940: the layout
		readTable("a new load");
	}
}

bool RoadLayoutProducer::readTable(const char* why)
{
	if(m_frozen || !m_table)
		return false;
	rereads++;
	m_table(m_rec, m_tableGen, m_w, m_h);
	m_haveTable = m_w && m_h && m_rec.size() == (size_t)m_w * m_h;
	roadsx::MapIn now;
	if(!m_haveTable || !RoadMapFromTable(m_rec, m_w, m_h, m_wrap, now))
	{
		if(!m_rec.empty())
		{
			tableErrors++;
			if(loudDue(tableErrors))
				say("LOUD: roadlayout: the plot table holds %zu records for %ux%u (%s; %lld so far): the roads in force are kept",
				    m_rec.size(), m_w, m_h, why, (long long)tableErrors);
		}
		m_haveTable = false;
		return false;
	}
	bool same = now.width == m_in.width && now.height == m_in.height && now.wrapX == m_in.wrapX && now.plots.size() == m_in.plots.size();
	for(size_t i = 0; same && i < now.plots.size(); ++i)
		same = now.plots[i].band == m_in.plots[i].band && now.plots[i].city == m_in.plots[i].city;
	if(same)
		return false;
	uint32_t n = 0;
	for(const roadsx::PlotIn& p : now.plots)
		n += p.band != roadsx::kBandNone ? 1 : 0;
	m_in.width = now.width;
	m_in.height = now.height;
	m_in.wrapX = now.wrapX;
	m_in.plots.swap(now.plots);
	m_dirty = true;
	say("roadlayout: plot table read (%s): %ux%u generation %u, %u route plots (changed)", why, m_w, m_h, m_tableGen, n);
	return true;
}

bool RoadLayoutProducer::onBatch(const SrvFeed::Batch& b)
{
	bool reread = !m_haveTable ? b.width && b.height : b.snapshotGen != m_tableGen;
	const char* why = "a new snapshot";
	for(const SrvFeed::Event& e : b.events)
	{
		if(e.kind == SrvFeed::kSnapshot)
			reread = true;
		else if(e.kind == SrvFeed::kOverflow)
		{
			overflows++;
			reread = true;
			why = "an overflow";
		}
	}
	if(b.dropped > m_lastDropped)
	{
		if(!reread)
			overflows++;
		reread = true;
		why = "dropped events";
	}
	m_lastDropped = b.dropped;
	if(!m_haveWorld)
		return false;   // the first field's setWorld reads the table
	if(reread)
		return readTable(why);
	if(!b.changedPlots.empty() && m_haveTable)
	{   // a patch: the whole table again (a route's pieces depend on its neighbours; the bands are compared)
		plotChanges++;
		return readTable("changed plots");
	}
	return false;
}

void RoadLayoutProducer::Produce(const Job& j, Result& r)
{
	const double t0 = NowMs();
	r.st = roadsx::produce(j.in, j.style, r.net, r.decals);
	r.ms = NowMs() - t0;
	r.in = j.in;
	r.loadSeq = j.loadSeq;
	if(!r.st.ok())
		return;
	const double t1 = NowMs();
	std::shared_ptr<roadsx::RoadFootprint> fp(new roadsx::RoadFootprint());
	fp->setFrame(j.ox, j.oy);
	r.footprintSt = fp->build(j.in, r.decals, j.worldRect, j.margin);
	r.footprintMs = NowMs() - t1;
	if(r.footprintSt.ok())
		r.footprint = std::move(fp);
}

bool RoadLayoutProducer::refresh()
{
	bool changed = takeResult();
	{   // a clear mask finished on the worker
		std::unique_ptr<ClearResult> cr;
		{
			std::lock_guard<std::mutex> lk(m_mx);
			cr = std::move(m_clearResult);
		}
		takeClear(std::move(cr));
	}
	if(m_haveWorld && !m_haveTable && !m_frozen && m_table)
		readTable("the table was not there yet");   // until the first table arrives (the player's: after the fog's first read)
	if(!m_dirty || !m_haveWorld || m_in.plots.empty())
		return changed;
	m_dirty = false;
	std::unique_ptr<Job> job(new Job());
	job->in = m_in;
	job->style = m_style;
	job->loadSeq = m_loadSeq;
	memcpy(job->worldRect, m_worldRect, sizeof(job->worldRect));
	job->ox = m_ox;
	job->oy = m_oy;
	job->margin = m_opt.footprintMargin;
	if(m_opt.threaded)
	{
		startWorker();
		std::lock_guard<std::mutex> lk(m_mx);
		m_job = std::move(job);   // a newer input replaces a queued one
		m_cv.notify_all();
		return changed;
	}
	std::unique_ptr<Result> r(new Result());
	Produce(*job, *r);
	{
		std::lock_guard<std::mutex> lk(m_mx);
		m_result = std::move(r);
	}
	return takeResult() || changed;
}

void RoadLayoutProducer::startWorker()
{
	if(m_running)
		return;
	m_running = true;
	m_worker = std::thread([this]() { workerLoop(); });
}

void RoadLayoutProducer::workerLoop()
{
	for(;;)
	{
		std::unique_ptr<Job> job;
		std::unique_ptr<ClearJob> cj;
		{
			std::unique_lock<std::mutex> lk(m_mx);
			m_cv.wait(lk, [this]() { return m_stop || m_job || m_clearJob; });
			if(m_stop)
				return;
			job = std::move(m_job);
			if(!job)
				cj = std::move(m_clearJob);
			m_working = true;
		}
		if(cj)
		{   // the EXE's route cells to clear (a tap set's), then the next job
			std::unique_ptr<ClearResult> cr(new ClearResult());
			BuildClear(*cj, *cr);
			{
				std::lock_guard<std::mutex> lk(m_mx);
				if(m_clearResult)
					staleResults++;
				m_clearResult = std::move(cr);
				m_working = false;
			}
			m_cv.notify_all();
			continue;
		}
		std::unique_ptr<Result> r(new Result());
		Produce(*job, *r);
		{
			std::lock_guard<std::mutex> lk(m_mx);
			if(m_result)
				staleResults++;
			m_result = std::move(r);
			m_working = false;
		}
		m_cv.notify_all();
	}
}

bool RoadLayoutProducer::takeResult()
{
	std::unique_ptr<Result> r;
	{
		std::lock_guard<std::mutex> lk(m_mx);
		r = std::move(m_result);
	}
	if(!r)
		return false;
	if(r->loadSeq != m_loadSeq)
	{
		staleResults++;
		return false;
	}
	lastProduceMs = r->ms;
	if(!r->st.ok())
	{
		productionErrors++;
		const std::string e = r->st.str();
		if(loudDue(productionErrors) || e != m_lastErr)
			say("LOUD: roadlayout: the roads were not laid out (%s); the %zu road decals in force are kept (%lld so far)", e.c_str(),
			    m_decals.decals.size(), (long long)productionErrors);
		m_lastErr = e;
		return false;
	}
	roadsx::RoadChange ch;
	const c5r::Status cs = roadsx::roadChange(m_decals, r->decals, ch);
	if(!cs.ok())
	{
		productionErrors++;
		say("LOUD: roadlayout: the change of a production was not computed (%s); every road decal's tile is re-baked", cs.str().c_str());
		for(const c5r::decal::Decal& d : m_decals.decals)
		{
			c5r::decal::DecalRect rr;
			memcpy(rr.v, d.rect, sizeof(rr.v));
			ch.rects.push_back(rr);
		}
		for(const c5r::decal::Decal& d : r->decals.decals)
		{
			c5r::decal::DecalRect rr;
			memcpy(rr.v, d.rect, sizeof(rr.v));
			ch.rects.push_back(rr);
		}
	}
	const bool first = !m_haveDecals;
	const bool changed = first || !ch.rects.empty();
	lastRoutePlots = r->net.routePlots;
	lastRejected = r->net.rejected;
	// the footprint for the trees, replaced only when its cells changed
	lastFootprintMs = r->footprintMs;
	if(!r->footprint)
	{
		footprintErrors++;
		if(loudDue(footprintErrors))
			say("LOUD: roadlayout: the roads' cut footprint was not built (%s); the one in force is kept (%lld so far)",
			    r->footprintSt.str().c_str(), (long long)footprintErrors);
	}
	else if(!m_footprint || m_footprint->hash() != r->footprint->hash())
	{
		r->footprint->setGeneration(++m_cutVersion);
		footprints++;
		say("roadlayout: footprint %llu: %llu cells of 2 x 2 samples under the roads (%u x %u cells, %.2f ms)", (unsigned long long)m_cutVersion,
		    (unsigned long long)r->footprint->cellsSet(), r->footprint->width(), r->footprint->height(), r->footprintMs);
		m_footprint = std::move(r->footprint);
	}
	if(changed || !m_bridges)
		publishBridges(r->in, r->net);   // ticket 0808: the EXE's bridges onto this production's river crossings
	if(!changed)
		return false;
	m_decals = std::move(r->decals);
	m_haveDecals = true;
	m_decalsVersion++;
	productions++;
	// the change: every decal that differs; the load's first production also replaces the tap's route curves the residency
	// baked until now (takeDecalChanges adds their rectangles from the newest tap set)
	m_pendingRects.insert(m_pendingRects.end(), ch.rects.begin(), ch.rects.end());
	m_pending = true;
	if(first)
		m_firstSwitch = true;
	say("roadlayout: %u route plots -> %zu decals (%zu vertices), %u edges left out by the ring rule, %zu decals changed on %zu plots, "
	    "%.2f ms on the %s",
	    r->net.routePlots, m_decals.decals.size(), m_decals.vertices.size(), r->net.rejected, ch.rects.size(), ch.plots.size(), r->ms,
	    m_opt.threaded ? "worker" : "render thread");
	return true;
}

void RoadLayoutProducer::BuildClear(const ClearJob& j, ClearResult& r)
{
	const double t0 = NowMs();
	r.mask.reset(new StockRouteClear());
	r.mask->setFrame(j.ox, j.oy);
	r.st = r.mask->build(*j.tap, j.plots, j.w, j.h, j.band);
	r.ms = NowMs() - t0;
	r.tapGen = j.tap->generation;
	r.loadSeq = j.loadSeq;
}

void RoadLayoutProducer::takeClear(std::unique_ptr<ClearResult> r)
{
	if(!r)
		return;
	if(r->loadSeq != m_loadSeq)
	{
		staleResults++;
		return;
	}
	lastClearMs = r->ms;
	if(!r->st.ok())
	{
		clearErrors++;
		if(loudDue(clearErrors))
			say("LOUD: roadlayout: the EXE's route cells were not marked (%s); the clear mask in force is kept (%lld so far)",
			    r->st.str().c_str(), (long long)clearErrors);
		return;
	}
	const StockRouteClear& c = *r->mask;
	r->mask->setGeneration(++m_cutVersion);
	clearBuilds++;
	say("roadlayout: tap set generation %u: the EXE's route cells to clear: %llu cells on %u plots from %u route curves; %llu cells "
	    "on %u plots kept (an improvement or a city), %.2f ms on the %s",
	    r->tapGen, (unsigned long long)c.cellsCleared, c.plotsCleared, c.routeCurves, (unsigned long long)c.cellsKept, c.plotsKept, r->ms,
	    m_opt.threaded ? "worker" : "render thread");
	m_clear = std::move(r->mask);
}

void RoadLayoutProducer::publishBridges(const roadsx::MapIn& m, const roadsx::Network& n)
{
	m_bridges = std::make_shared<const RoadBridgePlacer>(m, n, m_style, m_ox, m_oy, (uint64_t)(++bridgePlacers));
	SetBridgePlacer(m_bridges);
	say("roadlayout: bridges: placer %lld in force (the EXE's road and rail bridges onto this layout's river crossings; %u route "
	    "plots, frame %.1f %.1f)",
	    (long long)bridgePlacers, n.routePlots, m_ox, m_oy);
}

void RoadLayoutProducer::withdrawBridges()
{
	if(!m_bridges)
		return;
	if(GetBridgePlacer() == m_bridges)
		SetBridgePlacer(nullptr);
	m_bridges.reset();
}

bool RoadLayoutProducer::busy() const
{
	std::lock_guard<std::mutex> lk(m_mx);
	return m_job != nullptr || m_result != nullptr || m_clearJob != nullptr || m_clearResult != nullptr || m_working;
}

bool RoadLayoutProducer::settle(double ms)
{
	const double t0 = NowMs();
	refresh();   // an input not yet handed to the worker goes now
	for(;;)
	{
		{
			std::unique_lock<std::mutex> lk(m_mx);
			if(!m_job && !m_clearJob && !m_working)
				break;
			m_cv.wait_for(lk, std::chrono::milliseconds(20));
		}
		if(NowMs() - t0 > ms)
			return false;
	}
	refresh();
	return true;
}

std::shared_ptr<const c5r::decal::DecalSet> RoadLayoutProducer::composeDecals(const std::shared_ptr<const c5r::decal::DecalSet>& tap)
{
	composes++;
	if(!tap)
		return tap;
	m_lastTap = tap;
	if(!m_haveDecals)
	{   // no production in force yet (the first one of a load runs on the worker): the tap's set as it is, stock's roads
		// and stock's cut lanes, rather than a moment without roads
		composesBeforeFirst++;
		return tap;
	}
	// the EXE's route cells to clear in the trees' cut (MC #7664), once per tap set and load
	if((tap.get() != m_clearTap || tap->generation != m_clearTapGen || m_clearLoad != m_loadSeq) && m_haveTable)
	{   // built on the worker (about 140 ms on the huge save: never on the render thread when threaded)
		std::unique_ptr<ClearJob> j(new ClearJob());
		j->tap = tap;
		j->plots = m_rec;
		j->w = m_w;
		j->h = m_h;
		j->band = m_opt.stockBand;
		j->ox = m_ox;
		j->oy = m_oy;
		j->loadSeq = m_loadSeq;
		m_clearTap = tap.get();
		m_clearTapGen = tap->generation;
		m_clearLoad = m_loadSeq;
		if(m_opt.threaded)
		{
			startWorker();
			std::lock_guard<std::mutex> lk(m_mx);
			m_clearJob = std::move(j);   // a newer tap set replaces a queued one
			m_cv.notify_all();
		}
		else
		{
			std::unique_ptr<ClearResult> r(new ClearResult());
			BuildClear(*j, *r);
			takeClear(std::move(r));
		}
	}
	if(m_cacheTap == tap && m_cacheVersion == m_decalsVersion && m_cacheOut)
	{
		composeCached++;
		return m_cacheOut;
	}
	const double t0 = NowMs();
	std::shared_ptr<c5r::decal::DecalSet> out(new c5r::decal::DecalSet());
	roadsx::ComposeStats cs;
	const c5r::Status st = roadsx::composeRoadSet(*tap, m_opt.removeTapRoutes, m_decals, *out, &cs);
	lastComposeMs = NowMs() - t0;
	if(!st.ok())
	{
		composeErrors++;
		const std::string e = st.str();
		if(loudDue(composeErrors) || e != m_lastErr)
			say("LOUD: roadlayout: the road decals were not joined to the tap's set (generation %u): %s; the tap's set as it is (%lld so far)",
			    tap->generation, e.c_str(), (long long)composeErrors);
		m_lastErr = e;
		return tap;
	}
	say("roadlayout: tap set generation %u: %u decals, %u route curves dropped, %u road decals added (%u vertices), %.2f ms",
	    tap->generation, cs.tapDecals, cs.tapRemoved, cs.roadDecals, cs.roadVertices, lastComposeMs);
	m_cacheTap = tap;
	m_cacheVersion = m_decalsVersion;
	m_cacheOut = out;
	return out;
}

bool RoadLayoutProducer::takeDecalChanges(std::vector<RoadsChange>& out)
{
	if(!m_pending)
		return false;
	RoadsChange c;
	c.generation = kRoadDecalGenBase + (++m_gen);
	c.previousGeneration = c.generation - 1;
	memcpy(c.worldRect, m_worldRect, sizeof(c.worldRect));
	c.rects.swap(m_pendingRects);
	if(m_firstSwitch && m_lastTap)
	{   // the tap's route curves leave the set with the first production: their tiles are baked again too
		for(const c5r::decal::Decal& d : m_lastTap->decals)
			if(roadsx::isTapRouteCurve(*m_lastTap, d))
			{
				c5r::decal::DecalRect rr;
				memcpy(rr.v, d.rect, sizeof(rr.v));
				c.rects.push_back(rr);
			}
	}
	m_firstSwitch = false;
	// the area tiles our decals reach (composeRoadSet marks them): every tile of a changed rectangle
	std::set<std::pair<int, int>> tiles;
	for(const c5r::decal::DecalRect& r : c.rects)
	{
		const int x0 = (int)std::floor(r.v[0] / 64.0f), x1 = (int)std::floor(r.v[2] / 64.0f);
		const int y0 = (int)std::floor(r.v[1] / 64.0f), y1 = (int)std::floor(r.v[3] / 64.0f);
		for(int y = (std::max)(0, y0); y <= y1; ++y)
			for(int x = (std::max)(0, x0); x <= x1; ++x)
				tiles.insert(std::make_pair(x, y));
	}
	for(const auto& t : tiles)
	{
		c5r::decal::TileOrigin o;
		o.x = t.first;
		o.y = t.second;
		c.areaTiles.push_back(o);
	}
	m_pending = false;
	out.push_back(std::move(c));
	decalChangesMade++;
	return true;
}

bool RoadLayoutProducer::spanState(std::string& text, std::string& why) const
{
	text.clear();
	if(!m_haveDecals)
		return why = "no road layout in force at the span's first frame (no plot table or no production yet)", false;
	text = RoadSpanText(m_in);
	return true;
}

bool RoadLayoutProducer::restoreSpanInput(const std::string& text, std::string& why)
{
	roadsx::MapIn in;
	if(!ParseRoadSpanText(text, in, why))
		return false;
	m_in = std::move(in);
	m_frozen = true;
	m_haveTable = false;
	m_dirty = true;
	uint32_t n = 0;
	for(const roadsx::PlotIn& p : m_in.plots)
		n += p.band != roadsx::kBandNone ? 1 : 0;
	say("roadlayout: the recorded input restored: %ux%u, %u route plots", m_in.width, m_in.height, n);
	return true;
}

void RoadLayoutProducer::totalsLine(char* out, size_t n) const
{
	if(!out || !n)
		return;
	_snprintf_s(out, n, _TRUNCATE,
	            "roadlayout totals: route_plots=%u decals=%zu loads=%lld rereads=%lld changes=%lld overflows=%lld productions=%lld "
	            "prod_err=%lld composes=%lld cached=%lld compose_err=%lld decal_changes=%lld table_err=%lld footprints=%lld "
	            "footprint_err=%lld footprint_cells=%llu clear_builds=%lld clear_err=%lld cells_cleared=%llu cells_kept=%llu "
	            "last_produce_ms=%.2f last_compose_ms=%.2f bridge_placers=%lld",
	            lastRoutePlots, m_decals.decals.size(), (long long)loads, (long long)rereads, (long long)plotChanges, (long long)overflows,
	            (long long)productions, (long long)productionErrors, (long long)composes, (long long)composeCached, (long long)composeErrors,
	            (long long)decalChangesMade, (long long)tableErrors, (long long)footprints, (long long)footprintErrors,
	            (unsigned long long)(m_footprint ? m_footprint->cellsSet() : 0), (long long)clearBuilds, (long long)clearErrors,
	            (unsigned long long)(m_clear ? m_clear->cellsCleared : 0), (unsigned long long)(m_clear ? m_clear->cellsKept : 0),
	            lastProduceMs, lastComposeMs, (long long)bridgePlacers);
}

}  // namespace srvextras
