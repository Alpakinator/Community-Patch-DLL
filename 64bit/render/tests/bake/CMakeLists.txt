# c5r::bake pipeline tests (issue 3404): stand-in helpers, every order and input check with a negative test, the baked
# tile at every size class (C5R_GAME_DIR + C5R_EXE, else SKIP = fail), bakecompare run as a product (every exit code), and
# a sample of 3401's reference set (C5R_BAKE_REFSET, default D:/c5-64bit-work/3400/ref/C5H-3401-TINY_0008; missing = fail).
c5r_add_test(c5r_bake_test bake_test.cpp)
add_dependencies(c5r_bake_test bakecompare)
target_compile_definitions(c5r_bake_test PRIVATE "C5R_BAKECOMPARE=\"$<TARGET_FILE:bakecompare>\"")
