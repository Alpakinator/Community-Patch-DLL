# options/ - the options file of the 64-bit patch (one-off 9042, D89). Built in both trees by `python ipc/build.py`:
# x86 (VC9 headers, msvcr90: the proxy's toolchain) and x64 (MSVC 14.4x: the server's). The proxy and the server compile
# src/vp64_options.cpp themselves (proxy/build_proxy.py, server/build_server.py); this file builds the unit tests.
add_library(vp64options STATIC ${CMAKE_CURRENT_SOURCE_DIR}/src/vp64_options.cpp
                               ${CMAKE_CURRENT_SOURCE_DIR}/include/vp64_options.h)
target_include_directories(vp64options PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(vp64options PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0601 _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_compile_options(vp64options PRIVATE /W4 /WX /utf-8)
endif()

add_executable(test_options ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_options.cpp)
target_link_libraries(test_options PRIVATE vp64options)
if(MSVC)
    target_compile_options(test_options PRIVATE /W4 /WX /utf-8)
endif()
# Arguments: the player documentation (every key must be described there) and a scratch folder inside the build tree.
add_test(NAME test_options
         COMMAND test_options ${CMAKE_SOURCE_DIR}/docs/OPTIONS.md ${CMAKE_CURRENT_BINARY_DIR}/scratch)
set_tests_properties(test_options PROPERTIES TIMEOUT 60)
