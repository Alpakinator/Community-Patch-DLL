// bakecompare - the server's baked t0 / t8 against the EXE's own caches, texel by texel (issue 3404, epic 3400,
// acceptance B). Own code.
//
//   bakecompare --ref SAVE_DIR --game-dir INSTALL --exe CivilizationV_DX11.exe --out DIR
//               [--arm armB] [--limit N] [--tiles X,Y;X,Y...] [--diff-images N] [--style-probe] [--t8-rule exe|list] [--sources exe|standin] [--plot-art FILE] [--quiet]
//
// SAVE_DIR is one save of 3401's reference set (D:/c5-64bit-work/3400/ref/<save>/): manifest.json, <arm>/caches/
// raw_index.json with the raw BC1 files (all mips) and the tile records, <arm>/heightfield.c5hf (heights + material
// bytes of the same session), and beside them the EXE-vs-EXE ceiling of 3401: equal_between_sessions.json and
// decoded_<save>.json (optional: without them the ceiling and the "decals" class are reported as unavailable).
// For every tile with a cache the server bakes the tile at the record's size class (c5r::bake::PipelineBaker, stand-in
// sources where wave-2 inputs are open) and compares: BC1 blocks equal (mip 0, all mips), decoded RGB per channel
// (equal share, median, p95, max, histogram of the per-texel largest channel difference), per texture and over all
// compared textures, by cache, for the "fair" set (caches byte-identical between two EXE sessions) and per class of
// difference. Writes <out>/bakecompare_<save>.json, <out>/bakecompare_<save>.csv and, with --diff-images N, BMPs of the
// N worst textures (server | EXE | |difference| x 8, mip 0) under <out>/diff/ (game-derived: keep them on D).
//
// Exit codes: 0 report written (the figures are measurements, not pass / fail); 2 usage; 3 an input missing or
// unreadable (reference folder, manifest, raw index, height field, install, EXE) or the output folder not writable;
// 4 an input unparsable (JSON, height-field tap, TerrainDef.csv); 5 inputs inconsistent (no textures in the index,
// no material bytes in the height field, the height field's map not matching the index's tiles); 6 the D3D11 device,
// the EXE's shaders or the pipeline could not be set up. A tile whose record, raw file or bake is bad is skipped with
// a counted reason, never silently.
//
// Issue 4106 (epic 0166, the same-session comparison):
//   --decals FILE.dtap   the decal tap's image of the SAME session as the caches (4103's file form), read through the
//                        server's own reader (srvrender::DecalTapReader::loadFile, every check of server.exe); each tile
//                        is baked with BakeRequest::decals = c5r::decal::decalsForTile(set, 64 x, 64 y) (4105's pass)
//   --mods DIR           a loose mod folder mounted above the install (gamefs extraLooseRoots; repeatable), e.g.
//                        MODS/(2) Vox Populi: the EXE of the session read VP's farm textures from there
//   --t8-rule decals     BakeRequest::areaChanged = the tile is among tilesForRects(the first set's change) (what 4105's
//                        residency sets after the load: every decal is "added"), the server's own rule (0305)
//   --tie-order list|reverse   the order of equal priorities in each tile's list: as decalsForTile gives it (list order)
//                        or each run of equal priorities reversed (the test of terrain-decals.md 7.2)
//   --tile-flags FILE    the recording's tile_flags.json (every tile's +0x2FC / +0x300): the EXE's area-changed set
//                        against the server's rule over every tile (P14)
// Issue 4111:
//   --t8-rule tap        BakeRequest::areaChanged = the .dtap's area block (the EXE's tile+0x300 of every tile, read by
//                        the decal tap), what the server's residency now does (c5r::decal::areaChangedAt); needs --decals
//                        with an area block (exit 5 without one). "server_rule" in the report is then this set.
//   --tie-order list     is decalsForTile's own order, since 4111 small decals newest first, then large in list order
//   --tie-order listorder  the order before 4111 (ties by list order only), for comparison
// Ticket 0366:
//   --decals with a walk block (the tap's copy of the EXE's spatial index and large list, terrain-decals.md 18):
//                        decalsForTile gives the EXE's own order (its selection, walk and introsort); --tie-order list
//                        is that order; --tie-order rule4111 drops the walk (decalsForTile's 4111 rule on the same set)
//   --decal-bgra NAMES   diagnostic (terrain-decals.md 14.3): these decal textures (comma-separated names, legacy
//                        A8R8G8B8 files) are handed to the decal pass as B8G8R8A8 resources, i.e. with the file's
//                        channels in their meaning (gamefs's reading), instead of the pass's own rule (the file's bytes
//                        as R8G8B8A8). Exit 3 when a name is not in the install, 5 when it is not A8R8G8B8.
// Exit codes as above; --decals or --tile-flags missing 3, unparsable or refused by the reader 4; --t8-rule decals or
// --tie-order without --decals 2. With --decals a tile whose bake fails (a texture the install lacks: NotFound) is skipped
// with its reason.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "bc1.h"
#include "c5r/bake/pipeline.h"
#include "c5r/gamefs/dds.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/shaders/shadersection.h"
#include "c5r/decal/decal.h"
#include "json.h"
#include "srv_decal_tap.h"

using namespace c5r;
using namespace bakecompare;

