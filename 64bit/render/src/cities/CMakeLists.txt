# cities: the city art catalogue c5r::cities (ticket 4872, epic 0561; research/05-graphics-composition/city-renderer.md
# 2.1, landmark-renderer.md). Globbed: every src/cities/*.cpp goes into c5render. Uses c5r_models (0041's model
# reader), which src/units/CMakeLists.txt builds and links to c5render.
file(GLOB C5R_CITIES_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
if(C5R_CITIES_SRC)
    target_sources(c5render PRIVATE ${C5R_CITIES_SRC})
endif()
