// render/tests/common/testkit.h - minimal test kit for render/ (epic 2900). No third-party framework.
//
//   C5R_TEST(name) { CHECK(cond); CHECK_EQ(a, b); CHECK_ERR(status, c5r::Err::Truncated); }
//   int main() { return c5r::test::runAll(); }        // or use C5R_TEST_MAIN
//
// Game-file tests call c5r::test::gameDir() / exePath(); when the variable is unset or the path is missing they
// call skip(). An executable with any skipped test exits 77 (ctest: "skipped", never "passed"; bug 0091 item 2);
// failures (exit 1) take precedence. render/build.py --test fails the run when any test was skipped.
#pragma once
#include <cstdio>
#include <string>
#include "c5r/status.h"

namespace c5r { namespace test {

typedef void (*Fn)();
struct Reg { Reg(const char* name, Fn fn); };
int runAll();
// The exit code runAll() returns for these counts: 1 if any check failed, else 77 if any test skipped, else 0.
int exitCode(int ran, int failures, int skipped);
void fail(const char* file, int line, const std::string& what);
[[noreturn]] void skip(const std::string& why);
// C5R_GAME_DIR: the root of a Civ 5 install copy (folder with Assets/ and Resource/), read-only.
std::string gameDir();
// C5R_EXE: path of CivilizationV_DX11.exe (a copy, or the install's, opened read-only).
std::string exePath();
// C5R_WORK: scratch folder for large outputs (default <build tree>/test-out; the build tree is keyed by checkout).
std::string workDir();

}} // namespace c5r::test

#define C5R_TEST(name) \
    static void name(); static ::c5r::test::Reg name##_reg_(#name, &name); static void name()
#define C5R_TEST_MAIN int main() { return ::c5r::test::runAll(); }

#define CHECK(c) do { if (!(c)) ::c5r::test::fail(__FILE__, __LINE__, "CHECK(" #c ")"); } while (0)
#define CHECK_EQ(a, b) do { auto va_ = (a); auto vb_ = (b); if (!(va_ == vb_)) \
    ::c5r::test::fail(__FILE__, __LINE__, "CHECK_EQ(" #a ", " #b ")"); } while (0)
#define CHECK_OK(st) do { ::c5r::Status s_ = (st); if (!s_.ok()) \
    ::c5r::test::fail(__FILE__, __LINE__, "CHECK_OK(" #st "): " + s_.str()); } while (0)
// The negative-test assertion: the call must fail, and fail for the named reason.
#define CHECK_ERR(st, want) do { ::c5r::Status s_ = (st); if (s_.code != (want)) \
    ::c5r::test::fail(__FILE__, __LINE__, std::string("CHECK_ERR(" #st "): want ") + ::c5r::errName(want) + \
                      ", got " + s_.str()); } while (0)
