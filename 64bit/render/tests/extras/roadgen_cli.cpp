// roadgen_cli.cpp - the road generator's command line (epic 0454 X3, ticket 4933): lays out the roads of a plot table and
// writes them into a copy of a scene bundle's decal tap image (the offline candidate pictures), with the cost figures.
// A test-side tool: server.exe does not use it, and nothing it writes is committed (the images hold the player's EXE's
// records: nothing taken from the game's files is committed). Written 2026-11-19 for this repository (GPL-3.0-or-later).
//
//   c5r_roadgen --plots PLOTS.bin --style straight|spline|jitter|line [--dtap IN.dtap --out OUT.dtap [--keep-tap-routes]]
//               [--lines LINES.bin] [--stats STATS.json] [--repeat N] [--change X,Y,BAND]... [--sweep N]
//               [--connect tidy|all] [--junction through|cross|ring|rosette] [--junction-min N] [--ring R]
// --connect all (ticket 4940): every pair of neighbouring route plots joined (no ring rule; empty faces are then expected
// and not a failure). --junction: the centre of a plot with --junction-min (default 4) or more edges (roads.h Junction).
// --jitter N: style (c)'s pass-point offset, and in style line lineJitter (ticket 4940 part 1b; not given: 0 for line).
//
// PLOTS.bin (render/evidence/0454/4933/plotsbin.py): "C5RP", u32 version 1, u32 width, u32 height, u32 wrapX, then
// width * height bytes of Band, then width * height bytes of the city mark.
// LINES.bin: the centre lines, "C5RL", u32 decals, then per decal u32 owner, u32 points, points x float2 (iso; NaN pairs
// end pieces).
// --repeat N: the production timed N times (median and range in STATS). --change X,Y,BAND: after the first production,
// the plot's band set to BAND and produced again: the plots and rectangles that changed, and the time (repeatable flag;
// each change on the original map).
// Exit 0 OK, 1 a failed step (its line on stderr), 2 usage.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "c5r/extras/roads.h"

using namespace c5r;
using namespace c5r::extras::roads;

