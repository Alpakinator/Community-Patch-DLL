// render/src/farmfields/layout.cpp - the farm layout's stages (epic 0528, ticket 4852). See c5r/farmfields/layout.h.
// Behaviour from research/05-graphics-composition/farm-fields-layout.md sections 1-8 (own code, D83). The simplex noise
// is the textbook 2D simplex noise with Ken Perlin's published reference permutation (the text names both).
#include "c5r/farmfields/layout.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <utility>

#include "c5r/forest/crtrand.h"

namespace c5r { namespace farmfields {

namespace {

const double kPi = 3.14159265358979323846;

// Ken Perlin's reference permutation (Improved Noise, 2002).
const uint8_t kPerm[256] = {
    151, 160, 137, 91,  90,  15,  131, 13,  201, 95,  96,  53,  194, 233, 7,   225, 140, 36,  103, 30,  69,  142,
    8,   99,  37,  240, 21,  10,  23,  190, 6,   148, 247, 120, 234, 75,  0,   26,  197, 62,  94,  252, 219, 203,
    117, 35,  11,  32,  57,  177, 33,  88,  237, 149, 56,  87,  174, 20,  125, 136, 171, 168, 68,  175, 74,  165,
    71,  134, 139, 48,  27,  166, 77,  146, 158, 231, 83,  111, 229, 122, 60,  211, 133, 230, 220, 105, 92,  41,
    55,  46,  245, 40,  244, 102, 143, 54,  65,  25,  63,  161, 1,   216, 80,  73,  209, 76,  132, 187, 208, 89,
    18,  169, 200, 196, 135, 130, 116, 188, 159, 86,  164, 100, 109, 198, 173, 186, 3,   64,  52,  217, 226, 250,
    124, 123, 5,   202, 38,  147, 118, 126, 255, 82,  85,  212, 207, 206, 59,  227, 47,  16,  58,  17,  182, 189,
    28,  42,  223, 183, 170, 213, 119, 248, 152, 2,   44,  154, 163, 70,  221, 153, 101, 155, 167, 43,  172, 9,
    129, 22,  39,  253, 19,  98,  108, 110, 79,  113, 224, 232, 178, 185, 112, 104, 218, 246, 97,  228, 251, 34,
    242, 193, 238, 210, 144, 12,  191, 179, 162, 241, 81,  51,  145, 235, 249, 14,  239, 107, 49,  192, 214, 31,
    181, 199, 106, 157, 184, 84,  204, 176, 115, 121, 50,  45,  127, 4,   150, 254, 138, 236, 205, 93,  222, 114,
    67,  29,  24,  72,  243, 141, 128, 195, 78,  66,  215, 61,  156, 180};

const int kGrad[12][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}, {1, 0}, {-1, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {0, 1}, {0, -1}};

inline int perm(int i) { return kPerm[i & 255]; }

struct Pt { double x, y; };

double distSqPointSeg(Pt p, Pt a, Pt b)
{
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double l2 = dx * dx + dy * dy;
    double t = l2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2 : 0.0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    const double ex = a.x + t * dx - p.x, ey = a.y + t * dy - p.y;
    return ex * ex + ey * ey;
}

// Segment a-b against c-d: t on a-b, s on c-d (both 0..1); false when parallel or apart.
bool segHit(Pt a, Pt b, Pt c, Pt d, double& t, double& s)
{
    const double rx = b.x - a.x, ry = b.y - a.y, qx = d.x - c.x, qy = d.y - c.y;
    const double den = rx * qy - ry * qx;
    if (std::fabs(den) < 1e-14) return false;
    const double wx = c.x - a.x, wy = c.y - a.y;
    t = (wx * qy - wy * qx) / den;
    s = (wx * ry - wy * rx) / den;
    return t >= 0 && t <= 1 && s >= 0 && s <= 1;
}

// The collinear merge (3.4): appending q, the last point is replaced when it lies within squared distance kMergeTolSq
// of the segment from the point before it to q.
void appendMerged(std::vector<Pt>& v, Pt q)
{
    if (!v.empty() && v.back().x == q.x && v.back().y == q.y) return;
    if (v.size() >= 2 && distSqPointSeg(v.back(), v[v.size() - 2], q) < kMergeTolSq) v.back() = q;
    else v.push_back(q);
}

struct Mark { uint32_t seg; double t; uint32_t vertex; };
struct TLine {
    uint8_t family = 1;
    uint32_t tag = 0;                // the streamline counter (3.4): both halves and their continuations share it
    std::vector<Pt> pts;
    std::vector<Mark> marks;
    std::vector<uint32_t> stamp;    // per segment: the last query that saw it
    uint32_t vStart = 0, vEnd = 0;
};

struct Seed {
    int kind = 0;        // 0 point, 1 offset, 2 continuation
    uint8_t family = 0;  // offset and continuation seeds: the family to trace
    Pt p{0, 0};          // the seed point (tested)
    Pt start{0, 0};      // where the line starts
    double sign = 1.0;   // continuation: the direction (forward +1, backward -1)
    uint32_t tag = 0;    // continuation: the streamline it continues
    uint32_t vertex = 0; // continuation: its dangling end
};

class Layouter {
public:
    Layouter(uint32_t gameSeed, const FieldParams& f, int32_t bx, int32_t by, const double window[4], const double bounds[4],
             const LayoutParams& params)
        : f_(f), rng_(plotSeed(gameSeed ^ 0x5EED0B1Cu, bx, by)), p_(params)
    {
        std::memcpy(w_, window, sizeof w_);
        std::memcpy(b_, bounds, sizeof b_);
        cols_ = (std::max)(1, static_cast<int>(std::ceil((w_[2] - w_[0]) / kCell)));
        rows_ = (std::max)(1, static_cast<int>(std::ceil((w_[3] - w_[1]) / kCell)));
        cells_.assign(static_cast<size_t>(cols_) * rows_, std::vector<uint64_t>());
        tile_ = seedTile(gameSeed);
    }

