// test_inputs.cpp - ticket 4882: the BuildingShader PS inputs read from the install (c5r/landmarks/inputs.h) and the
// frame's camera in the drawn records (applyFrameCamera). The install, the database and the EXE are read at run time
// (skipped without them, which build.py counts as a failure); nothing of the game is in the repository (D83).
#include <d3d11.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>

#include "c5r/gamefs/gamefiles.h"
#include "c5r/landmarks/draw.h"
#include "c5r/landmarks/inputs.h"
#include "landmarks_testkit.h"

using namespace c5r;
using namespace c5r::landmarks;

namespace {

struct Device {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    Device() {
        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0, got;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev,
                                     &got, &ctx)))
            D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, &got, &ctx);
    }
    ~Device() {
        if (ctx) ctx->Release();
        if (dev) dev->Release();
    }
};

bool closeTo(double a, double b, double eps = 1e-3) { return std::fabs(a - b) <= eps; }

// improvements_cubelight.dds's identity (render/evidence/0561/4873/env_sets.txt: the tap's t2 of the instanced landmarks)
const uint64_t kImprovementsCubelight = 0xd56f3e9837131711ull;

}  // namespace

C5R_TEST(cube_names) {
    CHECK(envCubeCandidate("improvements_cubelight.dds"));
    CHECK(envCubeCandidate("hiconreflmap_soft.dds"));
    CHECK(envCubeCandidate("unit_environment_dull.dds"));
    CHECK(envCubeCandidate("resource_irradiance.dds"));
    CHECK(!envCubeCandidate("improvements_cubelight.tga"));
    CHECK(!envCubeCandidate("euro_anc_city_diff.dds"));
    CHECK(!envCubeCandidate(".dds"));
}

// The frame's camera replaces rows 0-3 (and 8-12 with the fog) of exactly the target's VS cb1 constants, each once;
// the shift is the instances' origins moved between the tap's camera and the frame's, in pixels.
C5R_TEST(frame_camera_rows) {
    ModelScene s;
    s.targets.push_back(TapTarget{1000, 500, 1, 28, 1000, 500, 1});
    s.targets.push_back(TapTarget{256, 256, 1, 28, 256, 256, 1});
    // rows 0-7: two instances (four rows each, the origin in row 3); 8: one skinned row block start; 20..: the constants
    s.rows.resize(20 + 13 + 13);
    for (int i = 0; i < 2; ++i) {
        s.rows[i * 4 + 0] = {1, 0, 0, 0};
        s.rows[i * 4 + 1] = {0, 1, 0, 0};
        s.rows[i * 4 + 2] = {0, 0, 1, 0};
        s.rows[i * 4 + 3] = {0.2f * i, 0.1f, 0.5f, 1};
    }
    // constant 0 (target 0) and 1 (target 1): the tap's camera = the identity, rows 8-12 marked
    for (int c = 0; c < 2; ++c) {
        const uint32_t f = 20 + 13 * c;
        s.rows[f + 0] = {1, 0, 0, 0};
        s.rows[f + 1] = {0, 1, 0, 0};
        s.rows[f + 2] = {0, 0, 1, 0};
        s.rows[f + 3] = {0, 0, 0, 1};
        for (int k = 4; k < 13; ++k) s.rows[f + k] = {7, 7, 7, 7};
        s.consts.push_back(TapConst{f, 13, 1, 1});
    }
    DrawItem a;
    a.material = Material::Instanced;
    a.instanceCount = 2;
    a.instRow = 0;
    a.vsCb1 = 0;
    a.target = 0;
    DrawItem b = a;   // a second item on the same constant: replaced once, shifted again
    b.instanceCount = 1;
    DrawItem sk = a;   // skinned: its rows are palettes, no origin
    sk.material = Material::Skinned;
    sk.instRow = 8;
    DrawItem other = a;
    other.vsCb1 = 1;
    other.target = 1;
    s.items = {a, b, sk, other};
    TapCameraRows tap;
    keepTapCamera(s, tap);
    CHECK_EQ(tap.size(), 2u);
    // the frame's camera: x moved by 0.1 in clip space (w = 1): 0.5 x 1000 x 0.1 = 50 px per origin
    const float vp[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.1f, 0, 0, 1};
    FrameCamera fc;
    fc.viewProj = vp;
    fc.width = 1000;
    fc.height = 500;
    CameraShift sh;
    applyFrameCamera(s, 0, fc, tap, &sh);
    CHECK_EQ(sh.consts, 1u);
    CHECK_EQ(sh.n, 3u);   // a's two instances and b's one; not the skinned item, not the other target
    CHECK(closeTo(sh.maxPx, 50.0, 1e-2) && closeTo(sh.sumPx, 150.0, 1e-2));
    CHECK(closeTo(s.rows[20 + 3].x, 0.1f));
    CHECK(closeTo(s.rows[20 + 8].x, 7.0f));   // no fog given: rows 8-12 kept
    CHECK(closeTo(s.rows[33 + 3].x, 0.0f));   // the other target's constant untouched
    // again (a later frame with the same tap): the shift is still against the tap's own rows
    const float vp2[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0.2f, 0, 1};
    const float ov[16] = {0.5f, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 1, 0, 0.25f, 0.25f, 0, 1};
    const float anim[4] = {0.002f, 0.002f, -3.0f, 0};
    fc.viewProj = vp2;
    fc.overlayProj = ov;
    fc.fowAnim = anim;
    CameraShift sh2;
    applyFrameCamera(s, 0, fc, tap, &sh2);
    CHECK(closeTo(sh2.maxPx, 50.0, 1e-2));   // 0.5 x 500 x 0.2
    CHECK(closeTo(s.rows[20 + 3].y, 0.2f) && closeTo(s.rows[20 + 3].x, 0.0f));
    CHECK(closeTo(s.rows[20 + 8].x, 0.5f) && closeTo(s.rows[20 + 11].x, 0.25f) && closeTo(s.rows[20 + 12].z, -3.0f));
    CHECK(closeTo(s.rows[20 + 4].x, 7.0f));   // rows 4-7 never touched
    // the tap's own camera: no shift (the check the live run reads while the camera is still)
    const float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    fc.viewProj = id;
    CameraShift sh3;
    applyFrameCamera(s, 0, fc, tap, &sh3);
    CHECK(sh3.n == 3 && closeTo(sh3.maxPx, 0.0, 1e-6));
    // no camera: nothing replaced, nothing counted
    fc.viewProj = nullptr;
    CameraShift sh4;
    applyFrameCamera(s, 1, fc, tap, &sh4);
    CHECK(sh4.consts == 0 && sh4.n == 0);
}

