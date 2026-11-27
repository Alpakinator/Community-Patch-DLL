// srv_unit_moored.cpp - ShipsMoored (epic 0455, issue 4984). See srv_unit_moored.h.
// Written 2026-11-20 for this repository. SPDX-License-Identifier: GPL-3.0-or-later
#include "srv_unit_moored.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "c5r/heightfield/heightfield.h"
#include "c5r/units/placement.h"
#include "c5r/units/unit_scene.h"
#include "srv_unit_garrison.h"   // GarrisonInnerRadius: the hex's inner radius of a height field

namespace srvextras {

namespace cu = c5r::units;
using srvunits::UnitExtraUnit;
using srvunits::UnitExtrasFrame;
using srvunits::UnitOverride;

namespace {
const float kPi = 3.14159265358979323846f;
const float kDeg = 0.017453292519943295f;
}  // namespace

bool IsLiveShip(const SrvFeed::UnitRecord& r)
{
	return r.domain == kDomainSea && (r.flags & (cu::kFlagDead | cu::kFlagDelayedDeath)) == 0;
}

float MooredFacing(MooredLook look, float a)
{
	switch(look)
	{
	case MooredLook::AlongShore: return a + 0.5f * kPi;   // along the shore
	case MooredLook::OutToSea: return a;                  // the bow out along the side
	default: return a + kPi;                              // the bow back to the city (a, d)
	}
}

bool MooredLookOfValue(int value, MooredLook& look)
{
	switch(value)
	{
	case 1: look = MooredLook::BowToLandSmall; return true;   // D250: look (d)
	case 2: look = MooredLook::AlongShore; return true;       // D250: look (b)
	default: return false;
	}
}

const char* MooredLookName(MooredLook look)
{
	switch(look)
	{
	case MooredLook::BowToLand: return "look (a) BowToLand";
	case MooredLook::AlongShore: return "look (b) AlongShore";
	case MooredLook::OutToSea: return "look (c) OutToSea";
	case MooredLook::BowToLandSmall: return "look (d) BowToLandSmall";
	}
	return "look (?)";
}

float MooredGapShare(MooredLook look) { return look == MooredLook::AlongShore ? kMooredLengthGap : kMooredBeamGap; }

float MooredScale(MooredLook look) { return look == MooredLook::BowToLandSmall ? kMooredSmallScale : 1.0f; }

bool MooredFootOnWater(const c5r::heightfield::HeightGrid& g, float x, float y, float hullRad, float inner, float s)
{
	const float hx = std::cos(hullRad), hy = std::sin(hullRad);
	const float l = kMooredHalfLength * inner * s, b = kMooredHalfBeam * inner * s;
	const float pts[5][2] = {{0, 0}, {l * hx, l * hy}, {-l * hx, -l * hy}, {-b * hy, b * hx}, {b * hy, -b * hx}};
	for(const auto& p : pts)
		if(cu::heightAtWorld(g, x + p[0], y + p[1]) > g.waterZ)
			return false;
	return true;
}

bool PlanMooredRow(const c5r::heightfield::HeightGrid& g, float cx, float cy, float inner, int n, MooredLook look,
                   std::vector<MooredSlot>& out)
{
	out.clear();
	if(n <= 0 || !(inner > 0) || !std::isfinite(cx) || !std::isfinite(cy))
		return false;
	const float s = MooredScale(look);
	const float gap = MooredGapShare(look) * inner * s;
	const int steps = (int)std::floor((kMooredFarShare - kMooredNearShare) / kMooredStepShare + 0.5f);
	// the nearest distance first, then the sides in their order: the first fit is the nearest, the earlier side on a tie
	for(int t = 0; t <= steps; ++t)
	{
		const float share = kMooredNearShare + (float)t * kMooredStepShare;
		const float d = share * inner;
		for(int k = 0; k < kMooredSides; ++k)
		{
			const float a = kMooredSideDeg[k] * kDeg;
			// south exactly (no cos(-90 deg) rounding in x), as the garrison's edge looks
			const float ux = k == 0 ? 0.0f : std::cos(a), uy = k == 0 ? -1.0f : std::sin(a);
			const float tx = -uy, ty = ux;   // along the shore (the side's direction turned by +90 degrees)
			const float facing = MooredFacing(look, a);
			bool all = true;
			for(int j = 0; j < n && all; ++j)
			{
				const float o = ((float)j - 0.5f * (float)(n - 1)) * gap;
				all = MooredFootOnWater(g, cx + d * ux + o * tx, cy + d * uy + o * ty, facing, inner, s);
			}
			if(!all)
				continue;
			out.resize((size_t)n);
			for(int j = 0; j < n; ++j)
			{
				const float o = ((float)j - 0.5f * (float)(n - 1)) * gap;
				MooredSlot& m = out[(size_t)j];
				m.x = cx + d * ux + o * tx;
				m.y = cy + d * uy + o * ty;
				m.yaw = facing + 0.5f * kPi;   // the seam's yaw for a unit facing the world angle `facing`
				m.scale = s;
				m.side = k;
				m.share = share;
			}
			return true;
		}
	}
	if(n == 1)
		return false;
	// no water side holds the whole row (a narrow bay, a big ship's look): each ship on its own, in key order, at the
	// nearest water spot along the shore (offsets 0, +gap, -gap, +2 gap, ...) at least one gap from every ship placed
	// before it; a ship with no such spot stays unplaced (side -1)
	out.assign((size_t)n, MooredSlot());
	bool any = false;
	for(int j = 0; j < n; ++j)
	{
		bool found = false;
		for(int t = 0; t <= steps && !found; ++t)
		{
			const float share = kMooredNearShare + (float)t * kMooredStepShare;
			const float d = share * inner;
			for(int k = 0; k < kMooredSides && !found; ++k)
			{
				const float a = kMooredSideDeg[k] * kDeg;
				const float ux = k == 0 ? 0.0f : std::cos(a), uy = k == 0 ? -1.0f : std::sin(a);
				const float tx = -uy, ty = ux;
				const float facing = MooredFacing(look, a);
				for(int m = 0; m <= 2 * n && !found; ++m)
				{
					const float o = (float)((m + 1) / 2) * (m % 2 ? 1.0f : -1.0f) * gap;
					const float x = cx + d * ux + o * tx, y = cy + d * uy + o * ty;
					bool clear = true;
					for(int q = 0; q < j && clear; ++q)
						if(out[(size_t)q].side >= 0)
							clear = std::hypot(out[(size_t)q].x - x, out[(size_t)q].y - y) >= 0.999f * gap;
					if(!clear || !MooredFootOnWater(g, x, y, facing, inner, s))
						continue;
					MooredSlot& sl = out[(size_t)j];
					sl.x = x;
					sl.y = y;
					sl.yaw = facing + 0.5f * kPi;
					sl.scale = s;
					sl.side = k;
					sl.share = share;
					found = any = true;
				}
			}
		}
	}
	if(!any)
		out.clear();
	return any;
}

MooredChoice ChooseMoored(const UnitExtraUnit& u, bool port, bool walking, const MooredSlot* slot)
{
	MooredChoice c;
	if(!u.rec || !port || !IsLiveShip(*u.rec))
		return c;
	if(u.heldInFight)
	{
		c.kind = MooredChoice::Held;
		return c;
	}
	if(!slot)
	{
		// no water spot: left as stock has it (a hidden ship stays hidden: shown at the plot centre it would stand on land)
		c.kind = MooredChoice::NoWaterSide;
		return c;
	}
	c.show = u.hiding;
	if(walking)
	{
		c.kind = MooredChoice::Walking;
		return c;
	}
	c.kind = MooredChoice::Placed;
	c.root = true;
	c.slot = *slot;
	return c;
}

namespace {

int64_t KeyOf(int32_t owner, int32_t id) { return ((int64_t)owner << 32) | (uint32_t)id; }

// One live ship on a port plot this frame (the frame's plan, sorted by key as the scene's table is)
struct PortShip
{
	int64_t key = 0;
	int32_t plot = -1, px = -1, py = -1;
	float cx = 0, cy = 0;     // its plot's centre
	bool placed = false;      // its row found a water side
	MooredSlot slot;
};

class ShipsMoored : public UnitExtra
{
public:
	ShipsMoored(Vp64Opt::Id id, MooredLook look, bool fixedLook) : m_id(id), m_look(look), m_fixedLook(fixedLook) {}

