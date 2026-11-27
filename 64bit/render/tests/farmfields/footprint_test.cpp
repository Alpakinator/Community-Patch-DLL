// render/tests/farmfields/footprint_test.cpp - the field footprints as a cut mask (ticket 4853, epic 0528 D;
// c5r/farmfields/footprint.h): the cell rule (the low corner inside, even-odd), the world point through cutCell's frame, the
// bounds, the hash, a real layout's fields (every field's centroid cell set, no track sets a cell of its own), and the
// negatives. Inputs are made by the tests (D83).
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

#include "c5r/farmfields/farmfields.h"
#include "c5r/farmfields/footprint.h"
#include "c5r/forest/cut.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::farmfields;

namespace {

FieldPolygon square(float x0, float y0, float x1, float y1)
{
    FieldPolygon f;
    f.corners = {x0, y0, x1, y0, x1, y1, x0, y1};
    f.uv = {0, 0, 1, 0, 1, 1, 0, 1};
    return f;
}

const float kWorld[4] = {0, 0, 255, 127};   // 128 x 64 cells

} // namespace

C5R_TEST(a_square_sets_the_cells_whose_low_corner_lies_inside)
{
    FarmLayout l;
    // corners 10..18 in x (cols 5..9; x = 20 is the right edge: out), 20..24 in y (rows 10..12; y = 26 is the top: out)
    l.fields.push_back(square(10.0f, 20.0f, 20.0f, 26.0f));
    FieldFootprint fp;
    CHECK_OK(fp.build(l, kWorld));
    CHECK_EQ(fp.width(), 128u);
    CHECK_EQ(fp.height(), 64u);
    CHECK_EQ(fp.cellsSet(), uint64_t(5 * 3));
    for (int32_t r = 0; r < 64; ++r)
        for (int32_t c = 0; c < 128; ++c)
            CHECK_EQ(fp.cell(c, r), c >= 5 && c <= 9 && r >= 10 && r <= 12);
}

C5R_TEST(a_triangle_and_a_concave_field_follow_even_odd)
{
    FarmLayout l;
    FieldPolygon t;   // right triangle (0,0) (40,0) (0,40): corner (2c, 2r) inside when 2c + 2r < 40 (on the hypotenuse: not checked)
    t.corners = {0, 0, 40, 0, 0, 40};
    t.uv = {0, 0, 0, 0, 0, 0};
    l.fields.push_back(t);
    FieldPolygon u;   // a U opening upwards at x 100..130, y 0..30, the notch x 110..120, y 10..30
    u.corners = {100, 0, 130, 0, 130, 30, 120, 30, 120, 10, 110, 10, 110, 30, 100, 30};
    u.uv.assign(16, 0);
    l.fields.push_back(u);
    FieldFootprint fp;
    CHECK_OK(fp.build(l, kWorld));
    uint64_t tri = 0;
    for (int32_t r = 0; r < 20; ++r)
        for (int32_t c = 0; c < 20; ++c) {
            tri += fp.cell(c, r) ? 1 : 0;
            if ((2 * c) + (2 * r) == 40)
                continue;   // on the hypotenuse
            CHECK_EQ(fp.cell(c, r), (2 * c) + (2 * r) < 40);
        }
    CHECK(fp.cell(0, 0) && fp.cell(0, 19) && fp.cell(19, 0));   // the corner on the left and bottom edges: in
    CHECK(fp.cell(57, 2));    // corner (114, 4): the U's base
    CHECK(!fp.cell(57, 8));   // corner (114, 16): in the notch
    CHECK(!fp.cell(55, 8));   // corner (110, 16): the notch's left edge, the left arm's right edge: out
    CHECK(fp.cell(52, 8));    // corner (104, 16): the left arm
    CHECK(fp.cell(60, 8));    // corner (120, 16): the notch's right edge, the right arm's left edge: in
    CHECK(fp.cell(62, 8));    // corner (124, 16): the right arm
    uint64_t uCells = 0;
    for (int32_t r = 0; r < 20; ++r)
        for (int32_t c = 45; c < 70; ++c) uCells += fp.cell(c, r) ? 1 : 0;
    CHECK_EQ(fp.cellsSet(), tri + uCells);
    CHECK_EQ(uCells, uint64_t(15 * 5 + 2 * 5 * 10));   // base 15 x 5 cells, two arms 5 x 10
}

