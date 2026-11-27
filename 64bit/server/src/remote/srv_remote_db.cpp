// srv_remote_db.cpp - the DLL's writes to its game database reach the engine's database (issue 2504; proxy-design 11.7).
// Copyright (C) 2026 the c5-64bit-patch authors. GPL-3.0-or-later.
//
// In the EXE the DLL writes to the engine's own connection (CvDllDatabaseUtility::PerformDatabasePostProcessing:
// DatabaseRemapper's ID updates, Defines from PostDefines, Resources.ResourceUsage, the hard-coded defines;
// CacheGameDatabaseData: CustomMods::prefetchCache's CustomModOptions, Analyze, ClearCountCache), and the engine's Lua
// (GameInfo, GameDefines, VPUI's CustomModOptions) reads the result. In remote mode the DLL writes to the server's copy.
// So the server records every write the DLL makes to the game connection and the proxy applies it to the engine's
// connection at once, in the same order, while the engine is still inside the same engine -> DLL call
// (kRtE_DbOps, nested): the engine sees its database change when it would have changed in-process.
//
// How the writes are seen: the DLL calls CvGameDatabase_x64.dll through its import table; after kRt_Boot loads the DLL
// the server points the DLL's import slots of that library (its own x64 image only; nothing of the EXE) at the hooks
// below. A Results prepared on the game connection (Connection::Execute(Results&, sql)) with a writing statement
// (vpdb::IsWriteSql) is tracked with its bindings (Bind*, BindNULL; Reset clears them, as CvGameDatabase_x64 does);
// each Step / Execute of it is one vpdb::Op (sql + bindings); Connection::Execute(sql) of a writing statement, Analyze,
// Begin/End/CommitTransaction and ClearCountCache are ops as they are. Select*, Release and ~Results end the
// tracking. The proxy applies each op with the same library member (vpdb::Apply) and returns its result; a result that
// differs from the server's is a loud failure. Every import of the database library the hooks do not cover must be on
// the read-only list (kNeutral) or the run fails at boot: a new write path cannot go unseen.
//
// Identity checks (vpdb::Fingerprint: schema + every table's rows, rowids and values): at SetGameDatabase
// (srv_remote_main.cpp: the proxy's fingerprint of the engine's database against the server's copy) and after every
// ICvDLLDatabaseUtility1 call (the hooks below: kRtE_DbFingerprint). A difference: SrvRemote::Fail (result FAILED) and,
// with fail_on_unexpected=1, exit code 4.
// 2504 follow-up: inside those calls the writes go in batches (BatchScope, at most 512 per kRtE_DbOps, flushed before
// any script-system call to the engine and before a check); a check that finds tables the EXE changed itself (the map
// script scan fills MapScripts and its option tables after the boot) pulls them from a snapshot of the engine's
// database, unless the DLL wrote them since the last check (SrvDbCheckEngine).
#include "srv_remote.h"
#include "vp_ids.gen.h"
#include "server_hooks.gen.h"
#include "vp_dbfp.h"
#include <stdio.h>
#include <string>
#include <unordered_map>
#include <vector>

void* SrvPatchImport(HMODULE hModule, const char* szDll, const char* szFunc, void* pHook);

// BEGIN 0129 COPY TUNING (server/tests/db/run_db_write_test.py compiles this block on its own: keep it self-contained)
// Issue 0129: the server's copy of the game database (the SetGameDatabase hook in srv_remote_main.cpp copies the
// engine's database into <run>/db/ and opens that file) is scratch: it is made again from the engine's database at
// every boot and nothing reads it after the process ends. With SQLite's defaults (synchronous FULL, journal DELETE)
// every autocommit write the DLL makes creates, flushes and deletes a rollback journal and flushes the database file:
// 0.18 s a write on the workstation's D: disk (a rotating SATA disk), 4 ms on its NVMe C: [L, 2026-11-13]. The front
// end's mod activation makes about 2,560 such writes (DatabaseRemapper's ID updates): 727 s on 9047's c5h-e (on D:),
// with the EXE blocked in the call. Under the real EXE the engine's own database is in memory. So the copy is opened
// with no flush and its rollback journal in memory (ROLLBACK and savepoints still work; a crash loses only the
// scratch copy). `main.` only: the attached Localization database is the engine's own file.
namespace vpsrvdb
{
inline bool TuneCopy(const Database::Connection& db, std::string& how)
{
	const bool bSync = db.Execute("PRAGMA main.synchronous = OFF");
	std::string jm;
	{
		Database::Results r;
		if(db.Execute(r, "PRAGMA main.journal_mode = MEMORY") && r.Step())
			jm = r.GetText(0) ? r.GetText(0) : "";
	}
	int sync = -1;
	{
		Database::Results r;
		if(db.Execute(r, "PRAGMA main.synchronous") && r.Step())
			sync = r.GetInt(0);
	}
	char sz[160];
	sprintf_s(sz, "synchronous %d, journal_mode %s", sync, jm.empty() ? "?" : jm.c_str());
	how = sz;
	return bSync && sync == 0 && _stricmp(jm.c_str(), "memory") == 0;
}
}  // namespace vpsrvdb
// END 0129 COPY TUNING

