// water.cpp - c5r::water: ocean, coast and lakes drawn with the EXE's WaterShader programs (epic 3600, issue 3604).
// Own code (team-3600 W4), written from the behaviour described in research/05-graphics-composition/water-system.md
// section 10 (the WaterFrame writer 0x739ae0, the view footprint 0x5D0370, Water::Startup 0x5F06A0 and its texture load
// 0x51F9E0; D83: no decompiler output here). The EXE's shaders and the water art are read from the player's files at run
// time; nothing of the EXE is copied here.
//
// The arithmetic below follows the EXE's operation order in single precision where the EXE uses SSE (so that the
// results are bit-equal), and double precision where the EXE uses the x87 unit (rounded to float where it stores).
#include "c5r/water/water.h"

#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include "c5r/bake/heightpage.h"
#include "c5r/bake/post.h"
#include "c5r/terrainsets/lean.h"
#include "c5r/water/currents.h"
#include "c5r/gamefs/dds.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/shaders/shadersection.h"
#include "c5r/terrainsets/terrainsets.h"

namespace c5r { namespace water {

namespace {

template <class T> void rel(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

Status hrOk(HRESULT h, const char* what) {
    if (SUCCEEDED(h)) return Status::Ok();
    char b[32];
    std::snprintf(b, sizeof b, " (hr 0x%08lx)", static_cast<unsigned long>(h));
    return Status::fail(h == E_OUTOFMEMORY ? Err::OutOfMemory : Err::Device, std::string(what) + b);
}

bool finite16(const float* m) {
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(m[i])) return false;
    return true;
}

// out = a * b (row-major), each element summed left to right in float as D3DXMatrixMultiply's scalar form.
void mul44f(const float* a, const float* b, float* out) {
    float r[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = a[i * 4 + 0] * b[0 * 4 + j];
            s += a[i * 4 + 1] * b[1 * 4 + j];
            s += a[i * 4 + 2] * b[2 * 4 + j];
            s += a[i * 4 + 3] * b[3 * 4 + j];
            r[i * 4 + j] = s;
        }
    std::memcpy(out, r, sizeof r);
}

bool inverse44(const float* m, float* out) {
    double a[4][8];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 8; ++j) a[i][j] = j < 4 ? m[i * 4 + j] : (j - 4 == i ? 1.0 : 0.0);
    for (int c = 0; c < 4; ++c) {
        int piv = c;
        for (int r = c + 1; r < 4; ++r)
            if (std::fabs(a[r][c]) > std::fabs(a[piv][c])) piv = r;
        if (std::fabs(a[piv][c]) < 1e-12) return false;
        if (piv != c)
            for (int j = 0; j < 8; ++j) std::swap(a[c][j], a[piv][j]);
        const double d = a[c][c];
        for (int j = 0; j < 8; ++j) a[c][j] /= d;
        for (int r = 0; r < 4; ++r) {
            if (r == c) continue;
            const double f = a[r][c];
            for (int j = 0; j < 8; ++j) a[r][j] -= f * a[c][j];
        }
    }
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) out[i * 4 + j] = static_cast<float>(a[i][j + 4]);
    return true;
}

void put(uint8_t* out, uint32_t off, const float* v, uint32_t n) { std::memcpy(out + off, v, n * 4); }
void put1(uint8_t* out, uint32_t off, float v) { std::memcpy(out + off, &v, 4); }

// The writer's rounding of a sample coordinate: half away from zero, in double, then truncated through a float.
int roundSample(float v) {
    const double d = v >= 0.0f ? std::floor(static_cast<double>(v) + 0.5) : std::ceil(static_cast<double>(v) - 0.5);
    return static_cast<int>(static_cast<float>(d));
}

bool worldOk(const World& w) {
    return w.valid && std::isfinite(w.minX) && std::isfinite(w.minY) && std::isfinite(w.maxX) && std::isfinite(w.maxY) &&
           std::isfinite(w.width) && std::isfinite(w.height) && std::isfinite(w.sx) && std::isfinite(w.sy) &&
           std::isfinite(w.sz) && w.width != 0 && w.height != 0 && w.sx != 0 && w.sy != 0 && w.sz != 0 &&
           w.samplesX != 0 && w.samplesY != 0;
}

using gamefs::DdsSubresource;

struct GpuQuery {
    ID3D11Query* disjoint = nullptr;
    ID3D11Query* t0 = nullptr;
    ID3D11Query* t1 = nullptr;
    bool issued = false;
    uint64_t serial = 0;
};

DXGI_FORMAT typelessOf(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB: return DXGI_FORMAT_BC1_TYPELESS;
    case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB: return DXGI_FORMAT_BC2_TYPELESS;
    case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB: return DXGI_FORMAT_BC3_TYPELESS;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
DXGI_FORMAT srgbOf(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case DXGI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case DXGI_FORMAT_BC1_UNORM: return DXGI_FORMAT_BC1_UNORM_SRGB;
    case DXGI_FORMAT_BC2_UNORM: return DXGI_FORMAT_BC2_UNORM_SRGB;
    case DXGI_FORMAT_BC3_UNORM: return DXGI_FORMAT_BC3_UNORM_SRGB;
    default: return f;
    }
}

// The six parameter texels of watersky.dds as floats (byte / 255, RGBA order), row-major from the top-left texel of
// mip 0. The EXE converts the file to a float image first (0x8CC4E0 with format 2) [I: format 2 = R32G32B32A32_FLOAT].
Status skyTexels(const gamefs::DdsImage& img, float out[6][4]) {
    if (img.format != DXGI_FORMAT_B8G8R8A8_UNORM && img.format != DXGI_FORMAT_R8G8B8A8_UNORM)
        return Status::fail(Err::Unsupported, "watersky.dds: format is not 8-bit RGBA");
    if (img.subresources.empty() || static_cast<uint64_t>(img.width) * img.height < 6)
        return Status::fail(Err::BadLayout, "watersky.dds: fewer than 6 texels");
    const DdsSubresource& s = img.subresources[0];
    const uint8_t* px = img.pixels() + s.offset;
    for (uint32_t i = 0; i < 6; ++i) {
        const uint32_t x = i % img.width, y = i / img.width;
        const uint8_t* t = px + static_cast<size_t>(y) * s.rowPitch + x * 4;
        const bool bgra = img.format == DXGI_FORMAT_B8G8R8A8_UNORM;
        const uint8_t r = bgra ? t[2] : t[0], g = t[1], b = bgra ? t[0] : t[2], a = t[3];
        out[i][0] = static_cast<float>(r) * (1.0f / 255.0f);  // byte * (1/255): bit-equal to the live fields [L, 3601 capture]
        out[i][1] = static_cast<float>(g) * (1.0f / 255.0f);  // byte * (1/255): bit-equal to the live fields [L, 3601 capture]
        out[i][2] = static_cast<float>(b) * (1.0f / 255.0f);  // byte * (1/255): bit-equal to the live fields [L, 3601 capture]
        out[i][3] = static_cast<float>(a) * (1.0f / 255.0f);  // byte * (1/255): bit-equal to the live fields [L, 3601 capture]
    }
    return Status::Ok();
}
} // namespace

// ---- WaterFields ----

float WaterFields::get(uint32_t off) const {
    if (off < 0x60 || off > 0xE4 || (off & 3)) return 0.0f;
    float v;
    std::memcpy(&v, raw + (off - 0x60), 4);
    return v;
}
void WaterFields::set(uint32_t off, float v) {
    if (off < 0x60 || off > 0xE4 || (off & 3)) return;
    std::memcpy(raw + (off - 0x60), &v, 4);
}

void waterFieldsFromSky(const float sky[6][4], WaterFields& f) {
    // +0x70 / +0x80: rgb divided by alpha (1 / a first, then each channel times it)
    for (int k = 0; k < 2; ++k) {
        const float inv = 1.0f / sky[k][3];
        const uint32_t base = k == 0 ? 0x70u : 0x80u;
        f.set(base + 0, inv * sky[k][0]);
        f.set(base + 4, sky[k][1] * inv);
        f.set(base + 8, sky[k][2] * inv);
    }
    // +0x90, +0x9C, +0xA8, +0xB8: normalize(rgb - 0.5) (D3DXVec3Normalize); +0xB4, +0xC4: alpha of texels 4, 5.
    // D3DX's own rounding is not reproduced: in double, 2 of the 4 vectors are bit-equal to the live fields and the
    // other 2 differ by 1 ulp in two components (3601's capture, water-system.md 10.6) [L].
    const uint32_t dst[4] = {0x90, 0x9C, 0xA8, 0xB8};
    for (int k = 0; k < 4; ++k) {
        float v[3] = {sky[2 + k][0] - 0.5f, sky[2 + k][1] - 0.5f, sky[2 + k][2] - 0.5f};
        const double len = std::sqrt(static_cast<double>(v[0]) * v[0] + static_cast<double>(v[1]) * v[1] + static_cast<double>(v[2]) * v[2]);
        if (len > 0.0)
            for (float& x : v) x = static_cast<float>(x / len);
        f.set(dst[k] + 0, v[0]);
        f.set(dst[k] + 4, v[1]);
        f.set(dst[k] + 8, v[2]);
    }
    f.set(0xB4, sky[4][3]);
    f.set(0xC4, sky[5][3]);
}

