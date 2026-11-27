// test_psb.cpp - the .psb particle file reader (ticket 4787): the header and the emitter block chain, the string
// slot rule, bare-name texture lookup and the refusals (truncated files, bad chains, hostile sizes, mutations), each
// on built-in files; with the install (C5R_GAME_DIR, the render_game_dir setting, or C5M_PSB_INSTALL) every .psb in
// the install's archives is read at run time and the counts of effects-formats.md 4 and 16 are checked. Nothing of
// the game's files is in this source (D83): the real part only counts and names.
//
// The archives are read by a test-only FileProvider (FpkProvider below) written from the .fpk layout of
// research/05-graphics-composition/asset-pipeline.md 1.1; the server's own FPK reader is render/'s GameFiles.
#include "c5models/psb.h"
#include "testing.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>

using namespace c5m;

namespace {

// ---------------------------------------------------------------------------------------------- building files
void put_u32(Bytes& b, size_t at, uint32_t v) { std::memcpy(&b[at], &v, 4); }
void put_text(Bytes& b, size_t at, const std::string& s) { std::memcpy(&b[at], s.data(), s.size()); }

struct SlotSpec {
    uint32_t offset;
    uint32_t flag;
    std::string text;
};
struct BlockSpec {
    std::string name;
    uint32_t size;
    std::vector<SlotSpec> slots;
    uint32_t field_104 = 272;
};

// Parameter filler that never forms a slot: floats 1.0 (bytes 00 00 80 3f) and 0.5.
Bytes make_block(const BlockSpec& s) {
    Bytes b(s.size, 0);
    for (size_t o = kPsbBlockFixed; o + 4 <= b.size(); o += 4) put_u32(b, o, (o / 4) % 3 ? 0x3f800000u : 0x3f000000u);
    put_text(b, 0, s.name);
    for (size_t i = s.name.size(); i < kPsbNameSize; ++i) b[i] = 0;
    put_u32(b, 0x100, s.size);
    put_u32(b, 0x104, s.field_104);
    for (auto& sl : s.slots) {
        put_u32(b, sl.offset, sl.flag);
        std::fill(b.begin() + sl.offset + 4, b.begin() + sl.offset + 4 + kPsbNameSize, (uint8_t)0);
        put_text(b, sl.offset + 4, sl.text);
    }
    return b;
}

Bytes make_psb(const std::vector<Bytes>& blocks) {
    Bytes f(kPsbHeaderSize, 0);
    std::memcpy(&f[0], "PSB\0", 4);
    size_t payload = 0;
    for (auto& b : blocks) payload += b.size();
    put_u32(f, 4, kPsbVersion);
    put_u32(f, 8, (uint32_t)payload);
    put_u32(f, 12, kPsbHeaderSize);
    put_u32(f, 16, 16);
    for (auto& b : blocks) f.insert(f.end(), b.begin(), b.end());
    return f;
}

const uint32_t kA = 0x5D0, kB = 0x960;   // the sizes of the two built-in blocks

BlockSpec block_a() {
    // The common one-texture layout of effects-formats.md 16.2: Custom Data at 0x340, a texture slot at 0x4B0.
    return {"FXP_Test_A", kA, {{0x340, 0, "Custom Data"}, {0x4B0, 1, "Tex_A.dds"}}};
}
BlockSpec block_b() {
    // An art-tree path in the Custom Data slot, two texture slots 0x120 apart, a shader slot.
    return {"FXP_Test_B",
            kB,
            {{0x33C, 0, "X:\\art\\Effects\\library\\Texture\\Grad_B.DDS"},
             {0x5F4, 1, "Tex_B1.tga"},
             {0x714, 1, "Tex_B2.dds"},
             {0x834, 1, "Shade.txt"}}};
}

class MemProvider : public FileProvider {
public:
    std::set<std::string> names;
    bool read(const std::string& n, Bytes& out, std::string* origin) override {
        if (!names.count(n)) return false;
        out.clear();
        if (origin) *origin = "mem:" + n;
        return true;
    }
};

// ---------------------------------------------------------------------------------------------- built-in files
void test_valid() {
    Bytes ba = make_block(block_a()), bb = make_block(block_b());
    Bytes f = make_psb({ba, bb});
    PsbFile p;
    Error e;
    CHECK(parse_psb(f, p, &e));
    CHECK(e.code == Err::None);
    CHECK_EQ(p.version, 100u);
    CHECK_EQ(p.payload_size, (uint32_t)(f.size() - 0x40));
    CHECK_EQ(p.header_size, 0x40u);
    CHECK_EQ(p.field_10, 16u);
    CHECK(p.header_zero);
    CHECK_EQ(p.emitters.size(), (size_t)2);
    if (p.emitters.size() != 2) return;
    const PsbEmitter& a = p.emitters[0];
    const PsbEmitter& b = p.emitters[1];
    CHECK_EQ(a.name, std::string("FXP_Test_A"));
    CHECK_EQ(a.file_offset, 0x40u);
    CHECK_EQ(a.size, kA);
    CHECK_EQ(a.field_104, 272u);
    CHECK(a.raw == ba);
    CHECK_EQ(b.file_offset, 0x40u + kA);
    CHECK(b.raw == bb);
    CHECK_EQ(a.slots.size(), (size_t)2);
    CHECK_EQ(a.texture_count(), (size_t)1);
    if (a.slots.size() == 2) {
        CHECK(a.slots[0].kind == PsbSlotKind::CustomData);
        CHECK_EQ(a.slots[0].offset, 0x340u);
        CHECK_EQ(a.slots[0].flag, 0u);
        CHECK_EQ(a.slots[0].bare, std::string());
        CHECK(a.slots[1].kind == PsbSlotKind::Texture);
        CHECK_EQ(a.slots[1].offset, 0x4B0u);
        CHECK_EQ(a.slots[1].flag, 1u);
        CHECK_EQ(a.slots[1].text, std::string("Tex_A.dds"));
        CHECK_EQ(a.slots[1].bare, std::string("tex_a.dds"));
    }
    CHECK_EQ(b.slots.size(), (size_t)4);
    CHECK_EQ(b.texture_count(), (size_t)3);
    if (b.slots.size() == 4) {
        CHECK(b.slots[0].kind == PsbSlotKind::Texture);   // a path in the Custom Data slot, upper-case extension
        CHECK_EQ(b.slots[0].flag, 0u);
        CHECK_EQ(b.slots[0].bare, std::string("grad_b.dds"));
        CHECK_EQ(b.slots[1].bare, std::string("tex_b1.tga"));
        CHECK_EQ(b.slots[2].offset - b.slots[1].offset, kPsbSlotSize);
        CHECK(b.slots[3].kind == PsbSlotKind::Shader);
        CHECK_EQ(b.slots[3].bare, std::string("shade.txt"));
    }
    CHECK_EQ(std::string(psb_slot_kind_name(PsbSlotKind::Shader)), std::string("shader"));

    // Bare-name lookup.
    MemProvider mp;
    mp.names = {"tex_a.dds", "tex_b1.tga", "tex_b2.dds", "shade.txt"};
    CHECK_EQ(resolve_psb_textures(p, mp), (size_t)1);   // grad_b.dds is missing; the shader slot is not looked up
    CHECK(p.emitters[0].slots[1].found);
    CHECK_EQ(p.emitters[0].slots[1].origin, std::string("mem:tex_a.dds"));
    CHECK(!p.emitters[1].slots[0].found);
    CHECK(p.emitters[1].slots[0].origin.empty());
    CHECK(!p.emitters[1].slots[3].found);
}

void test_slot_rule() {
    // Strings that are not slots by the rule (effects-formats.md 16.2).
    BlockSpec s{"FXP_Rule", 0x1000, {}};
    Bytes b = make_block(s);
    auto plant = [&](size_t at, uint32_t flag, const std::string& text) {
        put_u32(b, at, flag);
        std::fill(b.begin() + at + 4, b.begin() + std::min(b.size(), at + 4 + kPsbNameSize), (uint8_t)0);
        put_text(b, at + 4, text);
    };
    plant(0x110, 2, "flag2.dds");            // flag not 0 or 1
    plant(0x220, 1, "P");                    // no extension, not Custom Data
    plant(0x330, 0, "bad\x01name.dds");      // a non-printable character
    plant(0x440, 1, "");                     // empty
    // unaligned: "?.dds" inside float bytes, as 0320's two false hits
    b[0x561] = '?'; b[0x562] = '.'; b[0x563] = 'd'; b[0x564] = 'd'; b[0x565] = 's'; b[0x566] = 0;
    // a name field without its NUL in 256 bytes
    put_u32(b, 0x670, 1);
    std::fill(b.begin() + 0x674, b.begin() + 0x674 + kPsbNameSize, (uint8_t)'a');
    put_text(b, 0x674 + kPsbNameSize - 4, ".dds");
    // a real slot whose padding holds another aligned slot-like pair: the padding is not scanned again
    plant(0x800, 1, "outer.dds");
    put_u32(b, 0x810, 1);
    put_text(b, 0x814, "inner.dds");
    // a slot whose 256-byte field would cross the block end: not a slot
    plant(0x1000 - 0x100, 1, "edge.dds");
    // the last place where a field fits exactly
    Bytes b2 = make_block({"FXP_Edge", 0x400, {{0x400 - 0x104, 1, "fits.dds"}}});
    auto sl = find_psb_slots(b.data(), b.size());
    CHECK_EQ(sl.size(), (size_t)1);
    if (sl.size() == 1) {
        CHECK_EQ(sl[0].offset, 0x800u);
        CHECK_EQ(sl[0].bare, std::string("outer.dds"));
    }
    auto sl2 = find_psb_slots(b2.data(), b2.size());
    CHECK_EQ(sl2.size(), (size_t)1);
    // flag 1 with "Custom Data": classified by its text, flag kept
    Bytes b3 = make_block({"FXP_C", 0x300, {{0x120, 1, "Custom Data"}}});
    auto sl3 = find_psb_slots(b3.data(), b3.size());
    CHECK(sl3.size() == 1 && sl3[0].kind == PsbSlotKind::CustomData && sl3[0].flag == 1);
    CHECK(find_psb_slots(nullptr, 0x1000).empty());
    CHECK(find_psb_slots(b.data(), 0x100).empty());
}

void expect_refusal(const Bytes& f, Err code, const char* what) {
    PsbFile p;
    p.version = 7;
    p.emitters.resize(3);
    Error e;
    bool ok = parse_psb(f, p, &e);
    CHECK(!ok);
    if (ok || e.code != code)
        std::printf("  case %s: ok=%d code %s (%s)\n", what, (int)ok, err_name(e.code), e.what.c_str());
    CHECK(e.code == code);
    CHECK(p.emitters.empty() && p.version == 0);   // cleared
    CHECK(!e.what.empty());
}

void test_refusals() {
    Bytes ba = make_block(block_a()), bb = make_block(block_b());
    const Bytes good = make_psb({ba, bb});

    // Every truncation of a good file: Truncated, never a crash or a read past the end (ASan).
    int trunc_ok = 0;
    for (size_t n = 0; n < good.size(); ++n) {
        Bytes t(good.begin(), good.begin() + n);
        PsbFile p;
        Error e;
        if (!parse_psb(t, p, &e) && e.code == Err::Truncated) ++trunc_ok;
    }
    CHECK_EQ(trunc_ok, (int)good.size());
    // A truncated file whose header was rewritten to match: the chain runs past the end.
    {
        Bytes t(good.begin(), good.end() - 0x10);
        put_u32(t, 8, (uint32_t)(t.size() - 0x40));
        expect_refusal(t, Err::Truncated, "chain past the end");
    }
    // A tail shorter than a block's fixed part after the last block.
    {
        Bytes t = good;
        t.insert(t.end(), 0x20, 0);
        put_u32(t, 8, (uint32_t)(t.size() - 0x40));
        expect_refusal(t, Err::Truncated, "short tail");
    }
    {
        Bytes t = good;
        t[0] = 'X';
        expect_refusal(t, Err::BadMagic, "magic");
    }
    {
        Bytes t = good;
        put_u32(t, 4, 99);
        expect_refusal(t, Err::BadHeader, "version");
    }
    {
        Bytes t = good;
        put_u32(t, 12, 0x44);
        expect_refusal(t, Err::BadHeader, "header size");
    }
    {
        Bytes t = good;   // bytes after the declared payload
        t.push_back(0);
        expect_refusal(t, Err::BadHeader, "payload smaller than the file");
    }
    {
        Bytes t(good.begin(), good.begin() + 0x40);
        put_u32(t, 8, 0);
        expect_refusal(t, Err::BadHeader, "no block");
    }
    // Bad chains: a block size of 0 (would never advance), below the fixed part, not a multiple of 4, too large.
    const uint32_t bad_sizes[] = {0, 0x104, 0x5D2, 0x7FFFFFFF, 0xFFFFFFFF};
    const Err bad_codes[] = {Err::BadHeader, Err::BadHeader, Err::BadHeader, Err::BadHeader, Err::BadHeader};
    for (size_t i = 0; i < sizeof(bad_sizes) / sizeof(bad_sizes[0]); ++i) {
        Bytes t = good;
        put_u32(t, 0x40 + 0x100, bad_sizes[i]);
        // 0x7FFFFFFF and 0xFFFFFFFF are not multiples of 4: BadHeader before the length check
        expect_refusal(t, bad_codes[i], "bad block size");
    }
    {
        Bytes t = good;   // a size that skips past the end of the file
        put_u32(t, 0x40 + 0x100, kA + kB + 4);
        expect_refusal(t, Err::Truncated, "block past the end");
    }
    {
        Bytes t = good;   // the second block's size points into the middle: the chain lands short of the end
        put_u32(t, 0x40 + kA + 0x100, 0x400);
        expect_refusal(t, Err::Truncated, "chain lands short");
    }
    {
        Bytes t = good;
        std::fill(t.begin() + 0x40, t.begin() + 0x40 + 0x100, (uint8_t)'n');
        expect_refusal(t, Err::BadHeader, "name without NUL");
    }
    {
        // Budget: more blocks than kPsbMaxBlocks (each the smallest legal block).
        std::vector<Bytes> many(kPsbMaxBlocks + 1, make_block({"b", kPsbBlockFixed, {}}));
        expect_refusal(make_psb(many), Err::ReadBudget, "too many blocks");
        std::vector<Bytes> limit(kPsbMaxBlocks, make_block({"b", kPsbBlockFixed, {}}));
        PsbFile p;
        Error e;
        CHECK(parse_psb(make_psb(limit), p, &e));
        CHECK_EQ(p.emitters.size(), kPsbMaxBlocks);
    }
    {
        PsbFile p;
        Error e;
        CHECK(!parse_psb(good.data(), kPsbMaxFileSize + 1, p, &e));   // refused before any byte is read
        CHECK(e.code == Err::ReadBudget);
        CHECK(!parse_psb(nullptr, 0x100, p, &e));
        CHECK(e.code == Err::Truncated);
    }
}

// Deterministic mutations of a good file: no crash, no read outside the buffer (ASan); an accepted file keeps the
// chain invariant.
void test_mutations() {
    Bytes good = make_psb({make_block(block_a()), make_block(block_b()), make_block(block_a())});
    uint64_t x = 0x4787;
    auto rnd = [&]() {
        x = x * 6364136223846793005ull + 1442695040888963407ull;
        return (uint32_t)(x >> 33);
    };
    int accepted = 0, refused = 0, invariant_bad = 0;
    for (int it = 0; it < 20000; ++it) {
        Bytes t = good;
        int flips = 1 + (int)(rnd() % 8);
        for (int k = 0; k < flips; ++k) {
            size_t at = rnd() % t.size();
            // aim a third of the flips at the size fields of the header and the blocks
            if (rnd() % 3 == 0) {
                const size_t hot[] = {8, 9, 0x40 + 0x100, 0x40 + 0x101, 0x40 + kA + 0x100, 0x40 + kA + 0x102,
                                      0x40 + kA + kB + 0x100};
                at = hot[rnd() % 7];
            }
            t[at] = (uint8_t)rnd();
        }
        if (rnd() % 4 == 0) t.resize(rnd() % (t.size() + 1));
        PsbFile p;
        Error e;
        if (parse_psb(t, p, &e)) {
            ++accepted;
            size_t sum = 0;
            for (auto& em : p.emitters) {
                sum += em.size;
                if (em.raw.size() != em.size) ++invariant_bad;
                for (auto& s : em.slots)
                    if (s.offset + 4 + kPsbNameSize > em.size || (s.offset & 3)) ++invariant_bad;
            }
            if (sum + kPsbHeaderSize != t.size()) ++invariant_bad;
        } else {
            ++refused;
            if (e.code == Err::None || e.what.empty()) ++invariant_bad;
        }
    }
    std::printf("mutations: %d accepted, %d refused\n", accepted, refused);
    CHECK_EQ(invariant_bad, 0);
    CHECK(accepted > 0 && refused > 0);
}

// ---------------------------------------------------------------------------------------------- the install
std::string env(const char* n) {
    const char* v = std::getenv(n);
    return v ? std::string(v) : std::string();
}

// Test-only FileProvider over the install's .fpk archives (asset-pipeline.md 1.1): u32 version, "FPK_", u16 0,
// u32 count at 10, then per entry u32 name length, the name, padding to a multiple of 4 of the length, u32 a,
// u32 b, u32 size, u32 offset; payloads stored raw. The first archive (in the walk's order) holding a name wins.
class FpkProvider : public FileProvider {
public:
    struct Entry {
        std::string archive;   // origin of the archive in the DirectoryProvider ("res:DX9/EffectTextures.fpk")
        std::string name;      // lower-case entry name
        std::wstring path;     // the archive's path
        uint32_t size = 0, offset = 0;
    };
    std::vector<Entry> entries;
    std::unordered_map<std::string, size_t> index;
    int archives = 0;

