// render/src/farmfields/farmfields.cpp - the server's own farm fields and farm tracks (epic 0528, ticket 4851). See
// c5r/farmfields/farmfields.h; behaviour from research/05-graphics-composition/terrain-decals.md 1.2, 2.2, 4.2, 8.3, 8.4,
// 10.1, 14.6 and 18 (own code, D83). The layout itself (layoutFarms) is a PLACEHOLDER until 0528 C.
#include "c5r/farmfields/farmfields.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <map>
#include <string>
#include <utility>

#include "c5r/bake/grid.h"
#include "c5r/farmfields/layout.h"
#include "c5r/forest/crtrand.h"

namespace c5r { namespace farmfields {

const char* const kDefaultSpec = "blank_texture.dds";

namespace {

const char* const kCategoryNames[kCategoryCount] = {"Crops_Half_Built", "Crops_Africa", "Crops_America", "Crops_Asia",
                                                    "Crops_Europe",     "Crops_Wheat",  "Crops_Wheat_HB", "Crops_Damage"};
const char kRefPrefix[] = "@farm_types.xml/";

std::string lower(const std::string& s)
{
    std::string r = s;
    for (char& c : r) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return r;
}

bool finiteN(const float* v, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (!std::isfinite(v[i])) return false;
    return true;
}

bool worldOk(const float w[4]) { return w && finiteN(w, 4) && w[0] < w[2] && w[1] < w[3]; }

bool insideWorld(float x, float y, const float w[4]) { return x >= w[0] && x <= w[2] && y >= w[1] && y <= w[3]; }

Status bad(Invariant v, Invariant* which, const std::string& what)
{
    if (which) *which = v;
    return Status::fail(Err::BadLayout, std::string("invariant ") + invariantName(v) + ": " + what);
}

std::string plotText(const LayoutInput& in, uint32_t p)
{
    if (p >= in.plots.size()) return "plot #" + std::to_string(p);
    return "plot (" + std::to_string(in.plots[p].x) + ", " + std::to_string(in.plots[p].y) + ")";
}

const uint64_t kFnvBasis = 0xcbf29ce484222325ull;
uint64_t fnvAdd(uint64_t h, const void* p, size_t n)
{
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) {
        h ^= b[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

uint32_t rotl32(uint32_t v, int r) { return (v << r) | (v >> (32 - r)); }

// cvttss2si: truncation toward zero; NaN or out of range gives INT32_MIN (terrain-decals.md 18.1 / 18.3).
int32_t truncSse(float v)
{
    if (!(v > -2147483648.0f && v < 2147483648.0f)) return (std::numeric_limits<int32_t>::min)();
    return static_cast<int32_t>(v);
}

// 0x53F0C0's cell of a point on one axis (terrain-decals.md 18.1): trunc((p - origin) x inverse), clamped to 0 .. n - 1.
uint32_t axisCellClamped(float p, float origin, float inverse, uint32_t n)
{
    const float t = (p - origin) * inverse;
    const int32_t c = truncSse(t);
    if (c < 0) return 0;
    if (static_cast<uint32_t>(c) >= n) return n - 1;
    return static_cast<uint32_t>(c);
}

// The plain tile square of a rectangle on one axis (floor(min / 64) .. floor(max / 64)), clipped to 0 .. n - 1.
bool tileSpan(float lo, float hi, uint32_t n, uint32_t& a, uint32_t& b)
{
    const double t0 = std::floor(static_cast<double>(lo) / decal::kTileSamples);
    const double t1 = std::floor(static_cast<double>(hi) / decal::kTileSamples);
    if (n == 0 || t1 < 0 || t0 > static_cast<double>(n) - 1 || t1 < t0) return false;
    a = static_cast<uint32_t>((std::max)(0.0, t0));
    b = static_cast<uint32_t>((std::min)(static_cast<double>(n) - 1, t1));
    return true;
}

// The box of n points (x, y at stride floats from p).
void boxOf(const float* p, size_t n, size_t stride, float r[4])
{
    r[0] = r[2] = p[0];
    r[1] = r[3] = p[1];
    for (size_t i = 1; i < n; ++i) {
        const float x = p[i * stride], y = p[i * stride + 1];
        r[0] = (std::min)(r[0], x);
        r[1] = (std::min)(r[1], y);
        r[2] = (std::max)(r[2], x);
        r[3] = (std::max)(r[3], y);
    }
}

bool nameIs(const std::vector<std::string>& names, uint32_t idx, const std::string& wantLower)
{
    return idx < names.size() && lower(names[idx]) == wantLower;
}

// A field's three names, checked against the child they name (kTexture).
bool fieldNamesOk(const LayoutInput& in, const std::vector<std::string>& names, const decal::Decal& d, std::string& why)
{
    uint32_t cat = 0, child = 0, c2 = 0, ch2 = 0;
    std::string attr, attr2;
    if (d.texture[0] >= names.size() || !parseFarmTypesRef(names[d.texture[0]], cat, child, attr) || attr != "albedo")
        return why = "the albedo is not an @farm_types.xml/<category>/<child>/albedo reference", false;
    if (child >= in.types.children(cat))
        return why = "the albedo names child " + std::to_string(child) + " of " + categoryName(cat) + ", which farm_types.xml lacks",
               false;
    if (d.texture[1] >= names.size() || !parseFarmTypesRef(names[d.texture[1]], c2, ch2, attr2) || attr2 != "heightmap" ||
        c2 != cat || ch2 != child)
        return why = "the heightmap is not the heightmap reference of the albedo's child", false;
    if (in.types.hasSpec[cat][child]) {
        if (!nameIs(names, d.texture[2], lower(farmTypesRef(cat, child, "spec"))))
            return why = "the spec is not the spec reference of the albedo's child (which has one)", false;
    } else if (!nameIs(names, d.texture[2], lower(kDefaultSpec))) {
        return why = std::string("the spec is not the default ") + kDefaultSpec + " (the child has no spec attribute)", false;
    }
    return true;
}

bool trackNamesOk(const std::vector<std::string>& names, const decal::Decal& d, std::string& why)
{
    if (d.texture[0] != decal::kNoTexture || d.texture[1] != decal::kNoTexture)
        return why = "a track's albedo and heightmap must be kNoTexture (the EXE's handle 0)", false;
    if (!nameIs(names, d.texture[2], lower(kDefaultSpec)))
        return why = std::string("a track's spec must be ") + kDefaultSpec, false;
    return true;
}

} // namespace

// ---- names ----

const char* categoryName(uint32_t c) { return c < kCategoryCount ? kCategoryNames[c] : nullptr; }

bool categoryOf(const std::string& name, uint32_t& c)
{
    const std::string l = lower(name);
    for (uint32_t k = 0; k < kCategoryCount; ++k)
        if (lower(kCategoryNames[k]) == l) {
            c = k;
            return true;
        }
    return false;
}

std::string farmTypesRef(uint32_t category, uint32_t child, const char* attribute)
{
    if (category >= kCategoryCount || !attribute) return std::string();
    return std::string(kRefPrefix) + kCategoryNames[category] + "/" + std::to_string(child) + "/" + attribute;
}

bool parseFarmTypesRef(const std::string& name, uint32_t& category, uint32_t& child, std::string& attribute)
{
    const size_t np = sizeof(kRefPrefix) - 1;
    if (name.size() <= np || lower(name.substr(0, np)) != kRefPrefix) return false;
    const std::string rest = name.substr(np);
    const size_t a = rest.find('/');
    if (a == std::string::npos || a == 0) return false;
    const size_t b = rest.find('/', a + 1);
    if (b == std::string::npos || b == a + 1 || b + 1 >= rest.size() || rest.find('/', b + 1) != std::string::npos) return false;
    uint32_t cat = 0;
    if (!categoryOf(rest.substr(0, a), cat)) return false;
    const std::string idx = rest.substr(a + 1, b - a - 1);
    if (idx.size() > 10) return false;
    uint64_t v = 0;
    for (char c : idx) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<uint64_t>(c - '0');
    }
    if (v >= 0x80000000ull) return false;
    category = cat;
    child = static_cast<uint32_t>(v);
    attribute = lower(rest.substr(b + 1));
    return true;
}

const char* invariantName(Invariant v)
{
    switch (v) {
    case Invariant::kNone: return "none";
    case Invariant::kInput: return "input";
    case Invariant::kGroup: return "group";
    case Invariant::kOwner: return "owner";
    case Invariant::kCategory: return "category";
    case Invariant::kShape: return "shape";
    case Invariant::kFinite: return "finite";
    case Invariant::kInsideWorld: return "inside-world";
    case Invariant::kEmptyRect: return "empty-rect";
    case Invariant::kTables: return "tables";
    case Invariant::kVertexRun: return "vertex-run";
    case Invariant::kVertexInRect: return "vertex-in-rect";
    case Invariant::kPriority: return "priority";
    case Invariant::kTexture: return "texture";
    case Invariant::kHash: return "hash";
    case Invariant::kListOrder: return "list-order";
    }
    return "?";
}

// ---- seed and centre ----

uint32_t plotSeed(uint32_t gameSeed, int32_t x, int32_t y)
{
    // MurmurHash3's 32-bit block step over (x, y) with the game's seed as the start, then its finaliser
    uint32_t h = gameSeed;
    const uint32_t k[2] = {static_cast<uint32_t>(x), static_cast<uint32_t>(y)};
    for (uint32_t v : k) {
        v *= 0xcc9e2d51u;
        v = rotl32(v, 15);
        v *= 0x1b873593u;
        h ^= v;
        h = rotl32(h, 13);
        h = h * 5u + 0xe6546b64u;
    }
    h ^= 8u;
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

void plotCentre(int32_t x, int32_t y, float out[2])
{
    const int32_t hx = x - (y >> 1);
    out[0] = static_cast<float>(64 * hx + 32 * y + 32);
    out[1] = static_cast<float>(64 * y + 128);
}

// ---- input ----

Status checkInput(const LayoutInput& in)
{
    if (!in.mapW || !in.mapH) return Status::fail(Err::BadLayout, "checkInput: the map has no plots");
    if (!worldOk(in.worldRect)) return Status::fail(Err::BadLayout, "checkInput: the world rectangle is not finite or empty");
    for (size_t i = 0; i < in.plots.size(); ++i) {
        const FarmPlot& p = in.plots[i];
        if (p.x < 0 || p.y < 0 || static_cast<uint32_t>(p.x) >= in.mapW || static_cast<uint32_t>(p.y) >= in.mapH)
            return Status::fail(Err::BadLayout, "checkInput: " + plotText(in, static_cast<uint32_t>(i)) + " lies outside the " +
                                                    std::to_string(in.mapW) + " x " + std::to_string(in.mapH) + " map");
        if (i) {
            const FarmPlot& q = in.plots[i - 1];
            if (!(q.y < p.y || (q.y == p.y && q.x < p.x)))
                return Status::fail(Err::BadLayout, "checkInput: " + plotText(in, static_cast<uint32_t>(i)) +
                                                        " is not after the plot before it (ascending by y then x, each once)");
        }
        const uint8_t s = static_cast<uint8_t>(p.state);
        if (s < 1 || s > 3)
            return Status::fail(Err::BadLayout, "checkInput: " + plotText(in, static_cast<uint32_t>(i)) + " has state " +
                                                    std::to_string(s) + " (1 half-built, 2 built, 3 pillaged)");
        if (p.artType > 4)
            return Status::fail(Err::BadLayout, "checkInput: " + plotText(in, static_cast<uint32_t>(i)) + " has art type " +
                                                    std::to_string(p.artType) + " (0..4)");
    }
    return Status::Ok();
}

// ---- THE LAYOUT -------------------------------------------------------------------------------------------------------
// farm-fields-layout.md sections 1-8 through the stages of layout.cpp; the contract and the rules are in farmfields.h.
namespace {

struct KeptField {
    uint32_t plot = 0;
    uint32_t face = 0;
    double cx = 0, cy = 0;
    uint32_t graph = 0;
};

// A face that may become a field: centred on a farm plot, triangulated, inside the world.
struct Candidate {
    uint32_t plot = 0, graph = 0, face = 0;
    bool primary = false;                    // centred on a farm plot of its own block
    std::vector<uint32_t> tris;              // ear clipping of the face (L)
    const std::vector<double>* pts = nullptr;
    double box[4] = {0, 0, 0, 0};
};

void boxOfPts(const std::vector<double>& p, double b[4])
{
    b[0] = b[2] = p[0];
    b[1] = b[3] = p[1];
    for (size_t k = 1; k < p.size() / 2; ++k) {
        b[0] = (std::min)(b[0], p[2 * k]);
        b[1] = (std::min)(b[1], p[2 * k + 1]);
        b[2] = (std::max)(b[2], p[2 * k]);
        b[3] = (std::max)(b[3], p[2 * k + 1]);
    }
}

// Two triangles overlap by more than eps (a separating axis test on the six edge normals; shared edges do not count).
bool triOverlap(const double* a, const double* b, double eps)
{
    const double* t[2] = {a, b};
    for (int s = 0; s < 2; ++s)
        for (int e = 0; e < 3; ++e) {
            const double* p = t[s];
            const double nx = -(p[2 * ((e + 1) % 3) + 1] - p[2 * e + 1]), ny = p[2 * ((e + 1) % 3)] - p[2 * e];
            const double len = std::sqrt(nx * nx + ny * ny);
            if (len <= 0) continue;
            double lo[2] = {1e300, 1e300}, hi[2] = {-1e300, -1e300};
            for (int q = 0; q < 2; ++q)
                for (int v = 0; v < 3; ++v) {
                    const double d = (t[q][2 * v] * nx + t[q][2 * v + 1] * ny) / len;
                    lo[q] = (std::min)(lo[q], d);
                    hi[q] = (std::max)(hi[q], d);
                }
            if (hi[0] - lo[1] <= eps || hi[1] - lo[0] <= eps) return false;   // separated (or only touching)
        }
    return true;
}

// The area of two counter-clockwise triangles' intersection (Sutherland-Hodgman clipping of one by the other).
double triIntersectionArea(const double* a, const double* b)
{
    std::vector<double> poly(a, a + 6), next;
    for (int e = 0; e < 3 && poly.size() >= 6; ++e) {
        const double ex = b[2 * e], ey = b[2 * e + 1], fx = b[2 * ((e + 1) % 3)], fy = b[2 * ((e + 1) % 3) + 1];
        auto side = [&](double x, double y) { return (fx - ex) * (y - ey) - (fy - ey) * (x - ex); };   // >= 0: inside
        next.clear();
        const size_t n = poly.size() / 2;
        for (size_t i = 0; i < n; ++i) {
            const double px = poly[2 * i], py = poly[2 * i + 1];
            const double qx = poly[2 * ((i + 1) % n)], qy = poly[2 * ((i + 1) % n) + 1];
            const double sp = side(px, py), sq = side(qx, qy);
            if (sp >= 0) {
                next.push_back(px);
                next.push_back(py);
            }
            if ((sp >= 0) != (sq >= 0)) {
                const double t = sp / (sp - sq);
                next.push_back(px + t * (qx - px));
                next.push_back(py + t * (qy - py));
            }
        }
        poly.swap(next);
    }
    double a2 = 0;
    const size_t n = poly.size() / 2;
    for (size_t i = 0; i < n; ++i) a2 += poly[2 * i] * poly[2 * ((i + 1) % n) + 1] - poly[2 * ((i + 1) % n)] * poly[2 * i + 1];
    return a2 > 0 ? a2 * 0.5 : 0.0;
}

// The area two faces share (their ear-clip triangles, all counter-clockwise).
double facesOverlapArea(const Candidate& a, const Candidate& b)
{
    if (a.box[2] <= b.box[0] || b.box[2] <= a.box[0] || a.box[3] <= b.box[1] || b.box[3] <= a.box[1]) return 0;
    const std::vector<double>& pa = *a.pts;
    const std::vector<double>& pb = *b.pts;
    double sum = 0;
    for (size_t i = 0; i + 2 < a.tris.size(); i += 3) {
        const double ta[6] = {pa[2 * a.tris[i]], pa[2 * a.tris[i] + 1], pa[2 * a.tris[i + 1]], pa[2 * a.tris[i + 1] + 1],
                              pa[2 * a.tris[i + 2]], pa[2 * a.tris[i + 2] + 1]};
        for (size_t j = 0; j + 2 < b.tris.size(); j += 3) {
            const double tb[6] = {pb[2 * b.tris[j]], pb[2 * b.tris[j] + 1], pb[2 * b.tris[j + 1]], pb[2 * b.tris[j + 1] + 1],
                                  pb[2 * b.tris[j + 2]], pb[2 * b.tris[j + 2] + 1]};
            if (triOverlap(ta, tb, 0.0)) sum += triIntersectionArea(ta, tb);
        }
    }
    return sum;
}

// A face may share at most this part of its area with the kept faces of other blocks (the seam passes) [I: stock's own
// fields overlap on 1.8-3.1 % of farm land, compare_huge.txt; a small overlap is the price of closing the seam's gaps].
const double kSeamOverlapShare = 0.25;

// The kept faces in 32-L buckets, for the seam passes.
class SeamIndex {
public:
    void add(const std::vector<Candidate>& cs, uint32_t k)
    {
        const Candidate& c = cs[k];
        for (int64_t y = cell(c.box[1]); y <= cell(c.box[3]); ++y)
            for (int64_t x = cell(c.box[0]); x <= cell(c.box[2]); ++x) b_[std::make_pair(y, x)].push_back(k);
    }
    // True when c shares more than kSeamOverlapShare of its area with kept faces of other graphs (the faces of one
    // graph never overlap each other). Each kept face is counted once.
    bool overlaps(const std::vector<Candidate>& cs, const Candidate& c, double area) const
    {
        std::vector<uint32_t> seen;
        double shared = 0;
        for (int64_t y = cell(c.box[1]); y <= cell(c.box[3]); ++y)
            for (int64_t x = cell(c.box[0]); x <= cell(c.box[2]); ++x) {
                auto it = b_.find(std::make_pair(y, x));
                if (it == b_.end()) continue;
                for (uint32_t k : it->second) {
                    const Candidate& o = cs[k];
                    if (o.graph == c.graph || std::find(seen.begin(), seen.end(), k) != seen.end()) continue;
                    seen.push_back(k);
                    shared += facesOverlapArea(c, o);
                    if (shared > kSeamOverlapShare * area) return true;
                }
            }
        return false;
    }

private:
    static int64_t cell(double v) { return static_cast<int64_t>(std::floor(v / 32.0)); }
    std::map<std::pair<int64_t, int64_t>, std::vector<uint32_t>> b_;
};

// A field's margin polygon (each point pushed kMarginPush from the centroid) and its box, for the track mask.
struct Margin {
    std::vector<double> pts;
    double box[4];
    uint32_t plot;
};

bool insidePolygon(const std::vector<double>& p, double x, double y)
{
    bool in = false;
    const size_t n = p.size() / 2;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const double xi = p[2 * i], yi = p[2 * i + 1], xj = p[2 * j], yj = p[2 * j + 1];
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
    }
    return in;
}

// The worker threads of a load's block layout: the hardware's threads less one (the caller's), 1 to 8.
unsigned defaultLayoutThreads()
{
    const unsigned n = std::thread::hardware_concurrency();
    return n <= 1 ? 1u : (std::min)(8u, n - 1);
}

// fn(0 .. n - 1) on up to `threads` threads (the caller's included); each index once; returns when all are done.
template <class F> void parallelFor(size_t n, unsigned threads, F fn)
{
    if (n == 0) return;
    const unsigned t = static_cast<unsigned>((std::min)(static_cast<size_t>((std::max)(1u, threads)), n));
    std::atomic<size_t> next(0);
    auto work = [&]() {
        for (size_t k = next++; k < n; k = next++) fn(k);
    };
    std::vector<std::thread> pool;
    for (unsigned i = 1; i < t; ++i) pool.emplace_back(work);
    work();
    for (std::thread& th : pool) th.join();
}

const double kTrackCellW = 32.0;                // 2 cells per plot in x (64 L)
const double kTrackCellH = 64.0 / kLayoutYScale / 2.0;   // and per row (55.43 L)

} // namespace

Status layoutFarms(const LayoutInput& in, FarmLayout& out, BlockCache* cache)
{
    out = FarmLayout();
    C5R_TRY(checkInput(in));
    FarmLayout l;
    const FieldParams fp = fieldParamsFor(in.gameSeed);
    const double bounds[4] = {in.worldRect[0], in.worldRect[1] / kLayoutYScale, in.worldRect[2], in.worldRect[3] / kLayoutYScale};
    if (cache && (!cache->valid || cache->gameSeed != in.gameSeed || std::memcmp(cache->bounds, bounds, sizeof bounds) != 0)) {
        cache->graphs.clear();
        cache->gameSeed = in.gameSeed;
        std::memcpy(cache->bounds, bounds, sizeof bounds);
        cache->valid = true;
    }
    const float* w = in.worldRect;
    std::vector<int32_t> farmAt(static_cast<size_t>(in.mapW) * in.mapH, -1);
    std::map<std::pair<int32_t, int32_t>, std::vector<uint32_t>> blocks;   // (by, bx) -> farm plots, ascending
    for (uint32_t i = 0; i < in.plots.size(); ++i) {
        const FarmPlot& p = in.plots[i];
        farmAt[static_cast<size_t>(p.y) * in.mapW + p.x] = static_cast<int32_t>(i);
        blocks[std::make_pair(p.y / kBlockPlots, p.x / kBlockPlots)].push_back(i);
    }
    std::vector<uint32_t> groupOf(in.plots.size(), 0);
    std::vector<std::vector<KeptField>> perPlot(in.plots.size());
    // the block graphs: from the cache, the missing ones laid out in parallel (each depends on its block alone)
    std::vector<const BlockGraph*> graphs;
    std::vector<BlockGraph> local;   // without a cache
    {
        std::vector<std::pair<int32_t, int32_t>> missing;
        for (const auto& b : blocks)
            if (!cache || cache->graphs.find(b.first) == cache->graphs.end()) missing.push_back(b.first);
        std::vector<BlockGraph> made(missing.size());
        std::vector<Status> st(missing.size());
        const unsigned threads = cache && cache->threads ? cache->threads : defaultLayoutThreads();
        parallelFor(missing.size(), threads, [&](size_t k) {
            st[k] = layoutBlock(in.gameSeed, fp, missing[k].second, missing[k].first, bounds, made[k]);
        });
        for (const Status& s : st) C5R_TRY(s);
        if (cache) {
            for (size_t k = 0; k < missing.size(); ++k) cache->graphs[missing[k]] = std::move(made[k]);
            cache->misses += missing.size();
            cache->hits += blocks.size() - missing.size();
            for (const auto& b : blocks) graphs.push_back(&cache->graphs[b.first]);
        } else {
            local = std::move(made);   // every block is missing without a cache, in the blocks' order
            for (const BlockGraph& g : local) graphs.push_back(&g);
        }
    }
    std::vector<Candidate> cands;
    for (const auto& b : blocks) {
        const uint32_t g = static_cast<uint32_t>(l.groups.size());
        FarmGroup grp;
        grp.plots = b.second;
        for (uint32_t p : grp.plots) groupOf[p] = g;
        l.groups.push_back(std::move(grp));
        const BlockGraph& bg = *graphs[g];
        for (uint32_t fi = 0; fi < bg.faces.size(); ++fi) {
            const GraphFace& face = bg.faces[fi];
            int32_t px = 0, py = 0;
            if (!plotAt(face.cx, face.cy, in.mapW, in.mapH, px, py)) continue;   // off the map: dropped (4.1)
            const int32_t idx = farmAt[static_cast<size_t>(py) * in.mapW + px];
            if (idx < 0) continue;                                                // not on a farm plot: dropped (4.1)
            uint32_t cat = 0;
            if (!fieldCategory(in.plots[static_cast<size_t>(idx)], cat)) continue;   // a continent outside 1-4: no decal
            Candidate c;
            if (!earClip(face.pts.data(), face.pts.size() / 2, c.tris)) continue;   // no ear: no decal (4.2 step 2)
            bool inWorld = true;
            for (size_t k = 0; k < face.pts.size() / 2 && inWorld; ++k) {
                const double sx = face.pts[2 * k], sy = face.pts[2 * k + 1] * kLayoutYScale;
                inWorld = sx >= w[0] && sx <= w[2] && sy >= w[1] && sy <= w[3];
            }
            if (!inWorld) continue;
            c.plot = static_cast<uint32_t>(idx);
            c.graph = g;
            c.face = fi;
            c.primary = py / kBlockPlots == b.first.first && px / kBlockPlots == b.first.second;
            boxOfPts(face.pts, c.box);
            c.pts = &face.pts;
            cands.push_back(std::move(c));
        }
    }
    // the block seams (4852, farm-fields-layout.md 13): the graphs of two blocks do not share lines, so along a border
    // a face of one block can overlap a face of the other, and a point can be left to faces whose centroids lie on the
    // other side (each dropped by its own block). Two passes, in a fixed order (blocks by y, then x; faces in graph
    // order): (1) a block's own faces (centroid on its farm plots), each dropped when it overlaps a kept face of an
    // earlier block; (2) the faces of a block centred on another block's farm plot, each kept when it overlaps no kept
    // face (filling the gap; it then belongs to the plot under its centroid). The result depends on the two graphs and
    // the farms near the border only.
    SeamIndex kept;
    std::vector<uint8_t> keep(cands.size(), 0);
    for (int pass = 0; pass < 2; ++pass)
        for (size_t k = 0; k < cands.size(); ++k) {
            const Candidate& c = cands[k];
            if (c.primary != (pass == 0)) continue;
            if (kept.overlaps(cands, c, graphs[c.graph]->faces[c.face].area)) continue;
            keep[k] = 1;
            kept.add(cands, static_cast<uint32_t>(k));
        }
    std::vector<std::vector<uint8_t>> edgeBorders(graphs.size());   // per block graph: the edge borders a kept field
    for (size_t gi = 0; gi < graphs.size(); ++gi) edgeBorders[gi].assign(graphs[gi]->edges.size(), 0);
    for (size_t k = 0; k < cands.size(); ++k) {
        if (!keep[k]) continue;
        const Candidate& c = cands[k];
        const GraphFace& face = graphs[c.graph]->faces[c.face];
        perPlot[c.plot].push_back(KeptField{c.plot, c.face, face.cx, face.cy, c.graph});
        for (uint32_t e : face.edges) edgeBorders[c.graph][e] = 1;
    }
    // the fields, per plot in centroid order, each with its own stream (turn, then variant; pillaged: a damage copy)
    std::vector<Margin> margins;
    for (uint32_t i = 0; i < in.plots.size(); ++i) {
        std::vector<KeptField>& kf = perPlot[i];
        std::sort(kf.begin(), kf.end(), [](const KeptField& a, const KeptField& b) { return a.cy != b.cy ? a.cy < b.cy : a.cx < b.cx; });
        const FarmPlot& p = in.plots[i];
        const uint32_t g = groupOf[i];
        uint32_t cat = 0;
        fieldCategory(p, cat);
        const uint32_t n = in.types.children(cat), nd = in.types.children(kDamage);
        for (uint32_t ord = 0; ord < kf.size(); ++ord) {
            const GraphFace& face = graphs[kf[ord].graph]->faces[kf[ord].face];
            forest::CrtRand rng(plotSeed(plotSeed(in.gameSeed, p.x, p.y), static_cast<int32_t>(ord), 0x4852));
            double a = -fieldAngle(fp, face.cx, face.cy);   // a = -atan2 of the field vector at the centroid (4.2 step 3)
            if (rng.next() % 3 == 0) a += 3.14159265358979323846 / 2.0;
            if (!n) continue;   // a category with no variants: no decal, no draw
            FieldPolygon f;
            f.plot = i;
            f.group = g;
            f.category = cat;
            f.child = static_cast<uint32_t>(rng.next()) % n;
            const size_t nc = face.pts.size() / 2;
            for (size_t k = 0; k < nc; ++k) {
                const double lx = face.pts[2 * k], ly = face.pts[2 * k + 1];
                f.corners.push_back(static_cast<float>(lx));
                f.corners.push_back((std::min)(w[3], (std::max)(w[1], static_cast<float>(ly * kLayoutYScale))));
                float u = 0, v = 0;
                fieldUv(lx, ly, face.cx, face.cy, a, u, v);
                f.uv.push_back(u);
                f.uv.push_back(v);
            }
            if (p.state == FarmState::Pillaged && nd) {
                FieldPolygon dmg = f;
                dmg.category = kDamage;
                dmg.child = static_cast<uint32_t>(rng.next()) % nd;
                l.fields.push_back(std::move(f));
                l.fields.push_back(std::move(dmg));
            } else {
                l.fields.push_back(std::move(f));
            }
            Margin m;
            m.plot = i;
            for (size_t k = 0; k < nc; ++k) {
                double dx = face.pts[2 * k] - face.cx, dy = face.pts[2 * k + 1] - face.cy;
                const double len = std::sqrt(dx * dx + dy * dy);
                if (len > 0) {
                    dx *= (len + kMarginPush) / len;
                    dy *= (len + kMarginPush) / len;
                }
                m.pts.push_back(face.cx + dx);
                m.pts.push_back(face.cy + dy);
            }
            m.box[0] = m.box[2] = m.pts[0];
            m.box[1] = m.box[3] = m.pts[1];
            for (size_t k = 1; k < nc; ++k) {
                m.box[0] = (std::min)(m.box[0], m.pts[2 * k]);
                m.box[1] = (std::min)(m.box[1], m.pts[2 * k + 1]);
                m.box[2] = (std::max)(m.box[2], m.pts[2 * k]);
                m.box[3] = (std::max)(m.box[3], m.pts[2 * k + 1]);
            }
            margins.push_back(std::move(m));
        }
    }
    // the margin mask (bit 0x10): a bucket grid over the margins' boxes
    const double mcell = 32.0;
    std::map<std::pair<int64_t, int64_t>, std::vector<uint32_t>> buckets;
    for (uint32_t k = 0; k < margins.size(); ++k) {
        const Margin& m = margins[k];
        for (int64_t by = static_cast<int64_t>(std::floor(m.box[1] / mcell)); by <= static_cast<int64_t>(std::floor(m.box[3] / mcell)); ++by)
            for (int64_t bx = static_cast<int64_t>(std::floor(m.box[0] / mcell)); bx <= static_cast<int64_t>(std::floor(m.box[2] / mcell)); ++bx)
                buckets[std::make_pair(by, bx)].push_back(k);
    }
    auto marginAt = [&](double x, double y) -> int64_t {
        auto it = buckets.find(std::make_pair(static_cast<int64_t>(std::floor(y / mcell)), static_cast<int64_t>(std::floor(x / mcell))));
        if (it == buckets.end()) return -1;
        for (uint32_t k : it->second) {
            const Margin& m = margins[k];
            if (x >= m.box[0] && x <= m.box[2] && y >= m.box[1] && y <= m.box[3] && insidePolygon(m.pts, x, y)) return k;
        }
        return -1;
    };
    // the tracks (5.2): per cell, a strip 0.5 wide along each qualifying edge, u 0 / 1 across, v + 1.3 per point
    struct CellTracks {
        std::vector<float> xyuv;
        int64_t firstMargin = -1;
    };
    std::map<std::pair<int64_t, int64_t>, CellTracks> cells;
    for (size_t gi = 0; gi < graphs.size(); ++gi) {
        const BlockGraph& bg = *graphs[gi];
        for (size_t e = 0; e < bg.edges.size(); ++e) {
            const GraphEdge& E = bg.edges[e];
            if (!E.live || !edgeBorders[gi][e]) continue;   // edges that border no field are deleted (3.5)
            const size_t n = E.pts.size() / 2;
            if (n < 2) continue;
            int64_t first = -1;
            bool all = true;
            for (size_t k = 0; k < n && all; ++k) {
                const int64_t m = marginAt(E.pts[2 * k], E.pts[2 * k + 1]);
                if (m < 0) all = false;
                else if (k == 0) first = m;
            }
            if (!all) continue;
            // the vertices: two per point at +- 0.25 n (ends: the end segment's normal; inside: the normalised sum of the
            // adjacent unit directions, turned by 90 degrees)
            std::vector<double> nx(n), ny(n);
            for (size_t k = 0; k < n; ++k) {
                double dx = 0, dy = 0;
                for (int side = -1; side <= 1; side += 2) {
                    const size_t a = side < 0 ? k - 1 : k, b = side < 0 ? k : k + 1;
                    if ((side < 0 && k == 0) || (side > 0 && k + 1 >= n)) continue;
                    const double sx = E.pts[2 * b] - E.pts[2 * a], sy = E.pts[2 * b + 1] - E.pts[2 * a + 1];
                    const double len = std::sqrt(sx * sx + sy * sy);
                    if (len > 0) {
                        dx += sx / len;
                        dy += sy / len;
                    }
                }
                const double len = std::sqrt(dx * dx + dy * dy);
                nx[k] = len > 0 ? -dy / len : 0;
                ny[k] = len > 0 ? dx / len : 0;
            }
            std::vector<float> strip;
            bool ok = true;
            auto vert = [&](size_t k, int side, std::vector<float>& outv) {
                const double lx = E.pts[2 * k] + side * kTrackHalfWidth * nx[k], ly = E.pts[2 * k + 1] + side * kTrackHalfWidth * ny[k];
                const double sx = lx, sy = ly * kLayoutYScale;
                if (!(sx >= w[0] && sx <= w[2] && sy >= w[1] && sy <= w[3])) ok = false;
                outv.push_back(static_cast<float>(sx));
                outv.push_back(static_cast<float>(sy));
                outv.push_back(side > 0 ? 0.0f : 1.0f);
                outv.push_back(static_cast<float>(kTrackVStep * static_cast<double>(k)));
            };
            for (size_t k = 0; k + 1 < n; ++k) {   // two triangles per segment
                vert(k, 1, strip);
                vert(k, -1, strip);
                vert(k + 1, -1, strip);
                vert(k, 1, strip);
                vert(k + 1, -1, strip);
                vert(k + 1, 1, strip);
            }
            if (!ok) continue;   // a strip past the world's edge [I]
            const std::pair<int64_t, int64_t> key(static_cast<int64_t>(std::floor(E.pts[1] / kTrackCellH)),
                                                  static_cast<int64_t>(std::floor(E.pts[0] / kTrackCellW)));
            CellTracks& c = cells[key];
            if (c.xyuv.size() + strip.size() > static_cast<size_t>(kMaxDecalVertices) * 4) continue;   // AddDecal's limit
            if (c.firstMargin < 0) c.firstMargin = first;
            c.xyuv.insert(c.xyuv.end(), strip.begin(), strip.end());
        }
    }
    for (auto& kv : cells) {
        if (kv.second.xyuv.empty()) continue;
        const double ccx = (static_cast<double>(kv.first.second) + 0.5) * kTrackCellW;
        const double ccy = (static_cast<double>(kv.first.first) + 0.5) * kTrackCellH;
        int32_t px = 0, py = 0;
        int64_t owner = -1;
        if (plotAt(ccx, ccy, in.mapW, in.mapH, px, py)) {
            const int32_t idx = farmAt[static_cast<size_t>(py) * in.mapW + px];
            if (idx >= 0 && !perPlot[static_cast<size_t>(idx)].empty()) owner = idx;
        }
        if (owner < 0) owner = margins[static_cast<size_t>(kv.second.firstMargin)].plot;
        TrackStrip t;
        t.plot = static_cast<uint32_t>(owner);
        t.group = groupOf[static_cast<size_t>(owner)];
        t.xyuv = std::move(kv.second.xyuv);
        l.tracks.push_back(std::move(t));
    }
    out = std::move(l);
    return Status::Ok();
}
// ---- end of THE LAYOUT -------------------------------------------------------------------------------------------------

// ---- the layout's invariants ----

Status checkLayout(const LayoutInput& in, const FarmLayout& layout, Invariant* which)
{
    if (which) *which = Invariant::kNone;
    {
        const Status s = checkInput(in);
        if (!s.ok()) return bad(Invariant::kInput, which, s.msg);
    }
    const uint32_t np = static_cast<uint32_t>(in.plots.size());
    const uint32_t kNoGroup = 0xFFFFFFFFu;
    std::vector<uint32_t> groupOf(np, kNoGroup);
    for (uint32_t g = 0; g < layout.groups.size(); ++g) {
        const std::vector<uint32_t>& ps = layout.groups[g].plots;
        if (ps.empty()) return bad(Invariant::kGroup, which, "group " + std::to_string(g) + " is empty");
        for (size_t k = 0; k < ps.size(); ++k) {
            if (ps[k] >= np)
                return bad(Invariant::kGroup, which, "group " + std::to_string(g) + " names plot #" + std::to_string(ps[k]) +
                                                         ", outside the input's " + std::to_string(np));
            if (k && ps[k] <= ps[k - 1])
                return bad(Invariant::kGroup, which, "group " + std::to_string(g) + "'s plots are not ascending");
            if (groupOf[ps[k]] != kNoGroup)
                return bad(Invariant::kGroup, which, plotText(in, ps[k]) + " is in groups " + std::to_string(groupOf[ps[k]]) +
                                                         " and " + std::to_string(g));
            groupOf[ps[k]] = g;
        }
    }
    const float* w = in.worldRect;
    auto owner = [&](const char* kind, size_t idx, uint32_t plot, uint32_t group) -> Status {
        if (plot >= np)
            return bad(Invariant::kOwner, which, std::string(kind) + " " + std::to_string(idx) + " names plot #" +
                                                     std::to_string(plot) + ", outside the input's " + std::to_string(np));
        if (group >= layout.groups.size())
            return bad(Invariant::kOwner, which, std::string(kind) + " " + std::to_string(idx) + " names group " +
                                                     std::to_string(group) + ", outside the layout's " +
                                                     std::to_string(layout.groups.size()));
        if (groupOf[plot] != group)
            return bad(Invariant::kOwner, which, std::string(kind) + " " + std::to_string(idx) + "'s " + plotText(in, plot) +
                                                     " is not in its group " + std::to_string(group));
        return Status::Ok();
    };
    auto points = [&](const char* kind, size_t idx, const float* p, size_t n, size_t stride, size_t floats) -> Status {
        if (!finiteN(p, floats))
            return bad(Invariant::kFinite, which, std::string(kind) + " " + std::to_string(idx) + " has a value not finite");
        for (size_t k = 0; k < n; ++k)
            if (!insideWorld(p[k * stride], p[k * stride + 1], w))
                return bad(Invariant::kInsideWorld, which, std::string(kind) + " " + std::to_string(idx) + "'s vertex " +
                                                               std::to_string(k) + " lies outside the world rectangle");
        float r[4];
        boxOf(p, n, stride, r);
        if (!(r[0] < r[2] && r[1] < r[3]))
            return bad(Invariant::kEmptyRect, which, std::string(kind) + " " + std::to_string(idx) + " has an empty box");
        return Status::Ok();
    };
    for (size_t i = 0; i < layout.fields.size(); ++i) {
        const FieldPolygon& f = layout.fields[i];
        C5R_TRY(owner("field", i, f.plot, f.group));
        if (f.category >= kCategoryCount)
            return bad(Invariant::kCategory, which, "field " + std::to_string(i) + " has category " + std::to_string(f.category));
        if (f.child >= in.types.children(f.category))
            return bad(Invariant::kCategory, which, "field " + std::to_string(i) + " names child " + std::to_string(f.child) + " of " +
                                                        categoryName(f.category) + ", which has " +
                                                        std::to_string(in.types.children(f.category)));
        const size_t nc = f.corners.size() / 2;
        if (f.corners.size() % 2 || nc < 3 || f.uv.size() != f.corners.size() || 3 * (nc - 2) > kMaxDecalVertices)
            return bad(Invariant::kShape, which, "field " + std::to_string(i) + " has " + std::to_string(f.corners.size()) +
                                                     " corner floats and " + std::to_string(f.uv.size()) + " uv floats");
        if (!finiteN(f.uv.data(), f.uv.size()))
            return bad(Invariant::kFinite, which, "field " + std::to_string(i) + " has a uv not finite");
        C5R_TRY(points("field", i, f.corners.data(), nc, 2, f.corners.size()));
        std::vector<uint32_t> tris;
        if (!earClip(f.corners.data(), nc, tris))
            return bad(Invariant::kShape, which, "field " + std::to_string(i) + " does not triangulate (no ear: not a simple polygon)");
    }
    for (size_t i = 0; i < layout.tracks.size(); ++i) {
        const TrackStrip& t = layout.tracks[i];
        C5R_TRY(owner("track", i, t.plot, t.group));
        const size_t nv = t.xyuv.size() / 4;
        if (t.xyuv.empty() || t.xyuv.size() % 12 || nv > kMaxDecalVertices)
            return bad(Invariant::kShape, which, "track " + std::to_string(i) + " has " + std::to_string(t.xyuv.size()) +
                                                     " floats (4 per vertex, 3 vertices per triangle)");
        C5R_TRY(points("track", i, t.xyuv.data(), nv, 4, t.xyuv.size()));
    }
    return Status::Ok();
}

// ---- the records ----

uint64_t contentHash(const decal::Decal& d, const std::vector<std::string>& names, const std::vector<decal::DecalVertex>& vertices)
{
    uint64_t h = kFnvBasis;
    h = fnvAdd(h, d.rect, sizeof(d.rect));
    h = fnvAdd(h, &d.priority, sizeof(d.priority));
    h = fnvAdd(h, &d.pass, sizeof(d.pass));
    for (int k = 0; k < 3; ++k) {
        if (d.texture[k] < names.size())
            h = fnvAdd(h, names[d.texture[k]].c_str(), names[d.texture[k]].size() + 1);
        else
            h = fnvAdd(h, "\xFF", 1);
    }
    if (static_cast<uint64_t>(d.firstVertex) + d.vertexCount <= vertices.size() && d.vertexCount)
        h = fnvAdd(h, vertices.data() + d.firstVertex, static_cast<size_t>(d.vertexCount) * sizeof(decal::DecalVertex));
    return h;
}

Status makeDecals(const LayoutInput& in, const FarmLayout& layout, FarmDecals& out)
{
    out = FarmDecals();
    C5R_TRY(checkLayout(in, layout));
    uint64_t total = 0;
    for (const FieldPolygon& f : layout.fields) total += 3 * (f.corners.size() / 2 - 2);
    for (const TrackStrip& t : layout.tracks) total += t.xyuv.size() / 4;
    if (total > 0xFFFFFFFFull) return Status::fail(Err::BadLayout, "makeDecals: more than 2^32 - 1 vertices");
    FarmDecals d;
    d.vertices.reserve(static_cast<size_t>(total));
    std::map<std::string, uint32_t> idx;
    auto name = [&](const std::string& s) -> uint32_t {
        auto it = idx.find(s);
        if (it != idx.end()) return it->second;
        const uint32_t k = static_cast<uint32_t>(d.textures.size());
        d.textures.push_back(s);
        idx.emplace(s, k);
        return k;
    };
    // (p - min) x (1 / extent) in single precision, then binary16 round to nearest even (terrain-decals.md 1.2)
    auto push = [&](const float r[4], float x, float y, float u, float v) {
        const float ix = 1.0f / (r[2] - r[0]), iy = 1.0f / (r[3] - r[1]);
        d.vertices.push_back(decal::DecalVertex{bake::floatToHalf((x - r[0]) * ix), bake::floatToHalf((y - r[1]) * iy),
                                                bake::floatToHalf(u), bake::floatToHalf(v)});
    };
    auto finish = [&](decal::Decal& dc, uint32_t owner, uint8_t track) {
        dc.vertexCount = static_cast<uint32_t>(d.vertices.size()) - dc.firstVertex;
        dc.listOrder = static_cast<uint32_t>(d.decals.size());
        dc.hash = contentHash(dc, d.textures, d.vertices);
        d.decals.push_back(dc);
        d.owner.push_back(owner);
        d.track.push_back(track);
    };
    for (const FieldPolygon& f : layout.fields) {
        decal::Decal dc;
        const size_t nc = f.corners.size() / 2;
        boxOf(f.corners.data(), nc, 2, dc.rect);
        dc.priority = kFieldPriority;
        dc.pass = kFarmPass;
        dc.texture[0] = name(farmTypesRef(f.category, f.child, "albedo"));
        dc.texture[1] = name(farmTypesRef(f.category, f.child, "heightmap"));
        dc.texture[2] = name(in.types.hasSpec[f.category][f.child] ? farmTypesRef(f.category, f.child, "spec") : kDefaultSpec);
        dc.firstVertex = static_cast<uint32_t>(d.vertices.size());
        const float* c = f.corners.data();
        const float* t = f.uv.data();
        // the farm manager's ear clipping (4.2 step 2; checked by checkLayout): a convex field is the fan (c0, ck, ck+1)
        std::vector<uint32_t> tris;
        earClip(c, nc, tris);
        for (uint32_t k : tris) push(dc.rect, c[2 * k], c[2 * k + 1], t[2 * k], t[2 * k + 1]);
        finish(dc, f.plot, 0);
    }
    for (const TrackStrip& t : layout.tracks) {
        decal::Decal dc;
        const size_t nv = t.xyuv.size() / 4;
        boxOf(t.xyuv.data(), nv, 4, dc.rect);
        dc.priority = kTrackPriority;
        dc.pass = kFarmPass;
        dc.texture[0] = decal::kNoTexture;
        dc.texture[1] = decal::kNoTexture;
        dc.texture[2] = name(kDefaultSpec);
        dc.firstVertex = static_cast<uint32_t>(d.vertices.size());
        for (size_t k = 0; k < nv; ++k) push(dc.rect, t.xyuv[4 * k], t.xyuv[4 * k + 1], t.xyuv[4 * k + 2], t.xyuv[4 * k + 3]);
        finish(dc, t.plot, 1);
    }
    out = std::move(d);
    return Status::Ok();
}

Status checkDecals(const LayoutInput& in, const FarmDecals& d, Invariant* which)
{
    if (which) *which = Invariant::kNone;
    {
        const Status s = checkInput(in);
        if (!s.ok()) return bad(Invariant::kInput, which, s.msg);
    }
    const size_t n = d.decals.size();
    if (d.owner.size() != n || d.track.size() != n)
        return bad(Invariant::kTables, which, std::to_string(n) + " decals, " + std::to_string(d.owner.size()) + " owners, " +
                                                  std::to_string(d.track.size()) + " track flags");
    const float* w = in.worldRect;
    for (size_t i = 0; i < n; ++i) {
        const decal::Decal& dc = d.decals[i];
        const std::string at = "decal " + std::to_string(i);
        if (!dc.vertexCount || dc.vertexCount % 3 || dc.vertexCount > kMaxDecalVertices ||
            static_cast<uint64_t>(dc.firstVertex) + dc.vertexCount > d.vertices.size())
            return bad(Invariant::kVertexRun, which, at + ": vertices " + std::to_string(dc.firstVertex) + " + " +
                                                         std::to_string(dc.vertexCount) + " of " + std::to_string(d.vertices.size()));
        if (!finiteN(dc.rect, 4)) return bad(Invariant::kFinite, which, at + ": its rectangle is not finite");
        if (!(dc.rect[0] < dc.rect[2] && dc.rect[1] < dc.rect[3])) return bad(Invariant::kEmptyRect, which, at + ": empty rectangle");
        if (dc.rect[0] < w[0] || dc.rect[1] < w[1] || dc.rect[2] > w[2] || dc.rect[3] > w[3])
            return bad(Invariant::kInsideWorld, which, at + ": its rectangle is not inside the world rectangle");
        const bool track = d.track[i] != 0;
        if (dc.pass != kFarmPass || dc.priority != (track ? kTrackPriority : kFieldPriority))
            return bad(Invariant::kPriority, which, at + " (" + (track ? "track" : "field") + "): priority " +
                                                        std::to_string(dc.priority) + ", pass " + std::to_string(dc.pass));
        std::string why;
        if (!(track ? trackNamesOk(d.textures, dc, why) : fieldNamesOk(in, d.textures, dc, why)))
            return bad(Invariant::kTexture, which, at + ": " + why);
        for (uint32_t k = 0; k < dc.vertexCount; ++k) {
            const decal::DecalVertex& v = d.vertices[dc.firstVertex + k];
            const float px = bake::halfToFloat(v.px), py = bake::halfToFloat(v.py);
            if (!(px >= 0.0f && px <= 1.0f && py >= 0.0f && py <= 1.0f))
                return bad(Invariant::kVertexInRect, which, at + ": vertex " + std::to_string(k) + " at (" + std::to_string(px) +
                                                                ", " + std::to_string(py) + ") of its rectangle");
        }
        if (d.owner[i] >= in.plots.size())
            return bad(Invariant::kOwner, which, at + ": owner #" + std::to_string(d.owner[i]) + " is not a farm plot of the input (" +
                                                     std::to_string(in.plots.size()) + ")");
        if (dc.hash != contentHash(dc, d.textures, d.vertices))
            return bad(Invariant::kHash, which, at + ": its hash is not its content hash");
        if (dc.listOrder != i)
            return bad(Invariant::kListOrder, which, at + ": list order " + std::to_string(dc.listOrder));
    }
    return Status::Ok();
}

Status produce(const LayoutInput& in, FarmLayout& layout, FarmDecals& out, Invariant* which, BlockCache* cache)
{
    layout = FarmLayout();
    out = FarmDecals();
    if (which) *which = Invariant::kNone;
    FarmLayout l;
    Status s = layoutFarms(in, l, cache);
    if (!s.ok()) {
        if (which) *which = Invariant::kInput;
        return Status::fail(s.code, "produce: the layout: " + s.msg);
    }
    s = checkLayout(in, l, which);
    if (!s.ok()) return Status::fail(s.code, "produce: the layout's check: " + s.msg);
    FarmDecals d;
    s = makeDecals(in, l, d);
    if (!s.ok()) {
        if (which && *which == Invariant::kNone) *which = Invariant::kShape;
        return Status::fail(s.code, "produce: the records: " + s.msg);
    }
    s = checkDecals(in, d, which);
    if (!s.ok()) return Status::fail(s.code, "produce: the records' check: " + s.msg);
    layout = std::move(l);
    out = std::move(d);
    return Status::Ok();
}

// ---- the tap's set ----

TapClass tapClass(const decal::DecalSet& set, size_t i)
{
    if (i >= set.decals.size()) return TapClass::kOther;
    const decal::Decal& d = set.decals[i];
    if (d.pass != kFarmPass) return TapClass::kOther;
    if (d.priority == kTrackPriority && d.texture[0] == decal::kNoTexture && d.texture[1] == decal::kNoTexture)
        return TapClass::kTrack;
    if (d.priority == kFieldPriority && d.texture[0] < set.textures.size()) {
        const std::string& s = set.textures[d.texture[0]];
        const size_t np = sizeof(kRefPrefix) - 1;
        if (s.size() > np && lower(s.substr(0, np)) == kRefPrefix) return TapClass::kField;
    }
    return TapClass::kOther;
}

TapFarmCount tapFarmCount(const decal::DecalSet& set)
{
    TapFarmCount c;
    for (size_t i = 0; i < set.decals.size(); ++i) {
        const TapClass k = tapClass(set, i);
        if (k == TapClass::kField) ++c.fields;
        else if (k == TapClass::kTrack) ++c.tracks;
    }
    return c;
}

Status composeFarmSet(const decal::DecalSet& tap, bool removeTapFarm, const FarmDecals& farm,
                      const std::vector<decal::TileOrigin>* marks, decal::DecalSet& out, ComposeStats* stats)
{
    out = decal::DecalSet();
    if (stats) *stats = ComposeStats();
    if (!worldOk(tap.worldRect)) return Status::fail(Err::BadLayout, "composeFarmSet: the tap's world rectangle is not finite or empty");
    std::vector<uint8_t> drop(tap.decals.size(), 0);
    size_t tapFarm = 0;
    for (size_t i = 0; i < tap.decals.size(); ++i)
        if (tapClass(tap, i) != TapClass::kOther) {
            drop[i] = 1;
            ++tapFarm;
        }
    if (tapFarm && !removeTapFarm)
        return Status::fail(Err::Unsupported, "composeFarmSet: the tap's set holds " + std::to_string(tapFarm) +
                                                  " farm records (the EXE's farm builder ran): the server's would draw twice");
    const size_t nf = farm.decals.size();
    if (farm.owner.size() != nf || farm.track.size() != nf)
        return Status::fail(Err::BadLayout, "composeFarmSet: the farm decals' tables disagree in length");
    const float* w = tap.worldRect;
    uint64_t verts = static_cast<uint64_t>(tap.vertices.size()) + farm.vertices.size();
    for (size_t k = 0; k < nf; ++k) {
        const decal::Decal& d = farm.decals[k];
        const std::string at = "composeFarmSet: farm decal " + std::to_string(k);
        if (!d.vertexCount || d.vertexCount % 3 || d.vertexCount > kMaxDecalVertices ||
            static_cast<uint64_t>(d.firstVertex) + d.vertexCount > farm.vertices.size())
            return Status::fail(Err::BadLayout, at + " has a vertex run outside its vertices");
        for (int t = 0; t < 3; ++t)
            if (d.texture[t] != decal::kNoTexture && d.texture[t] >= farm.textures.size())
                return Status::fail(Err::BadLayout, at + " names texture " + std::to_string(d.texture[t]) + " of " +
                                                        std::to_string(farm.textures.size()));
        const float* r = d.rect;
        if (!finiteN(r, 4) || !(r[0] < r[2] && r[1] < r[3]) || r[0] < w[0] || r[1] < w[1] || r[2] > w[2] || r[3] > w[3])
            return Status::fail(Err::BadLayout, at + " has a rectangle not finite, empty or outside the tap's world");
    }
    if (verts > 0xFFFFFFFFull) return Status::fail(Err::BadLayout, "composeFarmSet: more than 2^32 - 1 vertices");

    decal::DecalSet m;
    m.generation = tap.generation;
    m.loadSeq = tap.loadSeq;
    std::memcpy(m.worldRect, tap.worldRect, sizeof m.worldRect);
    m.textures = tap.textures;
    m.vertices.reserve(static_cast<size_t>(verts));
    m.vertices.assign(tap.vertices.begin(), tap.vertices.end());
    m.areaTilesX = tap.areaTilesX;
    m.areaTilesY = tap.areaTilesY;
    m.areaChanged = tap.areaChanged;
    ComposeStats st;
    st.tapDecals = static_cast<uint32_t>(tap.decals.size());
    st.tapRemoved = static_cast<uint32_t>(tapFarm);

    const bool walk = decal::hasWalk(tap);
    std::vector<uint32_t> keptOld;   // the tap index of every kept decal, in order
    keptOld.reserve(tap.decals.size() - tapFarm);
    uint64_t order64 = 0;
    for (size_t i = 0; i < tap.decals.size(); ++i) {
        order64 = (std::max)(order64, static_cast<uint64_t>(tap.decals[i].listOrder) + 1);
        if (drop[i]) continue;
        keptOld.push_back(static_cast<uint32_t>(i));
        m.decals.push_back(tap.decals[i]);
    }
    if (order64 + nf > 0xFFFFFFFFull)
        return Status::fail(Err::BadLayout, "composeFarmSet: the tap's list order leaves no room for the farm decals");
    const size_t nk = keptOld.size();

    // the walk: the kept decals' cells and points, their ranks closed up per cell and in the large list (18.1, 18.2)
    uint32_t keptLarge = 0;
    if (walk) {
        m.walk.origin[0] = tap.walk.origin[0];
        m.walk.origin[1] = tap.walk.origin[1];
        m.walk.inverse[0] = tap.walk.inverse[0];
        m.walk.inverse[1] = tap.walk.inverse[1];
        m.walk.cols = tap.walk.cols;
        m.walk.rows = tap.walk.rows;
        m.walk.cell.resize(nk);
        m.walk.rank.resize(nk);
        m.walk.point.resize(2 * nk);
        struct Item { uint32_t cell, rank, idx; };
        std::vector<Item> items(nk);
        const uint64_t cellCount = static_cast<uint64_t>(m.walk.cols) * m.walk.rows;
        for (size_t j = 0; j < nk; ++j) {
            const uint32_t o = keptOld[j];
            if (tap.walk.cell[o] != decal::kWalkLarge && tap.walk.cell[o] >= cellCount)
                return Status::fail(Err::BadLayout, "composeFarmSet: the tap's walk puts decal " + std::to_string(o) + " in cell " +
                                                        std::to_string(tap.walk.cell[o]) + ", outside its " +
                                                        std::to_string(cellCount) + " cells (an inconsistent walk block)");
            m.walk.cell[j] = tap.walk.cell[o];
            m.walk.point[2 * j] = tap.walk.point[2 * o];
            m.walk.point[2 * j + 1] = tap.walk.point[2 * o + 1];
            items[j] = Item{tap.walk.cell[o], tap.walk.rank[o], static_cast<uint32_t>(j)};
        }
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
            return a.cell != b.cell ? a.cell < b.cell : a.rank != b.rank ? a.rank < b.rank : a.idx < b.idx;
        });
        for (size_t j = 0; j < items.size(); ++j) {
            const bool first = j == 0 || items[j].cell != items[j - 1].cell;
            m.walk.rank[items[j].idx] = first ? 0 : m.walk.rank[items[j - 1].idx] + 1;
            if (items[j].cell == decal::kWalkLarge) ++keptLarge;
        }
    }

