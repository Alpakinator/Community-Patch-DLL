# unitanim tests (epic 0374). Globbed so that parallel workers never edit this file: every tests/unitanim/test_*.cpp is
# one test executable named c5r_unitanim_<file name>, registered with ctest (SKIP = 77 counts as failed in
# build.py --test). Shared helpers for these tests go into tests/unitanim/*.h.
file(GLOB C5R_UNITANIM_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
foreach(t ${C5R_UNITANIM_TESTS})
    get_filename_component(n ${t} NAME_WE)
    c5r_add_test(c5r_unitanim_${n} ${t})
    target_include_directories(c5r_unitanim_${n} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()
# 0441: the controller's frame and the drawn figures' pose rows on a recorded timeline (a tool, not a ctest: it needs
# the install, the database and a timeline file; bench_frame.cpp's header)
c5r_add_tool(c5r_unitanim_bench_frame ${CMAKE_CURRENT_SOURCE_DIR}/bench_frame.cpp)
# 4775: one recorded combat through the choreography core on the install's art, written as a .c5anim (a tool, not a
# ctest: it needs the install and a database; combat_scene.cpp's header)
c5r_add_tool(c5r_unitanim_combat_scene ${CMAKE_CURRENT_SOURCE_DIR}/combat_scene.cpp)
