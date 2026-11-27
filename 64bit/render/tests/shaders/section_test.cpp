// ShaderSection tests (task 2902): synthetic PE files whose `.shader` section holds shaders compiled in the test.
#include "c5r/shaders/shadersection.h"
#include "testkit.h"
#include "testutil.h"

using namespace c5r;
using namespace c5r::shaders;
using namespace c5r::test;

namespace {
std::vector<uint8_t> vsBlob() {
    return stripLikeExe(compileHlsl("float4 main(float3 p : POSITION) : SV_Position { return float4(p, 1); }", "main", "vs_5_0"));
}
std::vector<uint8_t> psBlob() {
    return stripLikeExe(compileHlsl("float4 main() : SV_Target { return float4(1, 0, 0, 1); }", "main", "ps_4_0"));
}
// .text, then .shader = [vs][pad to 8][ps][extra], virtual size = all of it
std::vector<uint8_t> exeWith(const std::vector<uint8_t>& shader, uint32_t vsize, uint32_t stamp = 0x12345678) {
    return buildPe({{".text", 0x1000, 0x10, std::vector<uint8_t>(0x10, 0xC3)}, {".shader", 0x2000, vsize, shader}},
                   0x400000, stamp);
}
std::vector<uint8_t> twoBlobs(std::vector<uint8_t>* vsOut = nullptr, std::vector<uint8_t>* psOut = nullptr) {
    const std::vector<uint8_t> a = vsBlob(), b = psBlob();
    std::vector<uint8_t> s = a;
    while (s.size() % 8) s.push_back(0);
    s.insert(s.end(), b.begin(), b.end());
    if (vsOut) *vsOut = a;
    if (psOut) *psOut = b;
    return s;
}
} // namespace

C5R_TEST(section_splits_blobs_and_accounts_for_every_byte) {
    std::vector<uint8_t> a, b;
    const std::vector<uint8_t> s = twoBlobs(&a, &b);
    ShaderSection sec;
    CHECK_OK(sec.parse(exeWith(s, static_cast<uint32_t>(s.size()))));
    CHECK_EQ(sec.blobs().size(), size_t(2));
    CHECK_EQ(sec.sectionVa(), 0x402000u);
    CHECK_EQ(sec.sectionSize(), static_cast<uint32_t>(s.size()));
    const ShaderBlob& v = sec.blobs()[0];
    CHECK_EQ(v.va, 0x402000u);
    CHECK_EQ(v.fileOffset, sec.sectionFileOffset());
    CHECK_EQ(v.size, static_cast<uint32_t>(a.size()));
    CHECK(v.stage == Stage::Vertex);
    CHECK_EQ(v.model(), std::string("vs_5_0"));
    CHECK(std::memcmp(v.bytes, a.data(), a.size()) == 0);
    CHECK(std::memcmp(v.checksum, a.data() + 4, 16) == 0);
    const ShaderBlob& p = sec.blobs()[1];
    CHECK_EQ(p.model(), std::string("ps_4_0"));
    CHECK(sec.findByVa(p.va) == &p);
    CHECK(sec.findByVa(p.va + 4) == nullptr);
    uint64_t sum = 0;
    for (const ShaderBlob& x : sec.blobs()) sum += x.size;
    for (const SectionGap& g : sec.gaps()) { sum += g.size; CHECK(g.allZero); }
    CHECK_EQ(sum, static_cast<uint64_t>(sec.sectionSize()));
    CHECK_EQ(sec.gaps().size(), size_t(a.size() % 8 ? 1 : 0));
    // not build 4390913: no create sites
    CHECK(!sec.createSitesApplied());
    CHECK_EQ(v.set, -1);
    CHECK_EQ(v.createSite, 0u);
}

// Bug 0091 item 4: a gap that is not alignment padding (more than 4 bytes, or not all zero) is BadLayout, not Ok.
C5R_TEST(negative_section_non_zero_gap_is_bad_layout) {
    std::vector<uint8_t> s = twoBlobs();
    const std::vector<uint8_t> junk = {1, 2, 3, 4, 5, 6, 7, 8};
    s.insert(s.end(), junk.begin(), junk.end());
    ShaderSection sec;
    CHECK_ERR(sec.parse(exeWith(s, static_cast<uint32_t>(s.size()))), Err::BadLayout);
    CHECK(sec.blobs().size() == size_t(2));  // what was parsed before the gap; the Status is what callers check
}

