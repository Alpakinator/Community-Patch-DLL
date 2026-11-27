# models/: the .gr2 model reader (epic 3100). Its own CMake project; no dependency on server/, proxy/, ipc/,
# render/ or shell/. Build with: python models/build.py [--test]  (clang-cl -m64, MSVC 14.4x STL, C++14, /MD; D15)
#
# Layout (globbed so that parallel work does not edit this file):
#   src/*.cpp                  -> static library c5models
#   tests/test_*.cpp           -> one test executable each, registered with ctest
#   tools/<name>/*.cpp         -> one executable per tool folder (modelshot, gr2sweep, ...)
#   tools/<name>/libs.txt      -> optional: extra system libraries for that tool, one per line
cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0091 NEW)
project(c5models LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
foreach(kind EXE SHARED MODULE)
    foreach(c DEBUG RELWITHDEBINFO)
        string(REPLACE "/INCREMENTAL:YES" "/INCREMENTAL:NO" CMAKE_${kind}_LINKER_FLAGS_${c} "${CMAKE_${kind}_LINKER_FLAGS_${c}}")
    endforeach()
endforeach()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "models/ is x64 only (the server side)")
endif()

set(C5M_WARN /W4 /WX -Wno-unused-parameter /EHsc -D_CRT_SECURE_NO_WARNINGS -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DUNICODE -D_UNICODE)
# MSVC AddressSanitizer build (python models/build.py --asan; issue 9025): cl.exe, because the clang-cl 19 ASan
# runtime does not start on this Windows build (model-reader-verification.md section 4). The warnings gate is the
# clang-cl build's; cl.exe builds at /W3 without /WX.
option(C5M_ASAN "build everything with cl.exe /fsanitize=address" OFF)
if(C5M_ASAN)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        message(FATAL_ERROR "C5M_ASAN needs cl.exe (python models/build.py --asan)")
    endif()
    set(C5M_WARN /W3 /EHsc -D_CRT_SECURE_NO_WARNINGS -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DUNICODE -D_UNICODE)
    add_compile_options(/fsanitize=address /Zi)
    add_link_options(/DEBUG /INCREMENTAL:NO)
else()
    # Repeatable executables (bug 0093 V8, issue 9025): two clean Release builds differed only in the PE/COFF
    # TimeDateStamp (file offset 0x80); /Brepro makes lld-link write a hash of the output there instead of the time.
    add_link_options(/Brepro)
endif()

file(GLOB C5M_LIB_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(c5models STATIC ${C5M_LIB_SRC})
target_include_directories(c5models PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(c5models PRIVATE ${C5M_WARN})

enable_testing()
file(GLOB C5M_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp)
foreach(t ${C5M_TESTS})
    get_filename_component(n ${t} NAME_WE)
    add_executable(${n} ${t})
    target_include_directories(${n} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(${n} PRIVATE c5models)
    target_compile_options(${n} PRIVATE ${C5M_WARN})
    add_test(NAME ${n} COMMAND ${n})
    # Tests that need game files read them from C5M_SAMPLES (see models/README.md); without it they skip.
    if(DEFINED ENV{C5M_SAMPLES})
        set_tests_properties(${n} PROPERTIES ENVIRONMENT "C5M_SAMPLES=$ENV{C5M_SAMPLES}")
    endif()
    # 77 = some part skipped (game files missing); a hang ends as a failure after 600 s (test_hostile limits each
    # of its child processes to 20 s itself)
    set_tests_properties(${n} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)
endforeach()

file(GLOB C5M_TOOL_DIRS LIST_DIRECTORIES true ${CMAKE_CURRENT_SOURCE_DIR}/tools/*)
foreach(d ${C5M_TOOL_DIRS})
    if(IS_DIRECTORY ${d})
        file(GLOB tsrc CONFIGURE_DEPENDS ${d}/*.cpp)
        if(tsrc)
            get_filename_component(n ${d} NAME)
            add_executable(${n} ${tsrc})
            target_link_libraries(${n} PRIVATE c5models)
            target_compile_options(${n} PRIVATE ${C5M_WARN})
            if(EXISTS ${d}/libs.txt)
                file(STRINGS ${d}/libs.txt tlibs)
                target_link_libraries(${n} PRIVATE ${tlibs})
            endif()
        endif()
    endif()
endforeach()
