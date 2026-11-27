// render/src/bakelayers/layers.cpp - issue 3401: TerrainDef.csv -> LayerTables, HeightGrid -> LayerList per tile.
// Own code, written from the description in research/05-graphics-composition/terrain-bake-records.md section 2.2
// (D83). Float arithmetic is single precision, one rounding per operation in the order the research gives; nothing
// here may be contracted into fused multiply-adds (the x64 target has no FMA by default; the pragma makes it explicit).
#include "c5r/bake/layers.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "c5r/gamefs/gamefiles.h"

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace c5r {
namespace bake {

const char* const kAppendedSetTexture = "Checkerboard.dds";
const char* const kSharedTerrainDef = "Assets/DLC/Shared/Terrain/TerrainDef.csv";

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

std::vector<std::string> splitComma(const std::string& line) {
    std::vector<std::string> f;
    size_t at = 0;
    for (;;) {
        size_t c = line.find(',', at);
        f.push_back(trim(line.substr(at, c == std::string::npos ? std::string::npos : c - at)));
        if (c == std::string::npos) break;
        at = c + 1;
    }
    return f;
}

bool parseLong(const std::string& s, int base, long& v) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    v = std::strtol(s.c_str(), &end, base);
    return errno == 0 && end && *end == '\0';
}

bool parseFloat(const std::string& s, float& v) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    v = std::strtof(s.c_str(), &end);
    return errno == 0 && end && *end == '\0' && std::isfinite(v);
}

std::string where(size_t line) { return "TerrainDef.csv line " + std::to_string(line); }

Status checkHeights(const heightfield::HeightGrid& g) {
    if (g.mapW == 0 || g.mapH == 0 || g.samplesX != g.mapW * kTileSamples || g.samplesY != g.mapH * kTileSamples)
        return Status::fail(Err::BadLayout, "grid " + std::to_string(g.mapW) + "x" + std::to_string(g.mapH) +
                                                " plots with " + std::to_string(g.samplesX) + "x" +
                                                std::to_string(g.samplesY) + " samples (want plots * 64)");
    if (g.h.size() != static_cast<size_t>(g.samplesX) * g.samplesY)
        return Status::fail(Err::BadLayout, "grid size mismatch: " + std::to_string(g.h.size()) + " heights for " +
                                                std::to_string(g.samplesX) + "x" + std::to_string(g.samplesY));
    if (!std::isfinite(g.sx) || !std::isfinite(g.sy) || !std::isfinite(g.sz))
        return Status::fail(Err::BadLayout, "grid scales not finite");
    return Status::Ok();
}

// One weighted tap of the normal grid (2.2a step 3).
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

