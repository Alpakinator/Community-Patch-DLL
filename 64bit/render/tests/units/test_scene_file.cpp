// render/tests/units/test_scene_file.cpp - issue 4355: the .c5units reader and writer (unit-renderer.md section 3).
// Positive: every field of UnitScene round-trips bit-exactly through text and through a file. Negative: one test per
// refusal code asserting the code, the line number, the message and that the scene is left empty.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include "c5r/units/scene_file.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::units;

namespace {

uint32_t bits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

UnitScene sample() {
    UnitScene s;
    s.position_source = "exe";
    s.water_level = 12.3456789f;
    PlayerInput a;
    a.player = 0;
    a.colour = "PLAYERCOLOR_ROME";
    a.art_style = "ARTSTYLE_GRECO_ROMAN";
    a.era = "ERA_MEDIEVAL";
    PlayerInput b;
    b.player = 63;
    b.colour = "PLAYERCOLOR_BARBARIAN";
    b.art_style = "ARTSTYLE_BARBARIAN";
    b.era = "ERA_ANCIENT";
    s.players = {a, b};
    UnitInput u;
    u.player = 0;
    u.id = 8193;
    u.unit_type = "UNIT_ROMAN_LEGION";
    u.art_row = "";                 // resolve
    u.plot_x = 31;
    u.plot_y = 17;
    u.has_root = true;
    u.root_x = 0.1f;
    u.root_y = -0.0f;
    u.root_z = 3.40282347e38f;
    u.facing = 5;
    u.formation_seed = 4294967295u;
    u.damage = 37;
    u.max_hp = 100;
    u.alive = 7;
    u.flags = 127;
    u.fade_from = 1.0f;
    u.fade_to = 1e-30f;
    u.fade = 0.333333343f;
    FigureInput f0;
    f0.figure = 0;
    f0.clip = "IDLE";
    f0.t = 0.0f;
    FigureInput f1;
    f1.figure = 63;
    f1.clip = "FIDGET_2";
    f1.t = 1.23456789f;
    u.figures = {f0, f1};
    UnitInput v;
    v.player = 63;
    v.id = 0;
    v.unit_type = "UNIT_BARBARIAN_WARRIOR";
    v.art_row = "ART_DEF_UNIT_BARBARIAN_EURO";
    v.plot_x = 0;
    v.plot_y = 32767;
    v.has_root = false;
    v.facing = 0;
    v.formation_seed = 0;
    v.damage = 0;
    v.max_hp = 1;
    v.alive = -1;
    v.flags = 0;
    s.units = {u, v};
    return s;
}

void checkEqual(const UnitScene& a, const UnitScene& b) {
    CHECK_EQ(a.format, b.format);
    CHECK(a.position_source == b.position_source);
    CHECK_EQ(bits(a.water_level), bits(b.water_level));
    CHECK_EQ(a.players.size(), b.players.size());
    for (size_t i = 0; i < a.players.size() && i < b.players.size(); ++i) {
        CHECK_EQ(a.players[i].player, b.players[i].player);
        CHECK(a.players[i].colour == b.players[i].colour);
        CHECK(a.players[i].art_style == b.players[i].art_style);
        CHECK(a.players[i].era == b.players[i].era);
    }
    CHECK_EQ(a.units.size(), b.units.size());
    for (size_t i = 0; i < a.units.size() && i < b.units.size(); ++i) {
        const UnitInput& x = a.units[i];
        const UnitInput& y = b.units[i];
        CHECK_EQ(x.player, y.player);
        CHECK_EQ(x.id, y.id);
        CHECK(x.unit_type == y.unit_type);
        CHECK(x.art_row == y.art_row);
        CHECK_EQ(x.plot_x, y.plot_x);
        CHECK_EQ(x.plot_y, y.plot_y);
        CHECK_EQ(x.has_root, y.has_root);
        if (x.has_root) {
            CHECK_EQ(bits(x.root_x), bits(y.root_x));
            CHECK_EQ(bits(x.root_y), bits(y.root_y));
            CHECK_EQ(bits(x.root_z), bits(y.root_z));
        }
        CHECK_EQ(x.facing, y.facing);
        CHECK_EQ(x.formation_seed, y.formation_seed);
        CHECK_EQ(x.damage, y.damage);
        CHECK_EQ(x.max_hp, y.max_hp);
        CHECK_EQ(x.alive, y.alive);
        CHECK_EQ(x.flags, y.flags);
        CHECK_EQ(bits(x.fade_from), bits(y.fade_from));
        CHECK_EQ(bits(x.fade_to), bits(y.fade_to));
        CHECK_EQ(bits(x.fade), bits(y.fade));
        CHECK_EQ(x.figures.size(), y.figures.size());
        for (size_t k = 0; k < x.figures.size() && k < y.figures.size(); ++k) {
            CHECK_EQ(x.figures[k].figure, y.figures[k].figure);
            CHECK(x.figures[k].clip == y.figures[k].clip);
            CHECK_EQ(bits(x.figures[k].t), bits(y.figures[k].t));
        }
    }
}

const char* kHead = "c5units\t1\nscene\town\t0\nplayer\t0\tPC\tAS\tERA\n";
const char* kUnit = "unit\t0\t1\tUNIT_X\t-\t3\t4\t-\t-\t-\t2\t20\t0\t100\t-\t0\t1\t0\t0\n";

// The negative check: code, line, a message fragment, and an empty scene (pre-filled so that a reset is visible).
void refused(const std::string& text, SceneFileErr code, size_t line, const char* fragment) {
    UnitScene out = sample();
    SceneFileStatus s = parseScene(text, out);
    if (s.code != code || s.line != line || s.msg.find(fragment) == std::string::npos)
        test::fail(__FILE__, __LINE__, std::string("want ") + sceneFileErrName(code) + " at line " +
                                           std::to_string(line) + " with \"" + fragment + "\", got " + s.str());
    CHECK(!s.ok());
    CHECK(out.players.empty());
    CHECK(out.units.empty());
    CHECK(out.position_source == "own");
    CHECK_EQ(out.water_level, 0.0f);
    CHECK(s.str().find(sceneFileErrName(code)) == 0);
}

}  // namespace

