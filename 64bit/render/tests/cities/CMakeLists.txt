# cities tests (ticket 4872, epic 0561). Globbed: every tests/cities/test_*.cpp is one test executable named
# c5r_cities_<file name>, registered with ctest (SKIP = 77 counts as failed in build.py --test).
file(GLOB C5R_CITIES_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
foreach(t ${C5R_CITIES_TESTS})
    get_filename_component(n ${t} NAME_WE)
    c5r_add_test(c5r_cities_${n} ${t})
    target_include_directories(c5r_cities_${n} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../landmarks)
endforeach()