    const uint32_t cells = walk ? m.walk.cols * m.walk.rows : 0;
    std::vector<uint32_t> inCell(cells, 0);
    std::vector<uint32_t> seq;   // per farm decal: its place among the farm decals of its cell (1-based), 0 large
    const bool area = tap.areaTilesX && tap.areaTilesY &&
                      tap.areaChanged.size() == static_cast<size_t>(tap.areaTilesX) * tap.areaTilesY;
    auto setBit = [&](uint32_t x, uint32_t y) {
        uint8_t& b = m.areaChanged[static_cast<size_t>(y) * m.areaTilesX + x];
        if (!b) {
            b = 1;
            ++st.areaBitsSet;
        }
    };
    std::vector<uint32_t> nameMap(farm.textures.size(), decal::kNoTexture);
    for (size_t t = 0; t < farm.textures.size(); ++t) {
        for (size_t k = 0; k < m.textures.size(); ++k)
            if (m.textures[k] == farm.textures[t]) {
                nameMap[t] = static_cast<uint32_t>(k);
                break;
            }
        if (nameMap[t] == decal::kNoTexture) {
            nameMap[t] = static_cast<uint32_t>(m.textures.size());
            m.textures.push_back(farm.textures[t]);
            ++st.namesAdded;
        }
    }
    const uint32_t vbase = static_cast<uint32_t>(m.vertices.size());
    m.vertices.insert(m.vertices.end(), farm.vertices.begin(), farm.vertices.end());
    uint32_t order = static_cast<uint32_t>(order64);
    for (size_t k = 0; k < nf; ++k) {
        decal::Decal d = farm.decals[k];
        for (int t = 0; t < 3; ++t)
            if (d.texture[t] != decal::kNoTexture) d.texture[t] = nameMap[d.texture[t]];
        d.firstVertex += vbase;
        d.listOrder = order++;
        m.decals.push_back(d);
        ++st.farmDecals;
        st.farmVertices += d.vertexCount;
        if (walk) {
            // AddDecal [C 18.1]: small when neither side exceeds 256; the point is the rectangle's centre
            const float px = (d.rect[0] + d.rect[2]) * 0.5f, py = (d.rect[1] + d.rect[3]) * 0.5f;
            m.walk.point.push_back(px);
            m.walk.point.push_back(py);
            if (d.rect[2] - d.rect[0] > decal::kLargeDecal || d.rect[3] - d.rect[1] > decal::kLargeDecal) {
                m.walk.cell.push_back(decal::kWalkLarge);
                m.walk.rank.push_back(keptLarge + st.walkLarge);
                ++st.walkLarge;
                seq.push_back(0);
            } else {
                const uint32_t cx = axisCellClamped(px, m.walk.origin[0], m.walk.inverse[0], m.walk.cols);
                const uint32_t cy = axisCellClamped(py, m.walk.origin[1], m.walk.inverse[1], m.walk.rows);
                const uint32_t c = cy * m.walk.cols + cx;
                m.walk.cell.push_back(c);
                m.walk.rank.push_back(0);   // set below
                seq.push_back(++inCell[c]);
            }
        }
        if (area) {
            uint32_t x0, x1, y0, y1;
            if (tileSpan(d.rect[0], d.rect[2], m.areaTilesX, x0, x1) && tileSpan(d.rect[1], d.rect[3], m.areaTilesY, y0, y1))
                for (uint32_t y = y0; y <= y1; ++y)
                    for (uint32_t x = x0; x <= x1; ++x) setBit(x, y);
        }
    }
    if (walk) {
        for (size_t j = 0; j < nk; ++j)
            if (m.walk.cell[j] != decal::kWalkLarge) m.walk.rank[j] += inCell[m.walk.cell[j]];
        for (size_t k = 0; k < seq.size(); ++k) {
            const size_t j = nk + k;
            if (m.walk.cell[j] != decal::kWalkLarge) m.walk.rank[j] = inCell[m.walk.cell[j]] - seq[k];
        }
    }
    if (area && marks)
        for (const decal::TileOrigin& t : *marks)
            if (t.x >= 0 && t.y >= 0 && static_cast<uint32_t>(t.x) < m.areaTilesX && static_cast<uint32_t>(t.y) < m.areaTilesY)
                setBit(static_cast<uint32_t>(t.x), static_cast<uint32_t>(t.y));
    decal::buildIndex(m);
    if (walk && !decal::hasWalk(m))
        return Status::fail(Err::BadLayout, "composeFarmSet: the tap's walk did not survive (an inconsistent walk block)");
    st.walk = walk;
    st.area = area;
    out = std::move(m);
    if (stats) *stats = st;
    return Status::Ok();
}

Status farmChange(const FarmDecals& before, const FarmDecals& after, const float worldRect[4], FarmChange& out)
{
    out = FarmChange();
    if (!worldOk(worldRect)) return Status::fail(Err::BadLayout, "farmChange: the world rectangle is not finite or empty");
    for (const FarmDecals* s : {&before, &after})
        for (size_t i = 0; i < s->decals.size(); ++i)
            if (!finiteN(s->decals[i].rect, 4))
                return Status::fail(Err::BadLayout, "farmChange: decal " + std::to_string(i) + " has a rectangle not finite");
    // the two multisets by hash: walk both sorted lists, an equal hash pairs one decal of each side
    typedef std::pair<uint64_t, uint32_t> HI;
    std::vector<HI> a, b;
    for (uint32_t i = 0; i < before.decals.size(); ++i) a.push_back(HI(before.decals[i].hash, i));
    for (uint32_t i = 0; i < after.decals.size(); ++i) b.push_back(HI(after.decals[i].hash, i));
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    FarmChange ch;
    std::vector<const decal::Decal*> changed;
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        if (j == b.size() || (i < a.size() && a[i].first < b[j].first)) {
            changed.push_back(&before.decals[a[i].second]);
            ++ch.removed;
            ++i;
        } else if (i == a.size() || b[j].first < a[i].first) {
            changed.push_back(&after.decals[b[j].second]);
            ++ch.added;
            ++j;
        } else {
            ++i;
            ++j;
        }
    }
    const double tx0 = std::floor(static_cast<double>(worldRect[0]) / decal::kTileSamples);
    const double ty0 = std::floor(static_cast<double>(worldRect[1]) / decal::kTileSamples);
    const double tx1 = std::floor(static_cast<double>(worldRect[2]) / decal::kTileSamples);
    const double ty1 = std::floor(static_cast<double>(worldRect[3]) / decal::kTileSamples);
    for (const decal::Decal* d : changed) {
        decal::DecalRect r;
        std::memcpy(r.v, d->rect, sizeof r.v);
        ch.rects.push_back(r);
        const double a0 = (std::max)(tx0, std::floor(static_cast<double>(d->rect[0]) / decal::kTileSamples));
        const double a1 = (std::min)(tx1, std::floor(static_cast<double>(d->rect[2]) / decal::kTileSamples));
        const double b0 = (std::max)(ty0, std::floor(static_cast<double>(d->rect[1]) / decal::kTileSamples));
        const double b1 = (std::min)(ty1, std::floor(static_cast<double>(d->rect[3]) / decal::kTileSamples));
        for (double y = b0; y <= b1; ++y)
            for (double x = a0; x <= a1; ++x) ch.areaTiles.push_back(decal::TileOrigin{static_cast<int>(x), static_cast<int>(y)});
    }
    std::sort(ch.areaTiles.begin(), ch.areaTiles.end(), [](const decal::TileOrigin& p, const decal::TileOrigin& q) {
        return p.y != q.y ? p.y < q.y : p.x < q.x;
    });
    ch.areaTiles.erase(std::unique(ch.areaTiles.begin(), ch.areaTiles.end(),
                                   [](const decal::TileOrigin& p, const decal::TileOrigin& q) { return p.x == q.x && p.y == q.y; }),
                       ch.areaTiles.end());
    ch.bakeTiles = decal::tilesForRects(ch.rects.data(), ch.rects.size(), worldRect);
    out = std::move(ch);
    return Status::Ok();
}

