// test_art.cpp - L0 of the unit module (issue 4352): art row rules (cultural and era variation), member resolution,
// team colour, and every refusal with its status code. Synthetic tables and files only.
#include <cmath>
#include <cstdint>
#include <cstring>

#include "c5r/units/art.h"
#include "units_testkit.h"

using namespace c5r::units;
using units_test::tsv;

namespace {

const char* kFxs = "<?xml version=\"1.0\"?><Asset><Mesh file=\"Warrior.gr2\"/>"
                   "<Animation file=\"Warrior_Idle.gr2\" ec=\"1000\"/><Texture file=\"Warrior.dds\"/></Asset>";

struct Fixture {
    c5m::TsvTables db;
    units_test::MemFiles files;
    Fixture() {
        c5m::Error e;
        db.add("Units", tsv({{"Type", "UnitArtInfo", "UnitArtInfoCulturalVariation", "UnitArtInfoEraVariation"},
                             {"UNIT_WARRIOR", "ART_DEF_UNIT_WARRIOR", "0", "0"},
                             {"UNIT_SETTLER", "ART_DEF_UNIT__SETTLER", "1", "0"},
                             {"UNIT_WORKER", "ART_DEF_UNIT__WORKER", "0", "1"},
                             {"UNIT_MERCHANT", "ART_DEF_UNIT_MERCHANT", "0", "1"},
                             {"UNIT_GHOST", "ART_DEF_UNIT_GHOST", "0", "0"},
                             {"UNIT_EMPTY", "ART_DEF_UNIT_EMPTY", "0", "0"},
                             {"UNIT_NOFILE", "ART_DEF_UNIT_NOFILE", "0", "0"},
                             {"UNIT_BADXML", "ART_DEF_UNIT_BADXML", "0", "0"},
                             {"UNIT_NOMESH", "ART_DEF_UNIT_NOMESH", "0", "0"},
                             {"UNIT_NOART", "\\N", "0", "0"}}),
               &e);
        db.add("ArtDefine_UnitInfos",
               tsv({{"Type", "Formation", "DamageStates"},
                    {"ART_DEF_UNIT_WARRIOR", "Warrior", "1"},
                    {"ART_DEF_UNIT__SETTLER", "\\N", "1"},
                    {"ART_DEF_UNIT__SETTLER_AFRI", "\\N", "1"},
                    {"ART_DEF_UNIT__WORKER", "\\N", "1"},
                    {"ART_DEF_UNIT__WORKER_MODERN", "\\N", "1"},
                    {"ART_DEF_UNIT_MERCHANT", "\\N", "1"},
                    {"ART_DEF_UNIT_MERCHANT_LATE", "\\N", "1"},
                    {"ART_DEF_UNIT_EMPTY", "\\N", "1"},
                    {"ART_DEF_UNIT_NOFILE", "\\N", "1"},
                    {"ART_DEF_UNIT_BADXML", "\\N", "1"},
                    {"ART_DEF_UNIT_NOMESH", "\\N", "1"}}),
               &e);
        std::vector<std::vector<std::string>> im = {{"UnitInfoType", "UnitMemberInfoType", "NumMembers"},
                                                    {"ART_DEF_UNIT_WARRIOR", "M_WARRIOR_V2", "3"},
                                                    {"ART_DEF_UNIT_WARRIOR", "M_WARRIOR", "6"},
                                                    {"ART_DEF_UNIT_NOFILE", "M_NOFILE", "1"},
                                                    {"ART_DEF_UNIT_BADXML", "M_BADXML", "1"},
                                                    {"ART_DEF_UNIT_NOMESH", "M_NOMESH", "1"}};
        for (const char* r : {"ART_DEF_UNIT__SETTLER", "ART_DEF_UNIT__SETTLER_AFRI", "ART_DEF_UNIT__WORKER",
                              "ART_DEF_UNIT__WORKER_MODERN", "ART_DEF_UNIT_MERCHANT", "ART_DEF_UNIT_MERCHANT_LATE"})
            im.push_back({r, "M_WARRIOR", "1"});
        db.add("ArtDefine_UnitInfoMemberInfos", tsv(im), &e);
        db.add("ArtDefine_UnitMemberInfos",
               tsv({{"Type", "Scale", "ZOffset", "Domain", "Model", "MaterialTypeTag"},
                    {"M_WARRIOR_V2", "0.14", "\\N", "", "Assets/Units/Warrior/Warrior.fxsxml", "CLOTH"},
                    {"M_WARRIOR", "0.14", "1.5", "", "Warrior.fxsxml", "CLOTH"},
                    {"M_NOFILE", "0.1", "0", "", "Missing.fxsxml", ""},
                    {"M_BADXML", "0.1", "0", "", "Bad.fxsxml", ""},
                    {"M_NOMESH", "0.1", "0", "", "NoMesh.fxsxml", ""}}),
               &e);
        db.add("Eras", tsv({{"Type", "ArtPrefix"},
                            {"ERA_ANCIENT", "ANCIENT"},
                            {"ERA_RENAISSANCE", "RENAISSANCE"},
                            {"ERA_INDUSTRIAL", "INDUSTRIAL"},
                            {"ERA_MODERN", "MODERN"},
                            {"ERA_POSTMODERN", "POSTMODERN"}}),
               &e);
        db.add("Civilizations", tsv({{"Type", "ArtStyleType", "ArtStyleSuffix"},
                                     {"CIVILIZATION_ARABIA", "ARTSTYLE_MIDDLE_EAST", "_AFRI"},
                                     {"CIVILIZATION_PERSIA", "ARTSTYLE_MIDDLE_EAST", "_MED"},
                                     {"CIVILIZATION_ENGLAND", "ARTSTYLE_EUROPEAN", "_EURO"}}),
               &e);
        db.add("MinorCivilizations", tsv({{"Type", "ArtStyleType", "ArtStyleSuffix"},
                                          {"MINOR_CIV_MSOURA", "ARTSTYLE_MIDDLE_EAST", "_AFRI"}}),
               &e);
        db.add("PlayerColors", tsv({{"Type", "SecondaryColor"},
                                    {"PLAYERCOLOR_POLYNESIA", "COLOR_P"},
                                    {"PLAYERCOLOR_MOROCCO", "COLOR_M"},
                                    {"PLAYERCOLOR_DANGLING", "COLOR_NONE"},
                                    {"PLAYERCOLOR_T4453", "COLOR_T4453"},
                                    {"PLAYERCOLOR_T4453B", "COLOR_T4453B"}}),
               &e);
        db.add("Colors", tsv({{"Type", "Red", "Green", "Blue", "Alpha"},
                              {"COLOR_P", "0.851", "0.349", "0.0", "1.0"},
                              {"COLOR_M", "0.831372549", "0.6862745098", "0.2156862745", "1.0"},
                              // synthetic (issue 4453): bytes 57, 118, 229 and 70, 128, 52, the channel bytes of the
                              // live capture of 2026-11-16 (unit-draws.md 5.1); the values are (byte + fraction) / 255
                              {"COLOR_T4453", "0.2254901961", "0.4647058824", "0.8996078431", "1.0"},
                              {"COLOR_T4453B", "0.2756862745", "0.5027450980", "0.2062745098", "1.0"}}),
               &e);
        files.files["warrior.fxsxml"] = kFxs;
        files.files["bad.fxsxml"] = "<Asset><Mesh file=\"x.gr2\">";
        files.files["nomesh.fxsxml"] = "<?xml version=\"1.0\"?><Asset><Mesh file=\"NotThere.gr2\"/></Asset>";
        files.files["warrior.gr2"] = "not read at L0";
    }
};

C5R_TEST(art_row_rules) {
    Fixture f;
    ArtResolver a;
    CHECK_UNIT(a.open(f.db, f.files), UnitCode::Ok);
    std::string row;
    CHECK_UNIT(a.art_row("UNIT_WARRIOR", "", "", row), UnitCode::Ok);
    CHECK_EQ(row, std::string("ART_DEF_UNIT_WARRIOR"));
    // cultural: a suffix, a civilization Type, a minor civ Type, a single-suffix style
    CHECK_UNIT(a.art_row("UNIT_SETTLER", "_AFRI", "", row), UnitCode::Ok);
    CHECK_EQ(row, std::string("ART_DEF_UNIT__SETTLER_AFRI"));
    CHECK_UNIT(a.art_row("UNIT_SETTLER", "CIVILIZATION_ARABIA", "ERA_ANCIENT", row), UnitCode::Ok);
    CHECK_EQ(row, std::string("ART_DEF_UNIT__SETTLER_AFRI"));
    CHECK_UNIT(a.art_row("UNIT_SETTLER", "MINOR_CIV_MSOURA", "", row), UnitCode::Ok);
    CHECK_EQ(row, std::string("ART_DEF_UNIT__SETTLER_AFRI"));
    CHECK_UNIT(a.art_row("UNIT_SETTLER", "ARTSTYLE_EUROPEAN", "", row), UnitCode::Ok);   // _EURO: no row -> base
    CHECK_EQ(row, std::string("ART_DEF_UNIT__SETTLER"));
    CHECK_UNIT(a.art_row("UNIT_SETTLER", "CIVILIZATION_PERSIA", "", row), UnitCode::Ok);
    CHECK_EQ(row, std::string("ART_DEF_UNIT__SETTLER"));
    // a non-cultural type ignores the style
    CHECK_UNIT(a.art_row("UNIT_WARRIOR", "_AFRI", "", row), UnitCode::Ok);
    CHECK_EQ(row, std::string("ART_DEF_UNIT_WARRIOR"));
    // era: own era, an earlier era, _LATE from the Industrial era on, else the base
    CHECK_UNIT(a.art_row("UNIT_WORKER", "", "ERA_MODERN", row), UnitCode::Ok);
    CHECK_EQ(row, std::string("ART_DEF_UNIT__WORKER_MODERN"));
    CHECK_UNIT(a.art_row("UNIT_WORKER", "", "ERA_POSTMODERN", row), UnitCode::Ok);
    CHECK_EQ(row, std::string("ART_DEF_UNIT__WORKER_MODERN"));
    CHECK_UNIT(a.art_row("UNIT_WORKER", "", "ERA_INDUSTRIAL", row), UnitCode::Ok);
    CHECK_EQ(row, std::string("ART_DEF_UNIT__WORKER"));
    CHECK_UNIT(a.art_row("UNIT_MERCHANT", "", "ERA_INDUSTRIAL", row), UnitCode::Ok);
    CHECK_EQ(row, std::string("ART_DEF_UNIT_MERCHANT_LATE"));
    CHECK_UNIT(a.art_row("UNIT_MERCHANT", "", "ERA_RENAISSANCE", row), UnitCode::Ok);
    CHECK_EQ(row, std::string("ART_DEF_UNIT_MERCHANT"));
    CHECK_UNIT(a.art_row("UNIT_MERCHANT", "", "", row), UnitCode::Ok);
    CHECK_EQ(row, std::string("ART_DEF_UNIT_MERCHANT"));
    // lists for the pre-check
    CHECK_EQ(a.unit_types().size(), static_cast<size_t>(10));
    CHECK_EQ(a.eras().size(), static_cast<size_t>(5));
    CHECK_EQ(a.style_suffixes().size(), static_cast<size_t>(4));   // "", _AFRI, _EURO, _MED
}

C5R_TEST(art_row_refusals) {
    Fixture f;
    ArtResolver a;
    CHECK_UNIT(a.open(f.db, f.files), UnitCode::Ok);
    std::string row = "x";
    CHECK_UNIT(a.art_row("UNIT_NOPE", "", "", row), UnitCode::NoArtRow);
    CHECK(row.empty());
    CHECK_UNIT(a.art_row("UNIT_NOART", "", "", row), UnitCode::NoArtRow);
    CHECK_UNIT(a.art_row("UNIT_GHOST", "", "", row), UnitCode::NoArtRow);   // UnitArtInfo names no row
    CHECK_UNIT(a.art_row("UNIT_SETTLER", "ARTSTYLE_MIDDLE_EAST", "", row), UnitCode::BadInput);   // two suffixes
    CHECK_UNIT(a.art_row("UNIT_SETTLER", "ARTSTYLE_NOPE", "", row), UnitCode::BadInput);
    CHECK_UNIT(a.art_row("UNIT_WORKER", "", "ERA_NOPE", row), UnitCode::BadInput);
}

C5R_TEST(resolve_members) {
    Fixture f;
    ArtResolver a;
    CHECK_UNIT(a.open(f.db, f.files), UnitCode::Ok);
    UnitArt u;
    CHECK_UNIT(a.resolve("ART_DEF_UNIT_WARRIOR", u), UnitCode::Ok);
    CHECK_EQ(u.formation, std::string("Warrior"));
    CHECK_EQ(u.members.size(), static_cast<size_t>(2));
    CHECK_EQ(u.figures(), 9u);
    CHECK_EQ(u.members[0].member, std::string("M_WARRIOR_V2"));
    CHECK_EQ(u.members[0].count, 3);
    CHECK_EQ(u.members[0].scale, 0.14f);
    CHECK_EQ(u.members[0].zoffset, 0.0f);
    CHECK_EQ(u.members[1].zoffset, 1.5f);
    CHECK(u.members[1].files.mesh.found);
    CHECK_EQ(u.members[1].files.animations.size(), static_cast<size_t>(1));
}

C5R_TEST(resolve_refusals) {
    Fixture f;
    ArtResolver a;
    CHECK_UNIT(a.open(f.db, f.files), UnitCode::Ok);
    UnitArt u;
    CHECK_UNIT(a.resolve("ART_DEF_UNIT_NOPE", u), UnitCode::NoArtRow);
    CHECK_UNIT(a.resolve("ART_DEF_UNIT_EMPTY", u), UnitCode::NoMembers);
    CHECK_UNIT(a.resolve("ART_DEF_UNIT_NOFILE", u), UnitCode::ModelNotFound);
    CHECK_UNIT(a.resolve("ART_DEF_UNIT_BADXML", u), UnitCode::BadModel);
    CHECK_UNIT(a.resolve("ART_DEF_UNIT_NOMESH", u), UnitCode::ModelNotFound);
    CHECK(u.members.empty());
    ArtResolver closed;
    CHECK_UNIT(closed.resolve("ART_DEF_UNIT_WARRIOR", u), UnitCode::NoArtRow);
}

C5R_TEST(team_colour_rows) {
    Fixture f;
    ArtResolver a;
    CHECK_UNIT(a.open(f.db, f.files), UnitCode::Ok);
    float c[4];
    const float inv = 1.0f / 255.0f;   // the row is byte x float32(1/255), a product (4453, unit-draws.md 5.1)
    CHECK_UNIT(a.team_colour("PLAYERCOLOR_POLYNESIA", c), UnitCode::Ok);
    // the capture's row 0 (unit-draws.md 5): bytes 217, 88, 0; alpha 1
    CHECK_EQ(c[0], 217.0f * inv);
    CHECK_EQ(c[1], 88.0f * inv);
    CHECK_EQ(c[2], 0.0f);
    CHECK_EQ(c[3], 1.0f);
    CHECK_UNIT(a.team_colour("PLAYERCOLOR_MOROCCO", c), UnitCode::Ok);
    CHECK_EQ(c[0], 212.0f * inv);   // float32 0.831372549 x 255 rounds to 212.0 before the truncation
    CHECK_EQ(c[1], 175.0f * inv);
    CHECK_EQ(c[2], 55.0f * inv);
    CHECK_UNIT(a.team_colour("PLAYERCOLOR_NOPE", c), UnitCode::BadInput);
    CHECK_UNIT(a.team_colour("PLAYERCOLOR_DANGLING", c), UnitCode::BadInput);
}

// Issue 4453 (2026-11-16, unit-draws.md 5.1): the row is trunc(float32(v) x 255) x float32(1/255), bit-equal to the
// EXE's on 228 captured instances; the division by 255 differs by one ulp for 57, 118, 229 and 52 (and agrees for 70
// and 128), which pins the product.
C5R_TEST(team_colour_reciprocal_rule) {
    Fixture f;
    ArtResolver a;
    CHECK_UNIT(a.open(f.db, f.files), UnitCode::Ok);
    const float inv = 1.0f / 255.0f;
    float c[4];
    CHECK_UNIT(a.team_colour("PLAYERCOLOR_T4453", c), UnitCode::Ok);
    const float want[3] = {57.0f * inv, 118.0f * inv, 229.0f * inv};
    for (int i = 0; i < 3; ++i) {
        uint32_t got = 0, exp = 0;
        std::memcpy(&got, &c[i], 4);
        std::memcpy(&exp, &want[i], 4);
        CHECK_EQ(got, exp);
    }
    CHECK(c[0] != 57.0f / 255.0f);    // the division is one ulp off for these bytes
    CHECK(c[1] != 118.0f / 255.0f);
    CHECK(c[2] != 229.0f / 255.0f);
    CHECK_EQ(c[3], 1.0f);
    CHECK_UNIT(a.team_colour("PLAYERCOLOR_T4453B", c), UnitCode::Ok);
    CHECK_EQ(c[0], 70.0f * inv);
    CHECK_EQ(c[1], 128.0f * inv);
    CHECK_EQ(c[2], 52.0f * inv);
    CHECK(c[0] == 70.0f / 255.0f && c[1] == 128.0f / 255.0f);   // both rules agree on these two
    CHECK(c[2] != 52.0f / 255.0f);
}

// ---- issue 4464: the cache per art row ----------------------------------------------------------------------------
// MemFiles that counts every file access (read, and exists through the base's read).
class CountingFiles : public units_test::MemFiles {
public:
    int reads = 0;
    bool read(const std::string& name, c5m::Bytes& out, std::string* origin) override {
        ++reads;
        return MemFiles::read(name, out, origin);
    }
};

const char* const kRows[] = {"ART_DEF_UNIT_WARRIOR", "ART_DEF_UNIT__SETTLER", "ART_DEF_UNIT_MERCHANT_LATE",
                             "ART_DEF_UNIT_NOPE",    "ART_DEF_UNIT_EMPTY",    "ART_DEF_UNIT_NOFILE",
                             "ART_DEF_UNIT_BADXML",  "ART_DEF_UNIT_NOMESH"};
// .fxsxml parses of one resolution per row above (members reached: a failure stops at its member)
const uint64_t kParses[] = {2, 1, 1, 0, 0, 1, 1, 1};

C5R_TEST(cache_equals_rule_and_parses_once) {
    Fixture f;
    CountingFiles files;
    files.files = f.files.files;
    ArtResolver a, ref;
    CHECK_UNIT(a.open(f.db, files), UnitCode::Ok);
    CHECK_UNIT(ref.open(f.db, f.files), UnitCode::Ok);
    uint64_t want = 0;
    for (size_t i = 0; i < sizeof kRows / sizeof kRows[0]; ++i) want += kParses[i];
    int readsAfterFirst = -1;
    for (int frame = 0; frame < 50; ++frame) {
        for (const char* row : kRows) {
            UnitArt got, exp;
            got.unit_type = "UNIT_LEFT_OVER";   // resolve() overwrites the whole object
            const UnitStatus gs = a.resolve(row, got);
            const UnitStatus es = ref.resolve_uncached(row, exp);
            CHECK(gs.code == es.code);
            CHECK_EQ(gs.str(), es.str());
            CHECK(gs.warnings == es.warnings);
            const std::string d = units_test::artDiff(got, exp);
            if (!d.empty()) ::c5r::test::fail(__FILE__, __LINE__, std::string(row) + ": " + d);
            std::shared_ptr<const UnitArt> s1, s2;
            CHECK_EQ(a.resolve_shared(row, s1).str(), es.str());
            CHECK_EQ(a.resolve_shared(row, s2).str(), es.str());
            CHECK(s1 && s1.get() == s2.get());   // one object per row, shared
            CHECK(units_test::artDiff(*s1, exp).empty());
        }
        if (frame == 0) readsAfterFirst = files.reads;
        CHECK_EQ(a.fxsxml_parses(), want);   // one parse per member per row, over every frame
    }
    CHECK(readsAfterFirst > 0);
    CHECK_EQ(files.reads, readsAfterFirst);   // no file read after the first resolution of each row
    CHECK_EQ(a.cached_rows(), sizeof kRows / sizeof kRows[0]);
    CHECK_EQ(ref.fxsxml_parses(), 50 * want);   // the rule parses every time
    CHECK_EQ(ref.cached_rows(), static_cast<size_t>(0));
}

// Invalidation: what the key names can change only by a new open() (a load: other tables or files) or a caller's
// clear_cache() (files changed under the same tables); a held art stays valid across both.
C5R_TEST(cache_invalidated_by_open_and_clear) {
    Fixture f;
    ArtResolver a;
    CHECK_UNIT(a.open(f.db, f.files), UnitCode::Ok);
    std::shared_ptr<const UnitArt> held;
    CHECK_UNIT(a.resolve_shared("ART_DEF_UNIT_NOFILE", held), UnitCode::ModelNotFound);
    CHECK_EQ(a.fxsxml_parses(), static_cast<uint64_t>(1));
    // the file appears: the cache still answers ModelNotFound (no read) until cleared
    f.files.files["missing.fxsxml"] = kFxs;
    UnitArt u;
    CHECK_UNIT(a.resolve("ART_DEF_UNIT_NOFILE", u), UnitCode::ModelNotFound);
    CHECK_EQ(a.fxsxml_parses(), static_cast<uint64_t>(1));
    a.clear_cache();
    CHECK_EQ(a.cached_rows(), static_cast<size_t>(0));
    CHECK_UNIT(a.resolve("ART_DEF_UNIT_NOFILE", u), UnitCode::Ok);
    CHECK_EQ(a.fxsxml_parses(), static_cast<uint64_t>(2));
    CHECK_EQ(u.members.size(), static_cast<size_t>(1));
    CHECK(held && held->members.empty() && held->art_row == "ART_DEF_UNIT_NOFILE");   // the old art, still alive
    // a database reload (open) with the member's model changed: the new tables' result, parsed again
    std::shared_ptr<const UnitArt> before;
    CHECK_UNIT(a.resolve_shared("ART_DEF_UNIT_WARRIOR", before), UnitCode::Ok);
    CHECK_EQ(before->members[0].model, std::string("Assets/Units/Warrior/Warrior.fxsxml"));
    Fixture g;
    g.db.add("ArtDefine_UnitMemberInfos",
             tsv({{"Type", "Scale", "ZOffset", "Domain", "Model", "MaterialTypeTag"},
                  {"M_WARRIOR_V2", "0.25", "\\N", "", "NoMesh.fxsxml", "CLOTH"},
                  {"M_WARRIOR", "0.14", "1.5", "", "Warrior.fxsxml", "CLOTH"}}),
             nullptr);
    CHECK_UNIT(a.open(g.db, g.files), UnitCode::Ok);
    CHECK_EQ(a.fxsxml_parses(), static_cast<uint64_t>(0));
    CHECK_EQ(a.cached_rows(), static_cast<size_t>(0));
    CHECK_UNIT(a.resolve("ART_DEF_UNIT_WARRIOR", u), UnitCode::ModelNotFound);   // NoMesh.fxsxml names no mesh
    CHECK_EQ(a.fxsxml_parses(), static_cast<uint64_t>(1));
    CHECK_EQ(before->members[0].scale, 0.14f);   // the art of the old database, still alive
}

C5R_TEST(open_refuses_missing_table) {
    c5m::TsvTables db;
    units_test::MemFiles files;
    ArtResolver a;
    CHECK_UNIT(a.open(db, files), UnitCode::BadInput);
}

}  // namespace

C5R_TEST_MAIN
