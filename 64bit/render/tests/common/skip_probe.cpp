// render/tests/common/skip_probe.cpp - not a test of its own: an executable with one passing and one skipped test,
// run by common_test (testkit_some_skipped_exits_77) to check the exit code of runAll() end to end (bug 0091 item 2).
// It is built but not registered with ctest.
#include "testkit.h"

C5R_TEST(probe_passes) { CHECK(true); }
C5R_TEST(probe_skips) { c5r::test::skip("probe: skipped on purpose"); }

C5R_TEST_MAIN
