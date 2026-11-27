// srv_remote_main.cpp - the server's remote mode (epic 2300, task 2303):
//     server.exe --remote --section <name> --config <cfg>
// The proxy (the 32-bit DLL in the EXE) creates the vpipc section and starts the server; the server attaches, serves
// one channel per thread (ui, core, aux) and runs the x64 DLL for the engine. Design:
// research/03-dll-engine-boundary/proxy-design.md section 11; wire values: proxy/include/vp_wire.h.
//
// Settings (server.cfg, written by server/run_server.py --remote-config): run_dir, dll, lua_dll, db_dll, loc_dll,
// loc_db, icu_dat, language, game_dir, dll_logs, fail_on_unexpected (0|1), connect_timeout_ms (default 60000);
// render (0|1) and the render_* keys of the renderer inside the server (issue 3301, server/README.md "Renderer").
// Outputs in run_dir: server.log, server-report.txt, proxy-calls.tsv, proxy-calls-per-turn.tsv, Logs\ (the DLL's own
// logs with dll_logs=1), db\ (the server's copy of the game database; kept identical to the engine's database since
// issue 2504, srv_remote_db.cpp).
// Exit codes: 0 orderly (kRt_Shutdown), 1 setup failed (attach, libraries), 2 an exception, 3 the engine went away
// without kRt_Shutdown, 4 fail_on_unexpected and the DLL reached a method without remote treatment.
#include "srv_remote.h"
#include "vp_ids.gen.h"
#include "server_hooks.gen.h"
#include "vpipc/stl.h"
#include <stdio.h>
#include <stdarg.h>
#include <psapi.h>
#include <delayimp.h>
#include <string>
#include <vector>
#include "vp_dbfp.h"
#include "feed/srv_feed.h"
#include "render/srv_render_host.h"   // epic 3300, issue 3301: no DLL type in it
#include "srv_remote_feed.h"         // issue 4452: WriteTotals, the load mark
#include "feed/srv_feed_dbcopy.h"    // issue 4452: the render side's copy of the game database
#include "srv_exe_crash.h"            // ticket 0418: D118's report and popup for an EXE crash
#include "srv_dll_fatal.h"            // ticket 0422: the same for the DLL stopping on a failed check
#include "srv_remote_savemods.h"      // ticket 0744: the end at the DLL's first deserialization error of a load
#include "srv_dll_interface.h"        // ticket 4719: the interface version between the DLL and the server
#include "srv_budget.h"               // ticket 4652: the turn budget (VP64_TURN_BUDGET=1)
#include "../srv_hidden_embark.h"     // ticket 0573: [Performance] HiddenEmbarks into the DLL
#include "../feed/srv_render_events.h"   // ticket 4932: the DLL's render messages (export VP64RenderEvent)

// srv_remote_db.cpp / srv_remote_lua.cpp (issue 2504)
void SrvDbInstall(HMODULE hDll);
void SrvDbSetGame(const Database::Connection* pDb);
void SrvDbReport(FILE* f);
void SrvLuaInstall(HMODULE hDll);
namespace srvrender
{
	bool LogShutdown(DWORD ms);   // ticket 5054 (render/srv_render_common.h): the log writer writes its queue and stops
}

namespace
{
	CRITICAL_SECTION s_csReport;
	std::vector<std::string> s_report;
	volatile bool s_bFailed = false;
	std::string s_strFail;
	volatile LONG s_lReportWritten = 0;
	volatile LONG s_lExiting = 0;
	volatile DWORD s_dwExitingThread = 0;   // bug 0128: the thread inside ReportAndExit
	// Bug 0128 (issue 3309): SrvRemote::Init (srv_rt.cpp) initialises the critical sections of the call counters and the
	// unserved-method table. Before it ran (a library, the localization or the attach failed) the report must not touch
	// them: EnterCriticalSection on a zeroed CRITICAL_SECTION faulted in WriteReport, the unhandled-exception filter
	// re-entered ReportAndExit, which waits for the first caller forever, and the proxy waited out its attach timeout.
	volatile bool s_bRtInit = false;
	volatile bool s_bShutdownDone = false;
	bool s_bFailOnUnexpected = false;
	HMODULE s_hDll = NULL;
	ICvGameContext1* s_pContext = NULL;
	Database::Connection* s_pDb = NULL;
	int s_nDbCopies = 0;
	std::string s_strDbFile;   // issue 4452: the file s_pDb has open (<run>/db/Civ5DebugDatabase.<n>.db), "" without one
	double s_tStart = 0.0;
	std::string s_strSection;

