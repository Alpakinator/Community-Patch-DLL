// render/src/forest/placement.cpp - the tree placement of route A (epic 0431, ticket 4550). See c5r/forest/placement.h;
// the behaviour is described in research/05-graphics-composition/trees.md 2.3 and 10 (own code, D83).
#include "c5r/forest/placement.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

#include "c5r/gamefs/dds.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/heightfield/heightfield.h"

namespace c5r { namespace forest {

namespace {

bool finite4(const float* v, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (!std::isfinite(v[i])) return false;
    return true;
}

// (hx, hy, hz, 1) x M (row vectors, D3DX row-major), x and y only, in float.
void transformXY(const float m[16], float hx, float hy, float hz, float& x, float& y)
{
    x = ((hx * m[0] + hy * m[4]) + hz * m[8]) + m[12];
    y = ((hx * m[1] + hy * m[5]) + hz * m[9]) + m[13];
}

// ---- forests.xml: the one element the EXE reads ----
bool findAttr(const std::string& elem, const char* name, std::string& value)
{
    const std::string key = name;
    size_t at = 0;
    while ((at = elem.find(key, at)) != std::string::npos) {
        const bool startOk = at > 0 && (elem[at - 1] == ' ' || elem[at - 1] == '\t' || elem[at - 1] == '\n' ||
                                        elem[at - 1] == '\r');
        size_t p = at + key.size();
        while (p < elem.size() && (elem[p] == ' ' || elem[p] == '\t')) ++p;
        if (startOk && p < elem.size() && elem[p] == '=') {
            ++p;
            while (p < elem.size() && (elem[p] == ' ' || elem[p] == '\t')) ++p;
            if (p >= elem.size() || (elem[p] != '"' && elem[p] != '\'')) return false;
            const char q = elem[p];
            const size_t end = elem.find(q, p + 1);
            if (end == std::string::npos) return false;
            value = elem.substr(p + 1, end - p - 1);
            return true;
        }
        at += key.size();
    }
    return false;
}

bool parseUint(const std::string& s, uint32_t& out)
{
    if (s.empty() || s.size() > 9) return false;
    uint32_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<uint32_t>(c - '0');
    }
    out = v;
    return true;
}

// A plain decimal number ("2.00", ".60", "-1"); no exponent, no locale.
bool parseFloat(const std::string& s, float& out)
{
    if (s.empty() || s.size() > 32) return false;
    size_t i = 0;
    bool neg = false;
    if (s[i] == '-' || s[i] == '+') neg = s[i++] == '-';
    double v = 0, scale = 1;
    bool digits = false, dot = false;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '.' && !dot) { dot = true; continue; }
        if (c < '0' || c > '9') return false;
        digits = true;
        if (dot) { scale /= 10; v += (c - '0') * scale; } else { v = v * 10 + (c - '0'); }
    }
    if (!digits) return false;
    out = static_cast<float>(neg ? -v : v);
    return true;
}

} // namespace

float waterHeight()
{
    volatile float a = 0.2f, b = 50.583656f;   // MULSS of the two EXE constants, not a compile-time double
    return a * b;
}

uint32_t roundTo90(uint32_t n)
{
    const float f = static_cast<float>(n) * 0.011111111f;   // float(1/90) as 0x9F2380
    const double r = std::floor(static_cast<double>(f + 0.5f));
    return static_cast<uint32_t>(r * 90.0);
}