WaterFields stockWaterFields(const float (*sky)[4], uint16_t level) {
    WaterFields f;
    f.set(0x60, 0.1f);
    f.set(0x64, 0.1f);
    f.set(0x68, 2.0f);
    f.set(0x6C, 0.5f);
    f.set(0x7C, static_cast<float>(std::pow(2.0, 11.75)));
    f.set(0x8C, static_cast<float>(std::pow(2.0, 5.75)));
    f.set(0xC8, static_cast<float>(level) * 1.5259021893143654e-05f);  // * (1/65535) as stored
    f.set(0xCC, 1.43f);
    f.set(0xD0, 5.0f);
    f.set(0xD4, 0.008f);
    f.set(0xD8, 0.18f);
    f.set(0xDC, 0.0f);
    f.set(0xE0, 0.03f);
    f.set(0xE4, 0.0f);
    if (sky) {
        float s[6][4];
        for (int i = 0; i < 6; ++i)
            for (int j = 0; j < 4; ++j) s[i][j] = sky[i][j];
        waterFieldsFromSky(s, f);
    }
    f.valid = true;
    return f;
}

float zoomFraction(float cameraHeight, float zoomMin, float zoomMax) {
    if (!(cameraHeight > 0.0f) || !(zoomMax > zoomMin)) return 0.0f;
    const float zoom = std::log(cameraHeight / 50.0f) * 6.6667f;
    const float f = (std::min(std::max(zoom, zoomMin), zoomMax) - zoomMin) / (zoomMax - zoomMin);
    return std::min(std::max(f, 0.0f), 1.0f);
}

World worldFromGrid(const heightfield::HeightGrid& g) {
    World w;
    if (g.samplesX == 0 || g.samplesY == 0) return w;
    w.sx = g.sx;
    w.sy = g.sy;
    w.sz = g.sz;
    w.samplesX = g.samplesX;
    w.samplesY = g.samplesY;
    w.wrapX = g.wrapX;
    w.width = static_cast<float>(g.samplesX - 1) * g.sx;
    w.height = static_cast<float>(g.samplesY - 1) * g.sy;
    w.minX = -0.5f * w.width;
    w.maxX = 0.5f * w.width;
    w.minY = -0.5f * w.height;
    w.maxY = 0.5f * w.height;
    w.valid = true;
    return w;
}

// ---- WaterFrame (water-system.md 10.1) ----

Status computeWaterFrame(const FrameInputs& in, uint8_t out[kWaterFrameBytes]) {
    if (!out) return Status::fail(Err::BadLayout, "computeWaterFrame: out null");
    if (!in.fields.valid) return Status::fail(Err::NotFound, "computeWaterFrame: Water fields not valid");
    if (!finite16(in.cam.view) || !finite16(in.cam.proj) || !finite16(in.overlayProj) || !finite16(in.shadowMatrix0) ||
        !finite16(in.shadowMatrix1))
        return Status::fail(Err::BadLayout, "computeWaterFrame: a matrix is not finite");
    if (!std::isfinite(in.clock.seconds) || !std::isfinite(in.waterZ) || !std::isfinite(in.shadowBlend) ||
        !std::isfinite(in.fog.density) || !std::isfinite(in.fog.scale) || !std::isfinite(in.fog.w) ||
        (in.haveCamPos && !(std::isfinite(in.camPos[0]) && std::isfinite(in.camPos[1]) && std::isfinite(in.camPos[2]))))
        return Status::fail(Err::BadLayout, "computeWaterFrame: the clock, waterZ, fog, shadow blend or camera position is not finite");
    for (float c : in.fog.color)
        if (!std::isfinite(c)) return Status::fail(Err::BadLayout, "computeWaterFrame: fog colour not finite");
    if (!worldOk(in.world)) return Status::fail(Err::BadLayout, "computeWaterFrame: world not valid (box, sizes or scales)");
    const World& w = in.world;
    WaterFields f = in.fields;
    if (in.clock.apply) f.set(0xDC, static_cast<float>(in.clock.seconds));
    if (in.waterZ >= 0.0f) f.set(0xC8, in.waterZ / (w.sz * 255.0f));
    float camPos[3];
    if (in.haveCamPos) {
        std::memcpy(camPos, in.camPos, sizeof camPos);
    } else {
        float inv[16];
        if (!inverse44(in.cam.view, inv)) return Status::fail(Err::BadLayout, "computeWaterFrame: view not invertible");
        camPos[0] = inv[12];
        camPos[1] = inv[13];
        camPos[2] = inv[14];
    }
    std::memset(out, 0, kWaterFrameBytes);

    // 0x000 ViewProj = View x Proj; 0x040 OverlayProj; 0x160 ShadowMatrix1; 0x1A0 ShadowMatrix0
    float vp[16];
    mul44f(in.cam.view, in.cam.proj, vp);
    put(out, 0x000, vp, 16);
    put(out, 0x040, in.overlayProj, 16);
    put(out, 0x160, in.shadowMatrix1, 16);
    put(out, 0x1A0, in.shadowMatrix0, 16);

    // 0x080 WaveScale = float(sqrt(2)) * Water+0xE4 * (5/9, 2/3, 5/36, -5/6)
    const float root2 = static_cast<float>(std::sqrt(2.0));
    const float ws = root2 * f.get(0xE4);
    put1(out, 0x080, ws * 0.5555555820465088f);
    put1(out, 0x084, ws * 0.6666666865348816f);
    put1(out, 0x088, ws * 0.1388888955116272f);
    put1(out, 0x08C, ws * -0.8333333134651184f);

    // 0x090 WavePhase: n = trunc(Water+0xD4 * worldWidthX) waves across the box, s = 1 / worldWidthX
    const float boxW = w.maxX - w.minX;
    const float n = static_cast<float>(static_cast<int>(f.get(0xD4) * boxW));
    const float s = 1.0f / boxW;
    const float sn = s * n;
    put1(out, 0x090, sn);
    put1(out, 0x094, (n + 1.0f) * s);
    put1(out, 0x098, (n + 2.0f) * s);
    put1(out, 0x09C, (n + 3.0f) * s);
    // 0x0A0 WaveConst
    put1(out, 0x0A0, 1.7000000476837158f);
    put1(out, 0x0A4, 1.2000000476837158f);
    put1(out, 0x0A8, 1.7000000476837158f);
    put1(out, 0x0AC, 1.2000000476837158f);
    // 0x0B0 WaveR11 = Water+0xDC * WavePhase.x; 0x0C0 WaveR12 = Water+0xC8 * sz * 255 (the water height)
    put1(out, 0x0B0, f.get(0xDC) * sn);
    put1(out, 0x0C0, (f.get(0xC8) * w.sz) * 255.0f);
    // 0x0D0..0x13C: Water fields, copied as bits
    auto copy = [&](uint32_t dst, uint32_t src, uint32_t count) {
        std::memcpy(out + dst, f.raw + (src - 0x60), count * 4);
    };
    copy(0x0D0, 0xD0, 1);
    copy(0x0E0, 0x90, 3);
    copy(0x0F0, 0x9C, 3);
    copy(0x100, 0x70, 4);
    copy(0x110, 0x80, 4);
    copy(0x120, 0xA8, 4);
    copy(0x130, 0xB8, 4);
    // 0x140 FogParams = (-density, exp(-density * camZ) * (-1 / density) * scale, 0, w)
    put1(out, 0x140, -in.fog.density);
    if (in.fog.density != 0.0f) {
        const float x = -(in.fog.density * camPos[2]);
        const double e = std::exp2(static_cast<double>(x) * 1.4426950408889634);
        put1(out, 0x144, static_cast<float>(e * (-1.0 / static_cast<double>(in.fog.density)) * static_cast<double>(in.fog.scale)));
    }
    put1(out, 0x14C, in.fog.w < 0.0f ? zoomFraction(camPos[2]) : in.fog.w);  // 0xD90FAC (issue 3609)
    // 0x150 CameraPos (0xDA1440 + 0x40v: the inverse view's translation)
    put(out, 0x150, camPos, 3);
    // 0x1E0 HeightmapUV = Translation(-minX, -minY, 0) x Scaling(1 / width, -1 / height, 255 / (sz * 65535))
    {
        const float sxu = static_cast<float>(1.0 / static_cast<double>(w.width));
        const float syu = static_cast<float>(-1.0 / static_cast<double>(w.height));
        const float szu = 255.0f / (w.sz * 65535.0f);
        const float t[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -w.minX, -w.minY, 0, 1};
        const float sc[16] = {sxu, 0, 0, 0, 0, syu, 0, 0, 0, 0, szu, 0, 0, 0, 0, 1};
        float m[16];
        mul44f(t, sc, m);
        for (float& v : m)
            if (v == 0.0f) v = 0.0f;  // no negative zeros (the product's zero terms are +0 in the EXE's sums)
        put(out, 0x1E0, m, 16);
    }
    put1(out, 0x220, in.shadowBlend);
    put(out, 0x230, in.fog.color, 4);
    copy(0x240, 0x6C, 1);
    return Status::Ok();
}