namespace
{
using Database::Connection;
using Database::Results;

CRITICAL_SECTION s_cs;
struct InitCs
{
	InitCs() { InitializeCriticalSection(&s_cs); }
} s_initCs;

const Connection* volatile s_game = NULL;  // the connection the DLL got with SetGameDatabase
struct Stmt
{
	std::string sql;
	std::vector<vpdb::Bind> binds;
};
std::unordered_map<const void*, Stmt> s_stmts;  // tracked Results (writing statements on the game connection)
volatile LONG s_nStmts = 0;                     // s_stmts.size(), read without the lock on the fast path

volatile LONG s_nOps = 0, s_nMismatch = 0, s_nLost = 0, s_nChecks = 0, s_nCheckFails = 0;
long s_kinds[9] = {0};
bool s_bInstalled = false;
std::vector<std::string> s_firstOps;

void SetCount() { InterlockedExchange(&s_nStmts, (LONG)s_stmts.size()); }

void Forget(const void* r)
{
	if(!s_nStmts)
		return;
	EnterCriticalSection(&s_cs);
	if(s_stmts.erase(r))
		SetCount();
	LeaveCriticalSection(&s_cs);
}

void Track(const void* r, const char* sql, int len)
{
	EnterCriticalSection(&s_cs);
	Stmt& s = s_stmts[r];
	s.sql = len < 0 ? std::string(sql) : std::string(sql, (size_t)len);
	s.binds.clear();
	SetCount();
	LeaveCriticalSection(&s_cs);
}

bool Tracked(const void* r, Stmt& copy)
{
	if(!s_nStmts)
		return false;
	EnterCriticalSection(&s_cs);
	std::unordered_map<const void*, Stmt>::const_iterator it = s_stmts.find(r);
	const bool b = it != s_stmts.end();
	if(b)
		copy = it->second;
	LeaveCriticalSection(&s_cs);
	return b;
}

void SetBind(const void* r, const vpdb::Bind& b)
{
	if(!s_nStmts)
		return;
	EnterCriticalSection(&s_cs);
	std::unordered_map<const void*, Stmt>::iterator it = s_stmts.find(r);
	if(it != s_stmts.end())
	{
		std::vector<vpdb::Bind>& v = it->second.binds;
		size_t i = 0;
		while(i < v.size() && v[i].idx != b.idx)
			++i;
		if(i < v.size())
			v[i] = b;
		else
			v.push_back(b);
	}
	LeaveCriticalSection(&s_cs);
}

void FailLoud(const char* szWhy)
{
	SrvRemote::Fail(szWhy);
	if(SrvRemote::FailOnUnexpected())
		SrvRemote::ReportAndExit(4, szWhy);
}

// ---- batches (2504 follow-up) ----
// Inside an ICvDLLDatabaseUtility1 call (BatchScope) the writes are collected per service thread and sent as one
// kRtE_DbOps when the batch reaches kBatchMax ops, before any script-system call to the engine (SrvDbFlush, called by
// srv_remote_lua.cpp: the engine's Lua could read its database there), before an identity check and at the end of
// the call. Every op keeps its own result check. Outside such a call each write is sent at once, as before.
// Why: 2,559 single-op round trips took 12 s and 100 s in two live boots (2026-11-10, loaded machine).
const size_t kBatchMax = 512;
struct Pending
{
	std::vector<uint8_t> bytes;
	uint32_t serverResult;
	LONG k;
	std::string text;
};
thread_local int t_batchDepth = 0;
thread_local std::vector<Pending>* t_pending = NULL;
volatile LONG s_nBatches = 0;
double s_rtSeconds = 0.0, s_applySeconds = 0.0;	// round trips and the proxy's apply time, all batches

// Tables the DLL wrote since the last identity check ("*" = a statement whose table is not known).
std::vector<std::string> s_written;
void NoteWritten(const std::string& t)
{
	const std::string key = t.empty() ? std::string("*") : vpdb::detail::Lower(t);
	EnterCriticalSection(&s_cs);
	bool have = false;
	for(size_t i = 0; i < s_written.size() && !have; ++i)
		have = s_written[i] == key;
	if(!have)
		s_written.push_back(key);
	LeaveCriticalSection(&s_cs);
}

void SendOps(std::vector<Pending>& ops)
{
	if(ops.empty())
		return;
	const double t0 = SrvNow();
	vpipc::Channel& ch = SrvRt::Chan();
	vpipc::Writer& w = SrvRt::Begin(ch, vpw::kRtE_DbOps);
	w.u32((uint32_t)ops.size());
	for(size_t i = 0; i < ops.size(); ++i)
		w.bytes(&ops[i].bytes[0], (uint32_t)ops[i].bytes.size());
	vpipc::Reader r;
	if(!SrvRt::End(ch, vpw::kRtE_DbOps, r))
	{
		InterlockedExchangeAdd(&s_nLost, (LONG)ops.size());
		ops.clear();
		FailLoud("database: kRtE_DbOps failed; the engine's database misses writes of the DLL");
		return;
	}
	const uint32_t n = r.u32();
	std::vector<uint32_t> res;
	for(uint32_t i = 0; i < n && r.ok(); ++i)
		res.push_back(r.u32());
	const double apply = r.f64();
	SrvRt::DecodeCheck(vpw::kRtE_DbOps, r);
	const double dt = SrvNow() - t0;
	const LONG b = InterlockedIncrement(&s_nBatches);
	EnterCriticalSection(&s_cs);
	s_rtSeconds += dt;
	s_applySeconds += apply;
	LeaveCriticalSection(&s_cs);
	if(ops.size() > 1 || dt > 0.05)
		SrvLog("database: batch #%ld: %zu writes, round trip %.3f s, of which the proxy applied %.3f s", b, ops.size(), dt, apply);
	for(size_t i = 0; i < ops.size(); ++i)
	{
		const uint32_t er = i < res.size() ? res[i] : 0xFFFFFFFFu;
		if(n == ops.size() && er == ops[i].serverResult)
			continue;
		const LONG m = InterlockedIncrement(&s_nMismatch);
		char why[700];
		sprintf_s(why, "database: write #%ld gave %u on the engine's connection and %u on the server's: %s", ops[i].k, er,
		          ops[i].serverResult, ops[i].text.c_str());
		if(m <= 20)
			SrvLog("%s", why);
		ops.clear();
		FailLoud(why);
		return;
	}
	ops.clear();
}

void FlushPending()
{
	if(t_pending && !t_pending->empty() && SrvRemote::ThreadChannel() >= 0)
		SendOps(*t_pending);
}

// One op: the server already ran it (result serverResult); the proxy applies it to the engine's connection (now, or
// in this thread's batch).
void Replay(const vpdb::Op& op, uint32_t serverResult)
{
	const LONG k = InterlockedIncrement(&s_nOps);
	if(op.kind < 9)
		InterlockedIncrement(&s_kinds[op.kind]);
	if(op.kind == vpdb::kOpExecSql || op.kind == vpdb::kOpStmtStep || op.kind == vpdb::kOpStmtExecute)
		NoteWritten(vpdb::WriteTarget(op.sql.c_str(), op.sql.size()));
	else if(op.kind == vpdb::kOpAnalyze)
		NoteWritten("sqlite_stat1");
	char sz[400];
	sprintf_s(sz, "%s %.300s (%zu bindings): server %u", vpdb::OpName(op.kind), op.sql.c_str(), op.binds.size(), serverResult);
	if(k <= 40)
	{
		SrvLog("database: write #%ld: %s", k, sz);
		EnterCriticalSection(&s_cs);
		s_firstOps.push_back(sz);
		LeaveCriticalSection(&s_cs);
	}
	if(SrvRemote::ThreadChannel() < 0)
	{
		InterlockedIncrement(&s_nLost);
		FailLoud("database: the DLL wrote to the game database on a thread that serves no channel; the write cannot "
		         "reach the engine's database");
		return;
	}
	Pending p;
	p.bytes = op.Serialize();
	p.serverResult = serverResult;
	p.k = k;
	p.text = sz;
	if(!t_pending)
		t_pending = new std::vector<Pending>();
	t_pending->push_back(p);
	if(t_batchDepth == 0 || t_pending->size() >= kBatchMax)
		SendOps(*t_pending);
}

struct BatchScope
{
	BatchScope() { ++t_batchDepth; }
	~BatchScope()
	{
		if(--t_batchDepth == 0)
			FlushPending();
	}
};

void ReplaySimple(uint32_t kind, uint32_t serverResult, const char* sql = NULL, int len = -1)
{
	vpdb::Op op;
	op.kind = kind;
	if(sql)
		op.sql = len < 0 ? std::string(sql) : std::string(sql, (size_t)len);
	Replay(op, serverResult);
}

bool IsGame(const void* c) { return c && c == (const void*)s_game; }
size_t Len(const char* s, int len) { return len < 0 ? strlen(s) : (size_t)len; }

// ---- hooks (x64: a member function is a function with `this` first) -------------------------------------------
typedef bool (*PConnExecR)(const Connection*, Results*, const char*, int);
typedef bool (*PConnExec)(const Connection*, const char*, int);
typedef void (*PConnVoid)(const Connection*);
typedef bool (*PSelAll)(Connection*, Results*, const char*);
typedef bool (*PSelAtSS)(Connection*, Results*, const char*, const char*, const char*);
typedef bool (*PSelAtSI)(Connection*, Results*, const char*, const char*, int);
typedef bool (*PSelAtI)(Connection*, Results*, const char*, int);
typedef bool (*PSelWhere)(Connection*, Results*, const char*, const char*);
typedef bool (*PBindI)(Results*, int, int);
typedef bool (*PBindD)(Results*, int, double);
typedef bool (*PBindF)(Results*, int, float);
typedef bool (*PBindS4)(Results*, int, const char*, int, bool);
typedef bool (*PBindS3)(Results*, int, const char*, bool);
typedef bool (*PBindN)(Results*, int);
typedef bool (*PResBool)(Results*);
typedef void (*PResVoid)(Results*);

PConnExecR o_ExecR;
PConnExec o_Exec;
PConnVoid o_Analyze, o_Begin, o_End, o_Commit, o_ClearCount;
PSelAll o_SelAll;
PSelAtSS o_SelAtSS;
PSelAtSI o_SelAtSI;
PSelAtI o_SelAtI;
PSelWhere o_SelWhere;
PBindI o_BindI;
PBindD o_BindD;
PBindF o_BindF;
PBindS4 o_BindS4;
PBindS3 o_BindS3;
PBindN o_BindN;
PResBool o_Step, o_ResExec, o_Reset;
PResVoid o_Release, o_Dtor;

bool H_ExecR(const Connection* c, Results* r, const char* sql, int len)
{
	const bool b = o_ExecR(c, r, sql, len);
	if(b && IsGame(c) && sql && vpdb::IsWriteSql(sql, Len(sql, len)))
		Track(r, sql, len);
	else
		Forget(r);
	return b;
}
bool H_Exec(const Connection* c, const char* sql, int len)
{
	const bool b = o_Exec(c, sql, len);
	if(IsGame(c) && sql && vpdb::IsWriteSql(sql, Len(sql, len)))
		ReplaySimple(vpdb::kOpExecSql, b ? 1u : 0u, sql, len);
	return b;
}
void H_Analyze(const Connection* c)
{
	o_Analyze(c);
	if(IsGame(c))
		ReplaySimple(vpdb::kOpAnalyze, vpdb::kResultVoid);
}
void H_Begin(const Connection* c)
{
	o_Begin(c);
	if(IsGame(c))
		ReplaySimple(vpdb::kOpBegin, vpdb::kResultVoid);
}
void H_End(const Connection* c)
{
	o_End(c);
	if(IsGame(c))
		ReplaySimple(vpdb::kOpEnd, vpdb::kResultVoid);
}
void H_Commit(const Connection* c)
{
	o_Commit(c);
	if(IsGame(c))
		ReplaySimple(vpdb::kOpCommit, vpdb::kResultVoid);
}
void H_ClearCount(const Connection* c)
{
	o_ClearCount(c);
	if(IsGame(c))
		ReplaySimple(vpdb::kOpClearCountCache, vpdb::kResultVoid);
}
bool H_SelAll(Connection* c, Results* r, const char* t) { Forget(r); return o_SelAll(c, r, t); }
bool H_SelAtSS(Connection* c, Results* r, const char* t, const char* k, const char* v) { Forget(r); return o_SelAtSS(c, r, t, k, v); }
bool H_SelAtSI(Connection* c, Results* r, const char* t, const char* k, int v) { Forget(r); return o_SelAtSI(c, r, t, k, v); }
bool H_SelAtI(Connection* c, Results* r, const char* t, int v) { Forget(r); return o_SelAtI(c, r, t, v); }
bool H_SelWhere(Connection* c, Results* r, const char* t, const char* w) { Forget(r); return o_SelWhere(c, r, t, w); }

bool H_BindI(Results* r, int idx, int v)
{
	const bool b = o_BindI(r, idx, v);
	vpdb::Bind x = {idx, 1u, v, 0.0, std::string()};
	SetBind(r, x);
	return b;
}
bool H_BindD(Results* r, int idx, double v)
{
	const bool b = o_BindD(r, idx, v);
	vpdb::Bind x = {idx, 2u, 0, v, std::string()};
	SetBind(r, x);
	return b;
}
// Ticket 0752: Results::Bind(int, float). A DLL built without whole-program optimisation (debug, fast) imports it from
// SqliteLogger::Statement::bind(float), which release's LTO drops as unused; without this hook every such server
// failed at boot ("the write recording does not cover"). The library binds a float as sqlite3_bind_double of the
// value widened to double, so it is recorded as a double bind (type 2) and replayed through Bind(int, double): the
// widening is exact, the engine's row gets the same value.
bool H_BindF(Results* r, int idx, float v)
{
	const bool b = o_BindF(r, idx, v);
	vpdb::Bind x = {idx, 2u, 0, (double)v, std::string()};
	SetBind(r, x);
	return b;
}
void BindText(Results* r, int idx, const char* s, int len)
{
	if(!s_nStmts)
		return;
	vpdb::Bind x = {idx, s ? 3u : 0u, 0, 0.0, s ? (len < 0 ? std::string(s) : std::string(s, (size_t)len)) : std::string()};
	SetBind(r, x);
}
bool H_BindS4(Results* r, int idx, const char* s, int len, bool copy)
{
	const bool b = o_BindS4(r, idx, s, len, copy);
	BindText(r, idx, s, len);
	return b;
}
bool H_BindS3(Results* r, int idx, const char* s, bool copy)
{
	const bool b = o_BindS3(r, idx, s, copy);
	BindText(r, idx, s, -1);
	return b;
}
bool H_BindN(Results* r, int idx)
{
	const bool b = o_BindN(r, idx);
	vpdb::Bind x = {idx, 0u, 0, 0.0, std::string()};
	SetBind(r, x);
	return b;
}
bool H_Reset(Results* r)
{
	const bool b = o_Reset(r);
	if(s_nStmts)
	{
		EnterCriticalSection(&s_cs);
		std::unordered_map<const void*, Stmt>::iterator it = s_stmts.find(r);
		if(it != s_stmts.end())
			it->second.binds.clear();  // CvGameDatabase_x64 Results::Reset: sqlite3_reset + sqlite3_clear_bindings
		LeaveCriticalSection(&s_cs);
	}
	return b;
}
bool StepOrExecute(Results* r, PResBool orig, uint32_t kind)
{
	const bool b = orig(r);
	Stmt s;
	if(Tracked(r, s))
	{
		vpdb::Op op;
		op.kind = kind;
		op.sql.swap(s.sql);
		op.binds.swap(s.binds);
		Replay(op, b ? 1u : 0u);
	}
	return b;
}
bool H_Step(Results* r) { return StepOrExecute(r, o_Step, vpdb::kOpStmtStep); }
bool H_ResExec(Results* r) { return StepOrExecute(r, o_ResExec, vpdb::kOpStmtExecute); }
void H_Release(Results* r) { Forget(r); o_Release(r); }
void H_Dtor(Results* r) { Forget(r); o_Dtor(r); }

struct HookDef
{
	const char* name;
	void* hook;
	void** orig;
};
#define HK(n, h, o) {n, (void*)&h, (void**)&o}
const HookDef kHooks[] = {
	HK("?Execute@Connection@Database@@QEBA_NAEAVResults@2@PEBDH@Z", H_ExecR, o_ExecR),
	HK("?Execute@Connection@Database@@QEBA_NPEBDH@Z", H_Exec, o_Exec),
	HK("?Analyze@Connection@Database@@QEBAXXZ", H_Analyze, o_Analyze),
	HK("?BeginTransaction@Connection@Database@@QEAAXXZ", H_Begin, o_Begin),
	HK("?EndTransaction@Connection@Database@@QEAAXXZ", H_End, o_End),
	HK("?CommitTransaction@Connection@Database@@QEAAXXZ", H_Commit, o_Commit),
	HK("?ClearCountCache@Connection@Database@@QEAAXXZ", H_ClearCount, o_ClearCount),
	HK("?SelectAll@Connection@Database@@QEAA_NAEAVResults@2@PEBD@Z", H_SelAll, o_SelAll),
	HK("?SelectAt@Connection@Database@@QEAA_NAEAVResults@2@PEBD11@Z", H_SelAtSS, o_SelAtSS),
	HK("?SelectAt@Connection@Database@@QEAA_NAEAVResults@2@PEBD1H@Z", H_SelAtSI, o_SelAtSI),
	HK("?SelectAt@Connection@Database@@QEAA_NAEAVResults@2@PEBDH@Z", H_SelAtI, o_SelAtI),
	HK("?SelectWhere@Connection@Database@@QEAA_NAEAVResults@2@PEBD1@Z", H_SelWhere, o_SelWhere),
	HK("?Bind@Results@Database@@QEAA_NHH@Z", H_BindI, o_BindI),
	HK("?Bind@Results@Database@@QEAA_NHN@Z", H_BindD, o_BindD),
	HK("?Bind@Results@Database@@QEAA_NHM@Z", H_BindF, o_BindF),   // ticket 0752: debug and fast DLLs import it
	HK("?Bind@Results@Database@@QEAA_NHPEBDH_N@Z", H_BindS4, o_BindS4),
	HK("?Bind@Results@Database@@QEAA_NHPEBD_N@Z", H_BindS3, o_BindS3),
	HK("?BindNULL@Results@Database@@QEAA_NH@Z", H_BindN, o_BindN),
	HK("?Step@Results@Database@@QEAA_NXZ", H_Step, o_Step),
	HK("?Execute@Results@Database@@QEAA_NXZ", H_ResExec, o_ResExec),
	HK("?Reset@Results@Database@@QEAA_NXZ", H_Reset, o_Reset),
	HK("?Release@Results@Database@@QEAAXXZ", H_Release, o_Release),
	HK("??1Results@Database@@UEAA@XZ", H_Dtor, o_Dtor),
};
#undef HK
// Imports of the database library that neither write nor re-target a Results (the DLL at the pin imports these).
const char* const kNeutral[] = {
	"??0Connection@Database@@QEAA@XZ", "??0Results@Database@@QEAA@PEBD@Z", "??0SingleResult@Database@@QEAA@PEBD@Z",
	"??1Connection@Database@@QEAA@XZ", "??2Results@Database@@SAPEAX_K@Z", "??3Results@Database@@SAXPEAX@Z",
	"?CalculateMemoryStats@Connection@Database@@QEAAPEBDXZ", "?Close@Connection@Database@@QEAAXXZ",
	"?ColumnPosition@Results@Database@@QEAAHPEBD@Z", "?Count@Connection@Database@@QEAAHPEBD_N@Z",
	"?ErrorMessage@Connection@Database@@QEBAPEBDXZ", "?GetBool@Results@Database@@QEAA_NH@Z",
	"?GetBool@Results@Database@@QEAA_NPEBD@Z", "?GetFloat@Results@Database@@QEAAMH@Z",
	"?GetFloat@Results@Database@@QEAAMPEBD@Z", "?GetInt@Results@Database@@QEAAHH@Z", "?GetInt@Results@Database@@QEAAHPEBD@Z",
	"?GetText@Results@Database@@QEAAPEBDH@Z", "?GetText@Results@Database@@QEAAPEBDPEBD@Z",
	"?GetValue@Results@Database@@QEAAXPEBDAEAH@Z", "?HasColumn@Results@Database@@QEAA_NPEBD@Z",
	"?Open@Connection@Database@@QEAA_NPEBDH@Z", "?ValidateFKConstraints@Connection@Database@@QEBA_N_N@Z",
	// read-only accessors of the same library the DLL may start to import:
	"?ColumnCount@Results@Database@@QEBAHXZ", "?ColumnName@Results@Database@@QEAAPEBDH@Z",
	"?ColumnType@Results@Database@@QEAA?AW4ColumnTypes@2@H@Z", "?GetInt64@Results@Database@@QEAA_JH@Z",
	"?GetDouble@Results@Database@@QEAANH@Z", "?ErrorCode@Connection@Database@@QEBAHXZ",
};

// Every import the DLL has from szDll, by name.
std::vector<std::string> ImportsFrom(HMODULE hModule, const char* szDll)
{
	std::vector<std::string> names;
	BYTE* base = reinterpret_cast<BYTE*>(hModule);
	const IMAGE_NT_HEADERS* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
	const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if(!dir.VirtualAddress)
		return names;
	for(const IMAGE_IMPORT_DESCRIPTOR* d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name; ++d)
	{
		if(_stricmp(reinterpret_cast<const char*>(base + d->Name), szDll) != 0)
			continue;
		const IMAGE_THUNK_DATA* t = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
		for(; t->u1.AddressOfData; ++t)
			if(!IMAGE_SNAP_BY_ORDINAL(t->u1.Ordinal))
				names.push_back(reinterpret_cast<const char*>(reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + t->u1.AddressOfData)->Name));
	}
	return names;
}
}  // namespace

