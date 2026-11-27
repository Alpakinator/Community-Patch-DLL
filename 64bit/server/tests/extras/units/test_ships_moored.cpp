// test_ships_moored.cpp - tests of ShipsMoored (epic 0455, issue 4984; server/src/extras/units/srv_unit_moored.h): the
// choice, the row on the water side for each look, the scene through the units seam, flag off and E5/E6, held and walking
// ships, and a cost indication. Written 2026-11-20 for this repository. SPDX-License-Identifier: GPL-3.0-or-later
// Build: build_units_extras_tests.py --test test_ships_moored. Run: test_ships_moored.exe [case ...]; "CASE <name>
// PASS|FAIL <why>" per case. Exit: 0 every case passed, 1 a case failed, 2 no case matched. No install, no device.
#include <windows.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "units/srv_units_core.h"
#include "extras/units/srv_units_extras.h"
#include "extras/units/srv_unit_moored.h"
#include "c5r/heightfield/heightfield.h"
#include "srv_options.h"

// server.exe's SrvOptions() (server/src/srv_options.cpp) for the render units: every option at its default
static Vp64Opt::Options g_options;
const Vp64Opt::Options& SrvOptions() { return g_options; }

using namespace srvunits;
namespace cu = c5r::units;
using srvextras::MooredChoice;
using srvextras::MooredCounts;
using srvextras::MooredLook;
using srvextras::MooredSlot;

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

size_t LogCount(const std::string& needle)
{
	size_t n = 0;
	for(const std::string& l : g_log)
		n += l.find(needle) != std::string::npos;
	return n;
}

bool Near(float a, float b, float tol = 1e-3f) { return std::fabs(a - b) <= tol; }

const float kPi = 3.14159265358979323846f;

// the yaw's angle folded into (-pi, pi]
float Fold(float a)
{
	while(a > kPi)
		a -= 2 * kPi;
	while(a <= -kPi)
		a += 2 * kPi;
	return a;
}
bool NearAngle(float a, float b) { return std::fabs(Fold(a - b)) <= 1e-3f; }

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

const int kWarrior = 7, kTrireme = 10, kCargo = 11;
const int8_t kSea = 0, kLand = 2;

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
	l.units = {{kWarrior, "UNIT_WARRIOR"}, {kTrireme, "UNIT_TRIREME"}, {kCargo, "UNIT_CARGO_SHIP"}};
	l.colours = {{3, "PLAYERCOLOR_POLYNESIA"}};
	l.civs = {{5, "CIVILIZATION_POLYNESIA"}};
	l.eras = {{0, "ERA_ANCIENT"}};
	l.artStyles = {{5, "ARTSTYLE_POLYNESIAN"}};
	return l;
}

// The synthetic height field of test_scene_units.cpp's lattice: mapW x (mapH + 4) plots of 64 x 64 samples, world x =
// column, world y = row x 0.866. Plot centre (x, y): ((x - (y >> 1)) 64 + 32 y, 64 y 0.866); the inner radius 32. The
// water level is 1.0; land samples stand at 2.0 (h 2550), water samples at 0. `water(x, y)` says per sample (its world
// position) whether it is water.
const float kCx = 192.0f, kCy = 128.0f * 0.866f, kInner = 32.0f;   // plot (3, 2) = index 23 on the 10-wide feed grid
const int kCityPlot = 23;

c5r::heightfield::HeightGrid Grid(const std::function<bool(float, float)>& water, uint32_t mapW = 10, uint32_t mapH = 8)
{
	c5r::heightfield::HeightGrid g;
	g.mapW = mapW;
	g.mapH = mapH + 4;
	g.samplesX = g.mapW * 64;
	g.samplesY = g.mapH * 64;
	g.h.assign((size_t)g.samplesX * g.samplesY, 2550);
	g.waterZ = 1.0f;
	g.hexToWorld[0] = 64;
	g.hexToWorld[4] = 32;
	g.hexToWorld[5] = 64;
	g.hexToWorld[10] = 1;
	g.hexToWorld[15] = 1;
	for(uint32_t r = 0; r < g.samplesY; ++r)
		for(uint32_t c = 0; c < g.samplesX; ++c)
			if(water((float)c * g.sx - g.ox, (float)r * g.sy - g.oy))
				g.h[g.index(c, r)] = 0;
	return g;
}
// The sea south of the city: every sample more than 20 units south of the city plot's centre
c5r::heightfield::HeightGrid SouthSea()
{
	return Grid([](float, float y) { return y < kCy - 20.0f; });
}

// ShipsMoored with its state forced (the option row 25 comes with the rows of epic 0455, W3's hunk; once the row exists the
// module asks Vp64Opt::Extra and the cases that need "on" keep this wrapper)
struct ForcedOn : srvextras::UnitExtra
{
	std::unique_ptr<srvextras::UnitExtra> in;
	bool on = true;
	bool forwardConfigure = false;   // true: the inner extra's own configure (its row check) runs
	explicit ForcedOn(MooredLook look = MooredLook::BowToLand, Vp64Opt::Id id = Vp64Opt::kCount)
	    : in(srvextras::MakeShipsMoored(id, look))
	{
	}
	const char* name() const override { return in->name(); }
	bool configure(const Vp64Opt::Options& o, std::string& why) override { return forwardConfigure ? in->configure(o, why) : true; }
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
};

std::unique_ptr<UnitsExtras> MooredModule(ForcedOn** out = nullptr, bool ours = true, bool on = true,
                                          MooredLook look = MooredLook::BowToLand)
{
	ForcedOn* x = new ForcedOn(look);
	x->on = on;
	if(out)
		*out = x;
	std::vector<std::unique_ptr<srvextras::UnitExtra>> v;
	v.emplace_back(x);
	std::unique_ptr<UnitsExtras> m = srvextras::MakeUnitsExtrasWith(std::move(v));
	m->setLog([](void*, const char* line) { Log(line); }, nullptr);
	m->configure(Vp64Opt::Options(), ours);
	return m;
}