// Ticket 0598: with an eye, row 16 of exactly the target's PS cb1 constants (each once, file and CPU-built items) is the
// frame's eye (x, y, z, 1); rows 17-21 and the VS rows stay as before. Without an eye row 16 keeps the tap's (one Present
// old under lockstep, city-renderer.md 15.2), which the frame's-eye check below rejects.
C5R_TEST(frame_camera_eye) {
    const Float4 tapEye{735.95f, -782.05f, 350.73f, 1.0f};    // the tapped frame's eye (P6, 15.2)
    const float frameEye[3] = {741.95f, -782.05f, 350.73f};   // the frame's: one pan step east
    ModelScene s;
    s.targets.push_back(TapTarget{1000, 500, 1, 28, 1000, 500, 1});
    s.targets.push_back(TapTarget{1024, 1024, 1, 28, 1024, 1024, 1});
    s.rows.resize(3 * 22 + 4);
    for (int c = 0; c < 3; ++c) {
        const uint32_t f = 22 * c;
        for (int k = 0; k < 22; ++k) s.rows[f + k] = {9, 9, 9, 9};
        s.rows[f + 16] = tapEye;
        s.consts.push_back(TapConst{f, 22, 1, 1});
    }
    s.consts.push_back(TapConst{66, 4, 1, 1});   // constant 3: too short for row 16 (never written)
    DrawItem a;
    a.material = Material::Instanced;
    a.vsCb1 = 0;
    a.psCb1 = 0;   // VS and PS bind one constant (15.2)
    a.target = 0;
    DrawItem b = a;   // the same constant again: written once
    DrawItem shortPs = a;
    shortPs.psCb1 = 3;
    DrawItem refl = a;   // the reflection target's own constant
    refl.vsCb1 = refl.psCb1 = 1;
    refl.target = 1;
    s.items = {a, b, shortPs, refl};
    CpuDrawItem cpu;
    cpu.vsCb1 = cpu.psCb1 = 2;
    cpu.target = 0;
    s.cpuItems = {cpu};
    TapCameraRows tap;
    keepTapCamera(s, tap);
    const auto isFrameEye = [&](uint32_t c) {
        const Float4& r = s.rows[s.consts[c].firstRow + 16];
        return r.x == frameEye[0] && r.y == frameEye[1] && r.z == frameEye[2] && r.w == 1.0f;
    };
    const float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    FrameCamera fc;
    fc.viewProj = id;
    fc.width = 1000;
    fc.height = 500;
    CameraShift sh0;
    applyFrameCamera(s, 0, fc, tap, &sh0);   // no eye: the tapped eye stays, which the frame's-eye check rejects
    CHECK(!isFrameEye(0) && !isFrameEye(2) && sh0.eyeConsts == 0u);
    fc.eye = frameEye;
    CameraShift sh;
    applyFrameCamera(s, 0, fc, tap, &sh);
    CHECK(isFrameEye(0) && isFrameEye(2));
    CHECK_EQ(sh.eyeConsts, 2u);   // constants 0 and 2; 3 is too short, 1 is the other target's
    CHECK(!isFrameEye(1));
    CHECK(closeTo(s.rows[17].x, 9.0f) && closeTo(s.rows[15].x, 9.0f) && closeTo(s.rows[66].x, 0.0f));
    applyFrameCamera(s, 1, fc, tap, &sh);   // the reflection pass: the same main-view eye (stock, 15.2)
    CHECK(isFrameEye(1));

    // The tapped camera is the frame's (a still camera): the tap's own row 16 stays, bit for bit (the EXE's exact eye;
    // the server's differs in the last bits, city-renderer.md 15.8); moved: the frame's eye; still again: the tap's back.
    ModelScene q;
    q.targets.push_back(TapTarget{1000, 500, 1, 28, 1000, 500, 1});
    q.rows.resize(22);
    const float vpTap[16] = {1.2f, 0, 0, 0, 0, 0.9f, 0.3f, 0.3f, 0, 0.4f, -0.9f, -0.9f, -900.0f, 400.0f, 1300.0f, 1350.0f};
    for (int k = 0; k < 4; ++k) q.rows[k] = Float4{vpTap[k * 4], vpTap[k * 4 + 1], vpTap[k * 4 + 2], vpTap[k * 4 + 3]};
    const Float4 exeEye{991.9500122f, -794.1900024f, 473.7000122f, 1.0f};
    q.rows[16] = exeEye;
    q.consts.push_back(TapConst{0, 22, 1, 1});
    DrawItem qi;
    qi.material = Material::Instanced;
    qi.vsCb1 = qi.psCb1 = 0;
    qi.target = 0;
    q.items = {qi};
    TapCameraRows qtap;
    keepTapCamera(q, qtap);
    const float serverEye[3] = {991.9501f, -794.1899f, 473.7001f};   // the same camera's eye, last bits apart
    FrameCamera qc;
    qc.viewProj = vpTap;
    qc.eye = serverEye;
    qc.width = 1000;
    qc.height = 500;
    CameraShift k1;
    applyFrameCamera(q, 0, qc, qtap, &k1);
    CHECK(std::memcmp(&q.rows[16], &exeEye, sizeof(Float4)) == 0 && k1.eyeKept == 1u && k1.eyeConsts == 0u);
    float vpMoved[16];
    std::memcpy(vpMoved, vpTap, sizeof(vpMoved));
    vpMoved[12] -= 6.0f * 1.2f;   // one pan step
    qc.viewProj = vpMoved;
    CameraShift k2;
    applyFrameCamera(q, 0, qc, qtap, &k2);
    CHECK(q.rows[16].x == serverEye[0] && q.rows[16].y == serverEye[1] && k2.eyeConsts == 1u && k2.eyeKept == 0u);
    qc.viewProj = vpTap;
    CameraShift k3;
    applyFrameCamera(q, 0, qc, qtap, &k3);
    CHECK(std::memcmp(&q.rows[16], &exeEye, sizeof(Float4)) == 0 && k3.eyeKept == 1u);
}

