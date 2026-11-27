# Terrain-side shader sets (task 2903): descriptions of the EXE's terrain, bake, water, river, decal, overlay and fog-of-war
# shader sets with their constant-buffer layouts and texture slots, and the check against the parsed blobs.
target_sources(c5render PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/validate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lean.cpp)
