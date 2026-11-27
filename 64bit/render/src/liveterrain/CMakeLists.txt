# liveterrain: the region producer of live terrain (ticket 4903, epic 0465; live-terrain.md 8): the piece library
# reader, the native matcher, the block blend and the change step of the Architect's way 3.
target_sources(c5render PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/coaststrips.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/library.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/producer.cpp)
# 4910 (epic 0615, live-terrain.md 8.10.1): the EXE's coastline curves and region fill.
target_sources(c5render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/coastlines.cpp)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../tests/liveterrain/coastlines ${CMAKE_CURRENT_BINARY_DIR}/coastlines_tests)
# The producer's tests and its command-line runner live under tests/liveterrain/producer/ (that folder is one level
# deeper than the tests/<module> glob of the root CMakeLists.txt, so it is added from here).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../tests/liveterrain/producer ${CMAKE_CURRENT_BINARY_DIR}/producer_tests)
# The coast strips' tests (ticket 4911), likewise one level deeper.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../tests/liveterrain/coaststrips ${CMAKE_CURRENT_BINARY_DIR}/coaststrips_tests)