    void run(BlockGraph& g);

private:
    static constexpr double kCell = 16.0;
    FieldParams f_;
    forest::CrtRand rng_;
    LayoutParams p_;
    double w_[4], b_[4];
    int cols_ = 0, rows_ = 0;
    std::vector<std::vector<uint64_t>> cells_;
    std::vector<double> tile_;
    std::vector<TLine> lines_;
    std::vector<Pt> verts_;
    uint32_t query_ = 0;
    uint32_t tried_ = 0, accepted_ = 0;

    bool inside(Pt q) const
    {
        return q.x > w_[0] && q.x < w_[2] && q.y > w_[1] && q.y < w_[3] && q.x >= b_[0] && q.x <= b_[2] && q.y >= b_[1] && q.y <= b_[3];
    }
    Pt dir(Pt q, uint8_t family, double sign) const
    {
        const double a = fieldAngle(f_, q.x, q.y);
        const double c = std::cos(a), s = std::sin(a);
        // family 1: the field's direction; family 2: it turned by -90 degrees (3.4)
        return family == 1 ? Pt{sign * c, sign * s} : Pt{sign * s, -sign * c};
    }
    void cellRange(double x0, double y0, double x1, double y1, int& c0, int& c1, int& r0, int& r1) const
    {
        c0 = (std::max)(0, static_cast<int>(std::floor((x0 - w_[0]) / kCell)));
        r0 = (std::max)(0, static_cast<int>(std::floor((y0 - w_[1]) / kCell)));
        c1 = (std::min)(cols_ - 1, static_cast<int>(std::floor((x1 - w_[0]) / kCell)));
        r1 = (std::min)(rows_ - 1, static_cast<int>(std::floor((y1 - w_[1]) / kCell)));
    }
    void addSeg(uint32_t li, uint32_t si)
    {
        TLine& L = lines_[li];
        if (L.stamp.size() <= si) L.stamp.resize(si + 1, 0);
        const Pt a = L.pts[si], b = L.pts[si + 1];
        int c0, c1, r0, r1;
        cellRange((std::min)(a.x, b.x), (std::min)(a.y, b.y), (std::max)(a.x, b.x), (std::max)(a.y, b.y), c0, c1, r0, r1);
        for (int r = r0; r <= r1; ++r)
            for (int c = c0; c <= c1; ++c) cells_[static_cast<size_t>(r) * cols_ + c].push_back((static_cast<uint64_t>(li) << 32) | si);
    }
    template <class F> void query(double x0, double y0, double x1, double y1, F fn)
    {
        ++query_;
        int c0, c1, r0, r1;
        cellRange(x0, y0, x1, y1, c0, c1, r0, r1);
        for (int r = r0; r <= r1; ++r)
            for (int c = c0; c <= c1; ++c)
                for (uint64_t k : cells_[static_cast<size_t>(r) * cols_ + c]) {
                    const uint32_t li = static_cast<uint32_t>(k >> 32), si = static_cast<uint32_t>(k & 0xFFFFFFFFu);
                    uint32_t& st = lines_[li].stamp[si];
                    if (st == query_) continue;
                    st = query_;
                    fn(li, si);
                }
    }
    uint32_t vertex(Pt p)
    {
        verts_.push_back(p);
        return static_cast<uint32_t>(verts_.size() - 1);
    }
    // 3.4's separation test: rejected when a segment (of the given family, 0 = any; not of the streamline `skipTag`)
    // lies closer than r.
    bool separated(Pt p, double r, uint8_t family, uint32_t skipTag = 0)
    {
        bool ok = true;
        const double r2 = r * r;
        query(p.x - r, p.y - r, p.x + r, p.y + r, [&](uint32_t li, uint32_t si) {
            if (!ok) return;
            const TLine& L = lines_[li];
            if (family && L.family != family) return;
            if (skipTag && L.tag == skipTag) return;
            if (distSqPointSeg(p, L.pts[si], L.pts[si + 1]) < r2) ok = false;
        });
        return ok;
    }
    // Traces one half line; true when it stopped at the piece length (its end then dangles and may continue).
    bool traceHalf(uint32_t li, uint32_t partner, Pt p0, uint8_t family, double sign);
    void offsetSeeds(uint32_t li, std::deque<Seed>& q);
    void build(BlockGraph& g);
};

struct Hit { double t, s; uint32_t line, seg; Pt p; };

bool Layouter::traceHalf(uint32_t li, uint32_t partner, Pt p0, uint8_t family, double sign)
{
    {
        TLine& L = lines_[li];
        L.family = family;
        L.pts.assign(1, p0);
    }
    Pt p = p0;
    std::vector<Hit> hits;
    double arc = 0;
    bool isNear = false;   // (not `near`: a Windows macro)
    const uint32_t tag = lines_[li].tag;
    // The piece traced since the line's last junction is not in the graph yet (3.4: an edge is made at each junction):
    // its segments join the spatial grid at the next junction or at the end.
    uint32_t committed = 0;   // segments [0, committed) are in the grid
    auto commit = [&]() {
        TLine& L = lines_[li];
        for (; committed + 1 < L.pts.size(); ++committed) addSeg(li, committed);
    };
    for (int k = 0; k < kMaxTracePoints; ++k) {
        if (arc >= p_.contArc) {
            commit();
            lines_[li].vEnd = vertex(p);
            return true;
        }
        // the step (3.4): q1 = p + 0.65 d(p), then three times q = p + 0.65 d(q); the fourth q is the next point
        Pt d = dir(p, family, sign);
        Pt q{p.x + kTraceStep * d.x, p.y + kTraceStep * d.y};
        for (int i = 0; i < 3; ++i) {
            d = dir(q, family, sign);
            q = Pt{p.x + kTraceStep * d.x, p.y + kTraceStep * d.y};
        }
        if (!inside(q)) break;
        // the distance stops (3.4, 0x4072C0): the new point against the lines of the own family with another tag
        double dmin2 = std::numeric_limits<double>::infinity();
        const double zone = p_.nearZone;
        query(q.x - zone, q.y - zone, q.x + zone, q.y + zone, [&](uint32_t m, uint32_t j) {
            const TLine& M = lines_[m];
            if (M.family != family || M.tag == tag) return;
            dmin2 = (std::min)(dmin2, distSqPointSeg(q, M.pts[j], M.pts[j + 1]));
        });
        if (dmin2 < p_.stopDistance * p_.stopDistance) {
            // the step is refused, the trace ends, and the piece since the last junction is dropped
            TLine& L = lines_[li];
            const Mark* last = nullptr;
            for (const Mark& m : L.marks)
                if (!last || m.seg + m.t > last->seg + last->t) last = &m;
            if (last) {
                const Mark m = *last;
                L.pts.resize(static_cast<size_t>(m.seg) + 1);
                L.pts.push_back(verts_[m.vertex]);
                L.vEnd = m.vertex;
                commit();
            } else {
                L.pts.resize(1);   // no junction since the start: nothing of this half is kept
                L.vEnd = L.vStart;
            }
            return false;
        }
        if (dmin2 < zone * zone) isNear = true;
        hits.clear();
        const uint32_t own = static_cast<uint32_t>(lines_[li].pts.size()) - 1;   // index of p
        query((std::min)(p.x, q.x), (std::min)(p.y, q.y), (std::max)(p.x, q.x), (std::max)(p.y, q.y), [&](uint32_t m, uint32_t j) {
            if (m == li && j + 1 >= own) return;   // the own segment ending at p
            const TLine& M = lines_[m];
            double t, s;
            if (!segHit(p, q, M.pts[j], M.pts[j + 1], t, s) || t <= 1e-9) return;
            if (m == partner && j == 0 && s <= 1e-9) return;   // the shared start
            hits.push_back(Hit{t, s, m, j, Pt{p.x + t * (q.x - p.x), p.y + t * (q.y - p.y)}});
        });
        std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
            return a.t != b.t ? a.t < b.t : a.line != b.line ? a.line < b.line : a.seg < b.seg;
        });
        bool crossed = false;
        for (const Hit& h : hits) {
            const uint32_t v = vertex(h.p);
            lines_[h.line].marks.push_back(Mark{h.seg, h.s, v});
            if (isNear || lines_[h.line].family == family) {
                // a line of the own family (or the line itself), or any hit once near: the line ends there (3.4)
                TLine& L = lines_[li];
                L.pts.push_back(h.p);
                L.vEnd = v;
                commit();
                return false;
            }
            lines_[li].marks.push_back(Mark{own, h.t, v});   // a crossing of the other family: it goes on
            crossed = true;
        }
        TLine& L = lines_[li];
        L.pts.push_back(q);
        if (crossed) commit();
        arc += std::sqrt((q.x - p.x) * (q.x - p.x) + (q.y - p.y) * (q.y - p.y));
        p = q;
    }
    commit();
    lines_[li].vEnd = vertex(p);
    return false;
}

void Layouter::offsetSeeds(uint32_t li, std::deque<Seed>& q)
{
    const TLine& L = lines_[li];
    const uint8_t other = L.family == 1 ? 2 : 1;
    double acc = 0;
    for (size_t i = 0; i + 1 < L.pts.size(); ++i) {
        const Pt a = L.pts[i], b = L.pts[i + 1];
        const double dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0) continue;
        acc += len;
        while (acc >= p_.offsetSpacing) {
            acc -= p_.offsetSpacing;
            Seed s;
            s.kind = 1;
            s.family = other;
            s.p = Pt{(a.x + b.x) * 0.5, (a.y + b.y) * 0.5};
            s.start = Pt{s.p.x - dy / len * kOffsetStart, s.p.y + dx / len * kOffsetStart};   // + 0.65 x the unit normal
            q.push_front(s);   // last made, first taken (3.3)
        }
    }
}

