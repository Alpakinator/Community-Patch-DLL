// render/src/heightfield/heightfield.cpp - the tapped height field (task 2904, epic 2900). Own code; the layout facts
// come from the decompile of CivilizationV_DX11.exe and are cited in heightfield.h and asset-pipeline.md section 4.
#include "c5r/heightfield/heightfield.h"
#include <windows.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace c5r { namespace heightfield {

namespace {

const char kMagic[4] = {'C', '5', 'H', 'F'};
const uint32_t kFlagWrap = 1, kFlagMat = 2;

std::string num(uint64_t v) { return std::to_string(v); }

bool posFinite(float v) { return std::isfinite(v) && v > 0.0f; }

uint32_t rd32(const uint8_t* p, size_t at) { uint32_t v; std::memcpy(&v, p + at, 4); return v; }
float rdf(const uint8_t* p, size_t at) { float v; std::memcpy(&v, p + at, 4); return v; }
void wr32(std::vector<uint8_t>& o, size_t at, uint32_t v) { std::memcpy(&o[at], &v, 4); }
void wrf(std::vector<uint8_t>& o, size_t at, float v) { std::memcpy(&o[at], &v, 4); }

void readLayer(const uint8_t* p, size_t at, GridObject::Layer& L) {
    L.width = rd32(p, at + 0x00);
    L.height = rd32(p, at + 0x04);
    L.tilesX = rd32(p, at + 0x08);
    L.tilesY = rd32(p, at + 0x0C);
    L.dirPtr = rd32(p, at + 0x10);
    L.freeHead = rd32(p, at + 0x14);
    L.inUse = rd32(p, at + 0x18);
    L.poolPtr = rd32(p, at + 0x1C);
    L.poolSize = rd32(p, at + 0x20);
    L.poolCapacity = rd32(p, at + 0x24);
}

Status checkLayer(const GridObject::Layer& L, uint32_t sx, uint32_t sy, const char* what) {
    if (L.width != sx || L.height != sy)
        return Status::fail(Err::BadLayout, std::string(what) + ": layer " + num(L.width) + "x" + num(L.height) +
                                                 " != samples " + num(sx) + "x" + num(sy));
    if (L.tilesX != sx / kTile || L.tilesY != sy / kTile)
        return Status::fail(Err::BadLayout, std::string(what) + ": tiles " + num(L.tilesX) + "x" + num(L.tilesY) +
                                                 " do not cover " + num(sx) + "x" + num(sy));
    return Status::Ok();
}

} // namespace

Status parseGridObject(const uint8_t* p, size_t n, GridObject& out) {
    out = GridObject();
    if (p == nullptr || n < kGridObjectSize)
        return Status::fail(Err::Truncated, "grid object: need " + num(kGridObjectSize) + " bytes, have " + num(n));
    out.ready = p[0x09] == 1;
    if (!out.ready) return Status::fail(Err::BadLayout, "grid object: ready byte +0x09 is " + num(p[0x09]));
    out.samplesX = rd32(p, 0xA0);
    out.samplesY = rd32(p, 0xA4);
    out.sx = rdf(p, 0xA8);
    out.sy = rdf(p, 0xAC);
    out.sz = rdf(p, 0xB0);
    out.ox = rdf(p, 0xB4);
    out.oy = rdf(p, 0xB8);
    out.worldW = rdf(p, 0xC0);
    out.worldH = rdf(p, 0xC4);
    readLayer(p, 0x0C, out.h16);
    readLayer(p, 0x4C, out.b8);
    const uint32_t maxS = kMaxPlots * kSamplesPerPlot;
    if (out.samplesX == 0 || out.samplesY == 0 || out.samplesX % kSamplesPerPlot || out.samplesY % kSamplesPerPlot ||
        out.samplesX > maxS || out.samplesY > maxS)
        return Status::fail(Err::BadLayout, "grid object: samples " + num(out.samplesX) + "x" + num(out.samplesY) +
                                                " are not a map of 64-sample plots");
    if (!posFinite(out.sx) || !posFinite(out.sy) || !posFinite(out.sz))
        return Status::fail(Err::BadLayout, "grid object: scale sx/sy/sz not positive");
    C5R_TRY(checkLayer(out.h16, out.samplesX, out.samplesY, "ushort layer +0x0C"));
    C5R_TRY(checkLayer(out.b8, out.samplesX, out.samplesY, "byte layer +0x4C"));
    return Status::Ok();
}

