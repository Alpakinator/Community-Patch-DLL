/* tools/x64-dll/pgo/pgo_flush.c (ticket 4801): linked into the instrumented DLL (build.py --config pgo-instr) only.

   The server ends every run with TerminateProcess (server/src/srv_main.cpp), which never runs the profile runtime's
   atexit handler, so an instrumented DLL would leave an empty .profraw (research/08-performance/speedup-p1-pgo.md
   1.2). At load this object replaces the host EXE's import-table entry for TerminateProcess with vp_pgo_terminate,
   which writes the profile once (__llvm_profile_write_file, from clang_rt.profile) when the process ends itself, then
   calls the real TerminateProcess. A normal exit (ExitProcess) still writes through the runtime's own atexit handler;
   the write is never done twice. Nothing here runs in a release, fast or debug DLL.

   Copyright (C) 2026 the c5-64bit-patch authors; GPL-3.0-or-later. */
#include <windows.h>

int __llvm_profile_write_file(void);

typedef BOOL(WINAPI *TerminateProcessFn)(HANDLE, UINT);

static TerminateProcessFn s_real;
static volatile LONG s_written;
volatile LONG vp_pgo_flush_hooked; /* 0 not hooked, 1 hooked, -1 not found; read by tests in a debugger only */

static BOOL WINAPI vp_pgo_terminate(HANDLE h, UINT code)
{
    if ((h == GetCurrentProcess() || GetProcessId(h) == GetCurrentProcessId()) &&
        InterlockedExchange(&s_written, 1) == 0)
        __llvm_profile_write_file();
    return s_real(h, code);
}

/* The IAT slot of TerminateProcess in module `base`, found by name through each import descriptor's name table. */
static ULONG_PTR *find_slot(BYTE *base)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt;
    IMAGE_DATA_DIRECTORY dir;
    IMAGE_IMPORT_DESCRIPTOR *imp;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return NULL;
    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return NULL;
    dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress)
        return NULL;
    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); imp->Name; ++imp)
    {
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk
                                                                                       : imp->FirstThunk));
        ULONG_PTR *slots = (ULONG_PTR *)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots)
        {
            IMAGE_IMPORT_BY_NAME *ibn;
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            ibn = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (lstrcmpA((const char *)ibn->Name, "TerminateProcess") == 0)
                return slots;
        }
    }
    return NULL;
}

static void __cdecl vp_pgo_flush_install(void)
{
    ULONG_PTR *slot = find_slot((BYTE *)GetModuleHandleW(NULL));
    DWORD old;
    if (!slot || !VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old))
    {
        vp_pgo_flush_hooked = -1;
        return;
    }
    s_real = (TerminateProcessFn)*slot;
    *slot = (ULONG_PTR)vp_pgo_terminate;
    VirtualProtect(slot, sizeof(*slot), old, &old);
    vp_pgo_flush_hooked = 1;
}

/* Run by the CRT's DLL start-up with the C++ dynamic initialisers. build.py links this object with
   /INCLUDE:vp_pgo_flush_init so that /OPT:REF keeps it. */
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) void(__cdecl *vp_pgo_flush_init)(void) = vp_pgo_flush_install;
