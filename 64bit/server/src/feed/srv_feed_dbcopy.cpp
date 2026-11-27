// srv_feed_dbcopy.cpp - srv_feed_dbcopy.h (issue 4452, epic 0375). Written 2026-11-15 for this repository
// (GPL-3.0-or-later). SQLite's C API is bound from the system's winsqlite3.dll at run time (no SQLite source here).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string.h>
#include "srv_feed_dbcopy.h"

namespace SrvFeed {
namespace {

struct sqlite3;
struct sqlite3_stmt;
struct sqlite3_backup;
const int kOk = 0, kBusy = 5, kLocked = 6, kRow = 100, kDone = 101;
const int kOpenReadOnly = 0x1, kOpenReadWrite = 0x2, kOpenCreate = 0x4, kOpenUri = 0x40;

typedef int (*FnOpen)(const char*, sqlite3**, int, const char*);
typedef int (*FnClose)(sqlite3*);
typedef const char* (*FnErrmsg)(sqlite3*);
typedef int (*FnBusyTimeout)(sqlite3*, int);
typedef sqlite3_backup* (*FnBackupInit)(sqlite3*, const char*, sqlite3*, const char*);
typedef int (*FnBackupStep)(sqlite3_backup*, int);
typedef int (*FnBackupFinish)(sqlite3_backup*);
typedef int (*FnBackupPagecount)(sqlite3_backup*);
typedef int (*FnPrepare)(sqlite3*, const char*, int, sqlite3_stmt**, const char**);
typedef int (*FnStep)(sqlite3_stmt*);
typedef int (*FnFinalize)(sqlite3_stmt*);
typedef long long (*FnColInt64)(sqlite3_stmt*, int);
typedef const unsigned char* (*FnColText)(sqlite3_stmt*, int);

struct Api
{
	bool ok = false;
	std::string why;
	FnOpen open = nullptr;
	FnClose close = nullptr;
	FnErrmsg errmsg = nullptr;
	FnBusyTimeout busyTimeout = nullptr;
	FnBackupInit backupInit = nullptr;
	FnBackupStep backupStep = nullptr;
	FnBackupFinish backupFinish = nullptr;
	FnBackupPagecount backupPagecount = nullptr;
	FnPrepare prepare = nullptr;
	FnStep step = nullptr;
	FnFinalize finalize = nullptr;
	FnColInt64 colInt64 = nullptr;
	FnColText colText = nullptr;
};

template <class F> bool Bind(HMODULE m, const char* name, F& f, std::string& why)
{
	f = reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(m, name)));
	if (!f && why.empty()) why = std::string("winsqlite3.dll has no ") + name;
	return f != nullptr;
}

