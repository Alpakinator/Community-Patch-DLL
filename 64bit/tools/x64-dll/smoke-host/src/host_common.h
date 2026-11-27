// Common includes and declarations for the x64 smoke host (issue 1007, epic 1000).
//
// The host includes the DLL's own interface headers from the DLL fork (branch x64), with the macros the DLL's
// precompiled header (CvGameCoreDLL_Expansion2/CvGameCoreDLLPCH.h) defines before them, so every interface
// that crosses the boundary has the layout the DLL was compiled against. Same toolchain and /MD as the DLL
// (tools/x64-dll/build.py). Reference for the contract: civ5-headless's host/ folder (32-bit,
// read-only); nothing of its asm is used.
#pragma once

#include <assert.h>
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
// Call recording (host_record.cpp)

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
	const char* szClass;	// declaring interface
	const char* szName;
	const char* szSig;		// declaration as written in the header (whitespace collapsed)
};

struct HostIfaceInfo
{
	const char* szName;				// e.g. "ICvEngineUtility4"
	const HostMethodInfo* pMethods;
	int nMethods;
	int* piSlots;					// vtable slot per method (filled by HostResolveSlots), -1 if unknown
};

const HostIfaceInfo& HostGetIface(int iIface);

// One call of an interface method; the method index is the generated declaration index.
void HostIfaceHit(int iIface, int iMethod);
// One call of a host export (index into the export table of host_exports.cpp).
void HostExportHit(int iExport);
// Current step name (for the report).
void HostSetStep(const char* szStep);
const char* HostGetStep();
// Free-form note into the timeline (e.g. a log file the DLL opened).
void HostNote(const char* szFormat, ...);

// Host log (stdout + <workdir>/smoke-host.log).
void HostLog(const char* szFormat, ...);

// Settings answered to the DLL.
const char* HostCacheFolder();	// "<workdir>\cache\" (trailing backslash)
const char* HostWorkDir();

// Vtable slot of a virtual member function, decoded from the MS-ABI vcall thunk a pointer to it refers to.
int HostSlotFromThunk(const void* pThunk);
template<class PMF> int HostSlotOf(PMF pmf)
{
	static_assert(sizeof(PMF) == sizeof(void*), "single-inheritance pointer to member expected");
	const void* p;
	memcpy(&p, &pmf, sizeof(p));
	return HostSlotFromThunk(p);
}

// Default return value of a logging stub: zero / null / empty, a static zeroed object for references.
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

// Host objects handed to the DLL.
ICvEngineUtility4* HostGetEngineUtility();
ICvUserInterface2* HostGetUserInterface();
ICvEngineScriptSystem1* HostGetScriptSystem();
FIFileSystem* HostGetFileSystem();
FILogFileMgr* HostGetLogFileMgr();
void HostLogFilesClose();

// Lua (host_lua.cpp): the main state in the shared lua51_x64.dll.
bool HostLuaStartup(const wchar_t* wszLuaDll, char* szWhy, size_t cchWhy);
bool HostLuaAvailable();
HMODULE HostLuaModule();
void HostLuaSetUtility(ICvScriptSystemUtility1* pUtility);
lua_State* HostLuaCreateThread(const char* szName);
void HostLuaFreeThread(lua_State* L);
void HostLuaShutdown();

// Export table (host_exports.cpp).
int HostExportCount();
const char* HostExportName(int i);
long HostExportCalls(int i);
void HostExportsInit();
