# initbench (issue 0198): the install-reading part of the server's render start (terrain + water module init), one
# arm per process: before 0188, 0188 (the terrain's own index), 0198 (one shared index). Driver and results in
# render/evidence/0198/.
c5r_add_tool(initbench initbench.cpp)
target_link_libraries(initbench PRIVATE c5r_terrainshot_core)
if(MSVC)
    target_link_options(initbench PRIVATE /ENTRY:wmainCRTStartup)  # wmain (as terrainshot)
endif()
