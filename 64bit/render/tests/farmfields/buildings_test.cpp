// render/tests/farmfields/buildings_test.cpp - c5r::farmfields' farm buildings (ticket 0653, D230): one record per farm
// plot, placed on the server's own field outlines (c5r/farmfields/buildings.h; farm-fields-layout.md 16).
//
// Unit tests on inputs the tests make (no game files, D83): the era levels, the nearest point on outlines (0653's rule),
// the stock rule of ticket 0657 (F, the centroid, the threshold, the tie), the records on the real layout (each at the
// centroid of its plot's nearest-centroid field with |F| >= 100, found by brute force, else at the plot centre), facing = the direction field + the quarter turn, era level and row with the fallback, stable for a
// seed and different between plots, pillaged, a farm removed, a farm with no field outline at its plot centre), the
// height hook, and every refusal with its status and an empty output.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "c5r/farmfields/buildings.h"
#include "c5r/farmfields/farmfields.h"
#include "c5r/farmfields/layout.h"
#include "c5r/heightfield/heightfield.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::farmfields;

namespace {

const double kPi = 3.14159265358979323846;
const double kOnOutlineTolL = 0.01;   // L: a record's position lies on its field's outline within this
const double kNearestTolL = 0.05;     // L: and no point of the plot's outlines (sampled every 0.02 L) is nearer by more

FarmTypes vpTypes()
{   // VP's farm_types.xml shape (terrain-decals.md 8.3), as layout_test.cpp
    FarmTypes t;
    const uint32_t n[kCategoryCount] = {8, 8, 1, 1, 8, 1, 1, 1};
    for (uint32_t c = 0; c < kCategoryCount; ++c) t.hasSpec[c].assign(n[c], 0);
    return t;
}

// A W x H map, every third plot of a pattern a farm of art type `art` (layout_test.cpp's farmland with all = false).
LayoutInput farmland(uint32_t w, uint32_t h, uint8_t art, uint32_t seed = 4242)
{
    LayoutInput in;
    in.gameSeed = seed;
    in.mapW = w;
    in.mapH = h;
    in.worldRect[2] = static_cast<float>(64 * w - 1);
    in.worldRect[3] = static_cast<float>(64 * (h + 4) - 1);
    in.types = vpTypes();
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            if ((x + 2 * y) % 3 != 0) continue;
            FarmPlot p;
            p.x = static_cast<int32_t>(x);
            p.y = static_cast<int32_t>(y);
            p.artType = art;
            in.plots.push_back(p);
        }
    return in;
}

FarmRowChoice rowsMedievalModern()
{   // the CP + VP table: three Ancient rows (level 1), three Industrial rows (level 3)
    FarmRowChoice r;
    r.level[1] = {100, 101, 102};
    r.level[3] = {200, 201, 202};
    r.farmHandlerRows = 24;
    return r;
}

double segDist(double px, double py, double ax, double ay, double bx, double by)
{
    const double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    double t = l2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0;
    t = std::max(0.0, std::min(1.0, t));
    const double qx = ax + t * dx - px, qy = ay + t * dy - py;
    return std::sqrt(qx * qx + qy * qy);
}

// distance in L from (px, py) (L) to field f's outline
double outlineDist(const FieldPolygon& f, double px, double py)
{
    const size_t n = f.corners.size() / 2;
    double best = 1e30;
    for (size_t i = 0; i < n; ++i) {
        const size_t j = (i + 1) % n;
        best = std::min(best, segDist(px, py, f.corners[2 * i], f.corners[2 * i + 1] / kLayoutYScale, f.corners[2 * j],
                                      f.corners[2 * j + 1] / kLayoutYScale));
    }
    return best;
}

int32_t find(const LayoutInput& in, int32_t x, int32_t y)
{
    for (size_t i = 0; i < in.plots.size(); ++i)
        if (in.plots[i].x == x && in.plots[i].y == y) return static_cast<int32_t>(i);
    return -1;
}

}  // namespace

