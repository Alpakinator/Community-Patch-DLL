// render/tests/forest/placement_test.cpp - c5r::forest placement and the cut seam (epic 0431, ticket 4550).
//
// Unit tests: the CRT rand(), the rounding to 90, forests.xml, the L8 masks and the texel rule, the placement rule on a
// fixed rand() sequence (draw order, the 20 tries, size 0 past 350), the sort, the square -> offset transform, plot
// records, each hide rule, the slope normal, the Placement state, the cut grid; a negative for every refusal with its
// outputs and status. One test reads the install (C5R_GAME_DIR: forests.xml's origin, the four masks, 20 layouts); without
// it the test skips and render/build.py counts the run as failed.
//
// Proof mode (not run by ctest): c5r_forest_placement.exe --proof IN OUT [--game DIR]
//   IN: the text form written by render/evidence/0431/4550/proof_4550.py from a forest tap file and a C5HF height field;
//   OUT: per record its drawn count, hide counts and shadow rectangle (sample frame). With --game the tapped layouts are
//   also checked against the install's masks (every tree's square point accepted by its layout's mask) and against
//   squareToOffset. Exit 0 written, 2 usage, 3 input refused (the reason on stderr).
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "c5r/forest/cut.h"
#include "c5r/forest/placement.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/heightfield/heightfield.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::forest;

namespace {

const char* kVtXml =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!-- comment with <Forestry> inside -->\n"
    "<Forest num_tiles=\"4\" random_trees_per_tile=\"200\" gridded_trees_per_tile=\"0\" overlay_alpha=\".60\" "
    "space_between=\"2.00\"/>\n";

Status parseText(const std::string& s, ForestParams& p) { return parseForestsXml(s.data(), s.size(), p); }

std::string xmlWith(const std::string& attrs) { return "<Forest " + attrs + "/>"; }

// A legacy DDS: L8 (luminance 8), A8 (alpha 8) or with other dimensions.
std::vector<uint8_t> ddsBytes(uint32_t w, uint32_t h, bool alpha8, const std::vector<uint8_t>& texels)
{
    std::vector<uint8_t> b(128 + static_cast<size_t>(w) * h, 0);
    auto put = [&](size_t at, uint32_t v) { std::memcpy(&b[at], &v, 4); };
    std::memcpy(&b[0], "DDS ", 4);
    put(4, 124);
    put(8, 0x1007);         // caps, height, width, pixel format
    put(12, h);
    put(16, w);
    put(20, w);
    put(28, 1);
    put(76, 32);
    put(80, alpha8 ? 0x2u : 0x20000u);
    put(88, 8);
    put(92, alpha8 ? 0u : 0xFFu);
    put(104, alpha8 ? 0xFFu : 0u);
    put(108, 0x1000);
    for (size_t i = 0; i < texels.size() && i < static_cast<size_t>(w) * h; ++i) b[128 + i] = texels[i];
    return b;
}

TileMask fullMask()
{
    TileMask m;
    m.texels.assign(kMaskEdge * kMaskEdge, 255);
    m.nonZero = kMaskEdge * kMaskEdge;
    return m;
}

// The textbook placement (fow.h nominalPlacement): M_h2w rows (64, 0), (32, 64), 0, (32, 128).
void nominalMatrix(float m[16])
{
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = 64.0f; m[5] = 64.0f; m[4] = 32.0f; m[12] = 32.0f; m[13] = 128.0f; m[15] = 1.0f;
}

ForestParams paramsN(uint32_t n, float space)
{
    ForestParams p;
    p.numTiles = 4;
    p.randomTrees = n;
    p.rawRandom = n;
    p.spaceBetween = space;
    p.overlayAlpha = 0.6f;
    return p;
}

// A small wrapping grid of mapW x mapH plots, every sample at u16 h.
heightfield::HeightGrid flatGrid(uint32_t mapW, uint32_t mapH, uint16_t h)
{
    heightfield::HeightGrid g;
    g.mapW = mapW;
    g.mapH = mapH;
    g.samplesX = mapW * 64;
    g.samplesY = mapH * 64;
    g.wrapX = true;
    g.sx = 1.0f;
    g.sy = 0.866f;
    g.sz = 0.2f;
    g.ox = mapW * 32.0f;
    g.oy = mapH * 32.0f * 0.866f;
    nominalMatrix(g.hexToWorld);
    g.h.assign(static_cast<size_t>(g.samplesX) * g.samplesY, h);
    return g;
}

uint16_t u16For(float z) { return static_cast<uint16_t>(z / 0.2f * 255.0f + 0.5f); }

Layout layoutOf(const ForestParams& p, uint32_t seed)
{
    float m[16];
    nominalMatrix(m);
    CrtRand r(seed);
    Layout l;
    Status st = buildLayout(p, fullMask(), m, r, l);
    if (!st.ok()) std::printf("layoutOf: %s\n", st.str().c_str());
    return l;
}

} // namespace

// ---- rand, rounding, forests.xml ----

C5R_TEST(crt_rand_is_msvc_sequence)
{
    CrtRand r(1);
    const int want[10] = {41, 18467, 6334, 26500, 19169, 15724, 11478, 29358, 26962, 24464};
    for (int w : want) CHECK_EQ(r.next(), w);
    CrtRand z(0);
    CHECK_EQ(z.next(), 38);
    CHECK_EQ(z.state(), 2531011u);
}

C5R_TEST(round_to_90_as_the_exe)
{
    CHECK_EQ(roundTo90(200), 180u);
    CHECK_EQ(roundTo90(360), 360u);
    CHECK_EQ(roundTo90(135), 180u);   // 1.5 rounds away from zero
    CHECK_EQ(roundTo90(134), 90u);
    CHECK_EQ(roundTo90(45), 90u);     // float(45 / 90) is a hair above 0.5
    CHECK_EQ(roundTo90(44), 0u);
    CHECK_EQ(roundTo90(0), 0u);
    CHECK_EQ(roundTo90(4096), 4140u);
}

