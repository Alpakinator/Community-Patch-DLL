// c5r::water reflection camera tests (issue 0382; water-system.md 13.6). The rule of 0x615950 (second half) against
// hand-built views and against four stock captures read at run time (never committed, D83): task 4301's two Tiny loads
// (the reflected ice draws' IceFrame rows 0-3 and 12 against the scene draws' rows 0-3; C5R_ICE_REF, default
// F:/c5-64bit-work/0169/4301/ref) and task 3601's two cameras (WaterFrame 0x000 against 0x160; C5R_REFL_REF, default
// F:/c5-64bit-work/0382/ref, made by render/evidence/0382/make_ref.py, manifest refl_ref_manifest.json). A missing set
// fails the test naming its variable (never skipped). The draw tests use the EXE's WaterShader programs (C5R_EXE).
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "c5r/water/water.h"
#include "testkit.h"
#include "water_util.h"

using namespace c5r;
using namespace c5r::water;

namespace {

std::wstring wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? static_cast<size_t>(n) : 0);
    const bool ok = out.empty() || std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

// The reference file `rel` of the set named by env (default def); fails (not skips) when it is not there.
bool readRef(const char* env, const char* def, const std::string& rel, size_t size, std::vector<uint8_t>& out) {
    const char* e = std::getenv(env);
    const std::string d = e && *e ? e : def;
    if (!readFile(d + "/" + rel, out) || out.size() != size) {
        test::fail(__FILE__, __LINE__, std::string("reference ") + d + "/" + rel + " missing or not " + std::to_string(size) +
                                           " bytes: set " + env + " to the set's folder (D137; never skipped)");
        return false;
    }
    return true;
}

void mat(const std::vector<uint8_t>& b, size_t off, float m[16]) { std::memcpy(m, b.data() + off, 64); }

// largest |a - b| / max(|b|, 1e-3) over 16 elements
double maxRel(const float* a, const float* b, int n = 16) {
    double m = 0;
    for (int i = 0; i < n; ++i) m = std::max(m, std::fabs(static_cast<double>(a[i]) - b[i]) / std::max(std::fabs(static_cast<double>(b[i])), 1e-3));
    return m;
}

void xform(const float m[16], const float p[3], float out[4]) {
    for (int j = 0; j < 4; ++j) out[j] = p[0] * m[j] + p[1] * m[4 + j] + p[2] * m[8 + j] + m[12 + j];
}

void mulF(const float* a, const float* b, float* out) {   // left to right in float, as the module
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = a[i * 4 + 0] * b[0 * 4 + j];
            s += a[i * 4 + 1] * b[1 * 4 + j];
            s += a[i * 4 + 2] * b[2 * 4 + j];
            s += a[i * 4 + 3] * b[3 * 4 + j];
            out[i * 4 + j] = s;
        }
}

} // namespace

C5R_TEST(reflection_direction_is_the_exe_angles) {
    float d[3];
    reflectionDirection(d);
    const double pi = 3.14159265358979323846;
    const double e = 80.0 * pi / 180.0, f = 135.0 * pi / 180.0;
    CHECK_EQ(d[0], static_cast<float>(-std::cos(e) * std::sin(f)));
    CHECK_EQ(d[1], static_cast<float>(-std::sin(e) * std::sin(f)));
    CHECK_EQ(d[2], static_cast<float>(std::cos(f)));
    CHECK(std::fabs(d[0] * d[0] + d[1] * d[1] + d[2] * d[2] - 1.0f) < 1e-6f);
    CHECK(d[2] < 0 && d[1] < 0 && d[0] < 0);   // down, towards -y and slightly -x: the eye sits above, north-east
}

