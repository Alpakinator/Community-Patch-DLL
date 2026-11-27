// units/garrison_perimeter.cpp - GarrisonVisible's look (d), "perimeter" (issue 4983 stage d). See garrison_perimeter.h.
// Written 2026-11-23 for this repository (GPL-3.0-or-later).
#include "units/garrison_perimeter.h"

#include <algorithm>
#include <cmath>

namespace c5r {
namespace extras {
namespace units {

namespace cu = c5r::units;

static bool fin(float v) { return std::isfinite(v); }

Status latticeOf(const heightfield::HeightGrid& g, Lattice& out)
{
	out = Lattice();
	float x0 = 0, y0 = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0;
	Status s = cu::plotCentre(g, 0, 0, x0, y0);
	if(!s.ok())
		return s;
	s = cu::plotCentre(g, 1, 0, x1, y1);
	if(!s.ok())
		return s;
	s = cu::plotCentre(g, 0, 1, x2, y2);
	if(!s.ok())
		return s;
	out.ex = x1 - x0;
	out.ey = y1 - y0;
	out.ax = x2 - x0;
	out.ay = y2 - y0;
	const float cross = out.ex * out.ay - out.ey * out.ax;
	if(!fin(cross) || std::fabs(cross) < 1e-6f)
		return Status::fail(Err::BadLayout, "perimeter: the plot lattice is degenerate");
	return Status::Ok();
}

void neighbourCentres(float cx, float cy, const Lattice& l, float nx[kPerimeterEdges], float ny[kPerimeterEdges])
{
	const float dx[kPerimeterEdges] = {l.ex, l.ax, l.ax - l.ex, -l.ex, -l.ax, l.ex - l.ax};
	const float dy[kPerimeterEdges] = {l.ey, l.ay, l.ay - l.ey, -l.ey, -l.ay, l.ey - l.ay};
	for(int k = 0; k < kPerimeterEdges; ++k)
	{
		nx[k] = cx + dx[k];
		ny[k] = cy + dy[k];
	}
}

uint8_t landEdges(const heightfield::HeightGrid& g, float waterLevel, float cx, float cy, const Lattice& l)
{
	float nx[kPerimeterEdges], ny[kPerimeterEdges];
	neighbourCentres(cx, cy, l, nx, ny);
	const float floorZ = std::max(waterLevel, g.waterZ);
	uint8_t mask = 0;
	for(int k = 0; k < kPerimeterEdges; ++k)
	{
		const float h = cu::heightAtWorld(g, nx[k], ny[k]);
		if(fin(h) && h > floorZ)
			mask |= (uint8_t)(1u << k);
	}
	return mask;
}

Status perimeterSpots(float cx, float cy, const Lattice& l, uint8_t mask, float share, size_t n,
                      std::vector<PerimeterSpot>& out)
{
	out.clear();
	if(!fin(cx) || !fin(cy) || !fin(l.ex) || !fin(l.ey) || !fin(l.ax) || !fin(l.ay) || !fin(share))
		return Status::fail(Err::BadLayout, "perimeter: an input is not finite");
	if(!(share > 0.0f && share <= 1.0f))
		return Status::fail(Err::BadLayout, "perimeter: the share is outside (0, 1]");
	mask &= 0x3F;
	if(!mask || !n)
		return Status::Ok();
	float nx[kPerimeterEdges], ny[kPerimeterEdges];
	neighbourCentres(cx, cy, l, nx, ny);
	// vertex k: shared by edges k and k + 1 (the centroid of C, N_k, N_k+1), on the ring
	float vx[kPerimeterEdges], vy[kPerimeterEdges];
	for(int k = 0; k < kPerimeterEdges; ++k)
	{
		const int k1 = (k + 1) % kPerimeterEdges;
		const float px = (cx + nx[k] + nx[k1]) / 3.0f, py = (cy + ny[k] + ny[k1]) / 3.0f;
		vx[k] = cx + share * (px - cx);
		vy[k] = cy + share * (py - cy);
	}
	// edge k runs from vertex k - 1 to vertex k
	float len[kPerimeterEdges] = {0};
	double total = 0;
	for(int k = 0; k < kPerimeterEdges; ++k)
	{
		if(!(mask & (1u << k)))
			continue;
		const int km = (k + kPerimeterEdges - 1) % kPerimeterEdges;
		len[k] = std::hypot(vx[k] - vx[km], vy[k] - vy[km]);
		total += len[k];
	}
	if(!(total > 0))
		return Status::fail(Err::BadLayout, "perimeter: the land edges have no length");
	int edges[kPerimeterEdges], m = 0;
	for(int k = 0; k < kPerimeterEdges; ++k)
		if(mask & (1u << k))
			edges[m++] = k;
	out.reserve(n);
	for(size_t i = 0; i < n; ++i)
	{
		double t = (i + 0.5) * total / (double)n;   // the spot's distance along the land edges, in edge order
		int j = 0;
		while(j + 1 < m && t > len[edges[j]])   // the last land edge takes any rounding rest
		{
			t -= len[edges[j]];
			++j;
		}
		const int k = edges[j], km = (k + kPerimeterEdges - 1) % kPerimeterEdges;
		const float f = (float)std::min(1.0, std::max(0.0, t / len[k]));
		PerimeterSpot s;
		s.x = vx[km] + f * (vx[k] - vx[km]);
		s.y = vy[km] + f * (vy[k] - vy[km]);
		s.yaw = std::atan2(ny[k] - cy, nx[k] - cx) + 1.5707963267948966f;   // facing out across edge k
		s.edge = k;
		out.push_back(s);
	}
	return Status::Ok();
}

void GarrisonPlacement::set(int32_t player, int32_t id, uint8_t mask)
{
	if(mask & 0x3F)
		masks_[std::make_pair(player, id)] = (uint8_t)(mask & 0x3F);
	else
		masks_.erase(std::make_pair(player, id));
}

cu::UnitStatus GarrisonPlacement::place(const cu::PlacementRequest& r, cu::UnitPlacement& out)
{
	if(!inner_)
		return cu::UnitStatus::fail(cu::UnitCode::PlacementFailed, "garrison placement: no inner source");
	auto it = (r.unit && !masks_.empty()) ? masks_.find(std::make_pair(r.unit->player, r.unit->id)) : masks_.end();
	// issue 4982 (lead-0042 #11560 (2)): a unit spread here stands on the EXE's floor: its foot radius (FixUnitPlacement)
	// is dropped, since applyFigureOffsets moves each figure from max(heightAtWorld, water), not from the foot base
	cu::PlacementRequest q = r;
	cu::UnitInput stockFeet;
	if(it != masks_.end() && r.unit->foot_radius != 0.f)
	{
		stockFeet = *r.unit;
		stockFeet.foot_radius = 0.f;
		q.unit = &stockFeet;
	}
	cu::UnitStatus st = inner_->place(q, out);
	if(!st.ok() || !r.unit || masks_.empty())
		return st;
	if(it == masks_.end() || out.placed.empty())
		return st;
	// any failure below leaves the inner source's placement (the unit drawn as without the look) and counts it
	if(!r.grid)
	{
		failed_++;
		return st;
	}
	Lattice l;
	float cx = 0, cy = 0;
	if(!latticeOf(*r.grid, l).ok() || !cu::plotCentre(*r.grid, r.unit->plot_x, r.unit->plot_y, cx, cy).ok())
	{
		failed_++;
		return st;
	}
	std::vector<PerimeterSpot> spots;
	if(!perimeterSpots(cx, cy, l, it->second, kPerimeterShare, out.placed.size(), spots).ok() ||
	   spots.size() != out.placed.size())
	{
		failed_++;
		return st;
	}
	// world delta to the spot, in the unit's own frame (applyFigureOffsets rotates (dx, dy) by the unit's yaw)
	const float c = std::cos(out.yaw), s = std::sin(out.yaw);
	std::vector<cu::FigureOffset> offs;
	offs.reserve(spots.size());
	for(size_t i = 0; i < spots.size(); ++i)
	{
		const cu::FigurePlacement& f = out.placed[i];
		const float wx = spots[i].x - f.x, wy = spots[i].y - f.y;
		cu::FigureOffset o;
		o.figure = f.figure;
		o.dx = c * wx + s * wy;
		o.dy = -s * wx + c * wy;
		o.hasYaw = true;
		o.yaw = spots[i].yaw;
		offs.push_back(o);
	}
	if(!cu::applyFigureOffsets(*r.grid, r.water_level, offs, out).ok())
	{
		failed_++;
		return st;
	}
	spread_++;
	return st;
}

}  // namespace units
}  // namespace extras
}  // namespace c5r