C5R_TEST(forests_xml_vt_and_base)
{
    ForestParams p;
    CHECK_OK(parseText(kVtXml, p));
    CHECK_EQ(p.numTiles, 4u);
    CHECK_EQ(p.rawRandom, 200u);
    CHECK_EQ(p.randomTrees, 180u);
    CHECK_EQ(p.griddedTrees, 0u);
    CHECK_EQ(p.blocksPerPlot(), 2u);
    CHECK(std::fabs(p.overlayAlpha - 0.6f) < 1e-6f);
    CHECK_EQ(p.spaceBetween, 2.0f);
    CHECK_OK(parseText(xmlWith("num_tiles='4' random_trees_per_tile='360' gridded_trees_per_tile='0' "
                               "overlay_alpha='0.60' space_between='1.33'"), p));
    CHECK_EQ(p.randomTrees, 360u);
    CHECK_EQ(p.blocksPerPlot(), 4u);
    CHECK(std::fabs(p.spaceBetween - 1.33f) < 1e-6f);
}

C5R_TEST(forests_xml_refusals)
{
    const std::string ok = "random_trees_per_tile=\"200\" gridded_trees_per_tile=\"0\" overlay_alpha=\".6\" "
                           "space_between=\"2\"";
    struct Case { std::string text; Err want; };
    const Case cases[] = {
        {"<Forestry num_tiles=\"4\"/>", Err::BadLayout},                                   // no Forest element
        {"<Forest num_tiles=\"4\" " + ok.substr(0, ok.find(" space")) + "/>", Err::BadLayout},  // attribute missing
        {xmlWith("num_tiles=\"four\" " + ok), Err::BadLayout},
        {xmlWith("num_tiles=\"0\" " + ok), Err::BadLayout},
        {xmlWith("num_tiles=\"65\" " + ok), Err::BadLayout},
        {xmlWith("num_tiles=\"4\" random_trees_per_tile=\"30\" gridded_trees_per_tile=\"0\" overlay_alpha=\".6\" "
                 "space_between=\"2\""), Err::BadLayout},                                   // rounds to 0
        {xmlWith("num_tiles=\"4\" random_trees_per_tile=\"5000\" gridded_trees_per_tile=\"0\" overlay_alpha=\".6\" "
                 "space_between=\"2\""), Err::BadLayout},                                   // 4950 > 4096
        {xmlWith("num_tiles=\"4\" random_trees_per_tile=\"-200\" gridded_trees_per_tile=\"0\" overlay_alpha=\".6\" "
                 "space_between=\"2\""), Err::BadLayout},
        {xmlWith("num_tiles=\"4\" random_trees_per_tile=\"200\" gridded_trees_per_tile=\"200\" overlay_alpha=\".6\" "
                 "space_between=\"2\""), Err::Unsupported},
        {xmlWith("num_tiles=\"4\" random_trees_per_tile=\"200\" gridded_trees_per_tile=\"0\" overlay_alpha=\"x\" "
                 "space_between=\"2\""), Err::BadLayout},
        {xmlWith("num_tiles=\"4\" random_trees_per_tile=\"200\" gridded_trees_per_tile=\"0\" overlay_alpha=\".6\" "
                 "space_between=\"-1\""), Err::BadLayout},
        {"<Forest num_tiles=\"4\" " + ok, Err::BadLayout},                                  // not closed
    };
    for (const Case& c : cases) {
        ForestParams p;
        p.numTiles = 99;
        CHECK_ERR(parseText(c.text, p), c.want);
        CHECK_EQ(p.numTiles, 0u);   // output cleared on failure
        CHECK_EQ(p.randomTrees, 0u);
    }
    ForestParams p;
    CHECK_ERR(parseForestsXml(nullptr, 0, p), Err::BadLayout);
}

// ---- masks ----

C5R_TEST(tile_mask_l8_parsed_rows_in_file_order)
{
    std::vector<uint8_t> tex(kMaskEdge * kMaskEdge, 0);
    tex[5 * kMaskEdge + 7] = 200;   // row 5, column 7
    std::vector<uint8_t> f = ddsBytes(128, 128, false, tex);
    TileMask m;
    CHECK_OK(parseTileMask(f.data(), f.size(), m));
    CHECK_EQ(m.nonZero, 1u);
    CHECK_EQ(m.texels[5 * kMaskEdge + 7], 200);
}

C5R_TEST(tile_mask_refusals)
{
    std::vector<uint8_t> tex(kMaskEdge * kMaskEdge, 1);
    TileMask m;
    std::vector<uint8_t> a8 = ddsBytes(128, 128, true, tex);
    CHECK_ERR(parseTileMask(a8.data(), a8.size(), m), Err::Unsupported);
    CHECK(m.texels.empty());
    std::vector<uint8_t> small = ddsBytes(64, 64, false, tex);
    CHECK_ERR(parseTileMask(small.data(), small.size(), m), Err::BadLayout);
    CHECK(m.texels.empty());
    std::vector<uint8_t> empty = ddsBytes(128, 128, false, std::vector<uint8_t>(kMaskEdge * kMaskEdge, 0));
    CHECK_ERR(parseTileMask(empty.data(), empty.size(), m), Err::BadLayout);
    CHECK(m.texels.empty());
    std::vector<uint8_t> cut = ddsBytes(128, 128, false, tex);
    cut.resize(1000);
    CHECK_ERR(parseTileMask(cut.data(), cut.size(), m), Err::Truncated);
    std::vector<uint8_t> magic = ddsBytes(128, 128, false, tex);
    magic[0] = 'X';
    CHECK_ERR(parseTileMask(magic.data(), magic.size(), m), Err::BadMagic);
    CHECK(m.texels.empty());
}

C5R_TEST(mask_texel_rule_truncates)
{
    TileMask m;
    auto only = [&](uint32_t v, uint32_t u) {   // the EXE's texel (v, u) = the file's texel at v x 128 + u - 1
        m.texels.assign(kMaskEdge * kMaskEdge, 0);
        m.texels[v * kMaskEdge + u - 1] = 1;
        m.nonZero = 1;
    };
    // u = trunc(x / 96 x 127), v = trunc((1 - y / 96) x 127)
    only(127, 1);
    CHECK(maskAccepts(m, 0.76f, 0.0f));    // 0.76 / 96 x 127 = 1.005: column 1, row 127
    CHECK(maskAccepts(m, 1.5f, 0.0f));     // 1.98: truncates to 1 (rounding would give 2)
    CHECK(!maskAccepts(m, 0.75f, 0.0f));   // 0.99: column 0
    CHECK(!maskAccepts(m, 0.76f, 0.75f));  // (1 - 0.75 / 96) x 127 = 126.008: row 126
    // the one-byte shift: the file's texel (127, 0) is the EXE's (127, 1)
    m.texels.assign(kMaskEdge * kMaskEdge, 0);
    m.texels[127 * kMaskEdge + 0] = 1;
    CHECK(maskAccepts(m, 0.76f, 0.0f));
    CHECK(!maskAccepts(m, 0.0f, 0.0f));
    only(126, 1);
    CHECK(maskAccepts(m, 0.76f, 0.75f));
    only(0, 127);                          // x 96, y 96: u 127, v 0
    CHECK(maskAccepts(m, 96.0f, 96.0f));
    CHECK(!maskAccepts(m, 96.8f, 96.0f));  // u 128: outside
    // u = v = 0 reads before the texels in the EXE: refused
    m.texels.assign(kMaskEdge * kMaskEdge, 1);
    CHECK(!maskAccepts(m, 0.0f, 96.0f));
    CHECK(maskAccepts(m, 0.76f, 96.0f));
    CHECK(!maskAccepts(m, -1.0f, 0.0f));
    CHECK(!maskAccepts(m, std::nanf(""), 0.0f));
    TileMask none;
    CHECK(!maskAccepts(none, 1.0f, 1.0f));
}

