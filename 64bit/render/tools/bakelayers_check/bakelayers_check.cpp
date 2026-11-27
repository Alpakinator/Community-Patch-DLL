// bakelayers_check - issue 3401: the server's layer lists against the EXE's, on every tile of a save.
//
//   bakelayers_check --tap FILE.c5hf (--game-dir DIR | --csv FILE) --exe-lists FILE.txt
//                    [--report OUT.json] [--server-lists OUT.txt]
//
// --tap: the height-field tap of the session (C5HF, heights + material bytes, c5r/heightfield/heightfield.h).
// --game-dir: the install whose TerrainDef.csv the EXE read (through c5r::gamefs); --csv: that file directly.
// --exe-lists: the EXE's lists and TR8 tables as shell/tools/layer_probe.py --lists-txt writes them (format below).
// Prints one summary line, one line per differing tile with its cause, and with --report a JSON report.
//
// Exit codes: 0 every tile's list and the tables equal; 1 at least one difference (listed); 2 usage; 3 an input file
// cannot be read; 4 an input does not parse (tap, CSV, lists file); 5 the inputs disagree in shape (tile grid, sample
// counts) or a material byte has no texture set.
//
// Lists file (text, '#' starts a comment line):
//   grid TX TY                 tiles (x, y)
//   samples W H                TR8+0x1AA8 / +0x1AAC
//   layers N                   TR8+0x1110
//   c10 v0 v1 ...              TR8+0xC10 (any count up to 256)
//   b10 v0 ...                 TR8+0xB10
//   f10 v0 ...                 TR8+0xF10
//   k1010 v0 ...               TR8+0x1010
//   tile I TX TY N id1 .. idN  one line per tile, I = TX * TY_count + TY
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "c5r/bake/layers.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/heightfield/heightfield.h"

using namespace c5r;
using namespace c5r::bake;