// ---- Footprint and ocean geometry (water-system.md 10.2) ----

namespace {
// The bounding box of the 8 points where the four frustum edge lines (side planes 0/1 with 2/3) meet z = 0 and
// z = 127.5 (0x5D0370), before any clamp: 0xDA1B60 / 0xDA1B90 of the view. Returns the number of points found (an edge
// parallel to the plane gives none).
int footprintRect(const float* viewProj, float& mnx, float& mny, float& mxx, float& mxy) {
    // side planes of clip = p * M: left col3 + col0, right col3 - col0, bottom col3 + col1, top col3 - col1
    double pl[4][4];
    for (int k = 0; k < 4; ++k) {
        const double c3 = viewProj[k * 4 + 3], c0 = viewProj[k * 4 + 0], c1 = viewProj[k * 4 + 1];
        pl[0][k] = c3 + c0;
        pl[1][k] = c3 - c0;
        pl[2][k] = c3 + c1;
        pl[3][k] = c3 - c1;
    }
    mnx = mny = mxx = mxy = 0;
    int any = 0;
    const double zs[2] = {0.0, 127.5};
    for (double z : zs)
        for (int a = 0; a < 2; ++a)
            for (int b = 2; b < 4; ++b) {
                const double* A = pl[a];
                const double* B = pl[b];
                const double det = A[0] * B[1] - A[1] * B[0];
                if (std::fabs(det) < 1e-12) continue;  // edge parallel to the plane: no point
                const double ra = -(A[2] * z + A[3]), rb = -(B[2] * z + B[3]);
                const float x = static_cast<float>((ra * B[1] - A[1] * rb) / det);
                const float y = static_cast<float>((A[0] * rb - ra * B[0]) / det);
                if (!any) {
                    mnx = mxx = x;
                    mny = mxy = y;
                } else {
                    mnx = std::min(mnx, x);
                    mxx = std::max(mxx, x);
                    mny = std::min(mny, y);
                    mxy = std::max(mxy, y);
                }
                ++any;
            }
    if (!any) mnx = mny = mxx = mxy = 0;
    return any;
}
}  // namespace

Status viewFootprint(const float viewProj[16], const World& w, Footprint& out) {
    if (!viewProj || !finite16(viewProj)) return Status::fail(Err::BadLayout, "viewFootprint: matrix not finite");
    if (!worldOk(w)) return Status::fail(Err::BadLayout, "viewFootprint: world not valid");
    float mnx, mny, mxx, mxy;
    footprintRect(viewProj, mnx, mny, mxx, mxy);
    out.minX = std::max(mnx, w.minX);
    out.minY = std::max(mny, w.minY);
    out.maxX = std::min(mxx, w.maxX);
    out.maxY = std::min(mxy, w.maxY);
    return Status::Ok();
}

// ---- The reflection camera (issue 0382; water-system.md 13.6, the second half of the shadow set-up 0x615950) ----

void reflectionDirection(float out[3]) {
    // TerrainSystem+0x75744..+0x7574C: x87 sin / cos of the EXE's double radians (135 and 80 degrees), stored as float
    const double f = 2.356194490192345, e = 1.3962634015954636;
    out[0] = static_cast<float>(-(std::cos(e) * std::sin(f)));
    out[1] = static_cast<float>(-(std::sin(e) * std::sin(f)));
    out[2] = static_cast<float>(std::cos(f));
}

namespace {
// a zero or non-finite length gives a non-finite result, which reflectionCamera's last check refuses
void normalize3(const float in[3], float out[3]) {
    const float len = std::sqrt(in[0] * in[0] + in[1] * in[1] + in[2] * in[2]);
    for (int i = 0; i < 3; ++i) out[i] = in[i] / len;
}
void cross3(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}
float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
}  // namespace

Status reflectionCamera(const float sceneViewProj[16], ReflectionCamera& out) {
    out = ReflectionCamera();
    if (!sceneViewProj || !finite16(sceneViewProj)) return Status::fail(Err::BadLayout, "reflectionCamera: matrix not finite");
    float mnx, mny, mxx, mxy;
    if (footprintRect(sceneViewProj, mnx, mny, mxx, mxy) != 8)
        return Status::fail(Err::BadLayout, "reflectionCamera: a frustum edge is parallel to the ground (no footprint)");
    if (!(mxx > mnx) || !(mxy > mny))
        return Status::fail(Err::BadLayout, "reflectionCamera: the footprint has zero width or height");
    ReflectionCamera r;
    r.center[0] = (mxx + mnx) * 0.5f;   // (0xDA1B90 + 0xDA1B60) * 0.5, z (0 + 0) * 0.5
    r.center[1] = (mxy + mny) * 0.5f;
    r.center[2] = 0.0f;
    const float l = (mnx - mxx) * 0.5f, b = (mny - mxy) * 0.5f;
    r.halfX = -l;
    r.halfY = -b;
    float d[3];
    reflectionDirection(d);
    for (int i = 0; i < 3; ++i) r.eye[i] = -(d[i] * 5000.0f) + r.center[i];
    // D3DXMatrixLookAtRH(eye, at = center, up = (0, 1, 0))
    const float up[3] = {0.0f, 1.0f, 0.0f};
    float zd[3] = {r.eye[0] - r.center[0], r.eye[1] - r.center[1], r.eye[2] - r.center[2]}, z[3], xc[3], x[3], y[3];
    normalize3(zd, z);
    cross3(up, z, xc);
    normalize3(xc, x);
    cross3(z, x, y);
    const float v[16] = {x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0,
                         -dot3(x, r.eye), -dot3(y, r.eye), -dot3(z, r.eye), 1};
    std::memcpy(r.view, v, sizeof v);
    // D3DXMatrixOrthoOffCenterRH(l, r = -l, b, t = -b, zn = 30000, zf = -30000)
    const float rr = -l, t = -b, zn = 30000.0f, zf = -30000.0f;
    const float p[16] = {2.0f / (rr - l), 0, 0, 0, 0, 2.0f / (t - b), 0, 0, 0, 0, 1.0f / (zn - zf), 0,
                         -1.0f - 2.0f * l / (rr - l), 1.0f + 2.0f * t / (b - t), zn / (zn - zf), 1};
    std::memcpy(r.proj, p, sizeof p);
    mul44f(r.view, r.proj, r.viewProj);
    if (!finite16(r.view) || !finite16(r.proj) || !finite16(r.viewProj) || !std::isfinite(r.center[0]) || !std::isfinite(r.center[1]))
        return Status::fail(Err::BadLayout, "reflectionCamera: a result is not finite (a footprint beyond the float range)");
    out = r;
    return Status::Ok();
}

