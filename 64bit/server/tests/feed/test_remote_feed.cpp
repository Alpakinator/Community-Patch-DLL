// test_remote_feed.cpp - the DLL side of the unit feed (server/src/remote/srv_remote_feed.cpp, issue 4452, epic 0375) on
// stand-in DLL objects. Written 2026-11-15 for this repository (GPL-3.0-or-later).
// Built and run by server/tests/feed/run_remote_feed_test.py with the server's include folders and the fork's interface
// headers; the stand-ins (standins.gen.h, gen_standins.py) override every pure virtual, and every method a test does not
// model reports itself through StandHit: a call of the code under test outside the getters it may use is a failure.
// One test per kind 11-33 (event values, table effect, side-table entry), the units-off rule, the NULL arguments, an
// exception in a getter, the player rows, the load mark, dropped unit events, and the report totals.
// Exit 0 when every check passed, 1 otherwise; -DFEED_TEST_INJECT_FAIL adds one failing check.
#include "srv_remote.h"
#include "srv_remote_feed.h"
#include "feed/srv_feed.h"
#include "feed/srv_feed_server.h"
#include "standins.gen.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <set>
#include <string>
#include <vector>

static int g_pass = 0, g_fail = 0;
static const char* g_test = "";
#define CHECK(cond)                                                                                                   \
	do                                                                                                                \
	{                                                                                                                 \
		if (cond) ++g_pass;                                                                                           \
		else { ++g_fail; printf("FAIL %s: %s (line %d)\n", g_test, #cond, __LINE__); }                                \
	} while (0)

// ---- the server functions srv_remote_feed.cpp uses (stubs) ----
static std::vector<std::string> g_log;
static std::vector<std::string> g_hits;   // stand-in methods called that no test modelled
void SrvLog(const char* szFormat, ...)
{
	char sz[2048];
	va_list v;
	va_start(v, szFormat);
	vsnprintf(sz, sizeof(sz), szFormat, v);
	va_end(v);
	g_log.push_back(sz);
}
void StandHit(const char* szMethod) { g_hits.push_back(szMethod); }
// issue 4726 (srv_remote_db.cpp): the DLL's battle-damage Lua event, 1 on (the default here: GetDamageInflicted never
// runs), 0 off, -1 not known
static int g_damageEvents = 1, g_modOptReads = 0;
int SrvDbBattleDamageEvents() { ++g_modOptReads; return g_damageEvents; }
// ticket 4932's call in srv_remote_feed.cpp (feed/srv_render_events.cpp is not built here): this test's link broke since
// 4932 (found by 4726, 2026-11-20: "undefined symbol SrvRenderEvents::SetTurn")
namespace SrvRenderEvents
{
void SetTurn(int) {}
}  // namespace SrvRenderEvents
// ticket 4914 (epic 0615): server.cfg feed_rolling_plots for the rolling refresh (INT32_MIN: not in the config)
static int g_cfgRoll = INT32_MIN;
static int g_cfgLoadSnap = INT32_MIN;   // ticket 0529: server.cfg feed_load_snapshot
int SrvCfgInt(const char* szKey, int iDefault)
{
	if (strcmp(szKey, "feed_load_snapshot") == 0)
		return g_cfgLoadSnap != INT32_MIN ? g_cfgLoadSnap : iDefault;
	return strcmp(szKey, "feed_rolling_plots") == 0 && g_cfgRoll != INT32_MIN ? g_cfgRoll : iDefault;
}
static ICvGameContext1* g_ctx = nullptr;
static int g_channel = 1;
static bool g_inUpdate = true;
namespace SrvRemote
{
ICvGameContext1* Context() { return g_ctx; }
int ThreadChannel() { return g_channel; }
bool InUpdate() { return g_inUpdate; }
}  // namespace SrvRemote

static int LogCount(const char* needle)
{
	int n = 0;
	for (const std::string& s : g_log)
		if (s.find(needle) != std::string::npos) ++n;
	return n;
}

// ---- stand-in DLL objects ----
static int g_destroys = 0, g_wrappers = 0;   // wrappers handed out by GetPlot / GetUnit / GetCity / GetPlayer / GetGame

struct Plot : Stand_ICvPlot1
{
	int idx = -1;
	int GetPlotIndex() const override { return idx; }
	void Destroy() override { ++g_destroys; }
};

struct Unit : Stand_ICvUnit1
{
	int owner = 1, id = 1001, type = 7, domain = 2, facing = 3, damage = 20, maxHP = 100;
	bool embarked = false, garrisoned = false, invisible = false, delayed = false, throws = false;
	Plot plot;
	mutable std::set<std::string> called;
	mutable int lastTeam = -99;
	PlayerTypes GetOwner() const override { called.insert("GetOwner"); return (PlayerTypes)owner; }
	int GetID() const override { called.insert("GetID"); return id; }
	UnitTypes GetUnitType() const override { called.insert("GetUnitType"); return (UnitTypes)type; }
	DomainTypes GetDomainType() const override { called.insert("GetDomainType"); return (DomainTypes)domain; }
	DirectionTypes GetFacingDirection(bool b) const override
	{
		called.insert(b ? "GetFacingDirection(true)" : "GetFacingDirection(false)");
		return (DirectionTypes)facing;
	}
	ICvPlot1* GetPlot() const override
	{
		called.insert("GetPlot");
		++g_wrappers;
		return plot.idx >= 0 ? const_cast<Plot*>(&plot) : (--g_wrappers, nullptr);
	}
	int GetDamage() const override
	{
		called.insert("GetDamage");
		if (throws) throw 42;
		return damage;
	}
	int GetMaxHitPoints() const override { called.insert("GetMaxHitPoints"); return maxHP; }
	bool IsEmbarked() const override { called.insert("IsEmbarked"); return embarked; }
	bool IsGarrisoned() const override { called.insert("IsGarrisoned"); return garrisoned; }
	bool IsInvisible(TeamTypes t, bool bDebug) const override
	{
		called.insert(bDebug ? "IsInvisible(debug)" : "IsInvisible");
		lastTeam = (int)t;
		return invisible;
	}
	bool IsDelayedDeathExported() const override { called.insert("IsDelayedDeathExported"); return delayed; }
	void Destroy() override { ++g_destroys; }
};

struct City : Stand_ICvCity1
{
	int owner = 4, id = 9;
	int x = -1, y = -1;   // ticket 5062 (W108): OnCombat reads an attacking city's GetX / GetY (kind 28's a[3])
	PlayerTypes GetOwner() const override { return (PlayerTypes)owner; }
	int GetID() const override { return id; }
	int GetX() const override { return x; }
	int GetY() const override { return y; }
	void Destroy() override { ++g_destroys; }
};

struct Player : Stand_ICvPlayer1
{
	int team = 0, colour = 10, civ = 20, era = 1, art = 4, minorType = -1;
	mutable int civReads = 0, minorReads = 0;
	bool ever = true, alive = true, minor = false, barb = false, human = false, turnActive = false;
	bool IsEverAlive() const override { return ever; }
	TeamTypes GetTeam() const override { return (TeamTypes)team; }
	bool IsAlive() const override { return alive; }
	bool IsMinorCiv() const override { return minor; }
	bool IsBarbarian() const override { return barb; }
	PlayerColorTypes GetPlayerColor() const override { return (PlayerColorTypes)colour; }
	CivilizationTypes GetCivilizationType() const override { ++civReads; return (CivilizationTypes)civ; }
	MinorCivTypes GetMinorCivType() const override { ++minorReads; return (MinorCivTypes)minorType; }   // issue 4460
	EraTypes GetCurrentEra() const override { return (EraTypes)era; }
	ArtStyleTypes GetArtStyleType() const override { return (ArtStyleTypes)art; }
	bool IsHuman() const override { return human; }
	bool IsTurnActive() const override { return turnActive; }
	std::string embarkOverride;   // issue 0439
	const char* GetEmbarkedGraphicOverride() override { return embarkOverride.c_str(); }
	void Destroy() override { ++g_destroys; }
};

static Player g_players[64];

// issue 0439: a team for the kind 21 read (ICvTeam1::CanEmbarkAllWaterPassage)
struct Team : Stand_ICvTeam1
{
	bool allWater = false;
	bool CanEmbarkAllWaterPassage() const override { return allWater; }
	void Destroy() override { ++g_destroys; }
};
static Team g_teams[64];
static bool g_teamFails = false;   // GetTeam answers NULL

struct Game : Stand_ICvGame1
{
	int activeTeam = 0, activePlayer = 0;
	int turn = 0;   // ticket 0529: GetGameTurn (the load snapshot reads the turn itself)
	bool quick = false;
	int playerReads = 0;
	int GetGameTurn() const override { return turn; }
	ICvPlayer1* GetPlayer(const PlayerTypes e) override
	{
		++playerReads;
		if ((int)e < 0 || (int)e > 63) return nullptr;
		++g_wrappers;
		return &g_players[(int)e];
	}
	TeamTypes GetActiveTeam() override { return (TeamTypes)activeTeam; }
	PlayerTypes GetActivePlayer() override { return (PlayerTypes)activePlayer; }
	bool IsOption(GameOptionTypes o) const override { return o == GAMEOPTION_QUICK_COMBAT && quick; }
	void Destroy() override { ++g_destroys; }
};

// ticket 4914 (epic 0615): a map of plots with the getters ReadPlot uses, for the snapshot and the rolling refresh
struct MapPlot : Stand_ICvPlot1
{
	int idx = -1, terrain = 0, plotType = 2, feature = -1, improvement = -1, owner = -1, fow = 0;
	bool river = false;
	char continent = 1;
	int edges = 0;   // ticket 0631: bit 0 W of river, bit 1 NW, bit 2 NE (the DLL's own three edges of the plot)
	mutable int edgeReads = 0;
	int GetPlotIndex() const override { return idx; }
	bool IsWOfRiver() const override { ++edgeReads; return (edges & 1) != 0; }
	bool IsNWOfRiver() const override { ++edgeReads; return (edges & 2) != 0; }
	bool IsNEOfRiver() const override { ++edgeReads; return (edges & 4) != 0; }
	TerrainTypes GetTerrainType() const override { return (TerrainTypes)terrain; }
	PlotTypes GetPlotType() const override { return (PlotTypes)plotType; }
	FeatureTypes GetFeatureType() const override { return (FeatureTypes)feature; }
	ImprovementTypes GetImprovementType() const override { return (ImprovementTypes)improvement; }
	PlayerTypes GetOwner() const override { return (PlayerTypes)owner; }
	bool IsRiver() const override { return river; }
	FogOfWarModeTypes GetActiveFogOfWarMode() const override { return (FogOfWarModeTypes)fow; }
	char GetContinentType() const override { return continent; }
	void Destroy() override { ++g_destroys; }
};

struct Map : Stand_ICvMap1
{
	int w = 0, h = 0, width = 0;   // width: what PlotNum(0, 1) answers (the grid's width unless a test changes it)
	std::vector<MapPlot> plots;
	mutable std::vector<int> reads;   // the plot index of every GetPlot that returned a plot, in order
	mutable int outside = 0;          // GetPlot calls outside the grid other than the height probe (0, h)
	void Init(int iW, int iH)
	{
		w = width = iW;
		h = iH;
		plots.assign((size_t)(iW * iH), MapPlot());
		for (int i = 0; i < iW * iH; ++i)
		{
			plots[(size_t)i].idx = i;
			plots[(size_t)i].terrain = i % 5;
		}
		reads.clear();
		outside = 0;
	}
	int PlotNum(int iX, int iY) const override { return iY * width + iX; }
	ICvPlot1* GetPlot(int iX, int iY) const override
	{
		if (iX < 0 || iY < 0 || iX >= w || iY >= h)
		{
			if (!(iX == 0 && iY == h)) ++outside;
			return nullptr;
		}
		++g_wrappers;
		reads.push_back(iY * w + iX);
		return const_cast<MapPlot*>(&plots[(size_t)(iY * w + iX)]);
	}
	void Destroy() override { ++g_destroys; }
};

struct Ctx : Stand_ICvGameContext1
{
	Game* game = nullptr;
	Map* map = nullptr;   // ticket 4914: NULL in the unit tests (the plot snapshot finds no map, counted there)
	ICvGame1* GetGame() override
	{
		if (game) ++g_wrappers;
		return game;
	}
	ICvMap1* GetMap() override
	{
		if (map) ++g_wrappers;
		return map;
	}
	ICvFeatureInfo1* GetFeatureInfo(FeatureTypes) override { return nullptr; }   // ticket 4914: no natural wonder
	int GetNumImprovementInfos() override { return 0; }   // ticket 4914: the snapshot's farm ids (none here)
	int GetNumResourceInfos() override { return 0; }
	ICvTeam1* GetTeam(TeamTypes t) override   // issue 0439
	{
		if (g_teamFails || (int)t < 0 || (int)t > 63) return nullptr;
		++g_wrappers;
		return &g_teams[(int)t];
	}
};

struct Combat : Stand_ICvCombatInfo1
{
	Unit* att = nullptr;
	Unit* def = nullptr;
	Unit* inter = nullptr;   // issue 4726: BATTLE_UNIT_INTERCEPTOR
	int inflicted[3] = {0, 0, 0};
	mutable int inflictedCalls = 0;
	City* attCity = nullptr;
	City* defCity = nullptr;
	Plot plot;
	bool advances = true, retaliates = false, ranged = true, bombing = false, sweep = false, captured = false, vis = true;
	int nuclear = 0, final0 = 30, final1 = 60, count = 2, maxCount = 32;
	CvCombatMemberEntry members[40];
	mutable bool inflictedCalled = false;
	ICvUnit1* GetUnit(BattleUnitTypes b) const override
	{
		Unit* u = b == BATTLE_UNIT_ATTACKER ? att : b == BATTLE_UNIT_DEFENDER ? def : b == BATTLE_UNIT_INTERCEPTOR ? inter
		                                                                                                            : nullptr;
		if (u) ++g_wrappers;
		return u;
	}
	ICvCity1* GetCity(BattleUnitTypes b) const override
	{
		City* c = b == BATTLE_UNIT_ATTACKER ? attCity : defCity;
		if (c) ++g_wrappers;
		return c;
	}
	ICvPlot1* GetPlot() const override
	{
		++g_wrappers;
		return const_cast<Plot*>(&plot);
	}
	bool GetAttackerAdvances() const override { return advances; }
	bool GetVisualizeCombat() const override { return vis; }
	bool GetDefenderRetaliates() const override { return retaliates; }
	int GetNuclearDamageLevel() const override { return nuclear; }
	bool GetAttackIsRanged() const override { return ranged; }
	bool GetAttackIsBombingMission() const override { return bombing; }
	bool GetAttackIsAirSweep() const override { return sweep; }
	bool GetDefenderCaptured() const override { return captured; }
	// issue 4726: called only while the battle-damage Lua event is off (g_damageEvents 0)
	int GetDamageInflicted(BattleUnitTypes b) const override
	{
		inflictedCalled = true;
		++inflictedCalls;
		return b >= 0 && b < 3 ? inflicted[b] : 0;
	}
	int GetFinalDamage(BattleUnitTypes b) const override { return b == BATTLE_UNIT_ATTACKER ? final0 : final1; }
	const CvCombatMemberEntry* GetDamageMember(int i) const override
	{
		return i >= 0 && i < 40 && i != 1000 ? &members[i] : nullptr;
	}
	int GetDamageMemberCount() const override { return count; }
	int GetMaxDamageMemberCount() const override { return maxCount; }
};

static Game g_game;
static Ctx g_context;

static void Fresh(bool units = true)
{
	SrvFeed::ResetForTest();
	SrvFeed::SetEnabled(true);
	SrvFeed::SetUnitsEnabled(units);
	g_log.clear();
	g_hits.clear();
	g_destroys = g_wrappers = 0;
	for (int i = 0; i < 64; ++i)
	{
		g_players[i] = Player();
		g_players[i].team = i;
		g_players[i].era = 1 + i % 3;
		g_players[i].ever = i < 4 || i == 63;
	}
	g_players[63].barb = true;
	for (int i = 0; i < 64; ++i) g_teams[i] = Team();   // issue 0439
	g_teamFails = false;
	g_game = Game();
	g_context = Ctx();
	g_context.game = &g_game;
	g_ctx = &g_context;
	g_channel = 1;
	g_inUpdate = true;
}

static SrvFeed::Batch Drained()
{
	SrvFeed::Batch b;
	SrvFeed::Drain(b);
	return b;
}

static bool Rec(int owner, int id, SrvFeed::UnitRecord& r) { return SrvFeed::GetUnit(owner, id, r); }

static void Balanced(const char* where)
{
	if (g_wrappers != g_destroys) printf("info %s: wrappers %d, destroys %d\n", where, g_wrappers, g_destroys);
	CHECK(g_wrappers == g_destroys);   // every wrapper the feed got from the DLL was released
	if (!g_hits.empty()) printf("info %s: unmodelled call %s\n", where, g_hits[0].c_str());
	CHECK(g_hits.empty());             // no DLL call outside the getters the feed may use
}

// kind 11: the event and the whole record from the getters; the owner's row read through the context once
static void TestCreated()
{
	g_test = "kind-11";
	Fresh();
	Unit u;
	u.plot.idx = 77; u.embarked = false; u.garrisoned = true; u.invisible = true; u.delayed = false;
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitCreated, &u, 0, 0);
	SrvFeed::Batch b = Drained();
	CHECK(b.events.size() == 1);
	const SrvFeed::Event& e = b.events[0];
	CHECK(e.kind == SrvFeed::kUnitCreated && e.plot == 77 && e.a[0] == 1 && e.a[1] == 1001 && e.a[2] == 7 && e.a[3] == 0);
	SrvFeed::UnitRecord r;
	CHECK(Rec(1, 1001, r));
	CHECK(r.owner == 1 && r.id == 1001 && r.unitType == 7 && r.domain == 2 && r.facing == 3 && r.plot == 77 &&
	      r.damage == 20 && r.maxHP == 100 && r.seq == e.seq && r.reserved == 0);
	CHECK(r.flags == (SrvFeed::kUnitFlagGarrisoned | SrvFeed::kUnitFlagInvisible | SrvFeed::kUnitFlagVisible));
	CHECK(r.artStyle == 4 && r.era == g_players[1].era);
	CHECK(u.called.count("GetFacingDirection(false)") == 1 && u.called.count("GetFacingDirection(true)") == 0 &&
	      u.called.count("IsInvisible") == 1 && u.lastTeam == 0);
	CHECK(b.players.size() == 1 && b.players[0].player == 1 && b.players[0].colour == 10 && b.players[0].artStyle == 4);
	// a second unit of the same owner: the kept row, no new player read
	const int reads = g_game.playerReads;
	Unit v;
	v.id = 1002; v.plot.idx = 78;
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitCreated, &v, 0, 0);
	CHECK(g_game.playerReads == reads);
	Balanced("kind-11");
	// F8 counters: the thread role and Update are counted (report line)
	char buf[8192] = {};
	FILE* f = nullptr;
	tmpfile_s(&f);
	if (f)
	{
		SrvRemoteFeed::WriteTotals(f);
		rewind(f);
		const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
		buf[n] = 0;
		fclose(f);
	}
	CHECK(strstr(buf, "feed units: on") && strstr(buf, " 11=2") && strstr(buf, "core "));
}

