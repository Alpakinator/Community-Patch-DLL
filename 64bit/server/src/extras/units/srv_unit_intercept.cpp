// srv_unit_intercept.cpp - AirInterceptPatrol (epic 0455, issue 4986). See srv_unit_intercept.h.
// Written 2026-11-20 for this repository. SPDX-License-Identifier: GPL-3.0-or-later
#include "srv_unit_intercept.h"

#include <cmath>
#include <cstring>

#include "c5r/heightfield/heightfield.h"
#include "c5r/units/placement.h"
#include "c5r/units/unit_scene.h"

namespace srvextras {

namespace cu = c5r::units;
using srvunits::UnitExtraUnit;
using srvunits::UnitExtrasFrame;
using srvunits::UnitOverride;

namespace {
const float kPi = 3.14159265358979f;
const float kTwoPi = 6.28318530717959f;
// The least height above the ground at the root while flying (a mountain under the ring), world units.
const float kMinClearance = 10.0f;
const double kNoActivityMs = 5000.0;   // frames with a unit table and no feed kind 1000 before the no-render-messages line

float Wrap2Pi(double a)
{
	double w = std::fmod(a, (double)kTwoPi);
	if(w < 0)
		w += kTwoPi;
	return (float)w;
}

float Smooth(float s)
{
	if(s <= 0)
		return 0;
	if(s >= 1)
		return 1;
	return s * s * (3 - 2 * s);
}

// The yaw of a travel direction (dx, dy): its angle + pi/2 (the placement's yaw: facing 1, east (+x), is pi/2).
float YawOf(float dx, float dy, float fallback)
{
	if(!(dx * dx + dy * dy > 1e-12f))
		return fallback;
	return std::atan2(dy, dx) + 0.5f * kPi;
}

int64_t KeyOf(int32_t owner, int32_t id) { return ((int64_t)owner << 32) | (uint32_t)id; }
}  // namespace

PatrolLook PatrolLookA() { return PatrolLook(); }
PatrolLook PatrolLookB()
{
	PatrolLook l;
	l.radiusShare = 1.0f;
	return l;
}
PatrolLook PatrolLookC()
{
	PatrolLook l;
	l.absolute = false;
	l.lift = 40.0f;
	return l;
}
PatrolLook PatrolLookD()
{
	PatrolLook l;
	l.radiusPlots = 1.5f;
	return l;
}

float PatrolPhase(int32_t owner, int32_t id)
{
	// splitmix64 of the key: well spread for ids 1, 2, 3 ... of one owner
	uint64_t z = (uint64_t)KeyOf(owner, id) + 0x9E3779B97F4A7C15ull;
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
	z ^= z >> 31;
	return (float)((double)(z >> 32) / 4294967296.0 * (double)kTwoPi);
}

float RingAngle(float phase, float radius, float speed, double tS)
{
	if(!(radius > 0))
		return Wrap2Pi(phase);
	return Wrap2Pi((double)phase + (double)speed / (double)radius * tS);
}

PatrolPose RingPose(float cx, float cy, float r, float a, float lift)
{
	PatrolPose p;
	p.x = cx + r * std::cos(a);
	p.y = cy + r * std::sin(a);
	// counter-clockwise: the travel direction is the angle + pi/2, so the yaw is the angle + pi
	p.yaw = a + kPi;
	p.lift = lift;
	return p;
}

namespace {
// Quadratic Bezier point and derivative.
void Bezier(float x0, float y0, float x1, float y1, float x2, float y2, float s, float& x, float& y, float& dx, float& dy)
{
	const float u = 1 - s;
	x = u * u * x0 + 2 * u * s * x1 + s * s * x2;
	y = u * u * y0 + 2 * u * s * y1 + s * s * y2;
	dx = 2 * u * (x1 - x0) + 2 * s * (x2 - x1);
	dy = 2 * u * (y1 - y0) + 2 * s * (y2 - y1);
}
}  // namespace

PatrolPose TakeOffPose(float bx, float by, const PatrolPose& end, float arcLen, float s)
{
	s = s < 0 ? 0 : (s > 1 ? 1 : s);
	// the ring's travel direction at the end point: the yaw - pi/2
	const float tx = std::cos(end.yaw - 0.5f * kPi), ty = std::sin(end.yaw - 0.5f * kPi);
	const float x1 = end.x - tx * 0.5f * arcLen, y1 = end.y - ty * 0.5f * arcLen;
	PatrolPose p;
	float dx = 0, dy = 0;
	Bezier(bx, by, x1, y1, end.x, end.y, s, p.x, p.y, dx, dy);
	p.yaw = YawOf(dx, dy, end.yaw);
	p.lift = end.lift * Smooth(s);
	return p;
}

PatrolPose LandingPose(const PatrolPose& start, float bx, float by, float arcLen, float s)
{
	s = s < 0 ? 0 : (s > 1 ? 1 : s);
	const float tx = std::cos(start.yaw - 0.5f * kPi), ty = std::sin(start.yaw - 0.5f * kPi);
	const float x1 = start.x + tx * 0.5f * arcLen, y1 = start.y + ty * 0.5f * arcLen;
	PatrolPose p;
	float dx = 0, dy = 0;
	Bezier(start.x, start.y, x1, y1, bx, by, s, p.x, p.y, dx, dy);
	p.yaw = YawOf(dx, dy, start.yaw);
	p.lift = start.lift * (1 - Smooth(s));
	return p;
}

InterceptChoice ChooseIntercept(const UnitExtraUnit& u, bool patrolling, int32_t range, AirTeam team, bool plotOk)
{
	InterceptChoice c;
	if(!u.rec || u.rec->domain != kDomainAir)
		return c;
	if(!patrolling)
	{
		c.kind = InterceptChoice::NotPatrolling;
		return c;
	}
	if(!IsLiveAircraft(*u.rec))
	{
		c.kind = InterceptChoice::Dead;
		return c;
	}
	if(u.heldInFight)
	{
		c.kind = InterceptChoice::Held;
		return c;
	}
	if(team == AirTeam::NoRecord)
	{
		c.kind = InterceptChoice::NoPlayer;
		return c;
	}
	if(team == AirTeam::Other)
	{
		c.kind = InterceptChoice::Foreign;
		return c;
	}
	if(!plotOk)
	{
		c.kind = InterceptChoice::NoPlot;
		return c;
	}
	if(range <= 0)
	{
		c.kind = InterceptChoice::NoRange;
		return c;
	}
	c.kind = InterceptChoice::Flying;
	return c;
}

const char* const kNoActivityNote =
    "no unit activity message (feed kind 1000) since the load: the DLL sends no render messages (built without "
    "VP64_RENDER_EVENTS), so no aircraft is known to be on intercept and none flies";

std::string PromotionNote(const std::vector<std::string>& types)
{
	if(types.empty())
		return std::string();
	std::string names;
	const size_t kShown = 8;
	for(size_t i = 0; i < types.size() && i < kShown; ++i)
		names += (i ? ", " : "") + types[i];
	if(types.size() > kShown)
		names += ", and " + std::to_string(types.size() - kShown) + " more";
	return std::to_string(types.size()) + " promotion(s) change the intercept range (" + names +
	       "): the ring shows the unit row's base range (Units.AirInterceptRange) only, not a promoted aircraft's reach";
}

namespace {

enum class Mode
{
	Ring = 0,
	TakeOff = 1,
	Landing = 2
};

struct Patrol
{
	Mode mode = Mode::Ring;
	bool t0Pending = false;   // the start time is the next frame's clock
	double t0Ms = 0;
	bool haveLast = false;    // the pose drawn last (a landing starts there)
	PatrolPose last;
	bool haveStart = false;   // a landing's start pose
	PatrolPose start;
};

class AirInterceptPatrol : public UnitExtra
{
public:
	AirInterceptPatrol(Vp64Opt::Id id, const PatrolLook& look, const std::map<int32_t, int32_t>& ranges)
	    : m_id(id), m_look(look), m_ranges(ranges)
	{
	}