	std::wstring Wide(const std::string& s)
	{
		wchar_t wsz[MAX_PATH * 2];
		MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, wsz, MAX_PATH * 2);
		return wsz;
	}

	// Issue 9008: bind every delay-loaded import of a library at once, right after it is loaded. The lazy path is
	// wrong for float arguments: LLD 19.1.7's x64 delay-load thunk (__tailMerge_*) saves xmm0-xmm3 at [rsp+0..0x40)
	// and calls __delayLoadHelper2 without reserving the callee's 32-byte home area, so the helper's register spills
	// overwrite the saved xmm0/xmm1 [L 2026-11-10: the server's first lua_pushnumber(S, 1.0) stored the bits of a
	// pointer the helper had spilled; the second call was right]. Binding up front means the thunk never runs.
	void BindDelayImports(const char* szDll)
	{
		const HRESULT hr = __HrLoadAllImportsForDll(szDll);
		if(FAILED(hr))
			SrvRemote::ReportAndExit(1, (std::string("cannot bind the delay-loaded imports of ") + szDll).c_str());
		SrvLog("delay-loaded imports of %s bound up front", szDll);
	}

	LONG WINAPI Unhandled(EXCEPTION_POINTERS* pEx)
	{
		SrvLogException(pEx, "remote mode");
		if(s_lExiting && s_dwExitingThread == GetCurrentThreadId())
		{
			// Bug 0128: an exception while this thread writes the report. ReportAndExit would wait for itself forever:
			// end the process now (exit 2, an exception) with what server.log already has.
			SrvLog("remote: LOUD: an exception while writing the report; exit code 2 without the report");
			SrvLogFilesClose();
			fflush(stdout);
			TerminateProcess(GetCurrentProcess(), 2);
		}
		SrvRemote::ReportAndExit(2, "an exception (see server.log)");
	}

	// Ticket 0422: after the dll-fatal report and the helper, on the stopped thread: server-report.txt says why (the DLL's
	// own filter then ends the process with 0xC000001D, so ReportAndExit never runs).
	void DllFatalFail(const char* szWhy)
	{
		SrvRemote::Fail(szWhy);
		SrvRemote::WriteReport(NULL);
	}

	void OnIpcLog(void*, vpipc::LogLevel level, const char* message)
	{
		if(level >= vpipc::LogLevel::Info)
			SrvLog("vpipc: %s", message);
	}
	void OnPeerEvent(void*, vpipc::PeerEvent ev, uint32_t generation)
	{
		static const char* s_names[] = {"attached", "died", "closed", "hung", "recovered", "refused"};
		const unsigned i = (unsigned)ev;
		SrvLog("vpipc: peer %s (generation %u)", i < 6 ? s_names[i] : "?", generation);
	}

	DWORD WINAPI ServiceThread(void* pv)
	{
		const uint32_t uChannel = (uint32_t)(uintptr_t)pv;
		SrvRemote::BindThread(uChannel);
		vpipc::Channel& ch = SrvRemote::Ep()->channel(uChannel);
		ch.set_handlers(&SrvRemote::OnCall, &SrvRemote::OnPost, NULL);
		for(;;)
		{
			const vpipc::Status st = ch.serve(1000);
			if(s_bShutdownDone)
			{
				// kRt_Shutdown: the report is written and the RETURN is sent; the server never outlives the engine.
				SrvLog("remote: shutdown complete, exiting");
				srvrender::LogShutdown(5000);   // ticket 5054
				fflush(stdout);
				TerminateProcess(GetCurrentProcess(), s_bFailed ? 2 : 0);
			}
			if(st == vpipc::Status::Ok || st == vpipc::Status::Timeout)
				continue;
			char sz[256];
			sprintf_s(sz, "the engine is gone: channel %u serve() returned %s", uChannel, vpipc::status_name(st));
			SrvRemote::ReportAndExit(3, sz);   // ticket 0418: ReportAndExit(3) checks the EXE's exit code first
		}
	}

	std::string ReportPath() { return std::string(SrvRunDir()) + "\\server-report.txt"; }

	// ---- epic 3300, issue 3301: the renderer inside server.exe (render/srv_render_host.h) ----
	void RenderLog(const char* line) { SrvLog("%s", line); }

	// Issue 9008's rule for delay-loaded libraries: bind every import at once (the D3D libraries are delay-loaded so that
	// a server with render=0 never loads them; build_server.py RENDER_DELAYLOAD).
	bool BindRenderImports(std::string& why)
	{
		// the names exactly as in the delay-import table: __HrLoadAllImportsForDll compares them case-sensitively
		// [L 2026-11-13: "d3dcompiler_47.dll" gave 0x8007007E; the table says "D3DCOMPILER_47.dll" (llvm-readobj)]
		static const char* s_dlls[] = {"d3d11.dll", "dxgi.dll", "D3DCOMPILER_47.dll"};
		for(const char* d : s_dlls)
		{
			const HRESULT hr = __HrLoadAllImportsForDll(d);
			if(FAILED(hr) && !GetModuleHandleA(d))   // not delay-loaded after all (a static import) is loaded already
			{
				char sz[160];
				sprintf_s(sz, "cannot bind the delay-loaded imports of %s (0x%08lX)", d, hr);
				why = sz;
				return false;
			}
		}
		SrvLog("render: delay-loaded imports of d3d11.dll, dxgi.dll and D3DCOMPILER_47.dll bound up front");
		return true;
	}

	// kRt_Boot with server.cfg render=1: the render thread starts. Ticket 0600 (D118, D125): a refused configuration or a
	// refused start ends the server by D125's route (exit 5: the proxy's report and the popup), never a game without terrain.
	void StartRenderer()
	{
		if(SrvCfgInt("render", 0) == 0)
			return;
		srvrender::HostConfig cfg;
		std::string why;
		auto get = [](const char* key) { return SrvCfgStr(key); };
		if(!srvrender::ParseHostConfig(get, SrvGameDir(), SrvRunDir(), cfg, why))
		{
			SrvRemote::Report("render: LOUD: not started: server.cfg %s", why.c_str());
			SrvRemote::ReportAndExit(5, ("the server's renderer could not start: server.cfg " + why).c_str());   // ticket 0600
		}
		cfg.log = &RenderLog;
		cfg.bindImports = &BindRenderImports;
		cfg.fatal = &SrvRemote::ReportAndExit;   // issue 3704, MC #713: an overlay tap that cannot install ends the server (exit 5)
		if(srvrender::HostStart(cfg, why))
			SrvRemote::Report("render: render thread started (handover %s; render.log in the run folder)", cfg.handover.c_str());
		else   // HostStart wrote the LOUD line and called cfg.fatal (ticket 0600); left: "already started" (a second kRt_Boot)
			SrvRemote::Report("render: not started: %s", why.c_str());
	}
}

// =====================================================================================================================

