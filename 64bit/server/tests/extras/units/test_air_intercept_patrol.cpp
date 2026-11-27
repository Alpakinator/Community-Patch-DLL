// test_air_intercept_patrol.cpp - tests of AirInterceptPatrol (epic 0455, issue 4986; server/src/extras/units/
// srv_unit_intercept.h): the ring and arc geometry, the choice, the scene through the units seam (ring, take-off, landing,
// a load), flag off and E5/E6, a unit held in a fight, and a cost indication. Written 2026-11-20 for this repository.
// SPDX-License-Identifier: GPL-3.0-or-later
// Build: build_units_extras_tests.py --test test_air_intercept_patrol. Run: test_air_intercept_patrol.exe [case ...];
// "CASE <name> PASS|FAIL <why>" per case. Exit: 0 every case passed, 1 a case failed, 2 no case matched. No install.
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "units/srv_units_core.h"
#include "extras/units/srv_units_extras.h"
#include "extras/units/srv_unit_airbases.h"
#include "extras/units/srv_unit_intercept.h"
#include "c5r/heightfield/heightfield.h"
#include "srv_options.h"

// server.exe's SrvOptions() (server/src/srv_options.cpp) for the render units: every option at its default
static Vp64Opt::Options g_options;
const Vp64Opt::Options& SrvOptions() { return g_options; }

using namespace srvunits;
namespace cu = c5r::units;
using srvextras::AirTeam;
using srvextras::InterceptChoice;
using srvextras::InterceptCounts;
using srvextras::PatrolLook;
using srvextras::PatrolPose;

