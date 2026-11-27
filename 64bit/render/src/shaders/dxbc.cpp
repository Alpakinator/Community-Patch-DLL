// DXBC container parser and checksum (task 2902). See c5r/shaders/dxbc.h for the layout, the checksum and the checks.
#include "c5r/shaders/dxbc.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "c5r/binreader.h"

namespace c5r { namespace shaders {

const char* stageName(Stage s) {
    switch (s) {
    case Stage::Pixel: return "ps";
    case Stage::Vertex: return "vs";
    case Stage::Geometry: return "gs";
    case Stage::Hull: return "hs";
    case Stage::Domain: return "ds";
    case Stage::Compute: return "cs";
    default: return "?";
    }
}

std::string fourccStr(uint32_t f) {
    std::string s;
    for (int i = 0; i < 4; ++i) {
        const char c = static_cast<char>((f >> (8 * i)) & 0xFF);
        s += (c >= 32 && c < 127) ? c : '?';
    }
    return s;
}

const DxbcChunk* DxbcContainer::find(uint32_t f) const {
    for (const DxbcChunk& c : chunks)
        if (c.fourcc == f) return &c;
    return nullptr;
}

std::string DxbcContainer::chunkList() const {
    std::string s;
    for (const DxbcChunk& c : chunks) s += (s.empty() ? "" : "+") + c.tag();
    return s;
}

std::string DxbcContainer::model() const {
    if (programIndex < 0) return "?";
    return std::string(stageName(stage)) + "_" + std::to_string(major) + "_" + std::to_string(minor);
}

// ---- checksum: MD5 transform (RFC 1321) with the DXBC final block ----
namespace {
inline uint32_t rotl(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

void md5Block(uint32_t st[4], const uint8_t blk[64]) {
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,
                              14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                              4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t m[16];
    for (int i = 0; i < 16; ++i)
        m[i] = static_cast<uint32_t>(blk[4 * i]) | (static_cast<uint32_t>(blk[4 * i + 1]) << 8) |
               (static_cast<uint32_t>(blk[4 * i + 2]) << 16) | (static_cast<uint32_t>(blk[4 * i + 3]) << 24);
    uint32_t a = st[0], b = st[1], c = st[2], d = st[3];
    for (int i = 0; i < 64; ++i) {
        uint32_t f;
        int g;
        if (i < 16) { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) & 15; }
        else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) & 15; }
        else { f = c ^ (b | ~d); g = (7 * i) & 15; }
        const uint32_t t = d;
        d = c;
        c = b;
        b = b + rotl(a + f + K[i] + m[g], R[i]);
        a = t;
    }
    st[0] += a; st[1] += b; st[2] += c; st[3] += d;
}

void putU32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v); p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16); p[3] = static_cast<uint8_t>(v >> 24);
}
} // namespace