Status flattenTiles(const uint8_t* dir, size_t dirSize, const uint8_t* pool, size_t poolSize, uint32_t tilesX,
                    uint32_t tilesY, uint32_t bytesPerSample, std::vector<uint16_t>& out) {
    out.clear();
    if (bytesPerSample != 1 && bytesPerSample != 2)
        return Status::fail(Err::Unsupported, "flatten: bytes per sample " + num(bytesPerSample));
    const uint32_t maxT = kMaxPlots * (kSamplesPerPlot / kTile);
    if (tilesX == 0 || tilesY == 0 || tilesX > maxT || tilesY > maxT)
        return Status::fail(Err::BadLayout, "flatten: tiles " + num(tilesX) + "x" + num(tilesY));
    const uint64_t nTiles = static_cast<uint64_t>(tilesX) * tilesY;
    if (dir == nullptr || dirSize < nTiles * 8)
        return Status::fail(Err::Truncated, "flatten: directory needs " + num(nTiles * 8) + " bytes, have " +
                                                num(dir ? dirSize : 0));
    const uint64_t tileBytes = static_cast<uint64_t>(kTile) * kTile * bytesPerSample;
    const uint32_t W = tilesX * kTile;
    out.assign(static_cast<size_t>(W) * tilesY * kTile, 0);
    for (uint32_t ty = 0; ty < tilesY; ++ty) {
        for (uint32_t tx = 0; tx < tilesX; ++tx) {
            const uint64_t e = static_cast<uint64_t>(ty) * tilesX + tx;
            uint16_t state;
            std::memcpy(&state, dir + e * 8, 2);
            const uint32_t payload = rd32(dir, static_cast<size_t>(e * 8 + 4));
            uint16_t* row0 = &out[(static_cast<size_t>(ty) * kTile) * W + static_cast<size_t>(tx) * kTile];
            if (state == 1) {
                const uint16_t v = bytesPerSample == 2 ? static_cast<uint16_t>(payload & 0xFFFF)
                                                       : static_cast<uint16_t>(payload & 0xFF);
                for (uint32_t y = 0; y < kTile; ++y) std::fill(row0 + y * W, row0 + y * W + kTile, v);
            } else if (state == 0) {
                const uint64_t at = static_cast<uint64_t>(payload) * tileBytes;
                if (pool == nullptr || at + tileBytes > poolSize)
                    return Status::fail(Err::BadLayout, "flatten: tile " + num(tx) + "," + num(ty) + " pool index " +
                                                            num(payload) + " past pool of " + num(poolSize) + " bytes");
                const uint8_t* t = pool + at;
                for (uint32_t y = 0; y < kTile; ++y) {
                    for (uint32_t x = 0; x < kTile; ++x) {
                        uint16_t v;
                        if (bytesPerSample == 2) std::memcpy(&v, t + (y * kTile + x) * 2, 2);
                        else v = t[y * kTile + x];
                        row0[y * W + x] = v;
                    }
                }
            } else {
                return Status::fail(Err::BadLayout, "flatten: tile " + num(tx) + "," + num(ty) + " state " + num(state));
            }
        }
    }
    return Status::Ok();
}

Status gridFromMemory(const uint8_t* obj, size_t objSize, const uint8_t* dir16, size_t dir16Size,
                      const uint8_t* pool16, size_t pool16Size, const uint8_t* dir8, size_t dir8Size,
                      const uint8_t* pool8, size_t pool8Size, bool wrapX, HeightGrid& out) {
    out = HeightGrid();
    GridObject g;
    C5R_TRY(parseGridObject(obj, objSize, g));
    std::vector<uint16_t> h;
    C5R_TRY(flattenTiles(dir16, dir16Size, pool16, pool16Size, g.h16.tilesX, g.h16.tilesY, 2, h));
    out.mapW = g.samplesX / kSamplesPerPlot;
    out.mapH = g.samplesY / kSamplesPerPlot;
    out.samplesX = g.samplesX;
    out.samplesY = g.samplesY;
    out.wrapX = wrapX;
    out.sx = g.sx;
    out.sy = g.sy;
    out.sz = g.sz;
    out.ox = g.ox;
    out.oy = g.oy;
    // tiles cover exactly samplesX x samplesY (checked in parseGridObject), so no crop is needed
    out.h = std::move(h);
    if (dir8 != nullptr) {
        std::vector<uint16_t> b;
        C5R_TRY(flattenTiles(dir8, dir8Size, pool8, pool8Size, g.b8.tilesX, g.b8.tilesY, 1, b));
        out.mat.resize(b.size());
        for (size_t i = 0; i < b.size(); ++i) out.mat[i] = static_cast<uint8_t>(b[i]);
    }
    return Status::Ok();
}