// ---- the placement rule ----

C5R_TEST(rule_draw_order_on_a_fixed_rand_sequence)
{
    // Spacing 0 and a full mask: every tree takes its first try, so it uses exactly three draws: size, x, y.
    const ForestParams p = paramsN(90, 0.0f);
    float m[16];
    nominalMatrix(m);
    CrtRand r(12345);
    Layout l;
    CHECK_OK(buildLayout(p, fullMask(), m, r, l));
    CHECK_EQ(l.size(), 90u);
    CrtRand ref(12345);
    std::vector<Tree> want;
    const float inv = 3.051851e-5f;
    for (int i = 0; i < 90; ++i) {
        Tree t;
        t.size = (static_cast<float>(ref.next()) * inv + 1.0f) * 0.5f;
        float x = static_cast<float>(ref.next()) * inv;
        t.square[1] = static_cast<float>(ref.next()) * inv * 96.0f;
        t.square[0] = x * 96.0f;
        want.push_back(t);
    }
    CHECK_EQ(r.state(), ref.state());
    for (const Tree& w : want) {
        auto it = std::find_if(l.begin(), l.end(), [&](const Tree& t) {
            return t.square[0] == w.square[0] && t.square[1] == w.square[1] && t.size == w.size;
        });
        CHECK(it != l.end());
        if (it != l.end()) {
            float off[3];
            CHECK_OK(squareToOffset(m, w.square[0], w.square[1], off));
            CHECK_EQ(it->offset[0], off[0]);
            CHECK_EQ(it->offset[1], off[1]);
            CHECK_EQ(it->offset[2], off[2]);
            CHECK_EQ(it->one, 1.0f);
        }
    }
    for (const Tree& t : l) CHECK(t.size >= 0.5f && t.size <= 1.0f);
    // the same seed gives the same layout, another seed another
    CHECK(layoutOf(p, 12345).size() == 90u);
    Layout a = layoutOf(p, 12345), b = layoutOf(p, 12346);
    CHECK(std::memcmp(a.data(), l.data(), sizeof(Tree) * 90) == 0);
    CHECK(std::memcmp(a.data(), b.data(), sizeof(Tree) * 90) != 0);
}

C5R_TEST(rule_twenty_tries_then_kept)
{
    // Spacing 1000: from the second tree on every try fails; the 20th try's point is kept (1 + 2 x 20 draws per tree).
    const ForestParams p = paramsN(90, 1000.0f);
    float m[16];
    nominalMatrix(m);
    CrtRand r(7), ref(7);
    Layout l;
    CHECK_OK(buildLayout(p, fullMask(), m, r, l));
    CHECK_EQ(l.size(), 90u);
    for (int i = 0; i < 3 + 89 * 41; ++i) ref.next();
    CHECK_EQ(r.state(), ref.state());
    uint32_t zero = 0;
    for (const Tree& t : l) zero += t.size == 0.0f;
    CHECK_EQ(zero, 0u);   // below 350 trees nothing gets size 0
}

C5R_TEST(rule_distance_kept_when_tries_succeed)
{
    const ForestParams p = paramsN(180, 2.0f);
    Layout l = layoutOf(p, 99);
    CHECK_EQ(l.size(), 180u);
    // with VT's spacing 2.0 most pairs keep (s_a + s_b) x 2 apart; count violations (trees whose 20 tries failed)
    uint32_t bad = 0;
    for (size_t i = 0; i < l.size(); ++i)
        for (size_t j = i + 1; j < l.size(); ++j) {
            const float dx = l[i].square[0] - l[j].square[0], dy = l[i].square[1] - l[j].square[1];
            if (std::sqrt(dx * dx + dy * dy) < (l[i].size + l[j].size) * 2.0f * 0.999f) ++bad;
        }
    std::printf("rule_distance: %u close pairs of 180 trees at spacing 2.0\n", bad);
    CHECK(bad < 20);
    const Layout sparse = layoutOf(paramsN(90, 0.5f), 99);
    uint32_t badSparse = 0;
    for (size_t i = 0; i < sparse.size(); ++i)
        for (size_t j = i + 1; j < sparse.size(); ++j) {
            const float dx = sparse[i].square[0] - sparse[j].square[0], dy = sparse[i].square[1] - sparse[j].square[1];
            if (std::sqrt(dx * dx + dy * dy) < (sparse[i].size + sparse[j].size) * 0.5f) ++badSparse;
        }
    CHECK_EQ(badSparse, 0u);
}

C5R_TEST(rule_mask_limits_the_square)
{
    TileMask left;
    left.texels.assign(kMaskEdge * kMaskEdge, 0);
    for (uint32_t v = 0; v < kMaskEdge; ++v)
        for (uint32_t u = 0; u < 63; ++u) left.texels[v * kMaskEdge + u] = 1;   // EXE columns 1..63
    left.nonZero = 63 * 128;
    float m[16];
    nominalMatrix(m);
    CrtRand r(5);
    Layout l;
    CHECK_OK(buildLayout(paramsN(180, 1.0f), left, m, r, l));
    for (const Tree& t : l) CHECK(t.square[0] < 64.0f * 96.0f / 127.0f + 0.01f);
}

C5R_TEST(rule_size_zero_past_350)
{
    Layout l = layoutOf(paramsN(360, 0.0f), 3);
    CHECK_EQ(l.size(), 360u);
    uint32_t zero = 0;
    for (const Tree& t : l) zero += t.size == 0.0f;
    CHECK_EQ(zero, 9u);   // trees 351..359
}

