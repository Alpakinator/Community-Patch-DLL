// srv_unit_airbases.cpp - FixAirBases (epic 0455, issue 4985). See srv_unit_airbases.h.
// Written 2026-11-20 for this repository. SPDX-License-Identifier: GPL-3.0-or-later
#include "srv_unit_airbases.h"

#include <cmath>
#include <cstring>
#include <map>

#include "c5r/heightfield/heightfield.h"
#include "c5r/units/placement.h"
#include "c5r/units/unit_scene.h"

namespace srvextras {

namespace cu = c5r::units;
using srvunits::UnitExtraUnit;
using srvunits::UnitExtrasFrame;
using srvunits::UnitOverride;

bool IsLiveAircraft(const SrvFeed::UnitRecord& r)
{
	return r.domain == kDomainAir && (r.flags & (cu::kFlagDead | cu::kFlagDelayedDeath)) == 0;
}

AirTeam AirTeamOf(const SrvFeed::UnitRecord& r, const std::vector<SrvFeed::PlayerRecord>& players)
{
	if(r.owner < 0 || (size_t)r.owner >= players.size())
		return AirTeam::NoRecord;
	const SrvFeed::PlayerRecord& p = players[(size_t)r.owner];
	if(p.player != r.owner)
		return AirTeam::NoRecord;
	return p.team == p.activeTeam ? AirTeam::Active : AirTeam::Other;
}

AirBaseChoice ChooseAirBase(const UnitExtraUnit& u, AirTeam team, bool plotOk, bool walking)
{
	AirBaseChoice c;
	if(!u.rec || u.rec->domain != kDomainAir)
		return c;
	if(!u.hiding)
	{
		c.kind = AirBaseChoice::Listed;
		return c;
	}
	if(!IsLiveAircraft(*u.rec))
	{
		c.kind = AirBaseChoice::Dead;
		return c;
	}
	if(u.heldInFight)
	{
		c.kind = AirBaseChoice::Held;
		return c;
	}
	if(team == AirTeam::NoRecord)
	{
		c.kind = AirBaseChoice::NoPlayer;
		return c;
	}
	if(team == AirTeam::Other)
	{
		c.kind = AirBaseChoice::Foreign;
		return c;
	}
	if(!plotOk)
	{
		c.kind = AirBaseChoice::NoPlot;
		return c;
	}
	if(walking)
	{
		c.kind = AirBaseChoice::Walking;
		return c;
	}
	c.kind = AirBaseChoice::Shown;
	c.show = true;
	return c;
}

bool AirBaseWalking(const UnitExtraUnit& u, int32_t recX, int32_t recY, bool centreOk, float cx, float cy, float inner)
{
	if(u.plotX != recX || u.plotY != recY)
		return true;   // the scene draws it at the plot it left (a walk without a position yet)
	if(!u.hasRoot)
		return false;  // at the plot centre, as every aircraft after a load
	if(!centreOk || !(inner > 0))
		return true;
	const float dx = u.rootX - cx, dy = u.rootY - cy;
	return std::sqrt(dx * dx + dy * dy) > kAirBaseWalkShare * inner;
}

namespace {

// The hex's inner radius from the height field: half the distance of the plot centres (0, 0) and (1, 0).
bool InnerRadius(const c5r::heightfield::HeightGrid& g, float& inner, std::string& why)
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

class FixAirBases : public UnitExtra
{
public:
	explicit FixAirBases(Vp64Opt::Id id) : m_id(id) {}

	const char* name() const override { return "FixAirBases"; }
	Vp64Opt::Id id() const override { return m_id; }
	Vp64Opt::ExtraState state(const Vp64Opt::Options& o) const override { return Vp64Opt::Extra(o, m_id); }

	bool configure(const Vp64Opt::Options& o, std::string& why) override
	{
		(void)o;
		// the row it is made with must be its own (a maker given another id is a programming error, said once)
		if(m_id >= Vp64Opt::kCount || strcmp(Vp64Opt::Definition(m_id).key, name()) != 0)
		{
			why = "its option row is not FixAirBases";
			return false;
		}
		return true;
	}

