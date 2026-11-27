// c5r::bake blend, CPU side: the kernel, the per-layer weights, the 48-byte vertices, the indices and the constant
// blocks of the bake's blend pass (issue 3402). Written from research/05-graphics-composition/terrain-bake-blend.md
// sections 2 and 2.5 (D83). Every float operation is single precision and in the order the description gives, except
// the position x, which the EXE computes in double precision (x87) and rounds once.
#include "c5r/bake/blend.h"

#include <cmath>
#include <cstring>
#include <sstream>

namespace c5r {
namespace bake {

namespace {

float bitsToFloat(uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

// Constants of the description (values, not code): kernel row factors, the 1/255 height factor, the slope threshold,
// the TerrainLayers repeat factors.
const float kK2 = bitsToFloat(0x3e000000u);     // 0.125
const float kK1 = bitsToFloat(0x3f22d491u);     // 0.636056
const float kInv255 = bitsToFloat(0x3b808081u); // 1/255
const float kSlopeNz = bitsToFloat(0x3f666666u);// 0.9
const float kTop = bitsToFloat(0x3ea8f5c3u);    // 0.33
const float kSide = bitsToFloat(0x3f28f5c3u);   // 0.66
const float kTopMul = bitsToFloat(0x4041f07cu); // 3.030303
const float kSideMul = bitsToFloat(0x3fc1f07cu);// 1.5151515

Status checkTables(const LayerTables& t) {
    if (t.layerCount == 0 || t.layerCount > 256)
        return Status::fail(Err::BadLayout, "layer tables: layerCount " + std::to_string(t.layerCount) + " not in 1..256");
    return Status::Ok();
}

Status checkList(const LayerTables& t, const LayerList& list) {
    if (list.count > kMaxLayers)
        return Status::fail(Err::BadLayout, "layer list: count " + std::to_string(list.count) + " > 8");
    for (uint32_t k = 0; k < list.count; ++k)
        if (list.ids[k] >= t.layerCount)
            return Status::fail(Err::BadLayout, "layer list: id " + std::to_string(list.ids[k]) + " >= layerCount " +
                                                    std::to_string(t.layerCount));
    return Status::Ok();
}

// x mod n the EXE's way for the kernel taps: the sum is taken as an unsigned 32-bit value, without adding n first.
uint32_t umod(int32_t v, uint32_t n) { return static_cast<uint32_t>(v) % n; }

}  // namespace

std::vector<KernelTap> defaultKernel() {
    const float f[5] = {kK2, kK1, 1.0f, kK1, kK2};
    std::vector<KernelTap> k;
    float sum = 0.0f;
    for (int i = 0; i < 5; ++i) {
        const float r = f[i];
        const float a = r * kK2;  // |dx| = 2
        const float b = r * kK1;  // |dx| = 1
        const float w[5] = {a, b, r, b, a};
        for (int j = 0; j < 5; ++j) {
            KernelTap t;
            t.dx = j - 2;
            t.dy = i - 2;
            t.w = w[j];
            k.push_back(t);
        }
        sum = ((((a + b) + r) + b) + a) + sum;
    }
    const float inv = 1.0f / sum;
    for (KernelTap& t : k) t.w = inv * t.w;
    return k;
}

uint8_t slopeClass(float nx, float ny, float nz) {
    if (nz > kSlopeNz) return 0;  // NaN falls through to a slope class, as in the EXE
    return std::fabs(ny) > std::fabs(nx) ? 1 : 2;
}

static Status checkInputs(const BlendInputs& in) {
    if (!in.grid || !in.tables) return Status::fail(Err::BadLayout, "blend inputs: grid or tables missing");
    const heightfield::HeightGrid& g = *in.grid;
    if (g.samplesX == 0 || g.samplesY == 0 || g.h.size() != size_t(g.samplesX) * g.samplesY)
        return Status::fail(Err::BadLayout, "blend inputs: height grid size inconsistent");
    if (g.mat.size() != g.h.size()) return Status::fail(Err::BadLayout, "blend inputs: the grid has no material bytes");
    C5R_TRY(checkTables(*in.tables));
    if (in.kernel.empty() || in.kernel.size() > kMaxKernelTaps)
        return Status::fail(Err::BadLayout, "blend inputs: kernel has " + std::to_string(in.kernel.size()) + " taps");
    return Status::Ok();
}

static Status weightsUnchecked(const BlendInputs& in, int32_t sx, int32_t sy, uint8_t cls, std::vector<float>& W) {
    const heightfield::HeightGrid& g = *in.grid;
    const LayerTables& t = *in.tables;
    W.assign(t.layerCount, 0.0f);
    for (const KernelTap& k : in.kernel) {
        const uint32_t mx = umod(k.dx + sx, g.samplesX);
        const uint32_t my = umod(k.dy + sy, g.samplesY);
        const uint8_t m = g.mat[g.index(mx, my)];
        uint32_t l = t.firstLayerOfMaterial[m];
        if (cls != 0 && t.slopeProjected[m] == 1) l += cls;
        if (l >= t.layerCount)
            return Status::fail(Err::BadLayout, "material " + std::to_string(m) + " maps to layer " + std::to_string(l) +
                                                    " >= layerCount");
        W[l] = k.w + W[l];
    }
    float even = 0.0f, odd = 0.0f, last = 0.0f;
    uint32_t i = 0;
    const uint32_t n = t.layerCount;
    if (n >= 2) {
        for (; i < n - 1; i += 2) {
            even = W[i] + even;
            odd = W[i + 1] + odd;
        }
    }
    if (i < n) last = W[i];
    const float inv = 1.0f / ((odd + even) + last);
    for (float& w : W) w = w * inv;
    return Status::Ok();
}

Status layerWeights(const BlendInputs& in, int32_t sx, int32_t sy, uint8_t cls, std::vector<float>& out) {
    C5R_TRY(checkInputs(in));
    if (cls > 2) return Status::fail(Err::BadLayout, "slope class " + std::to_string(cls) + " > 2");
    return weightsUnchecked(in, sx, sy, cls, out);
}

Status fillBlendVertices(const BlendInputs& in, const TileGeom& tile, const LayerList& list, const SlopeClasses& cls,
                         std::vector<BlendVertex>& out) {
    out.clear();
    C5R_TRY(checkInputs(in));
    C5R_TRY(checkList(*in.tables, list));
    if (tile.extentX == 0 || tile.extentY == 0 || tile.extentX > kTileSamples + 1 || tile.extentY > kTileSamples + 1)
        return Status::fail(Err::BadLayout, "tile extent " + std::to_string(tile.extentX) + " x " +
                                                std::to_string(tile.extentY) + " not in 1..65");
    const uint32_t gw = tile.extentX + 2, gh = tile.extentY + 2;
    if (!cls.empty() && cls.size() != size_t(gw) * gh)
        return Status::fail(Err::BadLayout, "slope classes: " + std::to_string(cls.size()) + " entries for a " +
                                                std::to_string(gw) + " x " + std::to_string(gh) + " grid");
    for (uint8_t c : cls)
        if (c > 2) return Status::fail(Err::BadLayout, "slope class " + std::to_string(c) + " > 2");
    const heightfield::HeightGrid& g = *in.grid;
    out.resize(size_t(gw) * gh);
    std::vector<float> W;
    for (uint32_t j = 0; j < gh; ++j) {
        const float u = static_cast<float>(j) / static_cast<float>(tile.extentY + 1);
        const float py = 1.0f - u * 2.0f;
        for (uint32_t i = 0; i < gw; ++i) {
            BlendVertex& v = out[size_t(j) * gw + i];
            std::memset(&v, 0, sizeof v);
            const int32_t sx = tile.originX + static_cast<int32_t>(i) - 1;
            const int32_t sy = tile.originY + static_cast<int32_t>(j) - 1;
            const double td = static_cast<double>(i) / static_cast<double>(tile.extentX + 1);
            v.pos[0] = static_cast<float>(td + td - 1.0);
            v.pos[1] = py;
            v.pos[2] = 0.5f;
            const float fx = static_cast<float>(sx), fy = static_cast<float>(sy);
            v.sample[0] = fx;
            v.sample[1] = fy;
            v.world[0] = in.stepX * fx;
            v.world[1] = in.stepY * fy;
            const uint32_t hx = static_cast<uint32_t>(static_cast<int64_t>(g.samplesX) + sx) % g.samplesX;
            const uint32_t hy = static_cast<uint32_t>(static_cast<int64_t>(g.samplesY) + sy) % g.samplesY;
            v.world[2] = (static_cast<float>(g.h[g.index(hx, hy)]) * kInv255) * in.stepZ;
            const uint8_t c = cls.empty() ? 0 : cls[size_t(j) * gw + i];
            Status ws = weightsUnchecked(in, sx, sy, c, W);
            if (!ws.ok()) {
                out.clear();
                return ws;
            }
            float sel[kMaxLayers] = {};
            float even = 0.0f, odd = 0.0f, last = 0.0f;
            uint32_t k = 0;
            if (list.count >= 2) {
                for (; k < list.count - 1; k += 2) {
                    sel[k] = W[list.ids[k]];
                    sel[k + 1] = W[list.ids[k + 1]];
                    even = even + sel[k];
                    odd = odd + sel[k + 1];
                }
            }
            if (k < list.count) {
                sel[k] = W[list.ids[k]];
                last = sel[k];
            }
            if ((even + odd) + last < 0.001f) sel[0] = 1.0f;
            for (uint32_t s = 0; s < kMaxLayers; ++s) {
                v.weight[s] = static_cast<uint8_t>(static_cast<int32_t>(sel[s] * 255.0f));
                v.layer[s] = s < list.count ? static_cast<uint8_t>(list.ids[s]) : 0;
            }
        }
    }
    return Status::Ok();
}

Status buildBlendIndices(uint32_t gridW, uint32_t gridH, std::vector<uint16_t>& out) {
    out.clear();
    if (gridW < 2 || gridH < 2 || size_t(gridW) * gridH > 65536)
        return Status::fail(Err::BadLayout, "index grid " + std::to_string(gridW) + " x " + std::to_string(gridH));
    out.reserve(size_t(gridW - 1) * (gridH - 1) * 6);
    const uint32_t W = gridW;
    for (uint32_t x0 = 1; x0 < gridW; x0 += 6) {          // bands of 6 quad columns
        for (uint32_t y = 1; y < gridH; ++y) {            // rows in the band
            for (uint32_t x = x0; x < x0 + 6 && x < gridW; ++x) {
                const uint16_t a = static_cast<uint16_t>(y * W + x);            // (x, y)
                const uint16_t b = static_cast<uint16_t>((y - 1) * W + x);      // (x, y-1)
                const uint16_t d = static_cast<uint16_t>(y * W + x - 1);        // (x-1, y)
                const uint16_t c = static_cast<uint16_t>((y - 1) * W + x - 1);  // (x-1, y-1)
                const bool even = (x % 2) == 0;
                out.push_back(a);
                out.push_back(b);
                out.push_back(even ? c : d);
                out.push_back(d);
                out.push_back(even ? a : b);
                out.push_back(c);
            }
        }
    }
    return Status::Ok();
}

static Status checkLayerTables(const LayerTables& t, uint32_t mapPlotsX) {
    C5R_TRY(checkTables(t));
    if (t.layerCount > kTerrainLayersFloat4)
        return Status::fail(Err::BadLayout, "layerCount " + std::to_string(t.layerCount) + " > 96");
    if (t.setOfLayer.size() < t.layerCount || t.projectionOfLayer.size() < t.layerCount)
        return Status::fail(Err::BadLayout, "layer tables shorter than layerCount");
    for (uint32_t l = 0; l < t.layerCount; ++l)
        if (t.setOfLayer[l] >= t.sets.size())
            return Status::fail(Err::BadLayout, "layer " + std::to_string(l) + ": texture set " +
                                                    std::to_string(t.setOfLayer[l]) + " not in TerrainDef");
    if (mapPlotsX == 0) return Status::fail(Err::BadLayout, "map width 0");
    return Status::Ok();
}

static float layerScale(const LayerTables& t, uint32_t l, float X, float invX) {
    const float s = t.sets[t.setOfLayer[l]].textureScale;
    if (t.projectionOfLayer[l] == 0) return static_cast<float>(static_cast<int32_t>((s * kTop) * X)) * invX * kTopMul;
    return static_cast<float>(static_cast<int32_t>((s * kSide) * X)) * invX * kSideMul;
}

static void spanOf(uint32_t mapPlotsX, float& X, float& invX) {
    const float e = static_cast<float>(static_cast<int32_t>(mapPlotsX * 64 - 1));
    X = e * 0.5f - e * -0.5f;  // 0xD90FD8 - 0xD90FCC
    invX = 1.0f / X;
}

Status fillTerrainLayers(const LayerTables& t, uint32_t mapPlotsX, std::vector<float>& out) {
    out.assign(kTerrainLayersFloat4 * 4, 0.0f);
    C5R_TRY(checkLayerTables(t, mapPlotsX));
    float X, invX;
    spanOf(mapPlotsX, X, invX);
    for (uint32_t l = 0; l < t.layerCount; ++l) {
        out[4 * l + 0] = layerScale(t, l, X, invX);
        out[4 * l + 1] = static_cast<float>(static_cast<int32_t>(t.projectionOfLayer[l]));
    }
    return Status::Ok();
}

Status fillOwnBlock(const LayerTables& t, uint32_t mapPlotsX, const LayerList& list, uint32_t largestCacheSize,
                    int32_t tileE4, int32_t tileE8, std::vector<uint8_t>& out) {
    out.assign(kOwnBlockBytes, 0);
    C5R_TRY(checkLayerTables(t, mapPlotsX));
    C5R_TRY(checkList(t, list));
    if (largestCacheSize == 0) return Status::fail(Err::BadLayout, "largest cache size 0");
    float* f = reinterpret_cast<float*>(out.data());
    const float h = 1.0f / (static_cast<float>(largestCacheSize) * 2.0f);
    f[0] = -h;
    f[1] = h;
    float X, invX;
    spanOf(mapPlotsX, X, invX);
    for (uint32_t k = 0; k < list.count; ++k) {
        f[4 * (1 + k) + 0] = layerScale(t, list.ids[k], X, invX);
        f[4 * (1 + k) + 1] = static_cast<float>(static_cast<int32_t>(t.projectionOfLayer[list.ids[k]]));
    }
    f[4 * 73 + 0] = static_cast<float>(tileE4);
    f[4 * 73 + 1] = static_cast<float>(tileE8);
    return Status::Ok();
}

}  // namespace bake
}  // namespace c5r