// Ticket 0652 (city-renderer.md 17): on a wrapping map the EXE draws the models it sees through a wrap view with that
// view's camera (rows 0-3 = view 0's moved by k W in world x). Each constant is given the wrap copy of the frame's camera
// it was tapped through: rows 0-3, the OverlayProj rows 8-11 and the eye row 16 of that copy; a constant that fits no copy
// draws nothing (rows 0-3 zero) and is counted; without a wrap period everything is view 0's, as before.
namespace {
const float kW = 5567.0f;   // 87 x 64 - 1: the large 43-civ save's wrap period (terrain-decals.md 21.3)
// a perspective view-projection in the row-vector form of VS cb1 rows 0-3 (x scale in row 0 only, as the captures')
const float kVp[16] = {2.135113f, 0, 0, 0, 0, 1.4f, 0.55f, 0.55f, 0, 0.9f, -0.83f, -0.83f, -60.0f, -900.0f, 1170.0f, 1210.0f};
void moved(const float* m, float shift, float* out) {
    std::memcpy(out, m, 16 * sizeof(float));
    for (int j = 0; j < 4; ++j) out[12 + j] = static_cast<float>(static_cast<double>(shift) * m[j] + m[12 + j]);
}
void putRows(ModelScene& s, uint32_t first, const float* m) {
    for (int k = 0; k < 4; ++k) s.rows[first + k] = Float4{m[k * 4], m[k * 4 + 1], m[k * 4 + 2], m[k * 4 + 3]};
}
bool rowsEqual(const ModelScene& s, uint32_t first, const float* m, double eps) {
    for (int k = 0; k < 4; ++k) {
        const Float4& r = s.rows[first + k];
        if (!closeTo(r.x, m[k * 4], eps) || !closeTo(r.y, m[k * 4 + 1], eps) || !closeTo(r.z, m[k * 4 + 2], eps) ||
            !closeTo(r.w, m[k * 4 + 3], eps))
            return false;
    }
    return true;
}
}  // namespace

C5R_TEST(wrap_copy_of) {
    Float4 now[4], tap[4];
    for (int k = 0; k < 4; ++k) now[k] = Float4{kVp[k * 4], kVp[k * 4 + 1], kVp[k * 4 + 2], kVp[k * 4 + 3]};
    const auto at = [&](double worldShift) {   // the tap's rows = now moved by worldShift
        float m[16];
        moved(kVp, static_cast<float>(worldShift), m);
        for (int k = 0; k < 4; ++k) tap[k] = Float4{m[k * 4], m[k * 4 + 1], m[k * 4 + 2], m[k * 4 + 3]};
        return wrapCopyOf(tap, now, kW);
    };
    CHECK_EQ(at(0.0), 0);
    CHECK_EQ(at(-kW), -1);
    CHECK_EQ(at(kW), 1);
    CHECK_EQ(at(-kW + 6.0), -1);       // a pan step besides the copy
    CHECK_EQ(at(0.24 * kW), 0);        // within a quarter period of copy 0
    CHECK_EQ(at(0.26 * kW), kWrapUnfit);
    CHECK_EQ(at(0.76 * kW), 1);        // within a quarter period of copy +1
    CHECK_EQ(at(-0.5 * kW), kWrapUnfit);
    CHECK_EQ(at(2.0 * kW), kWrapUnfit);  // two periods: the EXE has no such copy
    CHECK_EQ(wrapCopyOf(tap, now, 0.0f), 0);   // no period: always view 0 (the rule before 0652)
    CHECK_EQ(wrapCopyOf(tap, now, -1.0f), 0);
    CHECK_EQ(wrapCopyOf(tap, now, std::nanf("")), 0);
    CHECK_EQ(wrapCopyOf(nullptr, now, kW), 0);
    // an orthographic matrix with small scales (the reflection camera's form, city-renderer.md 15.2)
    const float ortho[16] = {0.001724f, -0.000304f, 2e-06f, 0, 0, 0.0017f, 0.0001f, 0, 0, 0.0002f, -0.0001f, 0,
                             -1.5995f, 0.9717f, 0.4191f, 1.0f};
    for (int k = 0; k < 4; ++k) now[k] = Float4{ortho[k * 4], ortho[k * 4 + 1], ortho[k * 4 + 2], ortho[k * 4 + 3]};
    float om[16];
    moved(ortho, -kW + 3.0f, om);
    for (int k = 0; k < 4; ++k) tap[k] = Float4{om[k * 4], om[k * 4 + 1], om[k * 4 + 2], om[k * 4 + 3]};
    CHECK_EQ(wrapCopyOf(tap, now, kW), -1);
    // a degenerate row 0 or a non-finite translation: view 0
    Float4 zero[4] = {};
    CHECK_EQ(wrapCopyOf(tap, zero, kW), 0);
    tap[3].x = std::nanf("");
    CHECK_EQ(wrapCopyOf(tap, now, kW), 0);
}

