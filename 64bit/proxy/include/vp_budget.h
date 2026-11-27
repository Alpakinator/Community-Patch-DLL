// vp_budget.h - the turn budget (ticket 4652, epic 0437 W4a): the call classes both sides time by, the switch's name and
// the shared turn block through which server.exe tells the proxy where each turn begins. Shared by the x86 proxy and the
// x64 server: C++03, no STL.
// Definitions: research/08-performance/turn-time-study.md section "The turn budget (W4a)".
#pragma once
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "vp_wire.h"

namespace vpbudget
{
// The switch. Off unless the variable is exactly "1" in the environment of the EXE (the proxy reads it at its boot) and of
// server.exe (which inherits the EXE's environment: the proxy starts it with CreateProcess and no environment block).
static const char* const kEnvName = "VP64_TURN_BUDGET";
// value, n: what GetEnvironmentVariableA wrote and returned (0 when the variable is absent).
inline bool SwitchValueOn(const char* value, unsigned long n) { return n == 1 && value && value[0] == '1'; }
inline bool SwitchFromEnvironment()
{
	char v[16] = "";
	const unsigned long n = GetEnvironmentVariableA(kEnvName, v, sizeof(v));
	return SwitchValueOn(v, n < sizeof(v) ? n : 0);
}

// Call classes. 0 is "no call": a service thread waiting for work (idle). The server's rows use the same numbers:
// row 0 = the EXE alone, row kInUpdate = game logic, the other kIn* rows = calls from the EXE, the kOut* rows = calls
// into the EXE, kOutSave = the save (everything inside AutoSave/SaveGame/QuickSave, nested calls included).
enum Class
{
	kNone = 0,
	// engine -> DLL: made by the EXE (through the proxy), served by the server
	kInUpdate = 1,   // ICvGame1/2::Update: the DLL's own work in it is the row "game logic"
	kInLua = 2,      // UI Lua: kRt_LuaCall / LuaIndex / LuaRelease / LuaRegister / LuaDescribeType, ICvScriptSystemUtility1
	kInPlot = 3,     // ICvPlot1
	kInLock = 4,     // lock sessions: kRt_LockSync, kRt_CoreLockYield, kRt_CoreLockResume
	kInOther = 5,    // every other engine -> DLL method (game, player, unit, city, info getters, pregame, streams, ...)
	// DLL -> engine: made by the server, served by the EXE (through the proxy)
	kOutScript = 6,  // GameEvents and the DLL's Lua in the engine VM: ICvEngineScriptSystem1, Args1, kRtE_Lua*
	kOutUi = 7,      // ICvUserInterface1/2, CvDLLInterfaceIFaceBase
	kOutEngine = 8,  // ICvEngineUtility1-4 except the saves
	kOutFile = 9,    // FIFileSystem, FILogFile, FILogFileMgr
	kOutDb = 10,     // kRtE_DbOps, kRtE_DbFingerprint, kRtE_DbSnapshot (the engine's copy of the game database)
	kOutStream = 11, // kRtE_StreamRead (pull sessions of a load)
	kOutSave = 12,   // ICvEngineUtility*::AutoSave, SaveGame, QuickSave
	kOutOther = 13,  // every other DLL -> engine method (kRtH_*, unknown)
	kClassCount = 14
};

inline const char* ClassName(int c)
{
	static const char* const s[kClassCount] = {"none",   "update", "lua",  "plot",   "lock", "other", "script",
	                                            "ui",     "engine", "file", "db",     "stream", "save", "other"};
	return (c >= 0 && c < kClassCount) ? s[c] : "?";
}
inline bool IsIn(int c) { return c >= kInUpdate && c <= kInOther; }
inline bool IsOut(int c) { return c >= kOutScript && c <= kOutOther; }

// The server's row names (row = class number; row 0 = the EXE alone).
inline const char* RowName(int c)
{
	static const char* const s[kClassCount] = {
	    "exe_alone",          "game_logic",        "from_exe.lua",       "from_exe.plot",   "from_exe.lock",
	    "from_exe.other",     "into_exe.script",   "into_exe.ui",        "into_exe.engine", "into_exe.file",
	    "into_exe.db",        "into_exe.stream",   "save",               "into_exe.other"};
	return (c >= 0 && c < kClassCount) ? s[c] : "?";
}

inline bool StartsWith(const char* s, const char* p) { return s && strncmp(s, p, strlen(p)) == 0; }

// The class of a runtime method (types 0xFFF0 / 0xFFF1 / 0xFFF2); kNone when `method` is not a runtime method.
inline int ClassOfRuntime(uint32_t method)
{
	const uint32_t type = method >> 16;
#define VPB_IS(x) (method == (uint32_t)(vpw::x))
	if (type == vpw::kRtTypeProxyToServer)
	{
		if (VPB_IS(kRt_LuaCall) || VPB_IS(kRt_LuaIndex) || VPB_IS(kRt_LuaRelease) || VPB_IS(kRt_LuaRegister) ||
		    VPB_IS(kRt_LuaDescribeType))
			return kInLua;
		if (VPB_IS(kRt_LockSync) || VPB_IS(kRt_CoreLockYield) || VPB_IS(kRt_CoreLockResume)) return kInLock;
		return kInOther;
	}
	if (type == vpw::kRtTypeServerToProxy)
	{
		if (VPB_IS(kRtE_LuaGetGlobal) || VPB_IS(kRtE_LuaCall) || VPB_IS(kRtE_LuaRelease) || VPB_IS(kRtE_LuaRunChunk))
			return kOutScript;
		if (VPB_IS(kRtE_DbOps) || VPB_IS(kRtE_DbFingerprint) || VPB_IS(kRtE_DbSnapshot)) return kOutDb;
		if (VPB_IS(kRtE_StreamRead)) return kOutStream;
		return kOutOther;
	}
#undef VPB_IS
	if (type == vpw::kRtTypeServerToProxyHuman) return kOutOther;
	return kNone;
}

// The class of a generated interface method: `iface` = vpk_iface_name(method >> 16), `key` = vpk_method_key(method)
// (the model key, "ICvGame1::Update()"), `engineIface` = vpk_is_engine_iface(method >> 16) (DLL -> engine).
inline int ClassOfMethod(const char* iface, const char* key, bool engineIface)
{
	if (engineIface)
	{
		if (StartsWith(iface, "ICvEngineScriptSystem")) return kOutScript;
		if (StartsWith(iface, "ICvUserInterface") || (iface && strcmp(iface, "CvDLLInterfaceIFaceBase") == 0)) return kOutUi;
		if (StartsWith(iface, "ICvEngineUtility"))
		{
			if (key && (strstr(key, "::AutoSave(") || strstr(key, "::SaveGame(") || strstr(key, "::QuickSave(")))
				return kOutSave;
			return kOutEngine;
		}
		if (StartsWith(iface, "FIFileSystem") || StartsWith(iface, "FILogFile")) return kOutFile;
		return kOutOther;
	}
	if (iface && (strcmp(iface, "ICvGame1") == 0 || strcmp(iface, "ICvGame2") == 0) && key && strstr(key, "::Update("))
		return kInUpdate;
	if (iface && strcmp(iface, "ICvPlot1") == 0) return kInPlot;
	if (iface && strcmp(iface, "ICvScriptSystemUtility1") == 0) return kInLua;
	return kInOther;
}

// ---- the shared turn block ------------------------------------------------------------------------------------------
// Section Local\VPBudget_<proxy pid>, created by the proxy at its boot when the switch is on, opened by the server at
// kRt_Boot. Only the server writes it: at each turn boundary (the end of the ICvGame1::Update in which the game turn
// changed) it publishes boundary k = `count`: entry k % kRing gets the boundary's QueryPerformanceCounter value (the
// same clock in both processes) and the turn that begins there; then `count` = k + 1. Per entry a sequence number
// (2k + 1 while written, 2k + 2 when done) lets the reader drop a torn read.
static const char* const kSectionPrefix = "Local\\VPBudget_";
static const uint32_t kMagic = 0x47425056u;  // "VPBG"
static const uint32_t kVersion = 1u;
static const uint32_t kRing = 8u;
struct BoundaryEntry
{
	volatile int64_t tb;    // QPC ticks of the boundary
	volatile int32_t turn;  // the game turn that begins at tb
	volatile int32_t seq;   // 2k + 1 while written, 2k + 2 when entry k is complete
};
struct TurnBlock
{
	uint32_t magic;
	uint32_t version;
	volatile int32_t count;     // boundaries published
	volatile int32_t serverOn;  // 1 once the server (switch on) opened the block
	BoundaryEntry e[kRing];
};
}  // namespace vpbudget