C5R_TEST(rule_sorted_by_square_y_descending)
{
    Layout l = layoutOf(paramsN(180, 2.0f), 11);
    for (size_t i = 1; i < l.size(); ++i) CHECK(l[i - 1].square[1] >= l[i].square[1]);
    Layout s(3);
    s[0].square[1] = 1.0f; s[0].size = 0.5f;
    s[1].square[1] = 2.0f; s[1].size = 0.6f;
    s[2].square[1] = 1.0f; s[2].size = 0.7f;
    sortLayout(s);
    CHECK_EQ(s[0].size, 0.6f);
    CHECK_EQ(s[1].size, 0.5f);   // ties keep their order
    CHECK_EQ(s[2].size, 0.7f);
}

C5R_TEST(layout_refusals)
{
    float m[16];
    nominalMatrix(m);
    CrtRand r(1);
    Layout l(3);
    CHECK_ERR(buildLayout(paramsN(0, 1.0f), fullMask(), m, r, l), Err::BadLayout);
    CHECK(l.empty());
    TileMask bad;
    CHECK_ERR(buildLayout(paramsN(90, 1.0f), bad, m, r, l), Err::BadLayout);
    float nanM[16];
    nominalMatrix(nanM);
    nanM[0] = std::nanf("");
    CHECK_ERR(buildLayout(paramsN(90, 1.0f), fullMask(), nanM, r, l), Err::BadLayout);
    float sing[16] = {0};
    CHECK_ERR(buildLayout(paramsN(90, 1.0f), fullMask(), sing, r, l), Err::BadLayout);
    CHECK(l.empty());
    // a mask whose only texel needs x = 96 and y = 0 at once: the layout never fills, refused after the cap
    TileMask corner;
    corner.texels.assign(kMaskEdge * kMaskEdge, 0);
    corner.texels[127 * kMaskEdge + 126] = 1;   // the EXE's texel (127, 127)
    corner.nonZero = 1;
    CHECK_ERR(buildLayout(paramsN(90, 0.0f), corner, m, r, l), Err::BadLayout);
    CHECK(l.empty());
    std::vector<Layout> all(2);
    ForestParams p4 = paramsN(90, 1.0f);
    CHECK_ERR(buildLayouts(p4, std::vector<TileMask>(3, fullMask()), m, r, all), Err::BadLayout);
    CHECK(all.empty());
    CHECK_OK(buildLayouts(p4, std::vector<TileMask>(4, fullMask()), m, r, all));
    CHECK_EQ(all.size(), 20u);
}

C5R_TEST(square_to_offset_round_trip)
{
    float m[16];
    nominalMatrix(m);
    float off[3];
    CHECK_OK(squareToOffset(m, 48.0f, 48.0f, off));
    CHECK(std::fabs(off[0]) < 1e-6f && std::fabs(off[1]) < 1e-6f && std::fabs(off[2]) < 1e-6f);
    CHECK_OK(squareToOffset(m, 80.0f, 20.0f, off));
    // back through M: sample = origin + (x - 48, y - 48)
    const float sx = off[0] * m[0] + off[1] * m[4] + off[2] * m[8];
    const float sy = off[0] * m[1] + off[1] * m[5] + off[2] * m[9];
    CHECK(std::fabs(sx - 32.0f) < 1e-4f && std::fabs(sy + 28.0f) < 1e-4f);
    CHECK_EQ(off[2], -(off[0] + off[1]));
}

C5R_TEST(check_layouts_refusals)
{
    Layout l = layoutOf(paramsN(90, 1.0f), 1);
    std::vector<Layout> ok(20, l);
    CHECK_OK(checkLayouts(ok, 90));
    CHECK_ERR(checkLayouts(std::vector<Layout>(), 90), Err::BadLayout);
    CHECK_ERR(checkLayouts(std::vector<Layout>(65, l), 90), Err::BadLayout);
    CHECK_ERR(checkLayouts(ok, 180), Err::BadLayout);
    CHECK_ERR(checkLayouts(ok, 100), Err::BadLayout);
    std::vector<Layout> nanL = ok;
    nanL[3][7].offset[1] = std::nanf("");
    CHECK_ERR(checkLayouts(nanL, 90), Err::BadLayout);
    std::vector<Layout> sz = ok;
    sz[0][0].size = 0.3f;
    CHECK_ERR(checkLayouts(sz, 90), Err::BadLayout);
}

// ---- records ----

C5R_TEST(records_cube_coordinates)
{
    PlotRecord r;
    CHECK_OK(makeRecord(10, 7, Kind::Forest, 3, r));
    CHECK_EQ(r.cube[0], 7);    // 10 - (7 >> 1)
    CHECK_EQ(r.cube[1], 7);
    CHECK_EQ(r.cube[2], -14);
    PlotRecord b;
    CHECK_OK(recordFromCube(r.cube, 4, 9, b));
    CHECK_EQ(b.plotX, 10);
    CHECK_EQ(b.plotY, 7);
    CHECK(b.kind == Kind::JungleHills);
    CHECK_EQ(b.layout, 9u);
    CHECK(kindOf(true, false, false) == Kind::Forest);
    CHECK(kindOf(false, true, false) == Kind::Jungle);
    CHECK(kindOf(true, false, true) == Kind::ForestHills);
    CHECK(kindOf(false, true, true) == Kind::JungleHills);
    CHECK(kindOf(false, false, true) == Kind::None);
    CHECK(kindOf(true, true, false) == Kind::None);
    CHECK_ERR(makeRecord(1, 1, Kind::None, 0, r), Err::BadLayout);
    CHECK(r.kind == Kind::None);
    const int32_t c[3] = {1, 2, -3}, bad[3] = {1, 2, 3};
    CHECK_ERR(recordFromCube(c, 0, 0, r), Err::BadLayout);
    CHECK_ERR(recordFromCube(c, 5, 0, r), Err::BadLayout);
    CHECK_ERR(recordFromCube(bad, 1, 0, r), Err::BadLayout);
    CHECK(r.kind == Kind::None);
}

// ---- per plot: positions, hide rules, blocks ----

