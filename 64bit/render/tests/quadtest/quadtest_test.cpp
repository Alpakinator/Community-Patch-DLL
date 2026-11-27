// render/tests/quadtest/quadtest_test.cpp (task 2906).
#include <cmath>
#include <cstring>
#include "testkit.h"
#include "quadtest_core.h"

using namespace c5r;
using namespace c5r::quadtest;

namespace {
std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

// A 2x2 RGBA8 texture made in the test (red, green, blue, white).
ID3D11ShaderResourceView* synthTexture(Device& d) {
    const uint8_t px[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = 2; td.Height = 2; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd = {px, 8, 0};
    ID3D11Texture2D* t = nullptr; ID3D11ShaderResourceView* v = nullptr;
    if (FAILED(d.dev->CreateTexture2D(&td, &sd, &t))) return nullptr;
    d.dev->CreateShaderResourceView(t, nullptr, &v);
    t->Release();
    return v;
}

const terrainsets::CbLayout kTestLayout = {"TestLayout", -1, 0x50, "", {
    {"Transform", 0x00, terrainsets::MemberType::Float4x4, 1, "", terrainsets::Conf::Inferred},
    {"Vec", 0x40, terrainsets::MemberType::Float4, 1, "", terrainsets::Conf::Inferred}}};
const terrainsets::CbLayout kShortLayout = {"Short", -1, 0x30, "", {
    {"Transform", 0x00, terrainsets::MemberType::Float4x4, 1, "", terrainsets::Conf::Inferred}}};
const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
} // namespace

C5R_TEST(write_matrix_by_name) {
    std::vector<uint8_t> cb;
    CHECK_OK(writeMatrix(kTestLayout, "Transform", kIdentity, cb));
    CHECK_EQ(cb.size(), size_t(0x50));
    float f[16];
    std::memcpy(f, cb.data(), 64);
    CHECK(f[0] == 1.0f && f[5] == 1.0f && f[1] == 0.0f);
}

C5R_TEST(negative_write_matrix_missing_member) {
    std::vector<uint8_t> cb;
    CHECK_ERR(writeMatrix(kTestLayout, "World", kIdentity, cb), Err::NotFound);
}

C5R_TEST(negative_write_matrix_wrong_type) {
    std::vector<uint8_t> cb;
    CHECK_ERR(writeMatrix(kTestLayout, "Vec", kIdentity, cb), Err::BadLayout);
}

C5R_TEST(negative_write_matrix_past_end) {
    std::vector<uint8_t> cb;
    CHECK_ERR(writeMatrix(kShortLayout, "Transform", kIdentity, cb), Err::BadLayout);
}

C5R_TEST(negative_compare_size_mismatch) {
    Image a, b; a.width = a.height = 1; a.rgba.assign(4, 0); b.width = 2; b.height = 1; b.rgba.assign(8, 0);
    Diff d;
    CHECK_ERR(compare(a, b, d), Err::BadLayout);
}

C5R_TEST(negative_find_pass_unknown_set) {
    shaders::ShaderSection sec; // empty: the set lookup fails first
    GamePass gp;
    CHECK_ERR(findPass(sec, "NoSuchShader", 0, gp), Err::NotFound);
}

C5R_TEST(negative_find_pass_missing_pass) {
    shaders::ShaderSection sec;
    GamePass gp;
    CHECK_ERR(findPass(sec, "CompositeShader", 99, gp), Err::NotFound);
}

C5R_TEST(negative_find_pass_blob_not_in_section) {
    shaders::ShaderSection sec; // no blobs: the VAs of the set are not found
    GamePass gp;
    CHECK_ERR(findPass(sec, "CompositeShader", 0, gp), Err::NotFound);
}

C5R_TEST(reference_draw_and_negative_draw_checks) {
    Device d;
    if (!d.create()) c5r::test::skip("no D3D11 device");
    ID3D11ShaderResourceView* srv = synthTexture(d);
    CHECK(srv != nullptr);
    DrawParams p; p.width = p.height = 64;
    Image img;
    CHECK_OK(drawReference(d, srv, p, img));
    CHECK_EQ(img.rgba.size(), size_t(64 * 64 * 4));
    CHECK(img.rgba[(32 * 64 + 32) * 4 + 3] == 255);  // centre covered
    CHECK(img.rgba[(1 * 64 + 1) * 4 + 3] == 0);      // corner outside the quad
    CHECK_ERR(drawReference(d, nullptr, p, img), Err::BadLayout);
    DrawParams bad = p; bad.samplerStateId = 9999;
    CHECK_ERR(drawReference(d, srv, bad, img), Err::NotFound);
    if (srv) srv->Release();
}

C5R_TEST(negative_texture_not_found) {
    std::string game = c5r::test::gameDir();
    Device d;
    if (!d.create()) c5r::test::skip("no D3D11 device");
    ID3D11ShaderResourceView* srv = nullptr;
    CHECK_ERR(loadGameTexture(d, widen(game), "no_such_texture_2906.dds", &srv, nullptr, nullptr), Err::NotFound);
}

// The deliverable: a game texture drawn by the game's CompositeShader pass 0, equal to the reference, and moved by
// the transform written into the constant buffer through the recovered layout.
C5R_TEST(game_texture_with_game_shader) {
    std::string game = c5r::test::gameDir();
    std::string exe = c5r::test::exePath();
    Device d;
    CHECK_OK(d.create());
    shaders::ShaderSection sec;
    CHECK_OK(sec.open(widen(exe)));
    GamePass gp;
    CHECK_OK(findPass(sec, "CompositeShader", 0, gp));
    ID3D11ShaderResourceView* srv = nullptr;
    std::string origin;
    CHECK_OK(loadGameTexture(d, widen(game), "euro_grassland_d.dds", &srv, &origin, nullptr));
    if (!srv) return;
    DrawParams id, rot;
    const float c = std::cos(0.5f) * 0.8f, s = std::sin(0.5f) * 0.8f;
    const float m[16] = {c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, 0.1f, 0, 0, 1};
    std::memcpy(rot.transform, m, sizeof m);
    Image gi, ri, gr, rr;
    CHECK_OK(drawGame(d, gp, srv, id, gi));
    CHECK_OK(drawReference(d, srv, id, ri));
    CHECK_OK(drawGame(d, gp, srv, rot, gr));
    CHECK_OK(drawReference(d, srv, rot, rr));
    srv->Release();
    Diff a, b, mv;
    CHECK_OK(compare(gi, ri, a));
    CHECK_OK(compare(gr, rr, b));
    CHECK_OK(compare(gi, gr, mv));
    CHECK(a.maxAbs <= 2);
    CHECK(b.maxAbs <= 2);
    CHECK(mv.meanAbs > 5.0);
    CHECK(a.covered > 256 * 256 / 2);
    std::printf("identity: covered %u, max %u; rotated: max %u; moved mean %.2f; texture from %s\n", a.covered, a.maxAbs,
                b.maxAbs, mv.meanAbs, origin.c_str());
}

C5R_TEST_MAIN
