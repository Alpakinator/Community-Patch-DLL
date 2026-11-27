// c5r/heightfield/heightfield.h - the EXE's global height field, tapped from the EXE (task 2904, epic 2900).
//
// Decision (2026-11-11, research/05-graphics-composition/asset-pipeline.md section 4): the server does not re-compute
// the height field; it takes the EXE's finished field (the "tap"). This module is the server side of that tap: the
// layout of the EXE's GridHeightmap object 0xD91270 and of its two FDataField layers, flattening of raw memory copies
// into a dense grid, the tap file/shared-memory format (C5HF), the sample <-> world relation, the plot classification
// the EXE's bake starts from, a grid comparison and a 16-bit PNG preview. Everything here is the author's own code;
// the layout facts are [C] from the decompile of CivilizationV_DX11.exe (VAs at ImageBase 0x400000).
//
// Layout of GridHeightmap 0xD91270 after GridHeightmap::Init 0x47E350 [C]:
//   +0x00 f32 1.0       +0x08 u8 1        +0x09 u8 ready (1 after Init)
//   +0x0C ushort layer (FDataField, 16x16 tiles, 2 bytes per sample):
//        +0x0C width  +0x10 height (samples, rounded up to 16)   +0x14 tilesX  +0x18 tilesY
//        +0x1C directory ptr (tilesX*tilesY entries of 8 bytes)   +0x20 pool free-list head (0x0FFFFFFF = none)
//        +0x24 tiles in use  +0x28 pool ptr (512-byte tiles)      +0x2C pool size (tiles)  +0x30 pool capacity
//   +0x4C byte layer (same FDataField shape, 256-byte tiles): +0x4C width +0x50 height +0x54 tilesX +0x58 tilesY
//        +0x5C directory ptr  +0x60 free head  +0x64 in use  +0x68 pool ptr  +0x6C pool size  +0x70 capacity
//   +0xA0 samplesX = mapW*64   +0xA4 samplesY = mapH*64 (mapH here = grid plot rows = the DLL's map height + 4 [L])   +0xA8 sx (1.0)  +0xAC sy (0.866)  +0xB0 sz (0.2)
//   +0xB4 ox = mapW*32*sx      +0xB8 oy = mapH*32*sy (same mapH)     +0xBC 0         +0xC0 world width  +0xC4 world height
// Directory entry (8 bytes): u16 state (1 = constant tile, 0 = pooled tile), u16 unused, u32 payload (state 1: the
// constant value in the low 16 / 8 bits; state 0: the pool tile index). Sample (x, y) of a pooled tile is at
// pool[index*256 + (y&15)*16 + (x&15)] (ushort layer; bytes for the byte layer). Init pools every tile [C], so a
// field read right after a load has no constant tiles; the flattener accepts both.
// Height of a sample [C 0x6D8B60]: z = u16 * (1/255) * sz (world units). With map wrap (0xD90FBC == 1) x is taken
// modulo samplesX; outside the grid the EXE returns 0. World position of sample (c, r) [C 0x6851C0, 0x47E350]:
// x = c*sx - ox, y = r*sy - oy.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "c5r/status.h"

