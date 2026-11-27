// Helpers for the shaders tests (task 2902): compile HLSL written in the test with the SDK's D3DCompile, build DXBC
// containers and PE32 files by hand. No game bytes.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "c5r/shaders/dxbc.h"

namespace c5r { namespace test {

// D3DCompile(src, entry, target); empty on failure (the errors go to stderr).
std::vector<uint8_t> compileHlsl(const char* src, const char* entry, const char* target);
// D3DStripShader with reflection data, debug info and test blobs removed: ISGN/OSGN/(PCSG)/SHDR|SHEX like the EXE's.
std::vector<uint8_t> stripLikeExe(const std::vector<uint8_t>& blob);

inline void put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
inline void set32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
    for (int i = 0; i < 4; ++i) v[at + i] = static_cast<uint8_t>(x >> (8 * i));
}
inline uint32_t get32(const std::vector<uint8_t>& v, size_t at) {
    uint32_t x;
    std::memcpy(&x, v.data() + at, 4);
    return x;
}

// A DXBC container from chunks (FourCC, data), with a valid checksum.
std::vector<uint8_t> makeContainer(const std::vector<std::pair<uint32_t, std::vector<uint8_t>>>& chunks);
// A program chunk body: version token for (type, major, minor), length token, then the given tokens.
std::vector<uint8_t> makeProgram(uint32_t type, uint32_t major, uint32_t minor, const std::vector<uint32_t>& tokens);
// Offset of the data of the first chunk with this FourCC in a container (0 if none).
size_t chunkDataOffset(const std::vector<uint8_t>& c, uint32_t fourcc);

struct SynthSection {
    std::string name;
    uint32_t rva;
    uint32_t virtualSize;
    std::vector<uint8_t> data; // raw bytes (rounded up to 0x200 in the file)
};
// A PE32 (i386) file: DOS header, PE header, 224-byte optional header, the sections at 0x400 onwards.
// sizeOfImage: written to the optional header (ticket 4966: with the stamp it names an EXE build; 0 = none).
std::vector<uint8_t> buildPe(const std::vector<SynthSection>& secs, uint32_t imageBase = 0x400000, uint32_t timeDateStamp = 0x12345678,
                             uint32_t sizeOfImage = 0);

}} // namespace c5r::test
