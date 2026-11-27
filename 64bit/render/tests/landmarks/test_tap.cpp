// test_tap.cpp - ticket 4873: the tap frame's file form (.mtap = 4874's frame image), the wire key and texture
// identity, resolve() and the scene file form .c5models, on synthetic data only (no game file). A negative test for
// every check of the readers (COMMON 9).
#include <cstring>
#include <vector>

#include "c5r/gamefs/gamefiles.h"
#include "c5r/landmarks/tap.h"
#include "shell_model_scene.h"
#include "shell_model_tap.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::landmarks;

namespace {

TapFrame sample() {
    TapFrame f;
    f.frameSeq = 7;
    f.presentCount = 99;
    f.latchedBits = 6;
    f.wantBits = 6;
    f.targets.push_back(TapTarget{2554, 1411, 4, 28, 2554, 1411, 3});
    f.targets.push_back(TapTarget{1024, 1024, 1, 28, 1024, 1024, 1});
    // rows: 2 instances of a block (8 rows), one single (4), one skinned instance (96), one cb1 (22 rows)
    f.rows.resize(8 + 4 + 96 + 22);
    for (size_t i = 0; i < f.rows.size(); ++i) f.rows[i] = Float4{float(i), float(i) + 0.25f, float(i) + 0.5f, 1.0f};
    f.consts.push_back(TapConst{108, 22, 1, 3});
    TapDraw a;
    a.material = Material::Blocks;
    a.key = WireKey{1, 2, 3, 4, 10, 30, 5, 0};
    a.instanceCount = 2;
    a.instRow = 0;
    a.instRows = 4;
    a.vsCb1 = a.psCb1 = 0;
    a.flags = VPMTAP_DRAW_DROPPED;
    a.env[0] = 11;
    a.env[1] = 12;
    a.env[2] = 13;
    TapDraw b = a;
    b.material = Material::Single;
    b.instanceCount = 1;
    b.instRow = 8;
    b.target = 1;
    b.order = 1;
    TapDraw c = a;
    c.material = Material::Skinned;
    c.instanceCount = 1;
    c.instRow = 12;
    c.instRows = 96;
    c.skinId0 = 5;
    c.order = 2;
    c.psCb1 = kNoConst;
    f.draws = {a, b, c};
    return f;
}

std::vector<uint8_t> image(const TapFrame& f) {
    std::vector<uint8_t> b;
    CHECK_OK(writeTap(f, b));
    return b;
}

VPModelTapFrame& hdr(std::vector<uint8_t>& b) { return *reinterpret_cast<VPModelTapFrame*>(b.data()); }

void rehash(std::vector<uint8_t>& b) {
    VPModelTapFrame& h = hdr(b);
    h.fnv1a64 = VPModelHash64(b.data() + h.headerBytes, h.imageBytes - h.headerBytes);
}

}  // namespace

C5R_TEST(tap_round_trip) {
    const TapFrame f = sample();
    std::vector<uint8_t> b = image(f);
    CHECK_EQ(b.size() % 16, 0u);
    CHECK_EQ(hdr(b).magic, VPSHELL_MODELTAP_MAGIC);
    TapFrame g;
    CHECK_OK(readTap(b.data(), b.size(), g));
    CHECK_EQ(g.frameSeq, 7u);
    CHECK_EQ(g.presentCount, 99u);
    CHECK_EQ(g.draws.size(), 3u);
    CHECK_EQ(g.rows.size(), f.rows.size());
    CHECK(std::memcmp(g.rows.data(), f.rows.data(), f.rows.size() * sizeof(Float4)) == 0);
    CHECK(g.draws[0].key == f.draws[0].key);
    CHECK(g.draws[1].material == Material::Single);
    CHECK(g.draws[2].material == Material::Skinned);
    CHECK_EQ(g.draws[2].skinId0, 5u);
    CHECK_EQ(g.draws[2].psCb1, kNoConst);
    CHECK_EQ(g.draws[0].env[2], 13u);
    CHECK_EQ(g.targets[1].width, 1024u);
    CHECK_EQ(g.consts[0].uses, 3u);
    // the image is the frame image byte for byte: write(read(write(f))) == write(f)
    CHECK(image(g) == b);
}

