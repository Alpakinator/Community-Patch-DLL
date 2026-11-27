// The host's TempHeap (FirePlace/include/FireWorks/FTempHeap.h) as compiled for x64. The DLL's FireWorks code
// reads its members directly. Layout checked against the header's class in host_ifaces.cpp:
// HeapLock (one LONG, __declspec(align(16))) +0x00, byte* m_pMemHead +0x10, LONG m_uiCurrentOffset +0x18,
// uint m_uiCapacity +0x1c, DWORD m_dwFlags +0x20, size 0x30. (x86: +0x10/+0x14/+0x18/+0x1c, size 0x20.)
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stddef.h>

struct HostTempHeap
{
	struct alignas(16) { volatile LONG lLock; } lock;
	BYTE* pMemHead;
	volatile LONG lCurrentOffset;
	UINT uiCapacity;
	DWORD dwFlags;
};
static_assert(offsetof(HostTempHeap, pMemHead) == 0x10, "TempHeap layout");
static_assert(offsetof(HostTempHeap, lCurrentOffset) == 0x18, "TempHeap layout");
static_assert(offsetof(HostTempHeap, uiCapacity) == 0x1c, "TempHeap layout");
static_assert(offsetof(HostTempHeap, dwFlags) == 0x20, "TempHeap layout");
