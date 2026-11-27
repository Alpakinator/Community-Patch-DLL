// c5r::water tests (epic 3600, issue 3604). WaterFrame against a hand-computed case, the ocean geometry against hand-
// computed quads, and every check of the API with a negative test asserting the error code. The draw tests use the
// EXE's own WaterShader programs (C5R_EXE) on a windowless device; the art tests read the install (C5R_GAME_DIR).
// Without them those tests skip, the executable exits 77 and render/build.py --test fails the run (bug 0091 item 2).
#include <windows.h>
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

float at(const uint8_t* b, uint32_t off) {
    float v;
    std::memcpy(&v, b + off, 4);
    return v;
}
uint32_t bits(float v) {
    uint32_t u;
    std::memcpy(&u, &v, 4);
    return u;
}
bool approx(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

// A world of 2048 x 1024 units (powers of two, so that the hand-computed values are exact).
World world2048() {
    World w;
    w.minX = -1024;
    w.maxX = 1024;
    w.minY = -512;
    w.maxY = 512;
    w.width = 2048;
    w.height = 1024;
    w.samplesX = 2048;
    w.samplesY = 1184;
    w.valid = true;
    return w;
}

// The hand-computed case: camera at (10, 20, 300) with view = T(-10, -20, -300), proj = diag(2, 3, 0.5, 1).
FrameInputs handCase() {
    FrameInputs in;
    const float v[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -10, -20, -300, 1};
    const float p[16] = {2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 1};
    std::memcpy(in.cam.view, v, sizeof v);
    std::memcpy(in.cam.proj, p, sizeof p);
    in.cam.width = 64;
    in.cam.height = 32;
    in.fields = stockWaterFields(nullptr);
    in.fields.set(0xE4, 2.0f);
    in.fields.set(0xD4, 0.0078125f);  // 1/128: 2048 / 128 = 16 waves
    in.fields.set(0xDC, 0.5f);
    in.fields.set(0xD0, 5.0f);
    in.fields.set(0x6C, 0.5f);
    const float a[3] = {0.6f, 0.8f, 0.0f}, b[3] = {0.0f, 0.0f, 1.0f};
    for (int i = 0; i < 3; ++i) {
        in.fields.set(0x90 + 4 * i, a[i]);
        in.fields.set(0x9C + 4 * i, b[i]);
    }
    for (int i = 0; i < 4; ++i) {
        in.fields.set(0x70 + 4 * i, 1.0f + i);
        in.fields.set(0x80 + 4 * i, 5.0f + i);
        in.fields.set(0xA8 + 4 * i, 9.0f + i);
        in.fields.set(0xB8 + 4 * i, 13.0f + i);
    }
    in.world = world2048();
    in.waterZ = 10.117f;
    in.fog.density = 0.001f;
    in.fog.scale = 2.0f;
    in.fog.w = 7.0f;
    const float fc[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    std::memcpy(in.fog.color, fc, sizeof fc);
    in.shadowBlend = 0.75f;
    in.overlayProj[0] = 4.0f;
    in.shadowMatrix0[5] = 6.0f;
    in.shadowMatrix1[10] = 8.0f;
    return in;
}

} // namespace

// ---- WaterFrame ----

C5R_TEST(waterframe_members_equal_the_hand_computed_case) {
    const FrameInputs in = handCase();
    uint8_t f[kWaterFrameBytes];
    CHECK_OK(computeWaterFrame(in, f));
    // 0x000 ViewProj = T(-10, -20, -300) x diag(2, 3, 0.5, 1): exact
    const float vp[16] = {2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0.5f, 0, -20, -60, -150, 1};
    for (int i = 0; i < 16; ++i) CHECK_EQ(bits(at(f, 4 * i)), bits(vp[i]));
    CHECK_EQ(at(f, 0x040), 4.0f);   // OverlayProj
    CHECK_EQ(at(f, 0x1A0 + 20), 6.0f);  // ShadowMatrix0[1][1]
    CHECK_EQ(at(f, 0x160 + 40), 8.0f);  // ShadowMatrix1[2][2]
    // 0x080 WaveScale = sqrt(2) * 2 * (5/9, 2/3, 5/36, -5/6) = (1.5713484, 1.8856181, 0.3928371, -2.3570226)
    CHECK(approx(at(f, 0x080), 1.5713484f, 2e-6f));
    CHECK(approx(at(f, 0x084), 1.8856181f, 2e-6f));
    CHECK(approx(at(f, 0x088), 0.3928371f, 1e-6f));
    CHECK(approx(at(f, 0x08C), -2.3570226f, 2e-6f));
    // 0x090 WavePhase: n = trunc(2048 / 128) = 16, s = 1/2048: (16, 17, 18, 19) / 2048, exact
    CHECK_EQ(at(f, 0x090), 16.0f / 2048);
    CHECK_EQ(at(f, 0x094), 17.0f / 2048);
    CHECK_EQ(at(f, 0x098), 18.0f / 2048);
    CHECK_EQ(at(f, 0x09C), 19.0f / 2048);
    CHECK_EQ(at(f, 0x0A0), 1.7f);
    CHECK_EQ(at(f, 0x0A4), 1.2f);
    CHECK_EQ(at(f, 0x0A8), 1.7f);
    CHECK_EQ(at(f, 0x0AC), 1.2f);
    CHECK_EQ(at(f, 0x0B0), 0.5f * 16.0f / 2048);  // WaveR11 = Water+0xDC * WavePhase.x
    CHECK(approx(at(f, 0x0C0), 10.117f, 2e-6f));    // WaveR12: the water height
    CHECK_EQ(at(f, 0x0D0), 5.0f);
    CHECK_EQ(at(f, 0x0E0), 0.6f);
    CHECK_EQ(at(f, 0x0E4), 0.8f);
    CHECK_EQ(at(f, 0x0F8), 1.0f);
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(at(f, 0x100 + 4 * i), 1.0f + i);
        CHECK_EQ(at(f, 0x110 + 4 * i), 5.0f + i);
        CHECK_EQ(at(f, 0x120 + 4 * i), 9.0f + i);
        CHECK_EQ(at(f, 0x130 + 4 * i), 13.0f + i);
    }
    // 0x140 FogParams = (-0.001, exp(-0.3) * -1000 * 2 = -1481.6364, 0, 7)
    CHECK_EQ(bits(at(f, 0x140)), bits(-0.001f));
    CHECK(approx(at(f, 0x144), -1481.6364f, 2e-3f));
    CHECK_EQ(at(f, 0x148), 0.0f);
    CHECK_EQ(at(f, 0x14C), 7.0f);
    // 0x150 CameraPos (10, 20, 300)
    CHECK_EQ(at(f, 0x150), 10.0f);
    CHECK_EQ(at(f, 0x154), 20.0f);
    CHECK_EQ(at(f, 0x158), 300.0f);
    // 0x1E0 HeightmapUV = T(1024, 512, 0) x S(1/2048, -1/1024, 255 / (0.2 * 65535) = 0.0194553)
    CHECK_EQ(at(f, 0x1E0), 1.0f / 2048);
    CHECK_EQ(at(f, 0x1E0 + 20), -1.0f / 1024);
    CHECK(approx(at(f, 0x1E0 + 40), 0.01945525f, 1e-7f));
    CHECK_EQ(at(f, 0x1E0 + 48), 0.5f);
    CHECK_EQ(at(f, 0x1E0 + 52), -0.5f);
    CHECK_EQ(bits(at(f, 0x1E0 + 56)), 0u);
    CHECK_EQ(at(f, 0x1E0 + 60), 1.0f);
    CHECK_EQ(at(f, 0x220), 0.75f);
    CHECK_EQ(at(f, 0x230), 0.25f);
    CHECK_EQ(at(f, 0x23C), 1.0f);
    CHECK_EQ(at(f, 0x240), 0.5f);
    // bytes the writer never writes stay 0
    const uint32_t gaps[] = {0x0B4, 0x0B8, 0x0BC, 0x0C4, 0x0D4, 0x0DC, 0x0EC, 0x0FC, 0x15C, 0x224, 0x22C, 0x244, 0x24C};
    for (uint32_t g : gaps) CHECK_EQ(bits(at(f, g)), 0u);
}

