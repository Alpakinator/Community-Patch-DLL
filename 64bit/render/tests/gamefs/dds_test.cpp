// render/tests/gamefs/dds_test.cpp - DDS parser, conversions and D3D11 creation (task 2901). Synthetic data only;
// devices are WARP (no window, no adapter dependence).
#include <cstdio>
#include "c5r/gamefs/dds.h"
#include "synth.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::gamefs;
using synth::DdsSpec;

namespace {
DdsSpec bc1(uint32_t w, uint32_t h, uint32_t mips) {
    DdsSpec s;
    s.w = w;
    s.h = h;
    s.mips = mips;
    s.fourcc = synth::cc("DXT1");
    s.pixelBytes = synth::chainBytes(w, h, 1, mips, 1, 8, 0);
    return s;
}
DdsSpec rgbMask(uint32_t bits, uint32_t r, uint32_t g, uint32_t b, uint32_t a, uint32_t w = 4, uint32_t h = 4) {
    DdsSpec s;
    s.w = w;
    s.h = h;
    s.pfFlags = 0x40 | (a ? 0x1u : 0u);
    s.bits = bits;
    s.r = r;
    s.g = g;
    s.b = b;
    s.a = a;
    s.pixelBytes = synth::chainBytes(w, h, 1, 1, 1, 0, bits);
    return s;
}
struct Dev {
    ID3D11Device* d = synth::warpDevice();
    ~Dev() { if (d) d->Release(); }
};
bool createOk(ID3D11Device* dev, const DdsImage& img) {
    ID3D11Resource* t = nullptr;
    ID3D11ShaderResourceView* v = nullptr;
    Status st = createTexture(dev, img, &t, &v);
    if (!st.ok()) std::printf("  createTexture: %s\n", st.str().c_str());
    bool ok = st.ok() && t && v;
    if (v) v->Release();
    if (t) t->Release();
    return ok;
}
} // namespace

C5R_TEST(dds_bc1_chain_layout_and_create) {
    std::vector<uint8_t> f = synth::dds(bc1(16, 8, 5));
    DdsImage img;
    CHECK_OK(parseDds(f.data(), f.size(), img));
    CHECK(img.format == DXGI_FORMAT_BC1_UNORM);
    CHECK_EQ(img.mipLevels, 5u);
    CHECK_EQ(img.subresources.size(), size_t(5));
    CHECK_EQ(img.subresources[0].offset, size_t(128));
    CHECK_EQ(img.subresources[0].rowPitch, 32u);  // 4 blocks x 8 bytes
    CHECK_EQ(img.subresources[0].size, size_t(64));
    CHECK_EQ(img.subresources[1].size, size_t(16));  // 8x4: 2 blocks
    CHECK_EQ(img.subresources[4].size, size_t(8));   // 1x1: one block
    CHECK(!img.converted && img.pixels() == f.data());
    Dev dv;
    CHECK(dv.d != nullptr);
    if (dv.d) CHECK(createOk(dv.d, img));
}

C5R_TEST(dds_header_size_24_quirk_accepted) {
    std::vector<uint8_t> f = synth::dds(bc1(8, 8, 1));
    synth::set32(f, synth::kSize, 24);
    DdsImage img;
    CHECK_OK(parseDds(f.data(), f.size(), img));
}

C5R_TEST(dds_direct_mask_formats) {
    struct Case { DdsSpec s; DXGI_FORMAT f; };
    DdsSpec l8;
    l8.pfFlags = 0x20000; l8.bits = 8; l8.r = 0xff; l8.pixelBytes = 16;
    DdsSpec l16 = l8; l16.bits = 16; l16.r = 0xffff; l16.pixelBytes = 32;
    DdsSpec a8l8 = l8; a8l8.pfFlags = 0x20001; a8l8.bits = 16; a8l8.r = 0xff; a8l8.a = 0xff00; a8l8.pixelBytes = 32;
    DdsSpec a8; a8.pfFlags = 0x2; a8.bits = 8; a8.a = 0xff; a8.pixelBytes = 16;
    const Case cases[] = {
        {rgbMask(32, 0xff0000, 0xff00, 0xff, 0xff000000), DXGI_FORMAT_B8G8R8A8_UNORM},
        {rgbMask(32, 0xff0000, 0xff00, 0xff, 0), DXGI_FORMAT_B8G8R8X8_UNORM},
        {rgbMask(32, 0xff, 0xff00, 0xff0000, 0xff000000), DXGI_FORMAT_R8G8B8A8_UNORM},
        {rgbMask(32, 0xffff, 0xffff0000, 0, 0), DXGI_FORMAT_R16G16_UNORM},
        {rgbMask(32, 0x3ff, 0xffc00, 0x3ff00000, 0xc0000000), DXGI_FORMAT_R10G10B10A2_UNORM},
        {l8, DXGI_FORMAT_R8_UNORM}, {l16, DXGI_FORMAT_R16_UNORM}, {a8l8, DXGI_FORMAT_R8G8_UNORM}, {a8, DXGI_FORMAT_A8_UNORM},
    };
    Dev dv;
    for (const Case& c : cases) {
        std::vector<uint8_t> f = synth::dds(c.s);
        DdsImage img;
        CHECK_OK(parseDds(f.data(), f.size(), img));
        CHECK(img.format == c.f);
        CHECK(!img.converted);
        if (dv.d) CHECK(createOk(dv.d, img));
    }
}

