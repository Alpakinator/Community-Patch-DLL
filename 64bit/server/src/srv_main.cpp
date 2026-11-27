// The x64 server, first slice (epic 1500): loads the x64 DLL with the real database layer, supplies the EXE's
// services, creates a new game from a resolved spec and plays turns headless (AI only, autoplay).
//
// Sequence: research/sources/civ5-headless/engine-contract.md sections 3 (boot), 4-5 (pregame and new game), 6
// (turn loop), 7 (autoplay), 8 (shutdown), as reproduced by the 32-bit host for a plain VP DLL
// (server/evidence/1505/newgame-sequence.md and the steps list of its newgame-report.json). Threads: the new game
// runs on the main ("UI") thread, every Update on a separate core thread (IsGameCoreThread true only there, inside
// Update); net responses are delivered on the main thread between Updates. No IPC (epic 1200 attaches later).
//
// Usage: server.exe --config <run>\server.cfg      (written by server/run_server.py; key=value lines)
//        server.exe --remote --section <name> --config <cfg>   (remote mode, epic 2300: server/src/remote/, README)
// Exit: 0 game played to the target turn, 2 a phase failed (exception or error), 1 setup error.
#include "srv_common.h"
#include "srv_crashdump.h"	// issue 3522: the server's own minidump for the error report
#include "srv_guard.h"
#include "srv_hidden_embark.h"	// ticket 0573: [Performance] HiddenEmbarks into the DLL
#include "feed/srv_render_events.h"	// ticket 4932: the DLL's render messages (export VP64RenderEvent)
#include "srv_options.h"
#include "srv_power.h"	// bug 0118: power-throttling opt-out (header only)
#include "srv_write_options.h"	// one-off 9081: the installer's build-time verbs
#include "remote/srv_dll_interface.h"	// ticket 4719: the interface version between the DLL and the server
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string>
#include <vector>
#include <psapi.h>
#include <delayimp.h>

// Saves (srv_save.cpp, issue 2001): a .Civ5Save read natively; a save writes .pregame.bin, .body.bin and .Civ5Save.
bool SrvLoadCiv5Save(ICvGameContext1* pContext, ICvDLLDatabaseUtility1* pDbUtil, const char* szSave, const char* szLoadFileName,
                     std::string& why);
bool SrvSaveCiv5Save(ICvGameContext1* pContext, const char* szBase, const char* szGameDir, const char* szDlcExclude,
                     std::string& why);

// BEGIN 4253 PLAYER LIMITS (server/tests/limits/run_player_limits_test.py compiles this block on its own: keep it self-contained)
// Issue 4253 (epic 0334, MC #1614 G1): one server.exe for the 22- and the 43-major DLL. The DLL writes its compiled
// limits into the game database's Defines during PerformDatabasePostProcessing (`INSERT OR REPLACE INTO Defines`,
// CvDllDatabaseUtility.cpp:265-274 at fork 04b1211b8, through InsertGameDefine: MAX_CIV_PLAYERS, MAX_CIV_TEAMS, MAX_MAJOR_CIVS, MAX_MINOR_CIVS,
// MAX_PLAYERS, MAX_TEAMS, ...). The server reads the major count from there once after post-processing (the 32-bit
// headless host c5h does the same: civ5-headless host/setup/HHNewGame.cpp:15-31, which falls back to 22; this one fails
// instead) and checks the totals it is compiled with against the DLL's. Only MAX_MAJOR_CIVS, MAX_MINOR_CIVS and
// OBSERVER_TEAM differ between the 22 and the 43 build (research/03-dll-engine-boundary/player-limits.md 1.1, 4).
namespace vplimits
{
const int kMinMajors = 2;	// CustomModsGlobal.h:8 at 04b1211b8: "Valid values are 2 to 62"
const int kMaxMajors = 62;	// 63 leaves no minor slot and does not compile (player-limits.md 6.1)

struct Totals
{
	int iPlayers, iTeams, iCivPlayers;	// the values the server is compiled with
};

struct Limits
{
	int iMajors, iMinors, iPlayers, iTeams, iCivPlayers;	// as the DLL wrote them
};

// Defines.<szName> as an integer. False, with a message naming the row, when the query fails, the row is missing, or
// its value is not an integer (NULL, text, a real).
inline bool ReadDefine(const Database::Connection& db, const char* szName, int& iValue, std::string& why)
{
	Database::Results r;
	if(!db.Execute(r, "SELECT Value FROM Defines WHERE Name = ?"))
	{
		why = std::string("cannot query the game database's Defines for ") + szName;
		return false;
	}
	r.Bind(1, szName);
	if(!r.Step())
	{
		why = std::string("the game database's Defines has no row ") + szName +
		      " (the DLL writes it in PerformDatabasePostProcessing; was it called?)";
		return false;
	}
	if(r.ColumnType(0) != Database::COLTYPE_INT)
	{
		const char* sz = r.ColumnType(0) == Database::COLTYPE_NULL ? NULL : r.GetText(0);
		why = std::string("the game database's Defines row ") + szName + " is not an integer (" +
		      (sz ? std::string("'") + sz + "'" : std::string("NULL")) + ")";
		return false;
	}
	iValue = r.GetInt(0);
	return true;
}

// Reads the five rows and checks them. False with a message that names the value and the expected one.
inline bool Check(const Database::Connection& db, const Totals& compiled, Limits& got, std::string& why)
{
	if(!ReadDefine(db, "MAX_PLAYERS", got.iPlayers, why) || !ReadDefine(db, "MAX_TEAMS", got.iTeams, why) ||
	   !ReadDefine(db, "MAX_CIV_PLAYERS", got.iCivPlayers, why) || !ReadDefine(db, "MAX_MAJOR_CIVS", got.iMajors, why) ||
	   !ReadDefine(db, "MAX_MINOR_CIVS", got.iMinors, why))
		return false;
	char sz[512];
	const char* const kNames[] = {"MAX_PLAYERS", "MAX_TEAMS", "MAX_CIV_PLAYERS"};
	const int aiGot[] = {got.iPlayers, got.iTeams, got.iCivPlayers};
	const int aiCompiled[] = {compiled.iPlayers, compiled.iTeams, compiled.iCivPlayers};
	for(int i = 0; i < 3; ++i)
		if(aiGot[i] != aiCompiled[i])
		{
			sprintf_s(sz, "the DLL's %s is %d, this server is built for %d: this server.exe cannot run this DLL "
			          "(a DLL with other player totals needs a server built for them; player-limits.md 6.2)",
			          kNames[i], aiGot[i], aiCompiled[i]);
			why = sz;
			return false;
		}
	if(got.iMajors < kMinMajors || got.iMajors > kMaxMajors)
	{
		sprintf_s(sz, "the DLL's MAX_MAJOR_CIVS is %d, outside %d..%d", got.iMajors, kMinMajors, kMaxMajors);
		why = sz;
		return false;
	}
	if(got.iMajors + got.iMinors != got.iCivPlayers)
	{
		sprintf_s(sz, "the DLL's MAX_MAJOR_CIVS %d + MAX_MINOR_CIVS %d is not MAX_CIV_PLAYERS %d", got.iMajors,
		          got.iMinors, got.iCivPlayers);
		why = sz;
		return false;
	}
	return true;
}
}  // namespace vplimits
// END 4253 PLAYER LIMITS

namespace
{
	volatile long s_lMajorCivs = 0;	// issue 4253: Defines.MAX_MAJOR_CIVS once SrvCheckPlayerLimits has passed
}

int SrvMajorCivs() { return (int)s_lMajorCivs; }

