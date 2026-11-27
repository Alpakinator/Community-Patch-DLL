# ipc/ - the vpipc transport library (epic 1200). Owner: epic 1200 coordinator.
# Sources are globbed so workers add files without editing this file.
file(GLOB VPIPC_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/vpipc/*.h)

add_library(vpipc STATIC ${VPIPC_SOURCES})
target_include_directories(vpipc
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# Language level comes from the toolchain: C++11 in the VC9 tree (clang-cl default there), C++14 on x64 (D15).
# Windows 7 API level: the patch ships to players, not only to this machine.
target_compile_definitions(vpipc PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0601 _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_compile_options(vpipc PRIVATE /W4 /WX /utf-8)
endif()
# kernel32 only (linked by default). Nothing the EXE's process lacks.

# Shared warning set for test and tool executables.
set(VPIPC_EXE_OPTIONS $<$<BOOL:${MSVC}>:/W4 /utf-8>)

add_subdirectory(tests)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/testapps/CMakeLists.txt)
    add_subdirectory(testapps)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/bench/CMakeLists.txt)
    add_subdirectory(bench)
endif()
