# pathmodels tests (ticket 0300). Globbed so that parallel workers never edit this file: every tests/pathmodels/test_*.cpp
# is one test executable named c5r_pathmodels_<file name>, registered with ctest (SKIP = 77 counts as failed in
# build.py --test). Shared helpers go into tests/pathmodels/*.h.
file(GLOB C5R_PATHMODELS_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
foreach(t ${C5R_PATHMODELS_TESTS})
    get_filename_component(n ${t} NAME_WE)
    c5r_add_test(c5r_pathmodels_${n} ${t})
    target_include_directories(c5r_pathmodels_${n} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()
# The stand-alone picture (not a test): the path drawn at 3801's camera over a stock picture, labelled DIAGNOSTIC
# (render/evidence/0300/README.md).
add_executable(c5r_pathshot ${CMAKE_CURRENT_SOURCE_DIR}/pathshot.cpp)
target_link_libraries(c5r_pathshot PRIVATE c5r_testkit)
target_include_directories(c5r_pathshot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(c5r_pathshot PRIVATE ${C5R_WARN})