C5R_TEST(waterframe_clock_given_cam_and_no_fog) {
    FrameInputs in = handCase();
    in.clock.apply = true;
    in.clock.seconds = 2.0;
    in.haveCamPos = true;
    in.camPos[0] = 1;
    in.camPos[1] = 2;
    in.camPos[2] = 3;
    in.fog.density = 0;  // no fog: FogParams.y 0, not -inf
    in.waterZ = -1;      // Water+0xC8 as given: the stock 13000 / 65535 -> 13000 / 65535 * 51 = 10.1167
    uint8_t f[kWaterFrameBytes];
    CHECK_OK(computeWaterFrame(in, f));
    CHECK_EQ(at(f, 0x0B0), 2.0f * 16.0f / 2048);
    CHECK_EQ(at(f, 0x150), 1.0f);
    CHECK_EQ(at(f, 0x158), 3.0f);
    CHECK_EQ(bits(at(f, 0x144)), 0u);
    CHECK(approx(at(f, 0x0C0), 10.1167f, 1e-4f));
}

C5R_TEST(waterframe_rejects_bad_input) {
    uint8_t f[kWaterFrameBytes];
    std::memset(f, 0xAB, sizeof f);
    FrameInputs in = handCase();
    CHECK_ERR(computeWaterFrame(in, nullptr), Err::BadLayout);
    in.cam.view[3] = NAN;
    CHECK_ERR(computeWaterFrame(in, f), Err::BadLayout);
    CHECK_EQ(f[0], 0xAB);  // nothing written on failure
    in = handCase();
    in.cam.proj[0] = INFINITY;
    CHECK_ERR(computeWaterFrame(in, f), Err::BadLayout);
    in = handCase();
    in.clock.seconds = NAN;
    CHECK_ERR(computeWaterFrame(in, f), Err::BadLayout);
    in = handCase();
    in.fog.color[1] = NAN;
    CHECK_ERR(computeWaterFrame(in, f), Err::BadLayout);
    in = handCase();
    in.fields.valid = false;
    CHECK_ERR(computeWaterFrame(in, f), Err::NotFound);
    in = handCase();
    in.world.valid = false;
    CHECK_ERR(computeWaterFrame(in, f), Err::BadLayout);
    in = handCase();
    in.world.width = 0;
    CHECK_ERR(computeWaterFrame(in, f), Err::BadLayout);
    in = handCase();
    in.world.samplesX = 0;
    CHECK_ERR(computeWaterFrame(in, f), Err::BadLayout);
    in = handCase();
    const float sing[16] = {0};
    std::memcpy(in.cam.view, sing, sizeof sing);  // a view without inverse and no camPos given
    CHECK_ERR(computeWaterFrame(in, f), Err::BadLayout);
    CHECK_EQ(f[0], 0xAB);
}

