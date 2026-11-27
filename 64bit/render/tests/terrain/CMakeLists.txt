# c5r::terrain and terrainshot tests (epic 3200, issue 3202). The draw tests need the EXE (C5R_EXE) for its
# TerrainShader programs; without it they skip, the executable exits 77 and render/build.py --test fails the run.
c5r_add_test(c5r_terrain_test terrain_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/testutil.cpp)
target_include_directories(c5r_terrain_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../shaders)
target_link_libraries(c5r_terrain_test PRIVATE c5r_terrainshot_core)
# issue 3953 (epic 0168): the fog of war's inputs (Options::fow, the TerrainFOW LUTs, the tile styles)
c5r_add_test(c5r_terrain_fow_test fow_test.cpp)
target_link_libraries(c5r_terrain_fow_test PRIVATE c5r_terrainshot_core)
# epic 0465, ticket 4909 (live-terrain.md 8.14): the region path against the whole-field path (terrain, water,
# residency), on synthetic fields (the test) and on recorded fields (the bench, not a ctest test)
c5r_add_test(c5r_terrain_region_test region_test.cpp)
target_link_libraries(c5r_terrain_region_test PRIVATE c5r_terrainshot_core)
add_executable(c5r_terrain_region_bench region_bench.cpp)
target_link_libraries(c5r_terrain_region_bench PRIVATE c5r_terrainshot_core)
target_compile_options(c5r_terrain_region_bench PRIVATE ${C5R_WARN})
