// proxy_rt.cpp - the x86 proxy runtime (epic 2300, task 2302). Hand-written.
// Implements proxy_rt.h (the API the generated proxy code calls), the boot in DllGetGameContext, thread binding,
// the wrapper and engine-object tables, interning, stream sessions, the served-call handler, the failure policy,
// counters and the log. Design: research/03-dll-engine-boundary/proxy-design.md section 11; configuration:
// vpproxy.ini (team-2300 contract section F).
//
// Calls are staged: Begin() returns a per-thread, per-depth staging writer, End() copies the staged arguments into
// the channel (begin_call) and waits (end_call). Staging lets a codec make a call of its own between Begin and End
// (StreamInArg opens a stream session with kRt_StreamOpen there) without breaking the channel's one-writer rule;
// the cost is one copy of the arguments per call.
//
// Issue 2502 (epic 2500, the real EXE): thread binding by role, the D28 layout and per-boot run folders, the off
// switch (mode=off), the strict allow-list, per-interface counters, frames and snapshots, the Update hook of the Lua
// channel (proxy_update.cpp). The parts that need no engine are in proxy_rt_logic.cpp (unit-tested on their own).
#include "proxy_rt_internal.h"
#include "proxy_budget.h"  // ticket 4652: the turn budget (VP64_TURN_BUDGET=1; off: one bool test per hook)
#include "proxy_ifaces.gen.h"
#include "proxy_rt_logic.h"
#include "proxy_guard.h"
#include "proxy_local.h"
#include "proxy_shell.h"
#include "proxy_pack.h"
#include "proxy_purity.h"
#include "proxy_session.h"  // issue 3521 (D119, D124): the masks for the report, the popup helper
#include "proxy_report.h"   // issue 3521 (D119, D120, D122): the local error report
#include "proxy_crash_filter.h"  // ticket 0419: the chained exception filter (exe-crash.txt)
#include "proxy_queue_guard.h"   // ticket 0492: the UI queue guard's counters in the end lines and the report
#include "proxy_luaexec.h"       // ticket 0805: the Lua values of this image made inert at the orderly shutdown
#include "vp64_options.h"   // the options file's path for the report
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <new>
#include <exception>

namespace ProxyCombatHold  // proxy_combat_hold.cpp (ticket 4773, route S): the hold on a visualised fight's record
{
void AfterServed(uint32_t method, const uint8_t* ret, uint32_t retSize);
}

namespace ProxyMinimapFog  // proxy_minimap_fog.cpp (ticket 0727): the minimap's unexplored cells after a re-fog
{
void AfterServed(uint32_t method, const uint8_t* args, uint32_t argsSize);
}

namespace ProxyKbHook  // proxy_kbhook.cpp (ticket 0745, D255 G2): [Proxy] KeyboardHookThread, before ProxyShell::OnBoot
{
void SetThreadOption(int value, bool fromFile);
}

namespace ProxyWheel  // proxy_wheel.h (ticket 0755, [Extras] FixSmoothWheel); declared here: the fork's headers leave out
{                     // <windows.h>'s MSG, which proxy_wheel.h's test interface names
void Install(bool on, const char* offWhy);
bool OnUnload(char* line, size_t cap);
}

namespace ProxyPurity
{
// proxy_purity.cpp (issue 3503): the two loads of a pack=0 boot (issue 3504 calls them; not in proxy_purity.h)
bool LoadFromTestEnvironment(std::string* why);
bool LoadBesideX64(const char* x64Dir, std::string* why);
}  // namespace ProxyPurity

using namespace ProxyRtInt;
void ProxyBuildIdText(HMODULE self, char* out, size_t outSize);  // proxy_buildid.cpp (issue 2704)

