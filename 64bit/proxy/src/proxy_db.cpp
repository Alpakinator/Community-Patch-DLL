// proxy_db.cpp - the engine's game database on the proxy side (issue 2504; proxy-design 11.7). Hand-written.
// Copyright (C) 2026 the c5-64bit-patch authors. GPL-3.0-or-later.
//
// SetGameDatabase(Database::Connection*): the connection cannot cross (no virtual methods; the DLL calls the closed
// library with it). The proxy asks the engine's connection itself, through the EXE's own database library
// (CvGameDatabaseWin32Final Release.dll, linked like the real DLL), which database it is:
//   1. vpproxy.ini db= set: that file (explicit override; the engine's database_list is still logged);
//   2. PRAGMA database_list names a readable file for "main": that file (the server copies it);
//   3. otherwise (in memory, temporary, unreadable): Database::BinaryIO::Save of the connection into a temporary file
//      (a page copy, vp_dbfp.h), which the server copies; the file is deleted after the server answered.
// In every case the proxy sends the fingerprint of the engine's database (vp_dbfp.h) and the server compares it with
// its copy (srv_remote_main.cpp): a difference fails the run (exit 4 under fail_on_unexpected).
// The DLL's writes reach the engine's database through the served calls below (the server records them,
// srv_remote_db.cpp):
//   kRtE_DbOps          apply vpdb::Op writes, in order, on the engine's connection; return each member's result;
//   kRtE_DbFingerprint  the fingerprint of the engine's database (the server compares it after each
//                       ICvDLLDatabaseUtility1 call).
#include "proxy_rt_internal.h"
#include "proxy_ifaces.gen.h"
#define VPDB_WITH_BINARYIO
#include "vp_dbfp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace ProxyRtInt;

