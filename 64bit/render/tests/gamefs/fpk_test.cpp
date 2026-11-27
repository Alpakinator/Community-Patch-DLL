// render/tests/gamefs/fpk_test.cpp - FPK index parser and archive (task 2901). Synthetic data only.
#include <cstdio>
#include <windows.h>
#include "c5r/gamefs/fpk.h"
#include "synth.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::gamefs;

namespace {
std::vector<uint8_t> three() {
    return synth::fpk({{"a.dds", {1, 2, 3}}, {"Long_Name.gr2", {4, 5, 6, 7, 8}}, {"c", {}}});
}
// Offset of the u32 fields of entry i (name length, then name+pad, then a, b, size, offset).
size_t entryAt(const std::vector<uint8_t>& v, int i) {
    size_t p = 14;
    for (int k = 0; k < i; ++k) {
        uint32_t n = v[p] | (v[p + 1] << 8) | (v[p + 2] << 16) | (static_cast<uint32_t>(v[p + 3]) << 24);
        p += 4 + n + (4 - n % 4) % 4 + 16;
    }
    return p;
}
size_t fieldsAt(const std::vector<uint8_t>& v, int i) {
    size_t p = entryAt(v, i);
    uint32_t n = v[p] | (v[p + 1] << 8) | (v[p + 2] << 16) | (static_cast<uint32_t>(v[p + 3]) << 24);
    return p + 4 + n + (4 - n % 4) % 4;
}
std::wstring writeTemp(const std::vector<uint8_t>& v, const char* name) {
    std::string p = c5r::test::workDir() + "/" + name;
    FILE* f = std::fopen(p.c_str(), "wb");
    if (f) {
        if (!v.empty()) std::fwrite(v.data(), 1, v.size(), f);
        std::fclose(f);
    }
    return std::wstring(p.begin(), p.end());
}
} // namespace

C5R_TEST(fpk_parses_well_formed_pack) {
    std::vector<uint8_t> v = three();
    std::vector<FpkEntry> e;
    CHECK_OK(parseFpkIndex(v.data(), v.size(), e));
    CHECK_EQ(e.size(), size_t(3));
    CHECK(e[0].name == "a.dds");
    CHECK_EQ(e[0].size, 3u);
    CHECK_EQ(v[e[1].offset + 4], 8);
    CHECK_EQ(e[2].size, 0u);
}

C5R_TEST(negative_fpk_truncated_header) {
    std::vector<uint8_t> v = three();
    std::vector<FpkEntry> e;
    CHECK_ERR(parseFpkIndex(v.data(), 13, e), Err::Truncated);
    CHECK_ERR(parseFpkIndex(nullptr, 0, e), Err::Truncated);
}

C5R_TEST(negative_fpk_bad_magic) {
    std::vector<uint8_t> v = three();
    v[4] = 'X';
    std::vector<FpkEntry> e;
    CHECK_ERR(parseFpkIndex(v.data(), v.size(), e), Err::BadMagic);
}

C5R_TEST(negative_fpk_bad_version) {
    std::vector<uint8_t> v = three();
    v[0] = 7;
    std::vector<FpkEntry> e;
    CHECK_ERR(parseFpkIndex(v.data(), v.size(), e), Err::BadVersion);
}

C5R_TEST(negative_fpk_nonzero_u16) {
    std::vector<uint8_t> v = three();
    v[8] = 1;
    std::vector<FpkEntry> e;
    CHECK_ERR(parseFpkIndex(v.data(), v.size(), e), Err::BadLayout);
}

C5R_TEST(negative_fpk_count_too_large) {
    std::vector<uint8_t> v = three();
    synth::set32(v, 10, 1000000);
    std::vector<FpkEntry> e;
    CHECK_ERR(parseFpkIndex(v.data(), v.size(), e), Err::BadLayout);
}

C5R_TEST(negative_fpk_entry_truncated) {
    // One entry announced, 20 bytes present (passes the count check), but its name length says 12 bytes of name.
    std::vector<uint8_t> v = synth::fpk({});
    synth::set32(v, 10, 1);
    synth::put32(v, 12);
    for (int i = 0; i < 16; ++i) v.push_back('x');
    std::vector<FpkEntry> e;
    CHECK_ERR(parseFpkIndex(v.data(), v.size(), e), Err::Truncated);
    // The name-length field of the second entry is missing: one 60-byte entry (passes the count check for 2).
    std::vector<uint8_t> w = synth::fpk({{std::string(40, 'n'), {}}});
    CHECK_OK(parseFpkIndex(w.data(), w.size(), e));
    synth::set32(w, 10, 2);
    CHECK_ERR(parseFpkIndex(w.data(), w.size(), e), Err::Truncated);
    // Cut inside the first entry's fields.
    w = synth::fpk({{std::string(40, 'n'), {}}});
    w.resize(w.size() - 4);
    CHECK_ERR(parseFpkIndex(w.data(), w.size(), e), Err::Truncated);
}