C5R_TEST(stock_fields_and_sky_texels) {
    const WaterFields s = stockWaterFields(nullptr);
    CHECK(s.valid);
    CHECK_EQ(s.get(0x60), 0.1f);
    CHECK_EQ(s.get(0x68), 2.0f);
    CHECK_EQ(s.get(0x6C), 0.5f);
    CHECK(approx(s.get(0x7C), 3444.3117f, 1e-3f));  // 2^11.75
    CHECK(approx(s.get(0x8C), 53.817371f, 1e-4f));  // 2^5.75
    CHECK(approx(s.get(0xC8), 0.19836728f, 1e-7f)); // 13000 / 65535
    CHECK_EQ(s.get(0xCC), 1.43f);
    CHECK_EQ(s.get(0xD4), 0.008f);
    CHECK_EQ(s.get(0xD8), 0.18f);
    CHECK_EQ(s.get(0xE0), 0.03f);
    CHECK_EQ(s.get(0xE4), 0.0f);
    CHECK_EQ(s.get(0x5C), 0.0f);   // outside the block
    CHECK_EQ(s.get(0xE8), 0.0f);
    CHECK_EQ(s.get(0x61), 0.0f);   // unaligned
    const float sky[6][4] = {{0.5f, 0.25f, 1.0f, 0.5f}, {1, 1, 1, 1}, {1.0f, 0.5f, 0.5f, 0}, {0.5f, 0.5f, 0.0f, 0},
                             {0.5f, 1.0f, 0.5f, 0.25f}, {0.5f, 0.5f, 0.5f, 0.75f}};
    const WaterFields k = stockWaterFields(sky);
    CHECK_EQ(k.get(0x70), 1.0f);
    CHECK_EQ(k.get(0x74), 0.5f);
    CHECK_EQ(k.get(0x78), 2.0f);
    CHECK(approx(k.get(0x7C), 3444.3117f, 1e-3f));  // kept
    CHECK_EQ(k.get(0x90), 1.0f);   // normalize(0.5, 0, 0)
    CHECK_EQ(k.get(0x94), 0.0f);
    CHECK_EQ(k.get(0xA4), -1.0f);  // normalize(0, 0, -0.5)
    CHECK_EQ(k.get(0xAC), 1.0f);   // normalize(0, 0.5, 0)
    CHECK_EQ(k.get(0xB4), 0.25f);
    CHECK_EQ(k.get(0xB8), 0.0f);   // zero vector stays zero
    CHECK_EQ(k.get(0xC4), 0.75f);
}

// ---- Footprint and geometry ----

C5R_TEST(footprint_of_a_down_camera) {
    const World w = world2048();
    const terrain::Camera c = wtest::downCamera(0, 0, 1000, 400, 200);
    float vp[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            double s = 0;
            for (int k = 0; k < 4; ++k) s += static_cast<double>(c.view[i * 4 + k]) * c.proj[k * 4 + j];
            vp[i * 4 + j] = static_cast<float>(s);
        }
    Footprint fp;
    CHECK_OK(viewFootprint(vp, w, fp));
    // at z = 0 the half height is 1000 * tan 15 = 267.949, the half width twice that; z = 127.5 lies inside
    CHECK(approx(fp.maxY, 267.949f, 0.05f));
    CHECK(approx(fp.minY, -267.949f, 0.05f));
    CHECK(approx(fp.maxX, 535.898f, 0.1f));
    CHECK(approx(fp.minX, -535.898f, 0.1f));
    // clamped to the world box
    const terrain::Camera hi = wtest::downCamera(900, 0, 4000, 400, 200);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            double s = 0;
            for (int k = 0; k < 4; ++k) s += static_cast<double>(hi.view[i * 4 + k]) * hi.proj[k * 4 + j];
            vp[i * 4 + j] = static_cast<float>(s);
        }
    CHECK_OK(viewFootprint(vp, w, fp));
    CHECK_EQ(fp.maxX, 1024.0f);
    CHECK_EQ(fp.minY, -512.0f);
    vp[5] = NAN;
    CHECK_ERR(viewFootprint(vp, w, fp), Err::BadLayout);
    vp[5] = 1;
    World bad = w;
    bad.valid = false;
    CHECK_ERR(viewFootprint(vp, bad, fp), Err::BadLayout);
}

C5R_TEST(ocean_geometry_hand_computed_quads) {
    // Tiny-like: 28 x 16 plots, 1792 x 1024 samples; width 1791, height 1023 * 0.866
    const World w = worldFromGrid(wtest::flatGrid(28, 16, 0));
    CHECK(w.valid);
    CHECK_EQ(w.minX, -895.5f);
    CHECK(approx(w.height, 885.918f, 1e-3f));
    Footprint fp;
    fp.minX = w.minX;
    fp.minY = w.minY;
    fp.maxX = w.minX + 100;  // samples 0..100 -> blocks [0, 2)
    fp.maxY = w.minY + 50;   // samples 0..57.7 -> 58 -> blocks [0, 1)
    OceanGeometry g;
    CHECK_OK(buildOceanGeometry(w, &fp, 1, g));
    CHECK_EQ(g.quads, 2u);
    CHECK_EQ(g.batches.size(), size_t(1));
    CHECK_EQ(g.batches[0].page, 0u);
    CHECK_EQ(g.batches[0].quads, 2u);
    CHECK(!g.indexOverflow);
    // quad 1 = block (1, 0): x -831.5 .. -767.5, y minY .. minY + 55.424
    const float* q = g.vertices.data() + 16;
    CHECK_EQ(q[0], -831.5f);
    CHECK_EQ(q[4], -767.5f);
    CHECK(approx(q[1], w.minY + 55.424f, 1e-3f));
    CHECK(approx(q[9], w.minY, 1e-4f));
    CHECK(approx(q[2], 0.1250349f, 1e-6f));   // u0 = (64 * 1792/1791 + 64) / 1024
    CHECK(approx(q[6], 0.1875698f, 1e-6f));   // u1 = (128 * 1792/1791 + 64) / 1024
    CHECK(approx(q[3], -0.1250611f, 1e-6f));  // v top = -(55.424 * 1024/885.918 + 64) / 1024
    CHECK(approx(q[11], -0.0625f, 1e-6f));    // v bottom = -64 / 1024
    CHECK_EQ(q[14], q[2]);
    const uint16_t want[6] = {4, 5, 6, 4, 6, 7};
    for (int i = 0; i < 6; ++i) CHECK_EQ(g.indices[6 + i], want[i]);
    // two pages, two views: grouped by (view, page), view 0 first
    Footprint two[2] = {fp, fp};
    two[0].maxX = w.minX + 1000;  // blocks 0..15: 0..13 on page 0, 14..15 on page 1
    CHECK_OK(buildOceanGeometry(w, two, 2, g));
    CHECK_EQ(g.batches.size(), size_t(3));
    CHECK_EQ(g.batches[0].view, 0u);
    CHECK_EQ(g.batches[0].quads, 14u);
    CHECK_EQ(g.batches[1].page, 1u);
    CHECK_EQ(g.batches[1].firstQuad, 14u);
    CHECK_EQ(g.batches[1].quads, 2u);
    CHECK_EQ(g.batches[2].view, 1u);
    CHECK_EQ(g.batches[2].firstQuad, 16u);
    // page 1's u offset: block 14 starts at sample 896 -> u0 = (896 * 1792/1791 + 64 - 896) / 1024
    CHECK(approx(g.vertices[14 * 16 + 2], (896.0f * 1792 / 1791 + 64 - 896) / 1024, 2e-6f));
}

