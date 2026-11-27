# landmarks tests (ticket 4872, epic 0561). Globbed: every tests/landmarks/test_*.cpp is one test executable named
# c5r_landmarks_<file name>, registered with ctest (SKIP = 77 counts as failed in build.py --test). Shared helpers go
# into tests/landmarks/*.h.
file(GLOB C5R_LANDMARKS_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
foreach(t ${C5R_LANDMARKS_TESTS})
    get_filename_component(n ${t} NAME_WE)
    c5r_add_test(c5r_landmarks_${n} ${t})
    target_include_directories(c5r_landmarks_${n} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()
# A look at one model file (not a test): research/05-graphics-composition/landmark-renderer.md names its uses.
add_executable(c5r_gr2_facts ${CMAKE_CURRENT_SOURCE_DIR}/gr2_facts.cpp)
target_link_libraries(c5r_gr2_facts PRIVATE c5render)
target_compile_options(c5r_gr2_facts PRIVATE ${C5R_WARN})
add_executable(c5r_landmark_report ${CMAKE_CURRENT_SOURCE_DIR}/library_report.cpp)
target_link_libraries(c5r_landmark_report PRIVATE c5render)
target_compile_options(c5r_landmark_report PRIVATE ${C5R_WARN})
