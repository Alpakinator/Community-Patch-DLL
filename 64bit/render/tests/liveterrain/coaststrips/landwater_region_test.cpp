// Ticket 4914 (epic 0615; live-terrain.md 10.9 / the 4914 log's item 3): a land/water window through the region path
// of the terrain renderer, the water module and the residency (ticket 4909's region_core.h), bit for bit against the
// whole-field path. The land class (Tile::land: the highest sample of the tile and its 4-sample border at 2.0 world
// units, u16 510, or above) is in the terrain digest, so a coast that moves by 20 samples must re-class exactly the
// tiles the whole path re-classes. Needs C5R_EXE (the TerrainShader programs); without it the test skips (exit 77).
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "../../terrain/region_core.h"
#include "terrainshot_core.h"
#include "testkit.h"

using namespace c5r;
using c5r::terrain::SampleRect;

namespace {

std::wstring wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }

SampleRect rect(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
    SampleRect r;
    r.x0 = x0; r.y0 = y0; r.x1 = x1; r.y1 = y1;
    return r;
}

// mapW x mapH plots: x < coast sea (u16 200..499, material 37), the rest land (rolling hills, materials 0..40)
heightfield::HeightGrid field(uint32_t mapW, uint32_t mapH, uint32_t coast) {
    heightfield::HeightGrid g;
    g.mapW = mapW; g.mapH = mapH;
    g.samplesX = mapW * 64; g.samplesY = mapH * 64;
    g.wrapX = true;
    g.sx = 1.0f; g.sy = 0.866f; g.sz = 0.2f;
    g.ox = static_cast<float>(mapW * 32);
    g.oy = static_cast<float>(mapH) * 32 * 0.866f;
    g.waterZ = 10.117f;
    g.h.resize(static_cast<size_t>(g.samplesX) * g.samplesY);
    g.mat.resize(g.h.size());
    for (uint32_t r = 0; r < g.samplesY; ++r)
        for (uint32_t c = 0; c < g.samplesX; ++c) {
            const bool sea = c < coast;
            g.h[g.index(c, r)] = static_cast<uint16_t>(sea ? 200 + (c * 7 + r * 3) % 300
                                                           : 9000 + ((c / 50 + r / 70) % 7) * 2400 + (c * 37 + r * 11) % 1500);
            g.mat[g.index(c, r)] = static_cast<uint8_t>(sea ? 37 : ((c / 64) * 3 + (r / 64) * 5) % 41);
        }
    return g;
}

void fill(heightfield::HeightGrid& g, const SampleRect& r, uint16_t h, uint8_t m) {
    for (uint32_t y = r.y0; y < r.y1; ++y)
        for (uint32_t x = r.x0; x < r.x1; ++x) {
            g.h[g.index(x, y)] = h;
            g.mat[g.index(x, y)] = m;
        }
}

} // namespace

C5R_TEST(region_equals_whole_land_water) {
    const std::string exe = test::exePath();   // skips without C5R_EXE
    terrainshot::Device d;
    CHECK_OK(d.create(false));
    const uint32_t coast = 1024;
    const heightfield::HeightGrid before = field(32, 20, coast);
    // new land 20 samples into the sea, new water 20 samples into the land (beach height and material 3 / sea 37), and a
    // whole tile with its 4-sample border flooded, on a 1-plot tile grid and on a 2-plot one
    const std::vector<SampleRect> rects = {rect(coast - 20, 300, coast, 500), rect(coast, 700, coast + 20, 900),
                                           rect(1600 - 4, 1024 - 4, 1664 + 4, 1088 + 4)};
    heightfield::HeightGrid after = before;
    fill(after, rects[0], 13000, 3);
    fill(after, rects[1], 300, 37);
    fill(after, rects[2], 250, 37);
    for (uint32_t tilePlots = 1; tilePlots <= 2; ++tilePlots) {
        const regiontest::Report r = regiontest::runRegionCompare(d.dev, d.ctx, wide(exe), L"", before, after, rects, tilePlots, 1);
        const std::string tag = "land/water, " + std::to_string(tilePlots) + "-plot tiles: ";
        if (!r.terrain.equal) test::fail(__FILE__, __LINE__, tag + "terrain region != whole " + r.terrain.error);
        if (!r.terrainBuilt.equal) test::fail(__FILE__, __LINE__, tag + "terrain (built pages) != whole " + r.terrainBuilt.error);
        if (!r.water.equal) test::fail(__FILE__, __LINE__, tag + "water region != whole " + r.water.error);
        if (tilePlots == 1 && !r.residency.equal)
            test::fail(__FILE__, __LINE__, tag + "residency region != whole " + r.residency.error);
        CHECK(r.terrainStats.tilesLand > 0);
        std::printf("  %spages %u, terrain tiles bounds %u mat %u land %u exe %u, residency %u scanned %u changed\n", tag.c_str(),
                    r.pages, r.terrainStats.tilesBounds, r.terrainStats.tilesMat, r.terrainStats.tilesLand,
                    r.terrainStats.exeTiles, r.resStats.tilesScanned, r.resStats.tilesChanged);
    }
    // the land class really changes: the flooded tile (tile 25, 16 of the 1-plot grid, with its 4-sample border) is land before, sea after
    uint16_t hb = 0, ha = 0;
    for (uint32_t y = 1024 - 4; y < 1088 + 4; ++y)
        for (uint32_t x = 1600 - 4; x < 1664 + 4; ++x) {
            hb = std::max(hb, before.h[before.index(x, y)]);
            ha = std::max(ha, after.h[after.index(x, y)]);
        }
    CHECK(hb >= 510 && ha < 510);
}

C5R_TEST_MAIN