C5R_TEST(era_levels_and_row_levels)
{
    CHECK_EQ(farmEraLevel(-1), 1);
    CHECK_EQ(farmEraLevel(0), 1);
    CHECK_EQ(farmEraLevel(2), 1);
    CHECK_EQ(farmEraLevel(3), 2);
    CHECK_EQ(farmEraLevel(4), 3);
    CHECK_EQ(farmEraLevel(5), 4);
    CHECK_EQ(farmEraLevel(7), 4);
    CHECK_EQ(landmarkEraIndex("Ancient"), 0);
    CHECK_EQ(landmarkEraIndex("INDUSTRIAL"), 4);
    CHECK_EQ(landmarkEraIndex("Any"), -1);
    CHECK_EQ(landmarkEraIndex(""), -1);
    CHECK_EQ(landmarkEraIndex("Steampunk"), -2);
    CHECK_EQ(farmRowLevel("Ancient"), 1);
    CHECK_EQ(farmRowLevel("Industrial"), 3);
    CHECK_EQ(farmRowLevel("Modern"), 4);
    CHECK_EQ(farmRowLevel("Any"), 0);
    CHECK_EQ(farmRowLevel("Steampunk"), -1);
}

C5R_TEST(nearest_point_on_a_square_outline)
{
    // a square 0..10 x 0..10 in L (corners in the sample frame: y x kLayoutYScale)
    FieldPolygon sq;
    const double c[8] = {0, 0, 10, 0, 10, 10, 0, 10};
    for (int i = 0; i < 8; ++i) sq.corners.push_back(static_cast<float>(i % 2 ? c[i] * kLayoutYScale : c[i]));
    std::vector<FieldPolygon> fs(1, sq);
    float out[2];
    int32_t field = -2;
    double d = -1;
    CHECK(nearestOnOutlines(fs, {0}, 3, 4, out, field, d));   // inside: the left edge is nearest (3)
    CHECK_EQ(field, 0);
    CHECK(std::fabs(out[0] - 0.0f) < 1e-4f);
    CHECK(std::fabs(out[1] / kLayoutYScale - 4.0) < 1e-4);
    CHECK(std::fabs(d - 3.0) < 1e-6);
    CHECK(nearestOnOutlines(fs, {0}, 13, 14, out, field, d));   // outside, past the corner (10, 10)
    CHECK(std::fabs(out[0] - 10.0f) < 1e-4f && std::fabs(out[1] / kLayoutYScale - 10.0) < 1e-4);
    CHECK(std::fabs(d - 5.0) < 1e-6);
    // no outline: false, outputs untouched
    field = 7;
    CHECK(!nearestOnOutlines(fs, {}, 3, 4, out, field, d));
    CHECK(!nearestOnOutlines(fs, {5}, 3, 4, out, field, d));   // an index outside the list is skipped
    CHECK_EQ(field, 7);
}

// a polygon given in L (corners stored in the sample frame)
FieldPolygon polyL(const std::vector<double>& l)
{
    FieldPolygon f;
    for (size_t i = 0; i < l.size(); ++i) f.corners.push_back(static_cast<float>(i % 2 ? l[i] * kLayoutYScale : l[i]));
    return f;
}

