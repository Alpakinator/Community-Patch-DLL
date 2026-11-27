// test_units_records.cpp - tests of the unit feed's records 3-6 (server/src/units/srv_units_records.*; epic 0375, issue
// 4457). Written 2026-11-16 for this repository (GPL-3.0-or-later). Built and run by run_units_records_test.py.
//
//   test_units_records.exe                 every check; "<n> passed, <m> failed", exit 0 only when m == 0
//   test_units_records.exe --decode FILE [--format V]
//                                          decodes a file of framed records 3-6 (u32 type, u32 bytes, payload) with
//                                          format_version V (default 4): "records: OK <n> records" exit 0, or
//                                          "records: REFUSED <Err> record <k>: <why>" exit 10 + the code; usage errors
//                                          exit 2.
#include "units/srv_units_records.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

using namespace srvunits::rec;
using SrvFeed::UnitRecord;
using SrvFeed::PlayerRecord;
using SrvFeed::CombatRecord;
using SrvFeed::CombatMember;

static int g_pass = 0, g_fail = 0;
#define CHECK(c)                                                                                                         \
	do                                                                                                                   \
	{                                                                                                                    \
		if(c)                                                                                                            \
			++g_pass;                                                                                                    \
		else                                                                                                             \
		{                                                                                                                \
			++g_fail;                                                                                                    \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                          \
		}                                                                                                                \
	} while(0)

static UnitRecord U(int owner, int id, int plot, int seq)
{
	UnitRecord u;
	memset(&u, 0, sizeof(u));
	u.owner = (int8_t)owner;
	u.domain = 1;
	u.facing = 2;
	u.flags = SrvFeed::kUnitFlagVisible;
	u.id = id;
	u.unitType = 7 + id % 5;
	u.plot = plot;
	u.damage = (int16_t)(id % 30);
	u.maxHP = 100;
	u.artStyle = 3;
	u.era = 1;
	u.seq = (uint32_t)seq;
	return u;
}

static PlayerRecord P(int player, int colour)
{
	PlayerRecord p;
	memset(&p, 0, sizeof(p));
	p.player = (int8_t)player;
	p.team = (int8_t)player;
	p.alive = 1;
	p.flags = player >= 22 ? 1 : 0;
	p.colour = colour;
	p.civilization = 4 + player;
	p.era = 2;
	p.artStyle = 1;
	p.changeGen = (uint32_t)(10 + player);
	p.activeTeam = 0;
	p.state = 3;
	return p;
}

static bool sameUnits(const std::vector<UnitRecord>& a, const std::vector<UnitRecord>& b)
{
	return a.size() == b.size() && (a.empty() || memcmp(a.data(), b.data(), a.size() * sizeof(UnitRecord)) == 0);
}

// Splits framed records; false when the framing itself is short.
struct Framed { uint32_t type; std::vector<uint8_t> payload; };
static bool split(const std::vector<uint8_t>& b, std::vector<Framed>& out)
{
	size_t at = 0;
	while(at < b.size())
	{
		if(b.size() - at < 8)
			return false;
		Framed f;
		uint32_t n = 0;
		memcpy(&f.type, &b[at], 4);
		memcpy(&n, &b[at + 4], 4);
		at += 8;
		if(b.size() - at < n)
			return false;
		f.payload.assign(b.begin() + (ptrdiff_t)at, b.begin() + (ptrdiff_t)(at + n));
		at += n;
		out.push_back(f);
	}
	return true;
}

static Result dec(Decoder& d, const std::vector<uint8_t>& framed, Decoded* out)
{
	std::vector<Framed> f;
	if(!split(framed, f) || f.size() != 1)
	{
		Result r;
		r.err = Err::kTruncated;
		r.why = "test framing";
		return r;
	}
	return d.decode(f[0].type, f[0].payload.data(), f[0].payload.size(), out);
}

// payload builder for hand-made (bad) records
struct B
{
	std::vector<uint8_t> v;
	template<class T> B& w(const T& x)
	{
		const uint8_t* p = reinterpret_cast<const uint8_t*>(&x);
		v.insert(v.end(), p, p + sizeof(T));
		return *this;
	}
};
static Result decRaw(Decoder& d, uint32_t type, const B& b, Decoded* out = nullptr)
{
	return d.decode(type, b.v.empty() ? nullptr : b.v.data(), b.v.size(), out);
}
static bool refused(const Result& r, Err e, const char* text)
{
	const bool ok = r.err == e && r.why.find(text) != std::string::npos;
	if(!ok)
		printf("  got %s (want %s, \"%s\")\n", r.line().c_str(), ErrName(e), text);
	return ok;
}

