# Seam G tests (epic 0169): every *_test.cpp here is its own test executable c5r_seamg_<name>; the *_util.cpp helpers and
# the shaders tests' HLSL / PE builders (../shaders/testutil.cpp, read only) are linked into each. Tests that need the EXE,
# the install or task 4301's reference set read C5R_EXE / C5R_GAME_DIR / C5R_ICE_REF; a missing reference set FAILS (it is
# never skipped; ice_util.h says how to point C5R_ICE_REF at F:), a missing EXE or install skips, which render/build.py
# --test counts as a failure.
file(GLOB _seamg_tests RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)
file(GLOB _seamg_utils ${CMAKE_CURRENT_SOURCE_DIR}/*_util.cpp)
foreach(t ${_seamg_tests})
    string(REPLACE "_test.cpp" "" _n ${t})
    c5r_add_test(c5r_seamg_${_n} ${t} ${_seamg_utils} ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/testutil.cpp)
    target_include_directories(c5r_seamg_${_n} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../shaders ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()