// kind 12: the path side table, the record's plot after the move
static void TestMoved()
{
	g_test = "kind-12";
	Fresh();
	Unit u;
	u.plot.idx = 10;
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitCreated, &u, 0, 0);
	const int path[] = {10, 11, 12};
	u.plot.idx = 12;
	SrvRemoteFeed::OnUnitMoved(&u, path, 3);
	SrvRemoteFeed::OnUnitMoved(&u, nullptr, 0);
	SrvFeed::Batch b = Drained();
	CHECK(b.events.size() == 3 && b.events[1].kind == SrvFeed::kUnitMoved && b.events[1].a[2] == 0 &&
	      b.events[1].a[3] == 3 && b.events[1].plot == 12 && b.events[2].a[2] == 1 && b.events[2].a[3] == 0);
	CHECK(b.paths.size() == 2 && b.paths[0].length == 3 && b.paths[0].seq == b.events[1].seq && b.pathPlots.size() == 3 &&
	      b.pathPlots[0] == 10 && b.pathPlots[2] == 12 && b.paths[1].length == 0);
	SrvFeed::UnitRecord r;
	CHECK(Rec(1, 1001, r) && r.plot == 12);
	Balanced("kind-12");
}

// kinds 13 and 30: the plots of the call
static void TestPlots()
{
	g_test = "kind-13-30";
	Fresh();
	Unit u;
	u.plot.idx = 5;
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitCreated, &u, 0, 0);
	Plot to, from;
	to.idx = 40; from.idx = 5;
	u.plot.idx = 40;
	SrvRemoteFeed::OnUnitPlot(SrvFeed::kUnitTeleported, &u, nullptr, &to);
	SrvRemoteFeed::OnUnitPlot(SrvFeed::kUnitRebased, &u, &from, &to);
	SrvRemoteFeed::OnUnitPlot(SrvFeed::kUnitTeleported, &u, nullptr, nullptr);
	SrvFeed::Batch b = Drained();
	CHECK(b.events.size() == 4);
	CHECK(b.events[1].kind == SrvFeed::kUnitTeleported && b.events[1].a[2] == 40 && b.events[1].a[3] == 0 &&
	      b.events[1].plot == 40);
	CHECK(b.events[2].kind == SrvFeed::kUnitRebased && b.events[2].a[2] == 5 && b.events[2].a[3] == 40);
	CHECK(b.events[3].a[2] == -1);
	SrvFeed::UnitRecord r;
	CHECK(Rec(1, 1001, r) && r.plot == 40);
	Balanced("kind-13-30");
}