namespace {

enum Exit { kOk = 0, kDiff = 1, kUsage = 2, kIo = 3, kParse = 4, kShape = 5 };

struct ExeLists {
    uint32_t tx = 0, ty = 0, w = 0, h = 0, layers = 0;
    std::map<std::string, std::vector<uint32_t>> tables;
    std::vector<LayerList> lists;
    std::vector<bool> seen;
};

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// Returns an empty string or the reason the file does not parse.
std::string parseLists(const std::string& text, ExeLists& L) {
    std::istringstream in(text);
    std::string line;
    size_t no = 0;
    bool haveGrid = false;
    while (std::getline(in, line)) {
        ++no;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        std::vector<long long> v;
        std::string tok;
        while (ls >> tok) {
            char* end = nullptr;
            long long x = std::strtoll(tok.c_str(), &end, 10);
            if (!end || *end != '\0' || x < 0 || x > 0xFFFFFFFFll)
                return "line " + std::to_string(no) + ": '" + tok + "' is not a number";
            v.push_back(x);
        }
        if (key == "grid") {
            if (v.size() != 2 || v[0] == 0 || v[1] == 0 || v[0] > 1024 || v[1] > 1024)
                return "line " + std::to_string(no) + ": grid needs two tile counts";
            L.tx = static_cast<uint32_t>(v[0]);
            L.ty = static_cast<uint32_t>(v[1]);
            L.lists.assign(static_cast<size_t>(L.tx) * L.ty, LayerList());
            L.seen.assign(L.lists.size(), false);
            haveGrid = true;
        } else if (key == "samples") {
            if (v.size() != 2) return "line " + std::to_string(no) + ": samples needs two values";
            L.w = static_cast<uint32_t>(v[0]);
            L.h = static_cast<uint32_t>(v[1]);
        } else if (key == "layers") {
            if (v.size() != 1) return "line " + std::to_string(no) + ": layers needs one value";
            L.layers = static_cast<uint32_t>(v[0]);
        } else if (key == "c10" || key == "b10" || key == "f10" || key == "k1010") {
            if (v.size() > 256) return "line " + std::to_string(no) + ": table longer than 256";
            std::vector<uint32_t>& t = L.tables[key];
            for (long long x : v) t.push_back(static_cast<uint32_t>(x));
        } else if (key == "tile") {
            if (!haveGrid) return "line " + std::to_string(no) + ": tile before grid";
            if (v.size() < 4 || v[3] > kMaxLayers || v.size() != 4 + static_cast<size_t>(v[3]))
                return "line " + std::to_string(no) + ": tile needs I TX TY N and N ids (N <= 8)";
            if (v[1] >= L.tx || v[2] >= L.ty || v[0] != v[1] * static_cast<long long>(L.ty) + v[2])
                return "line " + std::to_string(no) + ": tile index outside the grid or not TX * TY_count + TY";
            LayerList& ll = L.lists[static_cast<size_t>(v[0])];
            ll.count = static_cast<uint32_t>(v[3]);
            for (uint32_t i = 0; i < ll.count; ++i) ll.ids[i] = static_cast<uint32_t>(v[4 + i]);
            L.seen[static_cast<size_t>(v[0])] = true;
        } else {
            return "line " + std::to_string(no) + ": unknown key '" + key + "'";
        }
    }
    if (!haveGrid) return "no grid line";
    if (L.layers == 0) return "no layers line";
    for (size_t i = 0; i < L.seen.size(); ++i)
        if (!L.seen[i]) return "tile " + std::to_string(i) + " missing";
    return std::string();
}

std::string listStr(const LayerList& l) {
    std::string s = "[";
    for (uint32_t i = 0; i < l.count; ++i) s += (i ? "," : "") + std::to_string(l.ids[i]);
    return s + "]";
}

bool sameList(const LayerList& a, const LayerList& b) {
    if (a.count != b.count) return false;
    for (uint32_t i = 0; i < a.count; ++i)
        if (a.ids[i] != b.ids[i]) return false;
    return true;
}

std::string cause(const LayerList& srv, const LayerList& exe, const LayerListDetail& d) {
    // Named in the order of likelihood (research 2.4).
    if (d.slopeSamples) {
        char b[160];
        std::snprintf(b, sizeof b, "slope-projected material: %u samples, moved +1 %u, +2 %u, n.z closest to 0.9: %.7f",
                      d.slopeSamples, d.moved1, d.moved2, d.closestNz);
        return b;
    }
    if (d.sorted) return "more than 8 ids: sort by count and cut";
    if (srv.count != exe.count) return "different id sets (material bytes differ from the EXE's at list time?)";
    return "same count, different ids";
}

int usage() {
    std::fprintf(stderr,
                 "usage: bakelayers_check --tap FILE (--game-dir DIR | --csv FILE) --exe-lists FILE "
                 "[--report OUT.json] [--server-lists OUT.txt]\n");
    return kUsage;
}

}  // namespace

