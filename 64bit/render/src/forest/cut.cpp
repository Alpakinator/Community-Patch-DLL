// render/src/forest/cut.cpp - the cut-mask seam of the tree placement (epic 0431, ticket 4550). See c5r/forest/cut.h.
#include "c5r/forest/cut.h"

#include <cmath>
#include <cstring>
#include <string>

namespace c5r { namespace forest {

CutCell cutCell(float worldX, float worldY, float ox, float oy)
{
    CutCell c;
    const float sx = ox + worldX;
    const float sy = (oy + worldY) * kCutRowScale;
    // Truncation toward zero (cvttss2si), then an arithmetic shift: a negative sum gives a negative cell (read as 0).
    // Values far outside the int range are clamped to a cell no grid holds.
    const float lim = 1.0e9f;
    const int32_t ix = std::isfinite(sx) ? static_cast<int32_t>(sx > lim ? lim : (sx < -lim ? -lim : sx)) : -2;
    const int32_t iy = std::isfinite(sy) ? static_cast<int32_t>(sy > lim ? lim : (sy < -lim ? -lim : sy)) : -2;
    c.col = ix >> 1;
    c.row = iy >> 1;
    return c;
}

Status DenseCut::set(uint32_t width, uint32_t height, std::vector<uint8_t> cells, float ox, float oy)
{
    if (width == 0 || height == 0 || width > 16384 || height > 16384)
        return Status::fail(Err::BadLayout, "DenseCut: width or height 0 or above 16384");
    if (cells.size() != static_cast<size_t>(width) * height)
        return Status::fail(Err::BadLayout, "DenseCut: cells " + std::to_string(cells.size()) + " != width x height");
    if (!std::isfinite(ox) || !std::isfinite(oy)) return Status::fail(Err::BadLayout, "DenseCut: ox / oy not finite");
    width_ = width;
    height_ = height;
    cells_ = std::move(cells);
    ox_ = ox;
    oy_ = oy;
    ++generation_;
    return Status::Ok();
}

Status DenseCut::setFromBlocks(uint32_t tilesX, uint32_t tilesY, const uint8_t* dir, size_t dirSize,
                               const uint8_t* pool, size_t poolSize, float ox, float oy)
{
    if (tilesX == 0 || tilesY == 0 || tilesX > 1024 || tilesY > 1024)
        return Status::fail(Err::BadLayout, "DenseCut: tiles 0 or above 1024");
    const size_t n = static_cast<size_t>(tilesX) * tilesY;
    if (!dir || dirSize < n * 8) return Status::fail(Err::BadLayout, "DenseCut: directory shorter than tiles x 8");
    const uint32_t w = tilesX * kCutBlock, h = tilesY * kCutBlock;
    std::vector<uint8_t> cells(static_cast<size_t>(w) * h, 0);
    for (size_t t = 0; t < n; ++t) {
        uint16_t state;
        uint32_t payload;
        std::memcpy(&state, dir + t * 8, 2);
        std::memcpy(&payload, dir + t * 8 + 4, 4);
        const uint32_t tx = static_cast<uint32_t>(t % tilesX), ty = static_cast<uint32_t>(t / tilesX);
        for (uint32_t r = 0; r < kCutBlock; ++r)
            for (uint32_t c = 0; c < kCutBlock; ++c) {
                uint8_t v;
                if (state != 0) {
                    v = static_cast<uint8_t>(payload & 0xFF);
                } else {
                    const size_t at = static_cast<size_t>(payload) * 256 + r * kCutBlock + c;
                    if (!pool || static_cast<uint64_t>(payload) * 256 + 256 > poolSize)
                        return Status::fail(Err::BadLayout, "DenseCut: pool index " + std::to_string(payload) +
                                                                " of tile " + std::to_string(t) + " past the pool");
                    v = pool[at];
                }
                cells[static_cast<size_t>(ty * kCutBlock + r) * w + tx * kCutBlock + c] = v;
            }
    }
    return set(w, h, std::move(cells), ox, oy);
}

uint8_t DenseCut::bitsAt(float worldX, float worldY) const
{
    if (cells_.empty()) return 0;
    const CutCell c = cutCell(worldX, worldY, ox_, oy_);
    if (c.col < 0 || c.row < 0 || static_cast<uint32_t>(c.col) >= width_ || static_cast<uint32_t>(c.row) >= height_)
        return 0;
    return cells_[static_cast<size_t>(c.row) * width_ + static_cast<uint32_t>(c.col)];
}

}} // namespace c5r::forest