static void testUnitsRoundTrip()
{
	Encoder e;
	Decoder d(4);
	std::vector<UnitRecord> t0 = {U(1, 1005, 40, 9), U(0, 1001, 10, 3), U(0, 1003, 12, 4), U(5, 1002, 99, 7)};
	std::vector<uint8_t> b;
	CHECK(e.addUnits(10, kUnitsFirstCopy, 1, t0, &b).ok());
	Decoded r;
	CHECK(dec(d, b, &r).ok());
	std::vector<UnitRecord> sorted = {U(0, 1001, 10, 3), U(0, 1003, 12, 4), U(1, 1005, 40, 9), U(5, 1002, 99, 7)};
	CHECK(r.type == kTypeUnits && r.loopIter == 10 && r.caller == 1 && r.tableGen == 1 && r.full);
	CHECK(sameUnits(r.units, sorted));
	// a later copy with one change, one removal, one new unit: a diff, smaller than the table
	std::vector<UnitRecord> t1 = sorted;
	t1[1].plot = 13;
	t1[1].seq = 20;
	t1.erase(t1.begin() + 2);
	t1.push_back(U(3, 1010, 50, 21));
	b.clear();
	CHECK(e.addUnits(11, kUnitsLaterCopy, 1, t1, &b).ok());
	CHECK(dec(d, b, &r).ok());
	CHECK(!r.full && r.nUpsert == 2 && r.nRemove == 1);
	std::vector<UnitRecord> want1 = {U(0, 1001, 10, 3), t1[1], U(3, 1010, 50, 21), U(5, 1002, 99, 7)};
	CHECK(sameUnits(r.units, want1));
	CHECK(b.size() < 8 + 24 + 4 * 32);
	// unchanged: an empty diff
	b.clear();
	CHECK(e.addUnits(11, kUnitsLaterCopy, 1, want1, &b).ok());
	CHECK(dec(d, b, &r).ok());
	CHECK(!r.full && r.nUpsert == 0 && r.nRemove == 0 && sameUnits(r.units, want1));
	CHECK(b.size() == 8 + 28);
	// a new tableGen (a load): full again, even when it would be larger than a diff
	b.clear();
	CHECK(e.addUnits(12, kUnitsLaterCopy, 2, want1, &b).ok());
	CHECK(dec(d, b, &r).ok());
	CHECK(r.full && r.tableGen == 2 && sameUnits(r.units, want1));
	// forceFull and caller 1 store full tables
	b.clear();
	CHECK(e.addUnits(12, kUnitsLaterCopy, 2, want1, &b, true).ok());
	CHECK(dec(d, b, &r).ok() && r.full);
	b.clear();
	CHECK(e.addUnits(13, kUnitsFirstCopy, 2, want1, &b).ok());
	CHECK(dec(d, b, &r).ok() && r.full && r.caller == 1);
	// an empty table (after a load's reset) and back
	b.clear();
	CHECK(e.addUnits(14, kUnitsLaterCopy, 3, std::vector<UnitRecord>(), &b).ok());
	CHECK(dec(d, b, &r).ok() && r.full && r.units.empty());
	b.clear();
	CHECK(e.addUnits(15, kUnitsLaterCopy, 3, sorted, &b).ok());
	CHECK(dec(d, b, &r).ok() && r.full && sameUnits(r.units, sorted));   // a diff of 4 upserts (156 B) is not smaller
	// a diff larger than the full table falls back to full: every record changed and a removal
	std::vector<UnitRecord> t2 = sorted;
	for(UnitRecord& u : t2)
		u.seq += 100;
	t2.pop_back();
	b.clear();
	CHECK(e.addUnits(16, kUnitsLaterCopy, 3, t2, &b).ok());
	CHECK(dec(d, b, &r).ok() && r.full && sameUnits(r.units, t2));
}

