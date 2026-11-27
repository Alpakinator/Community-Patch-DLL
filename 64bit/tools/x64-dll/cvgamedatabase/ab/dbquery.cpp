// A/B query program for the CvGameDatabase library (issues 1402, 1403).
//
// Compiled once and linked against either the closed CvGameDatabaseWin32.lib (x86, VC9 STL inside the DLL) or
// our CvGameDatabase_<arch>.lib. It must not depend on the layout of Connection/Results/SingleResult: objects are
// built with placement new into large aligned buffers, so only the library's exported constructors touch the
// members; only exported members are called; destruction is the explicit (virtual) destructor. sizeof of those
// classes is never used.
//
// Usage: dbquery <database> <scratch dir> <output file>
// The database is modified (writes in a transaction, ANALYZE): pass a fresh copy. Output is deterministic text.
// Lines starting with '~' hold values that legitimately differ between libraries (pointers, memory statistics);
// run_ab.py compares them separately.
#define CvGameDatabaseAPI __declspec(dllimport)
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <new>
#include <string>
#include <vector>
#include "CvGameDatabase.h"

using namespace Database;

static FILE* g_out;

static void P(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(g_out, fmt, ap);
	va_end(ap);
}

static std::string Q(const char* s);

// Writes every message into the output. Memory statistics are marked '~' (they differ between libraries).
class TestLogger : public IDatabaseLogger
{
public:
	virtual ~TestLogger() { P("  LOG logger deleted\n"); }
	virtual void Log(const char* szMessage, LogTypes eType)
	{
		const bool bVolatile = szMessage && strstr(szMessage, "SQLite Memory Statistics") != NULL;
		P("%s LOG%d %s\n", bVolatile ? "~" : " ", (int)eType, Q(szMessage).c_str());
	}
};

// ---- layout-agnostic object holders --------------------------------------------------------------------------
struct alignas(64) Buf
{
	unsigned char b[4096];
};

struct Conn
{
	Buf buf;
	Connection* c;
	Conn() { memset(&buf, 0xCD, sizeof(buf)); c = ::new (static_cast<void*>(&buf)) Connection(); }
	~Conn() { c->~Connection(); }
	Connection* operator->() { return c; }
};

struct Res
{
	Buf buf;
	Results* r;
	explicit Res(const char* cols = NULL, bool single = false)
	{
		memset(&buf, 0xCD, sizeof(buf));
		if (single)
			r = ::new (static_cast<void*>(&buf)) SingleResult(cols);
		else
			r = ::new (static_cast<void*>(&buf)) Results(cols);
	}
	~Res() { r->~Results(); }  // virtual: the library's own destructor
	Results* operator->() { return r; }
	Results& operator*() { return *r; }
};

// ---- formatting ----------------------------------------------------------------------------------------------
static std::string Q(const char* s)
{
	if (!s)
		return "<NULL>";
	std::string o = "\"";
	for (const unsigned char* p = (const unsigned char*)s; *p; ++p)
	{
		if (*p == '"' || *p == '\\') { o += '\\'; o += (char)*p; }
		else if (*p == '\n') o += "\\n";
		else if (*p == '\r') o += "\\r";
		else if (*p == '\t') o += "\\t";
		else if (*p < 0x20) { char t[8]; sprintf(t, "\\x%02x", *p); o += t; }
		else o += (char)*p;
	}
	return o + "\"";
}

static std::string QW(const wchar_t* s)
{
	if (!s)
		return "<NULL>";
	std::string o = "w\"";
	for (const wchar_t* p = s; *p; ++p)
	{
		if (*p >= 0x20 && *p < 0x7F && *p != '"' && *p != '\\') o += (char)*p;
		else { char t[12]; sprintf(t, "\\u%04x", (unsigned)*p); o += t; }
	}
	return o + "\"";
}

static unsigned FB(float f) { unsigned u; memcpy(&u, &f, 4); return u; }
static unsigned long long DB(double d) { unsigned long long u; memcpy(&u, &d, 8); return u; }

