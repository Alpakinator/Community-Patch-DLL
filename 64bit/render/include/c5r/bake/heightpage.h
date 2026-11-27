// c5r/bake/heightpage.h - the terrain bake's height pages (tile+0xEC, the macro shadow's input) and the constants the
// bake reads at bake time (issue 3406, epic 3400).
//
// Behaviour: research/05-graphics-composition/terrain-bake-passes.md section 5.0 (the page rule) and 5.3 (the bake-time
// constants). D83: written from that description; no decompiler output, no EXE bytes.
//
// Pages. The EXE covers the height field's sample plane with 1024 x 1024 A8_UNORM pages in steps of 896 samples, 64
// samples of margin on each side, (samplesX + 896) / 896 by (samplesY + 896) / 896 pages. Page (px, py), texel (u, v):
//   sample x = (px * 896 - 64 + u) mod samplesX,  sample y = (py * 896 - 64 + 1023 - v) mod samplesY  (both axes wrap,
//   whatever the map's wrap flag; rows bottom-up), texel = u16 height >> 8.
// Mips 1..10 (11 levels in all): from the previous level as stored, a 2 x 2 box with truncation, then a 3 x 3 sharpen
// b + ((9b - sum of the 8 clamped neighbours - b) >> 3), clamped to 0..255.
// A tile (x, y) (origin x * 64, y * 64 samples) uses page (originX / 896, originY / 896) at the offset
// (64 - px * 896, 64 - py * 896) = tile+0xE4 / +0xE8.
//
// Constants. Every bake sees a fixed identity view, so the half vector (and with it t8) does not depend on the camera;
// the light and shadow directions are the same for every map and session.
#pragma once
#include <cstdint>
#include <vector>
#include <d3d11.h>
#include "c5r/status.h"
#include "c5r/bake/bake_types.h"
#include "c5r/bake/post.h"

namespace c5r { namespace heightfield { struct HeightGrid; } }