C5R_TEST(round_trip_every_field) {
    UnitScene in = sample();
    std::string text;
    SceneFileStatus w = writeScene(in, text);
    CHECK(w.ok());
    CHECK(text.compare(0, 10, "c5units\t1\n") == 0);
    UnitScene back;
    SceneFileStatus r = parseScene(text, back);
    if (!r.ok()) test::fail(__FILE__, __LINE__, r.str());
    checkEqual(in, back);
    // The text is a fixed point: writing what was read gives the same bytes.
    std::string again;
    CHECK(writeScene(back, again).ok());
    CHECK(again == text);
}

C5R_TEST(round_trip_file_crlf_comments_bom) {
    UnitScene in = sample();
    std::string text;
    CHECK(writeScene(in, text).ok());
    // CRLF, a BOM, comments and blank lines are accepted by the reader.
    std::string crlf = "\xEF\xBB\xBF# a comment\r\n\r\n   \t\r\n";
    for (char ch : text) {
        if (ch == '\n') crlf += "\r\n";
        else crlf += ch;
    }
    crlf += "# trailing comment";
    UnitScene back;
    SceneFileStatus r = parseScene(crlf, back);
    if (!r.ok()) test::fail(__FILE__, __LINE__, r.str());
    checkEqual(in, back);

    std::string dir = test::workDir();
    std::wstring path(dir.begin(), dir.end());
    path += L"\\test_scene_file.c5units";
    CHECK(writeSceneFile(path, in).ok());
    UnitScene fromFile;
    SceneFileStatus rf = readSceneFile(path, fromFile);
    if (!rf.ok()) test::fail(__FILE__, __LINE__, rf.str());
    checkEqual(in, fromFile);
    _wremove(path.c_str());
}