// Points one import slot of hModule (szDll!szFunc) at pHook; returns the previous target, NULL when not imported.
// Used for the server's own x64 DLL image only (issue 2504: the database hooks here, luaL_loadstring in
// srv_remote_lua.cpp).
void* SrvPatchImport(HMODULE hModule, const char* szDll, const char* szFunc, void* pHook)
{
	BYTE* base = reinterpret_cast<BYTE*>(hModule);
	const IMAGE_NT_HEADERS* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
	const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if(!dir.VirtualAddress)
		return NULL;
	for(const IMAGE_IMPORT_DESCRIPTOR* d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name; ++d)
	{
		if(_stricmp(reinterpret_cast<const char*>(base + d->Name), szDll) != 0 || !d->OriginalFirstThunk)
			continue;
		const IMAGE_THUNK_DATA* names = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + d->OriginalFirstThunk);
		IMAGE_THUNK_DATA* iat = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->FirstThunk);
		for(; names->u1.AddressOfData; ++names, ++iat)
		{
			if(IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
				continue;
			const char* n = reinterpret_cast<const char*>(reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name);
			if(strcmp(n, szFunc) != 0)
				continue;
			void** slot = reinterpret_cast<void**>(&iat->u1.Function);
			DWORD old = 0;
			if(!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old))
				return NULL;
			void* prev = *slot;
			*slot = pHook;
			VirtualProtect(slot, sizeof(void*), old, &old);
			FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
			return prev;
		}
	}
	return NULL;
}

