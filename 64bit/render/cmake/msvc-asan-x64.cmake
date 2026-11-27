# Toolchain file for render/'s AddressSanitizer variant (issue 3209): MSVC cl.exe + link.exe, environment from VS 2022
# vcvarsall x64 (the same MSVC 14.4x STL and Windows 10 SDK as the clang-cl build). Used by `python render/build.py
# --asan`, never for a shipped build. Why cl and not clang-cl: LLVM 19.1.7's clang-cl ASan runtime crashes at start-up on
# this machine (Windows 11 26100, "interception_win: unhandled instruction"; research/05-graphics-composition/
# asset-pipeline-verification.md section 4), MSVC's own ASan runtime (clang_rt.asan_dynamic-x86_64.dll from the
# toolset's bin\Hostx64\x64, on PATH after vcvarsall) runs. C5R_ASAN switches on /fsanitize=address in CMakeLists.txt.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_CXX_COMPILER cl.exe)
set(CMAKE_LINKER link.exe CACHE FILEPATH "")
set(CMAKE_AR lib.exe CACHE FILEPATH "")
set(C5R_ASAN ON CACHE BOOL "MSVC AddressSanitizer variant" FORCE)
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
