// srv_feed_dbcopy.h - a consistent copy of the game database for the render side's unit art (issue 4452, epic 0375;
// MC #2105 Q5). Written 2026-11-15 for this repository (GPL-3.0-or-later).
//
// The server's copy of the game database (<run>/db/Civ5DebugDatabase.<n>.db, made by the SetGameDatabase hook in
// srv_remote_main.cpp) is the file the DLL writes through CvGameDatabase_x64 (SQLite 3.7.17, journal in memory,
// synchronous off: srv_remote_db.cpp "0129 COPY TUNING"). The render side (0041's model library) opens database files
// with `immutable=1`, which is only correct on a file nobody writes. So the render side never opens that file: the
// server makes a separate copy with SQLite's online backup API on a second connection of the system's SQLite
// (winsqlite3.dll, as models/src/art_db.cpp loads it; CvGameDatabase_x64 exports no sqlite3_* function), in steps of 256
// pages. The backup takes the source's read lock only for one step at a time and restarts by itself when another
// connection commits between two steps, so the copy is always one committed state; the DLL's connection waits at most
// one step (its busy handler retries for 60 ms, tools/x64-dll/cvgamedatabase/src/Database.cpp). The last step also
// commits the copy before it lets go of the source's lock, so the copy is written with no journal and no flushes
// (ticket 0631 take 2: with SQLite's defaults that commit held the source past the writer's budget and its COMMIT failed
// with SQLITE_BUSY; unit-server-scene.md, the database copy). Each copy is a new file, never written again after Copy
// returns, which the render side may open with `immutable=1`.
//
// No DLL header here (built and tested alone, server/tests/feed/).
#pragma once
#include <stdint.h>
#include <string>

namespace SrvFeed {

struct DbCopyResult
{
	bool ok = false;
	bool unchanged = false;       // no copy made: the source has not changed since this copier's last copy (data_version)
	std::string why;              // the failure, named (empty when ok)
	int pages = 0;                // pages of the copy
	int busySteps = 0;            // backup steps that found the source locked and were retried
	double ms = 0;                // the time Copy took
	double stepMsMax = 0;         // the longest backup step: the longest time the source's read lock was held (0631)
};

class DbCopier
{
public:
	DbCopier() {}
	~DbCopier();
	// Copies `src` into the new file `dst` (deleted first if it exists) and checks the copy (PRAGMA quick_check on a
	// read-only connection to it). Without `force`, makes no copy when the source is the one of the previous call and
	// its PRAGMA data_version on the copier's kept connection has not changed (r.unchanged). False on any failure.
	bool Copy(const std::wstring& src, const std::wstring& dst, bool force, DbCopyResult& r);
	void Close();   // drops the kept source connection
private:
	DbCopier(const DbCopier&);
	DbCopier& operator=(const DbCopier&);
	void* m_src = nullptr;        // sqlite3* on the source (read-only), kept between calls for data_version
	std::wstring m_srcPath;
	int64_t m_version = -1;
};

// Opens `path` read-only and immutable (as the render side will) and runs PRAGMA quick_check and an optional query
// whose first column of the first row is returned in *firstValue. False with why on any failure.
bool DbCheckCopy(const std::wstring& path, const char* query, std::string* firstValue, std::string& why);

// True when winsqlite3.dll and every function used here could be bound (why names the missing one otherwise).
bool DbCopyAvailable(std::string& why);

}  // namespace SrvFeed
