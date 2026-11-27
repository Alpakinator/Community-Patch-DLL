# arc: the ranged-attack arc (ticket 0199): every .cpp in this folder is part of c5render.
file(GLOB _arc_src ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
target_sources(c5render PRIVATE ${_arc_src})