bool CountsOf(ForcedOn* x, MooredCounts& c) { return x && srvextras::MooredCountsOf(*x->in, c); }

// The port table, a 10 x 8 grid, the sea south of the city at plot 23 = (3, 2), active team 0:
//   5001 trireme in the city, garrisoned, hidden (stock: a ship in a city is not drawn)     -> shown, moored, row slot 0
//   5002 cargo ship in the city, listed (stock draws it at the plot centre, on land)         -> moored, row slot 1
//   5003 trireme at sea, plot 3 = (3, 0), listed                                             -> untouched
//   5004 dead trireme in the city, hidden (fading out by a kill)                             -> untouched
//   5005 warrior in the city, hidden                                                         -> untouched (not a ship)
//   5006 cargo ship with the delayed-death flag in the city, hidden                          -> untouched
struct PortFixture
{
	FakePort port;
	UnitsCore core;
	c5r::heightfield::HeightGrid grid = SouthSea();
	uint32_t seq = 1;
	PortFixture() : core(port, Log)
	{
		const uint8_t v = cu::kFlagVisible;
		port.players = {Player(0, 0, 0), Player(1, 1, 0)};
		port.units = {Rec(0, 5001, kTrireme, kSea, kCityPlot, cu::kFlagGarrisoned, 1),
		              Rec(0, 5002, kCargo, kSea, kCityPlot, v, 2),
		              Rec(0, 5003, kTrireme, kSea, 3, v, 3),
		              Rec(0, 5004, kTrireme, kSea, kCityPlot, cu::kFlagDead, 4),
		              Rec(0, 5005, kWarrior, kLand, kCityPlot, 0, 5),
		              Rec(0, 5006, kCargo, kSea, kCityPlot, cu::kFlagDelayedDeath, 6)};
		core.setDatabase(TestLookups(), nullptr);
		std::vector<SrvFeed::Event> ev;
		for(const SrvFeed::UnitRecord& u : port.units)
			ev.push_back(Ev(seq++, SrvFeed::kUnitCreated, u.owner, u.id, u.unitType, 0, u.plot));
		core.onBatch(BatchOf(ev), 1000);
	}
};

const cu::UnitInput* Find(const BuiltFrame& f, int owner, int id)
{
	for(const cu::UnitInput& u : f.copies[1].units)
		if(u.player == owner && u.id == id)
			return &u;
	return nullptr;
}

bool SameScene(const cu::UnitScene& a, const cu::UnitScene& b)
{
	if(a.units.size() != b.units.size() || a.players.size() != b.players.size())
		return false;
	for(size_t i = 0; i < a.units.size(); ++i)
	{
		const cu::UnitInput &x = a.units[i], &y = b.units[i];
		if(x.player != y.player || x.id != y.id || x.unit_type != y.unit_type || x.plot_x != y.plot_x || x.plot_y != y.plot_y ||
		   x.has_root != y.has_root || x.root_x != y.root_x || x.root_y != y.root_y || x.facing != y.facing || x.flags != y.flags ||
		   x.fade_from != y.fade_from || x.fade_to != y.fade_to || x.fade != y.fade || x.formation_seed != y.formation_seed)
			return false;
	}
	return true;
}

UnitExtraUnit XU(const SrvFeed::UnitRecord& r, bool hiding = true, int px = 3, int py = 2)
{
	UnitExtraUnit u;
	u.rec = &r;
	u.plotX = px;
	u.plotY = py;
	u.hiding = hiding;
	u.listed = !hiding;
	u.facing = r.facing;
	return u;
}

// ------------------------------------------------------------------------------------------------------------- cases
// The choice without a scene; the looks' facings and gaps
bool CaseChoice()
{
	using srvextras::ChooseMoored;
	const SrvFeed::UnitRecord ship = Rec(0, 1, kTrireme, kSea, kCityPlot, 0, 1), land = Rec(0, 2, kWarrior, kLand, kCityPlot, 0, 1),
	                          dead = Rec(0, 3, kTrireme, kSea, kCityPlot, cu::kFlagDead, 1),
	                          dd = Rec(0, 4, kTrireme, kSea, kCityPlot, cu::kFlagDelayedDeath, 1),
	                          emb = Rec(0, 5, kWarrior, kLand, kCityPlot, cu::kFlagEmbarked, 1);
	MooredSlot slot;
	slot.x = 10;
	slot.y = 20;
	slot.yaw = 1.5f;
	slot.scale = 0.8f;
	MooredChoice c = ChooseMoored(XU(ship), true, false, &slot);
	EXPECT(c.kind == MooredChoice::Placed && c.show && c.root && c.slot.x == 10 && c.slot.y == 20 && c.slot.yaw == 1.5f &&
	       c.slot.scale == 0.8f);
	c = ChooseMoored(XU(ship, false), true, false, &slot);   // listed by stock: moored, no show needed
	EXPECT(c.kind == MooredChoice::Placed && !c.show && c.root);
	c = ChooseMoored(XU(ship), false, false, &slot);         // not on a port plot (at sea)
	EXPECT(c.kind == MooredChoice::NotShip && !c.show && !c.root);
	c = ChooseMoored(XU(land), true, false, &slot);
	EXPECT(c.kind == MooredChoice::NotShip && !c.show);
	c = ChooseMoored(XU(emb), true, false, &slot);           // an embarked land unit is land domain
	EXPECT(c.kind == MooredChoice::NotShip && !c.show);
	c = ChooseMoored(XU(dead), true, false, &slot);
	EXPECT(c.kind == MooredChoice::NotShip && !c.show);
	c = ChooseMoored(XU(dd), true, false, &slot);
	EXPECT(c.kind == MooredChoice::NotShip && !c.show);
	UnitExtraUnit held = XU(ship);
	held.heldInFight = true;
	c = ChooseMoored(held, true, false, &slot);
	EXPECT(c.kind == MooredChoice::Held && !c.show && !c.root);
	c = ChooseMoored(XU(ship), true, true, &slot);
	EXPECT(c.kind == MooredChoice::Walking && c.show && !c.root);
	c = ChooseMoored(XU(ship), true, false, nullptr);
	EXPECT(c.kind == MooredChoice::NoWaterSide && !c.show && !c.root);   // left as stock has it (never on land)
	c = ChooseMoored(XU(ship), true, true, nullptr);
	EXPECT(c.kind == MooredChoice::NoWaterSide && !c.show && !c.root);
	UnitExtraUnit none;
	c = ChooseMoored(none, true, false, &slot);
	EXPECT(c.kind == MooredChoice::NotShip && !c.show);
	EXPECT(srvextras::IsLiveShip(ship) && !srvextras::IsLiveShip(dead) && !srvextras::IsLiveShip(dd) && !srvextras::IsLiveShip(land));
	EXPECT(srvextras::kDomainSea == 0 && srvextras::kMooredWalkShare == 0.02f);
	// the facings (world angles) on the south side (-90 degrees)
	const float s = -0.5f * kPi;
	EXPECT(NearAngle(srvextras::MooredFacing(MooredLook::BowToLand, s), 0.5f * kPi));       // the bow north, to the city
	EXPECT(NearAngle(srvextras::MooredFacing(MooredLook::BowToLandSmall, s), 0.5f * kPi));
	EXPECT(NearAngle(srvextras::MooredFacing(MooredLook::OutToSea, s), -0.5f * kPi));       // the bow south, to the sea
	EXPECT(NearAngle(srvextras::MooredFacing(MooredLook::AlongShore, s), 0.0f));            // along the shore, east
	EXPECT(srvextras::MooredGapShare(MooredLook::AlongShore) == srvextras::kMooredLengthGap &&
	       srvextras::MooredGapShare(MooredLook::BowToLand) == srvextras::kMooredBeamGap &&
	       srvextras::MooredScale(MooredLook::BowToLandSmall) == srvextras::kMooredSmallScale &&
	       srvextras::MooredScale(MooredLook::OutToSea) == 1.0f);
	return true;
}