	const char* name() const override { return "ShipsMoored"; }
	Vp64Opt::Id id() const override { return m_id; }
	Vp64Opt::ExtraState state(const Vp64Opt::Options& o) const override { return Vp64Opt::Extra(o, m_id); }

	bool configure(const Vp64Opt::Options& o, std::string& why) override
	{
		// the row it is made with must be its own (a maker given another id is a programming error, said once)
		if(m_id >= Vp64Opt::kCount || strcmp(Vp64Opt::Definition(m_id).key, name()) != 0)
		{
			why = "its option row is not ShipsMoored";
			return false;
		}
		// D250: the value in force is the look (read once; the module calls configure only while the extra is on)
		const int v = Vp64Opt::Extra(o, m_id).value;
		if(!m_fixedLook && !MooredLookOfValue(v, m_look))
		{
			why = "ShipsMoored = " + std::to_string(v) + " names no look";
			return false;
		}
		m_note = std::string(MooredLookName(m_look)) +
		         (m_fixedLook ? " (fixed by the maker)" : " (ShipsMoored = " + std::to_string(v) + ")");
		return true;
	}

	bool takeNote(std::string& line) override
	{
		if(m_note.empty())
			return false;
		line.swap(m_note);
		m_note.clear();
		return true;
	}

	MooredLook look() const { return m_look; }