C5R_TEST(stock_rule_f_centroid_threshold_and_tie)
{
    // ticket 0657 (cities-and-landmarks.md 2.6.2 (1)): F = 0.5 x sum x_prev (y - y_prev); an axis-aligned rectangle
    // gives half its area, signed by the winding
    const FieldPolygon sq = polyL({0, 0, 20, 0, 20, 10, 0, 10});
    CHECK(std::fabs(stockFieldF(sq) - 100.0) < 1e-3);
    const FieldPolygon cw = polyL({0, 0, 0, 10, 20, 10, 20, 0});
    CHECK(std::fabs(stockFieldF(cw) + 100.0) < 1e-3);
    double c[2];
    CHECK(stockFieldCentroid(sq, c));
    CHECK(std::fabs(c[0] - 10.0) < 1e-4 && std::fabs(c[1] - 5.0) < 1e-4);
    // a triangle: F is not half its area in general (area 50, F = 0.5 x (0 x 0 + 10 x 10 + 0 x -10) = 50)
    const FieldPolygon tri = polyL({0, 0, 10, 0, 0, 10});
    CHECK(std::fabs(stockFieldF(tri) - 25.0) > 1.0);
    CHECK(stockFieldCentroid(tri, c));
    CHECK(std::fabs(c[0] - 10.0 / 3) < 1e-4 && std::fabs(c[1] - 10.0 / 3) < 1e-4);
    // zero area: no centroid
    CHECK(!stockFieldCentroid(polyL({0, 0, 10, 0, 20, 0}), c));
    CHECK(!stockFieldCentroid(polyL({0, 0, 10, 0}), c));
    // the rule: a field below the threshold never wins, even when nearest; the nearest qualifying centroid wins
    std::vector<FieldPolygon> fs;
    fs.push_back(polyL({-2, -2, 2, -2, 2, 2, -2, 2}));        // F 8 (< 100), centroid at the plot centre
    fs.push_back(polyL({30, 0, 50, 0, 50, 20, 30, 20}));      // F 200, centroid (40, 10)
    fs.push_back(polyL({-40, -10, -20, -10, -20, 10, -40, 10}));   // F 200, centroid (-30, 0): nearest of the two
    float out[2];
    int32_t field = -2;
    double d = -1;
    CHECK(stockPlacement(fs, {0, 1, 2}, 0, 0, out, field, d));
    CHECK_EQ(field, 2);
    CHECK(std::fabs(out[0] + 30.0f) < 1e-4f && std::fabs(out[1] / kLayoutYScale) < 1e-4);
    CHECK(std::fabs(d - 30.0) < 1e-6);
    // a tie: the first in `which` wins (strictly nearer replaces)
    fs.push_back(polyL({20, -10, 40, -10, 40, 10, 20, 10}));   // centroid (30, 0)
    CHECK(stockPlacement(fs, {3, 2}, 0, 0, out, field, d));
    CHECK_EQ(field, 3);
    CHECK(stockPlacement(fs, {2, 3}, 0, 0, out, field, d));
    CHECK_EQ(field, 2);
    // only small fields: false, outputs untouched
    field = 7;
    CHECK(!stockPlacement(fs, {0}, 0, 0, out, field, d));
    CHECK(!stockPlacement(fs, {}, 0, 0, out, field, d));
    CHECK(!stockPlacement(fs, {9}, 0, 0, out, field, d));
    CHECK_EQ(field, 7);
}

C5R_TEST(records_sit_on_the_stock_rule_and_face_the_field)
{
    const LayoutInput in = farmland(16, 12, 3);
    FarmLayout lay;
    FarmDecals dec;
    CHECK_OK(produce(in, lay, dec));
    BuildingFacts facts;
    std::vector<FarmBuilding> b;
    CHECK_OK(placeBuildings(in, lay, facts, rowsMedievalModern(), nullptr, b));
    CHECK_EQ(b.size(), in.plots.size());
    const FieldParams fp = fieldParamsFor(in.gameSeed);
    size_t withField = 0, centreNoQualifying = 0, smallFields = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        const FarmBuilding& r = b[i];
        CHECK_EQ(r.plot, static_cast<uint32_t>(i));
        CHECK_EQ(r.x, in.plots[i].x);
        CHECK_EQ(r.y, in.plots[i].y);
        CHECK(!r.hasHeight);
        const double px = r.pos[0], py = r.pos[1] / kLayoutYScale;
        // the rule by brute force: the plot's fields with |F| >= 100, the centroid nearest the plot centre
        double cc[2];
        plotCentreLayout(r.x, r.y, cc);
        int32_t want = -1;
        double wantD = 1e300, wantC[2] = {cc[0], cc[1]};
        for (size_t f = 0; f < lay.fields.size(); ++f) {
            if (lay.fields[f].plot != i) continue;
            if (std::fabs(stockFieldF(lay.fields[f])) < kStockFieldMinF) { ++smallFields; continue; }
            double c[2];
            if (!stockFieldCentroid(lay.fields[f], c)) continue;
            const double d = (c[0] - cc[0]) * (c[0] - cc[0]) + (c[1] - cc[1]) * (c[1] - cc[1]);
            if (d < wantD) { wantD = d; want = static_cast<int32_t>(f); wantC[0] = c[0]; wantC[1] = c[1]; }
        }
        CHECK_EQ(r.field, want);
        CHECK(std::fabs(px - wantC[0]) < 1e-3 && std::fabs(py - wantC[1]) < 1e-3);
        if (want < 0) { ++centreNoQualifying; CHECK_EQ(r.distance, 0.0f); }
        else { ++withField; CHECK(std::fabs(r.distance - std::sqrt(wantD)) < 1e-3); }
        // facing: the direction field at the position (clamped into the world) plus the quarter turn, in [0, 2 pi)
        const double fx = std::min(std::max(px, static_cast<double>(in.worldRect[0])), static_cast<double>(in.worldRect[2]));
        const double fy = std::min(std::max(static_cast<double>(r.pos[1]), static_cast<double>(in.worldRect[1])),
                                   static_cast<double>(in.worldRect[3])) / kLayoutYScale;
        double a = fieldAngle(fp, fx, fy) + r.quarter * kPi / 2;
        a = std::fmod(a, 2 * kPi);
        if (a < 0) a += 2 * kPi;
        CHECK(r.facing >= 0.0f && r.facing < static_cast<float>(2 * kPi));
        CHECK(std::fabs(r.facing - a) < 1e-3 || std::fabs(std::fabs(r.facing - a) - 2 * kPi) < 1e-3);
        CHECK(r.quarter < 4);
    }
    // art type 3 with children: nearly every farm of this pattern has a qualifying field
    CHECK(withField * 10 >= b.size() * 9);
    std::printf("records %zu, at a field's centroid %zu, at the plot centre (no field with |F| >= 100) %zu, fields below the "
                "threshold %zu\n",
                b.size(), withField, centreNoQualifying, smallFields);
}

