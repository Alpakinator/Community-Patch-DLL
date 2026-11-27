# c5-64bit-patch: top-level build. One source tree, two toolchains (see ipc/build.py):
#   build/x86/<config>  clang-cl -m32, VS 2008 (VC9) headers and libraries, msvcr90  - proxy side, inside the EXE
#   build/x64/<config>  clang-cl -m64, MSVC 14.4x STL, Windows 10 SDK               - server side (decision D15)
# Both C++14 and /MD. Use: python ipc/build.py [--test]
# Later subdirectories (server, proxy) are added below by their epics.
cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0091 NEW)  # CMAKE_MSVC_RUNTIME_LIBRARY
project(c5_64bit_patch LANGUAGES CXX)

# C++14 everywhere: the language level of the VP DLL build and of D15. The library core additionally avoids
# every C++11 library header, because VC9's library has none (ipc/PROTOCOL.md section 11).
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Dynamic CRT by default: the proxy shares the EXE's msvcr90 heap, the server shares the x64 DLL's MSVC 14 heap.
option(C5_STATIC_CRT "Link the static CRT (/MT) instead of /MD" OFF)
if(C5_STATIC_CRT)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# lld-link has no /INCREMENTAL:YES (CMake adds it when clang-cl reports _MSC_VER 1500, i.e. in the VC9 tree).
foreach(kind EXE SHARED MODULE)
    foreach(c DEBUG RELWITHDEBINFO)
        string(REPLACE "/INCREMENTAL:YES" "/INCREMENTAL:NO" CMAKE_${kind}_LINKER_FLAGS_${c} "${CMAKE_${kind}_LINKER_FLAGS_${c}}")
    endforeach()
endforeach()

set(C5_TOOLCHAIN "" CACHE STRING "vc9 (x86 proxy side) or msvc14 (x64 server side); set by ipc/build.py")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(C5_ARCH x64)
else()
    set(C5_ARCH x86)
    # msvcr90 is a side-by-side assembly: executables need its manifest dependency embedded. CMake embeds the
    # linker's manifest with the SDK's mt.exe (found in the VC9 environment; llvm-mt cannot embed).
endif()
message(STATUS "c5-64bit-patch: ${C5_ARCH}, toolchain '${C5_TOOLCHAIN}', compiler ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

enable_testing()
add_subdirectory(ipc)
add_subdirectory(options)  # one-off 9042: the options file loader shared by the proxy and the server, and its tests