namespace c5r { namespace bake {

constexpr uint32_t kHeightPageSize = 1024;   // texels per side
constexpr uint32_t kHeightPageStep = 896;    // samples between page origins
constexpr uint32_t kHeightPageMargin = 64;   // samples of margin before the page origin (and after its 896)
constexpr uint32_t kHeightPageMips = 11;     // with BicubicTerrainTessellation on (the stock DX11 setting); else 1
constexpr uint32_t kHeightPageBytes = 1398101; // all 11 levels: 1024^2 + 512^2 + ... + 1

struct HeightPageIndex {
    uint32_t px = 0, py = 0;
};

// Number of pages in x and y. BadLayout when the grid's sample counts are 0 or not equal to its plots * 64.
Status heightPageCount(const heightfield::HeightGrid& g, uint32_t& pagesX, uint32_t& pagesY);

// The page of tile t and its offset (tile+0xE4, tile+0xE8). NotFound when the tile lies outside the grid
// (t.x >= mapW or t.y >= mapH); BadLayout as heightPageCount.
Status heightPageForTile(const heightfield::HeightGrid& g, TileId t, HeightPageIndex& page, int32_t offset[2]);

// tile+0xE4 / +0xE8 of any tile on page p: 64 - px * 896, 64 - py * 896.
void heightPageOffset(HeightPageIndex p, int32_t offset[2]);

// One page as the EXE uploads it: level m is (1024 >> m)^2 bytes, rows as stored (row 0 first).
struct HeightPage {
    HeightPageIndex index;
    std::vector<std::vector<uint8_t>> levels;
};

// Builds page p from g. mips: 11 (the stock DX11 page) or 1 (BicubicTerrainTessellation off). Errors: BadLayout as
// heightPageCount, or g.h.size() != samplesX * samplesY; NotFound for a page index past the count; Unsupported for a
// mip count other than 1 or 11. On error `out` is empty.
Status buildHeightPage(const heightfield::HeightGrid& g, HeightPageIndex p, uint32_t mips, HeightPage& out);

// Level m + 1 from level m (n x n, n >= 2, n even) by the EXE's box + sharpen rule; exposed for the tests.
// BadLayout when n is 0, odd or src.size() != n * n.
Status heightPageNextMip(const std::vector<uint8_t>& src, uint32_t n, std::vector<uint8_t>& dst);

// An A8_UNORM texture (immutable, SHADER_RESOURCE) with the page's levels, and its view, for PostInputs::heightPage.
// The caller releases both. Errors: BadLayout (no device, a page without levels or with wrong level sizes), Device.
Status createHeightPageTexture(ID3D11Device* dev, const HeightPage& page, ID3D11Texture2D** tex,
                               ID3D11ShaderResourceView** srv);

// ---- Bake-time constants (terrain-bake-passes.md 5.3) ----

// The stock values, as float bits (the EXE computes them in x87 extended precision; these are the stored floats).
struct BakeLight {
    float auxDir[3];     // TerrainFrame reg 17: macro-shadow direction, zenith 30 deg, azimuth 150 deg
    float lightDir[3];   // TerrainFrame reg 18: sun direction, zenith 25 deg, azimuth 90 deg
    float halfVector[3]; // TerrainFrame reg 21: normalize(lightDir + (0, 0, 1)), float arithmetic
};
BakeLight stockBakeLight();

// dir(zenith, azimuth) = (-sin z cos a, -sin z sin a, cos z) in double, stored as float (for non-stock angles; the
// stock LightDir.x differs from this in its last bits, see 5.3).
void lightDirection(double zenithDeg, double azimuthDeg, float out[3]);

// The half vector as the EXE forms it: s = light + (0, 0, 1) in float, s * (1 / sqrtf(dot(s, s))).
void bakeHalfVector(const float light[3], float out[3]);

// The terrain style from tile+0x138's bits: the lowest set bit 1..4, 0 (Generic) when none.
uint32_t styleFromBits(uint32_t bits);
// The same from the continent art types (0 none, 1 America, 2 Asia, 3 Africa, 4 Europe) of the plots a tile lists:
// bit (1 << type) per plot, types >= 32 ignored.
uint32_t styleFromArtTypes(const uint8_t* types, size_t count);

// The style of tile t from every plot's continent art type (artTypes[y * mapW + x], the DLL's Plot:GetContinentArtType;
// mapW x mapH plots, the DLL's map, i.e. the height grid's mapH - 4 rows). The tile lists the plots of plot rows
// t.y - 2 and t.y - 1: for even t.y (t.x - 1, t.y - 1), (t.x, t.y - 2), (t.x, t.y - 1), (t.x + 1, t.y - 2); for odd t.y
// (t.x - 1, t.y - 2), (t.x, t.y - 2), (t.x, t.y - 1), (t.x + 1, t.y - 1). A tile with 1 <= t.x <= (t.y + 1) / 2
// (tileListsFifthPlot) lists a fifth plot of column t.x - 1: (t.x - 1, t.y - 2) for even t.y, (t.x - 1, t.y - 1) for
// odd (ticket 0383, terrain-bake-passes.md 5.3). Plots outside the map are skipped and x does not wrap: the EXE's
// lists never wrap (its column-0 and last-column tiles list 3 plots on a wrapping map); wrapX is kept for the callers
// and ignored. Against 3406's probes this gives the EXE's plot list for every tile inside the map rows and the EXE's
// style for all 2,240 Tiny and all 10,752 huge tiles. Returns 0 when artTypes is null or the sizes are 0.
uint32_t tileStyle(const uint8_t* artTypes, uint32_t mapW, uint32_t mapH, bool wrapX, TileId t);
// Whether tile t lists the fifth plot above: 1 <= t.x <= (t.y + 1) / 2 (integer division), a triangle at the map's
// left that grows by one column every two tile rows (1,764 of 10,752 tiles on a 128 x 80 map; 1,763 of them list five
// plots, tile (1, 1) lists 3 because two of its four are above the map).
bool tileListsFifthPlot(TileId t);

// The size class the EXE wants for a projected tile size p (0x4B2B40): 512 if p > 256, 256 if p > 128, 128 if p > 64,
// else 64, then at least minClass. The server's default for baking ahead is kCacheSizeDefault (256).
uint32_t sizeClassForProjectedSize(float p, uint32_t minClass);

// Everything the post passes take from the bake-time state for tile t: PostConstants (half vector, shadow direction,
// hex scale, height texel scale, height offset; normal step / strength and shadow draws left at their defaults), the
// page index, the style (from `artTypes` when given, else 0) and the size class (kCacheSizeDefault). Errors as
// heightPageForTile.
struct BakeTimeConstants {
    PostConstants k;
    HeightPageIndex page;
    int32_t pageOffset[2] = {0, 0};
    uint32_t style = 0;
    uint32_t sizeClass = kCacheSizeDefault;
};
Status bakeTimeConstants(const heightfield::HeightGrid& g, TileId t, const uint8_t* artTypes, size_t artTypeCount,
                         BakeTimeConstants& out);

}} // namespace c5r::bake
