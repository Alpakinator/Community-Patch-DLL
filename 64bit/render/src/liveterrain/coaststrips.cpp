// render/src/liveterrain/coaststrips.cpp - the EXE's coast strips (CreateTerrain step 6) for live terrain (ticket 4911).
// Written from research/05-graphics-composition/live-terrain.md 8.15 (D83). The f32 operation order follows that
// description, so that a sample comes out as the EXE's does; the comments name the rule of 8.15 each part implements.
#include "c5r/liveterrain/coaststrips.h"

#include <cmath>
#include <cstdlib>
#include <string>
#include "c5r/bake/bake_types.h"

namespace c5r { namespace liveterrain {

namespace {

const float kInv65535 = 1.5259022e-5f;    // the f32 0x37800080
const float kScale = 65535.0f;            // 0x477FFF00
const double kMinDet = 1e-5;              // the double 0x3EE4F8B588E368F1

// The edge weights per triangle of a quad (8.15 table): band 0 {0,0,0},{1,0,0}; bands 1-2 all 1; band 3 {0,0,1},{0,0,0}.
const float kEdge[8][3] = {
    {0, 0, 0}, {1, 0, 0}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {0, 0, 1}, {0, 0, 0},
};

// A profile with its band terrain codes resolved to material bytes (0x4C0560).
struct Profile {
    float weight[5], height[5];
    uint8_t band[4];
};

Profile profileOf(const StripStyles& st, int kind, const StripMaterials& mats) {
    Profile p;
    const bool have = kind >= 0 && kind < kStripStyleKinds && st.present[kind];
    for (int i = 0; i < 5; ++i) {
        p.weight[i] = have ? st.kind[kind].weight[i] : 0.0f;
        p.height[i] = have ? st.kind[kind].height[i] : 0.0f;
    }
    for (int k = 0; k < 4; ++k) p.band[k] = mats.resolveBand(have ? st.kind[kind].bandTerrain[k] : 0);
    return p;
}

// A column's point on a line (0x7222E0 at fraction 0, or 1 over the previous point for the last column).
StripPoint columnPoint(const std::vector<StripPoint>& line, uint32_t L, uint32_t j) {
    const bool last = int(L) - 2 <= int(j);
    const StripPoint& p0 = line[last ? j - 1 : j];
    const StripPoint& p1 = line[last ? j : j + 1];
    const StripPoint& p2 = line[last ? j + 1 : j + 2];
    const float f = last ? 1.0f : 0.0f;
    const float f2 = f * f;
    const float b0 = 0.5f * f2 + -1.0f * f + 0.5f;
    const float b1 = -1.0f * f2 + 1.0f * f + 0.5f;
    const float b2 = 0.5f * f2 + 0.0f * f + 0.0f;
    StripPoint o;
    o.x = p1.x * b1 + p0.x * b0 + p2.x * b2;
    o.y = p1.y * b1 + p0.y * b0 + p2.y * b2;
    o.t = p1.t * b1 + p0.t * b0 + p2.t * b2;
    o.styleA = p0.styleA;
    o.styleB = p0.styleB;
    return o;
}

inline uint32_t lcgNext(uint32_t& s) {
    s = s * 214013u + 2531011u;
    return (s >> 16) & 0x7FFFu;
}

struct Raster {
    StripField& f;
    uint8_t swapFrom[4], swapTo[4];
    StripStats st;

    explicit Raster(StripField& field) : f(field) {}

    // The sample's wrapped column, or -1 when it lies outside the window (and outside the field on a map that does not
    // wrap: the port clips where the EXE would write past the row, 8.15).
    long column(int x) const {
        long c = x;
        if (f.wrapX) {
            const long W = long(f.samplesX);
            c = (W * 100 + c) % W;
            if (c < 0) return -1;
        } else if (c < 0 || c >= long(f.samplesX)) {
            return -1;
        }
        if (c < long(f.x0) || c >= long(f.x0) + long(f.w)) return -1;
        return c - long(f.x0);
    }

