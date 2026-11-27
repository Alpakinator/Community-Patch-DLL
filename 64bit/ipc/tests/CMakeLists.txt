# ipc/tests - one executable and one ctest test per tests/test_*.cpp. Owner: epic 1200 coordinator.
# Workers add test files; this file globs them. Extra sources a test needs go in tests/support/<test>/*.cpp.
file(GLOB VPIPC_TEST_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
foreach(test_src ${VPIPC_TEST_FILES})
    get_filename_component(test_name ${test_src} NAME_WE)
    file(GLOB test_support CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/support/${test_name}/*.cpp)
    add_executable(${test_name} ${test_src} ${test_support})
    target_link_libraries(${test_name} PRIVATE vpipc)
    target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    target_compile_options(${test_name} PRIVATE ${VPIPC_EXE_OPTIONS})
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 300)
endforeach()
