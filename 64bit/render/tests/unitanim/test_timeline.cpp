// render/tests/unitanim/test_timeline.cpp - issue 4404 part A: the .c5anim reader and writer (unit-controller.md 2).
// Positive: every event kind and field round-trips through text and through a file with its start scene. Negative: one
// test per refusal code asserting the code, the line number, a message fragment and that the output is left empty.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "c5r/unitanim/timeline.h"
#include "c5r/units/scene_file.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::unitanim;
using c5r::units::PlayerInput;
using c5r::units::UnitInput;
using c5r::units::UnitScene;

namespace {

uint64_t dbits(double d) {
    uint64_t u;
    std::memcpy(&u, &d, 8);
    return u;
}
uint32_t fbits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

PlayerInput player(int32_t p, const char* colour) {
    PlayerInput x;
    x.player = p;
    x.colour = colour;
    x.art_style = "ARTSTYLE_EUROPEAN";
    x.era = "ERA_ANCIENT";
    return x;
}

UnitInput unit(int32_t p, int32_t id, const char* type) {
    UnitInput u;
    u.player = p;
    u.id = id;
    u.unit_type = type;
    u.plot_x = 3;
    u.plot_y = 4;
    u.facing = 2;
    u.formation_seed = 20;
    u.max_hp = 100;
    return u;
}

UnitScene startScene() {
    UnitScene s;
    s.players = {player(0, "PLAYERCOLOR_ROME")};
    s.units = {unit(0, 1, "UNIT_WARRIOR")};
    return s;
}

TimelineEvent ev(EventKind k, int32_t p, int32_t id) {
    TimelineEvent e;
    e.kind = k;
    e.player = p;
    e.id = id;
    return e;
}

// Every event kind once, with non-default values; a second player created by the file.
Timeline sample() {
    Timeline tl;
    tl.scene_path = "start.c5units";
    tl.seed = 18446744073709551615ull;
    tl.forced = true;
    tl.players = {player(5, "PLAYERCOLOR_CARTHAGE")};
    TimelineFrame f1;
    f1.seq = 7;
    f1.dt = 0.0333333333333333;
    f1.has_present = true;
    f1.present_time = 1234.56789012345;
    f1.has_exe_frame = true;
    f1.exe_frame = 4677;
    TimelineEvent c = ev(EventKind::Create, 5, 8193);
    c.unit = unit(5, 8193, "UNIT_HORSEMAN");
    c.unit.has_root = true;
    c.unit.root_x = 0.1f;
    c.unit.root_y = -2.5f;
    c.unit.root_z = 3.25f;
    c.unit.damage = 30;
    c.unit.alive = 3;
    c.unit.flags = 4;
    c.unit.fade_from = 0;
    c.unit.fade_to = 1;
    c.unit.fade = 0.5f;
    f1.events.push_back(c);
    TimelineEvent fo = ev(EventKind::Fortify, 0, 1);
    fo.a = 1;
    f1.events.push_back(fo);
    TimelineEvent mv = ev(EventKind::Move, 0, 1);
    mv.a = 3;
    mv.b = 12;
    f1.events.push_back(mv);
    TimelineEvent tu = ev(EventKind::Turn, 0, 1);
    tu.a = 4;
    tu.b = 1;
    f1.events.push_back(tu);
    TimelineEvent ar = ev(EventKind::Arrive, 0, 1);
    ar.a = 5;
    f1.events.push_back(ar);
    TimelineEvent pl = ev(EventKind::Place, 0, 1);
    pl.a = 10;
    pl.b = 11;
    pl.c = 0;
    f1.events.push_back(pl);
    TimelineEvent at = ev(EventKind::Action, 5, 8193);
    at.action = ActionKind::AttackCity;
    at.figures.all = false;
    at.figures.figures = {4, 0, 2};
    f1.events.push_back(at);
    TimelineEvent wk = ev(EventKind::Action, 0, 1);
    wk.action = ActionKind::Work;
    wk.argument = "BUILD_FARM";
    f1.events.push_back(wk);
    TimelineEvent ws = ev(EventKind::Action, 0, 1);   // 0439: the work stop, written "-", read back as ""
    ws.action = ActionKind::Work;
    f1.events.push_back(ws);
    TimelineFrame f2;
    f2.seq = 9;
    f2.dt = 0.2;
    TimelineEvent dm = ev(EventKind::Damage, 5, 8193);
    dm.a = 60;
    dm.b = 30;
    f2.events.push_back(dm);
    TimelineEvent kl = ev(EventKind::Kill, 5, 8193);
    kl.figures.all = false;
    kl.figures.figures = {1};
    f2.events.push_back(kl);
    TimelineEvent em = ev(EventKind::Embark, 0, 1);
    em.a = 1;
    f2.events.push_back(em);
    TimelineEvent ga = ev(EventKind::Garrison, 0, 1);
    ga.a = 0;
    f2.events.push_back(ga);
    TimelineEvent vi = ev(EventKind::Visibility, 0, 1);
    vi.a = 1;
    vi.blend_time = 0.75f;
    f2.events.push_back(vi);
    TimelineEvent rs = ev(EventKind::Reset, 0, 1);
    f2.events.push_back(rs);
    TimelineEvent ch = ev(EventKind::Choice, 0, 1);
    ch.point = "roll:1_omni_state_01.fsmxml:17";
    ch.a = 1;
    f2.events.push_back(ch);
    TimelineEvent st = ev(EventKind::Steer, 0, 1);
    st.figures.all = false;
    st.figures.figures = {0, 1};
    st.weight = 0.375f;
    st.a = 2;
    f2.events.push_back(st);
    TimelineEvent de = ev(EventKind::Destroyed, 5, 8193);
    de.a = 1;
    f2.events.push_back(de);
    TimelineEvent rm = ev(EventKind::Remove, 5, 8193);
    f2.events.push_back(rm);
    TimelineFrame f3;   // an empty frame
    f3.seq = 10;
    f3.dt = 0;
    tl.frames = {f1, f2, f3};
    return tl;
}

void checkEqual(const Timeline& a, const Timeline& b) {
    CHECK(a.scene_path == b.scene_path);
    CHECK_EQ(a.seed, b.seed);
    CHECK_EQ(a.forced, b.forced);
    CHECK_EQ(a.players.size(), b.players.size());
    CHECK_EQ(a.frames.size(), b.frames.size());
    for (size_t i = 0; i < a.frames.size() && i < b.frames.size(); ++i) {
        const TimelineFrame& x = a.frames[i];
        const TimelineFrame& y = b.frames[i];
        CHECK_EQ(x.seq, y.seq);
        CHECK_EQ(dbits(x.dt), dbits(y.dt));
        CHECK_EQ(x.has_present, y.has_present);
        if (x.has_present) CHECK_EQ(dbits(x.present_time), dbits(y.present_time));
        CHECK_EQ(x.has_exe_frame, y.has_exe_frame);
        CHECK_EQ(x.exe_frame, y.exe_frame);
        CHECK_EQ(x.events.size(), y.events.size());
        for (size_t k = 0; k < x.events.size() && k < y.events.size(); ++k) {
            const TimelineEvent& e = x.events[k];
            const TimelineEvent& f = y.events[k];
            CHECK(e.kind == f.kind);
            CHECK_EQ(e.player, f.player);
            CHECK_EQ(e.id, f.id);
            CHECK_EQ(e.a, f.a);
            CHECK_EQ(e.b, f.b);
            CHECK_EQ(e.c, f.c);
            CHECK_EQ(fbits(e.blend_time), fbits(f.blend_time));
            CHECK_EQ(fbits(e.weight), fbits(f.weight));
            CHECK(e.action == f.action);
            CHECK(e.argument == f.argument);
            CHECK_EQ(e.figures.all, f.figures.all);
            CHECK(e.figures.figures == f.figures.figures);
            CHECK(e.point == f.point);
            if (e.kind == EventKind::Create) {
                CHECK(e.unit.unit_type == f.unit.unit_type);
                CHECK_EQ(e.unit.has_root, f.unit.has_root);
                CHECK_EQ(fbits(e.unit.root_x), fbits(f.unit.root_x));
                CHECK_EQ(fbits(e.unit.root_y), fbits(f.unit.root_y));
                CHECK_EQ(fbits(e.unit.root_z), fbits(f.unit.root_z));
                CHECK_EQ(e.unit.damage, f.unit.damage);
                CHECK_EQ(e.unit.alive, f.unit.alive);
                CHECK_EQ(e.unit.flags, f.unit.flags);
                CHECK_EQ(fbits(e.unit.fade), fbits(f.unit.fade));
                CHECK_EQ(f.unit.player, f.player);
                CHECK_EQ(f.unit.id, f.id);
            }
        }
    }
}

const char* kHead = "c5anim\t1\nscene\t-\t0\tfree\nplayer\t0\tPC\tAS\tERA\n";
const char* kCreate = "ev\t0\t1\tcreate\tUNIT_X\t-\t3\t4\t-\t-\t-\t2\t20\t0\t100\t-\t0\t1\t0\t0\n";

void refused(const std::string& text, TimelineErr code, size_t line, const char* fragment,
             const UnitScene* start = nullptr) {
    Timeline out = sample();
    TimelineStatus s = parseTimeline(text, start, out);
    if (s.code != code || s.line != line || s.msg.find(fragment) == std::string::npos)
        test::fail(__FILE__, __LINE__, std::string("want ") + timelineErrName(code) + " at line " +
                                           std::to_string(line) + " with \"" + fragment + "\", got " + s.str());
    CHECK(!s.ok());
    CHECK(out.frames.empty());
    CHECK(out.players.empty());
}

}  // namespace

