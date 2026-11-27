// x64 smoke host (issue 1007, epic 1000): loads the x64 DLL, runs DllMain and the boot entry points the game
// calls (research/sources/civ5-headless/engine-contract.md section 3), each in its own SEH guard, records every
// host-export call and every engine-interface method call, and writes smoke-report.md.
//
// Usage: smoke_host.exe --dll <CvGameCore_Expansion2.dll> --workdir <dir> [--report <file>] [--stublog <file>]
//                       [--lua <lua51_x64.dll>]
//                       [--crash-to-dll-filter]
//   --workdir   becomes the current directory; Logs\, cache\ and the DLL's crashlogs\ land there.
//   --stublog   sets CV64_STUBLOG for the closed-library stubs' logger (default <workdir>\stubcalls.log).
//   --lua       lua51_x64.dll to load (default: beside --dll). Missing: CreateLuaThread returns NULL.
//   --report    default <workdir>\smoke-report.md.
//   --crash-to-dll-filter  the first exception that ends a step is not handled by the host but passed on to the
//               process's unhandled-exception filter (the DLL's CustomFilter once Init has run); the report is
//               written before, and the process ends there. Tests the DLL's x64 crash handler.
// Exit code: 0 all steps passed, 2 a step raised an exception, 1 setup error.
#include "host_common.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string>
#include <vector>

int HostResolveSlots();

// ---------------------------------------------------------------------------------------------------------
// State

namespace
{
	char s_szWorkDir[MAX_PATH];
	char s_szCacheDir[MAX_PATH];
	char s_szReport[MAX_PATH];
	char s_szStubLog[MAX_PATH];
	char s_szDll[MAX_PATH];
	const char* s_szStep = "host start";
	FILE* s_pHostLog = NULL;
	CRITICAL_SECTION s_cs;
	LARGE_INTEGER s_liFreq, s_liStart;

	double NowMs()
	{
		LARGE_INTEGER li;
		QueryPerformanceCounter(&li);
		return double(li.QuadPart - s_liStart.QuadPart) * 1000.0 / double(s_liFreq.QuadPart);
	}

	enum EventKind { EV_STEP, EV_EXPORT, EV_IFACE, EV_NOTE, EV_EXCEPTION };
	struct Event
	{
		int seq;
		double ms;
		DWORD tid;
		const char* step;
		EventKind kind;
		int a, b;			// export index | iface, method
		std::string text;
	};
	std::vector<Event> s_events;

	struct MethodStat { long calls; int firstSeq; const char* firstStep; };
	std::vector<MethodStat> s_aIfaceStats[HI_COUNT];
	MethodStat s_aExportStats[64];

	void AddEvent(EventKind k, int a, int b, const std::string& text)
	{
		Event e;
		e.seq = (int)s_events.size() + 1;
		e.ms = NowMs();
		e.tid = GetCurrentThreadId();
		e.step = s_szStep;
		e.kind = k;
		e.a = a;
		e.b = b;
		e.text = text;
		s_events.push_back(e);
	}

	// Exceptions
	struct ExceptionRecordInfo
	{
		const char* step;
		bool firstChance;
		DWORD code;
		ULONG_PTR addr;
		std::string where;			// module+RVA symbol file:line
		std::string detail;			// access violation read/write address
		std::vector<std::string> stack;
		DWORD tid;
	};
	std::vector<ExceptionRecordInfo> s_exceptions;
	volatile LONG s_lInVeh = 0;
}

void HostLog(const char* szFormat, ...)
{
	char sz[2048];
	va_list v;
	va_start(v, szFormat);
	vsnprintf(sz, sizeof(sz), szFormat, v);
	va_end(v);
	fprintf(stdout, "%s\n", sz);
	fflush(stdout);
	if(s_pHostLog)
	{
		fprintf(s_pHostLog, "[%10.3f] %s\n", NowMs(), sz);
		fflush(s_pHostLog);
	}
}

void HostNote(const char* szFormat, ...)
{
	char sz[1024];
	va_list v;
	va_start(v, szFormat);
	vsnprintf(sz, sizeof(sz), szFormat, v);
	va_end(v);
	EnterCriticalSection(&s_cs);
	AddEvent(EV_NOTE, 0, 0, sz);
	LeaveCriticalSection(&s_cs);
	HostLog("note: %s", sz);
}

void HostSetStep(const char* szStep)
{
	EnterCriticalSection(&s_cs);
	s_szStep = szStep;
	AddEvent(EV_STEP, 0, 0, szStep);
	LeaveCriticalSection(&s_cs);
	HostLog("=== step: %s", szStep);
}
const char* HostGetStep() { return s_szStep; }
const char* HostCacheFolder() { return s_szCacheDir; }
const char* HostWorkDir() { return s_szWorkDir; }

void HostIfaceHit(int iIface, int iMethod)
{
	EnterCriticalSection(&s_cs);
	MethodStat& s = s_aIfaceStats[iIface][iMethod];
	if(s.calls++ == 0)
	{
		s.firstStep = s_szStep;
		AddEvent(EV_IFACE, iIface, iMethod, "");
		s.firstSeq = (int)s_events.size();
		const HostIfaceInfo& f = HostGetIface(iIface);
		HostLog("iface first call: %s slot %d %s::%s", f.szName, f.piSlots[iMethod], f.pMethods[iMethod].szClass,
		        f.pMethods[iMethod].szName);
	}
	LeaveCriticalSection(&s_cs);
}

