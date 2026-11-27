// srv_liveterrain_derived.cpp - the server-drawn trees on the new ground after a live terrain change (epic 0465, ticket
// 4904). Contract and what follows by itself (the decals): srv_liveterrain_derived.h. Written 2026-11-19 by w7-4904 for
// this repository (GPL-3.0-or-later). Free of the server's logging so that the offline tests link it alone.
#include "srv_liveterrain_derived.h"

#include <chrono>
#include <cmath>

#include "forest/srv_forest_scene.h"

namespace srvrender {
namespace liveterrain {

size_t DerivedAfterWindow(SceneForest* forest, const std::vector<SampleBox>& boxes)
{
	if(!forest || boxes.empty())
		return 0;
	forest->expectWindows(boxes);
	return boxes.size();
}

namespace {

// The column and row the build reads a tree's height at (placement.cpp buildPlot: bx = (ox + wx) / sx, truncated), from
// the instance's world position, which the build stores before any cull. False: the build read no sample (not finite).
bool TreeSample(const c5r::heightfield::HeightGrid& g, const c5r::forest::Instance& in, int32_t& c, int32_t& r)
{
	const float bx = (g.ox + in.x) / g.sx, by = (g.oy + in.y) / g.sy;
	if(!std::isfinite(bx) || !std::isfinite(by) || std::fabs(bx) >= 1e9f || std::fabs(by) >= 1e9f)
		return false;
	c = static_cast<int32_t>(bx);
	r = static_cast<int32_t>(by);
	return true;
}

bool InBox(const SampleBox& b, int32_t c, int32_t r, int32_t W, bool wrap)
{
	const int32_t m = kTreeReadMargin;
	if(r < b.y0 - m || r > b.y1 + m)
		return false;
	if(c >= b.x0 - m && c <= b.x1 + m)
		return true;
	if(!wrap || W <= 0)
		return false;
	// the stencil and the height read wrap in x: the column taken into 0..W-1, then its copies one map-width away
	const int32_t cw = ((c % W) + W) % W;
	for(int32_t k = -1; k <= 1; ++k)
	{
		const int32_t cc = cw + k * W;
		if(cc >= b.x0 - m && cc <= b.x1 + m)
			return true;
	}
	return false;
}

}  // namespace

bool BlocksReadBoxes(const c5r::heightfield::HeightGrid& g, const c5r::forest::PlotBlocks& b, const std::vector<SampleBox>& boxes)
{
	if(boxes.empty())
		return false;
	const int32_t W = static_cast<int32_t>(g.samplesX);
	for(const c5r::forest::Instance& in : b.instances)
	{
		int32_t c = 0, r = 0;
		if(!TreeSample(g, in, c, r))
			continue;
		for(const SampleBox& box : boxes)
			if(InBox(box, c, r, W, g.wrapX))
				return true;
	}
	return false;
}

ReheightStats ReheightBuilt(c5r::forest::Placement& place, const c5r::heightfield::HeightGrid& g, const c5r::forest::CutSource& cut,
                            const std::vector<BuiltPlot>& built, const std::vector<SampleBox>& boxes, const RebuiltFn& onRebuilt)
{
	ReheightStats s;
	const auto t0 = std::chrono::steady_clock::now();
	const uint64_t gen = cut.generation();
	for(const BuiltPlot& p : built)
	{
		s.builtSeen++;
		bool touch = p.cutGeneration != gen;
		if(!touch)
		{
			// the cached blocks (built before the window was written; the positions do not depend on the heights)
			const c5r::forest::PlotBlocks* pb = nullptr;
			const c5r::Status st = place.blocks(g, cut, p.x, p.y, &pb);
			touch = !st.ok() || !pb || BlocksReadBoxes(g, *pb, boxes);
		}
		if(!touch)
			continue;
		s.touched++;
		place.invalidate(p.x, p.y);
		const c5r::forest::PlotBlocks* nb = nullptr;
		const c5r::Status st = place.blocks(g, cut, p.x, p.y, &nb);
		if(!st.ok() || !nb)
		{
			s.failed++;
			if(s.firstError.empty())
				s.firstError = "plot " + std::to_string(p.x) + "," + std::to_string(p.y) + ": " + (st.ok() ? std::string("no blocks") : st.str());
			if(onRebuilt)
				onRebuilt(p.x, p.y, place.record(p.x, p.y), nullptr);
			continue;
		}
		s.rebuilt++;
		if(onRebuilt)
			onRebuilt(p.x, p.y, place.record(p.x, p.y), nb);
	}
	s.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	return s;
}

}  // namespace liveterrain
}  // namespace srvrender
