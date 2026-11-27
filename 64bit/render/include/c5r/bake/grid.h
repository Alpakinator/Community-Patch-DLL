// c5r/bake/grid.h - bake stage 1, issue 3405: the height field's per-sample normals as the EXE's vertex jobs see them,
// the CacheBake grid normals (tile+0x308, NORMAL f16x4) and the blend's slope classes, from one port.
//
// Behaviour follows research/05-graphics-composition/terrain-bake-passes.md section 5.1 and terrain-bake-records.md
// 2.2a, written from a static reading in words and tables (D83: no decompiler output in this repository):
//   - the normal of a height sample is the filter of 2.2a (the list builder's 0x747580; the vertex jobs call 0x728240,
//     the same code with a 16 x 16 window instead of 76 x 76): integer central differences, two 16-tap weighted sums
//     with the plane-prediction quirk, the plot steps, a normalisation;
//   - it depends only on the sample's 5 x 5 neighbourhood, fetched with x wrapped (when the map wraps) and clamped,
//     y clamped (never wrapped), so it is a pure function of the sample coordinates;
//   - vertex (i, j) of a tile's (ex+2) x (ey+2) grid stands on sample (ox + i - 1, oy + j - 1), not wrapped;
//   - the CacheBake vertex's NORMAL is (nx, ny, nz, 1) as IEEE half floats, rounded to nearest even;
//   - the slope class (nz > 0.9 -> 0, else 1 when |ny| > |nx|, else 2) comes from the same single-precision normal.
//
// Every function returns a Status and never throws; each check names the Err a negative test asserts.
#pragma once

#include <cstdint>
#include <vector>

#include "c5r/bake/blend.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/status.h"

namespace c5r {
namespace bake {

// Largest window sampleNormals accepts in one call (each side); a tile grid is 67.
constexpr uint32_t kMaxNormalWindow = 4096;

// The normals of the w x h samples x0 .. x0+w-1, y0 .. y0+h-1 (any coordinates, also outside the field: the fetch
// wraps and clamps as the EXE does). out gets w * h float3, row-major (row = y). Checks: no heights or
// h.size() != samplesX * samplesY or samplesX/Y 0 [BadLayout]; scales not finite [BadLayout]; w or h 0 or above
// kMaxNormalWindow [BadLayout].
Status sampleNormals(const heightfield::HeightGrid& g, int32_t x0, int32_t y0, uint32_t w, uint32_t h,
                     std::vector<float>& out);

// The tile's grid normals: (extentX+2) x (extentY+2) float3, row-major (j outer), vertex (i, j) = the normal of
// sample (originX + i - 1, originY + j - 1). Checks: those of sampleNormals; extents 0 or above 65 [BadLayout].
Status gridNormals(const heightfield::HeightGrid& g, const TileGeom& tile, std::vector<float>& out);

// IEEE 754 binary16 from single precision, round to nearest even, as D3DXFloat32To16Array writes the vertex
// (overflow to infinity, NaN kept quiet, subnormals rounded).
uint16_t floatToHalf(float f);
// The inverse (exact).
float halfToFloat(uint16_t h);

// The CacheBake NORMAL field of every vertex: count x 4 halves (nx, ny, nz, 1.0). Checks: normals.size() not a
// multiple of 3 [BadLayout].
Status packGridNormals(const std::vector<float>& normals, std::vector<uint16_t>& out);

// The slope class of every normal (blend.h slopeClass), one byte per float3: the SlopeClasses fillBlendVertices takes
// when given a tile's gridNormals. Checks: normals.size() not a multiple of 3 [BadLayout].
Status slopeClassesFromNormals(const std::vector<float>& normals, SlopeClasses& out);

// Convenience for the baker: gridNormals + slopeClassesFromNormals in one call (normals may be null).
Status tileSlopeClasses(const heightfield::HeightGrid& g, const TileGeom& tile, SlopeClasses& out,
                        std::vector<float>* normals = nullptr);

}  // namespace bake
}  // namespace c5r
