// render/tests/extras/roads_test.cpp - FixRoadLayout's generator (epic 0454 X3, ticket 4933; docs/extras/roads.md):
// the band rule, the ring rule on hand-made and random networks (positive and negative), every route plot drawn, dead
// ends ending at their centre, no crossings, determinism, the local re-lay of a one-plot change, the empty and invalid
// inputs (E6: the caller keeps the tap's set), composing with a tap set (route curves out, ours in, walk and area kept)
// and every error path. Inputs are made by the tests (no game files).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include "c5r/bake/grid.h"
#include "c5r/decal/decal.h"
#include "c5r/extras/roads.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::extras::roads;

namespace {

const Style kStyles[4] = {Style::Straight, Style::Spline, Style::SplineJitter, Style::Line};

MapIn blank(uint32_t w, uint32_t h, bool wrap = false)
{
    MapIn m;
    m.width = w;
    m.height = h;
    m.wrapX = wrap;
    m.plots.assign(static_cast<size_t>(w) * h, PlotIn());
    return m;
}

void setBand(MapIn& m, int x, int y, Band b) { m.plots[static_cast<size_t>(y) * m.width + x].band = b; }

int edgeCount(const Network& n)
{
    int c = 0;
    for (uint8_t e : n.edges)
        for (int d = 0; d < 6; ++d) c += (e >> d) & 1;
    return c / 2;
}

// hex distance between two plots (offset rows, odd rows shifted right), no wrap
int hexDist(int x0, int y0, int x1, int y1)
{
    auto cube = [](int x, int y, int& q, int& r) {
        q = x - (y - (y & 1)) / 2;
        r = y;
    };
    int q0, r0, q1, r1;
    cube(x0, y0, q0, r0);
    cube(x1, y1, q1, r1);
    const int dq = q1 - q0, dr = r1 - r0;
    return (std::abs(dq) + std::abs(dr) + std::abs(dq + dr)) / 2;
}

// every plot within `r` of (cx, cy) set to the band
void disc(MapIn& m, int cx, int cy, int r, Band b)
{
    for (int y = 0; y < static_cast<int>(m.height); ++y)
        for (int x = 0; x < static_cast<int>(m.width); ++x)
            if (hexDist(cx, cy, x, y) <= r) setBand(m, x, y, b);
}

// random walks of routes (a fixed LCG): chains, junctions, corners and some thick clusters
MapIn randomMap(uint32_t w, uint32_t h, uint32_t seed, int walks, int steps)
{
    MapIn m = blank(w, h);
    uint32_t s = seed;
    auto rnd = [&]() {
        s = s * 1664525u + 1013904223u;
        return s >> 8;
    };
    for (int k = 0; k < walks; ++k) {
        int x = static_cast<int>(rnd() % w), y = static_cast<int>(rnd() % h);
        const Band b = (rnd() % 3 == 0) ? kBandRail : kBandRoad;
        int d = static_cast<int>(rnd() % 6);
        for (int i = 0; i < steps; ++i) {
            setBand(m, x, y, b);
            if (rnd() % 4 == 0) d = (d + (rnd() % 2 ? 1 : 5)) % 6;
            int nx, ny;
            if (!neighbour(m, x, y, d, nx, ny)) {
                d = (d + 3) % 6;
                continue;
            }
            x = nx;
            y = ny;
        }
    }
    // a few thick blobs (dense railroad yards)
    for (int k = 0; k < 4; ++k) disc(m, static_cast<int>(rnd() % w), static_cast<int>(rnd() % h), 1 + static_cast<int>(rnd() % 2), kBandRail);
    for (int k = 0; k < 6; ++k) m.plots[(rnd() % (w * h))].city = 1;
    for (size_t i = 0; i < m.plots.size(); ++i)
        if (m.plots[i].city && m.plots[i].band == kBandNone) m.plots[i].city = 0;
    return m;
}

bool sameDecals(const RoadDecals& a, const RoadDecals& b)
{
    if (a.decals.size() != b.decals.size() || a.vertices.size() != b.vertices.size() || a.owner != b.owner) return false;
    if (std::memcmp(a.vertices.data(), b.vertices.data(), a.vertices.size() * sizeof(decal::DecalVertex))) return false;
    for (size_t i = 0; i < a.decals.size(); ++i)
        if (a.decals[i].hash != b.decals[i].hash || std::memcmp(a.decals[i].rect, b.decals[i].rect, 16)) return false;
    return true;
}

}  // namespace

C5R_TEST(band_from_feed)
{
    CHECK_EQ(bandFromFeed(-1, 0, 0), kBandNone);
    CHECK_EQ(bandFromFeed(0, 0, 0), kBandRoad);
    CHECK_EQ(bandFromFeed(1, 1, 0), kBandRail);
    CHECK_EQ(bandFromFeed(0, 2, 0), kBandRoadPillaged);
    CHECK_EQ(bandFromFeed(1, 3, 0), kBandRailPillaged);
    CHECK_EQ(bandFromFeed(0, 4, 0), kBandNone);
    CHECK_EQ(bandFromFeed(1, 5, 0), kBandNone);
    CHECK_EQ(bandFromFeed(0, 6, 0), kBandRoadTrade);
    CHECK_EQ(bandFromFeed(1, 7, 0), kBandRailTrade);
    CHECK_EQ(bandFromFeed(0, 8, 0), kBandNone);
    CHECK_EQ(bandFromFeed(0, -1, 0), kBandRoad);
    CHECK_EQ(bandFromFeed(1, -1, 4), kBandRailPillaged);
    CHECK_EQ(bandFromFeed(0, 0, 4), kBandRoadPillaged);
}

C5R_TEST(grid_neighbours)
{
    // odd rows shifted right: (2,1)'s NE is (3,2), (2,2)'s NE is (2,3); every direction and its opposite agree
    MapIn m = blank(8, 6);
    int nx, ny;
    CHECK(neighbour(m, 2, 1, 1, nx, ny) && nx == 3 && ny == 2);
    CHECK(neighbour(m, 2, 2, 1, nx, ny) && nx == 2 && ny == 3);
    for (int y = 0; y < 6; ++y)
        for (int x = 0; x < 8; ++x)
            for (int d = 0; d < 6; ++d) {
                int bx, by;
                if (!neighbour(m, x, y, d, nx, ny)) continue;
                CHECK(neighbour(m, nx, ny, (d + 3) % 6, bx, by) && bx == x && by == y);
                float ax, ay, cx, cy;
                plotCentre(x, y, ax, ay);
                plotCentre(nx, ny, cx, cy);
                const float dist = std::sqrt((cx - ax) * (cx - ax) + (cy - ay) * (cy - ay) * kIsoY * kIsoY);
                CHECK(std::fabs(dist - 64.0f) < 0.05f);
            }
    CHECK(!neighbour(m, 7, 3, 0, nx, ny));   // no wrap
    MapIn w = blank(8, 6, true);
    CHECK(neighbour(w, 7, 3, 0, nx, ny) && nx == 0 && ny == 3);
    CHECK(!neighbour(w, 3, 0, 4, nx, ny));   // off the bottom row
}

C5R_TEST(triangle_is_not_a_ring)
{
    MapIn m = blank(10, 10);
    setBand(m, 4, 4, kBandRoad);
    setBand(m, 5, 4, kBandRoad);
    setBand(m, 4, 5, kBandRoad);   // (4,4)'s NE is (4,5): a triangle of three route plots
    Network n;
    CHECK_OK(buildNetwork(m, n));
    CHECK_EQ(n.candidateEdges, 3u);
    CHECK_EQ(edgeCount(n), 2);
    CHECK_EQ(n.rejected, 1u);
    CHECK_EQ(countEmptyFaces(m, n), 0u);
}

C5R_TEST(ring_around_a_plot_without_route_is_kept)
{
    MapIn m = blank(12, 12);
    for (int d = 0; d < 6; ++d) {
        int nx, ny;
        CHECK(neighbour(m, 6, 6, d, nx, ny));
        setBand(m, nx, ny, kBandRoad);
    }
    Network n;
    CHECK_OK(buildNetwork(m, n));
    CHECK_EQ(edgeCount(n), 6);   // the honest ring around (6,6), which has no route
    CHECK_EQ(n.rejected, 0u);
    CHECK_EQ(countEmptyFaces(m, n), 0u);
    // with the centre a route plot too, the flower becomes a tree (no ring around a route plot)
    setBand(m, 6, 6, kBandRoad);
    CHECK_OK(buildNetwork(m, n));
    CHECK_EQ(edgeCount(n), 6);
    CHECK_EQ(countEmptyFaces(m, n), 0u);
    // a city in the middle: the six spokes (a star) win
    m.plots[6 * 12 + 6].city = 1;
    CHECK_OK(buildNetwork(m, n));
    CHECK_EQ(n.edges[6 * 12 + 6], 0x3F);
}

