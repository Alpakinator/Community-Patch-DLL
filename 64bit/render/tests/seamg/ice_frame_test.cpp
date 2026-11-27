// c5r::ice IceFrame tests (issue 4306, epic 0169): the row map of polar-ice.md 7 against task 4301's captured blocks
// (draws 171 / 86 of load 1, 191 / 88 of load 2; D137, offline) and against independent sources (the probe's object
// fields, IceSettings_Low.xml and Ice_Bump.lean of the install, the FOWAnim rule, the stock fog), plus synthetic cases
// for the map copies and every error path. Own code; the reference set and the install are read at run time.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "c5r/fow/fow.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/ice/ice.h"
#include "c5r/water/water.h"
#include "ice_util.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::ice;
using namespace c5r::itest;

namespace {

std::string hexf(float v) {
    uint32_t u;
    std::memcpy(&u, &v, 4);
    char b[64];
    std::snprintf(b, sizeof b, "%.9g (0x%08x)", v, u);
    return b;
}

bool bitEq(float a, float b) { return std::memcmp(&a, &b, 4) == 0; }

// ulps between two floats of the same sign
int64_t ulps(float a, float b) {
    int32_t ia, ib;
    std::memcpy(&ia, &a, 4);
    std::memcpy(&ib, &b, 4);
    return ia > ib ? static_cast<int64_t>(ia) - ib : static_cast<int64_t>(ib) - ia;
}

// The scene block of the same load as captured draw i (0, 1 -> 0; 2, 3 -> 2).
size_t sceneOf(size_t i) { return i < 2 ? 0 : 2; }

// A synthetic object and camera (no game data).
IceFields synthFields() {
    IceFields f;
    f.valid = true;
    for (uint32_t off = kFieldsBase; off < kFieldsEnd; off += 4) f.set(off, 0.0f);
    f.set(field::kLeanScale, 0.25f);
    f.set(field::kWaterHeight, 10.0f);
    f.set(field::kBox, -100.0f);
    f.set(field::kBox + 4, -50.0f);
    f.set(field::kBox + 8, 120.0f);
    f.set(field::kBox + 12, 60.0f);
    f.set(field::kDirA, 0.5f);
    f.set(field::kDirB + 8, 1.0f);
    f.set(field::kHeightAbove, 25.0f);
    f.set(field::kDepthBelow, 8.0f);
    f.set(field::kTextureRepeat, 0.005f);
    f.set(field::kBumpRepeat, 0.009f);
    f.set(field::kSpecularPower, 150.0f);
    f.set(field::kColorRamp, 0.1f);
    f.set(field::kAlphaRamp, 0.5f);
    return f;
}

FrameInputs synthInputs() {
    FrameInputs in;
    const float view[16] = {1, 0, 0, 0, 0, 0.8f, -0.6f, 0, 0, 0.6f, 0.8f, 0, -30, 20, -500, 1};
    const float proj[16] = {2, 0, 0, 0, 0, 2.5f, 0, 0, 0, 0, 1.001f, 1, 0, 0, -1.001f, 0};
    std::memcpy(in.cam.view, view, sizeof view);
    std::memcpy(in.cam.proj, proj, sizeof proj);
    in.cam.width = 64;
    in.cam.height = 32;
    const float op[16] = {1.0f / 416, 0, 0, 0, 0, 1.0f / 320, 0, 0, 0, 0, -0.5f, 0, -2.5f, 2.6f, 0.5f, 1};
    std::memcpy(in.overlayProj, op, sizeof op);
    in.worldWidth = 3583.0f;
    in.fowAnim[0] = in.fowAnim[1] = 7.0f / 3583.0f;
    in.fowAnim[2] = -105.0f;
    return in;
}

// XML attribute value as float (strtof of the text between the quotes), NaN when absent.
float xmlAttr(const std::string& xml, const std::string& name) {
    size_t at = 0;
    for (;;) {
        at = xml.find(name, at);
        if (at == std::string::npos) return NAN;
        size_t p = at + name.size();
        while (p < xml.size() && (xml[p] == ' ' || xml[p] == '\t')) ++p;
        if (p < xml.size() && xml[p] == '=' && (at == 0 || !std::isalnum(static_cast<unsigned char>(xml[at - 1])))) {
            const size_t q = xml.find('"', p);
            if (q == std::string::npos) return NAN;
            return std::strtof(xml.c_str() + q + 1, nullptr);
        }
        at = p;
    }
}

} // namespace