C5R_TEST(negative_section_long_zero_gap_is_bad_layout) {
    std::vector<uint8_t> s = twoBlobs();
    s.insert(s.end(), 8, uint8_t(0));
    ShaderSection sec;
    CHECK_ERR(sec.parse(exeWith(s, static_cast<uint32_t>(s.size()))), Err::BadLayout);
}

C5R_TEST(negative_section_damaged_magic_is_bad_layout) {
    // The verification's case (asset-pipeline-verification.md 4, D4): the second container's "DXBC" magic broken. Before
    // the fix the walk skipped it as one gap and returned Ok with one blob.
    std::vector<uint8_t> a;
    std::vector<uint8_t> s = twoBlobs(&a);
    size_t second = a.size();
    while (second % 8) ++second;
    CHECK(std::memcmp(s.data() + second, "DXBC", 4) == 0);
    s[second + 1] = 'Y';
    ShaderSection sec;
    Status st = sec.parse(exeWith(s, static_cast<uint32_t>(s.size())));
    CHECK_ERR(st, Err::BadLayout);
    CHECK(st.msg.find("gap of") != std::string::npos);
}

C5R_TEST(section_four_byte_zero_gap_is_padding) {
    // the EXE's own case: 4 zero bytes between containers (each blob starts 8-aligned)
    std::vector<uint8_t> a = vsBlob();
    std::vector<uint8_t> s = a;
    while (s.size() % 8) s.push_back(0);
    if (s.size() == a.size()) s.insert(s.end(), 4, uint8_t(0));  // force one 4-byte gap
    const std::vector<uint8_t> b = psBlob();
    s.insert(s.end(), b.begin(), b.end());
    ShaderSection sec;
    CHECK_OK(sec.parse(exeWith(s, static_cast<uint32_t>(s.size()))));
    CHECK_EQ(sec.blobs().size(), size_t(2));
    CHECK_EQ(sec.gaps().size(), size_t(1));
    CHECK(sec.gaps()[0].size == 4 && sec.gaps()[0].allZero);
}

// ---- EXE builds (ticket 4966): the build is the stamp and SizeOfImage together (exebuild/include/vp_exe_build.h)
namespace {
const uint32_t kStamp4390913 = 0x546CD0A8u, kSoi4390913 = 0x06519000u;
const uint32_t kStamp26577625 = 0x6AF3837Du, kSoi26577625 = 0x063A2000u;
std::vector<uint8_t> exeOfBuild(const std::vector<uint8_t>& shader, uint32_t stamp, uint32_t soi) {
    return buildPe({{".text", 0x1000, 0x10, std::vector<uint8_t>(0x10, 0xC3)},
                    {".shader", 0x2000, static_cast<uint32_t>(shader.size()), shader}},
                   0x400000, stamp, soi);
}
// A two-row array for twoBlobs() in a fake build: the blobs "were" at 0x500000 / 0x500800 in build 4390913.
struct FakeArray {
    uint32_t anchor[16], exe[16];
    CreateSiteArray a;
    FakeArray(uint32_t vsSize, uint32_t psSize, uint32_t psExeVa) {
        const uint32_t an[16] = {0x500000, vsSize, 0, 50, 0x4E5C20, 0x4EE47F, 0x2DE1C8C, 7,
                                 0x500800, psSize, 1, 40, 0x4E5C20, 0x4EE4A0, 0x2DE1C90, 0};
        const uint32_t ex[16] = {0x402000, vsSize, 0, 50, 0x7E0000, 0x7E1000, 0x2C70000, 7,
                                 psExeVa, psSize, 1, 40, 0x7E0000, 0x7E1020, 0x2C70004, 0};
        std::memcpy(anchor, an, sizeof an);
        std::memcpy(exe, ex, sizeof ex);
        a.anchor = anchor;
        a.exe = exe;
        a.rows = 2;
        a.cols = 8;
    }
};
} // namespace