// An orthographic straight-down scene view whose footprint is the rectangle [50, 150] x [-45, 5] at every height.
C5R_TEST(reflection_camera_of_an_orthographic_down_view) {
    const float vp[16] = {1.0f / 50, 0, 0, 0, 0, 1.0f / 25, 0, 0, 0, 0, 0.001f, 0, -2.0f, 0.8f, 0.5f, 1};
    ReflectionCamera rc;
    CHECK_OK(reflectionCamera(vp, rc));
    auto closeTo = [](float a, float b) { return std::fabs(a - b) <= 1e-4f * std::max(1.0f, std::fabs(b)); };
    CHECK(closeTo(rc.center[0], 100.0f));
    CHECK(closeTo(rc.center[1], -20.0f));
    CHECK_EQ(rc.center[2], 0.0f);
    CHECK(closeTo(rc.halfX, 50.0f));
    CHECK(closeTo(rc.halfY, 25.0f));
    float d[3];
    reflectionDirection(d);
    for (int i = 0; i < 3; ++i) CHECK(std::fabs(rc.eye[i] - (rc.center[i] - 5000.0f * d[i])) < 1e-3f);
    // the projection: OrthoOffCenterRH(-50, 50, -25, 25, 30000, -30000)
    CHECK_EQ(rc.proj[0], 1.0f / rc.halfX);
    CHECK_EQ(rc.proj[5], 1.0f / rc.halfY);
    CHECK(closeTo(rc.proj[0], 1.0f / 50));
    CHECK(closeTo(rc.proj[5], 1.0f / 25));
    CHECK_EQ(rc.proj[10], 1.0f / 60000);
    CHECK_EQ(rc.proj[12], 0.0f);   // symmetric window: -1 - 2l / (r - l) = 0 exactly
    CHECK_EQ(rc.proj[13], 0.0f);
    CHECK_EQ(rc.proj[14], 0.5f);
    CHECK_EQ(rc.proj[15], 1.0f);
    // the view: orthonormal rows, the camera's z axis = -D, the eye at the origin of view space
    for (int r = 0; r < 3; ++r) {
        const float n = rc.view[r] * rc.view[r] + rc.view[4 + r] * rc.view[4 + r] + rc.view[8 + r] * rc.view[8 + r];
        CHECK(std::fabs(n - 1.0f) < 1e-5f);
    }
    for (int i = 0; i < 3; ++i) CHECK(std::fabs(rc.view[4 * i + 2] + d[i]) < 1e-6f);
    float e[4];
    xform(rc.view, rc.eye, e);
    CHECK(std::fabs(e[0]) < 1e-2f && std::fabs(e[1]) < 1e-2f && std::fabs(e[2]) < 1e-2f);
    // viewProj = view x proj; the center at clip (0, 0, 5000 / 60000 + 0.5); every point along D lands on the same clip xy
    float vp2[16];
    mulF(rc.view, rc.proj, vp2);
    CHECK(std::memcmp(vp2, rc.viewProj, 64) == 0);
    float c[4];
    xform(rc.viewProj, rc.center, c);
    std::printf("  centre at clip (%.3g %.3g %.7g %.3g)\n", c[0], c[1], c[2], c[3]);
    CHECK(std::fabs(c[0]) < 1e-4f && std::fabs(c[1]) < 1e-4f && std::fabs(c[2] - (0.5f - 5000.0f / 60000)) < 1e-5f);
    CHECK_EQ(c[3], 1.0f);
    const float p[3] = {120, -30, 0};
    const float q[3] = {120 + 300 * d[0], -30 + 300 * d[1], 300 * d[2]};
    float a[4], b[4];
    xform(rc.viewProj, p, a);
    xform(rc.viewProj, q, b);
    CHECK(std::fabs(a[0] - b[0]) < 1e-5f && std::fabs(a[1] - b[1]) < 1e-5f);
    // zn 30000 > zf -30000: depth falls with the distance from the eye, so under LESS_EQUAL the surface furthest along D
    // (the one seen from below the water) wins: the reflection's "mirror" is this reversed depth
    CHECK(b[2] < a[2]);
}

// A perspective camera straight down at (100, -50, 600): the footprint is symmetric about it.
C5R_TEST(reflection_camera_of_a_perspective_down_view) {
    const terrain::Camera cam = wtest::downCamera(100, -50, 600, 320, 200);
    float vp[16];
    mulF(cam.view, cam.proj, vp);
    ReflectionCamera rc;
    CHECK_OK(reflectionCamera(vp, rc));
    CHECK(std::fabs(rc.center[0] - 100.0f) < 1e-3f);
    CHECK(std::fabs(rc.center[1] + 50.0f) < 1e-3f);
    CHECK(rc.halfX > rc.halfY && rc.halfY > 0);   // 320 x 200: wider than high
    // the footprint at z 0 lies inside the reflection's window (|clip x|, |clip y| <= 1) at its corners
    Footprint fp;
    World w;
    w.minX = w.minY = -1e6f;
    w.maxX = w.maxY = 1e6f;
    w.width = w.height = 2e6f;
    w.samplesX = w.samplesY = 64;
    w.valid = true;
    CHECK_OK(viewFootprint(vp, w, fp));   // the clamp does not bite: the same rectangle
    CHECK(std::fabs((fp.minX + fp.maxX) * 0.5f - rc.center[0]) < 1e-3f);
    CHECK(std::fabs((fp.maxX - fp.minX) * 0.5f - rc.halfX) < 1e-3f);
}

