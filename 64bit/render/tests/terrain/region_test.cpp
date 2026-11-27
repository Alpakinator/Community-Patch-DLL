// render/tests/terrain/region_test.cpp - epic 0465, ticket 4909 (live-terrain.md 8.14): the region path of the terrain
// renderer, the water module and the residency against today's whole-field path, on synthetic fields, bit for bit
// (region_core.h's digests). Needs C5R_EXE (the TerrainShader programs); without it the tests skip (exit 77).
// Written 2026-11-19 by w8-4909 for this repository (GPL-3.0-or-later).
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "c5r/bake/layers.h"
#include "c5r/bake/pipeline.h"
#include "region_core.h"
#include "terrainshot_core.h"
#include "testkit.h"

using namespace c5r;
using c5r::terrain::SampleRect;

namespace {

std::wstring wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }

uint32_t lcg(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return s >> 8;
}

// A field of mapW x mapH plots with hills (smooth, so the mips and z bounds vary by place) and noise, materials 0..40.
heightfield::HeightGrid field(uint32_t mapW, uint32_t mapH, bool wrap, uint32_t seed) {
    heightfield::HeightGrid g;
    g.mapW = mapW;
    g.mapH = mapH;
    g.samplesX = mapW * 64;
    g.samplesY = mapH * 64;
    g.wrapX = wrap;
    g.sx = 1.0f;
    g.sy = 0.866f;
    g.sz = 0.2f;
    g.ox = static_cast<float>(mapW * 32);
    g.oy = static_cast<float>(mapH) * 32 * 0.866f;
    g.waterZ = 10.117f;
    g.h.resize(static_cast<size_t>(g.samplesX) * g.samplesY);
    g.mat.resize(g.h.size());
    uint32_t s = seed;
    for (uint32_t r = 0; r < g.samplesY; ++r)
        for (uint32_t c = 0; c < g.samplesX; ++c) {
            const uint32_t base = ((c / 50 + r / 70) % 7) * 2400 + ((c * 37 + r * 11) % 1500);
            g.h[g.index(c, r)] = static_cast<uint16_t>(base + (lcg(s) & 255));
            g.mat[g.index(c, r)] = static_cast<uint8_t>(((c / 64) * 3 + (r / 64) * 5) % 41);
        }
    return g;
}

// after = before with every sample inside rects changed (a raised block, new materials)
heightfield::HeightGrid edited(const heightfield::HeightGrid& before, const std::vector<SampleRect>& rects, uint32_t seed) {
    heightfield::HeightGrid g = before;
    uint32_t s = seed;
    for (const SampleRect& rc : rects)
        for (uint32_t y = rc.y0; y < rc.y1; ++y)
            for (uint32_t x = rc.x0; x < rc.x1; ++x) {
                g.h[g.index(x, y)] = static_cast<uint16_t>(30000 + (lcg(s) % 20000));
                g.mat[g.index(x, y)] = static_cast<uint8_t>(37 + (lcg(s) % 3));
            }
    return g;
}

SampleRect rect(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
    SampleRect r;
    r.x0 = x0;
    r.y0 = y0;
    r.x1 = x1;
    r.y1 = y1;
    return r;
}

void checkCase(const char* name, const heightfield::HeightGrid& before, const std::vector<SampleRect>& rects, uint32_t tilePlots) {
    const std::string exe = test::exePath();   // skips without C5R_EXE
    terrainshot::Device d;
    CHECK_OK(d.create(false));
    const heightfield::HeightGrid after = edited(before, rects, 99);
    const regiontest::Report r = regiontest::runRegionCompare(d.dev, d.ctx, wide(exe), L"", before, after, rects, tilePlots, 1);
    if (!r.terrain.equal) test::fail(__FILE__, __LINE__, std::string(name) + ": terrain region != whole " + r.terrain.error);
    if (!r.terrainBuilt.equal) test::fail(__FILE__, __LINE__, std::string(name) + ": terrain (built pages) != whole " + r.terrainBuilt.error);
    if (!r.water.equal) test::fail(__FILE__, __LINE__, std::string(name) + ": water region != whole " + r.water.error);
    if (tilePlots == 1 && !r.residency.equal)
        test::fail(__FILE__, __LINE__, std::string(name) + ": residency region != whole " + r.residency.error);
    CHECK(r.pages >= 1);
    CHECK(r.terrainStats.pagesRebuilt >= 1);
    CHECK_EQ(r.terrainBuiltStats.pagesFromWorker, r.terrainBuiltStats.pagesRebuilt);
    std::printf("  %s: pages %u, terrain tiles bounds %u mat %u land %u exe %u, residency %u scanned %u changed\n", name, r.pages,
                r.terrainStats.tilesBounds, r.terrainStats.tilesMat, r.terrainStats.tilesLand, r.terrainStats.exeTiles,
                r.resStats.tilesScanned, r.resStats.tilesChanged);
}

} // namespace

