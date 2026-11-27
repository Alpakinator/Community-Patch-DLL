// vp_dbfp.h - the game database on both sides of the proxy (issue 2504). Hand-written, header only.
// Copyright (C) 2026 the c5-64bit-patch authors. GPL-3.0-or-later.
//
// Shared by the x86 proxy (clang-cl -m32, VC9 headers, the EXE's closed CvGameDatabase library, C++11 at most) and the
// x64 server (CvGameDatabase_x64.dll, MSVC 14 STL). Only exported members of Database::Connection / Results are used,
// so the same code runs against either library. Design: research/03-dll-engine-boundary/proxy-design.md 11.7.
//
// 1. VpDbFingerprint: what "identical" means for the engine's database and the server's copy.
//      schema  FNV-1a 64 over every row of sqlite_master in rowid order (type, name, tbl_name, sql; NULL marked);
//      tables  for every table of sqlite_master (type 'table', by name): the row count and an FNV-1a 64 over every
//              row in rowid order (rowid first, then each column as its SQLite storage class and value: integer as
//              64 bits, real as its 64 bits, text and blob as bytes up to the first NUL, NULL as a tag).
//    Equal fingerprints = same schema in the same order and the same rows, rowids, types and values in every table
//    (blobs with an embedded NUL compare only up to it; the game database has none [I]). sqlite_sequence is included
//    as any table. The ANALYZE tables (sqlite_stat1, _stat2, _stat3) are compared as a set: their rows without rowid,
//    sorted by every column. Why: ANALYZE writes one row per index in the order of SQLite's in-memory schema hash,
//    whose bucket count follows the allocator's block sizes; the closed library (own allocator) and
//    CvGameDatabase_x64 (CRT allocator) write the same 608 rows of the VP 5.4.8 database in different rowid order
//    [L 2026-11-10, issue 2504, proxied Tiny boot: same set, other order]. The query planner reads them as a set.
// 2. VpDbOp: one write the DLL made to its connection, replayed on the engine's connection in the same order, with
//    the same SQL text, bindings and the same Results/Connection member (Step, Execute, Analyze, transactions).
// 3. VpDbMainFile: the file of the connection's main database ("" for an in-memory or temporary database).
// Serialisation is little-endian bytes in a std::vector (no vpipc dependency; the unit test links without it).
#pragma once
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>
#include "CvGameDatabase.h"

namespace vpdb
{
// ------------------------------------------------------------------------------------------------- bytes
struct Out
{
	std::vector<uint8_t> b;
	void u32(uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((uint8_t)(v >> (8 * i))); }
	void u64(uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back((uint8_t)(v >> (8 * i))); }
	void f64(double d) { uint64_t v; memcpy(&v, &d, 8); u64(v); }
	void str(const std::string& s) { u32((uint32_t)s.size()); b.insert(b.end(), s.begin(), s.end()); }
};
struct In
{
	const uint8_t* p;
	size_t n, i;
	bool ok;
	In(const uint8_t* p_, size_t n_) : p(p_), n(n_), i(0), ok(true) {}
	bool need(size_t k) { if (!ok || n - i < k) ok = false; return ok; }
	uint32_t u32() { uint32_t v = 0; if (need(4)) { for (int k = 0; k < 4; ++k) v |= (uint32_t)p[i + k] << (8 * k); i += 4; } return v; }
	uint64_t u64() { uint64_t v = 0; if (need(8)) { for (int k = 0; k < 8; ++k) v |= (uint64_t)p[i + k] << (8 * k); i += 8; } return v; }
	double f64() { uint64_t v = u64(); double d; memcpy(&d, &v, 8); return d; }
	std::string str() { const uint32_t k = u32(); std::string s; if (need(k)) { s.assign((const char*)p + i, k); i += k; } return s; }
};

// ------------------------------------------------------------------------------------------------- hashing
const uint64_t kFnvBasis = 1469598103934665603ull;
inline void Fnv(uint64_t& h, const void* p, size_t n)
{
	const uint8_t* b = static_cast<const uint8_t*>(p);
	for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
}
inline void FnvTag(uint64_t& h, uint8_t t) { Fnv(h, &t, 1); }
inline void FnvStr(uint64_t& h, const char* s)
{
	if (!s) { FnvTag(h, 0xFF); return; }
	const uint32_t n = (uint32_t)strlen(s);
	Fnv(h, &n, 4);
	Fnv(h, s, n);
}

// "name" -> "\"name\"" with embedded quotes doubled.
inline std::string Quote(const std::string& name)
{
	std::string q = "\"";
	for (size_t i = 0; i < name.size(); ++i)
	{
		q += name[i];
		if (name[i] == '"') q += '"';
	}
	return q + "\"";
}

// ------------------------------------------------------------------------------------------------- fingerprint
struct TableFp
{
	std::string name;
	uint64_t rows;
	uint64_t hash;
};
struct Fingerprint
{
	bool ok;
	std::string error;
	uint64_t schema;
	uint32_t schemaRows;
	std::vector<TableFp> tables;
	Fingerprint() : ok(false), schema(0), schemaRows(0) {}