Status parseForestsXml(const char* text, size_t size, ForestParams& out)
{
    out = ForestParams();
    if (!text) return Status::fail(Err::BadLayout, "forests.xml: no data");
    const std::string s(text, size);
    size_t at = 0;
    for (;;) {
        at = s.find("<Forest", at);
        if (at == std::string::npos) return Status::fail(Err::BadLayout, "forests.xml: no <Forest> element");
        const char after = at + 7 < s.size() ? s[at + 7] : '\0';
        if (after == ' ' || after == '\t' || after == '\r' || after == '\n' || after == '/' || after == '>') break;
        at += 7;
    }
    const size_t end = s.find('>', at);
    if (end == std::string::npos) return Status::fail(Err::BadLayout, "forests.xml: <Forest> element not closed");
    const std::string elem = s.substr(at, end - at);
    ForestParams p;
    std::string v;
    const char* names[5] = {"num_tiles", "random_trees_per_tile", "gridded_trees_per_tile", "overlay_alpha",
                            "space_between"};
    for (const char* n : names)
        if (!findAttr(elem, n, v)) return Status::fail(Err::BadLayout, std::string("forests.xml: attribute ") + n + " missing");
    findAttr(elem, "num_tiles", v);
    if (!parseUint(v, p.numTiles)) return Status::fail(Err::BadLayout, "forests.xml: num_tiles '" + v + "' not a number");
    findAttr(elem, "random_trees_per_tile", v);
    if (!parseUint(v, p.rawRandom))
        return Status::fail(Err::BadLayout, "forests.xml: random_trees_per_tile '" + v + "' not a number");
    findAttr(elem, "gridded_trees_per_tile", v);
    if (!parseUint(v, p.rawGridded))
        return Status::fail(Err::BadLayout, "forests.xml: gridded_trees_per_tile '" + v + "' not a number");
    findAttr(elem, "overlay_alpha", v);
    if (!parseFloat(v, p.overlayAlpha) || !std::isfinite(p.overlayAlpha))
        return Status::fail(Err::BadLayout, "forests.xml: overlay_alpha '" + v + "' not a number");
    findAttr(elem, "space_between", v);
    if (!parseFloat(v, p.spaceBetween) || !std::isfinite(p.spaceBetween) || p.spaceBetween < 0.0f)
        return Status::fail(Err::BadLayout, "forests.xml: space_between '" + v + "' not a number of 0 or more");
    if (p.numTiles == 0 || p.numTiles > kMaxTiles)
        return Status::fail(Err::BadLayout, "forests.xml: num_tiles " + std::to_string(p.numTiles) + " outside 1..64");
    p.randomTrees = roundTo90(p.rawRandom);
    p.griddedTrees = roundTo90(p.rawGridded);
    if (p.randomTrees < kTreesPerBlock || p.randomTrees > kMaxTreesPerPlot)
        return Status::fail(Err::BadLayout, "forests.xml: random_trees_per_tile " + std::to_string(p.rawRandom) +
                                                " rounds to " + std::to_string(p.randomTrees) + ", outside 90..4096");
    if (p.griddedTrees != 0)
        return Status::fail(Err::Unsupported, "forests.xml: gridded_trees_per_tile " + std::to_string(p.rawGridded) +
                                                  " rounds to " + std::to_string(p.griddedTrees) +
                                                  "; gridded placement is not implemented (trees.md 2.3)");
    out = p;
    return Status::Ok();
}

Status parseTileMask(const uint8_t* dds, size_t size, TileMask& out)
{
    out = TileMask();
    gamefs::DdsImage img;
    C5R_TRY(gamefs::parseDds(dds, size, img));
    if (img.sourceFormat != "L8" || img.format != DXGI_FORMAT_R8_UNORM || img.dim != gamefs::DdsDim::Tex2D ||
        img.cube || img.arraySize != 1)
        return Status::fail(Err::Unsupported, "tile mask: Incorrect tile texture format (L8 required), got " +
                                                  img.sourceFormat);
    if (img.width != kMaskEdge || img.height != kMaskEdge)
        return Status::fail(Err::BadLayout, "tile mask: " + std::to_string(img.width) + " x " +
                                                std::to_string(img.height) + ", 128 x 128 required");
    const gamefs::DdsSubresource& top = img.subresources.at(0);
    TileMask m;
    m.texels.resize(kMaskEdge * kMaskEdge);
    for (uint32_t r = 0; r < kMaskEdge; ++r)
        std::memcpy(&m.texels[r * kMaskEdge], img.pixels() + top.offset + static_cast<size_t>(r) * top.rowPitch,
                    kMaskEdge);
    for (uint8_t t : m.texels) m.nonZero += t != 0;
    if (m.nonZero == 0) return Status::fail(Err::BadLayout, "tile mask: no texel set (the placement would never end)");
    out = std::move(m);
    return Status::Ok();
}

