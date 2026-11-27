# fpkscan: list / extract packs, sweep every pack and texture of an install, duplicate-name analysis (task 2901).
c5r_add_tool(fpkscan fpkscan.cpp)
if(MSVC)
    # wmain entry point (clang-cl / MSVC link): the unicode CRT entry.
    target_link_options(fpkscan PRIVATE /ENTRY:wmainCRTStartup)
endif()