// The row on the water side, for each look and for the coast shapes a city can have
bool CaseRow()
{
	using srvextras::PlanMooredRow;
	std::vector<MooredSlot> out;
	const c5r::heightfield::HeightGrid south = SouthSea();
	// (a) bow to land, one ship: the south side (the first in the order) at 1.2 inner: the bow end (0.5 inner = 16 units
	// towards the city) is the first to reach the water that begins 20 units south of the centre (1.1: 35.2 - 16 = 19.2 < 20)
	EXPECT(PlanMooredRow(south, kCx, kCy, kInner, 1, MooredLook::BowToLand, out) && out.size() == 1);
	EXPECT(out[0].side == 0 && Near(out[0].share, 1.2f) && Near(out[0].x, kCx) && Near(out[0].y, kCy - 38.4f) &&
	       NearAngle(out[0].yaw, kPi) && out[0].scale == 1.0f);
	// three ships side by side along the shore (x), 0.5 inner apart, in order
	EXPECT(PlanMooredRow(south, kCx, kCy, kInner, 3, MooredLook::BowToLand, out) && out.size() == 3);
	EXPECT(Near(out[0].x, kCx - 16.0f) && Near(out[1].x, kCx) && Near(out[2].x, kCx + 16.0f));
	EXPECT(Near(out[0].y, kCy - 38.4f) && Near(out[2].y, kCy - 38.4f) && out[2].side == 0);
	// (b) along the shore: the hull parallel to the edge, so only half the beam (0.2 inner = 6.4 units) reaches towards the
	// city: 0.9 inner (0.8: 25.6 - 6.4 = 19.2 < 20); facing east (yaw pi/2); two ships end to end 1.1 inner apart
	EXPECT(PlanMooredRow(south, kCx, kCy, kInner, 2, MooredLook::AlongShore, out) && out.size() == 2);
	EXPECT(out[0].side == 0 && Near(out[0].share, 0.9f) && Near(out[0].y, kCy - 28.8f) && NearAngle(out[0].yaw, 0.5f * kPi));
	EXPECT(Near(out[0].x, kCx - 17.6f) && Near(out[1].x, kCx + 17.6f));
	// (c) out to sea: the same distance as (a), the bow south (yaw 0)
	EXPECT(PlanMooredRow(south, kCx, kCy, kInner, 1, MooredLook::OutToSea, out) && out[0].side == 0 &&
	       Near(out[0].share, 1.2f) && NearAngle(out[0].yaw, 0.0f));
	// (d) smaller: scale 0.8, its bow end 12.8 units from the root: 1.1 inner, nearer than (a)
	EXPECT(PlanMooredRow(south, kCx, kCy, kInner, 1, MooredLook::BowToLandSmall, out) && out[0].scale == 0.8f &&
	       Near(out[0].share, 1.1f) && out[0].side == 0);
	// a land-locked plot (no water anywhere): no row
	const c5r::heightfield::HeightGrid land = Grid([](float, float) { return false; });
	EXPECT(!PlanMooredRow(land, kCx, kCy, kInner, 1, MooredLook::BowToLand, out) && out.empty());
	// a one-plot island (land only within 20 units of the centre): the south side, as with the south sea
	const c5r::heightfield::HeightGrid island =
	    Grid([](float x, float y) { return (x - kCx) * (x - kCx) + (y - kCy) * (y - kCy) > 400.0f; });
	EXPECT(PlanMooredRow(island, kCx, kCy, kInner, 1, MooredLook::BowToLand, out) && out[0].side == 0 && Near(out[0].share, 1.2f));
	// the sea north only: the south, east and west sides fail; north-east and north-west fit at the same distance, the earlier
	// in the order (north-east, index 5) wins, at 1.3 inner (the bow end 0.866 x (41.6 - 16) = 22.2 north of the centre;
	// 1.2: 19.4); the bow to the city: facing 60 + 180 degrees, yaw 60 + 270 = -30 degrees
	const c5r::heightfield::HeightGrid north = Grid([](float, float y) { return y > kCy + 20.0f; });
	EXPECT(PlanMooredRow(north, kCx, kCy, kInner, 1, MooredLook::BowToLand, out) && out[0].side == 5);
	EXPECT(Near(out[0].share, 1.3f) && NearAngle(out[0].yaw, -kPi / 6.0f));
	EXPECT(Near(out[0].x, kCx + 0.5f * out[0].share * kInner) && Near(out[0].y, kCy + 0.8660254f * out[0].share * kInner, 2e-3f));
	// a narrow inlet south (16 units wide): one ship fits bow to land; a row of three side by side does not, so the fallback
	// places the first ship alone at the same spot and finds no spot for the other two (the inlet is too narrow and too
	// short for a second hull one gap away); along the shore not even one ship fits (the hull is 32 units long)
	const c5r::heightfield::HeightGrid inlet = Grid([](float x, float y) { return y < kCy - 20.0f && std::fabs(x - kCx) < 8.0f; });
	EXPECT(PlanMooredRow(inlet, kCx, kCy, kInner, 1, MooredLook::BowToLand, out) && out[0].side == 0);
	const MooredSlot one = out[0];
	EXPECT(PlanMooredRow(inlet, kCx, kCy, kInner, 3, MooredLook::BowToLand, out) && out.size() == 3);
	EXPECT(out[0].side == 0 && out[0].x == one.x && out[0].y == one.y && out[1].side == -1 && out[2].side == -1);
	EXPECT(!PlanMooredRow(inlet, kCx, kCy, kInner, 2, MooredLook::AlongShore, out) && out.empty());
	// a bay with a pier of land down its middle (water 20-60 units south of the centre, within 40 units east and west, but
	// not within 10 of the centre's x): wherever the two ships go (as the row or by the fallback), both stand on water
	// (their footprints at their facings) and at least one gap apart
	const c5r::heightfield::HeightGrid pier = Grid([](float x, float y) {
		return y < kCy - 20.0f && y > kCy - 60.0f && std::fabs(x - kCx) < 40.0f && !(std::fabs(x - kCx) < 10.0f);
	});
	EXPECT(PlanMooredRow(pier, kCx, kCy, kInner, 2, MooredLook::BowToLand, out) && out.size() == 2);
	EXPECT(out[0].side >= 0 && out[1].side >= 0 && std::hypot(out[0].x - out[1].x, out[0].y - out[1].y) >= 16.0f - 1e-3f);
	for(const MooredSlot& sl : out)
		EXPECT(srvextras::MooredFootOnWater(pier, sl.x, sl.y, sl.yaw - 0.5f * kPi, kInner, 1.0f));
	// bad inputs
	EXPECT(!PlanMooredRow(south, kCx, kCy, kInner, 0, MooredLook::BowToLand, out));
	EXPECT(!PlanMooredRow(south, kCx, kCy, 0.0f, 1, MooredLook::BowToLand, out));
	EXPECT(!PlanMooredRow(south, NAN, kCy, kInner, 1, MooredLook::BowToLand, out));
	// the footprint test alone: the root at the centre is land
	EXPECT(!srvextras::MooredFootOnWater(south, kCx, kCy, 0.0f, kInner, 1.0f) &&
	       srvextras::MooredFootOnWater(south, kCx, kCy - 40.0f, 0.5f * kPi, kInner, 1.0f));
	return true;
}