const Api& GetApi()
{
	static Api a = [] {
		Api r;
		HMODULE m = LoadLibraryExW(L"winsqlite3.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (!m)
		{
			r.why = "winsqlite3.dll not loadable from System32 (error " + std::to_string(GetLastError()) + ")";
			return r;
		}
		bool ok = Bind(m, "sqlite3_open_v2", r.open, r.why);
		ok &= Bind(m, "sqlite3_close", r.close, r.why);
		ok &= Bind(m, "sqlite3_errmsg", r.errmsg, r.why);
		ok &= Bind(m, "sqlite3_busy_timeout", r.busyTimeout, r.why);
		ok &= Bind(m, "sqlite3_backup_init", r.backupInit, r.why);
		ok &= Bind(m, "sqlite3_backup_step", r.backupStep, r.why);
		ok &= Bind(m, "sqlite3_backup_finish", r.backupFinish, r.why);
		ok &= Bind(m, "sqlite3_backup_pagecount", r.backupPagecount, r.why);
		ok &= Bind(m, "sqlite3_prepare_v2", r.prepare, r.why);
		ok &= Bind(m, "sqlite3_step", r.step, r.why);
		ok &= Bind(m, "sqlite3_finalize", r.finalize, r.why);
		ok &= Bind(m, "sqlite3_column_int64", r.colInt64, r.why);
		ok &= Bind(m, "sqlite3_column_text", r.colText, r.why);
		r.ok = ok;
		return r;
	}();
	return a;
}

std::string Utf8(const std::wstring& w)
{
	if (w.empty()) return std::string();
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string s((size_t)(n > 0 ? n : 0), '\0');
	if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
	return s;
}

// file: URI with mode=ro&immutable=1 (the render side's form, models/src/art_db.cpp file_uri)
std::string ImmutableUri(const std::string& path)
{
	std::string p = path;
	for (auto& c : p)
		if (c == '\\') c = '/';
	std::string u = "file:";
	if (p.size() >= 2 && p[1] == ':') u += '/';
	static const char* hex = "0123456789ABCDEF";
	for (unsigned char c : p)
	{
		const bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
		                  c == '_' || c == '.' || c == '~' || c == '/' || c == ':';
		if (keep) u += (char)c;
		else { u += '%'; u += hex[c >> 4]; u += hex[c & 15]; }
	}
	return u + "?mode=ro&immutable=1";
}

// The first column of the first row of `sql` as text (false: a prepare or step error, or no row).
bool QueryFirst(const Api& a, sqlite3* db, const char* sql, std::string& out, std::string& why)
{
	sqlite3_stmt* st = nullptr;
	if (a.prepare(db, sql, -1, &st, nullptr) != kOk || !st)
	{
		why = std::string("prepare \"") + sql + "\": " + a.errmsg(db);
		if (st) a.finalize(st);
		return false;
	}
	const int rc = a.step(st);
	bool ok = rc == kRow;
	if (ok)
	{
		const unsigned char* t = a.colText(st, 0);
		out = t ? (const char*)t : "";
	}
	else
		why = std::string("\"") + sql + "\": step returned " + std::to_string(rc) + " (" + a.errmsg(db) + ")";
	a.finalize(st);
	return ok;
}

bool DataVersion(const Api& a, sqlite3* db, int64_t& v, std::string& why)
{
	std::string s;
	if (!QueryFirst(a, db, "PRAGMA data_version", s, why)) return false;
	v = _strtoi64(s.c_str(), nullptr, 10);
	return true;
}

// Runs one statement to its end (rows ignored); false with why on a prepare or step error.
bool ExecOne(const Api& a, sqlite3* db, const char* sql, std::string& why)
{
	sqlite3_stmt* st = nullptr;
	if (a.prepare(db, sql, -1, &st, nullptr) != kOk || !st)
	{
		why = std::string("prepare \"") + sql + "\": " + a.errmsg(db);
		if (st) a.finalize(st);
		return false;
	}
	int rc = kRow;
	while (rc == kRow) rc = a.step(st);
	if (rc != kDone) why = std::string("\"") + sql + "\": step returned " + std::to_string(rc) + " (" + a.errmsg(db) + ")";
	a.finalize(st);
	return rc == kDone;
}

double NowMs()
{
	LARGE_INTEGER f, t;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&t);
	return f.QuadPart ? 1000.0 * (double)t.QuadPart / (double)f.QuadPart : 0.0;
}

}  // namespace

bool DbCopyAvailable(std::string& why)
{
	const Api& a = GetApi();
	why = a.why;
	return a.ok;
}

DbCopier::~DbCopier() { Close(); }

void DbCopier::Close()
{
	const Api& a = GetApi();
	if (m_src && a.ok) a.close(static_cast<sqlite3*>(m_src));
	m_src = nullptr;
	m_srcPath.clear();
	m_version = -1;
}