C5R_TEST(nearest_outline_rule_of_0653_still_answers)
{
    // nearestOnOutlines stays (0653's rule, not the EXE's: placeBuildings uses stockPlacement since 0657)
    const LayoutInput in = farmland(16, 12, 3);
    FarmLayout lay;
    FarmDecals dec;
    CHECK_OK(produce(in, lay, dec));
    size_t onOutline = 0, plots = 0;
    double worstOn = 0, worstNearest = 0;
    for (size_t i = 0; i < in.plots.size(); ++i) {
        double c[2];
        plotCentreLayout(in.plots[i].x, in.plots[i].y, c);
        std::vector<uint32_t> mine;
        for (uint32_t f = 0; f < lay.fields.size(); ++f)
            if (lay.fields[f].plot == i) mine.push_back(f);
        ++plots;
        float pos[2];
        int32_t field = -1;
        double dd = 0;
        if (!nearestOnOutlines(lay.fields, mine, c[0], c[1], pos, field, dd)) continue;
        ++onOutline;
        const double px = pos[0], py = pos[1] / kLayoutYScale;
        CHECK(static_cast<size_t>(field) < lay.fields.size());
        CHECK_EQ(lay.fields[field].plot, static_cast<uint32_t>(i));
        const double on = outlineDist(lay.fields[field], px, py);
        worstOn = std::max(worstOn, on);
        CHECK(on <= kOnOutlineTolL);
        // the nearest point of the plot's outlines to the plot centre: dense sampling finds none nearer
        double best = 1e30;
        for (const FieldPolygon& f : lay.fields) {
            if (f.plot != i) continue;
            const size_t n = f.corners.size() / 2;
            for (size_t k = 0; k < n; ++k) {
                const size_t j = (k + 1) % n;
                const double ax = f.corners[2 * k], ay = f.corners[2 * k + 1] / kLayoutYScale;
                const double bx = f.corners[2 * j], by = f.corners[2 * j + 1] / kLayoutYScale;
                const double len = std::sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
                const int steps = std::max(1, static_cast<int>(len / 0.02));
                for (int s = 0; s <= steps; ++s) {
                    const double t = static_cast<double>(s) / steps;
                    const double qx = ax + t * (bx - ax), qy = ay + t * (by - ay);
                    best = std::min(best, std::sqrt((qx - c[0]) * (qx - c[0]) + (qy - c[1]) * (qy - c[1])));
                }
            }
        }
        const double got = std::sqrt((px - c[0]) * (px - c[0]) + (py - c[1]) * (py - c[1]));
        worstNearest = std::max(worstNearest, got - best);
        CHECK(got <= best + kNearestTolL);
        CHECK(std::fabs(dd - got) < 0.01);
    }
    // art type 3 with children: nearly every farm of this pattern has fields (a map-edge plot may have no face)
    CHECK(onOutline * 10 >= plots * 9);
    std::printf("plots %zu, with an outline %zu, worst distance from the outline %.6f L, worst excess over the sampled "
                "nearest %.6f L\n",
                plots, onOutline, worstOn, worstNearest);
}

