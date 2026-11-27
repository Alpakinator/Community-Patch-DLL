// c5r/shaders/pe.h - minimal PE32 reader (task 2902, epic 2900): DOS and PE headers, the section table, and VA <->
// file offset. Enough to find CivilizationV_DX11.exe's `.shader` section; it does not load or relocate anything.
//
// Checks, in order (each has a negative test in render/tests/shaders/pe_test.cpp):
//   file shorter than the DOS header (64 B)                      Truncated
//   e_magic != "MZ"                                               BadMagic
//   e_lfanew + PE signature + file header past the end            Truncated
//   signature != "PE\0\0"                                         BadMagic
//   Machine != 0x14c (i386)                                       Unsupported
//   SizeOfOptionalHeader < 96 (the PE32 fields used here)          BadLayout
//   optional header past the end                                  Truncated
//   optional header magic != 0x10b (PE32; 0x20b PE32+ included)   BadVersion
//   NumberOfSections == 0 or > 96                                  BadLayout
//   section table past the end                                    Truncated
//   a section's raw data past the end of the file                 Truncated
// vaToOffset / offsetToVa fail with NotFound for an address no section's file-backed bytes contain.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "c5r/status.h"

namespace c5r { namespace shaders {

struct PeSection {
    std::string name;        // up to 8 characters, NULs stripped
    uint32_t rva = 0;        // VirtualAddress (relative to the image base)
    uint32_t virtualSize = 0;
    uint32_t rawOffset = 0;  // PointerToRawData
    uint32_t rawSize = 0;    // SizeOfRawData
    uint32_t characteristics = 0;
    // Bytes of the section that exist in the file: min(virtualSize, rawSize), or rawSize when virtualSize is 0.
    uint32_t fileBackedSize() const;
};

class PeImage {
public:
    // Parses the headers of a whole PE file held in memory (data must stay alive while offsets are used).
    Status parse(const uint8_t* data, size_t size);
    uint32_t imageBase() const { return imageBase_; }
    uint32_t timeDateStamp() const { return timeDateStamp_; }
    uint32_t sizeOfImage() const { return sizeOfImage_; }   // with the stamp, the EXE build's key (exebuild/include/vp_exe_build.h)
    uint16_t machine() const { return machine_; }
    const std::vector<PeSection>& sections() const { return sections_; }
    const PeSection* find(const std::string& name) const;
    // Virtual address (image base included) <-> file offset, through the file-backed part of a section.
    Status vaToOffset(uint32_t va, uint32_t& fileOffset) const;
    Status offsetToVa(uint32_t fileOffset, uint32_t& va) const;

private:
    uint32_t imageBase_ = 0;
    uint32_t timeDateStamp_ = 0;
    uint32_t sizeOfImage_ = 0;
    uint16_t machine_ = 0;
    std::vector<PeSection> sections_;
};

// Reads a whole file (opened read-only, shared). Io when it cannot be opened or read.
Status readFile(const std::wstring& path, std::vector<uint8_t>& out);

}} // namespace c5r::shaders
