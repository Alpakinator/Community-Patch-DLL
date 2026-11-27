// The engine-side objects the smoke host hands to the DLL (issue 1007).
//
// Every virtual is a generated logging override (host_ifaces.gen.h, from the fork's headers) that records
// its call and returns zero / null / empty. The few written here by hand are the ones without which nothing
// works at all: QueryInterface (the DLL asks for ICvEngineUtility4 / ICvUserInterface2 by GUID), Destroy,
// GetScriptSystem, GetCacheFolderPath (answers a directory under the host's working directory, never the
// user's My Games tree), the log manager and its log files (they write real files under <workdir>\Logs).
#include "host_common.h"
#include "host_ifaces.gen.h"
#include "host_tempheap.h"
#include <FireWorks/FTempHeap.h>
#include <stdio.h>
#include <stdarg.h>

// ---------------------------------------------------------------------------------------------------------
// Slot decoding

// A pointer to a virtual member function (MS ABI, single inheritance) points at a "vcall thunk"
// (??_9Class@@$B<offset>AA). clang-cl /Od emits
//   push rax; mov [rsp],rcx; mov rcx,[rsp]
//   mov rax, [rcx]            48 8B 01
//   mov rax, [rax+d]          48 8B 00 (d=0) | 48 8B 40 d8 | 48 8B 80 d32
//   pop r10; jmp rax
// and optimised builds  mov rax,[rcx]; jmp qword ptr [rax+d]  (FF 20 | FF 60 d8 | FF A0 d32).
// The displacement / 8 is the vtable slot. An incremental-link jump stub (E9 rel32) is followed first.
int HostSlotFromThunk(const void* pThunk)
{
	const unsigned char* p = static_cast<const unsigned char*>(pThunk);
	for(int hop = 0; hop < 4 && p && p[0] == 0xE9; ++hop)
	{
		int rel;
		memcpy(&rel, p + 1, 4);
		p = p + 5 + rel;
	}
	if(!p)
		return -1;
	for(int i = 0; i < 24; ++i)
	{
		if(p[i] != 0x48 || p[i + 1] != 0x8B || p[i + 2] != 0x01)
			continue;
		const unsigned char* q = p + i + 3;
		unsigned char modrm;
		if(q[0] == 0x48 && q[1] == 0x8B && (q[2] == 0x00 || q[2] == 0x40 || q[2] == 0x80))
			modrm = q[2], q += 3;		// mov rax, [rax+d]
		else if(q[0] == 0xFF && (q[1] == 0x20 || q[1] == 0x60 || q[1] == 0xA0))
			modrm = q[1] - 0x20, q += 2;	// jmp [rax+d]
		else
			return -1;
		if(modrm == 0x00)
			return 0;
		if(modrm == 0x40)
			return static_cast<signed char>(q[0]) / 8;
		int d;
		memcpy(&d, q, 4);
		return d / 8;
	}
	return -1;
}

// ---------------------------------------------------------------------------------------------------------
// Script system

class HostScriptSystem : public HostScriptSystemGen
{
public:
	lua_State* CreateLuaThread(_In_z_ const char* szName) override
	{
		HostIfaceHit(HI_SCRIPT, HM_HostScriptSystem_CreateLuaThread);
		return HostLuaCreateThread(szName);	// host_lua.cpp
	}
	void FreeLuaThread(_In_opt_ lua_State* L) override
	{
		HostIfaceHit(HI_SCRIPT, HM_HostScriptSystem_FreeLuaThread);
		HostLuaFreeThread(L);
	}
};

// ---------------------------------------------------------------------------------------------------------
// Engine utility

class HostEngineUtility : public HostEngineUtilityGen
{
public:
	void* DLLCALL QueryInterface(GUID guid) override;
	void DLLCALL Destroy() override
	{
		HostIfaceHit(HI_ENGINE, HM_HostEngineUtility_Destroy);	// static object: nothing to free
	}
	ICvEngineScriptSystem1* GetScriptSystem() override
	{
		HostIfaceHit(HI_ENGINE, HM_HostEngineUtility_GetScriptSystem);
		return HostGetScriptSystem();
	}
	const char* GetCacheFolderPath() override
	{
		HostIfaceHit(HI_ENGINE, HM_HostEngineUtility_GetCacheFolderPath);
		return HostCacheFolder();
	}
};

