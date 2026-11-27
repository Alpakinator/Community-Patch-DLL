// render/src/liveterrain/producer.cpp - the region producer (ticket 4903; live-terrain.md 8). Our own code, written
// from the descriptions in live-terrain.md 7.2 (matcher), 7.4 (block blend) and 7.5 (the map frame), and from the
// Architect's way 3 (route note 3.3). Nothing here is taken from the EXE (D83).
#include "c5r/liveterrain/producer.h"
#include "c5r/liveterrain/coastlines.h"
#include "c5r/liveterrain/coaststrips.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstring>
#include <set>

namespace c5r { namespace liveterrain {

namespace {

std::string num(int64_t v) { return std::to_string(v); }

// ------------------------------------------------ the MSVC CRT generator and random_shuffle as 7.2 describes them
struct Crt {
    uint32_t s = 0;
    uint64_t calls = 0;
    explicit Crt(uint32_t seed) : s(seed) {}
    uint32_t rand() { s = s * 214013u + 2531011u; ++calls; return (s >> 16) & 0x7FFFu; }
    template <class T> void shuffle(std::vector<T>& v) {
        for (size_t i = 1; i < v.size(); ++i) {
            const uint32_t k = uint32_t(i + 1);
            uint32_t r = rand() & 0x7FFFu, lim = 0x7FFFu;
            while (lim < k && lim != 0xFFFFFFFFu) { r = (r << 15) | (rand() & 0x7FFFu); lim = (lim << 15) | 0x7FFFu; }
            const size_t j = r % k;
            if (j != i) std::swap(v[i], v[j]);
        }
    }
};

// ------------------------------------------------ 4 x 4 int matrices, row vectors (7.2)
typedef std::array<int64_t, 16> M4;
M4 ident() { M4 m{}; m[0] = m[5] = m[10] = m[15] = 1; return m; }
M4 mul(const M4& a, const M4& b) {
    M4 r{};
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
        int64_t s = 0; for (int k = 0; k < 4; ++k) s += a[i * 4 + k] * b[k * 4 + j]; r[i * 4 + j] = s; }
    return r;
}
M4 transl(int64_t x, int64_t y, int64_t z) { M4 m = ident(); m[12] = x; m[13] = y; m[14] = z; return m; }
// rotation by 0: the identity on the plane q + r + s = 0, written as the EXE's rotation builder makes it (7.5 [L])
M4 rot0() { M4 m{}; m[1] = m[2] = m[4] = m[6] = m[8] = m[9] = -1; m[15] = 1; return m; }
// the four signed axis permutations the transforms use (7.2)
M4 perm(int k) {
    M4 m{}; m[15] = 1;
    if (k == 0) { m[0] = m[5] = m[10] = 1; }
    else if (k == 2) { m[2] = -1; m[5] = -1; m[8] = -1; }
    else if (k == 6) { m[2] = 1; m[5] = 1; m[8] = 1; }
    else { m[0] = m[5] = m[10] = -1; }
    return m;
}
struct TransformDef { int t[3]; int k; };
const TransformDef kTransforms[4] = {{{0, 0, 0}, 0}, {{1, -3, 2}, 2}, {{-4, 0, 4}, 6}, {{-2, -3, 5}, 9}};
M4 transformMatrix(int i) {
    const TransformDef& d = kTransforms[i];
    return mul(mul(mul(ident(), transl(d.t[0], d.t[1], d.t[2])), rot0()), perm(d.k));
}

int64_t roundAway(float x) {
    const double d = double(x);
    return d >= 0 ? int64_t(std::floor(d + 0.5)) : int64_t(std::ceil(d - 0.5));
}

// the class of a cell (0x5A8640's classes, 7.3): 0 land, 1 coast (coast, reef, Gibraltar bits), 2 ocean
uint8_t cellClass(uint32_t f) {
    if (f & 0xF3EF9FFFu) return 0;
    if (f & 0x0C102000u) return 1;
    return 2;
}

const float kOffset = 0.23533989489078522f;           // the blend's offset (7.4), as the f32 word 0x3E70FCF1
const float kInv65535 = 1.5259021893143654e-05f;       // 0x37800080
const float kInv255 = 0.003921568859368563f;           // 0x3B808081
const float kWsum0 = 9.999999974752427e-07f;           // 0x358637BD

// (4913) the coast strips' inputs from the coast curves (W2's adapter, live-terrain.md 8.16)
StripLines stripLinesOf(const CoastLine& L) {
    StripLines sl;
    for (int k = 0; k < 5; ++k) {
        sl.line[k].reserve(L.mesh[k].size());
        for (const CoastMeshPoint& m : L.mesh[k]) {
            StripPoint p; p.x = m.x; p.y = m.y; p.t = m.t; p.styleA = m.styleA; p.styleB = m.styleB; sl.line[k].push_back(p);
        }
    }
    return sl;
}
StripStyles stripStylesOf(const CoastStyles& cs) {
    StripStyles s;
    for (int k = 0; k < kStripStyleKinds && k < CoastStyles::kKinds; ++k) {
        s.present[k] = cs.present[k];
        if (!cs.present[k]) continue;
        const CoastStyle& c = cs.style[k];
        for (int i = 0; i < 5; ++i) { s.kind[k].weight[i] = c.flag[i]; s.kind[k].height[i] = c.height[i]; }
        s.kind[k].bandTerrain[0] = -1; s.kind[k].bandTerrain[1] = c.terrainHigh;
        s.kind[k].bandTerrain[2] = c.terrainLow; s.kind[k].bandTerrain[3] = -1;
    }
    return s;
}
// the region bytes of a sample window [x0, x0 + W) x [y0, y0 + H) inside the field (no seam crossing)
// the window's columns [x0, x0 + W) as up to two runs inside the field: (field x, window column, width)
struct Seg { int fx, wc, n; };
std::vector<Seg> segments(const MapFrame& fr, int x0, int W) {
    const int S = fr.samplesX();
    std::vector<Seg> v;
    if (x0 < 0) { v.push_back(Seg{x0 + S, 0, -x0}); v.push_back(Seg{0, -x0, W + x0}); }
    else if (x0 + W > S) { v.push_back(Seg{x0, 0, S - x0}); v.push_back(Seg{0, S - x0, W - (S - x0)}); }
    else v.push_back(Seg{x0, 0, W});
    return v;
}
Status windowRegion(const MapFrame& fr, const PlotTable& plots, const CoastCurves& cv, int x0, int y0, int W, int H,
                    std::vector<uint8_t>& out) {
    if (y0 < 0 || y0 + H > fr.samplesY() || W > fr.samplesX() || (!fr.wrapX && (x0 < 0 || x0 + W > fr.samplesX())))
        return Status::fail(Err::BadLayout, "producer: region window outside the field");
    out.resize(size_t(W) * H);
    for (const Seg& sg : segments(fr, x0, W)) {
        if (sg.n <= 0) continue;
        const int bx0 = sg.fx / 64, by0 = y0 / 64, bx1 = (sg.fx + sg.n + 63) / 64, by1 = (y0 + H + 63) / 64;
        std::vector<uint8_t> r;
        C5R_TRY(regionBlocks(fr, plots, cv, bx0, by0, bx1, by1, r));
        const int RW = (bx1 - bx0) * 64;
        for (int y = 0; y < H; ++y) for (int x = 0; x < sg.n; ++x)
            out[size_t(y) * W + size_t(sg.wc + x)] = r[size_t(y0 - by0 * 64 + y) * RW + size_t(sg.fx - bx0 * 64 + x)];
    }
    return Status::Ok();
}
// the coast strips over a window of window-sized arrays, with the meshes that reach it (their order kept)
Status windowStrips(const MapFrame& fr, const std::vector<StripMesh>& meshes, const StripMaterials& mats, int x0, int y0,
                    int W, int H, std::vector<uint16_t>& h, std::vector<uint8_t>& m, uint64_t& refused) {
    for (const Seg& sg : segments(fr, x0, W)) {
        if (sg.n <= 0) continue;
        // a run's samples in their own arrays (StripField is one dense rectangle that does not cross the seam)
        std::vector<uint16_t> sh(size_t(sg.n) * H); std::vector<uint8_t> sm(size_t(sg.n) * H);
        for (int y = 0; y < H; ++y) for (int x = 0; x < sg.n; ++x) {
            sh[size_t(y) * sg.n + x] = h[size_t(y) * W + size_t(sg.wc + x)];
            sm[size_t(y) * sg.n + x] = m[size_t(y) * W + size_t(sg.wc + x)];
        }
        StripField f;
        f.samplesX = uint32_t(fr.samplesX()); f.samplesY = uint32_t(fr.samplesY()); f.wrapX = fr.wrapX;
        f.x0 = uint32_t(sg.fx); f.y0 = uint32_t(y0); f.w = uint32_t(sg.n); f.h = uint32_t(H);
        f.heights = sh.data(); f.materials = sm.data();
        std::vector<StripMesh> touching;
        for (const StripMesh& s : meshes) if (stripTouches(s, f)) touching.push_back(s);
        StripStats ss;
        C5R_TRY(applyStrips(touching, mats, f, &ss));
        refused += ss.trianglesRefused;
        for (int y = 0; y < H; ++y) for (int x = 0; x < sg.n; ++x) {
            h[size_t(y) * W + size_t(sg.wc + x)] = sh[size_t(y) * sg.n + x];
            m[size_t(y) * W + size_t(sg.wc + x)] = sm[size_t(y) * sg.n + x];
        }
    }
    return Status::Ok();
}

} // namespace

// ------------------------------------------------ plots, seed, frame
bool PlotInfo::isMarsh() const {
    if (!base.featureType.empty()) return base.featureType == "FEATURE_MARSH";
    return base.feature == 2;
}

const char* waitReasonName(WaitReason r) {
    switch (r) {
    case WaitReason::None: return "none";
    case WaitReason::NoChange: return "no-change";
    case WaitReason::Water: return "water";
    case WaitReason::Lake: return "lake";
    case WaitReason::RiverEdge: return "river-edge";
    case WaitReason::Marsh: return "marsh";
    case WaitReason::Seam: return "seam";
    case WaitReason::TooLarge: return "too-large";
    }
    return "?";
}

uint64_t deriveSeed(const MapFrame& f, const PlotTable& t) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { for (int i = 0; i < 8; ++i) { h ^= (v >> (8 * i)) & 0xFF; h *= 1099511628211ull; } };
    mix(uint64_t(f.plotsW)); mix(uint64_t(f.plotsH)); mix(f.wrapX ? 1 : 0);
    for (const PlotInfo& p : t.plots) {
        mix(uint64_t(int64_t(p.base.terrain))); mix(p.base.hills ? 1 : 0); mix(p.base.mountain ? 1 : 0);
        mix(uint64_t(int64_t(p.base.feature))); mix(uint64_t(int64_t(p.continentArt)));
    }
    return h;
}