C5R_TEST(iceframe_mask_covers_the_draws_stores) {
    uint32_t stored = 0;
    for (uint32_t i = 0; i < kIceFrameBytes; ++i) stored += iceFrameStored(i) ? 1 : 0;
    CHECK_EQ(stored, kIceFrameBytes - 164u);   // polar-ice.md 7: 164 bytes (41 floats) not stored
    CHECK(iceFrameStored(0x000));
    CHECK(iceFrameStored(0x044));
    CHECK(!iceFrameStored(0x048));             // row 4.z
    CHECK(!iceFrameStored(0x0CC));             // row 12.w
    CHECK(iceFrameStored(0x1D8));              // row 29.z (stored 0)
    CHECK(!iceFrameStored(0x1E0));
}

// (a) The module's IceFrame from the capture's camera equals the captured block in every stored byte.
C5R_TEST(iceframe_equals_the_captured_draws) {
    size_t n = 0;
    const CapturedDraw* d = capturedDraws(&n);
    std::vector<std::vector<uint8_t>> blocks(n);
    for (size_t i = 0; i < n; ++i)
        if (!readRef(d[i].frame, blocks[i], kIceFrameBytes)) return;
    for (size_t i = 0; i < n; ++i) {
        const std::vector<uint8_t>& scene = blocks[sceneOf(i)];
        IceFields f;
        if (!tinyFields(scene, f)) return;
        const FrameInputs in = capturedInputs(blocks[i], scene);
        uint8_t out[kIceFrameBytes];
        CHECK_OK(computeIceFrame(f, in, 0, out));
        uint32_t first = 0;
        const uint32_t diff = maskedDiff(out, blocks[i].data(), &first);
        if (diff) {
            const uint32_t o = first & ~3u;
            test::fail(__FILE__, __LINE__, std::string(d[i].name) + ": " + std::to_string(diff) + " stored bytes differ, first at 0x" +
                                               [&] { char b[8]; std::snprintf(b, sizeof b, "%03x", o); return std::string(b); }() +
                                               ": module " + hexf(f32(std::vector<uint8_t>(out, out + kIceFrameBytes), o)) +
                                               ", capture " + hexf(f32(blocks[i], o)));
        }
        // the unstored bytes are 0 in the module's block
        for (uint32_t b = 0; b < kIceFrameBytes; ++b)
            if (!iceFrameStored(b)) CHECK_EQ(out[b], 0);
    }
    // the scene and reflection blocks of one load differ only in rows 0-3 and 12 (stored bytes)
    for (size_t s : {static_cast<size_t>(0), static_cast<size_t>(2)})
        for (uint32_t b = 0; b < kIceFrameBytes; ++b)
            if (iceFrameStored(b) && !(b < 0x40) && !(b >= 0xC0 && b < 0xCC)) CHECK_EQ(blocks[s][b], blocks[s + 1][b]);
}

// Rows checked against sources independent of the IceFrame.
C5R_TEST(iceframe_rows_equal_the_probe_fog_and_fowanim) {
    std::vector<uint8_t> scene, refl, js;
    if (!readRef("stock_load1/buffers/51172_9b6b504e.bin", scene, kIceFrameBytes)) return;
    if (!readRef("stock_load1/buffers/51180_70d5a20c.bin", refl, kIceFrameBytes)) return;
    IceFields f;
    if (!tinyFields(scene, f)) return;   // +0xFE80..+0xFF7F are the probe's bytes
    for (const std::vector<uint8_t>* b : {&scene, &refl}) {
        // rows 4, 5: the object's box +0xFF1C..+0xFF28; 14, 15: +0xFF38.., +0xFF44..; 16: +0xFF0C; 21: +0xFF14
        CHECK(bitEq(f32(*b, 0x40), f.get(0xFF1C)) && bitEq(f32(*b, 0x44), f.get(0xFF20)));
        CHECK(bitEq(f32(*b, 0x50), f.get(0xFF24)) && bitEq(f32(*b, 0x54), f.get(0xFF28)));
        for (uint32_t c = 0; c < 3; ++c) {
            CHECK(bitEq(f32(*b, 0xE0 + 4 * c), f.get(0xFF38 + 4 * c)));
            CHECK(bitEq(f32(*b, 0xF0 + 4 * c), f.get(0xFF44 + 4 * c)));
        }
        CHECK(bitEq(f32(*b, 0x100), f.get(0xFF0C)));
        CHECK(bitEq(f32(*b, 0x150), f.get(0xFF14)));
        // row 6.x = +0xFFC0 + +0xFFC4 with +0xFFC0 = the probe's +0xFF18 (25); row 7.x = water - 8
        CHECK(bitEq(f32(*b, 0x60), f.get(0xFF18) + 8.0f));
        CHECK(bitEq(f32(*b, 0x70), f.get(0xFF14) - 8.0f));
        // row 13.xy: FOWAnim's k of the Tiny world width
        float anim[4];
        fow::fowAnim(kTinyWorldWidth, 0.0, anim);
        CHECK(bitEq(f32(*b, 0xD0), anim[0]) && bitEq(f32(*b, 0xD4), anim[1]));
        CHECK(bitEq(f32(*b, 0xDC), 0.0f));
        // rows 28, 29.x: the stock DistanceFog colour and density
        const Fog fog;
        for (uint32_t c = 0; c < 4; ++c) CHECK(bitEq(f32(*b, 0x1C0 + 4 * c), fog.color[c]));
        CHECK(bitEq(f32(*b, 0x1D0), -fog.density));
        CHECK(bitEq(f32(*b, 0x1D8), 0.0f));
    }
    // row 29.y of the reflection draw is the scene camera's (z 424.26), not its own (z 3535.5)
    CHECK(bitEq(f32(refl, 0x1D4), f32(scene, 0x1D4)));
    {
        FrameInputs own = capturedInputs(refl, scene);
        own.fogCamZ = f32(refl, 0xC8);
        uint8_t out[kIceFrameBytes];
        CHECK_OK(computeIceFrame(f, own, 0, out));
        float y;
        std::memcpy(&y, out + 0x1D4, 4);
        CHECK(!bitEq(y, f32(refl, 0x1D4)));
        CHECK(std::fabs(y) < 1e-30f);   // exp(-176.8): about 1e-77 * 1e-3, 0 in float
    }
    // row 29.w: the zoom fraction of the camera height, to the controller's precision [I] (water.h zoomFraction)
    CHECK(std::fabs(water::zoomFraction(f32(scene, 0xC8)) - f32(scene, 0x1DC)) < 1e-3f);
}

