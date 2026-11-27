// lt_region_run - builds the coastline curves and the region bytes on recorded inputs and checks them against a tap's
// material layer (ticket 4910; live-terrain.md 8.10.1, 8.10.2). Offline only.
//
//   lt_region_run --block BLOCK.bin --plots PLOTS.txt --styles DIR --seed N [--m8 M.raw] [--out REGION.u8]
//                 [--curves CURVES.txt] [--after AFTER.txt]
//
// BLOCK.bin and PLOTS.txt as lt_producer_run (render/evidence/0465/4903/prep.py makes them). DIR holds coaststyles.xml
// and the CoastCharacterSpline_*.txt files (from the install's TerrainModels.fpk; lower-case names accepted). N is
// mapRandomSeed (decimal or 0x hex). With --m8 (the tap's u8 material layer, x fastest) it prints the material-37
// fingerprint: on samples of land hexes, the share where (material == 37) equals (region != 0), overall and on the
// samples of land hexes next to water, and the same shares for the per-hex class as a baseline. --after: a second
// plot table; prints which lines differ in point count and from which line on the jitter moves. Prints one JSON
// object. Exit 0 on success, 2 on bad input, 1 on an error.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "c5r/liveterrain/coastlines.h"

using namespace c5r;
using namespace c5r::liveterrain;