// ---- the inputs from the server's tables ----

FarmTypes farmTypesFromAttributes(const std::vector<std::pair<std::string, std::string>>& attributes)
{
    FarmTypes t;
    for (const auto& kv : attributes) {
        const std::string& k = kv.first;   // <category>/<child>/<attribute>
        const size_t a = k.find('/');
        const size_t b = a == std::string::npos ? a : k.find('/', a + 1);
        if (a == std::string::npos || b == std::string::npos || b == a + 1 || b - a - 1 > 9) continue;
        uint32_t cat = 0;
        if (!categoryOf(k.substr(0, a), cat)) continue;
        uint32_t child = 0;
        bool num = true;
        for (size_t i = a + 1; i < b; ++i) {
            if (k[i] < '0' || k[i] > '9') num = false;
            else child = child * 10 + static_cast<uint32_t>(k[i] - '0');
        }
        if (!num || child >= 4096) continue;   // farm_types.xml has a few dozen children; a larger index is no child
        std::vector<uint8_t>& v = t.hasSpec[cat];
        if (v.size() <= child) v.resize(child + 1, 0);
        if (lower(k.substr(b + 1)) == "spec" && !kv.second.empty()) v[child] = 1;
    }
    return t;
}

bool farmPlotOf(int32_t x, int32_t y, int farmId, int16_t improvement, int8_t improvementState, uint8_t flags, uint8_t fow,
                uint8_t artType, bool revealedOnly, FarmPlot& out, int16_t resource, int wheatResourceId)
{
    if (farmId < 0 || improvement != farmId) return false;
    if (revealedOnly && !(fow & 1)) return false;
    FarmPlot p;
    p.x = x;
    p.y = y;
    if ((flags & 2) || improvementState == 3) p.state = FarmState::Pillaged;
    else if (improvementState == 1) p.state = FarmState::HalfBuilt;
    else p.state = FarmState::Built;
    p.artType = artType <= 4 ? artType : 0;
    p.wheat = wheatResourceId >= 0 && resource == wheatResourceId;
    out = p;
    return true;
}

