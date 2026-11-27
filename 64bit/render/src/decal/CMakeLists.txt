# decal: terrain decals in the tile bake (epic 0166; research/05-graphics-composition/terrain-decals.md).
# Every .cpp of this folder (4103: the decal set, 4104: the pass); nobody needs to edit this file.
file(GLOB _c5r_decal_src CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
target_sources(c5render PRIVATE ${_c5r_decal_src})
