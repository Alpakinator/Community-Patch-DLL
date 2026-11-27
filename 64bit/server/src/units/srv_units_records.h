// srv_units_records.h - the unit feed's records of feed.bin (epic 0375, issue 4457; decision 11 of
// research/05-graphics-composition/unit-server-scene.md, the layouts as built in its section 11). Written 2026-11-16 for
// this repository (GPL-3.0-or-later).
//
// Four record types of the scene bundle's feed.bin (research/06-tooling-and-workflows/offline-scene-harness.md 2.5: each
// record is `u32 type, u32 payload bytes, payload`, little-endian, packed, no padding), from bundle format version 4:
//   3 UNITS    a unit table the scene copied (SrvFeed::CopyUnits): u64 loopIter, u32 caller (1 the scene's first copy,
//              2 a later copy), u32 tableGen, u32 full; full 1: u32 count, UnitRecord[count] (32 B, strictly increasing
//              (owner, id)); full 0: u32 nUpsert, UnitRecord[nUpsert], u32 nRemove, {i32 owner, i32 id}[nRemove] (each
//              strictly increasing) against the previous UNITS record, same tableGen.
//   4 PATHS    a drained batch's kUnitMoved paths (Batch::paths, pathPlots): u64 loopIter, u32 nPaths, then per path
//              u32 seq, u32 length, i32 plot[length]. PathEntry::first is the running sum (as the feed appends them).
//   5 COMBATS  a drained batch's combats (Batch::combats, combatMembers): u64 loopIter, u32 nCombats, then per combat the
//              64 B SrvFeed::CombatRecord (firstMember = the running sum of nMembers, reserved 0) followed by its
//              nMembers x 28 B SrvFeed::CombatMember.
//   6 PLAYERS  player rows: u64 loopIter, u32 caller (1 the whole table the scene copied, SrvFeed::CopyPlayers; 2 the
//              rows a drained batch carried, Batch::players), u32 count, PlayerRecord[count] (32 B as srv_feed.h).
// Nothing here throws, and nothing depends on the bundle library: the bundle (srv_scene_bundle.cpp) frames, orders and
// cross-checks them, the codecs here own the payloads.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>

#include "../feed/srv_feed.h"

