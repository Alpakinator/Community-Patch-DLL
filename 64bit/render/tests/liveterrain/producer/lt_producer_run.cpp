// lt_producer_run - runs the region producer on recorded inputs (ticket 4903; live-terrain.md 8). Offline only.
//
//   lt_producer_run --lib FXSB --block BLOCK.bin --plots PLOTS.txt --h16 H.raw --m8 M.raw [--seed HEX]
//                   [--own OWN_PREFIX] [--change AFTER.txt --result RESULT_PREFIX] [--field-after FIELD_PREFIX]
//
// BLOCK.bin: the EXE's grid constants block 0xD90FBC..0xD9132C (880 bytes). PLOTS.txt: "W H", then one line per plot in
// the DLL's order: "x y terrain hills mountain feature featureType continentArt riverEdges lake" (featureType "-" when
// none). H.raw / M.raw: the tap's dense layers (u16 / u8, x fastest). Writes OWN_PREFIX.h16 / .m8 (the own blend of
// the whole map), RESULT_PREFIX.json and RESULT_PREFIX.<i>.h16 / .m8 per window rectangle, FIELD_PREFIX.h16 / .m8 (the
// current field after the change; FIELD_PREFIX.<i>.h16 per change). --change and --result take comma-separated
// lists, applied in order to one producer. Prints one JSON line per step. Exit 0 on success, 2 on bad input, 1 on an error.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "c5r/bake/layers.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/liveterrain/coastlines.h"
#include "c5r/liveterrain/coaststrips.h"
#include "c5r/liveterrain/producer.h"

using namespace c5r;
using namespace c5r::liveterrain;

namespace {
bool readFile(const std::string& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}
bool writeFile(const std::string& p, const void* d, size_t n) {
    std::ofstream f(p, std::ios::binary);
    if (!f) return false;
    f.write(static_cast<const char*>(d), std::streamsize(n));
    return bool(f);
}
bool readPlots(const std::string& p, PlotTable& t) {
    std::ifstream f(p);
    if (!f || !(f >> t.w >> t.h) || t.w <= 0 || t.h <= 0 || t.w > 1024 || t.h > 1024) return false;
    t.plots.assign(size_t(t.w) * t.h, PlotInfo());
    std::vector<uint8_t> seen(t.plots.size(), 0);
    for (size_t i = 0; i < t.plots.size(); ++i) {
        int x, y, terrain, hills, mountain, feature, cart, river, lake; std::string ft;
        if (!(f >> x >> y >> terrain >> hills >> mountain >> feature >> ft >> cart >> river >> lake)) return false;
        if (x < 0 || x >= t.w || y < 0 || y >= t.h || seen[size_t(y) * t.w + x]) return false;
        seen[size_t(y) * t.w + x] = 1;
        PlotInfo& q = t.plots[size_t(y) * t.w + x];
        q.base.terrain = terrain; q.base.hills = hills != 0; q.base.mountain = mountain != 0; q.base.feature = feature;
        if (ft != "-") { q.base.featureType = ft; q.base.naturalWonder = ft.compare(0, 8, "FEATURE_") == 0 && ft != "FEATURE_MARSH"; }
        q.continentArt = cart; q.riverEdges = uint8_t(river); q.lake = lake != 0;
    }
    return true;
}
std::string esc(const std::string& s) { std::string o; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; } return o; }
}

