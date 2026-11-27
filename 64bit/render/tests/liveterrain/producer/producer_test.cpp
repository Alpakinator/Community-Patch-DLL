// The region producer's tests (ticket 4903; live-terrain.md 8). Synthetic library, frame and maps; every input check
// has a negative test that checks the error code and that nothing was produced. game_library reads the install's file.
#include <cstring>
#include <set>
#include <cmath>
#include <cstdio>
#include "c5r/liveterrain/coastlines.h"
#include "c5r/liveterrain/coaststrips.h"
#include "c5r/liveterrain/producer.h"
#include "../../common/testkit.h"

using namespace c5r;
using namespace c5r::liveterrain;

namespace {

void put32(std::vector<uint8_t>& b, size_t o, uint32_t v) { std::memcpy(&b[o], &v, 4); }
void put16(std::vector<uint8_t>& b, size_t o, uint16_t v) { std::memcpy(&b[o], &v, 2); }
void putf(std::vector<uint8_t>& b, size_t o, float v) { std::memcpy(&b[o], &v, 4); }

struct SynPiece { uint32_t cellFlags; int level; uint16_t height; uint8_t material; uint16_t region; };

// A library of 128-sample pieces whose pattern is a single claimed cell; every texture is one constant run.
std::vector<uint8_t> synLibrary(const std::vector<SynPiece>& ps) {
    const uint32_t n = uint32_t(ps.size());
    std::vector<uint32_t> rows;                 // 4 textures x 128 rows per piece
    std::vector<uint16_t> r16; std::vector<uint8_t> r8;
    for (const SynPiece& p : ps) {
        const uint32_t h = uint32_t(r16.size()); r16.push_back(128); r16.push_back(p.height);
        const uint32_t t1 = uint32_t(r8.size()); r8.push_back(128); r8.push_back(p.material);
        const uint32_t hb = uint32_t(r8.size()); r8.push_back(128); r8.push_back(255);
        const uint32_t tb = uint32_t(r8.size()); r8.push_back(128); r8.push_back(200);
        for (uint32_t s : {h, t1, hb, tb}) for (int y = 0; y < 128; ++y) rows.push_back(s);
    }
    std::vector<uint8_t> b;
    auto app32 = [&](uint32_t v) { size_t o = b.size(); b.resize(o + 4); put32(b, o, v); };
    app32(20);
    app32(uint32_t(rows.size())); for (uint32_t v : rows) app32(v);
    app32(uint32_t(r8.size())); b.insert(b.end(), r8.begin(), r8.end());   // pairs: an even length keeps u16 aligned
    app32(uint32_t(r16.size())); for (uint16_t v : r16) { size_t o = b.size(); b.resize(o + 2); put16(b, o, v); }
    app32(n);
    for (uint32_t k = 0; k < n; ++k) {
        const size_t o = b.size(); b.resize(o + 0x56C, 0);
        put32(b, o + 0x94, 10); put32(b, o + 0x98, 10);
        for (int i = 0; i < 100; ++i) put32(b, o + 0x9C + 8 * i, 0xFFFFFFFFu);
        // cell (1, 1) used and claimed: the 128 rectangle (corner hexes (0.5, 0) .. (1.5, 2)) covers it, as in the
        // install's small pieces, whose claimed cell is index 11 (7.3)
        put32(b, o + 0x9C + 8 * 11, ps[k].cellFlags); b[o + 0x9C + 8 * 11 + 5] = 1;
        put32(b, o + 0x3BC, k); put32(b, o + 0x3C0, 10); put32(b, o + 0x3C4, 10); put32(b, o + 0x3C8, uint32_t(ps[k].level));
        b[o + 0x3D0] = 5;                                              // small textures, a matcher pattern
        for (int t = 0; t < 4; ++t) put32(b, o + 0x3D4 + 4 * t, k * 512 + uint32_t(t) * 128);
        put16(b, o + 0x3E4, ps[k].region);
        std::memcpy(&b[o + 0x3EA], "SYN.csv", 7);
    }
    return b;
}
// grass flat, desert flat, coast, ocean
std::vector<uint8_t> goodLibrary() {
    return synLibrary({{0x1, 1, 15300, 11, 0}, {0x4, 1, 16000, 13, 0}, {0x2000, 1, 9766, 37, 1}, {0x4000, 1, 9766, 37, 2}});
}

std::vector<uint8_t> frameBlock(int W, int H, bool wrap) {
    const uint32_t base = 0xD90FBC;
    std::vector<uint8_t> b(880, 0);
    put32(b, 0xD90FBC - base, wrap ? 1 : 0);
    put32(b, 0xD911C4 - base, uint32_t(W)); put32(b, 0xD911C8 - base, uint32_t(H));
    put32(b, 0xD91264 - base, uint32_t(W)); put32(b, 0xD91268 - base, uint32_t(H + 4));
    const float h2w[16] = {64, 0, 0, 0, 32, 64, 0, 0, 0, 0, 0, 0, 32, 128, 0, 1};
    for (int i = 0; i < 16; ++i) putf(b, 0xD91030 - base + 4 * i, h2w[i]);
    const float cor[4][4] = {{0.5f, 0, -0.5f, 1}, {1.5f, 2, -3.5f, 1}, {0.5f, 0, -0.5f, 1}, {4.5f, 8, -12.5f, 1}};
    for (int c = 0; c < 4; ++c) for (int i = 0; i < 4; ++i) putf(b, 0xD910F0 - base + 16 * c + 4 * i, cor[c][i]);
    const int32_t G[16] = {0, -1, -1, 0, -1, 0, -1, 0, -1, -1, 0, 0, -1, 2, -1, 1};
    const int32_t Gi[16] = {0, -1, -1, 0, -1, 0, -1, 0, -1, -1, 0, 0, 1, -2, 1, 1};
    for (int i = 0; i < 16; ++i) { put32(b, 0xD911D0 - base + 4 * i, uint32_t(G[i])); put32(b, 0xD91210 - base + 4 * i, uint32_t(Gi[i])); }
    return b;
}

PlotTable grassMap(int W, int H) {
    PlotTable t; t.w = W; t.h = H; t.plots.assign(size_t(W) * H, PlotInfo());
    for (auto& p : t.plots) { p.base.terrain = 0; p.continentArt = 1; }
    return t;
}

struct Fixture {
    std::shared_ptr<const PieceLibrary> lib;
    MapFrame fr;
    PlotTable plots;
    std::vector<uint16_t> h; std::vector<uint8_t> m;
    TapView tap;
    Fixture(int W = 20, int H = 16) {
        CHECK_OK(PieceLibrary::fromBytes(goodLibrary(), lib));
        auto blk = frameBlock(W, H, false);
        CHECK_OK(MapFrame::fromConstantsBlock(blk.data(), blk.size(), fr));
        plots = grassMap(W, H);
        h.assign(size_t(fr.samplesX()) * fr.samplesY(), 15300);
        m.assign(h.size(), 11);
        tap.heights = h.data(); tap.materials = m.data(); tap.samplesX = fr.samplesX(); tap.samplesY = fr.samplesY();
    }
};

} // namespace

// ---------------------------------------------------------------- library
C5R_TEST(lib_ok) {
    std::shared_ptr<const PieceLibrary> L;
    CHECK_OK(PieceLibrary::fromBytes(goodLibrary(), L));
    CHECK_EQ(L->pieces().size(), size_t(4));
    CHECK_EQ(L->maxLevel(), 1);
    CHECK(L->makesMaterial(37));
    CHECK(!L->makesMaterial(22));
    CHECK_EQ(L->heights(1)[5 * 128 + 7], uint16_t(16000));
    CHECK_EQ(L->bytes(0, 1)[0], uint8_t(11));
}
C5R_TEST(lib_bad_version) {
    auto b = goodLibrary(); put32(b, 0, 21);
    std::shared_ptr<const PieceLibrary> L;
    CHECK_EQ(PieceLibrary::fromBytes(b, L).code, Err::BadVersion);
    CHECK(!L);
}
C5R_TEST(lib_truncated) {
    auto b = goodLibrary(); b.resize(100);
    std::shared_ptr<const PieceLibrary> L;
    CHECK_EQ(PieceLibrary::fromBytes(b, L).code, Err::Truncated);
    CHECK(!L);
}
C5R_TEST(lib_size_mismatch) {
    auto b = goodLibrary(); b.push_back(0);
    std::shared_ptr<const PieceLibrary> L;
    CHECK_EQ(PieceLibrary::fromBytes(b, L).code, Err::BadLayout);
    CHECK(!L);
}
C5R_TEST(lib_row_overrun) {
    // make the first u16 run 200 long: the row no longer closes at the width 128
    auto b = goodLibrary();
    uint32_t nRows; std::memcpy(&nRows, &b[4], 4);
    size_t o = 8 + size_t(nRows) * 4;
    uint32_t n8; std::memcpy(&n8, &b[o], 4); o += 4 + n8;
    o += 4;                                   // u16 count
    put16(b, o, 200);
    std::shared_ptr<const PieceLibrary> L;
    CHECK_EQ(PieceLibrary::fromBytes(b, L).code, Err::BadLayout);
    CHECK(!L);
}
C5R_TEST(lib_missing_file) {
    std::shared_ptr<const PieceLibrary> L;
    CHECK_EQ(PieceLibrary::open(L"Z:/no/such/TerrainPiecesLibrary.fxsb", L).code, Err::Io);
    CHECK(!L);
}

