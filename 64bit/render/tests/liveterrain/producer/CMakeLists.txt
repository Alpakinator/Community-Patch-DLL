# The region producer's tests (ticket 4903): synthetic library and maps, every input check with a negative test;
# one test reads the install's TerrainPiecesLibrary.fxsb (C5R_GAME_DIR). lt_producer_run: the runner the offline
# evidence scripts call (render/evidence/0465/4903/).
c5r_add_test(c5r_liveterrain_producer_test producer_test.cpp)
c5r_add_tool(lt_producer_run lt_producer_run.cpp)
