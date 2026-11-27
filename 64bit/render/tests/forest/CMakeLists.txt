# Forest tests (epic 0431): every *_test.cpp here is its own test executable c5r_forest_<name>; the *_util.cpp helpers
# and the shaders tests' HLSL / PE builders (../shaders/testutil.cpp, read only) are linked into each. A test that needs
# a reference set FAILS when it is missing (never skipped); a missing EXE or install skips, which render/build.py --test
# counts as a failure.
file(GLOB _forest_tests RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)
file(GLOB _forest_utils CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_util.cpp)
foreach(t ${_forest_tests})
    string(REPLACE "_test.cpp" "" _n ${t})
    c5r_add_test(c5r_forest_${_n} ${t} ${_forest_utils} ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/testutil.cpp)
    target_include_directories(c5r_forest_${_n} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../shaders ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()
