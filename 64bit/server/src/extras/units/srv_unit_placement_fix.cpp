// srv_unit_placement_fix.cpp - FixUnitPlacement (epic 0455, issue 4982). See srv_unit_placement_fix.h.
// Written 2026-11-20 for this repository. SPDX-License-Identifier: GPL-3.0-or-later
#include "srv_unit_placement_fix.h"

#include <cmath>
#include <cstring>
#include <map>
#include <vector>

#include "c5r/heightfield/heightfield.h"
#include "c5r/units/placement.h"
#include "c5r/units/unit_scene.h"

namespace srvextras {

namespace cu = c5r::units;
namespace hf = c5r::heightfield;
using srvunits::UnitExtraUnit;
using srvunits::UnitExtrasFrame;
using srvunits::UnitOverride;

float SurfaceAt(const hf::HeightGrid& g, float x, float y)
{
	const float fc = (x + g.ox) / g.sx, fr = (y + g.oy) / g.sy;
	if(!(std::fabs(fc) < 1e9f) || !(std::fabs(fr) < 1e9f))
		return 0.0f;
	const float c0f = std::floor(fc), r0f = std::floor(fr);
	const int c0 = (int)c0f, r0 = (int)r0f;
	const float tx = fc - c0f, ty = fr - r0f;
	const float h00 = hf::heightAtSample(g, c0, r0), h10 = hf::heightAtSample(g, c0 + 1, r0);
	const float h01 = hf::heightAtSample(g, c0, r0 + 1), h11 = hf::heightAtSample(g, c0 + 1, r0 + 1);
	return (h00 * (1 - tx) + h10 * tx) * (1 - ty) + (h01 * (1 - tx) + h11 * tx) * ty;
}

float MeanSlope(const hf::HeightGrid& g, float x, float y, float radius, float step, int* underWater)
{
	if(underWater)
		*underWater = 0;
	if(!(step > 0) || !(radius >= 0))
		return 0.0f;
	const int n = (int)std::floor(radius / step + 1e-4f);
	double sum = 0;
	int count = 0;
	for(int a = -n; a <= n; ++a)
		for(int b = -n; b <= n; ++b)
		{
			const float ox = a * step, oy = b * step;
			if(std::sqrt(ox * ox + oy * oy) > radius + 1e-4f)
				continue;
			const float px = x + ox, py = y + oy;
			const float gx = SurfaceAt(g, px + 0.5f, py) - SurfaceAt(g, px - 0.5f, py);
			const float gy = SurfaceAt(g, px, py + 0.5f) - SurfaceAt(g, px, py - 0.5f);
			sum += std::sqrt(gx * gx + gy * gy);
			++count;
			if(underWater && SurfaceAt(g, px, py) < g.waterZ)
				++*underWater;
		}
	return count ? (float)(sum / count) : 0.0f;
}

FlatGround FindFlatGround(const hf::HeightGrid& g, float cx, float cy)
{
	// The candidates and their cost points share one kFlatStep lattice around the centre: each point's slope and water
	// test once (a lattice of (2 (nc + np) + 1)^2 points instead of a MeanSlope per candidate); the same sums as MeanSlope.
	const int nc = (int)std::floor(kFlatSearch / kFlatStep + 1e-4f), np = (int)std::floor(kFlatRadius / kFlatStep + 1e-4f);
	const int half = nc + np, side = 2 * half + 1;
	std::vector<float> slope((size_t)side * side);
	std::vector<unsigned char> wet((size_t)side * side);
	for(int j = -half; j <= half; ++j)
		for(int i = -half; i <= half; ++i)
		{
			const float px = cx + i * kFlatStep, py = cy + j * kFlatStep;
			const float gx = SurfaceAt(g, px + 0.5f, py) - SurfaceAt(g, px - 0.5f, py);
			const float gy = SurfaceAt(g, px, py + 0.5f) - SurfaceAt(g, px, py - 0.5f);
			const size_t k = (size_t)(j + half) * side + (size_t)(i + half);
			slope[k] = std::sqrt(gx * gx + gy * gy);
			wet[k] = SurfaceAt(g, px, py) < g.waterZ;
		}
	auto cost = [&](int ix, int iy, int& water) {
		double sum = 0;
		int count = 0;
		water = 0;
		for(int a = -np; a <= np; ++a)
			for(int b = -np; b <= np; ++b)
			{
				const float ox = a * kFlatStep, oy = b * kFlatStep;
				if(std::sqrt(ox * ox + oy * oy) > kFlatRadius + 1e-4f)
					continue;
				const size_t k = (size_t)(iy + b + half) * side + (size_t)(ix + a + half);
				sum += slope[k];
				water += wet[k];
				++count;
			}
		return count ? (float)(sum / count) : 0.0f;
	};
	FlatGround f;
	int waterCentre = 0;
	f.slopeCentre = cost(0, 0, waterCentre);
	f.slopePick = f.slopeCentre;
	// the best candidate by the slope in hundredths, then by the distance from the centre (the scoring's order, 4982.4)
	long bestKey = std::lround(f.slopeCentre * 100.0f);
	float bestDist = 0, bestDx = 0, bestDy = 0, bestSlope = f.slopeCentre;
	for(int iy = -nc; iy <= nc; ++iy)
		for(int ix = -nc; ix <= nc; ++ix)
		{
			const float dx = ix * kFlatStep, dy = iy * kFlatStep, dist = std::sqrt(dx * dx + dy * dy);
			if(dist > kFlatSearch + 1e-4f || dist == 0)
				continue;
			int water = 0;
			const float s = cost(ix, iy, water);
			f.candidates++;
			if(water > waterCentre)
				continue;   // never onto more water than the centre stands by
			const long key = std::lround(s * 100.0f);
			if(key < bestKey || (key == bestKey && dist < bestDist))
			{
				bestKey = key;
				bestDist = dist;
				bestDx = dx;
				bestDy = dy;
				bestSlope = s;
			}
		}
	const float gain = f.slopeCentre - bestSlope;
	if(bestDist > 0 && gain >= kFlatMinGain && gain >= kFlatMinShare * f.slopeCentre)
	{
		f.move = true;
		f.dx = bestDx;
		f.dy = bestDy;
		f.slopePick = bestSlope;
	}
	return f;
}

PlacementChoice ChoosePlacement(const UnitExtraUnit& u, const UnitOverride& io, bool plotOk, bool walking)
{
	PlacementChoice c;
	if(!u.rec || u.rec->domain != kPlacementDomainLand)
		return c;
	if(u.rec->flags & cu::kFlagEmbarked)
		c.kind = PlacementChoice::Embarked;
	else if(u.rec->flags & (cu::kFlagDead | cu::kFlagDelayedDeath))
		c.kind = PlacementChoice::Dead;
	else if(!u.listed && !io.show)
		c.kind = PlacementChoice::NotDrawn;
	else if(u.heldInFight)
		c.kind = PlacementChoice::Held;
	else if(io.root)
		c.kind = PlacementChoice::Rooted;
	else if(!plotOk)
		c.kind = PlacementChoice::NoPlot;
	else if(walking)
		c.kind = PlacementChoice::Walking;
	else
		c.kind = PlacementChoice::Candidate;
	return c;
}

bool PlacementWalking(const UnitExtraUnit& u, int32_t recX, int32_t recY, bool centreOk, float cx, float cy, float inner)
{
	if(u.plotX != recX || u.plotY != recY)
		return true;   // drawn at the plot it left (a walk without a position yet)
	if(!u.hasRoot)
		return false;  // at the plot centre
	if(!centreOk || !(inner > 0))
		return true;
	const float dx = u.rootX - cx, dy = u.rootY - cy;
	return std::sqrt(dx * dx + dy * dy) > kPlacementWalkShare * inner;
}

float SettleShare(double nowMs, double restSinceMs)
{
	if(restSinceMs < 0)
		return 1.0f;
	const double t = (nowMs - restSinceMs) / kSettleMs;
	if(!(t > 0))
		return 0.0f;
	if(t >= 1)
		return 1.0f;
	return (float)(t * t * (3 - 2 * t));   // smoothstep: no jolt at either end
}

float FootRadiusFor(const FlatGround& fg)
{
	return (fg.move || fg.slopeCentre >= kFootRoughSlope) ? kFootRadius : 0.0f;
}

namespace {

bool InnerRadius(const hf::HeightGrid& g, float& inner, std::string& why)
{
	inner = 0;
	float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	const c5r::Status a = cu::plotCentre(g, 0, 0, x0, y0);
	if(!a.ok())
	{
		why = "the height field gives no plot centre: " + a.str();
		return false;
	}
	const c5r::Status b = cu::plotCentre(g, 1, 0, x1, y1);
	if(!b.ok())
	{
		why = "the height field gives no second plot centre: " + b.str();
		return false;
	}
	const float d = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
	if(!std::isfinite(d) || d <= 0)
	{
		why = "the height field's plot centres (0, 0) and (1, 0) coincide";
		return false;
	}
	inner = 0.5f * d;
	return true;
}

int64_t KeyOf(int32_t owner, int32_t id) { return ((int64_t)owner << 32) | (uint32_t)id; }

// A sparse checksum of the samples a plot's pick reads (every third sample of the square around its centre).
uint32_t GroundSignature(const hf::HeightGrid& g, float cx, float cy)
{
	const float reach = kFlatSearch + kFlatRadius + 1.0f;
	uint32_t h = 2166136261u;
	for(float y = cy - reach; y <= cy + reach; y += 3.0f)
		for(float x = cx - reach; x <= cx + reach; x += 3.0f)
		{
			const float fc = (x + g.ox) / g.sx, fr = (y + g.oy) / g.sy;
			const float v = hf::heightAtSample(g, (int)std::floor(fc), (int)std::floor(fr));
			uint32_t bits;
			memcpy(&bits, &v, sizeof(bits));
			h = (h ^ bits) * 16777619u;
		}
	return h;
}

class FixUnitPlacement : public UnitExtra
{
public:
	explicit FixUnitPlacement(Vp64Opt::Id id) : m_id(id) {}