// ticket 0613 (live-terrain.md 8.13.2): the EXE's rule is heightfield::patternCellFlags (the compare chain of 0x60ABB0, P7);
// a wonder it gives Fuji's 0x40000 without being Fuji is one it does not name. Kept in terms of that function, so the
// EXE's name table stays in one place.
bool unknownToExe(const heightfield::PlotInfo& p) {
    if (p.feature == -1 || !p.naturalWonder || p.featureType.empty()) return false;
    if (p.terrain == 5 || p.terrain == 6) return false;   // coast or ocean: the neutral base would be water (not handled)
    if (p.featureType == "FEATURE_FUJI" || p.featureType == "FEATURE_FOUNTAIN_YOUTH" || p.featureType == "FEATURE_EL_DORADO") return false;
    return heightfield::patternCellFlags(p) == 0x40000u;
}

uint32_t cellFlags(const heightfield::PlotInfo& p, bool neutralUnknownWonders) {
    if (!neutralUnknownWonders || !unknownToExe(p)) return heightfield::patternCellFlags(p);
    heightfield::PlotInfo n = p;
    n.naturalWonder = false;   // the plot's own terrain value, as Fountain of Youth and El Dorado keep it
    return heightfield::patternCellFlags(n);
}

Status MapFrame::fromConstantsBlock(const uint8_t* p, size_t n, MapFrame& out) {
    const uint32_t base = 0xD90FBC;
    if (!p || n < 0xD9132C - base) return Status::fail(Err::Truncated, "map frame: constants block of " + num(int64_t(n)) + " bytes, need 880");
    auto i32 = [&](uint32_t va) { int32_t v; std::memcpy(&v, p + (va - base), 4); return v; };
    auto f32 = [&](uint32_t va) { float v; std::memcpy(&v, p + (va - base), 4); return v; };
    MapFrame f;
    f.wrapX = i32(0xD90FBC) == 1;
    f.plotsW = i32(0xD911C4);
    f.plotsH = i32(0xD911C8);
    const int gw = i32(0xD91264), gh = i32(0xD91268);
    if (f.plotsW <= 0 || f.plotsH <= 0) return Status::fail(Err::BadLayout, "map frame: map size " + num(f.plotsW) + "x" + num(f.plotsH));
    if (f.plotsW > kMaxPlotsSide || f.plotsH > kMaxPlotsSide) return Status::fail(Err::Unsupported, "map frame: map larger than 256 plots");
    if (gw != f.plotsW || gh != f.plotsH + 4)
        return Status::fail(Err::BadLayout, "map frame: pattern grid " + num(gw) + "x" + num(gh) + " for a " + num(f.plotsW) + "x" + num(f.plotsH) + " map");
    for (int i = 0; i < 16; ++i) f.h2w[i] = f32(0xD91030 + 4 * i);
    for (int c = 0; c < 4; ++c) for (int i = 0; i < 4; ++i) f.corner[c][i] = f32(0xD910F0 + 16 * c + 4 * i);
    for (int i = 0; i < 16; ++i) { f.G[i] = i32(0xD911D0 + 4 * i); f.Ginv[i] = i32(0xD91210 + 4 * i); }
    for (int i = 0; i < 16; ++i) if (!std::isfinite(f.h2w[i])) return Status::fail(Err::BadLayout, "map frame: M_h2w not finite");
    out = f;
    return Status::Ok();
}

// ------------------------------------------------ the producer
struct Producer::Impl {
    std::shared_ptr<const PieceLibrary> lib;
    MapFrame fr;
    PlotTable plots;
    int gw = 0, gh = 0, sx = 0, sy = 0;
    uint64_t seed = 0;
    std::vector<uint32_t> flags;     // pattern grid, index x * gh + y (x-major, as the matcher reads it)
    std::vector<uint8_t> cont;
    std::vector<int32_t> owner;      // placement index + 1 that claimed the cell, 0 none
    std::vector<Placement> pl;       // the own placement set, in insertion order (the blend order)
    std::vector<uint8_t> alive;      // per placement
    std::vector<uint16_t> curH;      // current field (the tap plus every live window written so far)
    std::vector<uint8_t> curM;
    M4 T[4], G, Ginv;
    // per piece and transform: the used cells after T (q, r, s, claimed flag, cell flags, continent)
    struct UC { int32_t q, r, s; uint8_t claimed; uint32_t flags; uint8_t cont; };
    std::vector<std::vector<UC>> uc;     // index piece * 4 + t
    std::vector<uint32_t> patternsByLevel[17];
    struct Rect { int64_t x0, y0, x1, y1; };
    Rect rect[4][2];                     // per transform: [0] 128 rectangle, [1] 512 rectangle
    uint64_t draws = 0;
    // land/water (4913): the live coast curves (load-time lines plus every line a live change redrew) and the strips'
    // meshes, one per line in the curves' order
    bool lw = false;
    std::shared_ptr<const CoastStyles> coastStyles;
    std::shared_ptr<const StripMaterials> stripMats;
    StripStyles stripStyles;
    uint32_t mapSeed = 0;
    std::shared_ptr<const CoastCurves> curves;
    std::shared_ptr<const std::vector<StripMesh>> meshes;

    int cellIndex(int x, int y) const { return x * gh + y; }
    void plotToCell(int x, int y, int& cx, int& cy) const {
        const int64_t q = x - (y >> 1), r = y, s = -q - r;
        const int64_t v0 = q * G[0] + r * G[4] + s * G[8] + G[12];
        const int64_t v1 = q * G[1] + r * G[5] + s * G[9] + G[13];
        cy = int(v1); cx = int(v0 + (v1 >> 1));
    }
    Status buildCells(const PlotTable& t, std::vector<uint32_t>& f, std::vector<uint8_t>& c, bool neutralUnknownWonders = false) const {
        f.assign(size_t(gw) * gh, heightfield::kCellBorder);
        c.assign(size_t(gw) * gh, 0xFF);
        for (int y = 0; y < t.h; ++y) for (int x = 0; x < t.w; ++x) {
            int cx, cy; plotToCell(x, y, cx, cy);
            if (cx < 0 || cx >= gw || cy < 0 || cy >= gh)
                return Status::fail(Err::BadLayout, "plot " + num(x) + "," + num(y) + " maps outside the pattern grid");
            const PlotInfo& p = t.plots[size_t(y) * t.w + x];
            f[size_t(cellIndex(cx, cy))] = cellFlags(p.base, neutralUnknownWonders);
            c[size_t(cellIndex(cx, cy))] = uint8_t(p.continentArt & 0xFF);
        }
        return Status::Ok();
    }
    void prepare() {
        G = M4{}; Ginv = M4{};
        for (int i = 0; i < 16; ++i) { G[i] = fr.G[i]; Ginv[i] = fr.Ginv[i]; }
        for (int t = 0; t < 4; ++t) T[t] = transformMatrix(t);
        const auto& ps = lib->pieces();
        uc.assign(ps.size() * 4, {});
        for (size_t p = 0; p < ps.size(); ++p) {
            if (ps[p].flags & 4) patternsByLevel[std::min(ps[p].level, 16)].push_back(uint32_t(p));
            for (int t = 0; t < 4; ++t) {
                auto& v = uc[p * 4 + t];
                for (const Piece::Cell& c : ps[p].used) {
                    const int64_t q = int64_t(c.c) - (c.r >> 1), r = c.r, s = -q - r;
                    const M4& m = T[t];
                    UC u;
                    u.q = int32_t(q * m[0] + r * m[4] + s * m[8] + m[12]);
                    u.r = int32_t(q * m[1] + r * m[5] + s * m[9] + m[13]);
                    u.s = int32_t(q * m[2] + r * m[6] + s * m[10] + m[14]);
                    u.claimed = c.claimed; u.flags = c.flags; u.cont = c.continent;
                    v.push_back(u);
                }
            }
        }
        // the rectangle table (7.4): corners through the transform and M_h2w, relative to hex (0, 0, 0)
        float o[2]; hexToSample(0.f, 0.f, -0.0f, o);
        for (int t = 0; t < 4; ++t) {
            float Tf[16];
            for (int i = 0; i < 16; ++i) Tf[i] = float(T[t][i]);
            int64_t e[4][2];
            for (int c = 0; c < 4; ++c) {
                const float* v = fr.corner[c];
                float hv[4];
                for (int j = 0; j < 4; ++j) hv[j] = v[0] * Tf[j] + v[1] * Tf[4 + j] + v[2] * Tf[8 + j] + v[3] * Tf[12 + j];
                float w[2];
                for (int j = 0; j < 2; ++j) w[j] = hv[0] * fr.h2w[j] + hv[1] * fr.h2w[4 + j] + hv[2] * fr.h2w[8 + j] + hv[3] * fr.h2w[12 + j];
                e[c][0] = roundAway(-o[0] + w[0]);
                e[c][1] = roundAway(-o[1] + w[1]);
            }
            rect[t][0] = Rect{e[0][0], e[0][1], e[1][0], e[1][1]};
            rect[t][1] = Rect{e[2][0], e[2][1], e[3][0], e[3][1]};
        }
    }
    void hexToSample(float q, float r, float s, float out[2]) const {
        for (int j = 0; j < 2; ++j) out[j] = q * fr.h2w[j] + r * fr.h2w[4 + j] + s * fr.h2w[8 + j] + 1.0f * fr.h2w[12 + j];
    }
    // the sample rectangle of a placement (x0 > x1 or y0 > y1 for a reversed axis)
    Rect placementRect(const Placement& p) const {
        float w[2]; hexToSample(float(p.ax), float(p.ay), float(p.az), w);
        const int64_t ax = roundAway(w[0]), ay = roundAway(w[1]);
        const Rect& r = rect[p.transform][(lib->pieces()[p.piece].flags & 1) ? 0 : 1];
        return Rect{r.x0 + ax, r.y0 + ay, r.x1 + ax, r.y1 + ay};
    }
    // The grid cell a sample lies in (the hex whose centre is nearest, through the inverse of M_h2w's 2 x 2 part).
    void sampleCell(int c, int r, int& gx, int& gy) const {
        const double a = fr.h2w[0], b = fr.h2w[1], cc = fr.h2w[4], d = fr.h2w[5];
        const double det = a * d - b * cc;
        const double px = c + 0.5 - fr.h2w[12], py = r + 0.5 - fr.h2w[13];
        const double qf = (d * px - cc * py) / det, rf = (-b * px + a * py) / det, sf = -qf - rf;
        double q = std::nearbyint(qf), rr = std::nearbyint(rf), s = std::nearbyint(sf);
        const double dq = std::fabs(q - qf), dr = std::fabs(rr - rf), ds = std::fabs(s - sf);
        if (dq > dr && dq > ds) q = -rr - s; else if (dr > ds) rr = -q - s;
        const int64_t qi = int64_t(q), ri = int64_t(rr), si = -qi - ri;
        const int64_t v0 = qi * G[0] + ri * G[4] + si * G[8] + G[12];
        const int64_t v1 = qi * G[1] + ri * G[5] + si * G[9] + G[13];
        gy = int(std::min<int64_t>(std::max<int64_t>(v1, 0), gh - 1));
        int64_t x = v0 + (v1 >> 1);
        if (fr.wrapX) { x %= gw; if (x < 0) x += gw; } else x = std::min<int64_t>(std::max<int64_t>(x, 0), gw - 1);
        gx = int(x);
    }

