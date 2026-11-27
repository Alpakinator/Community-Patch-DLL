// gr2_file.cpp - Granny 2 container: header, section table, decompression, fix-ups, type tree, typed object access,
// string databases. Epic 3100 (issue 3101), own code, 2026-11-11. Format facts: research/05-graphics-composition/
// landmarks-and-models.md section 4 (one-off 9012); member-type numbers and sizes as LSLib's Format.cs (MIT, read
// as reference only by 9012) and as 9012's gr2read.py.
#include "c5models/gr2.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace c5m {

const char* err_name(Err e) {
    switch (e) {
    case Err::None: return "None";
    case Err::Truncated: return "Truncated";
    case Err::BadMagic: return "BadMagic";
    case Err::BadHeader: return "BadHeader";
    case Err::BadSectionTable: return "BadSectionTable";
    case Err::UnsupportedCompression: return "UnsupportedCompression";
    case Err::DecompressCorrupt: return "DecompressCorrupt";
    case Err::DecompressSize: return "DecompressSize";
    case Err::FixupOutOfRange: return "FixupOutOfRange";
    case Err::TypeTreeCorrupt: return "TypeTreeCorrupt";
    case Err::ObjectOutOfRange: return "ObjectOutOfRange";
    case Err::BadStringDatabase: return "BadStringDatabase";
    case Err::MissingMember: return "MissingMember";
    case Err::GeometryCheck: return "GeometryCheck";
    case Err::Xml: return "Xml";
    case Err::NotFound: return "NotFound";
    case Err::BadCrc: return "BadCrc";
    case Err::OutOfMemory: return "OutOfMemory";
    case Err::ReadBudget: return "ReadBudget";
    }
    return "?";
}

const char* member_type_name(MemberType t) {
    static const char* n[] = {"None", "Inline", "Reference", "ReferenceToArray", "ArrayOfReferences",
                              "VariantReference", "6?", "ReferenceToVariantArray", "String", "Transform", "Real32",
                              "Int8", "UInt8", "BinormalInt8", "NormalUInt8", "Int16", "UInt16", "BinormalInt16",
                              "NormalUInt16", "Int32", "UInt32", "Real16", "EmptyReference"};
    uint32_t i = (uint32_t)t;
    return i < sizeof(n) / sizeof(n[0]) ? n[i] : "?";
}

uint32_t member_type_size(MemberType t) {
    switch (t) {
    case MemberType::Reference: return 4;
    case MemberType::ReferenceToArray: return 8;
    case MemberType::ArrayOfReferences: return 8;
    case MemberType::VariantReference: return 8;
    case MemberType::ReferenceToVariantArray: return 12;
    case MemberType::String: return 4;
    case MemberType::Transform: return 68;
    case MemberType::Real32: case MemberType::Int32: case MemberType::UInt32: return 4;
    case MemberType::Int8: case MemberType::UInt8: case MemberType::BinormalInt8: case MemberType::NormalUInt8:
        return 1;
    case MemberType::Int16: case MemberType::UInt16: case MemberType::BinormalInt16: case MemberType::NormalUInt16:
    case MemberType::Real16:
        return 2;
    case MemberType::EmptyReference: return 4;
    default: return 0;
    }
}

static bool known_type(uint32_t t) { return t >= 1 && t <= 22 && t != 6; }

// ------------------------------------------------------------------------------------------------ CRC-32 (zlib)
uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

// ------------------------------------------------------------------------------------------------ byte order
static inline uint32_t bswap32(uint32_t v) {
    return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
}
static inline uint16_t bswap16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }

static const uint8_t kMagicLE[16] = {0x29, 0xde, 0x6c, 0xc0, 0xba, 0xa4, 0x53, 0x2b,
                                     0x25, 0xf5, 0xb7, 0xa5, 0xf6, 0x66, 0xe2, 0xee};
static const uint8_t kMagicBE[16] = {0xb5, 0x95, 0x11, 0x0e, 0x4b, 0xb5, 0xa5, 0x6a,
                                     0x50, 0x28, 0x28, 0xeb, 0x04, 0xb3, 0x78, 0x25};

static uint32_t rd32(const uint8_t* p, bool be) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return be ? bswap32(v) : v;
}

static bool fail(Error* err, Err c, const std::string& what) {
    if (err) *err = Error{c, what};
    return false;
}