// kinds 14 and 15: the dead flag, then the removal that reads only the key (CvUnit::~CvUnit)
static void TestDestroyed()
{
	g_test = "kind-14-15";
	Fresh();
	Unit u;
	u.plot.idx = 9;
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitCreated, &u, 0, 0);
	u.delayed = true;
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitDestroyedInCombat, &u, 0, 0);
	SrvFeed::UnitRecord r;
	CHECK(Rec(1, 1001, r) && (r.flags & SrvFeed::kUnitFlagDead) && (r.flags & SrvFeed::kUnitFlagDelayedDeath));
	u.called.clear();
	u.plot.idx = 123;   // must not be read
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitDestroyed, &u, 0, 0);
	CHECK(!Rec(1, 1001, r));
	CHECK(u.called.size() == 2 && u.called.count("GetOwner") && u.called.count("GetID"));
	SrvFeed::Batch b = Drained();
	CHECK(b.events.size() == 3 && b.events[1].kind == SrvFeed::kUnitDestroyedInCombat && b.events[2].kind ==
	      SrvFeed::kUnitDestroyed && b.events[2].plot == 9 && b.events[2].a[0] == 1 && b.events[2].a[1] == 1001);
	// a destroyed unit the table does not hold (a load's DLL-side reset): plot -1, counted
	Unit w;
	w.id = 5555;
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitDestroyed, &w, 0, 0);
	b = Drained();
	CHECK(b.events.size() == 1 && b.events[0].plot == -1 && SrvFeed::GetCounters2().unitRemoveUnknown == 1);
	Balanced("kind-14-15");
}

// kinds 16-23, 29, 31: the call's arguments in a[2..3], the record refreshed, the call bits
static void TestSimpleKinds()
{
	g_test = "kinds-16-23-29-31";
	Fresh();
	Unit u;
	u.plot.idx = 3;
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitCreated, &u, 0, 0);
	Drained();
	struct K { uint16_t kind; int a2, a3; } ks[] = {
		{SrvFeed::kUnitSetDamage, 35, 20}, {SrvFeed::kUnitFortify, 1, 0}, {SrvFeed::kUnitWork, 6, 0},
		{SrvFeed::kUnitParadrop, 0, 0}, {SrvFeed::kUnitActivate, 0, 0}, {SrvFeed::kUnitEmbark, 1, 0},
		{SrvFeed::kUnitGarrison, 1, 0}, {SrvFeed::kUnitShouldDimFlag, 1, 0}, {SrvFeed::kUnitMissionEnd, 0, 0},
		{SrvFeed::kUnitResetAnimationState, 0, 0}};
	u.damage = 35;
	u.embarked = true;
	for (const K& k : ks)
	{
		SrvRemoteFeed::OnUnit(k.kind, &u, k.a2, k.a3);
		SrvFeed::Batch b = Drained();
		// issue 0439: kind 21 (on) carries the embarked art's inputs in a[3]: read, no passage, no override here
		const int a3 = k.kind == SrvFeed::kUnitEmbark ? (int)SrvFeed::kEmbarkArtRead : k.a3;
		CHECK(b.events.size() == 1 && b.events[0].kind == k.kind && b.events[0].a[0] == 1 && b.events[0].a[1] == 1001 &&
		      b.events[0].a[2] == k.a2 && b.events[0].a[3] == a3 && b.events[0].plot == 3);
		SrvFeed::UnitRecord r;
		CHECK(Rec(1, 1001, r) && r.seq == b.events[0].seq && r.damage == 35 && (r.flags & SrvFeed::kUnitFlagEmbarked));
	}
	SrvFeed::UnitRecord r;
	CHECK(Rec(1, 1001, r) && (r.flags & SrvFeed::kUnitFlagFortified));
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitFortify, &u, 0, 0);
	CHECK(Rec(1, 1001, r) && !(r.flags & SrvFeed::kUnitFlagFortified));
	// kind 21 for a key not in the table (a unit set up embarked at a load) is a creation
	Unit e;
	e.id = 3001; e.embarked = true; e.plot.idx = 50;
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitEmbark, &e, 1, 0);
	CHECK(Rec(1, 3001, r) && r.plot == 50 && SrvFeed::GetCounters2().embarkCreations == 1);
	Balanced("kinds-16-23-29-31");
}

// kind 24: visible + 2 x check in a[2], the blend time's float bits in a[3]
static void TestVisibility()
{
	g_test = "kind-24";
	Fresh();
	Unit u;
	u.plot.idx = 3;
	SrvRemoteFeed::OnUnitVisibility(&u, false, false, 0.0f);   // before the creation: a unit made in a city
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitCreated, &u, 0, 0);
	SrvFeed::UnitRecord r;
	// issue 4463: the Visibility before the creation is ignored, as the EXE ignores it (unit-server-scene.md 12.1 [L])
	CHECK(Rec(1, 1001, r) && (r.flags & SrvFeed::kUnitFlagVisible));
	SrvRemoteFeed::OnUnitVisibility(&u, false, false, 0.0f);
	CHECK(Rec(1, 1001, r) && !(r.flags & SrvFeed::kUnitFlagVisible));
	SrvRemoteFeed::OnUnitVisibility(&u, true, true, 0.5f);
	CHECK(Rec(1, 1001, r) && (r.flags & SrvFeed::kUnitFlagVisible));
	SrvFeed::Batch b = Drained();
	float f = 0;
	memcpy(&f, &b.events[3].a[3], 4);
	CHECK(b.events.size() == 4 && b.events[0].kind == SrvFeed::kUnitVisibility && b.events[0].a[2] == 0 &&
	      b.events[0].flags == SrvFeed::kEventNoRecord && b.events[1].flags == 0 && b.events[2].flags == 0 &&
	      b.events[3].a[2] == 3 && f == 0.5f);
	Balanced("kind-24");
}

// kinds 25 and 28: the combat record, its members, the sides' records, the game flags, the returned value (Q7);
// GetDamageInflicted (not pure) is never called
static void TestCombat()
{
	g_test = "kind-25-28";
	Fresh();
	Unit a, d;
	a.owner = 1; a.id = 1001; a.plot.idx = 20; a.damage = 5;
	d.owner = 2; d.id = 2001; d.plot.idx = 21; d.damage = 0;
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitCreated, &a, 0, 0);
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitCreated, &d, 0, 0);
	Drained();
	Combat c;
	c.att = &a; c.def = &d; c.plot.idx = 21; c.count = 2;
	c.members[0].SetPlayer((PlayerTypes)2);
	c.members[0].m_iID = 2001; c.members[0].m_iDamage = 40; c.members[0].m_iFinalDamage = 40;
	c.members[0].m_iMaxHitPoints = 100; c.members[0].m_iX = 3; c.members[0].m_iY = 4; c.members[0].m_eEra = (EraTypes)2;
	c.members[1].m_iID = 2002; c.members[1].m_bfFlags = 1;
	g_game.quick = true;
	g_game.activePlayer = 0;
	g_players[0].turnActive = true;
	d.damage = 60;   // the defender's damage after the combat as the DLL holds it at the call
	const uint32_t s = SrvRemoteFeed::OnCombat(SrvFeed::kUnitCombat, &c);
	SrvRemoteFeed::OnCombatResult(s, 17);
	CHECK(s > 0 && !c.inflictedCalled);
	SrvFeed::Batch b = Drained();
	CHECK(b.events.size() == 1 && b.events[0].kind == SrvFeed::kUnitCombat && b.events[0].plot == 21 &&
	      b.events[0].a[0] == 1 && b.events[0].a[1] == 1001 && b.events[0].a[2] == 0 && b.events[0].a[3] == 0);
	CHECK(b.combats.size() == 1);
	if (b.combats.size() == 1)
	{
		const SrvFeed::CombatRecord& r = b.combats[0];
		CHECK(r.seq == s && r.kind == SrvFeed::kUnitCombat && r.plot == 21 && r.attackerOwner == 1 &&
		      r.attackerUnit == 1001 && r.attackerCity == -1 && r.defenderOwner == 2 && r.defenderUnit == 2001 &&
		      r.defenderCity == -1 && r.finalDamage[0] == 30 && r.finalDamage[1] == 60 && r.returned == 17 &&
		      r.nMembers == 2 && r.firstMember == 0 && r.nuclearLevel == 0);
		CHECK(r.flags == (SrvFeed::kCombatAttackerAdvances | SrvFeed::kCombatRanged | SrvFeed::kCombatVisualize |
		                  SrvFeed::kCombatReturnedKnown | SrvFeed::kCombatQuickOption | SrvFeed::kCombatActiveTurn |
		                  SrvFeed::kCombatGameRead));
		CHECK(b.combatMembers.size() == 2 && b.combatMembers[0].player == 2 && b.combatMembers[0].id == 2001 &&
		      b.combatMembers[0].damage == 40 && b.combatMembers[0].x == 3 && b.combatMembers[0].y == 4 &&
		      b.combatMembers[0].era == 2 && b.combatMembers[1].flags == 1);
	}
	SrvFeed::UnitRecord r;
	CHECK(Rec(2, 2001, r) && r.damage == 60 && r.seq == s);   // the defender's record refreshed with the combat
	// kind 28: a city attacks a unit; a[0..1] are the city's
	City city;
	city.owner = 4; city.id = 9;
	Combat cc;
	cc.attCity = &city; cc.def = &d; cc.plot.idx = 21; cc.count = 0;
	const uint32_t s2 = SrvRemoteFeed::OnCombat(SrvFeed::kCityCombat, &cc);
	b = Drained();
	SrvRemoteFeed::OnCombatResult(s2, 3);   // drained before the return: counted late, not stored
	CHECK(b.events.size() == 1 && b.events[0].kind == SrvFeed::kCityCombat && b.events[0].a[0] == 4 &&
	      b.events[0].a[1] == 9 && b.combats.size() == 1 && b.combats[0].attackerOwner == 4 &&
	      b.combats[0].attackerCity == 9 && b.combats[0].attackerUnit == -1 && b.combats[0].defenderUnit == 2001 &&
	      !(b.combats[0].flags & SrvFeed::kCombatReturnedKnown));
	CHECK(SrvFeed::GetCounters2().combatResultsLate == 1);
	// ticket 5062 (W108): the attacking city's plot reaches the drained event's a[3] (plot + 1) through PushCombat, the
	// product's path (live 2026-11-23 it was zeroed there); without a grid it is 0 (not known)
	CHECK(b.events.size() == 1 && b.events[0].a[3] == 0);   // no snapshot yet: outside the grid
	SrvFeed::BeginSnapshot(8, 6);
	SrvFeed::EndSnapshot();
	(void)Drained();
	City cxy;
	cxy.owner = 4; cxy.id = 9; cxy.x = 3; cxy.y = 2;
	Combat cc2;
	cc2.attCity = &cxy; cc2.def = &d; cc2.plot.idx = 21; cc2.count = 0;
	SrvRemoteFeed::OnCombat(SrvFeed::kCityCombat, &cc2);
	b = Drained();
	CHECK(b.events.size() == 1 && b.events[0].kind == SrvFeed::kCityCombat && b.events[0].a[3] == 2 * 8 + 3 + 1 &&
	      b.combats.size() == 1 && b.combats[0].attackerCity == 9);
	// more members than the cap of 32 (GetMaxDamageMemberCount): cut, counted; a NULL member skipped
	Combat big;
	big.att = &a; big.def = &d; big.count = 40; big.maxCount = 32;
	SrvRemoteFeed::OnCombat(SrvFeed::kUnitCombat, &big);
	b = Drained();
	CHECK(b.combats.size() == 1 && b.combats[0].nMembers == 32);
	Balanced("kind-25-28");
}

