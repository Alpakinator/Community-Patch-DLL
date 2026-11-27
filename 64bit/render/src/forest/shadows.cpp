// render/src/forest/shadows.cpp - the Tree_Shadow decals of route A (epic 0431, ticket 4552). See c5r/forest/shadows.h;
// the behaviour is described in research/05-graphics-composition/trees.md 2.5 and 12 and terrain-decals.md 1.2, 14.6
// and 18 (own code, D83).
#include "c5r/forest/shadows.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>

#include "c5r/bake/grid.h"
#include "c5r/heightfield/heightfield.h"

namespace c5r { namespace forest {

const char* const kShadowAlbedo = "Tree_Shadow_2.dds";
const char* const kShadowSpec = "blank_texture_spec_decal.dds";

namespace {

bool finiteN(const float* v, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (!std::isfinite(v[i])) return false;
    return true;
}

bool worldOk(const float w[4]) { return w && finiteN(w, 4) && w[0] < w[2] && w[1] < w[3]; }

struct Fnv {
    uint64_t h = 0xcbf29ce484222325ull;
    void add(const void* p, size_t n)
    {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 0x100000001b3ull;
        }
    }
};

std::string lowerBare(const std::string& s)
{
    const size_t at = s.find_last_of("/\\");
    std::string b = at == std::string::npos ? s : s.substr(at + 1);
    for (char& c : b) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return b;
}

// The name's index in the table (compared without case and path), appended when absent.
uint32_t nameIndex(std::vector<std::string>& names, const char* name, uint32_t& added)
{
    const std::string want = lowerBare(name);
    for (size_t i = 0; i < names.size(); ++i)
        if (lowerBare(names[i]) == want) return static_cast<uint32_t>(i);
    names.push_back(name);
    ++added;
    return static_cast<uint32_t>(names.size() - 1);
}

// cvttss2si: truncation toward zero; NaN or out of range gives INT32_MIN (terrain-decals.md 18.1 / 18.3).
int32_t truncSse(float v)
{
    if (!(v > -2147483648.0f && v < 2147483648.0f)) return (std::numeric_limits<int32_t>::min)();
    return static_cast<int32_t>(v);
}

// 0x53F0C0's cell of a point on one axis: trunc((p - origin) x inverse) in single precision, clamped to 0 .. n - 1.
uint32_t axisCellClamped(float p, float origin, float inverse, uint32_t n)
{
    const float t = (p - origin) * inverse;
    const int32_t c = truncSse(t);
    if (c < 0) return 0;
    if (static_cast<uint32_t>(c) >= n) return n - 1;
    return static_cast<uint32_t>(c);
}

// The plain tile square of a rectangle on one axis (floor(min / 64) .. floor(max / 64)), clipped to 0 .. n - 1; false when
// it lies outside.
bool tileSpan(float lo, float hi, uint32_t n, uint32_t& a, uint32_t& b)
{
    const double t0 = std::floor(static_cast<double>(lo) / decal::kTileSamples);
    const double t1 = std::floor(static_cast<double>(hi) / decal::kTileSamples);
    if (n == 0 || t1 < 0 || t0 > static_cast<double>(n) - 1 || t1 < t0) return false;
    a = static_cast<uint32_t>((std::max)(0.0, t0));
    b = static_cast<uint32_t>((std::min)(static_cast<double>(n) - 1, t1));
    return true;
}

} // namespace

void worldRectOf(const heightfield::HeightGrid& g, float out[4])
{
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = static_cast<float>(g.samplesX) - 1.0f;
    out[3] = static_cast<float>(g.samplesY) - 1.0f;
}

Status shadowDecal(const PlotBlocks& blocks, int32_t plotX, int32_t plotY, const float worldRect[4], ShadowDecal& out)
{
    out = ShadowDecal();
    if (!worldOk(worldRect)) return Status::fail(Err::BadLayout, "shadowDecal: world rectangle not finite or empty");
    const float h = blocks.shadowHalf;
    if (!std::isfinite(h) || !(h > 0.0f))
        return Status::fail(Err::BadLayout, "shadowDecal: shadow half-size not finite or <= 0");
    const size_t n = blocks.drawn.size();
    if (n > kMaxShadowQuads)
        return Status::fail(Err::BadLayout, "shadowDecal: " + std::to_string(n) + " drawn trees, more than AddDecal's " +
                                                std::to_string(kMaxShadowQuads) + " quads");
    ShadowDecal d;
    d.plotX = plotX;
    d.plotY = plotY;
    d.half = h;
    if (n == 0) {
        out = d;
        return Status::Ok();
    }
    // the corners in single precision, and their box (the rectangle AddDecal takes: every vertex the producer passed)
    std::vector<float> c(4 * n);
    float box[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < n; ++i) {
        const DrawnTree& t = blocks.drawn[i];
        const float q[4] = {t.sampleX - h, t.sampleY - h, t.sampleX + h, t.sampleY + h};
        if (!finiteN(q, 4) || !std::isfinite(t.sampleX) || !std::isfinite(t.sampleY))
            return Status::fail(Err::BadLayout, "shadowDecal: tree " + std::to_string(t.index) + " of plot (" +
                                                    std::to_string(plotX) + ", " + std::to_string(plotY) +
                                                    ") has a centre not finite");
        std::memcpy(&c[4 * i], q, sizeof q);
        if (i == 0) {
            std::memcpy(box, q, sizeof q);
        } else {
            box[0] = (std::min)(box[0], q[0]);
            box[1] = (std::min)(box[1], q[1]);
            box[2] = (std::max)(box[2], q[2]);
            box[3] = (std::max)(box[3], q[3]);
        }
    }
    // clamped to the world rectangle (TDS+0x6C, terrain-decals.md 1.2)
    float r[4] = {(std::max)(box[0], worldRect[0]), (std::max)(box[1], worldRect[1]), (std::min)(box[2], worldRect[2]),
                  (std::min)(box[3], worldRect[3])};
    if (!(r[0] < r[2] && r[1] < r[3]))
        return Status::fail(Err::BadLayout, "shadowDecal: plot (" + std::to_string(plotX) + ", " + std::to_string(plotY) +
                                                ") lies outside the world rectangle");
    std::memcpy(d.rect, r, sizeof r);
    // (p - min) x (1 / extent) in single precision, then binary16 round-to-nearest-even: the only arithmetic of the four
    // tried that gives the EXE's bytes for every quad of five stock recordings (trees.md 12.1; a division instead misses
    // 2 of 25,408 quads, double precision 91)
    const float ix = 1.0f / (r[2] - r[0]), iy = 1.0f / (r[3] - r[1]);
    auto px = [&](float x) { return bake::floatToHalf((x - r[0]) * ix); };
    auto py = [&](float y) { return bake::floatToHalf((y - r[1]) * iy); };
    const uint16_t h0 = bake::floatToHalf(0.0f), h1 = bake::floatToHalf(1.0f);
    d.quads = static_cast<uint32_t>(n);
    d.vertices.resize(6 * n);
    for (size_t i = 0; i < n; ++i) {
        const float* q = &c[4 * i];
        const uint16_t x0 = px(q[0]), y0 = py(q[1]), x1 = px(q[2]), y1 = py(q[3]);
        decal::DecalVertex* v = &d.vertices[6 * i];
        // the EXE's quad [L, trees.md 12.1]: (min, min) (max, min) (max, max) | (min, min) (max, max) (min, max)
        v[0] = decal::DecalVertex{x0, y0, h0, h1};
        v[1] = decal::DecalVertex{x1, y0, h1, h1};
        v[2] = decal::DecalVertex{x1, y1, h1, h0};
        v[3] = decal::DecalVertex{x0, y0, h0, h1};
        v[4] = decal::DecalVertex{x1, y1, h1, h0};
        v[5] = decal::DecalVertex{x0, y1, h0, h0};
    }
    Fnv f;
    const uint32_t pass = kShadowPass, tris = 2 * d.quads;
    const float prio = kShadowPriority;
    f.add(&pass, 4);
    f.add(&prio, 4);
    f.add(d.rect, sizeof d.rect);
    f.add(&tris, 4);
    f.add(d.vertices.data(), d.vertices.size() * sizeof(decal::DecalVertex));
    d.hash = f.h;
    out = std::move(d);
    return Status::Ok();
}

size_t treeShadowCount(const decal::DecalSet& set)
{
    const std::string want = lowerBare(kShadowAlbedo);
    std::vector<uint8_t> isShadow(set.textures.size(), 0);
    for (size_t i = 0; i < set.textures.size(); ++i) isShadow[i] = lowerBare(set.textures[i]) == want;
    size_t n = 0;
    for (const decal::Decal& d : set.decals)
        if (d.pass == kShadowPass && d.texture[0] < isShadow.size() && isShadow[d.texture[0]]) ++n;
    return n;
}

Status mergeShadows(const decal::DecalSet& tap, const ShadowDecal* const* shadows, size_t count,
                    const std::vector<decal::TileOrigin>* marks, decal::DecalSet& out, MergeStats* stats)
{
    out = decal::DecalSet();
    if (stats) *stats = MergeStats();
    if (!worldOk(tap.worldRect)) return Status::fail(Err::BadLayout, "mergeShadows: the tap's world rectangle not finite or empty");
    const size_t already = treeShadowCount(tap);
    if (already)
        return Status::fail(Err::Unsupported, "mergeShadows: the tap's set already holds " + std::to_string(already) +
                                                  " Tree_Shadow decals (the EXE's Forest is on): the server's would draw twice");
    if (count && !shadows) return Status::fail(Err::BadLayout, "mergeShadows: no shadow list");
    uint64_t verts = tap.vertices.size();
    for (size_t k = 0; k < count; ++k) {
        const ShadowDecal* s = shadows[k];
        if (!s) return Status::fail(Err::BadLayout, "mergeShadows: shadow " + std::to_string(k) + " is null");
        if (s->vertices.size() != 6ull * s->quads || s->quads > kMaxShadowQuads)
            return Status::fail(Err::BadLayout, "mergeShadows: shadow " + std::to_string(k) + " has " +
                                                    std::to_string(s->vertices.size()) + " vertices for " +
                                                    std::to_string(s->quads) + " quads");
        if (!s->quads) continue;
        const float* r = s->rect;
        const float* w = tap.worldRect;
        if (!finiteN(r, 4) || !(r[0] < r[2] && r[1] < r[3]) || r[0] < w[0] || r[1] < w[1] || r[2] > w[2] || r[3] > w[3])
            return Status::fail(Err::BadLayout, "mergeShadows: shadow " + std::to_string(k) + " of plot (" +
                                                    std::to_string(s->plotX) + ", " + std::to_string(s->plotY) +
                                                    ") has a rectangle not finite or outside the tap's world");
        verts += s->vertices.size();
    }
    if (verts > 0xFFFFFFFFull) return Status::fail(Err::BadLayout, "mergeShadows: more than 2^32 - 1 vertices");

    decal::DecalSet m;
    m.generation = tap.generation;
    m.loadSeq = tap.loadSeq;
    std::memcpy(m.worldRect, tap.worldRect, sizeof m.worldRect);
    m.textures = tap.textures;
    m.decals = tap.decals;
    m.vertices.reserve(static_cast<size_t>(verts));
    m.vertices.assign(tap.vertices.begin(), tap.vertices.end());
    m.areaTilesX = tap.areaTilesX;
    m.areaTilesY = tap.areaTilesY;
    m.areaChanged = tap.areaChanged;
    const bool walk = decal::hasWalk(tap);
    if (walk) {
        m.walk.origin[0] = tap.walk.origin[0];
        m.walk.origin[1] = tap.walk.origin[1];
        m.walk.inverse[0] = tap.walk.inverse[0];
        m.walk.inverse[1] = tap.walk.inverse[1];
        m.walk.cols = tap.walk.cols;
        m.walk.rows = tap.walk.rows;
        m.walk.cell = tap.walk.cell;
        m.walk.rank = tap.walk.rank;
        m.walk.point = tap.walk.point;
    }
    MergeStats st;
    st.tapDecals = static_cast<uint32_t>(tap.decals.size());
    const uint32_t albedo = nameIndex(m.textures, kShadowAlbedo, st.namesAdded);
    const uint32_t spec = nameIndex(m.textures, kShadowSpec, st.namesAdded);
    uint64_t order64 = 0;
    for (const decal::Decal& d : tap.decals) order64 = (std::max)(order64, static_cast<uint64_t>(d.listOrder) + 1);
    if (order64 + count > 0xFFFFFFFFull)
        return Status::fail(Err::BadLayout, "mergeShadows: the tap's list order leaves no room for the forest decals");
    uint32_t order = static_cast<uint32_t>(order64);

    // the walk: per cell, how many forest decals join it (they go before the cell's tap decals, newest first)
    const uint32_t cells = walk ? m.walk.cols * m.walk.rows : 0;
    std::vector<uint32_t> inCell(cells, 0);
    uint32_t tapLarge = 0;
    if (walk)
        for (uint32_t c : tap.walk.cell) tapLarge += c == decal::kWalkLarge;
    std::vector<uint32_t> seq;   // per appended forest decal: its place among the forest decals of its cell (1-based)

    const bool area = tap.areaTilesX && tap.areaTilesY &&
                      tap.areaChanged.size() == static_cast<size_t>(tap.areaTilesX) * tap.areaTilesY;
    auto setBit = [&](uint32_t x, uint32_t y) {
        uint8_t& b = m.areaChanged[static_cast<size_t>(y) * m.areaTilesX + x];
        if (!b) {
            b = 1;
            ++st.areaBitsSet;
        }
    };

    for (size_t k = 0; k < count; ++k) {
        const ShadowDecal& s = *shadows[k];
        if (!s.quads) continue;
        decal::Decal d;
        std::memcpy(d.rect, s.rect, sizeof d.rect);
        d.priority = kShadowPriority;
        d.pass = kShadowPass;
        d.texture[0] = albedo;
        d.texture[1] = albedo;
        d.texture[2] = spec;
        d.firstVertex = static_cast<uint32_t>(m.vertices.size());
        d.vertexCount = static_cast<uint32_t>(s.vertices.size());
        d.listOrder = order++;
        d.hash = s.hash;
        m.vertices.insert(m.vertices.end(), s.vertices.begin(), s.vertices.end());
        m.decals.push_back(d);
        ++st.shadowDecals;
        st.shadowVertices += d.vertexCount;
        if (walk) {
            // AddDecal [C 18.1]: small when neither side exceeds 256; the point is the rectangle's centre
            const float px = (d.rect[0] + d.rect[2]) * 0.5f, py = (d.rect[1] + d.rect[3]) * 0.5f;
            m.walk.point.push_back(px);
            m.walk.point.push_back(py);
            if (d.rect[2] - d.rect[0] > decal::kLargeDecal || d.rect[3] - d.rect[1] > decal::kLargeDecal) {
                m.walk.cell.push_back(decal::kWalkLarge);
                m.walk.rank.push_back(tapLarge + st.walkLarge);
                ++st.walkLarge;
                seq.push_back(0);
            } else {
                const uint32_t cx = axisCellClamped(px, m.walk.origin[0], m.walk.inverse[0], m.walk.cols);
                const uint32_t cy = axisCellClamped(py, m.walk.origin[1], m.walk.inverse[1], m.walk.rows);
                const uint32_t c = cy * m.walk.cols + cx;
                m.walk.cell.push_back(c);
                m.walk.rank.push_back(0);   // set below
                seq.push_back(++inCell[c]);
            }
        }
        if (area) {
            uint32_t x0, x1, y0, y1;
            if (tileSpan(d.rect[0], d.rect[2], m.areaTilesX, x0, x1) && tileSpan(d.rect[1], d.rect[3], m.areaTilesY, y0, y1))
                for (uint32_t y = y0; y <= y1; ++y)
                    for (uint32_t x = x0; x <= x1; ++x) setBit(x, y);
        }
    }
    if (walk) {
        const size_t n0 = tap.decals.size();
        for (size_t k = 0; k < n0; ++k)
            if (m.walk.cell[k] != decal::kWalkLarge) m.walk.rank[k] += inCell[m.walk.cell[k]];
        for (size_t j = 0; j < seq.size(); ++j) {
            const size_t k = n0 + j;
            if (m.walk.cell[k] != decal::kWalkLarge) m.walk.rank[k] = inCell[m.walk.cell[k]] - seq[j];
        }
    }
    if (area && marks)
        for (const decal::TileOrigin& t : *marks)
            if (t.x >= 0 && t.y >= 0 && static_cast<uint32_t>(t.x) < m.areaTilesX && static_cast<uint32_t>(t.y) < m.areaTilesY)
                setBit(static_cast<uint32_t>(t.x), static_cast<uint32_t>(t.y));
    decal::buildIndex(m);
    if (walk && !decal::hasWalk(m))
        return Status::fail(Err::BadLayout, "mergeShadows: the tap's walk did not survive the merge (an inconsistent walk block)");
    st.walk = walk;
    st.area = area;
    out = std::move(m);
    if (stats) *stats = st;
    return Status::Ok();
}

Status shadowChange(const ShadowDecal* const* changed, size_t count, const float worldRect[4], ShadowChange& out)
{
    out = ShadowChange();
    if (!worldOk(worldRect)) return Status::fail(Err::BadLayout, "shadowChange: world rectangle not finite or empty");
    if (count && !changed) return Status::fail(Err::BadLayout, "shadowChange: no decal list");
    ShadowChange ch;
    const double tx0 = std::floor(static_cast<double>(worldRect[0]) / decal::kTileSamples);
    const double ty0 = std::floor(static_cast<double>(worldRect[1]) / decal::kTileSamples);
    const double tx1 = std::floor(static_cast<double>(worldRect[2]) / decal::kTileSamples);
    const double ty1 = std::floor(static_cast<double>(worldRect[3]) / decal::kTileSamples);
    for (size_t k = 0; k < count; ++k) {
        const ShadowDecal* s = changed[k];
        if (!s) return Status::fail(Err::BadLayout, "shadowChange: decal " + std::to_string(k) + " is null");
        if (!s->quads) continue;
        if (!finiteN(s->rect, 4)) return Status::fail(Err::BadLayout, "shadowChange: decal " + std::to_string(k) + " rectangle not finite");
        decal::DecalRect r;
        std::memcpy(r.v, s->rect, sizeof r.v);
        ch.rects.push_back(r);
        const double a0 = (std::max)(tx0, std::floor(static_cast<double>(s->rect[0]) / decal::kTileSamples));
        const double a1 = (std::min)(tx1, std::floor(static_cast<double>(s->rect[2]) / decal::kTileSamples));
        const double b0 = (std::max)(ty0, std::floor(static_cast<double>(s->rect[1]) / decal::kTileSamples));
        const double b1 = (std::min)(ty1, std::floor(static_cast<double>(s->rect[3]) / decal::kTileSamples));
        for (double y = b0; y <= b1; ++y)
            for (double x = a0; x <= a1; ++x) ch.areaTiles.push_back(decal::TileOrigin{static_cast<int>(x), static_cast<int>(y)});
    }
    std::sort(ch.areaTiles.begin(), ch.areaTiles.end(), [](const decal::TileOrigin& a, const decal::TileOrigin& b) {
        return a.y != b.y ? a.y < b.y : a.x < b.x;
    });
    ch.areaTiles.erase(std::unique(ch.areaTiles.begin(), ch.areaTiles.end(),
                                   [](const decal::TileOrigin& a, const decal::TileOrigin& b) { return a.x == b.x && a.y == b.y; }),
                       ch.areaTiles.end());
    ch.bakeTiles = decal::tilesForRects(ch.rects.data(), ch.rects.size(), worldRect);
    out = std::move(ch);
    return Status::Ok();
}

}} // namespace c5r::forest
