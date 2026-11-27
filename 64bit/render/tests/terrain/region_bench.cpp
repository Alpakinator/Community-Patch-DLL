// render/tests/terrain/region_bench.cpp - epic 0465, ticket 4909 (live-terrain.md 8.14): the offline bench of a live
// terrain change on recorded fields. Reads a field before and after one change (raw u16 heights and u8 materials of the
// whole grid, samplesX x samplesY, as 4903's chains store them: field.<k>.h16 / .m8), runs region_core.h's compare and
// prints one JSON line: the whole-field and region times per module (median of --reps) and whether the digests agree.
// Written 2026-11-19 by w8-4909 for this repository (GPL-3.0-or-later).
//
//   c5r_terrain_region_bench --exe EXE [--game DIR] --plots W H --before H16 M8 --after H16 M8 [--rect X Y W H ...]
//                            [--reps N] [--tile-plots 1] [--no-water] [--no-residency]
// --plots: the grid's plots (the field's rows, the DLL's map + 4). Without --rect the bounding box of every differing
// sample. Exit 0: every compared module equal; 1: a module differs or failed; 2: usage or input.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "region_core.h"
#include "terrainshot_core.h"

using namespace c5r;

namespace {
bool readFile(const std::string& p, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? static_cast<size_t>(n) : 0);
    const size_t got = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return got == out.size();
}
bool load(const std::string& h16, const std::string& m8, uint32_t W, uint32_t H, heightfield::HeightGrid& g) {
    g = heightfield::HeightGrid();
    g.mapW = W;
    g.mapH = H;
    g.samplesX = W * 64;
    g.samplesY = H * 64;
    g.wrapX = true;
    g.sx = 1.0f;
    g.sy = 0.866f;
    g.sz = 0.2f;
    g.ox = static_cast<float>(W * 32);
    g.oy = static_cast<float>(H) * 32 * 0.866f;
    g.waterZ = 10.117f;
    std::vector<uint8_t> hb, mb;
    const size_t n = static_cast<size_t>(g.samplesX) * g.samplesY;
    if (!readFile(h16, hb) || !readFile(m8, mb) || hb.size() != n * 2 || mb.size() != n) return false;
    g.h.resize(n);
    std::memcpy(g.h.data(), hb.data(), n * 2);
    g.mat = mb;
    return true;
}
void times(const char* name, const std::vector<double>& v, const regiontest::PartResult& p) {
    std::printf("\"%s\":[", name);
    for (size_t i = 0; i < v.size(); ++i) std::printf("%s%.2f", i ? "," : "", v[i]);
    std::printf("],\"%s_median\":%.2f,", name, p.median(v));
}
void part(const char* name, const regiontest::PartResult& p) {
    std::printf("\"%s\":{\"equal\":%s,", name, p.equal ? "true" : "false");
    times("whole_ms", p.wholeMs, p);
    times("region_ms", p.regionMs, p);
    std::printf("\"digests\":%zu,\"error\":\"%s\"},", p.whole.size(), p.error.c_str());
}
} // namespace