C5R_TEST(reflection_camera_rejects) {
    ReflectionCamera rc;
    rc.halfX = 7;
    CHECK_ERR(reflectionCamera(nullptr, rc), Err::BadLayout);
    CHECK_EQ(rc.halfX, 0.0f);   // reset on failure
    float vp[16] = {1.0f / 50, 0, 0, 0, 0, 1.0f / 25, 0, 0, 0, 0, 0.001f, 0, -2.0f, 0.8f, 0.5f, 1};
    vp[5] = NAN;
    CHECK_ERR(reflectionCamera(vp, rc), Err::BadLayout);
    // clip x and y depend on z only: every edge line is parallel to the ground (no footprint point)
    const float flat[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 0.5f, 0.001f, 0, 0, 0, 0.5f, 1};
    CHECK_ERR(reflectionCamera(flat, rc), Err::BadLayout);
    // no w (column 3 zero): planes 0 and 1 meet at x = 0, planes 2 and 3 at y = 0: a footprint of zero size
    const float zero[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0};
    CHECK_ERR(reflectionCamera(zero, rc), Err::BadLayout);
    // a footprint beyond the float range: the center is not finite
    const float farAway[16] = {1e-6f, 0, 0, 0, 0, 1.0f / 25, 0, 0, 0, 0, 0.001f, 0, 1e33f, 0.8f, 0.5f, 1};   // x = -+1e39
    CHECK_ERR(reflectionCamera(farAway, rc), Err::BadLayout);
    for (float v : rc.viewProj) CHECK_EQ(v, 0.0f);
}

// 4301's Tiny loads: the scene ice draw's rows 0-3 (= g_View[0] x g_Proj) give the reflected ice draw's rows 0-3
// (= 0xDA1990 x 0xDA1A90) and its camera row 12 (= the eye).
C5R_TEST(reflection_camera_equals_stock_ice_reflection_draws) {
    struct Pair { const char* scene; const char* refl; };
    const Pair pairs[2] = {{"stock_load1/buffers/51172_9b6b504e.bin", "stock_load1/buffers/51180_70d5a20c.bin"},
                           {"stock_load2/buffers/51178_79458cee.bin", "stock_load2/buffers/51170_81013a40.bin"}};
    for (const Pair& p : pairs) {
        std::vector<uint8_t> s, r;
        if (!readRef("C5R_ICE_REF", "F:/c5-64bit-work/0169/4301/ref", p.scene, 480, s)) return;
        if (!readRef("C5R_ICE_REF", "F:/c5-64bit-work/0169/4301/ref", p.refl, 480, r)) return;
        float vp[16], want[16], eye[3];
        mat(s, 0, vp);
        mat(r, 0, want);
        std::memcpy(eye, r.data() + 12 * 16, 12);
        ReflectionCamera rc;
        CHECK_OK(reflectionCamera(vp, rc));
        const double rel = maxRel(rc.viewProj, want);
        std::printf("  %s: view-projection max relative difference %.3g, eye (%.3f %.3f %.3f) against (%.3f %.3f %.3f)\n", p.refl, rel,
                    rc.eye[0], rc.eye[1], rc.eye[2], eye[0], eye[1], eye[2]);
        CHECK(rel < 4e-6);
        for (int i = 0; i < 3; ++i) CHECK(std::fabs(rc.eye[i] - eye[i]) < 0.01f);
    }
}

// 3601's two cameras: WaterFrame 0x160 (0xDA1890) from WaterFrame 0x000 (view 0's ViewProj).
C5R_TEST(reflection_camera_equals_stock_waterframe_0x160) {
    for (const char* name : {"cam1_waterframe.bin", "cam2_waterframe.bin"}) {
        std::vector<uint8_t> b;
        if (!readRef("C5R_REFL_REF", "F:/c5-64bit-work/0382/ref", name, kWaterFrameBytes, b)) return;
        float vp[16], want[16];
        mat(b, 0x000, vp);
        mat(b, 0x160, want);
        ReflectionCamera rc;
        CHECK_OK(reflectionCamera(vp, rc));
        const double rel = maxRel(rc.viewProj, want);
        std::printf("  %s: 0x160 max relative difference %.3g\n", name, rel);
        CHECK(rel < 4e-6);
    }
}

namespace {
struct Fixture {
    wtest::Device d;
    Renderer r;
    bool ok = false;
    Fixture() {
        const std::string exe = test::exePath();
        if (exe.empty()) test::skip("C5R_EXE not set: the WaterShader programs come from the EXE");
        const std::string game = test::gameDir();   // the install's water art (the LEAN pair the PS's taps depend on)
        if (game.empty()) test::skip("C5R_GAME_DIR not set: the water art comes from the install");
        CHECK_OK(d.create(false));
        Status st = r.init(d.dev, wide(exe), wide(game));
        CHECK_OK(st);
        ok = st.ok();
    }
};

// a 1024 x 1024 RGBA8 texture of one colour
ID3D11ShaderResourceView* solid(ID3D11Device* dev, uint32_t rgba, ID3D11Texture2D** tex) {
    std::vector<uint32_t> px(kReflectionSize * kReflectionSize, rgba);
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = d.Height = kReflectionSize;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA s = {px.data(), kReflectionSize * 4, 0};
    ID3D11ShaderResourceView* srv = nullptr;
    if (FAILED(dev->CreateTexture2D(&d, &s, tex)) || FAILED(dev->CreateShaderResourceView(*tex, nullptr, &srv))) return nullptr;
    return srv;
}
} // namespace