namespace
{
Database::Connection* g_db = NULL;  // the engine's connection (SetGameDatabase), NULL before / after
long g_nOps = 0, g_nOpFails = 0;
// Fault injection for the negative check of the identity checks (issue 2504): VPPROXY_TEST_SKIP_DB_OP=N:R in the
// environment makes the proxy skip the N-th write (1-based) and report result R for it. Logged when set.
long g_skipOp = 0;
uint32_t g_skipResult = 0;
struct SkipInit
{
	SkipInit()
	{
		char v[64];
		const DWORD n = GetEnvironmentVariableA("VPPROXY_TEST_SKIP_DB_OP", v, sizeof(v));
		if (n > 0 && n < sizeof(v))
		{
			unsigned a = 0, b = 0;
			if (sscanf_s(v, "%u:%u", &a, &b) >= 1)
			{
				g_skipOp = (long)a;
				g_skipResult = b;
			}
		}
	}
} g_skipInit;
// Fault injection for the negative checks of the sync (issue 2504 follow-up): VPPROXY_TEST_ENGINE_SQL="<when>|<sql>"
// makes the proxy run <sql> on the engine's connection once, just before the fingerprint of the check named <when>
// (e.g. "after PerformDatabasePostProcessing"), as if the engine had changed its own database.
std::string g_testWhen, g_testSql;
struct TestSqlInit
{
	TestSqlInit()
	{
		char v[1024];
		const DWORD n = GetEnvironmentVariableA("VPPROXY_TEST_ENGINE_SQL", v, sizeof(v));
		if (n > 0 && n < sizeof(v))
		{
			const std::string s(v, n);
			const size_t bar = s.find('|');
			if (bar != std::string::npos)
			{
				g_testWhen = s.substr(0, bar);
				g_testSql = s.substr(bar + 1);
			}
		}
	}
} g_testSqlInit;
long g_nBatches = 0;
double g_applySeconds = 0.0;

const char* TempSnapshotPath()
{
	static char s_path[MAX_PATH + 64];
	static int s_n = 0;
	char dir[MAX_PATH];
	if (!GetTempPathA(MAX_PATH, dir)) strcpy_s(dir, ".\\");
	sprintf_s(s_path, "%svpproxy-%lu-engine-db-%d.db", dir, GetCurrentProcessId(), ++s_n);
	return s_path;
}

bool ServeOps(vpipc::Reader& args0, vpipc::Writer& ret, vpipc::Channel& ch)
{
	vpipc::Reader args;
	uint8_t* buf = CopyRest(args0, args);
	if (!g_db)
	{
		free(buf);
		return ProxyRt::DispatchFail(ch, ret, vpw::kStatusFailed, "DbOps: the engine passed no database (SetGameDatabase)");
	}
	const double t0 = Now();
	const uint32_t n = args.u32();
	std::vector<uint32_t> results;
	for (uint32_t i = 0; i < n && args.ok(); ++i)
	{
		uint32_t len = 0;
		const uint8_t* p = args.bytes(&len);
		vpdb::Op op;
		if (!args.ok() || !op.Parse(p, len))
		{
			free(buf);
			return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "DbOps: bad op");
		}
		const long k = ++g_nOps;
		uint32_t r;
		if (k == g_skipOp)
		{
			r = g_skipResult;  // fault injection (VPPROXY_TEST_SKIP_DB_OP): the write is NOT applied
			Log("DbOps #%ld: TEST: VPPROXY_TEST_SKIP_DB_OP: %s %.200s NOT applied, result %u reported", k,
			    vpdb::OpName(op.kind), op.sql.c_str(), r);
		}
		else
			r = vpdb::Apply(*g_db, op);
		results.push_back(r);
		if (r == 3u || r == 4u) ++g_nOpFails;
		if (k <= 40 || (k % 1000) == 0 || r == 3u || r == 4u)
			Log("DbOps #%ld: %s %.200s (%u bindings): result %u", k, vpdb::OpName(op.kind), op.sql.c_str(),
			    (unsigned)op.binds.size(), r);
	}
	const bool ok = args.ok();
	free(buf);
	if (!ok) return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "DbOps: bad arguments");
	const double dt = Now() - t0;
	g_applySeconds += dt;
	++g_nBatches;
	if (n > 1 || dt > 0.05)
		Log("DbOps batch #%ld: %u ops applied on the engine's connection in %.3f s (%.3f ms per op; %ld ops, %.3f s in all)",
		    g_nBatches, n, dt, n ? dt * 1000.0 / n : 0.0, g_nOps, g_applySeconds);
	ret.u32((uint32_t)results.size());
	for (size_t i = 0; i < results.size(); ++i) ret.u32(results[i]);
	ret.f64(dt);
	return true;
}

// kRtE_DbSnapshot: the engine's database into the file the server names (BinaryIO::Save, a page copy).
bool ServeSnapshot(vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	const char* p = args.cstr();
	if (!args.ok() || !p) return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "DbSnapshot: bad arguments");
	const std::string path = p;
	const double t0 = Now();
	const bool ok = g_db && vpdb::Snapshot(*g_db, path.c_str());
	Log("DbSnapshot to %s: %s (%.3f s)", path.c_str(), ok ? "ok" : "FAILED", Now() - t0);
	ret.u32(ok ? 1u : 0u);
	ret.f64(Now() - t0);
	return true;
}

bool ServeFingerprint(vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	const char* when = args.cstr();
	if (!args.ok()) return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "DbFingerprint: bad arguments");
	const std::string w = when ? when : "";
	vpdb::Fingerprint fp;
	if (g_db && !g_testSql.empty() && w == g_testWhen)
	{
		// fault injection: the engine changes its own database before this check (VPPROXY_TEST_ENGINE_SQL)
		const bool b = g_db->Execute(g_testSql.c_str());
		Log("DbFingerprint (%s): TEST: VPPROXY_TEST_ENGINE_SQL executed on the engine's connection: %s: %s", w.c_str(),
		    g_testSql.c_str(), b ? "ok" : "FAILED");
		g_testSql.clear();
	}
	if (g_db)
	{
		const double t0 = Now();
		fp = vpdb::Compute(*g_db);
		Log("DbFingerprint (%s): %u tables, %llu rows, digest %016llx (%.3f s; %ld ops applied so far)", w.c_str(),
		    (unsigned)fp.tables.size(), (unsigned long long)fp.TotalRows(), (unsigned long long)fp.Digest(), Now() - t0, g_nOps);
	}
	else
		fp.error = "the engine passed no database";
	const std::vector<uint8_t> b = fp.Serialize();
	ret.bytes(b.empty() ? NULL : &b[0], (uint32_t)b.size());
	return true;
}