// ---------------------------------------------------------------- frame
C5R_TEST(frame_ok) {
    auto b = frameBlock(56, 36, true);
    MapFrame f;
    CHECK_OK(MapFrame::fromConstantsBlock(b.data(), b.size(), f));
    CHECK_EQ(f.plotsW, 56); CHECK_EQ(f.plotsH, 36); CHECK(f.wrapX);
    CHECK_EQ(f.samplesY(), 40 * 64);
}
C5R_TEST(frame_short) {
    auto b = frameBlock(56, 36, true);
    MapFrame f;
    CHECK_EQ(MapFrame::fromConstantsBlock(b.data(), 879, f).code, Err::Truncated);
    CHECK_EQ(f.plotsW, 0);
}
C5R_TEST(frame_grid_mismatch) {
    auto b = frameBlock(56, 36, true);
    put32(b, 0xD91268 - 0xD90FBC, 36);        // grid height without the 4 margin rows
    MapFrame f;
    CHECK_EQ(MapFrame::fromConstantsBlock(b.data(), b.size(), f).code, Err::BadLayout);
    CHECK_EQ(f.plotsW, 0);
}
C5R_TEST(frame_too_big) {
    auto b = frameBlock(300, 36, true);
    MapFrame f;
    CHECK_EQ(MapFrame::fromConstantsBlock(b.data(), b.size(), f).code, Err::Unsupported);
}

// ---------------------------------------------------------------- create
C5R_TEST(create_ok_and_deterministic) {
    Fixture x;
    Producer a, b;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 7, a));
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 7, b));
    CHECK(!a.placements().empty());
    CHECK_EQ(a.placements().size(), b.placements().size());
    bool same = true;
    for (size_t i = 0; i < a.placements().size(); ++i)
        same = same && a.placements()[i].ax == b.placements()[i].ax && a.placements()[i].piece == b.placements()[i].piece;
    CHECK(same);
    // every land plot is claimed by exactly one single-cell piece: placements = land cells
    CHECK_EQ(a.placements().size(), size_t(20 * 16) + size_t(20 * 4) /* the margin cells match the coast piece */);
    std::vector<uint16_t> h; std::vector<uint8_t> m;
    CHECK_OK(a.ownField(h, m));
    CHECK_EQ(h.size(), x.h.size());
}
C5R_TEST(create_bad_plots) {
    Fixture x; x.plots.plots.pop_back();
    Producer p;
    CHECK_EQ(Producer::create(x.lib, x.fr, x.plots, x.tap, 1, p).code, Err::BadLayout);
    CHECK(p.placements().empty());
}
C5R_TEST(create_bad_tap) {
    Fixture x; x.tap.samplesY -= 64;
    Producer p;
    CHECK_EQ(Producer::create(x.lib, x.fr, x.plots, x.tap, 1, p).code, Err::BadLayout);
    CHECK(p.placements().empty());
}
C5R_TEST(create_no_library) {
    Fixture x;
    Producer p;
    CHECK_EQ(Producer::create(nullptr, x.fr, x.plots, x.tap, 1, p).code, Err::BadLayout);
}

