# unitanim: the unit animation controller c5r::unitanim (epic 0374, U5; research/05-graphics-composition/
# unit-controller.md). Globbed so that parallel workers never edit this file: every src/unitanim/*.cpp goes into
# c5render. The models library (c5r_models, with logic.h's readers) is built and linked to c5render by src/units/.
file(GLOB C5R_UNITANIM_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
if(C5R_UNITANIM_SRC)
    target_sources(c5render PRIVATE ${C5R_UNITANIM_SRC})
endif()