C5R_TEST(era_level_and_row_with_the_fallback)
{
    const LayoutInput in = farmland(16, 12, 3);
    FarmLayout lay;
    FarmDecals dec;
    CHECK_OK(produce(in, lay, dec));
    BuildingFacts facts;
    facts.owner.assign(in.plots.size(), -1);
    // owners: plot 0 none, 1 player 1 (Ancient), 2 player 2 (Renaissance -> level 2 -> rows of 1),
    // 3 player 3 (Industrial -> 3), 4 player 4 (Modern -> 4 -> rows of 3), 5 player 5 (era unknown -> 1)
    for (int p = 1; p <= 5; ++p) facts.owner[p] = static_cast<int8_t>(p);
    facts.eraOfPlayer[1] = 0;
    facts.eraOfPlayer[2] = 3;
    facts.eraOfPlayer[3] = 4;
    facts.eraOfPlayer[4] = 5;
    const FarmRowChoice rows = rowsMedievalModern();
    std::vector<FarmBuilding> b;
    CHECK_OK(placeBuildings(in, lay, facts, rows, nullptr, b));
    const int wantLevel[6] = {1, 1, 2, 3, 4, 1};
    const int wantRowLevel[6] = {1, 1, 1, 3, 3, 1};
    for (int i = 0; i < 6; ++i) {
        CHECK_EQ(static_cast<int>(b[i].eraLevel), wantLevel[i]);
        CHECK_EQ(static_cast<int>(b[i].rowLevel), wantRowLevel[i]);
        const std::vector<int32_t>& l = rows.level[wantRowLevel[i]];
        CHECK(std::find(l.begin(), l.end(), b[i].row) != l.end());
        CHECK_EQ(b[i].row, l[b[i].pick % l.size()]);
    }
    CHECK_EQ(b[0].owner, static_cast<int8_t>(-1));
    CHECK_EQ(b[0].era, -1);
    CHECK_EQ(b[5].era, -1);
    // no rows at the level or below: row -1, rowLevel 0xFF (the drawer draws nothing and counts it)
    FarmRowChoice high;
    high.level[4] = {300};
    CHECK_OK(placeBuildings(in, lay, facts, high, nullptr, b));
    CHECK_EQ(b[1].row, -1);
    CHECK_EQ(static_cast<int>(b[1].rowLevel), 0xFF);
    CHECK_EQ(b[4].row, 300);
    // a level-0 row (Era Any) is the last fallback
    FarmRowChoice any;
    any.level[0] = {400};
    CHECK_OK(placeBuildings(in, lay, facts, any, nullptr, b));
    for (const FarmBuilding& r : b) {
        CHECK_EQ(r.row, 400);
        CHECK_EQ(static_cast<int>(r.rowLevel), 0);
    }
    // no table at all: every row -1
    CHECK_OK(placeBuildings(in, lay, facts, FarmRowChoice(), nullptr, b));
    for (const FarmBuilding& r : b) CHECK_EQ(r.row, -1);
}

C5R_TEST(stable_for_a_seed_and_different_between_plots)
{
    const LayoutInput in = farmland(16, 12, 3);
    FarmLayout lay;
    FarmDecals dec;
    CHECK_OK(produce(in, lay, dec));
    const FarmRowChoice rows = rowsMedievalModern();
    std::vector<FarmBuilding> a, b;
    CHECK_OK(placeBuildings(in, lay, BuildingFacts(), rows, nullptr, a));
    // a second production of the same input (another load of the same save) gives the same records
    FarmLayout lay2;
    FarmDecals dec2;
    CHECK_OK(produce(in, lay2, dec2));
    CHECK_OK(placeBuildings(in, lay2, BuildingFacts(), rows, nullptr, b));
    CHECK(a == b);
    // different between plots: the rows and quarters are not all alike
    std::set<int32_t> rowsSeen;
    std::set<int> quarters;
    std::set<uint32_t> picks;
    for (const FarmBuilding& r : a) {
        rowsSeen.insert(r.row);
        quarters.insert(r.quarter);
        picks.insert(r.pick);
    }
    CHECK_EQ(rowsSeen.size(), static_cast<size_t>(3));
    CHECK_EQ(quarters.size(), static_cast<size_t>(4));
    CHECK_EQ(picks.size(), a.size());
    // the seeds are pure functions of (game seed, plot); another game seed picks differently
    CHECK_EQ(buildingPick(4242, 3, 4), buildingPick(4242, 3, 4));
    CHECK(buildingPick(4242, 3, 4) != buildingPick(4243, 3, 4));
    CHECK(buildingPick(4242, 3, 4) != buildingPick(4242, 4, 3));
    size_t differ = 0;
    for (int32_t x = 0; x < 64; ++x) differ += buildingQuarter(1, x, 0) != buildingQuarter(2, x, 0);
    CHECK(differ > 32);
}