C5R_TEST(build_plot_positions_and_blocks)
{
    heightfield::HeightGrid g = flatGrid(8, 8, u16For(20.0f));
    const ForestParams p = paramsN(180, 2.0f);
    Layout l = layoutOf(p, 21);
    PlotRecord r;
    CHECK_OK(makeRecord(3, 4, Kind::Forest, 0, r));
    PlotBlocks b;
    CHECK_OK(buildPlot(g, p, l, r, EmptyCut(), BuildOptions(), b));
    CHECK_EQ(b.blocks, 2u);
    CHECK_EQ(b.instances.size(), 180u);
    CHECK_EQ(b.counts.drawn, 180u);
    CHECK_EQ(b.drawn.size(), 180u);
    CHECK_EQ(b.shadowHalf, 6.0f);
    for (size_t i = 0; i < l.size(); ++i) {
        // world = (cube + offset) x M, then x sx - ox, y sy - oy; scale = size on kind 1
        const float hx = l[i].offset[0] + static_cast<float>(r.cube[0]), hy = static_cast<float>(r.cube[1]) + l[i].offset[1];
        const float sx = hx * 64.0f + hy * 32.0f + 32.0f, sy = hy * 64.0f + 128.0f;
        CHECK(std::fabs(b.instances[i].x - (sx - g.ox)) < 1e-3f);
        CHECK(std::fabs(b.instances[i].y - (sy * 0.866f - g.oy)) < 1e-3f);
        CHECK(std::fabs(b.instances[i].z - 20.0f) < 0.01f);
        CHECK_EQ(b.instances[i].scale, l[i].size);
        CHECK(std::fabs(b.drawn[i].sampleX - sx) < 1e-3f && std::fabs(b.drawn[i].sampleY - sy) < 1e-3f);
    }
    float mn = 1e9f, mx = -1e9f;
    for (const DrawnTree& d : b.drawn) { mn = std::min(mn, d.sampleX); mx = std::max(mx, d.sampleX); }
    CHECK_EQ(b.shadowRect[0], mn - 6.0f);
    CHECK_EQ(b.shadowRect[2], mx + 6.0f);
    // hills: 0.75 x size, quads 3.6
    PlotRecord h;
    CHECK_OK(makeRecord(3, 4, Kind::JungleHills, 0, h));
    CHECK_OK(buildPlot(g, p, l, h, EmptyCut(), BuildOptions(), b));
    CHECK_EQ(b.instances[5].scale, l[5].size * 0.75f);
    CHECK(std::fabs(b.shadowHalf - 3.6f) < 1e-6f);
}

C5R_TEST(hide_rule_water)
{
    const ForestParams p = paramsN(90, 1.0f);
    Layout l = layoutOf(p, 4);
    PlotRecord r;
    CHECK_OK(makeRecord(3, 4, Kind::Forest, 0, r));
    PlotBlocks b;
    // 10.1167 is the threshold: 2580 / 255 x 0.2 = 2.0235 below; 13 / 0.2 x 255 = 16575 -> 13.0 above
    heightfield::HeightGrid below = flatGrid(8, 8, 12896);   // 12896 / 255 x 0.2 = 10.1145 < 10.1167
    CHECK_OK(buildPlot(below, p, l, r, EmptyCut(), BuildOptions(), b));
    CHECK_EQ(b.counts.water, 90u);
    CHECK_EQ(b.counts.drawn, 0u);
    CHECK(b.drawn.empty());
    for (const Instance& in : b.instances) CHECK_EQ(in.scale, 0.0f);
    CHECK_EQ(b.shadowRect[0], 0.0f);
    heightfield::HeightGrid at = flatGrid(8, 8, 12900);      // 10.1176 >= 10.1167: drawn
    CHECK_OK(buildPlot(at, p, l, r, EmptyCut(), BuildOptions(), b));
    CHECK_EQ(b.counts.drawn, 90u);
    CHECK(std::fabs(waterHeight() - 10.116731f) < 1e-5f);
}

C5R_TEST(hide_rule_slope)
{
    // a ramp in x of 600 u16 per sample: z rises 0.47 per sample, the filter's normal z about 0.88 < 0.9
    heightfield::HeightGrid g = flatGrid(8, 8, 0);
    for (uint32_t y = 0; y < g.samplesY; ++y)
        for (uint32_t x = 0; x < g.samplesX; ++x) g.h[y * g.samplesX + x] = static_cast<uint16_t>(13000 + (x % 64) * 600);
    const ForestParams p = paramsN(90, 1.0f);
    Layout l = layoutOf(p, 4);
    PlotRecord r;
    CHECK_OK(makeRecord(3, 4, Kind::Forest, 0, r));
    PlotBlocks b;
    CHECK_OK(buildPlot(g, p, l, r, EmptyCut(), BuildOptions(), b));
    CHECK(b.counts.slope > 80u);   // the ramp's seams (x % 64 == 0) break a few windows
    BuildOptions off;
    off.slopeRule = false;
    CHECK_OK(buildPlot(g, p, l, r, EmptyCut(), off, b));
    CHECK_EQ(b.counts.slope, 0u);
    CHECK_EQ(b.counts.drawn, 90u);
}

C5R_TEST(slope_normal_values)
{
    heightfield::HeightGrid g = flatGrid(2, 2, 15000);
    CHECK_EQ(slopeNormalZ(g, 60, 60), 1.0f);
    // a gentle ramp: the filter's gradient is 1.166 x the true one (its derivative weights sum to 0.986 over 0.8456)
    for (uint32_t y = 0; y < g.samplesY; ++y)
        for (uint32_t x = 0; x < g.samplesX; ++x) g.h[y * g.samplesX + x] = static_cast<uint16_t>(15000 + x * 10);
    const float gx = 10.0f / 255.0f * 0.2f * (0.986f / 0.8456f);
    const float want = 1.0f / std::sqrt(1.0f + gx * gx);
    const float got = slopeNormalZ(g, 60, 60);
    CHECK(std::fabs(got - want) < 2e-4f);
    // a ramp in y uses sy: n = (0, -sx gy, sx sy) normalised
    for (uint32_t y = 0; y < g.samplesY; ++y)
        for (uint32_t x = 0; x < g.samplesX; ++x) g.h[y * g.samplesX + x] = static_cast<uint16_t>(15000 + y * 10);
    const float wantY = 0.866f / std::sqrt(0.866f * 0.866f + gx * gx);
    CHECK(std::fabs(slopeNormalZ(g, 60, 60) - wantY) < 2e-4f);
    heightfield::HeightGrid none;
    CHECK_EQ(slopeNormalZ(none, 0, 0), 1.0f);
}

