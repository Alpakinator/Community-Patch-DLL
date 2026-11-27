// render/tests/unitanim/test_bridge_driver.cpp - issue 4404 part B: the offline driver (driver.h; unit-controller.md
// 4 and 14). A synthetic .c5anim with its start scene played through the controller on the test doubles (FakeArt,
// FakeFactory, parsed machines given through PlayOptions::prepare), its outputs read back; the tool's exit codes by
// class through driverMain (usage, bad file, missing scene, missing install) and playTimeline (units failed, output,
// frame refused).
#include <cstdio>
#include <direct.h>
#include <string>
#include <vector>

#include "anim_testkit.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/unitanim/bridge.h"
#include "c5r/unitanim/driver.h"
#include "c5r/units/scene_file.h"

using namespace anim_test;

namespace {

std::string dir() {
    std::string d = c5r::test::workDir() + "/unitanim_4404";
    return d;
}
std::wstring w(const std::string& s) { return c5r::gamefs::fromUtf8(s); }

bool writeText(const std::string& path, const std::string& t) {
    std::FILE* f = _wfopen(w(path).c_str(), L"wb");
    if (!f) return false;
    const bool ok = std::fwrite(t.data(), 1, t.size(), f) == t.size();
    return std::fclose(f) == 0 && ok;
}
bool readText(const std::string& path, std::string& out) {
    std::FILE* f = _wfopen(w(path).c_str(), L"rb");
    if (!f) return false;
    out.clear();
    char b[4096];
    size_t n;
    while ((n = std::fread(b, 1, sizeof b, f)) > 0) out.append(b, n);
    std::fclose(f);
    return true;
}
size_t lines(const std::string& t) {
    size_t n = 0;
    for (char c : t) n += c == '\n';
    return n;
}

// Start scene: player 0, unit 0/7 of type W (3 figures) and, with `bad`, unit 0/8 of a type the art double lacks.
std::string sceneText(bool bad) {
    std::string s = "c5units\t1\nscene\town\t10\nplayer\t0\tPLAYERCOLOR_ROME\tCIVILIZATION_ROME\tERA_ANCIENT\n"
                    "unit\t0\t7\tW\t-\t3\t4\t-\t-\t-\t2\t20\t0\t100\t-\t0\t1\t0\t0\n";
    if (bad) s += "unit\t0\t8\tQ\t-\t5\t4\t-\t-\t-\t2\t21\t0\t100\t-\t0\t1\t0\t0\n";
    return s;
}

// 40 frames at 1/30 s; fortify on at frame 2 (seq 3), off at frame 5; attack at frame 10 on figure 1.
std::string timelineText(const std::string& scene) {
    std::string t = "c5anim\t1\nscene\t" + scene + "\t42\tfree\n";
    for (int i = 0; i < 40; ++i) {
        if (i == 2) t += "ev\t0\t7\tfortify\t1\n";
        if (i == 5) t += "ev\t0\t7\tfortify\t0\n";
        if (i == 10) t += "ev\t0\t7\taction\tattack\t-\t1\n";
        t += "frame\t" + std::to_string(i + 1) + "\t0.0333333333\t" + std::to_string(0.5 + i / 30.0) + "\t-\n";
    }
    return t;
}

struct Doubles {
    MemFiles files;
    FakeArt art;
    FakeFactory graphs;
    BridgeTables bt;
    ActionBridge bridge{bt};
    PlayOptions opt;
    Doubles() {
        c5r::units::UnitArt a;
        a.art_row = "ART_W";
        a.members.push_back(memberArt("M", 3, "omni.fsmxml", warriorClips()));
        art.rows["W"] = a;
        graphs.info[1000] = ClipInfo{6.0f, true};
        graphs.info[2000] = ClipInfo{6.0f, true};
        graphs.info[1040] = ClipInfo{5.0f, false};
        graphs.info[1520] = ClipInfo{4.0f, true};
        graphs.info[1100] = ClipInfo{1.0f, false};
        bt.words["M"] = {"Idle", "Attack", "Run", "Fortify", "Death", "CombatReady"};
        opt.controller.bridge = &bridge;
        opt.prepare = [](Controller& c) {
            CHECK_ANIM(c.logic()->putMachine("omni.fsmxml", idleMachine(0.0f)), AnimCode::Ok);
            c.logic()->putGraph("idle.dge", dag({1000, 2000}));
            c.logic()->putGraph("idlecont.dge", dag({1000, 2000}));
            c.logic()->putGraph("fidget.dge", dag({1040}));
            c.logic()->putGraph("fortify.dge", dag({1520}));
            c.logic()->putGraph("attack.dge", dag({1100}));
        };
    }
    DriverExit play(const std::string& tlPath, const std::string& out, PlaySummary& sum, std::string& msg) {
        Timeline tl;
        c5r::units::UnitScene start;
        TimelineStatus ts = readTimelineFile(w(tlPath), tl, start);
        CHECK(ts.ok());
        return playTimeline(tl, start, w(out), files, art, graphs, opt, sum, msg);
    }
};

int runMain(std::vector<std::string> args) {
    std::vector<char*> argv;
    static std::string prog = "unitanim";
    argv.push_back(&prog[0]);
    for (auto& a : args) argv.push_back(&a[0]);
    return driverMain(static_cast<int>(argv.size()), argv.data());
}

void setUp() {
    _wmkdir(w(c5r::test::workDir()).c_str());
    _wmkdir(w(dir()).c_str());
    CHECK(writeText(dir() + "/good.c5units", sceneText(false)));
    CHECK(writeText(dir() + "/bad.c5units", sceneText(true)));
    CHECK(writeText(dir() + "/good.c5anim", timelineText("good.c5units")));
    CHECK(writeText(dir() + "/bad_unit.c5anim", timelineText("bad.c5units")));
    CHECK(writeText(dir() + "/no_scene.c5anim", timelineText("missing.c5units")));
    CHECK(writeText(dir() + "/bad_header.c5anim", "c5anim\t2\nscene\t-\t1\tfree\n"));
}

}  // namespace