// ------------------------------------------------------------------------------------------------ layer 1
bool parse_header(const uint8_t* d, size_t size, Header& h, Error* err) {
    h = Header();
    if (size < 0x68) return fail(err, Err::Truncated, "file shorter than the Granny header (" + std::to_string(size) + " B)");
    if (std::memcmp(d, kMagicLE, 16) == 0)
        h.big_endian = false;
    else if (std::memcmp(d, kMagicBE, 16) == 0)
        h.big_endian = true;
    else
        return fail(err, Err::BadMagic, "not a Granny 2 file (magic)");
    bool be = h.big_endian;
    h.header_size = rd32(d + 0x10, be);
    h.header_format = rd32(d + 0x14, be);
    h.version = rd32(d + 0x20, be);
    h.total_size = rd32(d + 0x24, be);
    h.crc = rd32(d + 0x28, be);
    h.section_table_offset = rd32(d + 0x2C, be);
    h.section_count = rd32(d + 0x30, be);
    h.root_type = Addr{rd32(d + 0x34, be), rd32(d + 0x38, be)};
    h.root_object = Addr{rd32(d + 0x3C, be), rd32(d + 0x40, be)};
    h.type_tag = rd32(d + 0x44, be);
    for (int i = 0; i < 4; ++i) h.extra_tags[i] = rd32(d + 0x48 + 4 * i, be);
    h.string_db_crc = rd32(d + 0x58, be);
    if (h.version != 6 && h.version != 7)
        return fail(err, Err::BadHeader, "file-format version " + std::to_string(h.version) + " (expected 6 or 7)");
    if (h.header_size < 0x68 || h.header_size > size)
        return fail(err, Err::BadHeader, "header size " + std::to_string(h.header_size));
    if (h.total_size > size)
        return fail(err, Err::Truncated, "file has " + std::to_string(size) + " B, header declares " +
                                             std::to_string(h.total_size));
    if (h.total_size < h.header_size)
        return fail(err, Err::BadHeader, "total size smaller than the header");
    // The header CRC is zlib's CRC-32 of every byte after the 0x68-byte header block up to the total size
    // (established 2026-11-11 on warrior.gr2 and tank.gr2, then on every file by the sweep, issue 3106).
    if (crc32(d + 0x68, h.total_size - 0x68) != h.crc)
        return fail(err, Err::BadCrc, "file CRC mismatch (corrupt or modified file)");
    if (h.section_count == 0 || h.section_count > 64)
        return fail(err, Err::BadHeader, "section count " + std::to_string(h.section_count));
    uint64_t tab = 0x20ull + h.section_table_offset;
    if (tab + 44ull * h.section_count > h.header_size)
        return fail(err, Err::BadSectionTable, "section table outside the header");
    uint64_t expanded_total = 0;
    for (uint32_t i = 0; i < h.section_count; ++i) {
        const uint8_t* e = d + tab + 44 * i;
        SectionHeader s;
        uint32_t* f[11] = {&s.compression, &s.data_offset, &s.data_size, &s.expanded_size, &s.alignment, &s.stop0,
                           &s.stop1, &s.fixup_offset, &s.fixup_count, &s.marshal_offset, &s.marshal_count};
        for (int k = 0; k < 11; ++k) *f[k] = rd32(e + 4 * k, be);
        auto in = [&](uint64_t off, uint64_t len) { return off + len <= h.total_size; };
        if (!in(s.data_offset, s.data_size))
            return fail(err, Err::BadSectionTable, "section " + std::to_string(i) + " data outside the file");
        if (!in(s.fixup_offset, 12ull * s.fixup_count))
            return fail(err, Err::BadSectionTable, "section " + std::to_string(i) + " fix-up table outside the file");
        if (!in(s.marshal_offset, 16ull * s.marshal_count))
            return fail(err, Err::BadSectionTable,
                        "section " + std::to_string(i) + " marshalling table outside the file");
        if (s.expanded_size > (1u << 30))
            return fail(err, Err::BadSectionTable, "section " + std::to_string(i) + " expanded size too large");
        // Bug 0093 V4 (issue 9025): a compressed section may not declare more than kMaxExpandRatio times its stored
        // size (an empty stored section therefore expands to nothing), and all sections together not more than
        // kMaxExpandedTotal, so that a small file cannot make the reader allocate gigabytes. Uncompressed sections
        // are copied at their stored size and count with that.
        if (s.compression == 2 && (uint64_t)s.expanded_size > (uint64_t)kMaxExpandRatio * s.data_size)
            return fail(err, Err::BadSectionTable,
                        "section " + std::to_string(i) + " declares " + std::to_string(s.expanded_size) +
                            " B expanded from " + std::to_string(s.data_size) + " B stored (more than " +
                            std::to_string(kMaxExpandRatio) + " times)");
        expanded_total += s.compression == 2 ? s.expanded_size : s.data_size;
        if (expanded_total > kMaxExpandedTotal)
            return fail(err, Err::BadSectionTable, "sections expand to more than " +
                                                       std::to_string(kMaxExpandedTotal) + " B in all");
        h.sections.push_back(s);
    }
    if (h.root_type.sec >= h.section_count || h.root_object.sec >= h.section_count)
        return fail(err, Err::BadHeader, "root reference names a section that does not exist");
    return true;
}