C5R_TEST(hide_rule_cut_bits_and_size_zero)
{
    heightfield::HeightGrid g = flatGrid(8, 8, u16For(20.0f));
    const ForestParams p = paramsN(90, 1.0f);
    Layout l = layoutOf(p, 4);
    l[10].size = 0.0f;
    PlotRecord r;
    CHECK_OK(makeRecord(3, 4, Kind::Forest, 0, r));
    DenseCut cut;
    const uint32_t cw = g.samplesX / 2, ch = static_cast<uint32_t>(g.samplesY * 0.866f / 2 * 1.1547344f) + 2;
    CHECK_OK(cut.set(cw, ch, std::vector<uint8_t>(static_cast<size_t>(cw) * ch, 0x04), g.ox, g.oy));
    PlotBlocks b;
    CHECK_OK(buildPlot(g, p, l, r, cut, BuildOptions(), b));
    CHECK_EQ(b.counts.sizeZero, 1u);   // the first reason counts: size 0 before cut
    CHECK_EQ(b.counts.cut, 89u);
    CHECK_EQ(b.cutGeneration, 1u);
    CHECK_OK(cut.set(cw, ch, std::vector<uint8_t>(static_cast<size_t>(cw) * ch, 0x08), g.ox, g.oy));   // bit 3 only
    CHECK_OK(buildPlot(g, p, l, r, cut, BuildOptions(), b));
    CHECK_EQ(b.counts.cut, 0u);
    CHECK_EQ(b.counts.drawn, 89u);
    CHECK_EQ(b.instances[10].scale, 0.0f);
    CHECK_EQ(b.cutGeneration, 2u);
}

C5R_TEST(build_plot_refusals)
{
    heightfield::HeightGrid g = flatGrid(8, 8, u16For(20.0f));
    const ForestParams p = paramsN(90, 1.0f);
    Layout l = layoutOf(p, 4);
    PlotRecord r;
    CHECK_OK(makeRecord(3, 4, Kind::Forest, 0, r));
    PlotBlocks b;
    b.blocks = 9;
    CHECK_ERR(buildPlot(heightfield::HeightGrid(), p, l, r, EmptyCut(), BuildOptions(), b), Err::BadLayout);
    CHECK_EQ(b.blocks, 0u);
    CHECK(b.instances.empty());
    heightfield::HeightGrid bad = g;
    bad.hexToWorld[5] = std::nanf("");
    CHECK_ERR(buildPlot(bad, p, l, r, EmptyCut(), BuildOptions(), b), Err::BadLayout);
    CHECK_ERR(buildPlot(g, paramsN(100, 1.0f), l, r, EmptyCut(), BuildOptions(), b), Err::BadLayout);
    CHECK_ERR(buildPlot(g, paramsN(180, 1.0f), l, r, EmptyCut(), BuildOptions(), b), Err::BadLayout);   // layout short
    PlotRecord none;
    CHECK_ERR(buildPlot(g, p, l, none, EmptyCut(), BuildOptions(), b), Err::BadLayout);
    Layout nanL = l;
    nanL[0].offset[0] = std::nanf("");
    CHECK_ERR(buildPlot(g, p, nanL, r, EmptyCut(), BuildOptions(), b), Err::BadLayout);
    CHECK(b.instances.empty());
}

// ---- Placement ----

C5R_TEST(placement_records_and_blocks)
{
    Placement pl;
    PlotInput in;
    in.x = 3; in.y = 4; in.forest = true;
    CHECK_ERR(pl.setPlot(in), Err::NotFound);
    const PlotBlocks* pb = nullptr;
    heightfield::HeightGrid g = flatGrid(8, 8, u16For(20.0f));
    CHECK_ERR(pl.blocks(g, EmptyCut(), 3, 4, &pb), Err::NotFound);
    float m[16];
    nominalMatrix(m);
    CHECK_OK(pl.initWith(paramsN(180, 2.0f), std::vector<TileMask>(4, fullMask()), m, 77));
    CHECK_EQ(pl.layouts().size(), 20u);
    // the record's layout continues the stream after the layouts: rand() % 20
    CrtRand ref(77);
    std::vector<Layout> lays;
    CHECK_OK(buildLayouts(paramsN(180, 2.0f), std::vector<TileMask>(4, fullMask()), m, ref, lays));
    const uint32_t want = static_cast<uint32_t>(ref.next()) % 20u;
    CHECK_OK(pl.setPlot(in));
    CHECK(pl.record(3, 4) != nullptr);
    CHECK_EQ(pl.record(3, 4)->layout, want);
    CHECK(pl.record(3, 4)->kind == Kind::Forest);
    // unchanged kind keeps the layout and draws nothing
    CHECK_OK(pl.setPlot(in));
    CHECK_EQ(pl.record(3, 4)->layout, want);
    CHECK_OK(pl.blocks(g, EmptyCut(), 3, 4, &pb));
    CHECK(pb != nullptr);
    CHECK_EQ(pb->counts.drawn, 180u);
    const PlotBlocks* again = nullptr;
    CHECK_OK(pl.blocks(g, EmptyCut(), 3, 4, &again));
    CHECK(again == pb);
    CHECK_EQ(pl.builtCount(), 1u);
    // a cut generation change rebuilds
    DenseCut cut;
    CHECK_OK(cut.set(4, 4, std::vector<uint8_t>(16, 0), g.ox, g.oy));
    CHECK_OK(pl.blocks(g, cut, 3, 4, &pb));
    CHECK_EQ(pb->cutGeneration, 1u);
    pl.invalidate(3, 4);
    CHECK_EQ(pl.builtCount(), 0u);
    // hills: the kind changes, a new layout is drawn
    in.hills = true;
    CHECK_OK(pl.setPlotWithLayout(in, 19));
    CHECK(pl.record(3, 4)->kind == Kind::ForestHills);
    CHECK_EQ(pl.record(3, 4)->layout, 19u);
    // neither: removed
    in.forest = false;
    CHECK_OK(pl.setPlot(in));
    CHECK(pl.record(3, 4) == nullptr);
    CHECK_EQ(pl.recordCount(), 0u);
    // refusals leave the state as it was
    in.forest = true; in.jungle = true;
    CHECK_ERR(pl.setPlot(in), Err::BadLayout);
    in.jungle = false; in.x = -1;
    CHECK_ERR(pl.setPlot(in), Err::BadLayout);
    in.x = 3;
    CHECK_ERR(pl.setPlotWithLayout(in, 20), Err::BadLayout);
    CHECK_EQ(pl.recordCount(), 0u);
    CHECK_ERR(pl.blocks(g, EmptyCut(), 3, 4, &pb), Err::NotFound);
    CHECK(pb == nullptr);
    CHECK_OK(pl.setPlot(in));
    pl.invalidateAll();
    pl.clearPlots();
    CHECK_EQ(pl.recordCount(), 0u);
}