C5R_TEST(section_of_an_unknown_build_applies_no_sites) {
    const std::vector<uint8_t> s = twoBlobs();
    ShaderSection sec;
    // the stamp of build 4390913 with another SizeOfImage is not that build (the old check took the stamp alone)
    CHECK_OK(sec.parse(exeOfBuild(s, kStamp4390913, 0x01000000u)));
    CHECK_EQ(sec.build(), -1);
    CHECK(!sec.createSitesApplied());
    CHECK_EQ(sec.blobs()[0].set, -1);
    CHECK_EQ(sec.blobs()[0].va, sec.blobs()[0].exeVa);
    CHECK(sec.findByVa(0x402000u) == &sec.blobs()[0]);
    size_t n = 0;
    CHECK(createSiteTable(n) != nullptr);
    CHECK_EQ(n, size_t(1417));
}

C5R_TEST(compiled_array_anchor_is_the_committed_table) {
    size_t n = 0;
    const CreateSite* t = createSiteTable(n);
    const CreateSiteArray a = createSiteArray(0);
    CHECK(a.anchor != nullptr && a.exe != nullptr);
    CHECK_EQ(a.rows, 1417);
    CHECK_EQ(a.cols, 8);
    size_t same = 0;
    for (size_t i = 0; i < n && a.anchor; ++i) {
        const uint32_t* r = a.anchor + i * 8;
        same += r[0] == t[i].blobVa && r[1] == t[i].size && r[2] == t[i].stage && r[3] == static_cast<uint32_t>(t[i].set) &&
                r[4] == t[i].function && r[5] == t[i].callSite && r[6] == t[i].handleGlobal && r[7] == t[i].vertexFormatMask;
    }
    CHECK_EQ(same, n);
    // the anchor build carries every cell as itself
    CHECK(a.anchor && a.exe && std::memcmp(a.anchor, a.exe, n * 8 * 4) == 0);
    // a build id the binary does not know has no array
    CHECK(createSiteArray(-1).exe == nullptr && createSiteArray(99).exe == nullptr);
}

C5R_TEST(negative_known_build_with_other_blobs_fails_loud) {
    // build 4390913's header over two synthetic blobs: row 0's blob is not at its VA: the parse fails (it used to apply
    // nothing and return Ok)
    const std::vector<uint8_t> s = twoBlobs();
    ShaderSection sec;
    Status st = sec.parse(exeOfBuild(s, kStamp4390913, kSoi4390913));
    CHECK_ERR(st, Err::BadLayout);
    CHECK(st.msg.find("build 4390913") != std::string::npos && st.msg.find("carried VA") != std::string::npos);
    CHECK(!sec.createSitesApplied());
}

C5R_TEST(known_build_maps_build_4390913_vas_to_its_own) {
    std::vector<uint8_t> a, b;
    const std::vector<uint8_t> s = twoBlobs(&a, &b);
    size_t second = a.size();
    while (second % 8) ++second;
    FakeArray fa(static_cast<uint32_t>(a.size()), static_cast<uint32_t>(b.size()), 0x402000u + static_cast<uint32_t>(second));
    ShaderSection sec;
    sec.setCreateSiteArrayForTest(&fa.a);
    CHECK_OK(sec.parse(exeOfBuild(s, kStamp26577625, kSoi26577625)));
    CHECK_EQ(sec.build(), 1);
    CHECK(sec.createSitesApplied());
    const ShaderBlob* ps = sec.findByVa(0x500800u);   // the build-4390913 name
    CHECK(ps != nullptr && ps == sec.findByExeVa(0x402000u + static_cast<uint32_t>(second)));
    CHECK(ps && ps->va == 0x500800u && ps->set == 40 && ps->createSite == 0x4EE4A0u && ps->exeCreateSite == 0x7E1020u &&
          ps->handleGlobal == 0x2DE1C90u && ps->exeHandleGlobal == 0x2C70004u && ps->exeCreateFunction == 0x7E0000u);
    CHECK(sec.findByVa(0x402000u) == nullptr);   // this EXE's own VA is not a build-4390913 name
    CHECK(sec.findByExeVa(0x402000u) == sec.findByVa(0x500000u));
}