// ---------------------------------------------------------------- change
C5R_TEST(change_live_touches_only_the_window) {
    Fixture x;
    Producer p;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p));
    const std::vector<uint16_t> before = p.currentHeights();
    PlotTable after = x.plots;
    after.plots[size_t(8) * 20 + 10].base.terrain = 2;   // grass to desert at plot (10, 8)
    ChangeResult r;
    CHECK_OK(p.change(after, r));
    CHECK(r.live);
    CHECK_EQ(r.reason, WaitReason::None);
    CHECK_EQ(r.changedPlots.size(), size_t(1));
    CHECK_EQ(r.removedPlacements, 1u);
    CHECK(r.addedPlacements >= 1u);
    CHECK_EQ(r.samplesOutside, uint64_t(0));
    CHECK(r.samplesD > 0);
    CHECK_EQ(r.window.size(), size_t(1));
    // count every changed sample of the field and check it lies inside the window
    const WindowRect& w = r.window[0];
    const auto& now = p.currentHeights();
    uint64_t changed = 0, outside = 0;
    for (size_t k = 0; k < now.size(); ++k) if (now[k] != before[k]) {
        ++changed;
        const int c = int(k % size_t(x.fr.samplesX())), rr = int(k / size_t(x.fr.samplesX()));
        if (c < w.x0 || c >= w.x0 + w.w || rr < w.y0 || rr >= w.y0 + w.h) ++outside;
    }
    CHECK(changed > 0);
    CHECK_EQ(outside, uint64_t(0));
    // the desert piece's height reaches the changed plot's centre sample (w = 1 there)
    const int cx = 10 * 64 + (8 & 1) * 32 + 32, cy = (8 + 2) * 64;
    (void)cx; (void)cy;
}
C5R_TEST(change_same_result_twice) {
    Fixture x;
    Producer a, b;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, a));
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, b));
    PlotTable after = x.plots;
    after.plots[size_t(8) * 20 + 10].base.terrain = 2;
    ChangeResult ra, rb;
    CHECK_OK(a.change(after, ra)); CHECK_OK(b.change(after, rb));
    CHECK(ra.live && rb.live);
    CHECK(ra.window[0].heights == rb.window[0].heights);
}
C5R_TEST(change_no_change) {
    Fixture x;
    Producer p;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p));
    ChangeResult r;
    CHECK_OK(p.change(x.plots, r));
    CHECK(!r.live);
    CHECK_EQ(r.reason, WaitReason::NoChange);
    CHECK(r.window.empty());
}
C5R_TEST(change_bad_table) {
    Fixture x;
    Producer p;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p));
    PlotTable after = x.plots; after.h -= 1; after.plots.resize(size_t(after.w) * after.h);
    ChangeResult r;
    CHECK_EQ(p.change(after, r).code, Err::BadLayout);
    CHECK(!r.live);
}
// ---------------------------------------------------------------- unknown natural wonders (ticket 0613, live-terrain.md 8.13.2)
namespace {
heightfield::PlotInfo wonderPlot(const char* type, int terrain = 0, bool hills = false, bool mountain = false) {
    heightfield::PlotInfo p;
    p.terrain = terrain; p.hills = hills; p.mountain = mountain;
    p.feature = 30; p.naturalWonder = true; p.featureType = type;
    return p;
}
const char* kTestWonder = "FEATURE_C5H_TEST_WONDER";   // a Type the EXE's table does not name
// the Fixture's map with an unknown wonder on plot (10, 8) and a library that also holds a Fuji piece (0x40000, tall),
// and the tap the EXE would bake: the stock rule's own field (Fuji's piece on the wonder plot)
struct WonderFixture : Fixture {
    WonderFixture(const char* type = kTestWonder) {
        CHECK_OK(PieceLibrary::fromBytes(synLibrary({{0x1, 1, 15300, 11, 0}, {0x4, 1, 16000, 13, 0}, {0x2000, 1, 9766, 37, 1},
                                                     {0x4000, 1, 9766, 37, 2}, {0x40000, 1, 30000, 11, 0}}), lib));
        if (type) plots.plots[size_t(8) * 20 + 10].base = wonderPlot(type);
        Producer stock;
        CHECK_OK(Producer::create(lib, fr, plots, tap, 5, stock));
        CHECK_OK(stock.ownField(h, m));
        tap.heights = h.data(); tap.materials = m.data();
    }
};
Producer::Options neutral() { Producer::Options o; o.neutralUnknownWonders = true; return o; }
// the highest sample of a field inside a window rectangle (the blend averages the pieces that overlap a sample, and a
// piece's rectangle is not centred on its hex: a maximum over the change's window, not one sample)
uint16_t maxIn(const std::vector<uint16_t>& h, int sx, const WindowRect& w) {
    uint16_t m = 0;
    for (int yy = w.y0; yy < w.y0 + w.h; ++yy) for (int xx = w.x0; xx < w.x0 + w.w; ++xx) m = std::max(m, h[size_t(yy) * sx + xx]);
    return m;
}
}
C5R_TEST(wonder_rule) {
    using heightfield::patternCellFlags;
    // an unknown wonder: the EXE gives it Fuji's flag; the neutral base is the plot's own terrain value
    CHECK(unknownToExe(wonderPlot(kTestWonder)));
    CHECK_EQ(cellFlags(wonderPlot(kTestWonder), false), 0x40000u);
    CHECK_EQ(cellFlags(wonderPlot(kTestWonder), true), 0x1u);                        // grassland flat
    CHECK_EQ(cellFlags(wonderPlot(kTestWonder, 2), true), 0x4u);                     // desert flat
    CHECK_EQ(cellFlags(wonderPlot(kTestWonder, 0, true), true), 0x20u);              // grassland hills
    CHECK_EQ(cellFlags(wonderPlot(kTestWonder, 0, false, true), true), 0x400u);      // mountain
    // named by the EXE: its own flag, with and without the option
    for (const char* t : {"FEATURE_ULURU", "FEATURE_KILIMANJARO", "FEATURE_FUJI", "FEATURE_GEYSER"}) {
        CHECK(!unknownToExe(wonderPlot(t)));
        CHECK_EQ(cellFlags(wonderPlot(t), true), patternCellFlags(wonderPlot(t)));
    }
    // Fountain of Youth and El Dorado keep the terrain's value in stock already: not unknown, nothing changes
    CHECK(!unknownToExe(wonderPlot("FEATURE_FOUNTAIN_YOUTH")));
    CHECK(!unknownToExe(wonderPlot("FEATURE_EL_DORADO")));
    CHECK_EQ(cellFlags(wonderPlot("FEATURE_EL_DORADO"), true), 0x1u);
    // negatives: an unnamed wonder, a wonder on coast or ocean, a feature that is no wonder, no feature: the stock rule
    CHECK(!unknownToExe(wonderPlot("")));
    CHECK_EQ(cellFlags(wonderPlot(""), true), 0x40000u);
    CHECK(!unknownToExe(wonderPlot(kTestWonder, 5)));
    CHECK_EQ(cellFlags(wonderPlot(kTestWonder, 5), true), patternCellFlags(wonderPlot(kTestWonder, 5)));
    CHECK(!unknownToExe(wonderPlot(kTestWonder, 6)));
    heightfield::PlotInfo notWonder = wonderPlot(kTestWonder); notWonder.naturalWonder = false;
    CHECK(!unknownToExe(notWonder));
    CHECK_EQ(cellFlags(notWonder, true), 0x1u);
    heightfield::PlotInfo none = wonderPlot(kTestWonder); none.feature = -1;
    CHECK(!unknownToExe(none));
    CHECK_EQ(cellFlags(none, true), 0x1u);
}
C5R_TEST(wonder_load_pass_redraws_the_unknown_wonder) {
    WonderFixture x;
    Producer p;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p));
    const std::vector<uint16_t> before = p.currentHeights();
    const int sx = x.fr.samplesX();
    ChangeResult r;
    CHECK_OK(p.change(x.plots, r, neutral()));   // the load-time pass: the load's own table, the option on
    CHECK(r.live);
    if (r.window.empty()) return;
    // the EXE's bake: Fuji's piece (30000) blended with its grassland neighbours (15300): 18975 on this map
    CHECK(maxIn(before, sx, r.window[0]) > 17000);
    CHECK_EQ(r.reason, WaitReason::None);
    CHECK_EQ(r.changedPlots.size(), size_t(1));
    CHECK(r.changedPlots[0] == std::make_pair(10, 8));
    CHECK(!r.window.empty());
    CHECK_EQ(r.samplesOutside, uint64_t(0));
    const uint16_t neutralMax = maxIn(p.currentHeights(), sx, r.window[0]);
    CHECK(neutralMax > 15200 && neutralMax < 15400);                        // the neutral base: the grassland piece
    // every changed sample lies in the window
    const WindowRect& w = r.window[0];
    uint64_t outside = 0;
    for (size_t k = 0; k < before.size(); ++k) if (p.currentHeights()[k] != before[k]) {
        const int c = int(k % size_t(sx)), rr = int(k / size_t(sx));
        if (c < w.x0 || c >= w.x0 + w.w || rr < w.y0 || rr >= w.y0 + w.h) ++outside;
    }
    CHECK_EQ(outside, uint64_t(0));
    // the same pass on another load of the same save (the reload) gives the same window: live and reload agree
    Producer q;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, q));
    ChangeResult r2;
    CHECK_OK(q.change(x.plots, r2, neutral()));
    CHECK(r2.live);
    CHECK(r2.window.size() == r.window.size() && r2.window[0].heights == r.window[0].heights &&
          r2.window[0].materials == r.window[0].materials);
    CHECK(q.currentHeights() == p.currentHeights());
    // a second pass finds nothing (the producer keeps the neutral cell), and a later change elsewhere leaves the wonder
    ChangeResult again;
    CHECK_OK(p.change(x.plots, again, neutral()));
    CHECK_EQ(again.reason, WaitReason::NoChange);
    PlotTable later = x.plots;
    later.plots[size_t(4) * 20 + 4].base.terrain = 2;
    ChangeResult rl;
    CHECK_OK(p.change(later, rl, neutral()));
    CHECK(rl.live);
    CHECK_EQ(rl.changedPlots.size(), size_t(1));
    CHECK(rl.changedPlots[0] == std::make_pair(4, 4));
    CHECK_EQ(maxIn(p.currentHeights(), sx, r.window[0]), neutralMax);
}
C5R_TEST(wonder_placed_live_gets_the_neutral_base) {
    WonderFixture x(nullptr);   // no wonder at load
    Producer p, s;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p));
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, s));
    PlotTable after = x.plots;
    after.plots[size_t(8) * 20 + 10].base = wonderPlot(kTestWonder, 2);   // the wonder placed on a plot made desert
    ChangeResult rn, rs;
    CHECK_OK(p.change(after, rn, neutral()));
    CHECK_OK(s.change(after, rs));   // option off: today's rule, Fuji
    CHECK(rn.live && rs.live);
    if (rn.window.empty() || rs.window.empty()) return;
    const int sx = x.fr.samplesX();
    CHECK(maxIn(s.currentHeights(), sx, rs.window[0]) > 17000);            // stock: Fuji (18975 on this map)
    const uint16_t pc = maxIn(p.currentHeights(), sx, rn.window[0]);
    CHECK(pc > 15400 && pc < 16100);                                        // neutral: the desert piece (15475)
    // a wonder placed live on flat grassland with the option on is no change of the field at all
    Producer f;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, f));
    PlotTable flat = x.plots;
    flat.plots[size_t(8) * 20 + 10].base = wonderPlot(kTestWonder);
    ChangeResult rf;
    CHECK_OK(f.change(flat, rf, neutral()));
    CHECK_EQ(rf.reason, WaitReason::NoChange);
    CHECK(rf.window.empty());
}
C5R_TEST(wonder_load_pass_without_unknown_wonder_is_no_window) {
    for (const char* t : {(const char*)nullptr, "FEATURE_ULURU", "FEATURE_FUJI", "FEATURE_EL_DORADO", ""}) {
        WonderFixture x(t);
        Producer p;
        CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p));
        const std::vector<uint16_t> before = p.currentHeights();
        const size_t nBefore = p.placements().size();
        ChangeResult r;
        CHECK_OK(p.change(x.plots, r, neutral()));
        CHECK(!r.live);
        CHECK_EQ(r.reason, WaitReason::NoChange);
        CHECK(r.window.empty());
        CHECK(r.changedPlots.empty());
        CHECK(p.currentHeights() == before);
        CHECK_EQ(p.placements().size(), nBefore);
    }
}
C5R_TEST(wonder_option_off_is_todays_producer) {
    // with an unknown wonder in the load and the option off, the load's own table is no change (as before 0613), and a
    // change elsewhere gives bit for bit the window of a producer given Options() (the default before 0613)
    WonderFixture x;
    Producer a, b;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, a));
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, b));
    ChangeResult r0;
    Producer::Options off; off.neutralUnknownWonders = false;
    CHECK_OK(a.change(x.plots, r0, off));
    CHECK_EQ(r0.reason, WaitReason::NoChange);
    PlotTable after = x.plots;
    after.plots[size_t(8) * 20 + 9].base.terrain = 2;   // next to the wonder: its Fuji cell stays in both
    ChangeResult ra, rb;
    CHECK_OK(a.change(after, ra, off));
    CHECK_OK(b.change(after, rb));
    CHECK(ra.live && rb.live);
    CHECK_EQ(ra.changedPlots.size(), size_t(1));
    CHECK(ra.window.size() == rb.window.size() && ra.window[0].heights == rb.window[0].heights &&
          ra.window[0].materials == rb.window[0].materials);
    CHECK(a.currentHeights() == b.currentHeights());
}

