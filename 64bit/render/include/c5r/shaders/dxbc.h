// c5r/shaders/dxbc.h - DXBC container parser (task 2902, epic 2900).
//
// Container layout: "DXBC", a 16-byte checksum, version (u16 major = 1, u16 minor = 0), total size, chunk count,
// chunk offsets[count]; each chunk is a FourCC, a size and the data. The program chunk (SHDR for SM4, SHEX for SM5)
// starts with the version token (bits 0-3 minor, 4-7 major, 16-31 program type) and the length in DWORDs.
//
// Checksum: the DXBC checksum is MD5 with a modified final block, computed over bytes [20, totalSize). Implemented
// here from the public description of the algorithm that open-source DXBC container writers publish (no code copied;
// the MD5 transform is written from RFC 1321): the full 64-byte blocks go through the ordinary MD5 transform; the last
// L = n mod 64 bytes are finished differently from RFC 1321:
//   L <  56: one block  = [u32 bits][L data bytes][0x80][zeros...][u32 (bits >> 2) | 1 in the last DWORD]
//   L >= 56: two blocks = [L data bytes][0x80][zeros]   then   [u32 bits][zeros...][u32 (bits >> 2) | 1]
// where bits = 8 n. The 16 bytes are the four MD5 state words, little endian. Verified against every blob in the
// EXE and against containers the official d3dcompiler writes for every residue n mod 64 (tests/shaders/dxbc_test).
//
// Checks of parseDxbc, in order (negative tests in tests/shaders/dxbc_test.cpp):
//   fewer than 32 bytes available                                 Truncated
//   magic != "DXBC"                                               BadMagic
//   version != 1.0                                                BadVersion
//   total size > bytes available                                  Truncated
//   total size < 32 + 4 * chunk count, or count > 4096            BadLayout
//   chunk offset not 4-aligned, inside the header, or its 8-byte chunk header past the total    BadLayout
//   chunk data past the total size, or two chunks overlapping     BadLayout
//   checksum mismatch (when verifying)                            BadLayout
//   program chunk shorter than 8 bytes, program type > 5, or its length token != chunk size / 4   BadLayout
// So a corrupted byte fails with BadMagic in bytes 0-3, and with BadVersion, Truncated or BadLayout elsewhere: a byte
// inside the chunk data always gives BadLayout (the checksum).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "c5r/status.h"

namespace c5r { namespace shaders {

// Program type of the version token (D3D10_SB_TOKENIZED_PROGRAM_TYPE).
enum class Stage : uint8_t { Pixel = 0, Vertex = 1, Geometry = 2, Hull = 3, Domain = 4, Compute = 5, Unknown = 255 };
const char* stageName(Stage s); // "ps", "vs", "gs", "hs", "ds", "cs", "?"

constexpr uint32_t fourcc(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}
std::string fourccStr(uint32_t f);

struct DxbcChunk {
    uint32_t fourcc = 0;
    uint32_t offset = 0;      // of the chunk header within the container
    uint32_t size = 0;        // of the data
    const uint8_t* data = nullptr;
    std::string tag() const { return fourccStr(fourcc); }
};

struct DxbcContainer {
    uint8_t checksum[16] = {};
    uint16_t verMajor = 0, verMinor = 0;
    uint32_t totalSize = 0;
    std::vector<DxbcChunk> chunks;  // in header order
    int programIndex = -1;          // index of the SHEX or SHDR chunk, -1 if absent
    const DxbcChunk* program() const { return programIndex < 0 ? nullptr : &chunks[static_cast<size_t>(programIndex)]; }
    Stage stage = Stage::Unknown;   // from the program's version token
    uint8_t major = 0, minor = 0;
    const DxbcChunk* find(uint32_t fourcc) const;
    std::string chunkList() const;  // "ISGN+OSGN+SHEX"
    std::string model() const;      // "vs_5_0", "?" without a program chunk
};

// Parses the container at p (up to avail bytes; the container's own total size may be smaller). The chunk pointers
// point into p.
Status parseDxbc(const uint8_t* p, size_t avail, DxbcContainer& out, bool verifyChecksum = true);

// The DXBC checksum of a container of `size` bytes (computed over bytes [20, size)); size must be >= 20.
void dxbcChecksum(const uint8_t* container, size_t size, uint8_t out[16]);
// Rewrites bytes 4..19 of a container with its checksum (for tests that build or patch containers).
void dxbcFixChecksum(uint8_t* container, size_t size);

}} // namespace c5r::shaders