C5R_TEST(plays_a_synthetic_timeline_and_writes_the_outputs) {
    setUp();
    Doubles d;
    PlaySummary sum;
    std::string msg;
    const std::string out = dir() + "/out_good";
    CHECK_EQ(static_cast<int>(d.play(dir() + "/good.c5anim", out, sum, msg)), 0);
    CHECK_EQ(sum.frames, static_cast<size_t>(40));
    CHECK_EQ(sum.units_max, static_cast<size_t>(1));
    CHECK_EQ(sum.program_rows, static_cast<size_t>(120));   // 40 frames x 3 figures x 1 sample node
    CHECK_EQ(sum.status_rows, static_cast<size_t>(40));
    CHECK_EQ(sum.c5units_frames, static_cast<size_t>(40));
    CHECK_EQ(sum.failed_units, static_cast<size_t>(0));
    std::string p, s, t;
    CHECK(readText(out + "/programs.csv", p));
    CHECK(readText(out + "/status.csv", s));
    CHECK(readText(out + "/triggers.csv", t));
    CHECK_EQ(lines(p), static_cast<size_t>(121));
    CHECK_EQ(lines(s), static_cast<size_t>(41));
    CHECK_EQ(lines(t), static_cast<size_t>(1));
    CHECK(p.rfind("frame,seq,present_time,player,id,figure,node,kind,clip,ec,t,a,b,w,mask,state\n", 0) == 0);
    // frame 2 (seq 3): fortify entered at t 0 on every figure, state 7, ec 1520
    CHECK(p.find("\n2,3,0.566667,0,7,0,0,sample,w_fortify_idle.gr2,1520,0,-,-,-,-,7\n") != std::string::npos);
    // frame 10 (seq 11): figure 1 attacks (state 9, ec 1100); figure 0 does not
    CHECK(p.find("\n10,11,0.833333,0,7,1,0,sample,w_attacka.gr2,1100,0,-,-,-,-,9\n") != std::string::npos);
    CHECK(p.find("\n10,11,0.833333,0,7,0,0,sample,w_attacka.gr2") == std::string::npos);
    CHECK(s.find("\n39,40,0,7,Ok,\n") != std::string::npos);
    // a .c5units frame reads back with the programs' (clip, t)
    c5r::units::UnitScene sc;
    CHECK(c5r::units::readSceneFile(w(out + "/frames/0000000011.c5units"), sc).ok());
    CHECK_EQ(sc.units.size(), static_cast<size_t>(1));
    CHECK_EQ(sc.units[0].figures.size(), static_cast<size_t>(3));
    CHECK_EQ(sc.units[0].figures[1].clip, std::string("w_attacka.gr2"));
    CHECK_EQ(sc.units[0].figures[1].t, 0.0f);
    CHECK_EQ(sc.players.size(), static_cast<size_t>(1));
    CHECK_EQ(static_cast<int>(sc.units[0].facing), 2);
    // with --no-c5units nothing goes into frames/
    d.opt.c5units = false;
    CHECK_EQ(static_cast<int>(d.play(dir() + "/good.c5anim", dir() + "/out_noframes", sum, msg)), 0);
    CHECK_EQ(sum.c5units_frames, static_cast<size_t>(0));
}