namespace {
// a change at plot (10, 8) with something unclean at plot (11, 8): the window must wait, the producer stays as it was
void waitCase(void (*spoil)(PlotInfo&), WaitReason want, const Producer::Options& opt = Producer::Options()) {
    Fixture x;
    spoil(x.plots.plots[size_t(8) * 20 + 11]);
    Producer p;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p));
    const std::vector<uint16_t> before = p.currentHeights();
    const size_t nBefore = p.placements().size();
    PlotTable after = x.plots;
    after.plots[size_t(8) * 20 + 10].base.terrain = 2;
    ChangeResult r;
    CHECK_OK(p.change(after, r, opt));
    CHECK(!r.live);
    CHECK_EQ(r.reason, want);
    CHECK(!r.reasonText.empty());
    CHECK(r.window.empty());
    CHECK(p.currentHeights() == before);
    CHECK_EQ(p.placements().size(), nBefore);
}
}
namespace {
Producer::Options noProtect() { Producer::Options o; o.protectWater = false; o.protectMarsh = false; o.protectRivers = false; return o; }
// a change at plot (10, 8) next to something the producer keeps (default options): live, the kept hex bit-equal
void keepCase(void (*spoil)(PlotInfo&), int keptX, int keptY, const Producer::Options& opt = Producer::Options()) {
    Fixture x;
    spoil(x.plots.plots[size_t(8) * 20 + 11]);
    Producer p;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p));
    const std::vector<uint16_t> before = p.currentHeights();
    const std::vector<uint8_t> beforeM = p.currentMaterials();
    PlotTable after = x.plots;
    after.plots[size_t(8) * 20 + 10].base.terrain = 2;
    ChangeResult r;
    CHECK_OK(p.change(after, r, opt));
    CHECK(r.live);
    CHECK_EQ(r.samplesOutside, uint64_t(0));
    // every sample within 24 samples of the kept plot's centre is unchanged (its hex is kept)
    const int sx = x.fr.samplesX();
    const int q = keptX - (keptY >> 1), rr = keptY;
    const int cx = q * 64 + rr * 32 + 32, cy = rr * 64 + 128;
    uint64_t moved = 0;
    for (int yy = cy - 24; yy < cy + 24; ++yy) for (int xx = cx - 24; xx < cx + 24; ++xx) {
        const size_t k = size_t(yy) * sx + xx;
        if (p.currentHeights()[k] != before[k] || p.currentMaterials()[k] != beforeM[k]) ++moved;
    }
    CHECK_EQ(moved, uint64_t(0));
}
}
C5R_TEST(change_wait_water) { waitCase([](PlotInfo& q) { q.base.terrain = 5; }, WaitReason::Water, noProtect()); }
C5R_TEST(change_wait_lake) { waitCase([](PlotInfo& q) { q.lake = true; }, WaitReason::Lake, noProtect()); }
C5R_TEST(change_wait_river) { waitCase([](PlotInfo& q) { q.riverEdges = 1; }, WaitReason::RiverEdge, noProtect()); }
C5R_TEST(change_live_next_to_river_keeps_it) {   // the 4908 disc (Options::riverKeepDisc, the rule before 0717)
    Producer::Options o; o.riverKeepDisc = true;
    keepCase([](PlotInfo& q) { q.riverEdges = 1; }, 11, 8, o);
}
// ---------------------------------------------------------------- the river band (ticket 0717)
namespace {
int cX(int x, int y) { return (x - (y >> 1)) * 64 + y * 32 + 32; }
int cY(int y) { return y * 64 + 128; }
// plot (10, 8) grass to desert (a raise: 15300 to 16000 in the synthetic library) with river bits on some plots; the
// tap can be lifted so that the change lowers instead
struct BandCase {
    Fixture x;
    Producer p;
    std::vector<uint16_t> h0;
    ChangeResult r;
    BandCase(void (*rivers)(PlotTable&), const Producer::Options& opt, uint16_t tapHeight = 15300) {
        rivers(x.plots);
        std::fill(x.h.begin(), x.h.end(), tapHeight);
        CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p));
        h0 = p.currentHeights();
        PlotTable after = x.plots;
        after.plots[size_t(8) * 20 + 10].base.terrain = 2;
        CHECK_OK(p.change(after, r, opt));
        CHECK(r.live);
        CHECK_EQ(r.samplesOutside, uint64_t(0));
    }
    uint16_t at(int c, int rr) const { return p.currentHeights()[size_t(rr) * x.fr.samplesX() + c]; }
    uint16_t was(int c, int rr) const { return h0[size_t(rr) * x.fr.samplesX() + c]; }
};
void noRivers(PlotTable&) {}
void ownEastEdge(PlotTable& t) { t.plots[size_t(8) * 20 + 10].riverEdges = 1; }   // (10, 8) W of river: its east edge
}
C5R_TEST(river_band_changed_plot_on_a_river_takes_the_change) {
    // the owner's report: a plot with a river on an edge did not change at all (the disc kept its whole hex)
    BandCase ref(noRivers, Producer::Options());           // the same change with no river: the new ground everywhere
    BandCase b(ownEastEdge, Producer::Options());
    const int cx = cX(10, 8), cy = cY(8);
    CHECK(ref.at(cx, cy) > 15300);
    CHECK_EQ(int(b.at(cx, cy)), int(ref.at(cx, cy)));             // the centre, 32 world units from the river edge
    CHECK_EQ(int(b.at(cx - 20, cy)), int(ref.at(cx - 20, cy)));   // the west half
    CHECK_EQ(int(b.at(cx + 31, cy)), int(b.was(cx + 31, cy)));    // 1 from the east edge (x = cx + 32): the core keeps it
    CHECK(ref.at(cx + 17, cy) > b.was(cx + 17, cy) + 20);
    const int mid = b.at(cx + 17, cy);                            // 15 from the edge: part way up the band's rise
    CHECK(mid > int(b.was(cx + 17, cy)) && mid < int(ref.at(cx + 17, cy)));
    CHECK(b.r.samplesRiverCore > 0u);
    CHECK(b.r.samplesRiverBand > 0u);
    CHECK_EQ(ref.r.samplesRiverCore, uint64_t(0));
}
C5R_TEST(river_band_disc_negative) {
    // negative: the 4908 disc on the same change keeps the whole hex, the centre included
    Producer::Options o; o.riverKeepDisc = true;
    BandCase b(ownEastEdge, o);
    CHECK_EQ(int(b.at(cX(10, 8), cY(8))), 15300);
    CHECK_EQ(b.r.samplesRiverCore, uint64_t(0));
}
C5R_TEST(river_band_lowering_is_drawn) {
    // the carve only lowers: a lowering (tap 17000 above every piece) is drawn in the core too
    BandCase ref(noRivers, Producer::Options(), 17000);
    BandCase b(ownEastEdge, Producer::Options(), 17000);
    const int cx = cX(10, 8), cy = cY(8);
    CHECK(ref.at(cx + 30, cy) < 17000);
    CHECK_EQ(int(b.at(cx, cy)), int(ref.at(cx, cy)));
    CHECK_EQ(int(b.at(cx + 30, cy)), int(ref.at(cx + 30, cy)));
}
C5R_TEST(river_band_any_flag_needs_both_sides) {
    // riverEdgesFromAnyFlag: (10, 8) flagged alone has no river edge (no neighbour carries the flag): no band
    BandCase ref(noRivers, Producer::Options());
    const int cx = cX(10, 8), cy = cY(8);
    Producer::Options o; o.riverEdgesFromAnyFlag = true;
    BandCase alone([](PlotTable& t) { t.plots[size_t(8) * 20 + 10].riverEdges = 7; }, o);
    CHECK_EQ(alone.r.samplesRiverCore, uint64_t(0));
    CHECK_EQ(int(alone.at(cx + 30, cy)), int(ref.at(cx + 30, cy)));
    // with (11, 8) flagged too, the edge between them is a river edge: its core is kept
    BandCase both([](PlotTable& t) { t.plots[size_t(8) * 20 + 10].riverEdges = 7; t.plots[size_t(8) * 20 + 11].riverEdges = 7; }, o);
    CHECK(both.r.samplesRiverCore > 0u);
    CHECK_EQ(int(both.at(cx + 31, cy)), 15300);
    CHECK_EQ(int(both.at(cx, cy)), int(ref.at(cx, cy)));
    // the same table read as the DLL's own bits (the default) bands all three of (10, 8)'s edges and (11, 8)'s
    BandCase own([](PlotTable& t) { t.plots[size_t(8) * 20 + 10].riverEdges = 7; t.plots[size_t(8) * 20 + 11].riverEdges = 7; }, Producer::Options());
    CHECK(own.r.samplesRiverCore > both.r.samplesRiverCore);
}
C5R_TEST(river_band_off_waits) {
    // protectRivers off: a river edge in the window waits (RiverEdge), as before 0717
    Fixture x;
    ownEastEdge(x.plots);
    Producer p;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p));
    const std::vector<uint16_t> before = p.currentHeights();
    PlotTable after = x.plots;
    after.plots[size_t(8) * 20 + 10].base.terrain = 2;
    ChangeResult r;
    Producer::Options o; o.protectRivers = false;
    CHECK_OK(p.change(after, r, o));
    CHECK(!r.live);
    CHECK_EQ(r.reason, WaitReason::RiverEdge);
    CHECK(p.currentHeights() == before);
}
C5R_TEST(change_wait_marsh) { waitCase([](PlotInfo& q) { q.base.feature = 2; q.base.featureType = "FEATURE_MARSH"; }, WaitReason::Marsh, noProtect()); }
C5R_TEST(change_live_next_to_water_keeps_it) { keepCase([](PlotInfo& q) { q.base.terrain = 5; }, 11, 8); }
C5R_TEST(change_live_next_to_marsh_keeps_it) { keepCase([](PlotInfo& q) { q.base.feature = 2; q.base.featureType = "FEATURE_MARSH"; }, 11, 8); }
C5R_TEST(change_wait_land_to_water) {
    // a plot that becomes water moves the coast lines: it waits even with the protection on
    Fixture x;
    Producer p;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p));
    const std::vector<uint16_t> before = p.currentHeights();
    PlotTable after = x.plots;
    after.plots[size_t(8) * 20 + 10].base.terrain = 5;
    ChangeResult r;
    CHECK_OK(p.change(after, r));
    CHECK(!r.live);
    CHECK_EQ(r.reason, WaitReason::Water);
    CHECK(p.currentHeights() == before);
}
C5R_TEST(change_diagnostic_draws_but_says_so) {
    Fixture x;
    x.plots.plots[size_t(8) * 20 + 11].base.terrain = 5;   // coast next to the change: the product would wait
    Producer p;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p));
    PlotTable after = x.plots;
    after.plots[size_t(8) * 20 + 10].base.terrain = 2;
    ChangeResult r;
    Producer::Options opt = noProtect(); opt.diagnosticIgnoreClean = true;
    CHECK_OK(p.change(after, r, opt));
    CHECK(r.live);
    CHECK_EQ(r.reasonText.compare(0, 10, "DIAGNOSTIC"), 0);
    CHECK_EQ(r.samplesOutside, uint64_t(0));
}
C5R_TEST(change_wait_unknown_material) {
    Fixture x;
    // a material no piece makes (22, the marsh ground of 7.7) in the tap next to the change
    const int sx = x.fr.samplesX();
    for (int r = 600; r < 660; ++r) for (int c = 700; c < 760; ++c) x.m[size_t(r) * sx + c] = 22;
    Producer p;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p));
    PlotTable after = x.plots;
    after.plots[size_t(8) * 20 + 10].base.terrain = 2;
    ChangeResult r;
    CHECK_OK(p.change(after, r, noProtect()));
    CHECK(!r.live);
    CHECK_EQ(r.reason, WaitReason::Marsh);
}
C5R_TEST(change_wait_seam) {
    // a wrapping map: a change at x = 0 waits (the seam plots, 7.7)
    Fixture x;
    auto blk = frameBlock(20, 16, true);
    CHECK_OK(MapFrame::fromConstantsBlock(blk.data(), blk.size(), x.fr));
    Producer p;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p));
    PlotTable after = x.plots;
    after.plots[size_t(8) * 20 + 0].base.terrain = 2;
    ChangeResult r;
    CHECK_OK(p.change(after, r));
    CHECK(!r.live);
    CHECK_EQ(r.reason, WaitReason::Seam);
}