namespace {
bool readFile(const std::string& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
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
std::string lower(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a'); return s; }

uint8_t cls(uint32_t f) { if (f & 0xF3EF9FFFu) return 0; if (f & 0x0C102000u) return 1; return 2; }

// the pattern-grid cell of a sample: the hex whose centre is nearest (cube rounding through M_h2w's 2 x 2 part)
void sampleCell(const MapFrame& fr, int c, int r, int& gx, int& gy) {
    const double a = fr.h2w[0], b = fr.h2w[1], cc = fr.h2w[4], d = fr.h2w[5];
    const double det = a * d - b * cc;
    const double px = c + 0.5 - fr.h2w[12], py = r + 0.5 - fr.h2w[13];
    const double qf = (d * px - cc * py) / det, rf = (-b * px + a * py) / det, sf = -qf - rf;
    double q = std::nearbyint(qf), rr = std::nearbyint(rf), s = std::nearbyint(sf);
    const double dq = std::fabs(q - qf), dr = std::fabs(rr - rf), ds = std::fabs(s - sf);
    if (dq > dr && dq > ds) q = -rr - s; else if (dr > ds) rr = -q - s;
    const int64_t qi = int64_t(q), ri = int64_t(rr), si = -qi - ri;
    const int64_t v0 = qi * fr.G[0] + ri * fr.G[4] + si * fr.G[8] + fr.G[12];
    const int64_t v1 = qi * fr.G[1] + ri * fr.G[5] + si * fr.G[9] + fr.G[13];
    gy = int(v1);
    int64_t x = v0 + (v1 >> 1);
    const int gw = fr.gridW();
    if (fr.wrapX) { x %= gw; if (x < 0) x += gw; }
    gx = int(x);
}
}

int main(int argc, char** argv) {
    std::string block, plotsP, styles, seedArg, m8, outP, curvesP, afterP;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string k = argv[i], v = argv[i + 1];
        if (k == "--block") block = v; else if (k == "--plots") plotsP = v; else if (k == "--styles") styles = v;
        else if (k == "--seed") seedArg = v; else if (k == "--m8") m8 = v; else if (k == "--out") outP = v;
        else if (k == "--curves") curvesP = v; else if (k == "--after") afterP = v;
        else { std::fprintf(stderr, "lt_region_run: unknown option %s\n", k.c_str()); return 2; }
    }
    if (block.empty() || plotsP.empty() || styles.empty() || seedArg.empty()) {
        std::fprintf(stderr, "lt_region_run: --block --plots --styles --seed required\n"); return 2;
    }
    std::string blk;
    if (!readFile(block, blk)) { std::fprintf(stderr, "lt_region_run: cannot read %s\n", block.c_str()); return 2; }
    MapFrame fr;
    Status st = MapFrame::fromConstantsBlock(reinterpret_cast<const uint8_t*>(blk.data()), blk.size(), fr);
    if (!st) { std::fprintf(stderr, "lt_region_run: %s\n", st.str().c_str()); return 2; }
    PlotTable plots;
    if (!readPlots(plotsP, plots)) { std::fprintf(stderr, "lt_region_run: bad plot table\n"); return 2; }
    std::string xml;
    if (!readFile(styles + "/coaststyles.xml", xml) && !readFile(styles + "/CoastStyles.xml", xml)) {
        std::fprintf(stderr, "lt_region_run: no coaststyles.xml in %s\n", styles.c_str()); return 2;
    }
    CoastStyles cs;
    st = CoastStyles::parse(xml, [&](const std::string& name, std::string& text) {
        return readFile(styles + "/" + name, text) || readFile(styles + "/" + lower(name), text);
    }, cs);
    if (!st) { std::fprintf(stderr, "lt_region_run: %s\n", st.str().c_str()); return 2; }
    const uint32_t seed = uint32_t(std::strtoull(seedArg.c_str(), nullptr, 0));
    CoastCurves cv;
    st = buildCoastCurves(fr, plots, cs, seed, cv);
    if (!st) { std::fprintf(stderr, "lt_region_run: %s\n", st.str().c_str()); return 1; }
    std::vector<uint8_t> reg;
    st = regionField(fr, plots, cv, reg);
    if (!st) { std::fprintf(stderr, "lt_region_run: %s\n", st.str().c_str()); return 1; }
    uint64_t type0 = 0, closed = 0;
    for (const CoastLine& L : cv.lines) { type0 += L.type == 0; closed += L.closed; }
    std::printf("{\"lines\": %zu, \"type0\": %llu, \"closed\": %llu, \"draws\": %llu, \"points\": %zu, \"boundary\": %zu, \"fill\": %zu",
                cv.lines.size(), (unsigned long long)type0, (unsigned long long)closed, (unsigned long long)cv.randDraws,
                cv.points.size(), cv.boundary.size(), cv.fill.size());
    std::printf(", \"degenerate\": %u", cv.degenerate);   // MC #9538 item 1 (live-terrain.md 8.10.3)
    if (!outP.empty()) {
        std::ofstream o(outP, std::ios::binary);
        o.write(reinterpret_cast<const char*>(reg.data()), std::streamsize(reg.size()));
        if (!o) { std::fprintf(stderr, "lt_region_run: cannot write %s\n", outP.c_str()); return 1; }
    }
    if (!curvesP.empty()) {
        std::ofstream o(curvesP);
        for (size_t i = 0; i < cv.lines.size(); ++i) {
            const CoastLine& L = cv.lines[i];
            o << "line " << i << " type " << L.type << " closed " << L.closed << " start " << L.startX << "," << L.startY << "," << L.startSlot
              << " points " << L.points << " draws " << L.draws << "\n";
        }
        for (const CoastRange& R : cv.boundary) o << "B " << R.start << " " << R.end << " " << R.value << "\n";
        for (const CoastRange& R : cv.fill) o << "F " << R.start << " " << R.end << " " << R.value << "\n";
        for (const CoastPoint& p : cv.points) o << p.x << " " << p.y << "\n";
    }
    if (!m8.empty()) {
        std::string mat;
        if (!readFile(m8, mat) || mat.size() != size_t(fr.samplesX()) * fr.samplesY()) {
            std::fprintf(stderr, "\nlt_region_run: material layer size\n"); return 2;
        }
        // per-cell class from the plot table
        const int gw = fr.gridW(), gh = fr.gridH();
        std::vector<uint8_t> cc(size_t(gw) * gh, 1);
        for (int y = 0; y < plots.h; ++y) for (int x = 0; x < plots.w; ++x)
            cc[size_t(y + 2) * gw + x] = cls(heightfield::patternCellFlags(plots.plots[size_t(y) * plots.w + x].base));
        auto cellAt = [&](int x, int y) -> int {
            if (y < 0 || y >= gh) return -1;
            if (fr.wrapX) { x %= gw; if (x < 0) x += gw; } else if (x < 0 || x >= gw) return -1;
            return cc[size_t(y) * gw + x];
        };
        std::vector<uint8_t> nearWater(cc.size(), 0);
        for (int y = 0; y < gh; ++y) for (int x = 0; x < gw; ++x) {
            if (cc[size_t(y) * gw + x] != 0) continue;
            const int q = x - (y >> 1);
            static const int D[6][2] = {{0, 1}, {-1, 0}, {1, -1}, {0, -1}, {1, 0}, {-1, 1}};
            for (int k = 0; k < 6; ++k) {
                const int nq = q + D[k][0], nr = y + D[k][1];
                const int c = cellAt(nq + (nr >> 1), nr);
                if (c > 0) nearWater[size_t(y) * gw + x] = 1;
            }
        }
        // hexes with a river edge, or next to one (the river carve also writes 37 on land, not a region effect)
        std::vector<uint8_t> river(cc.size(), 0);
        for (int y = 0; y < plots.h; ++y) for (int x = 0; x < plots.w; ++x) {
            if (!plots.plots[size_t(y) * plots.w + x].riverEdges) continue;
            const int cy = y + 2, q = x - (cy >> 1);
            river[size_t(cy) * gw + x] = 1;
            static const int D[6][2] = {{0, 1}, {-1, 0}, {1, -1}, {0, -1}, {1, 0}, {-1, 1}};
            for (int k = 0; k < 6; ++k) {
                const int nq = q + D[k][0], nr = cy + D[k][1];
                int nx = nq + (nr >> 1);
                if (nr < 0 || nr >= gh) continue;
                if (fr.wrapX) { nx %= gw; if (nx < 0) nx += gw; } else if (nx < 0 || nx >= gw) continue;
                river[size_t(nr) * gw + nx] = 1;
            }
        }
        uint64_t land = 0, land37 = 0, agree = 0, base = 0, coastS = 0, coastAgree = 0, coastBase = 0, m37hit = 0, m37coast = 0, m37coastHit = 0;
        uint64_t nrS = 0, nr37 = 0, nr37hit = 0, wetLandMat = 0, nrWetLandMat = 0, nrLandMat = 0;
        for (int r = 0; r < fr.samplesY(); ++r) for (int c = 0; c < fr.samplesX(); ++c) {
            int gx, gy; sampleCell(fr, c, r, gx, gy);
            if (gy < 2 || gy >= gh - 2) continue;
            if (cc[size_t(gy) * gw + gx] != 0) continue;
            const size_t k = size_t(r) * fr.samplesX() + c;
            const uint8_t mv = uint8_t(mat[k]);
            const bool m37 = mv == 37, wet = reg[k] != 0, landMat = mv != 37 && mv != 3;
            ++land; land37 += m37; agree += (m37 == wet); base += !m37; m37hit += (m37 && wet);
            wetLandMat += (wet && landMat);
            if (nearWater[size_t(gy) * gw + gx]) {
                ++coastS; coastAgree += (m37 == wet); coastBase += !m37; m37coast += m37; m37coastHit += (m37 && wet);
                if (!river[size_t(gy) * gw + gx]) { ++nrS; nr37 += m37; nr37hit += (m37 && wet); nrWetLandMat += (wet && landMat); nrLandMat += landMat; }
            }
        }
        std::printf(", \"coast_noriver_samples\": %llu, \"coast_noriver_m37\": %llu, \"coast_noriver_m37_reproduced\": %llu, "
                    "\"coast_noriver_landmat\": %llu, \"coast_noriver_landmat_wet\": %llu, \"landmat_wet\": %llu",
                    (unsigned long long)nrS, (unsigned long long)nr37, (unsigned long long)nr37hit,
                    (unsigned long long)nrLandMat, (unsigned long long)nrWetLandMat, (unsigned long long)wetLandMat);
        std::printf(", \"land_samples\": %llu, \"land_m37\": %llu, \"m37_reproduced\": %llu, \"agree\": %llu, \"agree_share\": %.5f, \"baseline_share\": %.5f, "
                    "\"coast_land_samples\": %llu, \"coast_m37\": %llu, \"coast_m37_reproduced\": %llu, \"coast_agree\": %llu, \"coast_agree_share\": %.5f, \"coast_baseline_share\": %.5f",
                    (unsigned long long)land, (unsigned long long)land37, (unsigned long long)m37hit, (unsigned long long)agree,
                    land ? double(agree) / land : 0.0, land ? double(base) / land : 0.0, (unsigned long long)coastS,
                    (unsigned long long)m37coast, (unsigned long long)m37coastHit, (unsigned long long)coastAgree,
                    coastS ? double(coastAgree) / coastS : 0.0, coastS ? double(coastBase) / coastS : 0.0);
    }
    if (!afterP.empty()) {
        PlotTable after;
        if (!readPlots(afterP, after)) { std::fprintf(stderr, "\nlt_region_run: bad --after plot table\n"); return 2; }
        CoastCurves cv2;
        st = buildCoastCurves(fr, after, cs, seed, cv2);
        if (!st) { std::fprintf(stderr, "\nlt_region_run: %s\n", st.str().c_str()); return 1; }
        size_t firstDiff = cv.lines.size() < cv2.lines.size() ? cv.lines.size() : cv2.lines.size();
        size_t changed = 0;
        for (size_t i = 0; i < std::min(cv.lines.size(), cv2.lines.size()); ++i) {
            const CoastLine &a = cv.lines[i], &b = cv2.lines[i];
            const bool same = a.points == b.points && a.startX == b.startX && a.startY == b.startY && a.startSlot == b.startSlot;
            if (!same) { ++changed; if (i < firstDiff) firstDiff = i; }
        }
        std::printf(", \"after_lines\": %zu, \"after_draws\": %llu, \"lines_differing\": %zu, \"first_differing_line\": %zu, \"draws_same\": %s",
                    cv2.lines.size(), (unsigned long long)cv2.randDraws, changed, firstDiff, cv2.randDraws == cv.randDraws ? "true" : "false");
    }
    std::printf("}\n");
    return 0;
}
