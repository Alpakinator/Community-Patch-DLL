// Tests of modelshot (issue 3103): DDS reader (positive and negative), bone matrices, vertex reads, downscale, and
// the rendering self-test on the synthetic cube. The rendering part runs on a thread attached to a desktop this
// test creates (COMMON rule 14: nothing that touches the graphics stack runs on the interactive desktop).
// The tool's sources are compiled into this test directly (the generic tests/ rule links c5models only).
#include "testing.h"

#include "../tools/modelshot/dds.cpp"
#include "../tools/modelshot/image.cpp"
#include "../tools/modelshot/render.cpp"
#include "../tools/modelshot/scene.cpp"
#include "../tools/modelshot/selftest.cpp"

#include <cmath>
#include <cstring>
#include <thread>

namespace {

std::vector<uint8_t> dds_header(uint32_t w, uint32_t h, uint32_t pf_flags, const char* cc, uint32_t bits,
                                uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    std::vector<uint8_t> d(128, 0);
    auto w32 = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; ++i) d[o + size_t(i)] = uint8_t(v >> (8 * i)); };
    std::memcpy(d.data(), "DDS ", 4);
    w32(4, 124);
    w32(8, 0x1007);
    w32(12, h);
    w32(16, w);
    w32(76, 32);
    w32(80, pf_flags);
    if (cc) std::memcpy(&d[84], cc, 4);
    w32(88, bits);
    w32(92, r);
    w32(96, g);
    w32(100, b);
    w32(104, a);
    return d;
}

