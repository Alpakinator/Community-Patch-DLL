// render/src/gamefs/fpk.cpp - .fpk pack index parser and memory-mapped reader (task 2901). Own code.
#include "c5r/gamefs/fpk.h"
#include <algorithm>
#include <cstring>
#include <windows.h>
#include "c5r/binreader.h"

namespace c5r { namespace gamefs {

std::string lowerAscii(const std::string& s) {
    std::string r(s);
    for (char& c : r) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return r;
}

Status parseFpkIndex(const uint8_t* data, size_t size, std::vector<FpkEntry>& out) {
    out.clear();
    if (!data || size < kFpkHeaderSize)
        return Status::fail(Err::Truncated, "fpk: header needs 14 bytes, have " + std::to_string(size));
    BinReader r(data, size);
    uint32_t version = r.u32();
    char magic[4];
    r.raw(magic, 4);
    uint16_t zero = r.u16();
    uint32_t count = r.u32();
    if (std::memcmp(magic, "FPK_", 4) != 0) return Status::fail(Err::BadMagic, "fpk: magic is not FPK_");
    if (version != kFpkVersion)
        return Status::fail(Err::BadVersion, "fpk: version " + std::to_string(version) + ", expected 6");
    if (zero != 0) return Status::fail(Err::BadLayout, "fpk: u16 after the magic is " + std::to_string(zero));
    // Smallest possible entry: u32 nameLen + 1 name byte + 3 pad + 16 bytes of fields = 24; use 20 as the floor.
    if (static_cast<uint64_t>(count) * 20u > r.remaining())
        return Status::fail(Err::BadLayout, "fpk: count " + std::to_string(count) + " cannot fit in " +
                                                std::to_string(r.remaining()) + " bytes");
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        C5R_TRY(r.need(4, "fpk: entry name length"));
        uint32_t n = r.u32();
        if (n == 0 || n > kFpkMaxNameLen)
            return Status::fail(Err::BadLayout, "fpk: entry " + std::to_string(i) + " name length " + std::to_string(n));
        size_t padded = n + ((4 - n % 4) % 4);
        C5R_TRY(r.need(padded + 16, "fpk: entry name and fields"));
        FpkEntry e;
        e.name = r.str(n);
        r.skip(padded - n);
        for (char c : e.name) {
            unsigned char u = static_cast<unsigned char>(c);
            if (u < 0x20 || u > 0x7e || c == '/' || c == '\\')
                return Status::fail(Err::BadLayout, "fpk: entry " + std::to_string(i) + " name has a bad character");
        }
        e.a = r.u32();
        e.b = r.u32();
        e.size = r.u32();
        e.offset = r.u32();
        out.push_back(std::move(e));
    }
    const uint64_t indexEnd = r.pos();
    std::vector<std::pair<uint64_t, uint64_t>> ranges;
    ranges.reserve(out.size());
    for (size_t i = 0; i < out.size(); ++i) {
        const FpkEntry& e = out[i];
        uint64_t end = static_cast<uint64_t>(e.offset) + e.size;
        if (end > size)
            return Status::fail(Err::BadLayout, "fpk: payload of '" + e.name + "' ends at " + std::to_string(end) +
                                                    " past the file end " + std::to_string(size));
        if (e.size && e.offset < indexEnd)
            return Status::fail(Err::BadLayout, "fpk: payload of '" + e.name + "' starts inside the index");
        if (e.size) ranges.emplace_back(e.offset, end);
    }
    std::sort(ranges.begin(), ranges.end());
    for (size_t i = 1; i < ranges.size(); ++i) {
        if (ranges[i].first < ranges[i - 1].second)
            return Status::fail(Err::BadLayout, "fpk: payloads overlap at offset " + std::to_string(ranges[i].first));
    }
    std::vector<std::string> names;
    names.reserve(out.size());
    for (const FpkEntry& e : out) names.push_back(lowerAscii(e.name));
    std::sort(names.begin(), names.end());
    for (size_t i = 1; i < names.size(); ++i) {
        if (names[i] == names[i - 1]) return Status::fail(Err::BadLayout, "fpk: duplicate name '" + names[i] + "'");
    }
    return Status::Ok();
}

FpkArchive::FpkArchive() {}
FpkArchive::~FpkArchive() { close(); }

FpkArchive::FpkArchive(FpkArchive&& o) noexcept { *this = std::move(o); }

FpkArchive& FpkArchive::operator=(FpkArchive&& o) noexcept {
    if (this != &o) {
        close();
        path_ = std::move(o.path_);
        file_ = o.file_;
        mapping_ = o.mapping_;
        base_ = o.base_;
        size_ = o.size_;
        entries_ = std::move(o.entries_);
        sorted_ = std::move(o.sorted_);
        o.file_ = o.mapping_ = nullptr;
        o.base_ = nullptr;
        o.size_ = 0;
    }
    return *this;
}

void FpkArchive::close() {
    if (base_) UnmapViewOfFile(base_);
    if (mapping_) CloseHandle(mapping_);
    if (file_ && file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
    base_ = nullptr;
    mapping_ = nullptr;
    file_ = nullptr;
    size_ = 0;
    entries_.clear();
    sorted_.clear();
}

static std::string narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s.push_back(c < 128 ? static_cast<char>(c) : '?');
    return s;
}

Status FpkArchive::open(const std::wstring& path) {
    close();
    path_ = path;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return Status::fail(Err::Io, "fpk: cannot open " + narrow(path) + " (error " + std::to_string(GetLastError()) + ")");
    file_ = f;
    LARGE_INTEGER li;
    if (!GetFileSizeEx(f, &li)) {
        close();
        return Status::fail(Err::Io, "fpk: cannot size " + narrow(path));
    }
    size_ = static_cast<uint64_t>(li.QuadPart);
    if (size_ < kFpkHeaderSize) {
        close();
        return Status::fail(Err::Truncated, "fpk: " + narrow(path) + " is " + std::to_string(li.QuadPart) + " bytes");
    }
    mapping_ = CreateFileMappingW(f, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping_) {
        close();
        return Status::fail(Err::Io, "fpk: cannot map " + narrow(path));
    }
    base_ = static_cast<const uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0));
    if (!base_) {
        close();
        return Status::fail(Err::Io, "fpk: cannot map a view of " + narrow(path));
    }
    std::vector<FpkEntry> ents;
    Status st = parseFpkIndex(base_, static_cast<size_t>(size_), ents);
    if (!st.ok()) {
        std::wstring keep = path_;
        close();
        path_ = keep;
        st.msg = narrow(path) + ": " + st.msg;
        return st;
    }
    entries_ = std::move(ents);
    sorted_.reserve(entries_.size());
    for (size_t i = 0; i < entries_.size(); ++i) sorted_.emplace_back(lowerAscii(entries_[i].name), static_cast<int>(i));
    std::sort(sorted_.begin(), sorted_.end());
    return Status::Ok();
}

const uint8_t* FpkArchive::view(size_t index, size_t* size) const {
    if (!base_ || index >= entries_.size()) {
        if (size) *size = 0;
        return nullptr;
    }
    if (size) *size = entries_[index].size;
    return base_ + entries_[index].offset;
}

Status FpkArchive::read(size_t index, std::vector<uint8_t>& out) const {
    out.clear();
    if (!base_) return Status::fail(Err::Io, "fpk: archive not open");
    if (index >= entries_.size())
        return Status::fail(Err::NotFound, "fpk: entry index " + std::to_string(index) + " out of range");
    const FpkEntry& e = entries_[index];
    out.assign(base_ + e.offset, base_ + e.offset + e.size);
    return Status::Ok();
}

int FpkArchive::find(const std::string& name) const {
    std::string key = lowerAscii(name);
    auto it = std::lower_bound(sorted_.begin(), sorted_.end(), std::make_pair(key, -1));
    if (it != sorted_.end() && it->first == key) return it->second;
    return -1;
}

}} // namespace c5r::gamefs
