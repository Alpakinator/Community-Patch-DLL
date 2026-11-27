# water: ocean, coast, lakes, currents, rivers and coast waves with the EXE's water programs (epic 3600).
# Lead's file, not edited by the workers: every .cpp in this folder is part of c5render (3602 currents.cpp, 3604
# water.cpp and its helpers, 3605/3606 their own files). Configure again after adding a file (build.py always does).
file(GLOB _water_src ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
target_sources(c5render PRIVATE ${_water_src})
