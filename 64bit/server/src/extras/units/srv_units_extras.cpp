// srv_units_extras.cpp - the unit extras module behind the units seam (epic 0455, issue 4981). See srv_units_extras.h.
// Written 2026-11-20 for this repository. SPDX-License-Identifier: GPL-3.0-or-later
#include "srv_units_extras.h"

#include "srv_unit_airbases.h"
#include "srv_unit_garrison.h"
#include "srv_unit_intercept.h"   // issue 4986
#include "srv_unit_moored.h"
#include "srv_unit_placement_fix.h"   // issue 4982
#include "../../../../render/src/extras/units/garrison_perimeter.h"   // issue 4983 (d): the placement decorator

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <exception>

namespace srvextras {

using srvunits::UnitExtrasFrame;
using srvunits::UnitExtraUnit;
using srvunits::UnitOverride;

const std::vector<UnitExtraMaker>& UnitExtraMakers()
{
	// The features in id order (the order the scene applies them). Registered by the 0455 landing (2026-11-20): 4983
	// GarrisonVisible (24), 4984 ShipsMoored (25), 4985 FixAirBases (26); 4986 AirInterceptPatrol (27, 2026-11-23); 4982
	// FixUnitPlacement (23, 2026-11-23), first: it sets a root, so it runs before an extra that reads io.root; 4983
	// PerimeterGarrisons (32, D243, 2026-11-23): GarrisonVisible's look (d) as the players' option (GarrisonVisible, now a
	// test value, steps aside while it is in effect).
	// A look not yet picked by the owner is its maker's default argument (a pick changes the header, not this list);
	// ShipsMoored's look is its option's value since D250 (0 to 2: 1 look (d), 2 look (b); MooredLookOfValue).
	static const std::vector<UnitExtraMaker> makers = {
	    []() { return MakeFixUnitPlacement(Vp64Opt::kFixUnitPlacement); },   // issue 4982
	    []() { return MakeGarrisonVisible(Vp64Opt::kGarrisonVisible); },
	    []() { return MakeShipsMoored(Vp64Opt::kShipsMoored); },
	    []() { return MakeFixAirBases(Vp64Opt::kFixAirBases); },
	    []() { return MakeAirInterceptPatrol(Vp64Opt::kAirInterceptPatrol); },   // look (a) until the owner picks (4986)
	    []() { return MakePerimeterGarrisons(Vp64Opt::kPerimeterGarrisons); },   // issue 4983 (D243)
	};
	return makers;
}

namespace {

bool Finite(float v) { return std::isfinite(v) != 0; }

// The seam's ranges (srv_units_scene.h UnitOverride); empty why = in range.
const char* OutOfRange(const UnitOverride& o)
{
	if(o.root && (!Finite(o.rootX) || !Finite(o.rootY)))
		return "a root that is not finite";
	if(o.heading && !Finite(o.yaw))
		return "a heading that is not finite";
	if(!Finite(o.scale) || o.scale <= 0 || o.scale > srvunits::kMaxUnitExtraScale)
		return "a scale outside (0, 4]";
	if(o.perimeterEdges & ~0x3F)
		return "perimeter edges outside bits 0..5";
	if(!Finite(o.lift) || o.lift < 0 || o.lift > srvunits::kMaxUnitExtraLift)
		return "a lift outside [0, 200]";   // issue 4986 (H2)
	if(!Finite(o.footRadius) || o.footRadius < 0 || o.footRadius > srvunits::kMaxUnitExtraFootRadius)
		return "a foot radius outside [0, 16]";   // issue 4982
	return nullptr;
}

class UnitsExtrasModule : public srvunits::UnitsExtras
{
public:
	explicit UnitsExtrasModule(std::vector<std::unique_ptr<UnitExtra>> extras)
	{
		for(auto& e : extras)
			if(e)
			{
				Slot s;
				s.x = std::move(e);
				m_slots.push_back(std::move(s));
			}
	}

	void setLog(srvunits::UnitsExtrasLogFn log, void* ctx) override
	{
		m_log = log;
		m_logCtx = ctx;
	}