// 32 x 20 plots: 3 x 2 EXE pages; the rects of a live window (inside, on page overlaps, at the edges, across the seam).
C5R_TEST(region_equals_whole_inside) {
    const heightfield::HeightGrid g = field(32, 20, true, 1);
    checkCase("inside", g, {rect(1000, 600, 1256, 856)}, 1);
}
C5R_TEST(region_equals_whole_page_overlap) {
    const heightfield::HeightGrid g = field(32, 20, true, 2);
    checkCase("overlap", g, {rect(830, 830, 960, 960)}, 1);   // the 128-sample overlap of 4 pages
}
C5R_TEST(region_equals_whole_seam_and_edges) {
    const heightfield::HeightGrid g = field(32, 20, true, 3);
    // a window across the x seam (two rects), on row 0 and on the last rows (the pages wrap in y; exeTileClass3 too)
    checkCase("seam", g, {rect(0, 0, 70, 90), rect(2000, 0, 2048, 90)}, 1);
    checkCase("last rows", g, {rect(500, 1250, 700, 1280)}, 1);
    checkCase("one sample", g, {rect(1500, 700, 1501, 701)}, 1);
}
C5R_TEST(region_equals_whole_no_wrap_two_plot_tiles) {
    const heightfield::HeightGrid g = field(32, 20, false, 4);
    checkCase("nowrap 2-plot", g, {rect(0, 300, 200, 420), rect(1900, 1100, 2048, 1280)}, 2);
    checkCase("nowrap 1-plot", g, {rect(0, 300, 200, 420)}, 1);
}
C5R_TEST(region_two_windows) {
    const heightfield::HeightGrid g = field(32, 20, true, 5);
    checkCase("two windows", g, {rect(100, 100, 300, 300), rect(1700, 900, 1950, 1150)}, 1);
}

// Refusals: no field, another layout, a rect outside; nothing replaced (the whole field is the caller's fallback).
C5R_TEST(region_refusals) {
    const std::string exe = test::exePath();
    terrainshot::Device d;
    CHECK_OK(d.create(false));
    terrain::Renderer r;
    CHECK_OK(r.init(d.dev, wide(exe), L""));
    const heightfield::HeightGrid g = field(16, 8, true, 6);
    std::vector<SampleRect> one = {rect(10, 10, 20, 20)};
    CHECK_ERR(r.updateHeightRegion(g, one), Err::NotFound);
    CHECK_OK(r.setHeightField(g, 1));
    std::vector<uint64_t> d0, d1;
    CHECK_OK(r.fieldDigest(d0));
    heightfield::HeightGrid other = g;
    other.sz = 0.25f;
    CHECK_ERR(r.updateHeightRegion(other, one), Err::BadLayout);
    heightfield::HeightGrid noMat = g;
    noMat.mat.clear();
    CHECK_ERR(r.updateHeightRegion(noMat, one), Err::BadLayout);
    CHECK_ERR(r.updateHeightRegion(g, {rect(10, 10, 10, 20)}), Err::BadLayout);          // empty
    CHECK_ERR(r.updateHeightRegion(g, {rect(1000, 10, 1025, 20)}), Err::BadLayout);      // past samplesX
    std::vector<bake::HeightPage> bad(1);
    bad[0].levels.resize(1);
    CHECK_ERR(r.updateHeightRegion(g, one, &bad), Err::BadLayout);
    CHECK_OK(r.fieldDigest(d1));
    CHECK(d0 == d1);   // refused calls replaced nothing
    water::Renderer w;
    CHECK_OK(w.init(d.dev, wide(exe), L""));
    CHECK_ERR(w.updateHeightRegion(g, one), Err::NotFound);
    CHECK_OK(w.setHeightField(g));
    CHECK_ERR(w.updateHeightRegion(other, one), Err::BadLayout);
    bake::StandinBaker b{bake::StandinFaults()};
    CHECK_OK(b.init(d.dev, d.ctx));
    bake::Residency res;
    CHECK_OK(res.init(&b, bake::ResidencyOptions()));
    CHECK_ERR(res.onFieldRegion(d.dev, g, one), Err::NotFound);
    CHECK_OK(res.onField(d.dev, g, 1, 1));
    CHECK_ERR(res.onFieldRegion(d.dev, other, one), Err::BadLayout);
    CHECK_ERR(res.onFieldRegion(nullptr, g, one), Err::BadLayout);
}