namespace c5r { namespace heightfield {

const uint32_t kSamplesPerPlot = 64;  // CreateTerrain 0x709B50 passes mapW<<6, mapH<<6 [C]
const uint32_t kTile = 16;             // FDataField tile edge [C 0x798CB0]
const uint32_t kGridObjectSize = 0xC8; // bytes of 0xD91270 this module reads (+0x00 .. +0xC7)
const uint32_t kMaxPlots = 256;        // sanity bound on map width / height in plots (MAX_SUPPORTED_MAP is 152 x 96)

// The fields of the GridHeightmap object that a reader needs. Pointers are 32-bit addresses in the EXE.
struct GridObject {
    bool ready = false;
    uint32_t samplesX = 0, samplesY = 0;  // +0xA0 / +0xA4
    float sx = 0, sy = 0, sz = 0, ox = 0, oy = 0, worldW = 0, worldH = 0;
    struct Layer {
        uint32_t width = 0, height = 0, tilesX = 0, tilesY = 0;
        uint32_t dirPtr = 0, freeHead = 0, inUse = 0, poolPtr = 0, poolSize = 0, poolCapacity = 0;
    } h16, b8;  // +0x0C ushort heights, +0x4C byte layer (material / region index per sample)
};

// Parses a raw copy of the GridHeightmap object (at least kGridObjectSize bytes from 0xD91270).
// Checks: size < kGridObjectSize [Truncated]; ready byte != 1 [BadLayout]; samplesX or samplesY 0, not a multiple of
// 64, or above kMaxPlots*64 [BadLayout]; a layer whose width/height differ from samplesX/Y or whose tile counts are not
// width/16, height/16 [BadLayout]; sx, sy or sz not finite or <= 0 [BadLayout].
Status parseGridObject(const uint8_t* p, size_t n, GridObject& out);

// Flattens a raw copy of an FDataField's tile directory and pool into a dense row-major grid (x fastest).
// bytesPerSample: 2 (heights) or 1 (byte layer); out receives tilesX*16 * tilesY*16 values (bytes are widened).
// Checks: bytesPerSample not 1 or 2 [Unsupported]; tilesX or tilesY 0 or above kMaxPlots*4 [BadLayout];
// dirSize < tilesX*tilesY*8 [Truncated]; a directory state other than 0 or 1 [BadLayout]; a pool index whose tile
// lies past poolSize bytes [BadLayout].
Status flattenTiles(const uint8_t* dir, size_t dirSize, const uint8_t* pool, size_t poolSize, uint32_t tilesX,
                    uint32_t tilesY, uint32_t bytesPerSample, std::vector<uint16_t>& out);

// The dense height field as the server uses it: the EXE's samples in the EXE's row-major order (the order of the
// EXE's own dense export 0x493DD0: rows y = 0..samplesY-1, x fastest).
struct HeightGrid {
    uint32_t mapW = 0, mapH = 0;          // grid plots: mapW = map width, mapH = map height + 4 (4 margin rows;
                                          // live 2026-11-11: huge 128x80 -> 84, Tiny 56x36 -> 40)
    uint32_t samplesX = 0, samplesY = 0;  // mapW*64, mapH*64
    bool wrapX = false;                   // 0xD90FBC == 1 (the map wraps in x)
    float sx = 1.0f, sy = 0.866f, sz = 0.2f, ox = 0, oy = 0;
    float waterZ = 0;                     // 0xD91274: TerrainSystem+0x277F4 * sz * 255 (set by CreateTerrain) [C]
    uint32_t mapSeed = 0;                 // PreGame mapRandomSeed (CreateTerrain's srand seed), informational
    float hexToWorld[16] = {0};           // 0xD91030, row-major as in memory (D3DXMATRIX), informational
    std::vector<uint16_t> h;              // samplesX*samplesY heights
    std::vector<uint8_t> mat;             // samplesX*samplesY byte layer, empty if not tapped
    size_t index(uint32_t x, uint32_t y) const { return static_cast<size_t>(y) * samplesX + x; }
};

// Builds a HeightGrid from raw copies of the object and its layers (what a memory dump or the DLL-side tap copies).
// dir8/pool8 may be null (then mat stays empty). Checks: those of parseGridObject and flattenTiles; the flattened
// layer larger than samplesX x samplesY is cropped (the EXE rounds up to 16, which W*64 never needs).
Status gridFromMemory(const uint8_t* obj, size_t objSize, const uint8_t* dir16, size_t dir16Size,
                      const uint8_t* pool16, size_t pool16Size, const uint8_t* dir8, size_t dir8Size,
                      const uint8_t* pool8, size_t pool8Size, bool wrapX, HeightGrid& out);

// The inverse, for tests and for hfbake's synthetic mode: the memory a GridHeightmap holding g would have (object
// bytes, directories, pools), laid out as Init 0x47E350 leaves it (every tile pooled, pool index = row-major tile
// order). With compressConstant, uniform tiles become constant directory entries instead (state 1), the other state the
// EXE's FDataField supports. Checks: g's sizes inconsistent [BadLayout]; mat empty gives an all-zero byte layer.
struct MemoryImage {
    std::vector<uint8_t> obj, dir16, pool16, dir8, pool8;
};
Status buildMemoryImage(const HeightGrid& g, bool compressConstant, MemoryImage& out);

// ---- The tap format "C5HF" (file or shared-memory section), little-endian ----
//   0  char[4] "C5HF"     4 u32 version (1)    8 u32 mapW    12 u32 mapH    16 u32 samplesX    20 u32 samplesY
//   24 u32 flags (bit 0 wrapX, bit 1 byte layer present)    28 f32 sx  32 f32 sy  36 f32 sz  40 f32 ox  44 f32 oy
//   48 f32 waterZ   52 u32 mapSeed   56 f32[16] hexToWorld   120 u32[2] reserved (0)
//   128 u16[samplesY][samplesX] heights; then, if flag bit 1, u8[samplesY][samplesX] byte layer.
const uint32_t kTapHeaderSize = 128;
const uint32_t kTapVersion = 1;
// Checks: size < 128 [Truncated]; magic [BadMagic]; version [BadVersion]; mapW/mapH 0 or above kMaxPlots, samplesX !=
// mapW*64 or samplesY != mapH*64 [BadLayout]; unknown flag bits or nonzero reserved words [BadLayout]; payload shorter
// than the header announces [Truncated]. Trailing bytes are allowed (a shared section is page-rounded).
Status readTap(const uint8_t* p, size_t n, HeightGrid& out);
// Serialises g; BadLayout if g's sizes are inconsistent (h.size() != samplesX*samplesY, mat neither empty nor that size,
// samples != plots*64).
Status writeTap(const HeightGrid& g, std::vector<uint8_t>& out);
size_t tapSize(uint32_t mapW, uint32_t mapH, bool withMaterial);

// ---- World relation (the EXE's consumers) ----
// HeightAtGrid 0x6D8B60: wraps x when wrapX, returns 0 outside the grid, else u16/255*sz.
float heightAtSample(const HeightGrid& g, int x, int y);
// World position of a sample (x = c*sx - ox, y = r*sy - oy, z = heightAtSample).
void sampleToWorld(const HeightGrid& g, int c, int r, float out[3]);

// ---- Comparison (re-bake or second tap against a reference dump) ----
struct CompareStats {
    uint64_t samples = 0, differing = 0, overTolerance = 0;
    uint32_t maxAbs = 0, maxX = 0, maxY = 0;
    double meanAbs = 0, rms = 0;
    uint64_t hist[6] = {0};  // |d| = 0, 1, 2..3, 4..15, 16..255, >= 256
    uint64_t matDiffering = 0;  // byte layer, when both have it
};
// BadLayout if the sample dimensions differ.
Status compare(const HeightGrid& a, const HeightGrid& b, uint32_t tolerance, CompareStats& out);

// ---- The bake's per-plot input (what StartInitTerrainFromGameplayMap 0x60ABB0 writes into the HexPatternCell grid) ----
// Terrain ids are the game's TerrainTypes order (GRASS 0, PLAINS 1, DESERT 2, TUNDRA 3, SNOW 4, COAST 5, OCEAN 6).
struct PlotInfo {
    int terrain = 0;
    bool hills = false, mountain = false;
    int feature = -1;              // FeatureTypes id, -1 none
    bool naturalWonder = false;    // the feature's IsNaturalWonder
    std::string featureType;       // the feature's Type string, e.g. "FEATURE_GEYSER"
};
// Cell flags as 0x60ABB0 computes them [C]: water first (coast 0x2000, ocean 0x4000), then mountain 0x400, then flat
// (grass 0x1, plains 0x2, desert 0x4, tundra 0x8, snow 0x10) or hills (0x20, 0x40, 0x80, 0x100, 0x200); a natural
// wonder replaces the value by its own bit (table in heightfield.cpp). A grid position whose plot lookup returns null
// gets kCellNoPlot; cells of the pattern grid that no plot maps to keep the pre-fill kCellBorder.
const uint32_t kCellNoPlot = 0x4000, kCellBorder = 0x2000;
uint32_t patternCellFlags(const PlotInfo& p);

// ---- Preview ----
// Writes a 16-bit greyscale PNG through WIC (Windows SDK). stretch: map [min, max] of the data to [0, 65535].
// Checks: w or h 0, or px.size() != w*h [BadLayout]; COM/WIC failures [Device]; file errors [Io].
Status writePng16(const std::wstring& path, uint32_t w, uint32_t h, const std::vector<uint16_t>& px, bool stretch);

}} // namespace c5r::heightfield