	const char* name() const override { return "FixUnitPlacement"; }
	Vp64Opt::Id id() const override { return m_id; }
	Vp64Opt::ExtraState state(const Vp64Opt::Options& o) const override { return Vp64Opt::Extra(o, m_id); }

	bool configure(const Vp64Opt::Options& o, std::string& why) override
	{
		(void)o;
		if(m_id >= Vp64Opt::kCount || strcmp(Vp64Opt::Definition(m_id).key, name()) != 0)
		{
			why = "its option row is not FixUnitPlacement";
			return false;
		}
		return true;
	}

	bool beginFrame(const UnitExtrasFrame& f, std::string& why) override
	{
		m_settling = false;
		m_grid = nullptr;
		if(!f.units)
		{
			why = "the scene gives no unit table";   // E6
			return false;
		}
		if(!f.grid)
			return true;   // no height field this frame: the scene draws no unit either
		if(f.grid != m_lastGrid || f.grid->samplesX != m_lastSamplesX || f.grid->samplesY != m_lastSamplesY)
		{
			m_plots.clear();   // another field: every pick again
			m_lastGrid = f.grid;
			m_lastSamplesX = f.grid->samplesX;
			m_lastSamplesY = f.grid->samplesY;
			if(!InnerRadius(*f.grid, m_inner, why))
			{
				m_lastGrid = nullptr;
				return false;   // E6: a field that gives no plot centres
			}
		}
		m_grid = f.grid;
		m_width = f.width;
		m_height = f.height;
		m_now = f.tMs;
		m_frame++;
		m_c.frames++;
		// units not seen in the last frame are gone (or not offered): their rest state is forgotten
		for(auto it = m_units.begin(); it != m_units.end();)
		{
			if(it->second.seen + 1 < m_frame)
				it = m_units.erase(it);
			else
				++it;
		}
		m_c.cachedPlots = m_plots.size();
		return true;
	}

