# bakeresidency: residency, bake-ahead and invalidation of baked tiles, and the stand-in TileBaker (issue 3407, epic 3400);
# the near-camera pair's pools and state machine (issue 0332).
target_sources(c5render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/residency.cpp ${CMAKE_CURRENT_SOURCE_DIR}/standin.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/nearcache.cpp)
