// test_fix_air_bases.cpp - tests of FixAirBases (epic 0455, issue 4985; server/src/extras/units/srv_unit_airbases.h): the
// choice, the scene through the units seam, flag off and E5/E6, the live sequence of the 4985 re-scope against its reload,
// and a cost indication. Written 2026-11-20 for this repository. SPDX-License-Identifier: GPL-3.0-or-later
// Build: build_units_extras_tests.py --test test_fix_air_bases. Run: test_fix_air_bases.exe [case ...]; "CASE <name>
// PASS|FAIL <why>" per case. Exit: 0 every case passed, 1 a case failed, 2 no case matched. No install, no device.
#include <windows.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "units/srv_units_core.h"
#include "extras/units/srv_units_extras.h"
#include "extras/units/srv_unit_airbases.h"
#include "c5r/heightfield/heightfield.h"
#include "srv_options.h"

// server.exe's SrvOptions() (server/src/srv_options.cpp) for the render units: every option at its default
static Vp64Opt::Options g_options;
const Vp64Opt::Options& SrvOptions() { return g_options; }

using namespace srvunits;
namespace cu = c5r::units;
using srvextras::AirBaseChoice;
using srvextras::AirBaseCounts;
using srvextras::AirTeam;

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

const int kWarrior = 7, kFighter = 8, kCarrier = 9;
const int8_t kSea = 0, kAir = 1, kLand = 2;

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
	l.units = {{kWarrior, "UNIT_WARRIOR"}, {kFighter, "UNIT_FIGHTER"}, {kCarrier, "UNIT_CARRIER"}};
	l.colours = {{3, "PLAYERCOLOR_POLYNESIA"}};
	l.civs = {{5, "CIVILIZATION_POLYNESIA"}};
	l.eras = {{0, "ERA_ANCIENT"}};
	l.artStyles = {{5, "ARTSTYLE_POLYNESIAN"}};
	return l;
}

// A flat synthetic height field (test_scene_units.cpp's lattice): mapW x (mapH + 4) plots of 64 x 64 units, y scaled 0.866,
// height 0. Plot centre (x, y): ((x - (y >> 1)) 64 + 32 y, 64 y 0.866); the inner radius 32.
c5r::heightfield::HeightGrid Grid(uint32_t mapW = 10, uint32_t mapH = 8)
{
	c5r::heightfield::HeightGrid g;
	g.mapW = mapW;
	g.mapH = mapH + 4;
	g.samplesX = g.mapW * 64;
	g.samplesY = g.mapH * 64;
	g.h.assign((size_t)g.samplesX * g.samplesY, 0);
	g.waterZ = -5.0f;
	g.hexToWorld[0] = 64;
	g.hexToWorld[4] = 32;
	g.hexToWorld[5] = 64;
	g.hexToWorld[10] = 1;
	g.hexToWorld[15] = 1;
	return g;
}

