// .lean loader tests (issue 3602): every parseLean check on synthetic files built here, the mip addressing, texture
// creation, and the water's three files read through c5r::gamefs from C5R_GAME_DIR (SKIP without it).
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "c5r/gamefs/gamefiles.h"
#include "c5r/terrainsets/lean.h"
#include "testkit.h"

using Microsoft::WRL::ComPtr;
using namespace c5r;
using namespace c5r::terrainsets;

namespace {

void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) { std::memcpy(&b[at], &v, 4); }

// A synthetic .lean: w x h, mips, bpc; texel bytes = a running counter so that mips can be told apart.
std::vector<uint8_t> makeLean(uint32_t w, uint32_t h, uint32_t mips, uint32_t bpc) {
    const uint64_t texels = leanTexels(w, h, mips);
    const uint32_t four = static_cast<uint32_t>(texels * 4 * bpc), one = static_cast<uint32_t>(texels * bpc);
    std::vector<uint8_t> b(kLeanHeaderSize + four + one);
    std::memcpy(&b[0], "LEAN", 4);
    const float k = 0.33f;
    std::memcpy(&b[4], &k, 4);
    put32(b, 8, w);
    put32(b, 0xc, h);
    put32(b, 0x10, mips);
    put32(b, 0x14, bpc);
    put32(b, 0x18, kLeanHeaderSize);
    put32(b, 0x1c, kLeanHeaderSize + four);
    b[0x20] = 1;
    b[0x21] = b[0x22] = b[0x23] = 0xcc;
    for (size_t i = kLeanHeaderSize; i < b.size(); ++i) b[i] = static_cast<uint8_t>(i * 7);
    return b;
}

std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

} // namespace

C5R_TEST(lean_texel_counts) {
    CHECK_EQ(leanTexels(512, 512, 10), 349525ull);   // waterbumps16.lean
    CHECK_EQ(leanTexels(256, 64, 9), 21847ull);      // the river files
    CHECK_EQ(leanFullChain(512, 512), 10u);
    CHECK_EQ(leanFullChain(256, 64), 9u);
    CHECK_EQ(leanFullChain(1, 1), 1u);
}

C5R_TEST(lean_parse_good) {
    std::vector<uint8_t> b = makeLean(256, 64, 9, 2);
    LeanFile f;
    CHECK_OK(parseLean(b.data(), b.size(), f));
    CHECK_EQ(f.width, 256u);
    CHECK_EQ(f.height, 64u);
    CHECK_EQ(f.mips, 9u);
    CHECK_EQ(f.bytesPerChannel, 2u);
    CHECK_EQ(f.byte20, 1);
    CHECK(f.unknown04 == 0.33f);
    CHECK_EQ(f.fourBytes, static_cast<size_t>(21847 * 8));
    CHECK_EQ(f.oneBytes, static_cast<size_t>(21847 * 2));
    CHECK(f.four == b.data() + 0x24);
    CHECK(f.fourFormat == DXGI_FORMAT_R16G16B16A16_UNORM && f.oneFormat == DXGI_FORMAT_R16_UNORM);
    // mips: largest first, tightly packed
    LeanMip m;
    CHECK_OK(leanMip(f, true, 0, m));
    CHECK(m.data == f.four && m.width == 256 && m.height == 64 && m.rowPitch == 256 * 8);
    CHECK_OK(leanMip(f, true, 1, m));
    CHECK(m.data == f.four + 256 * 64 * 8 && m.width == 128 && m.height == 32);
    CHECK_OK(leanMip(f, false, 8, m));
    CHECK(m.width == 1 && m.height == 1 && m.rowPitch == 2);
    CHECK(m.data + 2 == f.one + f.oneBytes);   // the last mip ends the block
    CHECK(leanMip(f, false, 9, m).code == Err::BadLayout);
    LeanFile empty;
    CHECK(leanMip(empty, true, 0, m).code == Err::BadLayout);
    // 8-bit variant
    std::vector<uint8_t> b8 = makeLean(64, 64, 7, 1);
    CHECK_OK(parseLean(b8.data(), b8.size(), f));
    CHECK(f.fourFormat == DXGI_FORMAT_R8G8B8A8_UNORM && f.oneFormat == DXGI_FORMAT_R8_UNORM);
    // trailing bytes are allowed
    b.push_back(0);
    CHECK_OK(parseLean(b.data(), b.size(), f));
}