	bool beginFrame(const UnitExtrasFrame& f, std::string& why) override
	{
		m_grid = f.grid;
		m_ships.clear();
		if(!f.units)
		{
			// E6: an input missing (the scene always passes its table; a scene without it is not the one this extra reads)
			m_grid = nullptr;
			why = "the scene gives no unit table";
			return false;
		}
		if(!m_grid)
			return true;   // no height field this frame: the scene draws no unit either; nothing to do
		m_c.frames++;
		// E6: a field that gives no plot centres is an input missing: off for the session (the module's one line)
		if(!GarrisonInnerRadius(*m_grid, m_inner, why))
		{
			m_grid = nullptr;
			return false;
		}
		plan(*f.units, f.width, f.height);
		return true;
	}

	bool unit(const UnitExtraUnit& u, UnitOverride& io, std::string& why) override
	{
		(void)why;
		if(!m_grid || !u.rec || u.rec->domain != kDomainSea)
			return true;
		const PortShip* p = find(KeyOf(u.rec->owner, u.rec->id));
		if(!p || p->plot != u.rec->plot)
			return true;   // not a live ship on a port plot this frame
		m_c.ships++;
		const MooredChoice c = ChooseMoored(u, true, walking(u, *p), p->placed ? &p->slot : nullptr);
		switch(c.kind)
		{
		case MooredChoice::Held: m_c.held++; return true;
		case MooredChoice::Walking: m_c.walking++; break;
		case MooredChoice::NoWaterSide: m_c.noWaterSide++; break;
		case MooredChoice::Placed: m_c.placed++; break;
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
			io.rootX = c.slot.x;
			io.rootY = c.slot.y;
			io.heading = true;
			io.yaw = c.slot.yaw;
			if(c.slot.scale != 1.0f)
				io.scale = c.slot.scale;
		}
		return true;
	}

