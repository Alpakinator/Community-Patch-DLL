// srv_liveterrain_pages.cpp - BuildPageWithWindows (srv_liveterrain_pages.h; epic 0465 follow-up, ticket 0587).
// Written 2026-11-19 for this repository (GPL-3.0-or-later).
#include "srv_liveterrain_pages.h"

#include <string>

namespace srvrender { namespace liveterrain {

namespace {
// the scratch grid: 16 x 16 plots = one page's 1024 x 1024 samples
const uint32_t kScratchPlots = c5r::bake::kHeightPageSize / c5r::heightfield::kSamplesPerPlot;

// The field's sample read at scratch index i (0..1023) on one axis of page origin o (= index * 896): page (0, 0) of the
// scratch reads its sample (t - 64) mod 1024 at page position t (heightpage.h), the field's page reads (o - 64 + t) mod
// n; so scratch i holds the field's sample at t = (i + 64) mod 1024.
uint32_t FieldIndex(uint32_t i, int64_t o, uint32_t n)
{
	const int64_t t = (int64_t)((i + c5r::bake::kHeightPageMargin) % c5r::bake::kHeightPageSize);
	int64_t s = (o - (int64_t)c5r::bake::kHeightPageMargin + t) % (int64_t)n;
	if(s < 0)
		s += n;
	return (uint32_t)s;
}
}  // namespace

c5r::Status BuildPageWithWindows(const c5r::heightfield::HeightGrid& g, c5r::bake::HeightPageIndex p, uint32_t mips,
                                 const std::vector<const std::vector<Rect>*>& windows, c5r::bake::HeightPage& out)
{
	out = c5r::bake::HeightPage();
	uint32_t pagesX = 0, pagesY = 0;
	C5R_TRY(c5r::bake::heightPageCount(g, pagesX, pagesY));
	if(g.h.size() != (size_t)g.samplesX * g.samplesY)
		return c5r::Status::fail(c5r::Err::BadLayout, "height grid holds " + std::to_string(g.h.size()) + " samples, not " +
		                                                  std::to_string((size_t)g.samplesX * g.samplesY));
	if(p.px >= pagesX || p.py >= pagesY)
		return c5r::Status::fail(c5r::Err::NotFound, "page (" + std::to_string(p.px) + ", " + std::to_string(p.py) + ") past the " +
		                                                 std::to_string(pagesX) + " x " + std::to_string(pagesY) + " pages");
	for(const std::vector<Rect>* w : windows)   // ApplyWindow's checks, every one before any work
	{
		if(!w)
			return c5r::Status::fail(c5r::Err::BadLayout, "a null window");
		for(size_t k = 0; k < w->size(); ++k)
		{
			const Rect& r = (*w)[k];
			if(!r.w || !r.h || (uint64_t)r.x0 + r.w > g.samplesX || (uint64_t)r.y0 + r.h > g.samplesY)
				return c5r::Status::fail(c5r::Err::BadLayout, "rectangle " + std::to_string(k) + " (" + std::to_string(r.x0) + "," +
				                                                  std::to_string(r.y0) + " " + std::to_string(r.w) + "x" +
				                                                  std::to_string(r.h) + ") outside the field");
			if(r.heights.size() != (size_t)r.w * r.h)
				return c5r::Status::fail(c5r::Err::BadLayout, "rectangle " + std::to_string(k) + ": heights are not w x h");
		}
	}
	const uint32_t N = c5r::bake::kHeightPageSize;
	const int64_t ox = (int64_t)p.px * c5r::bake::kHeightPageStep, oy = (int64_t)p.py * c5r::bake::kHeightPageStep;
	std::vector<uint32_t> fx(N), fy(N);
	for(uint32_t i = 0; i < N; ++i)
	{
		fx[i] = FieldIndex(i, ox, g.samplesX);
		fy[i] = FieldIndex(i, oy, g.samplesY);
	}
	c5r::heightfield::HeightGrid s;
	s.mapW = s.mapH = kScratchPlots;
	s.samplesX = s.samplesY = N;
	s.wrapX = g.wrapX;
	s.h.resize((size_t)N * N);
	for(uint32_t y = 0; y < N; ++y)
	{
		const uint16_t* src = g.h.data() + (size_t)fy[y] * g.samplesX;
		uint16_t* dst = s.h.data() + (size_t)y * N;
		for(uint32_t x = 0; x < N; ++x)
			dst[x] = src[fx[x]];
	}
	for(const std::vector<Rect>* w : windows)   // the windows in order, as ApplyWindow writes them (a later one wins)
		for(const Rect& r : *w)
			for(uint32_t y = 0; y < N; ++y)
			{
				if(fy[y] < r.y0 || fy[y] >= r.y0 + r.h)
					continue;
				const uint16_t* src = r.heights.data() + (size_t)(fy[y] - r.y0) * r.w;
				uint16_t* dst = s.h.data() + (size_t)y * N;
				for(uint32_t x = 0; x < N; ++x)
					if(fx[x] >= r.x0 && fx[x] < r.x0 + r.w)
						dst[x] = src[fx[x] - r.x0];
			}
	c5r::bake::HeightPageIndex zero;
	C5R_TRY(c5r::bake::buildHeightPage(s, zero, mips, out));
	out.index = p;
	return c5r::Status::Ok();
}

}}  // namespace srvrender::liveterrain
