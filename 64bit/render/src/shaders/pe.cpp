// Minimal PE32 reader (task 2902). See c5r/shaders/pe.h for the checks and their error codes.
#include "c5r/shaders/pe.h"

#include <windows.h>

#include <cstdio>

#include "c5r/binreader.h"

namespace c5r { namespace shaders {

namespace {
std::string hex(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof b, "0x%llx", static_cast<unsigned long long>(v));
    return b;
}
} // namespace

uint32_t PeSection::fileBackedSize() const {
    if (virtualSize == 0) return rawSize;
    return virtualSize < rawSize ? virtualSize : rawSize;
}

Status PeImage::parse(const uint8_t* data, size_t size) {
    sections_.clear();
    BinReader r(data, size);
    C5R_TRY(r.need(64, "DOS header"));
    if (r.u16() != 0x5A4D) return Status::fail(Err::BadMagic, "DOS header: e_magic is not MZ");
    r.seek(0x3C);
    const uint32_t lfanew = r.u32();
    if (static_cast<uint64_t>(lfanew) + 24 > size)
        return Status::fail(Err::Truncated, "PE header at e_lfanew " + hex(lfanew) + " past the end (" + hex(size) + ")");
    r.seek(lfanew);
    if (r.u32() != 0x00004550) return Status::fail(Err::BadMagic, "PE signature is not PE\\0\\0 at " + hex(lfanew));
    machine_ = r.u16();
    const uint16_t nsec = r.u16();
    timeDateStamp_ = r.u32();
    r.skip(8); // PointerToSymbolTable, NumberOfSymbols
    const uint16_t optSize = r.u16();
    r.skip(2); // Characteristics
    if (machine_ != 0x14C) return Status::fail(Err::Unsupported, "Machine " + hex(machine_) + " is not i386 (0x14c)");
    if (optSize < 96) return Status::fail(Err::BadLayout, "SizeOfOptionalHeader " + std::to_string(optSize) + " < 96");
    const size_t optAt = r.pos();
    if (static_cast<uint64_t>(optAt) + optSize > size)
        return Status::fail(Err::Truncated, "optional header (" + std::to_string(optSize) + " B at " + hex(optAt) +
                                                ") past the end");
    const uint16_t optMagic = r.u16();
    if (optMagic != 0x10B)
        return Status::fail(Err::BadVersion, "optional header magic " + hex(optMagic) + " is not PE32 (0x10b)");
    r.seek(optAt + 28);
    imageBase_ = r.u32();
    r.seek(optAt + 56);   // ticket 4966: SizeOfImage, half of the EXE build's key (stamp + SizeOfImage)
    sizeOfImage_ = r.u32();
    if (nsec == 0 || nsec > 96)
        return Status::fail(Err::BadLayout, "NumberOfSections " + std::to_string(nsec) + " outside 1..96");
    const size_t secAt = optAt + optSize;
    if (static_cast<uint64_t>(secAt) + 40ull * nsec > size)
        return Status::fail(Err::Truncated, "section table (" + std::to_string(nsec) + " entries at " + hex(secAt) +
                                                ") past the end");
    r.seek(secAt);
    for (uint16_t i = 0; i < nsec; ++i) {
        PeSection s;
        std::string n = r.str(8);
        size_t z = n.find('\0');
        s.name = z == std::string::npos ? n : n.substr(0, z);
        s.virtualSize = r.u32();
        s.rva = r.u32();
        s.rawSize = r.u32();
        s.rawOffset = r.u32();
        r.skip(12); // relocations, line numbers, counts
        s.characteristics = r.u32();
        if (s.rawSize && static_cast<uint64_t>(s.rawOffset) + s.rawSize > size)
            return Status::fail(Err::Truncated, "section " + s.name + ": raw data " + hex(s.rawOffset) + "+" +
                                                    hex(s.rawSize) + " past the end of the file (" + hex(size) + ")");
        sections_.push_back(s);
    }
    if (!r.ok()) return Status::fail(Err::Truncated, "PE headers"); // not reachable after the checks above
    return Status::Ok();
}

const PeSection* PeImage::find(const std::string& name) const {
    for (const PeSection& s : sections_)
        if (s.name == name) return &s;
    return nullptr;
}

Status PeImage::vaToOffset(uint32_t va, uint32_t& fileOffset) const {
    if (va >= imageBase_) {
        const uint32_t rva = va - imageBase_;
        for (const PeSection& s : sections_) {
            if (rva >= s.rva && rva - s.rva < s.fileBackedSize()) {
                fileOffset = s.rawOffset + (rva - s.rva);
                return Status::Ok();
            }
        }
    }
    return Status::fail(Err::NotFound, "VA " + hex(va) + " is not in the file-backed part of any section");
}

Status PeImage::offsetToVa(uint32_t fileOffset, uint32_t& va) const {
    for (const PeSection& s : sections_) {
        if (fileOffset >= s.rawOffset && fileOffset - s.rawOffset < s.fileBackedSize()) {
            va = imageBase_ + s.rva + (fileOffset - s.rawOffset);
            return Status::Ok();
        }
    }
    return Status::fail(Err::NotFound, "file offset " + hex(fileOffset) + " is not in any section");
}

Status readFile(const std::wstring& path, std::vector<uint8_t>& out) {
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return Status::fail(Err::Io, "cannot open file (error " + std::to_string(GetLastError()) + ")");
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > 0x40000000) {
        CloseHandle(h);
        return Status::fail(Err::Io, "cannot size file or larger than 1 GB");
    }
    out.resize(static_cast<size_t>(sz.QuadPart));
    size_t done = 0;
    while (done < out.size()) {
        DWORD got = 0;
        const DWORD want = static_cast<DWORD>(out.size() - done > 0x1000000 ? 0x1000000 : out.size() - done);
        if (!ReadFile(h, out.data() + done, want, &got, nullptr) || got == 0) {
            CloseHandle(h);
            out.clear();
            return Status::fail(Err::Io, "read failed at " + std::to_string(done));
        }
        done += got;
    }
    CloseHandle(h);
    return Status::Ok();
}

}} // namespace c5r::shaders