// Rows 6, 7, 17-27 against IceSettings_Low.xml of the install (the instances' choice, polar-ice.md 1 I8) and row 16
// against Ice_Bump.lean's +0x04 float.
C5R_TEST(iceframe_settings_rows_equal_the_install_files) {
    const std::string game = test::gameDir();
    if (game.empty()) test::skip("C5R_GAME_DIR not set: IceSettings_Low.xml and Ice_Bump.lean come from the install");
    std::vector<uint8_t> scene;
    if (!readRef("stock_load1/buffers/51172_9b6b504e.bin", scene, kIceFrameBytes)) return;
    gamefs::GameFiles fs;
    CHECK_OK(fs.open(wide(game)));
    std::vector<uint8_t> xb, lean;
    CHECK_OK(fs.read("IceSettings_Low.xml", xb));
    CHECK_OK(fs.read("Ice_Bump.lean", lean));
    const std::string x(xb.begin(), xb.end());
    auto row = [&](uint32_t r, uint32_t c) { return f32(scene, r * 16 + c * 4); };
    CHECK(bitEq(row(7, 0), row(21, 0) - xmlAttr(x, "LargePolyDepth")));
    CHECK(bitEq(row(17, 0), xmlAttr(x, "TextureRepeat")));
    CHECK(bitEq(row(18, 0), xmlAttr(x, "BumpRepeat")));
    CHECK(bitEq(row(19, 0), xmlAttr(x, "SpecularPower")));
    CHECK(bitEq(row(22, 0), xmlAttr(x, "SideTextureRepeat_U")));
    CHECK(bitEq(row(22, 1), xmlAttr(x, "SideTextureRepeat_V")));
    CHECK(bitEq(row(23, 0), xmlAttr(x, "SideBumpRepeat_U")));
    CHECK(bitEq(row(23, 1), xmlAttr(x, "SideBumpRepeat_V")));
    CHECK(bitEq(row(26, 0), 1.0f / xmlAttr(x, "Underwater_ColorRamp")));
    CHECK(bitEq(row(27, 0), 1.0f / xmlAttr(x, "Underwater_AlphaRamp")));
    const char* sub[3] = {"Subsurface_R", "Subsurface_G", "Subsurface_B"};
    const char* uw1[3] = {"Underwater_R1", "Underwater_G1", "Underwater_B1"};
    const char* uw2[3] = {"Underwater_R2", "Underwater_G2", "Underwater_B2"};
    for (uint32_t c = 0; c < 3; ++c) {
        // (c / 255)^2.2; the EXE's own pow is not traced: within 2 ulp
        const float s = static_cast<float>(std::pow(xmlAttr(x, sub[c]) / 255.0, 2.2));
        const float u1 = static_cast<float>(std::pow(xmlAttr(x, uw1[c]) / 255.0, 2.2));
        const float u2 = static_cast<float>(std::pow(xmlAttr(x, uw2[c]) / 255.0, 2.2));
        CHECK(ulps(row(20, c), s) <= 2);
        CHECK(ulps(row(24, c), u1) <= 2);
        CHECK(ulps(row(25, c), u2) <= 2);
        if (ulps(row(20, c), s) > 2) test::fail(__FILE__, __LINE__, "row 20: " + hexf(row(20, c)) + " vs " + hexf(s));
    }
    // row 16.x = +0xFF0C = the .lean file's +0x04 float
    CHECK(lean.size() > 8);
    float scale = 0;
    std::memcpy(&scale, lean.data() + 4, 4);
    CHECK(bitEq(row(16, 0), scale));
}

