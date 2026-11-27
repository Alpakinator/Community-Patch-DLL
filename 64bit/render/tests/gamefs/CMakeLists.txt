# gamefs tests (task 2901). c5r_gamefs_test: parsers and lookup on synthetic data, every check with a negative test.
# c5r_gamefs_game_test: every pack of the install and terrain textures through GameFiles (SKIP without C5R_GAME_DIR).
c5r_add_test(c5r_gamefs_test fpk_test.cpp dds_test.cpp gamefiles_test.cpp)
c5r_add_test(c5r_gamefs_game_test game_test.cpp)
