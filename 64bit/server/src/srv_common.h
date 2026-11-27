// Common includes and declarations of the x64 server (epic 1500). Derived from the smoke host
// (tools/x64-dll/smoke-host/src/host_common.h, issue 1007): the DLL's own interface headers from the DLL fork, with
// the macros the DLL's precompiled header defines before them, so every interface that crosses the boundary has the
// layout the DLL was compiled against. Same toolchain and /MD as the DLL (tools/x64-dll/build.py).
// Contract: research/sources/civ5-headless/engine-contract.md (documents only; no civ5-headless code is used).
#pragma once

#include <assert.h>
#include <share.h>
#include <intrin.h>

// --- the part of CvGameCoreDLLPCH.h that precedes its includes (clang branch) ---
#define ASSERT_DIALOGUE(msg) ((void)0)
#define CLOSED_ENUM __attribute__((enum_extensibility(closed)))
#define OPEN_ENUM __attribute__((enum_extensibility(open)))
#define FLAG_ENUM __attribute__((flag_enum))
#define BUILTIN_UNREACHABLE() __builtin_unreachable()
#define BUILTIN_TRAP() __builtin_trap()
#define BUILTIN_ASSUME(expr) __builtin_assume(expr)
#define ENUM_META_VALUE [[maybe_unused]]
#define NODISCARD
#define UNUSED_VARIABLE(x) ((void)(x))
#define DEBUG_VARIABLE(x) UNUSED_VARIABLE(x)

#include "CvGameCoreDLLUtil_Win32Headers.h"
#include <MMSystem.h>
#include <algorithm>
#include <vector>
#include <list>
#include <math.h>
#include <tchar.h>
#include <map>
#define _SILENCE_STDEXT_HASH_DEPRECATION_WARNINGS
#include <hash_map>
#include <limits>
#include <unordered_set>
#include <string>
#include <type_traits>

#define DllExport __declspec(dllexport)
typedef unsigned char byte;
typedef unsigned int uint;
typedef wchar_t wchar;

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <FireWorks/FDefNew.h>
#include <FireWorks/FFireTypes.h>
#include <FireWorks/FAssert.h>
#include <FireWorks/FILogFile.h>
#include <FireWorks/FDataStream.h>
#include <FireWorks/FIFile.h>
#include "CvGameDatabase.h"
#include "CvGameCoreDLLUtil.h"
#include "ICvDLLUtility.h"
#include "ICvDLLUserInterface.h"
#include "ICvDLLScriptSystem.h"
#include "ICvDLLDatabaseUtility.h"
#include "CvDllInterfaces.h"

// ---------------------------------------------------------------------------------------------------------
// Issue 4253 (epic 0334, MC #1614 G1): one server.exe serves the 22- and the 43-major DLL. The major/minor split is the
// DLL's compile-time choice (MOD_GLOBAL_MAX_MAJOR_CIVS, CvDLLUtilDefines.h); the server reads it at run time from the
// game database's Defines, which the DLL fills in PerformDatabasePostProcessing. So the split's names are poisoned
// here, after the fork's headers: any use in server code stops the build and points to the run-time value. The totals
// (MAX_PLAYERS, MAX_TEAMS, MAX_CIV_PLAYERS) are the same in both builds; the boot guard checks them against the DLL's.
// The proxy has the same block after its fork include (proxy/src/proxy_rt.h). research/03-dll-engine-boundary/
// player-limits.md section 8.2.
#undef MAX_MAJOR_CIVS
#undef MAX_MINOR_CIVS
#undef OBSERVER_TEAM
#undef MAX_PREGAME_MAJOR_CIVS
#define MAX_MAJOR_CIVS _Pragma("GCC error \"MAX_MAJOR_CIVS is read at run time in the server: use SrvMajorCivs() (issue 4253)\"") 0
#define MAX_MINOR_CIVS _Pragma("GCC error \"MAX_MINOR_CIVS is read at run time in the server: use MAX_CIV_PLAYERS - SrvMajorCivs() (issue 4253)\"") 0
#define OBSERVER_TEAM _Pragma("GCC error \"OBSERVER_TEAM depends on the major count, read at run time in the server: SrvMajorCivs() - 1 (issue 4253)\"") 0
#define MAX_PREGAME_MAJOR_CIVS _Pragma("GCC error \"MAX_PREGAME_MAJOR_CIVS exists only in the 43-major DLL build (issue 4253)\"") 0

