// FireWorks members that trampoline into the host executable through
// GetProcAddress(GetModuleHandleA(NULL), "<export>"): real replacements over the same export names.
//
// x86 evidence (FireWorksWin32.obj, disassembly and relocation strings, 2026-11-10):
//   FIFileSystem::GetInstance        -> jmp [GetProcAddress(exe, "GetFileSystemMgr")]
//   FILogFileMgr::GetInstance        -> jmp [GetProcAddress(exe, "GetLogFileMgr")]
//   FStringA_GetStringInitData       -> jmp [GetProcAddress(exe, "FStringA_GetStringInitData")]
//   FStringW_GetStringInitData       -> jmp [GetProcAddress(exe, "FStringW_GetStringInitData")]
//   FireMallocAlignedNoTracking /
//   FireFreeAlignedNoTracking        -> FSBABind(): GetSmallObjectAllocator, FSBAAllocate,
//                                       FSBADeallocate, FSBADisable, FSBAIsDisabled, FSBAIsSmallBlock,
//                                       FSBAHasAllocatorForSize, FSBAGetMaxAllocatorSize,
//                                       FSBAPreventCreation; small-block allocator first, CRT
//                                       _aligned_malloc/_aligned_free as fallback.
// The proc address is looked up once and cached, as on x86. On x86 a missing export makes the
// trampolines jump to address 0; here a missing export is reported through the stub log and the
// process is stopped (the four GetInstance-style functions must return a valid object).
// In the 64-bit design the host executable is the server: it must export these names with x64
// signatures. The headless host's implementations (civ5-headless host/engine/HHExports.cpp) show
// the expected behaviour.
#include "fw_prelude.h"
#include <FireWorks/FString.h>
#include <FireWorks/FIFile.h>
#include <FireWorks/FMemHooks.h>
#include <FireWorks/FSTLBlockAllocator.h>
#include <malloc.h>
#include <stdlib.h>

namespace
{
	FARPROC LookupExeExport(const char* szName)
	{
		return GetProcAddress(GetModuleHandleA(NULL), szName);
	}

	__declspec(noreturn) void StopForMissingExport()
	{
		StubLog_Flush();
		abort();
	}

	typedef FIFileSystem& (__cdecl *GetFileSystemMgrFn)();
	typedef FILogFileMgr& (__cdecl *GetLogFileMgrFn)();
	typedef FStringA::FStringAData* (__cdecl *GetStringAInitDataFn)();
	typedef FStringW::FStringWData* (__cdecl *GetStringWInitDataFn)();

	GetFileSystemMgrFn sg_pfGetFileSystemMgr = NULL;
	GetLogFileMgrFn sg_pfGetLogFileMgr = NULL;
	GetStringAInitDataFn sg_pfFStringA_GetStringInitData = NULL;
	GetStringWInitDataFn sg_pfFStringW_GetStringInitData = NULL;
}

FIFileSystem& FIFileSystem::GetInstance()
{
	if(sg_pfGetFileSystemMgr == NULL)
	{
		sg_pfGetFileSystemMgr = reinterpret_cast<GetFileSystemMgrFn>(LookupExeExport("GetFileSystemMgr"));
		if(sg_pfGetFileSystemMgr == NULL)
		{
			STUB_CALL("FireWorks", "FATAL: host executable does not export GetFileSystemMgr (FIFileSystem::GetInstance)");
			StopForMissingExport();
		}
	}
	return sg_pfGetFileSystemMgr();
}

FILogFileMgr& FILogFileMgr::GetInstance()
{
	if(sg_pfGetLogFileMgr == NULL)
	{
		sg_pfGetLogFileMgr = reinterpret_cast<GetLogFileMgrFn>(LookupExeExport("GetLogFileMgr"));
		if(sg_pfGetLogFileMgr == NULL)
		{
			STUB_CALL("FireWorks", "FATAL: host executable does not export GetLogFileMgr (FILogFileMgr::GetInstance)");
			StopForMissingExport();
		}
	}
	return sg_pfGetLogFileMgr();
}

FStringA::FStringAData* FStringA_GetStringInitData()
{
	if(sg_pfFStringA_GetStringInitData == NULL)
	{
		sg_pfFStringA_GetStringInitData = reinterpret_cast<GetStringAInitDataFn>(LookupExeExport("FStringA_GetStringInitData"));
		if(sg_pfFStringA_GetStringInitData == NULL)
		{
			STUB_CALL("FireWorks", "FATAL: host executable does not export FStringA_GetStringInitData");
			StopForMissingExport();
		}
	}
	return sg_pfFStringA_GetStringInitData();
}

FStringW::FStringWData* FStringW_GetStringInitData()
{
	if(sg_pfFStringW_GetStringInitData == NULL)
	{
		sg_pfFStringW_GetStringInitData = reinterpret_cast<GetStringWInitDataFn>(LookupExeExport("FStringW_GetStringInitData"));
		if(sg_pfFStringW_GetStringInitData == NULL)
		{
			STUB_CALL("FireWorks", "FATAL: host executable does not export FStringW_GetStringInitData");
			StopForMissingExport();
		}
	}
	return sg_pfFStringW_GetStringInitData();
}

