# effects: effects timed and moved by the server (epic 0042, route S; ticket 4792 projectiles;
# research/05-graphics-composition/effects-server.md). Globbed so that parallel workers never edit this file: every
# src/effects/*.cpp goes into c5render (4722's emitter keeps its own files beside these).
file(GLOB C5R_EFFECTS_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
if(C5R_EFFECTS_SRC)
    target_sources(c5render PRIVATE ${C5R_EFFECTS_SRC})
endif()
