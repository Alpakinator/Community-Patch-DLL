// The 22 C exports the DLL's FireWorks code resolves from the host EXE with GetModuleHandle(NULL) +
// GetProcAddress (research/03-dll-engine-boundary/interfaces-and-traffic.md section 1; names as
// host\civ5headless.def of the civ5-headless checkout, the setting c5h of tools/release/settings.py). On x64 the closed FireWorks lib is replaced by
// the stubs of issue 1006, which trampoline over the same names.
//
// Implemented minimally (DllMain and logging need them): GetLogFileMgr / PeekLogFileMgr (a log manager that
// writes <workdir>\Logs\*), GetFileSystemMgr (a logging FIFileSystem whose every method returns 0 / NULL /
// false), FStringA/W_GetStringInitData (the shared, all-zero empty-string header), GetTempHeap (a real 8 MB
// bump block, layout below), GetSmallObjectAllocator (non-NULL dummy so binding completes; FSBAAllocate
// always NULL = "use malloc", FSBADeallocate always false = "use free"; the headless host's scheme).
// Everything else: logging stub returning 0.
// This file includes no Firaxis header on purpose: FString.h and FTempHeap.h declare some of these names as
// C++ functions (FStringA_GetStringInitData, GetTempHeap), which would clash with the extern "C" exports.
#include "srv_tempheap.h"
#include <string.h>

void HostExportHit(int iExport);
void* HostGetFileSystemPtr();
void* HostGetLogFileMgrPtr();

namespace
{
	const char* s_aszExportNames[] =
	{
		"FJobSetAddJobImpl", "FJobSetInitImpl", "FJobSetUninitImpl", "FJobSetWaitImpl",
		"FSBAAllocate", "FSBADeallocate", "FSBADisable", "FSBAGetMaxAllocatorSize",
		"FSBAHasAllocatorForSize", "FSBAIsDisabled", "FSBAIsSmallBlock", "FSBAPreventCreation",
		"FStringA_GetStringInitData", "FStringW_GetStringInitData", "GetFileSystemMgr",
		"GetLogFileMgr", "GetMemTrackerForDLL", "GetSmallObjectAllocator",
		"GetSmallObjectAllocatorInitParams", "GetTempHeap", "PeekLogFileMgr",
		// x86: ??4_Init_locks@std@@QAEAAV01@ABV01@@Z. Exported here under its x64 decoration (link /EXPORT).
		"??4_Init_locks@std@@QEAAAEAV01@AEBV01@@Z",
	};
	enum { NUM_EXPORTS = sizeof(s_aszExportNames) / sizeof(s_aszExportNames[0]) };
	static_assert(NUM_EXPORTS == 22, "22 exports");

	volatile long s_alCalls[NUM_EXPORTS];

	const UINT kTempHeapBytes = 8u * 1024u * 1024u;
	HostTempHeap s_tempHeap;

	// FStringAData / FStringWData: int alloc length, int data length, USHORT locked, USHORT fixed, then text.
	// All zero = the shared empty string. Generous size and alignment.
	__declspec(align(16)) unsigned char s_abStringAInit[64];
	__declspec(align(16)) unsigned char s_abStringWInit[64];

	struct { char ach[256]; } s_smallObjectAllocator;
	struct { char ach[64]; } s_smallObjectAllocatorInitParams;
	volatile LONG s_lSBAPreventCreation = 0;

	inline void Hit(int i)
	{
		InterlockedIncrement(&s_alCalls[i]);
		HostExportHit(i);
	}
}

int HostExportCount() { return NUM_EXPORTS; }
const char* HostExportName(int i) { return s_aszExportNames[i]; }
long HostExportCalls(int i) { return s_alCalls[i]; }

void HostExportsInit()
{
	// Written from FTempHeap.h's member list (srv_tempheap.h), issue 2007: an unlocked, empty heap with no flags,
	// whose block is one committed, page-aligned (so 16-byte aligned) region the process keeps until exit.
	s_tempHeap = HostTempHeap();
	if(void* pBlock = VirtualAlloc(NULL, kTempHeapBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))
	{
		s_tempHeap.pMemHead = static_cast<BYTE*>(pBlock);
		s_tempHeap.uiCapacity = kTempHeapBytes;
	}
}

extern "C"
{
	__declspec(dllexport) int __cdecl FJobSetAddJobImpl(void*, void*, void*) { Hit(0); return 0; }
	__declspec(dllexport) void __cdecl FJobSetInitImpl(void** pp) { Hit(1); if(pp) *pp = NULL; }
	__declspec(dllexport) void __cdecl FJobSetUninitImpl(void**) { Hit(2); }
	__declspec(dllexport) void __cdecl FJobSetWaitImpl(void*) { Hit(3); }

	__declspec(dllexport) void* __cdecl FSBAAllocate(size_t) { Hit(4); return NULL; }
	__declspec(dllexport) bool __cdecl FSBADeallocate(void*) { Hit(5); return false; }
	__declspec(dllexport) void __cdecl FSBADisable(bool) { Hit(6); }
	__declspec(dllexport) size_t __cdecl FSBAGetMaxAllocatorSize() { Hit(7); return 0; }
	__declspec(dllexport) bool __cdecl FSBAHasAllocatorForSize(size_t) { Hit(8); return false; }
	__declspec(dllexport) bool __cdecl FSBAIsDisabled() { Hit(9); return true; }
	__declspec(dllexport) bool __cdecl FSBAIsSmallBlock(void*, size_t&) { Hit(10); return false; }
	__declspec(dllexport) bool __cdecl FSBAPreventCreation(bool b)
	{
		Hit(11);
		return InterlockedExchange(&s_lSBAPreventCreation, b ? 1 : 0) != 0;
	}

	__declspec(dllexport) void* __cdecl FStringA_GetStringInitData() { Hit(12); return s_abStringAInit; }
	__declspec(dllexport) void* __cdecl FStringW_GetStringInitData() { Hit(13); return s_abStringWInit; }
	__declspec(dllexport) void* __cdecl GetFileSystemMgr() { Hit(14); return HostGetFileSystemPtr(); }
	__declspec(dllexport) void* __cdecl GetLogFileMgr() { Hit(15); return HostGetLogFileMgrPtr(); }
	__declspec(dllexport) void* __cdecl GetMemTrackerForDLL() { Hit(16); return NULL; }
	__declspec(dllexport) void* __cdecl GetSmallObjectAllocator(bool) { Hit(17); return &s_smallObjectAllocator; }
	__declspec(dllexport) void* __cdecl GetSmallObjectAllocatorInitParams() { Hit(18); return &s_smallObjectAllocatorInitParams; }
	__declspec(dllexport) void* __cdecl GetTempHeap() { Hit(19); return &s_tempHeap; }
	__declspec(dllexport) void* __cdecl PeekLogFileMgr() { Hit(20); return HostGetLogFileMgrPtr(); }

	// std::_Init_locks::operator=(const _Init_locks&): this in rcx, returns *this.
	void* __cdecl HostInitLocksAssign(void* pThis, const void*) { Hit(21); return pThis; }
}