int main(int argc, char** argv) {
    std::string exe, game, bh, bm, ah, am;
    uint32_t W = 0, H = 0, tp = 1;
    int reps = 5;
    bool doWater = true, doRes = true;
    std::vector<terrain::SampleRect> rects;
    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];
        auto need = [&](int n) { return i + n < argc; };
        if (k == "--exe" && need(1)) exe = argv[++i];
        else if (k == "--game" && need(1)) game = argv[++i];
        else if (k == "--plots" && need(2)) {
            W = static_cast<uint32_t>(std::atoi(argv[++i]));
            H = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (k == "--before" && need(2)) {
            bh = argv[++i];
            bm = argv[++i];
        } else if (k == "--after" && need(2)) {
            ah = argv[++i];
            am = argv[++i];
        } else if (k == "--rect" && need(4)) {
            terrain::SampleRect r;
            r.x0 = static_cast<uint32_t>(std::atoi(argv[++i]));
            r.y0 = static_cast<uint32_t>(std::atoi(argv[++i]));
            r.x1 = r.x0 + static_cast<uint32_t>(std::atoi(argv[++i]));
            r.y1 = r.y0 + static_cast<uint32_t>(std::atoi(argv[++i]));
            rects.push_back(r);
        } else if (k == "--reps" && need(1)) reps = std::atoi(argv[++i]);
        else if (k == "--tile-plots" && need(1)) tp = static_cast<uint32_t>(std::atoi(argv[++i]));
        else if (k == "--no-water") doWater = false;
        else if (k == "--no-residency") doRes = false;
        else {
            std::fprintf(stderr, "region_bench: unknown or incomplete option %s\n", k.c_str());
            return 2;
        }
    }
    if (exe.empty() || !W || !H || bh.empty() || ah.empty() || reps < 1) {
        std::fprintf(stderr, "region_bench: --exe --plots --before --after are required\n");
        return 2;
    }
    heightfield::HeightGrid before, after;
    if (!load(bh, bm, W, H, before) || !load(ah, am, W, H, after)) {
        std::fprintf(stderr, "region_bench: a field could not be read as %u x %u plots\n", W, H);
        return 2;
    }
    const std::vector<terrain::SampleRect> diff = regiontest::diffBox(before, after);
    if (rects.empty()) rects = diff;
    if (rects.empty()) {
        std::fprintf(stderr, "region_bench: the fields are equal and no --rect given\n");
        return 2;
    }
    terrainshot::Device d;
    Status st = d.create(false);
    if (!st.ok()) {
        std::fprintf(stderr, "region_bench: device: %s\n", st.str().c_str());
        return 2;
    }
    const regiontest::Report r = regiontest::runRegionCompare(d.dev, d.ctx, std::wstring(exe.begin(), exe.end()),
                                                              std::wstring(game.begin(), game.end()), before, after, rects, tp,
                                                              reps, doWater, doRes);
    std::printf("{\"plots\":[%u,%u],\"adapter\":\"%s\",\"reps\":%d,\"rects\":[", W, H, d.adapter.c_str(), reps);
    for (size_t i = 0; i < rects.size(); ++i)
        std::printf("%s[%u,%u,%u,%u]", i ? "," : "", rects[i].x0, rects[i].y0, rects[i].x1 - rects[i].x0, rects[i].y1 - rects[i].y0);
    std::printf("],\"diff_box\":");
    if (diff.empty()) std::printf("null,");
    else std::printf("[%u,%u,%u,%u],", diff[0].x0, diff[0].y0, diff[0].x1 - diff[0].x0, diff[0].y1 - diff[0].y0);
    std::printf("\"pages\":%u,", r.pages);
    regiontest::PartResult pb;
    times("page_build_ms", r.pageBuildMs, pb);
    part("terrain", r.terrain);
    part("terrain_built", r.terrainBuilt);
    std::printf("\"terrain_stats\":{\"pages\":%u,\"bounds\":%u,\"mat\":%u,\"land\":%u,\"exe\":%u,\"build_ms\":%.2f,\"upload_ms\":%.2f,"
                "\"tiles_ms\":%.2f},",
                r.terrainStats.pagesRebuilt, r.terrainStats.tilesBounds, r.terrainStats.tilesMat, r.terrainStats.tilesLand,
                r.terrainStats.exeTiles, r.terrainStats.buildMs, r.terrainStats.uploadMs, r.terrainStats.tilesMs);
    std::printf("\"terrain_built_stats\":{\"from_worker\":%u,\"upload_ms\":%.2f,\"tiles_ms\":%.2f},", r.terrainBuiltStats.pagesFromWorker,
                r.terrainBuiltStats.uploadMs, r.terrainBuiltStats.tilesMs);
    if (doWater) part("water", r.water);
    if (doRes) {
        part("residency", r.residency);
        std::printf("\"residency_stats\":{\"scanned\":%u,\"changed\":%u},", r.resStats.tilesScanned, r.resStats.tilesChanged);
    }
    const bool ok = r.terrain.equal && r.terrainBuilt.equal && (!doWater || r.water.equal) && (!doRes || r.residency.equal);
    std::printf("\"ok\":%s}\n", ok ? "true" : "false");
    return ok ? 0 : 1;
}
