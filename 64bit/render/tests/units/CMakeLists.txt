# units tests (epic 0041). Globbed so that parallel workers never edit this file: every tests/units/test_*.cpp is one
# test executable named c5r_units_<file name>, registered with ctest (SKIP = 77 counts as failed in build.py --test).
# Shared helpers for these tests go into tests/units/*.h.
file(GLOB C5R_UNITS_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
foreach(t ${C5R_UNITS_TESTS})
    get_filename_component(n ${t} NAME_WE)
    c5r_add_test(c5r_units_${n} ${t})
    target_include_directories(c5r_units_${n} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()