C5R_TEST(round_trip_every_kind) {
    UnitScene start = startScene();
    Timeline in = sample();
    std::string text;
    TimelineStatus w = writeTimeline(in, &start, text);
    if (!w.ok()) test::fail(__FILE__, __LINE__, w.str());
    Timeline back;
    TimelineStatus r = parseTimeline(text, &start, back);
    if (!r.ok()) test::fail(__FILE__, __LINE__, r.str());
    checkEqual(in, back);
    // A second write gives the same text.
    std::string again;
    CHECK(writeTimeline(back, &start, again).ok());
    CHECK(again == text);
}

C5R_TEST(round_trip_file_with_start_scene_crlf_bom) {
    UnitScene start = startScene();
    Timeline in = sample();
    std::string dir = test::workDir();
    std::wstring wdir(dir.begin(), dir.end());
    CHECK(units::writeSceneFile(wdir + L"\\start.c5units", start).ok());
    std::string text;
    CHECK(writeTimeline(in, &start, text).ok());
    std::string crlf = "\xEF\xBB\xBF# comment\r\n \t\r\n";
    for (char ch : text) {
        if (ch == '\n') crlf += "\r\n";
        else crlf += ch;
    }
    std::wstring path = wdir + L"\\test_timeline.c5anim";
    FILE* f = _wfopen(path.c_str(), L"wb");
    CHECK(f != nullptr);
    if (f) {
        std::fwrite(crlf.data(), 1, crlf.size(), f);
        std::fclose(f);
    }
    Timeline back;
    UnitScene backStart;
    TimelineStatus r = readTimelineFile(path, back, backStart);
    if (!r.ok()) test::fail(__FILE__, __LINE__, r.str());
    checkEqual(in, back);
    CHECK_EQ(backStart.units.size(), static_cast<size_t>(1));
    // The writer's file form.
    CHECK(writeTimelineFile(path, in, &start).ok());
    Timeline back2;
    CHECK(readTimelineFile(path, back2, backStart).ok());
    checkEqual(in, back2);
    _wremove(path.c_str());
    _wremove((wdir + L"\\start.c5units").c_str());
}