	uint64_t TotalRows() const
	{
		uint64_t n = 0;
		for (size_t i = 0; i < tables.size(); ++i) n += tables[i].rows;
		return n;
	}
	// One hash over everything (for a log line).
	uint64_t Digest() const
	{
		uint64_t h = kFnvBasis;
		Fnv(h, &schema, 8);
		for (size_t i = 0; i < tables.size(); ++i)
		{
			FnvStr(h, tables[i].name.c_str());
			Fnv(h, &tables[i].rows, 8);
			Fnv(h, &tables[i].hash, 8);
		}
		return h;
	}
	std::vector<uint8_t> Serialize() const
	{
		Out o;
		o.u32(ok ? 1u : 0u);
		o.str(error);
		o.u64(schema);
		o.u32(schemaRows);
		o.u32((uint32_t)tables.size());
		for (size_t i = 0; i < tables.size(); ++i)
		{
			o.str(tables[i].name);
			o.u64(tables[i].rows);
			o.u64(tables[i].hash);
		}
		return o.b;
	}
	bool Parse(const uint8_t* p, size_t n)
	{
		In r(p, n);
		ok = r.u32() != 0;
		error = r.str();
		schema = r.u64();
		schemaRows = r.u32();
		const uint32_t k = r.u32();
		tables.clear();
		for (uint32_t i = 0; i < k && r.ok; ++i)
		{
			TableFp t;
			t.name = r.str();
			t.rows = r.u64();
			t.hash = r.u64();
			tables.push_back(t);
		}
		return r.ok && r.i == r.n;
	}
};

// Differences between two fingerprints, one line each (at most maxLines; the count of all is returned).
inline size_t Compare(const Fingerprint& a, const Fingerprint& b, std::vector<std::string>& lines, size_t maxLines,
                      const char* nameA, const char* nameB)
{
	size_t n = 0;
	char sz[512];
	if (!a.ok || !b.ok)
	{
		_snprintf_s(sz, sizeof(sz), _TRUNCATE, "fingerprint not computed: %s %s, %s %s", nameA,
		            a.ok ? "ok" : a.error.c_str(), nameB, b.ok ? "ok" : b.error.c_str());
		lines.push_back(sz);
		return 1;
	}
	if (a.schema != b.schema || a.schemaRows != b.schemaRows)
	{
		++n;
		_snprintf_s(sz, sizeof(sz), _TRUNCATE, "schema differs: %s %u rows hash %016llx, %s %u rows hash %016llx", nameA,
		            a.schemaRows, (unsigned long long)a.schema, nameB, b.schemaRows, (unsigned long long)b.schema);
		lines.push_back(sz);
	}
	size_t i = 0, j = 0;
	while (i < a.tables.size() || j < b.tables.size())
	{
		const int c = i == a.tables.size() ? 1 : j == b.tables.size() ? -1 : a.tables[i].name.compare(b.tables[j].name);
		if (c == 0)
		{
			if (a.tables[i].rows != b.tables[j].rows || a.tables[i].hash != b.tables[j].hash)
			{
				if (++n <= maxLines)
				{
					_snprintf_s(sz, sizeof(sz), _TRUNCATE, "table %s differs: %s %llu rows hash %016llx, %s %llu rows hash %016llx",
					            a.tables[i].name.c_str(), nameA, (unsigned long long)a.tables[i].rows,
					            (unsigned long long)a.tables[i].hash, nameB, (unsigned long long)b.tables[j].rows,
					            (unsigned long long)b.tables[j].hash);
					lines.push_back(sz);
				}
			}
			++i;
			++j;
		}
		else
		{
			const bool onlyA = c < 0;
			const TableFp& t = onlyA ? a.tables[i] : b.tables[j];
			if (++n <= maxLines)
			{
				_snprintf_s(sz, sizeof(sz), _TRUNCATE, "table %s only in %s (%llu rows)", t.name.c_str(),
				            onlyA ? nameA : nameB, (unsigned long long)t.rows);
				lines.push_back(sz);
			}
			if (onlyA) ++i; else ++j;
		}
	}
	return n;
}

// The tables whose rows differ, when the schema and the set of tables are equal (true); false otherwise.
inline bool DifferingTables(const Fingerprint& a, const Fingerprint& b, std::vector<std::string>& names)
{
	names.clear();
	if (!a.ok || !b.ok || a.schema != b.schema || a.schemaRows != b.schemaRows || a.tables.size() != b.tables.size())
		return false;
	for (size_t i = 0; i < a.tables.size(); ++i)
	{
		if (a.tables[i].name != b.tables[i].name) return false;
		if (a.tables[i].rows != b.tables[i].rows || a.tables[i].hash != b.tables[i].hash) names.push_back(a.tables[i].name);
	}
	return true;
}

// Database::Results built and destroyed by the library's own exported members only.
inline Fingerprint Compute(Database::Connection& db)
{
	Fingerprint fp;
	{
		Database::Results r;
		if (!db.Execute(r, "SELECT type, name, tbl_name, sql FROM sqlite_master ORDER BY rowid"))
		{
			fp.error = std::string("sqlite_master: ") + (db.ErrorMessage() ? db.ErrorMessage() : "?");
			return fp;
		}
		uint64_t h = kFnvBasis;
		while (r.Step())
		{
			++fp.schemaRows;
			for (int c = 0; c < 4; ++c) FnvStr(h, r.GetText(c));
		}
		fp.schema = h;
	}
	std::vector<std::string> names;
	{
		Database::Results r;
		if (!db.Execute(r, "SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name"))
		{
			fp.error = "table list";
			return fp;
		}
		while (r.Step())
			names.push_back(r.GetText(0) ? r.GetText(0) : "");
	}
	for (size_t i = 0; i < names.size(); ++i)
	{
		TableFp t;
		t.name = names[i];
		t.rows = 0;
		uint64_t h = kFnvBasis;
		std::string sql = "SELECT rowid, * FROM " + Quote(names[i]) + " ORDER BY rowid";
		if (names[i].compare(0, 11, "sqlite_stat") == 0)
		{
			// a set: no rowid, every column sorted (see the top of this file)
			int nc0 = 0;
			{
				Database::Results c;
				const std::string probe = "SELECT * FROM " + Quote(names[i]) + " LIMIT 0";
				if (db.Execute(c, probe.c_str())) nc0 = c.ColumnCount();
			}
			sql = "SELECT * FROM " + Quote(names[i]);
			for (int c = 1; c <= nc0; ++c)
			{
				char k[16];
				_snprintf_s(k, sizeof(k), _TRUNCATE, "%s%d", c == 1 ? " ORDER BY " : ", ", c);
				sql += k;
			}
		}
		Database::Results r;
		if (!db.Execute(r, sql.c_str()))
		{
			fp.error = "cannot read table " + names[i];
			return fp;
		}
		const int nc = r.ColumnCount();
		while (r.Step())
		{
			++t.rows;
			for (int c = 0; c < nc; ++c)
			{
				const int type = (int)r.ColumnType(c);
				FnvTag(h, (uint8_t)type);
				if (type == 1)  // SQLITE_INTEGER
				{
					const int64_t v = r.GetInt64(c);
					Fnv(h, &v, 8);
				}
				else if (type == 2)  // SQLITE_FLOAT
				{
					const double v = r.GetDouble(c);
					Fnv(h, &v, 8);
				}
				else if (type == 3 || type == 4)  // SQLITE_TEXT, SQLITE_BLOB
					FnvStr(h, r.GetText(c));
			}
		}
		t.hash = h;
		fp.tables.push_back(t);
	}
	fp.ok = true;
	return fp;
}

// The main database's file ("" in memory or temporary) and a text description of every attached database.
inline std::string MainFile(Database::Connection& db, std::string& list)
{
	std::string main;
	Database::Results r;
	if (db.Execute(r, "PRAGMA database_list"))
	{
		while (r.Step())
		{
			const char* name = r.GetText(1);
			const char* file = r.GetText(2);
			if (!list.empty()) list += "; ";
			list += std::string(name ? name : "?") + "='" + (file ? file : "") + "'";
			if (name && strcmp(name, "main") == 0) main = file ? file : "";
		}
	}
	return main;
}
inline std::string Version(Database::Connection& db)
{
	Database::Results r;
	if (db.Execute(r, "SELECT sqlite_version()") && r.Step() && r.GetText(0)) return r.GetText(0);
	return "?";
}

#ifdef VPDB_WITH_BINARYIO
// The engine's closed library only (CvGameDatabase_x64 does not implement DatabaseIO.h): writes the connection's main
// database into a new file. BinaryIO::Save copies the database page by page: a VP 5.4.8 database loaded into
// ":memory:" with BinaryIO::Load and saved again differs from its source file only in header bytes 24-27 and 92-95
// (file change counter, version-valid-for) [L 2026-11-10, issue 2504, closed library f9563739...].
inline bool Snapshot(Database::Connection& db, const char* path)
{
	DeleteFileA(path);
	Database::BinaryIO io(path);
	return io.Save(db);
}
#endif

// ------------------------------------------------------------------------------------------------- write replay
struct Bind
{
	int32_t idx;
	uint32_t type;  // 0 NULL, 1 int, 2 double, 3 text
	int32_t i;
	double d;
	std::string s;
};
enum OpKind
{
	kOpExecSql = 1,      // Connection::Execute(sql)
	kOpStmtStep = 2,     // Connection::Execute(Results&, sql) + Bind... + Results::Step()
	kOpStmtExecute = 3,  // ... + Results::Execute()
	kOpAnalyze = 4,      // Connection::Analyze()
	kOpBegin = 5,        // Connection::BeginTransaction()
	kOpEnd = 6,          // Connection::EndTransaction()
	kOpCommit = 7,       // Connection::CommitTransaction()
	kOpClearCountCache = 8
};
inline const char* OpName(uint32_t k)
{
	switch (k)
	{
	case kOpExecSql: return "Execute(sql)";
	case kOpStmtStep: return "Results::Step";
	case kOpStmtExecute: return "Results::Execute";
	case kOpAnalyze: return "Analyze";
	case kOpBegin: return "BeginTransaction";
	case kOpEnd: return "EndTransaction";
	case kOpCommit: return "CommitTransaction";
	case kOpClearCountCache: return "ClearCountCache";
	default: return "?";
	}
}
const uint32_t kResultVoid = 2;  // a member without a result

struct Op
{
	uint32_t kind;
	std::string sql;
	std::vector<Bind> binds;
	std::vector<uint8_t> Serialize() const
	{
		Out o;
		o.u32(kind);
		o.str(sql);
		o.u32((uint32_t)binds.size());
		for (size_t i = 0; i < binds.size(); ++i)
		{
			o.u32((uint32_t)binds[i].idx);
			o.u32(binds[i].type);
			if (binds[i].type == 1) o.u32((uint32_t)binds[i].i);
			else if (binds[i].type == 2) o.f64(binds[i].d);
			else if (binds[i].type == 3) o.str(binds[i].s);
		}
		return o.b;
	}
	bool Parse(const uint8_t* p, size_t n)
	{
		In r(p, n);
		kind = r.u32();
		sql = r.str();
		const uint32_t k = r.u32();
		binds.clear();
		for (uint32_t i = 0; i < k && r.ok; ++i)
		{
			Bind b;
			b.idx = (int32_t)r.u32();
			b.type = r.u32();
			b.i = 0;
			b.d = 0.0;
			if (b.type == 1) b.i = (int32_t)r.u32();
			else if (b.type == 2) b.d = r.f64();
			else if (b.type == 3) b.s = r.str();
			binds.push_back(b);
		}
		return r.ok && r.i == r.n;
	}
};

// Runs one op on a connection with the same library members the DLL called. Returns the member's bool result (0/1),
// kResultVoid for members without one, or 3 when the statement could not be prepared on this connection.
inline uint32_t Apply(Database::Connection& db, const Op& op)
{
	switch (op.kind)
	{
	case kOpExecSql: return db.Execute(op.sql.c_str()) ? 1u : 0u;
	case kOpAnalyze: db.Analyze(); return kResultVoid;
	case kOpBegin: db.BeginTransaction(); return kResultVoid;
	case kOpEnd: db.EndTransaction(); return kResultVoid;
	case kOpCommit: db.CommitTransaction(); return kResultVoid;
	case kOpClearCountCache: db.ClearCountCache(); return kResultVoid;
	case kOpStmtStep:
	case kOpStmtExecute:
	{
		Database::Results r;
		if (!db.Execute(r, op.sql.c_str())) return 3u;
		for (size_t i = 0; i < op.binds.size(); ++i)
		{
			const Bind& b = op.binds[i];
			if (b.type == 1) r.Bind(b.idx, (int)b.i);
			else if (b.type == 2) r.Bind(b.idx, b.d);
			else if (b.type == 3) r.Bind(b.idx, b.s.c_str(), (int)b.s.size(), true);
			else r.BindNULL(b.idx);
		}
		return (op.kind == kOpStmtStep ? r.Step() : r.Execute()) ? 1u : 0u;
	}
	default: return 4u;
	}
}

// The table a writing statement changes (issue 2504 follow-up: a table the DLL wrote may not be pulled from the
// engine): INSERT [OR x] INTO t, REPLACE INTO t, UPDATE [OR x] t, DELETE FROM t; a "schema." prefix is dropped and
// quotes removed. "" for anything else (the caller treats that as "any table").
namespace detail
{
inline void Blank(const char* s, size_t n, size_t& i)
{
	while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
}
inline std::string Word(const char* s, size_t n, size_t& i)
{
	Blank(s, n, i);
	std::string w;
	if (i < n && (s[i] == '"' || s[i] == '[' || s[i] == '`' || s[i] == '\''))
	{
		const char close = s[i] == '[' ? ']' : s[i];
		for (++i; i < n && s[i] != close; ++i) w += s[i];
		if (i < n) ++i;
		return w;
	}
	while (i < n && ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '_'))
		w += s[i++];
	return w;
}
inline std::string Lower(std::string w)
{
	for (size_t k = 0; k < w.size(); ++k)
		if (w[k] >= 'A' && w[k] <= 'Z') w[k] = (char)(w[k] | 0x20);
	return w;
}
inline std::string Name(const char* s, size_t n, size_t& i)
{
	std::string a = Word(s, n, i);
	Blank(s, n, i);
	if (i < n && s[i] == '.')
	{
		++i;
		a = Word(s, n, i);
	}
	return a;
}
}  // namespace detail
inline std::string WriteTarget(const char* s, size_t n)
{
	using namespace detail;
	size_t i = 0;
	const std::string k = Lower(Word(s, n, i));
	if (k == "insert" || k == "replace" || k == "update")
	{
		size_t j = i;
		std::string w = Lower(Word(s, n, j));
		if (k != "replace" && w == "or")
		{
			Word(s, n, j);  // the conflict clause
			i = j;
			w = Lower(Word(s, n, j));
		}
		if (k != "update")
		{
			if (w != "into") return std::string();
			i = j;
		}
		return Name(s, n, i);
	}
	if (k == "delete")
	{
		if (Lower(Word(s, n, i)) != "from") return std::string();
		return Name(s, n, i);
	}
	return std::string();
}

// Statements that change the database (or its transaction state): everything but SELECT, EXPLAIN and PRAGMA without
// an assignment. Leading blanks, comments and '(' are skipped; an unknown first word counts as a write.
inline bool IsWriteSql(const char* s, size_t n)
{
	size_t i = 0;
	for (;;)
	{
		while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n' || s[i] == '(')) ++i;
		if (i + 1 < n && s[i] == '-' && s[i + 1] == '-')
		{
			while (i < n && s[i] != '\n') ++i;
			continue;
		}
		if (i + 1 < n && s[i] == '/' && s[i + 1] == '*')
		{
			i += 2;
			while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) ++i;
			i += 2;
			continue;
		}
		break;
	}
	char w[16];
	size_t k = 0;
	while (i < n && k < sizeof(w) - 1 && ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z')))
		w[k++] = (char)(s[i++] | 0x20);
	w[k] = 0;
	if (!strcmp(w, "select") || !strcmp(w, "explain") || !strcmp(w, "values")) return false;
	if (!strcmp(w, "pragma")) return memchr(s + i, '=', n - i) != NULL;
	return true;
}
}  // namespace vpdb
