# unitscene (epic 0041, 4355): the offline oracle run of the unit module over 0301's catalogue
# (research/05-graphics-composition/unit-renderer.md section 13). Every tools/unitscene/*.cpp goes into the one tool.
file(GLOB C5R_UNITSCENE_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
if(C5R_UNITSCENE_SRC)
    c5r_add_tool(unitscene ${C5R_UNITSCENE_SRC})
endif()