namespace SrvRemote
{
void Report(const char* szFormat, ...)
{
	char sz[2048];
	va_list v;
	va_start(v, szFormat);
	vsnprintf(sz, sizeof(sz), szFormat, v);
	va_end(v);
	EnterCriticalSection(&s_csReport);
	s_report.push_back(sz);
	LeaveCriticalSection(&s_csReport);
	SrvLog("%s", sz);
}

void Fail(const char* szWhy)
{
	EnterCriticalSection(&s_csReport);
	if(!s_bFailed)
		s_strFail = szWhy;
	s_bFailed = true;
	LeaveCriticalSection(&s_csReport);
	Report("FAILED: %s", szWhy);
}

bool FailOnUnexpected() { return s_bFailOnUnexpected; }

// unexpected_allow=<file> (issue 9008; proxy/tools/remote-allowlist.txt): "Key reason" per line, '#' comments; a key
// matches the unserved method's key, the key without its signature "(...)", or with a trailing '*' a prefix of either.
// Read once at startup; the entries are printed in the report.
static std::vector<std::string> s_allow;
static void LoadAllowList()
{
	const std::string path = SrvCfgStr("unexpected_allow");
	if(path.empty())
		return;
	FILE* f = NULL;
	if(fopen_s(&f, path.c_str(), "r") || !f)
		ReportAndExit(1, ("unexpected_allow: cannot open " + path).c_str());
	char line[1024];
	while(fgets(line, sizeof(line), f))
	{
		const char* p = line;
		while(*p == ' ' || *p == '\t')
			++p;
		if(!*p || *p == '#' || *p == '\r' || *p == '\n')
			continue;
		const char* e = p;
		while(*e && *e != ' ' && *e != '\t' && *e != '\r' && *e != '\n')
			++e;
		s_allow.push_back(std::string(p, e));
	}
	fclose(f);
	Report("allow-list: %s, %zu entries (fail_on_unexpected does not stop at these)", path.c_str(), s_allow.size());
	for(size_t i = 0; i < s_allow.size(); ++i)
		Report("allow-list: %s", s_allow[i].c_str());
}

const char* UnexpectedAllowed(const std::string& key)
{
	const std::string bare = key.substr(0, key.find('('));
	for(size_t i = 0; i < s_allow.size(); ++i)
	{
		const std::string& r = s_allow[i];
		if(!r.empty() && r.back() == '*')
		{
			const std::string pre = r.substr(0, r.size() - 1);
			if(key.compare(0, pre.size(), pre) == 0 || bare.compare(0, pre.size(), pre) == 0)
				return r.c_str();
		}
		else if(r == key || r == bare)
			return r.c_str();
	}
	return NULL;
}
ICvGameContext1* Context() { return s_pContext; }

// The report's head: the summaries and the "result: ..." line, into server.log (Report). The proxy's D119 report reads
// that line from server.log's tail for the popup's first line (ticket 0511, proxy_report.cpp ServerReason).
static bool ReportHead()
{
	PROCESS_MEMORY_COUNTERS pmc;
	GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
	const bool bRt = s_bRtInit;   // bug 0128: the call counters exist only after SrvRemote::Init
	SrvBudget::Final("report");   // ticket 4652: the last, partial turn (a no-op when off or after kRt_Shutdown's)
	if(!bRt)
		Report("report: written before the attach (SrvRemote::Init did not run): no call counts, no call tables");
	{
		const std::string he = SrvHiddenEmbark::Summary();   // ticket 0573 ("" without the export)
		if(!he.empty())
			Report("%s", he.c_str());
		const std::string re = SrvRenderEvents::Summary();   // ticket 4932 ("" when the DLL sends nothing)
		if(!re.empty())
			Report("%s", re.c_str());
	}
	Report("result: %s%s%s; remote mode, section %s, %.1f s; peak working set %.0f MB; scratch Lua errors %ld; "
	       "methods without remote treatment called %ld",
	       s_bFailed ? "FAILED" : "passed", s_bFailed ? ": " : "", s_bFailed ? s_strFail.c_str() : "", s_strSection.c_str(),
	       SrvNow() - s_tStart, pmc.PeakWorkingSetSize / 1048576.0, LuaScratchErrors(), bRt ? UnservedCount() : 0L);
	return bRt;
}

bool WriteReport(std::string* pstrPath)
{
	if(pstrPath)
		*pstrPath = ReportPath();
	if(InterlockedExchange(&s_lReportWritten, 1))
		return false;
	const bool bRt = ReportHead();
	FILE* f = NULL;
	if(!fopen_s(&f, ReportPath().c_str(), "w") && f)
	{
		EnterCriticalSection(&s_csReport);
		for(size_t i = 0; i < s_report.size(); ++i)
			fprintf(f, "%s\n", s_report[i].c_str());
		LeaveCriticalSection(&s_csReport);
		fprintf(f, "\n");
		ObjectsReport(f);
		CanonReport(f);  // issue 2700
		LuaReport(f);
		SrvDbReport(f);
		SrvLogReport(f);  // issue 2800
		SrvRemoteFeed::WriteTotals(f);   // issue 4452 (MC #2105 Q2): the feed's totals had no caller before
		if(bRt)
			ReportCounts(f);
		fclose(f);
	}
	if(bRt)
		WriteCallTables();
	return true;
}

void ReportAndExit(int code, const char* szWhy)
{
	if(InterlockedExchange(&s_lExiting, 1))
	{
		// Another thread is writing the report and will end the process.
		for(;;)
			Sleep(1000);
	}
	s_dwExitingThread = GetCurrentThreadId();   // bug 0128: Unhandled ends the process if this thread faults
	// Ticket 0418: exit 3 = the engine is gone (a channel's serve() or a call the DLL made saw PeerDied and kin). The EXE
	// may have crashed, taking the proxy, which writes every other D119 report, with it: the server checks the EXE's exit
	// code and, for an exception code on the shipped layout, writes the report (stage exe-crash) and starts the popup.
	// Here, inside the one exiting thread, so no other thread ends the process while the report is written [L 2026-11-16:
	// with the check in the service thread only, the core thread's PeerDied in kRtE_StreamRead ended the process mid-report].
	std::string strWhy = szWhy ? szWhy : "";
	if(code == 3)
	{
		const std::string strExe = SrvExeCrash::OnPeerGone();
		if(!strExe.empty())
			strWhy += "; " + strExe;
	}
	szWhy = strWhy.c_str();
	// Ticket 0641 (D118): the server's own reason for the proxy's D119 report and popup (srv_exe_crash.h FatalRecord); first,
	// before the larger server-report.txt, so that it is on disk whatever happens after. Not for 0, 3 and save-variant codes.
	if(code != 0 && code != 3)
		SrvExeCrash::WriteFatalRecord(code, strWhy);
	if(code != 0)
		Fail(szWhy);
	else
		Report("exit: %s", szWhy);
	srvrender::HostAbandon();   // issue 3301: the frame section says STOPPED (the process ends without joining the thread)
	// Ticket 0641: on the shipped layout (vpproxy.ini pack=1, SrvExeCrash::ShippedLayout) a deliberate end exits at once
	// after server.log's result line: server-report.txt and the call tables are six files, 2-8 s each on a slow disk (26 s
	// on F: in 0641's live arm B), while the EXE shows a world without the server's picture and still takes input. The
	// player's report attaches server.log, not those files. Development layouts (pack=0) keep the full report.
	std::string strShipped;
	if(code != 0 && code != 3 && SrvExeCrash::ShippedLayout(&strShipped))
	{
		if(!InterlockedExchange(&s_lReportWritten, 1))
			ReportHead();
		SrvLog("remote: the shipped layout (%s): server-report.txt and the call tables are not written; the server ends at "
		       "once (ticket 0641)",
		       strShipped.c_str());
	}
	else
		WriteReport(NULL);
	SrvLog("remote: exit code %d (%s)", code, szWhy);
	srvrender::LogShutdown(5000);   // ticket 5054: the queued lines reach server.log and render.log before the end
	SrvLogFilesClose();
	fflush(stdout);
	TerminateProcess(GetCurrentProcess(), (UINT)code);
	for(;;)
		Sleep(1000);
}

// ---- issue 0178: UI records of the proxy's Lua bridge (proxy/src/proxy_ui_bridge.h) ------------------------------
// Decode, counters and the event's layout: SrvRemote::UiRec (srv_remote.h; issue 4754 kept kind 6's player and arrival).
namespace
{
UiRec::Tally s_uiTally = {};
volatile LONGLONG s_llUiSummaryMs = 0;   // the last "so far" line (a harness stop never reaches kRt_Shutdown's line)
}  // namespace

void UiRecord(vpipc::Reader& args, uint32_t uChannel)
{
	uint32_t n = 0;
	const uint8_t* p = args.bytes(&n);
	UiRec::Fields r;
	if(!UiRec::Decode(args.ok() ? p : NULL, n, r))
	{
		if(s_uiTally.Bad() <= 5)
			SrvLog("ui records: a bad record on channel %u (%u bytes, version %u): ignored", uChannel, n, (unsigned)r.version);
		return;
	}
	const long kindCount = s_uiTally.Add(r);
	const long total = s_uiTally.total;
	if(kindCount == 1 || total % 1000 == 0)
		SrvLog("ui records: #%ld kind %u (first of this kind: %s) flags 0x%x a %d %d %d %d seq %u gen %u, channel %u", total,
		       (unsigned)r.kind, kindCount == 1 ? "yes" : "no", r.flags, r.a[0], r.a[1], r.a[2], r.a[3], r.seq, r.gen, uChannel);
	const LONGLONG llNow = (LONGLONG)GetTickCount64();
	if(r.kind == UiRec::kKindFrameMark && llNow - s_llUiSummaryMs >= 1000)   // the counts before this mark, at most 1/s
	{
		s_llUiSummaryMs = llNow;
		SrvLog("%s (so far, at frame mark %d)", s_uiTally.Summary().c_str(), r.a[0]);
	}
	SrvFeed::Push(UiRec::ToEvent(r));   // a no-op while the feed is off (render=0)
}

std::string UiRecordSummary()
{
	return s_uiTally.Summary();
}

// Runtime methods of the proxy (vp_wire.h), except the Lua ones (srv_remote_lua.cpp).
bool HandleRt(uint32_t method, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	switch(method)
	{
	case vpw::kRt_Boot:
	{
		const uint32_t uProxyPid = args.u32();
		std::string strDll = vpipc::read_string(args);
		if(!args.ok())
			return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "kRt_Boot: bad arguments");
		SrvRemote::LockOpen(uProxyPid);  // issue 2700: the proxy's lock-want words (absent with local_answers=0)
		SrvBudget::OpenBlock(uProxyPid); // ticket 4652: the proxy's turn block (only with VP64_TURN_BUDGET=1)
		SrvExeCrash::NoteBoot();         // ticket 0418: the report's "time since kRt_Boot"
		if(strDll.empty())
			strDll = SrvCfgStr("dll");
		const vpipc::Handle null_ = {0u, 0};
		std::string msg;
		if(s_pContext)
			msg = "already booted";
		else
		{
			s_hDll = LoadLibraryExW(Wide(strDll).c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
			if(!s_hDll)
			{
				char sz[256];
				sprintf_s(sz, "cannot load the DLL (error %lu)", GetLastError());
				msg = sz;
			}
			if(s_hDll)
			{
				// Ticket 4719: the interface version, before anything is installed into the DLL. A refusal answers
				// kRt_Boot with ok = 0 and both numbers; the proxy's dll-boot failure gives the D118/D119 report and popup.
				// A DLL without the number that this build accepts gets a LOUD line here (srv_dll_interface.h).
				const SrvDllInterface::Result iv = SrvDllInterface::Check(s_hDll);
				Report("boot: %s", iv.text.c_str());
				if(!iv.ok)
					msg = iv.text;
			}
			if(s_hDll && msg.empty())
			{
				// Issue 2504: the DLL's writes to its game database are recorded (srv_remote_db.cpp) and the chunks it
				// loads on its Lua threads run in the engine VM (srv_remote_lua.cpp): import slots of this x64 image only.
				SrvDbInstall(s_hDll);
				SrvLuaInstall(s_hDll);
				Report("%s", SrvDllFatal::Install(s_hDll).c_str());  // ticket 0422: a failed check -> report, popup
				typedef ICvGameContext1* (*PFN)();
				PFN pfn = reinterpret_cast<PFN>(GetProcAddress(s_hDll, "DllGetGameContext"));
				s_pContext = pfn ? pfn() : NULL;
				if(!s_pContext)
					msg = "DllGetGameContext returned NULL or is missing";
				else
				{
					Report("%s", SrvHiddenEmbark::Apply(s_hDll, true).c_str());   // ticket 0573
					Report("%s", SrvRenderEvents::Apply(s_hDll).c_str());         // ticket 4932
				}
			}
		}
		const bool ok = s_pContext != NULL;
		Report("boot: proxy pid %u, dll %s: %s%s", uProxyPid, strDll.c_str(), ok ? "context " : "FAILED: ",
		       ok ? "registered" : msg.c_str());
		if(!ok)
			Fail(("kRt_Boot: " + msg).c_str());
		else
			StartRenderer();   // issue 3301: server.cfg render=1
		ret.u32(ok ? 1u : 0u);
		write_string(ret, ok ? std::string("ok") : msg);
		ret.handle(ok ? SrvRt::Register(s_pContext, vpk_most_derived(kT_ICvGameContext1)) : null_);
		ret.u32(GetCurrentProcessId());
		return true;
	}
	case vpw::kRt_Shutdown:
	{
		const uint32_t uReason = args.u32();
		Report("shutdown: kRt_Shutdown, reason %u", uReason);
		Report("%s", UiRecordSummary().c_str());   // issue 0178
		// issue 3301: the renderer stops before the report (its summary line goes into it)
		srvrender::HostStop("kRt_Shutdown");
		{
			const std::string strRender = srvrender::HostSummary();
			if(!strRender.empty())
				Report("%s", strRender.c_str());
		}
		LocShutdown();
		SrvBudget::Final("kRt_Shutdown");   // ticket 4652 (a no-op when off)
		std::string path;
		WriteReport(&path);
		write_string(ret, path);
		s_bShutdownDone = true;	// the service thread exits after the RETURN is sent
		return true;
	}
	case vpw::kRt_CoreLockYield:
		ret.i32(SrvRemote::CoreLockYield());
		return true;
	case vpw::kRt_LockSync:  // issue 2700: the prologue (SrvRemote::LockSync) did the work
		return true;
	case vpw::kRt_Canon:
	{
		const uint32_t uGen = args.u32();
		const uint32_t uCode = args.u32();
		const int32_t iIndex = args.i32();
		if(!args.ok())
			return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "kRt_Canon: bad arguments");
		vpipc::Handle h;
		std::string err;
		if(!SrvRemote::Canon(uGen, uCode, iIndex, h, err))
			return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, ("kRt_Canon: " + err).c_str());
		ret.handle(h);
		return true;
	}
	case vpw::kRt_CoreLockResume:
	{
		const int32_t d = args.i32();
		if(!args.ok() || d < 0 || d > 64)
			return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "kRt_CoreLockResume: bad arguments");
		SrvRemote::CoreLockResume(d);
		return true;
	}
	case vpw::kRt_Stats:
		write_string(ret, StatsTable());
		return true;
	case vpw::kRt_StreamOpen:
	{
		uint32_t n = 0;
		const uint8_t* p = args.bytes(&n);
		if(!args.ok())
			return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "kRt_StreamOpen: bad arguments");
		ret.u32(StreamOpen(p, n));
		return true;
	}
	case vpw::kRt_StreamOpenPull:
	{
		const uint32_t token = args.u32();
		if(!args.ok())
			return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "kRt_StreamOpenPull: bad arguments");
		ret.u32(SrvRemote::StreamOpenPull(token));
		return true;
	}
	default:
		return false;
	}
}
}  // namespace SrvRemote