// issue 4726: the interceptor as a flagged member after the damage members; the per-combat damage only while the
// battle-damage Lua event is off (read once per load); with it on or not known, GetDamageInflicted never runs
static void TestCombatInterceptorAndDamage()
{
	g_test = "kind-25-4726";
	Fresh();
	Unit a, d, f;
	a.owner = 1; a.id = 1001; a.plot.idx = 20; a.damage = 0; a.domain = 3;
	d.owner = 2; d.id = 2001; d.plot.idx = 21;
	f.owner = 2; f.id = 2005; f.plot.idx = 22; f.damage = 10; f.maxHP = 100; f.domain = 3;
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitCreated, &a, 0, 0);
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitCreated, &d, 0, 0);
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitCreated, &f, 0, 0);
	Drained();
	// a new load: the option is read again at the next combat
	CHECK(SrvRemoteFeed::OnSetupGraphical(0));
	Drained();
	g_damageEvents = 0;   // VP's shipped setting: the getter is a plain read
	const int readsBefore = g_modOptReads;
	Combat c;
	c.att = &a; c.def = &d; c.inter = &f; c.plot.idx = 21; c.count = 1; c.bombing = true;
	c.members[0].SetPlayer((PlayerTypes)2);
	c.members[0].m_iID = 2001; c.members[0].m_iDamage = 0; c.members[0].m_iFinalDamage = 60;
	c.inflicted[BATTLE_UNIT_ATTACKER] = 0;     // the bomber deals nothing (an aborted strike)
	c.inflicted[BATTLE_UNIT_DEFENDER] = 0;
	c.inflicted[BATTLE_UNIT_INTERCEPTOR] = 35; // the fighter's damage on the bomber
	c.final0 = 0; c.final1 = 0;
	const uint32_t s = SrvRemoteFeed::OnCombat(SrvFeed::kUnitCombat, &c);
	CHECK(s > 0 && c.inflictedCalls == 3 && g_modOptReads == readsBefore + 1);
	SrvFeed::Batch b = Drained();
	CHECK(b.combats.size() == 1);
	if (b.combats.size() == 1)
	{
		const SrvFeed::CombatRecord& r = b.combats[0];
		CHECK((r.flags & SrvFeed::kCombatInterceptorMember) && (r.flags & SrvFeed::kCombatDamageKnown) &&
		      SrvFeed::CombatDamage(r, 0) == 0 &&
		      SrvFeed::CombatDamage(r, 1) == 0 && r.nMembers == 2);
		CHECK(b.combatMembers.size() == 2 && b.combatMembers[0].flags == 0 && b.combatMembers[0].id == 2001 &&
		      b.combatMembers[1].flags == SrvFeed::kCombatMemberInterceptor && b.combatMembers[1].player == 2 &&
		      b.combatMembers[1].id == 2005 && b.combatMembers[1].damage == 35 &&
		      b.combatMembers[1].finalDamage == 10 && b.combatMembers[1].maxHP == 100);
	}
	// a strike that hits: the attacker takes the defender's dealt damage, the defender the attacker's (the EXE's mapping)
	Combat h;
	h.att = &a; h.def = &d; h.count = 0;
	h.inflicted[BATTLE_UNIT_ATTACKER] = 42;
	h.inflicted[BATTLE_UNIT_DEFENDER] = 7;
	SrvRemoteFeed::OnCombat(SrvFeed::kUnitCombat, &h);
	b = Drained();
	CHECK(b.combats.size() == 1 && b.combatMembers.empty() && !(b.combats[0].flags & SrvFeed::kCombatInterceptorMember) &&
	      SrvFeed::CombatDamage(b.combats[0], 0) == 7 &&
	      SrvFeed::CombatDamage(b.combats[0], 1) == 42 && g_modOptReads == readsBefore + 1);   // read once per load
	// 0477's fix: the copy the engine gets has DamageInflicted(DEFENDER) 0: the attacker's damage in this combat is 0
	Combat sub;
	sub.att = &a; sub.def = &d; sub.count = 0;
	sub.inflicted[BATTLE_UNIT_ATTACKER] = 30;
	sub.inflicted[BATTLE_UNIT_DEFENDER] = 0;
	SrvRemoteFeed::OnCombat(SrvFeed::kUnitCombat, &sub);
	b = Drained();
	CHECK(b.combats.size() == 1 && SrvFeed::CombatDamage(b.combats[0], 0) == 0 &&
	      SrvFeed::CombatDamage(b.combats[0], 1) == 30);
	// the event on (after the next load): no GetDamageInflicted, the damage not known, the interceptor's damage 0
	CHECK(SrvRemoteFeed::OnSetupGraphical(0));
	Drained();
	g_damageEvents = 1;
	Combat on = c;
	on.inflictedCalls = 0;
	SrvRemoteFeed::OnCombat(SrvFeed::kUnitCombat, &on);
	b = Drained();
	CHECK(on.inflictedCalls == 0 && b.combats.size() == 1 && !(b.combats[0].flags & SrvFeed::kCombatDamageKnown) &&
	      b.combats[0].reserved == 0 && SrvFeed::CombatDamage(b.combats[0], 0) == -1 && b.combatMembers.size() == 2 &&
	      b.combatMembers[1].damage == 0);
	// not known (no table): as on
	CHECK(SrvRemoteFeed::OnSetupGraphical(0));
	Drained();
	g_damageEvents = -1;
	Combat unk = h;
	unk.inflictedCalls = 0;
	SrvRemoteFeed::OnCombat(SrvFeed::kUnitCombat, &unk);
	b = Drained();
	CHECK(unk.inflictedCalls == 0 && b.combats.size() == 1 && !(b.combats[0].flags & SrvFeed::kCombatDamageKnown));
	g_damageEvents = 1;
	Balanced("kind-25-4726");
}

// kinds 26, 27: city events without a table effect
static void TestCityEvents()
{
	g_test = "kind-26-27";
	Fresh();
	City c;
	SrvRemoteFeed::OnCityEvent(SrvFeed::kCitySetDamage, &c, 30, 10);
	SrvRemoteFeed::OnCityEvent(SrvFeed::kCityPopulationChanged, &c, 7, 0);
	SrvFeed::Batch b = Drained();
	CHECK(b.events.size() == 2 && b.events[0].kind == SrvFeed::kCitySetDamage && b.events[0].plot == -1 &&
	      b.events[0].a[0] == 4 && b.events[0].a[1] == 9 && b.events[0].a[2] == 30 && b.events[0].a[3] == 10 &&
	      b.events[1].kind == SrvFeed::kCityPopulationChanged && b.events[1].a[2] == 7);
	std::vector<SrvFeed::UnitRecord> t; uint32_t g = 0;
	SrvFeed::CopyUnits(t, g);
	CHECK(t.empty());
	Balanced("kind-26-27");
}

// kind 32: the load mark at the first ICvPlayer1::SetupGraphical of a load; kind 33: the era change and the row
static void TestLoadAndEra()
{
	g_test = "kind-32-33";
	Fresh();
	Unit u;
	u.plot.idx = 1;
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitCreated, &u, 0, 0);
	CHECK(SrvRemoteFeed::OnSetupGraphical(0));     // a load begins (the first call)
	CHECK(!SrvRemoteFeed::OnSetupGraphical(1));
	CHECK(!SrvRemoteFeed::OnSetupGraphical(63));
	SrvFeed::UnitRecord r;
	CHECK(!Rec(1, 1001, r));
	CHECK(SrvRemoteFeed::OnSetupGraphical(0));     // the next load
	SrvFeed::Batch b = Drained();
	int loads = 0;
	for (const SrvFeed::Event& e : b.events)
		if (e.kind == SrvFeed::kLoadBegin) { ++loads; CHECK(e.a[1] == 0 && e.plot == -1); }
	CHECK(loads == 2 && b.unitTableGen == 2 && LogCount("load 2 begins") == 1);
	// the era change: the event and the player row's era from the call
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitCreated, &u, 0, 0);   // reads player 1's row
	Drained();
	SrvRemoteFeed::OnEraChanged(1, 3);
	b = Drained();
	CHECK(b.events.size() == 1 && b.events[0].kind == SrvFeed::kEraChanged && b.events[0].a[0] == 1 &&
	      b.events[0].a[1] == 3 && b.events[0].plot == -1);
	CHECK(b.players.size() == 1 && b.players[0].player == 1 && b.players[0].era == 3);
	// a unit event after it carries the new era in its record
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitActivate, &u, 0, 0);
	CHECK(Rec(1, 1001, r) && r.era == 3);
	// an era change of a player without a row: the event only
	SrvRemoteFeed::OnEraChanged(30, 2);
	b = Drained();
	CHECK(b.events.size() == 2 && b.players.empty());
	Balanced("kind-32-33");
}

