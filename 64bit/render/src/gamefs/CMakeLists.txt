# gamefs: FPK packs, the game's file lookup order, DDS textures into D3D11 (task 2901).
target_sources(c5render PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fpk.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dds.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/civ5pkg.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gamefiles.cpp)
