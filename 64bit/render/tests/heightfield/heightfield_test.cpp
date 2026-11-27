// render/tests/heightfield/heightfield_test.cpp - task 2904. Synthetic data only (no game file, no EXE memory): every
// check of c5r/heightfield has a positive case and a negative case asserting the exact Err.
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include "c5r/heightfield/heightfield.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::heightfield;

namespace {

// A map of w x h plots with a deterministic pattern; the left half of plot (0,0) is flat so that constant tiles exist.
HeightGrid makeGrid(uint32_t w, uint32_t h, bool mat) {
    HeightGrid g;
    g.mapW = w;
    g.mapH = h;
    g.samplesX = w * 64;
    g.samplesY = h * 64;
    g.sx = 1.0f;
    g.sy = 0.866f;
    g.sz = 0.2f;
    g.ox = static_cast<float>(w * 32) * g.sx;
    g.oy = static_cast<float>(h * 32) * g.sy;
    g.waterZ = 12.5f;
    g.mapSeed = 0x1234567;
    for (int i = 0; i < 16; ++i) g.hexToWorld[i] = static_cast<float>(i);
    g.h.resize(static_cast<size_t>(g.samplesX) * g.samplesY);
    for (uint32_t y = 0; y < g.samplesY; ++y) {
        for (uint32_t x = 0; x < g.samplesX; ++x) {
            uint16_t v = static_cast<uint16_t>((x * 131u + y * 977u + (x * y) % 61u) & 0xFFFF);
            if (x < 32 && y < 64) v = 15423;  // a flat area: 2 x 4 uniform tiles
            g.h[g.index(x, y)] = v;
        }
    }
    if (mat) {
        g.mat.resize(g.h.size());
        for (size_t i = 0; i < g.mat.size(); ++i) g.mat[i] = static_cast<uint8_t>((i / 37) % 5);
    }
    return g;
}

void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) { std::memcpy(&v[at], &x, 4); }
void putf(std::vector<uint8_t>& v, size_t at, float x) { std::memcpy(&v[at], &x, 4); }

Status fromImage(const MemoryImage& m, bool withMat, HeightGrid& out) {
    return gridFromMemory(m.obj.data(), m.obj.size(), m.dir16.data(), m.dir16.size(), m.pool16.data(), m.pool16.size(),
                          withMat ? m.dir8.data() : nullptr, m.dir8.size(), m.pool8.data(), m.pool8.size(), false, out);
}

} // namespace

// ---- parseGridObject ----

C5R_TEST(grid_object_ok) {
    MemoryImage m;
    CHECK_OK(buildMemoryImage(makeGrid(2, 1, false), false, m));
    GridObject o;
    CHECK_OK(parseGridObject(m.obj.data(), m.obj.size(), o));
    CHECK(o.ready);
    CHECK_EQ(o.samplesX, 128u);
    CHECK_EQ(o.samplesY, 64u);
    CHECK_EQ(o.h16.tilesX, 8u);
    CHECK_EQ(o.h16.tilesY, 4u);
    CHECK_EQ(o.b8.tilesX, 8u);
    CHECK_EQ(o.h16.inUse, 32u);
    CHECK(std::fabs(o.sy - 0.866f) < 1e-6f);
}

C5R_TEST(grid_object_truncated) {
    MemoryImage m;
    CHECK_OK(buildMemoryImage(makeGrid(1, 1, false), false, m));
    GridObject o;
    CHECK_ERR(parseGridObject(m.obj.data(), kGridObjectSize - 1, o), Err::Truncated);
    CHECK_ERR(parseGridObject(nullptr, 0, o), Err::Truncated);
}

C5R_TEST(grid_object_not_ready) {
    MemoryImage m;
    CHECK_OK(buildMemoryImage(makeGrid(1, 1, false), false, m));
    m.obj[0x09] = 0;
    GridObject o;
    CHECK_ERR(parseGridObject(m.obj.data(), m.obj.size(), o), Err::BadLayout);
}