	MooredCounts m_c;

private:
	// The frame's ports: every live ship on a plot whose centre is land, sorted by key; each port plot's row planned once
	void plan(const std::vector<SrvFeed::UnitRecord>& units, uint32_t w, uint32_t h)
	{
		if(!w || !h)
			return;
		for(const SrvFeed::UnitRecord& r : units)
		{
			if(!IsLiveShip(r) || r.plot < 0 || (uint32_t)r.plot >= w * h)
				continue;
			PortShip s;
			s.key = KeyOf(r.owner, r.id);
			s.plot = r.plot;
			s.px = r.plot % (int32_t)w;
			s.py = r.plot / (int32_t)w;
			if(!cu::plotCentre(*m_grid, s.px, s.py, s.cx, s.cy).ok())
				continue;
			if(!(cu::heightAtWorld(*m_grid, s.cx, s.cy) > m_grid->waterZ))
				continue;   // a ship at sea (its plot's centre is water): not a port
			m_ships.push_back(s);
		}
		if(m_ships.empty())
			return;
		// the scene's table is sorted by (owner, id); keep the plan sorted by key for the lookup in unit()
		std::sort(m_ships.begin(), m_ships.end(), [](const PortShip& a, const PortShip& b) { return a.key < b.key; });
		m_order.resize(m_ships.size());
		for(size_t i = 0; i < m_order.size(); ++i)
			m_order[i] = i;
		std::stable_sort(m_order.begin(), m_order.end(),
		                 [this](size_t a, size_t b) { return m_ships[a].plot < m_ships[b].plot; });
		for(size_t i = 0; i < m_order.size();)
		{
			size_t j = i;
			while(j < m_order.size() && m_ships[m_order[j]].plot == m_ships[m_order[i]].plot)
				++j;
			const int n = (int)(j - i);
			const PortShip& first = m_ships[m_order[i]];
			m_c.ports++;
			if(n > 1)
				m_c.rows++;
			if(PlanMooredRow(*m_grid, first.cx, first.cy, m_inner, n, m_look, m_row))
				for(int k = 0; k < n; ++k)
				{
					PortShip& s = m_ships[m_order[i + (size_t)k]];   // the row's members in key order
					s.placed = m_row[(size_t)k].side >= 0;          // the fallback may leave one unplaced
					s.slot = m_row[(size_t)k];
				}
			i = j;
		}
	}

	const PortShip* find(int64_t key) const
	{
		auto it = std::lower_bound(m_ships.begin(), m_ships.end(), key,
		                           [](const PortShip& s, int64_t k) { return s.key < k; });
		return it != m_ships.end() && it->key == key ? &*it : nullptr;
	}

	// lead-0042 #7810: the scene draws it at the plot it left (a walk without a position yet), or its scene root is off
	// its plot centre
	bool walking(const UnitExtraUnit& u, const PortShip& p) const
	{
		if(u.plotX != p.px || u.plotY != p.py)
			return true;
		if(!u.hasRoot)
			return false;
		const float dx = u.rootX - p.cx, dy = u.rootY - p.cy;
		return std::sqrt(dx * dx + dy * dy) > kMooredWalkShare * m_inner;
	}

	Vp64Opt::Id m_id;
	MooredLook m_look;
	bool m_fixedLook;                 // true: the maker's look, whatever the value (tests, the scratch switch)
	std::string m_note;               // the look line, logged once after the first frame (takeNote)
	const c5r::heightfield::HeightGrid* m_grid = nullptr;
	float m_inner = 0;                // the hex's inner radius of this frame's field
	std::vector<PortShip> m_ships;    // this frame's port ships (capacity kept across frames)
	std::vector<size_t> m_order;      // m_ships by plot, then key
	std::vector<MooredSlot> m_row;    // one port's row
};

}  // namespace

std::unique_ptr<UnitExtra> MakeShipsMoored(Vp64Opt::Id id)
{
	return std::unique_ptr<UnitExtra>(new ShipsMoored(id, MooredLook::BowToLandSmall, false));   // the look: configure's
}

std::unique_ptr<UnitExtra> MakeShipsMoored(Vp64Opt::Id id, MooredLook look)
{
	return std::unique_ptr<UnitExtra>(new ShipsMoored(id, look, true));
}

bool MooredLookOf(const UnitExtra& x, MooredLook& out)
{
	const ShipsMoored* p = dynamic_cast<const ShipsMoored*>(&x);
	if(!p)
		return false;
	out = p->look();
	return true;
}

bool MooredCountsOf(const UnitExtra& x, MooredCounts& out)
{
	const ShipsMoored* p = dynamic_cast<const ShipsMoored*>(&x);
	if(!p)
		return false;
	out = p->m_c;
	return true;
}

}  // namespace srvextras