namespace
{
// =============================================================================================== small utilities
struct Lock
{
	CRITICAL_SECTION& cs;
	explicit Lock(CRITICAL_SECTION& c) : cs(c) { EnterCriticalSection(&cs); }
	~Lock() { LeaveCriticalSection(&cs); }

private:
	Lock& operator=(const Lock&);
};

CRITICAL_SECTION g_lock;     // tables, sessions, interning
CRITICAL_SECTION g_logLock;  // the log file
CRITICAL_SECTION g_snapLock;  // frame statistics and snapshots (issue 2502)
bool g_locksReady = false;

void InitLocks()
{
	if (!g_locksReady)
	{
		InitializeCriticalSection(&g_lock);
		InitializeCriticalSection(&g_logLock);
		InitializeCriticalSection(&g_snapLock);
		g_locksReady = true;
	}
}

HMODULE g_hModule = NULL;
LARGE_INTEGER g_qpcFreq = {};
LARGE_INTEGER g_qpc0 = {};

using ProxyLogic::DirOf;

bool FileExists(const std::string& p)
{
	const DWORD a = GetFileAttributesA(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(const std::string& p)
{
	const DWORD a = GetFileAttributesA(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool ReadTextFile(const std::string& path, std::string& out)
{
	FILE* f = NULL;
	if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
	char buf[4096];
	size_t n;
	out.clear();
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
	fclose(f);
	return true;
}

std::string ModulePath(HMODULE h)
{
	char mod[MAX_PATH * 2] = {};
	GetModuleFileNameA(h, mod, sizeof(mod));
	return mod;
}

// =============================================================================================== configuration
// vpproxy.ini: keys, defaults and the D28 layout in proxy_rt_logic.h (Settings). Relative paths: the proxy's folder.
ProxyLogic::Settings g_cfg;
std::string g_logFallbackDir;  // issue 0148: where vpproxy.log goes without a run folder ("" = beside the proxy)

void ReadConfig()
{
	char buf[MAX_PATH * 2];
	const std::string proxyDir = DirOf(ModulePath(g_hModule));
	std::string iniPath;
	const DWORD n = GetEnvironmentVariableA("VPPROXY_INI", buf, sizeof(buf));
	if (n > 0 && n < sizeof(buf))
		iniPath = buf;
	else
		iniPath = proxyDir + "\\vpproxy.ini";
	std::string text;
	const bool found = ReadTextFile(iniPath, text);
	ProxyLogic::ParseSettings(text, found, iniPath, proxyDir, DirOf(ModulePath(NULL)), g_cfg);
	// Issue 0148: nothing the patch writes at run time goes under the user's MODS folder. The EXE watches <user>\MODS with
	// a change notification on the whole tree and answers every change with a ~150-ms stall of its main thread on a huge
	// VP game (research/05-graphics-composition/renderer-in-server-design.md section 4.14). The default run root,
	// <x64 dir>\run, lies under MODS on the shipped layout (<user>\MODS\(1) Community Patch\VP64\run): it becomes
	// <user>\Logs\VP64\run. A run_root= of vpproxy.ini is kept as written. Issue 2606: in the modpack form (VP under
	// <game>\Assets\DLC) the user folder is config.ini's, as for the error report (ProxyLogic::UserLogsDir).
	std::string cfgIni;
	{
		std::wstring w;
		char a[MAX_PATH * 2];
		if (ProxySession::ConfigIniPath(&w) && WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, a, sizeof(a), NULL, NULL))
			cfgIni = a;
	}
	if (g_cfg.runRoot == g_cfg.x64Dir + "\\run")
	{
		const std::string logs = ProxyLogic::UserLogsDir(g_cfg.runRoot, cfgIni);
		if (!logs.empty()) g_cfg.runRoot = logs + "\\run";
	}
	// the fallback log of a boot without a run folder (mode=off, server_config=), beside the proxy until 0148: the same rule
	g_logFallbackDir = ProxyLogic::UserLogsDir(g_cfg.proxyDir + "\\vpproxy.log", cfgIni);
}

// =============================================================================================== run folder (D28)
std::string g_runDir;  // this boot's folder under run_root; "" with server_config= (the server's run is external)
std::string g_runWhy;  // why it could not be made
std::vector<std::string> g_runPruned;

// Deletes a folder tree. Reparse points (junctions, symlinks) are removed as links, never followed.
void DeleteTree(const std::string& dir)
{
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
	if (h != INVALID_HANDLE_VALUE)
	{
		do
		{
			const std::string name = fd.cFileName;
			if (name == "." || name == "..") continue;
			const std::string p = dir + "\\" + name;
			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
				DeleteTree(p);
			else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				RemoveDirectoryA(p.c_str());
			else
			{
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) SetFileAttributesA(p.c_str(), FILE_ATTRIBUTE_NORMAL);
				DeleteFileA(p.c_str());
			}
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	RemoveDirectoryA(dir.c_str());
}

bool MakeDirs(const std::string& dir)
{
	if (dir.empty() || DirExists(dir)) return true;
	const std::string parent = DirOf(dir);
	if (parent != dir && parent != "." && !MakeDirs(parent)) return false;
	return CreateDirectoryA(dir.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

// Creates <run_root>/<yyyymmdd-hhmmss>-<pid>-<n> and its db folder, after pruning the oldest run folders so that
// keep_runs remain (this one included). Returns "" on failure (why in g_runWhy).
std::string MakeRunFolder()
{
	char m[64];
	if (!MakeDirs(g_cfg.runRoot))
	{
		_snprintf_s(m, sizeof(m), _TRUNCATE, "error %u", (unsigned)GetLastError());
		g_runWhy = "cannot create run_root " + g_cfg.runRoot + ": " + m;
		return std::string();
	}
	std::vector<std::string> names;
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((g_cfg.runRoot + "\\*").c_str(), &fd);
	if (h != INVALID_HANDLE_VALUE)
	{
		do
			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
				names.push_back(fd.cFileName);
		while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	const std::vector<std::string> old = ProxyLogic::RunFoldersToPrune(names, g_cfg.keepRuns);
	for (size_t i = 0; i < old.size(); ++i)
	{
		DeleteTree(g_cfg.runRoot + "\\" + old[i]);
		g_runPruned.push_back(old[i]);
	}
	SYSTEMTIME t;
	GetLocalTime(&t);
	for (uint32_t n = 1; n < 1000; ++n)
	{
		const std::string d = g_cfg.runRoot + "\\" + ProxyLogic::RunFolderName(t, (uint32_t)GetCurrentProcessId(), n);
		if (CreateDirectoryA(d.c_str(), NULL))
		{
			CreateDirectoryA((d + "\\db").c_str(), NULL);
			return d;
		}
		if (GetLastError() != ERROR_ALREADY_EXISTS) break;
	}
	_snprintf_s(m, sizeof(m), _TRUNCATE, "error %u", (unsigned)GetLastError());
	g_runWhy = "cannot create a run folder under " + g_cfg.runRoot + ": " + m;
	return std::string();
}

// =============================================================================================== log
HANDLE g_logFile = INVALID_HANDLE_VALUE;
std::string g_logPathUsed;

// The log: log= when set; else the boot's run folder; else (mode=off, server_config=, no run folder) <user>\Logs\VP64
// when the proxy lies under MODS (issue 0148), else the proxy's folder.
void OpenLog()
{
	if (!g_cfg.logPath.empty())
		g_logPathUsed = g_cfg.logPath;
	else if (!g_runDir.empty())
		g_logPathUsed = g_runDir + "\\vpproxy.log";
	else if (!g_logFallbackDir.empty() && MakeDirs(g_logFallbackDir))
		g_logPathUsed = g_logFallbackDir + "\\vpproxy.log";
	else
		g_logPathUsed = g_cfg.proxyDir + "\\vpproxy.log";
	g_logFile = CreateFileA(g_logPathUsed.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
	                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	// Issue 2704: the build id is the log's first line (proxy_buildid.cpp, proxy/include/vp_buildid.h).
	char id[1024];
	ProxyBuildIdText(g_hModule, id, sizeof(id));
	Log("%s", id);
}

void LogRaw(const char* text, bool toStderr)
{
	if (!g_locksReady) return;
	Lock l(g_logLock);
	if (g_logFile != INVALID_HANDLE_VALUE)
	{
		DWORD w = 0;
		WriteFile(g_logFile, text, (DWORD)strlen(text), &w, NULL);
	}
	if (toStderr) fputs(text, stderr);
}

void LogV(bool toStderr, const char* fmt, va_list ap)
{
	char buf[2048];
	const int head = _snprintf_s(buf, sizeof(buf), _TRUNCATE, "[%10.6f] [%5u] ", Now(), (unsigned)GetCurrentThreadId());
	int n = _vsnprintf_s(buf + head, sizeof(buf) - head - 2, _TRUNCATE, fmt, ap);
	if (n < 0) n = (int)strlen(buf + head);
	buf[head + n] = '\n';
	buf[head + n + 1] = 0;
	LogRaw(buf, toStderr);
}

// Issue 3504: while the mode=on boot runs, the last error line is kept: with pack=1 it is the reason of the fallback.
bool g_keepBootErr = false;
std::string g_bootErr;

void LogErr(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (g_keepBootErr)
	{
		char msg[1024];
		_vsnprintf_s(msg, sizeof(msg), _TRUNCATE, fmt, ap);
		g_bootErr = msg;
		va_end(ap);
		va_start(ap, fmt);
	}
	LogV(true, fmt, ap);
	va_end(ap);
}

// =============================================================================================== counters
// Set by ProxyRt::DispatchFail on this thread; OnCall counts the served call as failed (issue 9008: a served call
// that failed was logged but not counted, so the totals said "failed calls 0").
__declspec(thread) bool t_dispatchFailed = false;
// Issue 2800: the engine method this thread is serving (innermost), for the borrowed-argument check of WrapTemp.
__declspec(thread) uint32_t t_servedMethod = 0;
volatile LONG g_tempUnlisted = 0;  // DLL objects handed to engine methods outside the borrowed-argument list
volatile LONG g_servedExceptions = 0;  // issue 9036: C++ exceptions caught in served calls (OnCall)
struct ServedMethod
{
	uint32_t outer;
	explicit ServedMethod(uint32_t m) : outer(t_servedMethod) { t_servedMethod = m; }
	~ServedMethod() { t_servedMethod = outer; }
};


struct Ctr
{
	uint64_t calls, bytesOut, bytesIn, ticks;
	uint64_t local;               // issue 2700: calls of this method answered locally (out only)
	uint64_t snapCalls, snapLocal;  // issue 2700: values at the previous snapshot (vpproxy-snapshot-methods.tsv)
	uint32_t fails, decodeFails, unimpl, unknownHandle;
	bool loggedFail, loggedDecode, loggedUnimpl;
};
struct CtrPair
{
	Ctr out;  // calls the proxy made
	Ctr in;   // calls the proxy served
};
CtrPair* g_ctrKinds[kVpKindCount + 1] = {};
uint32_t g_ctrKindN[kVpKindCount + 1] = {};
CtrPair g_ctrRt[2][128];  // issue 2700: kRt_Canon 0x40 and kRt_LockSync 0x41 need more than 64
CtrPair g_ctrOther;
uint64_t g_unsupportedLua = 0;
std::map<std::string, uint64_t> g_unsupportedByType;  // issue 9008: per Lua type, for the verdict and allow-list
uint32_t g_afterShutdown = 0;

// Issue 2502: the role of every method id (proxy_rt_logic.h Role), per-interface traffic by direction and channel,
// frames, the binding checks, the strict allow-list.
uint8_t* g_roles[kVpKindCount + 1] = {};
ProxyLogic::Traffic g_traffic;
bool g_trafficOn = false;
volatile LONG g_updateOffCore = 0;     // ICvGame1::Update on a channel other than core (host=exe)
volatile LONG g_uiForeign = 0;         // top-level calls on ui from a thread other than the boot thread
volatile LONG g_unimplAllowed = 0;     // unimplemented calls the strict allow-list names
ProxyLogic::AllowList g_strictAllow;

void InitCounters()
{
	for (uint32_t k = 1; k <= kVpKindCount; ++k)
	{
		const uint32_t n = vpk_method_count(k);
		if (!n) continue;
		g_ctrKinds[k] = static_cast<CtrPair*>(calloc(n, sizeof(CtrPair)));
		g_ctrKindN[k] = g_ctrKinds[k] ? n : 0;
		g_roles[k] = static_cast<uint8_t*>(calloc(n, 1));
		if (!g_roles[k]) continue;
		for (uint32_t i = 0; i < n; ++i)
			g_roles[k][i] = (uint8_t)ProxyLogic::RoleOfKey(vpk_method_key((k << 16) | i));
	}
	g_traffic.Init(kVpKindCount);
	g_trafficOn = g_cfg.counters;
}

inline ProxyLogic::Role RoleOf(uint32_t method)
{
	const uint32_t t = method >> 16, i = method & 0xFFFFu;
	if (t >= 1 && t <= kVpKindCount && g_roles[t] && i < g_ctrKindN[t]) return (ProxyLogic::Role)g_roles[t][i];
	return ProxyLogic::kRoleNone;
}

// Traffic row of a method id: its interface kind, rt (proxy -> server runtime), rtE (server -> proxy runtime), other.
inline uint32_t TrafficRow(uint32_t method)
{
	const uint32_t t = method >> 16;
	if (t >= 1 && t <= kVpKindCount) return t;
	if (t == vpw::kRtTypeProxyToServer) return kVpKindCount + 1;
	if (t == vpw::kRtTypeServerToProxy) return kVpKindCount + 2;
	return 0;
}

const char* TrafficRowName(uint32_t row)
{
	if (row == 0) return "other";
	if (row == kVpKindCount + 1) return "rt";
	if (row == kVpKindCount + 2) return "rtE";
	return vpk_iface_name(row);
}

CtrPair& CtrFor(uint32_t method)
{
	const uint32_t t = method >> 16, i = method & 0xFFFFu;
	if (t == vpw::kRtTypeProxyToServer && i < 128) return g_ctrRt[0][i];
	if (t == vpw::kRtTypeServerToProxy && i < 128) return g_ctrRt[1][i];
	if (t >= 1 && t <= kVpKindCount && g_ctrKinds[t] && i < g_ctrKindN[t]) return g_ctrKinds[t][i];
	return g_ctrOther;
}

const char* RtName(uint32_t method)
{
	switch (method)
	{
	case vpw::kRt_Boot: return "rt:Boot";
	case vpw::kRt_Shutdown: return "rt:Shutdown";
	case vpw::kRt_Stats: return "rt:Stats";
	case vpw::kRt_LuaRegister: return "rt:LuaRegister";
	case vpw::kRt_LuaDescribeType: return "rt:LuaDescribeType";
	case vpw::kRt_LuaCall: return "rt:LuaCall";
	case vpw::kRt_LuaIndex: return "rt:LuaIndex";
	case vpw::kRt_LuaRelease: return "rt:LuaRelease";
	case vpw::kRt_StreamOpen: return "rt:StreamOpen";
	case vpw::kRtE_LuaGetGlobal: return "rtE:LuaGetGlobal";
	case vpw::kRtE_LuaCall: return "rtE:LuaCall";
	case vpw::kRtE_LuaRelease: return "rtE:LuaRelease";
	case vpw::kRtE_DbOps: return "rtE:DbOps";
	case vpw::kRtE_DbFingerprint: return "rtE:DbFingerprint";
	case vpw::kRtE_LuaRunChunk: return "rtE:LuaRunChunk";
	case vpw::kRtE_DbSnapshot: return "rtE:DbSnapshot";
	case vpw::kRtE_LuaRaise: return "rtE:LuaRaise";  // ticket 4804
	case vpw::kRt_CoreLockYield: return "rt:CoreLockYield";
	case vpw::kRt_StreamOpenPull: return "rt:StreamOpenPull";
	case vpw::kRtE_StreamRead: return "rtE:StreamRead";
	case vpw::kRt_CoreLockResume: return "rt:CoreLockResume";
	case vpw::kRt_Canon: return "rt:Canon";
	case vpw::kRt_LockSync: return "rt:LockSync";
	default: return NULL;
	}
}

std::string MethodName(uint32_t method)
{
	const char* s = RtName(method);
	if (!s) s = vpk_method_key(method);
	if (s) return s;
	char b[32];
	_snprintf_s(b, sizeof(b), _TRUNCATE, "0x%08X", method);
	return b;
}

// =============================================================================================== runtime state
vpipc::Endpoint* g_ep = NULL;
vpipc::Channel g_dummyChannel(NULL);    // returned by Chan() when there is no endpoint; never used by End()
vpipc::Channel g_pendingChannel(NULL);  // returned by Chan() to a thread whose channel Begin() has yet to choose
HANDLE g_server = NULL;
DWORD g_serverPid = 0;
DWORD g_monitorTid = 0;         // the vpipc monitor thread (proxy code; must not outlive the proxy image)
volatile LONG g_dead = 0;       // on_server_death=continue after a fatal failure: every call fails at once
volatile LONG g_shutdown = 0;   // kRt_Shutdown sent (or being sent): failures are expected, never fatal
volatile LONG g_shutdownDone = 0;  // Shutdown() finished: the server ended and the endpoint is closed
volatile LONG g_booting = 0;    // inside the boot: failures return NULL from DllGetGameContext, never exit
volatile LONG g_coreClaimed = 0;
DWORD g_bootThread = 0;
int g_bootState = 0;  // 0 not tried, 1 ok, -1 failed
ICvGameContext1* g_context = NULL;
ICvEngineUtility1* g_engineUtility = NULL;
ICvEngineScriptSystem1* g_engineScript = NULL;
bool g_statsWritten = false;
// mode=off (D28 off switch): the original 32-bit DLL answers DllGetGameContext
bool g_offMode = false;
HMODULE g_fallback = NULL;
typedef ICvGameContext1* (*DllGetGameContextFn)();
DllGetGameContextFn g_fallbackGet = NULL;

// Issue 3521 (D119, D124): the failure of the 64-bit path as the error report states it, set at each failure point of
// the pack check and of Boot(), and by a server that fails in a game (CallFailed).
std::string g_failStage, g_failCode;
std::vector<std::string> g_failDetails;
void SetFail(const char* stage, const char* code, const std::string& detail)
{
	g_failStage = stage;
	g_failCode = code;
	g_failDetails.clear();
	if (!detail.empty()) g_failDetails.push_back(detail);
}
// Ticket 0354: the server's exit code after it refused a save of the other DLL variant (srv_remote_savevariant.cpp):
// 0x2056SSRR, SS the save's major count, RR the running DLL's (each 2..62, different). False for any other code.
bool SaveVariantExit(DWORD code, int* pSave, int* pRun)
{
	if ((code & 0xFFFF0000u) != 0x20560000u) return false;
	const int s = (int)((code >> 8) & 0xFF), r = (int)(code & 0xFF);
	if (s < 2 || s > 62 || r < 2 || r > 62 || s == r) return false;
	*pSave = s;
	*pRun = r;
	return true;
}
int g_failSaveMajors = 0, g_failRunMajors = 0;  // ticket 0354: set with the stage save-variant
std::string g_failSummary;  // ticket 0588: the player's summary line when the failure wrote its own (stage exe-build)
volatile LONG g_reported = 0;  // a report was claimed (InterlockedCompareExchange by the caller: once per proxy load)
// pack=1: writes the report and starts the helper (popup in a stock session, failed --pid in a D33 one; proxy_session.h);
// the caller then goes on: the fallback or on_server_death (stock), Fatal (D33). True when the helper was started.
// Ticket 0721: bootEnded = the boot ends the game whatever the session's kind (BootNullNoPlay: no context to return):
// vp64helper failed --pid as in a D33 session, and the report says the game was ended at the start.
bool ReportFailure(ProxySession::Kind kind, const std::string& sessionWhy, bool bootEnded = false);

void WriteStats();

// Ticket 0697: the wait for the composite's last pacing lines (the composite bounds its own lock waits by half of it).
// Below the 2,000-ms limit from Fatal to the TerminateProcess (MC #11248).
const unsigned long kFatalFlushWaitMs = 1500;

void Fatal(int exitCode, const char* why)
{
	LogErr("FATAL: %s: the process ends with exit code %d", why, exitCode);
	// ticket 0697: TerminateProcess skips the composite's process-detach and its "pacing totals" line: written here, bounded;
	// once per process (a second Fatal skips it); the report and the popup came before (CallFailed, ServerHungRoute)
	char detail[400];
	_snprintf_s(detail, sizeof(detail), _TRUNCATE, "exit=%d stage=%s code=%s: %s", exitCode,
	            g_failStage.empty() ? "-" : g_failStage.c_str(), g_failCode.empty() ? "-" : g_failCode.c_str(), why ? why : "");
	ProxyShell::FlushCompositeTotals("fatal", detail, kFatalFlushWaitMs);
	WriteStats();
	LogFlush();
	TerminateProcess(GetCurrentProcess(), (UINT)exitCode);
}

// =============================================================================================== frames, snapshots
// Frames (proxy_rt_logic.h FrameTracker): only the ui (boot) thread feeds the tracker; a snapshot may be taken on any
// thread, under g_snapLock. Snapshots: the named event Local\VPProxySnapshot_<pid> (auto-reset), polled at each frame
// marker and at each top-level Update, or ProxyRtInt::Snapshot(label) (for the Lua channel); each appends one block to
// vpproxy-snapshots.tsv beside the log.
ProxyLogic::FrameTracker g_frames;
HANDLE g_snapEvent = NULL;
uint32_t g_snapSeq = 0;
uint64_t g_snapFrames = 0, g_snapTimerCalls = 0;
double g_snapTime = 0.0;
bool g_snapHeader = false;

// Issue 2700: per method, the calls made (out), served (in) and answered locally since the previous snapshot, into
// vpproxy-snapshot-methods.tsv beside the log (columns: snapshot label frames method out in local per_frame_out
// per_frame_local). Same sequence number as vpproxy-snapshots.tsv; written after it.
uint64_t g_mSnapFrames = 0;
void WriteMethodSnapshot(const char* label)
{
	const std::string path = DirOf(g_logPathUsed) + "\\vpproxy-snapshot-methods.tsv";
	FILE* f = NULL;
	if (fopen_s(&f, path.c_str(), g_snapSeq == 1 ? "wb" : "ab") != 0 || !f) return;
	if (g_snapSeq == 1) fputs("snapshot\tlabel\tframes\tmethod\tout\tin\tlocal\tper_frame_out\tper_frame_local\n", f);
	const uint64_t frames = g_frames.frames - g_mSnapFrames;
	g_mSnapFrames = g_frames.frames;
	struct One
	{
		static void Row(FILE* f, uint32_t m, CtrPair& p, uint64_t frames, const char* label)
		{
			const uint64_t o = p.out.calls - p.out.snapCalls, i = p.in.calls - p.in.snapCalls,
			               l = p.out.local - p.out.snapLocal;
			p.out.snapCalls = p.out.calls;
			p.in.snapCalls = p.in.calls;
			p.out.snapLocal = p.out.local;
			if (!o && !i && !l) return;
			fprintf(f, "%u\t%s\t%I64u\t%s\t%I64u\t%I64u\t%I64u\t%.3f\t%.3f\n", g_snapSeq, label, frames,
			        MethodName(m).c_str(), o, i, l, frames ? (double)o / (double)frames : 0.0,
			        frames ? (double)l / (double)frames : 0.0);
		}
	};
	const char* lab = label && *label ? label : "snapshot";
	for (uint32_t k = 1; k <= kVpKindCount; ++k)
		for (uint32_t i = 0; i < g_ctrKindN[k]; ++i)
			One::Row(f, (k << 16) | i, g_ctrKinds[k][i], frames, lab);
	for (int t = 0; t < 2; ++t)
		for (uint32_t i = 0; i < 128; ++i)
			One::Row(f, ((t ? vpw::kRtTypeServerToProxy : vpw::kRtTypeProxyToServer) << 16) | i, g_ctrRt[t][i], frames, lab);
	fclose(f);
}

void TakeSnapshot(const char* label)
{
	if (!g_trafficOn || !g_locksReady) return;
	std::string text;
	std::string path;
	{
		Lock l(g_snapLock);
		const double t = Now();
		const ProxyLogic::FrameTracker::Stats st = g_frames.TakeWindow();
		const uint64_t frames = g_frames.frames - g_snapFrames, timer = g_frames.timerCalls - g_snapTimerCalls;
		g_snapFrames = g_frames.frames;
		g_snapTimerCalls = g_frames.timerCalls;
		std::string lab = label && *label ? label : "snapshot";
		for (size_t i = 0; i < lab.size(); ++i)
			if (lab[i] == '\t' || lab[i] == '\r' || lab[i] == '\n') lab[i] = ' ';
		ProxyLogic::FormatSnapshot(text, !g_snapHeader, ++g_snapSeq, lab, t, t - g_snapTime, frames, timer, st, g_traffic,
		                           TrafficRowName);
		g_snapHeader = true;
		g_snapTime = t;
		path = DirOf(g_logPathUsed) + "\\vpproxy-snapshots.tsv";
		FILE* f = NULL;
		if (fopen_s(&f, path.c_str(), g_snapSeq == 1 ? "wb" : "ab") == 0 && f)
		{
			fputs(text.c_str(), f);
			fclose(f);
		}
		else
			path += " (FAILED to open)";
	}
	Log("snapshot %u '%s' written to %s", g_snapSeq, label ? label : "", path.c_str());
	WriteMethodSnapshot(label);
	ProxyLocal::Report();  // issue 2700: the local-answer counters with every snapshot (a stopped EXE writes no shutdown report)
}

// The event's label: the first line of vpproxy-snapshot-label.txt beside the log, if present; else "event".
void PollSnapshotEvent()
{
	if (!g_snapEvent || WaitForSingleObject(g_snapEvent, 0) != WAIT_OBJECT_0) return;
	std::string label;
	const std::string labelFile = DirOf(g_logPathUsed) + "\\vpproxy-snapshot-label.txt";
	if (ReadTextFile(labelFile, label))
	{
		const size_t e = label.find_first_of("\r\n");
		if (e != std::string::npos) label = label.substr(0, e);
		DeleteFileA(labelFile.c_str());  // a label names one snapshot
	}
	TakeSnapshot(label.empty() ? "event" : label.c_str());
}

// =============================================================================================== threads
const uint32_t kMaxStage = 96;
struct StageFrame
{
	vpipc::Writer w;
	uint32_t method;
	bool impure;  // issue 2700: ProxyLocal::CallStart's verdict, closed in End
	bool genChange;  // issue 2801: ProxyLocal::BeforeCall's verdict (a generation change in flight), closed in End
	LARGE_INTEGER t0;
	uint8_t buf[1024];
};
// A thread's channel is chosen at its first call (Begin, ProxyLogic::ChooseChannel) and never changes; the boot thread
// is ui from the start. `decided` false: Chan() returns g_pendingChannel, Begin() decides, End() resolves it.
struct ThreadState
{
	uint32_t chan;
	bool decided;   // chan is final
	bool bound;     // vpipc bind_current_thread done
	bool isBoot;
	bool inUpdateHook;
	uint32_t depth;
	StageFrame* frames[kMaxStage];
	std::vector<std::vector<const void*> > scopes;  // DispatchScope stack: temporary wrappers
	std::vector<std::vector<const void*> > kept;    // per scope: temporary wrappers KeepIfTemp promoted (issue 2708)
	uint32_t scopeDepth;
	ThreadState()
		: chan(vpipc::kChannelAux), decided(false), bound(false), isBoot(false), inUpdateHook(false), depth(0),
		  scopeDepth(0)
	{
		memset(frames, 0, sizeof(frames));
	}
};
DWORD g_tls = TLS_OUT_OF_INDEXES;

// "module+0xoffset" of the calling thread's Win32 start address (NtQueryInformationThread class 9,
// ThreadQuerySetWin32StartAddress); "?" when unknown.
typedef LONG(WINAPI* NtQueryInformationThreadFn)(HANDLE, ULONG, PVOID, ULONG, PULONG);
uintptr_t ThreadStartAddress(HANDLE thread)
{
	static NtQueryInformationThreadFn s_fn = NULL;
	static bool s_tried = false;
	if (!s_tried)
	{
		HMODULE nt = GetModuleHandleA("ntdll.dll");
		s_fn = nt ? (NtQueryInformationThreadFn)GetProcAddress(nt, "NtQueryInformationThread") : NULL;
		s_tried = true;
	}
	uintptr_t addr = 0;
	if (!s_fn || s_fn(thread, 9, &addr, sizeof(addr), NULL) != 0) return 0;
	return addr;
}

std::string AddressText(uintptr_t addr)
{
	if (!addr) return "?";
	char b[MAX_PATH + 64];
	HMODULE m = NULL;
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                       reinterpret_cast<LPCSTR>(addr), &m) && m)
	{
		const std::string path = ModulePath(m);
		const size_t k = path.find_last_of("\\/");
		_snprintf_s(b, sizeof(b), _TRUNCATE, "%s+0x%X", (k == std::string::npos ? path : path.substr(k + 1)).c_str(),
		            (unsigned)(addr - (uintptr_t)m));
	}
	else
		_snprintf_s(b, sizeof(b), _TRUNCATE, "0x%08X (no module)", (unsigned)addr);
	return b;
}

// Logs a newly seen thread once: id, start address, first method, channel and why.
void LogNewThread(ThreadState* ts, uint32_t firstMethod, const char* why)
{
	Log("thread %u: start %s, first call %s, channel %s (%s)", (unsigned)GetCurrentThreadId(),
	    AddressText(ThreadStartAddress(GetCurrentThread())).c_str(),
	    firstMethod == 0xFFFFFFFFu ? "(DllGetGameContext)" : MethodName(firstMethod).c_str(),
	    ProxyLogic::ChannelName(ts->chan), why);
}

ThreadState* TS()
{
	if (g_tls == TLS_OUT_OF_INDEXES) return NULL;
	ThreadState* ts = static_cast<ThreadState*>(TlsGetValue(g_tls));
	if (ts) return ts;
	ts = new ThreadState();
	TlsSetValue(g_tls, ts);
	if (GetCurrentThreadId() == g_bootThread)
	{
		ts->isBoot = true;
		ts->chan = ProxyLogic::ChooseChannel(true, ProxyLogic::kRoleNone, &g_coreClaimed, NULL);
		ts->decided = true;
		LogNewThread(ts, 0xFFFFFFFFu, "the boot thread (it called DllGetGameContext)");
	}
	return ts;
}

// The channel of an undecided thread, chosen by its first call's role (Begin).
void Decide(ThreadState* ts, uint32_t method, ProxyLogic::Role role)
{
	const char* why = NULL;
	ts->chan = ProxyLogic::ChooseChannel(ts->isBoot, role, &g_coreClaimed, &why);
	ts->decided = true;
	LogNewThread(ts, method, why);
}

// The vpipc channel of a decided thread (binds it on first use).
vpipc::Channel& RealChannel(ThreadState* ts)
{
	if (!g_ep || !ts || !ts->decided) return g_dummyChannel;
	if (!ts->bound)
	{
		g_ep->bind_current_thread(ts->chan);
		ts->bound = true;
	}
	return g_ep->channel(ts->chan);
}

void FreeThreadState()
{
	if (g_tls == TLS_OUT_OF_INDEXES) return;
	ThreadState* ts = static_cast<ThreadState*>(TlsGetValue(g_tls));
	if (!ts) return;
	for (uint32_t i = 0; i < kMaxStage; ++i)
		if (ts->frames[i])
		{
			ts->frames[i]->~StageFrame();
			free(ts->frames[i]);
		}
	delete ts;
	TlsSetValue(g_tls, NULL);
}

StageFrame* PushFrame(ThreadState* ts)
{
	if (ts->depth >= kMaxStage) return NULL;
	StageFrame*& f = ts->frames[ts->depth];
	if (!f)
	{
		void* mem = malloc(sizeof(StageFrame));
		if (!mem) return NULL;
		f = new (mem) StageFrame();
	}
	++ts->depth;
	return f;
}

// A writer that drops everything (stage overflow); End() then fails the call.
vpipc::Writer& DummyWriter()
{
	static vpipc::Writer* w = NULL;
	static uint8_t buf[64];
	if (!w) w = new vpipc::Writer();
	w->reset(buf, sizeof(buf), false);
	return *w;
}

// =============================================================================================== tables
std::map<const void*, vpipc::Handle> g_wrappers;  // live Proxy_ wrappers -> handle
std::map<const void*, uint32_t> g_wrapperRefs;  // extra references from local QueryInterface answers
struct EngEntry
{
	const void* p;
	uint32_t kind;
	bool live;
};
std::vector<EngEntry> g_eng;  // engine objects; handle id = index + 1
std::map<const void*, uint32_t> g_engIndex;
ProxyLogic::SlotAllocator g_engSlots;  // issue 9036: dead slots of g_eng are reused (after a FIFO quarantine)
std::set<std::string> g_intern;
std::set<std::wstring> g_internW;
std::map<std::string, CvString*> g_internCv;
uint32_t g_badHandleLogs = 0;

// =============================================================================================== stream sessions
struct Session
{
	FDataStream* s;
	uint32_t start, total, consumed, id;
	bool unknown;  // issue 2500: the engine stream reported no real size (GetSizeLeft 0x7FFFFFFF): matched by position
};
std::vector<Session> g_sessions;
std::vector<FDataStream*> g_pullStreams;  // issue 2500: engine streams of pull sessions; token = index + 1

// =============================================================================================== failure policy
// A call that did not return Ok. Returns false (the generated caller returns the default) unless the process ends.
bool CallFailed(vpipc::Channel& ch, uint32_t method, vpipc::Status st, vpipc::Reader& r, Ctr& c)
{
	++c.fails;
	if (st == vpipc::Status::RemoteError)
	{
		// The server's handler refused the call (bad handle, unimplemented, ...): the RETURN is a message.
		if (c.fails <= 3)
		{
			const char* msg = r.cstr();
			Log("call %s failed on the server: remote status %u: %s%s", MethodName(method).c_str(),
			    ch.last_remote_status(), msg ? msg : "(no message)", c.fails == 3 ? " (further failures counted)" : "");
		}
		return false;
	}
	if (g_shutdown)
	{
		if (!c.loggedFail)
		{
			c.loggedFail = true;
			Log("call %s after shutdown: %s (expected)", MethodName(method).c_str(), vpipc::status_name(st));
		}
		return false;
	}
	if (g_booting)
	{
		LogErr("boot: call %s failed: %s", MethodName(method).c_str(), vpipc::status_name(st));
		return false;
	}
	LogErr("call %s (0x%08X) on channel %u failed: %s (server pid %u)", MethodName(method).c_str(), method, ch.index(),
	       vpipc::status_name(st), (unsigned)g_serverPid);
	// Ticket 0354: the server refused a save of the other DLL variant (srv_remote_savevariant.cpp) and ended with an exit
	// code that carries the save's and the running DLL's major counts. Logged whatever pack says.
	int saveMajors = 0, runMajors = 0;
	{
		DWORD code = STILL_ACTIVE;
		if (g_server) GetExitCodeProcess(g_server, &code);
		if (SaveVariantExit(code, &saveMajors, &runMajors))
			LogErr("save variant: the server refused the save: it was made with a %d-civilization build, the running DLL "
			       "has %d major civilizations (server exit code 0x%08lX; ticket 0354)", saveMajors, runMajors, code);
	}
	// Issues 3521, 3523 (D119, D125): with pack=1 the first such failure writes the report and starts the helper; in a
	// D33 session the game then ends (no play, whatever on_server_death says); in a stock one on_server_death decides as
	// before (a 32-bit DLL cannot take over a game in progress).
	// Ticket 0422: the server's DLL stopped on a failed check and the server already wrote the D119 report (stage
	// dll-fatal) and started the popup before it ended; it left <run>\dll-fatal.txt. No second report and no second
	// popup; a D33 session still ends the game below, on_server_death decides as before.
	std::string dllFatal;
	if (g_cfg.pack && !g_runDir.empty())
	{
		FILE* f = NULL;
		if (!fopen_s(&f, (g_runDir + "\\dll-fatal.txt").c_str(), "rb") && f)
		{
			char line[600] = "";
			char rep[600] = "";
			if (!fgets(line, sizeof(line), f)) line[0] = 0;
			if (!fgets(rep, sizeof(rep), f)) rep[0] = 0;
			fclose(f);
			for (char* p = rep; *p; ++p)
				if (*p == '\r' || *p == '\n') *p = 0;
			dllFatal = rep[0] ? rep : "(no report line)";
		}
	}
	if (!dllFatal.empty() && InterlockedCompareExchange(&g_reported, 1, 0) == 0)
	{
		LogErr("the server reported this failure itself (dll-fatal: its DLL stopped on a failed check; %s; ticket 0422): "
		       "no second report and no second popup", dllFatal.c_str());
		std::string sessionWhy;
		if (ProxySession::SessionKind(ProxySession::ReadMasks(), &sessionWhy) == ProxySession::kD33)
			Fatal(3, "the server failed a call in a D33 session (no play, D125)");
	}
	else if (g_cfg.pack && InterlockedCompareExchange(&g_reported, 1, 0) == 0)
	{
		std::string sessionWhy;
		const ProxySession::Kind kind = ProxySession::SessionKind(ProxySession::ReadMasks(), &sessionWhy);
		char d[300];
		DWORD code = STILL_ACTIVE;
		if (g_server) GetExitCodeProcess(g_server, &code);
		if (saveMajors)
		{
			char vc[32];
			_snprintf_s(vc, sizeof(vc), _TRUNCATE, "m%d-save-m%d-dll", saveMajors, runMajors);
			SetFail("save-variant", vc, "the failed call: " + MethodName(method));
			g_failSaveMajors = saveMajors;
			g_failRunMajors = runMajors;
			_snprintf_s(d, sizeof(d), _TRUNCATE, "the save was made with a %d-civilization build of Vox Populi; the running "
			            "DLL has %d major civilizations; the server refused it before the DLL read the game section",
			            saveMajors, runMajors);
			g_failDetails.push_back(d);
		}
		else
			SetFail("server-death", vpipc::status_name(st), "the failed call: " + MethodName(method));
		if (code == STILL_ACTIVE)
			_snprintf_s(d, sizeof(d), _TRUNCATE, "server pid %u: still running (it did not answer)", (unsigned)g_serverPid);
		else if ((code & 0xF0000000u) == 0xC0000000u)
			_snprintf_s(d, sizeof(d), _TRUNCATE, "server pid %u: ended by exception code 0x%08lX (the address is in a server "
			            "minidump only when the server wrote one)", (unsigned)g_serverPid, code);
		else
			_snprintf_s(d, sizeof(d), _TRUNCATE, "server pid %u: exit code %lu (0x%08lX)", (unsigned)g_serverPid, code, code);
		g_failDetails.push_back(d);
		ReportFailure(kind, sessionWhy);
		if (kind == ProxySession::kD33) Fatal(3, "the server failed a call in a D33 session (no play, D125)");
	}
	if (g_cfg.onDeathExit) Fatal(3, "the server failed a call (on_server_death=exit)");
	if (InterlockedExchange(&g_dead, 1) == 0)
		LogErr("on_server_death=continue: every later call returns its default at once");
	return false;
}

// Ticket 0691 (D242): the composite's exact lockstep wait ended because the server drew no frame for lockstep_hung_ms
// (proxy_shell.cpp's sink, the composite's control thread, once per process). The route of a server that failed in a
// game, as CallFailed above: the report and the helper once per proxy load (stage server-hung), then a D33 session ends
// (no play with a frozen world, D118/D125); a stock session goes on as on_server_death says.
void ServerHungRoute(const char* reason)
{
	const std::string why = reason && *reason ? reason : "(no reason given)";
	LogErr("server-hung: the composite's lockstep wait ended: %s (server pid %u; ticket 0691)", why.c_str(), (unsigned)g_serverPid);
	std::string sessionWhy;
	const ProxySession::Kind kind = ProxySession::SessionKind(ProxySession::ReadMasks(), &sessionWhy);
	if (g_cfg.pack && InterlockedCompareExchange(&g_reported, 1, 0) == 0)
	{
		SetFail("server-hung", "no-frame", "the composite: " + why);
		char d[160];
		_snprintf_s(d, sizeof(d), _TRUNCATE, "server pid %u: still running; it drew no frame for the composite's camera",
		            (unsigned)g_serverPid);
		g_failDetails.push_back(d);
		ReportFailure(kind, sessionWhy);
	}
	else
		LogErr("server-hung: no report (%s)", g_cfg.pack ? "this proxy load reported a failure already" : "pack=0");
	if (kind == ProxySession::kD33) Fatal(3, "the server hung in a D33 session (no play, D125)");
	if (g_cfg.onDeathExit) Fatal(3, "the server hung (on_server_death=exit)");
}
struct ServerHungRegister  // proxy_shell.cpp's sink reaches the route through ProxyShell::g_serverHung
{
	ServerHungRegister() { ProxyShell::g_serverHung = &ProxyRtInt::ServerHung; }
} g_serverHungRegister;

void Count(Ctr& c, uint32_t out, uint32_t in, const LARGE_INTEGER& t0)
{
	LARGE_INTEGER t1;
	QueryPerformanceCounter(&t1);
	++c.calls;
	c.bytesOut += out;
	c.bytesIn += in;
	c.ticks += (uint64_t)(t1.QuadPart - t0.QuadPart);
}

// The RETURN payload lives in the channel's data area until this side's next send; decoding it may make a nested
// call (an instance type described on first use, ...). Copy it into one of 64 per-thread buffers used in turn
// (2026-11-10, integration). Issue 9041 (bug 0102): the ring releases buffers above 64 KB before reuse
// (ProxyLogic::ReturnRing); before, it kept every save body's capacity for the life of the thread, about 22 MB of the
// EXE's address space per AI turn on the huge save. The ring is freed when its thread ends (FreeReturnRing).
static __declspec(thread) ProxyLogic::ReturnRing* t_returns = NULL;

static void KeepReturn(vpipc::Reader& r)
{
	if (!t_returns) t_returns = new ProxyLogic::ReturnRing();
	const uint32_t n = r.remaining();
	r.reset(t_returns->Keep(r.cursor(), n), n);
}

// DLL_THREAD_DETACH, and the unloading thread at DLL_PROCESS_DETACH (issue 9041: per-turn EXE worker threads each left
// their ring behind).
static void FreeReturnRing()
{
	delete t_returns;
	t_returns = NULL;
}

// Waits for the RETURN of a call whose arguments are in the channel writer, counts it, applies the policy.
bool Finish(vpipc::Channel& ch, uint32_t method, uint32_t bytesOut, const LARGE_INTEGER& t0, vpipc::Reader& r)
{
	const vpipc::Status st = ch.end_call(r);
	Ctr& c = CtrFor(method).out;
	Count(c, bytesOut, st == vpipc::Status::Ok ? r.remaining() : 0, t0);
	if (st == vpipc::Status::Ok) { KeepReturn(r); return true; }
	return CallFailed(ch, method, st, r, c);
}

// True when a call must not be attempted (no endpoint, dead server under continue, after the shutdown): the call
// fails at once and is counted.
bool CallsBlocked(uint32_t method, vpipc::Channel& ch, vpipc::Reader& r)
{
	const bool blocked = !g_ep || &ch == &g_dummyChannel || g_dead || (g_shutdown && method != vpw::kRt_Shutdown);
	if (!blocked) return false;
	r.reset(NULL, 0);
	Ctr& c = CtrFor(method).out;
	++c.fails;
	if (g_shutdown) ++g_afterShutdown;
	return true;
}

// =============================================================================================== served calls
// issue 2500: the server pulls exactly n bytes of a pull session's engine stream. The engine stream's ReadIt may throw
// (the EXE's stream does on a read past its end): the caller, OnCall, catches it (ticket 0359).
bool ServeStreamRead(vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)
{
	const uint32_t token = args.u32();
	const uint32_t n = args.u32();
	FDataStream* s = NULL;
	{
		Lock l(g_lock);
		if (token >= 1 && token <= g_pullStreams.size()) s = g_pullStreams[token - 1];
	}
	if (!args.ok() || !s || n > (64u << 20))
		return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, "kRtE_StreamRead: bad arguments or token");
	std::vector<uint8_t> buf(n ? n : 1);
	const uint32_t got = n ? s->ReadIt(n, &buf[0]) : 0;
	ret.bytes(&buf[0], got <= n ? got : 0);
	return true;
}

void OnCall(void*, vpipc::Channel& ch, uint32_t method, vpipc::Reader& args, vpipc::Writer& ret)
{
	ProxyRt::DispatchScope scope;
	// The CALL payload lives in the channel's data area, which the next nested call on this channel overwrites;
	// a handler that calls back (a DLL call inside an engine call, Lua) must still read its arguments, and
	// const char* arguments decoded by pointer must stay valid during the call: work on a private copy
	// (2026-11-10, integration: CreateLuaThread's name was overwritten by the nested RegisterScriptLibraries).
	std::vector<uint8_t> argsCopy(args.cursor(), args.cursor() + args.remaining());
	args.reset(argsCopy.empty() ? NULL : &argsCopy[0], (uint32_t)argsCopy.size());
	LARGE_INTEGER t0;
	QueryPerformanceCounter(&t0);
	const uint32_t inBytes = args.remaining();
	if (g_trafficOn) g_traffic.Add(TrafficRow(method), 1, ch.index());
	const bool outerFailed = t_dispatchFailed;  // a served call may nest inside another one's handler
	ServedMethod servedMethod(method);  // issue 2800: WrapTemp checks the borrowed-argument list against it
	t_dispatchFailed = false;
	bool handled = false;
	{
		// Issue 9036 (bug 0102): a C++ exception thrown inside a served call (the EXE's operator new threw std::bad_alloc
		// at turn 420 of the huge save, and so did the proxy's own table growth) had no handler between the EXE's
		// frames and here, so the CRT called terminate() with g_lock held (dialog "This application has requested the
		// Runtime to terminate it in an unusual way", a job thread blocked on g_lock). It now ends this served call as
		// failed and is logged LOUD; the locks taken in the call are released by the unwinding.
		// Ticket 0359 (from 0334's 4254): kRtE_StreamRead is served under the same catch. The EXE's save stream throws
		// when the DLL reads past its end (a truncated or corrupt save, or a save of another player-limit build), and
		// that throw escaped to terminate() too (CRT "Runtime Error!" box, no D118 report).
		const char* what = NULL;
		// Ticket 0451: the exception object (and the text its what() points into) is destroyed when its catch handler
		// ends, so the text is copied here inside the handler; `what` is read after the handlers.
		char whatCopy[256] = "";
		try
		{
			if (method == vpw::kRtE_StreamRead)
				handled = ServeStreamRead(args, ret, ch);
			else if ((method >> 16) == vpw::kRtTypeServerToProxy)
				handled = LuaServe(method, args, ret, ch);
			else
			{
				handled = EngDispatchGenerated(method, args, ret, ch);
				// Ticket 4773: the combat hold, right after the EXE's handler returned (nothing unless route S is on)
				if (handled && !t_dispatchFailed) ProxyCombatHold::AfterServed(method, ret.data(), ret.size());
				// Ticket 0727: the minimap's note of a plot set back to unexplored (nothing unless FixMinimapUnexplored is
				// in effect); the arguments from the call's private copy
				if (handled && !t_dispatchFailed)
					ProxyMinimapFog::AfterServed(method, argsCopy.empty() ? NULL : &argsCopy[0], (uint32_t)argsCopy.size());
			}
		}
		catch (const std::bad_alloc&)
		{
			what = "std::bad_alloc (the EXE's address space is exhausted or fragmented)";
		}
		catch (const std::exception& e)
		{
			const char* w = e.what();
			strncpy_s(whatCopy, sizeof(whatCopy), w ? w : "(no text)", _TRUNCATE);
			what = whatCopy;
		}
		catch (...)
		{
			what = "a C++ exception of unknown type";
		}
		if (what)
		{
			// MC decision 2026-11-12: the EXE did not complete this call, so the game must not go on (the EXE and the
			// server would differ silently). The unwinding released the locks taken in the call; the server is told
			// with kStatusEngineException and ends its session with `exe_exception` (exit 6); the EXE's thread that
			// waits on the server then takes the on_server_death path (Fatal with on_server_death=exit).
			InterlockedIncrement(&g_servedExceptions);
			const bool lockHeld = (DWORD)(ULONG_PTR)g_lock.OwningThread == GetCurrentThreadId();
			char msg[512];
			_snprintf_s(msg, sizeof(msg), _TRUNCATE, "exe_exception: %s threw %s", MethodName(method).c_str(), what);
			LogErr("LOUD: served call %s (0x%08X) threw %s inside the EXE or the proxy (issue 9036); g_lock held by this "
			       "thread after the catch: %s; the server is told to end the session", MethodName(method).c_str(),
			       method, what, lockHeld ? "YES (a defect)" : "no");
			LogFlush();
			// Whatever the handler wrote before the throw stays in front of the message: the server reads a failed
			// RETURN only for its report line (srv_rt.cpp, Status::RemoteError), so that line may be garbled.
			handled = ProxyRt::DispatchFail(ch, ret, vpw::kStatusEngineException, msg);
		}
	}
	Ctr& c = CtrFor(method).in;
	if (!handled)
	{
		Log("served call 0x%08X (%s): unknown method", method, MethodName(method).c_str());
		ch.set_return_status(vpw::kStatusUnknownMethod);
		ret.str("proxy: unknown method");
		t_dispatchFailed = true;
	}
	if (t_dispatchFailed)
	{
		++c.fails;
		if (!c.loggedFail)
		{
			c.loggedFail = true;
			LogErr("served call %s (0x%08X) failed (first failure of this method; all counted)",
			       MethodName(method).c_str(), method);
		}
	}
	t_dispatchFailed = outerFailed;
	Count(c, ret.size(), inBytes, t0);
	if (ProxyBudget::On()) ProxyBudget::Served(method, t0.QuadPart);  // ticket 4652
}

void OnPost(void*, vpipc::Channel& ch, uint32_t method, vpipc::Reader&)
{
	Log("unexpected post 0x%08X (%s) on channel %u: ignored", method, MethodName(method).c_str(), ch.index());
}

void OnIpcLog(void*, vpipc::LogLevel level, const char* message)
{
	if (level == vpipc::LogLevel::Debug) return;
	Log("vpipc: %s", message);
}

// Issue 3521: an attach refused by either side (protocol version, layout or schema hash) during the boot: the
// handshake's failure in the report. Set by the monitor thread's peer event, read by the boot thread.
volatile LONG g_attachRefused = 0;

// The handshake's code when the boot's attach was refused: the wait's mismatch status, else "attach-refused".
bool HandshakeRefused(vpipc::Status lastSt, const char** code)
{
	if (lastSt == vpipc::Status::VersionMismatch || lastSt == vpipc::Status::LayoutMismatch ||
	    lastSt == vpipc::Status::SchemaMismatch)
	{
		*code = vpipc::status_name(lastSt);
		return true;
	}
	*code = "attach-refused";
	return g_attachRefused != 0;
}

void OnPeerEvent(void*, vpipc::PeerEvent ev, uint32_t generation)
{
	if (ev == vpipc::PeerEvent::Refused && g_booting) InterlockedExchange(&g_attachRefused, 1);
	static const char* names[] = {"Attached", "Died", "Closed", "Hung", "Recovered", "Refused"};
	const unsigned i = (unsigned)ev;
	// Issue 9008: after kRt_Shutdown the server's exit is the expected end, not a peer death; vpipc reports it as
	// Died whatever the exit code (Shutdown() logs the code).
	if (g_shutdown && ev == vpipc::PeerEvent::Died)
		Log("vpipc peer event: the server process ended after kRt_Shutdown (generation %u)", generation);
	else
		Log("vpipc peer event: %s (generation %u)", i < 6 ? names[i] : "?", generation);
}

// =============================================================================================== stats
void WriteStats()
{
	if (g_statsWritten || !g_locksReady) return;
	g_statsWritten = true;
	{
		const std::string budget = ProxyBudget::Final();  // ticket 4652: the last turns of budget-proxy.tsv
		if (!budget.empty()) Log("%s", budget.c_str());
	}
	const std::string path = DirOf(g_logPathUsed) + "\\vpproxy-calls.tsv";
	FILE* f = NULL;
	if (fopen_s(&f, path.c_str(), "wb") != 0) f = NULL;
	if (f) fputs("method\tdirection\tcalls\tbytes_out\tbytes_in\tseconds\n", f);
	const double freq = g_qpcFreq.QuadPart ? (double)g_qpcFreq.QuadPart : 1.0;
	uint64_t tc[2] = {}, tb0[2] = {}, tb1[2] = {}, tt[2] = {}, tf = 0, tu = 0, td = 0, tfd[2] = {};
	uint32_t methods = 0;
	struct Emit
	{
		static void One(FILE* f, uint32_t m, const Ctr& c, int dir, double freq, uint64_t* tc, uint64_t* tb0,
		                uint64_t* tb1, uint64_t* tt, uint64_t& tf, uint64_t& tu, uint64_t& td, uint32_t& methods,
		                uint64_t* tfd)
		{
			tf += c.fails;
			tfd[dir] += c.fails;
			tu += c.unimpl;
			td += c.decodeFails;
			// Issue 9008: every method with failed calls, by direction, for the harness's verdict and allow-list.
			if (c.fails)
				Log("failed calls of %s: %s %u", MethodName(m).c_str(), dir ? "in" : "out", (unsigned)c.fails);
			if (!c.calls && !c.local) return;
			++methods;
			tc[dir] += c.calls;
			tb0[dir] += c.bytesOut;
			tb1[dir] += c.bytesIn;
			tt[dir] += c.ticks;
			if (f)
			{
				if (c.calls)
					fprintf(f, "%s\t%s\t%I64u\t%I64u\t%I64u\t%.6f\n", MethodName(m).c_str(), dir ? "in" : "out", c.calls,
					        c.bytesOut, c.bytesIn, (double)c.ticks / freq);
				if (c.local)  // issue 2700: answered by the proxy (direction "local", no bytes, no time)
					fprintf(f, "%s\tlocal\t%I64u\t0\t0\t0.000000\n", MethodName(m).c_str(), c.local);
			}
		}
	};
	for (uint32_t k = 1; k <= kVpKindCount; ++k)
		for (uint32_t i = 0; i < g_ctrKindN[k]; ++i)
		{
			const uint32_t m = (k << 16) | i;
			Emit::One(f, m, g_ctrKinds[k][i].out, 0, freq, tc, tb0, tb1, tt, tf, tu, td, methods, tfd);
			Emit::One(f, m, g_ctrKinds[k][i].in, 1, freq, tc, tb0, tb1, tt, tf, tu, td, methods, tfd);
		}
	for (int t = 0; t < 2; ++t)
		for (uint32_t i = 0; i < 128; ++i)
		{
			const uint32_t m = ((t ? vpw::kRtTypeServerToProxy : vpw::kRtTypeProxyToServer) << 16) | i;
			Emit::One(f, m, g_ctrRt[t][i].out, 0, freq, tc, tb0, tb1, tt, tf, tu, td, methods, tfd);
			Emit::One(f, m, g_ctrRt[t][i].in, 1, freq, tc, tb0, tb1, tt, tf, tu, td, methods, tfd);
		}
	Emit::One(f, 0xFFFFFFFFu, g_ctrOther.out, 0, freq, tc, tb0, tb1, tt, tf, tu, td, methods, tfd);
	Emit::One(f, 0xFFFFFFFFu, g_ctrOther.in, 1, freq, tc, tb0, tb1, tt, tf, tu, td, methods, tfd);
	if (f) fclose(f);
	{
		Lock l(g_lock);
		for (std::map<std::string, uint64_t>::const_iterator it = g_unsupportedByType.begin();
		     it != g_unsupportedByType.end(); ++it)
			Log("unsupported Lua values of type %s: %I64u", it->first.c_str(), it->second);
	}
	Log("totals: calls out %I64u (%I64u bytes out, %I64u bytes in, %.3f s); calls served %I64u (%I64u bytes in, "
	    "%I64u bytes out, %.3f s); %u method rows; failed calls %I64u (outbound %I64u, served %I64u); unimplemented calls "
	    "%I64u; short RETURNs %I64u; unsupported Lua values %I64u; calls after shutdown %u",
	    tc[0], tb0[0], tb1[0], (double)tt[0] / freq, tc[1], tb1[1], tb0[1], (double)tt[1] / freq, methods, tf, tfd[0],
	    tfd[1], tu, td, g_unsupportedLua, g_afterShutdown);
	Log("counters written to %s%s", path.c_str(), f ? "" : " (FAILED to open)");
	ProxyLocal::Report();  // issue 2700
	Log("temporary wrappers: DLL objects handed to engine methods outside the borrowed-argument list %ld%s", g_tempUnlisted,
	    g_tempUnlisted ? " (LOUD: see the first lines above; proxy-design 17.4)" : "");  // issue 2800
	{
		// Issue 9036 (bug 0102): the engine-object table stays small because dead slots are reused.
		Lock l(g_lock);
		Log("engine objects: table slots %u (%u KB), live %u, freed slots waiting %u; served calls ended by a C++ "
		    "exception %ld", (unsigned)g_eng.size(), (unsigned)(g_eng.capacity() * sizeof(EngEntry) / 1024u),
		    (unsigned)g_engIndex.size(), g_engSlots.Waiting(), g_servedExceptions);
	}
	// Issue 2502: the thread-binding checks and the strict allow-list (the line the live run reads).
	Log("binding: Update calls off the core channel %ld; top-level ui calls from other threads than the boot thread %ld; "
	    "unimplemented calls allow-listed %ld; frames %I64u (HasTurnTimerExpired calls on ui %I64u)",
	    g_updateOffCore, g_uiForeign, g_unimplAllowed, g_frames.frames, g_frames.timerCalls);
	{
		char qg[400];
		ProxyQueueGuard::StatusLine(qg, sizeof(qg));  // ticket 0492
		Log("%s", qg);
	}
}

// =============================================================================================== boot
void CloseAfterFailedBoot()
{
	if (g_ep)
	{
		g_ep->close();
		delete g_ep;
		g_ep = NULL;
	}
	if (g_server)
	{
		// Our own child: it normally exits when the client closes; give it a moment, then end it.
		if (WaitForSingleObject(g_server, 2000) != WAIT_OBJECT_0)
		{
			Log("boot: the server (pid %u) did not exit after the close; terminating it", (unsigned)g_serverPid);
			TerminateProcess(g_server, 1);
		}
		CloseHandle(g_server);
		g_server = NULL;
	}
}

// The vpipc monitor thread create_client started: the thread of this process, other than the caller, whose start address
// lies in the proxy image. Its code is the proxy's: it must not run after the image is unmapped (DllMain).
DWORD FindMonitorThread()
{
	const uintptr_t base = (uintptr_t)g_hModule;
	const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_hModule);
	const IMAGE_NT_HEADERS* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
	const uintptr_t end = base + nt->OptionalHeader.SizeOfImage;
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snap == INVALID_HANDLE_VALUE) return 0;
	DWORD found = 0;
	THREADENTRY32 te;
	te.dwSize = sizeof(te);
	for (BOOL ok = Thread32First(snap, &te); ok && !found; ok = Thread32Next(snap, &te))
	{
		if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == GetCurrentThreadId()) continue;
		HANDLE h = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
		if (!h) continue;
		const uintptr_t a = ThreadStartAddress(h);
		CloseHandle(h);
		if (a >= base && a < end) found = te.th32ThreadID;
	}
	CloseHandle(snap);
	return found;
}

// mode=off (decision D28's off switch): the original 32-bit DLL kept beside the proxy answers DllGetGameContext; no
// server, no endpoint. Loaded by full path with the standard search order for its own imports (msvcr90 through its
// manifest, lua51_Win32.dll and the Firaxis DLLs from the EXE's folder), as the EXE loads the DLL.
// Issue 3504: also the fallback of a mode=on boot with pack=1 (fallbackWhy != NULL): a pack that does not fit or a
// 64-bit boot that failed. Then the log may already be open (a failed Boot opened it in its run folder), the reason is
// logged once ("boot: FALLBACK to the 32-bit DLL: ...") and the player's notice (proxy_pack.h ShowNotice) follows the
// load.
ProxyPack::Result g_pack;     // issue 3504: the pack check of this boot (pack=1)
bool g_bootNoFallback = false;  // issue 3504: a failure that must stay visible (the window guard): no fallback

// Ticket 0721 (K30): the 32-bit DLL (fallback_dll) could not be loaded: stage fallback-dll with its code (missing,
// load-failed, no-export); a failure of the 64-bit path before it (the pack check, the 64-bit boot, the EXE-build gate)
// stays in the details, and the player's summary names the DLL (vp64helper's popup shows it as the details).
void FallbackFail(const char* code, const std::string& why)
{
	std::vector<std::string> before;
	if (!g_failStage.empty())
	{
		before.push_back("before it, the 64-bit path failed: stage " + g_failStage + ", code " + g_failCode);
		before.insert(before.end(), g_failDetails.begin(), g_failDetails.end());
	}
	SetFail("fallback-dll", code, why);
	g_failDetails.insert(g_failDetails.end(), before.begin(), before.end());
	const size_t slash = g_cfg.fallbackDll.find_last_of("\\/");  // the file's name only: no folder in the player's words
	const std::string name = slash == std::string::npos ? g_cfg.fallbackDll : g_cfg.fallbackDll.substr(slash + 1);
	g_failSummary = "Details: the game's own 32-bit DLL, which the patch loads in place of its 64-bit part (" + name +
	                " beside the patch's DLL), could not be loaded (fallback-dll: " + code + ").";
}

// Ticket 0635 (D224): [Proxy] GuardsWhenOff of VP64Options.ini, read by BootOff, where the 64-bit side does not
// start and OptionsBoot never runs. The file is only read, never created; its warnings are not logged here (the next
// mode=on boot names them). Returns the option's value (1 = the guard, the default) and in *how where it came from.
// Ticket 0680: *titlePrefix (may be NULL) gets [Shell] WindowTitlePrefix from the same read, for the notice (a title
// that a 64-bit boot earlier in this process prefixed loses the prefix).
int GuardsWhenOffOption(std::string* how, std::string* titlePrefix = NULL)
{
	Vp64Opt::Options o;
	wchar_t path[1024];
	const Vp64Opt::PathSource src = Vp64Opt::DefaultPath(path, sizeof(path) / sizeof(path[0]));
	char u8[3 * 1024 + 1];
	u8[0] = 0;
	const char* status = "no options file path (no Documents folder, or VP64_OPTIONS too long)";
	if (src != Vp64Opt::kPathNone)
	{
		if (!Vp64Opt::WideToUtf8(path, u8, sizeof(u8))) _snprintf_s(u8, sizeof(u8), _TRUNCATE, "(a path not in UTF-8)");
		unsigned warnings = 0;
		const Vp64Opt::LoadStatus st = Vp64Opt::LoadFile(path, o, NULL, NULL, &warnings);
		status = st == Vp64Opt::kLoaded ? "read" : st == Vp64Opt::kMissing ? "missing" : st == Vp64Opt::kUnreadable
		                                                                                 ? "unreadable"
		                                                                                 : "too large";
	}
	char line[3 * 1024 + 300];
	_snprintf_s(line, sizeof(line), _TRUNCATE, "[Proxy] GuardsWhenOff = %d (%s; options file %s%s%s)",
	            o.GuardsWhenOff(), o.fromFile[Vp64Opt::kGuardsWhenOff] ? "from the file" : "default", u8,
	            u8[0] ? ": " : "", status);
	*how = line;
	if (titlePrefix) *titlePrefix = o.WindowTitlePrefix();
	return o.GuardsWhenOff();
}

// Ticket 0747 (D255 G5/G5b): [Logs] KeepRuns, needed by MakeRunFolder before OptionsBoot reads the file (and before the
// log is open). The file is only read here, never created, its warnings not logged (OptionsBoot names them a moment
// later). g_cfg.keepRuns becomes the number in force (ProxyLogic::EffectiveKeepRuns), g_keepRunsWhy where it came from.
// The same number goes to the compositor as VPSHELL_KEEP_LOGS (its kept shell_composite.<stamp>.log files,
// PrepareDefaultLog), unless the EXE's environment had the variable before this proxy set it (a test's); g_keepLogsLine
// says what was done. Both lines are logged by Boot once the log is open.
std::string g_keepRunsWhy, g_keepLogsLine;
void ResolveKeepRuns()
{
	Vp64Opt::Options o;
	wchar_t path[1024];
	if (Vp64Opt::DefaultPath(path, sizeof(path) / sizeof(path[0])) != Vp64Opt::kPathNone)
	{
		unsigned warnings = 0;
		Vp64Opt::LoadFile(path, o, NULL, NULL, &warnings);
	}
	g_cfg.keepRuns = ProxyLogic::EffectiveKeepRuns(o.fromFile[Vp64Opt::kKeepRuns] != 0, o.KeepRuns(), g_cfg.keepRunsSet,
	                                               g_cfg.keepRuns, &g_keepRunsWhy);
	// a boot of this proxy marks its own value with VPSHELL_KEEP_LOGS_BY=vpproxy, so that a later boot in the same process
	// (a reload after a mod change: the proxy's statics start again, the environment does not) sets it again
	wchar_t have[8], by[16];
	char line[200];
	const bool ours = GetEnvironmentVariableW(L"VPSHELL_KEEP_LOGS_BY", by, 16) > 0 && wcscmp(by, L"vpproxy") == 0;
	if (!ours && GetEnvironmentVariableW(L"VPSHELL_KEEP_LOGS", have, 8) > 0)
		_snprintf_s(line, sizeof(line), _TRUNCATE, "VPSHELL_KEEP_LOGS already set in the EXE's environment: kept");
	else
	{
		wchar_t v[16];
		_snwprintf_s(v, sizeof(v) / sizeof(v[0]), _TRUNCATE, L"%u", g_cfg.keepRuns);
		if (SetEnvironmentVariableW(L"VPSHELL_KEEP_LOGS", v))
		{
			SetEnvironmentVariableW(L"VPSHELL_KEEP_LOGS_BY", L"vpproxy");
			_snprintf_s(line, sizeof(line), _TRUNCATE, "VPSHELL_KEEP_LOGS=%u set: the compositor keeps that many older "
			            "shell_composite logs (ticket 0747)", g_cfg.keepRuns);
		}
		else
			_snprintf_s(line, sizeof(line), _TRUNCATE, "VPSHELL_KEEP_LOGS could not be set (error %lu): the compositor "
			            "keeps its default of 10", GetLastError());
	}
	g_keepLogsLine = line;
}

// Ticket 0747 (D255 G5, options-file.md 10.4 (a)): at an orderly shutdown whose server exited by itself with code 0 and
// no error report in this load, the server's copies of the game database in <run>\db are deleted (13.7 MB a run folder;
// nothing reads them after the server's exit: proxy_report.cpp takes logs only). After any failure they stay, for the
// root cause. The server's process has ended, so its SQLite handles are closed (deleting them in the server would mean
// closing the DLL's database under it). Never anything but IsRunDatabaseName's names, never outside <run>\db.
void DeleteRunDatabaseCopies(bool waited, DWORD code)
{
	if (g_runDir.empty()) return;  // server_config= (a test's layout): no run folder of the proxy's
	std::string why;
	if (!ProxyLogic::DeleteRunDatabases(waited, (uint32_t)code, g_reported != 0, &why))
	{
		Log("shutdown: the server's database copies in %s\\db are kept: %s (ticket 0747)", g_runDir.c_str(), why.c_str());
		return;
	}
	const std::string db = g_runDir + "\\db";
	std::vector<std::string> names;
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((db + "\\*").c_str(), &fd);
	if (h != INVALID_HANDLE_VALUE)
	{
		do
			if (!(fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) names.push_back(fd.cFileName);
		while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	const std::vector<std::string> del = ProxyLogic::RunDatabasesToDelete(names);
	unsigned n = 0, failed = 0;
	unsigned long long bytes = 0;
	DWORD firstErr = 0;
	for (size_t i = 0; i < del.size(); ++i)
	{
		const std::string p = db + "\\" + del[i];
		WIN32_FILE_ATTRIBUTE_DATA fa;
		const bool sized = GetFileAttributesExA(p.c_str(), GetFileExInfoStandard, &fa) != 0;
		if (DeleteFileA(p.c_str()))
		{
			++n;
			if (sized) bytes += ((unsigned long long)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
		}
		else
		{
			if (!failed) firstErr = GetLastError();
			++failed;
		}
	}
	if (failed)
		LogErr("shutdown: %u of the server's %u database copies in %s could not be deleted (first error %lu); %u deleted "
		       "(%.1f MB) (ticket 0747)", failed, (unsigned)del.size(), db.c_str(), (unsigned long)firstErr, n,
		       bytes / 1048576.0);
	else
		Log("shutdown: the server exited cleanly: its %u database copies in %s deleted (%.1f MB; %u other files there "
		    "kept) (ticket 0747)", n, db.c_str(), bytes / 1048576.0, (unsigned)(names.size() - del.size()));
}

// Ticket 0745 (D255 G3): [Performance] RenderFrameJobs into server.cfg. shellCfg is ProxyShell::Configure's text; it
// starts with "render=1" exactly when the server's renderer runs this boot (ConfigureRender), and only then is the line
// added: render_frame_jobs=N for 0..8; nothing for -1 (auto), so that the server's own default (srv_frame_jobs.h
// ParseFrameJobsKey "auto": 2 workers on more than 2 logical cores, else 0) decides and the file stays as before the
// key. A test's server_cfg_extra comes after these lines and wins (ServerConfigFile).
std::string RenderFrameJobsLine(const std::string& shellCfg)
{
	const Vp64Opt::Options& o = ProxyRtInt::ProxyOptions();
	const int v = o.RenderFrameJobs();
	const char* from = o.fromFile[Vp64Opt::kRenderFrameJobs] ? "from the file" : "default";
	if (shellCfg.compare(0, 9, "render=1\n") != 0)
	{
		if (v != -1) Log("options: [Performance] RenderFrameJobs = %d (%s): not written, the server draws nothing this boot", v, from);
		return shellCfg;
	}
	if (v < 0)
	{
		Log("options: [Performance] RenderFrameJobs = -1 (%s): automatic, no render_frame_jobs line (the server's own "
		    "choice; ticket 0745)", from);
		return shellCfg;
	}
	char line[64];
	_snprintf_s(line, sizeof(line), _TRUNCATE, "render_frame_jobs=%d\n", v);
	Log("options: [Performance] RenderFrameJobs = %d (%s): server.cfg gets render_frame_jobs=%d (ticket 0745)", v, from, v);
	return shellCfg + line;
}

// Ticket 0680: the title keeper's log lines (from its own thread; proxy_pack.h StartTitleKeeper).
void TitleLog(const char* line) { Log("%s", line); }

ICvGameContext1* BootOff(const char* fallbackWhy = NULL, const char* notice = NULL)
{
	g_offMode = true;
	const char* what = fallbackWhy ? "fallback" : "mode=off";
	const bool logWasOpen = g_logFile != INVALID_HANDLE_VALUE;  // a failed Boot opened it and logged the header
	if (!logWasOpen) OpenLog();
	if (fallbackWhy)
	{
		if (!logWasOpen)
		{
			Log("boot: vpproxy (x86 proxy runtime, epics 2300/2500); proxy DLL %s; process %u; boot thread %u; mode=on "
			    "pack=1; ini %s",
			    ModulePath(g_hModule).c_str(), (unsigned)GetCurrentProcessId(), (unsigned)GetCurrentThreadId(),
			    g_cfg.iniPath.c_str());
			if (!g_cfg.problems.empty()) LogRaw(g_cfg.problems.c_str(), false);
		}
		LogErr("boot: FALLBACK to the 32-bit DLL %s: %s; the 64-bit patch is off for this load of the DLL",
		       g_cfg.fallbackDll.c_str(), fallbackWhy);
	}
	else
	{
		Log("boot: vpproxy (x86 proxy runtime, epics 2300/2500); proxy DLL %s; process %u; boot thread %u; mode=off",
		    ModulePath(g_hModule).c_str(), (unsigned)GetCurrentProcessId(), (unsigned)GetCurrentThreadId());
		if (!g_cfg.iniFound)
			Log("boot: no vpproxy.ini at %s (env VPPROXY_INI %s): mode=off (a missing ini means off)", g_cfg.iniPath.c_str(),
			    getenv("VPPROXY_INI") ? "set" : "not set");
		else if (g_cfg.modeOn && g_cfg.failed)
			Log("boot: ini %s: mode=on but failed=1 (vp64helper failed wrote it after a failure in a D33 session, issue "
			    "3523): the off switch until vp64helper on clears it; fallback_dll=%s", g_cfg.iniPath.c_str(),
			    g_cfg.fallbackDll.c_str());
		else
			Log("boot: ini %s: mode=off fallback_dll=%s", g_cfg.iniPath.c_str(), g_cfg.fallbackDll.c_str());
		if (!g_cfg.problems.empty()) LogRaw(g_cfg.problems.c_str(), false);
	}
	// Ticket 0721 (K30): a 32-bit DLL that cannot be loaded leaves no context to return; the caller (DllGetGameContext)
	// then ends the game on the report route (BootNullNoPlay) instead of handing the EXE a NULL it reads through.
	if (!FileExists(g_cfg.fallbackDll))
	{
		FallbackFail("missing", "fallback_dll " + g_cfg.fallbackDll + ": no such file");
		LogErr("boot FAILED: %s: fallback_dll %s: no such file; no game context (ticket 0721: the report route)", what,
		       g_cfg.fallbackDll.c_str());
		return NULL;
	}
	const double t0 = Now();
	g_fallback = LoadLibraryExA(g_cfg.fallbackDll.c_str(), NULL, 0);
	if (!g_fallback)
	{
		const DWORD err = GetLastError();
		char d[700];
		_snprintf_s(d, sizeof(d), _TRUNCATE, "LoadLibraryEx(%s): Windows error %lu", g_cfg.fallbackDll.c_str(), err);
		FallbackFail("load-failed", d);
		LogErr("boot FAILED: %s: LoadLibraryEx(%s): error %u; no game context (ticket 0721: the report route)", what,
		       g_cfg.fallbackDll.c_str(), (unsigned)err);
		return NULL;
	}
	g_fallbackGet = reinterpret_cast<DllGetGameContextFn>(GetProcAddress(g_fallback, "DllGetGameContext"));
	if (!g_fallbackGet)
	{
		FallbackFail("no-export", g_cfg.fallbackDll + " exports no DllGetGameContext");
		LogErr("boot FAILED: %s: %s exports no DllGetGameContext; no game context (ticket 0721: the report route)", what,
		       g_cfg.fallbackDll.c_str());
		FreeLibrary(g_fallback);
		g_fallback = NULL;
		return NULL;
	}
	ICvGameContext1* ctx = g_fallbackGet();
	Log("boot: %s: the original 32-bit DLL %s loaded at %p (%.1f ms); its DllGetGameContext returned %p; every "
	    "later DllGetGameContext call is forwarded to it; no server is started",
	    what, ModulePath(g_fallback).c_str(), g_fallback, (Now() - t0) * 1000.0, ctx);
	std::string how, offPrefix;
	const int guardsWhenOff = GuardsWhenOffOption(&how, &offPrefix);  // ticket 0680: one read gives the title prefix too
	if (!ctx)
		LogErr("boot: LOUD: %s: the 32-bit DLL's own DllGetGameContext returned NULL: handed to the EXE as that DLL gave "
		       "it, as without the patch (the EXE reads through it at once; ticket 0721)", what);
	if (fallbackWhy && ctx)
		Log("boot: %s", ProxyPack::ShowNotice(notice ? notice : "the 64-bit server did not start", offPrefix).c_str());
	if (ctx)
	{
		// Ticket 0635 (D224): the proxy's guard of the EXE's UI message queue with the patch switched off too, installed
		// after the 32-bit DLL's first DllGetGameContext, so that a DLL bringing its own guard (the -ia test DLL) has
		// hooked the slot first and is not hooked on top (InstallWhenOff refuses then, LOUD).
		if (guardsWhenOff)
		{
			Log("boot: %s: %s: the proxy installs its guards: the UI message queue guard, the farm builder guard "
			    "(ticket 0635, D224, D225)", what, how.c_str());
			ProxyQueueGuard::InstallWhenOff();
			ProxyShell::OnBootOff();  // D225: the farm builder guard with rule 2
		}
		else
			LogErr("LOUD: boot: %s: %s: the guards are LEFT OUT (the UI message queue guard, the farm builder guard); the "
			       "EXE runs unguarded, as in a game without the patch (ticket 0635)", what, how.c_str());
	}
	return ctx;
}

// Ticket 0635 (MC #9064; D118, D125): the patch switched off (mode=off, or failed=1) while config.ini is still in the
// patch's form (a D33 session: the EXE's fog of war, forests, decals and yield icons switched off, which the 32-bit DLL
// cannot draw: the "red map" of 0626's installed stock route). That is a degraded world, so: no play, the D118 report
// with the player's words, vp64helper failed --pid (which gives config.ini its player values back after the game's end,
// when vp64helper on recorded them), then the end (exit code 3). Never returns. The pack fallback in a D33 session is
// already no play (issue 3523, below in DllGetGameContext); the exe-build gate's A'' form too (ticket 0588).
void OffD33NoPlay(const std::string& sessionWhy)
{
	g_offMode = true;
	if (g_logFile == INVALID_HANDLE_VALUE) OpenLog();
	Log("boot: vpproxy (x86 proxy runtime, epics 2300/2500); proxy DLL %s; process %u; boot thread %u; %s",
	    ModulePath(g_hModule).c_str(), (unsigned)GetCurrentProcessId(), (unsigned)GetCurrentThreadId(),
	    g_cfg.modeOn ? "mode=on with failed=1 (the off switch)" : "mode=off");
	if (!g_cfg.problems.empty()) LogRaw(g_cfg.problems.c_str(), false);
	const std::string why = std::string("the patch is switched off (") +
	                        (g_cfg.modeOn ? "vpproxy.ini failed=1" : (g_cfg.iniFound ? "vpproxy.ini mode=off" : "no vpproxy.ini")) +
	                        "), but config.ini is in the patch's form: " + sessionWhy;
	SetFail("off-switch", "config-d33", why);
	// The popup's own two paragraphs for this stage are vp64helper.cpp's (CmdFailed, MC #9080); this line follows them
	// as the details (and is popup-summary.txt).
	g_failSummary = std::string("Details: the patch was switched off (") +
	                (g_cfg.modeOn ? "vpproxy.ini failed=1" : "vpproxy.ini mode=off") +
	                ") while config.ini still switched off the game's own fog of war, forests and other map systems "
	                "(off-switch: config-d33).";
	LogErr("boot: NO PLAY: %s; a 32-bit game with these settings would draw a broken map (a degraded world, D118): the "
	       "report, vp64helper failed, then the end of the game (ticket 0635)", why.c_str());
	InterlockedExchange(&g_reported, 1);
	ReportFailure(ProxySession::kD33, sessionWhy);
	Fatal(3, "the patch is switched off but config.ini is in the patch's form (no play, D118, ticket 0635)");
}

// Ticket 0721 (K30; D118, D185, D223): the boot has no game context to return. The EXE never checks DllGetGameContext's
// result: its context getter (build 4390913 0x66A390) calls the export through the pointer it took with GetProcAddress
// (0x2DD201C) and stores the result unchecked, and the boot's caller reads the context's vtable at once (0x47B95E: a
// read of address 0 on the boot thread; build 26577625 has the same code at its own addresses, kept out of git by
// D202; research/03-dll-engine-boundary/proxy-design.md section 34). So a NULL is never handed over: every
// path of the proxy that has no context left ends here: a failed 64-bit boot without a 32-bit fallback (pack=0; the
// window guard's refusal), and a 32-bit DLL that could not be loaded (mode=off, the pack's fallback, the EXE-build
// gate). The report (stage and code as the failure set them), vp64helper failed --pid (D240: the patch stays on, the
// popup offers "Remove 64bit patch"; stage fallback-dll has its own words), then the end (exit code 3). Never returns.
// A NULL from the 32-bit DLL's own DllGetGameContext is not the proxy's failure and is handed over as it is (BootOff).
void BootNullNoPlay()
{
	if (g_logFile == INVALID_HANDLE_VALUE)
	{
		OpenLog();
		Log("boot: vpproxy (x86 proxy runtime, epics 2300/2500); proxy DLL %s; process %u; boot thread %u; mode=%s pack=%d; "
		    "ini %s", ModulePath(g_hModule).c_str(), (unsigned)GetCurrentProcessId(), (unsigned)GetCurrentThreadId(),
		    g_cfg.modeOn ? "on" : "off", g_cfg.pack ? 1 : 0, g_cfg.iniPath.c_str());
		if (!g_cfg.problems.empty()) LogRaw(g_cfg.problems.c_str(), false);
	}
	if (g_failStage.empty()) SetFail("server-start", "boot", g_bootErr.empty() ? std::string("(no reason logged)") : g_bootErr);
	std::string sessionWhy;
	const ProxySession::Kind kind = ProxySession::SessionKind(ProxySession::ReadMasks(), &sessionWhy);
	LogErr("boot: NO PLAY: the boot failed (stage %s, code %s) and there is no game context to return: the EXE would read "
	       "through a NULL at once (K30): the report, vp64helper failed, then the end of the game (ticket 0721); %s",
	       g_failStage.c_str(), g_failCode.c_str(), sessionWhy.c_str());
	if (InterlockedCompareExchange(&g_reported, 1, 0) == 0)
		ReportFailure(kind, sessionWhy, true);
	else
		LogErr("boot: the report of this load of the proxy was written already: not a second one (ticket 0721)");
	Fatal(3, "the boot failed and there is no game context to return (no play, D118, ticket 0721)");
}

// Ticket 0641 (D118): the server's own reason when it ended on purpose, <run>\server-fatal.txt (server/src/remote/
// srv_exe_crash.h FatalRecord: lines "exit: N", "code: <token>", "summary: <the player's words>", "reason: <why>"). Taken
// only when its exit code is the server's (when that is readable): a file of an earlier server is never another's reason.
struct ServerFatal
{
	bool have;
	unsigned long exitCode;
	std::string code, summary, reason;
	ServerFatal() : have(false), exitCode(0) {}
};
ServerFatal ReadServerFatal()
{
	ServerFatal f;
	if (g_runDir.empty()) return f;
	std::string text;
	if (!ReadTextFile(g_runDir + "\\server-fatal.txt", text)) return f;
	bool haveExit = false, havePid = false;
	unsigned long pid = 0;
	size_t at = 0;
	while (at < text.size())
	{
		size_t e = text.find('\n', at);
		if (e == std::string::npos) e = text.size();
		std::string ln = text.substr(at, e - at);
		at = e + 1;
		while (!ln.empty() && (ln[ln.size() - 1] == '\r' || ln[ln.size() - 1] == ' ')) ln.erase(ln.size() - 1);
		const size_t c = ln.find(": ");
		if (c == std::string::npos) continue;
		const std::string k = ln.substr(0, c), v = ln.substr(c + 2);
		if (k == "exit")
		{
			char* end = NULL;
			const long n = strtol(v.c_str(), &end, 10);
			haveExit = end && end != v.c_str() && !*end;
			f.exitCode = (unsigned long)n;
		}
		else if (k == "pid")
		{
			char* end = NULL;
			const unsigned long n = strtoul(v.c_str(), &end, 10);
			havePid = end && end != v.c_str() && !*end;
			pid = n;
		}
		else if (k == "code") f.code = v;
		else if (k == "summary") f.summary = v;
		else if (k == "reason") f.reason = v;
	}
	if (!haveExit || f.code.empty() || f.summary.empty())
	{
		LogErr("report: %s\\server-fatal.txt has no exit, code or summary line: not used (ticket 0641)", g_runDir.c_str());
		return f;
	}
	DWORD code = STILL_ACTIVE;
	const bool ended = g_server && GetExitCodeProcess(g_server, &code) && code != STILL_ACTIVE;
	// Ticket 0744 (K31): a record of this server's pid is its reason also when it then died by an exception code
	std::string why;
	const ProxyReport::FatalUse use =
	    ProxyReport::UseFatalRecord(ended, (unsigned long)code, (unsigned long)g_serverPid, f.exitCode, havePid, pid, &why);
	if (use == ProxyReport::kFatalUseNo)
	{
		LogErr("report: %s\\server-fatal.txt names exit %lu, the server ended with %lu: not this server's reason, not used "
		       "(%s; tickets 0641, 0744)", g_runDir.c_str(), f.exitCode, (unsigned long)code, why.c_str());
		return f;
	}
	if (use == ProxyReport::kFatalUsePid)
		LogErr("report: %s\\server-fatal.txt names exit %lu and this server's pid %lu; the server then ended by exception "
		       "code 0x%08lX before its own exit: its record is used (ticket 0744)", g_runDir.c_str(), f.exitCode, pid,
		       (unsigned long)code);
	f.have = true;
	return f;
}

// Issues 3521, 3523 (D119, D125): the error report of a failure of the 64-bit path (pack=1) and the helper's start. The
// caller has claimed g_reported, set g_fail* and found the session's kind; afterwards it goes on: a stock session as
// before (the 32-bit fallback at the boot; on_server_death in a game), a D33 session ends the game (Fatal). Every step
// logs one line; nothing here can stop the caller's own way on.
bool ReportFailure(ProxySession::Kind kind, const std::string& sessionWhy, bool bootEnded)
{
	// Ticket 0721: d33 = the game ends with this report (a D33 session, or a boot with no context to return); the report's
	// own d33 field stays the session's kind
	const bool d33 = kind == ProxySession::kD33 || bootEnded;
	const double t0 = Now();
	if (g_logFile == INVALID_HANDLE_VALUE) OpenLog();
	if (g_failStage.empty()) SetFail("server-start", "boot", g_bootErr.empty() ? std::string("(no reason logged)") : g_bootErr);
	// Ticket 0641: the server's own reason (server-fatal.txt): in [failure] always; a death in a game takes its code and its
	// player's words as the popup's summary (a save-variant refusal keeps its own words, ticket 0354)
	const ServerFatal sf = ReadServerFatal();
	if (sf.have)
	{
		LogErr("report: the server's own reason (server-fatal.txt, ticket 0641): exit %lu, code %s: %s", sf.exitCode,
		       sf.code.c_str(), sf.reason.c_str());
		char ex[32];
		_snprintf_s(ex, sizeof(ex), _TRUNCATE, "%lu", sf.exitCode);
		g_failDetails.push_back("the server's reason (server-fatal.txt): exit " + std::string(ex) + ", code " + sf.code + ": " +
		                        sf.reason);
		if (g_failStage == "server-death") g_failCode = sf.code;
		// Ticket 0681: the server saw the DLL's game data mismatch of the loaded save (srv_remote_savemods.h): a stage of
		// its own, so that vp64helper's popup opens with the mods, not with a failure of the patch
		if (g_failStage == "server-death" && sf.code == "save-mod-mismatch") g_failStage = "save-mods";
	}
	LogErr("report: the 64-bit path failed: stage %s, code %s; %s; writing the error report and starting vp64helper %s",
	       g_failStage.c_str(), g_failCode.c_str(), sessionWhy.c_str(), d33 ? "failed" : "popup");
	LogFlush();
	ProxyReport::Input in;
	in.stage = g_failStage;
	in.code = g_failCode;
	in.details = g_failDetails;
	in.d33 = kind == ProxySession::kD33;
	in.bootEnded = bootEnded;
	in.summary = "The 64-bit part of the patch failed (" + g_failStage + ": " + g_failCode + "); " +
	             (d33 ? std::string("the game was ended.")  // D240 (0682): what happens next is vp64helper's popup's
	                  : std::string(g_failStage == "server-death" || g_failStage == "save-variant" || g_failStage == "save-mods"
	                                    ? "the game had to end."
	                                    : "the game goes on as the stock game."));
	if (g_failStage == "save-variant" && g_failSaveMajors && g_failRunMajors)
	{
		// Ticket 0354: in words a player understands, both variants named; the stage and code stay in report.txt.
		char sv[400];
		_snprintf_s(sv, sizeof(sv), _TRUNCATE,
		            "This save was made with the %d-civilization version of Vox Populi, but the installed patch is the "
		            "%d-civilization version. The save was not loaded%s",
		            g_failSaveMajors, g_failRunMajors,
		            d33 ? "; the game was ended." : ", so the game had to end.");
		in.summary = sv;
	}
	if (sf.have && (g_failStage == "server-death" || g_failStage == "save-mods"))
		in.summary = sf.summary;  // ticket 0641: the server's own words (0681: save-mods too)
	if (!g_failSummary.empty()) in.summary = g_failSummary;  // ticket 0588: the unknown EXE build's own line
	{
		char id[1024];
		ProxyBuildIdText(g_hModule, id, sizeof(id));
		in.patch = id;
	}
	const ProxyPack::Manifest& m = g_pack.m;
	in.packRelease = m.release;
	{
		ProxyReport::Hashed h;
		h.label = "32-bit DLL (fallback_dll)";
		h.path = g_cfg.fallbackDll;
		for (size_t i = 0; i < m.x86Shas.size(); ++i)
		{
			if (!h.expectedNote.empty()) h.expectedNote += "; ";
			h.expectedNote += m.x86Shas[i] + " (" + (i < m.x86Origins.size() ? m.x86Origins[i] : std::string("?")) + ")";
		}
		if (!h.expectedNote.empty()) h.expectedNote = "one of the manifest's x86_dlls: " + h.expectedNote;
		in.hashes.push_back(h);
		h = ProxyReport::Hashed();
		h.label = "x64 DLL";
		h.path = m.x64File.empty() ? g_cfg.x64Dll : g_cfg.x64Dir + "\\" + m.x64File;  // issue 2606: no fixed name
		h.expected = m.x64Sha;
		in.hashes.push_back(h);
		if (!m.purityFile.empty())
		{
			h = ProxyReport::Hashed();
			h.label = "purity file";
			h.path = g_cfg.x64Dir + "\\" + m.purityFile;
			h.expected = m.puritySha;
			in.hashes.push_back(h);
		}
		h = ProxyReport::Hashed();
		h.label = "proxy";
		h.path = ModulePath(g_hModule);
		in.hashes.push_back(h);
		h = ProxyReport::Hashed();
		h.label = "server.exe (server_exe)";
		h.path = g_cfg.serverExe;
		in.hashes.push_back(h);
	}
	in.copies.push_back(g_cfg.x64Dir + "\\" VP64PACK_MANIFEST);
	in.copies.push_back(g_cfg.iniPath);
	{
		wchar_t opt[1024];
		if (Vp64Opt::DefaultPath(opt, sizeof(opt) / sizeof(opt[0])) != Vp64Opt::kPathNone) in.copiesW.push_back(opt);
	}
	in.tails.push_back(g_logPathUsed);
	if (!g_runDir.empty())
	{
		in.runDir = g_runDir;
		WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA((g_runDir + "\\*.log").c_str(), &fd);
		if (h != INVALID_HANDLE_VALUE)
		{
			do
			{
				const std::string p = g_runDir + "\\" + fd.cFileName;
				if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && _stricmp(p.c_str(), g_logPathUsed.c_str()) != 0)
					in.tails.push_back(p);
			} while (FindNextFileA(h, &fd));
			FindClose(h);
		}
	}
	{
		const ProxySession::Masks mk = ProxySession::ReadMasks();
		char line[300];
		in.session.push_back(sessionWhy);
		in.session.push_back("masks from " + std::string(ProxySession::MaskSourceName(mk.src)) + ": " + mk.why);
		if (mk.src != ProxySession::kMaskNone)
		{
			_snprintf_s(line, sizeof(line), _TRUNCATE, "config mask 0x%08X; keys off: %s", mk.config,
			            ProxySession::ClearedKeys(mk.config).c_str());
			in.session.push_back(line);
			if (mk.haveView)
			{
				_snprintf_s(line, sizeof(line), _TRUNCATE, "view mask 0x%08X; systems off in the view: %s", mk.view,
				            ProxySession::ClearedKeys(mk.view).c_str());
				in.session.push_back(line);
			}
		}
		_snprintf_s(line, sizeof(line), _TRUNCATE, "vpproxy.ini: mode=%s%s pack=%d on_server_death=%s; this load of the "
		            "proxy: %s", g_cfg.modeOn ? "on" : "off", g_cfg.failed ? " failed=1" : "", g_cfg.pack ? 1 : 0,
		            g_cfg.onDeathExit ? "exit" : "continue",
		            g_bootState == 1 ? "booted (a game may be running)" : "booting");
		in.session.push_back(line);
		char qg[400];
		ProxyQueueGuard::StatusLine(qg, sizeof(qg));  // ticket 0492
		in.session.push_back(qg);
	}
	std::wstring cfgIni;
	if (ProxySession::ConfigIniPath(&cfgIni))
	{
		if (!ProxyReport::ActivatedMods(cfgIni, &in.mods, &in.modsWhy)) in.mods.clear();
	}
	else
		in.modsWhy = "the Documents folder could not be found";
	std::wstring root, dir;
	std::vector<std::string> lines;
	if (!ProxyReport::Root(&root))
		lines.push_back("report: FAILED: the Documents folder could not be found; no report");
	else
		ProxyReport::Write(in, root, &dir, &lines);
	for (size_t i = 0; i < lines.size(); ++i)
		if (lines[i].compare(0, 16, "report: written ") == 0)
			Log("%s", lines[i].c_str());
		else
			LogErr("%s", lines[i].c_str());
	std::string helperLine;
	if (dir.empty()) dir = root;
	const bool started = ProxySession::StartHelper(ProxySession::Widen(g_cfg.proxyDir),
	                                               ProxySession::VerbFor(d33, GetCurrentProcessId(), cfgIni), dir,
	                                               g_cfg.iniFound ? ProxySession::Widen(g_cfg.iniPath) : std::wstring(),
	                                               &helperLine);
	if (started)
		Log("%s", ProxyReport::Scrub(helperLine).c_str());
	else
		LogErr("%s", ProxyReport::Scrub(helperLine).c_str());
	Log("report: done in %.1f ms; %s", (Now() - t0) * 1000.0,
	    bootEnded ? "no game context to return: no play, the game ends now (ticket 0721)"
	    : d33     ? "a D33 session: no play, the game ends now (D125)"
	              : "a stock session: it goes on as before (the 32-bit DLL at the boot; on_server_death in a game)");
	LogFlush();
	return started;
}

ICvGameContext1* Boot()
{
	const double tStart = Now();
	if (g_cfg.serverConfig.empty())
	{
		ResolveKeepRuns();  // ticket 0747: [Logs] KeepRuns before the pruning (and VPSHELL_KEEP_LOGS for the compositor)
		g_runDir = MakeRunFolder();
	}
	OpenLog();
	Log("boot: vpproxy (x86 proxy runtime, epics 2300/2500); proxy DLL %s; process %u; boot thread %u; schema "
	    "0x%016I64x; mode=on",
	    ModulePath(g_hModule).c_str(), (unsigned)GetCurrentProcessId(), (unsigned)GetCurrentThreadId(), kAppSchemaHash);
	if (!g_cfg.problems.empty()) LogRaw(g_cfg.problems.c_str(), false);
	Log("boot: ini %s: server_exe=%s server_config=%s db=%s on_server_death=%s strict=%d strict_allow=%s "
	    "connect_timeout_ms=%u server_args=%s host=%s counters=%d pack=%d",
	    g_cfg.iniPath.c_str(), g_cfg.serverExe.c_str(),
	    g_cfg.serverConfig.empty() ? "(written by the proxy)" : g_cfg.serverConfig.c_str(), g_cfg.db.c_str(),
	    g_cfg.onDeathExit ? "exit" : "continue", g_cfg.strict ? 1 : 0, g_cfg.strictAllow.c_str(),
	    g_cfg.connectTimeoutMs, g_cfg.serverArgs.c_str(), g_cfg.singleThreadHost ? "headless" : "exe",
	    g_cfg.counters ? 1 : 0, g_cfg.pack ? 1 : 0);
	if (g_cfg.pack) Log("boot: %s", g_pack.why.c_str());  // issue 3504: checked in DllGetGameContext before Boot
	if (g_cfg.serverConfig.empty())
	{
		Log("boot: ini (D28 layout) x64_dir=%s run_root=%s keep_runs=%u game_dir=%s loc_db=%s server_allow=%s",
		    g_cfg.x64Dir.c_str(), g_cfg.runRoot.c_str(), g_cfg.keepRuns, g_cfg.gameDir.c_str(), g_cfg.locDb.c_str(),
		    g_cfg.serverAllow.c_str());
		Log("boot: run folders kept: %u (%s); %s", g_cfg.keepRuns, g_keepRunsWhy.c_str(), g_keepLogsLine.c_str());
		for (size_t i = 0; i < g_runPruned.size(); ++i)
			Log("boot: old run folder %s removed (keep_runs %u)", g_runPruned[i].c_str(), g_cfg.keepRuns);
		if (g_runDir.empty())
		{
			SetFail("server-start", "run-folder", g_runWhy);
			LogErr("boot FAILED: %s; DllGetGameContext returns NULL", g_runWhy.c_str());
			return NULL;
		}
		Log("boot: run folder %s", g_runDir.c_str());
	}
	// Ticket 0419 (D118 for an EXE crash, part 2): the chained top-level exception filter; on a crash in the game's
	// process it writes <run>\exe-crash.txt (code, module + RVA, thread; no memory, D83) for the server's exe-crash
	// report (0418) and one LOUD line here, then hands on to the filter it replaced.
	{
		char line[700];
		ProxyCrashFilter::Install(g_runDir.empty() ? "" : (g_runDir + "\\exe-crash.txt").c_str(), g_logFile,
		                          GetCurrentThreadId(), &Now, line, sizeof(line));
		Log("boot: %s", line);
	}
	if (!FileExists(g_cfg.serverExe))
	{
		SetFail("server-start", "server-missing", "server_exe " + g_cfg.serverExe + ": no such file");
		LogErr("boot FAILED: server_exe %s: no such file; DllGetGameContext returns NULL", g_cfg.serverExe.c_str());
		return NULL;
	}
	if (!g_cfg.strictAllow.empty())
	{
		std::string text;
		if (!ReadTextFile(g_cfg.strictAllow, text))
		{
			SetFail("server-start", "strict-allow", "strict_allow " + g_cfg.strictAllow + " cannot be read");
			LogErr("boot FAILED: strict_allow %s cannot be read; DllGetGameContext returns NULL", g_cfg.strictAllow.c_str());
			return NULL;
		}
		g_strictAllow.Parse(text);
		if (!g_strictAllow.problems.empty())
		{
			LogErr("boot: strict allow-list %s has entries without a reason (not honoured):", g_cfg.strictAllow.c_str());
			LogRaw(g_strictAllow.problems.c_str(), true);
		}
		Log("boot: strict allow-list %s: %u entries (logged and counted, not fatal under strict=1)",
		    g_cfg.strictAllow.c_str(), (unsigned)g_strictAllow.keys.size());
		for (size_t i = 0; i < g_strictAllow.keys.size(); ++i)
			Log("boot: strict allow-list: %s   %s", g_strictAllow.keys[i].c_str(), g_strictAllow.reasons[i].c_str());
	}
	// One-off 9042: the player's options file (VP64Options.ini); the server reads the same file (options_file=).
	// Ticket 0624: the session's kind decides how a missing file is created; then the renderer guard, its one place: a D33
	// session with ServerRenderer = 0 is a boot failure (pack=1: NO PLAY, the report, vp64helper failed; D118, D125).
	std::string optSessionWhy;
	const bool optD33 = ProxySession::SessionKind(ProxySession::ReadMasks(), &optSessionWhy) == ProxySession::kD33;
	const std::string optionsFile = ProxyRtInt::OptionsBoot(g_cfg.x64Dir, optD33, optSessionWhy);
	{
		const std::string refused = ProxyRtInt::OptionsGuard(optD33, optSessionWhy, g_cfg.serverConfig.empty());
		if (!refused.empty())
		{
			SetFail("server-start", "options-renderer-off", refused);
			g_failSummary = "The 64-bit part of the patch failed (server-start: options-renderer-off): the patch is switched "
			                "on, but its options file VP64Options.ini says ServerRenderer = 0 (or was deleted and could not "
			                "be re-created with the release's options), so the game would show a degraded world (no fog of "
			                "war, no forests). The game was ended. To play with the patch, run vp64helper.exe on (it sets "
			                "ServerRenderer = 1).";
			LogErr("boot FAILED: %s; DllGetGameContext returns NULL", refused.c_str());
			return NULL;
		}
	}
	// Issue 3305: [Renderer] ServerRenderer, the composite's check and the names (render=1, render_section=).
	// Ticket 0745 (D255 G3): with them, [Performance] RenderFrameJobs as render_frame_jobs (RenderFrameJobsLine).
	const std::string shellCfg = RenderFrameJobsLine(ProxyShell::Configure(g_cfg.serverConfig.empty()));
	{
		// Ticket 0624 item 5 (MC #8762): a D33 session whose ServerRenderer = 1 composite was refused (a partial or
		// damaged install) is the same degraded world: the same D125 route, its own reason code.
		const std::string refused = ProxyRtInt::CompositeGuard(optD33, optSessionWhy, g_cfg.serverConfig.empty(),
		                                                       ProxyShell::RefusedComposite());
		if (!refused.empty())
		{
			SetFail("server-start", "composite-refused", refused);
			g_failSummary = "The 64-bit part of the patch failed (server-start: composite-refused): the patch's compositor "
			                "(shell_composite.dll beside the patch's DLL) is missing, damaged or from another version, so the "
			                "game would show a degraded world. The patch's installation is partial or damaged: run the "
			                "patch's installer again. The game was ended.";
			LogErr("boot FAILED: %s; DllGetGameContext returns NULL", refused.c_str());
			return NULL;
		}
	}
	std::string serverCfg = g_cfg.serverConfig;
	// Issue 0307 (TEST ONLY): server_cfg_extra= appends its lines to the written server.cfg after every other line. A
	// file that cannot be read or parsed, or the key beside server_config=, is a boot failure (the D125 route), never
	// a silent skip; every appended line is logged LOUD.
	std::vector<std::string> extra;
	if (!g_cfg.serverCfgExtra.empty())
	{
		std::string why;
		if (!serverCfg.empty())
			why = "cannot be applied with server_config= (the proxy writes no server.cfg)";
		else if (!ProxyLogic::LoadServerCfgExtra(g_cfg.serverCfgExtra, extra, why))
			why = g_cfg.serverCfgExtra + ": " + why;
		if (!why.empty())
		{
			SetFail("server-start", "server-cfg-extra", "server_cfg_extra (TEST ONLY) " + why);
			LogErr("boot FAILED: server_cfg_extra (TEST ONLY) %s; DllGetGameContext returns NULL", why.c_str());
			return NULL;
		}
		LogErr("LOUD: TEST ONLY: vpproxy.ini server_cfg_extra=%s: %u line(s) appended to server.cfg (issue 0307; never "
		       "in a shipped ini)", g_cfg.serverCfgExtra.c_str(), (unsigned)extra.size());
		for (size_t i = 0; i < extra.size(); ++i) LogErr("LOUD: TEST ONLY: server.cfg += %s", extra[i].c_str());
	}
	if (serverCfg.empty())
	{
		serverCfg = g_runDir + "\\server.cfg";
		const std::string text = ProxyLogic::ServerConfigFile(ProxyLogic::ServerConfigText(g_cfg, g_runDir), optionsFile,
		                                                      shellCfg, g_cfg.serverCfgExtra, extra);
		FILE* f = NULL;
		if (fopen_s(&f, serverCfg.c_str(), "wb") != 0 || !f)
		{
			SetFail("server-start", "server-cfg", "cannot write " + serverCfg);
			LogErr("boot FAILED: cannot write %s; DllGetGameContext returns NULL", serverCfg.c_str());
			return NULL;
		}
		fputs(text.c_str(), f);
		fclose(f);
		Log("boot: server config written to %s", serverCfg.c_str());
	}
	InitCounters();
	{
		// Ticket 4652: the turn budget's section and file, before the server starts (it opens the section at kRt_Boot)
		const std::string budget = ProxyBudget::Init((uint32_t)GetCurrentProcessId(), DirOf(g_logPathUsed));
		if (!budget.empty()) Log("%s", budget.c_str());
	}
	// Issue 2700: local answers (proxy_local.cpp); local_answers=0 = every call crosses.
	ProxyLocal::Configure(g_cfg.localAnswers, g_cfg.localVerify,
	                      g_cfg.localTest == "stale" ? 1 : (g_cfg.localTest == "frozen" ? 2 : 0));
	// Issue 3504: the purity lists bound to the x64 DLL (proxy_purity.h; the file format is issue 3503's), before the
	// tables; every mode=on boot loads (or tries to), so ProxyLocal::InitTables has no load of its own (3503's interim
	// removed). pack=1: Load(the pack's purity file, held open and checked against the manifest, the manifest's x64
	// SHA-256). pack=0 (development and test layouts): VP_PROXY_PURITY="<purity.tsv>|<x64 DLL>" when set (headless test
	// runs), else <x64_dir>\purity.tsv bound to the x64 DLL there (issue 2606: CvGameCore_Expansion2_x64.dll when present,
	// else CvGameCore_Expansion2.dll); without either the lists stay empty.
	{
		std::string why;
		bool ok = false;
		if (g_cfg.pack)
			ok = ProxyPurity::Load(g_pack.purityPath.c_str(), g_pack.m.x64Sha.c_str(), &why);
		else
		{
			char probe[8];
			if (GetEnvironmentVariableA("VP_PROXY_PURITY", probe, sizeof(probe)) != 0)
				ok = ProxyPurity::LoadFromTestEnvironment(&why);
			else
				ok = ProxyPurity::LoadBesideX64(g_cfg.x64Dir.c_str(), &why);
		}
		Log("boot: purity: %s (pack=%d): %s (methods %u, Lua names %u)", ok ? "loaded" : "not loaded", g_cfg.pack ? 1 : 0,
		    why.c_str(), (unsigned)ProxyPurity::MethodCount(), (unsigned)ProxyPurity::LuaCount());
		ProxyPack::ReleasePurity(g_pack);
	}
	ProxyLocal::InitTables();
	if (g_cfg.counters)
	{
		char name[64];
		_snprintf_s(name, sizeof(name), _TRUNCATE, "Local\\VPProxySnapshot_%u", (unsigned)GetCurrentProcessId());
		g_snapEvent = CreateEventA(NULL, FALSE, FALSE, name);
		Log("boot: counters on; snapshot event %s %s; snapshots go to %s\\vpproxy-snapshots.tsv", name,
		    g_snapEvent ? "created" : "NOT created", DirOf(g_logPathUsed).c_str());
	}
	TS();  // the boot thread's state (logs it once)

	// 1. the client endpoint
	vpipc::EndpointConfig ec;
	ec.appSchemaHash = kAppSchemaHash;
	if (g_cfg.replySpinUs) ec.wait.replySpinUs = g_cfg.replySpinUs;
	if (g_cfg.idleSpinUs) ec.wait.idleSpinUs = g_cfg.idleSpinUs;
	Log("boot: wait replySpinUs %u idleSpinUs %u", ec.wait.replySpinUs, ec.wait.idleSpinUs);
	ec.onLog = OnIpcLog;
	ec.onPeerEvent = OnPeerEvent;
	vpipc::Endpoint* ep = NULL;
	const vpipc::Status cs = vpipc::Endpoint::create_client(ec, &ep);
	if (cs != vpipc::Status::Ok || !ep)
	{
		SetFail("server-start", "endpoint", std::string("create_client: ") + vpipc::status_name(cs) + ": " +
		                                        vpipc::Endpoint::last_error_text());
		LogErr("boot FAILED: create_client: %s: %s", vpipc::status_name(cs), vpipc::Endpoint::last_error_text());
		return NULL;
	}
	g_ep = ep;
	for (uint32_t i = 0; i < ep->channel_count(); ++i) ep->channel(i).set_handlers(OnCall, OnPost, NULL);
	ep->bind_current_thread(vpipc::kChannelUi);
	g_monitorTid = FindMonitorThread();
	Log("boot: endpoint %s created, %u channels, vpipc monitor thread %u (%.1f ms)", ep->section_name(),
	    ep->channel_count(), (unsigned)g_monitorTid, (Now() - tStart) * 1000.0);

	// 2. the server process
	std::string cmd = "\"" + g_cfg.serverExe + "\" --remote --section \"" + ep->section_name() + "\" --config \"" +
	                  serverCfg + "\"";
	if (!g_cfg.serverArgs.empty()) cmd += " " + g_cfg.serverArgs;
	std::vector<char> cmdBuf(cmd.begin(), cmd.end());
	cmdBuf.push_back(0);
	STARTUPINFOA si;
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi;
	memset(&pi, 0, sizeof(pi));
	const std::string cwd = DirOf(g_cfg.serverExe);
	// The server's working directory is its own folder (never the EXE's); no console window; no handle inherited.
	Log("boot: starting %s (cwd %s; CREATE_NO_WINDOW; handles not inherited)", cmd.c_str(), cwd.c_str());
	if (!CreateProcessA(NULL, &cmdBuf[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, cwd.c_str(), &si, &pi))
	{
		const DWORD err = GetLastError();
		char d[1024];
		char text[512] = "";
		// English text where the system has it (a report is read by the developers), else the system's language
		if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err,
		                    MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), text, sizeof(text), NULL))
			FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0, text, sizeof(text), NULL);
		for (char* c = text; *c; ++c)
			if (*c == '\r' || *c == '\n') *c = ' ';
		_snprintf_s(d, sizeof(d), _TRUNCATE, "CreateProcess(%s): Win32 error %lu: %s", g_cfg.serverExe.c_str(), err, text);
		SetFail("server-start", "create-process", d);
		SetLastError(err);
		LogErr("boot FAILED: CreateProcess: error %u", (unsigned)GetLastError());
		CloseAfterFailedBoot();
		return NULL;
	}
	CloseHandle(pi.hThread);
	g_server = pi.hProcess;
	g_serverPid = pi.dwProcessId;
	Log("boot: server pid %u (%.1f ms)", (unsigned)g_serverPid, (Now() - tStart) * 1000.0);

	// 3. wait for it to attach, watching the process
	const double tWait = Now();
	vpipc::Status lastSt = vpipc::Status::Ok;
	for (;;)
	{
		const vpipc::Status st = ep->wait_for_server(20);
		if (st == vpipc::Status::Ok) break;
		if (st != vpipc::Status::Timeout && st != lastSt) Log("boot: wait_for_server: %s", vpipc::status_name(st));
		lastSt = st;
		if (WaitForSingleObject(g_server, 0) == WAIT_OBJECT_0)
		{
			DWORD code = 0;
			GetExitCodeProcess(g_server, &code);
			{
				// Issue 3521: a refused attach (protocol, layout or schema hash) is the handshake's failure.
				// Race fix (issue 3522, proxy-design.md 21.7): a server that refused exits at once, and this thread can see the
				// exit before the vpipc monitor thread has raised PeerEvent::Refused (9 us apart, measured). Give the monitor
				// up to 250 ms to report a refusal before the failure is classified; only when the exit code is non-zero.
				for (int i = 0; code != 0 && i < 50 && !g_attachRefused; ++i) Sleep(5);
				char d[300];
				_snprintf_s(d, sizeof(d), _TRUNCATE, "the server exited with code %lu (0x%08lX) before it attached; endpoint: %s",
				            code, code, ep->last_error());
				const char* rcode = NULL;
				const bool refused = HandshakeRefused(lastSt, &rcode);
				SetFail(refused ? "handshake" : "server-start", refused ? rcode : "server-exit", d);
				if (refused)
				{
					_snprintf_s(d, sizeof(d), _TRUNCATE, "the proxy's schema hash 0x%016I64x", kAppSchemaHash);
					g_failDetails.push_back(d);
				}
			}
			LogErr("boot FAILED: the server exited with code %u (0x%08X) before it attached (%.1f ms); endpoint: %s",
			       (unsigned)code, (unsigned)code, (Now() - tWait) * 1000.0, ep->last_error());
			CloseAfterFailedBoot();
			return NULL;
		}
		if ((Now() - tWait) * 1000.0 >= (double)g_cfg.connectTimeoutMs)
		{
			{
				char d[300];
				_snprintf_s(d, sizeof(d), _TRUNCATE, "the server did not attach within connect_timeout_ms %u; endpoint: %s",
				            g_cfg.connectTimeoutMs, ep->last_error());
				const char* rcode = NULL;
				const bool refused = HandshakeRefused(lastSt, &rcode);
				SetFail(refused ? "handshake" : "server-start", refused ? rcode : "attach-timeout", d);
				if (refused)
				{
					_snprintf_s(d, sizeof(d), _TRUNCATE, "the proxy's schema hash 0x%016I64x", kAppSchemaHash);
					g_failDetails.push_back(d);
				}
			}
			LogErr("boot FAILED: the server did not attach within connect_timeout_ms %u; endpoint: %s",
			       g_cfg.connectTimeoutMs, ep->last_error());
			CloseAfterFailedBoot();
			return NULL;
		}
	}
	Log("boot: server attached, generation %u, peer pid %u (waited %.1f ms)", ep->generation(), ep->peer_pid(),
	    (Now() - tWait) * 1000.0);
	if (!ProxyGuard::CheckServerGuard(g_server, g_serverPid))  // issue 2703
	{
		g_bootNoFallback = true;  // issue 3504: a test-only safety refusal stays visible (never a quiet fallback)
		SetFail("server-start", "guard", "the server's window guard (CIVBG_GUARD=1, a test switch) is not complete: "
		                                 "refused (the boot FAILED: guard line above)");  // ticket 0721
		CloseAfterFailedBoot();
		return NULL;
	}

	// Issue 2700: the lock-want words exist before the server boots (it opens them at kRt_Boot).
	ProxyLocal::InitShared();
	// 4. kRt_Boot: the server loads the x64 DLL and returns its game context
	const double tBoot = Now();
	vpipc::Channel& ch = ProxyRt::Chan();
	vpipc::Writer& w = ProxyRt::Begin(ch, vpw::kRt_Boot);
	w.u32((uint32_t)GetCurrentProcessId());
	w.str("");
	vpipc::Reader r;
	if (!ProxyRt::End(ch, vpw::kRt_Boot, r))
	{
		SetFail("handshake", "boot-call", "kRt_Boot did not return");
		LogErr("boot FAILED: kRt_Boot did not return");
		CloseAfterFailedBoot();
		return NULL;
	}
	const uint32_t ok = r.u32();
	uint32_t msgLen = 0;
	const char* msg = r.cstr(&msgLen);
	const std::string message = msg ? std::string(msg, msgLen) : std::string();
	const vpipc::Handle h = r.handle();
	const uint32_t spid = r.u32();
	if (!r.ok() || !ok)
	{
		SetFail("handshake", "dll-boot", "kRt_Boot (the server's load of the x64 DLL): '" + message + "'" +
		                                     (r.ok() ? "" : " (short RETURN)"));
		LogErr("boot FAILED: kRt_Boot: ok %u, message '%s'%s", ok, message.c_str(), r.ok() ? "" : " (short RETURN)");
		CloseAfterFailedBoot();
		return NULL;
	}
	Log("boot: kRt_Boot ok (%.1f ms): '%s', context handle {0x%08X, %d}, server pid %u", (Now() - tBoot) * 1000.0,
	    message.c_str(), h.type, h.id, spid);
	if (vpw::Kind(h.type) != kT_ICvGameContext3 || vpw::IsEngine(h.type))
	{
		char d[200];
		_snprintf_s(d, sizeof(d), _TRUNCATE, "the context handle's kind is %u, expected %u (ICvGameContext3)",
		            vpw::Kind(h.type), kT_ICvGameContext3);
		SetFail("handshake", "context-kind", d);  // ticket 0721
		LogErr("boot FAILED: the context handle's kind is %u (%s), expected %u (ICvGameContext3)", vpw::Kind(h.type),
		       vpk_iface_name(vpw::Kind(h.type)) ? vpk_iface_name(vpw::Kind(h.type)) : "?", kT_ICvGameContext3);
		CloseAfterFailedBoot();
		return NULL;
	}
	void* p = ProxyRt::Wrap(h);
	if (p) ProxyRt::PinContext(p);
	if (p) ProxyLocal::AddPermanent(h);  // issue 2700: the context singleton's getters may be mirrored
	if (!p)
	{
		SetFail("handshake", "context-wrap", "no wrapper for the context handle");  // ticket 0721
		LogErr("boot FAILED: no wrapper for the context handle");
		CloseAfterFailedBoot();
		return NULL;
	}
	Log("boot: done in %.1f ms; DllGetGameContext returns the context wrapper %p", (Now() - tStart) * 1000.0, p);
	LogFlush();
	return static_cast<ICvGameContext1*>(static_cast<ICvGameContext3*>(p));
}
}  // namespace

// ================================================================================================= ProxyRtInt
namespace ProxyRtInt
{
void Log(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	LogV(false, fmt, ap);
	va_end(ap);
}

void ServerHung(const char* reason)   // ticket 0691
{
	ServerHungRoute(reason);
}

void LogFlush()
{
	if (!g_locksReady) return;
	Lock l(g_logLock);
	if (g_logFile != INVALID_HANDLE_VALUE) FlushFileBuffers(g_logFile);
	fflush(stderr);
}

double Now()
{
	if (!g_qpcFreq.QuadPart) return 0.0;
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return (double)(t.QuadPart - g_qpc0.QuadPart) / (double)g_qpcFreq.QuadPart;
}

bool Booted() { return g_ep != NULL && g_bootState == 1; }

void SetEngineUtility(ICvEngineUtility1* p)
{
	g_engineUtility = p;
	if (!p) g_engineScript = NULL;
}

ICvEngineUtility1* EngineUtility() { return g_engineUtility; }

ICvEngineScriptSystem1* EngineScriptSystem()
{
	if (!g_engineScript && g_engineUtility)
	{
		g_engineScript = g_engineUtility->GetScriptSystem();
		Log("engine script system: %p (from the engine utility %p)", g_engineScript, g_engineUtility);
	}
	return g_engineScript;
}

const char* DbPath() { return g_cfg.db.c_str(); }

uint32_t CurrentChannelIndex()
{
	ThreadState* ts = TS();
	if (!ts) return 0u;
	// An undecided thread's next call is not GetGame/Update when it asks this (Lua paths): it will be aux.
	return ts->decided ? ts->chan : (uint32_t)ProxyLogic::kChanAux;
}

void Snapshot(const char* label) { TakeSnapshot(label); }

// Issue 2700: a call answered locally. Counted per method (local), in the traffic table (direction local) and fed to
// the frame marker like a crossing of the same method.
void NoteLocal(uint32_t method)
{
	++CtrFor(method).out.local;
	ThreadState* ts = TS();
	if (!g_trafficOn || !ts || !ts->decided) return;
	g_traffic.Add(TrafficRow(method), 2, ts->chan);
	const ProxyLogic::Role role = RoleOf(method);
	if (role == ProxyLogic::kRoleNone || ts->chan != ProxyLogic::kChanUi) return;
	bool frame;
	if (role == ProxyLogic::kRoleTurnTimer)
	{
		LARGE_INTEGER q;
		QueryPerformanceCounter(&q);
		Lock l(g_snapLock);
		frame = g_frames.OnUiCall(role, q.QuadPart, g_qpcFreq.QuadPart);
	}
	else
		frame = g_frames.OnUiCall(role, 0, 0);
	if (frame) PollSnapshotEvent();
}

bool ThreadChannel(uint32_t& chan)
{
	ThreadState* ts = TS();
	if (!ts || !ts->decided) return false;
	chan = ts->chan;
	return true;
}

void LogErrPublic(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	LogV(true, fmt, ap);
	va_end(ap);
}

void StrictViolation(int exitCode, const char* why)
{
	if (g_cfg.strict) Fatal(exitCode, why);
}

void Shutdown()
{
	if (!g_ep || g_shutdown) return;
	const double t0 = Now();
	{
		char line[400];  // ticket 4783 (MC #8582 condition 3): route S's Combat bit back as found, before the server goes
		if (ProxyRouteS::OnUnload(line, sizeof(line))) Log("shutdown: %s", line);
	}
	TakeSnapshot("shutdown");
	Log("shutdown: SetDLLIFace(NULL) forwarded; sending kRt_Shutdown");
	InterlockedExchange(&g_shutdown, 1);
	vpipc::Channel& ch = ProxyRt::Chan();
	vpipc::Writer& w = ProxyRt::Begin(ch, vpw::kRt_Shutdown);
	w.u32(0);
	vpipc::Reader r;
	if (ProxyRt::End(ch, vpw::kRt_Shutdown, r))
	{
		const char* rep = r.cstr();
		Log("shutdown: server report %s (%.1f ms)", rep ? rep : "(none)", (Now() - t0) * 1000.0);
	}
	if (g_server)
	{
		const DWORD wr = WaitForSingleObject(g_server, 30000);
		DWORD code = 0;
		GetExitCodeProcess(g_server, &code);
		if (wr == WAIT_OBJECT_0)
			Log("shutdown: server pid %u exited with code %u (%.1f ms)", (unsigned)g_serverPid, (unsigned)code,
			    (Now() - t0) * 1000.0);
		else
		{
			// Issue 2502: an unload must leave no server behind.
			TerminateProcess(g_server, 1);
			LogErr("shutdown: server pid %u still running 30 s after kRt_Shutdown: terminated", (unsigned)g_serverPid);
		}
		DeleteRunDatabaseCopies(wr == WAIT_OBJECT_0, code);  // ticket 0747: only after a clean exit (code 0, no report)
		CloseHandle(g_server);
		g_server = NULL;
	}
	g_ep->close();  // joins the monitor thread; the endpoint object stays allocated until DllMain's detach
	Log("shutdown: endpoint closed (%.1f ms)", (Now() - t0) * 1000.0);
	// Ticket 0805 (proxy-design 38): the engine's Lua VM outlives this image (a mod change unloads it and loads it
	// again): this image's Lua values are made inert and its persistent engine Lua thread freed, here on the thread of
	// SetDLLIFace(NULL) while the image is mapped and nothing serves a Lua call any more.
	ProxyLuaExec::OnShutdown(GetCurrentThreadId() == g_bootThread);
	WriteStats();
	LogFlush();
	InterlockedExchange(&g_shutdownDone, 1);
}

void CountUnsupported(const char* luaType)
{
	Lock l(g_lock);
	++g_unsupportedLua;
	++g_unsupportedByType[luaType ? luaType : "?"];
}

uint8_t* CopyRest(const vpipc::Reader& src, vpipc::Reader& dst)
{
	const uint32_t n = src.ok() ? src.remaining() : 0u;
	uint8_t* p = static_cast<uint8_t*>(malloc(n ? n : 4u));
	if (!p)
	{
		dst.reset(NULL, 0);
		return NULL;
	}
	if (n) memcpy(p, src.cursor(), n);
	dst.reset(p, n);
	return p;
}
}  // namespace ProxyRtInt

// ================================================================================================= ProxyRt
namespace ProxyRt
{
vpipc::Channel& Chan()
{
	ThreadState* ts = TS();
	if (!g_ep || !ts) return g_dummyChannel;
	if (!ts->decided) return g_pendingChannel;  // Begin() chooses by the first call's role; End() resolves it
	return RealChannel(ts);
}

// A binding violation (proxy_rt_logic.h CheckCall): counted, logged loudly (the first 20 of each), fatal (exit 5)
// under strict=1.
void BindingViolation(uint32_t v, uint32_t chan, uint32_t method)
{
	volatile LONG& n = (v & ProxyLogic::kViolUpdateOffCore) ? g_updateOffCore : g_uiForeign;
	const LONG k = InterlockedIncrement(&n);
	if (k <= 20)
		LogErr("BINDING: %s %s on channel %s from thread %u (boot thread %u)%s",
		       (v & ProxyLogic::kViolUpdateOffCore) ? "ICvGame1::Update arrived as" : "a top-level call",
		       MethodName(method).c_str(), ProxyLogic::ChannelName(chan), (unsigned)GetCurrentThreadId(),
		       (unsigned)g_bootThread, k == 20 ? " (further cases counted only)" : "");
	if (g_cfg.strict) Fatal(5, "strict=1 and a call broke the thread binding");
}

vpipc::Writer& Begin(vpipc::Channel&, uint32_t method)
{
	const bool forcePure = ProxyLocal::TakeForcePure();  // issue 2801 (A6): taken before anything can fail
	ThreadState* ts = TS();
	if (ts && g_ep)
	{
		const ProxyLogic::Role role = RoleOf(method);
		if (!ts->decided) Decide(ts, method, role);
		const bool topLevel = ts->depth == 0 && ts->scopeDepth == 0;
		const uint32_t v = ProxyLogic::CheckCall(ts->chan, ts->isBoot, topLevel, role, g_cfg.singleThreadHost);
		if (v) BindingViolation(v, ts->chan, method);
		if (g_trafficOn)
		{
			g_traffic.Add(TrafficRow(method), 0, ts->chan);
			if (role != ProxyLogic::kRoleNone && ts->chan == ProxyLogic::kChanUi)
			{
				bool frame;
				if (role == ProxyLogic::kRoleTurnTimer)
				{
					LARGE_INTEGER q;
					QueryPerformanceCounter(&q);
					Lock l(g_snapLock);
					frame = g_frames.OnUiCall(role, q.QuadPart, g_qpcFreq.QuadPart);
				}
				else
					frame = g_frames.OnUiCall(role, 0, 0);
				if (frame) PollSnapshotEvent();
			}
		}
		// The Update hook (issue 2502): the Lua channel polls on the game-core thread at the top of each Update,
		// before it is forwarded (proxy_update.cpp), as the DLL's own channel polls at the top of CvGame::update.
		if (role == ProxyLogic::kRoleUpdate && !ts->inUpdateHook)
		{
			if (g_trafficOn) PollSnapshotEvent();
			ts->inUpdateHook = true;
			ProxyRtInt::BeforeUpdate();
			ts->inUpdateHook = false;
		}
	}
	StageFrame* f = ts ? PushFrame(ts) : NULL;
	if (!f)
	{
		Log("call %s: staging depth exceeded (%u); the call fails", MethodName(method).c_str(), kMaxStage);
		return DummyWriter();
	}
	f->method = method;
	// issue 2700: Init/Uninit/database caching drop canonical handles; issue 2801: again at their end (End)
	f->genChange = ProxyLocal::BeforeCall(method);
	ProxyLocal::CallStart(method, forcePure, f->impure);  // issue 2700: an impure call invalidates the mirror (start and end)
	QueryPerformanceCounter(&f->t0);
	f->w.reset(f->buf, sizeof(f->buf), true);
	return f->w;
}

bool End(vpipc::Channel& chIn, uint32_t method, vpipc::Reader& r)
{
	ThreadState* ts = TS();
	if (!ts || ts->depth == 0)
	{
		Log("call %s: End without Begin", MethodName(method).c_str());
		r.reset(NULL, 0);
		return false;
	}
	vpipc::Channel& ch = &chIn == &g_pendingChannel ? RealChannel(ts) : chIn;
	StageFrame* f = ts->frames[--ts->depth];
	if (f->method != method) Log("call %s: End does not match Begin (%s)", MethodName(method).c_str(), MethodName(f->method).c_str());
	struct AfterOuterUpdate  // ticket 4665 (D165 option 2): declared before epochEnd, so it runs after the epoch's end
	{
		bool on;
		~AfterOuterUpdate() { if (on && !std::uncaught_exception()) ProxyRtInt::AfterUpdate(); }
	} afterUpdate = {ts->depth == 0 && !ts->inUpdateHook && g_ep && RoleOf(method) == ProxyLogic::kRoleUpdate};
	struct EpochEnd  // issue 2700: the end of an impure call increments the epoch again, on every return path
	{
		bool impure, genChange;
		~EpochEnd()
		{
			ProxyLocal::AfterCall(genChange);  // issue 2801: the generation changes again at the end
			ProxyLocal::CallEnd(impure);
		}
	} epochEnd = {f->impure, f->genChange};
	f->impure = false;
	f->genChange = false;
	const LARGE_INTEGER t0 = f->t0;
	if (CallsBlocked(method, ch, r)) return false;
	if (f->w.overflowed())
	{
		Log("call %s: arguments could not be staged (out of memory)", MethodName(method).c_str());
		r.reset(NULL, 0);
		return false;
	}
	const uint32_t n = f->w.size();
	const bool budget = ProxyBudget::On();  // ticket 4652: this thread has a call in flight from here
	const int64_t budgetT0 = budget ? ProxyBudget::OutBegin() : 0;
	vpipc::Writer& cw = ch.begin_call(method);
	if (n)
	{
		uint8_t* p = cw.reserve(n);
		if (p) memcpy(p, f->w.data(), n);
	}
	// The frame is free from here on (a nested call during end_call may reuse it); drop a large spill now.
	if (f->w.spilled() && n > 1024u * 1024u)
	{
		f->w.~Writer();
		new (&f->w) vpipc::Writer();
	}
	const bool ok = Finish(ch, method, n, t0, r);
	if (budget) ProxyBudget::OutEnd(method, budgetT0);
	return ok;
}

void DecodeCheck(uint32_t method, const vpipc::Reader& r)
{
	if (r.ok()) return;
	Ctr& c = CtrFor(method).out;
	++c.decodeFails;
	if (!c.loggedDecode)
	{
		c.loggedDecode = true;
		LogErr("call %s: the RETURN was shorter than the signature (first time; counted)", MethodName(method).c_str());
	}
}

vpipc::Handle HandleOf(const void* pInterface)
{
	vpipc::Handle h = {0, 0};
	if (!pInterface) return h;
	{
		Lock l(g_lock);
		std::map<const void*, vpipc::Handle>::const_iterator it = g_wrappers.find(pInterface);
		if (it != g_wrappers.end()) return it->second;
		++g_ctrOther.out.unknownHandle;
		if (g_badHandleLogs >= 20) return h;
		++g_badHandleLogs;
	}
	LogErr("HandleOf(%p): not a live wrapper; sent as the null handle%s", pInterface,
	       g_badHandleLogs == 20 ? " (further cases counted only)" : "");
	return h;
}

void* Wrap(vpipc::Handle h)
{
	if (h.type == 0 && h.id == 0) return NULL;
	if (vpw::IsEngine(h.type))
	{
		LogErr("Wrap({0x%08X, %d}): an engine handle where a DLL object was expected", h.type, h.id);
		return NULL;
	}
	void* p = VpProxyNewWrapper(h);
	if (!p)
		LogErr("Wrap({0x%08X, %d}): no wrapper class for kind %u (%s)", h.type, h.id, vpw::Kind(h.type),
		       vpk_iface_name(vpw::Kind(h.type)) ? vpk_iface_name(vpw::Kind(h.type)) : "?");
	return p;
}

// Issue 2800 (item 4 of epic 2700 closed, MC 2026-11-11): the EXE borrows a DLL-object argument of these engine methods
// for the call only (read in the EXE, issue 2804, server/evidence/2800/w4/temp-wrapper-verdicts.txt; proxy-design
// section 17.4), so freeing the temporary wrapper at the call's end (keep_temp=0) is what the in-process DLL does. A
// DLL object handed to any other engine method is logged LOUD and counted: the list must be extended by reading that
// handler before the result can be trusted. Keys are "Interface::method(" prefixes of vpk_method_key.
static const char* const kBorrowedArgMethods[] = {
	// the 29 seen in the live runs of epics 2500 and 2700
	"ICvUserInterface1::isCitySelected(", "ICvEngineUtility1::GameplayUnitShouldDimFlag(",
	"ICvUserInterface1::SetSpecificCityInfoDirty(", "ICvEngineUtility1::GameplayPlotStateChange(",
	"ICvEngineUtility1::GameplayUnitTeleported(", "ICvEngineUtility1::GameplayUnitVisibility(",
	"ICvEngineUtility1::GameplayYieldMightHaveChanged(", "ICvEngineUtility1::GameplayUnitFortify(",
	"ICvEngineUtility1::GameplayUnitCreated(", "ICvEngineUtility1::GameplayUnitSetDamage(",
	"ICvEngineUtility1::GameplayUnitWork(", "ICvEngineUtility1::GameplayWonderCreated(",
	"ICvEngineUtility1::GameplayCitySetDamage(", "ICvUserInterface1::UpdateCountryBorder(",
	"ICvUserInterface1::RemoveFromSelectionList(", "ICvEngineUtility1::GameplayUnitDestroyed(",
	"ICvEngineUtility1::GameplayUnitGarrison(", "ICvEngineUtility1::GameplayUnitEmbark(",
	"ICvEngineUtility1::GameplayCityCreated(", "ICvEngineUtility1::GameplayDoFX(",
	"ICvEngineUtility1::GameplayWallCreated(", "ICvEngineUtility1::GameplayCityPopulationChanged(",
	"ICvUserInterface1::AddDeferredWonderCommand(", "ICvEngineUtility1::GameplayUnitDestroyedInCombat(",
	"ICvUserInterface1::InsertIntoSelectionList(", "ICvEngineUtility1::GameplayFeatureChanged(",
	"ICvEngineUtility1::GameplayCityCaptured(", "ICvUserInterface1::SetScratchDeal(",
	"ICvEngineUtility1::GameplayUnitActivate(",
	// never seen live, read in the EXE as borrowing (issue 2804)
	"ICvUserInterface1::setLastSelectedUnit(", "ICvUserInterface1::selectUnit(", "ICvUserInterface1::selectGroup(",
	"ICvUserInterface1::selectAll(", "ICvUserInterface1::selectCity(", "ICvUserInterface1::addSelectedCity(",
	"ICvUserInterface1::centerCamera(", "ICvUserInterface1::VisuallyDeselectUnit(",
	"ICvUserInterface1::updateCityScreen(", "ICvUserInterface1::lookAt(", "ICvUserInterface1::AddHexToUIRange(",
	"ICvEngineUtility1::GameplayUnitMoved(", "ICvEngineUtility1::GameplayCityDestroyed(",
	"ICvEngineUtility1::GameplayUnitRebased(",
	// bug 0101 (issue 9036, 2026-11-11): seen live on the huge save (turns 350-420) and read in the EXE as borrowing:
	// MissionEnd 168 -> 0x633860 (unit getters; 0x65C910 passes getter values to 0x591930), ResetAnimationState
	// 170 -> 0x6F2E30, SpaceshipCreated 192 -> 0x551E90, SpaceshipEdited 193 -> 0x5DDE50 (not seen live),
	// SpaceshipRemoved 194 -> 0x4D8A80 (plot getters, values into queued messages); no QueryInterface, no Destroy, no
	// store of the argument (proxy-design 17.4)
	"ICvEngineUtility1::GameplayUnitMissionEnd(", "ICvEngineUtility1::GameplayUnitResetAnimationState(",
	"ICvEngineUtility1::GameplaySpaceshipCreated(", "ICvEngineUtility1::GameplaySpaceshipEdited(",
	"ICvEngineUtility1::GameplaySpaceshipRemoved(",
	// bug 0124 (issue 3309, 2026-11-13): NaturalWonderRevealed 175 -> 0x741BB0, read in the EXE as borrowing: plot slot 40
	// IsRevealed(active team, false), then slot 15 GetPosition and slot 53 GetPlotIndex, values into a queued message;
	// the argument is only the `this` of its own getters: no QueryInterface, no Destroy, no store (proxy-design 17.4)
	"ICvEngineUtility1::GameplayNaturalWonderRevealed(",
	// ticket 0329 (2026-11-15): the combat pair, read in the EXE as borrowing (combat-on-the-product-path.md section 4,
	// proxy-design 17.4). UnitCombat 161 -> 0x67DD90 (ticket 0316: met live with Quick Combat off), CityCombat 167 ->
	// 0x48CCA0: the ICvCombatInfo1 argument is loaded once into a callee-saved register and is only the `this` of its
	// own getters (slots 2-16 and 2-14); no slot 0 (QueryInterface), no slot 1 (Destroy), never stored, never passed to
	// another call; the values go into a message built on the stack and queued (0x87E3D0 / 0x87E4A0, then 0x813D60)
	"ICvEngineUtility1::GameplayUnitCombat(", "ICvEngineUtility1::GameplayCityCombat(",
	// ticket 0344 (2026-11-15): the multiplayer deal sends, read in the EXE as borrowing (proxy-design 17.4).
	// sendNetDealAccepted 90 -> 0x476D90 (met live by ticket 0322: an AI accepting the human's deal), sendNetDemandAccepted
	// 91 -> 0x77DB50: a net message built on the stack (ctor 0x444AF0 / 0x788D20) copies the argument into a deal of
	// its own (GetGame()->GetGameDeals()->CreateDeal(), GetDeal(id)->CopyFrom(argument)) and keeps only that id; the
	// argument is passed to CopyFrom inside the call and nothing else: no QueryInterface, no Destroy, no store. The send
	// (0x445E30) writes the copy (GetDeal(id)->Write), the message's destructor (0x42F7B0 / 0x6E6960) DestroyDeals it
	"ICvEngineUtility1::sendNetDealAccepted(", "ICvEngineUtility1::sendNetDemandAccepted(",
};

static bool BorrowedArgMethod(uint32_t method)
{
	static std::map<uint32_t, bool>* s_cache = NULL;
	Lock l(g_lock);
	if (!s_cache) s_cache = new std::map<uint32_t, bool>();
	std::map<uint32_t, bool>::const_iterator it = s_cache->find(method);
	if (it != s_cache->end()) return it->second;
	const char* key = vpk_method_key(method);
	bool ok = false;
	for (size_t i = 0; key && i < sizeof(kBorrowedArgMethods) / sizeof(kBorrowedArgMethods[0]); ++i)
		if (strncmp(key, kBorrowedArgMethods[i], strlen(kBorrowedArgMethods[i])) == 0) ok = true;
	(*s_cache)[method] = ok;
	return ok;
}

static void CheckBorrowedArg(vpipc::Handle h)
{
	const uint32_t m = t_servedMethod;
	if (m == 0 || BorrowedArgMethod(m)) return;
	const LONG n = InterlockedIncrement(&g_tempUnlisted);
	if (n <= 20)
		LogErr("LOUD: engine method %s received a DLL object ({0x%08X, %d}) and is not in the borrowed-argument list "
		       "(issue 2800, proxy-design 17.4): the EXE may keep it past the call, where the proxy frees it; read its "
		       "handler and extend the list%s", MethodName(m).c_str(), h.type, h.id, n == 20 ? " (further ones counted only)" : "");
}
void* WrapTemp(vpipc::Handle h)
{
	void* p = Wrap(h);
	if (!p) return NULL;
	CheckBorrowedArg(h);
	ThreadState* ts = TS();
	if (!ts || ts->scopeDepth == 0)
	{
		LogErr("WrapTemp({0x%08X, %d}) outside a served call: the wrapper is kept", h.type, h.id);
		return p;
	}
	ts->scopes[ts->scopeDepth - 1].push_back(p);
	return p;
}

void ForgetWrapper(const void* pInterface)
{
	Lock l(g_lock);
	g_wrappers.erase(pInterface);
	g_wrapperRefs.erase(pInterface);
}

void AddWrapper(const void* pInterface, vpipc::Handle h)
{
	Lock l(g_lock);
	g_wrappers[pInterface] = h;
}

void WrapperAddRef(const void* pInterface)
{
	Lock l(g_lock);
	++g_wrapperRefs[pInterface];
}

static const void* g_pinnedContext = NULL;  // issue 2500: the context wrapper (proxy_rt.h IsPinned)

bool IsPinned(const void* pInterface) { return pInterface != NULL && pInterface == g_pinnedContext; }
bool LocalRelease(const void* pInterface, uint32_t destroyMethod) { return ProxyLocal::Release(pInterface, destroyMethod) != 0; }

volatile LONG g_tempKept = 0;
bool KeepIfTemp(const void* pInterface)
{
	if (!g_cfg.keepTemp) return false;  // keep_temp=0 (default): the behaviour of epics 2300-2500
	ThreadState* ts = TS();
	if (!ts || ts->scopeDepth == 0) return false;
	bool found = false;
	for (uint32_t d = 0; d < ts->scopeDepth && !found; ++d)
	{
		std::vector<const void*>& v = ts->scopes[d];
		for (size_t i = 0; i < v.size(); ++i)
			if (v[i] == pInterface)
			{
				// Issue 2708: the scope keeps its own reference (the call's borrowed argument stays in use after the
				// engine releases what it QueryInterfaced: EXE 0x557D1E Destroy, 0x557D20 next use); the wrapper moves
				// to the scope's kept list, which releases that reference instead of deleting the wrapper.
				v.erase(v.begin() + i);
				ts->kept[d].push_back(pInterface);
				found = true;
				break;
			}
	}
	if (!found) return false;
	vpipc::Handle h = HandleOf(pInterface);
	const uint32_t qi = (uint32_t)vpk_most_derived(vpw::Kind(h.type)) << 16;  // QueryInterface is index 0
	static const GUID kUnknown = ICvUnknown::GetInterfaceId();
	vpipc::Channel& ch = Chan();
	vpipc::Writer& w = Begin(ch, qi);
	w.handle(h);
	w.bytes(&kUnknown, 16u);
	vpipc::Reader r;
	vpipc::Handle h2 = {0, 0};
	if (End(ch, qi, r))
	{
		h2 = r.handle();
		DecodeCheck(qi, r);
	}
	const LONG n = InterlockedIncrement(&g_tempKept);
	if (h2.type != h.type || h2.id != h.id)
		LogErr("KeepIfTemp(%p): the server answered {0x%08X, %d} for {0x%08X, %d}; the wrapper keeps its handle", pInterface,
		       h2.type, h2.id, h.type, h.id);
	else if (n <= 20)
		Log("temporary wrapper %p {0x%08X, %d} kept: the engine took a reference (issue 2503 F6)", pInterface, h.type, h.id);
	return true;
}
void PinContext(const void* pInterface) { g_pinnedContext = pInterface; }

// Issue 2708: the engine released its last reference to a wrapper that KeepIfTemp promoted, while the call that
// passed it is still being served (the EXE's GameplayUnitCreated: QueryInterface 0x557CF5, Destroy 0x557D1E). The
// wrapper goes back to its dispatch scope (it dies with the call, as without keep_temp) and the promotion's server
// reference is released now, by a nested crossing inside the handler (the RETURN is not written yet).
static void DemoteIfKept(const void* pInterface)
{
	ThreadState* ts = TS();
	if (!ts) return;
	bool found = false;
	for (uint32_t d = 0; d < ts->scopeDepth && !found; ++d)
	{
		std::vector<const void*>& v = ts->kept[d];
		for (size_t i = 0; i < v.size(); ++i)
			if (v[i] == pInterface)
			{
				v.erase(v.begin() + i);
				ts->scopes[d].push_back(pInterface);
				found = true;
				break;
			}
	}
	if (!found) return;
	const vpipc::Handle h = HandleOf(pInterface);
	const uint32_t destroy = ((uint32_t)vpk_most_derived(vpw::Kind(h.type)) << 16) | 1u;  // Destroy is index 1
	vpipc::Channel& ch = Chan();
	vpipc::Writer& w = Begin(ch, destroy);
	w.handle(h);
	vpipc::Reader r;
	if (End(ch, destroy, r)) DecodeCheck(destroy, r);
}

bool WrapperRelease(const void* pInterface)
{
	{
		Lock l(g_lock);
		std::map<const void*, uint32_t>::iterator it = g_wrapperRefs.find(pInterface);
		if (it == g_wrapperRefs.end()) return true;
		if (--it->second != 0) return false;
		g_wrapperRefs.erase(it);
	}
	DemoteIfKept(pInterface);
	return false;
}

vpipc::Handle EngineHandleOf(const void* p, uint32_t kind)
{
	vpipc::Handle h = {0, 0};
	if (!p) return h;
	Lock l(g_lock);
	std::map<const void*, uint32_t>::iterator it = g_engIndex.find(p);
	if (it != g_engIndex.end())
	{
		EngEntry& e = g_eng[it->second - 1];
		if (e.kind != kind && vpk_in_chain(kind, e.kind)) e.kind = kind;  // a more derived view of the same object
		h.type = vpw::MakeType(e.kind, true);
		h.id = (int32_t)it->second;
		return h;
	}
	EngEntry e = {p, kind, true};
	const uint32_t slot = g_engSlots.Alloc();
	if (slot < g_eng.size())
		g_eng[slot] = e;
	else
		g_eng.push_back(e);
	const uint32_t id = slot + 1;
	g_engIndex[p] = id;
	h.type = vpw::MakeType(kind, true);
	h.id = (int32_t)id;
	return h;
}

void* EngineResolve(vpipc::Handle h, uint32_t kind)
{
	if (h.type == 0 && h.id == 0) return NULL;
	const void* p = NULL;
	bool bad = true;
	uint32_t have = 0;
	{
		Lock l(g_lock);
		if (vpw::IsEngine(h.type) && h.id > 0 && (uint32_t)h.id <= g_eng.size())
		{
			const EngEntry& e = g_eng[h.id - 1];
			have = e.kind;
			if (e.live && vpk_in_chain(e.kind, kind))
			{
				p = e.p;
				bad = false;
			}
		}
		if (bad)
		{
			++g_ctrOther.in.unknownHandle;
			if (g_badHandleLogs >= 20) return NULL;
			++g_badHandleLogs;
		}
	}
	if (bad)
		LogErr("EngineResolve({0x%08X, %d}, kind %u %s): unknown, dead or wrong kind (have %u)", h.type, h.id, kind,
		       vpk_iface_name(kind) ? vpk_iface_name(kind) : "?", have);
	return const_cast<void*>(p);
}

void EngineForget(vpipc::Handle h)
{
	Lock l(g_lock);
	if (!vpw::IsEngine(h.type) || h.id <= 0 || (uint32_t)h.id > g_eng.size()) return;
	EngEntry& e = g_eng[h.id - 1];
	if (!e.live) return;
	e.live = false;
	g_engIndex.erase(e.p);
	g_engSlots.Free((uint32_t)h.id - 1);
}

const char* Intern(const char* s)
{
	if (!s) return NULL;
	Lock l(g_lock);
	return g_intern.insert(std::string(s)).first->c_str();
}

const wchar_t* InternW(const wchar_t* s)
{
	if (!s) return NULL;
	Lock l(g_lock);
	return g_internW.insert(std::wstring(s)).first->c_str();
}

const CvString& InternCv(const char* s)
{
	const std::string key(s ? s : "");
	Lock l(g_lock);
	std::map<std::string, CvString*>::iterator it = g_internCv.find(key);
	if (it != g_internCv.end()) return *it->second;
	CvString* v = new CvString(key.c_str());
	g_internCv[key] = v;
	return *v;
}

// ---- streams (vp_wire.h "stream parameters", proxy-design 11.6) ----
static const uint32_t kStreamKnownMax = 256u << 20;  // a larger GetSizeLeft() is taken as "unknown" (issue 2500)

void StreamInArg(vpipc::Writer& w, FDataStream& s)
{
	const uint32_t pos = s.GetPosition();
	uint32_t left = s.GetSizeLeft();
	{
		Lock l(g_lock);
		for (size_t i = 0; i < g_sessions.size(); ++i)
		{
			const Session& se = g_sessions[i];
			if (se.s == &s && se.start + se.consumed == pos && se.id &&
			    (se.unknown ? se.consumed < se.total : se.total - se.consumed == left))
			{
				w.u32(se.id);
				w.u32(se.consumed);
				return;
			}
		}
	}
	// A new session: the rest of the engine stream crosses once (kRt_StreamOpen), read straight into the channel.
	uint32_t id = 0;
	bool unknown = false;
	vpipc::Channel& ch = Chan();
	vpipc::Reader r;
	if (!CallsBlocked(vpw::kRt_StreamOpen, ch, r))
	{
		LARGE_INTEGER t0;
		QueryPerformanceCounter(&t0);
		// Issue 2500 (live, 2026-11-10): the real EXE's save-load streams can report GetSizeLeft() = 0x7FFFFFFF (no
		// known size; the headless host's memory streams always knew theirs). Such a stream is read in 1 MB chunks
		// until a short read, then put back at its position; its session is matched by position, not by size left.
		if (left > kStreamKnownMax)
		{
			// First live try (2026-11-10 15:08) read it in 1 MB chunks until a short read: ReadIt never came up short,
			// the buffer grew to the 512 MB cap and the EXE aborted (CRT "Runtime Error!", bad_alloc). So: a pull session.
			unknown = true;
			uint32_t token;
			{
				Lock l(g_lock);
				g_pullStreams.push_back(&s);
				token = (uint32_t)g_pullStreams.size();
			}
			vpipc::Writer& cw = ch.begin_call(vpw::kRt_StreamOpenPull);
			cw.u32(token);
			if (Finish(ch, vpw::kRt_StreamOpenPull, cw.size(), t0, r)) id = r.u32();
			left = 0xFFFFFFFFu;
			Log("StreamInArg: engine stream %p reports %u bytes left (no size): pull session (token %u)", &s, s.GetSizeLeft(),
			    token);
		}
		else
		{
			vpipc::Writer& cw = ch.begin_call(vpw::kRt_StreamOpen);
			cw.u32(left);
			uint8_t* p = left ? cw.reserve(left) : NULL;
			uint32_t got = 0;
			if (p) got = s.ReadIt(left, p);
			s.SetPosition(pos);
			if (left && (!p || got != left))
			{
				Log("StreamInArg: read %u of %u bytes of the engine stream (rest zero)", got, left);
				if (p) memset(p + got, 0, left - got);
			}
			if (Finish(ch, vpw::kRt_StreamOpen, cw.size(), t0, r)) id = r.u32();
		}
		if (!r.ok()) id = 0;
		Log("stream session %u: engine stream %p at %u, %u bytes%s", id, &s, pos, left, unknown ? " (size unknown)" : "");
	}
	{
		Lock l(g_lock);
		for (size_t i = 0; i < g_sessions.size(); ++i)
			if (g_sessions[i].s == &s)
			{
				g_sessions.erase(g_sessions.begin() + i);
				break;
			}
		if (g_sessions.size() >= 16) g_sessions.erase(g_sessions.begin());
		Session se = {&s, pos, left, 0, id, unknown};
		g_sessions.push_back(se);
	}
	w.u32(id);
	w.u32(0);
}

void StreamInRet(vpipc::Reader& r, FDataStream& s)
{
	const uint32_t consumed = r.u32();
	if (!r.ok()) return;
	uint32_t newPos = 0;
	bool found = false;
	{
		Lock l(g_lock);
		for (size_t i = 0; i < g_sessions.size(); ++i)
		{
			Session& se = g_sessions[i];
			if (se.s != &s) continue;
			se.consumed += consumed;
			if (se.consumed > se.total) se.consumed = se.total;
			newPos = se.start + se.consumed;
			found = true;
			if (se.unknown) return;  // pull session: kRtE_StreamRead read exactly what the DLL consumed (issue 2500)
			break;
		}
	}
	if (found)
		s.SetPosition(newPos);
	else
	{
		Log("StreamInRet: no session for engine stream %p; advancing by %u", &s, consumed);
		s.SetPosition(s.GetPosition() + consumed);
	}
}

// Issue 9036 (bug 0102): the body is written into the engine stream in small pieces, as the in-process DLL writes it
// (item by item), not in one WriteIt of the whole body. Kept for that likeness only: the kept 12 MB buffers 9036 blamed on
// the single WriteIt were the proxy's own RETURN ring (KeepReturn; issue 9041, proxy-design section 18.7).
static const uint32_t kStreamOutPiece = 4096u;

void StreamOutRet(vpipc::Reader& r, FDataStream& s)
{
	uint32_t len = 0;
	const uint8_t* p = r.bytes(&len);
	if (!r.ok() || !p) return;
	for (uint32_t off = 0; off < len; off += kStreamOutPiece)
		s.WriteIt(len - off < kStreamOutPiece ? len - off : kStreamOutPiece, p + off);
}

// ---- serving ----
DispatchScope::DispatchScope()
{
	ThreadState* ts = TS();
	if (!ts) return;
	if (ts->scopes.size() <= ts->scopeDepth) ts->scopes.resize(ts->scopeDepth + 1);
	if (ts->kept.size() <= ts->scopeDepth) ts->kept.resize(ts->scopeDepth + 1);
	ts->scopes[ts->scopeDepth].clear();
	ts->kept[ts->scopeDepth].clear();
	++ts->scopeDepth;
}

DispatchScope::~DispatchScope()
{
	ThreadState* ts = TS();
	if (!ts || ts->scopeDepth == 0) return;
	std::vector<const void*>& v = ts->scopes[ts->scopeDepth - 1];
	for (size_t i = 0; i < v.size(); ++i)
	{
		bool live;
		{
			Lock l(g_lock);
			live = g_wrappers.erase(v[i]) != 0;
		}
		// Proxy_ classes are single inheritance at offset 0 with trivial members: freeing the storage is the
		// whole destruction (the engine did not Destroy it, so the server's temporary registration ends with the
		// server's own dispatch scope).
		if (live) ::operator delete(const_cast<void*>(v[i]));
	}
	v.clear();
	// Issue 2708: a promoted wrapper the engine still holds outlives the call: the scope drops its own reference (the
	// engine's references remain; its last Destroy forwards and releases the promotion's server reference).
	std::vector<const void*>& k = ts->kept[ts->scopeDepth - 1];
	for (size_t i = 0; i < k.size(); ++i)
	{
		bool held;
		{
			Lock l(g_lock);
			std::map<const void*, uint32_t>::iterator it = g_wrapperRefs.find(k[i]);
			held = it != g_wrapperRefs.end();
			if (held && --it->second == 0) g_wrapperRefs.erase(it);
		}
		// Not held: the engine released it on another thread (DemoteIfKept only looks at this thread's scopes). No
		// crossing is allowed here (the RETURN is written): the wrapper is freed and one server reference leaks.
		if (!held)
		{
			LogErr("temporary wrapper %p: promoted, released on another thread before its call returned; one server "
			       "reference leaks (issue 2708)", k[i]);
			bool live;
			{
				Lock l(g_lock);
				live = g_wrappers.erase(k[i]) != 0;
			}
			if (live) ::operator delete(const_cast<void*>(k[i]));
		}
	}
	k.clear();
	--ts->scopeDepth;
}

bool DispatchFail(vpipc::Channel& ch, vpipc::Writer& ret, uint32_t status, const char* message)
{
	// vpw::kStatusBadArguments (3) equals the transport's kRetProtocolError (3), which the caller would see as
	// Status::ProtocolError; send it as kStatusFailed (6) with the reason in the message instead.
	const uint32_t wire = status == vpw::kStatusBadArguments ? (uint32_t)vpw::kStatusFailed : status;
	ch.set_return_status(wire);
	t_dispatchFailed = true;
	ret.str(message ? message : "proxy: dispatch failed");
	static LONG s_logged = 0;
	if (InterlockedIncrement(&s_logged) <= 50)
		LogErr("served call failed: status %u: %s", status, message ? message : "");
	return true;
}

void Unimplemented(uint32_t method, const char* key)
{
	Ctr& c = CtrFor(method).out;
	++c.unimpl;
	// Issue 2502: strict_allow= names methods that are logged and counted but never fatal.
	const char* rule = key ? g_strictAllow.Match(key) : NULL;
	if (rule) InterlockedIncrement(&g_unimplAllowed);
	if (!c.loggedUnimpl)
	{
		c.loggedUnimpl = true;
		if (rule)
			LogErr("UNIMPLEMENTED %s (0x%08X): first call (later calls counted); allow-listed by '%s' in %s: not fatal",
			       key, method, rule, g_cfg.strictAllow.c_str());
		else
			LogErr("UNIMPLEMENTED %s (0x%08X): first call (later calls counted)", key ? key : "?", method);
	}
	if (g_cfg.strict && !rule) Fatal(4, "strict=1 and an unimplemented method was called");
}
}  // namespace ProxyRt

// ================================================================================================= DLL helpers
// The fork's headers (CvString, ...) assert through these with VPRELEASE_ERRORMSG; the proxy logs the assertion.
void CvAssertFailedFormat(const char* expr, const char* file, unsigned int line, bool& ignore)
{
	ignore = true;
	LogErr("assertion failed in a fork header: %s (%s:%u)", expr ? expr : "?", file ? file : "?", line);
}

void CvAssertFailedFormat(const char* expr, const char* file, unsigned int line, bool& ignore, const char* format, ...)
{
	char msg[512];
	va_list ap;
	va_start(ap, format);
	_vsnprintf_s(msg, sizeof(msg), _TRUNCATE, format ? format : "", ap);
	va_end(ap);
	ignore = true;
	LogErr("assertion failed in a fork header: %s (%s:%u): %s", expr ? expr : "?", file ? file : "?", line, msg);
}

// ================================================================================================= exports
// The EXE may call DllGetGameContext often (it is its get-context function); only the first call boots.
// mode=off forwards every call to the original 32-bit DLL's export.
extern "C" ICvGameContext1* DllGetGameContext()
{
	if (g_bootState != 0)
	{
		if (!g_offMode) return g_context;
		// Ticket 0635: mode=off has no Update hook, so the queue guard's drop report (one line per burst) and its check
		// for a hook on top run from the forwarded calls of the boot thread; cheap when nothing happened.
		if (g_bootState == 1 && GetCurrentThreadId() == g_bootThread) ProxyQueueGuard::OnUpdate();
		return g_fallbackGet ? g_fallbackGet() : NULL;
	}
	g_bootThread = GetCurrentThreadId();
	g_bootState = -1;
	ReadConfig();
	ICvGameContext1* ctx = NULL;
	if (!g_cfg.modeOn || g_cfg.failed)  // issue 3523: failed=1 (vp64helper failed wrote it) = the off switch
	{
		// Ticket 0635 (MC #9064): the off switch with config.ini still in the patch's form is a degraded world: no play.
		// Only on masks actually read (the EXE's, config.ini's or the test masks): the off switch is the player's way
		// out, so a session merely assumed D33 (no mask readable) still gets the 32-bit game, with the reason logged.
		std::string offWhy;
		const ProxySession::Masks offMasks = ProxySession::ReadMasks();
		const bool offD33 = ProxySession::SessionKind(offMasks, &offWhy) == ProxySession::kD33;
		if (offD33 && offMasks.src != ProxySession::kMaskNone) OffD33NoPlay(offWhy);
		ctx = BootOff();
		if (offD33) Log("boot: LOUD: %s; no mask could be read, so the off switch is honoured (ticket 0635)", offWhy.c_str());
	}
	else
	{
		// Ticket 0588 (epic 0590 child 4961; D118, D125): the EXE build decides first, before the pack check and the server.
		// An unknown build (or a known one this binary has no table for) never starts the server: a stock config.ini
		// gives the 32-bit DLL (the report and the popup once per build), the A'' form no play (the report, vp64helper
		// failed restores config.ini after the game's end, Fatal 3). proxy_session.h DecideBoot.
		const ProxySession::ExeBuild eb = ProxySession::ReadExeBuild();
		ProxySession::BootGateIn gi;
		gi.pathOn = true;
		gi.gameExe = eb.gameExe;
		gi.build = eb.id;
		gi.supported = eb.supported;
		gi.devReady = eb.devReady;
		gi.stamp = eb.stamp;
		gi.sizeOfImage = eb.sizeOfImage;
		std::string gateSessionWhy;
		std::wstring noticeFile;
		if (eb.gameExe && !(eb.id != kVpExeBuildUnknown && eb.supported))
		{
			// only off the go path: the masks (config.ini's keys on such a build) and the notice record
			const ProxySession::Masks mk = ProxySession::ReadMasks();
			ProxySession::SessionKind(mk, &gateSessionWhy);
			gi.form = mk.src == ProxySession::kMaskNone ? ProxySession::kFormUnknown
			          : ProxySession::Classify(mk.config, ProxySession::RuleBits()) == ProxySession::kD33
			              ? ProxySession::kFormA2
			              : ProxySession::kFormStock;
			std::wstring cfgIni;
			if (ProxySession::ConfigIniPath(&cfgIni)) noticeFile = ProxySession::NoticeFilePath(cfgIni);
			gi.noticeShown = ProxySession::NoticeShown(noticeFile, eb.stamp, eb.sizeOfImage);
		}
		const ProxySession::BootGate gate = ProxySession::DecideBoot(gi);
		std::string fallbackWhy, notice;
		if (gate.action != ProxySession::kBootGo)
		{
			SetFail("exe-build", gate.code.c_str(), gate.why);
			g_failSummary = gate.summary;
			if (eb.test) g_failDetails.push_back("VPPROXY_TEST_EXE_BUILD is set (a test switch): the build values are its");
			if (gate.action == ProxySession::kBootNoPlay)
			{
				if (g_logFile == INVALID_HANDLE_VALUE)
				{
					OpenLog();
					Log("boot: vpproxy (x86 proxy runtime, epics 2300/2500); proxy DLL %s; process %u; boot thread %u; "
					    "mode=on pack=%d; ini %s", ModulePath(g_hModule).c_str(), (unsigned)GetCurrentProcessId(),
					    (unsigned)GetCurrentThreadId(), g_cfg.pack ? 1 : 0, g_cfg.iniPath.c_str());
					if (!g_cfg.problems.empty()) LogRaw(g_cfg.problems.c_str(), false);
				}
				LogErr("boot: NO PLAY: %s; %s", gate.why.c_str(), gateSessionWhy.c_str());
				InterlockedExchange(&g_reported, 1);
				if (ReportFailure(ProxySession::kD33, gateSessionWhy) && gate.remember)
				{
					std::string rl;
					ProxySession::RememberNotice(noticeFile, eb.stamp, eb.sizeOfImage, &rl);
					Log("boot: %s", rl.c_str());
				}
				Fatal(gate.exitCode, "the EXE build is not supported and config.ini is in the A'' form (no play, D125, "
				                     "ticket 0588)");
			}
			ctx = BootOff(gate.why.c_str(), gate.notice.c_str());
			Log("boot: %s", gateSessionWhy.c_str());
			// ticket 0721: a 32-bit DLL that could not be loaded (no g_fallbackGet) is reported by BootNullNoPlay below
			if (g_fallbackGet && gate.report && InterlockedCompareExchange(&g_reported, 1, 0) == 0 &&
			    ReportFailure(ProxySession::kStock, gateSessionWhy) && gate.remember)
			{
				std::string rl;
				const bool ok = ProxySession::RememberNotice(noticeFile, eb.stamp, eb.sizeOfImage, &rl);
				if (ok)
					Log("boot: %s", rl.c_str());
				else
					LogErr("boot: %s", rl.c_str());
			}
		}
		// Issue 3504: pack=1 checks the DLL pack (x64_dir) before the server starts; a pack that does not fit, or a
		// 64-bit boot that fails, boots the 32-bit fallback_dll instead (BootOff), said once.
		else if (g_cfg.pack && ProxyPack::Check(g_cfg.x64Dir, g_cfg.fallbackDll, g_pack) != ProxyPack::kOk)
		{
			fallbackWhy = g_pack.why;
			notice = g_pack.notice;
			SetFail("pack", ProxyPack::StatusName(g_pack.status), g_pack.why);  // issue 3521: the report's failure
		}
		else
		{
			if (g_cfg.pack) g_cfg.x64Dll = g_pack.x64Path;  // issue 2606: server.cfg names the checked manifest's x64 DLL
			g_bootErr.clear();
			g_bootNoFallback = false;
			g_keepBootErr = true;  // pack=1: the fallback's reason; ticket 0721: also BootNullNoPlay's when no SetFail ran
			InterlockedExchange(&g_booting, 1);
			ctx = Boot();
			InterlockedExchange(&g_booting, 0);
			if (eb.gameExe && g_logFile != INVALID_HANDLE_VALUE)  // ticket 0588; the developer switch is LOUD
			{
				if (eb.devReady)
					LogErr("boot: %s", gate.why.c_str());
				else
					Log("boot: %s", gate.why.c_str());
			}
			g_keepBootErr = false;
			ProxyPack::ReleaseX64(g_pack);     // the server has loaded the x64 DLL (or never will)
			ProxyPack::ReleasePurity(g_pack);
			if (!ctx && g_cfg.pack && !g_bootNoFallback)
			{
				std::string e = g_bootErr.empty() ? std::string("(no reason logged)") : g_bootErr;
				const size_t cut = e.find("; DllGetGameContext returns NULL");
				if (cut != std::string::npos) e.erase(cut);
				fallbackWhy = "the 64-bit boot failed: " + e;
				notice = "the 64-bit server did not start";
			}
		}
		if (!fallbackWhy.empty())
		{
			// Issue 3523 (D119, D125): the session rule. A D33 session (config.ini in the A'' form) never plays: no 32-bit
			// fallback (it would draw no terrain, release-driver.md W3b.6), the report, vp64helper failed, the end.
			std::string sessionWhy;
			const ProxySession::Kind kind = ProxySession::SessionKind(ProxySession::ReadMasks(), &sessionWhy);
			if (kind == ProxySession::kD33)
			{
				const bool logWasOpen = g_logFile != INVALID_HANDLE_VALUE;
				if (!logWasOpen)
				{
					OpenLog();
					Log("boot: vpproxy (x86 proxy runtime, epics 2300/2500); proxy DLL %s; process %u; boot thread %u; "
					    "mode=on pack=1; ini %s", ModulePath(g_hModule).c_str(), (unsigned)GetCurrentProcessId(),
					    (unsigned)GetCurrentThreadId(), g_cfg.iniPath.c_str());
					if (!g_cfg.problems.empty()) LogRaw(g_cfg.problems.c_str(), false);
				}
				LogErr("boot: NO PLAY: %s; %s: no 32-bit fallback in a D33 session (D125): the report, vp64helper failed, "
				       "then the end of the game", fallbackWhy.c_str(), sessionWhy.c_str());
				InterlockedExchange(&g_reported, 1);
				ReportFailure(kind, sessionWhy);
				Fatal(3, "the 64-bit path failed in a D33 session (no play, D125)");
			}
			ctx = BootOff(fallbackWhy.c_str(), notice.c_str());
			// Issue 3521 (D119): after the fallback (its lines are then in the report's log tail), the report and the
			// popup helper; the stock session goes on on the 32-bit DLL. Ticket 0721: a 32-bit DLL that could not be
			// loaded leaves no game to go on with: BootNullNoPlay below reports it (stage fallback-dll) and ends the game.
			if (g_fallbackGet && InterlockedCompareExchange(&g_reported, 1, 0) == 0) ReportFailure(kind, sessionWhy);
		}
	}
	// Ticket 0721 (K30): no NULL is handed to the EXE for a failure of the proxy (the EXE reads through it at once): the
	// report route and the end. A NULL from the 32-bit DLL's own export (g_fallbackGet set) is handed over as it is.
	if (!ctx && !g_fallbackGet) BootNullNoPlay();
	if (ctx)
	{
		g_context = ctx;
		g_bootState = 1;
		// Ticket 0745 (D255 G2): whether OnBoot's ProxyKbHook::Install moves the EXE's keyboard hook (proxy_kbhook.cpp);
		// set before the off-mode test (OnBoot directly under it: shell/tests/test_farm_guard.py's order rule), unused in
		// off mode, where nothing installs the hook move
		ProxyKbHook::SetThreadOption(ProxyRtInt::ProxyOptions().KeyboardHookThread(),
		                             ProxyRtInt::ProxyOptions().fromFile[Vp64Opt::kKeyboardHookThread] != 0);
		if (!g_offMode)
		{
			ProxyShell::OnBoot();  // issues 9040, 3305: the composite and the view-mask write (proxy_shell.h)
			// Ticket 0755 (D255): [Extras] FixSmoothWheel, the EXE's PeekMessageW import slot (this is its main thread)
			{
				const Vp64Opt::ExtraState wheel = ProxyRtInt::ProxyOptions().Extra(Vp64Opt::kFixSmoothWheel);
				ProxyWheel::Install(wheel.on, wheel.why == Vp64Opt::kExtraCategoryOff ? "EnableBugfixes = 0" : "FixSmoothWheel = 0");
			}
			// Ticket 0680: the patch runs: [Shell] WindowTitlePrefix in front of the game window's title (proxy_pack.h)
			Log("boot: %s", ProxyPack::StartTitleKeeper(ProxyRtInt::ProxyOptions().WindowTitlePrefix(), 1000, TitleLog).c_str());
		}
	}
	LogFlush();
	return ctx;
}

// Unload (FreeLibrary; the EXE may unload and load the proxy again in one process: every boot starts a fresh server,
// every unload leaves none behind). Runs under the loader lock: nothing waits here and nothing crosses.
//   - after an orderly shutdown (SetDLLIFace(NULL) -> Shutdown(): the server ended, the endpoint closed and its monitor
//     thread joined): the endpoint is released;
//   - without one: the server is terminated and the vpipc monitor thread (proxy code, which would run on in an unmapped
//     image) is terminated, neither waited for; logged loudly;
//   - mode=off: the original 32-bit DLL is freed with the proxy, so a later load starts it fresh as the EXE would.
void DetachOnUnload()
{
	if (g_offMode)
	{
		// Ticket 0635: mode=off writes no end lines (WriteStats), so the queue guard's counters are said here, before
		// ProxyShell::OnUnload gives the slot back (StatusLine compares the slot with the thunk).
		char qg[400];
		ProxyQueueGuard::StatusLine(qg, sizeof(qg));
		Log("detach: mode=off: %s", qg);
	}
	ProxyPack::StopTitleKeeper();  // ticket 0680: the keeper's thread must not outlive the image (logs nothing)
	if (!g_offMode) Log("detach: %s", ProxyPack::TitleKeeperStatus().c_str());
	ProxyShell::OnUnload();  // issue 3305: the view-mask watcher thread must not outlive the image (logs nothing)
	{
		char line[700];  // ticket 4662 (D165 option 3): the engine utility's vptr back to the EXE's vftable
		if (ProxyPacing::OnUnload(line, sizeof(line))) Log("detach: %s", line);
	}
	{
		char line[600];  // ticket 0755: the PeekMessageW import slot back to its previous value (nothing when not installed)
		if (ProxyWheel::OnUnload(line, sizeof(line))) Log("detach: %s", line);
	}
	{
		char line[400];  // ticket 4783 (MC #8582 condition 3): route S's Combat bit back as found
		if (ProxyRouteS::OnUnload(line, sizeof(line))) Log("detach: %s", line);
	}
	{
		// Ticket 0419: the filter's code is in this image and its log handle closes below: give the process its
		// previous filter back before the image goes (the EXE unloads the game DLL at a mod change).
		char line[700];
		if (ProxyCrashFilter::Uninstall(line, sizeof(line)) || line[0]) Log("detach: %s", line);
	}
	if (g_offMode)
	{
		if (g_fallback)
		{
			Log("detach: mode=off: freeing the original 32-bit DLL at %p", g_fallback);
			LogFlush();
			FreeLibrary(g_fallback);
			g_fallback = NULL;
			g_fallbackGet = NULL;
		}
	}
	else if (g_ep)
	{
		if (g_shutdownDone)
		{
			delete g_ep;  // closed and its monitor joined in Shutdown(): no wait here
			g_ep = NULL;
			Log("detach: after the orderly shutdown; endpoint released");
			LogFlush();
		}
		else
		{
			LogErr("detach: the proxy is unloaded without SetDLLIFace(NULL) (no orderly shutdown): terminating the server "
			       "pid %u and the vpipc monitor thread %u without waiting (loader lock)",
			       (unsigned)g_serverPid, (unsigned)g_monitorTid);
			WriteStats();
			LogFlush();
			// No logging below: the monitor thread may be stopped holding the log lock.
			if (g_monitorTid)
			{
				HANDLE h = OpenThread(THREAD_TERMINATE, FALSE, g_monitorTid);
				if (h)
				{
					TerminateThread(h, 0);
					CloseHandle(h);
				}
			}
		}
	}
	if (g_server)
	{
		TerminateProcess(g_server, 3);  // never left behind; an exited process ignores it
		CloseHandle(g_server);
		g_server = NULL;
	}
	if (g_snapEvent)
	{
		CloseHandle(g_snapEvent);
		g_snapEvent = NULL;
	}
	if (g_logFile != INVALID_HANDLE_VALUE)
	{
		CloseHandle(g_logFile);
		g_logFile = INVALID_HANDLE_VALUE;
	}
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
	// Nothing crosses the process boundary here (loader lock).
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		g_hModule = hModule;
		QueryPerformanceFrequency(&g_qpcFreq);
		QueryPerformanceCounter(&g_qpc0);
		InitLocks();
		g_tls = TlsAlloc();
		break;
	case DLL_THREAD_DETACH:
		FreeReturnRing();
		FreeThreadState();
		ProxyLocal::FreeThread();  // issue 0119
		break;
	case DLL_PROCESS_DETACH:
		// Only on FreeLibrary: at process exit other threads were killed and may have held a lock; the server sees
		// the process end and exits by itself (code 3).
		if (reserved == NULL && g_bootState != 0)
		{
			if (g_bootState == 1 && !g_offMode && !g_shutdownDone) WriteStats();
			DetachOnUnload();
			FreeReturnRing();
			FreeThreadState();
			ProxyLocal::FreeThread();  // issue 0119
			if (g_tls != TLS_OUT_OF_INDEXES) TlsFree(g_tls);
			g_tls = TLS_OUT_OF_INDEXES;
		}
		break;
	}
	return TRUE;
}