// After kRt_Boot loaded the DLL, before its DllGetGameContext.
void SrvDbInstall(HMODULE hDll)
{
	const char* const kDb = "CvGameDatabase_x64.dll";
	int nHooked = 0;
	for(size_t i = 0; i < sizeof(kHooks) / sizeof(kHooks[0]); ++i)
	{
		void* prev = SrvPatchImport(hDll, kDb, kHooks[i].name, kHooks[i].hook);
		if(prev)
		{
			*kHooks[i].orig = prev;
			++nHooked;
		}
	}
	// Every import must be hooked or known not to write; anything else could write unseen.
	std::vector<std::string> unknown;
	const std::vector<std::string> imports = ImportsFrom(hDll, kDb);
	for(size_t i = 0; i < imports.size(); ++i)
	{
		bool known = false;
		for(size_t k = 0; k < sizeof(kHooks) / sizeof(kHooks[0]) && !known; ++k)
			known = imports[i] == kHooks[k].name;
		for(size_t k = 0; k < sizeof(kNeutral) / sizeof(kNeutral[0]) && !known; ++k)
			known = imports[i] == kNeutral[k];
		if(!known)
			unknown.push_back(imports[i]);
	}
	// A hooked member whose original was not found (not imported) is never called: only the ones that are imported
	// matter, and those are all patched (SrvPatchImport found them by the same walk).
	s_bInstalled = true;
	SrvRemote::Report("database: write recording installed: %d of %zu import slots of %s hooked, %zu imports in all, %zu "
	                  "not covered", nHooked, sizeof(kHooks) / sizeof(kHooks[0]), kDb, imports.size(), unknown.size());
	for(size_t i = 0; i < unknown.size(); ++i)
		SrvRemote::Report("database: LOUD: the DLL imports %s, which the write recording does not cover", unknown[i].c_str());
	if(!unknown.empty())
		FailLoud("database: the DLL imports database members the write recording does not cover (see the report)");
}