// =====================================================================================================================
// ICvGameContext1::SetGameDatabase (contract D; issue 2504): the proxy names the engine's database file (its own file,
// a BinaryIO snapshot of an in-memory database, or vpproxy.ini db=) and sends the fingerprint of the engine's database
// (proxy_db.cpp). The file is copied into the run folder, opened with CvGameDatabase_x64 and fingerprinted; a
// difference from the engine's fingerprint fails the run (exit 4 under fail_on_unexpected). The DLL's later writes
// are replayed on the engine's database (srv_remote_db.cpp). No database: SetGameDatabase(NULL), the copy is closed.
// A read-only engine database file (flag 2): the copy is opened read-only, so the DLL's writes fail here as they fail
// on the engine's database in the EXE (the 32-bit headless host's copy of a read-only pinned file is read-only).
// CALL: self, u32 flags (1 a database, 2 its file is read-only, 4 the engine has a database attached as Localization),
// str path, bytes fingerprint, str how, and with flag 4: str the Localization file (issue 2802).
// RETURN: u32 ok, str message.
// Issue 2802: the copy gets the engine's Localization attachment before the DLL sees it (LocAttachGameDb, srv_loc.cpp;
// the DLL's GetDiploResponse joins Language_en_US); the fingerprints cover main only, so they are not affected.