// the player rows at LeaveUpdate: every ever-alive player, after a load and at each turn change
static void TestPlayerRows()
{
	g_test = "player-rows";
	Fresh();
	g_players[2].minor = true;
	g_players[2].minorType = 7;   // issue 4460 (A1): its MinorCivilizations index goes into `civilization`
	g_players[3].human = true; g_players[3].turnActive = true;
	g_game.activeTeam = 3;
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, 10);
	SrvFeed::Batch b = Drained();
	CHECK(b.players.size() == 5);   // players 0-3 and the barbarians (63)
	std::vector<SrvFeed::PlayerRecord> ps;
	SrvFeed::CopyPlayers(ps);
	CHECK(ps.size() == 5 && ps[2].flags == 1 && ps[4].player == 63 && ps[4].flags == 2 && ps[3].state == 3 &&
	      ps[0].activeTeam == 3 && ps[1].team == 1);
	// issue 4460 (A1): the minor's row has its minor civilization index, majors and the barbarians their civilization;
	// GetMinorCivType is asked of minors only, GetCivilizationType of the others only
	CHECK(ps[2].civilization == 7 && ps[0].civilization == 20 && ps[4].civilization == 20);
	CHECK(g_players[2].minorReads == 1 && g_players[2].civReads == 0 && g_players[0].minorReads == 0 &&
	      g_players[0].civReads == 1);
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, 10);   // the same turn: no re-read
	b = Drained();
	CHECK(b.players.empty());
	g_players[1].era = 2;
	g_players[1].colour = 33;
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, 11);   // a turn change: re-read, one row changed
	b = Drained();
	CHECK(b.players.size() == 1 && b.players[0].player == 1 && b.players[0].era == 2 && b.players[0].colour == 33);
	// an active team of -1: IsInvisible is not asked (a NO_TEAM index), counted
	g_game.activeTeam = -1;
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, 12);
	Unit u;
	u.plot.idx = 1;
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitCreated, &u, 0, 0);
	CHECK(u.called.count("IsInvisible") == 0);
	Balanced("player-rows");
	// units off: AtLeaveUpdate reads no player
	Fresh(false);
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, 10);
	CHECK(g_game.playerReads == 0);
}

// units off (render_units=0): no DLL call, no event, no record, for every entry point
static void TestUnitsOff()
{
	g_test = "units-off";
	Fresh(false);
	Unit u;
	u.plot.idx = 1;
	City c;
	Combat cb;
	cb.att = &u;
	Plot p;
	p.idx = 3;
	const int path[] = {1};
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitCreated, &u, 0, 0);
	SrvRemoteFeed::OnUnitMoved(&u, path, 1);
	SrvRemoteFeed::OnUnitPlot(SrvFeed::kUnitTeleported, &u, nullptr, &p);
	SrvRemoteFeed::OnUnitVisibility(&u, true, false, 0.5f);
	CHECK(SrvRemoteFeed::OnCombat(SrvFeed::kUnitCombat, &cb) == 0);
	SrvRemoteFeed::OnCombatResult(1, 2);
	SrvRemoteFeed::OnCityEvent(SrvFeed::kCitySetDamage, &c, 1, 0);
	SrvRemoteFeed::OnEraChanged(1, 2);
	CHECK(!SrvRemoteFeed::OnSetupGraphical(0));
	CHECK(u.called.empty() && g_wrappers == 0 && g_game.playerReads == 0 && g_hits.empty());
	SrvFeed::Batch b;
	CHECK(!SrvFeed::Drain(b) && b.events.empty());
	// the feed off entirely, units on: nothing either
	SrvFeed::ResetForTest();
	SrvFeed::SetUnitsEnabled(true);
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitCreated, &u, 0, 0);
	CHECK(u.called.empty());
}

// NULL arguments: the event with key -1, no record, LOUD (first 5), counted
static void TestNulls()
{
	g_test = "nulls";
	Fresh();
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitSetDamage, nullptr, 5, 1);
	SrvRemoteFeed::OnUnitMoved(nullptr, nullptr, 4);
	SrvRemoteFeed::OnUnitPlot(SrvFeed::kUnitTeleported, nullptr, nullptr, nullptr);
	SrvRemoteFeed::OnUnitVisibility(nullptr, true, false, 0.5f);
	SrvRemoteFeed::OnCityEvent(SrvFeed::kCitySetDamage, nullptr, 1, 0);
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitActivate, nullptr, 0, 0);
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitActivate, nullptr, 0, 0);
	const uint32_t s = SrvRemoteFeed::OnCombat(SrvFeed::kUnitCombat, nullptr);
	SrvFeed::Batch b = Drained();
	CHECK(b.events.size() == 8 && s > 0);
	bool keys = true;
	for (const SrvFeed::Event& e : b.events) keys = keys && e.a[0] == -1 && e.a[1] == -1;
	CHECK(keys && b.events[0].a[2] == 5 && b.events[0].a[3] == 1 && b.events[1].a[3] == 4 && b.events[7].a[2] == -1);
	CHECK(b.combats.empty() && b.paths.empty());
	std::vector<SrvFeed::UnitRecord> t; uint32_t g = 0;
	SrvFeed::CopyUnits(t, g);
	CHECK(t.empty());
	// unit and city NULLs share one counter: the first 5 of 7 logged (4 unit lines, 1 city line), the combat its own
	CHECK(LogCount("feed: LOUD: a unit call with a NULL argument") == 4);
	CHECK(LogCount("feed: LOUD: a city call with a NULL argument") == 1);
	CHECK(LogCount("feed: LOUD: a combat call with a NULL argument") == 1);
}

// an exception in a getter: caught (NoteException, LOUD), the event lost, the game untouched, the next call works
static void TestException()
{
	g_test = "exception";
	Fresh();
	Unit u;
	u.plot.idx = 1;
	u.throws = true;
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitCreated, &u, 0, 0);
	SrvFeed::Batch b;
	CHECK(!SrvFeed::Drain(b) || b.events.empty());
	CHECK(LogCount("feed: LOUD: exception caught in a unit call") == 1);
	u.throws = false;
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitCreated, &u, 0, 0);
	b = Drained();
	CHECK(b.events.size() == 1);
}

// dropped unit events: LOUD at the next LeaveUpdate, counted in the report
static void TestDropped()
{
	g_test = "dropped";
	Fresh();
	SrvFeed::SetQueueCap(1);
	Unit u;
	u.plot.idx = 1;
	for (int i = 0; i < 3; ++i)
	{
		u.id = 1001 + i;
		SrvRemoteFeed::OnUnit(SrvFeed::kUnitCreated, &u, 0, 0);
	}
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, 20);
	CHECK(LogCount("feed: LOUD: 2 unit events dropped so far") == 1);
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, 20);   // nothing new: no second line
	CHECK(LogCount("unit events dropped") == 1);
	std::vector<SrvFeed::UnitRecord> t; uint32_t g = 0;
	Drained();   // issue 4463: CopyUnits is the last Drain's copy
	SrvFeed::CopyUnits(t, g);
	CHECK(t.size() == 3);   // the table changed anyway
	SrvFeed::SetQueueCap(65536);
}

// Issue 4463, rule (g) through the DLL side: the getter facing reaches the record only at a creation and a move; a facing
// changed in the DLL without a call (Lua setFacingDirection after InitUnit: 7) does not reach it at a later event.
static void TestFacingRule()
{
	g_test = "facing-4463";
	Fresh();
	Unit u;
	u.plot.idx = 5;
	u.facing = 2;
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitCreated, &u, 0, 0);
	u.facing = 7;   // NO_DIRECTION, set without a call
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitShouldDimFlag, &u, 1, 0);
	SrvRemoteFeed::OnUnit(SrvFeed::kUnitSetDamage, &u, 30, 20);
	SrvRemoteFeed::OnUnitVisibility(&u, true, false, 0.0f);
	SrvFeed::UnitRecord r;
	CHECK(Rec(1, 1001, r) && r.facing == 2);
	u.facing = 4;   // the DLL's setXY turned it, but at a teleport the EXE keeps the heading (issue 0449): the plot only
	Plot to;
	to.idx = 6;
	u.plot.idx = 6;
	SrvRemoteFeed::OnUnitPlot(SrvFeed::kUnitTeleported, &u, nullptr, &to);
	CHECK(Rec(1, 1001, r) && r.facing == 2 && r.plot == 6);
	u.facing = 5;   // a rebase (30) still takes it
	Plot from;
	from.idx = 6;
	SrvRemoteFeed::OnUnitPlot(SrvFeed::kUnitRebased, &u, &from, &to);
	CHECK(Rec(1, 1001, r) && r.facing == 5 && r.plot == 6);
	u.facing = 1;
	const int path[] = {6, 7};
	u.plot.idx = 7;
	SrvRemoteFeed::OnUnitMoved(&u, path, 2);
	CHECK(Rec(1, 1001, r) && r.facing == 1 && r.plot == 7);
	Drained();
	Balanced("facing-4463");
}

// issue 0439 (MC #2908): kind 21's a[3] read at the call: the owner's team's all-water passage and the owner's override
// (hashed); a disembark carries 0; a read that fails (no team) carries 0 and the event still goes; every wrapper released
static void TestEmbarkArt()
{
	g_test = "kind-21-embark-art";
	Fresh();
	Unit u;
	u.owner = 2;
	u.id = 4001;
	u.plot.idx = 7;
	u.embarked = true;
	g_players[2].team = 5;
	auto a3 = [&](int on) {
		SrvRemoteFeed::OnUnit(SrvFeed::kUnitEmbark, &u, on, 0);
		SrvFeed::Batch b = Drained();
		CHECK(b.events.size() == 1 && b.events[0].kind == SrvFeed::kUnitEmbark && b.events[0].a[2] == on);
		return b.events.empty() ? 0xFFFFFFFFu : (uint32_t)b.events[0].a[3];
	};
	CHECK(a3(1) == SrvFeed::kEmbarkArtRead);
	g_teams[5].allWater = true;   // the team researched the passage
	CHECK(a3(1) == (SrvFeed::kEmbarkArtRead | SrvFeed::kEmbarkArtAllWater));
	g_teams[2].allWater = false;   // another team's flag does not count
	g_players[2].embarkOverride = "ART_DEF_UNIT_U_DANISH_LONGBOAT";
	const uint32_t hb = SrvFeed::EmbarkArtHash24("ART_DEF_UNIT_U_DANISH_LONGBOAT") << SrvFeed::kEmbarkArtHashShift;
	CHECK(a3(1) == (SrvFeed::kEmbarkArtRead | SrvFeed::kEmbarkArtAllWater | SrvFeed::kEmbarkArtOverride | hb));
	g_players[2].embarkOverride = "ART_DEF_UNIT_GALLEON";   // a Lua SetEmbarkedGraphicOverride: seen at the next embark
	const uint32_t hg = SrvFeed::EmbarkArtHash24("ART_DEF_UNIT_GALLEON") << SrvFeed::kEmbarkArtHashShift;
	CHECK(a3(1) == (SrvFeed::kEmbarkArtRead | SrvFeed::kEmbarkArtAllWater | SrvFeed::kEmbarkArtOverride | hg));
	u.embarked = false;
	CHECK(a3(0) == 0);   // the disembark reads nothing
	u.embarked = true;
	g_teamFails = true;  // no team: 0, the event and the record as before
	CHECK(a3(1) == 0);
	SrvFeed::UnitRecord r;
	CHECK(Rec(2, 4001, r) && (r.flags & SrvFeed::kUnitFlagEmbarked));
	Balanced("kind-21-embark-art");
}

