// test_units_extras.cpp - tests of the units seam (epic 0455, issue 4981): srvunits::UnitsExtras (srv_units_scene.h), its
// use in UnitsCore (srv_units_core.cpp) and the unit host, and the module server/src/extras/units/srv_units_extras.cpp, with
// stand-in extras (no option row). Written 2026-11-20 for this repository. SPDX-License-Identifier: GPL-3.0-or-later
// Build: build_units_extras_tests.py. Run: test_units_extras.exe [case ...]; "CASE <name> PASS|FAIL <why>" per case.
// Exit: 0 every case passed, 1 a case failed, 2 no case matched. No install, no device.
#include <windows.h>

#include <algorithm>
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
#include "extras/units/srv_unit_garrison.h"
#include "../../../../render/src/extras/units/garrison_perimeter.h"   // issue 4983 (D243): the module's decorator
#include "srv_options.h"

// server.exe's SrvOptions() (server/src/srv_options.cpp) for the render units: every option at its default
static Vp64Opt::Options g_options;
const Vp64Opt::Options& SrvOptions() { return g_options; }

using namespace srvunits;
namespace cu = c5r::units;

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

SrvFeed::UnitRecord Rec(int owner, int id, int type, int plot, uint8_t flags = cu::kFlagVisible, uint32_t seq = 0)
{
	SrvFeed::UnitRecord r;
	memset(&r, 0, sizeof(r));
	r.owner = (int8_t)owner;
	r.id = id;
	r.unitType = type;
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
	l.units = {{7, "UNIT_WARRIOR"}, {8, "UNIT_FIGHTER"}};
	l.colours = {{3, "PLAYERCOLOR_POLYNESIA"}};
	l.civs = {{5, "CIVILIZATION_POLYNESIA"}};
	l.eras = {{0, "ERA_ANCIENT"}};
	l.artStyles = {{5, "ARTSTYLE_POLYNESIAN"}};
	return l;
}

// The table of the cases: 1001 a visible warrior (plot 23), 1002 a hidden fighter (plot 23: based in a city), 1003 a hidden
// dead warrior, 2001 a hidden invisible unit of player 1 (another team), 1004 a hidden invisible unit of player 0.
struct Fixture
{
	FakePort port;
	UnitsCore core;
	Fixture() : core(port, Log)
	{
		port.players = {Player(0, 0, 0), Player(1, 1, 0)};
		port.units = {Rec(0, 1001, 7, 23, cu::kFlagVisible, 1), Rec(0, 1002, 8, 23, 0, 2), Rec(0, 1003, 7, 24, cu::kFlagDead, 3),
		              Rec(0, 1004, 7, 25, cu::kFlagInvisible, 4), Rec(1, 2001, 7, 26, cu::kFlagInvisible, 5)};
		core.setDatabase(TestLookups(), nullptr);
		core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 23), Ev(2, SrvFeed::kUnitCreated, 0, 1002, 8, 0, 23),
		                      Ev(3, SrvFeed::kUnitCreated, 0, 1003, 7, 0, 24), Ev(4, SrvFeed::kUnitCreated, 0, 1004, 7, 0, 25),
		                      Ev(5, SrvFeed::kUnitCreated, 1, 2001, 7, 0, 26)}),
		             1000);
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
	if(a.units.size() != b.units.size() || a.players.size() != b.players.size() || a.position_source != b.position_source ||
	   a.water_level != b.water_level)
		return false;
	for(size_t i = 0; i < a.units.size(); ++i)
	{
		const cu::UnitInput &x = a.units[i], &y = b.units[i];
		if(x.player != y.player || x.id != y.id || x.unit_type != y.unit_type || x.art_row != y.art_row || x.plot_x != y.plot_x ||
		   x.plot_y != y.plot_y || x.has_root != y.has_root || x.root_x != y.root_x || x.root_y != y.root_y || x.root_z != y.root_z ||
		   x.facing != y.facing || x.formation_seed != y.formation_seed || x.flags != y.flags || x.fade_from != y.fade_from ||
		   x.fade_to != y.fade_to || x.fade != y.fade)
			return false;
	}
	for(size_t i = 0; i < a.players.size(); ++i)
		if(a.players[i].player != b.players[i].player || a.players[i].colour != b.players[i].colour)
			return false;
	return true;
}

// A stand-in extra: no option row; its state and every reply set by the case.
struct StandIn : srvextras::UnitExtra
{
	std::string nm = "StandIn";
	bool on = true;
	Vp64Opt::ExtraWhy offWhy = Vp64Opt::kExtraFlagOff;
	std::function<bool(const UnitExtraUnit&, UnitOverride&, std::string&)> fn;
	int batches = 0, frames = 0, units = 0;
	bool sawHeld = false;
	bool throwInBatch = false;
	const char* name() const override { return nm.c_str(); }
	Vp64Opt::ExtraState state(const Vp64Opt::Options&) const override
	{
		Vp64Opt::ExtraState s;
		s.on = on;
		s.value = on ? 1 : 0;
		s.why = on ? Vp64Opt::kExtraOn : offWhy;
		s.decidedBy = Vp64Opt::kCount;
		return s;
	}
	void onBatch(const SrvFeed::Batch&) override
	{
		batches++;
		if(throwInBatch)
			throw std::runtime_error("batch boom");
	}
	bool beginFrame(const UnitExtrasFrame& f, std::string&) override
	{
		frames++;
		return f.units != nullptr && f.players != nullptr && f.width == 10;
	}
	bool unit(const UnitExtraUnit& u, UnitOverride& io, std::string& why) override
	{
		units++;
		sawHeld |= u.heldInFight;
		return fn ? fn(u, io, why) : true;
	}
};

std::unique_ptr<UnitsExtras> ModuleOf(std::vector<StandIn*> xs, bool ours = true)
{
	std::vector<std::unique_ptr<srvextras::UnitExtra>> v;
	for(StandIn* x : xs)
		v.emplace_back(x);
	std::unique_ptr<UnitsExtras> m = srvextras::MakeUnitsExtrasWith(std::move(v));
	m->setLog([](void*, const char* line) { Log(line); }, nullptr);
	Vp64Opt::Options o;
	m->configure(o, ours);
	return m;
}

// Options parsed from an options file's text (warnings dropped).
static Vp64Opt::Options OptsFromText(const char* text)
{
	Vp64Opt::Options o;
	Vp64Opt::Parse(text, strlen(text), o, [](void*, const char*) {}, nullptr);
	return o;
}

// ------------------------------------------------------------------------------------------------------------- cases
// No module (null), a module with no extra on, and a module whose extra changes nothing: the same frames as before.
bool CaseNullFactory()
{
	const UnitsExtrasFactory keep = nullptr;
	(void)keep;
	EXPECT(MakeUnitsExtras() != nullptr);   // this program links server/src/extras/units/ (its registrar ran)
	RegisterUnitsExtrasFactory(nullptr);
	EXPECT(MakeUnitsExtras() == nullptr);   // a program without the folder
	RegisterUnitsExtrasFactory([]() { return srvextras::MakeUnitsExtrasImpl(); });
	std::unique_ptr<UnitsExtras> features = MakeUnitsExtras();
	EXPECT(features != nullptr);
	features->setLog([](void*, const char* line) { Log(line); }, nullptr);
	features->configure(Vp64Opt::Options(), true);
	EXPECT(features->anyOn());   // FixAirBases (26) on under the defaults since D235 (2) (2026-11-22); 24, 25 off
	features = MakeUnitsExtras();
	features->setLog([](void*, const char* line) { Log(line); }, nullptr);
	features->configure(OptsFromText("[Extras]\nFixAirBases = 0\n"), true);
	EXPECT(!features->anyOn());   // GarrisonVisible, ShipsMoored off under the defaults, FixAirBases off by the file

	Fixture a, b, c;
	BuiltFrame fa, fb, fc;
	a.core.build(nullptr, nullptr, 1100, fa);
	std::unique_ptr<UnitsExtras> none = ModuleOf({new StandIn()}, true);
	none->configure(Vp64Opt::Options(), true);
	EXPECT(none->anyOn());
	b.core.setExtras(none.get());   // on, changes nothing
	b.core.build(nullptr, nullptr, 1100, fb);
	c.core.setExtras(features.get());   // none on: the core does not call it
	c.core.build(nullptr, nullptr, 1100, fc);
	EXPECT(SameScene(fa.copies[1], fb.copies[1]) && SameScene(fa.copies[1], fc.copies[1]));
	EXPECT(fa.listed == fb.listed && fa.listed == 1 && fb.xforms.empty());
	EXPECT(Find(fa, 0, 1001) && !Find(fa, 0, 1002));
	const CoreCounters& k = b.core.counters();
	EXPECT(k.xShown == 0 && k.xRoot == 0 && k.xHeading == 0 && k.xScaled == 0 && k.xBad == 0 && k.xHeld == 0);

	// the host: attachExtras(null) and a module with nothing on keep no module; the lines carry no extras part
	FakePort port;
	std::unique_ptr<SceneUnits> host = MakeSceneUnitsWith(UnitsOptions(), port,
	                                                      [](const std::wstring&, Env&, std::string& why) {
		                                                      why = "test";
		                                                      return false;
	                                                      },
	                                                      Log, []() { return 0.0; });
	UnitsHostProbe* probe = dynamic_cast<UnitsHostProbe*>(host.get());
	EXPECT(probe);
	g_log.clear();
	probe->attachExtras(nullptr, Vp64Opt::Options(), true);
	EXPECT(!probe->extras() && !probe->core().extras());
	EXPECT(LogCount("units: extras: module not linked (the scene as before)") == 1 && g_log.size() == 1);
	probe->attachExtras(srvextras::MakeUnitsExtrasImpl(), OptsFromText("[Extras]\nFixAirBases = 0\n"), true);
	EXPECT(!probe->extras() && !probe->core().extras());
	EXPECT(LogCount("units: extras: module linked, 0 in effect: dropped (the scene as before)") == 1 && g_log.size() == 8);
	EXPECT(LogCount("units PerimeterGarrisons: off: EnableVisualAdditions = 0 (default) switches off every visual addition") ==
	       1);   // issue 4983 (D243)
	EXPECT(LogCount("units FixUnitPlacement: off: FixUnitPlacement = 0 (default)") == 1);   // issue 4982
	EXPECT(LogCount("units AirInterceptPatrol: off: EnableVisualAdditions = 0 (default) switches off every visual addition") == 1);
	EXPECT(LogCount("units GarrisonVisible: off: EnableVisualAdditions = 0 (default) switches off every visual addition") == 1);
	EXPECT(LogCount("units ShipsMoored: off: EnableVisualAdditions = 0 (default) switches off every visual addition") == 1);
	EXPECT(LogCount("units FixAirBases: off: FixAirBases = 0 (from the file)") == 1);
	char line[4096];
	host->totalsLine(line, sizeof(line));
	EXPECT(strstr(line, " | x ") == nullptr && strstr(line, "extras") == nullptr);
	return true;
}

