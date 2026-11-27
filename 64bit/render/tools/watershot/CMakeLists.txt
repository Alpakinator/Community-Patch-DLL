# watershot (epic 3600, issue 3604): a stock capture's water draws replayed and compared, the WaterFrame check and the
# module's water at a live camera. The core is a small library so that its own test can drive every step.
add_library(c5r_watershot_core STATIC watershot_core.cpp)
target_include_directories(c5r_watershot_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(c5r_watershot_core PUBLIC c5render c5r_terrainshot_core)
target_compile_options(c5r_watershot_core PRIVATE ${C5R_WARN})
c5r_add_tool(watershot watershot.cpp)
target_link_libraries(watershot PRIVATE c5r_watershot_core)
if(MSVC)
    target_link_options(watershot PRIVATE /ENTRY:wmainCRTStartup)  # wmain (as terrainshot)
endif()
# The test of the export reader, the replay and the comparator: a synthetic export written by the test from the
# module's own draw (the EXE's programs from C5R_EXE; without it the test skips and build.py --test fails the run).
c5r_add_test(c5r_watershot_test watershot_test.cpp)
target_link_libraries(c5r_watershot_test PRIVATE c5r_watershot_core)