// Ticket 0659: the copy a far jump's constant takes (the nearest, -2 to +2), and the fault beyond
C5R_TEST(nearest_wrap_copy_of) {
    Float4 now[4], tap[4];
    for (int k = 0; k < 4; ++k) now[k] = Float4{kVp[k * 4], kVp[k * 4 + 1], kVp[k * 4 + 2], kVp[k * 4 + 3]};
    const auto at = [&](double worldShift) {
        float m[16];
        moved(kVp, static_cast<float>(worldShift), m);
        for (int k = 0; k < 4; ++k) tap[k] = Float4{m[k * 4], m[k * 4 + 1], m[k * 4 + 2], m[k * 4 + 3]};
        return nearestWrapCopyOf(tap, now, kW);
    };
    CHECK_EQ(at(0.0), 0);
    CHECK_EQ(at(-kW), -1);
    CHECK_EQ(at(0.26 * kW), 0);          // unfit for wrapCopyOf, nearest copy 0
    CHECK_EQ(at(0.74 * kW), 1);
    CHECK_EQ(at(-0.6 * kW), -1);
    CHECK_EQ(at(1.6 * kW), 2);           // a tap through copy +1 and a jump of 0.6 W
    CHECK_EQ(at(-2.4 * kW), -2);
    CHECK_EQ(at(2.6 * kW), kWrapUnfit);  // no camera of this map: the fault
    CHECK_EQ(at(-3.0 * kW), kWrapUnfit);
    CHECK_EQ(nearestWrapCopyOf(tap, now, 0.0f), 0);
    CHECK_EQ(nearestWrapCopyOf(tap, now, std::nanf("")), 0);
    CHECK_EQ(nearestWrapCopyOf(nullptr, now, kW), 0);
    Float4 zero[4] = {};
    CHECK_EQ(nearestWrapCopyOf(tap, zero, kW), 0);
}