C5R_TEST(negative_known_build_without_its_array_fails_loud) {
    std::vector<uint8_t> a, b;
    const std::vector<uint8_t> s = twoBlobs(&a, &b);
    size_t second = a.size();
    while (second % 8) ++second;
    FakeArray fa(static_cast<uint32_t>(a.size()), static_cast<uint32_t>(b.size()), 0x402000u + static_cast<uint32_t>(second));
    ShaderSection sec;
    CreateSiteArray none = fa.a;
    none.exe = nullptr;   // the binary has no table for the build
    sec.setCreateSiteArrayForTest(&none);
    Status st = sec.parse(exeOfBuild(s, kStamp26577625, kSoi26577625));
    CHECK_ERR(st, Err::NotFound);
    CHECK(st.msg.find("build 26577625") != std::string::npos && st.msg.find("no create-site array") != std::string::npos);
    // a cell not carried (the call site of row 1)
    fa.exe[8 + 5] = 0;
    sec.setCreateSiteArrayForTest(&fa.a);
    st = sec.parse(exeOfBuild(s, kStamp26577625, kSoi26577625));
    CHECK_ERR(st, Err::NotFound);
    CHECK(st.msg.find("row 1 column 5") != std::string::npos && st.msg.find("not carried") != std::string::npos);
}

C5R_TEST(negative_blob_not_at_its_carried_va_fails_loud) {
    std::vector<uint8_t> a, b;
    const std::vector<uint8_t> s = twoBlobs(&a, &b);
    ShaderSection sec;
    FakeArray fa(static_cast<uint32_t>(a.size()), static_cast<uint32_t>(b.size()), 0x402000u + 0x10u);   // wrong VA
    sec.setCreateSiteArrayForTest(&fa.a);
    Status st = sec.parse(exeOfBuild(s, kStamp26577625, kSoi26577625));
    CHECK_ERR(st, Err::BadLayout);
    CHECK(st.msg.find("row 1") != std::string::npos && st.msg.find("carried VA 0x00402010") != std::string::npos);
    // a blob no row names: one row for two blobs
    size_t second = a.size();
    while (second % 8) ++second;
    FakeArray one(static_cast<uint32_t>(a.size()), static_cast<uint32_t>(b.size()), 0x402000u + static_cast<uint32_t>(second));
    one.a.rows = 1;
    sec.setCreateSiteArrayForTest(&one.a);
    st = sec.parse(exeOfBuild(s, kStamp26577625, kSoi26577625));
    CHECK_ERR(st, Err::BadLayout);
    CHECK(st.msg.find("2 blobs in the section, 1 create-site rows") != std::string::npos);
}

C5R_TEST(section_missing_is_not_found) {
    ShaderSection sec;
    CHECK_ERR(sec.parse(buildPe({{".text", 0x1000, 0x10, std::vector<uint8_t>(0x10, 0)}})), Err::NotFound);
}

C5R_TEST(section_bad_pe_fails_with_the_pe_error) {
    std::vector<uint8_t> f = exeWith(twoBlobs(), 0x100);
    f[0] = 'Z';
    ShaderSection sec;
    CHECK_ERR(sec.parse(f), Err::BadMagic);
    CHECK_ERR(sec.open(L"Z:/no/such/CivilizationV_DX11.exe"), Err::Io);
}

C5R_TEST(section_corrupted_blob_is_bad_layout) {
    std::vector<uint8_t> s = twoBlobs();
    s[100] ^= 0x40;
    ShaderSection sec;
    CHECK_ERR(sec.parse(exeWith(s, static_cast<uint32_t>(s.size()))), Err::BadLayout);
}

C5R_TEST(section_blob_past_the_section_is_truncated) {
    const std::vector<uint8_t> s = twoBlobs();
    ShaderSection sec;
    CHECK_ERR(sec.parse(exeWith(s, static_cast<uint32_t>(s.size() - 16))), Err::Truncated);
}
