// test_db_write_cost.cpp - issue 0129: one kRtE_DbOps batch of 512 writes, made on the server's copy of the game
// database and replayed on a stand-in for the engine's database, timed against a stated limit.
// Copyright (C) 2026 the c5-64bit-patch authors. GPL-3.0-or-later.
//
// Built and run by server/tests/db/run_db_write_test.py (x64 clang-cl, linked against CvGameDatabase_x64.lib, run beside
// CvGameDatabase_x64.dll and lua51_x64.dll of the pinned build). That script extracts the block "0129 COPY TUNING" of
// server/src/remote/srv_remote_db.cpp into srv_db_tune.gen.h, so the tuning tested here is the server's own text.
//
//   test_db_write_cost <server copy.db> <engine copy.db> <writes> <limit s> tuned|untuned
//
// 1. Opens the server's copy as the server does (Connection::Open, OPEN_READWRITE) and, with `tuned`, applies
//    vpsrvdb::TuneCopy as SrvDbSetGame does.
// 2. Makes <writes> writes on it in the form of the DLL's DatabaseRemapper (CvDllDatabaseUtility.cpp, the writes 9047's
//    server.log lists first): `UPDATE <table> SET ID = ? WHERE ID = ?` prepared with Connection::Execute(Results&),
//    Bind(1, new), Bind(2, old), Results::Execute, each in autocommit; each recorded as the server records it
//    (vpdb::Op, kOpStmtExecute) and serialised as kRtE_DbOps carries it.
// 3. Replays the batch as the proxy does (vpdb::Op::Parse, vpdb::Apply) on the engine copy, which stands in for the
//    engine's in-memory database (opened with its journal in memory and no flush), and checks every result and that
//    both databases have equal fingerprints (vpdb::Compute).
// 4. PASS when the server's writes plus the replay take at most <limit s>. In `untuned` mode the writes stop as soon as
//    the limit is passed (the negative control must fail, and quickly).
// Prints one line per stage and "RESULT PASS|FAIL ..."; exit 0 pass, 1 fail, 2 set-up error.
#define CvGameDatabaseAPI __declspec(dllimport)
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <string>
#include <vector>
#include "vp_dbfp.h"
#include "srv_db_tune.gen.h"

using namespace Database;

static double Now()
{
	static LARGE_INTEGER f = {};
	if (!f.QuadPart) QueryPerformanceFrequency(&f);
	LARGE_INTEGER c;
	QueryPerformanceCounter(&c);
	return (double)c.QuadPart / (double)f.QuadPart;
}

struct Target
{
	std::string table;
	int id;
};

// Rows with an integer ID in the tables the remapper walks first (by name), until n are found.
static std::vector<Target> PickTargets(Connection& db, size_t n)
{
	std::vector<std::string> tables;
	{
		Results r;
		if (db.Execute(r, "SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%' ORDER BY name"))
			while (r.Step())
				tables.push_back(r.GetText(0) ? r.GetText(0) : "");
	}
	std::vector<Target> out;
	for (size_t t = 0; t < tables.size() && out.size() < n; ++t)
	{
		bool hasId = false;
		{
			Results c;
			if (db.Execute(c, ("PRAGMA table_info(" + vpdb::Quote(tables[t]) + ")").c_str()))
				while (c.Step())
					if (c.GetText(1) && strcmp(c.GetText(1), "ID") == 0 && c.GetText(2) && _stricmp(c.GetText(2), "integer") == 0)
						hasId = true;
		}
		if (!hasId)
			continue;
		Results r;
		if (!db.Execute(r, ("SELECT ID FROM " + vpdb::Quote(tables[t]) + " ORDER BY ID").c_str()))
			continue;
		while (r.Step() && out.size() < n)
		{
			Target x = {tables[t], r.GetInt(0)};
			out.push_back(x);
		}
	}
	return out;
}

static std::string Pragmas(const Connection& db)
{
	std::string jm;
	int sync = -1;
	{
		Results r;
		if (db.Execute(r, "PRAGMA main.journal_mode") && r.Step()) jm = r.GetText(0) ? r.GetText(0) : "";
	}
	{
		Results r;
		if (db.Execute(r, "PRAGMA main.synchronous") && r.Step()) sync = r.GetInt(0);
	}
	char sz[96];
	sprintf_s(sz, "synchronous %d, journal_mode %s", sync, jm.c_str());
	return sz;
}