void Layouter::run(BlockGraph& g)
{
    // point seeds: every tile point strictly inside the window, each inserted at rand() % k (3.3)
    std::vector<Seed> points;
    const int i0 = static_cast<int>(std::floor(w_[0] / kSeedPeriod)), i1 = static_cast<int>(std::floor(w_[2] / kSeedPeriod));
    const int j0 = static_cast<int>(std::floor(w_[1] / kSeedPeriod)), j1 = static_cast<int>(std::floor(w_[3] / kSeedPeriod));
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i)
            for (int k = 0; k < kSeedTilePoints; ++k) {
                const Pt p{i * kSeedPeriod + tile_[2 * k], j * kSeedPeriod + tile_[2 * k + 1]};
                if (!inside(p)) continue;
                Seed s;
                s.kind = 0;
                s.p = s.start = p;
                if (points.empty()) points.push_back(s);
                else points.insert(points.begin() + (rng_.next() % static_cast<int>(points.size())), s);
            }
    std::deque<Seed> offsets, conts;
    size_t nextPoint = 0;
    uint32_t tag = 0;
    const uint32_t kNoLine = 0xFFFFFFFFu;
    auto continuation = [&](uint32_t li, double sign) {
        const TLine& L = lines_[li];
        Seed c;
        c.kind = 2;
        c.family = L.family;
        c.p = c.start = L.pts.back();
        c.sign = sign;
        c.tag = L.tag;
        c.vertex = L.vEnd;
        conts.push_back(c);
    };
    for (;;) {
        Seed s;
        if (!conts.empty()) {   // continuation seeds first, then offset seeds, then point seeds (3.3)
            s = conts.front();
            conts.pop_front();
        } else if (!offsets.empty()) {
            s = offsets.front();
            offsets.pop_front();
        } else if (nextPoint < points.size()) {
            s = points[nextPoint++];
        } else {
            break;
        }
        ++tried_;
        if (s.kind == 2) {
            // kind 2: against the own family's other streamlines (radius 9.6), then on from the dangling end
            if (!separated(s.p, p_.sepContinuation, s.family, s.tag)) continue;
            ++accepted_;
            const uint32_t li = static_cast<uint32_t>(lines_.size());
            lines_.emplace_back();
            lines_[li].tag = s.tag;
            lines_[li].vStart = s.vertex;
            const bool capped = traceHalf(li, kNoLine, s.start, s.family, s.sign);
            offsetSeeds(li, offsets);
            if (capped) continuation(li, s.sign);
            continue;
        }
        if (!separated(s.p, s.kind == 0 ? p_.sepPoint : p_.sepOffset, s.kind == 0 ? 0 : s.family)) continue;
        uint8_t family = s.family;
        if (s.kind == 0) family = static_cast<uint8_t>(2 - (rng_.next() % 2 != 0 ? 1 : 0));   // 1 or 2, equal odds
        if (!inside(s.start)) continue;
        ++accepted_;
        ++tag;   // the counter is raised for kinds 0 and 1 (3.4)
        const uint32_t fwd = static_cast<uint32_t>(lines_.size());
        lines_.emplace_back();
        lines_.emplace_back();
        const uint32_t v0 = vertex(s.start);
        for (uint32_t k = 0; k < 2; ++k) {
            lines_[fwd + k].vStart = v0;
            lines_[fwd + k].tag = tag;
        }
        const bool capF = traceHalf(fwd, fwd + 1, s.start, family, 1.0);
        const bool capB = traceHalf(fwd + 1, fwd, s.start, family, -1.0);
        offsetSeeds(fwd, offsets);
        offsetSeeds(fwd + 1, offsets);
        if (capF) continuation(fwd, 1.0);
        if (capB) continuation(fwd + 1, -1.0);
    }
    build(g);
}

