# c5r::bake::Residency and StandinBaker tests (issue 3407): every option and input check with a negative test, the
# residency with c5r::terrain::Renderer as its sink (C5R_EXE, else SKIP = fail), arrays, out of memory, the GPU budget.
c5r_add_test(c5r_bakeresidency_test residency_test.cpp)