// ---- ticket 4914 (epic 0615; MC #9700): the rolling refresh (srv_remote_feed.cpp AtLeaveUpdateBody) ----
// The feed's DLL-side state (the table's grid, the last turn, the cursor) lives across tests; each test starts with a
// snapshot forced by a turn that goes back (iTurn < the last turn) or jumps, so the tests run in any order after the
// unit tests (whose LeaveUpdates find no map and leave that state unset).
static Map g_map;
static int g_rollTurn = 1000;   // the turns of these tests: each forced snapshot moves 10 ahead

static bool Changed(const SrvFeed::Batch& b, uint32_t idx)
{
	for (uint32_t i : b.changedPlots)
		if (i == idx) return true;
	return false;
}

// a fresh feed, the stand-in map of iW x iH set as the context's map, K in the config (INT32_MIN: not set), a snapshot
static void RollStart(int iW, int iH, int k)
{
	Fresh(false);
	g_cfgRoll = k;
	g_map.Init(iW, iH);
	g_context.map = &g_map;
	g_rollTurn += 10;
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);   // a jump of 10 turns: a snapshot
	Drained();
	g_map.reads.clear();
}

static int RollFoundLines() { return LogCount("feed: plots found changed at turn "); }   // MC #9712: one line, both paths

// the round robin: every plot once per cycle, in order, wrapping; a plot count that is not a multiple of K; K above the
// plot count; the default K; no read outside the grid; nothing found while nothing changed
static void TestRollingCycle()
{
	g_test = "rolling-cycle-4914";
	RollStart(7, 5, 8);   // 35 plots, K 8: 35 is not a multiple of 8
	char want[128];
	snprintf(want, sizeof(want), "feed: snapshot 7 x 5 at turn %d (35 plots read", g_rollTurn);
	CHECK(LogCount(want) == 1);
	CHECK(LogCount("feed: rolling refresh: 8 plots per Update (server.cfg feed_rolling_plots); a sweep of the 35 plots "
	               "takes 5 Updates") == 1);
	const int kCalls = 14;   // 112 reads: three whole cycles of 35 and 7 more
	for (int i = 0; i < kCalls; ++i)
		SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);   // the same turn: between turns
	CHECK(g_map.reads.size() == (size_t)(kCalls * 8));
	bool inOrder = true;
	for (size_t i = 0; i < g_map.reads.size(); ++i)
		inOrder = inOrder && g_map.reads[i] == (int)(i % 35);
	CHECK(inOrder);   // each cycle of 35 reads is every plot exactly once
	std::vector<int> per(35, 0);
	for (size_t i = 0; i < 105; ++i) ++per[(size_t)g_map.reads[i]];
	bool three = true;
	for (int c : per) three = three && c == 3;
	CHECK(three);
	CHECK(g_map.outside == 0);   // never a plot index past the grid
	CHECK(RollFoundLines() == 0);
	SrvFeed::Batch b = Drained();
	CHECK(b.changedPlots.empty());
	Balanced("rolling-cycle-4914");
	// K above the plot count: each pass reads every plot once
	RollStart(3, 2, 100);
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	CHECK(g_map.reads.size() == 12);
	inOrder = true;
	for (size_t i = 0; i < g_map.reads.size(); ++i) inOrder = inOrder && g_map.reads[i] == (int)(i % 6);
	CHECK(inOrder);
	CHECK(LogCount("a sweep of the 6 plots takes 1 Updates") == 1);
	// K not in the config: the default 256
	RollStart(20, 15, INT32_MIN);
	CHECK(LogCount("feed: rolling refresh: 256 plots per Update") == 1 && LogCount("a sweep of the 300 plots takes 2 Updates") == 1);
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	CHECK(g_map.reads.size() == 512 && g_map.reads[255] == 255 && g_map.reads[256] == 256 && g_map.reads[299] == 299 &&
	      g_map.reads[300] == 0 && g_map.reads[511] == 211 && g_map.outside == 0);
	Balanced("rolling-cycle-4914 default");
}

// a plot-type and terrain change with no notification (equal yields: 11.7) is found within ceil(W * H / K) LeaveUpdates,
// through the same UpdatePlot path (changedPlots, the record), with one plain line per LeaveUpdate that found any
static void TestRollingFinds()
{
	g_test = "rolling-finds-4914";
	RollStart(7, 5, 8);
	for (int i = 0; i < 3; ++i)
		SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);   // the cursor at 24
	Drained();
	// the worst case: the plot the last pass read last (23 = x 2, y 3) changes now; ceil(35 / 8) = 5 passes find it
	g_map.plots[23].plotType = 3;   // land to ocean
	g_map.plots[23].terrain = 6;
	int found = -1;
	for (int call = 1; call <= 5 && found < 0; ++call)
	{
		SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
		if (Changed(Drained(), 23)) found = call;
	}
	CHECK(found == 5);
	SrvFeed::PlotRecord r;
	CHECK(SrvFeed::GetPlot(23, r) && r.plotType == 3 && r.terrain == 6);
	CHECK(RollFoundLines() == 1);
	char want[240];
	snprintf(want, sizeof(want), "feed: plots found changed at turn %d: by the plot-state path 0 (x,y: ), by the rolling "
	                             "refresh 1 (x,y: 2,3); rolling 8 plots read in ", g_rollTurn);
	CHECK(LogCount(want) == 1);
	CHECK(LogCount("LOUD") == 0);
	// every position: a change anywhere is found within 5 LeaveUpdates, wherever the cursor stands
	bool all = true;
	for (int p = 0; p < 35; ++p)
	{
		g_map.plots[(size_t)p].plotType = g_map.plots[(size_t)p].plotType == 3 ? 2 : 3;
		int at = -1;
		for (int call = 1; call <= 5 && at < 0; ++call)
		{
			SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
			if (Changed(Drained(), (uint32_t)p)) at = call;
		}
		all = all && at > 0;
	}
	CHECK(all);
	// two changes in one pass: one line with the count 2 and both plots (never a line per plot)
	const int linesBefore = RollFoundLines();
	const size_t c = (size_t)(g_map.reads.empty() ? 0 : (g_map.reads.back() + 1) % 35);   // the next pass's first plot
	g_map.plots[c].owner = 3;
	g_map.plots[(c + 1) % 35].river = true;
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	SrvFeed::Batch b = Drained();
	CHECK(Changed(b, (uint32_t)c) && Changed(b, (uint32_t)((c + 1) % 35)) && b.changedPlots.size() == 2);
	CHECK(RollFoundLines() == linesBefore + 1 && LogCount("by the rolling refresh 2 (x,y: ") == 1);
	// a plot a notification named: read by the dirty refresh, not counted as found by the rolling read after it
	const size_t d = (c + 8) % 35;   // inside the next pass
	g_map.plots[d].feature = 2;
	MapPlot np;
	np.idx = (int)d;
	SrvRemoteFeed::OnPlotOnly(SrvFeed::kYieldChanged, &np);
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	b = Drained();
	CHECK(Changed(b, (uint32_t)d) && SrvFeed::GetPlot((uint32_t)d, r) && r.feature == 2);
	CHECK(RollFoundLines() == linesBefore + 1);
	CHECK(g_map.outside == 0);
	// the cost line at the turn change: the passes of the turn that ended, then the full refresh (no rolling pass in it)
	g_map.reads.clear();
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn + 1);
	CHECK(g_map.reads.size() == 5 + 35);   // the grid's height probe (0, 0..4), then the full refresh only
	snprintf(want, sizeof(want), "feed: rolling refresh in turn %d: ", g_rollTurn);
	int costLines = 0;
	for (const std::string& s : g_log)
		if (s.find(want) != std::string::npos && s.find(" ms per pass, longest ") != std::string::npos &&
		    s.find("; K 8") != std::string::npos)
			++costLines;
	CHECK(costLines == 1);
	Balanced("rolling-finds-4914");
}

// Ticket 0631: the snapshot reads each plot's own three river edges (IsWOfRiver 1, IsNWOfRiver 2, IsNEOfRiver 4) into
// PlotRecord::riverEdges with known bit 2; a plot with the river flag from a neighbour's edge only has 0; a refresh finds
// an edge change alone (no flag change) and lists the plot
static void TestRiverEdgesRead()
{
	g_test = "river-edges-0631";
	Fresh(false);
	g_cfgRoll = 8;
	g_map.Init(4, 3);
	g_map.plots[1].edges = 1;
	g_map.plots[1].river = true;
	g_map.plots[2].edges = 6;
	g_map.plots[2].river = true;
	g_map.plots[5].river = true;   // the flag only: the edge is plot 4's (its W of river), not its own
	g_map.plots[4].edges = 1;
	g_map.plots[4].river = true;
	g_map.plots[7].edges = 7;      // all three, no flag (a crossing count that lags)
	g_context.map = &g_map;
	g_rollTurn += 10;
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);   // a snapshot
	Drained();
	SrvFeed::PlotRecord r;
	bool all = true;
	for (uint32_t i = 0; i < 12; ++i)
		all = all && SrvFeed::GetPlot(i, r) && r.riverEdges == (uint8_t)g_map.plots[i].edges &&
		      (r.known & SrvFeed::kPlotKnownRiverEdges) && ((r.flags & 1) != 0) == g_map.plots[i].river;
	CHECK(all);
	CHECK(SrvFeed::GetPlot(5, r) && r.riverEdges == 0 && (r.flags & 1));
	bool eachRead = true;
	for (const MapPlot& p : g_map.plots) eachRead = eachRead && p.edgeReads == 3;   // the three getters once per read
	CHECK(eachRead);
	// an edge change alone (a mod's SetNWOfRiver; the flag stays) is found by the rolling refresh and listed
	g_map.plots[9].edges = 2;
	for (int call = 0; call < 2; ++call)   // 12 plots, K 8: every plot within two LeaveUpdates
		SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	SrvFeed::Batch b = Drained();
	CHECK(Changed(b, 9) && SrvFeed::GetPlot(9, r) && r.riverEdges == 2 && !(r.flags & 1));
	Balanced("river-edges-0631");
}