// The world box the EXE holds at 0xD90FCC..0xD90FE8, read from the running game by epic 3000's ground truth
// (D:/c5-64bit-work/i2-ground-truth/{tiny,huge}/grid_constants.json, block_hex; 2026-11-11): bit-equal.
C5R_TEST(world_from_grid_equals_the_exe_globals) {
    heightfield::HeightGrid g;
    g.samplesX = 3584;
    g.samplesY = 2560;
    g.wrapX = true;
    World w = worldFromGrid(g);
    CHECK_EQ(w.minX, -1791.5f);
    CHECK_EQ(w.maxX, 1791.5f);
    CHECK_EQ(w.width, 3583.0f);
    CHECK_EQ(w.height, 2216.093994140625f);
    CHECK_EQ(w.minY, -1108.0469970703125f);
    CHECK_EQ(w.maxY, 1108.0469970703125f);
    g.samplesX = 8192;
    g.samplesY = 5376;
    w = worldFromGrid(g);
    CHECK_EQ(w.minX, -4095.5f);
    CHECK_EQ(w.width, 8191.0f);
    CHECK_EQ(w.height, 4654.75f);
    CHECK_EQ(w.minY, -2327.375f);
    g.samplesX = 0;
    CHECK(!worldFromGrid(g).valid);
}

C5R_TEST(ocean_geometry_rejects_and_overflows) {
    const World w = worldFromGrid(wtest::flatGrid(28, 16, 0));
    Footprint fp;
    fp.minX = w.minX;
    fp.minY = w.minY;
    fp.maxX = w.maxX;
    fp.maxY = w.maxY;
    OceanGeometry g;
    CHECK_ERR(buildOceanGeometry(w, nullptr, 1, g), Err::BadLayout);
    CHECK_ERR(buildOceanGeometry(w, &fp, 0, g), Err::BadLayout);
    Footprint five[5] = {fp, fp, fp, fp, fp};
    CHECK_ERR(buildOceanGeometry(w, five, 5, g), Err::BadLayout);
    Footprint nanFp = fp;
    nanFp.maxY = NAN;
    CHECK_ERR(buildOceanGeometry(w, &nanFp, 1, g), Err::BadLayout);
    World bad = w;
    bad.valid = false;
    CHECK_ERR(buildOceanGeometry(bad, &fp, 1, g), Err::BadLayout);
    // a whole 160 x 110-plot world in view: 17,600 quads, past 16,384 the 16-bit indices wrap (as the EXE's)
    World big;
    big.samplesX = 160 * 64;
    big.samplesY = 110 * 64;
    big.width = big.samplesX - 1.0f;
    big.height = (big.samplesY - 1.0f) * 0.866f;
    big.minX = -big.width / 2;
    big.maxX = big.width / 2;
    big.minY = -big.height / 2;
    big.maxY = big.height / 2;
    big.valid = true;
    Footprint all = {big.minX, big.minY, big.maxX, big.maxY};
    CHECK_OK(buildOceanGeometry(big, &all, 1, g));
    CHECK(g.indexOverflow);
    CHECK_EQ(g.quads, 160u * 110u);  // blocks [0, s/64 + 1); the clamp at samples - 1 never bites
    // a map past 16 pages (256 plots): Unsupported
    World huge = big;
    huge.samplesX = 256 * 64;
    huge.width = huge.samplesX - 1.0f;
    huge.minX = -huge.width / 2;
    huge.maxX = huge.width / 2;
    Footprint hall = {huge.minX, huge.minY, huge.maxX, huge.maxY};
    CHECK_ERR(buildOceanGeometry(huge, &hall, 1, g), Err::Unsupported);
}

// ---- Renderer without the EXE ----

