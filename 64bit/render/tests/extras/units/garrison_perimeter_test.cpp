// render/tests/extras/units/garrison_perimeter_test.cpp - GarrisonVisible's look (d), "perimeter" (issue 4983 stage d):
// the lattice, the land mask (water neighbours left out), the spots (count, edge, distance, facing, order), the decorator
// over a stand-in placement (spread, untouched units, every failure leaving the inner placement), and the error paths.
// Written 2026-11-23 for this repository (GPL-3.0-or-later). Inputs are made by the test (no game files).
#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

#include "c5r/heightfield/heightfield.h"
#include "c5r/units/placement.h"
#include "c5r/units/units.h"
#include "testkit.h"
#include "units/garrison_perimeter.h"

using namespace c5r;
namespace cu = c5r::units;
namespace gp = c5r::extras::units;

namespace {

const float kPi = 3.14159265358979f;

// A 6 x 4 plot map, land everywhere (samples 500), the EXE's plot spacing (as render/tests/units' fixtures).
heightfield::HeightGrid grid()
{
	heightfield::HeightGrid g;
	g.mapW = 6;
	g.mapH = 8;
	g.samplesX = g.mapW * 64;
	g.samplesY = g.mapH * 64;
	g.ox = static_cast<float>(g.mapW * 32);
	g.oy = static_cast<float>(g.mapH * 32) * g.sy;
	g.waterZ = 0.1f;   // land samples (500) read about 0.39, water (0) reads 0
	const float m[16] = {64, 0, 0, 0, 32, 64, 0, 0, 0, 0, 1, 0, 32, 128, 0, 1};
	for(int k = 0; k < 16; ++k)
		g.hexToWorld[k] = m[k];
	g.h.assign(static_cast<size_t>(g.samplesX) * g.samplesY, 500);
	return g;
}

// the samples within r world units of (x, y) set to height 0 (below the water level): a water plot there
void water(heightfield::HeightGrid& g, float x, float y, float r)
{
	for(uint32_t sy = 0; sy < g.samplesY; ++sy)
		for(uint32_t sx = 0; sx < g.samplesX; ++sx)
		{
			const float wx = sx * g.sx - g.ox, wy = sy * g.sy - g.oy;
			if(std::hypot(wx - x, wy - y) <= r)
				g.h[g.index(sx, sy)] = 0;
		}
}

bool approx(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

float wrapAngle(float a)
{
	while(a > kPi)
		a -= 2 * kPi;
	while(a <= -kPi)
		a += 2 * kPi;
	return a;
}

// A stand-in inner source: every figure at the plot centre, yaw 1, z 7 (or a failure)
class StandIn : public cu::PlacementSource
{
public:
	size_t n = 4;
	bool fail = false;
	int calls = 0;
	float lastFoot = -1;   // issue 4982: the foot radius the last request carried
	cu::UnitStatus place(const cu::PlacementRequest& r, cu::UnitPlacement& out) override
	{
		calls++;
		lastFoot = r.unit ? r.unit->foot_radius : -1;
		out = cu::UnitPlacement();
		if(fail)
			return cu::UnitStatus::fail(cu::UnitCode::PlacementFailed, "stand-in fails");
		float cx = 0, cy = 0;
		if(r.grid)
			cu::plotCentre(*r.grid, r.unit->plot_x, r.unit->plot_y, cx, cy);
		out.rootX = cx;
		out.rootY = cy;
		out.yaw = 1.0f;
		out.figures = out.living = static_cast<uint32_t>(n);
		for(size_t i = 0; i < n; ++i)
		{
			cu::FigurePlacement f;
			f.figure = f.slot = static_cast<uint16_t>(i);
			f.x = cx + 2.0f * i;
			f.y = cy - 1.0f * i;
			f.z = 7.0f;
			f.yaw = 1.0f;
			out.placed.push_back(f);
		}
		return cu::UnitStatus::Ok();
	}
};

cu::UnitInput unitAt(int px, int py, int player = 1, int id = 10)
{
	cu::UnitInput u;
	u.player = player;
	u.id = id;
	u.plot_x = px;
	u.plot_y = py;
	return u;
}

}  // namespace

C5R_TEST(lattice_and_neighbours)
{
	heightfield::HeightGrid g = grid();
	gp::Lattice l;
	CHECK_OK(gp::latticeOf(g, l));
	// the world frame is regular: a row is 64 x sy (0.866) above the one below, shifted half a plot
	CHECK(approx(l.ex, 64, 1e-4f) && approx(l.ey, 0, 1e-4f));
	CHECK(approx(l.ax, 32, 1e-4f) && approx(l.ay, 64 * g.sy, 1e-3f));
	// the six neighbours of plot (2, 2) are the plot centres of its hex neighbours (even row: NE (2, 3), NW (1, 3),
	// SW (1, 1), SE (2, 1))
	float cx, cy;
	CHECK_OK(cu::plotCentre(g, 2, 2, cx, cy));
	float nx[6], ny[6];
	gp::neighbourCentres(cx, cy, l, nx, ny);
	const int want[6][2] = {{3, 2}, {2, 3}, {1, 3}, {1, 2}, {1, 1}, {2, 1}};
	for(int k = 0; k < 6; ++k)
	{
		float wx, wy;
		CHECK_OK(cu::plotCentre(g, want[k][0], want[k][1], wx, wy));
		CHECK(approx(nx[k], wx, 1e-3f) && approx(ny[k], wy, 1e-3f));
	}
	// odd row (2, 1): E (3, 1), NE (3, 2), NW (2, 2), W (1, 1), SW (2, 0), SE (3, 0)
	CHECK_OK(cu::plotCentre(g, 2, 1, cx, cy));
	gp::neighbourCentres(cx, cy, l, nx, ny);
	const int want1[6][2] = {{3, 1}, {3, 2}, {2, 2}, {1, 1}, {2, 0}, {3, 0}};
	for(int k = 0; k < 6; ++k)
	{
		float wx, wy;
		CHECK_OK(cu::plotCentre(g, want1[k][0], want1[k][1], wx, wy));
		CHECK(approx(nx[k], wx, 1e-3f) && approx(ny[k], wy, 1e-3f));
	}
	// negative: an empty field gives no lattice
	heightfield::HeightGrid e;
	CHECK(!gp::latticeOf(e, l).ok());
}

C5R_TEST(land_mask_leaves_water_out)
{
	heightfield::HeightGrid g = grid();
	gp::Lattice l;
	CHECK_OK(gp::latticeOf(g, l));
	float cx, cy, wx, wy;
	CHECK_OK(cu::plotCentre(g, 2, 2, cx, cy));
	CHECK_EQ(gp::landEdges(g, 0.0f, cx, cy, l), static_cast<uint8_t>(0x3F));
	// the W neighbour (1, 2) and the SE neighbour (2, 1) turned to water: bits 3 and 5 clear
	CHECK_OK(cu::plotCentre(g, 1, 2, wx, wy));
	water(g, wx, wy, 20.0f);
	CHECK_OK(cu::plotCentre(g, 2, 1, wx, wy));
	water(g, wx, wy, 20.0f);
	CHECK_EQ(gp::landEdges(g, 0.0f, cx, cy, l), static_cast<uint8_t>(0x3F & ~(1u << 3) & ~(1u << 5)));
	// a water level above the land makes every edge water
	CHECK_EQ(gp::landEdges(g, 1e6f, cx, cy, l), static_cast<uint8_t>(0));
}

C5R_TEST(spots_all_edges)
{
	heightfield::HeightGrid g = grid();
	gp::Lattice l;
	CHECK_OK(gp::latticeOf(g, l));
	float cx, cy;
	CHECK_OK(cu::plotCentre(g, 2, 2, cx, cy));
	float nx[6], ny[6];
	gp::neighbourCentres(cx, cy, l, nx, ny);
	std::vector<gp::PerimeterSpot> s;
	// six figures on six land edges: one at the middle of each edge, at share x half the way to the neighbour, facing it
	CHECK_OK(gp::perimeterSpots(cx, cy, l, 0x3F, gp::kPerimeterShare, 6, s));
	CHECK_EQ(s.size(), static_cast<size_t>(6));
	std::set<int> edges;
	for(size_t i = 0; i < s.size(); ++i)
	{
		const int k = s[i].edge;
		edges.insert(k);
		const float mx = cx + gp::kPerimeterShare * 0.5f * (nx[k] - cx), my = cy + gp::kPerimeterShare * 0.5f * (ny[k] - cy);
		CHECK(approx(s[i].x, mx, 1e-2f) && approx(s[i].y, my, 1e-2f));
		const float yaw = std::atan2(ny[k] - cy, nx[k] - cx) + kPi / 2;
		CHECK(approx(wrapAngle(s[i].yaw - yaw), 0.0f, 1e-4f));
		// inside the plot: closer to its own centre than to the neighbour's
		CHECK(std::hypot(s[i].x - cx, s[i].y - cy) < std::hypot(s[i].x - nx[k], s[i].y - ny[k]));
	}
	CHECK_EQ(edges.size(), static_cast<size_t>(6));
	// negative: the same spots at share 1 lie farther out (the share is applied)
	std::vector<gp::PerimeterSpot> s1;
	CHECK_OK(gp::perimeterSpots(cx, cy, l, 0x3F, 1.0f, 6, s1));
	CHECK(std::hypot(s1[0].x - cx, s1[0].y - cy) > std::hypot(s[0].x - cx, s[0].y - cy) + 1.0f);
}

C5R_TEST(spots_only_on_land_edges_and_spread)
{
	heightfield::HeightGrid g = grid();
	gp::Lattice l;
	CHECK_OK(gp::latticeOf(g, l));
	float cx, cy;
	CHECK_OK(cu::plotCentre(g, 2, 2, cx, cy));
	std::vector<gp::PerimeterSpot> s;
	// two land edges (E and W, bits 0 and 3), 12 figures: six on each, none on a water edge, evenly spaced along each
	const uint8_t mask = (1u << 0) | (1u << 3);
	CHECK_OK(gp::perimeterSpots(cx, cy, l, mask, gp::kPerimeterShare, 12, s));
	CHECK_EQ(s.size(), static_cast<size_t>(12));
	int onE = 0, onW = 0;
	for(const gp::PerimeterSpot& p : s)
	{
		CHECK(p.edge == 0 || p.edge == 3);
		onE += p.edge == 0;
		onW += p.edge == 3;
	}
	CHECK_EQ(onE, 6);
	CHECK_EQ(onW, 6);
	// the spots on one edge are a straight line, equally spaced
	const float d01 = std::hypot(s[1].x - s[0].x, s[1].y - s[0].y), d12 = std::hypot(s[2].x - s[1].x, s[2].y - s[1].y);
	CHECK(d01 > 1.0f && approx(d01, d12, 1e-2f));
	// one land edge, 3 figures: all on it, same facing
	CHECK_OK(gp::perimeterSpots(cx, cy, l, 1u << 4, gp::kPerimeterShare, 3, s));
	CHECK_EQ(s.size(), static_cast<size_t>(3));
	CHECK(s[0].edge == 4 && s[1].edge == 4 && s[2].edge == 4);
	CHECK(approx(s[0].yaw, s[2].yaw, 1e-6f));
	// the figure count is the unit's own: 1 figure gives 1 spot, at the middle of the land edges' length
	CHECK_OK(gp::perimeterSpots(cx, cy, l, 1u << 4, gp::kPerimeterShare, 1, s));
	CHECK_EQ(s.size(), static_cast<size_t>(1));
}

C5R_TEST(spots_empty_and_errors)
{
	heightfield::HeightGrid g = grid();
	gp::Lattice l;
	CHECK_OK(gp::latticeOf(g, l));
	std::vector<gp::PerimeterSpot> s(3);
	CHECK_OK(gp::perimeterSpots(100, 100, l, 0, gp::kPerimeterShare, 5, s));   // no land edge (an island): no spot
	CHECK(s.empty());
	CHECK_OK(gp::perimeterSpots(100, 100, l, 0x3F, gp::kPerimeterShare, 0, s));
	CHECK(s.empty());
	CHECK_OK(gp::perimeterSpots(100, 100, l, 0xC0, gp::kPerimeterShare, 4, s));   // bits above 5 are not edges
	CHECK(s.empty());
	CHECK_ERR(gp::perimeterSpots(100, 100, l, 0x3F, 0.0f, 4, s), Err::BadLayout);
	CHECK(s.empty());
	CHECK_ERR(gp::perimeterSpots(100, 100, l, 0x3F, 1.5f, 4, s), Err::BadLayout);
	CHECK_ERR(gp::perimeterSpots(std::nanf(""), 100, l, 0x3F, 0.85f, 4, s), Err::BadLayout);
	gp::Lattice flat;   // degenerate: zero vectors give edges of no length
	CHECK_ERR(gp::perimeterSpots(100, 100, flat, 0x3F, 0.85f, 4, s), Err::BadLayout);
}

C5R_TEST(decorator_spreads_only_set_units)
{
	heightfield::HeightGrid g = grid();
	StandIn in;
	in.n = 6;
	gp::GarrisonPlacement dec(&in);
	cu::UnitInput u = unitAt(2, 2), other = unitAt(3, 2, 1, 11);
	cu::PlacementRequest r;
	r.unit = &u;
	r.grid = &g;
	r.water_level = 0;
	cu::UnitPlacement base, out;
	// nothing set: exactly the inner placement
	CHECK(in.place(r, base).ok());
	CHECK(dec.place(r, out).ok());
	CHECK_EQ(out.placed.size(), base.placed.size());
	for(size_t i = 0; i < out.placed.size(); ++i)
		CHECK(out.placed[i].x == base.placed[i].x && out.placed[i].y == base.placed[i].y && out.placed[i].z == base.placed[i].z &&
		      out.placed[i].yaw == base.placed[i].yaw);
	// set: each living figure on its spot, facing out, height resampled (the ZOffset part kept)
	dec.set(1, 10, 0x3F);
	CHECK_EQ(dec.pending(), static_cast<size_t>(1));
	CHECK(dec.place(r, out).ok());
	gp::Lattice l;
	CHECK_OK(gp::latticeOf(g, l));
	float cx, cy;
	CHECK_OK(cu::plotCentre(g, 2, 2, cx, cy));
	std::vector<gp::PerimeterSpot> s;
	CHECK_OK(gp::perimeterSpots(cx, cy, l, 0x3F, gp::kPerimeterShare, 6, s));
	CHECK_EQ(out.placed.size(), static_cast<size_t>(6));
	for(size_t i = 0; i < 6; ++i)
	{
		CHECK(approx(out.placed[i].x, s[i].x, 1e-3f) && approx(out.placed[i].y, s[i].y, 1e-3f));
		CHECK(approx(out.placed[i].yaw, s[i].yaw, 1e-6f));
		CHECK(approx(out.placed[i].z, 7.0f, 1e-3f));   // flat land: the floor is the same at the old and the new spot
	}
	CHECK(out.rootX == base.rootX && out.rootY == base.rootY && out.yaw == base.yaw);   // the unit's own frame unchanged
	CHECK_EQ(dec.spread(), static_cast<uint64_t>(1));
	// another unit is untouched
	r.unit = &other;
	cu::UnitPlacement ob, oo;
	CHECK(in.place(r, ob).ok());
	CHECK(dec.place(r, oo).ok());
	CHECK(oo.placed[0].x == ob.placed[0].x && oo.placed[0].yaw == ob.placed[0].yaw);
	// mask 0 removes the unit; clear() removes every unit
	r.unit = &u;
	dec.set(1, 10, 0);
	CHECK_EQ(dec.pending(), static_cast<size_t>(0));
	CHECK(dec.place(r, out).ok());
	CHECK(out.placed[0].x == base.placed[0].x);
	dec.set(1, 10, 0x3F);
	dec.clear();
	CHECK(dec.place(r, out).ok());
	CHECK(out.placed[0].x == base.placed[0].x);
	CHECK_EQ(dec.failed(), static_cast<uint64_t>(0));
}

// issue 4982 (lead-0042 #11560 (2)): a spread unit's inner placement never takes FixUnitPlacement's foot radius
// (applyFigureOffsets resamples from the EXE's floor); a unit not spread keeps it
C5R_TEST(decorator_drops_the_foot_radius_of_spread_units)
{
	heightfield::HeightGrid g = grid();
	StandIn in;
	gp::GarrisonPlacement dec(&in);
	cu::UnitInput u = unitAt(2, 2), other = unitAt(3, 2, 1, 11);
	u.foot_radius = 3.0f;
	other.foot_radius = 3.0f;
	cu::PlacementRequest r;
	r.unit = &u;
	r.grid = &g;
	cu::UnitPlacement out;
	CHECK(dec.place(r, out).ok());   // nothing set: the request as given
	CHECK(in.lastFoot == 3.0f);
	dec.set(1, 10, 0x3F);
	CHECK(dec.place(r, out).ok());
	CHECK(in.lastFoot == 0.0f && dec.spread() == 1);
	CHECK(u.foot_radius == 3.0f);   // the caller's input is not changed
	r.unit = &other;
	CHECK(dec.place(r, out).ok());   // another unit: its own foot radius
	CHECK(in.lastFoot == 3.0f);
}

C5R_TEST(decorator_failures_keep_the_inner_placement)
{
	heightfield::HeightGrid g = grid();
	StandIn in;
	gp::GarrisonPlacement dec(&in);
	cu::UnitInput u = unitAt(2, 2);
	cu::PlacementRequest r;
	r.unit = &u;
	r.grid = &g;
	cu::UnitPlacement base, out;
	CHECK(in.place(r, base).ok());
	dec.set(1, 10, 0x3F);
	// no height field: the inner placement, counted as failed
	r.grid = nullptr;
	CHECK(dec.place(r, out).ok());
	CHECK(out.placed[0].x == 0.0f && dec.failed() == 1);
	// a plot outside the field
	r.grid = &g;
	cu::UnitInput outside = unitAt(40, 40);
	r.unit = &outside;
	dec.set(1, 10, 0x3F);
	CHECK(dec.place(r, out).ok());
	CHECK_EQ(dec.failed(), static_cast<uint64_t>(2));
	// the inner source's failure passes through unchanged (status and the empty placement)
	r.unit = &u;
	in.fail = true;
	cu::UnitStatus st = dec.place(r, out);
	CHECK(!st.ok() && st.code == cu::UnitCode::PlacementFailed && out.placed.empty());
	// no inner source
	gp::GarrisonPlacement none(nullptr);
	CHECK(!none.place(r, out).ok());
	CHECK_EQ(dec.spread(), static_cast<uint64_t>(0));
}

C5R_TEST_MAIN