C5R_TEST(the_world_point_goes_through_cutCells_frame)
{
    FarmLayout l;
    l.fields.push_back(square(64.0f, 64.0f, 70.0f, 70.0f));   // corners 64, 66, 68: cols 32..34, rows 32..34
    FieldFootprint fp;
    CHECK_OK(fp.build(l, kWorld));
    const float ox = 12.5f, oy = 7.25f;
    fp.setFrame(ox, oy);
    // world (x, y) -> sample (ox + x, (oy + y) * 1.1547344): sample (67, 67) is in, (63, 67) and (67, 71) are not
    const float yIn = 67.0f / forest::kCutRowScale - oy;
    CHECK_EQ(fp.bitsAt(67.0f - ox, yIn), kFieldFootprintBit);
    CHECK_EQ(fp.bitsAt(63.0f - ox, yIn), uint8_t(0));
    CHECK_EQ(fp.bitsAt(67.0f - ox, 71.5f / forest::kCutRowScale - oy), uint8_t(0));
    // the same answer as a DenseCut holding the same cells through the same frame (the forest's own cut rule)
    std::vector<uint8_t> cells(static_cast<size_t>(fp.width()) * fp.height(), 0);
    for (uint32_t r = 0; r < fp.height(); ++r)
        for (uint32_t c = 0; c < fp.width(); ++c) cells[static_cast<size_t>(r) * fp.width() + c] = fp.cell(c, r) ? 1 : 0;
    forest::DenseCut dense;
    CHECK_OK(dense.set(fp.width(), fp.height(), cells, ox, oy));
    for (float wy = -20.0f; wy < 140.0f; wy += 0.37f)
        for (float wx = -20.0f; wx < 270.0f; wx += 0.53f)
            CHECK_EQ(fp.bitsAt(wx, wy), dense.bitsAt(wx, wy));
    // outside the grid: 0
    CHECK_EQ(fp.bitsAt(-1000.0f, 50.0f), uint8_t(0));
    CHECK_EQ(fp.bitsAt(50.0f, 1.0e6f), uint8_t(0));
    CHECK_EQ(fp.bitsAt(std::numeric_limits<float>::quiet_NaN(), 50.0f), uint8_t(0));
}

C5R_TEST(fields_past_the_world_are_clipped_and_tracks_set_nothing)
{
    FarmLayout l;
    l.fields.push_back(square(-30.0f, -30.0f, 4.0f, 4.0f));        // only cols 0..1, rows 0..1 inside
    l.fields.push_back(square(250.0f, 120.0f, 400.0f, 300.0f));    // only cols 125..127, rows 60..63
    TrackStrip t;
    t.xyuv = {150, 50, 0, 0, 160, 50, 1, 0, 150, 60, 0, 1};
    l.tracks.push_back(t);
    FieldFootprint fp;
    CHECK_OK(fp.build(l, kWorld));
    CHECK_EQ(fp.cellsSet(), uint64_t(2 * 2 + 3 * 4));
    CHECK(fp.cell(0, 0) && fp.cell(1, 1) && !fp.cell(2, 0));
    CHECK(fp.cell(125, 60) && fp.cell(127, 63));
    CHECK(!fp.cell(77, 26));   // under the track
}

C5R_TEST(the_hash_follows_the_cells)
{
    FarmLayout a, b, c;
    a.fields.push_back(square(10, 10, 30, 30));
    b.fields.push_back(square(10, 10, 30, 30));
    FieldPolygon dmg = square(10, 10, 30, 30);   // a pillaged farm's Crops_Damage copy: the same corners
    dmg.category = kDamage;
    b.fields.push_back(dmg);
    c.fields.push_back(square(10, 10, 32, 30));
    FieldFootprint fa, fb, fc, fe;
    CHECK_OK(fa.build(a, kWorld));
    CHECK_OK(fb.build(b, kWorld));
    CHECK_OK(fc.build(c, kWorld));
    CHECK_OK(fe.build(FarmLayout(), kWorld));
    CHECK_EQ(fa.hash(), fb.hash());
    CHECK(fa.hash() != fc.hash());
    CHECK(fa.hash() != fe.hash());
    CHECK_EQ(fe.cellsSet(), uint64_t(0));
    fa.setGeneration(7);
    CHECK_EQ(fa.generation(), uint64_t(7));
}

