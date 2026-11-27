// test_fix_unit_placement.cpp - tests of FixUnitPlacement part A (epic 0455, issue 4982; server/src/extras/units/
// srv_unit_placement_fix.h): the terrain helpers and the pick on synthetic fields, the choice, the scene through the units
// seam, flag off and E5/E6, the ease-in on arrival, the per-plot cache, and a cost indication.
// Written 2026-11-20 for this repository. SPDX-License-Identifier: GPL-3.0-or-later
// Build: build_units_extras_tests.py --test test_fix_unit_placement. Run: test_fix_unit_placement.exe [case ...]; "CASE
// <name> PASS|FAIL <why>" per case. Exit: 0 every case passed, 1 a case failed, 2 no case matched. No install, no device.
#include <windows.h>

#include <algorithm>
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
#include "extras/units/srv_unit_placement_fix.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/units/placement.h"
#include "srv_options.h"

// server.exe's SrvOptions() (server/src/srv_options.cpp) for the render units: every option at its default
static Vp64Opt::Options g_options;
const Vp64Opt::Options& SrvOptions() { return g_options; }

using namespace srvunits;
namespace cu = c5r::units;
using srvextras::FlatGround;
using srvextras::PlacementChoice;
using srvextras::PlacementCounts;

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

bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

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

const int kWarrior = 7, kGalley = 9;
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
	l.units = {{kWarrior, "UNIT_WARRIOR"}, {kGalley, "UNIT_GALLEY"}};
	l.colours = {{3, "PLAYERCOLOR_POLYNESIA"}};
	l.civs = {{5, "CIVILIZATION_POLYNESIA"}};
	l.eras = {{0, "ERA_ANCIENT"}};
	l.artStyles = {{5, "ARTSTYLE_POLYNESIAN"}};
	return l;
}

// The synthetic lattice of test_fix_air_bases.cpp (mapW x (mapH + 4) plots of 64 x 64 samples, y scaled 0.866; plot centre
// (x, y) = ((x - (y >> 1)) 64 + 32 y, 64 y 0.866); inner radius 32), with sz = 1 so a sample of h is h / 255 units high, and a
// ridge: the ground falls 1 unit per unit of x from x = 0 to x = kRidgeFoot and is flat (0) east of it. Plot (3, 2) (centre
// x 192) stands on the slope 8 units west of the foot; plot (6, 2) (centre x 384) is flat.
const float kRidgeFoot = 200.0f;

float RidgeHeight(float x) { return x < kRidgeFoot ? kRidgeFoot - x : 0.0f; }

c5r::heightfield::HeightGrid Grid(uint32_t mapW = 10, uint32_t mapH = 8)
{
	c5r::heightfield::HeightGrid g;
	g.mapW = mapW;
	g.mapH = mapH + 4;
	g.samplesX = g.mapW * 64;
	g.samplesY = g.mapH * 64;
	g.sz = 1.0f;
	g.h.assign((size_t)g.samplesX * g.samplesY, 0);
	for(uint32_t r = 0; r < g.samplesY; ++r)
		for(uint32_t c = 0; c < g.samplesX; ++c)
			g.h[g.index(c, r)] = (uint16_t)std::lround(RidgeHeight((float)c) * 255.0f);
	g.waterZ = -5.0f;
	g.hexToWorld[0] = 64;
	g.hexToWorld[4] = 32;
	g.hexToWorld[5] = 64;
	g.hexToWorld[10] = 1;
	g.hexToWorld[15] = 1;
	return g;
}

// FixUnitPlacement with its state forced (the option row 23 comes with the rows of epic 0455, W3's hunk)
struct ForcedOn : srvextras::UnitExtra
{
	std::unique_ptr<srvextras::UnitExtra> in;
	bool on = true;
	bool forwardConfigure = false;
	explicit ForcedOn(Vp64Opt::Id id = Vp64Opt::kCount) : in(srvextras::MakeFixUnitPlacement(id)) {}
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
	bool animating() const override { return in->animating(); }
};

std::unique_ptr<UnitsExtras> PlaceModule(ForcedOn** out = nullptr, bool ours = true, bool on = true)
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

bool CountsOf(const srvextras::UnitExtra* x, PlacementCounts& c) { return x && srvextras::PlacementCountsOf(*x, c); }

