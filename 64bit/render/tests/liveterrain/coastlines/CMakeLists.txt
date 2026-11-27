# The coastline curves and region fill (ticket 4910; live-terrain.md 8.10.1): synthetic island maps and style files,
# every input check with a negative test; game_styles reads the install's TerrainModels.fpk (C5R_GAME_DIR).
# lt_region_run: the offline runner on recorded inputs (render/evidence/0615/4910/).
c5r_add_test(c5r_liveterrain_coastlines_test coastlines_test.cpp)
c5r_add_tool(lt_region_run lt_region_run.cpp)