C5R_TEST(grid_object_bad_samples) {
    MemoryImage m;
    CHECK_OK(buildMemoryImage(makeGrid(1, 1, false), false, m));
    GridObject o;
    std::vector<uint8_t> b = m.obj;
    put32(b, 0xA0, 100);  // not a multiple of 64
    CHECK_ERR(parseGridObject(b.data(), b.size(), o), Err::BadLayout);
    b = m.obj;
    put32(b, 0xA4, 0);
    CHECK_ERR(parseGridObject(b.data(), b.size(), o), Err::BadLayout);
    b = m.obj;
    put32(b, 0xA0, (kMaxPlots + 1) * 64);
    CHECK_ERR(parseGridObject(b.data(), b.size(), o), Err::BadLayout);
}

C5R_TEST(grid_object_bad_scale) {
    MemoryImage m;
    CHECK_OK(buildMemoryImage(makeGrid(1, 1, false), false, m));
    GridObject o;
    std::vector<uint8_t> b = m.obj;
    putf(b, 0xB0, 0.0f);
    CHECK_ERR(parseGridObject(b.data(), b.size(), o), Err::BadLayout);
    b = m.obj;
    putf(b, 0xA8, NAN);
    CHECK_ERR(parseGridObject(b.data(), b.size(), o), Err::BadLayout);
}

C5R_TEST(grid_object_bad_layer) {
    MemoryImage m;
    CHECK_OK(buildMemoryImage(makeGrid(1, 1, false), false, m));
    GridObject o;
    std::vector<uint8_t> b = m.obj;
    put32(b, 0x0C, 32);  // ushort layer width != samplesX
    CHECK_ERR(parseGridObject(b.data(), b.size(), o), Err::BadLayout);
    b = m.obj;
    put32(b, 0x14, 3);  // tilesX != 64/16
    CHECK_ERR(parseGridObject(b.data(), b.size(), o), Err::BadLayout);
    b = m.obj;
    put32(b, 0x58, 1);  // byte layer tilesY
    CHECK_ERR(parseGridObject(b.data(), b.size(), o), Err::BadLayout);
}

// ---- flattenTiles ----

C5R_TEST(flatten_pooled_and_constant) {
    HeightGrid g = makeGrid(2, 1, true);
    for (int compress = 0; compress < 2; ++compress) {
        MemoryImage m;
        CHECK_OK(buildMemoryImage(g, compress != 0, m));
        if (compress) CHECK(m.pool16.size() < static_cast<size_t>(8 * 4 * 512));  // the flat tiles were compressed
        else CHECK_EQ(m.pool16.size(), static_cast<size_t>(8 * 4 * 512));
        std::vector<uint16_t> h;
        CHECK_OK(flattenTiles(m.dir16.data(), m.dir16.size(), m.pool16.data(), m.pool16.size(), 8, 4, 2, h));
        CHECK(h == g.h);
        std::vector<uint16_t> b;
        CHECK_OK(flattenTiles(m.dir8.data(), m.dir8.size(), m.pool8.data(), m.pool8.size(), 8, 4, 1, b));
        bool same = b.size() == g.mat.size();
        for (size_t i = 0; same && i < b.size(); ++i) same = b[i] == g.mat[i];
        CHECK(same);
    }
}

C5R_TEST(flatten_bad_bytes_per_sample) {
    MemoryImage m;
    CHECK_OK(buildMemoryImage(makeGrid(1, 1, false), false, m));
    std::vector<uint16_t> h;
    CHECK_ERR(flattenTiles(m.dir16.data(), m.dir16.size(), m.pool16.data(), m.pool16.size(), 4, 4, 3, h),
              Err::Unsupported);
}

C5R_TEST(flatten_bad_tiles) {
    MemoryImage m;
    CHECK_OK(buildMemoryImage(makeGrid(1, 1, false), false, m));
    std::vector<uint16_t> h;
    CHECK_ERR(flattenTiles(m.dir16.data(), m.dir16.size(), m.pool16.data(), m.pool16.size(), 0, 4, 2, h),
              Err::BadLayout);
    CHECK_ERR(flattenTiles(m.dir16.data(), m.dir16.size(), m.pool16.data(), m.pool16.size(), 4, kMaxPlots * 4 + 1, 2, h),
              Err::BadLayout);
}