// The scene with the extra on: the ships in the city moored on the south side in a row (the hidden one shown opaque, the
// listed one moved), turned bow to land; the ship at sea, the dead ones and the warrior as stock has them
bool CaseSceneOn()
{
	g_log.clear();
	PortFixture plain;
	BuiltFrame bp;
	plain.core.build(nullptr, &plain.grid, 1100, bp);
	EXPECT(bp.listed == 2 && Find(bp, 0, 5002) && Find(bp, 0, 5003) && !Find(bp, 0, 5001));   // stock: the hidden ship not drawn
	EXPECT(!Find(bp, 0, 5002)->has_root);
	PortFixture f;
	ForcedOn* x = nullptr;
	std::unique_ptr<UnitsExtras> m = MooredModule(&x);
	EXPECT(m->anyOn() && LogCount("units extra ShipsMoored: in effect") == 1);
	f.core.setExtras(m.get());
	BuiltFrame b;
	f.core.build(nullptr, &f.grid, 1100, b);
	const cu::UnitInput* a = Find(b, 0, 5001);
	const cu::UnitInput* c = Find(b, 0, 5002);
	const cu::UnitInput* s = Find(b, 0, 5003);
	EXPECT(a && c && s && !Find(b, 0, 5004) && !Find(b, 0, 5005) && !Find(b, 0, 5006));
	EXPECT(a->fade_from == 1 && a->fade_to == 0 && a->fade == 0 && a->plot_x == 3 && a->plot_y == 2);   // shown opaque
	// the row of two side by side: 5001 west, 5002 east, 0.5 inner apart, 1.2 inner south of the centre
	EXPECT(a->has_root && Near(a->root_x, kCx - 8.0f) && Near(a->root_y, kCy - 38.4f));
	EXPECT(c->has_root && Near(c->root_x, kCx + 8.0f) && Near(c->root_y, kCy - 38.4f));
	EXPECT(!s->has_root);
	EXPECT(b.listed == 3 && b.xforms.size() == 2);
	const UnitXform& xa = b.xforms.at(UnitKeyOf(0, 5001));
	EXPECT(xa.heading && NearAngle(xa.yaw, kPi) && xa.scale == 1.0f && b.xforms.count(UnitKeyOf(0, 5002)) == 1);
	const CoreCounters& k = f.core.counters();
	EXPECT(k.xShown == 1 && k.xShowRefused == 0 && k.xRoot == 2 && k.xHeading == 2 && k.xScaled == 0 && k.xHeld == 0);
	MooredCounts n;
	EXPECT(CountsOf(x, n));
	EXPECT(n.frames == 1 && n.ports == 1 && n.rows == 1 && n.ships == 2 && n.shown == 1 && n.placed == 2 && n.walking == 0 &&
	       n.held == 0 && n.noWaterSide == 0);
	srvextras::UnitExtraCounts mc;
	bool running = false, failed = true;
	EXPECT(srvextras::UnitExtraCountsOf(*m, 0, mc, running, failed) && running && !failed && mc.shown == 1 && mc.overrides == 2 &&
	       mc.scaled == 0);
	// a second frame: the same places
	f.core.build(nullptr, &f.grid, 1200, b);
	EXPECT(Find(b, 0, 5001) && Near(Find(b, 0, 5001)->root_x, kCx - 8.0f) && f.core.counters().xShown == 2);
	// the small look: scaled 0.8, in the xforms
	PortFixture g;
	ForcedOn* y = nullptr;
	std::unique_ptr<UnitsExtras> ms = MooredModule(&y, true, true, MooredLook::BowToLandSmall);
	g.core.setExtras(ms.get());
	BuiltFrame bs;
	g.core.build(nullptr, &g.grid, 1100, bs);
	EXPECT(bs.xforms.size() == 2 && bs.xforms.at(UnitKeyOf(0, 5001)).scale == 0.8f && g.core.counters().xScaled == 2);
	// a city with no water side (the whole map land): its ships left as stock has them (the hidden one not drawn, never on
	// land; the listed one at the plot centre, no root)
	PortFixture l;
	l.grid = Grid([](float, float) { return false; });
	ForcedOn* z = nullptr;
	std::unique_ptr<UnitsExtras> ml = MooredModule(&z);
	l.core.setExtras(ml.get());
	BuiltFrame bl;
	l.core.build(nullptr, &l.grid, 1100, bl);
	EXPECT(!Find(bl, 0, 5001) && Find(bl, 0, 5002) && !Find(bl, 0, 5002)->has_root);
	// the ship "at sea" at (3, 0) now stands on land too (the whole map land): a port of its own, on the grid's south edge,
	// where the samples outside the field read 0 (heightAtSample), which is water: moored south of it
	EXPECT(Find(bl, 0, 5003) && Find(bl, 0, 5003)->has_root && bl.xforms.size() == 1 && bl.xforms.count(UnitKeyOf(0, 5003)));
	EXPECT(CountsOf(z, n) && n.noWaterSide == 2 && n.ports == 2 && n.placed == 1 && n.shown == 0);
	return true;
}