void SrvDbSetGame(const Connection* pDb)
{
	if(pDb)
	{
		// Issue 0129 (above): before the DLL's first write. These calls are the server's own (not through the DLL's
		// import slots), so they are not recorded and never reach the engine's database.
		std::string how;
		const double t0 = SrvNow();
		const bool b = vpsrvdb::TuneCopy(*pDb, how);
		SrvRemote::Report("database: the server's copy tuned for scratch use (issue 0129): %s%s (%.3f s)", how.c_str(),
		                  b ? "" : " - NOT applied, every write of the DLL is flushed to disk", SrvNow() - t0);
	}
	EnterCriticalSection(&s_cs);
	s_game = pDb;
	s_stmts.clear();
	SetCount();
	LeaveCriticalSection(&s_cs);
}

// Writes collected in this thread's batch go to the engine now (srv_remote_lua.cpp calls it before every
// script-system call to the engine).
void SrvDbFlush() { FlushPending(); }

// Issue 4726 (unit-combat-server.md 12): for the unit feed (srv_remote_feed.cpp BattleDamageEventOff, once per load, on
// the DLL's thread): whether ICvCombatInfo1::GetDamageInflicted fires the Lua GameEvent BattleDamageDelta. The DLL's
// MOD_EVENTS_BATTLES_DAMAGE is EVENTS_BATTLES && EVENTS_BATTLES_DAMAGE (CustomMods.h; an option without a row is 0), cached
// from CustomModOptions at CacheGameDatabaseData. A SELECT on the DLL's game connection (SrvDb() before SetGameDatabase,
// standalone), not through the import hooks (a read). 1 on, 0 off, -1 not known (no connection, no table).
int SrvDbBattleDamageEvents()
{
	const Connection* pDb = s_game;
	if(!pDb)
		pDb = SrvDb();
	Results r;
	if(!pDb || !pDb->Execute(r, "SELECT Name, Value FROM CustomModOptions WHERE Name IN ('EVENTS_BATTLES', "
	                            "'EVENTS_BATTLES_DAMAGE')"))
		return -1;
	int battles = 0, damage = 0;
	while(r.Step())
	{
		const char* sz = r.GetText(0);
		const int v = r.GetInt(1);
		if(sz && strcmp(sz, "EVENTS_BATTLES") == 0)
			battles = v;
		else if(sz && strcmp(sz, "EVENTS_BATTLES_DAMAGE") == 0)
			damage = v;
	}
	return battles != 0 && damage != 0 ? 1 : 0;
}