bool DbCopier::Copy(const std::wstring& src, const std::wstring& dst, bool force, DbCopyResult& r)
{
	r = DbCopyResult();
	const double t0 = NowMs();
	const Api& a = GetApi();
	if (!a.ok)
	{
		r.why = a.why;
		return false;
	}
	if (src.empty() || dst.empty())
	{
		r.why = "an empty source or destination path";
		return false;
	}
	if (m_src && m_srcPath != src) Close();
	if (!m_src)
	{
		if (GetFileAttributesW(src.c_str()) == INVALID_FILE_ATTRIBUTES)
		{
			r.why = "the source " + Utf8(src) + " does not exist";
			return false;
		}
		sqlite3* db = nullptr;
		if (a.open(Utf8(src).c_str(), &db, kOpenReadOnly, nullptr) != kOk || !db)
		{
			r.why = "cannot open the source " + Utf8(src) + (db ? std::string(": ") + a.errmsg(db) : std::string());
			if (db) a.close(db);
			return false;
		}
		a.busyTimeout(db, 2000);
		m_src = db;
		m_srcPath = src;
		m_version = -1;
	}
	sqlite3* s = static_cast<sqlite3*>(m_src);
	int64_t v = -1;
	if (!DataVersion(a, s, v, r.why))
	{
		Close();
		return false;
	}
	if (!force && m_version >= 0 && v == m_version)
	{
		r.ok = true;
		r.unchanged = true;
		r.ms = NowMs() - t0;
		return true;
	}
	DeleteFileW(dst.c_str());
	sqlite3* d = nullptr;
	if (a.open(Utf8(dst).c_str(), &d, kOpenReadWrite | kOpenCreate, nullptr) != kOk || !d)
	{
		r.why = "cannot create the copy " + Utf8(dst) + (d ? std::string(": ") + a.errmsg(d) : std::string());
		if (d) a.close(d);
		DeleteFileW(dst.c_str());
		return false;
	}
	// Ticket 0631 take 2 (2026-11-24): the last backup step commits the copy while it still holds the SOURCE's read lock
	// (SQLite's backup.c ends the source's read transaction after the destination's commit). With the defaults
	// (journal_mode DELETE, synchronous FULL) that commit creates, writes, flushes and deletes a journal and flushes the
	// copy, and on a busy disk it held the source's lock past the writer's 60 ms busy budget: the writer's COMMIT failed
	// with SQLITE_BUSY (the db-copy test's writer failures; the DLL's own writes in the product). The copy is a scratch
	// file, checked by quick_check below and deleted on any failure, so it needs neither: no journal, no flushes.
	if (!ExecOne(a, d, "PRAGMA journal_mode = OFF", r.why) || !ExecOne(a, d, "PRAGMA synchronous = OFF", r.why))
	{
		r.why = "the copy's journal and sync settings: " + r.why;
		a.close(d);
		DeleteFileW(dst.c_str());
		return false;
	}
	sqlite3_backup* b = a.backupInit(d, "main", s, "main");
	if (!b)
	{
		r.why = std::string("sqlite3_backup_init: ") + a.errmsg(d);
		a.close(d);
		DeleteFileW(dst.c_str());
		return false;
	}
	int rc = kOk;
	for (;;)
	{
		const double ts = NowMs();
		rc = a.backupStep(b, 256);
		const double tsd = NowMs() - ts;
		if (tsd > r.stepMsMax) r.stepMsMax = tsd;
		if (rc == kDone) break;
		if (rc == kOk) continue;
		if ((rc == kBusy || rc == kLocked) && r.busySteps < 20000)
		{
			++r.busySteps;
			Sleep(1);
			continue;
		}
		break;
	}
	r.pages = a.backupPagecount(b);
	const int frc = a.backupFinish(b);
	const bool copied = rc == kDone && frc == kOk;
	if (!copied)
		r.why = "the backup ended with " + std::to_string(rc) + " / " + std::to_string(frc) + " after " +
		        std::to_string(r.busySteps) + " busy steps: " + a.errmsg(d);
	a.close(d);
	if (!copied)
	{
		DeleteFileW(dst.c_str());
		return false;
	}
	std::string why;
	if (!DbCheckCopy(dst, nullptr, nullptr, why))
	{
		r.why = "the copy failed its check: " + why;
		DeleteFileW(dst.c_str());
		return false;
	}
	m_version = v;
	r.ok = true;
	r.ms = NowMs() - t0;
	return true;
}

bool DbCheckCopy(const std::wstring& path, const char* query, std::string* firstValue, std::string& why)
{
	const Api& a = GetApi();
	if (!a.ok)
	{
		why = a.why;
		return false;
	}
	sqlite3* db = nullptr;
	const std::string uri = ImmutableUri(Utf8(path));
	if (a.open(uri.c_str(), &db, kOpenReadOnly | kOpenUri, nullptr) != kOk || !db)
	{
		why = "cannot open " + uri + (db ? std::string(": ") + a.errmsg(db) : std::string());
		if (db) a.close(db);
		return false;
	}
	std::string qc;
	bool ok = QueryFirst(a, db, "PRAGMA quick_check", qc, why);
	if (ok && qc != "ok")
	{
		why = "quick_check: " + qc;
		ok = false;
	}
	if (ok && query)
	{
		std::string v;
		ok = QueryFirst(a, db, query, v, why);
		if (ok && firstValue) *firstValue = v;
	}
	a.close(db);
	return ok;
}

}  // namespace SrvFeed