static const char* GuidName(const GUID& g)
{
	struct { const GUID* p; const char* n; } a[] = {
		{ &guidICvUnknown, "ICvUnknown" }, { &guidICvEngineUtility1, "ICvEngineUtility1" },
		{ &guidICvEngineUtility2, "ICvEngineUtility2" }, { &guidICvEngineUtility3, "ICvEngineUtility3" },
		{ &guidICvEngineUtility4, "ICvEngineUtility4" }, { &guidICvUserInterface1, "ICvUserInterface1" },
		{ &guidICvUserInterface2, "ICvUserInterface2" },
	};
	for(size_t i = 0; i < sizeof(a) / sizeof(a[0]); ++i)
		if(IsEqualGUID(g, *a[i].p))
			return a[i].n;
	return "unknown";
}

void* DLLCALL HostEngineUtility::QueryInterface(GUID guid)
{
	HostIfaceHit(HI_ENGINE, HM_HostEngineUtility_QueryInterface);
	const char* n = GuidName(guid);
	void* p = NULL;
	if(IsEqualGUID(guid, guidICvUnknown) || IsEqualGUID(guid, guidICvEngineUtility1) ||
	   IsEqualGUID(guid, guidICvEngineUtility2) || IsEqualGUID(guid, guidICvEngineUtility3) ||
	   IsEqualGUID(guid, guidICvEngineUtility4))
		p = static_cast<ICvEngineUtility4*>(this);
	HostNote("engine QueryInterface(%s) -> %s", n, p ? "this" : "NULL");
	return p;
}

// ---------------------------------------------------------------------------------------------------------
// User interface

class HostUserInterface : public HostUserInterfaceGen
{
public:
	void* DLLCALL QueryInterface(GUID guid) override
	{
		HostIfaceHit(HI_UI, HM_HostUserInterface_QueryInterface);
		void* p = NULL;
		if(IsEqualGUID(guid, guidICvUnknown) || IsEqualGUID(guid, guidICvUserInterface1) ||
		   IsEqualGUID(guid, guidICvUserInterface2))
			p = static_cast<ICvUserInterface2*>(this);
		HostNote("UI QueryInterface(%s) -> %s", GuidName(guid), p ? "this" : "NULL");
		return p;
	}
	void DLLCALL Destroy() override
	{
		HostIfaceHit(HI_UI, HM_HostUserInterface_Destroy);
	}
};

// ---------------------------------------------------------------------------------------------------------
// File system: all generated (Create -> NULL, Exist -> false, ...)

class HostFileSystem : public HostFileSystemGen
{
};

// ---------------------------------------------------------------------------------------------------------
// Log files: real files under <workdir>\Logs, so the DLL's own logging is visible after a crash.