namespace {

enum Exit { kOk = 0, kUsage = 2, kMissing = 3, kParse = 4, kInconsistent = 5, kSetup = 6 };

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    out.clear();
    std::ifstream f(gamefs::fromUtf8(path).c_str(), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !f.bad();
}
bool isDir(const std::string& p) {
    DWORD a = GetFileAttributesW(gamefs::fromUtf8(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
bool isFile(const std::string& p) {
    DWORD a = GetFileAttributesW(gamefs::fromUtf8(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
bool makeDirs(const std::string& p) {
    std::string cur;
    for (size_t i = 0; i < p.size(); ++i) {
        cur += p[i];
        if ((p[i] == '/' || p[i] == '\\') && cur.size() > 3) CreateDirectoryW(gamefs::fromUtf8(cur).c_str(), nullptr);
    }
    CreateDirectoryW(gamefs::fromUtf8(p).c_str(), nullptr);
    return isDir(p);
}
bool writeText(const std::string& path, const std::string& s) {
    std::ofstream f(gamefs::fromUtf8(path).c_str(), std::ios::binary);
    if (!f) return false;
    f << s;
    return static_cast<bool>(f);
}
std::string jsonStr(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (static_cast<unsigned char>(c) < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += c;
    }
    return o + "\"";
}
std::string fmt(double v, int prec = 6) {
    char b[64];
    std::snprintf(b, sizeof b, "%.*f", prec, v);
    return b;
}
std::string nowText() {
    std::time_t t = std::time(nullptr);
    std::tm tmv;
    localtime_s(&tmv, &t);
    char b[32];
    std::strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &tmv);
    return b;
}

// ---- statistics

struct Hist {
    uint64_t h[256] = {};
    uint64_t n() const { uint64_t s = 0; for (uint64_t v : h) s += v; return s; }
    uint32_t pct(double q) const {
        const uint64_t total = n();
        if (!total) return 0;
        uint64_t cum = 0;
        const double want = q * static_cast<double>(total);
        for (uint32_t v = 0; v < 256; ++v) {
            cum += h[v];
            if (static_cast<double>(cum) >= want) return v;
        }
        return 255;
    }
    uint32_t max() const { for (int v = 255; v > 0; --v) if (h[v]) return static_cast<uint32_t>(v); return 0; }
    void add(const Hist& o) { for (int v = 0; v < 256; ++v) h[v] += o.h[v]; }
};

struct Decoded {          // one comparison scope (mip 0, or all mips)
    Hist ch[3];           // |d| per channel R, G, B
    Hist maxc;            // per texel, the largest channel |d|
    void add(const Decoded& o) { for (int k = 0; k < 3; ++k) ch[k].add(o.ch[k]); maxc.add(o.maxc); }
    std::string json() const {
        const char* names[3] = {"r", "g", "b"};
        std::string s = "{\"texels\":" + std::to_string(maxc.n()) + ",\"texel_equal_share\":" +
                        fmt(maxc.n() ? static_cast<double>(maxc.h[0]) / maxc.n() : 0) + ",\"channels\":{";
        for (int k = 0; k < 3; ++k) {
            const Hist& c = ch[k];
            s += std::string(k ? "," : "") + "\"" + names[k] + "\":{\"equal_share\":" +
                 fmt(c.n() ? static_cast<double>(c.h[0]) / c.n() : 0) + ",\"median\":" + std::to_string(c.pct(0.5)) +
                 ",\"p95\":" + std::to_string(c.pct(0.95)) + ",\"max\":" + std::to_string(c.max()) + "}";
        }
        const Hist& m = maxc;
        uint64_t b47 = 0, b815 = 0, b16 = 0;
        for (int v = 4; v < 8; ++v) b47 += m.h[v];
        for (int v = 8; v < 16; ++v) b815 += m.h[v];
        for (int v = 16; v < 256; ++v) b16 += m.h[v];
        s += "},\"max_channel\":{\"median\":" + std::to_string(m.pct(0.5)) + ",\"p95\":" + std::to_string(m.pct(0.95)) +
             ",\"p99\":" + std::to_string(m.pct(0.99)) + ",\"p999\":" + std::to_string(m.pct(0.999)) + ",\"max\":" +
             std::to_string(m.max()) + ",\"hist_0_1_2_3_4to7_8to15_16up\":[" + std::to_string(m.h[0]) + "," +
             std::to_string(m.h[1]) + "," + std::to_string(m.h[2]) + "," + std::to_string(m.h[3]) + "," +
             std::to_string(b47) + "," + std::to_string(b815) + "," + std::to_string(b16) + "]}}";
        return s;
    }
};

struct Agg {
    uint64_t textures = 0, texturesEqual = 0;
    uint64_t blocks0 = 0, blocksEq0 = 0, blocksAll = 0, blocksEqAll = 0;
    // EXE-vs-EXE over the same textures, mip 0, where it is known (textures present in both EXE sessions)
    uint64_t ceilTextures = 0, ceilTexels0 = 0, ceilEqual0 = 0;
    Decoded mip0, all;
    void add(const Agg& o) {
        textures += o.textures; texturesEqual += o.texturesEqual;
        blocks0 += o.blocks0; blocksEq0 += o.blocksEq0; blocksAll += o.blocksAll; blocksEqAll += o.blocksEqAll;
        ceilTextures += o.ceilTextures; ceilTexels0 += o.ceilTexels0; ceilEqual0 += o.ceilEqual0;
        mip0.add(o.mip0);
        all.add(o.all);
    }
    std::string json() const {
        auto share = [](uint64_t a, uint64_t b) { return fmt(b ? static_cast<double>(a) / b : 0); };
        return "{\"textures\":" + std::to_string(textures) + ",\"textures_equal\":" + std::to_string(texturesEqual) +
               ",\"bc1_blocks_equal_share_mip0\":" + share(blocksEq0, blocks0) + ",\"bc1_blocks_equal_share_all_mips\":" +
               share(blocksEqAll, blocksAll) + ",\"blocks_mip0\":" + std::to_string(blocks0) + ",\"blocks_all_mips\":" +
               std::to_string(blocksAll) + ",\"decoded_mip0\":" + mip0.json() + ",\"decoded_all_mips\":" + all.json() +
               ",\"ceiling_textures\":" + std::to_string(ceilTextures) +
               ",\"ceiling_exe_vs_exe_mip0_texel_equal_share\":" +
               (ceilTexels0 ? share(ceilEqual0, ceilTexels0) : std::string("null")) + "}";
    }
};

// ---- the reference set

struct Record {
    std::string file;
    uint32_t cache = 0, tileIndex = 0, x = 0, y = 0, w = 0, mips = 0, fmtCode = 0;
    uint64_t bytes = 0;
    uint32_t sizeClass = 0, f2fc = 0, state = 0;
    int f300 = -1;  // tile+0x300 when the recording has it (4106), else -1
    uint32_t gen1cc = 0, gen1d0 = 0;  // tile+0x1CC (content changed, frame) / +0x1D0 (cache baked, frame) (4106)
    std::vector<uint32_t> layers;
    bool ok = true;
    std::string why;
};

bool parseRecords(const Json& idx, std::vector<Record>& out, std::string& err) {
    const Json* tex = idx.get("textures");
    if (!tex || tex->kind != Json::Array) { err = "raw_index.json has no \"textures\" array"; return false; }
    for (const Json& e : tex->a) {
        Record r;
        double v = 0;
        if (e.kind != Json::Object || !e.str("file", r.file)) { r.ok = false; r.why = "record without a file name"; out.push_back(r); continue; }
        auto u = [&](const Json& o, const char* k, uint32_t& dst) {
            if (!o.num(k, v) || v < 0 || v > 4294967295.0) return false;
            dst = static_cast<uint32_t>(v);
            return true;
        };
        const Json* t = e.get("tile");
        double bytesV = 0;
        if (!u(e, "cache", r.cache) || !u(e, "tileIndex", r.tileIndex) || !u(e, "tileX", r.x) || !u(e, "tileY", r.y) ||
            !u(e, "w", r.w) || !u(e, "mips", r.mips) || !u(e, "fmt", r.fmtCode) || !e.num("bytes", bytesV) || !t ||
            !u(*t, "size_17c", r.sizeClass) || !u(*t, "f2fc", r.f2fc)) {
            r.ok = false;
            r.why = "record fields missing";
        }
        r.bytes = bytesV > 0 ? static_cast<uint64_t>(bytesV) : 0;
        if (t) {
            u(*t, "s310", r.state);
            uint32_t f300 = 0;
            if (u(*t, "f300", f300)) r.f300 = static_cast<int>(f300);
            u(*t, "gen_1cc", r.gen1cc);
            u(*t, "gen_1d0", r.gen1d0);
            const Json* L = t->get("layers");
            if (L && L->kind == Json::Array)
                for (const Json& l : L->a) if (l.kind == Json::Number) r.layers.push_back(static_cast<uint32_t>(l.n));
        }
        out.push_back(r);
    }
    return true;
}

// ---- D3D11 helpers

bool readBc1(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* tex, std::vector<uint8_t>& out, std::string& err) {
    out.clear();
    D3D11_TEXTURE2D_DESC d;
    tex->GetDesc(&d);
    D3D11_TEXTURE2D_DESC s = d;
    s.Usage = D3D11_USAGE_STAGING;
    s.BindFlags = 0;
    s.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    s.MiscFlags = 0;
    ID3D11Texture2D* st = nullptr;
    if (FAILED(dev->CreateTexture2D(&s, nullptr, &st))) { err = "CreateTexture2D (staging)"; return false; }
    ctx->CopyResource(st, tex);
    bool ok = true;
    for (uint32_t m = 0; m < d.MipLevels && ok; ++m) {
        D3D11_MAPPED_SUBRESOURCE map;
        if (FAILED(ctx->Map(st, m, D3D11_MAP_READ, 0, &map))) { err = "Map (staging)"; ok = false; break; }
        const uint32_t blocks = bc1MipBlocks(d.Width, m);
        for (uint32_t r = 0; r < blocks; ++r) {
            const uint8_t* row = static_cast<const uint8_t*>(map.pData) + static_cast<size_t>(r) * map.RowPitch;
            out.insert(out.end(), row, row + blocks * 8);
        }
        ctx->Unmap(st, m);
    }
    st->Release();
    return ok;
}

// ---- comparison of one texture

struct TexResult {
    uint32_t blocks0 = 0, blocksEq0 = 0, blocksAll = 0, blocksEqAll = 0;
    Decoded mip0, all;
    double decEq0 = 0;  // texel equal share mip 0
    uint32_t decMax0 = 0, decP95_0 = 0;
};

void compareTexture(const std::vector<uint8_t>& srv, const std::vector<uint8_t>& exe, uint32_t size, uint32_t mips,
                    TexResult& r, std::vector<uint8_t>* srvRgb0, std::vector<uint8_t>* exeRgb0) {
    std::vector<uint8_t> a, b;
    for (uint32_t m = 0; m < mips; ++m) {
        const uint32_t blocks = bc1MipBlocks(size, m);
        const size_t off = bc1MipOffset(size, m);
        uint32_t eq = 0;
        for (uint32_t i = 0; i < blocks * blocks; ++i)
            if (std::memcmp(&srv[off + i * 8], &exe[off + i * 8], 8) == 0) ++eq;
        r.blocksAll += blocks * blocks;
        r.blocksEqAll += eq;
        if (m == 0) { r.blocks0 = blocks * blocks; r.blocksEq0 = eq; }
        bc1DecodeMip(&srv[off], blocks, a);
        bc1DecodeMip(&exe[off], blocks, b);
        Decoded d;
        for (size_t t = 0; t < a.size() / 3; ++t) {
            uint32_t mx = 0;
            for (int k = 0; k < 3; ++k) {
                const uint32_t dd = static_cast<uint32_t>(std::abs(static_cast<int>(a[t * 3 + k]) - static_cast<int>(b[t * 3 + k])));
                d.ch[k].h[dd]++;
                mx = std::max(mx, dd);
            }
            d.maxc.h[mx]++;
        }
        r.all.add(d);
        if (m == 0) {
            r.mip0 = d;
            if (srvRgb0) *srvRgb0 = a;
            if (exeRgb0) *exeRgb0 = b;
        }
    }
    const uint64_t n0 = r.mip0.maxc.n();
    r.decEq0 = n0 ? static_cast<double>(r.mip0.maxc.h[0]) / n0 : 0;
    r.decMax0 = r.mip0.maxc.max();
    r.decP95_0 = r.mip0.maxc.pct(0.95);
}

bool writeDiffBmp(const std::string& path, const std::vector<uint8_t>& s, const std::vector<uint8_t>& e, uint32_t w) {
    const uint32_t W = w * 3, H = w;
    const uint32_t rowBytes = (W * 3 + 3) & ~3u;
    std::vector<uint8_t> f(54 + static_cast<size_t>(rowBytes) * H, 0);
    auto put32 = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) f[at + i] = static_cast<uint8_t>(v >> (8 * i)); };
    f[0] = 'B'; f[1] = 'M';
    put32(2, static_cast<uint32_t>(f.size())); put32(10, 54); put32(14, 40); put32(18, W); put32(22, H);
    f[26] = 1; f[28] = 24; put32(34, static_cast<uint32_t>(f.size() - 54));
    for (uint32_t y = 0; y < H; ++y) {
        uint8_t* row = &f[54 + static_cast<size_t>(H - 1 - y) * rowBytes];
        for (uint32_t x = 0; x < w; ++x) {
            const uint8_t* ps = &s[(static_cast<size_t>(y) * w + x) * 3];
            const uint8_t* pe = &e[(static_cast<size_t>(y) * w + x) * 3];
            for (int k = 0; k < 3; ++k) {
                row[(x) * 3 + 2 - k] = ps[k];
                row[(w + x) * 3 + 2 - k] = pe[k];
                const int d = std::min(255, std::abs(static_cast<int>(ps[k]) - static_cast<int>(pe[k])) * 8);
                row[(2 * w + x) * 3 + 2 - k] = static_cast<uint8_t>(d);
            }
        }
    }
    return writeText(path, std::string(f.begin(), f.end()));
}

struct Row {
    const Record* rec = nullptr;
    TexResult res;
    std::string cls;
    bool listEqual = true, fair = false, ceilKnown = false;
    double ceilEq0 = 1;
    uint32_t ceilMax0 = 0;
    int bestStyle = -1;
    uint32_t style = 0;  // the style the baker's rule gave
    double bestStyleBlocks0 = -1;
    std::vector<uint8_t> srvRgb0, exeRgb0;  // only kept for the worst rows' images
    // 4106: the tile's decals as baked (count, pass-1 count, pairs of equal priority whose rectangles overlap, a river or
    // flood-plain decal among them), the server's t8 decision and areaChanged
    uint32_t decals = 0, pass1 = 0, tiePairs = 0;
    bool riverDecal = false, srvT8 = false, srvArea = false, srvByList = false;
};

// One baked tile's t8 decision against the EXE's (4106, P14 / 0305).
struct T8Fact {
    uint32_t x = 0, y = 0, f2fc = 0, decals = 0;
    int f300 = -1;
    bool exeT8 = false, srvT8 = false, srvByList = false, srvArea = false;
};

// The tile's decal facts for the CSV and the classes (4106).
void decalFacts(const decal::TileDecalList& L, uint32_t& n, uint32_t& pass1, uint32_t& tiePairs, bool& river) {
    n = static_cast<uint32_t>(L.indices.size());
    pass1 = tiePairs = 0;
    river = false;
    if (!L.set) return;
    const decal::DecalSet& S = *L.set;
    for (size_t i = 0; i < L.indices.size(); ++i) {
        const decal::Decal& a = S.decals[L.indices[i]];
        pass1 += a.pass == 1;
        for (uint32_t k = 0; k < 3; ++k) {
            if (a.texture[k] >= S.textures.size()) continue;
            std::string nm = S.textures[a.texture[k]];
            std::transform(nm.begin(), nm.end(), nm.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
            if (nm.compare(0, 9, "riverbank") == 0 || nm.compare(0, 11, "floodplains") == 0) river = true;
        }
        for (size_t j = i + 1; j < L.indices.size(); ++j) {
            const decal::Decal& b = S.decals[L.indices[j]];
            if (b.priority != a.priority || b.pass != a.pass) continue;
            if (a.rect[0] <= b.rect[2] && b.rect[0] <= a.rect[2] && a.rect[1] <= b.rect[3] && b.rect[1] <= a.rect[3]) ++tiePairs;
        }
    }
}

// farm_types.xml as the tap's references name it (terrain-decals.md 8.3): "<category>/<child index>/<attribute>" (lower
// case) -> the attribute's value. Categories are the root's child elements, children their child elements in order. A
// small reader of the file's plain XML (elements, attributes in double or single quotes, comments, declarations); the
// server's own resolution is 4105's (srv_residency_source.cpp); this one is the comparator's, written from 8.3.
bool parseFarmTypes(const std::string& x, std::map<std::string, std::string>& out, std::string& why) {
    out.clear();
    std::vector<std::string> open;
    std::map<std::string, int> childCount;
    size_t i = 0;
    auto lower = [](std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
    while ((i = x.find('<', i)) != std::string::npos) {
        if (x.compare(i, 4, "<!--") == 0) {
            const size_t e = x.find("-->", i + 4);
            if (e == std::string::npos) { why = "comment not closed"; return false; }
            i = e + 3;
            continue;
        }
        const size_t e = x.find('>', i);
        if (e == std::string::npos) { why = "tag not closed"; return false; }
        std::string tag = x.substr(i + 1, e - i - 1);
        i = e + 1;
        if (tag.empty() || tag[0] == '?' || tag[0] == '!') continue;
        if (tag[0] == '/') {
            if (open.empty()) { why = "closing tag without an open element"; return false; }
            open.pop_back();
            continue;
        }
        const bool selfClose = tag.back() == '/';
        if (selfClose) tag.pop_back();
        size_t k = 0;
        while (k < tag.size() && !std::isspace(static_cast<unsigned char>(tag[k]))) ++k;
        const std::string name = tag.substr(0, k);
        if (open.size() == 2) {   // a child of a category
            const std::string cat = lower(open[1]);
            const int idx = childCount[cat]++;
            while (k < tag.size()) {
                while (k < tag.size() && std::isspace(static_cast<unsigned char>(tag[k]))) ++k;
                const size_t eq = tag.find('=', k);
                if (eq == std::string::npos) break;
                std::string an = tag.substr(k, eq - k);
                while (!an.empty() && std::isspace(static_cast<unsigned char>(an.back()))) an.pop_back();
                size_t q = eq + 1;
                while (q < tag.size() && std::isspace(static_cast<unsigned char>(tag[q]))) ++q;
                if (q >= tag.size() || (tag[q] != '"' && tag[q] != '\'')) { why = "attribute without quotes in <" + name + ">"; return false; }
                const size_t qe = tag.find(tag[q], q + 1);
                if (qe == std::string::npos) { why = "attribute value not closed in <" + name + ">"; return false; }
                out[cat + "/" + std::to_string(idx) + "/" + lower(an)] = tag.substr(q + 1, qe - q - 1);
                k = qe + 1;
            }
        }
        if (!selfClose) open.push_back(name);
    }
    if (out.empty()) { why = "no category child with attributes"; return false; }
    return true;
}

// The bare file name of a path ("assets/Decals/Crops/Crops_Africa_03_D.dds" -> "Crops_Africa_03_D.dds").
std::string bareName(const std::string& p) {
    const size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

// Hypotheses for the EXE's selection order (terrain-decals.md 1.4: small decals from the spatial index, large ones from
// the large list; the sort is stable below 33 entries): within one priority, the small decals (both sides of the
// rectangle at most 256 samples, the large-decal constant) in descending list order (newest first) and the large ones in
// list order, small first (largeFirst false) or large first (true).
void orderTiesBySize(decal::TileDecalList& L, bool largeFirst) {
    if (!L.set) return;
    const decal::DecalSet& S = *L.set;
    auto large = [&](uint32_t i) {
        const decal::Decal& d = S.decals[i];
        return d.rect[2] - d.rect[0] > 256.0f || d.rect[3] - d.rect[1] > 256.0f;
    };
    std::stable_sort(L.indices.begin(), L.indices.end(), [&](uint32_t a, uint32_t b) {
        const decal::Decal& A = S.decals[a];
        const decal::Decal& B = S.decals[b];
        if (A.priority != B.priority) return A.priority < B.priority;
        const bool la = large(a), lb = large(b);
        if (la != lb) return largeFirst ? la : lb;
        if (la) return A.listOrder < B.listOrder;
        return A.listOrder > B.listOrder;
    });
}

// 4111: the order before 4111: ascending priority, ties by list order only.
void orderTiesByList(decal::TileDecalList& L) {
    if (!L.set) return;
    const decal::DecalSet& S = *L.set;
    std::stable_sort(L.indices.begin(), L.indices.end(), [&](uint32_t a, uint32_t b) {
        if (S.decals[a].priority != S.decals[b].priority) return S.decals[a].priority < S.decals[b].priority;
        return S.decals[a].listOrder < S.decals[b].listOrder;
    });
}

// Reverses each run of equal priorities in a tile's list (the list is in ascending priority, ties by list order).
void reverseTies(decal::TileDecalList& L) {
    if (!L.set) return;
    size_t i = 0;
    while (i < L.indices.size()) {
        size_t j = i + 1;
        const float p = L.set->decals[L.indices[i]].priority;
        while (j < L.indices.size() && L.set->decals[L.indices[j]].priority == p) ++j;
        std::reverse(L.indices.begin() + static_cast<std::ptrdiff_t>(i), L.indices.begin() + static_cast<std::ptrdiff_t>(j));
        i = j;
    }
}

int usage(const char* why) {
    std::fprintf(stderr, "bakecompare: %s\nusage: bakecompare --ref SAVE_DIR --game-dir INSTALL --exe EXE --out DIR "
                         "[--arm armB] [--limit N] [--tiles X,Y;...] [--diff-images N] [--style-probe] [--t8-rule exe|list|decals] [--sources exe|standin] [--plot-art FILE] "
                         "[--decals FILE.dtap] [--mods DIR]... [--tie-order list|reverse] [--tile-flags FILE] [--quiet]\n", why);
    return kUsage;
}

}  // namespace

int main(int argc, char** argv) {
    std::string ref, gameDir, exePath, outDir, arm = "armB", tilesArg;
    long limit = -1, diffImages = 0;
    bool styleProbe = false, quiet = false, t8FromExe = true, standIn = false, t8FromDecals = false, t8FromTap = false;
    bool tieReverse = false, tieGiven = false;
    std::string tieMode = "list";
    std::set<uint32_t> dropOrders;
    std::vector<std::string> bgraNames;   // 4111: --decal-bgra
    std::string plotArtPath, decalsPath, tileFlagsPath;
    std::vector<std::string> modDirs;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](std::string& dst) {
            if (i + 1 >= argc) return false;
            dst = argv[++i];
            return true;
        };
        std::string v;
        if (a == "--ref") { if (!next(ref)) return usage("--ref needs a value"); }
        else if (a == "--game-dir") { if (!next(gameDir)) return usage("--game-dir needs a value"); }
        else if (a == "--exe") { if (!next(exePath)) return usage("--exe needs a value"); }
        else if (a == "--out") { if (!next(outDir)) return usage("--out needs a value"); }
        else if (a == "--arm") { if (!next(arm)) return usage("--arm needs a value"); }
        else if (a == "--tiles") { if (!next(tilesArg)) return usage("--tiles needs a value"); }
        else if (a == "--limit" || a == "--diff-images") {
            if (!next(v)) return usage((a + " needs a value").c_str());
            char* end = nullptr;
            long n = std::strtol(v.c_str(), &end, 10);
            if (end == v.c_str() || *end || n < 0) return usage((a + ": not a count: " + v).c_str());
            (a == "--limit" ? limit : diffImages) = n;
        }
        else if (a == "--style-probe") styleProbe = true;
        else if (a == "--sources") {
            if (!next(v) || (v != "exe" && v != "standin")) return usage("--sources takes exe or standin");
            standIn = v == "standin";
        }
        else if (a == "--plot-art") { if (!next(plotArtPath)) return usage("--plot-art needs a value"); }
        else if (a == "--t8-rule") {
            if (!next(v) || (v != "exe" && v != "list" && v != "decals" && v != "tap"))
                return usage("--t8-rule takes exe, list, decals or tap");
            t8FromExe = v == "exe";
            t8FromDecals = v == "decals" || v == "tap";
            t8FromTap = v == "tap";
        }
        else if (a == "--decals") { if (!next(decalsPath)) return usage("--decals needs a value"); }
        else if (a == "--tile-flags") { if (!next(tileFlagsPath)) return usage("--tile-flags needs a value"); }
        else if (a == "--drop-decals") {   // diagnostic: leave out the decals of these list orders (comma-separated)
            if (!next(v)) return usage("--drop-decals needs a value");
            std::stringstream ss(v);
            std::string item;
            while (std::getline(ss, item, ',')) {
                char* end = nullptr;
                const unsigned long o = std::strtoul(item.c_str(), &end, 10);
                if (item.empty() || *end) return usage(("--drop-decals: not a list order: " + item).c_str());
                dropOrders.insert(static_cast<uint32_t>(o));
            }
        }
        else if (a == "--decal-bgra") {   // 4111, diagnostic
            if (!next(v) || v.empty()) return usage("--decal-bgra needs a value");
            std::stringstream ss(v);
            std::string item;
            while (std::getline(ss, item, ',')) {
                if (item.empty()) return usage("--decal-bgra: an empty name");
                bgraNames.push_back(item);
            }
        }
        else if (a == "--mods") {
            if (!next(v)) return usage("--mods needs a value");
            modDirs.push_back(v);
        }
        else if (a == "--tie-order") {
            if (!next(v) || (v != "list" && v != "reverse" && v != "small-rev" && v != "large-first" && v != "listorder" &&
                             v != "rule4111"))
                return usage("--tie-order takes list, reverse, small-rev, large-first, listorder or rule4111");
            tieReverse = v == "reverse";
            tieMode = v;
            tieGiven = true;
        }
        else if (a == "--quiet") quiet = true;
        else return usage(("unknown argument " + a).c_str());
    }
    if (ref.empty() || gameDir.empty() || exePath.empty() || outDir.empty())
        return usage("--ref, --game-dir, --exe and --out are required");
    if (decalsPath.empty() && t8FromDecals) return usage(t8FromTap ? "--t8-rule tap needs --decals" : "--t8-rule decals needs --decals");
    if (decalsPath.empty() && tieGiven) return usage("--tie-order needs --decals");
    if (decalsPath.empty() && !dropOrders.empty()) return usage("--drop-decals needs --decals");
    if (decalsPath.empty() && !bgraNames.empty()) return usage("--decal-bgra needs --decals");
    std::set<std::pair<uint32_t, uint32_t>> onlyTiles;
    if (!tilesArg.empty()) {
        std::stringstream ss(tilesArg);
        std::string item;
        while (std::getline(ss, item, ';')) {
            unsigned x = 0, y = 0;
            char c = 0;
            std::stringstream is(item);
            if (!(is >> x >> c >> y) || c != ',') return usage(("--tiles: bad item '" + item + "'").c_str());
            onlyTiles.insert(std::make_pair(x, y));
        }
    }

    // ---- 4106: the mod folders, the session's decals (through the server's reader) and the EXE's tile flags, before
    // the reference
    for (const std::string& m : modDirs)
        if (!isDir(m)) { std::fprintf(stderr, "bakecompare: mod folder %s missing\n", m.c_str()); return kMissing; }
    srvrender::DecalTapReader decalReader(0);
    std::shared_ptr<const decal::DecalSet> decalSet;
    std::set<std::pair<int, int>> areaTiles;   // tile origins of tilesForRects(the first set's change): 4105's areaChanged
    std::string decalSummary = "null";
    if (!decalsPath.empty()) {
        if (!isFile(decalsPath)) { std::fprintf(stderr, "bakecompare: decal file %s missing\n", decalsPath.c_str()); return kMissing; }
        Status ds = decalReader.loadFile(gamefs::fromUtf8(decalsPath));
        if (!ds.ok()) {
            std::fprintf(stderr, "bakecompare: decal file %s refused by the server's reader: %s (%s)\n", decalsPath.c_str(),
                         ds.str().c_str(), decalReader.lastWhy().c_str());
            return ds.code == Err::Io ? kMissing : kParse;
        }
        decalSet = decalReader.current();
        std::vector<srvrender::DecalChange> ch;
        decalReader.drainChanges(ch);
        size_t rects = 0;
        for (const srvrender::DecalChange& c : ch) {
            rects += c.rects.size();
            if (!t8FromTap)
                for (const decal::TileOrigin& o : decal::tilesForRects(c.rects.data(), c.rects.size(), c.worldRect))
                    areaTiles.insert(std::make_pair(o.x, o.y));
        }
        if (t8FromTap) {   // 4111: the EXE's tile+0x300 from the tap's area block
            if (!decalSet->areaTilesX || !decalSet->areaTilesY) {
                std::fprintf(stderr, "bakecompare: --t8-rule tap: %s carries no area block (a tap before 4111)\n", decalsPath.c_str());
                return kInconsistent;
            }
            for (uint32_t y = 0; y < decalSet->areaTilesY; ++y)
                for (uint32_t x = 0; x < decalSet->areaTilesX; ++x) {
                    bool known = false;
                    if (decal::areaChangedAt(*decalSet, static_cast<int>(x), static_cast<int>(y), known) && known)
                        areaTiles.insert(std::make_pair(static_cast<int>(x) * decal::kTileSamples, static_cast<int>(y) * decal::kTileSamples));
                }
        }
        uint32_t pass1 = 0;
        for (const decal::Decal& d : decalSet->decals) pass1 += d.pass == 1;
        decalSummary = "{\"file\":" + jsonStr(decalsPath) + ",\"generation\":" + std::to_string(decalSet->generation) +
                       ",\"load_seq\":" + std::to_string(decalSet->loadSeq) + ",\"decals\":" +
                       std::to_string(decalSet->decals.size()) + ",\"pass1\":" + std::to_string(pass1) + ",\"names\":" +
                       std::to_string(decalSet->textures.size()) + ",\"vertices\":" + std::to_string(decalSet->vertices.size()) +
                       ",\"first_change_rects\":" + std::to_string(rects) + ",\"area_block\":" +
                       std::to_string(decalSet->areaTilesX) + "," + "\"area_block_y\":" + std::to_string(decalSet->areaTilesY) +
                       ",\"area_from\":" + jsonStr(t8FromTap ? "tap" : "tilesForRects") + ",\"area_changed_tiles\":" +
                       std::to_string(areaTiles.size()) + ",\"tie_order\":" + jsonStr(tieMode) + ",\"dropped_list_orders\":" +
                       std::to_string(dropOrders.size()) + ",\"walk_cols\":" + std::to_string(decalSet->walk.cols) +
                       ",\"walk_rows\":" + std::to_string(decalSet->walk.rows) + ",\"walk_large\":" +
                       std::to_string(decalSet->walk.large.size()) + "}";
    }
    std::map<std::pair<uint32_t, uint32_t>, std::pair<int, int>> tileFlags;   // (x, y) -> (+0x2FC, +0x300) of every tile
    if (!tileFlagsPath.empty()) {
        std::vector<uint8_t> tb;
        if (!isFile(tileFlagsPath) || !readFile(tileFlagsPath, tb)) {
            std::fprintf(stderr, "bakecompare: tile flags %s missing or unreadable\n", tileFlagsPath.c_str());
            return kMissing;
        }
        Json tj;
        JsonParser tp;
        std::string terr;
        if (!tp.parse(std::string(tb.begin(), tb.end()), tj, terr)) {
            std::fprintf(stderr, "bakecompare: %s: %s\n", tileFlagsPath.c_str(), terr.c_str());
            return kParse;
        }
        const Json* tl = tj.get("tiles");
        if (!tl || tl->kind != Json::Array || tl->a.empty()) {
            std::fprintf(stderr, "bakecompare: %s has no \"tiles\" array\n", tileFlagsPath.c_str());
            return kParse;
        }
        for (const Json& e : tl->a) {
            double x = 0, y = 0, a2 = 0, a3 = 0;
            if (!e.num("tx", x) || !e.num("ty", y) || !e.num("f2fc", a2) || !e.num("f300", a3) || x < 0 || y < 0) {
                std::fprintf(stderr, "bakecompare: %s: a tile without tx, ty, f2fc, f300\n", tileFlagsPath.c_str());
                return kParse;
            }
            tileFlags[std::make_pair(static_cast<uint32_t>(x), static_cast<uint32_t>(y))] =
                std::make_pair(static_cast<int>(a2), static_cast<int>(a3));
        }
    }

    // ---- inputs
    if (!isDir(ref)) { std::fprintf(stderr, "bakecompare: reference folder %s missing\n", ref.c_str()); return kMissing; }
    const std::string armDir = ref + "/" + arm;
    std::vector<uint8_t> bytes;
    Json manifest, index, fairJ, decodedJ;
    JsonParser jp;
    std::string err;
    auto loadJson = [&](const std::string& path, Json& j, bool required, bool& present) -> int {
        present = false;
        if (!isFile(path)) {
            if (!required) return kOk;
            std::fprintf(stderr, "bakecompare: %s missing\n", path.c_str());
            return kMissing;
        }
        if (!readFile(path, bytes)) { std::fprintf(stderr, "bakecompare: cannot read %s\n", path.c_str()); return kMissing; }
        if (!jp.parse(std::string(bytes.begin(), bytes.end()), j, err)) {
            std::fprintf(stderr, "bakecompare: %s: %s\n", path.c_str(), err.c_str());
            return kParse;
        }
        present = true;
        return kOk;
    };
    bool have = false, haveFair = false, haveDecoded = false;
    if (int rc = loadJson(ref + "/manifest.json", manifest, true, have)) return rc;
    if (int rc = loadJson(armDir + "/caches/raw_index.json", index, true, have)) return rc;
    if (int rc = loadJson(ref + "/equal_between_sessions.json", fairJ, false, haveFair)) return rc;
    std::string decodedPath;
    {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(gamefs::fromUtf8(ref + "/decoded_*.json").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            decodedPath = ref + "/" + gamefs::toUtf8(fd.cFileName);
            FindClose(h);
        }
    }
    if (!decodedPath.empty())
        if (int rc = loadJson(decodedPath, decodedJ, false, haveDecoded)) return rc;
    std::string save;
    if (!manifest.str("save", save)) { std::fprintf(stderr, "bakecompare: manifest.json has no \"save\"\n"); return kParse; }
    std::vector<Record> recs;
    if (!parseRecords(index, recs, err)) { std::fprintf(stderr, "bakecompare: %s\n", err.c_str()); return kParse; }
    if (recs.empty()) { std::fprintf(stderr, "bakecompare: raw_index.json lists no texture\n"); return kInconsistent; }
    std::set<std::pair<uint32_t, uint32_t>> fair;  // (tileIndex, cache) byte-identical between two EXE sessions
    if (haveFair) {
        const Json* c = fairJ.get("caches");
        if (!c || c->kind != Json::Array) { std::fprintf(stderr, "bakecompare: equal_between_sessions.json has no \"caches\"\n"); return kParse; }
        for (const Json& e : c->a) {
            double t = 0, k = 0;
            if (!e.num("tile", t) || !e.num("cache", k)) { std::fprintf(stderr, "bakecompare: equal_between_sessions.json: bad entry\n"); return kParse; }
            fair.insert(std::make_pair(static_cast<uint32_t>(t), static_cast<uint32_t>(k)));
        }
    }
    std::map<std::pair<uint32_t, uint32_t>, std::pair<double, uint32_t>> ceilPer;  // differing: (equal share, max)
    double ceilAll = -1;
    if (haveDecoded) {
        decodedJ.num("texels_equal_share", ceilAll);
        const Json* p = decodedJ.get("per_texture");
        if (!p || p->kind != Json::Array) { std::fprintf(stderr, "bakecompare: %s has no \"per_texture\"\n", decodedPath.c_str()); return kParse; }
        for (const Json& e : p->a) {
            double t = 0, k = 0, es = 0, mx = 0;
            if (!e.num("tile", t) || !e.num("cache", k) || !e.num("equal_share", es) || !e.num("max", mx)) {
                std::fprintf(stderr, "bakecompare: %s: bad per_texture entry\n", decodedPath.c_str());
                return kParse;
            }
            ceilPer[std::make_pair(static_cast<uint32_t>(t), static_cast<uint32_t>(k))] = std::make_pair(es, static_cast<uint32_t>(mx));
        }
    }
    const bool ceilingKnown = haveFair && haveDecoded;

    const std::string hfPath = armDir + "/heightfield.c5hf";
    if (!isFile(hfPath)) { std::fprintf(stderr, "bakecompare: %s missing\n", hfPath.c_str()); return kMissing; }
    if (!readFile(hfPath, bytes)) { std::fprintf(stderr, "bakecompare: cannot read %s\n", hfPath.c_str()); return kMissing; }
    heightfield::HeightGrid grid;
    Status st = heightfield::readTap(bytes.data(), bytes.size(), grid);
    if (!st.ok()) { std::fprintf(stderr, "bakecompare: %s: %s\n", hfPath.c_str(), st.str().c_str()); return kParse; }
    bytes.clear();
    bytes.shrink_to_fit();
    if (grid.mat.empty()) { std::fprintf(stderr, "bakecompare: %s has no material bytes\n", hfPath.c_str()); return kInconsistent; }
    {
        uint32_t maxX = 0, maxY = 0;
        for (const Record& r : recs) if (r.ok) { maxX = std::max(maxX, r.x); maxY = std::max(maxY, r.y); }
        if (maxX >= grid.mapW || maxY >= grid.mapH) {
            std::fprintf(stderr, "bakecompare: the index has tile (%u, %u) beyond the height field's %u x %u tiles\n", maxX,
                         maxY, grid.mapW, grid.mapH);
            return kInconsistent;
        }
    }
    // The plots' continent art types (the DLL's Plot:GetContinentArtType; "W H v,v,v..." row-major, W = the grid's
    // tiles in x, H = the DLL's rows = the grid's mapH - 4): the tile style through 3406's tileStyle.
    std::vector<uint8_t> plotArt;
    uint32_t plotArtW = 0, plotArtH = 0;
    if (!plotArtPath.empty()) {
        if (!isFile(plotArtPath) || !readFile(plotArtPath, bytes)) {
            std::fprintf(stderr, "bakecompare: plot art file %s missing or unreadable\n", plotArtPath.c_str());
            return kMissing;
        }
        std::stringstream ss(std::string(bytes.begin(), bytes.end()));
        long w = -1, h = -1;
        ss >> w >> h;
        std::string cell;
        bool bad = !ss || w <= 0 || h <= 0 || w > 4096 || h > 4096;
        while (!bad && std::getline(ss >> std::ws, cell, ',')) {
            char* end = nullptr;
            const long v = std::strtol(cell.c_str(), &end, 10);
            while (end && (*end == ' ' || *end == '\r' || *end == '\n')) ++end;
            if (end == cell.c_str() || (end && *end) || v < 0 || v > 255) { bad = true; break; }
            plotArt.push_back(static_cast<uint8_t>(v));
        }
        if (bad || plotArt.size() != static_cast<size_t>(w) * static_cast<size_t>(h)) {
            std::fprintf(stderr, "bakecompare: plot art file %s: not \"W H\" and W * H comma-separated values 0..255\n",
                         plotArtPath.c_str());
            return kParse;
        }
        plotArtW = static_cast<uint32_t>(w);
        plotArtH = static_cast<uint32_t>(h);
        if (plotArtW != grid.mapW || plotArtH > grid.mapH) {
            std::fprintf(stderr, "bakecompare: plot art %u x %u does not fit the height field's %u x %u tiles\n", plotArtW,
                         plotArtH, grid.mapW, grid.mapH);
            return kInconsistent;
        }
    }
    if (!isDir(gameDir) || !isFile(exePath)) {
        std::fprintf(stderr, "bakecompare: install %s or EXE %s missing\n", gameDir.c_str(), exePath.c_str());
        return kMissing;
    }
    if (!makeDirs(outDir)) { std::fprintf(stderr, "bakecompare: cannot create %s\n", outDir.c_str()); return kMissing; }

    gamefs::GameFiles fs;
    gamefs::GameFiles::Options fsOpt;
    for (const std::string& m : modDirs) fsOpt.extraLooseRoots.push_back(gamefs::fromUtf8(m));
    st = fs.open(gamefs::fromUtf8(gameDir), fsOpt);
    if (!st.ok()) { std::fprintf(stderr, "bakecompare: install: %s\n", st.str().c_str()); return kMissing; }
    // 4106: the set's names as the decal pass loads them: the tap's farm references resolved through the farm_types.xml
    // the install and the mods give (8.3), every name checked against the install view (a name it lacks is listed in the
    // report; the tiles that draw it are skipped as "bake failed: NotFound").
    std::string namesJson = "null";
    if (decalSet) {
        decal::DecalSet resolved = *decalSet;
        std::map<std::string, std::string> farm;
        std::string farmOrigin, farmWhy;
        bool farmRead = false;
        uint32_t refs = 0;
        std::vector<std::string> unresolved;
        for (std::string& nm : resolved.textures) {
            if (!nm.empty() && nm[0] == '@') {
                if (!farmRead) {
                    std::vector<uint8_t> fx;
                    Status fst = fs.read("farm_types.xml", fx, &farmOrigin);
                    if (!fst.ok() || !parseFarmTypes(std::string(fx.begin(), fx.end()), farm, farmWhy)) {
                        std::fprintf(stderr, "bakecompare: farm_types.xml (%s): %s\n", farmOrigin.c_str(), fst.ok() ? farmWhy.c_str() : fst.str().c_str());
                        return kInconsistent;
                    }
                    farmRead = true;
                }
                static const std::string kRef = "@farm_types.xml/";
                std::string key = nm.size() > kRef.size() ? nm.substr(kRef.size()) : "";
                const size_t a = key.find('/'), b = a == std::string::npos ? a : key.find('/', a + 1);
                std::string low;
                if (a != std::string::npos && b != std::string::npos) {
                    for (char c : key.substr(0, a)) low += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    low += "/" + std::to_string(std::atoi(key.substr(a + 1, b - a - 1).c_str())) + "/";
                    for (char c : key.substr(b + 1)) low += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                auto it = farm.find(low);
                if (nm.compare(0, kRef.size(), kRef) != 0 || it == farm.end() || it->second.empty()) {
                    std::fprintf(stderr, "bakecompare: decal name %s names nothing in farm_types.xml (%s)\n", nm.c_str(), farmOrigin.c_str());
                    return kInconsistent;
                }
                nm = bareName(it->second);
                ++refs;
            }
            std::vector<uint8_t> probe;
            if (!fs.read(bareName(nm), probe).ok()) unresolved.push_back(nm);
        }
        if (tieMode == "rule4111") resolved.walk = decal::DecalWalk();   // 0366: the 4111 rule on the same set
        decal::buildIndex(resolved);
        decalSet = std::make_shared<const decal::DecalSet>(std::move(resolved));
        namesJson = "{\"names\":" + std::to_string(decalSet->textures.size()) + ",\"farm_references_resolved\":" +
                    std::to_string(refs) + ",\"farm_types\":" + jsonStr(farmOrigin) + ",\"not_in_install\":[";
        for (size_t k = 0; k < unresolved.size(); ++k) namesJson += std::string(k ? "," : "") + jsonStr(unresolved[k]);
        namesJson += "]}";
        if (!unresolved.empty())
            std::fprintf(stderr, "bakecompare: %zu decal texture names not in the install view (first: %s)\n", unresolved.size(),
                         unresolved[0].c_str());
    }
    bake::LayerTables tables;
    std::string tdOrigin;
    st = bake::loadTerrainDef(fs, tables, &tdOrigin);
    if (st.code == Err::NotFound || st.code == Err::Io) { std::fprintf(stderr, "bakecompare: TerrainDef.csv: %s\n", st.str().c_str()); return kMissing; }
    if (!st.ok()) { std::fprintf(stderr, "bakecompare: TerrainDef.csv: %s\n", st.str().c_str()); return kParse; }
    shaders::ShaderSection exe;
    st = exe.open(gamefs::fromUtf8(exePath));
    if (!st.ok()) { std::fprintf(stderr, "bakecompare: EXE: %s\n", st.str().c_str()); return kSetup; }
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx))) {
        std::fprintf(stderr, "bakecompare: no D3D11 hardware device\n");
        return kSetup;
    }
    bake::PipelineConfig cfg;
    cfg.fs = &fs;
    cfg.exe = &exe;
    cfg.decals = decalSet != nullptr;   // without --decals the bake is stage 1's, as before
    std::unique_ptr<bake::GridNormalSource> siN;
    std::unique_ptr<bake::SlopeClassSource> siS;
    std::unique_ptr<bake::HeightPageSource> siP;
    std::unique_ptr<bake::BakeConstantsSource> siC;
    if (standIn) {  // the wave-1 stand-ins (terrain-bake-comparison.md 5, run 1)
        siN = bake::makeApproxGridNormals();
        siS = bake::makeFlatSlopeClasses();
        siP = bake::makeFittedHeightPages();
        siC = bake::makeCaptureConstants();
        cfg.normals = siN.get();
        cfg.slopes = siS.get();
        cfg.heightPages = siP.get();
        cfg.constants = siC.get();
    }
    bake::PipelineBaker baker(cfg);
    st = baker.init(dev, ctx);
    bake::MapInputs mi;
    mi.grid = &grid;
    mi.tables = &tables;
    if (!plotArt.empty()) {
        mi.plotArtTypes = plotArt.data();
        mi.plotsX = plotArtW;
        mi.plotsY = plotArtH;
        mi.wrapX = grid.wrapX;
    }
    if (st.ok()) st = baker.setMap(mi);
    if (!st.ok()) {
        std::fprintf(stderr, "bakecompare: pipeline: %s\n", st.str().c_str());
        ctx->Release();
        dev->Release();
        return kSetup;
    }
    // 4111 (diagnostic): legacy A8R8G8B8 decal textures as B8G8R8A8 (the channels as the file means them); "all": every
    // name of the set that is an A8R8G8B8 file
    if (bgraNames.size() == 1 && bgraNames[0] == "all") {
        bgraNames.clear();
        for (const std::string& nm : decalSet->textures) {
            std::vector<uint8_t> raw;
            gamefs::DdsImage img;
            if (!nm.empty() && nm[0] != '@' && fs.read(nm, raw).ok() && gamefs::parseDds(raw.data(), raw.size(), img).ok() &&
                img.sourceFormat == "A8R8G8B8" && std::find(bgraNames.begin(), bgraNames.end(), nm) == bgraNames.end())
                bgraNames.push_back(nm);
        }
        std::string list;
        for (const std::string& nm : bgraNames) list += (list.empty() ? "" : ", ") + nm;
        std::printf("--decal-bgra all: %zu A8R8G8B8 decal textures: %s\n", bgraNames.size(), list.c_str());
    }
    for (const std::string& nm : bgraNames) {
        std::vector<uint8_t> raw;
        Status rs = fs.read(nm, raw);
        gamefs::DdsImage img;
        if (rs.ok()) rs = gamefs::parseDds(raw.data(), raw.size(), img);
        if (!rs.ok()) {
            std::fprintf(stderr, "bakecompare: --decal-bgra %s: %s\n", nm.c_str(), rs.str().c_str());
            ctx->Release();
            dev->Release();
            return kMissing;
        }
        if (img.format != DXGI_FORMAT_B8G8R8A8_UNORM || img.sourceFormat != "A8R8G8B8" || img.dim != gamefs::DdsDim::Tex2D ||
            img.arraySize != 1 || !baker.decalPass()) {
            std::fprintf(stderr, "bakecompare: --decal-bgra %s: not a plain 2D A8R8G8B8 file (%s), or no decal pass\n", nm.c_str(),
                         img.sourceFormat.c_str());
            ctx->Release();
            dev->Release();
            return kInconsistent;
        }
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = img.width;
        td.Height = img.height;
        td.MipLevels = img.mipLevels;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_TYPELESS;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        std::vector<D3D11_SUBRESOURCE_DATA> init;
        for (const gamefs::DdsSubresource& sr : img.subresources)
            init.push_back(D3D11_SUBRESOURCE_DATA{img.pixels() + sr.offset, sr.rowPitch, sr.slicePitch});
        ID3D11Texture2D* tex = nullptr;
        HRESULT hr = init.size() == img.mipLevels ? dev->CreateTexture2D(&td, init.data(), &tex) : E_FAIL;
        Status ps = SUCCEEDED(hr) ? baker.decalPass()->provideTexture(nm, tex) : Status::fail(Err::Device, "CreateTexture2D");
        if (tex) tex->Release();
        if (!ps.ok()) {
            std::fprintf(stderr, "bakecompare: --decal-bgra %s: %s\n", nm.c_str(), ps.str().c_str());
            ctx->Release();
            dev->Release();
            return kSetup;
        }
        if (!quiet) std::printf("decal texture %s handed to the pass as B8G8R8A8 (%u x %u, %u mips)\n", nm.c_str(), img.width,
                                img.height, img.mipLevels);
    }

    // ---- per tile
    std::map<uint32_t, std::vector<size_t>> byTile;  // tileIndex -> record indices (t0 / t8)
    std::map<std::string, uint64_t> skipped;
    for (size_t i = 0; i < recs.size(); ++i) {
        const Record& r = recs[i];
        if (!r.ok) { skipped[r.why]++; continue; }
        if (!onlyTiles.empty() && !onlyTiles.count(std::make_pair(r.x, r.y))) continue;
        byTile[r.tileIndex].push_back(i);
    }
    std::vector<Row> rows;
    std::vector<T8Fact> t8Facts;
    size_t tilesDone = 0;
    const clock_t t0Clock = std::clock();
    bake::PipelineTileInfo info;
    for (const auto& kv : byTile) {
        if (limit >= 0 && static_cast<long>(tilesDone) >= limit) break;
        ++tilesDone;
        const Record& first = recs[kv.second.front()];
        const bake::TileId tid{first.x, first.y};
        if (first.tileIndex != first.x * grid.mapH + first.y) {
            skipped["record inconsistent (tileIndex != x * tilesY + y)"] += kv.second.size();
            continue;
        }
        // The EXE's raw files.
        std::vector<std::vector<uint8_t>> raws(kv.second.size());
        std::vector<bool> rawOk(kv.second.size(), false);
        for (size_t k = 0; k < kv.second.size(); ++k) {
            const Record& r = recs[kv.second[k]];
            if (r.fmtCode != 70 && r.fmtCode != 71 && r.fmtCode != 72) { skipped["not BC1 (fmt)"]++; continue; }
            if (!bake::cacheMips(r.w) || r.mips != bake::cacheMips(r.w) || r.bytes != bc1Bytes(r.w, r.mips)) {
                skipped["size, mips and bytes of the record disagree"]++;
                continue;
            }
            if (!readFile(armDir + "/caches/" + r.file, raws[k])) { skipped["raw file missing or unreadable"]++; continue; }
            if (raws[k].size() != r.bytes) { skipped["raw file size differs from the record"]++; continue; }
            rawOk[k] = true;
        }
        if (std::find(rawOk.begin(), rawOk.end(), true) == rawOk.end()) continue;
        // 4106: the tile's decals (origin = 64 x the tile's column and row, height-field samples) and its areaChanged
        decal::TileDecalList tileDecals;
        if (decalSet) {
            tileDecals = decal::decalsForTile(*decalSet, static_cast<int>(first.x) * decal::kTileSamples,
                                              static_cast<int>(first.y) * decal::kTileSamples);
            if (!dropOrders.empty())
                tileDecals.indices.erase(std::remove_if(tileDecals.indices.begin(), tileDecals.indices.end(), [&](uint32_t i) {
                    return dropOrders.count(decalSet->decals[i].listOrder) > 0;
                }), tileDecals.indices.end());
            if (tieReverse) reverseTies(tileDecals);
            else if (tieMode == "small-rev" || tieMode == "large-first") orderTiesBySize(tileDecals, tieMode == "large-first");
            else if (tieMode == "listorder") orderTiesByList(tileDecals);
        }
        const bool srvArea = areaTiles.count(std::make_pair(static_cast<int>(first.x) * decal::kTileSamples,
                                                            static_cast<int>(first.y) * decal::kTileSamples)) > 0;
        // sty -1: the baker's own style rule (plot art types when given, else Generic); 0..4 forced (the probe)
        auto bakeTile = [&](int sty, uint32_t sizeClass, std::vector<uint8_t>& s0, std::vector<uint8_t>& s8, std::string& why) {
            bake::BakeRequest req;
            req.tile = tid;
            req.sizeClass = sizeClass;
            bake::PipelineBaker::Overrides ov;
            ov.style = sty;
            if (t8FromExe) req.areaChanged = first.f2fc != 0;  // tile+0x300 as the EXE's tile+0x2FC says (0x4387D0)
            if (t8FromDecals) req.areaChanged = srvArea;        // the server's own rule from the decals (4105, 0305)
            if (decalSet) req.decals = &tileDecals;
            bake::BakedTile out;
            Status bs = baker.bakeWith(req, ov, out);
            if (!bs.ok()) { why = "bake failed: " + std::string(errName(bs.code)); return false; }
            bool ok = readBc1(dev, ctx, out.t0, s0, why);
            if (ok && out.t8) ok = readBc1(dev, ctx, out.t8, s8, why);
            else s8.clear();
            if (out.t0) out.t0->Release();
            if (out.t8) out.t8->Release();
            return ok;
        };
        std::vector<uint8_t> s0, s8;
        std::string why;
        if (!bakeTile(-1, first.sizeClass, s0, s8, why)) {
            for (size_t k = 0; k < kv.second.size(); ++k) if (rawOk[k]) skipped[why]++;
            continue;
        }
        info = baker.lastTile();
        std::vector<uint32_t> srvList(info.list.ids, info.list.ids + info.list.count);
        {   // the t8 set (P14, 0305): the EXE's t8 (a t8 record in the index) against the server's decision
            T8Fact f;
            f.x = first.x; f.y = first.y; f.f2fc = first.f2fc; f.f300 = first.f300;
            for (size_t k = 0; k < kv.second.size(); ++k) f.exeT8 |= recs[kv.second[k]].cache == 8;
            f.srvT8 = info.needSecond; f.srvByList = info.needSecondByList; f.srvArea = srvArea;
            f.decals = static_cast<uint32_t>(tileDecals.indices.size());
            t8Facts.push_back(f);
        }
        uint32_t tdN = 0, tdP1 = 0, tdTies = 0;
        bool tdRiver = false;
        decalFacts(tileDecals, tdN, tdP1, tdTies, tdRiver);
        size_t tileRowStart = rows.size();
        for (size_t k = 0; k < kv.second.size(); ++k) {
            if (!rawOk[k]) continue;
            const Record& r = recs[kv.second[k]];
            const std::vector<uint8_t>& srv = r.cache == 8 ? s8 : s0;
            if (srv.empty()) { skipped["t8 flag differs (the server made no t8)"]++; continue; }
            if (srv.size() != raws[k].size()) { skipped["server size class differs"]++; continue; }
            Row row;
            row.rec = &r;
            row.listEqual = srvList == r.layers;
            row.decals = tdN; row.pass1 = tdP1; row.tiePairs = tdTies; row.riverDecal = tdRiver;
            row.srvT8 = info.needSecond; row.srvArea = srvArea; row.srvByList = info.needSecondByList;
            row.style = info.style;
            const auto key = std::make_pair(r.tileIndex, r.cache);
            row.fair = haveFair && fair.count(key) > 0;
            // The ceiling of a texture is known when it was in both EXE sessions: identical (fair, 1.0) or differing
            // (decoded_<save>.json per_texture). A texture of one session only has none.
            auto cp = ceilPer.find(key);
            row.ceilKnown = ceilingKnown && (row.fair || cp != ceilPer.end());
            if (cp != ceilPer.end()) { row.ceilEq0 = cp->second.first; row.ceilMax0 = cp->second.second; }
            compareTexture(srv, raws[k], r.w, r.mips, row.res, &row.srvRgb0, &row.exeRgb0);
            rows.push_back(std::move(row));
        }
        // Classes.
        bool t0Equal = false;
        for (size_t i = tileRowStart; i < rows.size(); ++i)
            if (rows[i].rec->cache == 0 && rows[i].res.blocksEqAll == rows[i].res.blocksAll) t0Equal = true;
        const bool standIns = info.normals.compare(0, 8, "stand-in") == 0 || info.slopes.compare(0, 8, "stand-in") == 0 ||
                              info.heightPage.compare(0, 8, "stand-in") == 0 || info.constants.compare(0, 8, "stand-in") == 0;
        const std::string remaining = standIns ? "stand-in inputs (grid normals, height page, slope classes, bake constants)"
                                               : "remaining (none of the classes above)";
        for (size_t i = tileRowStart; i < rows.size(); ++i) {
            Row& row = rows[i];
            const Record& r = *row.rec;
            if (row.res.blocksEqAll == row.res.blocksAll) row.cls = "equal";
            else if (decalSet && r.gen1cc > r.gen1d0)
                row.cls = "EXE cache stale (tile+0x1CC > +0x1D0: content changed after the bake, not re-baked yet)";
            else if (row.ceilKnown && !row.fair) row.cls = "decals (differs between two EXE sessions; bake stage 2, out of scope)";
            else if (!row.listEqual) row.cls = "layer list differs";
            else if ((r.f2fc != 0) != info.needSecond) row.cls = "t8 flag differs";
            else if (r.f2fc && !info.needSecondByList)
                row.cls = "tile+0x300 (area changed since load, 0x4387D0: t8 without a listed spec layer)";
            else if (r.x + 1 == grid.mapW || r.y + 1 == grid.mapH) row.cls = "edge tile (66 x 66 grid in the EXE)";
            else if (r.sizeClass != bake::kCacheSizeDefault) row.cls = "size class not 256";
            else if (r.cache == 8 && t0Equal) row.cls = "t8 only (t0 equal): the specular inputs (HalfVector, spec temp)";
            else if (decalSet && row.tiePairs > 0) row.cls = "remaining, the tile has overlapping decals of equal priority";
            else if (decalSet && row.decals > 0) row.cls = "remaining, decals without overlapping equal priorities";
            else row.cls = remaining;
        }
        if (styleProbe) {
            bool differs = false;
            for (size_t i = tileRowStart; i < rows.size(); ++i) differs |= rows[i].cls == remaining;
            if (differs) {
                for (size_t i = tileRowStart; i < rows.size(); ++i) {
                    rows[i].bestStyle = static_cast<int>(info.style);
                    rows[i].bestStyleBlocks0 = rows[i].res.blocksEq0;
                }
                // The tile's style is one input for t0 and t8: choose it by the t0's mip-0 blocks (t8 when no t0 row).
                std::vector<uint8_t> best0, best8;
                for (int sty = 0; sty < 5; ++sty) {
                    if (sty == static_cast<int>(info.style)) continue;
                    std::vector<uint8_t> p0, p8;
                    std::string w2;
                    if (!bakeTile(sty, first.sizeClass, p0, p8, w2)) continue;
                    uint32_t eq = 0;
                    double base = -1;
                    for (size_t i = tileRowStart; i < rows.size(); ++i) {
                        const Row& row = rows[i];
                        if (base >= 0 && row.rec->cache == 8) continue;
                        const std::vector<uint8_t>& srv = row.rec->cache == 8 ? p8 : p0;
                        std::vector<uint8_t> raw;
                        if (srv.size() != bc1Bytes(row.rec->w, row.rec->mips) ||
                            !readFile(armDir + "/caches/" + row.rec->file, raw) || raw.size() != srv.size()) continue;
                        eq = 0;
                        for (uint32_t b = 0; b < row.res.blocks0; ++b) eq += std::memcmp(&srv[b * 8], &raw[b * 8], 8) == 0;
                        base = row.bestStyleBlocks0;
                        if (row.rec->cache == 0) break;
                    }
                    if (base >= 0 && eq > base) {
                        for (size_t i = tileRowStart; i < rows.size(); ++i) {
                            rows[i].bestStyle = static_cast<int>(sty);
                            rows[i].bestStyleBlocks0 = eq;
                        }
                        best0 = p0;
                        best8 = p8;
                    }
                }
                if (!best0.empty()) {
                    // Figures with the probed style (the style is an input the reference set does not hold).
                    for (size_t i = tileRowStart; i < rows.size(); ++i) {
                        Row& row = rows[i];
                        const std::vector<uint8_t>& srv = row.rec->cache == 8 ? best8 : best0;
                        std::vector<uint8_t> raw;
                        if (srv.size() != bc1Bytes(row.rec->w, row.rec->mips) ||
                            !readFile(armDir + "/caches/" + row.rec->file, raw) || raw.size() != srv.size()) continue;
                        row.res = TexResult();
                        compareTexture(srv, raw, row.rec->w, row.rec->mips, row.res, &row.srvRgb0, &row.exeRgb0);
                        if (row.res.blocksEqAll == row.res.blocksAll) row.cls = "equal";
                        else if (row.cls == remaining) row.cls = "style (another terrain style matches better)";
                    }
                }
            }
        }
        // Keep the decoded pictures only while they may be among the worst.
        for (size_t i = tileRowStart; i < rows.size(); ++i)
            if (diffImages == 0 || rows[i].cls == "equal") { rows[i].srvRgb0.clear(); rows[i].srvRgb0.shrink_to_fit(); rows[i].exeRgb0.clear(); rows[i].exeRgb0.shrink_to_fit(); }
        if (diffImages > 0) {
            // keep at most diffImages * 4 candidate pictures (the worst so far)
            std::vector<size_t> withPics;
            for (size_t i = 0; i < rows.size(); ++i) if (!rows[i].srvRgb0.empty()) withPics.push_back(i);
            const size_t keep = static_cast<size_t>(diffImages) * 4;
            if (withPics.size() > keep) {
                std::sort(withPics.begin(), withPics.end(), [&](size_t a, size_t b) { return rows[a].res.decEq0 < rows[b].res.decEq0; });
                for (size_t j = keep; j < withPics.size(); ++j) {
                    rows[withPics[j]].srvRgb0.clear(); rows[withPics[j]].srvRgb0.shrink_to_fit();
                    rows[withPics[j]].exeRgb0.clear(); rows[withPics[j]].exeRgb0.shrink_to_fit();
                }
            }
        }
        if (!quiet && tilesDone % 200 == 0)
            std::printf("bakecompare: %zu tiles, %zu textures compared, %.1f s\n", tilesDone, rows.size(),
                        static_cast<double>(std::clock() - t0Clock) / CLOCKS_PER_SEC);
    }
    const double seconds = static_cast<double>(std::clock() - t0Clock) / CLOCKS_PER_SEC;

    // ---- aggregates
    std::map<std::string, Agg> agg;   // "all", "t0", "t8", "fair", "fair_t0", "fair_t8", "class:<name>"
    std::map<std::string, uint64_t> classT0, classT8;
    for (const Row& row : rows) {
        Agg a;
        a.textures = 1;
        a.texturesEqual = row.res.blocksEqAll == row.res.blocksAll;
        a.blocks0 = row.res.blocks0; a.blocksEq0 = row.res.blocksEq0;
        a.blocksAll = row.res.blocksAll; a.blocksEqAll = row.res.blocksEqAll;
        a.mip0 = row.res.mip0; a.all = row.res.all;
        if (row.ceilKnown) {
            a.ceilTextures = 1;
            a.ceilTexels0 = row.res.mip0.maxc.n();
            a.ceilEqual0 = static_cast<uint64_t>(std::llround(row.ceilEq0 * static_cast<double>(a.ceilTexels0)));
        }
        const std::string c = row.rec->cache == 8 ? "t8" : "t0";
        agg["all"].add(a);
        agg[c].add(a);
        if (row.fair) { agg["fair"].add(a); agg["fair_" + c].add(a); }
        agg["class:" + row.cls].add(a);
        (row.rec->cache == 8 ? classT8 : classT0)[row.cls]++;
        if (!a.texturesEqual) { agg["differing"].add(a); agg["differing_" + c].add(a); }   // 4106: "the rest"
        if (decalSet) {
            const std::string g = row.decals ? "with_decals" : "without_decals";
            agg[g].add(a);
            agg[g + "_" + c].add(a);
            if (row.riverDecal) { agg["river_decal"].add(a); agg["river_decal_" + c].add(a); }
        }
    }
    for (const char* k : {"all", "t0", "t8", "fair", "fair_t0", "fair_t8"}) agg[k];  // present even when empty

    // ---- worst textures and their pictures
    std::vector<size_t> order(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (rows[a].res.decEq0 != rows[b].res.decEq0) return rows[a].res.decEq0 < rows[b].res.decEq0;
        return a < b;
    });
    std::string images;
    if (diffImages > 0) {
        const std::string dd = outDir + "/diff";
        if (!makeDirs(dd)) { std::fprintf(stderr, "bakecompare: cannot create %s\n", dd.c_str()); return kMissing; }
        long written = 0;
        for (size_t i : order) {
            if (written >= diffImages) break;
            const Row& row = rows[i];
            if (row.srvRgb0.empty() || row.cls == "equal") continue;
            char name[160];
            std::snprintf(name, sizeof name, "%s_%u_%u_t%u.bmp", save.c_str(), row.rec->x, row.rec->y, row.rec->cache);
            if (!writeDiffBmp(dd + "/" + name, row.srvRgb0, row.exeRgb0, row.rec->w)) {
                std::fprintf(stderr, "bakecompare: cannot write %s/%s\n", dd.c_str(), name);
                return kMissing;
            }
            images += std::string(written ? "," : "") + jsonStr(std::string("diff/") + name);
            ++written;
        }
    }

    // ---- report
    const bake::PipelineTileInfo& li = info;
    std::string manifestArm;
    {
        const Json* arms = manifest.get("arms");
        const Json* am = arms ? arms->get(arm.c_str()) : nullptr;
        std::string dll, config;
        if (am) { am->str("dll", dll); am->str("config", config); }
        manifestArm = "{\"dll\":" + jsonStr(dll) + ",\"config\":" + jsonStr(config) + "}";
    }
    std::string rep = "{\"tool\":\"bakecompare\",\"issue\":\"3404\",\"time\":" + jsonStr(nowText()) + ",\"save\":" +
                      jsonStr(save) + ",\"ref\":" + jsonStr(ref) + ",\"arm\":" + jsonStr(arm) + ",\"exe_arm\":" + manifestArm +
                      ",\"terraindef\":" + jsonStr(tdOrigin) + ",\"decoder\":\"reference BC1 palette (as cache_decoded_diff.py)\"" +
                      ",\"inputs\":{\"grid_normals\":" + jsonStr(li.normals) + ",\"slope_classes\":" + jsonStr(li.slopes) +
                      ",\"height_page\":" + jsonStr(li.heightPage) + ",\"bake_constants\":" + jsonStr(li.constants) +
                      ",\"style\":" + jsonStr(std::string(plotArt.empty() ? "Generic (no plot data)" : "plot continent art types (" + plotArtPath + ")") + (styleProbe ? ", probe over all five (figures with the best)" : "")) + ",\"t8_flag\":" + jsonStr(t8FromExe ? "the EXE's tile+0x2FC from the record" : "the list rule") + "}" +
                      ",\"seconds\":" + fmt(seconds, 1) + ",\"counts\":{\"textures_in_index\":" + std::to_string(recs.size()) +
                      ",\"tiles_baked\":" + std::to_string(tilesDone) + ",\"textures_compared\":" + std::to_string(rows.size()) +
                      ",\"skipped\":{";
    bool firstSkip = true;
    for (const auto& s : skipped) { rep += std::string(firstSkip ? "" : ",") + jsonStr(s.first) + ":" + std::to_string(s.second); firstSkip = false; }
    rep += "}},\"ceiling\":";
    if (ceilingKnown)
        rep += "{\"source\":" + jsonStr(decodedPath) + ",\"exe_vs_exe_all_caches_mip0_texel_equal_share\":" + fmt(ceilAll) +
               ",\"identical_caches\":" + std::to_string(fair.size()) + "}";
    else
        rep += "null";
    rep += ",\"totals\":{";
    bool firstAgg = true;
    for (const auto& kv : agg) {
        if (kv.first.compare(0, 6, "class:") == 0) continue;
        rep += std::string(firstAgg ? "" : ",") + jsonStr(kv.first) + ":" + kv.second.json();
        firstAgg = false;
    }
    rep += "},\"classes\":{";
    bool firstCls = true;
    for (const auto& kv : agg) {
        if (kv.first.compare(0, 6, "class:") != 0) continue;
        const std::string name = kv.first.substr(6);
        rep += std::string(firstCls ? "" : ",") + jsonStr(name) + ":{\"t0\":" + std::to_string(classT0[name]) + ",\"t8\":" +
               std::to_string(classT8[name]) + ",\"figures\":" + kv.second.json() + "}";
        firstCls = false;
    }
    rep += "}";
    // 4106: decals, the decal pass's counters, the t8 set (P14 / 0305)
    rep += ",\"decals\":" + decalSummary + ",\"decal_names\":" + namesJson;
    if (decalSet) {
        const decal::DecalCounters& dc = baker.decalCounters();
        rep += ",\"decal_pass\":{\"draws\":" + std::to_string(dc.draws) + ",\"tiles_with_decals\":" +
               std::to_string(dc.tilesWithDecals) + ",\"missing_textures\":" + std::to_string(dc.missingTextures) +
               ",\"bad_decals\":" + std::to_string(dc.badDecals) + ",\"textures_loaded\":" + std::to_string(dc.texturesLoaded) + "}";
    }
    {
        uint64_t both = 0, neither = 0, exeOnly = 0, srvOnly = 0, f2fcVsT8 = 0, f300Known = 0, f300VsArea = 0;
        std::string ex;
        int nex = 0;
        for (const T8Fact& f : t8Facts) {
            if (f.exeT8 && f.srvT8) ++both;
            else if (!f.exeT8 && !f.srvT8) ++neither;
            else {
                (f.exeT8 ? exeOnly : srvOnly)++;
                if (nex++ < 40)
                    ex += std::string(ex.empty() ? "" : ",") + "{\"x\":" + std::to_string(f.x) + ",\"y\":" + std::to_string(f.y) +
                          ",\"exe_t8\":" + (f.exeT8 ? "1" : "0") + ",\"f2fc\":" + std::to_string(f.f2fc) + ",\"f300\":" +
                          std::to_string(f.f300) + ",\"srv_by_list\":" + (f.srvByList ? "1" : "0") + ",\"srv_area\":" +
                          (f.srvArea ? "1" : "0") + ",\"decals\":" + std::to_string(f.decals) + "}";
            }
            f2fcVsT8 += (f.f2fc != 0) != f.exeT8;
            if (f.f300 >= 0) { ++f300Known; f300VsArea += (f.f300 != 0) != f.srvArea; }
        }
        rep += ",\"t8_set\":{\"rule\":" + jsonStr(t8FromTap ? "tap (the .dtap's area block: the EXE's +0x300)" : t8FromDecals ? "decals (tilesForRects of the first set)" : t8FromExe ? "exe (+0x2FC)" : "list") +
               ",\"tiles\":" + std::to_string(t8Facts.size()) + ",\"both\":" + std::to_string(both) + ",\"neither\":" +
               std::to_string(neither) + ",\"exe_only\":" + std::to_string(exeOnly) + ",\"server_only\":" + std::to_string(srvOnly) +
               ",\"f2fc_not_t8_record\":" + std::to_string(f2fcVsT8) + ",\"cached_tiles_with_f300\":" + std::to_string(f300Known) +
               ",\"cached_f300_vs_server_area\":" + std::to_string(f300VsArea) + ",\"examples\":[" + ex + "]}";
    }
    if (!tileFlags.empty() && decalSet) {
        // Every tile of the map: the EXE's +0x300 against (a) the server's rule (tilesForRects of the first set, the
        // query square -2..66 inclusive) and (b) the plain tile square floor(min / 64) .. floor(max / 64).
        std::set<std::pair<int, int>> plain;
        for (const decal::Decal& d : decalSet->decals) {
            if (!(std::isfinite(d.rect[0]) && std::isfinite(d.rect[1]) && std::isfinite(d.rect[2]) && std::isfinite(d.rect[3]))) continue;
            for (int y = static_cast<int>(std::floor(d.rect[1] / 64)); y <= static_cast<int>(std::floor(d.rect[3] / 64)); ++y)
                for (int x = static_cast<int>(std::floor(d.rect[0] / 64)); x <= static_cast<int>(std::floor(d.rect[2] / 64)); ++x)
                    plain.insert(std::make_pair(x * 64, y * 64));
        }
        uint64_t n = 0, f300n = 0, aEq = 0, aExe = 0, aSrv = 0, bEq = 0, bExe = 0, bSrv = 0, f2fcN = 0;
        std::string exA, exB;
        int nA = 0, nB = 0;
        for (const auto& kv : tileFlags) {
            ++n;
            const bool e = kv.second.second != 0;
            f300n += e;
            f2fcN += kv.second.first != 0;
            const auto o = std::make_pair(static_cast<int>(kv.first.first) * 64, static_cast<int>(kv.first.second) * 64);
            const bool a = areaTiles.count(o) > 0, b = plain.count(o) > 0;
            auto note = [&](std::string& s, int& k, bool srv) {
                if (k++ < 30) s += std::string(s.empty() ? "" : ",") + "[" + std::to_string(kv.first.first) + "," +
                                   std::to_string(kv.first.second) + "," + (e ? "1" : "0") + "," + (srv ? "1" : "0") + "]";
            };
            if (a == e) ++aEq; else { (e ? aExe : aSrv)++; note(exA, nA, a); }
            if (b == e) ++bEq; else { (e ? bExe : bSrv)++; note(exB, nB, b); }
        }
        rep += ",\"area_changed_all_tiles\":{\"tiles\":" + std::to_string(n) + ",\"exe_f300\":" + std::to_string(f300n) +
               ",\"exe_f2fc\":" + std::to_string(f2fcN) + ",\"server_rule\":{\"tiles\":" + std::to_string(areaTiles.size()) +
               ",\"equal\":" + std::to_string(aEq) + ",\"exe_only\":" + std::to_string(aExe) + ",\"server_only\":" +
               std::to_string(aSrv) + ",\"examples_x_y_exe_srv\":[" + exA + "]},\"plain_square_rule\":{\"tiles\":" +
               std::to_string(plain.size()) + ",\"equal\":" + std::to_string(bEq) + ",\"exe_only\":" + std::to_string(bExe) +
               ",\"server_only\":" + std::to_string(bSrv) + ",\"examples_x_y_exe_srv\":[" + exB + "]}}";
    }
    rep += ",\"diff_images\":[" + images + "],\"worst\":[";
    std::string csv = "tile,x,y,cache,class,list_equal,fair,blocks_equal_mip0,blocks_mip0,blocks_equal_all,blocks_all,"
                      "decoded_equal_share_mip0,decoded_p95_mip0,decoded_max_mip0,decoded_equal_share_all,ceiling_equal_share_mip0,"
                      "ceiling_max_mip0,best_style,style,decals,pass1,tie_pairs,river_decal,exe_f2fc,exe_f300,srv_t8,srv_area,srv_t8_by_list\n";
    for (size_t j = 0; j < order.size() && j < 20; ++j) {
        const Row& row = rows[order[j]];
        rep += std::string(j ? "," : "") + "{\"x\":" + std::to_string(row.rec->x) + ",\"y\":" + std::to_string(row.rec->y) +
               ",\"cache\":" + std::to_string(row.rec->cache) + ",\"class\":" + jsonStr(row.cls) + ",\"decoded_equal_share_mip0\":" +
               fmt(row.res.decEq0, 4) + ",\"decoded_max_mip0\":" + std::to_string(row.res.decMax0) + "}";
    }
    rep += "]}";
    for (const Row& row : rows) {
        const uint64_t nAll = row.res.all.maxc.n();
        char b[640];
        std::snprintf(b, sizeof b, "%u,%u,%u,%u,\"%s\",%d,%d,%u,%u,%u,%u,%.4f,%u,%u,%.4f,%s,%s,%d,%u,%u,%u,%u,%d,%u,%d,%d,%d,%d\n", row.rec->tileIndex, row.rec->x,
                      row.rec->y, row.rec->cache, row.cls.c_str(), row.listEqual ? 1 : 0, row.fair ? 1 : 0, row.res.blocksEq0,
                      row.res.blocks0, row.res.blocksEqAll, row.res.blocksAll, row.res.decEq0, row.res.decP95_0, row.res.decMax0,
                      nAll ? static_cast<double>(row.res.all.maxc.h[0]) / nAll : 0.0,
                      row.ceilKnown ? fmt(row.ceilEq0, 4).c_str() : "", row.ceilKnown ? std::to_string(row.ceilMax0).c_str() : "",
                      row.bestStyle, row.style, row.decals, row.pass1, row.tiePairs, row.riverDecal ? 1 : 0, row.rec->f2fc,
                      row.rec->f300, row.srvT8 ? 1 : 0, row.srvArea ? 1 : 0, row.srvByList ? 1 : 0);
        csv += b;
    }
    const std::string repPath = outDir + "/bakecompare_" + save + ".json", csvPath = outDir + "/bakecompare_" + save + ".csv";
    if (!writeText(repPath, rep) || !writeText(csvPath, csv)) {
        std::fprintf(stderr, "bakecompare: cannot write %s\n", repPath.c_str());
        return kMissing;
    }
    const Agg& A = agg["all"];
    const Agg& F = agg["fair"];
    std::printf("bakecompare %s: %zu tiles, %zu textures compared, %llu skipped; BC1 blocks equal mip0 %.4f all %.4f; decoded "
                "mip0 texel-equal %.4f (p95 %u, max %u); fair set %llu textures: blocks mip0 %.4f, decoded %.4f; %.1f s\n",
                save.c_str(), tilesDone, rows.size(),
                static_cast<unsigned long long>([&] { uint64_t s = 0; for (const auto& k : skipped) s += k.second; return s; }()),
                A.blocks0 ? static_cast<double>(A.blocksEq0) / A.blocks0 : 0.0,
                A.blocksAll ? static_cast<double>(A.blocksEqAll) / A.blocksAll : 0.0,
                A.mip0.maxc.n() ? static_cast<double>(A.mip0.maxc.h[0]) / A.mip0.maxc.n() : 0.0, A.mip0.maxc.pct(0.95),
                A.mip0.maxc.max(), static_cast<unsigned long long>(F.textures),
                F.blocks0 ? static_cast<double>(F.blocksEq0) / F.blocks0 : 0.0,
                F.mip0.maxc.n() ? static_cast<double>(F.mip0.maxc.h[0]) / F.mip0.maxc.n() : 0.0, seconds);
    std::printf("report %s\n", repPath.c_str());
    ctx->Release();
    dev->Release();
    return kOk;
}