// Issue 2802: the file the engine has attached to its game database as `Localization` ("" if none). The EXE attaches
// its Localization-Merged.db (PRAGMA database_list, live 2026-11-10); the DLL's GetDiploResponse needs it, so the
// server attaches the same file to its copy. The 32-bit headless host attaches nothing.
// Test (issue 2802): VPPROXY_TEST_ATTACH_LOC=<file> makes the proxy attach <file> as Localization to the engine's
// connection at SetGameDatabase, as the EXE has it, so the headless host (which attaches nothing) runs the EXE's path.
void TestAttachLocalization(Database::Connection& db)
{
	char v[MAX_PATH * 2];
	const DWORD n = GetEnvironmentVariableA("VPPROXY_TEST_ATTACH_LOC", v, sizeof(v));
	if (n == 0 || n >= sizeof(v)) return;
	std::string q = "ATTACH DATABASE '";
	for (const char* c = v; *c; ++c)
	{
		q += *c;
		if (*c == '\'') q += '\'';
	}
	q += "' AS Localization";
	const bool ok = db.Execute(q.c_str());
	Log("SetGameDatabase: TEST: VPPROXY_TEST_ATTACH_LOC: %s on the engine's connection: %s", q.c_str(),
	    ok ? "ok" : (db.ErrorMessage() ? db.ErrorMessage() : "FAILED"));
}

std::string AttachedLocalization(Database::Connection& db)
{
	std::string file;
	Database::Results r;
	if (db.Execute(r, "PRAGMA database_list"))
		while (r.Step())
		{
			const char* name = r.GetText(1);
			const char* f = r.GetText(2);
			if (name && _stricmp(name, "Localization") == 0)
				file = f ? f : "";
		}
	return file;
}
}  // namespace

// Served kRtE_Db* methods (called from LuaServe in proxy_lua.cpp for every kRtE_* it does not handle).
bool ProxyDbServe(uint32_t method, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	switch (method)
	{
	case vpw::kRtE_DbOps: return ServeOps(args, ret, ch);
	case vpw::kRtE_DbFingerprint: return ServeFingerprint(args, ret, ch);
	case vpw::kRtE_DbSnapshot: return ServeSnapshot(args, ret, ch);
	default: return false;
	}
}