bool sameInput(const LayoutInput& a, const LayoutInput& b)
{
    if (a.gameSeed != b.gameSeed || a.mapW != b.mapW || a.mapH != b.mapH ||
        std::memcmp(a.worldRect, b.worldRect, sizeof a.worldRect) != 0 || a.plots.size() != b.plots.size())
        return false;
    for (uint32_t c = 0; c < kCategoryCount; ++c)
        if (a.types.hasSpec[c] != b.types.hasSpec[c]) return false;
    for (size_t i = 0; i < a.plots.size(); ++i) {
        const FarmPlot& p = a.plots[i];
        const FarmPlot& q = b.plots[i];
        if (p.x != q.x || p.y != q.y || p.state != q.state || p.artType != q.artType || p.wheat != q.wheat) return false;
    }
    return true;
}

// ---- the producer's state ----

void Producer::newLoad()
{
    input_ = LayoutInput();
    haveInput_ = false;
    layout_ = FarmLayout();
    decals_ = FarmDecals();
    marks_.clear();
    pendingRects_.clear();
    pendingArea_.clear();
    cache_.reset();
    ++version_;
}

Status Producer::update(const LayoutInput& in, bool& changed)
{
    changed = false;
    if (haveInput_ && sameInput(in, input_)) return Status::Ok();
    FarmLayout l;
    FarmDecals d;
    if (!cache_) cache_ = std::make_shared<BlockCache>();
    const Status s = produce(in, l, d, nullptr, cache_.get());
    if (!s.ok()) {
        ++failures_;
        return s;
    }
    return accept(in, std::move(l), std::move(d), changed);
}