C5R_TEST(dds_numeric_fourcc_formats) {
    struct Case { uint32_t n, bpp; DXGI_FORMAT f; };
    const Case cases[] = {{36, 64, DXGI_FORMAT_R16G16B16A16_UNORM}, {110, 64, DXGI_FORMAT_R16G16B16A16_SNORM},
                          {111, 16, DXGI_FORMAT_R16_FLOAT}, {112, 32, DXGI_FORMAT_R16G16_FLOAT},
                          {113, 64, DXGI_FORMAT_R16G16B16A16_FLOAT}, {114, 32, DXGI_FORMAT_R32_FLOAT},
                          {115, 64, DXGI_FORMAT_R32G32_FLOAT}, {116, 128, DXGI_FORMAT_R32G32B32A32_FLOAT}};
    Dev dv;
    for (const Case& c : cases) {
        DdsSpec s;
        s.w = 8; s.h = 4; s.mips = 2; s.fourcc = c.n;
        s.pixelBytes = synth::chainBytes(8, 4, 1, 2, 1, 0, c.bpp);
        std::vector<uint8_t> f = synth::dds(s);
        DdsImage img;
        CHECK_OK(parseDds(f.data(), f.size(), img));
        CHECK(img.format == c.f);
        CHECK_EQ(img.subresources[1].rowPitch, 4 * c.bpp / 8);
        if (dv.d) CHECK(createOk(dv.d, img));
    }
}

C5R_TEST(dds_fourcc_bc_variants) {
    struct Case { const char* cc; DXGI_FORMAT f; uint32_t block; };
    const Case cases[] = {{"DXT2", DXGI_FORMAT_BC2_UNORM, 16}, {"DXT3", DXGI_FORMAT_BC2_UNORM, 16},
                          {"DXT4", DXGI_FORMAT_BC3_UNORM, 16}, {"DXT5", DXGI_FORMAT_BC3_UNORM, 16},
                          {"ATI1", DXGI_FORMAT_BC4_UNORM, 8}, {"BC4U", DXGI_FORMAT_BC4_UNORM, 8},
                          {"BC4S", DXGI_FORMAT_BC4_SNORM, 8}, {"ATI2", DXGI_FORMAT_BC5_UNORM, 16},
                          {"BC5U", DXGI_FORMAT_BC5_UNORM, 16}, {"BC5S", DXGI_FORMAT_BC5_SNORM, 16}};
    Dev dv;
    for (const Case& c : cases) {
        DdsSpec s = bc1(8, 8, 4);
        s.fourcc = synth::cc(c.cc);
        s.pixelBytes = synth::chainBytes(8, 8, 1, 4, 1, c.block, 0);
        std::vector<uint8_t> f = synth::dds(s);
        DdsImage img;
        CHECK_OK(parseDds(f.data(), f.size(), img));
        CHECK(img.format == c.f);
        if (dv.d) CHECK(createOk(dv.d, img));
    }
}