// The bake pipeline's EXE height pages (makeExeHeightPages, the server's bake) are kept by the grid's address and sizes:
// an in-place edit of the same grid (a live terrain window) keeps the old page until the source is told
// (heightsChanged, which PipelineBaker::setMap calls with MapInputs::changedBoxes) [C for ticket 4909's finding].
C5R_TEST(exe_pages_follow_an_in_place_edit) {
    const std::string exe = test::exePath();   // a device as the other tests (the EXE is not read here)
    (void)exe;
    terrainshot::Device d;
    CHECK_OK(d.create(false));
    heightfield::HeightGrid g = field(32, 20, true, 7);
    std::unique_ptr<bake::HeightPageSource> src = bake::makeExeHeightPages();
    bake::TileWindow w;
    bake::TileId t;
    t.x = 20;
    t.y = 10;
    CHECK_OK(bake::tileWindow(g, t, w));
    bake::HeightPageView v0;
    CHECK_OK(src->page(d.dev, d.ctx, g, w, v0));
    uint64_t h0 = 0xcbf29ce484222325ull;
    CHECK_OK(terrain::digestTexture(d.dev, v0.srv, h0));
    // the edit: tile (20, 10)'s samples, in place
    const std::vector<SampleRect> rc = {rect(20 * 64, 10 * 64, 21 * 64, 11 * 64)};
    const heightfield::HeightGrid e = edited(g, rc, 3);
    g.h = e.h;   // same vector object: std::vector assignment of equal size keeps the buffer
    // the page a fresh build of the edited grid gives
    bake::HeightPageIndex pi;
    int32_t off[2] = {0, 0};
    CHECK_OK(bake::heightPageForTile(g, t, pi, off));
    bake::HeightPage fresh;
    CHECK_OK(bake::buildHeightPage(g, pi, bake::kHeightPageMips, fresh));
    ID3D11Texture2D* tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    CHECK_OK(bake::createHeightPageTexture(d.dev, fresh, &tex, &srv));
    uint64_t hf = 0xcbf29ce484222325ull;
    CHECK_OK(terrain::digestTexture(d.dev, srv, hf));
    srv->Release();
    tex->Release();
    CHECK(h0 != hf);   // the edit reaches the page
    // without a notice: the old page (the defect of the live path before 4909)
    bake::HeightPageView v1;
    CHECK_OK(src->page(d.dev, d.ctx, g, w, v1));
    uint64_t h1 = 0xcbf29ce484222325ull;
    CHECK_OK(terrain::digestTexture(d.dev, v1.srv, h1));
    CHECK(h1 == h0);
    // a notice elsewhere keeps it; one on the edit drops it and the next page is the fresh one
    const uint32_t elsewhere[4] = {0, 0, 10, 10};
    src->heightsChanged(g, elsewhere, 1);
    bake::HeightPageView v2;
    CHECK_OK(src->page(d.dev, d.ctx, g, w, v2));
    uint64_t h2 = 0xcbf29ce484222325ull;
    CHECK_OK(terrain::digestTexture(d.dev, v2.srv, h2));
    CHECK(h2 == h0);
    const uint32_t box[4] = {rc[0].x0, rc[0].y0, rc[0].x1, rc[0].y1};
    src->heightsChanged(g, box, 1);
    bake::HeightPageView v3;
    CHECK_OK(src->page(d.dev, d.ctx, g, w, v3));
    uint64_t h3 = 0xcbf29ce484222325ull;
    CHECK_OK(terrain::digestTexture(d.dev, v3.srv, h3));
    CHECK(h3 == hf);
    // no boxes: every page dropped
    src->heightsChanged(g, nullptr, 0);
    bake::HeightPageView v4;
    CHECK_OK(src->page(d.dev, d.ctx, g, w, v4));
    uint64_t h4 = 0xcbf29ce484222325ull;
    CHECK_OK(terrain::digestTexture(d.dev, v4.srv, h4));
    CHECK(h4 == hf);
}

C5R_TEST(span_meets_wrapped_window) {
    using terrain::spanMeetsWrappedWindow;
    CHECK(spanMeetsWrappedWindow(10, 20, 15, 4, 100));
    CHECK(!spanMeetsWrappedWindow(10, 20, 20, 4, 100));
    CHECK(spanMeetsWrappedWindow(0, 2, -64, 1024, 100));      // window longer than the period
    CHECK(spanMeetsWrappedWindow(95, 100, -10, 6, 100));      // window -10..-5 = 90..95
    CHECK(!spanMeetsWrappedWindow(96, 100, -10, 6, 100));
    CHECK(spanMeetsWrappedWindow(0, 3, 98, 4, 100));          // window 98, 99, 0, 1
    CHECK(spanMeetsWrappedWindow(97, 101 - 1, 99, 1, 100));
    CHECK(!spanMeetsWrappedWindow(2, 3, 98, 4, 100));
    CHECK(spanMeetsWrappedWindow(90, 99, 95, 10, 100));
}

C5R_TEST_MAIN