    // ------------------------------------------------ the matcher (7.2), over a list of anchors
    // Tests (piece, transform) at anchor; on success claims the cells and appends the placement.
    bool tryPlace(uint32_t p, int t, int64_t ax, int64_t ay, int64_t az, std::vector<int>& got) {
        got.clear();
        for (const UC& u : uc[size_t(p) * 4 + t]) {
            const int64_t q = u.q + ax, r = u.r + ay, s = u.s + az;
            const int64_t v0 = q * G[0] + r * G[4] + s * G[8] + G[12];
            const int64_t v1 = q * G[1] + r * G[5] + s * G[9] + G[13];
            const int64_t gy = v1, gx = v0 + (v1 >> 1);
            if (gx < 0 || gx >= gw || gy < 0 || gy >= gh) continue;
            const size_t ci = size_t(gx) * gh + size_t(gy);
            const uint32_t gf = flags[ci];
            const bool both = u.flags != 0 && gf != 0;
            const uint8_t gc = cont[ci];
            const bool contOk = u.cont == 0 || gc == 0 || u.cont == gc;
            if (((u.flags & gf) == 0 || !contOk) && both) return false;
            if (u.claimed) {
                got.push_back(int(ci));
                if (owner[ci]) return false;
            }
        }
        if (got.empty()) return false;
        Placement P; P.piece = p; P.transform = uint8_t(t); P.ax = int32_t(ax); P.ay = int32_t(ay); P.az = int32_t(az);
        pl.push_back(P); alive.push_back(1);
        for (int ci : got) owner[size_t(ci)] = int32_t(pl.size());
        return true;
    }
    // One pass per level from the highest to 0. anchorsAB: offset positions (a, b) of the pass, in their base order.
    void match(Crt& rng, const std::vector<std::pair<int, int>>& anchorsAB, std::vector<uint32_t>* added) {
        const uint32_t nP = uint32_t(lib->pieces().size());
        std::vector<uint32_t> pOrder(nP);
        std::vector<int> tOrder(4);
        std::vector<int> got;
        std::vector<uint8_t> atLevel(nP);
        for (int L = lib->maxLevel(); L >= 0; --L) {
            // the level pass sets both index lists to 0 .. n-1 at its start (7.2)
            for (uint32_t i = 0; i < nP; ++i) pOrder[i] = i;
            for (int i = 0; i < 4; ++i) tOrder[i] = i;
            for (uint32_t i = 0; i < nP; ++i) atLevel[i] = (lib->pieces()[i].flags & 4) && lib->pieces()[i].level == L;
            struct A { int64_t x, y, z; };
            std::vector<A> anchors;
            anchors.reserve(anchorsAB.size());
            for (const auto& ab : anchorsAB) {
                const int64_t q = ab.first - (ab.second >> 1), r = ab.second, s = -q - r;
                const int64_t X = q * Ginv[0] + r * Ginv[4] + s * Ginv[8] + Ginv[12];
                const int64_t Y = q * Ginv[1] + r * Ginv[5] + s * Ginv[9] + Ginv[13];
                anchors.push_back(A{X, Y, -(X + Y)});
            }
            rng.shuffle(anchors);
            for (const A& a : anchors) {
                rng.shuffle(pOrder);
                rng.shuffle(tOrder);
                for (uint32_t p : pOrder) {
                    if (!atLevel[p]) continue;
                    for (int t : tOrder)
                        if (tryPlace(p, t, a.x, a.y, a.z, got) && added) added->push_back(uint32_t(pl.size() - 1));
                }
            }
        }
        draws += rng.calls;
    }

    // ------------------------------------------------ the blend (7.4) over a window of samples [x0, x1) x [y0, y1)
    // (x0 may be negative or x1 past samplesX on a wrapping map: those columns are the wrapped ones).
    void blend(const std::vector<uint32_t>& cf, const std::vector<uint8_t>& use, int x0, int y0, int x1, int y1,
               std::vector<uint16_t>& oh, std::vector<uint8_t>& om, const std::vector<uint8_t>* regionOverride = nullptr) const {
        const int W = x1 - x0, H = y1 - y0;
        std::vector<float> acc(size_t(W) * H, 0.f), ws(size_t(W) * H, kWsum0);
        std::vector<uint8_t> best(size_t(W) * H, 0), mat(size_t(W) * H, 3), reg(size_t(W) * H, 0);
        // the region byte: the class of the sample's hex (7.5, 8.3), or with land/water the EXE's region fill from the
        // coast curves (4910, 8.10), given by the caller for the window
        if (regionOverride) reg = *regionOverride;
        else for (int r = 0; r < H; ++r) for (int c = 0; c < W; ++c) {
            int gx, gy; sampleCell(wrapX(x0 + c), y0 + r, gx, gy);
            reg[size_t(r) * W + c] = cellClass(cf[size_t(gx) * gh + gy]);
        }
        for (size_t i = 0; i < pl.size(); ++i) {
            if (!use[i]) continue;
            const Placement& P = pl[i];
            const Piece& pc = lib->pieces()[P.piece];
            const Rect R = placementRect(P);
            const int size = pc.size();
            const bool fx = R.x1 >= R.x0, fy = R.y1 >= R.y0;
            const int64_t xa = fx ? R.x0 : R.x1, xb = fx ? R.x1 : R.x0;
            const int64_t ya = fy ? R.y0 : R.y1, yb = fy ? R.y1 : R.y0;
            const int64_t yl = std::max<int64_t>(ya, y0), yh = std::min<int64_t>(yb, y1);
            if (yl >= yh) continue;
            const uint16_t* th = nullptr; const uint8_t *t1 = nullptr, *thb = nullptr, *ttb = nullptr;
            for (int64_t sxp = xa; sxp < xb; ++sxp) {
                // window column of this sample (wrapping)
                int64_t col;
                if (fr.wrapX) {
                    int64_t xw = ((sxp % sx) + sx) % sx;
                    int64_t rel = ((xw - x0) % sx + sx) % sx;
                    if (rel >= W) continue;
                    col = rel;
                } else {
                    if (sxp < x0 || sxp >= x1 || sxp < 0 || sxp >= sx) continue;
                    col = sxp - x0;
                }
                const int64_t tx = fx ? sxp - R.x0 : R.x0 - 1 - sxp;
                if (!th) { th = lib->heights(P.piece); t1 = lib->bytes(P.piece, 1); thb = lib->bytes(P.piece, 2); ttb = lib->bytes(P.piece, 3); }
                for (int64_t syp = yl; syp < yh; ++syp) {
                    if (syp < 0 || syp >= sy) continue;
                    const int64_t ty = fy ? syp - R.y0 : R.y0 - 1 - syp;
                    const size_t k = size_t(syp - y0) * W + size_t(col);
                    if (reg[k] != pc.region) continue;
                    const size_t tk = size_t(ty) * size + size_t(tx);
                    const float hw = float(thb[tk]) * kInv255;
                    acc[k] = acc[k] + (float(th[tk]) * kInv65535 - kOffset) * hw;
                    ws[k] = ws[k] + hw;
                    if (ttb[tk] > best[k]) { best[k] = ttb[tk]; mat[k] = t1[tk]; }
                }
            }
        }
        oh.resize(size_t(W) * H); om = mat;
        for (size_t k = 0; k < oh.size(); ++k) {
            const float m = acc[k] / ws[k] + kOffset;
            const double v = std::nearbyint(double(m) * 65535.0);
            oh[k] = uint16_t(int64_t(v) & 0xFFFF);
        }
    }
    int wrapX(int x) const { if (!fr.wrapX) return x; x %= sx; return x < 0 ? x + sx : x; }

