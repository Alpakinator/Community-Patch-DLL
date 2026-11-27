// bakegrid: the height field's per-sample normals, the CacheBake grid normals and the blend's slope classes
// (issue 3405). Written from research/05-graphics-composition/terrain-bake-passes.md 5.1 and terrain-bake-records.md
// 2.2a (words and tables only, D83). Every float operation is single precision, one rounding per operation, in the
// order the research states; the build uses /fp:precise and no contraction, so the compiler keeps that order.
#include "c5r/bake/grid.h"

#include <xmmintrin.h>

#include <cmath>
#include <cstring>
#include <string>

namespace c5r {
namespace bake {
namespace {

constexpr float k255 = 0.00392156885936856270f;  // 1/255 as the EXE's float word 0x3B808081

// 2.2a step 3: one weighted tap.
inline void tap(float p, float v, float k, float& num, float& den) {
    float diff = p - v;
    float d = diff * diff;
    d = d * -3.33333325386047363281f;  // 0xC0555555
    d = d * 6.103515625e-05f;          // 0x38800000 = 1/16384
    float w = d * d;
    w = w * 0.5f;
    w = w + d;
    w = w + 1.0f;
    w = w * 1.02999997138977050781f;   // 0x3F83D70A
    w = w * k;
    num = num + w * v;
    den = den + std::fabs(w);
}

// 2.2a kernels: outer products of A (derivative) and B (smoothing), each entry one float multiply.
struct Kernels {
    float x[5][5];
    float y[5][5];
    Kernels() {
        const float A[5] = {0.0702f, 0.3526f, 0.0f, -0.3526f, -0.0702f};
        const float B[5] = {0.0228f, 0.2293f, 0.4948f, 0.2293f, 0.0228f};
        for (int i = 0; i < 5; ++i)
            for (int k = 0; k < 5; ++k) {
                x[i][k] = A[i] * B[k];
                y[i][k] = B[i] * A[k];
            }
    }
};

const Kernels& kernels() {
    static const Kernels k;
    return k;
}

Status checkHeights(const heightfield::HeightGrid& g) {
    if (g.samplesX == 0 || g.samplesY == 0) return Status::fail(Err::BadLayout, "height field has no samples");
    if (g.h.size() != static_cast<size_t>(g.samplesX) * g.samplesY)
        return Status::fail(Err::BadLayout, "height field size mismatch: " + std::to_string(g.h.size()) + " heights for " +
                                                std::to_string(g.samplesX) + "x" + std::to_string(g.samplesY));
    if (!std::isfinite(g.sx) || !std::isfinite(g.sy) || !std::isfinite(g.sz))
        return Status::fail(Err::BadLayout, "height field scales not finite");
    return Status::Ok();
}

}  // namespace

Status sampleNormals(const heightfield::HeightGrid& g, int32_t x0, int32_t y0, uint32_t w, uint32_t h,
                     std::vector<float>& out) {
    C5R_TRY(checkHeights(g));
    if (w == 0 || h == 0 || w > kMaxNormalWindow || h > kMaxNormalWindow)
        return Status::fail(Err::BadLayout, "normal window " + std::to_string(w) + "x" + std::to_string(h));
    const int64_t W = g.samplesX, H = g.samplesY;
    // The window with a margin of 2: column c = x - x0 + 2, row r = y - y0 + 2.
    const size_t ww = static_cast<size_t>(w) + 4, wh = static_cast<size_t>(h) + 4;
    std::vector<int32_t> hi(ww * wh);
    std::vector<float> hf(ww * wh);
    std::vector<uint32_t> col(ww);
    for (size_t c = 0; c < ww; ++c) {
        int64_t x = static_cast<int64_t>(x0) - 2 + static_cast<int64_t>(c);
        if (g.wrapX) x = ((W * 100 + x) % W + W) % W;  // the EXE: (samplesX * 100 + x) mod samplesX
        if (x > W - 1) x = W - 1;
        if (x < 0) x = 0;
        col[c] = static_cast<uint32_t>(x);
    }
    for (size_t r = 0; r < wh; ++r) {
        int64_t y = static_cast<int64_t>(y0) - 2 + static_cast<int64_t>(r);
        if (y > H - 1) y = H - 1;  // y is clamped, never wrapped
        if (y < 0) y = 0;
        for (size_t c = 0; c < ww; ++c) {
            const int32_t v = g.h[g.index(col[c], static_cast<uint32_t>(y))];
            hi[r * ww + c] = v;
            hf[r * ww + c] = static_cast<float>(v) * k255;
        }
    }
    auto I = [&](size_t c, size_t r) { return hi[r * ww + c]; };
    auto F = [&](size_t c, size_t r) { return hf[r * ww + c]; };
    const Kernels& K = kernels();
    out.assign(static_cast<size_t>(w) * h * 3, 0.0f);
    for (size_t r = 2; r < wh - 2; ++r) {
        for (size_t c = 2; c < ww - 2; ++c) {
            // Arithmetic shift of the signed difference (2.2a step 1).
            const int32_t gx = (I(c + 1, r) - I(c - 1, r)) >> 1;
            const int32_t gy = (I(c, r + 1) - I(c, r - 1)) >> 1;
            const float centre = F(c, r);
            float px = static_cast<float>(gx) * k255;
            px = px * -2.0f;
            px = px + centre;
            float py = static_cast<float>(gy) * k255;
            py = py * -2.0f;
            py = py + centre;
            float nxs = 0.0f, dxs = 0.0f, nys = 0.0f, dys = 0.0f;
            // x sum: columns -2, -1, +1, +2; the prediction advances by 1.0 per column (the EXE's quirk).
            float p = px;
            for (int dr = -1; dr <= 1; ++dr) tap(p, F(c - 2, r + dr), K.x[0][dr + 2], nxs, dxs);
            p = p + 1.0f;
            for (int dr = -2; dr <= 2; ++dr) tap(p, F(c - 1, r + dr), K.x[1][dr + 2], nxs, dxs);
            p = p + 1.0f;
            p = p + 1.0f;
            for (int dr = -2; dr <= 2; ++dr) tap(p, F(c + 1, r + dr), K.x[3][dr + 2], nxs, dxs);
            p = p + 1.0f;
            for (int dr = -1; dr <= 1; ++dr) tap(p, F(c + 2, r + dr), K.x[4][dr + 2], nxs, dxs);
            // y sum: rows -2, -1, +1, +2.
            p = py;
            for (int dc = -1; dc <= 1; ++dc) tap(p, F(c + dc, r - 2), K.y[dc + 2][0], nys, dys);
            p = p + 1.0f;
            for (int dc = -2; dc <= 2; ++dc) tap(p, F(c + dc, r - 1), K.y[dc + 2][1], nys, dys);
            p = p + 1.0f;
            p = p + 1.0f;
            for (int dc = -2; dc <= 2; ++dc) tap(p, F(c + dc, r + 1), K.y[dc + 2][3], nys, dys);
            p = p + 1.0f;
            for (int dc = -1; dc <= 1; ++dc) tap(p, F(c + dc, r + 2), K.y[dc + 2][4], nys, dys);
            const float Dx = nxs / dxs;
            const float Dy = nys / dys;
            const float a = -(Dx * g.sz);
            const float b = -(Dy * g.sz);
            float v0 = b * 0.0f;
            const float t0 = g.sy * a;
            v0 = v0 - t0;
            float v1 = a * 0.0f;
            const float t1 = b * g.sx;
            v1 = v1 - t1;
            const float v2 = g.sy * g.sx;
            // D3DXVec3Normalize as measured against d3dx9_42 (terrain-bake-passes.md 5.1): the squared length summed
            // x, y, z, the processor's reciprocal square root estimate (rsqrtss), one Newton-Raphson step
            // (0.5 r) (3 - (l r) r), then a multiply. A division by the length or an exact reciprocal differ in the
            // last bit and move a half at a rounding tie (14 of 107,736 vertices in the check).
            float len2 = v0 * v0;
            len2 = len2 + v1 * v1;
            len2 = len2 + v2 * v2;
            float* o = &out[((r - 2) * w + (c - 2)) * 3];
            if (len2 > 0.0f) {
                const float est = _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(len2)));
                const float half = 0.5f * est;
                const float t = (len2 * est) * est;
                const float inv = half * (3.0f - t);
                o[0] = v0 * inv;
                o[1] = v1 * inv;
                o[2] = v2 * inv;
            }
        }
    }
    return Status::Ok();
}

