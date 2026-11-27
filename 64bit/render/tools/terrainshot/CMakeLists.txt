# terrainshot (epic 3200, issue 3202): the ground-truth height field drawn at a ground-truth camera by c5r::terrain.
# The core is a small library so that tests/terrain can drive it.
add_library(c5r_terrainshot_core STATIC terrainshot_core.cpp)
target_include_directories(c5r_terrainshot_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(c5r_terrainshot_core PUBLIC c5render)
target_compile_options(c5r_terrainshot_core PRIVATE ${C5R_WARN})
c5r_add_tool(terrainshot terrainshot.cpp)
target_link_libraries(terrainshot PRIVATE c5r_terrainshot_core)
if(MSVC)
    target_link_options(terrainshot PRIVATE /ENTRY:wmainCRTStartup)  # wmain (as fpkscan)
endif()