// ------------------------------------------------------------------------------------------------ strings
uint32_t StringDatabase::hash(const std::string& s) {
    return crc32((const uint8_t*)s.data(), s.size()) ^ 0xFFFFFFFFu;
}

const std::string* StringDatabase::find(uint32_t h) const {
    auto it = map_.find(h);
    return it == map_.end() ? nullptr : &it->second;
}

bool StringDatabase::add_gsd(const uint8_t* data, size_t size, Error* err) {
    RawFile f;
    if (!f.load(data, size, nullptr, err)) return false;
    try {
        Object r = f.root();
        if (!r.has("Strings") || !r.has("DatabaseCRC"))
            return fail(err, Err::BadStringDatabase, "root is not {Strings, DatabaseCRC}");
        Array a = r.array("Strings");
        for (uint32_t i = 0; i < a.size(); ++i) {
            Object o = a.at(i);
            if (o.null()) continue;
            std::string s = o.str("String");
            map_.emplace(hash(s), s);
        }
        crc_ = r.u32("DatabaseCRC");
    } catch (const ParseError& e) {
        return fail(err, e.code, std::string("string database: ") + e.what());
    } catch (const std::bad_alloc&) {
        return fail(err, Err::OutOfMemory, "string database: out of memory");
    }
    return true;
}

// ------------------------------------------------------------------------------------------------ RawFile
static uint64_t key(Addr a) { return ((uint64_t)a.sec << 32) | a.off; }

bool RawFile::load(const uint8_t* data, size_t size, const StringDatabase* strings, Error* err) {
    *this = RawFile();
    strings_ = strings;
    if (!parse_header(data, size, h_, err)) return false;
    // Bug 0093 V4 (issue 9025): an allocation that fails here (sizes inside the limits of parse_header, but more
    // memory than the process has) is a named error, not an exception that escapes the reader.
    try {
        return load_sections(data, err);
    } catch (const std::bad_alloc&) {
        return fail(err, Err::OutOfMemory, "out of memory while expanding the sections");
    }
}

bool RawFile::load_sections(const uint8_t* data, Error* err) {
    bool be = h_.big_endian;
    sec_.resize(h_.section_count);
    fix_.resize(h_.section_count);
    for (uint32_t i = 0; i < h_.section_count; ++i) {
        const SectionHeader& s = h_.sections[i];
        const uint8_t* raw = data + s.data_offset;
        if (s.compression == 0) {
            sec_[i].assign(raw, raw + s.data_size);
        } else if (s.compression == 2) {
            Error e;
            if (!oodle1_decompress(raw, s.data_size, s.stop0, s.stop1, s.expanded_size, sec_[i], &e, be)) {
                e.what = "section " + std::to_string(i) + ": " + e.what;
                if (be) e.what += " (big-endian file: not decodable, research/05-graphics-composition/model-reader.md 2)";
                if (err) *err = e;
                return false;
            }
        } else {
            return fail(err, Err::UnsupportedCompression,
                        "section " + std::to_string(i) + " compression " + std::to_string(s.compression));
        }
    }
    for (uint32_t i = 0; i < h_.section_count; ++i) {
        const SectionHeader& s = h_.sections[i];
        auto& v = fix_[i];
        v.reserve(s.fixup_count);
        for (uint32_t k = 0; k < s.fixup_count; ++k) {
            const uint8_t* e = data + s.fixup_offset + 12ull * k;
            uint32_t src = rd32(e, be), ts = rd32(e + 4, be), to = rd32(e + 8, be);
            if ((uint64_t)src + 4 > sec_[i].size())
                return fail(err, Err::FixupOutOfRange, "fix-up " + std::to_string(k) + " of section " +
                                                           std::to_string(i) + ": source offset " +
                                                           std::to_string(src) + " outside the section");
            if (ts >= h_.section_count || to > sec_[ts].size())
                return fail(err, Err::FixupOutOfRange, "fix-up " + std::to_string(k) + " of section " +
                                                           std::to_string(i) + ": target " + std::to_string(ts) +
                                                           ":" + std::to_string(to) + " outside the file");
            v.push_back(std::make_pair(src, Addr{ts, to}));
        }
        std::sort(v.begin(), v.end(), [](const std::pair<uint32_t, Addr>& a, const std::pair<uint32_t, Addr>& b) {
            return a.first < b.first;
        });
        stats_.fixups += s.fixup_count;
        stats_.marshallings += s.marshal_count;
    }
    if (h_.root_type.off > sec_[h_.root_type.sec].size() || h_.root_object.off > sec_[h_.root_object.sec].size())
        return fail(err, Err::BadHeader, "root reference outside its section");
    return true;
}