namespace {

bool readFile(const std::string& p, std::vector<uint8_t>& out)
{
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? static_cast<size_t>(n) : 0);
    const bool ok = n >= 0 && std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

bool writeFile(const std::string& p, const std::vector<uint8_t>& b)
{
    const std::string tmp = p + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(b.data(), 1, b.size(), f) == b.size();
    if (std::fclose(f) != 0 || !ok) return false;
    std::remove(p.c_str());
    return std::rename(tmp.c_str(), p.c_str()) == 0;
}

template <class T>
T rd(const std::vector<uint8_t>& b, size_t off)
{
    T v;
    std::memcpy(&v, b.data() + off, sizeof v);
    return v;
}
template <class T>
void wr(std::vector<uint8_t>& b, size_t off, T v)
{
    std::memcpy(b.data() + off, &v, sizeof v);
}

bool loadPlots(const std::string& p, MapIn& m, std::string& why)
{
    std::vector<uint8_t> b;
    if (!readFile(p, b)) return why = "cannot read " + p, false;
    if (b.size() < 20 || std::memcmp(b.data(), "C5RP", 4) || rd<uint32_t>(b, 4) != 1) return why = p + ": not a C5RP v1 file", false;
    m.width = rd<uint32_t>(b, 8);
    m.height = rd<uint32_t>(b, 12);
    m.wrapX = rd<uint32_t>(b, 16) != 0;
    const size_t n = static_cast<size_t>(m.width) * m.height;
    if (!m.width || !m.height || m.width > kMaxPlotsPerAxis || m.height > kMaxPlotsPerAxis || b.size() != 20 + 2 * n)
        return why = p + ": size does not match its header", false;
    m.plots.resize(n);
    for (size_t i = 0; i < n; ++i) {
        m.plots[i].band = b[20 + i];
        m.plots[i].city = b[20 + n + i];
    }
    return true;
}

// ---- the decal tap image (shell/common/shell_decal_tap.h, version 1; terrain-decals.md 10.4) ----
const uint32_t kMagic = 0x54443543u;
struct Image {
    decal::DecalSet set;
    std::vector<uint32_t> handles;   // per name
    uint32_t generation = 0, loadSeq = 0, changedSince = 0;
};

bool parseImage(const std::vector<uint8_t>& b, Image& img, std::string& why)
{
    if (b.size() < 112 || rd<uint32_t>(b, 0) != kMagic || rd<uint32_t>(b, 4) != 1 || rd<uint32_t>(b, 8) != 112 ||
        rd<uint32_t>(b, 12) != 48)
        return why = "not a C5DT v1 image", false;
    const uint32_t nd = rd<uint32_t>(b, 24), nn = rd<uint32_t>(b, 28), nv = rd<uint32_t>(b, 32);
    const uint32_t doff = rd<uint32_t>(b, 40), noff = rd<uint32_t>(b, 44), voff = rd<uint32_t>(b, 48);
    if (rd<uint64_t>(b, 72) != b.size()) return why = "imageBytes is not the file's size", false;
    if (static_cast<uint64_t>(doff) + 48ull * nd > b.size() || static_cast<uint64_t>(noff) + 200ull * nn > b.size() ||
        static_cast<uint64_t>(voff) + 8ull * nv > b.size())
        return why = "a block past the end", false;
    decal::DecalSet& s = img.set;
    img.generation = rd<uint32_t>(b, 16);
    img.loadSeq = rd<uint32_t>(b, 20);
    img.changedSince = rd<uint32_t>(b, 80);
    s.generation = img.generation;
    s.loadSeq = img.loadSeq;
    for (int k = 0; k < 4; ++k) s.worldRect[k] = rd<float>(b, 56 + 4 * k);
    for (uint32_t i = 0; i < nn; ++i) {
        img.handles.push_back(rd<uint32_t>(b, noff + 200 * i));
        const char* p = reinterpret_cast<const char*>(b.data() + noff + 200 * i + 8);
        s.textures.push_back(std::string(p, strnlen(p, 192)));
    }
    s.vertices.resize(nv);
    std::memcpy(s.vertices.data(), b.data() + voff, 8ull * nv);
    for (uint32_t i = 0; i < nd; ++i) {
        const size_t o = doff + 48ull * i;
        decal::Decal d;
        for (int k = 0; k < 4; ++k) d.rect[k] = rd<float>(b, o + 4 * k);
        d.priority = rd<float>(b, o + 16);
        d.pass = rd<uint32_t>(b, o + 20) & 1u;
        for (int k = 0; k < 3; ++k) d.texture[k] = rd<uint32_t>(b, o + 24 + 4 * k);
        d.firstVertex = rd<uint32_t>(b, o + 36);
        d.vertexCount = 3 * rd<uint32_t>(b, o + 40);
        d.listOrder = rd<uint32_t>(b, o + 44);
        if (static_cast<uint64_t>(d.firstVertex) + d.vertexCount > nv) return why = "a record's vertices past the block", false;
        d.hash = contentHash(d, s.textures, s.vertices);
        s.decals.push_back(d);
    }
    const uint32_t aoff = rd<uint32_t>(b, 88), ax = rd<uint32_t>(b, 92), ay = rd<uint32_t>(b, 96);
    if (aoff && ax && ay) {
        const uint64_t bits = static_cast<uint64_t>(ax) * ay;
        if (aoff + (bits + 7) / 8 > b.size()) return why = "the area block past the end", false;
        s.areaTilesX = ax;
        s.areaTilesY = ay;
        s.areaChanged.resize(static_cast<size_t>(bits));
        for (uint64_t i = 0; i < bits; ++i) s.areaChanged[static_cast<size_t>(i)] = (b[aoff + i / 8] >> (i % 8)) & 1u;
    }
    const uint32_t woff = rd<uint32_t>(b, 100);
    if (woff) {
        if (woff + 32ull + 16ull * nd > b.size()) return why = "the walk block past the end", false;
        decal::DecalWalk& w = s.walk;
        w.origin[0] = rd<float>(b, woff);
        w.origin[1] = rd<float>(b, woff + 4);
        w.inverse[0] = rd<float>(b, woff + 8);
        w.inverse[1] = rd<float>(b, woff + 12);
        w.cols = rd<uint32_t>(b, woff + 16);
        w.rows = rd<uint32_t>(b, woff + 20);
        for (uint32_t i = 0; i < nd; ++i) {
            const size_t o = woff + 32 + 16ull * i;
            w.cell.push_back(rd<uint32_t>(b, o));
            w.rank.push_back(rd<uint32_t>(b, o + 4));
            w.point.push_back(rd<float>(b, o + 8));
            w.point.push_back(rd<float>(b, o + 12));
        }
    }
    decal::buildIndex(s);
    return true;
}

size_t align16(size_t v) { return (v + 15) & ~static_cast<size_t>(15); }

// The image of a set (no changed block: a file load re-bakes every tile with decals; no hash block).
std::vector<uint8_t> writeImage(const Image& src, const decal::DecalSet& s)
{
    const size_t nd = s.decals.size(), nn = s.textures.size(), nv = s.vertices.size();
    const bool area = s.areaTilesX && s.areaTilesY && s.areaChanged.size() == static_cast<size_t>(s.areaTilesX) * s.areaTilesY;
    const bool walk = decal::hasWalk(s);
    size_t at = 112;
    const size_t doff = at;
    at = align16(at + 48 * nd);
    const size_t noff = at;
    at = align16(at + 200 * nn);
    const size_t voff = at;
    at = align16(at + 8 * nv);
    const size_t aoff = area ? at : 0;
    if (area) at = align16(at + (s.areaChanged.size() + 7) / 8);
    const size_t woff = walk ? at : 0;
    if (walk) at = align16(at + 32 + 16 * nd);
    const size_t coff = at;   // empty changed block
    std::vector<uint8_t> b(at, 0);
    wr<uint32_t>(b, 0, kMagic);
    wr<uint32_t>(b, 4, 1);
    wr<uint32_t>(b, 8, 112);
    wr<uint32_t>(b, 12, 48);
    wr<uint32_t>(b, 16, src.generation);
    wr<uint32_t>(b, 20, src.loadSeq);
    wr<uint32_t>(b, 24, static_cast<uint32_t>(nd));
    wr<uint32_t>(b, 28, static_cast<uint32_t>(nn));
    wr<uint32_t>(b, 32, static_cast<uint32_t>(nv));
    wr<uint32_t>(b, 36, 0);
    wr<uint32_t>(b, 40, static_cast<uint32_t>(doff));
    wr<uint32_t>(b, 44, static_cast<uint32_t>(noff));
    wr<uint32_t>(b, 48, static_cast<uint32_t>(voff));
    wr<uint32_t>(b, 52, static_cast<uint32_t>(coff));
    for (int k = 0; k < 4; ++k) wr<float>(b, 56 + 4 * k, s.worldRect[k]);
    wr<uint64_t>(b, 72, static_cast<uint64_t>(b.size()));
    wr<uint32_t>(b, 80, 0);
    wr<uint32_t>(b, 84, 0);
    wr<uint32_t>(b, 88, static_cast<uint32_t>(aoff));
    wr<uint32_t>(b, 92, area ? s.areaTilesX : 0);
    wr<uint32_t>(b, 96, area ? s.areaTilesY : 0);
    wr<uint32_t>(b, 100, static_cast<uint32_t>(woff));
    for (size_t i = 0; i < nd; ++i) {
        const decal::Decal& d = s.decals[i];
        const size_t o = doff + 48 * i;
        for (int k = 0; k < 4; ++k) wr<float>(b, o + 4 * k, d.rect[k]);
        wr<float>(b, o + 16, d.priority);
        wr<uint32_t>(b, o + 20, d.pass & 1u);
        for (int k = 0; k < 3; ++k) wr<uint32_t>(b, o + 24 + 4 * k, d.texture[k]);
        wr<uint32_t>(b, o + 36, d.firstVertex);
        wr<uint32_t>(b, o + 40, d.vertexCount / 3);
        wr<uint32_t>(b, o + 44, d.listOrder);
    }
    for (size_t i = 0; i < nn; ++i) {
        const size_t o = noff + 200 * i;
        wr<uint32_t>(b, o, i < src.handles.size() ? src.handles[i] : static_cast<uint32_t>(0x40000000u + i));
        const std::string& p = s.textures[i];
        std::memcpy(b.data() + o + 8, p.data(), (std::min)(p.size(), static_cast<size_t>(191)));
    }
    if (nv) std::memcpy(b.data() + voff, s.vertices.data(), 8 * nv);
    if (area)
        for (size_t i = 0; i < s.areaChanged.size(); ++i)
            if (s.areaChanged[i]) b[aoff + i / 8] |= static_cast<uint8_t>(1u << (i % 8));
    if (walk) {
        uint32_t nSmall = 0, nLarge = 0;
        for (uint32_t c : s.walk.cell) (c == decal::kWalkLarge ? nLarge : nSmall)++;
        wr<float>(b, woff, s.walk.origin[0]);
        wr<float>(b, woff + 4, s.walk.origin[1]);
        wr<float>(b, woff + 8, s.walk.inverse[0]);
        wr<float>(b, woff + 12, s.walk.inverse[1]);
        wr<uint32_t>(b, woff + 16, s.walk.cols);
        wr<uint32_t>(b, woff + 20, s.walk.rows);
        wr<uint32_t>(b, woff + 24, nSmall);
        wr<uint32_t>(b, woff + 28, nLarge);
        for (size_t i = 0; i < nd; ++i) {
            const size_t o = woff + 32 + 16 * i;
            wr<uint32_t>(b, o, s.walk.cell[i]);
            wr<uint32_t>(b, o + 4, s.walk.rank[i]);
            wr<float>(b, o + 8, s.walk.point[2 * i]);
            wr<float>(b, o + 12, s.walk.point[2 * i + 1]);
        }
    }
    return b;
}

double nowMs()
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

int fail(const std::string& s)
{
    std::fprintf(stderr, "roadgen: FAILED %s\n", s.c_str());
    return 1;
}

}  // namespace