bool SrvCheckPlayerLimits(const Database::Connection& db, std::string& why)
{
	const vplimits::Totals compiled = {MAX_PLAYERS, MAX_TEAMS, MAX_CIV_PLAYERS};
	vplimits::Limits got = {};
	if(!vplimits::Check(db, compiled, got, why))
	{
		why = "player limits: " + why;
		return false;
	}
	InterlockedExchange(&s_lMajorCivs, got.iMajors);
	char sz[256];
	sprintf_s(sz, "player limits: MAX_MAJOR_CIVS %d, MAX_MINOR_CIVS %d (the DLL's, read from Defines); MAX_PLAYERS %d, "
	          "MAX_TEAMS %d, MAX_CIV_PLAYERS %d (the DLL's = the server's)", got.iMajors, got.iMinors, got.iPlayers,
	          got.iTeams, got.iCivPlayers);
	why = sz;
	return true;
}

// Ticket 5054: the log writer (server/src/render/srv_render_common.h, which this source cannot include: shell/common is not
// on its include path). SrvLog queues its lines; the writer thread writes server.log and stdout.
namespace srvrender
{
	void LogSetServerSink(FILE* serverLog, double (*nowSeconds)());
	bool LogWriterStart();
	bool LogQueueServer(const char* text, size_t n, bool toStdout);
	bool LogSync(DWORD ms);
	bool LogShutdown(DWORD ms);
	void LogFlushForCrash(const char* why);
}
namespace SrvExeCrash
{
	void SetLogSync(bool (*fn)(unsigned long ms));	// remote/srv_exe_crash.h: the reports sync the logs before they read them
}

// ---------------------------------------------------------------------------------------------------------
// Settings

namespace
{
	std::map<std::string, std::string> s_cfg;
	std::vector<std::string> s_luaProbes;
	char s_szRunDir[MAX_PATH], s_szCacheDir[MAX_PATH], s_szGameDir[MAX_PATH];
	bool s_bDllLogs = false;
	FILE* s_pLog = NULL;
	FILE* s_pTurns = NULL;
	LARGE_INTEGER s_liFreq, s_liStart;
	Database::Connection* s_pDb = NULL;
	CRITICAL_SECTION s_csLog;

	double NowS()
	{
		LARGE_INTEGER li;
		QueryPerformanceCounter(&li);
		return double(li.QuadPart - s_liStart.QuadPart) / double(s_liFreq.QuadPart);
	}
	std::string Cfg(const char* k, const char* def = "")
	{
		std::map<std::string, std::string>::const_iterator it = s_cfg.find(k);
		return it == s_cfg.end() ? def : it->second;
	}
	int CfgInt(const char* k, int def)
	{
		const std::string s = Cfg(k);
		return s.empty() ? def : (int)strtol(s.c_str(), NULL, 0);
	}
	unsigned int CfgUInt(const char* k, unsigned int def)
	{
		const std::string s = Cfg(k);
		return s.empty() ? def : (unsigned int)strtoul(s.c_str(), NULL, 0);
	}
	bool LoadConfig(const char* szPath)
	{
		FILE* f = NULL;
		if(fopen_s(&f, szPath, "r") || !f)
			return false;
		char line[4096];
		while(fgets(line, sizeof(line), f))
		{
			size_t n = strlen(line);
			while(n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
				line[--n] = 0;
			if(!n || line[0] == '#')
				continue;
			char* eq = strchr(line, '=');
			if(!eq)
				continue;
			*eq = 0;
			if(!strcmp(line, "lua_probe"))
				s_luaProbes.push_back(eq + 1);
			else
				s_cfg[line] = eq + 1;
		}
		fclose(f);
		return true;
	}
}

void SrvLog(const char* szFormat, ...)
{
	char sz[4096];
	va_list v;
	va_start(v, szFormat);
	vsnprintf(sz, sizeof(sz), szFormat, v);
	va_end(v);
	// issue 0417: a line that takes 50 ms or more (the lock, stdout, server.log) is followed in server.log by a line naming
	// the parts (the first 64 of the process); the render thread's lines come through here (render.log's "render log: the
	// line before took" lines measured 57-2,512 ms in a huge load on the product path, 2026-11-16)
	static LONG s_slowLines = 0;
	// issue 0417: the stdout part was the slow one (60-68 ms on the F: instance, server.log 0.1 ms). Under the proxy
	// server.exe runs with CREATE_NO_WINDOW: its stdout is a console that has no window, which nobody can read, and each
	// write waits for the console host (conhost), which a loaded machine schedules late. Such a stdout is not written; a
	// pipe (run_server.py, the tests), a file or a console with a window is, as before. Decided once, said once.
	static volatile LONG s_stdout = -1;
	if(s_stdout < 0)
	{
		const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
		const bool hiddenConsole = h && h != INVALID_HANDLE_VALUE && GetFileType(h) == FILE_TYPE_CHAR && !GetConsoleWindow();
		InterlockedCompareExchange(&s_stdout, hiddenConsole ? 0 : 1, -1);
	}
	// ticket 5054: with the log writer running the line is queued and this thread never waits on a disk (the render
	// thread's RNote lines come here); the writer writes server.log and stdout. Without it (before the start, after the
	// stop at exit), as before.
	static volatile LONG s_saidQ = 0;   // the skip line below is said once, by either path
	{
		char line[4160];
		if(!s_stdout && s_pLog && !s_saidQ)
		{
			_snprintf_s(line, sizeof(line), _TRUNCATE, "[%9.3f] log: stdout is a console without a window (CREATE_NO_WINDOW): "
			            "lines go to server.log only (issue 0417)\n", NowS());
			if(srvrender::LogQueueServer(line, strlen(line), false))
				InterlockedExchange(&s_saidQ, 1);
		}
		int n = _snprintf_s(line, sizeof(line), _TRUNCATE, "[%9.3f] %s\n", NowS(), sz);
		if(n < 0)
		{
			line[sizeof(line) - 2] = '\n';
			line[sizeof(line) - 1] = 0;
			n = (int)strlen(line);
		}
		if(srvrender::LogQueueServer(line, (size_t)n, s_stdout != 0))
			return;
	}
	const double t0 = NowS();
	EnterCriticalSection(&s_csLog);
	const double t1 = NowS();
	if(!s_stdout && s_pLog && !s_saidQ)
	{
		InterlockedExchange(&s_saidQ, 1);
		fprintf(s_pLog, "[%9.3f] log: stdout is a console without a window (CREATE_NO_WINDOW): lines go to server.log only "
		        "(issue 0417)\n", NowS());
		fflush(s_pLog);
	}
	if(s_stdout)
	{
		fprintf(stdout, "[%9.3f] %s\n", NowS(), sz);
		fflush(stdout);
	}
	const double t2 = NowS();
	if(s_pLog)
	{
		fprintf(s_pLog, "[%9.3f] %s\n", NowS(), sz);
		fflush(s_pLog);
	}
	const double t3 = NowS();
	if(t3 - t0 >= 0.050 && s_pLog && InterlockedIncrement(&s_slowLines) <= 64)
	{
		fprintf(s_pLog, "[%9.3f] log: the line before took %.1f ms (waiting for the lock %.1f ms, stdout %.1f ms, server.log %.1f ms; "
		        "thread %lu)\n", NowS(), (t3 - t0) * 1000.0, (t1 - t0) * 1000.0, (t2 - t1) * 1000.0, (t3 - t2) * 1000.0,
		        GetCurrentThreadId());
		fflush(s_pLog);
	}
	LeaveCriticalSection(&s_csLog);
}

const char* SrvCacheFolder() { return s_szCacheDir; }
const char* SrvRunDir() { return s_szRunDir; }
const char* SrvGameDir() { return s_szGameDir; }
bool SrvKeepDllLogs() { return s_bDllLogs; }
Database::Connection* SrvDb() { return s_pDb; }

void HostExportHit(int) {}

std::string SrvCfgStr(const char* szKey, const char* szDefault) { return Cfg(szKey, szDefault); }
int SrvCfgInt(const char* szKey, int iDefault) { return CfgInt(szKey, iDefault); }
double SrvNow() { return NowS(); }

// ---------------------------------------------------------------------------------------------------------
// Crashes: the faulting address and an x64 unwind (module+RVA), symbolized offline (run_server.py).

namespace
{
	std::string DescribeAddress(DWORD64 addr)
	{
		HMODULE hMod = NULL;
		char szMod[MAX_PATH] = "?";
		if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		                      reinterpret_cast<LPCSTR>(addr), &hMod) && hMod)
		{
			char szPath[MAX_PATH];
			GetModuleFileNameA(hMod, szPath, MAX_PATH);
			const char* p = strrchr(szPath, '\\');
			strncpy_s(szMod, p ? p + 1 : szPath, _TRUNCATE);
		}
		char sz[512];
		sprintf_s(sz, "%s+0x%llx", szMod, hMod ? (unsigned long long)(addr - (DWORD64)hMod) : (unsigned long long)addr);
		return sz;
	}

	void LogException(EXCEPTION_POINTERS* pEx, const char* szWhere)
	{
		const DWORD code = pEx->ExceptionRecord->ExceptionCode;
		// ticket 5054: the queued lines first, written by this thread; this and later lines are written at once
		srvrender::LogFlushForCrash(szWhere);
		std::string detail;
		if(code == EXCEPTION_ACCESS_VIOLATION && pEx->ExceptionRecord->NumberParameters >= 2)
		{
			char sz[128];
			sprintf_s(sz, " (%s of 0x%llx)", pEx->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
			          (unsigned long long)pEx->ExceptionRecord->ExceptionInformation[1]);
			detail = sz;
		}
		SrvLog("EXCEPTION 0x%08lX in %s at %s%s, thread %lu", code, szWhere,
		       DescribeAddress((DWORD64)pEx->ExceptionRecord->ExceptionAddress).c_str(), detail.c_str(), GetCurrentThreadId());
		CONTEXT ctx = *pEx->ContextRecord;
		for(int i = 0; i < 48 && ctx.Rip; ++i)
		{
			SrvLog("  frame %2d %s", i, DescribeAddress(ctx.Rip).c_str());
			DWORD64 imageBase = 0;
			PRUNTIME_FUNCTION pFn = RtlLookupFunctionEntry(ctx.Rip, &imageBase, NULL);
			if(!pFn)
			{
				ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
				ctx.Rsp += 8;
				continue;
			}
			PVOID pHandlerData = NULL;
			DWORD64 establisher = 0;
			RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, pFn, &ctx, &pHandlerData, &establisher, NULL);
		}
	}

}