C5R_TEST(a_failed_unit_is_named_and_exits_units_failed) {
    setUp();
    Doubles d;
    PlaySummary sum;
    std::string msg;
    const std::string out = dir() + "/out_bad";
    CHECK_EQ(static_cast<int>(d.play(dir() + "/bad_unit.c5anim", out, sum, msg)),
             static_cast<int>(DriverExit::UnitsFailed));
    CHECK_EQ(sum.failed_units, static_cast<size_t>(1));
    CHECK(sum.first_failure.find("0/8 UnitFailed") == 0);
    CHECK_EQ(sum.c5units_frames, static_cast<size_t>(0));   // no frame with a failed unit is written
    CHECK_EQ(sum.frames, static_cast<size_t>(40));          // the other unit is still played
    std::string s;
    CHECK(readText(out + "/status.csv", s));
    CHECK(s.find("\n0,1,0,8,UnitFailed,Q: NoArtRow\n") != std::string::npos);
}

C5R_TEST(output_failed_and_frame_refused) {
    setUp();
    Doubles d;
    PlaySummary sum;
    std::string msg;
    // an output folder below a file
    CHECK_EQ(static_cast<int>(d.play(dir() + "/good.c5anim", dir() + "/good.c5units/out", sum, msg)),
             static_cast<int>(DriverExit::OutputFailed));
    CHECK(!msg.empty());
    // a frame the controller refuses as a whole (a negative dt, which the file reader would refuse first)
    Timeline tl;
    TimelineFrame f;
    f.seq = 1;
    f.dt = -1;
    tl.frames.push_back(f);
    c5r::units::UnitScene none;
    CHECK_EQ(static_cast<int>(playTimeline(tl, none, w(dir() + "/out_neg"), d.files, d.art, d.graphs, d.opt, sum,
                                           msg)),
             static_cast<int>(DriverExit::FrameRefused));
    CHECK(msg.find("BadEvent") != std::string::npos);
}

C5R_TEST(tool_exit_codes_by_class) {
    setUp();
    CHECK_EQ(runMain({}), 2);
    CHECK_EQ(runMain({"render"}), 2);
    CHECK_EQ(runMain({"play", "--bogus"}), 2);
    CHECK_EQ(runMain({"play", "--timeline"}), 2);
    CHECK_EQ(runMain({"play", "--timeline", dir() + "/good.c5anim"}), 2);
    CHECK_EQ(runMain({"play", "--fade", "m:6:3"}), 2);               // 4406: rules as data, malformed
    CHECK_EQ(runMain({"play", "--skip", "5:0:1:x:6"}), 2);
    const std::vector<std::string> rest = {"--game", dir() + "/no_install", "--db", dir() + "/no.db", "--out",
                                           dir() + "/out_tool"};
    auto with = [&](const std::string& tl) {
        std::vector<std::string> a = {"play", "--timeline", tl};
        a.insert(a.end(), rest.begin(), rest.end());
        return runMain(a);
    };
    CHECK_EQ(with(dir() + "/bad_header.c5anim"), 3);     // a bad file
    CHECK_EQ(with(dir() + "/no_scene.c5anim"), 3);       // its start scene is missing
    CHECK_EQ(with(dir() + "/absent.c5anim"), 3);         // the file is missing
    CHECK_EQ(with(dir() + "/good.c5anim"), 4);           // a good file, no install
    CHECK_EQ(std::string(driverExitName(DriverExit::FrameRefused)), std::string("FrameRefused"));
}

C5R_TEST_MAIN