	const char* name() const override { return "AirInterceptPatrol"; }
	Vp64Opt::Id id() const override { return m_id; }
	Vp64Opt::ExtraState state(const Vp64Opt::Options& o) const override { return Vp64Opt::Extra(o, m_id); }

	bool configure(const Vp64Opt::Options& o, std::string& why) override
	{
		(void)o;
		if(m_id >= Vp64Opt::kCount || strcmp(Vp64Opt::Definition(m_id).key, name()) != 0)
		{
			why = "its option row is not AirInterceptPatrol";
			return false;
		}
		if(!(m_look.radiusShare > 0) || !(m_look.speed > 0) || !(m_look.arcSeconds > 0) || !(m_look.lift >= 0) ||
		   !std::isfinite(m_look.altitude))
		{
			why = "its look is not usable (radius share, speed and arc time > 0, lift >= 0)";
			return false;
		}
		return true;
	}

	void onBatch(const SrvFeed::Batch& b) override
	{
		for(const SrvFeed::Event& e : b.events)
		{
			if(e.kind == SrvFeed::kUnitActivityChanged)
			{
				m_c.activityEvents++;
				m_sawActivity = true;
				const int64_t k = KeyOf(e.a[0], e.a[1]);
				const int32_t now = e.a[2], was = e.a[3];
				if(now == kActivityIntercept && was == kActivityIntercept)
				{
					// a load's (or a creation's) setup send: on the ring at once
					Patrol& p = m_patrols[k];
					p = Patrol();
					m_c.atLoad++;
				}
				else if(now == kActivityIntercept)
				{
					// the order given (a landing under way starts again from the base: the arcs begin and end there)
					Patrol& p = m_patrols[k];
					p.mode = Mode::TakeOff;
					p.t0Pending = true;
					p.haveStart = false;
					m_c.takeOffs++;
				}
				else if(was == kActivityIntercept)
				{
					std::map<int64_t, Patrol>::iterator it = m_patrols.find(k);
					if(it != m_patrols.end())
					{
						it->second.mode = Mode::Landing;
						it->second.t0Pending = true;
						it->second.haveStart = false;
						m_c.landings++;
					}
				}
			}
			else if(e.kind == SrvFeed::kUnitDestroyed)
				m_patrols.erase(KeyOf(e.a[0], e.a[1]));
			else if(e.kind == SrvFeed::kLoadBegin)
			{
				m_patrols.clear();   // the load's setup sends kind 1000 again for every unit
				m_sawActivity = false;
				m_tableSinceMs = -1;
			}
		}
		CountPatrols();
	}

