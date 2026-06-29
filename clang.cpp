// This file includes functions required to build VP using Clang.

// Intrinsics required by code generated using VS2022 `LINK /cvtcil`.
extern "C" {
    /// Convert a single precision floating point input as `xmm0` to a
    /// int64 with the low 32-bits in `eax` and high 32-bits in `edx`.
    __attribute__((naked)) void _ftol3() {
        // Clang 14 generated SSE3 dependent implementation.
        __asm__(".intel_syntax\n"
            "sub     esp, 12\n"
            "movss   dword ptr[esp], xmm0\n"
            "fld     dword ptr[esp]\n"
            "fisttp  qword ptr[esp]\n"
            "mov     eax, dword ptr[esp]\n"
            "mov     edx, dword ptr[esp + 4]\n"
            "add     esp, 12\n"
            "ret\n"
        );
    }
    /// Convert an int64 with the low 32-bits in `ecx` and the high 32-bits
    /// in `edx` to a double precision floating point output as `xmm0`.
    __attribute__((naked)) void _ltod3() {
        // Clang 14 generated SSE3 dependent implementation.
        __asm__(".intel_syntax\n"
            "sub     esp, 20\n"
            "movd    xmm0, edx\n"
            "movd    xmm1, ecx\n"
            "punpckldq       xmm1, xmm0\n"
            "movq    qword ptr[esp + 8], xmm1\n"
            "fild    qword ptr[esp + 8]\n"
            "fstp    qword ptr[esp]\n"
            "movsd   xmm0, qword ptr[esp]\n"
            "add     esp, 20\n"
            "ret\n"
        );
	}
}

// MSVC 2015+ CRT symbols not present in VC9 (2008) CRT.
// Simple stubs for thread-safe static initialization (magic statics).
extern "C" {
    void __Init_thread_header(int*) {}
    void __Init_thread_abort(int*) {}
    void __Init_thread_footer(void*) {}
    void* __Init_thread_epoch(void*) { return 0; }
}

// MSVC 2015+ std::terminate handler (not in VC9 CRT).
extern "C" void __cdecl __std_terminate() {
    // Should never be reached in normal DLL operation.
    for (;;) {}
}

// C++14 sized deallocation — not provided by VC9 CRT.
// Forward to the unsized versions.
void operator delete(void* ptr, unsigned int) {
    ::operator delete(ptr);
}
void operator delete[](void* ptr, unsigned int) {
    ::operator delete[](ptr);
}
