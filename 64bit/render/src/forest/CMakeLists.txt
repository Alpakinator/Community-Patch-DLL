# forest: the trees of route A (epic 0431; research/05-graphics-composition/trees.md 9). Globbed so that parallel workers
# (4550 placement, 4551 draw, 4552 shadows, 4553 cut) never edit this file: every src/forest/*.cpp goes into c5render.
file(GLOB C5R_FOREST_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
if(C5R_FOREST_SRC)
    target_sources(c5render PRIVATE ${C5R_FOREST_SRC})
endif()