C5R_TEST(tap_negative_file_form) {
    std::vector<uint8_t> b = image(sample());
    TapFrame g;
    CHECK_ERR(readTap(nullptr, 0, g), Err::Truncated);
    CHECK_ERR(readTap(b.data(), 4, g), Err::Truncated);
    CHECK_ERR(readTap(b.data(), 64, g), Err::Truncated);            // shorter than the header
    CHECK_ERR(readTap(b.data(), b.size() - 16, g), Err::Truncated);   // the image past the data
    {
        auto c = b;
        hdr(c).magic ^= 1;
        CHECK_ERR(readTap(c.data(), c.size(), g), Err::BadMagic);
    }
    {
        auto c = b;
        hdr(c).version = 2;
        CHECK_ERR(readTap(c.data(), c.size(), g), Err::BadVersion);
    }
    {
        auto c = b;
        hdr(c).headerBytes = 120;
        CHECK_ERR(readTap(c.data(), c.size(), g), Err::BadLayout);
    }
    {
        auto c = b;
        hdr(c).drawBytes = 96;
        CHECK_ERR(readTap(c.data(), c.size(), g), Err::BadLayout);
    }
    {
        auto c = b;
        hdr(c).drawCount = VPSHELL_MODELTAP_MAX_DRAWS + 1;
        CHECK_ERR(readTap(c.data(), c.size(), g), Err::BadLayout);
    }
    {
        auto c = b;
        hdr(c).constCount = 5000;   // counts: past the image and the bound
        CHECK_ERR(readTap(c.data(), c.size(), g), Err::BadLayout);
    }
    {
        auto c = b;
        hdr(c).rowOffset += 8;   // not 16-byte aligned
        CHECK_ERR(readTap(c.data(), c.size(), g), Err::BadLayout);
    }
    {
        auto c = b;
        hdr(c).constOffset = hdr(c).drawOffset;   // overlaps the draws
        CHECK_ERR(readTap(c.data(), c.size(), g), Err::BadLayout);
    }
    {
        auto c = b;
        hdr(c).rowCount += 1;   // rows past the image's end
        CHECK_ERR(readTap(c.data(), c.size(), g), Err::BadLayout);
    }
    {
        auto c = b;
        hdr(c).imageBytes = 64;   // smaller than its header
        CHECK_ERR(readTap(c.data(), c.size(), g), Err::BadLayout);
    }
    {
        auto c = b;
        c[hdr(c).rowOffset + 3] ^= 0x40;   // a byte changed: the hash
        CHECK_ERR(readTap(c.data(), c.size(), g), Err::BadLayout);
    }
    {
        auto c = b;
        reinterpret_cast<VPModelTapDraw*>(c.data() + hdr(c).drawOffset)->cls = 9;   // unknown class
        rehash(c);
        CHECK_ERR(readTap(c.data(), c.size(), g), Err::BadLayout);
    }
    {
        auto c = b;
        reinterpret_cast<VPModelTapDraw*>(c.data() + hdr(c).drawOffset)->instRows = 8;   // total, not per instance
        rehash(c);
        CHECK_ERR(readTap(c.data(), c.size(), g), Err::BadLayout);
        CHECK(g.draws.empty());   // a refused frame leaves nothing behind
    }
}