static std::string Cell(Results& r, int i)
{
	char t[160];
	const char* tx = r.GetText(i);
	std::string s = Q(tx);
	int ty = (int)r.ColumnType(i);
	int iv = r.GetInt(i);
	long long lv = r.GetInt64(i);
	float fv = r.GetFloat(i);
	double dv = r.GetDouble(i);
	bool bv = r.GetBool(i);
	sprintf(t, "t%d|i%d|l%lld|f%08x|d%016llx|b%d|", ty, iv, lv, FB(fv), DB(dv), bv ? 1 : 0);
	return t + s;
}

// The by-name getters and GetValue overloads, rendered the same way so they can be compared with Cell().
static std::string CellByName(Results& r, const char* name)
{
	char t[160];
	const char* tx = r.GetText(name);
	std::string s = Q(tx);
	int iv = r.GetInt(name);
	long long lv = r.GetInt64(name);
	float fv = r.GetFloat(name);
	double dv = r.GetDouble(name);
	bool bv = r.GetBool(name);
	int ty = r.HasColumn(name) ? (int)r.ColumnType(r.ColumnPosition(name)) : -99;
	sprintf(t, "t%d|i%d|l%lld|f%08x|d%016llx|b%d|", ty, iv, lv, FB(fv), DB(dv), bv ? 1 : 0);
	return t + s;
}

static std::string CellGetValue(Results& r, int i, const char* name)
{
	// Fill each out-param with a sentinel first: shows whether the library writes it at all.
	char t[200];
	bool b1 = true, b2 = true; int i1 = 0x5A5A5A5A, i2 = 0x5A5A5A5A; long long l1 = 0x5A5A5A5A5A5ALL, l2 = l1;
	float f1 = -7.25f, f2 = -7.25f; double d1 = -7.25, d2 = -7.25;
	const char* z1 = "<unset>"; const char* z2 = "<unset>";
	const wchar_t* w1 = L"<unset>"; const wchar_t* w2 = L"<unset>";
	r.GetValue(i, b1); r.GetValue(i, i1); r.GetValue(i, l1); r.GetValue(i, f1); r.GetValue(i, d1);
	r.GetValue(name, b2); r.GetValue(name, i2); r.GetValue(name, l2); r.GetValue(name, f2); r.GetValue(name, d2);
	sprintf(t, "b%d,%d|i%d,%d|l%lld,%lld|f%08x,%08x|d%016llx,%016llx|", b1, b2, i1, i2, l1, l2, FB(f1), FB(f2), DB(d1),
	        DB(d2));
	// Each text pointer is rendered before the next text call: a UTF-16 conversion may free the UTF-8 buffer.
	std::string s = t;
	r.GetValue(i, z1); s += Q(z1) + ",";
	r.GetValue(name, z2); s += Q(z2) + "|";
	r.GetValue(i, w1); s += QW(w1) + ",";
	r.GetValue(name, w2); s += QW(w2) + "|";
	s += QW(r.GetText16(i)) + ",";
	s += QW(r.GetText16(name));
	return s;
}

static void Header(Results& r)
{
	int n = r.ColumnCount();
	P("  cols=%d", n);
	for (int i = 0; i < n; ++i)
		P(" [%s:%s]", Q(r.ColumnName(i)).c_str(), Q(r.ColumnTypeName(i)).c_str());
	P("\n");
}

// Dump all rows of an already prepared Results.
static int DumpRows(Results& r, bool full)
{
	int rows = 0;
	bool first = true;
	while (r.Step())
	{
		if (first)
		{
			Header(r);
			first = false;
		}
		int n = r.ColumnCount();
		P("  R%d", rows);
		for (int i = 0; i < n; ++i)
			P("\t%s", Cell(r, i).c_str());
		P("\n");
		if (full)
		{
			for (int i = 0; i < n; ++i)
			{
				const char* name = r.ColumnName(i);
				std::string a = Cell(r, i), b = CellByName(r, name);
				if (a != b)
					P("  N%d.%d %s %s\n", rows, i, Q(name).c_str(), b.c_str());
				if (rows < 2)
					P("  V%d.%d %s\n", rows, i, CellGetValue(r, i, name).c_str());
			}
		}
		++rows;
	}
	if (first)
		Header(r);
	P("  rows=%d\n", rows);
	return rows;
}

static void Err(Conn& c, const char* what)
{
	P("  %s: err=%d msg=%s\n", what, c->ErrorCode(), Q(c->ErrorMessage()).c_str());
}

