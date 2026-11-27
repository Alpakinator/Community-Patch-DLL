// ASan canary (issue 3209), built only in the AddressSanitizer variant (C5R_ASAN, build.py --asan). It reads one byte
// past a heap block on purpose. build.py --asan --test runs it once with the run's ASAN_OPTIONS into a separate log
// folder and fails the run unless a heap-buffer-overflow report appears there: proof that the instrumentation, the
// runtime DLL and the quoted log_path work, so that "0 ASan reports" from the real tests means something.
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
    // the size comes from the command line so the compiler cannot prove the overflow and drop it
    size_t n = argc > 1 ? static_cast<size_t>(std::strtoul(argv[1], nullptr, 10)) : 16;
    volatile char* p = static_cast<volatile char*>(std::malloc(n));
    if (!p) return 2;
    for (size_t i = 0; i < n; ++i) p[i] = static_cast<char>(i);
    char c = p[n];  // one past the end
    std::printf("asan canary: read %d past the block: ASan is not active\n", c);
    std::free(const_cast<char*>(p));
    return 0;
}