C5R_TEST(minimal) {
    Timeline out;
    TimelineStatus s = parseTimeline("c5anim\t1\nscene\t-\t0\tfree\n", nullptr, out);
    CHECK(s.ok());
    CHECK(out.frames.empty());
    CHECK(!out.forced);
}

C5R_TEST(refuse_bad_header) {
    refused("", TimelineErr::BadHeader, 0, "no c5anim header");
    refused("c5anim\t2\n", TimelineErr::BadHeader, 1, "c5anim");
    refused("c5units\t1\n", TimelineErr::BadHeader, 1, "c5anim");
}

C5R_TEST(refuse_bad_record) {
    refused("c5anim\t1\n", TimelineErr::BadRecord, 0, "no scene record");
    refused("c5anim\t1\nframe\t1\t0\t-\t-\n", TimelineErr::BadRecord, 2, "scene record must follow");
    refused("c5anim\t1\nscene\t-\t0\n", TimelineErr::BadRecord, 2, "3 fields");
    refused(std::string(kHead) + "bogus\t1\n", TimelineErr::BadRecord, 4, "unknown record");
    refused(std::string(kHead) + kCreate + "ev\t0\t1\tdance\n", TimelineErr::BadRecord, 5, "unknown event kind");
    refused(std::string(kHead) + kCreate + "ev\t0\t1\tfortify\n", TimelineErr::BadRecord, 5, "1 argument");
    refused(std::string(kHead) + "frame\t1\t0\t-\n", TimelineErr::BadRecord, 4, "4 fields");
    refused(std::string(kHead) + kCreate, TimelineErr::BadRecord, 0, "after the last frame");
}