C5R_TEST(frame_camera_wrap) {
    // constants 0..3 (22 rows each, target 0): tapped through view 0, wrap copy -1, wrap copy +1, and one that fits no copy
    // within a quarter period (half a period off: a far jump, drawn at the nearest copy since 0659); constant 1 carries an
    // instance near the east edge seen through copy -1
    const float tapShift[4] = {0.0f, -kW, kW, 0.5f * kW};
    const float eye0[3] = {1500.0f, -700.0f, 450.0f};   // the tapped frame's view-0 eye (row 16 as tapped, per view)
    ModelScene s;
    s.targets.push_back(TapTarget{1000, 500, 1, 28, 1000, 500, 1});
    const uint32_t instRow = 4 * 22;
    s.rows.resize(instRow + 4);
    for (int c = 0; c < 4; ++c) {
        const uint32_t f = 22 * c;
        for (int k = 0; k < 22; ++k) s.rows[f + k] = {9, 9, 9, 9};
        float m[16];
        moved(kVp, tapShift[c], m);
        putRows(s, f, m);
        s.rows[f + 16] = Float4{eye0[0] - tapShift[c], eye0[1], eye0[2], 1.0f};   // the EXE's per-view eye [I]
        s.consts.push_back(TapConst{f, 22, 1, 1});
    }
    // one instance at world x = kW - 40 (the last column), drawn by the EXE through copy -1 (constant 1)
    s.rows[instRow + 0] = {1, 0, 0, 0};
    s.rows[instRow + 1] = {0, 1, 0, 0};
    s.rows[instRow + 2] = {0, 0, 1, 0};
    s.rows[instRow + 3] = {kW - 40.0f, 600.0f, 10.0f, 1};
    std::vector<DrawItem> items;
    for (uint32_t c = 0; c < 4; ++c) {
        DrawItem it;
        it.material = Material::Instanced;
        it.instanceCount = c == 1 ? 1u : 0u;
        it.instRow = instRow;
        it.vsCb1 = it.psCb1 = c;
        it.target = 0;
        items.push_back(it);
    }
    s.items = items;
    const ModelScene tapped = s;
    TapCameraRows tap;
    keepTapCamera(s, tap);
    const float ov[16] = {0.0017f, 0, 0, 0, 0, -0.0017f, 0, 0, 0, 0, 1, 0, -0.1f, 0.2f, 0, 1};
    float ovM[16], ovP[16];   // the EXE's OverlayProj of copies -1 and +1 (as the server's OverlayCopiesFromFrame)
    moved(ov, -kW, ovM);
    moved(ov, kW, ovP);
    ovM[14] = 0.4999847f;     // the EXE's wrap view differs in z / w (srv_render_scene.cpp): taken as given
    const float anim[4] = {0.002f, 0.002f, -3.0f, 0};

    // (a) the frame one pan step (6 world units) east of the tap: every constant gets its own copy of the frame's camera
    float frame[16];
    moved(kVp, 6.0f, frame);
    const float frameEye[3] = {eye0[0] - 6.0f, eye0[1], eye0[2]};
    FrameCamera fc;
    fc.viewProj = frame;
    fc.overlayProj = ov;
    fc.fowAnim = anim;
    fc.eye = frameEye;
    fc.width = 1000;
    fc.height = 500;
    fc.wrapPeriod = kW;
    fc.overlayProjWrap[0] = ovM;
    fc.overlayProjWrap[1] = ovP;
    CameraShift sh;
    applyFrameCamera(s, 0, fc, tap, &sh);
    CHECK_EQ(sh.consts, 4u);
    CHECK_EQ(sh.wrapConsts, 2u);
    CHECK_EQ(sh.farConsts, 1u);     // 0659: half a period less a pan step -> the nearest copy, 0
    CHECK_EQ(sh.unfitConsts, 0u);
    float fM[16], fP[16];
    moved(frame, -kW, fM);
    moved(frame, kW, fP);
    CHECK(rowsEqual(s, 0, frame, 1e-3));
    CHECK(rowsEqual(s, 22, fM, 1e-2));
    CHECK(rowsEqual(s, 44, fP, 1e-2));
    CHECK(rowsEqual(s, 8, ov, 1e-6));        // rows 8-11: copy 0's OverlayProj
    CHECK(rowsEqual(s, 22 + 8, ovM, 1e-6));  // copy -1: the EXE's view as given
    CHECK(rowsEqual(s, 44 + 8, ovP, 1e-6));
    CHECK(closeTo(s.rows[22 + 12].z, -3.0f) && closeTo(s.rows[44 + 12].z, -3.0f));   // FOWAnim the same for every copy
    // the eye (the camera moved: the frame's eye, moved by -k W)
    CHECK(closeTo(s.rows[16].x, frameEye[0]) && closeTo(s.rows[22 + 16].x, frameEye[0] + kW, 1e-2) &&
          closeTo(s.rows[44 + 16].x, frameEye[0] - kW, 1e-2) && closeTo(s.rows[44 + 16].y, frameEye[1]));
    CHECK_EQ(sh.eyeConsts, 4u);
    // the far constant (0659): drawn at the nearest copy (0 here): rows 0-3 the frame's, rows 8-11 copy 0's, row 16 the eye
    CHECK(rowsEqual(s, 66, frame, 1e-3));
    CHECK(rowsEqual(s, 66 + 8, ov, 1e-6));
    CHECK(closeTo(s.rows[66 + 16].x, frameEye[0]));
    CHECK_EQ(sh.farInstances, 0u);
    // the shift of the instance seen through copy -1: one pan step (as at view 0), not one world width
    CHECK(sh.n == 1u && sh.maxPx > 0.5 && sh.maxPx < 50.0);
    std::printf("wrap copy -1: instance shift %.2f px for a 6-unit pan\n", sh.maxPx);

    // (b) the copy given no EXE view: the OverlayProj of view 0 moved by k W
    ModelScene s2 = tapped;
    fc.overlayProjWrap[0] = fc.overlayProjWrap[1] = nullptr;
    CameraShift sh2;
    applyFrameCamera(s2, 0, fc, tap, &sh2);
    float ovM2[16];
    moved(ov, -kW, ovM2);
    CHECK(rowsEqual(s2, 22 + 8, ovM2, 1e-6) && rowsEqual(s2, 44 + 8, ovP, 1e-6));

    // (c) a still camera: every fitted constant keeps the EXE's own row 16 bit for bit (eyeKept), rows 0-3 its copy
    ModelScene s3 = tapped;
    fc.viewProj = kVp;
    CameraShift sh3;
    applyFrameCamera(s3, 0, fc, tap, &sh3);
    CHECK_EQ(sh3.eyeKept, 3u);
    CHECK(std::memcmp(&s3.rows[22 + 16], &tapped.rows[22 + 16], sizeof(Float4)) == 0);
    CHECK(sh3.n == 1u && sh3.maxPx < 0.01);   // the live check: win_max_px about 0 at a still camera by the seam
    float tM[16];
    moved(kVp, -kW, tM);
    CHECK(rowsEqual(s3, 22, tM, 1e-6));

    // (d) the camera on the seam: the EXE wrapped its own position between the tap and the frame (x W - 3 -> x - 3), so
    // the tap's view 0 is the frame's copy +1 and the tap's copy -1 is the frame's view 0; both are placed where tapped
    ModelScene s4 = tapped;
    float seam[16];
    moved(kVp, -kW + 3.0f, seam);
    fc.viewProj = seam;
    CameraShift sh4;
    applyFrameCamera(s4, 0, fc, tap, &sh4);
    float seamP[16];
    moved(seam, kW, seamP);
    CHECK(rowsEqual(s4, 0, seamP, 1e-2));   // tapped view 0 -> the frame's copy +1
    CHECK(rowsEqual(s4, 22, seam, 1e-2));   // tapped copy -1 -> the frame's view 0
    // 0659: tapped copy +1 is two periods away and the half-period one 1.5: far, drawn at copies +2 and +1
    CHECK_EQ(sh4.unfitConsts, 0u);
    CHECK_EQ(sh4.farConsts, 2u);
    float seam2[16];
    moved(seam, 2.0f * kW, seam2);
    CHECK(rowsEqual(s4, 44, seam2, 5e-2) && rowsEqual(s4, 66, seamP, 1e-2));
    CHECK(sh4.n == 1u && sh4.maxPx < 50.0);

    // (e) no wrap period: every constant at view 0, nothing unfit (the rule before 0652; a non-wrapping map)
    ModelScene s5 = tapped;
    fc.viewProj = frame;
    fc.wrapPeriod = 0.0f;
    CameraShift sh5;
    applyFrameCamera(s5, 0, fc, tap, &sh5);
    CHECK(sh5.consts == 4u && sh5.wrapConsts == 0u && sh5.unfitConsts == 0u);
    CHECK(rowsEqual(s5, 22, frame, 1e-3) && rowsEqual(s5, 66, frame, 1e-3));
    CHECK(sh5.n == 1u && sh5.maxPx > 1000.0);   // the fault 0652 found: the copy -1 instance one world width off
    std::printf("before 0652: instance shift %.0f px\n", sh5.maxPx);

    // (f) ticket 0659: the frame 2.9 periods from the tap (no camera of this map: the fault): constants 0 and 1 (d = -2.9 W,
    // -3.9 W) draw nothing (rows 0-3 zero, row 16 as tapped); constants 2 and 3 (d = -1.9 W, -2.4 W) are far: copy -2
    ModelScene s6 = tapped;
    float far3[16];
    moved(kVp, 2.9f * kW, far3);
    fc.viewProj = far3;
    fc.wrapPeriod = kW;
    CameraShift sh6;
    applyFrameCamera(s6, 0, fc, tap, &sh6);
    CHECK_EQ(sh6.unfitConsts, 2u);
    CHECK_EQ(sh6.farConsts, 2u);
    for (int k = 0; k < 4; ++k)
        CHECK(s6.rows[k].x == 0 && s6.rows[k].w == 0 && s6.rows[22 + k].x == 0 && s6.rows[22 + k].w == 0);
    CHECK(std::memcmp(&s6.rows[22 + 16], &tapped.rows[22 + 16], sizeof(Float4)) == 0);
    float far3m2[16];
    moved(far3, -2.0f * kW, far3m2);
    CHECK(rowsEqual(s6, 44, far3m2, 5e-2) && rowsEqual(s6, 66, far3m2, 5e-2));
}

