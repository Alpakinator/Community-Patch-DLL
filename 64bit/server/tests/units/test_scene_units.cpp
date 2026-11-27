// test_scene_units.cpp - tests of the server's unit scene (epic 0375, issue 4454; srv_units_scene.h, srv_units_core.h).
// Written 2026-11-15 for this repository (GPL-3.0-or-later). Build: build_units_tests.py; run: run_units_tests.py.
//
//   test_scene_units.exe [case ...]          every case (or the named ones); "CASE <name> PASS|FAIL <why>" per case
//   test_scene_units.exe oracle --scenes DIR --db FILE --heightfield FILE --out DIR
//        DoD B: per 0041 oracle scene (.c5units) a synthetic feed (players, unit records, creations in seed order) through
//        the scene's own code; the scene's UnitScene compared field by field with the oracle's, written to OUT/scene/ and,
//        with the oracle's figure lines added (forced poses, note 12.2), to OUT/merged/ for `unitscene prepare`.
// Cases that need the module's install-backed fixture (C5R_GAME_DIR, C5R_EXE) and a WARP device say so; without them the
// program exits 99 (never a skip: a skipped case counts as failed).
// Exit: 0 every case passed, 1 a case failed, 2 usage / no case matched, 99 the environment is missing.
#include <windows.h>
#include <d3d11.h>
#include <psapi.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>   // 5066: the walkopen_* cases' scripts; 0685
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "srv_render_common.h"
#include "srv_render_host.h"
#include "srv_render_scene.h"
#include "units/srv_units_core.h"
#include "units/srv_units_anim.h"   // issue 4456
#include "effects/srv_trigger_stream.h"   // ticket 5062 (W107): fight_city_flight counts the routes' flights
#include "effects/srv_effect_routes.h"
#include "units/srv_units_mode.h"   // issue 4755: the mode word writer's source (positions_switch)
#include "srv_scene_sources.h"       // issue 4456: FeedSource (the recording port)
#include "feed/srv_feed_server.h"    // issue 4463: ResetForTest, GetCounters2 (the live feed)
#include "c5r/unitanim/timeline.h"
#include "c5r/units/scene_file.h"

#include "srv_options.h"

// server.exe's SrvOptions() (server/src/srv_options.cpp) for the render units: every option at its default
static Vp64Opt::Options g_options;
const Vp64Opt::Options& SrvOptions() { return g_options; }

using namespace srvunits;
namespace cu = c5r::units;

namespace {

// ------------------------------------------------------------------------------------------------------------ helpers
std::vector<std::string> g_log;
std::string g_fail;
ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
std::string g_game, g_exe;

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
	uint32_t gen = 0;
	std::wstring db;
	uint32_t dbGen = 0;
	bool haveDb = false;
	int copies = 0, playerCopies = 0, dbAsks = 0;
	void copyUnits(std::vector<SrvFeed::UnitRecord>& out, uint32_t& g) override
	{
		copies++;
		out = units;
		g = gen;
	}
	void copyPlayers(std::vector<SrvFeed::PlayerRecord>& out) override
	{
		playerCopies++;
		out = players;
	}
	bool unitDatabase(std::wstring& p, uint32_t& g) override
	{
		dbAsks++;
		p = db;
		g = dbGen;
		return haveDb;
	}
};

SrvFeed::UnitRecord Rec(int owner, int id, int type, int plot, int facing = 2, uint8_t flags = cu::kFlagVisible, int damage = 0,
                        int maxHP = 100, uint32_t seq = 0)
{
	SrvFeed::UnitRecord r;
	memset(&r, 0, sizeof(r));
	r.owner = (int8_t)owner;
	r.id = id;
	r.unitType = type;
	r.plot = plot;
	r.facing = (int8_t)facing;
	r.flags = flags;
	r.damage = (int16_t)damage;
	r.maxHP = (int16_t)maxHP;
	r.seq = seq;
	return r;
}

SrvFeed::PlayerRecord Player(int p, int colour, int civ, int era, int artStyle = 5, uint8_t flags = 0)
{
	SrvFeed::PlayerRecord r;
	memset(&r, 0, sizeof(r));
	r.player = (int8_t)p;
	r.team = (int8_t)p;
	r.alive = 1;
	r.flags = flags;
	r.colour = colour;
	r.civilization = civ;
	r.era = era;
	r.artStyle = artStyle;
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

int FloatBits(float f)
{
	int i;
	memcpy(&i, &f, 4);
	return i;
}

SrvFeed::Batch BatchOf(std::vector<SrvFeed::Event> ev, uint32_t w = 10, uint32_t h = 8)
{
	SrvFeed::Batch b;
	b.events = std::move(ev);
	b.width = w;
	b.height = h;
	return b;
}

Lookups TestLookups()
{
	Lookups l;
	l.units = {{7, "UNIT_WARRIOR"}, {8, "UNIT_TRIREME"}};
	l.colours = {{3, "PLAYERCOLOR_POLYNESIA"}};
	l.civs = {{5, "CIVILIZATION_POLYNESIA"}};
	l.eras = {{0, "ERA_ANCIENT"}};
	l.artStyles = {{5, "ARTSTYLE_POLYNESIAN"}};
	l.minorCivs = {{2, "MINOR_CIV_MSOURA"}};   // issue 4460 (A1)
	// issue 4469: the embarked art (VP's values: LAST_EMBARK_ART_ERA 4, TECH_ASTRONOMY in the Renaissance era 3,
	// Polynesia's override)
	l.lastEmbarkArtEra = 4;
	l.allWaterEra = 3;
	l.embarkOverride = {{5, "ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE"}};
	return l;
}

// A flat synthetic height field: mapW x (mapH + 4) plots, a hex lattice of 64 x 64 units (y scaled by sy), height 0.
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

bool Near(float a, float b, float tol = 1e-6f) { return std::fabs(a - b) <= tol; }

// ------------------------------------------------------------------------------------------------------- core cases
bool CaseMapping()
{
	FakePort port;
	port.units = {Rec(0, 1002, 8, 5, 1, cu::kFlagVisible, 0, 100, 2),
	              Rec(0, 1001, 7, 23, 4, cu::kFlagVisible | cu::kFlagFortified, 20, 100, 1)};
	port.players = {Player(0, 3, 5, 0)};
	UnitsCore core(port, Log);
	core.setDatabase(TestLookups(), nullptr);
	core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 23), Ev(2, SrvFeed::kUnitCreated, 0, 1002, 8, 0, 5)}), 1000);
	EXPECT(port.copies == 1 && port.playerCopies == 1);
	BuiltFrame f;
	core.build(nullptr, nullptr, 1000, f);
	const cu::UnitScene& s = f.copies[1];
	EXPECT(f.copies[0].units.empty() && f.copies[2].units.empty());
	EXPECT(s.format == 1 && s.position_source == "own" && s.water_level == 0.0f);
	EXPECT(s.players.size() == 1);
	EXPECT(s.players[0].player == 0 && s.players[0].colour == "PLAYERCOLOR_POLYNESIA" && s.players[0].art_style == "CIVILIZATION_POLYNESIA" &&
	       s.players[0].era == "ERA_ANCIENT");
	EXPECT(s.units.size() == 2);
	const cu::UnitInput& a = s.units[0];   // table order by (owner, id), whatever the feed's order
	EXPECT(a.player == 0 && a.id == 1001 && a.unit_type == "UNIT_WARRIOR" && a.art_row.empty());
	EXPECT(a.plot_x == 3 && a.plot_y == 2 && !a.has_root && a.facing == 4 && a.formation_seed == 0);
	EXPECT(a.damage == 20 && a.max_hp == 100 && a.alive == -1 && a.flags == (cu::kFlagVisible | cu::kFlagFortified));
	EXPECT(a.fade_from == 1 && a.fade_to == 0 && a.fade == 0 && a.figures.empty());
	const cu::UnitInput& b = s.units[1];
	EXPECT(b.id == 1002 && b.unit_type == "UNIT_TRIREME" && b.plot_x == 5 && b.plot_y == 0 && b.facing == 1 && b.formation_seed == 1);
	// a removal frees index 0, the next creation takes it (the allocator's rule, note 4)
	port.units = {Rec(0, 1002, 8, 5, 1, cu::kFlagVisible, 0, 100, 2), Rec(0, 1003, 7, 6, 0, cu::kFlagVisible, 0, 100, 4)};
	core.onBatch(BatchOf({Ev(3, SrvFeed::kUnitDestroyed, 0, 1001), Ev(4, SrvFeed::kUnitCreated, 0, 1003, 7, 0, 6)}), 1100);
	core.build(nullptr, nullptr, 1100, f);
	EXPECT(f.copies[1].units.size() == 2 && f.copies[1].units[1].id == 1003 && f.copies[1].units[1].formation_seed == 0);
	// the grid's water level and a batch without unit events (no table copy)
	const int before = port.copies;
	core.onBatch(BatchOf({Ev(5, SrvFeed::kPlotState, 0, 0)}), 1200);
	EXPECT(port.copies == before);
	EXPECT(g_log.empty());
	return true;
}

bool CaseFades()
{
	float from, to, p;
	bool run;
	Fade none;
	EXPECT(!fadeState(none, false, 0, from, to, p, run));
	EXPECT(fadeState(none, true, 0, from, to, p, run) && from == 1 && to == 0 && p == 0 && !run);
	Fade in;
	in.have = true;
	in.visible = true;
	in.t0Ms = 1000;
	in.blendMs = 500;
	EXPECT(fadeState(in, true, 1250, from, to, p, run) && from == 0 && to == 1 && Near(p, 0.5f) && run);
	EXPECT(fadeState(in, true, 1600, from, to, p, run) && from == 0 && to == 1 && p == 1 && !run);
	EXPECT(fadeState(in, true, 900, from, to, p, run) && p == 0 && run);
	Fade out = in;
	out.visible = false;
	out.t0Ms = 2000;
	out.blendMs = 200;
	EXPECT(fadeState(out, false, 2100, from, to, p, run) && from == 1 && to == 0 && Near(p, 0.5f) && run);
	EXPECT(!fadeState(out, false, 2300, from, to, p, run));
	Fade now = in;
	now.blendMs = 0;
	EXPECT(fadeState(now, true, 1000, from, to, p, run) && from == 1 && to == 0 && p == 0 && !run);
	now.visible = false;
	EXPECT(!fadeState(now, true, 1000, from, to, p, run));
	// through the core: kUnitVisibility (visible, blend 0.5 s as float bits) at t 1000
	FakePort port;
	port.units = {Rec(0, 1001, 7, 23, 4, cu::kFlagVisible, 0, 100, 2)};
	port.players = {Player(0, 3, 5, 0)};
	UnitsCore core(port, Log);
	core.setDatabase(TestLookups(), nullptr);
	core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7), Ev(2, SrvFeed::kUnitVisibility, 0, 1001, 1, FloatBits(0.5f))}), 1000);
	BuiltFrame f;
	core.build(nullptr, nullptr, 1125, f);
	EXPECT(f.copies[1].units.size() == 1 && f.fading);
	const cu::UnitInput& u = f.copies[1].units[0];
	EXPECT(u.fade_from == 0 && u.fade_to == 1 && Near(u.fade, 0.25f));
	// hidden with blend 0.25 s at 2000: listed at 2100 (1, 0, 0.4), gone at 2300
	port.units[0].flags = 0;
	port.units[0].seq = 3;
	core.onBatch(BatchOf({Ev(3, SrvFeed::kUnitVisibility, 0, 1001, 0, FloatBits(0.25f))}), 2000);
	core.build(nullptr, nullptr, 2100, f);
	EXPECT(f.copies[1].units.size() == 1 && f.copies[1].units[0].fade_from == 1 && f.copies[1].units[0].fade_to == 0 &&
	       Near(f.copies[1].units[0].fade, 0.4f));
	core.build(nullptr, nullptr, 2300, f);
	EXPECT(f.copies[1].units.empty() && f.listed == 0 && !f.fading);
	return true;
}

// an orthographic camera over world x, y in [0, 300]: clip = ((x - 150) / 150, (y - 150) / 150, z / 1000, 1)
CullCamera Ortho(float wrap)
{
	CullCamera c;
	const float vp[16] = {1.0f / 150, 0, 0, 0, 0, 1.0f / 150, 0, 0, 0, 0, 1.0f / 1000, 0, -1, -1, 0, 1};
	memcpy(c.vp, vp, sizeof(vp));
	c.wrap = wrap;
	c.valid = true;
	return c;
}

bool CaseCull()
{
	FakePort port;
	// plot (1, 1): x 96 in view; plot (9, 1): x 608, outside (copy -1 at x - 640 = -32 is inside with wrap 640)
	port.units = {Rec(0, 1001, 7, 1 * 10 + 1, 2, cu::kFlagVisible, 0, 100, 1), Rec(0, 1002, 7, 1 * 10 + 9, 2, cu::kFlagVisible, 0, 100, 2)};
	port.players = {Player(0, 3, 5, 0)};
	UnitsCore core(port, Log);
	core.setDatabase(TestLookups(), nullptr);
	core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7), Ev(2, SrvFeed::kUnitCreated, 0, 1002, 7)}), 0);
	const c5r::heightfield::HeightGrid g = Grid();
	BuiltFrame f;
	CullCamera c = Ortho(0);
	core.build(&c, &g, 0, f);
	EXPECT(f.copies[1].units.size() == 1 && f.copies[1].units[0].id == 1001 && f.culled == 1 && f.listed == 2);
	EXPECT(f.copies[1].water_level == -5.0f);
	c = Ortho(640);
	core.build(&c, &g, 0, f);
	EXPECT(f.copies[1].units.size() == 1 && f.copies[0].units.size() == 1 && f.copies[0].units[0].id == 1002 && f.culled == 0);
	EXPECT(f.copies[0].players.size() == 1 && f.copies[2].units.empty());
	// boxInView: behind the camera (z < 0 for every corner) is out
	EXPECT(!boxInView(c.vp, 0, 0, -50, 10, 10, -10));
	EXPECT(boxInView(c.vp, 0, 0, 0, 10, 10, 10));
	return true;
}

// Ticket 5056 (unit-server-scene.md 20): buildList takes each owner's PlayerArt once per call and builds a note's key only
// for a note. The lists, counters and notes are those of playerArt per unit: several owners (a valid one with units in
// view, out of view in the wrap copy and on an unknown plot; an unknown colour; an unknown era; no player record), two
// frames each, and an owner's record changed between two calls is seen by the next call (the cache lives one call).
bool CaseBuildArtOncePerOwner()
{
	FakePort port;
	port.units = {Rec(0, 1001, 7, 1 * 10 + 1, 2, cu::kFlagVisible, 0, 100, 1), Rec(0, 1002, 7, 1 * 10 + 9, 2, cu::kFlagVisible, 0, 100, 2),
	              Rec(0, 1003, 7, 9999, 2, cu::kFlagVisible, 0, 100, 3),      Rec(0, 1004, 8, 2 * 10 + 2, 2, cu::kFlagVisible, 0, 100, 4),
	              Rec(1, 1101, 7, 1 * 10 + 2, 2, cu::kFlagVisible, 0, 100, 5), Rec(2, 1201, 7, 1 * 10 + 3, 2, cu::kFlagVisible, 0, 100, 6),
	              Rec(3, 1301, 7, 1 * 10 + 4, 2, cu::kFlagVisible, 0, 100, 7)};
	port.players = {Player(0, 3, 5, 0), Player(1, 99, 5, 0), Player(2, 3, 5, 42)};
	UnitsCore core(port, Log);
	core.setDatabase(TestLookups(), nullptr);
	std::vector<SrvFeed::Event> ev;
	for(const SrvFeed::UnitRecord& r : port.units)
		ev.push_back(Ev(r.seq, SrvFeed::kUnitCreated, r.owner, r.id, r.unitType));
	core.onBatch(BatchOf(ev), 0);
	const c5r::heightfield::HeightGrid g = Grid();
	const CullCamera c = Ortho(640);
	BuiltFrame f;
	for(int frame = 0; frame < 2; ++frame)
	{
		core.build(&c, &g, 10.0 * frame, f);
		EXPECT(f.listed == 7 && f.notDrawn == 4 && f.culled == 0);
		EXPECT(f.copies[1].units.size() == 2 && f.copies[1].units[0].id == 1001 && f.copies[1].units[1].id == 1004);
		EXPECT(f.copies[0].units.size() == 1 && f.copies[0].units[0].id == 1002 && f.copies[2].units.empty());
		for(int k = 0; k < 2; ++k)
		{
			const PlayerArt want = core.playerArt(0);
			EXPECT(f.copies[k].players.size() == 1);
			const cu::PlayerInput& p = f.copies[k].players[0];
			EXPECT(p.player == 0 && p.colour == want.in.colour && p.art_style == want.in.art_style && p.era == want.in.era);
		}
	}
	const CoreCounters& n = core.counters();
	EXPECT(n.byStatus[(int)Status::kPlotUnknown] == 2 && n.byStatus[(int)Status::kColourUnknown] == 2);
	EXPECT(n.byStatus[(int)Status::kPlayerArtUnknown] == 2 && n.byStatus[(int)Status::kPlayerUnknown] == 2);
	EXPECT(LogCount("unit (0, 1003) plot 9999 outside the feed's grid 10x8") == 1);   // the lazy key: still once per unit
	EXPECT(LogCount("player 1: colour index 99 has no PlayerColors row") == 1 && LogCount("player 2: era index 42 has no Eras row") == 1);
	EXPECT(LogCount("player 3 (owner of unit 1301) has no player record") == 1);
	// player 1's colour becomes known between two calls: its unit is drawn on the next call, and the copy lists both owners
	SrvFeed::Batch b;
	b.width = 10;
	b.height = 8;
	b.players = {Player(1, 3, 5, 0)};
	core.onBatch(b, 20);
	core.build(&c, &g, 30, f);
	EXPECT(f.listed == 7 && f.notDrawn == 3 && f.copies[1].units.size() == 3 && f.copies[1].units[2].id == 1101);
	EXPECT(f.copies[1].players.size() == 2 && f.copies[1].players[0].player == 0 && f.copies[1].players[1].player == 1 &&
	       f.copies[1].players[1].colour == "PLAYERCOLOR_POLYNESIA" && f.copies[0].players.size() == 1);
	EXPECT(n.byStatus[(int)Status::kColourUnknown] == 2);
	return true;
}

bool CasePassCamera()
{
	UnitsCamera cam;
	// view: identity rotation, eye at (100, -200, 300): row 3 = -eye
	for(int i = 0; i < 4; ++i)
		cam.view[i * 5] = cam.proj[i * 5] = cam.camWorld[i * 5] = 1;
	cam.view[12] = -100;
	cam.view[13] = 200;
	cam.view[14] = -300;
	cam.camWorld[12] = 100.5f;   // the EXE's own position (row 3 of camWorld) wins when given
	cam.camWorld[13] = -200;
	cam.camWorld[14] = 300;
	cam.proj[0] = 2;
	cam.wrapPeriod = 640;
	cu::PassCamera pc;
	EXPECT(passCamera(cam, 0, 10.117f, pc));
	EXPECT(pc.eye[0] == 100.5f && pc.eye[1] == -200 && pc.eye[2] == 300 && pc.threshold == 10.117f);
	EXPECT(pc.viewProj[0].x == 2 && pc.viewProj[3].x == -200 && pc.viewProj[3].y == 200 && pc.viewProj[3].z == -300 && pc.viewProj[3].w == 1);
	float fog[8];
	const float eye[3] = {100.5f, -200, 300};
	c5r::terrain::distanceFogBlock(c5r::terrain::DistanceFog(), eye, fog);
	EXPECT(pc.fog.x == fog[4] && pc.fog.y == fog[5] && pc.fog.z == fog[6] && pc.fog.w == fog[7]);
	EXPECT(pc.fogColour.x == fog[0] && pc.fogColour.y == fog[1] && pc.fogColour.z == fog[2] && pc.fogColour.w == fog[3]);
	EXPECT(pc.fog.x == -0.05f && Near(pc.fogColour.z, 243.0f / 255.0f));
	const cu::Cb1 cb = cu::makeCb1(pc);
	EXPECT(cb.r[3].x == 100.5f && cb.r[11].x == 100.5f && cb.r[8].x == 10.117f && cb.r[4].x == 2 && cb.r[10].x == fog[0]);
	// wrap copy +1: row 3 += 640 x row 0
	EXPECT(passCamera(cam, 1, 0, pc) && pc.viewProj[3].x == -200 + 640 * 2);
	// camWorld all zero: the eye from the inverse of view
	memset(cam.camWorld, 0, sizeof(cam.camWorld));
	EXPECT(passCamera(cam, 0, 0, pc) && Near(pc.eye[0], 100) && Near(pc.eye[1], -200) && Near(pc.eye[2], 300));
	// not invertible / not finite: refused
	memset(cam.view, 0, sizeof(cam.view));
	EXPECT(!passCamera(cam, 0, 0, pc));
	cam.view[0] = NAN;
	EXPECT(!passCamera(cam, 0, 0, pc));
	return true;
}

// One unit of player `owner` whose problem is `rec`/`player`; the scene's other unit (player 0) must still be drawn.
bool StatusCase(Status st, const SrvFeed::UnitRecord& bad, const SrvFeed::PlayerRecord* badPlayer, bool createBad, const char* needle)
{
	FakePort port;
	port.units = {Rec(0, 1001, 7, 23, 2, cu::kFlagVisible, 0, 100, 1), bad};
	port.players = {Player(0, 3, 5, 0)};
	if(badPlayer)
		port.players.push_back(*badPlayer);
	UnitsCore core(port, Log);
	core.setDatabase(TestLookups(), nullptr);
	std::vector<SrvFeed::Event> ev = {Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7)};
	if(createBad)
		ev.push_back(Ev(2, SrvFeed::kUnitCreated, bad.owner, bad.id, bad.unitType));
	core.onBatch(BatchOf(ev), 0);
	BuiltFrame f;
	core.build(nullptr, nullptr, 0, f);
	core.build(nullptr, nullptr, 10, f);   // twice: counted per frame, logged once per key
	EXPECT(f.copies[1].units.size() == 1 && f.copies[1].units[0].id == 1001 && f.notDrawn == 1);
	EXPECT(core.counters().byStatus[(int)st] == 2);
	const std::string tag = std::string("LOUD: units: ") + statusName(st) + ": ";
	EXPECT(LogCount(tag) == 1 && LogCount(needle) == 1);
	for(int i = 0; i < (int)Status::kCount; ++i)
		if(i != (int)st)
			EXPECT(core.counters().byStatus[i] == 0);
	return true;
}

bool CaseColourUnknown()
{
	const SrvFeed::PlayerRecord p1 = Player(1, 99, 5, 0);
	return StatusCase(Status::kColourUnknown, Rec(1, 1501, 7, 30, 2, cu::kFlagVisible, 0, 100, 2), &p1, true,
	                  "player 1: colour index 99 has no PlayerColors row: its units not drawn");
}
bool CasePlayerUnknown()
{
	return StatusCase(Status::kPlayerUnknown, Rec(4, 1501, 7, 30, 2, cu::kFlagVisible, 0, 100, 2), nullptr, true,
	                  "player 4 (owner of unit 1501) has no player record");
}
bool CaseUnitTypeUnknown()
{
	return StatusCase(Status::kUnitTypeUnknown, Rec(0, 1501, 999, 30, 2, cu::kFlagVisible, 0, 100, 2), nullptr, true,
	                  "unit type index 999 (unit 1501 of player 0) has no Units row");
}
bool CasePlayerArtUnknown()
{
	const SrvFeed::PlayerRecord p1 = Player(1, 3, 5, 42);
	return StatusCase(Status::kPlayerArtUnknown, Rec(1, 1501, 7, 30, 2, cu::kFlagVisible, 0, 100, 2), &p1, true,
	                  "player 1: era index 42 has no Eras row");
}
bool CasePlotUnknown()
{
	return StatusCase(Status::kPlotUnknown, Rec(0, 1501, 7, 9999, 2, cu::kFlagVisible, 0, 100, 2), nullptr, true,
	                  "unit (0, 1501) plot 9999 outside the feed's grid 10x8");
}
bool CaseSeedMissing()
{
	return StatusCase(Status::kSeedMissing, Rec(0, 1501, 7, 30, 2, cu::kFlagVisible, 0, 100, 1), nullptr, false,
	                  "unit (0, 1501) UNIT_WARRIOR: the allocator holds no seed");
}

// Issue 4463 (note 3.4's race): the scene over the live feed (the real srv_feed.cpp): a PushUnit between the render
// thread's Drain and the scene's copy (SrvFeed::CopyUnits through LiveFeedPort) is not in the copy, so no record is newer
// than the batch (0 FeedOutOfOrder); it comes with the next batch. Then a writer thread pushes while the loop drains,
// takes and builds: still 0. The global feed is reset before and after.
const int kLivePushes = 40000;   // namespace scope: the writer's lambda needs no capture of it (MSVC C3493)
bool LiveFeedNoRace()
{
	struct Reset
	{
		Reset() { SrvFeed::ResetForTest(); }
		~Reset() { SrvFeed::ResetForTest(); }
	} reset;
	SrvFeed::SetEnabled(true);
	SrvFeed::SetUnitsEnabled(true);
	SrvFeed::SetQueueCap(1u << 20);
	SrvFeed::BeginSnapshot(10, 8);   // the grid of TestLookups' scenes (BatchOf's 10 x 8)
	SrvFeed::EndSnapshot();
	SrvFeed::SetPlayer(Player(0, 3, 5, 0));
	SrvFeed::SetPlayer(Player(1, 3, 5, 0));
	UnitsCore live(LiveFeedPort(), Log);
	live.setDatabase(TestLookups(), nullptr);
	const int oo = (int)Status::kFeedOutOfOrder;
	SrvFeed::Batch b;
	EXPECT(SrvFeed::Drain(b) && b.events.size() == 1 && b.events[0].kind == SrvFeed::kSnapshot && b.width == 10);
	live.onBatch(b, 0);   // the start: the (empty) table and the player rows
	SrvFeed::UnitRecord a = Rec(0, 2001, 7, 23), c = Rec(0, 2002, 7, 24);
	SrvFeed::PushUnit(Ev(0, SrvFeed::kUnitCreated, 0, 2001, 7, 0, 23), &a, SrvFeed::UnitOp::kUpsert);
	EXPECT(SrvFeed::Drain(b) && b.events.size() == 1);
	SrvFeed::PushUnit(Ev(0, SrvFeed::kUnitCreated, 0, 2002, 7, 0, 24), &c, SrvFeed::UnitOp::kUpsert);   // the DLL's thread
	live.onBatch(b, 0);
	EXPECT(live.counters().byStatus[oo] == 0 && live.tableSize() == 1 && live.lastSeq() == b.events.back().seq);
	EXPECT(SrvFeed::Drain(b) && b.events.size() == 1);
	live.onBatch(b, 1);
	BuiltFrame f;
	live.build(nullptr, nullptr, 1, f);
	EXPECT(live.counters().byStatus[oo] == 0 && live.tableSize() == 2 && f.copies[1].units.size() == 2);
	// a writer thread: per key a creation, three damages and a removal, 64 keys round and round
	std::atomic<bool> writerDone(false);
	std::thread w([&writerDone]() {
		for(int i = 0; i < kLivePushes; ++i)
		{
			const int k = (i / 5) % 64, step = i % 5;
			SrvFeed::UnitRecord u = Rec(1, 3000 + k, 7, 30, 2, cu::kFlagVisible, step * 10);
			const uint16_t kind = step == 0 ? SrvFeed::kUnitCreated : step == 4 ? SrvFeed::kUnitDestroyed : SrvFeed::kUnitSetDamage;
			SrvFeed::PushUnit(Ev(0, kind, 1, 3000 + k, step * 10, 0, 30), &u,
			                  step == 4 ? SrvFeed::UnitOp::kRemove : SrvFeed::UnitOp::kUpsert);
			if(i % 32 == 0)
				SwitchToThread();
		}
		writerDone = true;
	});
	size_t batches = 0, withUnits = 0;
	double t = 2;
	for(;;)
	{
		const bool finished = writerDone.load();
		const bool any = SrvFeed::Drain(b);
		SwitchToThread();   // let the writer in between the Drain and the copy
		if(any)
		{
			live.onBatch(b, t);
			live.build(nullptr, nullptr, t, f);
			batches++;
			withUnits += !b.events.empty();
		}
		t += 1;
		if(finished && b.events.empty())
			break;
	}
	w.join();
	const SrvFeed::Counters2 c2 = SrvFeed::GetCounters2();
	printf("    info live_feed_no_race: %zu batches (%zu with events), %llu unit events, table %zu, FeedOutOfOrder %llu\n", batches,
	       withUnits, (unsigned long long)c2.unitEvents, live.tableSize(), (unsigned long long)live.counters().byStatus[oo]);
	EXPECT(c2.droppedUnit == 0 && batches >= 2);
	EXPECT(live.counters().byStatus[oo] == 0);
	EXPECT(live.tableSize() == 2);   // every key of the writer was removed again
	return true;
}

bool CaseFeedOutOfOrder()
{
	FakePort port;
	port.units = {Rec(0, 1001, 7, 23, 2, cu::kFlagVisible, 0, 100, 1)};
	port.players = {Player(0, 3, 5, 0)};
	UnitsCore core(port, Log);
	core.setDatabase(TestLookups(), nullptr);
	core.onBatch(BatchOf({Ev(5, SrvFeed::kUnitCreated, 0, 1001, 7)}), 0);
	// (1) a seq not above the last taken: skipped (no second record for the key)
	core.onBatch(BatchOf({Ev(5, SrvFeed::kUnitCreated, 0, 1001, 7)}), 0);
	// issue 4460: the real allocator (4453) groups one drain's creations and applies them at Advance (UnitsCore::build
	// calls it once per frame); the test double counted at once
	BuiltFrame f0;
	core.build(nullptr, nullptr, 0, f0);
	EXPECT(core.counters().byStatus[(int)Status::kFeedOutOfOrder] == 1 && core.seedCounters().created == 1);
	EXPECT(LogCount("kind 11 seq 5 not above the last taken 5: skipped") == 1);
	// (2) a path index outside the batch's side table
	core.onBatch(BatchOf({Ev(6, SrvFeed::kUnitMoved, 0, 1001, 3, 2)}), 0);
	EXPECT(core.counters().byStatus[(int)Status::kFeedOutOfOrder] == 2 && LogCount("path index 3 outside the batch's 0 paths") == 1);
	// (3) a table record newer than the drained events (the feed's CopyUnits contract broken)
	port.units[0].seq = 99;
	core.onBatch(BatchOf({Ev(7, SrvFeed::kUnitFortify, 0, 1001, 1)}), 0);
	EXPECT(core.counters().byStatus[(int)Status::kFeedOutOfOrder] == 3 && LogCount("has seq 99 above the last drained 7") == 1);
	BuiltFrame f;
	core.build(nullptr, nullptr, 0, f);
	EXPECT(f.copies[1].units.size() == 1);   // the unit stays drawn
	return LiveFeedNoRace();
}

bool CaseAllocatorStatuses()
{
	FakePort port;
	port.units = {Rec(0, 1001, 7, 23, 2, cu::kFlagVisible, 0, 100, 2)};
	port.players = {Player(0, 3, 5, 0)};
	UnitsCore core(port, Log);
	core.setDatabase(TestLookups(), nullptr);
	core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitDestroyed, 0, 7777), Ev(2, SrvFeed::kUnitCreated, 0, 1001, 7)}), 0);
	EXPECT(core.counters().byStatus[(int)Status::kUnknownRemoval] == 1 && LogCount("LOUD: units: UnknownRemoval: allocator") == 1);
	EXPECT(LogCount("kind 14 seq 1 key (0, 7777)") == 1);
	core.onBatch(BatchOf({Ev(3, SrvFeed::kUnitCreated, 0, 1001, 7)}), 0);
	EXPECT(core.counters().byStatus[(int)Status::kKnownCreation] == 1 && LogCount("LOUD: units: KnownCreation: allocator") == 1);
	SrvFeed::Event ov = Ev(4, SrvFeed::kOverflow, 3, 0);
	core.onBatch(BatchOf({ov}), 0);
	EXPECT(core.counters().byStatus[(int)Status::kAllocatorOutOfStep] == 1 && LogCount("LOUD: units: AllocatorOutOfStep: allocator") == 1);
	EXPECT(port.copies == 3);   // the overflow re-reads the table
	BuiltFrame f;
	core.build(nullptr, nullptr, 0, f);
	EXPECT(f.copies[1].units.size() == 1);   // units stay drawn with the allocator's seeds (decision 4)
	// the mapping of every allocator status (CombatHoldAmbiguous is reached by 4453's hold model; here its name and route)
	EXPECT(statusOfSeed(SeedStatus::kCombatHoldAmbiguous) == Status::kCombatHoldAmbiguous);
	EXPECT(statusOfSeed(SeedStatus::kOutOfStep) == Status::kAllocatorOutOfStep && statusOfSeed(SeedStatus::kOk) == Status::kCount);
	core.note(statusOfSeed(SeedStatus::kCombatHoldAmbiguous), "", "allocator: %s", "test");
	EXPECT(core.counters().byStatus[(int)Status::kCombatHoldAmbiguous] == 1 && LogCount("LOUD: units: CombatHoldAmbiguous: allocator: test") == 1);
	// a load (kLoadBegin, a new table generation): the table re-read, fades cleared, the DLL's reset removals no-ops
	port.gen = 1;
	port.units.clear();
	SrvFeed::Batch b = BatchOf({Ev(5, SrvFeed::kLoadBegin, -1, -1), Ev(6, SrvFeed::kUnitDestroyed, 0, 1001)});
	b.unitTableGen = 1;
	core.onBatch(b, 0);
	EXPECT(port.copies == 4 && core.counters().byStatus[(int)Status::kUnknownRemoval] == 1 && core.tableTaken());
	return true;
}

