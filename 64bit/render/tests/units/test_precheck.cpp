// test_precheck.cpp - the unit module's pre-check (issue 4352; unit-renderer.md 7): every unit type of the database
// with every style and era through L0, L1 and the clip binding (pose.h's bindAll), one entry per reachable art row,
// with the named status of each failing type. Install-backed (C5R_GAME_DIR, C5R_EXE) with the fixture's database.
#include <map>

#include "units_fixture.h"

using namespace c5r::units;

namespace {

C5R_TEST(precheck_per_type) {
    units_test::InstallFixture fx;
    UnitRenderer r;
    CHECK_OK(r.open(nullptr, fx.gf, fx.db, fx.exe));
    std::vector<UnitTypeStatus> out;
    CHECK_OK(r.precheck(out));
    std::map<std::string, UnitTypeStatus> by;
    for (auto& e : out) {
        CHECK(by.count(e.unit_type) == 0);   // one reachable row per type in this database
        by[e.unit_type] = e;
        std::printf("precheck %s %s %s\n", e.unit_type.c_str(), e.art_row.c_str(), e.status.str().c_str());
    }
    CHECK_EQ(out.size(), static_cast<size_t>(5));
    CHECK_UNIT(by["UNIT_WARRIOR"].status, UnitCode::Ok);   // the real clips bind (pose.h)
    CHECK_EQ(by["UNIT_WARRIOR"].art_row, std::string("ART_DEF_UNIT__WARRIOR"));
    CHECK_UNIT(by["UNIT_TRIREME"].status, UnitCode::Ok);
    CHECK_UNIT(by["UNIT_BADMODEL"].status, UnitCode::ModelNotFound);
    CHECK_UNIT(by["UNIT_NOMEMBERS"].status, UnitCode::NoMembers);
    CHECK_UNIT(by["UNIT_NOART"].status, UnitCode::NoArtRow);
    for (size_t i = 1; i < out.size(); ++i) CHECK(out[i - 1].unit_type <= out[i].unit_type);
}

// A pose source whose clip check fails: the pre-check reports it per type.
class FailingClips : public PoseSource {
public:
    UnitStatus rows(const PoseRequest&, Float4*) override { return UnitStatus::fail(UnitCode::ClipNotFound, "x"); }
    UnitStatus checkClips(const MemberArt& m, const UnitModel&) override {
        return UnitStatus::fail(UnitCode::CurveUnsupported, "format 13 in " + m.member);
    }
};

C5R_TEST(precheck_clip_failures) {
    units_test::InstallFixture fx;
    UnitRenderer r;
    CHECK_OK(r.open(nullptr, fx.gf, fx.db, fx.exe));
    FailingClips fc;
    r.setPoseSource(&fc);
    std::vector<UnitTypeStatus> out;
    CHECK_OK(r.precheck(out));
    for (auto& e : out)
        if (e.unit_type == "UNIT_WARRIOR" || e.unit_type == "UNIT_TRIREME") CHECK_UNIT(e.status, UnitCode::CurveUnsupported);
    UnitRenderer closed;
    CHECK_ERR(closed.precheck(out), c5r::Err::NotFound);
    CHECK(out.empty());
}


// Issue 4460 (epic 0375; MC ruling 2026-11-16): a clip the art file names but no package ships is the warning
// ClipMissingInInstall on an Ok row (the stock Assyrian siege tower: its .fxsxml names U_Assyrian_Siege_Tower_Charge_Attack.gr2,
// which the game does not ship); a file that is a real fault (a .fxsxml no package ships, UNIT_BADMODEL) stays a failure.
C5R_TEST(precheck_clip_missing_in_install_is_a_warning) {
    units_test::InstallFixture fx;
    c5m::Error e;
    fx.db.add("Units", units_test::tsv({{"Type", "UnitArtInfo", "UnitArtInfoCulturalVariation", "UnitArtInfoEraVariation"},
                            {"UNIT_ASSYRIAN_SIEGE_TOWER", "ART_DEF_UNIT_U_ASSYRIAN_SIEGE_TOWER", "0", "0"},
                            {"UNIT_BADMODEL", "ART_DEF_UNIT_BADMODEL", "0", "0"}}),
              &e);
    fx.db.add("ArtDefine_UnitInfos", units_test::tsv({{"Type", "Formation", "DamageStates"},
                                          {"ART_DEF_UNIT_U_ASSYRIAN_SIEGE_TOWER", "\\N", "3"},
                                          {"ART_DEF_UNIT_BADMODEL", "\\N", "1"}}),
              &e);
    fx.db.add("ArtDefine_UnitInfoMemberInfos",
              units_test::tsv({{"UnitInfoType", "UnitMemberInfoType", "NumMembers"},
                   {"ART_DEF_UNIT_U_ASSYRIAN_SIEGE_TOWER", "ART_DEF_UNIT_MEMBER_U_ASSYRIAN_SIEGE_TOWER", "1"},
                   {"ART_DEF_UNIT_BADMODEL", "ART_DEF_UNIT_MEMBER_BADMODEL", "1"}}),
              &e);
    fx.db.add("ArtDefine_UnitMemberInfos",
              units_test::tsv({{"Type", "Scale", "ZOffset", "Domain", "Model", "MaterialTypeTag"},
                   {"ART_DEF_UNIT_MEMBER_U_ASSYRIAN_SIEGE_TOWER", "0.10000000149011612", "\\N", "", "U_Assyrian_Siege_Tower.fxsxml", "WOOD"},
                   {"ART_DEF_UNIT_MEMBER_BADMODEL", "0.1", "\\N", "", "Assets/Units/None/No_Such_Unit_4352.fxsxml", ""}}),
              &e);
    UnitRenderer r;
    CHECK_OK(r.open(nullptr, fx.gf, fx.db, fx.exe));
    std::vector<UnitTypeStatus> out;
    CHECK_OK(r.precheck(out));
    std::map<std::string, UnitTypeStatus> by;
    for (auto& s : out) {
        by[s.unit_type] = s;
        std::printf("precheck %s %s %s warnings %zu\n", s.unit_type.c_str(), s.art_row.c_str(), s.status.str().c_str(),
                    s.status.warnings.size());
        for (auto& w : s.status.warnings) std::printf("  warning %s\n", w.c_str());
    }
    CHECK_EQ(out.size(), static_cast<size_t>(2));
    const UnitTypeStatus& st = by["UNIT_ASSYRIAN_SIEGE_TOWER"];
    CHECK_UNIT(st.status, UnitCode::Ok);   // drawn: the clips that exist bind
    CHECK_EQ(st.art_row, std::string("ART_DEF_UNIT_U_ASSYRIAN_SIEGE_TOWER"));
    size_t clipWarnings = 0;
    for (auto& w : st.status.warnings)
        if (w.find("ClipMissingInInstall: ART_DEF_UNIT_U_ASSYRIAN_SIEGE_TOWER: member ART_DEF_UNIT_MEMBER_U_ASSYRIAN_SIEGE_TOWER: "
                   "animation file ") == 0 && w.find("Charge_Attack") != std::string::npos)
            ++clipWarnings;
    CHECK_EQ(clipWarnings, static_cast<size_t>(1));
    CHECK_UNIT(by["UNIT_BADMODEL"].status, UnitCode::ModelNotFound);   // a real fault stays one
    CHECK(by["UNIT_BADMODEL"].status.warnings.empty());
}

}  // namespace

C5R_TEST_MAIN
