# Farm-field tests (epic 0528, ticket 4851): every *_test.cpp here is its own test executable c5r_farmfields_<name>.
# No game files: every input is made by the tests (D83). A skip counts as a failure (render/build.py --test).
file(GLOB _farmfields_tests RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)
foreach(t ${_farmfields_tests})
    string(REPLACE "_test.cpp" "" _n ${t})
    c5r_add_test(c5r_farmfields_${_n} ${t})
endforeach()
