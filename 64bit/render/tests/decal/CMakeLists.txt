# c5r::decal::DecalPass tests (issue 4104, epic 0166). The reference tests need C5R_EXE (the EXE's shaders) and the four
# reference tiles made by mkref_decal.py from 4109's set (C5R_DECAL_REF, default F:/c5-64bit-work/0166/4104/ref; game data,
# never committed); the install arm reads C5R_DECAL_GAME_DIR / C5R_DECAL_MODS. A missing input fails the test by name.
c5r_add_test(c5r_decal_test decal_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/testutil.cpp)
target_include_directories(c5r_decal_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../shaders)
