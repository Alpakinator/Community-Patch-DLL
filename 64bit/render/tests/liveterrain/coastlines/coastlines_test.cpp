// The coastline curves and region fill (ticket 4910; live-terrain.md 8.10.1). A synthetic island on a small map and
// a synthetic style file; every input check has a negative test. game_styles reads the install's TerrainModels.fpk.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include "c5r/gamefs/gamefiles.h"
#include "c5r/liveterrain/coastlines.h"
#include "../../common/testkit.h"

using namespace c5r;
using namespace c5r::liveterrain;

namespace {

// a frame like Tiny's (live-terrain.md 7.5): plot (x, y) is cell (x, y + 2); hex (q, r) at (64 q + 32 r + 32, 64 r + 128)
MapFrame frame(int w, int h, bool wrap) {
    MapFrame f;
    f.plotsW = w; f.plotsH = h; f.wrapX = wrap;
    f.h2w[0] = 64; f.h2w[1] = 0; f.h2w[4] = 32; f.h2w[5] = 64; f.h2w[12] = 32; f.h2w[13] = 128; f.h2w[15] = 1;
    f.G[0] = f.G[5] = f.G[10] = f.G[15] = 1; f.G[12] = -1; f.G[13] = 2; f.G[14] = -1;
    f.Ginv[0] = f.Ginv[5] = f.Ginv[10] = f.Ginv[15] = 1; f.Ginv[12] = 1; f.Ginv[13] = -2; f.Ginv[14] = 1;
    return f;
}

// terrain ids as heightfield::patternCellFlags reads them: 0 grassland, 5 coast, 6 ocean
PlotTable island(int w, int h, int cx, int cy, int landR, int coastR) {
    PlotTable t; t.w = w; t.h = h; t.plots.assign(size_t(w) * h, PlotInfo());
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        int d = 1 << 20;
        for (int sh = -w; sh <= w; sh += w) {   // hex distance, also across the x seam
            const int q = x + sh - (y >> 1), cq = cx - (cy >> 1);
            const int dq = q - cq, dr = y - cy;
            d = std::min(d, (std::abs(dq) + std::abs(dr) + std::abs(dq + dr)) / 2);
        }
        PlotInfo& p = t.plots[size_t(y) * w + x];
        p.base.terrain = d <= landR ? 0 : (d <= coastR ? 5 : 6);
    }
    return t;
}

std::string splineText() {
    std::string s;
    for (int i = 0; i < 100; ++i) {
        char b[64]; std::snprintf(b, sizeof b, "[%g,%g,0]\r\n", 400.0 - i * 4.0404, 2.0 * std::sin(i * 0.7));
        s += b;
    }
    return s;
}

const char* kXml =
    "<CoastRoot><SplineCharacterList>\n"
    "  <!-- comment <SplineCharacter Filename=\"nope.txt\" /> -->\n"
    "  <SplineCharacter Filename=\"Water.txt\" />\n"
    "</SplineCharacterList><CoastStyleList>\n"
    "  <CoastStyle type=\"Grass\"><SmoothingOffset HighSide=\"20\" LowSide=\"20\" />\n"
    "    <HighBeach Height=\"0.2\" Offset=\"1.5\" Character=\"0\" TerrainType=\"5\"/>\n"
    "    <LowBeach Height=\"0.19\" Offset=\"1.5\" Character=\"0\" TerrainType=\"5\"/>\n"
    "    <Waterline Height=\"0.198\" Offset=\"0\" Character=\"0\" /></CoastStyle>\n"
    "  <CoastStyle type=\"DefaultShallows\"><SmoothingOffset HighSide=\"20\" LowSide=\"20\" />\n"
    "    <HighBeach Height=\"0.11\" Offset=\"1.5\" Character=\"65535\" TerrainType=\"65535\"/>\n"
    "    <LowBeach Height=\"0.09\" Offset=\"1.5\" Character=\"65535\" TerrainType=\"65535\"/>\n"
    "    <Waterline Height=\"0.1\" Offset=\"0\" Character=\"65535\" /></CoastStyle>\n"
    "</CoastStyleList></CoastRoot>\n";

CoastStyles styles() {
    CoastStyles s;
    const Status st = CoastStyles::parse(kXml, [](const std::string& n, std::string& t) {
        if (n != "Water.txt") return false; t = splineText(); return true; }, s);
    CHECK_OK(st);
    return s;
}

uint32_t expectedDraws(uint32_t n) {
    uint32_t m = n, k = 0;
    while (m >= 6 && k < 4) { if (m & 1) ++m; m = m / 2 + 1; ++k; }
    uint32_t d = 0;
    for (uint32_t r = 1; r <= k; ++r) d += (1u << r) * (m - 2) + 2;
    return d;
}

} // namespace