C5R_TEST(renderer_calls_before_init_fail) {
    Renderer r;
    CHECK_ERR(r.init(nullptr, L"x.exe", L""), Err::BadLayout);
    CHECK_ERR(r.setHeightField(wtest::flatGrid(2, 2, 0)), Err::NotFound);
    CHECK_ERR(r.setMesh(System::River, Mesh()), Err::NotFound);
    CHECK_ERR(r.setTexture(Slot::Currents, nullptr), Err::NotFound);
    double ms = 0;
    CHECK_ERR(r.gpuTime(nullptr, false, &ms), Err::NotFound);
    FrameInputs in = handCase();
    CHECK_ERR(r.draw(nullptr, nullptr, nullptr, in, Options(), nullptr), Err::NotFound);
    CHECK_EQ(r.oceanGeometry().quads, 0u);
    CHECK(!r.stockFields().valid);
    wtest::Device d;
    CHECK_OK(d.create(true));
    CHECK_ERR(r.init(d.dev, L"C:/no/such/CivilizationV_DX11.exe", L""), Err::Io);
}

// ---- Renderer with the EXE's programs ----

namespace {

struct Fixture {
    wtest::Device d;
    Renderer r;
    bool ok = false;
    explicit Fixture(const std::wstring& game = L"") {
        const std::string exe = test::exePath();
        if (exe.empty()) test::skip("C5R_EXE not set: the WaterShader programs come from the EXE");
        CHECK_OK(d.create(false));
        Status st = r.init(d.dev, wide(exe), game);
        CHECK_OK(st);
        ok = st.ok();
    }
};

FrameInputs downInputs(Renderer& r, uint32_t w, uint32_t h) {
    FrameInputs in;
    in.cam = wtest::downCamera(0, 0, 600, w, h);
    in.fields = r.stockFields();
    return in;
}

} // namespace

C5R_TEST(renderer_mesh_and_texture_checks) {
    Fixture fx;
    if (!fx.ok) return;
    CHECK(fx.r.stockFields().valid);
    CHECK_ERR(fx.r.setMesh(System::Ocean, Mesh()), Err::Unsupported);
    CHECK_ERR(fx.r.setMesh(static_cast<System>(7), Mesh()), Err::BadLayout);
    Mesh m;
    m.vb.assign(96, 0);  // three 32-byte river vertices (3605: the river part takes stride 32 only)
    m.ib.assign(12, 0);
    m.indexFormat = DXGI_FORMAT_R16_UINT;
    CHECK_ERR(fx.r.setMesh(System::River, m), Err::BadLayout);  // stride 0
    m.stride = 32;
    m.ib.assign(13, 0);
    CHECK_ERR(fx.r.setMesh(System::River, m), Err::BadLayout);  // ib not whole indices
    m.ib.assign(12, 0);
    Mesh::Draw dr;
    dr.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    dr.indexCount = 7;
    m.draws.push_back(dr);
    CHECK_ERR(fx.r.setMesh(System::River, m), Err::BadLayout);  // indices outside
    m.draws[0].indexCount = 6;
    m.draws[0].pass = 9;
    CHECK_ERR(fx.r.setMesh(System::River, m), Err::BadLayout);  // pass
    m.draws[0].pass = 1;
    m.draws[0].topology = 0;
    CHECK_ERR(fx.r.setMesh(System::River, m), Err::BadLayout);  // topology
    m.draws[0].topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    m.indexFormat = 99;
    CHECK_ERR(fx.r.setMesh(System::River, m), Err::BadLayout);  // index format
    m.indexFormat = DXGI_FORMAT_R16_UINT;
    CHECK_OK(fx.r.setMesh(System::River, m));
    CHECK_OK(fx.r.setMesh(System::River, Mesh()));               // clears
    CHECK_ERR(fx.r.setTexture(static_cast<Slot>(2), nullptr), Err::BadLayout);
    CHECK_ERR(fx.r.setTexture(static_cast<Slot>(7), nullptr), Err::BadLayout);  // t7 is the height page
    CHECK_OK(fx.r.setTexture(Slot::Currents, nullptr));
}

C5R_TEST(renderer_draw_checks_and_missing_field) {
    Fixture fx;
    if (!fx.ok) return;
    wtest::Target t;
    CHECK_OK(t.create(fx.d.dev, 160, 120));
    FrameInputs in = downInputs(fx.r, 160, 120);
    CHECK_ERR(fx.r.draw(fx.d.ctx, t.rtv, nullptr, in, Options(), nullptr), Err::NotFound);  // no field
    heightfield::HeightGrid bad = wtest::flatGrid(28, 16, 0);
    bad.h.pop_back();
    CHECK_ERR(fx.r.setHeightField(bad), Err::BadLayout);
    bad = wtest::flatGrid(28, 16, 0);
    bad.sy = NAN;
    CHECK_ERR(fx.r.setHeightField(bad), Err::BadLayout);
    CHECK_OK(fx.r.setHeightField(wtest::flatGrid(28, 16, 0)));
    CHECK_ERR(fx.r.setHeightField(bad), Err::BadLayout);  // the previous field stays
    CHECK_ERR(fx.r.draw(nullptr, t.rtv, nullptr, in, Options(), nullptr), Err::BadLayout);
    CHECK_ERR(fx.r.draw(fx.d.ctx, nullptr, nullptr, in, Options(), nullptr), Err::BadLayout);
    FrameInputs nanCam = in;
    nanCam.cam.view[13] = NAN;
    CHECK_ERR(fx.r.draw(fx.d.ctx, t.rtv, nullptr, nanCam, Options(), nullptr), Err::BadLayout);
    FrameInputs wrongSize = in;
    wrongSize.cam.width = 161;
    CHECK_ERR(fx.r.draw(fx.d.ctx, t.rtv, nullptr, wrongSize, Options(), nullptr), Err::BadLayout);
    FrameInputs zero = in;
    zero.cam.height = 0;
    CHECK_ERR(fx.r.draw(fx.d.ctx, t.rtv, nullptr, zero, Options(), nullptr), Err::BadLayout);
    Options riverPass;
    riverPass.oceanPass = 1;
    CHECK_ERR(fx.r.draw(fx.d.ctx, t.rtv, nullptr, in, riverPass, nullptr), Err::BadLayout);
    FrameInputs noFields = in;
    noFields.fields.valid = false;
    CHECK_ERR(fx.r.draw(fx.d.ctx, t.rtv, nullptr, noFields, Options(), nullptr), Err::NotFound);
    FrameStats st;
    CHECK_OK(fx.r.draw(fx.d.ctx, t.rtv, nullptr, in, Options(), &st));  // the field set before the failures
    CHECK(st.oceanDraws > 0);
}