C5R_TEST(filled_disc_is_a_tree)
{
    for (int r = 1; r <= 5; ++r) {
        MapIn m = blank(24, 24);
        disc(m, 11, 11, r, kBandRail);
        int routes = 0;
        for (const PlotIn& p : m.plots) routes += p.band != kBandNone;
        Network n;
        CHECK_OK(buildNetwork(m, n));
        CHECK_EQ(edgeCount(n), routes - 1);   // connected and ring free: a spanning tree
        CHECK_EQ(countEmptyFaces(m, n), 0u);
        for (size_t i = 0; i < m.plots.size(); ++i) CHECK((m.plots[i].band != kBandNone) == (n.edges[i] != 0));
    }
}

C5R_TEST(ring_check_negative)
{
    // the checker must see a ring that the rule forbids: a hand-made network with the triangle closed
    MapIn m = blank(10, 10);
    setBand(m, 4, 4, kBandRoad);
    setBand(m, 5, 4, kBandRoad);
    setBand(m, 4, 5, kBandRoad);
    Network n;
    CHECK_OK(buildNetwork(m, n));
    n.edges[4 * 10 + 4] = static_cast<uint8_t>((1 << 0) | (1 << 1));
    n.edges[4 * 10 + 5] = static_cast<uint8_t>((1 << 3) | (1 << 2));
    n.edges[5 * 10 + 4] = static_cast<uint8_t>((1 << 4) | (1 << 5));
    std::vector<uint32_t> where;
    CHECK_EQ(countEmptyFaces(m, n, &where), 1u);
    CHECK_EQ(where.size(), 3u);
    // and a rhombus of two triangles with only its rim drawn (four plots, a ring around nothing)
    MapIn r = blank(10, 10);
    setBand(r, 4, 4, kBandRoad);
    setBand(r, 5, 4, kBandRoad);
    setBand(r, 4, 5, kBandRoad);
    setBand(r, 5, 5, kBandRoad);   // (4,4) E (5,4), NE (4,5); (5,4) NE (5,5); (4,5) E (5,5): (4,5)-(5,4) is the diagonal
    Network nr;
    CHECK_OK(buildNetwork(r, nr));
    CHECK_EQ(countEmptyFaces(r, nr), 0u);
    CHECK_EQ(edgeCount(nr), 3);
    Network rim = nr;
    std::fill(rim.edges.begin(), rim.edges.end(), static_cast<uint8_t>(0));
    auto join = [&](int x, int y, int d) {
        int nx, ny;
        neighbour(r, x, y, d, nx, ny);
        rim.edges[static_cast<size_t>(y) * 10 + x] |= static_cast<uint8_t>(1 << d);
        rim.edges[static_cast<size_t>(ny) * 10 + nx] |= static_cast<uint8_t>(1 << ((d + 3) % 6));
    };
    join(4, 4, 0);
    join(4, 4, 1);
    join(5, 4, 1);
    join(4, 5, 0);
    CHECK_EQ(countEmptyFaces(r, rim), 1u);
}

C5R_TEST(random_maps_ring_free_and_complete)
{
    for (uint32_t seed = 1; seed <= 12; ++seed) {
        MapIn m = randomMap(60, 40, seed, 30, 40);
        m.wrapX = (seed % 2) == 0;
        Network n;
        CHECK_OK(buildNetwork(m, n));
        CHECK_EQ(countEmptyFaces(m, n), 0u);
        // every route plot with a route neighbour is joined to one
        for (int y = 0; y < 40; ++y)
            for (int x = 0; x < 60; ++x) {
                if (m.plots[static_cast<size_t>(y) * 60 + x].band == kBandNone) continue;
                bool hasNb = false;
                for (int d = 0; d < 6; ++d) {
                    int nx, ny;
                    if (neighbour(m, x, y, d, nx, ny) && m.plots[static_cast<size_t>(ny) * 60 + nx].band != kBandNone) hasNb = true;
                }
                CHECK(hasNb == (n.edges[static_cast<size_t>(y) * 60 + x] != 0));
            }
        for (Style s : kStyles) {
            LayoutOptions o;
            o.style = s;
            RoadDecals r;
            CHECK_OK(layOut(m, n, o, r));
            const LineCheck lc = checkLines(m, r);
            if (lc.crossings || lc.overshoots)
            {
                const uint32_t i = lc.firstOvershootPlot;
                float cx, cy;
                plotCentre(static_cast<int>(i % 60), static_cast<int>(i / 60), cx, cy);
                std::printf("  seed %u style %s: %u crossings, %u overshoots; first at %.2f,%.2f, plot %u,%u (centre %.2f,%.2f, "
                            "edges 0x%02x)\n", seed, styleName(s), lc.crossings, lc.overshoots, lc.firstOvershoot[0],
                            lc.firstOvershoot[1], i % 60, i / 60, cx, cy * kIsoY, n.edges[i]);
                for (size_t k = 0; k < r.decals.size(); ++k) {
                    const uint32_t ow = r.owner[k];
                    if (ow / 60 != i / 60 || (ow % 60 + 1 < i % 60) || ow % 60 > i % 60 + 1) continue;
                    const uint32_t s0 = r.lineStart[k], s1 = r.lineStart[k + 1];
                    std::printf("    decal %zu plot %u,%u: %u points, first %.3f,%.3f last %.3f,%.3f\n", k, ow % 60, ow / 60,
                                s1 - s0, r.lines[2 * s0], r.lines[2 * s0 + 1], r.lines[2 * (s1 - 2)], r.lines[2 * (s1 - 2) + 1]);
                }
            }
            CHECK_EQ(lc.crossings, 0u);
            CHECK_EQ(lc.overshoots, 0u);
            if (s == Style::Line) {
                // the straight-first rule: no run stops straight inside a cluster, no long curve
                const LineCheck lr = checkLines(m, r, 1.0f, &n, 40.0f);
                if (lr.abruptEnds || lr.longCurves)
                    std::printf("  seed %u line: %u abrupt ends, %u long curves (longest %.1f)\n", seed, lr.abruptEnds,
                                lr.longCurves, lr.maxCurve);
                CHECK_EQ(lr.abruptEnds, 0u);
                CHECK_EQ(lr.longCurves, 0u);
                CHECK_EQ(lr.crossings, 0u);
                CHECK_EQ(lr.overshoots, 0u);
            }
            // every route plot owns at least one decal (one per piece), and no other plot does
            std::set<uint32_t> owners(r.owner.begin(), r.owner.end());
            for (uint32_t i = 0; i < m.plots.size(); ++i) CHECK((m.plots[i].band != kBandNone) == (owners.count(i) == 1));
            for (const decal::Decal& d : r.decals) {
                CHECK(d.vertexCount > 0 && d.vertexCount % 3 == 0);
                CHECK(d.rect[0] < d.rect[2] && d.rect[1] < d.rect[3]);
                CHECK(std::fabs(d.priority - 0.7f) < 1e-6f);
            }
        }
    }
}

C5R_TEST(line_check_negative)
{
    // the checker must see a crossing and loose ends: two pieces of two plots crossing in an X far from any centre
    MapIn m = blank(4, 4);
    RoadDecals r;
    r.decals.resize(2);
    r.owner = {0, 1};
    const float nan = std::nanf("");
    r.lines = {300, 300, 340, 340, nan, nan, 300, 340, 340, 300, nan, nan};
    r.lineStart = {0, 3, 6};
    const LineCheck lc = checkLines(m, r);
    CHECK(lc.crossings >= 1);
    CHECK_EQ(lc.overshoots, 4u);
    CHECK_EQ(lc.pieces, 2u);
    // the same two pieces meeting end to end, away from the centres: no crossing, and the shared ends are not loose
    RoadDecals s = r;
    s.lines = {300, 300, 340, 340, nan, nan, 340, 340, 380, 300, nan, nan};
    const LineCheck ls = checkLines(m, s);
    CHECK_EQ(ls.crossings, 0u);
    CHECK_EQ(ls.overshoots, 2u);
    CHECK(ls.maxTurnDeg < 1.0f);   // each piece is straight; turns are measured within a piece
}