C5R_TEST(styles_parse) {
    const CoastStyles s = styles();
    CHECK_EQ(s.characters.size(), size_t(1));
    CHECK_EQ(s.characters[0].size(), size_t(98));
    CHECK(s.present[1]); CHECK(s.present[12]); CHECK(!s.present[0]);
    CHECK_EQ(s.style[1].offset[0], -20.0f); CHECK_EQ(s.style[1].offset[1], -1.5f); CHECK_EQ(s.style[1].offset[2], 0.0f);
    CHECK_EQ(s.style[1].offset[3], 1.5f); CHECK_EQ(s.style[1].offset[4], 20.0f);
    CHECK_EQ(s.style[1].character[0], -1); CHECK_EQ(s.style[1].character[2], 0); CHECK_EQ(s.style[12].character[2], -1);
    CHECK_EQ(s.lookup(0).offset[4], 0.0f);   // an absent kind reads as zeros
}

C5R_TEST(styles_negative) {
    CoastStyles s;
    CHECK_ERR(CoastStyles::parse(kXml, [](const std::string&, std::string&) { return false; }, s), Err::NotFound);
    CHECK_ERR(CoastStyles::parse(kXml, [](const std::string&, std::string& t) { t = "[1,2,0]\n"; return true; }, s), Err::BadLayout);
    CHECK_ERR(CoastStyles::parse("<CoastRoot></CoastRoot>", [](const std::string&, std::string&) { return false; }, s), Err::BadLayout);
    std::string bad = kXml; bad.replace(bad.find("Character=\"0\" TerrainType"), 13, "Character=\"7\"");
    CHECK_ERR(CoastStyles::parse(bad, [](const std::string&, std::string& t) { t = splineText(); return true; }, s), Err::BadLayout);
    std::vector<CoastPoint> tab;
    CHECK_ERR(CoastStyles::characterTable("nothing", tab), Err::BadLayout);
}

C5R_TEST(island_curves_and_region) {
    const MapFrame f = frame(16, 12, false);
    const PlotTable t = island(16, 12, 7, 6, 2, 4);
    const CoastStyles s = styles();
    CoastCurves a, b;
    CHECK_OK(buildCoastCurves(f, t, s, 0x3DFC9816u, a));
    CHECK_OK(buildCoastCurves(f, t, s, 0x3DFC9816u, b));
    CHECK(a.lines.size() >= 2);
    bool have0 = false, have1 = false;
    uint64_t sum = 0;
    for (const CoastLine& L : a.lines) {
        have0 = have0 || L.type == 0; have1 = have1 || L.type == 1;
        CHECK_EQ(L.draws, expectedDraws(L.points));
        for (int k = 0; k < 5; ++k) CHECK_EQ(L.mesh[k].size(), size_t(8 * L.points - 14));
        sum += L.draws;
    }
    CHECK(have0); CHECK(have1);
    CHECK_EQ(a.randDraws, sum);
    CHECK_EQ(a.boundary.size(), a.lines.size());
    CHECK_EQ(a.fill.size(), 2 * a.lines.size());
    CHECK_EQ(a.points.size(), b.points.size());
    bool same = a.points.size() == b.points.size();
    for (size_t i = 0; same && i < a.points.size(); ++i) same = a.points[i].x == b.points[i].x && a.points[i].y == b.points[i].y;
    CHECK(same);
    std::vector<uint8_t> reg;
    CHECK_OK(regionField(f, t, a, reg));
    CHECK_EQ(reg.size(), size_t(16 * 64) * (16 * 64));
    // the island's centre plot (7, 6) is cell (7, 8): hex centre (64 * 7 + 32, 64 * 8)
    CHECK_EQ(int(reg[size_t(64 * 8) * 1024 + 64 * 7 + 32]), 0);
    // a far ocean plot (0, 0): cell (0, 2)
    CHECK_EQ(int(reg[size_t(64 * 2 + 10) * 1024 + 40]), 2);
    // a coast-ring plot (7 + 3, 6) is coast
    CHECK_EQ(int(reg[size_t(64 * 8) * 1024 + 64 * 10 + 32]), 1);
    // a sub-rectangle equals the same part of the whole field
    std::vector<uint8_t> part;
    CHECK_OK(regionBlocks(f, t, a, 5, 6, 9, 10, part));
    bool eq = part.size() == size_t(256) * 256;
    for (int r = 0; eq && r < 256; ++r) for (int c = 0; eq && c < 256; ++c)
        eq = part[size_t(r) * 256 + c] == reg[size_t(6 * 64 + r) * 1024 + size_t(5 * 64 + c)];
    CHECK(eq);
    // another seed moves the curves
    CoastCurves c;
    CHECK_OK(buildCoastCurves(f, t, s, 12345u, c));
    bool differs = false;
    for (size_t i = 0; i < c.points.size() && i < a.points.size(); ++i) differs = differs || c.points[i].x != a.points[i].x;
    CHECK(differs);
}

