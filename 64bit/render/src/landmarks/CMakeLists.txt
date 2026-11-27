# landmarks: the model library c5r::landmarks (ticket 4872, epic 0561; research/05-graphics-composition/
# city-renderer.md 2, landmark-renderer.md). Globbed: every src/landmarks/*.cpp goes into c5render. Uses c5r_models
# (0041's model reader), which src/units/CMakeLists.txt builds and links to c5render.
file(GLOB C5R_LANDMARKS_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
if(C5R_LANDMARKS_SRC)
    target_sources(c5render PRIVATE ${C5R_LANDMARKS_SRC})
endif()
# The tap frame's layout and the program table are the compositor's (4874): shell/common/shell_model_tap.h and
# shell_model_scene.h, plain C, included as they are (ticket 4873; lead2-0561 #6099).
target_include_directories(c5render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../shell/common)