namespace
{
volatile LONG s_nPulls = 0, s_nPulledTables = 0;
double s_pullSeconds = 0.0;
// Issue 0380 (ServerHook_ICvPreGame2_GetMapOption_s37 below): the checks a default lookup that missed set off, how
// many of them pulled tables, how many lookups the pull then filled, how many stayed without a default, and the time.
volatile LONG s_nMapMissChecks = 0, s_nMapMissPulls = 0, s_nMapMissFilled = 0, s_nMapMissUnfilled = 0;
double s_mapMissSeconds = 0.0;
std::unordered_map<std::string, LONG> s_mapMissChecked;  // map script -> s_nChecks after its own check (under s_cs)

std::string SqlString(const std::string& s)
{
	std::string q = "'";
	for(size_t i = 0; i < s.size(); ++i)
	{
		q += s[i];
		if(s[i] == '\'')
			q += '\'';
	}
	return q + "'";
}

bool ExecLogged(Connection& db, const std::string& sql, std::string& err)
{
	if(db.Execute(sql.c_str()))
		return true;
	err = sql.substr(0, 200) + ": " + (db.ErrorMessage() ? db.ErrorMessage() : "?");
	return false;
}

// Replaces the rows of `tables` in the server's copy with the engine's (a BinaryIO snapshot the proxy writes into the
// run folder, attached as vpsync): all DELETEs first, then all INSERTs (rowids kept; the ANALYZE tables are sets), in
// one savepoint; then DETACH and ClearCountCache. Triggers fire as they would (MapScripts has OnDeleteMapScript): the
// caller re-fingerprints and pulls the tables a trigger touched in the next round.
bool PullTables(Connection& db, const std::vector<std::string>& tables, std::string& err)
{
	char name[64];
	sprintf_s(name, "/engine-sync-%ld.db", InterlockedIncrement(&s_nPulls));
	const std::string path = std::string(SrvRunDir()) + "/db" + name;
	DeleteFileA(path.c_str());
	vpipc::Channel& ch = SrvRt::Chan();
	vpipc::Writer& w = SrvRt::Begin(ch, vpw::kRtE_DbSnapshot);
	w.str(path.c_str());
	vpipc::Reader r;
	if(!SrvRt::End(ch, vpw::kRtE_DbSnapshot, r))
	{
		err = "kRtE_DbSnapshot failed";
		return false;
	}
	const uint32_t ok = r.u32();
	r.f64();
	SrvRt::DecodeCheck(vpw::kRtE_DbSnapshot, r);
	if(!ok)
	{
		err = "the proxy could not write a snapshot of the engine's database to " + path;
		return false;
	}
	if(!ExecLogged(db, "ATTACH DATABASE " + SqlString(path) + " AS vpsync", err))
		return false;
	bool good = ExecLogged(db, "SAVEPOINT vpsync_pull", err);
	for(size_t i = 0; good && i < tables.size(); ++i)
		good = ExecLogged(db, "DELETE FROM main." + vpdb::Quote(tables[i]), err);
	for(size_t i = 0; good && i < tables.size(); ++i)
	{
		const std::string q = vpdb::Quote(tables[i]);
		if(tables[i].compare(0, 11, "sqlite_stat") == 0)
		{
			good = ExecLogged(db, "INSERT INTO main." + q + " SELECT * FROM vpsync." + q, err);
			continue;
		}
		std::string cols;
		{
			Results c;
			if(db.Execute(c, ("PRAGMA vpsync.table_info(" + q + ")").c_str()))
				while(c.Step())
					cols += (cols.empty() ? "" : ", ") + vpdb::Quote(c.GetText(1) ? c.GetText(1) : "");
		}
		if(cols.empty())
		{
			err = "no columns for " + tables[i];
			good = false;
			break;
		}
		good = ExecLogged(db, "INSERT INTO main." + q + "(rowid, " + cols + ") SELECT rowid, " + cols + " FROM vpsync." + q +
		                      " ORDER BY rowid", err);
	}
	std::string err2;
	if(good)
		good = ExecLogged(db, "RELEASE vpsync_pull", err);
	else
	{
		ExecLogged(db, "ROLLBACK TO vpsync_pull", err2);
		ExecLogged(db, "RELEASE vpsync_pull", err2);
	}
	ExecLogged(db, "DETACH DATABASE vpsync", err2);
	DeleteFileA(path.c_str());
	db.ClearCountCache();
	return good;
}

bool EngineFingerprint(const char* szWhen, vpdb::Fingerprint& eng)
{
	vpipc::Channel& ch = SrvRt::Chan();
	vpipc::Writer& w = SrvRt::Begin(ch, vpw::kRtE_DbFingerprint);
	w.str(szWhen);
	vpipc::Reader r;
	bool parsed = false;
	if(SrvRt::End(ch, vpw::kRtE_DbFingerprint, r))
	{
		uint32_t n = 0;
		const uint8_t* p = r.bytes(&n);
		parsed = p && eng.Parse(p, n);
		SrvRt::DecodeCheck(vpw::kRtE_DbFingerprint, r);
	}
	return parsed;
}

void ReportDiff(const vpdb::Fingerprint& eng, const vpdb::Fingerprint& mine)
{
	std::vector<std::string> lines;
	vpdb::Compare(eng, mine, lines, 20, "engine", "server");
	for(size_t i = 0; i < lines.size(); ++i)
		SrvRemote::Report("database: LOUD: %s", lines[i].c_str());
}
}  // namespace