C5R_TEST(wrap_seam_island) {
    const MapFrame f = frame(16, 12, true);
    const PlotTable t = island(16, 12, 0, 6, 2, 4);   // across the x seam
    CoastCurves a;
    CHECK_OK(buildCoastCurves(f, t, styles(), 7u, a));
    CHECK(!a.lines.empty());
    std::vector<uint8_t> reg;
    CHECK_OK(regionField(f, t, a, reg));
    CHECK_EQ(int(reg[size_t(64 * 8) * 1024 + 32]), 0);          // plot (0, 6)
    CHECK_EQ(int(reg[size_t(64 * 8) * 1024 + 64 * 15 + 32]), 0); // plot (15, 6), across the seam
}

// 4913's use: the curves with the old and the new plot table. A change on a line late in the scan order leaves
// every earlier line bit-equal; the jitter of later lines is the rand stream after the earlier lines' draws.
C5R_TEST(old_and_new_tables) {
    const MapFrame f = frame(24, 12, false);
    auto both = [](const PlotTable& a, const PlotTable& b) {
        PlotTable t = a;
        for (size_t i = 0; i < t.plots.size(); ++i) t.plots[i].base.terrain = std::min(a.plots[i].base.terrain, b.plots[i].base.terrain);
        return t;
    };
    const PlotTable before = both(island(24, 12, 5, 4, 1, 3), island(24, 12, 17, 7, 1, 3));
    const PlotTable after = both(island(24, 12, 5, 4, 1, 3), island(24, 12, 17, 7, 2, 3));   // the second island grows
    const CoastStyles s = styles();
    CoastCurves a, b;
    CHECK_OK(buildCoastCurves(f, before, s, 99u, a));
    CHECK_OK(buildCoastCurves(f, after, s, 99u, b));
    CHECK(a.lines.size() >= 2 && b.lines.size() >= 2);
    // every line before the first one whose count or start differs is bit-equal in both
    auto sameLine = [](const CoastLine& p, const CoastLine& q) {
        if (p.mesh[2].size() != q.mesh[2].size()) return false;
        for (size_t j = 0; j < p.mesh[2].size(); ++j)
            if (p.mesh[2][j].x != q.mesh[2][j].x || p.mesh[2][j].y != q.mesh[2][j].y) return false;
        return true;
    };
    size_t k = 0;
    while (k < a.lines.size() && k < b.lines.size() && a.lines[k].points == b.lines[k].points &&
           a.lines[k].startX == b.lines[k].startX && a.lines[k].startY == b.lines[k].startY) ++k;
    CHECK(k >= 1);
    CHECK(k < a.lines.size());
    for (size_t i = 0; i < k; ++i) CHECK(sameLine(a.lines[i], b.lines[i]));
    // and the total draws follow the changed counts
    uint64_t sa = 0, sb = 0;
    for (const CoastLine& L : a.lines) sa += expectedDraws(L.points);
    for (const CoastLine& L : b.lines) sb += expectedDraws(L.points);
    CHECK_EQ(a.randDraws, sa); CHECK_EQ(b.randDraws, sb);
    CHECK(sa != sb);
}

C5R_TEST(negative_inputs) {
    const MapFrame f = frame(16, 12, false);
    PlotTable t = island(16, 12, 7, 6, 2, 4);
    const CoastStyles s = styles();
    CoastCurves a;
    PlotTable bad = t; bad.plots.pop_back();
    CHECK_ERR(buildCoastCurves(f, bad, s, 1u, a), Err::BadLayout);
    CHECK(a.lines.empty());
    MapFrame big = f; big.plotsW = 300;
    CHECK_ERR(buildCoastCurves(big, t, s, 1u, a), Err::Unsupported);
    MapFrame empty;
    CHECK_ERR(buildCoastCurves(empty, t, s, 1u, a), Err::BadLayout);
    CHECK_OK(buildCoastCurves(f, t, s, 1u, a));
    std::vector<uint8_t> reg;
    CHECK_ERR(regionBlocks(f, t, a, 0, 0, 17, 1, reg), Err::BadLayout);
    CHECK_ERR(regionBlocks(f, t, a, 3, 3, 3, 4, reg), Err::BadLayout);
    CHECK_ERR(regionBlocks(f, bad, a, 0, 0, 1, 1, reg), Err::BadLayout);
    CoastCurves broken = a; broken.boundary[0].end = int32_t(broken.points.size()) + 5;
    CHECK_ERR(regionBlocks(f, t, broken, 0, 0, 1, 1, reg), Err::BadLayout);
}