C5R_TEST(tap_negative_frame_checks) {
    const TapFrame ok = sample();
    CHECK_OK(checkFrame(ok));
    std::vector<uint8_t> out;
    auto bad = [&](void (*mut)(TapFrame&)) {
        TapFrame f = ok;
        mut(f);
        CHECK_ERR(checkFrame(f), Err::BadLayout);
        CHECK_ERR(writeTap(f, out), Err::BadLayout);   // the writer refuses what the reader would refuse
    };
    bad([](TapFrame& f) { f.draws[0].target = 2; });
    bad([](TapFrame& f) { f.draws[0].instanceCount = 0; });
    bad([](TapFrame& f) { f.draws[0].instanceCount = VPSHELL_MODELTAP_MAX_INSTANCES + 1; });
    bad([](TapFrame& f) { f.draws[1].instanceCount = 2; });   // a single model with two instances
    bad([](TapFrame& f) { f.draws[2].instRows = 4; });        // skinned: 96 per instance
    bad([](TapFrame& f) { f.draws[2].instRow = 100; });       // rows past the block
    bad([](TapFrame& f) { f.draws[0].vsCb1 = 1; });
    bad([](TapFrame& f) { f.draws[0].psCb1 = 7; });
    bad([](TapFrame& f) { f.draws[0].key.indexCount = 0; });
    bad([](TapFrame& f) { f.consts[0].firstRow = 120; });     // past the rows
    bad([](TapFrame& f) { f.consts[0].rows = 0; });
    bad([](TapFrame& f) {
        f.rows.resize(400);
        f.consts[0].rows = 257;   // 4,112 bytes: above VPSHELL_MODELTAP_CB_MAX_BYTES
    });
    bad([](TapFrame& f) { f.targets.resize(VPSHELL_MODELTAP_MAX_TARGETS + 1); });
}

C5R_TEST(wire_hashes) {
    const uint16_t ib[5] = {28864, 0, 1, 2, 3};   // the pad index is not hashed
    uint64_t h = 0;
    CHECK_OK(wireIndexHash(ib, sizeof ib, 2, h));
    CHECK_EQ(h, VPModelHash64(ib + 1, 8));
    uint16_t ib2[5] = {3225, 0, 1, 2, 3};
    uint64_t h2 = 0;
    CHECK_OK(wireIndexHash(ib2, sizeof ib2, 2, h2));
    CHECK_EQ(h, h2);
    const uint32_t ib4[3] = {7, 1, 2};
    CHECK_OK(wireIndexHash(ib4, sizeof ib4, 4, h));
    CHECK_EQ(h, VPModelHash64(ib4 + 1, 8));
    CHECK_ERR(wireIndexHash(ib, sizeof ib, 3, h), Err::BadLayout);
    CHECK_ERR(wireIndexHash(ib, 2, 2, h), Err::BadLayout);       // one element only
    CHECK_ERR(wireIndexHash(ib, 9, 2, h), Err::BadLayout);       // not a multiple of the index size
    CHECK_EQ(wireHash(ib, 10), VPModelHash64(ib, 10));
    CHECK(wireHash(ib, 0) != 0);   // 0 is reserved for "none"
}

