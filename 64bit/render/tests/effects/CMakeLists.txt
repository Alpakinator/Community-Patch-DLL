# effects tests (epic 0042, ticket 4792). Globbed: every tests/effects/test_*.cpp is one test executable named
# c5r_effects_<file name>, registered with ctest (SKIP = 77 counts as failed in build.py --test).
file(GLOB C5R_EFFECTS_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
foreach(t ${C5R_EFFECTS_TESTS})
    get_filename_component(n ${t} NAME_WE)
    c5r_add_test(c5r_effects_${n} ${t})
    target_include_directories(c5r_effects_${n} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()