C5R_TEST(a_real_layouts_fields_cover_their_centroids)
{
    LayoutInput in;
    in.gameSeed = 4853;
    in.mapW = 12;
    in.mapH = 10;
    const uint32_t n[kCategoryCount] = {8, 8, 1, 1, 8, 1, 1, 1};
    for (uint32_t k = 0; k < kCategoryCount; ++k) in.types.hasSpec[k].assign(n[k], 0);
    for (int32_t y = 2; y < 8; ++y)
        for (int32_t x = 2; x < 9; ++x) {
            FarmPlot p;
            p.x = x;
            p.y = y;
            p.artType = 4;
            in.plots.push_back(p);
        }
    in.worldRect[0] = 0;
    in.worldRect[1] = 0;
    in.worldRect[2] = 12 * 64 - 1;
    in.worldRect[3] = (10 + 4) * 64 - 1;
    FarmLayout l;
    CHECK_OK(layoutFarms(in, l));
    CHECK(l.fields.size() > 20);
    FieldFootprint fp;
    CHECK_OK(fp.build(l, in.worldRect));
    // every field's area centroid lies inside it for these convex-ish faces: its cell is set when the centre test holds;
    // count the fields whose centroid cell is set (at least 95 %), and the mask's area against the fields' (within 15 %)
    double area = 0;
    size_t hit = 0;
    for (const FieldPolygon& f : l.fields) {
        const size_t m = f.corners.size() / 2;
        double a = 0, cx = 0, cy = 0;
        for (size_t i = 0, j = m - 1; i < m; j = i++) {
            const double cr = static_cast<double>(f.corners[2 * j]) * f.corners[2 * i + 1] - static_cast<double>(f.corners[2 * i]) * f.corners[2 * j + 1];
            a += cr;
            cx += (f.corners[2 * j] + f.corners[2 * i]) * cr;
            cy += (f.corners[2 * j + 1] + f.corners[2 * i + 1]) * cr;
        }
        a *= 0.5;
        cx /= 6 * a;
        cy /= 6 * a;
        area += std::fabs(a);
        hit += fp.cell(static_cast<int32_t>(cx) >> 1, static_cast<int32_t>(cy) >> 1) ? 1 : 0;
    }
    std::printf("footprint: %zu fields, %.0f samples^2, %llu cells (%.0f samples^2), centroid cells set %zu\n", l.fields.size(), area,
                static_cast<unsigned long long>(fp.cellsSet()), 4.0 * fp.cellsSet(), hit);
    CHECK(hit * 100 >= l.fields.size() * 95);
    CHECK(std::fabs(4.0 * fp.cellsSet() - area) <= 0.15 * area);
}

C5R_TEST(negatives)
{
    FarmLayout l;
    l.fields.push_back(square(10, 10, 20, 20));
    FieldFootprint fp;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float bad1[4] = {0, 0, nan, 100};
    CHECK_ERR(fp.build(l, bad1), Err::BadLayout);
    CHECK_EQ(fp.width(), 0u);
    CHECK_EQ(fp.bitsAt(15, 15), uint8_t(0));
    const float bad2[4] = {0, 0, -5, 100};
    CHECK_ERR(fp.build(l, bad2), Err::BadLayout);
    const float bad3[4] = {0, 0, 40000, 100};
    CHECK_ERR(fp.build(l, bad3), Err::BadLayout);
    FarmLayout lb;
    lb.fields.push_back(square(10, 10, 20, 20));
    lb.fields.back().corners[3] = nan;
    CHECK_ERR(fp.build(lb, kWorld), Err::BadLayout);
    CHECK_EQ(fp.cellsSet(), uint64_t(0));
    FarmLayout two;   // fewer than 3 corners: skipped
    FieldPolygon f2;
    f2.corners = {1, 1, 9, 9};
    two.fields.push_back(f2);
    CHECK_OK(fp.build(two, kWorld));
    CHECK_EQ(fp.cellsSet(), uint64_t(0));
}

C5R_TEST_MAIN