int main(int argc, char** argv) {
    std::string lib, block, plots, h16, m8, own, change, result, fieldAfter, seedArg, batch;
    std::string coastDir, gameDir, mapSeedArg;   // 4913: land/water (--coast DIR --game-dir INSTALL --map-seed N)
    bool diag = false;   // --diagnostic-ignore-clean 1: offline pictures only (live-terrain.md 8.5)
    bool protW = true, protM = true, protR = true;   // --protect-water 0 / --protect-marsh 0: 4903's stricter rule (8.9)
    bool protNW = true;   // --protect-wonders 0: a land/water change redraws natural wonders' own hexes too (8.17 (b))
    std::string diagOut;  // --diag-out PREFIX: per window sample the weights and why kept (PREFIX.<i>.*; w7-ridge, 8.17)
    int ringArg = 0;      // --ring N: a test run with an N-sample ring in place of kRingSamples (DIAGNOSTIC)
    bool keepOnWater = false;   // --river-keep-water 1: the river keep before w7-ridge (the negative, DIAGNOSTIC)
    // ticket 0717: --river-disc 1 the 4908 river keep in place of the band; --river-any-flag 1 the plot table's river
    // bits are the feed's any-edge flag (as server.exe gives them); --land-region 0 the 4908 water keep for land changes
    bool riverDisc = false, riverAny = false, landRegion = true;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string k = argv[i], v = argv[i + 1];
        if (k == "--lib") lib = v; else if (k == "--block") block = v; else if (k == "--plots") plots = v;
        else if (k == "--h16") h16 = v; else if (k == "--m8") m8 = v; else if (k == "--own") own = v;
        else if (k == "--change") change = v; else if (k == "--result") result = v; else if (k == "--seed") seedArg = v;
        else if (k == "--field-after") fieldAfter = v;
        else if (k == "--diagnostic-ignore-clean") diag = v == "1";
        else if (k == "--batch") batch = v;
        else if (k == "--coast") coastDir = v;
        else if (k == "--game-dir") gameDir = v;
        else if (k == "--map-seed") mapSeedArg = v;
        else if (k == "--protect-water") protW = v != "0";
        else if (k == "--protect-marsh") protM = v != "0";
        else if (k == "--protect-rivers") protR = v != "0";
        else if (k == "--protect-wonders") protNW = v != "0";
        else if (k == "--diag-out") diagOut = v;
        else if (k == "--ring") ringArg = std::atoi(v.c_str());
        else if (k == "--river-keep-water") keepOnWater = v == "1";
        else if (k == "--river-disc") riverDisc = v == "1";
        else if (k == "--river-any-flag") riverAny = v == "1";
        else if (k == "--land-region") landRegion = v != "0";
        else { std::fprintf(stderr, "lt_producer_run: unknown option %s\n", k.c_str()); return 2; }
    }
    if (lib.empty() || block.empty() || plots.empty() || h16.empty() || m8.empty() || (!change.empty() && result.empty())) {
        std::fprintf(stderr, "lt_producer_run: --lib --block --plots --h16 --m8 required (--result with --change)\n");
        return 2;
    }
    std::shared_ptr<const PieceLibrary> L;
    auto t0 = std::chrono::steady_clock::now();
    Status st = PieceLibrary::open(std::wstring(lib.begin(), lib.end()), L);
    if (!st) { std::fprintf(stderr, "lt_producer_run: library: %s\n", st.str().c_str()); return 1; }
    const double msLib = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::vector<uint8_t> blk, hb, mb;
    if (!readFile(block, blk) || !readFile(h16, hb) || !readFile(m8, mb)) { std::fprintf(stderr, "lt_producer_run: cannot read an input\n"); return 2; }
    MapFrame fr;
    st = MapFrame::fromConstantsBlock(blk.data(), blk.size(), fr);
    if (!st) { std::fprintf(stderr, "lt_producer_run: frame: %s\n", st.str().c_str()); return 1; }
    PlotTable pt;
    if (!readPlots(plots, pt)) { std::fprintf(stderr, "lt_producer_run: bad plots file\n"); return 2; }
    const size_t ns = size_t(fr.samplesX()) * fr.samplesY();
    if (hb.size() != ns * 2 || mb.size() != ns) { std::fprintf(stderr, "lt_producer_run: tap size does not fit the frame\n"); return 2; }
    std::vector<uint16_t> H(ns); std::memcpy(H.data(), hb.data(), ns * 2);
    TapView tap; tap.heights = H.data(); tap.materials = mb.data(); tap.samplesX = fr.samplesX(); tap.samplesY = fr.samplesY();
    const uint64_t seed = seedArg.empty() ? deriveSeed(fr, pt) : std::stoull(seedArg, nullptr, 16);
    // 4913: the coast styles (coaststyles.xml and the character splines in --coast DIR) and the TerrainDef rows
    // (--game-dir, through c5r::gamefs) for enableLandWater
    CoastStyles coast; StripMaterials mats; uint32_t mapSeed = 0;
    const bool landWater = !coastDir.empty();
    if (landWater) {
        if (gameDir.empty() || mapSeedArg.empty()) { std::fprintf(stderr, "lt_producer_run: --coast needs --game-dir and --map-seed\n"); return 2; }
        mapSeed = uint32_t(std::strtoull(mapSeedArg.c_str(), nullptr, 0));
        auto rd = [&](const std::string& path, std::string& text) {
            std::vector<uint8_t> b; if (!readFile(path, b)) return false; text.assign(b.begin(), b.end()); return true; };
        auto lower = [](std::string x) { for (char& c : x) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a'); return x; };
        std::string xml;
        if (!rd(coastDir + "/coaststyles.xml", xml) && !rd(coastDir + "/CoastStyles.xml", xml)) { std::fprintf(stderr, "lt_producer_run: no coaststyles.xml\n"); return 2; }
        st = CoastStyles::parse(xml, [&](const std::string& n, std::string& t) { return rd(coastDir + "/" + n, t) || rd(coastDir + "/" + lower(n), t); }, coast);
        if (!st) { std::fprintf(stderr, "lt_producer_run: coast styles: %s\n", st.str().c_str()); return 2; }
        gamefs::GameFiles fs;
        st = fs.open(std::wstring(gameDir.begin(), gameDir.end()));
        bake::LayerTables lt;
        if (st.ok()) st = bake::loadTerrainDef(fs, lt);
        if (st.ok()) st = StripMaterials::fromLayerTables(lt, mats);
        if (!st) { std::fprintf(stderr, "lt_producer_run: TerrainDef: %s\n", st.str().c_str()); return 2; }
    }
    Producer P;
    t0 = std::chrono::steady_clock::now();
    st = Producer::create(L, fr, pt, tap, seed, P);
    if (st.ok() && landWater) st = P.enableLandWater(coast, mats, mapSeed);
    const double msCreate = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!st) { std::fprintf(stderr, "lt_producer_run: create: %s\n", st.str().c_str()); return 1; }
    std::printf("{\"step\":\"create\",\"map\":\"%dx%d\",\"seed\":\"%016llx\",\"placements\":%zu,\"draws\":%llu,\"ms_library\":%.1f,\"ms_create\":%.1f}\n",
                fr.plotsW, fr.plotsH, (unsigned long long)seed, P.placements().size(), (unsigned long long)P.matcherDraws(), msLib, msCreate);
    if (!own.empty()) {
        std::vector<uint16_t> oh; std::vector<uint8_t> om;
        t0 = std::chrono::steady_clock::now();
        P.ownField(oh, om);
        const double msOwn = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!writeFile(own + ".h16", oh.data(), oh.size() * 2) || !writeFile(own + ".m8", om.data(), om.size())) { std::fprintf(stderr, "lt_producer_run: cannot write the own field\n"); return 1; }
        std::printf("{\"step\":\"own\",\"ms_blend\":%.1f}\n", msOwn);
    }
    // --batch FILE: lines "label after.txt"; each change is applied to a FRESH producer of the load (the measurement of
    // live-terrain.md 8.8), and one JSON line per change is printed (no window files).
    if (!batch.empty()) {
        std::ifstream bf(batch);
        if (!bf) { std::fprintf(stderr, "lt_producer_run: cannot read the batch file\n"); return 2; }
        std::string label, path;
        while (bf >> label >> path) {
            PlotTable after;
            if (!readPlots(path, after)) { std::fprintf(stderr, "lt_producer_run: bad plots file %s\n", path.c_str()); return 2; }
            Producer Q;
            st = Producer::create(L, fr, pt, tap, seed, Q);
            if (st.ok() && landWater) st = Q.enableLandWater(coast, mats, mapSeed);
            if (!st) { std::fprintf(stderr, "lt_producer_run: create: %s\n", st.str().c_str()); return 1; }
            ChangeResult r;
            Producer::Options opt; opt.diagnosticIgnoreClean = diag; opt.protectWater = protW; opt.protectMarsh = protM;
            opt.protectRivers = protR; opt.protectWonders = protNW;
            opt.riverKeepDisc = riverDisc; opt.riverEdgesFromAnyFlag = riverAny; opt.landRegion = landRegion;
            const std::vector<uint16_t> beforeH = Q.currentHeights();
            st = Q.change(after, r, opt);
            uint64_t visible = 0;   // samples whose height the live window changed (0: live but nothing to see)
            for (const WindowRect& wr : r.window)
                for (int yy = 0; yy < wr.h; ++yy) for (int xx = 0; xx < wr.w; ++xx) {
                    const int X = (wr.x0 + xx) % fr.samplesX();
                    if (wr.heights[size_t(yy) * wr.w + xx] != beforeH[size_t(wr.y0 + yy) * fr.samplesX() + X]) ++visible;
                }
            if (!st) { std::printf("{\"label\":\"%s\",\"error\":\"%s\"}\n", esc(label).c_str(), esc(st.str()).c_str()); continue; }
            std::printf("{\"label\":\"%s\",\"live\":%s,\"reason\":\"%s\",\"reasonText\":\"%s\",\"changed\":%zu,\"removed\":%u,\"added\":%u,\"samplesFull\":%llu,\"samplesRing\":%llu,\"samplesOutside\":%llu,\"visible\":%llu,\"landWater\":%s,\"linesRedrawn\":%u,\"linesBeyond\":%u,\"linesReloadMoves\":%u,\"ms\":%.1f}\n",
                        esc(label).c_str(), r.live ? "true" : "false", waitReasonName(r.reason), esc(r.reasonText).c_str(),
                        r.changedPlots.size(), r.removedPlacements, r.addedPlacements, (unsigned long long)r.samplesFull,
                        (unsigned long long)r.samplesRing, (unsigned long long)r.samplesOutside, (unsigned long long)visible,
                        r.landWater ? "true" : "false", r.coastLinesRedrawn, r.coastLinesBeyondWindow, r.coastLinesReloadMoves, r.ms);
            std::fflush(stdout);
        }
        return 0;
    }
    // --change and --result take comma-separated lists: the changes are applied in order to the same producer
    std::vector<std::string> changes, results;
    auto split = [](const std::string& s, std::vector<std::string>& v) {
        size_t a = 0; while (a <= s.size()) { size_t b = s.find(',', a); if (b == std::string::npos) b = s.size();
            if (b > a) v.push_back(s.substr(a, b - a)); a = b + 1; } };
    split(change, changes); split(result, results);
    if (changes.size() != results.size()) { std::fprintf(stderr, "lt_producer_run: as many --result prefixes as --change files\n"); return 2; }
    for (size_t ci = 0; ci < changes.size(); ++ci) {
        const std::string& rp = results[ci];
        PlotTable after;
        if (!readPlots(changes[ci], after)) { std::fprintf(stderr, "lt_producer_run: bad change plots file\n"); return 2; }
        ChangeResult r;
        Producer::Options opt; opt.diagnosticIgnoreClean = diag; opt.protectWater = protW; opt.protectMarsh = protM;
        opt.protectRivers = protR; opt.protectWonders = protNW;
        opt.riverKeepDisc = riverDisc; opt.riverEdgesFromAnyFlag = riverAny; opt.landRegion = landRegion;
        Producer::SampleDiagnostics dg;
        if (!diagOut.empty()) opt.diagnostics = &dg;
        opt.diagnosticRingSamples = ringArg; opt.diagnosticRiverKeepOnWater = keepOnWater;
        st = P.change(after, r, opt);
        if (!st) { std::fprintf(stderr, "lt_producer_run: change: %s\n", st.str().c_str()); return 1; }
        if (!diagOut.empty()) {
            // PREFIX.<ci>.json (the window and the river centres), .wtfull.f32, .wt.f32, .why.u8, .moved.u8, .rb.u8, .ra.u8
            const std::string dp = diagOut + "." + std::to_string(ci);
            std::ostringstream dj;
            dj << "{\"x0\":" << dg.x0 << ",\"y0\":" << dg.y0 << ",\"w\":" << dg.w << ",\"h\":" << dg.h << ",\"ring\":"
               << (ringArg > 0 ? ringArg : kRingSamples) << ",\"riverCentres\":[";
            for (size_t i = 0; i < dg.riverCentres.size(); ++i)
                dj << (i ? "," : "") << "[" << dg.riverCentres[i].first << "," << dg.riverCentres[i].second << "]";
            dj << "]}";
            std::ofstream(dp + ".json") << dj.str() << "\n";
            auto wr = [&](const char* ext, const void* p, size_t n) { if (n && !writeFile(dp + ext, p, n)) std::fprintf(stderr, "lt_producer_run: cannot write %s\n", ext); };
            wr(".wtfull.f32", dg.weightFull.data(), dg.weightFull.size() * 4);
            wr(".wt.f32", dg.weight.data(), dg.weight.size() * 4);
            wr(".why.u8", dg.why.data(), dg.why.size());
            wr(".moved.u8", dg.moved.data(), dg.moved.size());
            wr(".rb.u8", dg.regionBefore.data(), dg.regionBefore.size());
            wr(".ra.u8", dg.regionAfter.data(), dg.regionAfter.size());
            wr(".rf.f32", dg.riverFactor.data(), dg.riverFactor.size() * 4);   // 0717: the river band's factor
        }
        std::ostringstream js;
        js << "{\"step\":\"change\",\"live\":" << (r.live ? "true" : "false") << ",\"reason\":\"" << waitReasonName(r.reason)
           << "\",\"reasonText\":\"" << esc(r.reasonText) << "\",\"changedPlots\":[";
        for (size_t i = 0; i < r.changedPlots.size(); ++i) js << (i ? "," : "") << "[" << r.changedPlots[i].first << "," << r.changedPlots[i].second << "]";
        js << "],\"removed\":" << r.removedPlacements << ",\"added\":" << r.addedPlacements << ",\"samplesD\":" << r.samplesD
           << ",\"samplesFull\":" << r.samplesFull << ",\"samplesRing\":" << r.samplesRing << ",\"samplesOutside\":" << r.samplesOutside
           << ",\"landWater\":" << (r.landWater ? "true" : "false") << ",\"linesRedrawn\":" << r.coastLinesRedrawn
           << ",\"linesBeyond\":" << r.coastLinesBeyondWindow << ",\"linesReloadMoves\":" << r.coastLinesReloadMoves
           << ",\"wonderKept\":" << r.samplesWonderKept << ",\"landRegion\":" << (r.landRegion ? "true" : "false")
           << ",\"riverCore\":" << r.samplesRiverCore << ",\"riverBand\":" << r.samplesRiverBand << ",\"ms\":" << r.ms << ",\"window\":[";
        for (size_t i = 0; i < r.window.size(); ++i) {
            const WindowRect& w = r.window[i];
            js << (i ? "," : "") << "[" << w.x0 << "," << w.y0 << "," << w.w << "," << w.h << "]";
            if (!writeFile(rp + "." + std::to_string(i) + ".h16", w.heights.data(), w.heights.size() * 2) ||
                !writeFile(rp + "." + std::to_string(i) + ".m8", w.materials.data(), w.materials.size())) return 1;
        }
        js << "]}";
        std::printf("%s\n", js.str().c_str());
        std::ofstream(rp + ".json") << js.str() << "\n";
        if (!fieldAfter.empty()) {
            const auto& ch = P.currentHeights(); const auto& cm = P.currentMaterials();
            const std::string fp = fieldAfter + "." + std::to_string(ci);
            if (!writeFile(fp + ".h16", ch.data(), ch.size() * 2) || !writeFile(fp + ".m8", cm.data(), cm.size())) return 1;
        }
    }
    return 0;
}