static void testPathsCombatsPlayers()
{
	Encoder e;
	Decoder d(4);
	SrvFeed::Batch b;
	b.pathPlots = {5, 6, 7, 100, 101, 9, 8};
	b.paths = {{30, 0, 3}, {31, 3, 2}, {35, 5, 0}, {36, 5, 2}};
	std::vector<uint8_t> out;
	CHECK(e.addPaths(40, b, &out).ok());
	Decoded r;
	CHECK(dec(d, out, &r).ok());
	CHECK(r.type == kTypePaths && r.loopIter == 40 && r.paths.size() == 4 && r.pathPlots == b.pathPlots);
	bool same = true;
	for(size_t i = 0; i < 4; ++i)
		same &= r.paths[i].seq == b.paths[i].seq && r.paths[i].first == b.paths[i].first && r.paths[i].length == b.paths[i].length;
	CHECK(same);
	CHECK(out.size() == 8 + 12 + 4 * 8 + 7 * 4);
	// combats: two with members, one without; members in batch order
	CombatRecord c0;
	memset(&c0, 0, sizeof(c0));
	c0.seq = 50;
	c0.kind = SrvFeed::kUnitCombat;
	c0.flags = SrvFeed::kCombatVisualize | SrvFeed::kCombatReturnedKnown | SrvFeed::kCombatGameRead;
	c0.plot = 77;
	c0.attackerOwner = 0;
	c0.attackerUnit = 1001;
	c0.attackerCity = -1;
	c0.defenderOwner = 1;
	c0.defenderUnit = 1005;
	c0.defenderCity = -1;
	c0.finalDamage[0] = 10;
	c0.finalDamage[1] = 45;
	c0.returned = 1;
	c0.firstMember = 0;
	c0.nMembers = 2;
	CombatRecord c1 = c0;
	c1.seq = 51;
	c1.kind = SrvFeed::kCityCombat;
	c1.firstMember = 2;
	c1.nMembers = 0;
	CombatRecord c2 = c0;
	c2.seq = 52;
	c2.firstMember = 2;
	c2.nMembers = 1;
	CombatMember m;
	memset(&m, 0, sizeof(m));
	b.combatMembers.clear();
	for(int i = 0; i < 3; ++i)
	{
		m.player = i;
		m.id = 1000 + i;
		m.flags = (uint16_t)(i == 1);
		m.damage = (int16_t)(5 * i);
		m.finalDamage = (int16_t)(7 * i);
		m.maxHP = 100;
		m.x = (int16_t)i;
		m.y = (int16_t)(2 * i);
		m.combatType = 3;
		m.era = 1;
		b.combatMembers.push_back(m);
	}
	b.combats = {c0, c1, c2};
	out.clear();
	CHECK(e.addCombats(41, b, &out).ok());
	CHECK(dec(d, out, &r).ok());
	CHECK(r.combats.size() == 3 && r.combatMembers.size() == 3);
	CHECK(memcmp(r.combats.data(), b.combats.data(), 3 * sizeof(CombatRecord)) == 0);
	CHECK(memcmp(r.combatMembers.data(), b.combatMembers.data(), 3 * sizeof(CombatMember)) == 0);
	CHECK(out.size() == 8 + 12 + 3 * 64 + 3 * 28);
	// a batch whose members are not in combat order: the file renumbers firstMember (members follow their combat)
	SrvFeed::Batch b2 = b;
	b2.combatMembers = {b.combatMembers[2], b.combatMembers[0], b.combatMembers[1]};
	b2.combats[0].firstMember = 1;
	b2.combats[2].firstMember = 0;
	out.clear();
	CHECK(e.addCombats(42, b2, &out).ok());
	CHECK(dec(d, out, &r).ok());
	CHECK(memcmp(r.combats.data(), b.combats.data(), 3 * sizeof(CombatRecord)) == 0);
	CHECK(memcmp(r.combatMembers.data(), b.combatMembers.data(), 3 * sizeof(CombatMember)) == 0);
	// players: a copy (sorted) and a batch's rows (any order)
	std::vector<PlayerRecord> rows = {P(0, 5), P(1, 9), P(22, 40), P(63, 2)};
	out.clear();
	CHECK(e.addPlayers(43, kPlayersCopy, rows, &out).ok());
	CHECK(dec(d, out, &r).ok());
	CHECK(r.caller == 1 && r.players.size() == 4 && memcmp(r.players.data(), rows.data(), 4 * sizeof(PlayerRecord)) == 0);
	std::vector<PlayerRecord> brows = {P(22, 41), P(0, 6)};
	out.clear();
	CHECK(e.addPlayers(43, kPlayersBatch, brows, &out).ok());
	CHECK(dec(d, out, &r).ok());
	CHECK(r.caller == 2 && r.players.size() == 2 && memcmp(r.players.data(), brows.data(), 2 * sizeof(PlayerRecord)) == 0);
	// empty records are legal
	out.clear();
	CHECK(e.addPlayers(44, kPlayersBatch, std::vector<PlayerRecord>(), &out).ok());
	CHECK(dec(d, out, &r).ok() && r.players.empty());
	out.clear();
	CHECK(e.addPaths(44, SrvFeed::Batch(), &out).ok());
	CHECK(dec(d, out, &r).ok() && r.paths.empty());
}