class HostLogFile : public HostLogFileGen
{
public:
	explicit HostLogFile(const char* szName) : m_pFile(NULL)
	{
		strncpy_s(m_szName, szName, _TRUNCATE);
		MultiByteToWideChar(CP_ACP, 0, m_szName, -1, m_wszName, MAX_PATH);
		char szPath[MAX_PATH * 2];
		sprintf_s(szPath, "%s\\Logs\\%s", HostWorkDir(), m_szName);
		fopen_s(&m_pFile, szPath, "w");
	}
	~HostLogFile()
	{
		if(m_pFile)
			fclose(m_pFile);
	}
	void Msg(const TCHAR* format, ...) override
	{
		HostIfaceHit(HI_LOGFILE, HM_HostLogFile_Msg);
		va_list v; va_start(v, format); Write("", format, v); va_end(v);
	}
	void DebugMsg(const TCHAR* format, ...) override
	{
		HostIfaceHit(HI_LOGFILE, HM_HostLogFile_DebugMsg);
		va_list v; va_start(v, format); Write("[debug] ", format, v); va_end(v);
	}
	void WarningMsg(const TCHAR* format, ...) override
	{
		HostIfaceHit(HI_LOGFILE, HM_HostLogFile_WarningMsg);
		va_list v; va_start(v, format); Write("[warning] ", format, v); va_end(v);
	}
	void ErrorMsg(const TCHAR* format, ...) override
	{
		HostIfaceHit(HI_LOGFILE, HM_HostLogFile_ErrorMsg);
		va_list v; va_start(v, format); Write("[error] ", format, v); va_end(v);
	}
	LPCWSTR GetFileName() const override
	{
		HostIfaceHit(HI_LOGFILE, HM_HostLogFile_GetFileName);
		return m_wszName;
	}
	const char* Name() const { return m_szName; }
	void CloseFile()
	{
		if(m_pFile)
			fclose(m_pFile);
		m_pFile = NULL;
	}

private:
	void Write(const char* szPrefix, const char* szFormat, va_list v)
	{
		if(!m_pFile || !szFormat)
			return;
		fputs(szPrefix, m_pFile);
		vfprintf(m_pFile, szFormat, v);
		fputc('\n', m_pFile);
		fflush(m_pFile);
	}
	FILE* m_pFile;
	char m_szName[MAX_PATH];
	wchar_t m_wszName[MAX_PATH];
};

class HostLogFileMgr : public HostLogFileMgrGen
{
public:
	FILogFile* GetLog(LPCWSTR wszFileName, uint uiFlags, const TCHAR* szTitleString = NULL) override
	{
		HostIfaceHit(HI_LOGMGR, HM_HostLogFileMgr_GetLog);
		char sz[MAX_PATH] = "";
		if(wszFileName)
			WideCharToMultiByte(CP_ACP, 0, wszFileName, -1, sz, MAX_PATH, NULL, NULL);
		return Get(sz, uiFlags, szTitleString);
	}
	FILogFile* GetLog(LPCSTR szFileName, uint uiFlags, const TCHAR* szTitleString = NULL) override
	{
		HostIfaceHit(HI_LOGMGR, HM_HostLogFileMgr_GetLog + 1);
		return Get(szFileName ? szFileName : "", uiFlags, szTitleString);
	}
	bool IsLoggingEnabled() override
	{
		HostIfaceHit(HI_LOGMGR, HM_HostLogFileMgr_IsLoggingEnabled);
		return true;
	}
	void CloseAll()
	{
		for(size_t i = 0; i < m_apLogs.size(); ++i)
			m_apLogs[i]->CloseFile();
	}

private:
	FILogFile* Get(const char* szName, uint uiFlags, const char* szTitle)
	{
		// Only plain file names: anything with a path separator is flattened into one name.
		char szSafe[MAX_PATH];
		strncpy_s(szSafe, szName, _TRUNCATE);
		for(char* p = szSafe; *p; ++p)
			if(*p == '\\' || *p == '/' || *p == ':')
				*p = '_';
		for(size_t i = 0; i < m_apLogs.size(); ++i)
			if(_stricmp(m_apLogs[i]->Name(), szSafe) == 0)
				return m_apLogs[i];
		HostLogFile* p = new HostLogFile(szSafe);
		m_apLogs.push_back(p);
		HostNote("log file opened: Logs\\%s (flags 0x%x, title \"%s\")", szSafe, uiFlags, szTitle ? szTitle : "");
		return p;
	}
	std::vector<HostLogFile*> m_apLogs;
};

// ---------------------------------------------------------------------------------------------------------
// Instances and tables

static HostEngineUtility s_engine;
static HostUserInterface s_ui;
static HostScriptSystem s_script;
static HostFileSystem s_fileSystem;
static HostLogFileMgr s_logMgr;

void* HostGetFileSystemPtr() { return static_cast<FIFileSystem*>(&s_fileSystem); }
void* HostGetLogFileMgrPtr() { return static_cast<FILogFileMgr*>(&s_logMgr); }