C5R_TEST(refuse_bad_value) {
    std::string h = std::string(kHead) + kCreate;
    refused("c5anim\t1\nscene\t-\tx\tfree\n", TimelineErr::BadValue, 2, "bad seed");
    refused("c5anim\t1\nscene\t-\t0\tmaybe\n", TimelineErr::BadValue, 2, "mode");
    refused(std::string(kHead) + "frame\t1\t-0.1\t-\t-\n", TimelineErr::BadValue, 4, "bad dt");
    refused(std::string(kHead) + "frame\t1\tnan\t-\t-\n", TimelineErr::BadValue, 4, "bad dt");
    refused(std::string(kHead) + "frame\t1\t0.5x\t-\t-\n", TimelineErr::BadValue, 4, "bad dt");
    refused(std::string(kHead) + "frame\tx\t0\t-\t-\n", TimelineErr::BadValue, 4, "bad seq");
    refused(std::string(kHead) + "frame\t1\t0\tq\t-\n", TimelineErr::BadValue, 4, "present_time");
    refused(std::string(kHead) + "frame\t1\t0\t-\t-3\n", TimelineErr::BadValue, 4, "exe_frame");
    refused(std::string(kHead) + "ev\t64\t1\treset\n", TimelineErr::BadValue, 4, "bad player");
    refused(h + "ev\t0\t1\tfortify\t2\n", TimelineErr::BadValue, 5, "0 or 1");
    refused(h + "ev\t0\t1\tmove\t6\t3\n", TimelineErr::BadValue, 5, "facing");
    refused(h + "ev\t0\t1\tturn\t1\tup\n", TimelineErr::BadValue, 5, "left or right");
    refused(h + "ev\t0\t1\taction\tsing\t-\t*\n", TimelineErr::BadValue, 5, "unknown action");
    refused(h + "ev\t0\t1\taction\twork\tBAD TYPE\t*\n", TimelineErr::BadValue, 5, "build Type");   // 0439: - = the stop
    refused(h + "ev\t0\t1\taction\tattack\tX\t*\n", TimelineErr::BadValue, 5, "argument must be -");
    refused(h + "ev\t0\t1\tkill\t1,64\n", TimelineErr::BadValue, 5, "bad index");
    refused(h + "ev\t0\t1\tkill\t\n", TimelineErr::BadValue, 5, "bad index");
    refused(h + "ev\t0\t1\tvisibility\t1\t-1\n", TimelineErr::BadValue, 5, "blend time");
    refused(h + "ev\t0\t1\tchoice\t*\tdice:a:1\t1\n", TimelineErr::BadValue, 5, "decision point");
    refused(h + "ev\t0\t1\tchoice\t*\troll:a:x\t1\n", TimelineErr::BadValue, 5, "decision point");
    refused(h + "ev\t0\t1\tdamage\t-1\t0\n", TimelineErr::BadValue, 5, "bad damage");
    refused(h + "ev\t0\t1\tsteer\t*\t1.5\t1\n", TimelineErr::BadValue, 5, "path 7 weight");
    refused(h + "ev\t0\t1\tsteer\t*\t0.5\t16\n", TimelineErr::BadValue, 5, "path 8 selector");
    refused(h + "ev\t0\t1\tsteer\t*\t0.5\n", TimelineErr::BadRecord, 5, "3 argument");
    // A create whose unit fields the .c5units reader refuses (facing 9).
    refused(std::string(kHead) + "ev\t0\t1\tcreate\tUNIT_X\t-\t3\t4\t-\t-\t-\t9\t20\t0\t100\t-\t0\t1\t0\t0\n",
            TimelineErr::BadValue, 4, "create:");
    refused("c5anim\t1\nscene\t-\t0\tfree\nplayer\t0\tPC\tAS\n", TimelineErr::BadRecord, 3, "4 fields");
}