// E2: the flag off is the scene before; the real extra with no row is off with one line (not an extra); E5
bool CaseFlagOff()
{
	g_log.clear();
	PortFixture plain, off, real;
	BuiltFrame bp, bo, br;
	plain.core.build(nullptr, &plain.grid, 1100, bp);
	ForcedOn* x = nullptr;
	std::unique_ptr<UnitsExtras> m = MooredModule(&x, true, false);   // its flag 0
	EXPECT(!m->anyOn() && LogCount("units ShipsMoored: off: its own flag is 0") == 1 && g_log.size() == 1);
	off.core.setExtras(m.get());   // a module with nothing on (the host would drop it): the core does not call it
	off.core.build(nullptr, &off.grid, 1100, bo);
	EXPECT(SameScene(bp.copies[1], bo.copies[1]) && bp.listed == bo.listed && bp.listed == 2 && bo.xforms.empty());
	MooredCounts n;
	EXPECT(CountsOf(x, n) && n.frames == 0 && n.ships == 0);
	// the real extra made with an id that is no extra (no row yet): Vp64Opt::Extra says "not an extra", off, one line
	g_log.clear();
	std::vector<std::unique_ptr<srvextras::UnitExtra>> v;
	v.push_back(srvextras::MakeShipsMoored(Vp64Opt::kCount));
	std::unique_ptr<UnitsExtras> mr = srvextras::MakeUnitsExtrasWith(std::move(v));
	mr->setLog([](void*, const char* line) { Log(line); }, nullptr);
	mr->configure(Vp64Opt::Options(), true);
	EXPECT(!mr->anyOn() && g_log.size() == 1 && LogCount("units ShipsMoored: off: not an extra") == 1);
	real.core.setExtras(mr.get());
	real.core.build(nullptr, &real.grid, 1100, br);
	EXPECT(SameScene(bp.copies[1], br.copies[1]) && br.xforms.empty());
	// the default features list: the module has nothing on with every option at its default except FixAirBases (default
	// 1 since D235, 2026-11-22; switched off here: this case is about ShipsMoored's default 0)
	std::unique_ptr<UnitsExtras> feat = srvextras::MakeUnitsExtrasImpl();
	{
		const char* text = "[Extras]\nFixAirBases = 0\n";
		Vp64Opt::Options o;
		Vp64Opt::Parse(text, strlen(text), o, [](void*, const char*) {}, nullptr);
		feat->configure(o, true);
	}
	EXPECT(!feat->anyOn());
	// E5: with ServerUnits = 0 one line, nothing in effect
	g_log.clear();
	std::unique_ptr<UnitsExtras> m5 = MooredModule(nullptr, false, true);
	EXPECT(!m5->anyOn() && g_log.size() == 1 && LogCount("units extra ShipsMoored: off: ServerUnits = 0") == 1);
	return true;
}