bool ServerHook_ICvGameContext3_SetGameDatabase(ICvGameContext3* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	const uint32_t uFlags = args.u32();
	const bool bHas = (uFlags & 1u) != 0, bReadOnly = (uFlags & 2u) != 0;
	const std::string path = vpipc::read_string(args);
	uint32_t nFp = 0;
	const uint8_t* pFp = args.bytes(&nFp);
	vpdb::Fingerprint engineFp;
	const bool bFp = pFp && engineFp.Parse(pFp, nFp);
	const std::string how = vpipc::read_string(args);
	const std::string engineLoc = (uFlags & 4u) ? vpipc::read_string(args) : std::string();
	if(!args.ok())
		return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "SetGameDatabase: bad arguments");
	Database::Connection* pOld = s_pDb;
	std::string msg;
	bool ok = true;
	if(!bHas)
	{
		SrvDbSetGame(NULL);
		self->SetGameDatabase(NULL);
		s_pDb = NULL;
		s_strDbFile.clear();   // issue 4452
		msg = "SetGameDatabase(NULL)";
		SrvRemote::Report("database: SetGameDatabase(NULL)");
	}
	else if(path.empty())
	{
		ok = false;
		msg = "the proxy could not name the engine's database (" + how + ")";
	}
	else
	{
		const std::string dir = std::string(SrvRunDir()) + "/db";
		CreateDirectoryA(dir.c_str(), NULL);
		char sz[64];
		sprintf_s(sz, "/Civ5DebugDatabase.%d.db", ++s_nDbCopies);
		const std::string copy = dir + sz;
		const double t0 = SrvNow();
		Database::Connection* pDb = NULL;
		if(!CopyFileA(path.c_str(), copy.c_str(), FALSE))
		{
			ok = false;
			msg = "cannot copy " + path;
		}
		else
		{
			SetFileAttributesA(copy.c_str(), FILE_ATTRIBUTE_NORMAL);
			pDb = new Database::Connection();
			if(!pDb->Open(copy.c_str(), bReadOnly ? Database::OPEN_READONLY : Database::OPEN_READWRITE))
			{
				delete pDb;
				pDb = NULL;
				ok = false;
				msg = "cannot open " + copy;
			}
		}
		if(pDb)
		{
			const vpdb::Fingerprint mine = vpdb::Compute(*pDb);
			std::vector<std::string> lines;
			const size_t nDiff = bFp ? vpdb::Compare(engineFp, mine, lines, 20, "engine", "server") : 1;
			SrvRemote::Report("database: %s (%s) copied to %s and opened; engine %u tables %llu rows digest %016llx, copy %u "
			                  "tables %llu rows digest %016llx: %s (%.2f s)", path.c_str(), how.c_str(), copy.c_str(),
			                  (unsigned)engineFp.tables.size(), (unsigned long long)engineFp.TotalRows(),
			                  (unsigned long long)engineFp.Digest(), (unsigned)mine.tables.size(),
			                  (unsigned long long)mine.TotalRows(), (unsigned long long)mine.Digest(),
			                  nDiff == 0 ? "IDENTICAL" : "DIFFERENT", SrvNow() - t0);
			for(size_t i = 0; i < lines.size(); ++i)
				SrvRemote::Report("database: LOUD: %s", lines[i].c_str());
			if(!bFp)
				SrvRemote::Report("database: LOUD: the proxy sent no fingerprint");
			// Issue 2802: no loc_db in server.cfg (the shipped vpproxy.ini has none) and the engine has its localization
			// database attached: the dictionary is initialised from that file now. The EXE calls SetDLLIFace,
			// SetGameDatabase, then Init (live vpproxy.log, 2026-11-10), so this is still before the DLL's Init.
			if(!LocActive() && !engineLoc.empty())
			{
				std::string whyInit;
				const std::string strIcu = SrvCfgStr("icu_dat", (std::string(SrvGameDir()) + "\\Locale\\icudt44l.dat").c_str());
				if(LocStartup(SrvCfgStr("loc_dll").c_str(), engineLoc.c_str(), strIcu.c_str(), SrvCfgStr("language", "en_US").c_str(), whyInit))
				{
					BindDelayImports("CvLocalization_x64.dll");
					SrvRemote::Report("localization: from the engine's attached database: %s", whyInit.c_str());
				}
				else
				{
					SrvRemote::Report("localization: LOUD: from the engine's attached database %s: %s", engineLoc.c_str(), whyInit.c_str());
					SrvRemote::Fail(("localization: " + whyInit).c_str());
				}
			}
			else if(!LocActive())
				SrvRemote::Report("localization: LOUD: no loc_db and the engine has no localization database attached: the "
				                  "DLL's text lookups return nothing");
			std::string whyLoc;
			const bool bLoc = LocAttachGameDb(*pDb, engineLoc.c_str(), whyLoc);
			SrvRemote::Report("database: localization attach%s: %s", bLoc ? "" : " LOUD", whyLoc.c_str());
			SrvDbSetGame(pDb);
			self->SetGameDatabase(pDb);
			s_pDb = pDb;
			s_strDbFile = copy;   // issue 4452
			if(nDiff)
			{
				ok = false;
				msg = "the server's copy differs from the engine's database (" + how + ")";
			}
			else if(!bLoc)
			{
				ok = false;
				msg = "copy identical, but the localization database could not be attached as the engine's: " + whyLoc;
			}
			else
				msg = "copy identical to the engine's database; " + whyLoc;
		}
	}
	if(pOld && pOld != s_pDb)
		delete pOld;
	ret.u32(ok ? 1u : 0u);
	vpipc::write_string(ret, msg);
	if(!ok)
	{
		SrvRemote::Fail(("SetGameDatabase: " + msg).c_str());
		if(SrvRemote::FailOnUnexpected())
			SrvRemote::ReportAndExit(4, ("SetGameDatabase: " + msg).c_str());
	}
	return true;
}