// ---------------------------------------------------------------------------------------------
// Small-block allocator binding (FMemHooks.obj, FSBABind).

namespace
{
	enum SBABinding
	{
		SBA_UNBOUND = 0,	// nothing looked up yet
		SBA_PROCS = 1,		// all nine procs found, allocator not created yet (retried on every call)
		SBA_READY = 2,		// GetSmallObjectAllocator(false) returned non-NULL
		SBA_FAILED = 3,		// an export is missing: CRT only, for good
	};

	typedef void* (__cdecl *SBACreateFn)(bool);
	typedef void* (__cdecl *SBAAllocateFn)(size_t);
	typedef bool (__cdecl *SBADeallocateFn)(void*);

	SBABinding sgeSBABound = SBA_UNBOUND;
	SBACreateFn sgpfSBACreate = NULL;
	SBAAllocateFn sgpfSBAAllocate = NULL;
	SBADeallocateFn sgpfSBADeallocate = NULL;
	FARPROC sgpfSBADisable = NULL;
	FARPROC sgpfSBAIsDisabled = NULL;
	FARPROC sgpfSBAIsSmallBlock = NULL;
	FARPROC sgpfSBAHasAllocatorForSize = NULL;
	FARPROC sgpfSBAGetMaxAllocatorSize = NULL;
	FARPROC sgpfSBAPreventCreation = NULL;

	bool FSBABind()
	{
		if(sgeSBABound == SBA_READY)
			return true;
		if(sgeSBABound == SBA_UNBOUND)
		{
			sgpfSBACreate = reinterpret_cast<SBACreateFn>(LookupExeExport("GetSmallObjectAllocator"));
			sgpfSBAAllocate = reinterpret_cast<SBAAllocateFn>(LookupExeExport("FSBAAllocate"));
			sgpfSBADeallocate = reinterpret_cast<SBADeallocateFn>(LookupExeExport("FSBADeallocate"));
			sgpfSBADisable = LookupExeExport("FSBADisable");
			sgpfSBAIsDisabled = LookupExeExport("FSBAIsDisabled");
			sgpfSBAIsSmallBlock = LookupExeExport("FSBAIsSmallBlock");
			sgpfSBAHasAllocatorForSize = LookupExeExport("FSBAHasAllocatorForSize");
			sgpfSBAGetMaxAllocatorSize = LookupExeExport("FSBAGetMaxAllocatorSize");
			sgpfSBAPreventCreation = LookupExeExport("FSBAPreventCreation");
			if(sgpfSBACreate == NULL || sgpfSBAAllocate == NULL || sgpfSBADeallocate == NULL ||
			   sgpfSBADisable == NULL || sgpfSBAIsDisabled == NULL || sgpfSBAIsSmallBlock == NULL ||
			   sgpfSBAHasAllocatorForSize == NULL || sgpfSBAGetMaxAllocatorSize == NULL ||
			   sgpfSBAPreventCreation == NULL)
			{
				sgeSBABound = SBA_FAILED;
				return false;
			}
			sgeSBABound = SBA_PROCS;
		}
		else if(sgeSBABound != SBA_PROCS)
			return false;
		if(sgpfSBACreate(false) == NULL)
			return false;
		sgeSBABound = SBA_READY;
		return true;
	}
}

void* FireMallocAlignedNoTracking(size_t nSize, size_t nAlignment, const char* /*szFile*/, int /*nLine*/)
{
	if(nAlignment != 0 && (nAlignment & (nAlignment - 1)) == 0 && FSBABind())
	{
		void* pBlock = sgpfSBAAllocate((nSize + nAlignment - 1) & ~(nAlignment - 1));
		if(pBlock != NULL)
			return pBlock;
	}
	return _aligned_malloc(nSize, nAlignment);
}

void FireFreeAlignedNoTracking(void* pBlock)
{
	if(FSBABind() && sgpfSBADeallocate(pBlock))
		return;
	_aligned_free(pBlock);
}

// Not referenced by the DLL objects themselves: FSTLBlockAllocator (FSTLBlockAllocator.h) calls
// these from the FCallStack member that FAutoVariableBase has in VPDEBUG builds, whose
// construction and destruction now happen in FAutoArchive.cpp. Same binding, plain malloc/free.
void* FireMallocNoTracking(size_t nSize, const char* /*szFile*/, int /*nLine*/)
{
	if(FSBABind())
	{
		void* pBlock = sgpfSBAAllocate(nSize);
		if(pBlock != NULL)
			return pBlock;
	}
	return malloc(nSize);
}

void FireFreeNoTracking(void* pBlock)
{
	if(FSBABind() && sgpfSBADeallocate(pBlock))
		return;
	free(pBlock);
}