// Ticket 0659: a far jump (more than a quarter period) whose old and new views overlap: the records tapped through copy -1
// are drawn at the nearest copy, where the new frame shows them; at view 0 (the rule before 0652) they would lie outside it
C5R_TEST(frame_camera_far_jump) {
    float wide[16];
    std::memcpy(wide, kVp, sizeof(wide));
    wide[0] = 0.4f;   // a wide view: about 3,800 world units either side of the centre at the instance's depth
    ModelScene s;
    s.targets.push_back(TapTarget{1000, 500, 1, 28, 1000, 500, 1});
    s.rows.resize(22 + 4);
    for (int k = 0; k < 22; ++k) s.rows[k] = {9, 9, 9, 9};
    float m[16];
    moved(wide, -kW, m);
    putRows(s, 0, m);
    s.consts.push_back(TapConst{0, 22, 1, 1});
    s.rows[22 + 0] = {1, 0, 0, 0};
    s.rows[22 + 1] = {0, 1, 0, 0};
    s.rows[22 + 2] = {0, 0, 1, 0};
    s.rows[22 + 3] = {kW - 40.0f, 600.0f, 10.0f, 1};
    DrawItem it;
    it.material = Material::Instanced;
    it.instanceCount = 1;
    it.instRow = 22;
    it.vsCb1 = it.psCb1 = 0;
    it.target = 0;
    s.items.push_back(it);
    TapCameraRows tap;
    keepTapCamera(s, tap);
    float frame[16];
    moved(wide, 0.3f * kW, frame);   // a jump of 0.3 W: unfit for wrapCopyOf, the nearest copy is -1
    FrameCamera fc;
    fc.viewProj = frame;
    fc.width = 1000;
    fc.height = 500;
    fc.wrapPeriod = kW;
    CameraShift sh;
    applyFrameCamera(s, 0, fc, tap, &sh);
    CHECK_EQ(sh.unfitConsts, 0u);
    CHECK_EQ(sh.farConsts, 1u);
    CHECK_EQ(sh.farInstances, 1u);
    CHECK_EQ(sh.farOnScreen, 1u);        // at copy -1 the instance lies in the new frame
    CHECK_EQ(sh.farOnScreenView0, 0u);   // at view 0 it would not
    CHECK_EQ(sh.n, 0u);                  // a jump is not counted as a pan's shift
    float fM[16];
    moved(frame, -kW, fM);
    CHECK(rowsEqual(s, 0, fM, 1e-2));
}

