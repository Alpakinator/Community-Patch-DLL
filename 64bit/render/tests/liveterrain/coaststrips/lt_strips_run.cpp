// lt_strips_run - applies the coast strips to a recorded field (ticket 4911; live-terrain.md 8.15). Offline only.
//
//   lt_strips_run --w SAMPLES_X --h SAMPLES_Y --wrap 0|1 --h16 BASE.h16 --m8 BASE.m8 --curves CURVES.bin
//                 --styles STYLES.bin --game-dir DIR --out PREFIX [--window X0,Y0,W,H] [--only I,J,...]
//
// BASE.h16 / .m8: the dense field the strips are drawn over (u16 / u8, x fastest). CURVES.bin: u32 coasts, then per
// coast u32 L and the five lines of L points each, 16 bytes a point (f32 x, y, t; u8 styleA, styleB; 2 bytes pad).
// STYLES.bin: 13 records of {u8 present; 3 pad; f32 weight[5]; f32 height[5]; i32 band[4]} (made at run time from the
// install's CoastStyles.xml by render/evidence/0615/4911/prep.py). The TerrainDef rows come from DIR through c5r::gamefs.
// Writes PREFIX.h16 / .m8 (the field after the strips, the window only when --window), PREFIX.cover.u8 (1 where a
// band 1 or 2 triangle covers the sample: the samples whose height does not depend on the base) and one JSON line.
// --only: the coasts to draw (indices into CURVES.bin, the EXE's order kept). Exit 0 ok, 2 bad input, 1 error.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "c5r/bake/bake_types.h"
#include "c5r/bake/layers.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/liveterrain/coaststrips.h"

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
template <class T> bool get(const std::vector<uint8_t>& b, size_t& o, T& v) {
    if (o + sizeof(T) > b.size()) return false;
    std::memcpy(&v, &b[o], sizeof(T));
    o += sizeof(T);
    return true;
}
bool readCurves(const std::vector<uint8_t>& b, std::vector<StripLines>& out) {
    size_t o = 0;
    uint32_t n = 0;
    if (!get(b, o, n) || n > 1000000) return false;
    out.resize(n);
    for (uint32_t c = 0; c < n; ++c) {
        uint32_t L = 0;
        if (!get(b, o, L) || L > 10000000) return false;
        for (int i = 0; i < 5; ++i) {
            out[c].line[i].resize(L);
            for (uint32_t j = 0; j < L; ++j) {
                StripPoint& p = out[c].line[i][j];
                uint16_t pad = 0;
                if (!get(b, o, p.x) || !get(b, o, p.y) || !get(b, o, p.t) || !get(b, o, p.styleA) ||
                    !get(b, o, p.styleB) || !get(b, o, pad))
                    return false;
            }
        }
    }
    return o == b.size();
}
bool readStyles(const std::vector<uint8_t>& b, StripStyles& s) {
    if (b.size() != size_t(kStripStyleKinds) * 60) return false;
    size_t o = 0;
    for (int k = 0; k < kStripStyleKinds; ++k) {
        uint8_t present = 0, pad[3];
        if (!get(b, o, present) || !get(b, o, pad)) return false;
        s.present[k] = present != 0;
        for (int i = 0; i < 5; ++i) if (!get(b, o, s.kind[k].weight[i])) return false;
        for (int i = 0; i < 5; ++i) if (!get(b, o, s.kind[k].height[i])) return false;
        for (int i = 0; i < 4; ++i) { int32_t v; if (!get(b, o, v)) return false; s.kind[k].bandTerrain[i] = v; }
    }
    return true;
}
std::vector<uint32_t> parseList(const std::string& s) {
    std::vector<uint32_t> v;
    std::stringstream ss(s);
    std::string t;
    while (std::getline(ss, t, ',')) if (!t.empty()) v.push_back(uint32_t(std::stoul(t)));
    return v;
}
}