bool CaseOverrideRoot()
{
	Fixture f;
	StandIn* x = new StandIn();
	x->fn = [](const UnitExtraUnit& u, UnitOverride& io, std::string&) {
		if(u.rec->id == 1001)
		{
			if(u.hasRoot || u.plotX != 3 || u.plotY != 2 || u.facing != 2 || !u.listed || u.hiding)
				return false;   // the scene's own root and heading as inputs
			io.root = true;
			io.rootX = 250.5f;
			io.rootY = 140.25f;
		}
		return true;
	};
	std::unique_ptr<UnitsExtras> m = ModuleOf({x});
	f.core.setExtras(m.get());
	BuiltFrame b;
	f.core.build(nullptr, nullptr, 1100, b);
	const cu::UnitInput* u = Find(b, 0, 1001);
	EXPECT(u && u->has_root && u->root_x == 250.5f && u->root_y == 140.25f && u->plot_x == 3 && u->plot_y == 2);
	EXPECT(x->frames == 1 && x->units == 5);   // every table unit, listed or not
	EXPECT(f.core.counters().xRoot == 1 && b.xforms.empty());
	srvextras::UnitExtraCounts c;
	bool running = false, failed = true;
	EXPECT(srvextras::UnitExtraCountsOf(*m, 0, c, running, failed) && running && !failed && c.overrides == 1 && c.shown == 0);
	// onBatch reaches the extra after the scene took the batch
	f.core.onBatch(BatchOf({Ev(6, SrvFeed::kPlotState, 0, 0)}), 1200);
	EXPECT(x->batches == 1);
	return true;
}

bool CaseShow()
{
	Fixture f;
	StandIn* x = new StandIn();
	x->fn = [](const UnitExtraUnit& u, UnitOverride& io, std::string&) {
		io.show = true;   // asks for every unit
		(void)u;
		return true;
	};
	std::unique_ptr<UnitsExtras> m = ModuleOf({x});
	f.core.setExtras(m.get());
	BuiltFrame b;
	f.core.build(nullptr, nullptr, 1100, b);
	// 1001 visible (no-op), 1002 hidden: shown opaque; 1003 dead and 2001 invisible of another team: refused; 1004 invisible of
	// the active team: shown
	const cu::UnitInput* s = Find(b, 0, 1002);
	EXPECT(s && s->fade_from == 1 && s->fade_to == 0 && s->fade == 0 && s->unit_type == "UNIT_FIGHTER");
	uint32_t seed = 99;
	EXPECT(f.core.seedOf(0, 1002, seed) && s->formation_seed == seed);   // the allocator's seed, as any drawn unit
	EXPECT(Find(b, 0, 1001) && Find(b, 0, 1004) && !Find(b, 0, 1003) && !Find(b, 1, 2001));
	EXPECT(b.listed == 3 && f.core.counters().xShown == 2 && f.core.counters().xShowRefused == 2);
	srvextras::UnitExtraCounts c;
	bool running, failed;
	EXPECT(srvextras::UnitExtraCountsOf(*m, 0, c, running, failed) && c.shown == 4);   // asked for the four hidden units
	// the fog filter still applies to a shown unit: 2001 made visible-flag-free but not invisible, on an unseen plot
	Fixture g;
	g.port.units[4].flags = 0;   // 2001 hidden, not invisible
	g.port.units[4].seq = 6;
	g.core.onBatch(BatchOf({Ev(6, SrvFeed::kUnitVisibility, 1, 2001, 0)}), 1100);
	g.core.setExtras(m.get());
	BuiltFrame gb;
	g.core.build(nullptr, nullptr, 1200, gb);
	EXPECT(Find(gb, 1, 2001));   // shown by the extra ...
	std::vector<uint8_t> states(80, 2);   // ... every plot unexplored for the active team 0
	const FogFilterResult r = FilterByFog(gb, g.core.players(), 10, states.data(), states.size(), 10, 8);
	EXPECT(std::string(r.state) == "on" && !Find(gb, 1, 2001) && Find(gb, 0, 1002));   // ... and filtered by the fog
	// a fade-out in progress is made opaque by a show
	Fixture h;
	h.port.units[0].flags = 0;
	h.port.units[0].seq = 6;
	int bits;
	const float blend = 0.5f;
	memcpy(&bits, &blend, 4);
	h.core.onBatch(BatchOf({Ev(6, SrvFeed::kUnitVisibility, 0, 1001, 0, bits)}), 2000);
	BuiltFrame hb;
	h.core.build(nullptr, nullptr, 2100, hb);
	EXPECT(Find(hb, 0, 1001) && Find(hb, 0, 1001)->fade > 0 && hb.fading);   // stock: fading out
	h.core.setExtras(m.get());
	h.core.build(nullptr, nullptr, 2100, hb);
	EXPECT(Find(hb, 0, 1001) && Find(hb, 0, 1001)->fade == 0 && Find(hb, 0, 1001)->fade_from == 1 && !hb.fading);
	return true;
}