static B unitsHead(uint64_t loop, uint32_t caller, uint32_t gen, uint32_t full)
{
	B b;
	b.w(loop).w(caller).w(gen).w(full);
	return b;
}

static void testRefusals()
{
	// ---- format, type, loop ----
	{
		Decoder d(3);
		B b;
		b.w((uint64_t)1).w((uint32_t)0);
		CHECK(refused(decRaw(d, kTypePaths, b), Err::kOlderFormat, "needs format_version 4; the bundle has 3"));
		Decoder d4(4);
		CHECK(refused(decRaw(d4, 7, b), Err::kUnknownType, "record type 7"));
		CHECK(refused(decRaw(d4, 2, b), Err::kUnknownType, "record type 2"));
		CHECK(decRaw(d4, kTypePaths, b).ok());
		B back;
		back.w((uint64_t)0).w((uint32_t)0);
		CHECK(refused(decRaw(d4, kTypePaths, back), Err::kLoopBack, "loopIter 0 below the previous record's 1"));
		CHECK(refused(d4.noteLoopIter(0, "DRAIN"), Err::kLoopBack, "DRAIN loopIter 0"));
		CHECK(d4.noteLoopIter(5, "DRAIN").ok());
		B b4;
		b4.w((uint64_t)4).w((uint32_t)0);
		CHECK(refused(decRaw(d4, kTypePaths, b4), Err::kLoopBack, "PATHS loopIter 4 below the previous record's 5"));
		B empty;
		CHECK(refused(decRaw(d4, kTypeUnits, empty), Err::kTruncated, "no loopIter"));
	}
	// ---- UNITS ----
	{
		Decoder d(4);
		UnitRecord u = U(0, 1001, 3, 1), v = U(0, 1002, 4, 2);
		CHECK(refused(decRaw(d, kTypeUnits, B().w((uint64_t)1).w((uint32_t)1)), Err::kTruncated, "short of its 20 fixed bytes"));
		CHECK(refused(decRaw(d, kTypeUnits, unitsHead(1, 3, 1, 1).w((uint32_t)0)), Err::kBadRecord, "caller 3"));
		CHECK(refused(decRaw(d, kTypeUnits, unitsHead(1, 1, 1, 2).w((uint32_t)0)), Err::kBadRecord, "full 2"));
		CHECK(refused(decRaw(d, kTypeUnits, unitsHead(1, 1, 1, 1)), Err::kTruncated, "no count"));
		CHECK(refused(decRaw(d, kTypeUnits, unitsHead(1, 1, 1, 1).w((uint32_t)kMaxUnits + 1)), Err::kBadRecord, "count 4194305 above"));
		CHECK(refused(decRaw(d, kTypeUnits, unitsHead(1, 1, 1, 1).w((uint32_t)2).w(u)), Err::kTruncated, "count 2 needs 64 bytes, 32 left"));
		CHECK(refused(decRaw(d, kTypeUnits, unitsHead(1, 1, 1, 1).w((uint32_t)1).w(u).w((uint8_t)0)), Err::kWrongSize, "1 bytes after"));
		UnitRecord bad = u;
		bad.owner = 64;
		CHECK(refused(decRaw(d, kTypeUnits, unitsHead(1, 1, 1, 1).w((uint32_t)1).w(bad)), Err::kBadRecord, "owner 64 outside 0..63"));
		bad.owner = -1;
		CHECK(refused(decRaw(d, kTypeUnits, unitsHead(1, 1, 1, 1).w((uint32_t)1).w(bad)), Err::kBadRecord, "owner -1 outside 0..63"));
		CHECK(refused(decRaw(d, kTypeUnits, unitsHead(1, 1, 1, 1).w((uint32_t)2).w(v).w(u)), Err::kBadRecord, "not above"));
		CHECK(refused(decRaw(d, kTypeUnits, unitsHead(1, 1, 1, 1).w((uint32_t)2).w(u).w(u)), Err::kBadRecord, "duplicate or out of order"));
		// diffs
		CHECK(refused(decRaw(d, kTypeUnits, unitsHead(1, 2, 1, 0).w((uint32_t)0).w((uint32_t)0)), Err::kDiffBeforeFull,
		              "a diff before any full table"));
		CHECK(refused(decRaw(d, kTypeUnits, unitsHead(1, 1, 1, 0).w((uint32_t)0).w((uint32_t)0)), Err::kBadRecord,
		              "caller 1 (first copy) stored as a diff"));
		CHECK(decRaw(d, kTypeUnits, unitsHead(2, 1, 1, 1).w((uint32_t)2).w(u).w(v)).ok());
		CHECK(refused(decRaw(d, kTypeUnits, unitsHead(3, 2, 2, 0).w((uint32_t)0).w((uint32_t)0)), Err::kDiffBeforeFull,
		              "a diff of tableGen 2 against a table of tableGen 1"));
		CHECK(refused(decRaw(d, kTypeUnits, unitsHead(3, 2, 1, 0)), Err::kTruncated, "no nUpsert"));
		CHECK(refused(decRaw(d, kTypeUnits, unitsHead(3, 2, 1, 0).w((uint32_t)1)), Err::kTruncated, "nUpsert 1 needs 32 bytes, 0 left"));
		CHECK(refused(decRaw(d, kTypeUnits, unitsHead(3, 2, 1, 0).w((uint32_t)0)), Err::kTruncated, "no nRemove"));
		CHECK(refused(decRaw(d, kTypeUnits, unitsHead(3, 2, 1, 0).w((uint32_t)0).w((uint32_t)1)), Err::kTruncated,
		              "nRemove 1 needs 8 bytes, 0 left"));
		CHECK(refused(decRaw(d, kTypeUnits, unitsHead(3, 2, 1, 0).w((uint32_t)0).w((uint32_t)1).w((int32_t)0).w((int32_t)1999)),
		              Err::kBadRecord, "key (0, 1999) not held"));
		CHECK(refused(decRaw(d, kTypeUnits, unitsHead(3, 2, 1, 0).w((uint32_t)0).w((uint32_t)1).w((int32_t)9).w((int32_t)1)),
		              Err::kBadRecord, "key (9, 1) not held"));
		CHECK(refused(decRaw(d, kTypeUnits, unitsHead(3, 2, 1, 0).w((uint32_t)0).w((uint32_t)1).w((int32_t)70).w((int32_t)1)),
		              Err::kBadRecord, "owner 70 outside 0..63"));
		CHECK(refused(decRaw(d, kTypeUnits,
		                     unitsHead(3, 2, 1, 0).w((uint32_t)0).w((uint32_t)2).w((int32_t)0).w((int32_t)1001).w((int32_t)0).w((int32_t)1001)),
		              Err::kBadRecord, "not above the previous (duplicate or out of order)"));
		CHECK(refused(decRaw(d, kTypeUnits, unitsHead(3, 2, 1, 0).w((uint32_t)1).w(u).w((uint32_t)1).w((int32_t)0).w((int32_t)1001)),
		              Err::kBadRecord, "both removed and upserted"));
		CHECK(refused(decRaw(d, kTypeUnits, unitsHead(3, 2, 1, 0).w((uint32_t)2).w(v).w(u).w((uint32_t)0)), Err::kBadRecord, "UNITS upsert"));
		CHECK(refused(decRaw(d, kTypeUnits, unitsHead(3, 2, 1, 0).w((uint32_t)0).w((uint32_t)0).w((uint16_t)0)), Err::kWrongSize,
		              "2 bytes after"));
		// the refusals left the table alone: a good diff applies to {u, v}
		UnitRecord v2 = v;
		v2.plot = 44;
		Decoded r;
		CHECK(decRaw(d, kTypeUnits, unitsHead(3, 2, 1, 0).w((uint32_t)1).w(v2).w((uint32_t)1).w((int32_t)0).w((int32_t)1001), &r).ok());
		CHECK(r.units.size() == 1 && r.units[0].id == 1002 && r.units[0].plot == 44);
	}
	// ---- PATHS ----
	{
		Decoder d(4);
		CHECK(refused(decRaw(d, kTypePaths, B().w((uint64_t)1)), Err::kTruncated, "no nPaths"));
		CHECK(refused(decRaw(d, kTypePaths, B().w((uint64_t)1).w((uint32_t)kMaxPaths + 1)), Err::kBadRecord, "nPaths 4194305 above"));
		CHECK(refused(decRaw(d, kTypePaths, B().w((uint64_t)1).w((uint32_t)1).w((uint32_t)5)), Err::kTruncated, "short of its seq and length"));
		CHECK(refused(decRaw(d, kTypePaths, B().w((uint64_t)1).w((uint32_t)1).w((uint32_t)5).w((uint32_t)65537)), Err::kBadRecord,
		              "length 65537 above 65536"));
		CHECK(refused(decRaw(d, kTypePaths, B().w((uint64_t)1).w((uint32_t)1).w((uint32_t)5).w((uint32_t)2).w((int32_t)3)), Err::kTruncated,
		              "length 2 needs 8 bytes, 4 left"));
		CHECK(refused(decRaw(d, kTypePaths, B().w((uint64_t)1).w((uint32_t)2).w((uint32_t)5).w((uint32_t)0).w((uint32_t)5).w((uint32_t)0)),
		              Err::kBadRecord, "seq 5 not above the previous path's 5"));
		CHECK(refused(decRaw(d, kTypePaths, B().w((uint64_t)1).w((uint32_t)1).w((uint32_t)5).w((uint32_t)0).w((uint8_t)1)), Err::kWrongSize,
		              "1 bytes after its 1 paths"));
	}
	// ---- COMBATS ----
	{
		Decoder d(4);
		CombatRecord c;
		memset(&c, 0, sizeof(c));
		c.seq = 9;
		c.kind = SrvFeed::kUnitCombat;
		CHECK(refused(decRaw(d, kTypeCombats, B().w((uint64_t)1)), Err::kTruncated, "no nCombats"));
		CHECK(refused(decRaw(d, kTypeCombats, B().w((uint64_t)1).w((uint32_t)kMaxCombats + 1)), Err::kBadRecord, "nCombats 65537 above"));
		CHECK(refused(decRaw(d, kTypeCombats, B().w((uint64_t)1).w((uint32_t)1).w((uint32_t)9)), Err::kTruncated, "short of its 64 bytes"));
		CombatRecord k = c;
		k.kind = 24;
		CHECK(refused(decRaw(d, kTypeCombats, B().w((uint64_t)1).w((uint32_t)1).w(k)), Err::kBadRecord, "kind 24 is not 25 or 28"));
		k = c;
		k.flags = 0x2000;
		CHECK(refused(decRaw(d, kTypeCombats, B().w((uint64_t)1).w((uint32_t)1).w(k)), Err::kBadRecord, "flags 0x2000 above bit 12"));
		k = c;
		k.flags = SrvFeed::kCombatInterceptorMember;   // issue 4726: read (a pre-4726 decoder refused it)
		CHECK(decRaw(d, kTypeCombats, B().w((uint64_t)1).w((uint32_t)1).w(k)).ok());
		k = c;
		k.reserved = 1;
		CHECK(refused(decRaw(d, kTypeCombats, B().w((uint64_t)1).w((uint32_t)1).w(k)), Err::kBadRecord, "reserved 1 is not 0"));
		// issue 4726: with kCombatDamageKnown (bit 11) reserved carries the per-combat damage words
		k = c;
		k.flags = SrvFeed::kCombatDamageKnown;
		k.reserved = SrvFeed::CombatDamageWords(7, 42);
		CHECK(decRaw(d, kTypeCombats, B().w((uint64_t)1).w((uint32_t)1).w(k)).ok());
		CHECK(SrvFeed::CombatDamage(k, 0) == 7 && SrvFeed::CombatDamage(k, 1) == 42);
		k.flags = 0;
		CHECK(SrvFeed::CombatDamage(k, 0) == -1);
		k = c;
		k.nMembers = 258;
		CHECK(refused(decRaw(d, kTypeCombats, B().w((uint64_t)1).w((uint32_t)1).w(k)), Err::kBadRecord, "nMembers 258 above 257"));
		k = c;
		k.firstMember = 3;
		CHECK(refused(decRaw(d, kTypeCombats, B().w((uint64_t)1).w((uint32_t)1).w(k)), Err::kBadRecord, "firstMember 3 is not the running count 0"));
		k = c;
		k.nMembers = 1;
		CHECK(refused(decRaw(d, kTypeCombats, B().w((uint64_t)1).w((uint32_t)1).w(k)), Err::kTruncated, "1 members need 28 bytes, 0 left"));
		CHECK(refused(decRaw(d, kTypeCombats, B().w((uint64_t)1).w((uint32_t)2).w(c).w(c)), Err::kBadRecord, "seq 9 not above the previous combat's 9"));
		CHECK(refused(decRaw(d, kTypeCombats, B().w((uint64_t)1).w((uint32_t)1).w(c).w((uint32_t)0)), Err::kWrongSize, "4 bytes after its 1 combats"));
		CHECK(decRaw(d, kTypeCombats, B().w((uint64_t)1).w((uint32_t)1).w(c)).ok());
	}
	// ---- PLAYERS ----
	{
		Decoder d(4);
		PlayerRecord p = P(3, 1), q = P(4, 2);
		CHECK(refused(decRaw(d, kTypePlayers, B().w((uint64_t)1).w((uint32_t)1)), Err::kTruncated, "short of its caller and count"));
		CHECK(refused(decRaw(d, kTypePlayers, B().w((uint64_t)1).w((uint32_t)0).w((uint32_t)0)), Err::kBadRecord, "caller 0 is not 1"));
		CHECK(refused(decRaw(d, kTypePlayers, B().w((uint64_t)1).w((uint32_t)1).w((uint32_t)65)), Err::kBadRecord, "count 65 above 64"));
		CHECK(refused(decRaw(d, kTypePlayers, B().w((uint64_t)1).w((uint32_t)1).w((uint32_t)2).w(p)), Err::kTruncated, "count 2 needs 64 bytes, 32 left"));
		PlayerRecord bad = p;
		bad.player = 64;
		CHECK(refused(decRaw(d, kTypePlayers, B().w((uint64_t)1).w((uint32_t)2).w((uint32_t)1).w(bad)), Err::kBadRecord, "player 64 outside 0..63"));
		bad.player = -1;
		CHECK(refused(decRaw(d, kTypePlayers, B().w((uint64_t)1).w((uint32_t)2).w((uint32_t)1).w(bad)), Err::kBadRecord, "player -1 outside 0..63"));
		CHECK(refused(decRaw(d, kTypePlayers, B().w((uint64_t)1).w((uint32_t)2).w((uint32_t)2).w(p).w(p)), Err::kBadRecord, "player 3 twice"));
		CHECK(refused(decRaw(d, kTypePlayers, B().w((uint64_t)1).w((uint32_t)1).w((uint32_t)2).w(q).w(p)), Err::kBadRecord, "a copy is sorted"));
		CHECK(decRaw(d, kTypePlayers, B().w((uint64_t)1).w((uint32_t)2).w((uint32_t)2).w(q).w(p)).ok());
		CHECK(refused(decRaw(d, kTypePlayers, B().w((uint64_t)1).w((uint32_t)2).w((uint32_t)1).w(p).w((uint8_t)7)), Err::kWrongSize, "1 bytes after its 1 rows"));
	}
	// ---- the encoder's refusals: nothing appended ----
	{
		Encoder e;
		std::vector<uint8_t> out;
		std::vector<UnitRecord> dup = {U(0, 1, 1, 1), U(0, 1, 2, 2)};
		CHECK(refused(e.addUnits(1, kUnitsFirstCopy, 1, dup, &out), Err::kEncode, "duplicate or out of order"));
		CHECK(refused(e.addUnits(1, 0, 1, std::vector<UnitRecord>(), &out), Err::kEncode, "caller 0"));
		std::vector<UnitRecord> own = {U(0, 1, 1, 1)};
		own[0].owner = 99;
		CHECK(refused(e.addUnits(1, kUnitsFirstCopy, 1, own, &out), Err::kEncode, "owner 99"));
		SrvFeed::Batch b;
		b.paths = {{1, 0, 4}};
		b.pathPlots = {1, 2};
		CHECK(refused(e.addPaths(1, b, &out), Err::kEncode, "outside the batch's 2 plots"));
		b.paths = {{2, 0, 1}, {1, 1, 1}};
		CHECK(refused(e.addPaths(1, b, &out), Err::kEncode, "seq 1 not above the previous path's 2"));
		SrvFeed::Batch cb;
		CombatRecord c;
		memset(&c, 0, sizeof(c));
		c.kind = SrvFeed::kUnitCombat;
		c.nMembers = 2;
		cb.combats = {c};
		CHECK(refused(e.addCombats(1, cb, &out), Err::kEncode, "outside the batch's 0 members"));
		cb.combats[0].nMembers = 0;
		cb.combats[0].kind = 3;
		CHECK(refused(e.addCombats(1, cb, &out), Err::kEncode, "kind 3 is not 25 or 28"));
		CHECK(refused(e.addPlayers(1, 3, std::vector<PlayerRecord>(), &out), Err::kEncode, "caller 3"));
		std::vector<PlayerRecord> unsorted = {P(5, 1), P(2, 1)};
		CHECK(refused(e.addPlayers(1, kPlayersCopy, unsorted, &out), Err::kEncode, "a copy is sorted"));
		CHECK(out.empty());
	}
}