// D250 (ticket 4984, the owner 2026-11-24): the option's value is the look. The real extra on its row (kShipsMoored), the
// module configured from an options text: 0 draws nothing (the scene before); 1 = look (d) BowToLandSmall (bow to the
// city, scale 0.8, side by side 0.5 x 0.8 inner apart); 2 = look (b) AlongShore (hull along the shore, scale 1, end to end
// 1.1 inner apart). Each value's look line logged once after the first frame.
bool CaseValues()
{
	MooredLook l = MooredLook::BowToLand;
	EXPECT(!srvextras::MooredLookOfValue(0, l) && l == MooredLook::BowToLand);
	EXPECT(srvextras::MooredLookOfValue(1, l) && l == MooredLook::BowToLandSmall);
	EXPECT(srvextras::MooredLookOfValue(2, l) && l == MooredLook::AlongShore);
	EXPECT(!srvextras::MooredLookOfValue(3, l) && !srvextras::MooredLookOfValue(-1, l) && l == MooredLook::AlongShore);
	EXPECT(strcmp(srvextras::MooredLookName(MooredLook::BowToLandSmall), "look (d) BowToLandSmall") == 0 &&
	       strcmp(srvextras::MooredLookName(MooredLook::AlongShore), "look (b) AlongShore") == 0);
	EXPECT(Vp64Opt::Definition(Vp64Opt::kShipsMoored).minValue == 0 && Vp64Opt::Definition(Vp64Opt::kShipsMoored).maxValue == 2 &&
	       Vp64Opt::Definition(Vp64Opt::kShipsMoored).defaultValue == 0);
	PortFixture plain;
	BuiltFrame bp;
	plain.core.build(nullptr, &plain.grid, 1100, bp);
	struct V
	{
		const char* text;
		int value;            // -1: the default (no line)
		MooredLook look;
		float scale, gap;     // gap: the roots' distance, shares of the inner radius
		float yaw;            // the seam's yaw on the south side
		bool alongX;          // the row runs along x (the shore is the south edge either way)
	};
	const V vs[] = {
	    {"[Extras]\nEnableVisualAdditions = 1\n", -1, MooredLook::BowToLand, 0, 0, 0, true},
	    {"[Extras]\nEnableVisualAdditions = 1\nShipsMoored = 0\n", 0, MooredLook::BowToLand, 0, 0, 0, true},
	    {"[Extras]\nEnableVisualAdditions = 1\nShipsMoored = 1\n", 1, MooredLook::BowToLandSmall, 0.8f, 0.5f * 0.8f, kPi, true},
	    {"[Extras]\nEnableVisualAdditions = 1\nShipsMoored = 2\n", 2, MooredLook::AlongShore, 1.0f, 1.1f, 0.5f * kPi, true},
	    {"[Extras]\nShipsMoored = 2\n", -2, MooredLook::BowToLand, 0, 0, 0, true},   // the visual switch at its default 0
	};
	for(const V& v : vs)
	{
		g_log.clear();
		Vp64Opt::Options o;
		Vp64Opt::Parse(v.text, strlen(v.text), o, [](void*, const char*) {}, nullptr);
		std::vector<std::unique_ptr<srvextras::UnitExtra>> list;
		list.push_back(srvextras::MakeShipsMoored(Vp64Opt::kShipsMoored));
		srvextras::UnitExtra* x = list.back().get();
		std::unique_ptr<UnitsExtras> m = srvextras::MakeUnitsExtrasWith(std::move(list));
		m->setLog([](void*, const char* line) { Log(line); }, nullptr);
		m->configure(o, true);
		PortFixture f;
		BuiltFrame b;
		if(v.value <= 0)
		{
			// off: one line, nothing in effect, the scene as stock has it
			EXPECT(!m->anyOn() && g_log.size() == 1 && LogCount("units ShipsMoored: off: ") == 1);
			f.core.setExtras(m.get());
			f.core.build(nullptr, &f.grid, 1100, b);
			EXPECT(SameScene(bp.copies[1], b.copies[1]) && b.listed == bp.listed && b.xforms.empty() && !Find(b, 0, 5001));
			continue;
		}
		EXPECT(m->anyOn() && LogCount("units extra ShipsMoored: in effect") == 1);
		MooredLook got = MooredLook::BowToLand;
		EXPECT(srvextras::MooredLookOf(*x, got) && got == v.look);
		f.core.setExtras(m.get());
		f.core.build(nullptr, &f.grid, 1100, b);
		char note[120];
		snprintf(note, sizeof(note), "units extra ShipsMoored: %s (ShipsMoored = %d)", srvextras::MooredLookName(v.look), v.value);
		EXPECT(LogCount(note) == 1);
		f.core.build(nullptr, &f.grid, 1200, b);   // a second frame: the note is not repeated
		EXPECT(LogCount(note) == 1);
		// the row as PlanMooredRow gives it for the look; both ships placed, south of the centre, the look's facing and scale
		std::vector<MooredSlot> row;
		EXPECT(srvextras::PlanMooredRow(f.grid, kCx, kCy, kInner, 2, v.look, row) && row.size() == 2);
		const cu::UnitInput* a = Find(b, 0, 5001);
		const cu::UnitInput* c = Find(b, 0, 5002);
		EXPECT(a && c && a->has_root && c->has_root);
		EXPECT(Near(a->root_x, row[0].x) && Near(a->root_y, row[0].y) && Near(c->root_x, row[1].x) && Near(c->root_y, row[1].y));
		EXPECT(a->root_y < kCy && c->root_y < kCy && Near(a->root_y, c->root_y));   // on the south (water) side, in a row
		EXPECT(Near(c->root_x - a->root_x, v.gap * kInner));                          // the look's gap x its scale
		EXPECT(b.xforms.size() == 2);
		for(int key : {5001, 5002})
		{
			const UnitXform& t = b.xforms.at(UnitKeyOf(0, key));
			EXPECT(t.heading && NearAngle(t.yaw, v.yaw) && t.scale == v.scale);
		}
		EXPECT(f.core.counters().xScaled == (v.scale != 1.0f ? 4u : 0u));   // two frames
		MooredCounts n;
		EXPECT(srvextras::MooredCountsOf(*x, n) && n.placed == 4 && n.shown == 2 && n.noWaterSide == 0);
	}
	// a fixed-look extra (the tests' and the scratch switch's form) keeps its look whatever the value
	{
		g_log.clear();
		const char* text = "[Extras]\nEnableVisualAdditions = 1\nShipsMoored = 1\n";
		Vp64Opt::Options o;
		Vp64Opt::Parse(text, strlen(text), o, [](void*, const char*) {}, nullptr);
		std::vector<std::unique_ptr<srvextras::UnitExtra>> list;
		list.push_back(srvextras::MakeShipsMoored(Vp64Opt::kShipsMoored, MooredLook::OutToSea));
		srvextras::UnitExtra* x = list.back().get();
		std::unique_ptr<UnitsExtras> m = srvextras::MakeUnitsExtrasWith(std::move(list));
		m->setLog([](void*, const char* line) { Log(line); }, nullptr);
		m->configure(o, true);
		MooredLook got = MooredLook::BowToLand;
		EXPECT(m->anyOn() && srvextras::MooredLookOf(*x, got) && got == MooredLook::OutToSea);
		PortFixture f;
		BuiltFrame b;
		f.core.setExtras(m.get());
		f.core.build(nullptr, &f.grid, 1100, b);
		EXPECT(LogCount("units extra ShipsMoored: look (c) OutToSea (fixed by the maker)") == 1);
	}
	// the product's maker list: ShipsMoored made from its row, the look from the value (ShipsMoored = 2: look (b))
	{
		g_log.clear();
		const char* text = "[Extras]\nEnableVisualAdditions = 1\nShipsMoored = 2\nFixAirBases = 0\n";
		Vp64Opt::Options o;
		Vp64Opt::Parse(text, strlen(text), o, [](void*, const char*) {}, nullptr);
		std::unique_ptr<UnitsExtras> feat = srvextras::MakeUnitsExtrasImpl();
		feat->setLog([](void*, const char* line) { Log(line); }, nullptr);
		feat->configure(o, true);
		EXPECT(feat->anyOn() && feat->inEffect() == 1 && LogCount("units extra ShipsMoored: in effect") == 1);
		PortFixture f;
		BuiltFrame b;
		f.core.setExtras(feat.get());
		f.core.build(nullptr, &f.grid, 1100, b);
		EXPECT(LogCount("units extra ShipsMoored: look (b) AlongShore (ShipsMoored = 2)") == 1);
		EXPECT(b.xforms.size() == 2 && NearAngle(b.xforms.at(UnitKeyOf(0, 5001)).yaw, 0.5f * kPi));
	}
	return true;
}

