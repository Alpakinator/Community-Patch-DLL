# bakelayers tests (issue 3401): synthetic CSVs and height fields, every check with a negative test; the checker tool
# run as a product (exit codes and outputs); the install's TerrainDef.csv through gamefs (C5R_GAME_DIR, else SKIP = fail).
c5r_add_test(c5r_bakelayers_test bakelayers_test.cpp)
add_dependencies(c5r_bakelayers_test bakelayers_check)
target_compile_definitions(c5r_bakelayers_test PRIVATE "C5R_BAKELAYERS_CHECK=\"$<TARGET_FILE:bakelayers_check>\"")