struct Kernels {
    float x[5][5];
    float y[5][5];
    Kernels() {
        // 2.2a: outer products of A (derivative) and B (smoothing), each entry one float multiply.
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

}  // namespace

Status parseTerrainDef(const std::string& text, LayerTables& out) {
    LayerTables t;
    size_t lineNo = 0, at = 0;
    bool header = true;
    while (at <= text.size()) {
        size_t nl = text.find('\n', at);
        std::string line = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        at = nl == std::string::npos ? text.size() + 1 : nl + 1;
        ++lineNo;
        if (trim(line).empty()) continue;
        if (header) {
            header = false;
            continue;
        }
        std::vector<std::string> f = splitComma(line);
        if (f.size() < kTerrainDefColumns)
            return Status::fail(Err::BadLayout, where(lineNo) + ": " + std::to_string(f.size()) + " columns, want " +
                                                    std::to_string(kTerrainDefColumns));
        TextureSet s;
        long cont = 0, proj = 0, id = 0;
        if (!parseLong(f[0], 10, cont)) return Status::fail(Err::BadLayout, where(lineNo) + ": continent '" + f[0] + "'");
        if (!parseFloat(f[4], s.baseDominance))
            return Status::fail(Err::BadLayout, where(lineNo) + ": base dominance '" + f[4] + "'");
        if (!parseFloat(f[5], s.textureScale))
            return Status::fail(Err::BadLayout, where(lineNo) + ": texture scale '" + f[5] + "'");
        if (!parseLong(f[6], 10, proj)) return Status::fail(Err::BadLayout, where(lineNo) + ": projection '" + f[6] + "'");
        if (!parseLong(f[7], 16, id)) return Status::fail(Err::BadLayout, where(lineNo) + ": id '" + f[7] + "'");
        if (proj != 0 && proj != 1)
            return Status::fail(Err::Unsupported, where(lineNo) + ": projection " + std::to_string(proj));
        if (f[1].empty() || f[2].empty()) return Status::fail(Err::BadLayout, where(lineNo) + ": empty texture name");
        s.continent = f[0];
        s.diffuse = f[1];
        s.heightmap = f[2];
        s.specmap = (f[3] == "NULL") ? std::string() : f[3];
        s.projection = static_cast<int>(proj);
        s.id = static_cast<int>(id);
        t.sets.push_back(s);
        if (t.sets.size() + 1 > kMaxTextureSets)
            return Status::fail(Err::BadLayout, where(lineNo) + ": too many rows (more than " +
                                                    std::to_string(kMaxTextureSets - 1) + " plus the appended set)");
    }
    if (t.sets.empty()) return Status::fail(Err::BadLayout, "TerrainDef.csv has no data row");
    TextureSet extra;  // 2.2b: appended after the last row
    extra.continent = "-1";
    extra.diffuse = kAppendedSetTexture;
    extra.heightmap = kAppendedSetTexture;
    extra.textureScale = kAppendedSetScale;
    extra.projection = 0;
    extra.id = -1;
    t.sets.push_back(extra);
    for (size_t k = 0; k < t.sets.size(); ++k) {
        uint32_t n = t.sets[k].projection == 1 ? 3u : 1u;
        if (t.layerCount + n > kMaxLayerIds)
            return Status::fail(Err::BadLayout, "too many layer ids: set " + std::to_string(k) + " passes " +
                                                    std::to_string(kMaxLayerIds));
        t.firstLayerOfMaterial[k] = static_cast<uint8_t>(t.layerCount);
        t.slopeProjected[k] = n == 3 ? 1 : 0;
        for (uint32_t p = 0; p < n; ++p) {
            t.setOfLayer.push_back(static_cast<uint32_t>(k));
            t.projectionOfLayer.push_back(p);
        }
        t.layerCount += n;
    }
    out = t;
    return Status::Ok();
}

Status loadTerrainDef(const gamefs::GameFiles& fs, LayerTables& out, std::string* origin) {
    // Issue 3404: gamefs mounts the package folders like the EXE's "Common" kind, so the bare name resolves to the
    // Shared DLC's loose file the EXE uses; the loose-first read by path of 3401 is gone.
    const std::string name = "terraindef.csv";
    if (origin) origin->clear();
    if (!fs.exists(name)) return Status::fail(Err::NotFound, "terraindef.csv not in the game's files");
    std::vector<uint8_t> data;
    C5R_TRY(fs.read(name, data, origin));
    return parseTerrainDef(std::string(data.begin(), data.end()), out);
}

Status loadTerrainDef(const gamefs::GameFiles& fs, const std::wstring& /*installRoot*/, LayerTables& out,
                      std::string* origin) {
    return loadTerrainDef(fs, out, origin);
}

Status checkGrid(const heightfield::HeightGrid& g) {
    C5R_TRY(checkHeights(g));
    if (g.mat.empty()) return Status::fail(Err::NotFound, "the height field has no material bytes (byte layer not tapped)");
    if (g.mat.size() != g.h.size())
        return Status::fail(Err::BadLayout, "grid size mismatch: " + std::to_string(g.mat.size()) +
                                                " material bytes for " + std::to_string(g.h.size()) + " heights");
    return Status::Ok();
}

Status tileWindow(const heightfield::HeightGrid& g, TileId t, TileWindow& out) {
    if (t.x >= g.mapW || t.y >= g.mapH)
        return Status::fail(Err::BadLayout, "tile (" + std::to_string(t.x) + "," + std::to_string(t.y) +
                                                ") outside the " + std::to_string(g.mapW) + "x" +
                                                std::to_string(g.mapH) + " grid");
    out.ox = t.x * kTileSamples;
    out.oy = t.y * kTileSamples;
    out.ex = (t.x + 1 == g.mapW) ? kTileSamples : kTileSamples + 1;
    out.ey = (t.y + 1 == g.mapH) ? kTileSamples : kTileSamples + 1;
    return Status::Ok();
}

Status normalGrid(const heightfield::HeightGrid& g, uint32_t ox, uint32_t oy, std::vector<float>& out) {
    C5R_TRY(checkHeights(g));
    if (ox >= g.samplesX || oy >= g.samplesY)
        return Status::fail(Err::BadLayout, "normal grid origin outside the field");
    const int W = static_cast<int>(g.samplesX), H = static_cast<int>(g.samplesY);
    const int N = 76;  // window 76 x 76: x = ox-2 .. ox+73, y = oy-2 .. oy+73
    std::vector<int> hi(static_cast<size_t>(N) * N);
    std::vector<float> hf(hi.size());
    const float k255 = 0.00392156885936856270f;  // 0x3B808081
    for (int c = 0; c < N; ++c) {
        int x = static_cast<int>(ox) - 2 + c;
        if (g.wrapX) x = (W * 100 + x) % W;
        if (x > W - 1) x = W - 1;
        if (x < 0) x = 0;
        for (int r = 0; r < N; ++r) {
            int y = static_cast<int>(oy) - 2 + r;
            if (y > H - 1) y = H - 1;
            if (y < 0) y = 0;
            int v = g.h[g.index(static_cast<uint32_t>(x), static_cast<uint32_t>(y))];
            hi[static_cast<size_t>(c) * N + r] = v;
            hf[static_cast<size_t>(c) * N + r] = static_cast<float>(v) * k255;
        }
    }
    auto I = [&](int c, int r) { return hi[static_cast<size_t>(c) * N + r]; };
    auto F = [&](int c, int r) { return hf[static_cast<size_t>(c) * N + r]; };
    const Kernels& K = kernels();
    out.assign(static_cast<size_t>(kNormalGrid) * kNormalGrid * 3, 0.0f);
    for (int c = 2; c < 2 + static_cast<int>(kNormalGrid); ++c) {
        for (int r = 2; r < 2 + static_cast<int>(kNormalGrid); ++r) {
            const int gx = (I(c + 1, r) - I(c - 1, r)) >> 1;
            const int gy = (I(c, r + 1) - I(c, r - 1)) >> 1;
            const float centre = F(c, r);
            float px = static_cast<float>(gx) * k255;
            px = px * -2.0f;
            px = px + centre;
            float py = static_cast<float>(gy) * k255;
            py = py * -2.0f;
            py = py + centre;
            float nxs = 0.0f, dxs = 0.0f, nys = 0.0f, dys = 0.0f;
            // x sum (2.2a table): columns -2, -1, +1, +2
            float p = px;
            for (int dr = -1; dr <= 1; ++dr) tap(p, F(c - 2, r + dr), K.x[0][dr + 2], nxs, dxs);
            p = p + 1.0f;
            for (int dr = -2; dr <= 2; ++dr) tap(p, F(c - 1, r + dr), K.x[1][dr + 2], nxs, dxs);
            p = p + 1.0f;
            p = p + 1.0f;
            for (int dr = -2; dr <= 2; ++dr) tap(p, F(c + 1, r + dr), K.x[3][dr + 2], nxs, dxs);
            p = p + 1.0f;
            for (int dr = -1; dr <= 1; ++dr) tap(p, F(c + 2, r + dr), K.x[4][dr + 2], nxs, dxs);
            // y sum: rows -2, -1, +1, +2
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
            float t0 = g.sy * a;
            v0 = v0 - t0;
            float v1 = a * 0.0f;
            float t1 = b * g.sx;
            v1 = v1 - t1;
            const float v2 = g.sy * g.sx;
            // D3DXVec3Normalize: [I] v / sqrt(x^2 + y^2 + z^2) (research 2.2a)
            float len2 = v0 * v0;
            len2 = len2 + v1 * v1;
            len2 = len2 + v2 * v2;
            const float len = std::sqrt(len2);
            float* o = &out[(static_cast<size_t>(r - 2) * kNormalGrid + static_cast<size_t>(c - 2)) * 3];
            if (len > 0.0f) {
                o[0] = v0 / len;
                o[1] = v1 / len;
                o[2] = v2 / len;
            }
        }
    }
    return Status::Ok();
}

Status buildLayerList(const LayerTables& tables, const heightfield::HeightGrid& g, TileId t, LayerList& out,
                      LayerListDetail* detail) {
    C5R_TRY(checkGrid(g));
    if (tables.layerCount == 0 || tables.layerCount > kMaxLayerIds)
        return Status::fail(Err::BadLayout, "layer count " + std::to_string(tables.layerCount));
    TileWindow w;
    C5R_TRY(tileWindow(g, t, w));
    LayerListDetail local;
    LayerListDetail& d = detail ? *detail : local;
    d = LayerListDetail();
    const uint32_t W = g.samplesX, H = g.samplesY;
    const size_t nsets = tables.sets.size();
    // The normals matter only for slope-projected materials (step 4): compute the grid only when the window has one.
    bool anySlope = false;
    for (int64_t y = static_cast<int64_t>(w.oy) - 2; y < static_cast<int64_t>(w.oy) + w.ey + 1 && !anySlope; ++y) {
        const uint32_t yy = static_cast<uint32_t>((static_cast<int64_t>(H) + y) % H);
        for (int64_t x = static_cast<int64_t>(w.ox) - 2; x < static_cast<int64_t>(w.ox) + w.ex + 1; ++x) {
            const uint8_t b = g.mat[g.index(static_cast<uint32_t>((static_cast<int64_t>(W) + x) % W), yy)];
            if (b < nsets && tables.slopeProjected[b] == 1) {
                anySlope = true;
                break;
            }
        }
    }
    std::vector<float> normals;
    if (anySlope) C5R_TRY(normalGrid(g, w.ox, w.oy, normals));
    for (int64_t y = static_cast<int64_t>(w.oy) - 2; y < static_cast<int64_t>(w.oy) + w.ey + 1; ++y) {
        const uint32_t yy = static_cast<uint32_t>((static_cast<int64_t>(H) + y) % H);
        for (int64_t x = static_cast<int64_t>(w.ox) - 2; x < static_cast<int64_t>(w.ox) + w.ex + 1; ++x) {
            const uint32_t xx = static_cast<uint32_t>((static_cast<int64_t>(W) + x) % W);
            const uint8_t b = g.mat[g.index(xx, yy)];
            ++d.materials[b];
            ++d.samples;
            if (b >= nsets)
                return Status::fail(Err::NotFound, "material byte " + std::to_string(b) + " at sample (" +
                                                       std::to_string(xx) + "," + std::to_string(yy) +
                                                       ") has no texture set (" + std::to_string(nsets) + " sets)");
            uint32_t j = tables.firstLayerOfMaterial[b];
            const uint32_t i = static_cast<uint32_t>(x - static_cast<int64_t>(w.ox) + 2);
            const uint32_t k = static_cast<uint32_t>(y - static_cast<int64_t>(w.oy) + 2);
            float nx = 0, ny = 0, nz = 0;
            if (anySlope && i < kNormalGrid && k < kNormalGrid) {
                const float* n = &normals[(static_cast<size_t>(k) * kNormalGrid + i) * 3];
                nx = n[0];
                ny = n[1];
                nz = n[2];
            }
            if (tables.slopeProjected[b] == 1) {
                ++d.slopeSamples;
                if (std::fabs(nz - kSlopeThreshold) < std::fabs(d.closestNz - kSlopeThreshold)) d.closestNz = nz;
                if (nz <= kSlopeThreshold) {
                    if (std::fabs(ny) <= std::fabs(nx)) {
                        j += 2;
                        ++d.moved2;
                    } else {
                        j += 1;
                        ++d.moved1;
                    }
                }
                if (j + 2 >= 128) return Status::fail(Err::BadLayout, "layer id " + std::to_string(j) + " past the counters");
                ++d.counts[j];
                ++d.counts[j + 1];
                ++d.counts[j + 2];
            }
            ++d.counts[j];
        }
    }
    // step 6: ascending ids with a count, sort by count only above 8
    uint32_t ids[kMaxLayerIds], cnt[kMaxLayerIds];
    uint32_t n = 0;
    for (uint32_t id = 0; id < tables.layerCount; ++id)
        if (d.counts[id]) {
            ids[n] = id;
            cnt[n] = d.counts[id];
            ++n;
        }
    if (n > kMaxLayers) {
        d.sorted = true;
        bool swapped;
        do {
            swapped = false;
            for (uint32_t i = 0; i + 1 < n; ++i)
                if (cnt[i] < cnt[i + 1]) {
                    uint32_t c0 = cnt[i];
                    cnt[i] = cnt[i + 1];
                    cnt[i + 1] = c0;
                    uint32_t i0 = ids[i];
                    ids[i] = ids[i + 1];
                    ids[i + 1] = i0;
                    swapped = true;
                }
        } while (swapped);
        n = kMaxLayers;
    }
    out = LayerList();
    out.count = n;
    for (uint32_t i = 0; i < n; ++i) out.ids[i] = ids[i];
    return Status::Ok();
}

Status buildAllLayerLists(const LayerTables& tables, const heightfield::HeightGrid& g, std::vector<LayerList>& out) {
    C5R_TRY(checkGrid(g));
    std::vector<LayerList> all(static_cast<size_t>(g.mapW) * g.mapH);
    for (uint32_t x = 0; x < g.mapW; ++x)
        for (uint32_t y = 0; y < g.mapH; ++y) {
            TileId t;
            t.x = x;
            t.y = y;
            C5R_TRY(buildLayerList(tables, g, t, all[static_cast<size_t>(x) * g.mapH + y]));
        }
    out.swap(all);
    return Status::Ok();
}

bool needsSecondCache(const LayerTables& tables, const LayerList& list, bool globalSwitch, bool tileFlag300) {
    if (!globalSwitch) return false;
    if (tileFlag300) return true;
    for (uint32_t i = 0; i < list.count && i < kMaxLayers; ++i) {
        const uint32_t id = list.ids[i];
        if (id < tables.setOfLayer.size()) {
            const uint32_t k = tables.setOfLayer[id];
            if (k < tables.sets.size() && !tables.sets[k].specmap.empty()) return true;
        }
    }
    return false;
}

}  // namespace bake
}  // namespace c5r