C5R_TEST(pillaged_removed_and_without_a_field)
{
    LayoutInput in = farmland(16, 12, 3);
    FarmLayout lay;
    FarmDecals dec;
    CHECK_OK(produce(in, lay, dec));
    const FarmRowChoice rows = rowsMedievalModern();
    std::vector<FarmBuilding> base;
    CHECK_OK(placeBuildings(in, lay, BuildingFacts(), rows, nullptr, base));
    const int32_t at = find(in, 5, 5);
    CHECK(at >= 0);
    // pillaged: the record says so; its place, row and facing stay (the Crops_Damage copies share the outlines)
    LayoutInput pil = in;
    pil.plots[at].state = FarmState::Pillaged;
    FarmLayout lp;
    FarmDecals dp;
    CHECK_OK(produce(pil, lp, dp));
    std::vector<FarmBuilding> p;
    CHECK_OK(placeBuildings(pil, lp, BuildingFacts(), rows, nullptr, p));
    CHECK(p[at].pillaged);
    CHECK(p[at].state == FarmState::Pillaged);
    CHECK(!base[at].pillaged);
    CHECK(std::memcmp(p[at].pos, base[at].pos, sizeof(p[at].pos)) == 0);
    CHECK_EQ(p[at].row, base[at].row);
    CHECK_EQ(p[at].quarter, base[at].quarter);
    for (size_t i = 0; i < p.size(); ++i) {
        if (static_cast<int32_t>(i) == at) continue;
        FarmBuilding want = base[i];
        want.field = p[i].field;   // the damage copies move the later field indices
        CHECK(p[i] == want);
    }
    // removed: its record is gone; every other farm's record is the same but for the plot index
    LayoutInput less = in;
    less.plots.erase(less.plots.begin() + at);
    FarmLayout ll;
    FarmDecals dl;
    CHECK_OK(produce(less, ll, dl));
    std::vector<FarmBuilding> l;
    CHECK_OK(placeBuildings(less, ll, BuildingFacts(), rows, nullptr, l));
    CHECK_EQ(l.size(), base.size() - 1);
    size_t same = 0;
    for (const FarmBuilding& r : l) {
        CHECK(!(r.x == 5 && r.y == 5));
        const int32_t j = find(in, r.x, r.y);
        FarmBuilding want = base[j];
        want.plot = r.plot;
        want.field = r.field;   // the field indices move with the list
        same += r == want;
    }
    CHECK_EQ(same, l.size());
    // a farm with no field outline (art type 0: no continent, stock draws no field): at its plot centre, field -1,
    // distance 0, facing from the field there; the other farms keep theirs
    LayoutInput bare = in;
    bare.plots[at].artType = 0;
    FarmLayout lbare;
    FarmDecals dbare;
    CHECK_OK(produce(bare, lbare, dbare));
    for (const FieldPolygon& f : lbare.fields) CHECK(f.plot != static_cast<uint32_t>(at));
    std::vector<FarmBuilding> nb;
    CHECK_OK(placeBuildings(bare, lbare, BuildingFacts(), rows, nullptr, nb));
    float c[2];
    plotCentre(5, 5, c);
    CHECK_EQ(nb[at].field, -1);
    CHECK(std::fabs(nb[at].pos[0] - c[0]) < 1e-3f && std::fabs(nb[at].pos[1] - c[1]) < 1e-3f);
    CHECK_EQ(nb[at].distance, 0.0f);
    CHECK(nb[at].row >= 0);   // still a building (the EXE's placer keeps the plot centre for a record without curves)
}