void SrvLogException(EXCEPTION_POINTERS* pEx, const char* szWhere) { LogException(pEx, szWhere); }

namespace
{
	const char* s_szPhase = "start";
	LONG PhaseFilter(EXCEPTION_POINTERS* pEx)
	{
		LogException(pEx, s_szPhase);
		return EXCEPTION_EXECUTE_HANDLER;
	}
	// No C++ objects with destructors here (SEH).
	bool Guarded(void (*pfn)(void*), void* p)
	{
		__try
		{
			pfn(p);
			return true;
		}
		__except(PhaseFilter(GetExceptionInformation()))
		{
			return false;
		}
	}
}

// ---------------------------------------------------------------------------------------------------------
// The game

namespace
{
	HMODULE s_hDll = NULL;
	ICvGameContext1* s_pContext = NULL;
	ICvDLLDatabaseUtility1* s_pDbUtil = NULL;
	bool s_bFailed = false;
	std::string s_strFail;
	std::vector<std::string> s_report;		// lines for server-report.txt
	int s_iTargetTurn = 0;
	bool s_bGameOver = false;	// bug 0094: the game ended (GAMESTATE_OVER) before the target turn
	int s_iEndTurn = 0;		// the turn the game ended at (valid with s_bGameOver)
	long s_lUpdates = 0;
	bool s_bLoad = false;	// load_save or load_pregame / load_body given: continue a saved game instead of a new one

	void Report(const char* szFormat, ...)
	{
		char sz[2048];
		va_list v;
		va_start(v, szFormat);
		vsnprintf(sz, sizeof(sz), szFormat, v);
		va_end(v);
		s_report.push_back(sz);
		SrvLog("%s", sz);
	}

	bool Fail(const char* szWhy)
	{
		s_bFailed = true;
		s_strFail = szWhy;
		Report("FAILED: %s", szWhy);
		return false;
	}

	// Issue 2501 item (h), as remote mode since issue 9008 (srv_remote_main.cpp BindDelayImports; server-host.md
	// section 13): bind every delay-loaded import of a library at once, right after it is loaded. LLD's lazy x64
	// delay-load thunk loses xmm1 (and can lose xmm0) on the first call of each import, which corrupts float arguments
	// such as lua_pushnumber's; binding up front means the thunk never runs.
	bool BindDelayImports(const char* szDll)
	{
		const HRESULT hr = __HrLoadAllImportsForDll(szDll);
		if(FAILED(hr))
			return Fail((std::string("cannot bind the delay-loaded imports of ") + szDll).c_str());
		SrvLog("delay-loaded imports of %s bound up front", szDll);
		return true;
	}

	// One phase: SEH-guarded, timed, reported.
	bool Phase(const char* szName, void (*pfn)(void*), void* p = NULL)
	{
		s_szPhase = szName;
		SrvLog("-- phase: %s", szName);
		const double t0 = NowS();
		const bool ok = Guarded(pfn, p);
		Report("phase %-28s %s %8.3f s", szName, ok && !s_bFailed ? "ok    " : "FAILED", NowS() - t0);
		if(!ok && !s_bFailed)
			Fail((std::string("exception in phase ") + szName).c_str());
		return ok && !s_bFailed;
	}

	ICvPreGame1* PreGame() { return s_pContext->GetPreGame(); }

	// ---- boot (contract section 3) ----