Status Producer::accept(const LayoutInput& in, FarmLayout&& l, FarmDecals&& d, bool& changed)
{
    changed = false;
    FarmChange ch;
    // the world of the new input (checked by produce); the previous decals lie inside the previous world, which is the
    // same within a load (newLoad between loads)
    if (haveInput_ && sameInput(in, input_)) return Status::Ok();
    C5R_TRY(farmChange(decals_, d, in.worldRect, ch));
    ++productions_;
    input_ = in;
    haveInput_ = true;
    layout_ = std::move(l);
    decals_ = std::move(d);
    if (ch.added || ch.removed) {
        changed = true;
        ++version_;
        pendingRects_.insert(pendingRects_.end(), ch.rects.begin(), ch.rects.end());
        pendingArea_.insert(pendingArea_.end(), ch.areaTiles.begin(), ch.areaTiles.end());
        marks_.insert(marks_.end(), ch.areaTiles.begin(), ch.areaTiles.end());
        auto byYX = [](const decal::TileOrigin& p, const decal::TileOrigin& q) { return p.y != q.y ? p.y < q.y : p.x < q.x; };
        auto same = [](const decal::TileOrigin& p, const decal::TileOrigin& q) { return p.x == q.x && p.y == q.y; };
        std::sort(marks_.begin(), marks_.end(), byYX);
        marks_.erase(std::unique(marks_.begin(), marks_.end(), same), marks_.end());
    }
    return Status::Ok();
}