// The DLL's major count, read from the game database's Defines after PerformDatabasePostProcessing (srv_main.cpp).
// Headless mode: valid from the database phase on (0 before). Remote mode: valid after the first post-processing.
int SrvMajorCivs();
// Issue 4253: reads Defines.MAX_MAJOR_CIVS (2..62) and checks the DLL's MAX_PLAYERS, MAX_TEAMS, MAX_CIV_PLAYERS against
// the values this server is built with, and MAX_MAJOR_CIVS + MAX_MINOR_CIVS = MAX_CIV_PLAYERS. False with a message that
// names the value when a row is missing or wrong; the caller fails loudly (headless: FAILED, exit 2; remote: the error
// report and exit 1, D118). True: SrvMajorCivs() returns the value read.
bool SrvCheckPlayerLimits(const Database::Connection& db, std::string& why);

// ---------------------------------------------------------------------------------------------------------
// Interface call accounting (generated overrides call HostIfaceHit)

enum HostIface
{
	HI_ENGINE,		// ICvEngineUtility4 (gDLL)
	HI_UI,			// ICvUserInterface2 (DLLUI)
	HI_SCRIPT,		// ICvEngineScriptSystem1
	HI_FILESYS,		// FIFileSystem (GetFileSystemMgr export)
	HI_LOGMGR,		// FILogFileMgr (GetLogFileMgr / PeekLogFileMgr exports)
	HI_LOGFILE,		// FILogFile (returned by the log manager)
	HI_COUNT
};

struct HostMethodInfo
{
	const char* szClass;
	const char* szName;
	const char* szSig;
};

struct HostIfaceInfo
{
	const char* szName;
	const HostMethodInfo* pMethods;
	int nMethods;
	int* piSlots;
};

const HostIfaceInfo& HostGetIface(int iIface);
// Counts a call; generated stubs and hand-written methods both call it. The first call of a method the server does
// not implement (a generated zero/null stub, issue 9007) is logged to server.log with the method's name.
void HostIfaceHit(int iIface, int iMethod);
long HostIfaceCalls(int iIface, int iMethod);
bool HostIfaceImplemented(int iIface, int iMethod);	// false: the generated zero/null stub answered
void HostIfaceMarkImplemented(int iIface, int iMethod);
void HostExportHit(int iExport);
void HostExportsInit();	// srv_exports.cpp: the 22 host exports (TempHeap block)

int HostSlotFromThunk(const void* pThunk);
template<class PMF> int HostSlotOf(PMF pmf)
{
	static_assert(sizeof(PMF) == sizeof(void*), "single-inheritance pointer to member expected");
	const void* p;
	memcpy(&p, &pmf, sizeof(p));
	return HostSlotFromThunk(p);
}

template<class T> struct HostDefault
{
	static T Get() { return T(); }
};
template<> struct HostDefault<void>
{
	static void Get() {}
};
template<class T> struct HostDefault<T&>
{
	static T& Get()
	{
		static typename std::remove_const<T>::type s_value = typename std::remove_const<T>::type();
		return s_value;
	}
};

// ---------------------------------------------------------------------------------------------------------
// Logging and settings (srv_main.cpp)

void SrvLog(const char* szFormat, ...);			// stdout + <run>\server.log
const char* SrvCacheFolder();					// "<run>\cache\" (trailing backslash)
const char* SrvRunDir();
void SrvSetExtraWriteRoot(const char* szDir);	// remote mode: the engine's cache folder (FIFileSystem::Create may write there)
// Ticket 0488 (srv_ifaces.cpp): a write FIFileSystem::Create that was refused or failed. The first reason ("" when none),
// checked by the headless load after Game::Read; remote mode sets a hook that ends the server through its report.
typedef void (*SrvFileFailFn)(const char* szWhy);
const char* SrvFileCreateFailure();
void SrvSetFileCreateFailHook(SrvFileFailFn pfn);
const char* SrvGameDir();						// game data root (read only)
bool SrvKeepDllLogs();							// --dll-logs: write the DLL's own log files
void SrvLogReport(FILE* f);  // issue 2800: the log files the DLL asked for (server-report.txt)
Database::Connection* SrvDb();					// the gameplay database the server opened

// ---------------------------------------------------------------------------------------------------------
// Engine side objects (srv_ifaces.cpp)

ICvEngineUtility4* SrvEngine();
ICvUserInterface2* SrvUI();
ICvEngineScriptSystem1* SrvScriptSystem();
void SrvLogFilesClose();
int SrvResolveSlots();
void SrvSetContext(ICvGameContext1* pContext);	// the DLL's context (net handler, game) for the loopback

// The game-core lock and thread roles (engine-contract.md sections 1.2, 1.3, 2).
void SrvLockInit();
void SrvSetCoreThreadUpdating(bool b);			// true only on the core thread while it is inside CvDllGame::Update