// E6: an input missing switches the extra off with one line; the scene draws stock; the row check
bool CaseInputs()
{
	g_log.clear();
	ForcedOn* x = nullptr;
	std::unique_ptr<UnitsExtras> m = MooredModule(&x);
	UnitExtrasFrame f;   // no table
	m->beginFrame(f);
	EXPECT(!m->anyOn() && LogCount("units extra ShipsMoored: LOUD: failed: beginFrame: the scene gives no unit table: off for the session") == 1);
	// a height field that gives no plot centres
	g_log.clear();
	m = MooredModule(&x);
	std::vector<SrvFeed::UnitRecord> units;
	c5r::heightfield::HeightGrid empty;
	f.units = &units;
	f.grid = &empty;
	m->beginFrame(f);
	EXPECT(!m->anyOn() && LogCount("beginFrame: the height field gives no plot centre") == 1 && g_log.size() == 2);
	// no grid this frame: nothing to do, still on
	g_log.clear();
	m = MooredModule(&x);
	f.grid = nullptr;
	m->beginFrame(f);
	EXPECT(m->anyOn() && g_log.size() == 1);   // the "in effect" line only
	// the scene with a failed extra draws stock
	PortFixture a, b;
	BuiltFrame fa, fb;
	a.core.build(nullptr, &a.grid, 1100, fa);
	std::unique_ptr<UnitsExtras> mf = MooredModule(&x);
	UnitExtrasFrame bad;
	mf->beginFrame(bad);   // failed
	b.core.setExtras(mf.get());
	b.core.build(nullptr, &b.grid, 1100, fb);
	EXPECT(SameScene(fa.copies[1], fb.copies[1]) && fb.xforms.empty());
	// configure's row check: made with another option's id, the inner configure refuses
	g_log.clear();
	ForcedOn* r = new ForcedOn(MooredLook::BowToLand, (Vp64Opt::Id)0);
	r->forwardConfigure = true;
	std::vector<std::unique_ptr<srvextras::UnitExtra>> v;
	v.emplace_back(r);
	std::unique_ptr<UnitsExtras> mr = srvextras::MakeUnitsExtrasWith(std::move(v));
	mr->setLog([](void*, const char* line) { Log(line); }, nullptr);
	mr->configure(Vp64Opt::Options(), true);
	EXPECT(!mr->anyOn() && LogCount("units extra ShipsMoored: LOUD: failed: configure: its option row is not ShipsMoored") == 1);
	MooredCounts n;
	EXPECT(!srvextras::MooredCountsOf(*r, n));
	return true;
}