	bool unit(const UnitExtraUnit& u, UnitOverride& io, std::string& why) override
	{
		(void)why;
		if(!m_grid || !u.rec || u.rec->domain != kPlacementDomainLand)
			return true;
		m_c.land++;
		const SrvFeed::UnitRecord& r = *u.rec;
		const bool plotOk = m_width && m_height && r.plot >= 0 && (uint32_t)r.plot < m_width * m_height;
		const int32_t rx = plotOk ? r.plot % (int32_t)m_width : -1, ry = plotOk ? r.plot / (int32_t)m_width : -1;
		float cx = 0, cy = 0;
		bool centreOk = false, walking = false;
		if(plotOk)
		{
			centreOk = cu::plotCentre(*m_grid, rx, ry, cx, cy).ok();
			walking = PlacementWalking(u, rx, ry, centreOk, cx, cy, m_inner);
		}
		const PlacementChoice c = ChoosePlacement(u, io, plotOk && centreOk, walking);
		UnitState& st = m_units[KeyOf(r.owner, r.id)];
		const bool firstSeen = st.seen == 0;
		st.seen = m_frame;
		const bool atRest = c.kind == PlacementChoice::Candidate;
		if(atRest && !st.atRest)
			st.restSince = firstSeen ? -1.0 : m_now;   // arrived now (eases in) or at rest when first seen (at once)
		st.atRest = atRest;
		switch(c.kind)
		{
		case PlacementChoice::Embarked: m_c.embarked++; return true;
		case PlacementChoice::Dead: m_c.dead++; return true;
		case PlacementChoice::NotDrawn: m_c.notDrawn++; return true;
		case PlacementChoice::Held: m_c.held++; return true;
		case PlacementChoice::Rooted: m_c.rooted++; return true;
		case PlacementChoice::NoPlot: m_c.noPlot++; return true;
		case PlacementChoice::Walking: m_c.walking++; return true;
		case PlacementChoice::Candidate: break;
		default: return true;
		}
		const FlatGround& fg = pick(r.plot, cx, cy);
		const float foot = FootRadiusFor(fg);
		if(!fg.move && foot <= 0)
		{
			m_c.flat++;
			return true;
		}
		const float share = SettleShare(m_now, st.restSince);
		if(share < 1.0f)
		{
			m_settling = true;
			m_c.settling++;
		}
		if(foot > 0)
		{
			io.footRadius = foot * share;   // part B (placement.cpp footBase) grows in with the move
			m_c.feet++;
		}
		if(!fg.move)
		{
			m_c.flat++;
			return true;
		}
		const float bx = u.hasRoot ? u.rootX : cx, by = u.hasRoot ? u.rootY : cy;
		io.root = true;
		io.rootX = bx + fg.dx * share;
		io.rootY = by + fg.dy * share;
		m_c.moved++;
		return true;
	}