C5R_TEST(inputs_negative) {
    InstallInputs in;
    CHECK(!in.opened());
    Device d;
    if (d.dev) CHECK_ERR(in.create(d.dev), Err::NotFound);   // open() first
    CHECK(in.env(kImprovementsCubelight) == nullptr);         // no device
    CHECK(in.fogRamp() == nullptr);
    gamefs::GameFiles empty;
    CHECK_ERR(in.open(empty), Err::NotFound);                 // no fogramp.dds
}

// The install's inputs equal the captures' (by SHA-256, in the harness: cityscene --inputs install) and are bound.
C5R_TEST(inputs_from_install) {
    lmt::Install& ins = lmt::install(false);
    InstallInputs in;
    CHECK_OK(in.open(ins.files));
    const InstallInputs::Counts& c = in.counts();
    std::printf("cube candidates %u cubes %u identities %u, volumes from file %d %d, %.0f ms\n", c.candidates, c.cubes,
                c.identities, c.buildingsFromFile ? 1 : 0, c.fowFromFile ? 1 : 0, c.openMs);
    CHECK(c.cubes >= 20 && c.identities >= 10);
    CHECK(c.buildingsFromFile && c.fowFromFile);
    CHECK_EQ(in.buildingsBytes().size(), 131072u);
    CHECK_EQ(in.fowBytes().size(), 131072u);
    CHECK(in.rampBytes().size() >= 128u * 8u * 4u);
    CHECK(in.envName(kImprovementsCubelight).find("improvements_cubelight") != std::string::npos);
    Device d;
    if (!d.dev) test::skip("no D3D11 device");
    CHECK_OK(in.create(d.dev));
    CHECK(in.fogRamp() && in.buildingsLut() && in.fowLut() && in.white() && in.black());
    CHECK(in.env(kImprovementsCubelight) != nullptr);
    CHECK(in.env(kImprovementsCubelight) == in.env(kImprovementsCubelight));   // made once
    CHECK(in.env(0x1234ull) == nullptr && in.counts().unknown == 1u);
    CHECK(in.env(0x1234ull) == nullptr && in.counts().unknown == 1u);         // counted once
    ModelScene s;
    DrawItem it;
    it.env[0] = kImprovementsCubelight;
    it.env[1] = 0x1234ull;
    s.items.push_back(it);
    EnvInput e;
    in.fill(s, ~0u, e);
    CHECK(e.env.size() == 1u && e.env.count(kImprovementsCubelight));
    CHECK(e.frame[0] == in.fogRamp() && e.frame[1] == in.buildingsLut() && e.frame[4] == in.fowLut());
    CHECK(e.frame[2] == nullptr && e.frame[3] == nullptr);   // t7 / t8 are the caller's
    in.release();
    CHECK(!in.created() && in.fogRamp() == nullptr);
    CHECK_OK(in.create(d.dev));   // again on the same device (the server's device rebuild)
    CHECK(in.env(kImprovementsCubelight) != nullptr);
}