// ---------------------------------------------------------------------------------------------- issue 4460 (wave 3)
// A1: a city-state's art key is its MinorCivilizations.Type, from the minor civilization index the feed puts in
// PlayerRecord::civilization when flags bit 0 is set (srv_feed.h); an index without a row is PlayerArtUnknown.
bool CaseMinorCivArt()
{
	FakePort port;
	port.units = {Rec(1, 1501, 7, 30, 2, cu::kFlagVisible, 0, 100, 1)};
	port.players = {Player(1, 3, 2, 0, 5, 1)};   // minor: civilization = MinorCivilizations ID 2, art style 5
	UnitsCore core(port, Log);
	core.setDatabase(TestLookups(), nullptr);
	core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 1, 1501, 7)}), 0);
	BuiltFrame f;
	core.build(nullptr, nullptr, 0, f);
	EXPECT(f.copies[1].units.size() == 1 && f.copies[1].players.size() == 1);
	EXPECT(f.copies[1].players[0].art_style == "MINOR_CIV_MSOURA");
	EXPECT(core.playerArt(1).artOk && core.counters().byStatus[(int)Status::kPlayerArtUnknown] == 0);
	// a major civilization's index in a minor's record is not a MinorCivilizations row: refused by name, never a default
	g_log.clear();
	const SrvFeed::PlayerRecord p1 = Player(1, 3, 5, 0, 5, 1);
	return StatusCase(Status::kPlayerArtUnknown, Rec(1, 1501, 7, 30, 2, cu::kFlagVisible, 0, 100, 2), &p1, true,
	                  "player 1: minor civilization index 5 has no MinorCivilizations row: its units not drawn");
}

// The combat input of the allocator (decision 14 (a)): kinds 25 / 28 reach SeedAllocator::NoteCombat with the batch's
// combat record; a visible combat in the human's turn with quick combat off holds the defender's removal for the
// stock path's measured length (4.6 s melee), so a creation inside the hold appends instead of taking the freed index.
static SrvFeed::CombatRecord Combat(uint32_t seq, uint16_t kind, int ao, int au, int dOwner, int du, uint16_t flags)
{
	SrvFeed::CombatRecord c;
	memset(&c, 0, sizeof(c));
	c.seq = seq;
	c.kind = kind;
	c.flags = flags;
	c.plot = 24;
	c.attackerOwner = ao;
	c.attackerUnit = au;
	c.attackerCity = au < 0 ? 0 : -1;
	c.defenderOwner = dOwner;
	c.defenderUnit = du;
	c.defenderCity = -1;
	return c;
}

static bool CombatHoldRun(uint16_t flags, uint16_t kind, uint32_t& seedOfNew)
{
	FakePort port;
	port.units = {Rec(0, 1001, 7, 23), Rec(63, 2001, 7, 24)};
	port.players = {Player(0, 3, 5, 0), Player(63, 3, 5, 0)};
	UnitsCore core(port, Log);
	core.setDatabase(TestLookups(), nullptr);
	core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 23), Ev(2, SrvFeed::kUnitCreated, 63, 2001, 7, 0, 24)}), 0);
	BuiltFrame f;
	core.build(nullptr, nullptr, 0, f);
	const bool city = kind == SrvFeed::kCityCombat;
	SrvFeed::Batch b = BatchOf({Ev(3, kind, 0, city ? 7 : 1001, 0, 0, 24)});
	b.combats.push_back(Combat(3, kind, 0, city ? -1 : 1001, 63, 2001, flags));
	core.onBatch(b, 100);
	core.build(nullptr, nullptr, 100, f);
	port.units = {Rec(0, 1001, 7, 23)};
	core.onBatch(BatchOf({Ev(4, SrvFeed::kUnitDestroyed, 63, 2001, 0, 0, 24)}), 500);
	core.build(nullptr, nullptr, 500, f);
	port.units = {Rec(0, 1001, 7, 23), Rec(0, 1003, 7, 25)};
	core.onBatch(BatchOf({Ev(5, SrvFeed::kUnitCreated, 0, 1003, 7, 0, 25)}), 1000);
	core.build(nullptr, nullptr, 1000, f);
	EXPECT(core.counters().combatNotes == 1);
	EXPECT(core.seedOf(0, 1003, seedOfNew));
	core.build(nullptr, nullptr, 6000, f);   // past the modelled end: the held removal applied
	uint32_t s2 = 0;
	EXPECT(!core.seedOf(63, 2001, s2));
	return true;
}

bool CaseCombatHold()
{
	const uint16_t human = SrvFeed::kCombatVisualize | SrvFeed::kCombatGameRead | SrvFeed::kCombatActiveTurn;
	uint32_t seed = 99;
	// held: the creation at 1.0 s comes inside the 4.6 s hold, the defender's index 1 is still taken: it appends (2)
	EXPECT(CombatHoldRun(human, SrvFeed::kUnitCombat, seed) && seed == 2);
	// a city's attack (kind 28) holds its unit defender alike
	EXPECT(CombatHoldRun(human, SrvFeed::kCityCombat, seed) && seed == 2);
	// not held: an AI turn (active player's turn not active), quick combat, an invisible combat, unread game flags
	EXPECT(CombatHoldRun(SrvFeed::kCombatVisualize | SrvFeed::kCombatGameRead, SrvFeed::kUnitCombat, seed) && seed == 1);
	EXPECT(CombatHoldRun(human | SrvFeed::kCombatQuickOption, SrvFeed::kUnitCombat, seed) && seed == 1);
	EXPECT(CombatHoldRun(SrvFeed::kCombatGameRead | SrvFeed::kCombatActiveTurn, SrvFeed::kUnitCombat, seed) && seed == 1);
	EXPECT(CombatHoldRun(SrvFeed::kCombatVisualize | SrvFeed::kCombatActiveTurn, SrvFeed::kUnitCombat, seed) && seed == 1);
	// the record's class: ranged / bombing / air sweep take the ranged length; a city side has no unit key
	FakePort port;
	UnitsCore core(port, Log);
	EXPECT(core.combatNoteOf(Combat(1, SrvFeed::kUnitCombat, 0, 1, 1, 2, SrvFeed::kCombatRanged)).cls == HoldClass::kRanged);
	EXPECT(core.combatNoteOf(Combat(1, SrvFeed::kUnitCombat, 0, 1, 1, 2, SrvFeed::kCombatAirSweep)).cls == HoldClass::kRanged);
	EXPECT(core.combatNoteOf(Combat(1, SrvFeed::kUnitCombat, 0, 1, 1, 2, SrvFeed::kCombatBombing)).cls == HoldClass::kRanged);
	EXPECT(core.combatNoteOf(Combat(1, SrvFeed::kUnitCombat, 0, 1, 1, 2, 0)).cls == HoldClass::kMelee);
	const CombatNote cityAtt = core.combatNoteOf(Combat(1, SrvFeed::kCityCombat, 0, -1, 1, 2, 0));
	EXPECT(cityAtt.attacker.owner == -1 && cityAtt.attacker.id == -1 && cityAtt.defender.owner == 1 && cityAtt.defender.id == 2);
	EXPECT(core.counters().combatGameUnread == 5);
	return true;
}

// Negative: a combat event whose index is outside the batch's combats (or names another seq) is FeedOutOfOrder.
bool CaseCombatOutOfBatch()
{
	FakePort port;
	port.units = {Rec(0, 1001, 7, 23)};
	port.players = {Player(0, 3, 5, 0)};
	UnitsCore core(port, Log);
	core.setDatabase(TestLookups(), nullptr);
	core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 23)}), 0);
	core.onBatch(BatchOf({Ev(2, SrvFeed::kUnitCombat, 0, 1001, 0, 0, 24)}), 10);   // no combats in the batch
	EXPECT(core.counters().byStatus[(int)Status::kFeedOutOfOrder] == 1 && core.counters().combatNotes == 0);
	EXPECT(LogCount("kind 25 seq 2 combat index 0 outside the batch's 0 combats") == 1);
	SrvFeed::Batch b = BatchOf({Ev(3, SrvFeed::kCityCombat, 0, 7, 0, 0, 24)});
	b.combats.push_back(Combat(9, SrvFeed::kCityCombat, 0, -1, 0, 1001, 0));   // another seq
	core.onBatch(b, 20);
	EXPECT(core.counters().byStatus[(int)Status::kFeedOutOfOrder] == 2 && core.counters().combatNotes == 0);
	core.onBatch(BatchOf({Ev(4, SrvFeed::kUnitCombat, 0, 1001, -1, 0, 24)}), 30);   // the feed dropped the record (a[2] -1)
	EXPECT(core.counters().byStatus[(int)Status::kFeedOutOfOrder] == 3);
	return true;
}

// The visible flag at a load (note 2.5, 3.2; 4460): the DLL's setupGraphical sends Created without a Visibility for a
// unit in the open, Created + Visibility(false) + Garrison(true) for a garrisoned one, Embark(true) alone for an
// embarked one. The feed's table sets visible at a record's insertion (srv_feed.cpp ApplyUnitLocked); the scene lists
// exactly the records with the flag and no fade, so the first two are drawn and the garrisoned one is not.
bool CaseVisibleAtLoad()
{
	FakePort port;
	port.gen = 1;
	port.units = {Rec(0, 1001, 7, 23, 2, cu::kFlagVisible), Rec(0, 1002, 7, 24, 2, cu::kFlagVisible | cu::kFlagEmbarked),
	              Rec(0, 1003, 7, 25, 2, cu::kFlagGarrisoned)};
	port.players = {Player(0, 3, 5, 0)};
	UnitsCore core(port, Log);
	core.setDatabase(TestLookups(), nullptr);
	SrvFeed::Batch b = BatchOf({Ev(1, SrvFeed::kLoadBegin, 1, 0), Ev(2, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 23),
	                            Ev(3, SrvFeed::kUnitShouldDimFlag, 0, 1001, 0, 0, 23), Ev(4, SrvFeed::kUnitFortify, 0, 1001, 0, 0, 23),
	                            Ev(5, SrvFeed::kUnitEmbark, 0, 1002, 1, 0, 24), Ev(6, SrvFeed::kUnitCreated, 0, 1003, 7, 0, 25),
	                            Ev(7, SrvFeed::kUnitVisibility, 0, 1003, 0, 0, 25), Ev(8, SrvFeed::kUnitGarrison, 0, 1003, 1, 0, 25)});
	b.unitTableGen = 1;
	core.onBatch(b, 0);
	BuiltFrame f;
	core.build(nullptr, nullptr, 0, f);
	EXPECT(f.listed == 2 && f.copies[1].units.size() == 2);
	EXPECT(f.copies[1].units[0].id == 1001 && f.copies[1].units[1].id == 1002);
	EXPECT(f.copies[1].units[0].fade_from == 1 && f.copies[1].units[0].fade_to == 0 && f.copies[1].units[0].fade == 0);
	uint32_t s0 = 9, s1 = 9, s2 = 9;   // all three take a record, in load order (S2), the garrisoned one too
	EXPECT(core.seedOf(0, 1001, s0) && core.seedOf(0, 1002, s1) && core.seedOf(0, 1003, s2) && s0 == 0 && s1 == 1 && s2 == 2);
	// issue 4463, rule (f) (note 12.1 [L]): a Visibility before the creation (the feed flags it kEventNoRecord) sets no
	// fade: 12.1's worker on the stacked embarked plot (Visibility(0), then Embark(1) as its creation, which erases no
	// fade) is listed, as the EXE draws it
	port.units.push_back(Rec(0, 1004, 7, 26, 2, cu::kFlagVisible | cu::kFlagEmbarked, 0, 100, 10));
	SrvFeed::Event pre = Ev(9, SrvFeed::kUnitVisibility, 0, 1004, 0, 0, 26);
	pre.flags = SrvFeed::kEventNoRecord;
	core.onBatch(BatchOf({pre, Ev(10, SrvFeed::kUnitEmbark, 0, 1004, 1, 0, 26)}), 10);
	core.build(nullptr, nullptr, 10, f);
	EXPECT(f.listed == 3 && f.copies[1].units.size() == 3 && f.copies[1].units[2].id == 1004);
	// negative: the same call for a held key (no flag) hides it at once (blend 0), the table's flag notwithstanding
	core.onBatch(BatchOf({Ev(11, SrvFeed::kUnitVisibility, 0, 1004, 0, 0, 26)}), 20);
	core.build(nullptr, nullptr, 20, f);
	EXPECT(f.listed == 2 && f.copies[1].units.size() == 2);
	return true;
}

// ----------------------------------------------------------------------------------------- module (install-backed) cases
std::string Tsv(const std::vector<std::vector<std::string>>& rows)
{
	std::string s;
	for(const auto& r : rows)
	{
		for(size_t i = 0; i < r.size(); ++i)
			s += (i ? "\t" : "") + r[i];
		s += "\n";
	}
	return s;
}

// The 4352 fixture's warrior and trireme (render/tests/units/units_fixture.h) with the ID columns the scene reads; `bad`
// adds UNIT_NOART (an art row that does not exist: the pre-check fails); `noArt` leaves the art tables out.
std::unique_ptr<c5m::TsvTables> TestDb(bool bad, bool noArt = false, bool noAnimTables = false)
{
	std::unique_ptr<c5m::TsvTables> db(new c5m::TsvTables());
	c5m::Error e;
	std::vector<std::vector<std::string>> units = {{"ID", "Type", "UnitArtInfo", "UnitArtInfoCulturalVariation", "UnitArtInfoEraVariation"},
	                                               {"7", "UNIT_WARRIOR", "ART_DEF_UNIT__WARRIOR", "0", "0"},
	                                               {"8", "UNIT_TRIREME", "ART_DEF_UNIT_TRIREME", "0", "0"}};
	if(bad)
		units.push_back({"9", "UNIT_NOART", "ART_DEF_UNIT_NOT_A_ROW", "0", "0"});
	db->add("Units", Tsv(units), &e);
	if(!noArt)
	{
		db->add("ArtDefine_UnitInfos", Tsv({{"Type", "Formation", "DamageStates"}, {"ART_DEF_UNIT__WARRIOR", "Warrior", "1"}, {"ART_DEF_UNIT_TRIREME", "\\N", "3"}}),
		        &e);
		db->add("ArtDefine_UnitInfoMemberInfos", Tsv({{"UnitInfoType", "UnitMemberInfoType", "NumMembers"},
		                                              {"ART_DEF_UNIT__WARRIOR", "ART_DEF_UNIT_MEMBER_WARRIOR_V2", "3"},
		                                              {"ART_DEF_UNIT__WARRIOR", "ART_DEF_UNIT_MEMBER_WARRIOR", "6"},
		                                              {"ART_DEF_UNIT__WARRIOR", "ART_DEF_UNIT_MEMBER_WARRIOR_V4", "3"},
		                                              {"ART_DEF_UNIT_TRIREME", "ART_DEF_UNIT_MEMBER_TRIREME", "1"}}),
		        &e);
		db->add("ArtDefine_UnitMemberInfos",
		        Tsv({{"Type", "Scale", "ZOffset", "Domain", "Model", "MaterialTypeTag"},
		             {"ART_DEF_UNIT_MEMBER_WARRIOR_V2", "0.14000000059604645", "\\N", "", "Assets/Units/Warrior/Warrior_V2.fxsxml", "CLOTH"},
		             {"ART_DEF_UNIT_MEMBER_WARRIOR", "0.14000000059604645", "\\N", "", "Assets/Units/Warrior/Warrior.fxsxml", "CLOTH"},
		             {"ART_DEF_UNIT_MEMBER_WARRIOR_V4", "0.14000000059604645", "\\N", "", "Assets/Units/Warrior/Warrior_V4.fxsxml", "CLOTH"},
		             {"ART_DEF_UNIT_MEMBER_TRIREME", "0.07000000029802322", "\\N", "Sea", "Assets/Units/Trireme/Trireme.fxsxml", "WOOD"}}),
		        &e);
	}
	// issue 4456: the controller's bridge tables (ArtDefine_UnitMemberCombats, Builds) and Builds (ID, Type) for kind 18
	if(!noAnimTables)
	db->add("ArtDefine_UnitMemberCombats",
	        Tsv({{"UnitMemberType", "EnableActions"},
	             {"ART_DEF_UNIT_MEMBER_WARRIOR_V2", "Idle Attack RunCharge AttackCity Death BombardDefend Run Fortify CombatReady"},
	             {"ART_DEF_UNIT_MEMBER_WARRIOR", "Idle Attack RunCharge AttackCity Death BombardDefend Run Fortify CombatReady"},
	             {"ART_DEF_UNIT_MEMBER_WARRIOR_V4", "Idle Attack RunCharge AttackCity Death BombardDefend Run Fortify CombatReady"},
	             {"ART_DEF_UNIT_MEMBER_TRIREME", "Idle Attack AttackCity Death BombardDefend Run Fortify CombatReady"},
	             {"ART_DEF_UNIT_MEMBER_U_ASSYRIAN_SIEGE_TOWER", "Idle AttackCity AttackCharge Death BombardDefend Run Fortify CombatReady"}}),
	        &e);
	if(!noAnimTables)
		db->add("Builds", Tsv({{"ID", "Type", "EntityEvent"}, {"0", "BUILD_ROAD", "BUILD"}}), &e);
	db->add("Eras", Tsv({{"ID", "Type", "ArtPrefix"}, {"0", "ERA_ANCIENT", "ANCIENT"}}), &e);
	db->add("Civilizations", Tsv({{"ID", "Type", "ArtStyleType", "ArtStyleSuffix"}, {"5", "CIVILIZATION_POLYNESIA", "ARTSTYLE_POLYNESIAN", "_MED"}}), &e);
	db->add("MinorCivilizations", Tsv({{"ID", "Type", "ArtStyleType", "ArtStyleSuffix"}}), &e);
	db->add("PlayerColors", Tsv({{"ID", "Type", "SecondaryColor"}, {"3", "PLAYERCOLOR_POLYNESIA", "COLOR_P"}}), &e);
	db->add("Colors", Tsv({{"Type", "Red", "Green", "Blue", "Alpha"}, {"COLOR_P", "0.851", "0.349", "0.0", "1.0"}}), &e);
	db->add("ArtStyleTypes", Tsv({{"ID", "Type"}, {"5", "ARTSTYLE_POLYNESIAN"}}), &e);
	return db;
}

EnvOpener TestEnv(bool bad, bool noArt = false, bool unreadable = false)
{
	return [=](const std::wstring&, Env& out, std::string& why) {
		if(unreadable)
		{
			why = "test: the copy is unreadable";
			return false;
		}
		const c5r::Status s = out.gf.open(c5r::gamefs::fromUtf8(g_game));
		if(!s.ok())
		{
			why = s.str();
			return false;
		}
		out.db = TestDb(bad, noArt);
		out.exe = c5r::gamefs::fromUtf8(g_exe);
		out.install = c5r::gamefs::fromUtf8(g_game);
		return true;
	};
}

struct Host
{
	FakePort port;
	std::unique_ptr<SceneUnits> u;
	UnitsHostProbe* probe = nullptr;
	double t = 1000;
	explicit Host(EnvOpener env, const UnitsOptions& o = UnitsOptions())
	{
		port.haveDb = true;
		port.db = L"test.db";
		port.dbGen = 1;
		port.players = {Player(0, 3, 5, 0)};
		u = MakeSceneUnitsWith(o, port, std::move(env), Log, [this]() { return t; });
		probe = dynamic_cast<UnitsHostProbe*>(u.get());
	}
};

struct Target
{
	ID3D11Texture2D* rt = nullptr;
	ID3D11RenderTargetView* rtv = nullptr;
	ID3D11Texture2D* ds = nullptr;
	ID3D11DepthStencilView* dsv = nullptr;
	uint32_t w = 256, h = 256;
	Target()
	{
		D3D11_TEXTURE2D_DESC d = {};
		d.Width = w;
		d.Height = h;
		d.MipLevels = d.ArraySize = 1;
		d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		d.SampleDesc.Count = 1;
		d.BindFlags = D3D11_BIND_RENDER_TARGET;
		g_dev->CreateTexture2D(&d, nullptr, &rt);
		g_dev->CreateRenderTargetView(rt, nullptr, &rtv);
		d.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		g_dev->CreateTexture2D(&d, nullptr, &ds);
		g_dev->CreateDepthStencilView(ds, nullptr, &dsv);
		clear();
	}
	void clear()
	{
		const float c[4] = {0, 0, 0, 0};
		g_ctx->ClearRenderTargetView(rtv, c);
		g_ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	}
	size_t covered()
	{
		D3D11_TEXTURE2D_DESC d;
		rt->GetDesc(&d);
		d.Usage = D3D11_USAGE_STAGING;
		d.BindFlags = 0;
		d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ID3D11Texture2D* s = nullptr;
		if(FAILED(g_dev->CreateTexture2D(&d, nullptr, &s)))
			return 0;
		g_ctx->CopyResource(s, rt);
		D3D11_MAPPED_SUBRESOURCE m;
		size_t n = 0;
		if(SUCCEEDED(g_ctx->Map(s, 0, D3D11_MAP_READ, 0, &m)))
		{
			for(uint32_t y = 0; y < h; ++y)
				for(uint32_t x = 0; x < w; ++x)
					n += ((const uint8_t*)m.pData)[y * m.RowPitch + x * 4 + 3] != 0;
			g_ctx->Unmap(s, 0);
		}
		s->Release();
		return n;
	}
	~Target()
	{
		if(dsv)
			dsv->Release();
		if(ds)
			ds->Release();
		if(rtv)
			rtv->Release();
		if(rt)
			rt->Release();
	}
};

// A camera looking down at (cx, cy, 0) from 120 units south and 160 up (D3D's left-handed look-at, perspective 45 degrees).
UnitsCamera LookAt(float cx, float cy)
{
	UnitsCamera c;
	const float eye[3] = {cx, cy - 120, 160}, at[3] = {cx, cy, 0}, up[3] = {0, 0, 1};
	float z[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
	float n = std::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
	for(float& v : z)
		v /= n;
	float x[3] = {up[1] * z[2] - up[2] * z[1], up[2] * z[0] - up[0] * z[2], up[0] * z[1] - up[1] * z[0]};
	n = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
	for(float& v : x)
		v /= n;
	const float y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
	const float v[16] = {x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0,
	                     -(x[0] * eye[0] + x[1] * eye[1] + x[2] * eye[2]), -(y[0] * eye[0] + y[1] * eye[1] + y[2] * eye[2]),
	                     -(z[0] * eye[0] + z[1] * eye[1] + z[2] * eye[2]), 1};
	memcpy(c.view, v, sizeof(v));
	const float ys = 1.0f / std::tan(3.14159265f / 8), zn = 1, zf = 2000;
	const float p[16] = {ys, 0, 0, 0, 0, ys, 0, 0, 0, 0, zf / (zf - zn), 1, 0, 0, -zn * zf / (zf - zn), 0};
	memcpy(c.proj, p, sizeof(p));
	c.camWorld[0] = x[0];
	c.camWorld[1] = x[1];
	c.camWorld[2] = x[2];
	c.camWorld[4] = y[0];
	c.camWorld[5] = y[1];
	c.camWorld[6] = y[2];
	c.camWorld[8] = z[0];
	c.camWorld[9] = z[1];
	c.camWorld[10] = z[2];
	c.camWorld[12] = eye[0];
	c.camWorld[13] = eye[1];
	c.camWorld[14] = eye[2];
	c.camWorld[15] = 1;
	c.frameId = 41;
	return c;
}

// The warrior (0, 1001) at plot (2, 2) of the 10 x 8 grid: created and visible.
void FeedWarrior(Host& h, uint32_t seq0 = 1)
{
	h.port.units = {Rec(0, 1001, 7, 2 * 10 + 2, 2, cu::kFlagVisible, 0, 100, seq0)};
	h.u->feed(BatchOf({Ev(seq0, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 22)}));
}

// Issue 4463 (note 12.1 [L]: the EXE does not draw another team's unit on a plot the active team does not see): the
// fog filter of the unit host over the plot states the terrain scene hands in (SceneUnits::setPlotStates). Player 0 is
// the active team's (activeTeam 0 in every row), player 1 another team's; both have a warrior on plot (2, 2).
bool FogFilter()
{
	const size_t said0 = LogCount("units: no plot states from the fog scene");   // the calling case's own frames said it
	Host h(TestEnv(false));
	h.port.players = {Player(0, 3, 5, 0), Player(1, 3, 5, 0)};   // teams 0 and 1, activeTeam 0
	h.u->poll();
	h.probe->waitPrecheck();
	EXPECT(h.probe->precheckPassed());
	h.port.units = {Rec(0, 1001, 7, 22, 2, cu::kFlagVisible, 0, 100, 1), Rec(1, 1101, 7, 22, 2, cu::kFlagVisible, 0, 100, 2)};
	h.u->feed(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 22), Ev(2, SrvFeed::kUnitCreated, 1, 1101, 7, 0, 22)}));
	h.u->setDevice(g_dev);
	Target t;
	const c5r::heightfield::HeightGrid g = Grid();
	float cx = 0, cy = 0;
	EXPECT(cu::plotCentre(g, 2, 2, cx, cy).ok());
	const UnitsCamera cam = LookAt(cx, cy);
	char line[4096];
	auto ids = [&]() {
		std::string s;
		for(const cu::UnitInput& u : h.probe->lastBuilt().copies[1].units)
			s += std::to_string(u.player) + ":" + std::to_string(u.id) + " ";
		return s;
	};
	// (1) no states yet (render_fow=0, the tests): no filter, both listed, said once
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	EXPECT(ids() == "0:1001 1:1101 " && h.probe->lastBuilt().listed == 2);
	EXPECT(LogCount("units: no plot states from the fog scene") == said0 + 1);
	h.u->secondLine(line, sizeof(line));
	EXPECT(strstr(line, " fog_hidden=0 fog=none") != nullptr);
	// (2) every plot visible: both listed
	std::vector<uint8_t> st(10 * 8, 0);
	h.u->setPlotStates(st.data(), st.size(), 10, 8, 1);
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	EXPECT(ids() == "0:1001 1:1101 ");
	h.u->secondLine(line, sizeof(line));
	EXPECT(strstr(line, " fog_hidden=0 fog=on") != nullptr);
	// (3) plot (2, 2) seen, not visible: the other team's warrior is not listed, the active team's own is
	st[22] = 1;
	h.u->setPlotStates(st.data(), st.size(), 10, 8, 2);
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	EXPECT(ids() == "0:1001 " && h.probe->lastBuilt().listed == 1 && h.probe->lastBuilt().copies[1].players.size() == 1);
	h.u->secondLine(line, sizeof(line));
	EXPECT(strstr(line, " listed=1 ") != nullptr && strstr(line, " fog_hidden=1 fog=on") != nullptr);
	// unexplored hides it too; the same generation is not copied again (the caller's buffer may change under it)
	st[22] = 2;
	h.u->setPlotStates(st.data(), st.size(), 10, 8, 3);
	st[22] = 0;
	h.u->setPlotStates(st.data(), st.size(), 10, 8, 3);
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	EXPECT(ids() == "0:1001 ");
	h.u->setPlotStates(st.data(), st.size(), 10, 8, 4);   // visible again
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	EXPECT(ids() == "0:1001 1:1101 ");
	// (4) player 1 joins the active team (its row's team 0): listed on a plot it does not see
	st[22] = 1;
	h.u->setPlotStates(st.data(), st.size(), 10, 8, 5);
	SrvFeed::Batch rows = BatchOf({Ev(3, SrvFeed::kPlotState, 0, 0)});
	SrvFeed::PlayerRecord p1 = Player(1, 3, 5, 0);
	p1.team = 0;
	rows.players = {p1};
	h.u->feed(rows);
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	EXPECT(ids() == "0:1001 1:1101 ");
	p1.team = 1;
	rows.players = {p1};
	h.u->feed(rows);
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	EXPECT(ids() == "0:1001 ");
	// negatives: states of another grid, then no states: no filter (today's list), each reason said once, frames counted
	h.u->setPlotStates(st.data(), 5 * 8, 5, 8, 6);
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	EXPECT(ids() == "0:1001 1:1101 " && LogCount("units: the fog's plot states are not of the feed's grid") == 1);
	h.u->secondLine(line, sizeof(line));
	EXPECT(strstr(line, " fog=grid") != nullptr);
	h.u->setPlotStates(nullptr, 0, 0, 0, 7);
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	EXPECT(ids() == "0:1001 1:1101 " && LogCount("units: no plot states from the fog scene") == said0 + 1);
	h.u->totalsLine(line, sizeof(line));
	EXPECT(strstr(line, "| fog hidden_units=3 unfiltered_frames=5 |") != nullptr);
	return true;
}


// Issue 4460: render_test_units_table (TEST ONLY): the scene's table, seeds, player rows and the feed's per-kind counts
// rewritten at the second line when changed (tmp + rename), not rewritten while unchanged; a folder that is gone is LOUD
// once and the scene goes on.
static std::string ReadText(const std::string& p)
{
	std::string s;
	FILE* f = fopen(p.c_str(), "rb");
	if(!f)
		return s;
	char buf[4096];
	size_t n;
	while((n = fread(buf, 1, sizeof(buf), f)) > 0)
		s.append(buf, n);
	fclose(f);
	return s;
}

bool CaseTableFile()
{
	char tmp[MAX_PATH];
	GetTempPathA(MAX_PATH, tmp);
	const std::string dir = std::string(tmp) + "c5_4460_table_" + std::to_string(GetCurrentProcessId());
	CreateDirectoryA(dir.c_str(), nullptr);
	const std::string file = dir + "\\units.tsv";
	UnitsOptions o;
	o.tableFile = c5r::gamefs::fromUtf8(file);
	Host h([](const std::wstring&, Env&, std::string& why) { why = "no database in this case"; return false; }, o);
	FeedWarrior(h);
	char line[1024];
	h.u->secondLine(line, sizeof(line));
	EXPECT(h.probe->waitTestFiles());   // ticket 5054: the TEST files' worker
	const std::string a = ReadText(file);
	EXPECT(a.find("# c5-units-table 1\n") == 0);
	EXPECT(a.find("state gen 0 last_seq 1 units 1 ") != std::string::npos);
	EXPECT(a.find("\nkinds 11:") != std::string::npos);
	EXPECT(a.find("\nP 0 0 1 0 3 5 0 5 0\n") != std::string::npos);
	// flags 8 = visible; seed -1: no frame has ended the drain's group yet (the allocator's frame grouping, 4453)
	EXPECT(a.find("\nU 0 1001 7 22 2 8 0 100 0 0 0 1 -1\n") != std::string::npos);
	EXPECT(GetFileAttributesA((file + ".tmp").c_str()) == INVALID_FILE_ATTRIBUTES);
	// unchanged: not rewritten (the file removed by hand stays absent)
	DeleteFileA(file.c_str());
	h.u->secondLine(line, sizeof(line));
	EXPECT(h.probe->waitTestFiles());
	EXPECT(GetFileAttributesA(file.c_str()) == INVALID_FILE_ATTRIBUTES);
	// a change: rewritten
	h.port.units[0].plot = 23;
	h.port.units[0].seq = 2;
	h.t = 2000;   // a later drain ends the first group: the creation is applied, seed 0
	h.u->feed(BatchOf({Ev(2, SrvFeed::kUnitTeleported, 0, 1001, 23)}));
	h.u->secondLine(line, sizeof(line));
	EXPECT(h.probe->waitTestFiles());
	EXPECT(ReadText(file).find("\nU 0 1001 7 23 2 8 0 100 0 0 0 2 0\n") != std::string::npos);
	// the folder gone: LOUD once, the scene goes on
	DeleteFileA(file.c_str());
	RemoveDirectoryA(dir.c_str());
	h.port.units[0].seq = 3;
	h.u->feed(BatchOf({Ev(3, SrvFeed::kUnitTeleported, 0, 1001, 23)}));
	h.u->secondLine(line, sizeof(line));
	h.probe->waitTestFiles();
	h.port.units[0].seq = 4;
	h.u->feed(BatchOf({Ev(4, SrvFeed::kUnitTeleported, 0, 1001, 23)}));
	h.u->secondLine(line, sizeof(line));
	h.probe->waitTestFiles();
	EXPECT(LogCount("LOUD: units: TEST KEY render_test_units_table: cannot write") == 1);
	return true;
}

bool CasePrecheckPassAndDraw()
{
	Host h(TestEnv(false));
	EXPECT(h.probe && !h.u->ready());
	h.u->poll();
	EXPECT(h.probe->precheckRunning());
	h.probe->waitPrecheck();
	EXPECT(h.probe->precheckPassed() && h.probe->precheckEntries() >= 2);
	EXPECT(LogCount("units: pre-check of generation 1: ") == 1 && LogCount(" 0 failed") == 1);
	std::string why;
	EXPECT(!h.u->failed(why));
	FeedWarrior(h);
	h.u->setDevice(g_dev);
	Target t;
	const c5r::heightfield::HeightGrid g = Grid();
	float cx = 0, cy = 0;
	EXPECT(cu::plotCentre(g, 2, 2, cx, cy).ok());
	const UnitsCamera cam = LookAt(cx, cy);
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	EXPECT(h.probe->passDraws() == 1);
	EXPECT(h.probe->lastBuilt().copies[1].units.size() == 1);
	const size_t px = t.covered();
	EXPECT(px > 50);
	EXPECT(h.u->ready());
	EXPECT(h.probe->core().counters().drawnUnits == 1);
	// the camera turned away: the unit culled, nothing drawn, still ready
	t.clear();
	UnitsCamera away = LookAt(cx + 2000, cy);
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, away, &g);
	EXPECT(h.probe->lastBuilt().culled == 1 && t.covered() == 0 && h.u->ready());
	// device released: the drawing renderer goes, the tables stay; a new device draws again
	h.u->setDevice(nullptr);
	EXPECT(!h.u->ready());
	h.u->setDevice(g_dev);
	t.clear();
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	EXPECT(h.probe->passDraws() == 2 && t.covered() == px && h.u->ready());
	char line[4096];
	h.u->totalsLine(line, sizeof(line));
	EXPECT(strstr(line, "units totals: ready=1 precheck=passed") != nullptr);
	return FogFilter();
}


