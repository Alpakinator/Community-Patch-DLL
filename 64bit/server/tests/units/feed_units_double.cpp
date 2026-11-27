// feed_units_double.cpp - a test double of the unit part of SrvFeed (server/src/feed/srv_feed.h: PushUnit, SetPlayer,
// ResetUnits, CopyUnits, CopyPlayers, SetUnitDatabase, GetUnitDatabase, SetUnitsEnabled, UnitsEnabled) for the unit-scene
// tests of issue 4454 while issue 4452's implementation is not merged. Linked ONLY by
// server/tests/units/build_units_tests.py, and only while server/src/feed/srv_feed.cpp does not define CopyUnits.
// Written 2026-11-15 for this repository (GPL-3.0-or-later). Plain in-memory state, no lock (the tests are one thread).
#include "feed/srv_feed.h"

#include <algorithm>

namespace SrvFeed {

namespace {
std::vector<UnitRecord> g_units;
std::vector<PlayerRecord> g_players;
uint32_t g_gen = 0, g_seq = 0, g_dbGen = 0;
std::wstring g_db;
bool g_on = false, g_haveDb = false;
}

uint32_t PushUnit(Event e, const UnitRecord* rec, UnitOp op)
{
	if(!g_on)
		return 0;
	e.seq = ++g_seq;
	if(rec && op != UnitOp::kNone)
	{
		auto it = std::find_if(g_units.begin(), g_units.end(), [&](const UnitRecord& u) { return u.owner == rec->owner && u.id == rec->id; });
		if(op == UnitOp::kRemove)
		{
			if(it != g_units.end())
				g_units.erase(it);
		}
		else if(it != g_units.end())
			*it = *rec;
		else
			g_units.push_back(*rec);
	}
	Push(e);
	return e.seq;
}
void SetPlayer(const PlayerRecord& r) { g_players.push_back(r); }
void ResetUnits()
{
	g_units.clear();
	g_gen++;
}
void CopyUnits(std::vector<UnitRecord>& out, uint32_t& gen)
{
	out = g_units;
	gen = g_gen;
}
void CopyPlayers(std::vector<PlayerRecord>& out) { out = g_players; }
void SetUnitDatabase(const std::wstring& path, uint32_t gen)
{
	g_db = path;
	g_dbGen = gen;
	g_haveDb = true;
}
bool GetUnitDatabase(std::wstring& path, uint32_t& gen)
{
	path = g_db;
	gen = g_dbGen;
	return g_haveDb;
}
void SetUnitsEnabled(bool on) { g_on = on; }
bool UnitsEnabled() { return g_on; }

}  // namespace SrvFeed