// issue 4464 (4458's rule (g), unit-server-scene.md 12.1): the DLL's facing 7 (NO_DIRECTION after a Lua InitUnit
// without a facing), 6 (NUM_DIRECTION_TYPES) and -1 (stock's NO_DIRECTION) are written and read back unchanged.
C5R_TEST(facing_no_direction_round_trip) {
    for (int facing : {7, 6, -1, 0, 5}) {
        UnitScene in = sample();
        in.units[0].facing = static_cast<int8_t>(facing);
        std::string text;
        SceneFileStatus w = writeScene(in, text);
        if (!w.ok()) test::fail(__FILE__, __LINE__, "facing " + std::to_string(facing) + ": " + w.str());
        const std::string field = "\t" + std::to_string(facing) + "\t" + std::to_string(in.units[0].formation_seed) + "\t";
        CHECK(text.find(field) != std::string::npos);
        UnitScene back;
        SceneFileStatus r = parseScene(text, back);
        if (!r.ok()) test::fail(__FILE__, __LINE__, "facing " + std::to_string(facing) + ": " + r.str());
        checkEqual(in, back);
        CHECK_EQ(static_cast<int>(back.units[0].facing), facing);
    }
    UnitScene out;
    std::string unit7 = kUnit;
    unit7.replace(unit7.find("\t2\t20\t"), 6, "\t7\t20\t");
    SceneFileStatus s = parseScene(std::string(kHead) + unit7, out);
    if (!s.ok()) test::fail(__FILE__, __LINE__, s.str());
    CHECK_EQ(static_cast<int>(out.units[0].facing), 7);
}

C5R_TEST(minimal_scene_defaults) {
    UnitScene out;
    SceneFileStatus s = parseScene(std::string(kHead) + kUnit + "figure\t2\tIDLE\t0.5\n", out);
    if (!s.ok()) test::fail(__FILE__, __LINE__, s.str());
    CHECK_EQ(out.units.size(), static_cast<size_t>(1));
    CHECK(out.units[0].art_row.empty());
    CHECK(!out.units[0].has_root);
    CHECK_EQ(out.units[0].alive, static_cast<int16_t>(-1));
    CHECK_EQ(out.units[0].figures.size(), static_cast<size_t>(1));
    CHECK_EQ(out.units[0].figures[0].t, 0.5f);
}

C5R_TEST(refuse_bad_header) {
    refused("", SceneFileErr::BadHeader, 0, "no c5units header");
    refused("# only a comment\n", SceneFileErr::BadHeader, 0, "no c5units header");
    refused("scene\town\t0\n", SceneFileErr::BadHeader, 1, "first record must be");
    refused("# c\nc5units\t2\n", SceneFileErr::BadHeader, 2, "format 2");
    refused("c5units 1\n", SceneFileErr::BadHeader, 1, "first record must be");
    UnitScene sc = sample();
    sc.format = 2;
    std::string text = "x";
    SceneFileStatus w = writeScene(sc, text);
    CHECK(w.code == SceneFileErr::BadHeader);
    CHECK(text.empty());
}

C5R_TEST(refuse_bad_record) {
    refused(std::string(kHead) + "units\t1\n", SceneFileErr::BadRecord, 4, "unknown record kind \"units\"");
    refused(std::string(kHead) + "player\t1\tPC\tAS\n", SceneFileErr::BadRecord, 4, "player record has 4 fields, wants 5");
    refused(std::string(kHead) + kUnit + "figure\t0\tIDLE\t0\textra\n", SceneFileErr::BadRecord, 5,
            "figure record has 5 fields, wants 4");
    refused("c5units\t1\nplayer\t0\tPC\tAS\tERA\n", SceneFileErr::BadRecord, 0, "no scene record");
    refused(std::string(kHead) + "unit\t0\t1\tUNIT_X\n", SceneFileErr::BadRecord, 4, "unit record has 4 fields, wants 19");
}

