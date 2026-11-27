# Self-test of the shared pieces (status, binreader, testkit). Owner: team-2900 coordinator; team-3200 W1 since
# 2026-11-11 (bug 0091 items 1 and 2).
add_executable(c5r_skip_probe skip_probe.cpp)
target_link_libraries(c5r_skip_probe PRIVATE c5r_testkit)
target_compile_options(c5r_skip_probe PRIVATE ${C5R_WARN})

# ASan canary (issue 3209): only in the AddressSanitizer variant; not a ctest test (it fails on purpose), run by
# build.py --asan --test, which requires its heap-buffer-overflow report.
if(C5R_ASAN)
    add_executable(c5r_asan_canary asan_canary.cpp)
    target_compile_options(c5r_asan_canary PRIVATE ${C5R_WARN})
endif()

c5r_add_test(c5r_common_test common_test.cpp)
add_dependencies(c5r_common_test c5r_skip_probe)
target_compile_definitions(c5r_common_test PRIVATE "C5R_SKIP_PROBE=\"$<TARGET_FILE:c5r_skip_probe>\"")

# render/build.py's defaults (build folder and test scratch keyed by checkout): a Python unittest, run by ctest.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME c5r_build_py_test COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/build_py_test.py)
    # auditor cycle 3 C4: offline_all.py's exit codes (offline; nothing is started)
    add_test(NAME c5r_offline_all_test COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/offline_all_test.py)
else()
    # no interpreter: register a test that fails with the reason, never a silent omission
    add_test(NAME c5r_build_py_test COMMAND ${CMAKE_COMMAND} -E false)
    add_test(NAME c5r_offline_all_test COMMAND ${CMAKE_COMMAND} -E false)
    message(WARNING "Python3 not found: c5r_build_py_test will fail")
endif()