bool CaseScaleHeading()
{
	Fixture f;
	StandIn* x = new StandIn();
	x->fn = [](const UnitExtraUnit& u, UnitOverride& io, std::string&) {
		if(u.rec->id == 1001)
		{
			io.scale = 0.5f;
			io.heading = true;
			io.yaw = 1.0f;
		}
		return true;
	};
	std::unique_ptr<UnitsExtras> m = ModuleOf({x});
	f.core.setExtras(m.get());
	BuiltFrame b;
	f.core.build(nullptr, nullptr, 1100, b);
	EXPECT(b.xforms.size() == 1);
	const UnitXform& xf = b.xforms[UnitKeyOf(0, 1001)];
	EXPECT(xf.heading && xf.yaw == 1.0f && xf.scale == 0.5f);
	EXPECT(f.core.counters().xScaled == 1 && f.core.counters().xHeading == 1);
	const cu::UnitInput* u = Find(b, 0, 1001);
	EXPECT(u && !u->has_root && u->facing == 2);   // the scene's input unchanged: turn and scale after prepare

	// ApplyUnitXforms on a synthetic plan: unit (0, 1001) with two figures at z 5 around root (10, 20), placement yaw 0.25;
	// one skinned block (bone 0 = [I | (12, 21, 5)]) and one material-87 draw (world rows the same)
	cu::FramePlan plan;
	cu::UnitPlan up;
	up.player = 0;
	up.id = 1001;
	up.placement.rootX = 10;
	up.placement.rootY = 20;
	up.placement.yaw = 0.25f;
	cu::FigurePlacement f1, f2;
	f1.x = 12, f1.y = 21, f1.z = 4;
	f2.x = 8, f2.y = 19, f2.z = 6;
	up.placement.placed = {f1, f2};
	up.first_block = 1;   // block 0 belongs to another unit
	up.blocks = 1;
	plan.blocks = 2;
	plan.palette.assign(2 * cu::kPaletteRows, cu::Float4());
	for(uint32_t b2 = 0; b2 < 2; ++b2)
		for(uint32_t k = 0; k < cu::kPaletteBones; ++k)
		{
			cu::Float4* r = &plan.palette[b2 * cu::kPaletteRows + 2 + 3 * k];
			r[0] = {1, 0, 0, 12};
			r[1] = {0, 1, 0, 21};
			r[2] = {0, 0, 1, 5};
		}
	plan.palette[cu::kPaletteRows + 1] = {7, 7, 7, 7};   // the fade row stays
	cu::MeshDraw md;
	md.material = cu::Material::Static;
	md.cb0.assign(16, cu::Float4());
	md.cb0[0] = {1, 0, 0, 12};
	md.cb0[1] = {0, 1, 0, 21};
	md.cb0[2] = {0, 0, 1, 5};
	md.cb0[4] = {1, 0, 0, 0};
	up.draws.push_back(md);
	plan.units.push_back(up);
	cu::UnitPlan other = up;
	other.id = 1002;
	other.first_block = 0;
	plan.units.push_back(other);
	EXPECT(ApplyUnitXforms(plan, b.xforms) == 1);
	// P = (10, 20, 5); d = 1 - 0.25 = 0.75; L = 0.5 Rz(0.75); bone 0's origin (12, 21, 5) -> P + L (2, 1, 0)
	const float c = std::cos(0.75f), s = std::sin(0.75f);
	const float ex = 10 + 0.5f * (c * 2 - s * 1), ey = 20 + 0.5f * (s * 2 + c * 1);
	const cu::Float4* r = &plan.palette[cu::kPaletteRows + 2];
	EXPECT(std::fabs(r[0].w - ex) < 1e-5f && std::fabs(r[1].w - ey) < 1e-5f && std::fabs(r[2].w - 5) < 1e-5f);
	EXPECT(std::fabs(r[0].x - 0.5f * c) < 1e-6f && std::fabs(r[0].y + 0.5f * s) < 1e-6f && r[0].z == 0);
	EXPECT(std::fabs(r[1].x - 0.5f * s) < 1e-6f && std::fabs(r[1].y - 0.5f * c) < 1e-6f && std::fabs(r[2].z - 0.5f) < 1e-6f);
	const cu::Float4* last = &plan.palette[cu::kPaletteRows + 2 + 3 * 31];
	EXPECT(std::fabs(last[0].w - ex) < 1e-5f);   // every bone triple
	EXPECT(plan.palette[cu::kPaletteRows + 1].x == 7);
	EXPECT(plan.palette[2].w == 12 && plan.palette[2].x == 1);   // the other unit's block untouched
	const cu::Float4* w = plan.units[0].draws[0].cb0.data();
	EXPECT(std::fabs(w[0].w - ex) < 1e-5f && std::fabs(w[1].w - ey) < 1e-5f && w[4].x == 1);
	EXPECT(plan.units[1].draws[0].cb0[0].w == 12);
	// scale 1 with the placement's own yaw: nothing changes
	std::map<int64_t, UnitXform> same;
	same[UnitKeyOf(0, 1002)].heading = true;
	same[UnitKeyOf(0, 1002)].yaw = 0.25f;
	EXPECT(ApplyUnitXforms(plan, same) == 0 && plan.palette[2].w == 12);
	// issue 4986 (H2): a lift alone raises every bone and world row by it, nothing else; unit 1002 (block 0, z 5)
	std::map<int64_t, UnitXform> up40;
	up40[UnitKeyOf(0, 1002)].lift = 40;
	EXPECT(ApplyUnitXforms(plan, up40) == 1);
	EXPECT(plan.palette[2].w == 12 && plan.palette[3].w == 21 && std::fabs(plan.palette[4].w - 45) < 1e-5f &&
	       plan.palette[4].z == 1 && plan.palette[2].x == 1);
	EXPECT(std::fabs(plan.palette[2 + 3 * 31 + 2].w - 45) < 1e-5f && std::fabs(plan.units[1].draws[0].cb0[2].w - 45) < 1e-5f);
	EXPECT(std::fabs(r[2].w - 5) < 1e-5f);   // unit 1001's rows untouched
	// through the core: a lift out of range is ignored (x_bad), in range lands in the frame's xforms
	Fixture g;
	StandIn* y = new StandIn();
	float want = 30;
	y->fn = [&want](const UnitExtraUnit& u, UnitOverride& io, std::string&) {
		if(u.rec->id == 1001)
			io.lift = want;
		return true;
	};
	std::unique_ptr<UnitsExtras> mg = ModuleOf({y});
	g.core.setExtras(mg.get());
	BuiltFrame bg;
	g.core.build(nullptr, nullptr, 1100, bg);
	EXPECT(bg.xforms.size() == 1 && bg.xforms[UnitKeyOf(0, 1001)].lift == 30 && !bg.xforms[UnitKeyOf(0, 1001)].heading &&
	       bg.xforms[UnitKeyOf(0, 1001)].scale == 1);
	return true;
}

// E5: with ServerUnits = 0 every unit extra is off with one line
bool CaseServerUnitsOff()
{
	g_log.clear();
	StandIn* a = new StandIn();
	StandIn* b = new StandIn();
	b->nm = "StandInOff";
	b->on = false;
	b->offWhy = Vp64Opt::kExtraCategoryOff;
	std::unique_ptr<UnitsExtras> m = ModuleOf({a, b}, false);
	EXPECT(!m->anyOn());
	EXPECT(LogCount("units extra StandIn: off: ServerUnits = 0") == 1 && LogCount("off: ServerUnits = 0") == 1);
	EXPECT(LogCount("units StandInOff: off: its category switch is 0") == 1);
	EXPECT(g_log.size() == 2);
	// with the units ours: one "in effect" line
	g_log.clear();
	m->configure(Vp64Opt::Options(), true);
	EXPECT(m->anyOn() && LogCount("units extra StandIn: in effect") == 1);
	// the host drops a module with nothing on: no per-unit call, no counts in the lines
	FakePort port;
	std::unique_ptr<SceneUnits> host = MakeSceneUnitsWith(UnitsOptions(), port,
	                                                      [](const std::wstring&, Env&, std::string& why) {
		                                                      why = "test";
		                                                      return false;
	                                                      },
	                                                      Log, []() { return 0.0; });
	UnitsHostProbe* probe = dynamic_cast<UnitsHostProbe*>(host.get());
	StandIn* c = new StandIn();
	std::vector<std::unique_ptr<srvextras::UnitExtra>> v;
	v.emplace_back(c);
	probe->attachExtras(srvextras::MakeUnitsExtrasWith(std::move(v)), Vp64Opt::Options(), false);
	EXPECT(!probe->extras() && !probe->core().extras());
	return true;
}

// E6: a failing extra is off for the session with one line; the unit keeps what the others gave; the others go on
bool CaseFailingExtra()
{
	g_log.clear();
	Fixture f;
	StandIn* good = new StandIn();
	good->nm = "Good";
	good->fn = [](const UnitExtraUnit& u, UnitOverride& io, std::string&) {
		if(u.rec->id == 1001)
		{
			io.root = true;
			io.rootX = 100;
			io.rootY = 50;
		}
		return true;
	};
	StandIn* bad = new StandIn();
	bad->nm = "Bad";
	bad->fn = [](const UnitExtraUnit& u, UnitOverride& io, std::string& why) {
		if(u.rec->id == 1001)
		{
			io.rootX = 1;   // a change it does not keep: the failure drops it
			why = "the stand-in fails";
			return false;
		}
		return true;
	};
	StandIn* thrower = new StandIn();
	thrower->nm = "Thrower";
	thrower->fn = [](const UnitExtraUnit&, UnitOverride&, std::string&) -> bool { throw std::runtime_error("boom"); };
	StandIn* range = new StandIn();
	range->nm = "Range";
	range->fn = [](const UnitExtraUnit&, UnitOverride& io, std::string&) {
		io.scale = 9;   // outside (0, 4]
		return true;
	};
	std::unique_ptr<UnitsExtras> m = ModuleOf({good, bad, thrower, range});
	f.core.setExtras(m.get());
	BuiltFrame b;
	f.core.build(nullptr, nullptr, 1100, b);
	f.core.build(nullptr, nullptr, 1200, b);
	const cu::UnitInput* u = Find(b, 0, 1001);
	EXPECT(u && u->has_root && u->root_x == 100 && u->root_y == 50);   // Good's root kept, Bad's change dropped
	EXPECT(b.xforms.empty());                                           // Range's scale dropped
	EXPECT(LogCount("units extra Bad: LOUD: failed: unit (0, 1001) at frame") == 1 && LogCount("the stand-in fails") == 1);
	EXPECT(LogCount("units extra Thrower: LOUD: failed:") == 1 && LogCount("boom") == 1);
	EXPECT(LogCount("units extra Range: LOUD: failed:") == 1 && LogCount("a scale outside (0, 4]") == 1);
	EXPECT(LogCount("failed") == 3);   // one line each, the second frame adds none
	EXPECT(m->anyOn());
	srvextras::UnitExtraCounts c;
	bool running, failed;
	EXPECT(srvextras::UnitExtraCountsOf(*m, 1, c, running, failed) && !running && failed && c.failures == 1);
	EXPECT(srvextras::UnitExtraCountsOf(*m, 0, c, running, failed) && running && !failed && c.overrides == 2);
	char text[1000];
	m->countsText(text, sizeof(text));
	EXPECT(strstr(text, " | extras Good on ov=2 shown=0 scaled=0 failures=0") && strstr(text, " | extras Bad failed ov=0") &&
	       strstr(text, " | extras Range failed"));
	// a throw in onBatch fails that extra the same way
	StandIn* tb = new StandIn();
	tb->nm = "BatchThrower";
	tb->throwInBatch = true;
	std::unique_ptr<UnitsExtras> m2 = ModuleOf({tb});
	m2->onBatch(BatchOf({}));
	EXPECT(!m2->anyOn() && LogCount("units extra BatchThrower: LOUD: failed: onBatch: an exception: batch boom") == 1);
	return true;
}

