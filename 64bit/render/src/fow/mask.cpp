// mask.cpp - c5r::fow::MaskRenderer: the fog-of-war mask drawn with the EXE's own FOWDrawMask programs (epic 0168,
// task 3952). Own code, written from the behaviour described in research/05-graphics-composition/fog-of-war.md
// sections 2.1-2.2, 3.3 and 7.1 (D83: no decompiler output here). The programs are read from the player's EXE at run
// time (D85); the hexagon geometry, the per-hexagon matrices and the pass order are built here from that description.
#include <c5r/fow/fow.h>

#include <windows.h>
#include <d3d11.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include "c5r/shaders/shadersection.h"
#include "c5r/terrainsets/terrainsets.h"

namespace c5r {
namespace fow {

namespace {

template <class T> void rel(T*& p)
{
    if (p) p->Release();
    p = nullptr;
}

Status hrOk(HRESULT h, const char* what)
{
    if (SUCCEEDED(h)) return Status::Ok();
    char b[32];
    std::snprintf(b, sizeof b, " (hr 0x%08lx)", static_cast<unsigned long>(h));
    return Status::fail(h == E_OUTOFMEMORY ? Err::OutOfMemory : Err::Device, std::string(what) + b);
}

bool finiteN(const float* m, int n)
{
    for (int i = 0; i < n; ++i)
        if (!std::isfinite(m[i])) return false;
    return true;
}

// Row-vector 4x4 product c = a * b (row-major storage, as the EXE's D3DX matrices).
void mul(const float a[16], const float b[16], float c[16])
{
    for (int r = 0; r < 4; ++r)
        for (int k = 0; k < 4; ++k) {
            float s = 0.0f;
            for (int j = 0; j < 4; ++j) s += a[r * 4 + j] * b[j * 4 + k];
            c[r * 4 + k] = s;
        }
}

// The vertex of the hexagon sets (fog-of-war.md 7.1): POSITION = the corner in cube coordinates, NORMAL = the
// hexagon's slot 0..23 in all three components (the VS indexes cb0 with it), COLOR0 / COLOR1 = 0, TEXCOORD0 = the
// weight (1 inner, 0 outer) twice. 40 bytes, stock's stride.
struct Vertex {
    float pos[3];
    float slot[3];
    uint32_t color0, color1;
    float weight[2];
};
static_assert(sizeof(Vertex) == 40, "FOWDrawMask vertex stride");

const uint32_t kVertsPerHex = 12;      // 6 inner corners (weight 1), 6 outer (weight 0)
const uint32_t kIndicesPerHex = 48;    // 16 triangles: an inner fan of 4 and a ring of 12
const uint32_t kIndicesPerSet = kHexagonsPerDraw * kIndicesPerHex;   // 1,152
const float kOuterScale[2] = {1.33f, 1.40f};                        // set 0 (seen, visible), set 1 (unexplored)

struct PassPlan {
    PlotState state;
    uint32_t program;   // FOWDrawMask pass
    uint32_t set;       // hexagon set
};
// Render order and mapping [L: the captures' cb0, fog-of-war.md 7.1]: seen with program pass 2, visible with pass 1,
// unexplored with pass 0 on the wider set 1.
const PassPlan kPasses[3] = {{PlotState::Seen, 2, 0}, {PlotState::Visible, 1, 0}, {PlotState::Unexplored, 0, 1}};

uint32_t fullChain(uint32_t n)
{
    uint32_t m = 1;
    while (n > 1) { n >>= 1; ++m; }
    return m;
}

Status checkMap(const MapInput& m)
{
    if (!m.states) return Status::fail(Err::BadLayout, "fow mask: map states null");
    if (m.width == 0 || m.height == 0 || m.width > 4096 || m.height > 4096)
        return Status::fail(Err::BadLayout, "fow mask: map size " + std::to_string(m.width) + " x " +
                                                std::to_string(m.height) + " outside 1..4096");
    const size_t n = static_cast<size_t>(m.width) * m.height;
    if (m.stateCount != n)
        return Status::fail(Err::BadLayout, "fow mask: plot table holds " + std::to_string(m.stateCount) +
                                                " states, the map needs " + std::to_string(n));
    if (!finiteN(m.hexToWorld, 16) || !finiteN(m.gridScale, 2) || !finiteN(m.gridOrigin, 2))
        return Status::fail(Err::BadLayout, "fow mask: placement not finite");
    if (m.hexToWorld[15] == 0.0f || (m.hexToWorld[0] == 0.0f && m.hexToWorld[1] == 0.0f) ||
        (m.hexToWorld[4] == 0.0f && m.hexToWorld[5] == 0.0f) || m.gridScale[0] == 0.0f || m.gridScale[1] == 0.0f)
        return Status::fail(Err::BadLayout, "fow mask: placement missing (hexToWorld or gridScale zero)");
    return Status::Ok();
}

Status checkProj(const float* op, uint32_t v)
{
    if (!finiteN(op, 16)) return Status::fail(Err::BadLayout, "fow mask: view " + std::to_string(v) + " OverlayProj not finite");
    if (op[0] == 0.0f || op[5] == 0.0f)
        return Status::fail(Err::BadLayout, "fow mask: view " + std::to_string(v) + " OverlayProj has a zero x or y scale");
    return Status::Ok();
}

// The view's placement matrix A = hexToWorld x Scale(sx, sy, 1) x Translate(-ox, -oy, 0) x OverlayProj; a plot's
// matrix is Translate(hx, hy, -hx-hy) x A (fog-of-war.md 3.3).
void viewMatrix(const MapInput& m, const float op[16], float a[16])
{
    float s[16] = {m.gridScale[0], 0, 0, 0, 0, m.gridScale[1], 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float t[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -m.gridOrigin[0], -m.gridOrigin[1], 0, 1};
    float hs[16], hst[16];
    mul(m.hexToWorld, s, hs);
    mul(hs, t, hst);
    mul(hst, op, a);
}

PlotState stateAt(const MapInput& m, int32_t gx, int32_t gy)
{
    if (gy < 0 || gy >= static_cast<int32_t>(m.height)) return PlotState::Unexplored;
    const int32_t w = static_cast<int32_t>(m.width);
    if (gx < 0 || gx >= w) {
        if (!m.wrapX) return PlotState::Unexplored;
        gx = ((gx % w) + w) % w;
    }
    return m.states[static_cast<size_t>(gy) * m.width + static_cast<size_t>(gx)];
}

} // namespace

void nominalPlacement(MapInput& map)
{
    const float h[16] = {64, 0, 0, 0, 32, 64, 0, 0, 0, 0, 0, 0, 32, 128, 0, 1};
    std::memcpy(map.hexToWorld, h, sizeof h);
    map.gridScale[0] = 1.0f;
    map.gridScale[1] = 0.866f;
    map.gridOrigin[0] = 32.0f * static_cast<float>(map.width);
    map.gridOrigin[1] = 32.0f * static_cast<float>(map.height + 4) * 0.866f;
}

uint32_t convertFowBits(const uint8_t* bits, size_t n, PlotState* out)
{
    uint32_t bad = 0;
    if (!bits || !out) return 0;
    for (size_t i = 0; i < n; ++i) {
        if (bits[i] & ~3u) ++bad;
        out[i] = stateFromFowBits(bits[i]);
    }
    return bad;
}

namespace {

bool invert4(const double m[16], double out[16])
{
    double inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const double det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (!(std::fabs(det) > 1e-300) || !std::isfinite(det)) return false;
    for (int i = 0; i < 16; ++i) out[i] = inv[i] / det;
    return true;
}

// round half away from zero (the EXE's cube rounding rounds each component so)
double roundAway(double v) { return v < 0.0 ? std::ceil(v - 0.5) : std::floor(v + 0.5); }

} // namespace

Status footprintFromCamera(const View& view, Footprint& out)
{
    out = Footprint();
    if (!finiteN(view.viewProj, 16)) return Status::fail(Err::BadLayout, "fow mask: viewProj not finite");
    double vp[16], inv[16];
    for (int i = 0; i < 16; ++i) vp[i] = view.viewProj[i];
    if (!invert4(vp, inv)) return Status::fail(Err::BadLayout, "fow mask: viewProj singular");
    // the footprint: the four frustum corner rays (clip (+-1, +-1, 0..1)) cut with z = 0 and z = 127.5 [C 0x5D0370]
    double mnx = 1e300, mny = 1e300, mxx = -1e300, mxy = -1e300;
    const double planes[2] = {0.0, 127.5};
    const int cx[4] = {-1, 1, 1, -1}, cy[4] = {-1, -1, 1, 1};
    for (double z : planes)
        for (int c = 0; c < 4; ++c) {
            double p[2][3];
            for (int k = 0; k < 2; ++k) {
                const double in[4] = {static_cast<double>(cx[c]), static_cast<double>(cy[c]), static_cast<double>(k), 1.0};
                double o[4] = {0, 0, 0, 0};
                for (int j = 0; j < 4; ++j)
                    for (int i = 0; i < 4; ++i) o[j] += in[i] * inv[i * 4 + j];
                if (!(std::fabs(o[3]) > 1e-300)) return Status::fail(Err::BadLayout, "fow mask: viewProj corner at infinity");
                for (int j = 0; j < 3; ++j) p[k][j] = o[j] / o[3];
            }
            const double dz = p[1][2] - p[0][2];
            if (!(std::fabs(dz) > 1e-12)) return Status::fail(Err::BadLayout, "fow mask: a corner ray parallel to the ground");
            const double t = (z - p[0][2]) / dz;
            const double x = p[0][0] + t * (p[1][0] - p[0][0]), y = p[0][1] + t * (p[1][1] - p[0][1]);
            if (!std::isfinite(x) || !std::isfinite(y)) return Status::fail(Err::BadLayout, "fow mask: footprint not finite");
            mnx = std::min(mnx, x); mxx = std::max(mxx, x);
            mny = std::min(mny, y); mxy = std::max(mxy, y);
        }
    out.x0 = mnx;
    out.x1 = mxx;
    out.y0 = mny;
    out.y1 = mxy;
    return Status::Ok();
}

void windowProjFromBox(double x0, double x1, double y0, double y1, float op[16])
{
    std::memset(op, 0, 16 * sizeof(float));
    op[0] = static_cast<float>(2.0 / (x1 - x0));
    op[5] = static_cast<float>(2.0 / (y1 - y0));
    op[10] = -0.5f;
    op[12] = static_cast<float>(-(x0 + x1) / (x1 - x0));
    op[13] = static_cast<float>(-(y0 + y1) / (y1 - y0));
    op[14] = 0.5f;
    op[15] = 1.0f;
}

Status windowFromCamera(const View& view, float overlayProj[16], Footprint* footprint)
{
    Footprint fp;
    C5R_TRY(footprintFromCamera(view, fp));
    if (footprint) *footprint = fp;
    // the EXE's snap (fog-of-war.md 9.5, overlay-system.md 4): origin and size multiples of 64 world units, one step of
    // margin below the footprint on each axis and none above (cam1 [-1600, -768] x [-64, 640] from x -1530.35..-772.13,
    // y 40.33..586.04)
    const double x0 = std::floor(fp.x0 / 64.0) * 64.0 - 64.0, x1 = std::ceil(fp.x1 / 64.0) * 64.0;
    const double y0 = std::floor(fp.y0 / 64.0) * 64.0 - 64.0, y1 = std::ceil(fp.y1 / 64.0) * 64.0;
    windowProjFromBox(x0, x1, y0, y1, overlayProj);
    return Status::Ok();
}

bool footprintInWindow(const Footprint& fp, const float op[16], double tolerance)
{
    if (!finiteN(op, 16) || op[0] == 0.0f || op[5] == 0.0f) return false;
    double wx0 = (-1.0 - op[12]) / op[0], wx1 = (1.0 - op[12]) / op[0];
    double wy0 = (-1.0 - op[13]) / op[5], wy1 = (1.0 - op[13]) / op[5];
    if (wx0 > wx1) std::swap(wx0, wx1);
    if (wy0 > wy1) std::swap(wy0, wy1);
    return fp.x0 >= wx0 - tolerance && fp.x1 <= wx1 + tolerance && fp.y0 >= wy0 - tolerance && fp.y1 <= wy1 + tolerance;
}

Status uvMapBetweenWindows(const float from[16], const float to[16], UvMap& out)
{
    out = UvMap();
    for (const float* m : {from, to}) {
        if (!finiteN(m, 16)) return Status::fail(Err::BadLayout, "overlay resample: a window matrix not finite");
        if (m[0] == 0.0f || m[5] == 0.0f) return Status::fail(Err::BadLayout, "overlay resample: a window with a zero x or y scale");
        if (m[1] != 0.0f || m[4] != 0.0f) return Status::fail(Err::BadLayout, "overlay resample: a window with an x-y cross term");
    }
    // to: u -> NDC x = 2 u - 1 -> world x = (NDC - tx) / sx; from: u' = 0.5 (x sx' + tx') + 0.5. v runs against NDC y.
    const double sxT = to[0], txT = to[12], syT = to[5], tyT = to[13];
    const double sxF = from[0], txF = from[12], syF = from[5], tyF = from[13];
    out.a[0] = static_cast<float>(sxF / sxT);
    out.b[0] = static_cast<float>(0.5 * (sxF * (-1.0 - txT) / sxT + txF) + 0.5);
    out.a[1] = static_cast<float>(syF / syT);
    out.b[1] = static_cast<float>(0.5 - 0.5 * (syF * (1.0 - tyT) / syT + tyF));
    return Status::Ok();
}

Status plotRangeFromCamera(const MapInput& map, const View& view, int32_t range[4])
{
    range[0] = range[1] = range[2] = range[3] = 0;
    if (map.width == 0 || map.height == 0) return Status::fail(Err::BadLayout, "fow mask: empty map");
    if (!finiteN(map.hexToWorld, 16) || !finiteN(map.gridScale, 2) || map.gridScale[0] == 0.0f || map.gridScale[1] == 0.0f)
        return Status::fail(Err::BadLayout, "fow mask: placement missing or not finite");
    if (!(map.worldWidth > 0.0f) || !(map.worldHeight > 0.0f) || !std::isfinite(map.worldWidth) ||
        !std::isfinite(map.worldHeight))
        return Status::fail(Err::BadLayout, "fow mask: worldWidth / worldHeight not > 0 (camera range rule)");
    Footprint fp;
    C5R_TRY(footprintFromCamera(view, fp));
    const double mnx = fp.x0, mxx = fp.x1, mny = fp.y0, mxy = fp.y1;
    // world -> hex: the inverse of the placement's linear part (row vectors: (hx, hy) x L + T = sample coordinates)
    const float* H = map.hexToWorld;
    const double l00 = H[0] - H[8], l01 = H[1] - H[9], l10 = H[4] - H[8], l11 = H[5] - H[9];
    const double det = l00 * l11 - l01 * l10;
    if (!(std::fabs(det) > 1e-9)) return Status::fail(Err::BadLayout, "fow mask: placement not invertible");
    auto toGrid = [&](double x, double y, int64_t& gx, int64_t& gy) {
        const double sx = (x + 0.5 * map.worldWidth) / map.gridScale[0] - H[12];
        const double sy = (y + 0.5 * map.worldHeight) / map.gridScale[1] - H[13];
        const double h[3] = {(sx * l11 - sy * l10) / det, (sy * l00 - sx * l01) / det, 0.0};
        double hh[3] = {h[0], h[1], -h[0] - h[1]};
        double r[3] = {roundAway(hh[0]), roundAway(hh[1]), roundAway(hh[2])};
        if (r[0] + r[1] + r[2] != 0.0) {
            int i = 0;
            for (int k = 1; k < 3; ++k)
                if (std::fabs(r[k] - hh[k]) > std::fabs(r[i] - hh[i])) i = k;
            r[i] = -(r[(i + 1) % 3] + r[(i + 2) % 3]);
        }
        const int64_t hx = static_cast<int64_t>(r[0]), hy = static_cast<int64_t>(r[1]);
        gy = hy;
        gx = hx + (hy >= 0 ? hy / 2 : -((-hy + 1) / 2));   // hx + (hy >> 1), arithmetic shift
    };
    int64_t gx0, gy0, gx1, gy1;
    toGrid(mnx, mny, gx0, gy0);
    toGrid(mxx, mxy, gx1, gy1);
    const int64_t W = map.width;
    auto clampX = [&](int64_t v) { return static_cast<int32_t>(std::min(std::max(v, int64_t(0)), W)); };
    auto clampY = [&](int64_t v) { return static_cast<int32_t>(std::min(std::max(v, int64_t(-kMaxRangeSpan)), int64_t(kMaxRangeSpan))); };
    range[0] = clampX(gx0 - 2);
    range[1] = std::max(range[0], clampX(gx1 + 2));
    range[2] = clampY(gy0 - 1);
    range[3] = std::max(range[2], clampY(gy1 + 1));
    return Status::Ok();
}

Status plotRangeFromWindow(const MapInput& map, const View& view, int32_t range[4])
{
    range[0] = range[1] = range[2] = range[3] = 0;
    if (map.width == 0 || map.height == 0) return Status::fail(Err::BadLayout, "fow mask: empty map");
    if (!finiteN(map.hexToWorld, 16) || !finiteN(map.gridScale, 2) || !finiteN(map.gridOrigin, 2) ||
        map.hexToWorld[0] == 0.0f || map.hexToWorld[5] == 0.0f || map.gridScale[0] == 0.0f || map.gridScale[1] == 0.0f)
        return Status::fail(Err::BadLayout, "fow mask: placement missing or not finite");
    C5R_TRY(checkProj(view.overlayProj, 0));
    const float* op = view.overlayProj;
    // the window in world (OverlayProj is scale + offset in x and y, overlay-system.md 4)
    double wx0 = (-1.0 - op[12]) / op[0], wx1 = (1.0 - op[12]) / op[0];
    double wy0 = (-1.0 - op[13]) / op[5], wy1 = (1.0 - op[13]) / op[5];
    if (wx0 > wx1) std::swap(wx0, wx1);
    if (wy0 > wy1) std::swap(wy0, wy1);
    const float* H = map.hexToWorld;
    const double sx = map.gridScale[0], sy = map.gridScale[1], ox = map.gridOrigin[0], oy = map.gridOrigin[1];
    // the outer hexagon's half extents: corners (2/3, -1/3, -1/3) etc. x 1.40 through the linear part
    const double ex = 1.40 * std::fabs((2.0 / 3.0) * H[0] - (1.0 / 3.0) * H[4]) * std::fabs(sx);
    const double ey = 1.40 * std::fabs((2.0 / 3.0) * H[5]) * std::fabs(sy);
    // rows: centre y = (hy * H11 + H31) * sy - oy (H01 x hx is below 0.001 world units a plot and ignored here)
    auto rowOf = [&](double y) { return ((y + oy) / sy - H[13]) / H[5]; };
    double r0 = rowOf(wy0 - ey), r1 = rowOf(wy1 + ey);
    if (r0 > r1) std::swap(r0, r1);
    const int32_t H32 = static_cast<int32_t>(map.height), W32 = static_cast<int32_t>(map.width);
    int32_t y0 = static_cast<int32_t>(std::floor(r0)) + 1, y1 = static_cast<int32_t>(std::ceil(r1));
    y0 = std::max(y0, -4);
    y1 = std::min(y1, H32 + 4);
    int32_t x0 = INT32_MAX, x1 = INT32_MIN;
    for (int32_t y = y0; y < y1; ++y) {
        // centre x = ((x - (y >> 1)) * H00 + y * H10 + H30) * sx - ox
        const double base = (static_cast<double>(y) * H[4] - static_cast<double>(y >> 1) * H[0] + H[12]) * sx - ox;
        const double k = H[0] * sx;
        double a = (wx0 - ex - base) / k, b = (wx1 + ex - base) / k;
        if (a > b) std::swap(a, b);
        x0 = std::min(x0, static_cast<int32_t>(std::floor(a)) + 1);
        x1 = std::max(x1, static_cast<int32_t>(std::ceil(b)));
    }
    if (y1 <= y0 || x1 <= x0) return Status::Ok();   // the window misses the map: an empty range
    x0 = std::max(x0, -W32);
    x1 = std::min(x1, 2 * W32);
    range[0] = x0;
    range[1] = std::max(x1, x0);
    range[2] = y0;
    range[3] = y1;
    return Status::Ok();
}

struct MaskRenderer::Impl {
    ID3D11Device* dev = nullptr;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps[3] = {};
    ID3D11InputLayout* layout = nullptr;
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    ID3D11Buffer* cb = nullptr;
    ID3D11BlendState* blend = nullptr;
    ID3D11DepthStencilState* depth = nullptr;
    ID3D11RasterizerState* raster = nullptr;
    ID3D11Texture2D* tex = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    uint32_t size = 0;
    ~Impl()
    {
        rel(srv); rel(rtv); rel(tex); rel(raster); rel(depth); rel(blend); rel(cb); rel(ib); rel(vb); rel(layout);
        for (auto*& p : ps) rel(p);
        rel(vs);
        rel(dev);
    }
};

MaskRenderer::~MaskRenderer() { delete m_impl; }

Status MaskRenderer::init(ID3D11Device* dev, const std::wstring& exePath, uint32_t size)
{
    delete m_impl;
    m_impl = nullptr;
    if (!dev) return Status::fail(Err::BadLayout, "fow mask init: no device");
    if (size != 512 && size != 1024)
        return Status::fail(Err::BadLayout, "fow mask init: size " + std::to_string(size) + " (stock: 1024 or 512)");
    const terrainsets::ShaderSetDesc* set = terrainsets::findSet("FOWDrawMask");
    if (!set) return Status::fail(Err::NotFound, "terrainsets has no FOWDrawMask");
    shaders::ShaderSection sec;
    C5R_TRY(sec.open(exePath));
    std::unique_ptr<Impl> p(new Impl());
    p->dev = dev;
    dev->AddRef();
    p->size = size;
    auto blob = [&](uint32_t va, shaders::Stage st, const shaders::ShaderBlob** out) -> Status {
        char v[16];
        std::snprintf(v, sizeof v, "0x%08x", va);
        const shaders::ShaderBlob* b = va ? sec.findByVa(va) : nullptr;
        if (!b) return Status::fail(Err::NotFound, std::string("FOWDrawMask blob ") + v + " not in the .shader section");
        if (b->stage != st) return Status::fail(Err::BadLayout, std::string("FOWDrawMask blob ") + v + " has another stage");
        *out = b;
        return Status::Ok();
    };
    uint32_t vsVa = 0;
    for (uint32_t pass = 0; pass < 3; ++pass) {
        const terrainsets::PassDesc* pd = nullptr;
        for (const terrainsets::PassDesc& d : set->passes)
            if (d.index == pass) pd = &d;
        if (!pd) return Status::fail(Err::NotFound, "FOWDrawMask pass " + std::to_string(pass) + " not described");
        const shaders::ShaderBlob *bv = nullptr, *bp = nullptr;
        C5R_TRY(blob(pd->sm50[0], shaders::Stage::Vertex, &bv));
        C5R_TRY(blob(pd->sm50[1], shaders::Stage::Pixel, &bp));
        if (!p->vs) {
            vsVa = pd->sm50[0];
            C5R_TRY(hrOk(dev->CreateVertexShader(bv->bytes, bv->size, nullptr, &p->vs), "CreateVertexShader (FOWDrawMask)"));
            const D3D11_INPUT_ELEMENT_DESC e[5] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0}};
            C5R_TRY(hrOk(dev->CreateInputLayout(e, 5, bv->bytes, bv->size, &p->layout), "CreateInputLayout (FOWDrawMask)"));
        } else if (pd->sm50[0] != vsVa) {
            return Status::fail(Err::BadLayout, "FOWDrawMask passes 0-2 do not share one VS");
        }
        C5R_TRY(hrOk(dev->CreatePixelShader(bp->bytes, bp->size, nullptr, &p->ps[pass]), "CreatePixelShader (FOWDrawMask)"));
    }
    // geometry: two sets of 24 hexagons, 12 vertices and 48 indices each
    {
        std::vector<Vertex> v(2 * kHexagonsPerDraw * kVertsPerHex);
        std::vector<uint32_t> ix(2 * kIndicesPerSet);
        const float t = 1.0f / 3.0f, u = 2.0f / 3.0f;
        const float corner[6][3] = {{u, -t, -t}, {t, t, -u}, {-t, u, -t}, {-u, t, t}, {-t, -t, u}, {t, -u, t}};
        for (uint32_t s = 0; s < 2; ++s)
            for (uint32_t h = 0; h < kHexagonsPerDraw; ++h) {
                const uint32_t base = (s * kHexagonsPerDraw + h) * kVertsPerHex;
                for (uint32_t c = 0; c < 6; ++c)
                    for (uint32_t o = 0; o < 2; ++o) {
                        Vertex& x = v[base + o * 6 + c];
                        const float k = o ? kOuterScale[s] : 1.0f;
                        for (int i = 0; i < 3; ++i) {
                            x.pos[i] = corner[c][i] * k;
                            x.slot[i] = static_cast<float>(h);
                        }
                        x.color0 = x.color1 = 0;
                        x.weight[0] = x.weight[1] = o ? 0.0f : 1.0f;
                    }
                uint32_t* q = &ix[s * kIndicesPerSet + h * kIndicesPerHex];
                const uint32_t fan[12] = {5, 1, 0, 5, 4, 1, 1, 4, 2, 4, 3, 2};
                for (uint32_t i = 0; i < 12; ++i) *q++ = base + fan[i];
                for (uint32_t a = 0; a < 6; ++a) {
                    const uint32_t b = (a + 1) % 6;
                    const uint32_t tri[6] = {b, 6 + b, a, 6 + b, 6 + a, a};
                    for (uint32_t i = 0; i < 6; ++i) *q++ = base + tri[i];
                }
            }
        D3D11_BUFFER_DESC d = {};
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.ByteWidth = static_cast<UINT>(v.size() * sizeof(Vertex));
        d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA sd = {v.data(), 0, 0};
        C5R_TRY(hrOk(dev->CreateBuffer(&d, &sd, &p->vb), "CreateBuffer (FOW hexagon VB)"));
        d.ByteWidth = static_cast<UINT>(ix.size() * sizeof(uint32_t));
        d.BindFlags = D3D11_BIND_INDEX_BUFFER;
        sd.pSysMem = ix.data();
        C5R_TRY(hrOk(dev->CreateBuffer(&d, &sd, &p->ib), "CreateBuffer (FOW hexagon IB)"));
    }
    {
        D3D11_BUFFER_DESC d = {};
        d.Usage = D3D11_USAGE_DYNAMIC;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        d.ByteWidth = kHexagonsPerDraw * 64;   // 0x600
        d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        C5R_TRY(hrOk(dev->CreateBuffer(&d, nullptr, &p->cb), "CreateBuffer (FOWDrawMask cb0)"));
    }
    {
        D3D11_BLEND_DESC b = {};
        b.RenderTarget[0].BlendEnable = TRUE;
        b.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        b.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
        b.RenderTarget[0].BlendOp = D3D11_BLEND_OP_MAX;
        b.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        b.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        b.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_MAX;
        b.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        C5R_TRY(hrOk(dev->CreateBlendState(&b, &p->blend), "CreateBlendState (MAX)"));
        D3D11_DEPTH_STENCIL_DESC z = {};
        z.DepthEnable = FALSE;
        z.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        z.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        C5R_TRY(hrOk(dev->CreateDepthStencilState(&z, &p->depth), "CreateDepthStencilState (off)"));
        D3D11_RASTERIZER_DESC r = {};
        r.FillMode = D3D11_FILL_SOLID;
        r.CullMode = D3D11_CULL_NONE;
        r.FrontCounterClockwise = TRUE;
        r.DepthClipEnable = TRUE;
        C5R_TRY(hrOk(dev->CreateRasterizerState(&r, &p->raster), "CreateRasterizerState"));
    }
    {
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = d.Height = size;
        d.MipLevels = fullChain(size);
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        d.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        C5R_TRY(hrOk(dev->CreateTexture2D(&d, nullptr, &p->tex), "CreateTexture2D (FOW Mask)"));
        D3D11_RENDER_TARGET_VIEW_DESC rv = {};
        rv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        C5R_TRY(hrOk(dev->CreateRenderTargetView(p->tex, &rv, &p->rtv), "CreateRenderTargetView (FOW Mask)"));
        D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MipLevels = d.MipLevels;
        C5R_TRY(hrOk(dev->CreateShaderResourceView(p->tex, &sv, &p->srv), "CreateShaderResourceView (FOW Mask)"));
    }
    m_impl = p.release();
    return Status::Ok();
}

Status MaskRenderer::build(ID3D11DeviceContext* ctx, const MaskInput& in, MaskStats* stats)
{
    if (stats) *stats = MaskStats();
    if (!m_impl) return Status::fail(Err::NotFound, "fow mask build: init not done");
    if (!ctx) return Status::fail(Err::BadLayout, "fow mask build: no context");
    const MapInput& m = in.map;
    C5R_TRY(checkMap(m));
    const size_t n = m.stateCount;
    for (size_t i = 0; i < n; ++i)
        if (static_cast<uint8_t>(m.states[i]) > 2)
            return Status::fail(Err::BadLayout, "fow mask: plot " + std::to_string(i) + " has state " +
                                                    std::to_string(static_cast<unsigned>(static_cast<uint8_t>(m.states[i]))));
    if (!in.views) return Status::fail(Err::BadLayout, "fow mask: views null");
    if (in.numViews == 0 || in.numViews > kMaxViews)
        return Status::fail(Err::BadLayout, "fow mask: numViews " + std::to_string(in.numViews) + " outside 1..4");
    int32_t ranges[kMaxViews][4];
    float place[kMaxViews][16];
    for (uint32_t v = 0; v < in.numViews; ++v) {
        const View& vw = in.views[v];
        C5R_TRY(checkProj(vw.overlayProj, v));
        if (vw.x1 > vw.x0) {
            ranges[v][0] = vw.x0; ranges[v][1] = vw.x1; ranges[v][2] = vw.y0; ranges[v][3] = std::max(vw.y1, vw.y0);
        } else {
            bool cam = false;
            for (float f : vw.viewProj) cam = cam || f != 0.0f;
            if (cam && m.worldWidth > 0.0f && m.worldHeight > 0.0f)
                C5R_TRY(plotRangeFromCamera(m, vw, ranges[v]));
            else
                C5R_TRY(plotRangeFromWindow(m, vw, ranges[v]));
        }
        if (static_cast<int64_t>(ranges[v][1]) - ranges[v][0] > kMaxRangeSpan ||
            static_cast<int64_t>(ranges[v][3]) - ranges[v][2] > kMaxRangeSpan)
            return Status::fail(Err::BadLayout, "fow mask: view " + std::to_string(v) + " range wider than " +
                                                    std::to_string(kMaxRangeSpan) + " plots");
        viewMatrix(m, vw.overlayProj, place[v]);
        if (!finiteN(place[v], 16)) return Status::fail(Err::BadLayout, "fow mask: view " + std::to_string(v) + " matrix not finite");
    }

    Impl& p = *m_impl;
    const float zero[4] = {0, 0, 0, 0};
    ctx->ClearRenderTargetView(p.rtv, zero);
    ctx->OMSetRenderTargets(1, &p.rtv, nullptr);
    const float bf[4] = {0, 0, 0, 0};
    ctx->OMSetBlendState(p.blend, bf, 0xFFFFFFFFu);
    ctx->OMSetDepthStencilState(p.depth, 0);
    ctx->RSSetState(p.raster);
    D3D11_VIEWPORT vp = {0, 0, static_cast<float>(p.size), static_cast<float>(p.size), 0, 1};
    ctx->RSSetViewports(1, &vp);
    const UINT stride = sizeof(Vertex), offset = 0;
    ctx->IASetInputLayout(p.layout);
    ctx->IASetVertexBuffers(0, 1, &p.vb, &stride, &offset);
    ctx->IASetIndexBuffer(p.ib, DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(p.vs, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, &p.cb);
    ctx->PSSetConstantBuffers(0, 1, &p.cb);

    MaskStats st;
    float batch[kHexagonsPerDraw][16];
    Status err = Status::Ok();
    for (uint32_t pi = 0; pi < 3 && err.ok(); ++pi) {
        const PassPlan& pp = kPasses[pi];
        ctx->PSSetShader(p.ps[pp.program], nullptr, 0);
        uint32_t inBatch = 0, drawn = 0;
        auto flush = [&]() {
            if (!inBatch || !err.ok()) return;
            std::memset(&batch[inBatch][0], 0, (kHexagonsPerDraw - inBatch) * sizeof batch[0]);
            D3D11_MAPPED_SUBRESOURCE ms = {};
            err = hrOk(ctx->Map(p.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms), "Map (FOWDrawMask cb0)");
            if (!err.ok()) return;
            std::memcpy(ms.pData, batch, sizeof batch);
            ctx->Unmap(p.cb, 0);
            ctx->DrawIndexed(kIndicesPerSet, pp.set * kIndicesPerSet, 0);
            ++st.draws;
            inBatch = 0;
        };
        for (uint32_t v = 0; v < in.numViews && err.ok(); ++v) {
            const int32_t* r = ranges[v];
            const float* a = place[v];
            for (int32_t gx = r[0]; gx < r[1]; ++gx)       // stock's order: x outer, y inner [L cb0 slot order]
                for (int32_t gy = r[2]; gy < r[3]; ++gy) {
                    if (stateAt(m, gx, gy) != pp.state) continue;
                    if (drawn >= kMaxHexagonsPerPass) { ++st.dropped; continue; }
                    ++drawn;
                    const float hx = static_cast<float>(gx - (gy >> 1)), hy = static_cast<float>(gy), hz = -hx - hy;
                    float* o = batch[inBatch];
                    std::memcpy(o, a, 12 * sizeof(float));
                    for (int c = 0; c < 4; ++c) o[12 + c] = hx * a[c] + hy * a[4 + c] + hz * a[8 + c] + a[12 + c];
                    if (++inBatch == kHexagonsPerDraw) flush();
                }
        }
        flush();
        st.hexagons[pi] = drawn;
    }
    ID3D11RenderTargetView* none = nullptr;
    ctx->OMSetRenderTargets(1, &none, nullptr);
    if (!err.ok()) return err;
    ctx->GenerateMips(p.srv);
    if (stats) *stats = st;
    return Status::Ok();
}

ID3D11ShaderResourceView* MaskRenderer::srv() const { return m_impl ? m_impl->srv : nullptr; }
uint32_t MaskRenderer::size() const { return m_impl ? m_impl->size : 0; }

} // namespace fow
} // namespace c5r
