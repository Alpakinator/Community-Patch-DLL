// footprint.cpp - the field footprints as a cut mask (ticket 4853; contract: c5r/farmfields/footprint.h).
// Written 2026-11-18 for this repository (GPL-3.0-or-later). Own code from the knowledge base's description (D83).
#include "c5r/farmfields/footprint.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace c5r { namespace farmfields {

namespace {
const uint32_t kMaxCells = 16384;

// Even-odd test of point (px, py) against the polygon xy (n corners).
bool inside(const float* xy, size_t n, double px, double py)
{
    bool in = false;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const double xi = xy[2 * i], yi = xy[2 * i + 1], xj = xy[2 * j], yj = xy[2 * j + 1];
        if ((yi > py) != (yj > py)) {
            const double x = xj + (py - yj) * (xi - xj) / (yi - yj);
            if (px < x)
                in = !in;
        }
    }
    return in;
}
} // namespace

Status FieldFootprint::build(const FarmLayout& layout, const float worldRect[4])
{
    width_ = height_ = wordsPerRow_ = 0;
    bits_.clear();
    cellsSet_ = 0;
    hash_ = 0;
    for (int i = 0; i < 4; ++i)
        if (!std::isfinite(worldRect[i]))
            return Status::fail(Err::BadLayout, "footprint: the world rectangle is not finite");
    if (worldRect[2] < 0 || worldRect[3] < 0 || worldRect[2] < worldRect[0] || worldRect[3] < worldRect[1])
        return Status::fail(Err::BadLayout, "footprint: the world rectangle is empty");
    const uint32_t w = (static_cast<uint32_t>(worldRect[2]) >> 1) + 1, h = (static_cast<uint32_t>(worldRect[3]) >> 1) + 1;
    if (w > kMaxCells || h > kMaxCells)
        return Status::fail(Err::BadLayout, "footprint: more than 16384 cells on an axis");
    const uint32_t wpr = (w + 63) / 64;
    std::vector<uint64_t> bits(static_cast<size_t>(wpr) * h, 0);
    for (size_t f = 0; f < layout.fields.size(); ++f) {
        const std::vector<float>& c = layout.fields[f].corners;
        const size_t n = c.size() / 2;
        if (n < 3)
            continue;
        float minX = c[0], maxX = c[0], minY = c[1], maxY = c[1];
        for (size_t k = 0; k < n; ++k) {
            if (!std::isfinite(c[2 * k]) || !std::isfinite(c[2 * k + 1]))
                return Status::fail(Err::BadLayout, "footprint: field " + std::to_string(f) + " has a corner that is not finite");
            minX = std::min(minX, c[2 * k]); maxX = std::max(maxX, c[2 * k]);
            minY = std::min(minY, c[2 * k + 1]); maxY = std::max(maxY, c[2 * k + 1]);
        }
        // cells whose sample point (2 c, 2 r) can lie in [min, max]
        const int32_t c0 = std::max<int32_t>(0, static_cast<int32_t>(std::floor(minX / 2.0f)));
        const int32_t c1 = std::min<int32_t>(static_cast<int32_t>(w) - 1, static_cast<int32_t>(std::ceil(maxX / 2.0f)));
        const int32_t r0 = std::max<int32_t>(0, static_cast<int32_t>(std::floor(minY / 2.0f)));
        const int32_t r1 = std::min<int32_t>(static_cast<int32_t>(h) - 1, static_cast<int32_t>(std::ceil(maxY / 2.0f)));
        for (int32_t r = r0; r <= r1; ++r)
            for (int32_t col = c0; col <= c1; ++col)
                if (inside(c.data(), n, 2.0 * col, 2.0 * r))
                    bits[static_cast<size_t>(r) * wpr + (static_cast<uint32_t>(col) >> 6)] |= uint64_t(1) << (col & 63);
    }
    uint64_t hsh = 1469598103934665603ull, set = 0;
    auto mix = [&hsh](uint64_t v) {
        for (int b = 0; b < 8; ++b) {
            hsh ^= (v >> (8 * b)) & 0xFF;
            hsh *= 1099511628211ull;
        }
    };
    mix(w);
    mix(h);
    for (uint64_t v : bits) {
        mix(v);
        uint64_t x = v;
        while (x) { x &= x - 1; ++set; }
    }
    width_ = w;
    height_ = h;
    wordsPerRow_ = wpr;
    bits_.swap(bits);
    cellsSet_ = set;
    hash_ = hsh;
    return Status::Ok();
}

bool FieldFootprint::cell(int32_t col, int32_t row) const
{
    if (col < 0 || row < 0 || static_cast<uint32_t>(col) >= width_ || static_cast<uint32_t>(row) >= height_)
        return false;
    return (bits_[static_cast<size_t>(row) * wordsPerRow_ + (static_cast<uint32_t>(col) >> 6)] >> (col & 63)) & 1;
}

uint8_t FieldFootprint::bitsAt(float worldX, float worldY) const
{
    const forest::CutCell c = forest::cutCell(worldX, worldY, ox_, oy_);
    return cell(c.col, c.row) ? kFieldFootprintBit : 0;
}

}} // namespace c5r::farmfields
