# bakeblend: the per-vertex layer weights and the blend pass of the terrain bake (epic 3400, issue 3402).
target_sources(c5render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/blend.cpp ${CMAKE_CURRENT_SOURCE_DIR}/blendpass.cpp)
