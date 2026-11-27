# reflbench (issue 0382): the GPU cost of the reflected ice (the reflection target's clear and the ice draw into it).
# Driver and results in render/evidence/0382/.
c5r_add_tool(reflbench reflbench.cpp)
target_link_libraries(reflbench PRIVATE d3d11 dxgi)
if(MSVC)
    target_link_options(reflbench PRIVATE /ENTRY:wmainCRTStartup)  # wmain (as initbench)
endif()