    void triangle(const StripVertex& v0, const StripVertex& v1, const StripVertex& v2, const float e[3]) {
        // MC #9538 item 1 (live-terrain.md 8.10.3): a vertex that is not finite, or a box wider or taller than the field,
        // is refused before any truncation (a NaN truncates to INT_MIN and the row loop would run about 2^31 times on
        // the worker). No valid strip triangle is refused: their boxes span a few samples.
        if (!std::isfinite(v0.x) || !std::isfinite(v0.y) || !std::isfinite(v1.x) || !std::isfinite(v1.y) ||
            !std::isfinite(v2.x) || !std::isfinite(v2.y)) { ++st.trianglesRefused; return; }
        {
            const double lx = std::fmin(std::fmin(double(v0.x), double(v1.x)), double(v2.x));
            const double hx = std::fmax(std::fmax(double(v0.x), double(v1.x)), double(v2.x));
            const double ly = std::fmin(std::fmin(double(v0.y), double(v1.y)), double(v2.y));
            const double hy = std::fmax(std::fmax(double(v0.y), double(v1.y)), double(v2.y));
            if (hx - lx > double(f.samplesX) || hy - ly > double(f.samplesY) || std::fabs(lx) > 1e9 || std::fabs(hx) > 1e9 ||
                std::fabs(ly) > 1e9 || std::fabs(hy) > 1e9) { ++st.trianglesRefused; return; }
        }
        // the box: floor(min + 0.5) (x87 and double, exact) to trunc(max) + 1
        float mnx = v1.x < v2.x ? v1.x : v2.x; if (v0.x < mnx) mnx = v0.x;
        float mny = v1.y < v2.y ? v1.y : v2.y; if (v0.y < mny) mny = v0.y;
        float mxx = v1.x > v2.x ? v1.x : v2.x; if (v0.x > mxx) mxx = v0.x;
        float mxy = v1.y > v2.y ? v1.y : v2.y; if (v0.y > mxy) mxy = v0.y;
        const float ox = float(std::floor(double(mnx) + 0.5));
        const float oy = float(std::floor(double(mny) + 0.5));
        const int x0 = int(ox), y0 = int(oy);
        const int x1 = int(mxx) + 1, y1 = int(mxy) + 1;
        // the vertices relative to the box origin; the three edge functions
        const float X0 = v0.x - ox, X1 = v1.x - ox, X2 = v2.x - ox;
        const float Y0 = v0.y - oy, Y1 = v1.y - oy, Y2 = v2.y - oy;
        const float c0x = Y1 - Y2, c0y = X2 - X1, c0k = Y2 * X1 - X2 * Y1;   // b0
        const float c1x = Y2 - Y0, c1y = X0 - X2, c1k = X2 * Y0 - Y2 * X0;   // b1
        const float c2x = Y0 - Y1, c2y = X1 - X0, c2k = Y1 * X0 - X1 * Y0;   // b2
        const float det = c0y * Y0 + c0x * X0 + c0k;
        if (std::fabs(double(det)) < kMinDet) return;
        const float inv = 1.0f / det;
        // rows outside the field or the window are skipped as a whole (each row's sum still advances)
        const long wy0 = long(f.y0), wy1 = long(f.y0) + long(f.h);
        if (long(y1) <= wy0 || long(y0) >= wy1) return;
        ++st.trianglesDrawn;
        float rowSum = 0.0f;
        for (int y = y0; y < y1; ++y, rowSum += inv) {
            if (y < 0 || uint32_t(y) >= f.samplesY || long(y) < wy0 || long(y) >= wy1) continue;
            if (x0 >= x1) continue;
            const float base0 = c0k * inv + c0y * rowSum;
            const float k1 = c1k * inv, r1 = c1y * rowSum;
            const float k2 = c2k * inv, r2 = c2y * rowSum;
            float colSum = 0.0f;
            const size_t rowOff = size_t(y - long(f.y0)) * f.w;
            for (int x = x0; x < x1; ++x, colSum += inv) {
                const float b1 = c1x * colSum + k1 + r1;
                const float b2 = c2x * colSum + k2 + r2;
                const float b0 = c0x * colSum + base0;
                if (b0 < 0.0f || b1 < 0.0f || b2 < 0.0f) continue;   // the EXE skips only when 0 > b
                const long c = column(x);
                if (c < 0) continue;
                draw(rowOff + size_t(c), v0, v1, v2, e, b0, b1, b2);
            }
        }
    }