// a snapshot (a turn that jumps or goes back, a changed grid) starts the round robin at plot 0 again
static void TestRollingSnapshotReset()
{
	g_test = "rolling-snapshot-4914";
	RollStart(7, 5, 8);
	for (int i = 0; i < 2; ++i)
		SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);   // the cursor at 16
	CHECK(g_map.reads.size() == 16 && g_map.reads.back() == 15);
	g_rollTurn -= 3;   // a turn that goes back (a load of an older save): a snapshot
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	g_map.reads.clear();
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	CHECK(g_map.reads.size() == 8 && g_map.reads[0] == 0 && g_map.reads[7] == 7);
	// a grid of another size (a new map at the same turn, through an overflow's snapshot here: a turn jump): plot 0 of
	// the new grid, every read inside it
	g_map.Init(4, 3);
	g_rollTurn += 5;
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	char want[96];
	snprintf(want, sizeof(want), "feed: snapshot 4 x 3 at turn %d", g_rollTurn);
	CHECK(LogCount(want) == 1);
	g_map.reads.clear();
	for (int i = 0; i < 3; ++i)
		SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	bool inOrder = g_map.reads.size() == 24;
	for (size_t i = 0; i < g_map.reads.size(); ++i) inOrder = inOrder && g_map.reads[i] == (int)(i % 12);
	CHECK(inOrder && g_map.outside == 0);
	// the map's width no longer the table's, without a snapshot yet: no plot read, one plain line, the cursor at 0
	g_map.width = 9;
	g_map.reads.clear();
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	CHECK(g_map.reads.empty() && LogCount("feed: rolling refresh skipped at turn") == 1);
	g_map.width = 4;
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	CHECK(g_map.reads.size() == 8 && g_map.reads[0] == 0);
	Balanced("rolling-snapshot-4914");
}

// feed_rolling_plots=0 (the comparison switch): no rolling read, a change is seen only at the turn change; negative
// values count as 0. The negative cases keep their outputs: no map, a grid that cannot be read.
static void TestRollingOffAndNegative()
{
	g_test = "rolling-off-4914";
	RollStart(7, 5, 0);
	CHECK(LogCount("feed: rolling refresh off (server.cfg feed_rolling_plots=0)") == 1);
	g_log.clear();   // RollStart's turn change may have logged the previous test's cost line
	const int wrappers = g_wrappers;
	g_map.plots[4].plotType = 3;
	for (int i = 0; i < 10; ++i)
		SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	CHECK(g_map.reads.empty() && g_wrappers == wrappers);   // the early return: not even GetMap
	CHECK(Drained().changedPlots.empty());
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn + 1);   // the turn change's full refresh finds it
	CHECK(Changed(Drained(), 4) && LogCount("feed: rolling refresh in turn") == 0);
	Balanced("rolling-off-4914");
	RollStart(7, 5, -5);
	CHECK(LogCount("feed: rolling refresh off (server.cfg feed_rolling_plots=-5)") == 1);
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	CHECK(g_map.reads.empty());
	// no map, with the table of the snapshot above: the LeaveUpdate returns (counted "no map"), nothing read, no line
	RollStart(7, 5, 8);
	g_context.map = nullptr;
	g_log.clear();
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	CHECK(g_map.reads.empty() && g_log.empty());
	g_context.map = &g_map;
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	CHECK(g_map.reads.size() == 8 && g_map.reads[0] == 0);   // the cursor where it was
	// a grid that cannot be read at the next snapshot (PlotNum(0, 1) 0): the existing line, no snapshot, no rolling
	// read; the snapshot is retried and the rolling refresh starts again after it
	g_map.width = 0;
	g_map.reads.clear();
	g_log.clear();
	g_rollTurn += 10;
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	CHECK(LogCount("feed: no plot grid readable at turn") <= 1 && g_map.reads.empty() &&
	      LogCount("feed: plots found changed") == 0 && LogCount("feed: rolling refresh: ") == 0 &&
	      LogCount("feed: snapshot") == 0);
	g_map.width = 7;
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	CHECK(LogCount("feed: snapshot 7 x 5") == 1);
	Balanced("rolling-off-4914");
	// the report line is unchanged in form (the session summary is not the rolling refresh's)
	char buf[4096] = {};
	FILE* f = nullptr;
	tmpfile_s(&f);
	if (f)
	{
		SrvRemoteFeed::WriteTotals(f);
		rewind(f);
		const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
		buf[n] = 0;
		fclose(f);
	}
	CHECK(strstr(buf, "feed: on; events pushed ") && strstr(buf, ", no map ") && strstr(buf, ", bad grid ") &&
	      !strstr(buf, "rolling"));
	g_context.map = nullptr;
	g_cfgRoll = INT32_MIN;
}

// MC #9712 (i): a GameplayPlotStateChange marks its plot dirty: a plot-type + terrain change with no other notification
// is re-read at the very next LeaveUpdate (rolling refresh off, so only that path can find it), and the per-update line
// names the path per plot; with the rolling refresh on, a plot both paths would read is counted once, by the plot-state
// path (the dirty plots are read first)
static void TestStatePath()
{
	g_test = "state-path-9712";
	RollStart(7, 5, 0);
	g_map.plots[12].plotType = 3;   // (5,1) land to ocean, no notification of its own
	g_map.plots[12].terrain = 6;
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);   // nothing marks it: nothing read (K 0)
	SrvFeed::PlotRecord r;
	CHECK(g_map.reads.empty() && SrvFeed::GetPlot(12, r) && r.plotType == 2);
	SrvRemoteFeed::OnPlotStateChange(&g_map.plots[12], -1, -1, 0, -1, 0);   // the EXE's layout pass for the plot
	CHECK(SrvFeed::GetPlot(12, r) && r.plotType == 2);   // the call itself carries layout fields only
	Drained();
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	CHECK(g_map.reads.size() == 1 && g_map.reads[0] == 12);
	CHECK(SrvFeed::GetPlot(12, r) && r.plotType == 3 && r.terrain == 6);
	CHECK(Changed(Drained(), 12));
	char want[200];
	snprintf(want, sizeof(want), "feed: plots found changed at turn %d: by the plot-state path 1 (x,y: 5,1), by the "
	                             "rolling refresh 0 (x,y: ); rolling 0 plots read in ", g_rollTurn);
	CHECK(LogCount(want) == 1);
	// a plot-state change with no getter change: read, no line
	SrvRemoteFeed::OnPlotStateChange(&g_map.plots[12], -1, -1, 0, 0, 0);   // a road appears: layout only
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	CHECK(g_map.reads.size() == 2 && RollFoundLines() == 1);
	Balanced("state-path-9712");
	// rolling on: the same plot named by the plot-state path and inside the rolling pass -> found once, by the state path
	RollStart(7, 5, 8);
	g_map.plots[3].plotType = 1;   // (3,0) inside the first pass (plots 0-7)
	SrvRemoteFeed::OnPlotStateChange(&g_map.plots[3], -1, -1, 0, -1, 0);
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	CHECK(Changed(Drained(), 3) && SrvFeed::GetPlot(3, r) && r.plotType == 1);
	CHECK(LogCount("by the plot-state path 1 (x,y: 3,0), by the rolling refresh 0 (x,y: ); rolling 8 plots read") == 1);
	// one by each path in the same LeaveUpdate: one line with both
	g_map.plots[9].plotType = 0;    // (2,1) inside the next pass (8-15), no notification
	g_map.plots[30].plotType = 0;   // (2,4) outside it, named by a plot-state change
	SrvRemoteFeed::OnPlotStateChange(&g_map.plots[30], -1, -1, 0, -1, 0);
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	CHECK(LogCount("by the plot-state path 1 (x,y: 2,4), by the rolling refresh 1 (x,y: 2,1);") == 1 &&
	      RollFoundLines() == 2);
	// before any table (a load before the first snapshot): the mark is dropped with the dirty list at the snapshot
	Balanced("state-path-9712 rolling");
	g_context.map = nullptr;
	g_cfgRoll = INT32_MIN;
}

// ---- ticket 0529 (R2 of 0424's premise check): the plot snapshot under the load screen ----
// The EXE's load calls ICvPlayer1::SetupGraphical for players 0..63 (the hook calls AfterSetupGraphical after each), outside
// any Update; the last call takes the snapshot, the first LeaveUpdate after it re-reads every plot.
static void LoadCalls(int first, int last)
{
	for (int p = first; p <= last; ++p)
		SrvRemoteFeed::AfterSetupGraphical(p);
}

static uint32_t TableGen(std::vector<SrvFeed::PlotRecord>* pRecs = nullptr)
{
	std::vector<SrvFeed::PlotRecord> recs;
	uint32_t gen = 0, w = 0, h = 0;
	SrvFeed::CopyPlots(recs, gen, w, h);
	if (pRecs) pRecs->swap(recs);
	return gen;
}

// FogOfWarModeTypes (0 off, 1 unexplored, 2 no vis) -> PlotRecord::fow (bit 0 revealed, bit 1 visible)
static uint8_t Bits(int mode) { return (uint8_t)(mode == 0 ? 3 : mode == 2 ? 1 : 0); }

