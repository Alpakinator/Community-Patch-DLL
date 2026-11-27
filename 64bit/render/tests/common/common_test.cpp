// render/tests/common/common_test.cpp - BinReader and Status (epic 2900).
#include "testkit.h"
#include "c5r/binreader.h"
#include <string>
#include <vector>
#include <windows.h>

using namespace c5r;

C5R_TEST(reads_little_endian) {
    const uint8_t b[] = {0x01, 0x02, 0x03, 0x04, 0xAA};
    BinReader r(b, sizeof b);
    CHECK_EQ(r.u32(), 0x04030201u);
    CHECK_EQ(r.u8(), 0xAAu);
    CHECK(r.ok());
    CHECK_EQ(r.remaining(), size_t(0));
}

C5R_TEST(negative_read_past_end_latches) {
    const uint8_t b[] = {0x01, 0x02, 0x03};
    BinReader r(b, sizeof b);
    CHECK_EQ(r.u32(), 0u);
    CHECK(!r.ok());
    CHECK_ERR(r.need(1, "after failure"), Err::Truncated);
}

C5R_TEST(negative_need_reports_truncated) {
    const uint8_t b[] = {0, 0};
    BinReader r(b, sizeof b);
    CHECK_OK(r.need(2, "two"));
    CHECK_ERR(r.need(3, "three"), Err::Truncated);
    CHECK(!r.seek(3));
}

// Bug 0091 item 2: one test skipped, none failed -> 77 (ctest "skipped"), not 0 ("passed").
C5R_TEST(testkit_exit_codes) {
    CHECK_EQ(test::exitCode(3, 0, 0), 0);
    CHECK_EQ(test::exitCode(2, 0, 1), 77);
    CHECK_EQ(test::exitCode(0, 0, 4), 77);
    CHECK_EQ(test::exitCode(2, 1, 1), 1);  // a failure wins over a skip
    CHECK_EQ(test::exitCode(0, 0, 0), 0);
}

#ifdef C5R_SKIP_PROBE
// The same end to end: the probe executable has one passing and one skipped test.
C5R_TEST(testkit_some_skipped_exits_77) {
    std::wstring cmd = L"\"";
    for (const char* c = C5R_SKIP_PROBE; *c; ++c) cmd += static_cast<wchar_t>(*c == '/' ? '\\' : *c);
    cmd += L"\"";
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = INVALID_HANDLE_VALUE;
    si.hStdError = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    CHECK(CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi));
    if (!pi.hProcess) return;
    CHECK_EQ(WaitForSingleObject(pi.hProcess, 30000), static_cast<DWORD>(WAIT_OBJECT_0));
    DWORD code = 0;
    CHECK(GetExitCodeProcess(pi.hProcess, &code));
    CHECK_EQ(code, static_cast<DWORD>(77));
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}
#endif

C5R_TEST_MAIN
