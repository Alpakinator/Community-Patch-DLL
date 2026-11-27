// gr2_writer.h - a tiny Granny file writer for the tests (epic 3100, moved out of test_gr2.cpp by issue 9025 so that
// test_hostile.cpp builds its crafted files with the same code). Uncompressed sections, fix-ups, no marshalling.
#pragma once
#include "c5models/gr2.h"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace gr2w {

using c5m::Bytes;

struct Sec {
    Bytes b;
    struct Fix { uint32_t src, ts, to; };
    std::vector<Fix> fix;
    uint32_t put32(uint32_t v) { uint32_t o = (uint32_t)b.size(); b.resize(o + 4); std::memcpy(&b[o], &v, 4); return o; }
    void set32(uint32_t at, uint32_t v) { std::memcpy(&b[at], &v, 4); }
    uint32_t putf(float f) { uint32_t v; std::memcpy(&v, &f, 4); return put32(v); }
    uint32_t put16(uint16_t v) { uint32_t o = (uint32_t)b.size(); b.resize(o + 2); std::memcpy(&b[o], &v, 2); return o; }
    void align() { while (b.size() % 4) b.push_back(0); }
    uint32_t cstr(const std::string& s) { uint32_t o = (uint32_t)b.size(); b.insert(b.end(), s.begin(), s.end()); b.push_back(0); align(); return o; }
    void ptr(uint32_t at, uint32_t ts, uint32_t to) { fix.push_back(Fix{at, ts, to}); }
};

const uint32_t S_OBJ = 0, S_TYPE = 1;

struct TypeWriter {
    Sec& t;
    uint32_t start;
    explicit TypeWriter(Sec& s) : t(s), start((uint32_t)s.b.size()) {}
    // ref_off = offset in the type section of the referenced type (or ~0u for none)
    void member(uint32_t type, const char* name, uint32_t ref_off = ~0u, uint32_t arr = 0) {
        uint32_t e = (uint32_t)t.b.size();
        t.b.resize(e + 32, 0);
        t.set32(e, type);
        t.set32(e + 12, arr);
        names.push_back(std::make_pair(e + 4, std::string(name)));
        if (ref_off != ~0u) t.ptr(e + 8, S_TYPE, ref_off);
    }
    uint32_t end() { t.b.resize(t.b.size() + 32, 0); return start; }
    std::vector<std::pair<uint32_t, std::string>> names;
};

inline void finish_names(Sec& t, std::vector<TypeWriter*> ws) {
    for (auto* w : ws)
        for (auto& n : w->names) t.ptr(n.first, S_TYPE, t.cstr(n.second));
}

// root type in section S_TYPE (or root_type_sec), root object in section S_OBJ
inline Bytes write_file(std::vector<Sec>& secs, uint32_t root_type_off, uint32_t root_obj_off, uint32_t compression = 0,
                        uint32_t root_type_sec = S_TYPE) {
    const uint32_t hdr = 0x68 + 44 * (uint32_t)secs.size();
    Bytes f(hdr, 0);
    const uint8_t magic[16] = {0x29, 0xde, 0x6c, 0xc0, 0xba, 0xa4, 0x53, 0x2b, 0x25, 0xf5, 0xb7, 0xa5, 0xf6, 0x66, 0xe2, 0xee};
    std::memcpy(f.data(), magic, 16);
    auto w32 = [&](size_t at, uint32_t v) { std::memcpy(&f[at], &v, 4); };
    w32(0x10, hdr);
    w32(0x20, 7);
    w32(0x2C, 0x48);
    w32(0x30, (uint32_t)secs.size());
    w32(0x34, root_type_sec); w32(0x38, root_type_off);
    w32(0x3C, S_OBJ); w32(0x40, root_obj_off);
    w32(0x44, 0x80000031u);
    for (size_t i = 0; i < secs.size(); ++i) {
        size_t e = 0x68 + 44 * i;
        uint32_t data_off = (uint32_t)f.size();
        f.insert(f.end(), secs[i].b.begin(), secs[i].b.end());
        uint32_t fix_off = (uint32_t)f.size();
        for (auto& x : secs[i].fix) {
            uint32_t v[3] = {x.src, x.ts, x.to};
            f.insert(f.end(), (uint8_t*)v, (uint8_t*)v + 12);
        }
        w32(e, compression); w32(e + 4, data_off); w32(e + 8, (uint32_t)secs[i].b.size());
        w32(e + 12, (uint32_t)secs[i].b.size()); w32(e + 16, 4);
        w32(e + 28, fix_off); w32(e + 32, (uint32_t)secs[i].fix.size()); w32(e + 36, (uint32_t)f.size());
    }
    w32(0x24, (uint32_t)f.size());
    w32(0x28, c5m::crc32(f.data() + 0x68, f.size() - 0x68));
    return f;
}

inline void recrc(Bytes& f) {
    uint32_t c = c5m::crc32(f.data() + 0x68, f.size() - 0x68);
    std::memcpy(&f[0x28], &c, 4);
}

// section-table field k (0 compression, 1 data offset, 2 stored size, 3 expanded size, ...) of section i
inline void set_section_field(Bytes& f, uint32_t i, uint32_t k, uint32_t v) { std::memcpy(&f[0x68 + 44 * i + 4 * k], &v, 4); }

}  // namespace gr2w
