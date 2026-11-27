// PE32 reader tests (task 2902): synthetic PE files built in memory; one negative test per check.
#include "c5r/shaders/pe.h"
#include "testkit.h"
#include "testutil.h"

using namespace c5r;
using namespace c5r::shaders;
using c5r::test::buildPe;
using c5r::test::set32;

namespace {
std::vector<uint8_t> twoSections() {
    return buildPe({{".text", 0x1000, 0x300, std::vector<uint8_t>(0x300, 0xCC)},
                    {".shader", 0x2000, 0x100, std::vector<uint8_t>(0x100, 0x11)}});
}
Status parse(const std::vector<uint8_t>& f) {
    PeImage pe;
    return pe.parse(f.data(), f.size());
}
} // namespace

C5R_TEST(pe_parses_sections_and_maps_addresses) {
    const std::vector<uint8_t> f = twoSections();
    PeImage pe;
    CHECK_OK(pe.parse(f.data(), f.size()));
    CHECK_EQ(pe.imageBase(), 0x400000u);
    CHECK_EQ(pe.timeDateStamp(), 0x12345678u);
    CHECK_EQ(pe.sections().size(), size_t(2));
    const PeSection* s = pe.find(".shader");
    CHECK(s != nullptr);
    CHECK_EQ(s->rva, 0x2000u);
    CHECK_EQ(s->rawOffset, 0x800u); // .text raw 0x400..0x800 (0x300 rounded to 0x400)
    CHECK_EQ(s->fileBackedSize(), 0x100u);
    uint32_t off = 0, va = 0;
    CHECK_OK(pe.vaToOffset(0x402010, off));
    CHECK_EQ(off, 0x810u);
    CHECK_OK(pe.offsetToVa(0x810, va));
    CHECK_EQ(va, 0x402010u);
    CHECK(pe.find(".rsrc") == nullptr);
}

C5R_TEST(pe_address_outside_sections_is_not_found) {
    const std::vector<uint8_t> f = twoSections();
    PeImage pe;
    CHECK_OK(pe.parse(f.data(), f.size()));
    uint32_t x = 0;
    CHECK_ERR(pe.vaToOffset(0x3FFFFF, x), Err::NotFound);    // below the image base
    CHECK_ERR(pe.vaToOffset(0x402100, x), Err::NotFound);    // past .shader's file-backed bytes
    CHECK_ERR(pe.vaToOffset(0x400010, x), Err::NotFound);    // headers are not a section
    CHECK_ERR(pe.offsetToVa(0x10, x), Err::NotFound);
    CHECK_ERR(pe.offsetToVa(0x900, x), Err::NotFound);       // raw padding past .shader's virtual size
}

C5R_TEST(pe_short_dos_header_is_truncated) {
    std::vector<uint8_t> f = twoSections();
    f.resize(63);
    CHECK_ERR(parse(f), Err::Truncated);
}

C5R_TEST(pe_bad_mz_is_bad_magic) {
    std::vector<uint8_t> f = twoSections();
    f[1] = 'X';
    CHECK_ERR(parse(f), Err::BadMagic);
}

C5R_TEST(pe_lfanew_past_end_is_truncated) {
    std::vector<uint8_t> f = twoSections();
    set32(f, 0x3C, static_cast<uint32_t>(f.size() - 8));
    CHECK_ERR(parse(f), Err::Truncated);
}

C5R_TEST(pe_bad_signature_is_bad_magic) {
    std::vector<uint8_t> f = twoSections();
    f[0x42] = 1;
    CHECK_ERR(parse(f), Err::BadMagic);
}

C5R_TEST(pe_non_i386_machine_is_unsupported) {
    std::vector<uint8_t> f = twoSections();
    f[0x44] = 0x64; f[0x45] = 0x86; // AMD64
    CHECK_ERR(parse(f), Err::Unsupported);
}

C5R_TEST(pe_small_optional_header_is_bad_layout) {
    std::vector<uint8_t> f = twoSections();
    f[0x54] = 64;
    CHECK_ERR(parse(f), Err::BadLayout);
}

C5R_TEST(pe_optional_header_past_end_is_truncated) {
    std::vector<uint8_t> f = twoSections();
    f.resize(0x58 + 100);
    CHECK_ERR(parse(f), Err::Truncated);
}

C5R_TEST(pe_pe32_plus_is_bad_version) {
    std::vector<uint8_t> f = twoSections();
    f[0x58] = 0x0B; f[0x59] = 0x02;
    CHECK_ERR(parse(f), Err::BadVersion);
}

C5R_TEST(pe_zero_or_too_many_sections_is_bad_layout) {
    std::vector<uint8_t> f = twoSections();
    f[0x46] = 0;
    CHECK_ERR(parse(f), Err::BadLayout);
    f[0x46] = 97;
    CHECK_ERR(parse(f), Err::BadLayout);
}

C5R_TEST(pe_section_table_past_end_is_truncated) {
    std::vector<uint8_t> f = twoSections();
    f.resize(0x58 + 224 + 40 + 10); // the second entry is cut
    CHECK_ERR(parse(f), Err::Truncated);
}

C5R_TEST(pe_section_raw_data_past_end_is_truncated) {
    std::vector<uint8_t> f = twoSections();
    f.resize(f.size() - 0x100);
    CHECK_ERR(parse(f), Err::Truncated);
}

C5R_TEST(pe_missing_file_is_io) {
    std::vector<uint8_t> f;
    CHECK_ERR(readFile(L"Z:/no/such/dir/CivilizationV_DX11.exe", f), Err::Io);
}