// Issue 4460 (MC ruling 2026-11-16): the stock Assyrian siege tower names a clip no package ships; the scene's pre-check
// passes with one ClipMissingInInstall warning counted in its line (the D125 route stays for real faults: precheck_fail_route).
EnvOpener TestEnvSiege()
{
	return [=](const std::wstring&, Env& out, std::string& why) {
		const c5r::Status s = out.gf.open(c5r::gamefs::fromUtf8(g_game));
		if(!s.ok())
		{
			why = s.str();
			return false;
		}
		std::unique_ptr<c5m::TsvTables> db = TestDb(false);
		c5m::Error e;
		db->add("Units", Tsv({{"ID", "Type", "UnitArtInfo", "UnitArtInfoCulturalVariation", "UnitArtInfoEraVariation"},
		                      {"7", "UNIT_WARRIOR", "ART_DEF_UNIT__WARRIOR", "0", "0"},
		                      {"8", "UNIT_TRIREME", "ART_DEF_UNIT_TRIREME", "0", "0"},
		                      {"9", "UNIT_ASSYRIAN_SIEGE_TOWER", "ART_DEF_UNIT_U_ASSYRIAN_SIEGE_TOWER", "0", "0"}}),
		        &e);
		db->add("ArtDefine_UnitInfos", Tsv({{"Type", "Formation", "DamageStates"}, {"ART_DEF_UNIT__WARRIOR", "Warrior", "1"},
		                                    {"ART_DEF_UNIT_TRIREME", "\\N", "3"}, {"ART_DEF_UNIT_U_ASSYRIAN_SIEGE_TOWER", "\\N", "3"}}),
		        &e);
		db->add("ArtDefine_UnitInfoMemberInfos", Tsv({{"UnitInfoType", "UnitMemberInfoType", "NumMembers"},
		                                              {"ART_DEF_UNIT__WARRIOR", "ART_DEF_UNIT_MEMBER_WARRIOR_V2", "3"},
		                                              {"ART_DEF_UNIT__WARRIOR", "ART_DEF_UNIT_MEMBER_WARRIOR", "6"},
		                                              {"ART_DEF_UNIT__WARRIOR", "ART_DEF_UNIT_MEMBER_WARRIOR_V4", "3"},
		                                              {"ART_DEF_UNIT_TRIREME", "ART_DEF_UNIT_MEMBER_TRIREME", "1"},
		                                              {"ART_DEF_UNIT_U_ASSYRIAN_SIEGE_TOWER", "ART_DEF_UNIT_MEMBER_U_ASSYRIAN_SIEGE_TOWER", "1"}}),
		        &e);
		db->add("ArtDefine_UnitMemberInfos",
		        Tsv({{"Type", "Scale", "ZOffset", "Domain", "Model", "MaterialTypeTag"},
		             {"ART_DEF_UNIT_MEMBER_WARRIOR_V2", "0.14000000059604645", "\\N", "", "Assets/Units/Warrior/Warrior_V2.fxsxml", "CLOTH"},
		             {"ART_DEF_UNIT_MEMBER_WARRIOR", "0.14000000059604645", "\\N", "", "Assets/Units/Warrior/Warrior.fxsxml", "CLOTH"},
		             {"ART_DEF_UNIT_MEMBER_WARRIOR_V4", "0.14000000059604645", "\\N", "", "Assets/Units/Warrior/Warrior_V4.fxsxml", "CLOTH"},
		             {"ART_DEF_UNIT_MEMBER_TRIREME", "0.07000000029802322", "\\N", "Sea", "Assets/Units/Trireme/Trireme.fxsxml", "WOOD"},
		             {"ART_DEF_UNIT_MEMBER_U_ASSYRIAN_SIEGE_TOWER", "0.10000000149011612", "\\N", "", "U_Assyrian_Siege_Tower.fxsxml", "WOOD"}}),
		        &e);
		out.db = std::move(db);
		out.exe = c5r::gamefs::fromUtf8(g_exe);
		out.install = c5r::gamefs::fromUtf8(g_game);
		return true;
	};
}

bool CasePrecheckClipMissingInInstall()
{
	Host h(TestEnvSiege());
	h.u->poll();
	h.probe->waitPrecheck();
	std::string why;
	EXPECT(!h.u->failed(why) && h.probe->precheckPassed());
	EXPECT(LogCount("art rows, 0 failed, 1 warnings (ClipMissingInInstall 1)") == 1);
	EXPECT(LogCount("units: pre-check warning (the unit is drawn): ART_DEF_UNIT_U_ASSYRIAN_SIEGE_TOWER: ClipMissingInInstall: ") == 1);
	EXPECT(LogCount("LOUD: units: PrecheckFailed") == 0);
	return true;
}

bool CasePrecheckFailRoute()
{
	Host h(TestEnv(true));
	h.u->poll();
	h.probe->waitPrecheck();
	std::string why;
	EXPECT(h.u->failed(why) && !h.probe->precheckPassed());
	EXPECT(why.find("pre-check: 1 of ") != std::string::npos && why.find("art rows failed: UNIT_NOART") != std::string::npos);
	EXPECT(LogCount("LOUD: units: PrecheckFailed: pre-check: 1 of ") == 1);
	EXPECT(h.probe->core().counters().byStatus[(int)Status::kPrecheckFailed] == 1);
	// D125's route: UnitsFatalStep -> HostConfig::fatal(5, why) once
	static int s_code = -1, s_calls = 0;
	static std::string s_why;
	s_code = -1;
	s_calls = 0;
	srvrender::Scene scene;
	scene.setUnits(std::move(h.u));
	EXPECT(!scene.unitsReady());
	bool done = false;
	auto fatal = [](int code, const char* w) {
		s_code = code;
		s_why = w;
		s_calls++;
	};
	srvrender::UnitsFatalStep(scene, fatal, done);
	srvrender::UnitsFatalStep(scene, fatal, done);
	EXPECT(done && s_calls == 1 && s_code == 5 && s_why.find("art rows failed") != std::string::npos);
	// no draws after a failure
	FakePort& port = h.port;
	(void)port;
	Target t;
	const c5r::heightfield::HeightGrid g = Grid();
	scene.units()->setDevice(g_dev);
	scene.units()->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, LookAt(100, 100), &g);
	EXPECT(t.covered() == 0);
	// no fatal hook: LOUD only
	bool done2 = false;
	srvrender::UnitsFatalStep(scene, nullptr, done2);
	EXPECT(done2 && s_calls == 1);
	return true;
}

bool CaseDatabaseUnreadable()
{
	Host h(TestEnv(false, false, true));
	h.u->poll();
	std::string why;
	EXPECT(h.u->failed(why) && why.find("cannot be opened: test: the copy is unreadable") != std::string::npos);
	EXPECT(LogCount("LOUD: units: PrecheckFailed: pre-check: the unit database test.db cannot be opened") == 1);
	EXPECT(!h.probe->precheckRunning());
	// the art tables missing (the module's tables): PrecheckFailed too
	Host h2(TestEnv(false, true));
	h2.u->poll();
	EXPECT(h2.u->failed(why) && why.find("pre-check: the art tables: ") != std::string::npos);
	return true;
}

bool CaseUnitRefused()
{
	Host h(TestEnv(false));
	h.u->poll();
	h.probe->waitPrecheck();
	EXPECT(h.probe->precheckPassed());
	// the feed's grid (10 x 8) is larger than the height field (4 x 8): the plot is outside it -> PlacementFailed
	h.port.units = {Rec(0, 1001, 7, 2 * 10 + 8, 2, cu::kFlagVisible, 0, 100, 1)};
	h.u->feed(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7)}));
	h.u->setDevice(g_dev);
	Target t;
	const c5r::heightfield::HeightGrid g = Grid(4, 8);
	UnitsCamera cam = LookAt(100, 100);
	cam.view[0] = NAN;   // no cull (a camera that is not finite): the unit reaches prepare
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	EXPECT(h.probe->core().counters().byStatus[(int)Status::kUnitRefused] == 2);
	EXPECT(LogCount("LOUD: units: UnitRefused: unit (0, 1001) UNIT_WARRIOR: PlacementFailed") == 1);
	EXPECT(h.probe->passDraws() == 0 && t.covered() == 0);
	return true;
}

bool CaseFrameFailed()
{
	Host h(TestEnv(false));
	h.u->poll();
	h.probe->waitPrecheck();
	FeedWarrior(h);
	h.u->setDevice(g_dev);
	Target t;
	const c5r::heightfield::HeightGrid g = Grid();
	UnitsCamera cam = LookAt(100, 100);
	memset(cam.view, 0, sizeof(cam.view));   // finite but not invertible, camWorld zero: the pass camera is refused
	memset(cam.camWorld, 0, sizeof(cam.camWorld));
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	EXPECT(h.probe->core().counters().byStatus[(int)Status::kFrameFailed] == 1);
	EXPECT(LogCount("LOUD: units: FrameFailed: frame: the frame's camera is not finite or its view not invertible") == 1);
	EXPECT(!h.u->ready() && h.probe->passDraws() == 0);
	return true;
}

// Ticket 0704: the target's bytes (R8G8B8A8, rows packed)
std::vector<uint8_t> TargetBytes(Target& t)
{
	std::vector<uint8_t> out;
	D3D11_TEXTURE2D_DESC d;
	t.rt->GetDesc(&d);
	d.Usage = D3D11_USAGE_STAGING;
	d.BindFlags = 0;
	d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ID3D11Texture2D* s = nullptr;
	if(FAILED(g_dev->CreateTexture2D(&d, nullptr, &s)))
		return out;
	g_ctx->CopyResource(s, t.rt);
	D3D11_MAPPED_SUBRESOURCE m;
	if(SUCCEEDED(g_ctx->Map(s, 0, D3D11_MAP_READ, 0, &m)))
	{
		for(uint32_t y = 0; y < t.h; ++y)
			out.insert(out.end(), (const uint8_t*)m.pData + y * m.RowPitch, (const uint8_t*)m.pData + y * m.RowPitch + t.w * 4);
		g_ctx->Unmap(s, 0);
	}
	s->Release();
	return out;
}

// Ticket 0704: draws the warrior's frame until the host drew it (at most `ms`); the frames it was not ready in
int DrawUntilDrawn(Host& h, Target& t, const UnitsCamera& cam, const c5r::heightfield::HeightGrid& g, uint64_t passDraws0, DWORD ms = 20000)
{
	const ULONGLONG end = GetTickCount64() + ms;
	int notDrawn = 0;
	for(;;)
	{
		t.clear();
		h.u->poll();
		h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
		if(h.probe->passDraws() > passDraws0 || GetTickCount64() > end)
			return notDrawn;
		notDrawn++;
		Sleep(2);
	}
}

// Ticket 0704: UnitsOptions::openAsync (server.exe's factory): the drawing renderer opens on a worker thread with the table's art
// rows loaded and uploaded there; the frames before it is taken are not drawn and not ready; the first drawn frame equals the
// render-thread open's byte for byte; a device change during the worker's open drops its result and opens again; a worker whose
// environment does not open takes D125's route; a host destroyed during the worker's open ends cleanly.
bool CaseOpenAsync()
{
	const c5r::heightfield::HeightGrid g = Grid();
	float cx = 0, cy = 0;
	EXPECT(cu::plotCentre(g, 2, 2, cx, cy).ok());
	const UnitsCamera cam = LookAt(cx, cy);
	// the reference: the open on the render thread (the default)
	std::vector<uint8_t> ref;
	{
		Host h0(TestEnv(false));
		h0.u->poll();
		h0.probe->waitPrecheck();
		FeedWarrior(h0);
		h0.u->setDevice(g_dev);
		Target t;
		h0.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
		EXPECT(h0.probe->passDraws() == 1 && t.covered() > 50 && h0.u->ready());
		ref = TargetBytes(t);
		EXPECT(LogCount("opens on a worker thread") == 0);
	}
	UnitsOptions o;
	o.openAsync = true;
	{
		const size_t open0 = LogCount("units: the drawing renderer is open on the render device");
		Host h(TestEnv(false), o);
		h.u->poll();
		h.probe->waitPrecheck();
		EXPECT(h.probe->precheckPassed());
		FeedWarrior(h);
		h.u->setDevice(g_dev);
		Target t;
		h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);   // starts the worker: not drawn, not ready
		EXPECT(h.probe->passDraws() == 0 && t.covered() == 0 && !h.u->ready() && h.probe->prepares() == 0);
		EXPECT(LogCount("units: the drawing renderer opens on a worker thread (ticket 0704): 1 art keys of the table's 1 units") == 1);
		DrawUntilDrawn(h, t, cam, g, 0);
		EXPECT(h.probe->passDraws() == 1 && h.u->ready());
		EXPECT(LogCount("units: the drawing renderer was opened on a worker thread (ticket 0704): ") == 1);
		// the warrior's row was loaded by the worker, in the pre-check's environment: the first prepare found it cached
		EXPECT(LogCount("1 art keys loaded (0 failed, 0 skipped)") == 1 && LogCount("; 1 art rows cached") == 1);
		EXPECT(LogCount("(ticket 0704): environment the pre-check's ") == 1);
		EXPECT(LogCount("units: the drawing renderer is open on the render device") == open0 + 1);
		EXPECT(TargetBytes(t) == ref);
		// later frames as the render-thread open's
		h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
		EXPECT(h.probe->passDraws() == 2 && h.u->ready());
		// device released and given again: the render thread's next frame starts a new worker open
		h.u->setDevice(nullptr);
		EXPECT(!h.u->ready());
		h.u->setDevice(g_dev);
		DrawUntilDrawn(h, t, cam, g, 2);
		EXPECT(h.probe->passDraws() == 3 && h.u->ready() && TargetBytes(t) == ref);
		EXPECT(LogCount("units: the drawing renderer was opened on a worker thread (ticket 0704): ") == 2);
		EXPECT(LogCount("(ticket 0704): environment the pre-check's ") == 1);   // the second opened its own
	}
	{
		// a device change while the worker opens: its result is dropped (cancelled: its rows skipped), the open starts again
		Host h(TestEnv(false), o);
		h.u->poll();
		h.probe->waitPrecheck();
		FeedWarrior(h);
		h.u->setDevice(g_dev);
		Target t;
		h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);   // the worker starts
		h.u->setDevice(nullptr);
		h.u->setDevice(g_dev);
		const size_t stale0 = LogCount("was for another device or database: dropped, opened again");
		DrawUntilDrawn(h, t, cam, g, 0);
		EXPECT(LogCount("was for another device or database: dropped, opened again") == stale0 + 1);
		EXPECT(h.probe->passDraws() == 1 && h.u->ready() && TargetBytes(t) == ref);
	}
	{
		// the worker's environment does not open: the first worker takes the pre-check's (two opener calls: the render thread's,
		// the pre-check's); after a device change the second worker opens its own, the third call, which fails
		std::shared_ptr<std::atomic<int>> calls = std::make_shared<std::atomic<int>>(0);
		EnvOpener good = TestEnv(false);
		EnvOpener third = [calls, good](const std::wstring& p, Env& out, std::string& why) {
			if(++*calls == 3)
			{
				why = "test: the worker's copy is unreadable";
				return false;
			}
			return good(p, out, why);
		};
		Host h(third, o);
		h.u->poll();
		h.probe->waitPrecheck();
		EXPECT(h.probe->precheckPassed());
		FeedWarrior(h);
		h.u->setDevice(g_dev);
		Target t;
		DrawUntilDrawn(h, t, cam, g, 0);
		EXPECT(h.probe->passDraws() == 1 && calls->load() == 2);
		h.u->setDevice(nullptr);
		h.u->setDevice(g_dev);
		t.clear();
		const ULONGLONG end = GetTickCount64() + 20000;
		std::string why;
		while(!h.u->failed(why) && GetTickCount64() < end)
		{
			h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
			Sleep(2);
		}
		EXPECT(h.u->failed(why) && why.find("environment did not open on the drawing renderer's worker thread: test: the worker's copy is "
		                                    "unreadable") != std::string::npos);
		EXPECT(!h.u->ready() && h.probe->passDraws() == 1 && t.covered() == 0 && calls->load() == 3);
	}
	{
		// destroyed while the worker opens: the destructor joins it (no leak or fault under ASan)
		Host h(TestEnv(false), o);
		h.u->poll();
		h.probe->waitPrecheck();
		FeedWarrior(h);
		h.u->setDevice(g_dev);
		Target t;
		h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
		EXPECT(h.probe->passDraws() == 0);
	}
	return true;
}

bool CaseDump()
{
	char tmp[MAX_PATH];
	GetTempPathA(MAX_PATH, tmp);
	const std::string dir = std::string(tmp) + "c5_4454_dump_" + std::to_string(GetCurrentProcessId());
	CreateDirectoryA(dir.c_str(), nullptr);
	UnitsOptions o;
	o.dumpDir = c5r::gamefs::fromUtf8(dir);
	o.dumpMax = 2;
	Host h(TestEnv(false), o);
	h.u->poll();
	h.probe->waitPrecheck();
	FeedWarrior(h);
	h.u->setDevice(g_dev);
	Target t;
	const c5r::heightfield::HeightGrid g = Grid();
	float cx, cy;
	cu::plotCentre(g, 2, 2, cx, cy);
	UnitsCamera cam = LookAt(cx, cy);
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	cam.frameId = 42;
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);   // the same scene: no second file
	EXPECT(h.probe->dumpsWritten() == 1);
	EXPECT(h.probe->waitTestFiles());   // ticket 5054: the TEST files' worker
	cu::UnitScene back;
	EXPECT(cu::readSceneFile(c5r::gamefs::fromUtf8(dir + "\\frame_41.c5units"), back).ok());
	const cu::UnitScene& s = h.probe->lastBuilt().copies[1];
	EXPECT(back.units.size() == 1 && back.units[0].id == 1001 && back.units[0].formation_seed == s.units[0].formation_seed &&
	       back.players.size() == 1 && back.players[0].colour == "PLAYERCOLOR_POLYNESIA" && back.water_level == s.water_level);
	// two more changes: the second is past dumpMax 2 (LOUD once)
	h.port.units[0].facing = 3;
	h.port.units[0].seq = 2;
	h.u->feed(BatchOf({Ev(2, SrvFeed::kUnitTeleported, 0, 1001, 22)}));
	cam.frameId = 43;
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	h.port.units[0].facing = 4;
	h.port.units[0].seq = 3;
	h.u->feed(BatchOf({Ev(3, SrvFeed::kUnitTeleported, 0, 1001, 22)}));
	cam.frameId = 44;
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	EXPECT(h.probe->dumpsWritten() == 2 && LogCount("render_test_units_dump: 2 files written, no more") == 1);
	EXPECT(h.probe->waitTestFiles());
	DeleteFileA((dir + "\\frame_41.c5units").c_str());
	DeleteFileA((dir + "\\frame_43.c5units").c_str());
	RemoveDirectoryA(dir.c_str());
	return true;
}

// ---------------------------------------------------------------------------------------- issue 4455: the reflection
struct ReflTarget
{
	ID3D11Texture2D* rt = nullptr;
	ID3D11RenderTargetView* rtv = nullptr;
	ID3D11Texture2D* ds = nullptr;
	ID3D11DepthStencilView* dsv = nullptr;
	ReflTarget()
	{
		D3D11_TEXTURE2D_DESC d = {};
		d.Width = d.Height = srvrender::WaterScene::kReflectionTargetSize;
		d.MipLevels = d.ArraySize = 1;
		d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		d.SampleDesc.Count = 1;
		d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		g_dev->CreateTexture2D(&d, nullptr, &rt);
		g_dev->CreateRenderTargetView(rt, nullptr, &rtv);
		d.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		g_dev->CreateTexture2D(&d, nullptr, &ds);
		g_dev->CreateDepthStencilView(ds, nullptr, &dsv);
		const float c[4] = {0, 0, 0, 0};
		g_ctx->ClearRenderTargetView(rtv, c);
		g_ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	}
	size_t covered()
	{
		D3D11_TEXTURE2D_DESC d;
		rt->GetDesc(&d);
		d.Usage = D3D11_USAGE_STAGING;
		d.BindFlags = 0;
		d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ID3D11Texture2D* st = nullptr;
		if(FAILED(g_dev->CreateTexture2D(&d, nullptr, &st)))
			return 0;
		g_ctx->CopyResource(st, rt);
		D3D11_MAPPED_SUBRESOURCE m;
		size_t n = 0;
		if(SUCCEEDED(g_ctx->Map(st, 0, D3D11_MAP_READ, 0, &m)))
		{
			for(uint32_t y = 0; y < d.Height; ++y)
				for(uint32_t x = 0; x < d.Width; ++x)
					n += ((const uint8_t*)m.pData)[y * m.RowPitch + x * 4 + 3] != 0;
			g_ctx->Unmap(st, 0);
		}
		st->Release();
		return n;
	}
	~ReflTarget()
	{
		if(dsv)
			dsv->Release();
		if(ds)
			ds->Release();
		if(rtv)
			rtv->Release();
		if(rt)
			rt->Release();
	}
};

bool CaseReflectionPass()
{
	UnitsOptions o;
	o.passes = true;
	Host h(TestEnv(false), o);
	h.u->poll();
	h.probe->waitPrecheck();
	FeedWarrior(h);
	h.u->setDevice(g_dev);
	const c5r::heightfield::HeightGrid g = Grid();
	float cx = 0, cy = 0;
	cu::plotCentre(g, 2, 2, cx, cy);
	const UnitsCamera cam = LookAt(cx, cy);
	// an orthographic ShadowMatrix1 over the unit's plot: clip = ((x - cx) / 200, (y - cy) / 200, z / 1000 + 0.5, 1)
	const float sm1[16] = {1.0f / 200, 0, 0, 0, 0, 1.0f / 200, 0, 0, 0, 0, 1.0f / 1000, 0, -cx / 200, -cy / 200, 0.5f, 1};
	Target t;
	ReflTarget r;
	h.u->beginFrame(cam, &g);
	h.u->drawReflection(g_ctx, r.rtv, r.dsv, srvrender::WaterScene::kReflectionTargetSize, sm1);
	EXPECT(h.probe->reflectionDraws() == 1 && r.covered() > 50);
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	EXPECT(h.probe->passDraws() == 1 && t.covered() > 50);
	EXPECT(h.probe->prepares() == 1);   // one prepare for both passes
	// without a begun frame: nothing (the scene pass consumed it)
	h.u->drawReflection(g_ctx, r.rtv, r.dsv, srvrender::WaterScene::kReflectionTargetSize, sm1);
	EXPECT(h.probe->reflectionDraws() == 1);
	// the matrix moved off the unit: begun, drawn, nothing covered in the cleared target
	ReflTarget r2;
	float moved[16];
	memcpy(moved, sm1, sizeof(moved));
	moved[12] += 50;
	h.u->beginFrame(cam, &g);
	h.u->drawReflection(g_ctx, r2.rtv, r2.dsv, srvrender::WaterScene::kReflectionTargetSize, moved);
	EXPECT(h.probe->reflectionDraws() == 2 && r2.covered() == 0);
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	EXPECT(h.probe->prepares() == 2 && LogCount("LOUD") == 0);
	return true;
}

// ticket 4785: the shadow-mask pass (drawMask) into a ShadowRaw-like float target with an orthographic ShadowMatrix0 over
// the unit's plot: drawn, r = 1 under the unit (the EXE's mask PS writes (1, 1, 1, alpha)); no frame begun or no matrix:
// nothing drawn, false
struct MaskTarget
{
	ID3D11Texture2D* rt = nullptr;
	ID3D11RenderTargetView* rtv = nullptr;
	static const uint32_t kSize = 256;
	MaskTarget()
	{
		D3D11_TEXTURE2D_DESC d = {};
		d.Width = d.Height = kSize;
		d.MipLevels = d.ArraySize = 1;
		d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		d.SampleDesc.Count = 1;
		d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		g_dev->CreateTexture2D(&d, nullptr, &rt);
		g_dev->CreateRenderTargetView(rt, nullptr, &rtv);
		const float c[4] = {0, 0, 0, 0};
		g_ctx->ClearRenderTargetView(rtv, c);
	}
	size_t ones()   // texels whose r is exactly 1 (half 0x3C00)
	{
		D3D11_TEXTURE2D_DESC d;
		rt->GetDesc(&d);
		d.Usage = D3D11_USAGE_STAGING;
		d.BindFlags = 0;
		d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ID3D11Texture2D* st = nullptr;
		if(FAILED(g_dev->CreateTexture2D(&d, nullptr, &st)))
			return 0;
		g_ctx->CopyResource(st, rt);
		D3D11_MAPPED_SUBRESOURCE m;
		size_t n = 0;
		if(SUCCEEDED(g_ctx->Map(st, 0, D3D11_MAP_READ, 0, &m)))
		{
			for(uint32_t y = 0; y < d.Height; ++y)
				for(uint32_t x = 0; x < d.Width; ++x)
				{
					uint16_t r;
					memcpy(&r, (const uint8_t*)m.pData + y * m.RowPitch + x * 8, 2);
					n += r == 0x3C00;
				}
			g_ctx->Unmap(st, 0);
		}
		st->Release();
		return n;
	}
	~MaskTarget()
	{
		if(rtv)
			rtv->Release();
		if(rt)
			rt->Release();
	}
};

bool CaseMaskPass()
{
	UnitsOptions o;
	Host h(TestEnv(false), o);
	h.u->poll();
	h.probe->waitPrecheck();
	FeedWarrior(h);
	h.u->setDevice(g_dev);
	const c5r::heightfield::HeightGrid g = Grid();
	float cx = 0, cy = 0;
	cu::plotCentre(g, 2, 2, cx, cy);
	const UnitsCamera cam = LookAt(cx, cy);
	const float sm0[16] = {1.0f / 200, 0, 0, 0, 0, 1.0f / 200, 0, 0, 0, 0, 1.0f / 1000, 0, -cx / 200, -cy / 200, 0.5f, 1};
	Target t;
	MaskTarget mt;
	EXPECT(!h.u->drawMask(g_ctx, mt.rtv, MaskTarget::kSize, MaskTarget::kSize, sm0));   // no frame begun
	EXPECT(h.probe->maskDraws() == 0 && mt.ones() == 0);
	h.u->beginFrame(cam, &g);
	EXPECT(!h.u->drawMask(g_ctx, mt.rtv, MaskTarget::kSize, MaskTarget::kSize, nullptr));   // no matrix
	EXPECT(h.u->drawMask(g_ctx, mt.rtv, MaskTarget::kSize, MaskTarget::kSize, sm0));
	EXPECT(h.probe->maskDraws() == 1 && mt.ones() > 50);
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);   // the same plans: one prepare for both passes
	EXPECT(h.probe->passDraws() == 1 && t.covered() > 50 && h.probe->prepares() == 1);
	// the matrix moved off the unit: drawn, nothing in the cleared target
	MaskTarget m2;
	float moved[16];
	memcpy(moved, sm0, sizeof(moved));
	moved[12] += 50;
	h.u->beginFrame(cam, &g);
	EXPECT(h.u->drawMask(g_ctx, m2.rtv, MaskTarget::kSize, MaskTarget::kSize, moved) && h.probe->maskDraws() == 2 && m2.ones() == 0);
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	EXPECT(LogCount("LOUD") == 0 && h.u->ready());
	return true;
}

bool CaseReflectionMatrixMissing()
{
	UnitsOptions o;
	o.passes = true;
	Host h(TestEnv(false), o);
	h.u->poll();
	h.probe->waitPrecheck();
	FeedWarrior(h);
	h.u->setDevice(g_dev);
	const c5r::heightfield::HeightGrid g = Grid();
	float cx = 0, cy = 0;
	cu::plotCentre(g, 2, 2, cx, cy);
	const UnitsCamera cam = LookAt(cx, cy);
	Target t;
	ReflTarget r;
	for(int i = 0; i < 2; ++i)
	{
		h.u->beginFrame(cam, &g);
		h.u->drawReflection(g_ctx, r.rtv, r.dsv, srvrender::WaterScene::kReflectionTargetSize, nullptr);
		h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	}
	EXPECT(h.probe->core().counters().byStatus[(int)Status::kReflectionMatrixMissing] == 2);
	EXPECT(LogCount("LOUD: units: ReflectionMatrixMissing: render_units_passes=1 but the frame has no ShadowMatrix1") == 1);
	EXPECT(h.probe->reflectionDraws() == 0 && r.covered() == 0 && h.probe->passDraws() == 2 && h.u->ready());
	return true;
}

// the water scene's t4 (WaterSceneOptions::reflectionTarget), with a stand-in water module that keeps what it is given
struct FakeWater : srvrender::WaterModule
{
	ID3D11ShaderResourceView* t4 = nullptr;
	c5r::Status init(ID3D11Device*, const std::wstring&, const std::wstring&) override { return c5r::Status::Ok(); }
	c5r::Status setHeightField(const c5r::heightfield::HeightGrid&) override { return c5r::Status::Ok(); }
	c5r::Status setMesh(c5r::water::System, const c5r::water::Mesh&) override { return c5r::Status::Ok(); }
	c5r::Status setTexture(c5r::water::Slot slot, ID3D11ShaderResourceView* srv) override
	{
		if(slot == c5r::water::Slot::Reflection)
			t4 = srv;
		return c5r::Status::Ok();
	}
	c5r::Status draw(ID3D11DeviceContext*, ID3D11RenderTargetView*, ID3D11DepthStencilView*, const c5r::water::FrameInputs&,
	                 const c5r::water::Options&, c5r::water::FrameStats*) override
	{
		return c5r::Status::Ok();
	}
	void release() override {}
};

bool CaseWaterReflectionTarget()
{
	// since the merge with 0382 (2026-11-16) t4 is always a render target with its own depth (water-system.md 13.6); the
	// units draw into it through WaterSceneReflectionModels (hook tests in server/tests/render/water/)
	FakeWater* fw = new FakeWater();
	srvrender::WaterScene w{std::unique_ptr<srvrender::WaterModule>(fw)};
	srvrender::WaterSceneOptions wo;
	w.init(g_dev, wo);
	EXPECT(fw->t4 && fw->t4 == w.reflectionSrv());
	ID3D11Resource* res = nullptr;
	fw->t4->GetResource(&res);
	D3D11_TEXTURE2D_DESC d;
	((ID3D11Texture2D*)res)->GetDesc(&d);
	EXPECT(d.Width == srvrender::WaterScene::kReflectionTargetSize && d.Height == d.Width && d.Format == DXGI_FORMAT_R8G8B8A8_UNORM);
	EXPECT(d.Usage == D3D11_USAGE_DEFAULT && (d.BindFlags & D3D11_BIND_RENDER_TARGET));
	res->Release();
	w.releaseDevice();
	return true;
}

// ------------------------------------------------------------------------------------------------------- the switch
int g_made = 0;
std::unique_ptr<SceneUnits> CountingFactory(const UnitsOptions&)
{
	g_made++;
	FakePort* p = new FakePort();   // leaked on purpose (tests): the scene outlives the case
	return MakeSceneUnitsWith(UnitsOptions(), *p, TestEnv(false), Log, ClockFn());
}

bool CaseSwitch()
{
	auto parse = [](std::map<std::string, std::string> kv, std::string& why) {
		srvrender::HostConfig c;
		return srvrender::ParseHostConfig(
		    [&](const char* k) {
			    auto it = kv.find(k);
			    return it == kv.end() ? std::string() : it->second;
		    },
		    "C:\\game", "C:\\run", c, why);
	};
	std::string why;
	EXPECT(parse({}, why));
	EXPECT(!parse({{"render_units", "2"}}, why) && why == "render_units=2: 0 or 1");
	EXPECT(!parse({{"render_units", "1"}, {"render_test_units_table", "C:\\no\\such\\4460\\t.tsv"}}, why) &&
	       why == "render_test_units_table=C:\\no\\such\\4460\\t.tsv: a file in an existing folder");   // issue 4460
	EXPECT(!parse({{"render_units", "1"}, {"render_test_units_table", "t.tsv"}}, why) &&
	       why == "render_test_units_table=t.tsv: a file in an existing folder");
	EXPECT(!parse({{"render_units_passes", "1"}}, why) && why == "render_units_passes=1: 1 needs render_units=1");
	EXPECT(!parse({{"render_units_passes", "x"}, {"render_units", "1"}}, why) && why == "render_units_passes=x: 0 or 1");
	EXPECT(!parse({{"render_units", "1"}, {"render_test_units_dump", "C:\\no\\such\\folder\\4454"}}, why) &&
	       why == "render_test_units_dump=C:\\no\\such\\folder\\4454: an existing folder");
	EXPECT(!parse({{"render_units", "1"}, {"render_test_units_dump_max", "0"}}, why) && why == "render_test_units_dump_max=0: 1..1000000");
	EXPECT(!parse({{"render_units", "1"}, {"render_test_units_db", "C:\\no\\such\\4454.db"}}, why) &&
	       why == "render_test_units_db=C:\\no\\such\\4454.db: an existing file");
	EXPECT(parse({{"render_units", "1"}, {"render_units_passes", "1"}}, why));
	// a program without the unit scene refuses render_units=1 by name
	RegisterSceneUnitsFactory(nullptr);
	const bool refused = !parse({{"render_units", "1"}}, why) && why == "render_units=1: this program does not link the unit scene (server/src/units/)";
	srvrender::Scene s0;
	std::string w2;
	const bool cfgRefused = !s0.configureUnits(true, UnitsOptions(), w2) && w2.find("does not link the unit scene") != std::string::npos;
	// off = no unit scene at all: no factory call, no feed read, no draw
	RegisterSceneUnitsFactory(&CountingFactory);
	srvrender::Scene s;
	EXPECT(refused && cfgRefused);
	EXPECT(s.configureUnits(false, UnitsOptions(), why) && !s.units() && g_made == 0);
	s.unitFeed(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1, 7)}));
	EXPECT(!s.unitsReady() && !SrvFeed::UnitsEnabled());   // only HostStart turns the DLL side's unit writers on
	EXPECT(s.configureUnits(true, UnitsOptions(), why) && s.units() && g_made == 1);
	EXPECT(s.configureUnits(true, UnitsOptions(), why) && g_made == 1);   // made once
	EXPECT(s.configureUnits(false, UnitsOptions(), why) && !s.units());
	return true;
}

