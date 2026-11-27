// wbhost_exports.cpp - the C exports the FireWorks code of wbdump resolves from its own EXE with
// GetModuleHandle(NULL) + GetProcAddress (names as tools/x64-dll/smoke-host/src/host_exports.cpp; ticket 0343).
// Each is defined as WbX_<name> and exported under <name> by the link (run_ab.py EXPORTS: /EXPORT:<name>=WbX_<name>),
// because the closed FireWorksWin32.obj defines C functions of the same names that trampoline to the EXE's.
// x86: the closed FireWorksWin32.obj resolves them; x64: this repository's FireWorks replacement does.
// GetFileSystemMgr returns the file system of wbhost_fs.cpp (x86 only; NULL on x64, where the reader opens the file
// through Win32). Small-object allocator: bound but always "use malloc/free". TempHeap: an all-zero heap (capacity 0,
// so every Allocate fails over to the heap). No Firaxis header here: FString.h and FTempHeap.h declare some of these
// names as C++ functions.
#include <string.h>

void* WbHostFileSystem();

namespace
{
__declspec(align(16)) unsigned char s_abStringAInit[64];
__declspec(align(16)) unsigned char s_abStringWInit[64];
__declspec(align(16)) unsigned char s_abTempHeap[64];
struct { char ach[256]; } s_smallObjectAllocator;
struct { char ach[64]; } s_smallObjectAllocatorInitParams;
}

extern "C"
{
int __cdecl WbX_FJobSetAddJobImpl(void*, void*, void*) { return 0; }
void __cdecl WbX_FJobSetInitImpl(void** pp) { if(pp) *pp = 0; }
void __cdecl WbX_FJobSetUninitImpl(void**) {}
void __cdecl WbX_FJobSetWaitImpl(void*) {}
void* __cdecl WbX_FSBAAllocate(size_t) { return 0; }
bool __cdecl WbX_FSBADeallocate(void*) { return false; }
void __cdecl WbX_FSBADisable(bool) {}
size_t __cdecl WbX_FSBAGetMaxAllocatorSize() { return 0; }
bool __cdecl WbX_FSBAHasAllocatorForSize(size_t) { return false; }
bool __cdecl WbX_FSBAIsDisabled() { return true; }
bool __cdecl WbX_FSBAIsSmallBlock(void*, size_t&) { return false; }
bool __cdecl WbX_FSBAPreventCreation(bool) { return false; }
void* __cdecl WbX_FStringA_GetStringInitData() { return s_abStringAInit; }
void* __cdecl WbX_FStringW_GetStringInitData() { return s_abStringWInit; }
void* __cdecl WbX_GetFileSystemMgr() { return WbHostFileSystem(); }
void* __cdecl WbX_GetLogFileMgr() { return 0; }
void* __cdecl WbX_GetMemTrackerForDLL() { return 0; }
void* __cdecl WbX_GetSmallObjectAllocator(bool) { return &s_smallObjectAllocator; }
void* __cdecl WbX_GetSmallObjectAllocatorInitParams() { return &s_smallObjectAllocatorInitParams; }
void* __cdecl WbX_GetTempHeap() { return s_abTempHeap; }
void* __cdecl WbX_PeekLogFileMgr() { return 0; }
}
