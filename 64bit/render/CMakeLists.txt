# render/ - the asset pipeline of the server's terrain renderer (epic 2900, package I2-D).
# A standalone CMake project: it does not depend on server/, proxy/ or ipc/ and is not wired into the root
# CMakeLists.txt (the MC does that after epic 2800 merges); since ticket 4960 it includes exebuild/include and the
# generated EXE address header (C5R_EXEBUILD_GEN, below). Build with: python render/build.py [--test]
#
# Layout. One static library c5render, built from modules; each module owns its folder and its own CMakeLists.txt,
# so that parallel workers never edit the same build file:
#   src/<module>/CMakeLists.txt     adds sources to c5render with target_sources(c5render PRIVATE ...)
#   include/c5r/<module>/...        public headers
#   tests/<module>/CMakeLists.txt   test executables, registered with c5r_add_test()
#   tools/<tool>/CMakeLists.txt     command-line tools
# Modules are picked up when their folder has a CMakeLists.txt (configure again after adding one; build.py always
# configures).
cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0091 NEW)
project(c5render LANGUAGES CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "render/ is x64 only")
endif()

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
foreach(kind EXE SHARED MODULE)
    foreach(c DEBUG RELWITHDEBINFO)
        string(REPLACE "/INCREMENTAL:YES" "/INCREMENTAL:NO" CMAKE_${kind}_LINKER_FLAGS_${c} "${CMAKE_${kind}_LINKER_FLAGS_${c}}")
    endforeach()
endforeach()

# Own code: warnings as errors. clang-cl (CMAKE_CXX_COMPILER_ID "Clang") is the normal toolchain; MSVC cl ("MSVC") builds
# only the AddressSanitizer variant (build.py --asan, cmake/msvc-asan-x64.cmake) and takes the same /W4 /WX with C4100
# (unreferenced parameter) off, cl's name for -Wno-unused-parameter.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(C5R_WARN /W4 /WX /wd4100)
else()
    set(C5R_WARN /W4 /WX -Wno-unused-parameter)
endif()
# AddressSanitizer variant (issue 3209): every target, library and tests alike, instrumented, with full debug info for
# symbolised reports. MSVC only: LLVM 19's clang-cl ASan runtime crashes at start-up on this machine.
option(C5R_ASAN "build with MSVC /fsanitize=address (test variant, set by cmake/msvc-asan-x64.cmake)" OFF)
if(C5R_ASAN)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        message(FATAL_ERROR "C5R_ASAN needs MSVC cl (cmake/msvc-asan-x64.cmake): the clang-cl ASan runtime does not start on this machine")
    endif()
    add_compile_options(/fsanitize=address /Zi /Oy-)
    add_link_options(/DEBUG /INCREMENTAL:NO)
endif()
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)

add_library(c5render STATIC src/common/status.cpp)
target_include_directories(c5render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Ticket 4960 (epic 0590, D202): the EXE build table. exebuild/include (vp_exe_build.h, vp_exe_symbols.h,
# vp_exe_patterns.h) and the folder of the generated vp_exe_tables.gen.h, which render/build.py writes before it
# configures (tools/exebuild/emit_header.py, from the local per-build tables of the setting exe_tables / C5_EXE_TABLES).
# The one dependency of render/ on the rest of the repository.
set(C5R_EXEBUILD_GEN "" CACHE PATH "folder holding vp_exe_tables.gen.h (render/build.py sets it)")
if(NOT C5R_EXEBUILD_GEN OR NOT EXISTS "${C5R_EXEBUILD_GEN}/vp_exe_tables.gen.h")
    message(FATAL_ERROR "C5R_EXEBUILD_GEN does not name a folder with vp_exe_tables.gen.h: build with render/build.py, "
                        "which writes it from the setting exe_tables (C5_EXE_TABLES; tools/exebuild/emit_header.py)")
endif()
target_include_directories(c5render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../exebuild/include ${C5R_EXEBUILD_GEN})
target_compile_options(c5render PRIVATE ${C5R_WARN})
# Windows SDK only (official): D3D11, DXGI, the D3D compiler (reflection and disassembly), WIC (image files).
target_link_libraries(c5render PUBLIC d3d11 dxgi dxguid d3dcompiler windowscodecs ole32)

enable_testing()
add_library(c5r_testkit STATIC tests/common/testkit.cpp)
target_include_directories(c5r_testkit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tests/common)
target_link_libraries(c5r_testkit PUBLIC c5render)
target_compile_options(c5r_testkit PRIVATE ${C5R_WARN})
# Test scratch default: this build tree's own folder (bug 0091 item 1; build.py keys the build tree by checkout).
target_compile_definitions(c5r_testkit PRIVATE "C5R_DEFAULT_WORK=\"${CMAKE_BINARY_DIR}/test-out\"")

# c5r_add_test(name src...): one test executable, registered with ctest. Tests that need the game files read
# C5R_GAME_DIR and C5R_EXE from the environment (build.py passes them) and report SKIP when they are absent.
function(c5r_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE c5r_testkit)
    target_compile_options(${name} PRIVATE ${C5R_WARN})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77)
endfunction()

function(c5r_add_tool name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE c5render)
    target_compile_options(${name} PRIVATE ${C5R_WARN})
endfunction()

file(GLOB _mods LIST_DIRECTORIES true RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/*)
foreach(m ${_mods})
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/${m}/CMakeLists.txt)
        add_subdirectory(src/${m})
    endif()
endforeach()
foreach(kind tests tools)
    file(GLOB _subs LIST_DIRECTORIES true RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/${kind} ${CMAKE_CURRENT_SOURCE_DIR}/${kind}/*)
    foreach(s ${_subs})
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${kind}/${s}/CMakeLists.txt)
            add_subdirectory(${kind}/${s})
        endif()
    endforeach()
endforeach()