// =====================================================================================================================
// Issue 4452 (epic 0375; MC #2105 Q2 and Q5): the load mark of the unit feed and the render side's database copy.
// ICvPlayer1::SetupGraphical is what the EXE calls for players 0..63 at every load and new game, before the units'
// graphics are set up (unit-formations.md 12.7.3 S2: 64 calls in every logged load). The first call of a load (the
// first ever, or a player id not above the previous call's) pushes kLoadBegin (SrvRemoteFeed::OnSetupGraphical) and,
// before it is forwarded, gives the render side a consistent copy of the game database (srv_feed_dbcopy.h: SQLite's
// backup API on a second connection, never immutable=1 on the file the DLL writes) through SrvFeed::SetUnitDatabase.
// The copy is made again only when the database changed since the last one (PRAGMA data_version) or another
// SetGameDatabase named another file. Only with render_units=1 (SrvFeed::UnitsEnabled); a failure is LOUD and leaves
// the previous copy (the render side's pre-check then fails by D125's route if it has none).
namespace
{
	SrvFeed::DbCopier s_unitDb;
	uint32_t s_unitDbGen = 0;
	volatile LONG s_lUnitDbFailures = 0;

	// ticket 4875: the load mark of a server without the unit feed whose model module wants the copy (the units' own mark,
	// SrvRemoteFeed::OnSetupGraphical, runs only with UnitsEnabled): the first call of a load is the first ever or one whose
	// player id is not above the previous call's
	volatile LONG s_lModelSetupPlayer = 0x7FFFFFFF;
	bool ModelDatabaseLoadMark(int iPlayer)
	{
		const LONG prev = InterlockedExchange(&s_lModelSetupPlayer, (LONG)iPlayer);
		return iPlayer <= (int)prev;
	}