namespace srvunits {
namespace rec {

enum : uint32_t
{
	kTypeUnits = 3,
	kTypePaths = 4,
	kTypeCombats = 5,
	kTypePlayers = 6,
};
// The bundle format version that introduced these records (scenebundle::kFormatVersion 4).
constexpr uint32_t kFirstFormatVersion = 4;

enum : uint32_t { kUnitsFirstCopy = 1, kUnitsLaterCopy = 2 };     // UNITS caller
enum : uint32_t { kPlayersCopy = 1, kPlayersBatch = 2 };          // PLAYERS caller

// Limits checked by the decoders (each above the feed's own caps, srv_feed.cpp / unit-server-scene.md 2.3).
constexpr uint32_t kMaxUnits = 64u * 65536u;     // 64 owners x the DLL's unit limit
constexpr uint32_t kMaxPathLength = 65536;       // PathEntry::length cap
constexpr uint32_t kMaxPaths = 4194304;          // side table plots between two drains
constexpr uint32_t kMaxCombats = 65536;
constexpr uint32_t kMaxMembers = 257;            // per combat (issue 4726: 256 damage members and the interceptor's)
constexpr uint32_t kMaxPlayers = 64;
constexpr uint16_t kCombatFlagsKnown = 0x1FFF;   // CombatFlagBits 0..12 (issue 4726: bit 11 kCombatDamageKnown, bit 12
                                                 // kCombatInterceptorMember; a pre-4726 decoder refuses both by name)

enum class Err : uint8_t
{
	kOk = 0,
	kTruncated,       // the payload ends inside a field
	kWrongSize,       // the payload's size differs from what its counts announce (bytes left over)
	kBadRecord,       // a value out of range or out of order (named in why)
	kDiffBeforeFull,  // a UNITS diff before any full table (or across a tableGen change)
	kLoopBack,        // a loopIter below the previous record's
	kOlderFormat,     // a record type 3-6 in a bundle of format version below 4
	kUnknownType,     // a type that is not 3-6
	kEncode,          // the encoder's input is inconsistent (named in why); nothing appended
};
const char* ErrName(Err e);

struct Result
{
	Err err = Err::kOk;
	std::string why;
	bool ok() const { return err == Err::kOk; }
	// "<ErrName>: <why>" ("Ok" when ok)
	std::string line() const;
};

// A decoded record. Only the members of its type are set.
struct Decoded
{
	uint32_t type = 0;
	uint64_t loopIter = 0;
	uint32_t caller = 0;                               // UNITS, PLAYERS
	// UNITS
	uint32_t tableGen = 0;
	bool full = false;                                 // as stored
	uint32_t nUpsert = 0, nRemove = 0;                 // a diff's counts (0 for a full table)
	std::vector<SrvFeed::UnitRecord> units;            // the whole table after this record (diff applied), sorted
	// PATHS
	std::vector<SrvFeed::PathEntry> paths;
	std::vector<int32_t> pathPlots;
	// COMBATS
	std::vector<SrvFeed::CombatRecord> combats;
	std::vector<SrvFeed::CombatMember> combatMembers;
	// PLAYERS
	std::vector<SrvFeed::PlayerRecord> players;
};

// The decoder of one feed.bin's records 3-6 in file order. It keeps the previous UNITS table (for diffs) and the last
// loopIter it saw; the caller tells it of every other record's loopIter (noteLoopIter) so that "never goes back" holds
// across all types. formatVersion = the bundle's format_version (records 3-6 need 4 or more).
class Decoder
{
public:
	explicit Decoder(uint32_t formatVersion) : m_formatVersion(formatVersion) {}
	// One record's payload (without the 8-byte type and size). On a refusal `out` is unspecified and the decoder's state
	// unchanged.
	Result decode(uint32_t type, const uint8_t* payload, size_t bytes, Decoded* out);
	// A record of another type (DRAIN, PLOTS) at loopIter: kLoopBack when it goes back.
	Result noteLoopIter(uint64_t loopIter, const char* what);
private:
	Result loopCheck(uint64_t loopIter, const char* what);
	uint32_t m_formatVersion;
	uint64_t m_lastLoop = 0;
	bool m_haveLoop = false;
	std::vector<SrvFeed::UnitRecord> m_prevUnits;
	uint32_t m_prevGen = 0;
	bool m_haveUnits = false;
};

// The encoder (the recorder's side): each call appends one framed record (u32 type, u32 bytes, payload) to *out and
// returns ok, or appends nothing and returns kEncode naming the inconsistency. addUnits writes a full table the first
// time, when tableGen changes, when the caller is kUnitsFirstCopy, and when a diff would not be smaller; else a diff
// against the previous UNITS record. The table may come in any order; it is written sorted by (owner, id).
class Encoder
{
public:
	Result addUnits(uint64_t loopIter, uint32_t caller, uint32_t tableGen, const std::vector<SrvFeed::UnitRecord>& table,
	                std::vector<uint8_t>* out, bool forceFull = false);
	// Only when the batch has paths (an empty PATHS record is legal but never written by the recorder).
	Result addPaths(uint64_t loopIter, const SrvFeed::Batch& b, std::vector<uint8_t>* out);
	Result addCombats(uint64_t loopIter, const SrvFeed::Batch& b, std::vector<uint8_t>* out);
	Result addPlayers(uint64_t loopIter, uint32_t caller, const std::vector<SrvFeed::PlayerRecord>& rows,
	                  std::vector<uint8_t>* out);
private:
	std::vector<SrvFeed::UnitRecord> m_prev;
	uint32_t m_gen = 0;
	bool m_have = false;
};

// (owner, id) order of the unit table (CopyUnits' order).
bool UnitKeyLess(const SrvFeed::UnitRecord& a, const SrvFeed::UnitRecord& b);
const char* TypeName(uint32_t type);   // "UNITS", "PATHS", "COMBATS", "PLAYERS", else "?"

}  // namespace rec
}  // namespace srvunits