    bool add_archive(const std::string& origin, const std::wstring& path, std::string* why) {
        FILE* f = _wfopen(path.c_str(), L"rb");
        if (!f) return fail(why, "cannot open " + origin);
        _fseeki64(f, 0, SEEK_END);
        long long fsize = _ftelli64(f);
        _fseeki64(f, 0, SEEK_SET);
        uint8_t h[14];
        if (fsize < 14 || std::fread(h, 1, 14, f) != 14 || std::memcmp(h + 4, "FPK_", 4) != 0) {
            std::fclose(f);
            return fail(why, "not an FPK: " + origin);
        }
        uint32_t count;
        std::memcpy(&count, h + 10, 4);
        // The index: at most count x (4 + 260 + 3 + 16) bytes (names are 1-260 characters, asset-pipeline.md 1.1).
        long long want = std::min<long long>(fsize - 14, (long long)count * 283);
        Bytes ix((size_t)want);
        bool ok = std::fread(ix.data(), 1, ix.size(), f) == ix.size();
        std::fclose(f);
        if (!ok) return fail(why, "short read: " + origin);
        size_t p = 0;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t n;
            if (p + 4 > ix.size()) return fail(why, "index truncated: " + origin);
            std::memcpy(&n, &ix[p], 4);
            p += 4;
            if (n == 0 || n > 260 || n > ix.size() - p) return fail(why, "bad name: " + origin);
            Entry e;
            e.archive = origin;
            e.path = path;
            e.name = ascii_lower(std::string((const char*)&ix[p], n));
            p += n + (4 - n % 4) % 4;
            if (p + 16 > ix.size()) return fail(why, "index truncated: " + origin);
            std::memcpy(&e.size, &ix[p + 8], 4);
            std::memcpy(&e.offset, &ix[p + 12], 4);
            p += 16;
            if ((long long)e.offset + e.size > fsize) return fail(why, "entry past the end: " + origin);
            index.emplace(e.name, entries.size());   // the first holder wins
            entries.push_back(std::move(e));
        }
        ++archives;
        return true;
    }
    bool read_entry(const Entry& e, Bytes& out) const {
        FILE* f = _wfopen(e.path.c_str(), L"rb");
        if (!f) return false;
        out.resize(e.size);
        bool ok = _fseeki64(f, e.offset, SEEK_SET) == 0 && std::fread(out.data(), 1, e.size, f) == e.size;
        std::fclose(f);
        return ok;
    }
    bool read(const std::string& n, Bytes& out, std::string* origin) override {
        auto it = index.find(n);
        if (it == index.end()) return false;
        const Entry& e = entries[it->second];
        if (origin) *origin = e.archive + "/" + e.name;
        return read_entry(e, out);
    }
    bool exists(const std::string& n, std::string* origin) override {
        auto it = index.find(n);
        if (it == index.end()) return false;
        if (origin) *origin = entries[it->second].archive + "/" + n;
        return true;
    }

private:
    static bool fail(std::string* why, const std::string& w) {
        if (why) *why = w;
        return false;
    }
};