C5R_TEST(dds_conversion_rgb24_and_16bit) {
    // R8G8B8: bytes B,G,R per pixel -> B,G,R,255.
    DdsSpec s = rgbMask(24, 0xff0000, 0xff00, 0xff, 0, 2, 1);
    std::vector<uint8_t> f = synth::dds(s);
    f[128] = 10; f[129] = 20; f[130] = 30; f[131] = 40; f[132] = 50; f[133] = 60;
    DdsImage img;
    CHECK_OK(parseDds(f.data(), f.size(), img));
    CHECK(img.converted && img.format == DXGI_FORMAT_B8G8R8A8_UNORM);
    CHECK_EQ(img.bytes.size(), size_t(8));
    CHECK(img.bytes[0] == 10 && img.bytes[1] == 20 && img.bytes[2] == 30 && img.bytes[3] == 255);
    CHECK(img.bytes[4] == 40 && img.bytes[6] == 60);
    CHECK_EQ(img.subresources[0].rowPitch, 8u);
    // R5G6B5 0xF800 = pure red; A1R5G5B5 0x801F = blue, alpha 1; A4R4G4B4 0x0F0F -> R 0, G 255, B 255? (0x0F0F: A0 R15 G0 B15)
    DdsSpec s2 = rgbMask(16, 0xf800, 0x7e0, 0x1f, 0, 1, 1);
    f = synth::dds(s2);
    f[128] = 0x00; f[129] = 0xF8;
    CHECK_OK(parseDds(f.data(), f.size(), img));
    CHECK(img.bytes[2] == 255 && img.bytes[1] == 0 && img.bytes[0] == 0 && img.bytes[3] == 255);
    DdsSpec s3 = rgbMask(16, 0x7c00, 0x3e0, 0x1f, 0x8000, 1, 1);
    f = synth::dds(s3);
    f[128] = 0x1F; f[129] = 0x80;
    CHECK_OK(parseDds(f.data(), f.size(), img));
    CHECK(img.bytes[0] == 255 && img.bytes[2] == 0 && img.bytes[3] == 255);
    DdsSpec s4 = rgbMask(16, 0xf00, 0xf0, 0xf, 0xf000, 1, 1);
    f = synth::dds(s4);
    f[128] = 0x0F; f[129] = 0x0F;
    CHECK_OK(parseDds(f.data(), f.size(), img));
    CHECK(img.bytes[0] == 255 && img.bytes[1] == 0 && img.bytes[2] == 255 && img.bytes[3] == 0);
    // X8B8G8R8: alpha forced to 255; bytes R,G,B,x -> B,G,R,255.
    DdsSpec s5 = rgbMask(32, 0xff, 0xff00, 0xff0000, 0, 1, 1);
    f = synth::dds(s5);
    f[128] = 1; f[129] = 2; f[130] = 3; f[131] = 7;
    CHECK_OK(parseDds(f.data(), f.size(), img));
    CHECK(img.bytes[0] == 3 && img.bytes[1] == 2 && img.bytes[2] == 1 && img.bytes[3] == 255);
    // Every other converted layout parses.
    const DdsSpec more[] = {rgbMask(16, 0x7c00, 0x3e0, 0x1f, 0), rgbMask(16, 0xf00, 0xf0, 0xf, 0),
                            rgbMask(16, 0xe0, 0x1c, 0x3, 0xff00), rgbMask(8, 0xe0, 0x1c, 0x3, 0)};
    Dev dv;
    for (const DdsSpec& m : more) {
        f = synth::dds(m);
        CHECK_OK(parseDds(f.data(), f.size(), img));
        CHECK(img.converted);
        if (dv.d) CHECK(createOk(dv.d, img));
    }
    DdsSpec a4l4;
    a4l4.pfFlags = 0x20001; a4l4.bits = 8; a4l4.r = 0xf; a4l4.a = 0xf0; a4l4.w = 1; a4l4.h = 1; a4l4.pixelBytes = 1;
    f = synth::dds(a4l4);
    f[128] = 0xF5;  // A 15, L 5 -> L 85
    CHECK_OK(parseDds(f.data(), f.size(), img));
    CHECK(img.bytes[0] == 85 && img.bytes[1] == 85 && img.bytes[2] == 85 && img.bytes[3] == 255);
}

C5R_TEST(dds_cube_layout_and_create) {
    DdsSpec s = bc1(8, 8, 4);
    s.caps2 = 0x200 | 0xFC00;
    s.pixelBytes = synth::chainBytes(8, 8, 1, 4, 6, 8, 0);
    std::vector<uint8_t> f = synth::dds(s);
    DdsImage img;
    CHECK_OK(parseDds(f.data(), f.size(), img));
    CHECK(img.cube);
    CHECK_EQ(img.arraySize, 6u);
    CHECK_EQ(img.subresources.size(), size_t(24));
    // Face 1 mip 0 follows face 0's whole chain (32 + 8 + 8 + 8 = 56 bytes).
    CHECK_EQ(img.subresources[4].offset, size_t(128 + 56));
    Dev dv;
    if (dv.d) CHECK(createOk(dv.d, img));
}