C5R_TEST(straight_first_rule)
{
    // a corner of three route plots: the ring rule keeps two edges, so two plots end with a route neighbour left out.
    // Style (a) stops both runs straight (the checker must count them: the negative); style Line ends them in curves.
    MapIn m = blank(10, 10);
    setBand(m, 4, 4, kBandRail);
    setBand(m, 5, 4, kBandRail);
    setBand(m, 4, 5, kBandRail);
    Network n;
    CHECK_OK(buildNetwork(m, n));
    LayoutOptions o;
    o.style = Style::Straight;
    RoadDecals r;
    CHECK_OK(layOut(m, n, o, r));
    CHECK_EQ(checkLines(m, r, 1.0f, &n).abruptEnds, 2u);
    o.style = Style::Line;
    CHECK_OK(layOut(m, n, o, r));
    LineCheck lc = checkLines(m, r, 1.0f, &n);
    CHECK_EQ(lc.abruptEnds, 0u);
    CHECK_EQ(lc.overshoots, 0u);
    CHECK_EQ(lc.crossings, 0u);
    // a long sweeping spline is a long curve for the checker (negative), the Line style's fillets are short
    MapIn s = blank(30, 20);
    int x = 3, y = 3;
    for (int k = 0; k < 24; ++k) {   // a zig-zag of 60-degree turns at every plot
        setBand(s, x, y, kBandRoad);
        int nx, ny;
        neighbour(s, x, y, k % 2 == 0 ? 0 : 1, nx, ny);
        x = nx;
        y = ny;
    }
    Network ns;
    CHECK_OK(buildNetwork(s, ns));
    o.style = Style::Spline;
    o.tension = 1.6f;
    CHECK_OK(layOut(s, ns, o, r));
    lc = checkLines(s, r, 1.0f, &ns, 40.0f);
    std::printf("  straight_first_rule: spline tension 1.6: %u long curves (longest %.1f), straight share %.2f\n", lc.longCurves,
                lc.maxCurve, lc.straightShare);
    CHECK(lc.longCurves > 0);
    o = LayoutOptions();
    o.style = Style::Line;
    CHECK_OK(layOut(s, ns, o, r));
    lc = checkLines(s, r, 1.0f, &ns, 40.0f);
    CHECK_EQ(lc.longCurves, 0u);
    CHECK(lc.straightShare > 0.6f);
    // a straight run through a cluster continues (the straight-first rank): a row of 7 with a parallel row of 7 above
    MapIn rows = blank(20, 10);
    for (int i = 0; i < 7; ++i) {
        setBand(rows, 5 + i, 4, kBandRail);
        setBand(rows, 5 + i, 5, kBandRoad);
    }
    Network nr;
    CHECK_OK(buildNetwork(rows, nr));
    for (int i = 0; i < 6; ++i) CHECK((nr.edges[4 * 20 + 5 + i] & 1u) != 0);   // the whole lower row joined east to west
    CHECK_EQ(countEmptyFaces(rows, nr), 0u);
}

C5R_TEST(footprint_of_the_roads)
{
    // a west-east road of six plots on row 3: the cells along its centre line are set, cells 12 samples off are not;
    // every style gives a footprint; empty input gives none; bad inputs are refused with the mask empty
    MapIn m = blank(12, 8);
    for (int x = 2; x <= 7; ++x) setBand(m, x, 3, kBandRoad);
    const float world[4] = {0, 0, 64.0f * 12 - 1, 64.0f * 12 - 1};
    for (Style s : kStyles) {
        LayoutOptions o;
        o.style = s;
        Network n;
        RoadDecals r;
        CHECK_OK(produce(m, o, n, r));
        RoadFootprint fp;
        CHECK_OK(fp.build(m, r, world, 3.0f));
        float cx, cy;
        plotCentre(4, 3, cx, cy);   // 64 * 4 + 32 + 32, 64 * 3 + 128 (row 3 is odd)
        const int32_t row = static_cast<int32_t>(cy) / 2;
        for (int32_t col = static_cast<int32_t>(cx - 40) / 2; col <= static_cast<int32_t>(cx + 40) / 2; ++col) {
            CHECK(fp.cell(col, row));
            CHECK(!fp.cell(col, row + 7));   // 14 samples (12.1 iso) off the line
        }
        CHECK(fp.cellsSet() > 0);
        // bitsAt through the cut rule (ox, oy as the field's): the centre's world point is cut
        fp.setFrame(400.0f, 300.0f);
        CHECK_EQ(fp.bitsAt(cx - 400.0f, cy * kIsoY - 300.0f), kRoadFootprintBit);
        CHECK_EQ(fp.bitsAt(cx - 400.0f, (cy + 30) * kIsoY - 300.0f), static_cast<uint8_t>(0));
    }
    Network n;
    RoadDecals empty;
    RoadFootprint fp;
    CHECK_OK(fp.build(blank(12, 8), empty, world, 3.0f));
    CHECK_EQ(fp.cellsSet(), 0u);
    const float bad[4] = {0, 0, std::nanf(""), 10};
    CHECK_ERR(fp.build(m, empty, bad, 3.0f), Err::BadLayout);
    CHECK_EQ(fp.width(), 0u);
    RoadDecals r;
    CHECK_OK(produce(m, LayoutOptions(), n, r));
    RoadDecals broken = r;
    broken.owner.pop_back();
    CHECK_ERR(fp.build(m, broken, world, 3.0f), Err::BadLayout);
    CHECK_ERR(fp.build(m, r, world, 20.0f), Err::BadLayout);
    // the mask moves with the layout only: the same input gives the same hash
    RoadFootprint a, b;
    CHECK_OK(a.build(m, r, world, 3.0f));
    CHECK_OK(b.build(m, r, world, 3.0f));
    CHECK_EQ(a.hash(), b.hash());
}

C5R_TEST(dead_end_stops_at_its_centre)
{
    MapIn m = blank(12, 8);
    for (int x = 2; x <= 7; ++x) setBand(m, x, 3, kBandRoad);
    for (Style s : kStyles) {
        LayoutOptions o;
        o.style = s;
        Network n;
        RoadDecals r;
        CHECK_OK(produce(m, o, n, r));
        // the last plot's piece ends at its pass point: its line's last point within the jitter of the centre
        for (size_t k = 0; k < r.decals.size(); ++k) {
            const uint32_t i = r.owner[k];
            if (i != 3 * 12 + 7 && i != 3 * 12 + 2) continue;
            float cx, cy;
            plotCentre(static_cast<int>(i % 12), 3, cx, cy);
            const uint32_t last = r.lineStart[k + 1] - 2;   // the point before the closing NaN pair
            const float dx = r.lines[2 * last] - cx, dy = r.lines[2 * last + 1] - cy * kIsoY;
            CHECK(std::sqrt(dx * dx + dy * dy) <= o.jitter + 0.01f);
            // and nothing of the strip lies beyond the centre by more than the half width (no overshoot to the far edge)
            const decal::Decal& d = r.decals[k];
            if (i == 3 * 12 + 7) CHECK(d.rect[2] <= cx + bandLook(kBandRoad).halfWidth + o.jitter + 0.5f);
            else CHECK(d.rect[0] >= cx - bandLook(kBandRoad).halfWidth - o.jitter - 0.5f);
        }
        CHECK_EQ(checkLines(m, r).overshoots, 0u);
    }
}

C5R_TEST(deterministic)
{
    MapIn m = randomMap(80, 52, 99, 40, 50);
    for (Style s : kStyles) {
        LayoutOptions o;
        o.style = s;
        Network n1, n2;
        RoadDecals a, b;
        CHECK_OK(produce(m, o, n1, a));
        CHECK_OK(produce(m, o, n2, b));
        CHECK(n1.edges == n2.edges);
        CHECK(sameDecals(a, b));
        // the same map handed over in another order of construction (a copy) gives the same bytes
        MapIn c = m;
        RoadDecals cc;
        Network nc;
        CHECK_OK(produce(c, o, nc, cc));
        CHECK(sameDecals(a, cc));
    }
}