Status gridNormals(const heightfield::HeightGrid& g, const TileGeom& tile, std::vector<float>& out) {
    if (tile.extentX == 0 || tile.extentY == 0 || tile.extentX > kTileSamples + 1 || tile.extentY > kTileSamples + 1)
        return Status::fail(Err::BadLayout, "tile extent " + std::to_string(tile.extentX) + "x" + std::to_string(tile.extentY));
    return sampleNormals(g, tile.originX - 1, tile.originY - 1, tile.extentX + 2, tile.extentY + 2, out);
}

uint16_t floatToHalf(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    const uint32_t sign = (u >> 16) & 0x8000u;
    const uint32_t a = u & 0x7FFFFFFFu;
    if (a >= 0x7F800000u) return static_cast<uint16_t>(sign | (a > 0x7F800000u ? 0x7E00u | ((a >> 13) & 0x3FFu) : 0x7C00u));
    if (a >= 0x477FF000u) return static_cast<uint16_t>(sign | 0x7C00u);  // rounds to >= 65520: infinity
    if (a < 0x38800000u) {
        // Subnormal half (or zero): value = m * 2^-24, round to nearest even.
        if (a < 0x33000000u) return static_cast<uint16_t>(sign);  // below half of the smallest subnormal
        const uint32_t e = a >> 23;
        const uint32_t m = (a & 0x7FFFFFu) | 0x800000u;
        const uint32_t shift = 126u - e;  // value = m * 2^(e-150), in units of 2^-24: m >> (126 - e)
        uint32_t q = m >> shift;
        const uint32_t rem = m & ((1u << shift) - 1u);
        const uint32_t halfway = 1u << (shift - 1u);
        if (rem > halfway || (rem == halfway && (q & 1u))) ++q;
        return static_cast<uint16_t>(sign | q);
    }
    uint32_t v = a - 0x38000000u;  // rebias exponent 127 -> 15
    const uint32_t rem = v & 0x1FFFu;
    v >>= 13;
    if (rem > 0x1000u || (rem == 0x1000u && (v & 1u))) ++v;
    return static_cast<uint16_t>(sign | v);
}