C5R_TEST(dds_volume_layout_and_create) {
    DdsSpec s = rgbMask(32, 0xff0000, 0xff00, 0xff, 0xff000000, 4, 4);
    s.depth = 4;
    s.mips = 3;
    s.caps2 = 0x200000;
    s.pixelBytes = synth::chainBytes(4, 4, 4, 3, 1, 0, 32);
    std::vector<uint8_t> f = synth::dds(s);
    DdsImage img;
    CHECK_OK(parseDds(f.data(), f.size(), img));
    CHECK(img.dim == DdsDim::Tex3D);
    CHECK_EQ(img.depth, 4u);
    CHECK_EQ(img.subresources[0].size, size_t(4 * 4 * 4 * 4));
    CHECK_EQ(img.subresources[1].depth, 2u);
    CHECK_EQ(img.subresources[1].slicePitch, 2u * 2 * 4);
    CHECK_EQ(img.subresources[2].size, size_t(4));
    Dev dv;
    if (dv.d) CHECK(createOk(dv.d, img));
}

C5R_TEST(dds_dx10_arrays_cubes_1d_3d) {
    Dev dv;
    DdsSpec s;
    s.dx10 = true;
    s.dxgi = DXGI_FORMAT_BC3_UNORM;
    s.w = 8; s.h = 8; s.mips = 2; s.arraySize = 3;
    s.pixelBytes = synth::chainBytes(8, 8, 1, 2, 3, 16, 0);
    std::vector<uint8_t> f = synth::dds(s);
    DdsImage img;
    CHECK_OK(parseDds(f.data(), f.size(), img));
    CHECK_EQ(img.arraySize, 3u);
    CHECK_EQ(img.subresources[2].offset, size_t(148 + 64 + 16));
    if (dv.d) CHECK(createOk(dv.d, img));
    s.misc = 0x4; s.arraySize = 2;  // cube array of 2
    s.pixelBytes = synth::chainBytes(8, 8, 1, 2, 12, 16, 0);
    f = synth::dds(s);
    CHECK_OK(parseDds(f.data(), f.size(), img));
    CHECK(img.cube && img.arraySize == 12);
    if (dv.d) CHECK(createOk(dv.d, img));
    DdsSpec t;
    t.dx10 = true; t.dxgi = DXGI_FORMAT_R8G8B8A8_UNORM; t.resDim = 2; t.w = 16; t.h = 1; t.mips = 1;
    t.pixelBytes = 64;
    f = synth::dds(t);
    CHECK_OK(parseDds(f.data(), f.size(), img));
    CHECK(img.dim == DdsDim::Tex1D);
    if (dv.d) CHECK(createOk(dv.d, img));
    t.resDim = 4; t.w = 4; t.h = 4; t.depth = 2; t.pixelBytes = 4 * 4 * 2 * 4;
    f = synth::dds(t);
    CHECK_OK(parseDds(f.data(), f.size(), img));
    CHECK(img.dim == DdsDim::Tex3D && img.depth == 2);
    if (dv.d) CHECK(createOk(dv.d, img));
}

C5R_TEST(negative_dds_truncated_and_bad_magic) {
    std::vector<uint8_t> f = synth::dds(bc1(4, 4, 1));
    DdsImage img;
    CHECK_ERR(parseDds(f.data(), 3, img), Err::Truncated);
    CHECK_ERR(parseDds(nullptr, 0, img), Err::Truncated);
    CHECK_ERR(parseDds(f.data(), 100, img), Err::Truncated);
    f[0] = 'X';
    CHECK_ERR(parseDds(f.data(), f.size(), img), Err::BadMagic);
}

C5R_TEST(negative_dds_header_sizes) {
    std::vector<uint8_t> f = synth::dds(bc1(4, 4, 1));
    synth::set32(f, synth::kSize, 100);
    DdsImage img;
    CHECK_ERR(parseDds(f.data(), f.size(), img), Err::BadLayout);
    f = synth::dds(bc1(4, 4, 1));
    synth::set32(f, synth::kPfSize, 24);
    CHECK_ERR(parseDds(f.data(), f.size(), img), Err::BadLayout);
    DdsSpec s;
    s.dx10 = true; s.dxgi = DXGI_FORMAT_BC1_UNORM;
    f = synth::dds(s);
    CHECK_ERR(parseDds(f.data(), 140, img), Err::Truncated);  // DX10 header cut
}