void dxbcChecksum(const uint8_t* container, size_t size, uint8_t out[16]) {
    uint32_t st[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    const uint8_t* d = container + 20;
    const size_t n = size >= 20 ? size - 20 : 0;
    const uint32_t bits = static_cast<uint32_t>(n * 8);
    const size_t full = n & ~static_cast<size_t>(63);
    for (size_t i = 0; i < full; i += 64) md5Block(st, d + i);
    const size_t last = n - full;
    uint8_t blk[64];
    if (last >= 56) {
        std::memset(blk, 0, 64);
        std::memcpy(blk, d + full, last);
        blk[last] = 0x80;
        md5Block(st, blk);
        std::memset(blk, 0, 64);
        putU32(blk, bits);
        putU32(blk + 60, (bits >> 2) | 1);
        md5Block(st, blk);
    } else {
        std::memset(blk, 0, 64);
        putU32(blk, bits);
        std::memcpy(blk + 4, d + full, last);
        blk[4 + last] = 0x80;
        putU32(blk + 60, (bits >> 2) | 1);
        md5Block(st, blk);
    }
    for (int i = 0; i < 4; ++i) putU32(out + 4 * i, st[i]);
}

void dxbcFixChecksum(uint8_t* container, size_t size) {
    uint8_t c[16];
    dxbcChecksum(container, size, c);
    std::memcpy(container + 4, c, 16);
}

Status parseDxbc(const uint8_t* p, size_t avail, DxbcContainer& out, bool verifyChecksum) {
    out = DxbcContainer();
    BinReader r(p, avail);
    C5R_TRY(r.need(32, "DXBC header"));
    if (r.u32() != fourcc('D', 'X', 'B', 'C')) return Status::fail(Err::BadMagic, "not a DXBC container");
    r.raw(out.checksum, 16);
    out.verMajor = r.u16();
    out.verMinor = r.u16();
    out.totalSize = r.u32();
    const uint32_t count = r.u32();
    if (out.verMajor != 1 || out.verMinor != 0)
        return Status::fail(Err::BadVersion, "container version " + std::to_string(out.verMajor) + "." +
                                                 std::to_string(out.verMinor) + " (want 1.0)");
    if (out.totalSize > avail)
        return Status::fail(Err::Truncated, "total size " + std::to_string(out.totalSize) + " > " +
                                                std::to_string(avail) + " bytes available");
    if (count > 4096 || out.totalSize < 32 + 4ull * count)
        return Status::fail(Err::BadLayout, "chunk count " + std::to_string(count) + " does not fit total size " +
                                                std::to_string(out.totalSize));
    const uint32_t hdrEnd = 32 + 4 * count;
    struct Span { uint32_t a, b; };
    std::vector<Span> spans;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t off = r.u32();
        if ((off & 3) || off < hdrEnd || static_cast<uint64_t>(off) + 8 > out.totalSize)
            return Status::fail(Err::BadLayout, "chunk " + std::to_string(i) + " offset " + std::to_string(off) +
                                                    " unaligned, inside the header or past the total size");
        DxbcChunk c;
        c.offset = off;
        std::memcpy(&c.fourcc, p + off, 4);
        std::memcpy(&c.size, p + off + 4, 4);
        if (static_cast<uint64_t>(off) + 8 + c.size > out.totalSize)
            return Status::fail(Err::BadLayout, "chunk " + c.tag() + " (" + std::to_string(c.size) + " B at " +
                                                    std::to_string(off) + ") past the total size");
        c.data = p + off + 8;
        out.chunks.push_back(c);
        spans.push_back({off, off + 8 + c.size});
    }
    std::sort(spans.begin(), spans.end(), [](const Span& x, const Span& y) { return x.a < y.a; });
    for (size_t i = 1; i < spans.size(); ++i)
        if (spans[i].a < spans[i - 1].b) return Status::fail(Err::BadLayout, "chunks overlap");
    if (verifyChecksum) {
        uint8_t c[16];
        dxbcChecksum(p, out.totalSize, c);
        if (std::memcmp(c, out.checksum, 16) != 0) return Status::fail(Err::BadLayout, "checksum mismatch");
    }
    for (size_t i = 0; i < out.chunks.size(); ++i) {
        const uint32_t f = out.chunks[i].fourcc;
        if (f == fourcc('S', 'H', 'E', 'X') || f == fourcc('S', 'H', 'D', 'R')) {
            out.programIndex = static_cast<int>(i);
            break;
        }
    }
    if (const DxbcChunk* pc = out.program()) {
        const DxbcChunk& c = *pc;
        if (c.size < 8) return Status::fail(Err::BadLayout, c.tag() + " shorter than its 8-byte header");
        uint32_t ver, len;
        std::memcpy(&ver, c.data, 4);
        std::memcpy(&len, c.data + 4, 4);
        const uint32_t type = ver >> 16;
        if (type > 5) return Status::fail(Err::BadLayout, c.tag() + " program type " + std::to_string(type));
        if (static_cast<uint64_t>(len) * 4 != c.size)
            return Status::fail(Err::BadLayout, c.tag() + " length token " + std::to_string(len) + " DWORDs, chunk " +
                                                    std::to_string(c.size) + " B");
        out.stage = static_cast<Stage>(type);
        out.major = static_cast<uint8_t>((ver >> 4) & 0xF);
        out.minor = static_cast<uint8_t>(ver & 0xF);
    }
    return Status::Ok();
}

}} // namespace c5r::shaders