float halfToFloat(uint16_t h) {
    const uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
    const uint32_t e = (h >> 10) & 0x1Fu;
    const uint32_t m = h & 0x3FFu;
    uint32_t u;
    if (e == 0x1Fu) {
        u = sign | 0x7F800000u | (m << 13);
    } else if (e != 0) {
        u = sign | ((e + 112u) << 23) | (m << 13);
    } else if (m == 0) {
        u = sign;
    } else {
        float f = static_cast<float>(m) * 5.9604644775390625e-08f;  // m * 2^-24, exact
        std::memcpy(&u, &f, 4);
        u |= sign;
    }
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

Status packGridNormals(const std::vector<float>& normals, std::vector<uint16_t>& out) {
    if (normals.size() % 3 != 0)
        return Status::fail(Err::BadLayout, "normal array of " + std::to_string(normals.size()) + " floats is not float3");
    const size_t n = normals.size() / 3;
    out.resize(n * 4);
    const uint16_t one = floatToHalf(1.0f);
    for (size_t i = 0; i < n; ++i) {
        out[i * 4 + 0] = floatToHalf(normals[i * 3 + 0]);
        out[i * 4 + 1] = floatToHalf(normals[i * 3 + 1]);
        out[i * 4 + 2] = floatToHalf(normals[i * 3 + 2]);
        out[i * 4 + 3] = one;
    }
    return Status::Ok();
}

Status slopeClassesFromNormals(const std::vector<float>& normals, SlopeClasses& out) {
    if (normals.size() % 3 != 0)
        return Status::fail(Err::BadLayout, "normal array of " + std::to_string(normals.size()) + " floats is not float3");
    const size_t n = normals.size() / 3;
    out.resize(n);
    for (size_t i = 0; i < n; ++i) out[i] = slopeClass(normals[i * 3 + 0], normals[i * 3 + 1], normals[i * 3 + 2]);
    return Status::Ok();
}

Status tileSlopeClasses(const heightfield::HeightGrid& g, const TileGeom& tile, SlopeClasses& out,
                        std::vector<float>* normals) {
    std::vector<float> local;
    std::vector<float>& n = normals ? *normals : local;
    C5R_TRY(gridNormals(g, tile, n));
    return slopeClassesFromNormals(n, out);
}

}  // namespace bake
}  // namespace c5r
