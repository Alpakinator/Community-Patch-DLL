# fowmask (epic 0168, task 3952): the FOW mask of c5r::fow::MaskRenderer from a text description of a capture's map and
# views (fowmask_input.py), for the comparison with stock's t4 (fowmask_compare.py).
c5r_add_tool(fowmask fowmask.cpp)
target_link_libraries(fowmask PRIVATE c5r_terrainshot_core)
if(MSVC)
    target_link_options(fowmask PRIVATE /ENTRY:wmainCRTStartup)  # wmain (as terrainshot)
endif()