// draw(): view 0 carries shadowMatrix1 as given, the wrap views T(-width) x and T(+width) x it (0x615950's loop); with
// the empty t4 the matrix changes no pixel (the picture before 0382), with a t4 that holds something the water does.
C5R_TEST(renderer_reflection_matrix_per_view_and_t4) {
    Fixture fx;
    if (!fx.ok) return;
    CHECK_OK(fx.r.setHeightField(wtest::flatGrid(28, 16, 0, true)));
    wtest::Target t;
    CHECK_OK(t.create(fx.d.dev, 160, 120));
    FrameInputs in;
    in.cam = wtest::downCamera(0, 0, 600, 160, 120);
    in.fields = fx.r.stockFields();
    float vp[16];
    mulF(in.cam.view, in.cam.proj, vp);
    ReflectionCamera rc;
    CHECK_OK(reflectionCamera(vp, rc));
    std::memcpy(in.shadowMatrix1, rc.viewProj, 64);
    FrameStats st;
    CHECK_OK(fx.r.draw(fx.d.ctx, t.rtv, nullptr, in, Options(), &st));
    CHECK_EQ(fx.r.frames().size(), size_t(3));
    const World w = worldFromGrid(wtest::flatGrid(28, 16, 0, true));
    CHECK(std::memcmp(fx.r.frames()[0].data() + 0x160, rc.viewProj, 64) == 0);
    for (int v = 1; v <= 2; ++v) {
        const float shift = v == 1 ? -w.width : w.width;
        const float tm[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, shift, 0, 0, 1};
        float want[16];
        mulF(tm, rc.viewProj, want);
        CHECK(std::memcmp(fx.r.frames()[v].data() + 0x160, want, 64) == 0);
    }
    // pixels: identity against the reflection matrix with the module's zero t4: equal; a white t4: the water changes
    Options o;
    o.wrapCopies = false;
    std::vector<uint8_t> a, b, c;
    const float grey[4] = {0.3f, 0.3f, 0.3f, 1};
    FrameInputs ident = in;
    for (int i = 0; i < 16; ++i) ident.shadowMatrix1[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    fx.d.ctx->ClearRenderTargetView(t.rtv, grey);
    CHECK_OK(fx.r.draw(fx.d.ctx, t.rtv, nullptr, ident, o, &st));
    CHECK_OK(t.read(fx.d.ctx, a));
    fx.d.ctx->ClearRenderTargetView(t.rtv, grey);
    CHECK_OK(fx.r.draw(fx.d.ctx, t.rtv, nullptr, in, o, &st));
    CHECK_OK(t.read(fx.d.ctx, b));
    CHECK(a == b);
    ID3D11Texture2D* tex = nullptr;
    ID3D11ShaderResourceView* white = solid(fx.d.dev, 0xFFFFFFFFu, &tex);
    CHECK(white != nullptr);
    if (!white) return;
    CHECK_OK(fx.r.setTexture(Slot::Reflection, white));
    fx.d.ctx->ClearRenderTargetView(t.rtv, grey);
    CHECK_OK(fx.r.draw(fx.d.ctx, t.rtv, nullptr, in, o, &st));
    CHECK_OK(t.read(fx.d.ctx, c));
    size_t differ = 0;
    for (size_t i = 0; i < b.size(); i += 4)
        if (std::memcmp(&b[i], &c[i], 3) != 0) ++differ;
    std::printf("  white t4: %zu of %zu pixels differ from the empty reflection (centre %u %u %u %u -> %u %u %u %u)\n", differ,
                b.size() / 4, b[(60 * 160 + 80) * 4], b[(60 * 160 + 80) * 4 + 1], b[(60 * 160 + 80) * 4 + 2], b[(60 * 160 + 80) * 4 + 3],
                c[(60 * 160 + 80) * 4], c[(60 * 160 + 80) * 4 + 1], c[(60 * 160 + 80) * 4 + 2], c[(60 * 160 + 80) * 4 + 3]);
    CHECK(differ > b.size() / 8);
    CHECK_OK(fx.r.setTexture(Slot::Reflection, nullptr));
    white->Release();
    tex->Release();
}

C5R_TEST_MAIN
