# extras: increment 4's display-only extras (epic 0454). Ticket 4933: FixRoadLayout's road generator (roads.cpp). Every
# src/extras/*.cpp goes into c5render; the headers live in this folder under c5r/extras/.
file(GLOB C5R_EXTRAS_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
if(C5R_EXTRAS_SRC)
    target_sources(c5render PRIVATE ${C5R_EXTRAS_SRC})
endif()
target_include_directories(c5render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Ticket 4983 (epic 0455): the unit extras' drawing helpers in units/ (GarrisonVisible's perimeter look), headers beside
# their sources (#include "units/<name>.h").
file(GLOB C5R_EXTRAS_UNITS_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/units/*.cpp)
if(C5R_EXTRAS_UNITS_SRC)
    target_sources(c5render PRIVATE ${C5R_EXTRAS_UNITS_SRC})
endif()