C5R_TEST(placement_with_layouts_refusals)
{
    Placement pl;
    Layout l = layoutOf(paramsN(90, 1.0f), 1);
    CHECK_ERR(pl.initWithLayouts(paramsN(180, 1.0f), std::vector<Layout>(20, l)), Err::BadLayout);
    CHECK(!pl.ready());
    CHECK_OK(pl.initWithLayouts(paramsN(90, 1.0f), std::vector<Layout>(20, l)));
    CHECK(pl.ready());
    CHECK_ERR(pl.initWith(paramsN(90, 1.0f), std::vector<TileMask>(2, fullMask()), nullptr, 1), Err::BadLayout);
    CHECK(!pl.ready());   // a failed init leaves it empty
}

// ---- the cut seam ----

C5R_TEST(cut_cell_rule)
{
    // col = int(ox + x) >> 1, row = int((oy + y) x 1.1547344) >> 1
    CutCell c = cutCell(0.0f, 0.0f, 100.0f, 86.6f);
    CHECK_EQ(c.col, 50);
    CHECK_EQ(c.row, static_cast<int32_t>(86.6f * 1.1547344f) >> 1);
    c = cutCell(-100.5f, 0.0f, 100.0f, 0.0f);   // int(-0.5) = 0
    CHECK_EQ(c.col, 0);
    c = cutCell(-103.0f, -10.0f, 100.0f, 0.0f);
    CHECK(c.col < 0 && c.row < 0);
    c = cutCell(std::nanf(""), 1e30f, 0.0f, 0.0f);
    CHECK(c.col < 0);
    CHECK(c.row > 1000000);
}

C5R_TEST(dense_cut_grid_and_blocks)
{
    DenseCut d;
    CHECK_EQ(d.bitsAt(0, 0), 0);
    std::vector<uint8_t> cells(32 * 32, 0);
    cells[3 * 32 + 5] = 0x05;
    CHECK_OK(d.set(32, 32, cells, 0.0f, 0.0f));
    CHECK_EQ(d.bitsAt(10.5f, 6.0f / 1.1547344f + 0.01f), 0x05);
    CHECK_EQ(d.bitsAt(-1.0f, 0.0f), 0);
    CHECK_EQ(d.bitsAt(64.0f, 0.0f), 0);   // col 32: outside
    CHECK_EQ(d.generation(), 1u);
    // the block form: tile 0 constant 0x02, tile 1 pooled (index 1)
    std::vector<uint8_t> dir(2 * 8, 0), pool(512, 0);
    const uint16_t one = 1;
    std::memcpy(&dir[0], &one, 2);
    dir[4] = 0x02;
    const uint32_t idx = 1;
    std::memcpy(&dir[12], &idx, 4);
    pool[256 + 2 * 16 + 3] = 0x07;
    CHECK_OK(d.setFromBlocks(2, 1, dir.data(), dir.size(), pool.data(), pool.size(), 0.0f, 0.0f));
    CHECK_EQ(d.width(), 32u);
    CHECK_EQ(d.height(), 16u);
    CHECK_EQ(d.bitsAt(0.0f, 0.0f), 0x02);
    CHECK_EQ(d.bitsAt(2.0f * (16 + 3), 2.0f * 2 / 1.1547344f + 0.01f), 0x07);
    CHECK_EQ(d.generation(), 2u);
    // refusals keep the grid
    CHECK_ERR(d.set(0, 4, {}, 0, 0), Err::BadLayout);
    CHECK_ERR(d.set(4, 4, std::vector<uint8_t>(15, 0), 0, 0), Err::BadLayout);
    CHECK_ERR(d.set(4, 4, std::vector<uint8_t>(16, 0), std::nanf(""), 0), Err::BadLayout);
    CHECK_ERR(d.setFromBlocks(2, 1, dir.data(), 8, pool.data(), pool.size(), 0, 0), Err::BadLayout);
    CHECK_ERR(d.setFromBlocks(0, 1, dir.data(), dir.size(), pool.data(), pool.size(), 0, 0), Err::BadLayout);
    CHECK_ERR(d.setFromBlocks(2, 1, dir.data(), dir.size(), pool.data(), 256, 0, 0), Err::BadLayout);
    CHECK_EQ(d.width(), 32u);
    CHECK_EQ(d.generation(), 2u);
    CHECK_EQ(d.bitsAt(0.0f, 0.0f), 0x02);
    EmptyCut e;
    CHECK_EQ(e.bitsAt(1, 2), 0);
    CHECK_EQ(e.generation(), 0u);
}

// ---- the install ----

C5R_TEST(install_forests_xml_masks_and_layouts)
{
    const std::string dir = test::gameDir();
    gamefs::GameFiles fs;
    CHECK_OK(fs.open(gamefs::fromUtf8(dir)));
    ForestParams p;
    std::vector<TileMask> masks;
    std::string origin;
    CHECK_OK(loadInstall(fs, p, masks, &origin));
    std::printf("install: forests.xml from %s: num_tiles %u, random %u -> %u, gridded %u, overlay %.2f, spacing %.2f\n",
                origin.c_str(), p.numTiles, p.rawRandom, p.randomTrees, p.griddedTrees, p.overlayAlpha, p.spaceBetween);
    std::string low = origin;
    std::transform(low.begin(), low.end(), low.begin(), [](char ch) { return static_cast<char>(std::tolower(ch)); });
    const bool vt = low.find("dlc/vt/") != std::string::npos;
    std::printf("install: Vibrant Textures' copy: %s\n", vt ? "yes" : "no");
    CHECK_EQ(p.numTiles, 4u);
    CHECK(p.randomTrees == (vt ? 180u : 360u));
    CHECK_EQ(masks.size(), 4u);
    for (size_t i = 0; i < masks.size(); ++i) {
        std::printf("install: forest_tile%zu.dds from %s, %u of 16384 texels set\n", i + 1,
                    fs.origin("forest_tile" + std::to_string(i + 1) + ".dds").c_str(), masks[i].nonZero);
        CHECK(masks[i].nonZero > 4000u && masks[i].nonZero < 16384u);
    }
    Placement pl;
    float m[16];
    nominalMatrix(m);
    CHECK_OK(pl.init(fs, m, 1234, &origin));
    CHECK_EQ(pl.layouts().size(), 20u);
    for (const Layout& l : pl.layouts()) CHECK_EQ(l.size(), p.treesPerPlot());
    for (size_t k = 0; k < pl.layouts().size(); ++k)
        for (const Tree& t : pl.layouts()[k]) CHECK(maskAccepts(masks[k % 4], t.square[0], t.square[1]));
    // a missing file is NotFound, named
    gamefs::GameFiles empty;
    ForestParams q;
    Status st = loadInstall(empty, q, masks, nullptr);
    CHECK_ERR(st, Err::NotFound);
    CHECK(st.msg.find("forests.xml") != std::string::npos);
    CHECK(masks.empty());
}