int main(int argc, char** argv)
{
	if (argc != 4)
	{
		fprintf(stderr, "usage: dbquery <database> <scratch dir> <output>\n");
		return 2;
	}
	const char* dbpath = argv[1];
	std::string scratch = argv[2];
	g_out = fopen(argv[3], "wb");
	if (!g_out)
		return 3;
	P("# dbquery 1\n");

	// ---- 1. Open on files that do not exist ------------------------------------------------------------------
	P("== open-missing\n");
	{
		const int flagsets[] = {0, OPEN_READONLY, OPEN_READWRITE, OPEN_READWRITE | OPEN_CREATE,
		                        OPEN_READWRITE | OPEN_CREATE | OPEN_FULLMUTEX, OPEN_CREATE};
		for (int k = 0; k < (int)(sizeof(flagsets) / sizeof(flagsets[0])); ++k)
		{
			char name[64];
			sprintf(name, "\\missing_%d.db", k);
			std::string p = scratch + name;
			DeleteFileA(p.c_str());
			Conn c;
			bool ok = c->Open(p.c_str(), flagsets[k]);
			DWORD attr = GetFileAttributesA(p.c_str());
			P(" flags=0x%x open=%d created=%d\n", flagsets[k], ok ? 1 : 0, attr != INVALID_FILE_ATTRIBUTES ? 1 : 0);
			Err(c, "after open");
			if (ok)
			{
				bool e = c->Execute("CREATE TABLE t(a INTEGER, b TEXT)");
				P("  create=%d\n", e ? 1 : 0);
				Err(c, "after create");
				P("  count=%d\n", c->Count("t", false));
			}
			c->Close();
			Err(c, "after close");
		}
		// The constructor that opens.
		{
			std::string p = scratch + "\\ctor_open.db";
			DeleteFileA(p.c_str());
			Buf b;
			Connection* c = ::new (static_cast<void*>(&b)) Connection(p.c_str(), OPEN_READWRITE | OPEN_CREATE);
			P(" ctor(file,RW|CREATE): created=%d exec=%d\n",
			  GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES ? 1 : 0, c->Execute("SELECT 1") ? 1 : 0);
			c->~Connection();
		}
		// Unopened connection.
		{
			Conn c;
			Res r;
			P(" unopened: count=%d\n", c->Count("Units", false));
			bool e = c->Execute(*r, "SELECT 1");
			P(" unopened: execute=%d\n", e ? 1 : 0);
		}
	}

	Conn db;
	P("== open-main\n");
	{
		bool ok = db->Open(dbpath, OPEN_CREATE | OPEN_READWRITE | OPEN_FULLMUTEX);
		P(" open=%d\n", ok ? 1 : 0);
		Err(db, "after open");
		if (!ok)
			return 4;
		P(" totalchanges=%d\n", db->TotalChanges());
		// From here on every message the library logs is part of the output (owned and deleted by the Connection).
		db->SetLogger(new TestLogger);
		db->LogMessage("logger installed");
	}

	// ---- 2. Table list ----------------------------------------------------------------------------------------
	std::vector<std::string> tables;
	{
		Res r;
		bool e = db->Execute(*r, "SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name");
		P("== tables execute=%d\n", e ? 1 : 0);
		while (r->Step())
			tables.push_back(r->GetText(0));
		P(" n=%d\n", (int)tables.size());
	}

	// ---- 3. Every table: counts, all rows, all columns ----------------------------------------------------
	P("== dump\n");
	for (size_t t = 0; t < tables.size(); ++t)
	{
		const char* tn = tables[t].c_str();
		int c1 = db->Count(tn, true), c2 = db->Count(tn, true), c3 = db->Count(tn, false);
		P("TABLE %s count=%d,%d,%d\n", Q(tn).c_str(), c1, c2, c3);
		Res r;
		bool ok = db->SelectAll(*r, tn);
		P("  selectall=%d columns=%s\n", ok ? 1 : 0, Q(r->GetColumns()).c_str());
		if (!ok)
		{
			Err(db, "selectall");
			continue;
		}
		DumpRows(*r, true);
	}

	// ---- 4. Selection overloads -----------------------------------------------------------------------------
	P("== select\n");
	{
		const char* cases[] = {"Units", "Buildings", "Technologies", "Worlds", "Leaders", "NoSuchTable"};
		for (int k = 0; k < 6; ++k)
		{
			for (int single = 0; single < 2; ++single)
			{
				for (int id = -1; id < 3; ++id)
				{
					Res r(NULL, single != 0);
					bool ok = db->SelectAt(*r, cases[k], id);
					P(" SelectAt(%s,rowid %d) single=%d ok=%d sq=%d\n", cases[k], id, single, ok ? 1 : 0,
					  r->SingleQuery() ? 1 : 0);
					if (ok)
					{
						if (single)
							P("  first: %s / %s\n", Q(r->GetText(0)).c_str(), Q(r->GetText("Type")).c_str());
						DumpRows(*r, false);
					}
					else
						Err(db, "selectat");
				}
				{
					Res r(NULL, single != 0);
					bool ok = db->SelectAt(*r, cases[k], "ID", 1);
					P(" SelectAt(%s,ID,int 1) single=%d ok=%d\n", cases[k], single, ok ? 1 : 0);
					if (ok) DumpRows(*r, false); else Err(db, "selectat");
				}
				if (!single)
				{
					// Only 0.0 and only without LIMIT: the closed library formats the double with "%d" (see
					// README), which reads the high 32 bits as the next "%s" argument; for 0.0 that is NULL ("").
					Res r(NULL, false);
					bool ok = db->SelectAt(*r, cases[k], "ID", 0.0);
					P(" SelectAt(%s,ID,double 0) ok=%d\n", cases[k], ok ? 1 : 0);
					if (ok) DumpRows(*r, false); else Err(db, "selectat");
				}
				{
					Res r("ID, Type", single != 0);
					bool ok = db->SelectAt(*r, cases[k], "Type", "UNIT_WARRIOR");
					P(" SelectAt(%s,Type,'UNIT_WARRIOR') cols='ID, Type' single=%d ok=%d\n", cases[k], single,
					  ok ? 1 : 0);
					if (ok) DumpRows(*r, false); else Err(db, "selectat");
				}
				{
					Res r(NULL, single != 0);
					bool ok = db->SelectAt(*r, cases[k], "NoSuchColumn", "x");
					P(" SelectAt(%s,NoSuchColumn) single=%d ok=%d\n", cases[k], single, ok ? 1 : 0);
					if (ok) DumpRows(*r, false); else Err(db, "selectat");
				}
				{
					Res r("Type", single != 0);
					bool ok = db->SelectWhere(*r, cases[k], "ID < 3 ORDER BY ID DESC");
					P(" SelectWhere(%s,'ID < 3 ORDER BY ID DESC') single=%d ok=%d\n", cases[k], single, ok ? 1 : 0);
					if (ok) DumpRows(*r, false); else Err(db, "selectwhere");
				}
				{
					Res r(NULL, single != 0);
					bool ok = db->SelectWhere(*r, cases[k], "this is not sql");
					P(" SelectWhere(%s,bad) single=%d ok=%d\n", cases[k], single, ok ? 1 : 0);
					if (ok) DumpRows(*r, false); else Err(db, "selectwhere");
				}
			}
		}
		// SetColumns / GetColumns / copy constructor
		{
			Res r("count(*)", true);
			bool ok = db->SelectAt(*r, "HandicapInfo_Goodies", "HandicapType", "HANDICAP_PRINCE");
			P(" count(*) single ok=%d v=%d\n", ok ? 1 : 0, r->GetInt(0));
			r->SetColumns("Type, ID");
			P(" setcolumns -> %s\n", Q(r->GetColumns()).c_str());
			bool ok2 = db->SelectAll(*r, "Eras");
			P(" selectall eras after setcolumns ok=%d\n", ok2 ? 1 : 0);
			if (ok2) DumpRows(*r, false);
			r->SingleQuery(false);
			P(" sq after set false=%d\n", r->SingleQuery() ? 1 : 0);
		}
	}

	// ---- 5. Execute with Bind of every overload ------------------------------------------------------------
	P("== bind\n");
	{
		Res r;
		bool ok = db->Execute(*r, "SELECT ?1, typeof(?1), ?2, typeof(?2), ?3, typeof(?3), ?4, typeof(?4), ?5, "
		                          "typeof(?5), ?6, typeof(?6), ?7, typeof(?7), ?8, typeof(?8), ?9, typeof(?9)");
		P(" execute=%d\n", ok ? 1 : 0);
		const char* s = "abcdef";
		bool b[12];
		b[0] = r->Bind(1, s, 3, true);
		b[1] = r->Bind(2, "UNIT_WARRIOR", false);
		b[2] = r->Bind(3, L"wideé", 4, true);
		b[3] = r->Bind(4, L"wide2é", true);
		b[4] = r->Bind(5, 1.0 / 3.0);
		b[5] = r->Bind(6, 1.0f / 3.0f);
		b[6] = r->Bind(7, -42);
		b[7] = r->Bind(8, (__int64)0x123456789LL);
		b[8] = r->BindNULL(9);
		b[9] = r->Bind(10, 5);   // out of range
		b[10] = r->BindNULL(0);  // out of range
		P(" binds=");
		for (int k = 0; k < 11; ++k)
			P("%d", b[k] ? 1 : 0);
		P("\n");
		Err(db, "after binds");
		DumpRows(*r, true);
		P(" reset=%d\n", r->Reset() ? 1 : 0);
		DumpRows(*r, false);  // bindings cleared by Reset?
		P(" tryreset=%d\n", r->TryReset() ? 1 : 0);
		r->Release();
		P(" after release: step=%d reset=%d bind=%d cols=%d\n", r->Step() ? 1 : 0, r->Reset() ? 1 : 0,
		  r->Bind(1, 1) ? 1 : 0, r->ColumnCount());
	}
	{
		// The game's pattern (CvDllDatabaseUtility): one prepared statement, Bind + Reset many times.
		Res r;
		bool ok = db->Execute(*r, "SELECT ID, Type FROM Units WHERE Type = ?");
		P(" loop execute=%d\n", ok ? 1 : 0);
		const char* types[] = {"UNIT_WARRIOR", "UNIT_SETTLER", "UNIT_NOPE", "UNIT_WORKER"};
		for (int k = 0; k < 4; ++k)
		{
			bool bb = r->Bind(1, types[k], false);
			P("  bind=%d", bb ? 1 : 0);
			DumpRows(*r, false);
			P("  reset=%d\n", r->Reset() ? 1 : 0);
		}
	}

	// ---- 6. Conversions and odd values --------------------------------------------------------------------
	P("== conversions\n");
	{
		Res r;
		bool ok = db->Execute(*r,
		    "SELECT NULL AS n, 'abc' AS s, '12abc' AS s2, ' 7' AS s3, '0x10' AS hx, '3.75' AS sf, 'true' AS st, "
		    "'false' AS sfl, '1' AS s1, '' AS e, 0 AS z, 1 AS one, -1 AS m1, 2 AS two, 2147483648 AS big, "
		    "-2147483649 AS nbig, 9223372036854775807 AS i64, 2.5 AS f25, 3.5 AS f35, -2.5 AS fm25, 1e300 AS huge, "
		    "-0.0 AS nz, 0.1 AS tenth, x'414243' AS blob, x'' AS eblob, 1 AS a, 2 AS a, 3 AS A, 'x' AS \"Mixed Case\"");
		P(" execute=%d\n", ok ? 1 : 0);
		DumpRows(*r, true);
		ok = db->Execute(*r, "SELECT 1 AS a, 2 AS a, 3 AS A");
		P(" re-execute same results=%d\n", ok ? 1 : 0);
		if (ok && r->Step())
		{
			P("  a=%d A=%d hasA=%d posa=%d posA=%d posmissing=%d hasmissing=%d\n", r->GetInt("a"), r->GetInt("A"),
			  r->HasColumn("A") ? 1 : 0, r->ColumnPosition("a"), r->ColumnPosition("A"), r->ColumnPosition("zz"),
			  r->HasColumn("zz") ? 1 : 0);
			P("  missing by name: %s\n", CellByName(*r, "zz").c_str());
			P("  out of range: idx 7 %s | idx -1 type=%d\n", Q(r->GetText(7)).c_str(), (int)r->ColumnType(-1));
			P("  names: %s %s %s\n", Q(r->ColumnName(0)).c_str(), Q(r->ColumnName(3)).c_str(),
			  Q(r->ColumnTypeName(0)).c_str());
		}
		// Typed columns of a table: declared types and affinity.
		ok = db->Execute("CREATE TEMP TABLE conv(i INTEGER, t TEXT, r REAL, b BOOLEAN, v VARIANT, n NUMERIC)");
		P(" create temp=%d\n", ok ? 1 : 0);
		ok = db->Execute("INSERT INTO conv VALUES ('12', 34, '5.5', 'true', '7', '8.0'), (1.0, 2.5, 3, 1, 'x', "
		                 "'1e3'), (NULL, NULL, NULL, 0, NULL, '0x1F'), ('a', 'b', 'c', 'false', 2.0, ' 12 ')");
		P(" insert=%d\n", ok ? 1 : 0);
		Res r2;
		db->SelectAll(*r2, "conv");
		DumpRows(*r2, true);
	}

	// ---- 7. Error paths -------------------------------------------------------------------------------------
	P("== errors\n");
	{
		const char* bad[] = {"SELECT * FROM NoSuchTable", "THIS IS NOT SQL", "SELECT NoSuchColumn FROM Units", "",
		                     "   ", "SELECT 1; SELECT 2", "INSERT INTO Units(ID) VALUES (0)", NULL};
		for (int k = 0; bad[k]; ++k)
		{
			Res r;
			bool ok = db->Execute(*r, bad[k]);
			P(" Execute(R,%s)=%d", Q(bad[k]).c_str(), ok ? 1 : 0);
			Err(db, "");
			if (ok)
			{
				bool st = r->Step();
				P("  step=%d", st ? 1 : 0);
				Err(db, "");
				bool ex = r->Execute();
				P("  execute()=%d", ex ? 1 : 0);
				Err(db, "");
				P("  tryexecute()=%d\n", r->TryExecute() ? 1 : 0);
			}
			bool ok2 = db->Execute(bad[k]);
			P(" Execute(%s)=%d", Q(bad[k]).c_str(), ok2 ? 1 : 0);
			Err(db, "");
			bool ok3 = db->ExecuteMultiple(bad[k]);
			P(" ExecuteMultiple(%s)=%d", Q(bad[k]).c_str(), ok3 ? 1 : 0);
			Err(db, "");
		}
		P(" count(NoSuchTable)=%d,%d\n", db->Count("NoSuchTable", true), db->Count("NoSuchTable", false));
		Err(db, "count");
		P(" Execute(len 8 of 'SELECT 1234')=");
		{
			Res r;
			bool ok = db->Execute(*r, "SELECT 1234", 8);
			P("%d", ok ? 1 : 0);
			if (ok && r->Step()) P(" v=%d", r->GetInt(0));
			P("\n");
		}
		Res r;
		P(" step on unprepared=%d reset=%d cols=%d\n", r->Step() ? 1 : 0, r->Reset() ? 1 : 0, r->ColumnCount());
	}

	// ---- 8. Writes in transactions ---------------------------------------------------------------------------
	P("== writes\n");
	{
		db->BeginTransaction();
		Err(db, "begin");
		bool ok = db->Execute("CREATE TABLE ab_write(ID INTEGER PRIMARY KEY AUTOINCREMENT, Type TEXT UNIQUE, "
		                      "V REAL, B BOOLEAN DEFAULT 0)");
		P(" create=%d\n", ok ? 1 : 0);
		Err(db, "create");
		Res ins;
		ok = db->Execute(*ins, "INSERT INTO ab_write(Type, V, B) VALUES (?, ?, ?)");
		P(" prepare insert=%d\n", ok ? 1 : 0);
		for (int k = 0; k < 5; ++k)
		{
			char t[32];
			sprintf(t, "T_%d", k % 4);  // k=4 violates UNIQUE
			ins->Bind(1, t);
			ins->Bind(2, k * 0.5);
			ins->Bind(3, k & 1);
			bool e = (k == 4) ? ins->TryExecute() : ins->Execute();
			P("  insert %d exec=%d", k, e ? 1 : 0);
			Err(db, "");
			P("  reset=%d\n", ins->Reset() ? 1 : 0);
		}
		ins->Bind(1, "T_0");
		bool e = ins->Execute();
		P("  dup execute=%d", e ? 1 : 0);
		Err(db, "");
		P("  reset=%d", ins->Reset() ? 1 : 0);
		Err(db, "");
		ins->Release();
		db->EndTransaction();
		Err(db, "end");
		P(" totalchanges=%d count=%d\n", db->TotalChanges(), db->Count("ab_write", false));

		db->BeginTransaction();
		db->Execute("INSERT INTO ab_write(Type) VALUES ('ROLLED_BACK')");
		db->RollbackTransaction();
		Err(db, "rollback");
		db->BeginDeferredTransaction();
		db->Execute("INSERT INTO ab_write(Type) VALUES ('DEFERRED')");
		db->CommitTransaction();
		Err(db, "commit deferred");
		db->BeginImmediateTransaction();
		bool sp = db->SetSavePoint("sp1");
		db->Execute("INSERT INTO ab_write(Type) VALUES ('IN_SAVEPOINT')");
		bool rb = db->RollbackToSavePoint("sp1");
		bool rb2 = db->RollbackToSavePoint("nosuch");
		db->Execute("INSERT INTO ab_write(Type) VALUES ('IMMEDIATE')");
		db->CommitTransaction();
		Err(db, "commit immediate");
		P(" savepoint=%d rollbackto=%d rollbackto(nosuch)=%d\n", sp ? 1 : 0, rb ? 1 : 0, rb2 ? 1 : 0);
		db->BeginExclusiveTransaction();
		db->Execute("INSERT INTO ab_write(Type) VALUES ('EXCLUSIVE')");
		db->EndTransaction();
		db->EndTransaction();  // no transaction open
		Err(db, "end twice");
		db->CommitTransaction();
		Err(db, "commit none");
		bool m = db->ExecuteMultiple("INSERT INTO ab_write(Type) VALUES ('M1'); INSERT INTO ab_write(Type) VALUES "
		                             "('M2');UPDATE ab_write SET V = 9 WHERE Type = 'M1'");
		P(" executemultiple=%d\n", m ? 1 : 0);
		Res r;
		db->SelectAll(*r, "ab_write");
		DumpRows(*r, false);
		P(" count cached=%d uncached=%d\n", db->Count("ab_write", true), db->Count("ab_write", false));
		db->Execute("INSERT INTO ab_write(Type) VALUES ('AFTER_CACHE')");
		P(" after insert: cached=%d uncached=%d\n", db->Count("ab_write", true), db->Count("ab_write", false));
		db->ClearCountCache();
		P(" after clear: cached=%d\n", db->Count("ab_write", true));
		P(" busytimeout=%d\n", db->SetBusyTimeout(100) ? 1 : 0);
		P(" totalchanges=%d\n", db->TotalChanges());
		Res seq;
		db->Execute(*seq, "SELECT name, seq FROM sqlite_sequence ORDER BY name");
		DumpRows(*seq, false);
	}

	// ---- 9. Foreign keys, Analyze, statements --------------------------------------------------------------
	P("== fk\n");
	{
		bool v1 = db->ValidateFKConstraints(true);
		Err(db, "fk(true)");
		bool v2 = db->ValidateFKConstraints(false);
		Err(db, "fk(false)");
		P(" validate(true)=%d validate(false)=%d\n", v1 ? 1 : 0, v2 ? 1 : 0);
	}
	P("== analyze\n");
	{
		db->ClearCountCache();
		db->Analyze();
		Err(db, "analyze");
		Res r;
		bool ok = db->Execute(*r, "SELECT tbl, idx, stat FROM sqlite_stat1 ORDER BY tbl, idx");
		P(" execute=%d\n", ok ? 1 : 0);
		DumpRows(*r, false);
	}
	P("== statements\n");
	{
		int n = db->StatementCount();
		P(" count=%d\n", n);
		for (int k = -1; k <= n; ++k)
			P("  sql[%d]=%s\n", k, Q(db->StatementSQL(k)).c_str());
		Res a;
		db->Execute(*a, "SELECT 'held'");
		P(" count with one held=%d\n", db->StatementCount());
	}
	P("== memory\n");
	{
		const char* m = db->CalculateMemoryStats();
		P("~ memstats %s\n", Q(m).c_str());
		db->Vacuum();
		Err(db, "vacuum");
	}
	P("== close\n");
	db->Close();
	Err(db, "after close");
	P(" count after close=%d\n", db->Count("Units", false));
	db->SetLogger(NULL);  // deletes the logger while the output is still open
	P("# end\n");
	fclose(g_out);
	return 0;
}