// lead-0042's contract (#7520): a unit held in a fight gets no override; the extra reads the flag
bool CaseHeldInFight()
{
	Fixture f;
	StandIn* x = new StandIn();
	x->fn = [](const UnitExtraUnit& u, UnitOverride& io, std::string&) {
		if(u.rec->id == 1001 || u.rec->id == 1002)
		{
			io.root = true;
			io.rootX = 1;
			io.rootY = 2;
			io.scale = 2;
			io.show = true;
		}
		return true;
	};
	std::unique_ptr<UnitsExtras> m = ModuleOf({x});
	f.core.setExtras(m.get());
	f.core.setHeldInFight(0, 1001, true);
	f.core.setHeldInFight(0, 1002, true);
	EXPECT(f.core.heldInFight(0, 1001) && !f.core.heldInFight(0, 1004));
	BuiltFrame b;
	f.core.build(nullptr, nullptr, 1100, b);
	const cu::UnitInput* u = Find(b, 0, 1001);
	EXPECT(u && !u->has_root && b.xforms.empty() && !Find(b, 0, 1002) && x->sawHeld);
	EXPECT(f.core.counters().xHeld == 2 && f.core.counters().xRoot == 0 && f.core.counters().xShown == 0);
	f.core.setHeldInFight(0, 1001, false);
	f.core.build(nullptr, nullptr, 1200, b);
	EXPECT(Find(b, 0, 1001) && Find(b, 0, 1001)->has_root && b.xforms.size() == 1);
	return true;
}

// The host: the module's counts in the "units 1s" and "units totals" lines; animating from an extra
bool CaseHostLines()
{
	g_log.clear();
	FakePort port;
	std::unique_ptr<SceneUnits> host = MakeSceneUnitsWith(UnitsOptions(), port,
	                                                      [](const std::wstring&, Env&, std::string& why) {
		                                                      why = "test";
		                                                      return false;
	                                                      },
	                                                      Log, []() { return 0.0; });
	UnitsHostProbe* probe = dynamic_cast<UnitsHostProbe*>(host.get());
	struct Moving : StandIn
	{
		bool animating() const override { return true; }
	};
	Moving* x = new Moving();
	std::vector<std::unique_ptr<srvextras::UnitExtra>> v;
	v.emplace_back(x);
	probe->attachExtras(srvextras::MakeUnitsExtrasWith(std::move(v)), Vp64Opt::Options(), true);
	EXPECT(probe->extras() && probe->core().extras() == probe->extras());
	EXPECT(LogCount("units extra StandIn: in effect") == 1);
	EXPECT(LogCount("units: extras: module linked, 1 in effect: attached") == 1);
	EXPECT(host->animating());
	host->feed(BatchOf({Ev(1, SrvFeed::kPlotState, 0, 0)}), 10);
	EXPECT(x->batches == 1);
	char line[8192];
	host->secondLine(line, sizeof(line));
	EXPECT(strstr(line, " | x shown=0 show_refused=0 root=0 heading=0 scaled=0 foot=0 held=0 bad=0 | extras StandIn on ov=0"));
	host->totalsLine(line, sizeof(line));
	EXPECT(strstr(line, " | x shown=0") && strstr(line, " | extras StandIn on"));
	return true;
}

// ---- issue 4983: GarrisonVisible (srv_unit_garrison.h) ----------------------------------------------------------------

// A flat synthetic height field as test_scene_units.cpp's: mapW x (mapH + 4) plots, a hex lattice of 64 x 64 units (y
// scaled by sy 0.866), height 0. Plot centre (x, y): ((x - (y >> 1)) 64 + 32 y, 64 y 0.866); the inner radius 32.
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

bool Near(float a, float b, float tol = 1e-4f) { return std::fabs(a - b) <= tol; }

// GarrisonVisible with its state forced (the option row 24 comes with the rows of epic 0455; with the row, the module asks
// Vp64Opt::Extra and the cases below that need "on" keep this wrapper)
struct ForcedOn : srvextras::UnitExtra
{
	std::unique_ptr<srvextras::UnitExtra> in;
	bool on = true;
	bool forwardConfigure = false;   // true: the inner extra's own configure (its row check) runs
	explicit ForcedOn(srvextras::GarrisonLook look) : in(srvextras::MakeGarrisonVisible(Vp64Opt::kCount, look)) {}
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
	bool beginFrame(const UnitExtrasFrame& f, std::string& why) override { return in->beginFrame(f, why); }
	bool unit(const UnitExtraUnit& u, UnitOverride& io, std::string& why) override { return in->unit(u, io, why); }
	bool wantsPlacement() const override { return in->wantsPlacement(); }   // issue 4983: look (d)
};

std::unique_ptr<UnitsExtras> GarrisonModule(srvextras::GarrisonLook look, ForcedOn** out = nullptr, bool ours = true)
{
	ForcedOn* x = new ForcedOn(look);
	if(out)
		*out = x;
	std::vector<std::unique_ptr<srvextras::UnitExtra>> v;
	v.emplace_back(x);
	std::unique_ptr<UnitsExtras> m = srvextras::MakeUnitsExtrasWith(std::move(v));
	m->setLog([](void*, const char* line) { Log(line); }, nullptr);
	m->configure(Vp64Opt::Options(), ours);
	return m;
}

SrvFeed::UnitRecord Domain(SrvFeed::UnitRecord r, int domain)
{
	r.domain = (int8_t)domain;
	return r;
}

// The city plot 23 = (3, 2) of a 10-wide grid: 3001 its garrison (land, hidden: stock's Visibility(false)); 3002 a ship
// that counts as a garrison (sea); 3003 a second land unit in the city, not garrisoned (hidden); 3004 a worker standing in
// the city (land, visible: stock draws it after a load, 4980); 3005 a garrisoned unit that is embarked (cannot happen in a
// city; refused anyway); 3006 a dead garrison. 2001 another team's garrison on plot 26 = (6, 2).
struct CityFixture
{
	FakePort port;
	UnitsCore core;
	c5r::heightfield::HeightGrid grid = Grid();
	CityFixture() : core(port, Log)
	{
		const uint8_t g = cu::kFlagGarrisoned;
		port.players = {Player(0, 0, 0), Player(1, 1, 0)};
		port.units = {Domain(Rec(0, 3001, 7, 23, g, 1), 2),
		              Domain(Rec(0, 3002, 7, 23, g, 2), 0),
		              Domain(Rec(0, 3003, 7, 23, 0, 3), 2),
		              Domain(Rec(0, 3004, 7, 23, cu::kFlagVisible, 4), 2),
		              Domain(Rec(0, 3005, 7, 23, g | cu::kFlagEmbarked, 5), 2),
		              Domain(Rec(0, 3006, 7, 23, g | cu::kFlagDead, 6), 2),
		              Domain(Rec(1, 2001, 7, 26, g, 7), 2)};
		core.setDatabase(TestLookups(), nullptr);
		std::vector<SrvFeed::Event> ev;
		uint32_t seq = 1;
		for(const SrvFeed::UnitRecord& u : port.units)
			ev.push_back(Ev(seq++, SrvFeed::kUnitCreated, u.owner, u.id, 7, 0, u.plot));
		core.onBatch(BatchOf(ev), 1000);
	}
};

const float kCx = 192.0f, kCy = 128.0f * 0.866f, kInner = 32.0f;   // plot (3, 2) of Grid()

UnitExtraUnit XU(const SrvFeed::UnitRecord& r, bool hiding = true)
{
	UnitExtraUnit u;
	u.rec = &r;
	u.plotX = 3;
	u.plotY = 2;
	u.hiding = hiding;
	u.listed = !hiding;
	u.facing = r.facing;
	return u;
}