// the early snapshot: taken at the last call only, with the save's fog bits, a new generation even at the same turn (a
// reload); the first Update re-reads every plot without a new generation and names the record that changed in between;
// later Updates are the rolling refresh again; the next load takes its own
static void TestLoadSnapshot()
{
	g_test = "load-snapshot-0529";
	RollStart(7, 5, 8);   // an earlier load's table (its snapshot at an Update)
	const uint32_t gen0 = TableGen();
	for (int i = 0; i < 35; ++i) g_map.plots[(size_t)i].fow = i % 3 == 0 ? 0 : i % 3 == 1 ? 2 : 1;   // the save's fog
	g_game.turn = g_rollTurn;   // the same turn: a reload, which an Update's rule alone would not snapshot
	g_inUpdate = false;
	g_log.clear();
	g_map.reads.clear();
	LoadCalls(0, 62);
	CHECK(g_map.reads.empty() && LogCount("feed: snapshot") == 0 && LogCount("load snapshot") == 0);
	SrvRemoteFeed::AfterSetupGraphical(63);
	char want[400];
	snprintf(want, sizeof(want), "feed: snapshot 7 x 5 at turn %d (35 plots read", g_rollTurn);
	CHECK(LogCount(want) == 1 && g_map.reads.size() == 40);   // the grid probe's 5 rows (Grid), then the 35 plots
	snprintf(want, sizeof(want), "taken under the load screen at ICvPlayer1::SetupGraphical(63), turn %d (", g_rollTurn);
	CHECK(LogCount(want) == 1 && LogCount("the first Update re-reads every plot (ticket 0529)") == 1);
	std::vector<SrvFeed::PlotRecord> recs;
	const uint32_t gen1 = TableGen(&recs);
	CHECK(gen1 == gen0 + 1 && recs.size() == 35);
	bool fogOk = recs.size() == 35;
	for (size_t i = 0; fogOk && i < recs.size(); ++i) fogOk = recs[i].fow == Bits(g_map.plots[i].fow);
	CHECK(fogOk);
	SrvFeed::Batch b = Drained();
	int snaps = 0;
	for (const SrvFeed::Event& e : b.events)
		if (e.kind == SrvFeed::kSnapshot) ++snaps;
	CHECK(b.snapshotGen == gen1 && snaps == 1 && b.width == 7 && b.height == 5);
	Balanced("load-snapshot-0529 load");
	// between the load and the first Update a plot's fog changes with no notification (CvPlot::Validate's kind of change)
	g_map.plots[10].fow = 0;   // (3,1): no vis -> visible
	g_inUpdate = true;
	g_map.reads.clear();
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);   // the first Update after the click
	CHECK(g_map.reads.size() == 40);   // the grid probe and every plot, no rolling pass
	const uint32_t gen2 = TableGen(&recs);
	CHECK(gen2 == gen1 && recs.size() == 35 && recs[10].fow == 3);
	CHECK(Changed(Drained(), 10));
	snprintf(want, sizeof(want), "feed: the load's snapshot re-read at the first Update (turn %d): 1 of 35 records changed "
	                             "(x,y: 3,1); fields: terrain 0, plot type 0, feature 0, improvement 0, owner 0, river 0, natural "
	                             "wonder 0, fog 1, art type 0 (ticket 0529)", g_rollTurn);
	CHECK(LogCount(want) == 1 && LogCount("feed: snapshot ") == 1);
	// the next Updates: the rolling refresh, no second re-read
	g_map.reads.clear();
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	CHECK(g_map.reads.size() == 8 && g_map.reads[0] == 0 && LogCount("re-read at the first Update") == 1);
	// a re-read that finds nothing: the line says 0
	g_inUpdate = false;
	LoadCalls(0, 63);   // the next load
	CHECK(LogCount("feed: snapshot ") == 2 && LogCount("taken under the load screen") == 2 && TableGen() == gen1 + 1);
	g_inUpdate = true;
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	CHECK(LogCount("re-read at the first Update") == 2 && LogCount(": 0 of 35 records changed (x,y: ); fields: terrain 0") == 1);
	// the report line
	char buf[4096] = {};
	FILE* f = nullptr;
	tmpfile_s(&f);
	if (f)
	{
		SrvRemoteFeed::WriteTotals(f);
		rewind(f);
		const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
		buf[n] = 0;
		fclose(f);
	}
	CHECK(strstr(buf, "feed load snapshots (0529): taken ") && strstr(buf, "; first-Update re-reads "));
	Balanced("load-snapshot-0529");
	g_context.map = nullptr;
	g_cfgRoll = INT32_MIN;
}

// a load whose sequence never reaches the last call: no snapshot, no DLL call at the load; the first Update takes it as
// before 0529 (here a turn that jumps); an Update ends the sequence, so a lone 63 after it is no load
static void TestLoadSnapshotNotReached()
{
	g_test = "load-not-reached-0529";
	RollStart(7, 5, 8);
	g_inUpdate = false;
	g_log.clear();
	g_map.reads.clear();
	const int wrappers = g_wrappers;
	LoadCalls(0, 40);
	CHECK(g_map.reads.empty() && g_log.empty() && g_wrappers == wrappers);
	g_inUpdate = true;
	g_rollTurn += 10;
	g_game.turn = g_rollTurn;
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	char want[96];
	snprintf(want, sizeof(want), "feed: snapshot 7 x 5 at turn %d (35 plots read", g_rollTurn);
	CHECK(LogCount(want) == 1 && LogCount("load snapshot") == 0 && LogCount("re-read at the first Update") == 0);
	g_inUpdate = false;
	SrvRemoteFeed::AfterSetupGraphical(63);   // above the previous id (40) and after an Update: not a load
	CHECK(LogCount("feed: snapshot ") == 1 && LogCount("load snapshot") == 0);
	g_inUpdate = true;
	g_map.reads.clear();
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	CHECK(g_map.reads.size() == 8);   // the rolling refresh: no re-read of every plot
	Balanced("load-not-reached-0529");
	g_context.map = nullptr;
	g_cfgRoll = INT32_MIN;
}

// the sequence reaches 63 but the snapshot is not taken there: inside an Update (not a load), the switch off (the behaviour
// before 0529), no active team, no grid, no context, the feed off. One line each; with the lock held (no team, no grid)
// the first Update takes the snapshot whatever the turn says; otherwise nothing changes
static void TestLoadSnapshotRefused()
{
	g_test = "load-refused-0529";
	RollStart(7, 5, 8);
	g_game.turn = g_rollTurn;
	char want[96];
	snprintf(want, sizeof(want), "feed: snapshot 7 x 5 at turn %d", g_rollTurn);
	// inside an Update: no GetGame, no read
	g_log.clear();
	g_map.reads.clear();
	int wrappers = g_wrappers;
	g_inUpdate = true;
	LoadCalls(0, 63);
	CHECK(g_map.reads.empty() && g_wrappers == wrappers &&
	      LogCount("feed: no load snapshot at ICvPlayer1::SetupGraphical(63): the call came inside an ICvGame1::Update") == 1);
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	CHECK(g_map.reads.size() == 8 && LogCount("feed: snapshot ") == 0);   // the rolling refresh only
	// the switch off: as before 0529 (no snapshot at the load, none forced at the same turn's Update)
	g_cfgLoadSnap = 0;
	g_inUpdate = false;
	g_map.reads.clear();
	wrappers = g_wrappers;
	LoadCalls(0, 63);
	CHECK(g_map.reads.empty() && g_wrappers == wrappers && LogCount("server.cfg feed_load_snapshot=0;") == 1);
	g_inUpdate = true;
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	CHECK(g_map.reads.size() == 8 && LogCount("feed: snapshot ") == 0);
	g_cfgLoadSnap = INT32_MIN;
	// no active team: the game is read (and released), no plot; the first Update's snapshot is forced at the same turn
	g_game.activeTeam = -1;
	g_inUpdate = false;
	g_map.reads.clear();
	LoadCalls(0, 63);
	CHECK(g_map.reads.empty() && LogCount("no active team (the fog bits would all read unexplored)") == 1);
	g_game.activeTeam = 0;
	g_inUpdate = true;
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	CHECK(LogCount(want) == 1 && g_map.reads.size() == 40 && LogCount("re-read at the first Update") == 0);
	Balanced("load-refused-0529 team");
	// no grid at the load: the same
	g_map.width = 0;
	g_inUpdate = false;
	LoadCalls(0, 63);
	CHECK(LogCount("SetupGraphical(63): no plot grid readable;") == 1 && LogCount(want) == 1);
	g_map.width = 7;
	g_inUpdate = true;
	SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
	CHECK(LogCount(want) == 2);
	// no game context: nothing read
	g_ctx = nullptr;
	g_inUpdate = false;
	g_map.reads.clear();
	LoadCalls(0, 63);
	CHECK(g_map.reads.empty() && LogCount("SetupGraphical(63): no game context;") == 1);
	g_ctx = &g_context;
	// the feed off: nothing at all
	SrvFeed::SetEnabled(false);
	g_log.clear();
	wrappers = g_wrappers;
	LoadCalls(0, 63);
	CHECK(g_log.empty() && g_map.reads.empty() && g_wrappers == wrappers);
	SrvFeed::SetEnabled(true);
	g_inUpdate = true;
	Balanced("load-refused-0529");
	g_context.map = nullptr;
	g_cfgRoll = INT32_MIN;
}

// The rolling slice's cost on the stand-ins (the feed's own share: ReadPlot's calls into trivial getters, UpdatePlot's
// lock and compare; the DLL's getters cost more): huge (128 x 80) and Tiny (56 x 36) at K 256, printed as info lines
static void TestRollingTiming()
{
	g_test = "rolling-timing-4914";
	const int sizes[2][2] = {{56, 36}, {128, 80}};
	for (int s = 0; s < 2; ++s)
	{
		RollStart(sizes[s][0], sizes[s][1], 256);
		const int kPasses = 2000;
		LARGE_INTEGER f, t0, t1;
		QueryPerformanceFrequency(&f);
		QueryPerformanceCounter(&t0);
		for (int i = 0; i < kPasses; ++i)
		{
			SrvRemoteFeed::AtLeaveUpdate(&g_context, &g_game, g_rollTurn);
			if ((i & 63) == 63) g_map.reads.clear();
		}
		QueryPerformanceCounter(&t1);
		const double us = 1e6 * (double)(t1.QuadPart - t0.QuadPart) / (double)f.QuadPart / kPasses;
		printf("info rolling-timing-4914: %d x %d, K 256: %.2f us per LeaveUpdate (whole call, stand-in getters, %d passes)\n",
		       sizes[s][0], sizes[s][1], us, kPasses);
		CHECK(us > 0.0 && RollFoundLines() == 0);
		Balanced("rolling-timing-4914");
	}
	g_context.map = nullptr;
	g_cfgRoll = INT32_MIN;
}

int main()
{
	TestCreated();
	TestMoved();
	TestPlots();
	TestDestroyed();
	TestSimpleKinds();
	TestVisibility();
	TestCombat();
	TestCityEvents();
	TestLoadAndEra();
	TestPlayerRows();
	TestUnitsOff();
	TestNulls();
	TestException();
	TestDropped();
	TestFacingRule();   // issue 4463
	TestEmbarkArt();    // issue 0439
	TestCombatInterceptorAndDamage();   // issue 4726 (last: its loads move the feed's load count TestLoadAndEra reads)
	TestRollingCycle();             // ticket 4914 (epic 0615): the rolling refresh
	TestRollingFinds();
	TestRiverEdgesRead();           // ticket 0631
	TestRollingSnapshotReset();
	TestRollingOffAndNegative();
	TestStatePath();                // MC #9712: MarkDirty in OnPlotStateChange
	TestLoadSnapshot();             // ticket 0529: the plot snapshot under the load screen
	TestLoadSnapshotNotReached();
	TestLoadSnapshotRefused();
	TestRollingTiming();
#ifdef FEED_TEST_INJECT_FAIL
	g_test = "injected";
	CHECK(1 == 2);
#endif
	printf("test_remote_feed: %d checks passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
