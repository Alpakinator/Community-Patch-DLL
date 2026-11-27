# Terrain-side shader sets tests (task 2903). Synthetic shaders compiled in the test with the SDK's D3DCompile and
# placed in a hand-built PE (the shaders tests' helpers, tests/shaders/testutil.cpp, used read-only); game_test reads
# the EXE through C5R_EXE and reports SKIP without it.
c5r_add_test(c5r_terrainsets_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/testutil.cpp
    terrainsets_test.cpp game_test.cpp lean_test.cpp main.cpp)
target_include_directories(c5r_terrainsets_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../shaders)
