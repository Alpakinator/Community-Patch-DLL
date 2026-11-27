// render/tests/common/testkit.cpp (epic 2900)
#include "testkit.h"
#include <cstdlib>
#include <vector>
#include <windows.h>

namespace c5r { namespace test {

namespace {
struct Entry { const char* name; Fn fn; };
std::vector<Entry>& reg() { static std::vector<Entry> v; return v; }
int g_failures = 0;
struct SkipEx { std::string why; };

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}
bool exists(const std::string& p) {
    return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}
} // namespace

Reg::Reg(const char* name, Fn fn) { reg().push_back(Entry{name, fn}); }

void fail(const char* file, int line, const std::string& what) {
    ++g_failures;
    std::fprintf(stderr, "%s(%d): FAIL %s\n", file, line, what.c_str());
}

void skip(const std::string& why) { throw SkipEx{why}; }

std::string gameDir() {
    std::string d = env("C5R_GAME_DIR");
    if (d.empty() || !exists(d)) skip("C5R_GAME_DIR not set or missing");
    return d;
}
std::string exePath() {
    std::string p = env("C5R_EXE");
    if (p.empty() || !exists(p)) skip("C5R_EXE not set or missing");
    return p;
}
std::string workDir() {
    // Bug 0091 item 1: the default is this build tree's own folder (C5R_DEFAULT_WORK, set by render/CMakeLists.txt to
    // <build>/test-out), never a folder shared by every checkout on the machine.
    std::string d = env("C5R_WORK");
#ifdef C5R_DEFAULT_WORK
    if (d.empty()) d = C5R_DEFAULT_WORK;
#endif
    if (d.empty()) skip("C5R_WORK not set and no build-tree default");
    CreateDirectoryA(d.c_str(), nullptr);
    return d;
}

int runAll() {
    int skipped = 0, ran = 0;
    for (const Entry& e : reg()) {
        int before = g_failures;
        try {
            e.fn();
            ++ran;
            std::printf("%s %s\n", g_failures == before ? "PASS" : "FAIL", e.name);
        } catch (const SkipEx& s) {
            ++skipped;
            std::printf("SKIP %s (%s)\n", e.name, s.why.c_str());
        }
    }
    std::printf("%d run, %d failed checks, %d skipped\n", ran, g_failures, skipped);
    return exitCode(ran, g_failures, skipped);
}

int exitCode(int ran, int failures, int skipped) {
    (void)ran;
    if (failures) return 1;
    // Bug 0091 item 2: any skipped test makes the executable "skipped" (77), not "passed", so that ctest and
    // render/build.py --test (which fails on any skip) show it; before 2026-11-11 only "all skipped" returned 77.
    if (skipped) return 77;
    return 0;
}

}} // namespace c5r::test
