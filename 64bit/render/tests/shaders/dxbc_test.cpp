// DXBC container and checksum tests (task 2902): containers compiled in the test by D3DCompile, re-written by the
// official d3dcompiler (D3DSetBlobPart, D3DGetBlobPart) and built by hand. One negative test per check.
#include <windows.h>
#include <d3dcompiler.h>

#include "c5r/shaders/dxbc.h"
#include "testkit.h"
#include "testutil.h"

using namespace c5r;
using namespace c5r::shaders;
using namespace c5r::test;

namespace {
const char* kVs = "cbuffer C : register(b0) { float4x4 m; };\n"
                  "float4 main(float3 p : POSITION) : SV_Position { return mul(float4(p, 1), m); }\n";

std::vector<uint8_t> vs() { return compileHlsl(kVs, "main", "vs_5_0"); }
Status parse(const std::vector<uint8_t>& c, bool verify = true) {
    DxbcContainer x;
    return parseDxbc(c.data(), c.size(), x, verify);
}
std::vector<uint8_t> blobOf(ID3DBlob* b) {
    std::vector<uint8_t> v;
    if (b) {
        const uint8_t* p = static_cast<const uint8_t*>(b->GetBufferPointer());
        v.assign(p, p + b->GetBufferSize());
        b->Release();
    }
    return v;
}
} // namespace

C5R_TEST(dxbc_parses_compiled_shader) {
    const std::vector<uint8_t> c = vs();
    CHECK(!c.empty());
    DxbcContainer x;
    CHECK_OK(parseDxbc(c.data(), c.size(), x));
    CHECK_EQ(x.totalSize, static_cast<uint32_t>(c.size()));
    CHECK(x.program() != nullptr);
    CHECK_EQ(x.program()->tag(), std::string("SHEX"));
    CHECK(x.stage == Stage::Vertex);
    CHECK_EQ(int(x.major), 5);
    CHECK_EQ(int(x.minor), 0);
    CHECK_EQ(x.model(), std::string("vs_5_0"));
    CHECK(x.find(fourcc('R', 'D', 'E', 'F')) != nullptr);
    CHECK(x.find(fourcc('I', 'S', 'G', 'N')) != nullptr);
    const std::vector<uint8_t> s = stripLikeExe(c);
    CHECK_OK(parseDxbc(s.data(), s.size(), x));
    CHECK(x.find(fourcc('R', 'D', 'E', 'F')) == nullptr);
    CHECK(x.find(fourcc('S', 'T', 'A', 'T')) == nullptr);
    const std::vector<uint8_t> p = compileHlsl("float4 main() : SV_Target { return 1; }", "main", "ps_4_1");
    CHECK_OK(parseDxbc(p.data(), p.size(), x));
    CHECK_EQ(x.model(), std::string("ps_4_1"));
    CHECK_EQ(x.program()->tag(), std::string("SHDR"));
}

// The checksum matches what the official d3dcompiler writes, for every residue of (size - 20) mod 64 (both final-block
// branches): D3DSetBlobPart rewrites the container, with its checksum, around private data of 1..160 bytes.
C5R_TEST(dxbc_checksum_matches_official_writer_for_every_length) {
    const std::vector<uint8_t> c = vs();
    bool seen[64] = {};
    for (size_t n = 1; n <= 160; ++n) {
        std::vector<uint8_t> priv(n, static_cast<uint8_t>(n));
        ID3DBlob* out = nullptr;
        const HRESULT h = D3DSetBlobPart(c.data(), c.size(), D3D_BLOB_PRIVATE_DATA, 0, priv.data(), n, &out);
        CHECK(SUCCEEDED(h));
        const std::vector<uint8_t> b = blobOf(out);
        CHECK(b.size() > 20);
        if (b.size() <= 20) continue;
        seen[(b.size() - 20) % 64] = true;
        uint8_t sum[16];
        dxbcChecksum(b.data(), b.size(), sum);
        CHECK(std::memcmp(sum, b.data() + 4, 16) == 0);
        CHECK_OK(parse(b));
    }
    ID3DBlob* sig = nullptr;
    CHECK(SUCCEEDED(D3DGetBlobPart(c.data(), c.size(), D3D_BLOB_INPUT_SIGNATURE_BLOB, 0, &sig)));
    const std::vector<uint8_t> s = blobOf(sig);
    CHECK_OK(parse(s));
    int residues = 0;
    for (bool b : seen) residues += b ? 1 : 0;
    // container sizes are multiples of 4, so 16 of the 64 residues occur; both final-block branches are among them
    CHECK(residues >= 16);
    CHECK(seen[56] || seen[60]); // L >= 56: two final blocks
    CHECK(seen[0] || seen[52]);  // L < 56: one final block
}

C5R_TEST(dxbc_short_header_is_truncated) {
    std::vector<uint8_t> c = vs();
    c.resize(31);
    CHECK_ERR(parse(c), Err::Truncated);
}