// Live s3 point 8 offline: one block drawn with the PS inputs unbound is black wherever it is drawn; with the install's
// inputs (and the fog-off white mask and black cloud) it is not.
C5R_TEST(unbound_inputs_draw_black) {
    const std::string exe = test::exePath();
    if (exe.empty()) test::skip("C5R_EXE not set");
    lmt::Install& ins = lmt::install(true);
    CHECK_OK(ins.libStatus);
    Device d;
    if (!d.dev) test::skip("no D3D11 device");
    BuildingPrograms prog;
    CHECK_OK(prog.open(gamefs::fromUtf8(exe)));
    ModelRenderer r;
    CHECK_OK(r.init(d.dev, prog, ins.lib, ins.files));
    InstallInputs in;
    CHECK_OK(in.open(ins.files));
    CHECK_OK(in.create(d.dev));
    const int32_t fi = ins.lib.fileIndex("euro_anc_city.gr2", 1.0f);
    CHECK(fi >= 0);
    if (fi < 0) return;
    CHECK_OK(r.prepare(static_cast<uint32_t>(fi)));
    const ModelFile& mf = ins.lib.files()[fi];
    const ModelLibrary::Buffers* b = nullptr;
    CHECK_OK(ins.lib.resident(static_cast<uint32_t>(fi), &b));
    const Part& p = mf.layout.parts[0];
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    const uint16_t* ix = reinterpret_cast<const uint16_t*>(b->indices.data());
    for (uint32_t k = 0; k < p.key.indexCount; ++k) {
        const uint32_t v = ix[p.key.firstIndex + k] + static_cast<uint32_t>(p.key.baseVertex);
        float pos[3];
        std::memcpy(pos, b->vertices.data() + static_cast<size_t>(v) * 32, 12);
        for (int c = 0; c < 3; ++c) {
            lo[c] = std::min(lo[c], pos[c]);
            hi[c] = std::max(hi[c], pos[c]);
        }
    }
    const float e = std::max(hi[0] - lo[0], hi[1] - lo[1]) * 0.6f + 1e-3f, ez = (hi[2] - lo[2]) * 2.0f + 1e-3f;
    ModelScene s;
    s.targets.push_back(TapTarget{64, 64, 1, 28, 64, 64, 1});
    s.rows.resize(4 + 22);
    s.rows[0] = {1, 0, 0, 0};
    s.rows[1] = {0, 1, 0, 0};
    s.rows[2] = {0, 0, 1, 0};
    s.rows[3] = {-(lo[0] + hi[0]) / 2, -(lo[1] + hi[1]) / 2, -lo[2] + 1e-3f, 1};
    s.rows[4] = {1 / e, 0, 0, 0};
    s.rows[5] = {0, 1 / e, 0, 0};
    s.rows[6] = {0, 0, 1 / ez, 0};
    s.rows[7] = {0, 0, 0, 1};
    // PS rows 16-21 (PS c704c80b): 16 the eye, 17 the fog terms (density 0: no distance fog; .w the ramp row), 19 the fog
    // origin and 21.x the height scale (non-zero: the PS divides by the height difference)
    s.rows[4 + 16] = {0, 0, 1000, 0};
    s.rows[4 + 17] = {0, 0, 0, 0.5f};
    s.rows[4 + 19] = {0, 0, -1000, 0};
    s.rows[4 + 21] = {1, 0, 0, 0};
    s.consts.push_back(TapConst{4, 22, 1, 1});
    DrawItem it;
    it.file = static_cast<uint32_t>(fi);
    it.part = 0;
    it.material = Material::Blocks;
    it.instanceCount = 1;
    it.vsCb1 = it.psCb1 = 0;
    it.env[0] = it.env[1] = it.env[2] = kImprovementsCubelight;
    s.items.push_back(it);
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = td.Height = 64;
    td.MipLevels = td.ArraySize = 1;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    ID3D11Texture2D *rt = nullptr, *idt = nullptr, *dt = nullptr, *st = nullptr, *sti = nullptr;
    ID3D11RenderTargetView *rtv = nullptr, *idv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    CHECK(SUCCEEDED(d.dev->CreateTexture2D(&td, nullptr, &rt)) && SUCCEEDED(d.dev->CreateRenderTargetView(rt, nullptr, &rtv)));
    td.Format = DXGI_FORMAT_R32_UINT;
    CHECK(SUCCEEDED(d.dev->CreateTexture2D(&td, nullptr, &idt)) && SUCCEEDED(d.dev->CreateRenderTargetView(idt, nullptr, &idv)));
    td.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    CHECK(SUCCEEDED(d.dev->CreateTexture2D(&td, nullptr, &dt)) && SUCCEEDED(d.dev->CreateDepthStencilView(dt, nullptr, &dsv)));
    td.BindFlags = 0;
    td.Usage = D3D11_USAGE_STAGING;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    CHECK(SUCCEEDED(d.dev->CreateTexture2D(&td, nullptr, &st)));
    td.Format = DXGI_FORMAT_R32_UINT;
    CHECK(SUCCEEDED(d.dev->CreateTexture2D(&td, nullptr, &sti)));
    // drawn pixels (id 1) and the largest colour channel among them
    auto drawOnce = [&](const EnvInput& env, uint32_t& covered, uint32_t& maxChannel) {
        covered = maxChannel = 0;
        const float clear[4] = {0, 0, 0, 0};
        d.ctx->ClearRenderTargetView(rtv, clear);
        d.ctx->ClearRenderTargetView(idv, clear);
        d.ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
        ViewInput v;
        v.rtv = rtv;
        v.dsv = dsv;
        v.idRtv = idv;
        v.viewport = {0, 0, 64, 64, 0, 1};
        DrawReport rep;
        CHECK_OK(r.draw(d.ctx, s, v, env, &rep));
        CHECK_EQ(rep.drawn, 1u);
        if (!st || !sti) return;
        d.ctx->CopyResource(st, rt);
        d.ctx->CopyResource(sti, idt);
        D3D11_MAPPED_SUBRESOURCE mc, mi;
        if (SUCCEEDED(d.ctx->Map(st, 0, D3D11_MAP_READ, 0, &mc))) {
            if (SUCCEEDED(d.ctx->Map(sti, 0, D3D11_MAP_READ, 0, &mi))) {
                for (uint32_t y = 0; y < 64; ++y)
                    for (uint32_t x = 0; x < 64; ++x) {
                        if (static_cast<const uint32_t*>(mi.pData)[y * mi.RowPitch / 4 + x] != 1) continue;
                        ++covered;
                        const uint8_t* px = static_cast<const uint8_t*>(mc.pData) + y * mc.RowPitch + x * 4;
                        maxChannel = std::max<uint32_t>(maxChannel, std::max(px[0], std::max(px[1], px[2])));
                    }
                d.ctx->Unmap(sti, 0);
            }
            d.ctx->Unmap(st, 0);
        }
    };
    uint32_t covered = 0, maxCh = 0;
    EnvInput none;
    drawOnce(none, covered, maxCh);
    std::printf("unbound: covered %u max channel %u\n", covered, maxCh);
    CHECK(covered > 100);
    CHECK_EQ(maxCh, 0u);   // black: t6 (the colour key volume) unbound maps every colour to 0
    EnvInput full;
    in.fill(s, ~0u, full);
    full.frame[2] = in.white();
    full.frame[3] = in.black();
    drawOnce(full, covered, maxCh);
    std::printf("install inputs: covered %u max channel %u\n", covered, maxCh);
    CHECK(covered > 100);
    CHECK(maxCh > 40);
    for (IUnknown* u : std::initializer_list<IUnknown*>{rtv, idv, dsv, rt, idt, dt, st, sti})
        if (u) u->Release();
}

C5R_TEST_MAIN
