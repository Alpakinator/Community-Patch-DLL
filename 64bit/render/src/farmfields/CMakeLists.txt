# farmfields: the server's own farm fields and farm tracks (epic 0528, ticket 4851; the layout from 0528 C). Globbed so
# that 0528 C adds units here without editing this file: every src/farmfields/*.cpp goes into c5render.
file(GLOB C5R_FARMFIELDS_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
if(C5R_FARMFIELDS_SRC)
    target_sources(c5render PRIVATE ${C5R_FARMFIELDS_SRC})
endif()