size_t tapSize(uint32_t mapW, uint32_t mapH, bool withMaterial) {
    const size_t n = static_cast<size_t>(mapW) * kSamplesPerPlot * mapH * kSamplesPerPlot;
    return kTapHeaderSize + n * 2 + (withMaterial ? n : 0);
}

Status buildMemoryImage(const HeightGrid& g, bool compressConstant, MemoryImage& out) {
    out = MemoryImage();
    const size_t cnt = static_cast<size_t>(g.samplesX) * g.samplesY;
    if (g.mapW == 0 || g.mapH == 0 || g.mapW > kMaxPlots || g.mapH > kMaxPlots ||
        g.samplesX != g.mapW * kSamplesPerPlot || g.samplesY != g.mapH * kSamplesPerPlot || g.h.size() != cnt ||
        (!g.mat.empty() && g.mat.size() != cnt))
        return Status::fail(Err::BadLayout, "memory image: inconsistent grid sizes");
    const uint32_t tx = g.samplesX / kTile, ty = g.samplesY / kTile;
    // object
    out.obj.assign(kGridObjectSize, 0);
    wrf(out.obj, 0x00, 1.0f);
    out.obj[0x08] = 1;
    out.obj[0x09] = 1;
    const uint32_t dirPtr16 = 0x10000000, poolPtr16 = 0x20000000, dirPtr8 = 0x30000000, poolPtr8 = 0x40000000;
    for (int layer = 0; layer < 2; ++layer) {
        const size_t at = layer == 0 ? 0x0C : 0x4C;
        wr32(out.obj, at + 0x00, g.samplesX);
        wr32(out.obj, at + 0x04, g.samplesY);
        wr32(out.obj, at + 0x08, tx);
        wr32(out.obj, at + 0x0C, ty);
        wr32(out.obj, at + 0x10, layer == 0 ? dirPtr16 : dirPtr8);
        wr32(out.obj, at + 0x14, 0x0FFFFFFF);
        wr32(out.obj, at + 0x1C, layer == 0 ? poolPtr16 : poolPtr8);
    }
    wr32(out.obj, 0xA0, g.samplesX);
    wr32(out.obj, 0xA4, g.samplesY);
    wrf(out.obj, 0xA8, g.sx);
    wrf(out.obj, 0xAC, g.sy);
    wrf(out.obj, 0xB0, g.sz);
    wrf(out.obj, 0xB4, g.ox);
    wrf(out.obj, 0xB8, g.oy);
    wrf(out.obj, 0xC0, static_cast<float>(g.samplesX) * g.sx);
    wrf(out.obj, 0xC4, static_cast<float>(g.samplesY) * g.sy);
    // layers
    for (int layer = 0; layer < 2; ++layer) {
        const uint32_t bps = layer == 0 ? 2 : 1;
        std::vector<uint8_t>& dir = layer == 0 ? out.dir16 : out.dir8;
        std::vector<uint8_t>& pool = layer == 0 ? out.pool16 : out.pool8;
        dir.assign(static_cast<size_t>(tx) * ty * 8, 0);
        uint32_t used = 0;
        for (uint32_t y = 0; y < ty; ++y) {
            for (uint32_t x = 0; x < tx; ++x) {
                const size_t e = (static_cast<size_t>(y) * tx + x) * 8;
                uint16_t vals[kTile * kTile];
                bool uniform = true;
                for (uint32_t j = 0; j < kTile; ++j) {
                    for (uint32_t i = 0; i < kTile; ++i) {
                        const size_t s = g.index(x * kTile + i, y * kTile + j);
                        vals[j * kTile + i] = layer == 0 ? g.h[s] : (g.mat.empty() ? 0 : g.mat[s]);
                        uniform = uniform && vals[j * kTile + i] == vals[0];
                    }
                }
                if (compressConstant && uniform) {
                    const uint16_t st = 1;
                    std::memcpy(&dir[e], &st, 2);
                    wr32(dir, e + 4, vals[0]);
                    continue;
                }
                wr32(dir, e + 4, used);
                const size_t base = pool.size();
                pool.resize(base + kTile * kTile * bps);
                for (uint32_t k = 0; k < kTile * kTile; ++k) {
                    if (bps == 2) std::memcpy(&pool[base + k * 2], &vals[k], 2);
                    else pool[base + k] = static_cast<uint8_t>(vals[k]);
                }
                ++used;
            }
        }
        const size_t at = layer == 0 ? 0x0C : 0x4C;
        wr32(out.obj, at + 0x18, used);
        wr32(out.obj, at + 0x20, used);
        wr32(out.obj, at + 0x24, used);
    }
    return Status::Ok();
}