C5R_TEST(local_relay)
{
    // a one-plot change re-lays only plots near it: within two plots when no route triangle is near (chains), within
    // the triangle cluster's reach otherwise; the largest reach is printed (the hand-back's figure)
    MapIn m = randomMap(70, 46, 7, 36, 45);
    LayoutOptions o;
    o.style = Style::SplineJitter;
    Network n;
    RoadDecals base;
    CHECK_OK(produce(m, o, n, base));
    int worst = 0, worstChain = 0, changes = 0;
    for (uint32_t k = 0; k < 400; ++k) {
        const uint32_t i = (k * 2654435761u) % static_cast<uint32_t>(m.plots.size());
        const int x = static_cast<int>(i % m.width), y = static_cast<int>(i / m.width);
        // near a route? only changes where something is drawn nearby are interesting
        bool near2 = false, tri = false;
        for (int yy = y - 3; yy <= y + 3; ++yy)
            for (int xx = x - 3; xx <= x + 3; ++xx) {
                if (yy < 0 || xx < 0 || yy >= static_cast<int>(m.height) || xx >= static_cast<int>(m.width)) continue;
                if (hexDist(x, y, xx, yy) > 3 || m.plots[static_cast<size_t>(yy) * m.width + xx].band == kBandNone) continue;
                near2 = true;
                for (int d = 0; d < 2; ++d) {
                    int ax, ay, bx, by;
                    if (neighbour(m, xx, yy, d, ax, ay) && neighbour(m, xx, yy, d + 1, bx, by) &&
                        m.plots[static_cast<size_t>(ay) * m.width + ax].band != kBandNone &&
                        m.plots[static_cast<size_t>(by) * m.width + bx].band != kBandNone)
                        tri = true;
                }
            }
        if (!near2) continue;
        MapIn m2 = m;
        PlotIn& p = m2.plots[i];
        p.band = p.band == kBandNone ? kBandRoad : kBandNone;
        p.city = 0;
        // the changed plot's own triangles count too
        Network n2;
        RoadDecals r2;
        CHECK_OK(produce(m2, o, n2, r2));
        CHECK_EQ(countEmptyFaces(m2, n2), 0u);
        RoadChange ch;
        CHECK_OK(roadChange(base, r2, ch));
        int reach = 0;
        for (uint32_t q : ch.plots) reach = (std::max)(reach, hexDist(x, y, static_cast<int>(q % m.width), static_cast<int>(q / m.width)));
        ++changes;
        worst = (std::max)(worst, reach);
        if (!tri) {
            worstChain = (std::max)(worstChain, reach);
            CHECK(reach <= 2);
        }
        CHECK(!ch.plots.empty() || p.band == kBandNone);
    }
    std::printf("  local_relay: %d changes, reach at most %d plots (%d where no route triangle is within 3)\n", changes, worst, worstChain);
    CHECK(changes > 50);
}

C5R_TEST(empty_and_invalid_input)
{
    // no route: nothing drawn, and composing nothing with the tap's routes kept leaves the tap's set as it was (E6)
    MapIn m = blank(16, 10);
    Network n;
    RoadDecals r;
    CHECK_OK(produce(m, LayoutOptions(), n, r));
    CHECK(r.empty());
    decal::DecalSet tap;
    tap.worldRect[2] = 64 * 16 - 1;
    tap.worldRect[3] = 64 * 14 - 1;
    tap.textures = {"RoadsandRails_new_D.dds", "other_d.dds"};
    tap.vertices.resize(6);
    decal::Decal d;
    d.rect[0] = 10;
    d.rect[1] = 10;
    d.rect[2] = 50;
    d.rect[3] = 50;
    d.priority = 0.7f;
    d.texture[0] = 0;
    d.vertexCount = 3;
    tap.decals.push_back(d);
    d.priority = 0.5f;
    d.texture[0] = 1;
    d.firstVertex = 3;
    d.listOrder = 1;
    tap.decals.push_back(d);
    decal::buildIndex(tap);
    decal::DecalSet out;
    CHECK_OK(composeRoadSet(tap, false, r, out));
    CHECK_EQ(out.decals.size(), 2u);
    // invalid inputs: refused, output empty (the caller then keeps the tap's set: E6)
    MapIn bad = blank(16, 10);
    bad.plots.pop_back();
    CHECK_ERR(produce(bad, LayoutOptions(), n, r), Err::BadLayout);
    CHECK(r.empty());
    MapIn bad2 = blank(16, 10);
    bad2.plots[3].band = 9;
    CHECK_ERR(produce(bad2, LayoutOptions(), n, r), Err::BadLayout);
    MapIn bad3 = blank(0, 10);
    CHECK_ERR(produce(bad3, LayoutOptions(), n, r), Err::BadLayout);
    MapIn bad4 = blank(2000, 2);
    CHECK_ERR(buildNetwork(bad4, n), Err::BadLayout);
    // a network of another map, or with one half of an edge, is refused by layOut
    MapIn ok = blank(16, 10);
    setBand(ok, 3, 3, kBandRoad);
    setBand(ok, 4, 3, kBandRoad);
    CHECK_OK(buildNetwork(ok, n));
    Network half = n;
    half.edges[3 * 16 + 4] = 0;
    CHECK_ERR(layOut(ok, half, LayoutOptions(), r), Err::BadLayout);
    Network other = n;
    other.width = 17;
    CHECK_ERR(layOut(ok, other, LayoutOptions(), r), Err::BadLayout);
    LayoutOptions badOpt;
    badOpt.subStep = 0;
    CHECK_ERR(layOut(ok, n, badOpt, r), Err::BadLayout);
    badOpt = LayoutOptions();
    badOpt.jitter = std::nanf("");
    CHECK_ERR(layOut(ok, n, badOpt, r), Err::BadLayout);
    // an isolated route plot gets a stub
    MapIn one = blank(8, 8);
    setBand(one, 3, 3, kBandRail);
    CHECK_OK(produce(one, LayoutOptions(), n, r));
    CHECK_EQ(r.decals.size(), 1u);
}

C5R_TEST(compose_with_tap)
{
    MapIn m = randomMap(40, 30, 5, 14, 30);
    Network n;
    RoadDecals r;
    CHECK_OK(produce(m, LayoutOptions(), n, r));
    CHECK(!r.empty());
    // a tap set: two route curves, a field, with a walk (one cell per 256 samples) and an area block
    decal::DecalSet tap;
    tap.worldRect[2] = 64.0f * 40 - 1;
    tap.worldRect[3] = 64.0f * 34 - 1;
    tap.textures = {"other_d.dds", "roadsandrails_new_d.dds"};
    tap.vertices.resize(9);
    for (int k = 0; k < 3; ++k) {
        decal::Decal d;
        d.rect[0] = 100.0f + 300 * k;
        d.rect[1] = 200;
        d.rect[2] = d.rect[0] + 50;
        d.rect[3] = 260;
        d.priority = k == 1 ? 0.5f : 0.7f;
        d.texture[0] = k == 1 ? 0 : 1;
        d.firstVertex = 3 * k;
        d.vertexCount = 3;
        d.listOrder = static_cast<uint32_t>(k);
        tap.decals.push_back(d);
    }
    tap.walk.origin[0] = 0;
    tap.walk.origin[1] = 0;
    tap.walk.cols = 10;
    tap.walk.rows = 9;
    tap.walk.inverse[0] = 10.0f / tap.worldRect[2];
    tap.walk.inverse[1] = 9.0f / tap.worldRect[3];
    for (int k = 0; k < 3; ++k) {
        const float px = (tap.decals[k].rect[0] + tap.decals[k].rect[2]) / 2, py = 230;
        tap.walk.cell.push_back(static_cast<uint32_t>(py * tap.walk.inverse[1]) * 10 + static_cast<uint32_t>(px * tap.walk.inverse[0]));
        tap.walk.rank.push_back(0);
        tap.walk.point.push_back(px);
        tap.walk.point.push_back(py);
    }
    tap.areaTilesX = 40;
    tap.areaTilesY = 34;
    tap.areaChanged.assign(40 * 34, 0);
    decal::buildIndex(tap);
    CHECK(decal::hasWalk(tap));
    decal::DecalSet out;
    ComposeStats st;
    CHECK_OK(composeRoadSet(tap, true, r, out, &st));
    CHECK_EQ(st.tapRemoved, 2u);
    CHECK_EQ(st.roadDecals, static_cast<uint32_t>(r.decals.size()));
    CHECK_EQ(out.decals.size(), 1 + r.decals.size());
    CHECK(st.walk && decal::hasWalk(out));
    CHECK(st.area && st.areaBitsSet > 0);
    CHECK_EQ(out.textures.size(), 5u);   // the tap's two names plus our three (the D name differs in case)
    for (size_t i = 1; i < out.decals.size(); ++i) CHECK(isTapRouteCurve(out, out.decals[i]));
    CHECK(!isTapRouteCurve(out, out.decals[0]));
    // every tile query works and lists our decals in the tiles they cover
    size_t listed = 0;
    for (int ty = 0; ty < 34; ++ty)
        for (int tx = 0; tx < 40; ++tx) listed += decal::decalsForTile(out, tx * 64, ty * 64).indices.size();
    CHECK(listed >= r.decals.size());
    // without removing, the tap's route curves stay
    CHECK_OK(composeRoadSet(tap, false, r, out, &st));
    CHECK_EQ(st.tapRemoved, 0u);
    CHECK_EQ(out.decals.size(), 3 + r.decals.size());
    // negatives: a road decal outside the world, a bad vertex run, a bad texture index
    RoadDecals b = r;
    b.decals[0].rect[2] = 1e7f;
    CHECK_ERR(composeRoadSet(tap, true, b, out), Err::BadLayout);
    CHECK(out.decals.empty());
    b = r;
    b.decals[0].vertexCount = 4;
    CHECK_ERR(composeRoadSet(tap, true, b, out), Err::BadLayout);
    b = r;
    b.decals[0].texture[1] = 7;
    CHECK_ERR(composeRoadSet(tap, true, b, out), Err::BadLayout);
    b = r;
    b.owner.pop_back();
    CHECK_ERR(composeRoadSet(tap, true, b, out), Err::BadLayout);
    decal::DecalSet badTap = tap;
    badTap.worldRect[2] = std::nanf("");
    CHECK_ERR(composeRoadSet(badTap, true, r, out), Err::BadLayout);
}

