# bakecompare: server t0/t8 against the EXE's own caches of the reference set, texel by texel (issue 3404, acceptance B).
# Issue 4106 (epic 0166): --decals FILE.dtap reads the decal tap's file form through the server's own reader
# (server/src/render/srv_decal_tap.cpp, the code server.exe compiles; 4103), so the comparison uses the product's checks.
set(_repo ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
add_library(bakecompare_decalsrc STATIC ${_repo}/server/src/render/srv_decal_tap.cpp ${_repo}/server/src/render/srv_render_common.cpp)
target_include_directories(bakecompare_decalsrc PUBLIC ${_repo}/server/src/render ${_repo}/shell/common)
target_link_libraries(bakecompare_decalsrc PUBLIC c5render)
c5r_add_tool(bakecompare bakecompare.cpp)
target_link_libraries(bakecompare PRIVATE bakecompare_decalsrc)