    // samples of a placement whose height or material weight is above zero (its reach), clipped to rows; x unwrapped
    template <class F> void forReach(const Placement& P, F f) const {
        const Rect R = placementRect(P);
        const Piece& pc = lib->pieces()[P.piece];
        const int size = pc.size();
        const uint8_t* hb = lib->bytes(P.piece, 2);
        const uint8_t* tb = lib->bytes(P.piece, 3);
        const bool fx = R.x1 >= R.x0, fy = R.y1 >= R.y0;
        const int64_t xa = fx ? R.x0 : R.x1, xb = fx ? R.x1 : R.x0, ya = fy ? R.y0 : R.y1, yb = fy ? R.y1 : R.y0;
        for (int64_t y = ya; y < yb; ++y) {
            if (y < 0 || y >= sy) continue;
            const int64_t ty = fy ? y - R.y0 : R.y0 - 1 - y;
            for (int64_t x = xa; x < xb; ++x) {
                const int64_t tx = fx ? x - R.x0 : R.x0 - 1 - x;
                const size_t tk = size_t(ty) * size + size_t(tx);
                if (hb[tk] || tb[tk]) f(x, y);
            }
        }
    }
    // the cells a placement's used cells cover (inside the grid)
    template <class F> void forUsedCells(const Placement& P, F f) const {
        for (const UC& u : uc[size_t(P.piece) * 4 + P.transform]) {
            const int64_t q = u.q + P.ax, r = u.r + P.ay, s = u.s + P.az;
            const int64_t v0 = q * G[0] + r * G[4] + s * G[8] + G[12];
            const int64_t v1 = q * G[1] + r * G[5] + s * G[9] + G[13];
            const int64_t gy = v1, gx = v0 + (v1 >> 1);
            if (gx < 0 || gx >= gw || gy < 0 || gy >= gh) continue;
            f(int(gx), int(gy));
        }
    }
};

Producer::Producer() : d_(new Impl()) {}
Producer::~Producer() {}
Producer::Producer(Producer&&) noexcept = default;
Producer& Producer::operator=(Producer&&) noexcept = default;
const std::vector<Placement>& Producer::placements() const { return d_->pl; }
const std::vector<uint16_t>& Producer::currentHeights() const { return d_->curH; }
const std::vector<uint8_t>& Producer::currentMaterials() const { return d_->curM; }
uint64_t Producer::matcherDraws() const { return d_->draws; }

bool Producer::landWaterEnabled() const { return d_ && d_->lw; }
uint32_t Producer::loadCoastDegenerate() const { return (d_ && d_->curves) ? d_->curves->degenerate : 0; }

Status Producer::enableLandWater(const CoastStyles& styles, const StripMaterials& materials, uint32_t mapRandomSeed) {
    if (!d_ || !d_->lib) return Status::fail(Err::BadLayout, "producer: enableLandWater before create");
    Impl& d = *d_;
    auto cs = std::make_shared<const CoastStyles>(styles);
    auto mats = std::make_shared<const StripMaterials>(materials);
    auto cv = std::make_shared<CoastCurves>();
    C5R_TRY(buildCoastCurves(d.fr, d.plots, *cs, mapRandomSeed, *cv));
    const StripStyles sst = stripStylesOf(*cs);
    auto meshes = std::make_shared<std::vector<StripMesh>>(cv->lines.size());
    for (size_t i = 0; i < cv->lines.size(); ++i) C5R_TRY(buildStripMesh(stripLinesOf(cv->lines[i]), sst, *mats, (*meshes)[i]));
    d.coastStyles = cs; d.stripMats = mats; d.stripStyles = sst; d.mapSeed = mapRandomSeed;
    d.curves = cv; d.meshes = meshes; d.lw = true;
    return Status::Ok();
}

Status Producer::create(std::shared_ptr<const PieceLibrary> lib, const MapFrame& fr, const PlotTable& atLoad,
                        const TapView& tap, uint64_t seed, Producer& out) {
    if (!lib) return Status::fail(Err::BadLayout, "producer: no piece library");
    if (fr.plotsW <= 0 || fr.plotsH <= 0) return Status::fail(Err::BadLayout, "producer: empty map frame");
    if (fr.plotsW > kMaxPlotsSide || fr.plotsH > kMaxPlotsSide) return Status::fail(Err::Unsupported, "producer: map larger than 256 plots");
    if (atLoad.w != fr.plotsW || atLoad.h != fr.plotsH || atLoad.plots.size() != size_t(atLoad.w) * atLoad.h)
        return Status::fail(Err::BadLayout, "producer: plot table " + num(atLoad.w) + "x" + num(atLoad.h) + " (" +
                            num(int64_t(atLoad.plots.size())) + " plots) for a " + num(fr.plotsW) + "x" + num(fr.plotsH) + " map");
    if (!tap.heights || !tap.materials || tap.samplesX != fr.samplesX() || tap.samplesY != fr.samplesY())
        return Status::fail(Err::BadLayout, "producer: tap " + num(tap.samplesX) + "x" + num(tap.samplesY) + ", expected " +
                            num(fr.samplesX()) + "x" + num(fr.samplesY()));
    std::unique_ptr<Impl> d(new Impl());
    d->lib = lib; d->fr = fr; d->plots = atLoad; d->seed = seed;
    d->gw = fr.gridW(); d->gh = fr.gridH(); d->sx = fr.samplesX(); d->sy = fr.samplesY();
    d->prepare();
    C5R_TRY(d->buildCells(atLoad, d->flags, d->cont));
    d->owner.assign(d->flags.size(), 0);
    const size_t ns = size_t(d->sx) * d->sy;
    d->curH.assign(tap.heights, tap.heights + ns);
    d->curM.assign(tap.materials, tap.materials + ns);
    std::vector<std::pair<int, int>> ab;
    for (int a = -2; a < d->gw + 2; ++a) for (int b = -2; b < d->gh + 2; ++b) ab.push_back(std::make_pair(a, b));
    Crt rng(uint32_t(seed ^ (seed >> 32)));
    d->match(rng, ab, nullptr);
    out.d_ = std::move(d);
    return Status::Ok();
}

Status Producer::ownField(std::vector<uint16_t>& h, std::vector<uint8_t>& m) const {
    std::vector<uint8_t> use(d_->pl.size());
    for (size_t i = 0; i < use.size(); ++i) use[i] = d_->alive[i];
    d_->blend(d_->flags, use, 0, 0, d_->sx, d_->sy, h, m);
    return Status::Ok();
}

Status Producer::change(const PlotTable& after, ChangeResult& out) { return change(after, out, Options()); }

