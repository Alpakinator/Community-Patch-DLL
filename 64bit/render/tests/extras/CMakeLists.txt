# Extras tests (epic 0454). Every *_test.cpp here is its own test executable c5r_extras_<name>; no game files: every
# input is made by the tests (D83). A skip counts as a failure (render/build.py --test).
# c5r_roadgen: the generator's command line for the offline candidate pictures and the cost figures (ticket 4933);
# a tool, not a test.
file(GLOB _extras_tests RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)
foreach(t ${_extras_tests})
    string(REPLACE "_test.cpp" "" _n ${t})
    c5r_add_test(c5r_extras_${_n} ${t})
endforeach()
c5r_add_tool(c5r_roadgen roadgen_cli.cpp)
# Ticket 4983 (epic 0455): the unit extras' tests in units/, each its own executable c5r_extras_units_<name>.
file(GLOB _extras_units_tests RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/units/*_test.cpp)
foreach(t ${_extras_units_tests})
    get_filename_component(_n ${t} NAME_WE)
    string(REPLACE "_test" "" _n ${_n})
    c5r_add_test(c5r_extras_units_${_n} ${t})
endforeach()
