# quadtest (task 2906): a game texture drawn by a game VS/PS pair on a windowless D3D11 device, into PNG files.
# The core is a small library so that tests/quadtest can drive it.
add_library(c5r_quadtest_core STATIC quadtest_core.cpp)
target_include_directories(c5r_quadtest_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(c5r_quadtest_core PUBLIC c5render)
target_compile_options(c5r_quadtest_core PRIVATE ${C5R_WARN})
c5r_add_tool(quadtest quadtest.cpp)
target_link_libraries(quadtest PRIVATE c5r_quadtest_core)
