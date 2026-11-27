# c5r::bake height pages and bake-time constants (issue 3406). The game tests read C5R_HEIGHT_REF (expected data on D:
# height taps, the captured page, tile P's TerrainFrame, the tiles' fields read by PID; default
# D:/c5-64bit-work/3400/w6/ref); a missing input fails the test with its path.
c5r_add_test(c5r_bakeheight_test bakeheight_test.cpp)
