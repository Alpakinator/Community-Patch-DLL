// tests/check.h - minimal test harness (no third-party dependency). Owner: epic 1200 coordinator.
// Usage:  TEST(name) { CHECK(x == 1); CHECK_EQ(a, b); }   and one  VPIPC_TEST_MAIN()  per test file.
// Exit code 0 = all passed. A test executable is one ctest test (see tests/CMakeLists.txt).
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace vpipc_test {
struct Case { const char* name; void (*fn)(); Case* next; };
inline Case*& head() { static Case* h = nullptr; return h; }
inline int& failures() { static int f = 0; return f; }
struct Reg { Reg(const char* n, void (*f)()) { static Case c[256]; static int k = 0; c[k] = {n, f, head()}; head() = &c[k++]; } };
inline int run_all(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    Case* list[256]; int n = 0;
    for (Case* c = head(); c; c = c->next) list[n++] = c;
    int ran = 0;
    for (int i = n - 1; i >= 0; --i) {  // registration order
        if (filter && !strstr(list[i]->name, filter)) continue;
        int before = failures();
        printf("[ RUN  ] %s\n", list[i]->name); fflush(stdout);
        list[i]->fn();
        printf(failures() == before ? "[  OK  ] %s\n" : "[ FAIL ] %s\n", list[i]->name); fflush(stdout);
        ++ran;
    }
    printf("%d test(s), %d failure(s)\n", ran, failures());
    return failures() ? 1 : 0;
}
}  // namespace vpipc_test

#define TEST(name) static void name(); static vpipc_test::Reg reg_##name(#name, &name); static void name()
#define CHECK(cond) do { if (!(cond)) { ++vpipc_test::failures(); \
    printf("  %s(%d): CHECK failed: %s\n", __FILE__, __LINE__, #cond); fflush(stdout); } } while (0)
#define CHECK_EQ(a, b) do { auto va_ = (a); auto vb_ = (b); if (!(va_ == vb_)) { ++vpipc_test::failures(); \
    printf("  %s(%d): CHECK_EQ failed: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, \
           (long long)va_, (long long)vb_); fflush(stdout); } } while (0)
#define REQUIRE(cond) do { if (!(cond)) { ++vpipc_test::failures(); \
    printf("  %s(%d): REQUIRE failed: %s\n", __FILE__, __LINE__, #cond); fflush(stdout); return; } } while (0)
#define VPIPC_TEST_MAIN() int main(int argc, char** argv) { return vpipc_test::run_all(argc, argv); }