bool maskAccepts(const TileMask& m, float x, float y)
{
    if (m.texels.size() != kMaskEdge * kMaskEdge) return false;
    const double c = 0.010416667f;   // float(1/96) as 0xA0A170; the EXE multiplies in x87 extended precision
    const double fu = static_cast<double>(x) * c * 127.0;
    const double fv = (1.0 - c * static_cast<double>(y)) * 127.0;
    if (!(fu > -1.0 && fu < 128.0 && fv > -1.0 && fv < 128.0)) return false;   // also NaN
    const int64_t u = static_cast<int64_t>(fu), v = static_cast<int64_t>(fv);
    if (u < 0 || v < 0 || u >= 128 || v >= 128) return false;
    // The EXE's copy of the mask holds the file's texels one byte later (trees.md 10.3 [L]: 7,200 of 7,200 tapped trees
    // stand on a set texel at file index v x 128 + u - 1, 264 of them not at v x 128 + u). Index -1 (u = v = 0) reads a
    // byte before the texels in the EXE [I]; it is taken as 0 here (the square's corner, outside every mask's hexagon).
    const int64_t i = v * static_cast<int64_t>(kMaskEdge) + u - 1;
    return i >= 0 && m.texels[static_cast<size_t>(i)] != 0;
}

Status loadInstall(const gamefs::GameFiles& fs, ForestParams& params, std::vector<TileMask>& masks,
                   std::string* xmlOrigin)
{
    params = ForestParams();
    masks.clear();
    std::vector<uint8_t> data;
    std::string origin;
    Status st = fs.read("forests.xml", data, &origin);
    if (!st.ok()) return Status::fail(Err::NotFound, "forests.xml: " + st.str());
    ForestParams p;
    C5R_TRY(parseForestsXml(reinterpret_cast<const char*>(data.data()), data.size(), p));
    std::vector<TileMask> ms(p.numTiles);
    for (uint32_t i = 0; i < p.numTiles; ++i) {
        const std::string name = "forest_tile" + std::to_string(i + 1) + ".dds";
        std::vector<uint8_t> b;
        st = fs.read(name, b);
        if (!st.ok()) return Status::fail(Err::NotFound, name + ": " + st.str());
        st = parseTileMask(b.data(), b.size(), ms[i]);
        if (!st.ok()) return Status::fail(st.code, name + ": " + st.msg);
    }
    params = p;
    masks = std::move(ms);
    if (xmlOrigin) *xmlOrigin = origin;
    return Status::Ok();
}

Status squareToOffset(const float m[16], float x, float y, float offset[3])
{
    offset[0] = offset[1] = offset[2] = 0;
    if (!m || !finite4(m, 16)) return Status::fail(Err::BadLayout, "squareToOffset: hexToWorld not finite");
    // The sample-frame point of the tree: M's origin (the hex (0, 0, -0)) plus the square point centred.
    float ox, oy;
    transformXY(m, 0.0f, 0.0f, -0.0f, ox, oy);
    const double px = static_cast<double>((ox + x) - 48.0f) - m[12];
    const double py = static_cast<double>((oy + y) - 48.0f) - m[13];
    // Invert the 2 x 2 part with hz = -(hx + hy): (hx, hy) x [[m0 - m8, m1 - m9], [m4 - m8, m5 - m9]].
    const double a = static_cast<double>(m[0]) - m[8], b = static_cast<double>(m[1]) - m[9];
    const double c = static_cast<double>(m[4]) - m[8], d = static_cast<double>(m[5]) - m[9];
    const double det = a * d - b * c;
    if (!(std::fabs(det) > 1e-9)) return Status::fail(Err::BadLayout, "squareToOffset: hexToWorld not invertible");
    const double hx = (px * d - py * c) / det, hy = (-px * b + py * a) / det;
    offset[0] = static_cast<float>(hx);
    offset[1] = static_cast<float>(hy);
    offset[2] = static_cast<float>(-(hx + hy));
    return Status::Ok();
}