Status buildOceanGeometry(const World& w, const Footprint* views, uint32_t viewCount, OceanGeometry& out) {
    out = OceanGeometry();
    if (!worldOk(w)) return Status::fail(Err::BadLayout, "buildOceanGeometry: world not valid");
    if (!views || viewCount == 0 || viewCount > kMaxViews)
        return Status::fail(Err::BadLayout, "buildOceanGeometry: views 0 or above " + std::to_string(kMaxViews));
    struct Range { int bx0, bx1, by0, by1; };
    std::vector<Range> ranges(viewCount);
    for (uint32_t v = 0; v < viewCount; ++v) {
        const Footprint& fp = views[v];
        if (!(std::isfinite(fp.minX) && std::isfinite(fp.minY) && std::isfinite(fp.maxX) && std::isfinite(fp.maxY)))
            return Status::fail(Err::BadLayout, "buildOceanGeometry: footprint of view " + std::to_string(v) + " not finite");
        const int rx0 = roundSample((fp.minX - w.minX) / w.sx);
        const int ry0 = roundSample((fp.minY - w.minY) / w.sy);
        const int rx1 = roundSample((fp.maxX - w.minX) / w.sx);
        const int ry1 = roundSample((fp.maxY - w.minY) / w.sy);
        Range r;
        r.bx0 = std::max(rx0 / 64, 0);  // C division: toward zero, as the EXE's
        r.by0 = std::max(ry0 / 64, 0);
        r.bx1 = std::min(rx1 / 64 + 1, static_cast<int>(w.samplesX) - 1);
        r.by1 = std::min(ry1 / 64 + 1, static_cast<int>(w.samplesY) - 1);
        ranges[v] = r;
    }
    // counts per (view, page), then the start of each group in (view, page) order
    std::vector<uint32_t> count(static_cast<size_t>(viewCount) * 256, 0), start(count.size(), 0);
    for (uint32_t v = 0; v < viewCount; ++v) {
        const Range& r = ranges[v];
        for (int bx = r.bx0; bx < r.bx1; ++bx)
            for (int by = r.by0; by < r.by1; ++by) {
                const int px = bx * 64 / 896, py = by * 64 / 896;
                if (px > 15 || py > 15) return Status::fail(Err::Unsupported, "buildOceanGeometry: page index past 16 x 16");
                ++count[v * 256 + static_cast<uint32_t>(py * 16 + px)];
            }
    }
    uint32_t total = 0;
    for (size_t i = 0; i < count.size(); ++i) {
        start[i] = total;
        total += count[i];
    }
    out.quads = total;
    out.vertices.assign(static_cast<size_t>(total) * 16, 0.0f);
    out.indices.assign(static_cast<size_t>(total) * 6, 0);
    std::vector<uint32_t> fill(count.size(), 0);
    const float halfX = w.sx * 32.0f, halfY = w.sy * 32.0f;
    const float ratioX = static_cast<float>(static_cast<double>(w.samplesX) / static_cast<double>(w.width));
    const float ratioY = static_cast<float>(static_cast<double>(w.samplesY) / static_cast<double>(w.height));
    const float inv1024 = 0.0009765625f;
    for (uint32_t v = 0; v < viewCount; ++v) {
        const Range& r = ranges[v];
        for (int bx = r.bx0; bx < r.bx1; ++bx) {
            const int px = bx * 64 / 896;
            for (int by = r.by0; by < r.by1; ++by) {
                const int py = by * 64 / 896;
                const uint32_t g = v * 256 + static_cast<uint32_t>(py * 16 + px);
                const uint32_t q = start[g] + fill[g]++;
                float cx = static_cast<float>(bx * 64) * w.sx;
                cx = cx + halfX;
                cx = cx + w.minX;
                float cy = static_cast<float>(by * 64) * w.sy;
                cy = cy + halfY;
                cy = cy + w.minY;
                const float left = cx - halfX, right = halfX + cx;
                const float bottom = cy - halfY, top = halfY + cy;
                const float offX = static_cast<float>(64 - px * 896), offY = static_cast<float>(64 - py * 896);
                float u0 = (left - w.minX) * ratioX;
                u0 = (u0 + offX) * inv1024;
                float u1 = (right - w.minX) * ratioX;
                u1 = (u1 + offX) * inv1024;
                float vt = (top - w.minY) * ratioY;
                vt = (vt + offY) * -inv1024;
                float vb = (bottom - w.minY) * ratioY;
                vb = (vb + offY) * -inv1024;
                float* p = out.vertices.data() + static_cast<size_t>(q) * 16;
                const float quad[16] = {left, top, u0, vt, right, top, u1, vt, right, bottom, u1, vb, left, bottom, u0, vb};
                std::memcpy(p, quad, sizeof quad);
                const uint32_t b = 4u * q;
                if (b + 3 > 0xFFFFu) out.indexOverflow = true;
                uint16_t* ix = out.indices.data() + static_cast<size_t>(q) * 6;
                ix[0] = static_cast<uint16_t>(b);
                ix[1] = static_cast<uint16_t>(b + 1);
                ix[2] = static_cast<uint16_t>(b + 2);
                ix[3] = static_cast<uint16_t>(b);
                ix[4] = static_cast<uint16_t>(b + 2);
                ix[5] = static_cast<uint16_t>(b + 3);
            }
        }
    }
    for (uint32_t v = 0; v < viewCount; ++v)
        for (uint32_t k = 0; k < 256; ++k) {
            const uint32_t g = v * 256 + k;
            if (count[g]) out.batches.push_back(OceanBatch{v, k, start[g], count[g]});
        }
    return Status::Ok();
}

// ---- Renderer ----

struct Renderer::Impl {
    ID3D11Device* dev = nullptr;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps[6] = {};       // by pass; 1 and 4 (rivers) not created here
    ID3D11InputLayout* layout = nullptr;
    ID3D11Buffer* cbOwn = nullptr;       // slot 0, 0x80 (unused by the ocean passes)
    ID3D11Buffer* cbFrame = nullptr;     // slot 1, WaterFrame
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    uint32_t vbQuads = 0;
    ID3D11SamplerState* samp[13] = {};
    ID3D11ShaderResourceView* own[14] = {};    // the module's own inputs per slot
    ID3D11ShaderResourceView* given[14] = {};  // setTexture
    ID3D11RasterizerState* rs = nullptr;
    ID3D11DepthStencilState* dssWrite = nullptr;
    ID3D11DepthStencilState* dssNoWrite = nullptr;
    ID3D11BlendState* bsAlpha = nullptr;
    ID3D11BlendState* bsOpaque = nullptr;
    GpuQuery q[4];
    uint64_t serial = 0;
    double lastGpuMs = -1;
    WaterFields stock;

    bool haveField = false;
    World world;
    ID3D11ShaderResourceView* pages[256] = {};
    Mesh meshes[4];
    Currents currents;                 // t5 (3602's WorldCurrents pass), computed at the first draw after setHeightField
    CurrentsParams currentsParams;
    bool currentsStale = false, currentsReady = false;
    RiverRenderer river;               // issue 3605 (river.cpp)

    OceanGeometry geo;
    std::vector<std::vector<uint8_t>> frames;

    void dropField() {
        for (auto& p : pages) rel(p);
        haveField = false;
        world = World();
    }
    ~Impl() {
        dropField();
        rel(vs);
        for (auto& x : ps) rel(x);
        rel(layout);
        rel(cbOwn);
        rel(cbFrame);
        rel(vb);
        rel(ib);
        for (auto& s : samp) rel(s);
        for (auto& s : own) rel(s);
        for (auto& s : given) rel(s);
        rel(rs);
        rel(dssWrite);
        rel(dssNoWrite);
        rel(bsAlpha);
        rel(bsOpaque);
        for (GpuQuery& g : q) {
            rel(g.disjoint);
            rel(g.t0);
            rel(g.t1);
        }
        rel(dev);
    }
    Status makeCb(uint32_t size, ID3D11Buffer** out) {
        D3D11_BUFFER_DESC d = {};
        d.ByteWidth = size;
        d.Usage = D3D11_USAGE_DYNAMIC;
        d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        return hrOk(dev->CreateBuffer(&d, nullptr, out), "CreateBuffer (constant)");
    }
    Status makeTex2D(uint32_t w, uint32_t h, DXGI_FORMAT f, const void* data, uint32_t pitch, ID3D11ShaderResourceView** srv) {
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = w;
        d.Height = h;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = f;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA s = {data, pitch, 0};
        ID3D11Texture2D* t = nullptr;
        C5R_TRY(hrOk(dev->CreateTexture2D(&d, &s, &t), "CreateTexture2D"));
        Status st = hrOk(dev->CreateShaderResourceView(t, nullptr, srv), "CreateShaderResourceView");
        t->Release();
        return st;
    }
    // A 2D DDS with an sRGB view when `srgb` (the EXE's sampler state 3 takes the texture's second view).
    Status loadDds(const gamefs::GameFiles& fs, const std::string& name, bool srgb, ID3D11ShaderResourceView** out,
                   gamefs::DdsImage* keep) {
        std::vector<uint8_t> bytes;
        Status st = fs.read(name, bytes);
        if (!st.ok()) return Status::fail(Err::NotFound, name + ": " + st.str());
        gamefs::DdsImage img;
        C5R_TRY(gamefs::parseDds(bytes.data(), bytes.size(), img));
        if (img.dim != gamefs::DdsDim::Tex2D || img.cube || img.arraySize != 1)
            return Status::fail(Err::Unsupported, name + ": not a plain 2D texture");
        if (keep) {
            *keep = img;
            if (!keep->converted) {  // keep the pixels: the source buffer goes away with `bytes`
                keep->bytes.assign(img.pixels(), img.pixels() + (img.subresources.back().offset + img.subresources.back().size));
                keep->converted = true;
            }
        }
        if (!out) return Status::Ok();
        const DXGI_FORMAT tl = srgb ? typelessOf(img.format) : img.format;
        if (tl == DXGI_FORMAT_UNKNOWN) return Status::fail(Err::Unsupported, name + ": no sRGB view for its format");
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = img.width;
        d.Height = img.height;
        d.MipLevels = img.mipLevels;
        d.ArraySize = 1;
        d.Format = tl;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        std::vector<D3D11_SUBRESOURCE_DATA> init;
        for (const DdsSubresource& s : img.subresources)
            init.push_back(D3D11_SUBRESOURCE_DATA{img.pixels() + s.offset, s.rowPitch, s.slicePitch});
        ID3D11Texture2D* t = nullptr;
        C5R_TRY(hrOk(dev->CreateTexture2D(&d, init.data(), &t), (std::string("CreateTexture2D ") + name).c_str()));
        D3D11_SHADER_RESOURCE_VIEW_DESC v = {};
        v.Format = srgb ? srgbOf(img.format) : img.format;
        v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        v.Texture2D.MipLevels = img.mipLevels;
        st = hrOk(dev->CreateShaderResourceView(t, &v, out), "CreateShaderResourceView (art)");
        t->Release();
        return st;
    }
    Status upload(ID3D11DeviceContext* ctx, ID3D11Buffer* b, const void* data, size_t n) {
        D3D11_MAPPED_SUBRESOURCE m;
        C5R_TRY(hrOk(ctx->Map(b, 0, D3D11_MAP_WRITE_DISCARD, 0, &m), "Map"));
        std::memcpy(m.pData, data, n);
        ctx->Unmap(b, 0);
        return Status::Ok();
    }
    // Grows the dynamic VB / IB to hold `quads` (x 1.2, as the EXE grows its own).
    Status ensureGeometry(uint32_t quads) {
        if (quads <= vbQuads && vb && ib) return Status::Ok();
        const uint32_t cap = std::max<uint32_t>(64, static_cast<uint32_t>(static_cast<double>(quads) * 1.2));
        rel(vb);
        rel(ib);
        vbQuads = 0;
        D3D11_BUFFER_DESC d = {};
        d.Usage = D3D11_USAGE_DYNAMIC;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        d.ByteWidth = cap * 64;
        d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        C5R_TRY(hrOk(dev->CreateBuffer(&d, nullptr, &vb), "CreateBuffer (ocean VB)"));
        d.ByteWidth = cap * 12;
        d.BindFlags = D3D11_BIND_INDEX_BUFFER;
        C5R_TRY(hrOk(dev->CreateBuffer(&d, nullptr, &ib), "CreateBuffer (ocean IB)"));
        vbQuads = cap;
        return Status::Ok();
    }
    void pollQueries(ID3D11DeviceContext* ctx, bool wait) {
        for (;;) {
            GpuQuery* g = nullptr;
            for (GpuQuery& x : q)
                if (x.issued && (!g || x.serial < g->serial)) g = &x;
            if (!g) return;
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj = {};
            UINT64 a = 0, b = 0;
            const UINT flags = wait ? 0 : D3D11_ASYNC_GETDATA_DONOTFLUSH;
            const auto t0 = std::chrono::steady_clock::now();
            for (;;) {
                if (ctx->GetData(g->disjoint, &dj, sizeof dj, flags) == S_OK &&
                    ctx->GetData(g->t0, &a, sizeof a, flags) == S_OK && ctx->GetData(g->t1, &b, sizeof b, flags) == S_OK)
                    break;
                if (!wait || std::chrono::steady_clock::now() - t0 > std::chrono::seconds(2)) return;
                Sleep(0);
            }
            g->issued = false;
            if (!dj.Disjoint && dj.Frequency && b >= a)
                lastGpuMs = static_cast<double>(b - a) * 1000.0 / static_cast<double>(dj.Frequency);
        }
    }
};