int main(int argc, char** argv)
{
	if (argc != 6)
	{
		printf("usage: test_db_write_cost <server copy.db> <engine copy.db> <writes> <limit s> tuned|untuned\n");
		return 2;
	}
	const size_t nWrites = (size_t)atoi(argv[3]);
	const double limit = atof(argv[4]);
	const bool tuned = strcmp(argv[5], "tuned") == 0;

	Connection srv, eng;
	if (!srv.Open(argv[1], OPEN_READWRITE) || !eng.Open(argv[2], OPEN_READWRITE))
	{
		printf("SETUP cannot open %s or %s\n", argv[1], argv[2]);
		return 2;
	}
	// The engine's database under the real EXE is in memory: its stand-in neither flushes nor journals on disk.
	eng.Execute("PRAGMA main.synchronous = OFF");
	{
		Results r;
		eng.Execute(r, "PRAGMA main.journal_mode = MEMORY");
		r.Step();
	}
	if (tuned)
	{
		std::string how;
		const bool b = vpsrvdb::TuneCopy(srv, how);
		printf("TUNE %s: %s\n", b ? "applied" : "NOT applied", how.c_str());
		if (!b)
		{
			printf("RESULT FAIL the server's tuning did not apply\n");
			return 1;
		}
	}
	printf("SERVER COPY %s (%s)\n", argv[1], Pragmas(srv).c_str());
	printf("ENGINE STAND-IN %s (%s)\n", argv[2], Pragmas(eng).c_str());

	const std::vector<Target> targets = PickTargets(srv, nWrites);
	if (targets.size() < nWrites)
	{
		printf("SETUP only %zu rows with an integer ID found, %zu wanted\n", targets.size(), nWrites);
		return 2;
	}

	// 2. the DLL's writes on the server's copy, recorded as the server records them
	std::vector<std::vector<uint8_t> > batch;
	std::vector<uint32_t> serverResults;
	std::vector<double> each;
	const double t0 = Now();
	bool stoppedEarly = false;
	for (size_t i = 0; i < targets.size(); ++i)
	{
		vpdb::Op op;
		op.kind = vpdb::kOpStmtExecute;
		op.sql = "UPDATE " + targets[i].table + " SET ID = ? WHERE ID = ?";
		vpdb::Bind b1 = {1, 1u, targets[i].id + 1000000, 0.0, std::string()};
		vpdb::Bind b2 = {2, 1u, targets[i].id, 0.0, std::string()};
		op.binds.push_back(b1);
		op.binds.push_back(b2);
		const double w0 = Now();
		bool ok = false;
		{
			Results r;
			if (srv.Execute(r, op.sql.c_str()))
			{
				r.Bind(1, (int)b1.i);
				r.Bind(2, (int)b2.i);
				ok = r.Execute();
			}
		}
		each.push_back(Now() - w0);
		serverResults.push_back(ok ? 1u : 0u);
		batch.push_back(op.Serialize());
		if (!tuned && Now() - t0 > limit)
		{
			stoppedEarly = true;
			break;
		}
	}
	const double tServer = Now() - t0;
	std::vector<double> sorted = each;
	std::sort(sorted.begin(), sorted.end());
	const double med = sorted[sorted.size() / 2], p90 = sorted[(sorted.size() * 9) / 10], mx = sorted.back();
	printf("SERVER WRITES %zu in %.4f s: %.3f ms a write (median %.3f ms, p90 %.3f ms, max %.3f ms)%s\n", each.size(),
	       tServer, 1000.0 * tServer / each.size(), 1000.0 * med, 1000.0 * p90, 1000.0 * mx,
	       stoppedEarly ? " - stopped at the limit" : "");
	if (stoppedEarly)
	{
		printf("RESULT FAIL %zu of %zu writes took %.3f s, over the limit %.3f s (projected %.1f s for the batch)\n",
		       each.size(), nWrites, tServer, limit, tServer / each.size() * nWrites);
		return 1;
	}

	// 3. the proxy's side of kRtE_DbOps
	const double t1 = Now();
	size_t nMismatch = 0;
	for (size_t i = 0; i < batch.size(); ++i)
	{
		vpdb::Op op;
		if (!op.Parse(&batch[i][0], batch[i].size()))
		{
			printf("RESULT FAIL write %zu does not parse\n", i + 1);
			return 1;
		}
		if (vpdb::Apply(eng, op) != serverResults[i] && ++nMismatch <= 5)
			printf("MISMATCH write %zu: %s\n", i + 1, op.sql.c_str());
	}
	const double tApply = Now() - t1;
	printf("ENGINE APPLY %zu in %.4f s, %zu results differ\n", batch.size(), tApply, nMismatch);

	const vpdb::Fingerprint a = vpdb::Compute(srv), b = vpdb::Compute(eng);
	std::vector<std::string> lines;
	const size_t nDiff = vpdb::Compare(a, b, lines, 5, "server", "engine");
	printf("FINGERPRINT server %016llx engine %016llx: %s\n", (unsigned long long)a.Digest(), (unsigned long long)b.Digest(),
	       nDiff == 0 ? "IDENTICAL" : "DIFFERENT");
	for (size_t i = 0; i < lines.size(); ++i)
		printf("  %s\n", lines[i].c_str());

	const double total = tServer + tApply;
	const bool pass = nMismatch == 0 && nDiff == 0 && total <= limit;
	printf("RESULT %s batch of %zu writes: %.4f s (server %.4f s + engine apply %.4f s), limit %.3f s%s%s\n",
	       pass ? "PASS" : "FAIL", batch.size(), total, tServer, tApply, limit, nMismatch ? ", results differ" : "",
	       nDiff ? ", databases differ" : "");
	return pass ? 0 : 1;
}
