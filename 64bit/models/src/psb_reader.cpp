// psb_reader.cpp - the .psb particle file reader (ticket 4787, epic 0042, 2026-11-23). Own code, GPL-3.0-or-later.
// Behaviour as described in research/05-graphics-composition/effects-formats.md 4 and 16.
#include "c5models/psb.h"

#include <cstring>
#include <new>

namespace c5m {

const char* psb_slot_kind_name(PsbSlotKind k) {
    switch (k) {
    case PsbSlotKind::Texture: return "texture";
    case PsbSlotKind::Shader: return "shader";
    case PsbSlotKind::CustomData: return "custom";
    }
    return "?";
}

size_t PsbEmitter::texture_count() const {
    size_t n = 0;
    for (auto& s : slots)
        if (s.kind == PsbSlotKind::Texture) ++n;
    return n;
}

namespace {

uint32_t u32_at(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

void set_err(Error* e, Err c, const std::string& w) {
    if (e) *e = {c, w};
}

std::string hex(size_t v) {
    static const char* d = "0123456789abcdef";
    std::string s;
    do {
        s.insert(s.begin(), d[v & 15]);
        v >>= 4;
    } while (v);
    return "0x" + s;
}

bool ends_with_ci(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        char c = s[s.size() - n + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != suffix[i]) return false;
    }
    return true;
}

// The text of a 256-byte name field at p if it is printable ASCII (0x20..0x7e) of at least one character and ends
// with a NUL inside the field; false otherwise.
bool field_text(const uint8_t* p, std::string& out) {
    size_t n = 0;
    while (n < kPsbNameSize && p[n] != 0) {
        if (p[n] < 0x20 || p[n] > 0x7e) return false;
        ++n;
    }
    if (n == 0 || n == kPsbNameSize) return false;
    out.assign((const char*)p, n);
    return true;
}

}  // namespace

std::vector<PsbSlot> find_psb_slots(const uint8_t* block, size_t size) {
    std::vector<PsbSlot> out;
    if (!block || size < kPsbBlockFixed) return out;
    size_t o = kPsbBlockFixed;
    std::string text;
    while (o + 4 + kPsbNameSize <= size) {
        uint32_t flag = u32_at(block + o);
        if ((flag == 0 || flag == 1) && field_text(block + o + 4, text)) {
            PsbSlot s;
            bool slot = true;
            if (text == "Custom Data") {
                s.kind = PsbSlotKind::CustomData;
            } else if (ends_with_ci(text, ".dds") || ends_with_ci(text, ".tga")) {
                s.kind = PsbSlotKind::Texture;
                s.bare = basename_lower(text);
            } else if (ends_with_ci(text, ".txt")) {
                s.kind = PsbSlotKind::Shader;
                s.bare = basename_lower(text);
            } else {
                slot = false;
            }
            if (slot) {
                s.offset = (uint32_t)o;
                s.flag = flag;
                s.text = text;
                out.push_back(std::move(s));
                o += 4 + kPsbNameSize;   // the name field is not scanned again
                continue;
            }
        }
        o += 4;
    }
    return out;
}

bool parse_psb(const uint8_t* data, size_t size, PsbFile& out, Error* err) {
    out = PsbFile();
    if (size > kPsbMaxFileSize) {
        set_err(err, Err::ReadBudget, "file of " + std::to_string(size) + " bytes is above the limit");
        return false;
    }
    if (!data || size < kPsbHeaderSize) {
        set_err(err, Err::Truncated, "file of " + std::to_string(size) + " bytes is shorter than the 0x40 header");
        return false;
    }
    if (std::memcmp(data, "PSB\0", 4) != 0) {
        set_err(err, Err::BadMagic, "no PSB magic");
        return false;
    }
    PsbFile f;
    f.version = u32_at(data + 4);
    f.payload_size = u32_at(data + 8);
    f.header_size = u32_at(data + 12);
    f.field_10 = u32_at(data + 16);
    f.header_zero = true;
    for (size_t i = 0x14; i < kPsbHeaderSize; ++i)
        if (data[i]) f.header_zero = false;
    if (f.version != kPsbVersion) {
        set_err(err, Err::BadHeader, "version " + std::to_string(f.version) + ", only 100 is known");
        return false;
    }
    if (f.header_size != kPsbHeaderSize) {
        set_err(err, Err::BadHeader, "header size " + hex(f.header_size) + ", not 0x40");
        return false;
    }
    if ((size_t)f.payload_size != size - kPsbHeaderSize) {
        set_err(err, f.payload_size > size - kPsbHeaderSize ? Err::Truncated : Err::BadHeader,
                "payload size " + std::to_string(f.payload_size) + " but the file holds " +
                    std::to_string(size - kPsbHeaderSize) + " bytes after the header");
        return false;
    }
    if (size == kPsbHeaderSize) {
        set_err(err, Err::BadHeader, "no emitter block");
        return false;
    }
    try {
        size_t p = kPsbHeaderSize;
        while (p < size) {
            if (f.emitters.size() >= kPsbMaxBlocks) {
                set_err(err, Err::ReadBudget, "more than " + std::to_string(kPsbMaxBlocks) + " blocks");
                return false;
            }
            if (size - p < kPsbBlockFixed) {
                set_err(err, Err::Truncated, "block at " + hex(p) + ": " + std::to_string(size - p) +
                                                 " bytes left, its fixed part needs 0x108");
                return false;
            }
            uint32_t bs = u32_at(data + p + 0x100);
            if (bs < kPsbBlockFixed || (bs & 3) != 0) {
                set_err(err, Err::BadHeader, "block at " + hex(p) + ": size " + hex(bs) +
                                                 " (below 0x108 or not a multiple of 4)");
                return false;
            }
            if (bs > size - p) {
                set_err(err, Err::Truncated, "block at " + hex(p) + ": size " + hex(bs) + " runs past the end (" +
                                                 std::to_string(size - p) + " bytes left)");
                return false;
            }
            const uint8_t* b = data + p;
            size_t n = 0;
            while (n < kPsbNameSize && b[n]) ++n;
            if (n == kPsbNameSize) {
                set_err(err, Err::BadHeader, "block at " + hex(p) + ": name without its NUL");
                return false;
            }
            PsbEmitter e;
            e.name.assign((const char*)b, n);
            e.file_offset = (uint32_t)p;
            e.size = bs;
            e.field_104 = u32_at(b + 0x104);
            e.raw.assign(b, b + bs);
            e.slots = find_psb_slots(b, bs);
            f.emitters.push_back(std::move(e));
            p += bs;
        }
    } catch (const std::bad_alloc&) {
        set_err(err, Err::OutOfMemory, "out of memory");
        return false;
    }
    out = std::move(f);
    if (err) *err = Error();
    return true;
}

size_t resolve_psb_textures(PsbFile& f, FileProvider& files) {
    size_t missing = 0;
    for (auto& e : f.emitters)
        for (auto& s : e.slots) {
            if (s.kind != PsbSlotKind::Texture) continue;
            s.origin.clear();
            s.found = !s.bare.empty() && files.exists(s.bare, &s.origin);
            if (!s.found) {
                s.origin.clear();
                ++missing;
            }
        }
    return missing;
}

}  // namespace c5m