	void onBatch(const SrvFeed::Batch& b) override
	{
		for(const SrvFeed::Event& e : b.events)
		{
			if(e.kind == SrvFeed::kUnitCarrierChanged)
			{
				m_c.carrierEvents++;
				if(e.a[2] >= 0)
					m_cargo[KeyOf(e.a[0], e.a[1])] = KeyOf(e.a[3], e.a[2]);
				else
					m_cargo.erase(KeyOf(e.a[0], e.a[1]));
			}
			else if(e.kind == SrvFeed::kUnitDestroyed)
				m_cargo.erase(KeyOf(e.a[0], e.a[1]));
			else if(e.kind == SrvFeed::kLoadBegin)
				m_cargo.clear();   // the load's setup sends kind 1001 again for every unit that is cargo
		}
		m_c.cargo = m_cargo.size();
	}

	bool beginFrame(const UnitExtrasFrame& f, std::string& why) override
	{
		m_grid = f.grid;
		m_players = f.players;
		m_width = f.width;
		m_height = f.height;
		if(!f.units || !f.players)
		{
			// E6: an input missing (the scene always passes both; a scene without them is not the one this extra reads)
			m_grid = nullptr;
			why = !f.units ? "the scene gives no unit table" : "the scene gives no player table";
			return false;
		}
		if(!m_grid)
			return true;   // no height field this frame: the scene draws no unit either; nothing to do
		m_c.frames++;
		if(!InnerRadius(*m_grid, m_inner, why))
		{
			m_grid = nullptr;
			return false;   // E6: a field that gives no plot centres is an input missing: off for the session
		}
		return true;
	}

	bool unit(const UnitExtraUnit& u, UnitOverride& io, std::string& why) override
	{
		(void)why;
		if(!m_grid || !m_players || !u.rec || u.rec->domain != kDomainAir)
			return true;
		m_c.aircraft++;
		const SrvFeed::UnitRecord& r = *u.rec;
		const bool plotOk = m_width && m_height && r.plot >= 0 && (uint32_t)r.plot < m_width * m_height;
		const int32_t rx = plotOk ? r.plot % (int32_t)m_width : -1, ry = plotOk ? r.plot / (int32_t)m_width : -1;
		bool walking = false;
		if(plotOk && u.hiding)
		{
			float cx = 0, cy = 0;
			const bool centreOk = cu::plotCentre(*m_grid, rx, ry, cx, cy).ok();
			walking = AirBaseWalking(u, rx, ry, centreOk, cx, cy, m_inner);
		}
		const AirBaseChoice c = ChooseAirBase(u, AirTeamOf(r, *m_players), plotOk, walking);
		switch(c.kind)
		{
		case AirBaseChoice::Dead: m_c.dead++; break;
		case AirBaseChoice::Held: m_c.held++; break;
		case AirBaseChoice::NoPlayer: m_c.noPlayer++; break;
		case AirBaseChoice::Foreign: m_c.foreign++; break;
		case AirBaseChoice::NoPlot: m_c.noPlot++; break;
		case AirBaseChoice::Walking: m_c.walking++; break;
		default: break;
		}
		if(c.show)
		{
			io.show = true;
			m_c.shown++;
			if(m_cargo.count(KeyOf(r.owner, r.id)))
				m_c.shownCarried++;
			else
				m_c.shownBased++;
		}
		return true;
	}

	AirBaseCounts m_c;

private:
	Vp64Opt::Id m_id;
	const c5r::heightfield::HeightGrid* m_grid = nullptr;
	const std::vector<SrvFeed::PlayerRecord>* m_players = nullptr;
	uint32_t m_width = 0, m_height = 0;
	float m_inner = 0;                 // the hex's inner radius of this frame's field
	std::map<int64_t, int64_t> m_cargo;   // (owner, id) -> (transport owner, transport id), from kind 1001
};

}  // namespace

std::unique_ptr<UnitExtra> MakeFixAirBases(Vp64Opt::Id id) { return std::unique_ptr<UnitExtra>(new FixAirBases(id)); }

bool AirBaseCountsOf(const UnitExtra& x, AirBaseCounts& out)
{
	const FixAirBases* p = dynamic_cast<const FixAirBases*>(&x);
	if(!p)
		return false;
	out = p->m_c;
	return true;
}

}  // namespace srvextras
