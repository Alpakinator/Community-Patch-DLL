# c5r::bake::BakePost tests (issue 3403). The game tests need C5R_EXE (the EXE's shaders) and C5R_BAKE_REF (expected data
# made by mkref.py from the RenderDoc capture of 3023 run 3, kept on D); a missing input fails the test with its path.
c5r_add_test(c5r_bakepost_test bakepost_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/testutil.cpp)
target_include_directories(c5r_bakepost_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../shaders)
# Required share of BC1 blocks equal to the EXE's own for tile P (terrain-bake-passes.md section 7).
target_compile_definitions(c5r_bakepost_test PRIVATE C5R_BAKEPOST_MIN_EQUAL=1.0 C5R_BAKEPOST_APPROX_P95=4)