C5R_TEST(negative_fpk_name_length_zero_or_too_long) {
    std::vector<uint8_t> v = three();
    synth::set32(v, entryAt(v, 0), 0);
    std::vector<FpkEntry> e;
    CHECK_ERR(parseFpkIndex(v.data(), v.size(), e), Err::BadLayout);
    v = three();
    synth::set32(v, entryAt(v, 0), 261);
    CHECK_ERR(parseFpkIndex(v.data(), v.size(), e), Err::BadLayout);
}

C5R_TEST(negative_fpk_bad_name_characters) {
    std::vector<uint8_t> v = three();
    v[entryAt(v, 0) + 4] = '/';
    std::vector<FpkEntry> e;
    CHECK_ERR(parseFpkIndex(v.data(), v.size(), e), Err::BadLayout);
    v = three();
    v[entryAt(v, 0) + 4] = '\\';
    CHECK_ERR(parseFpkIndex(v.data(), v.size(), e), Err::BadLayout);
    v = three();
    v[entryAt(v, 0) + 5] = 0x01;
    CHECK_ERR(parseFpkIndex(v.data(), v.size(), e), Err::BadLayout);
}

C5R_TEST(negative_fpk_payload_past_end) {
    std::vector<uint8_t> v = three();
    synth::set32(v, fieldsAt(v, 1) + 8, 6);  // size 6 where 5 bytes remain at the end
    std::vector<FpkEntry> e;
    CHECK_ERR(parseFpkIndex(v.data(), v.size(), e), Err::BadLayout);
    v = three();
    synth::set32(v, fieldsAt(v, 0) + 12, 0xFFFFFFF0u);  // offset far past the end
    CHECK_ERR(parseFpkIndex(v.data(), v.size(), e), Err::BadLayout);
    v = three();  // truncating the file leaves the last payload hanging
    CHECK_ERR(parseFpkIndex(v.data(), v.size() - 1, e), Err::BadLayout);
}

C5R_TEST(negative_fpk_payload_inside_index) {
    std::vector<uint8_t> v = three();
    synth::set32(v, fieldsAt(v, 0) + 12, 14);
    std::vector<FpkEntry> e;
    CHECK_ERR(parseFpkIndex(v.data(), v.size(), e), Err::BadLayout);
}

C5R_TEST(negative_fpk_overlapping_payloads) {
    std::vector<uint8_t> v = three();
    uint32_t off1 = v[fieldsAt(v, 1) + 12] | (v[fieldsAt(v, 1) + 13] << 8);
    synth::set32(v, fieldsAt(v, 0) + 12, off1 + 1);  // a.dds now starts inside Long_Name.gr2
    std::vector<FpkEntry> e;
    CHECK_ERR(parseFpkIndex(v.data(), v.size(), e), Err::BadLayout);
    v = three();  // identical ranges count as overlap too
    synth::set32(v, fieldsAt(v, 0) + 12, off1);
    synth::set32(v, fieldsAt(v, 0) + 8, 5);
    CHECK_ERR(parseFpkIndex(v.data(), v.size(), e), Err::BadLayout);
}

C5R_TEST(negative_fpk_duplicate_names) {
    std::vector<uint8_t> v = synth::fpk({{"x.dds", {1}}, {"X.DDS", {2}}});
    std::vector<FpkEntry> e;
    CHECK_ERR(parseFpkIndex(v.data(), v.size(), e), Err::BadLayout);
}

C5R_TEST(fpk_archive_open_read_find) {
    std::wstring p = writeTemp(three(), "c5r_gamefs_three.fpk");
    FpkArchive a;
    CHECK_OK(a.open(p));
    CHECK_EQ(a.entries().size(), size_t(3));
    CHECK_EQ(a.find("LONG_NAME.GR2"), 1);
    CHECK_EQ(a.find("missing"), -1);
    std::vector<uint8_t> b;
    CHECK_OK(a.read(1, b));
    CHECK(b.size() == 5 && b[0] == 4 && b[4] == 8);
    CHECK_OK(a.read(2, b));
    CHECK(b.empty());
    size_t n = 0;
    CHECK(a.view(0, &n) != nullptr && n == 3);
    FpkArchive moved(std::move(a));
    CHECK(moved.isOpen() && !a.isOpen());
    CHECK_EQ(moved.find("a.dds"), 0);
}

C5R_TEST(negative_fpk_archive_errors) {
    FpkArchive a;
    CHECK_ERR(a.open(L"D:/c5-64bit-work/2900/test-out/does-not-exist.fpk"), Err::Io);
    std::vector<uint8_t> shortFile = {6, 0, 0, 0, 'F', 'P'};
    CHECK_ERR(a.open(writeTemp(shortFile, "c5r_gamefs_short.fpk")), Err::Truncated);
    std::vector<uint8_t> bad = three();
    bad[5] = 'Q';
    CHECK_ERR(a.open(writeTemp(bad, "c5r_gamefs_bad.fpk")), Err::BadMagic);
    CHECK(!a.isOpen());
    std::vector<uint8_t> b;
    CHECK_ERR(a.read(0, b), Err::Io);
    CHECK_OK(a.open(writeTemp(three(), "c5r_gamefs_three2.fpk")));
    CHECK_ERR(a.read(3, b), Err::NotFound);
    CHECK(a.view(3, nullptr) == nullptr);
}