C5R_TEST(flatten_short_directory) {
    MemoryImage m;
    CHECK_OK(buildMemoryImage(makeGrid(1, 1, false), false, m));
    std::vector<uint16_t> h;
    CHECK_ERR(flattenTiles(m.dir16.data(), m.dir16.size() - 1, m.pool16.data(), m.pool16.size(), 4, 4, 2, h),
              Err::Truncated);
    CHECK_ERR(flattenTiles(nullptr, 0, m.pool16.data(), m.pool16.size(), 4, 4, 2, h), Err::Truncated);
}

C5R_TEST(flatten_bad_state) {
    MemoryImage m;
    CHECK_OK(buildMemoryImage(makeGrid(1, 1, false), false, m));
    m.dir16[5 * 8] = 2;  // tile 5: state 2 does not exist
    std::vector<uint16_t> h;
    CHECK_ERR(flattenTiles(m.dir16.data(), m.dir16.size(), m.pool16.data(), m.pool16.size(), 4, 4, 2, h),
              Err::BadLayout);
}

C5R_TEST(flatten_index_past_pool) {
    MemoryImage m;
    CHECK_OK(buildMemoryImage(makeGrid(1, 1, false), false, m));
    put32(m.dir16, 7 * 8 + 4, 16);  // 16 tiles in the pool: index 16 is one past the end
    std::vector<uint16_t> h;
    CHECK_ERR(flattenTiles(m.dir16.data(), m.dir16.size(), m.pool16.data(), m.pool16.size(), 4, 4, 2, h),
              Err::BadLayout);
    CHECK_OK(buildMemoryImage(makeGrid(1, 1, false), false, m));
    CHECK_ERR(flattenTiles(m.dir16.data(), m.dir16.size(), m.pool16.data(), m.pool16.size() - 2, 4, 4, 2, h),
              Err::BadLayout);
}

// ---- gridFromMemory ----

C5R_TEST(grid_from_memory_roundtrip) {
    HeightGrid g = makeGrid(3, 2, true);
    MemoryImage m;
    CHECK_OK(buildMemoryImage(g, true, m));
    HeightGrid r;
    CHECK_OK(fromImage(m, true, r));
    CHECK_EQ(r.mapW, 3u);
    CHECK_EQ(r.mapH, 2u);
    CHECK(r.h == g.h);
    CHECK(r.mat == g.mat);
    CHECK(std::fabs(r.oy - g.oy) < 1e-4f);
    HeightGrid r2;
    CHECK_OK(fromImage(m, false, r2));
    CHECK(r2.mat.empty());
}

C5R_TEST(grid_from_memory_errors_propagate) {
    MemoryImage m;
    CHECK_OK(buildMemoryImage(makeGrid(1, 1, true), false, m));
    HeightGrid r;
    MemoryImage bad = m;
    bad.obj[0x09] = 0;
    CHECK_ERR(fromImage(bad, true, r), Err::BadLayout);
    bad = m;
    bad.dir16.resize(8);
    CHECK_ERR(fromImage(bad, true, r), Err::Truncated);
    bad = m;
    bad.dir8[0] = 9;
    CHECK_ERR(fromImage(bad, true, r), Err::BadLayout);
}

// ---- buildMemoryImage ----

C5R_TEST(memory_image_bad_sizes) {
    HeightGrid g = makeGrid(1, 1, false);
    g.h.pop_back();
    MemoryImage m;
    CHECK_ERR(buildMemoryImage(g, false, m), Err::BadLayout);
    g = makeGrid(1, 1, true);
    g.mat.resize(3);
    CHECK_ERR(buildMemoryImage(g, false, m), Err::BadLayout);
}

// ---- tap format ----

C5R_TEST(tap_roundtrip) {
    for (int withMat = 0; withMat < 2; ++withMat) {
        HeightGrid g = makeGrid(2, 2, withMat != 0);
        g.wrapX = withMat != 0;
        std::vector<uint8_t> f;
        CHECK_OK(writeTap(g, f));
        CHECK_EQ(f.size(), tapSize(2, 2, withMat != 0));
        f.resize(f.size() + 100, 0xEE);  // a page-rounded section: trailing bytes are allowed
        HeightGrid r;
        CHECK_OK(readTap(f.data(), f.size(), r));
        CHECK(r.h == g.h);
        CHECK(r.mat == g.mat);
        CHECK_EQ(r.wrapX, g.wrapX);
        CHECK_EQ(r.mapSeed, g.mapSeed);
        CHECK(r.waterZ == g.waterZ);
        CHECK(std::memcmp(r.hexToWorld, g.hexToWorld, 64) == 0);
    }
}