// Held in a fight: untouched (not shown, not moved); a walking ship: shown, never placed (the extra directly, and the scene)
bool CaseHeldWalking()
{
	PortFixture f;
	ForcedOn* x = nullptr;
	std::unique_ptr<UnitsExtras> m = MooredModule(&x);
	f.core.setExtras(m.get());
	f.core.setHeldInFight(0, 5001, true);
	f.core.setHeldInFight(0, 5002, true);
	BuiltFrame b;
	f.core.build(nullptr, &f.grid, 1100, b);
	EXPECT(!Find(b, 0, 5001) && Find(b, 0, 5002) && !Find(b, 0, 5002)->has_root && b.xforms.empty());
	EXPECT(f.core.counters().xHeld == 0);   // the extra asked nothing for them
	MooredCounts n;
	EXPECT(CountsOf(x, n) && n.held == 2 && n.shown == 0 && n.placed == 0);
	f.core.setHeldInFight(0, 5001, false);
	f.core.setHeldInFight(0, 5002, false);
	f.core.build(nullptr, &f.grid, 1200, b);
	EXPECT(Find(b, 0, 5001) && Find(b, 0, 5001)->has_root);
	// walking, through the extra itself: a root 1.0 from the centre (> 0.02 x 32 = 0.64) and a scene plot that is not the
	// record's plot are walking; a root 0.5 from it and no root are at rest
	std::unique_ptr<srvextras::UnitExtra> e = srvextras::MakeShipsMoored(Vp64Opt::kCount);
	std::vector<SrvFeed::UnitRecord> units = {Rec(0, 5001, kTrireme, kSea, kCityPlot, 0, 1)};
	c5r::heightfield::HeightGrid grid = SouthSea();
	UnitExtrasFrame fr;
	fr.units = &units;
	fr.grid = &grid;
	fr.width = 10;
	fr.height = 8;
	std::string why;
	EXPECT(e->beginFrame(fr, why));
	struct W
	{
		bool hasRoot;
		float dy;
		int px;
		bool walking;
	};
	const W ws[] = {{false, 0, 3, false}, {true, 0.5f, 3, false}, {true, 1.0f, 3, true}, {false, 0, 4, true}};
	for(const W& w : ws)
	{
		UnitExtraUnit u = XU(units[0], true, w.px, 2);
		u.hasRoot = w.hasRoot;
		u.rootX = kCx;
		u.rootY = kCy + w.dy;
		UnitOverride io;
		EXPECT(e->unit(u, io, why));
		EXPECT(io.show && io.root == !w.walking && io.heading == !w.walking);
	}
	MooredCounts c;
	EXPECT(srvextras::MooredCountsOf(*e, c) && c.walking == 2 && c.placed == 2 && c.ships == 4 && c.ports == 1);
	// a ship whose record plot is at sea is not a port: nothing
	std::vector<SrvFeed::UnitRecord> sea = {Rec(0, 7001, kTrireme, kSea, 3, 0, 1)};
	fr.units = &sea;
	EXPECT(e->beginFrame(fr, why));
	UnitOverride io;
	UnitExtraUnit u = XU(sea[0], true, 3, 0);
	EXPECT(e->unit(u, io, why) && !io.show && !io.root);
	return true;
}

// A cost indication (E8; the workstation, not a measurement for a claim, PERFORMANCE.md): the build of a 10,000-unit
// table with 400 port plots of two ships each (one hidden), with no module and with the extra on
bool CaseCost()
{
	const int n = 10000, ports = 400;
	std::vector<SrvFeed::UnitRecord> t;
	// a 100 x 100 grid; the city plots on the even rows' first 400 even columns, the sea on every odd row
	for(int i = 0; i < n; ++i)
	{
		if(i < 2 * ports)
		{
			const int p = i / 2, plot = (p / 50) * 2 * 100 + (p % 50) * 2;
			t.push_back(Rec(0, 20000 + i, kTrireme, kSea, plot, i % 2 ? cu::kFlagVisible : 0, 1));
		}
		else
			t.push_back(Rec(0, 20000 + i, kWarrior, kLand, 100 + (i % 9800), cu::kFlagVisible, 1));
	}
	c5r::heightfield::HeightGrid grid = Grid([](float, float y) { return ((int)std::floor(y / (64.0f * 0.866f) + 0.5f)) % 2 == 1; },
	                                         100, 100);
	double ms[2] = {0, 0};
	size_t listed[2] = {0, 0};
	uint64_t placed = 0;
	for(int arm = 0; arm < 2; ++arm)
	{
		FakePort port;
		UnitsCore core(port, Log);
		port.players = {Player(0, 0, 0)};
		port.units = t;
		core.setDatabase(TestLookups(), nullptr);
		std::vector<SrvFeed::Event> ev;
		uint32_t seq = 1;
		for(const SrvFeed::UnitRecord& u : port.units)
			ev.push_back(Ev(seq++, SrvFeed::kUnitCreated, u.owner, u.id, u.unitType, 0, u.plot));
		SrvFeed::Batch b0 = BatchOf(ev);
		b0.width = 100;
		b0.height = 100;
		core.onBatch(b0, 1000);
		std::unique_ptr<UnitsExtras> m;
		ForcedOn* x = nullptr;
		if(arm)
		{
			m = MooredModule(&x);
			core.setExtras(m.get());
		}
		BuiltFrame b;
		core.build(nullptr, &grid, 1100, b);   // warm
		const int reps = 20;
		const auto t0 = std::chrono::steady_clock::now();
		for(int r = 0; r < reps; ++r)
			core.build(nullptr, &grid, 1100, b);
		ms[arm] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / reps;
		listed[arm] = b.listed;
		MooredCounts c;
		if(x && CountsOf(x, c))
			placed = c.placed;
	}
	EXPECT(listed[1] == listed[0] + (size_t)ports);
	printf("cost indication (workstation, not a result): build of %d units with %d port plots: %.3f ms without the module, "
	       "%.3f ms with ShipsMoored on (%zu more listed, %llu ship-frames placed)\n",
	       n, ports, ms[0], ms[1], listed[1] - listed[0], (unsigned long long)placed);
	return true;
}

struct Case
{
	const char* name;
	bool (*fn)();
};
const Case kCases[] = {
    {"moored_choice", CaseChoice}, {"moored_row", CaseRow},       {"moored_scene_on", CaseSceneOn},
    {"moored_flag_off", CaseFlagOff}, {"moored_inputs", CaseInputs}, {"moored_held_walking", CaseHeldWalking},
    {"moored_cost", CaseCost},  {"moored_values", CaseValues},
};

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
		printf("test_ships_moored: no case matches\n");
		return 2;
	}
	printf("test_ships_moored: %d of %d cases passed\n", passed, run);
	return passed == run ? 0 : 1;
}