// ------------------------------------------------------------------------------------------------------- the oracle
std::string ReadAll(const std::string& p)
{
	std::string s;
	FILE* f = fopen(p.c_str(), "rb");
	if(!f)
		return s;
	char b[65536];
	size_t n;
	while((n = fread(b, 1, sizeof(b), f)) > 0)
		s.append(b, n);
	fclose(f);
	return s;
}

int Oracle(int argc, char** argv)
{
	std::string scenes, db, hf, out;
	for(int i = 2; i + 1 < argc; i += 2)
	{
		const std::string k = argv[i], v = argv[i + 1];
		if(k == "--scenes")
			scenes = v;
		else if(k == "--db")
			db = v;
		else if(k == "--heightfield")
			hf = v;
		else if(k == "--out")
			out = v;
		else
		{
			printf("oracle: unknown argument %s\n", k.c_str());
			return 2;
		}
	}
	if(scenes.empty() || db.empty() || hf.empty() || out.empty())
	{
		printf("oracle: needs --scenes --db --heightfield --out\n");
		return 2;
	}
	c5m::SqliteTables tables;
	c5m::Error e;
	if(!c5m::SqliteTables::available() || !tables.open(db, &e))
	{
		printf("oracle: database %s: %s\n", db.c_str(), e.what.c_str());
		return 3;
	}
	Lookups look;
	std::string why;
	if(!look.load(tables, why))
	{
		printf("oracle: lookups: %s\n", why.c_str());
		return 3;
	}
	std::map<std::string, int32_t> rev[6];   // Type -> ID: units, colours, civs, eras, artStyles, minorCivs (4460)
	const std::map<int32_t, std::string>* maps[6] = {&look.units, &look.colours, &look.civs, &look.eras, &look.artStyles,
	                                                 &look.minorCivs};
	for(int i = 0; i < 6; ++i)
		for(const auto& kv : *maps[i])
			rev[i][kv.second] = kv.first;
	// minor civilizations: the player record carries the MinorCivilizations index in `civilization` (issue 4460, A1) and
	// its ArtStyleTypes index in `artStyle`, as the DLL's GetMinorCivType / GetArtStyleType give them
	std::map<std::string, std::string> minorStyle;
	{
		std::vector<c5m::DbRow> rows;
		if(tables.rows("MinorCivilizations", {"Type", "ArtStyleType"}, rows, &e))
			for(const c5m::DbRow& r : rows)
				if(r.size() == 2 && !r[0].null && !r[1].null)
					minorStyle[r[0].text] = r[1].text;
	}
	const std::string hfb = ReadAll(hf);
	c5r::heightfield::HeightGrid g;
	const c5r::Status hs = c5r::heightfield::readTap((const uint8_t*)hfb.data(), hfb.size(), g);
	if(!hs.ok())
	{
		printf("oracle: height field %s: %s\n", hf.c_str(), hs.str().c_str());
		return 3;
	}
	CreateDirectoryA(out.c_str(), nullptr);
	CreateDirectoryA((out + "\\scene").c_str(), nullptr);
	CreateDirectoryA((out + "\\merged").c_str(), nullptr);
	WIN32_FIND_DATAA fd;
	HANDLE fh = FindFirstFileA((scenes + "\\*.c5units").c_str(), &fd);
	if(fh == INVALID_HANDLE_VALUE)
	{
		printf("oracle: no .c5units in %s\n", scenes.c_str());
		return 3;
	}
	std::vector<std::string> names;
	do
		names.push_back(fd.cFileName);
	while(FindNextFileA(fh, &fd));
	FindClose(fh);
	std::sort(names.begin(), names.end());
	int files = 0, unitsAll = 0, unitsEqual = 0, differ = 0;
	std::map<std::string, int> diffKinds;
	for(const std::string& name : names)
	{
		cu::UnitScene ref;
		const cu::SceneFileStatus rs = cu::readSceneFile(c5r::gamefs::fromUtf8(scenes + "\\" + name), ref);
		if(!rs.ok())
		{
			printf("oracle: %s refused: %s\n", name.c_str(), rs.str().c_str());
			return 1;
		}
		// the synthetic feed of this frame
		FakePort port;
		std::vector<std::string> bad;
		for(const cu::PlayerInput& p : ref.players)
		{
			const bool minor = minorStyle.count(p.art_style) != 0;
			const std::string style = minor ? minorStyle[p.art_style] : p.art_style;
			if(!rev[1].count(p.colour) || !rev[3].count(p.era) ||
			   (minor ? !rev[4].count(style) || !rev[5].count(p.art_style) : !rev[2].count(p.art_style)))
				bad.push_back("player " + std::to_string(p.player) + " has a Type the database lacks");
			port.players.push_back(Player(p.player, rev[1][p.colour], minor ? rev[5][p.art_style] : rev[2][p.art_style], rev[3][p.era],
			                              minor ? rev[4][style] : -1, minor ? 1 : 0));
		}
		const uint32_t W = g.mapW, H = g.mapH - 4;
		uint32_t maxSeed = 0;
		std::map<uint32_t, const cu::UnitInput*> bySeed;
		for(const cu::UnitInput& u : ref.units)
		{
			maxSeed = (std::max)(maxSeed, u.formation_seed);
			if(bySeed.count(u.formation_seed))
				bad.push_back("two units with seed " + std::to_string(u.formation_seed));
			bySeed[u.formation_seed] = &u;
			SrvFeed::UnitRecord r = Rec(u.player, u.id, rev[0].count(u.unit_type) ? rev[0][u.unit_type] : -1,
			                            u.plot_y * (int)W + u.plot_x, u.facing, (uint8_t)(u.flags | cu::kFlagVisible), u.damage, u.max_hp, 0);
			port.units.push_back(r);
		}
		// creations in seed order: index s goes to the frame's unit of seed s, else to a filler of player 63 (never listed)
		std::vector<SrvFeed::Event> ev;
		uint32_t seq = 0;
		for(uint32_t s = 0; s <= maxSeed; ++s)
		{
			auto it = bySeed.find(s);
			if(it != bySeed.end())
				ev.push_back(Ev(++seq, SrvFeed::kUnitCreated, it->second->player, it->second->id, 0));
			else
			{
				ev.push_back(Ev(++seq, SrvFeed::kUnitCreated, 63, 900000 + (int)s, 0));
				port.units.push_back(Rec(63, 900000 + (int)s, rev[0].begin()->second, 0, 0, 0, 0, 100, 0));
			}
		}
		for(SrvFeed::UnitRecord& r : port.units)
			r.seq = seq;
		std::vector<std::string> logBefore = g_log;
		UnitsCore core(port, Log);
		core.setDatabase(look, nullptr);
		core.onBatch(BatchOf(ev, W, H), 0);
		BuiltFrame f;
		core.build(nullptr, &g, 0, f);
		const cu::UnitScene& s = f.copies[1];
		// field by field (documented exceptions: art_row "" (the module resolves it; 4355's P10 equal 231/231), flags bit
		// visible (set by the scene: only visible units are listed), figures (none: the controller is 0374's))
		std::map<std::pair<int, int>, const cu::UnitInput*> mine;
		for(const cu::UnitInput& u : s.units)
			mine[{u.player, u.id}] = &u;
		auto d = [&](const std::string& k) {
			diffKinds[k]++;
			differ++;
		};
		if(std::fabs(s.water_level - ref.water_level) > 1e-5f)
			d("water_level");
		if(s.players.size() != ref.players.size())
			d("players count");
		for(size_t i = 0; i < (std::min)(s.players.size(), ref.players.size()); ++i)
		{
			const cu::PlayerInput& a = s.players[i];
			const cu::PlayerInput& b = ref.players[i];
			if(a.player != b.player || a.colour != b.colour || a.era != b.era)
				d("player colour/era");
			if(a.art_style != b.art_style)
				d("player art_style " + b.art_style + " -> " + a.art_style);
		}
		for(const cu::UnitInput& b : ref.units)
		{
			unitsAll++;
			auto it = mine.find({b.player, b.id});
			if(it == mine.end())
			{
				d("unit missing");
				continue;
			}
			const cu::UnitInput& a = *it->second;
			bool eq = true;
			auto chk = [&](bool same, const char* k) {
				if(!same)
				{
					d(k);
					eq = false;
				}
			};
			chk(a.unit_type == b.unit_type, "unit_type");
			chk(a.plot_x == b.plot_x && a.plot_y == b.plot_y, "plot");
			chk(a.has_root == b.has_root, "has_root");
			chk(a.facing == b.facing, "facing");
			chk(a.formation_seed == b.formation_seed, "seed");
			chk(a.damage == b.damage && a.max_hp == b.max_hp && a.alive == b.alive, "damage/max_hp/alive");
			chk((a.flags & ~cu::kFlagVisible) == (b.flags & ~cu::kFlagVisible), "flags");
			chk(a.fade_from == b.fade_from && a.fade_to == b.fade_to && a.fade == b.fade, "fade");
			unitsEqual += eq;
		}
		if(s.units.size() != ref.units.size())
			d("unit count");
		std::string text;
		cu::SceneFileStatus ws = cu::writeScene(s, text);
		if(!ws.ok())
		{
			printf("oracle: %s: the scene's writer refused: %s\n", name.c_str(), ws.str().c_str());
			return 1;
		}
		FILE* o = fopen((out + "\\scene\\" + name).c_str(), "wb");
		if(o)
		{
			fwrite(text.data(), 1, text.size(), o);
			fclose(o);
		}
		// merged: the scene's units with the oracle's figure lines (forced poses), the scene's art_row ("") and flags kept
		cu::UnitScene m = s;
		for(cu::UnitInput& u : m.units)
			for(const cu::UnitInput& b : ref.units)
				if(b.player == u.player && b.id == u.id)
					u.figures = b.figures;
		ws = cu::writeSceneFile(c5r::gamefs::fromUtf8(out + "\\merged\\" + name), m);
		if(!ws.ok())
		{
			printf("oracle: %s: merged scene refused: %s\n", name.c_str(), ws.str().c_str());
			return 1;
		}
		files++;
		for(const std::string& b : bad)
			printf("oracle: %s: input: %s\n", name.c_str(), b.c_str());
		for(size_t i = logBefore.size(); i < g_log.size(); ++i)
			printf("oracle: %s: log: %s\n", name.c_str(), g_log[i].c_str());
	}
	printf("oracle: files %d units %d equal %d differences %d\n", files, unitsAll, unitsEqual, differ);
	for(const auto& kv : diffKinds)
		printf("oracle: difference %s: %d\n", kv.first.c_str(), kv.second);
	return 0;
}


// ----------------------------------------------------------------------------------- issue 4456: the controller hook
namespace ca = c5r::unitanim;

std::string Kinds(const ca::TimelineFrame& f)
{
	std::string s;
	for(const ca::TimelineEvent& e : f.events)
		s += std::string(s.empty() ? "" : ",") + ca::eventKindName(e.kind) + ":" + std::to_string(e.id);
	return s;
}


// Issue 4469: the embarked row (EmbarkedRow, unit-server-scene.md 3.2): the owner's override in every era; else by the
// team's era: GALLEY below the all-water technology's era, refused from that era up to LAST_EMBARK_ART_ERA (the feed has
// no all-water flag), TRANSPORT from LAST_EMBARK_ART_ERA on; a city-state (civilization -1) has no override; missing
// tables refuse.
bool CaseEmbarkedRow()
{
	Lookups l = TestLookups();
	std::string row, why;
	EXPECT(EmbarkedRow(l, 5, 0, row, why) && row == "ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE");
	EXPECT(EmbarkedRow(l, 5, 7, row, why) && row == "ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE");
	EXPECT(EmbarkedRow(l, 9, 0, row, why) && row == "ART_DEF_UNIT_GALLEY");
	EXPECT(EmbarkedRow(l, 9, 2, row, why) && row == "ART_DEF_UNIT_GALLEY");
	EXPECT(!EmbarkedRow(l, 9, 3, row, why) && row.empty() && why.find("all-water passage") != std::string::npos);
	EXPECT(EmbarkedRow(l, 9, 4, row, why) && row == "ART_DEF_UNIT_TRANSPORT");
	EXPECT(EmbarkedRow(l, -1, 7, row, why) && row == "ART_DEF_UNIT_TRANSPORT");
	EXPECT(EmbarkedRow(l, -1, 1, row, why) && row == "ART_DEF_UNIT_GALLEY");
	Lookups n = l;
	n.allWaterEra = -1;   // no technology gives the passage: GALLEY up to LAST_EMBARK_ART_ERA
	EXPECT(EmbarkedRow(n, 9, 3, row, why) && row == "ART_DEF_UNIT_GALLEY");
	Lookups u = l;
	u.lastEmbarkArtEra = -1;
	why.clear();
	EXPECT(!EmbarkedRow(u, 5, 0, row, why) && why == "the embarked art tables were not loaded");
	Lookups w = l;
	w.embarkWhy = "table Traits: no such table";
	EXPECT(!EmbarkedRow(w, 5, 0, row, why) && why == "table Traits: no such table");
	return true;
}

// Issue 4469: Lookups::loadEmbark from the database: the override through Civilization_Leaders -> Leader_Traits -> Traits
// (EmbarkedAllWater first, as CvPlayer::init), the define and the all-water era; each missing table or value refused by
// name; Lookups::load stays good without them (embarkWhy set).
bool CaseEmbarkedLookups()
{
	auto make = [](int drop) {
		std::unique_ptr<c5m::TsvTables> db(new c5m::TsvTables());
		c5m::Error e;
		if(drop != 1)
			db->add("Defines", Tsv({{"Name", "Value"}, {"MAX_HIT_POINTS", "100"}, {"LAST_EMBARK_ART_ERA", drop == 6 ? "x" : "4"}}), &e);
		if(drop != 2)
			db->add("Technologies", Tsv({{"Type", "Era", "EmbarkedAllWaterPassage"}, {"TECH_SAILING", "ERA_ANCIENT", "0"},
			                             {"TECH_ASTRONOMY", drop == 7 ? "ERA_NOWHERE" : "ERA_RENAISSANCE", "1"},
			                             {"TECH_LATE", "ERA_MODERN", "1"}}), &e);
		if(drop != 3)
			db->add("Traits", Tsv({{"Type", "EmbarkedAllWater", "EmbarkedToLandFlatCost"}, {"TRAIT_WAYFINDING", "1", "0"},
			                       {"TRAIT_VIKING_FURY", "0", "1"}, {"TRAIT_BOTH", "1", "1"}, {"TRAIT_OTHER", "0", "0"}}), &e);
		if(drop != 4)
			db->add("Leader_Traits", Tsv({{"LeaderType", "TraitType"}, {"LEADER_KAMEHAMEHA", "TRAIT_WAYFINDING"},
			                              {"LEADER_HARALD", "TRAIT_VIKING_FURY"}, {"LEADER_X", "TRAIT_BOTH"},
			                              {"LEADER_DIDO", "TRAIT_OTHER"}}), &e);
		if(drop != 5)
			db->add("Civilization_Leaders", Tsv({{"CivilizationType", "LeaderheadType"}, {"CIVILIZATION_POLYNESIA", "LEADER_KAMEHAMEHA"},
			                                     {"CIVILIZATION_DENMARK", "LEADER_HARALD"}, {"CIVILIZATION_X", "LEADER_X"},
			                                     {"CIVILIZATION_CARTHAGE", "LEADER_DIDO"}, {"CIVILIZATION_GONE", "LEADER_KAMEHAMEHA"}}), &e);
		return db;
	};
	Lookups l = TestLookups();
	l.civs = {{5, "CIVILIZATION_POLYNESIA"}, {6, "CIVILIZATION_DENMARK"}, {7, "CIVILIZATION_X"}, {8, "CIVILIZATION_CARTHAGE"}};
	l.eras = {{0, "ERA_ANCIENT"}, {3, "ERA_RENAISSANCE"}, {5, "ERA_MODERN"}};
	std::string why;
	{
		auto db = make(0);
		EXPECT(l.loadEmbark(*db, why));
		EXPECT(l.lastEmbarkArtEra == 4 && l.allWaterEra == 3 && l.embarkWhy.empty());
		EXPECT(l.embarkOverride.size() == 3 && l.embarkOverride[5] == "ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE" &&
		       l.embarkOverride[6] == "ART_DEF_UNIT_U_DANISH_LONGBOAT" && l.embarkOverride[7] == "ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE");
	}
	const char* const names[] = {"", "table Defines", "table Technologies", "table Traits", "table Leader_Traits",
	                             "table Civilization_Leaders", "Defines has no integer LAST_EMBARK_ART_ERA",
	                             "Technologies: era ERA_NOWHERE"};
	for(int d = 1; d <= 7; ++d)
	{
		auto db = make(d);
		Lookups m = l;
		why.clear();
		EXPECT(!m.loadEmbark(*db, why));
		if(why.find(names[d]) != 0)
		{
			printf("  drop %d: why '%s'\n", d, why.c_str());
			return false;
		}
	}
	// Lookups::load without the embarked tables: the load succeeds, embarkWhy names the first missing table
	{
		auto db = TestDb(false);
		Lookups m;
		why.clear();
		EXPECT(m.load(*db, why) && m.embarkWhy.find("table Defines") == 0);
		std::string row, w2;
		EXPECT(!EmbarkedRow(m, 5, 0, row, w2) && w2 == m.embarkWhy);
	}
	return true;
}

// Issue 4469 [L: 4465 J2, crop embarked_crops_S_J1_J0.png]: an embarked unit is listed with the embarked row, a land unit
// with "" (the module resolves its land row); the controller gets the same row through inputOf; an embarked unit in the
// era the feed cannot decide is not drawn (PlayerArtUnknown, once per owner and reason) and not given to the controller.
bool CaseEmbarkedArt()
{
	FakePort port;
	const uint8_t vis = cu::kFlagVisible;
	port.units = {Rec(0, 1001, 7, 23, 2, vis | cu::kFlagEmbarked), Rec(0, 1002, 7, 24, 2, vis),
	              Rec(1, 1003, 7, 25, 2, vis | cu::kFlagEmbarked), Rec(2, 1004, 7, 26, 2, vis | cu::kFlagEmbarked)};
	port.players = {Player(0, 3, 5, 0), Player(1, 3, 9, 4), Player(2, 3, 2, 3, 5, 1)};
	UnitsCore core(port, Log);
	Lookups l = TestLookups();
	l.civs[9] = "CIVILIZATION_CARTHAGE";
	l.eras = {{0, "ERA_ANCIENT"}, {3, "ERA_RENAISSANCE"}, {4, "ERA_INDUSTRIAL"}};
	core.setDatabase(l, nullptr);
	core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitEmbark, 0, 1001, 1, 0, 23), Ev(2, SrvFeed::kUnitCreated, 0, 1002, 7, 0, 24),
	                      Ev(3, SrvFeed::kUnitEmbark, 1, 1003, 1, 0, 25), Ev(4, SrvFeed::kUnitEmbark, 2, 1004, 1, 0, 26)}), 1000);
	BuiltFrame f;
	core.build(nullptr, nullptr, 1000, f);
	std::map<int32_t, std::string> rows;
	for(const cu::UnitInput& u : f.copies[1].units)
		rows[u.id] = u.art_row;
	EXPECT(rows.size() == 3 && rows[1001] == "ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE" && rows[1002].empty() &&
	       rows[1003] == "ART_DEF_UNIT_TRANSPORT");
	EXPECT(f.notDrawn == 1 && core.counters().byStatus[(int)Status::kPlayerArtUnknown] == 1);   // 1004: a city-state in era 3
	SrvFeed::UnitRecord r;
	cu::UnitInput ui;
	EXPECT(core.record(0, 1001, r) && core.inputOf(r, ui) && ui.art_row == "ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE");
	EXPECT(core.record(0, 1002, r) && core.inputOf(r, ui) && ui.art_row.empty());
	EXPECT(core.record(2, 1004, r) && !core.inputOf(r, ui));
	std::string row, why;
	EXPECT(!core.embarkedRowOf(r, row, why) && why.find("all-water passage") != std::string::npos);
	core.build(nullptr, nullptr, 1100, f);   // the second frame: counted again (per frame), logged once
	EXPECT(core.counters().byStatus[(int)Status::kPlayerArtUnknown] == 2);
	size_t said = 0;
	for(const std::string& ln : g_log)
		said += ln.find("embarked unit (2, 1004)") != std::string::npos;
	EXPECT(said == 1);
	// disembarked (the table's flag cleared): the land row again
	port.units[0].flags = vis;
	port.units[0].seq = 5;
	core.onBatch(BatchOf({Ev(5, SrvFeed::kUnitEmbark, 0, 1001, 0, 0, 23)}), 1200);
	core.build(nullptr, nullptr, 1200, f);
	for(const cu::UnitInput& u : f.copies[1].units)
		if(u.id == 1001)
			EXPECT(u.art_row.empty());
	// the controller: an Embark of a unit it holds is remove + create with the new row
	AnimFeed af;
	core.setAnimFeed(&af);
	af.start();
	ca::TimelineFrame tf;
	std::vector<cu::PlayerInput> pl;
	af.close(core, 1300, 50, tf, pl);
	EXPECT(af.holds(0, 1001) && !af.holds(2, 1004));   // 1004 has no embarked row: not given to the controller
	port.units[0].flags = vis | cu::kFlagEmbarked;
	port.units[0].seq = 6;
	core.onBatch(BatchOf({Ev(6, SrvFeed::kUnitEmbark, 0, 1001, 1, 0, 23)}), 1400);
	core.build(nullptr, nullptr, 1400, f);
	af.close(core, 1400, 51, tf, pl);
	EXPECT(Kinds(tf) == "remove:1001,create:1001" && tf.events[1].unit.art_row == "ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE");
	// an Embark of a unit the controller does not hold stays an embark creation there (unchanged)
	port.units.push_back(Rec(0, 1005, 7, 27, 2, vis | cu::kFlagEmbarked, 0, 100, 7));
	core.onBatch(BatchOf({Ev(7, SrvFeed::kUnitEmbark, 0, 1005, 1, 0, 27)}), 1500);
	core.build(nullptr, nullptr, 1500, f);
	af.close(core, 1500, 52, tf, pl);
	EXPECT(Kinds(tf) == "create:1005" && tf.events[0].unit.art_row == "ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE");
	return true;
}

// Issue 0439 (MC #2908, #2942): kind 21's a[3] (srv_feed.h EmbarkArtBits) decides the embarked row by the EXE's rule:
// the override by its hash when an art define has it (one that names none: the era rule; a hash two Types share: refused),
// TRANSPORT from LAST_EMBARK_ART_ERA on, else GALLEON with the team's all-water passage and GALLEY without, also in the
// Renaissance gap the database alone cannot decide; bits without the read bit: the older rule. Through UnitsCore: the bits
// of the unit's last embark, cleared by a disembark, a creation, a removal and a load.
bool CaseEmbarkedBits()
{
	Lookups l = TestLookups();
	const char* canoe = "ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE";
	const char* longboat = "ART_DEF_UNIT_U_DANISH_LONGBOAT";
	for(const char* t : {canoe, longboat, "ART_DEF_UNIT_GALLEY", "ART_DEF_UNIT_GALLEON", "ART_DEF_UNIT_TRANSPORT"})
		l.artByHash24[SrvFeed::EmbarkArtHash24(t)] = t;
	const uint32_t R = SrvFeed::kEmbarkArtRead, W = SrvFeed::kEmbarkArtAllWater, O = SrvFeed::kEmbarkArtOverride;
	auto ovr = [&](const char* name) { return O | (SrvFeed::EmbarkArtHash24(name) << SrvFeed::kEmbarkArtHashShift); };
	std::string row, why;
	EXPECT(EmbarkedRow(l, 9, 3, R | W, row, why) && row == "ART_DEF_UNIT_GALLEON");   // the Renaissance gap, decided
	EXPECT(EmbarkedRow(l, 9, 3, R, row, why) && row == "ART_DEF_UNIT_GALLEY");
	EXPECT(EmbarkedRow(l, 9, 1, R | W, row, why) && row == "ART_DEF_UNIT_GALLEON");   // the bit, not the era, decides
	EXPECT(EmbarkedRow(l, 9, 4, R | W, row, why) && row == "ART_DEF_UNIT_TRANSPORT");
	EXPECT(EmbarkedRow(l, -1, 2, R, row, why) && row == "ART_DEF_UNIT_GALLEY");
	// the override as the feed read it, not the database's: a Lua change is seen; none read: no override even for
	// a civilization whose traits give one
	EXPECT(EmbarkedRow(l, 5, 3, R | ovr(longboat), row, why) && row == longboat);
	EXPECT(EmbarkedRow(l, 9, 7, R | W | ovr(canoe), row, why) && row == canoe);        // in every era
	EXPECT(EmbarkedRow(l, 5, 3, R | W, row, why) && row == "ART_DEF_UNIT_GALLEON");
	EXPECT(EmbarkedRow(l, 5, 3, R | ovr("ART_DEF_UNIT_NOT_A_ROW"), row, why) && row == "ART_DEF_UNIT_GALLEY");   // ignored
	// bits without the read bit (a recording before 0439, a failed read): the older rule
	EXPECT(EmbarkedRow(l, 5, 3, W | ovr(longboat), row, why) && row == canoe);
	EXPECT(!EmbarkedRow(l, 9, 3, 0, row, why) && why.find("all-water passage") != std::string::npos);
	// refusals: an ambiguous hash, the art table unread, the embarked tables missing
	Lookups a = l;
	a.artByHash24[SrvFeed::EmbarkArtHash24(longboat)] = "";
	why.clear();
	EXPECT(!EmbarkedRow(a, 5, 3, R | ovr(longboat), row, why) && row.empty() && why.find("names two art defines") != std::string::npos);
	Lookups t = l;
	t.artHashWhy = "table ArtDefine_UnitInfos: no such table";
	EXPECT(!EmbarkedRow(t, 5, 3, R | ovr(longboat), row, why) && why.find("ArtDefine_UnitInfos") != std::string::npos);
	EXPECT(EmbarkedRow(t, 5, 3, R | W, row, why) && row == "ART_DEF_UNIT_GALLEON");   // no override: not needed
	Lookups m = l;
	m.embarkWhy = "table Traits: no such table";
	EXPECT(!EmbarkedRow(m, 9, 3, R | W, row, why) && why == m.embarkWhy);
	// the hash: FNV-1a 32's top 24 bits, NULL 0, distinct for these Types
	EXPECT(SrvFeed::EmbarkArtHash24(nullptr) == 0 && SrvFeed::EmbarkArtHash24("") == (2166136261u >> 8));
	EXPECT(SrvFeed::EmbarkArtHash24(canoe) != SrvFeed::EmbarkArtHash24(longboat) && SrvFeed::EmbarkArtHash24(canoe) < (1u << 24));
	// through UnitsCore: a city-state (no override) in the Renaissance; a major civilization whose override changed
	FakePort port;
	const uint8_t vis = cu::kFlagVisible;
	port.units = {Rec(2, 1004, 7, 26, 2, vis | cu::kFlagEmbarked, 0, 100, 1), Rec(0, 1001, 7, 23, 2, vis | cu::kFlagEmbarked, 0, 100, 2)};
	port.players = {Player(0, 3, 5, 3), Player(2, 3, 2, 3, 5, 1)};
	UnitsCore core(port, Log);
	l.civs[9] = "CIVILIZATION_CARTHAGE";
	l.eras = {{0, "ERA_ANCIENT"}, {3, "ERA_RENAISSANCE"}, {4, "ERA_INDUSTRIAL"}};
	core.setDatabase(l, nullptr);
	core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitEmbark, 2, 1004, 1, (int)(R | W), 26),
	                      Ev(2, SrvFeed::kUnitEmbark, 0, 1001, 1, (int)(R | ovr(longboat)), 23)}), 1000);
	BuiltFrame f;
	core.build(nullptr, nullptr, 1000, f);
	std::map<int32_t, std::string> rows;
	for(const cu::UnitInput& u : f.copies[1].units)
		rows[u.id] = u.art_row;
	EXPECT(rows.size() == 2 && rows[1004] == "ART_DEF_UNIT_GALLEON" && rows[1001] == longboat);
	EXPECT(f.notDrawn == 0 && core.counters().byStatus[(int)Status::kPlayerArtUnknown] == 0);
	SrvFeed::UnitRecord r;
	cu::UnitInput ui;
	EXPECT(core.record(2, 1004, r) && core.inputOf(r, ui) && ui.art_row == "ART_DEF_UNIT_GALLEON");   // the controller's too
	// the next embark of 1004 without the passage bit: GALLEY; a disembark then a creation of the key: the bits gone
	port.units[0].seq = 3;
	core.onBatch(BatchOf({Ev(3, SrvFeed::kUnitEmbark, 2, 1004, 1, (int)R, 26)}), 1100);
	EXPECT(core.record(2, 1004, r) && core.inputOf(r, ui) && ui.art_row == "ART_DEF_UNIT_GALLEY");
	port.units[0].seq = 4;
	core.onBatch(BatchOf({Ev(4, SrvFeed::kUnitEmbark, 2, 1004, 0, 0, 26)}), 1200);
	port.units[0].seq = 5;
	core.onBatch(BatchOf({Ev(5, SrvFeed::kUnitCreated, 2, 1004, 7, 0, 26)}), 1300);   // table flag still embarked
	std::string w2;
	EXPECT(core.record(2, 1004, r) && !core.embarkedRowOf(r, row, w2) && w2.find("all-water passage") != std::string::npos);
	// a load clears every unit's bits
	port.units[1].seq = 6;
	core.onBatch(BatchOf({Ev(6, SrvFeed::kLoadBegin, 2, 0)}), 1400);
	EXPECT(core.record(0, 1001, r) && core.embarkedRowOf(r, row, w2) && row == canoe);   // the database's override again
	return true;
}