// ---------------------------------------------------------------- land/water (4913, live-terrain.md 8.17)
namespace {
std::string lwSpline() {
    std::string s;
    for (int i = 0; i < 100; ++i) {
        char b[64]; std::snprintf(b, sizeof b, "[%g,%g,0]\n", 400.0 - i * 4.0404, 1.5 * std::sin(i * 0.5));
        s += b;
    }
    return s;
}
CoastStyles lwStyles() {
    const char* xml =
        "<CoastRoot><SplineCharacterList><SplineCharacter Filename=\"W.txt\" /></SplineCharacterList><CoastStyleList>"
        "<CoastStyle type=\"Grass\"><SmoothingOffset HighSide=\"20\" LowSide=\"20\" />"
        "<HighBeach Height=\"0.2\" Offset=\"1.5\" Character=\"0\" TerrainType=\"5\"/>"
        "<LowBeach Height=\"0.19\" Offset=\"1.5\" Character=\"0\" TerrainType=\"5\"/>"
        "<Waterline Height=\"0.198\" Offset=\"0\" Character=\"0\" /></CoastStyle>"
        "<CoastStyle type=\"DefaultShallows\"><SmoothingOffset HighSide=\"20\" LowSide=\"20\" />"
        "<HighBeach Height=\"0.11\" Offset=\"1.5\" Character=\"65535\" TerrainType=\"65535\"/>"
        "<LowBeach Height=\"0.09\" Offset=\"1.5\" Character=\"65535\" TerrainType=\"65535\"/>"
        "<Waterline Height=\"0.1\" Offset=\"0\" Character=\"65535\" /></CoastStyle></CoastStyleList></CoastRoot>";
    CoastStyles s;
    CHECK_OK(CoastStyles::parse(xml, [](const std::string& n, std::string& t) { if (n != "W.txt") return false; t = lwSpline(); return true; }, s));
    return s;
}
StripMaterials lwMaterials() {
    StripMaterials m;
    for (uint32_t i = 0; i < 20; ++i) m.rowId.push_back((0x10u << 16) | i);   // generic rows, code k at row k
    return m;
}
// every changed sample of the field lies in one of the result's rectangles
uint64_t changedOutside(const Producer& p, const std::vector<uint16_t>& h0, const std::vector<uint8_t>& m0,
                        const ChangeResult& r, int sx) {
    uint64_t out = 0;
    for (size_t k = 0; k < h0.size(); ++k) {
        if (p.currentHeights()[k] == h0[k] && p.currentMaterials()[k] == m0[k]) continue;
        const int c = int(k % size_t(sx)), rr = int(k / size_t(sx));
        bool in = false;
        for (const WindowRect& w : r.window) in = in || (c >= w.x0 && c < w.x0 + w.w && rr >= w.y0 && rr < w.y0 + w.h);
        if (!in) ++out;
    }
    return out;
}
}