Renderer::Renderer() {}
Renderer::~Renderer() { release(); }
void Renderer::release() {
    delete p_;
    p_ = nullptr;
}

Status Renderer::init(ID3D11Device* device, const std::wstring& exePath, const std::wstring& gameDir) {
    release();
    if (!device) return Status::fail(Err::BadLayout, "init: no device");
    const terrainsets::ShaderSetDesc* set = terrainsets::findSet("WaterShader");
    if (!set) return Status::fail(Err::NotFound, "terrainsets has no WaterShader");
    shaders::ShaderSection sec;
    C5R_TRY(sec.open(exePath));
    std::unique_ptr<Impl> p(new Impl());
    p->dev = device;
    device->AddRef();
    auto blob = [&](uint32_t va, shaders::Stage st, const shaders::ShaderBlob** out) -> Status {
        const shaders::ShaderBlob* b = sec.findByVa(va);
        char v[16];
        std::snprintf(v, sizeof v, "0x%08x", va);
        if (!b) return Status::fail(Err::NotFound, std::string("WaterShader blob ") + v + " not in the .shader section");
        if (b->stage != st) return Status::fail(Err::BadLayout, std::string("WaterShader blob ") + v + " has another stage");
        *out = b;
        return Status::Ok();
    };
    // the ocean passes 0, 2, 3, 5 share VS 0x06815a48 (asset-pipeline.md 3.2)
    for (const terrainsets::PassDesc& pd : set->passes) {
        if (pd.index != 0 && pd.index != 2 && pd.index != 3 && pd.index != 5) continue;
        const shaders::ShaderBlob *bv = nullptr, *bp = nullptr;
        C5R_TRY(blob(pd.sm50[0], shaders::Stage::Vertex, &bv));
        C5R_TRY(blob(pd.sm50[1], shaders::Stage::Pixel, &bp));
        if (!p->vs) {
            C5R_TRY(hrOk(device->CreateVertexShader(bv->bytes, bv->size, nullptr, &p->vs), "CreateVertexShader (EXE)"));
            // VS input: POSITION float4 = (world x, world y, u, v), stride 16 [C VS signature, 0x739ae0's vertex]
            D3D11_INPUT_ELEMENT_DESC e = {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
            C5R_TRY(hrOk(device->CreateInputLayout(&e, 1, bv->bytes, bv->size, &p->layout), "CreateInputLayout (ocean)"));
        }
        C5R_TRY(hrOk(device->CreatePixelShader(bp->bytes, bp->size, nullptr, &p->ps[pd.index]), "CreatePixelShader (EXE)"));
    }
    if (!p->vs || !p->ps[0] || !p->ps[2] || !p->ps[3] || !p->ps[5])
        return Status::fail(Err::NotFound, "WaterShader: an ocean pass (0, 2, 3, 5) has no SM5 VS/PS");
    uint32_t ownSize = 0, frameSize = 0;
    for (const terrainsets::CbBinding& cb : set->cbuffers) {
        if (cb.slot == 0) ownSize = cb.layout->sizeBytes;
        if (cb.slot == 1) frameSize = cb.layout->sizeBytes;
    }
    if (frameSize != kWaterFrameBytes || ownSize == 0)
        return Status::fail(Err::NotFound, "WaterShader constant-buffer layouts missing or WaterFrame not 0x250");
    C5R_TRY(p->makeCb(ownSize, &p->cbOwn));
    C5R_TRY(p->makeCb(frameSize, &p->cbFrame));
    for (uint32_t i = 0; i < 13; ++i) {
        const terrainsets::SamplerStateDesc* s = terrainsets::samplerState(i);
        if (!s) return Status::fail(Err::NotFound, "sampler state " + std::to_string(i) + " not described");
        D3D11_SAMPLER_DESC d = {};
        d.Filter = static_cast<D3D11_FILTER>(s->filter);
        d.AddressU = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(s->address[0]);
        d.AddressV = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(s->address[1]);
        d.AddressW = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(s->address[2]);
        d.MaxAnisotropy = s->maxAnisotropy ? s->maxAnisotropy : 1;
        d.ComparisonFunc = static_cast<D3D11_COMPARISON_FUNC>(s->comparison);
        d.MinLOD = 0;
        d.MaxLOD = D3D11_FLOAT32_MAX;
        C5R_TRY(hrOk(device->CreateSamplerState(&d, &p->samp[i]), "CreateSamplerState"));
    }
    // constant inputs (water-system.md 10.4)
    const uint32_t zero = 0, white = 0xFFFFFFFFu, neutral = 0x80808080u, blue = 0xFF6E4A2Au;
    const uint16_t lean[4] = {0, 0, 0x8000, 0x8000};   // LEAN: no slope (zw = 0.5), no variance (xy = 0)
    const uint16_t lean1 = 0x8000;
    C5R_TRY(p->makeTex2D(1, 1, DXGI_FORMAT_R16G16B16A16_UNORM, lean, 8, &p->own[0]));
    C5R_TRY(p->makeTex2D(1, 1, DXGI_FORMAT_R16_UNORM, &lean1, 2, &p->own[1]));
    C5R_TRY(p->makeTex2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &zero, 4, &p->own[4]));     // the EXE's 32x32 zero reflection
    C5R_TRY(p->makeTex2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &neutral, 4, &p->own[5]));  // currents: no flow
    C5R_TRY(p->makeTex2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &zero, 4, &p->own[9]));     // overlay transparent
    C5R_TRY(p->makeTex2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &white, 4, &p->own[10]));   // FOW mask visible
    // shadow: 0 = no shadow (the ocean PS darkens by 0.67 x t13.x x shadowBlend; stock's "Shadow Texture Blur" is 0 on
    // 98.6 % of its texels [L 3601 cam1]; issue 3609 - it was white)
    C5R_TRY(p->makeTex2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &zero, 4, &p->own[13]));
    {
        std::vector<uint32_t> v(32 * 32 * 32);
        for (uint32_t z = 0; z < 32; ++z)
            for (uint32_t y = 0; y < 32; ++y)
                for (uint32_t x = 0; x < 32; ++x)
                    v[(z * 32 + y) * 32 + x] = 0xFF000000u | ((z * 255 / 31) << 16) | ((y * 255 / 31) << 8) | (x * 255 / 31);
        D3D11_TEXTURE3D_DESC d = {};
        d.Width = d.Height = d.Depth = 32;
        d.MipLevels = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA s = {v.data(), 32 * 4, 32 * 32 * 4};
        ID3D11Texture3D* t = nullptr;
        C5R_TRY(hrOk(device->CreateTexture3D(&d, &s, &t), "CreateTexture3D (identity LUT)"));
        Status st = hrOk(device->CreateShaderResourceView(t, nullptr, &p->own[11]), "CreateShaderResourceView (LUT)");
        t->Release();
        C5R_TRY(st);
    }
    // art: t6 waterdepthcolor_new.dds (sRGB view), t8 fogramp.dds, watersky.dds texels -> the stock fields
    if (!gameDir.empty()) {
        gamefs::GameFiles fs;
        C5R_TRY(fs.open(gameDir));
        C5R_TRY(p->loadDds(fs, "waterdepthcolor_new.dds", true, &p->own[6], nullptr));
        C5R_TRY(p->loadDds(fs, "fogramp.dds", false, &p->own[8], nullptr));
        {
            // t0 / t1: the waterbumps16 LEAN pair through 3602's loader (issue 3609; it was a flat constant)
            std::vector<uint8_t> lb;
            terrainsets::LeanFile lf;
            C5R_TRY(terrainsets::loadLean(fs, terrainsets::kLeanOcean, lb, lf));
            ID3D11Texture2D* four = nullptr;
            ID3D11Texture2D* one = nullptr;
            C5R_TRY(terrainsets::createLeanTextures(device, lf, &four, &one));
            rel(p->own[0]);
            rel(p->own[1]);
            Status s1 = hrOk(device->CreateShaderResourceView(four, nullptr, &p->own[0]), "CreateShaderResourceView (lean)");
            Status s2 = hrOk(device->CreateShaderResourceView(one, nullptr, &p->own[1]), "CreateShaderResourceView (lean)");
            four->Release();
            one->Release();
            C5R_TRY(s1);
            C5R_TRY(s2);
        }
        {
            // t11: the ColorKey TerrainFOW volume of style Generic (0xD90E60), when the install has the file
            std::vector<uint8_t> tga, lut;
            if (fs.read("TerrainFOW_Generic_ColorKey.tga", tga).ok() && bake::buildColorKeyLut(tga.data(), tga.size(), lut).ok()) {
                D3D11_TEXTURE3D_DESC d = {};
                d.Width = d.Height = d.Depth = 32;
                d.MipLevels = 1;
                d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                d.Usage = D3D11_USAGE_IMMUTABLE;
                d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA sd = {lut.data(), 32 * 4, 32 * 32 * 4};
                ID3D11Texture3D* t = nullptr;
                C5R_TRY(hrOk(device->CreateTexture3D(&d, &sd, &t), "CreateTexture3D (TerrainFOW LUT)"));
                rel(p->own[11]);
                Status st2 = hrOk(device->CreateShaderResourceView(t, nullptr, &p->own[11]), "CreateShaderResourceView (TerrainFOW LUT)");
                t->Release();
                C5R_TRY(st2);
            }
        }
        gamefs::DdsImage sky;
        C5R_TRY(p->loadDds(fs, "watersky.dds", false, nullptr, &sky));
        float texels[6][4];
        C5R_TRY(skyTexels(sky, texels));
        p->stock = stockWaterFields(texels);
    } else {
        C5R_TRY(p->makeTex2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &blue, 4, &p->own[6]));
        C5R_TRY(p->makeTex2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &white, 4, &p->own[8]));
        p->stock = stockWaterFields(nullptr);
    }
    // states of the ocean pass as the stock capture shows them [L, 3601 export cam1_frame309938, draw 350]: blend
    // SRC_ALPHA / INV_SRC_ALPHA (alpha ONE / ONE), depth LESS_EQUAL without write, cull FRONT with
    // FrontCounterClockwise (the quads face the camera and are kept), depth clip on
    D3D11_RASTERIZER_DESC r = {};
    r.FillMode = D3D11_FILL_SOLID;
    r.CullMode = D3D11_CULL_FRONT;
    r.FrontCounterClockwise = TRUE;
    r.DepthClipEnable = TRUE;
    C5R_TRY(hrOk(device->CreateRasterizerState(&r, &p->rs), "CreateRasterizerState"));
    D3D11_DEPTH_STENCIL_DESC ds = {};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    C5R_TRY(hrOk(device->CreateDepthStencilState(&ds, &p->dssWrite), "CreateDepthStencilState"));
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    C5R_TRY(hrOk(device->CreateDepthStencilState(&ds, &p->dssNoWrite), "CreateDepthStencilState"));
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    C5R_TRY(hrOk(device->CreateBlendState(&bd, &p->bsOpaque), "CreateBlendState"));
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    C5R_TRY(hrOk(device->CreateBlendState(&bd, &p->bsAlpha), "CreateBlendState (alpha)"));
    for (GpuQuery& g : p->q) {
        D3D11_QUERY_DESC qd = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        C5R_TRY(hrOk(device->CreateQuery(&qd, &g.disjoint), "CreateQuery (disjoint)"));
        qd.Query = D3D11_QUERY_TIMESTAMP;
        C5R_TRY(hrOk(device->CreateQuery(&qd, &g.t0), "CreateQuery (timestamp)"));
        C5R_TRY(hrOk(device->CreateQuery(&qd, &g.t1), "CreateQuery (timestamp)"));
    }
    C5R_TRY(p->currents.init(device, sec));            // t5: the currents map (3602; issue 3609 binds it)
    C5R_TRY(p->river.init(device, exePath, gameDir));  // the river part (issue 3605)
    p_ = p.release();
    return Status::Ok();
}

