# bakegrid tests (issue 3405): synthetic height fields for the normal rule, its wraps and clamps, the half conversion and
# every check with a negative test; the reference tests read C5R_GRID_REF (default D:/c5-64bit-work/3400/w5/ref, game
# data made by tools/grid_ref.py from RenderDoc exports) and fail with the path when an input is missing.
c5r_add_test(c5r_bakegrid_test bakegrid_test.cpp)