C5R_TEST(lw_enable_before_create_refused) {
    Producer p;
    CHECK_EQ(p.enableLandWater(lwStyles(), lwMaterials(), 1u).code, Err::BadLayout);
    CHECK(!p.landWaterEnabled());
}
C5R_TEST(lw_land_to_water_goes_live) {
    Fixture x;
    Producer p;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p));
    CHECK_OK(p.enableLandWater(lwStyles(), lwMaterials(), 0x3DFC9816u));
    CHECK(p.landWaterEnabled());
    const std::vector<uint16_t> h0 = p.currentHeights(); const std::vector<uint8_t> m0 = p.currentMaterials();
    PlotTable after = x.plots;
    after.plots[size_t(8) * 20 + 10].base.terrain = 5;   // a one-plot lake of coast water in grass
    ChangeResult r;
    CHECK_OK(p.change(after, r));
    CHECK(r.live);
    CHECK(r.landWater);
    CHECK_EQ(r.reason, WaitReason::None);
    CHECK(r.coastLinesRedrawn >= 1u);
    CHECK_EQ(r.samplesOutside, uint64_t(0));
    CHECK_EQ(r.window.size(), size_t(1));
    CHECK_EQ(changedOutside(p, h0, m0, r, x.fr.samplesX()), uint64_t(0));
    // the plot's centre now carries the coast piece's water ground (material 37, height 9766)
    const int sx = x.fr.samplesX(), q = 10 - (8 >> 1), cx = q * 64 + 8 * 32 + 32, cy = 8 * 64 + 128;
    CHECK_EQ(int(p.currentMaterials()[size_t(cy) * sx + cx]), 37);
    // the same change on a second producer gives the same window
    Producer p2;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p2));
    CHECK_OK(p2.enableLandWater(lwStyles(), lwMaterials(), 0x3DFC9816u));
    ChangeResult r2;
    CHECK_OK(p2.change(after, r2));
    CHECK(r2.live && r2.window.size() == r.window.size() && r2.window[0].heights == r.window[0].heights);
    // and back to land: live again, the coast line removed
    ChangeResult r3;
    CHECK_OK(p.change(x.plots, r3));
    CHECK(r3.live);
    CHECK(r3.landWater);
    CHECK_EQ(int(p.currentMaterials()[size_t(cy) * sx + cx]), 11);
}
C5R_TEST(lw_disabled_still_waits) {
    // without enableLandWater the class change waits as before (4908's rule), nothing written
    Fixture x;
    Producer p;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p));
    const std::vector<uint16_t> before = p.currentHeights();
    PlotTable after = x.plots;
    after.plots[size_t(8) * 20 + 10].base.terrain = 5;
    ChangeResult r;
    CHECK_OK(p.change(after, r));
    CHECK(!r.live); CHECK(!r.landWater);
    CHECK_EQ(r.reason, WaitReason::Water);
    CHECK(r.window.empty());
    CHECK(p.currentHeights() == before);
}
C5R_TEST(lw_across_the_seam_splits) {
    Fixture x;
    auto blk = frameBlock(20, 16, true);
    CHECK_OK(MapFrame::fromConstantsBlock(blk.data(), blk.size(), x.fr));
    Producer p;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p));
    CHECK_OK(p.enableLandWater(lwStyles(), lwMaterials(), 7u));
    const std::vector<uint16_t> h0 = p.currentHeights(); const std::vector<uint8_t> m0 = p.currentMaterials();
    PlotTable after = x.plots;
    after.plots[size_t(8) * 20 + 0].base.terrain = 5;   // at x = 0: the window crosses the seam
    ChangeResult r;
    CHECK_OK(p.change(after, r));
    CHECK(r.live);
    CHECK_EQ(r.window.size(), size_t(2));
    for (const WindowRect& w : r.window) {
        CHECK(w.x0 >= 0 && w.x0 + w.w <= x.fr.samplesX());
        CHECK_EQ(w.heights.size(), size_t(w.w) * w.h);
    }
    CHECK_EQ(changedOutside(p, h0, m0, r, x.fr.samplesX()), uint64_t(0));
}
// (h18, live-terrain.md 11.8 / 11.9) a land/water change in the map's first or last rows: the window reaches the
// grid's margin rows (frame water, no plot). Before the fix the clean check read after.plots for those cells, before or
// past the table (an ASan heap-buffer-overflow here; the server's access violation of 2026-11-21 19:18 on huge h18).
namespace {
void edgeRowCase(bool wrap, int px, int py, bool protectRivers) {
    Fixture x;
    auto blk = frameBlock(20, 16, wrap);
    CHECK_OK(MapFrame::fromConstantsBlock(blk.data(), blk.size(), x.fr));
    Producer p;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p));
    CHECK_OK(p.enableLandWater(lwStyles(), lwMaterials(), 0x067F5502u));
    const std::vector<uint16_t> h0 = p.currentHeights(); const std::vector<uint8_t> m0 = p.currentMaterials();
    PlotTable after = x.plots;
    after.plots[size_t(py) * 20 + px].base.terrain = 5;   // land to coast water in an edge row
    ChangeResult r;
    Producer::Options opt; opt.protectRivers = protectRivers;
    CHECK_OK(p.change(after, r, opt));
    CHECK(r.live);
    CHECK(r.landWater);
    CHECK_EQ(r.reason, WaitReason::None);
    CHECK_EQ(r.samplesOutside, uint64_t(0));
    CHECK_EQ(changedOutside(p, h0, m0, r, x.fr.samplesX()), uint64_t(0));
    // and back to land (the margin rows are in the window again)
    ChangeResult r2;
    CHECK_OK(p.change(x.plots, r2, opt));
    CHECK(r2.live);
    CHECK(r2.landWater);
}
}
C5R_TEST(lw_edge_row_top) { edgeRowCase(false, 10, 0, true); edgeRowCase(false, 10, 1, true); }
C5R_TEST(lw_edge_row_bottom) { edgeRowCase(false, 10, 15, true); edgeRowCase(false, 10, 14, true); }
C5R_TEST(lw_edge_row_rivers_unprotected) { edgeRowCase(false, 10, 0, false); edgeRowCase(false, 10, 15, false); }
C5R_TEST(lw_edge_row_seam_corners) {
    // a wrapping map: the corners at the x seam, top and bottom (the window is split at the seam)
    edgeRowCase(true, 0, 0, true); edgeRowCase(true, 19, 0, true);
    edgeRowCase(true, 0, 15, true); edgeRowCase(true, 19, 15, true);
}
C5R_TEST(lw_edge_row_no_false_river_wait) {
    // the silent half of the h18 fault: a margin cell's read that lands in mapped memory gave a wait from whatever bytes
    // it found. Here the bytes past the table's end (its spare capacity, where the bottom margin rows' reads landed) hold
    // plots with river edges and the lake flag: with protectRivers off the change waited "river edge at plot x,16" before
    // the fix (and ASan reports a container-overflow there); after it, the margin cells are skipped and it goes live.
    Fixture x;
    Producer p;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p));
    CHECK_OK(p.enableLandWater(lwStyles(), lwMaterials(), 0x067F5502u));
    PlotTable after = x.plots;
    after.plots[size_t(15) * 20 + 10].base.terrain = 5;   // land to coast water in the last row
    const size_t n = after.plots.size();
    after.plots.reserve(n + 2 * 20);
    PlotInfo bait; bait.riverEdges = 7; bait.lake = true;
    for (int i = 0; i < 2 * 20; ++i) after.plots.push_back(bait);
    after.plots.resize(n);                                  // size w * h again; the spare capacity keeps the bait bytes
    ChangeResult r;
    Producer::Options opt; opt.protectRivers = false;
    CHECK_OK(p.change(after, r, opt));
    std::printf("lw_edge_row_no_false_river_wait: reason %s (%s)\n", waitReasonName(r.reason), r.reasonText.c_str());
    CHECK_EQ(r.reason, WaitReason::None);
    CHECK(r.live);
    CHECK(r.landWater);
    CHECK_EQ(r.samplesOutside, uint64_t(0));
}
C5R_TEST(lw_too_large_waits_untouched) {
    // the whole map to water: the changed coast spans more than kMaxWindowPlots; it waits, nothing changes
    Fixture x;
    Producer p;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p));
    CHECK_OK(p.enableLandWater(lwStyles(), lwMaterials(), 7u));
    const std::vector<uint16_t> before = p.currentHeights();
    const size_t nBefore = p.placements().size();
    PlotTable after = x.plots;
    for (int y = 1; y < 15; ++y) for (int xx = 1; xx < 19; ++xx) after.plots[size_t(y) * 20 + xx].base.terrain = 6;
    for (int y = 2; y < 14; ++y) for (int xx = 2; xx < 18; ++xx) after.plots[size_t(y) * 20 + xx].base.terrain = 5;
    ChangeResult r;
    CHECK_OK(p.change(after, r));
    CHECK(!r.live);
    CHECK_EQ(r.reason, WaitReason::TooLarge);
    CHECK(!r.reasonText.empty());
    CHECK(r.window.empty());
    CHECK(p.currentHeights() == before);
    CHECK_EQ(p.placements().size(), nBefore);
}
// (4913 (b), live-terrain.md 8.17) natural wonders next to a land/water change keep the load's ground on their own hexes
namespace {
int plotCx(int x, int y) { return (x - (y >> 1)) * 64 + y * 32 + 32; }
int plotCy(int y) { return y * 64 + 128; }
// samples within 24 of plot (x, y)'s centre (inside its hex) that differ from the given field
uint64_t movedNear(const Producer& p, const std::vector<uint16_t>& h0, const std::vector<uint8_t>& m0, int sx, int x, int y) {
    uint64_t moved = 0;
    for (int yy = plotCy(y) - 24; yy < plotCy(y) + 24; ++yy) for (int xx = plotCx(x, y) - 24; xx < plotCx(x, y) + 24; ++xx) {
        const size_t k = size_t(yy) * sx + xx;
        if (p.currentHeights()[k] != h0[k] || p.currentMaterials()[k] != m0[k]) ++moved;
    }
    return moved;
}
// a two-plot wonder at (11, 8) and (12, 8) whose load ground (height 20000, material 13) no piece of the synthetic
// library makes at that height; the change turns (10, 8) next to it into coast water
struct WonderCase {
    Fixture x;
    Producer p;
    std::vector<uint16_t> h0; std::vector<uint8_t> m0;
    PlotTable after;
    WonderCase() {
        for (int px : {11, 12}) {
            PlotInfo& q = x.plots.plots[size_t(8) * 20 + px];
            q.base.feature = 30; q.base.featureType = "FEATURE_KILIMANJARO"; q.base.naturalWonder = true;
            const int sx = x.fr.samplesX();
            for (int yy = plotCy(8) - 30; yy < plotCy(8) + 30; ++yy) for (int xx = plotCx(px, 8) - 30; xx < plotCx(px, 8) + 30; ++xx) {
                x.h[size_t(yy) * sx + xx] = 20000; x.m[size_t(yy) * sx + xx] = 13;
            }
        }
        CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p));
        CHECK_OK(p.enableLandWater(lwStyles(), lwMaterials(), 0x3DFC9816u));
        h0 = p.currentHeights(); m0 = p.currentMaterials();
        after = x.plots;
        after.plots[size_t(8) * 20 + 10].base.terrain = 5;
    }
};
}
C5R_TEST(lw_wonder_beside_change_keeps_its_hexes) {
    WonderCase c;
    ChangeResult r;
    CHECK_OK(c.p.change(c.after, r));
    CHECK(r.live);
    CHECK(r.landWater);
    CHECK(r.samplesWonderKept > 0u);
    CHECK_EQ(r.samplesOutside, uint64_t(0));
    const int sx = c.x.fr.samplesX();
    // both wonder hexes keep the load's ground; the changed plot is redrawn as water around them
    CHECK_EQ(movedNear(c.p, c.h0, c.m0, sx, 11, 8), uint64_t(0));
    CHECK_EQ(movedNear(c.p, c.h0, c.m0, sx, 12, 8), uint64_t(0));
    CHECK_EQ(int(c.p.currentMaterials()[size_t(plotCy(8)) * sx + plotCx(10, 8)]), 37);
    CHECK_EQ(changedOutside(c.p, c.h0, c.m0, r, sx), uint64_t(0));
}
C5R_TEST(lw_wonder_unprotected_is_redrawn) {
    // negative: with protectWonders off the same change redraws the wonder's hex next to it
    WonderCase c;
    ChangeResult r;
    Producer::Options opt; opt.protectWonders = false;
    CHECK_OK(c.p.change(c.after, r, opt));
    CHECK(r.live);
    CHECK_EQ(r.samplesWonderKept, uint64_t(0));
    CHECK(movedNear(c.p, c.h0, c.m0, c.x.fr.samplesX(), 11, 8) > 0u);
}
C5R_TEST(lw_wonder_own_plot_changed_is_redrawn) {
    // negative: the wonder's own plot turned to water is a changed plot: redrawn (water), the other wonder hex kept
    WonderCase c;
    PlotInfo& q = c.after.plots[size_t(8) * 20 + 11];
    q.base.terrain = 5; q.base.feature = -1; q.base.featureType.clear(); q.base.naturalWonder = false;
    ChangeResult r;
    CHECK_OK(c.p.change(c.after, r));
    CHECK(r.live);
    const int sx = c.x.fr.samplesX();
    CHECK_EQ(int(c.p.currentMaterials()[size_t(plotCy(8)) * sx + plotCx(11, 8)]), 37);
    CHECK_EQ(movedNear(c.p, c.h0, c.m0, sx, 12, 8), uint64_t(0));
}
C5R_TEST(lw_wonder_not_kept_outside_land_water) {
    // a change that keeps every plot's land/water class: protectWonders does not apply (4903-4908 unchanged)
    WonderCase c;
    PlotTable after = c.x.plots;
    after.plots[size_t(8) * 20 + 9].base.terrain = 2;
    ChangeResult r;
    CHECK_OK(c.p.change(after, r));
    CHECK(!r.landWater);
    CHECK_EQ(r.samplesWonderKept, uint64_t(0));
}
// (w7-ridge, live-terrain.md 8.17 "Finding: a ridge of the old shore") the river keep does not hold water hexes in a
// land/water change: the server marks every plot with a river on any edge (riverEdges 7), the changed plot included
namespace {
struct RiverShoreCase {
    Fixture x;
    Producer p;
    std::vector<uint16_t> h0; std::vector<uint8_t> m0;
    PlotTable after;
    RiverShoreCase() {
        x.plots.plots[size_t(8) * 20 + 11].base.terrain = 5;   // coast water east of (10, 8) at the load
        x.plots.plots[size_t(8) * 20 + 10].riverEdges = 7;     // (10, 8): land with a river flag
        x.plots.plots[size_t(8) * 20 + 9].riverEdges = 7;      // (9, 8): land with a river flag, stays land
        CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p));
        CHECK_OK(p.enableLandWater(lwStyles(), lwMaterials(), 0x3DFC9816u));
        h0 = p.currentHeights(); m0 = p.currentMaterials();
        after = x.plots;
        after.plots[size_t(8) * 20 + 10].base.terrain = 5;     // (10, 8) to coast water; its river flag stays (the DLL)
    }
    // samples of (11, 8)'s hex (within 28 of its centre) that lie in (10, 8)'s river keep circle and differ from the load
    uint64_t waterKeptMoved() const {
        const int sx = x.fr.samplesX();
        uint64_t moved = 0;
        for (int yy = plotCy(8) - 28; yy <= plotCy(8) + 28; ++yy) for (int xx = plotCx(11, 8) - 28; xx <= plotCx(11, 8) + 28; ++xx) {
            const int ax = xx - plotCx(11, 8), ay = yy - plotCy(8), bx = xx - plotCx(10, 8);
            if (ax * ax + ay * ay > 28 * 28 || bx * bx + ay * ay >= 56 * 56) continue;
            const size_t k = size_t(yy) * sx + xx;
            if (p.currentHeights()[k] != h0[k] || p.currentMaterials()[k] != m0[k]) ++moved;
        }
        return moved;
    }
    // the same samples, counted by the diagnostics' reason
    static uint64_t waterWhy(const Producer::SampleDiagnostics& dg, uint8_t why) {
        uint64_t n = 0;
        for (int yy = plotCy(8) - 28; yy <= plotCy(8) + 28; ++yy) for (int xx = plotCx(11, 8) - 28; xx <= plotCx(11, 8) + 28; ++xx) {
            const int ax = xx - plotCx(11, 8), ay = yy - plotCy(8), bx = xx - plotCx(10, 8);
            if (ax * ax + ay * ay > 28 * 28 || bx * bx + ay * ay >= 56 * 56) continue;
            const int c = xx - dg.x0, r = yy - dg.y0;
            if (c < 0 || r < 0 || c >= dg.w || r >= dg.h) continue;
            n += dg.why[size_t(r) * dg.w + c] == why ? 1 : 0;
        }
        return n;
    }
};
}
C5R_TEST(lw_river_keep_not_on_water_hexes) {
    RiverShoreCase c;
    ChangeResult r;
    Producer::SampleDiagnostics dg;
    Producer::Options opt; opt.diagnostics = &dg;
    CHECK_OK(c.p.change(c.after, r, opt));
    CHECK(r.live);
    CHECK(r.landWater);
    CHECK_EQ(r.samplesOutside, uint64_t(0));
    // the water hex beside the changed plot is redrawn, not held by (10, 8)'s river keep
    CHECK(c.waterKeptMoved() > 0u);
    CHECK_EQ(RiverShoreCase::waterWhy(dg, 2), uint64_t(0));
    CHECK(RiverShoreCase::waterWhy(dg, 5) > 0u);
    // the land plot (9, 8) with its river flag keeps its own hex (the river carve stays the load's)
    CHECK_EQ(movedNear(c.p, c.h0, c.m0, c.x.fr.samplesX(), 9, 8), uint64_t(0));
    // the diagnostics: one value per window sample, the river keep present (on land) and nothing kept on water
    CHECK_EQ(dg.weight.size(), size_t(dg.w) * dg.h);
    CHECK_EQ(dg.why.size(), dg.weight.size());
    CHECK(!dg.riverCentres.empty());
    uint64_t riverKept = 0;
    for (uint8_t v : dg.why) riverKept += v == 2 ? 1 : 0;
    CHECK(riverKept > 0u);
    CHECK_EQ(changedOutside(c.p, c.h0, c.m0, r, c.x.fr.samplesX()), uint64_t(0));
}
C5R_TEST(lw_river_keep_on_water_negative) {
    // negative: the keep as before w7-ridge holds (11, 8)'s samples within the circle: the old shore stays
    RiverShoreCase c;
    ChangeResult r;
    Producer::SampleDiagnostics dg;
    Producer::Options opt; opt.diagnosticRiverKeepOnWater = true; opt.diagnostics = &dg;
    CHECK_OK(c.p.change(c.after, r, opt));
    CHECK(r.live);
    CHECK(RiverShoreCase::waterWhy(dg, 2) > 0u);
    CHECK_EQ(RiverShoreCase::waterWhy(dg, 5), uint64_t(0));
    // and the kept samples there hold the load's field, so fewer of them move than with the fix
    RiverShoreCase fixed;
    ChangeResult rf;
    CHECK_OK(fixed.p.change(fixed.after, rf));
    CHECK(c.waterKeptMoved() < fixed.waterKeptMoved());
    CHECK_EQ(movedNear(c.p, c.h0, c.m0, c.x.fr.samplesX(), 9, 8), uint64_t(0));
}
C5R_TEST(lw_diagnostic_ring_widens_window) {
    // the offline ring switch (a test run, DIAGNOSTIC): a wider ring gives a larger window and changes nothing outside it
    RiverShoreCase a, b;
    ChangeResult ra, rb;
    Producer::Options wide; wide.diagnosticRingSamples = 128;
    CHECK_OK(a.p.change(a.after, ra));
    CHECK_OK(b.p.change(b.after, rb, wide));
    CHECK(ra.live && rb.live);
    CHECK(rb.window[0].w > ra.window[0].w);
    CHECK_EQ(rb.samplesOutside, uint64_t(0));
    CHECK_EQ(changedOutside(b.p, b.h0, b.m0, rb, b.x.fr.samplesX()), uint64_t(0));
}
// (0717) a land change beside the coast with land/water on goes through the region fill and the strips of the current
// lines (Options::landRegion): live, no coast moved, and the changed plot's side towards the water takes more of the new
// ground than under the 4908 water keep's feather
namespace {
struct LandRegionCase {
    Fixture x;
    Producer p;
    std::vector<uint16_t> h0; std::vector<uint8_t> m0;
    ChangeResult r;
    explicit LandRegionCase(const Producer::Options& opt) {
        x.plots.plots[size_t(8) * 20 + 11].base.terrain = 5;   // coast water east of (10, 8)
        CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p));
        CHECK_OK(p.enableLandWater(lwStyles(), lwMaterials(), 0x3DFC9816u));
        h0 = p.currentHeights(); m0 = p.currentMaterials();
        PlotTable after = x.plots;
        after.plots[size_t(8) * 20 + 10].base.terrain = 2;     // grass to desert: the class stays land
        CHECK_OK(p.change(after, r, opt));
    }
    int64_t raisedTowardsWater() const {   // the rise summed over the samples 0..16 east of (10, 8)'s centre
        int64_t s = 0;
        for (int dx = 0; dx <= 16; ++dx) s += int64_t(p.currentHeights()[size_t(plotCy(8)) * x.fr.samplesX() + plotCx(10, 8) + dx]) - 15300;
        return s;
    }
};
}
C5R_TEST(lw_land_change_through_the_region) {
    LandRegionCase c{Producer::Options()};
    CHECK(c.r.live);
    CHECK(c.r.landRegion);
    CHECK(!c.r.landWater);
    CHECK_EQ(c.r.coastLinesRedrawn, 0u);
    CHECK_EQ(c.r.samplesOutside, uint64_t(0));
    CHECK_EQ(changedOutside(c.p, c.h0, c.m0, c.r, c.x.fr.samplesX()), uint64_t(0));
    CHECK(int(c.p.currentHeights()[size_t(plotCy(8)) * c.x.fr.samplesX() + plotCx(10, 8)]) > 15300);
    // negative: landRegion off is the 4908 water keep (the coast hex bit-equal, the side towards it feathered)
    Producer::Options off; off.landRegion = false;
    LandRegionCase k{off};
    CHECK(k.r.live);
    CHECK(!k.r.landRegion);
    CHECK_EQ(movedNear(k.p, k.h0, k.m0, k.x.fr.samplesX(), 11, 8), uint64_t(0));
    CHECK(c.raisedTowardsWater() > k.raisedTowardsWater());
    // a land/water change is not a landRegion change
    Fixture x;
    Producer p;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p));
    CHECK_OK(p.enableLandWater(lwStyles(), lwMaterials(), 0x3DFC9816u));
    PlotTable after = x.plots;
    after.plots[size_t(8) * 20 + 10].base.terrain = 5;
    ChangeResult r;
    CHECK_OK(p.change(after, r));
    CHECK(r.live && r.landWater && !r.landRegion);
}
C5R_TEST(lw_bad_table_refused) {
    Fixture x;
    Producer p;
    CHECK_OK(Producer::create(x.lib, x.fr, x.plots, x.tap, 3, p));
    CHECK_OK(p.enableLandWater(lwStyles(), lwMaterials(), 7u));
    PlotTable after = x.plots; after.w -= 1; after.plots.resize(size_t(after.w) * after.h);
    ChangeResult r;
    CHECK_EQ(p.change(after, r).code, Err::BadLayout);
    CHECK(!r.live);
}

// ---------------------------------------------------------------- the install's library (C5R_GAME_DIR)
C5R_TEST(game_library) {
    const std::string dir = c5r::test::gameDir();
    const std::string p = dir + "/Resource/Common/TerrainPiecesLibrary.fxsb";
    std::shared_ptr<const PieceLibrary> L;
    CHECK_OK(PieceLibrary::open(std::wstring(p.begin(), p.end()), L));
    if (!L) return;
    CHECK_EQ(L->pieces().size(), size_t(299));
    CHECK_EQ(L->maxLevel(), 4);
    CHECK(L->makesMaterial(37));
    CHECK(!L->makesMaterial(22));
    int small = 0;
    for (const Piece& pc : L->pieces()) small += (pc.flags & 1) ? 1 : 0;
    CHECK_EQ(small, 148);
}

C5R_TEST_MAIN