C5R_TEST(dxbc_bad_magic) {
    std::vector<uint8_t> c = vs();
    c[2] = 'X';
    CHECK_ERR(parse(c), Err::BadMagic);
}

C5R_TEST(dxbc_bad_version) {
    std::vector<uint8_t> c = vs();
    c[20] = 2;
    dxbcFixChecksum(c.data(), c.size());
    CHECK_ERR(parse(c), Err::BadVersion);
}

C5R_TEST(dxbc_total_size_past_available_is_truncated) {
    std::vector<uint8_t> c = vs();
    c.resize(c.size() - 4);
    CHECK_ERR(parse(c), Err::Truncated);
}

C5R_TEST(dxbc_total_smaller_than_chunk_table_is_bad_layout) {
    std::vector<uint8_t> c = vs();
    set32(c, 28, 5000); // chunk count beyond 4096
    CHECK_ERR(parse(c, false), Err::BadLayout);
    c = vs();
    set32(c, 24, 36); // total 36 < 32 + 4 * count
    CHECK_ERR(parse(c, false), Err::BadLayout);
}

C5R_TEST(dxbc_bad_chunk_offsets_are_bad_layout) {
    std::vector<uint8_t> c = vs();
    set32(c, 32, get32(c, 32) + 2); // unaligned
    CHECK_ERR(parse(c, false), Err::BadLayout);
    c = vs();
    set32(c, 32, 8); // inside the header
    CHECK_ERR(parse(c, false), Err::BadLayout);
    c = vs();
    set32(c, 32, static_cast<uint32_t>(c.size() - 4)); // chunk header past the total
    CHECK_ERR(parse(c, false), Err::BadLayout);
}

C5R_TEST(dxbc_chunk_data_past_total_is_bad_layout) {
    std::vector<uint8_t> c = vs();
    const uint32_t off = get32(c, 32);
    set32(c, off + 4, 0x10000);
    CHECK_ERR(parse(c, false), Err::BadLayout);
}

C5R_TEST(dxbc_overlapping_chunks_are_bad_layout) {
    std::vector<uint8_t> c = vs();
    set32(c, 36, get32(c, 32)); // chunk 1 = chunk 0
    CHECK_ERR(parse(c, false), Err::BadLayout);
}

C5R_TEST(dxbc_checksum_mismatch_is_bad_layout) {
    std::vector<uint8_t> c = vs();
    c[c.size() - 1] ^= 0x01;
    CHECK_ERR(parse(c), Err::BadLayout);
    CHECK_OK(parse(c, false)); // the same container without verification
    c = vs();
    c[4] ^= 0x80; // the stored checksum itself
    CHECK_ERR(parse(c), Err::BadLayout);
}

// Every single-byte corruption fails; inside the chunk data it fails with BadLayout (the checksum).
C5R_TEST(dxbc_every_corrupted_byte_fails) {
    const std::vector<uint8_t> good = stripLikeExe(vs());
    const uint32_t firstChunk = get32(good, 32);
    for (size_t i = 0; i < good.size(); ++i) {
        std::vector<uint8_t> c = good;
        c[i] ^= 0x5A;
        DxbcContainer x;
        const Status s = parseDxbc(c.data(), c.size(), x, true);
        CHECK(!s.ok());
        if (i < 4) CHECK_ERR(s, Err::BadMagic);
        else if (i >= firstChunk) CHECK(s.code == Err::BadLayout || s.code == Err::Truncated);
        if (i >= firstChunk + 8 && s.code != Err::BadLayout) {
            // chunk headers (FourCC/size) may also be reported as layout errors; data bytes must be BadLayout
            bool inHeader = false;
            for (uint32_t k = 0; k < get32(good, 28); ++k) {
                const uint32_t off = get32(good, 32 + 4 * k);
                inHeader = inHeader || (i >= off && i < off + 8);
            }
            CHECK(inHeader);
        }
    }
}

C5R_TEST(dxbc_program_chunk_checks_are_bad_layout) {
    // program type 7
    std::vector<uint8_t> c = makeContainer({{fourcc('S', 'H', 'E', 'X'), makeProgram(7, 5, 0, {})}});
    CHECK_ERR(parse(c), Err::BadLayout);
    // length token disagrees with the chunk size
    std::vector<uint8_t> p = makeProgram(1, 5, 0, {0x0100003E}); // ret
    set32(p, 4, 9);
    c = makeContainer({{fourcc('S', 'H', 'E', 'X'), p}});
    CHECK_ERR(parse(c), Err::BadLayout);
    // shorter than its header
    c = makeContainer({{fourcc('S', 'H', 'D', 'R'), std::vector<uint8_t>(4, 0)}});
    CHECK_ERR(parse(c), Err::BadLayout);
    // a well-formed hand-built one parses
    c = makeContainer({{fourcc('S', 'H', 'E', 'X'), makeProgram(0, 5, 0, {0x0100003E})}});
    DxbcContainer x;
    CHECK_OK(parseDxbc(c.data(), c.size(), x));
    CHECK(x.stage == Stage::Pixel);
}
