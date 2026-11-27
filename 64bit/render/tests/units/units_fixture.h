// units_fixture.h - the install-backed fixture of the 4352 renderer tests (epic 0041): the install's files
// (C5R_GAME_DIR) and the EXE (C5R_EXE), with a small hand-written database naming the stock warrior and trireme art
// plus rows that fail on purpose. No game data in the repository.
#pragma once
#include <memory>
#include <string>

#include "c5models/art.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/units/units.h"
#include "units_testkit.h"

namespace units_test {

inline void addTables(c5m::TsvTables& db) {
    c5m::Error e;
    db.add("Units", tsv({{"Type", "UnitArtInfo", "UnitArtInfoCulturalVariation", "UnitArtInfoEraVariation"},
                         {"UNIT_WARRIOR", "ART_DEF_UNIT__WARRIOR", "0", "0"},
                         {"UNIT_TRIREME", "ART_DEF_UNIT_TRIREME", "0", "0"},
                         {"UNIT_BADMODEL", "ART_DEF_UNIT_BADMODEL", "0", "0"},
                         {"UNIT_NOMEMBERS", "ART_DEF_UNIT_NOMEMBERS", "0", "0"},
                         {"UNIT_NOART", "ART_DEF_UNIT_NOT_A_ROW", "0", "0"}}),
           &e);
    db.add("ArtDefine_UnitInfos", tsv({{"Type", "Formation", "DamageStates"},
                                       {"ART_DEF_UNIT__WARRIOR", "Warrior", "1"},
                                       {"ART_DEF_UNIT_TRIREME", "\\N", "3"},
                                       {"ART_DEF_UNIT_BADMODEL", "\\N", "1"},
                                       {"ART_DEF_UNIT_NOMEMBERS", "\\N", "1"}}),
           &e);
    db.add("ArtDefine_UnitInfoMemberInfos",
           tsv({{"UnitInfoType", "UnitMemberInfoType", "NumMembers"},
                {"ART_DEF_UNIT__WARRIOR", "ART_DEF_UNIT_MEMBER_WARRIOR_V2", "3"},
                {"ART_DEF_UNIT__WARRIOR", "ART_DEF_UNIT_MEMBER_WARRIOR", "6"},
                {"ART_DEF_UNIT__WARRIOR", "ART_DEF_UNIT_MEMBER_WARRIOR_V4", "3"},
                {"ART_DEF_UNIT_TRIREME", "ART_DEF_UNIT_MEMBER_TRIREME", "1"},
                {"ART_DEF_UNIT_BADMODEL", "ART_DEF_UNIT_MEMBER_BADMODEL", "1"}}),
           &e);
    db.add("ArtDefine_UnitMemberInfos",
           tsv({{"Type", "Scale", "ZOffset", "Domain", "Model", "MaterialTypeTag"},
                {"ART_DEF_UNIT_MEMBER_WARRIOR_V2", "0.14000000059604645", "\\N", "", "Assets/Units/Warrior/Warrior_V2.fxsxml", "CLOTH"},
                {"ART_DEF_UNIT_MEMBER_WARRIOR", "0.14000000059604645", "\\N", "", "Assets/Units/Warrior/Warrior.fxsxml", "CLOTH"},
                {"ART_DEF_UNIT_MEMBER_WARRIOR_V4", "0.14000000059604645", "\\N", "", "Assets/Units/Warrior/Warrior_V4.fxsxml", "CLOTH"},
                {"ART_DEF_UNIT_MEMBER_TRIREME", "0.07000000029802322", "\\N", "Sea", "Assets/Units/Trireme/Trireme.fxsxml", "WOOD"},
                {"ART_DEF_UNIT_MEMBER_BADMODEL", "0.1", "\\N", "", "Assets/Units/None/No_Such_Unit_4352.fxsxml", ""}}),
           &e);
    db.add("Eras", tsv({{"Type", "ArtPrefix"}, {"ERA_ANCIENT", "ANCIENT"}}), &e);
    db.add("Civilizations", tsv({{"Type", "ArtStyleType", "ArtStyleSuffix"},
                                 {"CIVILIZATION_POLYNESIA", "ARTSTYLE_POLYNESIAN", "_MED"}}),
           &e);
    db.add("MinorCivilizations", tsv({{"Type", "ArtStyleType", "ArtStyleSuffix"}}), &e);
    db.add("PlayerColors", tsv({{"Type", "SecondaryColor"}, {"PLAYERCOLOR_POLYNESIA", "COLOR_P"}}), &e);
    db.add("Colors", tsv({{"Type", "Red", "Green", "Blue", "Alpha"}, {"COLOR_P", "0.851", "0.349", "0.0", "1.0"}}), &e);
}

struct InstallFixture {
    c5r::gamefs::GameFiles gf;
    c5m::TsvTables db;
    std::wstring exe;
    InstallFixture() {
        const std::string dir = c5r::test::gameDir();
        exe = c5r::gamefs::fromUtf8(c5r::test::exePath());
        CHECK_OK(gf.open(c5r::gamefs::fromUtf8(dir)));
        addTables(db);
    }
};

}  // namespace units_test