// The mapping of unit-controller.md 3 as srv_units_anim.h lists it, kind by kind, with every counted refusal: events before
// the start, the start's creates, a unit not held, a create not formed, the work stop and an unknown build, a creation of a
// held key, the removal, the frame id going back (seq bump), an overflow's resync, a load and a player's era change; then
// the frames as 0374's file read back by 0374's reader.
bool CaseAnimMapping()
{
	FakePort port;
	port.units = {Rec(0, 1001, 7, 23, 4, cu::kFlagVisible, 0, 100, 1)};
	port.players = {Player(0, 3, 5, 0)};
	UnitsCore core(port, Log);
	Lookups l = TestLookups();
	l.eras[1] = "ERA_CLASSICAL";
	core.setDatabase(l, nullptr);
	AnimFeed af;
	af.setBuilds({{0, "BUILD_ROAD"}});
	core.setAnimFeed(&af);
	AnimRecord rec;
	core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 23)}), 1000);   // before the start: not taken
	BuiltFrame bf;
	core.build(nullptr, nullptr, 1000, bf);
	EXPECT(!af.started());
	af.start();
	ca::TimelineFrame f;
	std::vector<cu::PlayerInput> pl;
	af.close(core, 1000, 41, f, pl);
	rec.add(f, pl);
	EXPECT(Kinds(f) == "create:1001");
	const cu::UnitInput& u = f.events[0].unit;
	EXPECT(u.unit_type == "UNIT_WARRIOR" && u.plot_x == 3 && u.plot_y == 2 && u.facing == 4 && u.formation_seed == 0 && u.alive == -1 &&
	       u.flags == cu::kFlagVisible);
	EXPECT(f.dt == 0 && f.seq == 1 && f.has_exe_frame && f.exe_frame == 41 && f.has_present && f.present_time == 1.0);
	EXPECT(pl.size() == 1 && pl[0].colour == "PLAYERCOLOR_POLYNESIA" && pl[0].art_style == "CIVILIZATION_POLYNESIA" && pl[0].era == "ERA_ANCIENT");
	// every mapped kind on the held unit, a creation, an event of a unit never held, kinds with no event
	port.units = {Rec(0, 1001, 7, 24, 5, cu::kFlagVisible | cu::kFlagFortified, 30, 100, 17), Rec(0, 1002, 8, 5, 1, cu::kFlagVisible, 0, 100, 18)};
	SrvFeed::Batch b = BatchOf({Ev(2, SrvFeed::kUnitMoved, 0, 1001, 0, 3), Ev(3, SrvFeed::kUnitTeleported, 0, 1001, 24),
	                            Ev(4, SrvFeed::kUnitRebased, 0, 1001, 23, 24), Ev(5, SrvFeed::kUnitSetDamage, 0, 1001, 30, 0),
	                            Ev(6, SrvFeed::kUnitFortify, 0, 1001, 1), Ev(7, SrvFeed::kUnitWork, 0, 1001, 0), Ev(8, SrvFeed::kUnitWork, 0, 1001, -1),
	                            Ev(9, SrvFeed::kUnitWork, 0, 1001, 99), Ev(10, SrvFeed::kUnitParadrop, 0, 1001), Ev(11, SrvFeed::kUnitActivate, 0, 1001),
	                            Ev(12, SrvFeed::kUnitEmbark, 0, 1001, 1), Ev(13, SrvFeed::kUnitGarrison, 0, 1001, 1),
	                            Ev(14, SrvFeed::kUnitVisibility, 0, 1001, 1, (int)FloatBits(0.5f)), Ev(15, SrvFeed::kUnitResetAnimationState, 0, 1001),
	                            Ev(16, SrvFeed::kUnitShouldDimFlag, 0, 1001, 1), Ev(17, SrvFeed::kUnitDestroyedInCombat, 0, 1001),
	                            Ev(18, SrvFeed::kUnitCreated, 0, 1002, 8), Ev(19, SrvFeed::kUnitSetDamage, 0, 1003, 10, 0),
	                            Ev(20, SrvFeed::kUnitMissionEnd, 0, 1001)});
	SrvFeed::PathEntry pe;
	pe.seq = 2;
	pe.first = 0;
	pe.length = 3;
	b.paths = {pe};
	b.pathPlots = {21, 22, 24};
	core.onBatch(b, 1100);
	core.build(nullptr, nullptr, 1100, bf);
	af.close(core, 1100, 42, f, pl);
	rec.add(f, pl);
	// issue 4469: the Embark of the held unit reaches the controller as remove + create (the EXE's model swap), so the unit
	// binds its new art through inputOf (was the controller's embark event, which kept the art)
	// issue 0439: the work stop is `action work -` (an empty argument: the EXE posts IDLE) and an unknown build index an
	// action whose Type no Builds row has (the EXE's hammer); was: neither formed an event
	EXPECT(Kinds(f) == "move:1001,arrive:1001,place:1001,place:1001,damage:1001,fortify:1001,action:1001,action:1001,action:1001,"
	                   "action:1001,action:1001,remove:1001,create:1001,garrison:1001,visibility:1001,reset:1001,destroyed:1001,"
	                   "create:1002");
	EXPECT(f.events[0].a == 5 && f.events[0].b == 3 && f.events[1].a == 5);                       // move 5 3, arrive 5
	EXPECT(f.events[2].a == 4 && f.events[2].b == 2 && f.events[2].c == 5);                        // teleported to plot 24
	EXPECT(f.events[3].a == 4 && f.events[3].b == 2);                                              // rebased to plot 24
	EXPECT(f.events[4].a == 30 && f.events[4].b == 0 && f.events[5].a == 1);
	EXPECT(f.events[6].action == ca::ActionKind::Work && f.events[6].argument == "BUILD_ROAD");
	EXPECT(f.events[7].action == ca::ActionKind::Work && f.events[7].argument.empty());
	EXPECT(f.events[8].action == ca::ActionKind::Work && f.events[8].argument == "BUILD_UNKNOWN_INDEX_99");
	EXPECT(f.events[9].action == ca::ActionKind::Paradrop && f.events[10].action == ca::ActionKind::Activate);
	EXPECT(f.events[12].unit.unit_type == "UNIT_WARRIOR" && f.events[12].unit.art_row.empty());   // the table: not embarked
	EXPECT(f.events[13].a == 1 && f.events[14].a == 1 && f.events[14].blend_time == 0.5f && f.events[16].a == 1);
	EXPECT(f.events[17].unit.unit_type == "UNIT_TRIREME" && f.events[17].unit.formation_seed == 1);
	EXPECT(Near((float)f.dt, 0.1f) && f.seq == 2 && f.exe_frame == 42 && pl.empty());
	EXPECT(af.counters().workStops == 1 && af.counters().workUnknown == 1 && af.counters().droppedNotHeld == 1);
	// a creation of a held key (an era change re-creates it): remove + create; the frame id going back: a redraw, seq goes on
	port.units[0].seq = 21;
	// issue 4739: the destroyed record leaves the table with its batch, as the feed's table does (PushUnit's kRemove in the
	// event's own lock; was: popped after the batch, a table copy the feed cannot give, which 4739's follow re-created)
	port.units.pop_back();
	core.onBatch(BatchOf({Ev(21, SrvFeed::kUnitCreated, 0, 1001, 7), Ev(22, SrvFeed::kUnitDestroyed, 0, 1002)}), 1200);
	core.onBatch(BatchOf({}), 1200);
	core.build(nullptr, nullptr, 1200, bf);
	af.close(core, 1200, 5, f, pl);
	rec.add(f, pl);
	EXPECT(Kinds(f) == "remove:1001,create:1001,remove:1002");
	EXPECT(f.seq == 3 && f.exe_frame == 5 && af.counters().redraws == 1);
	// created and destroyed before the frame's end: not formed, its removal not held; an overflow: the held set follows the
	// re-copied table (1001 gone, 1005 new); the player's era changes: given again
	port.units = {Rec(0, 1005, 7, 30, 0, cu::kFlagVisible, 0, 100, 25)};
	SrvFeed::Batch b2 = BatchOf({Ev(23, SrvFeed::kUnitCreated, 0, 1004, 7), Ev(24, SrvFeed::kUnitDestroyed, 0, 1004), Ev(25, SrvFeed::kOverflow, 3, 0)});
	b2.events[2].a[0] = 3;
	b2.players = {Player(0, 3, 5, 1)};
	core.onBatch(b2, 1300);
	core.build(nullptr, nullptr, 1300, bf);
	af.close(core, 1300, 50, f, pl);
	rec.add(f, pl);
	EXPECT(Kinds(f) == "remove:1001,create:1005");
	EXPECT(af.counters().createsNotFormed == 1 && af.counters().droppedNotHeld == 2 && af.counters().resyncs == 1);
	EXPECT(pl.size() == 1 && pl[0].era == "ERA_CLASSICAL" && af.counters().playerChanges == 1);
	// a load: every held unit removed
	port.units.clear();
	core.onBatch(BatchOf({Ev(26, SrvFeed::kLoadBegin, 2, 0)}), 1400);
	core.build(nullptr, nullptr, 1400, bf);
	af.close(core, 1400, 51, f, pl);
	rec.add(f, pl);
	EXPECT(Kinds(f) == "remove:1005" && af.held() == 0);
	// the record as 0374's file, read back by 0374's reader
	char tmp[MAX_PATH];
	GetTempPathA(MAX_PATH, tmp);
	const std::string file = std::string(tmp) + "c5_4456_anim_" + std::to_string(GetCurrentProcessId()) + ".c5anim";
	std::string why;
	EXPECT(rec.write(c5r::gamefs::fromUtf8(file), why));
	ca::Timeline back;
	cu::UnitScene start;
	const ca::TimelineStatus ts = ca::readTimelineFile(c5r::gamefs::fromUtf8(file), back, start);
	DeleteFileA(file.c_str());
	EXPECT(ts.ok() && back.frames.size() == 5 && back.players.size() == 1 && back.players[0].era == "ERA_ANCIENT" && back.scene_path.empty());
	EXPECT(back.frames[1].events.size() == 18 && back.frames[2].seq == 3 && back.frames[2].exe_frame == 5 && back.frames[1].events[6].argument == "BUILD_ROAD" &&
	       back.frames[1].events[7].argument.empty());   // issue 0439: the stop read back
	// the writer refuses what 0374's reader refuses (here: an event for a unit the file never created): named, no file
	AnimRecord bad;
	ca::TimelineFrame bf2;
	ca::TimelineEvent ev;
	ev.kind = ca::EventKind::Fortify;
	ev.player = 0;
	ev.id = 77;
	bf2.events.push_back(ev);
	bad.add(bf2, {});
	EXPECT(!bad.write(c5r::gamefs::fromUtf8(file), why) && why.find("UnknownUnit") != std::string::npos);
	EXPECT(GetFileAttributesA(file.c_str()) == INVALID_FILE_ATTRIBUTES && GetFileAttributesA((file + ".tmp").c_str()) == INVALID_FILE_ATTRIBUTES);
	return true;
}

// Issue 4739 (W38's S1', 2026-11-21 [L]; the cause [C]): a unit a Lua InitUnit made carries facing 7 (NO_DIRECTION) until
// its first move, and CvUnit::move posts the embark before setXY turns it, so kind 21 puts 7 into the record. The embark of
// a held unit is remove + create (4469); the create was refused for its facing (0..5 only) while the scene drew the
// embarked unit: 'no pose program ... figure 0' at sea, drawn again only at the disembark. Now: the creation, the embark's
// swap, the move and the disembark are all held, the facing 7 kept (the module's 1 rad, 4464), no create refused.
bool CaseAnimEmbarkFacing7()
{
	FakePort port;
	const uint8_t vis = cu::kFlagVisible;
	port.units = {Rec(0, 1001, 7, 22, 7, vis, 0, 100, 1)};   // Lua-made: facing 7
	port.players = {Player(0, 3, 5, 0)};
	UnitsCore core(port, Log);
	core.setDatabase(TestLookups(), nullptr);
	AnimFeed af;
	core.setAnimFeed(&af);
	core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 22)}), 1000);
	BuiltFrame bf;
	core.build(nullptr, nullptr, 1000, bf);
	af.start();   // the start's creates (the op before it is not taken)
	ca::TimelineFrame f;
	std::vector<cu::PlayerInput> pl;
	af.close(core, 1000, 41, f, pl);
	EXPECT(Kinds(f) == "create:1001" && f.events.size() == 1 && f.events[0].unit.facing == 7 && af.holds(0, 1001));
	// the embark at the old plot, facing still 7: the model swap is remove + create with the embarked row
	port.units[0].flags = vis | cu::kFlagEmbarked;
	port.units[0].seq = 2;
	core.onBatch(BatchOf({Ev(2, SrvFeed::kUnitEmbark, 0, 1001, 1, 0, 22)}), 1100);
	core.build(nullptr, nullptr, 1100, bf);
	EXPECT(bf.copies[1].units.size() == 1 && bf.copies[1].units[0].art_row == "ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE");   // listed
	af.close(core, 1100, 42, f, pl);
	EXPECT(Kinds(f) == "remove:1001,create:1001" && af.holds(0, 1001));
	EXPECT(f.events.size() == 2 && f.events[1].unit.art_row == "ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE" && f.events[1].unit.facing == 7);
	// the move onto the sea plot turns it (NE): held, so the move and its arrive are given
	port.units[0].plot = 23;
	port.units[0].facing = 0;
	port.units[0].seq = 3;
	SrvFeed::Batch mb = BatchOf({Ev(3, SrvFeed::kUnitMoved, 0, 1001, 0, 2)});
	SrvFeed::PathEntry pe;
	pe.seq = 3;
	pe.first = 0;
	pe.length = 2;
	mb.paths = {pe};
	mb.pathPlots = {22, 23};
	core.onBatch(mb, 1200);
	core.build(nullptr, nullptr, 1200, bf);
	af.close(core, 1200, 43, f, pl);
	EXPECT(Kinds(f) == "move:1001,arrive:1001");
	// the disembark (facing 0 now): remove + create with the land row
	port.units[0].flags = vis;
	port.units[0].seq = 4;
	core.onBatch(BatchOf({Ev(4, SrvFeed::kUnitEmbark, 0, 1001, 0, 0, 23)}), 1300);
	core.build(nullptr, nullptr, 1300, bf);
	af.close(core, 1300, 44, f, pl);
	EXPECT(Kinds(f) == "remove:1001,create:1001" && f.events[1].unit.art_row.empty() && f.events[1].unit.facing == 0);
	EXPECT(af.counters().createsNotFormed == 0 && af.counters().droppedNotHeld == 0 && af.counters().recreated == 0 &&
	       af.counters().rereads == 0);
	return true;
}

// Issue 4739 (MC #9402, shape b): a held unit's removal while its record stays in the scene's table (create, then one batch
// {UnitDestroyed, SetDamage} with the record kept): the controller creates it again at the same close, from the record (its
// damage), counted; the SetDamage of the frame is dropped as not held (the record carries it). Before 4739: removed, not held,
// the scene still listing it ('no pose program').
bool CaseAnimRecordKept()
{
	FakePort port;
	port.units = {Rec(0, 1001, 7, 22, 2, cu::kFlagVisible, 0, 100, 1)};
	port.players = {Player(0, 3, 5, 0)};
	UnitsCore core(port, Log);
	core.setDatabase(TestLookups(), nullptr);
	AnimFeed af;
	core.setAnimFeed(&af);
	core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 22)}), 1000);
	BuiltFrame bf;
	core.build(nullptr, nullptr, 1000, bf);
	af.start();   // the start's creates (the op before it is not taken)
	ca::TimelineFrame f;
	std::vector<cu::PlayerInput> pl;
	af.close(core, 1000, 41, f, pl);
	EXPECT(Kinds(f) == "create:1001");
	port.units[0].damage = 10;
	port.units[0].seq = 3;
	core.onBatch(BatchOf({Ev(2, SrvFeed::kUnitDestroyed, 0, 1001), Ev(3, SrvFeed::kUnitSetDamage, 0, 1001, 10, 0)}), 1100);
	core.build(nullptr, nullptr, 1100, bf);
	af.close(core, 1100, 42, f, pl);
	EXPECT(Kinds(f) == "remove:1001,create:1001" && af.holds(0, 1001) && f.events[1].unit.damage == 10);
	EXPECT(af.counters().recreated == 1 && af.counters().droppedNotHeld == 1);
	// the next frames: nothing more (held, nothing touched)
	core.onBatch(BatchOf({}), 1200);
	core.build(nullptr, nullptr, 1200, bf);
	af.close(core, 1200, 43, f, pl);
	EXPECT(f.events.empty() && af.counters().recreated == 1);
	return true;
}

// Issue 4739 (MC #9402, no ghost): the same batch with the record really gone stays removed, at that close and after.
bool CaseAnimRecordGone()
{
	FakePort port;
	port.units = {Rec(0, 1001, 7, 22, 2, cu::kFlagVisible, 0, 100, 1)};
	port.players = {Player(0, 3, 5, 0)};
	UnitsCore core(port, Log);
	core.setDatabase(TestLookups(), nullptr);
	AnimFeed af;
	core.setAnimFeed(&af);
	core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 22)}), 1000);
	BuiltFrame bf;
	core.build(nullptr, nullptr, 1000, bf);
	af.start();   // the start's creates (the op before it is not taken)
	ca::TimelineFrame f;
	std::vector<cu::PlayerInput> pl;
	af.close(core, 1000, 41, f, pl);
	port.units.clear();
	core.onBatch(BatchOf({Ev(2, SrvFeed::kUnitDestroyed, 0, 1001), Ev(3, SrvFeed::kUnitSetDamage, 0, 1001, 10, 0)}), 1100);
	core.build(nullptr, nullptr, 1100, bf);
	af.close(core, 1100, 42, f, pl);
	EXPECT(Kinds(f) == "remove:1001" && !af.holds(0, 1001) && bf.copies[1].units.empty());
	for(int k = 0; k < 3; ++k)
	{
		core.onBatch(BatchOf({Ev(4 + k, SrvFeed::kUnitVisibility, 0, 1001, 1, 0)}), 1200 + 100 * k);   // late ops of the gone key
		core.build(nullptr, nullptr, 1200 + 100 * k, bf);
		af.close(core, 1200 + 100 * k, 43 + k, f, pl);
		EXPECT(f.events.empty() && !af.holds(0, 1001));
	}
	EXPECT(af.counters().recreated == 0 && af.counters().createsNotFormed == 0);
	return true;
}

// Issue 4739: a create the record cannot form is LOUD once with its reason and tried again only when the record changes
// (not every frame); then the unit is created. A held unit whose embarked bit differs from the table's (no embark op came)
// is removed and created again with the table's art (rereads).
bool CaseAnimUnformedRetry()
{
	FakePort port;
	port.units = {Rec(0, 1009, 99, 22, 2, cu::kFlagVisible, 0, 100, 1)};   // type 99: no Units row
	port.players = {Player(0, 3, 5, 0)};
	UnitsCore core(port, Log);
	core.setDatabase(TestLookups(), nullptr);
	AnimFeed af;
	core.setAnimFeed(&af);
	core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1009, 99, 0, 22)}), 1000);
	BuiltFrame bf;
	core.build(nullptr, nullptr, 1000, bf);
	af.start();
	ca::TimelineFrame f;
	std::vector<cu::PlayerInput> pl;
	af.close(core, 1000, 41, f, pl);
	EXPECT(f.events.empty() && !af.holds(0, 1009) && af.counters().createsNotFormed == 1);
	size_t loud = 0;
	for(const std::string& l : af.lastLoud())
		loud += l == "issue 4739: the create of unit (0, 1009) was not formed: unit type 99 has no Units row: not drawn until its record "
		             "changes";
	EXPECT(loud == 1);
	for(int k = 0; k < 3; ++k)   // the record unchanged: not tried again
	{
		core.onBatch(BatchOf({}), 1100 + 100 * k);
		core.build(nullptr, nullptr, 1100 + 100 * k, bf);
		af.close(core, 1100 + 100 * k, 42 + k, f, pl);
		EXPECT(f.events.empty() && af.lastLoud().empty());
	}
	EXPECT(af.counters().createsNotFormed == 1 && af.counters().retriesWaiting == 4);   // the first close's own check, then 3
	// the record changes (its type known now, an op of the frame dropped as not held): created at the close
	port.units[0].unitType = 7;
	port.units[0].seq = 2;
	core.onBatch(BatchOf({Ev(2, SrvFeed::kUnitFortify, 0, 1009, 1)}), 1500);
	core.build(nullptr, nullptr, 1500, bf);
	af.close(core, 1500, 45, f, pl);
	EXPECT(Kinds(f) == "create:1009" && af.holds(0, 1009) && af.counters().recreated == 1 && f.events[0].unit.unit_type == "UNIT_WARRIOR");
	// the table says embarked, no embark op came: removed and created again with the embarked row
	port.units[0].flags = cu::kFlagVisible | cu::kFlagEmbarked;
	port.units[0].seq = 3;
	core.onBatch(BatchOf({Ev(3, SrvFeed::kUnitVisibility, 0, 1009, 1, 0)}), 1600);
	core.build(nullptr, nullptr, 1600, bf);
	af.close(core, 1600, 46, f, pl);
	EXPECT(Kinds(f) == "visibility:1009,remove:1009,create:1009" && af.counters().rereads == 1);
	EXPECT(f.events.size() == 3 && f.events[2].unit.art_row == "ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE");
	// a facing the module cannot place (beyond -1..7) is refused with its reason
	port.units.push_back(Rec(0, 1010, 7, 24, 9, cu::kFlagVisible, 0, 100, 4));
	core.onBatch(BatchOf({Ev(4, SrvFeed::kUnitCreated, 0, 1010, 7, 0, 24)}), 1700);
	core.build(nullptr, nullptr, 1700, bf);
	af.close(core, 1700, 47, f, pl);
	loud = 0;
	for(const std::string& l : af.lastLoud())
		loud += l.find("issue 4739: the create of unit (0, 1010) was not formed: facing 9 outside -1..7") == 0;
	EXPECT(f.events.empty() && loud == 1 && !af.holds(0, 1010));
	return true;
}

// Issue 4739 through the host (the scene's table, the controller, the pose source; needs the install): a warrior a Lua
// InitUnit made (facing 7) is drawn with the controller's figures and no UnitRefused. Before 4739 its create was refused
// for the facing while the scene listed it: UnitRefused 'no pose program ... figure 0' on every frame.
bool CaseAnimFacing7Host()
{
	Host h(TestEnv(false));
	h.u->poll();
	h.probe->waitPrecheck();
	EXPECT(h.probe->precheckPassed());
	h.port.units = {Rec(0, 1001, 7, 2 * 10 + 2, 7, cu::kFlagVisible, 0, 100, 1)};
	h.u->feed(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 22)}));
	h.u->setDevice(g_dev);
	Target t;
	const c5r::heightfield::HeightGrid g = Grid();
	float cx = 0, cy = 0;
	EXPECT(cu::plotCentre(g, 2, 2, cx, cy).ok());
	UnitsCamera cam = LookAt(cx, cy);
	for(int k = 0; k < 3; ++k)
	{
		h.t = 1000 + 100 * k;
		cam.frameId = 41 + k;
		t.clear();
		h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
		EXPECT(t.covered() > 50 && h.probe->lastBuilt().copies[1].units.size() == 1 &&
		       h.probe->lastBuilt().copies[1].units[0].figures.size() == 12);
	}
	EXPECT(h.probe->core().counters().byStatus[(int)Status::kUnitRefused] == 0 &&
	       h.probe->core().counters().byStatus[(int)Status::kAnimFailed] == 0);
	EXPECT(LogCount("no pose program for unit (0, 1001)") == 0);
	char line[4096];
	h.u->secondLine(line, sizeof(line));
	EXPECT(strstr(line, " anim_not_formed=0 anim_recreated=0 anim_rereads=0") != nullptr);
	return true;
}

// Issue 4457 / 4456: the batch clock is the drain's (the recorder's value when recording), the scene's own clock otherwise.
bool CaseDrainClock()
{
	Host h([](const std::wstring&, Env&, std::string& why) { why = "no database in this case"; return false; });
	h.port.units = {Rec(0, 1001, 7, 22, 2, 0, 0, 100, 1)};
	h.t = 1000;
	h.u->feed(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7)}), 900);
	h.probe->core().setDatabase(TestLookups(), nullptr);
	h.port.units[0].flags = cu::kFlagVisible;
	h.port.units[0].seq = 2;
	h.t = 5000;   // the scene's clock, not used while drainMs is given
	h.u->feed(BatchOf({Ev(2, SrvFeed::kUnitVisibility, 0, 1001, 1, (int)FloatBits(1.0f))}), 1500);
	BuiltFrame bf;
	h.probe->core().build(nullptr, nullptr, 2000, bf);
	EXPECT(bf.copies[1].units.size() == 1 && Near(bf.copies[1].units[0].fade, 0.5f));
	// no drain clock (-1): the scene reads its own (h.t = 2000)
	h.port.units[0].seq = 3;
	h.t = 2000;
	h.u->feed(BatchOf({Ev(3, SrvFeed::kUnitVisibility, 0, 1001, 1, (int)FloatBits(1.0f))}));
	h.probe->core().build(nullptr, nullptr, 2250, bf);
	EXPECT(bf.copies[1].units.size() == 1 && Near(bf.copies[1].units[0].fade, 0.25f));
	return true;
}

// Issue 4457 / 4456: useFeedSource routes the scene's table copies through the host's (recording) feed; the unit database
// is still asked of the scene's own port; null restores the port.
struct FakeSource : srvrender::FeedSource
{
	int units = 0, players = 0;
	std::vector<SrvFeed::UnitRecord> table;
	bool drain(SrvFeed::Batch&) override { return false; }
	void copyPlots(std::vector<SrvFeed::PlotRecord>& out, uint32_t& gen, uint32_t& w, uint32_t& h, uint32_t) override
	{
		out.clear();
		gen = w = h = 0;
	}
	void copyUnits(std::vector<SrvFeed::UnitRecord>& out, uint32_t& gen) override
	{
		units++;
		out = table;
		gen = 0;
	}
	void copyPlayers(std::vector<SrvFeed::PlayerRecord>& out) override
	{
		players++;
		out = {Player(0, 3, 5, 0)};
	}
};

bool CaseRecordingPort()
{
	Host h([](const std::wstring&, Env&, std::string& why) { why = "no database in this case"; return false; });
	FakeSource src;
	src.table = {Rec(0, 1001, 7, 22, 2, cu::kFlagVisible, 0, 100, 1)};
	h.u->useFeedSource(&src);
	EXPECT(LogCount("units: the scene's table copies go through the recording feed") == 1);
	h.u->feed(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7)}));
	EXPECT(src.units == 1 && src.players == 1 && h.port.copies == 0 && h.port.playerCopies == 0);
	EXPECT(h.probe->core().tableSize() == 1);
	h.u->poll();   // the database: the scene's own port
	EXPECT(h.port.dbAsks == 1);
	h.u->useFeedSource(nullptr);
	h.port.units = {Rec(0, 1001, 7, 23, 2, cu::kFlagVisible, 0, 100, 2)};
	h.u->feed(BatchOf({Ev(2, SrvFeed::kUnitTeleported, 0, 1001, 23)}));
	EXPECT(src.units == 1 && h.port.copies == 1);
	return true;
}

// Issue 4457 / 4456: CullOf is the product the scene culled with (view[0] x proj), not finite = no cull.
bool CaseCullOf()
{
	UnitsCamera cam = LookAt(100, 200);
	cam.wrapPeriod = 640;
	const CullCamera cc = CullOf(cam);
	float vp[16];
	for(int r = 0; r < 4; ++r)
		for(int c = 0; c < 4; ++c)
			vp[r * 4 + c] = cam.view[r * 4 + 0] * cam.proj[0 * 4 + c] + cam.view[r * 4 + 1] * cam.proj[1 * 4 + c] +
			                cam.view[r * 4 + 2] * cam.proj[2 * 4 + c] + cam.view[r * 4 + 3] * cam.proj[3 * 4 + c];
	EXPECT(!memcmp(vp, cc.vp, sizeof(vp)) && cc.valid && cc.wrap == 640);
	cam.proj[5] = NAN;
	EXPECT(!CullOf(cam).valid);
	return true;
}

// Issue 4456 (module): the figures come from the controller; the frame step advances their clip times; a unit the
// controller refuses (work without a work table, BadEvent) is AnimFailed, counted once, LOUD once, and not drawn.
bool CaseAnimController()
{
	Host h(TestEnv(false));
	h.u->poll();
	h.probe->waitPrecheck();
	EXPECT(h.probe->precheckPassed());
	FeedWarrior(h);
	h.u->setDevice(g_dev);
	EXPECT(LogCount("figures from the controller") == 0);   // the drawing renderer opens at the first draw
	Target t;
	const c5r::heightfield::HeightGrid g = Grid();
	float cx = 0, cy = 0;
	EXPECT(cu::plotCentre(g, 2, 2, cx, cy).ok());
	UnitsCamera cam = LookAt(cx, cy);
	h.t = 1000;
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	EXPECT(LogCount("figures from the controller") == 1 && LogCount("units: the controller starts at frame 41 with the table's 1 units") == 1);
	// issue 4468: the clip durations were read ahead on the pre-check's thread; the start read no clip on this thread
	EXPECT(LogCount("units: the controller's clip durations read ahead on the pre-check's thread: ") >= 1);
	EXPECT(LogCount("the controller's first frame 41: 1 units held (1 events), 1 drawn, 0 not drawn; the feed's close") == 1);
	EXPECT(LogCount("; clip files read on the render thread 0") >= 1);
	EXPECT(h.probe->passDraws() == 1 && t.covered() > 50 && h.probe->lastBuilt().copies[1].units.size() == 1);
	const std::vector<cu::FigureInput> f0 = h.probe->lastBuilt().copies[1].units[0].figures;
	EXPECT(f0.size() == 12);
	for(const cu::FigureInput& fi : f0)
		EXPECT(!fi.clip.empty() && fi.t >= 0);
	// 0.5 s later: every single-sample figure's clip time moved on
	h.t = 1500;
	cam.frameId = 42;
	t.clear();
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	EXPECT(h.probe->passDraws() == 2 && t.covered() > 50 && h.u->ready() && h.probe->lastBuilt().copies[1].units.size() == 1);
	const std::vector<cu::FigureInput> f1 = h.probe->lastBuilt().copies[1].units[0].figures;
	size_t moved = 0;
	for(const cu::FigureInput& a : f1)
		for(const cu::FigureInput& b : f0)
			if(a.figure == b.figure && a.clip == b.clip && a.t > b.t)
				moved++;
	EXPECT(moved >= 6);
	EXPECT(h.probe->core().counters().byStatus[(int)Status::kAnimFailed] == 0);
	// issue 0439: a work order on the warrior (BUILD_ROAD: the EXE's shovel, 1910, which the warrior's machine never reads)
	// is a no-op: the unit stays drawn (was BadEvent without a work table: AnimFailed, not drawn)
	h.port.units[0].seq = 2;
	h.u->feed(BatchOf({Ev(2, SrvFeed::kUnitWork, 0, 1001, 0)}));
	for(int k = 0; k < 2; ++k)
	{
		h.t += 100;
		cam.frameId++;
		t.clear();
		h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	}
	EXPECT(h.probe->core().counters().byStatus[(int)Status::kAnimFailed] == 0 && t.covered() > 50);
	// the refusal (D118's route): a damage the figure rule refuses (a negative damage; a paradrop would also be refused, but
	// the allocator frees a paradropped unit's seed, so it would not be listed at all)
	h.port.units[0].seq = 3;
	h.u->feed(BatchOf({Ev(3, SrvFeed::kUnitSetDamage, 0, 1001, -5, 0)}));
	for(int k = 0; k < 2; ++k)
	{
		h.t += 100;
		cam.frameId++;
		t.clear();
		h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	}
	EXPECT(h.probe->core().counters().byStatus[(int)Status::kAnimFailed] == 1);
	EXPECT(LogCount("LOUD: units: AnimFailed: unit (0, 1001) UNIT_WARRIOR: the controller: BadEvent") == 1);
	EXPECT(t.covered() == 0 && h.probe->lastBuilt().copies[1].units.size() == 1 && h.probe->lastBuilt().copies[1].units[0].figures.empty());
	char line[4096];
	h.u->totalsLine(line, sizeof(line));
	EXPECT(strstr(line, "| anim on frames=6 ") != nullptr && strstr(line, "AnimFailed=1") != nullptr);
	return true;
}

// Issue 4456: render_units_anim=0: no controller, the module's default clip at t 0 (no figure lines), drawn as before.
bool CaseAnimOff()
{
	UnitsOptions o;
	o.anim = false;
	Host h(TestEnv(false), o);
	h.u->poll();
	h.probe->waitPrecheck();
	FeedWarrior(h);
	h.u->setDevice(g_dev);
	Target t;
	const c5r::heightfield::HeightGrid g = Grid();
	float cx = 0, cy = 0;
	cu::plotCentre(g, 2, 2, cx, cy);
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, LookAt(cx, cy), &g);
	EXPECT(h.probe->passDraws() == 1 && t.covered() > 50 && h.probe->lastBuilt().copies[1].units.size() == 1 &&
	       h.probe->lastBuilt().copies[1].units[0].figures.empty());
	EXPECT(LogCount("figures at the module's default clip (render_units_anim=0)") == 1 && LogCount("the controller starts") == 0);
	char line[4096];
	h.u->totalsLine(line, sizeof(line));
	EXPECT(strstr(line, "| anim off frames=0 ") != nullptr);
	return true;
}

// Issue 4456: the controller cannot open on the database (its bridge tables missing): D125's route (AnimUnavailable, the
// scene failed, never figures of our choosing); the same database with render_units_anim=0 passes.
bool CaseAnimUnavailable()
{
	EnvOpener noTables = [](const std::wstring&, Env& out, std::string& why) {
		const c5r::Status s = out.gf.open(c5r::gamefs::fromUtf8(g_game));
		if(!s.ok())
		{
			why = s.str();
			return false;
		}
		out.db = TestDb(false, false, true);
		out.exe = c5r::gamefs::fromUtf8(g_exe);
		out.install = c5r::gamefs::fromUtf8(g_game);
		return true;
	};
	Host h(noTables);
	h.u->poll();
	std::string why;
	EXPECT(h.u->failed(why) && why.find("units: the unit animation controller cannot open on test.db: the controller's bridge tables") == 0);
	EXPECT(h.probe->core().counters().byStatus[(int)Status::kAnimUnavailable] == 1 && LogCount("LOUD: units: AnimUnavailable: ") == 1);
	EXPECT(!h.probe->precheckRunning() && !h.u->ready());
	UnitsOptions o;
	o.anim = false;
	Host h2(noTables, o);
	h2.u->poll();
	h2.probe->waitPrecheck();
	EXPECT(!h2.u->failed(why) && h2.probe->precheckPassed());
	return true;
}

// Issue 4468: the target's bytes (RGBA8, row by row), for byte-equal pictures.
std::vector<uint8_t> PixelBytes(Target& t)
{
	std::vector<uint8_t> out;
	D3D11_TEXTURE2D_DESC d;
	t.rt->GetDesc(&d);
	d.Usage = D3D11_USAGE_STAGING;
	d.BindFlags = 0;
	d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ID3D11Texture2D* s = nullptr;
	if(FAILED(g_dev->CreateTexture2D(&d, nullptr, &s)))
		return out;
	g_ctx->CopyResource(s, t.rt);
	D3D11_MAPPED_SUBRESOURCE m;
	if(SUCCEEDED(g_ctx->Map(s, 0, D3D11_MAP_READ, 0, &m)))
	{
		for(uint32_t y = 0; y < t.h; ++y)
			out.insert(out.end(), (const uint8_t*)m.pData + (size_t)y * m.RowPitch, (const uint8_t*)m.pData + (size_t)y * m.RowPitch + t.w * 4);
		g_ctx->Unmap(s, 0);
	}
	s->Release();
	return out;
}