WaterFields Renderer::stockFields() const { return p_ ? p_->stock : WaterFields(); }

Status Renderer::setHeightField(const heightfield::HeightGrid& g) {
    if (!p_) return Status::fail(Err::NotFound, "setHeightField: init not done");
    if (g.mapW == 0 || g.mapH == 0 || g.mapW > heightfield::kMaxPlots || g.mapH > heightfield::kMaxPlots ||
        g.samplesX != g.mapW * heightfield::kSamplesPerPlot || g.samplesY != g.mapH * heightfield::kSamplesPerPlot)
        return Status::fail(Err::BadLayout, "setHeightField: samples are not plots * 64 (or plots 0 / above 256)");
    if (g.h.size() != static_cast<size_t>(g.samplesX) * g.samplesY)
        return Status::fail(Err::BadLayout, "setHeightField: h.size() != samplesX*samplesY");
    if (!(std::isfinite(g.sx) && std::isfinite(g.sy) && std::isfinite(g.sz) && g.sx > 0 && g.sy > 0 && g.sz > 0 &&
          std::isfinite(g.ox) && std::isfinite(g.oy) && std::isfinite(g.waterZ)))
        return Status::fail(Err::BadLayout, "setHeightField: scales or offsets not finite (or <= 0)");
    uint32_t pagesX = 0, pagesY = 0;
    C5R_TRY(bake::heightPageCount(g, pagesX, pagesY));
    if (pagesX > 16 || pagesY > 16) return Status::fail(Err::BadLayout, "setHeightField: more than 16 x 16 height pages");
    // build into a fresh table first, so a failure leaves the previous field in place
    ID3D11ShaderResourceView* fresh[256] = {};
    auto drop = [&]() {
        for (auto& s : fresh) rel(s);
    };
    for (uint32_t py = 0; py < pagesY; ++py)
        for (uint32_t px = 0; px < pagesX; ++px) {
            bake::HeightPage page;
            bake::HeightPageIndex idx;
            idx.px = px;
            idx.py = py;
            Status st = bake::buildHeightPage(g, idx, bake::kHeightPageMips, page);
            ID3D11Texture2D* tex = nullptr;
            if (st.ok()) st = bake::createHeightPageTexture(p_->dev, page, &tex, &fresh[py * 16 + px]);
            rel(tex);
            if (!st.ok()) {
                drop();
                return st;
            }
        }
    p_->dropField();
    std::memcpy(p_->pages, fresh, sizeof fresh);
    p_->world = worldFromGrid(g);
    p_->haveField = true;
    if (currentsParamsFromGrid(g, p_->currentsParams).ok()) {
        p_->currentsStale = true;
        p_->currentsReady = false;
    }
    return Status::Ok();
}

