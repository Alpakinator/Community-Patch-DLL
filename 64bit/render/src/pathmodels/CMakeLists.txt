# pathmodels: the move-order path's models c5r::pathmodels (ticket 0300; research/05-graphics-composition/path-models.md).
# Globbed so that parallel workers never edit this file: every src/pathmodels/*.cpp goes into c5render. Uses c5r_models
# (0041's model reader), which src/units/CMakeLists.txt builds and links to c5render.
file(GLOB C5R_PATHMODELS_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
if(C5R_PATHMODELS_SRC)
    target_sources(c5render PRIVATE ${C5R_PATHMODELS_SRC})
endif()