// FixAirBases with its state forced (the option row 26 comes with the rows of epic 0455, W3's hunk; once the row exists the
// module asks Vp64Opt::Extra and the cases that need "on" keep this wrapper)
struct ForcedOn : srvextras::UnitExtra
{
	std::unique_ptr<srvextras::UnitExtra> in;
	bool on = true;
	bool forwardConfigure = false;   // true: the inner extra's own configure (its row check) runs
	explicit ForcedOn(Vp64Opt::Id id = Vp64Opt::kCount) : in(srvextras::MakeFixAirBases(id)) {}
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

std::unique_ptr<UnitsExtras> AirModule(ForcedOn** out = nullptr, bool ours = true, bool on = true)
{
	ForcedOn* x = new ForcedOn();
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

bool CountsOf(ForcedOn* x, AirBaseCounts& c) { return x && srvextras::AirBaseCountsOf(*x->in, c); }

// The air base table, a 10 x 8 grid, active team 0. City A at plot 23 = (3, 2), the carrier's plot 33 = (3, 3).
//   4001 fighter based in A, hidden (the DLL's updateCenterUnit: not the centre unit)   -> shown, based
//   4002 fighter, cargo of carrier 4010 (kind 1001), hidden                                -> shown, carried
//   4003 fighter in A that stock lists (visible)                                           -> untouched (listed)
//   4004 dead fighter, hidden (fading out by a kill)                                       -> untouched
//   4005 fighter with the delayed-death flag, hidden                                       -> untouched
//   4006 warrior in A, hidden (a land unit not the centre)                                 -> untouched (not air)
//   4010 the carrier (sea), visible                                                        -> untouched
//   2001 fighter of player 1 (team 1), hidden, plot 26                                     -> untouched (another team)
//   3001 fighter of player 2 (no player row), hidden, plot 27                              -> untouched (no record)
struct AirFixture
{
	FakePort port;
	UnitsCore core;
	c5r::heightfield::HeightGrid grid = Grid();
	uint32_t seq = 1;
	explicit AirFixture(bool allVisible = false) : core(port, Log)
	{
		const uint8_t h = allVisible ? cu::kFlagVisible : 0;
		port.players = {Player(0, 0, 0), Player(1, 1, 0)};
		port.units = {Rec(0, 4001, kFighter, kAir, 23, h, 1),
		              Rec(0, 4002, kFighter, kAir, 33, h, 2),
		              Rec(0, 4003, kFighter, kAir, 23, cu::kFlagVisible, 3),
		              Rec(0, 4004, kFighter, kAir, 23, cu::kFlagDead, 4),
		              Rec(0, 4005, kFighter, kAir, 23, cu::kFlagDelayedDeath, 5),
		              Rec(0, 4006, kWarrior, kLand, 23, h, 6),
		              Rec(0, 4010, kCarrier, kSea, 33, cu::kFlagVisible, 7),
		              Rec(1, 2001, kFighter, kAir, 26, h, 8),
		              Rec(2, 3001, kFighter, kAir, 27, h, 9)};
		core.setDatabase(TestLookups(), nullptr);
		std::vector<SrvFeed::Event> ev;
		for(const SrvFeed::UnitRecord& u : port.units)
			ev.push_back(Ev(seq++, SrvFeed::kUnitCreated, u.owner, u.id, u.unitType, 0, u.plot));
		ev.push_back(Ev(seq++, SrvFeed::kUnitCarrierChanged, 0, 4002, 4010, 0, 33));   // the setup's carrier message
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

// Every field the placement reads but the seed (a reload allocates new seeds, in stock as here) and the visible bit of
// flags (the record's, clear for a shown unit; no part of render/src reads kFlagVisible, the scene's fade fields carry the
// visibility)
bool SameInput(const cu::UnitInput& x, const cu::UnitInput& y)
{
	const uint8_t mask = (uint8_t)~cu::kFlagVisible;
	return x.player == y.player && x.id == y.id && x.unit_type == y.unit_type && x.art_row == y.art_row && x.plot_x == y.plot_x &&
	       x.plot_y == y.plot_y && x.has_root == y.has_root && x.root_x == y.root_x && x.root_y == y.root_y && x.root_z == y.root_z &&
	       x.facing == y.facing && (x.flags & mask) == (y.flags & mask) && x.fade_from == y.fade_from && x.fade_to == y.fade_to &&
	       x.fade == y.fade && x.damage == y.damage && x.max_hp == y.max_hp && x.alive == y.alive;
}

bool SameScene(const cu::UnitScene& a, const cu::UnitScene& b)
{
	if(a.units.size() != b.units.size() || a.players.size() != b.players.size())
		return false;
	for(size_t i = 0; i < a.units.size(); ++i)
		if(!SameInput(a.units[i], b.units[i]) || a.units[i].formation_seed != b.units[i].formation_seed)
			return false;
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
// The choice without a scene: which units are shown and which not
bool CaseChoice()
{
	using srvextras::ChooseAirBase;
	const SrvFeed::UnitRecord air = Rec(0, 1, kFighter, kAir, 23, 0, 1), land = Rec(0, 2, kWarrior, kLand, 23, 0, 1),
	                          sea = Rec(0, 3, kCarrier, kSea, 23, 0, 1), dead = Rec(0, 4, kFighter, kAir, 23, cu::kFlagDead, 1),
	                          dd = Rec(0, 5, kFighter, kAir, 23, cu::kFlagDelayedDeath, 1),
	                          inv = Rec(0, 6, kFighter, kAir, 23, cu::kFlagInvisible, 1);
	AirBaseChoice c = ChooseAirBase(XU(air), AirTeam::Active, true, false);
	EXPECT(c.kind == AirBaseChoice::Shown && c.show);
	c = ChooseAirBase(XU(inv), AirTeam::Active, true, false);   // own team: the seam shows the active team's invisible units
	EXPECT(c.kind == AirBaseChoice::Shown && c.show);
	c = ChooseAirBase(XU(air, false), AirTeam::Active, true, false);
	EXPECT(c.kind == AirBaseChoice::Listed && !c.show);
	c = ChooseAirBase(XU(land), AirTeam::Active, true, false);
	EXPECT(c.kind == AirBaseChoice::NotAircraft && !c.show);
	c = ChooseAirBase(XU(sea), AirTeam::Active, true, false);
	EXPECT(c.kind == AirBaseChoice::NotAircraft && !c.show);
	c = ChooseAirBase(XU(dead), AirTeam::Active, true, false);
	EXPECT(c.kind == AirBaseChoice::Dead && !c.show);
	c = ChooseAirBase(XU(dd), AirTeam::Active, true, false);
	EXPECT(c.kind == AirBaseChoice::Dead && !c.show);
	UnitExtraUnit held = XU(air);
	held.heldInFight = true;
	c = ChooseAirBase(held, AirTeam::Active, true, false);
	EXPECT(c.kind == AirBaseChoice::Held && !c.show);
	c = ChooseAirBase(XU(air), AirTeam::Other, true, false);
	EXPECT(c.kind == AirBaseChoice::Foreign && !c.show);
	c = ChooseAirBase(XU(air), AirTeam::NoRecord, true, false);
	EXPECT(c.kind == AirBaseChoice::NoPlayer && !c.show);
	c = ChooseAirBase(XU(air), AirTeam::Active, false, false);
	EXPECT(c.kind == AirBaseChoice::NoPlot && !c.show);
	c = ChooseAirBase(XU(air), AirTeam::Active, true, true);
	EXPECT(c.kind == AirBaseChoice::Walking && !c.show);
	UnitExtraUnit none;
	c = ChooseAirBase(none, AirTeam::Active, true, false);
	EXPECT(c.kind == AirBaseChoice::NotAircraft && !c.show);
	EXPECT(srvextras::IsLiveAircraft(air) && !srvextras::IsLiveAircraft(dead) && !srvextras::IsLiveAircraft(dd) &&
	       !srvextras::IsLiveAircraft(land));

	// the walking test: plot (3, 2), centre (192, 110.848), inner 32
	using srvextras::AirBaseWalking;
	const float cx = 192.0f, cy = 128.0f * 0.866f, inner = 32.0f;
	UnitExtraUnit w = XU(air);
	EXPECT(!AirBaseWalking(w, 3, 2, true, cx, cy, inner));        // no root, its own plot: as after a load
	EXPECT(AirBaseWalking(XU(air, true, 4, 2), 3, 2, true, cx, cy, inner));   // the scene draws it at the plot it left
	w.hasRoot = true;
	w.rootX = cx;
	w.rootY = cy + 0.5f;
	EXPECT(!AirBaseWalking(w, 3, 2, true, cx, cy, inner));        // a root at the centre (0.5 < 0.02 x 32 = 0.64)
	w.rootY = cy + 1.0f;
	EXPECT(AirBaseWalking(w, 3, 2, true, cx, cy, inner));         // 1.0 > 0.64: walking
	w.rootY = cy;
	EXPECT(AirBaseWalking(w, 3, 2, false, cx, cy, inner));        // a root and no centre to compare: not shown
	EXPECT(srvextras::kAirBaseWalkShare == 0.02f && srvextras::kDomainAir == 1);

	// the team of the owner from the player table (indexed by player)
	std::vector<SrvFeed::PlayerRecord> pl = {Player(0, 0, 1), Player(1, 1, 1)};
	EXPECT(srvextras::AirTeamOf(Rec(0, 1, kFighter, kAir, 0, 0, 1), pl) == AirTeam::Other);
	EXPECT(srvextras::AirTeamOf(Rec(1, 1, kFighter, kAir, 0, 0, 1), pl) == AirTeam::Active);
	EXPECT(srvextras::AirTeamOf(Rec(2, 1, kFighter, kAir, 0, 0, 1), pl) == AirTeam::NoRecord);
	pl[1].player = -1;
	EXPECT(srvextras::AirTeamOf(Rec(1, 1, kFighter, kAir, 0, 0, 1), pl) == AirTeam::NoRecord);
	return true;
}

// The scene with the extra on: the hidden own aircraft listed opaque at the scene's own placement, nothing else changes; the
// shown inputs equal those of the same state after a load (every aircraft visible)
bool CaseSceneOn()
{
	g_log.clear();
	AirFixture f;
	ForcedOn* x = nullptr;
	std::unique_ptr<UnitsExtras> m = AirModule(&x);
	EXPECT(m->anyOn() && LogCount("units extra FixAirBases: in effect") == 1);
	f.core.setExtras(m.get());
	m->onBatch(BatchOf({Ev(f.seq++, SrvFeed::kUnitCarrierChanged, 0, 4002, 4010, 0, 33)}));   // the host passes every batch
	BuiltFrame b;
	f.core.build(nullptr, &f.grid, 1100, b);
	const cu::UnitInput* a = Find(b, 0, 4001);
	const cu::UnitInput* c = Find(b, 0, 4002);
	EXPECT(a && c && Find(b, 0, 4003) && Find(b, 0, 4010));
	EXPECT(!Find(b, 0, 4004) && !Find(b, 0, 4005) && !Find(b, 0, 4006) && !Find(b, 1, 2001) && !Find(b, 2, 3001));
	EXPECT(a->fade_from == 1 && a->fade_to == 0 && a->fade == 0 && !a->has_root && a->plot_x == 3 && a->plot_y == 2);
	EXPECT(c->fade_from == 1 && !c->has_root && c->plot_x == 3 && c->plot_y == 3 && c->unit_type == "UNIT_FIGHTER");
	EXPECT(b.listed == 4 && b.xforms.empty());
	const CoreCounters& k = f.core.counters();
	EXPECT(k.xShown == 2 && k.xShowRefused == 0 && k.xRoot == 0 && k.xHeading == 0 && k.xScaled == 0 && k.xHeld == 0);
	AirBaseCounts n;
	EXPECT(CountsOf(x, n));
	EXPECT(n.frames == 1 && n.aircraft == 7 && n.shown == 2 && n.shownCarried == 1 && n.shownBased == 1 && n.dead == 2 &&
	       n.foreign == 1 && n.noPlayer == 1 && n.walking == 0 && n.held == 0 && n.noPlot == 0 && n.carrierEvents == 1 && n.cargo == 1);
	srvextras::UnitExtraCounts mc;
	bool running = false, failed = true;
	EXPECT(srvextras::UnitExtraCountsOf(*m, 0, mc, running, failed) && running && !failed && mc.shown == 2 && mc.overrides == 0 &&
	       mc.scaled == 0);
	// the same table after a load (4001, 4002 visible, as stock creates every aircraft at a load): the shown inputs are equal
	AirFixture loaded(true);
	BuiltFrame lb;
	loaded.core.build(nullptr, &loaded.grid, 1100, lb);
	EXPECT(Find(lb, 0, 4001) && Find(lb, 0, 4002) && Find(lb, 0, 4006));   // stock after a load lists the warrior too
	EXPECT(SameInput(*a, *Find(lb, 0, 4001)) && SameInput(*c, *Find(lb, 0, 4002)));
	// a second frame: the same (the extra keeps nothing per frame but its counts)
	f.core.build(nullptr, &f.grid, 1200, b);
	EXPECT(Find(b, 0, 4001) && Find(b, 0, 4002) && b.listed == 4 && f.core.counters().xShown == 4);
	return true;
}

// E2: the flag off is the scene before; the real extra with no row is off with one line (not an extra); E5
bool CaseFlagOff()
{
	g_log.clear();
	AirFixture plain, off, real;
	BuiltFrame bp, bo, br;
	plain.core.build(nullptr, &plain.grid, 1100, bp);
	ForcedOn* x = nullptr;
	std::unique_ptr<UnitsExtras> m = AirModule(&x, true, false);   // its flag 0
	EXPECT(!m->anyOn() && LogCount("units FixAirBases: off: its own flag is 0") == 1 && g_log.size() == 1);
	off.core.setExtras(m.get());   // a module with nothing on (the host would drop it): the core does not call it
	off.core.build(nullptr, &off.grid, 1100, bo);
	EXPECT(SameScene(bp.copies[1], bo.copies[1]) && bp.listed == bo.listed && bp.listed == 2);
	EXPECT(!Find(bp, 0, 4001) && !Find(bp, 0, 4002));   // stock: hidden aircraft are not drawn
	AirBaseCounts n;
	EXPECT(CountsOf(x, n) && n.frames == 0 && n.aircraft == 0);
	// the real extra made with an id that is no extra (no row yet): Vp64Opt::Extra says "not an extra", off, one line
	g_log.clear();
	std::vector<std::unique_ptr<srvextras::UnitExtra>> v;
	v.push_back(srvextras::MakeFixAirBases(Vp64Opt::kCount));
	std::unique_ptr<UnitsExtras> mr = srvextras::MakeUnitsExtrasWith(std::move(v));
	mr->setLog([](void*, const char* line) { Log(line); }, nullptr);
	mr->configure(Vp64Opt::Options(), true);
	EXPECT(!mr->anyOn() && g_log.size() == 1 && LogCount("units FixAirBases: off: not an extra") == 1);
	real.core.setExtras(mr.get());
	real.core.build(nullptr, &real.grid, 1100, br);
	EXPECT(SameScene(bp.copies[1], br.copies[1]));
	// the default features list: FixAirBases is on with every option at its default (default 1 since D235 (2), 2026-11-22)
	std::unique_ptr<UnitsExtras> feat = srvextras::MakeUnitsExtrasImpl();
	feat->configure(Vp64Opt::Options(), true);
	EXPECT(feat->anyOn());
	// E5: with ServerUnits = 0 one line, nothing in effect
	g_log.clear();
	std::unique_ptr<UnitsExtras> m5 = AirModule(nullptr, false, true);
	EXPECT(!m5->anyOn() && g_log.size() == 1 && LogCount("units extra FixAirBases: off: ServerUnits = 0") == 1);
	return true;
}

// E6: an input missing switches the extra off with one line; the scene draws stock; the row check
bool CaseInputs()
{
	g_log.clear();
	ForcedOn* x = nullptr;
	std::unique_ptr<UnitsExtras> m = AirModule(&x);
	UnitExtrasFrame f;   // no tables
	m->beginFrame(f);
	EXPECT(!m->anyOn() && LogCount("units extra FixAirBases: LOUD: failed: beginFrame: the scene gives no unit table: off for the session") == 1);
	g_log.clear();
	m = AirModule(&x);
	std::vector<SrvFeed::UnitRecord> units;
	f.units = &units;
	m->beginFrame(f);
	EXPECT(!m->anyOn() && LogCount("beginFrame: the scene gives no player table") == 1);
	// a height field that gives no plot centres
	m = AirModule(&x);
	g_log.clear();
	std::vector<SrvFeed::PlayerRecord> players;
	c5r::heightfield::HeightGrid empty;
	f.players = &players;
	f.grid = &empty;
	m->beginFrame(f);
	EXPECT(!m->anyOn() && LogCount("beginFrame: the height field gives no plot centre") == 1 && g_log.size() == 1);
	// no grid this frame: nothing to do, still on
	g_log.clear();
	m = AirModule(&x);
	f.grid = nullptr;
	m->beginFrame(f);
	EXPECT(m->anyOn() && g_log.size() == 1);   // the "in effect" line only
	// the scene with a failed extra draws stock
	AirFixture a, b;
	BuiltFrame fa, fb;
	a.core.build(nullptr, &a.grid, 1100, fa);
	std::unique_ptr<UnitsExtras> mf = AirModule(&x);
	UnitExtrasFrame bad;
	mf->beginFrame(bad);   // failed
	b.core.setExtras(mf.get());
	b.core.build(nullptr, &b.grid, 1100, fb);
	EXPECT(SameScene(fa.copies[1], fb.copies[1]));
	// configure's row check: made with another option's id, the inner configure refuses
	g_log.clear();
	ForcedOn* r = new ForcedOn((Vp64Opt::Id)0);
	r->forwardConfigure = true;
	std::vector<std::unique_ptr<srvextras::UnitExtra>> v;
	v.emplace_back(r);
	std::unique_ptr<UnitsExtras> mr = srvextras::MakeUnitsExtrasWith(std::move(v));
	mr->setLog([](void*, const char* line) { Log(line); }, nullptr);
	mr->configure(Vp64Opt::Options(), true);
	EXPECT(!mr->anyOn() && LogCount("units extra FixAirBases: LOUD: failed: configure: its option row is not FixAirBases") == 1);
	// AirBaseCountsOf on another extra
	AirBaseCounts n;
	EXPECT(!srvextras::AirBaseCountsOf(*r, n));
	return true;
}

// Held in a fight: not shown (the extra's own rule and the seam's)
bool CaseHeld()
{
	AirFixture f;
	ForcedOn* x = nullptr;
	std::unique_ptr<UnitsExtras> m = AirModule(&x);
	f.core.setExtras(m.get());
	f.core.setHeldInFight(0, 4001, true);
	BuiltFrame b;
	f.core.build(nullptr, &f.grid, 1100, b);
	EXPECT(!Find(b, 0, 4001) && Find(b, 0, 4002) && f.core.counters().xHeld == 0);   // the extra asked nothing for it
	AirBaseCounts n;
	EXPECT(CountsOf(x, n) && n.held == 1 && n.shown == 1);
	f.core.setHeldInFight(0, 4001, false);
	f.core.build(nullptr, &f.grid, 1200, b);
	EXPECT(Find(b, 0, 4001));
	return true;
}

// The 4985 re-scope's sequence on synthetic tables (extras-units.md "4985 re-scope", W4 #7884 [L]): after a load every
// aircraft drawn; a live rebase city -> city (kind 30, the DLL's setXY and updateCenterUnit hide the rebased fighter and the
// aircraft left behind), a carrier move (the cargo teleported with it, kind 13, and hidden), a kill on a plot (the aircraft
// left hidden). Stock (flag off) stops drawing them; with the flag each is drawn, and its input equals the reload's of the
// same state.
struct Seq
{
	FakePort port;
	UnitsCore core;
	c5r::heightfield::HeightGrid grid = Grid();
	uint32_t seq = 1;
	double t = 1000;
	Seq() : core(port, Log)
	{
		port.players = {Player(0, 0, 0)};
		core.setDatabase(TestLookups(), nullptr);
	}
	SrvFeed::UnitRecord* rec(int id)
	{
		for(SrvFeed::UnitRecord& u : port.units)
			if(u.id == id)
				return &u;
		return nullptr;
	}
	void load(const std::vector<SrvFeed::UnitRecord>& units)
	{
		port.units = units;
		std::vector<SrvFeed::Event> ev;
		for(const SrvFeed::UnitRecord& u : port.units)
			ev.push_back(Ev(seq++, SrvFeed::kUnitCreated, u.owner, u.id, u.unitType, 0, u.plot));
		feed(ev);
	}
	void feed(const std::vector<SrvFeed::Event>& ev)
	{
		const SrvFeed::Batch b = BatchOf(ev);
		core.onBatch(b, t);
		t += 100;
	}
	void hide(std::vector<SrvFeed::Event>& ev, int id)
	{
		rec(id)->flags = (uint8_t)(rec(id)->flags & ~cu::kFlagVisible);
		rec(id)->seq = seq;
		ev.push_back(Ev(seq++, SrvFeed::kUnitVisibility, 0, id, 0));
	}
};

// city A plot 23 = (3, 2) with fighters 5001, 5002, bomber-like fighter 5003; city T plot 45 = (5, 4); carrier 5010 at
// plot 33 = (3, 3) with cargo 5011, 5012
std::vector<SrvFeed::UnitRecord> SeqTable()
{
	const uint8_t v = cu::kFlagVisible;
	return {Rec(0, 5001, kFighter, kAir, 23, v, 1),  Rec(0, 5002, kFighter, kAir, 23, v, 1), Rec(0, 5003, kFighter, kAir, 23, v, 1),
	        Rec(0, 5010, kCarrier, kSea, 33, v, 1), Rec(0, 5011, kFighter, kAir, 33, v, 1), Rec(0, 5012, kFighter, kAir, 33, v, 1)};
}

bool CaseLiveSequence()
{
	for(int arm = 0; arm < 2; ++arm)   // 0 stock (no extra), 1 the flag on
	{
		Seq s;
		std::unique_ptr<UnitsExtras> m;
		ForcedOn* x = nullptr;
		if(arm == 1)
		{
			m = AirModule(&x);
			s.core.setExtras(m.get());
		}
		s.load(SeqTable());
		BuiltFrame b;
		s.core.build(nullptr, &s.grid, s.t, b);
		EXPECT(b.listed == 6);   // after the load: every aircraft drawn by both
		// (b) rebase 5001 A -> T: kind 30, the record moves; its setXY into T hides it, updateCenterUnit hides 5002, 5003 in A
		{
			std::vector<SrvFeed::Event> ev;
			s.rec(5001)->plot = 45;
			ev.push_back(Ev(s.seq++, SrvFeed::kUnitRebased, 0, 5001, 23, 45, 45));
			s.hide(ev, 5001);
			s.hide(ev, 5002);
			s.hide(ev, 5003);
			s.feed(ev);
		}
		s.core.build(nullptr, &s.grid, s.t, b);
		if(arm == 0)
			EXPECT(b.listed == 3 && !Find(b, 0, 5001) && !Find(b, 0, 5002));   // stock: not drawn
		else
		{
			const cu::UnitInput* r = Find(b, 0, 5001);
			EXPECT(b.listed == 6 && r && r->plot_x == 5 && r->plot_y == 4 && !r->has_root && r->fade == 0 && Find(b, 0, 5002));
		}
		// (d) the carrier moves 33 -> 34; its cargo teleported with it (kind 13) and hidden by updateCenterUnit
		{
			std::vector<SrvFeed::Event> ev;
			s.rec(5010)->plot = 34;
			s.rec(5011)->plot = 34;
			s.rec(5012)->plot = 34;
			ev.push_back(Ev(s.seq++, SrvFeed::kUnitTeleported, 0, 5010, 34, 0, 34));
			ev.push_back(Ev(s.seq++, SrvFeed::kUnitTeleported, 0, 5011, 34, 0, 34));
			ev.push_back(Ev(s.seq++, SrvFeed::kUnitTeleported, 0, 5012, 34, 0, 34));
			s.hide(ev, 5011);
			s.hide(ev, 5012);
			s.feed(ev);
		}
		s.core.build(nullptr, &s.grid, s.t, b);
		EXPECT(b.listed == (arm == 0 ? 1u : 6u));
		if(arm == 1)
			EXPECT(Find(b, 0, 5011) && Find(b, 0, 5011)->plot_x == 4 && Find(b, 0, 5011)->plot_y == 3 && !Find(b, 0, 5011)->has_root);
		// a kill on the carrier's plot: 5012 destroyed (kind 15, then 14); 5011 stays hidden
		{
			std::vector<SrvFeed::Event> ev;
			s.rec(5012)->flags |= cu::kFlagDead;
			ev.push_back(Ev(s.seq++, SrvFeed::kUnitDestroyedInCombat, 0, 5012, 0, 0, 34));
			s.feed(ev);
		}
		s.core.build(nullptr, &s.grid, s.t, b);
		EXPECT(!Find(b, 0, 5012));   // dead: never shown by the extra
		{
			std::vector<SrvFeed::Event> ev;
			std::vector<SrvFeed::UnitRecord> keep;
			for(const SrvFeed::UnitRecord& u : s.port.units)
				if(u.id != 5012)
					keep.push_back(u);
			s.port.units = keep;
			ev.push_back(Ev(s.seq++, SrvFeed::kUnitDestroyed, 0, 5012, 0, 0, 34));
			s.feed(ev);
		}
		s.core.build(nullptr, &s.grid, s.t, b);
		EXPECT(b.listed == (arm == 0 ? 1u : 5u));
		if(arm == 1)
		{
			// the reload of exactly this state: every aircraft visible at its plot (stock after a load); the inputs equal
			Seq r;
			std::vector<SrvFeed::UnitRecord> t = s.port.units;
			for(SrvFeed::UnitRecord& u : t)
				u.flags |= cu::kFlagVisible;
			r.load(t);
			BuiltFrame rb;
			r.core.build(nullptr, &r.grid, r.t, rb);
			EXPECT(rb.listed == 5);
			for(int id : {5001, 5002, 5003, 5010, 5011})
				EXPECT(Find(b, 0, id) && Find(rb, 0, id) && SameInput(*Find(b, 0, id), *Find(rb, 0, id)));
			AirBaseCounts n;
			EXPECT(CountsOf(x, n) && n.walking == 0 && n.foreign == 0);
		}
	}
	return true;
}

// A cost indication (E8; the workstation, not a measurement for a claim, PERFORMANCE.md): the build of a 10,000-unit
// table, a third hidden aircraft, with no module and with the extra on
bool CaseCost()
{
	const int n = 10000;
	std::vector<SrvFeed::UnitRecord> t;
	for(int i = 0; i < n; ++i)
		t.push_back(Rec(0, 10000 + i, i % 3 ? kWarrior : kFighter, i % 3 ? kLand : kAir, i % 80, i % 3 ? cu::kFlagVisible : 0, 1));
	double ms[2] = {0, 0};
	size_t listed[2] = {0, 0};
	for(int arm = 0; arm < 2; ++arm)
	{
		Seq s;
		std::unique_ptr<UnitsExtras> m;
		if(arm)
		{
			m = AirModule();
			s.core.setExtras(m.get());
		}
		s.load(t);
		BuiltFrame b;
		s.core.build(nullptr, &s.grid, s.t, b);   // warm
		const int reps = 20;
		const auto t0 = std::chrono::steady_clock::now();
		for(int r = 0; r < reps; ++r)
			s.core.build(nullptr, &s.grid, s.t, b);
		ms[arm] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / reps;
		listed[arm] = b.listed;
	}
	EXPECT(listed[0] == (size_t)(n - (n + 2) / 3) && listed[1] == (size_t)n);
	printf("cost indication (workstation, not a result): build of %d units: %.3f ms without the module, %.3f ms with FixAirBases "
	       "on (%zu more listed)\n",
	       n, ms[0], ms[1], listed[1] - listed[0]);
	return true;
}

struct Case
{
	const char* name;
	bool (*fn)();
};
const Case kCases[] = {
    {"air_choice", CaseChoice}, {"air_scene_on", CaseSceneOn},         {"air_flag_off", CaseFlagOff},
    {"air_inputs", CaseInputs}, {"air_held", CaseHeld},                {"air_live_sequence", CaseLiveSequence},
    {"air_cost", CaseCost},
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
		printf("test_fix_air_bases: no case matches\n");
		return 2;
	}
	printf("test_fix_air_bases: %d of %d cases passed\n", passed, run);
	return passed == run ? 0 : 1;
}