C5R_TEST(tap_truncated_header) {
    std::vector<uint8_t> f;
    CHECK_OK(writeTap(makeGrid(1, 1, false), f));
    HeightGrid r;
    CHECK_ERR(readTap(f.data(), kTapHeaderSize - 1, r), Err::Truncated);
}

C5R_TEST(tap_bad_magic) {
    std::vector<uint8_t> f;
    CHECK_OK(writeTap(makeGrid(1, 1, false), f));
    f[0] = 'X';
    HeightGrid r;
    CHECK_ERR(readTap(f.data(), f.size(), r), Err::BadMagic);
}

C5R_TEST(tap_bad_version) {
    std::vector<uint8_t> f;
    CHECK_OK(writeTap(makeGrid(1, 1, false), f));
    put32(f, 4, 2);
    HeightGrid r;
    CHECK_ERR(readTap(f.data(), f.size(), r), Err::BadVersion);
}

C5R_TEST(tap_bad_layout) {
    std::vector<uint8_t> f0;
    CHECK_OK(writeTap(makeGrid(1, 1, false), f0));
    HeightGrid r;
    std::vector<uint8_t> f = f0;
    put32(f, 8, 0);  // mapW 0
    CHECK_ERR(readTap(f.data(), f.size(), r), Err::BadLayout);
    f = f0;
    put32(f, 16, 65);  // samplesX != 64 * mapW
    CHECK_ERR(readTap(f.data(), f.size(), r), Err::BadLayout);
    f = f0;
    put32(f, 24, 4);  // unknown flag
    CHECK_ERR(readTap(f.data(), f.size(), r), Err::BadLayout);
    f = f0;
    put32(f, 124, 1);  // reserved
    CHECK_ERR(readTap(f.data(), f.size(), r), Err::BadLayout);
}

C5R_TEST(tap_truncated_payload) {
    std::vector<uint8_t> f;
    CHECK_OK(writeTap(makeGrid(1, 1, true), f));
    HeightGrid r;
    CHECK_ERR(readTap(f.data(), f.size() - 1, r), Err::Truncated);  // the byte layer is one short
}

C5R_TEST(tap_write_inconsistent) {
    HeightGrid g = makeGrid(1, 1, false);
    g.samplesX = 65;
    std::vector<uint8_t> f;
    CHECK_ERR(writeTap(g, f), Err::BadLayout);
    g = makeGrid(1, 1, false);
    g.h.resize(10);
    CHECK_ERR(writeTap(g, f), Err::BadLayout);
}

// ---- world relation ----

C5R_TEST(height_and_world) {
    HeightGrid g = makeGrid(2, 1, false);
    g.h[g.index(5, 7)] = 255;
    CHECK(std::fabs(heightAtSample(g, 5, 7) - 0.2f) < 1e-6f);  // 255/255 * sz
    CHECK(heightAtSample(g, -1, 7) == 0.0f);                    // no wrap: outside -> 0, as 0x6D8B60
    CHECK(heightAtSample(g, 5, 64) == 0.0f);
    g.wrapX = true;
    CHECK(heightAtSample(g, 5 - 128, 7) == heightAtSample(g, 5, 7));  // wrap in x only
    CHECK(heightAtSample(g, 5 + 128, 7) == heightAtSample(g, 5, 7));
    CHECK(heightAtSample(g, 5, -1) == 0.0f);
    float w[3];
    sampleToWorld(g, 64, 32, w);  // the map centre: x = 64 - 64, y = 32*0.866 - 32*0.866
    CHECK(std::fabs(w[0]) < 1e-5f);
    CHECK(std::fabs(w[1]) < 1e-4f);
}

// ---- compare ----