void Layouter::build(BlockGraph& g)
{
    g.window[0] = w_[0];
    g.window[1] = w_[1];
    g.window[2] = w_[2];
    g.window[3] = w_[3];
    g.lines = static_cast<uint32_t>(lines_.size());
    g.seedsTried = tried_;
    g.seedsAccepted = accepted_;
    for (const Pt& p : verts_) {
        g.vertices.push_back(p.x);
        g.vertices.push_back(p.y);
    }
    // edges: each line cut at its marks (start, crossings, T junctions on it, end)
    for (TLine& L : lines_) {
        if (L.pts.size() < 2) continue;
        std::vector<Mark> ms = L.marks;
        ms.push_back(Mark{0, 0.0, L.vStart});
        ms.push_back(Mark{static_cast<uint32_t>(L.pts.size() - 2), 1.0, L.vEnd});
        std::sort(ms.begin(), ms.end(), [](const Mark& a, const Mark& b) {
            return a.seg + a.t != b.seg + b.t ? a.seg + a.t < b.seg + b.t : a.vertex < b.vertex;
        });
        for (size_t i = 0; i + 1 < ms.size(); ++i) {
            const double pa = ms[i].seg + ms[i].t, pb = ms[i + 1].seg + ms[i + 1].t;
            if (ms[i].vertex == ms[i + 1].vertex) continue;
            std::vector<Pt> pts;
            pts.push_back(verts_[ms[i].vertex]);
            for (size_t k = static_cast<size_t>(std::floor(pa)) + 1; k < L.pts.size() && static_cast<double>(k) < pb - 1e-12; ++k)
                if (static_cast<double>(k) > pa + 1e-12) appendMerged(pts, L.pts[k]);
            appendMerged(pts, verts_[ms[i + 1].vertex]);
            if (pts.size() < 2) continue;
            GraphEdge e;
            e.v0 = ms[i].vertex;
            e.v1 = ms[i + 1].vertex;
            e.family = L.family;
            for (const Pt& p : pts) {
                e.pts.push_back(p.x);
                e.pts.push_back(p.y);
            }
            g.edges.push_back(std::move(e));
        }
    }
    // pruning (3.5): dead ends, repeatedly
    std::vector<uint32_t> deg(verts_.size(), 0);
    for (const GraphEdge& e : g.edges) {
        ++deg[e.v0];
        ++deg[e.v1];
    }
    for (bool again = true; again;) {
        again = false;
        for (GraphEdge& e : g.edges)
            if (e.live && (deg[e.v0] == 1 || deg[e.v1] == 1)) {
                e.live = false;
                --deg[e.v0];
                --deg[e.v1];
                again = true;
            }
    }
    // faces (3.6): at each junction the next edge is the first one clockwise from the edge it came along (a left turn),
    // so a bounded face is walked counter-clockwise; abandoned past kMaxFaceEdges junction edges
    const size_t ne = g.edges.size();
    std::vector<std::vector<uint32_t>> out(verts_.size());   // half-edges leaving each vertex, by angle
    std::vector<double> ang(2 * ne, 0);
    auto dest = [&](uint32_t h) { const GraphEdge& e = g.edges[h >> 1]; return (h & 1) ? e.v0 : e.v1; };
    for (uint32_t e = 0; e < ne; ++e) {
        const GraphEdge& E = g.edges[e];
        if (!E.live) continue;
        const size_t n = E.pts.size() / 2;
        ang[2 * e] = std::atan2(E.pts[3] - E.pts[1], E.pts[2] - E.pts[0]);
        ang[2 * e + 1] = std::atan2(E.pts[2 * n - 3] - E.pts[2 * n - 1], E.pts[2 * n - 4] - E.pts[2 * n - 2]);
        out[E.v0].push_back(2 * e);
        out[E.v1].push_back(2 * e + 1);
    }
    for (auto& o : out)
        std::sort(o.begin(), o.end(), [&](uint32_t a, uint32_t b) { return ang[a] != ang[b] ? ang[a] < ang[b] : a < b; });
    std::vector<uint32_t> posOf(2 * ne, 0);
    for (auto& o : out)
        for (uint32_t i = 0; i < o.size(); ++i) posOf[o[i]] = i;
    std::vector<uint8_t> seen(2 * ne, 0);
    for (uint32_t h0 = 0; h0 < 2 * ne; ++h0) {
        if (!g.edges[h0 >> 1].live || seen[h0]) continue;
        std::vector<uint32_t> cyc;
        uint32_t h = h0;
        int junctionEdges = 0;
        bool ok = true;
        for (int guard = 0; guard < 4096; ++guard) {
            seen[h] = 1;
            cyc.push_back(h);
            const uint32_t v = dest(h);
            const std::vector<uint32_t>& o = out[v];
            if (o.size() >= 3) ++junctionEdges;
            const uint32_t twin = h ^ 1u;
            const uint32_t nx = o[(posOf[twin] + o.size() - 1) % o.size()];
            h = nx;
            if (h == h0) break;
            if (seen[h] || junctionEdges > kMaxFaceEdges) {
                ok = false;
                break;
            }
        }
        if (!ok || h != h0 || junctionEdges > kMaxFaceEdges) continue;
        std::vector<Pt> ring;
        for (uint32_t hh : cyc) {
            const GraphEdge& E = g.edges[hh >> 1];
            const size_t n = E.pts.size() / 2;
            for (size_t k = 0; k + 1 < n; ++k) {   // every point but the last (the next edge starts there)
                const size_t idx = (hh & 1) ? n - 1 - k : k;
                appendMerged(ring, Pt{E.pts[2 * idx], E.pts[2 * idx + 1]});
            }
        }
        // close the ring's merge across the seam
        while (ring.size() >= 3 && distSqPointSeg(ring.back(), ring[ring.size() - 2], ring[0]) < kMergeTolSq) ring.pop_back();
        while (ring.size() >= 3 && distSqPointSeg(ring[0], ring.back(), ring[1]) < kMergeTolSq) ring.erase(ring.begin());
        if (ring.size() < 3) continue;
        double a2 = 0, cx = 0, cy = 0;
        for (size_t i = 0; i < ring.size(); ++i) {
            const Pt p = ring[i], q = ring[(i + 1) % ring.size()];
            const double c = p.x * q.y - q.x * p.y;
            a2 += c;
            cx += (p.x + q.x) * c;
            cy += (p.y + q.y) * c;
        }
        if (!(a2 > 1e-9)) continue;   // the outer cycle (clockwise) and slivers
        GraphFace f;
        f.area = a2 * 0.5;
        f.cx = cx / (3.0 * a2);
        f.cy = cy / (3.0 * a2);
        for (const Pt& p : ring) {
            f.pts.push_back(p.x);
            f.pts.push_back(p.y);
        }
        for (uint32_t hh : cyc) f.edges.push_back(hh >> 1);
        g.faces.push_back(std::move(f));
    }
}