C5R_TEST(styles_differ_and_strip_look)
{
    MapIn m = randomMap(30, 20, 3, 8, 20);
    Network n;
    CHECK_OK(buildNetwork(m, n));
    RoadDecals a, b, c;
    LayoutOptions o;
    o.style = Style::Straight;
    CHECK_OK(layOut(m, n, o, a));
    o.style = Style::Spline;
    CHECK_OK(layOut(m, n, o, b));
    o.style = Style::SplineJitter;
    CHECK_OK(layOut(m, n, o, c));
    CHECK(!sameDecals(a, b) && !sameDecals(b, c));
    // the strip's v values are the band's three rows only; u inside the band's span
    for (const RoadDecals* r : {&a, &b, &c})
        for (size_t k = 0; k < r->decals.size(); ++k) {
            const Band band = static_cast<Band>(m.plots[r->owner[k]].band);
            const BandLook& lk = bandLook(band);
            const decal::Decal& d = r->decals[k];
            for (uint32_t v = d.firstVertex; v < d.firstVertex + d.vertexCount; v += 7) {
                const decal::DecalVertex& dv = r->vertices[v];
                bool hit = false;
                // decode the half v
                const uint16_t h = dv.v;
                const int e = (h >> 10) & 31, man = h & 1023;
                const float vv = e ? std::ldexp(1.0f + man / 1024.0f, e - 15) : std::ldexp(man / 1024.0f, -14);
                for (int q = 0; q < 3; ++q)
                    if (std::fabs(vv - lk.v[q]) < 2e-3f) hit = true;
                CHECK(hit);
                const uint16_t hu = dv.u;
                const int eu = (hu >> 10) & 31, mu = hu & 1023;
                const float uu = eu ? std::ldexp(1.0f + mu / 1024.0f, eu - 15) : std::ldexp(mu / 1024.0f, -14);
                CHECK(uu >= lk.u0 - 1e-3f && uu <= lk.u1 + 1e-3f);
            }
        }
}

// ---- all connections and the junction treatments (ticket 4940) ----

namespace {

const Junction kJunctions[4] = {Junction::Through, Junction::Cross, Junction::Ring, Junction::Rosette};

LayoutOptions variantE()
{
    LayoutOptions o;   // variant (e): FixRoadLayout 1's style (docs/extras/roads.md 4c)
    o.style = Style::Line;
    o.filletRadius = 22;
    o.mergeLength = 22;
    return o;
}

uint32_t decalsOf(const RoadDecals& r, uint32_t plot)
{
    uint32_t c = 0;
    for (uint32_t o : r.owner) c += o == plot;
    return c;
}

}  // namespace

C5R_TEST(names_of_connect_and_junction)
{
    for (Junction j : kJunctions) {
        Junction back = Junction::Through;
        CHECK(junctionFromName(junctionName(j), back) && back == j);
    }
    Connect c = Connect::Tidy;
    CHECK(connectFromName("all", c) && c == Connect::All && std::string(connectName(c)) == "all");
    CHECK(connectFromName("tidy", c) && c == Connect::Tidy && std::string(connectName(c)) == "tidy");
    Junction j;
    CHECK(!junctionFromName("star", j));
    CHECK(!connectFromName("some", c));
}

C5R_TEST(all_connections_keep_every_candidate)
{
    // the triangle the ring rule cuts: all three edges
    MapIn t = blank(10, 10);
    setBand(t, 4, 4, kBandRoad);
    setBand(t, 5, 4, kBandRoad);
    setBand(t, 4, 5, kBandRoad);
    Network n;
    CHECK_OK(buildNetwork(t, n, Connect::All));
    CHECK_EQ(n.candidateEdges, 3u);
    CHECK_EQ(edgeCount(n), 3);
    CHECK_EQ(n.rejected, 0u);
    CHECK_EQ(countEmptyFaces(t, n), 1u);   // the triangle is an empty face: expected under all connections
    // a filled disc: every interior plot joined to all six neighbours
    MapIn d = blank(20, 20);
    disc(d, 10, 10, 3, kBandRoad);
    CHECK_OK(buildNetwork(d, n, Connect::All));
    CHECK_EQ(n.edges[10 * 20 + 10], 0x3F);
    CHECK_EQ(static_cast<uint32_t>(edgeCount(n)), n.candidateEdges);
    // random maps, plain and wrapped: every pair of neighbouring route plots joined, both halves, nothing else
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        MapIn m = randomMap(30, 22, seed, 10, 25);
        if (seed % 2 == 0) m.wrapX = true;
        CHECK_OK(buildNetwork(m, n, Connect::All));
        CHECK_EQ(n.rejected, 0u);
        CHECK_EQ(static_cast<uint32_t>(edgeCount(n)), n.candidateEdges);
        for (int y = 0; y < static_cast<int>(m.height); ++y)
            for (int x = 0; x < static_cast<int>(m.width); ++x) {
                const size_t i = static_cast<size_t>(y) * m.width + x;
                for (int dd = 0; dd < 6; ++dd) {
                    int nx, ny;
                    const bool both = m.plots[i].band != kBandNone && neighbour(m, x, y, dd, nx, ny) &&
                                      m.plots[static_cast<size_t>(ny) * m.width + nx].band != kBandNone && !(nx == x && ny == y);
                    CHECK_EQ(((n.edges[i] >> dd) & 1) != 0, both);
                }
            }
        // the tidy network is a subset
        Network tidy;
        CHECK_OK(buildNetwork(m, tidy));
        for (size_t i = 0; i < m.plots.size(); ++i) CHECK_EQ(tidy.edges[i] & ~n.edges[i], 0);
    }
    CHECK_ERR(buildNetwork(d, n, static_cast<Connect>(7)), Err::BadLayout);
    CHECK(n.edges.empty());
}

C5R_TEST(junction_treatments_at_a_six_link_plot)
{
    MapIn m = blank(20, 20);
    disc(m, 10, 10, 3, kBandRoad);
    const uint32_t centre = 10 * 20 + 10;
    // decals of the centre plot per treatment: through 1 (two edges) + 4 spurs; cross 3 lines; ring 6 spokes + the ring;
    // rosette 6 curves
    const uint32_t want[4] = {5, 3, 7, 6};
    for (int k = 0; k < 4; ++k) {
        LayoutOptions o = variantE();
        o.connect = Connect::All;
        o.junction = kJunctions[k];
        Network n;
        RoadDecals r;
        CHECK_OK(produce(m, o, n, r));
        CHECK_EQ(decalsOf(r, centre), want[k]);
        // every route plot drawn; no crossing between plots, no loose end (a ring's spokes end on the ring)
        for (uint32_t i = 0; i < m.plots.size(); ++i)
            if (m.plots[i].band != kBandNone) CHECK(decalsOf(r, i) > 0);
        const LineCheck lc = checkLines(m, r, 1.0f, &n);
        CHECK_EQ(lc.crossings, 0u);
        CHECK_EQ(lc.overshoots, 0u);
        CHECK_EQ(lc.abruptEnds, 0u);
        // a city stays a hub: six spokes whatever the treatment
        MapIn mc = m;
        mc.plots[centre].city = 1;
        CHECK_OK(produce(mc, o, n, r));
        CHECK_EQ(decalsOf(r, centre), 6u);
    }
    // the cross's lines pass through the centre; the rosette's curves stay off it
    {
        LayoutOptions o = variantE();
        o.connect = Connect::All;
        float cx, cy;
        plotCentre(10, 10, cx, cy);
        const float px = cx, py = cy * kIsoY;
        for (Junction j : {Junction::Cross, Junction::Rosette, Junction::Ring}) {
            o.junction = j;
            Network n;
            RoadDecals r;
            CHECK_OK(produce(m, o, n, r));
            float nearest = 1e9f;
            for (size_t k = 0; k < r.decals.size(); ++k) {
                if (r.owner[k] != centre) continue;
                for (uint32_t q = r.lineStart[k]; q < r.lineStart[k + 1]; ++q) {
                    const float x = r.lines[2 * q], y = r.lines[2 * q + 1];
                    if (std::isnan(x)) continue;
                    nearest = (std::min)(nearest, std::sqrt((x - px) * (x - px) + (y - py) * (y - py)));
                }
            }
            if (j == Junction::Cross) CHECK(nearest < 0.01f);
            if (j == Junction::Rosette) CHECK(nearest > 4.0f);
            if (j == Junction::Ring) CHECK(std::fabs(nearest - o.ringRadius) < 0.01f);
        }
    }
}