// ticket 4909 (live-terrain.md 8.14)
Status Renderer::updateHeightRegion(const heightfield::HeightGrid& g, const std::vector<terrain::SampleRect>& rects,
                                    const std::vector<bake::HeightPage>* built, terrain::RegionUpdateStats* stats) {
    const auto t0 = std::chrono::steady_clock::now();
    terrain::RegionUpdateStats st;
    if (!p_) return Status::fail(Err::NotFound, "updateHeightRegion: init not done");
    if (!p_->haveField) return Status::fail(Err::NotFound, "updateHeightRegion: no field");
    if (g.h.size() != static_cast<size_t>(g.samplesX) * g.samplesY)
        return Status::fail(Err::BadLayout, "updateHeightRegion: h.size() != samplesX*samplesY");
    const World w = worldFromGrid(g);
    const World& c = p_->world;
    CurrentsParams cp;
    if (!w.valid || w.samplesX != c.samplesX || w.samplesY != c.samplesY || w.sx != c.sx || w.sy != c.sy || w.sz != c.sz ||
        w.wrapX != c.wrapX || !currentsParamsFromGrid(g, cp).ok() || cp.sizeX != p_->currentsParams.sizeX ||
        cp.sizeY != p_->currentsParams.sizeY)
        return Status::fail(Err::BadLayout, "updateHeightRegion: not the current field's layout");
    std::vector<bake::HeightPageIndex> which;
    C5R_TRY(terrain::pagesForRects(g, rects, which));
    std::vector<ID3D11ShaderResourceView*> fresh(which.size(), nullptr);
    auto drop = [&]() {
        for (auto& s : fresh) rel(s);
    };
    double buildMs = 0;
    for (size_t k = 0; k < which.size(); ++k) {
        if (which[k].px >= 16 || which[k].py >= 16) {
            drop();
            return Status::fail(Err::BadLayout, "updateHeightRegion: page index past 16 x 16");
        }
        const bake::HeightPage* use = nullptr;
        bake::HeightPage own;
        if (built)
            for (const bake::HeightPage& hp : *built)
                if (hp.index.px == which[k].px && hp.index.py == which[k].py) {
                    use = &hp;
                    st.pagesFromWorker++;
                    break;
                }
        Status bs = Status::Ok();
        if (!use) {
            const auto b0 = std::chrono::steady_clock::now();
            bs = bake::buildHeightPage(g, which[k], bake::kHeightPageMips, own);
            buildMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - b0).count();
            use = &own;
        }
        ID3D11Texture2D* tex = nullptr;
        if (bs.ok()) bs = bake::createHeightPageTexture(p_->dev, *use, &tex, &fresh[k]);
        rel(tex);
        if (!bs.ok()) {
            drop();
            return bs;
        }
    }
    for (size_t k = 0; k < which.size(); ++k) {
        ID3D11ShaderResourceView*& slot = p_->pages[which[k].py * 16 + which[k].px];
        rel(slot);
        slot = fresh[k];
    }
    st.pagesRebuilt = static_cast<uint32_t>(which.size());
    st.buildMs = buildMs;
    st.totalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    st.uploadMs = st.totalMs - buildMs;
    if (stats) *stats = st;
    return Status::Ok();
}

Status Renderer::fieldDigest(std::vector<uint64_t>& out) const {
    out.clear();
    if (!p_ || !p_->haveField) return Status::fail(Err::NotFound, "fieldDigest: no field");
    uint64_t h = 0xcbf29ce484222325ull;
    for (ID3D11ShaderResourceView* s : p_->pages) C5R_TRY(terrain::digestTexture(p_->dev, s, h));
    out.push_back(h);
    uint64_t k = 0xcbf29ce484222325ull;
    auto fold = [&](const void* d, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(d);
        for (size_t i = 0; i < n; ++i) {
            k ^= b[i];
            k *= 0x100000001b3ull;
        }
    };
    const World& w = p_->world;
    fold(&w.minX, sizeof(float) * 9);
    fold(&w.samplesX, sizeof(uint32_t) * 2);
    fold(&w.wrapX, 1);
    fold(&w.valid, 1);
    fold(&p_->currentsParams.sizeX, sizeof(float));
    fold(&p_->currentsParams.sizeY, sizeof(float));
    out.push_back(k);
    return Status::Ok();
}

Status Renderer::setMesh(System s, const Mesh& m) {
    if (!p_) return Status::fail(Err::NotFound, "setMesh: init not done");
    if (s == System::Ocean)
        return Status::fail(Err::Unsupported, "setMesh: the ocean mesh is built per frame by the module (water-system.md 10.2)");
    if (s != System::River && s != System::Wave) return Status::fail(Err::BadLayout, "setMesh: unknown system");
    if (!m.vb.empty() || !m.ib.empty() || !m.draws.empty()) {
        if (m.stride == 0) return Status::fail(Err::BadLayout, "setMesh: stride 0");
        uint32_t isz = 0;
        if (m.indexFormat == DXGI_FORMAT_R16_UINT) isz = 2;
        else if (m.indexFormat == DXGI_FORMAT_R32_UINT) isz = 4;
        else if (!m.ib.empty()) return Status::fail(Err::BadLayout, "setMesh: index format neither R16 nor R32");
        if (isz && m.ib.size() % isz) return Status::fail(Err::BadLayout, "setMesh: index buffer size not a multiple of the index size");
        for (const Mesh::Draw& d : m.draws) {
            if (d.topology == 0 || d.topology > 64) return Status::fail(Err::BadLayout, "setMesh: unknown topology");
            if (d.pass > 5) return Status::fail(Err::BadLayout, "setMesh: pass outside 0..5");
            if (d.indexCount) {
                if (!isz || static_cast<uint64_t>(d.startIndex) + d.indexCount > m.ib.size() / isz)
                    return Status::fail(Err::BadLayout, "setMesh: a draw's indices lie outside the index buffer");
            } else if (static_cast<uint64_t>(d.vbOffset) + static_cast<uint64_t>(d.vertexCount) * m.stride > m.vb.size()) {
                return Status::fail(Err::BadLayout, "setMesh: a draw's vertices lie outside the vertex buffer");
            }
        }
    }
    if (s == System::River) C5R_TRY(p_->river.setMesh(m));  // the river part's own checks (stride 32, triangle lists, indices)
    p_->meshes[static_cast<uint32_t>(s)] = m;
    return Status::Ok();
}

Status Renderer::setRiverTexture(RiverSlot slot, ID3D11ShaderResourceView* srv) {
    if (!p_) return Status::fail(Err::NotFound, "setRiverTexture: init not done");
    return p_->river.setTexture(slot, srv);
}

Status Renderer::setTexture(Slot slot, ID3D11ShaderResourceView* srv) {
    if (!p_) return Status::fail(Err::NotFound, "setTexture: init not done");
    const uint32_t i = static_cast<uint32_t>(slot);
    const bool known = i == 0 || i == 1 || i == 4 || i == 5 || i == 6 || i == 8 || i == 9 || i == 10 || i == 11 || i == 13;
    if (!known) return Status::fail(Err::BadLayout, "setTexture: slot " + std::to_string(i) + " is not an input slot");
    if (srv) srv->AddRef();
    rel(p_->given[i]);
    p_->given[i] = srv;
    return Status::Ok();
}

ID3D11ShaderResourceView* Renderer::inputTexture(Slot slot) const {
    if (!p_) return nullptr;
    const uint32_t i = static_cast<uint32_t>(slot);
    if (i >= 14) return nullptr;
    if (p_->given[i]) return p_->given[i];
    if (i == 5 && p_->currentsReady) return p_->currents.srv();
    return p_->own[i];
}

const OceanGeometry& Renderer::oceanGeometry() const {
    static const OceanGeometry none;
    return p_ ? p_->geo : none;
}
const std::vector<std::vector<uint8_t>>& Renderer::frames() const {
    static const std::vector<std::vector<uint8_t>> none;
    return p_ ? p_->frames : none;
}

Status Renderer::gpuTime(ID3D11DeviceContext* ctx, bool wait, double* ms) {
    if (!p_ || !ctx) return Status::fail(Err::NotFound, "gpuTime: init not done");
    p_->pollQueries(ctx, wait);
    if (p_->lastGpuMs < 0) return Status::fail(Err::NotFound, "gpuTime: no timestamps available");
    if (ms) *ms = p_->lastGpuMs;
    return Status::Ok();
}