template <class T> bool earClipT(const T* xy, size_t n, std::vector<uint32_t>& tris)
{
    tris.clear();
    if (n < 3) return false;
    double a2 = 0;
    for (size_t i = 0; i < n; ++i) {
        const size_t j = (i + 1) % n;
        a2 += static_cast<double>(xy[2 * i]) * xy[2 * j + 1] - static_cast<double>(xy[2 * j]) * xy[2 * i + 1];
    }
    const double orient = a2 >= 0 ? 1.0 : -1.0;
    std::vector<uint32_t> live(n);
    for (size_t i = 0; i < n; ++i) live[i] = static_cast<uint32_t>(i);
    auto X = [&](uint32_t k) { return static_cast<double>(xy[2 * k]); };
    auto Y = [&](uint32_t k) { return static_cast<double>(xy[2 * k + 1]); };
    auto cross = [&](uint32_t a, uint32_t b, uint32_t c) { return (X(b) - X(a)) * (Y(c) - Y(a)) - (Y(b) - Y(a)) * (X(c) - X(a)); };
    while (live.size() > 3) {
        bool found = false;
        for (size_t ii = 0; ii < live.size(); ++ii) {   // from index 0: the first ear
            const uint32_t i = live[ii], j = live[(ii + 1) % live.size()], k = live[(ii + 2) % live.size()];
            if (cross(i, j, k) * orient <= 0) continue;   // not the polygon's orientation
            bool inside = false;
            for (uint32_t m : live) {
                if (m == i || m == j || m == k) continue;
                const double c1 = cross(i, j, m) * orient, c2 = cross(j, k, m) * orient, c3 = cross(k, i, m) * orient;
                if (c1 >= 0 && c2 >= 0 && c3 >= 0) {
                    inside = true;
                    break;
                }
            }
            if (inside) continue;
            tris.push_back(i);
            tris.push_back(j);
            tris.push_back(k);
            live.erase(live.begin() + static_cast<std::ptrdiff_t>((ii + 1) % live.size()));
            found = true;
            break;
        }
        if (!found) {
            tris.clear();
            return false;
        }
    }
    if (cross(live[0], live[1], live[2]) * orient <= 0) {
        tris.clear();
        return false;
    }
    tris.push_back(live[0]);
    tris.push_back(live[1]);
    tris.push_back(live[2]);
    return true;
}

} // namespace