// Issue 4468 (unit-controller.md 18): the controller builds programs for the frame's drawn set only; issue 0450 (18.1,
// the EXE's rule settled by 0442): it also steps only the drawn units, a unit not drawn holds its clock. Three hosts take
// the same feed and frames: A with UnitsOptions::animAll (every unit drawn for the controller) and the frames' times, A0
// with animAll and the times held over the frames 1002 is hidden from B (dt 0 there, the same steps after), B the
// product's. Warrior 1001 is in view of camera 1, warrior 1002 of camera 2 only; 1002 is fortified while B does not draw
// it. While camera 1 shows 1001, B's figure lines and picture are byte-equal to A's; from the frame camera 2 shows 1002
// on, they are byte-equal to A0's (1002's clock held while hidden: 0450; before 0450 they equalled A's) and differ from
// A's. On every frame before it B builds no program for 1002 (anim_quiet=1 in the second line) while A does (anim_quiet=0).
bool CaseAnimDrawnSet()
{
	UnitsOptions all;
	all.animAll = true;
	Host a(TestEnv(false), all), a0(TestEnv(false), all), b(TestEnv(false));
	Host* hs[3] = {&a, &a0, &b};
	for(Host* h : hs)
	{
		h->u->poll();
		h->probe->waitPrecheck();
		EXPECT(h->probe->precheckPassed());
		h->port.units = {Rec(0, 1001, 7, 22, 2, cu::kFlagVisible, 0, 100, 1), Rec(0, 1002, 7, 67, 2, cu::kFlagVisible, 0, 100, 2)};
		h->u->feed(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 22), Ev(2, SrvFeed::kUnitCreated, 0, 1002, 7, 0, 67)}));
		h->u->setDevice(g_dev);
	}
	const c5r::heightfield::HeightGrid g = Grid();
	float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
	EXPECT(cu::plotCentre(g, 2, 2, x1, y1).ok() && cu::plotCentre(g, 7, 6, x2, y2).ok());
	Target ta, ta0, tb;
	size_t equalPictures = 0, shownFrames = 0, differFromA = 0;
	auto clockAt = [](int64_t f) { return 1000 + 70.0 * (double)(f - 41) + (f % 3 == 0 ? 210 : 0); };
	for(int64_t frame = 41; frame <= 70; ++frame)
	{
		const bool second = frame >= 58;
		UnitsCamera cam = second ? LookAt(x2, y2) : LookAt(x1, y1);
		cam.frameId = frame;
		if(frame == 47)   // 1002 fortified while B does not draw it
			for(Host* h : hs)
			{
				h->port.units[1].seq = 3;
				h->port.units[1].flags = cu::kFlagVisible | cu::kFlagFortified;
				h->u->feed(BatchOf({Ev(3, SrvFeed::kUnitFortify, 0, 1002, 1)}));
			}
		a.t = b.t = clockAt(frame);   // steps of 70 ms and some over the 0.2 s ceiling
		a0.t = second ? clockAt(41) + clockAt(frame) - clockAt(57) : clockAt(41);   // 0450: held while B hides 1002
		ta.clear();
		ta0.clear();
		tb.clear();
		a.u->drawScene(g_ctx, ta.rtv, ta.dsv, ta.w, ta.h, cam, &g);
		a0.u->drawScene(g_ctx, ta0.rtv, ta0.dsv, ta0.w, ta0.h, cam, &g);
		b.u->drawScene(g_ctx, tb.rtv, tb.dsv, tb.w, tb.h, cam, &g);
		const cu::UnitScene& sall = a.probe->lastBuilt().copies[1];
		const cu::UnitScene& sa = (second ? a0 : a).probe->lastBuilt().copies[1];   // the reference of this frame
		const cu::UnitScene& sb = b.probe->lastBuilt().copies[1];
		EXPECT(sa.units.size() == 1 && sb.units.size() == 1);
		if(sa.units.size() != 1 || sb.units.size() != 1)
			return false;
		EXPECT(sb.units[0].id == (second ? 1002 : 1001));
		EXPECT(sa.units[0].figures.size() == sb.units[0].figures.size() && !sb.units[0].figures.empty());
		for(size_t k = 0; k < sa.units[0].figures.size() && k < sb.units[0].figures.size(); ++k)
		{
			const cu::FigureInput& fa = sa.units[0].figures[k];
			const cu::FigureInput& fb = sb.units[0].figures[k];
			EXPECT(fa.figure == fb.figure && fa.clip == fb.clip && !memcmp(&fa.t, &fb.t, sizeof(float)));
		}
		if(second && sall.units.size() == 1)
		{
			bool same = sall.units[0].figures.size() == sb.units[0].figures.size();
			for(size_t k = 0; same && k < sb.units[0].figures.size(); ++k)
				same = sall.units[0].figures[k].clip == sb.units[0].figures[k].clip &&
				       !memcmp(&sall.units[0].figures[k].t, &sb.units[0].figures[k].t, sizeof(float));
			differFromA += same ? 0 : 1;
		}
		Target& tr = second ? ta0 : ta;
		const std::vector<uint8_t> pa = PixelBytes(tr), pb = PixelBytes(tb);
		EXPECT(!pa.empty() && pa == pb && tr.covered() > 50);
		equalPictures += (!pa.empty() && pa == pb) ? 1 : 0;
		shownFrames += second ? 1 : 0;
		char la[4096], la0[4096], lb[4096];
		a.u->secondLine(la, sizeof(la));
		a0.u->secondLine(la0, sizeof(la0));
		b.u->secondLine(lb, sizeof(lb));
		EXPECT(strstr(la, "anim_drawn=2 anim_quiet=0 ") != nullptr);
		EXPECT(strstr(la0, "anim_drawn=2 anim_quiet=0 ") != nullptr);
		EXPECT(strstr(lb, "anim_drawn=1 anim_quiet=1 ") != nullptr);
	}
	EXPECT(equalPictures == 30 && shownFrames == 13);
	EXPECT(differFromA == shownFrames);   // 0450: the every-unit-advanced rule shows 1002 at another time on every such frame
	for(Host* h : hs)
		EXPECT(h->probe->core().counters().byStatus[(int)Status::kAnimFailed] == 0 && h->u->ready());
	EXPECT(LogCount("units: the controller's first frame 41: 2 units held (2 events), 2 drawn, 0 not drawn") == 2);
	EXPECT(LogCount("units: the controller's first frame 41: 2 units held (2 events), 1 drawn, 1 not drawn") == 1);
	return true;
}

// Issue 4456 (TEST ONLY render_test_units_anim): the controller's input as 0374's file, rewritten at the second line when
// frames were added; 0374's reader reads it back; an unwritable folder is LOUD once and the scene goes on.
bool CaseAnimFile()
{
	char tmp[MAX_PATH];
	GetTempPathA(MAX_PATH, tmp);
	const std::string dir = std::string(tmp) + "c5_4456_animfile_" + std::to_string(GetCurrentProcessId());
	CreateDirectoryA(dir.c_str(), nullptr);
	const std::string file = dir + "\\server.c5anim";
	UnitsOptions o;
	o.animFile = c5r::gamefs::fromUtf8(file);
	Host h(TestEnv(false), o);
	h.u->poll();
	h.probe->waitPrecheck();
	FeedWarrior(h);
	h.u->setDevice(g_dev);
	Target t;
	const c5r::heightfield::HeightGrid g = Grid();
	float cx = 0, cy = 0;
	cu::plotCentre(g, 2, 2, cx, cy);
	UnitsCamera cam = LookAt(cx, cy);
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	h.t += 250;
	cam.frameId = 42;
	h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
	char line[1024];
	h.u->secondLine(line, sizeof(line));
	EXPECT(h.probe->waitTestFiles());   // ticket 5054: the TEST files' worker
	ca::Timeline back;
	cu::UnitScene start;
	const ca::TimelineStatus ts = ca::readTimelineFile(c5r::gamefs::fromUtf8(file), back, start);
	EXPECT(ts.ok() && back.frames.size() == 2 && back.frames[0].events.size() == 1 && back.frames[0].events[0].kind == ca::EventKind::Create);
	EXPECT(back.frames[0].seq == 1 && back.frames[0].exe_frame == 41 && back.frames[1].seq == 2 && back.frames[1].exe_frame == 42 &&
	       back.frames[1].dt == 0.25 && back.players.size() == 1);
	// the folder gone: LOUD once, the scene draws on
	DeleteFileA(file.c_str());
	RemoveDirectoryA(dir.c_str());
	for(int k = 0; k < 2; ++k)
	{
		h.t += 100;
		cam.frameId++;
		h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
		h.u->secondLine(line, sizeof(line));
		h.probe->waitTestFiles();
	}
	EXPECT(LogCount("LOUD: units: TEST KEY render_test_units_anim: ") == 1 && h.probe->passDraws() == 4);
	return true;
}

// Issue 4470: 4463's filter as the one function server.exe and unitfeed both call (FilterByFog), and the fog scene's
// states from a table read (FogStatesOfTable, FowScene::readTable's rule), on hand-made frames: no device, no module.
bool CaseFogFunction()
{
	// players 0 (team 0) and 1 (team 1), active team 0; a unit of each on plot (2, 1) of a 4 x 3 grid in every wrap copy,
	// and player 2 (no row) on plot (0, 0); copy 0 also lists player 1 on (3, 2)
	std::vector<SrvFeed::PlayerRecord> rows = {Player(0, 3, 5, 0), Player(1, 3, 5, 0)};
	for(SrvFeed::PlayerRecord& r : rows)
		r.activeTeam = 0;
	auto unit = [](int p, int id, int x, int y) {
		cu::UnitInput u;
		u.player = p;
		u.id = id;
		u.plot_x = x;
		u.plot_y = y;
		return u;
	};
	auto frame = [&]() {
		BuiltFrame b;
		for(cu::UnitScene& sc : b.copies)
		{
			sc.units = {unit(0, 1001, 2, 1), unit(1, 1101, 2, 1), unit(2, 1201, 0, 0)};
			sc.players.resize(3);
			for(int p = 0; p < 3; ++p)
				sc.players[(size_t)p].player = p;
		}
		b.copies[1].units.push_back(unit(1, 1102, 3, 2));
		b.listed = 4;
		return b;
	};
	auto ids = [](const cu::UnitScene& sc) {
		std::string s;
		for(const cu::UnitInput& u : sc.units)
			s += std::to_string(u.player) + ":" + std::to_string(u.id) + " ";
		return s;
	};
	std::vector<uint8_t> st(4 * 3, 0);
	// every plot visible: nothing hidden
	BuiltFrame b = frame();
	FogFilterResult r = FilterByFog(b, rows, 4, st.data(), st.size(), 4, 3);
	EXPECT(!strcmp(r.state, "on") && !r.why && r.hidden == 0 && b.listed == 4 && ids(b.copies[1]) == "0:1001 1:1101 2:1201 1:1102 ");
	// (2, 1) seen and (0, 0) unexplored: the other team's unit and the unit with no row leave every copy, the active
	// team's stays; the player inputs with no unit left go; a unit counted once over the three copies
	st[1 * 4 + 2] = 1;
	st[0] = 2;
	b = frame();
	r = FilterByFog(b, rows, 4, st.data(), st.size(), 4, 3);
	EXPECT(!strcmp(r.state, "on") && r.hidden == 2 && b.listed == 2);
	EXPECT(ids(b.copies[0]) == "0:1001 " && ids(b.copies[1]) == "0:1001 1:1102 " && ids(b.copies[2]) == "0:1001 ");
	EXPECT(b.copies[0].players.size() == 1 && b.copies[1].players.size() == 2);
	// a unit on a plot outside the grid is not seen
	b = frame();
	b.copies[1].units = {unit(1, 1103, 4, 0), unit(1, 1104, -1, 0)};
	r = FilterByFog(b, rows, 4, st.data(), st.size(), 4, 3);
	EXPECT(r.hidden == 4 && b.copies[1].units.empty());   // 1101, 1201 (copies -1, +1), 1103, 1104
	// negatives: no states, an empty array, no active team, another grid (width, size): nothing filtered, the reason named
	b = frame();
	r = FilterByFog(b, rows, 4, nullptr, 0, 4, 3);
	EXPECT(!strcmp(r.state, "none") && r.why && strstr(r.why, "units: no plot states from the fog scene") && r.hidden == 0 &&
	       b.listed == 4 && b.copies[1].units.size() == 4);
	r = FilterByFog(b, rows, 4, st.data(), 0, 4, 3);
	EXPECT(!strcmp(r.state, "none") && b.copies[1].units.size() == 4);
	std::vector<SrvFeed::PlayerRecord> noTeam = rows;
	for(SrvFeed::PlayerRecord& x : noTeam)
		x.activeTeam = -1;
	r = FilterByFog(b, noTeam, 4, st.data(), st.size(), 4, 3);
	EXPECT(!strcmp(r.state, "no-team") && strstr(r.why, "name no active team") && b.copies[1].units.size() == 4);
	r = FilterByFog(b, {}, 4, st.data(), st.size(), 4, 3);
	EXPECT(!strcmp(r.state, "no-team"));
	r = FilterByFog(b, rows, 5, st.data(), st.size(), 4, 3);
	EXPECT(!strcmp(r.state, "grid") && strstr(r.why, "not of the feed's grid") && b.copies[1].units.size() == 4);
	r = FilterByFog(b, rows, 4, st.data(), st.size() - 1, 4, 3);
	EXPECT(!strcmp(r.state, "grid") && b.listed == 4);

	// FogStatesOfTable: PlotRecord::fow bits 0 revealed, 1 visible -> 0 visible, 1 seen, 2 unexplored (bits above 1 ignored)
	std::vector<SrvFeed::PlotRecord> t(6);
	memset(t.data(), 0, t.size() * sizeof(t[0]));
	const uint8_t bits[6] = {0, 1, 2, 3, 0x05, 0xFE};
	for(size_t i = 0; i < 6; ++i)
		t[i].fow = bits[i];
	std::vector<uint8_t> s;
	EXPECT(FogStatesOfTable(t, 3, 2, s) && s == std::vector<uint8_t>({2, 1, 0, 0, 1, 0}));
	// negatives: an empty table and a table not w x h keep the states (false), as the fog scene keeps its own
	const std::vector<uint8_t> kept = s;
	EXPECT(!FogStatesOfTable({}, 0, 0, s) && s == kept);
	EXPECT(!FogStatesOfTable(t, 2, 2, s) && s == kept);
	EXPECT(!FogStatesOfTable(t, 0x10000, 0x10000, s) && s == kept);
	// the filter on states made from a table: (2, 0) of a 3 x 2 grid is visible (bits 2), (1, 0) seen (bits 1)
	BuiltFrame c;
	c.copies[1].units = {unit(1, 1, 2, 0), unit(1, 2, 1, 0)};
	c.listed = 2;
	r = FilterByFog(c, rows, 3, s.data(), s.size(), 3, 2);
	EXPECT(r.hidden == 1 && ids(c.copies[1]) == "1:1 ");
	return true;
}

// ---------------------------------------------------------- epic 0040, issue 4755: the root position source exe (note 17)
// A kind-6 record as SrvFeed::kUiMirror carries it (srv_remote.h UiRec::ToEvent): plot = the player, turn = 1 arrival / 0
// position, a[0] the unit id, a[1..3] x, y, z as float bits.
SrvFeed::Event Pos(uint32_t seq, int player, int id, float x, float y, float z, bool arrival = false)
{
	SrvFeed::Event e;
	memset(&e, 0, sizeof(e));
	e.seq = seq;
	e.kind = SrvFeed::kUiMirror;
	e.flags = kUiKindUnitPosition;
	e.plot = player;
	e.turn = arrival ? 1 : 0;
	e.a[0] = id;
	e.a[1] = FloatBits(x);
	e.a[2] = FloatBits(y);
	e.a[3] = FloatBits(z);
	return e;
}

SrvFeed::Event Mark(uint32_t seq, int exeFrame, int count)
{
	SrvFeed::Event e;
	memset(&e, 0, sizeof(e));
	e.seq = seq;
	e.kind = SrvFeed::kUiMirror;
	e.flags = kUiKindFrameMark;
	e.plot = e.turn = -1;
	e.a[0] = exeFrame;
	e.a[1] = count;
	return e;
}

// a kind 12 with its path record (one path of `len` plots ending at `to`)
SrvFeed::Batch MoveBatch(uint32_t seq, int owner, int id, int to, std::vector<SrvFeed::Event> more = {})
{
	SrvFeed::Batch b = BatchOf({Ev(seq, SrvFeed::kUnitMoved, owner, id, 0, 2)});
	for(const SrvFeed::Event& e : more)
		b.events.push_back(e);
	SrvFeed::PathEntry pe;
	pe.seq = seq;
	pe.first = 0;
	pe.length = 2;
	b.paths = {pe};
	b.pathPlots = {to - 1, to};
	return b;
}

const cu::UnitInput* UnitOf(const BuiltFrame& f, int id)
{
	for(const cu::UnitInput& u : f.copies[1].units)
		if(u.id == id)
			return &u;
	return nullptr;
}

// render_units_positions -> UnitsOptions::positionsExe of server.exe's scene (MakeLive) and the mode word writer's source,
// both from the one value ParseHostConfig read (lead-0040's answer to queue #4007); refused values by name.
bool CasePositionsSwitch()
{
	auto parse = [](std::map<std::string, std::string> kv, std::string& why) {
		srvrender::HostConfig c;
		return srvrender::ParseHostConfig(
		    [&](const char* k) {
			    auto it = kv.find(k);
			    return it == kv.end() ? std::string() : it->second;
		    },
		    "C:\\game", "C:\\run", c, why);
	};
	EXPECT(!UnitsOptions().positionsExe);
	struct Arm { const char* value; bool exe; };
	const Arm arms[] = {{nullptr, false}, {"own", false}, {"exe", true}, {"own", false}};
	std::string why;
	for(const Arm& a : arms)
	{
		std::map<std::string, std::string> kv;
		if(a.value)
			kv["render_units_positions"] = a.value;
		EXPECT(parse(kv, why));
		// the writer's source: SceneOpts copies the parsed value into SceneOptions::unitsPositions (srv_render_host.cpp)
		srvrender::SceneOptions so;
		so.unitsPositions = srvrender::RenderUnitsPositionsOption();
		std::unique_ptr<ModeWriter> w = MakeModeWriter();
		EXPECT(w);
		w->setOptions(so.unitsReadyHoldMs, so.unitsPositions == srvrender::SceneOptions::kUnitsPositionsExe);
		// the scene: server.exe's factory
		std::unique_ptr<SceneUnits> s = MakeSceneUnitsLive(UnitsOptions());
		UnitsHostProbe* p = dynamic_cast<UnitsHostProbe*>(s.get());
		EXPECT(p && p->core().positionsExe() == a.exe && w->sourceExe() == a.exe);
		char line[2048];
		s->secondLine(line, sizeof(line));
		EXPECT(strstr(line, a.exe ? " pos=exe " : " pos=own ") != nullptr);
	}
	SrvFeed::SetUnitsEnabled(false);   // MakeLive turned the DLL side's writers on
	EXPECT(!parse({{"render_units_positions", "EXE"}}, why) && why == "render_units_positions=EXE: own or exe");
	EXPECT(!parse({{"render_units_positions", "1"}}, why) && why == "render_units_positions=1: own or exe");
	// a caller's own choice is kept (tests, unitfeed): exe without the key
	EXPECT(parse({}, why));
	UnitsOptions o;
	o.positionsExe = true;
	std::unique_ptr<SceneUnits> s = MakeSceneUnitsLive(o);
	SrvFeed::SetUnitsEnabled(false);
	EXPECT(dynamic_cast<UnitsHostProbe*>(s.get())->core().positionsExe());
	return true;
}

// The root: the newest position, an arrival's snap, a unit with no record at its plot; refusals counted; own ignores them.
bool CasePositionsRoot()
{
	FakePort port;
	port.units = {Rec(0, 1001, 7, 23, 4, cu::kFlagVisible, 0, 100, 1), Rec(0, 1002, 8, 5, 1, cu::kFlagVisible, 0, 100, 2)};
	port.players = {Player(0, 3, 5, 0)};
	UnitsCore core(port, Log);
	core.setDatabase(TestLookups(), nullptr);
	core.setPositionsExe(true);
	core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 23), Ev(2, SrvFeed::kUnitCreated, 0, 1002, 8, 0, 5)}), 1000);
	BuiltFrame f;
	core.build(nullptr, nullptr, 1000, f, 10);
	EXPECT(f.copies[1].position_source == "exe" && f.copies[1].units.size() == 2);
	EXPECT(!UnitOf(f, 1001)->has_root && UnitOf(f, 1001)->plot_x == 3 && UnitOf(f, 1001)->plot_y == 2 && !UnitOf(f, 1002)->has_root);
	// two positions in one batch, a frame mark before them: the newest wins
	core.onBatch(BatchOf({Mark(3, 1713, 2), Pos(4, 0, 1001, 100.5f, 200.25f, 3.0f), Pos(5, 0, 1001, 101.5f, 202.25f, 4.0f)}), 1033);
	core.build(nullptr, nullptr, 1033, f, 11);
	const cu::UnitInput* a = UnitOf(f, 1001);
	EXPECT(a && a->has_root && a->root_x == 101.5f && a->root_y == 202.25f && a->root_z == 4.0f && a->plot_x == 3 && a->plot_y == 2);
	float x, y, z;
	EXPECT(core.rootOf(0, 1001, x, y, z) && x == 101.5f && !core.rootOf(0, 1002, x, y, z));
	EXPECT(!UnitOf(f, 1002)->has_root);
	EXPECT(core.posCounters().positions == 2 && core.posCounters().noWalk == 2 && core.posCounters().frameMarks == 1 &&
	       core.posCounters().lastExeFrame == 1713);
	// the arrival snaps it to its plot (no walk open: counted idle, no arrive)
	core.onBatch(BatchOf({Pos(6, 0, 1001, 101.5f, 202.25f, 4.0f, true)}), 1066);
	core.build(nullptr, nullptr, 1066, f, 12);
	EXPECT(!UnitOf(f, 1001)->has_root && core.posCounters().arrivals == 1 && core.posCounters().arrivalsIdle == 1);
	// refusals: a unit the table does not hold, a player outside 0..63, a non-finite value, an arrival field not 0 / 1, a seq
	// not above the last one: counted, nothing applied, the LOUD ones logged
	SrvFeed::Event badTurn = Pos(11, 0, 1002, 1, 2, 3);
	badTurn.turn = 2;
	core.onBatch(BatchOf({Pos(7, 0, 1009, 1, 2, 3), Pos(8, 70, 1001, 1, 2, 3), Pos(9, 0, 1001, std::nanf(""), 2, 3),
	                      Pos(10, 0, 1002, 1, INFINITY, 3), badTurn, Pos(11, 0, 1002, 5, 6, 7), Mark(6, 1, 1)}),
	             1100);
	core.build(nullptr, nullptr, 1100, f, 13);
	const PosCounters& pc = core.posCounters();
	EXPECT(pc.unknownUnit == 1 && pc.bad == 4 && pc.outOfOrder == 2 && pc.positions == 2);
	EXPECT(!UnitOf(f, 1001)->has_root && !UnitOf(f, 1002)->has_root);
	EXPECT(LogCount("LOUD: units: positions: ") == 6 && LogCount("the player is not 0..63") == 1 && LogCount("x, y or z is not finite") == 2 &&
	       LogCount("the arrival field is not 0 or 1") == 1 && LogCount("not above the last taken") == 2 &&
	       LogCount("which the table does not hold") == 1);
	// mode own: the same records are not applied (0375's picture), only counted
	FakePort p2;
	p2.units = port.units;
	p2.players = port.players;
	UnitsCore own(p2, Log);
	own.setDatabase(TestLookups(), nullptr);
	own.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 23), Mark(2, 5, 1), Pos(3, 0, 1001, 100.5f, 200.25f, 3.0f)}), 1000);
	own.build(nullptr, nullptr, 1000, f, 10);
	EXPECT(f.copies[1].position_source == "own" && !UnitOf(f, 1001)->has_root && own.posCounters().ignoredOwn == 1 &&
	       own.posCounters().positions == 0 && own.posCounters().frameMarks == 1);
	return true;
}

// A walk: kind 12 -> move only (the unit at the plot it left until its first position), positions -> the root, the
// arrival -> arrive in a later frame and the unit at its plot; a move and its arrival in one batch: the arrive waits a frame.
bool CasePositionsWalk()
{
	FakePort port;
	port.units = {Rec(0, 1001, 7, 23, 4, cu::kFlagVisible, 0, 100, 1)};
	port.players = {Player(0, 3, 5, 0)};
	UnitsCore core(port, Log);
	core.setDatabase(TestLookups(), nullptr);
	AnimFeed af;
	core.setPositionsExe(true);
	core.setAnimFeed(&af);
	EXPECT(af.positionsExe());
	core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 23)}), 1000);
	core.setLedger(true);
	BuiltFrame bf;
	core.build(nullptr, nullptr, 1000, bf, 20);
	af.start();
	ca::TimelineFrame f;
	std::vector<cu::PlayerInput> pl;
	af.close(core, 1000, 20, f, pl);
	EXPECT(Kinds(f) == "create:1001");
	// the DLL moved it to plot 25 (facing 1) and called GameplayUnitMoved
	port.units[0] = Rec(0, 1001, 7, 25, 1, cu::kFlagVisible, 0, 100, 2);
	core.onBatch(MoveBatch(2, 0, 1001, 25), 1033);
	core.build(nullptr, nullptr, 1033, bf, 21);
	const cu::UnitInput* u = UnitOf(bf, 1001);
	EXPECT(u && !u->has_root && u->plot_x == 3 && u->plot_y == 2 && core.walking(0, 1001));   // held at the plot it left
	af.close(core, 1033, 21, f, pl);
	EXPECT(Kinds(f) == "move:1001" && f.events[0].a == 1 && f.events[0].b == 2);
	// its positions (the first one frame after the move: F7 within two frames)
	core.onBatch(BatchOf({Mark(3, 500, 1), Pos(4, 0, 1001, 150, 140, 2)}), 1066);
	core.build(nullptr, nullptr, 1066, bf, 22);
	u = UnitOf(bf, 1001);
	EXPECT(u && u->has_root && u->root_x == 150 && u->root_y == 140 && u->plot_x == 5 && u->plot_y == 2);
	af.close(core, 1066, 22, f, pl);
	EXPECT(Kinds(f).empty());
	core.onBatch(BatchOf({Mark(5, 501, 1), Pos(6, 0, 1001, 160, 140, 2)}), 1100);
	core.build(nullptr, nullptr, 1100, bf, 23);
	af.close(core, 1100, 23, f, pl);
	// issue 4772 (a behaviour change, lead-0042 #7808): the 10-unit step east turns the unit (created facing 4, west) to 1
	EXPECT(Kinds(f) == "turn:1001" && f.events[0].a == 1 && UnitOf(bf, 1001)->root_x == 160);
	// the arrival: arrive with the record's facing, the unit at its plot centre, the walk closed
	core.onBatch(BatchOf({Mark(7, 502, 1), Pos(8, 0, 1001, 160, 140, 2, true)}), 1133);
	core.build(nullptr, nullptr, 1133, bf, 24);
	EXPECT(!UnitOf(bf, 1001)->has_root && UnitOf(bf, 1001)->plot_x == 5 && !core.walking(0, 1001));
	af.close(core, 1133, 24, f, pl);
	EXPECT(Kinds(f) == "arrive:1001" && f.events[0].a == 1);
	const PosCounters& pc = core.posCounters();
	EXPECT(pc.walks == 1 && pc.moves == 1 && pc.arrived == 1 && pc.inWalk == 2 && pc.positions == 2 && pc.holdFrames == 1 &&
	       pc.firstPosFramesMax == 1 && pc.firstPosOver2 == 0 && af.counters().arrives == 1);
	EXPECT(core.ledger().size() == 1 && core.ledger()[0].kind == SrvFeed::kUnitMoved && core.ledger()[0].seq == 2 &&
	       core.ledger()[0].frame == 20 && core.ledger()[0].firstPosSeq == 4 && core.ledger()[0].firstPosFrame == 21 &&
	       core.ledger()[0].positions == 2 && std::string(core.ledger()[0].end) == "arrival" && core.ledger()[0].endSeq == 8);
	// a move and its arrival in one batch: move in this frame, arrive in the next (never the same frame)
	port.units[0] = Rec(0, 1001, 7, 26, 2, cu::kFlagVisible, 0, 100, 9);
	core.onBatch(MoveBatch(9, 0, 1001, 26, {Pos(10, 0, 1001, 170, 140, 2), Pos(11, 0, 1001, 170, 140, 2, true)}), 1166);
	core.build(nullptr, nullptr, 1166, bf, 25);
	af.close(core, 1166, 25, f, pl);
	EXPECT(Kinds(f) == "move:1001" && af.counters().arrivesDeferred == 1);
	core.build(nullptr, nullptr, 1200, bf, 26);
	af.close(core, 1200, 26, f, pl);
	EXPECT(Kinds(f) == "arrive:1001" && f.events[0].a == 2 && af.counters().arrives == 2);
	// a second kind 12 while walking continues the walk (one walk, two moves)
	core.onBatch(MoveBatch(12, 0, 1001, 27), 1233);
	core.onBatch(MoveBatch(13, 0, 1001, 28), 1233);
	EXPECT(core.posCounters().walks == 3 && core.posCounters().moves == 4 && core.ledger().back().moves == 2);
	// mode own (0375's mapping): move and arrive in one frame
	FakePort p2;
	p2.units = {Rec(0, 1001, 7, 23, 4, cu::kFlagVisible, 0, 100, 1)};
	p2.players = port.players;
	UnitsCore own(p2, Log);
	own.setDatabase(TestLookups(), nullptr);
	AnimFeed af2;
	own.setAnimFeed(&af2);
	own.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 23)}), 1000);
	own.build(nullptr, nullptr, 1000, bf);
	af2.start();
	af2.close(own, 1000, 1, f, pl);
	own.onBatch(MoveBatch(2, 0, 1001, 25), 1033);
	own.build(nullptr, nullptr, 1033, bf);
	af2.close(own, 1033, 2, f, pl);
	EXPECT(!af2.positionsExe() && Kinds(f) == "move:1001,arrive:1001");
	return true;
}

// F9's rule: a walk with no position for kWalkQuietFrames frames (kWalkStartQuietFrames before its first) ends: arrive, the
// unit at its plot, counted; a teleport ends a walk with place (no arrive); a removal and a load end it with nothing;
// frame ids going back restart the count.
bool CasePositionsQuietRule()
{
	FakePort port;
	port.units = {Rec(0, 1001, 7, 23, 4, cu::kFlagVisible, 0, 100, 1), Rec(0, 1002, 7, 24, 4, cu::kFlagVisible, 0, 100, 2)};
	port.players = {Player(0, 3, 5, 0)};
	UnitsCore core(port, Log);
	core.setDatabase(TestLookups(), nullptr);
	AnimFeed af;
	core.setPositionsExe(true);
	core.setAnimFeed(&af);
	core.setLedger(true);
	core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 23), Ev(2, SrvFeed::kUnitCreated, 0, 1002, 7, 0, 24)}), 0);
	BuiltFrame bf;
	core.build(nullptr, nullptr, 0, bf, 100);
	af.start();
	ca::TimelineFrame f;
	std::vector<cu::PlayerInput> pl;
	af.close(core, 0, 100, f, pl);
	// 1001 walks and has one position at frame 100; 1002 walks and never gets one
	SrvFeed::Batch b = MoveBatch(3, 0, 1001, 25, {Ev(4, SrvFeed::kUnitMoved, 0, 1002, 1, 2), Pos(5, 0, 1001, 150, 140, 2)});
	SrvFeed::PathEntry pe;
	pe.seq = 4;
	pe.first = 0;
	pe.length = 2;
	b.paths.push_back(pe);
	core.onBatch(b, 10);
	// the records taken after frame 100 are drawn first in frame 101: F9's clock counts from there (a batch drained after a
	// long draw is not old), so the walk ends at 101 + kWalkQuietFrames + 1
	for(int64_t fr = 101; fr <= 101 + kWalkQuietFrames; ++fr)
		core.build(nullptr, nullptr, (double)fr, bf, fr);
	EXPECT(core.walking(0, 1001) && core.posCounters().quietEnds == 0);
	af.close(core, 131, 131, f, pl);
	EXPECT(Kinds(f) == "move:1001,move:1002");
	core.build(nullptr, nullptr, 132, bf, 101 + kWalkQuietFrames + 1);
	EXPECT(!core.walking(0, 1001) && core.walking(0, 1002) && core.posCounters().quietEnds == 1 && !UnitOf(bf, 1001)->has_root);
	af.close(core, 131, 131, f, pl);
	EXPECT(Kinds(f) == "arrive:1001");
	EXPECT(LogCount("F9: the walk of unit (0, 1001)") == 1 && LogCount("LOUD") == 0);
	// a redraw / a new composite (frame ids back to 5) restarts 1002's count from there
	core.build(nullptr, nullptr, 132, bf, 5);
	for(int64_t fr = 6; fr <= 5 + kWalkStartQuietFrames; ++fr)
		core.build(nullptr, nullptr, (double)fr, bf, fr);
	EXPECT(core.walking(0, 1002));
	core.build(nullptr, nullptr, 300, bf, 5 + kWalkStartQuietFrames + 1);
	EXPECT(!core.walking(0, 1002) && core.posCounters().quietEnds == 2 && LogCount("no first position for") == 1);
	EXPECT(std::string(core.ledger()[1].end) == "quiet" && core.ledger()[1].firstPosSeq == 0);
	// a teleport during a walk: place, no arrive; a removal during a walk: nothing more; a load: every walk ends, roots cleared
	core.onBatch(MoveBatch(6, 0, 1001, 26, {Pos(7, 0, 1001, 151, 141, 2), Ev(8, SrvFeed::kUnitTeleported, 0, 1001, 27)}), 400);
	float x, y, z;
	EXPECT(!core.walking(0, 1001) && core.posCounters().placeEnds == 1 && !core.rootOf(0, 1001, x, y, z));
	core.onBatch(BatchOf({Pos(9, 0, 1001, 152, 142, 2)}), 410);   // the EXE's position after the teleport: applied (no walk)
	EXPECT(core.rootOf(0, 1001, x, y, z) && x == 152 && core.posCounters().noWalk >= 1);
	core.onBatch(MoveBatch(10, 0, 1002, 25), 420);
	port.units.pop_back();
	core.onBatch(BatchOf({Ev(11, SrvFeed::kUnitDestroyed, 0, 1002)}), 430);
	EXPECT(!core.walking(0, 1002) && core.posCounters().removeEnds == 1);
	core.onBatch(MoveBatch(12, 0, 1001, 27), 440);
	EXPECT(core.walking(0, 1001));
	port.units.clear();
	core.onBatch(BatchOf({Ev(13, SrvFeed::kLoadBegin, 2, 0)}), 450);
	EXPECT(!core.walking(0, 1001) && !core.rootOf(0, 1001, x, y, z) && core.walksOpen() == 0 && core.posCounters().loadEnds == 1 &&
	       core.posCounters().loadsCleared == 1);
	af.close(core, 450, 460, f, pl);
	EXPECT(Kinds(f).find("arrive") == std::string::npos);   // place / removal / load give no arrive
	const std::vector<WalkRecord>& L = core.ledger();
	EXPECT(L.size() == 6 && std::string(L[2].end) == "place" && L[3].kind == SrvFeed::kUnitTeleported && L[3].firstPosSeq == 9 &&
	       std::string(L[4].end) == "removed" && std::string(L[5].end) == "load");
	return true;
}