	bool beginFrame(const UnitExtrasFrame& f, std::string& why) override
	{
		m_grid = f.grid;
		m_players = f.players;
		m_width = f.width;
		m_height = f.height;
		m_tMs = f.tMs;
		if(!f.units || !f.players)
		{
			m_grid = nullptr;
			why = !f.units ? "the scene gives no unit table" : "the scene gives no player table";
			return false;
		}
		// the scene's database (hunk H1) first; the maker's table (tests, a scene without H1) when the frame gives none
		m_rangesNow = f.airInterceptRange && !f.airInterceptRange->empty() ? f.airInterceptRange : &m_ranges;
		// MC #11389: a promotion that changes the intercept range (a mod's; none in VP) is not in the row: said once per list
		if(f.airInterceptPromotions && !f.airInterceptPromotions->empty() && *f.airInterceptPromotions != m_notedPromotions)
		{
			m_notedPromotions = *f.airInterceptPromotions;
			m_notes.push_back(PromotionNote(m_notedPromotions));
		}
		// 2026-11-23 (MC #11423): the activity comes only from a DLL built with VP64_RENDER_EVENTS (feed kind 1000; the 0.1b
		// pins have none). No kind 1000 for kNoActivityMs of frames with a unit table since the load: one line per session.
		if(!m_sawActivity && !m_quietNoted && !f.units->empty())
		{
			if(m_tableSinceMs < 0)
				m_tableSinceMs = f.tMs;
			else if(f.tMs - m_tableSinceMs >= kNoActivityMs)
			{
				m_quietNoted = true;
				m_notes.push_back(kNoActivityNote);
				m_c.noActivityNotes++;
			}
		}
		if(m_rangesNow->empty())
		{
			// E6: the intercept ranges (Units.AirInterceptRange) are an input; without them nothing can be drawn right
			m_grid = nullptr;
			why = "no intercept ranges (Units.AirInterceptRange was not read)";
			return false;
		}
		for(std::map<int64_t, Patrol>::iterator it = m_patrols.begin(); it != m_patrols.end(); ++it)
		{
			if(it->second.t0Pending)
			{
				it->second.t0Pending = false;
				it->second.t0Ms = m_tMs;
			}
		}
		if(!m_grid)
			return true;
		m_c.frames++;
		float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		const c5r::Status a = cu::plotCentre(*m_grid, 0, 0, x0, y0);
		const c5r::Status b = a.ok() ? cu::plotCentre(*m_grid, 1, 0, x1, y1) : a;
		if(!a.ok() || !b.ok())
		{
			m_grid = nullptr;
			why = "the height field gives no plot centre: " + (a.ok() ? b.str() : a.str());
			return false;
		}
		m_step = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
		if(!std::isfinite(m_step) || m_step <= 0)
		{
			m_grid = nullptr;
			why = "the height field's plot centres (0, 0) and (1, 0) coincide";
			return false;
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
		const int64_t k = KeyOf(r.owner, r.id);
		std::map<int64_t, Patrol>::iterator it = m_patrols.find(k);
		const bool plotOk = m_width && m_height && r.plot >= 0 && (uint32_t)r.plot < m_width * m_height;
		std::map<int32_t, int32_t>::const_iterator ri = m_rangesNow->find(r.unitType);
		const int32_t range = ri == m_rangesNow->end() ? -1 : ri->second;
		const InterceptChoice c = ChooseIntercept(u, it != m_patrols.end(), range, AirTeamOf(r, *m_players), plotOk);
		switch(c.kind)
		{
		case InterceptChoice::Dead: m_c.dead++; break;
		case InterceptChoice::Held: m_c.held++; break;
		case InterceptChoice::NoPlayer: m_c.noPlayer++; break;
		case InterceptChoice::Foreign: m_c.foreign++; break;
		case InterceptChoice::NoPlot: m_c.noPlot++; break;
		case InterceptChoice::NoRange: m_c.noRange++; break;
		default: break;
		}
		if(c.kind != InterceptChoice::Flying)
			return true;
		const int32_t bxp = r.plot % (int32_t)m_width, byp = r.plot / (int32_t)m_width;
		float bx = 0, by = 0;
		if(!cu::plotCentre(*m_grid, bxp, byp, bx, by).ok())
		{
			m_c.noPlot++;
			return true;
		}
		Patrol& p = it->second;
		const float radius = (m_look.radiusPlots > 0 ? m_look.radiusPlots : m_look.radiusShare * (float)range) * m_step;
		const float arcLen = m_look.speed * m_look.arcSeconds;
		const float H = FlightHeight(bx, by);   // above the base's ground
		const double tS = m_tMs / 1000.0;
		PatrolPose pose;
		bool drawn = true;
		if(p.mode == Mode::TakeOff)
		{
			const double s = (m_tMs - p.t0Ms) / 1000.0 / m_look.arcSeconds;
			if(s >= 1)
				p.mode = Mode::Ring;
			else
			{
				const double tEnd = p.t0Ms / 1000.0 + m_look.arcSeconds;
				const PatrolPose end = RingPose(bx, by, radius, RingAngle(PatrolPhase(r.owner, r.id), radius, m_look.speed, tEnd), H);
				pose = TakeOffPose(bx, by, end, arcLen, (float)s);
				m_c.takingOff++;
			}
		}
		if(p.mode == Mode::Landing)
		{
			if(!p.haveStart)
			{
				p.start = p.haveLast ? p.last
				                     : RingPose(bx, by, radius, RingAngle(PatrolPhase(r.owner, r.id), radius, m_look.speed,
				                                                          p.t0Ms / 1000.0), H);
				p.haveStart = true;
			}
			const double s = (m_tMs - p.t0Ms) / 1000.0 / m_look.arcSeconds;
			if(s >= 1)
			{
				m_patrols.erase(it);   // landed: stock's parked formation from here on
				CountPatrols();
				drawn = false;
			}
			else
			{
				pose = LandingPose(p.start, bx, by, arcLen, (float)s);
				m_c.landing++;
			}
		}
		else if(p.mode == Mode::Ring)
		{
			pose = RingPose(bx, by, radius, RingAngle(PatrolPhase(r.owner, r.id), radius, m_look.speed, tS), H);
			m_c.onRing++;
		}
		if(!drawn)
			return true;
		p.last = pose;
		p.haveLast = true;
		io.show = true;
		io.root = true;
		io.rootX = pose.x;
		io.rootY = pose.y;
		io.heading = true;
		io.yaw = pose.yaw;
		float lift = AbsoluteLift(pose, bx, by);
		if(lift > srvunits::kMaxUnitExtraLift)
			lift = srvunits::kMaxUnitExtraLift;
		io.lift = lift;   // the seam's hunk H2
		m_c.lastLift = lift;
		m_c.flying++;
		return true;
	}

	bool animating() const override { return !m_patrols.empty(); }

	bool takeNote(std::string& line) override
	{
		if(m_notes.empty())
			return false;
		line.swap(m_notes.front());
		m_notes.erase(m_notes.begin());
		if(line.find("promotion(s)") != std::string::npos)
			m_c.promotionNotes++;
		return true;
	}

	// The flight height above the base plot's ground: the look's absolute altitude less that ground (at least
	// kMinClearance), or its lift.
	float FlightHeight(float bx, float by) const
	{
		if(!m_look.absolute)
			return m_look.lift;
		const float h = m_look.altitude - cu::heightAtWorld(*m_grid, bx, by);
		return h < kMinClearance ? kMinClearance : h;
	}

	// The lift above the ground at the root that keeps the aircraft at (base ground + pose.lift), at least kMinClearance
	// above the ground under it while flying (pose.lift > kMinClearance), for the seam's lift (hunk H2).
	float AbsoluteLift(const PatrolPose& pose, float bx, float by) const
	{
		if(!m_grid || !(pose.lift > 0))
			return 0;
		const float want = cu::heightAtWorld(*m_grid, bx, by) + pose.lift - cu::heightAtWorld(*m_grid, pose.x, pose.y);
		const float floor = pose.lift < kMinClearance ? pose.lift : kMinClearance;
		return want < floor ? floor : want;
	}

	void CountPatrols()
	{
		size_t n = 0;
		for(std::map<int64_t, Patrol>::const_iterator it = m_patrols.begin(); it != m_patrols.end(); ++it)
			if(it->second.mode != Mode::Landing)
				n++;
		m_c.patrols = n;
	}

	InterceptCounts m_c;

private:
	Vp64Opt::Id m_id;
	PatrolLook m_look;
	std::map<int32_t, int32_t> m_ranges;
	const std::map<int32_t, int32_t>* m_rangesNow = &m_ranges;   // this frame's table (the scene's or m_ranges)
	const c5r::heightfield::HeightGrid* m_grid = nullptr;
	const std::vector<SrvFeed::PlayerRecord>* m_players = nullptr;
	uint32_t m_width = 0, m_height = 0;
	double m_tMs = 0;
	float m_step = 0;   // the distance of two neighbouring plot centres
	std::map<int64_t, Patrol> m_patrols;   // (owner, id) -> its patrol (activity 5, or a landing under way)
	std::vector<std::string> m_notedPromotions;   // the promotion list last said (MC #11389)
	std::vector<std::string> m_notes;             // remarks not yet taken by the module (one per frame)
	bool m_sawActivity = false;                   // a feed kind 1000 since the last load (the DLL sends render messages)
	double m_tableSinceMs = -1;                   // the frame clock of the first frame with a unit table and no kind 1000
	bool m_quietNoted = false;                    // the no-render-messages line said (once per session)
};

}  // namespace

std::unique_ptr<UnitExtra> MakeAirInterceptPatrol(Vp64Opt::Id id, const PatrolLook& look, const std::map<int32_t, int32_t>& ranges)
{
	return std::unique_ptr<UnitExtra>(new AirInterceptPatrol(id, look, ranges));
}

bool InterceptCountsOf(const UnitExtra& x, InterceptCounts& out)
{
	const AirInterceptPatrol* p = dynamic_cast<const AirInterceptPatrol*>(&x);
	if(!p)
		return false;
	out = p->m_c;
	return true;
}

}  // namespace srvextras