C5R_TEST(height_from_the_field_when_given)
{
    const LayoutInput in = farmland(8, 6, 4);
    FarmLayout lay;
    FarmDecals dec;
    CHECK_OK(produce(in, lay, dec));
    heightfield::HeightGrid g;
    g.mapW = 8;
    g.mapH = 10;
    g.samplesX = 8 * 64;
    g.samplesY = 10 * 64;
    g.sz = 0.5f;
    g.h.assign(static_cast<size_t>(g.samplesX) * g.samplesY, 0);
    for (uint32_t y = 0; y < g.samplesY; ++y)
        for (uint32_t x = 0; x < g.samplesX; ++x) g.h[g.index(x, y)] = static_cast<uint16_t>((x + y) % 256);
    std::vector<FarmBuilding> b;
    CHECK_OK(placeBuildings(in, lay, BuildingFacts(), rowsMedievalModern(), &g, b));
    CHECK(!b.empty());
    for (const FarmBuilding& r : b) {
        CHECK(r.hasHeight);
        const float cx = std::min(std::max(r.pos[0], in.worldRect[0]), in.worldRect[2]);   // 0657: read at the clamped point
        const float cy = std::min(std::max(r.pos[1], in.worldRect[1]), in.worldRect[3]);
        const float want = heightfield::heightAtSample(g, static_cast<int>(std::floor(cx)), static_cast<int>(std::floor(cy)));
        CHECK_EQ(r.z, want);
    }
}

C5R_TEST(refusals_leave_the_output_empty)
{
    const LayoutInput in = farmland(16, 12, 3);
    FarmLayout lay;
    FarmDecals dec;
    CHECK_OK(produce(in, lay, dec));
    std::vector<FarmBuilding> b(3);
    // owners neither empty nor one per plot
    BuildingFacts f;
    f.owner.assign(2, 0);
    CHECK_ERR(placeBuildings(in, lay, f, rowsMedievalModern(), nullptr, b), Err::BadLayout);
    CHECK(b.empty());
    // a row id below 0
    FarmRowChoice bad = rowsMedievalModern();
    bad.level[2] = {-5};
    b.resize(3);
    CHECK_ERR(placeBuildings(in, lay, BuildingFacts(), bad, nullptr, b), Err::BadLayout);
    CHECK(b.empty());
    // a field naming a plot outside the input
    FarmLayout broken = lay;
    broken.fields[0].plot = static_cast<uint32_t>(in.plots.size() + 4);
    b.resize(3);
    CHECK_ERR(placeBuildings(in, broken, BuildingFacts(), rowsMedievalModern(), nullptr, b), Err::BadLayout);
    CHECK(b.empty());
    // an input checkInput refuses (no plots in the map)
    LayoutInput none = in;
    none.mapW = 0;
    b.resize(3);
    CHECK_ERR(placeBuildings(none, lay, BuildingFacts(), rowsMedievalModern(), nullptr, b), Err::BadLayout);
    CHECK(b.empty());
    // an empty input: no records, no error
    LayoutInput empty = in;
    empty.plots.clear();
    CHECK_OK(placeBuildings(empty, FarmLayout(), BuildingFacts(), rowsMedievalModern(), nullptr, b));
    CHECK(b.empty());
}

C5R_TEST(record_text_and_equality)
{
    FarmBuilding a;
    a.x = 3;
    a.y = 4;
    a.row = 101;
    a.rowLevel = 1;
    const std::string t = buildingText(a);
    CHECK(t.find("3,4 ") == 0);
    CHECK(t.find("row=101") != std::string::npos);
    CHECK(t.find("z=-") != std::string::npos);
    FarmBuilding b = a;
    CHECK(a == b);
    b.facing = 1e-7f;
    CHECK(a != b);
    FarmRowChoice r1 = rowsMedievalModern(), r2 = rowsMedievalModern();
    CHECK(r1 == r2);
    CHECK(r1.any());
    r2.refused.push_back(FarmRowRefusal());
    CHECK(r1 != r2);
    CHECK(!FarmRowChoice().any());
}

C5R_TEST_MAIN