C5R_TEST(texture_identity) {
    // 256 x 256 RGBA8, 9 levels: the identity is level 2 (64 x 64), its rows tight
    std::vector<std::vector<uint8_t>> lv;
    std::vector<TextureLevel> mips;
    for (uint32_t m = 0; m < 9; ++m) {
        const uint32_t w = 256u >> m;
        lv.emplace_back(static_cast<size_t>(w) * w * 4);
        for (size_t i = 0; i < lv.back().size(); ++i) lv.back()[i] = static_cast<uint8_t>(i * 7 + m);
    }
    for (auto& l : lv) mips.push_back(TextureLevel{l.data(), l.size(), 0});
    const uint64_t id = textureIdentity(28, 256, 256, 9, mips);
    CHECK_EQ(id, VPModelTextureHash(lv[2].data(), 64 * 4, 64, 64, 28));
    CHECK(id != 0);
    CHECK_EQ(textureIdentity(27, 256, 256, 9, mips), id);     // typeless and unorm: the same block size, the same id
    // the top level left out (the texture quality setting): the same level, the same identity
    std::vector<TextureLevel> skipped(mips.begin() + 1, mips.end());
    CHECK_EQ(textureIdentity(28, 128, 128, 8, skipped), id);
    // a padded pitch: the padding is not hashed
    std::vector<uint8_t> padded(64 * 272);
    for (uint32_t y = 0; y < 64; ++y) std::memcpy(&padded[y * 272], &lv[2][y * 256], 256);
    std::vector<TextureLevel> pm = mips;
    pm[2] = TextureLevel{padded.data(), padded.size(), 272};
    CHECK_EQ(textureIdentity(28, 256, 256, 9, pm), id);
    // negatives: a format without a row size, the level missing, the level short, a pitch below the row
    CHECK_EQ(textureIdentity(45, 256, 256, 9, mips), 0u);
    CHECK_EQ(textureIdentity(28, 256, 256, 9, std::vector<TextureLevel>(mips.begin(), mips.begin() + 2)), 0u);
    pm[2] = TextureLevel{lv[2].data(), lv[2].size() - 1, 0};
    CHECK_EQ(textureIdentity(28, 256, 256, 9, pm), 0u);
    pm[2] = TextureLevel{lv[2].data(), lv[2].size(), 100};
    CHECK_EQ(textureIdentity(28, 256, 256, 9, pm), 0u);
    CHECK_EQ(textureIdentity(28, 0, 256, 9, mips), 0u);
    // BC1 (8 bytes per 4 x 4 block): 64 x 64 -> 16 rows of 128 bytes
    std::vector<uint8_t> bc(16 * 128, 0x5A);
    std::vector<TextureLevel> bm(1, TextureLevel{bc.data(), bc.size(), 0});
    CHECK_EQ(textureIdentity(71, 64, 64, 1, bm), VPModelTextureHash(bc.data(), 128, 64, 64, 71));
}

C5R_TEST(materials) {
    Material m;
    CHECK(materialFromClass(VPMODEL_CLASS_BLOCK, m) && m == Material::Blocks);
    CHECK(materialFromClass(VPMODEL_CLASS_SINGLE, m) && m == Material::Single);
    CHECK(materialFromClass(VPMODEL_CLASS_INST, m) && m == Material::Instanced);
    CHECK(materialFromClass(VPMODEL_CLASS_SKINNED, m) && m == Material::Skinned);
    CHECK(!materialFromClass(4, m));
    for (Material x : {Material::Single, Material::Instanced, Material::Blocks, Material::Skinned}) {
        Material y;
        CHECK(materialFromClass(classOf(x), y) && y == x);
    }
    CHECK_EQ(rowsPerInstance(Material::Skinned), 96u);
    CHECK_EQ(rowsPerInstance(Material::Single), 4u);
    CHECK(partMaterial(ModelClass::CityBlock, false) == Material::Blocks);
    CHECK(partMaterial(ModelClass::CityPiece, false) == Material::Single);
    CHECK(partMaterial(ModelClass::Landmark, false) == Material::Instanced);
    CHECK(partMaterial(ModelClass::Landmark, true) == Material::Skinned);
}

C5R_TEST(resolve_unknown_keys) {
    // An index of an unloaded library holds no key: every draw is unknown, counted, and no item is made.
    ModelLibrary lib;
    gamefs::GameFiles files;   // not opened
    WireIndex index(lib, files);
    CHECK_ERR(index.build(), Err::NotFound);
    ModelScene s;
    ResolveReport r;
    CHECK_OK(resolve(index, lib, sample(), s, &r));
    CHECK_EQ(r.draws, 3u);
    CHECK_EQ(r.unknown, 3u);
    CHECK_EQ(s.unknownDraws, 3u);
    CHECK(s.items.empty());
    CHECK_EQ(s.unknown.size(), 3u);
    TapFrame bad = sample();
    bad.draws[0].target = 9;
    CHECK_ERR(resolve(index, lib, bad, s, &r), Err::BadLayout);
}

