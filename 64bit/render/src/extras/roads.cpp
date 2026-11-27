// roads.cpp - FixRoadLayout's road and railroad generator (epic 0454 X3, ticket 4933). Interface and frames:
// c5r/extras/roads.h; design: docs/extras/roads.md. Written 2026-11-19 for this repository (GPL-3.0-or-later).
#include "c5r/extras/roads.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <unordered_map>
#include "c5r/bake/grid.h"   // floatToHalf

namespace c5r {
namespace extras {
namespace roads {

namespace {

const float kRowRise = 64.0f * kIsoY;         // a row's height (iso)
// direction offsets (iso): 0 E, 1 NE, 2 NW, 3 W, 4 SW, 5 SE
const float kOffX[kDirs] = {64.0f, 32.0f, -32.0f, -64.0f, -32.0f, 32.0f};
const float kOffY[kDirs] = {0.0f, kRowRise, kRowRise, 0.0f, -kRowRise, -kRowRise};

const uint64_t kFnvBasis = 1469598103934665603ull;
const uint64_t kFnvPrime = 1099511628211ull;
uint64_t fnvAdd(uint64_t h, const void* p, size_t n)
{
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) {
        h ^= b[i];
        h *= kFnvPrime;
    }
    return h;
}

// a fixed 32-bit mix of a plot (and a salt): the layout's only source of variety, the same on every load
uint32_t mix(uint32_t a, uint32_t b, uint32_t salt)
{
    uint32_t h = 0x9E3779B9u ^ salt;
    h ^= a + 0x7F4A7C15u + (h << 6) + (h >> 2);
    h ^= b + 0x165667B1u + (h << 6) + (h >> 2);
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

bool finite4(const float* r)
{
    return std::isfinite(r[0]) && std::isfinite(r[1]) && std::isfinite(r[2]) && std::isfinite(r[3]);
}

struct Pt {
    float x, y;
};
Pt operator+(Pt a, Pt b) { return Pt{a.x + b.x, a.y + b.y}; }
Pt operator-(Pt a, Pt b) { return Pt{a.x - b.x, a.y - b.y}; }
Pt operator*(Pt a, float s) { return Pt{a.x * s, a.y * s}; }
float len(Pt a) { return std::sqrt(a.x * a.x + a.y * a.y); }
Pt unit(Pt a)
{
    const float l = len(a);
    return l > 1e-6f ? Pt{a.x / l, a.y / l} : Pt{1.0f, 0.0f};
}

int wrapX(const MapIn& m, int x)
{
    const int w = static_cast<int>(m.width);
    return ((x % w) + w) % w;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// bands and their look

bool isRail(Band b) { return b == kBandRail || b == kBandRailTrade || b == kBandRailPillaged; }

Band bandFromFeed(int route, int routeState, unsigned flags)
{
    if (route < 0) return kBandNone;
    const bool pillagedBit = (flags & 4u) != 0;
    switch (routeState) {
    case 0: return pillagedBit ? kBandRoadPillaged : kBandRoad;
    case 1: return pillagedBit ? kBandRailPillaged : kBandRail;
    case 2: return kBandRoadPillaged;
    case 3: return kBandRailPillaged;
    case 4:
    case 5:
    case 8: return kBandNone;   // under construction or none: stock draws no curve (extras.md X0.1)
    case 6: return kBandRoadTrade;
    case 7: return kBandRailTrade;
    default: break;
    }
    if (route == 0) return pillagedBit ? kBandRoadPillaged : kBandRoad;
    return pillagedBit ? kBandRailPillaged : kBandRail;
}

// Measured on the huge bundle C5H-0301-HUGE_0350_20261119-140930_workstation's 1,376 recorded route curves
// (render/evidence/0454/4933/stockbands.py, 2026-11-19; roads.md 1): v rows per route state, the u span of one texture
// repeat, the median repeat length and the median half width (iso samples).
const BandLook kLooks[kBandCount] = {
    {{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 1.0f, 0.0f},                   // none
    {{0.2844f, 0.3518f, 0.4192f}, 0.6772f, 0.9268f, 20.3f, 2.27f},  // road (state 0)
    {{0.4294f, 0.4929f, 0.5566f}, 0.6777f, 0.9268f, 15.9f, 2.23f},  // railroad (state 1)
    {{0.5459f, 0.6660f, 0.7856f}, 0.0103f, 0.2598f, 12.7f, 3.19f},  // road with a trade route (state 6)
    {{0.2896f, 0.4116f, 0.5337f}, 0.0105f, 0.2598f, 15.2f, 4.30f},  // railroad with a trade route (state 7)
    {{0.2844f, 0.3518f, 0.4192f}, 0.6772f, 0.9268f, 20.3f, 2.27f},  // road pillaged: the road's band (roads.md 5)
    {{0.4294f, 0.4929f, 0.5566f}, 0.6777f, 0.9268f, 15.9f, 2.23f},  // railroad pillaged: the railroad's band
};
const BandLook& bandLook(Band b) { return kLooks[b < kBandCount ? b : 0]; }

const char* const kRouteTextures[3] = {"RoadsandRails_new_D.dds", "RoadsandRails_new_H.dds", "RoadsandRails_new_SREF.dds"};

const char* styleName(Style s)
{
    switch (s) {
    case Style::Straight: return "straight";
    case Style::Spline: return "spline";
    case Style::SplineJitter: return "jitter";
    case Style::Line: return "line";
    }
    return "?";
}

bool styleFromName(const std::string& n, Style& out)
{
    if (n == "straight" || n == "a") out = Style::Straight;
    else if (n == "spline" || n == "b") out = Style::Spline;
    else if (n == "jitter" || n == "c") out = Style::SplineJitter;
    else if (n == "line") out = Style::Line;
    else return false;
    return true;
}

const char* connectName(Connect c) { return c == Connect::All ? "all" : "tidy"; }

bool connectFromName(const std::string& n, Connect& out)
{
    if (n == "tidy") out = Connect::Tidy;
    else if (n == "all") out = Connect::All;
    else return false;
    return true;
}

const char* junctionName(Junction j)
{
    switch (j) {
    case Junction::Through: return "through";
    case Junction::Cross: return "cross";
    case Junction::Ring: return "ring";
    case Junction::Rosette: return "rosette";
    }
    return "?";
}

bool junctionFromName(const std::string& n, Junction& out)
{
    if (n == "through") out = Junction::Through;
    else if (n == "cross") out = Junction::Cross;
    else if (n == "ring") out = Junction::Ring;
    else if (n == "rosette") out = Junction::Rosette;
    else return false;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// the grid

void plotCentre(int x, int y, float& sx, float& sy)
{
    sx = 64.0f * static_cast<float>(x) + 32.0f + 32.0f * static_cast<float>(y & 1);
    sy = 64.0f * static_cast<float>(y) + 128.0f;
}

bool neighbourRaw(int x, int y, int d, int height, int& nx, int& ny)
{
    const int o = y & 1;
    switch (d) {
    case 0: nx = x + 1; ny = y; break;
    case 1: nx = x + o; ny = y + 1; break;
    case 2: nx = x - 1 + o; ny = y + 1; break;
    case 3: nx = x - 1; ny = y; break;
    case 4: nx = x - 1 + o; ny = y - 1; break;
    case 5: nx = x + o; ny = y - 1; break;
    default: return false;
    }
    return ny >= 0 && ny < height;
}

bool neighbour(const MapIn& m, int x, int y, int d, int& nx, int& ny)
{
    if (!neighbourRaw(x, y, d, static_cast<int>(m.height), nx, ny)) return false;
    if (m.wrapX) nx = wrapX(m, nx);
    return nx >= 0 && nx < static_cast<int>(m.width);
}

namespace {

Status checkMap(const MapIn& m, const char* who)
{
    if (!m.width || !m.height || m.width > kMaxPlotsPerAxis || m.height > kMaxPlotsPerAxis)
        return Status::fail(Err::BadLayout, std::string(who) + ": map size " + std::to_string(m.width) + " x " +
                                                std::to_string(m.height) + " outside 1 .. " + std::to_string(kMaxPlotsPerAxis));
    if (m.plots.size() != static_cast<size_t>(m.width) * m.height)
        return Status::fail(Err::BadLayout, std::string(who) + ": " + std::to_string(m.plots.size()) + " plots for a " +
                                                std::to_string(m.width) + " x " + std::to_string(m.height) + " map");
    for (size_t i = 0; i < m.plots.size(); ++i)
        if (m.plots[i].band >= kBandCount || m.plots[i].city > 1)
            return Status::fail(Err::BadLayout, std::string(who) + ": plot " + std::to_string(i) + " has band " +
                                                    std::to_string(m.plots[i].band) + ", city " + std::to_string(m.plots[i].city));
    return Status::Ok();
}

bool isRoute(const MapIn& m, int x, int y) { return m.plots[static_cast<size_t>(y) * m.width + x].band != kBandNone; }

// The faces of the drawn graph. A face is walked by turning as far left as possible at every plot (the first edge
// clockwise from the way back), so a bounded face is walked counter-clockwise (positive area in iso, y up).
struct FaceWalk {
    std::vector<Pt> pts;         // iso, unwrapped from the start
    std::vector<int> px, py;     // the plots (wrapped)
    bool closed = false;         // came back to the start half-edge at the start position (not around the cylinder)
    bool sawReverse = false;     // walked the start edge the other way too (the edge is a bridge of this face)
    bool tooLong = false;
};

const int kMaxFaceSteps = 4096;

void walkFace(const MapIn& m, const std::vector<uint8_t>& edges, int x0, int y0, int d0, FaceWalk& f)
{
    f = FaceWalk();
    float sx, sy;
    plotCentre(x0, y0, sx, sy);
    Pt pos{sx, sy * kIsoY};
    const Pt start = pos;
    int x = x0, y = y0, d = d0;
    int rx = 0, ry = 0;
    neighbour(m, x0, y0, d0, rx, ry);
    const int rd = (d0 + 3) % 6;
    for (int step = 0; step < kMaxFaceSteps; ++step) {
        f.pts.push_back(pos);
        f.px.push_back(x);
        f.py.push_back(y);
        int nx, ny;
        if (!neighbour(m, x, y, d, nx, ny)) return;   // cannot happen for a drawn edge
        pos = pos + Pt{kOffX[d], kOffY[d]};
        const int back = (d + 3) % 6;
        const uint8_t e = edges[static_cast<size_t>(ny) * m.width + nx];
        int nd = back;
        for (int k = 1; k <= 6; ++k) {
            const int c = (back - k + 12) % 6;
            if (e & (1u << c)) {
                nd = c;
                break;
            }
        }
        x = nx;
        y = ny;
        d = nd;
        if (x == rx && y == ry && d == rd) f.sawReverse = true;
        if (x == x0 && y == y0 && d == d0) {
            f.closed = std::fabs(pos.x - start.x) < 1.0f && std::fabs(pos.y - start.y) < 1.0f;
            return;
        }
    }
    f.tooLong = true;
}

double signedArea(const std::vector<Pt>& p)
{
    double a = 0;
    for (size_t i = 0; i < p.size(); ++i) {
        const Pt& u = p[i];
        const Pt& v = p[(i + 1) % p.size()];
        a += static_cast<double>(u.x) * v.y - static_cast<double>(v.x) * u.y;
    }
    return a / 2;
}

bool insidePoly(const std::vector<Pt>& poly, float x, float y)
{
    bool c = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Pt& a = poly[i];
        const Pt& b = poly[j];
        if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) c = !c;
    }
    return c;
}

// A bounded face (positive area) is empty when no plot without a route lies strictly inside its walk.
bool faceIsEmpty(const MapIn& m, const FaceWalk& f)
{
    if (!f.closed || f.tooLong || f.pts.size() < 3) return false;
    if (signedArea(f.pts) <= 1.0) return false;   // the outer face (or a spur-only walk)
    float x0 = f.pts[0].x, x1 = x0, y0 = f.pts[0].y, y1 = y0;
    for (const Pt& p : f.pts) {
        x0 = (std::min)(x0, p.x);
        x1 = (std::max)(x1, p.x);
        y0 = (std::min)(y0, p.y);
        y1 = (std::max)(y1, p.y);
    }
    const int r0 = static_cast<int>(std::floor((y0 / kIsoY - 128.0f) / 64.0f)) - 1;
    const int r1 = static_cast<int>(std::ceil((y1 / kIsoY - 128.0f) / 64.0f)) + 1;
    for (int y = r0; y <= r1; ++y) {
        if (y < 0 || y >= static_cast<int>(m.height)) continue;
        const float py = (64.0f * y + 128.0f) * kIsoY;
        if (py <= y0 || py >= y1) continue;
        const int c0 = static_cast<int>(std::floor((x0 - 32.0f - 32.0f * (y & 1)) / 64.0f)) - 1;
        const int c1 = static_cast<int>(std::ceil((x1 - 32.0f - 32.0f * (y & 1)) / 64.0f)) + 1;
        for (int xu = c0; xu <= c1; ++xu) {
            const float px = 64.0f * xu + 32.0f + 32.0f * (y & 1);
            if (px <= x0 || px >= x1) continue;
            bool onWalk = false;
            for (const Pt& p : f.pts)
                if (std::fabs(p.x - px) < 1.0f && std::fabs(p.y - py) < 1.0f) {
                    onWalk = true;
                    break;
                }
            if (onWalk || !insidePoly(f.pts, px, py)) continue;
            int x = xu;
            if (m.wrapX) x = wrapX(m, x);
            else if (x < 0 || x >= static_cast<int>(m.width)) return false;   // off the map: not empty
            if (!isRoute(m, x, y)) return false;
        }
    }
    return true;
}

const int kRunLook = 4;   // plots looked at beyond each end of an edge for its straight run (the rank)

struct Cand {
    uint32_t p;       // plot index
    int d;            // direction 0..2 (each undirected edge once)
    uint32_t q;
    int t;            // route triangles on the edge (0..2)
    uint64_t key;     // rank (larger first)
};

}  // namespace

Status buildNetwork(const MapIn& m, Network& out, Connect connect)
{
    out = Network();
    C5R_TRY(checkMap(m, "buildNetwork"));
    if (connect != Connect::Tidy && connect != Connect::All)
        return Status::fail(Err::BadLayout, "buildNetwork: connect mode " + std::to_string(static_cast<int>(connect)));
    Network n;
    n.width = m.width;
    n.height = m.height;
    n.wrapX = m.wrapX;
    n.edges.assign(m.plots.size(), 0);
    const int W = static_cast<int>(m.width), H = static_cast<int>(m.height);
    std::vector<Cand> cands;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            if (!isRoute(m, x, y)) continue;
            ++n.routePlots;
            for (int d = 0; d < 3; ++d) {
                int qx, qy;
                if (!neighbour(m, x, y, d, qx, qy) || !isRoute(m, qx, qy)) continue;
                if (qx == x && qy == y) continue;   // a one-plot-wide wrapped map
                Cand c;
                c.p = static_cast<uint32_t>(y * W + x);
                c.q = static_cast<uint32_t>(qy * W + qx);
                c.d = d;
                c.t = 0;
                for (int s = -1; s <= 1; s += 2) {
                    int wx, wy;
                    if (neighbour(m, x, y, (d + s + 6) % 6, wx, wy) && isRoute(m, wx, wy)) ++c.t;
                }
                // the rank (roads.md 2): city edges, then railroad pairs, then same kind, then straight continuations,
                // then edges on fewer triangles, then a fixed hash
                const PlotIn& a = m.plots[c.p];
                const PlotIn& b = m.plots[c.q];
                const uint64_t city = (a.city || b.city) ? 1 : 0;
                const bool ra = isRail(static_cast<Band>(a.band)), rb = isRail(static_cast<Band>(b.band));
                const uint64_t kind = (ra && rb) ? 2 : (ra == rb ? 1 : 0);
                // the length of the straight run of route plots the edge lies on (along its axis, at most kRunLook
                // plots beyond each end, which bounds how far a change can reach): long straight lines win in a cluster
                // (the route runs straight as long as it can)
                uint64_t straight = 0;
                {
                    int cx = x, cy = y;
                    for (int k = 0; k < kRunLook; ++k) {
                        int ox, oy;
                        if (!neighbour(m, cx, cy, (d + 3) % 6, ox, oy) || !isRoute(m, ox, oy)) break;
                        ++straight;
                        cx = ox;
                        cy = oy;
                    }
                    cx = qx;
                    cy = qy;
                    for (int k = 0; k < kRunLook; ++k) {
                        int ox, oy;
                        if (!neighbour(m, cx, cy, d, ox, oy) || !isRoute(m, ox, oy)) break;
                        ++straight;
                        cx = ox;
                        cy = oy;
                    }
                }
                const uint64_t fewer = static_cast<uint64_t>(2 - c.t);
                const uint64_t h = mix(c.p, static_cast<uint32_t>(d), 0x52D0ADu);
                c.key = (straight << 46) | (city << 44) | (kind << 42) | (fewer << 38) | (h & 0xFFFFFFFFull);
                cands.push_back(c);
            }
        }
    n.candidateEdges = static_cast<uint32_t>(cands.size());
    auto setEdge = [&](const Cand& c, bool on) {
        if (on) {
            n.edges[c.p] |= static_cast<uint8_t>(1u << c.d);
            n.edges[c.q] |= static_cast<uint8_t>(1u << ((c.d + 3) % 6));
        } else {
            n.edges[c.p] &= static_cast<uint8_t>(~(1u << c.d));
            n.edges[c.q] &= static_cast<uint8_t>(~(1u << ((c.d + 3) % 6)));
        }
    };
    if (connect == Connect::All) {
        // all connections (ticket 4940): every candidate is drawn, no ring rule, so no clusters to decide
        for (const Cand& c : cands) {
            setEdge(c, true);
            if (c.t == 0) ++n.edges0;
        }
        out = std::move(n);
        return Status::Ok();
    }
    // edges on no route triangle can never close an empty face (roads.md 2): kept
    std::unordered_map<uint64_t, uint32_t> index;   // (plot, dir 0..2) -> candidate
    for (uint32_t i = 0; i < cands.size(); ++i) index[static_cast<uint64_t>(cands[i].p) * 8 + cands[i].d] = i;
    for (const Cand& c : cands)
        if (c.t == 0) {
            setEdge(c, true);
            ++n.edges0;
        }
    // the triangle clusters: candidates joined when they share a route triangle (union-find)
    std::vector<uint32_t> parent(cands.size());
    std::iota(parent.begin(), parent.end(), 0u);
    auto find = [&](uint32_t a) {
        while (parent[a] != a) {
            parent[a] = parent[parent[a]];
            a = parent[a];
        }
        return a;
    };
    auto unite = [&](uint32_t a, uint32_t b) {
        a = find(a);
        b = find(b);
        if (a != b) parent[(std::max)(a, b)] = (std::min)(a, b);
    };
    // the candidate of the edge from plot (x, y) in direction d (any of the six)
    auto candOf = [&](int x, int y, int d, uint32_t& out) -> bool {
        int qx, qy;
        if (!neighbour(m, x, y, d, qx, qy)) return false;
        uint64_t k;
        if (d < 3) k = static_cast<uint64_t>(y * W + x) * 8 + d;
        else k = static_cast<uint64_t>(qy * W + qx) * 8 + (d - 3);
        auto it = index.find(k);
        if (it == index.end()) return false;
        out = it->second;
        return true;
    };
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            if (!isRoute(m, x, y)) continue;
            // the two triangles above and to the right of the plot's E and NE edges cover every triangle once:
            // (p, E, NE) and (p, NE, NW)
            for (int d = 0; d < 2; ++d) {
                uint32_t a, b, c;
                int ux, uy;
                if (!candOf(x, y, d, a) || !candOf(x, y, d + 1, b)) continue;
                if (!neighbour(m, x, y, d, ux, uy) || !candOf(ux, uy, (d + 2) % 6, c)) continue;
                unite(a, b);
                unite(a, c);
            }
        }
    std::vector<std::vector<uint32_t>> clusters;
    {
        std::unordered_map<uint32_t, uint32_t> slot;
        for (uint32_t i = 0; i < cands.size(); ++i) {
            if (cands[i].t == 0) continue;
            const uint32_t r = find(i);
            auto it = slot.find(r);
            if (it == slot.end()) {
                slot[r] = static_cast<uint32_t>(clusters.size());
                clusters.push_back(std::vector<uint32_t>());
                clusters.back().push_back(i);
            } else {
                clusters[it->second].push_back(i);
            }
        }
    }
    n.clusters = static_cast<uint32_t>(clusters.size());
    FaceWalk f1, f2;
    for (std::vector<uint32_t>& cl : clusters) {
        n.maxClusterEdges = (std::max)(n.maxClusterEdges, static_cast<uint32_t>(cl.size()));
        std::sort(cl.begin(), cl.end(), [&](uint32_t a, uint32_t b) {
            return cands[a].key != cands[b].key ? cands[a].key > cands[b].key : a < b;
        });
        for (uint32_t i : cl) {
            const Cand& c = cands[i];
            setEdge(c, true);
            const int px = static_cast<int>(c.p % m.width), py = static_cast<int>(c.p / m.width);
            const int qx = static_cast<int>(c.q % m.width), qy = static_cast<int>(c.q / m.width);
            walkFace(m, n.edges, px, py, c.d, f1);
            bool empty = false;
            if (!f1.sawReverse) {
                walkFace(m, n.edges, qx, qy, (c.d + 3) % 6, f2);
                empty = faceIsEmpty(m, f1) || faceIsEmpty(m, f2);
            }
            if (empty) {
                setEdge(c, false);
                ++n.rejected;
            }
        }
    }
    out = std::move(n);
    return Status::Ok();
}

uint32_t countEmptyFaces(const MapIn& m, const Network& n, std::vector<uint32_t>* where)
{
    if (!checkMap(m, "countEmptyFaces").ok() || n.edges.size() != m.plots.size()) return 0xFFFFFFFFu;
    if (where) where->clear();
    std::vector<uint8_t> seen(m.plots.size(), 0);   // per plot: half-edges already walked (bit d)
    uint32_t bad = 0;
    FaceWalk f;
    for (uint32_t i = 0; i < m.plots.size(); ++i)
        for (int d = 0; d < kDirs; ++d) {
            if (!(n.edges[i] & (1u << d)) || (seen[i] & (1u << d))) continue;
            const int x = static_cast<int>(i % m.width), y = static_cast<int>(i / m.width);
            walkFace(m, n.edges, x, y, d, f);
            // mark the face's half-edges: each walked step leaves plot k in the direction to plot k + 1
            for (size_t k = 0; k < f.px.size(); ++k) {
                const size_t j = (k + 1) % f.px.size();
                for (int dd = 0; dd < kDirs; ++dd) {
                    int nx, ny;
                    if (neighbour(m, f.px[k], f.py[k], dd, nx, ny) && nx == f.px[j] && ny == f.py[j] &&
                        (n.edges[static_cast<size_t>(f.py[k]) * m.width + f.px[k]] & (1u << dd))) {
                        seen[static_cast<size_t>(f.py[k]) * m.width + f.px[k]] |= static_cast<uint8_t>(1u << dd);
                        break;
                    }
                }
            }
            seen[i] |= static_cast<uint8_t>(1u << d);
            if (faceIsEmpty(m, f)) {
                if (!bad && where)
                    for (size_t k = 0; k < f.px.size(); ++k) where->push_back(static_cast<uint32_t>(f.py[k]) * m.width + f.px[k]);
                ++bad;
            }
        }
    return bad;
}

// ---------------------------------------------------------------------------------------------------------------------
// the layout

namespace {

struct Ctx {
    const MapIn& m;
    const Network& n;
    const LayoutOptions& o;
};

uint8_t edgesOf(const Ctx& c, int x, int y) { return c.n.edges[static_cast<size_t>(y) * c.m.width + x]; }

// The pass point of plot (x, y), iso, around the given unwrapped centre: the centre, moved by the plot's fixed jitter in
// style (c), or by lineJitter in style Line (ticket 4940 part 1b). The jitter depends on the wrapped plot only, so both
// sides of an edge agree.
Pt passPoint(const Ctx& c, int x, int y, Pt centre)
{
    const float amount = c.o.style == Style::SplineJitter ? c.o.jitter : (c.o.style == Style::Line ? c.o.lineJitter : 0.0f);
    if (amount <= 0) return centre;
    const uint32_t h = mix(static_cast<uint32_t>(y) * c.m.width + static_cast<uint32_t>(x), 0x71u, 0x0A11CEu);
    const float ang = static_cast<float>(h & 0xFFFFu) * (6.2831853f / 65536.0f);
    const float r = amount * std::sqrt(static_cast<float>(h >> 16) / 65536.0f);
    return Pt{centre.x + r * std::cos(ang), centre.y + r * std::sin(ang)};
}

Pt isoCentre(int x, int y)
{
    float sx, sy;
    plotCentre(x, y, sx, sy);
    return Pt{sx, sy * kIsoY};
}

// The through pair of a plot with two or more drawn edges: the two directions farthest apart (3 = opposite), ties by a
// railroad on both neighbours, then by a fixed hash. -1, -1 with fewer than two edges.
void throughPair(const Ctx& c, int x, int y, int& d1, int& d2)
{
    d1 = d2 = -1;
    // a city is a hub: every edge a spoke to its centre, no road through it (a U-turn inside the city otherwise)
    if (c.m.plots[static_cast<size_t>(y) * c.m.width + x].city) return;
    const uint8_t e = edgesOf(c, x, y);
    int bestSep = -1;
    uint32_t bestTie = 0;
    for (int a = 0; a < kDirs; ++a) {
        if (!(e & (1u << a))) continue;
        for (int b = a + 1; b < kDirs; ++b) {
            if (!(e & (1u << b))) continue;
            const int sep = (std::min)(b - a, 6 - (b - a));
            int ax, ay, bx, by;
            neighbour(c.m, x, y, a, ax, ay);
            neighbour(c.m, x, y, b, bx, by);
            const uint32_t rail = (isRail(static_cast<Band>(c.m.plots[static_cast<size_t>(ay) * c.m.width + ax].band)) ? 1u : 0u) +
                                  (isRail(static_cast<Band>(c.m.plots[static_cast<size_t>(by) * c.m.width + bx].band)) ? 1u : 0u);
            const uint32_t tie = (rail << 28) | (mix(static_cast<uint32_t>(y) * c.m.width + x, static_cast<uint32_t>(a * 8 + b), 0x7A1Bu) >> 4);
            if (sep > bestSep || (sep == bestSep && tie > bestTie)) {
                bestSep = sep;
                bestTie = tie;
                d1 = a;
                d2 = b;
            }
        }
    }
}

// The unit tangent of the road at plot (x, y) (whose pass point is P, its neighbours' pass points around the unwrapped
// centre C) leaving in direction d: along the through curve when d is in the through pair, else straight at the
// neighbour's pass point.
Pt tangentAt(const Ctx& c, int x, int y, Pt C, int d)
{
    const Pt P = passPoint(c, x, y, C);
    int d1, d2;
    throughPair(c, x, y, d1, d2);
    auto passOf = [&](int dd) {
        int nx, ny;
        neighbour(c.m, x, y, dd, nx, ny);
        return passPoint(c, nx, ny, C + Pt{kOffX[dd], kOffY[dd]});
    };
    if (d == d1 || d == d2) {
        const int other = d == d1 ? d2 : d1;
        return unit(passOf(d) - passOf(other));
    }
    return unit(passOf(d) - P);
}

// Points of the road between plot (x, y) and its neighbour in direction d, from the plot's pass point (s = 0) to the
// shared edge's middle (s = 0.5), `steps` + 1 points, appended to out in that order.
void halfEdge(const Ctx& c, int x, int y, Pt C, int d, int steps, std::vector<Pt>& out)
{
    int nx, ny;
    neighbour(c.m, x, y, d, nx, ny);
    const Pt Cq = C + Pt{kOffX[d], kOffY[d]};
    const Pt P0 = passPoint(c, x, y, C);
    const Pt P1 = passPoint(c, nx, ny, Cq);
    if (c.o.style == Style::Straight || c.o.style == Style::Line) {
        for (int i = 0; i <= steps; ++i) {
            const float s = 0.5f * static_cast<float>(i) / steps;
            out.push_back(P0 + (P1 - P0) * s);
        }
        return;
    }
    const float L = len(P1 - P0) * c.o.tension;
    const Pt M0 = tangentAt(c, x, y, C, d) * L;
    const Pt M1 = tangentAt(c, nx, ny, Cq, (d + 3) % 6) * (-L);
    for (int i = 0; i <= steps; ++i) {
        const float s = 0.5f * static_cast<float>(i) / steps;
        const float s2 = s * s, s3 = s2 * s;
        const float h00 = 2 * s3 - 3 * s2 + 1, h10 = s3 - 2 * s2 + s, h01 = -2 * s3 + 3 * s2, h11 = s3 - s2;
        out.push_back(P0 * h00 + M0 * h10 + P1 * h01 + M1 * h11);
    }
}

// A piece's centre line resampled every `step` along its length (the ends kept).
void resample(const std::vector<Pt>& in, float step, std::vector<Pt>& out, std::vector<float>& arc)
{
    out.clear();
    arc.clear();
    if (in.size() < 2) return;
    std::vector<float> acc(in.size(), 0.0f);
    for (size_t i = 1; i < in.size(); ++i) acc[i] = acc[i - 1] + len(in[i] - in[i - 1]);
    const float L = acc.back();
    if (L < 1e-3f) return;
    const int n = (std::max)(1, static_cast<int>(std::ceil(L / step)));
    size_t j = 0;
    for (int k = 0; k <= n; ++k) {
        const float s = L * static_cast<float>(k) / n;
        while (j + 1 < in.size() - 1 && acc[j + 1] < s) ++j;
        const float seg = acc[j + 1] - acc[j];
        const float t = seg > 1e-6f ? (s - acc[j]) / seg : 0.0f;
        out.push_back(in[j] + (in[j + 1] - in[j]) * (std::min)(1.0f, (std::max)(0.0f, t)));
        arc.push_back(s);
    }
}

struct Piece {
    std::vector<Pt> pts;   // iso
};

// The point `dist` along the polyline `line` from its vertex nearest `from`, walking the way that runs with `dir`.
Pt alongFrom(const std::vector<Pt>& line, Pt from, Pt dir, float dist)
{
    if (line.size() < 2) return from;
    size_t k = 0;
    float best = len(line[0] - from);
    for (size_t i = 1; i < line.size(); ++i)
        if (len(line[i] - from) < best) {
            best = len(line[i] - from);
            k = i;
        }
    const Pt fwd = line[(std::min)(k + 1, line.size() - 1)] - line[k > 0 ? k - 1 : 0];
    const int step = (fwd.x * dir.x + fwd.y * dir.y) >= 0 ? 1 : -1;
    float left = dist;
    for (long i = static_cast<long>(k); i + step >= 0 && i + step < static_cast<long>(line.size()); i += step) {
        const Pt a = line[static_cast<size_t>(i)], b = line[static_cast<size_t>(i + step)];
        const float l = len(b - a);
        if (l >= left && l > 1e-6f) return a + (b - a) * (left / l);
        left -= l;
    }
    return line[step > 0 ? line.size() - 1 : 0];
}

// n + 1 points from a to b (n points when skipFirst: a is already the piece's last point)
void appendLine(Piece& pc, Pt a, Pt b, int n, bool skipFirst)
{
    for (int i = skipFirst ? 1 : 0; i <= n; ++i) pc.pts.push_back(a + (b - a) * (static_cast<float>(i) / n));
}

const int kRingSteps = 48;   // a ring's points: every 7.5 degrees, so the six edge directions (k * 60) are among them
const float kRosetteLegShare = 0.6f;   // Junction::Rosette: a curve's legs at most this share of the half edge

Pt ringPoint(Pt P, float R, int k)
{
    const float a = static_cast<float>(k % kRingSteps) * (6.2831853f / kRingSteps);
    return Pt{P.x + R * std::cos(a), P.y + R * std::sin(a)};
}

// The pieces of a junction plot (Junction other than Through; ticket 4940): straight legs from the edges' middles, the
// centre by the treatment. P = the plot's pass point, mid[d] = the middle of edge d (as halfEdge ends it, the same point
// the neighbour's half ends on).
// Points per short curve: 16, or 32 in style Line with lineJitter (part 1b: the jitter adds to a curve's turn, the
// finer step keeps the turn per step within the unjittered layout's; 16 keeps lineJitter 0 byte for byte)
int curveSteps(const Ctx& c) { return (c.o.style == Style::Line && c.o.lineJitter > 0) ? 32 : 16; }

void junctionPieces(const Ctx& c, Pt P, uint8_t e, const Pt* mid, std::vector<Piece>& pieces)
{
    const int legSteps = 24;
    const int nCurve = curveSteps(c);
    switch (c.o.junction) {
    case Junction::Cross: {
        bool used[kDirs] = {false, false, false, false, false, false};
        for (int d = 0; d < 3; ++d) {
            const int o = d + 3;
            if (!(e & (1u << d)) || !(e & (1u << o))) continue;
            Piece pc;
            // P itself a point of every line: they cross in it. A line bent at P (lineJitter moved the pass points) is
            // rounded through P: straight to A, a cubic Hermite A -> P -> B with P's tangent halfway between the legs,
            // then straight on, so the turn per step stays small and the lines still meet in P exactly (part 1b)
            const Pt u1 = unit(P - mid[d]), u2 = unit(mid[o] - P);
            const float cosT = (std::max)(-1.0f, (std::min)(1.0f, u1.x * u2.x + u1.y * u2.y));
            const float phi = std::acos(cosT);
            const float legs = (std::min)(len(P - mid[d]), len(mid[o] - P));
            const float t = phi > 1e-3f ? (std::min)(c.o.filletRadius * std::tan(phi / 2), 0.85f * legs) : 0.0f;
            if (t <= 0.5f) {
                appendLine(pc, mid[d], P, legSteps, false);
                appendLine(pc, P, mid[o], legSteps, true);
            } else {
                const Pt A = P - u1 * t, B = P + u2 * t, T = unit(u1 + u2);
                appendLine(pc, mid[d], A, 12, false);
                auto herm = [&](Pt p0, Pt m0, Pt p1, Pt m1) {
                    for (int k = 1; k <= nCurve / 2; ++k) {
                        const float s = static_cast<float>(k) / (nCurve / 2), s2 = s * s, s3 = s2 * s;
                        pc.pts.push_back(p0 * (2 * s3 - 3 * s2 + 1) + m0 * (s3 - 2 * s2 + s) + p1 * (-2 * s3 + 3 * s2) +
                                         m1 * (s3 - s2));
                    }
                };
                herm(A, u1 * t, P, T * t);   // ends exactly on P (s = 1)
                herm(P, T * t, B, u2 * t);
                appendLine(pc, B, mid[o], 12, true);
            }
            pieces.push_back(pc);
            used[d] = used[o] = true;
        }
        for (int d = 0; d < kDirs; ++d) {
            if (!(e & (1u << d)) || used[d]) continue;
            Piece pc;
            appendLine(pc, mid[d], P, legSteps, false);
            pieces.push_back(pc);
        }
        break;
    }
    case Junction::Ring: {
        // the spoke of edge d ends on ring point 8 d (direction d is d * 60 degrees in iso), the very point of the ring
        const float R = c.o.ringRadius;
        for (int d = 0; d < kDirs; ++d) {
            if (!(e & (1u << d))) continue;
            Piece pc;
            appendLine(pc, mid[d], ringPoint(P, R, d * (kRingSteps / kDirs)), legSteps, false);
            pieces.push_back(pc);
        }
        Piece ring;
        for (int k = 0; k <= kRingSteps; ++k) ring.pts.push_back(ringPoint(P, R, k));
        pieces.push_back(ring);
        break;
    }
    case Junction::Rosette: {
        int ds[kDirs], nd = 0;
        for (int d = 0; d < kDirs; ++d)
            if (e & (1u << d)) ds[nd++] = d;
        for (int i = 0; i < nd; ++i) {
            const Pt m1 = mid[ds[i]], m2 = mid[ds[(i + 1) % nd]];
            const Pt u1 = unit(P - m1), u2 = unit(m2 - P);
            const float cosT = (std::max)(-1.0f, (std::min)(1.0f, u1.x * u2.x + u1.y * u2.y));
            const float phi = std::acos(cosT);   // the deflection: 0 for opposite edges (a straight line through P)
            const float legs = (std::min)(len(P - m1), len(m2 - P));
            float t = phi > 1e-3f ? c.o.filletRadius * std::tan((std::min)(phi, 3.0f) / 2) : 0.0f;
            t = (std::min)(t, kRosetteLegShare * legs);
            Piece pc;
            if (t <= 0) {
                appendLine(pc, m1, P, legSteps, false);
                appendLine(pc, P, m2, legSteps, true);
            } else {
                const Pt A = P - u1 * t, B = P + u2 * t;
                appendLine(pc, m1, A, 12, false);
                for (int k = 1; k < nCurve; ++k) {
                    const float s = static_cast<float>(k) / nCurve;
                    pc.pts.push_back(A * ((1 - s) * (1 - s)) + P * (2 * (1 - s) * s) + B * (s * s));
                }
                appendLine(pc, B, m2, 12, false);
            }
            pieces.push_back(pc);
        }
        break;
    }
    case Junction::Through: break;
    }
}

// The pieces of plot (x, y). C = its unwrapped centre (iso).
void piecesOf(const Ctx& c, int x, int y, std::vector<Piece>& pieces)
{
    pieces.clear();
    const int nCurve = curveSteps(c);
    const Pt C = isoCentre(x, y);
    const Pt P = passPoint(c, x, y, C);
    const uint8_t e = edgesOf(c, x, y);
    const int steps = 24;
    std::vector<Pt> a, b;
    if (!e) {   // a stub through the centre along one of the three axes (fixed per plot)
        const int axis = static_cast<int>(mix(static_cast<uint32_t>(y) * c.m.width + x, 3u, 0x5707u) % 3u);
        const Pt dir = unit(Pt{kOffX[axis], kOffY[axis]});
        Piece pc;
        // centred on the plot's centre, not the (jittered) pass point: its ends stay stubLength / 2 from the centre
        for (int i = 0; i <= 8; ++i) pc.pts.push_back(C + dir * (c.o.stubLength * (static_cast<float>(i) / 8 - 0.5f)));
        pieces.push_back(pc);
        return;
    }
    // a junction plot (ticket 4940): its centre by the treatment asked for; a city stays a hub
    int links = 0;
    for (int d = 0; d < kDirs; ++d) links += (e >> d) & 1;
    if (c.o.junction != Junction::Through && links >= c.o.junctionMin &&
        !c.m.plots[static_cast<size_t>(y) * c.m.width + x].city) {
        Pt mid[kDirs];
        for (int d = 0; d < kDirs; ++d) {
            mid[d] = P;
            if (!(e & (1u << d))) continue;
            a.clear();
            halfEdge(c, x, y, C, d, steps, a);
            mid[d] = a.back();
        }
        junctionPieces(c, P, e, mid, pieces);
        return;
    }
    int d1, d2;
    throughPair(c, x, y, d1, d2);
    Pt hub = P;   // where spurs and dead ends stop
    Pt hubDir{0, 0};   // style Line: the through-line's direction at the hub (0, 0: no through piece)
    const bool lineStyle = c.o.style == Style::Line;
    if (d1 >= 0) {
        // through: the middle of edge d1 -> the pass point -> the middle of edge d2
        a.clear();
        b.clear();
        halfEdge(c, x, y, C, d1, steps, a);
        halfEdge(c, x, y, C, d2, steps, b);
        Piece pc;
        if (c.o.style == Style::Straight || lineStyle) {
            // straight legs with the turn rounded: a quadratic arc from A to B with its corner at the pass point
            const Pt m1 = a.back(), m2 = b.back();
            const Pt u1 = unit(P - m1), u2 = unit(m2 - P);
            const float cosT = (std::max)(-1.0f, (std::min)(1.0f, u1.x * u2.x + u1.y * u2.y));
            const float phi = std::acos(cosT);   // deflection
            const float legs = (std::min)(len(P - m1), len(m2 - P));
            float t = phi > 1e-3f ? c.o.filletRadius * std::tan(phi / 2) : 0.0f;
            t = (std::min)(t, 0.85f * legs);
            const Pt A = P - u1 * t, B = P + u2 * t;
            // a junction's spurs end on the arc's middle, not at the corner it cuts off
            hub = A * 0.25f + P * 0.5f + B * 0.25f;
            hubDir = t > 0 ? unit(B - A) : u2;
            const int na = 12;
            for (int i = 0; i <= na; ++i) pc.pts.push_back(m1 + (A - m1) * (static_cast<float>(i) / na));
            if (t > 0) {
                for (int i = 1; i < nCurve; ++i) {
                    const float s = static_cast<float>(i) / nCurve;
                    pc.pts.push_back(A * ((1 - s) * (1 - s)) + P * (2 * (1 - s) * s) + B * (s * s));
                }
            }
            for (int i = 0; i <= na; ++i) pc.pts.push_back(B + (m2 - B) * (static_cast<float>(i) / na));
        } else {
            for (size_t i = a.size(); i-- > 0;) pc.pts.push_back(a[i]);
            for (size_t i = 1; i < b.size(); ++i) pc.pts.push_back(b[i]);
        }
        pieces.push_back(pc);
    }
    for (int d = 0; d < kDirs; ++d) {
        if (!(e & (1u << d)) || d == d1 || d == d2) continue;
        // a spur (a junction's other edges, a city's spokes) or a dead end: from the edge's middle to the pass point
        // (style (a): to the hub on the through piece's arc), where it stops
        a.clear();
        halfEdge(c, x, y, C, d, steps, a);
        Piece pc;
        if (lineStyle) {
            const Pt m = a.back();
            const Pt u = unit(hub - m);   // heading into the plot
            const float room = len(hub - m);
            // the end of the straight and a short curve: into the through-line (a branch merging), or, for a run cut
            // short inside a cluster (one edge, a route neighbour left out by the ring rule), a hook that ends inside
            // the plot, so no straight run stops abruptly
            Pt endDir{0, 0};
            float legs = 0, endScale = 1.0f;
            if (hubDir.x != 0 || hubDir.y != 0) {
                // a branch merges only when it is the only one on its side of the through-line: two branches on one
                // side merging in opposite directions would cross, so they meet the line straight
                int sameSide = 0;
                const float mySide = hubDir.x * (m.y - hub.y) - hubDir.y * (m.x - hub.x);
                for (int dd = 0; dd < kDirs; ++dd) {
                    if (!(e & (1u << dd)) || dd == d1 || dd == d2) continue;
                    const Pt md = C + Pt{kOffX[dd], kOffY[dd]} * 0.5f;
                    const float side = hubDir.x * (md.y - hub.y) - hubDir.y * (md.x - hub.x);
                    if ((side > 0) == (mySide > 0)) ++sameSide;
                }
                // and only into a straight through-line (a bent one turns away from the merge's tangent and is crossed)
                const bool straightThrough = (std::min)(std::abs(d2 - d1), 6 - std::abs(d2 - d1)) == 3;
                if (c.o.mergeLength > 0 && sameSide == 1 && straightThrough) {
                    endDir = (u.x * hubDir.x + u.y * hubDir.y) >= 0 ? hubDir : hubDir * -1.0f;
                    legs = c.o.mergeLength;
                }
            } else if (c.o.leafCurve > 0 && !c.m.plots[static_cast<size_t>(y) * c.m.width + x].city) {
                // the left-out route neighbour most nearly ahead decides the side of the hook
                int best = -1;
                float bestDot = -2;
                for (int dd = 0; dd < kDirs; ++dd) {
                    int nx, ny;
                    if (e & (1u << dd)) continue;
                    if (!neighbour(c.m, x, y, dd, nx, ny) || c.m.plots[static_cast<size_t>(ny) * c.m.width + nx].band == kBandNone)
                        continue;
                    const Pt v = unit(Pt{kOffX[dd], kOffY[dd]});
                    const float dot = u.x * v.x + u.y * v.y;
                    if (dot > bestDot) {
                        bestDot = dot;
                        best = dd;
                    }
                }
                if (best >= 0) {
                    // turn by 60 degrees towards that neighbour's side (straight ahead: to the left)
                    const Pt v = unit(Pt{kOffX[best], kOffY[best]});
                    const float side = (u.x * v.y - u.y * v.x) >= 0 ? 1.0f : -1.0f;
                    const float cs = 0.5f, sn = 0.8660254f * side;
                    endDir = Pt{u.x * cs - u.y * sn, u.x * sn + u.y * cs};
                    legs = c.o.leafCurve;
                    endScale = 0.6f;
                }
            }
            legs = (std::min)(legs, 0.85f * room);
            if (legs > 0.5f) {
                const Pt A = hub - u * legs;
                Pt J = hub + endDir * (legs * endScale);
                // a branch merging into a through-line that lineJitter bent (part 1b): its end on the line itself, the
                // same distance along it, not on the hub's tangent, which leaves the line where it bends
                if (c.o.lineJitter > 0 && d1 >= 0 && !pieces.empty() && (hubDir.x != 0 || hubDir.y != 0))
                    J = alongFrom(pieces[0].pts, hub, endDir, legs * endScale);
                for (int i = 0; i <= 12; ++i) pc.pts.push_back(m + (A - m) * (static_cast<float>(i) / 12));
                for (int i = 1; i <= nCurve; ++i) {
                    const float s = static_cast<float>(i) / nCurve;
                    pc.pts.push_back(A * ((1 - s) * (1 - s)) + hub * (2 * (1 - s) * s) + J * (s * s));
                }
            } else {
                for (int i = 0; i <= steps; ++i) pc.pts.push_back(m + (hub - m) * (static_cast<float>(i) / steps));
            }
        } else if (c.o.style == Style::Straight) {
            const Pt m = a.back();
            for (int i = 0; i <= steps; ++i) pc.pts.push_back(m + (hub - m) * (static_cast<float>(i) / steps));
        } else {
            for (size_t i = a.size(); i-- > 0;) pc.pts.push_back(a[i]);
        }
        pieces.push_back(pc);
    }
    // a city: every piece ends cityStop from the centre (the ring)
    if (c.o.cityStop > 0 && c.m.plots[static_cast<size_t>(y) * c.m.width + x].city) {
        std::vector<Piece> cut;
        for (const Piece& pc : pieces) {
            Piece cur;
            for (const Pt& p : pc.pts) {
                if (len(p - P) >= c.o.cityStop) cur.pts.push_back(p);
                else if (cur.pts.size() >= 2) {
                    cut.push_back(cur);
                    cur.pts.clear();
                } else {
                    cur.pts.clear();
                }
            }
            if (cur.pts.size() >= 2) cut.push_back(cur);
        }
        pieces.swap(cut);
    }
}

// The strip of one piece: three vertex rows (edge, centre, edge) in the band's v rows; u from u1 to u0 over each texture
// repeat (the repeats share the piece's length evenly). Appends triangles (sample frame, absolute) to tri: x, y, u, v.
void strip(const Piece& pc, const BandLook& lk, float subStep, std::vector<float>& tri)
{
    std::vector<Pt> q;
    std::vector<float> arc;
    resample(pc.pts, subStep, q, arc);
    if (q.size() < 2) return;
    const float L = arc.back();
    const int reps = (std::max)(1, static_cast<int>(std::lround(L / lk.repeat)));
    const float rl = L / reps;
    // the positions: the resampled points plus every repeat boundary
    std::vector<Pt> pts;
    std::vector<float> s;
    for (size_t i = 0; i < q.size(); ++i) {
        if (i) {
            const int k0 = static_cast<int>(std::floor(arc[i - 1] / rl + 1e-4f));
            for (int k = k0 + 1; k < reps; ++k) {
                const float sb = k * rl;
                if (sb > arc[i - 1] + 1e-3f && sb < arc[i] - 1e-3f) {
                    const float t = (sb - arc[i - 1]) / (arc[i] - arc[i - 1]);
                    pts.push_back(q[i - 1] + (q[i] - q[i - 1]) * t);
                    s.push_back(sb);
                }
            }
        }
        pts.push_back(q[i]);
        s.push_back(arc[i]);
    }
    const size_t n = pts.size();
    std::vector<Pt> nrm(n);
    for (size_t i = 0; i < n; ++i) {
        const Pt t = unit(pts[(std::min)(n - 1, i + 1)] - pts[i == 0 ? 0 : i - 1]);
        nrm[i] = Pt{-t.y, t.x};
    }
    auto emit = [&](Pt p, float u, float v) {
        tri.push_back(p.x);
        tri.push_back(p.y / kIsoY);
        tri.push_back(u);
        tri.push_back(v);
    };
    for (size_t i = 0; i + 1 < n; ++i) {
        const float mid = 0.5f * (s[i] + s[i + 1]);
        const int k = (std::min)(reps - 1, static_cast<int>(std::floor(mid / rl)));
        const float fa = (s[i] - k * rl) / rl, fb = (s[i + 1] - k * rl) / rl;
        const float ua = lk.u1 + (lk.u0 - lk.u1) * (std::max)(0.0f, (std::min)(1.0f, fa));
        const float ub = lk.u1 + (lk.u0 - lk.u1) * (std::max)(0.0f, (std::min)(1.0f, fb));
        Pt ra[3], rb[3];
        for (int r = 0; r < 3; ++r) {
            const float off = (1 - r) * lk.halfWidth;   // row 0 left edge, 1 centre, 2 right edge
            ra[r] = pts[i] + nrm[i] * off;
            rb[r] = pts[i + 1] + nrm[i + 1] * off;
        }
        for (int r = 0; r < 2; ++r) {
            emit(ra[r], ua, lk.v[r]);
            emit(rb[r], ub, lk.v[r]);
            emit(rb[r + 1], ub, lk.v[r + 1]);
            emit(rb[r + 1], ub, lk.v[r + 1]);
            emit(ra[r + 1], ua, lk.v[r + 1]);
            emit(ra[r], ua, lk.v[r]);
        }
    }
}

// One piece of plot `owner` as one decal of `r`: its strip, its centre line, its rectangle (clamped to the world as the
// EXE clamps its rectangles, terrain-decals.md 1.1; the vertices keep their place) and its vertices normalised to it.
Status emitPiece(const Piece& pc, const BandLook& lk, const LayoutOptions& o, uint32_t owner, float worldX1, float worldY1,
                 std::vector<float>& tri, RoadDecals& r)
{
    tri.clear();
    strip(pc, lk, o.subStep, tri);
    const size_t nv = tri.size() / 4;
    if (!nv) return Status::Ok();
    if (nv > 0xFFFEu / 3 * 3)
        return Status::fail(Err::BadLayout, "layOut: a piece of plot " + std::to_string(owner) + " has " + std::to_string(nv) +
                                                " vertices (more than one decal holds)");
    r.lineStart.push_back(static_cast<uint32_t>(r.lines.size() / 2));
    for (const Pt& p : pc.pts) {
        r.lines.push_back(p.x);
        r.lines.push_back(p.y);
    }
    r.lines.push_back(std::numeric_limits<float>::quiet_NaN());
    r.lines.push_back(std::numeric_limits<float>::quiet_NaN());
    decal::Decal d;
    float r0 = tri[0], r1 = tri[1], r2 = tri[0], r3 = tri[1];
    for (size_t k = 0; k < nv; ++k) {
        r0 = (std::min)(r0, tri[4 * k]);
        r2 = (std::max)(r2, tri[4 * k]);
        r1 = (std::min)(r1, tri[4 * k + 1]);
        r3 = (std::max)(r3, tri[4 * k + 1]);
    }
    d.rect[0] = (std::max)(0.0f, r0);
    d.rect[1] = (std::max)(0.0f, r1);
    d.rect[2] = (std::min)(worldX1, r2);
    d.rect[3] = (std::min)(worldY1, r3);
    if (!(d.rect[0] < d.rect[2] && d.rect[1] < d.rect[3])) {
        d.rect[2] = d.rect[0] + 1;
        d.rect[3] = d.rect[1] + 1;
    }
    d.priority = o.priority;
    d.pass = 0;
    d.texture[0] = 0;
    d.texture[1] = 1;
    d.texture[2] = 2;
    d.firstVertex = static_cast<uint32_t>(r.vertices.size());
    d.vertexCount = static_cast<uint32_t>(nv);
    d.listOrder = static_cast<uint32_t>(r.decals.size());
    const float ix = 1.0f / (d.rect[2] - d.rect[0]), iy = 1.0f / (d.rect[3] - d.rect[1]);
    for (size_t k = 0; k < nv; ++k)
        r.vertices.push_back(decal::DecalVertex{bake::floatToHalf((tri[4 * k] - d.rect[0]) * ix),
                                                bake::floatToHalf((tri[4 * k + 1] - d.rect[1]) * iy),
                                                bake::floatToHalf(tri[4 * k + 2]), bake::floatToHalf(tri[4 * k + 3])});
    r.decals.push_back(d);
    r.owner.push_back(owner);
    return Status::Ok();
}

}  // namespace

Status layOut(const MapIn& m, const Network& n, const LayoutOptions& o, RoadDecals& out)
{
    out = RoadDecals();
    C5R_TRY(checkMap(m, "layOut"));
    if (n.width != m.width || n.height != m.height || n.wrapX != m.wrapX || n.edges.size() != m.plots.size())
        return Status::fail(Err::BadLayout, "layOut: the network is not this map's");
    if (!(o.subStep >= 0.5f && o.subStep <= 16.0f) || !(o.tension >= 0 && o.tension <= 2) || !(o.jitter >= 0 && o.jitter <= 16) ||
        !(o.filletRadius >= 0 && o.filletRadius <= 64) || !(o.stubLength > 0 && o.stubLength <= 64) ||
        !(o.cityStop >= 0 && o.cityStop < 32) || !std::isfinite(o.priority) || !(o.mergeLength >= 0 && o.mergeLength <= 32) ||
        !(o.leafCurve >= 0 && o.leafCurve <= 32) || static_cast<int>(o.style) > static_cast<int>(Style::Line) ||
        static_cast<int>(o.junction) > static_cast<int>(Junction::Rosette) || o.junctionMin < 3 || o.junctionMin > 6 ||
        !(o.ringRadius >= 4 && o.ringRadius <= 24) || !(o.lineJitter >= 0 && o.lineJitter <= 7) || static_cast<int>(o.connect) > static_cast<int>(Connect::All))
        return Status::fail(Err::BadLayout, "layOut: an option outside its range");
    for (uint32_t i = 0; i < m.plots.size(); ++i) {
        const uint8_t e = n.edges[i];
        if (e && m.plots[i].band == kBandNone) return Status::fail(Err::BadLayout, "layOut: an edge on a plot without a route");
        for (int d = 0; d < kDirs; ++d) {
            if (!(e & (1u << d))) continue;
            int qx, qy;
            if (!neighbour(m, static_cast<int>(i % m.width), static_cast<int>(i / m.width), d, qx, qy) ||
                !(n.edges[static_cast<size_t>(qy) * m.width + qx] & (1u << ((d + 3) % 6))))
                return Status::fail(Err::BadLayout, "layOut: an edge without its other half");
        }
    }
    RoadDecals r;
    for (int t = 0; t < 3; ++t) r.textures.push_back(kRouteTextures[t]);
    const Ctx c{m, n, o};
    const float worldX1 = 64.0f * m.width - 1, worldY1 = 64.0f * (m.height + 4) - 1;
    std::vector<Piece> pieces;
    std::vector<float> tri;
    for (uint32_t y = 0; y < m.height; ++y)
        for (uint32_t x = 0; x < m.width; ++x) {
            const uint32_t i = y * m.width + x;
            const Band band = static_cast<Band>(m.plots[i].band);
            if (band == kBandNone) continue;
            piecesOf(c, static_cast<int>(x), static_cast<int>(y), pieces);
            // one decal per piece (as the recorded route curves: one per plot and direction of travel)
            for (const Piece& pc : pieces) C5R_TRY(emitPiece(pc, bandLook(band), o, i, worldX1, worldY1, tri, r));
        }
    r.lineStart.push_back(static_cast<uint32_t>(r.lines.size() / 2));
    for (decal::Decal& d : r.decals) d.hash = contentHash(d, r.textures, r.vertices);
    out = std::move(r);
    return Status::Ok();
}

Status produce(const MapIn& m, const LayoutOptions& o, Network& net, RoadDecals& out)
{
    out = RoadDecals();
    C5R_TRY(buildNetwork(m, net, o.connect));
    return layOut(m, net, o, out);
}

// ---------------------------------------------------------------------------------------------------------------------
// joining the tap's set

bool isTapRouteCurve(const decal::DecalSet& set, const decal::Decal& d)
{
    if (!(std::fabs(d.priority - 0.7f) < 1e-4f) || d.texture[0] >= set.textures.size()) return false;
    std::string s = set.textures[d.texture[0]];
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s.find("roadsandrails") != std::string::npos;
}

namespace {

uint32_t axisCell(float p, float origin, float inverse, uint32_t n)
{
    const float t = (p - origin) * inverse;
    if (!(t >= 0)) return 0;
    const double c = std::trunc(static_cast<double>(t));
    if (c >= static_cast<double>(n)) return n - 1;
    return static_cast<uint32_t>(c);
}

bool tileSpan(float lo, float hi, uint32_t n, uint32_t& a, uint32_t& b)
{
    const double t0 = std::floor(static_cast<double>(lo) / decal::kTileSamples);
    const double t1 = std::floor(static_cast<double>(hi) / decal::kTileSamples);
    if (n == 0 || t1 < 0 || t0 > static_cast<double>(n) - 1 || t1 < t0) return false;
    a = static_cast<uint32_t>((std::max)(0.0, t0));
    b = static_cast<uint32_t>((std::min)(static_cast<double>(n) - 1, t1));
    return true;
}

}  // namespace

Status composeRoadSet(const decal::DecalSet& tap, bool removeTapRoutes, const RoadDecals& roads, decal::DecalSet& out,
                      ComposeStats* stats)
{
    out = decal::DecalSet();
    if (stats) *stats = ComposeStats();
    const float* w = tap.worldRect;
    if (!finite4(w) || !(w[0] < w[2] && w[1] < w[3]))
        return Status::fail(Err::BadLayout, "composeRoadSet: the tap's world rectangle is not finite or empty");
    const size_t nr = roads.decals.size();
    if (roads.owner.size() != nr) return Status::fail(Err::BadLayout, "composeRoadSet: owner table length");
    for (size_t k = 0; k < nr; ++k) {
        const decal::Decal& d = roads.decals[k];
        const std::string at = "composeRoadSet: road decal " + std::to_string(k);
        if (!d.vertexCount || d.vertexCount % 3 || d.vertexCount > 0xFFFEu / 3 * 3 ||
            static_cast<uint64_t>(d.firstVertex) + d.vertexCount > roads.vertices.size())
            return Status::fail(Err::BadLayout, at + " has a vertex run outside its vertices");
        for (int t = 0; t < 3; ++t)
            if (d.texture[t] != decal::kNoTexture && d.texture[t] >= roads.textures.size())
                return Status::fail(Err::BadLayout, at + " names a texture outside its table");
        const float* r = d.rect;
        if (!finite4(r) || !(r[0] < r[2] && r[1] < r[3]) || r[0] < w[0] - 1 || r[1] < w[1] - 1 || r[2] > w[2] + 1 ||
            r[3] > w[3] + 1)
            return Status::fail(Err::BadLayout, at + " has a rectangle not finite, empty or outside the tap's world");
    }
    std::vector<uint8_t> drop(tap.decals.size(), 0);
    size_t removed = 0;
    if (removeTapRoutes)
        for (size_t i = 0; i < tap.decals.size(); ++i)
            if (isTapRouteCurve(tap, tap.decals[i])) {
                drop[i] = 1;
                ++removed;
            }
    decal::DecalSet m;
    m.generation = tap.generation;
    m.loadSeq = tap.loadSeq;
    std::memcpy(m.worldRect, tap.worldRect, sizeof m.worldRect);
    m.textures = tap.textures;
    m.vertices = tap.vertices;
    m.areaTilesX = tap.areaTilesX;
    m.areaTilesY = tap.areaTilesY;
    m.areaChanged = tap.areaChanged;
    ComposeStats st;
    st.tapDecals = static_cast<uint32_t>(tap.decals.size());
    st.tapRemoved = static_cast<uint32_t>(removed);
    const bool walk = decal::hasWalk(tap);
    std::vector<uint32_t> kept;
    uint64_t order64 = 0;
    for (size_t i = 0; i < tap.decals.size(); ++i) {
        order64 = (std::max)(order64, static_cast<uint64_t>(tap.decals[i].listOrder) + 1);
        if (drop[i]) continue;
        kept.push_back(static_cast<uint32_t>(i));
        m.decals.push_back(tap.decals[i]);
    }
    if (order64 + nr > 0xFFFFFFFFull) return Status::fail(Err::BadLayout, "composeRoadSet: no list order left for the roads");
    const size_t nk = kept.size();
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
        struct Item {
            uint32_t cell, rank, idx;
        };
        std::vector<Item> items(nk);
        const uint64_t cells = static_cast<uint64_t>(m.walk.cols) * m.walk.rows;
        for (size_t j = 0; j < nk; ++j) {
            const uint32_t o = kept[j];
            if (tap.walk.cell[o] != decal::kWalkLarge && tap.walk.cell[o] >= cells)
                return Status::fail(Err::BadLayout, "composeRoadSet: an inconsistent walk block");
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
    std::vector<uint32_t> seq;
    const bool area = tap.areaTilesX && tap.areaTilesY &&
                      tap.areaChanged.size() == static_cast<size_t>(tap.areaTilesX) * tap.areaTilesY;
    std::vector<uint32_t> nameMap(roads.textures.size(), decal::kNoTexture);
    for (size_t t = 0; t < roads.textures.size(); ++t) {
        for (size_t k = 0; k < m.textures.size(); ++k)
            if (m.textures[k] == roads.textures[t]) {
                nameMap[t] = static_cast<uint32_t>(k);
                break;
            }
        if (nameMap[t] == decal::kNoTexture) {
            nameMap[t] = static_cast<uint32_t>(m.textures.size());
            m.textures.push_back(roads.textures[t]);
            ++st.namesAdded;
        }
    }
    const uint32_t vbase = static_cast<uint32_t>(m.vertices.size());
    m.vertices.insert(m.vertices.end(), roads.vertices.begin(), roads.vertices.end());
    uint32_t order = static_cast<uint32_t>(order64);
    for (size_t k = 0; k < nr; ++k) {
        decal::Decal d = roads.decals[k];
        for (int t = 0; t < 3; ++t)
            if (d.texture[t] != decal::kNoTexture) d.texture[t] = nameMap[d.texture[t]];
        d.firstVertex += vbase;
        d.listOrder = order++;
        d.hash = contentHash(d, m.textures, m.vertices);
        m.decals.push_back(d);
        ++st.roadDecals;
        st.roadVertices += d.vertexCount;
        if (walk) {
            const float px = (d.rect[0] + d.rect[2]) * 0.5f, py = (d.rect[1] + d.rect[3]) * 0.5f;
            m.walk.point.push_back(px);
            m.walk.point.push_back(py);
            if (d.rect[2] - d.rect[0] > decal::kLargeDecal || d.rect[3] - d.rect[1] > decal::kLargeDecal) {
                m.walk.cell.push_back(decal::kWalkLarge);
                m.walk.rank.push_back(keptLarge + st.walkLarge);
                ++st.walkLarge;
                seq.push_back(0);
            } else {
                const uint32_t cx = axisCell(px, m.walk.origin[0], m.walk.inverse[0], m.walk.cols);
                const uint32_t cy = axisCell(py, m.walk.origin[1], m.walk.inverse[1], m.walk.rows);
                const uint32_t cc = cy * m.walk.cols + cx;
                m.walk.cell.push_back(cc);
                m.walk.rank.push_back(0);
                seq.push_back(++inCell[cc]);
            }
        }
        if (area) {
            uint32_t x0, x1, y0, y1;
            if (tileSpan(d.rect[0], d.rect[2], m.areaTilesX, x0, x1) && tileSpan(d.rect[1], d.rect[3], m.areaTilesY, y0, y1))
                for (uint32_t y = y0; y <= y1; ++y)
                    for (uint32_t x = x0; x <= x1; ++x) {
                        uint8_t& b = m.areaChanged[static_cast<size_t>(y) * m.areaTilesX + x];
                        if (!b) {
                            b = 1;
                            ++st.areaBitsSet;
                        }
                    }
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
    decal::buildIndex(m);
    if (walk && !decal::hasWalk(m)) return Status::fail(Err::BadLayout, "composeRoadSet: the tap's walk did not survive");
    st.walk = walk;
    st.area = area;
    out = std::move(m);
    if (stats) *stats = st;
    return Status::Ok();
}

// ---------------------------------------------------------------------------------------------------------------------
// changes and checks

uint64_t contentHash(const decal::Decal& d, const std::vector<std::string>& names, const std::vector<decal::DecalVertex>& v)
{
    uint64_t h = kFnvBasis;
    h = fnvAdd(h, d.rect, sizeof(d.rect));
    h = fnvAdd(h, &d.priority, sizeof(d.priority));
    h = fnvAdd(h, &d.pass, sizeof(d.pass));
    for (int k = 0; k < 3; ++k) {
        if (d.texture[k] < names.size()) h = fnvAdd(h, names[d.texture[k]].c_str(), names[d.texture[k]].size() + 1);
        else h = fnvAdd(h, "\xFF", 1);
    }
    if (d.vertexCount && static_cast<uint64_t>(d.firstVertex) + d.vertexCount <= v.size())
        h = fnvAdd(h, v.data() + d.firstVertex, static_cast<size_t>(d.vertexCount) * sizeof(decal::DecalVertex));
    return h;
}

Status roadChange(const RoadDecals& before, const RoadDecals& after, RoadChange& out)
{
    out = RoadChange();
    if (before.owner.size() != before.decals.size() || after.owner.size() != after.decals.size())
        return Status::fail(Err::BadLayout, "roadChange: owner table length");
    std::unordered_map<uint64_t, int> count;
    for (const decal::Decal& d : before.decals) ++count[contentHash(d, before.textures, before.vertices)];
    std::vector<uint8_t> keepAfter(after.decals.size(), 0);
    for (size_t i = 0; i < after.decals.size(); ++i) {
        auto it = count.find(contentHash(after.decals[i], after.textures, after.vertices));
        if (it != count.end() && it->second > 0) {
            --it->second;
            keepAfter[i] = 1;
        }
    }
    // the before decals left over in count were removed
    std::unordered_map<uint64_t, int> left = count;
    for (size_t i = 0; i < before.decals.size(); ++i) {
        auto it = left.find(contentHash(before.decals[i], before.textures, before.vertices));
        if (it->second > 0) {
            --it->second;
            decal::DecalRect r;
            std::memcpy(r.v, before.decals[i].rect, sizeof r.v);
            out.rects.push_back(r);
            out.plots.push_back(before.owner[i]);
        }
    }
    for (size_t i = 0; i < after.decals.size(); ++i)
        if (!keepAfter[i]) {
            decal::DecalRect r;
            std::memcpy(r.v, after.decals[i].rect, sizeof r.v);
            out.rects.push_back(r);
            out.plots.push_back(after.owner[i]);
        }
    std::sort(out.plots.begin(), out.plots.end());
    out.plots.erase(std::unique(out.plots.begin(), out.plots.end()), out.plots.end());
    return Status::Ok();
}

LineCheck checkLines(const MapIn& m, const RoadDecals& r, float tol, const Network* net, float curveMax)
{
    LineCheck lc;
    // segments per piece, tagged with their decal; a spatial hash of 32-sample cells
    struct Seg {
        Pt a, b;
        uint32_t decal, piece;
    };
    std::vector<Seg> segs;
    uint32_t pieceId = 0;
    for (size_t k = 0; k + 1 < r.lineStart.size() && k < r.decals.size(); ++k) {
        const uint32_t s0 = r.lineStart[k], s1 = r.lineStart[k + 1];
        std::vector<Pt> cur;
        auto flush = [&]() {
            if (cur.size() >= 2) {
                ++lc.pieces;
                for (size_t i = 1; i < cur.size(); ++i) {
                    segs.push_back(Seg{cur[i - 1], cur[i], static_cast<uint32_t>(k), pieceId});
                    if (i + 1 < cur.size()) {
                        const Pt u = unit(cur[i] - cur[i - 1]), v = unit(cur[i + 1] - cur[i]);
                        if (len(cur[i] - cur[i - 1]) > 0.05f && len(cur[i + 1] - cur[i]) > 0.05f) {
                            const float dot = (std::max)(-1.0f, (std::min)(1.0f, u.x * v.x + u.y * v.y));
                            lc.maxTurnDeg = (std::max)(lc.maxTurnDeg, std::acos(dot) * 57.29578f);
                        }
                    }
                }
                ++pieceId;
            }
            cur.clear();
        };
        for (uint32_t i = s0; i < s1; ++i) {
            const float x = r.lines[2 * i], y = r.lines[2 * i + 1];
            if (std::isnan(x)) flush();
            else cur.push_back(Pt{x, y});
        }
        flush();
    }
    // dead ends: every end of a piece is either at its plot's pass point (within tol + 23 of the centre: the jitter, or
    // a stub's end) or meets the end of a piece of another plot (the shared middle of their edge, within 0.5); an end
    // anywhere else is a road that runs on past its last route plot or stops short of its neighbour: an overshoot
    {
        struct End {
            Pt p;
            uint32_t decal;
        };
        std::vector<End> ends;
        for (size_t k = 0; k + 1 < r.lineStart.size() && k < r.owner.size(); ++k) {
            bool start = true;
            Pt last{0, 0};
            for (uint32_t j = r.lineStart[k]; j < r.lineStart[k + 1]; ++j) {
                const float px = r.lines[2 * j], py = r.lines[2 * j + 1];
                if (std::isnan(px)) {
                    if (!start) ends.push_back(End{last, static_cast<uint32_t>(k)});
                    start = true;
                    continue;
                }
                if (start) ends.push_back(End{Pt{px, py}, static_cast<uint32_t>(k)});
                start = false;
                last = Pt{px, py};
            }
        }
        std::vector<uint32_t> byX(ends.size());
        std::iota(byX.begin(), byX.end(), 0u);
        std::sort(byX.begin(), byX.end(), [&](uint32_t a, uint32_t b) { return ends[a].p.x < ends[b].p.x; });
        for (const End& e : ends) {
            const uint32_t i = r.owner[e.decal];
            float sx, sy;
            plotCentre(static_cast<int>(i % m.width), static_cast<int>(i / m.width), sx, sy);
            // the pass point, or a stub's end (18 from the pass point by default), with the jitter (4 by default)
            if (len(e.p - Pt{sx, sy * kIsoY}) <= tol + 23.0f) continue;
            // across the seam of a wrapped map the neighbour's end lies one wrap period (64 x width) away
            bool met = false;
            const float period = 64.0f * static_cast<float>(m.width);
            for (int w = m.wrapX ? -1 : 0; w <= (m.wrapX ? 1 : 0) && !met; ++w) {
                const Pt q{e.p.x + w * period, e.p.y};
                auto lo = std::lower_bound(byX.begin(), byX.end(), q.x - 0.5f,
                                           [&](uint32_t a, float x) { return ends[a].p.x < x; });
                for (auto it = lo; it != byX.end() && ends[*it].p.x <= q.x + 0.5f && !met; ++it)
                    if (ends[*it].decal != e.decal && len(ends[*it].p - q) <= 0.5f) met = true;
            }
            // a branch that merges into its plot's through-line ends on that line (style Line): a join, not a loose end
            for (size_t si = 0; si < segs.size() && !met; ++si) {
                const Seg& sg = segs[si];
                if (sg.decal == e.decal || r.owner[sg.decal] != i) continue;
                const Pt ab = sg.b - sg.a;
                const float l2 = ab.x * ab.x + ab.y * ab.y;
                float t = l2 > 0 ? ((e.p.x - sg.a.x) * ab.x + (e.p.y - sg.a.y) * ab.y) / l2 : 0.0f;
                t = (std::max)(0.0f, (std::min)(1.0f, t));
                if (len(e.p - (sg.a + ab * t)) <= 0.75f) met = true;
            }
            if (!met) {
                if (!lc.overshoots) {
                    lc.firstOvershoot[0] = e.p.x;
                    lc.firstOvershoot[1] = e.p.y;
                    lc.firstOvershootPlot = i;
                }
                ++lc.overshoots;
            }
        }
    }
    std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
    auto cellKey = [](float x, float y) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(static_cast<int32_t>(std::floor(x / 32)) + 1000000)) << 32) |
               static_cast<uint32_t>(static_cast<int32_t>(std::floor(y / 32)) + 1000000);
    };
    for (uint32_t i = 0; i < segs.size(); ++i) {
        const float x0 = (std::min)(segs[i].a.x, segs[i].b.x), x1 = (std::max)(segs[i].a.x, segs[i].b.x);
        const float y0 = (std::min)(segs[i].a.y, segs[i].b.y), y1 = (std::max)(segs[i].a.y, segs[i].b.y);
        for (float x = std::floor(x0 / 32) * 32; x <= x1; x += 32)
            for (float y = std::floor(y0 / 32) * 32; y <= y1; y += 32) grid[cellKey(x, y)].push_back(i);
    }
    auto cross = [](Pt a, Pt b, Pt c, Pt d) {
        auto cr = [](Pt o, Pt p, Pt q) { return (p.x - o.x) * (q.y - o.y) - (p.y - o.y) * (q.x - o.x); };
        const float d1 = cr(c, d, a), d2 = cr(c, d, b), d3 = cr(a, b, c), d4 = cr(a, b, d);
        const float eps = 1e-2f;
        return ((d1 > eps && d2 < -eps) || (d1 < -eps && d2 > eps)) && ((d3 > eps && d4 < -eps) || (d3 < -eps && d4 > eps));
    };
    for (auto& kv : grid) {
        const std::vector<uint32_t>& v = kv.second;
        for (size_t i = 0; i < v.size(); ++i)
            for (size_t j = i + 1; j < v.size(); ++j) {
                const Seg& a = segs[v[i]];
                const Seg& b = segs[v[j]];
                if (a.decal == b.decal) continue;   // pieces of one plot meet at its pass point by design
                if (cross(a.a, a.b, b.a, b.b)) ++lc.crossings;
            }
    }
    // the straight-first rule: curved stretches and their length; runs that stop straight inside a cluster
    double total = 0, curved = 0;
    const float kCurveDegPerSample = 0.75f;
    for (size_t k = 0; k + 1 < r.lineStart.size() && k < r.owner.size(); ++k) {
        std::vector<Pt> pts;
        for (uint32_t j = r.lineStart[k]; j < r.lineStart[k + 1]; ++j)
            if (!std::isnan(r.lines[2 * j])) pts.push_back(Pt{r.lines[2 * j], r.lines[2 * j + 1]});
        if (pts.size() < 2) continue;
        // resample every 2 iso samples so that the turn per sample does not depend on the point spacing
        std::vector<Pt> q;
        std::vector<float> arc;
        resample(pts, 2.0f, q, arc);
        float run = 0;
        for (size_t i = 1; i + 1 < q.size(); ++i) {
            const float l1 = len(q[i] - q[i - 1]), l2 = len(q[i + 1] - q[i]);
            total += l1;
            const Pt u = unit(q[i] - q[i - 1]), v = unit(q[i + 1] - q[i]);
            const float turn = std::acos((std::max)(-1.0f, (std::min)(1.0f, u.x * v.x + u.y * v.y))) * 57.29578f;
            if (turn / (0.5f * (l1 + l2)) > kCurveDegPerSample) {
                run += l1;
                curved += l1;
            } else {
                if (run > curveMax) ++lc.longCurves;
                lc.maxCurve = (std::max)(lc.maxCurve, run);
                run = 0;
            }
        }
        if (q.size() >= 2) total += len(q.back() - q[q.size() - 2]);
        if (run > curveMax) ++lc.longCurves;
        lc.maxCurve = (std::max)(lc.maxCurve, run);
        if (!net) continue;
        const uint32_t i = r.owner[k];
        const uint8_t e = net->edges.size() == m.plots.size() ? net->edges[i] : 0;
        int degree = 0;
        for (int d = 0; d < kDirs; ++d) degree += (e >> d) & 1;
        if (degree != 1 || m.plots[i].city) continue;   // a city is a hub: its spokes end at its centre
        bool leftOut = false;
        for (int d = 0; d < kDirs; ++d) {
            int nx, ny;
            if (!(e & (1u << d)) && neighbour(m, static_cast<int>(i % m.width), static_cast<int>(i / m.width), d, nx, ny) &&
                m.plots[static_cast<size_t>(ny) * m.width + nx].band != kBandNone)
                leftOut = true;
        }
        if (!leftOut) continue;
        // the last 12 iso samples of the piece (it runs from the edge's middle into the plot)
        float back = 0, turnSum = 0;
        for (size_t j = q.size() - 1; j >= 2 && back < 12.0f; --j) {
            const Pt u = unit(q[j - 1] - q[j - 2]), v = unit(q[j] - q[j - 1]);
            turnSum += std::acos((std::max)(-1.0f, (std::min)(1.0f, u.x * v.x + u.y * v.y))) * 57.29578f;
            back += len(q[j] - q[j - 1]);
        }
        if (turnSum < 10.0f) ++lc.abruptEnds;
    }
    lc.straightShare = total > 0 ? static_cast<float>(1.0 - curved / total) : 0.0f;
    return lc;
}

// ---------------------------------------------------------------------------------------------------------------------
// the trees' cut mask

Status RoadFootprint::build(const MapIn& m, const RoadDecals& r, const float worldRect[4], float margin)
{
    width_ = height_ = wordsPerRow_ = 0;
    bits_.clear();
    cellsSet_ = hash_ = 0;
    if (!finite4(worldRect) || worldRect[2] < 0 || worldRect[3] < 0 || worldRect[2] < worldRect[0] || worldRect[3] < worldRect[1])
        return Status::fail(Err::BadLayout, "RoadFootprint: the world rectangle is not finite or empty");
    if (!(margin >= 0 && margin <= 16)) return Status::fail(Err::BadLayout, "RoadFootprint: margin outside 0 .. 16");
    if (r.owner.size() != r.decals.size() || (r.lineStart.size() != r.decals.size() + 1 && !(r.decals.empty() && r.lineStart.empty())))
        return Status::fail(Err::BadLayout, "RoadFootprint: the decals' tables disagree in length");
    const uint32_t w = (static_cast<uint32_t>(worldRect[2]) >> 1) + 1, h = (static_cast<uint32_t>(worldRect[3]) >> 1) + 1;
    if (w > 16384 || h > 16384) return Status::fail(Err::BadLayout, "RoadFootprint: more than 16384 cells on an axis");
    const uint32_t wpr = (w + 63) / 64;
    std::vector<uint64_t> bits(static_cast<size_t>(wpr) * h, 0);
    for (size_t k = 0; k < r.decals.size(); ++k) {
        if (r.owner[k] >= m.plots.size()) return Status::fail(Err::BadLayout, "RoadFootprint: an owner outside the map");
        const float reach = bandLook(static_cast<Band>(m.plots[r.owner[k]].band)).halfWidth + margin;
        Pt prev{0, 0};
        bool have = false;
        for (uint32_t j = r.lineStart[k]; j < r.lineStart[k + 1]; ++j) {
            if (2ull * j + 1 >= r.lines.size()) return Status::fail(Err::BadLayout, "RoadFootprint: a line past its table");
            const float px = r.lines[2 * j], py = r.lines[2 * j + 1];
            if (std::isnan(px)) {
                have = false;
                continue;
            }
            const Pt cur{px, py};
            if (have) {
                // the segment prev..cur (iso), and every cell whose sample point (2 col, 2 row) is within reach of it
                const float x0 = (std::min)(prev.x, cur.x) - reach, x1 = (std::max)(prev.x, cur.x) + reach;
                const float y0 = ((std::min)(prev.y, cur.y) - reach) / kIsoY, y1 = ((std::max)(prev.y, cur.y) + reach) / kIsoY;
                const int32_t c0 = (std::max)(0, static_cast<int32_t>(std::floor(x0 / 2)));
                const int32_t c1 = (std::min)(static_cast<int32_t>(w) - 1, static_cast<int32_t>(std::ceil(x1 / 2)));
                const int32_t r0 = (std::max)(0, static_cast<int32_t>(std::floor(y0 / 2)));
                const int32_t r1 = (std::min)(static_cast<int32_t>(h) - 1, static_cast<int32_t>(std::ceil(y1 / 2)));
                const Pt ab = cur - prev;
                const float l2 = ab.x * ab.x + ab.y * ab.y;
                for (int32_t row = r0; row <= r1; ++row)
                    for (int32_t col = c0; col <= c1; ++col) {
                        const Pt p{2.0f * col, 2.0f * row * kIsoY};
                        float t = l2 > 0 ? ((p.x - prev.x) * ab.x + (p.y - prev.y) * ab.y) / l2 : 0.0f;
                        t = (std::max)(0.0f, (std::min)(1.0f, t));
                        if (len(p - (prev + ab * t)) <= reach)
                            bits[static_cast<size_t>(row) * wpr + (static_cast<uint32_t>(col) >> 6)] |= uint64_t(1) << (col & 63);
                    }
            }
            prev = cur;
            have = true;
        }
    }
    uint64_t hsh = kFnvBasis, set = 0;
    hsh = fnvAdd(hsh, &w, sizeof w);
    hsh = fnvAdd(hsh, &h, sizeof h);
    for (uint64_t v : bits) {
        hsh = fnvAdd(hsh, &v, sizeof v);
        uint64_t x = v;
        while (x) {
            x &= x - 1;
            ++set;
        }
    }
    width_ = w;
    height_ = h;
    wordsPerRow_ = wpr;
    bits_.swap(bits);
    cellsSet_ = set;
    hash_ = hsh;
    return Status::Ok();
}

bool RoadFootprint::cell(int32_t col, int32_t row) const
{
    if (col < 0 || row < 0 || static_cast<uint32_t>(col) >= width_ || static_cast<uint32_t>(row) >= height_) return false;
    return (bits_[static_cast<size_t>(row) * wordsPerRow_ + (static_cast<uint32_t>(col) >> 6)] >> (col & 63)) & 1;
}

uint8_t RoadFootprint::bitsAt(float worldX, float worldY) const
{
    const forest::CutCell c = forest::cutCell(worldX, worldY, ox_, oy_);
    return cell(c.col, c.row) ? kRoadFootprintBit : 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// bridges (ticket 0808)

bool edgeCrossing(const MapIn& m, const Network& n, const LayoutOptions& o, int x, int y, float cx, float cy, int d,
                  float& isoX, float& isoY, float& dirX, float& dirY)
{
    if (d < 0 || d >= kDirs || x < 0 || y < 0 || static_cast<uint32_t>(x) >= m.width || static_cast<uint32_t>(y) >= m.height ||
        n.width != m.width || n.height != m.height || n.edges.size() != m.plots.size() || m.plots.size() != static_cast<size_t>(m.width) * m.height)
        return false;
    if (!(n.edges[static_cast<size_t>(y) * m.width + x] & (1u << d))) return false;
    int nx, ny;
    if (!neighbour(m, x, y, d, nx, ny)) return false;
    const Ctx c{m, n, o};
    std::vector<Pt> a;
    halfEdge(c, x, y, Pt{cx, cy}, d, 24, a);
    if (a.size() < 2) return false;
    const Pt mid = a.back();
    const Pt dir = unit(mid - a[a.size() - 2]);
    isoX = mid.x;
    isoY = mid.y;
    dirX = dir.x;
    dirY = dir.y;
    return std::isfinite(isoX) && std::isfinite(isoY);
}

Status fitBridgeMesh(const uint8_t* vb, size_t bytes, uint32_t stride, BridgeFit& out)
{
    out = BridgeFit();
    if (!vb || (stride != 20 && stride != 32) || !bytes || bytes % stride)
        return Status::fail(Err::BadLayout, "fitBridgeMesh: no data, or a vertex buffer not of stride 20 or 32");
    const size_t nv = bytes / stride;
    std::vector<float> px(nv), py(nv), pz(nv);
    for (size_t i = 0; i < nv; ++i) {
        const uint8_t* p = vb + i * stride;
        float v[3];
        if (stride == 20) {
            for (int k = 0; k < 3; ++k) {
                uint16_t h;
                std::memcpy(&h, p + 2 * k, 2);
                v[k] = bake::halfToFloat(h);
            }
        } else {
            std::memcpy(v, p, 12);
        }
        if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2]))
            return Status::fail(Err::BadLayout, "fitBridgeMesh: a position is not finite");
        px[i] = v[0];
        py[i] = v[1];
        pz[i] = v[2];
    }
    double mx = 0, my = 0, mz = 0;
    for (size_t i = 0; i < nv; ++i) {
        mx += px[i];
        my += py[i];
        mz += pz[i];
    }
    mx /= nv;
    my /= nv;
    mz /= nv;
    double sxx = 0, sxy = 0, syy = 0;
    for (size_t i = 0; i < nv; ++i) {
        const double dx = px[i] - mx, dy = py[i] - my;
        sxx += dx * dx;
        sxy += dx * dy;
        syy += dy * dy;
    }
    const double th = 0.5 * std::atan2(2.0 * sxy, sxx - syy);
    double ax = std::cos(th), ay = std::sin(th);
    if (ax < 0 || (ax == 0 && ay < 0)) {
        ax = -ax;
        ay = -ay;
    }
    double a0 = 1e30, a1 = -1e30, b0 = 1e30, b1 = -1e30;
    for (size_t i = 0; i < nv; ++i) {
        const double a = px[i] * ax + py[i] * ay, b = -px[i] * ay + py[i] * ax;
        a0 = (std::min)(a0, a);
        a1 = (std::max)(a1, a);
        b0 = (std::min)(b0, b);
        b1 = (std::max)(b1, b);
    }
    const double am = 0.5 * (a0 + a1), bm = 0.5 * (b0 + b1);
    BridgeFit f;
    f.cx = static_cast<float>(am * ax - bm * ay);
    f.cy = static_cast<float>(am * ay + bm * ax);
    f.cz = static_cast<float>(mz);
    f.ax = static_cast<float>(ax);
    f.ay = static_cast<float>(ay);
    f.length = static_cast<float>(a1 - a0);
    f.width = static_cast<float>(b1 - b0);
    f.vertices = static_cast<uint32_t>(nv);
    if (!(f.length > f.width))
        return Status::fail(Err::BadLayout, "fitBridgeMesh: the mesh has no long axis (length " + std::to_string(f.length) +
                                                ", width " + std::to_string(f.width) + ")");
    out = f;
    return Status::Ok();
}

BridgePlacement placeBridge(const MapIn& m, const Network& n, const LayoutOptions& o, const float rowsIn[16],
                            const BridgeFit& fit, float ox, float oy)
{
    BridgePlacement p;
    for (int k = 0; k < 16; ++k) p.rows[k] = rowsIn[k];
    if (!m.width || !m.height || m.plots.size() != static_cast<size_t>(m.width) * m.height || n.edges.size() != m.plots.size() ||
        n.width != m.width || n.height != m.height)
        return p;
    for (int k = 0; k < 16; ++k)
        if (!std::isfinite(rowsIn[k])) return p;
    const float* r = rowsIn;
    // the mesh's centre and long axis in the world (row vectors: world = x row0 + y row1 + z row2 + row3)
    const float wx = fit.cx * r[0] + fit.cy * r[4] + fit.cz * r[8] + r[12];
    const float wy = fit.cx * r[1] + fit.cy * r[5] + fit.cz * r[9] + r[13];
    const Pt h = unit(Pt{fit.ax * r[0] + fit.ay * r[4], fit.ax * r[1] + fit.ay * r[5]});
    const Pt I{wx + ox, wy + oy};
    if (!std::isfinite(I.x) || !std::isfinite(I.y)) return p;
    // the plot whose centre is nearest (column unwrapped: the bridge's own wrap copy), then its edge middle nearest
    const int ry = static_cast<int>(std::floor((I.y / kIsoY - 128.0f) / 64.0f + 0.5f));
    float best = (std::numeric_limits<float>::max)();
    int bx = 0, by = -1;
    for (int y = ry - 1; y <= ry + 1; ++y) {
        if (y < 0 || y >= static_cast<int>(m.height)) continue;
        const float rowOff = 32.0f + 32.0f * static_cast<float>(y & 1);
        const int cg = static_cast<int>(std::floor((I.x - rowOff) / 64.0f + 0.5f));
        for (int x = cg - 1; x <= cg + 1; ++x) {
            if (!m.wrapX && (x < 0 || x >= static_cast<int>(m.width))) continue;
            const Pt C{64.0f * static_cast<float>(x) + rowOff, (64.0f * static_cast<float>(y) + 128.0f) * kIsoY};
            const float d2 = (C.x - I.x) * (C.x - I.x) + (C.y - I.y) * (C.y - I.y);
            if (d2 < best) {
                best = d2;
                bx = x;
                by = y;
            }
        }
    }
    if (by < 0) return p;
    const Pt C{64.0f * static_cast<float>(bx) + 32.0f + 32.0f * static_cast<float>(by & 1), (64.0f * static_cast<float>(by) + 128.0f) * kIsoY};
    const int xw = m.wrapX ? wrapX(m, bx) : bx;
    int bd = -1, qx = 0, qy = 0;
    float bdist = (std::numeric_limits<float>::max)();
    for (int d = 0; d < kDirs; ++d) {
        int nx, ny;
        if (!neighbour(m, xw, by, d, nx, ny)) continue;
        const Pt mid = C + Pt{kOffX[d] * 0.5f, kOffY[d] * 0.5f};
        const float dd = len(I - mid);
        if (dd < bdist) {
            bdist = dd;
            bd = d;
            qx = nx;
            qy = ny;
        }
    }
    if (bd < 0 || bdist > kBridgeEdgeReach) return p;
    const uint64_t ka = (static_cast<uint64_t>(by) * m.width + xw) * 6 + bd;
    const uint64_t kb = (static_cast<uint64_t>(qy) * m.width + qx) * 6 + (bd + 3) % 6;
    const uint64_t key = (std::min)(ka, kb);
    p.edgeKey = key + 1;
    p.plot = static_cast<uint32_t>(key / 6);
    p.dir = static_cast<uint8_t>(key % 6);
    p.edgeDist = bdist;
    float X, Y, dx, dy;
    if (!edgeCrossing(m, n, o, xw, by, C.x, C.y, bd, X, Y, dx, dy)) {
        p.action = BridgeAction::Drop;
        return p;
    }
    // the smaller turn that lays the mesh's axis on our heading
    double turn = std::atan2(static_cast<double>(dy), static_cast<double>(dx)) - std::atan2(static_cast<double>(h.y), static_cast<double>(h.x));
    const double pi = 3.14159265358979323846;
    while (turn > pi / 2) turn -= pi;
    while (turn <= -pi / 2) turn += pi;
    const float c = static_cast<float>(std::cos(turn)), s = static_cast<float>(std::sin(turn));
    const float tx = X - ox, ty = Y - oy;   // our crossing in the world
    float* q = p.rows;
    for (int i = 0; i < 3; ++i) {   // R' = R Rz (row vectors: rotate after the mesh's own rows)
        const float a = r[4 * i], b = r[4 * i + 1];
        q[4 * i] = a * c - b * s;
        q[4 * i + 1] = a * s + b * c;
    }
    const float ex = r[12] - wx, ey = r[13] - wy;   // row 3: the old origin about the old centre, turned, onto our crossing
    q[12] = ex * c - ey * s + tx;
    q[13] = ex * s + ey * c + ty;
    p.action = BridgeAction::Move;
    p.shift = len(Pt{tx - wx, ty - wy});
    p.turnDeg = static_cast<float>(turn * 180.0 / pi);
    return p;
}

}  // namespace roads
}  // namespace extras
}  // namespace c5r