std::string hist(const std::map<size_t, size_t>& m) {
    std::string s;
    for (auto& kv : m) s += (s.empty() ? "" : " ") + std::to_string(kv.first) + ":" + std::to_string(kv.second);
    return s;
}

void test_install() {
    std::string inst = env("C5R_GAME_DIR");
    if (inst.empty()) inst = env("C5M_PSB_INSTALL");
    if (inst.empty()) {
        std::printf("SKIP install: C5R_GAME_DIR (or C5M_PSB_INSTALL) is not set\n");
        t::skipped() = true;
        return;
    }
    DirectoryProvider res;
    Error e;
    if (!res.add_root("res", inst + "/Resource", &e)) {
        std::printf("SKIP install: %s\n", e.what.c_str());
        t::skipped() = true;
        return;
    }
    // The archives of Resource/Common, DX9 and DX11 (0320's census, effects-formats.md 13), in the walk's order.
    FpkProvider fp;
    int bad_archives = 0;
    for (auto& en : res.entries()) {
        if (en.name.size() < 4 || en.name.compare(en.name.size() - 4, 4, ".fpk") != 0) continue;
        std::string o = ascii_lower(en.origin);
        if (o.compare(0, 11, "res:common/") != 0 && o.compare(0, 8, "res:dx9/") != 0 && o.compare(0, 9, "res:dx11/") != 0)
            continue;
        std::string why;
        if (!fp.add_archive(en.origin, en.path, &why)) {
            std::printf("archive not read: %s\n", why.c_str());
            ++bad_archives;
        }
    }
    std::printf("archives %d read, %d not read, %zu entries\n", fp.archives, bad_archives, fp.entries.size());
    CHECK_EQ(bad_archives, 0);

    // Every .psb entry (0320 counted files, so a name held by two archives counts twice).
    size_t files = 0, parsed = 0, bytes = 0, emitters = 0;
    std::set<std::string> psb_names;
    std::map<size_t, size_t> per_file, per_emitter_tex, block_sizes, field104, versions, f10, first_tex;
    size_t header_zero = 0, tex_refs = 0, tex_flag1 = 0, tex_flag0 = 0, custom = 0, shader = 0, missing_refs = 0;
    size_t flag1_fits = 0, flag1_total = 0;
    std::map<std::string, size_t> shaders;
    std::map<std::string, std::string> tex_found;   // bare -> origin ("" if missing)
    for (auto& en : fp.entries) {
        if (en.name.size() < 4 || en.name.compare(en.name.size() - 4, 4, ".psb") != 0) continue;
        ++files;
        psb_names.insert(en.name);
        Bytes b;
        if (!fp.read_entry(en, b)) {
            std::printf("FAIL read %s\n", en.name.c_str());
            CHECK(false);
            continue;
        }
        bytes += b.size();
        PsbFile p;
        Error pe;
        if (!parse_psb(b, p, &pe)) {
            std::printf("not parsed: %s: %s %s\n", en.name.c_str(), err_name(pe.code), pe.what.c_str());
            continue;
        }
        ++parsed;
        ++versions[p.version];
        ++f10[p.field_10];
        if (p.header_zero) ++header_zero;
        ++per_file[p.emitters.size()];
        missing_refs += resolve_psb_textures(p, fp);
        for (auto& em : p.emitters) {
            ++emitters;
            ++block_sizes[em.size];
            ++field104[em.field_104];
            ++per_emitter_tex[em.texture_count()];
            bool first = true;
            for (auto& s : em.slots) {
                if (s.kind == PsbSlotKind::CustomData) {
                    ++custom;
                } else if (s.kind == PsbSlotKind::Shader) {
                    ++shader;
                    ++shaders[s.bare];
                } else {
                    ++tex_refs;
                    (s.flag ? tex_flag1 : tex_flag0)++;
                    tex_found[s.bare] = s.found ? s.origin : std::string();
                    if (first && s.flag) ++first_tex[s.offset];
                    if (s.flag) first = false;
                }
                if (s.flag) {
                    ++flag1_total;
                    if (s.offset + kPsbSlotSize <= em.size) ++flag1_fits;
                }
            }
        }
    }
    size_t found = 0;
    std::vector<std::string> missing;
    std::map<std::string, size_t> found_by_archive;
    for (auto& kv : tex_found) {
        if (kv.second.empty()) {
            missing.push_back(kv.first);
        } else {
            ++found;
            ++found_by_archive[kv.second.substr(0, kv.second.rfind('/'))];
        }
    }
    std::printf("psb files %zu (distinct names %zu), parsed %zu, bytes %zu\n", files, psb_names.size(), parsed, bytes);
    std::printf("version %s; header 0x10 %s; header zero to 0x40 in %zu\n", hist(versions).c_str(), hist(f10).c_str(),
                header_zero);
    std::printf("emitters per file: %s\n", hist(per_file).c_str());
    std::printf("emitters %zu; distinct block sizes %zu; 1488-byte blocks %zu; +0x104: %s\n", emitters,
                block_sizes.size(), block_sizes[1488], hist(field104).c_str());
    std::printf("texture slots per emitter: %s\n", hist(per_emitter_tex).c_str());
    std::printf("texture references %zu (flag 1: %zu, in the Custom Data slot: %zu); Custom Data slots %zu; shader "
                "slots %zu:",
                tex_refs, tex_flag1, tex_flag0, custom, shader);
    for (auto& kv : shaders) std::printf(" %s %zu", kv.first.c_str(), kv.second);
    std::printf("\nflag-1 slots %zu, of which %zu end inside their block with the 0x120 stride\n", flag1_total,
                flag1_fits);
    std::printf("first flag-1 texture slot offset (top):");
    std::vector<std::pair<size_t, size_t>> ft(first_tex.begin(), first_tex.end());
    std::sort(ft.begin(), ft.end(), [](const std::pair<size_t, size_t>& a, const std::pair<size_t, size_t>& b) {
        return a.second > b.second;
    });
    for (size_t i = 0; i < ft.size() && i < 4; ++i) std::printf(" 0x%zx:%zu", ft[i].first, ft[i].second);
    std::printf("\ndistinct texture names %zu: found by bare name %zu, missing %zu (references not found %zu)\n",
                tex_found.size(), found, missing.size(), missing_refs);
    std::printf("found per archive:");
    for (auto& kv : found_by_archive) std::printf(" %s %zu", kv.first.c_str(), kv.second);
    std::printf("\nmissing:");
    for (auto& m : missing) std::printf(" %s", m.c_str());
    std::printf("\n");

    // effects-formats.md 4 (0320) and 16: 384 files parse to their exact end, version 100, field 16, 1-18 blocks,
    // 1,232 emitters, 49 block sizes (1,488 bytes x 746), +0x104 = 272 everywhere.
    CHECK_EQ(files, (size_t)384);
    CHECK_EQ(parsed, files);
    CHECK_EQ(bytes, (size_t)2772208);
    CHECK_EQ(versions[100], parsed);
    CHECK_EQ(f10[16], parsed);
    CHECK_EQ(header_zero, parsed);
    const std::map<size_t, size_t> want_per_file = {{1, 159}, {2, 53}, {3, 43}, {4, 40}, {5, 22},
                                                    {6, 18},  {7, 11}, {8, 14}, {9, 6},  {10, 8},
                                                    {11, 2},  {12, 3}, {13, 2}, {17, 1}, {18, 2}};
    CHECK(per_file == want_per_file);
    CHECK_EQ(emitters, (size_t)1232);
    CHECK_EQ(block_sizes.size(), (size_t)49);
    CHECK_EQ(block_sizes[1488], (size_t)746);
    // Smallest 1,488, largest 61,808 (effects-formats.md 4 said 19,632, corrected 2026-11-23 in 16.1).
    std::printf("block size smallest %zu, largest %zu\n", block_sizes.begin()->first, block_sizes.rbegin()->first);
    CHECK(block_sizes.begin()->first == 1488 && block_sizes.rbegin()->first == 61808);
    CHECK(field104.size() == 1 && field104[272] == 1232);
    // Texture slots per emitter: 0320's histogram with its one 10-reference emitter at 8 (its two '?.dds' hits are
    // float bytes, not slots: effects-formats.md 16.2).
    const std::map<size_t, size_t> want_tex = {{0, 3},  {1, 776}, {2, 314}, {3, 50},  {4, 28},  {5, 8},  {6, 14},
                                               {7, 2},  {8, 6},   {9, 6},   {13, 2},  {14, 2},  {16, 3}, {24, 2},
                                               {55, 1}, {60, 1},  {64, 12}, {77, 1},  {84, 1}};
    CHECK(per_emitter_tex == want_tex);
    CHECK_EQ(tex_refs, (size_t)3100);
    CHECK_EQ(tex_flag1, (size_t)2750);
    CHECK_EQ(tex_flag0, (size_t)350);
    CHECK_EQ(custom, (size_t)1208);
    CHECK_EQ(shader, (size_t)354);
    CHECK(shaders["fakevolume.txt"] == 350 && shaders["brightness.txt"] == 3 && shaders["mask.txt"] == 1);
    CHECK_EQ(flag1_fits, flag1_total);
    // 0320: 123 distinct names, 112 found; without '?.dds' 122 and 112.
    CHECK_EQ(tex_found.size(), (size_t)122);
    CHECK_EQ(found, (size_t)112);
    CHECK_EQ(missing.size(), (size_t)10);
    // ... in the archives 0320 named: EffectTextures 106, Expansion1 Effects 2, Expansion2 Effects 1, ConquestDeluxe 1,
    // Polynesia 1, Misc 1 (all in Resource/DX9 on this install).
    CHECK(found_by_archive["res:DX9/EffectTextures.fpk"] == 106 &&
          found_by_archive["res:DX9/Expansion1EffectsTextures.fpk"] == 2 &&
          found_by_archive["res:DX9/Expansion2EffectsTextures.fpk"] == 1 &&
          found_by_archive["res:DX9/ConquestDeluxeTextures.fpk"] == 1 &&
          found_by_archive["res:DX9/PolynesiaTextures.fpk"] == 1 && found_by_archive["res:DX9/MiscTextures.fpk"] == 1);
}

}  // namespace

int main() {
    test_valid();
    test_slot_rule();
    test_refusals();
    test_mutations();
    test_install();
    return t::finish();
}