// The choice, without a scene: which units and where, for both looks
bool CaseGarrisonChoice()
{
	using srvextras::GarrisonChoice;
	using srvextras::GarrisonLook;
	using srvextras::ChooseGarrison;
	const uint8_t g = cu::kFlagGarrisoned;
	const SrvFeed::UnitRecord land = Domain(Rec(0, 1, 7, 23, g), 2), sea = Domain(Rec(0, 2, 7, 23, g), 0),
	                          air = Domain(Rec(0, 3, 8, 23, g), 1), plain = Domain(Rec(0, 4, 7, 23, 0), 2),
	                          emb = Domain(Rec(0, 5, 7, 23, g | cu::kFlagEmbarked), 2),
	                          dead = Domain(Rec(0, 6, 7, 23, g | cu::kFlagDead), 2);
	// the edge look: shown, root at the centre - 0.6 inner in y, facing south (yaw 0)
	GarrisonChoice c = ChooseGarrison(XU(land), true, kCx, kCy, kInner, GarrisonLook::SouthEdge);
	EXPECT(c.kind == GarrisonChoice::Placed && c.show && c.root && c.heading);
	EXPECT(c.rootX == kCx && Near(c.rootY, kCy - 0.6f * kInner) && c.yaw == 0.0f);
	EXPECT(srvextras::kGarrisonEdgeShare == 0.6f && srvextras::kGarrisonSouthYaw == 0.0f);
	EXPECT(c.scale == 1.0f);
	// the centre look: shown only, the scene's own root and facing
	c = ChooseGarrison(XU(land), true, kCx, kCy, kInner, GarrisonLook::Centre);
	EXPECT(c.kind == GarrisonChoice::Placed && c.show && !c.root && !c.heading && c.scale == 1.0f);
	// the compact edge look: 0.85 inner south, facing south, scale 0.8
	c = ChooseGarrison(XU(land), true, kCx, kCy, kInner, GarrisonLook::SouthEdgeCompact);
	EXPECT(c.kind == GarrisonChoice::Placed && c.show && c.root && c.heading && c.yaw == 0.0f && c.scale == 0.8f);
	EXPECT(c.rootX == kCx && Near(c.rootY, kCy - 0.85f * kInner));
	// a garrison stock already lists: no show asked, still placed
	c = ChooseGarrison(XU(land, false), true, kCx, kCy, kInner, GarrisonLook::SouthEdge);
	EXPECT(c.kind == GarrisonChoice::Placed && !c.show && c.root);
	// negatives: not a military land garrison
	for(const SrvFeed::UnitRecord* r : {&sea, &air, &plain, &emb, &dead})
	{
		c = ChooseGarrison(XU(*r), true, kCx, kCy, kInner, GarrisonLook::SouthEdge);
		EXPECT(c.kind == GarrisonChoice::NotGarrison && !c.show && !c.root && !c.heading);
	}
	UnitExtraUnit none;
	EXPECT(ChooseGarrison(none, true, kCx, kCy, kInner, GarrisonLook::SouthEdge).kind == GarrisonChoice::NotGarrison);
	// held in a fight: untouched (lead-0042's contract)
	UnitExtraUnit h = XU(land);
	h.heldInFight = true;
	c = ChooseGarrison(h, true, kCx, kCy, kInner, GarrisonLook::SouthEdge);
	EXPECT(c.kind == GarrisonChoice::Held && !c.show && !c.root);
	// no plot centre: untouched
	c = ChooseGarrison(XU(land), false, kCx, kCy, kInner, GarrisonLook::SouthEdge);
	EXPECT(c.kind == GarrisonChoice::NoPlot && !c.show && !c.root);
	// walking (#7812): a scene root off the plot centre by more than 0.02 inner: shown, no root, no heading
	UnitExtraUnit w = XU(land);
	w.hasRoot = true;
	w.rootX = kCx + 0.7f;   // 0.0219 inner
	w.rootY = kCy;
	c = ChooseGarrison(w, true, kCx, kCy, kInner, GarrisonLook::SouthEdge);
	EXPECT(c.kind == GarrisonChoice::Walking && c.show && !c.root && !c.heading);
	// at rest at the EXE's position (within 0.02 inner of the centre): placed
	w.rootX = kCx + 0.6f;   // 0.01875 inner
	c = ChooseGarrison(w, true, kCx, kCy, kInner, GarrisonLook::SouthEdge);
	EXPECT(c.kind == GarrisonChoice::Placed && c.root && Near(c.rootY, kCy - 19.2f));
	// the land test: water south of the city (every sample row below world y kCy - 10 at height 0, the rest at 0.2 over a
	// water level of 0.1): south, south-east, south-west, east and west have water in their footprint (r = 16), north-east
	// (60 deg) is the first side on land, facing out: yaw 60 + 90 = 150 deg
	{
		const c5r::heightfield::HeightGrid gr0 = Grid();
		c5r::heightfield::HeightGrid wet = Grid();
		wet.waterZ = 0.1f;
		for(uint32_t r = 0; r < wet.samplesY; ++r)
			for(uint32_t x = 0; x < wet.samplesX; ++x)
				wet.h[wet.index(x, r)] = (float)r * wet.sy < kCy - 10.0f ? 0 : 255;
		EXPECT(srvextras::GarrisonFootOnLand(wet, kCx, kCy + 20, 8) && !srvextras::GarrisonFootOnLand(wet, kCx, kCy - 12, 1));
		c = ChooseGarrison(XU(land), true, kCx, kCy, kInner, GarrisonLook::SouthEdge, &wet);
		EXPECT(c.kind == GarrisonChoice::Placed && c.side == 5 && !c.noLandSide && c.root && c.heading);
		EXPECT(Near(c.rootX, kCx + 19.2f * 0.5f) && Near(c.rootY, kCy + 19.2f * 0.8660254f) && Near(c.yaw, 2.6179939f));
		// dry everywhere: south (side 0)
		c = ChooseGarrison(XU(land), true, kCx, kCy, kInner, GarrisonLook::SouthEdge, &gr0);
		EXPECT(c.side == 0 && c.rootX == kCx && Near(c.rootY, kCy - 19.2f) && c.yaw == 0.0f);
		// all water (a one-plot island seen as water around it): the centre look's placement, shown only
		for(uint16_t& h : wet.h)
			h = 0;
		c = ChooseGarrison(XU(land), true, kCx, kCy, kInner, GarrisonLook::SouthEdgeCompact, &wet);
		EXPECT(c.kind == GarrisonChoice::Placed && c.noLandSide && c.show && !c.root && !c.heading && c.scale == 1.0f &&
		       c.side == -1);
	}
	// the inner radius of the field
	float inner = 0;
	std::string why;
	const c5r::heightfield::HeightGrid gr = Grid();
	EXPECT(srvextras::GarrisonInnerRadius(gr, inner, why) && Near(inner, 32.0f));
	c5r::heightfield::HeightGrid bad = Grid();
	memset(bad.hexToWorld, 0, sizeof(bad.hexToWorld));
	EXPECT(!srvextras::GarrisonInnerRadius(bad, inner, why) && why.find("no plot centre") != std::string::npos);
	c5r::heightfield::HeightGrid one = Grid(1, 8);   // a field one plot wide: no second centre
	EXPECT(!srvextras::GarrisonInnerRadius(one, inner, why) && why.find("second plot centre") != std::string::npos);
	return true;
}