bool contains(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

void test_dds() {
    shot::DdsImage im;
    std::string err;
    // positive: A8R8G8B8 2x2
    auto d = dds_header(2, 2, 0x41, nullptr, 32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000u);
    uint8_t px[16] = {0x10, 0x20, 0x30, 0x40, 0, 0, 0xFF, 0xFF, 0, 0xFF, 0, 0xFF, 0xFF, 0, 0, 0xFF};   // BGRA in memory
    d.insert(d.end(), px, px + 16);
    CHECK(shot::parse_dds(d.data(), d.size(), im, err));
    CHECK(im.format == shot::TexFormat::RGBA8 && im.width == 2 && im.levels.size() == 1);
    CHECK(im.levels[0][0] == 0x30 && im.levels[0][1] == 0x20 && im.levels[0][2] == 0x10 && im.levels[0][3] == 0x40);
    CHECK(im.levels[0][4] == 0xFF && im.levels[0][5] == 0 && im.levels[0][6] == 0);   // red
    CHECK(im.has_alpha);                                                               // 0x40 < 128
    // positive: R5G6B5 1x1 pure green, L8 1x1
    d = dds_header(1, 1, 0x40, nullptr, 16, 0xF800, 0x7E0, 0x1F, 0);
    d.push_back(0xE0), d.push_back(0x07);
    CHECK(shot::parse_dds(d.data(), d.size(), im, err));
    CHECK(im.levels[0][0] == 0 && im.levels[0][1] == 255 && im.levels[0][2] == 0 && im.levels[0][3] == 255 && !im.has_alpha);
    d = dds_header(1, 1, 0x20000, nullptr, 8, 0xFF, 0, 0, 0);
    d.push_back(0x80);
    CHECK(shot::parse_dds(d.data(), d.size(), im, err));
    CHECK(im.levels[0][0] == 0x80 && im.levels[0][1] == 0x80 && im.levels[0][2] == 0x80 && im.levels[0][3] == 255);
    // positive: DXT5 4x4 with 2 mips declared but only one present -> mip 0 kept
    d = dds_header(4, 4, 0x4, "DXT5", 0, 0, 0, 0, 0);
    d[10] |= 0x02;               // DDSD_MIPMAPCOUNT (0x20000)
    d[28] = 2;
    std::vector<uint8_t> blk(16, 0);
    blk[0] = 255, blk[1] = 255;    // alpha endpoints 255: opaque
    d.insert(d.end(), blk.begin(), blk.end());
    CHECK(shot::parse_dds(d.data(), d.size(), im, err));
    CHECK(im.format == shot::TexFormat::BC3 && im.levels.size() == 1 && !im.has_alpha);

    // negative: truncated BC1 payload (8x8 needs 32 bytes, 20 given)
    d = dds_header(8, 8, 0x4, "DXT1", 0, 0, 0, 0, 0);
    d.resize(d.size() + 20);
    CHECK(!shot::parse_dds(d.data(), d.size(), im, err));
    CHECK(contains(err, "truncated payload"));
    std::printf("truncated BC1 -> %s\n", err.c_str());
    // negative: truncated uncompressed payload
    d = dds_header(4, 4, 0x41, nullptr, 32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000u);
    d.resize(d.size() + 63);
    CHECK(!shot::parse_dds(d.data(), d.size(), im, err));
    CHECK(contains(err, "truncated payload"));
    // negative: unknown FourCC
    d = dds_header(4, 4, 0x4, "ABCD", 0, 0, 0, 0, 0);
    d.resize(d.size() + 64);
    CHECK(!shot::parse_dds(d.data(), d.size(), im, err));
    CHECK(contains(err, "unsupported DDS format") && contains(err, "'ABCD'"));
    std::printf("unknown FourCC -> %s\n", err.c_str());
    // negative: unknown DX10 format
    d = dds_header(4, 4, 0x4, "DX10", 0, 0, 0, 0, 0);
    d.resize(148 + 64);
    d[128] = 2;   // DXGI_FORMAT_R32G32B32A32_FLOAT
    CHECK(!shot::parse_dds(d.data(), d.size(), im, err));
    CHECK(contains(err, "DXGI_FORMAT 2"));
    // negative: bad magic, short header, header size field, zero size, BC top level not a multiple of 4
    d = dds_header(4, 4, 0x4, "DXT1", 0, 0, 0, 0, 0);
    d[0] = 'X';
    CHECK(!shot::parse_dds(d.data(), d.size(), im, err) && contains(err, "magic"));
    CHECK(!shot::parse_dds(d.data(), 100, im, err) && contains(err, "truncated"));
    d = dds_header(4, 4, 0x4, "DXT1", 0, 0, 0, 0, 0);
    d[4] = 100;
    CHECK(!shot::parse_dds(d.data(), d.size(), im, err) && contains(err, "header size"));
    d = dds_header(0, 4, 0x4, "DXT1", 0, 0, 0, 0, 0);
    CHECK(!shot::parse_dds(d.data(), d.size(), im, err) && contains(err, "out of range"));
    d = dds_header(6, 4, 0x4, "DXT1", 0, 0, 0, 0, 0);
    d.resize(d.size() + 64);
    CHECK(!shot::parse_dds(d.data(), d.size(), im, err) && contains(err, "multiple of 4"));
    // negative: unsupported bit depth
    d = dds_header(1, 1, 0x40, nullptr, 12, 0xF00, 0xF0, 0xF, 0);
    d.resize(d.size() + 4);
    CHECK(!shot::parse_dds(d.data(), d.size(), im, err) && contains(err, "bits per pixel"));
}

bool approx(float a, float b) { return std::fabs(a - b) < 1e-4f; }

void test_matrices() {
    // parent: translate (1,0,0); child: rotate 90 deg about Z, translate (0,1,0).
    c5m::Skeleton sk;
    c5m::Bone p, c;
    p.name = "p";
    p.local.flags = 1;
    p.local.translation[0] = 1;
    c.name = "c";
    c.parent = 0;
    c.local.flags = 3;
    c.local.translation[1] = 1;
    float s = std::sqrt(0.5f);
    c.local.rotation[2] = s, c.local.rotation[3] = s;
    sk.bones.push_back(p);
    sk.bones.push_back(c);
    auto W = shot::world_matrices(sk, shot::identity());
    float in[3] = {1, 0, 0}, out[3];
    shot::xform_point(W[1], in, out);   // (1,0,0) -> rotate (0,1,0) -> +(0,1,0) -> +(1,0,0) = (1,2,0)
    CHECK(approx(out[0], 1) && approx(out[1], 2) && approx(out[2], 0));
    // flags without "has orientation": the rotation is ignored
    sk.bones[1].local.flags = 1;
    W = shot::world_matrices(sk, shot::identity());
    shot::xform_point(W[1], in, out);   // (1,0,0) + (0,1,0) + (1,0,0) = (2,1,0)
    CHECK(approx(out[0], 2) && approx(out[1], 1) && approx(out[2], 0));
    // scale-shear applied before the rotation: scale x by 2, then rotate 90 about Z -> (0,2,0)
    c5m::Transform t;
    t.flags = 6;
    t.rotation[2] = s, t.rotation[3] = s;
    t.scale_shear[0] = 2;
    shot::xform_point(shot::from_transform(t), in, out);
    CHECK(approx(out[0], 0) && approx(out[1], 2) && approx(out[2], 0));
    // inverse world x world = identity for a consistent skeleton
    shot::Mat4 w = shot::from_transform(t);
    shot::Mat4 inv = shot::identity();
    inv.m[0] = 0, inv.m[1] = -1, inv.m[4] = 0.5f, inv.m[5] = 0;   // inverse of (scale x2, rotate 90 deg)
    CHECK(shot::max_dev_from_identity3(shot::mul(inv, w)) < 1e-5f);
    // a parent cycle does not hang
    sk.bones[0].parent = 1;
    W = shot::world_matrices(sk, shot::identity());
    CHECK(W.size() == 2);
}

void test_vertices_and_downscale() {
    c5m::Mesh m;
    c5m::VertexComponent a, b;
    a.type = c5m::MemberType::Real32, a.name = "Position", a.count = 3, a.offset = 0;
    b.type = c5m::MemberType::NormalUInt8, b.name = "BoneWeights", b.count = 4, b.offset = 12;
    m.format.components = {a, b};
    m.format.stride = 16;
    m.vertex_count = 1;
    m.vertices.resize(16);
    float p[3] = {1.5f, -2, 3};
    std::memcpy(m.vertices.data(), p, 12);
    m.vertices[12] = 255, m.vertices[13] = 0, m.vertices[14] = 51, m.vertices[15] = 0;
    float o[4];
    CHECK(shot::read_component(m, 0, "Position", o) == 3 && o[0] == 1.5f && o[1] == -2 && o[2] == 3);
    CHECK(shot::read_component(m, 0, "BoneWeights", o) == 4 && approx(o[0], 1) && approx(o[2], 0.2f));
    CHECK(shot::read_component(m, 0, "Normal", o) == 0);
    CHECK(shot::read_component(m, 1, "Position", o) == 0);   // out of range vertex
    m.vertices.resize(8);                                     // declared stride larger than the bytes: refused
    CHECK(shot::read_component(m, 0, "Position", o) == 0);

    shot::Image im;
    im.w = im.h = 4;
    im.rgba.assign(64, 0);
    for (uint32_t y = 0; y < 2; ++y)
        for (uint32_t x = 0; x < 2; ++x) im.rgba[(y * 4 + x) * 4] = uint8_t(x == 0 && y == 0 ? 200 : 100);
    shot::Image sm = shot::downscale_box(im, 2);
    CHECK(sm.w == 2 && sm.rgba[0] == 125 && sm.rgba[4] == 0);   // (200+100+100+100)/4 = 125
}

void test_render_on_hidden_desktop() {
    wchar_t name[64];
    std::swprintf(name, 64, L"C5MS_test_%lu", GetCurrentProcessId());
    HDESK desk = CreateDesktopW(name, nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    CHECK(desk != nullptr);
    if (!desk) return;
    int fails = -1;
    std::string log;
    bool attached = false;
    std::thread th([&] {
        attached = SetThreadDesktop(desk) != 0;
        if (attached) fails = shot::self_test(false, log);
    });
    th.join();
    CloseDesktop(desk);
    std::fputs(log.c_str(), stdout);
    CHECK(attached);
    CHECK_EQ(fails, 0);
}

}  // namespace

int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    test_dds();
    test_matrices();
    test_vertices_and_downscale();
    test_render_on_hidden_desktop();
    return t::finish();
}
