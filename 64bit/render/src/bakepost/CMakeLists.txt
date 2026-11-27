# bakepost: the terrain bake's passes after the blend, as far as the BC1 caches t0 / t8 (issue 3403, epic 3400).
target_sources(c5render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/post.cpp ${CMAKE_CURRENT_SOURCE_DIR}/style.cpp)