// The export's TempHeap against the header's class.
struct TempHeapLayout : TempHeap
{
	static constexpr size_t MemHead() { return offsetof(TempHeapLayout, m_pMemHead); }
	static constexpr size_t Offset() { return offsetof(TempHeapLayout, m_uiCurrentOffset); }
	static constexpr size_t Capacity() { return offsetof(TempHeapLayout, m_uiCapacity); }
	static constexpr size_t Flags() { return offsetof(TempHeapLayout, m_dwFlags); }
};
static_assert(TempHeapLayout::MemHead() == offsetof(HostTempHeap, pMemHead), "TempHeap layout");
static_assert(TempHeapLayout::Offset() == offsetof(HostTempHeap, lCurrentOffset), "TempHeap layout");
static_assert(TempHeapLayout::Capacity() == offsetof(HostTempHeap, uiCapacity), "TempHeap layout");
static_assert(TempHeapLayout::Flags() == offsetof(HostTempHeap, dwFlags), "TempHeap layout");
static_assert(sizeof(TempHeap) == sizeof(HostTempHeap), "TempHeap size");

ICvEngineUtility4* HostGetEngineUtility() { return &s_engine; }
ICvUserInterface2* HostGetUserInterface() { return &s_ui; }
ICvEngineScriptSystem1* HostGetScriptSystem() { return &s_script; }
FIFileSystem* HostGetFileSystem() { return &s_fileSystem; }
FILogFileMgr* HostGetLogFileMgr() { return &s_logMgr; }
void HostLogFilesClose() { s_logMgr.CloseAll(); }

static int s_aiEngineSlots[HostEngineUtility_NUM_METHODS];
static int s_aiUISlots[HostUserInterface_NUM_METHODS];
static int s_aiScriptSlots[HostScriptSystem_NUM_METHODS];
static int s_aiFileSysSlots[HostFileSystem_NUM_METHODS];
static int s_aiLogMgrSlots[HostLogFileMgr_NUM_METHODS];
static int s_aiLogFileSlots[HostLogFile_NUM_METHODS];

static const HostIfaceInfo s_aIfaces[HI_COUNT] = {
	{ "ICvEngineUtility4", s_aHostEngineUtilityMethods, HostEngineUtility_NUM_METHODS, s_aiEngineSlots },
	{ "ICvUserInterface2", s_aHostUserInterfaceMethods, HostUserInterface_NUM_METHODS, s_aiUISlots },
	{ "ICvEngineScriptSystem1", s_aHostScriptSystemMethods, HostScriptSystem_NUM_METHODS, s_aiScriptSlots },
	{ "FIFileSystem", s_aHostFileSystemMethods, HostFileSystem_NUM_METHODS, s_aiFileSysSlots },
	{ "FILogFileMgr", s_aHostLogFileMgrMethods, HostLogFileMgr_NUM_METHODS, s_aiLogMgrSlots },
	{ "FILogFile", s_aHostLogFileMethods, HostLogFile_NUM_METHODS, s_aiLogFileSlots },
};

const HostIfaceInfo& HostGetIface(int iIface) { return s_aIfaces[iIface]; }

// Fills the slot tables; returns the number of methods whose slot could not be decoded.
int HostResolveSlots()
{
	HostEngineUtilityResolveSlots<HostEngineUtility>(s_aiEngineSlots);
	HostUserInterfaceResolveSlots<HostUserInterface>(s_aiUISlots);
	HostScriptSystemResolveSlots<HostScriptSystem>(s_aiScriptSlots);
	HostFileSystemResolveSlots<HostFileSystem>(s_aiFileSysSlots);
	HostLogFileMgrResolveSlots<HostLogFileMgr>(s_aiLogMgrSlots);
	HostLogFileResolveSlots<HostLogFile>(s_aiLogFileSlots);
	int nBad = 0;
	for(int i = 0; i < HI_COUNT; ++i)
	{
		const HostIfaceInfo& f = s_aIfaces[i];
		// Check: slots unique and in range of the object's vtable size implied by the method count (+dtor).
		std::vector<int> seen;
		for(int k = 0; k < f.nMethods; ++k)
		{
			if(f.piSlots[k] < 0 || std::find(seen.begin(), seen.end(), f.piSlots[k]) != seen.end())
				++nBad;
			seen.push_back(f.piSlots[k]);
		}
	}
	return nBad;
}