C5R_TEST(renderer_draws_the_ocean_with_the_exe_programs) {
    Fixture fx;
    if (!fx.ok) return;
    CHECK_OK(fx.r.setHeightField(wtest::flatGrid(28, 16, 0)));  // sea floor 0: deep water everywhere
    wtest::Target t;
    CHECK_OK(t.create(fx.d.dev, 160, 120));
    for (int pass : {0, 2, 3, 5}) {
        const float magenta[4] = {1, 0, 1, 1};
        fx.d.ctx->ClearRenderTargetView(t.rtv, magenta);
        FrameInputs in = downInputs(fx.r, 160, 120);
        Options o;
        o.oceanPass = pass;
        o.oceanBlend = false;  // the PS's own output
        FrameStats st;
        CHECK_OK(fx.r.draw(fx.d.ctx, t.rtv, nullptr, in, o, &st));
        CHECK_EQ(st.oceanPassUsed, pass);
        CHECK(st.oceanDraws >= 1);
        CHECK(st.oceanQuads > 0);
        CHECK_EQ(st.pagesMissing, 0u);
        CHECK_EQ(st.systemsMissing, (1u << 1) | (1u << 3));  // rivers and waves have no mesh: skipped and counted
        std::vector<uint8_t> px;
        CHECK_OK(t.read(fx.d.ctx, px));
        size_t changed = 0;
        for (size_t i = 0; i < px.size(); i += 4)
            if (!(px[i] == 255 && px[i + 1] == 0 && px[i + 2] == 255)) ++changed;
        if (changed != px.size() / 4) std::printf("  pass %d: %zu of %zu pixels drawn\n", pass, changed, px.size() / 4);
        CHECK_EQ(changed, px.size() / 4);  // the camera sees only water: every pixel drawn
        // the module's WaterFrame is computeWaterFrame's for the same inputs
        FrameInputs ref = in;
        ref.world = worldFromGrid(wtest::flatGrid(28, 16, 0));
        uint8_t f[kWaterFrameBytes];
        CHECK_OK(computeWaterFrame(ref, f));
        CHECK_EQ(fx.r.frames().size(), size_t(1));
        CHECK(std::memcmp(fx.r.frames()[0].data(), f, kWaterFrameBytes) == 0);
    }
    // ocean off: nothing drawn
    Options off;
    off.ocean = false;
    FrameStats st;
    CHECK_OK(fx.r.draw(fx.d.ctx, t.rtv, nullptr, downInputs(fx.r, 160, 120), off, &st));
    CHECK_EQ(st.oceanDraws, 0u);
    // a wrapping field draws three views
    CHECK_OK(fx.r.setHeightField(wtest::flatGrid(28, 16, 0, true)));
    CHECK_OK(fx.r.draw(fx.d.ctx, t.rtv, nullptr, downInputs(fx.r, 160, 120), Options(), &st));
    CHECK_EQ(fx.r.frames().size(), size_t(3));
    double ms = -1;
    CHECK_OK(fx.r.gpuTime(fx.d.ctx, true, &ms));
    CHECK(ms >= 0);
}

