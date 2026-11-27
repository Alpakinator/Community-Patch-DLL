// c5r/farmfields/footprint.h - the field footprints of the server's farm layout as a cut mask for the trees (ticket 4853,
// epic 0528 D; research/05-graphics-composition/farm-fields-layout.md 10.1 and 14). Written 2026-11-18 for this repository
// (GPL-3.0-or-later). Own code from the knowledge base's description (D83).
//
// What stock does (farm-fields-layout.md 4.2 step 1, cities-and-landmarks.md 2.7, trees.md 2.3) [C]: the farm manager
// rasterises every field polygon into the plot mask grid with bit 0x01, and bits 0-2 of that grid hide a tree. With the
// EXE's farm builder retired ([Renderer] ServerFarmFields = 1) nothing stamps the fields any more, so the server stamps its
// own: this mask, ORed into the forest's cut source (srvrender::CutOverlaySlot, server/src/render/srv_cut_tap.h).
//
// The grid: cells of 2 x 2 samples in the decal sample frame (the frame of FieldPolygon::corners: plots 64 apart in x,
// 64 between rows; the frame c5r::forest::cutCell maps a world point into: sample = (ox + x, (oy + y) x 1.1547344)), cell
// (col, row) = (int(X) >> 1, int(Y) >> 1). A cell is set when its sample point (2 col, 2 row), the cell's low corner,
// lies inside a field polygon (even-odd rule; left and bottom edges in, right and top out). [I: the best of four rules
// tried against one stock huge session on oldpc, 2026-11-18, farm-fields-layout.md 14.7: it sets the fewest cells the
// EXE's grid lacks (2,679, all on field outlines, against 6,665 for the cell centre); the EXE's rasteriser 0x6287A0 was
// not read.] The Crops_Damage copy
// of a pillaged farm's field has the same corners and sets nothing more. Tracks set nothing (the EXE stamps only the field
// polygons).
#pragma once
#include <cstdint>
#include <vector>
#include "c5r/farmfields/farmfields.h"
#include "c5r/forest/cut.h"
#include "c5r/status.h"

namespace c5r { namespace farmfields {

const uint8_t kFieldFootprintBit = 0x01;   // the farm manager's footprint bit (cities-and-landmarks.md 2.7) [C]

class FieldFootprint : public forest::CutSource {
public:
    // The footprints of every field of `layout` over the world rectangle (sample frame: cells 0 .. int(max) >> 1 on each
    // axis). Errors: BadLayout (the rectangle not finite, max < 0, or more than 16384 cells on an axis; a field corner
    // not finite). On an error the mask is empty.
    Status build(const FarmLayout& layout, const float worldRect[4]);
    // The height field's ox / oy (HeightGrid::ox / oy), for bitsAt's world point (cutCell's rule).
    void setFrame(float ox, float oy) { ox_ = ox; oy_ = oy; }
    // kFieldFootprintBit when the world point's cell is set, else 0 (also outside the grid).
    uint8_t bitsAt(float worldX, float worldY) const override;
    // The cell in the sample frame (tests, the evidence): true when set.
    bool cell(int32_t col, int32_t row) const;
    // The generation the owner gave it (FarmFieldsProducer counts its footprints); 0 by default.
    uint64_t generation() const override { return generation_; }
    void setGeneration(uint64_t g) { generation_ = g; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    uint64_t cellsSet() const { return cellsSet_; }
    // FNV-1a 64 over width, height and the bits: two masks with the same hash cut the same trees (the owner publishes a
    // new mask only when the hash moved, so an unchanged layout rebuilds no tree).
    uint64_t hash() const { return hash_; }

private:
    uint32_t width_ = 0, height_ = 0;
    float ox_ = 0, oy_ = 0;
    std::vector<uint64_t> bits_;   // row-major, width_ bits per row rounded up to 64
    uint32_t wordsPerRow_ = 0;
    uint64_t cellsSet_ = 0, hash_ = 0, generation_ = 0;
};

}} // namespace c5r::farmfields