Status Renderer::draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv,
                      const FrameInputs& in, const Options& o, FrameStats* stats) {
    const auto tStart = std::chrono::steady_clock::now();
    if (!p_) return Status::fail(Err::NotFound, "draw: init not done");
    if (!p_->haveField) return Status::fail(Err::NotFound, "draw: no height field set");
    if (!ctx || !rtv) return Status::fail(Err::BadLayout, "draw: no context or render target");
    if (in.cam.width == 0 || in.cam.height == 0) return Status::fail(Err::BadLayout, "draw: camera width or height 0");
    if (!finite16(in.cam.view) || !finite16(in.cam.proj)) return Status::fail(Err::BadLayout, "draw: a camera matrix is not finite");
    if (in.haveOverlayProjWrap && (!finite16(in.overlayProjWrap[0]) || !finite16(in.overlayProjWrap[1])))
        return Status::fail(Err::BadLayout, "draw: an overlay matrix of a wrap view is not finite");
    if (in.haveShadowMatrix0Wrap && (!finite16(in.shadowMatrix0Wrap[0]) || !finite16(in.shadowMatrix0Wrap[1])))
        return Status::fail(Err::BadLayout, "draw: a shadow matrix of a wrap view is not finite");   // issue 4308
    if (o.oceanPass != -1 && o.oceanPass != 0 && o.oceanPass != 2 && o.oceanPass != 3 && o.oceanPass != 5)
        return Status::fail(Err::BadLayout, "draw: oceanPass " + std::to_string(o.oceanPass) + " is not -1, 0, 2, 3 or 5");
    if (!in.fields.valid) return Status::fail(Err::NotFound, "draw: Water fields not valid (Renderer::stockFields() gives the EXE's)");
    if (o.riverPass != -1 && o.riverPass != 1 && o.riverPass != 4)
        return Status::fail(Err::BadLayout, "draw: riverPass " + std::to_string(o.riverPass) + " is not -1, 1 or 4");
    if (o.riverStyle >= 5) return Status::fail(Err::BadLayout, "draw: riverStyle " + std::to_string(o.riverStyle) + " outside 0..4");
    Impl& p = *p_;
    {
        ID3D11Resource* res = nullptr;
        rtv->GetResource(&res);
        ID3D11Texture2D* tex = nullptr;
        HRESULT h = res ? res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex)) : E_FAIL;
        rel(res);
        if (FAILED(h) || !tex) return Status::fail(Err::BadLayout, "draw: render target is not a 2D texture");
        D3D11_TEXTURE2D_DESC d;
        tex->GetDesc(&d);
        tex->Release();
        if (d.Width != in.cam.width || d.Height != in.cam.height)
            return Status::fail(Err::BadLayout, "draw: camera " + std::to_string(in.cam.width) + " x " + std::to_string(in.cam.height) +
                                                    " but target " + std::to_string(d.Width) + " x " + std::to_string(d.Height));
    }
    FrameInputs fin = in;
    if (!fin.world.valid) fin.world = p.world;
    // views: the camera, and the map-wrap copies T(-+width) * view as the EXE's views 1, 2 [C structure, I order]
    float shifts[3] = {0, 0, 0};
    uint32_t views = 1;
    if (o.wrapCopies && fin.world.wrapX) {
        shifts[1] = -fin.world.width;
        shifts[2] = fin.world.width;
        views = 3;
    }
    FrameStats st;
    st.oceanPassUsed = o.oceanPass < 0 ? 3 : o.oceanPass;
    st.systemsMissing = 0;
    if (o.rivers && p.meshes[static_cast<uint32_t>(System::River)].draws.empty()) st.systemsMissing |= 1u << static_cast<uint32_t>(System::River);
    if (o.waves && p.meshes[static_cast<uint32_t>(System::Wave)].draws.empty()) st.systemsMissing |= 1u << static_cast<uint32_t>(System::Wave);
    p.frames.assign(views, std::vector<uint8_t>(kWaterFrameBytes, 0));
    Footprint fps[kMaxViews];
    for (uint32_t v = 0; v < views; ++v) {
        FrameInputs vi = fin;
        const float t[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, shifts[v], 0, 0, 1};
        mul44f(t, fin.cam.view, vi.cam.view);
        if (fin.haveCamPos) vi.camPos[0] = fin.camPos[0] - shifts[v];
        if (v > 0 && fin.haveOverlayProjWrap)   // issue 3704: the EXE's own matrix of that wrap view
            std::memcpy(vi.overlayProj, fin.overlayProjWrap[v - 1], sizeof vi.overlayProj);
        if (v > 0 && fin.haveShadowMatrix0Wrap)   // issue 4308: the EXE's own ShadowMatrix0 of that wrap view
            std::memcpy(vi.shadowMatrix0, fin.shadowMatrix0Wrap[v - 1], sizeof vi.shadowMatrix0);
        if (v > 0)   // issue 0382: 0xDA1890 + 0x40v = (g_View[v] x inverse(g_View[0])) x 0xDA1890 = T(shift) x view 0's
            mul44f(t, fin.shadowMatrix1, vi.shadowMatrix1);
        C5R_TRY(computeWaterFrame(vi, p.frames[v].data()));
        float vp[16];
        std::memcpy(vp, p.frames[v].data(), sizeof vp);
        C5R_TRY(viewFootprint(vp, fin.world, fps[v]));
    }
    if (o.ocean) {
        C5R_TRY(buildOceanGeometry(fin.world, fps, views, p.geo));
        st.oceanQuads = p.geo.quads;
        st.indexOverflow = p.geo.indexOverflow;
    } else {
        p.geo = OceanGeometry();
    }

    p.pollQueries(ctx, false);
    GpuQuery* gq = nullptr;
    for (GpuQuery& g : p.q)
        if (!g.issued) { gq = &g; break; }
    if (gq) {
        ctx->Begin(gq->disjoint);
        ctx->End(gq->t0);
    }
    if (dsv && o.clearDepth) ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    if (p.currentsStale && o.computeCurrents) {
        // the EXE computes the map at load; here at the first draw after the field (its own pass changes the state)
        p.currentsStale = false;
        p.currentsReady = p.currents.compute(ctx, p.currentsParams, nullptr).ok();
    }
    if (o.ocean && p.geo.quads) {
        C5R_TRY(p.ensureGeometry(p.geo.quads));
        C5R_TRY(p.upload(ctx, p.vb, p.geo.vertices.data(), p.geo.vertices.size() * 4));
        C5R_TRY(p.upload(ctx, p.ib, p.geo.indices.data(), p.geo.indices.size() * 2));
        D3D11_VIEWPORT vpt = {0, 0, static_cast<float>(in.cam.width), static_cast<float>(in.cam.height), 0, 1};
        ctx->RSSetViewports(1, &vpt);
        ctx->RSSetState(p.rs);
        ctx->OMSetRenderTargets(1, &rtv, dsv);
        ctx->OMSetDepthStencilState(o.oceanDepthWrite ? p.dssWrite : p.dssNoWrite, 0);
        ctx->OMSetBlendState(o.oceanBlend ? p.bsAlpha : p.bsOpaque, nullptr, 0xFFFFFFFFu);
        ctx->IASetInputLayout(p.layout);
        const UINT stride = 16, off = 0;
        ctx->IASetVertexBuffers(0, 1, &p.vb, &stride, &off);
        ctx->IASetIndexBuffer(p.ib, DXGI_FORMAT_R16_UINT, 0);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(p.vs, nullptr, 0);
        ctx->HSSetShader(nullptr, nullptr, 0);
        ctx->DSSetShader(nullptr, nullptr, 0);
        ctx->GSSetShader(nullptr, nullptr, 0);
        ctx->PSSetShader(p.ps[st.oceanPassUsed], nullptr, 0);
        ID3D11Buffer* cbs[2] = {p.cbOwn, p.cbFrame};
        ctx->VSSetConstantBuffers(0, 2, cbs);
        ctx->PSSetConstantBuffers(0, 2, cbs);
        // sampler words of the ocean draw object: t6 3, t8 5, t11 5, every other slot 0 (t13's is never written)
        ID3D11SamplerState* ss[14];
        for (auto& s : ss) s = p.samp[0];
        ss[6] = p.samp[3];
        ss[8] = p.samp[5];
        ss[11] = p.samp[5];
        ctx->PSSetSamplers(0, 14, ss);
        ID3D11ShaderResourceView* srv[14] = {};
        for (uint32_t i = 0; i < 14; ++i) srv[i] = p.given[i] ? p.given[i] : p.own[i];
        if (!p.given[5] && p.currentsReady) srv[5] = p.currents.srv();
        srv[2] = srv[3] = srv[12] = nullptr;  // the ocean draw object leaves t2, t3, t12 unset
        for (uint32_t v = 0; v < views; ++v) {
            C5R_TRY(p.upload(ctx, p.cbFrame, p.frames[v].data(), kWaterFrameBytes));
            for (const OceanBatch& b : p.geo.batches) {
                if (b.view != v) continue;
                srv[7] = p.pages[b.page];
                if (!srv[7]) ++st.pagesMissing;
                ctx->PSSetShaderResources(0, 14, srv);
                ctx->DrawIndexed(b.quads * 6, b.firstQuad * 6, 0);
                ++st.oceanDraws;
            }
        }
        ID3D11ShaderResourceView* none[14] = {};
        ctx->PSSetShaderResources(0, 14, none);
    }
    // rivers (issue 3605): one draw per tapped draw record with view 0's WaterFrame. The river material binds shared
    // buffer 26 = Water+0x34 = view 0's block (the wrap views use 0x134..0x136) [L 3601 +0x34 = 0x1A, C 0x739AE0], so
    // the EXE draws its rivers in view 0 only, not in the map-wrap copies.
    if (o.rivers && p.river.hasMesh()) {
        RiverShared sh;
        for (uint32_t i = 0; i < 14; ++i) sh.srv[i] = p.given[i] ? p.given[i] : p.own[i];
        for (uint32_t i = 0; i < 13; ++i) sh.samplers[i] = p.samp[i];
        C5R_TRY(p.river.draw(ctx, rtv, dsv, in.cam.width, in.cam.height, p.frames[0].data(), sh, o.riverPass, o.riverStyle,
                             &st.riverDraws, &st.riverPassUsed));
    }
    if (gq) {
        ctx->End(gq->t1);
        ctx->End(gq->disjoint);
        gq->issued = true;
        gq->serial = ++p.serial;
    }
    st.gpuMs = p.lastGpuMs;
    st.cpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tStart).count();
    if (stats) *stats = st;
    return Status::Ok();
}

}} // namespace c5r::water