namespace {

std::vector<std::string> g_log;
std::string g_fail;

void Log(const char* line) { g_log.push_back(line); }

#define EXPECT(c)                                                                                                          \
	do                                                                                                                     \
	{                                                                                                                      \
		if(!(c))                                                                                                           \
		{                                                                                                                  \
			char b_[512];                                                                                                  \
			snprintf(b_, sizeof(b_), "line %d: %s", __LINE__, #c);                                                       \
			g_fail = b_;                                                                                                   \
			return false;                                                                                                  \
		}                                                                                                                  \
	} while(0)

const float kPi = 3.14159265358979f;

size_t LogCount(const std::string& needle)
{
	size_t n = 0;
	for(const std::string& l : g_log)
		n += l.find(needle) != std::string::npos;
	return n;
}

bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
// Two yaws equal modulo 2 pi
bool SameYaw(float a, float b, float tol)
{
	float d = std::fmod(a - b, 2 * kPi);
	if(d < -kPi)
		d += 2 * kPi;
	if(d > kPi)
		d -= 2 * kPi;
	return std::fabs(d) <= tol;
}

struct FakePort : FeedPort
{
	std::vector<SrvFeed::UnitRecord> units;
	std::vector<SrvFeed::PlayerRecord> players;
	void copyUnits(std::vector<SrvFeed::UnitRecord>& out, uint32_t& g) override
	{
		out = units;
		g = 0;
	}
	void copyPlayers(std::vector<SrvFeed::PlayerRecord>& out) override { out = players; }
	bool unitDatabase(std::wstring&, uint32_t&) override { return false; }
};

const int kWarrior = 7, kFighter = 8, kBomber = 10;
const int8_t kAir = 1, kLand = 2;

SrvFeed::UnitRecord Rec(int owner, int id, int type, int8_t domain, int plot, uint8_t flags, uint32_t seq)
{
	SrvFeed::UnitRecord r;
	memset(&r, 0, sizeof(r));
	r.owner = (int8_t)owner;
	r.id = id;
	r.unitType = type;
	r.domain = domain;
	r.plot = plot;
	r.facing = 2;
	r.flags = flags;
	r.maxHP = 100;
	r.seq = seq;
	return r;
}

SrvFeed::PlayerRecord Player(int p, int team, int activeTeam)
{
	SrvFeed::PlayerRecord r;
	memset(&r, 0, sizeof(r));
	r.player = (int8_t)p;
	r.team = (int8_t)team;
	r.alive = 1;
	r.colour = 3;
	r.civilization = 5;
	r.era = 0;
	r.artStyle = 5;
	r.activeTeam = activeTeam;
	return r;
}

SrvFeed::Event Ev(uint32_t seq, uint16_t kind, int owner, int id, int a2 = 0, int a3 = 0, int plot = -1)
{
	SrvFeed::Event e;
	memset(&e, 0, sizeof(e));
	e.seq = seq;
	e.kind = kind;
	e.plot = plot;
	e.turn = 1;
	e.a[0] = owner;
	e.a[1] = id;
	e.a[2] = a2;
	e.a[3] = a3;
	return e;
}

SrvFeed::Batch BatchOf(std::vector<SrvFeed::Event> ev)
{
	SrvFeed::Batch b;
	b.events = std::move(ev);
	b.width = 10;
	b.height = 8;
	return b;
}

Lookups TestLookups()
{
	Lookups l;
	l.units = {{kWarrior, "UNIT_WARRIOR"}, {kFighter, "UNIT_FIGHTER"}, {kBomber, "UNIT_BOMBER"}};
	l.colours = {{3, "PLAYERCOLOR_POLYNESIA"}};
	l.civs = {{5, "CIVILIZATION_POLYNESIA"}};
	l.eras = {{0, "ERA_ANCIENT"}};
	l.artStyles = {{5, "ARTSTYLE_POLYNESIAN"}};
	return l;
}

// The ranges the seam's hunk H1 would read from Units.AirInterceptRange (VP: fighter 8, bomber 0)
std::map<int32_t, int32_t> TestRanges() { return {{kFighter, 8}, {kBomber, 0}, {kWarrior, 0}}; }

// A flat synthetic height field (test_scene_units.cpp's lattice): mapW x (mapH + 4) plots of 64 x 64 units, y scaled 0.866,
// height h0. Plot centre (x, y): ((x - (y >> 1)) 64 + 32 y, 64 y 0.866); neighbouring plot centres 64 apart.
c5r::heightfield::HeightGrid Grid(float h0 = 0, uint32_t mapW = 10, uint32_t mapH = 8)
{
	c5r::heightfield::HeightGrid g;
	g.mapW = mapW;
	g.mapH = mapH + 4;
	g.samplesX = g.mapW * 64;
	g.samplesY = g.mapH * 64;
	g.h.assign((size_t)g.samplesX * g.samplesY, h0);
	g.waterZ = -5.0f;
	g.hexToWorld[0] = 64;
	g.hexToWorld[4] = 32;
	g.hexToWorld[5] = 64;
	g.hexToWorld[10] = 1;
	g.hexToWorld[15] = 1;
	return g;
}

// AirInterceptPatrol with its state forced (row 27 lands with the lead's landing commit; until then a stand-in state)
struct ForcedOn : srvextras::UnitExtra
{
	std::unique_ptr<srvextras::UnitExtra> in;
	bool on = true;
	explicit ForcedOn(const PatrolLook& look = srvextras::PatrolLookA(), std::map<int32_t, int32_t> ranges = TestRanges())
	    : in(srvextras::MakeAirInterceptPatrol(Vp64Opt::kCount, look, ranges))
	{
	}
	const char* name() const override { return in->name(); }
	bool configure(const Vp64Opt::Options&, std::string&) override { return true; }
	Vp64Opt::ExtraState state(const Vp64Opt::Options&) const override
	{
		Vp64Opt::ExtraState s;
		s.on = on;
		s.value = on ? 1 : 0;
		s.why = on ? Vp64Opt::kExtraOn : Vp64Opt::kExtraFlagOff;
		s.decidedBy = Vp64Opt::kCount;
		return s;
	}
	void onBatch(const SrvFeed::Batch& b) override { in->onBatch(b); }
	bool beginFrame(const UnitExtrasFrame& f, std::string& why) override { return in->beginFrame(f, why); }
	bool unit(const UnitExtraUnit& u, UnitOverride& io, std::string& why) override { return in->unit(u, io, why); }
	bool animating() const override { return in->animating(); }
	bool takeNote(std::string& line) override { return in->takeNote(line); }
};

std::unique_ptr<UnitsExtras> Module(ForcedOn* x, bool ours = true)
{
	std::vector<std::unique_ptr<srvextras::UnitExtra>> v;
	v.emplace_back(x);
	std::unique_ptr<UnitsExtras> m = srvextras::MakeUnitsExtrasWith(std::move(v));
	m->setLog([](void*, const char* line) { Log(line); }, nullptr);
	m->configure(Vp64Opt::Options(), ours);
	return m;
}

bool CountsOf(ForcedOn* x, InterceptCounts& c) { return x && srvextras::InterceptCountsOf(*x->in, c); }

// The table, a 10 x 8 grid, active team 0. City A at plot 23 = (3, 2), centre (192, 110.848).
//   4001 own fighter in A, visible (after a load stock draws it parked)       -> the patrol when its activity is 5
//   4002 own fighter in A, visible, never on intercept                        -> untouched
//   4003 own bomber in A, activity 5 (range 0: cannot intercept)               -> untouched (no range)
//   4006 warrior in A                                                          -> untouched (not air)
//   2001 fighter of player 1 (team 1), visible, plot 26, activity 5            -> untouched (another team, E7)
//   1001 fighter of player 3 (team 0, the active team's ally), plot 27, act 5  -> the patrol (team, D207)
struct Fixture
{
	FakePort port;
	UnitsCore core;
	c5r::heightfield::HeightGrid grid;
	uint32_t seq = 1;
	explicit Fixture(float h0 = 0) : core(port, Log), grid(Grid(h0))
	{
		const uint8_t v = cu::kFlagVisible;
		port.players = {Player(0, 0, 0), Player(1, 1, 0), Player(2, 2, 0), Player(3, 0, 0)};
		port.units = {Rec(0, 4001, kFighter, kAir, 23, v, 1),  Rec(0, 4002, kFighter, kAir, 23, v, 2),
		              Rec(0, 4003, kBomber, kAir, 23, v, 3),   Rec(0, 4006, kWarrior, kLand, 23, v, 4),
		              Rec(1, 2001, kFighter, kAir, 26, v, 5),  Rec(3, 1001, kFighter, kAir, 27, v, 6)};
		core.setDatabase(TestLookups(), nullptr);
		std::vector<SrvFeed::Event> ev;
		for(const SrvFeed::UnitRecord& u : port.units)
			ev.push_back(Ev(seq++, SrvFeed::kUnitCreated, u.owner, u.id, u.unitType, 0, u.plot));
		core.onBatch(BatchOf(ev), 1000);
	}
	// A batch to the core, which hands it to the module set with setExtras (UnitsCore::onBatch's end); m: unused, kept for
	// the call sites' reading
	void feed(UnitsExtras* m, std::vector<SrvFeed::Event> ev, double tMs)
	{
		(void)m;
		core.onBatch(BatchOf(std::move(ev)), tMs);
	}
};

const cu::UnitInput* Find(const BuiltFrame& f, int owner, int id)
{
	for(const cu::UnitInput& u : f.copies[1].units)
		if(u.player == owner && u.id == id)
			return &u;
	return nullptr;
}

float DistFrom(const cu::UnitInput& u, float x, float y) { return std::sqrt((u.root_x - x) * (u.root_x - x) + (u.root_y - y) * (u.root_y - y)); }

const float kAx = 192.0f, kAy = 128.0f * 0.866f;   // city A's centre (plot (3, 2))
const float kRing = 0.5f * 8 * 64;                   // look (a): half a fighter's range of 8 plots, 64 units per plot

// ------------------------------------------------------------------------------------------------------------- cases
// Ring and arcs without a scene
bool CaseMath()
{
	using namespace srvextras;
	// the phase: stable, in [0, 2 pi), spread
	const float p1 = PatrolPhase(0, 1), p2 = PatrolPhase(0, 2), p3 = PatrolPhase(0, 3);
	EXPECT(p1 == PatrolPhase(0, 1) && p1 >= 0 && p1 < 2 * kPi && p2 >= 0 && p2 < 2 * kPi);
	EXPECT(p1 != p2 && p2 != p3 && p1 != p3);
	int bins[8] = {0};   // 8,000 ids of one owner over 8 sectors: each near 1,000
	for(int i = 0; i < 8000; ++i)
	{
		const float ph = PatrolPhase(0, 1000 + i);
		EXPECT(ph >= 0 && ph < 2 * kPi);
		bins[(std::min)(7, (int)(ph / (2 * kPi) * 8))]++;   // (std::min): windows.h's min macro
	}
	for(int k = 0; k < 8; ++k)
		EXPECT(bins[k] > 850 && bins[k] < 1150);
	EXPECT(!SameYaw(PatrolPhase(0, 4001), PatrolPhase(1, 4001), 0.01f));
	// the angle: speed / radius per second
	EXPECT(Near(RingAngle(0.5f, 100, 20, 0), 0.5f, 1e-6f));
	EXPECT(Near(RingAngle(0.5f, 100, 20, 2.0), 0.9f, 1e-5f));
	EXPECT(RingAngle(0, 100, 20, 1e6) >= 0 && RingAngle(0, 100, 20, 1e6) < 2 * kPi);   // a long session stays wrapped
	// the ring pose: at angle 0 the point east of the centre, travelling north (counter-clockwise), yaw pi
	PatrolPose r = RingPose(10, 20, 100, 0, 50);
	EXPECT(Near(r.x, 110, 1e-4f) && Near(r.y, 20, 1e-4f) && SameYaw(r.yaw, kPi, 1e-5f) && r.lift == 50);
	r = RingPose(10, 20, 100, 0.5f * kPi, 50);   // north of the centre, travelling west: yaw 3 pi / 2 (facing 4, west: -pi/2)
	EXPECT(Near(r.x, 10, 1e-3f) && Near(r.y, 120, 1e-3f) && SameYaw(r.yaw, -0.5f * kPi, 1e-5f));
	// take-off: from the base (lift 0) to the ring point (its lift, its yaw), monotone lift
	const PatrolPose end = RingPose(0, 0, 256, 1.0f, 80);
	PatrolPose a = TakeOffPose(0, 0, end, 144, 0);
	EXPECT(Near(a.x, 0, 1e-4f) && Near(a.y, 0, 1e-4f) && a.lift == 0);
	a = TakeOffPose(0, 0, end, 144, 1);
	EXPECT(Near(a.x, end.x, 1e-3f) && Near(a.y, end.y, 1e-3f) && Near(a.lift, 80, 1e-4f) && SameYaw(a.yaw, end.yaw, 1e-4f));
	float last = -1;
	for(int i = 0; i <= 20; ++i)
	{
		const PatrolPose q = TakeOffPose(0, 0, end, 144, i / 20.0f);
		EXPECT(q.lift >= last && std::isfinite(q.x) && std::isfinite(q.yaw));
		last = q.lift;
	}
	// just before the end the arc moves at the ring's speed (arcLen / seconds = speed): 144 / 6 = 24 units/s
	const PatrolPose a1 = TakeOffPose(0, 0, end, 144, 1.0f - 1.0f / 600), a2 = TakeOffPose(0, 0, end, 144, 1.0f);
	const float v = std::sqrt((a2.x - a1.x) * (a2.x - a1.x) + (a2.y - a1.y) * (a2.y - a1.y)) / 0.01f;
	EXPECT(Near(v, 24, 0.2f));
	// landing: from the ring point to the base, lift to 0
	PatrolPose l = LandingPose(end, 0, 0, 144, 0);
	EXPECT(Near(l.x, end.x, 1e-3f) && Near(l.y, end.y, 1e-3f) && Near(l.lift, 80, 1e-4f) && SameYaw(l.yaw, end.yaw, 1e-4f));
	l = LandingPose(end, 0, 0, 144, 1);
	EXPECT(Near(l.x, 0, 1e-3f) && Near(l.y, 0, 1e-3f) && l.lift == 0);
	// the looks
	EXPECT(PatrolLookA().radiusShare == 0.5f && PatrolLookA().absolute && PatrolLookA().altitude == 90.0f);
	EXPECT(PatrolLookB().radiusShare == 1.0f && PatrolLookB().absolute);
	EXPECT(PatrolLookC().radiusShare == 0.5f && !PatrolLookC().absolute && PatrolLookC().lift == 40.0f);
	return true;
}

UnitExtraUnit XU(const SrvFeed::UnitRecord& r)
{
	UnitExtraUnit u;
	u.rec = &r;
	u.plotX = 3;
	u.plotY = 2;
	u.listed = true;
	u.facing = r.facing;
	return u;
}

bool CaseChoice()
{
	using srvextras::ChooseIntercept;
	const SrvFeed::UnitRecord air = Rec(0, 1, kFighter, kAir, 23, cu::kFlagVisible, 1),
	                          land = Rec(0, 2, kWarrior, kLand, 23, 0, 1), dead = Rec(0, 4, kFighter, kAir, 23, cu::kFlagDead, 1),
	                          dd = Rec(0, 5, kFighter, kAir, 23, cu::kFlagDelayedDeath, 1);
	EXPECT(ChooseIntercept(XU(air), true, 8, AirTeam::Active, true).kind == InterceptChoice::Flying);
	UnitExtraUnit hidden = XU(air);
	hidden.hiding = true;
	hidden.listed = false;
	EXPECT(ChooseIntercept(hidden, true, 8, AirTeam::Active, true).kind == InterceptChoice::Flying);   // shown while flying
	EXPECT(ChooseIntercept(XU(air), false, 8, AirTeam::Active, true).kind == InterceptChoice::NotPatrolling);
	EXPECT(ChooseIntercept(XU(land), true, 8, AirTeam::Active, true).kind == InterceptChoice::NotAircraft);
	EXPECT(ChooseIntercept(XU(dead), true, 8, AirTeam::Active, true).kind == InterceptChoice::Dead);
	EXPECT(ChooseIntercept(XU(dd), true, 8, AirTeam::Active, true).kind == InterceptChoice::Dead);
	UnitExtraUnit held = XU(air);
	held.heldInFight = true;
	EXPECT(ChooseIntercept(held, true, 8, AirTeam::Active, true).kind == InterceptChoice::Held);
	EXPECT(ChooseIntercept(XU(air), true, 8, AirTeam::Other, true).kind == InterceptChoice::Foreign);
	EXPECT(ChooseIntercept(XU(air), true, 8, AirTeam::NoRecord, true).kind == InterceptChoice::NoPlayer);
	EXPECT(ChooseIntercept(XU(air), true, 8, AirTeam::Active, false).kind == InterceptChoice::NoPlot);
	EXPECT(ChooseIntercept(XU(air), true, 0, AirTeam::Active, true).kind == InterceptChoice::NoRange);
	EXPECT(ChooseIntercept(XU(air), true, -1, AirTeam::Active, true).kind == InterceptChoice::NoRange);
	UnitExtraUnit none;
	EXPECT(ChooseIntercept(none, true, 8, AirTeam::Active, true).kind == InterceptChoice::NotAircraft);
	return true;
}

// After a load (kind 1000 with old == new == 5): the own and the team fighter on the ring at once; the rest untouched
bool CaseLoadRing()
{
	g_log.clear();
	Fixture f;
	ForcedOn* x = new ForcedOn();
	std::unique_ptr<UnitsExtras> m = Module(x);
	EXPECT(m->anyOn() && LogCount("units extra AirInterceptPatrol: in effect") == 1);
	f.core.setExtras(m.get());
	f.feed(m.get(),
	       {Ev(f.seq++, SrvFeed::kUnitActivityChanged, 0, 4001, 5, 5, 23), Ev(f.seq++, SrvFeed::kUnitActivityChanged, 0, 4003, 5, 5, 23),
	        Ev(f.seq++, SrvFeed::kUnitActivityChanged, 1, 2001, 5, 5, 26), Ev(f.seq++, SrvFeed::kUnitActivityChanged, 3, 1001, 5, 5, 27),
	        Ev(f.seq++, SrvFeed::kUnitActivityChanged, 0, 4002, 2, 2, 23)},
	       1050);
	EXPECT(m->animating());
	BuiltFrame b;
	f.core.build(nullptr, &f.grid, 1100, b);
	const cu::UnitInput* p = Find(b, 0, 4001);
	EXPECT(p && p->has_root && Near(DistFrom(*p, kAx, kAy), kRing, 0.05f));
	const float a0x = p->root_x, a0y = p->root_y;
	EXPECT(Find(b, 0, 4002) && !Find(b, 0, 4002)->has_root);   // asleep: parked
	EXPECT(Find(b, 0, 4003) && !Find(b, 0, 4003)->has_root);   // a bomber: no intercept range
	EXPECT(Find(b, 1, 2001) && !Find(b, 1, 2001)->has_root);   // another team's fighter: E7
	const cu::UnitInput* t = Find(b, 3, 1001);                  // a team member's fighter (D207): its own ring at (7, 2)
	float tx = 0, ty = 0;
	EXPECT(cu::plotCentre(f.grid, 7, 2, tx, ty).ok());
	EXPECT(t && t->has_root && Near(DistFrom(*t, tx, ty), kRing, 0.05f));
	EXPECT(b.xforms.count(UnitKeyOf(0, 4001)) && b.xforms.at(UnitKeyOf(0, 4001)).heading && !b.xforms.count(UnitKeyOf(1, 2001)));
	// the heading: tangent to the ring, counter-clockwise: yaw = the angle from the centre + pi
	const float ang = std::atan2(a0y - kAy, a0x - kAx);
	EXPECT(SameYaw(b.xforms.at(UnitKeyOf(0, 4001)).yaw, ang + kPi, 1e-3f));
	InterceptCounts n;
	if(CountsOf(x, n))
		printf("  load_ring counts: atLoad %llu patrols %zu flying %llu onRing %llu foreign %llu noRange %llu aircraft %llu frames %llu\n",
		       (unsigned long long)n.atLoad, n.patrols, (unsigned long long)n.flying, (unsigned long long)n.onRing,
		       (unsigned long long)n.foreign, (unsigned long long)n.noRange, (unsigned long long)n.aircraft, (unsigned long long)n.frames);
	EXPECT(CountsOf(x, n) && n.atLoad == 4 && n.patrols == 4 && n.flying == 2 && n.onRing == 2 && n.foreign == 1 && n.noRange == 1);
	// the absolute height: z 90 over ground 0: 90 above the ground, in the frame's xforms (hunk H2)
	EXPECT(Near(n.lastLift, 90, 1e-3f) && Near(b.xforms.at(UnitKeyOf(0, 4001)).lift, 90, 1e-3f));
	// one second later: 24 units further along the ring (counter-clockwise), the same radius
	f.core.build(nullptr, &f.grid, 2100, b);
	p = Find(b, 0, 4001);
	EXPECT(p && Near(DistFrom(*p, kAx, kAy), kRing, 0.05f));
	const float a1 = std::atan2(p->root_y - kAy, p->root_x - kAx);
	float d = a1 - ang;
	if(d < -kPi)
		d += 2 * kPi;
	EXPECT(Near(d * kRing, 24, 0.05f));
	return true;
}

// The order given and taken back: take-off from the base, on the ring, landing back, then parked
bool CaseArcs()
{
	g_log.clear();
	Fixture f(38250);   // samples 38250 x sz 0.2 / 255: ground at 30, the flight height 90 - 30 = 60 above it
	ForcedOn* x = new ForcedOn();
	std::unique_ptr<UnitsExtras> m = Module(x);
	f.core.setExtras(m.get());
	BuiltFrame b;
	f.core.build(nullptr, &f.grid, 1000, b);
	EXPECT(!m->animating() && !Find(b, 0, 4001)->has_root);
	// the order (activity 0 -> 5) at t 10 s: the take-off starts at the next frame's clock
	f.feed(m.get(), {Ev(f.seq++, SrvFeed::kUnitActivityChanged, 0, 4001, 5, 0, 23)}, 9990);
	EXPECT(m->animating());
	f.core.build(nullptr, &f.grid, 10000, b);
	const cu::UnitInput* p = Find(b, 0, 4001);
	InterceptCounts n;
	if(p && CountsOf(x, n))
		printf("  arcs t0: has_root %d dist %.3f takingOff %llu takeOffs %llu onRing %llu lift %.3f\n", (int)p->has_root,
		       DistFrom(*p, kAx, kAy), (unsigned long long)n.takingOff, (unsigned long long)n.takeOffs, (unsigned long long)n.onRing,
		       n.lastLift);
	EXPECT(p && p->has_root && Near(DistFrom(*p, kAx, kAy), 0, 0.05f) && CountsOf(x, n) && n.takingOff == 1 && n.takeOffs == 1 &&
	       Near(n.lastLift, 0, 1e-3f));
	f.core.build(nullptr, &f.grid, 13000, b);   // half way: off the base, below the flight height
	p = Find(b, 0, 4001);
	// (the root may be off the 10 x 8 test grid there, where the ground reads 0: up to 30 + 30 above it)
	EXPECT(p && DistFrom(*p, kAx, kAy) > 20 && CountsOf(x, n) && n.lastLift > 1 && n.lastLift < 60.5f);
	// at the arc's end it is on the ring at the ring's own place for that time
	f.core.build(nullptr, &f.grid, 16000, b);
	p = Find(b, 0, 4001);
	const float ringAt16x = p->root_x, ringAt16y = p->root_y;
	// at world z 90: 90 less the ground under the root (30 on the grid, 0 where the ring leaves the 10 x 8 test grid)
	const float under = cu::heightAtWorld(f.grid, ringAt16x, ringAt16y);
	EXPECT(Near(DistFrom(*p, kAx, kAy), kRing, 0.05f) && CountsOf(x, n) && Near(n.lastLift, 90 - under, 1e-3f));
	const float phase = srvextras::PatrolPhase(0, 4001);
	const PatrolPose ring =
	    srvextras::RingPose(kAx, kAy, kRing, srvextras::RingAngle(phase, kRing, 24, 16.0), 60);
	EXPECT(Near(ringAt16x, ring.x, 0.05f) && Near(ringAt16y, ring.y, 0.05f));
	// a frame a little before the end is within one step of the ring point (no jump at the hand-over)
	Fixture g(38250);
	ForcedOn* y = new ForcedOn();
	std::unique_ptr<UnitsExtras> mg = Module(y);
	g.core.setExtras(mg.get());
	g.feed(mg.get(), {Ev(g.seq++, SrvFeed::kUnitActivityChanged, 0, 4001, 5, 0, 23)}, 9990);
	BuiltFrame bg;
	g.core.build(nullptr, &g.grid, 10000, bg);
	g.core.build(nullptr, &g.grid, 15950, bg);
	EXPECT(DistFrom(*Find(bg, 0, 4001), ringAt16x, ringAt16y) < 1.5f);
	// the order ends (5 -> 0) at t 30 s: the landing starts where it was last drawn and ends at the base, then parked
	f.core.build(nullptr, &f.grid, 30000, b);
	const float lastX = Find(b, 0, 4001)->root_x, lastY = Find(b, 0, 4001)->root_y;
	f.feed(m.get(), {Ev(f.seq++, SrvFeed::kUnitActivityChanged, 0, 4001, 0, 5, 23)}, 30010);
	f.core.build(nullptr, &f.grid, 30020, b);
	p = Find(b, 0, 4001);
	EXPECT(p && p->has_root && DistFrom(*p, lastX, lastY) < 1.0f && CountsOf(x, n) && n.landing == 1 && n.landings == 1);
	f.core.build(nullptr, &f.grid, 36000, b);   // just before the end: almost at the base, almost down
	p = Find(b, 0, 4001);
	// (the arc reaches the base at about 2 |P2 - P1| / 6 s, 88 units/s here: 20 ms before the end it is under 3 units out)
	EXPECT(p && p->has_root && DistFrom(*p, kAx, kAy) < 3.0f && CountsOf(x, n) && n.lastLift < 0.5f);
	f.core.build(nullptr, &f.grid, 36100, b);   // landed: stock's parked formation
	p = Find(b, 0, 4001);
	EXPECT(p && !p->has_root && !m->animating() && CountsOf(x, n) && n.patrols == 0);
	return true;
}

// E2: the flag off (or the real extra with no row) is the scene with no module; E5; E6 (no ranges)
bool CaseOff()
{
	g_log.clear();
	auto run = [](UnitsExtras* m, BuiltFrame& b) {
		Fixture f;
		if(m)
			f.core.setExtras(m);
		f.feed(m, {Ev(f.seq++, SrvFeed::kUnitActivityChanged, 0, 4001, 5, 5, 23)}, 1050);
		f.core.build(nullptr, &f.grid, 1100, b);
	};
	BuiltFrame plain, off, real;
	run(nullptr, plain);
	ForcedOn* x = new ForcedOn();
	x->on = false;
	std::unique_ptr<UnitsExtras> m = Module(x);
	EXPECT(!m->anyOn() && LogCount("units AirInterceptPatrol: off:") == 1);
	run(m.get(), off);
	std::vector<std::unique_ptr<srvextras::UnitExtra>> v;
	v.emplace_back(srvextras::MakeAirInterceptPatrol(Vp64Opt::kCount));
	std::unique_ptr<UnitsExtras> mr = srvextras::MakeUnitsExtrasWith(std::move(v));
	mr->setLog([](void*, const char* line) { Log(line); }, nullptr);
	mr->configure(Vp64Opt::Options(), true);
	EXPECT(!mr->anyOn());
	run(mr.get(), real);
	for(const BuiltFrame* o : {&off, &real})
	{
		EXPECT(o->copies[1].units.size() == plain.copies[1].units.size() && o->xforms.empty() && o->listed == plain.listed);
		for(size_t i = 0; i < plain.copies[1].units.size(); ++i)
		{
			const cu::UnitInput &a = plain.copies[1].units[i], &c = o->copies[1].units[i];
			EXPECT(a.id == c.id && a.has_root == c.has_root && a.root_x == c.root_x && a.root_y == c.root_y &&
			       a.formation_seed == c.formation_seed && a.fade == c.fade);
		}
	}
	// E5: ServerUnits = 0
	g_log.clear();
	std::unique_ptr<UnitsExtras> m5 = Module(new ForcedOn(), false);
	EXPECT(!m5->anyOn() && LogCount("units extra AirInterceptPatrol: off: ServerUnits = 0") == 1 && g_log.size() >= 1);
	// E6: no intercept ranges: one LOUD line, off for the session, the scene stock
	g_log.clear();
	Fixture f;
	std::unique_ptr<UnitsExtras> m6 = Module(new ForcedOn(srvextras::PatrolLookA(), std::map<int32_t, int32_t>()));
	f.core.setExtras(m6.get());
	f.feed(m6.get(), {Ev(f.seq++, SrvFeed::kUnitActivityChanged, 0, 4001, 5, 5, 23)}, 1050);
	BuiltFrame b;
	f.core.build(nullptr, &f.grid, 1100, b);
	f.core.build(nullptr, &f.grid, 1200, b);
	EXPECT(LogCount("units extra AirInterceptPatrol: LOUD: failed: beginFrame: no intercept ranges") == 1);
	EXPECT(Find(b, 0, 4001) && !Find(b, 0, 4001)->has_root && b.xforms.empty());
	return true;
}

// Hunk H1: the ranges from the scene's database (Lookups::airInterceptRange) when the maker gives none
bool CaseDbRanges()
{
	g_log.clear();
	Fixture f;
	Lookups l = TestLookups();
	l.airInterceptRange = {{kFighter, 4}, {kBomber, 0}};   // a range of 4: a ring of 0.5 x 4 x 64 = 128
	f.core.setDatabase(l, nullptr);
	ForcedOn* x = new ForcedOn(srvextras::PatrolLookA(), std::map<int32_t, int32_t>());
	std::unique_ptr<UnitsExtras> m = Module(x);
	f.core.setExtras(m.get());
	f.feed(m.get(), {Ev(f.seq++, SrvFeed::kUnitActivityChanged, 0, 4001, 5, 5, 23)}, 1050);
	BuiltFrame b;
	f.core.build(nullptr, &f.grid, 1100, b);
	const cu::UnitInput* p = Find(b, 0, 4001);
	EXPECT(p && p->has_root && Near(DistFrom(*p, kAx, kAy), 128, 0.05f));
	EXPECT(LogCount("LOUD") == 0);
	return true;
}

// MC #11389: a promotion that changes the intercept range (a mod's; none in VP) is said in one line by the module, once per
// list, and nothing else changes (the ring stays at the row's range); no list or an unread column: no line
bool CasePromotionNote()
{
	using namespace srvextras;
	EXPECT(PromotionNote(std::vector<std::string>()).empty());
	const std::string one = PromotionNote({"PROMOTION_MOD_RADAR"});
	EXPECT(one.find("1 promotion(s) change the intercept range (PROMOTION_MOD_RADAR)") == 0 &&
	       one.find("Units.AirInterceptRange") != std::string::npos);
	std::vector<std::string> many;
	for(int i = 0; i < 10; ++i)
		many.push_back("PROMOTION_P" + std::to_string(i));
	const std::string ten = PromotionNote(many);
	EXPECT(ten.find("10 promotion(s)") == 0 && ten.find("PROMOTION_P7, and 2 more)") != std::string::npos &&
	       ten.find("PROMOTION_P8") == std::string::npos);
	const char* kLine = "units extra AirInterceptPatrol: 1 promotion(s) change the intercept range (PROMOTION_MOD_RADAR)";
	for(int arm = 0; arm < 3; ++arm)   // 0: a promotion listed; 1: none listed; 2: the column not read (Why set)
	{
		g_log.clear();
		Fixture f;
		Lookups l = TestLookups();
		l.airInterceptRange = {{kFighter, 8}, {kBomber, 0}};
		if(arm == 0)
			l.airInterceptPromotions = {"PROMOTION_MOD_RADAR"};
		if(arm == 2)
		{
			l.airInterceptPromotions = {"PROMOTION_MOD_RADAR"};
			l.airInterceptPromotionsWhy = "table UnitPromotions (Type, AirInterceptRangeChange): no such column";
		}
		f.core.setDatabase(l, nullptr);
		ForcedOn* x = new ForcedOn(PatrolLookA(), std::map<int32_t, int32_t>());
		std::unique_ptr<UnitsExtras> m = Module(x);
		f.core.setExtras(m.get());
		f.feed(m.get(), {Ev(f.seq++, SrvFeed::kUnitActivityChanged, 0, 4001, 5, 5, 23)}, 1050);
		for(int k = 0; k < 3; ++k)
		{
			BuiltFrame b;
			f.core.build(nullptr, &f.grid, 1100 + 100 * k, b);
			const cu::UnitInput* p = Find(b, 0, 4001);
			EXPECT(p && p->has_root && Near(DistFrom(*p, kAx, kAy), kRing, 0.05f));   // the row's range in every arm
		}
		EXPECT(LogCount(kLine) == (arm == 0 ? 1u : 0u));
		EXPECT(LogCount("promotion(s)") == (arm == 0 ? 1u : 0u) && LogCount("LOUD") == 0);
		InterceptCounts c;
		EXPECT(CountsOf(x, c) && c.promotionNotes == (arm == 0 ? 1u : 0u));
	}
	return true;
}

// MC #11423: a DLL without render messages (the 0.1b pins) sends no kind 1000: after 5 s of frames with a unit table the
// module logs one line per session (not LOUD) and nothing flies; a kind 1000 before that (a define-on DLL): no line
bool CaseNoActivityNote()
{
	using namespace srvextras;
	const std::string kLine = std::string("units extra AirInterceptPatrol: ") + kNoActivityNote;
	for(int arm = 0; arm < 2; ++arm)   // 0: no kind 1000 at all; 1: one kind 1000 (another unit, activity 2 <- 0) first
	{
		g_log.clear();
		Fixture f;
		Lookups l = TestLookups();
		l.airInterceptRange = {{kFighter, 8}, {kBomber, 0}};
		f.core.setDatabase(l, nullptr);
		ForcedOn* x = new ForcedOn(PatrolLookA(), std::map<int32_t, int32_t>());
		std::unique_ptr<UnitsExtras> m = Module(x);
		f.core.setExtras(m.get());
		if(arm == 1)
			f.feed(m.get(), {Ev(f.seq++, SrvFeed::kUnitActivityChanged, 0, 4006, 2, 0, 23)}, 1050);
		const double ts[] = {1100, 3000, 6000, 6200, 9000, 20000};
		for(double t : ts)
		{
			BuiltFrame b;
			f.core.build(nullptr, &f.grid, t, b);
			const cu::UnitInput* p = Find(b, 0, 4001);
			EXPECT(!p || !p->has_root || DistFrom(*p, kAx, kAy) < 1.0f);   // nothing flies
			if(arm == 0 && t == 6000)
				EXPECT(LogCount(kLine) == 0);   // 4.9 s after the first frame with the table: not yet
		}
		EXPECT(LogCount(kLine) == (arm == 0 ? 1u : 0u) && LogCount("LOUD") == 0);
		InterceptCounts c;
		EXPECT(CountsOf(x, c) && c.noActivityNotes == (arm == 0 ? 1u : 0u) && c.flying == 0);
		// a load again: the line stays said (one per session)
		if(arm == 0)
		{
			f.feed(m.get(), {Ev(f.seq++, SrvFeed::kLoadBegin, 0, 0, 0, 0, -1)}, 21000);
			for(double t : {22000.0, 30000.0})
			{
				BuiltFrame b;
				f.core.build(nullptr, &f.grid, t, b);
			}
			EXPECT(LogCount(kLine) == 1);
		}
	}
	return true;
}

// Look (d): a fixed ring of 1.5 plots (96 units) whatever the range; a range of 0 still keeps the aircraft parked
bool CaseFixedRing()
{
	g_log.clear();
	Fixture f;
	Lookups l = TestLookups();
	l.airInterceptRange = {{kFighter, 8}, {kBomber, 0}};
	f.core.setDatabase(l, nullptr);
	ForcedOn* x = new ForcedOn(srvextras::PatrolLookD(), std::map<int32_t, int32_t>());
	std::unique_ptr<UnitsExtras> m = Module(x);
	f.core.setExtras(m.get());
	f.feed(m.get(), {Ev(f.seq++, SrvFeed::kUnitActivityChanged, 0, 4001, 5, 5, 23)}, 1050);
	BuiltFrame b;
	f.core.build(nullptr, &f.grid, 1100, b);
	const cu::UnitInput* p = Find(b, 0, 4001);
	EXPECT(p && p->has_root && Near(DistFrom(*p, kAx, kAy), 96, 0.05f));
	EXPECT(LogCount("LOUD") == 0);
	EXPECT(srvextras::PatrolLookD().radiusPlots == 1.5f && srvextras::PatrolLookD().absolute &&
	       srvextras::PatrolLookD().altitude == 90.0f);
	return true;
}

// A unit held in a fight: no override; on the ring again once released
bool CaseHeld()
{
	Fixture f;
	ForcedOn* x = new ForcedOn();
	std::unique_ptr<UnitsExtras> m = Module(x);
	f.core.setExtras(m.get());
	f.feed(m.get(), {Ev(f.seq++, SrvFeed::kUnitActivityChanged, 0, 4001, 5, 5, 23)}, 1050);
	f.core.setHeldInFight(0, 4001, true);
	BuiltFrame b;
	f.core.build(nullptr, &f.grid, 1100, b);
	InterceptCounts n;
	EXPECT(Find(b, 0, 4001) && !Find(b, 0, 4001)->has_root && f.core.counters().xHeld == 0 && CountsOf(x, n) && n.held == 1);
	f.core.setHeldInFight(0, 4001, false);
	f.core.build(nullptr, &f.grid, 1200, b);
	EXPECT(Find(b, 0, 4001) && Find(b, 0, 4001)->has_root);
	// a kill (kind 14) ends the patrol
	f.feed(m.get(), {Ev(f.seq++, SrvFeed::kUnitDestroyed, 0, 4001, 0, 0, 23)}, 1250);
	EXPECT(!m->animating());
	// a load (kind 32) forgets every patrol: the setup's sends start them again
	f.feed(m.get(), {Ev(f.seq++, SrvFeed::kUnitActivityChanged, 3, 1001, 5, 5, 27)}, 1300);
	EXPECT(m->animating());
	f.feed(m.get(), {Ev(f.seq++, SrvFeed::kLoadBegin, 0, 0)}, 1350);
	EXPECT(!m->animating());
	return true;
}

// E8 indication only (not a result, PERFORMANCE.md): a 2,000-unit build with 200 own fighters on intercept, without a
// module and with the extra on
bool CaseCost()
{
	const int n = 2000, planes = 200, reps = 41;
	auto make = [&](Fixture& f) {
		f.port.units.clear();
		std::vector<SrvFeed::Event> ev;
		for(int i = 0; i < n; ++i)
		{
			const bool plane = i < planes;
			const int plot = (i * 7) % 80;
			f.port.units.push_back(Rec(0, 10000 + i, plane ? kFighter : kWarrior, plane ? kAir : kLand, plot, cu::kFlagVisible, (uint32_t)i + 1));
			ev.push_back(Ev(f.seq++, SrvFeed::kUnitCreated, 0, 10000 + i, plane ? kFighter : kWarrior, 0, plot));
		}
		f.core.onBatch(BatchOf(ev), 1000);
	};
	Fixture a, c;
	make(a);
	make(c);
	ForcedOn* x = new ForcedOn();
	std::unique_ptr<UnitsExtras> m = Module(x);
	c.core.setExtras(m.get());
	std::vector<SrvFeed::Event> act;
	for(int i = 0; i < planes; ++i)
		act.push_back(Ev(c.seq++, SrvFeed::kUnitActivityChanged, 0, 10000 + i, 5, 5, (i * 7) % 80));
	m->onBatch(BatchOf(act));
	std::vector<double> ta, tc;
	for(int r = 0; r < reps; ++r)
	{
		BuiltFrame b;
		auto t0 = std::chrono::steady_clock::now();
		a.core.build(nullptr, &a.grid, 1100 + r * 16, b);
		auto t1 = std::chrono::steady_clock::now();
		c.core.build(nullptr, &c.grid, 1100 + r * 16, b);
		auto t2 = std::chrono::steady_clock::now();
		ta.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
		tc.push_back(std::chrono::duration<double, std::micro>(t2 - t1).count());
	}
	std::sort(ta.begin(), ta.end());
	std::sort(tc.begin(), tc.end());
	InterceptCounts k;
	EXPECT(CountsOf(x, k) && k.flying == (uint64_t)planes * reps);
	printf("cost (indication only): %d units, %d on intercept, %d builds per arm: median without %.1f us, with the extra on %.1f us\n", n,
	       planes, reps, ta[reps / 2], tc[reps / 2]);
	return true;
}

struct Case
{
	const char* name;
	bool (*fn)();
};
const Case kCases[] = {{"intercept_math", CaseMath},   {"intercept_choice", CaseChoice}, {"intercept_load_ring", CaseLoadRing},
                       {"intercept_arcs", CaseArcs},   {"intercept_off", CaseOff},       {"intercept_db_ranges", CaseDbRanges},
                       {"intercept_held", CaseHeld},   {"intercept_fixed_ring", CaseFixedRing},
                       {"intercept_promotion_note", CasePromotionNote}, {"intercept_no_activity_note", CaseNoActivityNote},
                       {"intercept_cost", CaseCost}};

}  // namespace

int main(int argc, char** argv)
{
	int run = 0, passed = 0;
	for(const Case& c : kCases)
	{
		bool want = argc < 2;
		for(int i = 1; i < argc; ++i)
			want |= strcmp(argv[i], c.name) == 0;
		if(!want)
			continue;
		run++;
		g_fail.clear();
		bool ok = false;
		try
		{
			ok = c.fn();
		}
		catch(const std::exception& e)
		{
			g_fail = std::string("exception: ") + e.what();
		}
		if(ok)
			passed++;
		printf("CASE %s %s%s%s\n", c.name, ok ? "PASS" : "FAIL", ok ? "" : " ", ok ? "" : g_fail.c_str());
	}
	if(!run)
	{
		printf("test_air_intercept_patrol: no case matches\n");
		return 2;
	}
	printf("test_air_intercept_patrol: %d of %d cases passed\n", passed, run);
	return passed == run ? 0 : 1;
}