static int decodeFile(const char* path, uint32_t version)
{
	FILE* f = fopen(path, "rb");
	if(!f)
	{
		printf("records: cannot open %s\n", path);
		return 2;
	}
	std::vector<uint8_t> b;
	uint8_t buf[65536];
	size_t n;
	while((n = fread(buf, 1, sizeof(buf), f)) > 0)
		b.insert(b.end(), buf, buf + n);
	fclose(f);
	Decoder d(version);
	size_t at = 0, k = 0;
	while(at < b.size())
	{
		if(b.size() - at < 8)
		{
			printf("records: REFUSED Truncated record %zu: %zu bytes, short of a record head\n", k, b.size() - at);
			return 10 + (int)Err::kTruncated;
		}
		uint32_t type = 0, bytes = 0;
		memcpy(&type, &b[at], 4);
		memcpy(&bytes, &b[at + 4], 4);
		at += 8;
		if(b.size() - at < bytes)
		{
			printf("records: REFUSED Truncated record %zu: %u payload bytes, %zu left\n", k, bytes, b.size() - at);
			return 10 + (int)Err::kTruncated;
		}
		Result r = d.decode(type, &b[at], bytes, nullptr);
		if(!r.ok())
		{
			printf("records: REFUSED %s record %zu: %s\n", ErrName(r.err), k, r.why.c_str());
			return 10 + (int)r.err;
		}
		at += bytes;
		++k;
	}
	printf("records: OK %zu records\n", k);
	return 0;
}

int main(int argc, char** argv)
{
	if(argc > 1)
	{
		if(strcmp(argv[1], "--decode") == 0 && (argc == 3 || (argc == 5 && strcmp(argv[3], "--format") == 0)))
			return decodeFile(argv[2], argc == 5 ? (uint32_t)strtoul(argv[4], nullptr, 10) : 4u);
		printf("usage: test_units_records.exe [--decode FILE [--format V]]\n");
		return 2;
	}
	static_assert(sizeof(UnitRecord) == 32 && sizeof(PlayerRecord) == 32 && sizeof(CombatRecord) == 64 && sizeof(CombatMember) == 28,
	              "the record layouts of srv_feed.h");
	testUnitsRoundTrip();
	testPathsCombatsPlayers();
	testRefusals();
	CHECK(strcmp(ErrName(Err::kDiffBeforeFull), "UnitsDiffBeforeFull") == 0 && strcmp(TypeName(kTypeCombats), "COMBATS") == 0);
#ifdef RECORDS_TEST_INJECT_FAIL
	CHECK(!"FAIL injected");
	printf("FAIL injected\n");
#endif
	printf("test_units_records: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