// SetGameDatabase(Database::Connection*). CALL: self, u32 flags (1 a database, 2 its file is read-only, 4 the engine
// has a database attached as Localization), str path, bytes fingerprint, str how, and with flag 4: str the Localization
// file (issue 2802). RETURN: u32 ok, str message.
void ProxyHook_ICvGameContext3_SetGameDatabase(Proxy_ICvGameContext3& self, Database::Connection* pGameDatabase)
{
	g_db = pGameDatabase;
	if (g_skipOp)
		Log("SetGameDatabase: TEST: VPPROXY_TEST_SKIP_DB_OP is set: write #%ld will not be applied (result %u reported)",
		    g_skipOp, g_skipResult);
	if (!g_testSql.empty())
		Log("SetGameDatabase: TEST: VPPROXY_TEST_ENGINE_SQL is set: before the check '%s': %s", g_testWhen.c_str(),
		    g_testSql.c_str());
	std::string path, how, snapshot, loc;
	std::vector<uint8_t> fpBytes;
	uint32_t flags = pGameDatabase ? 1u : 0u;  // 1 = a database, 2 = its file is read-only
	if (pGameDatabase)
	{
		const double t0 = Now();
		std::string list;
		TestAttachLocalization(*pGameDatabase);
		const std::string version = vpdb::Version(*pGameDatabase);
		const std::string main = vpdb::MainFile(*pGameDatabase, list);
		const char* ini = DbPath();
		if (ini && *ini)
		{
			path = ini;
			how = "vpproxy.ini db= (explicit override)";
		}
		else if (!main.empty() && GetFileAttributesA(main.c_str()) != INVALID_FILE_ATTRIBUTES)
		{
			path = main;
			how = "the engine's database file (PRAGMA database_list)";
		}
		else
		{
			snapshot = TempSnapshotPath();
			if (vpdb::Snapshot(*pGameDatabase, snapshot.c_str()))
			{
				path = snapshot;
				how = main.empty() ? "in-memory database: BinaryIO::Save snapshot" : "database file not readable: BinaryIO::Save snapshot";
			}
			else
			{
				how = "FAILED: BinaryIO::Save of the engine's database to " + snapshot;
				snapshot.clear();
			}
		}
		// A read-only database file (the 32-bit headless host copies the read-only pinned file and keeps the attribute):
		// the DLL's writes fail on it in the EXE, so the server opens its copy read-only too and they fail there alike.
		const DWORD attr = main.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesA(main.c_str());
		if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY))
		{
			flags |= 2u;
			how += "; the engine's database file is READ-ONLY (the DLL's writes fail on it; the server's copy is opened read-only)";
		}
		loc = AttachedLocalization(*pGameDatabase);
		if (!loc.empty()) flags |= 4u;
		const vpdb::Fingerprint fp = vpdb::Compute(*pGameDatabase);
		fpBytes = fp.Serialize();
		Log("SetGameDatabase(%p): sqlite %s; database_list %s; sending %s: '%s'; fingerprint %s%s%u schema rows, %u tables, "
		    "%llu rows, digest %016llx (%.3f s)",
		    pGameDatabase, version.c_str(), list.c_str(), how.c_str(), path.c_str(), fp.ok ? "" : "FAILED ",
		    fp.ok ? "" : fp.error.c_str(), fp.schemaRows, (unsigned)fp.tables.size(), (unsigned long long)fp.TotalRows(),
		    (unsigned long long)fp.Digest(), Now() - t0);
	}
	else
		Log("SetGameDatabase(NULL)");
	vpipc::Channel& ch = ProxyRt::Chan();
	vpipc::Writer& w = ProxyRt::Begin(ch, kM_ICvGameContext3_SetGameDatabase);
	w.handle(self.ProxyHandle());
	w.u32(flags);
	w.str(path.c_str());
	w.bytes(fpBytes.empty() ? NULL : &fpBytes[0], (uint32_t)fpBytes.size());
	w.str(how.c_str());
	if (flags & 4u) w.str(loc.c_str());
	vpipc::Reader r;
	const bool sent = ProxyRt::End(ch, kM_ICvGameContext3_SetGameDatabase, r);
	if (sent)
	{
		const uint32_t ok = r.u32();
		const char* msg = r.cstr();
		const std::string m = msg ? msg : "";
		ProxyRt::DecodeCheck(kM_ICvGameContext3_SetGameDatabase, r);
		Log("SetGameDatabase(%p): server %s: %s", pGameDatabase, ok ? "ok" : "FAILED", m.c_str());
	}
	if (!snapshot.empty() && !DeleteFileA(snapshot.c_str()))
		Log("SetGameDatabase: could not delete the snapshot %s (error %lu)", snapshot.c_str(), GetLastError());
	if (!pGameDatabase && g_nOps)
		Log("database: %ld writes of the DLL applied to the engine's database, %ld could not be prepared", g_nOps, g_nOpFails);
}