// F7 (contract 6): a position whose seq is below its unit's kind 12 is counted and not applied; a first position more than
// two frames after its kind 12 is counted.
bool CasePositionsF7()
{
	FakePort port;
	port.units = {Rec(0, 1001, 7, 23, 4, cu::kFlagVisible, 0, 100, 1)};
	port.players = {Player(0, 3, 5, 0)};
	UnitsCore core(port, Log);
	core.setDatabase(TestLookups(), nullptr);
	core.setPositionsExe(true);
	core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 23)}), 0);
	BuiltFrame bf;
	core.build(nullptr, nullptr, 0, bf, 20);
	core.onBatch(MoveBatch(10, 0, 1001, 25), 10);
	core.onBatch(BatchOf({Pos(8, 0, 1001, 99, 99, 1)}), 20);   // caused before the move's call
	core.build(nullptr, nullptr, 30, bf, 24);
	EXPECT(core.posCounters().beforeMove == 1 && core.posCounters().positions == 0 && !UnitOf(bf, 1001)->has_root &&
	       LogCount("LOUD: units: positions: F7: position seq 8 of unit (0, 1001) is below its kind 12 seq 10") == 1);
	core.onBatch(BatchOf({Pos(11, 0, 1001, 150, 140, 2)}), 40);   // the first position, 4 frames after the move's frame 20
	EXPECT(core.posCounters().firstPosFramesMax == 4 && core.posCounters().firstPosOver2 == 1 && core.posCounters().inWalk == 1);
	// a long draw (40 frame ids, more than kWalkQuietFrames) with the position drained before the next frame: not quiet
	core.build(nullptr, nullptr, 50, bf, 64);
	EXPECT(core.walking(0, 1001) && core.posCounters().quietEnds == 0);
	core.build(nullptr, nullptr, 60, bf, 64 + kWalkQuietFrames + 1);   // nothing more: quiet from 64
	EXPECT(!core.walking(0, 1001) && core.posCounters().quietEnds == 1);
	return true;
}

// The cull takes the root, not the plot: a unit whose plot is out of view but whose root is in view is drawn, and back.
bool CasePositionsCull()
{
	FakePort port;
	port.units = {Rec(0, 1001, 7, 1 * 10 + 9, 2, cu::kFlagVisible, 0, 100, 1)};   // plot (9, 1): x 608, out of [0, 300]
	port.players = {Player(0, 3, 5, 0)};
	UnitsCore core(port, Log);
	core.setDatabase(TestLookups(), nullptr);
	core.setPositionsExe(true);
	core.onBatch(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7)}), 0);
	const c5r::heightfield::HeightGrid g = Grid();
	BuiltFrame f;
	CullCamera c = Ortho(0);
	core.build(&c, &g, 0, f, 1);
	EXPECT(f.copies[1].units.empty() && f.culled == 1);
	core.onBatch(BatchOf({Pos(2, 0, 1001, 100, 100, 0)}), 10);
	core.build(&c, &g, 10, f, 2);
	EXPECT(f.copies[1].units.size() == 1 && f.copies[1].units[0].has_root && f.culled == 0);
	core.onBatch(BatchOf({Pos(3, 0, 1001, 900, 100, 0)}), 20);   // root out of view: culled although the plot is the same
	core.build(&c, &g, 20, f, 3);
	EXPECT(f.copies[1].units.empty() && f.culled == 1);
	return true;
}

// Ticket 0502 (unit-server-scene.md 18.3): a load's setup is one feed-order allocation however many drains it spans; it
// ends at the first event that is not a setup call, at a lower player, or after a gap of more than kLoadSetupGapSec.
bool CaseLoadSetupRule()
{
	LoadSetup s;
	EXPECT(!LoadSetupGoesOn(s, Ev(1, SrvFeed::kUnitCreated, 0, 1001), 0.0) && !s.active);   // no setup without kLoadBegin
	EXPECT(LoadSetupGoesOn(s, Ev(2, SrvFeed::kLoadBegin, 0, 0), 1.0) && s.active && s.events == 0);
	const uint16_t setupKinds[] = {SrvFeed::kUnitCreated, SrvFeed::kUnitShouldDimFlag, SrvFeed::kUnitFortify, SrvFeed::kUnitSetDamage,
	                               SrvFeed::kUnitWork, SrvFeed::kUnitVisibility, SrvFeed::kUnitGarrison, SrvFeed::kUnitEmbark};
	uint32_t seq = 3;
	for(uint16_t k : setupKinds)
		EXPECT(LoadSetupGoesOn(s, Ev(seq++, k, 0, 1001), 1.1));
	EXPECT(LoadSetupGoesOn(s, Ev(seq++, SrvFeed::kUnitCreated, 1, 2001), 1.5) && s.maxOwner == 1);
	EXPECT(LoadSetupGoesOn(s, Ev(seq++, SrvFeed::kUnitCreated, 1, 2002), 1.5 + kLoadSetupGapSec));   // a gap of exactly 5 s
	EXPECT(s.events == 10);
	// each way to end it
	const uint16_t ends[] = {SrvFeed::kUnitMoved, SrvFeed::kUnitTeleported, SrvFeed::kUnitDestroyed, SrvFeed::kUnitDestroyedInCombat,
	                         SrvFeed::kUnitParadrop, SrvFeed::kUnitActivate, SrvFeed::kUnitCombat, SrvFeed::kCityCombat,
	                         SrvFeed::kUnitMissionEnd, SrvFeed::kUnitRebased, SrvFeed::kUnitResetAnimationState, SrvFeed::kEraChanged};
	for(uint16_t k : ends)
	{
		LoadSetup t;
		LoadSetupGoesOn(t, Ev(1, SrvFeed::kLoadBegin, 0, 0), 0.0);
		EXPECT(!LoadSetupGoesOn(t, Ev(2, k, 0, 1001), 0.1) && !t.active);
		EXPECT(!LoadSetupGoesOn(t, Ev(3, SrvFeed::kUnitCreated, 0, 1002), 0.2));   // ended stays ended
	}
	LoadSetup lower;
	LoadSetupGoesOn(lower, Ev(1, SrvFeed::kLoadBegin, 0, 0), 0.0);
	EXPECT(LoadSetupGoesOn(lower, Ev(2, SrvFeed::kUnitCreated, 5, 1), 0.1));
	EXPECT(!LoadSetupGoesOn(lower, Ev(3, SrvFeed::kUnitCreated, 4, 2), 0.1) && !lower.active);   // a player below the setup's
	LoadSetup gap;
	LoadSetupGoesOn(gap, Ev(1, SrvFeed::kLoadBegin, 0, 0), 0.0);
	EXPECT(LoadSetupGoesOn(gap, Ev(2, SrvFeed::kUnitCreated, 0, 1), 1.0));
	EXPECT(!LoadSetupGoesOn(gap, Ev(3, SrvFeed::kUnitCreated, 0, 2), 1.0 + kLoadSetupGapSec + 0.01) && !gap.active);
	LoadSetup back;
	LoadSetupGoesOn(back, Ev(1, SrvFeed::kLoadBegin, 0, 0), 2.0);
	EXPECT(!LoadSetupGoesOn(back, Ev(2, SrvFeed::kUnitCreated, 0, 1), 1.0));   // a clock that goes back ends it
	return true;
}

// The core with the allocator: a setup of two drains (0502's huge t350 load came in two, the boundary at another unit in
// each run). In the second drain an Embark(true) creation and the second creation on a shared plot keep feed order as
// the EXE allocates them; once a teleport has ended the setup, rule (e) of 12.7.9 applies again.
static bool LoadSetupRun(bool endByTeleport, std::vector<uint32_t>& seeds)
{
	FakePort port;
	port.units = {Rec(0, 1001, 7, 10), Rec(0, 1002, 7, 11), Rec(1, 2001, 7, 20, 2, cu::kFlagVisible | cu::kFlagEmbarked),
	              Rec(1, 2002, 7, 21), Rec(1, 2003, 7, 21), Rec(1, 2004, 7, 22),
	              Rec(2, 3001, 7, 40, 2, cu::kFlagVisible | cu::kFlagEmbarked), Rec(2, 3002, 7, 41)};
	port.players = {Player(0, 3, 5, 0), Player(1, 3, 5, 0), Player(2, 3, 5, 0)};
	UnitsCore core(port, Log);
	core.setDatabase(TestLookups(), nullptr);
	BuiltFrame f;
	core.onBatch(BatchOf({Ev(1, SrvFeed::kLoadBegin, 0, 0)}), 0);
	core.build(nullptr, nullptr, 0, f);
	core.onBatch(BatchOf({Ev(2, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 10), Ev(3, SrvFeed::kUnitShouldDimFlag, 0, 1001, 0, 0, 10),
	                      Ev(4, SrvFeed::kUnitCreated, 0, 1002, 7, 0, 11), Ev(5, SrvFeed::kUnitFortify, 0, 1002, 0, 0, 11)}), 100);
	core.build(nullptr, nullptr, 100, f);
	core.onBatch(BatchOf({Ev(6, SrvFeed::kUnitEmbark, 1, 2001, 1, 0, 20), Ev(7, SrvFeed::kUnitShouldDimFlag, 1, 2001, 0, 0, 20),
	                      Ev(8, SrvFeed::kUnitCreated, 1, 2002, 7, 0, 21), Ev(9, SrvFeed::kUnitShouldDimFlag, 1, 2002, 0, 0, 21),
	                      Ev(10, SrvFeed::kUnitCreated, 1, 2003, 7, 0, 21), Ev(11, SrvFeed::kUnitFortify, 1, 2003, 0, 0, 21),
	                      Ev(12, SrvFeed::kUnitCreated, 1, 2004, 7, 0, 22)}), 700);
	core.build(nullptr, nullptr, 700, f);
	const uint64_t setupEvents = core.counters().loadSetupEvents;
	if(endByTeleport)
		core.onBatch(BatchOf({Ev(13, SrvFeed::kUnitTeleported, 0, 1001, 12, 0, 12)}), 40000);
	core.build(nullptr, nullptr, 40000, f);
	// after the setup (or, without the teleport, 50 s later: the gap ends it): an Embark(true) creation is late again
	core.onBatch(BatchOf({Ev(14, SrvFeed::kUnitEmbark, 2, 3001, 1, 0, 40), Ev(15, SrvFeed::kUnitCreated, 2, 3002, 7, 0, 41)}), 50000);
	core.build(nullptr, nullptr, 50000, f);
	const int32_t keys[][2] = {{0, 1001}, {0, 1002}, {1, 2001}, {1, 2002}, {1, 2003}, {1, 2004}, {2, 3001}, {2, 3002}};
	seeds.clear();
	for(const auto& k : keys)
	{
		uint32_t s = 999;
		EXPECT(core.seedOf((int8_t)k[0], k[1], s));
		seeds.push_back(s);
	}
	EXPECT(core.counters().loadSetups == 1 && setupEvents == 11);
	EXPECT(LogCount(endByTeleport ? "units: load setup 1 ended at kind 13 of player 0 (seq 13): 11 unit events in feed order"
	                              : "units: load setup 1 ended at kind 21 of player 2 (seq 14): 11 unit events in feed order") == 1);
	return true;
}

bool CaseLoadSetupTwoDrains()
{
	std::vector<uint32_t> seeds;
	g_log.clear();
	EXPECT(LoadSetupRun(true, seeds));
	const std::vector<uint32_t> want = {0, 1, 2, 3, 4, 5, 7, 6};   // the setup in feed order; 3001 after 3002 (rule (e))
	EXPECT(seeds == want);
	g_log.clear();
	EXPECT(LoadSetupRun(false, seeds));   // the gap ends the setup: the same seeds
	EXPECT(seeds == want);
	return true;
}

// ------------------------------------------ epic 0042, issue 4772: the walk's facing in mode exe (unit-server-scene.md 19)
// One walk scene: a core in mode exe with its AnimFeed and the drawn side (WalkFacings), stepped frame by frame as the
// unit host steps them (build, the feed's close, follow, apply to the built list).
struct WalkRig
{
	FakePort port;
	UnitsCore core;
	AnimFeed af;
	WalkFacings wf;
	BuiltFrame bf;
	ca::TimelineFrame f;
	std::vector<cu::PlayerInput> pl;
	int64_t frame = 20;
	double t = 1000;
	uint32_t seq = 1;
	std::vector<ca::TimelineFrame> frames;
	explicit WalkRig(int facing, bool exe = true) : core(port, Log)
	{
		port.units = {Rec(0, 1001, 7, 23, facing, cu::kFlagVisible, 0, 100, seq)};
		port.players = {Player(0, 3, 5, 0)};
		core.setDatabase(TestLookups(), nullptr);
		core.setPositionsExe(exe);
		core.setAnimFeed(&af);
		core.onBatch(BatchOf({Ev(seq++, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 23)}), t);
		core.build(nullptr, nullptr, t, bf, frame);
		af.start();
		step();
	}
	// the frame after the batch b (or none): build, close, follow, the drawn facing applied
	void step(const SrvFeed::Batch* b = nullptr)
	{
		t += 33;
		++frame;
		if(b)
			core.onBatch(*b, t);
		core.build(nullptr, nullptr, t, bf, frame);
		af.close(core, t, frame, f, pl);
		wf.follow(f);
		for(cu::UnitScene& sc : bf.copies)
			wf.apply(sc);
		frames.push_back(f);
	}
	void pos(float x, float y, bool arrival = false)
	{
		const uint32_t s = seq;
		seq += 2;
		SrvFeed::Batch b = BatchOf({Mark(s, (int)frame, 1), Pos(s + 1, 0, 1001, x, y, 2, arrival)});
		step(&b);
	}
	void move(int toPlot, int endFacing)
	{
		port.units[0] = Rec(0, 1001, 7, toPlot, endFacing, cu::kFlagVisible, 0, 100, seq);
		SrvFeed::Batch b = MoveBatch(seq++, 0, 1001, toPlot);
		step(&b);
	}
	void teleport(int toPlot)
	{
		port.units[0].plot = toPlot;   // the feed keeps the facing at kind 13 (0449)
		port.units[0].seq = seq;
		SrvFeed::Batch b = BatchOf({Ev(seq++, SrvFeed::kUnitTeleported, 0, 1001, toPlot)});
		step(&b);
	}
	int drawn() const
	{
		const cu::UnitInput* u = UnitOf(bf, 1001);
		return u ? u->facing : -9;
	}
};

// The two rules (unit-server-scene.md 19.2): a direction's heading is 60 - 60 d degrees; the nearest direction, ties to the
// counter-clockwise neighbour; the signed offset in (-180, 180].
bool CaseWalkHeadingMath()
{
	const double pi = 3.14159265358979323846;
	for(int d = 0; d < 6; ++d)
	{
		const double h = (60.0 - 60.0 * d) * pi / 180.0;
		const float x = (float)std::cos(h) * 7.0f, y = (float)std::sin(h) * 7.0f;
		EXPECT(WalkDirection(x, y) == d && std::fabs(WalkOffsetDeg(d, x, y)) < 1e-3f);
		// 29 degrees either side still the same direction; 31 the neighbour
		for(int s = -1; s <= 1; s += 2)
		{
			const double h29 = h + s * 29.0 * pi / 180.0, h31 = h + s * 31.0 * pi / 180.0;
			EXPECT(WalkDirection((float)std::cos(h29), (float)std::sin(h29)) == d);
			EXPECT(WalkDirection((float)std::cos(h31), (float)std::sin(h31)) == (d + 6 - s) % 6);   // left = d - 1
		}
	}
	EXPECT(WalkDirection(1, 0) == 1 && WalkDirection(-1, 0) == 4 && WalkDirection(0, 1) == 5 && WalkDirection(0, -1) == 2);
	EXPECT(WalkDirection(0, 0) == -1 && WalkDirection(std::nanf(""), 1) == -1 && WalkDirection(INFINITY, 0) == -1);
	EXPECT(Near(WalkOffsetDeg(1, -1, 0), 180.0f, 1e-4f) && Near(WalkOffsetDeg(0, 1, 0), -60.0f, 1e-4f) &&
	       Near(WalkOffsetDeg(1, 0.5f, 0.8660254f), 60.0f, 1e-3f) && Near(WalkOffsetDeg(5, 1, 0), -120.0f, 1e-3f));
	EXPECT(WalkOffsetDeg(-1, 1, 0) == 0.0f && WalkOffsetDeg(6, 1, 0) == 0.0f && WalkOffsetDeg(1, 0, 0) == 0.0f);
	return true;
}

// A bent walk: east, a left bend to north-east, a right bend to south-east. The unit faces its old facing until its first
// step, each bend is one turn event with its side, a step under kWalkMinStep accumulates, the drawn facing is the walk's,
// the arrival gives the record's end facing back; the frames go through 0374's writer and reader (the turn records).
bool CaseWalkHeadingBent()
{
	g_log.clear();
	WalkRig r(1);   // created facing east
	EXPECT(Kinds(r.f) == "create:1001" && r.drawn() == 1);
	r.move(25, 0);   // the DLL's record: plot 25, end facing north-east
	EXPECT(Kinds(r.f) == "move:1001" && r.drawn() == 1 && r.wf.walking() == 1);   // not the record's 0 (0040's named difference)
	r.pos(100, 100);   // the first sample: no step
	EXPECT(Kinds(r.f).empty() && r.drawn() == 1);
	r.pos(110, 100);   // east: no turn
	EXPECT(Kinds(r.f).empty() && r.drawn() == 1 && r.af.counters().walkSamples == 1);
	r.pos(110.4f, 100.3f);   // 0.5 units: accumulates, no sample
	EXPECT(Kinds(r.f).empty() && r.af.counters().walkSamples == 1);
	r.pos(115, 108.660254f);   // 60 degrees from (110, 100): a left turn to 0
	EXPECT(Kinds(r.f) == "turn:1001" && r.f.events[0].a == 0 && r.f.events[0].b == 0 && r.drawn() == 0);
	r.pos(120, 117.320508f);   // straight on
	EXPECT(Kinds(r.f).empty() && r.drawn() == 0);
	r.pos(125, 108.660254f);   // -60 degrees: a right turn to 2 (south-east)
	EXPECT(Kinds(r.f) == "turn:1001" && r.f.events[0].a == 2 && r.f.events[0].b == 1 && r.drawn() == 2);
	r.pos(130, 100.0f);
	EXPECT(Kinds(r.f).empty() && r.drawn() == 2);
	r.pos(130, 100.0f, true);   // the arrival: arrive with the record's end facing; drawn as the record
	EXPECT(Kinds(r.f) == "arrive:1001" && r.f.events[0].a == 0 && r.drawn() == 0 && r.wf.walking() == 0 && r.af.walksOpen() == 0);
	const AnimCounters& c = r.af.counters();
	EXPECT(c.turns == 2 && c.turnsRight == 1 && c.walkSamples == 5 && c.walkJumps == 0 && c.walksNoHeading == 0 && c.arrives == 1);
	// the frames as 0374's file and back: the turn records with their side
	AnimRecord rec2;   // the player of the first frame (the start's create)
	std::vector<cu::PlayerInput> pls = {cu::PlayerInput()};
	pls[0].player = 0;
	pls[0].colour = "PLAYERCOLOR_POLYNESIA";
	pls[0].art_style = "ARTSTYLE_POLYNESIAN";
	pls[0].era = "ERA_ANCIENT";
	for(size_t i = 0; i < r.frames.size(); ++i)
		rec2.add(r.frames[i], i == 0 ? pls : std::vector<cu::PlayerInput>());
	char tmp[MAX_PATH];
	GetTempPathA(MAX_PATH, tmp);
	const std::string file = std::string(tmp) + "c5_4772_walk_" + std::to_string(GetCurrentProcessId()) + ".c5anim";
	std::string why;
	EXPECT(rec2.write(c5r::gamefs::fromUtf8(file), why));
	ca::Timeline back;
	cu::UnitScene start;
	const ca::TimelineStatus ts = ca::readTimelineFile(c5r::gamefs::fromUtf8(file), back, start);
	DeleteFileA(file.c_str());
	EXPECT(ts.ok() && back.frames.size() == r.frames.size());
	std::string turns;
	for(const ca::TimelineFrame& x : back.frames)
		for(const ca::TimelineEvent& e : x.events)
			if(e.kind == ca::EventKind::Turn)
				turns += std::to_string(e.a) + (e.b ? "R" : "L") + " ";
	EXPECT(turns == "0L 2R ");
	return true;
}

// The negative cases: a walk with a single record (no step: no turn, the old facing to the arrival, counted), an arrival
// with no walk open (no arrive, no turn), a jump over kWalkMaxStep (no heading, counted; the next step heads again), a
// teleport during a walk (place ends it: positions after it turn nothing), and mode own (no walk, no turn, the record's
// facing drawn).
bool CaseWalkHeadingNegative()
{
	g_log.clear();
	{
		WalkRig r(3);
		r.move(25, 1);
		r.pos(200, 200);
		EXPECT(Kinds(r.f).empty() && r.drawn() == 3);
		r.pos(200, 200, true);
		EXPECT(Kinds(r.f) == "arrive:1001" && r.f.events[0].a == 1 && r.drawn() == 1);
		EXPECT(r.af.counters().turns == 0 && r.af.counters().walksNoHeading == 1 && r.af.counters().walkSamples == 0);
		// an arrival record with no walk open: the core snaps it, no arrive, no turn
		r.pos(210, 200);
		r.pos(220, 200, true);
		EXPECT(Kinds(r.f).empty() && r.af.counters().arrives == 1 && r.af.counters().turns == 0 && r.drawn() == 1 &&
		       r.core.posCounters().arrivalsIdle == 1);
	}
	{
		WalkRig r(1);
		r.move(25, 1);
		r.pos(100, 100);
		r.pos(100 - 600, 100);   // a jump (the wrap seam's step, a dropped stretch): no heading
		EXPECT(Kinds(r.f).empty() && r.af.counters().walkJumps == 1 && r.drawn() == 1);
		r.pos(-500, 110);   // the next step heads again: north (90 degrees, the tie to 5)
		EXPECT(Kinds(r.f) == "turn:1001" && r.f.events[0].a == 5 && r.drawn() == 5);
		// a teleport during the walk: place (the record's facing, kept by the feed), the walk ended; later positions turn
		// nothing
		r.teleport(30);
		EXPECT(Kinds(r.f) == "place:1001" && r.f.events[0].c == 1 && r.af.walksOpen() == 0 && r.wf.walking() == 0 && r.drawn() == 1);
		r.pos(300, 300);
		r.pos(300, 340);
		EXPECT(Kinds(r.f).empty() && r.af.counters().turns == 1 && r.drawn() == 1);
	}
	{
		WalkRig r(1, false);   // mode own: move and arrive in one frame, the positions not applied
		r.move(25, 0);
		EXPECT(Kinds(r.f) == "move:1001,arrive:1001" && r.drawn() == 0 && r.wf.walking() == 0);
		r.pos(100, 100);
		r.pos(100, 140);
		EXPECT(Kinds(r.f).empty() && r.af.counters().turns == 0 && r.af.walksOpen() == 0 && r.drawn() == 0);
	}
	return true;
}

// Quick movement (the DLL's SetPosition per plot: kind 13, never kind 12; the EXE posts its positions after each, two per
// plot in 4755's r1): no walk opens, no turn, the unit keeps the record's facing (the feed keeps it at kind 13, 0449).
bool CaseWalkHeadingQuick()
{
	g_log.clear();
	WalkRig r(4);
	r.teleport(24);
	EXPECT(Kinds(r.f) == "place:1001" && r.f.events[0].c == 4);
	r.pos(100, 100);
	r.pos(110, 100);   // east, as the next plot
	r.teleport(25);
	r.pos(115, 108.660254f);
	r.pos(120, 117.320508f);   // north-east
	EXPECT(r.af.counters().turns == 0 && r.af.walksOpen() == 0 && r.wf.walking() == 0 && r.drawn() == 4);
	for(const ca::TimelineFrame& x : r.frames)
		for(const ca::TimelineEvent& e : x.events)
			EXPECT(e.kind != ca::EventKind::Turn && e.kind != ca::EventKind::Move);
	return true;
}

// ---- 0685 (epic 0673): the walk's turn over time (unit-server-scene.md 19.6) ------------------------------------------
namespace {
ca::TimelineFrame TurnFrame(std::initializer_list<ca::TimelineEvent> evs, double dt)
{
	ca::TimelineFrame f;
	f.dt = dt;
	f.events = evs;
	return f;
}
ca::TimelineEvent TurnEv(ca::EventKind k, int a = 0, int b = 0, int c = 0)
{
	ca::TimelineEvent e;
	e.kind = k;
	e.player = 0;
	e.id = 1001;
	e.unit.player = 0;
	e.unit.id = 1001;
	e.unit.facing = (int8_t)a;
	e.a = a;
	e.b = b;
	e.c = c;
	return e;
}
const int64_t kTurnKey = ((int64_t)0 << 32) | 1001u;
const double kTurnPi = 3.14159265358979323846;
double TurnDeg(float yaw) { return std::remainder((double)yaw, 2.0 * kTurnPi) * 180.0 / kTurnPi; }
// frames of dt until the unit stops turning (at most limit); its drawn yaws in degrees per frame appended to ys
// 0685's first rate (0.75 turns per second): the tests of the turn's mechanics keep it; the default is 19.9's
const std::function<float(int64_t)> kRate075 = [](int64_t) { return 0.75f; };
int TurnFrames(WalkFacings& wf, double dt, std::vector<double>& ys, const std::function<float(int64_t)>& rate = kRate075,
               int limit = 200)
{
	int n = 0;
	for(; n < limit; ++n)
	{
		wf.follow(TurnFrame({}, dt));
		wf.advance(dt, rate);
		auto it = wf.turnYaws().find(kTurnKey);
		if(it == wf.turnYaws().end())
			break;
		ys.push_back(TurnDeg(it->second));
	}
	return n;
}
}  // namespace

// A 60-degree left turn (facing 1 -> 0, yaw 90 -> 150) at 60 fps and 0.75 turns per second (kRate075): 4.5 degrees a
// frame, monotone, 13 turned frames, the 14th at the facing (233 ms counted); then a 120-degree right turn (0 -> 2) in 26
// turned frames; the arrival's facing turns too. The create sets the yaw at once (no turn).
bool CaseWalkTurnSmooth()
{
	WalkFacings wf;
	const double dt = 1.0 / 60.0;
	wf.follow(TurnFrame({TurnEv(ca::EventKind::Create, 1)}, dt));
	wf.advance(dt, kRate075);
	EXPECT(wf.turnYaws().empty() && wf.turnCounters().turns == 0);
	wf.follow(TurnFrame({TurnEv(ca::EventKind::Move, 0, 2)}, dt));
	wf.advance(dt, kRate075);
	EXPECT(wf.turnYaws().empty());   // the move alone keeps the facing it had before the walk
	wf.follow(TurnFrame({TurnEv(ca::EventKind::Turn, 0, 0)}, dt));
	wf.advance(dt, kRate075);
	auto it = wf.turnYaws().find(kTurnKey);
	EXPECT(it != wf.turnYaws().end() && std::fabs(TurnDeg(it->second) - 94.5) < 0.01);
	std::vector<double> ys = {TurnDeg(it->second)};
	const int more = TurnFrames(wf, dt, ys);
	EXPECT(ys.size() == 13 && more == 12);
	for(size_t i = 1; i < ys.size(); ++i)
		EXPECT(ys[i] > ys[i - 1] && ys[i] < 150.0);
	EXPECT(wf.turnCounters().turns == 1 && wf.turnCounters().turnFrames == 13);
	EXPECT(std::fabs(wf.turnCounters().turnMsMax - 14000.0 / 60.0) < 0.5);
	int fc = -1;
	EXPECT(wf.target(kTurnKey, fc) && fc == 0);
	// right by 120 degrees: 150 -> 30, falling
	wf.follow(TurnFrame({TurnEv(ca::EventKind::Turn, 2, 1)}, dt));
	wf.advance(dt, kRate075);
	ys.clear();
	ys.push_back(TurnDeg(wf.turnYaws().at(kTurnKey)));
	TurnFrames(wf, dt, ys);
	EXPECT(ys.size() == 26 && std::fabs(ys[0] - 145.5) < 0.01);
	for(size_t i = 1; i < ys.size(); ++i)
		EXPECT(ys[i] < ys[i - 1] && ys[i] > 30.0);
	EXPECT(wf.turnCounters().turns == 2);
	// the arrival's facing (2 -> 3, 60 degrees right, the short way) turns too, and the walk is over
	wf.follow(TurnFrame({TurnEv(ca::EventKind::Arrive, 3)}, dt));
	wf.advance(dt, kRate075);
	EXPECT(wf.walking() == 0 && wf.turnYaws().size() == 1 && std::fabs(TurnDeg(wf.turnYaws().at(kTurnKey)) - 25.5) < 0.01);
	return true;
}

// The edges: an exactly opposite turn goes the event's side; a teleport (place) sets the yaw at once; a long frame step
// turns by kWalkTurnMaxDt only; remove forgets the unit; a rate from the function (1.1 turns per second: 6.6 degrees a frame);
// a rate not above 0 falls back to the default; off: nothing turns and the facing still switches (4772).
bool CaseWalkTurnEdges()
{
	const double dt = 1.0 / 60.0;
	{
		WalkFacings wf;   // 0 -> 3: 180 degrees, the event says right: the yaw falls (150 -> 145.5)
		wf.follow(TurnFrame({TurnEv(ca::EventKind::Create, 0), TurnEv(ca::EventKind::Move, 3, 2)}, dt));
		wf.advance(dt, kRate075);
		wf.follow(TurnFrame({TurnEv(ca::EventKind::Turn, 3, 1)}, dt));
		wf.advance(dt, kRate075);
		EXPECT(std::fabs(TurnDeg(wf.turnYaws().at(kTurnKey)) - 145.5) < 0.01);
		// 0685's diagnostic text (C5_RENDER_UNITS_WALK_LOG): the unit's facing, side, drawn yaw and goal
		EXPECT(wf.describe(kTurnKey) == "facing 3 side -1 walking 1 yaw 145.5 goal 330.0 turning 1 tan -999.0");   // 0783: tan
		EXPECT(wf.describe(kTurnKey + 1) == "not followed");
		WalkFacings wl;   // the same, left: the yaw rises
		wl.follow(TurnFrame({TurnEv(ca::EventKind::Create, 0), TurnEv(ca::EventKind::Move, 3, 2)}, dt));
		wl.advance(dt, kRate075);
		wl.follow(TurnFrame({TurnEv(ca::EventKind::Turn, 3, 0)}, dt));
		wl.advance(dt, kRate075);
		EXPECT(std::fabs(TurnDeg(wl.turnYaws().at(kTurnKey)) - 154.5) < 0.01);
		// a teleport mid-turn: at the new facing at once
		wf.follow(TurnFrame({TurnEv(ca::EventKind::Place, 7, 23, 1)}, dt));
		wf.advance(dt, kRate075);
		EXPECT(wf.turnYaws().empty() && wf.walking() == 0);
		// a long step: 0.1 s at 0.75 turns per second is 27 degrees (1 -> 0 is 60)
		wf.follow(TurnFrame({TurnEv(ca::EventKind::Move, 0, 2), TurnEv(ca::EventKind::Turn, 0, 0)}, 5.0));
		wf.advance(5.0, kRate075);
		EXPECT(std::fabs(TurnDeg(wf.turnYaws().at(kTurnKey)) - 117.0) < 0.01);
		wf.follow(TurnFrame({TurnEv(ca::EventKind::Remove)}, dt));
		wf.advance(dt, kRate075);
		int fc = -1;
		EXPECT(wf.turnYaws().empty() && !wf.target(kTurnKey, fc));
	}
	{
		WalkFacings wf;   // the rate function: 1.1 turns per second, 6.6 degrees a frame: 60 degrees in 9 turned frames + 1
		wf.follow(TurnFrame({TurnEv(ca::EventKind::Create, 1), TurnEv(ca::EventKind::Move, 0, 2)}, dt));
		wf.advance(dt, kRate075);
		wf.follow(TurnFrame({TurnEv(ca::EventKind::Turn, 0, 0)}, dt));
		wf.advance(dt, [](int64_t) { return 1.1f; });
		std::vector<double> ys = {TurnDeg(wf.turnYaws().at(kTurnKey))};
		TurnFrames(wf, dt, ys, [](int64_t) { return 1.1f; });
		EXPECT(ys.size() == 9 && std::fabs(ys[0] - 96.6) < 0.01);
		// the facing rates in radians per second: 15 / 20 -> 17.5 / 2 pi turns per second (the default); max below min -> min
		EXPECT(std::fabs(WalkTurnRate(15.0f, 20.0f) - 2.7852115f) < 1e-5f && std::fabs(kWalkTurnRateDefault - 2.7852115f) < 1e-5f);
		EXPECT(std::fabs(WalkTurnRate(20.0f, 15.0f) - 3.1830989f) < 1e-5f && WalkTurnRate(0, 0) == kWalkTurnRateDefault);
	}
	{
		WalkFacings wf;   // a rate not above 0 from the function: the default
		wf.follow(TurnFrame({TurnEv(ca::EventKind::Create, 1), TurnEv(ca::EventKind::Move, 0, 2)}, dt));
		wf.advance(dt, kRate075);
		wf.follow(TurnFrame({TurnEv(ca::EventKind::Turn, 0, 0)}, dt));
		wf.advance(dt, [](int64_t) { return -1.0f; });
		EXPECT(std::fabs(TurnDeg(wf.turnYaws().at(kTurnKey)) - 106.711) < 0.01);   // 90 + 16.711 (2.785 turns/s at 60 fps)
		// the default rate: 60 degrees in 3 turned frames + 1 (about 60 ms)
		std::vector<double> ys = {TurnDeg(wf.turnYaws().at(kTurnKey))};
		TurnFrames(wf, dt, ys, nullptr);
		EXPECT(ys.size() == 3 && std::fabs(ys[2] - 140.134) < 0.01);
	}
	{
		WalkFacings wf;   // off: 4772's snap
		wf.setTurnOn(false);
		wf.follow(TurnFrame({TurnEv(ca::EventKind::Create, 1), TurnEv(ca::EventKind::Move, 0, 2)}, dt));
		wf.advance(dt, kRate075);
		wf.follow(TurnFrame({TurnEv(ca::EventKind::Turn, 0, 0)}, dt));
		wf.advance(dt, kRate075);
		EXPECT(!wf.turnOn() && wf.turnYaws().empty() && wf.turnCounters().turns == 0);
		int fc = -1;
		EXPECT(wf.facing(0, 1001, fc) && fc == 0);
	}
	return true;
}