C5R_TEST(lean_parse_rejects) {
    const std::vector<uint8_t> good = makeLean(64, 32, 7, 2);
    LeanFile f;
    CHECK(parseLean(nullptr, 0, f).code == Err::Truncated);
    CHECK(parseLean(good.data(), 0x23, f).code == Err::Truncated);
    std::vector<uint8_t> b = good;
    b[0] = 'X';
    CHECK(parseLean(b.data(), b.size(), f).code == Err::BadMagic);
    b = good;
    put32(b, 8, 0);
    CHECK(parseLean(b.data(), b.size(), f).code == Err::BadLayout);   // width 0
    b = good;
    put32(b, 0xc, kLeanMaxSize + 1);
    CHECK(parseLean(b.data(), b.size(), f).code == Err::BadLayout);   // height too large
    b = good;
    put32(b, 0x10, 0);
    CHECK(parseLean(b.data(), b.size(), f).code == Err::BadLayout);   // no mips
    b = good;
    put32(b, 0x10, 8);
    CHECK(parseLean(b.data(), b.size(), f).code == Err::BadLayout);   // more mips than 64 x 32 has
    b = good;
    put32(b, 0x14, 4);
    CHECK(parseLean(b.data(), b.size(), f).code == Err::Unsupported); // 4 bytes per channel
    b = good;
    put32(b, 0x14, 0);
    CHECK(parseLean(b.data(), b.size(), f).code == Err::Unsupported);
    b = good;
    put32(b, 0x18, 0x10);
    CHECK(parseLean(b.data(), b.size(), f).code == Err::BadLayout);   // offset inside the header
    b = good;
    put32(b, 0x1c, 0x24 + 8);
    CHECK(parseLean(b.data(), b.size(), f).code == Err::BadLayout);   // one-channel block overlaps the four-channel one
    b = good;
    b.resize(b.size() - 1);
    CHECK(parseLean(b.data(), b.size(), f).code == Err::Truncated);   // last block runs past the end
    b = good;
    put32(b, 0x1c, static_cast<uint32_t>(b.size()));
    CHECK(parseLean(b.data(), b.size(), f).code == Err::Truncated);
    CHECK(f.four == nullptr);   // a failed parse leaves out empty
}

C5R_TEST(lean_create_textures) {
    const D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    ComPtr<ID3D11Device> dev;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, nullptr)))
        CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, nullptr)));
    std::vector<uint8_t> b = makeLean(64, 16, 7, 2);
    LeanFile f;
    CHECK_OK(parseLean(b.data(), b.size(), f));
    ID3D11Texture2D *t4 = nullptr, *t1 = nullptr;
    CHECK(createLeanTextures(nullptr, f, &t4, &t1).code == Err::BadLayout);
    CHECK(createLeanTextures(dev.Get(), f, nullptr, &t1).code == Err::BadLayout);
    CHECK(createLeanTextures(dev.Get(), LeanFile(), &t4, &t1).code == Err::BadLayout);
    CHECK_OK(createLeanTextures(dev.Get(), f, &t4, &t1));
    CHECK(t4 && t1);
    if (t4 && t1) {
        D3D11_TEXTURE2D_DESC d4, d1;
        t4->GetDesc(&d4);
        t1->GetDesc(&d1);
        CHECK(d4.Width == 64 && d4.Height == 16 && d4.MipLevels == 7 && d4.Format == DXGI_FORMAT_R16G16B16A16_UNORM);
        CHECK(d1.Format == DXGI_FORMAT_R16_UNORM && d1.MipLevels == 7);
        t4->Release();
        t1->Release();
    }
}

// The install's water .lean files (C5R_GAME_DIR): resolvable by the names Water::Startup uses, the expected header,
// and a mip chain whose smaller mips are averages of the larger ones (checks the mip addressing on real data).
C5R_TEST(game_lean_water_files) {
    const std::string root = c5r::test::gameDir();
    gamefs::GameFiles fs;
    CHECK_OK(fs.open(widen(root)));
    struct Want { const char* name; uint32_t w, h, mips; } wants[] = {
        {kLeanOcean, 512, 512, 10}, {kLeanRiver1, 256, 64, 9}, {kLeanRiver2, 256, 64, 9}};
    for (const Want& w : wants) {
        std::vector<uint8_t> bytes;
        LeanFile f;
        std::string origin;
        Status st = loadLean(fs, w.name, bytes, f);
        CHECK_OK(st);
        if (!st.ok()) continue;
        std::printf("%s: %s, %u x %u, %u mips, %u bytes/channel, +4 = %g, %zu bytes\n", w.name, fs.origin(w.name).c_str(), f.width,
                    f.height, f.mips, f.bytesPerChannel, static_cast<double>(f.unknown04), bytes.size());
        CHECK(f.width == w.w && f.height == w.h && f.mips == w.mips && f.bytesPerChannel == 2 && f.offFour == 0x24);
        CHECK_EQ(bytes.size(), kLeanHeaderSize + f.fourBytes + f.oneBytes);   // nothing after the blocks
        // mip 1 against the 2x2 box average of mip 0, channel 0 of the four-channel block (16-bit)
        LeanMip m0, m1;
        CHECK_OK(leanMip(f, true, 0, m0));
        CHECK_OK(leanMip(f, true, 1, m1));
        double sumErr = 0;
        size_t n = 0;
        for (uint32_t y = 0; y < m1.height; ++y)
            for (uint32_t x = 0; x < m1.width; ++x) {
                auto at = [&](const LeanMip& m, uint32_t xx, uint32_t yy) {
                    uint16_t v;
                    std::memcpy(&v, m.data + yy * m.rowPitch + xx * 8, 2);
                    return static_cast<double>(v);
                };
                const double avg = (at(m0, 2 * x, 2 * y) + at(m0, 2 * x + 1, 2 * y) + at(m0, 2 * x, 2 * y + 1) +
                                    at(m0, 2 * x + 1, 2 * y + 1)) / 4;
                sumErr += std::fabs(at(m1, x, y) - avg);
                ++n;
            }
        const double meanErr = sumErr / static_cast<double>(n);
        std::printf("  mip 1 vs box average of mip 0 (channel 0): mean |d| = %.2f of 65535\n", meanErr);
        CHECK(meanErr < 2000.0);   // a misaddressed mip gives unrelated texels (|d| in the thousands)
    }
    std::vector<uint8_t> bytes;
    LeanFile f;
    CHECK(loadLean(fs, "no_such_file16.lean", bytes, f).code == Err::NotFound);
}