const uint8_t* perlinPermutation() { return kPerm; }

double simplex2(double xin, double yin)
{
    const double F2 = 0.5 * (std::sqrt(3.0) - 1.0), G2 = (3.0 - std::sqrt(3.0)) / 6.0;
    const double s = (xin + yin) * F2;
    const int i = static_cast<int>(std::floor(xin + s)), j = static_cast<int>(std::floor(yin + s));
    const double t = (i + j) * G2;
    const double x0 = xin - (i - t), y0 = yin - (j - t);
    const int i1 = x0 > y0 ? 1 : 0, j1 = x0 > y0 ? 0 : 1;
    const double x1 = x0 - i1 + G2, y1 = y0 - j1 + G2;
    const double x2 = x0 - 1.0 + 2.0 * G2, y2 = y0 - 1.0 + 2.0 * G2;
    const int ii = i & 255, jj = j & 255;
    const int gi0 = perm(ii + perm(jj)) % 12, gi1 = perm(ii + i1 + perm(jj + j1)) % 12, gi2 = perm(ii + 1 + perm(jj + 1)) % 12;
    double n0 = 0, n1 = 0, n2 = 0;
    double t0 = 0.5 - x0 * x0 - y0 * y0;
    if (t0 > 0) {
        t0 *= t0;
        n0 = t0 * t0 * (kGrad[gi0][0] * x0 + kGrad[gi0][1] * y0);
    }
    double t1 = 0.5 - x1 * x1 - y1 * y1;
    if (t1 > 0) {
        t1 *= t1;
        n1 = t1 * t1 * (kGrad[gi1][0] * x1 + kGrad[gi1][1] * y1);
    }
    double t2 = 0.5 - x2 * x2 - y2 * y2;
    if (t2 > 0) {
        t2 *= t2;
        n2 = t2 * t2 * (kGrad[gi2][0] * x2 + kGrad[gi2][1] * y2);
    }
    return 70.0 * (n0 + n1 + n2);
}