// ---- proof mode ----

namespace {

int proofMain(const std::string& inPath, const std::string& outPath, const std::string& gameDirArg)
{
    std::ifstream in(inPath);
    if (!in) { std::fprintf(stderr, "proof: cannot read %s\n", inPath.c_str()); return 3; }
    std::string word, gridPath;
    ForestParams params;
    std::vector<Layout> layouts;
    struct Rec { PlotRecord rec; uint32_t index, built; };
    std::vector<Rec> recs;
    auto bitsToFloat = [](uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; };
    while (in >> word) {
        if (word == "grid") {
            in >> std::ws;
            std::getline(in, gridPath);
        } else if (word == "params") {
            in >> params.randomTrees >> params.griddedTrees;
            params.numTiles = 4;
            params.spaceBetween = 2.0f;
        } else if (word == "layout") {
            size_t k, n;
            in >> k >> n;
            if (k != layouts.size() || n > kMaxTreesPerPlot) { std::fprintf(stderr, "proof: bad layout header\n"); return 3; }
            Layout l(n);
            for (size_t i = 0; i < n; ++i) {
                uint32_t u[7];
                for (uint32_t& x : u) in >> std::hex >> x >> std::dec;
                l[i].offset[0] = bitsToFloat(u[0]); l[i].offset[1] = bitsToFloat(u[1]); l[i].offset[2] = bitsToFloat(u[2]);
                l[i].one = bitsToFloat(u[3]); l[i].square[0] = bitsToFloat(u[4]); l[i].square[1] = bitsToFloat(u[5]);
                l[i].size = bitsToFloat(u[6]);
            }
            layouts.push_back(std::move(l));
        } else if (word == "record") {
            Rec r;
            int32_t cube[3];
            uint32_t kind, lay;
            in >> r.index >> cube[0] >> cube[1] >> cube[2] >> kind >> lay >> r.built;
            Status st = recordFromCube(cube, kind, lay, r.rec);
            if (!st.ok() || lay >= layouts.size()) { std::fprintf(stderr, "proof: record %u refused\n", r.index); return 3; }
            recs.push_back(r);
        } else {
            std::fprintf(stderr, "proof: unknown word %s\n", word.c_str());
            return 3;
        }
    }
    if (!in.eof()) { std::fprintf(stderr, "proof: input not read to the end\n"); return 3; }
    Status st = checkLayouts(layouts, params.treesPerPlot());
    if (!st.ok()) { std::fprintf(stderr, "proof: %s\n", st.str().c_str()); return 3; }
    std::ifstream gf(gridPath, std::ios::binary);
    std::vector<uint8_t> gb((std::istreambuf_iterator<char>(gf)), std::istreambuf_iterator<char>());
    heightfield::HeightGrid g;
    st = heightfield::readTap(gb.data(), gb.size(), g);
    if (!st.ok()) { std::fprintf(stderr, "proof: grid %s: %s\n", gridPath.c_str(), st.str().c_str()); return 3; }
    FILE* out = std::fopen(outPath.c_str(), "w");
    if (!out) { std::fprintf(stderr, "proof: cannot write %s\n", outPath.c_str()); return 3; }
    std::fprintf(out, "# index plotX plotY kind layout built drawn sizeZero cut water slope half minX minY maxX maxY\n");
    for (const Rec& r : recs) {
        PlotBlocks b;
        st = buildPlot(g, params, layouts[r.rec.layout], r.rec, EmptyCut(), BuildOptions(), b);
        if (!st.ok()) { std::fprintf(stderr, "proof: record %u: %s\n", r.index, st.str().c_str()); std::fclose(out); return 3; }
        std::fprintf(out, "rec %u %d %d %u %u %u %u %u %u %u %u %.4f %.4f %.4f %.4f %.4f\n", r.index, r.rec.plotX,
                     r.rec.plotY, static_cast<unsigned>(r.rec.kind), r.rec.layout, r.built, b.counts.drawn,
                     b.counts.sizeZero, b.counts.cut, b.counts.water, b.counts.slope, b.shadowHalf, b.shadowRect[0],
                     b.shadowRect[1], b.shadowRect[2], b.shadowRect[3]);
    }
    if (!gameDirArg.empty()) {
        gamefs::GameFiles fs;
        st = fs.open(gamefs::fromUtf8(gameDirArg));
        ForestParams ip;
        std::vector<TileMask> masks;
        std::string origin;
        if (st.ok()) st = loadInstall(fs, ip, masks, &origin);
        if (!st.ok()) { std::fprintf(stderr, "proof: install: %s\n", st.str().c_str()); std::fclose(out); return 3; }
        uint64_t trees = 0, accepted = 0;
        double maxOff = 0;
        for (size_t k = 0; k < layouts.size(); ++k)
            for (const Tree& t : layouts[k]) {
                ++trees;
                accepted += maskAccepts(masks[k % masks.size()], t.square[0], t.square[1]);
                float off[3];
                if (squareToOffset(g.hexToWorld, t.square[0], t.square[1], off).ok())
                    for (int i = 0; i < 3; ++i) maxOff = std::max(maxOff, static_cast<double>(std::fabs(off[i] - t.offset[i])));
            }
        std::fprintf(out, "masks %s params %u %u trees %llu accepted %llu max_offset_diff %.3g\n", origin.c_str(),
                     ip.randomTrees, ip.numTiles, static_cast<unsigned long long>(trees),
                     static_cast<unsigned long long>(accepted), maxOff);
    }
    std::fclose(out);
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc >= 2 && std::string(argv[1]) == "--proof") {
        if (argc != 4 && !(argc == 6 && std::string(argv[4]) == "--game")) {
            std::fprintf(stderr, "usage: %s --proof IN OUT [--game DIR]\n", argv[0]);
            return 2;
        }
        return proofMain(argv[2], argv[3], argc == 6 ? argv[5] : "");
    }
    return test::runAll();
}