C5R_TEST(compare_stats) {
    HeightGrid a = makeGrid(1, 1, true), b = a;
    CompareStats s;
    CHECK_OK(compare(a, b, 0, s));
    CHECK_EQ(s.differing, 0u);
    CHECK_EQ(s.hist[0], 4096u);
    b.h[b.index(3, 4)] = static_cast<uint16_t>(b.h[b.index(3, 4)] + 1);
    b.h[b.index(9, 9)] = static_cast<uint16_t>(b.h[b.index(9, 9)] ^ 0x400);
    b.mat[0] = static_cast<uint8_t>(b.mat[0] + 1);
    CHECK_OK(compare(a, b, 1, s));
    CHECK_EQ(s.differing, 2u);
    CHECK_EQ(s.overTolerance, 1u);
    CHECK_EQ(s.maxAbs, 0x400u);
    CHECK_EQ(s.maxX, 9u);
    CHECK_EQ(s.maxY, 9u);
    CHECK_EQ(s.hist[1], 1u);
    CHECK_EQ(s.hist[5], 1u);
    CHECK_EQ(s.matDiffering, 1u);
}

C5R_TEST(compare_size_mismatch) {
    HeightGrid a = makeGrid(1, 1, false), b = makeGrid(2, 1, false);
    CompareStats s;
    CHECK_ERR(compare(a, b, 0, s), Err::BadLayout);
}

// ---- plot classification ----

C5R_TEST(pattern_cell_flags) {
    PlotInfo p;
    const uint32_t flat[7] = {0x1, 0x2, 0x4, 0x8, 0x10, 0x2000, 0x4000};
    const uint32_t hill[7] = {0x20, 0x40, 0x80, 0x100, 0x200, 0x2000, 0x4000};
    for (int t = 0; t < 7; ++t) {
        p = PlotInfo();
        p.terrain = t;
        CHECK_EQ(patternCellFlags(p), flat[t]);
        p.hills = true;
        CHECK_EQ(patternCellFlags(p), hill[t]);
        p.mountain = true;
        CHECK_EQ(patternCellFlags(p), t >= 5 ? flat[t] : 0x400u);
    }
    p = PlotInfo();
    p.terrain = 9;  // an id the EXE does not name: plains bit
    CHECK_EQ(patternCellFlags(p), 0x2u);
    p.feature = 17;
    p.naturalWonder = true;
    p.featureType = "FEATURE_KILIMANJARO";
    CHECK_EQ(patternCellFlags(p), 0x800u);
    p.featureType = "FEATURE_REEF";
    CHECK_EQ(patternCellFlags(p), 0x100000u);
    p.featureType = "FEATURE_SOLOMONS_MINES";
    CHECK_EQ(patternCellFlags(p), 0x4u);
    p.featureType = "FEATURE_EL_DORADO";  // keeps the terrain value
    CHECK_EQ(patternCellFlags(p), 0x2u);
    p.featureType = "FEATURE_FOUNTAIN_YOUTH";
    CHECK_EQ(patternCellFlags(p), 0x2u);
    p.featureType = "FEATURE_SOME_MOD_WONDER";  // unknown wonder: Fuji's bit
    CHECK_EQ(patternCellFlags(p), 0x40000u);
    p.naturalWonder = false;  // an ordinary feature changes nothing
    CHECK_EQ(patternCellFlags(p), 0x2u);
}

// ---- PNG preview ----

C5R_TEST(png_write_ok) {
    HeightGrid g = makeGrid(1, 1, false);
    const std::string dir = c5r::test::workDir();
    const std::string path = dir + "/hf_test_preview.png";
    std::wstring wpath(path.begin(), path.end());
    CHECK_OK(writePng16(wpath, g.samplesX, g.samplesY, g.h, true));
    FILE* f = std::fopen(path.c_str(), "rb");
    CHECK(f != nullptr);
    if (f) {
        unsigned char sig[8] = {0};
        CHECK_EQ(std::fread(sig, 1, 8, f), static_cast<size_t>(8));
        std::fclose(f);
        CHECK(sig[1] == 'P' && sig[2] == 'N' && sig[3] == 'G');
    }
    std::remove(path.c_str());
}

C5R_TEST(png_bad_layout) {
    std::vector<uint16_t> px(10);
    CHECK_ERR(writePng16(L"unused.png", 4, 4, px, false), Err::BadLayout);
    CHECK_ERR(writePng16(L"unused.png", 0, 4, std::vector<uint16_t>(), false), Err::BadLayout);
}

C5R_TEST(png_bad_path) {
    std::vector<uint16_t> px(16, 7);
    CHECK_ERR(writePng16(L"Z:/no/such/folder/c5r_hf.png", 4, 4, px, false), Err::Io);
}

C5R_TEST_MAIN