C5R_TEST(refuse_bad_value) {
    refused("c5units\t1\nscene\tserver\t0\n", SceneFileErr::BadValue, 2, "position_source \"server\"");
    refused("c5units\t1\nscene\town\t1.5x\n", SceneFileErr::BadValue, 2, "water_level \"1.5x\"");
    refused("c5units\t1\nscene\town\tnan\n", SceneFileErr::BadValue, 2, "water_level \"nan\"");
    refused("c5units\t1\nscene\town\t1e999\n", SceneFileErr::BadValue, 2, "water_level \"1e999\"");
    refused("c5units\t1\nscene\town\t 1\n", SceneFileErr::BadValue, 2, "water_level \" 1\"");
    refused("c5units\t1\nscene\town\t0\nplayer\t64\tPC\tAS\tERA\n", SceneFileErr::BadValue, 3, "player \"64\"");
    refused("c5units\t1\nscene\town\t0\nplayer\t0\t\tAS\tERA\n", SceneFileErr::BadValue, 3, "colour \"\"");
    const std::string h = kHead;
    // issue 4464: facing -1..7 (the DLL's NO_DIRECTION 7 included); 8, -2 and a dash are refused
    refused(h + "unit\t0\t1\tUNIT_X\t-\t3\t4\t-\t-\t-\t8\t20\t0\t100\t-\t0\t1\t0\t0\n", SceneFileErr::BadValue, 4,
            "facing \"8\": an integer -1..7");
    refused(h + "unit\t0\t1\tUNIT_X\t-\t3\t4\t-\t-\t-\t-2\t20\t0\t100\t-\t0\t1\t0\t0\n", SceneFileErr::BadValue, 4,
            "facing \"-2\"");
    refused(h + "unit\t0\t1\tUNIT_X\t-\t3\t4\t-\t-\t-\t-\t20\t0\t100\t-\t0\t1\t0\t0\n", SceneFileErr::BadValue, 4,
            "facing \"-\"");
    refused(h + "unit\t0\t1\tUNIT_X\t-\t3\t4\t1\t-\t-\t2\t20\t0\t100\t-\t0\t1\t0\t0\n", SceneFileErr::BadValue, 4,
            "all three or none");
    refused(h + "unit\t0\t1\tUNIT_X\t-\t3\t4\t-\t-\t-\t2\t20\t101\t100\t-\t0\t1\t0\t0\n", SceneFileErr::BadValue, 4,
            "damage \"101\"");
    refused(h + "unit\t0\t1\tUNIT_X\t-\t3\t4\t-\t-\t-\t2\t20\t0\t100\t-\t128\t1\t0\t0\n", SceneFileErr::BadValue, 4,
            "flags \"128\"");
    refused(h + "unit\t0\t1\tUNIT_X\t-\t3\t4\t-\t-\t-\t2\t4294967296\t0\t100\t-\t0\t1\t0\t0\n", SceneFileErr::BadValue,
            4, "seed \"4294967296\"");
    refused(h + "unit\t0\t+1\tUNIT_X\t-\t3\t4\t-\t-\t-\t2\t20\t0\t100\t-\t0\t1\t0\t0\n", SceneFileErr::BadValue, 4,
            "id \"+1\"");
    refused(h + kUnit + "figure\t64\tIDLE\t0\n", SceneFileErr::BadValue, 5, "figure \"64\"");
    refused(h + kUnit + "figure\t0\tIDLE\t-1\n", SceneFileErr::BadValue, 5, "t \"-1\"");
    refused(h + kUnit + "figure\t0\tID\x01LE\t0\n", SceneFileErr::BadValue, 5, "clip \"ID");
    UnitScene sc = sample();
    sc.units[0].figures[0].clip = "A\tB";
    std::string text;
    SceneFileStatus w = writeScene(sc, text);
    CHECK(w.code == SceneFileErr::BadValue);
    CHECK(w.msg.find("clip") != std::string::npos);
    sc = sample();
    sc.units[1].alive = -2;
    CHECK(writeScene(sc, text).code == SceneFileErr::BadValue);
    sc = sample();
    sc.units[1].facing = 9;   // a range rule: the writer refuses it through its read-back
    w = writeScene(sc, text);
    CHECK(w.code == SceneFileErr::BadValue);
    CHECK(w.msg.find("facing") != std::string::npos);
    CHECK(text.empty());
}