    void draw(size_t i, const StripVertex& v0, const StripVertex& v1, const StripVertex& v2, const float e[3],
              float b0, float b1, float b2) {
        const uint16_t old = f.heights[i];
        const uint8_t oldMat = f.materials[i];
        if (v0.material != kNoMaterial) f.materials[i] = v0.material;
        float w = v0.w * b0 * b0 + v1.w * b1 * b1;
        w = w + v2.w * b2 * b2;
        w = w + e[0] * b1 * b0 * 2.0f;
        w = w + e[1] * b2 * b1 * 2.0f;
        w = w + e[2] * b2 * b0 * 2.0f;
        if (0.0f > w) w = 0.0f;
        float hv = v2.h * b2 + v1.h * b1;
        hv = hv + v0.h * b0;
        const float keep = float(old) * kInv65535 * (1.0f - w);
        const float v = (hv * w + keep) * kScale;
        const uint16_t nh = uint16_t(int32_t(v) & 0xFFFF);
        f.heights[i] = nh;
        ++st.samplesWritten;
        if (nh <= kMountainSwapMax) {
            for (int c = 0; c < 4; ++c)
                if (oldMat == swapFrom[c]) f.materials[i] = swapTo[c];
        }
    }
};

} // namespace

// ---------------------------------------------------------------- materials (the TerrainDef rows)
int StripMaterials::find(uint32_t id) const {
    for (size_t k = 0; k < rowId.size(); ++k)
        if (rowId[k] == id) return int(k);
    return -1;
}

uint8_t StripMaterials::resolveBand(int code) const {
    if (code == -1) return kNoMaterial;
    int r = find(0x110000u | uint32_t(code));
    if (r < 0) r = find(0x100000u | uint32_t(code));
    return (r < 0 || r > 0xFE) ? kNoMaterial : uint8_t(r);
}

void StripMaterials::swapPairs(uint8_t from[4], uint8_t to[4]) const {
    for (uint32_t c = 0; c < 4; ++c) {
        const uint32_t pre = (0x11u + c) << 16;
        const int a = find(pre | 0x35u), b = find(pre | 0x10u);
        from[c] = (a < 0 || a > 0xFE) ? kNoMaterial : uint8_t(a);
        to[c] = (b < 0 || b > 0xFE) ? kNoMaterial : uint8_t(b);
        if (to[c] == kNoMaterial) from[c] = kNoMaterial;
    }
}

Status StripMaterials::fromLayerTables(const bake::LayerTables& t, StripMaterials& out) {
    StripMaterials m;
    for (const bake::TextureSet& s : t.sets) {
        char* end = nullptr;
        const long cont = std::strtol(s.continent.c_str(), &end, 10);
        if (end == s.continent.c_str() || *end != 0)
            return Status::fail(Err::BadLayout, "TerrainDef continent '" + s.continent + "'");
        if (cont < 0 || s.id < 0) { m.rowId.push_back(0xFFFFFFFFu); continue; }
        if (cont > 0xEF || s.id > 0xFFFF)
            return Status::fail(Err::BadLayout, "TerrainDef row out of range: continent " + s.continent);
        m.rowId.push_back((uint32_t(0x10 + cont) << 16) | uint32_t(s.id));
    }
    if (m.rowId.empty()) return Status::fail(Err::BadLayout, "TerrainDef has no rows");
    out = std::move(m);
    return Status::Ok();
}

// ---------------------------------------------------------------- the mesh (0x4B2100 > 0x55F9F0)
void stripEdgeWeights(uint32_t triangle, float out[3]) {
    const float* e = kEdge[triangle % 8];
    out[0] = e[0]; out[1] = e[1]; out[2] = e[2];
}

Status buildStripMesh(const StripLines& lines, const StripStyles& styles, const StripMaterials& mats, StripMesh& out) {
    const uint32_t L = uint32_t(lines.line[0].size());
    for (int i = 1; i < 5; ++i)
        if (lines.line[i].size() != L)
            return Status::fail(Err::BadLayout, "offset line " + std::to_string(i) + " has " +
                                std::to_string(lines.line[i].size()) + " points, line 0 " + std::to_string(L));
    // (a style kind with no profile, 13 and above included, takes the EXE's all-zero profile: profileOf)
    StripMesh m;
    if (L < 3) { out = std::move(m); return Status::Ok(); }
    const uint32_t cols = L - 1, quads = L - 2;
    m.vertices.resize(size_t(cols) * 8);
    // the vertices, column by column, band by band; the profiles are re-read when the inner point's style A changes
    int curA = -1;
    bool odd = true;           // flips on every re-read; the first read leaves it even (f = t)
    Profile A = profileOf(styles, 0, mats), B = A;
    for (uint32_t j = 0; j < cols; ++j) {
        for (int k = 0; k < 4; ++k) {
            const StripPoint in = columnPoint(lines.line[k], L, j);
            if (int(in.styleA) != curA) {
                A = profileOf(styles, in.styleA, mats);
                B = profileOf(styles, in.styleB, mats);
                odd = !odd;
                curA = in.styleA;
            }
            const float f = odd ? 1.0f - in.t : in.t;
            const float g = 1.0f - f;
            const uint8_t mat = (f <= 0.5f) ? B.band[k] : A.band[k];
            StripVertex& vi = m.vertices[size_t(j) * 8 + size_t(k) * 2];
            vi.x = in.x; vi.y = in.y; vi.material = mat;
            vi.h = B.height[k] * g + A.height[k] * f;
            vi.w = B.weight[k] * g + A.weight[k] * f;
            const StripPoint outP = columnPoint(lines.line[k + 1], L, j);
            StripVertex& vo = m.vertices[size_t(j) * 8 + size_t(k) * 2 + 1];
            vo.x = outP.x; vo.y = outP.y; vo.material = mat;
            vo.h = B.height[k + 1] * g + A.height[k + 1] * f;
            vo.w = B.weight[k + 1] * g + A.weight[k + 1] * f;
        }
    }
    // the triangles: per quad and band, (inner j, outer j, inner j+1) and (outer j, outer j+1, inner j+1)
    m.tris.reserve(size_t(quads) * 24);
    for (uint32_t q = 0; q < quads; ++q)
        for (uint32_t k = 0; k < 4; ++k) {
            const uint32_t s = q * 8 + k * 2;
            m.tris.push_back(s); m.tris.push_back(s + 1); m.tris.push_back(s + 8);
            m.tris.push_back(s + 1); m.tris.push_back(s + 9); m.tris.push_back(s + 8);
        }
    // the segments: c from 64 in steps of 8 while (more than one segment and an odd count) or a remainder of 1..7
    const uint32_t T = quads * 8;
    uint32_t c = 64;
    for (;;) {
        const uint32_t n = (T - 1) / c + 1, r = T % c;
        if ((n > 1 && (n & 1u)) || (r >= 1 && r <= 7)) { c += 8; continue; }
        break;
    }
    m.segmentTris = c;
    for (uint32_t e = c; ; e += c) {
        m.segmentEnd.push_back(e < T ? e : T);
        if (e >= T) break;
    }
    // the box
    m.minX = m.maxX = m.vertices[0].x;
    m.minY = m.maxY = m.vertices[0].y;
    for (const StripVertex& v : m.vertices) {
        if (v.x < m.minX) m.minX = v.x;
        if (v.x > m.maxX) m.maxX = v.x;
        if (v.y < m.minY) m.minY = v.y;
        if (v.y > m.maxY) m.maxY = v.y;
    }
    out = std::move(m);
    return Status::Ok();
}

// ---------------------------------------------------------------- the raster (0x639D40 > 0x6990D0)
bool stripTouches(const StripMesh& m, const StripField& f) {
    if (m.tris.empty()) return false;
    const long ya = long(std::floor(double(m.minY) + 0.5)), yb = long(m.maxY) + 1;
    if (yb <= long(f.y0) || ya >= long(f.y0) + long(f.h)) return false;
    const long xa = long(std::floor(double(m.minX) + 0.5)), xb = long(m.maxX) + 1;
    if (!f.wrapX) return !(xb <= long(f.x0) || xa >= long(f.x0) + long(f.w));
    const long W = long(f.samplesX);
    if (xb - xa >= W) return true;
    for (long k = -2; k <= 2; ++k) {     // the box against the window shifted by whole field widths
        const long a = long(f.x0) + k * W, b = a + long(f.w);
        if (!(xb <= a || xa >= b)) return true;
    }
    return false;
}

Status applyStrips(const std::vector<StripMesh>& meshes, const StripMaterials& mats, StripField& field,
                   StripStats* stats) {
    if (!field.heights || !field.materials)
        return Status::fail(Err::BadLayout, "strip field: no arrays");
    if (field.samplesX == 0 || field.samplesY == 0 || field.w == 0 || field.h == 0 ||
        uint64_t(field.x0) + field.w > field.samplesX || uint64_t(field.y0) + field.h > field.samplesY)
        return Status::fail(Err::BadLayout, "strip window " + std::to_string(field.x0) + "," + std::to_string(field.y0) +
                            " " + std::to_string(field.w) + "x" + std::to_string(field.h) + " outside the field " +
                            std::to_string(field.samplesX) + "x" + std::to_string(field.samplesY));
    for (const StripMesh& m : meshes)
        for (uint32_t v : m.tris)
            if (v >= m.vertices.size()) return Status::fail(Err::BadLayout, "strip triangle index out of range");
    Raster r(field);
    mats.swapPairs(r.swapFrom, r.swapTo);
    std::vector<char> touch(meshes.size());
    for (size_t i = 0; i < meshes.size(); ++i) {
        touch[i] = stripTouches(meshes[i], field) ? 1 : 0;
        r.st.meshesTouching += touch[i] ? 1u : 0u;
    }
    for (uint32_t wave = 0; wave < 2; ++wave) {
        for (size_t i = 0; i < meshes.size(); ++i) {
            if (!touch[i]) continue;
            const StripMesh& m = meshes[i];
            for (uint32_t s = wave; s < m.segments(); s += 2) {
                const uint32_t t0 = s * m.segmentTris, t1 = m.segmentEnd[s];
                for (uint32_t t = t0; t < t1; ++t) {
                    float e[3];
                    stripEdgeWeights(t, e);
                    r.triangle(m.vertices[m.tris[3 * t]], m.vertices[m.tris[3 * t + 1]],
                               m.vertices[m.tris[3 * t + 2]], e);
                }
            }
        }
    }
    if (stats) *stats = r.st;
    return Status::Ok();
}

bool specialPick(const std::vector<StripMesh>& meshes, uint32_t& randState, SpecialPick& out) {
    if (meshes.empty()) return false;
    out.coast = lcgNext(randState) % uint32_t(meshes.size());
    const uint32_t half = meshes[out.coast].segments() / 2;
    if (half == 0) return false;
    out.segment = (lcgNext(randState) % half) * 2;
    return true;
}

}} // namespace c5r::liveterrain