int main(int argc, char** argv) {
    std::string h16, m8, curves, styles, gameDir, out, window, only;
    uint32_t W = 0, H = 0;
    bool wrap = false;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string k = argv[i], v = argv[i + 1];
        if (k == "--w") W = uint32_t(std::stoul(v)); else if (k == "--h") H = uint32_t(std::stoul(v));
        else if (k == "--wrap") wrap = v == "1";
        else if (k == "--h16") h16 = v; else if (k == "--m8") m8 = v; else if (k == "--curves") curves = v;
        else if (k == "--styles") styles = v; else if (k == "--game-dir") gameDir = v; else if (k == "--out") out = v;
        else if (k == "--window") window = v; else if (k == "--only") only = v;
        else { std::fprintf(stderr, "lt_strips_run: unknown option %s\n", k.c_str()); return 2; }
    }
    if (!W || !H || h16.empty() || m8.empty() || curves.empty() || styles.empty() || gameDir.empty() || out.empty()) {
        std::fprintf(stderr, "lt_strips_run: --w --h --h16 --m8 --curves --styles --game-dir --out required\n");
        return 2;
    }
    std::vector<uint8_t> hb, mb, cb, sb;
    if (!readFile(h16, hb) || !readFile(m8, mb) || !readFile(curves, cb) || !readFile(styles, sb)) {
        std::fprintf(stderr, "lt_strips_run: an input file cannot be read\n");
        return 2;
    }
    if (hb.size() != size_t(W) * H * 2 || mb.size() != size_t(W) * H) {
        std::fprintf(stderr, "lt_strips_run: field size is not %ux%u\n", W, H);
        return 2;
    }
    std::vector<StripLines> lines;
    StripStyles st;
    if (!readCurves(cb, lines) || !readStyles(sb, st)) {
        std::fprintf(stderr, "lt_strips_run: curves or styles file malformed\n");
        return 2;
    }
    gamefs::GameFiles fs;
    Status s = fs.open(std::wstring(gameDir.begin(), gameDir.end()));
    bake::LayerTables lt;
    if (s.ok()) s = bake::loadTerrainDef(fs, lt);
    StripMaterials mats;
    if (s.ok()) s = StripMaterials::fromLayerTables(lt, mats);
    if (!s.ok()) { std::fprintf(stderr, "lt_strips_run: TerrainDef: %s\n", s.str().c_str()); return 1; }

    const auto t0 = std::chrono::steady_clock::now();
    std::vector<StripMesh> meshes(lines.size());
    for (size_t c = 0; c < lines.size(); ++c) {
        s = buildStripMesh(lines[c], st, mats, meshes[c]);
        if (!s.ok()) { std::fprintf(stderr, "lt_strips_run: coast %zu: %s\n", c, s.str().c_str()); return 1; }
    }
    if (!only.empty()) {
        std::vector<StripMesh> sel;
        for (uint32_t i : parseList(only)) {
            if (i >= meshes.size()) { std::fprintf(stderr, "lt_strips_run: --only %u out of range\n", i); return 2; }
            sel.push_back(meshes[i]);
        }
        meshes.swap(sel);
    }
    const auto t1 = std::chrono::steady_clock::now();

    uint32_t x0 = 0, y0 = 0, ww = W, wh = H;
    if (!window.empty()) {
        const std::vector<uint32_t> v = parseList(window);
        if (v.size() != 4) { std::fprintf(stderr, "lt_strips_run: --window X0,Y0,W,H\n"); return 2; }
        x0 = v[0]; y0 = v[1]; ww = v[2]; wh = v[3];
    }
    if (uint64_t(x0) + ww > W || uint64_t(y0) + wh > H || !ww || !wh) {
        std::fprintf(stderr, "lt_strips_run: window outside the field\n");
        return 2;
    }
    std::vector<uint16_t> fh(size_t(ww) * wh);
    std::vector<uint8_t> fm(size_t(ww) * wh), cover(size_t(ww) * wh, 0xFE);
    for (uint32_t y = 0; y < wh; ++y) {
        std::memcpy(&fh[size_t(y) * ww], &hb[(size_t(y0 + y) * W + x0) * 2], size_t(ww) * 2);
        std::memcpy(&fm[size_t(y) * ww], &mb[size_t(y0 + y) * W + x0], ww);
    }
    StripField f;
    f.samplesX = W; f.samplesY = H; f.wrapX = wrap; f.x0 = x0; f.y0 = y0; f.w = ww; f.h = wh;
    f.heights = fh.data(); f.materials = fm.data();
    StripStats stats;
    s = applyStrips(meshes, mats, f, &stats);
    if (!s.ok()) { std::fprintf(stderr, "lt_strips_run: %s\n", s.str().c_str()); return 1; }
    const auto t2 = std::chrono::steady_clock::now();

    // the cover mask: the same meshes with only bands 1 and 2 carrying a marker material and no height change
    std::vector<StripMesh> marks = meshes;
    for (StripMesh& m : marks)
        for (size_t v = 0; v < m.vertices.size(); ++v) {
            const bool band12 = ((v % 8) / 2 == 1) || ((v % 8) / 2 == 2);
            m.vertices[v].material = band12 ? 1 : kNoMaterial;
            m.vertices[v].w = 0.0f;
        }
    std::vector<uint16_t> ch(size_t(ww) * wh, 0);
    StripField g = f;
    g.heights = ch.data(); g.materials = cover.data();
    StripMaterials none;   // no rows: no mountain swap in the mask run
    none.rowId.push_back(0xFFFFFFFFu);
    s = applyStrips(marks, none, g, nullptr);
    if (!s.ok()) { std::fprintf(stderr, "lt_strips_run: mask: %s\n", s.str().c_str()); return 1; }
    for (uint8_t& c : cover) c = (c == 1) ? 1 : 0;

    if (!writeFile(out + ".h16", fh.data(), fh.size() * 2) || !writeFile(out + ".m8", fm.data(), fm.size()) ||
        !writeFile(out + ".cover.u8", cover.data(), cover.size())) {
        std::fprintf(stderr, "lt_strips_run: cannot write %s.*\n", out.c_str());
        return 1;
    }
    uint32_t segs = 0, tris = 0;
    for (const StripMesh& m : meshes) { segs += m.segments(); tris += m.triangles(); }
    const double msBuild = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double msRaster = std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::printf("{\"coasts\": %zu, \"segments\": %u, \"triangles\": %u, \"touching\": %u, \"drawn\": %llu, "
                "\"writes\": %llu, \"refused\": %llu, \"window\": [%u, %u, %u, %u], \"ms_build\": %.1f, \"ms_raster\": %.1f}\n",
                meshes.size(), segs, tris, stats.meshesTouching, (unsigned long long)stats.trianglesDrawn,
                (unsigned long long)stats.samplesWritten, (unsigned long long)stats.trianglesRefused, x0, y0, ww, wh, msBuild,
                msRaster);
    return 0;
}
