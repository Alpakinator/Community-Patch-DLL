# Shaders module tests (task 2902). Synthetic inputs only (HLSL compiled in the test, hand-built containers and PE
# files); game_test reads the EXE through C5R_EXE and reports SKIP without it.
c5r_add_test(c5r_shaders_test testutil.cpp pe_test.cpp dxbc_test.cpp reflect_test.cpp section_test.cpp game_test.cpp main.cpp)