FieldParams fieldParamsFor(uint32_t gameSeed)
{
    forest::CrtRand r(plotSeed(gameSeed ^ 0x0FF5E7u, 0, 0));
    FieldParams f;
    f.ox = (r.next() % 25600) * 0.01;   // 0 .. 256: the noise lattice's period
    f.oy = (r.next() % 25600) * 0.01;
    return f;
}

double fieldAngle(const FieldParams& f, double lx, double ly)
{
    return kBaseAngle + (kPi / 8.0) * simplex2(lx * kNoiseScale1 + f.ox, ly * kNoiseScale1 + f.oy) +
           (kPi / 16.0) * simplex2(lx * kNoiseScale2 + f.ox, ly * kNoiseScale2 + f.oy);
}

void plotCentreLayout(int32_t x, int32_t y, double out[2])
{
    float c[2];
    plotCentre(x, y, c);
    out[0] = c[0];
    out[1] = c[1] / kLayoutYScale;
}

bool plotAt(double lx, double ly, uint32_t mapW, uint32_t mapH, int32_t& px, int32_t& py)
{
    if (!std::isfinite(lx) || !std::isfinite(ly) || !mapW || !mapH) return false;
    const double sy = ly * kLayoutYScale;
    const int r0 = static_cast<int>(std::floor((sy - 128.0) / 64.0 + 0.5));
    double best = std::numeric_limits<double>::infinity();
    bool any = false;
    for (int r = r0 - 1; r <= r0 + 1; ++r) {
        if (r < 0 || static_cast<uint32_t>(r) >= mapH) continue;
        const int xc = static_cast<int>(std::floor((lx - 32.0 - 32.0 * (r & 1)) / 64.0 + 0.5));
        for (int x = xc - 1; x <= xc + 1; ++x) {
            if (x < 0 || static_cast<uint32_t>(x) >= mapW) continue;
            double c[2];
            plotCentreLayout(x, r, c);
            const double d = (c[0] - lx) * (c[0] - lx) + (c[1] - ly) * (c[1] - ly);
            if (d < best) {
                best = d;
                px = x;
                py = r;
                any = true;
            }
        }
    }
    return any;
}