// Through the unit scene: off draws exactly as before (E2); on shows only the land garrison, placed by the look
bool CaseGarrisonScene()
{
	using srvextras::GarrisonLook;
	// off: no module
	CityFixture off;
	BuiltFrame fo;
	off.core.build(nullptr, &off.grid, 1100, fo);
	EXPECT(fo.listed == 1 && Find(fo, 0, 3004) && !Find(fo, 0, 3001));   // stock: only the worker
	// a module whose GarrisonVisible is off (its state): nothing on, the frame field-for-field the same
	{
		CityFixture f;
		std::vector<std::unique_ptr<srvextras::UnitExtra>> v;
		ForcedOn* x = new ForcedOn(GarrisonLook::SouthEdge);
		x->on = false;
		v.emplace_back(x);
		std::unique_ptr<UnitsExtras> m = srvextras::MakeUnitsExtrasWith(std::move(v));
		m->configure(Vp64Opt::Options(), true);
		EXPECT(!m->anyOn());
		f.core.setExtras(m.get());
		BuiltFrame b;
		f.core.build(nullptr, &f.grid, 1100, b);
		EXPECT(SameScene(fo.copies[1], b.copies[1]) && b.xforms.empty());
	}
	// on, the edge look
	CityFixture f;
	ForcedOn* x = nullptr;
	std::unique_ptr<UnitsExtras> m = GarrisonModule(GarrisonLook::SouthEdge, &x);
	f.core.setExtras(m.get());
	BuiltFrame b;
	f.core.build(nullptr, &f.grid, 1100, b);
	const cu::UnitInput* u = Find(b, 0, 3001);
	EXPECT(u && u->fade_from == 1 && u->fade_to == 0 && u->fade == 0);   // shown opaque
	EXPECT(u->has_root && Near(u->root_x, kCx) && Near(u->root_y, kCy - 19.2f) && u->plot_x == 3 && u->plot_y == 2);
	EXPECT(b.xforms.size() == 2 && b.xforms.count(UnitKeyOf(1, 2001)) && b.xforms[UnitKeyOf(0, 3001)].heading && b.xforms[UnitKeyOf(0, 3001)].yaw == 0.0f &&
	       b.xforms[UnitKeyOf(0, 3001)].scale == 1.0f);
	EXPECT(!Find(b, 0, 3002) && !Find(b, 0, 3003) && !Find(b, 0, 3005) && !Find(b, 0, 3006));   // ship, second unit, embarked, dead
	const cu::UnitInput* w = Find(b, 0, 3004);
	EXPECT(w && !w->has_root);   // the worker as stock draws it
	EXPECT(Find(b, 1, 2001));    // another team's garrison: shown here ...
	std::vector<uint8_t> states(80, 0);
	states[26] = 1;               // ... and removed by the fog filter where the active team does not see its plot now
	BuiltFrame b2 = b;
	const FogFilterResult r = FilterByFog(b2, f.core.players(), 10, states.data(), states.size(), 10, 8);
	EXPECT(std::string(r.state) == "on" && !Find(b2, 1, 2001) && Find(b2, 0, 3001));
	states[26] = 0;               // a visible plot: kept, as the game's unit flag of a foreign garrison shows there
	BuiltFrame b3 = b;
	FilterByFog(b3, f.core.players(), 10, states.data(), states.size(), 10, 8);
	EXPECT(Find(b3, 1, 2001));
	srvextras::GarrisonCounts gc;
	EXPECT(srvextras::GarrisonCountsOf(*x->in, gc) && gc.frames == 1 && gc.garrisons == 2 && gc.shown == 2 && gc.placed == 2 &&
	       gc.walking == 0 && gc.held == 0);
	srvextras::UnitExtraCounts c;
	bool running, failed;
	EXPECT(srvextras::UnitExtraCountsOf(*m, 0, c, running, failed) && running && !failed && c.shown == 2 && c.overrides == 2);
	// a garrison held in a fight: not shown, not moved (the seam's rule and the extra's)
	f.core.setHeldInFight(0, 3001, true);
	f.core.build(nullptr, &f.grid, 1200, b);
	EXPECT(!Find(b, 0, 3001));
	EXPECT(srvextras::GarrisonCountsOf(*x->in, gc) && gc.held == 1);
	f.core.setHeldInFight(0, 3001, false);
	// no height field this frame: nothing (the scene draws no unit then either), not a failure
	f.core.build(nullptr, nullptr, 1300, b);
	EXPECT(m->anyOn());
	// the centre look: shown at the scene's own root, no turn
	CityFixture fc;
	std::unique_ptr<UnitsExtras> mc = GarrisonModule(GarrisonLook::Centre);
	fc.core.setExtras(mc.get());
	BuiltFrame bc;
	fc.core.build(nullptr, &fc.grid, 1100, bc);
	const cu::UnitInput* uc = Find(bc, 0, 3001);
	EXPECT(uc && !uc->has_root && uc->facing == 2 && bc.xforms.empty() && uc->fade_from == 1);
	// the compact look: its scale reaches the build's xforms
	CityFixture fk;
	std::unique_ptr<UnitsExtras> mk = GarrisonModule(GarrisonLook::SouthEdgeCompact);
	fk.core.setExtras(mk.get());
	BuiltFrame bk;
	fk.core.build(nullptr, &fk.grid, 1100, bk);
	const cu::UnitInput* uk = Find(bk, 0, 3001);
	EXPECT(uk && uk->has_root && Near(uk->root_y, kCy - 27.2f) && bk.xforms[UnitKeyOf(0, 3001)].scale == 0.8f &&
	       bk.xforms[UnitKeyOf(0, 3001)].heading);
	return true;
}

// E5 and E6 for GarrisonVisible: ServerUnits = 0 one line and nothing drawn; a field with no plot centres: one LOUD line,
// off for the session, the scene stock
bool CaseGarrisonOffAndFail()
{
	g_log.clear();
	std::unique_ptr<UnitsExtras> m5 = GarrisonModule(srvextras::GarrisonLook::SouthEdge, nullptr, false);
	EXPECT(!m5->anyOn() && LogCount("units extra GarrisonVisible: off: ServerUnits = 0") == 1 && g_log.size() == 1);
	g_log.clear();
	CityFixture f;
	f.grid = Grid();
	memset(f.grid.hexToWorld, 0, sizeof(f.grid.hexToWorld));
	std::unique_ptr<UnitsExtras> m = GarrisonModule(srvextras::GarrisonLook::SouthEdge);
	EXPECT(m->anyOn() && LogCount("units extra GarrisonVisible: in effect") == 1);
	f.core.setExtras(m.get());
	BuiltFrame b;
	f.core.build(nullptr, &f.grid, 1100, b);
	f.core.build(nullptr, &f.grid, 1200, b);
	EXPECT(LogCount("units extra GarrisonVisible: LOUD: failed: beginFrame: the height field gives no plot centre") == 1);
	EXPECT(LogCount("off for the session") == 1 && !m->anyOn());
	EXPECT(!Find(b, 0, 3001) && b.xforms.empty());   // stock: the garrison hidden
	// made with a row that is not an extra: off by its state, one line (the row's own reason)
	g_log.clear();
	std::vector<std::unique_ptr<srvextras::UnitExtra>> v;
	v.push_back(srvextras::MakeGarrisonVisible(Vp64Opt::kServerUnits));
	std::unique_ptr<UnitsExtras> mw = srvextras::MakeUnitsExtrasWith(std::move(v));
	mw->setLog([](void*, const char* line) { Log(line); }, nullptr);
	mw->configure(Vp64Opt::Options(), true);
	EXPECT(!mw->anyOn() && g_log.size() == 1 && LogCount("units ServerUnits: not an extra") == 1);
	// in effect by its state but made with a row that is not its own: refused at configure, one LOUD line
	g_log.clear();
	ForcedOn* fwd = new ForcedOn(srvextras::GarrisonLook::SouthEdge);   // its inner extra made with kCount
	fwd->forwardConfigure = true;
	std::vector<std::unique_ptr<srvextras::UnitExtra>> vr;
	vr.emplace_back(fwd);
	std::unique_ptr<UnitsExtras> mr = srvextras::MakeUnitsExtrasWith(std::move(vr));
	mr->setLog([](void*, const char* line) { Log(line); }, nullptr);
	mr->configure(Vp64Opt::Options(), true);
	EXPECT(!mr->anyOn() && g_log.size() == 1 &&
	       LogCount("units extra GarrisonVisible: LOUD: failed: configure: its option row is not GarrisonVisible") == 1);
	return true;
}

// E8, an indication only (PERFORMANCE.md: not a product figure, no lock held): the CPU time of UnitsCore::build for a
// table of 2000 units, 200 of them land garrisons, without a module and with GarrisonVisible on; arms alternated, 201 builds
// each, the median printed. Run alone: test_units_extras.exe garrison_cost
bool CaseGarrisonCost()
{
	struct Big
	{
		FakePort port;
		UnitsCore core;
		c5r::heightfield::HeightGrid grid = Grid();
		Big() : core(port, Log)
		{
			port.players = {Player(0, 0, 0)};
			std::vector<SrvFeed::Event> ev;
			for(int i = 0; i < 2000; ++i)
			{
				const bool gar = i % 10 == 0;
				port.units.push_back(Domain(Rec(0, 5000 + i, 7, i % 80, gar ? cu::kFlagGarrisoned : cu::kFlagVisible, (uint32_t)i + 1), 2));
				ev.push_back(Ev((uint32_t)i + 1, SrvFeed::kUnitCreated, 0, 5000 + i, 7, 0, i % 80));
			}
			core.setDatabase(TestLookups(), nullptr);
			core.onBatch(BatchOf(ev), 1000);
		}
	};
	Big off, on;
	std::unique_ptr<UnitsExtras> m = GarrisonModule(srvextras::GarrisonLook::SouthEdge);
	on.core.setExtras(m.get());
	LARGE_INTEGER fq, t0, t1;
	QueryPerformanceFrequency(&fq);
	std::vector<double> a, b;
	BuiltFrame f;
	for(int r = 0; r < 201; ++r)
	{
		QueryPerformanceCounter(&t0);
		off.core.build(nullptr, &off.grid, 1100 + r, f);
		QueryPerformanceCounter(&t1);
		a.push_back(1e6 * (double)(t1.QuadPart - t0.QuadPart) / (double)fq.QuadPart);
		EXPECT(f.listed == 1800);
		QueryPerformanceCounter(&t0);
		on.core.build(nullptr, &on.grid, 1100 + r, f);
		QueryPerformanceCounter(&t1);
		b.push_back(1e6 * (double)(t1.QuadPart - t0.QuadPart) / (double)fq.QuadPart);
		EXPECT(f.listed == 2000 && f.xforms.size() == 200);
	}
	std::sort(a.begin(), a.end());
	std::sort(b.begin(), b.end());
	printf("garrison_cost: UnitsCore::build of 2000 units (200 land garrisons): no module median %.1f us (p10 %.1f, p90 %.1f), "
	       "GarrisonVisible on median %.1f us (p10 %.1f, p90 %.1f); 201 builds per arm, alternated (an indication, no lock)\n",
	       a[100], a[20], a[180], b[100], b[20], b[180]);
	return true;
}