// MC #9538 item 1 (live-terrain.md 8.10.3): two equal neighbouring waterline points (a zero-length segment) used to
// give 1/0 in step 8, NaN offset points, INT_MIN in the fill and a line walk of about 2^31 steps. Now the segment's
// offset points are not made (counted, one per offset line), every point stays finite and the fill runs as usual;
// a point that is not finite or far outside the field is refused by the fill, never walked.
C5R_TEST(zero_length_segments) {
    const MapFrame f = frame(16, 12, false);
    const PlotTable t = island(16, 12, 7, 6, 2, 4);
    CoastCurves a;
    CHECK_OK(buildCoastCurves(f, t, styles(), 0x3DFC9816u, a));
    CHECK_EQ(a.degenerate, 0u);                        // a real coast has none (also Tiny and huge, 8.10.3)
    CHECK_EQ(a.degenerateSmooth, 0u);
    for (const CoastLine& L : a.lines) CHECK_EQ(L.degenerate, 0u);
    // the same lines with one waterline point doubled
    std::vector<CoastLine> lines = a.lines;
    CHECK(lines[0].mesh[2].size() > 20);
    lines[0].mesh[2].insert(lines[0].mesh[2].begin() + 10, lines[0].mesh[2][10]);
    CoastCurves d;
    CHECK_OK(assembleCurves(lines, d));
    CHECK_EQ(d.degenerate, 2u);                        // the segment on each of its two offset lines
    // one more waterline point; each offset line gets one segment more and skips one, so it keeps its count
    CHECK_EQ(d.points.size(), a.points.size() + 1);
    bool finite = true;
    for (const CoastPoint& p : d.points) finite = finite && std::isfinite(p.x) && std::isfinite(p.y);
    CHECK(finite);
    std::vector<uint8_t> reg;
    CHECK_OK(regionField(f, t, d, reg));
    CHECK_EQ(int(reg[size_t(64 * 8) * 1024 + 64 * 7 + 32]), 0);   // the island is still land
    CHECK_EQ(int(reg[size_t(64 * 2 + 10) * 1024 + 40]), 2);        // the far ocean still ocean
    // the doubled point at the line's end (the last segment's derivative is taken one point back)
    std::vector<CoastLine> tail = a.lines;
    tail[0].mesh[2].push_back(tail[0].mesh[2].back());
    CoastCurves e;
    CHECK_OK(assembleCurves(tail, e));
    CHECK_EQ(e.degenerate, 2u);
    // the fill refuses a point that is not finite, or far outside the field, instead of truncating it to INT_MIN
    const float bads[4] = {std::nanf(""), INFINITY, -INFINITY, 1.0e9f};
    for (float bad : bads) {
        CoastCurves n = a;
        n.points[size_t(n.fill[0].start + 3)].x = bad;
        const auto t0 = std::chrono::steady_clock::now();
        CHECK_ERR(regionField(f, t, n, reg), Err::BadLayout);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(ms < 2000.0);   // never a walk of 2^31 steps
        CoastCurves m = a;
        m.points[size_t(m.boundary[0].start + 2)].y = bad;
        CHECK_ERR(regionBlocks(f, t, m, 0, 0, 2, 2, reg), Err::BadLayout);
    }
    // a point past the seam by a field width or two (as the seam shift makes them) is not refused
    CoastCurves s = a;
    s.points[size_t(s.fill[0].start + 3)].x += 2.0f * float(f.samplesX());
    CHECK_OK(regionField(f, t, s, reg));
}

C5R_TEST(game_styles) {
    const std::string dir = c5r::test::gameDir();
    gamefs::GameFiles gf;
    CHECK_OK(gf.open(std::wstring(dir.begin(), dir.end())));
    std::vector<uint8_t> x;
    CHECK_OK(gf.read("coaststyles.xml", x));
    CoastStyles s;
    CHECK_OK(CoastStyles::parse(std::string(x.begin(), x.end()), [&](const std::string& n, std::string& t) {
        std::vector<uint8_t> b; if (!gf.read(n, b).ok()) return false; t.assign(b.begin(), b.end()); return true; }, s));
    CHECK_EQ(s.characters.size(), size_t(9));
    for (int k = 0; k < 13; ++k) { CHECK(s.present[k]); CHECK_EQ(s.style[k].offset[2], 0.0f); }
    CHECK_EQ(s.style[1].character[2], 0);
    CHECK_EQ(s.style[11].character[2], 6);
    CHECK_EQ(s.style[12].character[2], -1);
    CHECK_EQ(s.style[1].offset[0], -20.0f);
}

C5R_TEST_MAIN
