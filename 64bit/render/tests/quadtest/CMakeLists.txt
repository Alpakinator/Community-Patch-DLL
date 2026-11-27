# quadtest tests (task 2906). The positive test needs the game files and the EXE (SKIP without them).
c5r_add_test(c5r_quadtest_test quadtest_test.cpp)
target_link_libraries(c5r_quadtest_test PRIVATE c5r_quadtest_core)