// The engine's database (kRtE_DbFingerprint) against the server's copy. true when identical (at once or after a pull).
// 2504 follow-up: the EXE writes to its own database after SetGameDatabase (map script scan: MapScripts,
// MapScriptOptions, MapScriptOptionPossibleValues, live 2026-11-10). Tables that differ and that the DLL did not write
// since the last check are pulled from the engine (PullTables); a differing table the DLL wrote, a schema
// difference, or a pull that does not end IDENTICAL is a loud failure.
bool SrvDbCheckEngine(const char* szWhen)
{
	Connection* pDb = const_cast<Connection*>(s_game);
	if(!pDb || SrvRemote::ThreadChannel() < 0)
		return true;
	FlushPending();
	InterlockedIncrement(&s_nChecks);
	const double t0 = SrvNow();
	vpdb::Fingerprint mine = vpdb::Compute(*pDb);
	const double t1 = SrvNow();
	vpdb::Fingerprint eng;
	const bool parsed = EngineFingerprint(szWhen, eng);
	std::vector<std::string> lines;
	const size_t nDiff = parsed ? vpdb::Compare(eng, mine, lines, 20, "engine", "server") : 1;
	SrvRemote::Report("database: check %s: engine %u tables %llu rows digest %016llx, server %u tables %llu rows digest "
	                  "%016llx: %s (%ld writes replayed so far; server %.3f s, round trip %.3f s)", szWhen,
	                  (unsigned)eng.tables.size(), (unsigned long long)eng.TotalRows(), (unsigned long long)eng.Digest(),
	                  (unsigned)mine.tables.size(), (unsigned long long)mine.TotalRows(), (unsigned long long)mine.Digest(),
	                  nDiff == 0 ? "IDENTICAL" : "DIFFERENT", s_nOps, t1 - t0, SrvNow() - t1);
	std::vector<std::string> written;
	EnterCriticalSection(&s_cs);
	written.swap(s_written);
	LeaveCriticalSection(&s_cs);
	if(nDiff == 0)
		return true;
	std::string why;
	std::vector<std::string> diff, pulled;
	if(!parsed)
		why = "no fingerprint from the proxy";
	else if(!vpdb::DifferingTables(eng, mine, diff))
		why = "the schema or the set of tables differs (not pulled)";
	const double tp = SrvNow();
	for(int round = 1; why.empty() && round <= 3; ++round)
	{
		for(size_t i = 0; i < diff.size() && why.empty(); ++i)
		{
			const std::string lower = vpdb::detail::Lower(diff[i]);
			for(size_t k = 0; k < written.size() && why.empty(); ++k)
				if(written[k] == "*" || written[k] == lower)
					why = "table " + diff[i] + " differs and the DLL wrote " +
					      (written[k] == "*" ? std::string("to a table not known (\"*\")") : std::string("it")) +
					      " since the last check (not pulled)";
			bool have = false;
			for(size_t k = 0; k < pulled.size() && !have; ++k)
				have = pulled[k] == diff[i];
			if(!have)
				pulled.push_back(diff[i]);
		}
		if(!why.empty())
			break;
		std::string names;
		for(size_t i = 0; i < pulled.size(); ++i)
			names += (i ? ", " : "") + pulled[i];
		std::string err;
		if(!PullTables(*pDb, pulled, err))
		{
			why = "pulling " + names + " from the engine failed: " + err;
			break;
		}
		mine = vpdb::Compute(*pDb);
		const bool same = vpdb::DifferingTables(eng, mine, diff) && diff.empty();
		SrvRemote::Report("database: pull %s round %d: %zu tables from the engine's database (%s): %s (%.3f s)", szWhen, round,
		                  pulled.size(), names.c_str(), same ? "IDENTICAL" : "still DIFFERENT", SrvNow() - tp);
		if(same)
		{
			InterlockedExchangeAdd(&s_nPulledTables, (LONG)pulled.size());
			EnterCriticalSection(&s_cs);
			s_pullSeconds += SrvNow() - tp;
			LeaveCriticalSection(&s_cs);
			return true;
		}
		if(diff.empty())
			why = "the schema differs after the pull";
	}
	if(why.empty())
		why = "still DIFFERENT after 3 pull rounds";
	InterlockedIncrement(&s_nCheckFails);
	ReportDiff(eng, mine);
	char sz[600];
	sprintf_s(sz, "database: the engine's database and the server's copy differ %s: %.400s", szWhen, why.c_str());
	FailLoud(sz);
	return false;
}

void SrvDbReport(FILE* f)
{
	fprintf(f, "database: write recording %s; %ld writes replayed on the engine's database (Execute(sql) %ld, Step %ld, "
	           "Results::Execute %ld, Analyze %ld, Begin %ld, End %ld, Commit %ld, ClearCountCache %ld); %ld results "
	           "differed, %ld could not be sent; %ld identity checks, %ld failed\n",
	        s_bInstalled ? "installed" : "NOT installed", s_nOps, s_kinds[1], s_kinds[2], s_kinds[3], s_kinds[4], s_kinds[5],
	        s_kinds[6], s_kinds[7], s_kinds[8], s_nMismatch, s_nLost, s_nChecks, s_nCheckFails);
	fprintf(f, "database: %ld batches (kRtE_DbOps), round trips %.3f s, of which the proxy applied %.3f s; %ld pulls from "
	           "the engine's database, %ld tables pulled, %.3f s\n", s_nBatches, s_rtSeconds, s_applySeconds, s_nPulls,
	        s_nPulledTables, s_pullSeconds);
	fprintf(f, "database: map option lookups without a default in the server's copy (issue 0380): %ld engine checks "
	           "(%.3f s), %ld of them pulled tables; %ld lookups filled by the pull, %ld left without a default\n",
	        s_nMapMissChecks, s_mapMissSeconds, s_nMapMissPulls, s_nMapMissFilled, s_nMapMissUnfilled);
	EnterCriticalSection(&s_cs);
	for(size_t i = 0; i < s_firstOps.size(); ++i)
		fprintf(f, "database: write %zu: %s\n", i + 1, s_firstOps[i].c_str());
	LeaveCriticalSection(&s_cs);
}