C5R_TEST(negative_dds_zero_dimensions) {
    DdsImage img;
    std::vector<uint8_t> f = synth::dds(bc1(4, 4, 1));
    synth::set32(f, synth::kWidth, 0);
    CHECK_ERR(parseDds(f.data(), f.size(), img), Err::BadLayout);
    f = synth::dds(bc1(4, 4, 1));
    synth::set32(f, synth::kHeight, 0);
    CHECK_ERR(parseDds(f.data(), f.size(), img), Err::BadLayout);
    DdsSpec v = rgbMask(32, 0xff0000, 0xff00, 0xff, 0xff000000);
    v.caps2 = 0x200000;
    v.depth = 0;
    f = synth::dds(v);
    CHECK_ERR(parseDds(f.data(), f.size(), img), Err::BadLayout);
    DdsSpec d;
    d.dx10 = true; d.dxgi = DXGI_FORMAT_BC1_UNORM; d.arraySize = 0; d.pixelBytes = 8;
    f = synth::dds(d);
    CHECK_ERR(parseDds(f.data(), f.size(), img), Err::BadLayout);
}

C5R_TEST(negative_dds_unsupported_formats) {
    DdsImage img;
    DdsSpec s = bc1(4, 4, 1);
    s.fourcc = synth::cc("YUY2");
    std::vector<uint8_t> f = synth::dds(s);
    CHECK_ERR(parseDds(f.data(), f.size(), img), Err::Unsupported);
    s.fourcc = 42;  // a numeric D3DFMT outside the table
    f = synth::dds(s);
    CHECK_ERR(parseDds(f.data(), f.size(), img), Err::Unsupported);
    f = synth::dds(rgbMask(32, 0xff, 0xff, 0xff, 0));  // nonsense masks
    CHECK_ERR(parseDds(f.data(), f.size(), img), Err::Unsupported);
    DdsSpec bump;
    bump.pfFlags = 0x80000; bump.bits = 16; bump.r = 0xff; bump.g = 0xff00; bump.pixelBytes = 32;
    f = synth::dds(bump);
    CHECK_ERR(parseDds(f.data(), f.size(), img), Err::Unsupported);
    DdsSpec d;
    d.dx10 = true; d.dxgi = 999; d.pixelBytes = 64;
    f = synth::dds(d);
    CHECK_ERR(parseDds(f.data(), f.size(), img), Err::Unsupported);
    DdsSpec lum;
    lum.pfFlags = 0x20000; lum.bits = 32; lum.r = 0xffffffff; lum.pixelBytes = 64;
    f = synth::dds(lum);
    CHECK_ERR(parseDds(f.data(), f.size(), img), Err::Unsupported);
}

C5R_TEST(negative_dds_incomplete_cube_and_volume_cube) {
    DdsImage img;
    DdsSpec s = bc1(4, 4, 1);
    s.caps2 = 0x200 | 0x400 | 0x800;  // +X and -X only
    s.pixelBytes = 16;
    std::vector<uint8_t> f = synth::dds(s);
    CHECK_ERR(parseDds(f.data(), f.size(), img), Err::Unsupported);
    s.caps2 = 0x200 | 0xFC00 | 0x200000;
    s.depth = 2;
    f = synth::dds(s);
    CHECK_ERR(parseDds(f.data(), f.size(), img), Err::BadLayout);
}

C5R_TEST(negative_dds_dx10_layout_errors) {
    DdsImage img;
    DdsSpec d;
    d.dx10 = true; d.dxgi = DXGI_FORMAT_R8G8B8A8_UNORM; d.pixelBytes = 4096;
    d.resDim = 5;
    std::vector<uint8_t> f = synth::dds(d);
    CHECK_ERR(parseDds(f.data(), f.size(), img), Err::BadLayout);  // unknown dimension
    d.resDim = 4; d.arraySize = 2; d.depth = 2;
    f = synth::dds(d);
    CHECK_ERR(parseDds(f.data(), f.size(), img), Err::BadLayout);  // 3D array
    d.arraySize = 1; d.misc = 0x4;
    f = synth::dds(d);
    CHECK_ERR(parseDds(f.data(), f.size(), img), Err::BadLayout);  // 3D cube
    d.resDim = 2; d.misc = 0; d.h = 2;
    f = synth::dds(d);
    CHECK_ERR(parseDds(f.data(), f.size(), img), Err::BadLayout);  // 1D with height 2
    d.resDim = 3; d.misc = 0x4; d.h = 4; d.arraySize = 400;
    f = synth::dds(d);
    CHECK_ERR(parseDds(f.data(), f.size(), img), Err::Unsupported);  // cube array above 2048 slices
}