// ---- implementation, inline (issue 4457): the bundle library (server/src/render/srv_scene_bundle.cpp) decodes these
// records, and every build that compiles server/src/render/*.cpp without server/src/units/ (the render host, fog, overlay,
// shadow, water, decal, ice tests, sceneshot) must link without a new source; srv_units_records.cpp holds no definition.
namespace srvunits {
namespace rec {

namespace detail {

struct Reader
{
	const uint8_t* p;
	size_t n, at;
	bool take(void* dst, size_t k)
	{
		if(n - at < k)
			return false;
		memcpy(dst, p + at, k);
		at += k;
		return true;
	}
	size_t left() const { return n - at; }
};

inline Result fail(Err e, const char* fmt, ...)
{
	Result r;
	r.err = e;
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	r.why = buf;
	return r;
}

template<class T> void put(std::vector<uint8_t>& o, const T& v)
{
	const uint8_t* b = reinterpret_cast<const uint8_t*>(&v);
	o.insert(o.end(), b, b + sizeof(T));
}
inline void putBytes(std::vector<uint8_t>& o, const void* d, size_t k)
{
	const uint8_t* b = static_cast<const uint8_t*>(d);
	o.insert(o.end(), b, b + k);
}

// Frames a payload as `u32 type, u32 bytes, payload` and appends it.
inline void frame(uint32_t type, const std::vector<uint8_t>& payload, std::vector<uint8_t>* out)
{
	put(*out, type);
	put(*out, (uint32_t)payload.size());
	putBytes(*out, payload.data(), payload.size());
}

inline bool keyEq(const SrvFeed::UnitRecord& a, const SrvFeed::UnitRecord& b) { return a.owner == b.owner && a.id == b.id; }
inline bool sameRecord(const SrvFeed::UnitRecord& a, const SrvFeed::UnitRecord& b) { return memcmp(&a, &b, sizeof(a)) == 0; }

// Strictly increasing keys, owners 0..63.
inline Result checkUnitTable(const std::vector<SrvFeed::UnitRecord>& t, const char* what)
{
	for(size_t i = 0; i < t.size(); ++i)
	{
		if(t[i].owner < 0 || t[i].owner >= 64)
			return fail(Err::kBadRecord, "%s record %zu: owner %d outside 0..63", what, i, (int)t[i].owner);
		if(i && !UnitKeyLess(t[i - 1], t[i]))
			return fail(Err::kBadRecord, "%s record %zu: key (%d, %d) not above (%d, %d) (duplicate or out of order)", what, i,
			            (int)t[i].owner, t[i].id, (int)t[i - 1].owner, t[i - 1].id);
	}
	return Result();
}

// The encoder never writes what the decoder refuses: a framed record of type 4-6 is decoded once by a fresh decoder.
inline Result selfCheck(const std::vector<uint8_t>& framed)
{
	Decoder d(kFirstFormatVersion);
	uint32_t type = 0;
	memcpy(&type, framed.data(), 4);
	Result r = d.decode(type, framed.data() + 8, framed.size() - 8, nullptr);
	if(!r.ok())
		r.err = Err::kEncode;
	return r;
}

}  // namespace detail
using namespace detail;

inline const char* ErrName(Err e)
{
	switch(e)
	{
	case Err::kOk: return "Ok";
	case Err::kTruncated: return "Truncated";
	case Err::kWrongSize: return "WrongSize";
	case Err::kBadRecord: return "BadRecord";
	case Err::kDiffBeforeFull: return "UnitsDiffBeforeFull";
	case Err::kLoopBack: return "LoopBack";
	case Err::kOlderFormat: return "OlderFormat";
	case Err::kUnknownType: return "UnknownType";
	case Err::kEncode: return "Encode";
	}
	return "?";
}

inline std::string Result::line() const { return ok() ? std::string("Ok") : std::string(ErrName(err)) + ": " + why; }

inline bool UnitKeyLess(const SrvFeed::UnitRecord& a, const SrvFeed::UnitRecord& b)
{
	return a.owner != b.owner ? a.owner < b.owner : a.id < b.id;
}

inline const char* TypeName(uint32_t type)
{
	switch(type)
	{
	case kTypeUnits: return "UNITS";
	case kTypePaths: return "PATHS";
	case kTypeCombats: return "COMBATS";
	case kTypePlayers: return "PLAYERS";
	}
	return "?";
}

// ---- decoder ---------------------------------------------------------------------------------------------------------
inline Result Decoder::loopCheck(uint64_t loopIter, const char* what)
{
	if(m_haveLoop && loopIter < m_lastLoop)
		return fail(Err::kLoopBack, "%s loopIter %llu below the previous record's %llu", what, (unsigned long long)loopIter,
		            (unsigned long long)m_lastLoop);
	return Result();
}

inline Result Decoder::noteLoopIter(uint64_t loopIter, const char* what)
{
	Result r = loopCheck(loopIter, what);
	if(!r.ok())
		return r;
	m_lastLoop = loopIter;
	m_haveLoop = true;
	return r;
}

inline Result Decoder::decode(uint32_t type, const uint8_t* payload, size_t bytes, Decoded* out)
{
	if(type < kTypeUnits || type > kTypePlayers)
		return fail(Err::kUnknownType, "record type %u is not a unit record (3 UNITS, 4 PATHS, 5 COMBATS, 6 PLAYERS)", type);
	const char* name = TypeName(type);
	if(m_formatVersion < kFirstFormatVersion)
		return fail(Err::kOlderFormat, "record type %u %s needs format_version %u; the bundle has %u", type, name,
		            kFirstFormatVersion, m_formatVersion);
	Reader r = {payload, payload ? bytes : 0, 0};
	Decoded d;
	d.type = type;
	if(!r.take(&d.loopIter, 8))
		return fail(Err::kTruncated, "%s: %zu bytes, no loopIter", name, bytes);
	Result lc = loopCheck(d.loopIter, name);
	if(!lc.ok())
		return lc;
	// what a successful UNITS decode leaves as the previous table
	std::vector<SrvFeed::UnitRecord> table;
	switch(type)
	{
	case kTypeUnits:
	{
		uint32_t full = 0;
		if(!r.take(&d.caller, 4) || !r.take(&d.tableGen, 4) || !r.take(&full, 4))
			return fail(Err::kTruncated, "UNITS: %zu bytes, short of its 20 fixed bytes", bytes);
		if(d.caller != kUnitsFirstCopy && d.caller != kUnitsLaterCopy)
			return fail(Err::kBadRecord, "UNITS: caller %u is not 1 (first copy) or 2 (later copy)", d.caller);
		if(full > 1)
			return fail(Err::kBadRecord, "UNITS: full %u is not 0 or 1", full);
		d.full = full == 1;
		if(d.full)
		{
			uint32_t count = 0;
			if(!r.take(&count, 4))
				return fail(Err::kTruncated, "UNITS: no count");
			if(count > kMaxUnits)
				return fail(Err::kBadRecord, "UNITS: count %u above %u", count, kMaxUnits);
			if(r.left() < (size_t)count * sizeof(SrvFeed::UnitRecord))
				return fail(Err::kTruncated, "UNITS: count %u needs %zu bytes, %zu left", count,
				            (size_t)count * sizeof(SrvFeed::UnitRecord), r.left());
			table.resize(count);
			if(count)
				r.take(table.data(), (size_t)count * sizeof(SrvFeed::UnitRecord));
			Result c = checkUnitTable(table, "UNITS");
			if(!c.ok())
				return c;
		}
		else
		{
			if(d.caller == kUnitsFirstCopy)
				return fail(Err::kBadRecord, "UNITS: caller 1 (first copy) stored as a diff");
			if(!m_haveUnits)
				return fail(Err::kDiffBeforeFull, "UNITS: a diff before any full table");
			if(d.tableGen != m_prevGen)
				return fail(Err::kDiffBeforeFull, "UNITS: a diff of tableGen %u against a table of tableGen %u", d.tableGen, m_prevGen);
			if(!r.take(&d.nUpsert, 4))
				return fail(Err::kTruncated, "UNITS: no nUpsert");
			if(d.nUpsert > kMaxUnits)
				return fail(Err::kBadRecord, "UNITS: nUpsert %u above %u", d.nUpsert, kMaxUnits);
			if(r.left() < (size_t)d.nUpsert * sizeof(SrvFeed::UnitRecord))
				return fail(Err::kTruncated, "UNITS: nUpsert %u needs %zu bytes, %zu left", d.nUpsert,
				            (size_t)d.nUpsert * sizeof(SrvFeed::UnitRecord), r.left());
			std::vector<SrvFeed::UnitRecord> ups(d.nUpsert);
			if(d.nUpsert)
				r.take(ups.data(), (size_t)d.nUpsert * sizeof(SrvFeed::UnitRecord));
			Result c = checkUnitTable(ups, "UNITS upsert");
			if(!c.ok())
				return c;
			if(!r.take(&d.nRemove, 4))
				return fail(Err::kTruncated, "UNITS: no nRemove");
			if(d.nRemove > kMaxUnits)
				return fail(Err::kBadRecord, "UNITS: nRemove %u above %u", d.nRemove, kMaxUnits);
			if(r.left() < (size_t)d.nRemove * 8)
				return fail(Err::kTruncated, "UNITS: nRemove %u needs %zu bytes, %zu left", d.nRemove, (size_t)d.nRemove * 8, r.left());
			std::vector<SrvFeed::UnitRecord> rem(d.nRemove);
			for(uint32_t i = 0; i < d.nRemove; ++i)
			{
				int32_t owner = 0, id = 0;
				r.take(&owner, 4);
				r.take(&id, 4);
				if(owner < 0 || owner >= 64)
					return fail(Err::kBadRecord, "UNITS remove %u: owner %d outside 0..63", i, owner);
				memset(&rem[i], 0, sizeof(rem[i]));
				rem[i].owner = (int8_t)owner;
				rem[i].id = id;
				if(i && !UnitKeyLess(rem[i - 1], rem[i]))
					return fail(Err::kBadRecord, "UNITS remove %u: key (%d, %d) not above the previous (duplicate or out of order)", i,
					            owner, id);
			}
			// merge: previous table minus removals, then upserts (an upsert of a removed key is refused: a diff never
			// carries both for one key)
			size_t ri = 0, ui = 0;
			for(const SrvFeed::UnitRecord& p : m_prevUnits)
			{
				if(ri < rem.size() && UnitKeyLess(rem[ri], p))
					return fail(Err::kBadRecord, "UNITS remove: key (%d, %d) not held by the previous table", (int)rem[ri].owner,
					            rem[ri].id);
				while(ui < ups.size() && UnitKeyLess(ups[ui], p))
					table.push_back(ups[ui++]);
				const bool removed = ri < rem.size() && keyEq(rem[ri], p);
				const bool upserted = ui < ups.size() && keyEq(ups[ui], p);
				if(removed && upserted)
					return fail(Err::kBadRecord, "UNITS: key (%d, %d) both removed and upserted", (int)p.owner, p.id);
				if(removed)
					++ri;
				else if(upserted)
					table.push_back(ups[ui++]);
				else
					table.push_back(p);
			}
			if(ri < rem.size())
				return fail(Err::kBadRecord, "UNITS remove: key (%d, %d) not held by the previous table", (int)rem[ri].owner, rem[ri].id);
			while(ui < ups.size())
				table.push_back(ups[ui++]);
		}
		if(r.left())
			return fail(Err::kWrongSize, "UNITS: %zu bytes after the records its counts announce", r.left());
		d.units = table;
		break;
	}
	case kTypePaths:
	{
		uint32_t nPaths = 0;
		if(!r.take(&nPaths, 4))
			return fail(Err::kTruncated, "PATHS: no nPaths");
		if(nPaths > kMaxPaths)
			return fail(Err::kBadRecord, "PATHS: nPaths %u above %u", nPaths, kMaxPaths);
		d.paths.reserve(nPaths);
		for(uint32_t i = 0; i < nPaths; ++i)
		{
			SrvFeed::PathEntry pe;
			if(!r.take(&pe.seq, 4) || !r.take(&pe.length, 4))
				return fail(Err::kTruncated, "PATHS path %u of %u: short of its seq and length", i, nPaths);
			if(pe.length > kMaxPathLength)
				return fail(Err::kBadRecord, "PATHS path %u: length %u above %u", i, pe.length, kMaxPathLength);
			if(i && pe.seq <= d.paths.back().seq)
				return fail(Err::kBadRecord, "PATHS path %u: seq %u not above the previous path's %u", i, pe.seq, d.paths.back().seq);
			if(r.left() < (size_t)pe.length * 4)
				return fail(Err::kTruncated, "PATHS path %u: length %u needs %zu bytes, %zu left", i, pe.length, (size_t)pe.length * 4,
				            r.left());
			if(d.pathPlots.size() + pe.length > kMaxPaths)
				return fail(Err::kBadRecord, "PATHS: more than %u plots", kMaxPaths);
			pe.first = (uint32_t)d.pathPlots.size();
			const size_t at = d.pathPlots.size();
			d.pathPlots.resize(at + pe.length);
			if(pe.length)
				r.take(&d.pathPlots[at], (size_t)pe.length * 4);
			d.paths.push_back(pe);
		}
		if(r.left())
			return fail(Err::kWrongSize, "PATHS: %zu bytes after its %u paths", r.left(), nPaths);
		break;
	}
	case kTypeCombats:
	{
		uint32_t nCombats = 0;
		if(!r.take(&nCombats, 4))
			return fail(Err::kTruncated, "COMBATS: no nCombats");
		if(nCombats > kMaxCombats)
			return fail(Err::kBadRecord, "COMBATS: nCombats %u above %u", nCombats, kMaxCombats);
		d.combats.reserve(nCombats);
		for(uint32_t i = 0; i < nCombats; ++i)
		{
			SrvFeed::CombatRecord c;
			if(!r.take(&c, sizeof(c)))
				return fail(Err::kTruncated, "COMBATS combat %u of %u: short of its 64 bytes", i, nCombats);
			if(c.kind != SrvFeed::kUnitCombat && c.kind != SrvFeed::kCityCombat)
				return fail(Err::kBadRecord, "COMBATS combat %u: kind %u is not 25 or 28", i, (unsigned)c.kind);
			if(c.flags & ~kCombatFlagsKnown)
				return fail(Err::kBadRecord, "COMBATS combat %u: flags 0x%x above bit 12", i, (unsigned)c.flags);
			// issue 4726: reserved carries the per-combat damage words with kCombatDamageKnown, else it is 0
			if(c.reserved != 0 && !(c.flags & SrvFeed::kCombatDamageKnown))
				return fail(Err::kBadRecord, "COMBATS combat %u: reserved %u is not 0", i, c.reserved);
			if(c.nMembers > kMaxMembers)
				return fail(Err::kBadRecord, "COMBATS combat %u: nMembers %u above %u", i, c.nMembers, kMaxMembers);
			if(c.firstMember != (uint32_t)d.combatMembers.size())
				return fail(Err::kBadRecord, "COMBATS combat %u: firstMember %u is not the running count %zu", i, c.firstMember,
				            d.combatMembers.size());
			if(i && c.seq <= d.combats.back().seq)
				return fail(Err::kBadRecord, "COMBATS combat %u: seq %u not above the previous combat's %u", i, c.seq, d.combats.back().seq);
			if(r.left() < (size_t)c.nMembers * sizeof(SrvFeed::CombatMember))
				return fail(Err::kTruncated, "COMBATS combat %u: %u members need %zu bytes, %zu left", i, c.nMembers,
				            (size_t)c.nMembers * sizeof(SrvFeed::CombatMember), r.left());
			const size_t at = d.combatMembers.size();
			d.combatMembers.resize(at + c.nMembers);
			if(c.nMembers)
				r.take(&d.combatMembers[at], (size_t)c.nMembers * sizeof(SrvFeed::CombatMember));
			d.combats.push_back(c);
		}
		if(r.left())
			return fail(Err::kWrongSize, "COMBATS: %zu bytes after its %u combats", r.left(), nCombats);
		break;
	}
	case kTypePlayers:
	{
		uint32_t count = 0;
		if(!r.take(&d.caller, 4) || !r.take(&count, 4))
			return fail(Err::kTruncated, "PLAYERS: short of its caller and count");
		if(d.caller != kPlayersCopy && d.caller != kPlayersBatch)
			return fail(Err::kBadRecord, "PLAYERS: caller %u is not 1 (copy) or 2 (batch rows)", d.caller);
		if(count > kMaxPlayers)
			return fail(Err::kBadRecord, "PLAYERS: count %u above %u", count, kMaxPlayers);
		if(r.left() < (size_t)count * sizeof(SrvFeed::PlayerRecord))
			return fail(Err::kTruncated, "PLAYERS: count %u needs %zu bytes, %zu left", count, (size_t)count * sizeof(SrvFeed::PlayerRecord),
			            r.left());
		d.players.resize(count);
		if(count)
			r.take(d.players.data(), (size_t)count * sizeof(SrvFeed::PlayerRecord));
		uint64_t seen = 0;
		for(uint32_t i = 0; i < count; ++i)
		{
			const int p = d.players[i].player;
			if(p < 0 || p >= 64)
				return fail(Err::kBadRecord, "PLAYERS row %u: player %d outside 0..63", i, p);
			if(seen & (1ull << p))
				return fail(Err::kBadRecord, "PLAYERS row %u: player %d twice", i, p);
			seen |= 1ull << p;
			if(d.caller == kPlayersCopy && i && d.players[i - 1].player > p)
				return fail(Err::kBadRecord, "PLAYERS row %u: player %d below the previous row's (a copy is sorted)", i, p);
		}
		if(r.left())
			return fail(Err::kWrongSize, "PLAYERS: %zu bytes after its %u rows", r.left(), count);
		break;
	}
	}
	// accepted: commit the state
	m_lastLoop = d.loopIter;
	m_haveLoop = true;
	if(type == kTypeUnits)
	{
		m_prevUnits = d.units;
		m_prevGen = d.tableGen;
		m_haveUnits = true;
	}
	if(out)
		*out = std::move(d);
	return Result();
}

// ---- encoder ---------------------------------------------------------------------------------------------------------
inline Result Encoder::addUnits(uint64_t loopIter, uint32_t caller, uint32_t tableGen, const std::vector<SrvFeed::UnitRecord>& table,
                         std::vector<uint8_t>* out, bool forceFull)
{
	if(caller != kUnitsFirstCopy && caller != kUnitsLaterCopy)
		return fail(Err::kEncode, "UNITS: caller %u is not 1 or 2", caller);
	std::vector<SrvFeed::UnitRecord> t(table);
	std::stable_sort(t.begin(), t.end(), UnitKeyLess);
	Result c = checkUnitTable(t, "UNITS (encoder)");
	if(!c.ok())
		return fail(Err::kEncode, "%s", c.why.c_str());
	std::vector<uint8_t> full;
	put(full, loopIter);
	put(full, caller);
	put(full, tableGen);
	put(full, (uint32_t)1);
	put(full, (uint32_t)t.size());
	if(!t.empty())
		putBytes(full, t.data(), t.size() * sizeof(SrvFeed::UnitRecord));
	const std::vector<uint8_t>* chosen = &full;
	std::vector<uint8_t> diff;
	if(!forceFull && m_have && tableGen == m_gen && caller == kUnitsLaterCopy)
	{
		std::vector<SrvFeed::UnitRecord> ups, rem;
		size_t i = 0, j = 0;
		while(i < m_prev.size() || j < t.size())
		{
			if(j >= t.size() || (i < m_prev.size() && UnitKeyLess(m_prev[i], t[j])))
				rem.push_back(m_prev[i++]);
			else if(i >= m_prev.size() || UnitKeyLess(t[j], m_prev[i]))
				ups.push_back(t[j++]);
			else
			{
				if(!sameRecord(m_prev[i], t[j]))
					ups.push_back(t[j]);
				++i;
				++j;
			}
		}
		put(diff, loopIter);
		put(diff, caller);
		put(diff, tableGen);
		put(diff, (uint32_t)0);
		put(diff, (uint32_t)ups.size());
		if(!ups.empty())
			putBytes(diff, ups.data(), ups.size() * sizeof(SrvFeed::UnitRecord));
		put(diff, (uint32_t)rem.size());
		for(const SrvFeed::UnitRecord& u : rem)
		{
			put(diff, (int32_t)u.owner);
			put(diff, u.id);
		}
		if(diff.size() < full.size())
			chosen = &diff;
	}
	frame(kTypeUnits, *chosen, out);
	m_prev.swap(t);
	m_gen = tableGen;
	m_have = true;
	return Result();
}

inline Result Encoder::addPaths(uint64_t loopIter, const SrvFeed::Batch& b, std::vector<uint8_t>* out)
{
	std::vector<uint8_t> p;
	put(p, loopIter);
	put(p, (uint32_t)b.paths.size());
	for(size_t i = 0; i < b.paths.size(); ++i)
	{
		const SrvFeed::PathEntry& pe = b.paths[i];
		if((uint64_t)pe.first + pe.length > b.pathPlots.size())
			return fail(Err::kEncode, "PATHS: path %zu (first %u, length %u) outside the batch's %zu plots", i, pe.first, pe.length,
			            b.pathPlots.size());
		if(pe.length > kMaxPathLength)
			return fail(Err::kEncode, "PATHS: path %zu length %u above %u", i, pe.length, kMaxPathLength);
		put(p, pe.seq);
		put(p, pe.length);
		if(pe.length)
			putBytes(p, &b.pathPlots[pe.first], (size_t)pe.length * 4);
	}
	std::vector<uint8_t> rec;
	frame(kTypePaths, p, &rec);
	Result c = selfCheck(rec);
	if(!c.ok())
		return c;
	out->insert(out->end(), rec.begin(), rec.end());
	return Result();
}

inline Result Encoder::addCombats(uint64_t loopIter, const SrvFeed::Batch& b, std::vector<uint8_t>* out)
{
	std::vector<uint8_t> p;
	put(p, loopIter);
	put(p, (uint32_t)b.combats.size());
	uint32_t running = 0;
	for(size_t i = 0; i < b.combats.size(); ++i)
	{
		SrvFeed::CombatRecord c = b.combats[i];
		if((uint64_t)c.firstMember + c.nMembers > b.combatMembers.size())
			return fail(Err::kEncode, "COMBATS: combat %zu (firstMember %u, nMembers %u) outside the batch's %zu members", i,
			            c.firstMember, c.nMembers, b.combatMembers.size());
		const uint32_t first = c.firstMember;
		c.firstMember = running;   // the file's index: members follow their combat
		put(p, c);
		if(c.nMembers)
			putBytes(p, &b.combatMembers[first], (size_t)c.nMembers * sizeof(SrvFeed::CombatMember));
		running += c.nMembers;
	}
	std::vector<uint8_t> rec;
	frame(kTypeCombats, p, &rec);
	Result c = selfCheck(rec);
	if(!c.ok())
		return c;
	out->insert(out->end(), rec.begin(), rec.end());
	return Result();
}

inline Result Encoder::addPlayers(uint64_t loopIter, uint32_t caller, const std::vector<SrvFeed::PlayerRecord>& rows,
                           std::vector<uint8_t>* out)
{
	if(caller != kPlayersCopy && caller != kPlayersBatch)
		return fail(Err::kEncode, "PLAYERS: caller %u is not 1 or 2", caller);
	if(rows.size() > kMaxPlayers)
		return fail(Err::kEncode, "PLAYERS: %zu rows above %u", rows.size(), kMaxPlayers);
	std::vector<uint8_t> p;
	put(p, loopIter);
	put(p, caller);
	put(p, (uint32_t)rows.size());
	if(!rows.empty())
		putBytes(p, rows.data(), rows.size() * sizeof(SrvFeed::PlayerRecord));
	std::vector<uint8_t> rec;
	frame(kTypePlayers, p, &rec);
	Result c = selfCheck(rec);
	if(!c.ok())
		return c;
	out->insert(out->end(), rec.begin(), rec.end());
	return Result();
}

}  // namespace rec
}  // namespace srvunits