	bool animating() const override { return m_settling; }

	PlacementCounts m_c;

private:
	struct PlotPick
	{
		uint32_t sig = 0;
		uint64_t checked = 0;     // the extra's frame count when the signature was last compared
		FlatGround fg;
	};
	struct UnitState
	{
		uint64_t seen = 0;        // the extra's frame count when last offered (0: never)
		bool atRest = false;
		double restSince = -1;    // the frame clock when rest began; -1: at rest when first seen
	};

	const FlatGround& pick(int32_t plot, float cx, float cy)
	{
		auto it = m_plots.find(plot);
		if(it != m_plots.end() && it->second.checked == m_frame)
			return it->second.fg;   // checked this frame already (another unit of the plot)
		const uint32_t sig = GroundSignature(*m_grid, cx, cy);
		if(it != m_plots.end() && it->second.sig == sig)
		{
			it->second.checked = m_frame;
			return it->second.fg;
		}
		PlotPick& p = m_plots[plot];
		p.sig = sig;
		p.checked = m_frame;
		p.fg = FindFlatGround(*m_grid, cx, cy);
		m_c.picks++;
		m_c.cachedPlots = m_plots.size();
		return p.fg;
	}

	Vp64Opt::Id m_id;
	const hf::HeightGrid* m_grid = nullptr;
	const hf::HeightGrid* m_lastGrid = nullptr;
	uint32_t m_lastSamplesX = 0, m_lastSamplesY = 0;
	uint32_t m_width = 0, m_height = 0;
	float m_inner = 0;
	double m_now = 0;
	uint64_t m_frame = 0;
	bool m_settling = false;
	std::map<int32_t, PlotPick> m_plots;
	std::map<int64_t, UnitState> m_units;
};

}  // namespace

std::unique_ptr<UnitExtra> MakeFixUnitPlacement(Vp64Opt::Id id)
{
	return std::unique_ptr<UnitExtra>(new FixUnitPlacement(id));
}

bool PlacementCountsOf(const UnitExtra& x, PlacementCounts& out)
{
	const FixUnitPlacement* p = dynamic_cast<const FixUnitPlacement*>(&x);
	if(!p)
		return false;
	out = p->m_c;
	return true;
}

}  // namespace srvextras