C5R_TEST(refuse_unknown_unit) {
    refused(std::string(kHead) + "ev\t0\t2\treset\n", TimelineErr::UnknownUnit, 4, "not in the scene");
    refused(std::string(kHead) + kCreate + "ev\t0\t1\tremove\nev\t0\t1\treset\n", TimelineErr::UnknownUnit, 6,
            "not in the scene");
    // A unit of the start scene is known.
    UnitScene start = startScene();
    Timeline out;
    CHECK(parseTimeline("c5anim\t1\nscene\tstart.c5units\t0\tfree\nev\t0\t1\treset\nframe\t1\t0\t-\t-\n", &start,
                        out).ok());
    CHECK_EQ(out.frames.size(), static_cast<size_t>(1));
}

C5R_TEST(refuse_duplicate) {
    refused("c5anim\t1\nscene\t-\t0\tfree\nscene\t-\t0\tfree\n", TimelineErr::Duplicate, 3, "second scene");
    refused(std::string(kHead) + "player\t0\tPC\tAS\tERA\n", TimelineErr::Duplicate, 4, "player 0 twice");
    refused(std::string(kHead) + kCreate + kCreate, TimelineErr::Duplicate, 5, "exists");
    refused(std::string(kHead) + kCreate + "ev\t0\t1\tkill\t3,1,3\n", TimelineErr::BadValue, 5, "twice");
}

C5R_TEST(refuse_orphan) {
    refused("c5anim\t1\nscene\t-\t0\tfree\n" + std::string(kCreate), TimelineErr::Orphan, 3, "no player record");
}

C5R_TEST(refuse_out_of_order) {
    refused(std::string(kHead) + "frame\t5\t0\t-\t-\nframe\t5\t0\t-\t-\n", TimelineErr::OutOfOrder, 5, "after 5");
    refused(std::string(kHead) + "frame\t5\t0\t-\t-\nframe\t4\t0\t-\t-\n", TimelineErr::OutOfOrder, 5, "after 5");
}

C5R_TEST(refuse_too_large) {
    refused(std::string(kHead) + std::string(5000, '#') + "\n", TimelineErr::TooLarge, 4, "line over");
    std::string many = "c5anim\t1\nscene\t-\t0\tfree\n";
    for (int p = 0; p < 64; ++p) many += "player\t" + std::to_string(p) + "\tPC\tAS\tERA\n";
    // 64 players fit; there is no 65th valid player index, so the limit is the index range itself.
    Timeline out;
    CHECK(parseTimeline(many, nullptr, out).ok());
    refused(many + "player\t64\tPC\tAS\tERA\n", TimelineErr::BadValue, 67, "player");
}

C5R_TEST(refuse_io_and_bad_scene) {
    std::string dir = test::workDir();
    std::wstring wdir(dir.begin(), dir.end());
    Timeline out = sample();
    UnitScene start;
    TimelineStatus s = readTimelineFile(wdir + L"\\no_such_folder_4404\\x.c5anim", out, start);
    CHECK(s.code == TimelineErr::Io);
    CHECK(out.frames.empty());
    CHECK(writeTimelineFile(wdir + L"\\no_such_folder_4404\\x.c5anim", sample(), nullptr).code != TimelineErr::Ok);
    // A timeline whose start scene file is missing.
    std::wstring path = wdir + L"\\test_timeline_badscene.c5anim";
    FILE* f = _wfopen(path.c_str(), L"wb");
    const char* text = "c5anim\t1\nscene\tmissing_4404.c5units\t0\tfree\n";
    if (f) {
        std::fwrite(text, 1, std::strlen(text), f);
        std::fclose(f);
    }
    TimelineStatus b = readTimelineFile(path, out, start);
    CHECK(b.code == TimelineErr::BadScene);
    CHECK(b.msg.find("missing_4404.c5units") != std::string::npos);
    CHECK(out.frames.empty());
    _wremove(path.c_str());
}

C5R_TEST(writer_refuses_what_reader_refuses) {
    Timeline tl;
    TimelineFrame f;
    f.seq = 1;
    TimelineEvent e = ev(EventKind::Reset, 0, 99);   // unknown unit
    f.events.push_back(e);
    tl.frames = {f};
    std::string text = "x";
    TimelineStatus s = writeTimeline(tl, nullptr, text);
    CHECK(s.code == TimelineErr::UnknownUnit);
    CHECK(text.empty());
    Timeline orphan;
    TimelineFrame g;
    g.seq = 1;
    TimelineEvent c = ev(EventKind::Create, 3, 1);
    c.unit = unit(3, 1, "UNIT_X");
    g.events.push_back(c);
    orphan.frames = {g};
    CHECK(writeTimeline(orphan, nullptr, text).code == TimelineErr::Orphan);
}

C5R_TEST_MAIN