void sortLayout(Layout& layout)
{
    std::stable_sort(layout.begin(), layout.end(), [](const Tree& a, const Tree& b) { return a.square[1] > b.square[1]; });
}

Status buildLayout(const ForestParams& params, const TileMask& mask, const float hexToWorld[16], CrtRand& rng,
                   Layout& out)
{
    out.clear();
    const uint32_t n = params.treesPerPlot();
    if (n == 0 || n > kMaxTreesPerPlot) return Status::fail(Err::BadLayout, "buildLayout: trees per plot 0 or above 4096");
    if (mask.texels.size() != kMaskEdge * kMaskEdge || mask.nonZero == 0)
        return Status::fail(Err::BadLayout, "buildLayout: mask not 128 x 128 or empty");
    float probe[3];
    C5R_TRY(squareToOffset(hexToWorld, 48.0f, 48.0f, probe));
    const float inv = 3.051851e-5f;   // 0x9E1D2C, float(1/32767)
    const float space = params.spaceBetween;
    Layout lay;
    lay.reserve(n);
    uint64_t draws = 0;
    const uint64_t maxDraws = 100000ull * n;
    for (uint32_t i = 0; i < n;) {
        if (++draws > maxDraws)
            return Status::fail(Err::BadLayout, "buildLayout: mask accepts too little, layout not filled after " +
                                                    std::to_string(maxDraws) + " draws");
        float size = (static_cast<float>(rng.next()) * inv + 1.0f) * 0.5f;
        float x = 0, y = 0;
        uint32_t tries = 0;
        for (;;) {
            if (tries >= kTriesPerTree) break;   // the 20th failed try's point is kept
            x = static_cast<float>(rng.next()) * inv;
            y = static_cast<float>(rng.next()) * inv * kSquare;
            x = x * kSquare;
            bool ok = true;
            for (const Tree& t : lay) {
                const float dx = -t.square[0] + x, dy = -t.square[1] + y;
                if (std::sqrt(dx * dx + dy * dy) < (t.size + size) * space) {
                    ok = false;
                    break;
                }
            }
            ++tries;
            if (ok) break;
        }
        if (i > kZeroSizeAfter && tries >= kTriesPerTree) size = 0.0f;
        if (!maskAccepts(mask, x, y)) continue;   // the draw does not count
        if (i > kZeroSizeAfter) size = 0.0f;      // [C 0x5A3DA7]: every tree past 350 is size 0
        Tree t;
        C5R_TRY(squareToOffset(hexToWorld, x, y, t.offset));
        t.one = 1.0f;
        t.square[0] = x;
        t.square[1] = y;
        t.size = size;
        lay.push_back(t);
        ++i;
    }
    sortLayout(lay);
    out = std::move(lay);
    return Status::Ok();
}

Status buildLayouts(const ForestParams& params, const std::vector<TileMask>& masks, const float hexToWorld[16],
                    CrtRand& rng, std::vector<Layout>& out)
{
    out.clear();
    if (params.numTiles == 0 || masks.size() != params.numTiles)
        return Status::fail(Err::BadLayout, "buildLayouts: " + std::to_string(masks.size()) + " masks for num_tiles " +
                                                std::to_string(params.numTiles));
    std::vector<Layout> all(kLayoutCount);
    for (uint32_t k = 0; k < kLayoutCount; ++k) {
        Status st = buildLayout(params, masks[k % params.numTiles], hexToWorld, rng, all[k]);
        if (!st.ok()) return Status::fail(st.code, "layout " + std::to_string(k) + ": " + st.msg);
    }
    out = std::move(all);
    return Status::Ok();
}

