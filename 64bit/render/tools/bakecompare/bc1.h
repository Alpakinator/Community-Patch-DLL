// render/tools/bakecompare/bc1.h - BC1 block layout and the reference decoder the comparator uses (issue 3404).
// The decoder is the one of 3401's cache_decoded_diff.py (terrain-bake-records.md 2.4), so that the server-vs-EXE
// figures and the EXE-vs-EXE ceiling are measured the same way: 5:6:5 expanded by (v * 255 + n / 2) / n, colour0 >
// colour1: four colours at integer thirds (2a + b) / 3, else three colours, (a + b) / 2, and black. Own code.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bakecompare {

// Bytes of a BC1 texture of size x size with `mips` levels (every level at least one 4 x 4 block).
inline size_t bc1Bytes(uint32_t size, uint32_t mips) {
    size_t n = 0;
    for (uint32_t m = 0; m < mips; ++m) {
        uint32_t s = size >> m;
        uint32_t b = s < 4 ? 1 : s / 4;
        n += static_cast<size_t>(b) * b * 8;
    }
    return n;
}
inline size_t bc1MipOffset(uint32_t size, uint32_t mip) { return bc1Bytes(size, mip); }
inline uint32_t bc1MipBlocks(uint32_t size, uint32_t mip) {
    uint32_t s = size >> mip;
    return s < 4 ? 1 : s / 4;
}

inline void bc1Rgb(uint16_t c, int out[3]) {
    const int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    out[0] = (r * 255 + 15) / 31;
    out[1] = (g * 255 + 31) / 63;
    out[2] = (b * 255 + 15) / 31;
}

// Decodes one mip level (blocks x blocks blocks at p) into RGB bytes, row-major, (blocks*4)^2 texels.
inline void bc1DecodeMip(const uint8_t* p, uint32_t blocks, std::vector<uint8_t>& rgb) {
    const uint32_t w = blocks * 4;
    rgb.assign(static_cast<size_t>(w) * w * 3, 0);
    for (uint32_t by = 0; by < blocks; ++by)
        for (uint32_t bx = 0; bx < blocks; ++bx) {
            const uint8_t* b = p + (static_cast<size_t>(by) * blocks + bx) * 8;
            const uint16_t c0 = static_cast<uint16_t>(b[0] | (b[1] << 8)), c1 = static_cast<uint16_t>(b[2] | (b[3] << 8));
            const uint32_t bits = static_cast<uint32_t>(b[4]) | (static_cast<uint32_t>(b[5]) << 8) |
                                  (static_cast<uint32_t>(b[6]) << 16) | (static_cast<uint32_t>(b[7]) << 24);
            int pal[4][3];
            bc1Rgb(c0, pal[0]);
            bc1Rgb(c1, pal[1]);
            for (int k = 0; k < 3; ++k) {
                if (c0 > c1) {
                    pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3;
                    pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3;
                } else {
                    pal[2][k] = (pal[0][k] + pal[1][k]) / 2;
                    pal[3][k] = 0;
                }
            }
            for (uint32_t i = 0; i < 16; ++i) {
                const uint32_t ix = (bits >> (2 * i)) & 3;
                const uint32_t x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                uint8_t* o = &rgb[(static_cast<size_t>(y) * w + x) * 3];
                for (int k = 0; k < 3; ++k) o[k] = static_cast<uint8_t>(pal[ix][k]);
            }
        }
}

}  // namespace bakecompare