C5R_TEST(junction_only_where_enough_links)
{
    // a straight line and a T: no plot has four links, so every treatment draws what Through draws
    MapIn m = blank(20, 12);
    for (int x = 2; x < 18; ++x) setBand(m, x, 5, kBandRoad);
    int px = 9, py = 5;
    for (int k = 0; k < 4; ++k) {
        int nx, ny;
        CHECK(neighbour(m, px, py, 1, nx, ny));
        setBand(m, nx, ny, kBandRail);
        px = nx;
        py = ny;
    }
    LayoutOptions o = variantE();
    o.connect = Connect::All;
    Network n;
    RoadDecals base, other;
    CHECK_OK(produce(m, o, n, base));
    for (Junction j : kJunctions) {
        o.junction = j;
        CHECK_OK(produce(m, o, n, other));
        CHECK(sameDecals(base, other));
    }
    // junctionMin 3 makes the T's plot a junction under cross: its three edges as one line and a spoke
    o.junction = Junction::Cross;
    o.junctionMin = 3;
    CHECK_OK(produce(m, o, n, other));
    CHECK(!sameDecals(base, other));
    CHECK_EQ(decalsOf(other, 5 * 20 + 9), 2u);
}

C5R_TEST(all_connections_random_maps_clean_and_deterministic)
{
    for (uint32_t seed = 11; seed <= 16; ++seed) {
        MapIn m = randomMap(32, 24, seed, 12, 30);
        if (seed % 2) m.wrapX = true;
        for (Junction j : kJunctions) {
            LayoutOptions o = variantE();
            o.connect = Connect::All;
            o.junction = j;
            Network n1, n2;
            RoadDecals a, b;
            CHECK_OK(produce(m, o, n1, a));
            CHECK_OK(produce(m, o, n2, b));
            CHECK(sameDecals(a, b));
            CHECK(n1.edges == n2.edges);
            const LineCheck lc = checkLines(m, a, 1.0f, &n1);
            CHECK_EQ(lc.crossings, 0u);
            CHECK_EQ(lc.overshoots, 0u);
            CHECK_EQ(lc.abruptEnds, 0u);
            for (uint32_t i = 0; i < m.plots.size(); ++i)
                if (m.plots[i].band != kBandNone) CHECK(decalsOf(a, i) > 0);
        }
    }
}

// ---- the line jitter (ticket 4940 part 1b) ----

C5R_TEST(line_jitter_zero_is_unchanged_and_style_c_ignores_it)
{
    MapIn m = randomMap(30, 22, 21, 10, 25);
    for (Connect cn : {Connect::Tidy, Connect::All})
        for (Junction j : kJunctions) {
            LayoutOptions o = variantE();
            o.connect = cn;
            o.junction = j;
            Network n;
            RoadDecals a, b;
            CHECK_OK(produce(m, o, n, a));
            o.lineJitter = 0.0f;   // explicit 0: the same bytes
            CHECK_OK(produce(m, o, n, b));
            CHECK(sameDecals(a, b));
            o.lineJitter = 4.0f;   // and a jitter moves them
            CHECK_OK(produce(m, o, n, b));
            CHECK(!sameDecals(a, b));
        }
    // style (c) reads its own jitter only
    LayoutOptions c;
    c.style = Style::SplineJitter;
    Network n;
    RoadDecals a, b;
    CHECK_OK(produce(m, c, n, a));
    c.lineJitter = 6.0f;
    CHECK_OK(produce(m, c, n, b));
    CHECK(sameDecals(a, b));
}

C5R_TEST(line_jitter_clean_and_deterministic)
{
    // every amount the previews use and the range's top, on plain and wrapped random maps and a filled disc: no crossing
    // between pieces, no loose or abrupt end, every route plot drawn, the turn per centre-line step no larger than
    // without the jitter on the same map, the same bytes twice
    const float amounts[5] = {2.0f, 3.0f, 5.0f, 6.0f, 7.0f};
    for (uint32_t seed = 31; seed <= 34; ++seed) {
        MapIn m = randomMap(32, 24, seed, 12, 30);
        if (seed % 2) m.wrapX = true;
        if (seed == 34) disc(m, 16, 12, 4, kBandRoad);
        for (Junction j : kJunctions) {
            LayoutOptions o = variantE();
            o.connect = Connect::All;
            o.junction = j;
            Network n0;
            RoadDecals r0;
            CHECK_OK(produce(m, o, n0, r0));
            const float turn0 = checkLines(m, r0, 1.0f, &n0).maxTurnDeg;
            for (float amt : amounts) {
                o.lineJitter = amt;
                Network n1, n2;
                RoadDecals a, b;
                CHECK_OK(produce(m, o, n1, a));
                CHECK_OK(produce(m, o, n2, b));
                CHECK(sameDecals(a, b));
                const LineCheck lc = checkLines(m, a, 1.0f, &n1);
                if (lc.crossings || lc.overshoots || lc.maxTurnDeg > turn0 + 0.05f)
                    std::printf("  line_jitter: seed %u, %s, jitter %.0f: %u crossings, %u overshoots, turn %.1f (%.1f at 0)\n",
                                seed, junctionName(j), amt, lc.crossings, lc.overshoots, lc.maxTurnDeg, turn0);
                CHECK_EQ(lc.crossings, 0u);
                CHECK_EQ(lc.overshoots, 0u);
                CHECK_EQ(lc.abruptEnds, 0u);
                CHECK(lc.maxTurnDeg <= turn0 + 0.05f);
                for (uint32_t i = 0; i < m.plots.size(); ++i)
                    if (m.plots[i].band != kBandNone) CHECK(decalsOf(a, i) > 0);
            }
        }
    }
}

C5R_TEST(line_jitter_cross_still_meets_in_one_point)
{
    // the jittered centre plot of a disc under cross: its three lines share one point (the moved pass point), off the
    // plot centre, and the lines bend there smoothly (no step turning more than 12.4 degrees)
    MapIn m = blank(20, 20);
    disc(m, 10, 10, 3, kBandRoad);
    LayoutOptions o = variantE();
    o.connect = Connect::All;
    o.junction = Junction::Cross;
    o.lineJitter = 7.0f;
    Network n;
    RoadDecals r;
    CHECK_OK(produce(m, o, n, r));
    const uint32_t centre = 10 * 20 + 10;
    std::vector<std::vector<std::pair<float, float>>> lines;
    for (size_t k = 0; k < r.decals.size(); ++k) {
        if (r.owner[k] != centre) continue;
        lines.push_back({});
        for (uint32_t q = r.lineStart[k]; q < r.lineStart[k + 1]; ++q)
            if (!std::isnan(r.lines[2 * q])) lines.back().push_back({r.lines[2 * q], r.lines[2 * q + 1]});
    }
    CHECK_EQ(lines.size(), 3u);
    bool shared = false;
    float sx = 0, sy = 0;
    for (const auto& p : lines[0]) {
        bool inAll = true;
        for (size_t l = 1; l < lines.size() && inAll; ++l)
            inAll = std::find(lines[l].begin(), lines[l].end(), p) != lines[l].end();
        if (inAll) {
            shared = true;
            sx = p.first;
            sy = p.second;
        }
    }
    CHECK(shared);
    float cx, cy;
    plotCentre(10, 10, cx, cy);
    const float off = std::sqrt((sx - cx) * (sx - cx) + (sy - cy * kIsoY) * (sy - cy * kIsoY));
    CHECK(off > 0.01f && off <= 7.0f + 1e-3f);
    CHECK(checkLines(m, r, 1.0f, &n).maxTurnDeg <= 12.4f);
}