void HostExportHit(int iExport)
{
	EnterCriticalSection(&s_cs);
	MethodStat& s = s_aExportStats[iExport];
	if(s.calls++ == 0)
	{
		s.firstStep = s_szStep;
		AddEvent(EV_EXPORT, iExport, 0, "");
		s.firstSeq = (int)s_events.size();
		HostLog("export first call: %s", HostExportName(iExport));
	}
	LeaveCriticalSection(&s_cs);
}

// ---------------------------------------------------------------------------------------------------------
// Exceptions: a vectored handler sees every first-chance exception (also ones the DLL or the loader would
// swallow); the step guard sees the one that ends the step. Addresses are written as module+RVA;
// run_smoke.py symbolizes them with llvm-symbolizer and the PDBs. (In-process dbghelp from System32 loaded only
// the export table of the lld-linked DLL, SymType 5, 2026-11-10; not pursued.)

static const char* ExceptionName(DWORD c)
{
	switch(c)
	{
	case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
	case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
	case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
	case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
	case EXCEPTION_BREAKPOINT: return "EXCEPTION_BREAKPOINT";
	case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
	case 0xC0000409: return "STATUS_STACK_BUFFER_OVERRUN (fast fail)";
	case 0xC0000374: return "STATUS_HEAP_CORRUPTION";
	case 0xE06D7363: return "C++ exception (0xE06D7363)";
	case 0xC000041D: return "STATUS_FATAL_USER_CALLBACK_EXCEPTION";
	default: return "exception";
	}
}

static std::string DescribeAddress(DWORD64 addr)
{
	char sz[1024];
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
	std::string s;
	sprintf_s(sz, "0x%016llx %s+0x%llx", (unsigned long long)addr, szMod,
	          hMod ? (unsigned long long)(addr - (DWORD64)hMod) : 0ull);
	s = sz;
	return s;
}

// x64 unwind from the faulting context with the images' own .pdata (RtlLookupFunctionEntry / RtlVirtualUnwind):
// no dbghelp needed.
static void WalkStack(const CONTEXT* pCtx, std::vector<std::string>& out, int maxFrames)
{
	CONTEXT ctx = *pCtx;
	for(int i = 0; i < maxFrames && ctx.Rip; ++i)
	{
		out.push_back(DescribeAddress(ctx.Rip));
		DWORD64 imageBase = 0;
		PRUNTIME_FUNCTION pFn = RtlLookupFunctionEntry(ctx.Rip, &imageBase, NULL);
		if(!pFn)
		{
			// Leaf function: the return address is at [rsp].
			ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
			ctx.Rsp += 8;
			continue;
		}
		PVOID pHandlerData = NULL;
		DWORD64 establisher = 0;
		RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, pFn, &ctx, &pHandlerData, &establisher, NULL);
	}
}

static void RecordException(EXCEPTION_POINTERS* pEx, bool bFirstChance)
{
	ExceptionRecordInfo r;
	r.step = s_szStep;
	r.firstChance = bFirstChance;
	r.code = pEx->ExceptionRecord->ExceptionCode;
	r.addr = (ULONG_PTR)pEx->ExceptionRecord->ExceptionAddress;
	r.tid = GetCurrentThreadId();
	r.where = DescribeAddress(r.addr);
	if(r.code == EXCEPTION_ACCESS_VIOLATION && pEx->ExceptionRecord->NumberParameters >= 2)
	{
		char sz[128];
		ULONG_PTR op = pEx->ExceptionRecord->ExceptionInformation[0];
		sprintf_s(sz, "%s of address 0x%016llx", op == 0 ? "read" : op == 1 ? "write" : "execute (DEP)",
		          (unsigned long long)pEx->ExceptionRecord->ExceptionInformation[1]);
		r.detail = sz;
	}
	WalkStack(pEx->ContextRecord, r.stack, 40);
	s_exceptions.push_back(r);
	char sz[256];
	sprintf_s(sz, "%s 0x%08lx %s at %s %s", bFirstChance ? "first-chance" : "ENDS STEP", r.code,
	          ExceptionName(r.code), r.where.c_str(), r.detail.c_str());
	AddEvent(EV_EXCEPTION, 0, 0, sz);
	HostLog("exception (%s): %s", s_szStep, sz);
}

static LONG CALLBACK VectoredHandler(EXCEPTION_POINTERS* pEx)
{
	const DWORD c = pEx->ExceptionRecord->ExceptionCode;
	// OutputDebugString (0x40010006 / 0x4001000A), thread naming (0x406D1388) and RPC/CLR noise are not faults.
	if(c == 0x40010006 || c == 0x4001000A || c == 0x406D1388 || c == 0x000006BA || c == 0x000006A6)
		return EXCEPTION_CONTINUE_SEARCH;
	if(InterlockedExchange(&s_lInVeh, 1))
		return EXCEPTION_CONTINUE_SEARCH;
	EnterCriticalSection(&s_cs);
	if(s_exceptions.size() < 32)
		RecordException(pEx, true);
	LeaveCriticalSection(&s_cs);
	InterlockedExchange(&s_lInVeh, 0);
	return EXCEPTION_CONTINUE_SEARCH;
}