std::vector<double> seedTile(uint32_t gameSeed)
{
    forest::CrtRand r(plotSeed(gameSeed ^ 0x711E5u, 0, 0));
    std::vector<double> t(2 * kSeedTilePoints);
    for (int k = 0; k < kSeedTilePoints; ++k) {
        t[2 * k] = (r.next() % 1000) * 0.064;   // x first, then y (3.3)
        t[2 * k + 1] = (r.next() % 1000) * 0.064;
    }
    return t;
}

bool blockWindow(int32_t bx, int32_t by, const double bounds[4], double out[4])
{
    double lo[2], hi[2];
    plotCentreLayout(kBlockPlots * bx, kBlockPlots * by, lo);
    plotCentreLayout(kBlockPlots * bx + kBlockPlots - 1, kBlockPlots * by + kBlockPlots - 1, hi);
    const double hexX = 32.0, hexY = 37.0;   // a hex's half extent in L (64 wide, 2 x 64 / sqrt 3 / 1.1547 tall)
    out[0] = (std::max)(bounds[0], lo[0] - hexX - kBlockMargin);
    out[1] = (std::max)(bounds[1], lo[1] - hexY - kBlockMargin);
    out[2] = (std::min)(bounds[2], hi[0] + 32.0 + hexX + kBlockMargin);   // odd rows sit 32 to the right
    out[3] = (std::min)(bounds[3], hi[1] + hexY + kBlockMargin);
    return out[0] < out[2] && out[1] < out[3];
}

Status layoutBlock(uint32_t gameSeed, const FieldParams& f, int32_t bx, int32_t by, const double bounds[4], BlockGraph& out,
                   const LayoutParams& params)
{
    out = BlockGraph();
    if (!bounds || !std::isfinite(bounds[0]) || !std::isfinite(bounds[1]) || !std::isfinite(bounds[2]) || !std::isfinite(bounds[3]) ||
        !(bounds[0] < bounds[2] && bounds[1] < bounds[3]))
        return Status::fail(Err::BadLayout, "layoutBlock: the bounds are not finite or empty");
    if (bx < 0 || by < 0) return Status::fail(Err::BadLayout, "layoutBlock: a negative block index");
    double w[4];
    BlockGraph g;
    g.bx = bx;
    g.by = by;
    if (!blockWindow(bx, by, bounds, w)) {   // the block lies outside the world: an empty graph
        out = std::move(g);
        return Status::Ok();
    }
    Layouter l(gameSeed, f, bx, by, w, bounds, params);
    l.run(g);
    out = std::move(g);
    return Status::Ok();
}

bool earClip(const double* xy, size_t n, std::vector<uint32_t>& tris) { return earClipT(xy, n, tris); }
bool earClip(const float* xy, size_t n, std::vector<uint32_t>& tris) { return earClipT(xy, n, tris); }

bool fieldCategory(const FarmPlot& p, uint32_t& category)
{
    if (p.artType < 1 || p.artType > 4) return false;   // the continent nibble outside 1-4: no decal (4.2 step 5)
    if (p.wheat) category = p.state == FarmState::HalfBuilt ? kWheatHalfBuilt : kWheat;
    else if (p.state == FarmState::HalfBuilt) category = kHalfBuilt;
    else category = p.artType == 1 ? kAmerica : p.artType == 2 ? kAsia : p.artType == 3 ? kAfrica : kEurope;
    return true;
}

void fieldUv(double px, double py, double cx, double cy, double a, float& u, float& v)
{
    const double x = (px - cx) / kUvScale + 0.5, y = (py - cy) / kUvScale + 0.5;
    const double c = std::cos(a), s = std::sin(a);
    u = static_cast<float>(x * c - y * s);
    v = static_cast<float>(x * s + y * c);
}

}} // namespace c5r::farmfields
