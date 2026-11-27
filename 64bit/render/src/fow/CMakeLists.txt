# fow: the fog of war's mask and cloud planes with the EXE's own FOW programs (epic 0168).
# Lead's file, not edited by the workers: every .cpp in this folder is part of c5render (3952 mask*.cpp, 3955
# cloud*.cpp). Configure again after adding a file (build.py always does).
file(GLOB _fow_src ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
target_sources(c5render PRIVATE ${_fow_src})