// ---------------------------------------------------------------------------------------------------------
// Steps

namespace
{
	struct StepResult
	{
		const char* name;
		bool ran;
		bool ok;
		bool failed;	// returned normally but reported failure (s_bStepFailed)
		DWORD code;
		double ms;
		std::string value;
	};
	std::vector<StepResult> s_steps;

	HMODULE s_hDll = NULL;
	ICvGameContext1* s_pContext = NULL;
	ICvDLLDatabaseUtility1* s_pDbUtil = NULL;
	__declspec(align(16)) unsigned char s_abFakeConnection[4096];	// stub Database::Connection (zeroed)
	wchar_t s_wszDll[MAX_PATH];
	wchar_t s_wszLua[MAX_PATH];
	char s_szValue[256];
	DWORD s_dwExCode;

	void StepLoad()
	{
		s_hDll = LoadLibraryExW(s_wszDll, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
		sprintf_s(s_szValue, "HMODULE %p (GetLastError %lu)", (void*)s_hDll, s_hDll ? 0ul : GetLastError());
		// One Lua copy: the DLL's import of lua51_x64.dll must have bound to the module the host loaded.
		HMODULE hLua = GetModuleHandleW(L"lua51_x64.dll");
		if(s_hDll && HostLuaModule())
			HostNote("lua51_x64.dll after the DLL load: %p, host's %p (%s)", (void*)hLua, (void*)HostLuaModule(),
			         hLua == HostLuaModule() ? "one Lua copy" : "TWO COPIES");
	}
	void StepGetContext()
	{
		typedef ICvGameContext1* (*PFN)();
		PFN pfn = reinterpret_cast<PFN>(GetProcAddress(s_hDll, "DllGetGameContext"));
		s_pContext = pfn ? pfn() : NULL;
		sprintf_s(s_szValue, "%s -> %p", pfn ? "DllGetGameContext" : "export missing", (void*)s_pContext);
	}
	void StepSetDLLIFace() { bool b = s_pContext->SetDLLIFace(HostGetEngineUtility()); sprintf_s(s_szValue, "returned %s", b ? "true" : "false"); }
	// A step that fails without an exception sets this (RunStep reports "failed: <value>").
	bool s_bStepFailed = false;

	// The gameplay database (issue 1404): the host owns the connection, as the EXE does, and hands it to the DLL.
	// CvGameDatabase_x64.dll (issue 1402) is one copy per process: the host loads it from beside the game DLL by full
	// path before the DLL's own import binds to it, then calls it through the delay-loaded import library.
	char s_szDb[MAX_PATH];
	Database::Connection* s_pDb = NULL;
	void StepOpenDatabase()
	{
		wchar_t wszDbDll[MAX_PATH];
		wcscpy_s(wszDbDll, s_wszDll);
		wchar_t* p = wcsrchr(wszDbDll, L'\\');
		wcscpy_s(p ? p + 1 : wszDbDll, MAX_PATH - (p ? (p + 1 - wszDbDll) : 0), L"CvGameDatabase_x64.dll");
		HMODULE h = LoadLibraryExW(wszDbDll, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
		if(!h)
		{
			sprintf_s(s_szValue, "LoadLibrary(CvGameDatabase_x64.dll beside the DLL) failed, GetLastError %lu", GetLastError());
			s_bStepFailed = true;
			return;
		}
		s_pDb = new Database::Connection();
		// OPEN_READWRITE: post-processing and Analyze() write into the gameplay database (the run's own copy).
		const bool b = s_pDb->Open(s_szDb, Database::OPEN_READWRITE);
		sprintf_s(s_szValue, "CvGameDatabase_x64.dll %p, Open(%s, OPEN_READWRITE) -> %s%s%s", (void*)h, s_szDb,
		          b ? "true" : "false", b ? "" : ": ", b ? "" : s_pDb->ErrorMessage());
		s_bStepFailed = !b;
	}
	void StepSetGameDatabase()
	{
		if(s_pDb)
		{
			s_pContext->SetGameDatabase(s_pDb);
			sprintf_s(s_szValue, "real Database::Connection %p", (void*)s_pDb);
		}
		else
		{
			s_pContext->SetGameDatabase(reinterpret_cast<Database::Connection*>(s_abFakeConnection));
			sprintf_s(s_szValue, "zeroed 4 KB block %p (no --db)", (void*)s_abFakeConnection);
		}
	}

	// Info counts after CacheGameDatabaseData (issue 1404): what the DLL cached per info type, against the table's
	// row count and MAX(ID) + 1 (a table whose IDs have gaps gives an info array of MAX(ID) + 1 entries).
	struct InfoCountRow { const char* szWhat; const char* szTable; int (DLLCALL ICvGameContext1::*pfn)(); };
	const InfoCountRow s_aInfoCounts[] =
	{
		{ "Builds", "Builds", &ICvGameContext1::GetNumBuildInfos },
		{ "Buildings", "Buildings", &ICvGameContext1::GetNumBuildingInfos },
		{ "Civilizations", "Civilizations", &ICvGameContext1::GetNumCivilizationInfos },
		{ "Climates", "Climates", &ICvGameContext1::GetNumClimateInfos },
		{ "Colors", "Colors", &ICvGameContext1::GetNumColorInfos },
		{ "Eras", "Eras", &ICvGameContext1::GetNumEraInfos },
		{ "Features", "Features", &ICvGameContext1::GetNumFeatureInfos },
		{ "GameOptions", "GameOptions", &ICvGameContext1::GetNumGameOptionInfos },
		{ "GameSpeeds", "GameSpeeds", &ICvGameContext1::GetNumGameSpeedInfos },
		{ "Handicaps", "HandicapInfos", &ICvGameContext1::GetNumHandicapInfos },
		{ "Improvements", "Improvements", &ICvGameContext1::GetNumImprovementInfos },
		{ "Leaders", "Leaders", &ICvGameContext1::GetNumLeaderHeadInfos },
		{ "MinorCivilizations", "MinorCivilizations", &ICvGameContext1::GetNumMinorCivInfos },
		{ "PlayerColors", "PlayerColors", &ICvGameContext1::GetNumPlayerColorInfos },
		{ "Policies", "Policies", &ICvGameContext1::GetNumPolicyInfos },
		{ "Promotions", "UnitPromotions", &ICvGameContext1::GetNumPromotionInfos },
		{ "Resources", "Resources", &ICvGameContext1::GetNumResourceInfos },
		{ "SeaLevels", "SeaLevels", &ICvGameContext1::GetNumSeaLevelInfos },
		{ "Technologies", "Technologies", &ICvGameContext1::GetNumTechInfos },
		{ "Terrains", "Terrains", &ICvGameContext1::GetNumTerrainInfos },
		{ "UnitCombatClasses", "UnitCombatInfos", &ICvGameContext1::GetNumUnitCombatClassInfos },
		{ "Units", "Units", &ICvGameContext1::GetNumUnitInfos },
		{ "Victories", "Victories", &ICvGameContext1::GetNumVictoryInfos },
		{ "Worlds", "Worlds", &ICvGameContext1::GetNumWorldInfos },
	};
	std::string s_strInfoCounts;
	int SqlInt(const char* szSql)
	{
		Database::SingleResult* pr = new Database::SingleResult();
		int v = -1;
		if(s_pDb->Execute(*pr, szSql))	// a SingleResult steps to its first row inside Execute (SetCommand)
			v = pr->GetInt(0);
		delete pr;
		return v;
	}
	void StepInfoCounts()
	{
		const int nTypes = (int)(sizeof(s_aInfoCounts) / sizeof(s_aInfoCounts[0]));
		int nExact = 0, nIdRange = 0, nDiff = 0;
		char sz[512];
		s_strInfoCounts = "| Info | Table | DLL | SQL rows | MAX(ID)+1 | DLL count equals |\n|---|---|---|---|---|---|\n";
		for(int i = 0; i < nTypes; ++i)
		{
			const InfoCountRow& r = s_aInfoCounts[i];
			const int nDll = (s_pContext->*r.pfn)();
			char szSql[160];
			sprintf_s(szSql, "SELECT count(*) FROM %s", r.szTable);
			const int nRows = SqlInt(szSql);
			sprintf_s(szSql, "SELECT MAX(ID) + 1 FROM %s", r.szTable);
			const int nIds = SqlInt(szSql);
			const char* szVerdict = nDll == nRows ? "rows" : nDll == nIds ? "MAX(ID)+1" : "**neither**";
			if(nDll == nRows) ++nExact; else if(nDll == nIds) ++nIdRange; else ++nDiff;
			sprintf_s(sz, "| %s | %s | %d | %d | %d | %s |\n", r.szWhat, r.szTable, nDll, nRows, nIds, szVerdict);
			s_strInfoCounts += sz;
			HostLog("info count %-20s %-20s dll %5d sql %5d maxid+1 %5d %s", r.szWhat, r.szTable, nDll, nRows, nIds, szVerdict);
		}
		char szPlay[160];
		sprintf_s(szPlay, "playable civilizations %d, AI-playable %d, playable minor civs %d",
		          s_pContext->GetNumPlayableCivilizationInfos(), s_pContext->GetNumAIPlayableCivilizationInfos(),
		          s_pContext->GetNumPlayableMinorCivs());
		s_strInfoCounts += std::string("\n") + szPlay + "\n";
		HostLog("%s", szPlay);
		sprintf_s(s_szValue, "%d info types: %d = rows, %d = MAX(ID)+1, %d neither; %s", nTypes, nExact, nIdRange, nDiff, szPlay);
		s_bStepFailed = nDiff != 0;
	}
	void StepInit() { s_pContext->Init(); sprintf_s(s_szValue, "returned"); }
	void StepSetUI()
	{
		ICvGameContext2* p2 = s_pContext->QueryInterface<ICvGameContext2>();
		if(p2)
			p2->SetEngineUserInterface(HostGetUserInterface());
		sprintf_s(s_szValue, "QueryInterface(ICvGameContext2) -> %p%s", (void*)p2, p2 ? ", SetEngineUserInterface done" : "");
	}
	void StepLua()
	{
		char szWhy[512] = "";
		const bool b = HostLuaStartup(s_wszLua, szWhy, sizeof(szWhy));
		sprintf_s(s_szValue, "%s%s", b ? "" : "FAILED: ", szWhy);
	}
	void StepGetUtility()
	{
		ICvScriptSystemUtility1* p = s_pContext->GetScriptSystemUtility();
		HostLuaSetUtility(p);
		sprintf_s(s_szValue, "-> %p (CreateLuaThread will call its RegisterScriptLibraries)", (void*)p);
	}
	// The rest of phase A (engine-contract.md section 3) that needs no ini parser or setup data.
	void StepPreGameVersion()
	{
		ICvPreGame1* pPre = s_pContext->GetPreGame();
		pPre->setVersionString(std::string("403694 FINAL_RELEASE"));
		const GUID g = s_pContext->GetDLLGUID();
		const char* szVersion = s_pContext->GetDLLVersion();
		sprintf_s(s_szValue, "GetPreGame -> %p; DLL GUID {%08lX-...}, version \"%s\"", (void*)pPre, g.Data1,
		          szVersion ? szVersion : "(null)");
		pPre->Destroy();
	}
	void StepScratchDeal()
	{
		ICvGame1* pGame = s_pContext->GetGame();
		ICvGameDeals1* pDeals = pGame ? pGame->GetGameDeals() : NULL;
		const unsigned int uDeal = pDeals ? pDeals->CreateDeal() : 0;
		sprintf_s(s_szValue, "GetGame -> %p, GetGameDeals -> %p, CreateDeal -> %u", (void*)pGame, (void*)pDeals, uDeal);
		if(pDeals)
			pDeals->Destroy();
		if(pGame)
			pGame->Destroy();
	}
	void StepPreGameReset()
	{
		ICvPreGame1* pPre = s_pContext->GetPreGame();
		pPre->setGameMode(static_cast<GameMode>(-1));
		pPre->setGameType(static_cast<GameTypes>(0), static_cast<GameStartTypes>(0));
		pPre->resetSlots();
		pPre->resetGame();
		pPre->resetPlayers();
		sprintf_s(s_szValue, "setGameMode(-1), setGameType(0,0), resetSlots, resetGame, resetPlayers returned");
		pPre->Destroy();
	}
	void StepGetDbUtil() { s_pDbUtil = s_pContext->GetDatabaseLoadUtility(); sprintf_s(s_szValue, "-> %p", (void*)s_pDbUtil); }
	void StepFlush() { bool b = s_pDbUtil->FlushGameDatabaseData(); sprintf_s(s_szValue, "returned %s", b ? "true" : "false"); }
	void StepPost() { bool b = s_pDbUtil->PerformDatabasePostProcessing(); sprintf_s(s_szValue, "returned %s", b ? "true" : "false"); }
	void StepCache() { bool b = s_pDbUtil->CacheGameDatabaseData(); sprintf_s(s_szValue, "returned %s", b ? "true" : "false"); }
	void StepFree() { BOOL b = FreeLibrary(s_hDll); sprintf_s(s_szValue, "FreeLibrary returned %d", b); if(b) s_hDll = NULL; }

	bool s_bCrashToDllFilter = false;	// --crash-to-dll-filter
	void WriteReportNow();

	LONG StepFilter(EXCEPTION_POINTERS* pEx)
	{
		s_dwExCode = pEx->ExceptionRecord->ExceptionCode;
		EnterCriticalSection(&s_cs);
		RecordException(pEx, false);
		LeaveCriticalSection(&s_cs);
		if(s_bCrashToDllFilter)
		{
			// Let the exception go unhandled so the process filter the DLL installed in CvGlobals::init
			// (CustomFilter: crashlogs\crashes.log, minidump) handles it. Write the report first: the process
			// does not come back.
			HostNote("--crash-to-dll-filter: passing the exception to the process's unhandled-exception filter");
			WriteReportNow();
			return EXCEPTION_CONTINUE_SEARCH;
		}
		return EXCEPTION_EXECUTE_HANDLER;
	}

	// No C++ objects with destructors in this function (SEH).
	bool GuardedCall(void (*pfn)())
	{
		__try
		{
			pfn();
			return true;
		}
		__except(StepFilter(GetExceptionInformation()))
		{
			return false;
		}
	}

	bool RunStep(const char* szName, void (*pfn)())
	{
		HostSetStep(szName);
		StepResult r;
		r.name = szName;
		r.ran = true;
		s_szValue[0] = 0;
		s_dwExCode = 0;
		s_bStepFailed = false;
		const double t0 = NowMs();
		r.ok = GuardedCall(pfn);
		r.failed = r.ok && s_bStepFailed;
		r.ms = NowMs() - t0;
		r.code = s_dwExCode;
		r.value = r.ok ? s_szValue : "";
		s_steps.push_back(r);
		HostLog("    %s: %s%s (%.1f ms)", szName, r.failed ? "FAILED: " : "", r.ok ? s_szValue : "EXCEPTION", r.ms);
		return r.ok && !r.failed;
	}
}

// ---------------------------------------------------------------------------------------------------------
// Report

// The stub logger may still hold the file open for writing: open with full sharing.
static std::string ReadFileText(const char* szPath, DWORD* pdwError)
{
	std::string s;
	*pdwError = 0;
	HANDLE h = CreateFileA(szPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(h == INVALID_HANDLE_VALUE)
	{
		*pdwError = GetLastError();
		return s;
	}
	char buf[65536];
	DWORD n = 0;
	while(ReadFile(h, buf, sizeof(buf), &n, NULL) && n > 0)
		s.append(buf, n);
	CloseHandle(h);
	return s;
}

static int s_nSlotProblems = 0;
static void WriteReport(int nSlotProblems)
{
	FILE* f = NULL;
	if(fopen_s(&f, s_szReport, "w") || !f)
	{
		HostLog("cannot write %s", s_szReport);
		return;
	}
	time_t now = time(NULL);
	struct tm lt;
	localtime_s(&lt, &now);
	char szTime[64];
	strftime(szTime, sizeof(szTime), "%Y-%m-%d %H:%M:%S", &lt);

	WIN32_FILE_ATTRIBUTE_DATA fad;
	unsigned long long ullSize = 0;
	char szDllTime[64] = "?";
	if(GetFileAttributesExA(s_szDll, GetFileExInfoStandard, &fad))
	{
		ullSize = ((unsigned long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
		SYSTEMTIME st, stl;
		FileTimeToSystemTime(&fad.ftLastWriteTime, &st);
		SystemTimeToTzSpecificLocalTime(NULL, &st, &stl);
		sprintf_s(szDllTime, "%04u-%02u-%02u %02u:%02u:%02u", stl.wYear, stl.wMonth, stl.wDay, stl.wHour,
		          stl.wMinute, stl.wSecond);
	}

	fprintf(f, "# x64 smoke run\n\n");
	fprintf(f, "Written by `tools/x64-dll/smoke-host` (issue 1007) at %s (local time).\n\n", szTime);
	fprintf(f, "| Item | Value |\n|---|---|\n");
	fprintf(f, "| DLL | `%s` (%llu bytes, modified %s) |\n", s_szDll, ullSize, szDllTime);
	fprintf(f, "| Working directory | `%s` |\n", s_szWorkDir);
	fprintf(f, "| Cache folder answered | `%s` |\n", s_szCacheDir);
	fprintf(f, "| Stub log | `%s` |\n", s_szStubLog);
	fprintf(f, "| Interface slot decode problems | %d |\n\n", nSlotProblems);

	// Steps
	int iFirstFail = -1;
	fprintf(f, "## Steps\n\n| # | Step | Result | ms |\n|---|---|---|---|\n");
	for(size_t i = 0; i < s_steps.size(); ++i)
	{
		const StepResult& r = s_steps[i];
		if(!r.ok && iFirstFail < 0)
			iFirstFail = (int)i;
		if(r.ok)
			fprintf(f, "| %zu | %s | %s: %s | %.1f |\n", i + 1, r.name, r.failed ? "**failed**" : "ok", r.value.c_str(), r.ms);
		else
			fprintf(f, "| %zu | %s | **exception 0x%08lX %s** | %.1f |\n", i + 1, r.name, r.code, ExceptionName(r.code), r.ms);
	}
	fprintf(f, "\n");

	if(!s_strInfoCounts.empty())
		fprintf(f, "## Info counts after CacheGameDatabaseData\n\nDatabase: `%s`\n\n%s\n", s_szDb, s_strInfoCounts.c_str());

	// Exceptions
	fprintf(f, "## Exceptions\n\n");
	if(s_exceptions.empty())
		fprintf(f, "None.\n\n");
	for(size_t i = 0; i < s_exceptions.size(); ++i)
	{
		const ExceptionRecordInfo& e = s_exceptions[i];
		fprintf(f, "### %zu. %s, step `%s`, thread %lu\n\n", i + 1, e.firstChance ? "First chance" : "Ended the step",
		        e.step, e.tid);
		fprintf(f, "- Code: 0x%08lX %s%s%s\n", e.code, ExceptionName(e.code), e.detail.empty() ? "" : ", ", e.detail.c_str());
		fprintf(f, "- At: `%s`\n", e.where.c_str());
		fprintf(f, "- Stack:\n\n```\n");
		for(size_t k = 0; k < e.stack.size(); ++k)
			fprintf(f, "%2zu  %s\n", k, e.stack[k].c_str());
		fprintf(f, "```\n\n");
	}

	// Timeline
	fprintf(f, "## Timeline (first call of each export and interface method, steps, notes)\n\n");
	fprintf(f, "| seq | ms | thread | step | what |\n|---|---|---|---|---|\n");
	for(size_t i = 0; i < s_events.size(); ++i)
	{
		const Event& e = s_events[i];
		std::string what;
		char sz[512];
		switch(e.kind)
		{
		case EV_STEP: what = std::string("**step: ") + e.text + "**"; break;
		case EV_EXPORT: sprintf_s(sz, "export `%s`", HostExportName(e.a)); what = sz; break;
		case EV_IFACE:
		{
			const HostIfaceInfo& fi = HostGetIface(e.a);
			sprintf_s(sz, "%s slot %d `%s::%s`", fi.szName, fi.piSlots[e.b], fi.pMethods[e.b].szClass, fi.pMethods[e.b].szName);
			what = sz;
			break;
		}
		case EV_NOTE: what = "note: " + e.text; break;
		case EV_EXCEPTION: what = "**exception: " + e.text + "**"; break;
		}
		for(size_t k = 0; k < what.size(); ++k)
			if(what[k] == '|')
				what[k] = '/';
		fprintf(f, "| %d | %.3f | %lu | %s | %s |\n", e.seq, e.ms, e.tid, e.step, what.c_str());
	}
	fprintf(f, "\n");

	// Exports
	fprintf(f, "## Host exports (22)\n\n| Export | Calls | First step |\n|---|---|---|\n");
	for(int i = 0; i < HostExportCount(); ++i)
		fprintf(f, "| `%s` | %ld | %s |\n", HostExportName(i), s_aExportStats[i].calls,
		        s_aExportStats[i].calls ? s_aExportStats[i].firstStep : "-");
	fprintf(f, "\n");

	// Interface methods called
	fprintf(f, "## Engine-side interface methods called\n\n");
	fprintf(f, "Slot = vtable index decoded from the compiler's vcall thunk for the host's override; method names from "
	           "the fork's interface headers.\n\n");
	fprintf(f, "| Interface | Slot | Declared in | Method | Calls | First step | Declaration |\n|---|---|---|---|---|---|---|\n");
	for(int i = 0; i < HI_COUNT; ++i)
	{
		const HostIfaceInfo& fi = HostGetIface(i);
		for(int k = 0; k < fi.nMethods; ++k)
		{
			const MethodStat& s = s_aIfaceStats[i][k];
			if(!s.calls)
				continue;
			fprintf(f, "| %s | %d | %s | `%s` | %ld | %s | `%s` |\n", fi.szName, fi.piSlots[k], fi.pMethods[k].szClass,
			        fi.pMethods[k].szName, s.calls, s.firstStep, fi.pMethods[k].szSig);
		}
	}
	fprintf(f, "\n");

	// Stub log
	fprintf(f, "## Closed-library stub log (`%s`, verbatim)\n\n", s_szStubLog);
	DWORD dwErr = 0;
	std::string stub = ReadFileText(s_szStubLog, &dwErr);
	if(stub.empty())
		fprintf(f, "Empty or missing (open error %lu; no stub was called, or the DLL was linked without the stub logger).\n\n", dwErr);
	else
		fprintf(f, "```\n%s%s```\n", stub.c_str(), stub[stub.size() - 1] == '\n' ? "" : "\n");
	fclose(f);
	HostLog("report: %s", s_szReport);
}

namespace { void WriteReportNow() { HostLogFilesClose(); WriteReport(s_nSlotProblems); } }

static void WriteSlotTable()
{
	char szPath[MAX_PATH];
	sprintf_s(szPath, "%s\\interface-slots.tsv", s_szWorkDir);
	FILE* f = NULL;
	if(fopen_s(&f, szPath, "w") || !f)
		return;
	fprintf(f, "interface\tdecl_index\tslot\tdeclared_in\tmethod\tdeclaration\n");
	for(int i = 0; i < HI_COUNT; ++i)
	{
		const HostIfaceInfo& fi = HostGetIface(i);
		for(int k = 0; k < fi.nMethods; ++k)
			fprintf(f, "%s\t%d\t%d\t%s\t%s\t%s\n", fi.szName, k, fi.piSlots[k], fi.pMethods[k].szClass,
			        fi.pMethods[k].szName, fi.pMethods[k].szSig);
	}
	fclose(f);
}

// ---------------------------------------------------------------------------------------------------------

int main(int argc, char** argv)
{
	InitializeCriticalSection(&s_cs);
	QueryPerformanceFrequency(&s_liFreq);
	QueryPerformanceCounter(&s_liStart);
	setvbuf(stdout, NULL, _IONBF, 0);
	// No error dialogs from the system (missing DLLs, GP faults) and none from the CRT: the host must never
	// block or take focus.
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

	const char* szDll = NULL;
	const char* szWork = NULL;
	const char* szReport = NULL;
	const char* szStub = NULL;
	const char* szLua = NULL;
	const char* szDb = NULL;
	for(int i = 1; i < argc; ++i)
	{
		if(!strcmp(argv[i], "--crash-to-dll-filter")) s_bCrashToDllFilter = true;
		else if(i + 1 >= argc) break;
		else if(!strcmp(argv[i], "--dll")) szDll = argv[++i];
		else if(!strcmp(argv[i], "--workdir")) szWork = argv[++i];
		else if(!strcmp(argv[i], "--report")) szReport = argv[++i];
		else if(!strcmp(argv[i], "--stublog")) szStub = argv[++i];
		else if(!strcmp(argv[i], "--lua")) szLua = argv[++i];
		else if(!strcmp(argv[i], "--db")) szDb = argv[++i];
	}
	if(!szDll || !szWork)
	{
		fprintf(stderr, "usage: smoke_host --dll <dll> --workdir <dir> [--db <gameplay db>] [--report <file>] [--stublog <file>]\n");
		return 1;
	}
	if(!GetFullPathNameA(szDll, MAX_PATH, s_szDll, NULL) || !GetFullPathNameA(szWork, MAX_PATH, s_szWorkDir, NULL))
		return 1;
	CreateDirectoryA(s_szWorkDir, NULL);
	if(!SetCurrentDirectoryA(s_szWorkDir))
	{
		fprintf(stderr, "cannot enter %s\n", s_szWorkDir);
		return 1;
	}
	if(szDb)
		GetFullPathNameA(szDb, MAX_PATH, s_szDb, NULL);
	CreateDirectoryA("Logs", NULL);
	CreateDirectoryA("cache", NULL);
	sprintf_s(s_szCacheDir, "%s\\cache\\", s_szWorkDir);
	if(szReport)
		GetFullPathNameA(szReport, MAX_PATH, s_szReport, NULL);
	else
		sprintf_s(s_szReport, "%s\\smoke-report.md", s_szWorkDir);
	if(szStub)
		GetFullPathNameA(szStub, MAX_PATH, s_szStubLog, NULL);
	else
		sprintf_s(s_szStubLog, "%s\\stubcalls.log", s_szWorkDir);
	SetEnvironmentVariableA("CV64_STUBLOG", s_szStubLog);
	DeleteFileA(s_szStubLog);
	fopen_s(&s_pHostLog, "smoke-host.log", "w");
	MultiByteToWideChar(CP_ACP, 0, s_szDll, -1, s_wszDll, MAX_PATH);
	{
		char szLuaPath[MAX_PATH];
		if(szLua)
			GetFullPathNameA(szLua, MAX_PATH, szLuaPath, NULL);
		else
		{
			strcpy_s(szLuaPath, s_szDll);
			char* p = strrchr(szLuaPath, '\\');
			strcpy_s(p ? p + 1 : szLuaPath, MAX_PATH - (p ? (p + 1 - szLuaPath) : 0), "lua51_x64.dll");
		}
		MultiByteToWideChar(CP_ACP, 0, szLuaPath, -1, s_wszLua, MAX_PATH);
	}

	for(int i = 0; i < HI_COUNT; ++i)
	{
		MethodStat z = { 0, 0, NULL };
		s_aIfaceStats[i].assign(HostGetIface(i).nMethods, z);
	}
	s_nSlotProblems = HostResolveSlots();
	const int nSlotProblems = s_nSlotProblems;
	WriteSlotTable();
	HostExportsInit();
	HostLog("smoke host: DLL %s, workdir %s, slot problems %d", s_szDll, s_szWorkDir, nSlotProblems);

	PVOID hVeh = AddVectoredExceptionHandler(1, VectoredHandler);

	// The engine has its Lua before it loads the DLL.
	RunStep("Lua: load lua51_x64.dll, luaL_newstate, luaL_openlibs", StepLua);
	bool ok = true;
	if(szDb)
		ok = RunStep("Database: load CvGameDatabase_x64.dll, Connection::Open(gameplay database)", StepOpenDatabase);
	ok = ok && RunStep("LoadLibrary (DllMain PROCESS_ATTACH)", StepLoad) && s_hDll;
	ok = ok && RunStep("DllGetGameContext", StepGetContext) && s_pContext;
	ok = ok && RunStep("SetDLLIFace", StepSetDLLIFace);
	ok = ok && RunStep("SetGameDatabase", StepSetGameDatabase);
	ok = ok && RunStep("Init", StepInit);
	ok = ok && RunStep("SetEngineUserInterface", StepSetUI);
	ok = ok && RunStep("GetScriptSystemUtility", StepGetUtility);
	ok = ok && RunStep("GetPreGame, setVersionString, GetDLLGUID, GetDLLVersion", StepPreGameVersion);
	ok = ok && RunStep("GetGame, GetGameDeals, CreateDeal (scratch deal)", StepScratchDeal);
	ok = ok && RunStep("pregame reset (setGameMode, setGameType, resetSlots, resetGame, resetPlayers)", StepPreGameReset);
	ok = ok && RunStep("GetDatabaseLoadUtility", StepGetDbUtil) && s_pDbUtil;
	ok = ok && RunStep("FlushGameDatabaseData", StepFlush);
	ok = ok && RunStep("PerformDatabasePostProcessing", StepPost);
	ok = ok && RunStep("CacheGameDatabaseData", StepCache);
	if(szDb)
		ok = ok && RunStep("Info counts (ICvGameContext1::GetNum*Infos against the database)", StepInfoCounts);
	const bool bAllSteps = ok;
	// Unload so the stub logger writes its call counts (DLL_PROCESS_DETACH). Also after a failed step: the
	// unload is itself guarded and its outcome is part of the report.
	if(s_hDll)
		RunStep("FreeLibrary (DllMain PROCESS_DETACH)", StepFree);

	if(hVeh)
		RemoveVectoredExceptionHandler(hVeh);
	HostSetStep("report");
	HostLuaShutdown();
	HostLogFilesClose();
	WriteReport(nSlotProblems);
	if(s_pHostLog)
		fclose(s_pHostLog);
	// The DLL may have left threads or state behind after an exception: leave without running more of it.
	fflush(stdout);
	TerminateProcess(GetCurrentProcess(), bAllSteps ? 0 : 2);
	return bAllSteps ? 0 : 2;
}