Status readTap(const uint8_t* p, size_t n, HeightGrid& out) {
    out = HeightGrid();
    if (p == nullptr || n < kTapHeaderSize)
        return Status::fail(Err::Truncated, "tap: need " + num(kTapHeaderSize) + " header bytes, have " + num(n));
    if (std::memcmp(p, kMagic, 4) != 0) return Status::fail(Err::BadMagic, "tap: magic is not C5HF");
    const uint32_t ver = rd32(p, 4);
    if (ver != kTapVersion) return Status::fail(Err::BadVersion, "tap: version " + num(ver));
    out.mapW = rd32(p, 8);
    out.mapH = rd32(p, 12);
    out.samplesX = rd32(p, 16);
    out.samplesY = rd32(p, 20);
    const uint32_t flags = rd32(p, 24);
    if (out.mapW == 0 || out.mapH == 0 || out.mapW > kMaxPlots || out.mapH > kMaxPlots)
        return Status::fail(Err::BadLayout, "tap: map " + num(out.mapW) + "x" + num(out.mapH));
    if (out.samplesX != out.mapW * kSamplesPerPlot || out.samplesY != out.mapH * kSamplesPerPlot)
        return Status::fail(Err::BadLayout, "tap: samples " + num(out.samplesX) + "x" + num(out.samplesY) +
                                                " != 64 x map " + num(out.mapW) + "x" + num(out.mapH));
    if (flags & ~(kFlagWrap | kFlagMat)) return Status::fail(Err::BadLayout, "tap: unknown flags " + num(flags));
    if (rd32(p, 120) != 0 || rd32(p, 124) != 0) return Status::fail(Err::BadLayout, "tap: reserved words not 0");
    out.wrapX = (flags & kFlagWrap) != 0;
    const bool hasMat = (flags & kFlagMat) != 0;
    out.sx = rdf(p, 28);
    out.sy = rdf(p, 32);
    out.sz = rdf(p, 36);
    out.ox = rdf(p, 40);
    out.oy = rdf(p, 44);
    out.waterZ = rdf(p, 48);
    out.mapSeed = rd32(p, 52);
    std::memcpy(out.hexToWorld, p + 56, 64);
    const size_t need = tapSize(out.mapW, out.mapH, hasMat);
    if (n < need) return Status::fail(Err::Truncated, "tap: need " + num(need) + " bytes, have " + num(n));
    const size_t cnt = static_cast<size_t>(out.samplesX) * out.samplesY;
    out.h.resize(cnt);
    std::memcpy(out.h.data(), p + kTapHeaderSize, cnt * 2);
    if (hasMat) out.mat.assign(p + kTapHeaderSize + cnt * 2, p + kTapHeaderSize + cnt * 3);
    return Status::Ok();
}

