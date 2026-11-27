# c5r::arc tests (ticket 0199). Every *_test.cpp here is its own executable c5r_arc_<name>.
#   c5r_arc_arc      the settings parser, the mesh, the arrow record, the end points, the constants and states (no game
#                    files); against 0179's recorded arc (C5R_ARC_REF, default F:/c5-64bit-work/0199/ref, made by
#                    mkref_arc.py; game data, never committed); the install's ArrowSettings.xml (C5R_GAME_DIR) and the
#                    EXE's ArrowShader (C5R_EXE) with draws into an offscreen target.
#   c5r_arc_picture  the arc drawn by c5r into stock's own colour and depth before its arc draws (the replay of
#                    frame2846 in C5R_ARC_REF) against stock's picture after them; writes the pictures and the
#                    figures under C5R_WORK/arc (a DIAGNOSTIC picture, D112).
# A missing input fails or skips by name (render/build.py --test fails a run with a skip).
file(GLOB _arc_tests RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)
foreach(t ${_arc_tests})
    string(REPLACE "_test.cpp" "" _n ${t})
    c5r_add_test(c5r_arc_${_n} ${t})
    target_include_directories(c5r_arc_${_n} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()