bool RawFile::fixup(Addr slot, Addr& target) const {
    if (slot.sec >= fix_.size()) return false;
    const auto& v = fix_[slot.sec];
    auto it = std::lower_bound(v.begin(), v.end(), slot.off,
                               [](const std::pair<uint32_t, Addr>& e, uint32_t o) { return e.first < o; });
    if (it == v.end() || it->first != slot.off) return false;
    target = it->second;
    return true;
}

void RawFile::need(Addr a, uint32_t n) const {
    if (a.sec >= sec_.size() || (uint64_t)a.off + n > sec_[a.sec].size())
        throw ParseError(Err::ObjectOutOfRange, "read of " + std::to_string(n) + " B at " + std::to_string(a.sec) +
                                                    ":" + std::to_string(a.off) + " outside the section");
}

uint32_t RawFile::u32(Addr a) const {
    need(a, 4);
    return rd32(&sec_[a.sec][a.off], h_.big_endian);
}
uint16_t RawFile::u16(Addr a) const {
    need(a, 2);
    uint16_t v;
    std::memcpy(&v, &sec_[a.sec][a.off], 2);
    return h_.big_endian ? bswap16(v) : v;
}
uint8_t RawFile::u8(Addr a) const {
    need(a, 1);
    return sec_[a.sec][a.off];
}
float RawFile::f32(Addr a) const {
    uint32_t v = u32(a);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

// Bug 0093 V10/V11 (issue 4000): the per-file read budget. A bookkeeping entry of an unordered_map (visited_,
// unresolved_) is counted as kMapNodeBytes: node, allocation overhead and bucket share, rounded up [I: MSVC x64 STL].
static const uint64_t kMapNodeBytes = 64;

void RawFile::charge_bytes(uint64_t n) const {
    stats_.built_bytes += n;
    if (stats_.built_bytes > kMaxBuiltBytes)
        throw ParseError(Err::ReadBudget, "read budget: the file makes the reader build more than " +
                                              std::to_string(kMaxBuiltBytes) + " B (kMaxBuiltBytes)");
}

void RawFile::charge_steps(uint64_t n) const {
    stats_.read_steps += n;
    if (stats_.read_steps > kMaxReadSteps)
        throw ParseError(Err::ReadBudget, "read budget: the file needs more than " + std::to_string(kMaxReadSteps) +
                                              " read steps (kMaxReadSteps)");
}

std::string RawFile::cstr(Addr a) const {
    need(a, 0);
    const Bytes& b = sec_[a.sec];
    auto e = std::find(b.begin() + a.off, b.end(), (uint8_t)0);
    if (e == b.end()) throw ParseError(Err::ObjectOutOfRange, "unterminated string");
    // (issue 4000, bug 0093 V11) every string returned is counted before it is copied: one long string behind many
    // slots once meant gigabytes of copying from a 2 MB file
    charge_bytes((uint64_t)(e - (b.begin() + a.off)) + 1);
    return std::string(b.begin() + a.off, e);
}

bool RawFile::string_slot_null(Addr slot) const {
    Addr t;
    if (fixup(slot, t)) return false;
    return h_.string_db_crc == 0;
}

std::string RawFile::string_slot(Addr slot) const {
    charge_steps(1);
    Addr t;
    if (fixup(slot, t)) return cstr(t);
    uint32_t v = u32(slot);
    if (h_.string_db_crc == 0) return std::string();   // no database: an unfixed slot is a NULL string
    if (strings_) {
        const std::string* s = strings_->find(v);
        if (s) {
            charge_bytes(s->size() + 1);
            return *s;
        }
    }
    if (unresolved_.emplace(key(slot), 1).second) charge_bytes(kMapNodeBytes);
    char buf[16];
    std::snprintf(buf, sizeof buf, "#%08X", v);
    charge_bytes(10);
    return buf;
}

// (issue 4000, bug 0093 V11) what string_slot() checks and records, without the string it would return and throw
// away: walk_all visits every slot (1.1e9 in one crafted file), and the copy and the "#hash" text were most of the
// cost. Same errors (an unterminated string, a slot outside its section); a fixed-up string is scanned, and the
// scanned bytes count as built bytes, as the copy did.
void RawFile::walk_string_slot(Addr slot) const {
    charge_steps(1);
    Addr t;
    if (fixup(slot, t)) {
        need(t, 0);
        const Bytes& b = sec_[t.sec];
        auto e = std::find(b.begin() + t.off, b.end(), (uint8_t)0);
        if (e == b.end()) throw ParseError(Err::ObjectOutOfRange, "unterminated string");
        charge_bytes((uint64_t)(e - (b.begin() + t.off)) + 1);
        return;
    }
    uint32_t v = u32(slot);
    if (h_.string_db_crc == 0 || (strings_ && strings_->find(v))) return;
    if (unresolved_.emplace(key(slot), 1).second) charge_bytes(kMapNodeBytes);
}

const TypeDef* RawFile::typedef_at(Addr a) const {
    uint64_t k = key(a);
    auto it = types_.find(k);
    if (it != types_.end()) return it->second.get();
    if (types_.size() > 100000) throw ParseError(Err::TypeTreeCorrupt, "too many type definitions");
    // Bug 0093 V1 (issue 9025): each definition reached for the first time through a member's reference is read
    // by a nested call, so a chain of definitions that reference one another would overflow the stack (1,500 did).
    struct DepthGuard {
        uint32_t& d;
        explicit DepthGuard(uint32_t& x) : d(x) { ++d; }
        ~DepthGuard() { --d; }
    } guard(type_depth_);
    if (type_depth_ > kMaxTypeNesting)
        throw ParseError(Err::TypeTreeCorrupt, "type definitions nested more than " +
                                                   std::to_string(kMaxTypeNesting) + " deep");
    // Bug 0093 V10 (issue 4000): count the members first and charge the definition's records before building them.
    // Overlapping definitions (each suffix of one run of members is a definition of its own) once built 8.4 million
    // records (643 MB) from a 328 KB file. The count only measures: whatever is wrong with an entry is still reported
    // by the loop below, as before.
    uint32_t nm = 0;
    while (nm <= 4096 && (uint64_t)a.off + 32ull * (nm + 1) <= sec_[a.sec].size() &&
           known_type(u32(Addr{a.sec, a.off + 32 * nm})))
        ++nm;
    charge_steps(nm + 1ull);
    charge_bytes(sizeof(TypeDef) + kMapNodeBytes + (uint64_t)nm * sizeof(Member));
    std::unique_ptr<TypeDef> up(new TypeDef());
    TypeDef* t = up.get();
    t->addr = a;
    t->members.reserve(nm);
    types_[k] = std::move(up);
    uint32_t off = a.off;
    uint32_t size = 0;
    bool ptrs = false;
    for (int n = 0;; ++n) {
        if (n > 4096) throw ParseError(Err::TypeTreeCorrupt, "unterminated type definition");
        Addr e{a.sec, off};
        need(e, 32);
        uint32_t ty = u32(e);
        if (ty == 0) break;
        if (!known_type(ty))
            throw ParseError(Err::TypeTreeCorrupt, "unknown member type " + std::to_string(ty) + " at " +
                                                       std::to_string(a.sec) + ":" + std::to_string(off));
        Member m;
        m.type = (MemberType)ty;
        m.name = string_slot(Addr{a.sec, off + 4});
        Addr r;
        if (fixup(Addr{a.sec, off + 8}, r)) m.ref = typedef_at(r);
        m.array_size = u32(Addr{a.sec, off + 12});
        if (m.array_size > 65536) throw ParseError(Err::TypeTreeCorrupt, "member array size too large");
        uint32_t es;
        if (m.type == MemberType::Inline) {
            if (!m.ref) throw ParseError(Err::TypeTreeCorrupt, "inline member '" + m.name + "' without a type");
            if (!m.ref->complete) throw ParseError(Err::TypeTreeCorrupt, "type contains itself inline");
            es = m.ref->size;
            if (m.ref->has_pointers) ptrs = true;
        } else {
            es = member_type_size(m.type);
            if (m.type == MemberType::String || m.type == MemberType::Reference ||
                m.type == MemberType::ReferenceToArray || m.type == MemberType::ArrayOfReferences ||
                m.type == MemberType::VariantReference || m.type == MemberType::ReferenceToVariantArray)
                ptrs = true;
        }
        m.offset = size;
        uint64_t total = (uint64_t)es * (m.array_size ? m.array_size : 1);
        if (size + total > (1u << 24)) throw ParseError(Err::TypeTreeCorrupt, "type size too large");
        m.size = (uint32_t)total;
        size += m.size;
        t->members.push_back(m);
        off += 32;
    }
    t->size = size;
    t->has_pointers = ptrs;
    t->complete = true;
    return t;
}

const Member* TypeDef::find(const char* name) const {
    // the last member of a name wins, as in a dict built in order (the Python reference)
    const Member* r = nullptr;
    for (const auto& m : members)
        if (m.name == name) r = &m;
    return r;
}

Object RawFile::root() const {
    return Object(this, typedef_at(h_.root_type), h_.root_object);
}

// ------------------------------------------------------------------------------------------------ walk_all
// A visited key counts as kVisitBytes: an 8-byte slot of a table at most half full, plus the old table while it grows.
static const uint64_t kVisitBytes = 32;

bool RawFile::visit_once(uint64_t k) const {
    if (k == 0) k = 0x9E3779B97F4A7C15ull;   // 0 marks an empty slot
    if ((visited_n_ + 1) * 2 > visited_.size()) {
        std::vector<uint64_t> old(visited_.empty() ? 1024 : visited_.size() * 2, 0);
        old.swap(visited_);
        size_t mask = visited_.size() - 1;
        for (uint64_t x : old)
            if (x) {
                uint64_t h = x;
                h ^= h >> 33; h *= 0xFF51AFD7ED558CCDull; h ^= h >> 33;
                size_t i = (size_t)h & mask;
                while (visited_[i]) i = (i + 1) & mask;
                visited_[i] = x;
            }
    }
    size_t mask = visited_.size() - 1;
    uint64_t h = k;
    h ^= h >> 33; h *= 0xFF51AFD7ED558CCDull; h ^= h >> 33;
    size_t i = (size_t)h & mask;
    while (visited_[i]) {
        if (visited_[i] == k) return false;
        i = (i + 1) & mask;
    }
    visited_[i] = k;
    ++visited_n_;
    return true;
}

void RawFile::walk_all() const {
    if (h_.string_db_crc == 0) return;   // nothing can be unresolved
    visited_.clear();
    visited_n_ = 0;
    const TypeDef* t = typedef_at(h_.root_type);
    walk(t, h_.root_object, 0);
}

void RawFile::walk(const TypeDef* t, Addr a, int depth) const {
    if (!t || depth > 200) return;
    // Bug 0093 V10/V11 (issue 4000): every visit, every element and every bookkeeping entry counts against the read
    // budget. One array walked through 4,096 types once kept 2.7e8 entries in visited_ (the 1 GiB cap, then a stack
    // overflow); 1,024 overlapping types over one 4 MiB array took 358 s.
    charge_steps(1);
    uint64_t k = key(a) ^ ((uint64_t)(uintptr_t)t * 0x9E3779B97F4A7C15ull);
    if (!visit_once(k)) return;
    charge_bytes(kVisitBytes);
    need(a, t->size);
    for (const Member& m : t->members) {
        switch (m.type) {   // members without strings or pointers have nothing to visit (numbers, Transform, ...)
        case MemberType::Inline: case MemberType::String: case MemberType::Reference:
        case MemberType::ReferenceToArray: case MemberType::ArrayOfReferences: case MemberType::VariantReference:
        case MemberType::ReferenceToVariantArray:
            break;
        default: continue;
        }
        uint32_t n = m.array_size ? m.array_size : 1;
        uint32_t es = n ? m.size / n : 0;
        charge_steps(n);
        for (uint32_t i = 0; i < n; ++i) {
            Addr e{a.sec, a.off + m.offset + es * i};
            Addr p, tp;
            switch (m.type) {
            case MemberType::Inline: walk(m.ref, e, depth + 1); break;
            case MemberType::String: walk_string_slot(e); break;
            case MemberType::Reference:
                if (m.ref && fixup(e, p)) walk(m.ref, p, depth + 1);
                break;
            case MemberType::ReferenceToArray: {
                uint32_t c = u32(e);
                if (m.ref && m.ref->has_pointers && fixup(Addr{e.sec, e.off + 4}, p)) {
                    need(p, (uint32_t)std::min<uint64_t>((uint64_t)c * m.ref->size, 0xFFFFFFFFull));
                    for (uint32_t j = 0; j < c; ++j) walk(m.ref, Addr{p.sec, p.off + m.ref->size * j}, depth + 1);
                }
                break;
            }
            case MemberType::ArrayOfReferences: {
                uint32_t c = u32(e);
                if (m.ref && fixup(Addr{e.sec, e.off + 4}, p))
                    for (uint32_t j = 0; j < c; ++j) {
                        charge_steps(1);
                        Addr q;
                        need(Addr{p.sec, p.off + 4 * j}, 4);
                        if (fixup(Addr{p.sec, p.off + 4 * j}, q)) walk(m.ref, q, depth + 1);
                    }
                break;
            }
            case MemberType::VariantReference:
                if (fixup(e, tp) && fixup(Addr{e.sec, e.off + 4}, p)) walk(typedef_at(tp), p, depth + 1);
                break;
            case MemberType::ReferenceToVariantArray:
                if (fixup(e, tp)) {
                    const TypeDef* vt = typedef_at(tp);
                    uint32_t c = u32(Addr{e.sec, e.off + 4});
                    if (vt->has_pointers && fixup(Addr{e.sec, e.off + 8}, p))
                        for (uint32_t j = 0; j < c; ++j) walk(vt, Addr{p.sec, p.off + vt->size * j}, depth + 1);
                }
                break;
            default: break;
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------ Object / Array
bool Object::has(const char* name) const {
    if (!t_) return false;
    // (issue 4000, bug 0093 V11) a look-up scans every member: with 4,096 members and many references to one object
    // that was the costly part of a read, so it counts the members it scans
    if (f_) f_->charge_steps(1 + t_->members.size());
    return t_->find(name) != nullptr;
}

const Member& Object::need(const char* name) const {
    if (null()) throw ParseError(Err::MissingMember, std::string("member '") + name + "' of a NULL object");
    f_->charge_steps(1 + t_->members.size());   // as in has()
    const Member* m = t_->find(name);
    if (!m) throw ParseError(Err::MissingMember, std::string("type has no member '") + name + "'");
    return *m;
}

static float half_to_float(uint16_t h) {
    uint32_t s = (uint32_t)(h >> 15) << 31, e = (h >> 10) & 0x1F, m = h & 0x3FF, bits;
    if (e == 0) {
        if (m == 0) {
            bits = s;
        } else {   // subnormal
            float f = std::ldexp((float)m, -24);
            return (h >> 15) ? -f : f;
        }
    } else if (e == 31) {
        bits = s | 0x7F800000u | (m << 13);
    } else {
        bits = s | ((e + 112) << 23) | (m << 13);
    }
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

static double prim(const RawFile* f, MemberType t, Addr a) {
    switch (t) {
    case MemberType::Real32: return f->f32(a);
    case MemberType::Int8: case MemberType::BinormalInt8: return (int8_t)f->u8(a);
    case MemberType::UInt8: case MemberType::NormalUInt8: return f->u8(a);
    case MemberType::Int16: case MemberType::BinormalInt16: return (int16_t)f->u16(a);
    case MemberType::UInt16: case MemberType::NormalUInt16: return f->u16(a);
    case MemberType::Int32: return (int32_t)f->u32(a);
    case MemberType::UInt32: return f->u32(a);
    case MemberType::Real16: return half_to_float(f->u16(a));
    default: throw ParseError(Err::TypeTreeCorrupt, std::string("not a number: ") + member_type_name(t));
    }
}

double Object::number(const char* name, uint32_t idx) const {
    const Member& m = need(name);
    uint32_t n = m.array_size ? m.array_size : 1;
    if (idx >= n) throw ParseError(Err::ObjectOutOfRange, std::string("index past member '") + name + "'");
    return prim(f_, m.type, Addr{a_.sec, a_.off + m.offset + (m.size / n) * idx});
}

std::string Object::str(const char* name) const {
    const Member& m = need(name);
    if (m.type != MemberType::String) throw ParseError(Err::TypeTreeCorrupt, std::string("not a string: ") + name);
    return f_->string_slot(Addr{a_.sec, a_.off + m.offset});
}

bool Object::str_null(const char* name) const {
    const Member& m = need(name);
    return f_->string_slot_null(Addr{a_.sec, a_.off + m.offset});
}

Transform Object::transform(const char* name) const {
    const Member& m = need(name);
    if (m.type != MemberType::Transform)
        throw ParseError(Err::TypeTreeCorrupt, std::string("not a transform: ") + name);
    Addr a{a_.sec, a_.off + m.offset};
    f_->need(a, 68);
    Transform t;
    t.flags = f_->u32(a);
    for (int i = 0; i < 3; ++i) t.translation[i] = f_->f32(Addr{a.sec, a.off + 4 + 4 * i});
    for (int i = 0; i < 4; ++i) t.rotation[i] = f_->f32(Addr{a.sec, a.off + 16 + 4 * i});
    for (int i = 0; i < 9; ++i) t.scale_shear[i] = f_->f32(Addr{a.sec, a.off + 32 + 4 * i});
    return t;
}

Object Object::ref(const char* name) const {
    const Member& m = need(name);
    if (m.type != MemberType::Reference) throw ParseError(Err::TypeTreeCorrupt, std::string("not a reference: ") + name);
    Addr p;
    if (!f_->fixup(Addr{a_.sec, a_.off + m.offset}, p)) return Object();
    if (!m.ref) throw ParseError(Err::TypeTreeCorrupt, std::string("reference without a type: ") + name);
    f_->need(p, m.ref->size);
    return Object(f_, m.ref, p);
}

Object Object::variant(const char* name) const {
    const Member& m = need(name);
    if (m.type != MemberType::VariantReference)
        throw ParseError(Err::TypeTreeCorrupt, std::string("not a variant reference: ") + name);
    Addr tp, p;
    if (!f_->fixup(Addr{a_.sec, a_.off + m.offset}, tp) || !f_->fixup(Addr{a_.sec, a_.off + m.offset + 4}, p))
        return Object();
    const TypeDef* t = f_->typedef_at(tp);
    f_->need(p, t->size);
    return Object(f_, t, p);
}

Object Object::inline_member(const char* name) const {
    const Member& m = need(name);
    if (m.type != MemberType::Inline) throw ParseError(Err::TypeTreeCorrupt, std::string("not inline: ") + name);
    return Object(f_, m.ref, Addr{a_.sec, a_.off + m.offset});
}

Array Object::array(const char* name) const {
    const Member& m = need(name);
    Array r;
    r.f_ = f_;
    Addr e{a_.sec, a_.off + m.offset}, p;
    switch (m.type) {
    case MemberType::ReferenceToArray:
    case MemberType::ArrayOfReferences: {
        uint32_t c = f_->u32(e);
        if (!f_->fixup(Addr{e.sec, e.off + 4}, p) || c == 0) return r;
        if (!m.ref) throw ParseError(Err::TypeTreeCorrupt, std::string("array without a type: ") + name);
        r.t_ = m.ref;
        r.refs_ = m.type == MemberType::ArrayOfReferences;
        uint64_t bytes = (uint64_t)c * (r.refs_ ? 4 : m.ref->size);
        // Bug 0093 V2 (issue 9025): elements of 0 bytes would let any count pass the bounds check below, and every
        // loop over the array would then run that count (up to 4.3e9) without reading anything.
        if (bytes == 0) throw ParseError(Err::ObjectOutOfRange, std::string("array of 0-byte elements: ") + name);
        if (bytes > 0xFFFFFFFFull) throw ParseError(Err::ObjectOutOfRange, std::string("array too large: ") + name);
        f_->need(p, (uint32_t)bytes);
        r.base_ = p;
        r.count_ = c;
        return r;
    }
    case MemberType::ReferenceToVariantArray: {
        Addr tp;
        uint32_t c = f_->u32(Addr{e.sec, e.off + 4});
        if (!f_->fixup(e, tp)) return r;
        r.t_ = f_->typedef_at(tp);
        if (!f_->fixup(Addr{e.sec, e.off + 8}, p) || c == 0) return r;
        uint64_t bytes = (uint64_t)c * r.t_->size;
        if (bytes == 0) throw ParseError(Err::ObjectOutOfRange, std::string("array of 0-byte elements: ") + name);
        if (bytes > 0xFFFFFFFFull) throw ParseError(Err::ObjectOutOfRange, std::string("array too large: ") + name);
        f_->need(p, (uint32_t)bytes);
        r.base_ = p;
        r.count_ = c;
        return r;
    }
    default: throw ParseError(Err::TypeTreeCorrupt, std::string("not an array: ") + name);
    }
}

Object Array::at(uint32_t i) const {
    if (i >= count_) throw ParseError(Err::ObjectOutOfRange, "array index");
    f_->charge_steps(1);   // (issue 4000, bug 0093 V10/V11) each element read counts against the read budget
    if (refs_) {
        Addr q;
        if (!f_->fixup(Addr{base_.sec, base_.off + 4 * i}, q)) return Object();
        f_->need(q, t_->size);
        return Object(f_, t_, q);
    }
    return Object(f_, t_, Addr{base_.sec, base_.off + t_->size * i});
}

}  // namespace c5m
