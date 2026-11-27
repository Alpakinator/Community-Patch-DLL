// srv_unit_garrison.cpp - GarrisonVisible (epic 0455, issue 4983). See srv_unit_garrison.h.
// Written 2026-11-20 for this repository. SPDX-License-Identifier: GPL-3.0-or-later
#include "srv_unit_garrison.h"

#include <cmath>
#include <cstring>

#include "c5r/heightfield/heightfield.h"
#include "c5r/units/placement.h"
#include "c5r/units/unit_scene.h"
#include "../../../../render/src/extras/units/garrison_perimeter.h"   // look (d) (render/src/extras: no include path)

namespace srvextras {

namespace cu = c5r::units;
namespace gpr = c5r::extras::units;
using srvunits::UnitExtraUnit;
using srvunits::UnitExtrasFrame;
using srvunits::UnitOverride;

bool IsGarrisonUnit(const SrvFeed::UnitRecord& r)
{
	return (r.flags & cu::kFlagGarrisoned) != 0 && r.domain == kDomainLand && (r.flags & cu::kFlagEmbarked) == 0 &&
	       (r.flags & cu::kFlagDead) == 0;
}

bool GarrisonInnerRadius(const c5r::heightfield::HeightGrid& g, float& inner, std::string& why)
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

bool GarrisonFootOnLand(const c5r::heightfield::HeightGrid& g, float x, float y, float r)
{
	const float pts[5][2] = {{0, 0}, {r, 0}, {-r, 0}, {0, r}, {0, -r}};
	for(const auto& p : pts)
		if(!(cu::heightAtWorld(g, x + p[0], y + p[1]) > g.waterZ))
			return false;
	return true;
}

GarrisonChoice ChooseGarrison(const UnitExtraUnit& u, bool centreOk, float cx, float cy, float inner, GarrisonLook look,
                              const c5r::heightfield::HeightGrid* grid)
{
	GarrisonChoice c;
	if(!u.rec || !IsGarrisonUnit(*u.rec))
		return c;
	if(u.heldInFight)
	{
		c.kind = GarrisonChoice::Held;
		return c;
	}
	if(!centreOk)
	{
		c.kind = GarrisonChoice::NoPlot;
		return c;
	}
	c.show = u.hiding;
	if(u.hasRoot)
	{
		const float dx = u.rootX - cx, dy = u.rootY - cy;
		if(std::sqrt(dx * dx + dy * dy) > kGarrisonWalkShare * inner)
		{
			c.kind = GarrisonChoice::Walking;
			return c;
		}
	}
	c.kind = GarrisonChoice::Placed;
	if(look == GarrisonLook::SouthEdge || look == GarrisonLook::SouthEdgeCompact)
	{
		const bool compact = look == GarrisonLook::SouthEdgeCompact;
		const float d = (compact ? kGarrisonCompactShare : kGarrisonEdgeShare) * inner;
		const float s = compact ? kGarrisonCompactScale : 1.0f;
		for(int k = 0; k < kGarrisonSides; ++k)
		{
			const float a = kGarrisonSideDeg[k] * 0.017453292519943295f;
			// world y points north (unit-formations.md 11.2); south exactly (no cos(-90 deg) rounding in x)
			const float x = k == 0 ? cx : cx + d * std::cos(a), y = k == 0 ? cy - d : cy + d * std::sin(a);
			if(grid && !GarrisonFootOnLand(*grid, x, y, kGarrisonFootShare * inner * s))
				continue;
			c.root = true;
			c.rootX = x;
			c.rootY = y;
			c.heading = true;
			c.yaw = k == 0 ? kGarrisonSouthYaw : a + 1.5707963267948966f;   // facing out along the side
			c.scale = s;
			c.side = k;
			return c;
		}
		c.noLandSide = true;   // a one-plot island: the centre look's placement
	}
	else if(look == GarrisonLook::Perimeter)
	{
		// stage (d): no root or heading (the plot centre); the module's placement decorator spreads the living figures
		// along the land edges (render/src/extras/units/garrison_perimeter.h). No field: every edge
		uint8_t mask = 0x3F;
		if(grid)
		{
			gpr::Lattice l;
			mask = gpr::latticeOf(*grid, l).ok() ? gpr::landEdges(*grid, grid->waterZ, cx, cy, l) : 0;
		}
		c.perimeter = mask;
		c.noLandSide = mask == 0;   // a one-plot island: the centre look's placement
	}
	return c;
}

namespace {

class GarrisonVisible : public UnitExtra
{
public:
	// name: the [Extras] key of the row it is made for ("GarrisonVisible", or "PerimeterGarrisons" for look (d) as the
	// players' option, ticket 4983 D243)
	GarrisonVisible(Vp64Opt::Id id, GarrisonLook look, const char* name) : m_id(id), m_look(look), m_name(name) {}