Status checkLayouts(const std::vector<Layout>& layouts, uint32_t treesPerPlot)
{
    if (layouts.empty() || layouts.size() > kMaxTiles)
        return Status::fail(Err::BadLayout, "layouts: " + std::to_string(layouts.size()) + ", 1..64 required");
    if (treesPerPlot == 0 || treesPerPlot % kTreesPerBlock != 0 || treesPerPlot > kMaxTreesPerPlot)
        return Status::fail(Err::BadLayout, "layouts: trees per plot " + std::to_string(treesPerPlot) +
                                                " not a multiple of 90 in 90..4096");
    for (size_t k = 0; k < layouts.size(); ++k) {
        if (layouts[k].size() != treesPerPlot)
            return Status::fail(Err::BadLayout, "layout " + std::to_string(k) + ": " + std::to_string(layouts[k].size()) +
                                                    " trees, " + std::to_string(treesPerPlot) + " required");
        for (size_t i = 0; i < layouts[k].size(); ++i) {
            const Tree& t = layouts[k][i];
            const float v[7] = {t.offset[0], t.offset[1], t.offset[2], t.one, t.square[0], t.square[1], t.size};
            if (!finite4(v, 7) || !(t.size == 0.0f || (t.size >= 0.5f && t.size <= 1.0f)))
                return Status::fail(Err::BadLayout, "layout " + std::to_string(k) + " tree " + std::to_string(i) +
                                                        ": a value not finite or a size outside 0 / 0.5..1");
        }
    }
    return Status::Ok();
}

Kind kindOf(bool forest, bool jungle, bool hills)
{
    if (forest == jungle) return Kind::None;   // neither, or both (refused by Placement)
    if (forest) return hills ? Kind::ForestHills : Kind::Forest;
    return hills ? Kind::JungleHills : Kind::Jungle;
}

Status makeRecord(int32_t plotX, int32_t plotY, Kind kind, uint32_t layout, PlotRecord& out)
{
    out = PlotRecord();
    if (kind == Kind::None) return Status::fail(Err::BadLayout, "record: kind None");
    PlotRecord r;
    r.plotX = plotX;
    r.plotY = plotY;
    r.cube[0] = plotX - (plotY >> 1);
    r.cube[1] = plotY;
    r.cube[2] = -(r.cube[0] + r.cube[1]);
    r.kind = kind;
    r.layout = layout;
    out = r;
    return Status::Ok();
}

Status recordFromCube(const int32_t cube[3], uint32_t kind, uint32_t layout, PlotRecord& out)
{
    out = PlotRecord();
    if (kind < 1 || kind > 4) return Status::fail(Err::BadLayout, "record: kind " + std::to_string(kind) + " outside 1..4");
    if (static_cast<int64_t>(cube[0]) + cube[1] + cube[2] != 0)
        return Status::fail(Err::BadLayout, "record: cube coordinates do not sum to 0");
    PlotRecord r;
    std::memcpy(r.cube, cube, sizeof r.cube);
    r.plotY = cube[1];
    r.plotX = cube[0] + (cube[1] >> 1);
    r.kind = static_cast<Kind>(kind);
    r.layout = layout;
    out = r;
    return Status::Ok();
}