// Issue 3704 (epic 0167): the overlay. Each view's WaterFrame carries the OverlayProj of the EXE view it stands for at
// 0x040 (views 1, 2 = the copies at -width, +width take overlayProjWrap[0], [1] unchanged); without them every view
// takes overlayProj, as before 3704. A matrix that is not finite is a named error; t9 takes a given overlay view, and a
// transparent one leaves the picture bit-equal.
C5R_TEST(renderer_overlay_matrices_per_view_and_t9) {
    Fixture fx;
    if (!fx.ok) return;
    CHECK_OK(fx.r.setHeightField(wtest::flatGrid(28, 16, 0, true)));
    wtest::Target t;
    CHECK_OK(t.create(fx.d.dev, 160, 120));
    FrameInputs in = downInputs(fx.r, 160, 120);
    for (int i = 0; i < 16; ++i) in.overlayProj[i] = 0.5f + static_cast<float>(i);
    FrameStats st;
    CHECK_OK(fx.r.draw(fx.d.ctx, t.rtv, nullptr, in, Options(), &st));
    CHECK_EQ(fx.r.frames().size(), size_t(3));
    for (size_t v = 0; v < fx.r.frames().size(); ++v)
        CHECK(std::memcmp(fx.r.frames()[v].data() + 0x40, in.overlayProj, 64) == 0);   // before 3704: one matrix for all
    in.haveOverlayProjWrap = true;
    for (int i = 0; i < 16; ++i) {
        in.overlayProjWrap[0][i] = -1.25f - static_cast<float>(i);
        in.overlayProjWrap[1][i] = 100.0f + static_cast<float>(i);
    }
    CHECK_OK(fx.r.draw(fx.d.ctx, t.rtv, nullptr, in, Options(), &st));
    CHECK(std::memcmp(fx.r.frames()[0].data() + 0x40, in.overlayProj, 64) == 0);
    CHECK(std::memcmp(fx.r.frames()[1].data() + 0x40, in.overlayProjWrap[0], 64) == 0);
    CHECK(std::memcmp(fx.r.frames()[2].data() + 0x40, in.overlayProjWrap[1], 64) == 0);
    FrameInputs bad = in;
    bad.overlayProjWrap[1][7] = NAN;
    CHECK_ERR(fx.r.draw(fx.d.ctx, t.rtv, nullptr, bad, Options(), &st), Err::BadLayout);
    bad = in;
    bad.overlayProj[3] = INFINITY;
    CHECK_ERR(fx.r.draw(fx.d.ctx, t.rtv, nullptr, bad, Options(), &st), Err::BadLayout);
    bad = in;
    bad.haveOverlayProjWrap = false;
    bad.overlayProjWrap[0][0] = NAN;   // not used without haveOverlayProjWrap
    CHECK_OK(fx.r.draw(fx.d.ctx, t.rtv, nullptr, bad, Options(), &st));
    // t9: the module's transparent 1x1 against a given transparent 4 x 4 view: the same picture, bit for bit
    std::vector<uint8_t> a, b;
    const float grey[4] = {0.3f, 0.3f, 0.3f, 1};
    fx.d.ctx->ClearRenderTargetView(t.rtv, grey);
    CHECK_OK(fx.r.draw(fx.d.ctx, t.rtv, nullptr, in, Options(), &st));
    CHECK_OK(t.read(fx.d.ctx, a));
    std::vector<uint32_t> zero(16, 0);
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = d.Height = 4;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA s = {zero.data(), 16, 0};
    ID3D11Texture2D* tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    CHECK(SUCCEEDED(fx.d.dev->CreateTexture2D(&d, &s, &tex)) && SUCCEEDED(fx.d.dev->CreateShaderResourceView(tex, nullptr, &srv)));
    if (!srv) return;
    CHECK_OK(fx.r.setTexture(Slot::Overlay, srv));
    CHECK(fx.r.inputTexture(Slot::Overlay) == srv);
    fx.d.ctx->ClearRenderTargetView(t.rtv, grey);
    CHECK_OK(fx.r.draw(fx.d.ctx, t.rtv, nullptr, in, Options(), &st));
    CHECK_OK(t.read(fx.d.ctx, b));
    CHECK(a == b);
    CHECK_OK(fx.r.setTexture(Slot::Overlay, nullptr));   // the module's own again
    CHECK(fx.r.inputTexture(Slot::Overlay) != srv);
    srv->Release();
    tex->Release();
}

// Issue 4308 (epic 0169): the shadow. Each view's WaterFrame carries the ShadowMatrix0 of the EXE view it stands for at
// 0x1A0 (views 1, 2 take shadowMatrix0Wrap[0], [1] unchanged) and the strength at 0x220; without the wrap matrices every
// view takes shadowMatrix0, as before. A wrap matrix that is not finite is a named error. t13 takes the EXE's map (R16 or,
// at quality 1, R16G16B16A16_FLOAT): a map of zeros is the module's own "no shadow" picture bit for bit, and the float
// map of r = 1 equals the R16 map of ones.
C5R_TEST(renderer_shadow_matrices_per_view_and_t13) {
    Fixture fx;
    if (!fx.ok) return;
    CHECK_OK(fx.r.setHeightField(wtest::flatGrid(28, 16, 0, true)));
    wtest::Target t;
    CHECK_OK(t.create(fx.d.dev, 160, 120));
    FrameInputs in = downInputs(fx.r, 160, 120);
    FrameStats st;
    for (int i = 0; i < 16; ++i) in.shadowMatrix0[i] = 0.25f + static_cast<float>(i);
    in.shadowBlend = 0.75f;
    CHECK_OK(fx.r.draw(fx.d.ctx, t.rtv, nullptr, in, Options(), &st));
    CHECK_EQ(fx.r.frames().size(), size_t(3));
    for (size_t v = 0; v < fx.r.frames().size(); ++v) {
        CHECK(std::memcmp(fx.r.frames()[v].data() + 0x1A0, in.shadowMatrix0, 64) == 0);
        CHECK(std::memcmp(fx.r.frames()[v].data() + 0x220, &in.shadowBlend, 4) == 0);
    }
    in.haveShadowMatrix0Wrap = true;
    for (int i = 0; i < 16; ++i) {
        in.shadowMatrix0Wrap[0][i] = -3.5f - static_cast<float>(i);
        in.shadowMatrix0Wrap[1][i] = 42.0f + static_cast<float>(i);
    }
    CHECK_OK(fx.r.draw(fx.d.ctx, t.rtv, nullptr, in, Options(), &st));
    CHECK(std::memcmp(fx.r.frames()[0].data() + 0x1A0, in.shadowMatrix0, 64) == 0);
    CHECK(std::memcmp(fx.r.frames()[1].data() + 0x1A0, in.shadowMatrix0Wrap[0], 64) == 0);
    CHECK(std::memcmp(fx.r.frames()[2].data() + 0x1A0, in.shadowMatrix0Wrap[1], 64) == 0);
    FrameInputs bad = in;
    bad.shadowMatrix0Wrap[0][2] = NAN;
    CHECK_ERR(fx.r.draw(fx.d.ctx, t.rtv, nullptr, bad, Options(), &st), Err::BadLayout);
    bad.haveShadowMatrix0Wrap = false;   // not used without the flag
    CHECK_OK(fx.r.draw(fx.d.ctx, t.rtv, nullptr, bad, Options(), &st));
    // t13: identity matrices (the map's texel at uv (-0.5, -0.5), wrapped) and full strength
    FrameInputs pic = downInputs(fx.r, 160, 120);
    pic.shadowBlend = 1.0f;
    auto tex = [&](DXGI_FORMAT f, const void* px, uint32_t pitch) -> ID3D11ShaderResourceView* {
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = d.Height = 4;
        d.MipLevels = d.ArraySize = 1;
        d.Format = f;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA s = {px, pitch, 0};
        ID3D11Texture2D* tx = nullptr;
        ID3D11ShaderResourceView* v = nullptr;
        if (FAILED(fx.d.dev->CreateTexture2D(&d, &s, &tx))) return nullptr;
        fx.d.dev->CreateShaderResourceView(tx, nullptr, &v);
        tx->Release();
        return v;
    };
    std::vector<uint16_t> z16(16, 0), o16(16, 0xFFFF), f16(64, 0);
    for (int i = 0; i < 16; ++i) f16[i * 4] = 0x3C00;   // half 1.0 in r, the rest 0
    ID3D11ShaderResourceView* zeros = tex(DXGI_FORMAT_R16_UNORM, z16.data(), 8);
    ID3D11ShaderResourceView* ones = tex(DXGI_FORMAT_R16_UNORM, o16.data(), 8);
    ID3D11ShaderResourceView* fOne = tex(DXGI_FORMAT_R16G16B16A16_FLOAT, f16.data(), 32);
    CHECK(zeros && ones && fOne);
    if (!zeros || !ones || !fOne) return;
    const float grey[4] = {0.3f, 0.3f, 0.3f, 1};
    std::vector<uint8_t> own, z, o, f;
    auto shot = [&](ID3D11ShaderResourceView* s, std::vector<uint8_t>& out) {
        CHECK_OK(fx.r.setTexture(Slot::Shadow, s));
        CHECK(fx.r.inputTexture(Slot::Shadow) == (s ? s : fx.r.inputTexture(Slot::Shadow)));
        fx.d.ctx->ClearRenderTargetView(t.rtv, grey);
        CHECK_OK(fx.r.draw(fx.d.ctx, t.rtv, nullptr, pic, Options(), &st));
        CHECK_OK(t.read(fx.d.ctx, out));
    };
    shot(nullptr, own);
    shot(zeros, z);
    shot(ones, o);
    shot(fOne, f);
    CHECK_OK(fx.r.setTexture(Slot::Shadow, nullptr));
    size_t dz = 0, doo = 0, df = 0;
    for (size_t i = 0; i < own.size(); ++i) {
        dz += own[i] != z[i];
        doo += own[i] != o[i];
        df += o[i] != f[i];
    }
    std::printf("  t13: own against R16 zeros %zu bytes differ, against R16 ones %zu; float r=1 against R16 ones %zu\n", dz, doo, df);
    CHECK_EQ(dz, size_t(0));
    // (doo is reported, not asserted: the ocean PS's shadow term is 0.67 x t13.x x the blend on its sun term, which this
    // flat down-looking test frame does not light; the binding is checked through inputTexture above)
    CHECK_EQ(df, size_t(0));
    zeros->Release();
    ones->Release();
    fOne->Release();
}