	void PhaseBoot(void*)
	{
		char szWhy[1024];
		wchar_t wsz[MAX_PATH];
		std::string why;
		if(!VfsInit(s_szGameDir, why))
		{
			Fail(("VFS: " + why).c_str());
			return;
		}
		Report("vfs: %s", why.c_str());
		MultiByteToWideChar(CP_ACP, 0, Cfg("lua_dll").c_str(), -1, wsz, MAX_PATH);
		if(!LuaStartup(wsz, szWhy, sizeof(szWhy)))
		{
			Fail(szWhy);
			return;
		}
		Report("lua: %s", szWhy);
		if(!BindDelayImports("lua51_x64.dll"))
			return;
		// Localization before the DLL's Init, as the EXE (issue 1704): the DLL's Lookup calls then return text.
		const std::string strLocDb = Cfg("loc_db");
		if(!strLocDb.empty())
		{
			const std::string strIcu = Cfg("icu_dat", (std::string(s_szGameDir) + "\\Locale\\icudt44l.dat").c_str());
			if(!LocStartup(Cfg("loc_dll").c_str(), strLocDb.c_str(), strIcu.c_str(), Cfg("language", "en_US").c_str(), why))
			{
				Fail(("localization: " + why).c_str());
				return;
			}
			Report("localization: %s", why.c_str());
			if(!BindDelayImports("CvLocalization_x64.dll"))
				return;
		}
		else
			Report("localization: not initialised (no loc_db): the DLL's text lookups return the key");
		// The gameplay database: CvGameDatabase_x64.dll (one copy per process), the run's own copy of the database.
		MultiByteToWideChar(CP_ACP, 0, Cfg("db_dll").c_str(), -1, wsz, MAX_PATH);
		if(!LoadLibraryExW(wsz, NULL, LOAD_WITH_ALTERED_SEARCH_PATH))
		{
			Fail("cannot load CvGameDatabase_x64.dll");
			return;
		}
		if(!BindDelayImports("CvGameDatabase_x64.dll"))
			return;
		s_pDb = new Database::Connection();
		if(!s_pDb->Open(Cfg("db").c_str(), Database::OPEN_READWRITE))
		{
			Fail("cannot open the gameplay database");
			return;
		}
		// Issue 2802: the localization database attached as the EXE attaches it (direct mode: only with loc_attach=1;
		// the 32-bit headless reference attaches nothing).
		if(!LocAttachGameDb(*s_pDb, NULL, why))
		{
			Fail(("localization attach: " + why).c_str());
			return;
		}
		Report("localization attach: %s", why.c_str());
		MultiByteToWideChar(CP_ACP, 0, Cfg("dll").c_str(), -1, wsz, MAX_PATH);
		s_hDll = LoadLibraryExW(wsz, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
		if(!s_hDll)
		{
			Fail("cannot load the DLL");
			return;
		}
		Report("dll: %s; lua51_x64.dll %s", Cfg("dll").c_str(),
		       GetModuleHandleW(L"lua51_x64.dll") == LuaModule() ? "one copy (the server's)" : "TWO COPIES");
		{
			// Ticket 4719: the interface version, as kRt_Boot checks it (remote/srv_dll_interface.h).
			const SrvDllInterface::Result iv = SrvDllInterface::Check(s_hDll);
			Report("dll: %s", iv.text.c_str());
			if(!iv.ok)
			{
				Fail(iv.text.c_str());
				return;
			}
		}
		typedef ICvGameContext1* (*PFN)();
		PFN pfn = reinterpret_cast<PFN>(GetProcAddress(s_hDll, "DllGetGameContext"));
		s_pContext = pfn ? pfn() : NULL;
		if(!s_pContext || !s_pContext->SetDLLIFace(SrvEngine()))
		{
			Fail("DllGetGameContext / SetDLLIFace");
			return;
		}
		Report("%s", SrvHiddenEmbark::Apply(s_hDll, false).c_str());   // ticket 0573 (no report in direct mode)
		Report("%s", SrvRenderEvents::Apply(s_hDll).c_str());          // ticket 4932
		SrvSetContext(s_pContext);
		s_pContext->SetGameDatabase(s_pDb);
		s_pContext->Init();
		ICvGameContext2* p2 = s_pContext->QueryInterface<ICvGameContext2>();
		if(p2)
			p2->SetEngineUserInterface(SrvUI());
		LuaSetUtility(s_pContext->GetScriptSystemUtility());
		ICvPreGame1* pPre = PreGame();
		pPre->setVersionString(std::string("403694 FINAL_RELEASE"));
		pPre->Destroy();
		Report("DLL version %s", s_pContext->GetDLLVersion());
		// Logging flags as the 32-bit reference (flags 15: AI, AI perf, builder AI, player/city split); they change
		// only logs in this DLL, set anyway for parity.
		const int iLog = CfgInt("dll_log_flags", 15);
		s_pContext->SetLogging(iLog != 0);
		s_pContext->SetAILogging((iLog & 1) != 0);
		s_pContext->SetAIPerfLogging((iLog & 2) != 0);
		s_pContext->SetBuilderAILogging((iLog & 4) != 0);
		s_pContext->SetPlayerAndCityAILogSplit((iLog & 8) != 0);
		ICvGame1* pGame = s_pContext->GetGame();
		ICvGameDeals1* pDeals = pGame->GetGameDeals();
		pDeals->CreateDeal();	// the EXE's scratch deal
		pDeals->Destroy();
		pGame->Destroy();
		pPre = PreGame();
		pPre->setGameMode(static_cast<GameMode>(-1));
		pPre->setGameType(static_cast<GameTypes>(0), static_cast<GameStartTypes>(0));
		pPre->resetSlots();
		pPre->resetGame();
		pPre->resetPlayers();
		pPre->Destroy();
	}

	void PhaseDatabase(void*)
	{
		s_pDbUtil = s_pContext->GetDatabaseLoadUtility();
		if(!s_pDbUtil->FlushGameDatabaseData() || !s_pDbUtil->PerformDatabasePostProcessing())
		{
			Fail("database post-processing");
			return;
		}
		// Issue 4253: the DLL's major count and the boot guard on the totals, right after the DLL wrote its Defines.
		std::string why;
		if(!SrvCheckPlayerLimits(*s_pDb, why))
		{
			Fail(why.c_str());
			return;
		}
		Report("%s", why.c_str());
		const double t0 = NowS();
		if(!s_pDbUtil->CacheGameDatabaseData())
		{
			Fail("CacheGameDatabaseData");
			return;
		}
		Report("CacheGameDatabaseData true, %.1f s", NowS() - t0);
		Report("info counts: units %d, buildings %d, techs %d, civs %d (playable %d, AI %d), minor civs %d",
		       s_pContext->GetNumUnitInfos(), s_pContext->GetNumBuildingInfos(), s_pContext->GetNumTechInfos(),
		       s_pContext->GetNumCivilizationInfos(), s_pContext->GetNumPlayableCivilizationInfos(),
		       s_pContext->GetNumAIPlayableCivilizationInfos(), s_pContext->GetNumMinorCivInfos());
	}

	void PhaseLua(void*)
	{
		LuaInstallEngineTables(s_pDb);
		LuaHideDebug();
	}

	// ---- pregame (contract 4.1, 4.2; the reference host's steps) ----

	void PhasePregame(void*)
	{
		ICvPreGame1* p = PreGame();
		const unsigned int uExeSeed = CfgUInt("exe_seed", CfgUInt("seed_map", 0));
		const int nMajors = CfgInt("majors", 4);
		const int nMajorSlots = SrvMajorCivs();	// issue 4253: the DLL's MAX_MAJOR_CIVS, read in the database phase
		if(nMajors > nMajorSlots)
		{
			char sz[256];
			sprintf_s(sz, "majors=%d in server.cfg, but this DLL has %d major slots (Defines MAX_MAJOR_CIVS; issue 4253)",
			          nMajors, nMajorSlots);
			Fail(sz);
			return;
		}
		p->resetGame();
		p->resetPlayers();
		for(int i = 0; i < MAX_CIV_PLAYERS; ++i)
			p->setPlayable((PlayerTypes)i, true);
		p->setGameType(GAME_SINGLE_PLAYER, GAME_NEW);
		p->setMapRandomSeed(uExeSeed);
		p->setSyncRandomSeed(uExeSeed);
		p->setActivePlayer((PlayerTypes)0);
		p->setGameName(CvString("My Game"));
		p->setNickname((PlayerTypes)0, CvString("Player 1"));
		p->clearDLCAllowed();
		p->setSlotStatus((PlayerTypes)0, SS_TAKEN);
		p->setSlotClaim((PlayerTypes)0, SLOTCLAIM_ASSIGNED);
		p->setLeaderName((PlayerTypes)0, CvString(""));
		p->setCivilizationDescription((PlayerTypes)0, CvString(""));
		p->setCivilizationShortDescription((PlayerTypes)0, CvString(""));
		p->setCivilizationAdjective((PlayerTypes)0, CvString(""));
		for(int i = 0; i < nMajorSlots; ++i)
		{
			char k[32];
			sprintf_s(k, "slot%d_civ", i);
			p->setCivilization((PlayerTypes)i, (CivilizationTypes)CfgInt(k, -1));
			sprintf_s(k, "slot%d_team", i);
			p->setTeamType((PlayerTypes)i, (TeamTypes)CfgInt(k, i));
		}
		p->setRandomWorldSize(false);
		p->setWorldSize((WorldSizeTypes)CfgInt("world_size", 1));
		p->setNumMinorCivs(CfgInt("city_states", 8));
		p->setLoadWBScenario(false);
		p->setRandomMapScript(false);
		p->setMapScriptName(CvString(Cfg("map_script").c_str()));
		p->setGameSpeed((GameSpeedTypes)CfgInt("speed", 2));
		p->setGameSpeed((GameSpeedTypes)CfgInt("speed", 2));
		p->setEra((EraTypes)CfgInt("era", 0));
		p->setHandicap((PlayerTypes)0, (HandicapTypes)CfgInt("handicap", 5));
		{
			const std::string v = Cfg("victories", "0,1,2,3,4");
			for(const char* s = v.c_str(); *s;)
			{
				p->setVictory((VictoryTypes)atoi(s), true);
				const char* c = strchr(s, ',');
				if(!c)
					break;
				s = c + 1;
			}
		}
		p->setMaxTurns(CfgInt("max_turns", 0));
		p->ResetGameOptions();
		p->ResetMapOptions();
		// gameoption_<NAME>=value, mapoption_<NAME>=value
		for(std::map<std::string, std::string>::const_iterator it = s_cfg.begin(); it != s_cfg.end(); ++it)
		{
			if(it->first.compare(0, 11, "gameoption_") == 0)
				p->SetGameOption(it->first.c_str() + 11, atoi(it->second.c_str()));
			else if(it->first.compare(0, 10, "mapoption_") == 0)
				p->SetMapOption(it->first.c_str() + 10, atoi(it->second.c_str()));
		}
		for(int i = 0; i < nMajorSlots; ++i)
			p->setSlotStatus((PlayerTypes)i, i == 0 ? SS_TAKEN : i < nMajors ? SS_COMPUTER : SS_CLOSED);
		p->Destroy();
	}

	// ---- new game (contract 5.1, 5.4) ----

	int LeaderOfCiv(int iCiv)
	{
		int iLeader = -1;
		Database::Results* pr = new Database::Results();
		if(s_pDb->Execute(*pr, "SELECT l.ID FROM Civilizations c JOIN Civilization_Leaders cl ON cl.CivilizationType = c.Type "
		                       "JOIN Leaders l ON l.Type = cl.LeaderheadType WHERE c.ID = ? ORDER BY cl.rowid LIMIT 1"))
		{
			pr->Bind(1, iCiv);
			if(pr->Step())
				iLeader = pr->GetInt(0);
		}
		delete pr;
		return iLeader;
	}

	void PhaseStart(void*)
	{
		ICvPreGame1* p = PreGame();
		p->setPersistSettings(false);
		ICvPreGame2* p2 = p->QueryInterface<ICvPreGame2>();
		if(p2)
		{
			p2->VerifyHandicap((PlayerTypes)0);	// QueryInterface returns the same wrapper: no second Destroy
		}
		p->setActivePlayer((PlayerTypes)0);
		const unsigned int uInfo = s_pContext->CreateNetInitInfo();	// snapshots pregame in its constructor
		ICvNetInitInfo1* pInfo = s_pContext->GetNetInitInfo(uInfo);
		if(pInfo)
		{
			pInfo->Commit();	// re-applies pregame; calls GetEvaluatedMapScriptPath
			pInfo->Destroy();
		}
		s_pContext->DestroyNetInitInfo(uInfo);
		p->setWorldSize((WorldSizeTypes)CfgInt("world_size", 1), false);
		p->setClimate((ClimateTypes)CfgInt("climate", 0));
		p->setSeaLevel((SeaLevelTypes)CfgInt("sea_level", 1));
		p->setEra((EraTypes)CfgInt("era", 0));
		s_pDbUtil->CacheGameDatabaseData();
		p->setMapRandomSeed(CfgUInt("seed_map", 0));
		p->setSyncRandomSeed(CfgUInt("seed_sync", 0));
		p->closeInactiveSlots();	// VP chooses random civs and city-states here, from the map seed
		SrvNetDeliver();
		ICvGame1* pGame = s_pContext->GetGame();
		pGame->Init((HandicapTypes)CfgInt("handicap", 5));
		pGame->Destroy();
		s_pDbUtil->CacheGameDatabaseData();
		// Leader choice (the EXE's job): VP has one leader per civ. Issue 4253: the DLL's major count, read at run time.
		const int nMajorSlots = SrvMajorCivs();
		for(int i = 0; i < nMajorSlots; ++i)
		{
			const SlotStatus s = p->slotStatus((PlayerTypes)i);
			if((s == SS_TAKEN || s == SS_COMPUTER) && p->leaderHead((PlayerTypes)i) == NO_LEADER)
			{
				const int iCiv = p->civilization((PlayerTypes)i);
				const int iLeader = LeaderOfCiv(iCiv);
				p->setLeaderHead((PlayerTypes)i, (LeaderHeadTypes)iLeader);
				Report("slot %2d: status %d civ %d leader %d", i, (int)s, iCiv, iLeader);
			}
		}
		for(int i = nMajorSlots; i < MAX_CIV_PLAYERS; ++i)
			if(p->slotStatus((PlayerTypes)i) == SS_COMPUTER)
				Report("slot %2d: city-state, minor civ %d", i, (int)p->minorCivType((PlayerTypes)i));
		p->Destroy();
	}

	void PhaseInit2(void*)
	{
		ICvGame1* pGame = s_pContext->GetGame();
		const long lErr0 = LuaErrorCount();
		const bool b = pGame->Init2();
		pGame->Destroy();
		if(!b)
			Fail("Init2 returned false");
		else if(LuaErrorCount() != lErr0)
			Fail("Lua errors during map generation (see Logs/Lua.log)");
	}

	void PhaseGameStarted(void*)
	{
		ICvGame1* pGame = s_pContext->GetGame();
		for(int i = 0; i < MAX_PLAYERS; ++i)
		{
			ICvPlayer1* pl = pGame->GetPlayer((PlayerTypes)i);
			if(pl)
			{
				pl->LaterInit();
				pl->Destroy();
			}
		}
		ICvMap1* pMap = s_pContext->GetMap();
		const int nPlots = pMap->NumPlots();
		Report("map %d x %d, %d plots", pMap->GetGridWidth(), pMap->GetGridHeight(), nPlots);
		pMap->Destroy();
		pGame->InitTacticalAnalysisMap(nPlots);
		ICvPreGame1* p = PreGame();
		p->SetGameOption("GAMEOPTION_DYNAMIC_TURNS", 0);
		p->SetGameOption("GAMEOPTION_SIMULTANEOUS_TURNS", 0);
		p->SetGameOption("GAMEOPTION_PITBOSS", 0);
		p->Destroy();
		s_pContext->SetGraphicsInitialized(true);
		for(int i = 0; i < MAX_PLAYERS; ++i)
		{
			ICvPlayer1* pl = pGame->GetPlayer((PlayerTypes)i);
			if(pl)
			{
				pl->SetupGraphical();
				pl->Destroy();
			}
		}
		pGame->DoGameStarted();
		ICvGame2* pGame2 = pGame->QueryInterface<ICvGame2>();
		if(pGame2)
		{
			pGame2->NetMessageStaticsReset();
			pGame2->Destroy();	// CvDllGame is reference counted: QueryInterface added a reference (CvDllPreGame is not)
		}
		pGame->SendPlayerOptions(true);
		ICvNetworkSyncronization1* pSync = s_pContext->GetNetworkSyncronizer();
		if(pSync)
		{
			pSync->ClearCityDeltas();
			pSync->ClearRandomDeltas();
			pSync->ClearPlayerDeltas();
			pSync->ClearPlotDeltas();
			pSync->ClearUnitDeltas();
		}
		Report("net: %zu responses delivered after SendPlayerOptions", SrvNetDeliver());
		// Issue 2402: a loaded game's first UI frame selects a unit, as the 32-bit reference does with
		// UpdateSelectionList (civ5-headless host/setup/HHNewGame.cpp HHGameStartedSteps): with nothing selected,
		// no original plot and no selection plot that is SelectSettler on turn 0 and CycleUnits(true) otherwise
		// (CvGame::updateSelectionList), which selects the active player's ready unit nearest (0,0) and so
		// increments its serialized CvUnit::m_iEverSelectedCount. The server's UI answers DoAutoUnitCycle false,
		// so UpdateSelectionList itself would do nothing here.
		if(!s_bLoad || pGame->GetGameTurn() == 0)
			pGame->SelectSettler();
		else
			pGame->CycleUnits(true);
		ICvPlayer1* p0 = pGame->GetPlayer((PlayerTypes)0);
		if(p0)
		{
			p0->RebroadcastNotifications();
			p0->Destroy();
		}
		// the load screen pauses the game for the active player, LoadScreen.lua's Game.SetPausePlayer(Game.GetActivePlayer())
		// (contract finding 10; civ5-headless host/setup/HHNewGame.cpp:923). Issue 2403: a save whose active player is not 0
		// otherwise resaved with a different CvGame::m_ePausePlayer than the 32-bit reference's.
		pGame->SetPausePlayer(pGame->GetActivePlayer());
		pGame->Destroy();
	}

	// ---- saves (issues 1506, 2001) ----

	void PhaseLoad(void*)
	{
		std::string why;
		// load_save: a .Civ5Save read natively (issue 2001); load_pregame + load_body: the DLL-written parts (tools).
		const std::string strSave = Cfg("load_save");
		const bool ok = !strSave.empty()
		                    ? SrvLoadCiv5Save(s_pContext, s_pDbUtil, strSave.c_str(), Cfg("load_file_name").c_str(), why)
		                    : SrvLoadRaw(s_pContext, s_pDbUtil, Cfg("load_pregame").c_str(), Cfg("load_body").c_str(),
		                                 Cfg("load_file_name").c_str(), why);
		Report("load: %s", why.c_str());
		if(!ok)
		{
			Fail("the save could not be read, or its pregame or body was not used up exactly");
			return;
		}
		SrvSetLoadedGame(true);
		ICvGame1* pGame = s_pContext->GetGame();
		Report("loaded game at turn %d, active player %d", pGame->GetGameTurn(), (int)pGame->GetActivePlayer());
		pGame->Destroy();
	}

	void PhaseSave(void* pv)
	{
		std::string why;
		const char* szBase = static_cast<const char*>(pv);
		// <base>.pregame.bin, <base>.body.bin and <base>.Civ5Save (issue 2001; new-game DLC list from the game folder
		// minus save_dlc_exclude)
		if(!SrvSaveCiv5Save(s_pContext, szBase, s_szGameDir, Cfg("save_dlc_exclude").c_str(), why))
			Fail(("save: " + why).c_str());
		else
			Report("save %s: %s", szBase, why.c_str());
	}

	void PhaseArm(void*)
	{
		for(size_t i = 0; i < s_luaProbes.size(); ++i)
		{
			std::string err;
			Report("lua probe %s: %s", s_luaProbes[i].c_str(), LuaRunFile(s_luaProbes[i].c_str(), &err) ? "ok" : err.c_str());
		}
		ICvGame1* pGame = s_pContext->GetGame();
		const int iTurn = pGame->GetGameTurn();
		pGame->Destroy();
		const int nTurns = s_iTargetTurn - iTurn;
		char sz[512];
		sprintf_s(sz, "Game.SetOption('GAMEOPTION_QUICK_COMBAT', true); Game.SetOption('GAMEOPTION_QUICK_MOVEMENT', true); "
		              "Game.SetAIAutoPlay(%d, 0)", nTurns);
		std::string err;
		if(!LuaRunString(sz, "=arm", &err))
		{
			Fail(("arming autoplay: " + err).c_str());
			return;
		}
		pGame = s_pContext->GetGame();
		Report("ARM: quick combat, quick movement, Game.SetAIAutoPlay(%d, 0); active player %d", nTurns, (int)pGame->GetActivePlayer());
		Report("net: %zu responses delivered after arming", SrvNetDeliver());
		pGame->SetPausePlayer(NO_PLAYER);
		pGame->Destroy();
	}

	// ---- turn loop (contract 6): Update on the core thread, net delivery on the main thread in between ----

	HANDLE s_hGo = NULL, s_hDone = NULL;
	volatile bool s_bQuit = false;
	volatile bool s_bUpdateOk = true;

	void CallUpdate(void*)
	{
		ICvGame1* pGame = s_pContext->GetGame();	// the wrapper takes the game-core lock
		pGame->Update();
		pGame->Destroy();
	}

	DWORD WINAPI CoreThread(void*)
	{
		for(;;)
		{
			WaitForSingleObject(s_hGo, INFINITE);
			if(s_bQuit)
				return 0;
			SrvSetCoreThreadUpdating(true);
			const char* szPrev = s_szPhase;
			s_szPhase = "Update (core thread)";
			s_bUpdateOk = Guarded(CallUpdate, NULL);
			s_szPhase = szPrev;
			SrvSetCoreThreadUpdating(false);
			SetEvent(s_hDone);
		}
	}

	bool OneUpdate()
	{
		SrvNoteUpdateIndex(++s_lUpdates);
		SetEvent(s_hGo);
		WaitForSingleObject(s_hDone, INFINITE);
		SrvNetDeliver();
		return s_bUpdateOk;
	}

	int GameTurn()
	{
		ICvGame1* pGame = s_pContext->GetGame();
		const int t = pGame->GetGameTurn();
		pGame->Destroy();
		return t;
	}

	void PhaseTurnLoop(void*)
	{
		s_hGo = CreateEventA(NULL, FALSE, FALSE, NULL);
		s_hDone = CreateEventA(NULL, FALSE, FALSE, NULL);
		HANDLE hThread = CreateThread(NULL, 64 * 1024 * 1024, CoreThread, NULL, 0, NULL);
		int iTurn = GameTurn();
		double tTurn = NowS();
		long lTurnUpdates = 0;
		const long lMaxUpdatesPerTurn = CfgInt("max_updates_per_turn", 2000);
		const int nIdle = CfgInt("idle_updates", 10);
		while(iTurn < s_iTargetTurn)
		{
			if(!OneUpdate())
			{
				Fail("exception in Update");
				break;
			}
			++lTurnUpdates;
			const int t = GameTurn();
			if(t != iTurn)
			{
				const double now = NowS();
				fprintf(s_pTurns, "%d\t%.3f\t%ld\n", iTurn, now - tTurn, lTurnUpdates);
				fflush(s_pTurns);
				PROCESS_MEMORY_COUNTERS pmc;
				GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
				SrvLog("turn %d done: %.3f s, %ld updates; now turn %d; working set %.0f MB", iTurn, now - tTurn,
				       lTurnUpdates, t, pmc.WorkingSetSize / 1048576.0);
				SrvRenderEvents::TurnDone(iTurn);   // ticket 4932: render-events-per-turn.tsv (only when the DLL sends)
				iTurn = t;
				tTurn = now;
				lTurnUpdates = 0;
			}
			// Bug 0094: the game has ended when its state is no longer GAMESTATE_ON. A victory in AI autoplay sets
			// GAMESTATE_EXTENDED (CvGame::setWinner: autoplay switched off, the winner's leader made the active
			// player), a victory otherwise GAMESTATE_OVER; either way the turn never advances again and further
			// Updates only move units in the stopped turn. Checked after every Update, as civ5-headless does
			// (HHNewGame.cpp autoplay loop: "game state %d at turn %d"), so the loop stops at the Update that ended
			// the game. (Corrected 2026-11-11 by 9029: the first fix checked GAMESTATE_OVER only and only after
			// max_updates_per_turn, which a real autoplay victory never reaches.)
			{
				ICvGame1* pGame = s_pContext->GetGame();
				const int iState = (int)pGame->GetGameState();
				const int iWinner = (int)pGame->GetWinner(), iVictory = (int)pGame->GetVictory();
				const int iWinTurn = pGame->GetWinningTurn();
				pGame->Destroy();
				if(iState != (int)GAMESTATE_ON)
				{
					s_bGameOver = true;
					s_iEndTurn = t;
					Report("GAME OVER at turn %d: winner team %d, victory type %d, winning turn %d (elapsed), game state %d "
					       "(1 over, 2 extended); the turn loop ends here after %ld updates of this turn (bug 0094)", t,
					       iWinner, iVictory, iWinTurn, iState, lTurnUpdates);
					break;
				}
			}
			if(lTurnUpdates > lMaxUpdatesPerTurn)
			{
				Fail("turn does not advance (max_updates_per_turn)");
				break;
			}
		}
		// No idle Updates after the end of a game: they would play the stopped turn on (bug 0094).
		for(int i = 0; i < nIdle && !s_bFailed && !s_bGameOver; ++i)
			if(!OneUpdate())
				Fail("exception in an idle Update");
		fprintf(s_pTurns, "%d\t%.3f\t%ld\t(idle)\n", iTurn, NowS() - tTurn, s_bGameOver ? 0L : (long)nIdle);
		s_bQuit = true;
		SetEvent(s_hGo);
		WaitForSingleObject(hThread, 10000);
		Report("turn loop: reached turn %d, %ld updates in all", iTurn, s_lUpdates);
	}

	// ---- final state summary (Lua, read only) ----

	const char* kSummaryLua = R"LUA(
local out = {}
local function add(s) out[#out + 1] = s end
local w, h = Map.GetGridSize()
add(string.format("turn %d, map %dx%d", Game.GetGameTurn(), w, h))
-- issue 4253: the DLL's limits as its Lua sees them (major slots 0..MAX_MAJOR_CIVS-1, city states from MAX_MAJOR_CIVS)
add(string.format("limits MAX_MAJOR_CIVS %d MAX_MINOR_CIVS %d MAX_CIV_PLAYERS %d", GameDefines.MAX_MAJOR_CIVS,
	GameDefines.MAX_MINOR_CIVS, GameDefines.MAX_CIV_PLAYERS))
for i = 0, GameDefines.MAX_CIV_PLAYERS - 1 do
	local p = Players[i]
	if p and p:IsEverAlive() then
		add(string.format("player %2d %-28s alive %s cities %d pop %d techs %d score %d gold %d units %d %s", i,
			GameInfo.Civilizations[p:GetCivilizationType()].Type, tostring(p:IsAlive()), p:GetNumCities(),
			p:GetTotalPopulation(), Teams[p:GetTeam()]:GetTeamTechs():GetNumTechsKnown(), p:GetScore(), p:GetGold(), p:GetNumUnits(),
			p:IsMinorCiv() and "city-state" or "major"))
	end
end
local f = io.open(SRV_SUMMARY_PATH, "w")
f:write(table.concat(out, "\n"), "\n")
f:close()
)LUA";

	void PhaseSummary(void* pv)
	{
		const char* szName = static_cast<const char*>(pv);
		char sz[MAX_PATH * 2];
		sprintf_s(sz, "SRV_SUMMARY_PATH = [[%s\\%s]]", s_szRunDir, szName);
		LuaRunString(sz, "=summary path");
		LuaRunString(kSummaryLua, "=summary");
	}

	void PhaseShutdown(void*)
	{
		ICvPreGame1* p = PreGame();
		p->setGameStarted(false);
		p->Destroy();
		ICvGame1* pGame = s_pContext->GetGame();
		pGame->Uninit();
		pGame->Destroy();
		SrvSetDone(true);
		s_pContext->Uninit();
		LocShutdown();
	}
}

// Issue 2703: srv_guard writes its fail-closed line (a veto that failed on a later thread) here as well.
static void GuardLogSink(const char* szLine) { SrvLog("%s", szLine); }

int main(int argc, char** argv)
{
	// One-off 9081: the installer's build-time verbs (--write-options, --write-options-schema; srv_write_options.h) come
	// before everything else: they write two files from the options table and exit, with no guard, log, window or DLL.
	{
		const int iVerb = SrvWriteOptionsVerb(argc, argv);
		if(iVerb >= 0)
			return iVerb;
	}
	// Issue 2501: the window guard first, before anything is loaded (srv_guard.h). With CIVBG_GUARD=1 every top-level
	// window of this process is cloaked at creation and never activated; if the guard cannot be installed, the server
	// says why (stderr now, server.log once it is open) and exits with SrvGuardExitCode() (srv_guard.h) before loading any DLL.
	char szGuard[768] = "";
	bool bTestNoGuard = false;
#ifdef VP_SERVER_TEST_DIALOG
	bTestNoGuard = SrvGuardTestArg(argc, argv, "--test-no-guard");	// TEST BUILD ONLY: the negative tests
#endif
	const int iGuard = bTestNoGuard ? (int)SRV_GUARD_NOT_REQUESTED : SrvGuardInstall(szGuard, sizeof(szGuard));
	if(bTestNoGuard)
		strcpy_s(szGuard, "TEST BUILD --test-no-guard: not installed");
	if(iGuard == SRV_GUARD_FAILED)
		fprintf(stderr, "server: CIVBG_GUARD=1 but the window guard could not be installed (%s); exit 0x%08lX before "
		                "loading any DLL\n", szGuard, SrvGuardExitCode());
#ifdef VP_SERVER_TEST_DIALOG
	if(iGuard != SRV_GUARD_FAILED && SrvGuardTestArg(argc, argv, "--test-dialog"))
		return SrvGuardTestDialogMain(argc, argv, iGuard);
#endif
	InitializeCriticalSection(&s_csLog);
	QueryPerformanceFrequency(&s_liFreq);
	QueryPerformanceCounter(&s_liStart);
	setvbuf(stdout, NULL, _IONBF, 0);
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
	// Bug 0118 (tracker 4000): the server has no visible window, so Windows 11 may throttle it (EcoQoS) and stop honouring
	// its timer resolution after a few seconds (srv_power.h). Opt out before anything else runs; the line goes to stdout
	// now (server/tests/run_power_test.py checks it) and into server.log once that is open.
	char szPower[320];
	SrvPowerOptOut(szPower, sizeof(szPower));
	printf("server: %s\n", szPower);
	// Remote mode (epic 2300): server.exe --remote --section <name> --config <cfg> [other arguments are ignored]
	const char* szSection = NULL;
	const char* szConfig = NULL;
	bool bRemote = false;
	for(int i = 1; i < argc; ++i)
	{
		if(!strcmp(argv[i], "--remote"))
			bRemote = true;
		else if(!strcmp(argv[i], "--section") && i + 1 < argc)
			szSection = argv[++i];
		else if(!strcmp(argv[i], "--config") && i + 1 < argc)
			szConfig = argv[++i];
	}
	if(iGuard == SRV_GUARD_FAILED && (!szConfig || !LoadConfig(szConfig)))
		TerminateProcess(GetCurrentProcess(), SrvGuardExitCode());
	if(!szConfig || !LoadConfig(szConfig) || (bRemote && !szSection))
	{
		fprintf(stderr, "usage: server.exe --config <run>\\server.cfg\n"
		                "       server.exe --remote --section <name> --config <remote server.cfg>\n");
		return 1;
	}
	strcpy_s(s_szRunDir, Cfg("run_dir").c_str());
	strcpy_s(s_szGameDir, Cfg("game_dir").c_str());
	sprintf_s(s_szCacheDir, "%s\\cache\\", s_szRunDir);
	s_bDllLogs = CfgInt("dll_logs", 0) != 0;
	s_iTargetTurn = CfgInt("turns", 10);
	if(!SetCurrentDirectoryA(s_szRunDir))
		return iGuard == SRV_GUARD_FAILED ? (int)SrvGuardExitCode() : 1;
	CreateDirectoryA("Logs", NULL);
	CreateDirectoryA("cache", NULL);
	s_pLog = _fsopen("server.log", "w", _SH_DENYWR);
	{
		// Issue 2704: the build id is the log's first line (srv_buildid.cpp, proxy/include/vp_buildid.h).
		extern void SrvBuildIdText(char* out, size_t outSize);
		char szId[1024];
		SrvBuildIdText(szId, sizeof(szId));
		SrvLog("%s", szId);
	}
	{
		char szState[1024];
		SrvGuardDescribe(iGuard, szState, sizeof(szState));
		SrvLog("server: pid %lu, %s; %s", GetCurrentProcessId(), szState, szGuard);
		SrvLog("server: %s", szPower);	// bug 0118
		SrvLog("server: %s", SrvCrashDumpInstall(s_szRunDir));	// issue 3522: <run>\server-<pid>-<stamp>.dmp on a crash
		if(iGuard == SRV_GUARD_FAILED)
		{
			SrvLog("server: FAILED: CIVBG_GUARD=1 and the window guard could not be installed; exit 0x%08lX before "
			       "loading any DLL", SrvGuardExitCode());
			if(s_pLog)
				fclose(s_pLog);
			TerminateProcess(GetCurrentProcess(), SrvGuardExitCode());
		}
		SrvGuardSetLogSink(GuardLogSink);	// issue 2703: a later veto failure also lands in server.log
	}
	// ticket 5054: from here every SrvLog / RLog / RNote line is queued and the log writer thread writes the files; the
	// reports (srv_exe_crash.cpp) read the logs after a sync; exits drain it (ReportAndExit, below), the crash path flushes it
	srvrender::LogSetServerSink(s_pLog, &NowS);
	SrvLog("server: %s", srvrender::LogWriterStart() ? "log writer started: render.log, server.log and stdout are written by one "
	       "thread; a stalled disk makes it wait, not the render thread (queue 4096 lines / 4 MiB; ticket 5054)"
	       : "LOUD: the log writer could not be started: the logs are written by each thread itself (ticket 5054)");
	SrvExeCrash::SetLogSync(&srvrender::LogSync);
	SrvOptionsLoad();	// one-off 9042: the player's options file (server.cfg options_file=), both modes
	if(bRemote)
	{
		// No game of its own and no turn-times.tsv: the engine drives the DLL through the proxy (proxy-design 11).
		SetEnvironmentVariableA("CV64_STUBLOG", (std::string(s_szRunDir) + "\\stubcalls.log").c_str());
		SrvLockInit();
		SrvResolveSlots();
		HostExportsInit();
		return RemoteMain(szSection);
	}
	s_pTurns = _fsopen("turn-times.tsv", "w", _SH_DENYWR);
	fprintf(s_pTurns, "turn\tseconds\tupdates\n");
	SetEnvironmentVariableA("CV64_STUBLOG", (std::string(s_szRunDir) + "\\stubcalls.log").c_str());
	SrvLockInit();
	const int nSlotProblems = SrvResolveSlots();
	Report("server: run %s, game dir %s, slot decode problems %d, target turn %d", s_szRunDir, s_szGameDir, nSlotProblems, s_iTargetTurn);
	HostExportsInit();

	const double tStart = NowS();
	s_bLoad = !Cfg("load_body").empty() || !Cfg("load_save").empty();
	bool ok = Phase("boot", PhaseBoot) && Phase("database", PhaseDatabase) && Phase("lua", PhaseLua);
	if(s_bLoad)
		ok = ok && Phase("load", PhaseLoad) && Phase("game started", PhaseGameStarted);
	else
		ok = ok && Phase("pregame", PhasePregame) && Phase("start", PhaseStart) && Phase("init2 (map generation)", PhaseInit2) &&
		     Phase("game started", PhaseGameStarted);
	if(ok)
		Phase("t000 summary", PhaseSummary, (void*)"t000-summary.txt");
	const std::string strSaveStart = Cfg("save_at_start");
	if(ok && !strSaveStart.empty())
		ok = Phase("save at start", PhaseSave, (void*)strSaveStart.c_str());
	const double tAutoplay = NowS();
	const int iStartTurn = s_pContext && !s_bFailed ? GameTurn() : 0;	// a loaded game starts at the save's turn (issue 2003)
	if(ok && GameTurn() < s_iTargetTurn)
		ok = Phase("arm autoplay", PhaseArm) && Phase("turn loop", PhaseTurnLoop);
	const double tLoop = NowS() - tAutoplay;
	if(s_pContext && !s_bFailed)
		Phase("final summary", PhaseSummary, (void*)"final-summary.txt");
	const std::string strSaveOut = Cfg("save_out");
	if(s_pContext && !s_bFailed && !strSaveOut.empty())
		Phase("final save", PhaseSave, (void*)strSaveOut.c_str());
	if(s_pContext && !s_bFailed)
		Phase("shutdown", PhaseShutdown);

	PROCESS_MEMORY_COUNTERS pmc;
	GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
	const SrvNetStats net = SrvNetGetStats();
	// Bug 0094: a game that ended before the target turn reports "game_over" and the turn it ended at (exit code 0;
	// the runners say "game over at turn N" instead of a failure).
	const int iEndTurn = s_bGameOver ? s_iEndTurn : s_iTargetTurn;
	const int iPlayed = iEndTurn - iStartTurn;
	Report("result: %s; turns %d -> %d (%d played), autoplay %.1f s (%.3f s/turn), total %.1f s; peak working set %.0f MB; Lua errors %ld",
	       s_bFailed ? "FAILED" : (s_bGameOver ? "game_over" : "passed"), iStartTurn, iEndTurn, iPlayed, tLoop,
	       iPlayed > 0 ? tLoop / iPlayed : 0.0,
	       NowS() - tStart, pmc.PeakWorkingSetSize / 1048576.0, LuaErrorCount());
	Report("net loopback: queued %ld, delivered %ld, undelivered %ld (%s)", net.lQueued, net.lDelivered, net.lUndelivered,
	       SrvNetSummary().c_str());
	int nUnserved = 0;
	for(int i = 0; i < HI_COUNT; ++i)
		for(int k = 0; k < HostGetIface(i).nMethods; ++k)
			nUnserved += HostIfaceCalls(i, k) && !HostIfaceImplemented(i, k) ? 1 : 0;
	Report("engine calls not served by the server: %d methods (listed at the end; issue 9007)", nUnserved);
	{
		const std::string he = SrvHiddenEmbark::Summary();   // ticket 0573 ("" when the DLL has no export)
		if(!he.empty())
			Report("%s", he.c_str());
		const std::string re = SrvRenderEvents::Summary();   // ticket 4932 ("" when the DLL sends nothing)
		if(!re.empty())
			Report("%s", re.c_str());
	}

	// Engine methods called: implemented by the server vs answered by a generated zero stub; then the unserved ones
	// on their own (run_server.py --fail-on-unserved reads that section: "unserved<TAB>Class::method<TAB>calls").
	FILE* f = NULL;
	if(!fopen_s(&f, "server-report.txt", "w") && f)
	{
		for(size_t i = 0; i < s_report.size(); ++i)
			fprintf(f, "%s\n", s_report[i].c_str());
		fprintf(f, "\nengine methods called (interface, slot, method, calls, served by):\n");
		for(int i = 0; i < HI_COUNT; ++i)
		{
			const HostIfaceInfo& fi = HostGetIface(i);
			for(int k = 0; k < fi.nMethods; ++k)
				if(HostIfaceCalls(i, k))
					fprintf(f, "%s\t%d\t%s::%s\t%ld\t%s\n", fi.szName, fi.piSlots[k], fi.pMethods[k].szClass,
					        fi.pMethods[k].szName, HostIfaceCalls(i, k), HostIfaceImplemented(i, k) ? "server" : "stub");
		}
		fprintf(f, "\nengine calls not served by the server (generated stubs that answered zero/NULL; %d methods):\n", nUnserved);
		for(int i = 0; i < HI_COUNT; ++i)
		{
			const HostIfaceInfo& fi = HostGetIface(i);
			for(int k = 0; k < fi.nMethods; ++k)
				if(HostIfaceCalls(i, k) && !HostIfaceImplemented(i, k))
					fprintf(f, "unserved\t%s::%s\t%ld\n", fi.pMethods[k].szClass, fi.pMethods[k].szName, HostIfaceCalls(i, k));
		}
		fclose(f);
	}
	SrvLogFilesClose();
	LuaShutdown();
	// ticket 5054: the queued lines are written before server.log closes (a writer still stuck on the disk keeps it open)
	if(srvrender::LogShutdown(5000) && s_pLog)
		fclose(s_pLog);
	if(s_pTurns)
		fclose(s_pTurns);
	fflush(stdout);
	// The DLL's static destructors and threads are not run: leave at once.
	TerminateProcess(GetCurrentProcess(), s_bFailed ? 2 : 0);
	return s_bFailed ? 2 : 0;
}
