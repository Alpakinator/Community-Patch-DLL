// c5r/gamefs/fpk.h - Firaxis .fpk pack reader (task 2901, epic 2900).
//
// Layout (verified on all 147 packs of the install, 2026-11-10; research/05-graphics-composition/asset-pipeline.md 1.1):
//   u32 version (6) | char[4] "FPK_" | u16 0 | u32 count |
//   count x { u32 nameLen | char name[nameLen] | pad to a multiple of 4 of nameLen (pad bytes are garbage) |
//             u32 a (0, or a file time) | u32 b (0) | u32 size | u32 offset }
//   payloads stored raw (uncompressed) at their offsets.
// Every check is a Status; nothing here crashes or throws on bad input.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "c5r/status.h"

namespace c5r { namespace gamefs {

struct FpkEntry {
    std::string name;      // as stored (the game's packs store bare lower-case file names)
    uint32_t offset = 0;   // payload offset from the start of the pack
    uint32_t size = 0;     // payload size in bytes
    uint32_t a = 0;        // 0 in 30,442 of 30,557 entries; otherwise a Unix time (seconds) [I]
    uint32_t b = 0;        // always 0 in the install
};

// Limits the parser enforces (BadLayout beyond them).
const uint32_t kFpkVersion = 6;
const uint32_t kFpkMaxNameLen = 260;      // longest name in the install: 74
const size_t kFpkHeaderSize = 14;         // version, magic, u16, count

// Parses the index of a whole pack held in memory (data, size = the complete file). Checks, in order:
//   size < 14                              -> Truncated
//   magic != "FPK_"                        -> BadMagic
//   version != 6                           -> BadVersion
//   u16 after the magic != 0               -> BadLayout
//   count > what the remaining bytes could hold (20 bytes per entry minimum) -> BadLayout
//   an entry header or name runs past the end -> Truncated
//   nameLen == 0 or > 260, or a name byte outside 0x20..0x7e, or '/' or '\\' in a name -> BadLayout
//   payload offset + size > file size      -> BadLayout
//   payload starting inside the index      -> BadLayout
//   two payloads overlapping (zero-size entries excepted) -> BadLayout
//     (decision: the install has 0 overlaps and 0 shared ranges among 30,557 entries, so any overlap is treated as
//      a damaged pack rather than as aliasing)
//   the same name twice (case-insensitive)  -> BadLayout (0 in the install)
Status parseFpkIndex(const uint8_t* data, size_t size, std::vector<FpkEntry>& out);

// A pack opened read-only and memory-mapped (CreateFileMapping, PAGE_READONLY): open() maps the whole file and
// parses the index; read() copies one payload. Address space only, nothing is committed until read. Move-only.
class FpkArchive {
public:
    FpkArchive();
    ~FpkArchive();
    FpkArchive(FpkArchive&& o) noexcept;
    FpkArchive& operator=(FpkArchive&& o) noexcept;
    FpkArchive(const FpkArchive&) = delete;
    FpkArchive& operator=(const FpkArchive&) = delete;

    Status open(const std::wstring& path);
    void close();
    bool isOpen() const { return base_ != nullptr; }
    const std::wstring& path() const { return path_; }
    uint64_t fileSize() const { return size_; }
    const std::vector<FpkEntry>& entries() const { return entries_; }
    // Copies payload `index` into out. NotFound for a bad index.
    Status read(size_t index, std::vector<uint8_t>& out) const;
    // Zero-copy view of payload `index` (valid while the archive is open); nullptr for a bad index.
    const uint8_t* view(size_t index, size_t* size) const;
    // Index of the entry with this name, case-insensitive; -1 if absent.
    int find(const std::string& name) const;

private:
    std::wstring path_;
    void* file_ = nullptr;     // HANDLE
    void* mapping_ = nullptr;  // HANDLE
    const uint8_t* base_ = nullptr;
    uint64_t size_ = 0;
    std::vector<FpkEntry> entries_;
    std::vector<std::pair<std::string, int>> sorted_;  // lower-case name -> index, sorted, for find()
};

// Lower-case ASCII copy (the game resolves names case-insensitively).
std::string lowerAscii(const std::string& s);

}} // namespace c5r::gamefs