C5R_TEST(scene_file_negative) {
    ModelLibrary lib;   // empty: no file can be named
    ModelScene s;
    s.frameSeq = 3;
    s.targets.push_back(TapTarget{8, 8, 1, 28, 8, 8, 0});
    s.rows.resize(4);
    s.unknownDraws = 2;
    s.unknown = {0, 1};
    std::vector<uint8_t> b;
    CHECK_OK(writeScene(s, lib, b));
    ModelScene g;
    CHECK_OK(readScene(b.data(), b.size(), lib, g));
    CHECK_EQ(g.frameSeq, 3u);
    CHECK_EQ(g.unknown.size(), 2u);
    CHECK_EQ(g.rows.size(), 4u);
    CHECK_ERR(readScene(b.data(), 2, lib, g), Err::Truncated);
    CHECK_ERR(readScene(b.data(), 40, lib, g), Err::Truncated);
    CHECK_ERR(readScene(b.data(), b.size() - 4, lib, g), Err::Truncated);
    {
        auto c = b;
        c[0] ^= 1;
        CHECK_ERR(readScene(c.data(), c.size(), lib, g), Err::BadMagic);
    }
    {
        auto c = b;
        c[4] = 9;   // version
        CHECK_ERR(readScene(c.data(), c.size(), lib, g), Err::BadVersion);
    }
    {
        auto c = b;
        c[8] = 60;   // header size
        CHECK_ERR(readScene(c.data(), c.size(), lib, g), Err::BadLayout);
    }
    // an item naming a file: the string table, the item, then the checks in order
    auto withItem = [&](uint32_t material, uint32_t target, uint32_t vsCb1, const char* name, uint32_t strLen) {
        std::vector<uint8_t> c(b.begin(), b.begin() + 64);
        uint32_t* h = reinterpret_cast<uint32_t*>(c.data());
        h[6] = 1;                                     // itemCount
        h[7] = 1;                                     // stringCount
        h[8] = 4 + static_cast<uint32_t>(std::strlen(name));   // stringBytes
        const uint32_t len = strLen;
        c.insert(c.end(), reinterpret_cast<const uint8_t*>(&len), reinterpret_cast<const uint8_t*>(&len) + 4);
        c.insert(c.end(), name, name + std::strlen(name));
        uint8_t item[72] = {};
        uint32_t* it = reinterpret_cast<uint32_t*>(item);
        it[0] = 0;          // file
        it[3] = material;
        it[4] = target;
        it[5] = 1;          // instances
        it[6] = 0;          // instRow
        it[7] = vsCb1;
        it[8] = kNoConst;
        c.insert(c.end(), item, item + 72);
        c.insert(c.end(), b.begin() + 64, b.end());   // rows, consts, targets, unknown of the sample
        return c;
    };
    auto c1 = withItem(4, 0, kNoConst, "nosuch.gr2", 10);
    CHECK_ERR(readScene(c1.data(), c1.size(), lib, g), Err::NotFound);
    auto c2 = withItem(7, 0, kNoConst, "nosuch.gr2", 10);
    CHECK_ERR(readScene(c2.data(), c2.size(), lib, g), Err::BadLayout);   // unknown material
    auto c3 = withItem(4, 3, kNoConst, "nosuch.gr2", 10);
    CHECK_ERR(readScene(c3.data(), c3.size(), lib, g), Err::BadLayout);   // target out of range
    auto c4 = withItem(4, 0, 5, "nosuch.gr2", 10);
    CHECK_ERR(readScene(c4.data(), c4.size(), lib, g), Err::BadLayout);   // const out of range
    auto c5 = withItem(4, 0, kNoConst, "nosuch.gr2", 99);
    CHECK_ERR(readScene(c5.data(), c5.size(), lib, g), Err::BadLayout);   // a string past the table
    auto c6 = withItem(6, 0, kNoConst, "nosuch.gr2", 10);
    CHECK_ERR(readScene(c6.data(), c6.size(), lib, g), Err::BadLayout);   // 96 rows past the 4 rows
}

C5R_TEST_MAIN
