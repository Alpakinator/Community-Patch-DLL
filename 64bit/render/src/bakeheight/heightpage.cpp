// render/src/bakeheight/heightpage.cpp - the terrain bake's height pages and bake-time constants (issue 3406).
// Behaviour: research/05-graphics-composition/terrain-bake-passes.md sections 5.0 and 5.3 (D83: from that description).
#include "c5r/bake/heightpage.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include "c5r/heightfield/heightfield.h"

namespace c5r { namespace bake {

namespace {

Status checkGrid(const heightfield::HeightGrid& g) {
    if (g.samplesX == 0 || g.samplesY == 0 || g.mapW == 0 || g.mapH == 0)
        return Status::fail(Err::BadLayout, "height grid is empty");
    if (g.samplesX != g.mapW * heightfield::kSamplesPerPlot || g.samplesY != g.mapH * heightfield::kSamplesPerPlot)
        return Status::fail(Err::BadLayout, "height grid samples are not plots * 64");
    return Status::Ok();
}

float fromBits(uint32_t b) {
    float f;
    std::memcpy(&f, &b, sizeof f);
    return f;
}

} // namespace

Status heightPageCount(const heightfield::HeightGrid& g, uint32_t& pagesX, uint32_t& pagesY) {
    pagesX = pagesY = 0;
    C5R_TRY(checkGrid(g));
    pagesX = (g.samplesX + kHeightPageStep) / kHeightPageStep;
    pagesY = (g.samplesY + kHeightPageStep) / kHeightPageStep;
    return Status::Ok();
}

void heightPageOffset(HeightPageIndex p, int32_t offset[2]) {
    offset[0] = static_cast<int32_t>(kHeightPageMargin) - static_cast<int32_t>(p.px * kHeightPageStep);
    offset[1] = static_cast<int32_t>(kHeightPageMargin) - static_cast<int32_t>(p.py * kHeightPageStep);
}

Status heightPageForTile(const heightfield::HeightGrid& g, TileId t, HeightPageIndex& page, int32_t offset[2]) {
    page = HeightPageIndex();
    offset[0] = offset[1] = 0;
    C5R_TRY(checkGrid(g));
    if (t.x >= g.mapW || t.y >= g.mapH)
        return Status::fail(Err::NotFound, "tile (" + std::to_string(t.x) + ", " + std::to_string(t.y) +
                                               ") outside the " + std::to_string(g.mapW) + " x " +
                                               std::to_string(g.mapH) + " tile grid");
    page.px = t.x * kTileSamples / kHeightPageStep;
    page.py = t.y * kTileSamples / kHeightPageStep;
    heightPageOffset(page, offset);
    return Status::Ok();
}

Status heightPageNextMip(const std::vector<uint8_t>& src, uint32_t n, std::vector<uint8_t>& dst) {
    dst.clear();
    if (n == 0 || (n & 1) != 0 || src.size() != static_cast<size_t>(n) * n)
        return Status::fail(Err::BadLayout, "mip source must be n x n with n even and > 0");
    const uint32_t m = n / 2;
    std::vector<int> box(static_cast<size_t>(m) * m);
    for (uint32_t y = 0; y < m; ++y)
        for (uint32_t x = 0; x < m; ++x) {
            const size_t a = static_cast<size_t>(2 * y) * n + 2 * x;
            box[static_cast<size_t>(y) * m + x] = (src[a] + src[a + 1] + src[a + n] + src[a + n + 1]) >> 2;
        }
    dst.resize(box.size());
    for (uint32_t y = 0; y < m; ++y) {
        const uint32_t ys[3] = {y > 0 ? y - 1 : 0, y, y + 1 < m ? y + 1 : m - 1};
        for (uint32_t x = 0; x < m; ++x) {
            const uint32_t xs[3] = {x > 0 ? x - 1 : 0, x, x + 1 < m ? x + 1 : m - 1};
            int s = 0;
            for (int j = 0; j < 3; ++j)
                for (int i = 0; i < 3; ++i) {
                    const int w = (i == 1 && j == 1) ? 9 : -1;
                    s += w * box[static_cast<size_t>(ys[j]) * m + xs[i]];
                }
            const int c = box[static_cast<size_t>(y) * m + x];
            int v = ((s - c) >> 3) + c;  // arithmetic shift: floor division by 8
            v = std::min(255, std::max(0, v));
            dst[static_cast<size_t>(y) * m + x] = static_cast<uint8_t>(v);
        }
    }
    return Status::Ok();
}

Status buildHeightPage(const heightfield::HeightGrid& g, HeightPageIndex p, uint32_t mips, HeightPage& out) {
    out = HeightPage();
    uint32_t pagesX = 0, pagesY = 0;
    C5R_TRY(heightPageCount(g, pagesX, pagesY));
    if (g.h.size() != static_cast<size_t>(g.samplesX) * g.samplesY)
        return Status::fail(Err::BadLayout, "height grid holds " + std::to_string(g.h.size()) + " samples, not " +
                                                std::to_string(static_cast<size_t>(g.samplesX) * g.samplesY));
    if (p.px >= pagesX || p.py >= pagesY)
        return Status::fail(Err::NotFound, "page (" + std::to_string(p.px) + ", " + std::to_string(p.py) +
                                               ") past the " + std::to_string(pagesX) + " x " +
                                               std::to_string(pagesY) + " pages");
    if (mips != 1 && mips != kHeightPageMips)
        return Status::fail(Err::Unsupported, "height page mip count must be 1 or 11");

    const uint32_t N = kHeightPageSize;
    // Both axes wrap: (start + i) mod samples, with start = page origin - margin (possibly negative).
    const int64_t sx0 = static_cast<int64_t>(p.px) * kHeightPageStep - kHeightPageMargin;
    const int64_t sy0 = static_cast<int64_t>(p.py) * kHeightPageStep - kHeightPageMargin;
    std::vector<uint32_t> col(N);
    for (uint32_t u = 0; u < N; ++u) {
        int64_t x = (sx0 + u) % static_cast<int64_t>(g.samplesX);
        if (x < 0) x += g.samplesX;
        col[u] = static_cast<uint32_t>(x);
    }
    HeightPage page;
    page.index = p;
    page.levels.resize(mips);
    std::vector<uint8_t>& l0 = page.levels[0];
    l0.resize(static_cast<size_t>(N) * N);
    for (uint32_t r = 0; r < N; ++r) {  // r = sample row offset; stored at row N - 1 - r
        int64_t y = (sy0 + r) % static_cast<int64_t>(g.samplesY);
        if (y < 0) y += g.samplesY;
        const uint16_t* row = g.h.data() + static_cast<size_t>(y) * g.samplesX;
        uint8_t* dst = l0.data() + static_cast<size_t>(N - 1 - r) * N;
        for (uint32_t u = 0; u < N; ++u) dst[u] = static_cast<uint8_t>(row[col[u]] >> 8);
    }
    for (uint32_t m = 1; m < mips; ++m) C5R_TRY(heightPageNextMip(page.levels[m - 1], N >> (m - 1), page.levels[m]));
    out = std::move(page);
    return Status::Ok();
}

Status createHeightPageTexture(ID3D11Device* dev, const HeightPage& page, ID3D11Texture2D** tex,
                               ID3D11ShaderResourceView** srv) {
    if (tex) *tex = nullptr;
    if (srv) *srv = nullptr;
    if (!dev || !tex || !srv) return Status::fail(Err::BadLayout, "no device or no output");
    const uint32_t mips = static_cast<uint32_t>(page.levels.size());
    if (mips != 1 && mips != kHeightPageMips) return Status::fail(Err::BadLayout, "page must have 1 or 11 levels");
    std::vector<D3D11_SUBRESOURCE_DATA> init(mips);
    for (uint32_t m = 0; m < mips; ++m) {
        const uint32_t n = kHeightPageSize >> m;
        if (page.levels[m].size() != static_cast<size_t>(n) * n)
            return Status::fail(Err::BadLayout, "page level " + std::to_string(m) + " has the wrong size");
        init[m].pSysMem = page.levels[m].data();
        init[m].SysMemPitch = n;
        init[m].SysMemSlicePitch = n * n;
    }
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = d.Height = kHeightPageSize;
    d.MipLevels = mips;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* t = nullptr;
    HRESULT hr = dev->CreateTexture2D(&d, init.data(), &t);
    if (FAILED(hr)) return Status::fail(Err::Device, "CreateTexture2D (height page) failed");
    ID3D11ShaderResourceView* v = nullptr;
    hr = dev->CreateShaderResourceView(t, nullptr, &v);
    if (FAILED(hr)) {
        t->Release();
        return Status::fail(Err::Device, "CreateShaderResourceView (height page) failed");
    }
    *tex = t;
    *srv = v;
    return Status::Ok();
}

BakeLight stockBakeLight() {
    BakeLight l;
    l.auxDir[0] = fromBits(0x3EDDB3D7u); l.auxDir[1] = fromBits(0xBE800000u); l.auxDir[2] = fromBits(0x3F5DB3D7u);
    l.lightDir[0] = fromBits(0xA3EEAC75u); l.lightDir[1] = fromBits(0xBED8616Cu); l.lightDir[2] = fromBits(0x3F6803CAu);
    bakeHalfVector(l.lightDir, l.halfVector);
    return l;
}

void lightDirection(double zenithDeg, double azimuthDeg, float out[3]) {
    const double k = 3.14159265358979323846 / 180.0;
    const double z = zenithDeg * k, a = azimuthDeg * k;
    out[0] = static_cast<float>(-(std::sin(z) * std::cos(a)));
    out[1] = static_cast<float>(-(std::sin(z) * std::sin(a)));
    out[2] = static_cast<float>(std::cos(z));
}

void bakeHalfVector(const float light[3], float out[3]) {
    volatile float s0 = light[0] + 0.0f, s1 = light[1] + 0.0f, s2 = light[2] + 1.0f;  // camera axis (0, 0, 1)
    volatile float d = s0 * s0 + s1 * s1;
    d = d + s2 * s2;
    volatile float inv = 1.0f / std::sqrt(static_cast<float>(d));
    out[0] = s0 * inv;
    out[1] = s1 * inv;
    out[2] = s2 * inv;
}

uint32_t styleFromBits(uint32_t bits) {
    for (uint32_t s = 1; s < 5; ++s)
        if (bits & (1u << s)) return s;
    return 0;
}

uint32_t styleFromArtTypes(const uint8_t* types, size_t count) {
    uint32_t bits = 0;
    for (size_t i = 0; types && i < count; ++i)
        if (types[i] < 32) bits |= 1u << types[i];
    return styleFromBits(bits);
}

bool tileListsFifthPlot(TileId t) {
    return t.x >= 1 && t.x <= ((static_cast<uint64_t>(t.y) + 1) >> 1);
}

uint32_t tileStyle(const uint8_t* artTypes, uint32_t mapW, uint32_t mapH, bool /*wrapX: the EXE's lists never wrap*/,
                   TileId t) {
    if (!artTypes || mapW == 0 || mapH == 0) return 0;
    // The four plots every tile lists, then the fifth: (t.x - 1, t.y - 2) for even t.y, (t.x - 1, t.y - 1) for odd.
    static const int kEven[5][2] = {{-1, -1}, {0, -2}, {0, -1}, {1, -2}, {-1, -2}};
    static const int kOdd[5][2] = {{-1, -2}, {0, -2}, {0, -1}, {1, -1}, {-1, -1}};
    const int(*pat)[2] = (t.y & 1) ? kOdd : kEven;
    const int count = tileListsFifthPlot(t) ? 5 : 4;
    uint32_t bits = 0;
    for (int k = 0; k < count; ++k) {
        const int64_t x = static_cast<int64_t>(t.x) + pat[k][0];
        const int64_t y = static_cast<int64_t>(t.y) + pat[k][1];
        if (y < 0 || y >= static_cast<int64_t>(mapH)) continue;
        if (x < 0 || x >= static_cast<int64_t>(mapW)) continue;  // no wrap: the EXE's edge tiles list 3 plots
        const uint8_t a = artTypes[static_cast<size_t>(y) * mapW + static_cast<size_t>(x)];
        if (a < 32) bits |= 1u << a;
    }
    return styleFromBits(bits);
}

uint32_t sizeClassForProjectedSize(float p, uint32_t minClass) {
    uint32_t c = 64;
    if (p > 256.0f) c = 512;
    else if (p > 128.0f) c = 256;
    else if (p > 64.0f) c = 128;
    return c < minClass ? minClass : c;
}

Status bakeTimeConstants(const heightfield::HeightGrid& g, TileId t, const uint8_t* artTypes, size_t artTypeCount,
                         BakeTimeConstants& out) {
    out = BakeTimeConstants();
    BakeTimeConstants r;
    C5R_TRY(heightPageForTile(g, t, r.page, r.pageOffset));
    const BakeLight l = stockBakeLight();
    for (int i = 0; i < 3; ++i) {
        r.k.halfVector[i] = l.halfVector[i];
        r.k.shadowDir[i] = l.auxDir[i];
    }
    r.k.hexScale[0] = 1.0f;
    r.k.hexScale[1] = 0.866f;
    r.k.hexScale[2] = 0.2f;
    r.k.heightTexelScale[0] = 1.0f / kHeightPageSize;
    r.k.heightTexelScale[1] = -1.0f / kHeightPageSize;
    r.k.heightOffset[0] = static_cast<float>(r.pageOffset[0]);
    r.k.heightOffset[1] = static_cast<float>(r.pageOffset[1]);
    r.style = styleFromArtTypes(artTypes, artTypeCount);
    r.sizeClass = kCacheSizeDefault;
    out = r;
    return Status::Ok();
}

}} // namespace c5r::bake