// =====================================================================================================================
// ICvDLLDatabaseUtility1: the call runs, then the engine's database is compared with the server's copy (before the
// RETURN is written: the comparison makes a nested call on the channel).

bool ServerHook_ICvDLLDatabaseUtility1_CacheGameDatabaseData(ICvDLLDatabaseUtility1* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	if(!args.ok())
		return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "CacheGameDatabaseData: bad arguments");
	bool b;
	{
		BatchScope batch;  // the writes of this call go to the engine in batches (2504 follow-up)
		b = self->CacheGameDatabaseData();
	}
	SrvDbCheckEngine("after CacheGameDatabaseData");
	ret.b(b);
	return true;
}
bool ServerHook_ICvDLLDatabaseUtility1_FlushGameDatabaseData(ICvDLLDatabaseUtility1* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	if(!args.ok())
		return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "FlushGameDatabaseData: bad arguments");
	bool b;
	{
		BatchScope batch;  // the writes of this call go to the engine in batches (2504 follow-up)
		b = self->FlushGameDatabaseData();
	}
	SrvDbCheckEngine("after FlushGameDatabaseData");
	ret.b(b);
	return true;
}
bool ServerHook_ICvDLLDatabaseUtility1_PerformDatabasePostProcessing(ICvDLLDatabaseUtility1* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	if(!args.ok())
		return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "PerformDatabasePostProcessing: bad arguments");
	bool b;
	{
		BatchScope batch;  // the writes of this call go to the engine in batches (2504 follow-up)
		b = self->PerformDatabasePostProcessing();
	}
	// Issue 4253: the boot guard on the player totals and the run-time major count, on the server's copy the DLL has
	// just written its Defines into. A mismatch ends the server with its report (exit 1); the proxy's call fails and
	// takes the D118/D119 path (error report and popup), never a game on a DLL this server.exe was not built for.
	if(b && s_game)
	{
		std::string why;
		if(!SrvCheckPlayerLimits(*s_game, why))
			SrvRemote::ReportAndExit(1, why.c_str());
		SrvRemote::Report("%s", why.c_str());
	}
	SrvDbCheckEngine("after PerformDatabasePostProcessing");
	ret.b(b);
	return true;
}

// =====================================================================================================================
// Issue 0380 (proxy-design 31): ICvPreGame1::GetMapOption(const char*, int&) on the front end. The DLL answers an
// option the setup screen has not set from MapScriptOptions.DefaultValue of the current map script (CvPreGame.cpp
// GetMapOption) and stores that value. The EXE fills MapScripts, MapScriptOptions and MapScriptOptionPossibleValues of
// its own database with the active mods' map scripts after the database calls that end in an identity check, so the
// server's copy has none of them until the next check (in a game start's CacheGameDatabaseData): the lookup missed
// and the setup screen showed blank values where stock shows the defaults. So a lookup that misses runs the identity
// check against the engine's database (SrvDbCheckEngine pulls the tables the EXE changed itself) and, when tables were
// pulled, asks the DLL again. At most one such check per map script between two checks of any kind (the setup screen
// asks for about 15 options per script; an engine fingerprint costs about 0.15 s each way). A miss the pull does not
// fill returns the DLL's own answer with a LOUD line naming the script: nothing is invented.
// Wire as generated (kM_ICvPreGame2_GetMapOption_s37): CALL self, str szOptionName, i32 iValue (in/out); RETURN b,
// i32 iValue.
bool ServerHook_ICvPreGame2_GetMapOption_s37(ICvPreGame2* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	const char* szName = args.cstr();
	int iValue = static_cast<int>(args.i32());
	if(!args.ok())
		return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "ICvPreGame1::GetMapOption(const char *, int &): bad "
		                           "arguments");
	const int iCaller = iValue;
	bool b = static_cast<ICvPreGame1*>(self)->GetMapOption(szName, iValue);
	if(!b && szName && s_game && SrvRemote::ThreadChannel() >= 0)
	{
		const CvString script = static_cast<ICvPreGame1*>(self)->mapScriptName();
		const std::string key(script.c_str());
		bool doCheck;
		EnterCriticalSection(&s_cs);
		{
			std::unordered_map<std::string, LONG>::const_iterator it = s_mapMissChecked.find(key);
			doCheck = it == s_mapMissChecked.end() || it->second != s_nChecks;
		}
		LeaveCriticalSection(&s_cs);
		if(doCheck)
		{
			char when[512];
			sprintf_s(when, "before the default of map option %.120s of map script %.300s", szName, key.c_str());
			const LONG pulls0 = s_nPulls;
			const double t0 = SrvNow();
			const bool same = SrvDbCheckEngine(when);
			const double dt = SrvNow() - t0;
			const bool pulled = s_nPulls != pulls0;
			InterlockedIncrement(&s_nMapMissChecks);
			if(pulled)
				InterlockedIncrement(&s_nMapMissPulls);
			EnterCriticalSection(&s_cs);
			s_mapMissChecked[key] = s_nChecks;
			s_mapMissSeconds += dt;
			LeaveCriticalSection(&s_cs);
			if(same && pulled)
			{
				iValue = iCaller;
				b = static_cast<ICvPreGame1*>(self)->GetMapOption(szName, iValue);
			}
			if(b)
				InterlockedIncrement(&s_nMapMissFilled);
			else
				InterlockedIncrement(&s_nMapMissUnfilled);
			SrvRemote::Report("database: %smap option %s of map script %s had no value in the server's copy: engine check "
			                  "%.3f s, %s; %s", b ? "" : "LOUD: ", szName, key.c_str(), dt,
			                  !same ? "FAILED" : pulled ? "tables pulled" : "identical, nothing to pull",
			                  b ? "the DLL's default lookup now finds it" :
			                      "no default in the engine's database either: the DLL's own answer (false, value "
			                      "unchanged) goes back");
		}
	}
	ret.b(b);
	ret.i32(iValue);
	return true;
}