C5R_TEST(junction_options_checked)
{
    MapIn m = blank(12, 12);
    disc(m, 6, 6, 2, kBandRoad);
    Network n;
    CHECK_OK(buildNetwork(m, n, Connect::All));
    RoadDecals r;
    LayoutOptions o = variantE();
    o.junction = Junction::Ring;
    o.ringRadius = 2.0f;
    CHECK_ERR(layOut(m, n, o, r), Err::BadLayout);
    CHECK(r.empty());
    o.ringRadius = 30.0f;
    CHECK_ERR(layOut(m, n, o, r), Err::BadLayout);
    o.ringRadius = 9.0f;
    o.lineJitter = -1.0f;
    CHECK_ERR(layOut(m, n, o, r), Err::BadLayout);
    o.lineJitter = 8.0f;   // above the range (7): a merge can cross its bent line there
    CHECK_ERR(layOut(m, n, o, r), Err::BadLayout);
    o.lineJitter = 0.0f;
    o.junctionMin = 2;
    CHECK_ERR(layOut(m, n, o, r), Err::BadLayout);
    o.junctionMin = 7;
    CHECK_ERR(layOut(m, n, o, r), Err::BadLayout);
    o.junctionMin = 4;
    o.junction = static_cast<Junction>(9);
    CHECK_ERR(layOut(m, n, o, r), Err::BadLayout);
    o.junction = Junction::Ring;
    o.connect = static_cast<Connect>(5);
    CHECK_ERR(produce(m, o, n, r), Err::BadLayout);
    o.connect = Connect::All;
    CHECK_OK(produce(m, o, n, r));
    CHECK(!r.empty());
}

// ---- bridges (ticket 0808) ----

// A bridge-like mesh: a box of 11.6 x 3.7 (deck z 9.5 .. 13.4) turned by `deg` about z and centred on (cx, cy), as the
// EXE's CPU-built bridge holds it (stride 20: half POSITION, NORMAL, TEXCOORD; stride 32: float POSITION, NORMAL, UV).
std::vector<uint8_t> bridgeMesh(float deg, float cx, float cy, uint32_t stride)
{
    const float a = deg * 3.14159265f / 180.0f, c = std::cos(a), s = std::sin(a);
    std::vector<uint8_t> b;
    for (int i = 0; i <= 10; ++i)
        for (int j = 0; j <= 2; ++j)
            for (int k = 0; k <= 1; ++k) {
                const float x = -5.8f + 1.16f * i, y = -1.85f + 1.85f * j, z = k ? 13.4f : 9.5f;
                const float p[3] = {cx + x * c - y * s, cy + x * s + y * c, z};
                std::vector<uint8_t> v(stride, 0);
                if (stride == 20) {
                    for (int q = 0; q < 3; ++q) {
                        const uint16_t h = bake::floatToHalf(p[q]);
                        std::memcpy(v.data() + 2 * q, &h, 2);
                    }
                } else {
                    std::memcpy(v.data(), p, 12);
                }
                b.insert(b.end(), v.begin(), v.end());
            }
    return b;
}

void applyRows(const float* r, float x, float y, float z, float& wx, float& wy, float& wz)
{
    wx = x * r[0] + y * r[4] + z * r[8] + r[12];
    wy = x * r[1] + y * r[5] + z * r[9] + r[13];
    wz = x * r[2] + y * r[6] + z * r[10] + r[14];
}

void identityAt(float* r, float tx, float ty)
{
    for (int k = 0; k < 16; ++k) r[k] = (k % 5 == 0) ? 1.0f : 0.0f;
    r[12] = tx;
    r[13] = ty;
}

