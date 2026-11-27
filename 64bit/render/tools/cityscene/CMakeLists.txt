# cityscene (ticket 4873, epic 0561): the offline harness of the BuildingShader draw layer - capture export -> tap frame
# (.mtap) -> resolved scene (.c5models) -> the server's draw -> compare (research/05-graphics-composition/
# landmark-renderer.md 7). The Python driver cityscene.py runs it; every tools/cityscene/*.cpp goes into the one tool.
file(GLOB C5R_CITYSCENE_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
if(C5R_CITYSCENE_SRC)
    c5r_add_tool(cityscene ${C5R_CITYSCENE_SRC})
endif()