	const char* name() const override { return m_name; }
	Vp64Opt::Id id() const override { return m_id; }
	Vp64Opt::ExtraState state(const Vp64Opt::Options& o) const override { return Vp64Opt::Extra(o, m_id); }

	bool configure(const Vp64Opt::Options& o, std::string& why) override
	{
		// the row it is made with must be its own (a maker given another id is a programming error, said once)
		if(m_id >= Vp64Opt::kCount || strcmp(Vp64Opt::Definition(m_id).key, name()) != 0)
		{
			why = std::string("its option row is not ") + name();
			return false;
		}
		// D243: GarrisonVisible (a test value) steps aside while the players' PerimeterGarrisons is in effect, so that two
		// looks never mix on one unit (each unit left as the extras before it gave it)
		m_yield = m_id == Vp64Opt::kGarrisonVisible && Vp64Opt::Extra(o, Vp64Opt::kPerimeterGarrisons).on;
		m_noted = false;
		return true;
	}

	bool takeNote(std::string& line) override
	{
		if(!m_yield || m_noted)
			return false;
		m_noted = true;
		line = "PerimeterGarrisons = 1 is in effect: this test look draws nothing (PerimeterGarrisons draws the garrisons)";
		return true;
	}

	bool beginFrame(const UnitExtrasFrame& f, std::string& why) override
	{
		if(m_yield)
		{
			m_grid = nullptr;
			return true;
		}
		m_grid = f.grid;
		if(!m_grid)
			return true;   // no height field this frame: the scene draws no unit either; nothing to do
		m_c.frames++;
		// every frame (two plot centres): the field object may be refilled at a new load. E6: a field that gives no plot
		// centres is an input missing: off for the session (the module's one line)
		if(!GarrisonInnerRadius(*m_grid, m_inner, why))
		{
			m_grid = nullptr;
			return false;
		}
		return true;
	}

	bool unit(const UnitExtraUnit& u, UnitOverride& io, std::string& why) override
	{
		(void)why;
		if(!m_grid || !u.rec || !IsGarrisonUnit(*u.rec))
			return true;
		m_c.garrisons++;
		float cx = 0, cy = 0;
		const bool centreOk = u.plotX >= 0 && u.plotY >= 0 && cu::plotCentre(*m_grid, u.plotX, u.plotY, cx, cy).ok();
		const GarrisonChoice c = ChooseGarrison(u, centreOk, cx, cy, m_inner, m_look, m_grid);
		if(c.noLandSide)
			m_c.noLandSide++;
		switch(c.kind)
		{
		case GarrisonChoice::Held: m_c.held++; return true;
		case GarrisonChoice::NoPlot: m_c.noPlot++; return true;
		case GarrisonChoice::Walking: m_c.walking++; break;
		case GarrisonChoice::Placed: m_c.placed++; break;
		default: return true;
		}
		if(c.show)
		{
			io.show = true;
			m_c.shown++;
		}
		if(c.root)
		{
			io.root = true;
			io.rootX = c.rootX;
			io.rootY = c.rootY;
		}
		if(c.heading)
		{
			io.heading = true;
			io.yaw = c.yaw;
		}
		if(c.scale != 1.0f)
			io.scale = c.scale;
		if(c.perimeter)
			io.perimeterEdges = c.perimeter;   // look (d): the module's placement decorator spreads the figures
		return true;
	}

	bool wantsPlacement() const override { return m_look == GarrisonLook::Perimeter && !m_yield; }

	GarrisonCounts m_c;

private:
	Vp64Opt::Id m_id;
	GarrisonLook m_look;
	const char* m_name;
	bool m_yield = false;   // D243: GarrisonVisible while PerimeterGarrisons is in effect (configure)
	bool m_noted = false;
	const c5r::heightfield::HeightGrid* m_grid = nullptr;
	float m_inner = 0;   // the hex's inner radius of this frame's field
};

}  // namespace

std::unique_ptr<UnitExtra> MakeGarrisonVisible(Vp64Opt::Id id, GarrisonLook look)
{
	return std::unique_ptr<UnitExtra>(new GarrisonVisible(id, look, "GarrisonVisible"));
}

std::unique_ptr<UnitExtra> MakePerimeterGarrisons(Vp64Opt::Id id)
{
	return std::unique_ptr<UnitExtra>(new GarrisonVisible(id, GarrisonLook::Perimeter, "PerimeterGarrisons"));
}

bool GarrisonCountsOf(const UnitExtra& x, GarrisonCounts& out)
{
	const GarrisonVisible* g = dynamic_cast<const GarrisonVisible*>(&x);
	if(!g)
		return false;
	out = g->m_c;
	return true;
}

}  // namespace srvextras