// The map copies (synthetic): T_k = Translation(+W) for k = 1, (-W) for k = 2.
C5R_TEST(iceframe_map_copies) {
    const IceFields f = synthFields();
    FrameInputs in = synthInputs();
    in.haveCamPos = true;
    in.camPos[0] = 30.0f;
    in.camPos[1] = 400.0f;
    in.camPos[2] = 300.0f;
    uint8_t b0[kIceFrameBytes], b1[kIceFrameBytes], b2[kIceFrameBytes];
    CHECK_OK(computeIceFrame(f, in, 0, b0));
    CHECK_OK(computeIceFrame(f, in, 1, b1));
    CHECK_OK(computeIceFrame(f, in, 2, b2));
    const std::vector<uint8_t> v0(b0, b0 + kIceFrameBytes), v1(b1, b1 + kIceFrameBytes), v2(b2, b2 + kIceFrameBytes);
    const float W = in.worldWidth;
    CHECK(bitEq(f32(v1, 0xC0), -W + 30.0f));
    CHECK(bitEq(f32(v2, 0xC0), 30.0f + W));
    CHECK(bitEq(f32(v1, 0xC4), 400.0f) && bitEq(f32(v2, 0xC8), 300.0f));
    // rows 8-11: T_k * O = O with row 3 += shift * row 0
    for (uint32_t j = 0; j < 4; ++j) {
        CHECK(std::fabs(f32(v1, 0xB0 + 4 * j) - (in.overlayProj[12 + j] + W * in.overlayProj[j])) < 1e-6f);
        CHECK(std::fabs(f32(v2, 0xB0 + 4 * j) - (in.overlayProj[12 + j] - W * in.overlayProj[j])) < 1e-6f);
    }
    // rows 0-3: (T_k V) P against a double product, 2 ulp relative (or 1e-6 absolute near 0)
    for (uint32_t k = 0; k < 3; ++k) {
        const std::vector<uint8_t>& v = k == 0 ? v0 : (k == 1 ? v1 : v2);
        const double s = k == 0 ? 0.0 : (k == 1 ? W : -W);
        double tv[16];
        for (int i = 0; i < 16; ++i) tv[i] = in.cam.view[i];
        for (int j = 0; j < 4; ++j) tv[12 + j] += s * in.cam.view[j];
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                double e = 0;
                for (int t = 0; t < 4; ++t) e += tv[i * 4 + t] * in.cam.proj[t * 4 + j];
                const float got = f32(v, static_cast<size_t>(4 * (i * 4 + j)));
                CHECK(std::fabs(got - e) <= std::max(1e-6, std::fabs(e) * 2.4e-7));
            }
    }
    // the rest is the same in every copy
    for (uint32_t b = 0x40; b < kIceFrameBytes; ++b) {
        if ((b >= 0x80 && b < 0xC0) || (b >= 0xC0 && b < 0xCC)) continue;
        CHECK_EQ(b0[b], b1[b]);
        CHECK_EQ(b0[b], b2[b]);
    }
    // the camera from the view's inverse when not given: (30, 400, 300) for this view
    FrameInputs inv = synthInputs();
    const float view[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -30, -400, -300, 1};
    std::memcpy(inv.cam.view, view, sizeof view);
    CHECK_OK(computeIceFrame(f, inv, 0, b0));
    const std::vector<uint8_t> vi(b0, b0 + kIceFrameBytes);
    CHECK(bitEq(f32(vi, 0xC0), 30.0f) && bitEq(f32(vi, 0xC4), 400.0f) && bitEq(f32(vi, 0xC8), 300.0f));
    // fog w < 0: the zoom fraction of the fog camera z
    CHECK(bitEq(f32(vi, 0x1DC), water::zoomFraction(300.0f)));
}