	void configure(const Vp64Opt::Options& o, bool unitsAreOurs) override
	{
		for(size_t i = 0; i < m_slots.size(); ++i)
		{
			Slot& s = m_slots[i];
			s.running = false;
			const char* nm = s.x->name();
			Vp64Opt::ExtraState st;
			try
			{
				st = s.x->state(o);
			}
			catch(...)
			{
				fail(s, "its state could not be read (an exception)");
				continue;
			}
			if(!st.on)
			{
				char why[300];
				const Vp64Opt::Id id = s.x->id();
				if(id < Vp64Opt::kCount)
					Vp64Opt::DescribeExtra(o, id, why, sizeof(why));   // "<Key>: off: ..." as the start log's extras lines
				else
					snprintf(why, sizeof(why), "%s: off: %s", nm,
					         st.why == Vp64Opt::kExtraFlagOff       ? "its own flag is 0"
					         : st.why == Vp64Opt::kExtraCategoryOff ? "its category switch is 0"
					         : st.why == Vp64Opt::kExtraNeedsOff    ? "an extra it needs is off"
					                                                : "not an extra");
				say("units %s", why);
				continue;
			}
			if(!unitsAreOurs)
			{
				say("units extra %s: off: ServerUnits = 0", nm);   // E5: the game draws the units, nothing of ours to change
				continue;
			}
			std::string why;
			bool ok = false;
			try
			{
				ok = s.x->configure(o, why);
			}
			catch(const std::exception& e)
			{
				why = std::string("an exception: ") + e.what();
			}
			catch(...)
			{
				why = "an exception";
			}
			if(!ok)
			{
				fail(s, ("configure: " + why).c_str());
				continue;
			}
			s.running = true;
			say("units extra %s: in effect", nm);
		}
	}

	bool anyOn() const override { return inEffect() != 0; }

	size_t inEffect() const override
	{
		size_t n = 0;
		for(const Slot& s : m_slots)
			n += s.running ? 1 : 0;
		return n;
	}

	void onBatch(const SrvFeed::Batch& b) override
	{
		for(Slot& s : m_slots)
		{
			if(!s.running)
				continue;
			try
			{
				s.x->onBatch(b);
			}
			catch(const std::exception& e)
			{
				fail(s, (std::string("onBatch: an exception: ") + e.what()).c_str());
			}
			catch(...)
			{
				fail(s, "onBatch: an exception");
			}
		}
	}

	void beginFrame(const UnitExtrasFrame& f) override
	{
		m_frameId = f.frameId;
		m_place.clear();   // issue 4983 (d): the units to spread are named again for each built frame
		for(Slot& s : m_slots)
		{
			if(!s.running)
				continue;
			std::string why;
			bool ok = false;
			try
			{
				ok = s.x->beginFrame(f, why);
			}
			catch(const std::exception& e)
			{
				why = std::string("an exception: ") + e.what();
			}
			catch(...)
			{
				why = "an exception";
			}
			if(!ok)
			{
				fail(s, ("beginFrame: " + why).c_str());
				continue;
			}
			std::string note;   // issue 4986: an extra's one-time remark (an input it reads only in part)
			if(s.x->takeNote(note) && !note.empty())
				say("units extra %s: %s", s.x->name(), note.c_str());
		}
	}

	void unit(const UnitExtraUnit& u, UnitOverride& io) override
	{
		for(Slot& s : m_slots)
		{
			if(!s.running)
				continue;
			UnitOverride mine = io;
			std::string why;
			bool ok = false;
			try
			{
				ok = s.x->unit(u, mine, why);
			}
			catch(const std::exception& e)
			{
				why = std::string("an exception: ") + e.what();
			}
			catch(...)
			{
				why = "an exception";
			}
			if(ok)
			{
				const char* bad = OutOfRange(mine);
				if(bad)
				{
					ok = false;
					why = std::string("it gave ") + bad;
				}
			}
			if(!ok)
			{
				char at[200];
				snprintf(at, sizeof(at), "unit (%d, %d) at frame %lld: ", u.rec ? u.rec->owner : -1, u.rec ? u.rec->id : -1,
				         (long long)m_frameId);
				fail(s, (at + why).c_str());
				continue;   // io keeps what the extras before it gave
			}
			if((mine.root && !(io.root && mine.rootX == io.rootX && mine.rootY == io.rootY)) ||
			   (mine.heading && !(io.heading && mine.yaw == io.yaw)) || mine.lift != io.lift ||   // lift: issue 4986 (H2)
			   mine.footRadius != io.footRadius)   // issue 4982
				s.c.overrides++;
			if(mine.show && !io.show && u.hiding)
				s.c.shown++;
			if(mine.scale != io.scale)
				s.c.scaled++;
			io = mine;
		}
		// issue 4983 (d): a unit an extra wants spread is handed to the placement decorator (held in a fight: never, the
		// seam ignores its overrides)
		if(io.perimeterEdges && u.rec && !u.heldInFight)
			m_place.set(u.rec->owner, u.rec->id, io.perimeterEdges);
	}