	void UnitDatabaseAtLoad()
	{
		if(!SrvFeed::DatabaseCopyWanted())   // ticket 4875: one shared copy, whichever asks (units or the model module)
			return;
		if(s_strDbFile.empty())
		{
			if(InterlockedIncrement(&s_lUnitDbFailures) <= 5)
				SrvRemote::Report("units: LOUD: a load began without a game database file: no copy for the render side");
			return;
		}
		char sz[96];
		sprintf_s(sz, "/units-%d.%u.db", s_nDbCopies, s_unitDbGen + 1);
		const std::string dst = std::string(SrvRunDir()) + "/db" + sz;
		SrvFeed::DbCopyResult r;
		if(!s_unitDb.Copy(Wide(s_strDbFile), Wide(dst), false, r))
		{
			if(InterlockedIncrement(&s_lUnitDbFailures) <= 5)
				SrvRemote::Report("units: LOUD: the render side's copy of %s failed: %s", s_strDbFile.c_str(), r.why.c_str());
			return;
		}
		if(r.unchanged)
		{
			SrvLog("units: game database unchanged since copy %u (data_version): no new copy", s_unitDbGen);
			return;
		}
		SrvFeed::SetUnitDatabase(Wide(dst), ++s_unitDbGen);
		SrvRemote::Report("units: game database copied for the render side: %s (%d pages, %d busy steps, %.1f ms, "
		                  "longest step %.1f ms, gen %u)",
		                  dst.c_str(), r.pages, r.busySteps, r.ms, r.stepMsMax, s_unitDbGen);
	}
}

bool ServerHook_ICvPlayer3_SetupGraphical(ICvPlayer3* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	// the player id for the load marks and (ticket 0529) the feed's load snapshot after the forwarded call
	int iPlayer = -1;
	if(SrvFeed::Enabled() || SrvFeed::UnitsEnabled() || SrvFeed::ModelDatabaseWanted())
	{
		try
		{
			iPlayer = (int)self->GetID();   // pure (pure-methods.tsv)
		}
		catch(...)
		{
			iPlayer = -1;
		}
	}
	if(SrvFeed::UnitsEnabled())
	{
		if(SrvRemoteFeed::OnSetupGraphical(iPlayer))
			UnitDatabaseAtLoad();
	}
	else if(SrvFeed::ModelDatabaseWanted())   // ticket 4875: the copy for the model library without the unit feed
	{
		if(ModelDatabaseLoadMark(iPlayer))
		{
			// ticket 4882: without the unit feed nothing else pushes kLoadBegin, so the model bits were never cleared at a
			// load (4875's smoke s3 point 7); the unit feed's own mark pushes it when the units are on (never twice)
			const uint32_t seq = SrvFeed::PushModelLoadBegin(iPlayer);
			SrvLog("feed: models: a load begins at ICvPlayer1::SetupGraphical(%d): kLoadBegin pushed (seq %u)", iPlayer, seq);
			UnitDatabaseAtLoad();
		}
	}
	const bool bServed = ServerFwd_ICvPlayer3_SetupGraphical(self, args, ret, ch);
	// ticket 0529: the last call of a load's 0..63 takes the plot snapshot under the load screen (returns at once with the
	// feed off and for every other call)
	SrvRemoteFeed::AfterSetupGraphical(iPlayer);
	return bServed;
}

// Ticket 0488: a write FIFileSystem::Create that was refused or failed (in practice the save's embedded database, written
// by CvGame::readSaveGameDB inside Game::Read) ends the server with its report before the DLL reads on misaligned; the
// proxy's failed call takes the D118/D119 path (error report and popup), as the 4253 boot guard's exit 1 does.
static void FileCreateFail(const char* szWhy)
{
	SrvRemote::ReportAndExit(1, szWhy);
}