// The table, a 10 x 8 grid, active team 0:
//   6001 warrior on the slope, plot 23 = (3, 2), visible        -> moved east (toward the flat foot)
//   6002 warrior on flat ground, plot 26 = (6, 2), visible      -> untouched (flat)
//   6003 embarked warrior on the slope, plot 22 = (2, 2)        -> untouched (embarked)
//   6004 galley (sea), plot 24 = (4, 2)                         -> untouched (not land)
//   6005 warrior on the slope, plot 21 = (1, 2), hidden         -> untouched (not drawn)
struct Fixture
{
	FakePort port;
	UnitsCore core;
	c5r::heightfield::HeightGrid grid = Grid();
	uint32_t seq = 1;
	Fixture() : core(port, Log)
	{
		const uint8_t v = cu::kFlagVisible;
		port.players = {Player(0, 0, 0)};
		port.units = {Rec(0, 6001, kWarrior, kLand, 23, v, 1), Rec(0, 6002, kWarrior, kLand, 26, v, 2),
		              Rec(0, 6003, kWarrior, kLand, 22, (uint8_t)(v | cu::kFlagEmbarked), 3),
		              Rec(0, 6004, kGalley, kSea, 24, v, 4), Rec(0, 6005, kWarrior, kLand, 21, 0, 5)};
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

bool SameInput(const cu::UnitInput& x, const cu::UnitInput& y)
{
	return x.player == y.player && x.id == y.id && x.unit_type == y.unit_type && x.art_row == y.art_row && x.plot_x == y.plot_x &&
	       x.plot_y == y.plot_y && x.has_root == y.has_root && x.root_x == y.root_x && x.root_y == y.root_y && x.root_z == y.root_z &&
	       x.facing == y.facing && x.flags == y.flags && x.fade_from == y.fade_from && x.fade_to == y.fade_to && x.fade == y.fade &&
	       x.damage == y.damage && x.max_hp == y.max_hp && x.alive == y.alive && x.formation_seed == y.formation_seed &&
	       x.foot_radius == y.foot_radius;
}

bool SameScene(const cu::UnitScene& a, const cu::UnitScene& b)
{
	if(a.units.size() != b.units.size() || a.players.size() != b.players.size())
		return false;
	for(size_t i = 0; i < a.units.size(); ++i)
		if(!SameInput(a.units[i], b.units[i]))
			return false;
	return true;
}

UnitExtraUnit XU(const SrvFeed::UnitRecord& r, bool listed = true, int px = 3, int py = 2)
{
	UnitExtraUnit u;
	u.rec = &r;
	u.plotX = px;
	u.plotY = py;
	u.listed = listed;
	u.hiding = !listed;
	u.facing = r.facing;
	return u;
}

const float kCx3 = 192.0f, kCy2 = 128.0f * 0.866f;   // plot (3, 2)'s centre

// ------------------------------------------------------------------------------------------------------------- cases
// The terrain helpers and the pick on the ridge
bool CaseTerrain()
{
	c5r::heightfield::HeightGrid g = Grid();
	// bilinear between samples: on the slope exactly the line, at the foot the kink's interpolation
	EXPECT(Near(srvextras::SurfaceAt(g, 150.0f, 50.0f), 50.0f, 0.01f));
	EXPECT(Near(srvextras::SurfaceAt(g, 150.5f, 50.3f), 49.5f, 0.01f));
	EXPECT(Near(srvextras::SurfaceAt(g, 199.5f, 50.0f), 0.5f, 0.01f) && Near(srvextras::SurfaceAt(g, 260.0f, 50.0f), 0.0f, 1e-6f));
	// the stock rule's one sample (truncated) against the bilinear surface: 0.5 apart at x + 0.5
	EXPECT(Near(cu::heightAtWorld(g, 150.5f, 50.0f), 50.0f, 0.01f));
	// mean slope: 1 on the slope, 0 on flat ground, between at the foot; the points under the water counted
	int water = -1;
	EXPECT(Near(srvextras::MeanSlope(g, 100.0f, 100.0f, 10.0f, 2.0f, &water), 1.0f, 0.01f) && water == 0);
	EXPECT(Near(srvextras::MeanSlope(g, 400.0f, 100.0f, 10.0f, 2.0f, &water), 0.0f, 1e-6f) && water == 0);
	const float foot = srvextras::MeanSlope(g, 200.0f, 100.0f, 10.0f, 2.0f, &water);
	EXPECT(foot > 0.3f && foot < 0.7f);
	g.waterZ = 0.25f;
	srvextras::MeanSlope(g, 400.0f, 100.0f, 10.0f, 2.0f, &water);
	EXPECT(water == 81);   // every point of the 2-unit grid within 10 (81 of them) under the water
	g.waterZ = -5.0f;

	// the pick: from plot (3, 2) (8 units west of the foot) east toward the flat ground, along the row
	FlatGround f = srvextras::FindFlatGround(g, kCx3, kCy2);
	EXPECT(f.candidates == 80);   // 81 points of the grid within 10, the centre not a candidate
	EXPECT(f.move && f.dx > 0 && std::fabs(f.dy) <= 2.0f && std::sqrt(f.dx * f.dx + f.dy * f.dy) <= srvextras::kFlatSearch + 1e-3f);
	EXPECT(f.slopeCentre > 0.6f && f.slopeCentre < 0.95f && f.slopePick < f.slopeCentre - srvextras::kFlatMinGain);
	// the pick's lattice gives MeanSlope's numbers at the centre and at the pick
	EXPECT(Near(f.slopeCentre, srvextras::MeanSlope(g, kCx3, kCy2, 10.0f, 2.0f, nullptr), 1e-4f));
	EXPECT(Near(f.slopePick, srvextras::MeanSlope(g, kCx3 + f.dx, kCy2 + f.dy, 10.0f, 2.0f, nullptr), 1e-4f));
	// flat ground: no move
	f = srvextras::FindFlatGround(g, 384.0f, kCy2);
	EXPECT(!f.move && f.dx == 0 && f.dy == 0 && f.slopeCentre == 0);
	// the middle of a uniform slope: every candidate costs the same; no move
	f = srvextras::FindFlatGround(g, 100.0f, kCy2);
	EXPECT(!f.move && Near(f.slopeCentre, 1.0f, 0.01f));
	// the water rule: the flat foot under water, so every eastern candidate stands by more water than the centre: no move
	g.waterZ = 0.25f;
	f = srvextras::FindFlatGround(g, kCx3, kCy2);
	EXPECT(!f.move);
	return true;
}

// The choice without a scene; the walking test; the ease-in share
bool CaseChoice()
{
	using srvextras::ChoosePlacement;
	const SrvFeed::UnitRecord land = Rec(0, 1, kWarrior, kLand, 23, cu::kFlagVisible, 1),
	                          sea = Rec(0, 2, kGalley, kSea, 23, cu::kFlagVisible, 1),
	                          air = Rec(0, 3, kWarrior, kAir, 23, cu::kFlagVisible, 1),
	                          emb = Rec(0, 4, kWarrior, kLand, 23, (uint8_t)(cu::kFlagVisible | cu::kFlagEmbarked), 1),
	                          dead = Rec(0, 5, kWarrior, kLand, 23, cu::kFlagDead, 1),
	                          dd = Rec(0, 6, kWarrior, kLand, 23, cu::kFlagDelayedDeath, 1);
	UnitOverride io;
	EXPECT(ChoosePlacement(XU(land), io, true, false).kind == PlacementChoice::Candidate);
	EXPECT(ChoosePlacement(XU(sea), io, true, false).kind == PlacementChoice::NotLand);
	EXPECT(ChoosePlacement(XU(air), io, true, false).kind == PlacementChoice::NotLand);
	EXPECT(ChoosePlacement(XU(emb), io, true, false).kind == PlacementChoice::Embarked);
	EXPECT(ChoosePlacement(XU(dead), io, true, false).kind == PlacementChoice::Dead);
	EXPECT(ChoosePlacement(XU(dd), io, true, false).kind == PlacementChoice::Dead);
	EXPECT(ChoosePlacement(XU(land, false), io, true, false).kind == PlacementChoice::NotDrawn);
	UnitOverride shown;
	shown.show = true;   // an earlier extra shows it: drawn
	EXPECT(ChoosePlacement(XU(land, false), shown, true, false).kind == PlacementChoice::Candidate);
	UnitExtraUnit held = XU(land);
	held.heldInFight = true;
	EXPECT(ChoosePlacement(held, io, true, false).kind == PlacementChoice::Held);
	UnitOverride rooted;
	rooted.root = true;
	EXPECT(ChoosePlacement(XU(land), rooted, true, false).kind == PlacementChoice::Rooted);
	EXPECT(ChoosePlacement(XU(land), io, false, false).kind == PlacementChoice::NoPlot);
	EXPECT(ChoosePlacement(XU(land), io, true, true).kind == PlacementChoice::Walking);
	UnitExtraUnit none;
	EXPECT(ChoosePlacement(none, io, true, false).kind == PlacementChoice::NotLand);

	using srvextras::PlacementWalking;
	const float inner = 32.0f;
	UnitExtraUnit w = XU(land);
	EXPECT(!PlacementWalking(w, 3, 2, true, kCx3, kCy2, inner));
	EXPECT(PlacementWalking(XU(land, true, 4, 2), 3, 2, true, kCx3, kCy2, inner));
	w.hasRoot = true;
	w.rootX = kCx3;
	w.rootY = kCy2 + 0.5f;
	EXPECT(!PlacementWalking(w, 3, 2, true, kCx3, kCy2, inner));   // 0.5 < 0.02 x 32
	w.rootY = kCy2 + 1.0f;
	EXPECT(PlacementWalking(w, 3, 2, true, kCx3, kCy2, inner));
	w.rootY = kCy2;
	EXPECT(PlacementWalking(w, 3, 2, false, kCx3, kCy2, inner));

	using srvextras::SettleShare;
	EXPECT(SettleShare(5000, -1) == 1.0f && SettleShare(1000, 1000) == 0.0f && SettleShare(900, 1000) == 0.0f);
	EXPECT(Near(SettleShare(1200, 1000), 0.5f, 1e-6f) && SettleShare(1400, 1000) == 1.0f && SettleShare(9000, 1000) == 1.0f);
	EXPECT(SettleShare(1100, 1000) < 0.25f && SettleShare(1300, 1000) > 0.75f);   // smoothstep
	EXPECT(srvextras::kFlatSearch == 10.0f && srvextras::kFlatStep == 2.0f && srvextras::kFlatRadius == 10.0f &&
	       srvextras::kPlacementDomainLand == 2 && srvextras::kSettleMs == 400.0);
	return true;
}

// The scene with the extra on: the slope's warrior gets a root east of its plot centre (the pick's offset), every other
// unit is as before; a second frame the same, from the cache
bool CaseSceneOn()
{
	g_log.clear();
	Fixture plain, f;
	BuiltFrame bp;
	plain.core.build(nullptr, &plain.grid, 1100, bp);
	ForcedOn* x = nullptr;
	std::unique_ptr<UnitsExtras> m = PlaceModule(&x);
	EXPECT(m->anyOn() && LogCount("units extra FixUnitPlacement: in effect") == 1);
	f.core.setExtras(m.get());
	BuiltFrame b;
	f.core.build(nullptr, &f.grid, 1100, b);
	const FlatGround pick = srvextras::FindFlatGround(f.grid, kCx3, kCy2);
	const cu::UnitInput* a = Find(b, 0, 6001);
	EXPECT(a && pick.move && a->has_root && a->root_x == kCx3 + pick.dx && Near(a->root_y, kCy2 + pick.dy, 1e-4f));
	EXPECT(a->plot_x == 3 && a->plot_y == 2);
	EXPECT(a->foot_radius == srvextras::kFootRadius);   // part B: moved, so its figures stand on the highest ground within 3
	EXPECT(!Find(bp, 0, 6001)->has_root && Find(bp, 0, 6001)->foot_radius == 0);
	for(int id : {6002, 6004})
	{
		EXPECT(Find(b, 0, id) && Find(bp, 0, id));
		EXPECT(SameInput(*Find(b, 0, id), *Find(bp, 0, id)));
	}
	// the embarked warrior: not drawn in either (this test has no embarked art tables), and the extra left it alone
	EXPECT(!Find(b, 0, 6003) && !Find(bp, 0, 6003));
	EXPECT(!Find(b, 0, 6005) && !Find(bp, 0, 6005) && b.listed == bp.listed && b.xforms.empty());
	EXPECT(f.core.counters().xRoot == 1 && f.core.counters().xShown == 0 && f.core.counters().xFoot == 1);
	PlacementCounts n;
	EXPECT(CountsOf(x->in.get(), n));
	EXPECT(n.frames == 1 && n.land == 4 && n.moved == 1 && n.flat == 1 && n.embarked == 1 && n.notDrawn == 1 && n.settling == 0 &&
	       n.picks == 2 && n.cachedPlots == 2 && n.feet == 1);
	EXPECT(!m->animating());   // at rest when first seen: the whole move at once
	srvextras::UnitExtraCounts mc;
	bool running = false, failed = true;
	EXPECT(srvextras::UnitExtraCountsOf(*m, 0, mc, running, failed) && running && !failed && mc.overrides == 1 && mc.shown == 0);
	// a second frame: the same root, no new pick
	f.core.build(nullptr, &f.grid, 1200, b);
	a = Find(b, 0, 6001);
	EXPECT(a && a->has_root && a->root_x == kCx3 + pick.dx);
	EXPECT(CountsOf(x->in.get(), n) && n.picks == 2 && n.moved == 2);
	return true;
}

// E2: the flag off is the scene before; the real extra with no row is off with one line; the default features; E5
bool CaseFlagOff()
{
	Fixture plain, off, real;
	BuiltFrame bp, bo, br;
	plain.core.build(nullptr, &plain.grid, 1100, bp);
	g_log.clear();
	ForcedOn* x = nullptr;
	std::unique_ptr<UnitsExtras> m = PlaceModule(&x, true, false);
	EXPECT(!m->anyOn() && LogCount("units FixUnitPlacement: off: its own flag is 0") == 1 && g_log.size() == 1);
	off.core.setExtras(m.get());
	off.core.build(nullptr, &off.grid, 1100, bo);
	EXPECT(SameScene(bp.copies[1], bo.copies[1]) && bp.listed == bo.listed);
	PlacementCounts n;
	EXPECT(CountsOf(x->in.get(), n) && n.frames == 0 && n.land == 0);
	g_log.clear();
	std::vector<std::unique_ptr<srvextras::UnitExtra>> v;
	v.push_back(srvextras::MakeFixUnitPlacement(Vp64Opt::kCount));
	std::unique_ptr<UnitsExtras> mr = srvextras::MakeUnitsExtrasWith(std::move(v));
	mr->setLog([](void*, const char* line) { Log(line); }, nullptr);
	mr->configure(Vp64Opt::Options(), true);
	EXPECT(!mr->anyOn() && g_log.size() == 1 && LogCount("units FixUnitPlacement: off: not an extra") == 1);
	real.core.setExtras(mr.get());
	real.core.build(nullptr, &real.grid, 1100, br);
	EXPECT(SameScene(bp.copies[1], br.copies[1]));
	// every option at its default except FixAirBases (default 1 since D235, 2026-11-22; switched off here)
	std::unique_ptr<UnitsExtras> feat = srvextras::MakeUnitsExtrasImpl();
	{
		const char* text = "[Extras]\nFixAirBases = 0\n";
		Vp64Opt::Options o;
		Vp64Opt::Parse(text, strlen(text), o, [](void*, const char*) {}, nullptr);
		feat->configure(o, true);
	}
	EXPECT(!feat->anyOn());
	g_log.clear();
	std::unique_ptr<UnitsExtras> m5 = PlaceModule(nullptr, false, true);
	EXPECT(!m5->anyOn() && g_log.size() == 1 && LogCount("units extra FixUnitPlacement: off: ServerUnits = 0") == 1);
	return true;
}

// E6: an input missing switches the extra off with one line; the scene draws stock; the row check
bool CaseInputs()
{
	g_log.clear();
	ForcedOn* x = nullptr;
	std::unique_ptr<UnitsExtras> m = PlaceModule(&x);
	UnitExtrasFrame f;   // no unit table
	m->beginFrame(f);
	EXPECT(!m->anyOn() &&
	       LogCount("units extra FixUnitPlacement: LOUD: failed: beginFrame: the scene gives no unit table: off for the session") == 1);
	g_log.clear();
	m = PlaceModule(&x);
	std::vector<SrvFeed::UnitRecord> units;
	c5r::heightfield::HeightGrid empty;
	f.units = &units;
	f.grid = &empty;
	m->beginFrame(f);
	EXPECT(!m->anyOn() && LogCount("beginFrame: the height field gives no plot centre") == 1 && g_log.size() == 2);
	g_log.clear();
	m = PlaceModule(&x);
	f.grid = nullptr;   // no field this frame: nothing to do, still on
	m->beginFrame(f);
	EXPECT(m->anyOn() && g_log.size() == 1);
	Fixture a, b;
	BuiltFrame fa, fb;
	a.core.build(nullptr, &a.grid, 1100, fa);
	std::unique_ptr<UnitsExtras> mf = PlaceModule(&x);
	UnitExtrasFrame bad;
	mf->beginFrame(bad);
	b.core.setExtras(mf.get());
	b.core.build(nullptr, &b.grid, 1100, fb);
	EXPECT(SameScene(fa.copies[1], fb.copies[1]));
	g_log.clear();
	ForcedOn* r = new ForcedOn((Vp64Opt::Id)0);
	r->forwardConfigure = true;
	std::vector<std::unique_ptr<srvextras::UnitExtra>> v;
	v.emplace_back(r);
	std::unique_ptr<UnitsExtras> mr = srvextras::MakeUnitsExtrasWith(std::move(v));
	mr->setLog([](void*, const char* line) { Log(line); }, nullptr);
	mr->configure(Vp64Opt::Options(), true);
	EXPECT(!mr->anyOn() && LogCount("units extra FixUnitPlacement: LOUD: failed: configure: its option row is not FixUnitPlacement") == 1);
	PlacementCounts n;
	EXPECT(!srvextras::PlacementCountsOf(*r, n));
	return true;
}

// Held in a fight: the extra asks nothing (the known limit: the formation is at the scene's root for the fight)
bool CaseHeld()
{
	Fixture f;
	ForcedOn* x = nullptr;
	std::unique_ptr<UnitsExtras> m = PlaceModule(&x);
	f.core.setExtras(m.get());
	f.core.setHeldInFight(0, 6001, true);
	BuiltFrame b;
	f.core.build(nullptr, &f.grid, 1100, b);
	EXPECT(Find(b, 0, 6001) && !Find(b, 0, 6001)->has_root && f.core.counters().xHeld == 0);
	PlacementCounts n;
	EXPECT(CountsOf(x->in.get(), n) && n.held == 1 && n.moved == 0);
	f.core.setHeldInFight(0, 6001, false);
	f.core.build(nullptr, &f.grid, 1200, b);
	EXPECT(Find(b, 0, 6001)->has_root);
	return true;
}

// The ease-in: a unit walking in (scene root off its plot centre) gets nothing; on arrival the move eases in over 400 ms
// (animating while it does); a unit at rest when first seen takes it at once; one that leaves snaps back
bool CaseSettle()
{
	c5r::heightfield::HeightGrid g = Grid();
	std::unique_ptr<srvextras::UnitExtra> x = srvextras::MakeFixUnitPlacement(Vp64Opt::kCount);
	std::vector<SrvFeed::UnitRecord> table = {Rec(0, 7001, kWarrior, kLand, 23, cu::kFlagVisible, 1)};
	UnitExtrasFrame fr;
	fr.grid = &g;
	fr.units = &table;
	fr.width = 10;
	fr.height = 8;
	const FlatGround pick = srvextras::FindFlatGround(g, kCx3, kCy2);
	EXPECT(pick.move);
	std::string why;
	auto frame = [&](double t, float rootX, UnitOverride& io) {
		fr.tMs = t;
		io = UnitOverride();
		UnitExtraUnit u = XU(table[0]);
		u.hasRoot = true;
		u.rootX = rootX;
		u.rootY = kCy2;
		return x->beginFrame(fr, why) && x->unit(u, io, why);
	};
	UnitOverride io;
	EXPECT(frame(1000, kCx3 - 20.0f, io) && !io.root && !x->animating());   // walking in
	EXPECT(frame(1100, kCx3, io) && io.root && io.rootX == kCx3 && x->animating());   // arrived: share 0
	EXPECT(frame(1300, kCx3, io) && Near(io.rootX, kCx3 + 0.5f * pick.dx, 1e-4f) && x->animating());
	EXPECT(frame(1500, kCx3, io) && io.rootX == kCx3 + pick.dx && !x->animating());
	EXPECT(frame(1600, kCx3 + 5.0f, io) && !io.root);   // walks off: the scene's root (snap back)
	PlacementCounts n;
	EXPECT(srvextras::PlacementCountsOf(*x, n) && n.walking == 2 && n.moved == 3 && n.settling == 2 && n.picks == 1);
	// a unit that leaves the table and comes back is first seen again (at once)
	std::vector<SrvFeed::UnitRecord> emptyTable;
	fr.units = &emptyTable;
	EXPECT(x->beginFrame(fr, why));
	EXPECT(x->beginFrame(fr, why));
	fr.units = &table;
	EXPECT(frame(2000, kCx3, io) && io.root && io.rootX == kCx3 + pick.dx && !x->animating());
	return true;
}

// The pick is cached per plot and taken again when the ground there changes (a live terrain change)
bool CaseCache()
{
	c5r::heightfield::HeightGrid g = Grid();
	std::unique_ptr<srvextras::UnitExtra> x = srvextras::MakeFixUnitPlacement(Vp64Opt::kCount);
	std::vector<SrvFeed::UnitRecord> table = {Rec(0, 7001, kWarrior, kLand, 23, cu::kFlagVisible, 1)};
	UnitExtrasFrame fr;
	fr.grid = &g;
	fr.units = &table;
	fr.width = 10;
	fr.height = 8;
	std::string why;
	UnitOverride io;
	for(int i = 0; i < 3; ++i)
	{
		io = UnitOverride();
		EXPECT(x->beginFrame(fr, why) && x->unit(XU(table[0]), io, why) && io.root);
	}
	PlacementCounts n;
	EXPECT(srvextras::PlacementCountsOf(*x, n) && n.picks == 1 && n.moved == 3);
	// the plot's ground flattened in place (same grid object): the next frame picks again and no longer moves
	for(uint32_t r = 0; r < g.samplesY; ++r)
		for(uint32_t c = 0; c < g.samplesX; ++c)
			g.h[g.index(c, r)] = 0;
	io = UnitOverride();
	EXPECT(x->beginFrame(fr, why) && x->unit(XU(table[0]), io, why) && !io.root);
	EXPECT(srvextras::PlacementCountsOf(*x, n) && n.picks == 2 && n.flat == 1);
	return true;
}

// E8 indication: a build of many land units on the slope, the first frame (every plot picked) and a cached one
bool CaseCost()
{
	const int n = 10000;
	std::vector<SrvFeed::UnitRecord> t;
	for(int i = 0; i < n; ++i)
		t.push_back(Rec(0, 20000 + i, kWarrior, kLand, i % 80, cu::kFlagVisible, 1));
	double ms[3] = {0, 0, 0};
	size_t listed[2] = {0, 0};
	for(int arm = 0; arm < 2; ++arm)
	{
		FakePort port;
		UnitsCore core(port, Log);
		c5r::heightfield::HeightGrid grid = Grid();
		port.players = {Player(0, 0, 0)};
		port.units = t;
		core.setDatabase(TestLookups(), nullptr);
		std::vector<SrvFeed::Event> ev;
		uint32_t seq = 1;
		for(const SrvFeed::UnitRecord& u : t)
			ev.push_back(Ev(seq++, SrvFeed::kUnitCreated, u.owner, u.id, u.unitType, 0, u.plot));
		core.onBatch(BatchOf(ev), 1000);
		std::unique_ptr<UnitsExtras> m;
		if(arm)
		{
			m = PlaceModule();
			core.setExtras(m.get());
		}
		BuiltFrame b;
		auto t0 = std::chrono::steady_clock::now();
		core.build(nullptr, &grid, 1100, b);   // first: every plot picked
		const double first = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		const int reps = 20;
		t0 = std::chrono::steady_clock::now();
		for(int r = 0; r < reps; ++r)
			core.build(nullptr, &grid, 1200, b);
		ms[arm] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / reps;
		if(arm)
			ms[2] = first;
		listed[arm] = b.listed;
	}
	EXPECT(listed[0] == listed[1]);
	printf("cost indication (workstation, not a result): build of %d land units on 80 plots: %.3f ms without the module, %.3f ms "
	       "with FixUnitPlacement on (cached picks), first frame with every plot picked %.3f ms\n",
	       n, ms[0], ms[1], ms[2]);
	return true;
}

// Part B: placement.cpp's footRadius (0 = the EXE's rule exactly; > 0 the highest ground within it; the water floor first)
// and the extra's FootRadiusFor
bool CaseFoot()
{
	using namespace c5r::units;
	FormationTable t;
	const c5r::Status ps = t.parse("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<Formations>\n"
	                               "  <Formation><Name>Line</Name><SpacingX>3</SpacingX><SpacingY>11</SpacingY><UnitRandomness>0</UnitRandomness>"
	                               "<TotalWidth>1</TotalWidth><YOffsetCurved>0</YOffsetCurved><YOffsetInc>0.5</YOffsetInc></Formation>\n"
	                               "</Formations>\n");
	EXPECT(ps.ok());
	c5r::heightfield::HeightGrid g = Grid();
	PlacementInput in;
	in.formation = "Line";
	in.zOffset = {0.0f, 0.25f, 0.0f};
	in.plotX = 2;   // centre x 128: every figure well inside the slope
	in.plotY = 2;
	in.facing = 1;
	in.seed = 7;
	in.waterLevel = g.waterZ;
	UnitPlacement p0, p;
	EXPECT(placeUnit(t, g, in, p0).ok() && p0.placed.size() == 3);
	// 0: the EXE's rule, exactly
	for(size_t i = 0; i < p0.placed.size(); ++i)
	{
		const FigurePlacement& f = p0.placed[i];
		EXPECT(f.z == (std::max)(heightAtWorld(g, f.x, f.y), in.waterLevel) + in.zOffset[i]);
	}
	// 3 on the ridge (falling 1 per unit of x): the highest ground within 3 is 3 units uphill (west) of the origin
	in.footRadius = 3.0f;
	EXPECT(placeUnit(t, g, in, p).ok() && p.placed.size() == 3);
	for(size_t i = 0; i < p.placed.size(); ++i)
	{
		const FigurePlacement& f = p.placed[i];
		EXPECT(f.x == p0.placed[i].x && f.y == p0.placed[i].y && f.yaw == p0.placed[i].yaw);   // only the height changes
		char at[160];
		snprintf(at, sizeof(at), "place_foot figure %zu at (%.3f, %.3f): z %.4f, stock %.4f, want %.4f", i, f.x, f.y, f.z,
		         p0.placed[i].z, RidgeHeight(f.x - 3.0f) + in.zOffset[i]);
		Log(at);
		EXPECT(f.x + 3.0f < kRidgeFoot && Near(f.z, RidgeHeight(f.x - 3.0f) + in.zOffset[i], 0.02f));
		EXPECT(f.z > p0.placed[i].z + 2.0f);
	}
	// flat ground (plot (6, 2)): the same as the EXE's rule
	in.plotX = 6;
	EXPECT(placeUnit(t, g, in, p).ok());
	for(const FigurePlacement& f : p.placed)
		EXPECT(f.z == (std::max)(heightAtWorld(g, f.x, f.y), in.waterLevel) + in.zOffset[f.figure]);
	// water: a figure whose own sample is at or under the water stands on the water level, footRadius or not (ships, and the
	// shore's wet side)
	in.plotX = 2;
	in.waterLevel = 1000.0f;
	EXPECT(placeUnit(t, g, in, p).ok());
	for(const FigurePlacement& f : p.placed)
		EXPECT(f.z == 1000.0f + in.zOffset[f.figure]);
	in.waterLevel = g.waterZ;
	// the range 0..16
	in.footRadius = 16.0f;
	EXPECT(placeUnit(t, g, in, p).ok());
	for(float bad : {-0.5f, 16.5f, std::nanf(""), INFINITY})
	{
		in.footRadius = bad;
		EXPECT(placeUnit(t, g, in, p).code == c5r::Err::BadLayout && p.placed.empty());
	}
	// the extra's choice: moved or rough -> kFootRadius; flat and not moved -> 0
	FlatGround fg;
	fg.move = true;
	EXPECT(srvextras::FootRadiusFor(fg) == srvextras::kFootRadius);
	fg.move = false;
	fg.slopeCentre = srvextras::kFootRoughSlope;
	EXPECT(srvextras::FootRadiusFor(fg) == srvextras::kFootRadius);
	fg.slopeCentre = srvextras::kFootRoughSlope * 0.99f;
	EXPECT(srvextras::FootRadiusFor(fg) == 0.0f);
	// the middle of a uniform slope does not move but is rough
	EXPECT(srvextras::FootRadiusFor(srvextras::FindFlatGround(g, 100.0f, kCy2)) == srvextras::kFootRadius);
	return true;
}

struct Case
{
	const char* name;
	bool (*fn)();
};
const Case kCases[] = {
    {"place_terrain", CaseTerrain}, {"place_choice", CaseChoice}, {"place_scene_on", CaseSceneOn},
    {"place_flag_off", CaseFlagOff}, {"place_inputs", CaseInputs}, {"place_held", CaseHeld},
    {"place_settle", CaseSettle},   {"place_cache", CaseCache},   {"place_cost", CaseCost},
    {"place_foot", CaseFoot},
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
		if(!ok)
			for(size_t i = 0; i < g_log.size() && i < 12; ++i)
				printf("  log: %s\n", g_log[i].c_str());
	}
	if(!run)
	{
		printf("test_fix_unit_placement: no case matches\n");
		return 2;
	}
	printf("test_fix_unit_placement: %d of %d cases passed\n", passed, run);
	return passed == run ? 0 : 1;
}