int main(int argc, char** argv) {
    std::string tap, gameDir, csv, lists, report, serverOut;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (i + 1 >= argc) return usage();
        std::string v = argv[++i];
        if (a == "--tap") tap = v;
        else if (a == "--game-dir") gameDir = v;
        else if (a == "--csv") csv = v;
        else if (a == "--exe-lists") lists = v;
        else if (a == "--report") report = v;
        else if (a == "--server-lists") serverOut = v;
        else return usage();
    }
    if (tap.empty() || lists.empty() || (gameDir.empty() == csv.empty())) return usage();

    // inputs
    std::vector<uint8_t> tapBytes, listBytes, csvBytes;
    if (!readFile(tap, tapBytes)) { std::fprintf(stderr, "bakelayers_check: cannot read %s\n", tap.c_str()); return kIo; }
    if (!readFile(lists, listBytes)) { std::fprintf(stderr, "bakelayers_check: cannot read %s\n", lists.c_str()); return kIo; }
    heightfield::HeightGrid g;
    Status s = heightfield::readTap(tapBytes.data(), tapBytes.size(), g);
    if (!s) { std::fprintf(stderr, "bakelayers_check: tap %s: %s\n", tap.c_str(), s.str().c_str()); return kParse; }
    tapBytes.clear();
    tapBytes.shrink_to_fit();
    LayerTables tables;
    std::string origin;
    if (!csv.empty()) {
        if (!readFile(csv, csvBytes)) { std::fprintf(stderr, "bakelayers_check: cannot read %s\n", csv.c_str()); return kIo; }
        origin = csv;
        s = parseTerrainDef(std::string(csvBytes.begin(), csvBytes.end()), tables);
    } else {
        gamefs::GameFiles fs;
        s = fs.open(gamefs::fromUtf8(gameDir));
        if (!s) { std::fprintf(stderr, "bakelayers_check: game dir %s: %s\n", gameDir.c_str(), s.str().c_str()); return kIo; }
        s = loadTerrainDef(fs, tables, &origin);  // through gamefs (issue 3404)
        if (s.code == Err::NotFound || s.code == Err::Io) {
            std::fprintf(stderr, "bakelayers_check: %s\n", s.str().c_str());
            return kIo;
        }
    }
    if (!s) { std::fprintf(stderr, "bakelayers_check: TerrainDef.csv: %s\n", s.str().c_str()); return kParse; }
    ExeLists L;
    std::string why = parseLists(std::string(listBytes.begin(), listBytes.end()), L);
    if (!why.empty()) { std::fprintf(stderr, "bakelayers_check: lists %s: %s\n", lists.c_str(), why.c_str()); return kParse; }
    s = checkGrid(g);
    if (!s) { std::fprintf(stderr, "bakelayers_check: tap %s: %s\n", tap.c_str(), s.str().c_str()); return kShape; }
    if (L.tx != g.mapW || L.ty != g.mapH) {
        std::fprintf(stderr, "bakelayers_check: lists have %ux%u tiles, the tap %ux%u plots\n", L.tx, L.ty, g.mapW, g.mapH);
        return kShape;
    }
    if ((L.w || L.h) && (L.w != g.samplesX || L.h != g.samplesY)) {
        std::fprintf(stderr, "bakelayers_check: lists say %ux%u samples, the tap %ux%u\n", L.w, L.h, g.samplesX, g.samplesY);
        return kShape;
    }

    // tables
    std::vector<std::string> tableDiffs;
    if (L.layers != tables.layerCount)
        tableDiffs.push_back("layer count: EXE " + std::to_string(L.layers) + ", server " + std::to_string(tables.layerCount));
    auto cmp = [&](const char* key, size_t n, auto server) {
        auto it = L.tables.find(key);
        if (it == L.tables.end()) return;
        for (size_t i = 0; i < n && i < it->second.size(); ++i)
            if (it->second[i] != server(i))
                tableDiffs.push_back(std::string(key) + "[" + std::to_string(i) + "]: EXE " + std::to_string(it->second[i]) +
                                     ", server " + std::to_string(server(i)));
    };
    cmp("c10", tables.sets.size(), [&](size_t i) { return static_cast<uint32_t>(tables.firstLayerOfMaterial[i]); });
    cmp("b10", tables.sets.size(), [&](size_t i) { return static_cast<uint32_t>(tables.slopeProjected[i]); });
    cmp("f10", tables.layerCount, [&](size_t i) { return tables.setOfLayer[i]; });
    cmp("k1010", tables.layerCount, [&](size_t i) { return tables.projectionOfLayer[i]; });

    // lists
    uint32_t equal = 0, differ = 0, slopeTiles = 0, sortedTiles = 0;
    std::string diffLines, diffJson, slopeLines, slopeJson;
    std::string serverText;
    for (uint32_t x = 0; x < g.mapW; ++x) {
        for (uint32_t y = 0; y < g.mapH; ++y) {
            TileId t;
            t.x = x;
            t.y = y;
            LayerList srv;
            LayerListDetail d;
            s = buildLayerList(tables, g, t, srv, &d);
            if (!s) {
                std::fprintf(stderr, "bakelayers_check: tile (%u,%u): %s\n", x, y, s.str().c_str());
                return s.code == Err::NotFound ? kShape : kParse;
            }
            const size_t idx = static_cast<size_t>(x) * g.mapH + y;
            if (d.slopeSamples) {
                ++slopeTiles;
                char b[256];
                std::snprintf(b, sizeof b, "SLOPE tile %zu (%u,%u): %u slope samples, moved +1 %u, +2 %u, n.z closest to "
                              "0.9: %.7f, list %s\n", idx, x, y, d.slopeSamples, d.moved1, d.moved2, d.closestNz,
                              listStr(srv).c_str());
                slopeLines += b;
                std::snprintf(b, sizeof b, "%s{\"i\":%zu,\"x\":%u,\"y\":%u,\"samples\":%u,\"moved1\":%u,\"moved2\":%u,"
                              "\"closest_nz\":%.7f}", slopeJson.empty() ? "" : ",", idx, x, y, d.slopeSamples, d.moved1,
                              d.moved2, d.closestNz);
                slopeJson += b;
            }
            if (d.sorted) ++sortedTiles;
            if (!serverOut.empty()) {
                serverText += "tile " + std::to_string(idx) + " " + std::to_string(x) + " " + std::to_string(y) + " " +
                              std::to_string(srv.count);
                for (uint32_t i = 0; i < srv.count; ++i) serverText += " " + std::to_string(srv.ids[i]);
                serverText += "\n";
            }
            if (sameList(srv, L.lists[idx])) {
                ++equal;
                continue;
            }
            ++differ;
            std::string c = cause(srv, L.lists[idx], d);
            char b[512];
            std::snprintf(b, sizeof b, "DIFF tile %zu (%u,%u): EXE %s server %s: %s\n", idx, x, y,
                          listStr(L.lists[idx]).c_str(), listStr(srv).c_str(), c.c_str());
            diffLines += b;
            std::snprintf(b, sizeof b, "%s{\"i\":%zu,\"x\":%u,\"y\":%u,\"exe\":%s,\"server\":%s,\"cause\":\"%s\"}",
                          diffJson.empty() ? "" : ",", idx, x, y, listStr(L.lists[idx]).c_str(), listStr(srv).c_str(),
                          c.c_str());
            diffJson += b;
        }
    }
    std::printf("bakelayers_check: %u tiles, %u equal, %u differ; %u with slope-projected materials, %u sorted (>8); "
                "tables %s; TerrainDef from %s\n",
                equal + differ, equal, differ, slopeTiles, sortedTiles,
                tableDiffs.empty() ? "equal" : (std::to_string(tableDiffs.size()) + " differences").c_str(),
                origin.c_str());
    for (const std::string& t : tableDiffs) std::printf("TABLE %s\n", t.c_str());
    std::fputs(slopeLines.c_str(), stdout);
    std::fputs(diffLines.c_str(), stdout);
    if (!serverOut.empty()) {
        std::ofstream o(serverOut, std::ios::binary);
        o << "grid " << g.mapW << " " << g.mapH << "\nsamples " << g.samplesX << " " << g.samplesY << "\nlayers "
          << tables.layerCount << "\n" << serverText;
        if (!o) { std::fprintf(stderr, "bakelayers_check: cannot write %s\n", serverOut.c_str()); return kIo; }
    }
    if (!report.empty()) {
        std::ofstream o(report, std::ios::binary);
        o << "{\"tiles\":" << (equal + differ) << ",\"equal\":" << equal << ",\"differ\":" << differ
          << ",\"slope_tiles\":" << slopeTiles << ",\"sorted_tiles\":" << sortedTiles
          << ",\"table_differences\":" << tableDiffs.size() << ",\"layer_count\":" << tables.layerCount
          << ",\"sets\":" << tables.sets.size() << ",\"slope\":[" << slopeJson << "],\"differences\":[" << diffJson
          << "]}\n";
        if (!o) { std::fprintf(stderr, "bakelayers_check: cannot write %s\n", report.c_str()); return kIo; }
    }
    return (differ || !tableDiffs.empty()) ? kDiff : kOk;
}