// The registered features with their option rows (the 0455 landing: ids 24, 25, 26 in id order): each off by its category
// switch and by its own flag, on with both, E5 with ServerUnits = 0; one line per registered feature per configure (the
// module's lines through the real state)
bool CaseGarrisonRegistered()
{
	auto opts = [](const char* text) {
		Vp64Opt::Options o;
		Vp64Opt::Parse(text, strlen(text), o, [](void*, const char*) {}, nullptr);
		return o;
	};
	EXPECT(Vp64Opt::kFixUnitPlacement == 23 &&
	       strcmp(Vp64Opt::Definition(Vp64Opt::kFixUnitPlacement).key, "FixUnitPlacement") == 0);   // issue 4982
	EXPECT(Vp64Opt::kGarrisonVisible == 24 && strcmp(Vp64Opt::Definition(Vp64Opt::kGarrisonVisible).key, "GarrisonVisible") == 0);
	EXPECT(Vp64Opt::kShipsMoored == 25 && strcmp(Vp64Opt::Definition(Vp64Opt::kShipsMoored).key, "ShipsMoored") == 0);
	EXPECT(Vp64Opt::kFixAirBases == 26 && strcmp(Vp64Opt::Definition(Vp64Opt::kFixAirBases).key, "FixAirBases") == 0);
	EXPECT(Vp64Opt::kAirInterceptPatrol == 27 &&
	       strcmp(Vp64Opt::Definition(Vp64Opt::kAirInterceptPatrol).key, "AirInterceptPatrol") == 0);   // issue 4986
	EXPECT(Vp64Opt::kPerimeterGarrisons == 32 &&
	       strcmp(Vp64Opt::Definition(Vp64Opt::kPerimeterGarrisons).key, "PerimeterGarrisons") == 0);   // issue 4983 (D243)
	// the list: six makers, in id order, each made with its row
	{
		const std::vector<srvextras::UnitExtraMaker>& mk = srvextras::UnitExtraMakers();
		const Vp64Opt::Id ids[] = {Vp64Opt::kFixUnitPlacement, Vp64Opt::kGarrisonVisible, Vp64Opt::kShipsMoored, Vp64Opt::kFixAirBases,
		                           Vp64Opt::kAirInterceptPatrol, Vp64Opt::kPerimeterGarrisons};
		EXPECT(mk.size() == 6);
		for(size_t i = 0; i < mk.size(); ++i)
		{
			std::unique_ptr<srvextras::UnitExtra> x = mk[i]();
			EXPECT(x && x->id() == ids[i] && strcmp(x->name(), Vp64Opt::Definition(ids[i]).key) == 0);
		}
	}
	const size_t kRegistered = 6;
	struct C
	{
		const char* text;
		bool ours;
		bool on;
		const char* line;
	};
	const C cs[] = {
	    // on: the module's anyOn; FixAirBases defaults to 1 since D235 (2) (2026-11-22), so the module is on with ours
	    // unless the text switches it off
	    {"", true, true, "units GarrisonVisible: off: EnableVisualAdditions = 0 (default) switches off every visual addition"},
	    {"[Extras]\nGarrisonVisible = 1\n", true, true, "units GarrisonVisible: off: EnableVisualAdditions = 0 (default)"},
	    {"[Extras]\nEnableVisualAdditions = 1\nFixAirBases = 0\n", true, false, "units GarrisonVisible: off: GarrisonVisible = 0 (default)"},
	    {"[Extras]\nEnableVisualAdditions = 1\nGarrisonVisible = 1\n", true, true, "units extra GarrisonVisible: in effect"},
	    {"[Extras]\nEnableVisualAdditions = 1\nGarrisonVisible = 1\n", false, false, "units extra GarrisonVisible: off: ServerUnits = 0"},
	    {"[Extras]\nFixAirBases = 0\n", true, false, "units ShipsMoored: off: EnableVisualAdditions = 0 (default) switches off every visual addition"},
	    {"[Extras]\nShipsMoored = 1\n", true, true, "units ShipsMoored: off: EnableVisualAdditions = 0 (default)"},
	    {"[Extras]\nEnableVisualAdditions = 1\n", true, true, "units ShipsMoored: off: ShipsMoored = 0 (default)"},
	    {"[Extras]\nEnableVisualAdditions = 1\nShipsMoored = 1\n", true, true, "units extra ShipsMoored: in effect"},
	    {"[Extras]\nEnableVisualAdditions = 1\nShipsMoored = 1\n", false, false, "units extra ShipsMoored: off: ServerUnits = 0"},
	    {"", true, true, "units extra FixAirBases: in effect"},
	    {"[Extras]\nFixAirBases = 0\n", true, false, "units FixAirBases: off: FixAirBases = 0 (from the file)"},
	    {"[Extras]\nFixAirBases = 1\nEnableBugfixes = 0\n", true, false, "units FixAirBases: off: EnableBugfixes = 0 (from the file)"},
	    {"[Extras]\nFixAirBases = 1\n", true, true, "units extra FixAirBases: in effect"},
	    {"[Extras]\nFixAirBases = 1\n", false, false, "units extra FixAirBases: off: ServerUnits = 0"},
	    // issue 4986: AirInterceptPatrol (27), a visual addition, default 0
	    {"[Extras]\nFixAirBases = 0\n", true, false, "units AirInterceptPatrol: off: EnableVisualAdditions = 0 (default) switches off every visual addition"},
	    {"[Extras]\nEnableVisualAdditions = 1\nFixAirBases = 0\n", true, false, "units AirInterceptPatrol: off: AirInterceptPatrol = 0 (default)"},
	    {"[Extras]\nEnableVisualAdditions = 1\nAirInterceptPatrol = 1\nFixAirBases = 0\n", true, true, "units extra AirInterceptPatrol: in effect"},
	    {"[Extras]\nEnableVisualAdditions = 1\nAirInterceptPatrol = 1\n", false, false, "units extra AirInterceptPatrol: off: ServerUnits = 0"},
	    // issue 4982: FixUnitPlacement (23), a bugfix, default 0
	    {"[Extras]\nFixAirBases = 0\n", true, false, "units FixUnitPlacement: off: FixUnitPlacement = 0 (default)"},
	    {"[Extras]\nFixUnitPlacement = 1\nFixAirBases = 0\n", true, true, "units extra FixUnitPlacement: in effect"},
	    {"[Extras]\nFixUnitPlacement = 1\nEnableBugfixes = 0\n", true, false, "units FixUnitPlacement: off: EnableBugfixes = 0 (from the file)"},
	    {"[Extras]\nFixUnitPlacement = 1\n", false, false, "units extra FixUnitPlacement: off: ServerUnits = 0"},
	    // issue 4983 (D243): PerimeterGarrisons (32), a visual addition, default 0
	    {"[Extras]\nFixAirBases = 0\n", true, false, "units PerimeterGarrisons: off: EnableVisualAdditions = 0 (default) switches off every visual addition"},
	    {"[Extras]\nEnableVisualAdditions = 1\nFixAirBases = 0\n", true, false, "units PerimeterGarrisons: off: PerimeterGarrisons = 0 (default)"},
	    {"[Extras]\nPerimeterGarrisons = 1\nFixAirBases = 0\n", true, false, "units PerimeterGarrisons: off: EnableVisualAdditions = 0 (default)"},
	    {"[Extras]\nEnableVisualAdditions = 1\nPerimeterGarrisons = 1\nFixAirBases = 0\n", true, true, "units extra PerimeterGarrisons: in effect"},
	    {"[Extras]\nEnableVisualAdditions = 1\nPerimeterGarrisons = 1\n", false, false, "units extra PerimeterGarrisons: off: ServerUnits = 0"},
	    // both garrison rows on: both in effect by their state (GarrisonVisible steps aside at its first frame, a note line)
	    {"[Extras]\nEnableVisualAdditions = 1\nPerimeterGarrisons = 1\nGarrisonVisible = 1\n", true, true, "units extra GarrisonVisible: in effect"},
	};
	for(const C& c : cs)
	{
		g_log.clear();
		std::unique_ptr<UnitsExtras> m = srvextras::MakeUnitsExtrasImpl();
		m->setLog([](void*, const char* line) { Log(line); }, nullptr);
		m->configure(opts(c.text), c.ours);
		if(m->anyOn() != c.on || g_log.size() != kRegistered || LogCount(c.line) != 1)
		{
			g_fail = std::string("case '") + c.text + "' (" + c.line + "): " + (g_log.empty() ? "no line" : g_log[0]);
			return false;
		}
	}
	// on: the registered extra places the garrison as the edge look (the default)
	CityFixture f;
	std::unique_ptr<UnitsExtras> m = srvextras::MakeUnitsExtrasImpl();
	m->configure(opts("[Extras]\nEnableVisualAdditions = 1\nGarrisonVisible = 1\n"), true);
	f.core.setExtras(m.get());
	BuiltFrame b;
	f.core.build(nullptr, &f.grid, 1100, b);
	const cu::UnitInput* u = Find(b, 0, 3001);
	EXPECT(u && u->has_root && Near(u->root_y, kCy - 19.2f) && b.xforms.size() == 2);   // 3001 and the foreign 2001
	return true;
}

