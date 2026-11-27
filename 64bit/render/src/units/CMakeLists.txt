# units: the renderer's unit module c5r::units (epic 0041, U1-U3; research/05-graphics-composition/unit-renderer.md).
# Globbed so that parallel workers never edit this file: every src/units/*.cpp goes into c5render.
#
# c5models (models/, the .gr2 reader, the pose sampler, the 28-byte repack, the art tables) is built here from its own
# sources into c5r_models, with models' own warning set, and linked to c5render (decision 1 of the epic brief; CMP0079
# allows target_link_libraries on c5render from this directory). render/CMakeLists.txt is not touched, and models'
# tests are not registered in render's ctest (models/build.py keeps running them).
set(C5R_MODELS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../models)
file(GLOB C5R_MODELS_SRC CONFIGURE_DEPENDS ${C5R_MODELS_DIR}/src/*.cpp)
add_library(c5r_models STATIC ${C5R_MODELS_SRC})
target_include_directories(c5r_models PUBLIC ${C5R_MODELS_DIR}/include)
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    # models/CMakeLists.txt's ASan set: cl.exe builds models at /W3 without /WX
    target_compile_options(c5r_models PRIVATE /W3 /EHsc)
else()
    target_compile_options(c5r_models PRIVATE /W4 /WX -Wno-unused-parameter /EHsc)
endif()
target_compile_definitions(c5r_models PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

file(GLOB C5R_UNITS_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
if(C5R_UNITS_SRC)
    target_sources(c5render PRIVATE ${C5R_UNITS_SRC})
endif()
target_link_libraries(c5render PUBLIC c5r_models)