C5R_TEST(renderer_missing_art_is_named) {
    const std::string exe = test::exePath();
    if (exe.empty()) test::skip("C5R_EXE not set");
    // an install root with Assets/ and Resource/ but no water art
    const std::string root = test::workDir() + "/water_empty_install";
    CreateDirectoryA(test::workDir().c_str(), nullptr);
    CreateDirectoryA(root.c_str(), nullptr);
    CreateDirectoryA((root + "/Assets").c_str(), nullptr);
    CreateDirectoryA((root + "/Resource").c_str(), nullptr);
    wtest::Device d;
    CHECK_OK(d.create(false));
    Renderer r;
    const Status st = r.init(d.dev, wide(exe), wide(root));
    CHECK_ERR(st, Err::NotFound);
    CHECK(st.msg.find("waterdepthcolor_new.dds") != std::string::npos);
}

C5R_TEST(game_art_gives_the_stock_fields) {
    const std::string game = test::gameDir();
    if (game.empty()) test::skip("C5R_GAME_DIR not set");
    Fixture fx(wide(game));
    if (!fx.ok) return;
    const WaterFields f = fx.r.stockFields();
    CHECK(f.valid);
    // +0x90 and +0x9C are unit vectors (normalized texels), +0x70 finite
    const float a = f.get(0x90), b = f.get(0x94), c = f.get(0x98);
    CHECK(approx(a * a + b * b + c * c, 1.0f, 1e-5f));
    CHECK(std::isfinite(f.get(0x70)) && std::isfinite(f.get(0x80)));
    std::printf("  watersky fields: +0x70 (%g %g %g) +0x80 (%g %g %g) +0x90 (%g %g %g) +0x9C (%g %g %g) +0xA8 (%g %g %g %g) +0xB8 (%g %g %g %g)\n",
                f.get(0x70), f.get(0x74), f.get(0x78), f.get(0x80), f.get(0x84), f.get(0x88), a, b, c, f.get(0x9C),
                f.get(0xA0), f.get(0xA4), f.get(0xA8), f.get(0xAC), f.get(0xB0), f.get(0xB4), f.get(0xB8), f.get(0xBC),
                f.get(0xC0), f.get(0xC4));
    CHECK_OK(fx.r.setHeightField(wtest::flatGrid(28, 16, 0)));
    wtest::Target t;
    CHECK_OK(t.create(fx.d.dev, 160, 120));
    FrameStats st;
    CHECK_OK(fx.r.draw(fx.d.ctx, t.rtv, nullptr, downInputs(fx.r, 160, 120), Options(), &st));
    CHECK(st.oceanDraws > 0);
}

C5R_TEST_MAIN