C5R_TEST(iceframe_cull_box_per_copy) {
    FrameInputs in = synthInputs();
    in.haveCullBox = true;
    in.cullBox[0] = 500;
    in.cullBox[1] = -900;
    in.cullBox[2] = 1700;
    in.cullBox[3] = -400;
    float b[4];
    copyCullBox(in, 0, b);
    CHECK(b[0] == 500 && b[2] == 1700 && b[1] == -900 && b[3] == -400);
    copyCullBox(in, 1, b);
    CHECK(bitEq(b[0], -3583.0f + 500.0f) && bitEq(b[2], -3583.0f + 1700.0f) && b[1] == -900);
    copyCullBox(in, 2, b);
    CHECK(bitEq(b[0], 500.0f + 3583.0f) && bitEq(b[2], 1700.0f + 3583.0f));
    Chunk c;
    c.box[0] = -1793;
    c.box[1] = -1098.5f;
    c.box[2] = 1823.9f;
    c.box[3] = 1043.1f;
    copyCullBox(in, 0, b);
    CHECK(chunkVisible(c, b));
    copyCullBox(in, 1, b);
    CHECK(!chunkVisible(c, b));   // box max x -1883 < chunk min x
    copyCullBox(in, 2, b);
    CHECK(!chunkVisible(c, b));
    // edges inclusive
    const float touch[4] = {1823.9f, 1043.1f, 2000.0f, 2000.0f};
    CHECK(chunkVisible(c, touch));
    const float past[4] = {1824.0f, 0.0f, 2000.0f, 2000.0f};
    CHECK(!chunkVisible(c, past));
}

// Every error path of computeIceFrame, with the output untouched by nothing but the memset of a good call.
C5R_TEST(iceframe_errors) {
    const IceFields good = synthFields();
    const FrameInputs in = synthInputs();
    uint8_t out[kIceFrameBytes];
    CHECK_ERR(computeIceFrame(good, in, 0, nullptr), Err::BadLayout);
    CHECK_ERR(computeIceFrame(good, in, 3, out), Err::BadLayout);
    IceFields invalid = good;
    invalid.valid = false;
    CHECK_ERR(computeIceFrame(invalid, in, 0, out), Err::NotFound);
    IceFields nan = good;
    nan.set(field::kSpecularPower, NAN);
    CHECK_ERR(computeIceFrame(nan, in, 0, out), Err::BadLayout);
    nan = good;
    nan.set(field::kBox + 8, INFINITY);
    CHECK_ERR(computeIceFrame(nan, in, 0, out), Err::BadLayout);
    FrameInputs bad = in;
    bad.cam.proj[5] = NAN;
    CHECK_ERR(computeIceFrame(good, bad, 0, out), Err::BadLayout);
    bad = in;
    bad.overlayProj[12] = INFINITY;
    CHECK_ERR(computeIceFrame(good, bad, 0, out), Err::BadLayout);
    bad = in;
    bad.fowAnim[2] = NAN;
    CHECK_ERR(computeIceFrame(good, bad, 0, out), Err::BadLayout);
    bad = in;
    bad.fog.color[1] = NAN;
    CHECK_ERR(computeIceFrame(good, bad, 0, out), Err::BadLayout);
    bad = in;
    bad.fog.scale = INFINITY;
    CHECK_ERR(computeIceFrame(good, bad, 0, out), Err::BadLayout);
    bad = in;
    bad.haveFogCamZ = true;
    bad.fogCamZ = NAN;
    CHECK_ERR(computeIceFrame(good, bad, 0, out), Err::BadLayout);
    bad = in;
    bad.haveCamPos = true;
    bad.camPos[1] = NAN;
    CHECK_ERR(computeIceFrame(good, bad, 0, out), Err::BadLayout);
    bad = in;
    bad.worldWidth = 0.0f;
    CHECK_OK(computeIceFrame(good, bad, 0, out));             // copy 0 does not need W
    CHECK_ERR(computeIceFrame(good, bad, 1, out), Err::BadLayout);
    CHECK_ERR(computeIceFrame(good, bad, 2, out), Err::BadLayout);
    bad = in;
    std::memset(bad.cam.view, 0, sizeof bad.cam.view);       // singular view, no camPos
    CHECK_ERR(computeIceFrame(good, bad, 0, out), Err::BadLayout);
    // IceFields accessors outside the range read 0 and write nothing
    IceFields g = good;
    CHECK_EQ(g.get(kFieldsBase - 4), 0.0f);
    CHECK_EQ(g.get(kFieldsEnd), 0.0f);
    CHECK_EQ(g.get(field::kBox + 1), 0.0f);
    g.set(kFieldsEnd, 5.0f);
    CHECK(std::memcmp(g.raw, good.raw, kFieldsSize) == 0);
}

C5R_TEST_MAIN