bool Producer::takeChange(FarmChange& out)
{
    out = FarmChange();
    if (pendingRects_.empty() && pendingArea_.empty()) return false;
    FarmChange ch;
    ch.rects.swap(pendingRects_);
    ch.areaTiles.swap(pendingArea_);
    auto byYX = [](const decal::TileOrigin& p, const decal::TileOrigin& q) { return p.y != q.y ? p.y < q.y : p.x < q.x; };
    auto same = [](const decal::TileOrigin& p, const decal::TileOrigin& q) { return p.x == q.x && p.y == q.y; };
    std::sort(ch.areaTiles.begin(), ch.areaTiles.end(), byYX);
    ch.areaTiles.erase(std::unique(ch.areaTiles.begin(), ch.areaTiles.end(), same), ch.areaTiles.end());
    if (haveInput_) ch.bakeTiles = decal::tilesForRects(ch.rects.data(), ch.rects.size(), input_.worldRect);
    out = std::move(ch);
    return true;
}

Status Producer::compose(const std::shared_ptr<const decal::DecalSet>& tap, bool removeTapFarm,
                         std::shared_ptr<const decal::DecalSet>& out, ComposeStats* stats)
{
    out = tap;
    if (stats) *stats = ComposeStats();
    if (!tap) return Status::fail(Err::BadLayout, "Producer::compose: no tap set");
    if (composed_ && composedTap_ == tap && composedVersion_ == version_ && composedRemove_ == removeTapFarm) {
        out = composed_;
        return Status::Ok();
    }
    std::shared_ptr<decal::DecalSet> m = std::make_shared<decal::DecalSet>();
    const Status s = composeFarmSet(*tap, removeTapFarm, decals_, &marks_, *m, stats);
    if (!s.ok()) return s;
    composedTap_ = tap;
    composed_ = m;
    composedVersion_ = version_;
    composedRemove_ = removeTapFarm;
    out = composed_;
    return Status::Ok();
}