// Net loopback (engine-contract.md 6.4): send* calls with a Response* are queued here and delivered by the driver
// after the current top-level call returns, before the next Update. Every send* the DLL calls that has a Response* is
// looped back (issue 9007; the audit is research/03-dll-engine-boundary/server-host.md section 11); the others are
// generated stubs and show in the report's unserved list.
size_t SrvNetDeliver();
struct SrvNetStats { long lQueued, lDelivered, lUndelivered; };	// undelivered: the DLL's handler lacked the interface
SrvNetStats SrvNetGetStats();
std::string SrvNetSummary();

// Saves (srv_save.cpp, issue 1506)
bool SrvSaveRaw(ICvGameContext1* pContext, const char* szBase, std::string& why);
bool SrvLoadRaw(ICvGameContext1* pContext, ICvDLLDatabaseUtility1* pDbUtil, const char* szPregame, const char* szBody, const char* szLoadFileName,
                std::string& why);
void SrvSetLoadedGame(bool b);	// ICvUserInterface1::IsLoadedGame

// Turn signalling (engine-contract.md 6.3)
void SrvNoteUpdateIndex(long lUpdate);
void SrvSetDone(bool b);

// ---------------------------------------------------------------------------------------------------------
// Virtual file system (srv_vfs.cpp): read only, over the game directory.

bool VfsInit(const char* szGameDir, std::string& strWhy);
// Resolve an include name (with or without .lua, any case) or a relative path (Assets\...) to a full path. A relative
// path is looked up by its file name first (the highest-priority package wins) and taken literally only when the index
// has no such name; an absolute path that exists is taken as it is (bug 0100).
bool VfsResolve(const char* szName, std::string& strFull, std::string& strVfsPath);
const std::vector<std::string>& VfsLuaFileList();	// winners of every .lua name (VFS paths), for IncludeFileList
bool VfsReadFile(const std::string& strFull, std::string& strData);
size_t VfsIndexSize();

// ---------------------------------------------------------------------------------------------------------
// Lua (srv_lua.cpp): one main state in the shared lua51_x64.dll.

bool LuaStartup(const wchar_t* wszLuaDll, char* szWhy, size_t cchWhy);
HMODULE LuaModule();
lua_State* LuaMain();
void LuaSetUtility(ICvScriptSystemUtility1* pUtility);
lua_State* LuaCreateThread(const char* szName);
void LuaFreeThread(lua_State* L);
bool LuaLoadFile(lua_State* L, const char* szFile);
void LuaHideDebug();								// after boot (engine-contract.md 10.5)
void LuaInstallEngineTables(Database::Connection* pDb);	// GameInfo, GameDefines, DB, Locale, MapModData, Modding, Events
bool LuaRunString(const char* szChunk, const char* szName, std::string* pstrErr = NULL);
bool LuaRunFile(const char* szPath, std::string* pstrErr = NULL);
long LuaErrorCount();
int LuaPCall(lua_State* L, int nArgs, int nRet);	// lua_pcall with a stack trace on error
void LuaCountError(const char* szWhere, const char* szText);
void LuaShutdown();

// Localization (srv_loc.cpp, issue 1704): CvLocalization_x64.dll initialised at boot; the Lua Locale table over it.
bool LocStartup(const char* szLocDll, const char* szLocDb, const char* szIcuDat, const char* szLanguage, std::string& why);
bool LocActive();
void LocShutdown();
void LocInstallLua(lua_State* L);					// Locale.* (Lookup, ConvertTextKey, HasTextKey, ...)
// Issue 2802: ATTACH the localization database to the game database as `Localization`, as the EXE does (server.cfg
// loc_attach: engine | 1 | 0). szEnginePath: the file the engine has attached (remote mode), NULL/"" if none.
bool LocAttachGameDb(Database::Connection& db, const char* szEnginePath, std::string& why);

ICvEngineScriptSystemArgs1* LuaCreateArgs();
void LuaDestroyArgs(ICvEngineScriptSystemArgs1* p);
// GameEvents dispatch with the engine's semantics (engine-contract.md 10.1). kind: 0 hook, 1 testall, 2 testany,
// 3 accumulator int, 4 accumulator float.
bool LuaDispatch(int kind, const char* szName, ICvEngineScriptSystemArgs1* pArgs, bool* pb, int* pi, float* pf);

// ---------------------------------------------------------------------------------------------------------
// Remote mode (epic 2300, task 2303): `server.exe --remote --section <name> --config <cfg>`. srv_main.cpp loads the
// config, opens server.log and initialises the host exports, then calls RemoteMain (server/src/remote/).

std::string SrvCfgStr(const char* szKey, const char* szDefault = "");	// a server.cfg value
int SrvCfgInt(const char* szKey, int iDefault);
double SrvNow();												// seconds since the server started
void SrvLogException(EXCEPTION_POINTERS* pEx, const char* szWhere);	// the faulting address and an x64 unwind
int RemoteMain(const char* szSection);							// never returns: the process exits from inside