C5R_TEST(negative_dds_oversized) {
    DdsImage img;
    std::vector<uint8_t> f = synth::dds(bc1(4, 4, 1));
    synth::set32(f, synth::kWidth, 32768);
    CHECK_ERR(parseDds(f.data(), f.size(), img), Err::Unsupported);
    DdsSpec v = rgbMask(32, 0xff0000, 0xff00, 0xff, 0xff000000);
    v.caps2 = 0x200000;
    v.depth = 4096;
    f = synth::dds(v);
    CHECK_ERR(parseDds(f.data(), f.size(), img), Err::Unsupported);
}

C5R_TEST(negative_dds_inconsistent_mip_count) {
    DdsImage img;
    DdsSpec s = bc1(8, 8, 4);
    s.mips = 5;  // 8x8 has at most 4 levels
    s.pixelBytes = 4096;
    std::vector<uint8_t> f = synth::dds(s);
    CHECK_ERR(parseDds(f.data(), f.size(), img), Err::BadLayout);
}

C5R_TEST(negative_dds_bc_not_multiple_of_4) {
    DdsImage img;
    DdsSpec s = bc1(8, 8, 1);
    s.w = 6;
    s.pixelBytes = 4096;
    std::vector<uint8_t> f = synth::dds(s);
    CHECK_ERR(parseDds(f.data(), f.size(), img), Err::Unsupported);
}

C5R_TEST(negative_dds_truncated_pixels) {
    DdsImage img;
    std::vector<uint8_t> f = synth::dds(bc1(16, 16, 5));
    CHECK_OK(parseDds(f.data(), f.size(), img));
    CHECK_ERR(parseDds(f.data(), f.size() - 1, img), Err::Truncated);
    f = synth::dds(rgbMask(24, 0xff0000, 0xff00, 0xff, 0));  // converted path
    CHECK_ERR(parseDds(f.data(), f.size() - 1, img), Err::Truncated);
    std::vector<uint8_t> g = synth::dds(bc1(4, 4, 1));
    g.push_back(0xAA);  // trailing bytes are allowed
    CHECK_OK(parseDds(g.data(), g.size(), img));
}

C5R_TEST(negative_create_texture_errors) {
    std::vector<uint8_t> f = synth::dds(bc1(4, 4, 1));
    DdsImage img;
    CHECK_OK(parseDds(f.data(), f.size(), img));
    ID3D11Resource* t = nullptr;
    CHECK_ERR(createTexture(nullptr, img, &t, nullptr), Err::Device);
    Dev dv;
    if (!dv.d) return;
    DdsImage broken = img;
    broken.subresources.clear();
    CHECK_ERR(createTexture(dv.d, broken, &t, nullptr), Err::BadLayout);
    DdsImage typeless = img;
    typeless.format = DXGI_FORMAT_UNKNOWN;  // the format-support query fails
    CHECK_ERR(createTexture(dv.d, typeless, &t, nullptr), Err::Device);
    CHECK(t == nullptr);
}

// Records what D3D11 does with a BC texture whose top level is not a multiple of 4 (the reason parseDds refuses it).
C5R_TEST(device_rejects_bc_not_multiple_of_4) {
    Dev dv;
    if (!dv.d) return;
    std::vector<uint8_t> data(64 * 64, 0);
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = 6;
    d.Height = 6;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_BC1_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd = {data.data(), 16, 32};
    ID3D11Texture2D* t = nullptr;
    HRESULT h = dv.d->CreateTexture2D(&d, &sd, &t);
    std::printf("  WARP CreateTexture2D BC1 6x6: 0x%08X\n", static_cast<unsigned>(h));
    if (t) t->Release();
    CHECK(FAILED(h));
}

C5R_TEST_MAIN
