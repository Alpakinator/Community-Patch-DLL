// c5r/forest/cut.h - the "cut" mask the tree placement reads (epic 0431, ticket 4550; the stand-in that fills it is 4553).
//
// Facts (research/05-graphics-composition/trees.md 2.3, 10): the EXE hides a tree (instance scale 0, no shadow quad) when
// its cut mask has any of bits 0-2 at the tree's world point. The mask is a byte grid of 2-unit cells in the EXE's sample
// frame, stored as 16 x 16-cell blocks (a directory of 8-byte entries: u16 state, nonzero = the whole block holds the
// byte at +4; 0 = a pooled block, index at +4, 256 bytes in the pool). The cell of a world point (x, y) (HexToWorld's
// output, x = sample.x * sx - ox, y = sample.y * sy - oy) is
//     col = int(ox + x) >> 1,   row = int((oy + y) * 1.1547344f) >> 1        (float sums, truncation, arithmetic shift)
// and a point whose col or row is negative or not below the grid's width / height in cells reads 0. Stamps (routes,
// improvements, wonders, landmark tree-cut surfaces, CreateTerrain's own) write it and raise a change event; the EXE then
// rebuilds the trees of every plot (trees.md 8.8).
//
// This header is the seam: CutSource is what the placement reads; EmptyCut (nothing cut) is the implementation 4550
// ships; DenseCut holds a whole grid of cells for a reader of the EXE's grid (4553) or a test. Own code from the
// knowledge base's description (D83).
#pragma once
#include <cstdint>
#include <vector>
#include "c5r/status.h"

namespace c5r { namespace forest {

const uint8_t kCutTreeBits = 0x07;              // the bits that hide a tree (mask 7 of the EXE's lookup) [C]
const float kCutRowScale = 1.1547344f;          // 1 / 0.866 as the EXE stores it (0x9F3498) [C]
const uint32_t kCutBlock = 16;                  // cells per block edge in the EXE's grid [C]

struct CutCell {
    int32_t col = 0, row = 0;
};
// The cell rule above, given the height field's ox, oy (HeightGrid::ox / oy).
CutCell cutCell(float worldX, float worldY, float ox, float oy);

class CutSource {
public:
    virtual ~CutSource() {}
    // All bits of the mask at a world point (the caller masks them with kCutTreeBits).
    virtual uint8_t bitsAt(float worldX, float worldY) const = 0;
    // Bumped by the source on every change of the mask; a placement that built blocks at another generation rebuilds them.
    virtual uint64_t generation() const = 0;
};

// Nothing is cut (every lookup 0, generation 0): today's state until 4553's stand-in exists.
class EmptyCut : public CutSource {
public:
    uint8_t bitsAt(float, float) const override { return 0; }
    uint64_t generation() const override { return 0; }
};

// A whole grid of cells, row-major (cell (col, row) at cells[row * width + col]), with the placement's ox / oy.
class DenseCut : public CutSource {
public:
    // Errors: BadLayout (width or height 0 or above 16384, cells.size() != width * height, ox / oy not finite).
    Status set(uint32_t width, uint32_t height, std::vector<uint8_t> cells, float ox, float oy);
    // Fills from the EXE's block form (a directory of tilesX * tilesY 8-byte entries and a pool of 256-byte blocks), as
    // 4553's reader will copy it. Errors: BadLayout (tiles 0, directory shorter than tilesX * tilesY * 8, a pool index
    // past the pool, ox / oy not finite).
    Status setFromBlocks(uint32_t tilesX, uint32_t tilesY, const uint8_t* dir, size_t dirSize, const uint8_t* pool,
                         size_t poolSize, float ox, float oy);
    uint8_t bitsAt(float worldX, float worldY) const override;
    uint64_t generation() const override { return generation_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }

private:
    uint32_t width_ = 0, height_ = 0;
    float ox_ = 0, oy_ = 0;
    std::vector<uint8_t> cells_;
    uint64_t generation_ = 0;
};

}} // namespace c5r::forest