C5R_TEST(refuse_duplicate) {
    refused(std::string(kHead) + "scene\texe\t0\n", SceneFileErr::Duplicate, 4, "second scene record");
    refused(std::string(kHead) + "player\t0\tPC\tAS\tERA\n", SceneFileErr::Duplicate, 4, "second player record");
    refused(std::string(kHead) + kUnit + kUnit, SceneFileErr::Duplicate, 5, "second unit (0, 1)");
    refused(std::string(kHead) + kUnit + "figure\t3\tIDLE\t0\nfigure\t3\tRUN\t0\n", SceneFileErr::Duplicate, 6,
            "second figure 3 in unit (0, 1)");
    refused(std::string(kHead) + "c5units\t1\n", SceneFileErr::Duplicate, 4, "second c5units header");
    UnitScene sc = sample();
    sc.units[1] = sc.units[0];
    std::string text;
    CHECK(writeScene(sc, text).code == SceneFileErr::Duplicate);
}

C5R_TEST(refuse_orphan) {
    refused(std::string(kHead) + "figure\t0\tIDLE\t0\n", SceneFileErr::Orphan, 4, "figure record before any unit");
    // A unit whose player has no player record (player 5; only player 0 declared), line 4.
    refused(std::string(kHead) + "unit\t5\t1\tUNIT_X\t-\t3\t4\t-\t-\t-\t2\t20\t0\t100\t-\t0\t1\t0\t0\n" + kUnit,
            SceneFileErr::Orphan, 4, "no player record for player 5");
    UnitScene sc = sample();
    sc.players.pop_back();
    std::string text;
    CHECK(writeScene(sc, text).code == SceneFileErr::Orphan);
}

C5R_TEST(refuse_too_large) {
    std::string longLine = std::string(kHead) + "# " + std::string(4095, 'x') + "\n";
    refused(longLine, SceneFileErr::TooLarge, 4, "line of 4097 bytes");
    std::string units = kHead;
    for (int i = 0; i <= 4096; ++i)
        units += "unit\t0\t" + std::to_string(i) + "\tUNIT_X\t-\t3\t4\t-\t-\t-\t2\t20\t0\t100\t-\t0\t1\t0\t0\n";
    refused(units, SceneFileErr::TooLarge, 4 + 4096, "more than 4096 units");
    std::string figs = std::string(kHead) + kUnit;   // 64 distinct indices fill the unit; a 65th cannot be distinct,
    for (int i = 0; i < 64; ++i)                     // so the limit is reached through the index range (BadValue)
        figs += "figure\t" + std::to_string(i) + "\tIDLE\t0\n";
    UnitScene ok;
    CHECK(parseScene(figs, ok).ok());
    CHECK_EQ(ok.units[0].figures.size(), static_cast<size_t>(64));
    std::string huge(kSceneMaxFile + 1, '#');
    refused(huge, SceneFileErr::TooLarge, 0, "file of");
    UnitScene sc = sample();
    const UnitInput filler = sc.units[1];
    sc.units.resize(4097, filler);
    for (size_t i = 0; i < sc.units.size(); ++i) sc.units[i].id = static_cast<int32_t>(i + 1);
    std::string text;
    CHECK(writeScene(sc, text).code == SceneFileErr::TooLarge);
}

C5R_TEST(refuse_io) {
    std::string dir = test::workDir();
    std::wstring missing(dir.begin(), dir.end());
    missing += L"\\no_such_folder_4355\\x.c5units";
    UnitScene out = sample();
    SceneFileStatus s = readSceneFile(missing, out);
    CHECK(s.code == SceneFileErr::Io);
    CHECK(s.msg.find("cannot open") != std::string::npos);
    CHECK(out.units.empty());
    CHECK(writeSceneFile(missing, sample()).code == SceneFileErr::Io);
}

C5R_TEST_MAIN