C5R_TEST(bridge_mesh_fit)
{
    for (uint32_t stride : {20u, 32u}) {
        for (float deg : {0.0f, 30.0f, 90.0f, 145.0f}) {
            const std::vector<uint8_t> vb = bridgeMesh(deg, 1.0f, -2.0f, stride);
            BridgeFit f;
            CHECK_OK(fitBridgeMesh(vb.data(), vb.size(), stride, f));
            CHECK(f.vertices == 66);
            CHECK(std::fabs(f.cx - 1.0f) < 0.02f && std::fabs(f.cy + 2.0f) < 0.02f);
            CHECK(std::fabs(f.cz - 11.45f) < 0.02f);
            CHECK(std::fabs(f.length - 11.6f) < 0.05f && std::fabs(f.width - 3.7f) < 0.05f);
            const float a = deg * 3.14159265f / 180.0f;
            CHECK(std::fabs(std::fabs(f.ax * std::cos(a) + f.ay * std::sin(a)) - 1.0f) < 1e-3f);   // parallel, either sign
            CHECK(f.ax >= 0.0f);
        }
    }
    BridgeFit f;
    const std::vector<uint8_t> vb = bridgeMesh(0, 0, 0, 20);
    CHECK_ERR(fitBridgeMesh(nullptr, 100, 20, f), Err::BadLayout);
    CHECK_ERR(fitBridgeMesh(vb.data(), vb.size(), 24, f), Err::BadLayout);
    CHECK_ERR(fitBridgeMesh(vb.data(), vb.size() - 1, 20, f), Err::BadLayout);
    std::vector<uint8_t> sq(20 * 4, 0);   // a square: no long axis
    const float pts[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    for (int i = 0; i < 4; ++i)
        for (int q = 0; q < 2; ++q) {
            const uint16_t h = bake::floatToHalf(pts[i][q]);
            std::memcpy(sq.data() + 20 * i + 2 * q, &h, 2);
        }
    CHECK_ERR(fitBridgeMesh(sq.data(), sq.size(), 20, f), Err::BadLayout);
    std::vector<uint8_t> nan = vb;
    const uint16_t inf = 0x7C00;
    std::memcpy(nan.data(), &inf, 2);
    CHECK_ERR(fitBridgeMesh(nan.data(), nan.size(), 20, f), Err::BadLayout);
}

C5R_TEST(bridge_moves_onto_our_crossing)
{
    // a road along row 4 from (2,4) to (7,4); the EXE's bridge over the edge (4,4)-(5,4) turned 35 degrees and 3 off the
    // edge's middle, in a frame with an offset (iso = world + (ox, oy))
    MapIn m = blank(12, 10);
    for (int x = 2; x <= 7; ++x) setBand(m, x, 4, kBandRail);
    Network n;
    CHECK_OK(buildNetwork(m, n));
    const LayoutOptions o = variantE();
    const float ox = 100.0f, oy = 250.0f;
    float sx, sy;
    plotCentre(4, 4, sx, sy);
    const float midX = sx + 32.0f, midY = sy * kIsoY;   // the edge's middle (iso)
    const std::vector<uint8_t> vb = bridgeMesh(35.0f, 0.0f, 0.0f, 20);
    BridgeFit f;
    CHECK_OK(fitBridgeMesh(vb.data(), vb.size(), 20, f));
    float rows[16];
    identityAt(rows, midX - ox + 2.0f, midY - oy - 2.2f);
    rows[14] = 0.0f;
    const BridgePlacement p = placeBridge(m, n, o, rows, f, ox, oy);
    CHECK(p.action == BridgeAction::Move);
    CHECK(std::fabs(p.turnDeg + 35.0f) < 0.5f);
    CHECK(p.edgeDist > 2.9f && p.edgeDist < 3.1f && std::fabs(p.shift - p.edgeDist) < 1e-3f);
    CHECK(p.plot == 4 * 12 + 4 && p.dir == 0 && p.edgeKey == (4u * 12 + 4) * 6 + 1);
    // the mesh's centre lands on our crossing, its long axis along our heading (east), the deck height kept
    float wx, wy, wz;
    applyRows(p.rows, f.cx, f.cy, f.cz, wx, wy, wz);
    CHECK(std::fabs(wx + ox - midX) < 1e-3f && std::fabs(wy + oy - midY) < 1e-3f);
    CHECK(std::fabs(wz - f.cz) < 1e-4f);
    float ex, ey, ez;
    applyRows(p.rows, f.cx + f.ax, f.cy + f.ay, f.cz, ex, ey, ez);
    CHECK(std::fabs(std::fabs(ex - wx) - 1.0f) < 1e-3f && std::fabs(ey - wy) < 1e-3f);
    CHECK(p.rows[3] == 0 && p.rows[7] == 0 && p.rows[11] == 0 && p.rows[15] == 1);
    // the same bridge seen from the other plot of the edge (its centre just across the edge): the same edge, the same place
    identityAt(rows, midX - ox + 4.0f, midY - oy + 1.0f);
    const BridgePlacement q = placeBridge(m, n, o, rows, f, ox, oy);
    CHECK(q.action == BridgeAction::Move && q.edgeKey == p.edgeKey);
    applyRows(q.rows, f.cx, f.cy, f.cz, wx, wy, wz);
    CHECK(std::fabs(wx + ox - midX) < 1e-3f && std::fabs(wy + oy - midY) < 1e-3f);
    // a mesh already along the road: no turn, only the shift
    const std::vector<uint8_t> vb0 = bridgeMesh(180.0f, 0.0f, 0.0f, 32);
    BridgeFit f0;
    CHECK_OK(fitBridgeMesh(vb0.data(), vb0.size(), 32, f0));
    identityAt(rows, midX - ox, midY - oy + 1.5f);
    const BridgePlacement z = placeBridge(m, n, o, rows, f0, ox, oy);
    CHECK(z.action == BridgeAction::Move && std::fabs(z.turnDeg) < 0.05f && std::fabs(z.shift - 1.5f) < 1e-3f);
}

C5R_TEST(bridge_dropped_or_kept)
{
    MapIn m = blank(12, 10);
    for (int x = 2; x <= 7; ++x) setBand(m, x, 4, kBandRoad);
    Network n;
    CHECK_OK(buildNetwork(m, n));
    const LayoutOptions o = variantE();
    const std::vector<uint8_t> vb = bridgeMesh(80.0f, 0.0f, 0.0f, 20);
    BridgeFit f;
    CHECK_OK(fitBridgeMesh(vb.data(), vb.size(), 20, f));
    float sx, sy, rows[16];
    // the edge (4,4)-(4,5): (4,5) has no route, so our layout draws no road there: no bridge
    plotCentre(4, 4, sx, sy);
    identityAt(rows, sx + 16.0f, sy * kIsoY + 27.7f);
    BridgePlacement p = placeBridge(m, n, o, rows, f, 0, 0);
    CHECK(p.action == BridgeAction::Drop && p.edgeKey != 0);
    for (int k = 0; k < 16; ++k) CHECK(p.rows[k] == rows[k]);
    // at a plot's centre (no edge within reach): kept as it is
    identityAt(rows, sx, sy * kIsoY);
    p = placeBridge(m, n, o, rows, f, 0, 0);
    CHECK(p.action == BridgeAction::Keep && p.edgeKey == 0);
    for (int k = 0; k < 16; ++k) CHECK(p.rows[k] == rows[k]);
    // off the map, non-finite rows, a network of another map: kept
    identityAt(rows, -500.0f, -500.0f);
    CHECK(placeBridge(m, n, o, rows, f, 0, 0).action == BridgeAction::Keep);
    identityAt(rows, sx + 32.0f, sy * kIsoY);
    rows[5] = std::numeric_limits<float>::quiet_NaN();
    CHECK(placeBridge(m, n, o, rows, f, 0, 0).action == BridgeAction::Keep);
    identityAt(rows, sx + 32.0f, sy * kIsoY);
    CHECK(placeBridge(m, n, o, rows, f, 0, 0).action == BridgeAction::Move);
    Network other;
    CHECK_OK(buildNetwork(blank(6, 6), other));
    CHECK(placeBridge(m, other, o, rows, f, 0, 0).action == BridgeAction::Keep);
    // edgeCrossing: not joined, off the map
    float X, Y, dx, dy;
    CHECK(!edgeCrossing(m, n, o, 4, 4, sx, sy * kIsoY, 1, X, Y, dx, dy));
    CHECK(!edgeCrossing(m, n, o, 4, 4, sx, sy * kIsoY, 7, X, Y, dx, dy));
    CHECK(edgeCrossing(m, n, o, 4, 4, sx, sy * kIsoY, 0, X, Y, dx, dy));
    CHECK(std::fabs(X - sx - 32.0f) < 1e-3f && std::fabs(dx - 1.0f) < 1e-4f);
}

C5R_TEST(bridge_on_every_edge_direction_and_the_seam)
{
    // a star of road around (5,5) on a wrapped map: one bridge per hex edge direction, each placed on our crossing
    MapIn m = blank(10, 10, true);
    setBand(m, 5, 5, kBandRoad);
    for (int d = 0; d < 6; ++d) {
        int nx, ny;
        CHECK(neighbour(m, 5, 5, d, nx, ny));
        setBand(m, nx, ny, kBandRoad);
    }
    Network n;
    CHECK_OK(buildNetwork(m, n, Connect::All));
    const LayoutOptions o = variantE();
    const std::vector<uint8_t> vb = bridgeMesh(10.0f, 0.0f, 0.0f, 20);
    BridgeFit f;
    CHECK_OK(fitBridgeMesh(vb.data(), vb.size(), 20, f));
    float sx, sy;
    plotCentre(5, 5, sx, sy);
    const float off[6][2] = {{64, 0}, {32, 55.424f}, {-32, 55.424f}, {-64, 0}, {-32, -55.424f}, {32, -55.424f}};
    std::set<uint64_t> keys;
    for (int d = 0; d < 6; ++d) {
        float rows[16];
        const float mx = sx + off[d][0] * 0.5f, my = sy * kIsoY + off[d][1] * 0.5f;
        identityAt(rows, mx + 1.0f, my - 1.0f);
        const BridgePlacement p = placeBridge(m, n, o, rows, f, 0, 0);
        CHECK(p.action == BridgeAction::Move);
        keys.insert(p.edgeKey);
        float wx, wy, wz, ex, ey, ez;
        applyRows(p.rows, f.cx, f.cy, f.cz, wx, wy, wz);
        CHECK(std::fabs(wx - mx) < 1e-2f && std::fabs(wy - my) < 1e-2f);
        applyRows(p.rows, f.cx + f.ax, f.cy + f.ay, f.cz, ex, ey, ez);
        const float hx = off[d][0] / 64.0f, hy = off[d][1] / 64.0f;   // our heading: centre to centre
        CHECK(std::fabs(std::fabs((ex - wx) * hx + (ey - wy) * hy) - 1.0f) < 1e-3f);
    }
    CHECK(keys.size() == 6);
    // the seam: a road from (9,3) east to (0,3); the bridge's own wrap copy (x past the map's width) stays where it is
    MapIn w = blank(10, 10, true);
    setBand(w, 8, 3, kBandRoad);
    setBand(w, 9, 3, kBandRoad);
    setBand(w, 0, 3, kBandRoad);
    Network nw;
    CHECK_OK(buildNetwork(w, nw));
    plotCentre(9, 3, sx, sy);
    float rows[16];
    identityAt(rows, sx + 32.0f + 0.5f, sy * kIsoY + 2.0f);
    const BridgePlacement p = placeBridge(w, nw, o, rows, f, 0, 0);
    CHECK(p.action == BridgeAction::Move);
    float wx, wy, wz;
    applyRows(p.rows, f.cx, f.cy, f.cz, wx, wy, wz);
    CHECK(std::fabs(wx - sx - 32.0f) < 1e-2f && std::fabs(wy - sy * kIsoY) < 1e-2f);
    identityAt(rows, 32.0f - 0.5f, sy * kIsoY + 2.0f);   // the same edge in the copy one map width (640) to the west
    const BridgePlacement q = placeBridge(w, nw, o, rows, f, 0, 0);
    CHECK(q.action == BridgeAction::Move && q.edgeKey == p.edgeKey);
    applyRows(q.rows, f.cx, f.cy, f.cz, wx, wy, wz);
    CHECK(std::fabs(wx - 32.0f) < 1e-2f && std::fabs(wy - sy * kIsoY) < 1e-2f);   // its own copy, not moved across the seam
}

C5R_TEST(bridge_follows_the_jitter)
{
    // all connections with jitter: the crossing is the middle of the two moved pass points, not the edge's middle
    MapIn m = blank(12, 10);
    for (int x = 2; x <= 7; ++x) setBand(m, x, 4, kBandRoad);
    LayoutOptions o = variantE();
    o.connect = Connect::All;
    o.lineJitter = 7.0f;
    Network n;
    CHECK_OK(buildNetwork(m, n, o.connect));
    const std::vector<uint8_t> vb = bridgeMesh(0.0f, 0.0f, 0.0f, 20);
    BridgeFit f;
    CHECK_OK(fitBridgeMesh(vb.data(), vb.size(), 20, f));
    float sx, sy, rows[16];
    plotCentre(4, 4, sx, sy);
    identityAt(rows, sx + 32.0f, sy * kIsoY);
    const BridgePlacement p = placeBridge(m, n, o, rows, f, 0, 0);
    CHECK(p.action == BridgeAction::Move);
    float X, Y, dx, dy;
    CHECK(edgeCrossing(m, n, o, 4, 4, sx, sy * kIsoY, 0, X, Y, dx, dy));
    float wx, wy, wz;
    applyRows(p.rows, f.cx, f.cy, f.cz, wx, wy, wz);
    CHECK(std::fabs(wx - X) < 1e-3f && std::fabs(wy - Y) < 1e-3f);
    CHECK(p.shift > 0.01f);   // the jitter moved the crossing off the edge's middle
    // and the drawn road passes there: a piece of plot (4,4) ends at the crossing
    RoadDecals r;
    CHECK_OK(layOut(m, n, o, r));
    bool found = false;
    for (size_t i = 0; i + 1 < r.lines.size(); i += 2)
        if (std::fabs(r.lines[i] - X) < 1e-3f && std::fabs(r.lines[i + 1] - Y) < 1e-3f) found = true;
    CHECK(found);
}

C5R_TEST_MAIN
