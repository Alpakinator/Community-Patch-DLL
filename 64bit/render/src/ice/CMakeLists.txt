# ice: the polar ice with the EXE's IceShader (epic 0169, seam G, issue 4306). Every .cpp here is part of c5render.
file(GLOB _ice_src ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
target_sources(c5render PRIVATE ${_ice_src})