float slopeNormalZ(const heightfield::HeightGrid& g, int32_t c, int32_t r)
{
    if (g.samplesX == 0 || g.samplesY == 0 || g.h.size() != static_cast<size_t>(g.samplesX) * g.samplesY) return 1.0f;
    const int32_t W = static_cast<int32_t>(g.samplesX), H = static_cast<int32_t>(g.samplesY);
    auto wrapCol = [&](int32_t x) { return g.wrapX ? ((x % W) + W) % W : x; };
    auto rawAt = [&](int32_t x, int32_t y, bool& in) -> uint16_t {
        x = wrapCol(x);
        in = x >= 0 && y >= 0 && x < W && y < H;
        return in ? g.h[static_cast<size_t>(y) * W + x] : 0;
    };
    bool in = false;
    const uint16_t h0 = rawAt(c, r, in);
    const float k255 = 0.003921569f;   // 0x9F9E34
    float hv[5][5];                    // [x offset][y offset], heights / 255; outside the grid: the centre [I]
    uint16_t raw[5][5];
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 5; ++j) {
            bool ok = false;
            const uint16_t v = rawAt(c + i - 2, r + j - 2, ok);
            raw[i][j] = ok ? v : h0;
            hv[i][j] = static_cast<float>(raw[i][j]) * k255;
        }
    const float h0f = static_cast<float>(h0) * k255;
    const float refX = static_cast<float>((static_cast<int32_t>(raw[3][2]) - static_cast<int32_t>(raw[1][2])) >> 1) *
                       k255 + h0f;
    const float refY = static_cast<float>((static_cast<int32_t>(raw[2][3]) - static_cast<int32_t>(raw[2][1])) >> 1) *
                       k255 + h0f;
    static const float smooth[5] = {0.0228f, 0.2293f, 0.4948f, 0.2293f, 0.0228f};   // 0xA03A18, 0x9DA18C, 0x9D9998
    static const float deriv[5] = {0.0702f, 0.3526f, 0.0f, -0.3526f, -0.0702f};    // 0x9F5C44, 0x9FD118, -, ...
    const float norm = static_cast<float>(1.0 / std::sqrt(0.94247779607693793));   // 1/sqrt([0x9F7E20]) (cancels)
    auto weight = [&](float ref, float h) {
        float f = (ref - h) * 0.0078125f;
        f = f * f * -3.3333333f;
        return (f * f * f * 0.16666667f + f * f * 0.5f + f + 1.0f) * norm;
    };
    float numX = 0, denX = 0, numY = 0, denY = 0;
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 5; ++j) {
            const float h = hv[i][j];
            const float wx = weight(refX, h) * smooth[j] * deriv[i];
            const float wy = weight(refY, h) * deriv[j] * smooth[i];
            numX += wx * h;
            denX += std::fabs(wx);
            numY += wy * h;
            denY += std::fabs(wy);
        }
    if (!(denX > 0.0f) || !(denY > 0.0f)) return 1.0f;
    const float gx = -((numX / denX) * g.sz), gy = -((numY / denY) * g.sz);
    const float nx = gy * 0.0f - g.sy * gx, ny = gx * 0.0f - gy * g.sx, nz = g.sy * g.sx;
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    return len > 0.0f ? nz / len : 1.0f;
}