Status writeTap(const HeightGrid& g, std::vector<uint8_t>& out) {
    out.clear();
    const size_t cnt = static_cast<size_t>(g.samplesX) * g.samplesY;
    if (g.mapW == 0 || g.mapH == 0 || g.mapW > kMaxPlots || g.mapH > kMaxPlots ||
        g.samplesX != g.mapW * kSamplesPerPlot || g.samplesY != g.mapH * kSamplesPerPlot || g.h.size() != cnt ||
        (!g.mat.empty() && g.mat.size() != cnt))
        return Status::fail(Err::BadLayout, "tap: inconsistent grid sizes");
    const bool hasMat = !g.mat.empty();
    out.assign(tapSize(g.mapW, g.mapH, hasMat), 0);
    std::memcpy(&out[0], kMagic, 4);
    wr32(out, 4, kTapVersion);
    wr32(out, 8, g.mapW);
    wr32(out, 12, g.mapH);
    wr32(out, 16, g.samplesX);
    wr32(out, 20, g.samplesY);
    wr32(out, 24, (g.wrapX ? kFlagWrap : 0) | (hasMat ? kFlagMat : 0));
    wrf(out, 28, g.sx);
    wrf(out, 32, g.sy);
    wrf(out, 36, g.sz);
    wrf(out, 40, g.ox);
    wrf(out, 44, g.oy);
    wrf(out, 48, g.waterZ);
    wr32(out, 52, g.mapSeed);
    std::memcpy(&out[56], g.hexToWorld, 64);
    std::memcpy(&out[kTapHeaderSize], g.h.data(), cnt * 2);
    if (hasMat) std::memcpy(&out[kTapHeaderSize + cnt * 2], g.mat.data(), cnt);
    return Status::Ok();
}

float heightAtSample(const HeightGrid& g, int x, int y) {
    if (g.samplesX == 0 || g.samplesY == 0) return 0.0f;
    if (g.wrapX) {
        const int w = static_cast<int>(g.samplesX);
        x = ((x % w) + w) % w;  // the EXE uses (w*100 + x) % w, the same for x >= -100*w
    }
    if (x < 0 || y < 0 || x >= static_cast<int>(g.samplesX) || y >= static_cast<int>(g.samplesY)) return 0.0f;
    return static_cast<float>(g.h[g.index(static_cast<uint32_t>(x), static_cast<uint32_t>(y))]) * (1.0f / 255.0f) *
           g.sz;
}

void sampleToWorld(const HeightGrid& g, int c, int r, float out[3]) {
    out[0] = static_cast<float>(c) * g.sx - g.ox;
    out[1] = static_cast<float>(r) * g.sy - g.oy;
    out[2] = heightAtSample(g, c, r);
}

Status compare(const HeightGrid& a, const HeightGrid& b, uint32_t tolerance, CompareStats& out) {
    out = CompareStats();
    if (a.samplesX != b.samplesX || a.samplesY != b.samplesY || a.h.size() != b.h.size())
        return Status::fail(Err::BadLayout, "compare: " + num(a.samplesX) + "x" + num(a.samplesY) + " vs " +
                                                num(b.samplesX) + "x" + num(b.samplesY));
    double sum = 0, sum2 = 0;
    for (size_t i = 0; i < a.h.size(); ++i) {
        const int d = static_cast<int>(a.h[i]) - static_cast<int>(b.h[i]);
        const uint32_t ad = static_cast<uint32_t>(d < 0 ? -d : d);
        sum += ad;
        sum2 += static_cast<double>(ad) * ad;
        if (ad) ++out.differing;
        if (ad > tolerance) ++out.overTolerance;
        if (ad > out.maxAbs) {
            out.maxAbs = ad;
            out.maxX = static_cast<uint32_t>(i % a.samplesX);
            out.maxY = static_cast<uint32_t>(i / a.samplesX);
        }
        const int bucket = ad == 0 ? 0 : ad == 1 ? 1 : ad < 4 ? 2 : ad < 16 ? 3 : ad < 256 ? 4 : 5;
        ++out.hist[bucket];
    }
    out.samples = a.h.size();
    if (out.samples) {
        out.meanAbs = sum / static_cast<double>(out.samples);
        out.rms = std::sqrt(sum2 / static_cast<double>(out.samples));
    }
    if (!a.mat.empty() && a.mat.size() == b.mat.size()) {
        for (size_t i = 0; i < a.mat.size(); ++i) out.matDiffering += a.mat[i] != b.mat[i];
    }
    return Status::Ok();
}