// =====================================================================================================================

int RemoteMain(const char* szSection)
{
	s_tStart = SrvNow();
	s_strSection = szSection;
	InitializeCriticalSection(&s_csReport);
	SetUnhandledExceptionFilter(Unhandled);
	// Ticket 0422: the dll-fatal report names the calls the stopped thread served and updates server-report.txt
	SrvDllFatal::SetContextText(&SrvRemote::ServedCallsText, &SrvRemote::LuaCallText);
	SrvDllFatal::SetFailHook(&DllFatalFail);
	SrvSetFileCreateFailHook(&FileCreateFail);
	// Ticket 0744 (K31): the DLL's first deserialization error of a save's read ends the server (exit 6, the
	// save-mod-mismatch record) before the misread damages memory; direct mode sets no hook and reads on
	SrvSaveMods::SetEndHook(&SrvRemote::ReportAndExit);
	s_bFailOnUnexpected = SrvCfgInt("fail_on_unexpected", 0) != 0;
	// epic 3300: the renderer's event feed (feed/srv_feed.h) is on with the renderer (render=1) or alone (feed=1, tests)
	SrvFeed::SetEnabled(SrvCfgInt("render", 0) != 0 || SrvCfgInt("feed", 0) != 0);
	SrvRemote::LockInit();
	SrvRemote::Report("server: remote mode, section %s, run %s, game dir %s, fail_on_unexpected %d, pid %lu", szSection,
	                  SrvRunDir(), SrvGameDir(), s_bFailOnUnexpected ? 1 : 0, GetCurrentProcessId());
	SrvRemote::LoadAllowList();

	// Libraries, as the direct server: Lua (the DLL's and the scratch states' one copy), the database layer, and the
	// localization before the DLL's Init (issue 1704).
	const std::string strLua = SrvCfgStr("lua_dll"), strDbDll = SrvCfgStr("db_dll");
	if(!LoadLibraryExW(Wide(strLua).c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH))
		SrvRemote::ReportAndExit(1, ("cannot load " + strLua).c_str());
	if(!LoadLibraryExW(Wide(strDbDll).c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH))
		SrvRemote::ReportAndExit(1, ("cannot load " + strDbDll).c_str());
	BindDelayImports("lua51_x64.dll");
	BindDelayImports("CvGameDatabase_x64.dll");
	const std::string strLocDb = SrvCfgStr("loc_db");
	if(!strLocDb.empty())
	{
		std::string why;
		const std::string strIcu = SrvCfgStr("icu_dat", (std::string(SrvGameDir()) + "\\Locale\\icudt44l.dat").c_str());
		if(!LocStartup(SrvCfgStr("loc_dll").c_str(), strLocDb.c_str(), strIcu.c_str(), SrvCfgStr("language", "en_US").c_str(), why))
			SrvRemote::ReportAndExit(1, ("localization: " + why).c_str());
		SrvRemote::Report("localization: %s", why.c_str());
		BindDelayImports("CvLocalization_x64.dll");
	}
	else
		SrvRemote::Report("localization: no loc_db: initialised at SetGameDatabase from the engine's attached localization "
		                  "database, if it has one (issue 2802)");

	// Attach to the proxy's section.
	vpipc::EndpointConfig cfg;
	strncpy_s(cfg.sectionName, szSection, _TRUNCATE);
	cfg.appSchemaHash = kAppSchemaHash;
	if(SrvCfgInt("reply_spin_us", 0) > 0) cfg.wait.replySpinUs = (uint32_t)SrvCfgInt("reply_spin_us", 0);
	if(SrvCfgInt("idle_spin_us", 0) > 0) cfg.wait.idleSpinUs = (uint32_t)SrvCfgInt("idle_spin_us", 0);
	cfg.onLog = OnIpcLog;
	cfg.onPeerEvent = OnPeerEvent;
	vpipc::Endpoint* pEp = NULL;
	const uint32_t uTimeout = (uint32_t)SrvCfgInt("connect_timeout_ms", 60000);
	const double t0 = SrvNow();
	const vpipc::Status st = vpipc::Endpoint::attach_server(cfg, &pEp, uTimeout);
	if(st != vpipc::Status::Ok || !pEp)
	{
		char sz[1024];
		sprintf_s(sz, "attach_server(%s): %s: %s", szSection, vpipc::status_name(st), vpipc::Endpoint::last_error_text());
		SrvRemote::ReportAndExit(1, sz);
	}
	SrvRemote::Report("attach: section %s, %u channels, peer pid %u, schema hash 0x%016llx, %.3f s", szSection,
	                  pEp->channel_count(), pEp->peer_pid(), (unsigned long long)kAppSchemaHash, SrvNow() - t0);
	SrvRemote::Init(pEp);
	SrvBudget::Init();   // ticket 4652: reads VP64_TURN_BUDGET; off: nothing else happens
	s_bRtInit = true;   // bug 0128: from here the report includes the call counts and tables
	SrvExeCrash::OnAttach(pEp->peer_pid());   // ticket 0418: the EXE's handle, for its exit code

	// One service thread per channel; the DLL runs on the thread of the channel the engine thread is bound to.
	std::vector<HANDLE> threads;
	for(uint32_t i = 0; i < pEp->channel_count(); ++i)
	{
		HANDLE h = CreateThread(NULL, 64 * 1024 * 1024, ServiceThread, (void*)(uintptr_t)i, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
		if(!h)
			SrvRemote::ReportAndExit(1, "CreateThread failed");
		threads.push_back(h);
	}
	WaitForMultipleObjects((DWORD)threads.size(), &threads[0], TRUE, INFINITE);
	SrvRemote::ReportAndExit(3, "every service thread ended");
}
