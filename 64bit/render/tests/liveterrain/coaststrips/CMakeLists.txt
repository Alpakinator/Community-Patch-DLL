# The coast strips' tests (ticket 4911; live-terrain.md 8.15): synthetic lines, styles and TerrainDef rows, every input
# check with a negative test; one test reads the install's TerrainDef.csv (C5R_GAME_DIR). lt_strips_run: the offline
# runner of render/evidence/0615/4911/.
c5r_add_test(c5r_liveterrain_coaststrips_test coaststrips_test.cpp)
c5r_add_tool(lt_strips_run lt_strips_run.cpp)
# Ticket 4914: a land/water window through the region path of the terrain, water and residency modules (4909's
# region_core.h) against the whole path; needs C5R_EXE.
c5r_add_test(c5r_liveterrain_landwater_region_test landwater_region_test.cpp)
target_link_libraries(c5r_liveterrain_landwater_region_test PRIVATE c5r_terrainshot_core)