// A placement source that is never asked (the module's decorator is only looked at, not run)
struct NoPlacement : cu::PlacementSource
{
	cu::UnitStatus place(const cu::PlacementRequest&, cu::UnitPlacement& out) override
	{
		out = cu::UnitPlacement();
		return cu::UnitStatus::fail(cu::UnitCode::PlacementFailed, "not used");
	}
};

// Issue 4983 (D243): PerimeterGarrisons, look (d) as the players' option (id 32). Key off: the registered module draws the
// frame as the five extras before this ticket did (rule A's arm), with no placement decorator. Key on: the garrisons
// shown at the plot centre (no root, no heading) and named for the decorator with their land edges; the same frame as
// GarrisonVisible forced to look (d). Both garrison keys on: look (d) only, GarrisonVisible notes once that it steps aside.
bool CasePerimeterGarrisons()
{
	auto opts = [](const char* text) {
		Vp64Opt::Options o;
		Vp64Opt::Parse(text, strlen(text), o, [](void*, const char*) {}, nullptr);
		return o;
	};
	auto registered = [&](const char* text) {
		std::unique_ptr<UnitsExtras> m = srvextras::MakeUnitsExtrasImpl();
		m->setLog([](void*, const char* line) { Log(line); }, nullptr);
		m->configure(opts(text), true);
		return m;
	};
	// the five makers before this ticket (ids 23-27), as UnitExtraMakers() listed them
	auto before = [&](const char* text) {
		std::vector<std::unique_ptr<srvextras::UnitExtra>> v;
		const std::vector<srvextras::UnitExtraMaker>& mk = srvextras::UnitExtraMakers();
		for(size_t i = 0; i < 5 && i < mk.size(); ++i)
			v.push_back(mk[i]());
		std::unique_ptr<UnitsExtras> m = srvextras::MakeUnitsExtrasWith(std::move(v));
		m->setLog([](void*, const char* line) { Log(line); }, nullptr);
		m->configure(opts(text), true);
		return m;
	};
	// key off (its default, and set to 0), with the visual switch on and off, GarrisonVisible on and off: the frame is the
	// frame of the module before this ticket, field for field, and no decorator is asked for
	const char* offTexts[] = {"", "[Extras]\nEnableVisualAdditions = 1\n", "[Extras]\nEnableVisualAdditions = 1\nPerimeterGarrisons = 0\n",
	                          "[Extras]\nEnableVisualAdditions = 1\nGarrisonVisible = 1\n", "[Extras]\nPerimeterGarrisons = 1\n"};
	for(const char* t : offTexts)
	{
		CityFixture a, b;
		std::unique_ptr<UnitsExtras> ma = registered(t), mb = before(t);
		if(ma->anyOn())
			a.core.setExtras(ma.get());
		if(mb->anyOn())
			b.core.setExtras(mb.get());
		BuiltFrame fa, fb;
		a.core.build(nullptr, &a.grid, 1100, fa);
		b.core.build(nullptr, &b.grid, 1100, fb);
		if(!SameScene(fa.copies[1], fb.copies[1]) || fa.xforms.size() != fb.xforms.size() || fa.listed != fb.listed ||
		   ma->wantsPlacement() || ma->inEffect() != mb->inEffect())
		{
			g_fail = std::string("key off, '") + t + "': the frame differs from the module before 4983";
			return false;
		}
	}
	// key on: look (d)
	g_log.clear();
	CityFixture f;
	std::unique_ptr<UnitsExtras> m = registered("[Extras]\nEnableVisualAdditions = 1\nPerimeterGarrisons = 1\n");
	EXPECT(LogCount("units extra PerimeterGarrisons: in effect") == 1 && LogCount("LOUD") == 0);
	EXPECT(m->anyOn() && m->wantsPlacement());
	f.core.setExtras(m.get());
	BuiltFrame b;
	f.core.build(nullptr, &f.grid, 1100, b);
	const cu::UnitInput* u = Find(b, 0, 3001);
	EXPECT(u && u->fade_from == 1 && u->fade_to == 0 && !u->has_root);   // shown, at the plot centre
	EXPECT(Find(b, 1, 2001) && !Find(b, 0, 3002) && !Find(b, 0, 3003) && !Find(b, 0, 3005) && !Find(b, 0, 3006));
	srvextras::UnitExtraCounts c;
	bool running = false, failed = false;
	EXPECT(srvextras::UnitExtraCountsOf(*m, 5, c, running, failed) && running && !failed && c.shown == 2);
	NoPlacement inner;
	c5r::units::PlacementSource* deco = m->placementOver(&inner);
	EXPECT(deco && deco != &inner);
	EXPECT(static_cast<c5r::extras::units::GarrisonPlacement*>(deco)->pending() == 2);   // 3001 and the foreign 2001
	char line[2048];
	m->countsText(line, sizeof(line));
	EXPECT(strstr(line, "extras PerimeterGarrisons on") != nullptr && strstr(line, " | perimeter spread=") != nullptr);
	// the same frame as GarrisonVisible forced to look (d) (stage d's code path)
	{
		CityFixture g;
		std::unique_ptr<UnitsExtras> md = GarrisonModule(srvextras::GarrisonLook::Perimeter);
		g.core.setExtras(md.get());
		BuiltFrame bd;
		g.core.build(nullptr, &g.grid, 1100, bd);
		EXPECT(SameScene(b.copies[1], bd.copies[1]) && b.xforms.size() == bd.xforms.size() && md->wantsPlacement());
		EXPECT(static_cast<c5r::extras::units::GarrisonPlacement*>(md->placementOver(&inner))->pending() == 2);
	}
	// both garrison keys on: GarrisonVisible steps aside (no root of look (a)), PerimeterGarrisons places; one note line
	g_log.clear();
	CityFixture h;
	std::unique_ptr<UnitsExtras> mb =
	    registered("[Extras]\nEnableVisualAdditions = 1\nPerimeterGarrisons = 1\nGarrisonVisible = 1\n");
	EXPECT(LogCount("units extra GarrisonVisible: in effect") == 1 && LogCount("units extra PerimeterGarrisons: in effect") == 1);
	h.core.setExtras(mb.get());
	BuiltFrame bb;
	h.core.build(nullptr, &h.grid, 1100, bb);
	h.core.build(nullptr, &h.grid, 1200, bb);
	EXPECT(LogCount("units extra GarrisonVisible: PerimeterGarrisons = 1 is in effect: this test look draws nothing") == 1);
	EXPECT(LogCount("LOUD") == 0);
	const cu::UnitInput* ub = Find(bb, 0, 3001);
	EXPECT(ub && !ub->has_root && ub->fade_from == 1);
	EXPECT(SameScene(b.copies[1], bb.copies[1]) && b.xforms.size() == bb.xforms.size());
	EXPECT(srvextras::UnitExtraCountsOf(*mb, 1, c, running, failed) && running && !failed && c.shown == 0 && c.overrides == 0);
	EXPECT(static_cast<c5r::extras::units::GarrisonPlacement*>(mb->placementOver(&inner))->pending() == 2);
	// GarrisonVisible alone: look (a) as before (its root south of the centre), no decorator
	CityFixture k;
	std::unique_ptr<UnitsExtras> mk = registered("[Extras]\nEnableVisualAdditions = 1\nGarrisonVisible = 1\nFixAirBases = 0\n");
	EXPECT(!mk->wantsPlacement());
	k.core.setExtras(mk.get());
	BuiltFrame bk;
	k.core.build(nullptr, &k.grid, 1100, bk);
	const cu::UnitInput* uk = Find(bk, 0, 3001);
	EXPECT(uk && uk->has_root && Near(uk->root_y, kCy - 19.2f));
	return true;
}

struct Case
{
	const char* name;
	bool (*fn)();
};
const Case kCases[] = {
    {"null_factory", CaseNullFactory},   {"override_root", CaseOverrideRoot}, {"show", CaseShow},
    {"scale_heading", CaseScaleHeading}, {"server_units_off", CaseServerUnitsOff}, {"failing_extra", CaseFailingExtra},
    {"held_in_fight", CaseHeldInFight},  {"host_lines", CaseHostLines},
    {"garrison_choice", CaseGarrisonChoice}, {"garrison_scene", CaseGarrisonScene},
    {"garrison_off_and_fail", CaseGarrisonOffAndFail}, {"garrison_cost", CaseGarrisonCost},
    {"garrison_registered", CaseGarrisonRegistered}, {"perimeter_garrisons", CasePerimeterGarrisons},
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
		printf("test_units_extras: no case matches\n");
		return 2;
	}
	printf("test_units_extras: %d of %d cases passed\n", passed, run);
	return passed == run ? 0 : 1;
}