	bool wantsPlacement() const override
	{
		for(const Slot& s : m_slots)
			if(s.running && s.x->wantsPlacement())
				return true;
		return false;
	}

	c5r::units::PlacementSource* placementOver(c5r::units::PlacementSource* inner) override
	{
		if(!inner)
			return nullptr;
		m_place.setInner(inner);
		return &m_place;
	}

	bool animating() const override
	{
		for(const Slot& s : m_slots)
			if(s.running && s.x->animating())
				return true;
		return false;
	}

	void countsText(char* out, size_t n) const override
	{
		if(!n)
			return;
		out[0] = 0;
		size_t used = 0;
		for(const Slot& s : m_slots)
		{
			if(used + 1 >= n)
				break;
			const int w = snprintf(out + used, n - used, " | extras %s %s ov=%llu shown=%llu scaled=%llu failures=%llu", s.x->name(),
			                       s.failed ? "failed" : s.running ? "on" : "off", (unsigned long long)s.c.overrides,
			                       (unsigned long long)s.c.shown, (unsigned long long)s.c.scaled, (unsigned long long)s.c.failures);
			if(w < 0)
				break;
			used += (size_t)w < n - used ? (size_t)w : n - used - 1;
		}
		if(wantsPlacement() && used + 1 < n)   // issue 4983 (d): the decorator's units (only while an extra uses it)
			snprintf(out + used, n - used, " | perimeter spread=%llu failed=%llu", (unsigned long long)m_place.spread(),
			         (unsigned long long)m_place.failed());
	}

	bool countsOf(size_t i, UnitExtraCounts& out, bool& running, bool& failed) const
	{
		if(i >= m_slots.size())
			return false;
		out = m_slots[i].c;
		running = m_slots[i].running;
		failed = m_slots[i].failed;
		return true;
	}

private:
	struct Slot
	{
		std::unique_ptr<UnitExtra> x;
		bool running = false, failed = false;
		UnitExtraCounts c;
	};

	void say(const char* fmt, ...)
	{
		if(!m_log)
			return;
		char line[600];
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(line, sizeof(line), fmt, ap);
		va_end(ap);
		m_log(m_logCtx, line);
	}
	// E6: off for the session, one line; the scene draws stock for what this extra changed
	void fail(Slot& s, const char* why)
	{
		s.c.failures++;
		s.running = false;
		if(s.failed)
			return;
		s.failed = true;
		say("units extra %s: LOUD: failed: %s: off for the session (the scene draws stock for it)", s.x->name(), why);
	}

	std::vector<Slot> m_slots;
	c5r::extras::units::GarrisonPlacement m_place{nullptr};   // issue 4983 (d): given to the host by placementOver
	srvunits::UnitsExtrasLogFn m_log = nullptr;
	void* m_logCtx = nullptr;
	int64_t m_frameId = 0;
};

std::unique_ptr<srvunits::UnitsExtras> MakeLiveUnitsExtras() { return MakeUnitsExtrasImpl(); }

struct Registrar
{
	Registrar() { srvunits::RegisterUnitsExtrasFactory(&MakeLiveUnitsExtras); }
} g_registrar;

}  // namespace

std::unique_ptr<srvunits::UnitsExtras> MakeUnitsExtrasWith(std::vector<std::unique_ptr<UnitExtra>> extras)
{
	return std::unique_ptr<srvunits::UnitsExtras>(new UnitsExtrasModule(std::move(extras)));
}

std::unique_ptr<srvunits::UnitsExtras> MakeUnitsExtrasImpl()
{
	std::vector<std::unique_ptr<UnitExtra>> v;
	for(UnitExtraMaker m : UnitExtraMakers())
		if(m)
			v.push_back(m());
	return MakeUnitsExtrasWith(std::move(v));
}

bool UnitExtraCountsOf(const srvunits::UnitsExtras& m, size_t i, UnitExtraCounts& out, bool& running, bool& failed)
{
	const UnitsExtrasModule* p = dynamic_cast<const UnitsExtrasModule*>(&m);
	return p && p->countsOf(i, out, running, failed);
}

}  // namespace srvextras
