# c5r::bake blend tests (issue 3402). The synthetic tests need nothing; the reference tests read C5R_BAKE_REF (capture-
# derived data on D, made by tools/), C5R_EXE and C5R_GAME_DIR, and fail with the path when one is missing.
c5r_add_test(c5r_bakeblend_test bakeblend_test.cpp)
target_link_libraries(c5r_bakeblend_test PRIVATE bcrypt)