uint32_t patternCellFlags(const PlotInfo& p) {
    uint32_t v;
    if (p.terrain == 5) v = 0x2000;
    else if (p.terrain == 6) v = 0x4000;
    else if (p.mountain) v = 0x400;
    else {
        static const uint32_t flat[5] = {0x1, 0x2, 0x4, 0x8, 0x10};
        static const uint32_t hill[5] = {0x20, 0x40, 0x80, 0x100, 0x200};
        // 0x60ABB0: terrain 0, 2, 3, 4 have their own bit; every other id takes the plains bit
        const int t = (p.terrain == 0 || p.terrain == 2 || p.terrain == 3 || p.terrain == 4) ? p.terrain : 1;
        v = p.hills ? hill[t] : flat[t];
    }
    if (p.feature != -1 && p.naturalWonder) {
        struct NW { const char* name; uint32_t bit; };  // the compare chain of 0x60ABB0, in its order [C]
        static const NW table[] = {
            {"FEATURE_GEYSER", 0x8000},       {"FEATURE_CRATER", 0x10000},      {"FEATURE_GIBRALTAR", 0x4000000},
            {"FEATURE_FUJI", 0x40000},        {"FEATURE_MESA", 0x80000},        {"FEATURE_REEF", 0x100000},
            {"FEATURE_VOLCANO", 0x200000},    {"FEATURE_POTOSI", 0x20000},      {"FEATURE_ULURU", 0x400000},
            {"FEATURE_MT_SINAI", 0x800000},   {"FEATURE_MT_KAILASH", 0x1000000}, {"FEATURE_SRI_PADA", 0x2000000},
            {"FEATURE_KILIMANJARO", 0x800},   {"FEATURE_LAKE_VICTORIA", 0x1000}, {"FEATURE_SOLOMONS_MINES", 0x4},
        };
        bool found = false;
        for (const NW& e : table) {
            if (p.featureType == e.name) { v = e.bit; found = true; break; }
        }
        // FOUNTAIN_YOUTH and EL_DORADO keep the terrain value; every other natural wonder takes Fuji's bit
        if (!found && p.featureType != "FEATURE_FOUNTAIN_YOUTH" && p.featureType != "FEATURE_EL_DORADO") v = 0x40000;
    }
    return v;
}

Status writePng16(const std::wstring& path, uint32_t w, uint32_t h, const std::vector<uint16_t>& px, bool stretch) {
    if (w == 0 || h == 0 || px.size() != static_cast<size_t>(w) * h)
        return Status::fail(Err::BadLayout, "png: " + num(w) + "x" + num(h) + " with " + num(px.size()) + " pixels");
    std::vector<uint16_t> img(px);
    if (stretch) {
        const auto mm = std::minmax_element(img.begin(), img.end());
        const uint32_t lo = *mm.first, hi = *mm.second;
        if (hi > lo) {
            for (uint16_t& v : img) v = static_cast<uint16_t>((static_cast<uint64_t>(v - lo) * 65535u) / (hi - lo));
        }
    }
    const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninit = SUCCEEDED(hrInit);
    Status st;
    IWICImagingFactory* fac = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac));
    if (FAILED(hr)) st = Status::fail(Err::Device, "png: WIC factory hr=" + num(static_cast<uint32_t>(hr)));
    if (st.ok()) {
        hr = fac->CreateStream(&stream);
        if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
        if (FAILED(hr)) st = Status::fail(Err::Io, "png: cannot create file hr=" + num(static_cast<uint32_t>(hr)));
    }
    if (st.ok()) {
        hr = fac->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
        if (SUCCEEDED(hr)) hr = enc->Initialize(stream, WICBitmapEncoderNoCache);
        if (SUCCEEDED(hr)) hr = enc->CreateNewFrame(&frame, nullptr);
        if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
        if (SUCCEEDED(hr)) hr = frame->SetSize(w, h);
        WICPixelFormatGUID fmt = GUID_WICPixelFormat16bppGray;
        if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&fmt);
        if (SUCCEEDED(hr) && fmt != GUID_WICPixelFormat16bppGray) hr = E_FAIL;
        if (SUCCEEDED(hr)) {
            // PNG stores 16-bit samples big-endian; WIC's 16bppGray buffer is native (little-endian) words.
            hr = frame->WritePixels(h, w * 2, static_cast<UINT>(img.size() * 2),
                                    reinterpret_cast<BYTE*>(img.data()));
        }
        if (SUCCEEDED(hr)) hr = frame->Commit();
        if (SUCCEEDED(hr)) hr = enc->Commit();
        if (FAILED(hr)) st = Status::fail(Err::Device, "png: WIC encode hr=" + num(static_cast<uint32_t>(hr)));
    }
    if (frame) frame->Release();
    if (enc) enc->Release();
    if (stream) stream->Release();
    if (fac) fac->Release();
    if (uninit) CoUninitialize();
    return st;
}

}} // namespace c5r::heightfield