Status buildPlot(const heightfield::HeightGrid& g, const ForestParams& params, const Layout& layout,
                 const PlotRecord& rec, const CutSource& cut, const BuildOptions& opt, PlotBlocks& out)
{
    out = PlotBlocks();
    if (g.samplesX == 0 || g.samplesY == 0 || g.h.size() != static_cast<size_t>(g.samplesX) * g.samplesY)
        return Status::fail(Err::BadLayout, "buildPlot: height field without samples");
    if (!finite4(g.hexToWorld, 16) || !std::isfinite(g.sx) || !std::isfinite(g.sy) || !std::isfinite(g.ox) ||
        !std::isfinite(g.oy) || !(g.sx > 0) || !(g.sy > 0))
        return Status::fail(Err::BadLayout, "buildPlot: hexToWorld, sx, sy, ox or oy not finite (or scale <= 0)");
    const uint32_t n = params.treesPerPlot();
    if (n == 0 || n % kTreesPerBlock != 0 || n > kMaxTreesPerPlot)
        return Status::fail(Err::BadLayout, "buildPlot: trees per plot " + std::to_string(n) + " not 90..4096 by 90");
    if (layout.size() < n)
        return Status::fail(Err::BadLayout, "buildPlot: layout of " + std::to_string(layout.size()) + " trees, " +
                                                std::to_string(n) + " needed");
    if (rec.kind == Kind::None) return Status::fail(Err::BadLayout, "buildPlot: record of kind None");
    PlotBlocks b;
    b.blocks = n / kTreesPerBlock;
    b.instances.resize(n);
    b.cutGeneration = cut.generation();
    const bool hills = isHills(rec.kind);
    const float factor = hills ? kHillScale : 1.0f;
    b.shadowHalf = (hills ? kShadowHillFactor : 1.0f) * kShadowHalf;
    const float water = waterHeight();
    const float* m = g.hexToWorld;
    const float cx = static_cast<float>(rec.cube[0]), cy = static_cast<float>(rec.cube[1]);
    const int32_t W = static_cast<int32_t>(g.samplesX);
    bool any = false;
    for (uint32_t i = 0; i < n; ++i) {
        const Tree& t = layout[i];
        const float hx = t.offset[0] + cx, hy = cy + t.offset[1];
        const float hz = -hx - hy;
        float sx, sy;
        transformXY(m, hx, hy, hz, sx, sy);
        // HexToWorld (0x6851C0): world x, y; z from the sample of the world point taken back to the grid.
        const float wx = sx * g.sx - g.ox, wy = g.sy * sy - g.oy;
        const float bx = (g.ox + wx) / g.sx, by = (g.oy + wy) / g.sy;
        const int32_t zc = std::isfinite(bx) && std::fabs(bx) < 1e9f ? static_cast<int32_t>(bx) : -1;
        const int32_t zr = std::isfinite(by) && std::fabs(by) < 1e9f ? static_cast<int32_t>(by) : -1;
        const float z = heightfield::heightAtSample(g, zc, zr);
        Instance& in = b.instances[i];
        in.x = wx;
        in.y = wy;
        in.z = z;
        in.scale = 0.0f;
        if (t.size == 0.0f) { ++b.counts.sizeZero; continue; }
        if ((cut.bitsAt(wx, wy) & kCutTreeBits) != 0) { ++b.counts.cut; continue; }
        if (water > z) { ++b.counts.water; continue; }
        if (opt.slopeRule) {
            const int32_t nc = std::isfinite(sx) && std::fabs(sx) < 1e9f ? static_cast<int32_t>(sx) : 0;
            const int32_t nr = std::isfinite(sy) && std::fabs(sy) < 1e9f ? static_cast<int32_t>(sy) : 0;
            const int32_t ncw = g.wrapX ? ((nc % W) + W) % W : nc;
            if (kSlopeMinNormalZ > slopeNormalZ(g, ncw, nr)) { ++b.counts.slope; continue; }
        }
        in.scale = t.size * factor;
        ++b.counts.drawn;
        DrawnTree d;
        d.index = i;
        d.sampleX = sx;
        d.sampleY = sy;
        d.worldX = wx;
        d.worldY = wy;
        d.z = z;
        d.scale = in.scale;
        b.drawn.push_back(d);
        const float r[4] = {sx - b.shadowHalf, sy - b.shadowHalf, sx + b.shadowHalf, sy + b.shadowHalf};
        if (!any) {
            std::memcpy(b.shadowRect, r, sizeof r);
            any = true;
        } else {
            b.shadowRect[0] = std::min(b.shadowRect[0], r[0]);
            b.shadowRect[1] = std::min(b.shadowRect[1], r[1]);
            b.shadowRect[2] = std::max(b.shadowRect[2], r[2]);
            b.shadowRect[3] = std::max(b.shadowRect[3], r[3]);
        }
    }
    for (const Instance& in : b.instances) {
        const float v[4] = {in.x, in.y, in.z, in.scale};
        if (!finite4(v, 4)) return Status::fail(Err::BadLayout, "buildPlot: a cb7 value not finite");
    }
    out = std::move(b);
    return Status::Ok();
}

// ---- Placement ----

Status Placement::initWith(const ForestParams& params, const std::vector<TileMask>& masks, const float hexToWorld[16],
                           uint32_t seed)
{
    layouts_.clear();
    records_.clear();
    CrtRand rng(seed);
    std::vector<Layout> lays;
    C5R_TRY(buildLayouts(params, masks, hexToWorld, rng, lays));
    params_ = params;
    layouts_ = std::move(lays);
    rng_ = rng;
    return Status::Ok();
}

Status Placement::init(const gamefs::GameFiles& fs, const float hexToWorld[16], uint32_t seed, std::string* xmlOrigin)
{
    layouts_.clear();
    records_.clear();
    ForestParams p;
    std::vector<TileMask> masks;
    C5R_TRY(loadInstall(fs, p, masks, xmlOrigin));
    return initWith(p, masks, hexToWorld, seed);
}

