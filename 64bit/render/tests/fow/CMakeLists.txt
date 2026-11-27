# c5r::fow tests (epic 0168; lead's file, not edited by the workers). Every *_test.cpp in this folder is its own test
# executable c5r_fow_<name>, so 3952 (mask_test.cpp, ...) and 3955 (cloud_test.cpp, ...) add tests without editing
# this file. Tests that need the EXE or the game files read C5R_EXE / C5R_GAME_DIR and exit 77 (SKIP) without them;
# render/build.py --test fails a run with a skip. Shared helpers: *_util.cpp here are linked into each.
file(GLOB _fow_tests RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)
file(GLOB _fow_utils ${CMAKE_CURRENT_SOURCE_DIR}/*_util.cpp)
foreach(t ${_fow_tests})
    string(REPLACE "_test.cpp" "" _n ${t})
    c5r_add_test(c5r_fow_${_n} ${t} ${_fow_utils} ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/testutil.cpp)
    target_include_directories(c5r_fow_${_n} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../shaders ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()