// MergeWalkTurns: a turning unit drawn at its target facing gets the rigid turn (heading, yaw = the drawn yaw), once for
// all three copies; an extra's heading composes; a unit drawn at another facing is left as placed; nothing turning, no
// entry.
bool CaseWalkTurnMerge()
{
	const double dt = 1.0 / 60.0;
	WalkFacings wf;
	wf.follow(TurnFrame({TurnEv(ca::EventKind::Create, 1), TurnEv(ca::EventKind::Move, 0, 2)}, dt));
	wf.advance(dt, kRate075);
	BuiltFrame b;
	cu::UnitInput u;
	u.player = 0;
	u.id = 1001;
	u.facing = 0;
	b.copies[1].units.push_back(u);
	b.copies[2].units.push_back(u);   // the wrap copy: one entry for both
	EXPECT(MergeWalkTurns(wf, b) == 0 && b.xforms.empty());
	wf.follow(TurnFrame({TurnEv(ca::EventKind::Turn, 0, 0)}, dt));
	wf.advance(dt, kRate075);
	EXPECT(MergeWalkTurns(wf, b) == 1 && b.xforms.size() == 1);
	const UnitXform x = b.xforms.at(kTurnKey);
	EXPECT(x.heading && std::fabs(TurnDeg(x.yaw) - 94.5) < 0.01 && x.scale == 1.0f && x.lift == 0.0f);
	// an extra's heading 10 degrees off the facing's composes: 160 + (94.5 - 150) = 104.5
	b.xforms.clear();
	UnitXform e;
	e.heading = true;
	e.yaw = (float)(160.0 * kTurnPi / 180.0);
	e.scale = 1.5f;
	b.xforms[kTurnKey] = e;
	EXPECT(MergeWalkTurns(wf, b) == 1);
	EXPECT(std::fabs(TurnDeg(b.xforms.at(kTurnKey).yaw) - 104.5) < 0.01 && b.xforms.at(kTurnKey).scale == 1.5f);
	// drawn at another facing (a fight's view): as placed
	b.xforms.clear();
	b.copies[1].units[0].facing = 4;
	b.copies[2].units[0].facing = 4;
	EXPECT(MergeWalkTurns(wf, b) == 0 && b.xforms.empty());
	// 0783: the placement lays the drawn yaw (placedYaw): no entry for the walk alone; an extra's heading still composes
	b.copies[1].units[0].facing = 0;
	b.copies[2].units[0].facing = 0;
	EXPECT(MergeWalkTurns(wf, b, true) == 0 && b.xforms.empty());
	UnitXform s;
	s.scale = 1.5f;   // scale only: left alone (ApplyUnitXforms turns nothing, the placement has the yaw)
	b.xforms[kTurnKey] = s;
	EXPECT(MergeWalkTurns(wf, b, true) == 0 && !b.xforms.at(kTurnKey).heading);
	b.xforms[kTurnKey] = e;
	EXPECT(MergeWalkTurns(wf, b, true) == 1 && std::fabs(TurnDeg(b.xforms.at(kTurnKey).yaw) - 104.5) < 0.01);
	return true;
}

namespace {
// 0783: the EXE's root path through a bend as fitted to the laptop's walks (unit-server-scene.md 19.13): from A straight
// to 3/4 plot before B, a quadratic Bezier with control point B to 3/4 plot after B, straight to C. Leg 1 runs west (A
// east of B), leg 2 leaves B at heading 180 - bend degrees (a right turn). Sampled every `step` world units of path.
std::vector<std::pair<float, float>> CurveRoots(double bendDeg, double step)
{
	const double plot = 64.0, a = 0.75, h2 = (180.0 - bendDeg) * kTurnPi / 180.0;
	const double Ax = plot, Ay = 0, Bx = 0, By = 0, Cx = plot * std::cos(h2), Cy = plot * std::sin(h2);
	const double Px = Bx + a * (Ax - Bx), Py = By + a * (Ay - By), Qx = Bx + a * (Cx - Bx), Qy = By + a * (Cy - By);
	std::vector<std::pair<double, double>> fine;
	for(int i = 0; i < 400; ++i)
		fine.push_back({Ax + (Px - Ax) * i / 400.0, Ay + (Py - Ay) * i / 400.0});
	for(int i = 0; i < 4000; ++i)
	{
		const double s = i / 4000.0, w0 = (1 - s) * (1 - s), w1 = 2 * s * (1 - s), w2 = s * s;
		fine.push_back({w0 * Px + w1 * Bx + w2 * Qx, w0 * Py + w1 * By + w2 * Qy});
	}
	for(int i = 0; i <= 400; ++i)
		fine.push_back({Qx + (Cx - Qx) * i / 400.0, Qy + (Cy - Qy) * i / 400.0});
	std::vector<std::pair<float, float>> out = {{(float)fine[0].first, (float)fine[0].second}};
	double acc = 0;
	for(size_t i = 1; i < fine.size(); ++i)
	{
		acc += std::hypot(fine[i].first - fine[i - 1].first, fine[i].second - fine[i - 1].second);
		if(acc >= step)
		{
			out.push_back({(float)fine[i].first, (float)fine[i].second});
			acc = 0;
		}
	}
	out.push_back({(float)Cx, (float)Cy});
	return out;
}

// One walk over the roots at dt a frame, AnimFeed's 30-degree rule giving the turn events (as 4772 measures them), the
// root tracked before advance (AnimRunner::apply's order), the arrival at the end facing; the drawn yaw per frame (the
// facing's when the unit is not in turnYaws) in degrees, and the walk's tangent per frame (the heading + 90).
struct CurveWalk
{
	std::vector<double> yaw, tan;
	int turns = 0;
	bool drawnOk = true;   // drawnYaw agrees with turnYaws every frame
};
CurveWalk WalkCurve(WalkFacings& wf, const std::vector<std::pair<float, float>>& roots, double dt, int endFacing)
{
	CurveWalk w;
	int facing = 4;   // leg 1: west
	wf.follow(TurnFrame({TurnEv(ca::EventKind::Create, 4)}, dt));
	wf.advance(dt, nullptr);
	wf.follow(TurnFrame({TurnEv(ca::EventKind::Move, endFacing, 2)}, dt));
	float sx = roots[0].first, sy = roots[0].second;
	for(size_t i = 0; i < roots.size(); ++i)
	{
		const float x = roots[i].first, y = roots[i].second;
		std::vector<ca::TimelineEvent> ev;
		const float dx = x - sx, dy = y - sy;
		if(i > 0 && std::hypot(dx, dy) >= kWalkMinStep)
		{
			if(std::fabs(WalkOffsetDeg(facing, dx, dy)) > kWalkTurnDeg)
			{
				const bool right = WalkOffsetDeg(facing, dx, dy) < 0;
				facing = WalkDirection(dx, dy);
				ev.push_back(TurnEv(ca::EventKind::Turn, facing, right ? 1 : 0));
				w.turns++;
			}
			w.tan.push_back(std::atan2((double)dy, (double)dx) * 180.0 / kTurnPi + 90.0);
			sx = x;
			sy = y;
		}
		if(i > 0)
		{
			ca::TimelineFrame tf;
			tf.dt = dt;
			tf.events = ev;
			wf.follow(tf);
		}
		wf.track([&](int64_t key, float& rx, float& ry) {
			if(key != kTurnKey)
				return false;
			rx = x;
			ry = y;
			return true;
		});
		wf.advance(dt, nullptr);
		auto it = wf.turnYaws().find(kTurnKey);
		int fc = -1;
		if(!wf.target(kTurnKey, fc))
			w.drawnOk = false;
		w.yaw.push_back(it != wf.turnYaws().end() ? TurnDeg(it->second) : TurnDeg(cu::facingYaw(fc)));
		int dfc = -1;
		float dy2 = 0;
		const bool d = wf.drawnYaw(kTurnKey, dfc, dy2);
		if(d != (it != wf.turnYaws().end()) || (d && (dfc != fc || dy2 != it->second)))
			w.drawnOk = false;
	}
	wf.follow(TurnFrame({TurnEv(ca::EventKind::Arrive, endFacing)}, dt));
	wf.advance(dt, nullptr);
	int fc = -1;
	if(!wf.target(kTurnKey, fc))
		w.drawnOk = false;
	auto it = wf.turnYaws().find(kTurnKey);
	w.yaw.push_back(it != wf.turnYaws().end() ? TurnDeg(it->second) : TurnDeg(cu::facingYaw(fc)));
	return w;
}
double DegDiff(double a, double b) { return std::fabs(std::remainder(a - b, 360.0)); }
double MaxStep(const std::vector<double>& ys)
{
	double m = 0;
	for(size_t i = 1; i < ys.size(); ++i)
		m = (std::max)(m, DegDiff(ys[i], ys[i - 1]));
	return m;
}
}  // namespace

// 0783: through a 60-degree bend on the EXE's curve the drawn yaw follows the root's tangent every frame (stock's arc),
// never more than 4 degrees a frame at 30 fps (foot 3.5, mounted 5 world units a frame; 19.13's laptop speeds), with one
// turn event at the 30-degree point as before (4772) and no turn at the arrival; the 120-degree bend's curve stays under
// 20 degrees a frame (foot 14, mounted 20 at 30 fps); off (C5_RENDER_UNITS_WALK_CURVE=0): 0685's frames (the yaw on the
// discrete facing, one fast turn).
bool CaseWalkCurve()
{
	const double dt = 1.0 / 30.0;
	for(double speed : {119.3 / 34.0, 119.3 / 24.0})
	{
		WalkFacings wf;
		const CurveWalk w = WalkCurve(wf, CurveRoots(60.0, speed), dt, 5);
		EXPECT(w.turns == 1 && w.drawnOk);
		EXPECT(MaxStep(w.yaw) <= 4.5);
		// on the tangent: every frame after the first step, the drawn yaw is the step's tangent
		size_t on = 0;
		for(size_t i = 1; i + 1 < w.yaw.size() && i - 1 < w.tan.size(); ++i)
			on += DegDiff(w.yaw[i], w.tan[i - 1]) < 0.01 ? 1 : 0;
		EXPECT(on + 2 >= w.tan.size());
		EXPECT(DegDiff(w.yaw.front(), -90.0) < 0.01 && DegDiff(w.yaw.back(), -150.0) < 0.01);   // west, then north-west
		EXPECT(wf.turnYaws().empty() && wf.walking() == 0 && wf.turnCounters().turns == 0);    // no rate-limited turn
		EXPECT(wf.turnCounters().tanSamples > 20 && wf.turnCounters().tanFrames > 20);
		// the yaw falls monotonically (a right turn): never back
		for(size_t i = 1; i < w.yaw.size(); ++i)
			EXPECT(std::remainder(w.yaw[i] - w.yaw[i - 1], 360.0) <= 1e-6);
	}
	{
		WalkFacings wf;   // 120 degrees, foot and mounted speeds
		const CurveWalk f = WalkCurve(wf, CurveRoots(120.0, 119.3 / 34.0), dt, 0);
		EXPECT(f.drawnOk && MaxStep(f.yaw) < 15.0 && DegDiff(f.yaw.back(), 150.0) < 0.01);
		WalkFacings wm;
		const CurveWalk m = WalkCurve(wm, CurveRoots(120.0, 119.3 / 24.0), dt, 0);
		EXPECT(m.drawnOk && MaxStep(m.yaw) < 20.0);
	}
	{
		WalkFacings wf;   // off: the discrete facing's yaw and one fast turn (0685)
		wf.setCurveOn(false);
		const CurveWalk w = WalkCurve(wf, CurveRoots(60.0, 119.3 / 34.0), dt, 5);
		EXPECT(w.turns == 1 && MaxStep(w.yaw) > 30.0 && wf.turnCounters().tanFrames == 0);
	}
	{
		// a jump (over kWalkMaxStep) is no tangent; the walk's tangent ends with the arrival; a new walk samples afresh
		WalkFacings wf;
		const double d = 1.0 / 30.0;
		wf.follow(TurnFrame({TurnEv(ca::EventKind::Create, 4), TurnEv(ca::EventKind::Move, 4, 2)}, d));
		auto at = [&](float x, float y) {
			wf.follow(TurnFrame({}, d));
			wf.track([&](int64_t, float& rx, float& ry) {
				rx = x;
				ry = y;
				return true;
			});
			wf.advance(d, nullptr);
		};
		at(0, 0);
		at(-3, 0);
		EXPECT(wf.turnYaws().empty());   // west = facing 4's yaw: on the facing
		at(-3, 300);                     // a jump north: refused, the tangent kept (west)
		EXPECT(wf.turnYaws().empty());
		at(-6, 301);                     // west-north-west: 18 degrees off the facing
		EXPECT(wf.turnYaws().size() == 1 && DegDiff(TurnDeg(wf.turnYaws().at(kTurnKey)), -90.0 - 18.43) < 0.1);
		EXPECT(wf.describe(kTurnKey).find(" tan -108.4") != std::string::npos);
		wf.follow(TurnFrame({TurnEv(ca::EventKind::Arrive, 4)}, d));
		wf.advance(d, nullptr);
		EXPECT(wf.turnYaws().empty());   // the arrival: back to the facing (18 degrees in one frame at the default rate)
		at(-9, 302);                     // not walking: no tangent
		EXPECT(wf.turnYaws().empty());
	}
	return true;
}

// Module: the unit host in mode exe (server.exe's path: beginFrame -> build -> animFrame -> fillFigures -> prepare): the
// drawn list carries the walk's facing, the controller takes the turn events (no refusal), the arrival restores the record's.
bool CaseWalkHeadingHost()
{
	UnitsOptions o;
	o.positionsExe = true;
	Host h(TestEnv(false), o);
	h.u->poll();
	h.probe->waitPrecheck();
	EXPECT(h.probe->precheckPassed());
	h.port.units = {Rec(0, 1001, 7, 2 * 10 + 2, 1, cu::kFlagVisible, 0, 100, 1)};
	h.u->feed(BatchOf({Ev(1, SrvFeed::kUnitCreated, 0, 1001, 7, 0, 22)}));
	h.u->setDevice(g_dev);
	Target t;
	const c5r::heightfield::HeightGrid g = Grid();
	float cx = 0, cy = 0;
	EXPECT(cu::plotCentre(g, 2, 2, cx, cy).ok());
	UnitsCamera cam = LookAt(cx, cy);
	auto frame = [&](const SrvFeed::Batch* b) {
		if(b)
			h.u->feed(*b);
		h.t += 33;
		cam.frameId++;
		t.clear();
		h.u->drawScene(g_ctx, t.rtv, t.dsv, t.w, t.h, cam, &g);
		const BuiltFrame& bf = h.probe->lastBuilt();
		return bf.copies[1].units.size() == 1 ? (int)bf.copies[1].units[0].facing : -9;
	};
	EXPECT(frame(nullptr) == 1);
	h.port.units = {Rec(0, 1001, 7, 2 * 10 + 3, 0, cu::kFlagVisible, 0, 100, 2)};   // moved to (3, 2), end facing 0
	SrvFeed::Batch mv = MoveBatch(2, 0, 1001, 23);
	EXPECT(frame(&mv) == 1);
	SrvFeed::Batch p1 = BatchOf({Mark(3, 1, 1), Pos(4, 0, 1001, cx, cy, 0)});
	EXPECT(frame(&p1) == 1);
	SrvFeed::Batch p2 = BatchOf({Mark(5, 2, 1), Pos(6, 0, 1001, cx + 5, cy + 8.660254f, 0)});
	EXPECT(frame(&p2) == 0);   // the left turn to north-east
	// 0783: a step 15 degrees right of north-east (heading 45): no turn event, the facing stays 0, and the runner's
	// placement lays the figures at the step's tangent (yaw 135, not facing 0's 150)
	const AnimRunner* ar0 = h.probe->animRunner();
	EXPECT(ar0 != nullptr);
	const uint64_t placed0 = ar0->walkYawPlaced();
	SrvFeed::Batch p2b = BatchOf({Mark(7, 3, 1), Pos(8, 0, 1001, cx + 10, cy + 13.660254f, 0)});
	EXPECT(frame(&p2b) == 0);
	EXPECT(ar0->walkYawPlaced() > placed0);
	{
		int fc = -1;
		float yaw = 0;
		EXPECT(ar0->walkFacings().drawnYaw(UnitKeyOf(0, 1001), fc, yaw) && fc == 0 && std::fabs(TurnDeg(yaw) - 135.0) < 0.01);
	}
	SrvFeed::Batch p3 = BatchOf({Mark(9, 4, 1), Pos(10, 0, 1001, cx + 20, cy + 13.660254f, 0)});
	EXPECT(frame(&p3) == 1);   // the right turn to east
	EXPECT(h.u->ready() && t.covered() > 50 && h.probe->core().counters().byStatus[(int)Status::kAnimFailed] == 0);
	SrvFeed::Batch ar = BatchOf({Mark(11, 5, 1), Pos(12, 0, 1001, cx + 20, cy + 13.660254f, 0, true)});
	EXPECT(frame(&ar) == 0);   // the record's end facing
	EXPECT(LogCount("AnimFailed") == 0 && LogCount("refused frame") == 0);
	return true;
}
#include "test_scene_units_fights.inc"   // issue 4774: the fight_* cases
#include "test_scene_units_merge.inc"    // ticket 4724 item 0: where 4772 and 4774 meet (the merge_* cases)
#include "test_scene_units_motion.inc"   // ticket 4724: the fights' figure motion (the motion_* cases)
#include "test_scene_units_walkopen.inc" // ticket 5066: the walk home when the attacker's walk is open (walkopen_* cases)
#include "test_scene_units_afterwalk.inc"   // ticket 5065: the hold behind an open walk (the afterwalk_* cases)

struct Case
{
	const char* name;
	bool (*fn)();
	bool module;   // needs C5R_GAME_DIR, C5R_EXE and the WARP device
};
const Case kCases[] = {
    {"mapping", CaseMapping, false},
    {"fades", CaseFades, false},
    {"cull", CaseCull, false},
    {"build_art_once_per_owner", CaseBuildArtOncePerOwner, false},   // ticket 5056
    {"pass_camera", CasePassCamera, false},
    {"colour_unknown", CaseColourUnknown, false},
    {"player_unknown", CasePlayerUnknown, false},
    {"unit_type_unknown", CaseUnitTypeUnknown, false},
    {"player_art_unknown", CasePlayerArtUnknown, false},
    {"plot_unknown", CasePlotUnknown, false},
    {"seed_missing", CaseSeedMissing, false},
    {"feed_out_of_order", CaseFeedOutOfOrder, false},
    {"allocator_statuses", CaseAllocatorStatuses, false},
    {"minor_civ_art", CaseMinorCivArt, false},
    {"combat_hold", CaseCombatHold, false},
    {"combat_out_of_batch", CaseCombatOutOfBatch, false},
    {"visible_at_load", CaseVisibleAtLoad, false},
    {"embarked_row", CaseEmbarkedRow, false},
    {"embarked_lookups", CaseEmbarkedLookups, false},
    {"embarked_art", CaseEmbarkedArt, false},
    {"embarked_bits", CaseEmbarkedBits, false},
    {"table_file", CaseTableFile, false},
    {"switch", CaseSwitch, false},
    {"precheck_pass_draw", CasePrecheckPassAndDraw, true},
    {"precheck_fail_route", CasePrecheckFailRoute, true},
    {"precheck_clip_missing_in_install", CasePrecheckClipMissingInInstall, true},
    {"database_unreadable", CaseDatabaseUnreadable, true},
    {"unit_refused", CaseUnitRefused, true},
    {"frame_failed", CaseFrameFailed, true},
    {"open_async", CaseOpenAsync, true},   // ticket 0704
    {"dump", CaseDump, true},
    {"reflection_pass", CaseReflectionPass, true},
    {"reflection_matrix_missing", CaseReflectionMatrixMissing, true},
    {"mask_pass", CaseMaskPass, true},   // ticket 4785
    {"water_reflection_target", CaseWaterReflectionTarget, true},
    {"anim_mapping", CaseAnimMapping, false},          // issue 4456
    {"anim_embark_facing7", CaseAnimEmbarkFacing7, false},   // issue 4739
    {"anim_record_kept", CaseAnimRecordKept, false},
    {"anim_record_gone", CaseAnimRecordGone, false},
    {"anim_unformed_retry", CaseAnimUnformedRetry, false},
    {"anim_facing7_host", CaseAnimFacing7Host, true},
    {"drain_clock", CaseDrainClock, false},
    {"recording_port", CaseRecordingPort, false},
    {"cull_of", CaseCullOf, false},
    {"anim_controller", CaseAnimController, true},
    {"anim_off", CaseAnimOff, true},
    {"anim_unavailable", CaseAnimUnavailable, true},
    {"anim_file", CaseAnimFile, true},
    {"anim_drawn_set", CaseAnimDrawnSet, true},       // issue 4468
    {"fog_function", CaseFogFunction, false},   // issue 4470
    {"positions_switch", CasePositionsSwitch, false},   // epic 0040, issue 4755
    {"positions_root", CasePositionsRoot, false},
    {"positions_walk", CasePositionsWalk, false},
    {"positions_quiet_rule", CasePositionsQuietRule, false},
    {"positions_f7", CasePositionsF7, false},
    {"positions_cull", CasePositionsCull, false},
    {"load_setup_rule", CaseLoadSetupRule, false},     // ticket 0502
    {"load_setup_two_drains", CaseLoadSetupTwoDrains, false},
    {"walk_heading_math", CaseWalkHeadingMath, false},   // epic 0042, issue 4772
    {"walk_heading_bent", CaseWalkHeadingBent, false},
    {"walk_heading_negative", CaseWalkHeadingNegative, false},
    {"walk_heading_quick", CaseWalkHeadingQuick, false},
    {"walk_heading_host", CaseWalkHeadingHost, true},
    {"walk_turn_smooth", CaseWalkTurnSmooth, false},   // 0685 (epic 0673)
    {"walk_turn_edges", CaseWalkTurnEdges, false},
    {"walk_turn_merge", CaseWalkTurnMerge, false},
    {"walk_curve", CaseWalkCurve, false},   // 0783 (epic 0673)
    // issue 4774 (epic 0042): the per-unit hold and the fight player (test_scene_units_fights.inc)
    {"fight_hold", CaseFightHold, false},
    {"fight_quick_combat", CaseFightQuickCombat, false},
    {"fight_no_player", CaseFightNoPlayer, false},
    {"fight_two_on_one_unit", CaseFightTwoOnOneUnit, false},
    {"fight_load", CaseFightLoad, false},
    {"fight_refused", CaseFightRefused, false},
    {"fight_overflow", CaseFightOverflow, false},
    {"fight_late_result", CaseFightLateResult, false},   // issue 4783: a late event id through Batch::lateResults
    {"fight_controller", CaseFightController, true},
    {"fight_strike_rule", CaseFightStrikeRule, true},   // 4783 (W33, C1; MC #8906)
    {"fight_limit_line", CaseFightLimitLine, true},     // 4733 (W36): the 12 s line's types and figure states
    {"fight_city_shot", CaseFightCityShot, true},       // ticket 5062 (W98): a city's strike named by its owner's era
    {"fight_city_flight", CaseFightCityFlight, true},   // ticket 5062 (W107): flown from the city's centre (kind 28 a[3])
    // ticket 4724 item 0: the 4774 + 4772 merge (test_scene_units_merge.inc)
    {"merge_walk_held_fight", CaseMergeWalkHeldFight, false},
    {"merge_released_arrive", CaseMergeReleasedArrive, false},
    {"merge_walk_held_fight_on", CaseMergeWalkHeldFightOn, false},   // ticket 5066 (a): the combat waits behind the walk
    {"merge_released_arrive_on", CaseMergeReleasedArriveOn, false},  // ticket 5066 (a): B behind A's held move
    // ticket 4724: the fights' figure motion (test_scene_units_motion.inc)
    {"motion_melee", CaseMotionMelee, true},
    {"motion_no_grid", CaseMotionNoGrid, true},
    {"motion_others", CaseMotionOthers, true},   // issue 4726: the interceptor and a damage member through the adapter
    {"fight_overkill", CaseFightOverkill, true},   // issue 4726: an unclamped final damage (found live) plays
    {"motion_hidden_return", CaseMotionHiddenReturn, true},   // issue 4729: a unit hidden in its fight walks back drawn
    // ticket 5066: the walk home when the attacker's walk is open at its fight's start (test_scene_units_walkopen.inc)
    {"walkopen_move_attack_rush", CaseWalkOpenMoveAttackRush, true},
    {"walkopen_move_attack_foot", CaseWalkOpenMoveAttackFoot, true},
    {"walkopen_move_attack_later", CaseWalkOpenMoveAttackLater, true},
    {"walkopen_arrived_first", CaseWalkOpenArrivedFirst, true},
    {"walkopen_kill_advance", CaseWalkOpenKillAdvance, true},
    {"walkopen_kill_advance_off", CaseWalkOpenKillAdvanceOff, true},
    {"walkopen_b_behind_a", CaseWalkOpenBehindA, true},
    // ticket 5066 (a): the knob's off arms (the before numbers) and the defender killed in the move-attack's drain
    {"walkopen_move_attack_rush_off", CaseWalkOpenMoveAttackRushOff, true},
    {"walkopen_move_attack_foot_off", CaseWalkOpenMoveAttackFootOff, true},
    {"walkopen_move_attack_later_off", CaseWalkOpenMoveAttackLaterOff, true},
    {"walkopen_b_behind_a_off", CaseWalkOpenBehindAOff, true},
    {"walkopen_defender_dies", CaseWalkOpenDefenderDies, true},
    {"walkopen_defender_dies_off", CaseWalkOpenDefenderDiesOff, true},
    {"walkopen_feed", CaseWalkOpenFeed, false},   // ticket 5066 (a): order and bounds at feed level
    {"walkopen_two_walks", CaseWalkOpenTwoWalks, false},   // ticket 5066 (a): a combat waiting behind two walks
    {"walkopen_later_combat_first", CaseWalkOpenLaterCombatFirst, false},   // ticket 0742: a waiting combat comes back
                                                                             // below a later combat's seq (the owner's watchdog)
    {"walkopen_later_combat_first_off", CaseWalkOpenLaterCombatFirstOff, false},   // ticket 0742: fight after walk off
    {"afterwalk_feed", CaseAfterWalkFeed, false},               // ticket 5065: work / fortify wait behind an open walk
    {"afterwalk_worker_host", CaseAfterWalkWorkerHost, true},   // ticket 5065: the worker runs, then digs (product path)
};

// ------------------------------------------------------------------------------------- issue 4460: the pre-check mode
// test_scene_units.exe precheck --db FILE --install DIR --exe EXE [--mods-auto | --mod DIR ...] --out TSV
// The scene's own pre-check (srv_units_scene.cpp PrecheckRun: LiveEnvOpener, a CPU-only UnitRenderer, precheck) on one
// database, on this thread: every reached art row's status into TSV (unit_type, art_style, era, art_row, status,
// warnings), then one line with the row count, failures, warnings, the wall time and the process's peak commit and peak
// working set before and after (the pre-check is the only work of the process). Exit 0 every row Ok, 1 a row failed
// or none reached, 2 bad arguments, 3 the environment did not open.
int Precheck(int argc, char** argv)
{
	std::string db, install, exe, out;
	bool modsAuto = false;
	std::vector<std::wstring> mods;
	for(int i = 2; i < argc; ++i)
	{
		const std::string a = argv[i];
		if(a == "--mods-auto")
			modsAuto = true;
		else if(i + 1 < argc && a == "--db")
			db = argv[++i];
		else if(i + 1 < argc && a == "--install")
			install = argv[++i];
		else if(i + 1 < argc && a == "--exe")
			exe = argv[++i];
		else if(i + 1 < argc && a == "--out")
			out = argv[++i];
		else if(i + 1 < argc && a == "--mod")
			mods.push_back(c5r::gamefs::fromUtf8(argv[++i]));
		else
		{
			printf("precheck: unknown or incomplete argument '%s'\n", a.c_str());
			return 2;
		}
	}
	if(db.empty() || install.empty() || exe.empty() || out.empty())
	{
		printf("precheck: needs --db --install --exe --out\n");
		return 2;
	}
	PROCESS_MEMORY_COUNTERS_EX m0 = {}, m1 = {};
	m0.cb = m1.cb = sizeof(m0);
	GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&m0, sizeof(m0));
	UnitsOptions o;
	o.install = c5r::gamefs::fromUtf8(install);
	o.exe = c5r::gamefs::fromUtf8(exe);
	o.modsAuto = modsAuto;
	o.mods = mods;
	const int64_t t0 = srvrender::Qpc();
	Env env;
	std::string why;
	if(!LiveEnvOpener(o)(c5r::gamefs::fromUtf8(db), env, why))
	{
		printf("precheck: the environment did not open: %s\n", why.c_str());
		return 3;
	}
	const double msEnv = srvrender::QpcToMs(srvrender::Qpc() - t0);
	std::vector<cu::UnitTypeStatus> rows;
	c5r::Status st;
	{
		cu::UnitRenderer r;
		const c5r::Status os = r.open(nullptr, env.gf, *env.db, env.exe, env.install);
		if(!os.ok())
		{
			printf("precheck: the unit module did not open: %s\n", os.str().c_str());
			return 3;
		}
		st = r.precheck(rows);
	}
	const double ms = srvrender::QpcToMs(srvrender::Qpc() - t0);
	GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&m1, sizeof(m1));
	FILE* f = fopen(out.c_str(), "wb");
	if(!f)
	{
		printf("precheck: cannot write %s\n", out.c_str());
		return 2;
	}
	fprintf(f, "unit_type\tart_style\tera\tart_row\tstatus\twarnings\n");
	size_t bad = 0, warn = 0;
	std::map<std::string, size_t> byCode, warnKinds;
	for(const cu::UnitTypeStatus& s : rows)
	{
		std::string w;
		for(const std::string& x : s.status.warnings)
			w += (w.empty() ? "" : " | ") + x;
		warn += s.status.warnings.size();
		for(const std::string& x : s.status.warnings)
			warnKinds[x.substr(0, x.find(':'))]++;
		bad += s.status.ok() ? 0 : 1;
		byCode[cu::unitCodeName(s.status.code)]++;
		fprintf(f, "%s\t%s\t%s\t%s\t%s\t%s\n", s.unit_type.c_str(), s.art_style.c_str(), s.era.c_str(), s.art_row.c_str(),
		        s.status.str().c_str(), w.c_str());
	}
	fclose(f);
	std::string codes;
	for(const auto& kv : byCode)
		codes += (codes.empty() ? "" : ", ") + kv.first + " " + std::to_string(kv.second);
	for(const auto& kv : warnKinds)
		codes += ", warning " + kv.first + " " + std::to_string(kv.second);
	printf("precheck: status %s; %zu art rows, %zu failed, %zu warnings (%s); %.0f ms (environment %.0f ms); peak commit %.1f -> %.1f MB, "
	       "peak working set %.1f -> %.1f MB\n",
	       st.str().c_str(), rows.size(), bad, warn, codes.c_str(), ms, msEnv, m0.PeakPagefileUsage / 1048576.0,
	       m1.PeakPagefileUsage / 1048576.0, m0.PeakWorkingSetSize / 1048576.0, m1.PeakWorkingSetSize / 1048576.0);
	return st.ok() && !bad && !rows.empty() ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv)
{
	srvrender::LogOpen(nullptr, nullptr);
	if(argc >= 2 && !strcmp(argv[1], "oracle"))
		return Oracle(argc, argv);
	if(argc >= 2 && !strcmp(argv[1], "precheck"))   // issue 4460
		return Precheck(argc, argv);
	const char* gd = getenv("C5R_GAME_DIR");
	const char* ex = getenv("C5R_EXE");
	g_game = gd ? gd : "";
	g_exe = ex ? ex : "";
	bool needModule = false;
	for(const Case& c : kCases)
	{
		bool want = argc < 2;
		for(int i = 1; i < argc; ++i)
			want |= !strcmp(argv[i], c.name);
		needModule |= want && c.module;
	}
	if(needModule)
	{
		if(g_game.empty() || g_exe.empty())
		{
			printf("test_scene_units: C5R_GAME_DIR and C5R_EXE must name the install and its CivilizationV_DX11.exe\n");
			return 99;
		}
		const D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
		if(FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &g_dev, nullptr, &g_ctx)))
		{
			printf("test_scene_units: no WARP device\n");
			return 99;
		}
	}
	int fails = 0, run = 0;
	for(const Case& c : kCases)
	{
		bool want = argc < 2;
		for(int i = 1; i < argc; ++i)
			want |= !strcmp(argv[i], c.name);
		if(!want)
			continue;
		g_log.clear();
		g_fail.clear();
		const bool ok = c.fn();
		run++;
		fails += ok ? 0 : 1;
		printf("CASE %s %s%s%s\n", c.name, ok ? "PASS" : "FAIL", ok ? "" : " ", g_fail.c_str());
		if(!ok)
			for(const std::string& l : g_log)
				printf("    log: %s\n", l.c_str());
		fflush(stdout);
	}
	if(!run)
	{
		printf("test_scene_units: no case matches\n");
		return 2;
	}
	if(g_ctx)
		g_ctx->Release();
	if(g_dev)
		g_dev->Release();
	printf("test_scene_units: %d of %d cases passed\n", run - fails, run);
	return fails ? 1 : 0;
}
