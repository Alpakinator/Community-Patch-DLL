// Minimal test harness for models/ tests (epic 3100). Each tests/test_*.cpp is one executable registered with
// ctest. Exit 0 = pass, 1 = fail, 77 = skipped (some part needed game files that are not available: C5M_SAMPLES
// unset or a file missing; the checks that did run all passed).
#pragma once
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace t {
inline int& failures() { static int n = 0; return n; }
inline int& checks() { static int n = 0; return n; }
inline bool& skipped() { static bool s = false; return s; }

// Root of extracted game files (D:/c5-64bit-work/3100/extract by convention); "" if unset.
inline std::string samples() {
    const char* s = std::getenv("C5M_SAMPLES");
    return s ? std::string(s) : std::string();
}
inline bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}
// Reads <C5M_SAMPLES>/<rel>; marks the test skipped and returns false if unavailable.
inline bool sample(const std::string& rel, std::vector<uint8_t>& out) {
    std::string root = samples();
    if (root.empty() || !read_file(root + "/" + rel, out)) {
        std::printf("SKIP sample not available: %s\n", rel.c_str());
        skipped() = true;
        return false;
    }
    return true;
}
// Exit 77 (ctest: "Skipped") as soon as any part was skipped, even when other checks ran, so that a run without
// the game files cannot be mistaken for a full run (bug 0093 V5, issue 9025; it was 77 only when no check ran).
inline int finish() {
    std::printf("%d checks, %d failures%s\n", checks(), failures(), skipped() ? " (some parts skipped)" : "");
    if (failures()) return 1;
    return skipped() ? 77 : 0;
}
}  // namespace t

#define CHECK(cond)                                                                                     \
    do {                                                                                                \
        ++t::checks();                                                                                  \
        if (!(cond)) { ++t::failures(); std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); }   \
    } while (0)
#define CHECK_EQ(a, b)                                                                                  \
    do {                                                                                                \
        ++t::checks();                                                                                  \
        if (!((a) == (b))) {                                                                            \
            ++t::failures();                                                                            \
            std::printf("FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b);                          \
        }                                                                                               \
    } while (0)