int main(int argc, char** argv)
{
    std::string plotsPath, styleName_, dtapIn, dtapOut, linesOut, statsOut;
    bool keepTap = false;
    int repeat = 1, sweep = 0;
    int dump[4] = {0, 0, -1, -1};
    float curveMax = 40.0f;
    struct Change {
        int x, y, band;
    };
    std::vector<Change> changes;
    LayoutOptions o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--plots") plotsPath = next();
        else if (a == "--style") styleName_ = next();
        else if (a == "--dtap") dtapIn = next();
        else if (a == "--out") dtapOut = next();
        else if (a == "--lines") linesOut = next();
        else if (a == "--stats") statsOut = next();
        else if (a == "--keep-tap-routes") keepTap = true;
        else if (a == "--repeat") repeat = std::atoi(next().c_str());
        else if (a == "--sweep") sweep = std::atoi(next().c_str());
        else if (a == "--dump") {
            if (std::sscanf(next().c_str(), "%d,%d,%d,%d", &dump[0], &dump[1], &dump[2], &dump[3]) != 4)
                return std::fprintf(stderr, "usage: --dump X0,Y0,X1,Y1\n"), 2;
        }
        else if (a == "--jitter") o.jitter = o.lineJitter = static_cast<float>(std::atof(next().c_str()));   // (c), line
        else if (a == "--tension") o.tension = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--fillet") o.filletRadius = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--city-stop") o.cityStop = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--merge") o.mergeLength = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--leaf") o.leafCurve = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--curve-max") curveMax = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--connect") {
            if (!connectFromName(next(), o.connect)) return std::fprintf(stderr, "usage: --connect tidy|all\n"), 2;
        }
        else if (a == "--junction") {
            if (!junctionFromName(next(), o.junction))
                return std::fprintf(stderr, "usage: --junction through|cross|ring|rosette\n"), 2;
        }
        else if (a == "--junction-min") o.junctionMin = static_cast<uint8_t>(std::atoi(next().c_str()));
        else if (a == "--ring") o.ringRadius = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--change") {
            Change c;
            if (std::sscanf(next().c_str(), "%d,%d,%d", &c.x, &c.y, &c.band) != 3) return std::fprintf(stderr, "usage: --change X,Y,BAND\n"), 2;
            changes.push_back(c);
        } else {
            std::fprintf(stderr, "roadgen: unknown argument %s\n", a.c_str());
            return 2;
        }
    }
    if (plotsPath.empty() || !styleFromName(styleName_, o.style) || repeat < 1 || dtapIn.empty() != dtapOut.empty()) {
        std::fprintf(stderr, "usage: c5r_roadgen --plots P.bin --style straight|spline|jitter|line [--dtap IN --out OUT] ...\n");
        return 2;
    }
    MapIn m;
    std::string why;
    if (!loadPlots(plotsPath, m, why)) return fail(why);
    Network net;
    RoadDecals roads;
    std::vector<double> ms, netMs;
    for (int r = 0; r < repeat; ++r) {
        const double t0 = nowMs();
        Status st = buildNetwork(m, net, o.connect);
        const double t1 = nowMs();
        if (st.ok()) st = layOut(m, net, o, roads);
        const double t2 = nowMs();
        if (!st.ok()) return fail("produce: " + st.str());
        ms.push_back(t2 - t0);
        netMs.push_back(t1 - t0);
    }
    std::vector<uint32_t> where;
    const uint32_t empty = countEmptyFaces(m, net, &where);
    const LineCheck lc = checkLines(m, roads, 1.0f, &net, curveMax);
    std::sort(ms.begin(), ms.end());
    std::sort(netMs.begin(), netMs.end());
    std::string js = "{\"style\": \"" + std::string(styleName(o.style)) + "\", \"connect\": \"" + connectName(o.connect) +
                     "\", \"junction\": \"" + junctionName(o.junction) + "\", \"junction_min\": " + std::to_string(o.junctionMin) +
                     ", \"ring_radius\": " + std::to_string(o.ringRadius) + ", \"line_jitter\": " + std::to_string(o.lineJitter);
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  ", \"map\": [%u, %u], \"wrap\": %d, \"route_plots\": %u, \"candidate_edges\": %u, \"edges_no_triangle\": %u, "
                  "\"clusters\": %u, \"max_cluster_edges\": %u, \"rejected\": %u, \"empty_faces\": %u, \"decals\": %zu, "
                  "\"vertices\": %zu, \"pieces\": %u, \"crossings\": %u, \"overshoots\": %u, \"max_turn_deg\": %.2f",
                  m.width, m.height, m.wrapX ? 1 : 0, net.routePlots, net.candidateEdges, net.edges0, net.clusters,
                  net.maxClusterEdges, net.rejected, empty, roads.decals.size(), roads.vertices.size(), lc.pieces, lc.crossings,
                  lc.overshoots, lc.maxTurnDeg);
    js += buf;
    std::snprintf(buf, sizeof buf, ", \"produce_ms\": {\"n\": %d, \"median\": %.3f, \"min\": %.3f, \"max\": %.3f}, \"network_ms_median\": %.3f",
                  repeat, ms[ms.size() / 2], ms.front(), ms.back(), netMs[netMs.size() / 2]);
    js += buf;
    std::snprintf(buf, sizeof buf, ", \"abrupt_ends\": %u, \"long_curves\": %u, \"curve_max\": %.1f, \"max_curve\": %.1f, "
                  "\"straight_share\": %.4f, \"fillet\": %.1f, \"merge\": %.1f, \"leaf_curve\": %.1f",
                  lc.abruptEnds, lc.longCurves, curveMax, lc.maxCurve, lc.straightShare, o.filletRadius, o.mergeLength, o.leafCurve);
    js += buf;
    std::printf("roadgen: straight-first rule: %u abrupt ends, %u curves longer than %.0f (longest %.1f), straight share %.3f\n",
                lc.abruptEnds, lc.longCurves, curveMax, lc.maxCurve, lc.straightShare);
    std::printf("roadgen: connect %s, junction %s (from %u edges)\n", connectName(o.connect), junctionName(o.junction),
                static_cast<unsigned>(o.junctionMin));
    std::printf("roadgen: %s: %u route plots, %u edges kept of %u, %u rejected in %u clusters, %u empty faces, %zu decals, "
                "%u crossings, %u overshoots, max turn %.1f deg, produce median %.2f ms (n %d)\n",
                styleName(o.style), net.routePlots, net.candidateEdges - net.rejected, net.candidateEdges, net.rejected,
                net.clusters, empty, roads.decals.size(), lc.crossings, lc.overshoots, lc.maxTurnDeg, ms[ms.size() / 2], repeat);
    // --dump X0,Y0,X1,Y1: per plot of the window its band, city mark and kept edges (diagnosis)
    if (dump[2] >= dump[0] && dump[3] >= dump[1] && dump[2] >= 0) {
        const char* dn[6] = {"E", "NE", "NW", "W", "SW", "SE"};
        for (int y = dump[3]; y >= dump[1]; --y)
            for (int x = dump[0]; x <= dump[2]; ++x) {
                if (x < 0 || y < 0 || x >= static_cast<int>(m.width) || y >= static_cast<int>(m.height)) continue;
                const size_t i = static_cast<size_t>(y) * m.width + x;
                if (m.plots[i].band == kBandNone) continue;
                std::string ed;
                for (int d = 0; d < 6; ++d)
                    if (net.edges[i] & (1u << d)) ed += std::string(ed.empty() ? "" : ",") + dn[d];
                std::string cands;
                for (int d = 0; d < 6; ++d) {
                    int nx, ny;
                    if (neighbour(m, x, y, d, nx, ny) && m.plots[static_cast<size_t>(ny) * m.width + nx].band != kBandNone &&
                        !(net.edges[i] & (1u << d)))
                        cands += std::string(cands.empty() ? "" : ",") + dn[d];
                }
                std::printf("roadgen: plot %d,%d band %u city %u edges [%s] dropped [%s]\n", x, y, m.plots[i].band, m.plots[i].city,
                            ed.c_str(), cands.c_str());
            }
    }
    // local changes
    js += ", \"changes\": [";
    for (size_t k = 0; k < changes.size(); ++k) {
        const Change& c = changes[k];
        if (c.x < 0 || c.y < 0 || c.x >= static_cast<int>(m.width) || c.y >= static_cast<int>(m.height) || c.band < 0 ||
            c.band >= kBandCount)
            return fail("--change outside the map or the bands");
        MapIn m2 = m;
        m2.plots[static_cast<size_t>(c.y) * m.width + c.x].band = static_cast<uint8_t>(c.band);
        Network n2;
        RoadDecals r2;
        RoadChange ch;
        std::vector<double> cms;
        for (int r = 0; r < repeat; ++r) {
            const double t0 = nowMs();
            Status st = produce(m2, o, n2, r2);
            if (st.ok()) st = roadChange(roads, r2, ch);
            const double t1 = nowMs();
            if (!st.ok()) return fail("change: " + st.str());
            cms.push_back(t1 - t0);
        }
        std::sort(cms.begin(), cms.end());
        int farthest = 0;
        for (uint32_t p : ch.plots) {
            const int dx0 = std::abs(static_cast<int>(p % m.width) - c.x);
            const int dx = m.wrapX ? (std::min)(dx0, static_cast<int>(m.width) - dx0) : dx0;
            farthest = (std::max)(farthest, (std::max)(dx, std::abs(static_cast<int>(p / m.width) - c.y)));
        }
        std::snprintf(buf, sizeof buf, "%s{\"plot\": [%d, %d], \"band\": %d, \"changed_plots\": %zu, \"rects\": %zu, \"farthest\": %d, "
                      "\"ms_median\": %.3f}", k ? ", " : "", c.x, c.y, c.band, ch.plots.size(), ch.rects.size(), farthest, cms[cms.size() / 2]);
        js += buf;
        std::printf("roadgen: change %d,%d -> band %d: %zu plots re-laid (farthest %d plots away), %zu rectangles, %.2f ms\n", c.x, c.y,
                    c.band, ch.plots.size(), farthest, ch.rects.size(), cms[cms.size() / 2]);
    }
    js += "]";
    // --sweep N: N one-plot changes spread over the map (every k-th route plot removed, every k-th plot beside a route
    // given a road), each produced from scratch and diffed: the reach (hex distance of the farthest plot whose decals
    // changed) and the time of production + diff
    if (sweep > 0) {
        std::vector<uint32_t> cand;
        for (uint32_t i = 0; i < m.plots.size(); ++i) {
            bool nearRoute = m.plots[i].band != kBandNone;
            for (int d = 0; d < kDirs && !nearRoute; ++d) {
                int nx, ny;
                if (neighbour(m, static_cast<int>(i % m.width), static_cast<int>(i / m.width), d, nx, ny) &&
                    m.plots[static_cast<size_t>(ny) * m.width + nx].band != kBandNone)
                    nearRoute = true;
            }
            if (nearRoute) cand.push_back(i);
        }
        const size_t step = (std::max)(static_cast<size_t>(1), cand.size() / static_cast<size_t>(sweep));
        std::vector<int> reach;
        std::vector<double> sms;
        std::vector<size_t> nplots;
        auto hexDist = [&](int x0, int y0, int x1, int y1) {
            const int q0 = x0 - (y0 - (y0 & 1)) / 2, q1 = x1 - (y1 - (y1 & 1)) / 2;
            int dq = q1 - q0;
            const int dr = y1 - y0;
            int best = (std::abs(dq) + std::abs(dr) + std::abs(dq + dr)) / 2;
            if (m.wrapX)
                for (int s = -1; s <= 1; s += 2) {
                    const int dq2 = dq + s * static_cast<int>(m.width);
                    best = (std::min)(best, (std::abs(dq2) + std::abs(dr) + std::abs(dq2 + dr)) / 2);
                }
            return best;
        };
        for (size_t k = 0; k < cand.size() && reach.size() < static_cast<size_t>(sweep); k += step) {
            const uint32_t i = cand[k];
            MapIn m2 = m;
            PlotIn& p = m2.plots[i];
            p.band = p.band == kBandNone ? static_cast<uint8_t>(kBandRoad) : static_cast<uint8_t>(kBandNone);
            p.city = 0;
            Network n2;
            RoadDecals r2;
            RoadChange ch;
            const double t0 = nowMs();
            Status st = produce(m2, o, n2, r2);
            if (st.ok()) st = roadChange(roads, r2, ch);
            const double t1 = nowMs();
            if (!st.ok()) return fail("sweep: " + st.str());
            int far0 = 0;
            for (uint32_t q : ch.plots)
                far0 = (std::max)(far0, hexDist(static_cast<int>(i % m.width), static_cast<int>(i / m.width), static_cast<int>(q % m.width),
                                                static_cast<int>(q / m.width)));
            reach.push_back(far0);
            sms.push_back(t1 - t0);
            nplots.push_back(ch.plots.size());
        }
        std::vector<int> rs = reach;
        std::sort(rs.begin(), rs.end());
        std::sort(sms.begin(), sms.end());
        std::sort(nplots.begin(), nplots.end());
        int within2 = 0;
        for (int r : reach) within2 += r <= 2;
        std::snprintf(buf, sizeof buf, ", \"sweep\": {\"changes\": %zu, \"reach_median\": %d, \"reach_p95\": %d, \"reach_max\": %d, "
                      "\"within_2\": %d, \"plots_median\": %zu, \"plots_max\": %zu, \"ms_median\": %.3f, \"ms_p95\": %.3f}",
                      rs.size(), rs[rs.size() / 2], rs[rs.size() * 95 / 100], rs.back(), within2, nplots[nplots.size() / 2],
                      nplots.back(), sms[sms.size() / 2], sms[sms.size() * 95 / 100]);
        js += buf;
        std::printf("roadgen: sweep of %zu one-plot changes: reach median %d, p95 %d, max %d plots (%d within 2); plots re-laid "
                    "median %zu, max %zu; produce + diff median %.2f ms, p95 %.2f ms\n", rs.size(), rs[rs.size() / 2],
                    rs[rs.size() * 95 / 100], rs.back(), within2, nplots[nplots.size() / 2], nplots.back(), sms[sms.size() / 2],
                    sms[sms.size() * 95 / 100]);
    }
    if (!dtapIn.empty()) {
        std::vector<uint8_t> b;
        if (!readFile(dtapIn, b)) return fail("cannot read " + dtapIn);
        Image img;
        if (!parseImage(b, img, why)) return fail(dtapIn + ": " + why);
        decal::DecalSet composed;
        ComposeStats cs;
        const double t0 = nowMs();
        const Status st = composeRoadSet(img.set, !keepTap, roads, composed, &cs);
        const double t1 = nowMs();
        if (!st.ok()) return fail("compose: " + st.str());
        if (!writeFile(dtapOut, writeImage(img, composed))) return fail("cannot write " + dtapOut);
        std::snprintf(buf, sizeof buf, ", \"compose\": {\"tap_decals\": %u, \"tap_routes_removed\": %u, \"road_decals\": %u, "
                      "\"walk\": %d, \"walk_large\": %u, \"area\": %d, \"area_bits_set\": %u, \"ms\": %.3f}",
                      cs.tapDecals, cs.tapRemoved, cs.roadDecals, cs.walk ? 1 : 0, cs.walkLarge, cs.area ? 1 : 0, cs.areaBitsSet, t1 - t0);
        js += buf;
        std::printf("roadgen: composed %s: %u tap decals, %u route curves removed, %u road decals added, walk %d, area %d\n",
                    dtapOut.c_str(), cs.tapDecals, cs.tapRemoved, cs.roadDecals, cs.walk ? 1 : 0, cs.area ? 1 : 0);
    }
    js += "}\n";
    if (!statsOut.empty() && !writeFile(statsOut, std::vector<uint8_t>(js.begin(), js.end()))) return fail("cannot write " + statsOut);
    if (!linesOut.empty()) {
        std::vector<uint8_t> b(8);
        std::memcpy(b.data(), "C5RL", 4);
        wr<uint32_t>(b, 4, static_cast<uint32_t>(roads.decals.size()));
        for (size_t k = 0; k < roads.decals.size(); ++k) {
            const uint32_t s0 = roads.lineStart[k], s1 = roads.lineStart[k + 1];
            const size_t at = b.size();
            b.resize(at + 8 + 8ull * (s1 - s0));
            wr<uint32_t>(b, at, roads.owner[k]);
            wr<uint32_t>(b, at + 4, s1 - s0);
            std::memcpy(b.data() + at + 8, roads.lines.data() + 2ull * s0, 8ull * (s1 - s0));
        }
        if (!writeFile(linesOut, b)) return fail("cannot write " + linesOut);
    }
    if (empty && o.connect == Connect::Tidy) return fail("the network has " + std::to_string(empty) + " empty faces");
    return 0;
}