// ---- the span text ----

std::string spanText(const LayoutInput& in, int farmId, int wheatId)
{
    std::string s;
    char b[256];
    std::snprintf(b, sizeof b, "fv1 seed=%u farm=%d wheat=%d map=%u,%u world=%a,%a,%a,%a farms=", in.gameSeed, farmId, wheatId, in.mapW, in.mapH,
                  static_cast<double>(in.worldRect[0]), static_cast<double>(in.worldRect[1]), static_cast<double>(in.worldRect[2]),
                  static_cast<double>(in.worldRect[3]));
    s = b;
    for (size_t i = 0; i < in.plots.size(); ++i) {
        const FarmPlot& p = in.plots[i];
        std::snprintf(b, sizeof b, "%s%d.%d.%u.%u.%u", i ? "," : "", p.x, p.y, static_cast<unsigned>(p.state), static_cast<unsigned>(p.artType),
                      p.wheat ? 1u : 0u);
        s += b;
    }
    return s;
}

Status parseSpanText(const std::string& text, LayoutInput& out, int& farmId, int& wheatId)
{
    auto bad = [](const std::string& w) { return Status::fail(Err::BadLayout, "parseSpanText: " + w); };
    if (text.compare(0, 2, "fv") != 0) return bad("not the producer's span text");
    if (text.compare(0, 4, "fv1 ") != 0) return Status::fail(Err::BadVersion, "parseSpanText: form '" + text.substr(0, 4) + "', not fv1");
    // fields by name, in the fixed order
    const char* p = text.c_str() + 4;
    LayoutInput in;
    int fid = 0, wid = 0;
    unsigned mw = 0, mh = 0;
    double w[4] = {0, 0, 0, 0};
    int used = 0;
    if (std::sscanf(p, "seed=%u farm=%d wheat=%d map=%u,%u world=%la,%la,%la,%la farms=%n", &in.gameSeed, &fid, &wid, &mw, &mh, &w[0], &w[1],
                    &w[2], &w[3], &used) != 9 ||
        used == 0)
        return bad("the header fields (seed, farm, wheat, map, world, farms) are not all there");
    in.mapW = mw;
    in.mapH = mh;
    for (int k = 0; k < 4; ++k) in.worldRect[k] = static_cast<float>(w[k]);
    p += used;
    while (*p) {
        int x = 0, y = 0, n = 0;
        unsigned st = 0, art = 0, wh = 0;
        if (std::sscanf(p, "%d.%d.%u.%u.%u%n", &x, &y, &st, &art, &wh, &n) != 5 || n == 0) return bad("a farm entry is malformed");
        if (st < 1 || st > 3 || art > 4 || wh > 1) return bad("a farm entry has a value out of range");
        FarmPlot fpl;
        fpl.x = x;
        fpl.y = y;
        fpl.state = static_cast<FarmState>(st);
        fpl.artType = static_cast<uint8_t>(art);
        fpl.wheat = wh != 0;
        in.plots.push_back(fpl);
        p += n;
        if (*p == ',') {
            ++p;
            if (!*p) return bad("the farm list ends with ','");
        } else if (*p) {
            return bad("a farm entry is followed by '" + std::string(1, *p) + "'");
        }
    }
    const Status s = checkInput(in);
    if (!s.ok()) return bad(s.msg);
    out = std::move(in);
    farmId = fid;
    wheatId = wid;
    return Status::Ok();
}

}} // namespace c5r::farmfields