Status Producer::change(const PlotTable& after, ChangeResult& out, const Options& opt) {
    const auto t0 = std::chrono::steady_clock::now();
    Impl& d = *d_;
    out = ChangeResult();
    auto finish = [&]() { out.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); };
    if (after.w != d.plots.w || after.h != d.plots.h || after.plots.size() != d.plots.plots.size())
        return Status::fail(Err::BadLayout, "producer: plot table " + num(after.w) + "x" + num(after.h) + " does not fit the load's " +
                            num(d.plots.w) + "x" + num(d.plots.h));
    std::vector<uint32_t> nf; std::vector<uint8_t> nc;
    C5R_TRY(d.buildCells(after, nf, nc, opt.neutralUnknownWonders));   // ticket 0613: the neutral base for unknown wonders
    std::vector<int> changedCells;
    for (size_t i = 0; i < nf.size(); ++i) if (nf[i] != d.flags[i] || nc[i] != d.cont[i]) changedCells.push_back(int(i));
    for (int y = 0; y < after.h; ++y) for (int x = 0; x < after.w; ++x) {
        int cx, cy; d.plotToCell(x, y, cx, cy);
        const size_t ci = size_t(cx) * d.gh + cy;
        if (nf[ci] != d.flags[ci] || nc[ci] != d.cont[ci]) out.changedPlots.push_back(std::make_pair(x, y));
    }
    if (changedCells.empty()) { out.reason = WaitReason::NoChange; out.reasonText = "no plot's pattern cell changed"; finish(); return Status::Ok(); }

    // work on a copy of the light state so that a "wait for reload" leaves the producer as it was; the current field
    // is not copied: it is lent to this call and handed back by the guard on every exit
    struct Lend {
        std::vector<uint16_t>& dh; std::vector<uint8_t>& dm; std::vector<uint16_t> h; std::vector<uint8_t> m;
        Lend(std::vector<uint16_t>& a, std::vector<uint8_t>& b) : dh(a), dm(b) { h.swap(dh); m.swap(dm); }
        ~Lend() { dh.swap(h); dm.swap(m); }
    } field(d.curH, d.curM);
    Impl w = d;
    std::vector<uint16_t>& curH = field.h;
    std::vector<uint8_t>& curM = field.m;
    w.flags = nf; w.cont = nc;
    std::vector<uint8_t> isChanged(w.flags.size(), 0);
    for (int ci : changedCells) isChanged[size_t(ci)] = 1;
    // (2) free the placements whose used cells include a changed cell
    std::vector<uint32_t> removed;
    for (size_t i = 0; i < w.pl.size(); ++i) {
        if (!w.alive[i]) continue;
        bool hit = false;
        w.forUsedCells(w.pl[i], [&](int gx, int gy) { if (isChanged[size_t(gx) * w.gh + gy]) hit = true; });
        if (hit) removed.push_back(uint32_t(i));
    }
    std::vector<uint8_t> freed(w.flags.size(), 0);
    for (int ci : changedCells) freed[size_t(ci)] = 1;
    for (uint32_t i : removed) {
        w.alive[i] = 0;
        for (size_t ci = 0; ci < w.owner.size(); ++ci) if (w.owner[ci] == int32_t(i + 1)) { w.owner[ci] = 0; freed[ci] = 1; }
    }
    // re-match: anchors within 12 offset steps of a freed cell (a pattern spans 10 hexes), seeded from the load's seed
    // and the change, so that the same change gives the same picture
    std::set<std::pair<int, int>> abSet;
    int minX = 1 << 30, maxX = -(1 << 30), minY = 1 << 30, maxY = -(1 << 30);
    for (size_t ci = 0; ci < freed.size(); ++ci) if (freed[ci]) {
        const int gx = int(ci / size_t(w.gh)), gy = int(ci % size_t(w.gh));
        minX = std::min(minX, gx); maxX = std::max(maxX, gx); minY = std::min(minY, gy); maxY = std::max(maxY, gy);
    }
    if ((maxX - minX + 1) * (maxY - minY + 1) > kMaxWindowPlots * 4) {
        out.reason = WaitReason::TooLarge; out.reasonText = "the freed cells span " + num(maxX - minX + 1) + "x" + num(maxY - minY + 1) + " plots";
        finish(); return Status::Ok();
    }
    for (int a = minX - 12; a <= maxX + 12; ++a) for (int b = minY - 12; b <= maxY + 12; ++b)
        if (a >= -2 && a < w.gw + 2 && b >= -2 && b < w.gh + 2) abSet.insert(std::make_pair(a, b));
    std::vector<std::pair<int, int>> ab(abSet.begin(), abSet.end());
    uint64_t cseed = w.seed;
    for (int ci : changedCells) { cseed ^= uint64_t(ci) + 0x9E3779B97F4A7C15ull + (cseed << 6) + (cseed >> 2); cseed ^= nf[size_t(ci)]; }
    Crt rng(uint32_t(cseed ^ (cseed >> 32)));
    std::vector<uint32_t> added;
    w.match(rng, ab, &added);
    out.removedPlacements = uint32_t(removed.size());
    out.addedPlacements = uint32_t(added.size());

    // (3) the full set: changed plots' samples and the reach of every removed or added piece
    std::vector<std::pair<int64_t, int64_t>> full;
    for (uint32_t i : removed) w.forReach(w.pl[i], [&](int64_t x, int64_t y) { full.push_back(std::make_pair(x, y)); });
    for (uint32_t i : added) w.forReach(w.pl[i], [&](int64_t x, int64_t y) { full.push_back(std::make_pair(x, y)); });
    // (4913, 8.17) a change of a plot's land/water class with land/water enabled: the new coast curves; the live set
    // keeps every line whose pre-jitter shape is unchanged (its load-time jitter) and takes the changed lines from the
    // new build; the changed lines' points within kCoastNearSamples of a changed plot widen the full set
    bool classMode = false;
    CoastCurves liveCurves;
    std::vector<StripMesh> liveMeshes;
    {
        bool classChanged = false;
        for (int ci : changedCells) if (cellClass(nf[size_t(ci)]) != cellClass(d.flags[size_t(ci)])) classChanged = true;
        if (classChanged && d.lw) {
            CoastCurves fresh;
            C5R_TRY(buildCoastCurves(w.fr, after, *d.coastStyles, d.mapSeed, fresh));
            const CoastCurves& cur = *d.curves;
            std::vector<uint8_t> curTaken(cur.lines.size(), 0);
            std::vector<CoastLine> live; live.reserve(fresh.lines.size());
            std::vector<int> fromCur;   // per live line: index into cur, or -1 (a changed line)
            for (const CoastLine& f : fresh.lines) {
                int hit = -1;
                for (size_t i = 0; i < cur.lines.size(); ++i)
                    if (!curTaken[i] && cur.lines[i].shape == f.shape && cur.lines[i].type == f.type && cur.lines[i].closed == f.closed) { hit = int(i); break; }
                if (hit >= 0) {
                    curTaken[size_t(hit)] = 1; live.push_back(cur.lines[size_t(hit)]); fromCur.push_back(hit);
                    const auto& a = cur.lines[size_t(hit)].mesh[2];
                    const auto& b = f.mesh[2];
                    bool same = a.size() == b.size();
                    for (size_t j = 0; same && j < a.size(); ++j) same = a[j].x == b[j].x && a[j].y == b[j].y;
                    if (!same) ++out.coastLinesReloadMoves;
                } else { live.push_back(f); fromCur.push_back(-1); }
            }
            // the changed plots' centres (samples)
            std::vector<std::pair<float, float>> centres;
            for (const auto& pxy : out.changedPlots) {
                const int64_t q = pxy.first - (pxy.second >> 1);
                float cxy[2]; w.hexToSample(float(q), float(pxy.second), float(-q - pxy.second), cxy);
                centres.push_back(std::make_pair(cxy[0], cxy[1]));
            }
            const float S = float(w.sx);
            int64_t nx0 = INT64_MAX, nx1 = INT64_MIN, ny0 = INT64_MAX, ny1 = INT64_MIN;
            auto scan = [&](const CoastLine& L) {
                bool beyond = false;
                for (int k = 0; k < 5; ++k) for (const CoastMeshPoint& m : L.mesh[k]) {
                    bool near = false; float ux = 0;
                    for (const auto& c : centres) {
                        float dx = m.x - c.first;
                        if (w.fr.wrapX) { dx = std::fmod(dx, S); if (dx > S / 2) dx -= S; else if (dx < -S / 2) dx += S; }
                        const float dy = m.y - c.second;
                        if (dx * dx + dy * dy <= float(kCoastNearSamples) * kCoastNearSamples) { near = true; ux = c.first + dx; break; }
                    }
                    if (!near) { beyond = true; continue; }
                    const int64_t xi = int64_t(std::floor(ux)), yi = int64_t(std::floor(m.y));
                    nx0 = std::min(nx0, xi); nx1 = std::max(nx1, xi + 1); ny0 = std::min(ny0, yi); ny1 = std::max(ny1, yi + 1);
                }
                return beyond;
            };
            for (size_t i = 0; i < cur.lines.size(); ++i)
                if (!curTaken[i]) { ++out.coastLinesRedrawn; if (scan(cur.lines[i])) ++out.coastLinesBeyondWindow; }
            for (size_t i = 0; i < live.size(); ++i)
                if (fromCur[i] < 0) { ++out.coastLinesRedrawn; if (scan(live[i])) ++out.coastLinesBeyondWindow; }
            // the changed plots' own samples (their hex lies within about 40 samples of the centre)
            for (const auto& c : centres) {
                const int64_t cx = int64_t(std::floor(c.first)), cy = int64_t(std::floor(c.second));
                nx0 = std::min(nx0, cx - 40); nx1 = std::max(nx1, cx + 41);
                ny0 = std::min(ny0, cy - 40); ny1 = std::max(ny1, cy + 41);
            }
            ny0 = std::max<int64_t>(ny0, 0); ny1 = std::min<int64_t>(ny1, w.sy);
            if (nx1 > nx0 && ny1 > ny0) {
                if (int64_t(nx1 - nx0) * (ny1 - ny0) > int64_t(kMaxWindowPlots) * 64 * 64) {
                    out.reason = WaitReason::TooLarge;
                    out.reasonText = "the changed coast lines span " + num(nx1 - nx0) + "x" + num(ny1 - ny0) + " samples";
                    finish(); return Status::Ok();
                }
                for (int64_t y = ny0; y < ny1; ++y) for (int64_t x = nx0; x < nx1; ++x) full.push_back(std::make_pair(x, y));
            }
            C5R_TRY(assembleCurves(std::move(live), liveCurves));
            out.coastDegenerate = liveCurves.degenerate + fresh.degenerateSmooth;   // MC #9538 item 1 (8.10.3)
            liveMeshes.resize(liveCurves.lines.size());
            for (size_t i = 0; i < liveCurves.lines.size(); ++i) {
                if (fromCur[i] >= 0) { liveMeshes[i] = (*d.meshes)[size_t(fromCur[i])]; continue; }
                C5R_TRY(buildStripMesh(stripLinesOf(liveCurves.lines[i]), d.stripStyles, *d.stripMats, liveMeshes[i]));
            }
            classMode = true;
            out.landWater = true;
        }
    }
    // (0717, live-terrain-mountain-partial.md 4) a change that keeps every plot's land/water class, with land/water on:
    // drawn through the same step as a land/water change in which no coast line changed: the region fill of the current
    // lines, the blend and the coast strips. The coast beside it is then drawn as a reload draws it; the 4908 water keep
    // with its 32-sample feather left the side of a hex next to the coast on the old ground.
    bool landMode = false;
    if (!classMode && d.lw && opt.landRegion) {
        bool water = false;
        for (int ci : changedCells) if (cellClass(nf[size_t(ci)]) != 0 || cellClass(d.flags[size_t(ci)]) != 0) water = true;
        landMode = !water;
        out.landRegion = landMode;
    }
    const bool regionMode = classMode || landMode;
    int64_t bx0 = INT64_MAX, bx1 = INT64_MIN, by0 = INT64_MAX, by1 = INT64_MIN;
    for (const auto& s : full) { bx0 = std::min(bx0, s.first); bx1 = std::max(bx1, s.first + 1); by0 = std::min(by0, s.second); by1 = std::max(by1, s.second + 1); }
    // (a changed cell lies in the used cells of a removed placement, so its samples are inside that piece's reach)
    if (full.empty()) { // a change whose pieces reach nothing (cannot happen with a valid library): treat as no change
        out.reason = WaitReason::NoChange; out.reasonText = "the change re-matched to the same pieces"; finish(); return Status::Ok();
    }
    const int R = opt.diagnosticRingSamples > 0 ? opt.diagnosticRingSamples : kRingSamples;
    int64_t wx0 = bx0 - R, wx1 = bx1 + R, wy0 = std::max<int64_t>(by0 - R, 0), wy1 = std::min<int64_t>(by1 + R, w.sy);
    if (!w.fr.wrapX) { wx0 = std::max<int64_t>(wx0, 0); wx1 = std::min<int64_t>(wx1, w.sx); }
    const int WW = int(wx1 - wx0), WH = int(wy1 - wy0);
    if (wx1 - wx0 > w.sx || int64_t(WW) * WH > int64_t(kMaxWindowPlots) * 64 * 64) {
        out.reason = WaitReason::TooLarge; out.reasonText = "window " + num(WW) + "x" + num(WH) + " samples"; finish(); return Status::Ok();
    }
    // (4913) the region bytes of the window from the old and the new curves
    std::vector<uint8_t> rB, rA;
    if (classMode) {
        C5R_TRY(windowRegion(w.fr, d.plots, *d.curves, int(wx0), int(wy0), WW, WH, rB));
        C5R_TRY(windowRegion(w.fr, after, liveCurves, int(wx0), int(wy0), WW, WH, rA));
    } else if (landMode) {   // (0717) no line changed: one region for both sides
        C5R_TRY(windowRegion(w.fr, d.plots, *d.curves, int(wx0), int(wy0), WW, WH, rB));
        rA = rB;
    }
    // weights: 1 on the full set, falling to 0 over the ring (two-pass chamfer distance, 3/4 metric scaled to samples)
    std::vector<int> dist(size_t(WW) * WH, 1 << 28);
    for (const auto& s : full) {
        const int64_t r = s.second - wy0; if (r < 0 || r >= WH) continue;
        int64_t c = s.first - wx0;
        if (w.fr.wrapX) { /* window may not straddle: the seam rule below stops such windows */ }
        if (c < 0 || c >= WW) continue;
        dist[size_t(r) * WW + size_t(c)] = 0;
    }
    for (int r = 0; r < WH; ++r) for (int c = 0; c < WW; ++c) {
        int& v = dist[size_t(r) * WW + c];
        if (c > 0) v = std::min(v, dist[size_t(r) * WW + c - 1] + 3);
        if (r > 0) { v = std::min(v, dist[size_t(r - 1) * WW + c] + 3);
            if (c > 0) v = std::min(v, dist[size_t(r - 1) * WW + c - 1] + 4);
            if (c + 1 < WW) v = std::min(v, dist[size_t(r - 1) * WW + c + 1] + 4); }
    }
    for (int r = WH - 1; r >= 0; --r) for (int c = WW - 1; c >= 0; --c) {
        int& v = dist[size_t(r) * WW + c];
        if (c + 1 < WW) v = std::min(v, dist[size_t(r) * WW + c + 1] + 3);
        if (r + 1 < WH) { v = std::min(v, dist[size_t(r + 1) * WW + c] + 3);
            if (c + 1 < WW) v = std::min(v, dist[size_t(r + 1) * WW + c + 1] + 4);
            if (c > 0) v = std::min(v, dist[size_t(r + 1) * WW + c - 1] + 4); }
    }
    std::vector<float> wt(dist.size());
    for (size_t k = 0; k < dist.size(); ++k) {
        const double ds = dist[k] / 3.0;
        wt[k] = ds <= 0 ? 1.f : (ds >= R ? 0.f : float(1.0 - ds / R));
    }
    // (4908, option A, live-terrain.md 8.9) samples the producer cannot reproduce are kept from the current field:
    // with protectWater the samples of coast, ocean and margin hexes and of material 37 (the EXE's water region, whose
    // polylines this change does not move as long as no plot changes its land or water class); with protectMarsh the
    // samples of marsh plots and of materials no piece makes (the marsh stamp, a later writer). w is forced to 0 there
    // and feathered to 0 over kFeatherSamples on the land side, so the window meets the kept samples without a step.
    std::vector<uint8_t> prot(dist.size(), 0);
    std::vector<uint8_t> why, movedDiag;   // offline diagnostics only (Options::diagnostics)
    if (opt.diagnostics) { why.assign(dist.size(), 0); movedDiag.assign(dist.size(), 0); }
    std::vector<uint8_t> wonderKept(dist.size(), 0);   // (4913 (b)) the kept samples of natural wonders' own hexes
    std::set<std::pair<int, int>> wcells;
    // with protectRivers and riverKeepDisc (the 4908 rule, 8.9): the samples within kRiverKeepSamples of the centre of a
    // plot with a river edge are kept too (the river carve, 7.4 step 7, lowers the field along the edge on both sides)
    std::vector<std::pair<float, float>> riverCentres;
    // with protectRivers (ticket 0717, live-terrain-mountain-partial.md 3): the river band. rfac per window sample: 0
    // within kRiverCoreWorld of a river edge, rising linearly to 1 at kRiverReachWorld; a raise is scaled by it, so the
    // carved bed and its banks stay where the carve made them. The 4908 disc kept the whole hex of every plot with a
    // river on any edge, the changed plot's own included, and about half of each neighbour's.
    std::vector<float> rfac;
    std::vector<std::pair<float, float>> bandMids;   // diagnostics only: the band's edge midpoints (never the disc's centres)
    // A land/water change keeps the 4908 disc with its w7-ridge exemptions (epic 0615's validated rule; its keep is ticket
    // 0631's); the band is for the changes that keep every plot's class
    const bool useDisc = opt.riverKeepDisc || classMode;
    if (opt.protectRivers && !useDisc) {
        struct Seg { float ax, ay, bx, by; };
        std::vector<Seg> segs;
        auto flagged = [&](int x, int y) {
            if (y < 0 || y >= after.h) return false;
            if (w.fr.wrapX) x = ((x % after.w) + after.w) % after.w;
            else if (x < 0 || x >= after.w) return false;
            return after.plots[size_t(y) * after.w + x].riverEdges != 0;
        };
        auto centre = [&](int64_t q, int64_t r, float o[2]) { w.hexToSample(float(q), float(r), float(-q - r), o); };
        const float reachRows = kRiverReachWorld / kRowWorld + 2.f;
        for (int py = 0; py < after.h; ++py) for (int px = 0; px < after.w; ++px) {
            const uint8_t bits = after.plots[size_t(py) * after.w + px].riverEdges;
            if (!bits) continue;
            const int64_t q = px - (py >> 1), r = py;
            float A[2], E[2], NE[2], SE[2], SW[2], Wn[2];
            centre(q, r, A); centre(q + 1, r, E); centre(q, r + 1, NE); centre(q + 1, r - 1, SE); centre(q, r - 1, SW); centre(q - 1, r, Wn);
            auto corner = [&](const float* a, const float* b, const float* c, float* o) { o[0] = (a[0] + b[0] + c[0]) / 3.f; o[1] = (a[1] + b[1] + c[1]) / 3.f; };
            float cENE[2], cESE[2], cSESW[2], cSWW[2];
            corner(A, E, NE, cENE); corner(A, E, SE, cESE); corner(A, SE, SW, cSESW); corner(A, SW, Wn, cSWW);
            // bit 0 (W of river): the east edge, across it plot (x + 1, y); bit 1 (NW of river): the south-east edge,
            // across it hex (q + 1, r - 1); bit 2 (NE of river): the south-west edge, across it hex (q, r - 1)
            const float* ends[3][2] = {{cENE, cESE}, {cESE, cSESW}, {cSESW, cSWW}};
            const int nx[3] = {px + 1, int(q + 1 + ((r - 1) >> 1)), int(q + ((r - 1) >> 1))};
            const int ny[3] = {py, py - 1, py - 1};
            for (int b = 0; b < 3; ++b) {
                const bool on = opt.riverEdgesFromAnyFlag ? flagged(nx[b], ny[b]) : (bits & (1 << b)) != 0;
                if (!on) continue;
                const float* a = ends[b][0]; const float* e = ends[b][1];
                if (std::max(a[1], e[1]) < float(wy0) - reachRows || std::min(a[1], e[1]) > float(wy1) + reachRows) continue;
                for (int sh = -1; sh <= 1; ++sh) {
                    if (sh && !w.fr.wrapX) continue;
                    const float off = float(sh) * float(w.sx);
                    if (std::max(a[0], e[0]) + off < float(wx0) - kRiverReachWorld - 2.f ||
                        std::min(a[0], e[0]) + off > float(wx1) + kRiverReachWorld + 2.f) continue;
                    segs.push_back(Seg{a[0] + off, a[1], e[0] + off, e[1]});
                }
            }
        }
        if (!segs.empty()) {
            rfac.assign(dist.size(), 1.f);
            std::vector<float> rd(dist.size(), 1e30f);
            for (const Seg& s : segs) {
                if (opt.diagnostics) bandMids.push_back(std::make_pair((s.ax + s.bx) / 2.f, (s.ay + s.by) / 2.f));
                const int c0 = std::max(0, int(std::floor(std::min(s.ax, s.bx) - kRiverReachWorld)) - int(wx0));
                const int c1 = std::min(WW - 1, int(std::ceil(std::max(s.ax, s.bx) + kRiverReachWorld)) - int(wx0));
                const int r0 = std::max(0, int(std::floor(std::min(s.ay, s.by) - reachRows)) - int(wy0));
                const int r1 = std::min(WH - 1, int(std::ceil(std::max(s.ay, s.by) + reachRows)) - int(wy0));
                const double ax = s.ax, ay = double(s.ay) * kRowWorld, dx = double(s.bx) - s.ax, dy = (double(s.by) - s.ay) * kRowWorld;
                const double dd = dx * dx + dy * dy;
                for (int r = r0; r <= r1; ++r) for (int c = c0; c <= c1; ++c) {
                    const double px = double(wx0 + c), py = double(wy0 + r) * kRowWorld;
                    double t = dd > 0 ? ((px - ax) * dx + (py - ay) * dy) / dd : 0.0;
                    t = std::min(1.0, std::max(0.0, t));
                    const double ex = ax + t * dx - px, ey = ay + t * dy - py;
                    const float dist2 = float(std::sqrt(ex * ex + ey * ey));
                    float& v = rd[size_t(r) * WW + c];
                    if (dist2 < v) v = dist2;
                }
            }
            for (size_t k = 0; k < rd.size(); ++k)
                if (rd[k] < kRiverCoreWorld) rfac[k] = 0.f;
                else if (rd[k] < kRiverReachWorld) rfac[k] = (rd[k] - kRiverCoreWorld) / (kRiverReachWorld - kRiverCoreWorld);
        }
    }
    if (opt.protectRivers && useDisc) {
        for (int py = 0; py < after.h; ++py) for (int px = 0; px < after.w; ++px) {
            if (!after.plots[size_t(py) * after.w + px].riverEdges) continue;
            float cxy[2];
            const int64_t q = px - (py >> 1);
            w.hexToSample(float(q), float(py), float(-q - py), cxy);
            if (cxy[1] < wy0 - kRiverKeepSamples || cxy[1] > wy1 + kRiverKeepSamples) continue;
            riverCentres.push_back(std::make_pair(cxy[0], cxy[1]));
        }
    }
    for (int r = 0; r < WH; ++r) for (int c = 0; c < WW; ++c) {
        const size_t k = size_t(r) * WW + c;
        if (wt[k] <= 0.f) continue;
        // (4913) a plot whose land/water class changed, and every sample the coast moved over, is redrawn, its river carve
        // included: elsewhere the river beside it is kept (live-terrain.md 8.17)
        bool changedHex = false, waterHexNoKeep = false;
        if (regionMode) {   // (0717) a land change in landMode as a land/water change with no line changed
            int gx0, gy0; w.sampleCell(w.wrapX(int(wx0) + c), int(wy0) + r, gx0, gy0);
            const size_t c0 = size_t(gx0) * w.gh + gy0;
            // (4913 (b), live-terrain.md 8.17) a natural wonder's own hex keeps the current field (the load's ground):
            // the re-match would place the wonder's piece by the producer's own seed, possibly in another transform. Not
            // when the wonder's own plot changed. The coast around it is redrawn; the feather below meets it.
            const int py0 = gy0 - 2;
            if (opt.protectWonders && classMode && !isChanged[c0] && py0 >= 0 && py0 < after.h &&   // (0717) not landMode
                after.plots[size_t(py0) * after.w + gx0].base.naturalWonder) {
                prot[k] = 1; wonderKept[k] = 1; ++out.samplesWonderKept; if (!why.empty()) why[k] = 1; continue;
            }
            // (0717) in landMode the changed plot keeps the river band: the carve along its river runs on the new ground
            changedHex = classMode && (isChanged[c0] != 0 || rB[k] != rA[k]);   // or the coast moved over it
            if (!movedDiag.empty()) movedDiag[k] = changedHex ? 1 : 0;
            // (w7-ridge, live-terrain.md 8.17 "Finding: a ridge of the old shore") the river keep holds the river carve,
            // which lies on land: a sample whose hex is water after the change is redrawn (the region fill and the strips
            // draw the water there). Kept, the old shore's beach stayed as a raised line in the new water: the server
            // marks every plot with a river on any edge (and so the changed plot itself) as a river-edge plot.
            const bool waterAfter = cellClass(w.flags[c0]) != 0 || py0 < 0 || py0 >= after.h;
            if (waterAfter && !opt.diagnosticRiverKeepOnWater) { waterHexNoKeep = true; if (!why.empty()) why[k] = 5; }
        }
        if (!riverCentres.empty() && !changedHex && !waterHexNoKeep) {
            const float sxp = float(wx0 + c), syp = float(wy0 + r);
            bool nearRiver = false;
            for (const auto& rc : riverCentres) {
                float dx = sxp - rc.first;
                if (w.fr.wrapX) { const float S = float(w.sx); if (dx > S / 2) dx -= S; else if (dx < -S / 2) dx += S; }
                const float dy = syp - rc.second;
                if (dx * dx + dy * dy < float(kRiverKeepSamples) * kRiverKeepSamples) { nearRiver = true; break; }
            }
            if (nearRiver) { prot[k] = 1; if (!why.empty()) why[k] = 2; continue; }
        }
        if (!rfac.empty()) {
            if (changedHex || waterHexNoKeep) rfac[k] = 1.f;   // redrawn whole, as the 4908 keep was not applied there
            else if (rfac[k] < 1.f) { if (rfac[k] <= 0.f) ++out.samplesRiverCore; else ++out.samplesRiverBand;
                                      if (!why.empty()) why[k] = 2; }
        }
        int gx, gy; w.sampleCell(w.wrapX(int(wx0) + c), int(wy0) + r, gx, gy);
        const size_t fk = size_t(wy0 + r) * w.sx + size_t(w.wrapX(int(wx0) + c));
        const uint8_t m = curM[fk];
        const int py = gy - 2;
        const bool water = cellClass(w.flags[size_t(gx) * w.gh + gy]) != 0 || py < 0 || py >= after.h || m == 37;
        const bool marsh = !water && (after.plots[size_t(py) * after.w + gx].isMarsh() || !w.lib->makesMaterial(m));
        if (water && opt.protectWater && !regionMode) { prot[k] = 1; if (!why.empty()) why[k] = 4; continue; }
        if (marsh && opt.protectMarsh) { prot[k] = 1; if (!why.empty()) why[k] = 3; continue; }
        if (water && out.reason == WaitReason::None && !regionMode) { out.reason = WaitReason::Water; out.reasonText = "water, margin or material 37 at sample " + num(w.wrapX(int(wx0) + c)) + "," + num(wy0 + r); }
        if (marsh && out.reason == WaitReason::None) { out.reason = WaitReason::Marsh; out.reasonText = "marsh or material " + num(m) + " at sample " + num(w.wrapX(int(wx0) + c)) + "," + num(wy0 + r); }
        wcells.insert(std::make_pair(gx, gy));
    }
    // a changed plot that is water, or whose land/water class changed, moves the coast polylines: not reproducible
    for (const auto& pxy : out.changedPlots) {
        if (out.reason != WaitReason::None || classMode) break;
        int cx, cy; w.plotToCell(pxy.first, pxy.second, cx, cy);
        const size_t ci = size_t(cx) * w.gh + cy;
        if (cellClass(nf[ci]) != 0 || cellClass(d.flags[ci]) != 0) {
            out.reason = WaitReason::Water; out.reasonText = "plot " + num(pxy.first) + "," + num(pxy.second) + " is or was water (the coast lines move)";
        }
    }
    if (int(wcells.size()) > kMaxWindowPlots && out.reason == WaitReason::None) { out.reason = WaitReason::TooLarge; out.reasonText = num(int64_t(wcells.size())) + " plots in the window"; }
    for (const auto& cxy : wcells) {
        if (out.reason != WaitReason::None) break;
        const int gx = cxy.first, gy = cxy.second;
        const int py = gy - 2;
        // (h18, live-terrain.md 11.8 and 11.9) a cell of the grid's two margin rows above and below the map is frame water
        // with no plot: no lake, no river edge. It reaches wcells in a land/water change (water is not kept there, line
        // above); reading after.plots for it read before or past the table (the server's crash of 2026-11-21 19:18).
        if (py < 0 || py >= after.h || gx < 0 || gx >= after.w) continue;
        const PlotInfo& p = after.plots[size_t(py) * after.w + gx];
        if (p.lake && !opt.protectWater && !regionMode) { out.reason = WaitReason::Lake; out.reasonText = "lake at plot " + num(gx) + "," + num(py); break; }
        if (p.riverEdges && !opt.protectRivers) { out.reason = WaitReason::RiverEdge; out.reasonText = "river edge at plot " + num(gx) + "," + num(py); break; }
    }
    // feather: w falls to 0 over kFeatherSamples towards the kept samples (two-pass chamfer from the kept set)
    const std::vector<float> wtFull = wt;   // (4913) before the feather
    {
        bool any = false;
        for (uint8_t p : prot) any = any || p;
        if (any) {
            std::vector<int> dp(prot.size(), 1 << 28);
            for (size_t k = 0; k < prot.size(); ++k) if (prot[k]) dp[k] = 0;
            for (int r = 0; r < WH; ++r) for (int c = 0; c < WW; ++c) {
                int& v = dp[size_t(r) * WW + c];
                if (c > 0) v = std::min(v, dp[size_t(r) * WW + c - 1] + 3);
                if (r > 0) { v = std::min(v, dp[size_t(r - 1) * WW + c] + 3);
                    if (c > 0) v = std::min(v, dp[size_t(r - 1) * WW + c - 1] + 4);
                    if (c + 1 < WW) v = std::min(v, dp[size_t(r - 1) * WW + c + 1] + 4); }
            }
            for (int r = WH - 1; r >= 0; --r) for (int c = WW - 1; c >= 0; --c) {
                int& v = dp[size_t(r) * WW + c];
                if (c + 1 < WW) v = std::min(v, dp[size_t(r) * WW + c + 1] + 3);
                if (r + 1 < WH) { v = std::min(v, dp[size_t(r + 1) * WW + c] + 3);
                    if (c + 1 < WW) v = std::min(v, dp[size_t(r + 1) * WW + c + 1] + 4);
                    if (c > 0) v = std::min(v, dp[size_t(r + 1) * WW + c - 1] + 4); }
            }
            for (size_t k = 0; k < wt.size(); ++k) {
                const double ds = dp[k] / 3.0;
                if (ds < kFeatherSamples) wt[k] = float(double(wt[k]) * (ds / kFeatherSamples));
            }
        }
    }
    // (4913) in the full set, a sample whose region byte the change moves takes the new ground whole: the feather
    // towards kept river samples does not hold back a coast that moved (it is not kept itself, see above).
    // (4913 (b)) Not within kFeatherSamples of a natural wonder's kept hex: there the feather wins, so the new coast
    // meets the wonder's ground without a step (live-terrain.md 8.17 (b): up to 4.8 world units on huge's Gibraltar).
    if (classMode) {
        std::vector<int> dw;
        bool anyW = false;
        for (uint8_t v : wonderKept) anyW = anyW || v;
        if (anyW) {
            dw.assign(wonderKept.size(), 1 << 28);
            for (size_t k = 0; k < wonderKept.size(); ++k) if (wonderKept[k]) dw[k] = 0;
            for (int r = 0; r < WH; ++r) for (int c = 0; c < WW; ++c) {
                int& v = dw[size_t(r) * WW + c];
                if (c > 0) v = std::min(v, dw[size_t(r) * WW + c - 1] + 3);
                if (r > 0) { v = std::min(v, dw[size_t(r - 1) * WW + c] + 3);
                    if (c > 0) v = std::min(v, dw[size_t(r - 1) * WW + c - 1] + 4);
                    if (c + 1 < WW) v = std::min(v, dw[size_t(r - 1) * WW + c + 1] + 4); }
            }
            for (int r = WH - 1; r >= 0; --r) for (int c = WW - 1; c >= 0; --c) {
                int& v = dw[size_t(r) * WW + c];
                if (c + 1 < WW) v = std::min(v, dw[size_t(r) * WW + c + 1] + 3);
                if (r + 1 < WH) { v = std::min(v, dw[size_t(r + 1) * WW + c] + 3);
                    if (c + 1 < WW) v = std::min(v, dw[size_t(r + 1) * WW + c + 1] + 4);
                    if (c > 0) v = std::min(v, dw[size_t(r + 1) * WW + c - 1] + 4); }
            }
        }
        for (size_t k = 0; k < wt.size(); ++k)
            if (wtFull[k] >= 1.f && rB[k] != rA[k] && !prot[k] && (dw.empty() || dw[k] / 3.0 >= kFeatherSamples)) wt[k] = 1.f;
    }
    // (the seam plots themselves are no longer excluded: 4902's seam residual was the per-level reset of the index lists,
    // which this port has; 8.3. A window that crosses the seam still waits: this code writes one unwrapped rectangle.)
    // (4913) the land/water step handles a window across the seam (its rectangles are split there)
    if (out.reason == WaitReason::None && w.fr.wrapX && (wx0 < 0 || wx1 > w.sx) && !regionMode) { out.reason = WaitReason::Seam; out.reasonText = "the window crosses the x wrap seam"; }
    if (out.reason != WaitReason::None && opt.diagnosticIgnoreClean && out.reason != WaitReason::Seam &&
        out.reason != WaitReason::TooLarge) {
        out.reasonText = std::string("DIAGNOSTIC, clean rule ignored (would wait: ") + waitReasonName(out.reason) + ", " + out.reasonText + ")";
        out.reason = WaitReason::None;
    }
    if (opt.diagnostics) {
        SampleDiagnostics& g = *opt.diagnostics;
        g.x0 = int(wx0); g.y0 = int(wy0); g.w = WW; g.h = WH;
        g.weightFull = wtFull; g.weight = wt; g.why = why; g.moved = movedDiag; g.regionBefore = rB; g.regionAfter = rA;
        g.riverCentres = useDisc ? riverCentres : bandMids;
        g.riverFactor = rfac;
    }
    if (out.reason != WaitReason::None) { finish(); return Status::Ok(); }

    // (3)-(4) own blend before (the load's set) and after (the new set) over the window; D; the result
    std::vector<uint8_t> useB(w.pl.size(), 0), useA(w.pl.size(), 0);
    for (size_t i = 0; i < d.pl.size(); ++i) useB[i] = d.alive[i];
    for (size_t i = 0; i < w.pl.size(); ++i) useA[i] = w.alive[i];
    std::vector<uint16_t> hB, hA; std::vector<uint8_t> mB, mA;
    if (!regionMode) {
        w.blend(d.flags, useB, int(wx0), int(wy0), int(wx1), int(wy1), hB, mB);   // the old cells decide the old regions
        w.blend(w.flags, useA, int(wx0), int(wy0), int(wx1), int(wy1), hA, mA);
    } else {
        // (4913) the region bytes from the old and the new curves, the blend, then the coast strips of each set
        w.blend(d.flags, useB, int(wx0), int(wy0), int(wx1), int(wy1), hB, mB, &rB);
        w.blend(w.flags, useA, int(wx0), int(wy0), int(wx1), int(wy1), hA, mA, &rA);
        C5R_TRY(windowStrips(w.fr, *d.meshes, *d.stripMats, int(wx0), int(wy0), WW, WH, hB, mB, out.stripTrianglesRefused));
        C5R_TRY(windowStrips(w.fr, classMode ? liveMeshes : *d.meshes, *d.stripMats, int(wx0), int(wy0), WW, WH, hA, mA, out.stripTrianglesRefused));
    }
    WindowRect wr; wr.x0 = int(wx0); wr.y0 = int(wy0); wr.w = WW; wr.h = WH;
    wr.heights.resize(size_t(WW) * WH); wr.materials.resize(size_t(WW) * WH);
    for (int r = 0; r < WH; ++r) for (int c = 0; c < WW; ++c) {
        const size_t k = size_t(r) * WW + c;
        const size_t fk = size_t(wy0 + r) * w.sx + size_t(w.wrapX(int(wx0) + c));
        // D: the samples the own blend changes. They lie in the reach of a removed or added piece, so their w is 1 before
        // the feather; next to kept samples the feather (8.9) decides, and on kept samples w stays 0.
        if ((hA[k] != hB[k] || mA[k] != mB[k]) && !prot[k]) ++out.samplesD;
        const double tapv = curH[fk];
        // (0717) the river band scales a raise (and the material); a lowering is drawn as the generic weight gives it
        const double rf = rfac.empty() ? 1.0 : double(rfac[k]);
        const double we = double(hA[k]) > tapv ? double(wt[k]) * rf : double(wt[k]);
        const double v = std::nearbyint(tapv + we * (double(hA[k]) - tapv));
        wr.heights[k] = uint16_t(std::min(65535.0, std::max(0.0, v)));
        wr.materials[k] = double(wt[k]) * rf >= 0.5 ? mA[k] : curM[fk];
        if (we >= 1.0) ++out.samplesFull; else if (we > 0.0) ++out.samplesRing;
        else if (wr.heights[k] != curH[fk] || wr.materials[k] != curM[fk]) ++out.samplesOutside;
    }
    if (out.samplesOutside != 0)
        return Status::fail(Err::BadLayout, "producer: " + num(int64_t(out.samplesOutside)) + " samples changed outside w > 0 (a defect)");
    for (int r = 0; r < WH; ++r) for (int c = 0; c < WW; ++c) {
        const size_t fk = size_t(wy0 + r) * w.sx + size_t(w.wrapX(int(wx0) + c));
        curH[fk] = wr.heights[size_t(r) * WW + c];
        curM[fk] = wr.materials[size_t(r) * WW + c];
    }
    if (wx0 >= 0 && wx1 <= w.sx) out.window.push_back(std::move(wr));
    else {
        // (4913) split at the seam: the part left of it and the wrapped part
        const int split = wx0 < 0 ? int(-wx0) : int(w.sx - wx0);
        for (int part = 0; part < 2; ++part) {
            WindowRect pr;
            const int c0 = part == 0 ? 0 : split, c1 = part == 0 ? split : WW;
            pr.x0 = w.wrapX(int(wx0) + c0); pr.y0 = int(wy0); pr.w = c1 - c0; pr.h = WH;
            pr.heights.resize(size_t(pr.w) * WH); pr.materials.resize(size_t(pr.w) * WH);
            for (int r = 0; r < WH; ++r) for (int c = c0; c < c1; ++c) {
                pr.heights[size_t(r) * pr.w + size_t(c - c0)] = wr.heights[size_t(r) * WW + c];
                pr.materials[size_t(r) * pr.w + size_t(c - c0)] = wr.materials[size_t(r) * WW + c];
            }
            out.window.push_back(std::move(pr));
        }
    }
    out.live = true;
    w.plots = after;
    if (classMode) {
        w.curves = std::make_shared<const CoastCurves>(std::move(liveCurves));
        w.meshes = std::make_shared<const std::vector<StripMesh>>(std::move(liveMeshes));
    }
    d = std::move(w);
    finish();
    return Status::Ok();
}

}} // namespace c5r::liveterrain
