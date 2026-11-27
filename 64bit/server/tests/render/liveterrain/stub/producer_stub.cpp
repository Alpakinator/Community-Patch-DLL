// producer_stub.cpp - TEST STUB of 4903's region producer (see stub/c5r/liveterrain/producer.h). Ticket 4907.
// Written 2026-11-18 by w5-4907 for this repository (GPL-3.0-or-later). Not the producer: plot (x, y) is the sample box
// 64 x .. 64 x + 63, 64 (y + 2) .. 64 (y + 2) + 63 (the two margin rows of plots above), the own height a flat value per
// plot kind, w = 1 on the changed plots' boxes and falling linearly to 0 over kRingSamples around them (Chebyshev).
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
#include <cmath>

namespace c5r {
namespace liveterrain {

int Producer::s_createDelayMs = 0;
int Producer::s_changeDelayMs = 0;
Err Producer::s_failCreate = Err::Ok;
Err Producer::s_failChange = Err::Ok;
ChangeResult Producer::s_landWaterCounts;

Status PieceLibrary::open(const std::wstring& path, std::shared_ptr<const PieceLibrary>& out)
{
	FILE* f = nullptr;
	if(_wfopen_s(&f, path.c_str(), L"rb") || !f)
		return Status::fail(Err::Io, "piece library not readable");
	uint32_t v = 0;
	const size_t n = fread(&v, 1, 4, f);
	fclose(f);
	if(n != 4)
		return Status::fail(Err::Truncated, "piece library shorter than 4 bytes");
	if(v != 20)
		return Status::fail(Err::BadVersion, "piece library version " + std::to_string(v) + " (20 wanted)");
	std::shared_ptr<PieceLibrary> l = std::make_shared<PieceLibrary>();
	l->m_version = v;
	out = l;
	return Status::Ok();
}

Status MapFrame::fromConstantsBlock(const uint8_t* bytes, size_t n, MapFrame& out)
{
	if(!bytes || n < 880)
		return Status::fail(Err::Truncated, "constants block of " + std::to_string(n) + " bytes (880 wanted)");
	out = MapFrame();
	int32_t wrap = 0, gw = 0, gh = 0;
	memcpy(&wrap, bytes, 4);
	memcpy(&gw, bytes + (0xD91264 - 0xD90FBC), 4);
	memcpy(&gh, bytes + (0xD91268 - 0xD90FBC), 4);
	if(gw <= 0 || gh <= 4)
		return Status::fail(Err::BadLayout, "grid size in the block " + std::to_string(gw) + " x " + std::to_string(gh));
	out.wrapX = wrap != 0;
	out.plotsW = gw;
	out.plotsH = gh - 4;
	return Status::Ok();
}

uint64_t deriveSeed(const MapFrame& frame, const PlotTable& t)
{
	uint64_t h = 0xcbf29ce484222325ull;
	auto mix = [&h](uint64_t v) {
		for(int i = 0; i < 8; ++i)
		{
			h ^= (v >> (8 * i)) & 0xFF;
			h *= 0x100000001b3ull;
		}
	};
	mix((uint64_t)frame.plotsW);
	mix((uint64_t)frame.plotsH);
	for(const PlotInfo& p : t.plots)
		mix((uint64_t)(uint32_t)p.base.terrain | ((uint64_t)p.base.hills << 32) | ((uint64_t)p.base.mountain << 33));
	return h;
}

const char* waitReasonName(WaitReason r)
{
	static const char* n[] = {"None", "NoChange", "Water", "Lake", "RiverEdge", "Marsh", "Seam", "TooLarge"};
	const int i = (int)r;
	return i >= 0 && i < 8 ? n[i] : "?";
}

Status Producer::create(std::shared_ptr<const PieceLibrary> lib, const MapFrame& frame, const PlotTable& atLoad, const TapView& tap, uint64_t seed,
                        Producer& out)
{
	(void)seed;
	if(s_createDelayMs)
		Sleep((DWORD)s_createDelayMs);
	if(s_failCreate != Err::Ok)
		return Status::fail(s_failCreate, "stub: create fails on purpose");
	if(!lib || lib->version() != 20)
		return Status::fail(Err::BadVersion, "library");
	if(atLoad.plots.size() != (size_t)frame.plotsW * frame.plotsH || atLoad.w != frame.plotsW || atLoad.h != frame.plotsH)
		return Status::fail(Err::BadLayout, "plot table size not W * H");
	if(!tap.heights || !tap.materials || tap.samplesX != frame.samplesX() || tap.samplesY != frame.samplesY())
		return Status::fail(Err::BadLayout, "tap size not 64 W x 64 (H + 4)");
	out = Producer();
	out.m_frame = frame;
	out.m_cur = atLoad;
	out.m_sx = tap.samplesX;
	out.m_sy = tap.samplesY;
	out.m_h.assign(tap.heights, tap.heights + (size_t)tap.samplesX * tap.samplesY);
	out.m_m.assign(tap.materials, tap.materials + (size_t)tap.samplesX * tap.samplesY);
	out.m_ok = true;
	return Status::Ok();
}

namespace {
// STUB (0613): the stub's unknown wonder, drawn as Fuji by the stock rule
bool UnknownWonder(const PlotInfo& p)
{
	return p.base.naturalWonder && p.base.featureType.compare(0, 8, "UNKNOWN_") == 0;
}
bool Cell(const PlotInfo& a, bool aNeutral, const PlotInfo& b, bool bNeutral)
{
	return a.base.terrain != b.base.terrain || a.base.hills != b.base.hills || a.base.mountain != b.base.mountain ||
	       a.base.featureType != b.base.featureType || a.base.naturalWonder != b.base.naturalWonder ||
	       (UnknownWonder(a) && !aNeutral) != (UnknownWonder(b) && !bNeutral);
}
uint16_t OwnHeight(const PlotInfo& p, bool neutral)
{
	if(UnknownWonder(p) && !neutral)
		return 40000;   // STUB (0613): the EXE's Fuji
	return p.base.mountain ? 30000 : p.base.hills ? 20000 : 15330;
}
}  // namespace

Producer::Options Producer::s_lastOptions;

Status Producer::change(const PlotTable& after, ChangeResult& out, const Options& opt)
{
	s_lastOptions = opt;   // STUB (0717)
	m_neutralNext = opt.neutralUnknownWonders;   // STUB (0613)
	const Status st = change(after, out);
	m_neutralNext = false;
	return st;
}

Status Producer::change(const PlotTable& after, ChangeResult& out)
{
	const bool neutral = m_neutralNext;   // STUB (0613): false through this two-argument call
	out = ChangeResult();
	{   // STUB seam (MC #9538 items 1 and 2)
		const ChangeResult& c = s_landWaterCounts;
		out.landWater = c.landWater;
		out.coastLinesRedrawn = c.coastLinesRedrawn;
		out.coastLinesBeyondWindow = c.coastLinesBeyondWindow;
		out.coastLinesReloadMoves = c.coastLinesReloadMoves;
		out.samplesWonderKept = c.samplesWonderKept;
		out.coastDegenerate = c.coastDegenerate;
		out.stripTrianglesRefused = c.stripTrianglesRefused;
	}
	const LARGE_INTEGER t0 = [] { LARGE_INTEGER c; QueryPerformanceCounter(&c); return c; }();
	if(s_changeDelayMs)
		Sleep((DWORD)s_changeDelayMs);
	if(s_failChange != Err::Ok)
		return Status::fail(s_failChange, "stub: change fails on purpose");
	if(!m_ok)
		return Status::fail(Err::NotFound, "not created");
	if(after.w != m_frame.plotsW || after.h != m_frame.plotsH || after.plots.size() != m_cur.plots.size())
		return Status::fail(Err::BadLayout, "plot table size not W * H");
	const int W = m_frame.plotsW, H = m_frame.plotsH;
	std::vector<int> changed;
	for(int i = 0; i < W * H; ++i)
		if(Cell(m_cur.plots[i], m_neutral, after.plots[i], neutral))
		{
			changed.push_back(i);
			out.changedPlots.push_back(std::make_pair(i % W, i / W));
		}
	auto wait = [&out](WaitReason r, const std::string& t) {
		out.live = false;
		out.reason = r;
		out.reasonText = t;
		out.window.clear();
		return Status::Ok();
	};
	if(changed.empty())
		return wait(WaitReason::NoChange, "no pattern cell changed");
	if((int)changed.size() > kMaxWindowPlots)
		return wait(WaitReason::TooLarge, std::to_string(changed.size()) + " plots");
	// the window's plots: the changed ones and their 8 neighbours (the ring reaches one plot)
	for(int i : changed)
		for(int dy = -1; dy <= 1; ++dy)
			for(int dx = -1; dx <= 1; ++dx)
			{
				int x = i % W + dx;
				const int y = i / W + dy;
				if(y < 0 || y >= H)
					continue;
				if(x < 0 || x >= W)
				{
					if(!m_frame.wrapX)
						continue;
					x = (x + W) % W;
				}
				const PlotInfo& p = after.plots[(size_t)y * W + x];
				const std::string at = std::to_string(x) + "," + std::to_string(y);
				if(p.base.terrain == 5 || p.base.terrain == 6)
					return wait(WaitReason::Water, "plot " + at + " is water");
				if(p.lake)
					return wait(WaitReason::Lake, "plot " + at + " is a lake");
				if(p.riverEdges)
					return wait(WaitReason::RiverEdge, "plot " + at + " has a river edge");
				if(m_frame.wrapX && (x < 2 || x >= W - 2))
					return wait(WaitReason::Seam, "plot " + at + " is within 2 of the seam");
			}
	// w over the whole field (the stub is small-map only), then the window as one rectangle per seam side
	const int SX = m_sx, SY = m_sy;
	std::vector<float> w((size_t)SX * SY, 0.0f);
	for(int i : changed)
	{
		const int bx = (i % W) * 64, by = (i / W + 2) * 64;
		const int R = kRingSamples;
		for(int y = by - R; y < by + 64 + R; ++y)
		{
			if(y < 0 || y >= SY)
				continue;
			for(int x = bx - R; x < bx + 64 + R; ++x)
			{
				int xx = x;
				if(xx < 0 || xx >= SX)
				{
					if(!m_frame.wrapX)
						continue;
					xx = (xx + SX) % SX;
				}
				const int dx = x < bx ? bx - x : x >= bx + 64 ? x - (bx + 63) : 0;
				const int dy = y < by ? by - y : y >= by + 64 ? y - (by + 63) : 0;
				const int d = std::max(dx, dy);
				const float v = d == 0 ? 1.0f : std::max(0.0f, 1.0f - (float)d / (float)(R + 1));
				float& cur = w[(size_t)y * SX + xx];
				cur = std::max(cur, v);
			}
		}
	}
	int x0 = SX, y0 = SY, x1 = -1, y1 = -1;
	for(int y = 0; y < SY; ++y)
		for(int x = 0; x < SX; ++x)
			if(w[(size_t)y * SX + x] > 0)
			{
				x0 = std::min(x0, x);
				y0 = std::min(y0, y);
				x1 = std::max(x1, x);
				y1 = std::max(y1, y);
			}
	if(x1 < 0)
		return wait(WaitReason::NoChange, "empty window");
	WindowRect r;
	r.x0 = x0;
	r.y0 = y0;
	r.w = x1 - x0 + 1;
	r.h = y1 - y0 + 1;
	r.heights.resize((size_t)r.w * r.h);
	r.materials.resize((size_t)r.w * r.h);
	for(int y = 0; y < r.h; ++y)
		for(int x = 0; x < r.w; ++x)
		{
			const size_t gi = (size_t)(r.y0 + y) * SX + r.x0 + x, ri = (size_t)y * r.w + x;
			const float wv = w[gi];
			// the own value of the plot the sample lies in (the stub's box rule)
			const int px = (int)(r.x0 + x) / 64, py = (int)(r.y0 + y) / 64 - 2;
			uint16_t own = m_h[gi];
			uint8_t ownM = m_m[gi];
			if(py >= 0 && py < H)
			{
				const PlotInfo& p = after.plots[(size_t)py * W + px];
				own = OwnHeight(p, neutral);
				ownM = (uint8_t)(p.base.terrain + 1);
			}
			if(wv > 0)
			{
				const float hv = (float)m_h[gi] + wv * ((float)own - (float)m_h[gi]);
				m_h[gi] = (uint16_t)std::lround(hv);
				if(wv >= 0.5f)
					m_m[gi] = ownM;
				if(wv >= 1.0f)
					out.samplesFull++;
				else
					out.samplesRing++;
			}
			r.heights[ri] = m_h[gi];
			r.materials[ri] = m_m[gi];
		}
	out.samplesD = out.samplesFull;
	out.window.push_back(std::move(r));
	out.live = true;
	m_cur = after;
	m_neutral = neutral;   // STUB (0613)
	LARGE_INTEGER t1, f;
	QueryPerformanceCounter(&t1);
	QueryPerformanceFrequency(&f);
	out.ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart;
	return Status::Ok();
}

}  // namespace liveterrain
}  // namespace c5r