Status Placement::initWithLayouts(const ForestParams& params, std::vector<Layout> layouts, uint32_t layoutSeed)
{
    layouts_.clear();
    records_.clear();
    C5R_TRY(checkLayouts(layouts, params.treesPerPlot()));
    params_ = params;
    layouts_ = std::move(layouts);
    rng_ = CrtRand(layoutSeed);
    return Status::Ok();
}

Status Placement::put(const PlotInput& p, bool haveLayout, uint32_t layout)
{
    if (!ready()) return Status::fail(Err::NotFound, "placement: not initialised");
    if (p.x < 0 || p.y < 0 || p.x > 65535 || p.y > 65535)
        return Status::fail(Err::BadLayout, "placement: plot " + std::to_string(p.x) + "," + std::to_string(p.y) +
                                                " outside 0..65535");
    if (p.forest && p.jungle) return Status::fail(Err::BadLayout, "placement: plot both forest and jungle");
    if (haveLayout && layout >= layouts_.size())
        return Status::fail(Err::BadLayout, "placement: layout " + std::to_string(layout) + " past the count " +
                                                std::to_string(layouts_.size()));
    const Kind k = kindOf(p.forest, p.jungle, p.hills);
    const uint64_t id = key(p.x, p.y);
    if (k == Kind::None) {
        records_.erase(id);
        return Status::Ok();
    }
    auto it = records_.find(id);
    if (it != records_.end() && it->second.rec.kind == k && (!haveLayout || it->second.rec.layout == layout))
        return Status::Ok();
    const uint32_t lay = haveLayout ? layout : static_cast<uint32_t>(rng_.next()) % static_cast<uint32_t>(layouts_.size());
    Entry e;
    C5R_TRY(makeRecord(p.x, p.y, k, lay, e.rec));
    records_[id] = std::move(e);
    return Status::Ok();
}

Status Placement::setPlot(const PlotInput& p) { return put(p, false, 0); }
Status Placement::setPlotWithLayout(const PlotInput& p, uint32_t layout) { return put(p, true, layout); }

void Placement::removePlot(int32_t x, int32_t y) { records_.erase(key(x, y)); }
void Placement::clearPlots() { records_.clear(); }

const PlotRecord* Placement::record(int32_t x, int32_t y) const
{
    auto it = records_.find(key(x, y));
    return it == records_.end() ? nullptr : &it->second.rec;
}

std::vector<PlotRecord> Placement::records() const
{
    std::vector<PlotRecord> v;
    v.reserve(records_.size());
    for (const auto& e : records_) v.push_back(e.second.rec);
    return v;
}

Status Placement::blocks(const heightfield::HeightGrid& g, const CutSource& cut, int32_t x, int32_t y,
                         const PlotBlocks** out, const BuildOptions& opt)
{
    if (out) *out = nullptr;
    if (!ready()) return Status::fail(Err::NotFound, "placement: not initialised");
    auto it = records_.find(key(x, y));
    if (it == records_.end())
        return Status::fail(Err::NotFound, "placement: no record at " + std::to_string(x) + "," + std::to_string(y));
    Entry& e = it->second;
    if (!e.built || e.blocks.cutGeneration != cut.generation()) {
        PlotBlocks b;
        C5R_TRY(buildPlot(g, params_, layouts_[e.rec.layout], e.rec, cut, opt, b));
        e.blocks = std::move(b);
        e.built = true;
    }
    if (out) *out = &e.blocks;
    return Status::Ok();
}

void Placement::invalidateAll()
{
    for (auto& e : records_) {
        e.second.built = false;
        e.second.blocks = PlotBlocks();
    }
}

void Placement::invalidate(int32_t x, int32_t y)
{
    auto it = records_.find(key(x, y));
    if (it == records_.end()) return;
    it->second.built = false;
    it->second.blocks = PlotBlocks();
}

size_t Placement::builtCount() const
{
    size_t n = 0;
    for (const auto& e : records_) n += e.second.built;
    return n;
}

}} // namespace c5r::forest
